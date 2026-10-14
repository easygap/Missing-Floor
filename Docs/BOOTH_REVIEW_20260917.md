# 관리실 소품과 조사 동작 재점검 — 2026-09-17

편의점, 집, 복도, 관리실, 옥상의 21개 시점을 다시 찍었다. 관리실은 낮과 둘째 밤에 각각 8개 시점을 더 확인했다. 지난번 캡처에 있던 문제를 기준으로 비교했다.

가장 큰 문제는 관리실 접수철이었다. 검은 책을 문지르면 대사만 바뀌었고, 연필도 복원되는 글씨도 없었다. 가까운 기록이 불안 연출 때문에 흐려지는 문제와 현관문 바로 앞에서 조작 안내가 화면 밖으로 밀리는 문제도 고쳤다.

| 대상 | 수정 전 | 수정 후 |
| --- | --- | --- |
| 민원처리대장 | 표지가 비어 있고 종이 재질이 책 전체에 씌워져 있었다. | 국내 A4 장부를 참고해 천 책등, 모서리 보강, 종이 단면과 표지를 다시 만들었다. |
| 민원 접수철 | 두꺼운 검은 책이 놓여 있었다. | 뜯긴 윗장, 철심, 얇은 종이와 받침이 있는 접수철로 교체했다. 옆에는 육각 연필을 놓았다. |
| 복원 동작 | 세 번 조사하는 동안 물건은 그대로였다. | 문지르는 진행에 맞춰 흑연과 눌린 글씨가 세 구간으로 나타난다. 손을 떼면 소리가 멎고, 같은 조사 중에 남긴 흔적부터 이어서 작업한다. |
| 완료한 기록 | 다시 볼 때도 문지르는 동작을 거쳤다. | 즉시 다시 읽는다. 저장된 증거로 장면을 복구할 때도 글씨가 드러난 상태를 되살린다. |
| 압박과 단서 | 단계별 대사만으로 날짜를 알려 줬다. | 실제 접수 내용과 처리 장부를 비교한다. 작업 중에는 짧은 간격으로 소음을 전달해, 중간에 멈추는 선택에도 의미를 줬다. |
| 초점 | 불안 효과가 초점을 7~24m에 고정해 가까운 단서까지 흐렸다. | 불안 효과의 강제 초점 이동을 없앴다. 주변 시야, 색 변화와 심박은 유지했다. |
| 문 조작 안내 | 문을 가까이 보면 안내가 화면 아래로 잘렸다. | 글자 높이와 하단 조작 안내의 여백을 계산해 화면 안에 둔다. |

부분 진행은 같은 장면에서 손을 떼었다가 다시 조사할 때 이어진다. 게임을 종료한 뒤 복구하는 것은 **복원을 끝내 등록한 증거**다.

## 물건과 사건을 연결하기

접수철에는 7월 27일부터 31일까지 401호에서 연락한 내용이 남는다. 처음에는 벽에서 사람이 내는 듯한 소리가 들렸고, 계속 연락했지만 확인은 늦어졌다. 31일에도 소리가 난다는 접수와 장부의 ‘소음 없음. 종결’이 맞지 않는다. 독백은 화면에서 읽은 내용을 짚도록 고쳤다.

이 조사는 둘째 밤부터 할 수 있다. 부동산 문자 사본과 접수철은 어느 쪽을 먼저 읽어도 진행된다. 문서에 남아 있던 ‘셋째 밤에 처음 찾는다’는 설명도 실제 진행과 맞췄다. 기존 저장 파일의 증거 식별자는 유지했다.

[Townfall 개발진 인터뷰(2026-07-30)](https://blog.playstation.com/2026/07/30/silent-hill-townfall-developers-discuss-the-scottish-setting-retro-technology-first-person-combat/)와 [체험 기사(2026-07-29)](https://blog.playstation.com/2026/07/29/silent-hill-townfall-hands-on-report/)에서 장소의 생활 흔적과 손에 잡히는 도구를 조사에 연결하는 방식을 참고했다. 이번에는 관리실에서 쓰일 법한 물건과 기록의 모순을 먼저 맞췄다.

## 사진과 제작 자료

- [알파의 근영사 A4 장부 실물](https://www.alpha.co.kr/product/productView.do?productCode=137700&PS_IDX=): 표지 비율, 천 책등과 모서리 보강을 참고했다.
- [스테들러 Noris 120 실물](https://staedtler.co.kr/product/noris-120-%EB%85%B8%EB%A6%AC%EC%8A%A4-%EC%97%B0%ED%95%84-120/): 육각 몸통, 깎인 나무와 흑연, 끝부분의 구조를 참고했다. 상품 사진을 게임 텍스처로 복사하지 않았다.
- [NIST의 눌린 글씨 정의](https://www.nist.gov/glossary-term/47341), [필적 검사 소개](https://www.nist.gov/news-events/news/2021/06/handwriting-examiners-digital-age), [SWGDE의 사광 촬영 안내](https://www.swgde.org/documents/published-complete-listing/21-p-002-swgde-lighting-techniques-in-forensic-photography/): 윗장을 써서 아래 종이에 압흔이 남는 구조를 확인했다. 흑연으로 칠하는 동작은 게임 속 표현이며, 문서 감정의 권장 절차로 소개하지 않는다.

실물 사진 두 장을 imagegen의 내장 도구에 참고 이미지로 전달했다. 생성한 여러 각도의 참고도로 Blender 모델을 만들고, 별도로 생성한 흑연 질감을 복원 재질에 사용했다.

| 저장 파일 | 용도 |
| --- | --- |
| [BoothStationeryStudy_20260917.png](../Content/SourceArt/AI/BoothStationeryStudy_20260917.png) | 장부·접수철·연필의 여러 방향 참고도 |
| [ComplaintRubbing_20260917.png](../Content/SourceArt/AI/ComplaintRubbing_20260917.png) | 종이 위 흑연 질감 |

한글은 생성 이미지에 맡기지 않았다. 표지와 접수 양식은 별도 인쇄 마스크로 만들었고, 눌린 글씨는 [나눔손글씨 펜](https://github.com/google/fonts/blob/main/ofl/nanumpenscript/METADATA.pb)으로 그렸다. 글꼴 원본과 OFL은 `Scripts/fonts/NanumPenScript/`에 있다. 물체를 통째로 뒤집지 않고 인쇄 UV와 종이 표면 방향을 따로 맞췄다.

장부는 508삼각형, 접수철은 405삼각형, 연필은 82삼각형이다. 장부 표지는 2K 아틀라스, 접수철과 연필은 각각 512·256px 아틀라스를 쓴다. 복원 표면은 평면 한 장과 텍스처 두 장으로 처리하고 충돌·그림자·거리장 계산에서 제외했다. 종이 안으로 인쇄면이 파묻히거나 연필이 박히지 않는지도 실제 배치 높이에서 확인했다.

## 실제 게임 화면

아래는 생성 참고도가 아닌 UE 5.8 게임 실행 캡처다. 확대 사진에는 물건을 읽기 위한 검사 카메라를 썼다. 밤 검사는 둘째 밤 조명을 적용하고 추적자의 이동을 멈춘 상태에서 찍었다.

| 수정 전 책상 | 수정 후 둘째 밤 책상 |
| --- | --- |
| ![검은 책과 인쇄 없는 장부가 놓였던 책상](Media/readme/booth-before-desk.webp) | ![얇은 접수철과 연필, 민원처리대장이 놓인 책상](Media/readme/booth-night-desk-return.webp) |

| 복원 전 | 중단한 지점 | 복원 완료 |
| --- | --- | --- |
| ![비어 있는 접수철 밑장](Media/readme/booth-pad-blank.webp) | ![첫 구간 중간에 멈춘 흑연 흔적](Media/readme/booth-pad-partial.webp) | ![민원 다섯 건이 드러난 밑장](Media/readme/booth-pad-complete.webp) |

[장부 표지](Media/booth-ledger.png) · [밤의 복원 완료 상태](Media/booth-night-pad-complete.png) · [현관문 안내 수정 전](Media/booth-before-door-prompt.png) · [수정 후](Media/spatial-home-entry.png)

넓은 동선에서는 [편의점 상품 뒷면](Media/retail-packaging-back.png), [냉장 진열대](Media/retail-cooler.png), [계산대와 직원](Media/retail-counter.png), [전자레인지](Media/kitchen-microwave.png), [복도 바닥](Media/spatial-corridor-floor.png), [매장 바닥](Media/spatial-store-floor.png), [관리실 출입구](Media/spatial-booth-entry.png), [뒤편 골목](Media/retail-backlane-wide.png), [옥상 밸브](Media/spatial-roof-valves.png)도 다시 확인했다. 이 목록의 모든 물건을 이번에 새로 만든 것은 아니다.

## 렌더링 비교

2026-09-16까지 공개된 UE 5.8 기능과 자료를 기준으로 조사했다. [Lumen 성능 안내](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine), [Virtual Shadow Maps](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine), [TSR](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine), [MegaLights](https://dev.epicgames.com/documentation/unreal-engine/megalights-in-unreal-engine)를 확인했다.

작은 복원 표면에 중복 그림자와 거리장을 만들지 않고, 밉과 텍스처 스트리밍을 사용한다. 기존 성능 우선 설정의 Lumen Lite와 반사 설정은 유지했다. 하드웨어 레이 트레이싱을 쓰지 않는 현재 프로젝트에 MegaLights를 바로 켜지는 않았다.

RTX 3060 8GB·Ryzen 7900X, D3D12, 1920×1080 출력, 프레임 제한과 VSync 해제 상태에서 동일한 12개 시점을 측정했다. 시작 120프레임을 제외했다. 높음은 내부 해상도 100%, 성능 우선은 71%다. 캡처와 측정은 따로 실행했다.

| 조건 | 프레임 중앙값 | 95백분위 | 최대 | 33.33ms 초과 / 표본 |
| --- | ---: | ---: | ---: | ---: |
| [수정 전 높음](Performance/Booth20260917-BeforeHigh/summary.json) | 13.943ms | 16.556ms | 32.291ms | 0 / 4,350 |
| [새 재질의 픽셀 애니메이션 켬, TSR 기본값 2](Performance/Booth20260917-Async2/summary.json) | 14.842ms | 16.918ms | 61.578ms | 3 / 4,168 |
| [위 조건에서 TSR 비동기 연산 3](Performance/Booth20260917-Async3/summary.json) | 15.178ms | 17.348ms | 31.275ms | 0 / 4,087 |
| [픽셀 애니메이션 끔, 첫 측정](Performance/Booth20260917-NoPixelAnimation/summary.json) | 13.457ms | 15.902ms | 30.650ms | 0 / 4,511 |
| [최종 높음 재측정](Performance/Booth20260917-FinalHigh/summary.json) | 14.059ms | 16.381ms | 42.651ms | 1 / 4,356 |
| [최종 성능 우선](Performance/Booth20260917-FinalPerformance/summary.json) | 7.215ms | 10.776ms | 35.487ms | 4 / 7,801 |

TSR 비동기 연산을 3으로 늘리는 변경은 채택하지 않았다. 픽셀 애니메이션을 켠 시험에서는 Basepass 중앙값이 0.959ms까지 늘었고, 끄고 재측정하자 0.281ms로 돌아왔다. 이 재질은 해당 플래그를 끈 상태로 반영한다. 최종 높음의 전체 프레임 중앙값은 수정 전보다 약 0.8% 길고, 95백분위는 약 1.1% 짧다. 결과는 새 소품을 넣은 뒤 기존 성능 수준을 회복한 것으로 본다. 전체 게임이 빨라졌다고 단정하지 않는다.

최종 측정에서도 33.33ms를 넘는 프레임이 있었다. 한 PC의 에디터 게임 실행·고정 시점 이동 결과이며, 모든 PC와 실제 이동 경로의 지연을 보장하는 수치는 아니다. 각 폴더에 전체 CSV의 ZIP, 선택한 프레임과 SHA-256 요약을 보관했다.

## 검증

- UE 5.8 Win64 Development Editor 빌드.
- 전체 진행 검사: `MISSINGFLOOR_GREYBOX PASS`.
- 복원 동작 검사: `BOOTH_RUB_PROGRESS PASS interrupted=1 resumed=1 revealed=1 instant_reread=1 restored=1`.
- 60fps 조작·상호작용 검사: `REALISM_PROBE PASS failures=0`. 제동, 문 통과, 앉은 자세 복구, 가려진 물건의 선택 방지, 문에 따른 포획 판정과 추적자 표시를 확인했다.
- 전체 동선 캡처: `RETAIL_CAPTURE PASS shots=21 production=1 d3d12=1`.
- 관리실 낮·밤 캡처: `Run-BoothReview.ps1`, `Run-BoothReview.ps1 -Night` 모두 `BOOTH_REVIEW PASS shots=8`.
- 저장소 검사: `pwsh -NoProfile -File Scripts/Validate-Project.ps1` 통과. README 이미지와 소품·진행 계약 검사도 포함한다.

수정 전 기준은 `a7505d5b4f7d11185dcdb397becb4534ec0bfa64`다. [검증 로그의 발췌와 해시](Performance/Booth20260917-verification.json)도 함께 남겼다.
