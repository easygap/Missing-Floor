[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$assertionCount = 0

function Read-ProjectText {
	param([Parameter(Mandatory)][string]$RelativePath)

	$path = Join-Path $projectRoot $RelativePath
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "M1_WAKE_ECHO_CONTRACT FAIL: missing file: $RelativePath"
	}
	return Get-Content -Raw -Encoding UTF8 -LiteralPath $path
}

function Assert-True {
	param(
		[Parameter(Mandatory)][bool]$Condition,
		[Parameter(Mandatory)][string]$Message
	)

	if (-not $Condition) {
		throw "M1_WAKE_ECHO_CONTRACT FAIL: $Message"
	}
	$script:assertionCount++
}

function Assert-ContainsAll {
	param(
		[Parameter(Mandatory)][string]$Text,
		[Parameter(Mandatory)][string[]]$Needles,
		[Parameter(Mandatory)][string]$Context
	)

	foreach ($needle in $Needles) {
		Assert-True $Text.Contains($needle) "$Context missing token: $needle"
	}
}

function Get-Block {
	param(
		[Parameter(Mandatory)][string]$Text,
		[Parameter(Mandatory)][string]$Start,
		[Parameter(Mandatory)][string]$End
	)

	$startIndex = $Text.IndexOf($Start)
	$endIndex = $Text.IndexOf($End, $startIndex + $Start.Length)
	Assert-True ($startIndex -ge 0 -and $endIndex -gt $startIndex) `
		"could not isolate block: $Start"
	return $Text.Substring($startIndex, $endIndex - $startIndex)
}

$nightHeader = Read-ProjectText 'Source/IndieGame/Entity/IGNightLoopDirector.h'
$night = Read-ProjectText 'Source/IndieGame/Entity/IGNightLoopDirector.cpp'
$hudHeader = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.h'
$hud = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.cpp'
$greybox = Read-ProjectText 'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'
$story = Read-ProjectText 'Docs/STORY_BIBLE_MISSING_FLOOR.md'
$status = Read-ProjectText 'Docs/IMPLEMENTATION_STATUS.md'
$artMatrix = Read-ProjectText 'Docs/MISSING_FLOOR_ART_MATRIX.md'
$captureRunner = Read-ProjectText 'Scripts/Run-MissingFloor-NightCapture.bat'

# 깨어날 자리로 옮기고 눈이 뜨이는 첫 순간까지 입력을 잠근다(§5.4).
Assert-ContainsAll $nightHeader @(
	'bool IsCaptureResetInFlight() const { return bResetInFlight; }',
	'void FinishWakeRecovery();',
	'float GetWakeEchoSeconds() const;',
	'float GetWakeRecoverySeconds() const;',
	'FTimerHandle WakeRecoveryTimer;'
) 'wake recovery declarations'

Assert-ContainsAll $night @(
	'#include "Audio/IGAudioHelpers.h"',
	'#include "Audio/IGToneSequenceSoundWave.h"',
	'#include "Player/IGHorrorHUD.h"',
	'HorrorHUD->PlayCaptureWakeEcho(',
	'UIGToneSequenceSoundWave::CreateClothSettle(this)',
	'CaptureWake.GetLocation()',
	'&AIGNightLoopDirector::FinishWakeRecovery',
	'WakeEchoSeconds',
	'WakeRecoverySeconds',
	'FMath::Max(GetWakeFadeInSeconds(), GetWakeEchoSeconds())',
	'return 0.68f;',
	'return 0.48f;',
	'return 0.30f;',
	'return 0.16f;'
) 'wake recovery runtime'

$finishResetBlock = Get-Block $night `
	'void AIGNightLoopDirector::FinishReset()' `
	'void AIGNightLoopDirector::FinishWakeRecovery()'
Assert-True (-not $finishResetBlock.Contains('Character->EnableInput(Controller);')) `
	'input must stay locked while the wake echo owns the frame'
Assert-True (
	$finishResetBlock.IndexOf('Character->TeleportTo(') -lt
	$finishResetBlock.IndexOf('HorrorHUD->PlayCaptureWakeEcho(')) `
	'wake echo must begin after the player reaches the wake transform'
Assert-True (
	$finishResetBlock.IndexOf('HorrorHUD->PlayCaptureWakeEcho(') -lt
	$finishResetBlock.IndexOf('&AIGNightLoopDirector::FinishWakeRecovery')) `
	'wake echo must begin before the recovery timer is armed'

$finishRecoveryBlock = Get-Block $night `
	'void AIGNightLoopDirector::FinishWakeRecovery()' `
	'bool AIGNightLoopDirector::SpawnCaptureHandprint('
Assert-ContainsAll $finishRecoveryBlock @(
	'Character->EnableInput(Controller);',
	'CapturedPlayer = nullptr;',
	'bResetInFlight = false;'
) 'single input recovery exit'

# 깨어날 때는 그 자리의 시야와 암전만 남기고 입력 복귀까지 HUD를 가린다.
Assert-ContainsAll $hudHeader @(
    'void PlayCaptureWakeEcho(',
    'float OwnershipDurationSeconds = 0.68f);',
    'bool DrawCaptureWakeEcho(double CurrentTime);',
    'double CaptureWakeEchoStartTime = -1.0;',
    'double CaptureWakeEchoEndTime = -1.0;'
) 'wake HUD declarations'
Assert-ContainsAll $hud @(
    'SafeOwnershipDuration',
    'if (DrawCaptureWakeEcho(CurrentTime))',
    'CurrentTime >= CaptureWakeEchoStartTime && CurrentTime < CaptureWakeEchoEndTime'
) 'wake HUD ownership'
$wakeDrawBlock = Get-Block $hud 'bool AIGHorrorHUD::DrawCaptureWakeEcho(' 'float AIGHorrorHUD::MeasureTextWidth('
Assert-True (-not $wakeDrawBlock.Contains('DrawItem')) '기상 화면에 손 잔상을 그리면 안 된다'
Assert-True (-not $hud.Contains('CaptureEmbraceFrames')) '예전 손 잔상 리소스를 읽으면 안 된다'

Assert-ContainsAll $greybox @(
	'!NightLoop->IsCaptureResetInFlight()',
	'bWakeRecoveryFinished',
	'PlayerCharacter->InputEnabled()',
	'bInputRestored',
	'recovery=%d input=%d'
) 'runtime recovery probe'

Assert-ContainsAll $story @(
	'2026년 9월 29일',
	'## 26. 2026-08-11 제품 감사',
	'### 26.7 2026-08-11 델타 감사',
	'## 29. v2.8 — 포획 뒤 침대 복귀',
	'게임 시작**은 저녁의 프롤로그',
	'포획 뒤 시작**은 새 게임이나 밤 처음부터가 아니다',
	# 회차와 실제 입력 잠금 시간을 함께 확인한다.
	'| 1회 | 3.0초 |',
	'| 2회 | 2.2초 |',
	'| 3~4회 | 1.4초 |',
	'| 5회 이상 | 0.4초 |',
	'회차별 페이드 3.0/2.2/1.4/0.4초가 끝난 뒤',
	'1280×800',
	'https://gdconf.com/article/gdc-2026-state-of-the-game-industry-reveals-impact-of-layoffs-generative-ai-and-more/',
	'https://www.routinegame.com/',
	'https://blog.playstation.com/2026/07/13/coloratura-designing-a-world-where-sound-is-the-only-guide/'
) 'v2.8 design and trend audit'

Assert-ContainsAll $status @(
	'정사 v2.8 M1.1 포획 뒤 기상 잔향 구현',
	'게임 전체의 시작, 각 밤의 시작, 포획 뒤 재시작을 분리했다',
	'저녁 입주에서 시작하고',
	'잡은 괴이마다 다른 자리에서 깬다',
	'일반 HUD와 입력은',
	'M1.1 기상 잔향 계약',
	'구현 완료·체감 승인 대기'
) 'implementation status'
Assert-ContainsAll $artMatrix @(
	'| 1인칭 기상 잔향 |',
	'손 그림 합성 제거',
	'페이드 끝까지 HUD 점유',
	'1920×1080과 1280×800 D3D12 실렌더'
) 'art matrix wake reuse'
# README의 소개 화면과 링크는 Validate-Project.ps1에서 검사한다.
Assert-ContainsAll $captureRunner @(
	'IG_NIGHT_CAPTURE_RES_X',
	'IG_NIGHT_CAPTURE_RES_Y',
	'-ResX=%IG_NIGHT_CAPTURE_RES_X%',
	'-ResY=%IG_NIGHT_CAPTURE_RES_Y%'
) 'resolution-variable night capture'

$wakeMediaPath = Join-Path $projectRoot 'Docs/Media/readme/m1-capture-wake-echo.gif'
Assert-True (Test-Path -LiteralPath $wakeMediaPath -PathType Leaf) `
	'wake echo README GIF must exist'
$wakeMedia = Get-Item -LiteralPath $wakeMediaPath
Assert-True ($wakeMedia.Length -gt 100KB -and $wakeMedia.Length -lt 5MB) `
	'wake echo README GIF must stay inside the review size budget'
$gifHeader = [System.Text.Encoding]::ASCII.GetString(
	[System.IO.File]::ReadAllBytes($wakeMediaPath), 0, 6)
Assert-True ($gifHeader -in @('GIF87a', 'GIF89a')) `
	'wake echo README media must be a real GIF'

Write-Host (
	'M1 wake echo contract passed ({0} assertions).' -f $assertionCount) `
	-ForegroundColor Green
