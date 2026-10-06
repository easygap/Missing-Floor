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
		throw "M1_CAPTURE_CONTRACT FAIL: missing file: $RelativePath"
	}
	return Get-Content -Raw -Encoding UTF8 -LiteralPath $path
}

function Assert-True {
	param(
		[Parameter(Mandatory)][bool]$Condition,
		[Parameter(Mandatory)][string]$Message
	)

	if (-not $Condition) {
		throw "M1_CAPTURE_CONTRACT FAIL: $Message"
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
$playerHeader = Read-ProjectText 'Source/IndieGame/Player/IGPlayerCharacter.h'
$player = Read-ProjectText 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
$hudHeader = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.h'
$hud = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.cpp'
$listener = Read-ProjectText 'Source/IndieGame/Entity/IGListenerEntity.cpp'
$greybox = Read-ProjectText 'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'
$toneSequence = Read-ProjectText 'Source/IndieGame/Audio/IGToneSequenceSoundWave.cpp'
$flashlight = Read-ProjectText 'Source/IndieGame/Player/IGFlashlightComponent.cpp'
$audioDirector = Read-ProjectText 'Source/IndieGame/Audio/IGMissingFloorAudioSubsystem.cpp'

Assert-ContainsAll $nightHeader @(
	'float FadeOutSeconds = 1.25f;',
	'int32 GetCaptureHandprintCount() const',
	'TArray<TObjectPtr<UStaticMeshComponent>> CaptureHandprints;',
	'float GetWakeFadeInSeconds() const;'
) 'night-loop declarations'
Assert-ContainsAll $night @(
	'Character->PlayCaptureFeedback(FadeOutSeconds, CutSeconds);',
	'Character->DisableInput(Controller);',
	'FMath::Max(FadeOutSeconds, 0.05f)',
	'SpawnCaptureHandprint(Character);',
	'WallProbeCount = 8',
	'WallProbeDistance = 260.0f',
	'FMath::Abs(Hit.ImpactNormal.Z) > 0.35f',
	'/Game/Prototype/Materials/M_MissingFloorHandprints.',
	'MaximumCaptureHandprints = 12',
	'MissingFloor.CaptureHandprint',
	'return 3.0f;',
	'return 2.2f;',
	'return 1.4f;',
	'return 0.4f;'
) 'capture reset and residue'
$captureBlock = Get-Block $night `
	'void AIGNightLoopDirector::HandlePlayerCaptured(APawn* Player)' `
	'void AIGNightLoopDirector::FinishReset()'
Assert-True (
	$captureBlock.IndexOf('Character->PlayCaptureFeedback(FadeOutSeconds, CutSeconds);') -lt
	$captureBlock.IndexOf('Character->DisableInput(Controller);')) `
	'capture feedback must start before input is disabled'
Assert-True (-not $captureBlock.Contains('+ 0.4f')) `
	'blackout reset must finish at the authored 1.25-second boundary'
# 되풀이될수록 짧게 끊는다. 첫 포획도 1초 안에 끊는다.
Assert-True $captureBlock.Contains(
	'CaptureCount <= 1 ? 0.95f : (CaptureCount == 2 ? 0.75f : 0.55f)') `
	'capture cut must shorten 0.95 -> 0.75 -> 0.55 seconds'

Assert-ContainsAll $playerHeader @(
	'void PlayCaptureFeedback(float DurationSeconds = 1.2f, float CutSeconds = -1.0f);',
	'void UpdateCaptureFeedback(float DeltaSeconds);',
	'float CaptureFeedbackRemainingSeconds = 0.0f;',
	'uint64 CaptureForceFeedbackHandle = 0;'
) 'player capture declarations'
Assert-ContainsAll $player @(
	'CaptureCameraKickDegrees = 3.2f',
	'CaptureHapticIntensity = 0.70f',
	'void AIGPlayerCharacter::PlayCaptureFeedback(',
	'HorrorHUD->PlayCaptureEmbrace(CaptureFeedbackDurationSeconds);',
	'EDynamicForceFeedbackAction::Start',
	'EDynamicForceFeedbackAction::Update',
	'EDynamicForceFeedbackAction::Stop',
	'AccessibilitySubsystem->AreHapticsEnabled()',
	'IGPlayerNoise::CaptureHapticIntensity * RemainingAlpha',
	'IGPlayerNoise::CaptureCameraKickDegrees',
	'if (!bReducedMotion && CaptureFeedbackRemainingSeconds > 0.0f)'
) 'camera and haptic feedback'

# 포획 중에는 실제 몸을 보여 주고 일반 HUD만 가린다.
Assert-ContainsAll $hudHeader @(
    'void PlayCaptureEmbrace(float DurationSeconds = 1.2f);',
    'bool DrawCaptureEmbrace(double CurrentTime);',
    'double CaptureEmbraceEndTime = -1.0;'
) 'capture HUD declarations'
Assert-ContainsAll $hud @(
    'if (DrawCaptureEmbrace(CurrentTime))',
    'Guidance.Interrupt();',
    'CurrentTime >= CaptureEmbraceStartTime && CurrentTime < CaptureEmbraceEndTime'
) 'capture HUD ownership'
Assert-True (-not $hud.Contains('T_FPCaptureEmbrace')) '포획용 손 그림을 HUD에서 읽으면 안 된다'
Assert-True (-not $hudHeader.Contains('CaptureEmbraceFrames')) '사용하지 않는 손 그림 배열이 남아 있다'
$captureDraw = Get-Block $hud 'bool AIGHorrorHUD::DrawCaptureEmbrace(' 'bool AIGHorrorHUD::DrawCaptureWakeEcho('
Assert-True (-not $captureDraw.Contains('DrawItem')) '포획 장면에 2D 팔을 겹치면 안 된다'

Assert-ContainsAll $listener @(
	'UIGToneSequenceSoundWave::CreateCaptureStruggle(this)',
	'OnPlayerCaptured.Broadcast(Player);'
) 'close capture sound'

# 잡힌 순간: 손전등이 튕겨 나가고, 얼굴은 한 번 물러났다가 끊기는 순간 눈앞에
# 닿는다. 눈앞에 머무는 시간과 주먹 쥔 덮치기 동작은 다시 들이지 않는다.
Assert-ContainsAll $player @(
	'Flashlight->PlayKnockLoose();',
	'Flashlight->ClearKnockLoose();',
	'SetManualCameraFade(',
	'AudioDirector->PlayCaptureCut();'
) 'capture cut'
Assert-True (-not $player.Contains('SpawnDryOneShotAt(')) `
	'the capture cut rides the score bus; only the held knock may be dry'
Assert-ContainsAll $flashlight @(
	'void UIGFlashlightComponent::PlayKnockLoose()',
	'Beam->SetWorldRotation(FRotator(WorldPitch, PreviousWorldRotation.Yaw + YawOffset, 0.0f));'
) 'knocked-loose torch'
Assert-True $audioDirector.Contains('void UIGMissingFloorAudioSubsystem::PlayCaptureCut()') `
	'capture cut player missing'
$holdBlock = Get-Block $listener `
	'// 넘어지는 동안 한 번 물러나 웅크렸다가' 'case EIGListenerState::Banging:'
Assert-ContainsAll $holdBlock @(
	'Victim->GetCaptureCutSeconds()',
	'const FVector Coil = CaptureFallenEye + CaptureStrikeLine * 70.0f',
	'FMath::Lerp(Coil, CaptureViewTarget,'
) 'coil and strike'
Assert-True (-not $holdBlock.Contains('EIGListenerBodyAnim::Lunge')) `
	'the fist-raised lunge must not return to the capture'
Assert-ContainsAll $greybox @(
	'NightLoop->GetCaptureHandprintCount() >= 1',
	'!NightLoop->IsCaptureResetInFlight()',
	'PlayerCharacter->InputEnabled()',
	'PHYSICAL_CAPTURE_CONTACT',
	'bTexturesReady &= Texture->IsFullyStreamedIn();',
	'reset incomplete (atBed=%d tier=%d handprints=%d recovery=%d input=%d)'
) 'capture runtime probe'
$replyBlock = Get-Block $toneSequence `
	'UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateWallKnockReply(UObject* Outer)' `
	'UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateAnswerKnockPattern('
Assert-ContainsAll $replyBlock @(
	'KnockIndex < 2',
	'const float Start = 0.42f * KnockIndex;'
) 'calm close double knock'

Write-Host (
	'M1 capture contract passed ({0} assertions).' -f $assertionCount) `
	-ForegroundColor Green
