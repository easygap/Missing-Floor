"""현재 빌드에서 찍은 화면으로 소개용 GIF를 만든다."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageOps


PROJECT_ROOT = Path(__file__).resolve().parents[1]
MEDIA_ROOT = PROJECT_ROOT / "Docs" / "Media"
OUTPUT_PATH = MEDIA_ROOT / "readme-route-preview.gif"
FRAME_SIZE = (768, 432)
PALETTE_COLORS = 160

CAPTURES = (
    "game-bedroom.png",
    "game-corridor-day.png",
    "game-alley.png",
    "game-store.png",
    "game-booth.png",
    "game-bedroom-dawn.png",
    "game-alley-dawn.png",
)


def load_capture(filename: str) -> Image.Image:
    source_path = MEDIA_ROOT / filename
    if not source_path.is_file():
        raise FileNotFoundError(f"README capture is missing: {source_path}")

    with Image.open(source_path) as source:
        return ImageOps.fit(
            source.convert("RGB"),
            FRAME_SIZE,
            method=Image.Resampling.LANCZOS,
        )


def build_global_palette(frames: list[Image.Image]) -> Image.Image:
    samples = [frame.resize((192, 108), Image.Resampling.LANCZOS) for frame in frames]
    atlas = Image.new("RGB", (192, 108 * len(samples)))
    for index, sample in enumerate(samples):
        atlas.paste(sample, (0, index * 108))
    return atlas.quantize(colors=PALETTE_COLORS, method=Image.Quantize.MEDIANCUT)


def build_route_preview() -> int:
    captures = [load_capture(filename) for filename in CAPTURES]
    palette = build_global_palette(captures)

    rgb_frames: list[Image.Image] = []
    durations: list[int] = []
    for index, current in enumerate(captures):
        rgb_frames.append(current)
        durations.append(1200)

        if index == len(captures) - 1:
            continue

        following = captures[index + 1]
        for step in range(1, 5):
            rgb_frames.append(Image.blend(current, following, step / 5.0))
            durations.append(80)

    gif_frames = [
        frame.quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG)
        for frame in rgb_frames
    ]
    gif_frames[0].save(
        OUTPUT_PATH,
        save_all=True,
        append_images=gif_frames[1:],
        duration=durations,
        loop=0,
        optimize=True,
        disposal=2,
        comment=b"Actual in-game capture route preview",
    )
    return len(gif_frames)


def build_chase() -> None:
    # 추격 GIF는 예고편과 같은 촬영분의 연속 프레임을 쓴다. 4층 참에 귀를 댄 몸, 복도
    # 끝에서 기어 와 발밑에서 붙잡고, 바닥 빛 속에 얼굴을 들이민 채 듣다가 끊기기까지
    # (listener-approach 0~116, 끊김은 111). 화면은 흔들리지 않는다.
    motion = []
    for shot, start, end in (('stair-landing', 0, 60), ('listener-approach', 0, 117)):
        for index in range(start, end, 3):
            with Image.open(PROJECT_ROOT / 'Saved/Trailer' / shot / f'frame_{index:04d}.png') as source:
                motion.append(source.convert('RGB').resize(FRAME_SIZE, Image.Resampling.LANCZOS))
    motion_palette = build_global_palette(motion[::5])
    motion = [frame.quantize(palette=motion_palette, dither=Image.Dither.FLOYDSTEINBERG) for frame in motion]
    motion[0].save(MEDIA_ROOT / 'night-listener-chase.gif', save_all=True, append_images=motion[1:],
                   duration=100, loop=0, optimize=True, disposal=2)


def main() -> None:
    # --chase면 추격 GIF만 다시 만든다. 낮 동선 GIF는 README 캡처가 바뀔 때만 만든다.
    import sys
    if '--chase' not in sys.argv:
        frames = build_route_preview()
        print(f"README_MEDIA PASS captures={len(CAPTURES)} frames={frames} output={OUTPUT_PATH}")
    build_chase()
    print("README_MEDIA PASS chase=night-listener-chase.gif")


if __name__ == "__main__":
    main()
