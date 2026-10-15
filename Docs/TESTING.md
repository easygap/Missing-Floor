# 테스트 도와주기

Missing Floor는 정식 출시 전에 두 가지를 확인해야 합니다. 여러 PC에서 부드럽게
도는지, 그리고 처음 하는 사람이 설명 없이도 끝까지 갈 수 있는지입니다. 개발 PC
한 대로는 알 수 없는 것들이라 도움을 부탁드립니다.

받은 ZIP의 압축을 모두 풀면 「테스트 도구」 폴더가 있습니다. 아래 두 가지 중
할 수 있는 것만 해 주셔도 큰 도움이 됩니다.

## 1. 성능 측정 (15분 정도)

밤 장면을 정해진 경로로 세 번 돌며 프레임 시간을 잽니다. 게임을 직접 조작할
필요는 없습니다.

1. 다른 프로그램을 닫고, 노트북이면 전원을 연결합니다.
2. 「테스트 도구」 폴더에서 **성능 측정 - 높음.bat**을 실행합니다.
   그래픽카드가 GTX 1060이나 RX 580처럼 몇 해 지난 것이면 **성능 측정 - 낮음.bat**을 실행해 주세요.
3. 게임 창이 세 번 열렸다 닫힙니다. 끝나면 결과가 화면에 나오고, 바탕 화면에
   `MissingFloor-performance-날짜.zip`이 생깁니다.
4. [성능 측정 결과](https://github.com/easygap/Missing-Floor/issues/new?template=performance_report.yml)에
   그 ZIP을 끌어다 놓아 보내 주세요.

특히 다음 PC의 결과가 필요합니다.

| 그래픽카드 | Windows | 실행할 측정 |
|---|---|---|
| GTX 1060 6GB 또는 비슷한 것 | 10, 11 | 낮음 |
| RX 580 8GB 또는 비슷한 것 | 10, 11 | 낮음 |
| RTX 2060 또는 비슷한 것 | 11 | 높음 |
| RX 6600 또는 비슷한 것 | 11 | 높음 |

다른 PC의 결과도 반갑습니다. 결과는 [성능 기준](PERFORMANCE.md)과 견줘 봅니다.

## 2. 처음 플레이 기록 (오래 걸리니 나눠서 해도 됩니다)

이 게임을 처음 하는 분께 부탁드립니다. 공략이나 다른 사람의 영상을 보지 않고
평소처럼 플레이해 주세요.

1. 「테스트 도구」 폴더에서 **플레이테스트로 시작.bat**을 실행합니다.
2. 새 게임으로 시작해 할 수 있는 데까지 플레이합니다. 중간에 꺼도 됩니다.
   다시 이어 할 때도 같은 파일로 실행해 주세요. 실행할 때마다 기록 파일이 하나씩 생깁니다.
3. 게임을 끄면 기록 폴더가 열립니다. `playrecord-날짜.json` 파일을 모두
   [처음 플레이 기록](https://github.com/easygap/Missing-Floor/issues/new?template=playtest_record.yml)에
   끌어다 놓고 짧은 질문에 답해 주세요.

헤드폰을 쓰면 위층 사람의 위치를 소리로 더 잘 들을 수 있습니다.

## 기록에 담기는 것

성능 측정 ZIP에는 CPU·그래픽카드 이름과 드라이버 버전, 메모리 크기, Windows 버전,
전원 설정, 게임 설정과 프레임 시간이 들어갑니다(dxdiag 사양 파일 포함).
플레이 기록에는 밤마다 걸린 시간, 붙잡힌 횟수, 5초마다의 게임 속 위치, 기록 화면과
힌트를 연 횟수, 앉기·문 조용히 열기 같은 동작을 처음 쓴 때, 프레임 시간, 사양이
들어갑니다. 이름, 계정, 사진, 녹음은 들어가지 않습니다. 기록은 이 PC에만 남고,
보내 주신 파일만 확인합니다.

## 모인 결과

보내 주신 성능 측정 ZIP은 `Scripts/summarize_performance_reports.py`로 모아
[성능 기준](PERFORMANCE.md)과, 플레이 기록은 `Scripts/summarize_playtest_records.py`로 모아
[출시 기준](MISSING_FLOOR_ACCEPTANCE.md)과 비교합니다.

## Help test the game (English)

Open the 테스트 도구 (Test Tools) folder in the extracted ZIP.

- **Performance check (about 15 minutes):** run `성능 측정 - 높음.bat` (High) or, on older
  graphics cards such as a GTX 1060 or RX 580, `성능 측정 - 낮음.bat` (Low). The game runs a
  fixed night route three times and writes `MissingFloor-performance-<date>.zip` to your
  desktop. Attach it to a [performance report](https://github.com/easygap/Missing-Floor/issues/new?template=performance_report.yml).
- **First playthrough (first-time players only; it takes a while, so feel free to split it up):** start the game with
  `플레이테스트로 시작.bat` and play without a walkthrough. You can stop whenever you like;
  use the same file when you return. Each session creates a new record. When you quit,
  the PlayRecords folder opens. Attach all your `playrecord-<date>.json` files to a
  [playtest record](https://github.com/easygap/Missing-Floor/issues/new?template=playtest_record.yml).

The performance ZIP contains your PC specifications, driver and Windows versions,
power settings, game settings, and frame times, including a dxdiag report. Playtest
records include time spent on each night, how often you get caught, your in-game position every five
seconds, journal and hint use, when you first use certain controls, frame times, and
PC specifications. They do not record your name, account, photos, or audio. Files stay
on your PC until you choose to share them.
