<#
.SYNOPSIS
	§9 「밤 5」 슬롯 계약 검사 — 메타 개입은 이 한 번뿐이다.

.DESCRIPTION
	설계서가 이 기능에 붙인 조건은 대부분 **금지**다. 그래서 이 계약도 절반이
	금지 항목이다.

	하나, 신뢰를 깨지 않는다. 세이브 파일을 만들거나 고치지 않고, 가짜 크래시를
	내지 않고, 창 제목을 바꾸지 않는다. 슬롯이 늘어나는 근거는 플레이어가 아니라
	**세이브**이므로 최신 호환 자동 저장의 엔딩 플래그를 그대로 읽는다.

	둘, 기존 메뉴를 흔들지 않는다. 밤 5는 액션 목록의 마지막이면서 화면에서는
	「이어하기」 바로 밑이다. 이 분리 덕분에 확인 디스패치의 인덱스가 하나도
	움직이지 않는다 — 대신 위아래 키가 **화면 순서**로 돌아야 한다.

	셋, 30초는 대부분 침묵이다. 두 소리 사이의 9초와 뒤의 17초가 「새 사건도,
	갇힌 사람도 암시하지 않는다」를 지키는 방식이다.

	넷, 월드 타이머로는 이 30초가 한 프레임도 진행하지 않는다. 타이틀 메뉴가
	월드를 일시정지시키기 때문이다. 엔진 티커여야 한다.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Read-Source([string]$RelativePath) {
	Get-Content -Raw -Encoding UTF8 -LiteralPath (
		Join-Path $projectRoot $RelativePath)
}

$layout = Read-Source 'Source/IndieGame/Player/IGFrontendMenuLayout.h'
$controller = Read-Source 'Source/IndieGame/Player/IGPlayerController.cpp'
$controllerHeader = Read-Source 'Source/IndieGame/Player/IGPlayerController.h'
$hud = Read-Source 'Source/IndieGame/Player/IGHorrorHUD.cpp'
$hudHeader = Read-Source 'Source/IndieGame/Player/IGHorrorHUD.h'
$saveHeader = Read-Source 'Source/IndieGame/Save/IGSaveSubsystem.h'
$save = Read-Source 'Source/IndieGame/Save/IGSaveSubsystem.cpp'
$runner = Read-Source 'Scripts/Run-MissingFloor-NightFiveProbe.ps1'
$assertions = 0

function Require-All(
	[string]$Source,
	[string[]]$Needles,
	[string]$ContractName) {
	foreach ($needle in $Needles) {
		if (-not $Source.Contains($needle)) {
			throw "$ContractName invariant is missing: $needle"
		}
		$script:assertions++
	}
}

function Require-None(
	[string]$Source,
	[string[]]$Needles,
	[string]$ContractName) {
	foreach ($needle in $Needles) {
		if ($Source.Contains($needle)) {
			throw "$ContractName forbids: $needle"
		}
		$script:assertions++
	}
}

# --- 신뢰를 깨지 않는다 -----------------------------------------------------
# 근거는 세이브다. 설정 파일에 별도 플래그를 심지 않는다.
Require-All $saveHeader @(
	'bool HasEndingBAutosave() const;'
) 'ending B is read from the save'
Require-All $save @(
	'bool UIGSaveSubsystem::HasEndingBAutosave() const',
	'FindNewestCompatibleAutosave(NewestSlot)',
	'MissingFloorNarrative.Night.EndingChoice',
	'FName(TEXT("Ending.B"))'
) 'ending B is read from the save'
# 금지된 메타. 슬롯이 세이브를 만들거나, 크래시를 흉내내거나, 창 제목을
# 건드리는 순간 §9의 유일한 조건이 깨진다.
Require-None $controller @(
	'RequestSave',
	'RequestAutosave',
	'DoesSaveGameExist',
	'SaveGameToSlot',
	'DeleteGameInSlot',
	'SetWindowTitle',
	'check(false)',
	'UE_LOG(LogTemp, Fatal'
) 'night five must not break trust'
# 검증 실행이 세이브를 남기지 않았다는 것을 파일 시스템으로 확인한다.
Require-All $runner @(
	'Saved\SaveGames',
	'밤 5가 세이브 파일을 만들었습니다',
	"'-RenderOffScreen',",
	"if (`$arguments -notcontains '-RenderOffScreen')"
) 'no save file, no window'

# --- 기존 메뉴를 흔들지 않는다 ----------------------------------------------
Require-All $layout @(
	'constexpr int32 ActionCount = 6;',
	'constexpr int32 NightFiveAction = 5;',
	'constexpr int32 ScreenOrder[ActionCount] = {0, 2, 3, 4, 5, 1};',
	'inline bool HidesNightFive(',
	'return !bTitleMenu || !bNightFiveAvailable;',
	'inline bool IsActionHidden('
) 'night five is the last action and the second row'
# 확인 디스패치는 이름으로 밤 5를 잡는다. 숫자를 옮기지 않았다는 증거다.
Require-All $controller @(
	'if (SystemMenuSelection == IGFrontendMenuLayout::NightFiveAction)',
	'PlayNightFive();'
) 'dispatch untouched'
# 위아래 키는 화면 순서로 돈다. 액션 순서로 돌면 선택이 화면을 건너뛴다.
Require-All $controller @(
	'IGFrontendMenuLayout::GetVisibleSlotForAction(',
	'IGFrontendMenuLayout::GetActionForVisibleSlot(',
	'VisibleSlot + (Direction < 0 ? VisibleCount - 1 : 1)) % VisibleCount'
) 'navigation walks screen order'
# 일시정지 메뉴에는 절대 없다.
Require-All $controller @(
	'IGFrontendMenuLayout::HidesNightFive(',
	'SystemMenuMode == EIGSystemMenuMode::Title,'
) 'never while paused'

# --- 30초는 대부분 침묵이다 -------------------------------------------------
Require-All $controllerHeader @(
	'static constexpr float NightFiveTotalSeconds = 30.0f;',
	'void PlayNightFive();',
	'bool IsNightFivePlaying() const'
) 'night five surface'
Require-All $controller @(
	'constexpr float SignalAtSeconds = 3.2f;',
	'constexpr float AnswerAtSeconds = 12.6f;',
	'CreateAnswerKnockPattern(this, 0.0f)',
	'CreateWallKnockReply(this)',
	'EIGAudioBus::Player',
	'EIGAudioBus::Entity'
) 'two cues and the silence between them'
# 같은 공간 잔향. 엔딩 B는 별관 복도 안이고 그 대답은 복도 끝에서 왔다.
Require-All $controller @(
	'AudioDirector->SetAcousticSpace(EIGAcousticSpace::Corridor);'
) 'the same reverb as ending B'
# 비언어음이므로 자막이 있어야 한다.
Require-All $controller @(
	'AIGHorrorHUD::PushAudioCaption(',
	'"둘, 쉬고, 하나"',
	'"복도 끝에서 대답하듯 두 번 두드리는 소리"'
) 'captions for both cues'

# --- 월드 타이머로는 한 프레임도 진행하지 않는다 ----------------------------
# 타이틀 메뉴가 SetPause(true)를 부르므로, 월드 타이머는 절대 틱하지 않는다.
Require-All $controller @(
	'SetPause(NewMode != EIGSystemMenuMode::Hidden);',
	'FTSTicker::GetCoreTicker().AddTicker(',
	'FTSTicker::GetCoreTicker().RemoveTicker('
) 'the engine ticker drives the thirty seconds'
Require-All $controllerHeader @(
	'FTSTicker::FDelegateHandle NightFiveTicker;',
	'bool AdvanceNightFive(float DeltaSeconds);'
) 'the engine ticker drives the thirty seconds'

# --- 화면 -------------------------------------------------------------------
# 로딩 없이 검정 화면. 타이틀 키아트도 메뉴 행도 그리지 않는다.
Require-All $hud @(
	'if (bSystemMenuNightFivePlaying)',
	'FLinearColor::Black',
	'SE_BLEND_Opaque',
	'DrawAudioCaption(',
	'NSLOCTEXT("IGHUD", "Menu.Night5", "다섯째 밤")'
) 'black screen and the row label'
# 한 번 재생하면 흐려진다. 사라지지는 않는다.
Require-All $hud @(
	'const bool bDimmed = ActionRow == IGFrontendMenuLayout::NightFiveAction',
	'bSystemMenuNightFiveSpent;',
	'(bDimmed ? 0.55f : 1.0f)'
) 'dimmed but present'
Require-All $hudHeader @(
	'bool bNightFiveAvailable = false;',
	'bool bNightFiveSpent = false;',
	'bool bNightFivePlaying = false;'
) 'presentation carries the three states'

# --- 검증 -------------------------------------------------------------------
Require-All $controller @(
	'MISSINGFLOOR_NIGHT5 PASS',
	'night five is not under continue',
	'night five reachable while paused',
	'the row was not selectable',
	'it never returned to the title'
) 'runtime slot probe'

Write-Host (
	"MISSINGFLOOR_NIGHT5_SLOT_CONTRACT PASS assertions=$assertions " +
	'seconds=30 cues=2 signal_at=3.2 answer_at=12.6 rows=6 screen_slot=1 ' +
	'save_files_written=0 paused_menu=0') `
	-ForegroundColor Green
