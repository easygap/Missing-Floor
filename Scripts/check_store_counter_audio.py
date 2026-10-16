"""계산대 검사(-IGStoreCounterProbe -IGStoreCounterRecord)의 믹서 녹음을 사건 기록과 맞춰 본다.

    python Scripts/check_store_counter_audio.py <로그> <mix.wav> <출력 폴더>

녹음은 -DeterministicAudio라 게임 시계와 같은 박자로 렌더된다. 녹음 시작 시각(STORE_AV record_start)을
0으로 놓고 나린의 소리(STORE_CLERK sound=beep|print|pull|place)가 기록된 순간에 실제로 그 소리가 믹스에
나왔는지, 프린터 소리가 손님에게서 본 프린터 쪽 귀에 더 크게 들렸는지, 가게를 나간 뒤에는 단말기 소리가
작아지고 먹먹해졌는지 본다. 사람의 귀를 대신하지 않는다. 소리가 장면과 같은 때·같은 자리에서 나는지만 본다.

결과 줄: STORE_AUDIO_CHECK <이름> PASS|FAIL, 시트: <출력 폴더>/store_audio_sheet.png
"""
from __future__ import annotations

import math
import os
import re
import sys
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import signal

failures = 0


def check(ok: bool, name: str, detail: str = "") -> None:
    global failures
    if not ok:
        failures += 1
    print(f"STORE_AUDIO_CHECK {name} {'PASS' if ok else 'FAIL'} {detail}".rstrip())


def read_log(path: str):
    events = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = re.search(r"STORE_AV t=([0-9.]+) (.*)$", line)
            if m:
                events.append((float(m.group(1)), "av", m.group(2).strip()))
                continue
            m = re.search(r"STORE_CLERK t=([0-9.]+) (.*)$", line)
            if m:
                events.append((float(m.group(1)), "clerk", m.group(2).strip()))
    return events


def read_wav(path: str):
    with wave.open(path, "rb") as handle:
        rate = handle.getframerate()
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        raw = handle.readframes(handle.getnframes())
    if width == 2:
        data = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif width == 4:
        data = np.frombuffer(raw, dtype="<i4").astype(np.float32) / 2147483648.0
    else:
        raise SystemExit(f"지원하지 않는 샘플 폭: {width}")
    data = data.reshape(-1, channels)
    if channels == 1:
        data = np.repeat(data, 2, axis=1)
    return rate, data[:, :2]


def band(x: np.ndarray, rate: int, lo: float, hi: float) -> np.ndarray:
    sos = signal.butter(4, [lo / (rate / 2), hi / (rate / 2)], btype="band", output="sos")
    return signal.sosfilt(sos, x, axis=0)


def envelope(x: np.ndarray, rate: int, window: float = 0.010) -> np.ndarray:
    n = max(1, int(window * rate))
    mono = np.sqrt(np.mean(x ** 2, axis=1)) if x.ndim == 2 else np.abs(x)
    kernel = np.ones(n) / n
    return np.sqrt(np.convolve(mono ** 2, kernel, mode="same"))


def db(v: float) -> float:
    return 20.0 * math.log10(max(v, 1e-9))


def onset_near(env: np.ndarray, rate: int, at: float, before: float, after: float) -> tuple[float, float, float]:
    """at 근처에서 바닥보다 12 dB 넘게 솟는 첫 순간과 그 높이, 바닥을 돌려준다."""
    a = max(0, int((at - before) * rate))
    b = min(len(env), int((at + after) * rate))
    seg = env[a:b]
    if len(seg) < 10:
        return float("nan"), 0.0, 0.0
    floor = float(np.percentile(env[max(0, a - int(0.3 * rate)):a + 1], 50)) if a > 0 else float(np.percentile(seg, 10))
    peak = float(np.max(seg))
    threshold = max(floor * 4.0, peak * 0.25)
    idx = np.argmax(seg > threshold)
    if seg[idx] <= threshold:
        return float("nan"), peak, floor
    return (a + idx) / rate, peak, floor


def player_state_at(events, t: float):
    best = None
    for et, kind, text in events:
        if kind == "av" and text.startswith("player ") and et <= t:
            best = text
    if not best:
        return None
    m = re.search(r"at=([-0-9.]+),([-0-9.]+),([-0-9.]+) yaw=([-0-9.]+) pitch=([-0-9.]+) counter_dist=([-0-9.]+)", best)
    if not m:
        return None
    return {"x": float(m.group(1)), "y": float(m.group(2)), "yaw": float(m.group(4)), "dist": float(m.group(6))}


def main() -> int:
    log_path, mix_path, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    events = read_log(log_path)
    starts = [t for t, kind, text in events if kind == "av" and text == "record_start"]
    check(bool(starts), "record_start_logged")
    if not starts:
        return 1
    t0 = starts[0]
    rate, mix = read_wav(mix_path)
    duration = len(mix) / rate
    check(duration > 20.0, "recording_long_enough", f"seconds={duration:.1f}")
    peak = float(np.max(np.abs(mix)))
    check(peak < 0.999, "mix_not_clipped", f"peak_dbfs={db(peak):.1f}")

    beep_band = band(mix, rate, 2550.0, 2950.0)
    print_band = band(mix, rate, 900.0, 2500.0)
    beep_env = envelope(beep_band, rate)
    print_env = envelope(print_band, rate)

    clerk = [(t - t0, text) for t, kind, text in events if kind == "clerk"]
    beeps = [t for t, text in clerk if text == "sound=beep"]
    prints = [t for t, text in clerk if text == "sound=print"]
    variant = "stay"
    for t, kind, text in events:
        m = re.search(r"setup variant=(\w+)", text)
        if m:
            variant = m.group(1)
    rows = []
    near_levels, far_levels = [], []
    for index, t in enumerate(beeps):
        found, height, floor = onset_near(beep_env, rate, t, 0.05, 0.20)
        lag = (found - t) * 1000.0 if not math.isnan(found) else float("nan")
        state = player_state_at(events, t + t0)
        dist = state["dist"] if state else -1.0
        rows.append(("beep", t, found, height))
        ok = not math.isnan(found) and -40.0 <= lag <= 70.0
        # 가게를 나간 뒤의 삑은 작고 먹먹해서 못 찾아도 된다. 그때는 크기만 잰다.
        if dist < 650.0:
            check(ok, f"beep_{index}_heard_on_cue", f"lag_ms={lag:.0f} level_db={db(height):.1f} dist={dist:.0f}")
            near_levels.append(db(height))
        else:
            far_levels.append(db(height))
            print(f"STORE_AUDIO_NOTE beep_{index}_far dist={dist:.0f} level_db={db(height):.1f} lag_ms={lag:.0f}")
    if variant != "leave":
        check(len(beeps) >= 6, "terminal_and_print_taps", f"beeps={len(beeps)}")
    for index, t in enumerate(prints):
        found, height, floor = onset_near(print_env, rate, t, 0.05, 0.25)
        lag = (found - t) * 1000.0 if not math.isnan(found) else float("nan")
        rows.append(("print", t, found, height))
        check(not math.isnan(found) and -40.0 <= lag <= 90.0, f"print_{index}_heard_on_cue",
              f"lag_ms={lag:.0f} rise_db={db(height) - db(max(floor, 1e-6)):.1f}")
        # 출력 소리가 1.4초 남짓 이어지는지(글줄 구간). 녹음 1.40초에 커터.
        a, b = int(t * rate), int((t + 1.62) * rate)
        seg = print_env[a:b]
        if len(seg) > 10:
            loud = float(np.mean(seg > max(floor * 3.0, float(np.max(seg)) * 0.12))) * 1.62
            check(loud > 0.7, f"print_{index}_lasts", f"audible_seconds={loud:.2f}")
        # 손님 자리에서 프린터는 어느 귀 쪽인가. 화면 방향(yaw)과 프린터 자리로 기대 방향을 잡는다.
        state = player_state_at(events, t + t0)
        if state:
            printer = (2581.5, -201.2)
            dx, dy = printer[0] - state["x"], printer[1] - state["y"]
            yaw = math.radians(state["yaw"])
            # UE 오른쪽 벡터 = (-sin yaw, cos yaw)
            right = -math.sin(yaw) * dx + math.cos(yaw) * dy
            seg_lr = print_band[a:b]
            if len(seg_lr) > 10:
                left_rms = float(np.sqrt(np.mean(seg_lr[:, 0] ** 2)))
                right_rms = float(np.sqrt(np.mean(seg_lr[:, 1] ** 2)))
                balance = db(right_rms) - db(left_rms)
                expect_right = right > 0
                if abs(right) > 15.0:
                    check((balance > 0.5) == expect_right, f"print_{index}_from_printer_side",
                          f"right_minus_left_db={balance:.1f} printer_on_{'right' if expect_right else 'left'} lateral_cm={right:.0f}")
                else:
                    print(f"STORE_AUDIO_NOTE print_{index}_centered lateral_cm={right:.0f} balance_db={balance:.1f}")
    if variant != "leave":
        check(len(prints) == 1, "printed_once", f"prints={len(prints)}")
    else:
        check(len(prints) == 0, "no_print_sound_when_away", f"prints={len(prints)}")
        if near_levels and far_levels:
            check(max(far_levels) < min(near_levels) - 6.0, "taps_fade_when_outside",
                  f"near_min_db={min(near_levels):.1f} far_max_db={max(far_levels):.1f}")

    # 시트: 0~6 kHz 스펙트로그램과 사건 표시.
    mono = mix.mean(axis=1)
    f, tt, s = signal.spectrogram(mono, fs=rate, nperseg=1024, noverlap=768)
    keep = f <= 6000.0
    s = 10 * np.log10(s[keep] + 1e-12)
    s = np.clip((s - (s.max() - 75.0)) / 75.0, 0, 1)
    width = min(2400, max(800, int(duration * 20)))
    spec = Image.fromarray((np.flipud(s) * 255).astype(np.uint8)).resize((width, 260)).convert("RGB")
    sheet = Image.new("RGB", (width, 420), (14, 14, 16))
    sheet.paste(spec, (0, 40))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("C:/Windows/Fonts/malgun.ttf", 12)
    except OSError:
        font = ImageFont.load_default()
    colours = {"beep": (110, 200, 255), "print": (255, 170, 80), "pull": (255, 120, 120), "place": (180, 255, 140)}
    for t, text in clerk:
        kind = text.replace("sound=", "")
        if kind not in colours:
            continue
        x = int(t / duration * width)
        draw.line([(x, 40), (x, 300)], fill=colours[kind])
    y = 305
    for t, kind, text in events:
        if kind != "av" or t < t0:
            continue
        if text.startswith(("dialogue speaker", "caption \"", "receipt_shown", "clerk_busy")):
            x = int((t - t0) / duration * width)
            label = text.replace("dialogue speaker=", "").split(" line=")[0] if text.startswith("dialogue") else text[:18]
            draw.line([(x, 300), (x, 310)], fill=(220, 220, 220))
            draw.text((x + 2, y + (hash(text) % 5) * 20), label[:14], fill=(220, 220, 210), font=font)
    draw.text((6, 6), f"store counter mix {duration:.1f}s  (파랑 삑, 주황 출력, 빨강 뜯기, 초록 놓기)", fill=(230, 230, 220), font=font)
    os.makedirs(out_dir, exist_ok=True)
    sheet_path = os.path.join(out_dir, "store_audio_sheet.png")
    sheet.save(sheet_path)
    print(f"STORE_AUDIO_SHEET {sheet_path}")
    print(f"STORE_AUDIO_CHECKS failures={failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
