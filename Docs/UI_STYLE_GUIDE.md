# UI 스타일 가이드

기준일: 2026-08-06 (적용 범위 각주 2026-08-10)
적용 범위: 인게임 대화, 내면 독백, 기기 메시지, 음성 자막, 비언어음 캡션

대화·자막의 모습과 동작, 설정 화면, 타이틀 메뉴의 제작 기준을 정리한다.
화면 구성, 조사 기록, 목표·힌트 표시, 재시작, 첫 실행 안내, 접근성의 전체 기준은
[STORY_BIBLE_MISSING_FLOOR.md §19](STORY_BIBLE_MISSING_FLOOR.md)를 따른다.
두 문서의 내용이 다르면 스토리 바이블을 우선한다.

## 시각 방향

대화창은 어두운 반투명 유리처럼 보이게 만든다. 뒤에 있는 방과 복도가 비치고,
글자는 또렷하게 읽혀야 한다. 화면을 넓게 덮는 검은 패널, 굵은 상태 막대,
네온색, 강한 흐림 효과는 피한다.

- 기본 대화창은 화면 폭 약 56%를 시작점으로 하되 1080p에서는 840px을 넘지
  않는다. 주변을 살펴볼 수 있도록 하단 중앙의 빈 공간에 놓는다.
- 배경은 녹색이 조금 섞인 짙은 회색이다. 원본 이미지의 미세한 무늬를
  옅게 입히되 글자를 읽는 데 방해가 되지 않게 한다.
- 화자명은 본문보다 작은 둥근 상자에 둔다. 대화는 청록색, 내면 독백은
  청회색, 위험을 알리는 표시는 채도가 낮은 적갈색으로 구분한다.
- 비언어음은 대화창과 합치지 않는다. 내용 폭에 맞는 별도 캡슐과 코드로 그린
  파형을 사용해 대사와 구분한다.
- 본문은 상아색, 보조 문자는 어두운 청록색을 쓴다. 배경 불투명도
  42% 미만에서만 외곽선을 켜므로 기본 화면에서 글자가 두껍게 번지지 않는다.

## 화면 크기와 접근성

| 항목 | 기본 | 허용 범위·동작 |
|---|---:|---|
| 자막 크기 | 100% | 85~200%. 125% 초과 시 페이지당 3줄 |
| 표면 불투명도 | 82% | 0~100%. 텍스트 가독성 설정과 시각 질감을 분리 |
| 안전 영역 | 90% | 80~100%. 720p~1440p 네 해상도에서 실측 |
| 본문 줄간격 | 132% | 3줄일 때 136% |
| 등장·퇴장 | 240ms / 160ms | 동작 감소 사용 시 위치 이동 제거 |
| 음성/비언어음 | 독립 | 자막과 소리 캡션 토글을 별도로 유지 |

자막을 200%로 키우면 안전 영역에 맞춰 줄을 다시 나눈다. 한 화면에 들어가지
않는 내용은 다음 페이지로 넘겨 모두 보여 준다. 확대해도 화자 표시, 소리 파형,
배경 불투명도 설정은 유지한다.

## 구현과 성능

- 대화 큐와 접근성 설정은 기존 C++ HUD에서 이벤트가 발생할 때 갱신한다.
  대화창을 고칠 때 전체 UI를 CommonUI로 옮길 필요는 없다.
- 64px 런타임 9-slice 마스크 하나를 재사용한다. 라운드 표면마다 새 텍스처나
  위젯을 만들지 않는다.
- `T_HudDialogueFilm_D`는 `TEXTUREGROUP_UI`, `NoMipmaps`, `NeverStream`으로
  임포트한다. 월드 텍스처 스트리밍 풀과 LOD 변화에 영향을 주지 않는다.
- 글자 폭과 줄바꿈은 대화가 바뀌거나 해상도·접근성 배율이 바뀔 때만 다시
  계산한다. 매 프레임 실행되는 바인딩에서 원문을 다시 분석하지 않는다.
- 패널 렌더링은 대화 또는 소리 캡션이 보일 때만 실행한다. 숨겨진 상태의 UI
  렌더 비용은 0이다.

## 아트·검증 근거

- 콘셉트: `Content/SourceArt/AI/DialogueHUDConcept_v1.png`
- 표면 원본: `Content/SourceArt/AI/TextureHudDialogueFilm.png`
- 파생 PNG: `Content/SourceArt/T_HudDialogueFilm_D.png`
- 런타임 UAsset: `Content/Prototype/Textures/T_HudDialogueFilm_D.uasset`
- 기본 Shipping 캡처: `Docs/Media/dialogue-hud-default-1080.png`
- 접근성 200% 캡처: `Docs/Media/dialogue-hud-accessibility-200-1080.png`

자동 검사에서는 1280×720, 1600×900, 1920×1080, 2560×1440에서 기본 크기와
200% 크기를 각각 확인한다. Shipping 실행 파일로 글자가 차지하는 영역, 안전 영역,
화자 표시, 페이지 사이에 빠진 내용이 없는지, 키보드·게임패드 조작을 검사한다.

참조한 엔진·접근성 기준:

- Unreal Engine 5.8 UMG Styling: 9-slice와 UI 텍스처 스타일링
- Unreal Engine CommonUI Design Guidelines: 기존 단일 레이어 HUD의 이관 판단
- Unreal Engine UMG Optimization Guidelines: 이벤트 기반 갱신과 raw binding 회피
- Unreal Engine DPI Scaling / Safe Zones: 해상도와 안전 영역 대응
- Xbox Accessibility Guideline 104: 화자 식별, 비언어음 구분, 조절 가능한 배경

## 설정 화면

기준일: 2026-08-12

적용 범위: 화면·성능, 접근성, 설정 내 하위 화면

왼쪽에는 카테고리 목록을, 오른쪽에는 선택한 카테고리의 항목과 설명을 놓는다.
값을 바꾸기 전에 화면, 조작, 공포 연출에 어떤 변화가 생기는지 알 수 있게 쓴다.

- 선택한 항목은 배경색, 왼쪽의 3px 표시선, 글자의 밝기와 굵기로 구별한다.
- 항목명은 왼쪽, 값은 오른쪽에 정렬한다. 조절 가능한 값만 `- 값 +`으로
  표시하며, 열기·적용·닫기 같은 동작을 숫자 설정처럼 보이게 하지 않는다.
- 화면 설정은 `화면 / 성능 / 플레이 보조 / 변경 사항`, 접근성은
  `게임 진행 / 화면 효과 / 소리 알림 / 자막 / 조작 / 기타`로 묶는다.
- 접근성 설정은 바꾸는 즉시 저장한다. 화면 모드·해상도·품질은 적용 버튼을
  누른 뒤 10초 안에 유지할지 확인한다. 각 화면 상단에 저장 방식을 안내한다.
- 자막 카테고리는 크기·배경 농도·안전 영역을 실제 캡션 표면으로 미리 본다.
- 카테고리 클릭은 값을 변경하지 않고 첫 항목으로 포커스만 옮긴다. 키보드,
  마우스, 게임패드는 같은 기능과 순서를 제공한다.
- 카테고리는 이름과 선택 표시만으로 구별한다. 항목 수는 표시하지 않고,
  숫자는 실제 설정값을 보여 줄 때만 쓴다.
- 지원 해상도에서 카테고리·옵션의 포인터 판정은 렌더 좌표와 같은
  `IGSettingsMenuLayout`을 사용한다. 최소 카테고리 높이는 48px, 옵션 높이는
  52px이며 720p에서도 축소하지 않는다. 글자는 실제 글리프 높이로 수직 중앙
  정렬하고 제목–부제는 최소 8px, 설명 줄은 148% 간격을 확보한다.

설정 구성에는 Xbox Accessibility Guidelines의 글자·입력·메뉴 이동·움직임 기준을
참고했다. 짧은 항목 설명은 Ghost of Yōtei, 시각·오디오·안내·조작 분류는
Assassin's Creed Shadows와 비교해 Canvas HUD에 적용했다. 나중에 UMG로 옮겨도
분류는 유지한다. 고정된 부분은 Invalidation Box에 두고, 값과 선택 상태는
바뀔 때만 갱신한다.

프런트엔드 검사 도구는 Shipping에서 화면 설정과 접근성 설정을
720p·900p·1080p·1440p PNG로 남긴다. 영역 밖으로 나간 글자, 키보드·게임패드
입력, 카테고리 전환을 모두 검사한다. 에셋 로딩과 셰이더 컴파일이 끝나고
화면이 안정된 뒤 캡처한다. 엔진 준비 문구가 남은 이미지는 소개 화면에 쓰지 않는다.

### 글꼴과 글자 넘침

- 제목은 고운바탕 Bold, 선택 항목은 Pretendard SemiBold, 본문·값·설명은
  Pretendard Regular를 사용한다. 바탕 계열은 제목에만 제한해 분위기를 만들고,
  조작 중 빠르게 훑는 정보는 산세리프로 유지한다.
- 설정 본문은 1080p 기준 18px을 기준으로 잡고 720p에서도 임의의 시스템 폰트로
  바뀌지 않는다. 폰트 파일과 OFL 원문을 패키지 실행 파일 옆 `UI/Fonts`에 함께
  스테이징한다.
- 제목·상태·카테고리·항목명·값·하단 입력 안내는 각 컨테이너의 남은 폭으로
  실제 글리프를 측정한 뒤 필요한 경우에만 축소한다. 설명은 두 줄을 우선하고
  내용이 남으면 세 번째 줄을 사용하며 문자열을 생략하지 않는다.
- 자막 미리보기는 200%에서도 안전 영역 폭으로 다시 줄바꿈하고 표면 높이를
  줄 수에 맞춰 늘린다. 자동 프로브는 화면 바깥뿐 아니라 행·헤더·푸터·미리보기
  컨테이너를 벗어난 글리프도 별도 실패로 처리한다.
- Shipping 검사에서는 접근성의 5행 자막 그룹과 입력 그룹, 화면 설정의 3행 성능
  그룹을 선택한다. 항목이 많은 화면에서도 글자가 잘리지 않는지 확인한다.
- 왼쪽 목록의 배경색은 본문 구간에만 칠한다. 상단 제목과 하단 안내까지
  이어지면 글자가 잘린 것처럼 보일 수 있다.

## 타이틀과 시스템 메뉴

기준일: 2026-08-12

타이틀 화면 오른쪽에는 비가 그친 새벽의 무영로 빌라를 보여 주고, 왼쪽 어두운
여백에 메뉴를 놓는다. 작품명은 고운바탕 Bold, 메뉴와 상태는 Pretendard를 쓴다.
바탕은 먹색, 글자는 상아색과 회색으로 하고 선택한 항목만 적갈색으로 강조한다.

- 첫 실행에는 사용할 수 없는 `이어하기`를 회색으로 남기지 않고 숨긴다. 첫
  항목은 `게임 시작`이며, 호환 자동 저장이 생긴 뒤에만 `이어하기 / 새 게임`을
  함께 표시한다.
- 선택한 항목은 건물 단면을 닮은 세로선, 길어진 층 눈금, 어두운 배경색,
  SemiBold 글씨로 구별한다.
- 행 높이는 720p에서도 44px 아래로 줄이지 않는다. 네이티브 HUD와 마우스
  판정은 모두 `IGFrontendMenuLayout`의 좌표를 사용한다.
- 타이틀 원화에 글자·로고·UI를 넣지 않는다. 제목과 메뉴는 게임에서 언어에 맞춰
  표시한다. 16:9가 아닌 화면에서는 왼쪽 메뉴 공간을 확보한 뒤 배경을 잘라 맞춘다.
- 진입은 전체 그룹의 320ms 불투명도 변화 한 번만 사용한다. 이동·반복 맥동·
  글리치·깜빡임은 없으며 `동작 감소`에서는 전환을 즉시 완료한다.
- 일시정지는 별도 배경으로 교체하지 않고 멈춘 세계를 어둡게 남긴다. 플레이어가
  메뉴를 닫았을 때 위치 관계를 다시 찾을 필요가 없어야 한다.
- 설정·소리/밝기 보정은 각각의 정보 구조를 유지한다. 타이틀 장식선을 뒤에
  겹쳐 두 번째 포커스처럼 보이게 하지 않는다.

사용 파일:

- 생성 원본: `Content/SourceArt/AI/TitleBackgroundMissingFloor_v1.png`
- 파생 PNG: `Content/SourceArt/T_TitleBackground_D.png` (1920×1080)
- 런타임: `Content/UI/Textures/T_TitleBackground_D.uasset`
- Shipping 첫 실행 캡처: `Docs/Media/title-menu-first-run-1080.png`
- 생성·해시·프롬프트: `Docs/IMAGEGEN_PROMPTS_2026-08-12.md`

Xbox Accessibility Guidelines 101·102·112·113·115·117의 글자, 대비,
메뉴 이동 순서, 선택 표시, 위험한 동작의 확인, 움직임 제어 기준을 참고했다.
구현은 Unreal Engine 5.8의 UMG Optimization/Invalidation 지침을 따른다.
배경이 잘 보이도록 메뉴를 단순하게 구성하고, 한국 주거지와 새벽이라는
게임 배경에 어울리는 장식만 사용한다.

- <https://learn.microsoft.com/en-us/xbox/accessibility/guidelines>
- <https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/101>
- <https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/102>
- <https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/112>
- <https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/113>
- <https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/115>
- <https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/117>
- <https://dev.epicgames.com/documentation/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine>
- <https://dev.epicgames.com/documentation/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine>
