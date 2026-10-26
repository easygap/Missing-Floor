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
$feel = Read-ProjectText 'Source/IndieGame/Player/IGCameraFeelModifier.cpp'
$stress = Read-ProjectText 'Source/IndieGame/Player/IGStressComponent.cpp'
$hudHeader = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.h'
$hud = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.cpp'
$listener = Read-ProjectText 'Source/IndieGame/Entity/IGListenerEntity.cpp'
$threats = Read-ProjectText 'Source/IndieGame/Entity/IGNightThreatDirector.cpp'
$manager = Read-ProjectText 'Source/IndieGame/Entity/IGManagerPatrol.cpp'
$greybox = Read-ProjectText 'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'
$toneSequence = Read-ProjectText 'Source/IndieGame/Audio/IGToneSequenceSoundWave.cpp'
$flashlight = Read-ProjectText 'Source/IndieGame/Player/IGFlashlightComponent.cpp'
$audioDirector = Read-ProjectText 'Source/IndieGame/Audio/IGMissingFloorAudioSubsystem.cpp'

# 잡힌 뒤의 문법(§5.4). 괴이마다 끊기는 때, 끊긴 뒤의 시간, 몸을 일으키는 시간,
# 흐른 시간이 다르다. 침대로 돌려보내지 않는다.
Assert-ContainsAll $nightHeader @(
	'int32 GetCaptureHandprintCount() const',
	'TArray<TObjectPtr<UStaticMeshComponent>> CaptureHandprints;',
	'float GetWakeFadeInSeconds() const;',
	'void RequestExternalCapture(APawn* Player, EIGCaptureKind Kind, const FTransform* WakeAt = nullptr);',
	'FIGCaptureSettledSignature OnCaptureSettled;'
) 'night-loop declarations'
Assert-ContainsAll $night @(
	'{1.48f, 0.92f, 1.5f, 120.0f}',
	'{1.70f, 1.00f, 1.6f, 90.0f}',
	'{1.30f, 0.80f, 1.3f, 60.0f}',
	'{1.20f, 1.60f, 0.0f, 180.0f}',
	'DragMinDistance = 300.0f',
	'ListenerGraceSeconds = 12.0f',
	'EoduksiniBatteryCost = 0.35f',
	'Character->PlayCaptureFeedback(Spec.CutSeconds + Spec.HoldSeconds, Spec.CutSeconds, Kind);',
	'Character->DisableInput(Controller);',
	'SpawnCaptureHandprint(Character);',
	'DropFlashlightAt(*Character);',
	'Torch->DrainBattery(IGNightLoop::EoduksiniBatteryCost);',
	# 잃는 시간은 포획 종류의 값이다. 추격 없음에서 덧댄 것에 눌린 것만 0으로 덮는다(0.10.0).
	'float LostSeconds = Spec.LostSeconds;',
	'It->ConsumeHourSeconds(LostSeconds)',
	'Entity->ReturnAfterCapture(IGNightLoop::ListenerGraceSeconds);',
	'OnCaptureSettled.Broadcast(Kind, LastCaptureSpot);',
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
) 'capture flow and residue'
$captureBlock = Get-Block $night `
	'void AIGNightLoopDirector::BeginCaptureReset(' `
	'FTransform AIGNightLoopDirector::ChooseCaptureWake('
Assert-True (
	$captureBlock.IndexOf('Character->PlayCaptureFeedback(') -lt
	$captureBlock.IndexOf('Character->DisableInput(Controller);')) `
	'capture feedback must start before input is disabled'
$finishBlock = Get-Block $night `
	'void AIGNightLoopDirector::FinishReset()' `
	'void AIGNightLoopDirector::Tick('
Assert-True ($finishBlock.Contains('CaptureWake.Rotator()') -and -not $finishBlock.Contains('WakeTransform.GetLocation()')) `
	'a capture must wake where the director chose, never at the night-start bed'

Assert-ContainsAll $playerHeader @(
	'EIGCaptureKind Kind = EIGCaptureKind::Listener);',
	'FLinearColor GetCaptureCutColour() const;',
	'void PlayComeTo(float Seconds);',
	'bool FindFootholdAway(const FVector& From, float MinDistance, float MaxAgeSeconds,',
	'bool FindLitFoothold(float MaxRoomDarkness, float MaxAgeSeconds,',
	'void UpdateCaptureFeedback(float DeltaSeconds);',
	'float CaptureFeedbackRemainingSeconds = 0.0f;',
	'uint64 CaptureForceFeedbackHandle = 0;'
) 'player capture declarations'
Assert-ContainsAll $player @(
	'CaptureHapticIntensity = 0.70f',
	'FootholdIntervalSeconds = 0.5;',
	'void AIGPlayerCharacter::PlayCaptureFeedback(',
	'HorrorHUD->PlayCaptureEmbrace(CaptureFeedbackDurationSeconds);',
	'EDynamicForceFeedbackAction::Start',
	'EDynamicForceFeedbackAction::Update',
	'EDynamicForceFeedbackAction::Stop',
	'AccessibilitySubsystem->AreHapticsEnabled()',
	'IGPlayerNoise::CaptureHapticIntensity * RemainingAlpha',
	'Flashlight->PlayKnockLoose();',
	'Flashlight->ClearKnockLoose();',
	'Flashlight->TriggerBrownOut(CaptureCutSeconds + 0.2f);',
	'SetManualCameraFade(',
	'AccessibilitySubsystem->IsReducedFlickerEnabled()'
) 'capture view'

# 화면을 흔들지 않는다(§18.3, §36). 기울기·떨림·색 번짐·흐림, 끊김 효과음은 돌아오면 안 된다.
foreach ($banned in @('CaptureCameraKickDegrees', 'Sin(Age * 71.0f)', 'GetTremor()', 'PlayCaptureCut',
	'CameraRotation.Roll -= 13.0f', 'CaptureImpactAlpha')) {
	Assert-True (-not $player.Contains($banned)) "capture camera shake came back: $banned"
}
Assert-True (-not $feel.Contains('SceneFringeIntensity') -and -not $feel.Contains('MotionBlurAmount')) `
	'the camera feel modifier must not smear the capture with fringe or blur'
Assert-True (-not $stress.Contains('UpdateTremor')) 'the fear tremor must not shake the view'
Assert-True (-not $audioDirector.Contains('PlayCaptureCut')) 'the capture cut thud and ringing were removed'
Assert-True (-not $toneSequence.Contains('CreateCaptureCut(')) 'the capture cut tone was removed'
Assert-True (-not $player.Contains('SpawnDryOneShotAt(')) `
	'only the held knock may be dry'

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
	'OnPlayerCaptured.Broadcast(Player);',
	'void AIGListenerEntity::ReturnAfterCapture(const float GraceSeconds)',
	'Event.TimeSeconds < PerceptionGraceUntil',
	'GetWorld()->GetTimeSeconds() >= PerceptionGraceUntil'
) 'close capture and grace'
Assert-ContainsAll $flashlight @(
	'void UIGFlashlightComponent::PlayKnockLoose()',
	'void UIGFlashlightComponent::SetDropped(const bool bInDropped)',
	'Beam->SetWorldRotation(FRotator(WorldPitch, PreviousWorldRotation.Yaw + YawOffset, 0.0f));'
) 'knocked-loose and dropped torch'
# 얼굴은 기어 올라와 눈앞에 멈춰 듣는다. 물러났다 덮치거나 프레임을 끊어 튀지 않는다.
$holdBlock = Get-Block $listener `
	'// 쓰러진 눈을 향해 기어 올라온다.' 'case EIGListenerState::Banging:'
Assert-ContainsAll $holdBlock @(
	'Victim->GetCaptureCutSeconds()',
	'IGListener::CaptureArriveFraction',
	'FMath::InterpEaseOut(0.0f, 1.0f, Arrive, 2.2f)'
) 'crawl and stop'
foreach ($banned in @('Coil', 'bFrozen', 'EIGListenerBodyAnim::Lunge')) {
	Assert-True (-not $holdBlock.Contains($banned)) "the capture face must not lunge or stutter: $banned"
}
# 다른 셋도 각자의 연출로 잡는다.
Assert-ContainsAll $threats @(
	'Loop->RequestExternalCapture(&Player, EIGCaptureKind::Eoduksini);',
	'Loop->RequestExternalCapture(Player, EIGCaptureKind::Guest, &Wake);',
	'void AIGNightThreatDirector::SpeakGuestBehind()',
	'void AIGNightThreatDirector::SlamGuestDoor()',
	'Latch->SetBroken(true);'
) 'eoduksini and guest captures'
Assert-ContainsAll $manager @(
	'Loop->RequestExternalCapture(Character, EIGCaptureKind::Manager, &Wake);',
	'Story->RemoveState(KeyTag);'
) 'manager capture'
Assert-True (-not (Get-Block $manager 'void AIGManagerPatrol::BeginCatch()' 'void AIGManagerPatrol::BeginCatchForProbe()').Contains('PlayScareKick')) `
	'the manager does not jolt the camera when he grabs her wrist'
Assert-ContainsAll $greybox @(
	'NightLoop->GetCaptureHandprintCount() >= 1',
	'!NightLoop->IsCaptureResetInFlight()',
	'PlayerCharacter->InputEnabled()',
	'NightLoop->GetLastWakeLocation()',
	'PHYSICAL_CAPTURE_CONTACT',
	'bTexturesReady &= Texture->IsFullyStreamedIn();',
	'reset incomplete (atWake=%d tier=%d handprints=%d recovery=%d input=%d)'
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
