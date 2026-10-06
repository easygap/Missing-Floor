"""유담의 숨소리를 여성 음역으로 옮긴다.

유담은 27세 여성인데 숨소리와 헐떡임이 남자 녹음이었다(OpenGameArt Owlish Media
팩의 scared-breathing·gasp1·freakedbreath). WORLD 보코더로 소리를 기본 주파수(F0),
스펙트럼 포락, 비주기성으로 나눈 뒤

- 목소리가 울리는 구간의 F0를 1.45~1.55배 올리고(남성 130~150 Hz → 여성 210~230 Hz)
- 포락의 주파수 축을 1.17~1.18배 늘려 성도를 짧게 만든다(여성의 포먼트가 15~20% 높다).

숨은 대부분 바람 소리라 포락이 성별을 가른다. F0만 올리면 다람쥐 목소리가 되고,
포락만 올리면 몸집 작은 남자가 된다. 둘을 같이 옮겨야 사람이 바뀐다. 다시 지은 뒤에는
원본의 크기 흐름을 되돌려 놓아 기존 믹스가 그대로 맞는다.

curate_cc0_audio.py가 원본 팩에서 Player_* 를 만들 때 이 모듈의 feminize()를 거친다.
원본 팩이 없을 때는 이미 다듬어 둔 남자 숨을 그대로 옮길 수 있다. 한 번 옮긴 파일은
manifest의 "voice_shift"가 표시하므로 두 번 옮겨지지 않는다.

    python Scripts/feminize_player_voice.py --audio Content/SourceArt/Audio

필요한 것: numpy, scipy, pyworld(pip install pyworld)
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import wave

import numpy as np

SR = 48000

# 숨 루프와 헐떡임. 파일 전체를 옮긴다.
PLAYER_VOICE = {
    "Player_Breath_Scared": {"f0": 1.45, "formant": 1.17, "loop": True},
    "Player_Gasp": {"f0": 1.55, "formant": 1.18, "loop": False},
}

# 붙잡힐 때(Entity_Grab)의 숨은 원래 84 Hz 언저리의 굵은 남자 숨이라 옮겨도 남자로
# 들린다. 손이 어깨를 잡는 충격(앞 0.22초)만 남기고, 그 뒤는 위에서 옮긴 유담의
# 헐떡임과 숨으로 새로 짠다.
GRAB_HEAD_SECONDS = 0.22
GRAB_HEAD_DECAY_SECONDS = 0.25
GRAB_GASP = {"offset": 0.12, "gain": 1.0, "length": 0.9}
GRAB_BREATH = {"offset": 0.60, "gain": 0.55, "start": 1.0, "length": 3.1, "rate": 1.08}

# 이보다 작은 프레임은 음높이를 버린다(가장 큰 프레임 기준 dB).
GATE_DB = -32.0
# 5 ms 프레임 8개(40 ms)보다 짧은 유성 구간은 숨으로 친다.
MIN_VOICED_FRAMES = 8
# 옮긴 뒤에도 150 Hz 아래로 긁히는 소리(목 긁는 소리)는 숨으로 바꾼다. 남자 앓는
# 소리처럼 들리는 건 대부분 이 구간이다.
MIN_SHIFTED_F0 = 150.0
# 이 주파수 위로는 바람 소리를 남긴다(두 배 지점에서 BREATHY_FLOOR에 닿는다).
BREATHY_FROM_HZ = 500.0
BREATHY_FLOOR = 0.92
# 루프 이음매를 섞는 길이와 크기 흐름을 맞추는 창.
LOOP_SEAM_SECONDS = 0.05
ENVELOPE_WINDOW_SECONDS = 0.05


def _require_pyworld():
    try:
        import pyworld  # noqa: F401
    except ImportError as error:  # pragma: no cover - 설치 안내
        raise SystemExit("pyworld가 필요하다: pip install pyworld") from error
    import pyworld
    return pyworld


def _warp(matrix: np.ndarray, ratio: float) -> np.ndarray:
    """주파수 축을 ratio배 늘린다. 프레임마다 선형 보간한다."""
    bins = matrix.shape[1]
    axis = np.arange(bins, dtype=np.float64)
    source = np.clip(axis / ratio, 0.0, bins - 1)
    out = np.empty_like(matrix)
    for index in range(matrix.shape[0]):
        out[index] = np.interp(source, axis, matrix[index])
    return out


def _gate_quiet_frames(signal: np.ndarray, f0: np.ndarray, times: np.ndarray, sr: int) -> np.ndarray:
    """숨 사이 잡음에서 잡힌 음높이는 버린다.

    harvest는 방 잡음에서도 가끔 음높이를 찾는다. 그 프레임을 그대로 올리면 숨 사이에
    웅웅거리는 소리가 새로 생긴다. 가장 큰 프레임보다 GATE_DB 이상 작은 프레임은
    무성음으로 합성한다.
    """
    half = int(0.0125 * sr)
    levels = np.empty(times.size)
    for index, time in enumerate(times):
        center = int(time * sr)
        frame = signal[max(0, center - half):center + half]
        levels[index] = float(np.sqrt(np.mean(frame ** 2))) if frame.size else 0.0
    floor = levels.max() * 10.0 ** (GATE_DB / 20.0)
    gated = f0.copy()
    gated[levels < floor] = 0.0
    return gated


def _keep_breathy(aperiodicity: np.ndarray, sr: int) -> np.ndarray:
    """숨에 섞인 목소리를 숨결로 남긴다.

    원본 숨의 목소리는 바람 소리 밑에 깔린 기본음 하나뿐이다. WORLD는 이걸 배음이
    가득한 유성음으로 다시 짓기 때문에 그대로 두면 숨이 '흐응' 하는 콧소리가 된다.
    BREATHY_FROM_HZ 위로는 비주기성을 BREATHY_FLOOR 아래로 내리지 않아 기본음 근처만
    울리고 나머지는 바람으로 남긴다.
    """
    bins = aperiodicity.shape[1]
    freqs = np.linspace(0.0, sr / 2.0, bins)
    ramp = np.clip((freqs - BREATHY_FROM_HZ) / BREATHY_FROM_HZ, 0.0, 1.0) * BREATHY_FLOOR
    return np.maximum(aperiodicity, ramp[np.newaxis, :])


def _smooth_voiced_runs(f0: np.ndarray) -> np.ndarray:
    """음높이 궤적을 고른다.

    숨에 섞인 목소리는 짧고 들쭉날쭉해서 harvest가 프레임마다 수십 Hz씩 튄다. 그대로
    옮기면 떨리는 기계음이 된다. 40 ms보다 짧은 유성 구간은 버리고, 남은 구간은
    중앙값 필터로 튀는 값을 누른다.
    """
    from scipy.signal import medfilt

    out = np.zeros_like(f0)
    voiced = f0 > 0
    index = 0
    while index < f0.size:
        if not voiced[index]:
            index += 1
            continue
        end = index
        while end < f0.size and voiced[end]:
            end += 1
        if end - index >= MIN_VOICED_FRAMES:
            run = f0[index:end]
            out[index:end] = medfilt(run, 5) if run.size >= 5 else run
            out[index:end][out[index:end] <= 0] = run[out[index:end] <= 0]
        index = end
    return out


def feminize(x: np.ndarray, f0_ratio: float = 1.55, formant_ratio: float = 1.18, sr: int = SR) -> np.ndarray:
    """남성 숨·헐떡임을 여성 음역으로 옮긴다. 길이와 RMS는 그대로다."""
    pyworld = _require_pyworld()
    signal = np.ascontiguousarray(x, dtype=np.float64)
    if signal.size < sr // 20:
        return x.astype(np.float32)
    f0, times = pyworld.harvest(signal, sr, f0_floor=60.0, f0_ceil=600.0, frame_period=5.0)
    f0 = _smooth_voiced_runs(_gate_quiet_frames(signal, f0, times, sr))
    envelope = pyworld.cheaptrick(signal, f0, times, sr)
    aperiodicity = pyworld.d4c(signal, f0, times, sr)
    target = f0 * f0_ratio
    target[target < MIN_SHIFTED_F0] = 0.0
    shifted = pyworld.synthesize(
        target,
        _warp(envelope, formant_ratio),
        _keep_breathy(_warp(aperiodicity, formant_ratio), sr),
        sr,
        5.0)
    shifted = shifted[:signal.size]
    if shifted.size < signal.size:
        shifted = np.pad(shifted, (0, signal.size - shifted.size))
    source_rms = float(np.sqrt(np.mean(signal ** 2))) + 1e-12
    shifted_rms = float(np.sqrt(np.mean(shifted ** 2))) + 1e-12
    shifted *= source_rms / shifted_rms
    return shifted.astype(np.float32)


def feminize_loop(x: np.ndarray, f0_ratio: float, formant_ratio: float, sr: int = SR) -> np.ndarray:
    """이음매 없는 루프를 옮긴다. 세 번 이어 붙여 옮기고 가운데 한 바퀴만 쓴다.

    가운데 바퀴를 잘라도 끝과 시작은 맞물리지 않는다. 합성은 음높이의 위상과 잡음을
    처음부터 이어 가기 때문에 둘째 바퀴의 시작과 셋째 바퀴의 시작이 다른 파형이다.
    그래서 가운데 바퀴의 앞 LOOP_SEAM_SECONDS를 셋째 바퀴의 시작과 맞바꿔 섞는다.
    루프가 돌아올 때 끝 다음에 오는 소리가 원래 그 뒤에 이어지던 소리가 된다.
    """
    n = x.size
    tiled = np.concatenate([x, x, x])
    shifted = feminize(tiled, f0_ratio, formant_ratio, sr)
    loop = shifted[n:2 * n].copy()
    seam = min(int(LOOP_SEAM_SECONDS * sr), n // 4)
    phase = np.linspace(0.0, np.pi / 2.0, seam, dtype=np.float64)
    loop[:seam] = shifted[n:n + seam] * np.sin(phase) + shifted[2 * n:2 * n + seam] * np.cos(phase)
    return loop.astype(np.float32)


def _short_rms(x: np.ndarray, window: int, wrap: bool) -> np.ndarray:
    """창 길이 window의 이동 RMS. wrap이면 루프처럼 끝과 시작을 잇는다."""
    power = x.astype(np.float64) ** 2
    kernel = np.ones(window) / window
    if wrap:
        padded = np.concatenate([power[-window:], power, power[:window]])
        return np.sqrt(np.convolve(padded, kernel, mode="same")[window:-window] + 1e-12)
    return np.sqrt(np.convolve(power, kernel, mode="same") + 1e-12)


def match_envelope(shifted: np.ndarray, source: np.ndarray, loop: bool, sr: int = SR) -> np.ndarray:
    """옮긴 숨의 크기 흐름을 원본에 맞춘다.

    WORLD로 다시 지으면 큰 숨은 그대로인데 작은 숨이 5~7 dB씩 죽는다. 원래 믹스는
    남자 숨의 흐름에 맞춰 둔 것이라 50 ms 이동 RMS의 비율로 원본의 흐름을 되돌린다.
    """
    window = int(ENVELOPE_WINDOW_SECONDS * sr)
    gain = _short_rms(source, window, loop) / _short_rms(shifted, window, loop)
    gain = np.clip(gain, 0.25, 4.0)
    out = shifted * gain.astype(np.float32)
    peak = float(np.max(np.abs(out)))
    if peak > 0.98:
        out *= 0.98 / peak
    return out


def _speed(x: np.ndarray, rate: float) -> np.ndarray:
    """재생 속도를 rate배로. 숨이 급해지는 만큼 음높이도 조금 오른다."""
    positions = np.arange(0.0, x.size - 1, rate)
    return np.interp(positions, np.arange(x.size), x).astype(np.float32)


def rebuild_grab(original: np.ndarray, gasp: np.ndarray, breath: np.ndarray, sr: int = SR) -> np.ndarray:
    """어깨를 잡는 충격은 원본에서, 그 뒤의 거친 숨은 유담의 숨으로 짠다."""
    head = int(GRAB_HEAD_SECONDS * sr)
    decay = int(GRAB_HEAD_DECAY_SECONDS * sr)
    impact = original[:head + decay].astype(np.float32).copy()
    impact[head:] *= np.linspace(1.0, 0.0, impact.size - head, dtype=np.float32) ** 2
    gasp_part = gasp[:int(GRAB_GASP["length"] * sr)] * GRAB_GASP["gain"]
    start = int(GRAB_BREATH["start"] * sr)
    breath_part = _speed(breath[start:start + int(GRAB_BREATH["length"] * sr)], GRAB_BREATH["rate"])
    tail = int(0.4 * sr)
    breath_part[-tail:] *= np.linspace(1.0, 0.0, tail, dtype=np.float32)
    breath_part *= GRAB_BREATH["gain"]
    length = max(original.size, int(GRAB_BREATH["offset"] * sr) + breath_part.size)
    out = np.zeros(length, dtype=np.float32)
    out[:impact.size] += impact
    for part, offset in ((gasp_part, GRAB_GASP["offset"]), (breath_part, GRAB_BREATH["offset"])):
        begin = int(offset * sr)
        out[begin:begin + part.size] += part[:max(0, length - begin)]
    return out[:original.size]


def _read(path: str) -> np.ndarray:
    with wave.open(path, "rb") as handle:
        if handle.getframerate() != SR or handle.getnchannels() != 1 or handle.getsampwidth() != 2:
            raise SystemExit(f"48 kHz 모노 16비트가 아니다: {path}")
        data = handle.readframes(handle.getnframes())
    return np.frombuffer(data, dtype="<i2").astype(np.float32) / 32767.0


def _write(path: str, samples: np.ndarray) -> None:
    clipped = np.clip(samples, -1.0, 1.0)
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SR)
        handle.writeframes((clipped * 32767.0).astype("<i2").tobytes())


def _peak_normalize(x: np.ndarray, peak: float) -> np.ndarray:
    current = float(np.max(np.abs(x))) + 1e-12
    return x * (peak / current)


def apply(audio_dir: str) -> int:
    """audio_dir의 Player_* 와 Entity_Grab을 옮긴다. 옮긴 파일 수를 돌려준다."""
    manifest_path = os.path.join(audio_dir, "manifest.json")
    with open(manifest_path, encoding="utf-8") as handle:
        manifest = json.load(handle)
    changed = 0
    entries = {entry["name"]: entry for entry in manifest["sounds"]}
    for name, recipe in PLAYER_VOICE.items():
        entry = entries[name]
        if entry.get("voice_shift"):
            print(f"skip {name}: 이미 옮겼다")
            continue
        path = os.path.join(audio_dir, entry["file"])
        source = _read(path)
        if recipe["loop"]:
            result = feminize_loop(source, recipe["f0"], recipe["formant"])
        else:
            result = feminize(source, recipe["f0"], recipe["formant"])
        _write(path, match_envelope(result, source, recipe["loop"]))
        entry["voice_shift"] = {"f0": recipe["f0"], "formant": recipe["formant"], "tool": "WORLD (pyworld)"}
        changed += 1
        print(f"{name}: F0 x{recipe['f0']}, 포락 x{recipe['formant']}")
    grab = entries["Entity_Grab"]
    if grab.get("voice_shift"):
        print("skip Entity_Grab: 이미 다시 짰다")
    else:
        path = os.path.join(audio_dir, grab["file"])
        original = _read(path)
        gasp = _read(os.path.join(audio_dir, entries["Player_Gasp"]["file"]))
        breath = _read(os.path.join(audio_dir, entries["Player_Breath_Scared"]["file"]))
        result = rebuild_grab(original, gasp, breath)
        _write(path, _peak_normalize(result, float(np.max(np.abs(original)))))
        grab["voice_shift"] = {"rebuilt": "어깨 충격 0.22초 + Player_Gasp + Player_Breath_Scared",
                               "tool": "WORLD (pyworld)"}
        changed += 1
        print("Entity_Grab: 충격은 그대로, 숨은 유담의 것으로")
    if changed:
        with open(manifest_path, "w", encoding="utf-8") as handle:
            json.dump(manifest, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
    return changed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--audio", default=os.path.join("Content", "SourceArt", "Audio"))
    args = parser.parse_args()
    apply(args.audio)
    return 0


if __name__ == "__main__":
    sys.exit(main())
