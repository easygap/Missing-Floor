"""위층 사람(SK_ListenerCrawler)에 웅크려 돌아서는 동작 셋을 더한다. 메시·뼈·굽기는 그대로다.

원본 .blend를 열어 TurnGatherIn·TurnGather·TurnGatherOut을 rig_crawler.py의 함수로 짜고, 몸 원점에서 가장 먼 정점까지의
거리(복도 폭의 반에 들어가야 한다)와 바닥 아래로 내려간 깊이를 재고, 옆·위·앞에서 찍는다. --export면 FBX(동작 전부)와
manifest를 다시 쓰고 .blend를 저장한다. 그다음 Import-BlenderAssets.ps1 -Only SK_ListenerCrawler로 반입한다.

    blender -b --factory-startup --python Scripts/blender/crawler_turn_gather.py -- \\
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

import crawler_wall_listen as cw  # noqa: E402
import ig_blender_lib as ig  # noqa: E402
import rig_crawler as rc  # noqa: E402

ACTIONS = {"TurnGatherIn": (14, False), "TurnGather": (24, True), "TurnGatherOut": (14, False)}


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--blend", default=os.path.join(cw.SOURCE, "SK_ListenerCrawler.blend"))
    parser.add_argument("--review", required=True)
    parser.add_argument("--pose-json")
    parser.add_argument("--export", action="store_true")
    return parser.parse_args(argv)


def measure(arm, mesh_ob, action, frame, regions):
    rc.set_action_frame(arm, action, frame)
    points = cw.deformed_points(mesh_ob)
    far = {}
    low = {}
    for index, point in enumerate(points):
        region = regions[index]
        far[region] = max(far.get(region, 0.0), math.hypot(point.x, point.y))
        low[region] = min(low.get(region, 9.0), point.z)
    worst = max(far, key=far.get)
    return {
        "action": action.name, "frame": frame,
        "radius_cm": round(far[worst] * 100.0, 1), "worst_region": worst,
        "x_cm": [round(min(p.x for p in points) * 100.0, 1), round(max(p.x for p in points) * 100.0, 1)],
        "y_cm": [round(min(p.y for p in points) * 100.0, 1), round(max(p.y for p in points) * 100.0, 1)],
        "below_floor_cm": round(-min(p.z for p in points) * 100.0, 1),
        "lowest_region": min(low, key=low.get),
        "top_cm": round(max(p.z for p in points) * 100.0, 1),
        "regions": {key: round(value * 100.0, 1) for key, value in sorted(far.items())},
    }


def main():
    args = parse_args()
    bpy.ops.wm.open_mainfile(filepath=os.path.abspath(args.blend))
    scene = bpy.context.scene
    scene.render.fps = rc.FPS
    arm, mesh_ob = cw.find_objects()
    pose = dict(rc.GATHER_POSE)
    if args.pose_json:
        with open(args.pose_json, encoding="utf-8-sig") as handle:
            pose.update(json.load(handle))
    for name in ACTIONS:
        old = bpy.data.actions.get(name)
        if old is not None:
            bpy.data.actions.remove(old)
    actions = {
        "TurnGatherIn": rc.author_turn_gather_in(arm, ACTIONS["TurnGatherIn"][0], pose=pose),
        "TurnGather": rc.author_turn_gather(arm, ACTIONS["TurnGather"][0], pose=pose),
        "TurnGatherOut": rc.author_turn_gather_out(arm, ACTIONS["TurnGatherOut"][0], pose=pose),
    }
    regions = cw.vertex_regions(mesh_ob)
    report = {"pose": pose, "frames": []}
    listen = bpy.data.actions.get("Listen")
    if listen is not None:
        report["rest"] = measure(arm, mesh_ob, listen, 1, regions)
        ig.log(f"  rest radius {report['rest']['radius_cm']} ({report['rest']['worst_region']})")
    for name, frame in (("TurnGatherIn", 5), ("TurnGatherIn", 9), ("TurnGatherIn", 14), ("TurnGather", 0), ("TurnGather", 6),
                        ("TurnGather", 12), ("TurnGather", 18), ("TurnGatherOut", 5), ("TurnGatherOut", 9)):
        row = measure(arm, mesh_ob, actions[name], frame, regions)
        report["frames"].append(row)
        ig.log(f"  {row['action']:>13} f{frame:02d} radius {row['radius_cm']} ({row['worst_region']}) "
               f"x {row['x_cm']} y {row['y_cm']} below {row['below_floor_cm']} ({row['lowest_region']}) top {row['top_cm']}")
    os.makedirs(args.review, exist_ok=True)
    with open(os.path.join(args.review, "turn_gather_report.json"), "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, ensure_ascii=False)

    views = (("side", -90.0, 8.0), ("three", -135.0, 24.0), ("front", 0.0, 12.0), ("back", 180.0, 14.0), ("top", -90.0, 80.0))
    rc.set_action_frame(arm, actions["TurnGather"], 0)
    for label, yaw, pitch in views:
        ig.render_preview(mesh_ob, os.path.join(args.review, f"gather_{label}.png"), camera_yaw_deg=yaw, camera_pitch_deg=pitch)
    rc.set_action_frame(arm, actions["TurnGather"], 6)
    ig.render_preview(mesh_ob, os.path.join(args.review, "gather_step.png"), camera_yaw_deg=-135.0, camera_pitch_deg=24.0)
    rc.set_action_frame(arm, actions["TurnGatherIn"], 7)
    ig.render_preview(mesh_ob, os.path.join(args.review, "gather_in_f07.png"), camera_yaw_deg=-135.0, camera_pitch_deg=24.0)

    if not args.export:
        return
    rc.set_action_frame(arm, actions["TurnGather"], 0)
    ig.render_preview(mesh_ob, os.path.join(cw.SOURCE, "SK_ListenerCrawler_turngather.png"),
                      camera_yaw_deg=-135.0, camera_pitch_deg=24.0)
    crawl = bpy.data.actions.get("Crawl")
    if crawl is not None:
        rc.set_action_frame(arm, crawl, 1)
    # rig_crawler.py와 같은 순서: 슬롯을 Baked 하나로 정리해 내보내고, 작업 파일에는
    # 구운 이미지로 만든 미리보기 재질을 다시 입힌다.
    ig.finalize_slots(mesh_ob)
    rc.export_skeletal_fbx(os.path.join(cw.SOURCE, "SK_ListenerCrawler.fbx"), arm, mesh_ob)
    ig.preview_material_from_bakes(mesh_ob, {key: os.path.join(cw.SOURCE, f"SK_ListenerCrawler_{key}.png")
                                             for key in ("D", "N", "ORM")})
    bpy.ops.file.pack_all()
    manifest_path = os.path.join(cw.SOURCE, "manifest.json")
    with open(manifest_path, encoding="utf-8") as handle:
        data = json.load(handle)
    for name, (frames, loop) in ACTIONS.items():
        data["animations"][name] = {"frames": frames, "fps": rc.FPS, "loop": loop}
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.blend))
    ig.log("SK_ListenerCrawler: turn gather actions exported")


if __name__ == "__main__":
    main()
