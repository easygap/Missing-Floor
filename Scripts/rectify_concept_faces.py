"""시안 사진의 면을 원근 없이 펴서 Blender 빌더가 바로 쓰는 텍스처로 만든다.

gpt-image 시안은 3/4 각도에서 찍은 사진이다. 형상은 Blender에서 다시 만들고, 겉면은
시안의 면을 그대로 옮겨야 시안과 같은 물건이 된다. 면의 네 꼭짓점을 사진에서 재 두고
투영 변환으로 직사각형에 편다. 꼭짓점은 시안을 격자 위에 올려 눈으로 쟀다.

- 고임목(FireDoorWedge_20261007): 닳아 검게 뭉개진 얇은 끝까지 이어지는 윗면.
- 새벽배송 보냉 가방(DawnDeliveryBag_20261007): 앞면(끈·투명 주머니·지퍼 손잡이),
  옆면, 윗면에 쓸 민무늬 천, 손잡이 끈.

    python Scripts/rectify_concept_faces.py
"""
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
AI = ROOT / 'Content/SourceArt/AI'
OUT = ROOT / 'Content/SourceArt/UtilityPrints'

# (원본, 결과, 결과 크기, 꼭짓점: 결과의 왼쪽 위·오른쪽 위·오른쪽 아래·왼쪽 아래 순)
FACES = [
    # 왼쪽이 얇은 끝, 위가 먼 쪽 모서리다.
    ('FireDoorWedge_20261007', 'FireDoorWedgeTop', (1024, 400),
     [(165, 790), (1175, 155), (1390, 330), (430, 885)]),
    # 앞면. 테두리의 초록 배경이 섞이지 않게 꼭짓점을 몇 픽셀씩 안으로 들였다.
    ('DawnDeliveryBag_20261007', 'DawnBagFront', (900, 560),
     [(246, 266), (1122, 395), (1098, 926), (256, 748)]),
    ('DawnDeliveryBag_20261007', 'DawnBagSide', (600, 560),
     [(1132, 395), (1288, 236), (1284, 696), (1106, 924)]),
]

# 가방 옆면에서 끈도 지퍼도 없는 가운데 천만 잘라 윗면에 쓴다. 시안의 윗면은 너무
# 비스듬하고 선 손잡이가 겹쳐서 펴면 늘어진다.
TOP_FROM_SIDE = ('DawnBagSide', 'DawnBagTop', (60, 120, 560, 470))
# 앞면 왼쪽 끈의 곧은 구간. 윗면에 얹는 손잡이 띠가 이 결을 쓴다.
WEBBING_FROM_FRONT = ('DawnBagFront', 'DawnBagWebbing', (122, 140, 188, 520))


def solve(dst, src):
    rows, values = [], []
    for (x, y), (u, v) in zip(dst, src):
        rows.append([x, y, 1, 0, 0, 0, -u * x, -u * y])
        values.append(u)
        rows.append([0, 0, 0, x, y, 1, -v * x, -v * y])
        values.append(v)
    return tuple(np.linalg.solve(np.array(rows, float), np.array(values, float)))


def run():
    OUT.mkdir(parents=True, exist_ok=True)
    for source, name, (width, height), corners in FACES:
        image = Image.open(AI / f'{source}.png').convert('RGB')
        coefficients = solve([(0, 0), (width, 0), (width, height), (0, height)], corners)
        image.transform((width, height), Image.PERSPECTIVE, coefficients, Image.BICUBIC).save(OUT / f'{name}.png')
    for source, name, box in (TOP_FROM_SIDE, WEBBING_FROM_FRONT):
        Image.open(OUT / f'{source}.png').crop(box).save(OUT / f'{name}.png')
    print('RECTIFY_CONCEPT_FACES PASS faces=%d' % (len(FACES) + 2))


if __name__ == '__main__':
    run()
