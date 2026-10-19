"""생성 GLB를 리깅된 스켈레탈 메시로 다듬는다 — 위층 사람의 기는 몸.

refine_generated.py가 정적 메시 한 자세를 만들었다면, 여기서는 같은 원본에서
뼈대와 동작까지 만든다. 순서:

1. GLB를 읽어 한 메시로 합치고 yaw로 돌려 머리를 +X에 둔다(폰의 앞이 +X).
2. 한 축을 실제 cm에 맞춰 균일 배율, 원점은 바닥 중심.
3. 저밀도 사본: 복셀 리메시로 조각난 석고 껍질을 한 표면으로 녹이고,
   스무딩·데시메이트. 생성물이 부서진 조각처럼 보이던 이유가 이 껍질이다.
4. 고밀도 → 저밀도로 BaseColor·Normal(DX)·ORM 굽기 (refine_generated와 같다).
5. 저밀도 메시의 정점 분포에서 뼈대를 세운다. 기는 자세라서 뼈 자리는
   바운드 비율과 좌우 군집으로 잡는다. 자동 웨이트.
6. 동작 넷을 키프레임으로 짠다 — Crawl(루프)·Listen(루프)·Bang(1회)·Lunge(1회).
   이동은 폰이 하므로 전부 제자리 동작이다.
7. 미리보기(정지 + 기는 동작 네 프레임), Y 반전 좌표계에서 FBX(뼈대+메시+
   동작 전부), manifest("skeletal": true).

    blender -b --factory-startup --python Scripts/blender/rig_crawler.py -- \\
        --glb Content/SourceArt/Generated/ListenerEntityCrawl/trellis1024-s56-side/raw/pbr_00001_.glb \\
        --name SK_ListenerEntityCrawl --length 190 --yaw 0 --voxel-remesh 0.007

--no-bake 를 주면 굽기와 FBX 없이 뼈대·동작 미리보기만 찍는다(빠른 반복용).
"""

import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import bpy  # noqa: E402
import bmesh  # noqa: E402
from mathutils import Vector  # noqa: E402

import ig_blender_lib as ig  # noqa: E402

FPS = 30


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--glb", required=True)
    parser.add_argument("--name", default="SK_ListenerEntityCrawl")
    parser.add_argument("--length", type=float, default=190.0, help="X 크기 cm (머리끝~발끝)")
    parser.add_argument("--yaw", type=float, default=0.0)
    parser.add_argument("--rot-x", type=float, default=0.0)
    parser.add_argument("--rot-y", type=float, default=0.0)
    parser.add_argument("--voxel-remesh", type=float, default=0.007)
    parser.add_argument("--smooth-iterations", type=int, default=3)
    parser.add_argument("--budget", type=int, default=12000)
    parser.add_argument("--texture-size", type=int, default=2048)
    parser.add_argument("--out", default=None)
    parser.add_argument("--no-bake", action="store_true")
    parser.add_argument("--notes", default="")
    parser.add_argument("--landmarks", help="다각도 렌더에서 확인한 관절 좌표 JSON")
    parser.add_argument("--keep-largest", action="store_true", help="몸과 떨어진 생성 조각을 제거한다")
    return parser.parse_args(argv)


# --------------------------------------------------------------------------
# 원본 읽기 (refine_generated와 같은 규약)
# --------------------------------------------------------------------------

def import_glb(path):
    bpy.ops.import_scene.gltf(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise RuntimeError("GLB에 메시가 없다")
    joined = ig.join(meshes, "generated_high")
    for o in list(bpy.context.scene.objects):
        if o.type != "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    color_names = [a.name for a in joined.data.color_attributes]
    if color_names:
        for mat in joined.data.materials:
            if mat is None or not mat.use_nodes:
                continue
            for node in mat.node_tree.nodes:
                if node.type == "VERTEX_COLOR" and node.layer_name not in color_names:
                    node.layer_name = color_names[0]
    return joined


def bake_rotation(ob, rx, ry, rz):
    ob.rotation_euler = (math.radians(rx), math.radians(ry), math.radians(rz))
    bpy.context.view_layer.update()
    bm = ig._world_bm(ob)
    me = bpy.data.meshes.new(ob.data.name)
    bm.to_mesh(me)
    bm.free()
    for mat in ob.data.materials:
        me.materials.append(mat)
    old = ob.data
    ob.data = me
    ob.rotation_euler = (0.0, 0.0, 0.0)
    bpy.data.meshes.remove(old)
    if me.color_attributes:
        me.color_attributes.active_color = me.color_attributes[0]


def fit_and_ground(ob, length_cm):
    lo, hi = ig.bounds(ob)
    size = hi - lo
    scale = (length_cm / 100.0) / max(size.x, 1e-6)
    for v in ob.data.vertices:
        v.co *= scale
    lo, hi = ig.bounds(ob)
    center = (lo + hi) * 0.5
    shift = Vector((center.x, center.y, lo.z))
    for v in ob.data.vertices:
        v.co -= shift
    ob.data.update()
    return scale


# --------------------------------------------------------------------------
# 뼈대 자리 잡기
# --------------------------------------------------------------------------

def vertex_array(ob):
    import numpy as np
    n = len(ob.data.vertices)
    buf = np.empty(n * 3, dtype=np.float32)
    ob.data.vertices.foreach_get("co", buf)
    return buf.reshape(n, 3)


def find_landmarks(ob):
    """기는 자세의 관절 자리. 전부 미터, 저작 좌표(머리 +X, 위 +Z).

    통계로 잡을 수 있는 것은 잡고(팔꿈치·주먹·다리 군집), 통계가 흔들리는 것은
    전신 길이의 비율로 잡는다. 기는 사람의 비율은 서 있는 사람과 같다.
    """
    import numpy as np
    pts = vertex_array(ob)
    x, y, z = pts[:, 0], pts[:, 1], pts[:, 2]
    xmin, xmax = float(x.min()), float(x.max())
    length = xmax - xmin
    zmax = float(z.max())

    def slab(x0, x1, mask=None):
        m = (x >= x0) & (x < x1)
        if mask is not None:
            m &= mask
        return pts[m]

    def mid_z(sel, fallback):
        return float((sel[:, 2].min() + sel[:, 2].max()) * 0.5) if len(sel) else fallback

    core = np.abs(y) < 0.11

    # 머리: 끝 22 cm. 목은 그 뒤 6 cm.
    head_x0 = xmax - 0.23
    head = slab(head_x0, xmax + 0.01, core)
    head_top = float(head[:, 2].max()) if len(head) else zmax
    head_bottom = float(np.percentile(head[:, 2], 8)) if len(head) else head_top - 0.22
    head_center = Vector((head_x0 + 0.115, 0.0, (head_top + head_bottom) * 0.5))
    neck_x = head_x0 - 0.05
    neck = slab(neck_x - 0.03, neck_x + 0.03, core)
    neck_pt = Vector((neck_x, 0.0, mid_z(neck, head_center.z - 0.12)))

    # 가슴/어깨: 목에서 20 cm 뒤. 어깨 폭은 그 구간의 팔 아닌 몸통 폭.
    chest_x = neck_x - 0.20
    chest = slab(chest_x - 0.04, chest_x + 0.04, core)
    chest_pt = Vector((chest_x, 0.0, mid_z(chest, neck_pt.z - 0.1)))
    torso = slab(chest_x - 0.10, chest_x + 0.10)
    if len(torso):
        half_w = float(np.percentile(np.abs(torso[:, 1]), 70))
    else:
        half_w = 0.2
    shoulder_half = max(0.14, min(half_w, 0.24))
    shoulder_z = chest_pt.z + 0.07

    # 팔: 몸통 폭 바깥의 정점. 바닥에 닿은 것(z < 0.14)이 아래팔.
    arms = {}
    for side, sign in (("l", 1.0), ("r", -1.0)):
        sel = pts[(np.sign(y) == sign) & (np.abs(y) > shoulder_half * 0.85)
                  & (x > chest_x - 0.45) & (x < chest_x + 0.55)]
        floor = sel[sel[:, 2] < 0.14] if len(sel) else sel
        if len(floor) > 50:
            fx = floor[:, 0]
            elbow_x = float(np.percentile(fx, 6))
            hand_x = float(np.percentile(fx, 96))
            fy = float(np.median(floor[:, 1]))
            elbow = Vector((elbow_x + 0.02, fy, 0.07))
            hand = Vector((hand_x - 0.06, fy, 0.05))
        else:
            fy = sign * (shoulder_half + 0.05)
            elbow = Vector((chest_x - 0.05, fy, 0.07))
            hand = Vector((chest_x + 0.25, fy, 0.05))
        shoulder = Vector((chest_x + 0.02, sign * shoulder_half, shoulder_z))
        arms[side] = (shoulder, elbow, hand)

    # 골반: 발끝에서 전신의 45%. 다리 군집은 그 뒤에서 좌우로 갈린다.
    hip_x = xmin + length * 0.46
    pelvis = slab(hip_x - 0.05, hip_x + 0.05, np.abs(y) < 0.16)
    pelvis_pt = Vector((hip_x, 0.0, mid_z(pelvis, 0.16)))
    spine1 = Vector((hip_x + (chest_x - hip_x) * 0.33, 0.0, 0.0))
    spine2 = Vector((hip_x + (chest_x - hip_x) * 0.66, 0.0, 0.0))
    for pt in (spine1, spine2):
        sel = slab(pt.x - 0.04, pt.x + 0.04, core)
        pt.z = mid_z(sel, (pelvis_pt.z + chest_pt.z) * 0.5)

    legs = {}
    for side, sign in (("l", 1.0), ("r", -1.0)):
        sel = pts[(np.sign(y) == sign) & (x < hip_x) & (np.abs(y) > 0.02)]
        if len(sel) > 50:
            ly = float(np.median(sel[:, 1]))
            foot_x = float(np.percentile(sel[:, 0], 2))
        else:
            ly = sign * 0.10
            foot_x = xmin
        hip = Vector((hip_x - 0.06, sign * 0.10, pelvis_pt.z - 0.02))
        knee_x = hip.x - (hip.x - foot_x) * 0.5
        knee_sel = sel[(sel[:, 0] > knee_x - 0.05) & (sel[:, 0] < knee_x + 0.05)] if len(sel) else sel
        knee = Vector((knee_x, ly, mid_z(knee_sel, 0.10)))
        ankle_x = foot_x + 0.16
        ankle_sel = sel[(sel[:, 0] > ankle_x - 0.04) & (sel[:, 0] < ankle_x + 0.04)] if len(sel) else sel
        ankle = Vector((ankle_x, ly, mid_z(ankle_sel, 0.08)))
        toe = Vector((foot_x + 0.02, ly, 0.03))
        legs[side] = (hip, knee, ankle, toe)

    marks = {
        "pelvis": pelvis_pt, "spine_01": spine1, "spine_02": spine2, "spine_03": chest_pt,
        "neck": neck_pt, "head": head_center, "head_top": Vector((xmax - 0.02, 0.0, head_top)),
        "arms": arms, "legs": legs,
    }
    ig.log(f"landmarks: length={length:.3f} head={tuple(round(v, 3) for v in head_center)} "
           f"chest={tuple(round(v, 3) for v in chest_pt)} pelvis={tuple(round(v, 3) for v in pelvis_pt)} "
           f"shoulder_half={shoulder_half:.3f}")
    for side in ("l", "r"):
        s, e, h = arms[side]
        hp, k, a, t = legs[side]
        ig.log(f"  arm_{side}: shoulder={tuple(round(v, 3) for v in s)} elbow={tuple(round(v, 3) for v in e)} "
               f"hand={tuple(round(v, 3) for v in h)}")
        ig.log(f"  leg_{side}: hip={tuple(round(v, 3) for v in hp)} knee={tuple(round(v, 3) for v in k)} "
               f"ankle={tuple(round(v, 3) for v in a)} toe={tuple(round(v, 3) for v in t)}")
    return marks


def build_armature(name, marks):
    """뼈 이름은 UE 마네킹 관례를 따른다. root는 바닥 원점."""
    arm_data = bpy.data.armatures.new(name + "_Armature")
    arm = ig._link(bpy.data.objects.new(name + "_Armature", arm_data))
    ig.set_active(arm)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm_data.edit_bones

    def bone(bname, head, tail, parent=None, connect=False, roll_up=Vector((0.0, 0.0, 1.0))):
        b = eb.new(bname)
        b.head = Vector(head)
        b.tail = Vector(tail)
        if parent is not None:
            b.parent = eb[parent]
            b.use_connect = connect
        # 뼈의 Z축을 위로 세워 두면 로컬 X 회전이 곧 「숙이기/젖히기」다.
        # 세로에 가까운 뼈(머리·손)는 그 정렬이 퇴화하므로 앞(+X)에 맞춘다 —
        # 그래도 로컬 X는 좌우로 남는다.
        direction = (b.tail - b.head).normalized()
        if abs(direction.z) > 0.85:
            b.align_roll(Vector((1.0, 0.0, 0.0)))
        else:
            b.align_roll(roll_up)
        return b

    bone("root", (0.0, 0.0, 0.0), (0.0, 0.0, 0.08))
    bone("pelvis", marks["pelvis"], marks["spine_01"], "root")
    bone("spine_01", marks["spine_01"], marks["spine_02"], "pelvis", True)
    bone("spine_02", marks["spine_02"], marks["spine_03"], "spine_01", True)
    bone("spine_03", marks["spine_03"], marks["neck"], "spine_02", True)
    bone("neck", marks["neck"], marks["head"], "spine_03", True)
    bone("head", marks["head"], marks["head_top"], "neck", True)
    for side in ("l", "r"):
        s, e, h = marks["arms"][side]
        bone(f"clavicle_{side}", marks["spine_03"] + Vector((0.0, 0.0, 0.04)), s, "spine_03")
        bone(f"upperarm_{side}", s, e, f"clavicle_{side}", True)
        bone(f"lowerarm_{side}", e, h, f"upperarm_{side}", True)
        bone(f"hand_{side}", h, h + Vector((0.08, 0.0, 0.0)), f"lowerarm_{side}", True)
        hp, k, a, t = marks["legs"][side]
        bone(f"thigh_{side}", hp, k, "pelvis")
        bone(f"calf_{side}", k, a, f"thigh_{side}", True)
        bone(f"foot_{side}", a, t, f"calf_{side}", True)
    bpy.ops.object.mode_set(mode="OBJECT")
    for pb in arm.pose.bones:
        pb.rotation_mode = "XYZ"
    return arm


def load_landmarks(path):
    """검토한 관절 좌표를 쓴다. 비대칭 자세를 바운드 비율로 다시 추정하지 않는다."""
    import json
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    marks = {key: Vector(data[key]) for key in
             ("pelvis", "spine_01", "spine_02", "spine_03", "neck", "head", "head_top")}
    for key in ("arms", "legs"):
        marks[key] = {side: tuple(Vector(p) for p in data[key][side]) for side in ("l", "r")}
    return marks


BONE_RADIUS = {
    "pelvis": 0.15, "spine_01": 0.15, "spine_02": 0.15, "spine_03": 0.15,
    "neck": 0.07, "head": 0.11,
    "clavicle_l": 0.05, "clavicle_r": 0.05,
    "upperarm_l": 0.055, "upperarm_r": 0.055,
    "lowerarm_l": 0.05, "lowerarm_r": 0.05,
    "hand_l": 0.045, "hand_r": 0.045,
    "thigh_l": 0.085, "thigh_r": 0.085,
    "calf_l": 0.07, "calf_r": 0.07,
    "foot_l": 0.05, "foot_r": 0.05,
}
WEIGHT_FALLOFF = 0.045
MAX_INFLUENCES = 4


def _segment_distance(p, a, b):
    ab = b - a
    denom = ab.length_squared
    t = 0.0 if denom < 1e-12 else max(0.0, min(1.0, (p - a).dot(ab) / denom))
    return (p - (a + ab * t)).length


def skin(mesh_ob, arm):
    """뼈까지의 거리로 웨이트를 준다.

    Blender의 열 확산(automatic weights)은 복셀 리메시로 녹인 생성 껍질에서
    「failed to find solution」으로 거의 모든 정점을 비운 채 돌아왔다. 뼈마다
    두께 반지름을 두고, 표면에서 뼈 표면까지의 거리로 가우시안 감쇠를 걸어
    이웃 뼈끼리 관절 주변 몇 cm를 섞는다. 예측 가능하고 매번 같다.
    """
    bones = [(b.name, b.head_local.copy(), b.tail_local.copy())
             for b in arm.data.bones if b.name != "root"]
    for name, _, _ in bones:
        if mesh_ob.vertex_groups.get(name) is None:
            mesh_ob.vertex_groups.new(name=name)
    groups = {name: mesh_ob.vertex_groups[name] for name, _, _ in bones}
    for v in mesh_ob.data.vertices:
        scored = []
        for name, head, tail in bones:
            d = _segment_distance(v.co, head, tail) - BONE_RADIUS.get(name, 0.06)
            d = max(d, 0.0)
            scored.append((math.exp(-(d / WEIGHT_FALLOFF) ** 2), name))
        scored.sort(reverse=True)
        top = [(w, n) for w, n in scored[:MAX_INFLUENCES] if w > 0.02 * scored[0][0]]
        if not top or top[0][0] <= 0.0:
            # 어느 뼈 반지름 안에도 없다: 가장 가까운 뼈에 통째로.
            nearest = min(bones, key=lambda item: _segment_distance(v.co, item[1], item[2]))[0]
            groups[nearest].add([v.index], 1.0, "REPLACE")
            continue
        total = sum(w for w, _ in top)
        for w, n in top:
            groups[n].add([v.index], w / total, "REPLACE")
    mod = mesh_ob.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    mod.use_vertex_groups = True
    # UE 기본 스키닝과 같은 선형 블렌딩으로 미리본다.
    mod.use_deform_preserve_volume = False
    mesh_ob.parent = arm
    ig.log(f"  skin: distance weights, {len(bones)} bones, falloff {WEIGHT_FALLOFF} m")


# --------------------------------------------------------------------------
# 동작
# --------------------------------------------------------------------------

def new_action(arm, name, frames):
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    if arm.animation_data is None:
        arm.animation_data_create()
    arm.animation_data.action = action
    try:
        # Blender 4.4+ 슬롯. 슬롯이 없으면 키가 안 들어간다.
        if hasattr(action, "slots") and len(action.slots) == 0:
            slot = action.slots.new(id_type="OBJECT", name=arm.name)
            arm.animation_data.action_slot = slot
    except Exception as error:  # noqa: BLE001 - 5.x 마이너마다 API가 다르다
        ig.log(f"  action slot: {error}")
    for pb in arm.pose.bones:
        pb.rotation_euler = (0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)
        # 다른 테이크의 팔·골반 자세가 다음 테이크에 남지 않게 전 채널을 적는다.
        for f in (0, frames):
            for channel in ("rotation_euler", "location", "scale"):
                pb.keyframe_insert(channel, frame=f)
    action.frame_range = (0, frames)
    action.use_frame_range = True
    return action


def key_rot(arm, bone, frame, rx=0.0, ry=0.0, rz=0.0):
    pb = arm.pose.bones[bone]
    pb.rotation_euler = (math.radians(rx), math.radians(ry), math.radians(rz))
    pb.keyframe_insert("rotation_euler", frame=frame)


def key_loc(arm, bone, frame, x=0.0, y=0.0, z=0.0):
    pb = arm.pose.bones[bone]
    pb.location = (x, y, z)
    pb.keyframe_insert("location", frame=frame)


def action_fcurves(action):
    """5.x 계층 액션: F커브는 슬롯의 channelbag 안에 있다. 옛 API도 받는다."""
    if hasattr(action, "fcurves"):
        return list(action.fcurves)
    curves = []
    for layer in getattr(action, "layers", []):
        for strip in layer.strips:
            for bag in strip.channelbags:
                curves.extend(bag.fcurves)
    return curves


def cyclic(arm, action):
    for fc in action_fcurves(action):
        mod = fc.modifiers.new("CYCLES")
        mod.mode_before = "REPEAT"
        mod.mode_after = "REPEAT"
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
            kp.easing = "AUTO"


def orient_bone(arm, name, start, end, frame):
    """접지 목표에서 구한 뼈 방향을 로컬 키로 굽는다. 런타임 IK 비용은 없다."""
    bone = arm.data.bones[name]
    rotation = (bone.tail_local - bone.head_local).rotation_difference(end - start)
    matrix = rotation.to_matrix().to_4x4() @ bone.matrix_local
    matrix.translation = start
    arm.pose.bones[name].matrix = matrix
    bpy.context.view_layer.update()
    arm.pose.bones[name].keyframe_insert("rotation_euler", frame=frame)


def plant_limb(arm, upper_name, lower_name, end_name, target, pole, frame):
    """두 관절 해석 IK. 손목·발목 목표와 팔꿈치·무릎의 굽힘 방향을 보존한다."""
    bpy.context.view_layer.update()
    upper = arm.pose.bones[upper_name]
    start = upper.head.copy()
    a = arm.data.bones[upper_name].length
    b = arm.data.bones[lower_name].length
    direction = (target - start).normalized()
    distance = max(abs(a - b) + 0.0001, min((target - start).length, a + b - 0.0001))
    along = (a * a - b * b + distance * distance) / (2.0 * distance)
    bend = pole - start
    bend -= direction * bend.dot(direction)
    bend.normalize()
    elbow = start + direction * along + bend * math.sqrt(max(0.0, a * a - along * along))
    endpoint = start + direction * distance
    orient_bone(arm, upper_name, start, elbow, frame)
    orient_bone(arm, lower_name, elbow, endpoint, frame)
    # 접지 중 주먹과 발끝은 팔·종아리 회전을 따라 말려 들어가지 않는다.
    end = arm.data.bones[end_name]
    orient_bone(arm, end_name, endpoint, endpoint + end.tail_local - end.head_local, frame)


def author_crawl(arm, frames=36):
    """한 팔로 버티는 동안 반대 손을 옮긴다. 발목도 바닥 목표를 따라 움직인다."""
    action = new_action(arm, "Crawl", frames)
    for f in range(frames + 1):
        t = f / frames * math.tau
        key_rot(arm, "pelvis", f, ry=1.5 * math.sin(t))
        key_rot(arm, "spine_01", f, rz=1.5 * math.sin(t))
        key_rot(arm, "spine_02", f, rx=0.7 * math.sin(2.0 * t), rz=-1.2 * math.sin(t))
        key_rot(arm, "spine_03", f, rz=-1.0 * math.sin(t))
        key_rot(arm, "neck", f, rx=1.0 * math.sin(2.0 * t))
        key_rot(arm, "head", f, rx=-1.5 * math.sin(2.0 * t), rz=3.0 * math.sin(t))
        for side, off in (("l", 0.0), ("r", 0.5)):
            phase = (f / frames + off) % 1.0
            if phase < 0.65:
                slide, lift = 0.045 - 0.09 * phase / 0.65, 0.0
            else:
                swing = (phase - 0.65) / 0.35
                slide = -0.045 + 0.09 * swing
                lift = 0.035 * math.sin(math.pi * swing)
            forearm = arm.data.bones[f"lowerarm_{side}"]
            target = forearm.tail_local + Vector((slide, 0.0, lift))
            plant_limb(arm, f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}",
                       target, forearm.head_local, f)
            calf = arm.data.bones[f"calf_{side}"]
            plant_limb(arm, f"thigh_{side}", f"calf_{side}", f"foot_{side}",
                       calf.tail_local + Vector((-slide * 0.4, 0.0, lift * 0.2)), calf.head_local, f)
    cyclic(arm, action)
    return action


def author_listen(arm, frames=90):
    """멈춰서 듣는다. 숨과, 귀를 벽에 대듯 천천히 고개를 돌리는 것뿐."""
    action = new_action(arm, "Listen", frames)
    n = frames
    for f in range(0, n + 1, 5):
        t = f / n * math.tau
        breath = math.sin(t * 2.0)
        key_rot(arm, "spine_02", f, rx=1.2 * breath)
        key_rot(arm, "spine_03", f, rx=-1.8 * breath)
        key_rot(arm, "neck", f, rx=1.0 * breath)
        key_rot(arm, "head", f, rx=-3.0 + 2.0 * math.sin(t + 1.0), ry=9.0 * math.sin(t), rz=14.0 * math.sin(t * 0.5))
        for side in ("l", "r"):
            forearm = arm.data.bones[f"lowerarm_{side}"]
            plant_limb(arm, f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}",
                       forearm.tail_local, forearm.head_local, f)
            calf = arm.data.bones[f"calf_{side}"]
            plant_limb(arm, f"thigh_{side}", f"calf_{side}", f"foot_{side}",
                       calf.tail_local, calf.head_local, f)
        key_rot(arm, "pelvis", f, ry=1.0 * math.sin(t))
    cyclic(arm, action)
    return action


def author_bang(arm, frames=63):
    """오른주먹으로 세 번 두드린다. 2.1초 = 63프레임, 노크 소리와 박자를 맞춘다."""
    action = new_action(arm, "Bang", frames)
    # 녹음은 0 / 0.62 / 1.24초에 친다. 30fps에서 가장 가까운 프레임.
    strikes = (0, 19, 37)
    for f in range(0, frames + 1):
        raise_amt = 0.0
        for s in strikes:
            if s - 8 <= f < s:
                raise_amt = max(raise_amt, (f - (s - 8)) / 8.0)
            elif s <= f < s + 3:
                raise_amt = max(raise_amt, 0.0)
        # 마지막 타격 뒤 서서히 내린다.
        if f > strikes[-1] + 3:
            raise_amt = 0.0
        if f % 3 == 0 or any(abs(f - s) <= 1 for s in strikes):
            # 몸통은 왼팔에 체중을 싣고 오른쪽 어깨를 든다.
            weight = min(1.0, f / 6.0) if f < strikes[-1] + 6 else max(0.0, 1.0 - (f - strikes[-1] - 6) / 10.0)
            key_rot(arm, "spine_03", f, ry=-6.0 * weight, rz=4.0 * weight)
            key_rot(arm, "spine_02", f, ry=-3.0 * weight)
            key_rot(arm, "upperarm_l", f, rx=4.0 * weight)
            key_rot(arm, "head", f, rx=-6.0 * weight, rz=-8.0 * weight)
            key_rot(arm, "neck", f, rx=-2.0 * weight)
            for side in ("l", "r"):
                forearm = arm.data.bones[f"lowerarm_{side}"]
                lift = Vector((0.02 * raise_amt, 0.0, 0.14 * raise_amt)) if side == "r" else Vector()
                plant_limb(arm, f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}",
                           forearm.tail_local + lift, forearm.head_local, f)
    for fc in action_fcurves(action):
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
    return action


def author_lunge(arm, frames=24):
    """덮친다. 가슴이 솟고 두 팔이 벌어지며 고개가 든다. 끝 프레임을 잡아 둔다."""
    action = new_action(arm, "Lunge", frames)
    for f in (0, 6, 12, 18, 24):
        a = min(1.0, f / 14.0)
        ease = a * a * (3.0 - 2.0 * a)
        key_rot(arm, "spine_01", f, rx=-3.0 * ease)
        key_rot(arm, "spine_02", f, rx=-4.0 * ease)
        key_rot(arm, "spine_03", f, rx=-4.0 * ease)
        key_rot(arm, "neck", f, rx=-5.0 * ease)
        key_rot(arm, "head", f, rx=-12.0 * ease)
        for side, sgn in (("l", 1.0), ("r", -1.0)):
            # 두 손이 바닥에서 떠 앞으로 나온다. 팔꿈치는 거의 펴진 채로.
            forearm = arm.data.bones[f"lowerarm_{side}"]
            plant_limb(arm, f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}",
                       forearm.tail_local + Vector((0.07, sgn * 0.035, 0.12)) * ease,
                       forearm.head_local, f)
            key_rot(arm, f"thigh_{side}", f, rz=sgn * 6.0 * ease)
            key_rot(arm, f"calf_{side}", f, rx=14.0 * ease)
    for fc in action_fcurves(action):
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
    return action


# --------------------------------------------------------------------------
# 벽에 귀를 대는 자세 (2026-10-19)
# --------------------------------------------------------------------------
# 셋째 밤 귀환길에서 그는 4층 복도 남쪽 벽에 귀를 대고 있다. 예전에는 벽을 정면으로 보고
# Listen을 틀어서 190 cm 몸의 머리와 상체가 벽 속에 묻혔다. 이 자세는 몸을 벽과 나란히
# 두고 팔다리가 벌어진 오른쪽(저작 좌표 -Y, UE에서는 몸의 오른쪽)을 벽에 붙인다. 머리를
# 벽 쪽으로 기울여 옆머리를 대고, 오른주먹은 머리 앞 벽을 짚는다. 왼팔과 두 다리는 몸
# 아래로 모아 복도 쪽 폭을 줄인다. 좌표는 저작 공간 미터, 회전은 뼈 로컬 도(°)다.
WALL_POSE = {
    # 벽면. 게임에서는 몸 원점을 벽에서 이만큼(43 cm) 띄워 세운다. 기는 자세로 돌아간
    # 순간에도 오른 허벅지(-0.424)가 벽을 넘지 않는 거리다. 벽 자세에서는 몸을 11 cm 벽
    # 쪽으로 붙여 오른 위팔과 옆머리가 벽에 닿는다. 복도 쪽 끝은 +0.27이라 몸 폭이 70 cm다.
    "wall_y": -0.43,
    "pelvis_shift": (0.0, -0.11, -0.01),
    # 엎드린 몸의 척추는 바닥 평면에서 옆으로 굽는다(로컬 rz). 굽힌 만큼 얼굴이 벽으로
    # 돌아가므로 머리는 반대로 돌리고(ry +25) 벽 쪽으로 기울여(rz +19) 귀를 댄다.
    "spine": {"spine_01": (0.0, 0.0, -3.0), "spine_02": (0.0, 0.0, -5.0), "spine_03": (0.0, 3.0, -7.0)},
    "neck": (0.0, 0.0, -18.0),
    "head": (0.0, 25.0, 19.0),
    "hand_r": (0.80, -0.28, 0.05), "elbow_r": (0.55, -0.29, 0.06),
    "hand_l": (0.70, -0.06, 0.05), "elbow_l": (0.56, 0.09, 0.05),
    "ankle_r": (-0.60, -0.20, 0.05), "knee_r": (-0.25, -0.25, 0.08),
    "ankle_l": (-0.82, -0.08, 0.10), "knee_l": (-0.50, 0.04, 0.10),
}


def _shift_bone(arm, name, frame, delta):
    """뼈대 좌표의 이동을 그 뼈의 로컬 이동 키로 적는다(부모가 자세를 안 잡은 뼈만)."""
    bone = arm.data.bones[name]
    local = bone.matrix_local.to_3x3().inverted() @ Vector(delta)
    key_loc(arm, name, frame, local.x, local.y, local.z)


def key_wall_pose(arm, frame, s, breath=0.0, sway=0.0, press=0.0, pose=None):
    """s=0이면 바닥 자세, 1이면 벽 자세. 몸통을 먼저 잡고 그 어깨·엉덩이에서 팔다리를 푼다."""
    p = pose or WALL_POSE
    shift = Vector(p["pelvis_shift"]) * s + Vector((sway, 0.0, 0.0))
    _shift_bone(arm, "pelvis", frame, shift)
    for name, (rx, ry, rz) in p["spine"].items():
        extra = 1.2 * breath if name == "spine_02" else (-1.6 * breath if name == "spine_03" else 0.0)
        key_rot(arm, name, frame, rx=rx * s + extra, ry=ry * s, rz=rz * s)
    rx, ry, rz = p["neck"]
    key_rot(arm, "neck", frame, rx=rx * s + 0.8 * breath, ry=ry * s, rz=rz * s)
    rx, ry, rz = p["head"]
    key_rot(arm, "head", frame, rx=rx * s, ry=(ry - press) * s, rz=rz * s)
    for side in ("l", "r"):
        forearm = arm.data.bones[f"lowerarm_{side}"]
        target = forearm.tail_local.lerp(Vector(p[f"hand_{side}"]), s)
        if side == "r":
            # 주먹을 바닥에서 들어 벽으로 옮긴다. 끌지 않는다.
            target = target + Vector((0.0, 0.0, 0.08 * math.sin(math.pi * min(1.0, s))))
        pole = forearm.head_local.lerp(Vector(p[f"elbow_{side}"]), s)
        plant_limb(arm, f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}", target, pole, frame)
        calf = arm.data.bones[f"calf_{side}"]
        target = calf.tail_local.lerp(Vector(p[f"ankle_{side}"]), s)
        pole = calf.head_local.lerp(Vector(p[f"knee_{side}"]), s)
        plant_limb(arm, f"thigh_{side}", f"calf_{side}", f"foot_{side}", target, pole, frame)


def _smooth(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3.0 - 2.0 * x)


def author_listen_wall_in(arm, frames=24, pose=None):
    """기던 자세에서 벽에 붙는다. 0.8초. 오른주먹이 먼저 벽을 짚고 머리가 따라간다."""
    action = new_action(arm, "ListenWallIn", frames)
    for f in range(frames + 1):
        key_wall_pose(arm, f, _smooth(f / frames), pose=pose)
    return action


def author_listen_wall(arm, frames=120, pose=None):
    """벽에 귀를 댄 채 숨을 쉰다. 4초 루프. 귀를 더 붙였다 늦추고 몸이 벽을 따라 조금 밀린다."""
    action = new_action(arm, "ListenWall", frames)
    for f in range(0, frames + 1, 4):
        t = f / frames * math.tau
        key_wall_pose(arm, f, 1.0, breath=math.sin(2.0 * t), sway=0.012 * math.sin(t),
                      press=3.0 * max(0.0, math.sin(t + 0.6)), pose=pose)
    cyclic(arm, action)
    return action


def author_listen_wall_out(arm, frames=18, pose=None):
    """벽에서 떨어져 기는 자세로 돌아간다. 0.6초. 이동은 이 동작이 끝난 뒤에 시작한다."""
    action = new_action(arm, "ListenWallOut", frames)
    for f in range(frames + 1):
        key_wall_pose(arm, f, 1.0 - _smooth(f / frames), pose=pose)
    return action


def author_bang_wall(arm, frames=63, pose=None):
    """벽에 귀를 댄 채 오른주먹으로 벽을 세 번 친다. Bang과 같은 0/19/37프레임에 닿는다."""
    action = new_action(arm, "BangWall", frames)
    p = dict(pose or WALL_POSE)
    rest = Vector(p["hand_r"])
    # 주먹 겉면은 손목 목표보다 5 cm쯤 벽 쪽에 있다. 닿는 자리는 머리 앞, 바닥에서 22 cm.
    contact = Vector((rest.x - 0.03, p["wall_y"] + 0.058, 0.22))
    back = Vector((rest.x - 0.02, p["wall_y"] + 0.15, 0.26))
    strikes = (0, 19, 37)
    for f in range(frames + 1):
        # 각 타격: 6프레임 동안 뒤로 뺐다가 닿고, 2프레임 붙어 있다가 다시 뺀다.
        target = None
        for s in strikes:
            if s - 6 <= f < s:
                target = back.lerp(contact, _smooth((f - (s - 6)) / 6.0))
            elif s <= f <= s + 2:
                target = contact
            elif s + 2 < f <= s + 6 and s != strikes[-1]:
                target = contact.lerp(back, _smooth((f - s - 2) / 4.0))
        if target is None:
            if f < strikes[-1]:
                target = back
            else:
                target = contact.lerp(rest, _smooth((f - strikes[-1] - 2) / 14.0))
        jolt = 0.6 if any(s <= f <= s + 2 for s in strikes) else 0.0
        p["hand_r"] = tuple(target)
        p["elbow_r"] = (0.55, -0.30, 0.16)
        key_wall_pose(arm, f, 1.0, breath=jolt, pose=p)
    for fc in action_fcurves(action):
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
    return action


# --------------------------------------------------------------------------
# 미리보기 · 내보내기
# --------------------------------------------------------------------------

def debug_bone_props(arm):
    """뼈를 얇은 원기둥으로 그린 미리보기용 소품. 렌더 뒤 지운다."""
    props = []
    mat = bpy.data.materials.new("__bone_mat")
    bsdf = ig._principled(mat)
    bsdf.inputs["Base Color"].default_value = (1.0, 0.15, 0.05, 1.0)
    bsdf.inputs["Emission Color"].default_value = (1.0, 0.2, 0.05, 1.0)
    bsdf.inputs["Emission Strength"].default_value = 2.0
    for b in arm.data.bones:
        head = arm.matrix_world @ b.head_local
        tail = arm.matrix_world @ b.tail_local
        axis = tail - head
        if axis.length < 1e-4:
            continue
        cyl = ig.cylinder("__bone_" + b.name, 0.012, axis.length, segments=8)
        cyl.location = (head + tail) * 0.5
        cyl.rotation_euler = axis.to_track_quat("Z", "Y").to_euler()
        ig.assign_material(cyl, mat)
        props.append(cyl)
    return props, mat


def remove_props(props, mat):
    for p in props:
        me = p.data
        bpy.data.objects.remove(p, do_unlink=True)
        bpy.data.meshes.remove(me)
    bpy.data.materials.remove(mat)


def set_action_frame(arm, action, frame):
    arm.animation_data.action = action
    bpy.context.scene.frame_set(frame)
    bpy.context.view_layer.update()


def export_skeletal_fbx(path, arm, mesh_ob):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.context.view_layer.update()
    for other in list(bpy.context.view_layer.objects):
        if other is not None:
            other.select_set(False)
    arm.select_set(True)
    mesh_ob.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={"ARMATURE", "MESH"},
        use_mesh_modifiers=True,
        mesh_smooth_type="EDGE",
        use_tspace=True,
        add_leaf_bones=False,
        primary_bone_axis="Y",
        secondary_bone_axis="X",
        armature_nodetype="NULL",
        use_armature_deform_only=True,
        bake_anim=True,
        bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=True,
        bake_anim_force_startend_keying=True,
        bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0,
        path_mode="STRIP",
        embed_textures=False,
        global_scale=1.0,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_NONE",
        use_custom_props=False,
        **ig.EXPORT_AXIS)
    ig.log(f"  fbx(skeletal) -> {path} ({os.path.getsize(path)} bytes)")


def main():
    args = parse_args()
    # 상대 경로는 Blender가 드라이브 루트 기준으로 풀어 C:\Saved에 렌더를 쓴다.
    # out_root_from_argv는 `--` 뒤 첫 인자를 폴더로 읽는다. 여기는 인자가 --glb라
    # 저장소 루트에 `--glb` 폴더가 생긴다. 기본은 Content/SourceArt/Blender.
    out_root = os.path.abspath(args.out) if args.out else os.path.abspath(
        os.path.join(HERE, "..", "..", "Content", "SourceArt", "Blender"))
    name = args.name
    out_dir = os.path.join(out_root, name)
    os.makedirs(out_dir, exist_ok=True)
    scene = bpy.context.scene
    ig.reset_scene()
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.frame_start = 1

    high = import_glb(os.path.abspath(args.glb))
    ig.log(f"{name}: imported {ig.triangle_count(high)} tris")
    if args.rot_x or args.rot_y:
        bake_rotation(high, args.rot_x, args.rot_y, 0.0)
    if args.yaw:
        bake_rotation(high, 0.0, 0.0, args.yaw)
    scale = fit_and_ground(high, args.length)
    # glTF의 UV 경계마다 분리된 정점은 smooth 플래그만으로 이어지지 않는다.
    # 0.05mm 안의 중복 정점만 용접한다. UV는 면 모서리 속성이므로 보존된다.
    source_mesh = bmesh.new()
    source_mesh.from_mesh(high.data)
    before_weld = len(source_mesh.verts)
    bmesh.ops.remove_doubles(source_mesh, verts=list(source_mesh.verts), dist=.00005)
    bmesh.ops.recalc_face_normals(source_mesh, faces=list(source_mesh.faces))
    ig.log(f"원본 중복 정점 연결: {before_weld} -> {len(source_mesh.verts)}")
    source_mesh.to_mesh(high.data)
    source_mesh.free()
    ig.prepare_organic_source(high, roughness_floor=0.70)
    lo, hi = ig.bounds(high)
    ig.log(f"{name}: scale x{scale:.4f} -> {(hi - lo).x * 100:.1f} x {(hi - lo).y * 100:.1f} x {(hi - lo).z * 100:.1f} cm")

    low = high.copy()
    low.data = high.data.copy()
    low.name = name
    low.data.name = name
    ig._link(low)
    if args.voxel_remesh > 0.0:
        mod = low.modifiers.new("Remesh", "REMESH")
        mod.mode = "VOXEL"
        mod.voxel_size = args.voxel_remesh
        mod.use_smooth_shade = True
        ig.apply_modifiers(low)
        ig.log(f"{name}: voxel remesh {args.voxel_remesh} m -> {ig.triangle_count(low)} tris")
    if args.smooth_iterations > 0:
        mod = low.modifiers.new("Smooth", "SMOOTH")
        mod.iterations = args.smooth_iterations
        mod.factor = 0.5
        ig.apply_modifiers(low)
    ig.remove_small_islands(low, keep_largest=args.keep_largest)
    ig.decimate(low, args.budget)
    for poly in low.data.polygons:
        poly.use_smooth = True
    low.data.materials.clear()
    ue_bounds = ig.bounds(low)

    # 저작 좌표 미리보기(고밀도, 정점색/텍스처).
    low.hide_render = True
    ig.render_preview(high, os.path.join(out_dir, f"{name}_source_preview.png"))
    low.hide_render = False

    # 여기서부터 FBX 좌표. 뼈대도 이 좌표에서 세운다.
    ig.mirror_y(low)
    ig.mirror_y(high)

    textures = {}
    if not args.no_bake:
        ig.uv_smart(low, margin=0.003)
        # 베이크 탐색 범위를 몸 길이 대신 리메시 간격에 맞춘다.
        # 겹친 옷자락과 얼굴 주변에서 멀리 떨어진 표면까지 읽지 않게 한다.
        extrusion = max(args.voxel_remesh * 2.0, 0.006)
        textures = ig.bake_from_high(low, high, name, out_dir, size=args.texture_size,
                                     cage_extrusion=extrusion, max_ray_distance=extrusion * 3.0)
    high_mesh = high.data
    bpy.data.objects.remove(high, do_unlink=True)
    bpy.data.meshes.remove(high_mesh)

    if textures:
        ig.preview_material_from_bakes(low, textures)
    else:
        mat = bpy.data.materials.new("__flat")
        bsdf = ig._principled(mat)
        bsdf.inputs["Base Color"].default_value = (0.62, 0.60, 0.56, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.9
        ig.assign_material(low, mat)

    marks = load_landmarks(args.landmarks) if args.landmarks else find_landmarks(low)
    arm = build_armature(name, marks)
    skin(low, arm)

    actions = {
        "Crawl": author_crawl(arm),
        "Listen": author_listen(arm),
        "Bang": author_bang(arm),
        "Lunge": author_lunge(arm),
        "ListenWallIn": author_listen_wall_in(arm),
        "ListenWall": author_listen_wall(arm),
        "ListenWallOut": author_listen_wall_out(arm),
        "BangWall": author_bang_wall(arm),
    }

    # 뼈 자리 확인 한 장, 기는 동작 네 프레임, 손전등 한 장.
    set_action_frame(arm, actions["Listen"], 1)
    props, pmat = debug_bone_props(arm)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_bones.png"), extra_objects=props,
                      camera_yaw_deg=-40.0, camera_pitch_deg=34.0)
    remove_props(props, pmat)
    for frame in (1, 10, 19, 28):
        set_action_frame(arm, actions["Crawl"], frame)
        ig.render_preview(low, os.path.join(out_dir, f"{name}_crawl_f{frame:02d}.png"),
                          camera_yaw_deg=-55.0, camera_pitch_deg=10.0)
    set_action_frame(arm, actions["Lunge"], 24)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_lunge.png"), camera_yaw_deg=-80.0, camera_pitch_deg=6.0)
    set_action_frame(arm, actions["Bang"], 19)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_bang.png"), camera_yaw_deg=-30.0, camera_pitch_deg=14.0)
    set_action_frame(arm, actions["Crawl"], 1)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_preview.png"), camera_yaw_deg=-55.0)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_preview_torch.png"), flashlight=True, camera_yaw_deg=-75.0)

    if args.no_bake:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{name}.blend"))
        ig.log(f"{name}: previews only (--no-bake)")
        return

    slots = ig.finalize_slots(low)
    set_action_frame(arm, actions["Crawl"], 1)
    fbx_path = os.path.join(out_dir, f"{name}.fbx")
    export_skeletal_fbx(fbx_path, arm, low)
    manifest = ig.write_manifest(out_dir, name, "hero", fbx_path, textures, low, slots,
                                 notes=args.notes or f"생성 GLB {os.path.basename(args.glb)}에서 리깅",
                                 origin="bottom", ue_bounds=ue_bounds)
    import json
    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    data["skeletal"] = True
    data["animations"] = {key: {"frames": int(a.frame_range[1]), "fps": FPS,
                                "loop": key in ("Crawl", "Listen", "ListenWall")} for key, a in actions.items()}
    data["generated_from"] = os.path.relpath(os.path.abspath(args.glb), out_root).replace("\\", "/")
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    # FBX의 슬롯 정리로 빠진 재질을 복구한 뒤 이미지까지 작업 파일에 넣는다.
    if textures:
        ig.preview_material_from_bakes(low, textures)
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{name}.blend"))
    ig.log(f"{name}: done tris={ig.triangle_count(low)} bones={len(arm.data.bones)}")


if __name__ == "__main__":
    main()
