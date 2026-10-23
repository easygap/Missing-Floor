"""게임에서 쓰는 환경음과 효과음만으로 예고편 소리를 짠다. 음악과 나레이션은 없다.

조용한 곳과 큰 소리의 차이가 공포를 만든다. 암전 구간은 비워 두고, 걸음과 노크,
붙잡히는 소리 뒤의 정적에 무게를 둔다. 현관 너머의 목소리는 게임처럼 철문에 걸러진
웅얼거림이고, 말은 자막이 맡는다. 마지막 노크는 게임의 가족 노크(둘, 쉬고, 하나)다.

    python Scripts/Trailer/assemble.py --marks
    python Scripts/Trailer/compose.py
"""
from pathlib import Path
import json
import os
import subprocess
import sys
import wave
import numpy as np
from scipy.signal import butter, sosfilt

ROOT = Path(__file__).resolve().parents[2]
# 예고편 만들기(Build-Trailer.ps1)는 실행 폴더를 IG_TRAILER_DIR로 넘긴다. 따로 돌릴 때도 쓸 실행 폴더를
# 지정한다. 예전에는 비워 두면 Saved/Trailer 한 곳에 덮어써서 어느 촬영의 결과인지 알 수 없었다.
if not os.environ.get('IG_TRAILER_DIR'):
    sys.exit('IG_TRAILER_DIR에 예고편 실행 폴더(Saved/Trailer/Runs/<시각>)를 지정해 주세요.')
HERE = Path(os.environ['IG_TRAILER_DIR'])
AUDIO = ROOT / 'Content/SourceArt/Audio'
SR = 48000
EDL = json.loads((HERE / 'edl.json').read_text(encoding='utf-8'))
T = EDL['marks']
MIX = np.zeros((round(EDL['length'] * SR), 2), dtype=np.float32)


def load(name):
    with wave.open(str(AUDIO / f'{name}.wav')) as w:
        if w.getsampwidth() != 2 or w.getframerate() != SR:
            raise ValueError(f'48kHz PCM16 파일이 필요합니다: {name}')
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768
        return x.reshape(-1, w.getnchannels()).mean(axis=1)


def put(name, start, gain, duration=None, pan=0.0, fade=0.04, lowpass=None):
    x = load(name)
    if duration is not None:
        n = round(duration * SR)
        x = np.tile(x, int(np.ceil(n / len(x))))[:n]
    if lowpass:
        x = sosfilt(butter(3, lowpass, fs=SR, output='sos'), x)
    k = min(round(fade * SR), len(x) // 2)
    if k:
        x[:k] *= np.linspace(0, 1, k)
        x[-k:] *= np.linspace(1, 0, k)
    i = round(start * SR)
    n = min(len(x), len(MIX) - i)
    angle = (pan + 1) * np.pi / 4
    MIX[i:i + n] += np.column_stack((x[:n] * np.cos(angle), x[:n] * np.sin(angle))) * gain


def steps(start, end, interval, gain, surface='Concrete', count=5, pan=0.0):
    t, index = start, 0
    while t < end:
        put(f'Foot_{surface}_{index % count}', t, gain * (0.9 + 0.2 * ((index * 7) % 3) / 2), pan=pan)
        t += interval
        index += 1


def write(path, data):
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((np.clip(data, -1, 1) * 32767).astype('<i2').tobytes())


def murmur(start, duration, gain, seed):
    """철문 너머의 말소리. 게임의 CreateDoorMurmur처럼 낱말이 안 들리는 웅얼거림이다."""
    rng = np.random.default_rng(seed)
    n = round(duration * SR)
    noise = rng.standard_normal(n).astype(np.float32)
    voiced = sosfilt(butter(2, (170, 780), btype='bandpass', fs=SR, output='sos'), noise)
    # 음절 박자(초당 4~5번)로 올라갔다 내려간다. 끝은 말끝처럼 처진다.
    t = np.arange(n) / SR
    syllables = 0.5 + 0.5 * np.sin(2 * np.pi * (4.3 + 0.6 * np.sin(2 * np.pi * 0.7 * t)) * t) ** 2
    phrase = np.minimum(1.0, t / 0.18) * np.minimum(1.0, (duration - t) / 0.5)
    x = voiced * syllables * phrase
    x /= max(1e-6, float(np.max(np.abs(x))))
    i = round(start * SR)
    m = min(n, len(MIX) - i)
    MIX[i:i + m] += np.column_stack((x[:m], x[:m])) * gain


# 새벽 네 시 반의 403호. 첫 화면부터 방이고, 천장의 노크가 먼저 온다.
put('Bed_Corridor', 0.0, 0.06, duration=T['alley'] - 0.7, fade=0.5, lowpass=900)
put('Entity_KnockTriple_Muffled', T['first_knock'], 0.95)
put('Settle_Creak_0', T['creak'], 0.22, pan=0.15, lowpass=2500)

# 전날 저녁의 빌라. 골목의 도시 소리, 복도를 걷는 발소리, 관리실의 안정기.
put('Bed_City_Night', T['alley'], 0.44, duration=T['day'] - T['alley'], fade=0.35)
put('Wind_Gap', T['alley'] + 0.5, 0.10, duration=3.5, fade=0.8)
put('Bed_Corridor', T['day'], 0.30, duration=T['booth'] - T['day'], fade=0.25)
steps(T['day'] + 0.2, T['door402'] - 0.3, 0.52, 0.17)
put('Settle_Creak_1', T['door402'] + 1.8, 0.12, pan=-0.3)
put('Ballast_Tick', T['booth'] + 0.6, 0.30, pan=-0.2)
put('Hum_Machine', T['booth'], 0.07, duration=T['door403'] - T['booth'] - 0.6, fade=0.4, lowpass=900)

# 새벽의 현관. 철문을 손등으로 빠르게 두드리고, 문밖에서 아는 목소리가 웅얼거린다.
put('Bed_Corridor', T['door403'], 0.10, duration=T['night'] - T['door403'], fade=0.5, lowpass=700)
for index, offset in enumerate((0.0, 0.38, 0.74)):
    put(f'Knock_Steel_{index}', T['door_knock'] + offset, 0.62, pan=0.05)
murmur(T['door_voice'], 3.6, 0.20, seed=24)
put('Player_Breath_Scared', T['door_voice'] + 2.0, 0.08, duration=2.6, fade=0.5)

# 불 꺼진 새벽의 복도. 발소리가 느려지고 숨이 가빠진다.
put('Bed_Corridor', T['night'], 0.18, duration=T['eoduksini'] - T['night'], fade=0.4, lowpass=2200)
put('Hum_Machine', T['night'], 0.05, duration=T['eoduksini_end'] - T['night'], fade=0.6, lowpass=700)
steps(T['night'] + 0.3, T['eoduksini'] - 0.4, 0.74, 0.09)
put('Entity_KnockTriple', T['night'] + 2.6, 0.26, pan=-0.55, lowpass=3000)

# 어둑시니. 쳐다볼수록 커지며 다가온다. 바람 같은 숨이 깊어지다가 다 빠져나간다.
put('Wind_Gap', T['eoduksini'] + 0.6, 0.16, duration=4.6, fade=1.4, lowpass=420)
put('Player_Breath_Scared', T['eoduksini'] + 1.5, 0.14, duration=T['eoduksini_end'] - T['eoduksini'] - 1.9, fade=0.4)

# 4층 참의 위층 사람, 그리고 복도 끝에서 기어 오는 걸음. 걸음은 점점 잦아지고 커진다.
for offset, pan in ((0.5, 0.3), (1.4, 0.2), (2.4, 0.35)):
    put(f'Entity_CrawlStep_{int(offset * 10) % 3}', T['stair'] + offset, 0.2, pan=pan)
put('Entity_Breath_Loop', T['stair'], 0.10, duration=T['approach'] - T['stair'] + 0.8, fade=0.5)
put('Bed_Corridor', T['approach'], 0.14, duration=T['grab'] - T['approach'], fade=0.2, lowpass=2200)
t, index = T['approach'] + 0.2, 0
while t < T['grab'] - 0.12:
    progress = (t - T['approach']) / max(0.1, T['grab'] - T['approach'])
    put(f'Entity_CrawlStep_{index % 3}', t, 0.22 + 0.4 * progress, pan=0.12 if index % 2 else -0.12)
    t += 0.62 - 0.30 * progress
    index += 1
put('Player_Breath_Scared', T['approach'] + 0.3, 0.2, duration=T['grab'] - T['approach'] - 0.3, fade=0.1)

# 붙잡힘. 발목을 낚아채는 소리, 바닥에 끌리는 몸, 귓가의 숨. 그가 멈춰 듣는 동안은 조용하다.
# 바닥을 두드리는 노크 하나에 끊기고, 둘째 노크는 검은 화면에서 듣는다.
put('Entity_Grab', T['grab'], 0.66, duration=min(1.25, T['cut'] - T['grab']), fade=0.02)
put('Entity_Breath_Loop', T['grab'] + 0.4, 0.12, duration=max(0.2, T['cut'] - T['grab'] - 0.6), fade=0.2)
put('Knock_Plaster_0', T['cut'], 0.62)
put('Knock_Plaster_1', T['cut'] + 0.42, 0.54)

# 제목 뒤로 위에서 둘, 쉬고, 하나.
put('Bed_Corridor', T['title'], 0.045, duration=EDL['length'] - T['title'], fade=0.8, lowpass=1200)
for offset, name in ((0.0, 'Knock_Plaster_0'), (0.42, 'Knock_Plaster_1'), (1.34, 'Knock_Plaster_2')):
    put(name, T['answer'] + offset, 0.58, lowpass=1600)

peak = float(np.max(np.abs(MIX)))
if peak > 0.89:
    MIX *= 0.89 / peak
raw = HERE / 'trailer_mix.wav'
write(raw, MIX)
base = ['ffmpeg', '-y', '-hide_banner', '-i', str(raw)]
result = subprocess.run(base + ['-af', 'loudnorm=I=-19:TP=-1.5:LRA=14:print_format=json', '-f', 'null', '-'],
                        capture_output=True, text=True, check=True)
report = json.JSONDecoder().raw_decode(result.stderr[result.stderr.rfind('{'):])[0]
(HERE / 'audio-loudness.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
params = ':'.join([f'measured_I={report["input_i"]}', f'measured_TP={report["input_tp"]}',
                   f'measured_LRA={report["input_lra"]}', f'measured_thresh={report["input_thresh"]}',
                   f'offset={report["target_offset"]}'])
subprocess.run(base + ['-af', f'loudnorm=I=-19:TP=-1.5:LRA=14:{params}:linear=true', '-ar', str(SR),
                       '-c:a', 'pcm_s16le', str(HERE / 'trailer_audio.wav')],
               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
print('TRAILER_AUDIO PASS 목표 -19 LUFS / 최대 -1.5 dBTP, 측정 기록 audio-loudness.json')
