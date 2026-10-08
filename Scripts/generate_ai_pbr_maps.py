"""Generate conservative PBR companion maps from the approved AI material scans.

This is an offline source-art step.  It deliberately keeps the generated bitmap as
BaseColor and derives only low-amplitude surface information from it; hero geometry,
silhouette, collision, and shadows remain the responsibility of the 3D mesh.
"""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageFilter, ImageOps, ImageStat


@dataclass(frozen=True)
class SurfaceSpec:
    stem: str
    roughness: float
    roughness_min: float
    roughness_max: float
    normal_strength: float
    rough_detail: float = 0.22
    ao_depth: float = 1.35
    wetness: float = 0.0


SURFACES = (
    # ImageGen supplies only the evenly lit BaseColor scan. Conservative
    # companion maps make the embossed paper read under a moving practical
    # light without turning the tiny floral print into glittering geometry.
    SurfaceSpec(
        "T_ApartmentWallpaperV2", 0.86, 0.72, 0.94, 0.32,
        rough_detail=0.12, ao_depth=0.62),
    # Exterior cement render is deliberately matte. The generated scan owns
    # only colour; this conservative relief keeps rain streaks from becoming
    # deep grooves and remains stable under the moving alley practicals.
    SurfaceSpec(
        "T_KoreanVillaStucco", 0.88, 0.76, 0.96, 0.44,
        rough_detail=0.10, ao_depth=0.66),
    # 무코팅 크라프트지는 확산 반사가 강하다. 상자가 조각처럼 보이지 않는
    # 범위에서 손전등에 세로 골과 섬유 결이 드러나도록 조정한다.
    SurfaceSpec(
        "T_MovingBoxCardboard", 0.87, 0.74, 0.95, 0.34,
        rough_detail=0.12, ao_depth=0.54),
    # One neutral fibre response is shared by the clean, damp, folded and old
    # paper stocks. Their colour maps keep the individual stains and creases;
    # this companion set supplies only sub-millimetre fibre relief, roughness
    # breakup and shallow occlusion. Keeping text out of the height source is
    # important for the runtime-drawn Korean thermal receipt.
    SurfaceSpec(
        "T_PaperClean_V2", 0.84, 0.72, 0.93, 0.24,
        rough_detail=0.10, ao_depth=0.48),
    SurfaceSpec("T_AlleyCatTabby", 0.83, 0.68, 0.94, 0.34, rough_detail=0.14),
    SurfaceSpec("T_P3CabinetPaintedSteel", 0.64, 0.48, 0.78, 0.58, wetness=0.42),
    SurfaceSpec("T_CarrierBagFilm", 0.29, 0.18, 0.46, 0.24, rough_detail=0.12),
    # Dry gypsum is shared by the fifth-floor shell and the listener mesh.
    # Keep it highly diffuse; the flashlight should reveal powder and cracks
    # through N/A, never turn the body into polished stone.
    SurfaceSpec(
        "T_MissingFloorDryPlaster", 0.91, 0.78, 0.97, 0.62,
        rough_detail=0.10, ao_depth=0.86),
    # The listener front card is still a lit surface, not an unlit pasted
    # photo. Conservative relief lets the flashlight pick up pajama folds and
    # plaster grain without pretending the portrait has full geometric depth.
    SurfaceSpec(
        "T_SpriteListenerFront", 0.86, 0.72, 0.96, 0.38,
        rough_detail=0.12, ao_depth=0.58),
    # The four locomotion phases share the same restrained response. Keeping
    # their parameters identical prevents roughness or normal strength from
    # visibly flashing when the runtime advances a frame.
    SurfaceSpec(
        "T_SpriteListenerCrawl0", 0.86, 0.72, 0.96, 0.38,
        rough_detail=0.12, ao_depth=0.58),
    SurfaceSpec(
        "T_SpriteListenerCrawl1", 0.86, 0.72, 0.96, 0.38,
        rough_detail=0.12, ao_depth=0.58),
    SurfaceSpec(
        "T_SpriteListenerCrawl2", 0.86, 0.72, 0.96, 0.38,
        rough_detail=0.12, ao_depth=0.58),
    SurfaceSpec(
        "T_SpriteListenerCrawl3", 0.86, 0.72, 0.96, 0.38,
        rough_detail=0.12, ao_depth=0.58),
    SurfaceSpec(
        "T_SpriteFinalCavity", 0.90, 0.76, 0.97, 0.42,
        rough_detail=0.10, ao_depth=0.72),
    SurfaceSpec(
        "T_SpriteMokFinalUpper", 0.84, 0.68, 0.94, 0.36,
        rough_detail=0.10, ao_depth=0.54),
    # Painted steel stair treads. The diamond tread is a real 3 mm relief that
    # no mesh here will ever carry — the stairs are scaled boxes — so the normal
    # is the only place it can exist.
    #
    # Strength came down from 0.88 to 0.62 when v2 replaced v1. That is not a
    # retreat: 0.88 was propping up a scan with stddev 3.5, and v2 arrives at
    # 19.6, so the same setting would now emboss the plate into corrugation.
    # 0.62 matches the dry plaster, which sits at a comparable contrast.
    #
    # Roughness stays below the concrete family on purpose. Alkyd paint over
    # steel is still a dielectric, so metallic remains 0, but it catches a
    # flashlight in a way troweled concrete cannot. That difference is the
    # point: README rule 2 asks the player to choose a floor by how loud it is,
    # and until now the metal stair and the concrete corridor were the same
    # picture.
    SurfaceSpec(
        "T_MissingFloorSteelStair", 0.68, 0.52, 0.86, 0.62,
        rough_detail=0.14, ao_depth=0.90),
    # 403호 욕실 벽은 유약 타일이라 번들거리고, 줄눈만 거칠게 파인다. 바닥은 미끄럼
    # 방지 타일이라 무광이다. 손전등에 벽과 바닥이 다르게 비쳐야 한다.
    SurfaceSpec(
        "T_BathroomWallTile", 0.14, 0.06, 0.62, 0.30,
        rough_detail=0.30, ao_depth=0.70),
    SurfaceSpec(
        "T_BathroomFloorTile", 0.74, 0.60, 0.88, 0.40,
        rough_detail=0.12, ao_depth=0.80),
    # Rooftop urethane membrane. A thick rubbery coat: diffuse, but not as dead
    # as concrete, and the roller laps are a soft thickness change rather than
    # cut relief, so the normal stays gentle.
    SurfaceSpec(
        "T_RooftopWaterproofing", 0.82, 0.70, 0.92, 0.45,
        rough_detail=0.14, ao_depth=0.70),
    # Powder coat on a door leaf. Flat is correct here: orange peel is a
    # sub-millimetre swell, and any more relief turns a maintained door into a
    # corroded one. Metallic stays 0 — the paint is what the light meets.
    # rough_detail is high for so flat a surface, and has to be: at 0.10 the
    # roughness map came back effectively constant and the art contract caught
    # it. A door with one uniform roughness reads as plastic under a moving
    # flashlight, which ASSET_STYLE forbids outright — and it is wrong anyway.
    # Powder coat collects dust in the orange-peel troughs and polishes where
    # hands pass, so the gloss genuinely varies even when the colour does not.
    SurfaceSpec(
        "T_UnitDoorPaintedSteel", 0.74, 0.62, 0.86, 0.30,
        rough_detail=0.26, ao_depth=0.50),
)


def _clamp(value: float, low: float = 0.0, high: float = 1.0) -> float:
    return min(high, max(low, value))


def _save_l(path: Path, values: bytearray, size: tuple[int, int]) -> None:
    Image.frombytes("L", size, bytes(values)).save(path, optimize=True)


def _generate(spec: SurfaceSpec, source_root: Path, force: bool) -> list[Path]:
    base_path = source_root / f"{spec.stem}_D.png"
    if not base_path.exists():
        raise FileNotFoundError(f"BaseColor source is missing: {base_path}")

    outputs = {
        "N": source_root / f"{spec.stem}_N.png",
        "R": source_root / f"{spec.stem}_R.png",
        "A": source_root / f"{spec.stem}_A.png",
    }
    if spec.wetness > 0.0:
        outputs["W"] = source_root / f"{spec.stem}_W.png"

    if not force and all(path.exists() and path.stat().st_mtime >= base_path.stat().st_mtime for path in outputs.values()):
        print(f"[PBR] up-to-date: {spec.stem}")
        return list(outputs.values())

    base = Image.open(base_path).convert("RGB")
    gray = base.convert("L")
    width, height = gray.size
    pixels = gray.tobytes()
    local_blur = gray.filter(ImageFilter.GaussianBlur(radius=3.0)).tobytes()
    broad_blur = gray.filter(ImageFilter.GaussianBlur(radius=18.0)).tobytes()
    mean_luma = ImageStat.Stat(gray).mean[0]

    normal = bytearray(width * height * 3)
    roughness = bytearray(width * height)
    occlusion = bytearray(width * height)
    wetness = bytearray(width * height) if spec.wetness > 0.0 else None

    for y in range(height):
        y_up = (y - 1) % height
        y_down = (y + 1) % height
        row = y * width
        up_row = y_up * width
        down_row = y_down * width
        for x in range(width):
            x_left = (x - 1) % width
            x_right = (x + 1) % width
            index = row + x
            value = pixels[index]

            dx = (pixels[row + x_right] - pixels[row + x_left]) / 255.0
            dy = (pixels[down_row + x] - pixels[up_row + x]) / 255.0
            nx = -dx * spec.normal_strength
            ny = -dy * spec.normal_strength
            inv_length = 1.0 / math.sqrt(nx * nx + ny * ny + 1.0)
            n_index = index * 3
            normal[n_index] = round((nx * inv_length * 0.5 + 0.5) * 255.0)
            normal[n_index + 1] = round((ny * inv_length * 0.5 + 0.5) * 255.0)
            normal[n_index + 2] = round((inv_length * 0.5 + 0.5) * 255.0)

            local_delta = (local_blur[index] - value) / 255.0
            micro_detail = abs(local_delta)
            tonal_bias = (127.5 - value) / 255.0
            rough = spec.roughness + micro_detail * spec.rough_detail + tonal_bias * 0.08
            roughness[index] = round(_clamp(rough, spec.roughness_min, spec.roughness_max) * 255.0)

            cavity = max(0.0, local_delta)
            ao = _clamp(1.0 - cavity * spec.ao_depth, 0.58, 1.0)
            occlusion[index] = round(ao * 255.0)

            if wetness is not None:
                # Wetness is intentionally low-frequency. Fine weave/rust grain
                # belongs in N/R and must not become glittering wet speckles.
                local_value = local_blur[index]
                broad_cavity = max(0.0, (broad_blur[index] - local_value) / 72.0)
                low_tone = max(0.0, (mean_luma - local_value) / 255.0)
                wet = _clamp(broad_cavity * 0.82 + low_tone * 0.38 - 0.035)
                wetness[index] = round(wet * 255.0)

    Image.frombytes("RGB", (width, height), bytes(normal)).save(outputs["N"], optimize=True)
    _save_l(outputs["R"], roughness, (width, height))
    _save_l(outputs["A"], occlusion, (width, height))
    if wetness is not None:
        wet_image = Image.frombytes("L", (width, height), bytes(wetness)).filter(ImageFilter.GaussianBlur(radius=8.0))
        wet_image = ImageOps.autocontrast(wet_image, cutoff=(4.0, 1.0))
        wet_image = wet_image.point(lambda value: round(value * spec.wetness))
        wet_image.save(outputs["W"], optimize=True)

    print(f"[PBR] generated: {spec.stem} ({width}x{height}, maps={','.join(outputs)})")
    return list(outputs.values())


def main() -> int:
    parser = argparse.ArgumentParser(description="Build PBR companion maps for AI material scans")
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1] / "Content" / "SourceArt")
    parser.add_argument("--only", action="append", default=[], help="Surface stem to generate (repeatable)")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    selected = [spec for spec in SURFACES if not args.only or spec.stem in args.only]
    unknown = sorted(set(args.only) - {spec.stem for spec in SURFACES})
    if unknown:
        parser.error(f"Unknown surface stem(s): {', '.join(unknown)}")

    generated: list[Path] = []
    for spec in selected:
        generated.extend(_generate(spec, args.source_root.resolve(), args.force))
    print(f"[PBR] ready: surfaces={len(selected)}, maps={len(generated)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
