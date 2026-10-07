"""현관 신발장 위에서 집는 생활용 손전등. 생성 이미지는 형태 참고로만 쓴다."""

import hashlib
import json
import math
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
sys.path.insert(0, os.path.dirname(SCRIPT_DIR))

import bpy
from mathutils import Matrix, Vector
import ig_blender_lib as ig
import mesh_lod_contract


ASSET_NAME = "SM_PocketFlashlight"
SOURCE = os.path.abspath(os.path.join(
    SCRIPT_DIR, "../../Content/SourceArt/AI/PocketFlashlight_20261002.png"))


def make_flashlight():
    """길이 방향을 잠시 Z로 제작한 뒤, 렌즈가 +X를 보도록 눕힌다."""
    plastic = ig.mat_plastic("검정몸통", (0.025, 0.028, 0.030), roughness=0.58, bump=0.012)
    grip = ig.mat_rubber("고무그립", (0.017, 0.019, 0.020), roughness=0.74)
    silver = ig.mat_metal("은색테두리", (0.48, 0.50, 0.51), roughness=0.32, streak=0.025)
    reflector = ig.mat_metal("반사판", (0.68, 0.70, 0.71), roughness=0.17, streak=0.008)
    lens = ig.mat_gloss("전구렌즈", (0.63, 0.67, 0.67), roughness=0.10)
    orange = ig.mat_plastic("주황스위치", (0.56, 0.17, 0.035), roughness=0.56, bump=0.009)
    parts = []

    parts.append(ig.lathe("몸통과뒤뚜껑", [
        (0, -0.085), (0.0165, -0.085), (0.017, -0.082),
        (0.017, -0.071), (0.015, -0.069), (0.015, 0.027),
        (0.016, 0.030), (0, 0.030)], segments=20, material=plastic))
    parts.append(ig.lathe("헤드어깨", [
        (0, 0.025), (0.0155, 0.025), (0.0165, 0.036),
        (0.022, 0.059), (0.022, 0.064), (0, 0.064)],
        segments=24, material=plastic))
    # 앞 테두리 안은 실제로 파여 있다. 사진 한 장을 앞면에 붙이지 않는다.
    parts.append(ig.lathe("은색렌즈테두리", [
        (0.021, 0.061), (0.0225, 0.063), (0.0225, 0.083),
        (0.0215, 0.085), (0.0195, 0.085), (0.0195, 0.080),
        (0.021, 0.078), (0.021, 0.061)], segments=24, material=silver))
    parts.append(ig.lathe("오목한반사판", [
        (0.0194, 0.0805), (0.017, 0.077), (0.007, 0.066),
        (0.004, 0.065)], segments=24, material=reflector))
    parts.append(ig.lathe("전구렌즈", [
        (0, 0.065), (0.004, 0.065), (0.0038, 0.068),
        (0.0025, 0.069), (0, 0.0695)], segments=16, material=lens))

    # 몸통을 따라 난 열 줄의 낮은 돌기. 실루엣과 그림자로 방향을 읽을 수 있다.
    for index in range(10):
        angle = math.tau * index / 10
        parts.append(ig.box("그립돌기", (0.0021, 0.0011, 0.069),
            location=(math.sin(angle) * 0.0155, math.cos(angle) * 0.0155, -0.028),
            rotation=(0, 0, -angle), material=grip))

    # 눕힌 뒤 +Z로 향할 면은 저작 좌표의 -X다.
    parts.append(ig.box("스위치테두리", (0.003, 0.014, 0.027),
        location=(-0.0158, 0, 0.009), bevel=0.0011, segments=1, material=plastic))
    parts.append(ig.box("스위치", (0.003, 0.010, 0.019),
        location=(-0.018, 0, 0.010), bevel=0.0008, segments=1, material=orange))
    for offset in (-0.005, 0, 0.005):
        parts.append(ig.box("스위치요철", (0.0006, 0.008, 0.0008),
            location=(-0.0197, 0, 0.010 + offset), material=orange))

    # 넓은 헤드와 뒤뚜껑이 함께 상판에 닿는 작은 기울기다.
    lay_down = Matrix.Rotation(math.radians(87.7), 4, "Y")
    bpy.context.view_layer.update()
    for part in parts:
        part.matrix_world = lay_down @ part.matrix_world
    bpy.context.view_layer.update()
    coordinates = [part.matrix_world @ vertex.co
                   for part in parts for vertex in part.data.vertices]
    low = Vector(tuple(min(point[axis] for point in coordinates) for axis in range(3)))
    high = Vector(tuple(max(point[axis] for point in coordinates) for axis in range(3)))
    offset = Vector((-(high.x + low.x) * 0.5, -(high.y + low.y) * 0.5, -low.z))
    for part in parts:
        part.location += offset
    return parts


def main():
    ig.reset_scene()
    parts = make_flashlight()
    output_root = ig.out_root_from_argv()

    def light_small_prop(scene):
        # 공용 조명의 15cm 아래 오프셋은 이 작은 소품보다 크다. 바닥 아래로
        # 내려가지 않도록 카메라 곁에 두고 소품 중앙을 직접 비춘다.
        light = bpy.data.lights.get("__spot")
        if light is not None:
            spot = bpy.data.objects["__spot"]
            spot.location = scene.camera.location + Vector((0, 0, -0.015))
            spot.rotation_euler = (Vector((0, 0, 0.023)) - spot.location).to_track_quat("-Z", "Y").to_euler()
            light.energy = 12.0
            scene.view_settings.exposure = 0.0

    bpy.app.handlers.render_pre.append(light_small_prop)
    try:
        manifest = ig.build_asset(
            ASSET_NAME, "prop", parts, output_root,
            collision_parts=[parts[:3]], texture_size=512,
            origin="bottom-center", preview_yaw=-38.0, sharp_angle=38,
            notes="생성 참고 이미지를 보고 메시와 절차 재질로 재구성한 생활용 손전등. "
                  "약 17cm 길이, 헤드 지름 4.5cm. +X 렌즈, +Z 스위치, 상판에 누운 자세, 바닥 원점. "
                  "실제 그립 돌기·오목한 반사판·주황 스위치. 512px D/N/ORM, 재질 1개, UCX 1개.")
    finally:
        bpy.app.handlers.render_pre.remove(light_small_prop)
    if manifest["triangles"] > 1500 or manifest["slots"] != ["Baked"]:
        raise RuntimeError("손전등의 1500 삼각형·재질 하나 예산을 벗어났다")
    mesh = bpy.data.objects[ASSET_NAME]
    low, high = ig.bounds(mesh)
    size = high - low
    if not (0.169 < size.x < 0.174 and abs(size.y - 0.045) < 0.0002 and size.z < 0.05):
        raise RuntimeError(f"손전등 외곽 치수가 맞지 않는다: {tuple(size)}")
    if abs(low.z) > 0.0001:
        raise RuntimeError("손전등 바닥이 원점에 닿지 않는다")
    manifest["lod_plan"] = [
        {"level": level, "triangle_ratio": ratio, "screen_size": screen}
        for level, ratio, screen in mesh_lod_contract.lod_plan(ASSET_NAME, mesh_lod_contract.PROP)]
    manifest["texture_size"] = 512
    manifest["collision_hulls"] = 1
    manifest["source_image"] = "Content/SourceArt/AI/PocketFlashlight_20261002.png"
    manifest["source_usage"] = "형태와 색 참고. 원본 이미지를 텍스처로 사용하지 않음."
    with open(SOURCE, "rb") as handle:
        manifest["source_sha256"] = hashlib.sha256(handle.read()).hexdigest()
    with open(os.path.join(output_root, ASSET_NAME, "manifest.json"), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    ig.log(f"POCKET_FLASHLIGHT PASS tris={manifest['triangles']} slots=1 collision=1 texture=512")


if __name__ == "__main__":
    main()
