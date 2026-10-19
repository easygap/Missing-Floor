"""예고편 위에 얹을 글자. 게임 화면의 소리 자막·속말·대사 상자와 같은 모양으로 그린다.

설명 자막을 따로 쓰지 않는다. 화면에 나오는 글은 게임에서 실제로 뜨는 자막 세 줄과
끝 카드(제목, 받는 곳)뿐이다. 한국어판과 영어판은 같은 촬영분에 글자만 바꿔 얹는다.

    python Scripts/Trailer/cards.py            # 두 언어의 글자 판
    python Scripts/Trailer/cards.py --social Docs/Media/social-preview.png
"""
from pathlib import Path
import os
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
# 예고편 만들기(Build-Trailer.ps1)는 실행 폴더를 IG_TRAILER_DIR로 넘긴다. 없으면 예전 자리다.
OUT = Path(os.environ.get('IG_TRAILER_DIR') or ROOT / 'Saved/Trailer') / 'cards'
FONTS = ROOT / 'Source/IndieGame/UI/Fonts'
SIZE = (1920, 1080)

# 게임 문자열을 그대로 쓴다(IGMissingFloor, NightOneCeilingKnockCaption /
# NightOneTopFloorThought / GuestSpeaker / GuestN2Line1, 방위 표기 IGHUD,
# SoundCaptionWithBearing).
TEXT = {
    'ko': {
        'knock': '[위] 천장에서 세 번 두드리는 소리',
        'topfloor': '…4층이 꼭대기인데.',
        'door_speaker': '문밖',
        'door_line': '언니, 저 편의점 나린이에요. 아까 지갑 두고 가셨어요.',
        'subtitle': 'Windows 무료 플레이 테스트',
    },
    'en': {
        'knock': '[above] three knocks from the ceiling',
        'topfloor': '…But the fourth floor is the top floor.',
        'door_speaker': 'Outside the door',
        'door_line': "Hey, it's Narin from the convenience store. You left your wallet earlier.",
        'subtitle': 'Free Windows playtest',
    },
}
LINK = 'github.com/easygap/Missing-Floor'


def font(size, bold=False):
    return ImageFont.truetype(str(FONTS / ('Pretendard-SemiBold.otf' if bold else 'Pretendard-Regular.otf')), size)


def sound_caption(text):
    """화면 아래쪽 알약 모양의 소리 자막. 왼쪽에 소리 막대 표시가 붙는다."""
    layer = Image.new('RGBA', SIZE)
    draw = ImageDraw.Draw(layer)
    typeface = font(21)
    box = draw.textbbox((0, 0), text, font=typeface)
    width = box[2] - box[0] + 76
    left = (SIZE[0] - width) // 2
    top, height = 861, 46
    draw.rounded_rectangle((left, top, left + width, top + height), radius=12, fill=(32, 36, 36, 214))
    for index, bar in enumerate((6, 12, 16, 9)):
        x = left + 20 + index * 5
        draw.rectangle((x, top + height / 2 - bar / 2, x + 2, top + height / 2 + bar / 2), fill=(170, 176, 172, 255))
    draw.text((left + 54, top + (height - (box[3] - box[1])) / 2 - box[1]), text, font=typeface, fill=(214, 214, 206, 255))
    return layer


def thought(text):
    """속말 상자. 게임처럼 넓은 상자에 왼쪽 정렬로 쓴다."""
    layer = Image.new('RGBA', SIZE)
    draw = ImageDraw.Draw(layer)
    left, top, right, bottom = 540, 922, 1380, 982
    draw.rounded_rectangle((left, top, right, bottom), radius=8, fill=(36, 40, 40, 206), outline=(86, 92, 92, 150))
    typeface = font(24)
    box = draw.textbbox((0, 0), text, font=typeface)
    draw.text((left + 30, top + (bottom - top - (box[3] - box[1])) / 2 - box[1]), text, font=typeface,
              fill=(226, 226, 219, 255))
    return layer


def dialogue(speaker, text):
    """대사 상자. 게임처럼 위에 말하는 쪽 이름표를 달고, 왼쪽 위에 청록 선을 긋는다."""
    layer = Image.new('RGBA', SIZE)
    draw = ImageDraw.Draw(layer)
    typeface = font(24)
    label = font(17, bold=True)
    box = draw.textbbox((0, 0), text, font=typeface)
    width = max(840, box[2] - box[0] + 64)
    left, top = (SIZE[0] - width) // 2, 884
    right, bottom = left + width, top + 98
    draw.rounded_rectangle((left, top, right, bottom), radius=8, fill=(3, 4, 4, 214), outline=(86, 92, 92, 150))
    draw.rectangle((left + 30, top, left + 86, top + 1), fill=(107, 163, 150, 140))
    chip = draw.textbbox((0, 0), speaker, font=label)
    draw.rounded_rectangle((left + 30, top + 14, left + 30 + chip[2] - chip[0] + 20, top + 38), radius=12,
                           fill=(26, 39, 36, 160))
    draw.text((left + 40, top + 26 - (chip[3] - chip[1]) / 2 - chip[1]), speaker, font=label, fill=(107, 163, 150, 245))
    draw.text((left + 30, top + 50 - box[1]), text, font=typeface, fill=(224, 222, 207, 255))
    return layer


def centered(draw, text, y, size, bold=False, fill=(234, 234, 228, 255)):
    typeface = font(size, bold)
    box = draw.textbbox((0, 0), text, font=typeface)
    draw.text(((SIZE[0] - (box[2] - box[0])) / 2 - box[0], y - box[1]), text, font=typeface, fill=fill)


def title(subtitle):
    """끝 카드. 제목과, 지금 받아 볼 수 있는 곳."""
    layer = Image.new('RGBA', SIZE)
    draw = ImageDraw.Draw(layer)
    centered(draw, 'Missing Floor', 430, 108, bold=True)
    centered(draw, subtitle, 576, 32, fill=(196, 200, 196, 255))
    centered(draw, LINK, 628, 26, fill=(140, 146, 146, 255))
    return layer


def social(path):
    """저장소 미리보기 카드. 실제 게임 화면 위에 제목과 한 줄만 둔다."""
    art = Image.open(ROOT / 'Docs/Media/game-corridor-day.png').convert('RGBA')
    art = Image.alpha_composite(art, Image.new('RGBA', SIZE, (0, 0, 0, 140)))
    draw = ImageDraw.Draw(art)
    centered(draw, 'Missing Floor', 372, 128, bold=True)
    centered(draw, '오빠의 주소는 501호. 이 빌라는 4층까지다.', 574, 42)
    art.convert('RGB').resize((1280, 720), Image.Resampling.LANCZOS).save(path)


def main():
    for lang, text in TEXT.items():
        folder = OUT / lang
        folder.mkdir(parents=True, exist_ok=True)
        sound_caption(text['knock']).save(folder / 'knock.png')
        thought(text['topfloor']).save(folder / 'topfloor.png')
        dialogue(text['door_speaker'], text['door_line']).save(folder / 'door.png')
        title(text['subtitle']).save(folder / 'title.png')
    print('TRAILER_CARDS PASS', ', '.join(TEXT))


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[1] == '--social':
        social(sys.argv[2])
    else:
        main()
