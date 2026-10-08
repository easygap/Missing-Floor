"""403호 욕실의 벽·바닥 타일 텍스처와 재질을 만든다.

원본은 gpt-image로 뽑은 타일 스캔(Content/SourceArt/AI/Bathroom{Wall,Floor}Tile_20261007.png)을
줄눈 가운데부터 정수 장으로 자른 _D와, condition_ai_tiles.py·generate_ai_pbr_maps.py가 만든
N/R/A다. 재질은 복도 벽과 같은 월드 투영이라 어느 벽에 붙여도 타일 크기가 같다.

- M_BathroomWallTile_X: X축으로 뻗은 벽(남북을 보는 면). 흰 유약 타일 25 cm, 1 m에 4장.
- M_BathroomWallTile_Y: Y축으로 뻗은 벽(동서를 보는 면).
- M_BathroomFloorTile_XY: 회색 논슬립 타일 20 cm, 1 m에 5장.

    -ExecutePythonScript=Scripts/import_bathroom_surfaces.py (ArtImport 프로젝트)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import create_textured_materials as materials

# 한글 경로를 피해 반입 작업 폴더에 복사해 둔 사본을 읽는다(Import-BathroomSurfaces.ps1).
STAGING = os.environ["IG_BATHROOM_SURFACES_DIR"]
STEMS = ("T_BathroomWallTile", "T_BathroomFloorTile")
# 유약 타일은 스캔이 밝기 띠(25~65%) 안에 들어오게 뽑혀서 실제 흰 타일보다 어둡다.
# 재질에서 1.3배로 올린다. 줄눈도 같이 밝아지지만 흰 타일 사이의 회색으로 남는다.
WALL_TINT = (1.30, 1.30, 1.28)
SPECS = {
    "M_BathroomWallTile_X": {"tex": "BathroomWallTile", "mapping": "XZ", "tile": 100.0, "tint": WALL_TINT},
    "M_BathroomWallTile_Y": {"tex": "BathroomWallTile", "mapping": "YZ", "tile": 100.0, "tint": WALL_TINT},
    "M_BathroomFloorTile_XY": {"tex": "BathroomFloorTile", "mapping": "XY", "tile": 100.0},
}


def import_textures():
    tasks = []
    for stem in STEMS:
        for suffix in ("D", "N", "R", "A"):
            task = unreal.AssetImportTask()
            task.filename = os.path.join(STAGING, f"{stem}_{suffix}.png")
            task.destination_path = materials.TEXTURE_ROOT
            task.automated = True
            task.replace_existing = True
            task.save = False
            tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    textures = []
    for stem in STEMS:
        for suffix in ("D", "N", "R", "A"):
            texture = unreal.load_asset(f"{materials.TEXTURE_ROOT}/{stem}_{suffix}")
            if texture is None:
                raise RuntimeError(f"{stem}_{suffix} 반입 실패")
            if suffix == "N":
                texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                texture.set_editor_property("srgb", False)
                texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
            elif suffix in ("R", "A"):
                texture.set_editor_property("srgb", False)
                texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
            textures.append(texture)
    return textures


def run():
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    textures = import_textures()
    created = materials.create_textured_materials(assets, tools, specs=SPECS)
    if len(created) != len(SPECS):
        raise RuntimeError(f"재질 {len(created)}개만 만들어졌다")
    for asset in list(textures) + list(created):
        if not assets.save_loaded_asset(asset, False):
            raise RuntimeError(f"저장 실패: {asset.get_name()}")
    unreal.log_warning(f"BATHROOM_SURFACES PASS textures={len(textures)} materials={len(created)}")


if __name__ == "__main__":
    run()
