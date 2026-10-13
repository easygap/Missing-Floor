"""서일영이 5층 판재 더미 밑에 잃어버린 소형 녹음기. 생성 이미지는 형태와 색 참고로만 쓴다.

1년 동안 공사 바닥에 있었다. 윗면에만 석고 가루가 앉고, 손목끈은 바래고 해졌다.
렌즈 쪽 같은 앞뒤가 없는 물건이라 길이 방향을 X로 두고, 액정 쪽이 +X, 끈이 -X다.
"""

import hashlib
import json
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
sys.path.insert(0, os.path.dirname(SCRIPT_DIR))

import bpy
from mathutils import Vector
import ig_blender_lib as ig
import mesh_lod_contract


ASSET_NAME = "SM_SeoVoiceRecorder"
SOURCE = os.path.abspath(os.path.join(
    SCRIPT_DIR, "../../Content/SourceArt/AI/SeoVoiceRecorder_20261012.png"))

LENGTH = 0.110
WIDTH = 0.038
THICK = 0.015


def mat_dusty(name, base, roughness, dust=(0.50, 0.49, 0.46), amount=0.55, grain_scale=260.0):
    """윗면에만 가루가 앉은 플라스틱. 위를 보는 면일수록, 큰 얼룩 안쪽일수록 짙다.

    점묘가 생기지 않게 가루 마스크는 2~3 cm 크기의 부드러운 노이즈로만 만든다.
    면 방향은 Z만 본다. 반입 전에 Y를 뒤집어도 가루 자리가 바뀌지 않는다.
    """
    mat = ig.mat_plastic(name, base, roughness=roughness, bump=0.012, grain_scale=grain_scale)
    tree = mat.node_tree
    nodes, links = tree.nodes, tree.links
    bsdf = next(node for node in nodes if node.type == "BSDF_PRINCIPLED")
    geometry = nodes.new("ShaderNodeNewGeometry")
    to_object = nodes.new("ShaderNodeVectorTransform")
    to_object.vector_type = "NORMAL"
    to_object.convert_from = "WORLD"
    to_object.convert_to = "OBJECT"
    links.new(geometry.outputs["Normal"], to_object.inputs["Vector"])
    split = nodes.new("ShaderNodeSeparateXYZ")
    links.new(to_object.outputs["Vector"], split.inputs["Vector"])
    facing = nodes.new("ShaderNodeMapRange")
    facing.inputs["From Min"].default_value = 0.35
    facing.inputs["From Max"].default_value = 0.95
    links.new(split.outputs["Z"], facing.inputs["Value"])
    coords = nodes.new("ShaderNodeTexCoord")
    patches = nodes.new("ShaderNodeTexNoise")
    patches.inputs["Scale"].default_value = 38.0
    patches.inputs["Detail"].default_value = 1.0
    patches.inputs["Roughness"].default_value = 0.4
    links.new(coords.outputs["Object"], patches.inputs["Vector"])
    patch_range = nodes.new("ShaderNodeMapRange")
    patch_range.inputs["From Min"].default_value = 0.38
    patch_range.inputs["From Max"].default_value = 0.66
    links.new(patches.outputs["Fac"], patch_range.inputs["Value"])
    mask = nodes.new("ShaderNodeMath")
    mask.operation = "MULTIPLY"
    links.new(facing.outputs["Result"], mask.inputs[0])
    links.new(patch_range.outputs["Result"], mask.inputs[1])
    strength = nodes.new("ShaderNodeMath")
    strength.operation = "MULTIPLY"
    strength.inputs[1].default_value = amount
    links.new(mask.outputs["Value"], strength.inputs[0])
    base_rgb = nodes.new("ShaderNodeRGB")
    base_rgb.outputs[0].default_value = (*base, 1.0)
    dust_rgb = nodes.new("ShaderNodeRGB")
    dust_rgb.outputs[0].default_value = (*dust, 1.0)
    mix = nodes.new("ShaderNodeMixRGB")
    links.new(strength.outputs["Value"], mix.inputs["Fac"])
    links.new(base_rgb.outputs[0], mix.inputs["Color1"])
    links.new(dust_rgb.outputs[0], mix.inputs["Color2"])
    links.new(mix.outputs[0], bsdf.inputs["Base Color"])
    # 가루가 앉은 곳은 광택이 죽는다.
    rough = nodes.new("ShaderNodeMapRange")
    rough.inputs["From Min"].default_value = 0.0
    rough.inputs["From Max"].default_value = 1.0
    rough.inputs["To Min"].default_value = roughness
    rough.inputs["To Max"].default_value = 0.92
    links.new(strength.outputs["Value"], rough.inputs["Value"])
    links.new(rough.outputs["Result"], bsdf.inputs["Roughness"])
    return mat


def mat_grille(name):
    """스피커 구멍. 구멍을 메시로 뚫지 않고 2.6 mm 간격의 점 격자를 굽는다.

    격자는 폭 방향 가운데를 기준으로 대칭이라 반입 전 Y 뒤집기에도 그대로다.
    """
    mat = mat_dusty(name, (0.028, 0.029, 0.031), 0.62, amount=0.30)
    tree = mat.node_tree
    nodes, links = tree.nodes, tree.links
    bsdf = next(node for node in nodes if node.type == "BSDF_PRINCIPLED")
    mix = bsdf.inputs["Base Color"].links[0].from_node
    coords = nodes.new("ShaderNodeTexCoord")
    scale = nodes.new("ShaderNodeVectorMath")
    scale.operation = "SCALE"
    scale.inputs["Scale"].default_value = 1.0 / 0.0026
    links.new(coords.outputs["Object"], scale.inputs[0])
    cell = nodes.new("ShaderNodeVectorMath")
    cell.operation = "FRACTION"
    links.new(scale.outputs["Vector"], cell.inputs[0])
    centre = nodes.new("ShaderNodeVectorMath")
    centre.operation = "SUBTRACT"
    centre.inputs[1].default_value = (0.5, 0.5, 0.0)
    links.new(cell.outputs["Vector"], centre.inputs[0])
    flat = nodes.new("ShaderNodeVectorMath")
    flat.operation = "MULTIPLY"
    flat.inputs[1].default_value = (1.0, 1.0, 0.0)
    links.new(centre.outputs["Vector"], flat.inputs[0])
    radius = nodes.new("ShaderNodeVectorMath")
    radius.operation = "LENGTH"
    links.new(flat.outputs["Vector"], radius.inputs[0])
    hole = nodes.new("ShaderNodeMapRange")
    hole.inputs["From Min"].default_value = 0.20
    hole.inputs["From Max"].default_value = 0.30
    hole.inputs["To Min"].default_value = 1.0
    hole.inputs["To Max"].default_value = 0.0
    links.new(radius.outputs["Value"], hole.inputs["Value"])
    dark = nodes.new("ShaderNodeMixRGB")
    dark.inputs["Color2"].default_value = (0.004, 0.004, 0.005, 1.0)
    links.new(hole.outputs["Result"], dark.inputs["Fac"])
    links.new(mix.outputs[0], dark.inputs["Color1"])
    links.new(dark.outputs[0], bsdf.inputs["Base Color"])
    return mat


def make_recorder():
    body = mat_dusty("검정몸통", (0.020, 0.021, 0.023), 0.70)
    bezel = mat_dusty("액정테두리", (0.012, 0.012, 0.013), 0.40, amount=0.35)
    screen = ig.mat_gloss("꺼진액정", (0.060, 0.072, 0.064), roughness=0.16)
    red = mat_dusty("녹음단추", (0.42, 0.035, 0.03), 0.48, amount=0.30)
    grey = mat_dusty("회색단추", (0.20, 0.205, 0.21), 0.70, amount=0.35)
    grille = mat_grille("스피커망")
    strap = mat_dusty("바랜주황끈", (0.50, 0.20, 0.075), 0.88, dust=(0.55, 0.52, 0.47),
                      amount=0.45, grain_scale=900.0)
    sleeve = mat_dusty("끈조임", (0.014, 0.014, 0.015), 0.50, amount=0.25)
    parts = []

    parts.append(ig.box("몸통", (LENGTH, WIDTH, THICK), location=(0, 0, THICK * 0.5),
                        bevel=0.0042, segments=3, material=body))
    top = THICK
    # 액정은 몸통 윗면에서 0.4 mm 내려앉은 창이고, 테두리가 0.6 mm 솟는다.
    # 얇은 판의 베벨 폭은 두께의 절반을 넘기지 않는다. 넘으면 면이 뒤집혀 AO가 0으로 구워진다.
    parts.append(ig.box("액정테두리", (0.031, 0.026, 0.0010), location=(0.030, 0, top + 0.0002),
                        bevel=0.0004, segments=1, material=bezel))
    parts.append(ig.box("액정", (0.026, 0.020, 0.0008), location=(0.030, 0, top + 0.0005),
                        material=screen))
    parts.append(ig.cylinder("녹음단추", 0.0047, 0.0016, location=(0.004, 0.0085, top + 0.0006),
                             segments=16, bevel=0.0006, material=red))
    for index, (x, y) in enumerate(((0.010, -0.0085), (-0.001, -0.0085), (-0.006, 0.0085))):
        parts.append(ig.box(f"회색단추{index}", (0.0082, 0.0046, 0.0013), location=(x, y, top + 0.0004),
                            bevel=0.0005, segments=2, material=grey))
    parts.append(ig.box("스피커망", (0.030, 0.026, 0.0004), location=(-0.030, 0, top + 0.0001),
                        material=grille))
    # 액정 쪽 끝의 마이크 구멍.
    parts.append(ig.cylinder("마이크", 0.0010, 0.0006, location=(0.0548, 0, top - 0.0042),
                             rotation=(0, 1.5708, 0), segments=8, material=screen))

    # 손목끈. 고리 구멍에서 나와 바닥에 늘어진 납작한 끈이다. 둥근 관을 높이
    # 방향으로 0.6배 눌러 폭 4.4 mm, 두께 2.6 mm로 만든다. 경로 높이는 미리 나눠 둔다.
    flatten = 0.6
    loop = [(-0.050, 0.011, 0.0060), (-0.058, 0.015, 0.0040), (-0.070, 0.022, 0.0014),
            (-0.090, 0.028, 0.0013), (-0.106, 0.027, 0.0013), (-0.114, 0.018, 0.0013),
            (-0.109, 0.008, 0.0013), (-0.090, 0.008, 0.0013), (-0.071, 0.011, 0.0014),
            (-0.058, 0.010, 0.0040), (-0.050, 0.011, 0.0060)]
    cord = ig.pipe("손목끈", [(x, y, z / flatten) for x, y, z in loop], 0.0022, resolution=8,
                   corner_radius=0.004, corner_steps=3, caps=False, material=strap)
    for vertex in cord.data.vertices:
        vertex.co.z *= flatten
    parts.append(cord)
    parts.append(ig.cylinder("끈조임", 0.0026, 0.0060, location=(-0.0625, 0.0133, 0.0030),
                             rotation=(0, 1.5708, 0.45), segments=10, material=sleeve))

    bpy.context.view_layer.update()
    coordinates = [part.matrix_world @ vertex.co for part in parts for vertex in part.data.vertices]
    low_z = min(point.z for point in coordinates)
    for part in parts:
        part.location.z -= low_z
    return parts


def main():
    ig.reset_scene()
    parts = make_recorder()
    output_root = ig.out_root_from_argv()

    def light_small_prop(scene):
        light = bpy.data.lights.get("__spot")
        if light is not None:
            spot = bpy.data.objects["__spot"]
            spot.location = scene.camera.location + Vector((0, 0, -0.012))
            spot.rotation_euler = (Vector((0, 0, 0.008)) - spot.location).to_track_quat("-Z", "Y").to_euler()
            light.energy = 9.0
            scene.view_settings.exposure = 0.0

    bpy.app.handlers.render_pre.append(light_small_prop)
    try:
        manifest = ig.build_asset(
            ASSET_NAME, "prop", parts, output_root,
            collision_parts=[parts[:1]], texture_size=512,
            origin="bottom-center", preview_yaw=-32.0, sharp_angle=38,
            notes="생성 참고 이미지를 보고 메시와 절차 재질로 재구성한 소형 녹음기. 몸통 11 x 3.8 x 1.5 cm, "
                  "액정 쪽 +X, 손목끈 -X, 화면이 +Z로 바닥에 누운 자세, 바닥 원점(끈 포함 외곽의 가운데). "
                  "윗면에만 석고 가루, 스피커 구멍은 점 격자로 굽는다. 512px D/N/ORM, 재질 1개, UCX 1개(몸통).")
    finally:
        bpy.app.handlers.render_pre.remove(light_small_prop)
    if manifest["triangles"] > 1600 or manifest["slots"] != ["Baked"]:
        raise RuntimeError("녹음기의 1600 삼각형·재질 하나 예산을 벗어났다")
    mesh = bpy.data.objects[ASSET_NAME]
    low, high = ig.bounds(mesh)
    size = high - low
    if not (0.16 < size.x < 0.18 and 0.037 < size.y < 0.050 and 0.0155 < size.z < 0.0185):
        raise RuntimeError(f"녹음기 외곽 치수가 맞지 않는다: {tuple(size)}")
    if abs(low.z) > 0.0001:
        raise RuntimeError("녹음기 바닥이 원점에 닿지 않는다")
    manifest["lod_plan"] = [
        {"level": level, "triangle_ratio": ratio, "screen_size": screen}
        for level, ratio, screen in mesh_lod_contract.lod_plan(ASSET_NAME, mesh_lod_contract.PROP)]
    manifest["texture_size"] = 512
    manifest["collision_hulls"] = 1
    manifest["source_image"] = "Content/SourceArt/AI/SeoVoiceRecorder_20261012.png"
    manifest["source_usage"] = "형태와 색 참고. 원본 이미지를 텍스처로 사용하지 않음."
    with open(SOURCE, "rb") as handle:
        manifest["source_sha256"] = hashlib.sha256(handle.read()).hexdigest()
    with open(os.path.join(output_root, ASSET_NAME, "manifest.json"), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    ig.log(f"SEO_RECORDER PASS tris={manifest['triangles']} slots=1 collision=1 texture=512")


if __name__ == "__main__":
    main()
