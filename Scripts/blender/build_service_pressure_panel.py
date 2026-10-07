"""생성된 석고 손자국 원본을 앞면에만 쓰는 50cm 점검 패널."""

import hashlib
import json
import math
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
sys.path.insert(0, os.path.dirname(SCRIPT_DIR))

import bpy
import ig_blender_lib as ig
import mesh_lod_contract


ASSET_NAME = "SM_ServicePressurePanel"
SOURCE = os.path.abspath(os.path.join(
    SCRIPT_DIR, "../../Content/SourceArt/AI/PlasterPressure_20261002.png"))


def make_panel():
    """중앙은 평평하게 두고 테두리만 0.9mm 안쪽으로 휘게 만든다."""
    divisions = 8
    side_count = (divisions + 1) ** 2
    vertices = []
    faces = []

    for side in (-1.0, 1.0):
        for row in range(divisions + 1):
            z = -0.25 + row * 0.5 / divisions
            for column in range(divisions + 1):
                x = -0.25 + column * 0.5 / divisions
                edge = max(abs(x), abs(z)) / 0.25
                bow = 0.0009 * edge ** 6 * (0.55 + 0.45 * math.sin(7.0 * x + 5.0 * z) ** 2)
                # 외곽 치수 50×50×1.2cm를 넘기지 않는다. 중앙 원점도 유지한다.
                vertices.append((x, side * (0.006 - bow), z))

    def index(row, column, back=False):
        return (side_count if back else 0) + row * (divisions + 1) + column

    for row in range(divisions):
        for column in range(divisions):
            front = (index(row, column), index(row, column + 1),
                     index(row + 1, column + 1), index(row + 1, column))
            faces.append(front)
            faces.append(tuple(vertex + side_count for vertex in reversed(front)))

    # 두 격자의 가장자리를 이어 닫힌 판으로 만든다.
    perimeter = ([index(0, column) for column in range(divisions + 1)]
                 + [index(row, divisions) for row in range(1, divisions + 1)]
                 + [index(divisions, column) for column in range(divisions - 1, -1, -1)]
                 + [index(row, 0) for row in range(divisions - 1, 0, -1)])
    for current, following in zip(perimeter, perimeter[1:] + perimeter[:1]):
        faces.append((following, current, current + side_count, following + side_count))

    mesh = bpy.data.meshes.new("점검패널")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    panel = bpy.data.objects.new("점검패널", mesh)
    bpy.context.scene.collection.objects.link(panel)

    front = ig.mat_image_uv("석고손자국", SOURCE, roughness=0.91)
    back = ig.mat_plastic("석고마구리", (0.38, 0.36, 0.30), roughness=0.96, bump=0)
    mesh.materials.append(front)
    mesh.materials.append(back)
    image_uv = mesh.uv_layers.new(name="ImageUV")
    for face in mesh.polygons:
        is_front = face.normal.y < -0.5
        face.material_index = 0 if is_front else 1
        for loop_index in face.loop_indices:
            coordinate = mesh.vertices[mesh.loops[loop_index].vertex_index].co
            image_uv.data[loop_index].uv = (
                coordinate.x / 0.5 + 0.5, coordinate.z / 0.5 + 0.5) if is_front else (0.0, 0.0)

    # 원본 손자국은 그대로 두고 실제 판의 바깥 모서리만 둥글린다.
    bevel = ig.add_bevel(panel, width=0.00065, segments=1)
    bevel.material = 1
    return panel


def main():
    ig.reset_scene()
    panel = make_panel()
    output_root = ig.out_root_from_argv()

    def expose_plaster(scene):
        # 흰 석고가 공용 손전등 미리보기의 강한 빛에 날아가지 않게 한다.
        # 원본 이미지·게임 재질은 건드리지 않고 이 미리보기의 조명만 조절한다.
        light = bpy.data.lights.get("__spot")
        if light is not None:
            light.energy = 125.0
            scene.view_settings.exposure = 0.0

    bpy.app.handlers.render_pre.append(expose_plaster)
    try:
        manifest = ig.build_asset(
            ASSET_NAME, "prop", [panel], output_root,
            collision_parts=[], texture_size=512, origin="center",
            mirror_print_for_ue=True, preview_yaw=12.0,
            notes="50×50cm 석고 점검 패널, 최대 두께 1.2cm. 앞 -Y, 원점 중앙. "
                  "생성 원본 PlasterPressure_20261002.png를 앞면에만 배치. "
                  "가장자리 0.9mm 휨과 0.65mm 베벨. 512px D/N/ORM, 재질 1개, 충돌 없음. "
                  "별도 Visibility 조사 영역은 배치 액터가 담당한다.")
    finally:
        bpy.app.handlers.render_pre.remove(expose_plaster)
    mesh = bpy.data.objects[ASSET_NAME]
    if manifest["slots"] != ["Baked"]:
        raise RuntimeError("점검 패널의 최종 재질은 하나여야 한다")
    if manifest["triangles"] > mesh_lod_contract.PROP.lod0_triangles:
        raise RuntimeError("점검 패널이 일반 소품의 삼각형 예산을 넘었다")
    if any(obj.name.startswith("UCX_") for obj in bpy.data.objects):
        raise RuntimeError("점검 패널에는 별도 충돌 메시를 내보내지 않는다")
    low, high = ig.bounds(mesh)
    for actual, expected in zip(high - low, (0.5, 0.012, 0.5)):
        if abs(actual - expected) > 0.0002:
            raise RuntimeError("점검 패널의 외곽 치수가 맞지 않는다")

    # UE 임포터가 mesh_class로 적용하는 공용 LOD 값을 제작 기록에도 남긴다.
    manifest["lod_plan"] = [
        {"level": level, "triangle_ratio": ratio, "screen_size": screen}
        for level, ratio, screen in mesh_lod_contract.lod_plan(ASSET_NAME, mesh_lod_contract.PROP)
    ]
    manifest["texture_size"] = 512
    manifest["collision_hulls"] = 0
    manifest["source_image"] = "Content/SourceArt/AI/PlasterPressure_20261002.png"
    with open(SOURCE, "rb") as handle:
        manifest["source_sha256"] = hashlib.sha256(handle.read()).hexdigest()
    manifest_path = os.path.join(output_root, ASSET_NAME, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    ig.log(f"SERVICE_PRESSURE_PANEL PASS tris={manifest['triangles']} slots=1 collision=0 texture=512")


if __name__ == "__main__":
    main()
