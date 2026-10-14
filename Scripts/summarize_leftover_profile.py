"""남은 짐 경로(관리실 안쪽 방·302호·5층 벽)를 Shipping 배포본으로 돌린 프레임 기록을 요약한다.

Run-LeftoverRuntimeProfile.ps1이 부른다. 밤 장면 경로(summarize_shipping_profile.py)와 같은 원본
형식을 읽고, 검사 단계 번호를 낮1·밤2·밤3·덮개·밤4 구간으로 묶어 구간마다 프레임 시간을 낸다.
이 경로는 장면 사이를 순간 이동으로 건너뛰므로 이동 직후의 스트리밍 멈춤이 섞인다. 그래서
구간 판정은 p95와 1% low로 하고, 50ms를 넘은 프레임 수는 따로 적는다.

    python Scripts/summarize_leftover_profile.py <MissingFloorRuntime.csv> <출력 폴더> --warmup-seconds 20
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from summarize_shipping_profile import CPU_COLUMNS, describe, read_samples  # noqa: E402

# 검사 단계(IGLeftoverProbe의 Phase)를 구간으로 묶는다.
SEGMENTS = (
    ("day1", range(0, 10)),
    ("night2_inner_room", range(10, 20)),
    ("day2", range(20, 30)),
    ("night3_storage", range(30, 40)),
    ("cover_assembly", range(40, 50)),
    ("night4_wall", range(50, 60)),
)


def one_percent_low(times):
    if not times:
        return None
    slow = math.ceil(len(times) * .01)
    return round(1000 / statistics.mean(sorted(times, reverse=True)[:slow]), 3)


def summarize(source, output, warmup_seconds, target_fps):
    rows = read_samples(source, CPU_COLUMNS)
    selected = [row for row in rows if row["Stage"] >= 0 and row["ElapsedSeconds"] >= warmup_seconds
                and row["Paused"] == 0]
    if len(selected) < 100:
        raise ValueError("측정할 프레임이 100개보다 적습니다.")
    budget = 1000 / target_fps
    segments = {}
    gate = True
    for name, stages in SEGMENTS:
        times = [row["FrameTime"] for row in selected if int(row["Stage"]) in stages]
        if not times:
            continue
        frame = describe(times)
        p99 = round(statistics.quantiles(times, n=100, method="inclusive")[98], 3) if len(times) > 1 else frame["max"]
        low = one_percent_low(times)
        passed = frame["p95"] <= budget and low >= (50 if target_fps == 60 else 25)
        # 낮은 이동 장면이 많아 판정에서 뺀다. 새 장면은 밤 구간이다.
        if name.startswith(("night", "cover")):
            gate = gate and passed
        segments[name] = {"FrameTime": frame, "p99": p99, "one_percent_low_fps": low,
                          "frames_over_50ms": sum(time > 50 for time in times),
                          "seconds": round(sum(times) / 1000, 3), "passed": passed}
    times = [row["FrameTime"] for row in selected]
    summary = {
        "scope": "Shipping 오프스크린으로 남은 짐 검사 경로를 실제 시간으로 돌렸다. 장면 사이는 순간 이동이다.",
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "warmup_seconds": warmup_seconds,
        "target_fps": target_fps,
        "analyzed_frames": len(selected),
        "analyzed_seconds": round(sum(times) / 1000, 3),
        "overall": {"FrameTime": describe(times),
                    "p99": round(statistics.quantiles(times, n=100, method="inclusive")[98], 3),
                    "one_percent_low_fps": one_percent_low(times),
                    "frames_over_50ms": sum(time > 50 for time in times)},
        "segments": segments,
        "night_segments_passed": gate,
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "leftover-summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                                  encoding="utf-8")
    for name, metrics in segments.items():
        print("LEFTOVER_PROFILE %-18s p50=%6.2f p95=%6.2f p99=%6.2f low1=%7.2f over50=%d %s" % (
            name, metrics["FrameTime"]["median"], metrics["FrameTime"]["p95"], metrics["p99"],
            metrics["one_percent_low_fps"], metrics["frames_over_50ms"], "PASS" if metrics["passed"] else "FAIL"))
    print("LEFTOVER_PROFILE night_segments=%s" % ("PASS" if gate else "FAIL"))
    return gate


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--warmup-seconds", type=float, default=20)
    parser.add_argument("--target-fps", type=int, choices=(30, 60), default=60)
    args = parser.parse_args()
    summarize(args.csv, args.output, args.warmup_seconds, args.target_fps)


if __name__ == "__main__":
    main()
