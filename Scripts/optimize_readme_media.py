# -*- coding: utf-8 -*-
"""README에 인라인으로 걸리는 캡처의 표시용 파생본을 만든다.

원본은 건드리지 않는다. Docs/Media의 1080p 캡처는 접근성 설정과 대화 HUD의
증거이고 계약이 그 경로를 고정하고 있어서, 해상도를 깎으면 증거가 아니게 된다.
그래서 표시용으로만 Docs/Media/readme/에 축소본을 따로 만든다.

왜 필요한가: 원본을 그대로 인라인하면 README를 여는 순간 37 MB를 받는다.
GitHub는 이미지를 camo로 프록시하므로 그 무게가 그대로 첫 화면 지연이 된다.
어두운 게임 화면은 JPEG에서 계조가 뭉치므로 WebP를 쓴다.

    python Scripts/optimize_readme_media.py
"""

import os
import shutil
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MEDIA = os.path.join(ROOT, "Docs", "Media")
OUT = os.path.join(MEDIA, "readme")

# 인라인으로 걸리는 무거운 캡처만 줄인다. 접힌 <details> 안이나 이미 작은
# 파일은 그대로 쓴다 — 파생본이 하나 늘 때마다 확인해야 할 것도 하나 늘어난다.
STILLS = (
    "settings-caption-200-20260930-en.png",
    "settings-audio-20260930-en.png",
    "settings-accessibility-20260929.png",
    # 번역 README는 그 언어로 켠 화면을 건다.
    "settings-accessibility-20260929-en.png",
    "settings-accessibility-20260929-ja.png",
    "settings-accessibility-20260929-zh-Hans.png",
    "settings-accessibility-20260929-zh-Hant.png",
    "title-menu-first-run-1080-en.png",
    "title-menu-first-run-1080-ja.png",
    "title-menu-first-run-1080-zh-Hans.png",
    "title-menu-first-run-1080-zh-Hant.png",
    "settings-accessibility-20260922.png",
    "settings-controls-20260922.png",
    "settings-audio-20260922.png",
    "game-bedroom.png",
    "game-corridor-day.png",
    "game-alley.png",
    "game-store.png",
    "game-corridor-night.png",
    "game-booth.png",
    "game-bedroom-dawn.png",
    "game-alley-dawn.png",
    # 검수 기록도 본문에 전후 비교 캡처를 여러 장 건다. 원본을 그대로 걸면
    # 문서 하나를 여는 데 수십 MB가 들어서 같이 줄인다.
    "bedroom-before-bed.png",
    "bedroom-after-bed.png",
    "bedroom-before-desk.png",
    "bedroom-after-desk.png",
    "cloth-before-vest-installed.png",
    "cloth-after-vest-installed.png",
    "cloth-before-vest-angle.png",
    "cloth-after-vest-angle.png",
    "surface-before-capture.png",
    "surface-after-capture.png",
    "reading-label-1920x1080-1.png",
    "reading-journal-1280x720-2.png",
    "circuit-before-front.png",
    "circuit-front.png",
    "circuit-wide.png",
    "circuit-before-corridor-front.png",
    "circuit-corridor-front.png",
    "booth-before-desk.png",
    "booth-pad-blank.png",
    "booth-pad-partial.png",
    "booth-pad-complete.png",
    "booth-night-desk-return.png",
    "title-menu-first-run-1080.png",
    "p1-meter-cabinet.png",
    # 계단참 목격 컷은 뺐다. 원본부터 거의 검은 화면이라 GitHub에서는 빈
    # 사각형으로 보인다. 게임 안에서 통하는 어둠이 문서에서도 통하지는 않는다.
    # 설정 메뉴·보정 화면·밤 4 스포일러도 README에는 걸지 않는다. 원본은 각자의
    # 계약이 Docs/Media에 증거로 잡고 있고, 표시용 파생본만 여기서 빠진다.
)

# 번역 README에 거는 플레이 화면. Run-ReadmeCapture.ps1 -Culture ja처럼 그 언어로
# 켜고 찍은 것만 있다. 아직 찍지 않은 언어는 건너뛴다. 그 전까지 번역 README는
# 자막과 조작 안내가 없는 화면만 건다.
LOCALIZED_CULTURES = ("en", "ja", "zh-Hans", "zh-Hant")
LOCALIZED_SHOTS = (
    "game-bedroom", "game-corridor-day", "game-alley", "game-store",
    "game-corridor-night", "game-booth", "game-bedroom-dawn", "game-alley-dawn",
)


def localized_stills():
    for culture in LOCALIZED_CULTURES:
        for shot in LOCALIZED_SHOTS:
            name = "%s-%s.png" % (shot, culture)
            if os.path.exists(os.path.join(MEDIA, name)):
                yield name


# GIF는 현재 빌드의 화면을 사용한다. 원본의 화면 비율을 유지한다.
# 애니메이션 WebP는 브라우저마다 첫 프레임만 보이는 경우가 있어 GIF로 남긴다.
ANIMATIONS = (
    # 추격 컷은 README에서 가장 무거운 파일 하나다. 720·12fps에서 4.5 MB였고
    # 그것만으로 첫 화면 무게의 절반을 넘겼다. 640·10fps면 절반 아래로 떨어지고,
    # GitHub 본문 폭에서는 차이가 눈에 띄지 않는다.
    ("night-listener-chase.gif", 560, 8),
    # 기상 잔향은 README에서 뺐지만 파생본 자체는 M1 기상 잔향 계약이
    # 파일로 잡고 있어 계속 만든다.
    ("m1-capture-wake-echo.gif", 640, 12),
    # 낮 동선은 본문에 걸지 않고 링크로만 준다. 누른 사람만 받는다.
    ("readme-route-preview.gif", 640, 10),
)

MAX_WIDTH = 1600
QUALITY = 86


def optimize_still(name):
    source = os.path.join(MEDIA, name)
    target = os.path.join(OUT, os.path.splitext(name)[0] + ".webp")
    with Image.open(source) as image:
        frame = image.convert("RGB")
        if frame.width > MAX_WIDTH:
            height = round(frame.height * MAX_WIDTH / frame.width)
            frame = frame.resize((MAX_WIDTH, height), Image.LANCZOS)
        frame.save(target, "WEBP", quality=QUALITY, method=6)
    return source, target


def optimize_animation(name, width, fps):
    source = os.path.join(MEDIA, name)
    target = os.path.join(OUT, name)
    palette = os.path.join(OUT, "_palette.png")
    chain = "fps={0},scale={1}:-1:flags=lanczos".format(fps, width)
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-i", source,
         "-vf", chain + ",palettegen=max_colors=96:stats_mode=diff", palette],
        check=True)
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-i", source, "-i", palette,
         "-lavfi", chain + " [x]; [x][1:v] paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle",
         target],
        check=True)
    os.remove(palette)
    return source, target


def main():
    if not shutil.which("ffmpeg"):
        print("ffmpeg를 찾을 수 없습니다. GIF 축소를 건너뜁니다.", file=sys.stderr)
    os.makedirs(OUT, exist_ok=True)
    before = after = 0
    for name in STILLS + tuple(localized_stills()):
        source, target = optimize_still(name)
        source_size, target_size = os.path.getsize(source), os.path.getsize(target)
        before += source_size
        after += target_size
        print("%-44s %7.2f MB -> %6.2f MB" % (
            name, source_size / 1048576.0, target_size / 1048576.0))
        if target_size >= source_size:
            raise SystemExit("파생본이 원본보다 크다: %s" % name)
    if shutil.which("ffmpeg"):
        for name, width, fps in ANIMATIONS:
            source, target = optimize_animation(name, width, fps)
            source_size, target_size = os.path.getsize(source), os.path.getsize(target)
            before += source_size
            after += target_size
            print("%-44s %7.2f MB -> %6.2f MB" % (
                name, source_size / 1048576.0, target_size / 1048576.0))
            if target_size >= source_size:
                raise SystemExit("파생본이 원본보다 크다: %s" % name)
    print("-" * 62)
    print("%-44s %7.2f MB -> %6.2f MB" % (
        "합계", before / 1048576.0, after / 1048576.0))


if __name__ == "__main__":
    main()
