"""승강기 거울 재질(M_ElevatorMirror)을 만든다.

엘리베이터 뒷벽 거울은 씬 캡처가 그린 렌더 타깃을 거울 판 UV에 그대로 붙인다. 캡처는
거울 너머에서 거울 판을 창 삼아 비대칭 프러스텀으로 찍는다(IGElevator.cpp). 캡처의
오른쪽과 판 UV의 U가 둘 다 엘리베이터 안쪽 +X라서 좌우를 따로 뒤집지 않는다. 안에서
보면 U가 왼쪽으로 늘어나니 그 자체가 거울상이다. 다섯 점을 평균해 스테인리스처럼
흐리게 하고, 반사율만큼 어둡게 한다. 언릿이라 캡처가 이미 받은 빛을 다시 받지 않는다.

    -ExecutePythonScript=Scripts/import_elevator_mirror.py (ArtImport 프로젝트)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import create_textured_materials as materials

LIB = unreal.MaterialEditingLibrary


def connect(source, source_pin, target, target_pin):
    if not LIB.connect_material_expressions(source, source_pin, target, target_pin):
        raise RuntimeError(f"connect failed: {source.get_name()}.{source_pin} -> {target.get_name()}.{target_pin}")


def build():
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = materials._recreate_material(assets, tools, "M_ElevatorMirror")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", False)
    expr = materials._expr

    black = unreal.load_asset("/Engine/EngineResources/Black") or unreal.load_asset("/Engine/EngineResources/DefaultTexture")
    feed = expr(material, unreal.MaterialExpressionTextureObjectParameter, -1600, 0)
    feed.set_editor_property("parameter_name", "Feed")
    feed.set_editor_property("texture", black)
    feed.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    panel_uv = expr(material, unreal.MaterialExpressionTextureCoordinate, -1600, 300)

    # 다섯 점 흐림. 렌더 타깃 256 x 584 기준 1.5 픽셀.
    offsets = [(0.0, 0.0), (1.5 / 256.0, 0.0), (-1.5 / 256.0, 0.0), (0.0, 1.5 / 584.0), (0.0, -1.5 / 584.0)]
    samples = []
    for index, (du, dv) in enumerate(offsets):
        uv_in = panel_uv
        if du or dv:
            offset = expr(material, unreal.MaterialExpressionConstant2Vector, -1400, 500 + index * 90)
            offset.set_editor_property("r", du)
            offset.set_editor_property("g", dv)
            add = expr(material, unreal.MaterialExpressionAdd, -1200, 400 + index * 90)
            connect(panel_uv, "", add, "A")
            connect(offset, "", add, "B")
            uv_in = add
        sample = expr(material, unreal.MaterialExpressionTextureSample, -1000, index * 160)
        sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        connect(feed, "", sample, "Tex")
        connect(uv_in, "", sample, "UVs")
        samples.append(sample)
    total = samples[0]
    for index, sample in enumerate(samples[1:], start=1):
        add = expr(material, unreal.MaterialExpressionAdd, -760, index * 120)
        connect(total, "RGB" if total is samples[0] else "", add, "A")
        connect(sample, "RGB", add, "B")
        total = add
    fifth = expr(material, unreal.MaterialExpressionConstant, -760, 700)
    fifth.set_editor_property("r", 0.2)
    average = expr(material, unreal.MaterialExpressionMultiply, -560, 300)
    connect(total, "", average, "A")
    connect(fifth, "", average, "B")

    # 광택 스테인리스의 반사율. 약간 차갑고 어둡다.
    tint = expr(material, unreal.MaterialExpressionVectorParameter, -560, 500)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.56, 0.58, 0.61, 1.0))
    tinted = expr(material, unreal.MaterialExpressionMultiply, -360, 360)
    connect(average, "", tinted, "A")
    connect(tint, "", tinted, "B")
    strength = expr(material, unreal.MaterialExpressionScalarParameter, -360, 560)
    strength.set_editor_property("parameter_name", "Strength")
    strength.set_editor_property("default_value", 1.0)
    out = expr(material, unreal.MaterialExpressionMultiply, -160, 420)
    connect(tinted, "", out, "A")
    connect(strength, "", out, "B")
    LIB.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    LIB.recompile_material(material)
    if not assets.save_loaded_asset(material, False):
        raise RuntimeError("M_ElevatorMirror 저장 실패")
    unreal.log_warning("ELEVATOR_MIRROR PASS")


if __name__ == "__main__":
    build()
