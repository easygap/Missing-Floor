"""나린 정면 시안의 두 팔을 몸에서 22도 벌린 입력 그림을 만든다. 3D 생성용이다.

    python Scripts/make_narin_arms_out.py <out.png>

원본 시안(SourceArt/AI/NarinFront_20260914.png)은 팔이 조끼 옆선과 허벅지에 붙어 늘어져 있어서, 그
그림으로 뽑은 메시는 팔 안쪽과 손이 몸에 한 면으로 붙었다. 그러면 팔을 들 때 바지와 조끼가 막처럼
딸려 나온다(2026-10-19). 소매(밝은 회색)와 손(살색)을 색으로 골라 어깨 아래를 떼어 내고, 어깨 위
축으로 바깥쪽으로 22도 돌려 다시 얹는다. 비운 자리는 둘레의 배경만 골라 넓게 흐린 값으로 메운다.
원본 그림은 바꾸지 않는다.
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Content" / "SourceArt" / "AI" / "NarinFront_20260914.png"
# (쪽, 팔이 있는 x 범위, 어깨 축, 도는 각도). 그림 왼쪽이 그녀의 오른팔이다.
ARMS = (
    ("R", (255, 372), (338, 334), -22.0),
    ("L", (648, 772), (686, 334), 22.0),
)
SHOULDER_CUT = 338
ARM_BOTTOM = 900


def main(out: Path) -> int:
    source = Image.open(SOURCE).convert("RGB")
    image = np.asarray(source).astype(np.float32)
    height, width = image.shape[:2]
    red, green, blue = image[..., 0], image[..., 1], image[..., 2]
    luma = 0.2126 * red + 0.7152 * green + 0.0722 * blue
    top, low = image.max(axis=2), image.min(axis=2)
    saturation = (top - low) / np.maximum(top, 1.0)
    sleeve = (luma > 150) & (saturation < 0.12)
    skin = (red > 110) & (red > green + 8) & (saturation > 0.08)
    rows, cols = np.mgrid[0:height, 0:width]

    # 조끼(어두운 초록)와 바지(검정)는 남긴다. 팔 자리에서 이 둘이 아닌 것은 소매 가장자리의 회색 테까지
    # 모두 비운다. 색으로 고른 팔 마스크만 지우면 조끼 옆선에 소매 테가 톱니처럼 남았다.
    vest = (green > red + 3.0) & (luma < 110.0)
    pants = (luma < 70.0) & (saturation < 0.25)
    arm_masks = []
    clear_regions = []
    for side, (x0, x1), _, _ in ARMS:
        box = (cols >= x0) & (cols < x1) & (rows >= SHOULDER_CUT) & (rows < ARM_BOTTOM)
        # 조끼 밑단 아래로 보이는 맨투맨 밑단은 팔이 아니다.
        box &= ~((rows > 745) & ((cols > 346) if side == "R" else (cols < 652)))
        mask = ndimage.binary_closing(box & (sleeve | skin), iterations=3)
        mask = ndimage.binary_fill_holes(mask)
        labels, count = ndimage.label(mask)
        sizes = ndimage.sum(mask, labels, range(1, count + 1))
        mask = labels == (int(np.argmax(sizes)) + 1)
        arm_masks.append(ndimage.binary_dilation(mask, iterations=2))
        clear_regions.append(box & ~(ndimage.binary_erosion(vest, iterations=1) | ndimage.binary_erosion(pants, iterations=1)))

    # 비운 자리: 가장 가까운 배경 픽셀의 색을 끌어와 조금 흐린다. 몸 색이 번져 들지 않는다.
    # (넓게 흐린 값으로 나누면 배경에서 먼 가운데는 무게가 0에 가까워 검게 남았다.)
    everything = np.zeros((height, width), bool)
    for mask in arm_masks + clear_regions:
        everything |= mask
    background = (np.linalg.norm(image - np.array([128.0, 127.0, 132.0]), axis=2) < 18.0) & ~everything
    _, (near_rows, near_cols) = ndimage.distance_transform_edt(~background, return_indices=True)
    filled = image[near_rows, near_cols]
    filled = np.stack([ndimage.gaussian_filter(filled[..., c], 6.0) for c in range(3)], axis=-1)
    grain = np.random.default_rng(7).normal(0.0, 1.2, size=image.shape).astype(np.float32)
    result = image.copy()
    result[everything] = (filled + grain)[everything]

    canvas = Image.fromarray(np.clip(result, 0, 255).astype(np.uint8)).convert("RGBA")
    for mask, (_, _, pivot, angle) in zip(arm_masks, ARMS):
        alpha = Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))
        piece = source.copy()
        piece.putalpha(alpha)
        canvas = Image.alpha_composite(canvas, piece.rotate(angle, resample=Image.BICUBIC, center=pivot))
    out.parent.mkdir(parents=True, exist_ok=True)
    canvas.convert("RGB").save(out)
    print(f"NARIN_ARMS_OUT {out} masks={[int(m.sum()) for m in arm_masks]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(Path(sys.argv[1])))
