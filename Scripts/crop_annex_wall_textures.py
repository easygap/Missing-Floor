"""밤4 5층 시신 벽 칸의 겹친 층 텍스처를 시안에서 잘라 낸다(2026-10-14).

원본은 Content/SourceArt/AI/AnnexWallLayers_20261014.png(1024x1536)다. 시안의 칸은 가로 106 cm
(각재 사이), 덧판은 위쪽 53 cm, 이음 띠는 그 아래 19 cm, 각재는 폭 4.5 cm·높이 236 cm로 잡는다.
가로세로 비가 실제 치수와 맞게 자른다. 결과는 Content/SourceArt/AnnexWall/에 둔다.

    python Scripts/crop_annex_wall_textures.py
"""
from pathlib import Path

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Content/SourceArt/AI/AnnexWallLayers_20261014.png'
OUT = ROOT / 'Content/SourceArt/AnnexWall'
OUT.mkdir(parents=True, exist_ok=True)

image = Image.open(SOURCE).convert('RGB')
if image.size != (1024, 1536):
    raise SystemExit(f'시안 크기가 바뀌었다: {image.size}')

# 덧판 면. 각재 안쪽(x 192..820)의 이음 위. 106x53 cm = 2:1.
face = image.crop((192, 40, 820, 354)).resize((1024, 512), Image.LANCZOS)
face.save(OUT / 'AnnexOverlayFace.png')

# 이음 띠. 테이프와 갈라진 퍼티가 한 줄로 지나간다. 106x19 cm.
band = image.crop((192, 352, 820, 466)).resize((1024, 184), Image.LANCZOS)
band.save(OUT / 'AnnexJointBand.png')

# 각재 면. 검은 나사 머리가 30 cm마다 박혀 있다. 4.5x236 cm를 가로로 넉넉히 늘려 읽는다.
batten = image.crop((157, 0, 189, 1380)).resize((64, 2048), Image.LANCZOS)
batten = batten.filter(ImageFilter.GaussianBlur(0.4))
batten.save(OUT / 'AnnexBattenFace.png')

print('ANNEX_WALL_TEXTURES PASS face=1024x512 band=1024x184 batten=64x2048')
