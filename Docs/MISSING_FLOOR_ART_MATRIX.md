# 없는 층 — ImageGen 에셋 적용 매트릭스

> 기준일 2026-08-11. 이 문서는 이미지 목록이 아니라 **어떤 원본을 어떤
> 런타임 표현으로 바꾸고, 어디까지 접근을 허용할지**를 잠그는 실행 계약이다.

## 적용 원칙

1. 가까이 갈 수 있거나 손전등 각도가 바뀌는 사물·인물·배경은 연속 3D/PBR이
   기준이다. 정면 디테일 레이어를 쓰더라도 접지·실루엣·그림자는 3D가 소유한다.
2. 손자국·먼지처럼 본질적으로 얇은 것은 값 마스크로 기존 표면에 블렌드한다.
3. 일반 인물 스프라이트는 12m 이상, 접근 불가, 한 컷에만 쓴다. 위층 사람과
   M5 최종 대치는 예외적으로 연속 3D 접지 셸을 유지한 채 제한된 정면 거리에서
   PBR 가독성 레이어를 켠다. 범위를 벗어나면 즉시 셸로 되돌린다.
4. ImageGen에 조명·그림자·한글·증거 상태를 굽지 않는다. 조명과 그림자는
   UE, 의미 있는 한글은 런타임 UI, 퍼즐 상태는 코드가 소유한다.
5. 원본 시트는 카메라에 직접 노출하지 않는다. 크롭·알파·PBR·메시 변환을
   거친 파생 에셋만 Shipping 경로에 들어간다.

## Keep / 3D / PBR / Sprite / Blend

| 대상 | ImageGen 원본 | 런타임 에셋 | 방식 | 실제 적용 | 합격 기준 |
|---|---|---|---|---|---|
| 불법 5층 배경 | `SheetMissingFloorEnvironmentReference.png` | `AIGPrologueWorldScene::BuildFifthFloorAnnex` | Keep geometry + reference | 철골·옥상·복도 비례와 노출 기준 | 배경 사진·가짜 출입구 0, 충돌과 보이는 면 일치 |
| 건식 석고 | `TextureMissingFloorDryPlaster.png` | `T_MissingFloorDryPlaster_{D,N,R,A}`, `M_MissingFloorPlaster_X/Y/XY` | PBR | 5층 벽·천장 | 이음선 0, 금속성 0, 거칠기 0.88 이상, 조명 베이크 0 |
| 손자국·끌림·분진·긁힘 | `SheetMissingFloorResidueMasks.png` | 마스크 4장 + 전용 masked material 4종 | Blend | 5층 벽·바닥·공동 안쪽 | 사각 테두리 0, 표면 이격 0.15cm, 그림자·충돌 0 |
| 위층 사람 | `SheetListenerEntityAnatomyReference.png`, `ListenerEntityFrontCutout.png`, `SheetListenerEntityCrawlPhases.png` | `SM_ListenerEntityCrawl`, `M_MissingFloorListenerPlasterUV`, `T_SpriteListenerCrawl0..3_{D,N,R,A}`, `M_SpriteListenerCrawl0..3` | 3D shell + distance/angle PBR sequence | 1.6m 정면에서 4단계 레이어 활성화, 1.25m까지 유지, 측면은 3D | 128×85cm 투영, 프레임별 초록 프린지 0, 1.6~6fps 속도 연동, 바닥 접지, 숨은 3D 그림자; 측면 평면 노출 0 |
| 서일영 | `SheetMissingFloorDistantCharacters.png` 좌상 | `T_SpriteSeo_D`, `M_SpriteSeo` | Sprite | 낮3 골목 건너편 1회 | 12m 이상, 접근 불가, 발 알파가 지면과 일치, 8m 내 숨김 |
| 목한수·황순금·나린 | 같은 시트 나머지 | 각 `T_Sprite*_D`, `M_Sprite*` | Prepared sprite | 고정 원거리 큐가 생길 때만 배치 | 근접 대화 대용 금지. 현재는 미배치가 정답 |
| 튜닝 해머 | `SheetMissingFloorHeroPropsReference.png` 우상 | `SM_TuningHammer` | 3D hero prop | 밤3 자재 위, P3·엔딩 인과 | 27cm L자형, 실제 접촉 그림자, 원통형 청음봉 폴백은 개발 전용 |
| 조율 공구 카트 | 같은 시트 좌상 | `SM_TunerToolCart` | 3D prop | 5층 서북 모서리 | 45×34×78cm, 선반 틈·바퀴·손잡이 시차, 바닥 관통 0 |
| 민원 대장·접수철·연필 | `BoothStationeryStudy_20260917.png`, `ComplaintRubbing_20260917.png` | `SM_ComplaintLedger`, `SM_ComplaintImpressionPad`, `SM_GraphitePencil`, `M_ComplaintImpression` | 3D 소품 + 진행에 따른 표면 복원 | 관리실 책상 Z=76cm 위 | A4 대장과 얇은 접수철 구분, 3단계 복원, 중단·재개, 표지 인쇄 정방향, 책등 중첩 없음 |
| 로비 분전반 | `CircuitPanelReference_20260917.png` | `SM_LobbyCircuitPanel`, `SM_CircuitPanelPrints`, `SM_CircuitToggle` | 함체·인쇄·회전 손잡이 분리 | 계량기함 오른쪽, 현관문 바깥 | 실물 누전차단기 비율, 회로명 정방향, 고정 축 회전, 낮의 복귀, 등갓 발광과 전원 일치 |
| 복도 분전함 | `CorridorCabinetReference_20260917.png` | `SM_CorridorCircuitCabinet`, `SM_CorridorCircuitPrint` | 매입형 함체 + 별도 이름표 | 402·403호 사이 벽면 | 문틀 좌우 9·7cm 간격, 문짝 벽 밖 8mm, 한글 정방향·획 보존, 기존 벽 충돌 사용 |
| 달력 뒷장 소리 일지 | 같은 시트 우하 | `SM_CalendarJournal` | 3D hero paper | 401호 문 앞 전달 큐 | 종이판·상단 바인딩 실제 기하, 글은 런타임 패널, 가짜 글자 0 |
| 1인칭 두드리기 오른손 | `SheetFirstPersonKnockPhases_v2.png` | `T_FPHandKnock0..3_D` | UI-space RGBA sprite blend | Q/B 유효 노크의 준비·접촉·반동 0.22초 | 같은 손·소매 유지, 입력 프레임에 접촉(2), 소매 끝은 화면 밖, 초록 프린지 0, 흔들림 감소 시 이동 0, 월드 평면 0 |
| 1인칭 포획 접촉 | 기존 포복·포획 모델 기준 시트 | `SK_ListenerCrawler`와 포획 애니메이션 | 실제 3D 몸 + 카메라 암전 | 추격하던 몸이 다가온 뒤 2.15초에 침대 복귀 | 손 그림 합성 제거, 입력 잠금, 카메라 관통 없음 |
| 1인칭 기상 잔향 | 손 그림 합성 제거 | 별도 이미지 없음 | 침대 시야 + 회차별 페이드 | 회차별 3.0/2.2/1.4/0.4초 뒤 조작 복구 | 페이드 끝까지 HUD 점유, 손 이미지 로드·역재생 없음 |
| 공동 최종 잔존물 | `SheetFinalCavityRemainsReference_v1.png`, `FinalCavityFrontBlend_v1.png` | `SM_FinalCavityClothingShell`, `SM_FinalCavityBoneInsert`, `SM_FinalCavityTarp`, `SM_FinalCavityBrokenCaster`, `M_SpriteFinalCavity` | continuous 3D + lit masked PBR detail | 밤4 공동 개방 뒤 105~360cm·정면 내적 0.68에서 디테일, 그 밖은 셸 | 120cm 베이 안, 피부·머리카락·피·젖은 조직 0, 두개골→흉곽→신발 판독, 숨은 셸 그림자 유지, 측면 평면 노출 0 |
| 목한수 최종 대치 | `SheetMokHansooConfrontationReference_v1.png`, `MokHansooFinalFrontBlend_v1.png` | `SM_MokHansooWorkwear`, `SM_MokHansooHeadHands`, `SM_MokHansooGypsumBoard`, `M_SpriteMokFinalUpper` | continuous 3D + upper-body PBR detail | 남쪽 계단참 등장→북쪽 퇴장 2.8초, 105~360cm·정면 내적 0.68에서 얼굴·재킷 보강 | 평균 체형·두 손 파지·95cm 보드·하체·그림자는 3D, 세로 31~39% 알파 감쇠, 근접 `T_SpriteMok_D` 사용 0, 괴물 통과 중 충돌 0 |
| 엔딩 C 매물 외관 | `TitleBackgroundMissingFloor_v1.png` | `T_TitleBackground_D` + 네이티브 Canvas 한글 | Keep crop + runtime UI | 403호 매물 사진→입주자 후기 전환 | 신규 생성·브랜드 복제·구운 글자 0, 720p·텍스트 200% 안전 영역, 모션 감소 컷 제공 |
| 에필로그 1 공방 (§9) | `EpilogueWorkshop_v1.png` | `T_EpilogueWorkshop_D` + 런타임 한글 | Keep + runtime UI | 엔딩 A 17.6~45초 정지 화면 | 손과 현만, 얼굴·상체 0, 구운 글자 0, 원본 3:2 유지 |
| 에필로그 2 가을 (§9) | `EpilogueAutumn_v1.png` | `T_EpilogueAutumn_D` + 런타임 한글 | Keep + runtime UI | 엔딩 A 45~61초 정지 화면 | 간판·상호 0, 401호 창턱 라디오 1개, 원본 2:3 유지 |
| 마지막 신 서비스 베이 (§9) | `EpilogueServiceBay_v1.png` | `T_EpilogueServiceBay_D` + 런타임 한글 | Keep + runtime UI | 엔딩 B 17.6~45초 정지 화면 | 사람·유해·카트 0, 방수포 자국과 캐스터 자국만, 원본 3:2 유지 |
| 에필로그 403호 문 (§9) | 게임 안 렌더 `Run-EpilogueDoorStill.ps1` | `T_EpilogueDoorNote_D` + 런타임 한글 | Keep + runtime UI | 두 엔딩 보도 뒤 12초 정지 화면(A 75~87초, B 59~71초) | 303호 쪽지 셋 없이 마지막 쪽지 한 장, 문 위 호수 표찰 0, 쪽지 글씨는 화면 크기에서 판독 불가, 원본 2:3 유지 |

## 2026-09-08 생성 메시 갱신

| 대상 | 새 원본 | 새 런타임 에셋 | 바뀐 점 |
|---|---|---|---|
| 위층 사람 셸 | `SheetListenerEntityAnatomyReference.png` 좌하 칸 → TRELLIS.2 | `SM_ListenerEntityCrawl` (12000 삼각형, 정점 AO) | 타원체 조립을 생성 형상으로. 정면 레이어·석고 재질·WPO 계약은 그대로. 접지 오프셋은 메시 바운드에서 계산 |
| 목한수 최종 대치 | `SheetMokHansooConfrontationReference_v1.png` 좌상 칸 → TRELLIS.2 | `SM_MokHansooFigure` | 작업복·머리손·석고보드 세 조각과 `M_SpriteMokFinalUpper` 카드를 통짜 하나가 대체. 조각과 카드는 메시가 없을 때의 폴백 |
| 공동 최종 잔존물 | `SheetFinalCavityRemainsReference_v1.png` 좌상 칸 → TRELLIS.2 | `SM_FinalCavityRemains` | 옷·뼈·방수포·캐스터 네 조각과 `M_SpriteFinalCavity` 카드를 통짜 하나가 대체. 앞끝 X -31, 깊이 68 |
| 골목 고양이 | `SheetAlleyCatPoseReference.png` 우상 칸 → TRELLIS.2 | `SM_AlleyCatRun` | 복셀 리메시로 털 조각을 녹인 8000 삼각형. 구운 털 색 인스턴스 사용 |

## 블렌딩·거리·성능 계약

- 건식 석고는 D/N/R/A를 한 재질에서 샘플하고 X/Y/XY 세 방향 인스턴스로
  공유한다. 5층 표면마다 별도 4K 재질을 만들지 않는다.
- 흔적 평면은 최대 4종, 한 화각 동시 노출 최대 3장이다. 투명 반투명 대신
  masked를 사용해 정렬 문제와 과도한 오버드로를 막는다.
- 서일영 스프라이트는 낮 구간 한 장만 활성화하고 나머지 셋은 비가시 상태다.
  8m 안쪽 숨김, 12m 이상 표시, 40m 컬링을 기본값으로 삼는다.
- 위층 사람 정면 레이어는 1.6m·정면 내적 0.60에서 켜지고 1.25m 또는
  내적 0.35 아래에서 꺼지는 히스테리시스를 쓴다. 보이는 레이어는 masked
  D/N/R/A로 수광하고, 이동 속도에 따라 네 자세를 1.6~6fps로 순환한다.
  정지하면 프레임을 유지하며 응답 노크 대기 상태는 중앙 지지 자세로
  고정한다. 숨은 연속 셸만 실제 접지 그림자를 낸다.
- 위층 사람 셸은 조립 타원체가 아니라 자가 합집합으로 융합한 한 장의 닫힌
  표면이다. 접합 능선은 스무딩으로 잇고, 펄린 결 두 층이 마른 석고 요철을
  만들되 손가락은 결 변위를 받지 않는다(유착 방지). 굽기 단계가 정점 AO를
  메시에 남기고, 재질의 골 분진·심화 폐색·균열 노멀 증폭이 그 값을 읽는다.
- 셸의 숨과 잔떨림은 재질 WPO이고 진폭 파라미터(BreathAmplitude,
  TremorAmplitude)만 상태 머신이 MID로 정한다. 듣기·경청 정지에서 잦아들고
  추격에서 거칠어지며, 응답 노크 대기(Waiting)에서는 0으로 멎는다 — 배운
  대답이 통했다는 확인은 이 정지로 보여 준다.
- M5 잔존물·목한수 디테일은 105~360cm·정면 내적 0.68 초과에서만 켠다.
  잔존물은 디테일 활성 중에도 숨은 3D 셸 그림자를 보존하고, 목한수는 얼굴·재킷
  아래 알파를 감쇠해 실제 석고보드·하체·그림자를 덮지 않는다. 원거리 목한수
  `T_SpriteMok_D`와 최종 대치 레이어는 서로 다른 용도이며 교체할 수 없다.
- `SM_ListenerEntityCrawl`, 튜닝 해머, 원장, 일지는 서사 근접 판독 때문에
  LOD0를 보존한다. 카트는 SmallProp LOD 그룹을 쓰며 화면 점유율 2% 아래에서
  단순화한다. 불법 5층 구조물은 반복 블록 재질을 공유하고 개별 Tick이 없다.
- 에필로그 세 장은 전체 화면을 덮는 UI 판이라 월드 아틀라스가 아니라
  `/Game/UI/Textures`로 들어간다. 타이틀 키아트와 같은 경로이며, HUD가
  **원본 비례로** 그린다 — 고정 높이로 늘이면 세로 사진이 눌려 건물이
  납작해지고, 그것이 이 화면에서 가장 눈에 띄는 거짓말이 된다. 텍스처가
  없으면 그 장면은 글자만 남는다.
- 일반 스프라이트 알파, 흔적 값 마스크, 석고 PBR은 1024px 이하 파생본을 쓰고,
  M5 세로형 정면 디테일만 1024×1536을 허용한다. 원본 시트는 SourceArt 증빙이며
  런타임에 직접 로드하지 않는다.
- 1인칭 손은 투명 여백을 포함한 768px RGBA 네 장을 UI 그룹·NoMip·Clamp·NeverStream으로
  임포트한다. 문·벽·인물·동물·배경은 기존 3D/PBR을 유지하며, 손 이미지를
  월드 카드나 충돌 대용으로 쓰지 않는다.
- 포획은 실제 스켈레탈 몸과 카메라 암전으로 이어진다. HUD는 예전 손 그림
  네 장을 읽지 않는다. 원본과 파생본은 과거 제작 기록으로만 남긴다.
- 기상 때는 침대 시야만 서서히 돌아온다. 회차별 3.0/2.2/1.4/0.4초
  페이드 끝까지 HUD 점유와 입력 잠금을 유지한다.
- 엔딩 C는 타이틀 외관의 중앙 빌라 구간만 UV 크롭해 재사용한다. 사진에 방
  번호·앱 로고·매물 문구를 굽지 않고, 403호 칩과 모든 정보는 실제 한글 폰트로
  런타임에 그린다. UI 텍스처 1장 외 신규 스프라이트·월드 카드·충돌은 없다.

## 물리·화면 승인

- 프롭 바닥/책상 접지 오차 1cm 이하, 접촉 그림자 중심 이탈 2cm 이하.
- 손전등을 좌우 60도로 움직여 존재·튜닝 해머·카트에 시차와 셀프 섀도가 남아야 한다.
- 흔적을 비스듬히 볼 때 카드 두께나 사각 외곽이 보이면 실패다.
- 원거리 인물은 720p에서 사람으로 읽히고 1080p에서 마젠타 프린지 픽셀 0,
  지면 아래 알파 누락 0이어야 한다.
- 위층 사람은 1920×1080 이동 캡처에서 복도 벽 관통 0, 초록 프린지 0,
  4단계 자세 중복 0, 실제 전진, 사람 판독 성공, 1.25m 경계 전환 뒤 카드
  노출 0이어야 한다.
- 셸 근접 캡처에서 부품이 서로 파고드는 교차 타원 자국 0, 손가락 유착 0,
  응답 노크 대기 중 표면 미동 0을 승인한다.
- 캡처 한 장이 아니라 이동 전후 두 프레임과 그림자 프레임을 함께 승인한다.
- 1920×1080과 1280×800 D3D12 실렌더에서 접촉부터 침대 복귀까지
  확인한다. 카메라에 붙은 별도 팔이나 반투명 손 잔상이 남으면 실패다.
- 기상 페이드가 끝나면 입력을 돌려주고, 그 전에 목표와 조작 안내를
  띄우지 않는다. 다시 보기 키는 포획 중 작동하지 않는다.

## 재현 경로

```powershell
.\Scripts\Build-ArtAssets.ps1 -MissingFloorOnly
.\Scripts\Build-ArtAssets.ps1 -HudUiOnly
.\Scripts\Test-ArtAssetContract.ps1
.\Scripts\Run-MissingFloor-NightCapture.bat
$env:IG_NIGHT_CAPTURE_RES_X='1280'; $env:IG_NIGHT_CAPTURE_RES_Y='800'
pwsh -NoProfile -File Scripts/Run-CaptureRecoveryReview.ps1
pwsh -NoProfile -File Scripts/Run-CaptureRecoveryReview.ps1 -Width 1280 -Height 800
```

첫 명령은 ImageGen 원본 분리 → PBR 생성 → 메시 베이크 → 텍스처/머티리얼
UAsset 생성 → 링크 감사를 헤드리스로 수행한다. 두 번째 명령은 원본·파생·
런타임 바인딩과 스프라이트 사용 경계를 정적으로 잠근다.
