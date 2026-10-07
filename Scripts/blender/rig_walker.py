"""생성 GLB를 걷는 사람의 스켈레탈 메시로 다듬는다 — 밤3 순찰하는 목한수.

rig_crawler.py가 기는 몸을 다뤘다면 여기는 서서 걷는 몸이다. 같은 원본 처리
(복셀 리메시·굽기·거리 웨이트)를 쓰고, 뼈 자리와 동작만 두 발 사람에 맞춘다.

1. GLB를 읽고 yaw로 정면을 +X에 둔다(폰의 앞이 +X).
2. 손전등을 든 손이 오른쪽(+Y, UE 기준)에 오도록 필요하면 좌우를 뒤집는다.
   생성물은 오른손 좌표계라 저작 좌표(UE 숫자)로 읽으면 거울상이 된다.
3. 키를 실제 cm에 맞추고 원점은 발밑 중심.
4. 저밀도 사본을 만들고 고밀도에서 BaseColor·Normal·ORM을 굽는다.
5. 서 있는 사람의 비율과 정점 분포로 관절 자리를 잡는다. 팔은 몸통과의
   틈으로, 다리는 가랑이 아래 좌우로 가른다.
6. 웨이트는 거리로 주되 정점이 속한 부위(머리·팔·다리·몸통)의 뼈만 쓴다.
   통바지 가랑이와 팔 안쪽이 반대편 뼈를 따라가지 않게 하려는 것이다.
7. 동작 여섯 — Idle·Walk·Run·Look(루프), Freeze(루프), Grab(1회). 이동은
   폰이 하므로 전부 제자리 동작이고, 손전등 쥔 오른손은 늘 앞을 비춘다.

    blender -b --factory-startup --python Scripts/blender/rig_walker.py -- \\
        --glb Content/SourceArt/Generated/MokHansooPatrol/trellis1024-s56/raw/pbr_00001_.glb \\
        --name SK_MokHansooPatrol --height 172 --yaw 90

--profile resident는 손에 든 것 없는 사람이다(승강기 거울에 비치는 유담). 손전등과
열쇠 꾸러미 규칙을 빼고 두 팔을 다 흔들며, 동작은 Idle·Walk(루프)와 LookBack(1회,
어깨 너머로 고개를 돌려 뒤를 본다) 셋이다.

    blender -b --factory-startup --python Scripts/blender/rig_walker.py -- \\
        --glb Content/SourceArt/Generated/YudamReflection/trellis1024-s56/raw/pbr_00001_.glb \\
        --name SK_YudamReflection --height 162 --yaw 90 --profile resident
"""

import argparse
import json
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
from rig_crawler import (  # noqa: E402
    action_fcurves,
    bake_rotation,
    cyclic,
    debug_bone_props,
    export_skeletal_fbx,
    import_glb,
    key_loc,
    key_rot,
    new_action,
    orient_bone,
    remove_props,
    set_action_frame,
    vertex_array,
)

FPS = 30

# 손전등을 쥔 오른손 자리(UE, 폰 기준). 허리 높이에서 앞 바닥을 비춘다.
# IGManagerPatrol의 TorchOffset은 이 손에서 렌즈까지 앞으로 더 나간 자리다.
TORCH_HAND = Vector((0.30, 0.19, 0.98))

# patrol(목한수) 또는 resident(빈손으로 선 사람). main()이 인자에서 정한다.
PROFILE = "patrol"


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--glb", required=True)
    parser.add_argument("--name", default="SK_MokHansooPatrol")
    parser.add_argument("--height", type=float, default=172.0, help="키 cm")
    parser.add_argument("--yaw", type=float, default=90.0, help="정면을 +X로 돌리는 Z 회전(도)")
    parser.add_argument("--voxel-remesh", type=float, default=0.004)
    parser.add_argument("--smooth-iterations", type=int, default=2)
    parser.add_argument("--budget", type=int, default=14000)
    parser.add_argument("--texture-size", type=int, default=2048)
    parser.add_argument("--out", default=None)
    parser.add_argument("--no-bake", action="store_true")
    parser.add_argument("--notes", default="")
    parser.add_argument("--profile", choices=["patrol", "resident"], default="patrol")
    return parser.parse_args(argv)


def fit_height_and_ground(ob, height_cm):
    lo, hi = ig.bounds(ob)
    scale = (height_cm / 100.0) / max((hi - lo).z, 1e-6)
    for v in ob.data.vertices:
        v.co *= scale
    lo, hi = ig.bounds(ob)
    center = (lo + hi) * 0.5
    shift = Vector((center.x, center.y, lo.z))
    for v in ob.data.vertices:
        v.co -= shift
    ob.data.update()
    return scale


def hand_mass(ob, height):
    """좌우 손 부근(손목 아래)의 정점 수. 손전등을 쥔 쪽이 훨씬 많다."""
    import numpy as np
    pts = vertex_array(ob)
    band = (pts[:, 2] > 0.33 * height) & (pts[:, 2] < 0.50 * height)
    out = np.abs(pts[:, 1]) > 0.17
    plus = int(np.count_nonzero(band & out & (pts[:, 1] > 0)))
    minus = int(np.count_nonzero(band & out & (pts[:, 1] < 0)))
    return plus, minus


def mirror_handedness(ob):
    """Y를 뒤집어 좌우를 바꾼다. 면 방향도 함께 뒤집는다."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    for v in bm.verts:
        v.co.y = -v.co.y
    bmesh.ops.reverse_faces(bm, faces=list(bm.faces))
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()


# --------------------------------------------------------------------------
# 관절 자리
# --------------------------------------------------------------------------

def arm_gap(pts, height):
    """팔뚝 높이에서 몸통과 팔 사이 틈의 |y|. 못 찾으면 0.215 m."""
    import numpy as np
    band = pts[(pts[:, 2] > 0.48 * height) & (pts[:, 2] < 0.60 * height)]
    if len(band) < 200:
        return 0.215
    ay = np.sort(np.abs(band[:, 1]))
    best_gap, best_mid = 0.0, 0.215
    for a, b in zip(ay[:-1], ay[1:]):
        if 0.12 < a < 0.34 and b - a > best_gap:
            best_gap, best_mid = b - a, (a + b) * 0.5
    return float(best_mid) if best_gap > 0.012 else 0.215


def find_landmarks(ob):
    """서 있는 사람의 관절 자리(미터, FBX 좌표: 앞 +X, 왼쪽 +Y, 위 +Z)."""
    import numpy as np
    pts = vertex_array(ob)
    height = float(pts[:, 2].max())
    gap = arm_gap(pts, height)
    core = pts[np.abs(pts[:, 1]) < gap]

    def center_x(z0, z1, sel=core):
        m = (sel[:, 2] > z0 * height) & (sel[:, 2] < z1 * height)
        return float(np.median(sel[m, 0])) if np.count_nonzero(m) else 0.0

    pelvis_z = 0.525 * height
    pelvis = Vector((center_x(0.50, 0.56), 0.0, pelvis_z))
    spine1 = Vector((center_x(0.57, 0.63), 0.0, 0.60 * height))
    spine2 = Vector((center_x(0.64, 0.70), 0.0, 0.67 * height))
    chest = Vector((center_x(0.71, 0.77), 0.0, 0.74 * height))
    neck = Vector((center_x(0.83, 0.86), 0.0, 0.845 * height))
    head = Vector((center_x(0.88, 0.95), 0.0, 0.915 * height))
    head_top = Vector((head.x, 0.0, height))

    chest_band = core[(core[:, 2] > 0.74 * height) & (core[:, 2] < 0.80 * height)]
    shoulder_half = float(np.percentile(np.abs(chest_band[:, 1]), 90)) if len(chest_band) else 0.18
    shoulder_half = max(0.15, min(shoulder_half, gap - 0.01))

    arms = {}
    for side, sign in (("l", 1.0), ("r", -1.0)):
        sel = pts[(np.sign(pts[:, 1]) == sign) & (np.abs(pts[:, 1]) > gap)]

        def arm_point(z0, z1, fallback_y):
            m = (sel[:, 2] > z0 * height) & (sel[:, 2] < z1 * height)
            if np.count_nonzero(m) < 20:
                return Vector((chest.x, sign * fallback_y, (z0 + z1) * 0.5 * height))
            s = sel[m]
            return Vector((float(np.median(s[:, 0])), float(np.median(s[:, 1])), (z0 + z1) * 0.5 * height))

        elbow = arm_point(0.60, 0.65, shoulder_half + 0.05)
        wrist = arm_point(0.46, 0.50, shoulder_half + 0.08)
        shoulder = Vector((chest.x, sign * shoulder_half, 0.815 * height))
        tip = wrist + (wrist - elbow).normalized() * 0.085
        arms[side] = (shoulder, elbow, wrist, tip)

    legs = {}
    crotch = 0.47 * height
    # 골반 앞뒤 자리는 허벅지 위쪽의 단면 중심으로 잡는다. 골반 높이의 몸통
    # 단면은 열린 재킷 자락과 열쇠 꾸러미 때문에 앞으로 쏠린다.
    thigh_top = pts[(np.abs(pts[:, 1]) < gap) & (pts[:, 2] > 0.42 * height) & (pts[:, 2] < 0.46 * height)]
    if len(thigh_top) > 50:
        pelvis.x = float(np.median(thigh_top[:, 0]))
    for side, sign in (("l", 1.0), ("r", -1.0)):
        sel = pts[(np.sign(pts[:, 1]) == sign) & (np.abs(pts[:, 1]) < gap) & (pts[:, 2] < crotch)]

        def leg_point(z0, z1):
            m = (sel[:, 2] > z0 * height) & (sel[:, 2] < z1 * height)
            if np.count_nonzero(m) < 20:
                return Vector((pelvis.x, sign * 0.10, (z0 + z1) * 0.5 * height))
            s = sel[m]
            return Vector((float(np.median(s[:, 0])), float(np.median(s[:, 1])), (z0 + z1) * 0.5 * height))

        knee = leg_point(0.27, 0.30)
        ankle = leg_point(0.04, 0.07)
        ankle.z = 0.055 * height
        foot = sel[sel[:, 2] < 0.04 * height] if len(sel) else sel
        toe_x = float(np.percentile(foot[:, 0], 96)) if len(foot) > 20 else ankle.x + 0.14
        hip = Vector((pelvis.x, sign * 0.092, pelvis_z - 0.035 * height))
        # 무릎은 엉덩이와 발목을 잇는 선 위에 둔다. 통바지 단면의 중심은 뒤로 처져
        # 있어서, 그대로 쓰면 선 자세가 이미 굽은 다리가 되고 걸음마다 쪼그려 앉는다.
        knee = hip.lerp(ankle, 0.49) + Vector((0.012, 0.0, 0.0))
        toe = Vector((toe_x - 0.02, ankle.y, 0.02))
        legs[side] = (hip, knee, ankle, toe)

    marks = {"pelvis": pelvis, "spine_01": spine1, "spine_02": spine2, "spine_03": chest,
             "neck": neck, "head": head, "head_top": head_top, "arms": arms, "legs": legs,
             "height": height, "gap": gap}
    ig.log(f"walker landmarks: height={height:.3f} gap={gap:.3f} shoulder_half={shoulder_half:.3f} "
           f"pelvis={tuple(round(v, 3) for v in pelvis)} chest={tuple(round(v, 3) for v in chest)}")
    for side in ("l", "r"):
        s, e, w, _ = arms[side]
        hp, k, a, t = legs[side]
        ig.log(f"  arm_{side}: shoulder={tuple(round(v, 3) for v in s)} elbow={tuple(round(v, 3) for v in e)} "
               f"wrist={tuple(round(v, 3) for v in w)}")
        ig.log(f"  leg_{side}: hip={tuple(round(v, 3) for v in hp)} knee={tuple(round(v, 3) for v in k)} "
               f"ankle={tuple(round(v, 3) for v in a)} toe={tuple(round(v, 3) for v in t)}")
    return marks


def build_armature(name, marks):
    arm_data = bpy.data.armatures.new(name + "_Armature")
    arm = ig._link(bpy.data.objects.new(name + "_Armature", arm_data))
    ig.set_active(arm)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm_data.edit_bones

    def bone(bname, head, tail, parent=None, connect=False):
        b = eb.new(bname)
        b.head = Vector(head)
        b.tail = Vector(tail)
        if parent is not None:
            b.parent = eb[parent]
            b.use_connect = connect
        direction = (b.tail - b.head).normalized()
        # 세로 뼈는 로컬 Z를 앞(+X)에 둔다. 로컬 X가 좌우 축이라 rx가 숙이기다.
        b.align_roll(Vector((1.0, 0.0, 0.0)) if abs(direction.z) > 0.7 else Vector((0.0, 0.0, 1.0)))
        return b

    bone("root", (0.0, 0.0, 0.0), (0.0, 0.0, 0.08))
    bone("pelvis", marks["pelvis"], marks["spine_01"], "root")
    bone("spine_01", marks["spine_01"], marks["spine_02"], "pelvis", True)
    bone("spine_02", marks["spine_02"], marks["spine_03"], "spine_01", True)
    bone("spine_03", marks["spine_03"], marks["neck"], "spine_02", True)
    bone("neck", marks["neck"], marks["head"], "spine_03", True)
    bone("head", marks["head"], marks["head_top"], "neck", True)
    for side in ("l", "r"):
        s, e, w, t = marks["arms"][side]
        bone(f"clavicle_{side}", marks["spine_03"] + Vector((0.0, 0.0, 0.06)), s, "spine_03")
        bone(f"upperarm_{side}", s, e, f"clavicle_{side}", True)
        bone(f"lowerarm_{side}", e, w, f"upperarm_{side}", True)
        bone(f"hand_{side}", w, t, f"lowerarm_{side}", True)
        hp, k, a, toe = marks["legs"][side]
        bone(f"thigh_{side}", hp, k, "pelvis")
        bone(f"calf_{side}", k, a, f"thigh_{side}", True)
        bone(f"foot_{side}", a, toe, f"calf_{side}", True)
    bpy.ops.object.mode_set(mode="OBJECT")
    for pb in arm.pose.bones:
        pb.rotation_mode = "XYZ"
    return arm


# --------------------------------------------------------------------------
# 웨이트 — 부위별 거리
# --------------------------------------------------------------------------

BONE_RADIUS = {
    "pelvis": 0.16, "spine_01": 0.16, "spine_02": 0.17, "spine_03": 0.17,
    "neck": 0.07, "head": 0.11,
    "clavicle_l": 0.06, "clavicle_r": 0.06,
    "upperarm_l": 0.06, "upperarm_r": 0.06,
    "lowerarm_l": 0.05, "lowerarm_r": 0.05,
    "hand_l": 0.06, "hand_r": 0.07,
    "thigh_l": 0.10, "thigh_r": 0.10,
    "calf_l": 0.075, "calf_r": 0.075,
    "foot_l": 0.06, "foot_r": 0.06,
}
WEIGHT_FALLOFF = 0.05
MAX_INFLUENCES = 4

TORSO_BONES = ("pelvis", "spine_01", "spine_02", "spine_03", "clavicle_l", "clavicle_r",
               "neck", "thigh_l", "thigh_r")


def _segment_distance(p, a, b):
    ab = b - a
    denom = ab.length_squared
    t = 0.0 if denom < 1e-12 else max(0.0, min(1.0, (p - a).dot(ab) / denom))
    return (p - (a + ab * t)).length


def region_bones(co, marks):
    """정점이 속한 부위가 쓸 수 있는 뼈. 하나뿐이면 그 뼈에 통째로 붙는다."""
    height = marks["height"]
    gap = marks["gap"]
    side = "l" if co.y >= 0.0 else "r"
    if co.z > 0.83 * height and abs(co.y) < 0.14:
        return ("head", "neck", "spine_03")
    shoulder, elbow, wrist, tip = marks["arms"][side]
    reach = tip + (tip - wrist).normalized() * 0.12
    near_arm = min(_segment_distance(co, shoulder, elbow), _segment_distance(co, elbow, wrist),
                   _segment_distance(co, wrist, reach))
    # 팔은 몸통 틈 바깥에서 팔뼈에 가까운 정점만이다. 허리 높이의 통바지 옆선도
    # 틈보다 넓어서, 틈만 보면 바지 자락이 손을 따라 판처럼 뜯겨 나온다.
    if abs(co.y) > gap - 0.02 and near_arm < 0.11 and 0.30 * height < co.z < 0.84 * height:
        # 손목 아래(손과 쥔 손전등)는 손뼈 하나에 단단히 붙인다. 거리로 나누면
        # 손을 앞으로 들 때 손전등이 팔뚝까지 늘어나 판자처럼 휜다.
        if co.z < wrist.z - 0.015 and _segment_distance(co, wrist, reach) < 0.13:
            return (f"hand_{side}",)
        return (f"clavicle_{side}", f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}", "spine_03")
    # 왼쪽 허리 앞에 매단 열쇠 꾸러미. 허벅지를 따라 돌면 앞으로 판처럼 튀어나온다.
    if PROFILE == "patrol" and 0.42 * height < co.z < 0.58 * height and co.x > marks["pelvis"].x + 0.07:
        return ("pelvis",)
    if co.z < 0.47 * height:
        # 팔 높이 아래는 다 다리다. 슬리퍼 바깥 날은 팔 틈보다 넓다.
        # 발목 아래(슬리퍼와 발)는 발뼈 하나에. 바짓단만 종아리와 섞는다.
        if co.z < 0.065 * height:
            return (f"foot_{side}",)
        if co.z < 0.40 * height:
            return (f"thigh_{side}", f"calf_{side}", f"foot_{side}")
        return (f"thigh_{side}", f"calf_{side}", "pelvis")
    return TORSO_BONES


def skin(mesh_ob, arm, marks):
    bones = {b.name: (b.head_local.copy(), b.tail_local.copy())
             for b in arm.data.bones if b.name != "root"}
    groups = {}
    for name in bones:
        groups[name] = mesh_ob.vertex_groups.get(name) or mesh_ob.vertex_groups.new(name=name)
    for v in mesh_ob.data.vertices:
        allowed = region_bones(v.co, marks)
        scored = []
        for name in allowed:
            head, tail = bones[name]
            d = max(_segment_distance(v.co, head, tail) - BONE_RADIUS.get(name, 0.06), 0.0)
            scored.append((math.exp(-(d / WEIGHT_FALLOFF) ** 2), name))
        scored.sort(reverse=True)
        top = [(w, n) for w, n in scored[:MAX_INFLUENCES] if w > 0.02 * scored[0][0]]
        if not top or top[0][0] <= 1e-6:
            nearest = min(allowed, key=lambda n: _segment_distance(v.co, *bones[n]))
            groups[nearest].add([v.index], 1.0, "REPLACE")
            continue
        total = sum(w for w, _ in top)
        for w, n in top:
            groups[n].add([v.index], w / total, "REPLACE")
    mod = mesh_ob.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    mod.use_vertex_groups = True
    mod.use_deform_preserve_volume = False
    mesh_ob.parent = arm
    ig.log(f"  skin: region distance weights, {len(bones)} bones, falloff {WEIGHT_FALLOFF} m")


# --------------------------------------------------------------------------
# 동작
# --------------------------------------------------------------------------

def solve_limb(arm, upper, lower, end, target, pole, frame, end_direction=None):
    """두 관절 해석 IK. end_direction이 있으면 손·발끝을 그 방향으로 둔다."""
    bpy.context.view_layer.update()
    start = arm.pose.bones[upper].head.copy()
    a = arm.data.bones[upper].length
    b = arm.data.bones[lower].length
    direction = (target - start).normalized()
    distance = max(abs(a - b) + 0.0001, min((target - start).length, a + b - 0.0001))
    along = (a * a - b * b + distance * distance) / (2.0 * distance)
    bend = pole - start
    bend -= direction * bend.dot(direction)
    if bend.length < 1e-6:
        bend = Vector((1.0, 0.0, 0.0))
    bend.normalize()
    joint = start + direction * along + bend * math.sqrt(max(0.0, a * a - along * along))
    endpoint = start + direction * distance
    orient_bone(arm, upper, start, joint, frame)
    orient_bone(arm, lower, joint, endpoint, frame)
    end_bone = arm.data.bones[end]
    tail_dir = end_direction if end_direction is not None else (end_bone.tail_local - end_bone.head_local)
    orient_bone(arm, end, endpoint, endpoint + tail_dir.normalized() * end_bone.length, frame)


def rest(arm, name, which="tail"):
    bone = arm.data.bones[name]
    return (bone.tail_local if which == "tail" else bone.head_local).copy()


def torch_hand_target(arm, frame_phase=0.0, bob=0.0, sweep=0.0, lift=0.0):
    """오른손이 손전등을 앞으로 쥐는 자리. FBX 좌표라 오른쪽이 -Y다."""
    return Vector((TORCH_HAND.x, -TORCH_HAND.y + sweep, TORCH_HAND.z + bob + lift))


def hold_torch(arm, frame, bob=0.0, sweep=0.0, lift=0.0, tremble=0.0):
    target = torch_hand_target(arm, bob=bob, sweep=sweep, lift=lift)
    target += Vector((0.0, tremble, tremble * 0.6))
    shoulder = arm.pose.bones["upperarm_r"].head
    pole = shoulder + Vector((-0.30, -0.25, -0.30))
    # 손등이 위, 손전등이 앞을 본다.
    solve_limb(arm, "upperarm_r", "lowerarm_r", "hand_r", target, pole, frame,
               end_direction=Vector((0.82, 0.0, -0.57)))


def author_walk(arm, name, frames, stride, lift, bob, lean, arm_swing, crouch):
    """제자리 걸음. 한 주기에 두 걸음. 디딘 발은 미끄러지듯 뒤로 가고, 든 발은 낮게 앞으로.

    골반은 crouch만큼 늘 낮춰 무릎을 조금 굽힌다. 다리를 다 펴서는 보폭 끝의 발이
    바닥에 닿지 않는다. 세로 뼈의 로컬 Y가 위라서 오르내림은 location.y다.
    """
    action = new_action(arm, name, frames)
    half = stride * 0.5
    for f in range(frames + 1):
        t = f / frames
        phase2 = 2.0 * math.tau * t
        key_loc(arm, "pelvis", f, y=-crouch - bob * 0.5 * (1.0 + math.cos(phase2)))
        key_rot(arm, "pelvis", f, ry=4.0 * math.sin(math.tau * t), rz=1.8 * math.sin(math.tau * t))
        key_rot(arm, "spine_01", f, rx=lean * 0.5, ry=-2.0 * math.sin(math.tau * t))
        key_rot(arm, "spine_02", f, rx=lean * 0.3, ry=-1.5 * math.sin(math.tau * t))
        key_rot(arm, "spine_03", f, rx=lean * 0.2 + 0.8 * math.sin(phase2))
        key_rot(arm, "neck", f, rx=-lean * 0.4)
        key_rot(arm, "head", f, rx=-lean * 0.3 + 1.2 * math.sin(phase2 + 0.6), ry=2.0 * math.sin(math.tau * t * 0.5))
        for side, offset in (("r", 0.0), ("l", 0.5)):
            p = (t + offset) % 1.0
            ankle = rest(arm, f"calf_{side}")
            toe = rest(arm, f"foot_{side}")
            if p < 0.6:
                s = p / 0.6
                x = half - stride * s
                z = 0.0
                foot_dir = toe - ankle
            else:
                s = (p - 0.6) / 0.4
                x = -half + stride * (s * s * (3.0 - 2.0 * s))
                z = lift * math.sin(math.pi * s)
                # 발끝을 조금 든다. 슬리퍼가 바닥을 끌다 떨어진다.
                foot_dir = (toe - ankle) + Vector((0.0, 0.0, 0.04 * math.sin(math.pi * s)))
            target = ankle + Vector((x, 0.0, z))
            knee = rest(arm, f"thigh_{side}")
            solve_limb(arm, f"thigh_{side}", f"calf_{side}", f"foot_{side}", target,
                       knee + Vector((0.6, 0.0, 0.0)), f, end_direction=foot_dir)
        # 왼팔은 반대 다리와 함께 흔든다. 오른손은 손전등을 앞으로 든다.
        # 빈손인 사람은 오른팔도 반대 박자로 흔든다.
        swing = math.sin(math.tau * t)
        swing_arm(arm, "l", f, arm_swing * swing, 0.03 * abs(swing))
        if PROFILE == "resident":
            swing_arm(arm, "r", f, -arm_swing * swing, 0.03 * abs(swing))
        else:
            hold_torch(arm, f, bob=0.012 * math.sin(phase2 + 1.2), sweep=0.02 * math.sin(math.tau * t))
    cyclic(arm, action)
    return action


def swing_arm(arm, side, frame, forward, lift):
    """늘어뜨린 팔을 앞뒤로 흔든다. FBX 좌표라 오른쪽이 -Y다."""
    sign = 1.0 if side == "l" else -1.0
    wrist = rest(arm, f"lowerarm_{side}")
    elbow = rest(arm, f"upperarm_{side}")
    target = wrist + Vector((forward, -0.02 * sign, lift))
    solve_limb(arm, f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}", target,
               elbow + Vector((-0.3, 0.05 * sign, 0.0)), frame)


def author_resident_idle(arm, frames=120):
    """빈손으로 서서 숨 쉰다. 무게가 한쪽 다리로 조금 실렸다 돌아온다."""
    action = new_action(arm, "Idle", frames)
    for f in range(0, frames + 1, 3):
        t = f / frames * math.tau
        breath = math.sin(2.0 * t)
        key_rot(arm, "pelvis", f, rz=1.2 * math.sin(t))
        key_rot(arm, "spine_02", f, rx=0.7 * breath)
        key_rot(arm, "spine_03", f, rx=-0.9 * breath)
        key_rot(arm, "head", f, rx=1.0 * math.sin(t + 0.5), ry=3.0 * math.sin(t))
        swing_arm(arm, "l", f, 0.0, 0.008 * breath)
        swing_arm(arm, "r", f, 0.0, 0.008 * breath)
        plant_feet(arm, f)
    cyclic(arm, action)
    return action


def author_look_back(arm, frames=75):
    """어깨 너머로 뒤를 본다. 처음 30프레임에 돌고 나머지는 그대로 멈춰 있다.

    사람이 돌 수 있는 것보다 조금 더(합 145도) 돈다. 거울 속에서 그만큼이면 어긋나 보인다.
    """
    action = new_action(arm, "LookBack", frames)
    for f in range(0, frames + 1, 2):
        a = min(1.0, f / 30.0)
        ease = a * a * (3.0 - 2.0 * a)
        key_rot(arm, "spine_02", f, ry=-12.0 * ease)
        key_rot(arm, "spine_03", f, ry=-13.0 * ease)
        key_rot(arm, "neck", f, ry=-25.0 * ease)
        key_rot(arm, "head", f, ry=-95.0 * ease, rx=-4.0 * ease)
        swing_arm(arm, "l", f, 0.0, 0.0)
        swing_arm(arm, "r", f, 0.0, 0.0)
        plant_feet(arm, f)
    for fc in action_fcurves(action):
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
    return action


def author_idle(arm, frames=90):
    action = new_action(arm, "Idle", frames)
    for f in range(0, frames + 1, 3):
        t = f / frames * math.tau
        breath = math.sin(2.0 * t)
        key_rot(arm, "spine_02", f, rx=0.8 * breath)
        key_rot(arm, "spine_03", f, rx=-1.0 * breath)
        key_rot(arm, "head", f, rx=1.5 * math.sin(t + 0.5), ry=10.0 * math.sin(t))
        hold_torch(arm, f, bob=0.006 * breath, sweep=0.05 * math.sin(t))
        hang_left(arm, f, 0.01 * breath)
        plant_feet(arm, f)
    cyclic(arm, action)
    return action


def author_look(arm, frames=90):
    """멈춰 서서 손전등으로 훑는다. 고개가 빛을 따라간다."""
    action = new_action(arm, "Look", frames)
    for f in range(0, frames + 1, 3):
        t = f / frames * math.tau
        sweep = math.sin(t)
        key_rot(arm, "spine_02", f, ry=-6.0 * sweep)
        key_rot(arm, "spine_03", f, ry=-5.0 * sweep, rx=1.0)
        key_rot(arm, "head", f, ry=-16.0 * sweep, rx=2.0 * math.sin(2.0 * t))
        hold_torch(arm, f, sweep=-0.24 * sweep, lift=0.03 * math.cos(2.0 * t))
        hang_left(arm, f, 0.0)
        plant_feet(arm, f)
    cyclic(arm, action)
    return action


def author_freeze(arm, frames=60):
    """위에서 노크가 들렸다. 어깨가 솟고 고개가 천장 쪽으로 굳는다. 손전등이 떤다."""
    action = new_action(arm, "Freeze", frames)
    for f in range(frames + 1):
        t = f / frames * math.tau
        tremble = 0.006 * math.sin(7.0 * t) + 0.004 * math.sin(13.0 * t + 1.0)
        key_rot(arm, "spine_01", f, rx=-3.0)
        key_rot(arm, "spine_03", f, rx=-4.0)
        key_rot(arm, "clavicle_l", f, rx=6.0)
        key_rot(arm, "clavicle_r", f, rx=6.0)
        key_rot(arm, "neck", f, rx=-10.0)
        key_rot(arm, "head", f, rx=-14.0 + 0.8 * math.sin(5.0 * t))
        hold_torch(arm, f, lift=0.06, tremble=tremble)
        hang_left(arm, f, 0.04)
        plant_feet(arm, f)
    cyclic(arm, action)
    return action


def author_grab(arm, frames=20):
    """왼손이 앞으로 뻗어 손목을 쥔다. 마지막 자세를 잡아 둔다."""
    action = new_action(arm, "Grab", frames)
    for f in range(0, frames + 1, 2):
        a = min(1.0, f / 12.0)
        ease = a * a * (3.0 - 2.0 * a)
        key_rot(arm, "spine_01", f, rx=6.0 * ease)
        key_rot(arm, "spine_02", f, rx=4.0 * ease, ry=8.0 * ease)
        key_rot(arm, "head", f, rx=4.0 * ease)
        shoulder = arm.pose.bones["upperarm_l"].head
        wrist = rest(arm, "lowerarm_l")
        target = wrist.lerp(Vector((0.58, 0.04, 1.02)), ease)
        solve_limb(arm, "upperarm_l", "lowerarm_l", "hand_l", target,
                   shoulder + Vector((-0.2, 0.35, -0.3)), f,
                   end_direction=Vector((1.0, -0.2, -0.1)))
        hold_torch(arm, f, lift=0.04 * ease, sweep=0.06 * ease)
        plant_feet(arm, f)
    for fc in action_fcurves(action):
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
    return action


def hang_left(arm, frame, lift):
    wrist = rest(arm, "lowerarm_l")
    elbow = rest(arm, "upperarm_l")
    solve_limb(arm, "upperarm_l", "lowerarm_l", "hand_l", wrist + Vector((0.02, -0.03, lift)),
               elbow + Vector((-0.3, 0.05, 0.0)), frame)


def plant_feet(arm, frame):
    for side in ("l", "r"):
        ankle = rest(arm, f"calf_{side}")
        knee = rest(arm, f"thigh_{side}")
        solve_limb(arm, f"thigh_{side}", f"calf_{side}", f"foot_{side}", ankle,
                   knee + Vector((0.6, 0.0, 0.0)), frame)


# --------------------------------------------------------------------------

def patrol_actions(arm):
    return {
        "Idle": author_idle(arm),
        "Walk": author_walk(arm, "Walk", 36, stride=0.46, lift=0.035, bob=0.014, lean=4.0,
                            arm_swing=0.10, crouch=0.025),
        "Run": author_walk(arm, "Run", 24, stride=0.66, lift=0.075, bob=0.028, lean=11.0,
                           arm_swing=0.20, crouch=0.06),
        "Look": author_look(arm),
        "Freeze": author_freeze(arm),
        "Grab": author_grab(arm),
    }


def main():
    args = parse_args()
    out_root = os.path.abspath(args.out) if args.out else os.path.abspath(
        os.path.join(HERE, "..", "..", "Content", "SourceArt", "Blender"))
    name = args.name
    out_dir = os.path.join(out_root, name)
    os.makedirs(out_dir, exist_ok=True)
    ig.reset_scene()
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.frame_start = 1

    high = import_glb(os.path.abspath(args.glb))
    ig.log(f"{name}: imported {ig.triangle_count(high)} tris")
    if args.yaw:
        bake_rotation(high, 0.0, 0.0, args.yaw)
    scale = fit_height_and_ground(high, args.height)
    global PROFILE
    PROFILE = args.profile
    if PROFILE == "patrol":
        plus, minus = hand_mass(high, args.height / 100.0)
        # 저작 좌표에서 오른손은 +Y다. 손전등 쪽이 -Y면 거울상이다.
        if minus > plus:
            mirror_handedness(high)
            ig.log(f"{name}: torch hand on -Y ({minus} vs {plus}); mirrored to keep it in the right hand")
        else:
            ig.log(f"{name}: torch hand on +Y ({plus} vs {minus})")

    source_mesh = bmesh.new()
    source_mesh.from_mesh(high.data)
    bmesh.ops.remove_doubles(source_mesh, verts=list(source_mesh.verts), dist=.00005)
    bmesh.ops.recalc_face_normals(source_mesh, faces=list(source_mesh.faces))
    source_mesh.to_mesh(high.data)
    source_mesh.free()
    ig.prepare_organic_source(high, roughness_floor=0.72)
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
    ig.remove_small_islands(low, keep_largest=False)
    ig.decimate(low, args.budget)
    for poly in low.data.polygons:
        poly.use_smooth = True
    low.data.materials.clear()
    ue_bounds = ig.bounds(low)

    low.hide_render = True
    ig.render_preview(high, os.path.join(out_dir, f"{name}_source_preview.png"))
    low.hide_render = False

    ig.mirror_y(low)
    ig.mirror_y(high)

    textures = {}
    if not args.no_bake:
        ig.uv_smart(low, margin=0.003)
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
        bsdf.inputs["Base Color"].default_value = (0.32, 0.36, 0.44, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.9
        ig.assign_material(low, mat)

    marks = find_landmarks(low)
    arm = build_armature(name, marks)
    skin(low, arm, marks)

    if PROFILE == "resident":
        actions = {
            "Idle": author_resident_idle(arm),
            "Walk": author_walk(arm, "Walk", 32, stride=0.50, lift=0.04, bob=0.012, lean=2.0,
                                arm_swing=0.12, crouch=0.02),
            "LookBack": author_look_back(arm),
        }
    else:
        actions = patrol_actions(arm)
    set_action_frame(arm, actions["Idle"], 1)
    props, pmat = debug_bone_props(arm)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_bones.png"), extra_objects=props,
                      camera_yaw_deg=-35.0, camera_pitch_deg=10.0)
    remove_props(props, pmat)
    for frame in (0, 9, 18, 27):
        set_action_frame(arm, actions["Walk"], frame)
        ig.render_preview(low, os.path.join(out_dir, f"{name}_walk_f{frame:02d}.png"),
                          camera_yaw_deg=-90.0, camera_pitch_deg=4.0)
    if PROFILE == "resident":
        set_action_frame(arm, actions["LookBack"], 60)
        ig.render_preview(low, os.path.join(out_dir, f"{name}_lookback.png"), camera_yaw_deg=160.0, camera_pitch_deg=4.0)
    else:
        for frame in (0, 12):
            set_action_frame(arm, actions["Run"], frame)
            ig.render_preview(low, os.path.join(out_dir, f"{name}_run_f{frame:02d}.png"),
                              camera_yaw_deg=-90.0, camera_pitch_deg=4.0)
        set_action_frame(arm, actions["Freeze"], 20)
        ig.render_preview(low, os.path.join(out_dir, f"{name}_freeze.png"), camera_yaw_deg=-30.0, camera_pitch_deg=6.0)
        set_action_frame(arm, actions["Grab"], 20)
        ig.render_preview(low, os.path.join(out_dir, f"{name}_grab.png"), camera_yaw_deg=-60.0, camera_pitch_deg=6.0)
    set_action_frame(arm, actions["Idle"], 1)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_preview.png"), camera_yaw_deg=-20.0)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_preview_torch.png"), flashlight=True, camera_yaw_deg=-40.0)

    if args.no_bake:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{name}.blend"))
        ig.log(f"{name}: previews only (--no-bake)")
        return

    slots = ig.finalize_slots(low)
    set_action_frame(arm, actions["Idle"], 1)
    fbx_path = os.path.join(out_dir, f"{name}.fbx")
    export_skeletal_fbx(fbx_path, arm, low)
    ig.write_manifest(out_dir, name, "hero", fbx_path, textures, low, slots,
                      notes=args.notes or f"생성 GLB {os.path.basename(args.glb)}에서 두 발 리깅",
                      origin="bottom", ue_bounds=ue_bounds)
    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    data["skeletal"] = True
    data["animations"] = {key: {"frames": int(a.frame_range[1]), "fps": FPS,
                                "loop": key not in ("Grab", "LookBack")} for key, a in actions.items()}
    data["generated_from"] = os.path.relpath(os.path.abspath(args.glb), out_root).replace("\\", "/")
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    if textures:
        ig.preview_material_from_bakes(low, textures)
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{name}.blend"))
    ig.log(f"{name}: done tris={ig.triangle_count(low)} bones={len(arm.data.bones)}")


if __name__ == "__main__":
    main()
