"""플레이테스트 기록(playrecord-*.json)을 모아 출시 기준의 사람 검수 항목과 견준다.

기록은 「테스트 도구/플레이테스트로 시작.bat」으로 켠 게임이
%LOCALAPPDATA%/IndieGame/Saved/PlayRecords에 실행마다 하나씩 남긴다. 테스터마다
폴더를 하나 만들어 그 사람이 보낸 파일을 모두 넣고, 그 폴더들의 상위 폴더를 넘긴다.
같은 폴더의 기록은 한 사람으로 합친다(이어서 한 밤은 시간을 더한다).

    python Scripts/summarize_playtest_records.py <기록 폴더> [--markdown]

판정 기준은 Docs/MISSING_FLOOR_ACCEPTANCE.md의 §18.7·§19.9·§20.5다. 기록으로
알 수 없는 것(헤드폰으로 층을 맞혔는지, 입력 지연)은 설문으로 따로 받는다.
"""
import argparse
import glob
import json
import math
import os
import statistics
import sys

# 출시 확인표 §20.5의 처음 하는 사람 중앙값 목표. 0.10.0에서 밤을 침대에서 끝내게 바꾸며 다시 정했다.
# 사람 기록이 없는 설계 목표라 기록이 모이면 다시 본다.
NIGHT_MINUTES = {1: (20, 30), 2: (30, 40), 3: (35, 50), 4: (30, 45)}
CAPTURE_MEDIAN_LIMIT = {1: 1, 4: 3}
STUCK_SECONDS = 600.0
STUCK_RADIUS = 600.0  # 같은 자리로 보는 범위(cm)


def histogram_percentile(histogram, fraction):
    total = sum(histogram)
    if not total:
        return None
    target = fraction * total
    running = 0
    for millis, count in enumerate(histogram):
        running += count
        if running >= target:
            return millis + 0.5
    return len(histogram) - 0.5


def stuck_episodes(trail):
    """진실 수가 그대로인 채 같은 자리 근처에 10분 넘게 머문 구간."""
    episodes = []
    start = 0
    for index in range(1, len(trail) + 1):
        ended = index == len(trail)
        if not ended:
            first, point = trail[start], trail[index]
            moved = math.dist(first[1:4], point[1:4]) > STUCK_RADIUS
            progressed = point[6] != first[6] or point[4] != first[4]
        if ended or moved or progressed:
            span = trail[index - 1][0] - trail[start][0]
            if span >= STUCK_SECONDS:
                anchor = trail[start]
                episodes.append({"from": anchor[0], "seconds": span, "night": anchor[4],
                                 "at": [round(v) for v in anchor[1:4]]})
            start = index
    return episodes


def summarize(records):
    """한 사람의 기록 여러 개(실행마다 하나)를 합친다."""
    nights = {}
    for record in records:
        for night in record.get("nights", []):
            ended = night.get("ended_seconds", -1)
            # 밤 도중에 끈 기록은 그 실행이 끝난 때까지를 그 밤의 시간으로 센다.
            end = ended if ended >= 0 else record.get("duration_seconds", night["started_seconds"])
            entry = nights.setdefault(night["night"], {"minutes": 0.0, "captures": 0, "histogram": [], "finished": False})
            entry["minutes"] += (end - night["started_seconds"]) / 60.0
            entry["captures"] += night.get("captures", 0)
            entry["finished"] |= ended >= 0
            histogram = night.get("frame_ms_histogram", [])
            if len(entry["histogram"]) < len(histogram):
                entry["histogram"] += [0] * (len(histogram) - len(entry["histogram"]))
            for millis, count in enumerate(histogram):
                entry["histogram"][millis] += count
    for entry in nights.values():
        entry["frame_p95"] = histogram_percentile(entry.pop("histogram"), 0.95)
    record = {"events": [e for r in records for e in r.get("events", [])],
              "actions": {}, "settings": {}, "system": records[-1].get("system", {})}
    for r in records:
        for name, value in r.get("actions", {}).items():
            merged = record["actions"].setdefault(name, {"count": 0})
            merged["count"] += value.get("count", 0)
        for key, value in r.get("settings", {}).items():
            if isinstance(value, (int, float)) and key.endswith("_seconds"):
                record["settings"][key] = record["settings"].get(key, 0) + value
            else:
                record["settings"][key] = value
    stuck = [episode for r in records for episode in stuck_episodes(r.get("trail", []))]
    events = record.get("events", [])
    journal_in_day = any(event["type"] in ("first_journal", "journal") for event in events)
    actions = record.get("actions", {})
    endings = sorted({event.get("detail") for event in events if event["type"] == "ending"})
    settings = record.get("settings", {})
    return {
        "nights": nights,
        "journal": journal_in_day,
        "crouch": "crouch" in actions,
        "quiet_door": "quiet_door" in actions,
        "hints": actions.get("hint", {}).get("count", 0),
        "stuck": stuck,
        "endings": endings,
        "difficulty": settings.get("difficulty"),
        "gamepad_share": settings.get("gamepad_seconds", 0) / max(1.0, settings.get("gamepad_seconds", 0) + settings.get("keyboard_seconds", 0)),
        "system": record.get("system", {}),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder")
    args = parser.parse_args(argv)
    paths = sorted(glob.glob(os.path.join(args.folder, "**", "playrecord-*.json"), recursive=True))
    if not paths:
        print("기록 파일이 없습니다.")
        return 2
    grouped = {}
    for path in paths:
        with open(path, encoding="utf-8") as handle:
            grouped.setdefault(os.path.dirname(path), []).append(json.load(handle))
    people = [(os.path.basename(folder) or folder, summarize(records)) for folder, records in sorted(grouped.items())]

    print(f"테스터 {len(people)}명, 기록 파일 {len(paths)}개\n")
    print("| 테스터 | 밤1 | 밤2 | 밤3 | 밤4 | 포획(1/4) | 기록 화면 | 앉기 | 조용히 열기 | 힌트 | 10분 막힘 | 결말 | GPU |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for name, person in people:
        cells = []
        for index in (1, 2, 3, 4):
            night = person["nights"].get(index)
            cells.append(f"{night['minutes']:.0f}분" if night else "-")
        captures = "/".join(str(person["nights"].get(i, {}).get("captures", "-")) for i in (1, 4))
        print(f"| {name} | " + " | ".join(cells) + f" | {captures} | {'예' if person['journal'] else '아니오'} | "
              f"{'예' if person['crouch'] else '아니오'} | {'예' if person['quiet_door'] else '아니오'} | {person['hints']} | "
              f"{len(person['stuck'])} | {', '.join(person['endings']) or '-'} | {person['system'].get('gpu', '-')} |")

    print("\n## 출시 기준과 비교\n")
    total = len(people)
    for index, (low, high) in NIGHT_MINUTES.items():
        values = [p["nights"][index]["minutes"] for _, p in people if index in p["nights"]]
        if values:
            median = statistics.median(values)
            verdict = "통과" if low <= median <= high else "벗어남"
            print(f"- 밤{index} 소요 시간 중앙값 {median:.1f}분 (기준 {low}~{high}분, 기록 {len(values)}명): {verdict}")
        else:
            print(f"- 밤{index} 소요 시간: 기록 없음")
    for index, limit in CAPTURE_MEDIAN_LIMIT.items():
        values = [p["nights"][index]["captures"] for _, p in people if index in p["nights"]]
        if values:
            median = statistics.median(values)
            print(f"- 밤{index} 붙잡힌 횟수 중앙값 {median:g}회 (기준 {limit}회 이하): {'통과' if median <= limit else '벗어남'}")
    stuck = sum(1 for _, p in people if p["stuck"])
    print(f"- 같은 자리에서 10분 넘게 막힌 사람 {stuck}명 (기준 1명 이하): {'통과' if stuck <= 1 else '벗어남'}")
    for key, label in (("crouch", "앉기"), ("quiet_door", "문 조용히 열기"), ("journal", "기록 화면 열기")):
        used = sum(1 for _, p in people if p[key])
        need = math.ceil(total * 0.8)
        print(f"- {label}를 스스로 쓴 사람 {used}/{total}명 (기준 5명 중 4명 비율): {'통과' if used >= need else '벗어남'}")
    if total < 5:
        print(f"\n처음 하는 사람 5명이 기준이다. 지금은 {total}명이라 판정을 확정하지 않는다.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
