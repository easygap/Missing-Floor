"""Condition ImageGen albedo scans before PBR companion maps are derived.

ImageGen returns clean, evenly rendered material scans, but two defects survive
every prompt we have tried, and both of them are invisible in the flat source
image and glaring once the surface tiles across a wall under a moving flashlight:

  1. A low-frequency brightness field — a soft bright blob, usually centred.
     The generator cannot help composing an image, so it lights one. Tiled, that
     blob repeats on a grid and reads as baked lighting, which is exactly what
     MISSING_FLOOR_ART_MATRIX principle 4 forbids.
  2. Edges that do not wrap. Nothing in the generator knows the image is a tile.

Neither is a reason to regenerate: both are mechanical and both are measurable.
This step removes them and reports what it did, so the numbers and the pixels
cannot disagree.

Order matters. generate_ai_pbr_maps.py derives the tangent normal from the
albedo, so a brightness field left in the albedo becomes fake geometry — a slow
swell in the surface that lights wrong from every angle. This must run first.

    python Scripts/condition_ai_tiles.py                     # every registered tile
    python Scripts/condition_ai_tiles.py --report-only        # measure, change nothing
    python Scripts/condition_ai_tiles.py --only T_MissingFloorSteelStair
    python Scripts/condition_ai_tiles.py --input a.png --output b.png --seam period

PIL only, matching generate_ai_pbr_maps.py; this machine has no numpy and an
offline source-art step is not worth a dependency.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter, ImageStat

RESAMPLE_BOX = Image.Resampling.BOX
RESAMPLE_BICUBIC = Image.Resampling.BICUBIC
RESAMPLE_LANCZOS = Image.Resampling.LANCZOS
RESAMPLE_NEAREST = Image.Resampling.NEAREST


@dataclass(frozen=True)
class TileSpec:
    """One registered albedo scan and how it must be conditioned.

    stem        `<stem>_D.png` under Content/SourceArt. Carries the `T_` prefix
                so that the same --only value works here and in
                generate_ai_pbr_maps.py, which runs immediately after.
    flat_field  0 removes nothing, 1 removes the whole low-frequency field.
    field_cells Coarseness of the illumination estimate. This MUST stay well
                below the pattern frequency: with 8 cells on a 1024 px tile each
                cell covers 128 px, so anything smaller than that — the diamond
                treads, the plaster grain — is preserved untouched. Raising this
                starts eating the material itself.
    detail_gain Multiplier on everything finer than a field cell. 1.0 leaves the
                material as generated. Above 1.0 is for scans that came back
                too soft to derive a normal from: the anti-stipple wording in
                our prompts works, and it works on the real relief too, so a
                clean scan can arrive with almost no contrast to read. Boosting
                here rather than raising normal_strength downstream is safer,
                because it is measurable before the normal exists.
    seam        "none"      leave the edges alone (non-tiling art: dials, paper)
                "crossfade" wrap by cross-dissolving a margin; right for
                            stochastic surfaces with no repeating structure
                "period"    find the pattern pitch and crop to a whole number of
                            repeats; right for regular structure, where a
                            crossfade would smear the rhythm
    blend       Crossfade margin as a fraction of the edge.
    period_axis Expected fine structure for period mode. "vertical" and
                "horizontal" prevent a smooth orthogonal field from being
                mistaken for a second pattern axis. "auto" keeps the generic
                detector used by two-dimensional patterns such as checker
                plate.
    """

    stem: str
    flat_field: float = 1.0
    field_cells: int = 8
    detail_gain: float = 1.0
    seam: str = "crossfade"
    blend: float = 0.12
    period_axis: str = "auto"
    note: str = ""


# Registered incoming art. A stem with no file on disk is skipped quietly:
# the sheets arrive one at a time and a missing one is not an error.
TILES: tuple[TileSpec, ...] = (
    # The three surfaces README rule 2 asks the player to tell apart by eye.
    TileSpec(
        "T_MissingFloorSteelStair",
        seam="period",
        blend=0.03,
        note="Painted checker plate. The diamond pitch carries the rhythm, so "
        "align to whole repeats first and keep the fade narrow — a wide one "
        "smears two rows of diamonds into each other. No gain: v2 arrived at "
        "stddev 19.6 against 10-12 for the approved surfaces. v1 needed 3.0 and "
        "it did not help — the flashlight frame showed a smooth plate anyway, "
        "because a multiplier cannot add contrast the scan never had. The fix "
        "was a prompt that asked for a 30-75% tonal span outright instead of "
        "quoting a 3 mm height and hoping.",
    ),
    TileSpec(
        "T_RooftopWaterproofing",
        detail_gain=3.0,
        seam="crossfade",
        blend=0.14,
        note="Brushed urethane membrane. Stochastic roller lap, nothing to "
        "keep in register. Gain 3.0: the scan came back at stddev 2.6, so the "
        "roller laps that make this read as a coating rather than a green "
        "floor would not survive into the normal.",
    ),
    # The door leaf.
    TileSpec(
        "T_UnitDoorPaintedSteel",
        detail_gain=2.6,
        seam="crossfade",
        blend=0.12,
        note="Powder coat. Almost featureless, so the crossfade is free. "
        "Gain 2.6 only lifts orange peel to visible; a maintained Korean "
        "entrance door is supposed to be flat, and the target here is well "
        "under the 10-12 of the weathered surfaces.",
    ),
    # 403호 욕실. 원본에서 줄눈 가운데부터 줄눈 가운데까지 정수 장을 잘라 왔으니
    # 가장자리는 이미 이어진다. period 탐지는 타일 면의 잔결(51~64 px)을 주기로 읽고
    # 줄눈 하나를 지웠다. 이음매는 건드리지 않고 밝기 얼룩만 걷는다.
    TileSpec(
        "T_BathroomWallTile",
        seam="none",
        note="흰 유약 타일 25 cm, 줄눈 3 mm, 4 x 4장. 유약 면이라 원래 밋밋하다. "
        "올리지 않는다.",
    ),
    TileSpec(
        "T_BathroomFloorTile",
        seam="none",
        note="회색 논슬립 타일 20 cm, 줄눈 4 mm, 5 x 5장.",
    ),
)


# --------------------------------------------------------------------------
# Measurement. Every number reported here is also how we decide the result is
# better than the input, so the same functions run before and after.
# --------------------------------------------------------------------------


def _luma(image: Image.Image) -> Image.Image:
    return image.convert("RGB").convert("L")


def stipple_energy(gray: Image.Image) -> float:
    """Mean |image - 1px blur|: the pointillist speckle ImageGen adds.

    We do not correct this — a denoise strong enough to remove real stipple
    also removes the material. It is a regenerate-or-accept decision, so the
    number exists to make that call explicit rather than to drive a filter.
    """
    blurred = gray.filter(ImageFilter.GaussianBlur(1.0))
    return ImageStat.Stat(ImageChops.difference(gray, blurred)).mean[0]


def field_spread(gray: Image.Image, cells: int = 16) -> tuple[int, int, int]:
    """Low-frequency brightness unevenness: the blob, in 0..255 levels."""
    small = gray.resize((cells, cells), RESAMPLE_BOX)
    # Pillow 11이 Image.getdata를 폐기 예고했고, 그 경고는 stderr로 나간다.
    # 아트 빌드는 stderr를 실패로 읽으므로 경고 한 줄이 에셋 파이프라인 전체를
    # 막는다. 새 이름이 있으면 그것을 쓰고, 없으면 예전 이름으로 내려간다.
    flatten = getattr(small, "get_flattened_data", None)
    values = list(flatten() if flatten else small.getdata())
    return min(values), max(values), max(values) - min(values)


def seam_error(gray: Image.Image) -> tuple[float, float, float, float]:
    """Wrap error on each axis, and the adjacent-line error as its baseline.

    The baseline matters: on a noisy scan two genuinely neighbouring columns
    already differ, so a raw wrap error means nothing on its own. What decides
    tileability is the ratio. Below about 1.3 the seam is inside the material's
    own variation and no viewer can find it.
    """
    width, height = gray.size

    def mad(a: Image.Image, b: Image.Image) -> float:
        return ImageStat.Stat(ImageChops.difference(a, b)).mean[0]

    left = gray.crop((0, 0, 1, height))
    right = gray.crop((width - 1, 0, width, height))
    top = gray.crop((0, 0, width, 1))
    bottom = gray.crop((0, height - 1, width, height))

    mid_x = width // 2
    mid_y = height // 2
    base_h = mad(gray.crop((mid_x, 0, mid_x + 1, height)), gray.crop((mid_x + 1, 0, mid_x + 2, height)))
    base_v = mad(gray.crop((0, mid_y, width, mid_y + 1)), gray.crop((0, mid_y + 1, width, mid_y + 2)))
    return mad(left, right), mad(top, bottom), base_h, base_v


def measure(image: Image.Image, cells: int = 16) -> dict:
    gray = _luma(image)
    stat = ImageStat.Stat(gray)
    histogram = gray.histogram()
    total = sum(histogram)
    low, high, spread = field_spread(gray, cells)
    seam_h, seam_v, base_h, base_v = seam_error(gray)
    return {
        "size": list(image.size),
        "luma_mean": round(stat.mean[0] / 255.0, 4),
        "luma_stddev": round(stat.stddev[0], 2),
        "below_25pct": round(sum(histogram[: int(255 * 0.25)]) / total, 4),
        "above_65pct": round(sum(histogram[int(255 * 0.65) :]) / total, 4),
        "stipple": round(stipple_energy(gray), 3),
        "field_spread": spread,
        "field_range": [low, high],
        "seam_h": round(seam_h, 2),
        "seam_v": round(seam_v, 2),
        "seam_h_ratio": round(seam_h / base_h, 2) if base_h > 0.01 else None,
        "seam_v_ratio": round(seam_v / base_v, 2) if base_v > 0.01 else None,
    }


# --------------------------------------------------------------------------
# Correction
# --------------------------------------------------------------------------


def _flatten_once(source: Image.Image, cells: int, gain: float) -> Image.Image:
    """One split at the cell frequency: drop the low band, scale what is left.

    ImageChops.subtract computes (a - b) / scale + offset, so a gain of G is
    just scale = 1/G — removing the field and amplifying the detail happen in
    the same C-speed traversal.
    """
    width, height = source.size
    scale = 1.0 / max(0.01, gain)
    bands = []
    for band in source.split():
        # BOX down then BICUBIC up: BOX averages honestly over each cell
        # instead of point-sampling, BICUBIC gives a field with no cell edges.
        low = band.resize((cells, cells), RESAMPLE_BOX).resize((width, height), RESAMPLE_BICUBIC)
        mean = ImageStat.Stat(band).mean[0]
        bands.append(ImageChops.subtract(band, low, scale, int(round(mean))))
    return Image.merge("RGB", bands)


def flatten_and_boost(
    image: Image.Image, strength: float, cells: int, gain: float = 1.0
) -> Image.Image:
    """Remove the low-frequency field, optionally lifting the detail that remains.

    Per channel rather than on luma, because the blob usually carries a warm
    cast with it and a colour cast tiles just as visibly as a bright one. Each
    channel is re-centred on its own mean, so the surface keeps its colour
    exactly — only the spatial unevenness goes.

    Subtractive, not multiplicative: these scans sit inside a narrow luminance
    band by prompt, so over that range the two are equivalent and subtraction
    costs no division and cannot blow out a dark pixel.

    ImageChops.subtract computes (a - b) / scale + offset, so a gain of G is
    just scale = 1/G — removing the field and amplifying what is left happen in
    the same C-speed traversal.

    The gain scales the surviving field along with the material, so a tile
    conditioned at gain 3 reports roughly three times the field_spread of the
    same tile at gain 1. That is arithmetic, not a defect: what decides whether
    a blob is visible is its size relative to the surface's own contrast, which
    is why the accept test compares field/stddev and not field alone.
    """
    if strength <= 0.0:
        return image

    source = image.convert("RGB")
    width, height = source.size
    scale = 1.0 / max(0.01, gain)
    corrected_bands = []
    for band in source.split():
        # BOX down then BICUBIC up: BOX averages honestly over each cell
        # instead of point-sampling, BICUBIC gives a field with no cell edges.
        low = band.resize((cells, cells), RESAMPLE_BOX).resize((width, height), RESAMPLE_BICUBIC)
        mean = ImageStat.Stat(band).mean[0]
        corrected_bands.append(ImageChops.subtract(band, low, scale, int(round(mean))))

    corrected = Image.merge("RGB", corrected_bands)
    if strength >= 1.0:
        return corrected
    return Image.blend(source, corrected, strength)


def _ramp(width: int, height: int, horizontal: bool) -> Image.Image:
    """Linear 0..255 alpha ramp, built on one line and stretched."""
    if horizontal:
        line = Image.new("L", (width, 1))
        line.putdata([int(round(255 * i / max(1, width - 1))) for i in range(width)])
    else:
        line = Image.new("L", (1, height))
        line.putdata([int(round(255 * i / max(1, height - 1))) for i in range(height)])
    return line.resize((width, height), RESAMPLE_NEAREST)


def _crossfade_axis(image: Image.Image, margin: int, horizontal: bool) -> Image.Image:
    """Make one axis wrap by cross-dissolving a margin, then dropping it.

    Output loses `margin` pixels on this axis. Continuity: the new first line
    equals the old line at (size - margin) and the new last line equals the one
    just before it, so the wrap joins two lines that were already neighbours.
    """
    width, height = image.size
    extent = width if horizontal else height
    if margin * 2 >= extent:
        return image

    if horizontal:
        head_near = image.crop((0, 0, margin, height))
        head_far = image.crop((width - margin, 0, width, height))
        mask = _ramp(margin, height, True)
        # mask 0 at the outer edge -> head_far, 255 inside -> head_near.
        head = Image.composite(head_near, head_far, mask)
        out = Image.new(image.mode, (width - margin, height))
        out.paste(head, (0, 0))
        out.paste(image.crop((margin, 0, width - margin, height)), (margin, 0))
        return out

    head_near = image.crop((0, 0, width, margin))
    head_far = image.crop((0, height - margin, width, height))
    mask = _ramp(width, margin, False)
    head = Image.composite(head_near, head_far, mask)
    out = Image.new(image.mode, (width, height - margin))
    out.paste(head, (0, 0))
    out.paste(image.crop((0, margin, width, height - margin)), (0, margin))
    return out


def _detect_period(gray: Image.Image, horizontal: bool, coarse: int = 256) -> int | None:
    """Find the pattern pitch by self-difference at increasing lag.

    Coarse pass on a downsample so the search is cheap, then a fine pass at full
    resolution around the winner. Both use ImageChops.difference on crops, which
    is C speed — a pure-Python autocorrelation over a megapixel is not viable.
    """
    width, height = gray.size
    extent = width if horizontal else height

    def error_at(source: Image.Image, lag: int) -> float:
        w, h = source.size
        if horizontal:
            a = source.crop((0, 0, w - lag, h))
            b = source.crop((lag, 0, w, h))
        else:
            a = source.crop((0, 0, w, h - lag))
            b = source.crop((0, lag, w, h))
        return ImageStat.Stat(ImageChops.difference(a, b)).mean[0]

    small = gray.resize((coarse, coarse), RESAMPLE_BOX)
    # Below 3% the "period" is just neighbouring pixels; above 30% too few
    # repeats remain to crop against.
    lo = max(3, int(coarse * 0.03))
    hi = int(coarse * 0.30)
    if hi <= lo:
        return None
    scores = [(error_at(small, lag), lag) for lag in range(lo, hi)]
    best_small = min(scores)[1]

    scale = extent / coarse
    centre = int(round(best_small * scale))
    window = max(2, int(round(scale * 1.5)))
    lo_f = max(3, centre - window)
    hi_f = min(extent // 3, centre + window + 1)
    if hi_f <= lo_f:
        return None
    fine = [(error_at(gray, lag), lag) for lag in range(lo_f, hi_f)]
    best_error, best_lag = min(fine)

    # A real period beats the average lag clearly. If it does not, the surface
    # has no repeating structure and cropping to it would be superstition.
    average = sum(score for score, _ in fine) / len(fine)
    if average <= 0.01 or best_error > average * 0.92:
        return None
    return best_lag


def make_seamless(
    image: Image.Image,
    mode: str,
    blend: float,
    period_axis: str = "auto",
) -> tuple[Image.Image, str]:
    """Return a wrapping image at the original size, and what was done.

    The crossfade must be the LAST operation. Any resample after it re-breaks
    the wrap: the filter kernel runs off the edge of the image, PIL clamps, and
    the two borders stop agreeing. The first attempt here cropped to whole
    periods and then scaled back up, and measurably made the seam worse. So the
    order is: align the rhythm, scale to (target + margin), then fade the
    margin away — and nothing touches the pixels afterwards.

    That also means period and crossfade are not alternatives. The period crop
    removes the large phase error a crossfade would have to smear across; the
    narrow crossfade then absorbs what integer-pixel pitch cannot express.
    """
    if mode == "none":
        return image, "left alone"

    width, height = image.size
    notes: list[str] = []
    working = image

    if mode == "period":
        # A generated tile can already wrap more cleanly than the approved
        # conditioned surfaces.  In that case any crop, resample, or dissolve
        # is strictly destructive: it changes a valid rib pitch and can smear
        # the very structure period mode is meant to protect.  Use the same
        # wrap/contrast yardstick as --rank and preserve a passing source.
        source_stats = measure(image)
        contrast = source_stats["luma_stddev"] or 0.01
        wrap_h = source_stats["seam_h"] / contrast
        wrap_v = source_stats["seam_v"] / contrast
        if max(wrap_h, wrap_v) <= SEAM_OVER_CONTRAST_MAX:
            return image, (
                f"source wrap retained ({wrap_h:.2f}/{wrap_v:.2f} <= "
                f"{SEAM_OVER_CONTRAST_MAX:.2f}); no resample"
            )

        gray = _luma(image)
        px = _detect_period(gray, True) if period_axis != "horizontal" else None
        py = _detect_period(gray, False) if period_axis != "vertical" else None

        # _detect_period deliberately ignores sub-3%-of-frame lags.  That is
        # appropriate for checker plate and panels, but a wallpaper emboss at
        # 1024 px can be only 8 px wide.  The line-profile DFT used by --rank
        # sees that scale reliably.  Fall back to its dominant axis instead of
        # silently degrading period mode into a crossfade-only operation.
        fine_period, fine_power, fine_axis = stripe_period(gray)
        if (fine_period and fine_axis == "vertical"
                and period_axis in ("auto", "vertical")):
            px = fine_period
            notes.append(f"fine vertical period {px}px (power {fine_power:.2f})")
        elif (fine_period and fine_axis == "horizontal"
              and period_axis in ("auto", "horizontal")):
            py = fine_period
            notes.append(f"fine horizontal period {py}px (power {fine_power:.2f})")

        if px or py:
            keep_w = (width // px) * px if px else width
            keep_h = (height // py) * py if py else height
            min_w = px or 1
            min_h = py or 1
            if keep_w >= min_w and keep_h >= min_h:
                working = image.crop((0, 0, keep_w, keep_h))
                notes.append(
                    f"period crop {keep_w}x{keep_h} "
                    f"(pitch {px or '-'}x{py or '-'})"
                )
        else:
            # No trustworthy pitch. Say so rather than crop to a made-up one.
            notes.append("no period found, crossfade only")

    margin_x = max(2, int(round(width * blend)))
    margin_y = max(2, int(round(height * blend)))
    # Oversize first so the fade lands exactly on the requested size and no
    # resample follows it.
    working = working.resize((width + margin_x, height + margin_y), RESAMPLE_LANCZOS)
    working = _crossfade_axis(working, margin_x, True)
    working = _crossfade_axis(working, margin_y, False)
    notes.append(f"crossfade {margin_x}x{margin_y}px into {working.size[0]}x{working.size[1]}")
    return working, "; ".join(notes)


def write_preview(image: Image.Image, path: Path, side: int = 1024) -> None:
    """Lay the tile out 2x2 so a human can look for the grid.

    The ratios say the borders agree; they cannot say the eye is satisfied. A
    tile can measure clean and still show an obvious repeat, because what gives
    a tile away is usually one memorable feature recurring, not an edge. So the
    numbers gate the file and this picture gates the approval.
    """
    cell = side // 2
    small = image.convert("RGB").resize((cell, cell), RESAMPLE_LANCZOS)
    sheet = Image.new("RGB", (side, side))
    for x in (0, cell):
        for y in (0, cell):
            sheet.paste(small, (x, y))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path, optimize=True)
    print(f"       preview {path}")


# --------------------------------------------------------------------------
# Provenance. Conditioning is destructive and in place, so the sidecar is what
# stops a second run from flattening the field twice.
# --------------------------------------------------------------------------


def _digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def _sidecar(source_root: Path, stem: str) -> Path:
    return source_root / ".conditioned" / f"{stem}.json"


def condition_file(
    path: Path,
    spec: TileSpec,
    source_root: Path,
    report_only: bool,
    force: bool,
) -> bool:
    image = Image.open(path)
    before = measure(image)

    sidecar = _sidecar(source_root, spec.stem)
    current = _digest(path)
    if not report_only and sidecar.exists():
        record = json.loads(sidecar.read_text(encoding="utf-8"))
        if record.get("output_sha256") == current:
            # Refuse even under --force. Conditioning is destructive and in
            # place, so "do it again" on an already-conditioned file means
            # flattening a flattened field and multiplying an applied gain.
            # The first version of this let --force through and the stair tile
            # came back at 9x gain with its pitch mis-detected. Re-running has
            # exactly one correct starting point: the ImageGen original.
            if force:
                print(f"[COND] {spec.stem}: already conditioned — --force cannot "
                      f"re-run in place. Regenerate the albedo first:")
                print(r"       .\Scripts\Prepare-AIArt.ps1 -OnlySource @('<raw stem>')")
            else:
                print(f"[COND] {spec.stem}: already conditioned, skipping")
            return False

    print(f"\n[COND] {spec.stem}  ({path.name})")
    print(f"       in   {before['size'][0]}x{before['size'][1]}  luma {before['luma_mean']:.3f}"
          f"  stipple {before['stipple']:.2f}  field {before['field_spread']}"
          f"  seam {before['seam_h']:.2f}/{before['seam_v']:.2f}"
          f"  ratio {before['seam_h_ratio']}/{before['seam_v_ratio']}")

    if report_only:
        return False

    corrected = flatten_and_boost(
        image, spec.flat_field, spec.field_cells, spec.detail_gain
    )
    corrected, seam_note = make_seamless(
        corrected, spec.seam, spec.blend, spec.period_axis
    )
    after = measure(corrected)

    print(f"       out  luma {after['luma_mean']:.3f}  stipple {after['stipple']:.2f}"
          f"  field {after['field_spread']}"
          f"  seam {after['seam_h']:.2f}/{after['seam_v']:.2f}"
          f"  ratio {after['seam_h_ratio']}/{after['seam_v_ratio']}")
    print(f"       seam {seam_note}")

    # Refuse to make things worse. A conditioning step that quietly degrades a
    # scan is worse than one that fails, because nothing downstream would tell.
    #
    # The test is field/stddev, not field. A blob is visible in proportion to
    # the contrast it sits in: 27 levels of unevenness across a surface that
    # varies by 8 is a stain, the same 27 across a surface that varies by 24 is
    # not. Testing raw field rejected every gained tile here even though the
    # flat-field had worked perfectly — the residue and the material had simply
    # been multiplied together.
    #
    # The ratio only means something once there is a blob to measure. A scan
    # that arrives at field 1 is already at the floor of what a 16x16 average
    # can resolve, and 1 -> 2 is rounding — but as a ratio it doubles and
    # rejects a perfectly good tile. Four levels out of 255 is invisible
    # against any contrast, so below that the absolute value decides.
    NEGLIGIBLE_FIELD = 4
    before_relative = before["field_spread"] / max(0.5, before["luma_stddev"])
    after_relative = after["field_spread"] / max(0.5, after["luma_stddev"])
    print(f"       blob/contrast {before_relative:.2f} -> {after_relative:.2f}")
    if (spec.flat_field > 0
            and after["field_spread"] > NEGLIGIBLE_FIELD
            and after_relative > before_relative * 1.05):
        print(f"       REJECTED: blob grew relative to surface contrast "
              f"({before_relative:.2f} -> {after_relative:.2f})")
        return False

    corrected.save(path, optimize=True)
    if spec.seam != "none":
        write_preview(
            corrected,
            source_root.parent.parent / "Docs" / "Media" / "tiles" / f"{spec.stem}_tile2x2.png",
        )
    sidecar.parent.mkdir(parents=True, exist_ok=True)
    sidecar.write_text(
        json.dumps(
            {
                "stem": spec.stem,
                "seam_mode": spec.seam,
                "seam_action": seam_note,
                "flat_field": spec.flat_field,
                "field_cells": spec.field_cells,
                "period_axis": spec.period_axis,
                "input_sha256": current,
                "output_sha256": _digest(path),
                "before": before,
                "after": after,
            },
            indent=2,
            ensure_ascii=False,
        ),
        encoding="utf-8",
    )
    return True


# --------------------------------------------------------------------------
# Candidate ranking. A generation session returns a batch of near-identical
# variants, and the differences that decide which one ships -- speckle, wrap
# error, how bright it sits, how fine the structure is -- are all invisible
# side by side on a screen. Measure them instead of squinting.
# --------------------------------------------------------------------------

# Bands taken from the record in Docs/IMAGEGEN_PROMPTS_2026-08-14.md: six
# approved scans came in at 0.39-1.37 stipple with the anti-stipple wording,
# against 6.89 for the older asset. Seam is the module's own 1.3 rule.
STIPPLE_GOOD = 1.5
STIPPLE_BAD = 5.0
# Wrap error as a fraction of the material's own contrast. The ratio-to-
# adjacent-line measure that measure() reports cannot be used for ranking a
# structured texture: on vertical stripes two adjacent *rows* are identical,
# so its baseline collapses to ~0.05 levels and every candidate scores 20+.
# Normalising by stddev instead is stable whatever the structure. Calibrated
# against the conditioned assets that shipped: steel stair 0.12, rooftop 0.40,
# door 0.62/0.78, villa stucco 0.86/0.93, and the wallpaper this one hangs
# beside 0.88/1.04. One is the line shipping practice already sits on.
SEAM_OVER_CONTRAST_MAX = 1.0
# The blob, in 0..255 levels across a 16x16 grid. Brightness alone is not the
# tell: a pale wallpaper legitimately sits high and the shipping
# T_ApartmentWallpaperV2_D has 99% of its pixels above 65%. What makes a scan
# unusable is brightness that *varies* across the frame, because that is the
# baked composition light this script exists to remove -- and past a point it
# cannot be removed without taking the material with it.
FIELD_SPREAD_MAX = 14


def _line_profile(gray: Image.Image, vertical_stripes: bool) -> list[float]:
    """Column means (or row means), as a 1D signal, in one resize."""
    width, height = gray.size
    strip = (gray.resize((width, 1), RESAMPLE_BOX) if vertical_stripes
             else gray.resize((1, height), RESAMPLE_BOX))
    # Pillow 11 deprecated getdata and the warning goes to stderr, which the
    # art build reads as failure. Same fallback field_spread uses.
    flatten = getattr(strip, "get_flattened_data", None)
    return list(flatten() if flatten else strip.getdata())


def stripe_period(gray: Image.Image, lo: int = 2, hi: int = 120):
    """Dominant stripe pitch in pixels, by DFT over the line profile.

    _detect_period cannot answer this. It searches a 256 px downsample from
    lag 7 up, so the finest pitch it can see is about 3% of the frame -- 45 mm
    on a 165 cm tile. A wallpaper emboss is millimetres, so that detector
    returns whatever low-frequency drift happens to sit near its floor and
    reports it as the pattern. Collapsing to a line profile first keeps the
    stripes and throws away everything that is not one, which makes a plain
    DFT both cheap and correct.

    Returns (period_px, power, axis) for the stronger axis, or (None, 0, "").
    """
    best = (None, 0.0, "")
    for vertical in (True, False):
        profile = _line_profile(gray, vertical)
        count = len(profile)
        if count < 2 * hi:
            continue
        mean = sum(profile) / count
        centred = [value - mean for value in profile]
        for period in range(lo, hi + 1):
            omega = 2.0 * math.pi / period
            real = sum(v * math.cos(omega * i) for i, v in enumerate(centred))
            imag = sum(v * math.sin(omega * i) for i, v in enumerate(centred))
            power = math.hypot(real, imag) / count
            if power > best[1]:
                best = (period, power, "vertical" if vertical else "horizontal")
    return best


def rank_candidates(
    directory: Path,
    coverage_cm: float | None = None,
) -> list[dict]:
    """Measure every image in a folder and score it as a tile candidate.

    ``coverage_cm`` is how much real surface the tile is meant to span. Given
    it, the detected structure period is reported in millimetres, which is the
    only way to tell a hairline emboss from panelling: both are "vertical
    ribs" and they are the same picture until you know the scale.
    """
    rows = []
    for path in sorted(directory.iterdir()):
        if path.suffix.lower() not in (".png", ".jpg", ".jpeg", ".webp"):
            continue
        try:
            image = Image.open(path)
        except Exception:  # noqa: BLE001 - a stray non-image in the folder
            continue
        stats = measure(image)
        gray = _luma(image)
        period, power, axis = stripe_period(gray)
        pitch_mm = None
        if period and coverage_cm:
            extent = image.size[0] if axis == "vertical" else image.size[1]
            pitch_mm = round(period / extent * coverage_cm * 10.0, 2)

        contrast = stats["luma_stddev"] or 0.01
        seam_h = round(stats["seam_h"] / contrast, 2)
        seam_v = round(stats["seam_v"] / contrast, 2)

        problems = []
        if stats["stipple"] > STIPPLE_BAD:
            problems.append("stipple")
        if max(seam_h, seam_v) > SEAM_OVER_CONTRAST_MAX:
            problems.append("seam")
        if stats["field_spread"] > FIELD_SPREAD_MAX:
            problems.append("blob")
        rows.append({
            "name": path.name,
            "period_px": period,
            "period_power": round(power, 2),
            "period_axis": axis,
            "pitch_mm": pitch_mm,
            "seam_over_contrast": [seam_h, seam_v],
            "problems": problems,
            **stats,
        })

    # Sort by what cannot be fixed downstream, then by what can. Stipple is a
    # regenerate-or-accept call; the brightness field and the seam are both
    # corrected by this script, so they only break ties.
    rows.sort(key=lambda r: (
        len(r["problems"]),
        r["stipple"],
        max(r["seam_over_contrast"]),
    ))
    return rows


def print_ranking(rows: list[dict], coverage_cm: float | None) -> None:
    pitch_head = "pitch" if coverage_cm else "period"
    print(
        f"\n{'#':>2}  {'file':<40} {'size':>10} {'luma':>6} {'sdev':>5} "
        f"{'stipple':>8} {'blob':>5} {'seam h/v':>10} {pitch_head:>9} "
        f"{'/frame':>7}  notes"
    )
    for index, row in enumerate(rows, start=1):
        pitch = (f"{row['pitch_mm']}mm" if row["pitch_mm"] is not None
                 else (f"{row['period_px']}px" if row["period_px"] else "-"))
        extent = (row["size"][0] if row["period_axis"] == "vertical"
                  else row["size"][1])
        count = f"{extent // row['period_px']}" if row["period_px"] else "-"
        seam = "/".join(str(v) for v in row["seam_over_contrast"])
        print(
            f"{index:>2}. {row['name'][:40]:<40} "
            f"{row['size'][0]:>4}x{row['size'][1]:<5} "
            f"{row['luma_mean']:>6.3f} {row['luma_stddev']:>5.1f} "
            f"{row['stipple']:>8.2f} {row['field_spread']:>5} "
            f"{seam:>10} {pitch:>9} {count:>7}  "
            f"{', '.join(row['problems']) or 'ok'}"
        )
    print(
        f"\nbands: stipple <={STIPPLE_GOOD} good / >{STIPPLE_BAD} regenerate; "
        f"seam (wrap error / contrast) <={SEAM_OVER_CONTRAST_MAX}; "
        f"blob (field spread, levels) <={FIELD_SPREAD_MAX}.\n"
        "The blob and the seam are corrected by this script, so they break "
        "ties rather than decide. Stipple does not correct: a denoise strong "
        "enough to remove it removes the material too.\n"
        "sdev is expected low before conditioning -- the anti-stipple wording "
        "flattens real relief as well, and detail_gain puts it back.\n"
        "pitch needs --coverage-cm to mean anything: a hairline emboss and "
        "panelling are the same picture until you know the real scale."
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Flat-field and seam conditioning for ImageGen albedo scans"
    )
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "Content" / "SourceArt",
    )
    parser.add_argument("--only", action="append", default=[], help="Tile stem (repeatable)")
    parser.add_argument("--report-only", action="store_true", help="Measure, write nothing")
    parser.add_argument("--force", action="store_true", help="Re-run even if the sidecar matches")
    parser.add_argument("--input", type=Path, help="Ad-hoc: condition this file")
    parser.add_argument("--output", type=Path, help="Ad-hoc: write here (default: in place)")
    parser.add_argument("--seam", default="crossfade", choices=("none", "crossfade", "period"))
    parser.add_argument(
        "--period-axis",
        default="auto",
        choices=("auto", "vertical", "horizontal"),
        help="Expected fine stripe direction for period seam mode",
    )
    parser.add_argument("--flat-field", type=float, default=1.0)
    parser.add_argument("--field-cells", type=int, default=8)
    parser.add_argument("--blend", type=float, default=0.12)
    parser.add_argument("--detail-gain", type=float, default=1.0)
    parser.add_argument(
        "--preview",
        action="store_true",
        help="Also write a 2x2 tiling sheet for eye approval",
    )
    parser.add_argument(
        "--rank",
        type=Path,
        help="Measure every image in this folder and rank them as candidates",
    )
    parser.add_argument(
        "--coverage-cm",
        type=float,
        help="Real surface the tile spans, so --rank can report pitch in mm",
    )
    args = parser.parse_args()

    if args.rank:
        if not args.rank.is_dir():
            print(f"[COND] not a folder: {args.rank}", file=sys.stderr)
            return 1
        rows = rank_candidates(args.rank, args.coverage_cm)
        if not rows:
            print(f"[COND] no images in {args.rank}", file=sys.stderr)
            return 1
        print_ranking(rows, args.coverage_cm)
        return 0

    if args.input:
        if not args.input.exists():
            print(f"[COND] missing input: {args.input}", file=sys.stderr)
            return 1
        image = Image.open(args.input)
        before = measure(image)
        print(f"\n[COND] {args.input.name}")
        print(f"       in   {before['size'][0]}x{before['size'][1]}  luma {before['luma_mean']:.3f}"
              f"  stipple {before['stipple']:.2f}  field {before['field_spread']}"
              f"  seam {before['seam_h']:.2f}/{before['seam_v']:.2f}"
              f"  ratio {before['seam_h_ratio']}/{before['seam_v_ratio']}")
        if args.report_only:
            return 0
        spec = TileSpec(
            "adhoc",
            flat_field=args.flat_field,
            field_cells=args.field_cells,
            detail_gain=args.detail_gain,
            seam=args.seam,
            blend=args.blend,
            period_axis=args.period_axis,
        )
        out = flatten_and_boost(
            image, spec.flat_field, spec.field_cells, spec.detail_gain
        )
        out, seam_note = make_seamless(
            out, spec.seam, spec.blend, spec.period_axis
        )
        after = measure(out)
        print(f"       out  luma {after['luma_mean']:.3f}  stipple {after['stipple']:.2f}"
              f"  field {after['field_spread']}"
              f"  seam {after['seam_h']:.2f}/{after['seam_v']:.2f}"
              f"  ratio {after['seam_h_ratio']}/{after['seam_v_ratio']}")
        print(f"       seam {seam_note}")
        target = args.output or args.input
        out.save(target, optimize=True)
        if args.preview:
            write_preview(out, target.with_name(f"{target.stem}_tile2x2.png"))
        return 0

    wanted = set(args.only)
    touched = 0
    seen = 0
    for spec in TILES:
        if wanted and spec.stem not in wanted:
            continue
        path = args.source_root / f"{spec.stem}_D.png"
        if not path.exists():
            continue
        seen += 1
        if condition_file(path, spec, args.source_root, args.report_only, args.force):
            touched += 1

    if seen == 0:
        print("[COND] no registered albedo scans present yet")
    else:
        print(f"\n[COND] {seen} present, {touched} conditioned")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
