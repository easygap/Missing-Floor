"""Headless validation for generated meshes, PBR textures, and materials."""

from __future__ import annotations

import os
import sys

import unreal

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import mesh_lod_contract
import photo_prop_lod_contract
import texture_atlas_contract
from photo_prop_lod_contract import inspect_photo_prop_lods
from create_textured_materials import (
    DECAL_MATERIALS,
    OPTICAL_PROP_MATERIALS,
    OPTICAL_RESPONSE_MARKER,
    PRINT_RESPONSE_MARKER,
    SURFACE_RESPONSE_DEFAULTS,
    SURFACE_RESPONSE_MARKER,
    surface_response_marker,
    TEXTURED_MATERIALS,
    WET_GROUND_RESPONSE_MARKER,
)


MESH_NAMES = (
    "SM_AlleyCatRun",
    "SM_P3ValveWheelLarge",
    "SM_P3ValveWheelSmall",
    "SM_CrackedPhone",
    "SM_LabelSleeve",
    "SM_StickyNote76mm",
    "SM_CaptureMercyNote",
    "SM_ListenerEntityCrawl",
    "SM_FinalCavityClothingShell",
    "SM_FinalCavityBoneInsert",
    "SM_FinalCavityTarp",
    "SM_FinalCavityBrokenCaster",
    "SM_MokHansooWorkwear",
    "SM_MokHansooHeadHands",
    "SM_MokHansooGypsumBoard",
    "SM_TuningHammer",
    "SM_TunerToolCart",
    "SM_ComplaintLedger",
    "SM_CalendarJournal",
)

# Mesh classes, budgets and the LOD curve are the bake contract; do not keep
# a second copy of them here.
HERO_MESHES = mesh_lod_contract.HERO_MESHES

PRINT_SURFACE_MESHES = {
    "SM_LabelSleeve": 120,
    "SM_StickyNote76mm": 60,
    "SM_CaptureMercyNote": 60,
}

PBR_STEMS = {
    "T_ApartmentWallpaperV2": ("D", "N", "R", "A"),
    "T_KoreanVillaStucco": ("D", "N", "R", "A"),
    "T_MovingBoxCardboard": ("D", "N", "R", "A"),
    "T_PaperClean_V2": ("D", "N", "R", "A"),
    "T_AlleyCatTabby": ("D", "N", "R", "A"),
    "T_P3CabinetPaintedSteel": ("D", "N", "R", "A", "W"),
    "T_CarrierBagFilm": ("D", "N", "R", "A"),
    "T_MissingFloorDryPlaster": ("D", "N", "R", "A"),
    "T_SpriteListenerFront": ("D", "N", "R", "A"),
    "T_SpriteListenerCrawl0": ("D", "N", "R", "A"),
    "T_SpriteListenerCrawl1": ("D", "N", "R", "A"),
    "T_SpriteListenerCrawl2": ("D", "N", "R", "A"),
    "T_SpriteListenerCrawl3": ("D", "N", "R", "A"),
    "T_SpriteFinalCavity": ("D", "N", "R", "A"),
    "T_SpriteMokFinalUpper": ("D", "N", "R", "A"),
}

MATERIAL_TEXTURES = {
    "M_Wallpaper_X": "T_ApartmentWallpaperV2",
    "M_Wallpaper_Y": "T_ApartmentWallpaperV2",
    "M_WallpaperCeil": "T_ApartmentWallpaperV2",
    "M_VillaStucco_X": "T_KoreanVillaStucco",
    "M_VillaStucco_Y": "T_KoreanVillaStucco",
    "M_MovingBoxCardboardUV": "T_MovingBoxCardboard",
    "M_AlleyCatTabbyUV": "T_AlleyCatTabby",
    "M_P3CabinetMetalUV": "T_P3CabinetPaintedSteel",
    "M_CarrierBagFilm": "T_CarrierBagFilm",
    "M_MissingFloorPlaster_X": "T_MissingFloorDryPlaster",
    "M_MissingFloorPlaster_Y": "T_MissingFloorDryPlaster",
    "M_MissingFloorPlaster_XY": "T_MissingFloorDryPlaster",
    "M_MissingFloorListenerPlasterUV": "T_MissingFloorDryPlaster",
    "M_SpriteListenerFront": "T_SpriteListenerFront",
    "M_SpriteListenerCrawl0": "T_SpriteListenerCrawl0",
    "M_SpriteListenerCrawl1": "T_SpriteListenerCrawl1",
    "M_SpriteListenerCrawl2": "T_SpriteListenerCrawl2",
    "M_SpriteListenerCrawl3": "T_SpriteListenerCrawl3",
    "M_SpriteFinalCavity": "T_SpriteFinalCavity",
    "M_SpriteMokFinalUpper": "T_SpriteMokFinalUpper",
}

MASK_MATERIALS = {
    "M_ApartmentWallPatina": "T_ApartmentWallPatina_M",
    "M_DecalDampWallpaper": "T_DecalDampWallpaper_D",
    "M_MissingFloorHandprints": "T_MissingFloorHandprints_M",
    "M_MissingFloorDragTrails": "T_MissingFloorDragTrails_M",
    "M_MissingFloorDustJoint": "T_MissingFloorDustJoint_M",
    "M_MissingFloorCavityScratches": "T_MissingFloorCavityScratches_M",
    "M_SpriteSeo": "T_SpriteSeo_D",
    "M_SpriteMok": "T_SpriteMok_D",
    "M_SpriteListenerFront": "T_SpriteListenerFront_D",
    "M_SpriteListenerCrawl0": "T_SpriteListenerCrawl0_D",
    "M_SpriteListenerCrawl1": "T_SpriteListenerCrawl1_D",
    "M_SpriteListenerCrawl2": "T_SpriteListenerCrawl2_D",
    "M_SpriteListenerCrawl3": "T_SpriteListenerCrawl3_D",
    "M_SpriteFinalCavity": "T_SpriteFinalCavity_D",
    "M_SpriteMokFinalUpper": "T_SpriteMokFinalUpper_D",
}

PRINT_MATERIALS = {
    "M_NoteFridge": "T_NoteFridge_D",
    "M_CaptureMercyNote": "T_CaptureMercyNote_D",
    "M_MercyNoteUnderDoor": "T_MercyNoteUnderDoor_D",
    "M_SignAux5MonitorOnly": "T_SignAux5MonitorOnly_D",
    "M_LabelWater": "T_LabelWater_D",
    "M_LabelGreenTea": "T_LabelGreenTea_D",
    "M_LabelBarley": "T_LabelBarley_D",
    "M_LabelSoda": "T_LabelSoda_D",
}

ENTRANCE_PLATE_MATERIALS = {
    "M_Plate401": "T_Plate401_D",
    "M_Plate402": "T_Plate402_D",
    "M_Plate403": "T_Plate403_D",
}

INSTANCED_PRODUCT_MATERIALS = {
    "M_BottleBrown",
    "M_BottleGreen",
    "M_FridgeInterior",
    "M_LabelBarley",
    "M_LabelGreenTea",
    "M_LabelSoda",
    "M_LabelWater",
    "M_SnackBlue",
    "M_SnackRed",
    "M_SnackYellow",
    "M_StainlessUV",
}

WRAPPED_LABEL_MATERIALS = {
    "M_LabelBarley",
    "M_LabelGreenTea",
    "M_LabelSoda",
    "M_LabelWater",
}

PRINT_RESPONSE_MATERIALS = {
    name: DECAL_MATERIALS[name]
    for name in (
        "M_PaperClean",
        "M_PaperOld",
        "M_LabelWater",
        "M_LabelGreenTea",
        "M_LabelBarley",
        "M_LabelSoda",
    )
}

EVIDENCE_MASK_MATERIALS = {
    "M_MissingFloorHandprints": "T_MissingFloorHandprints_M",
    "M_MissingFloorDragTrails": "T_MissingFloorDragTrails_M",
    "M_MissingFloorDustJoint": "T_MissingFloorDustJoint_M",
    "M_MissingFloorCavityScratches": "T_MissingFloorCavityScratches_M",
}

TWO_SIDED_PRINT_MATERIALS = {
    "M_CaptureMercyNote",
    "M_MercyNoteUnderDoor",
}
ALL_PRINT_MATERIALS = {**PRINT_MATERIALS, **ENTRANCE_PLATE_MATERIALS}

# §14 CCTV 채널 5. The C++ binds the render target and drives the collapse by
# these exact names, and a silently renamed parameter shows up as a dead black
# screen in the one beat that cannot be replayed — so the names are a contract.
CCTV_MONITOR_MATERIAL = "M_CctvChannelFive"
CCTV_SCALAR_PARAMETERS = {"Static", "Gain"}
CCTV_TEXTURE_PARAMETERS = {"Feed"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def load(path: str, expected_type):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    require(asset is not None, f"Missing asset: {path}")
    require(isinstance(asset, expected_type), f"Wrong asset class: {path}")
    return asset


def texture_path(texture) -> str:
    return texture.get_path_name().split(".", 1)[0]


def expression_texture_paths(material) -> set[str]:
    """Read persisted graph nodes; this also works when NullRHI has no resource."""
    paths = set()
    expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    for expression in expressions:
        try:
            texture = expression.get_editor_property("texture")
        except Exception:  # noqa: BLE001 - most expressions are not texture nodes
            continue
        if texture is not None:
            paths.add(texture_path(texture))
    return paths


def texture_sample_types(material) -> dict[str, str]:
    """Return persisted sampler types keyed by the linked texture path."""
    sample_types = {}
    expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    for expression in expressions:
        if not isinstance(expression, unreal.MaterialExpressionTextureSample):
            continue
        texture = expression.get_editor_property("texture")
        if texture is not None:
            sample_types[texture_path(texture)] = str(
                expression.get_editor_property("sampler_type")
            )
    return sample_types


def texture_sample_counts(material) -> dict[str, int]:
    """Count repeated samples as well as unique linked texture packages."""
    counts: dict[str, int] = {}
    expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    for expression in expressions:
        if not isinstance(expression, unreal.MaterialExpressionTextureSample):
            continue
        texture = expression.get_editor_property("texture")
        if texture is None:
            continue
        path = texture_path(texture)
        counts[path] = counts.get(path, 0) + 1
    return counts


def resolved_surface_texture(name: str) -> str | None:
    """Mirror create_textured_materials.py's CC0-photo-first lookup."""
    if name.startswith("T_") and not name.startswith("T_Photo_"):
        photo = f"/Game/Prototype/Textures/T_Photo_{name[2:]}"
        if unreal.EditorAssetLibrary.does_asset_exist(photo):
            return photo
    normal = f"/Game/Prototype/Textures/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(normal):
        return normal
    return None


def surface_value(spec, base_name, key, default=None):
    if key in spec:
        return spec[key]
    return SURFACE_RESPONSE_DEFAULTS.get(base_name, {}).get(key, default)


def has_scalar_parameter(material, parameter_name: str) -> bool:
    for expression in unreal.MaterialEditingLibrary.get_material_expressions(material):
        if not isinstance(expression, unreal.MaterialExpressionScalarParameter):
            continue
        if str(expression.get_editor_property("parameter_name")) == parameter_name:
            return True
    return False


def _lod0_triangle_count(mesh) -> int | None:
    """소스 모델 LOD0의 삼각형 수. 5.8에서 서브시스템의
    get_number_triangles가 사라져 지오메트리 스크립트로 돌아 읽는다."""
    try:
        dynamic = unreal.new_object(unreal.DynamicMesh)
    except Exception:  # noqa: BLE001 - binding differences across versions
        dynamic = unreal.DynamicMesh()
    read_lod = unreal.GeometryScriptMeshReadLOD()
    try:
        read_lod.set_editor_property(
            "lod_type", unreal.GeometryScriptLODType.SOURCE_MODEL
        )
    except Exception:  # noqa: BLE001 - MaxAvailable 기본값도 소스 모델에 닿는다
        pass
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(
        mesh,
        dynamic,
        unreal.GeometryScriptCopyMeshFromAssetOptions(),
        read_lod,
    )
    counter = getattr(dynamic, "get_triangle_count", None)
    if counter is not None:
        return int(counter())
    queries = unreal.GeometryScript_MeshQueries
    counter = getattr(queries, "get_num_triangle_i_ds", None)
    if counter is not None:
        return int(counter(dynamic))
    return None


def validate_meshes() -> tuple[int, int]:
    """Every generated mesh carries its authored LOD chain and stays in budget.

    A single full-density LOD is the failure this checks for: it is invisible
    in the editor, costs nothing to author, and is paid for on every frame the
    prop is on screen at any distance.
    """
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    require(subsystem is not None, "StaticMeshEditorSubsystem is unavailable")
    total_lods = 0
    reduced_meshes = 0
    for name in MESH_NAMES:
        mesh = load(f"/Game/Meshes/{name}", unreal.StaticMesh)
        mesh_class = mesh_lod_contract.classify(name)
        lod_count = subsystem.get_lod_count(mesh)
        require(lod_count >= 1, f"Mesh has no LOD0: {name}")
        require(
            lod_count >= 2,
            f"Mesh ships a single LOD, including at distance: {name}",
        )
        require(
            lod_count >= mesh_class.lod_count,
            f"Mesh has {lod_count} LODs, contract wants "
            f"{mesh_class.lod_count} for class {mesh_class.name}: {name}",
        )
        total_lods += lod_count
        reduced_meshes += 1

        triangles = _lod0_triangle_count(mesh)
        require(
            triangles is not None,
            f"LOD0 triangle count could not be read: {name}",
        )
        require(
            triangles <= mesh_class.lod0_triangles,
            f"LOD0 is {triangles} triangles, over the {mesh_class.name} budget "
            f"of {mesh_class.lod0_triangles}: {name}",
        )
        # 라이트맵 UV는 렌더 빌드가 만들어서 소스 모델 채널 수로는 보이지
        # 않는다. 굽기가 약속하는 것은 설정이므로 그 설정을 검사한다.
        build_settings = subsystem.get_lod_build_settings(mesh, 0)
        require(
            bool(build_settings.get_editor_property("generate_lightmap_u_vs")),
            f"Lightmap UV generation is disabled: {name}",
        )
        require(
            int(mesh.get_editor_property("light_map_coordinate_index")) == 1,
            f"Lightmap coordinate index is not the generated channel: {name}",
        )

        if name in PRINT_SURFACE_MESHES:
            require(
                subsystem.get_num_uv_channels(mesh, 0) >= 1,
                f"Printed surface has no UV0: {name}",
            )
            require(
                subsystem.get_number_verts(mesh, 0) >= PRINT_SURFACE_MESHES[name],
                f"Printed surface collapsed below its authored seam/grid density: {name}",
            )
    return total_lods, reduced_meshes


def validate_print_atlas() -> int:
    """The packed pages are present, imported and clamped.

    An atlas that is half-applied is worse than none: the materials read UV
    rects out of a page that either is not there or wraps, and every notice in
    the store samples its neighbour's artwork.
    """
    manifest = texture_atlas_contract.try_load_manifest()
    if manifest is None:
        unreal.log_warning(
            "[IndieGame] No print atlas manifest; materials keep their "
            "individual textures. Run Scripts/build_texture_atlas.py."
        )
        return 0

    imported = [
        texture_atlas_contract.page_package_path(page["index"])
        for page in manifest["pages"]
    ]
    if not all(unreal.EditorAssetLibrary.does_asset_exist(p) for p in imported):
        # A manifest without imported pages is the ordinary state of a
        # checkout that has not run the atlas stage of Build-ArtAssets.ps1.
        # The material builder falls back to the individual textures, so this
        # is a build that has not been optimised, not a broken one.
        unreal.log_warning(
            "[IndieGame] Print atlas manifest present but the pages are not "
            "imported; materials keep their individual textures. Run "
            "Scripts/import_texture_atlas.py."
        )
        return 0

    for index, path in enumerate(imported):
        texture = load(path, unreal.Texture2D)
        require(
            texture.get_editor_property("address_x") == unreal.TextureAddress.TA_CLAMP,
            f"Atlas page wraps in U; entries would bleed across: {path}",
        )
        require(
            texture.get_editor_property("address_y") == unreal.TextureAddress.TA_CLAMP,
            f"Atlas page wraps in V; entries would bleed across: {path}",
        )
        require(
            texture.get_editor_property("never_stream"),
            f"아틀라스 페이지가 스트리밍된다. 인쇄물 글자가 낮은 밉으로 뭉개진다: {path}",
        )
        # Pages are only as big as their own contents need, so each one is
        # checked against its own recorded shape. An imported page that is not
        # the shape the manifest packed means the UV rects address the wrong
        # pixels.
        width, height = texture_atlas_contract.page_dimensions(manifest, index)
        require(
            texture.blueprint_get_size_x() == width
            and texture.blueprint_get_size_y() == height,
            f"Atlas page is {texture.blueprint_get_size_x()}x"
            f"{texture.blueprint_get_size_y()}, manifest says {width}x{height}: "
            f"{path}",
        )
    return len(manifest["entries"])


def validate_photo_prop_lods() -> int:
    """Scanned props obey the same chain and the same budget as the built ones."""
    inspected = inspect_photo_prop_lods()
    over_budget = []
    for item in inspected:
        mesh_class = photo_prop_lod_contract.prop_class(str(item["asset_id"]))
        require(
            int(item["lod_count"]) >= 2,
            f"Photo prop has no reduced LOD: {item['path']}",
        )
        require(
            int(item["lod_count"]) >= mesh_class.lod_count,
            f"Photo prop has {item['lod_count']} LODs, contract wants "
            f"{mesh_class.lod_count}: {item['path']}",
        )
        triangles = int(item["triangle_count"])
        if triangles > 0 and triangles > mesh_class.lod0_triangles:
            # 하나씩 끊지 말고 전부 모아서 한 번에 알린다. 스캔의 감축 바닥은
            # 프롭마다 달라서, 전체 목록이 있어야 분류를 한 번에 정할 수 있다.
            over_budget.append(
                f"{item['path']}: {triangles} > {mesh_class.name} "
                f"{mesh_class.lod0_triangles}"
            )
    require(
        not over_budget,
        "Scanned LOD0 over budget:\n  " + "\n  ".join(over_budget),
    )
    return len(inspected)


def validate_pbr_textures(pbr_stems) -> int:
    checked = 0
    for stem, suffixes in pbr_stems.items():
        for suffix in suffixes:
            name = f"{stem}_{suffix}"
            texture = load(f"/Game/Prototype/Textures/{name}", unreal.Texture2D)
            srgb = bool(texture.get_editor_property("srgb"))
            if suffix == "D":
                require(srgb, f"Base colour must be sRGB: {name}")
            else:
                require(not srgb, f"Data texture must be linear: {name}")
            compression = str(texture.get_editor_property("compression_settings"))
            if suffix == "N":
                require("NORMALMAP" in compression.upper(), f"Normal map compression missing: {name}")
            elif suffix == "M":
                require("MASK" in compression.upper(), f"Mask compression missing: {name}")
            elif suffix in {"R", "A", "W"}:
                require("GRAYSCALE" in compression.upper(), f"Grayscale compression missing: {name}")
            size_x = texture.blueprint_get_size_x()
            size_y = texture.blueprint_get_size_y()
            minimum_edge = 512 if stem == "T_PaperClean_V2" else 1024
            require(
                size_x >= minimum_edge and size_y >= minimum_edge,
                f"Texture below authored minimum {minimum_edge}: "
                f"{name} ({size_x}x{size_y})",
            )
            checked += 1
    return checked


def validate_textures() -> int:
    checked = validate_pbr_textures(PBR_STEMS)
    for texture_name in EVIDENCE_MASK_MATERIALS.values():
        texture = load(
            f"/Game/Prototype/Textures/{texture_name}", unreal.Texture2D
        )
        require(not texture.get_editor_property("srgb"), f"Mask must be linear: {texture_name}")
        compression = str(texture.get_editor_property("compression_settings"))
        require("MASK" in compression.upper(), f"Mask compression missing: {texture_name}")
        checked += 1
    for texture_name in sorted(set(PRINT_MATERIALS.values())):
        texture = load(
            f"/Game/Prototype/Textures/{texture_name}", unreal.Texture2D
        )
        require(texture.get_editor_property("srgb"), f"Print texture must be sRGB: {texture_name}")
        require(
            texture.get_editor_property("address_x") == unreal.TextureAddress.TA_CLAMP
            and texture.get_editor_property("address_y") == unreal.TextureAddress.TA_CLAMP,
            f"Print texture edges must clamp at the authored seam: {texture_name}",
        )
        require(
            texture.get_editor_property("filter") == unreal.TextureFilter.TF_DEFAULT,
            f"Print texture must inherit the World group's anisotropic sampler: {texture_name}",
        )
        require(
            texture.blueprint_get_size_x() >= 512
            and texture.blueprint_get_size_y() >= 240,
            f"Print texture is below inspection resolution: {texture_name}",
        )
        checked += 1
    for texture_name in sorted(set(ENTRANCE_PLATE_MATERIALS.values())):
        texture = load(
            f"/Game/Prototype/Textures/{texture_name}", unreal.Texture2D
        )
        require(
            texture.get_editor_property("srgb"),
            f"Plate texture must be sRGB: {texture_name}",
        )
        require(
            texture.get_editor_property("address_x") == unreal.TextureAddress.TA_CLAMP
            and texture.get_editor_property("address_y") == unreal.TextureAddress.TA_CLAMP,
            f"Plate texture edges must clamp at the authored border: {texture_name}",
        )
        require(
            texture.get_editor_property("filter") == unreal.TextureFilter.TF_DEFAULT,
            f"Plate texture must inherit the World sampler: {texture_name}",
        )
        require(
            texture.blueprint_get_size_x() >= 128
            and texture.blueprint_get_size_y() >= 64,
            f"Plate texture is below authored resolution: {texture_name}",
        )
        checked += 1
    return checked


def material_input(material, material_property) -> None:
    node = unreal.MaterialEditingLibrary.get_material_property_input_node(
        material, material_property
    )
    require(node is not None, f"Missing material input {material_property}: {material.get_name()}")


def validate_surface_response_materials(material_specs=None) -> tuple[int, int]:
    """Prove the live architecture has bounded multi-scale PBR response."""
    if material_specs is None:
        material_specs = TEXTURED_MATERIALS
    checked = 0
    linked_samples = 0
    for name, spec in material_specs.items():
        material = load(f"/Game/Prototype/Materials/{name}", unreal.Material)
        errors = unreal.MaterialEditingLibrary.recompile_material(material)
        require(not errors, f"Surface material compile failed: {name}: {errors}")
        if spec.get("retail_finish"):
            # 도장 벽과 연마 타일에는 암석용 다중 노멀 검사를 강제하지 않는다.
            # 대신 실제 생성 원본 연결, 평면 노멀, 스트리밍과 샘플 예산을 확인한다.
            require(has_scalar_parameter(material, "IG_RetailFinish_20260914"),
                    f"매장 마감 재질이 반입되지 않았다: {name}")
            for prop in (unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_NORMAL,
                         unreal.MaterialProperty.MP_ROUGHNESS, unreal.MaterialProperty.MP_SPECULAR):
                material_input(material, prop)
            require(material.get_editor_property("tangent_space_normal"), f"평면 노멀 좌표계 오류: {name}")
            if spec["retail_finish"] in ("floor", "granite"):
                stem = "PocheonGranite" if spec["retail_finish"] == "granite" else "PorcelainStore"
                path = f"/Game/Prototype/Textures/T_{stem}_20260915_D"
                counts = texture_sample_counts(material)
                require(counts.get(path, 0) == 1, f"생성 원본 연결 오류: {name}: {counts}")
                texture = load(path, unreal.Texture2D)
                require(not texture.get_editor_property("never_stream"), f"바닥 스트리밍이 꺼져 있다: {name}")
                require(texture.get_editor_property("max_texture_size") == 1024, f"바닥 텍스처 예산 초과: {name}")
                require(texture.get_editor_property("mip_gen_settings") != unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
                        f"바닥 밉맵이 꺼져 있다: {name}")
            samples = int(unreal.MaterialEditingLibrary.get_statistics(material).get_editor_property("num_pixel_texture_samples"))
            require(samples <= 1, f"단순 마감의 샘플 예산 초과: {name}: {samples}")
            checked += 1
            linked_samples += samples
            continue
        require(
            has_scalar_parameter(material, surface_response_marker(spec)),
            f"Surface response version marker is missing: {name}",
        )
        for material_property in (
            unreal.MaterialProperty.MP_BASE_COLOR,
            unreal.MaterialProperty.MP_NORMAL,
            unreal.MaterialProperty.MP_ROUGHNESS,
            unreal.MaterialProperty.MP_SPECULAR,
        ):
            material_input(material, material_property)

        base_name = spec["tex"]
        counts = texture_sample_counts(material)
        diffuse_path = resolved_surface_texture(f"T_{base_name}_D")
        normal_path = resolved_surface_texture(f"T_{base_name}_N")
        require(diffuse_path is not None, f"Surface diffuse is missing: {name}")
        require(normal_path is not None, f"Surface normal is missing: {name}")
        expected_diffuse_samples = (
            2
            if spec["mapping"] != "UV"
            and float(surface_value(spec, base_name, "macro_strength", 0.0)) > 0.0
            else 1
        )
        expected_normal_samples = (
            2
            if float(
                surface_value(spec, base_name, "detail_normal_strength", 0.0)
            ) > 0.0
            else 1
        )
        require(
            counts.get(diffuse_path, 0) >= expected_diffuse_samples,
            f"Macro colour blend is missing: {name}",
        )
        require(
            counts.get(normal_path, 0) >= expected_normal_samples,
            f"Detail-normal blend is missing: {name}",
        )

        rough_path = resolved_surface_texture(f"T_{base_name}_R")
        rough_detail_strength = float(
            surface_value(spec, base_name, "roughness_detail_strength", 0.0)
        )
        rough_variation = float(
            surface_value(spec, base_name, "roughness_variation", 0.0)
        )
        if rough_path is not None and (
            spec.get("force_rough") is None or rough_variation > 0.0
        ):
            expected_rough_samples = 2 if rough_detail_strength > 0.0 else 1
            require(
                counts.get(rough_path, 0) >= expected_rough_samples,
                f"Roughness variation is missing: {name}",
            )

        ao_path = resolved_surface_texture(f"T_{base_name}_A")
        if ao_path is not None:
            require(
                counts.get(ao_path, 0) >= 1,
                f"Authored cavity map is not linked: {name}",
            )
            material_input(material, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

        # Existing rooted packages can retain disconnected legacy nodes after
        # an in-place migration.  The shader compiler prunes them, so enforce
        # the real pixel-shader cost rather than counting every editor node.
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        pixel_samples = int(
            statistics.get_editor_property("num_pixel_texture_samples")
        )
        require(
            pixel_samples <= 7,
            f"Compiled surface sample budget exceeded: {name}: {pixel_samples}",
        )
        linked_samples += pixel_samples
        checked += 1
    return checked, linked_samples


def validate_prop_response_materials() -> tuple[int, int]:
    """Prove the live paper, film, glass, metal and wet-ground response."""
    checked = 0
    linked_samples = 0
    for name, spec in PRINT_RESPONSE_MATERIALS.items():
        material = load(f"/Game/Prototype/Materials/{name}", unreal.Material)
        errors = unreal.MaterialEditingLibrary.recompile_material(material)
        require(not errors, f"Print response compile failed: {name}: {errors}")
        require(
            has_scalar_parameter(material, PRINT_RESPONSE_MARKER),
            f"Print response version marker is missing: {name}",
        )
        for material_property in (
            unreal.MaterialProperty.MP_BASE_COLOR,
            unreal.MaterialProperty.MP_NORMAL,
            unreal.MaterialProperty.MP_ROUGHNESS,
            unreal.MaterialProperty.MP_SPECULAR,
        ):
            material_input(material, material_property)
        used = expression_texture_paths(material)
        micro_stem = spec["micro_stem"]
        for suffix in ("N", "R"):
            expected = f"/Game/Prototype/Textures/{micro_stem}_{suffix}"
            require(
                expected in used,
                f"Print micro-{suffix} is not linked: {name}: {expected}",
            )
        if spec.get("ao"):
            expected = f"/Game/Prototype/Textures/{micro_stem}_A"
            require(expected in used, f"Print micro-AO is not linked: {name}")
            material_input(material, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        pixel_samples = int(
            statistics.get_editor_property("num_pixel_texture_samples")
        )
        require(
            pixel_samples <= 4,
            f"Compiled print sample budget exceeded: {name}: {pixel_samples}",
        )
        linked_samples += pixel_samples
        checked += 1

    for name, spec in OPTICAL_PROP_MATERIALS.items():
        material = load(f"/Game/Prototype/Materials/{name}", unreal.Material)
        errors = unreal.MaterialEditingLibrary.recompile_material(material)
        require(not errors, f"Optical prop compile failed: {name}: {errors}")
        require(
            has_scalar_parameter(material, OPTICAL_RESPONSE_MARKER),
            f"Optical response version marker is missing: {name}",
        )
        for material_property in (
            unreal.MaterialProperty.MP_BASE_COLOR,
            unreal.MaterialProperty.MP_ROUGHNESS,
            unreal.MaterialProperty.MP_SPECULAR,
        ):
            material_input(material, material_property)
        if spec.get("micro_stem"):
            material_input(material, unreal.MaterialProperty.MP_NORMAL)
        if "metallic" in spec:
            material_input(material, unreal.MaterialProperty.MP_METALLIC)
        if "opacity_center" in spec:
            require(
                material.get_editor_property("blend_mode")
                == unreal.BlendMode.BLEND_TRANSLUCENT,
                f"Grazing glass lost translucent blending: {name}",
            )
            material_input(material, unreal.MaterialProperty.MP_OPACITY)
            material_input(material, unreal.MaterialProperty.MP_REFRACTION)
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        pixel_samples = int(
            statistics.get_editor_property("num_pixel_texture_samples")
        )
        require(
            pixel_samples <= 2,
            f"Compiled optical-prop sample budget exceeded: {name}: {pixel_samples}",
        )
        linked_samples += pixel_samples
        checked += 1

    asphalt = load(
        "/Game/Prototype/Materials/M_AsphaltWorld", unreal.Material
    )
    asphalt_errors = unreal.MaterialEditingLibrary.recompile_material(asphalt)
    require(
        not asphalt_errors,
        f"Wet-ground material compile failed: {asphalt_errors}",
    )
    require(
        has_scalar_parameter(asphalt, WET_GROUND_RESPONSE_MARKER),
        "Wet-ground response version marker is missing: M_AsphaltWorld",
    )
    for material_property in (
        unreal.MaterialProperty.MP_BASE_COLOR,
        unreal.MaterialProperty.MP_NORMAL,
        unreal.MaterialProperty.MP_ROUGHNESS,
        unreal.MaterialProperty.MP_SPECULAR,
    ):
        material_input(asphalt, material_property)
    asphalt_statistics = unreal.MaterialEditingLibrary.get_statistics(asphalt)
    asphalt_samples = int(
        asphalt_statistics.get_editor_property("num_pixel_texture_samples")
    )
    require(
        asphalt_samples <= 4,
        f"Compiled wet-ground sample budget exceeded: {asphalt_samples}",
    )
    linked_samples += asphalt_samples
    checked += 1
    return checked, linked_samples


def validate_materials() -> tuple[int, int]:
    checked, linked_textures = validate_surface_response_materials()
    prop_checked, prop_samples = validate_prop_response_materials()
    checked += prop_checked
    linked_textures += prop_samples
    for name, stem in MATERIAL_TEXTURES.items():
        material = load(f"/Game/Prototype/Materials/{name}", unreal.Material)
        errors = unreal.MaterialEditingLibrary.recompile_material(material)
        require(not errors, f"Material compile failed: {name}: {errors}")
        used = expression_texture_paths(material)
        expected = {
            f"/Game/Prototype/Textures/{stem}_{suffix}"
            for suffix in PBR_STEMS[stem]
        }
        missing = sorted(expected - used)
        require(not missing, f"PBR maps are not linked to {name}: {missing}")
        linked_textures += len(expected)
        for material_property in (
            unreal.MaterialProperty.MP_BASE_COLOR,
            unreal.MaterialProperty.MP_NORMAL,
            unreal.MaterialProperty.MP_ROUGHNESS,
            unreal.MaterialProperty.MP_AMBIENT_OCCLUSION,
        ):
            material_input(material, material_property)
        if name == "M_CarrierBagFilm":
            require(
                material.get_editor_property("blend_mode")
                == unreal.BlendMode.BLEND_TRANSLUCENT,
                f"Translucent blend mode missing: {name}",
            )
            material_input(material, unreal.MaterialProperty.MP_OPACITY)
        checked += 1

    for name, texture_name in MASK_MATERIALS.items():
        material = load(f"/Game/Prototype/Materials/{name}", unreal.Material)
        errors = unreal.MaterialEditingLibrary.recompile_material(material)
        require(not errors, f"Material compile failed: {name}: {errors}")
        used = expression_texture_paths(material)
        expected = f"/Game/Prototype/Textures/{texture_name}"
        require(expected in used, f"Mask texture is not linked to {name}: {expected}")
        if name in EVIDENCE_MASK_MATERIALS:
            sampler_types = texture_sample_types(material)
            sampler_type = sampler_types.get(expected, "")
            require(
                "MASK" in sampler_type.upper(),
                f"Mask sampler type does not match TC_MASKS: {name}: {sampler_type}",
            )
        if name == "M_ApartmentWallPatina":
            validate_apartment_patina(material)
        else:
            require(
                material.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_MASKED,
                f"Masked blend mode missing: {name}",
            )
            material_input(material, unreal.MaterialProperty.MP_OPACITY_MASK)
        linked_textures += 1
        checked += 1
    for name, texture_name in ALL_PRINT_MATERIALS.items():
        material = load(f"/Game/Prototype/Materials/{name}", unreal.Material)
        errors = unreal.MaterialEditingLibrary.recompile_material(material)
        require(not errors, f"Printed material compile failed: {name}: {errors}")
        expected = f"/Game/Prototype/Textures/{texture_name}"
        paths = expression_texture_paths(material)
        manifest = texture_atlas_contract.try_load_manifest()
        if expected not in paths and manifest and texture_name in manifest["entries"]:
            entry = manifest["entries"][texture_name]
            expected = texture_atlas_contract.page_package_path(entry["page"])
            transform = texture_atlas_contract.atlas_uv_transform(manifest, texture_name)
            pairs = [(float(e.get_editor_property("r")), float(e.get_editor_property("g")))
                     for e in unreal.MaterialEditingLibrary.get_material_expressions(material)
                     if isinstance(e, unreal.MaterialExpressionConstant2Vector)]
            for target in (transform[:2], transform[2:]):
                require(any(abs(pair[0] - target[0]) < 1e-6 and abs(pair[1] - target[1]) < 1e-6
                            for pair in pairs), f"아틀라스의 다른 그림을 가리킨다: {name}")
        require(
            expected in paths,
            f"Printed texture is not linked to {name}: {expected}",
        )
        material_input(material, unreal.MaterialProperty.MP_BASE_COLOR)
        material_input(material, unreal.MaterialProperty.MP_ROUGHNESS)
        if name in TWO_SIDED_PRINT_MATERIALS:
            require(
                material.get_editor_property("two_sided"),
                f"Printed paper lost two-sided rendering: {name}",
            )
        linked_textures += 1
        checked += 1
    cctv = load(
        f"/Game/Prototype/Materials/{CCTV_MONITOR_MATERIAL}", unreal.Material
    )
    cctv_errors = unreal.MaterialEditingLibrary.recompile_material(cctv)
    require(
        not cctv_errors,
        f"CCTV monitor material compile failed: {cctv_errors}",
    )
    cctv_scalars = {
        str(parameter)
        for parameter in unreal.MaterialEditingLibrary.get_scalar_parameter_names(cctv)
    }
    require(
        CCTV_SCALAR_PARAMETERS.issubset(cctv_scalars),
        "CCTV monitor material lost a scalar parameter the C++ drives: "
        f"expected {sorted(CCTV_SCALAR_PARAMETERS)}, found {sorted(cctv_scalars)}",
    )
    cctv_textures = {
        str(parameter)
        for parameter in unreal.MaterialEditingLibrary.get_texture_parameter_names(cctv)
    }
    require(
        CCTV_TEXTURE_PARAMETERS.issubset(cctv_textures),
        "CCTV monitor material lost the render-target parameter: "
        f"expected {sorted(CCTV_TEXTURE_PARAMETERS)}, found {sorted(cctv_textures)}",
    )
    # The tube is the light source in that corner of the booth, so it must stay
    # unlit and emissive. A lit screen would need the booth lamp to read at all.
    require(
        cctv.get_editor_property("shading_model")
        == unreal.MaterialShadingModel.MSM_UNLIT,
        "CCTV monitor material is no longer unlit",
    )
    material_input(cctv, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    checked += 1
    for name in sorted(INSTANCED_PRODUCT_MATERIALS):
        material = load(f"/Game/Prototype/Materials/{name}", unreal.Material)
        require(
            material.get_editor_property("used_with_instanced_static_meshes"),
            f"Instanced product shader usage is not persisted: {name}",
        )
        if name in WRAPPED_LABEL_MATERIALS:
            require(
                material.get_editor_property("two_sided"),
                f"Wrapped product film lost two-sided rendering: {name}",
            )
        checked += 1
    return checked, linked_textures


def validate_apartment_visual_assets() -> None:
    """Audit only assets owned by the ApartmentVisual targeted build."""
    pbr_stems = {
        stem: PBR_STEMS[stem]
        for stem in (
            "T_ApartmentWallpaperV2",
        )
    }
    material_names = (
        "M_Wallpaper_X",
        "M_Wallpaper_Y",
        "M_WallpaperCeil",
    )
    material_specs = {
        name: TEXTURED_MATERIALS[name]
        for name in material_names
    }

    texture_count = validate_pbr_textures(pbr_stems)
    patina_texture_name = MASK_MATERIALS["M_ApartmentWallPatina"]
    patina_texture = load(
        f"/Game/Prototype/Textures/{patina_texture_name}", unreal.Texture2D
    )
    require(
        not patina_texture.get_editor_property("srgb"),
        f"Mask must be linear: {patina_texture_name}",
    )
    patina_compression = str(
        patina_texture.get_editor_property("compression_settings")
    )
    require(
        "MASK" in patina_compression.upper(),
        f"Mask compression missing: {patina_texture_name}",
    )
    require(
        patina_texture.blueprint_get_size_x() >= 1024
        and patina_texture.blueprint_get_size_y() >= 1024,
        f"Texture below authored minimum 1024: {patina_texture_name}",
    )
    texture_count += 1

    material_count, _ = validate_surface_response_materials(material_specs)
    linked_textures = 0
    for name in material_names:
        stem = MATERIAL_TEXTURES[name]
        material = load(f"/Game/Prototype/Materials/{name}", unreal.Material)
        expected = {
            f"/Game/Prototype/Textures/{stem}_{suffix}"
            for suffix in pbr_stems[stem]
        }
        missing = sorted(expected - expression_texture_paths(material))
        require(not missing, f"PBR maps are not linked to {name}: {missing}")
        linked_textures += len(expected)

    patina_material = load(
        "/Game/Prototype/Materials/M_ApartmentWallPatina", unreal.Material
    )
    patina_errors = unreal.MaterialEditingLibrary.recompile_material(
        patina_material
    )
    require(
        not patina_errors,
        f"Material compile failed: M_ApartmentWallPatina: {patina_errors}",
    )
    expected_mask = f"/Game/Prototype/Textures/{patina_texture_name}"
    require(
        expected_mask in expression_texture_paths(patina_material),
        "Mask texture is not linked to M_ApartmentWallPatina: "
        f"{expected_mask}",
    )
    validate_apartment_patina(patina_material)
    material_count += 1
    linked_textures += 1

    unreal.log_warning(
        "ART_UASSET_AUDIT PASS target=ApartmentVisual "
        f"textures={texture_count} materials={material_count} "
        f"linked_textures={linked_textures}"
    )


def validate_apartment_patina(material):
    require(material.get_editor_property("material_domain") == unreal.MaterialDomain.MD_DEFERRED_DECAL,
            "벽 얼룩은 벽지 위에 투영해야 한다")
    require(material.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_TRANSLUCENT,
            "벽 얼룩의 옅은 테두리가 잘렸다")
    material_input(material, unreal.MaterialProperty.MP_OPACITY)
    expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    require(any(isinstance(expr, unreal.MaterialExpressionOneMinus) for expr in expressions),
            "벽 얼룩의 사각 경계 감쇠가 없다")


def validate_listener_shell() -> None:
    """위층 사람 셸 계약: 메시에 구운 정점 AO와, 그것을 읽는 재질 장치.

    골 분진과 심화 폐색은 정점색이 없으면 조용히 0이 된다. 런타임 폴백으로는
    옳은 방향이지만, 릴리스 에셋이 그 상태로 나가는 것은 굽기 단계가 죽은
    채 지나갔다는 뜻이다. 메시가 흰 판때기가 아니라 실제 명암을 가졌는지,
    재질이 숨·분진 장치를 전부 붙들고 있는지 여기서 같이 잠근다.
    """
    mesh = load("/Game/Meshes/SM_ListenerEntityCrawl", unreal.StaticMesh)
    try:
        dynamic = unreal.new_object(unreal.DynamicMesh)
    except Exception:  # noqa: BLE001 - binding differences across versions
        dynamic = unreal.DynamicMesh()
    unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(
        mesh,
        dynamic,
        unreal.GeometryScriptCopyMeshFromAssetOptions(),
        unreal.GeometryScriptMeshReadLOD(),
    )
    queries = unreal.GeometryScript_MeshQueries
    require(
        bool(queries.get_has_vertex_colors(dynamic)),
        "Listener shell carries no baked vertex occlusion",
    )
    id_space = 0
    for counter in ("get_num_triangle_i_ds", "get_num_triangles"):
        function = getattr(queries, counter, None)
        if function is not None:
            id_space = int(function(dynamic))
            break
    samples = []
    for triangle_id in range(0, id_space, max(1, id_space // 128)):
        result = queries.get_triangle_vertex_colors(dynamic, triangle_id)
        if result[4]:
            samples.extend(color.r for color in result[1:4])
    require(
        len(samples) >= 32,
        "Listener shell vertex colors could not be sampled",
    )
    lowest, highest = min(samples), max(samples)
    require(
        lowest <= 0.8 and highest - lowest >= 0.15,
        "Listener shell vertex occlusion is flat "
        f"(min={lowest:.3f} max={highest:.3f}); the bake step did not run",
    )

    material = load(
        "/Game/Prototype/Materials/M_MissingFloorListenerPlasterUV",
        unreal.Material,
    )
    for parameter_name in ("BreathAmplitude", "TremorAmplitude", "DustAmount"):
        require(
            has_scalar_parameter(material, parameter_name),
            f"Listener plaster lost its vitals parameter: {parameter_name}",
        )
    expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    require(
        any(
            isinstance(expression, unreal.MaterialExpressionVertexColor)
            for expression in expressions
        ),
        "Listener plaster no longer reads the baked vertex occlusion",
    )
    material_input(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


def main() -> None:
    if os.environ.get("IG_APARTMENT_VISUAL_ONLY") == "1":
        validate_apartment_visual_assets()
        return
    total_lods, reduced_meshes = validate_meshes()
    photo_prop_meshes = validate_photo_prop_lods()
    texture_count = validate_textures()
    material_count, linked_textures = validate_materials()
    validate_listener_shell()
    # 생활 소품의 실제 반입 결과도 같은 에셋 검사에서 확인한다.
    import validate_household_assets
    validate_household_assets.main()
    atlas_entries = validate_print_atlas()
    unreal.log_warning(
        "ART_UASSET_AUDIT PASS "
        f"meshes={len(MESH_NAMES)} total_lods={total_lods} "
        f"reduced_meshes={reduced_meshes} photo_meshes={photo_prop_meshes} "
        f"textures={texture_count} "
        f"materials={material_count} linked_textures={linked_textures} "
        f"atlas_entries={atlas_entries}"
    )


if __name__ == "__main__":
    main()
