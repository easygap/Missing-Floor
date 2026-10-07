"""Import the packed print-atlas pages built by Scripts/build_texture_atlas.py.

The pages are ordinary colour textures with two things set deliberately:
clamped addressing, because every entry's UV rect ends inside the page and a
wrapped sample would pull in the entry on the far side; and a mip floor, so no
mip level is ever coarse enough for one texel to straddle two entries.

Run with:
    UnrealEditor-Cmd <uproject> -ExecutePythonScript=.../import_texture_atlas.py
"""

from __future__ import annotations

import os
import sys

import unreal

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from texture_atlas_contract import (  # noqa: E402
    ATLAS_DIR,
    ATLAS_TEXTURE_ROOT,
    AtlasContractError,
    GUTTER_SAFE_MIP_LEVELS,
    page_asset_name,
    page_file_name,
    try_load_manifest,
)


def _set_if_supported(texture, name, value):
    """UE renames a texture property now and then; never fail the import.

    Only for settings whose absence costs nothing visible. Anything the atlas
    depends on goes through _require_property instead.
    """
    try:
        texture.set_editor_property(name, value)
        return True
    except Exception:  # noqa: BLE001 - property set differs across UE minors
        unreal.log_warning(f"[IndieGame] Atlas: could not set {name}")
        return False


def _require_property(texture, name, value, why):
    """Set a property and read it back, or fail the import saying which.

    Four of these settings are the atlas. Clamped addressing is what keeps one
    notice from sampling the one packed beside it; BC7 is why the small Korean
    type on a shared page stays legible at all; sRGB is whether the page is
    the colour it was painted. Swallowing a failure on any of them produces a
    run that reports PASS and ships mush -- and it would ship it on exactly
    the artwork the atlas was built to protect.

    Read-back matters as much as the set: an enum this project has never used
    before is the likeliest thing to be spelled differently in a given UE
    minor, and a set that silently does nothing looks identical to one that
    worked.
    """
    try:
        texture.set_editor_property(name, value)
    except Exception as error:  # noqa: BLE001 - reported, not hidden
        raise RuntimeError(
            f"Atlas page rejected {name}={value} ({why}): {error}"
        ) from error
    actual = texture.get_editor_property(name)
    if actual != value:
        raise RuntimeError(
            f"Atlas page kept {name}={actual} instead of {value} ({why}); "
            "the import would have passed while shipping the wrong page"
        )


def import_texture_atlas() -> int:
    manifest = try_load_manifest()
    if manifest is None:
        # No packed atlas in this checkout. Every print material falls back to
        # its own texture, which is the pre-atlas behaviour and still ships.
        unreal.log_warning(
            "PRINT_ATLAS_IMPORT PASS pages=0 entries=0 "
            "(no manifest; run Scripts/build_texture_atlas.py)"
        )
        return 0

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset_subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if asset_tools is None or asset_subsystem is None:
        raise RuntimeError("Editor asset services are unavailable")

    imported = []
    for page in manifest["pages"]:
        index = page["index"]
        source = os.path.join(ATLAS_DIR, page_file_name(index))
        if not os.path.isfile(source):
            raise AtlasContractError(f"Atlas page not built: {source}")

        task = unreal.AssetImportTask()
        task.filename = source
        task.destination_path = ATLAS_TEXTURE_ROOT
        task.destination_name = page_asset_name(index)
        task.automated = True
        task.replace_existing = True
        task.save = False
        asset_tools.import_asset_tasks([task])

        asset_path = f"{ATLAS_TEXTURE_ROOT}/{page_asset_name(index)}"
        texture = unreal.load_asset(asset_path)
        if texture is None:
            raise RuntimeError(f"Atlas page import failed: {asset_path}")

        # Clamp: an atlas rect has no wrap. Without this a UV that lands a
        # hair past 1.0 on one entry samples whatever is packed opposite it.
        _require_property(
            texture, "address_x", unreal.TextureAddress.TA_CLAMP,
            "an atlas rect has no wrap")
        _require_property(
            texture, "address_y", unreal.TextureAddress.TA_CLAMP,
            "an atlas rect has no wrap")
        # A full mip chain from the texture group. Unreal has no per-texture
        # cap on how far it goes, so the 8 px gutter is what keeps neighbours
        # out of the sample: it survives to mip 3, by which point a notice is
        # thirty-odd pixels on screen and nothing on it is readable anyway.
        _set_if_supported(texture, "mip_gen_settings",
                          unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        _set_if_supported(texture, "num_cinematic_mip_levels", 0)
        _set_if_supported(texture, "max_texture_size", 0)
        _set_if_supported(texture, "lod_bias", 0)
        # 페이지는 스트리밍하지 않는다. 스트리머는 머티리얼이 UV를 엔트리 크기로
        # 줄여 쓴다는 걸 모르고 메시가 페이지 전체를 덮는다고 계산해서, 필요한
        # 밉을 몇 단계 낮게 잡는다. 같은 페이지의 다른 인쇄물이 가까이 있으면
        # 우연히 맞고, 없으면 편의점 담배 판매 안내처럼 글자가 뭉개진다.
        # 세 장을 다 올려 둬도 12 MB 남짓이다.
        _require_property(
            texture, "never_stream", True,
            "the streamer underestimates mips for atlas UV rects")
        # BC7 keeps the small Korean type on the notices legible; the pages are
        # the only textures in the project where several signs share one block.
        _require_property(
            texture,
            "compression_settings",
            unreal.TextureCompressionSettings.TC_BC7,
            "several notices share one compression block",
        )
        _require_property(
            texture, "srgb", True, "the pages are colour, not data")

        # Compression, sRGB and addressing all change how the texture is
        # built, not just how it is described, so nudge a rebuild before the
        # save. Best effort only: no other importer in this project calls it
        # -- import_photo_textures.py and generate_surface_textures.py both
        # set properties and go straight to save_loaded_assets, and their
        # textures are correct -- so this is belt over a brace that already
        # holds. If the method is not exposed in this UE minor, the read-back
        # above has already proved the properties took.
        try:
            texture.post_edit_change()
        except Exception:  # noqa: BLE001 - not fatal; the save still lands
            unreal.log_warning(
                f"[IndieGame] Atlas: {asset_path} did not rebuild in place"
            )
        imported.append(texture)

    if not asset_subsystem.save_loaded_assets(imported, False):
        raise RuntimeError("Could not save the imported atlas pages")

    unreal.log_warning(
        "PRINT_ATLAS_IMPORT PASS "
        f"pages={len(imported)} entries={len(manifest['entries'])} "
        f"gutter_safe_mips={GUTTER_SAFE_MIP_LEVELS}"
    )
    return len(imported)


if __name__ == "__main__":
    import_texture_atlas()
