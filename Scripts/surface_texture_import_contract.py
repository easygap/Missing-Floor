"""재질에서 교체된 텍스처의 중복 import를 막고, 원본을 쓰는 빌드는 유지한다."""

from texture_atlas_contract import page_package_path


# 2026-10-20에 코드·맵·패키지 참조와 AssetRegistry를 함께 확인한 목록이다.
# 새 atlas 항목을 자동으로 포함하면 개별 텍스처를 쓰는 다른 재질을 놓칠 수 있다.
ATLAS_SOURCE_ONLY = frozenset({
    "T_ArrivalContract_D", "T_Calendar_D", "T_CaptureMercyNote_D",
    "T_DoorLock_D", "T_Intercom_D", "T_LabelBarley_D", "T_LabelGreenTea_D",
    "T_LabelSoda_D", "T_LabelWater_D", "T_MercyNoteUnderDoor_D",
    "T_NoteFridge_D", "T_Plate401_D", "T_Plate402_D", "T_Plate403_D",
    "T_PosterSale_D", "T_SignAux5MonitorOnly_D", "T_SignHair_D", "T_SignHof_D",
    "T_SignKaraoke_D", "T_SignLaundry_D", "T_SignPC_D", "T_SignSuper_D",
    "T_SignVilla_D", "T_TobaccoNotice_D",
})
PHOTO_FALLBACKS = frozenset({
    "T_Asphalt_D", "T_Asphalt_N", "T_Asphalt_R", "T_Blanket_D", "T_Blanket_N",
    "T_Concrete_N", "T_Concrete_R", "T_Jangpan_D", "T_Jangpan_N", "T_Jangpan_R",
    "T_MetalBrushed_N", "T_MetalBrushed_R", "T_WoodDark_D", "T_WoodDark_N",
})
# 벽지는 ApartmentWallpaperV2, 매장 바닥·천장은 retail_surface_contract가 만든다.
RETIRED_SURFACES = frozenset({
    "T_Wallpaper_D", "T_Wallpaper_N",
    "T_StoreTile_D", "T_StoreTile_N", "T_StoreTile_R",
    "T_CeilingTile_D", "T_CeilingTile_N",
})


def skip_import_reason(asset_name, atlas_manifest, asset_exists):
    """이미 불필요한 패키지일 때만 사유를 반환한다. 빈 문자열이면 import한다."""
    if asset_name in RETIRED_SURFACES:
        return "retired_surface"
    if asset_name in PHOTO_FALLBACKS:
        replacement = "/Game/Prototype/Textures/T_Photo_" + asset_name[2:]
        if asset_exists(replacement):
            return "photo:" + replacement
    if asset_name in ATLAS_SOURCE_ONLY and atlas_manifest is not None:
        entry = atlas_manifest["entries"].get(asset_name)
        if entry is not None:
            replacement = page_package_path(entry["page"])
            if asset_exists(replacement):
                return "atlas:" + replacement
    return ""


def self_test():
    """대체품 유무·일부 페이지 누락·개별 재질 예외를 엔진 없이 검사한다."""
    manifest = {"entries": {
        name: {"page": index % 2}
        for index, name in enumerate(sorted(ATLAS_SOURCE_ONLY))
    }}
    for name in ATLAS_SOURCE_ONLY:
        assert skip_import_reason(name, manifest, lambda _: True).startswith("atlas:")
        assert not skip_import_reason(name, manifest, lambda _: False)
        assert not skip_import_reason(name, None, lambda _: True)
        page = manifest["entries"][name]["page"]
        expected = "atlas:" + page_package_path(page) if page == 0 else ""
        assert skip_import_reason(name, manifest, lambda p: p == page_package_path(0)) == expected
    for name in PHOTO_FALLBACKS:
        replacement = "/Game/Prototype/Textures/T_Photo_" + name[2:]
        assert skip_import_reason(name, None, lambda p: p == replacement) == "photo:" + replacement
        assert not skip_import_reason(name, None, lambda _: False)
        assert not skip_import_reason(name, None, lambda p: p == replacement + "_Other")
    for name in RETIRED_SURFACES:
        assert skip_import_reason(name, None, lambda _: False) == "retired_surface"
    for name in ("T_SwitchPlate_D", "T_MeterBox_D", "T_SignMain_D", "T_Concrete_D",
                 "T_ApartmentWallpaperV2_D", "T_Photo_Asphalt_D", "T_NewAsset_D"):
        assert not skip_import_reason(name, manifest, lambda _: True)
    assert not skip_import_reason("T_Plate401_D", {"entries": {}}, lambda _: True)
    print("SURFACE_TEXTURE_IMPORT PASS atlas=24 photo=14 retired=7 fallback=preserved")


if __name__ == "__main__":
    import sys
    if sys.argv[1:] != ["--self-test"]:
        raise SystemExit("사용법: surface_texture_import_contract.py --self-test")
    self_test()
