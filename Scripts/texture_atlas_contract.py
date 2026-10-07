"""Shared, dependency-free contract for the printed-artwork texture atlas.

Every notice, plate, label and shop sign in the game is a separate
1-channel colour texture on its own material, and each of those is a separate
draw call and a separate streaming entry. There are twenty-five of them, none of
them tiles, and most of them are on screen at the same time in the store and on
the fourth-floor landing. That is exactly the set an atlas is for.

This module is imported by three very different callers -- the offline packer,
the release validator, and the in-editor material builder -- so it must stay
free of both Pillow and ``unreal``.

The atlas is authored, not discovered: ``PRINT_ATLAS_ENTRIES`` is the list of
textures allowed in, in a fixed order, so two machines pack identical pages.
"""

from __future__ import annotations

import json
import os


PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SOURCE_ART_DIR = os.path.join(PROJECT_ROOT, "Content", "SourceArt")
ATLAS_DIR = os.path.join(SOURCE_ART_DIR, "Atlas")
MANIFEST_PATH = os.path.join(ATLAS_DIR, "print_atlas.json")

# Package path the pages import to, alongside the individual textures.
ATLAS_TEXTURE_ROOT = "/Game/Prototype/Textures"
ATLAS_PAGE_PREFIX = "T_PrintAtlas"

MANIFEST_VERSION = 2

# 2048 is the largest page that still streams in one 8 MB BC7 block on the
# minimum spec in PERFORMANCE.md.
#
# 지금 인쇄물은 25장, 6.91 Mpx라 넓이만 보면 두 장에 들어간다. 그런데 폭이
# 1024인 메모 둘과 라벨 넷은 거터 때문에 한 줄에 둘씩 설 수 없어 세로로만
# 쌓이고, 그래서 2048 두 장 뒤에 1024 한 장이 붙는다.
#
# A page is only as large as its own contents need. Pages are packed at the
# maximum and then each one is repacked into the smallest power-of-two box
# that still holds it, so a tail page carrying a third of a full page ships as
# 2048x1024 rather than as 2048x2048 of mostly nothing. Every page is still a
# power of two on both axes, which is what the block compressors want.
PAGE_SIZE = 2048

# Candidate page shapes, smallest area first. The packer walks this list and
# keeps the first shape a page's contents fit into.
PAGE_SHAPES = tuple(sorted(
    (
        (width, height)
        for width in (256, 512, 1024, 2048)
        for height in (256, 512, 1024, 2048)
    ),
    key=lambda shape: (shape[0] * shape[1], max(shape)),
))

# Eight pixels of edge-extended bleed around every entry. At 2048 that keeps
# neighbours out of the sample down to the 1/32 mip, which is well past the
# distance any of this artwork is still legible at.
GUTTER = 8

# How far down the mip chain the gutter still separates neighbours. Each mip
# halves it: 8, 4, 2, 1, and at the fifth level it is half a texel and the
# entries start averaging into each other. Unreal has no per-texture way to
# stop a mip chain early, so this is not enforced -- it is the number that
# says the gutter is big enough. At mip 3 a 2048 page is 256 px and a 256 px
# notice is 32 px, which is well past anything on it being readable.
GUTTER_SAFE_MIP_LEVELS = GUTTER.bit_length() - 1

# Entries are packed in this order. Adding a texture goes at the end so the
# existing pages keep their layout; a reshuffle is a deliberate act.
PRINT_ATLAS_ENTRIES = (
    # Unit doors and the fourth-floor landing.
    "T_Plate401_D",
    "T_Plate402_D",
    "T_Plate403_D",
    "T_DoorLock_D",
    "T_Intercom_D",
    "T_SwitchPlate_D",
    "T_MercyNoteUnderDoor_D",
    "T_CaptureMercyNote_D",
    # Lobby.
    "T_SignVilla_D",
    "T_Calendar_D",
    # Apartment paper.
    "T_ArrivalContract_D",
    "T_NoteFridge_D",
    # Store interior print.
    "T_PosterSale_D",
    "T_TobaccoNotice_D",
    "T_SignAux5MonitorOnly_D",
    # Product artwork.
    "T_LabelWater_D",
    "T_LabelGreenTea_D",
    "T_LabelBarley_D",
    "T_LabelSoda_D",
    # Alley shopfronts.
    "T_SignLaundry_D",
    "T_SignHair_D",
    "T_SignHof_D",
    "T_SignSuper_D",
    "T_SignPC_D",
    "T_SignKaraoke_D",
)

# Textures that must stay off the atlas, with the reason. Kept explicit so a
# later reader does not "helpfully" add them back.
#
# The three the HUD loads by path are the subtle ones. They are ordinary world
# print materials too, so they look like obvious atlas candidates, and the
# damage from atlassing them does not show up in the editor at all. A canvas
# draw samples the whole texture and has no UV transform to point at a rect;
# worse, LoadObject on a literal path is invisible to the cooker, so the
# moment their material stops sampling them nothing keeps them in the pak and
# the HUD draws nothing. Scripts/check_cook_references.py fails if one of them
# reappears in PRINT_ATLAS_ENTRIES.
ATLAS_EXCLUSIONS = {
    "T_SignMain_D": "1K facade hero sign, lit by its own emissive path",
    "T_SignBlade_D": "1K facade hero sign, lit by its own emissive path",
    "T_TitleBackground_D": "full-screen frontend art, never in the world",
    "T_EpilogueWorkshop_D": "§9 에필로그 정지 화면, 월드에 놓이지 않는다",
    "T_EpilogueAutumn_D": "§9 에필로그 정지 화면, 월드에 놓이지 않는다",
    "T_EpilogueServiceBay_D": "§9 에필로그 정지 화면, 월드에 놓이지 않는다",
    "T_HudDialogueFilm_D": "UI group, no mips, not streamed",
    "T_MissingFloorJournalPaper_D": "UI group, no mips, not streamed",
    "T_AudioCalibrationWall_D": "UI group, no mips, not streamed",
    "T_MeterBox_D": "the journal draws it to the canvas; HUD path load",
    "T_PaperClean_V2_D": "the receipt panel draws it to the canvas; HUD path load",
    "T_PaperOld_V2_D": "the reading panel draws it to the canvas; HUD path load",
}


class AtlasContractError(RuntimeError):
    """Raised when the atlas on disk does not satisfy the contract."""


def page_asset_name(page_index: int) -> str:
    return f"{ATLAS_PAGE_PREFIX}{page_index}_D"


def page_file_name(page_index: int) -> str:
    return f"{page_asset_name(page_index)}.png"


def page_package_path(page_index: int) -> str:
    return f"{ATLAS_TEXTURE_ROOT}/{page_asset_name(page_index)}"


def source_texture_path(stem: str) -> str:
    return os.path.join(SOURCE_ART_DIR, f"{stem}.png")


def page_dimensions(manifest: dict, page_index: int) -> tuple[int, int]:
    """(width, height) of one page. Pages are not all the same shape."""
    page = manifest["pages"][page_index]
    return (page["width"], page["height"])


def load_manifest(path: str = MANIFEST_PATH, strict: bool = True) -> dict:
    """Reads the packed layout, or raises if it has not been built.

    ``strict`` also checks membership against PRINT_ATLAS_ENTRIES. A caller
    that only needs the layout -- where each stem landed and on what page --
    passes False, so it works on any well-formed manifest instead of only on
    this project's contracted one.
    """
    if not os.path.isfile(path):
        raise AtlasContractError(
            f"No atlas manifest at {path}. Run Scripts/build_texture_atlas.py."
        )
    with open(path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    if strict:
        validate_manifest(manifest)
    else:
        validate_manifest_geometry(manifest)
    return manifest


def try_load_manifest(path: str = MANIFEST_PATH, strict: bool = True):
    """Manifest if it is present and valid, otherwise None.

    The material builder uses this: a checkout that has not run the packer
    yet still produces a playable build from the individual textures.
    """
    try:
        return load_manifest(path, strict=strict)
    except (AtlasContractError, ValueError):
        return None


def validate_manifest(manifest: dict) -> None:
    """Full check: the geometry, plus the entry set this project contracted."""
    validate_manifest_geometry(manifest)

    entries = manifest.get("entries") or {}
    missing = [name for name in PRINT_ATLAS_ENTRIES if name not in entries]
    if missing:
        raise AtlasContractError(
            f"Atlas is missing {len(missing)} contracted entries: "
            + ", ".join(missing[:6])
        )
    extra = [name for name in entries if name not in PRINT_ATLAS_ENTRIES]
    if extra:
        raise AtlasContractError(
            "Atlas holds textures outside the contract: " + ", ".join(extra)
        )


def validate_manifest_geometry(manifest: dict) -> None:
    """Shape, bounds, gutters and UV maths, for any set of entries.

    Split out from validate_manifest so the packer's self-test can check the
    same maths against synthetic fixtures, which are deliberately nothing to
    do with this project's contracted texture list.
    """
    if manifest.get("version") != MANIFEST_VERSION:
        raise AtlasContractError(
            f"Atlas manifest version {manifest.get('version')} "
            f"!= {MANIFEST_VERSION}"
        )
    page_size = manifest.get("page_size")
    if page_size != PAGE_SIZE:
        raise AtlasContractError(f"Atlas page size {page_size} != {PAGE_SIZE}")
    if manifest.get("gutter") != GUTTER:
        raise AtlasContractError(
            f"Atlas gutter {manifest.get('gutter')} != {GUTTER}"
        )

    entries = manifest.get("entries") or {}
    pages = manifest.get("pages") or []
    if not pages:
        raise AtlasContractError("Atlas manifest lists no pages")
    for page in pages:
        for axis in ("width", "height"):
            extent = page.get(axis)
            if not isinstance(extent, int) or extent <= 0:
                raise AtlasContractError(
                    f"Atlas page {page.get('index')} has no {axis}"
                )
            if extent & (extent - 1):
                raise AtlasContractError(
                    f"Atlas page {page.get('index')} {axis} {extent} is not a "
                    "power of two"
                )
            if extent > PAGE_SIZE:
                raise AtlasContractError(
                    f"Atlas page {page.get('index')} {axis} {extent} exceeds "
                    f"the {PAGE_SIZE} maximum"
                )

    occupied: dict[int, list[tuple[str, int, int, int, int]]] = {}
    for name, entry in entries.items():
        page = entry["page"]
        if not 0 <= page < len(pages):
            raise AtlasContractError(f"{name} references page {page}")
        page_width = pages[page]["width"]
        page_height = pages[page]["height"]
        x, y = entry["x"], entry["y"]
        width, height = entry["w"], entry["h"]
        if x < 0 or y < 0:
            raise AtlasContractError(f"{name} starts outside page {page}")
        # A page edge needs no gutter: there is no neighbour beyond it and the
        # sampler clamps. Gutters are only ever about the entry next door.
        if x + width > page_width or y + height > page_height:
            raise AtlasContractError(f"{name} overruns page {page}")
        occupied.setdefault(page, []).append((name, x, y, width, height))

        # UVs are normalised against the page the entry actually landed on,
        # which is not the same size for every page.
        scale = entry["uv_scale"]
        bias = entry["uv_bias"]
        expected = (width / page_width, height / page_height)
        if abs(scale[0] - expected[0]) > 1e-9 or abs(scale[1] - expected[1]) > 1e-9:
            raise AtlasContractError(f"{name} UV scale disagrees with its rect")
        if abs(bias[0] - x / page_width) > 1e-9 \
                or abs(bias[1] - y / page_height) > 1e-9:
            raise AtlasContractError(f"{name} UV bias disagrees with its rect")

    for page, rects in occupied.items():
        for first in range(len(rects)):
            name_a, ax, ay, aw, ah = rects[first]
            for second in range(first + 1, len(rects)):
                name_b, bx, by, bw, bh = rects[second]
                # Gutters may not overlap either: one entry's bleed must not
                # land inside another entry's pixels.
                if (ax - GUTTER < bx + bw + GUTTER
                        and bx - GUTTER < ax + aw + GUTTER
                        and ay - GUTTER < by + bh + GUTTER
                        and by - GUTTER < ay + ah + GUTTER):
                    raise AtlasContractError(
                        f"{name_a} and {name_b} overlap on page {page}"
                    )


def atlas_uv_transform(manifest: dict, stem: str):
    """(scale_u, scale_v, bias_u, bias_v) for one texture, or None."""
    entry = (manifest.get("entries") or {}).get(stem)
    if entry is None:
        return None
    scale = entry["uv_scale"]
    bias = entry["uv_bias"]
    return (scale[0], scale[1], bias[0], bias[1])
