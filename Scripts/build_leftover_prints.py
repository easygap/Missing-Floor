"""「302호에 남은 짐」의 글자 인쇄(2026-10-13). 게임의 읽기 화면 문구와 글자가 같아야 한다.

- LeftoverCollectionNotice.png: 1층 게시판 옆 「대형 폐기물 수거 안내」 A4. 수거일 줄을 볼펜으로 긋고
  옆에 손글씨로 「이번 주 목요일 아침 7시」.
- LeftoverLabelJeongwoo.png: 관리실 안쪽 방 상자 이름표. 매직 손글씨 「202 정우 / 겨울 장비」 위에 빨간
  「주인 없음」 딱지.
- LeftoverLabelDoha.png: 오빠 글씨의 상자 이름표 「백도하 / 작업 공구」.
- LeftoverLabelMarked.png: 정우가 오빠 상자에 붙인 테이프 「버리지 마세요」.
- LeftoverLabel303.png: 303호가 김치통 상자에 써 붙인 종이 「303 김치통 / 겨울까지만 부탁드려요」(2026-10-14).
- LeftoverStickerUnclaimed.png: 그 위에 관리인이 덧붙인 빨간 「주인 없음」 딱지(들춰 볼 수 있게 따로 뺐다).

    python Scripts/build_leftover_prints.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Content/SourceArt/LeftoverPrints'
OUT.mkdir(parents=True, exist_ok=True)
PRINT = 'C:/Windows/Fonts/malgun.ttf'
PRINT_BOLD = 'C:/Windows/Fonts/malgunbd.ttf'
HAND = str(ROOT / 'Scripts/fonts/NanumPenScript/NanumPenScript-Regular.ttf')


def font(path, size):
    return ImageFont.truetype(path, size)


# -- 수거 안내(A4, 210x297 → 1240x1754) ------------------------------------------
W, H = 1240, 1754
notice = Image.new('RGB', (W, H), '#eeece4')
d = ImageDraw.Draw(notice)
d.text((W // 2, 150), '대형 폐기물 수거 안내', font=font(PRINT_BOLD, 92), fill='#1f211d', anchor='mt')
d.line((150, 290, W - 150, 290), fill='#2b2d28', width=4)
body = [
    (420, '건물 안에 주인 없이 남은 짐을'),
    (500, '한꺼번에 처리합니다.'),
]
for y, text in body:
    d.text((W // 2, y), text, font=font(PRINT, 58), fill='#262823', anchor='mt')
# 수거일 줄. 인쇄된 날짜를 볼펜으로 두 번 긋는다.
date_y = 690
d.text((180, date_y), '수거일:', font=font(PRINT_BOLD, 60), fill='#262823')
d.text((430, date_y), '다음 주 월요일 오전', font=font(PRINT, 60), fill='#262823')
left, right = 425, 1010
for offset, tilt in ((34, -6), (44, 5)):
    d.line((left, date_y + offset + tilt, right, date_y + offset - tilt), fill='#1b2a6b', width=7)
# 손글씨 고침. 볼펜이 조금 번진다.
hand = Image.new('L', (W, H), 0)
hd = ImageDraw.Draw(hand)
hd.text((470, 790), '이번 주 목요일 아침 7시', font=font(HAND, 104), fill=255)
hd.line((440, 905, 1120, 912), fill=200, width=5)
hand = hand.filter(ImageFilter.GaussianBlur(0.8))
ink = Image.new('RGB', (W, H), '#1b2a6b')
notice.paste(ink, (0, 0), hand)
for y, text in ((1080, '버릴 짐은 전날 밤까지'), (1160, '필로티 적치 자리에 내놓아 주세요.'),
                (1320, '찾아갈 짐이 있으면'), (1400, '관리실에 말씀해 주세요.')):
    d.text((W // 2, y), text, font=font(PRINT, 54), fill='#262823', anchor='mt')
d.text((W - 170, 1560), '-관리인', font=font(PRINT, 50), fill='#262823', anchor='rt')
notice.save(OUT / 'LeftoverCollectionNotice.png')


# -- 이름표(16x9 cm → 640x360) ----------------------------------------------------
def label(path, lines, sticker=None, paper='#e6e1d2'):
    img = Image.new('RGB', (640, 360), paper)
    ink = Image.new('L', (640, 360), 0)
    dr = ImageDraw.Draw(ink)
    y = 60
    for text, size in lines:
        dr.text((320, y), text, font=font(HAND, size), fill=255, anchor='mt')
        y += size + 18
    ink = ink.filter(ImageFilter.GaussianBlur(0.6))
    img.paste(Image.new('RGB', (640, 360), '#111213'), (0, 0), ink)
    if sticker:
        # 사선으로 붙인 빨간 인쇄 딱지.
        tag = Image.new('RGBA', (300, 110), (0, 0, 0, 0))
        td = ImageDraw.Draw(tag)
        td.rounded_rectangle((0, 0, 299, 109), radius=12, fill='#b8231d')
        td.text((150, 55), sticker, font=font(PRINT_BOLD, 52), fill='#f3efe6', anchor='mm')
        tag = tag.rotate(-7, expand=True, resample=Image.BICUBIC)
        img.paste(tag, (318, 236), tag)
    img.save(OUT / path)


label('LeftoverLabelJeongwoo.png', [('202 정우', 108), ('겨울 장비', 90)], sticker='주인 없음')
label('LeftoverLabelDoha.png', [('백도하', 118), ('작업 공구', 84)], paper='#e3dccb')

# -- 정우가 붙인 테이프(20x5 cm → 800x200) ------------------------------------------
tape = Image.new('RGB', (800, 200), '#d9cfa8')
tink = Image.new('L', (800, 200), 0)
ImageDraw.Draw(tink).text((400, 100), '버리지 마세요', font=font(HAND, 120), fill=255, anchor='mm')
tink = tink.filter(ImageFilter.GaussianBlur(0.7))
tape.paste(Image.new('RGB', (800, 200), '#121314'), (0, 0), tink)
tape.save(OUT / 'LeftoverLabelMarked.png')

# -- 303호 김치통 상자(16x9 cm)와 그 위에 덧붙인 딱지(8x3 cm → 400x150) --------------------
label('LeftoverLabel303.png', [('303 김치통', 104), ('겨울까지만 부탁드려요', 64)], paper='#ece6d6')
sticker = Image.new('RGB', (400, 150), '#b8231d')
sd = ImageDraw.Draw(sticker)
sd.rectangle((6, 6, 393, 143), outline='#d8564e', width=3)
sd.text((200, 75), '주인 없음', font=font(PRINT_BOLD, 70), fill='#f3efe6', anchor='mm')
sticker.save(OUT / 'LeftoverStickerUnclaimed.png')

print('LEFTOVER_PRINTS PASS notice=1 labels=5')
