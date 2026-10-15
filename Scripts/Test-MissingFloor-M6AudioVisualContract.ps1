[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot

function Read-ProjectText {
	param([Parameter(Mandatory = $true)][string]$RelativePath)
	$path = Join-Path $projectRoot $RelativePath
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Missing M6 contract file: $RelativePath"
	}
	return Get-Content -Raw -Encoding UTF8 -LiteralPath $path
}

function Assert-ContainsAll {
	param(
		[Parameter(Mandatory = $true)][string]$Text,
		[Parameter(Mandatory = $true)][string[]]$Tokens,
		[Parameter(Mandatory = $true)][string]$Label
	)
	foreach ($token in $Tokens) {
		if (-not $Text.Contains($token)) {
			throw "$Label contract is missing: $token"
		}
	}
}

$story = Read-ProjectText 'Docs/STORY_BIBLE_MISSING_FLOOR.md'
$audioHeader = Read-ProjectText `
	'Source/IndieGame/Audio/IGMissingFloorAudioSubsystem.h'
$audioSource = Read-ProjectText `
	'Source/IndieGame/Audio/IGMissingFloorAudioSubsystem.cpp'
$toneHeader = Read-ProjectText 'Source/IndieGame/Audio/IGToneSequenceSoundWave.h'
$toneSource = Read-ProjectText 'Source/IndieGame/Audio/IGToneSequenceSoundWave.cpp'
$audioHelpers = Read-ProjectText 'Source/IndieGame/Audio/IGAudioHelpers.cpp'
$playerHeader = Read-ProjectText 'Source/IndieGame/Player/IGPlayerCharacter.h'
$playerSource = Read-ProjectText 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
$accessibilityHeader = Read-ProjectText `
	'Source/IndieGame/Accessibility/IGAccessibilitySubsystem.h'
$accessibilitySource = Read-ProjectText `
	'Source/IndieGame/Accessibility/IGAccessibilitySubsystem.cpp'
$controllerSource = Read-ProjectText 'Source/IndieGame/Player/IGPlayerController.cpp'
$hudSource = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.cpp'
$worldSource = Read-ProjectText 'Source/IndieGame/Core/IGPrologueWorldScene.cpp'
$stressSource = Read-ProjectText 'Source/IndieGame/Player/IGStressComponent.cpp'
$listenerSource = Read-ProjectText 'Source/IndieGame/Entity/IGListenerEntity.cpp'
$nightThreeSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorNightThreeDirector.cpp'
$nightFourSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.cpp'
$narrativeSource = Read-ProjectText `
	'Source/IndieGame/Narrative/IGMissingFloorNarrativeSubsystem.cpp'
$greyboxSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'
$engineConfig = Read-ProjectText 'Config/DefaultEngine.ini'
$gameConfig = Read-ProjectText 'Config/DefaultGame.ini'
$buildRules = Read-ProjectText 'Source/IndieGame/IndieGame.Build.cs'

Assert-ContainsAll $story @(
	'## 31. v3.0 — M6 공간 음향·표면 소음·야간 연출 실행판',
	'코드와 자동 게이트 완료·체감 승인 대기',
	'ImageGen 출력을 추가하지 않았다',
	'WORLD는 최종 −24dB',
	'야간 적색 비상등과 스트레스에 따른 시야·색 변화'
) 'M6 story'

Assert-ContainsAll $audioHeader @(
	'enum class EIGAudioBus',
	'Entity,',
	'Player,',
	'Puzzle,',
	'World,',
	'UI,',
	'Score,',
	'PlayTruthConfirmation(int32 ConfirmationIndex)',
	'PlayEndingATuningResolution()',
	'ValidateContract(FString& OutFailure) const'
) 'M6 audio header'
Assert-ContainsAll $audioSource @(
	'TEXT("BUS_ENTITY")',
	'TEXT("BUS_PLAYER")',
	'TEXT("BUS_PUZZLE")',
	'TEXT("BUS_WORLD")',
	'TEXT("BUS_UI")',
	'TEXT("BUS_SCORE")',
	'TEXT("BUS_SPEECH")',
	'0.0f,   // ENTITY',
	'-3.0f,  // PLAYER',
	'-1.0f,  // PUZZLE',
	'-8.0f,  // WORLD',
	'-10.0f, // UI',
	'-6.0f,  // SCORE',
	'-2.0f   // SPEECH',
	'4,  // ENTITY',
	'6,  // PLAYER',
	'6,  // PUZZLE',
	'12, // WORLD',
	'Oldest->FadeOut(0.12f, 0.0f)',
	'IGMissingFloorMix::EntityNearDistance;',
	'GetBusDuckingDecibels(Bus)',
	'return -4.0f;',
	'return -6.0f;',
	'return -16.0f;',
	'constexpr float SilentDecibels = -96.0f',
	'ReleaseSeconds = ActiveScoreState == EIGAudioThreatState::Chasing',
	'? 4.0f',
	'IsTitleReplyTime(FDateTime::Now())',
	'constexpr float TitleCycleSeconds = 12.0f',
	'constexpr float TitleReplyDelaySeconds = 1.15f'
) 'M6 six-bus runtime'

Assert-ContainsAll $toneHeader @(
	'CreateSurfaceFootstep(',
	'CreateTuningMotif(',
	'CreateTuningStrike(',
	'CreateCavityDrone(',
	'CreateChaseScore('
) 'M6 procedural score header'
Assert-ContainsAll $toneSource @(
	'const float Cents = -30.0f + Strike * 2.0f',
	'if (bResolvedEndingA)',
	# 두 음은 이름이 있다. 맨 숫자로 찾으면 다른 화음에서도 걸린다.
	'constexpr float NoteE4 = 329.63f;',
	'constexpr float NoteA4 = 440.00f;',
	'44.0f, 0.095f',
	'180.0f, 0.027f',
	'constexpr float Beat = 60.0f / 118.0f',
	'52.0f, 0.15f * Accent',
	'{220.0f, 223.0f, 227.0f}'
) 'M6 procedural score synthesis'
Assert-ContainsAll $narrativeSource @(
	'AudioDirector->PlayTruthConfirmation(',
	'GetConfirmedTruthCount()'
) 'M6 truth-score bridge'
Assert-ContainsAll $audioSource @(
	'UIGToneSequenceSoundWave::CreateTuningMotif(this, true)'
) 'M6 ending-A resolution synthesis'
Assert-ContainsAll $nightFourSource @(
	'AudioDirector->PlayEndingATuningResolution()',
	'AudioDirector->SetAuthoredSilence(true)',
	'AudioDirector->SetThreatState(EIGAudioThreatState::Finale)'
) 'M6 finale score bridge'

Assert-ContainsAll $playerSource @(
	'{0.15f, 0.05f, 0.50f}',
	'{0.17f, 0.05f, 0.55f}',
	'{0.22f, 0.08f, 0.62f}',
	'{0.14f, 0.05f, 0.48f}',
	'{0.28f, 0.12f, 0.70f}',
	'{0.30f, 0.14f, 0.72f}',
	'return 0.86f;',
	'return 0.78f;',
	'CreateSurfaceFootstep(',
	'EIGAudioBus::Player'
) 'M6 footstep matrix'
Assert-ContainsAll $worldSource @(
	'TEXT("Footstep.Vinyl")',
	'TEXT("Footstep.Concrete")',
	'TEXT("Footstep.MetalStair")',
	'TEXT("Footstep.Rooftop")',
	'TEXT("Footstep.GypsumDebris")',
	'EmergencyPractical->SetVolumetricScatteringIntensity(0.14f)',
	'FilmGrainIntensity = 0.0f',
	'AutoExposureMaxBrightness = bSealed ? 1.30f : 5.0f'
) 'M6 world surface and night visual'
Assert-ContainsAll $stressSource @(
	'Settings.bOverride_FilmGrainIntensity = true',
	'Settings.FilmGrainIntensity = 0.0f'
) 'M6 pursuit post process'

Assert-ContainsAll $accessibilityHeader @(
	'bool bMicrophoneNoiseEnabled = false',
	'never records, stores or transmits captured samples'
) 'M6 microphone default'
Assert-ContainsAll $accessibilitySource @(
	'TEXT("MicrophoneNoiseEnabled")',
	'TEXT("IGMicrophoneMode")'
) 'M6 microphone persistence'
Assert-ContainsAll $playerHeader @(
	'Audio::FAudioCaptureSynth* MicrophoneCaptureSynth = nullptr',
	'TArray<float> MicrophoneScratchSamples'
) 'M6 microphone ownership'
Assert-ContainsAll $playerSource @(
	'OpenDefaultStream()',
	'StartCapturing()',
	'GetAudioData(MicrophoneScratchSamples)',
	'MicrophoneScratchSamples.Reset()',
	'MicrophoneCalibrationSeconds',
	'MicrophoneReportCooldownSeconds',
	'delete MicrophoneCaptureSynth',
	'소리는 저장하지 않음'
) 'M6 ephemeral microphone envelope'
Assert-ContainsAll $hudSource @(
	'마이크 소리 사용',
	'헤드폰을 쓰면 소리가 나는 방향을 구분하기 쉽습니다.',
	'bSystemMenuIsTitle && !bSystemMenuNightFivePlaying',
	'Missing Floor'
) 'M6 user-facing audio UI'
Assert-ContainsAll $controllerSource @(
	'HeadphoneRecommendationShown',
	'HeadphoneRecommendationDeadline = FPlatformTime::Seconds() + 2.5',
	'const bool bKeepTitleSoundscape =',
	'NewMode == EIGSystemMenuMode::DisplaySettings',
	'NewMode == EIGSystemMenuMode::Credits',
	'AudioDirector->SetTitleMode(bKeepTitleSoundscape)',
	'Settings.bMicrophoneNoiseEnabled = !Settings.bMicrophoneNoiseEnabled'
) 'M6 title and microphone controller'

Assert-ContainsAll $audioHelpers @(
	'Settings.SpatializationAlgorithm = GetSpatializationAlgorithm()',
	'EIGOutputMode ActiveOutputMode = EIGOutputMode::Headphones',
	'AudioDirector->PrepareSound(Sound, Bus)',
	'AudioDirector->RegisterComponent(Component, Bus)'
) 'M6 spatial helper'
Assert-ContainsAll $engineConfig @(
	'SpatializationPlugin=Resonance Audio',
	'ReverbPlugin=Resonance Audio',
	'AudioSampleRate=48000'
) 'M6 engine audio config'
Assert-ContainsAll $buildRules @(
	'PrivateDependencyModuleNames.Add("AudioCaptureCore")'
) 'M6 microphone build rules'
$projectDescriptor = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'IndieGame.uproject') | ConvertFrom-Json
foreach ($pluginName in @('AudioCapture', 'ResonanceAudio')) {
	$plugin = $projectDescriptor.Plugins | Where-Object { $_.Name -eq $pluginName }
	if ($null -eq $plugin -or -not $plugin.Enabled) {
		throw "M6 required plugin is not enabled: $pluginName"
	}
}
Assert-ContainsAll $gameConfig @(
	'ProjectName=Missing Floor',
	'ProjectVersion=0.10.0',
	'CompanyName=easygap'
) 'M6 product metadata'

Assert-ContainsAll $nightThreeSource @(
	'AudioDirector->SetAuthoredSilence(true)',
	'EndAnswerSilence()',
	'SuppressHeartbeat('
) 'M6 P4 silence'
Assert-ContainsAll $listenerSource @(
	'EIGAudioThreatState::Investigating',
	'EIGAudioThreatState::Chasing',
	'AudioDirector->SetEntityDistance(Distance)'
) 'M6 listener audio state bridge'
Assert-ContainsAll $greyboxSource @(
	'EProbeStep::AudioVisualContract',
	'GetEffectiveBusDecibels(EIGAudioBus::Score)',
	'GetEffectiveBusDecibels(EIGAudioBus::World)',
	'GetEffectiveBusDecibels(EIGAudioBus::Player)',
	'MISSINGFLOOR_M6_AUDIO PASS'
) 'M6 runtime probe'
# README의 소개 화면과 링크는 Validate-Project.ps1에서 검사한다.

Write-Host 'MISSING_FLOOR_M6_AUDIO_VISUAL_CONTRACT PASS buses=6 surfaces=6 scores=3 hrtf=1 microphone_default_off=1'
