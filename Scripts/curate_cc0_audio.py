"""CC0 녹음을 게임 소리로 다듬는다.

합성기(IGToneSequenceSoundWave)는 발소리·문·노크·기는 소리가 전부 「붕」과
「부스스」였다. 가장 자주 들리는 것부터 실제 녹음으로 바꾼다. 원본은 전부
CC0(OpenGameart rubberduck·Kenney·Owlish Media)라 출처 표기 의무가 없지만
어디서 왔는지는 manifest에 남긴다.

    python Scripts/curate_cc0_audio.py --packs Saved/AudioCC0 --out Content/SourceArt/Audio

산출물은 48 kHz 16비트 WAV. 루프는 끝과 시작을 겹쳐 이어 붙여 이음매가 없다.
디코딩은 ffmpeg(winget Gyan.FFmpeg), 가공은 numpy·scipy.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import wave

import numpy as np
from scipy import signal

SR = 48000


def find_ffmpeg() -> str:
    found = shutil.which("ffmpeg")
    if found:
        return found
    for path in glob.glob(os.path.expandvars(
            r"%LOCALAPPDATA%\Microsoft\WinGet\Packages\Gyan.FFmpeg*\ffmpeg-*\bin\ffmpeg.exe")):
        return path
    raise RuntimeError("ffmpeg를 찾지 못했다")


FFMPEG = None


def decode(path: str) -> np.ndarray:
    """어떤 포맷이든 48 kHz 모노 float32로."""
    result = subprocess.run(
        [FFMPEG, "-v", "error", "-i", path, "-f", "f32le", "-ac", "1", "-ar", str(SR), "-"],
        capture_output=True, check=True)
    return np.frombuffer(result.stdout, dtype=np.float32).copy()


def write_wav(path: str, samples: np.ndarray) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    clipped = np.clip(samples, -1.0, 1.0)
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SR)
        handle.writeframes((clipped * 32767.0).astype("<i2").tobytes())


# --------------------------------------------------------------------------
# 가공
# --------------------------------------------------------------------------

def normalize(x: np.ndarray, peak: float = 0.89) -> np.ndarray:
    m = float(np.max(np.abs(x))) if len(x) else 0.0
    return x * (peak / m) if m > 1e-6 else x


def trim_silence(x: np.ndarray, threshold: float = 0.004, pad: float = 0.01) -> np.ndarray:
    idx = np.where(np.abs(x) > threshold)[0]
    if len(idx) == 0:
        return x
    a = max(0, idx[0] - int(pad * SR))
    b = min(len(x), idx[-1] + int(pad * SR))
    return x[a:b]


def pitch(x: np.ndarray, ratio: float) -> np.ndarray:
    """ratio < 1이면 낮고 길어진다(테이프처럼). 효과음에는 그게 맞다."""
    if abs(ratio - 1.0) < 1e-3:
        return x
    n = int(round(len(x) / ratio))
    return signal.resample(x, n).astype(np.float32)


def lowpass(x: np.ndarray, hz: float, order: int = 4) -> np.ndarray:
    sos = signal.butter(order, hz / (SR * 0.5), btype="low", output="sos")
    return signal.sosfilt(sos, x).astype(np.float32)


def highpass(x: np.ndarray, hz: float, order: int = 2) -> np.ndarray:
    sos = signal.butter(order, hz / (SR * 0.5), btype="high", output="sos")
    return signal.sosfilt(sos, x).astype(np.float32)


def fade(x: np.ndarray, in_s: float = 0.0, out_s: float = 0.0) -> np.ndarray:
    y = x.copy()
    n_in = int(in_s * SR)
    n_out = int(out_s * SR)
    if n_in > 0:
        y[:n_in] *= np.linspace(0.0, 1.0, n_in, dtype=np.float32)
    if n_out > 0:
        y[-n_out:] *= np.linspace(1.0, 0.0, n_out, dtype=np.float32)
    return y


def room(x: np.ndarray, delay_s: float = 0.021, feedback: float = 0.32, wet: float = 0.25,
         damp_hz: float = 2600.0) -> np.ndarray:
    """복도 되울림. 합성기의 ConfigureRoomTail과 같은 발상."""
    d = int(delay_s * SR)
    tail = int(SR * 0.9)
    y = np.zeros(len(x) + tail, dtype=np.float32)
    y[:len(x)] = x
    buf = np.zeros_like(y)
    buf[:len(x)] = x
    sos = signal.butter(2, damp_hz / (SR * 0.5), btype="low", output="sos")
    acc = np.zeros_like(y)
    cur = buf
    for _ in range(8):
        shifted = np.zeros_like(y)
        shifted[d:] = cur[:-d] * feedback
        shifted = signal.sosfilt(sos, shifted).astype(np.float32)
        acc += shifted
        cur = shifted
    return y + acc * wet


def mix(parts: list[tuple[np.ndarray, float, float]]) -> np.ndarray:
    """(신호, 시작 초, 게인) 목록을 겹친다."""
    length = max(int(start * SR) + len(sig) for sig, start, _ in parts)
    out = np.zeros(length, dtype=np.float32)
    for sig, start, gain in parts:
        a = int(start * SR)
        out[a:a + len(sig)] += sig * gain
    return out


def loop_seamless(x: np.ndarray, seconds: float, cross: float = 1.2) -> np.ndarray:
    """앞부분 seconds만큼 잘라 끝과 시작을 cross초 겹쳐 잇는다."""
    n = int(seconds * SR)
    c = int(cross * SR)
    if len(x) < n + c:
        reps = int(np.ceil((n + c) / max(len(x), 1)))
        x = np.tile(x, reps)
    body = x[:n + c].copy()
    head = body[:c] * np.linspace(0.0, 1.0, c, dtype=np.float32)
    tail = body[n:n + c] * np.linspace(1.0, 0.0, c, dtype=np.float32)
    out = body[:n].copy()
    out[:c] = head + tail
    return out


def rumble(seconds: float, hz: float = 38.0, gain: float = 0.5, seed: int = 1) -> np.ndarray:
    """저역 드론. 스팅어 밑에 까는 몸통."""
    t = np.arange(int(seconds * SR)) / SR
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal(len(t)).astype(np.float32)
    noise = lowpass(noise, 90.0, order=4)
    tone = np.sin(2 * np.pi * hz * t) * 0.6 + np.sin(2 * np.pi * hz * 1.5 * t) * 0.25
    return normalize((tone + noise * 2.0).astype(np.float32), gain)


# --------------------------------------------------------------------------
# 목록
# --------------------------------------------------------------------------

def spec(packs: str):
    K = os.path.join(packs, "kenney_impact")
    S1 = os.path.join(packs, "oga_100_cc0_sfx")
    WM = os.path.join(packs, "oga_100_cc0_wood_metal")
    S2 = os.path.join(packs, "oga_100_cc0_sfx2")
    OW = os.path.join(packs, "oga_owlish")

    def one(folder, name):
        hits = glob.glob(os.path.join(folder, "**", name), recursive=True)
        if not hits:
            raise FileNotFoundError(f"{folder}/{name}")
        return hits[0]

    items = []

    def add(name, sources, build, loop=False, note=""):
        items.append({"name": name, "sources": sources, "build": build, "loop": loop, "note": note})

    # --- 발소리. 표면마다 셋~다섯. 볼륨·피치 변주는 코드가 건다. ---------------
    for i in range(5):
        src = one(K, f"footstep_concrete_00{i}.ogg")
        add(f"Foot_Concrete_{i}", [src], lambda x: normalize(highpass(trim_silence(x), 60.0), 0.8),
            note="복도 화강석 타일")
    for i in range(5):
        src = one(K, f"footstep_carpet_00{i}.ogg")
        add(f"Foot_Vinyl_{i}", [src], lambda x: normalize(lowpass(trim_silence(x), 5200.0), 0.62),
            note="403호 장판. 카펫 녹음을 눌러 비닐 위 맨발처럼")
    for i in range(5):
        src = one(K, f"impactPlate_light_00{i}.ogg")
        add(f"Foot_MetalStair_{i}", [src], lambda x: normalize(room(pitch(trim_silence(x), 0.82), 0.013, 0.28, 0.35), 0.8),
            note="철제 계단. 얇은 판 충격을 낮춰 계단실 울림")
    for i in range(3):
        src = one(S2, f"sfx100v2_footstep_wet_0{i + 1}.ogg")
        add(f"Foot_Water_{i}", [src], lambda x: normalize(trim_silence(x), 0.78), note="밤4 고인 물")
    for i in range(5):
        src = one(K, f"footstep_snow_00{i}.ogg")
        add(f"Foot_Gypsum_{i}", [src], lambda x: normalize(highpass(pitch(trim_silence(x), 1.12), 120.0), 0.72),
            note="5층 석고 파편. 눈 밟는 소리를 올려 바삭하게")
    for i in range(3):
        src = one(K, f"footstep_concrete_00{i + 1}.ogg")
        add(f"Foot_Rooftop_{i}", [src], lambda x: normalize(lowpass(pitch(trim_silence(x), 0.9), 3000.0), 0.55),
            note="옥상 우레탄 방수층. 콘크리트를 낮고 둔하게")

    # --- 문. 세대 현관은 철문이다. -------------------------------------------
    add("Door_Steel_Open", [one(WM, "metal_open_01.ogg")], lambda x: normalize(trim_silence(x), 0.8))
    add("Door_Steel_Close", [one(WM, "metal_close_01.ogg")], lambda x: normalize(trim_silence(x), 0.85))
    add("Door_Creak_0", [one(WM, "wood_squeak_01.ogg")], lambda x: normalize(pitch(trim_silence(x), 0.9), 0.7))
    add("Door_Creak_1", [one(WM, "wood_squeak_02.ogg")], lambda x: normalize(pitch(trim_silence(x), 0.85), 0.7))
    add("Lock_Rattle", [one(WM, "keys_03.ogg")], lambda x: normalize(trim_silence(x)[:int(0.9 * SR)], 0.7),
        note="잠긴 문 손잡이")
    add("Lock_Open", [one(WM, "lock_open_01.ogg")], lambda x: normalize(trim_silence(x), 0.8))

    # --- 노크. 벽은 석고보드, 문은 철판. -------------------------------------
    for i, src_name in enumerate(("wood_hit_02.ogg", "wood_hit_05.ogg", "wood_hit_08.ogg")):
        src = one(WM, src_name)
        add(f"Knock_Plaster_{i}", [src], lambda x: normalize(lowpass(pitch(trim_silence(x), 0.72), 1800.0), 0.85),
            note="주먹으로 석고보드. 나무 타격을 낮추고 고역을 먹였다")
    for i, src_name in enumerate(("metal_hit_02.ogg", "metal_hit_04.ogg", "metal_hit_05.ogg")):
        src = one(WM, src_name)
        add(f"Knock_Steel_{i}", [src], lambda x: normalize(pitch(trim_silence(x), 0.88), 0.8),
            note="현관 철문")

    def knock_triple(x_list, muffled):
        hits = []
        for i, x in enumerate(x_list):
            h = lowpass(pitch(trim_silence(x), 0.70), 700.0 if muffled else 2200.0)
            hits.append((normalize(h, 0.9), 0.62 * i, 1.0 if not muffled else 0.7))
        out = mix(hits)
        return normalize(room(out, 0.021, 0.40, 0.35 if not muffled else 0.5), 0.9)

    add("Entity_KnockTriple", [one(WM, "wood_hit_02.ogg"), one(WM, "wood_hit_05.ogg"), one(WM, "wood_hit_08.ogg")],
        lambda *xs: knock_triple(list(xs), False), note="그의 노크 셋. 0.62초 간격, CreateWallKnockTriple과 같다")
    add("Entity_KnockTriple_Muffled", [one(WM, "wood_hit_02.ogg"), one(WM, "wood_hit_05.ogg"), one(WM, "wood_hit_08.ogg")],
        lambda *xs: knock_triple(list(xs), True), note="벽 너머로 듣는 같은 노크")

    # --- 위층 사람. ------------------------------------------------------------
    for i in range(3):
        # 긁힘은 한 걸음 길이(0.6초)에서 자른다. scrape3은 4.6초짜리 원본이라 걸음 하나가
        # 4초 넘게 울리며 다음 걸음과 겹쳤다.
        add(f"Entity_CrawlStep_{i}", [one(OW, f"scrape{i + 1}.wav"), one(K, f"impactPunch_medium_00{i}.ogg")],
            lambda a, b: normalize(mix([
                (lowpass(pitch(fade(trim_silence(a)[:int(0.6 * SR)], 0.0, 0.12), 0.8), 3500.0), 0.0, 0.9),
                (lowpass(pitch(trim_silence(b), 0.7), 900.0), 0.02, 0.55),
            ]), 0.85),
            note="팔꿈치가 닿고 몸이 끌린다. 긁힘 위에 둔탁한 타격")
    add("Entity_Breath_Loop", [one(OW, "breath-male.wav")],
        lambda x: normalize(loop_seamless(lowpass(pitch(x, 0.86), 4000.0), 6.0, 1.0), 0.6), loop=True,
        note="숨. 남자 숨을 낮춰서")
    # 유담의 헐떡임(Player_Gasp)과 같은 원본이다. 첫 들숨 하나만 남기고 그의 숨 루프처럼
    # 어둡게 깎아, 같은 녹음이 두 사람 입에서 나오지 않게 한다. 재생 쪽이 피치 0.9를
    # 한 번 더 걸므로 실제로는 0.72배로 들린다.
    add("Entity_Alert", [one(OW, "gasp1.wav")],
        lambda x: normalize(room(lowpass(fade(pitch(trim_silence(x)[:int(0.5 * SR)], 0.8), 0.0, 0.1), 3500.0),
                                 0.02, 0.3, 0.3), 0.8),
        note="소리를 들었다. 첫 들숨 하나만, 낮고 어둡게")
    add("Entity_Grab", [one(OW, "shouldergrab.wav"), one(OW, "freakedbreath.wav")],
        lambda a, b: normalize(mix([(trim_silence(a), 0.0, 1.0), (pitch(trim_silence(b), 0.9), 0.15, 0.7)]), 0.95),
        note="덮침. 붙잡는 손과 거친 숨")

    # --- 놀람. 앰비언트보다 14dB 위, 0.5~2초, 뒤는 침묵. -------------------------
    add("Stinger_CloseCall", [one(OW, "hit.wav"), one(OW, "blackhole1.wav")],
        lambda a, b: normalize(fade(mix([
            (trim_silence(a), 0.0, 1.0),
            (pitch(trim_silence(b), 0.7)[:int(1.6 * SR)], 0.05, 0.8),
            (rumble(1.4, 34.0, 0.7, 3), 0.0, 1.0),
        ]), 0.0, 0.4), 0.98), note="코앞에서 마주쳤다")
    add("Stinger_ChaseStart", [one(K, "impactMetal_heavy_001.ogg"), one(OW, "earthquake.wav")],
        lambda a, b: normalize(fade(mix([
            (pitch(trim_silence(a), 0.6), 0.0, 0.9),
            (pitch(trim_silence(b), 0.9)[:int(1.8 * SR)], 0.0, 0.8),
        ]), 0.0, 0.5), 0.98), note="추격이 시작됐다")

    # --- 세계. 루프는 이음매를 지웠다. ---------------------------------------
    add("Bed_City_Night", [one(S2, "sfx100v2_loop_highway.ogg")],
        lambda x: normalize(loop_seamless(lowpass(x, 1800.0), 24.0, 2.0), 0.5), loop=True,
        note="먼 도로. 창 너머 서울 새벽")
    add("Bed_Corridor", [one(S2, "sfx100v2_loop_ambient_02.ogg"), one(S2, "sfx100v2_air_01.ogg")],
        lambda a, b: normalize(loop_seamless(mix([
            (lowpass(a, 900.0), 0.0, 0.8),
            (np.tile(lowpass(b, 2400.0), 3), 0.0, 0.35),
        ]), 20.0, 2.0), 0.45), loop=True, note="복도 베드. 공기와 문틈 바람")
    add("Hum_Machine", [one(S2, "sfx100v2_loop_machine_02.ogg")],
        lambda x: normalize(loop_seamless(lowpass(x, 700.0), 8.0, 1.0), 0.5), loop=True,
        note="냉장고·배전반 험")
    add("Wind_Gap", [one(S2, "sfx100v2_air_02.ogg")],
        lambda x: normalize(loop_seamless(x, 10.0, 1.5), 0.5), loop=True, note="옥상 문틈")
    add("Ballast_Tick", [one(S1, "switch_02.ogg")],
        lambda x: normalize(highpass(trim_silence(x), 900.0), 0.55), note="죽어 가는 형광등 안정기")
    add("Extinguisher_Drop", [one(WM, "metal_falling_02.ogg")],
        lambda x: normalize(room(trim_silence(x), 0.02, 0.35, 0.3), 0.95), note="복도 소화기 낙하")
    add("Phone_Vibrate", [one(OW, "Phone_vibrate.wav")], lambda x: normalize(trim_silence(x), 0.7))
    add("Paper_Turn_0", [one(OW, "pageturn1.wav")], lambda x: normalize(trim_silence(x), 0.6))
    add("Paper_Turn_1", [one(OW, "pageturn2.wav")], lambda x: normalize(trim_silence(x), 0.6))
    add("Hammer_Hit_0", [one(WM, "hammer_02.ogg")], lambda x: normalize(room(trim_silence(x), 0.018, 0.4, 0.4), 0.95), note="밤4 망치")
    add("Hammer_Hit_1", [one(WM, "hammer_03.ogg")], lambda x: normalize(room(trim_silence(x), 0.018, 0.4, 0.4), 0.95))
    add("Settle_Creak_0", [one(WM, "wood_cracking_01.ogg")], lambda x: normalize(lowpass(pitch(trim_silence(x), 0.8), 3000.0), 0.55), note="건물이 뒤틀린다")
    add("Settle_Creak_1", [one(WM, "wood_cracking_03.ogg")], lambda x: normalize(lowpass(pitch(trim_silence(x), 0.75), 3000.0), 0.55))
    # 둘 다 남자 녹음이다. main()이 끝에서 feminize_player_voice로 유담의 음역에 옮기고,
    # 덮칠 때 숨(Entity_Grab)도 옮긴 숨으로 다시 짠다.
    add("Player_Breath_Scared", [one(OW, "scared-breathing.wav")],
        lambda x: normalize(loop_seamless(x, 5.0, 0.8), 0.55), loop=True, note="숨 참기 직전, 심장 위에")
    add("Player_Gasp", [one(OW, "gasp1.wav")], lambda x: normalize(trim_silence(x), 0.7))
    return items


def main():
    global FFMPEG
    parser = argparse.ArgumentParser()
    parser.add_argument("--packs", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    FFMPEG = find_ffmpeg()
    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)
    manifest = {"sample_rate": SR, "license": "CC0 1.0 (OpenGameArt rubberduck, Kenney, Owlish Media)", "sounds": []}
    for item in spec(os.path.abspath(args.packs)):
        decoded = [decode(src) for src in item["sources"]]
        try:
            result = item["build"](*decoded)
        except TypeError:
            result = item["build"](decoded[0])
        result = np.asarray(result, dtype=np.float32)
        if not item["loop"]:
            result = fade(result, 0.002, 0.02)
        path = os.path.join(out_dir, f"{item['name']}.wav")
        write_wav(path, result)
        manifest["sounds"].append({
            "name": item["name"],
            "file": os.path.basename(path),
            "loop": item["loop"],
            "seconds": round(len(result) / SR, 3),
            "sources": [os.path.relpath(s, os.path.abspath(args.packs)).replace("\\", "/") for s in item["sources"]],
            "note": item["note"],
        })
        print(f"[audio] {item['name']}: {len(result) / SR:.2f}s from {[os.path.basename(s) for s in item['sources']]}")
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    print(f"[audio] {len(manifest['sounds'])} sounds -> {out_dir}")
    # 유담의 숨과 헐떡임은 남자 녹음이라 마지막에 여성 음역으로 옮긴다.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import feminize_player_voice
    feminize_player_voice.apply(out_dir)


if __name__ == "__main__":
    main()
