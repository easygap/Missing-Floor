"""rig_walker.py로 만든 두 발 인물의 가중치만 다시 매긴다. 메시·뼈·동작·굽기는 그대로다.

원본 .blend를 열어 기존 가중치와 Armature 모디파이어를 지우고 rig_walker.skin을 다시 부른다.
--soft-upper-arm이면 위팔 띠 섞기(skin_soft)를 쓴다. 동작마다 삼각형이 얼마나 늘어나는지
(자세 면적 / 기본 자세 면적) 재고, 정면·옆·뒤에서 찍는다. --export면 FBX와 .blend를 다시 쓴다.
그다음 Import-BlenderAssets.ps1 -Only <이름>으로 반입한다.

--reauthor Crouch Hold처럼 동작 이름을 주면 그 동작만 지우고 rig_walker의 지금 저작 함수로 다시
만든다. 처음 뽑을 때 없던 동작도 이렇게 더하고, manifest.json의 동작 목록도 맞춘다.
--keep-weights와 같이 쓰면 메시와 가중치는 손대지 않는다.

    blender -b --factory-startup --python Scripts/blender/walker_reskin.py -- \\
        --name SK_Jeongwoo --profile mover --soft-upper-arm --review <폴더> [--export]
    blender -b --factory-startup --python Scripts/blender/walker_reskin.py -- \\
        --name SK_Jeongwoo --profile mover --keep-weights --reauthor Crouch Hold --review <폴더> --export
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import bpy  # noqa: E402

import ig_blender_lib as ig  # noqa: E402
import rig_walker as rw  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
# 늘어남을 재는 프레임. 각 동작에서 팔이 가장 멀리 가는 때를 고른다.
PROBE_FRAMES = (("Idle", 1), ("Walk", 8), ("Walk", 24), ("Carry", 10), ("CarryIdle", 40), ("Crouch", 40),
                ("Brace", 20), ("Brace", 45), ("Hold", 30), ("HoldDoor", 30), ("LookBack", 60))
# 찍는 자세와 방향(카메라 yaw: -90 정면, 0 오른쪽 옆, 90 뒤).
SHOTS = (("Brace", 20, -90.0), ("Brace", 20, 0.0), ("Brace", 20, 90.0), ("Brace", 20, -45.0),
         ("Crouch", 40, -90.0), ("Crouch", 40, 0.0), ("Crouch", 40, 90.0), ("Crouch", 40, -35.0),
         ("Hold", 30, -90.0), ("Hold", 30, 0.0), ("Hold", 30, 90.0),
         ("HoldDoor", 30, -90.0), ("HoldDoor", 30, 0.0), ("Walk", 8, -60.0), ("Walk", 8, -90.0), ("Walk", 24, 0.0),
         ("Idle", 1, -90.0), ("Idle", 1, 0.0), ("LookBack", 60, 160.0), ("Carry", 10, -60.0))
# 다시 만들 수 있는 동작과 저작 함수. 이름은 manifest.json과 반입 뒤 A_<이름>_<동작>에 그대로 쓰인다.
# 원본 폴더의 미리보기는 rig_walker가 처음 뽑을 때와 같은 프레임·방향으로 다시 찍는다.
PREVIEWS = {"Crouch": (30, -35.0), "Hold": (30, -70.0), "Brace": (20, -70.0), "HoldDoor": (30, -20.0),
            "CarryIdle": (30, -35.0), "Idle": (1, -20.0), "LookBack": (60, 160.0), "Walk": (9, -90.0)}
# 미리보기 파일 이름. 처음 뽑을 때 Idle은 _preview, Walk는 _walk_f09로 찍었다.
PREVIEW_FILES = {"Idle": "preview", "Walk": "walk_f09"}
AUTHORS = {
    "Idle": (lambda arm: rw.author_resident_idle(arm), 120, True),
    "Walk": (lambda arm: rw.author_walk(arm, "Walk", 32, stride=0.50, lift=0.04, bob=0.012, lean=2.0,
                                        arm_swing=0.12, crouch=0.02), 32, True),
    "LookBack": (lambda arm: rw.author_look_back(arm), 75, False),
    "Crouch": (lambda arm: rw.author_crouch(arm), 90, True),
    "Hold": (lambda arm: rw.author_hold(arm), 60, True),
    "Brace": (lambda arm: rw.author_brace(arm), 60, True),
    "HoldDoor": (lambda arm: rw.author_hold_door(arm), 90, True),
    "CarryIdle": (lambda arm: rw.author_carry_idle(arm), 90, True),
}


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", required=True)
    parser.add_argument("--profile", choices=["patrol", "resident", "officer", "mover"], required=True)
    parser.add_argument("--soft-upper-arm", action="store_true")
    parser.add_argument("--review", required=True)
    parser.add_argument("--export", action="store_true")
    parser.add_argument("--keep-weights", action="store_true", help="가중치를 바꾸지 않고 재기만 한다(전후 비교)")
    parser.add_argument("--reauthor", nargs="*", default=[], choices=sorted(AUTHORS),
                        help="지우고 다시 만들 동작(rig_walker의 지금 저작 함수로)")
    return parser.parse_args(argv)


def reauthor(arm, mesh, names, source):
    """동작을 지우고 다시 만든다. 액션 이름이 같아야 FBX 테이크와 반입 이름이 그대로다."""
    if rw.PROFILE in rw.ARM_HANG_PROFILES:
        rw.set_arm_hang(marks_from_armature(arm, mesh), os.path.basename(source))
    manifest_path = os.path.join(source, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    for name in names:
        old = bpy.data.actions.get(name)
        if old is not None:
            bpy.data.actions.remove(old)
        author, frames, loop = AUTHORS[name]
        action = author(arm)
        if action.name != name:
            raise RuntimeError(f"{name}: action came back as {action.name}")
        manifest.setdefault("animations", {})[name] = {"frames": frames, "fps": rw.FPS, "loop": loop}
        ig.log(f"  reauthored {name} ({frames} frames)")
    return manifest_path, manifest


def find_objects():
    arm = next(ob for ob in bpy.data.objects if ob.type == "ARMATURE")
    mesh = next(ob for ob in bpy.data.objects
                if ob.type == "MESH" and any(m.type == "ARMATURE" for m in ob.modifiers))
    return arm, mesh


def marks_from_armature(arm, mesh):
    """뼈대에서 관절 자리를 다시 읽는다. 처음 뽑을 때 잡은 자리와 같다."""
    import numpy as np
    bones = arm.data.bones
    pts = np.array([list(v.co) for v in mesh.data.vertices])
    height = float(pts[:, 2].max())
    marks = {
        "height": height,
        "gap": rw.arm_gap(pts, height),
        "pelvis": bones["pelvis"].head_local.copy(),
        "spine_03": bones["spine_03"].head_local.copy(),
        "arms": {},
    }
    for side in ("l", "r"):
        marks["arms"][side] = (bones[f"upperarm_{side}"].head_local.copy(), bones[f"lowerarm_{side}"].head_local.copy(),
                               bones[f"hand_{side}"].head_local.copy(), bones[f"hand_{side}"].tail_local.copy())
    return marks


def stretch_report(arm, mesh):
    rest = [v.co.copy() for v in mesh.data.vertices]
    tris = []
    for poly in mesh.data.polygons:
        vs = list(poly.vertices)
        for i in range(1, len(vs) - 1):
            tris.append((vs[0], vs[i], vs[i + 1]))

    def area(p, a, b, c):
        return ((p[b] - p[a]).cross(p[c] - p[a])).length * 0.5

    rest_area = [area(rest, *t) for t in tris]
    rows = []
    for name, frame in PROBE_FRAMES:
        action = bpy.data.actions.get(name)
        if action is None:
            continue
        rw.set_action_frame(arm, action, frame)
        dg = bpy.context.evaluated_depsgraph_get()
        ev = mesh.evaluated_get(dg)
        me = ev.to_mesh()
        posed = [v.co.copy() for v in me.vertices]
        ev.to_mesh_clear()
        ratios = [area(posed, *t) / rest_area[i] for i, t in enumerate(tris) if rest_area[i] > 1e-7]
        rows.append({"action": name, "frame": frame, "over2": sum(r > 2.0 for r in ratios),
                     "over3": sum(r > 3.0 for r in ratios), "over5": sum(r > 5.0 for r in ratios),
                     "max": round(max(ratios), 2)})
    return rows


def main():
    args = parse_args()
    source = os.path.join(ROOT, "Content", "SourceArt", "Blender", args.name)
    blend = os.path.join(source, f"{args.name}.blend")
    bpy.ops.wm.open_mainfile(filepath=blend)
    rw.PROFILE = args.profile
    rw.SOFT_UPPER_ARM = args.soft_upper_arm
    arm, mesh = find_objects()
    manifest_update = reauthor(arm, mesh, args.reauthor, source) if args.reauthor else None
    if not args.keep_weights:
        for mod in [m for m in mesh.modifiers if m.type == "ARMATURE"]:
            mesh.modifiers.remove(mod)
        mesh.vertex_groups.clear()
        rw.skin(mesh, arm, marks_from_armature(arm, mesh))
    rows = stretch_report(arm, mesh)
    os.makedirs(args.review, exist_ok=True)
    with open(os.path.join(args.review, "stretch.json"), "w", encoding="utf-8") as handle:
        json.dump(rows, handle, indent=1)
    for row in rows:
        ig.log(f"  {row['action']:>9} f{row['frame']:02d} >2x {row['over2']:4d} >3x {row['over3']:4d} "
               f">5x {row['over5']:4d} max {row['max']}")
    for name, frame, yaw in SHOTS:
        action = bpy.data.actions.get(name)
        if action is None:
            continue
        rw.set_action_frame(arm, action, frame)
        ig.render_preview(mesh, os.path.join(args.review, f"{name.lower()}_f{frame:02d}_yaw{int(yaw):+04d}.png"),
                          camera_yaw_deg=yaw, camera_pitch_deg=6.0)
    if not args.export:
        return
    for name in args.reauthor:
        frame, yaw = PREVIEWS[name]
        rw.set_action_frame(arm, bpy.data.actions[name], frame)
        ig.render_preview(mesh, os.path.join(source, f"{args.name}_{PREVIEW_FILES.get(name, name.lower())}.png"),
                          camera_yaw_deg=yaw, camera_pitch_deg=4.0)
    idle = bpy.data.actions.get("Idle")
    if idle is not None:
        rw.set_action_frame(arm, idle, 1)
    ig.finalize_slots(mesh)
    rw.export_skeletal_fbx(os.path.join(source, f"{args.name}.fbx"), arm, mesh)
    if manifest_update is not None:
        manifest_path, manifest = manifest_update
        with open(manifest_path, "w", encoding="utf-8") as handle:
            json.dump(manifest, handle, indent=2, ensure_ascii=False)
            handle.write("\n")
    ig.preview_material_from_bakes(mesh, {key: os.path.join(source, f"{args.name}_{key}.png") for key in ("D", "N", "ORM")})
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=blend)
    ig.log(f"{args.name}: reskinned and exported")


if __name__ == "__main__":
    main()
