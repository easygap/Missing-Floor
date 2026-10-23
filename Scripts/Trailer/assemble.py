"""실제 게임에서 30fps로 찍은 프레임을 이어 붙여 예고편을 만든다.

첫 화면부터 게임이다(2026-10-08 다시 엮음). 어두운 방에서 천장을 올려다보는 장면에
천장의 노크와 그 자막이 같이 온다. 소리 없이 자동 재생돼도 자막이 소리를 알려 준다.
낮의 빌라를 한 사람의 걸음으로 보여 주고, 새벽의 현관 너머 아는 목소리, 불 꺼진 복도의
어둑시니, 기어 오는 위층 사람으로 간다. 붙잡히는 장면은 한 번에 찍힌 그대로 쓰고 바닥
노크에 끊는다. 마지막은 제목과 받는 곳이다. 카메라를 흔들지 않고, 색 보정과 입자는 따로
얹지 않는다. 화면 질감은 게임 자체의 것이다.

    python Scripts/Trailer/assemble.py --marks          # 편집표(edl.json)만 쓴다
    python Scripts/Trailer/assemble.py ko               # MissingFloor-Trailer.mp4
    python Scripts/Trailer/assemble.py en               # MissingFloor-Trailer-en.mp4
    python Scripts/Trailer/assemble.py --still 12.0 ko  # 한 장만 확인
"""
from pathlib import Path
from functools import lru_cache
import json
import os
import subprocess
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
# 예고편 만들기(Build-Trailer.ps1)는 실행 폴더를 IG_TRAILER_DIR로 넘긴다. 따로 돌릴 때도 쓸 실행 폴더를
# 지정한다. 예전에는 비워 두면 Saved/Trailer 한 곳에 덮어써서 어느 촬영의 결과인지 알 수 없었다.
if not os.environ.get('IG_TRAILER_DIR'):
    sys.exit('IG_TRAILER_DIR에 예고편 실행 폴더(Saved/Trailer/Runs/<시각>)를 지정해 주세요.')
HERE = Path(os.environ['IG_TRAILER_DIR'])
FPS = 30
SIZE = (1920, 1080)
# 기어 오는 장면은 붙잡히고 끊기기까지 한 번에 찍힌다. 끊긴 뒤 다섯 프레임까지 쓴다.
APPROACH_FIRST = 0
APPROACH_GRAB = 67
APPROACH_CUT = 111
APPROACH_SECONDS = 3.9
# (시작 초, 길이 초, 촬영 폴더, 첫 프레임). 빈 폴더명은 암전이다.
SEGMENTS = [
    (0.0, 6.0, 'bedroom-night', 0),
    (6.0, 0.6, '', 0),
    (6.6, 4.6, 'alley', 5),
    (11.2, 5.6, 'corridor-day', 0),
    (16.8, 4.0, 'door-402', 0),
    (20.8, 3.6, 'booth-cctv', 10),
    (24.4, 0.6, '', 0),
    (25.0, 7.0, 'door-403-night', 0),
    (32.0, 5.0, 'corridor-night', 6),
    (37.0, 6.0, 'eoduksini-corridor', 0),
    (43.0, 0.6, '', 0),
    (43.6, 3.6, 'stair-landing', 0),
    (47.2, APPROACH_SECONDS, 'listener-approach', APPROACH_FIRST),
    (47.2 + APPROACH_SECONDS, 53.6 - 47.2 - APPROACH_SECONDS, '', 0),
    (53.6, 8.4, '', 0),
]
# 장면별 고정 화각(배율, 중심 x, 중심 y). 어둑시니는 복도 끝 등 하나를 등지고 서서 원래 화각으로는
# 작은 화면에서 몇 픽셀밖에 안 된다. 1.6배로 잘라 망원으로 찍은 것처럼 쓴다. 움직이지는 않는다.
FRAMING = {'eoduksini-corridor': (1.6, 960, 500)}
# (글자 판, 시작, 끝). 두 언어가 같은 자리를 쓴다.
CARDS = [('knock', 0.8, 3.6), ('topfloor', 3.7, 6.0), ('door', 27.0, 31.6), ('title', 53.6, 62.0)]
LENGTH = 62.0
# 붙잡힘(grab)은 시점이 끌려 내려가기 시작하는 프레임, 끊김(cut)은 얼굴이 멈춰 듣다가
# 검게 끊기는 프레임이다. listener-approach의 APPROACH_GRAB·APPROACH_CUT 프레임에서 잰다.
MARKS = {
    'first_knock': 0.6, 'creak': 3.4, 'alley': 6.6, 'day': 11.2, 'door402': 16.8, 'booth': 20.8,
    'door403': 25.0, 'door_knock': 25.7, 'door_voice': 27.0, 'night': 32.0, 'eoduksini': 37.0,
    'eoduksini_end': 43.0, 'stair': 43.6, 'approach': 47.2,
    'grab': 47.2 + (APPROACH_GRAB - APPROACH_FIRST) / 30.0,
    'cut': 47.2 + (APPROACH_CUT - APPROACH_FIRST) / 30.0,
    'title': 53.6, 'answer': 56.2,
}
OUTPUT = {'ko': 'MissingFloor-Trailer.mp4', 'en': 'MissingFloor-Trailer-en.mp4'}


def validate_frames():
    previous_end = 0
    for start, length, shot, first in SEGMENTS:
        if abs(start - previous_end) > 0.001:
            raise ValueError(f'편집 구간이 이어지지 않습니다: {start}')
        previous_end = start + length
        if not shot:
            continue
        for n in range(first, first + round(length * FPS)):
            if not (HERE / shot / f'frame_{n:04d}.png').is_file():
                raise FileNotFoundError(f'촬영 프레임이 없습니다: {shot}/{n}')
    if abs(previous_end - LENGTH) > 0.001:
        raise ValueError('편집 길이가 다릅니다.')


@lru_cache(maxsize=8)
def card(lang, name):
    return Image.open(HERE / 'cards' / lang / f'{name}.png').convert('RGBA')


def compose(frame_index, lang):
    t = frame_index / FPS
    img = Image.new('RGBA', SIZE, (0, 0, 0, 255))
    for start, length, shot, first in SEGMENTS:
        if round(start * FPS) <= frame_index < round((start + length) * FPS):
            if shot:
                index = first + frame_index - round(start * FPS)
                with Image.open(HERE / shot / f'frame_{index:04d}.png') as source:
                    if source.size != SIZE:
                        raise ValueError(f'촬영 해상도가 다릅니다: {shot}, {source.size}')
                    img = source.convert('RGBA')
                if shot in FRAMING:
                    zoom, cx, cy = FRAMING[shot]
                    w, h = round(SIZE[0] / zoom), round(SIZE[1] / zoom)
                    left = min(max(cx - w // 2, 0), SIZE[0] - w)
                    top = min(max(cy - h // 2, 0), SIZE[1] - h)
                    img = img.crop((left, top, left + w, top + h)).resize(SIZE, Image.Resampling.LANCZOS)
            break
    for name, start, end in CARDS:
        if start <= t < end:
            fade = 0.6 if name == 'title' else 0.2
            alpha = max(0.0, min(1.0, (t - start) / fade, (end - t) / fade))
            layer = card(lang, name).copy()
            layer.putalpha(layer.getchannel('A').point(lambda x: round(x * alpha)))
            img = Image.alpha_composite(img, layer)
    return img.convert('RGB')


def main(lang):
    validate_frames()
    out = HERE / OUTPUT[lang]
    cmd = ['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', '1920x1080',
           '-r', str(FPS), '-i', '-', '-i', str(HERE / 'trailer_audio.wav'), '-map', '0:v', '-map', '1:a',
           '-c:v', 'libx264', '-preset', 'slow', '-crf', '17', '-pix_fmt', 'yuv420p',
           '-c:a', 'aac', '-b:a', '192k', '-movflags', '+faststart', '-t', str(LENGTH), str(out)]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    try:
        for n in range(round(LENGTH * FPS)):
            proc.stdin.write(compose(n, lang).tobytes())
            if n % 300 == 0:
                print(f'편집 {n}/{round(LENGTH * FPS)}', flush=True)
    finally:
        proc.stdin.close()
    if proc.wait() != 0:
        raise RuntimeError('ffmpeg 인코딩 실패')
    print(f'TRAILER PASS {out} {LENGTH}s')


if __name__ == '__main__':
    HERE.mkdir(parents=True, exist_ok=True)
    if len(sys.argv) > 1 and sys.argv[1] == '--marks':
        (HERE / 'edl.json').write_text(json.dumps(
            {'length': LENGTH, 'fps': FPS, 'segments': SEGMENTS, 'marks': MARKS}, indent=2), encoding='utf-8')
    elif len(sys.argv) > 2 and sys.argv[1] == '--still':
        lang = sys.argv[3] if len(sys.argv) > 3 else 'ko'
        compose(round(float(sys.argv[2]) * FPS), lang).save(HERE / f'still-{lang}-{sys.argv[2]}.png')
    else:
        main(sys.argv[1] if len(sys.argv) > 1 else 'ko')
