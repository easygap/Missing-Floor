"""계단참의 젖은 작업화 자국(T_StairWetBootprints_M, M_StairWetBootprints)을 만든다.

마스크는 gpt-image로 뽑은 신발 바닥 한 켤레(Content/SourceArt/AI/StairWetBootprints_20261007.png)를
회색조로 줄인 것이다. 재질은 바닥에 투영하는 디퍼드 데칼이고, 마스크 자리만 어둡고
매끈하게 만들어 손전등에 물기가 번들거리게 한다. Wetness를 낮추면 마르며 옅어진다.

    -ExecutePythonScript=Scripts/import_stair_bootprints.py (ArtImport 프로젝트)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import create_textured_materials as materials

LIB = unreal.MaterialEditingLibrary
# 한글 경로를 피해 반입 작업 폴더에 복사해 둔 사본을 읽는다(Import-StairBootprints.ps1).
SOURCE = os.environ["IG_BOOTPRINTS_PNG"]


def connect(source, source_pin, target, target_pin):
    if not LIB.connect_material_expressions(source, source_pin, target, target_pin):
        raise RuntimeError(f"connect failed: {source.get_name()}.{source_pin} -> {target.get_name()}.{target_pin}")


def import_mask():
    task = unreal.AssetImportTask()
    task.filename = SOURCE
    task.destination_path = materials.TEXTURE_ROOT
    task.automated = True
    task.replace_existing = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(f"{materials.TEXTURE_ROOT}/T_StairWetBootprints_M")
    if texture is None:
        raise RuntimeError("T_StairWetBootprints_M 반입 실패")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    return texture


def build(texture):
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = materials._recreate_material(assets, tools, "M_StairWetBootprints")
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    expr = materials._expr
    sample = materials._sample(material, texture, None, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 0)
    wetness = expr(material, unreal.MaterialExpressionScalarParameter, -420, 160)
    wetness.set_editor_property("parameter_name", "Wetness")
    wetness.set_editor_property("default_value", 0.72)
    opacity = expr(material, unreal.MaterialExpressionMultiply, -220, 80)
    connect(sample, "R", opacity, "A")
    connect(wetness, "", opacity, "B")
    LIB.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    # 젖은 철판과 콘크리트는 마른 자리보다 어둡고 매끈하다.
    color = expr(material, unreal.MaterialExpressionConstant3Vector, -220, -80)
    color.set_editor_property("constant", unreal.LinearColor(0.035, 0.034, 0.032, 1.0))
    LIB.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(material, unreal.MaterialExpressionConstant, -220, 260)
    rough.set_editor_property("r", 0.12)
    LIB.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    LIB.layout_material_expressions(material)
    LIB.recompile_material(material)
    for asset in (texture, material):
        if not assets.save_loaded_asset(asset, False):
            raise RuntimeError(f"저장 실패: {asset.get_name()}")
    unreal.log_warning("STAIR_BOOTPRINTS PASS")


if __name__ == "__main__":
    build(import_mask())
