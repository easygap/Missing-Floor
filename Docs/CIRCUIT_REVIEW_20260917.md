# 로비 분전반과 첫날 조사 점검 — 2026-09-17

수정 전 기준은 `f2f845987a0a4f63bea0efa068a658bb3f0d0b31`이다. 2026년 9월 16일까지 공개된 자료와 UE 5.8 문서를 기준으로 조사했다.

## 확인한 문제와 수정

계량기함 옆 분전반에는 차단기 그림 위로 4×2.4×5cm 상자 다섯 개가 붙어 있었다. 손잡이를 조작하면 상자 전체가 3cm 이동했다. 낮에는 아무것도 움직이지 않은 채 ‘계전기가 안 물린다’는 독백만 나왔고, 공용 조명 조작은 무시됐다.

분전반을 36×54cm 금속 함체, 다섯 개의 누전차단기, 예비 덮개로 다시 만들었다. 단자와 배선은 덮개 안에 있다. 손잡이는 폭 1.2cm의 별도 메시로 만들고, 같은 자리에서 축을 중심으로 32도씩 돌아가게 했다. 세대 회로의 초기 자세도 켜진 방향으로 맞췄다.

4층 복도의 분전함도 문제가 있었다. 폭 22cm인 401·402호 문 사이에 34cm 함체를 넣은 데다, 앞문을 벽 안쪽에 붙여서 문틀 사이로 민무늬 판만 보였다. 34×50cm 매입형 함으로 교체해 402·403호 사이의 폭 50cm 벽으로 옮겼다. 좌우 문틀까지 각각 9cm와 7cm를 남기고, 문짝은 벽 표면에서 약 8mm 나오게 했다.

낮에도 공용 조명을 끄고 켤 수 있다. 이름 없는 회로를 올리면 0.24초 뒤 손잡이가 떨어지며 두 번째 딸깍 소리가 난다. 실제 복귀 뒤에만 ‘올려도 다시 떨어진다’는 짧은 독백을 띄운다.

공용 전원을 내려도 등갓의 발광 재질은 남아 있던 문제를 함께 고쳤다. 이제 직접 조명과 등갓이 함께 꺼지고, 복도 점멸 타이머나 연출이 전원 차단을 덮어쓰지 않는다. 복구할 때는 현재 낮·밤의 밝기를 따른다.

로비에서 아직 들리지 않는 위층 안정기 소리를 설명하던 독백도 뺐다. 계량기를 살폈을 때는 눈앞의 정지·회전만 짚는다. 위층의 안정기 소리는 그곳에 올라갔을 때 들린다. 공용 회로를 분리하고 무명 회로의 전원 전후를 비교한 뒤 검침표까지 대조하는 기존 퍼즐 조건은 유지했다.

## 실물 자료와 제작

[LS EBS32Fb 실물 제품 사진](https://gs181215.cafe24.com/product/ls%EC%9D%BC%EB%A0%89%ED%8A%B8%EB%A6%AD-ebs32fb-30a-30ma-ce%EC%9D%B8%EC%A6%9D-%EB%B6%84%EC%A0%84%EB%B0%98%EC%9A%A9%EB%88%84%EC%A0%84%EC%B0%A8%EB%8B%A8%EA%B8%B0/27171/)에서 사출 몸체, 전면 덮개, 좁은 토글 홈, 시험 버튼과 손잡이 돌출량을 확인했다. 이 사진을 imagegen 내장 도구의 참고 이미지로 전달해 여러 각도의 시트를 만든 뒤 Blender에서 모델링했다.

- [생성한 모델링 참고 시트](../Content/SourceArt/AI/CircuitPanelReference_20260917.png)
- 실물 출처: [에셋 대장의 참고 자료](ASSET_POLICY.md#생성-시안에-참고한-외부-자료)
- [재현 가능한 Blender 빌더](../Scripts/blender/build_circuit_panel.py)
- 복도 함체는 [KDM의 매입형 외함 실물](https://www.kdmsteel.com/es/caja-electrica-empotrada/)도 참고했다. [생성한 정면·측면·잠금쇠 참고도](../Content/SourceArt/AI/CorridorCabinetReference_20260917.png)를 따로 남겼다.

회로명과 정격은 정확한 글자로 별도 제작했다. 첫 반입에서는 작은 글씨가 함체 전체 아틀라스에 묻혀 뭉개졌다. 인쇄면을 별도 1K 아틀라스로 모으고, 글자가 빠진 함체는 2K에서 1K로 낮췄다. 토글은 256px 아틀라스를 다섯 개가 공유한다. 인쇄 UV만 보정해 한글과 회로의 좌우 배치를 맞췄다.

함체 원본은 3,094삼각형, 인쇄면은 264삼각형, 토글 하나는 284삼각형이다. 반입할 때 기존 메시 예산과 화면 크기에 따른 LOD를 적용한다. 인쇄면과 손잡이는 충돌·그림자·거리장 계산에서 제외했다.

복도 함체는 532삼각형, 512px 아틀라스다. 이쪽도 이름표를 함께 구우면 한글 획이 빠져 보였다. 12삼각형 이름표를 별도 256px 아틀라스로 분리하고, 함체 텍스처를 줄였다. 기존 벽이 충돌을 담당하므로 별도의 충돌체는 만들지 않았다.

## 적용한 개발 자료

[UE 5.8의 Significance Manager 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/significance-manager-in-unreal-engine)은 물체의 중요도에 따라 갱신 빈도를 조절하는 방법을 다룬다. 여기서는 계량기 다섯 개를 위해 별도 플러그인을 추가하지 않고, 기존 디렉터에 거리별 갱신을 넣었다. 2.5m 이내에서는 최대 초당 60회, 6.5m까지는 20회, 그 밖에서는 4회 갱신한다. 멀리서는 각도만 누적하고 메시 변환은 보내지 않는다. 프레임이 늦어져도 누락된 시간만큼 계산하며, 한 프레임에 타이머가 여러 번 몰려 실행되지 않게 했다.

[가시성·오클루전 컬링 안내](https://dev.epicgames.com/documentation/unreal-engine/visibility-and-occlusion-culling-in-unreal-engine)를 참고해 작은 손잡이와 인쇄면에는 별도의 표시 거리를 뒀다. 함체는 더 멀리서 남는다. 텍스처의 밉과 스트리밍, 기존 LOD·TSR 설정도 함께 사용한다.

[Reanimal 체험 기사(2026-02-11)](https://blog.playstation.com/2026/02/11/reanimal-hands-on-a-haunting-co-op-platformer-built-on-trust-timing-and-fear/)에서 설명을 줄이고 환경의 움직임과 소리로 상황을 전달하는 방식을 참고했다. 첫날 조사에서도 회로 조작, 원판의 반응, 위층 소리를 각 장소에서 직접 확인하도록 적용했다.

## 실제 게임 화면

아래는 UE 5.8에서 실행한 화면이다. 생성 참고 시트와 구분한다. 근접 검사는 고정 카메라를 사용했고, 밤에는 첫날 밤 조명을 적용한 뒤 추적자만 멈춰 두었다.

| 수정 전 | 수정 후 |
| --- | --- |
| ![그림 위에 큰 상자 손잡이가 붙어 있던 분전반](Media/readme/circuit-before-front.webp) | ![회로별 차단기와 축이 있는 손잡이](Media/readme/circuit-front.webp) |

![계량기함 옆에 배치한 분전반](Media/readme/circuit-wide.webp)

[측면 두께](Media/circuit-side.png) · [낮에 올린 순간](Media/circuit-day-raised.png) · [다시 떨어진 뒤](Media/circuit-day-reset.png) · [공용 전원 차단](Media/circuit-common-off.png) · [무명 회로 투입](Media/circuit-unnamed-on.png)

| 401·402호 사이에 끼어 있던 함체 | 402·403호 사이로 옮긴 매입형 함 |
| --- | --- |
| ![문틀에 가리고 앞면이 벽 속에 있던 분전함](Media/readme/circuit-before-corridor-front.webp) | ![벽면과 문틀에서 간격을 확보한 분전함](Media/readme/circuit-corridor-front.webp) |

복도 비교 사진은 위치를 옮겼으므로 각 함체의 정면을 따로 찍었다. 로비 전후 사진은 같은 카메라다.

## 다른 구역 재점검

`Run-RetailReview.ps1 -Spatial -Profile Circuit20260917`로 21개 시점을 다시 찍어 확인했다. 새 메시를 만든 곳은 두 분전반이며, 아래 구역은 기존 수정 사항이 현재 실행에서도 유지되는지 점검했다.

| 구역 | 확인한 부분 | 실행 화면 |
| --- | --- | --- |
| 편의점 | 간판·가격표·포장 글자 방향, 앞뒤 상품 표면, 진열대와 바닥 접점 | [진열대](Media/retail-stock.png), [뒤집어 놓은 상품](Media/retail-packaging-back.png), [냉장고](Media/retail-cooler.png) |
| 계산대와 알바생 | 얼굴·목 경계, 좌우 측면의 표면 끊김, 계산대 위 물건 겹침 | [정면](Media/retail-counter.png), [왼쪽](Media/retail-clerk-left.png), [오른쪽](Media/retail-clerk-right.png) |
| 방·복도·매장 | 전자레인지 외함 돌출, 침대 옆 조명 접지, 바닥 줄눈과 문턱 | [전자레인지](Media/kitchen-microwave.png), [침대 옆](Media/bedroom-lamp.png), [복도 바닥](Media/spatial-corridor-floor.png), [매장 바닥](Media/spatial-store-floor.png) |
| 관리실·골목·옥상 | 문 앞 상호작용, 자재 두께와 겹침, 안내문·밸브 표기 | [관리실 자재](Media/spatial-booth-storage.png), [골목](Media/retail-backlane-wide.png), [옥상 밸브](Media/spatial-roof-valves.png) |

이 시점들에서 글자 반전이나 기존의 전자레인지 돌출·얼굴 경계 단절은 재현되지 않았다. 모든 동적 상황을 촬영한 것은 아니므로 전 구간에서 시각적 결함이 없다는 판정으로 쓰지 않는다.

## 밤 화면 검수 보완

기존 밝기 검사가 10곳 중 8곳에서 실패했다. 공용 조명 수정 전 코드를 되돌려 비교 실행해도 같은 문제가 재현됐다. 9월 11일에 복도 조명을 서쪽 등 하나만 남기도록 바꾼 뒤에도, 이전의 촬영 위치와 밝기 기준이 남아 있었다.

검사 자체에도 문제가 있었다. 추적자가 촬영 중에 플레이어를 잡아 침실로 돌려보냈고, 구역 연출은 검수하려던 등을 꺼 버렸다. 소리 파문은 0.85초 동안 남는데 촬영은 효과가 사라진 뒤에 이루어졌다.

조명 검수에서만 추적자 이동과 구역 연출을 고정했다. 괴물은 실제로 켜져 있는 서쪽 등 아래에 두고, 카메라의 위치·시선이 지정값에서 벗어나면 실패하게 했다. 노출을 4초 동안 안정시킨 뒤 촬영하며, 파문은 촬영 0.2초 전에 발생시킨다. 게임 플레이의 조명 배치와 추적 동작은 이 검사 설정에 영향을 받지 않는다.

다시 촬영한 PNG를 직접 확인한 후 시점별 밝기 범위를 맞췄다. 완전히 검은 화면을 걸러내는 상한 99.95%와 하이라이트 상한 1%는 유지했다. 손전등을 끈 복도는 어둡지만, 켰을 때는 벽·문턱·바닥 재질을 읽을 수 있다. 아래 비율은 화면 전체에서 밝기가 5% 미만인 픽셀의 비율이며, 공포감이나 그래픽 품질 점수가 아니다.

| 시점 | 암부 비율 | 실행 화면 |
| --- | ---: | --- |
| 복도, 손전등 끔 | 99.06% | [복도](Media/v5-corridor_dark.png) |
| 손전등과 먼지 | 72.29% | [빛이 닿는 범위](Media/v5-beam_dust.png) |
| 서쪽 등 아래 추적자 | 97.42% | [윤곽](Media/v5-entity_rim.png) |
| 별관 벽 | 38.02% | [벽](Media/v5-cavity_wall.png) |
| 별관 바닥 흔적 | 66.23% | [바닥](Media/v5-residue_fifth_floor.png) |
| 복도 바닥 | 36.37% | [바닥](Media/v5-residue_corridor.png) |
| 추격 후처리 | 94.27% | [복도](Media/v5-chase_post.png) |
| 소리 파문 | 99.23% | [화면 위쪽 파문](Media/v5-ripple_ring.png) |
| 철제 계단 | 15.06% | [디딤판](Media/v5-steel_stair.png) |
| 옥상 통로 | 68.48% | [방수 도막](Media/v5-rooftop_deck.png) |

## 검증과 성능

UE 5.8 Editor Development 빌드가 통과했다. 분전반 검수에서는 낮의 투입·복귀, 공용 전원 차단, 무명 회로 투입, 전원 복구를 확인했고, 로비 9장과 복도 정면·측면 2장을 남겼다. 밤 히스토그램 10곳도 지정한 시점에서 촬영을 마쳤다.

전체 진행 검사도 통과했다. 첫날 회로 퍼즐의 전원 전후 비교·되돌리기 조건과 기존 밤 진행을 확인했다. 추가 검사는 낮의 공용 전원 조작, 등갓 발광 차단, 연출의 전원 덮어쓰기 방지, 근거리 60회·원거리 4회 갱신, 원거리에서도 누적되는 원판 각도를 확인한다.

이동 검사는 60fps 조건에서 달리기·정지, 문 통과, 앉기·일어서기, 작은 물체 충돌, 벽 너머 상호작용 차단, 문에 막히는 추적자와 붙잡힘 자세까지 통과했다. 달리다 멈추는 데 62.5cm·0.267초가 걸렸다. 이 수치는 자동 검사 조건의 값이다.

성능은 Ryzen 7900X·RTX 3060 8GB, 1920×1080, D3D12에서 편의점·관리실·별관의 같은 12개 시점으로 측정했다. VSync와 프레임 제한을 끄고 초기 120프레임을 제외했다. High는 내부 해상도 100%, Performance는 71%다. 전체 플레이 시간의 평균이나 최저 사양 보장값으로 해석하지 않는다.

| 측정 | 표본 수 | 프레임 중앙값 | 95백분위 | 33.33ms 초과 | GPU 메모리 중앙값 |
| --- | ---: | ---: | ---: | ---: | ---: |
| [수정 전 High](Performance/Circuit20260917-BeforeHigh/summary.json) | 4,326 | 13.962ms | 16.492ms | 0 | 2,991.8MiB |
| [수정 후 High 1차](Performance/Circuit20260917-FinalHigh/summary.json) | 4,132 | 14.123ms | 16.672ms | 49 | 2,992.4MiB |
| [수정 후 High 재측정](Performance/Circuit20260917-FinalHighRepeat/summary.json) | 4,597 | 13.284ms | 15.286ms | 0 | 2,993.2MiB |
| [수정 후 Performance](Performance/Circuit20260917-FinalPerformance/summary.json) | 8,550 | 6.968ms | 8.234ms | 0 | 2,423.8MiB |

High 1차에서는 끝부분에 최대 52.304ms 지연이 있었고, 같은 조건의 재측정에서는 재현되지 않았다. 원인을 특정하지 못했으므로 첫 결과도 원본 CSV와 함께 보존했다. 이 표본에서 메모리와 일반적인 프레임 비용은 이전과 비슷하다. 거리별 계량기 갱신으로 원거리 호출을 초당 20회에서 4회로 줄였지만, 이를 게임 전체가 80% 빨라졌다는 뜻으로 쓰지 않는다.

`Scripts/Validate-Project.ps1`도 통과했다. 검사 로그의 해시·결과와 새 캡처 42장의 해시는 [검증 기록](Performance/Circuit20260917-verification.json)에 남겼다. 이미지 출처와 모델링 참고도, 수정 전후 사진은 위 링크에서 확인할 수 있다.

```powershell
pwsh -NoProfile -File Scripts/Run-CircuitReview.ps1
pwsh -NoProfile -File Scripts/Run-RetailReview.ps1 -Spatial -Profile Circuit20260917
pwsh -NoProfile -File Scripts/Run-MissingFloor-NightHistogram.ps1
pwsh -NoProfile -File Scripts/Run-GameplayRealismProbe.ps1 -FrameRate 60
pwsh -NoProfile -File Scripts/Validate-Project.ps1
```
