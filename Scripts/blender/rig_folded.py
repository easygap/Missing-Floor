"""생성 GLB를 계단의 「접힌 사람」 스켈레탈 메시로 다듬는다.

작업복 차림으로 허리가 앞으로 꺾이고, 머리가 있어야 할 자리에 모자만 늘어진 몸이다.
보는 동안에는 움직이지 않는다. 안 보는 사이에 다른 참으로 옮겨 가 있을 뿐이라 다리는
걷지 않는다. 그래서 뼈는 등뼈와 목(모자)만 둔다.

1. GLB를 읽고 yaw로 정면을 +X에 둔다(폰의 앞이 +X).
2. 키를 실제 cm에 맞추고 원점은 발밑 중심.
3. 저밀도 사본을 만들고 고밀도에서 BaseColor·Normal·ORM을 굽는다.
4. 뼈: root - pelvis - spine_01 - spine_02 - spine_03 - neck - head. 엉덩이 아래와
   다리는 pelvis에 통째로, 늘어진 팔은 어깨(spine_03)를 따라간다. 모자와 깃은 neck·head.
5. 동작 셋 — Idle(루프, 거의 안 보이는 숨), Lift(1회, 꺾인 목이 빛 쪽으로 천천히
   들린다. 깃 안이 비어 있는 게 보인다), Twitch(1회, 모자가 한 번 움찔한다).

    blender -b --factory-startup --python Scripts/blender/rig_folded.py -- \\
        --glb Content/SourceArt/Generated/StairFoldedFigure/trellis1024-s56/raw/pbr_00001_.glb \\
        --name SK_StairFoldedFigure --height 138 --yaw 90
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
    key_rot,
    new_action,
    remove_props,
    set_action_frame,
    vertex_array,
)
from rig_walker import fit_height_and_ground  # noqa: E402

FPS = 30
WEIGHT_FALLOFF = 0.06
MAX_INFLUENCES = 3


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--glb", required=True)
    parser.add_argument("--name", default="SK_StairFoldedFigure")
    parser.add_argument("--height", type=float, default=138.0, help="접힌 채 선 키 cm")
    parser.add_argument("--yaw", type=float, default=90.0, help="정면을 +X로 돌리는 Z 회전(도)")
    parser.add_argument("--voxel-remesh", type=float, default=0.004)
    parser.add_argument("--smooth-iterations", type=int, default=2)
    parser.add_argument("--budget", type=int, default=12000)
    parser.add_argument("--texture-size", type=int, default=2048)
    parser.add_argument("--out", default=None)
    parser.add_argument("--notes", default="")
    return parser.parse_args(argv)


def find_landmarks(ob):
    """접힌 몸의 등뼈 자리(미터, FBX 좌표: 앞 +X, 위 +Z).

    엉덩이는 몸 뒤쪽 가장 낮은 굴곡, 등의 꼭대기는 가장 높은 점, 모자는 가장 앞으로
    나온 덩어리다. 그 셋을 이어 등뼈를 휜 선으로 놓는다.
    """
    import numpy as np
    pts = vertex_array(ob)
    height = float(pts[:, 2].max())
    core = pts[np.abs(pts[:, 1]) < 0.12]

    top_band = core[core[:, 2] > 0.93 * height]
    top = Vector((float(np.median(top_band[:, 0])), 0.0, float(np.percentile(top_band[:, 2], 60))))
    front_cut = np.percentile(pts[:, 0], 97)
    cap_pts = pts[(pts[:, 0] > front_cut) & (pts[:, 2] > 0.6 * height)]
    cap = Vector((float(np.median(cap_pts[:, 0])), 0.0, float(np.median(cap_pts[:, 2]))))
    hip_band = core[(core[:, 2] > 0.55 * height) & (core[:, 2] < 0.62 * height)]
    rear = float(np.percentile(hip_band[:, 0], 15)) if len(hip_band) > 50 else -0.15
    pelvis = Vector((rear * 0.55, 0.0, 0.585 * height))
    back_band = core[(core[:, 2] > 0.70 * height) & (core[:, 2] < 0.78 * height)]
    back_rear = float(np.percentile(back_band[:, 0], 8)) if len(back_band) > 50 else -0.25
    spine1 = Vector((back_rear * 0.62, 0.0, 0.74 * height))
    spine2 = Vector((top.x - 0.14, 0.0, 0.88 * height))
    spine3 = Vector((top.x, 0.0, top.z))
    neck = Vector((top.x + 0.06, 0.0, top.z - 0.05))
    head = cap
    head_tip = cap + (cap - neck).normalized() * 0.10
    marks = {"pelvis": pelvis, "spine_01": spine1, "spine_02": spine2, "spine_03": spine3,
             "neck": neck, "head": head, "head_tip": head_tip, "height": height}
    for key in ("pelvis", "spine_01", "spine_02", "spine_03", "neck", "head"):
        ig.log(f"  folded {key}: {tuple(round(v, 3) for v in marks[key])}")
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
        # 로컬 X를 몸의 좌우 축으로 맞춘다. 그래야 rx가 숙이고 드는 축이 된다.
        b.align_roll(Vector((0.0, 0.0, 1.0)) if abs((b.tail - b.head).normalized().z) < 0.7
                     else Vector((1.0, 0.0, 0.0)))
        return b

    bone("root", (0.0, 0.0, 0.0), (0.0, 0.0, 0.08))
    bone("pelvis", marks["pelvis"], marks["spine_01"], "root")
    bone("spine_01", marks["spine_01"], marks["spine_02"], "pelvis", True)
    bone("spine_02", marks["spine_02"], marks["spine_03"], "spine_01", True)
    bone("spine_03", marks["spine_03"], marks["neck"], "spine_02", True)
    bone("neck", marks["neck"], marks["head"], "spine_03", True)
    bone("head", marks["head"], marks["head_tip"], "neck", True)
    bpy.ops.object.mode_set(mode="OBJECT")
    for pb in arm.pose.bones:
        pb.rotation_mode = "XYZ"
    return arm


def _segment_distance(p, a, b):
    ab = b - a
    denom = ab.length_squared
    t = 0.0 if denom < 1e-12 else max(0.0, min(1.0, (p - a).dot(ab) / denom))
    return (p - (a + ab * t)).length


def region_bones(co, marks):
    height = marks["height"]
    # 다리와 엉덩이 아래는 움직이지 않는다.
    if co.z < 0.50 * height:
        return ("pelvis",)
    # 모자와 깃. 등 꼭대기보다 앞에 늘어진 덩어리다.
    if co.x > marks["neck"].x - 0.02 and co.z > 0.62 * height and abs(co.y) < 0.16:
        return ("head", "neck")
    # 늘어진 팔. 어깨 바깥, 몸통 앞쪽 아래로 처진 부분은 어깨를 따라간다.
    if abs(co.y) > 0.13 and co.z < 0.85 * height and co.x > marks["pelvis"].x:
        return ("spine_03", "spine_02")
    return ("pelvis", "spine_01", "spine_02", "spine_03", "neck")


BONE_RADIUS = {"pelvis": 0.14, "spine_01": 0.15, "spine_02": 0.15, "spine_03": 0.14,
               "neck": 0.06, "head": 0.09}


def skin(mesh_ob, arm, marks):
    bones = {b.name: (b.head_local.copy(), b.tail_local.copy())
             for b in arm.data.bones if b.name != "root"}
    groups = {name: mesh_ob.vertex_groups.get(name) or mesh_ob.vertex_groups.new(name=name)
              for name in bones}
    for v in mesh_ob.data.vertices:
        allowed = region_bones(v.co, marks)
        scored = []
        for name in allowed:
            d = max(_segment_distance(v.co, *bones[name]) - BONE_RADIUS.get(name, 0.08), 0.0)
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
    mesh_ob.parent = arm


def author_idle(arm, frames=150):
    """거의 보이지 않는 숨. 보는 사람이 정말 움직였는지 확신하지 못할 만큼만."""
    action = new_action(arm, "Idle", frames)
    for f in range(0, frames + 1, 5):
        t = f / frames * math.tau
        key_rot(arm, "spine_02", f, rx=0.5 * math.sin(t))
        key_rot(arm, "spine_03", f, rx=-0.6 * math.sin(t))
        key_rot(arm, "neck", f, rx=0.8 * math.sin(t + 1.1), ry=0.6 * math.sin(0.5 * t))
    cyclic(arm, action)
    return action


def author_lift(arm, frames=90):
    """꺾인 목이 빛 쪽으로 들린다. 처음 54프레임에 들고, 나머지는 든 채 멈춘다.

    등은 조금만 펴고 목이 대부분을 든다. 사람 목이 그렇게 꺾일 수는 없어서 그게 보인다.
    """
    action = new_action(arm, "Lift", frames)
    for f in range(0, frames + 1, 2):
        a = min(1.0, f / 54.0)
        ease = a * a * a * (a * (a * 6.0 - 15.0) + 10.0)
        # 이 뼈들의 로컬 X는 몸의 오른쪽(-Y)을 본다. rx가 양수면 뼈 끝이 위로 든다.
        key_rot(arm, "spine_02", f, rx=6.0 * ease)
        key_rot(arm, "spine_03", f, rx=10.0 * ease)
        key_rot(arm, "neck", f, rx=58.0 * ease)
        key_rot(arm, "head", f, rx=22.0 * ease)
    for fc in action_fcurves(action):
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
    return action


def author_twitch(arm, frames=18):
    """모자가 한 번 움찔한다. 3프레임에 튀고 나머지로 돌아온다."""
    action = new_action(arm, "Twitch", frames)
    for f, amount in ((0, 0.0), (2, 0.7), (3, 1.0), (6, 0.45), (10, 0.15), (18, 0.0)):
        key_rot(arm, "neck", f, rx=9.0 * amount, rz=4.0 * amount)
        key_rot(arm, "head", f, rx=5.0 * amount)
        key_rot(arm, "spine_03", f, rx=1.5 * amount)
    return action


def main():
    args = parse_args()
    out_root = os.path.abspath(args.out) if args.out else os.path.abspath(
        os.path.join(HERE, "..", "..", "Content", "SourceArt", "Blender"))
    name = args.name
    out_dir = os.path.join(out_root, name)
    os.makedirs(out_dir, exist_ok=True)
    ig.reset_scene()
    bpy.context.scene.render.fps = FPS
    bpy.context.scene.frame_start = 1

    high = import_glb(os.path.abspath(args.glb))
    ig.log(f"{name}: imported {ig.triangle_count(high)} tris")
    if args.yaw:
        bake_rotation(high, 0.0, 0.0, args.yaw)
    fit_height_and_ground(high, args.height)
    source_mesh = bmesh.new()
    source_mesh.from_mesh(high.data)
    bmesh.ops.remove_doubles(source_mesh, verts=list(source_mesh.verts), dist=.00005)
    bmesh.ops.recalc_face_normals(source_mesh, faces=list(source_mesh.faces))
    source_mesh.to_mesh(high.data)
    source_mesh.free()
    ig.prepare_organic_source(high, roughness_floor=0.78)

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
    ig.uv_smart(low, margin=0.003)
    extrusion = max(args.voxel_remesh * 2.0, 0.006)
    textures = ig.bake_from_high(low, high, name, out_dir, size=args.texture_size,
                                 cage_extrusion=extrusion, max_ray_distance=extrusion * 3.0)
    high_mesh = high.data
    bpy.data.objects.remove(high, do_unlink=True)
    bpy.data.meshes.remove(high_mesh)
    ig.preview_material_from_bakes(low, textures)

    marks = find_landmarks(low)
    arm = build_armature(name, marks)
    skin(low, arm, marks)
    actions = {"Idle": author_idle(arm), "Lift": author_lift(arm), "Twitch": author_twitch(arm)}

    set_action_frame(arm, actions["Idle"], 1)
    props, pmat = debug_bone_props(arm)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_bones.png"), extra_objects=props,
                      camera_yaw_deg=-80.0, camera_pitch_deg=8.0)
    remove_props(props, pmat)
    for frame in (0, 30, 80):
        set_action_frame(arm, actions["Lift"], frame)
        ig.render_preview(low, os.path.join(out_dir, f"{name}_lift_f{frame:02d}_side.png"),
                          camera_yaw_deg=-80.0, camera_pitch_deg=4.0)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_lift_f80_front.png"), camera_yaw_deg=-10.0, camera_pitch_deg=4.0)
    set_action_frame(arm, actions["Idle"], 1)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_preview.png"), camera_yaw_deg=-25.0)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_preview_torch.png"), flashlight=True, camera_yaw_deg=-35.0)

    slots = ig.finalize_slots(low)
    set_action_frame(arm, actions["Idle"], 1)
    fbx_path = os.path.join(out_dir, f"{name}.fbx")
    export_skeletal_fbx(fbx_path, arm, low)
    ig.write_manifest(out_dir, name, "hero", fbx_path, textures, low, slots,
                      notes=args.notes or f"생성 GLB {os.path.basename(args.glb)}에서 접힌 몸 리깅",
                      origin="bottom", ue_bounds=ue_bounds)
    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    data["skeletal"] = True
    data["animations"] = {key: {"frames": int(a.frame_range[1]), "fps": FPS, "loop": key == "Idle"}
                          for key, a in actions.items()}
    data["generated_from"] = os.path.relpath(os.path.abspath(args.glb), out_root).replace("\\", "/")
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{name}.blend"))
    ig.log(f"{name}: done tris={ig.triangle_count(low)} bones={len(arm.data.bones)}")


if __name__ == "__main__":
    main()
