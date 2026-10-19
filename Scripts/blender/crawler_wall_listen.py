"""위층 사람(SK_ListenerCrawler)에 벽에 귀를 대는 동작 셋을 더한다. 메시·뼈·굽기는 그대로다.

원본 .blend를 열어 ListenWallIn·ListenWall·ListenWallOut을 rig_crawler.py의 함수로 짜고,
벽면을 세워 관통과 복도 쪽 폭을 재고, 정면·옆·뒤·위에서 찍는다. --export면 FBX(동작
전부)와 manifest를 다시 쓰고 .blend를 저장한다. 그다음 Import-BlenderAssets.ps1 -Only
SK_ListenerCrawler로 반입한다.

    blender -b --factory-startup --python Scripts/blender/crawler_wall_listen.py -- \\
        --review <미리보기 폴더> [--pose-json <덮어쓸 자세>] [--export]
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
from mathutils import Vector  # noqa: E402

import ig_blender_lib as ig  # noqa: E402
import rig_crawler as rc  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SOURCE = os.path.join(ROOT, "Content", "SourceArt", "Blender", "SK_ListenerCrawler")
ACTIONS = {"ListenWallIn": (24, False), "ListenWall": (120, True), "ListenWallOut": (18, False),
           "BangWall": (63, False)}


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--blend", default=os.path.join(SOURCE, "SK_ListenerCrawler.blend"))
    parser.add_argument("--review", required=True)
    parser.add_argument("--pose-json")
    parser.add_argument("--export", action="store_true")
    return parser.parse_args(argv)


def find_objects():
    arm = next(ob for ob in bpy.data.objects if ob.type == "ARMATURE")
    mesh = next(ob for ob in bpy.data.objects
                if ob.type == "MESH" and any(m.type == "ARMATURE" for m in ob.modifiers))
    return arm, mesh


def deformed_points(mesh_ob):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = mesh_ob.evaluated_get(dg)
    me = ev.to_mesh()
    points = [mesh_ob.matrix_world @ v.co for v in me.vertices]
    ev.to_mesh_clear()
    return points


def vertex_regions(mesh_ob):
    names = {g.index: g.name for g in mesh_ob.vertex_groups}
    regions = []
    for v in mesh_ob.data.vertices:
        best = max(v.groups, key=lambda g: g.weight, default=None)
        regions.append(names.get(best.group, "") if best else "")
    return regions


def measure(arm, mesh_ob, action, frame, wall_y, regions):
    rc.set_action_frame(arm, action, frame)
    pts = deformed_points(mesh_ob)
    ys = [p.y for p in pts]
    beyond = [i for i, p in enumerate(pts) if p.y < wall_y - 0.005]
    region_min = {}
    region_max = {}
    for i, p in enumerate(pts):
        r = regions[i]
        region_min[r] = min(region_min.get(r, 9.0), p.y)
        region_max[r] = max(region_max.get(r, -9.0), p.y)
    return {
        "regions": {r: [round(region_min[r], 3), round(region_max[r], 3)] for r in sorted(region_min)},
        "action": action.name, "frame": frame,
        "wall_side_min_y": round(min(ys), 4), "corridor_side_max_y": round(max(ys), 4),
        "beyond_wall_vertices": len(beyond),
        "beyond_regions": sorted({regions[i] for i in beyond}),
        "head_gap_cm": round((region_min.get("head", 9.0) - wall_y) * 100.0, 1),
        "hand_r_gap_cm": round((region_min.get("hand_r", 9.0) - wall_y) * 100.0, 1),
        "max_z": round(max(p.z for p in pts), 3),
    }


def wall_prop(wall_y):
    wall = ig.box("__wall", (2.6, 0.02, 1.2), location=(0.05, wall_y - 0.01, 0.6))
    mat = bpy.data.materials.new("__wall_mat")
    bsdf = ig._principled(mat)
    bsdf.inputs["Base Color"].default_value = (0.66, 0.63, 0.57, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.9
    ig.assign_material(wall, mat)
    return wall, mat


def main():
    args = parse_args()
    bpy.ops.wm.open_mainfile(filepath=os.path.abspath(args.blend))
    scene = bpy.context.scene
    scene.render.fps = rc.FPS
    arm, mesh_ob = find_objects()
    pose = dict(rc.WALL_POSE)
    if args.pose_json:
        with open(args.pose_json, encoding="utf-8-sig") as handle:
            pose.update(json.load(handle))
    for name in ACTIONS:
        old = bpy.data.actions.get(name)
        if old is not None:
            bpy.data.actions.remove(old)
    actions = {
        "ListenWallIn": rc.author_listen_wall_in(arm, ACTIONS["ListenWallIn"][0], pose=pose),
        "ListenWall": rc.author_listen_wall(arm, ACTIONS["ListenWall"][0], pose=pose),
        "ListenWallOut": rc.author_listen_wall_out(arm, ACTIONS["ListenWallOut"][0], pose=pose),
        "BangWall": rc.author_bang_wall(arm, ACTIONS["BangWall"][0], pose=pose),
    }
    listen = bpy.data.actions.get("Listen")
    wall_y = pose["wall_y"]
    regions = vertex_regions(mesh_ob)
    report = {"pose": pose, "rest": None, "frames": []}
    if listen is not None:
        report["rest"] = measure(arm, mesh_ob, listen, 1, wall_y, regions)
    for name, frame in (("ListenWallIn", 12), ("ListenWallIn", 24), ("ListenWall", 0), ("ListenWall", 30),
                        ("ListenWall", 60), ("ListenWall", 90), ("ListenWallOut", 9), ("ListenWallOut", 18),
                        ("BangWall", 0), ("BangWall", 10), ("BangWall", 19), ("BangWall", 37), ("BangWall", 60)):
        report["frames"].append(measure(arm, mesh_ob, actions[name], frame, wall_y, regions))
    os.makedirs(args.review, exist_ok=True)
    with open(os.path.join(args.review, "wall_listen_report.json"), "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, ensure_ascii=False)
    for row in report["frames"]:
        ig.log(f"  {row['action']:>13} f{row['frame']:03d} wall {row['wall_side_min_y']:+.3f} "
               f"corridor {row['corridor_side_max_y']:+.3f} beyond {row['beyond_wall_vertices']} "
               f"{row['beyond_regions']} head {row['head_gap_cm']} cm hand {row['hand_r_gap_cm']} cm")

    wall, wmat = wall_prop(wall_y)
    views = (("corridor", 180.0, 14.0), ("approach", -135.0, 24.0), ("front", -90.0, 8.0),
             ("back", 90.0, 10.0), ("top", 180.0, 80.0))
    rc.set_action_frame(arm, actions["ListenWall"], 30)
    for label, yaw, pitch in views:
        ig.render_preview(mesh_ob, os.path.join(args.review, f"wall_{label}.png"), extra_objects=[wall],
                          camera_yaw_deg=yaw, camera_pitch_deg=pitch)
    rc.set_action_frame(arm, actions["BangWall"], 19)
    ig.render_preview(mesh_ob, os.path.join(args.review, "wall_bang_f19.png"), extra_objects=[wall],
                      camera_yaw_deg=-135.0, camera_pitch_deg=24.0)
    for frame in (6, 12, 18):
        rc.set_action_frame(arm, actions["ListenWallIn"], frame)
        ig.render_preview(mesh_ob, os.path.join(args.review, f"wall_in_f{frame:02d}.png"), extra_objects=[wall],
                          camera_yaw_deg=-135.0, camera_pitch_deg=24.0)
    bpy.data.objects.remove(wall, do_unlink=True)
    bpy.data.materials.remove(wmat)

    if not args.export:
        return
    # 반입용 미리보기 두 장(다가오며 보는 쪽, 위)을 원본 폴더에 남긴다.
    wall, wmat = wall_prop(wall_y)
    rc.set_action_frame(arm, actions["ListenWall"], 30)
    ig.render_preview(mesh_ob, os.path.join(SOURCE, "SK_ListenerCrawler_listenwall.png"), extra_objects=[wall],
                      camera_yaw_deg=-135.0, camera_pitch_deg=24.0)
    ig.render_preview(mesh_ob, os.path.join(SOURCE, "SK_ListenerCrawler_listenwall_top.png"), extra_objects=[wall],
                      camera_yaw_deg=180.0, camera_pitch_deg=80.0)
    bpy.data.objects.remove(wall, do_unlink=True)
    bpy.data.materials.remove(wmat)
    crawl = bpy.data.actions.get("Crawl")
    if crawl is not None:
        rc.set_action_frame(arm, crawl, 1)
    # rig_crawler.py와 같은 순서: 슬롯을 Baked 하나로 정리해 내보내고, 작업 파일에는
    # 구운 이미지로 만든 미리보기 재질을 다시 입힌다.
    ig.finalize_slots(mesh_ob)
    rc.export_skeletal_fbx(os.path.join(SOURCE, "SK_ListenerCrawler.fbx"), arm, mesh_ob)
    ig.preview_material_from_bakes(mesh_ob, {key: os.path.join(SOURCE, f"SK_ListenerCrawler_{key}.png")
                                             for key in ("D", "N", "ORM")})
    bpy.ops.file.pack_all()
    manifest_path = os.path.join(SOURCE, "manifest.json")
    with open(manifest_path, encoding="utf-8") as handle:
        data = json.load(handle)
    for name, (frames, loop) in ACTIONS.items():
        data["animations"][name] = {"frames": frames, "fps": rc.FPS, "loop": loop}
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.blend))
    ig.log("SK_ListenerCrawler: wall listen actions exported")


if __name__ == "__main__":
    main()
