"""반복 재질 텍스처의 1~2픽셀짜리 고운 점만 누그러뜨린다.

    python Scripts/soften_fine_detail.py --retail
    python Scripts/soften_fine_detail.py <png> --radius 2.0 --keep 0.4 [--contrast 0.7] [--out <png>]

화면에서 1~3픽셀로 찍히는 알갱이는 가만히 있어도 자글거리고, 움직이면 TAA가 다 거두지 못해
반짝인다. 반지름 radius로 흐린 바탕은 그대로 두고 그보다 고운 성분만 keep배로 줄인 뒤, 필요하면
무늬 전체의 대비를 contrast배로 낮춘다. 평균 색은 그대로다. 흐림은 가장자리를 감아 돌려(wrap)
반복 경계가 생기지 않는다. 알파는 건드리지 않는다.

--retail은 공용부 마감 재질(retail_surface_contract.floor_albedo)이 생성 원본 대신 읽는 파생본을
Content/SourceArt/Retail에 다시 만든다. 원본(SourceArt/AI)은 바꾸지 않는다. 화면 잡티 검토
(Run-ScreenNoiseReview.ps1, 2026-10-19)에서 원인으로 확인된 텍스처만 RETAIL에 둔다.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]
RETAIL_SIZE = 1024
# (원본 이름, 반지름, keep, 대비)
RETAIL = (
    # 복도·계단의 600 mm 포천석. 검은 운모 알갱이가 화면에서 1~3픽셀로 찍혔다. 밉 바이어스만
    # 1.5 줘도 손전등으로 비춘 바닥의 점 세기가 절반 아래로, 돌아볼 때 남는 반짝임이 3분의 1 줄었다.
    # 0.4·0.7로는 점 세기가 34% 줄고도 2배로 보면 여전히 촘촘한 점무늬였다.
    ("PocheonGranite_20260915.png", 2.0, 0.35, 0.5),
)


def soften(path: Path, radius: float, keep: float, out: Path | None = None, contrast: float = 1.0) -> tuple[float, float]:
    image = Image.open(path)
    mode = image.mode
    rgba = np.asarray(image.convert("RGBA"), dtype=np.float32)
    colour = rgba[..., :3]
    low = np.stack([ndimage.gaussian_filter(colour[..., c], radius, mode="wrap") for c in range(3)], axis=-1)
    softened = low + keep * (colour - low)
    # 큰 알갱이까지 화면에서 1~3픽셀이면 무늬 전체의 대비도 낮춘다. 평균 밝기는 그대로 둔다.
    if contrast != 1.0:
        mean = softened.reshape(-1, 3).mean(axis=0)
        softened = mean + contrast * (softened - mean)

    def fine(values: np.ndarray) -> float:
        luma = values @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
        return float(np.mean(np.abs(luma - ndimage.gaussian_filter(luma, 1.0, mode="wrap"))))

    before, after = fine(colour), fine(softened)
    rgba[..., :3] = np.clip(np.rint(softened), 0, 255)
    result = Image.fromarray(rgba.astype(np.uint8), "RGBA")
    if mode != "RGBA":
        result = result.convert(mode if mode in ("RGB", "L") else "RGB")
    result.save(out or path)
    return before, after


def build_retail() -> None:
    source_dir = ROOT / "Content" / "SourceArt" / "AI"
    out_dir = ROOT / "Content" / "SourceArt" / "Retail"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, radius, keep, contrast in RETAIL:
        target = out_dir / name
        # 재질은 1K까지만 쓴다. 엔진이 늘였다 줄이기 전에 1024로 맞춰 두어야 반지름이 실제 텍셀 기준이 된다.
        with Image.open(source_dir / name) as image:
            image.convert("RGB").resize((RETAIL_SIZE, RETAIL_SIZE), Image.Resampling.LANCZOS).save(target)
        before, after = soften(target, radius, keep, contrast=contrast)
        print(f"SOFTEN {name} radius={radius} keep={keep} contrast={contrast} fine {before:.2f} -> {after:.2f}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path, nargs="?")
    parser.add_argument("--retail", action="store_true")
    parser.add_argument("--radius", type=float)
    parser.add_argument("--keep", type=float)
    parser.add_argument("--contrast", type=float, default=1.0)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if args.retail:
        build_retail()
        return 0
    if args.path is None or args.radius is None or args.keep is None:
        parser.error("파일 하나를 다듬을 때는 path, --radius, --keep이 모두 필요하다")
    before, after = soften(args.path, args.radius, args.keep, args.out, args.contrast)
    print(f"SOFTEN {args.path.name} radius={args.radius} keep={args.keep} contrast={args.contrast} fine {before:.2f} -> {after:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
