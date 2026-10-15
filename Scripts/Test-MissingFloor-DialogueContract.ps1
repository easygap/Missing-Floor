[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$hudHeader = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGHorrorHUD.h')
$hudSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGHorrorHUD.cpp')
$accessibilityHeader = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Accessibility/IGAccessibilitySubsystem.h')
$accessibilitySource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Accessibility/IGAccessibilitySubsystem.cpp')
$controller = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGPlayerController.cpp')
$assertionCount = 0

function Assert-True {
	param(
		[Parameter(Mandatory = $true)][bool]$Condition,
		[Parameter(Mandatory = $true)][string]$Message
	)
	if (-not $Condition) {
		throw "MISSINGFLOOR_DIALOGUE_CONTRACT FAIL: $Message"
	}
	$script:assertionCount++
}

function Assert-ContainsAll {
	param(
		[Parameter(Mandatory = $true)][string]$Text,
		[Parameter(Mandatory = $true)][string[]]$Needles,
		[Parameter(Mandatory = $true)][string]$Context
	)
	foreach ($needle in $Needles) {
		Assert-True $Text.Contains($needle) "$Context 누락: $needle"
	}
}

Assert-ContainsAll $hudHeader @(
	'EIGDialogueChannel',
	'InnerVoice',
	'Conversation',
	'VoiceSubtitle',
	'Device',
	'EIGDialoguePriority',
	'FIGDialogueMessage',
	'PushDialogue',
	'ShowDialogue',
	'GetDialogueRenderSample',
	'InitializeDialogueSurfaceTextures',
	'DrawRoundedHudSurface',
	'DrawDialogueFilm',
	'DrawDialoguePanel',
	'DrawLeftAlignedText',
	'PrepareDialoguePage',
	'WrapHudText',
	'DialogueQueue',
	'AudioCaptionQueue',
	'bCurrentDialogueHasContinuation',
	'bDialogueLastInsideSafeArea',
	'DialogueFilmTexture',
	'HudRoundedMaskTexture'
) '하단 대화 UI 공용 표면'

Assert-ContainsAll $hudSource @(
	'MaximumDialogueQueueDepth = 6',
	'MaximumAudioCaptionQueueDepth = 4',
	'StoryDialogueMaximumQueueAge = 14.0',
	'AmbientDialogueMaximumQueueAge = 6.0',
	'DialogueGlyphsPerSecond = 11.5f',
	'DialogueMinimumSeconds = 2.2f',
	'DialogueMaximumSeconds = 9.0f',
	'void AIGHorrorHUD::EnqueueDialogue(',
	'Queued.MinimumDurationSeconds = FMath::Max(',
	'> static_cast<uint8>(CurrentDialogue.Priority)',
	'DialogueQueue.Insert(MoveTemp(CurrentDialogue), 0)',
	'if (!Next.bContinuation && CurrentTime - Next.QueuedAt > MaximumAge)',
	'Next.Scope.Context == CompletedContext',
	'void AIGHorrorHUD::SuspendDialoguePresentation(',
	'void AIGHorrorHUD::ResumeDialoguePresentation(',
	'DialogueEndTime += OccludedDuration'
) '중복 억제·우선순위·중단 복원 큐'

Assert-ContainsAll $hudSource @(
	'void AIGHorrorHUD::PrepareDialoguePage(',
	'const int32 MaximumLines = Settings.CaptionSizeScale > 1.25f ? 3 : 2',
	'Continuation.bContinuation = true',
	'CurrentDialogue.Line = FText::FromString(FString::Join(Lines, TEXT("\n")))',
	'WrapHudText(',
	'FChar::IsWhitespace(Character)',
	'FCString::Strchr(TEXT(".,!?;:…。！？、，"), Character)',
	'OutRemainder = Remaining.TrimStartAndEnd()',
	'"DialogueContinues"',
	'"이어짐"',
	'ContinuationRawWidth * ContinuationScale'
) '한국어 줄바꿈·무손실 이어 보기'
Assert-True (-not $hudSource.Contains(
	'const FString Ellipsis = TEXT("…")')) '메시지 본문이 말줄임표로 손실된다'
Assert-True (-not $hudHeader.Contains('CurrentThought')) '단일 덮어쓰기 생각 슬롯이 남아 있다'

Assert-ContainsAll $hudSource @(
	'GetResolutionTextScale(Settings.CaptionSizeScale)',
	'Canvas->ClipY / 1080.0f',
	'Settings.CaptionSafeAreaScale',
	'Settings.CaptionBackgroundOpacity',
	'CurrentDialogue.Channel == EIGDialogueChannel::InnerVoice',
	'CurrentDialogue.Channel == EIGDialogueChannel::Device',
	'const int32 MaximumCaptionLines = Settings.CaptionSizeScale > 1.25f ? 3 : 2',
	'Accessibility->IsReducedCameraMotionEnabled()',
	'Elapsed / 0.24',
	'Remaining / 0.16',
	'LineStep = BodyHeight * (',
	'CurrentDialogueLines.Num() >= 3 ? 1.36f : 1.32f',
	'const bool bLowerLaneBusy = bDialogueVisible || bAudioCaptionVisible;',
	'if (!bLowerLaneBusy && !bFadedOut)',
	'const float DialogueLaneGap = 14.0f',
	'DrawLeftAlignedText('
) '화면 크기·안전 영역·동작 감소·HUD 레인'

Assert-ContainsAll $hudSource @(
	'/Game/Prototype/Textures/T_HudDialogueFilm_D.T_HudDialogueFilm_D',
	'HudRoundedMaskTextureSize = 64',
	'Canvas->ClipX * 0.56f',
	'Canvas->ClipX * 0.42f',
	'520.0f * ResolutionScale',
	'840.0f * ResolutionScale',
	'const bool bUseTextOutline = Settings.CaptionBackgroundOpacity < 0.42f',
	'const float WaveHeights[] = {5.0f, 11.0f, 16.0f, 8.0f}',
	'const FString Inner = DisplayCaption.Mid(1, DisplayCaption.Len() - 2);',
	'DisplayCaption = Inner.TrimStartAndEnd();',
	'DrawDialogueFilm(',
	'SpeakerChipHeight * 0.5f'
) '2026 하단 글래스 표면·화자 칩·효과음 캡슐'
Assert-True (-not $hudSource.Contains('FVector2D(3.0f * ResolutionScale, PanelHeight)')) (
	'구형 디버그형 왼쪽 강조 막대가 남아 있다')

Assert-ContainsAll $accessibilityHeader @(
	'bSubtitlesEnabled',
	'bSoundCaptionsEnabled',
	'CaptionSizeScale',
	'CaptionBackgroundOpacity',
	'CaptionSafeAreaScale',
	'AreSubtitlesEnabled',
	'AreSoundCaptionsEnabled'
) '대사와 비언어음의 독립 접근성 설정'
Assert-ContainsAll $accessibilitySource @(
	'MaximumCaptionScale = 2.0f',
	'TEXT("SoundCaptionsEnabled")',
	'TEXT("CaptionBackgroundOpacity")',
	'IGNoSubtitles',
	'IGNoSoundCaptions',
	'IGCaptionBackground='
) '200% 확대·배경·명령행 검증 경로'
Assert-ContainsAll $controller @(
	'RowCount = IGSettingsMenuLayout::AccessibilityRowCount',
	'Settings.bSoundCaptionsEnabled = !Settings.bSoundCaptionsEnabled',
	'Settings.CaptionBackgroundOpacity = FMath::Clamp(',
	'FrontendProbeNextActionTime = Now + 0.30',
	'2.0f'
) '접근성 메뉴 입력 연결'

# 읽기 시간 정책을 C++과 독립적으로 다시 계산한다. 요청 시간이 짧아도
# 자동 읽기 시간이 이기고, 연출상 긴 요청 시간은 줄이지 않아야 한다.
function Get-DialogueDuration {
	param([string]$Line, [double]$Minimum)
	$glyphs = 0
	foreach ($character in $Line.ToCharArray()) {
		if (-not [char]::IsWhiteSpace($character)) {
			$glyphs++
		}
	}
	$reading = [Math]::Min(9.0, [Math]::Max(2.2, 1.15 + $glyphs / 11.5))
	return [Math]::Max($reading, $Minimum)
}
$shortDuration = Get-DialogueDuration '통화 연결 불가.' 0.0
$longDuration = Get-DialogueDuration (
	'끝낸 작업이면 시각이랑 수치가 남아야 해. 아무것도 없어.') 0.0
$authoredDuration = Get-DialogueDuration '방금 위에서.' 6.0
Assert-True ($shortDuration -ge 2.2) '짧은 기기 메시지 최소 읽기 시간 미달'
Assert-True ($longDuration -gt $shortDuration) '긴 한국어 문장의 읽기 시간이 늘어나지 않음'
Assert-True ([Math]::Abs($authoredDuration - 6.0) -lt 0.001) '연출 지정 최소 시간이 축소됨'
Assert-True ($longDuration -le 9.0) '자동 읽기 시간이 상한을 초과함'

# 최대 확대에서도 720p~1440p 하단 패널이 안전 영역 안에 들어오는지
# 최악의 3줄+화자 조합으로 확인한다. 실제 글리프 경계는 Shipping 프로브가
# 별도로 검사한다.
foreach ($profile in @(
	@{ Width = 1280.0; Height = 720.0 },
	@{ Width = 1600.0; Height = 900.0 },
	@{ Width = 1920.0; Height = 1080.0 },
	@{ Width = 2560.0; Height = 1440.0 }
)) {
	$resolutionScale = [Math]::Min(
		2.0,
		[Math]::Max(0.85, $profile.Height / 1080.0))
	$textScale = 2.0 * $resolutionScale
	$safeArea = 0.80
	$safeWidth = $profile.Width * $safeArea
	$panelWidth = [Math]::Min(
		[Math]::Min(
			840.0 * $resolutionScale,
			[Math]::Max(520.0 * $resolutionScale, $profile.Width * 0.56)),
		[Math]::Max(280.0, $safeWidth - 48.0 * $resolutionScale))
	$panelX = ($profile.Width - $panelWidth) * 0.5
	$safeX = ($profile.Width - $safeWidth) * 0.5
	$bodyHeight = [Math]::Max(16.0, 19.0 * $textScale)
	$lineStep = $bodyHeight * 1.36
	$speakerScale = $textScale * 0.78
	$speakerHeight = [Math]::Max(11.0, 14.0 * $speakerScale)
	$speakerChipHeight = [Math]::Max(
		24.0 * $resolutionScale,
		$speakerHeight + 10.0 * $resolutionScale)
	$panelHeight = 14.0 * $resolutionScale + $speakerChipHeight +
		10.0 * $resolutionScale + $bodyHeight + 2.0 * $lineStep +
		26.0 * $resolutionScale
	$safeY = $profile.Height * (1.0 - $safeArea) * 0.5
	$panelYRaw = $profile.Height - $safeY - $panelHeight - 42.0 * $resolutionScale
	$panelYMinimum = $safeY + 24.0 * $resolutionScale
	$panelYMaximum = $profile.Height - $safeY - $panelHeight - 24.0 * $resolutionScale
	$panelY = [Math]::Min(
		$panelYMaximum,
		[Math]::Max($panelYMinimum, $panelYRaw))
	Assert-True ($panelX -ge $safeX - 0.1) (
		"최대 확대 대화창 왼쪽 안전 영역 침범: $($profile.Width)x$($profile.Height)")
	Assert-True ($panelX + $panelWidth -le $profile.Width - $safeX + 0.1) (
		"최대 확대 대화창 오른쪽 안전 영역 침범: $($profile.Width)x$($profile.Height)")
	Assert-True ($panelY -ge $safeY - 0.1) (
		"최대 확대 대화창 위쪽 안전 영역 침범: $($profile.Width)x$($profile.Height)")
	Assert-True ($panelY + $panelHeight -le $profile.Height - $safeY + 0.1) (
		"최대 확대 대화창 아래쪽 안전 영역 침범: $($profile.Width)x$($profile.Height)")
}

Write-Host (
	"MISSINGFLOOR_DIALOGUE_CONTRACT PASS assertions=$assertionCount " +
	'dialogue_queue=1 speaker=1 device=1 pagination=1 no_truncation=1 ' +
	'reading_time=1 max_scale=2.0 layout_profiles=4 sound_lane=1 reduced_motion=1'
) -ForegroundColor Green
