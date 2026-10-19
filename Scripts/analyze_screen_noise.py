"""Run-ScreenNoiseReview.ps1가 남긴 프레임에서 화면 잡티를 원인별로 잰다.

    python Scripts/analyze_screen_noise.py <실행 폴더> [<고치기 전 실행 폴더>]

같은 자리, 같은 카메라에서 설정 하나씩만 바꾼 화면을 비교한다. 값은 모두 sRGB 밝기(0~255)다.

- speckle: 1~2픽셀 크기 점의 세기. 밝기에서 반지름 1픽셀로 흐린 밝기를 뺀 차이의 평균.
  벽·천장의 고정된 점무늬(구운 텍스처, 고주파 노멀, 디더)가 여기서 잡힌다.
- hf: 라플라시안 절댓값 평균. 가장자리와 점을 함께 센다.
- still_flicker: 카메라를 세운 채 이어진 두 프레임의 차이. 가만히 있어도 끓는 잡음(루멘 GI·반사의
  시간 누적, TAA 흔들림)이 여기서 잡힌다. p99는 끓는 점 하나하나의 세기다.
- pan_residual: 천천히 도는 화면에서 이웃 프레임을 화면 이동만큼 맞춘 뒤 남는 차이. 움직일 때
  반짝이는 점이 많을수록 크다.
- luma: 평균 밝기. 어둡게 해서 잡티가 줄어든 것처럼 보이는 경우를 가려낸다.

결과는 <실행 폴더>/analysis/summary.csv와 장면마다 같은 자리를 2배로 자른 비교판이다.
고치기 전 실행 폴더를 함께 주면 두 실행에 모두 있는 장면의 기본 설정 화면을 나란히 놓은
before_after_*.jpg와 before_after_<고치기 전 폴더 이름>.csv를 더 만든다.

마지막에 다시 찍은 기본 설정(base-again)이 처음 것과 밝기나 점 세기가 3% 넘게 다르면 그 장면은
노출이 덜 맞은 채 찍힌 것이다. UNSETTLED로 따로 알린다.
"""
from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

VARIANT_ORDER = [
    "base", "grain-quantization-off", "texture-mip-bias", "specular-off", "lumen-gi-off",
    "lumen-reflections-off", "aa-fxaa", "aa-tsr", "aa-off", "base-again",
]


def luma(path: Path) -> np.ndarray:
    with Image.open(path) as image:
        rgb = np.asarray(image.convert("RGB"), dtype=np.float32)
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def interior(values: np.ndarray, margin: int = 4) -> np.ndarray:
    return values[margin:-margin, margin:-margin]


def speckle(y: np.ndarray) -> float:
    return float(np.mean(np.abs(interior(y - ndimage.gaussian_filter(y, 1.0)))))


def high_frequency(y: np.ndarray) -> float:
    return float(np.mean(np.abs(interior(ndimage.laplace(y)))))


def flicker(a: np.ndarray, b: np.ndarray) -> tuple[float, float]:
    difference = np.abs(interior(b - a))
    return float(np.mean(difference)), float(np.percentile(difference, 99))


def shift_between(a: np.ndarray, b: np.ndarray) -> tuple[float, float]:
    """b가 a에서 얼마나 옮겨 갔는지(행, 열). 위상 상관으로 정수 이동을 찾고 꼭짓점 둘레로 소수점까지."""
    window = np.outer(np.hanning(a.shape[0]), np.hanning(a.shape[1]))
    fa = np.fft.fft2(a * window)
    fb = np.fft.fft2(b * window)
    cross = fb * np.conj(fa)
    cross /= np.maximum(np.abs(cross), 1e-6)
    surface = np.real(np.fft.ifft2(cross))
    peak = np.unravel_index(np.argmax(surface), surface.shape)
    shifts = []
    for axis, size in enumerate(surface.shape):
        index = peak[axis]
        before = surface[tuple((index - 1) % size if i == axis else peak[i] for i in range(2))]
        after = surface[tuple((index + 1) % size if i == axis else peak[i] for i in range(2))]
        centre = surface[peak]
        denominator = before - 2.0 * centre + after
        offset = 0.5 * (before - after) / denominator if abs(denominator) > 1e-9 else 0.0
        value = index + offset
        if value > size / 2:
            value -= size
        shifts.append(float(value))
    return shifts[0], shifts[1]


def pan_residual(frames: list[np.ndarray]) -> float:
    residuals = []
    for a, b in zip(frames, frames[1:]):
        dy, dx = shift_between(a, b)
        moved = ndimage.shift(a, (dy, dx), order=1, mode="nearest")
        margin = int(np.ceil(max(abs(dx), abs(dy)))) + 6
        residuals.append(float(np.mean(np.abs((b - moved)[margin:-margin, margin:-margin]))))
    return float(np.mean(residuals)) if residuals else float("nan")


def contact_sheet(folder: Path, variants: list[str], out: Path, title: str) -> None:
    images = []
    for variant in variants:
        path = folder / f"{variant}_still0.png"
        if path.is_file():
            images.append((variant, Image.open(path).convert("RGB")))
    if not images:
        return
    width, height = images[0][1].size
    # 화면 가운데 4분의 1을 두 배로 키운다. 1~2픽셀 점이 보이는 크기다.
    crop_w, crop_h = width // 4, height // 4
    box = ((width - crop_w) // 2, (height - crop_h) // 2 + height // 10)
    box = (box[0], box[1], box[0] + crop_w, box[1] + crop_h)
    tile = (crop_w * 2, crop_h * 2)
    columns = 3
    rows = (len(images) + columns - 1) // columns
    label_h = 26
    sheet = Image.new("RGB", (tile[0] * columns, (tile[1] + label_h) * rows + 34), (18, 18, 18))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("C:/Windows/Fonts/malgun.ttf", 16)
    except OSError:
        font = ImageFont.load_default()
    draw.text((8, 8), title, fill=(230, 228, 220), font=font)
    for index, (variant, image) in enumerate(images):
        x = (index % columns) * tile[0]
        y = 34 + (index // columns) * (tile[1] + label_h)
        sheet.paste(image.crop(box).resize(tile, Image.Resampling.NEAREST), (x, y + label_h))
        draw.text((x + 6, y + 4), variant, fill=(230, 228, 220), font=font)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out, quality=90)


def flicker_map(folder: Path, variant: str, out: Path) -> None:
    a, b = folder / f"{variant}_still0.png", folder / f"{variant}_still1.png"
    if not (a.is_file() and b.is_file()):
        return
    difference = np.clip(np.abs(luma(b) - luma(a)) * 8.0, 0, 255).astype(np.uint8)
    Image.fromarray(difference).save(out, quality=88)


def main(run: Path, before: Path | None = None) -> int:
    rows = []
    analysis = run / "analysis"
    for resolution in sorted(p for p in run.iterdir() if p.is_dir() and "x" in p.name and p.name[0].isdigit()):
        for view in sorted(p for p in resolution.iterdir() if p.is_dir()):
            variants = [v for v in VARIANT_ORDER if (view / f"{v}_still0.png").is_file()]
            for variant in variants:
                still0 = luma(view / f"{variant}_still0.png")
                still1_path = view / f"{variant}_still1.png"
                still1 = luma(still1_path) if still1_path.is_file() else None
                pans = sorted(view.glob(f"{variant}_pan*.png"))
                row = {
                    "resolution": resolution.name, "view": view.name, "variant": variant,
                    "luma": round(float(np.mean(still0)), 2),
                    "speckle": round(speckle(still0), 3),
                    "hf": round(high_frequency(still0), 3),
                }
                if still1 is not None:
                    mean, p99 = flicker(still0, still1)
                    row["still_flicker"] = round(mean, 3)
                    row["still_flicker_p99"] = round(p99, 2)
                if len(pans) >= 2:
                    row["pan_residual"] = round(pan_residual([luma(p) for p in pans]), 3)
                rows.append(row)
            contact_sheet(view, variants, analysis / f"{resolution.name}_{view.name}.jpg",
                          f"{resolution.name} · {view.name} · 정지 화면 가운데를 2배로")
            for variant in ("base", "lumen-gi-off", "aa-tsr"):
                flicker_map(view, variant, analysis / f"{resolution.name}_{view.name}_flicker_{variant}.jpg")
    if not rows:
        print("SCREEN_NOISE_ANALYSIS FAIL no frames")
        return 1
    analysis.mkdir(parents=True, exist_ok=True)
    fields = ["resolution", "view", "variant", "luma", "speckle", "hf", "still_flicker", "still_flicker_p99", "pan_residual"]
    with open(analysis / "summary.csv", "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    # 장면마다 기본값 대비 비율. 1보다 크게 줄어든 항목이 그 장면 잡티의 원인 쪽이다.
    ratios = {}
    for row in rows:
        base = next((r for r in rows if r["resolution"] == row["resolution"] and r["view"] == row["view"]
                     and r["variant"] == "base"), None)
        if not base or row["variant"] == "base":
            continue
        key = f"{row['resolution']}/{row['view']}/{row['variant']}"
        ratios[key] = {name: round(row[name] / base[name], 3) for name in ("speckle", "still_flicker", "pan_residual", "luma")
                       if name in row and name in base and base[name]}
    (analysis / "ratios.json").write_text(json.dumps(ratios, indent=1, ensure_ascii=False), encoding="utf-8")
    unsettled = 0
    for key, ratio in ratios.items():
        if key.endswith("/base-again") and any(abs(ratio.get(name, 1.0) - 1.0) > 0.03 for name in ("luma", "speckle")):
            unsettled += 1
            print(f"SCREEN_NOISE_ANALYSIS UNSETTLED {key.rsplit('/', 1)[0]} {ratio}")
    if before is not None:
        compare_runs(before, run, rows, analysis)
    print(f"SCREEN_NOISE_ANALYSIS PASS rows={len(rows)} unsettled={unsettled} out={analysis}")
    return 0


def compare_runs(before: Path, run: Path, rows: list[dict], analysis: Path) -> None:
    """같은 카메라로 찍은 고치기 전과 뒤의 기본 설정 화면을 나란히 놓는다."""
    before_rows = {}
    before_csv = before / "analysis" / "summary.csv"
    if before_csv.is_file():
        with open(before_csv, encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                if row["variant"] == "base":
                    before_rows[(row["resolution"], row["view"])] = row
    out_rows = []
    try:
        font = ImageFont.truetype("C:/Windows/Fonts/malgun.ttf", 16)
    except OSError:
        font = ImageFont.load_default()
    for row in rows:
        if row["variant"] != "base":
            continue
        key = (row["resolution"], row["view"])
        old_path = before / row["resolution"] / row["view"] / "base_still0.png"
        new_path = run / row["resolution"] / row["view"] / "base_still0.png"
        if not old_path.is_file():
            continue
        old_row = before_rows.get(key)
        if old_row:
            entry = {"resolution": key[0], "view": key[1]}
            for name in ("luma", "speckle", "still_flicker", "pan_residual"):
                if old_row.get(name) and name in row:
                    entry[f"{name}_before"] = float(old_row[name])
                    entry[f"{name}_after"] = row[name]
                    entry[f"{name}_ratio"] = round(row[name] / float(old_row[name]), 3) if float(old_row[name]) else ""
            out_rows.append(entry)
        old_image = Image.open(old_path).convert("RGB")
        new_image = Image.open(new_path).convert("RGB")
        width, height = new_image.size
        half = (width // 2, height // 2)
        crop_w, crop_h = width // 4, height // 4
        left, top = (width - crop_w) // 2, (height - crop_h) // 2 + height // 10
        box = (left, top, left + crop_w, top + crop_h)
        sheet = Image.new("RGB", (half[0] * 2, half[1] * 2 + 34), (18, 18, 18))
        draw = ImageDraw.Draw(sheet)
        draw.text((8, 8), f"{key[0]} · {key[1]} · 왼쪽 고치기 전, 오른쪽 고친 뒤 (아래는 가운데를 2배로)", fill=(230, 228, 220), font=font)
        for column, image in enumerate((old_image, new_image)):
            sheet.paste(image.resize(half, Image.Resampling.LANCZOS), (column * half[0], 34))
            sheet.paste(image.crop(box).resize(half, Image.Resampling.NEAREST), (column * half[0], 34 + half[1]))
        sheet.save(analysis / f"before_after_{key[0]}_{key[1]}.jpg", quality=90)
    if out_rows:
        fields = sorted({name for entry in out_rows for name in entry}, key=lambda name: (name not in ("resolution", "view"), name))
        with open(analysis / f"before_after_{before.name}.csv", "w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()
            writer.writerows(out_rows)
        for entry in out_rows:
            print("SCREEN_NOISE_COMPARE " + " ".join(f"{name}={entry[name]}" for name in entry))


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]), Path(sys.argv[2]) if len(sys.argv) > 2 else None))
