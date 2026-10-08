"""계단 센서등. SM_StairSensorLight 한 메시(밑판·확산 돔·가운데 감지 렌즈).

기준: Content/SourceArt/AI/StairSensorLightOff_20261007.png(꺼진 모습)과
StairSensorLightLit_20261007.png(켜진 모습). 빌라 계단에 흔한 지름 26 cm 원형 LED
센서등이다. 누렇게 바랜 밑판, 우윳빛 돔, 돔 한가운데 고리에 끼운 다면 감지 렌즈.

발광은 돔에만 굽는다(_E). 밑판과 렌즈는 _E가 검정이라, 씬이 MID의
EmissiveStrength만 올리고 내리면 돔만 켜지고 꺼진다. 켜진 돔은 가운데가 조금 더
밝고 테두리로 갈수록 은은하게 떨어진다.

원점은 천장에 닿는 윗면 중심이고 아래(-Z)로 늘어진다. 씬은 천장 아랫면 Z에 그대로
놓는다.

    blender -b --factory-startup --python Scripts/blender/build_stair_sensor_light.py -- <out_dir>
"""

import math
import os
import sys

import bmesh
import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import ig_blender_lib as ig  # noqa: E402

ASSET = "SM_StairSensorLight"

# 밑판: 바깥 지름 26 cm, 높이 4.6 cm. 아래로 갈수록 살짝 좁아지고 아랫단은 둥글다.
BASE_PROFILE = [
    (0.0, 0.0), (0.127, 0.0), (0.130, -0.004), (0.1285, -0.036), (0.1255, -0.043),
    (0.1205, -0.0465), (0.1165, -0.0465), (0.1165, -0.038), (0.0, -0.038),
]
# 돔이 밑판 안쪽 턱에 앉는 높이와 돔 아랫면의 깊이.
DOME_SEAT_Z = -0.038
DOME_RIM_R = 0.1170
DOME_TOP_Z = -0.0445
DOME_DEPTH = 0.0350
# 감지 렌즈 고리와 렌즈.
COLLAR_R = 0.0235
LENS_R = 0.0150
LENS_H = 0.0095


def dome_profile():
    """밑판 턱에서 돔 아랫면 가운데까지. 곡면은 7.5도 간격이라 날카로운 모서리가 없다."""
    points = [(0.0, DOME_SEAT_Z), (DOME_RIM_R, DOME_SEAT_Z), (DOME_RIM_R, DOME_TOP_Z)]
    radius = DOME_RIM_R - 0.003
    for step in range(1, 13):
        angle = math.radians(7.5 * step)
        r = radius * math.cos(angle)
        z = DOME_TOP_Z - DOME_DEPTH * math.sin(angle)
        points.append((max(r, 0.0), z))
    return points


def lens_bottom_z():
    return DOME_TOP_Z - DOME_DEPTH


def yellowed_abs():
    """바랜 ABS. 윗단 2 cm에만 넓고 흐린 먼지가 앉는다. 잔 점은 만들지 않는다."""
    mat = bpy.data.materials.new("YellowedABS")
    bsdf = ig._principled(mat)
    tree, nodes, links = ig._nodes(mat)
    coords = ig._object_coords(nodes)
    clean = (0.735, 0.690, 0.575, 1.0)
    dusty = (0.560, 0.525, 0.455, 1.0)
    separate = nodes.new("ShaderNodeSeparateXYZ")
    links.new(coords, separate.inputs["Vector"])
    band = ig._map_range(nodes, links, separate.outputs["Z"], -0.020, -0.002, 0.0, 1.0)
    band.clamp = True
    patches = ig._noise(nodes, links, 9.0, detail=0.0, roughness=0.3, coords=coords)
    patch_mask = ig._map_range(nodes, links, patches.outputs["Fac"], 0.35, 0.75, 0.35, 1.0)
    patch_mask.clamp = True
    amount = nodes.new("ShaderNodeMath")
    amount.operation = "MULTIPLY"
    links.new(band.outputs["Result"], amount.inputs[0])
    links.new(patch_mask.outputs["Result"], amount.inputs[1])
    mix = nodes.new("ShaderNodeMixRGB")
    mix.inputs["Color1"].default_value = clean
    mix.inputs["Color2"].default_value = dusty
    links.new(amount.outputs["Value"], mix.inputs["Fac"])
    links.new(mix.outputs[0], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.52
    bsdf.inputs["Metallic"].default_value = 0.0
    mat["ig_kind"] = "baked"
    return mat


def diffuser():
    """우윳빛 확산 돔. 꺼져 있을 때의 색과, 켜질 때 쓸 발광 그림을 같이 갖는다.

    발광 세기는 1로 둔다. 8비트 _E에 가운데 1.0, 테두리 0.72의 기울기가 그대로
    남아야 하고, 실제 밝기는 씬이 EmissiveStrength로 정한다.
    """
    mat = bpy.data.materials.new("Diffuser")
    bsdf = ig._principled(mat)
    tree, nodes, links = ig._nodes(mat)
    coords = ig._object_coords(nodes)
    bsdf.inputs["Base Color"].default_value = (0.845, 0.845, 0.815, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.36
    bsdf.inputs["Metallic"].default_value = 0.0
    separate = nodes.new("ShaderNodeSeparateXYZ")
    links.new(coords, separate.inputs["Vector"])
    flat = nodes.new("ShaderNodeCombineXYZ")
    links.new(separate.outputs["X"], flat.inputs["X"])
    links.new(separate.outputs["Y"], flat.inputs["Y"])
    length = nodes.new("ShaderNodeVectorMath")
    length.operation = "LENGTH"
    links.new(flat.outputs["Vector"], length.inputs[0])
    falloff = ig._map_range(nodes, links, length.outputs["Value"], 0.0, DOME_RIM_R, 1.0, 0.72)
    falloff.clamp = True
    tint = nodes.new("ShaderNodeVectorMath")
    tint.operation = "SCALE"
    tint.inputs[0].default_value = (0.93, 0.96, 1.0)
    links.new(falloff.outputs["Result"], tint.inputs["Scale"])
    links.new(tint.outputs["Vector"], bsdf.inputs["Emission Color"])
    bsdf.inputs["Emission Strength"].default_value = 1.0
    mat["ig_kind"] = "baked"
    mat["ig_emissive"] = True
    return mat


def faceted_lens(material):
    """다면 감지 렌즈. 이십면체를 한 번 나눈 반구라 면마다 각이 서서 렌즈 무늬로 읽힌다."""
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=1, radius=1.0)
    upper = [v for v in bm.verts if v.co.z > 0.02]
    bmesh.ops.delete(bm, geom=upper, context="VERTS")
    for v in bm.verts:
        v.co.x *= LENS_R
        v.co.y *= LENS_R
        v.co.z = v.co.z * LENS_H
    # 잘린 윗면을 막는다. 고리 안에 묻히는 자리라 보이지 않는다.
    boundary = [e for e in bm.edges if e.is_boundary]
    if boundary:
        bmesh.ops.holes_fill(bm, edges=boundary, sides=0)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new("lens")
    bm.to_mesh(mesh)
    bm.free()
    ob = bpy.data.objects.new("lens", mesh)
    bpy.context.scene.collection.objects.link(ob)
    # 렌즈 윗면(적도)이 돔 가장 낮은 점에 닿고, 반구는 고리 아래로 6 mm쯤 나온다.
    ob.location = (0.0, 0.0, lens_bottom_z())
    ig.assign_material(ob, material)
    return ob


def collar(material):
    """렌즈를 잡는 고리. 윗단은 돔 안에 묻히고 아랫단만 돔 밖으로 나온다."""
    bottom = lens_bottom_z()
    return ig.lathe("collar", [
        (0.0, bottom + 0.008), (COLLAR_R, bottom + 0.008), (COLLAR_R, bottom - 0.0015),
        (COLLAR_R - 0.003, bottom - 0.0035), (LENS_R + 0.0008, bottom - 0.0035),
        (LENS_R + 0.0008, bottom - 0.0015), (0.0, bottom - 0.0015),
    ], segments=40, material=material)


def _flipped_copy(parts):
    """미리보기용 사본. 천장 등이라 뒤집어 돔이 위를 보게 하고 위에서 비스듬히 찍는다."""
    copies = []
    for src in parts:
        dup = src.copy()
        dup.data = src.data.copy()
        bpy.context.scene.collection.objects.link(dup)
        copies.append(dup)
    joined = ig.join(copies, "__preview_light")
    joined.rotation_euler = (math.pi, 0.0, 0.0)
    bpy.context.view_layer.update()
    ig.set_active(joined)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    return joined


def render_previews(parts, out_dir, dome_material):
    os.makedirs(out_dir, exist_ok=True)
    preview = _flipped_copy(parts)
    ig.render_preview(preview, os.path.join(out_dir, f"{ASSET}_preview.png"),
                      camera_pitch_deg=38.0)
    ig.render_preview(preview, os.path.join(out_dir, f"{ASSET}_preview_torch.png"),
                      flashlight=True, camera_pitch_deg=38.0)
    bsdf = dome_material.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Emission Strength"].default_value = 6.0
    ig.render_preview(preview, os.path.join(out_dir, f"{ASSET}_preview_lit.png"),
                      camera_pitch_deg=38.0)
    bsdf.inputs["Emission Strength"].default_value = 1.0
    data = preview.data
    bpy.data.objects.remove(preview, do_unlink=True)
    bpy.data.meshes.remove(data)


def build(out_root):
    ig.reset_scene()
    plastic = yellowed_abs()
    dome_material = diffuser()
    lens_material = ig.mat_plastic("PirLens", (0.905, 0.905, 0.885), roughness=0.30, bump=0.0)
    base = ig.lathe("base", BASE_PROFILE, segments=64, material=plastic)
    ig.add_bevel(base, 0.0008, 1)
    ig.apply_modifiers(base)
    dome = ig.lathe("dome", dome_profile(), segments=64, material=dome_material)
    ring = collar(plastic)
    lens = faceted_lens(lens_material)
    parts = [base, dome, ring, lens]
    render_previews(parts, os.path.join(out_root, ASSET), dome_material)
    return ig.build_asset(
        ASSET, "prop", parts, out_root,
        collision_parts=[[base]],
        notes="계단 센서등. 원점은 천장 접촉면 중심, 아래로 9 cm. 발광은 돔에만 구웠고 "
              "씬이 MID EmissiveStrength로 켜고 끈다.",
        texture_size=512, preview=False, origin="top-center",
        # 돔과 밑판은 7.5도 이하로 꺾여 매끈하고, 렌즈 면은 그보다 크게 꺾여 각이 선다.
        sharp_angle=12.0)


def main():
    build(ig.out_root_from_argv())


if __name__ == "__main__":
    main()
