[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Read-Source([string]$RelativePath) {
	Get-Content -Raw -Encoding UTF8 -LiteralPath (
		Join-Path $projectRoot $RelativePath)
}

$ambience = Read-Source 'Source/IndieGame/Audio/IGAmbienceSoundWave.cpp'
$tone = Read-Source 'Source/IndieGame/Audio/IGToneSequenceSoundWave.cpp'
$stress = Read-Source 'Source/IndieGame/Player/IGStressComponent.cpp'
$mixDirector = Read-Source 'Source/IndieGame/Audio/IGMissingFloorAudioSubsystem.cpp'
$attenuation = Read-Source 'Source/IndieGame/Audio/IGAudioHelpers.cpp'
$listener = Read-Source 'Source/IndieGame/Entity/IGListenerEntity.cpp'
$dust = Read-Source 'Source/IndieGame/Environment/IGDustSubsystem.h'
$beamDust = Read-Source 'Source/IndieGame/Player/IGBeamDustComponent.cpp'
$worldScene = Read-Source 'Source/IndieGame/Core/IGPrologueWorldScene.cpp'
$nightThree = Read-Source 'Source/IndieGame/Entity/IGMissingFloorNightThreeDirector.cpp'
$nightFour = Read-Source 'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.cpp'
$nightPhaseScene = Read-Source 'Source/IndieGame/Entity/IGNightPhaseDirector.cpp'
$greybox = Read-Source 'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'
$assertions = 0

function Require-All(
	[string]$Source,
	[string[]]$Needles,
	[string]$ContractName) {
	foreach ($needle in $Needles) {
		if (-not $Source.Contains($needle)) {
			throw "$ContractName audio invariant is missing: $needle"
		}
		$script:assertions++
	}
}

# 403호 룸톤(냉장고 험)의 기계음 네 대역. 402호 문 너머에 이 험이 없다는 게
# 단서라서, 대역이 빠지면 플레이어가 비교할 소리가 없어진다.
Require-All $ambience @(
	'StableSine(31.0f, SampleIndex)',
	'StableSine(60.0f, SampleIndex)',
	'StableSine(120.0f, SampleIndex)',
	'StableSine(240.0f, SampleIndex)'
) '403호 룸톤'

# 편의점 징글은 낮의 유일한 음악으로 남았다(§10.2). 76BPM 여덟 마디.
Require-All $tone @(
	'BaseBeatSeconds = 60.0f / 76.0f',
	'32.0f * Beat',
	'EIGToneWaveform::Triangle',
	'EIGToneWaveform::SoftSquare'
) '편의점 징글'

# 다섯째 새벽의 벽 너머 배관과 밤4 물 마스크가 같은 물 베드를 쓴다. 물의
# 무게는 52Hz 사인이 맡는다.
Require-All $tone @(
	'CreateFloodedCorridorWaterBed(',
	'Notes.Add({0.0f, 9.0f, 52.0f'
) '물 베드'

# 숨 참기와 밤3·밤4의 두 침묵이 심박 억제를 같이 쓴다. 풀릴 때 한 박을 치는
# 경로까지 있어야 침묵이 끝난 순간이 몸으로 들린다.
Require-All $stress @(
	'void UIGStressComponent::SuppressHeartbeat(',
	'HeartbeatSuppressionRemaining',
	'HeartbeatComponent->Stop();',
	'PlayHeartbeat(FMath::Max(Stress, 0.38f))'
) '심박 억제'

# §10.4 거리 리버브 문법: 건물 반향이 거리의 두 번째 축이다. 복도와 계단실
# 두 프리셋만 존재하고, 옥상은 프리셋의 부재로 드라이하게 남는다.
Require-All $mixDirector @(
	'const FName AcousticSpaceReverbTag(TEXT("MissingFloor.AcousticSpace"));',
	'constexpr float CorridorDecayTime = 1.25f',
	'constexpr float StairwellDecayTime = 2.70f',
	'constexpr float CorridorDecayHFRatio = 0.72f',
	'constexpr float StairwellDecayHFRatio = 0.95f',
	'REVERB_MISSING_FLOOR_CORRIDOR',
	'REVERB_MISSING_FLOOR_STAIRWELL',
	'AcousticPresets[static_cast<int32>(EIGAcousticSpace::Open)] = nullptr;',
	'UGameplayStatics::ActivateReverbEffect(',
	'UGameplayStatics::DeactivateReverbEffect(',
	'constexpr float AcousticCrossfadeSeconds = 0.90f',
	'constexpr float AcousticPollIntervalSeconds = 0.20f',
	'case EIGFootstepSurface::MetalStair:',
	'return EIGAcousticSpace::Stairwell;',
	'case EIGFootstepSurface::Rooftop:',
	'return EIGAcousticSpace::Open;',
	'SoundClass->Properties.bReverb =',
	'SoundClass->Properties.Default2DReverbSendAmount = 0.0f;',
	'Settings.ReverbSendMethod = EReverbSendMethod::Manual;',
	'Settings.ReverbSendMethod = EReverbSendMethod::Linear;',
	'constexpr float EntityReverbFloor = 0.22f',
	'constexpr float EntityReverbCeiling = 0.95f',
	'constexpr float PlayerManualReverbSend = 0.30f',
	'stairwell does not ring twice the corridor',
	'bare concrete must hold high frequencies longer',
	'entity cues do not ride the building reverb'
) '§10.4 reverb grammar'
# UI와 스코어만 건물 반향에서 빠진다. 나머지 네 버스는 반드시 방을 탄다.
if ($mixDirector -notmatch '(?s)constexpr bool BusUsesBuildingReverb\[\]\s*=\s*\{\s*true,[^}]*true,[^}]*true,[^}]*true,[^}]*false,[^}]*false') {
	throw 'ENTITY/PLAYER/PUZZLE/WORLD must ride the building reverb while UI/SCORE stay dry.'
}
$assertions++
# 전 공간 원샷이 한 지점을 지나야 거리 축이 하나로 유지된다.
Require-All $attenuation @(
	'UIGMissingFloorAudioSubsystem::ConfigureReverbSend(',
	'if (bForceDry)',
	'Settings.bEnableReverbSend = false;',
	'UAudioComponent* SpawnDryOneShotAt('
) '§10.4 attenuation routing'
# §21.3: 포획 노크만 리버브 0이다. 그 순간 그는 나를 안고 있다.
if (-not $listener.Contains('IGAudio::SpawnDryOneShotAt(')) {
	throw 'The capture knock must arrive completely dry (§21.3).'
}
$assertions++
$dryCueCount = 0
foreach ($file in @(
	'Source/IndieGame/Entity/IGListenerEntity.cpp',
	'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp',
	'Source/IndieGame/Entity/IGMissingFloorFifthDawnDirector.cpp',
	'Source/IndieGame/Entity/IGMissingFloorNightThreeDirector.cpp',
	'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.cpp',
	'Source/IndieGame/Player/IGPlayerCharacter.cpp')) {
	$dryCueCount += ([regex]::Matches(
		(Read-Source $file),
		'IGAudio::SpawnDryOneShotAt\(')).Count
}
if ($dryCueCount -ne 1) {
	throw ('Exactly one cue in the game may be dry, or the grammar stops ' +
		"meaning anything; found $dryCueCount.")
}
$assertions++
$busTaggedAttenuationCount = ([regex]::Matches(
	(($listener + (Read-Source 'Source/IndieGame/Entity/IGMissingFloorNightThreeDirector.cpp')) +
		(Read-Source 'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.cpp')) +
		(Read-Source 'Source/IndieGame/Entity/IGMissingFloorPuzzleOneDirector.cpp'),
	'IGAudio::MakeAttenuation\((?s).{0,220}?EIGAudioBus::')).Count
if ($busTaggedAttenuationCount -lt 4) {
	throw ('Every persistent audio bed must declare its bus so the reverb ' +
		"curve matches; found $busTaggedAttenuationCount.")
}
$assertions++

# §10.3 분진 낙하, §21.3 파라미터: 6kHz 하이패스·지수 감쇠·0.90초. 그보다
# 낮은 성분이 생기면 가루가 아니라 벽이 움직이거나 파편이 떨어지는 소리다.
Require-All $tone @(
	'CreatePlasterDustFall(',
	'IGPlasterDustFall',
	'0.000f, 0.900f, 6200.0f'
) '§10.3 dust fall synthesis'
$dustFallBody = [regex]::Match(
	$tone,
	'(?s)UIGToneSequenceSoundWave\* UIGToneSequenceSoundWave::CreatePlasterDustFall\(.*?\n\}').Value
if ([string]::IsNullOrWhiteSpace($dustFallBody)) {
	throw 'The dust-fall generator body could not be located.'
}
$assertions++
$dustBands = @()
$dustEndSeconds = 0.0
# Inline notes: {start, duration, band, amplitude, attack, release, waveform}.
foreach ($note in [regex]::Matches(
	$dustFallBody,
	'DustNotes\.Add\(\{\s*([0-9.]+)f,\s*([0-9.]+)f,\s*([0-9.]+)f,')) {
	$dustBands += [double]$note.Groups[3].Value
	$dustEndSeconds = [Math]::Max(
		$dustEndSeconds,
		[double]$note.Groups[1].Value + [double]$note.Groups[2].Value)
}
# The individual grains carry their bands in a table instead of inline.
$grainTable = [regex]::Match($dustFallBody, 'GrainBands\[\]\s*=\s*\{([^}]*)\}')
if (-not $grainTable.Success) {
	throw 'The dust-fall grain band table could not be located.'
}
foreach ($band in [regex]::Matches($grainTable.Groups[1].Value, '([0-9.]+)f')) {
	$dustBands += [double]$band.Groups[1].Value
}
if ($dustBands.Count -lt 10) {
	throw "분진 낙하 lost its layers; only $($dustBands.Count) bands were found."
}
$assertions++
# §21.3 fixes the high pass at 6 kHz. Falling grains push the spectral centroid
# up as they get finer, and plaster powder is much finer than sand.
foreach ($band in $dustBands) {
	if ($band -lt 6000.0) {
		throw ('분진 낙하 is high-passed at 6 kHz per §21.3; a lower band is ' +
			"debris or the wall itself, not powder (found $band Hz).")
	}
	$assertions++
}
# §21.3 also fixes the length at 0.90 s.
if ([Math]::Abs($dustEndSeconds - 0.90) -gt 0.001) {
	throw "분진 낙하 must run 0.90 s per §21.3; found $dustEndSeconds s."
}
$assertions++
if ($dustFallBody -match 'EIGToneWaveform::(Sine|Triangle|SoftSquare)') {
	throw 'Falling powder is broadband granular noise, never a pitched oscillator.'
}
$assertions++
Require-All $listener @(
	'constexpr float DustSiftIntervalCentimeters = 260.0f',
	'constexpr float DustSiftVolume = 0.42f',
	'UIGToneSequenceSoundWave::CreatePlasterDustFall(this)',
	'!AudioDirector->IsAuthoredSilence()',
	'EIGAudioBus::Entity'
) '§10.3 dust fall route'

# §21.3 시그니처 SFX: 배관 수류 원근 4단, 밸브 개방, 망치 임팩트. 표에 적힌
# 파라미터가 실제 합성값이어야 문서가 명세로 남는다.
Require-All $tone @(
	'CreatePipeWaterFlow(',
	'const float Bandwidths[] = {5000.0f, 2400.0f, 1100.0f, 480.0f};',
	'CreateValveOpen(',
	'const float RingHz[] = {1800.0f, 1520.0f, 2150.0f};',
	'CreateHammerImpact(',
	'0.000f, 0.180f, 90.0f'
) '§21.3 signature SFX'
# 원근 4단은 루프여야 벽에 붙어 비교할 시간이 생긴다.
$flowBody = [regex]::Match(
	$tone,
	'(?s)UIGToneSequenceSoundWave\* UIGToneSequenceSoundWave::CreatePipeWaterFlow\(.*?\n\}').Value
if ($flowBody -notmatch 'ConfigureNotes\(MoveTemp\(FlowNotes\), true') {
	throw '배관 수류는 루프여야 한다. 원샷이면 벽을 비교할 시간이 없다.'
}
$assertions++
# 망치는 §21.3의 1.4초 건물 반향과 §10.3의 3단 파쇄를 함께 가져야 한다.
$hammerBody = [regex]::Match(
	$tone,
	'(?s)UIGToneSequenceSoundWave\* UIGToneSequenceSoundWave::CreateHammerImpact\(.*?\n\}').Value
foreach ($needle in @('1.400f', 'const int32 FractureCount = 3 + Stage * 3;')) {
	if (-not $hammerBody.Contains($needle)) {
		throw "망치 임팩트에서 $needle 가 사라졌다."
	}
	$assertions++
}

# §7 P3: 판별은 독백이 아니라 벽이 한다. 빈 벽은 실측 mass-air-mass 공명
# 대역에서 길게 울고, 속이 찬 벽은 짧게 죽는다.
Require-All $tone @(
	'CreateWallCavityResponse(',
	'IGWallCavityHollow',
	'IGWallCavitySolid',
	'0.006f, 1.450f, 105.0f'
) '§7 P3 wall discrimination synthesis'
$cavityBody = [regex]::Match(
	$tone,
	'(?s)UIGToneSequenceSoundWave\* UIGToneSequenceSoundWave::CreateWallCavityResponse\(.*?\n\}').Value
if ([string]::IsNullOrWhiteSpace($cavityBody)) {
	throw 'The wall cavity response body could not be located.'
}
$cavityHalves = $cavityBody -split 'if \(bHollow\)', 2
if ($cavityHalves.Count -ne 2) { throw 'The hollow/solid split is missing.' }
$hollowHalf = ($cavityHalves[1] -split 'else', 2)[0]
$solidHalf = ($cavityHalves[1] -split 'else', 2)[1]
$hollowLongest = 0.0
foreach ($note in [regex]::Matches($hollowHalf, 'WallNotes\.Add\(\{\s*[0-9.]+f,\s*([0-9.]+)f,')) {
	$hollowLongest = [Math]::Max($hollowLongest, [double]$note.Groups[1].Value)
}
$solidLongest = 0.0
foreach ($note in [regex]::Matches($solidHalf, 'WallNotes\.Add\(\{\s*[0-9.]+f,\s*([0-9.]+)f,')) {
	$solidLongest = [Math]::Max($solidLongest, [double]$note.Groups[1].Value)
}
if ($hollowLongest -lt $solidLongest * 3.0) {
	throw ('빈 벽은 속이 찬 벽보다 최소 세 배 길게 울어야 한다: ' +
		"hollow=$hollowLongest solid=$solidLongest")
}
$assertions++
# 여기서 사인파를 빼면 공명이 사라지고 다시 노이즈 두 덩이가 된다.
if ($hollowHalf -notmatch 'EIGToneWaveform::Sine') {
	throw '공동 공명은 대역 노이즈가 아니라 실제 공진 모드여야 한다.'
}
$assertions++
Require-All $nightThree @(
	'void AIGMissingFloorNightThreeDirector::PlayWallListenResponse(',
	'UIGToneSequenceSoundWave::CreateWallCavityResponse(this, bHollow)',
	'UIGToneSequenceSoundWave::CreateValveOpen(',
	'constexpr int32 RiserBedDistanceStep = 2',
	'constexpr int32 CavityWallDistanceStep = 0',
	'constexpr int32 SolidWallDistanceStep = 3'
) '§7 P3 wall discrimination route'
# 세 경로(밸브 전, 공동 벽, 나머지 벽) 모두가 실제로 소리를 낸다.
$wallResponseCalls = ([regex]::Matches(
	$nightThree,
	'PlayWallListenResponse\(BayIndex, /\*bHollow=\*/')).Count
if ($wallResponseCalls -ne 3) {
	throw ('세 청음 경로가 모두 소리를 내야 한다. 하나라도 빠지면 그 벽은 ' +
		"독백으로만 대답한다. 현재 $wallResponseCalls 개.")
}
$assertions++
Require-All $nightFour @(
	'UIGToneSequenceSoundWave::CreateHammerImpact(this, FMath::Max(Index - 1, 0))',
	'UIGToneSequenceSoundWave::CreateValveOpen(',
	'UIGToneSequenceSoundWave::CreateVentDuctSpinUp(this)'
) '§21.3 night four impact and controls'

# §11 V1: 빛으로 그의 최근 경로를 읽는다. 밀도는 정확히 두 배까지만 오르고,
# 포획 리셋은 공기까지 04:30으로 되돌린다.
Require-All $dust @(
	'static constexpr float MaxDensityMultiplier = 2.0f;',
	'static constexpr float DisturbanceLifetimeSeconds = 52.0f;',
	'void ReportDisturbance(',
	'float GetDensityMultiplierAt(',
	'void ClearDisturbances();'
) '§11 V1 dust world model'
Require-All $listener @(
	'void AIGListenerEntity::ReportDustTrail()',
	'DustSubsystem->ReportDisturbance(DragHeight, DragStrength)',
	'Dust->ClearDisturbances();'
) '§11 V1 trail reporting'
Require-All $beamDust @(
	'BatchUpdateInstancesTransforms(',
	'Motes->SetUsingAbsoluteLocation(true);',
	'M_FridgeInterior',
	'constexpr float MinRevealingBeamStrength = 0.35f',
	'ActiveMoteCount = BaseMoteCount',
	'TrailAnchors[Index % TrailAnchors.Num()]'
) '§11 V1 beam motes'
# 발광 모트는 어둠 속에서도 보인다. 빔만 리빌한다는 계약이 깨진다.
if ($beamDust -match 'Emissive|SetEmissive|M_(SignWhite|LightPanel|ScreenGlow|WindowGlow)') {
	throw 'Motes must be lit by the beam, never emissive — darkness has to stay dark.'
}
$assertions++
$beamMoteCounts = [regex]::Match(
	(Read-Source 'Source/IndieGame/Player/IGBeamDustComponent.h'),
	'BaseMoteCount = (\d+);(?s).*?TrailMoteCount = (\d+);')
if (-not $beamMoteCounts.Success -or
	[int]$beamMoteCounts.Groups[1].Value -ne [int]$beamMoteCounts.Groups[2].Value) {
	throw '§11 V1 asks for exactly twice the density over his lane, no more.'
}
$assertions++
# 밤 구간 자동노출 하한 잠금: 상한만 묶으면 히스토그램이 암부를 끌어올려
# 손전등이 자원이 아니게 된다.
Require-All $worldScene @(
	'PostProcess->Settings.AutoExposureMinBrightness = bSealed ? 1.30f : -0.5f;',
	'PostProcess->Settings.AutoExposureMaxBrightness = bSealed ? 1.30f : 5.0f;'
) '§11 V1 night exposure lock'

# §5.5 기록되지 않는 시간. 그 시간의 소리는 어떤 기계에도 담기지 않고, 노크가
# 있던 자리에는 정확히 그 길이만큼의 무음이 남는다. 규칙은 밤4 벽 개방으로
# 단 한 번 해제된다.
$recording = Read-Source 'Source/IndieGame/Narrative/IGRecordingSubsystem.cpp'
$recordingHeader = Read-Source 'Source/IndieGame/Narrative/IGRecordingSubsystem.h'
Require-All $recordingHeader @(
	'bool bSuppressed = false;',
	'float DurationSeconds = 0.0f;',
	'bool IsRuleLifted() const;'
) '§5.5 recording log'
Require-All $recording @(
	'Instigator->IsA<AIGListenerEntity>()',
	'Narrative->IsNightFourWallOpened()'
) '§5.5 suppression rule'
# 규칙 해제는 저장본에서 파생돼야 한다. 따로 저장하면 로드 후 어긋난다.
if ($recording -notmatch '(?s)bool UIGRecordingSubsystem::IsRuleLifted\(\).*?IsNightFourWallOpened') {
	throw '규칙 해제는 저장된 벽 상태에서 파생돼야 한다.'
}
$assertions++
# 그녀의 소리는 담긴다. 비트 2-6은 자기 소리만 들리는 것으로 성립한다.
$suppressBody = [regex]::Match(
	$recording,
	'(?s)bool UIGRecordingSubsystem::ShouldSuppress\(.*?\r?\n\}').Value
if ($suppressBody -match 'IsHourSealed|bTheHourSealed') {
	throw '시간대가 아니라 누가 소리를 냈는지로 판정해야 한다(비트 2-6).'
}
$assertions++
# 정규식이 체크아웃 줄끝에 묶이면 안 된다. 문자열 안에 실제 줄바꿈을
# 넣으면 CRLF 클론에서만 매치되고, LF로 받은 클론이나 리눅스 CI에서는
# 추출이 빈 문자열이 되어 내용과 무관하게 계약이 실패한다.
# 무음은 편집이 아니라 부재다. 억압된 이벤트는 노트를 하나도 내지 않는다.
Require-All $tone @(
	'CreateRecordingPlayback(',
	'IGRecordingPlayback',
	'if (Sound.bSuppressed)'
) '§5.5 playback synthesis'
$takeBody = [regex]::Match(
	$tone,
	'(?s)UIGToneSequenceSoundWave\* UIGToneSequenceSoundWave::CreateRecordingPlayback\(.*?\r?\n\}').Value
if ($takeBody -notmatch '(?s)if \(Sound\.bSuppressed\)\s*\{[^}]*continue;') {
	throw '억압된 소리는 노트를 내지 않고 길이만 남겨야 한다.'
}
$assertions++
# 폰 스피커에는 저역이 없다. 노크가 사는 58~80Hz는 애초에 통과하지 못한다.
$takeBands = [regex]::Matches($takeBody, 'TakeNotes\.Add\(\{\s*[^,]+,\s*[^,]+,\s*([0-9.]+)f,')
# 한 음도 못 잡으면 아래 반복문이 한 번도 안 돌고, 이 블록은 아무것도
# 지키지 않은 채 통과한다. 노트 표기가 바뀌는 것만으로 그렇게 된다.
if ($takeBands.Count -lt 3) {
	throw "녹음 재생의 노트를 $($takeBands.Count)개만 읽었다. 대역을 볼 수 없다."
}
$assertions++
foreach ($band in $takeBands) {
	if ([double]$band.Groups[1].Value -lt 400.0) {
		throw "폰 스피커 재생에 저역이 들어갔다: $($band.Groups[1].Value) Hz"
	}
	$assertions++
}
# 밤2가 심고 아침에 재생한다.
$puzzleTwoRec = Read-Source 'Source/IndieGame/Entity/IGMissingFloorPuzzleTwoDirector.cpp'
Require-All $puzzleTwoRec @(
	'Recording->StartRecording();',
	'Recording->StopRecording();',
	'Recording->PlayBack(At)',
	'내 발소리는 들리는데, 문에서 난 소리는 하나도 안 담겼어.'
) '§5.5 night two beats'
Require-All $greybox @(
	'case EProbeStep::RecordingRuleContract:',
	'MISSINGFLOOR_RECORDING PASS'
) '§5.5 runtime probe'

# §24 즉시 차단 19: 봉쇄된 한 시간에는 F9 즉시 로드도 증거 기록 화면도 열리지
# 않는다. 둘 다 구현돼 있었지만 아무것도 잠그지 않고 있었다. 잠금 없는 규칙은
# 다음 사람이 지우면 그만이다. 정적 핀은 프로브가 사라지지 않았는지만 보고,
# 실제 판정은 봉쇄 중에 직접 눌러 보는 런타임 단계가 한다.
Require-All $greybox @(
	'case EProbeStep::SealedHourUiContract:',
	'OpenMissingFloorJournalForTesting()',
	'LoadLatestAutosaveForTesting()',
	'MISSINGFLOOR_SEALEDUI PASS'
) '§24 즉시 차단 19 runtime probe'
# 거부했다는 사실만으로는 부족하다. 아무 일도 일어나지 않는 것은 입력이
# 끊어졌을 때의 모습이기도 하다. 어떤 거부를 했는지까지 읽어야 한다.
if ($greybox -notmatch 'HasDialogueLineForTesting\(\s*[
\s]*TEXT\("지금은 되돌릴 수 없어\."\)') {
	throw '봉쇄 중 F9 거부는 침묵이 아니라 그 문장으로 확인해야 한다.'
}
$assertions++
# 잠금 자체도 남아 있어야 한다.
$playerCharacterBody = Read-Source 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
if ($playerCharacterBody -notmatch
	'Narrative && Narrative->IsHourSealed\(\)[\s\S]{0,400}?MissingFloorLoadLocked') {
	throw '봉쇄된 시간에 최근 자동 저장 불러오기을 막는 가드가 없다.'
}
$assertions++
$playerControllerBody = Read-Source 'Source/IndieGame/Player/IGPlayerController.cpp'
if ($playerControllerBody -notmatch
	'if \(bMissingFloorJournalVisible \|\| IsMissingFloorNight\(\)\)') {
	throw '밤에는 증거 기록 화면이 열리지 않아야 한다.'
}
$assertions++

# §24 즉시 차단 17: 밤 구간에 목표 텍스트가 노출되지 않는다. 그 한 시간 동안
# 플레이어는 아무것도 듣지 못하고 대신 들어야 한다(§11 V4). 잠금은 두 겹이다 —
# 봉쇄 상태가 밤 표시를 켜고, 밤 표시가 목표 줄을 그리지 않는다. 둘 중 하나만
# 끊겨도 목표 줄이 돌아오므로 양쪽을 다 본다.
$hudBody = Read-Source 'Source/IndieGame/Player/IGHorrorHUD.cpp'
if ($hudBody -notmatch
	'if \(bNightPresentation\)[\s\S]{0,320}?EIGHudTextRole::Objective') {
	throw '밤 표시 중에 목표 텍스트가 그려지지 않도록 막는 가드가 없다.'
}
$assertions++
$nightPhaseBody = Read-Source 'Source/IndieGame/Entity/IGNightPhaseDirector.cpp'
if (-not $nightPhaseBody.Contains('Hud->SetNightPresentation(bSealed);')) {
	throw '봉쇄 상태가 밤 표시를 켜지 않으면 목표 줄이 밤에도 나온다.'
}
$assertions++
Require-All $greybox @('night_presented=%d') '§24 즉시 차단 17 runtime probe'

# §24 즉시 차단 22의 나머지 절반: UI가 색으로만 정보를 전달하지 않는다.
# 기본 설정에서 가운데 점은 조사 대상을 물었을 때만 뜬다. 점이 있고 없고가
# 형태 신호다. 항상 표시를 켜면 두 상태의 기하가 같고 색만 다르다. 포커스
# 브래킷이 뒤따르지만 0.18초 늦으므로, 그 사이를 색만으로 버티지 않으려면
# 두 색이 회색조에서도 갈려야 한다. 상대 휘도로 확인한다.
$hudColors = @{}
foreach ($colorName in @('PaleGray', 'RedAccent')) {
	$colorMatch = [regex]::Match(
		$hudBody,
		('const FLinearColor {0}\(\s*(?<r>[\d.]+)f,\s*(?<g>[\d.]+)f,' +
		 '\s*(?<b>[\d.]+)f') -f $colorName)
	if (-not $colorMatch.Success) {
		throw "조준 표시 색을 읽을 수 없다: $colorName"
	}
	# 값은 이미 선형이므로 sRGB 역감마를 다시 적용하지 않는다.
	$hudColors[$colorName] =
		0.2126 * [double]$colorMatch.Groups['r'].Value +
		0.7152 * [double]$colorMatch.Groups['g'].Value +
		0.0722 * [double]$colorMatch.Groups['b'].Value
}
$brighter = [Math]::Max($hudColors['PaleGray'], $hudColors['RedAccent'])
$darker = [Math]::Min($hudColors['PaleGray'], $hudColors['RedAccent'])
$stateContrast = ($brighter + 0.05) / ($darker + 0.05)
if ($stateContrast -lt 3.0) {
	throw ('조준 표시의 두 상태가 회색조에서 갈리지 않는다: 명암비 {0:N2}' -f
		$stateContrast)
}
$assertions++
# 형태 신호가 아예 사라지면 색만 남는다. 브래킷 자체도 지켜야 한다.
Require-All $hudBody @(
	'void AIGHorrorHUD::DrawFocusBracket(',
	'UpdateFocusBracket(bHasFocus && !bFadedOut ? Interaction->GetFocusedActor() : nullptr',
	'DrawCenterDot(bHasFocus)'
) '§24 즉시 차단 22 형태 신호'

# §11 V5 밤 구간 8지점 히스토그램. 임계는 설계서가 고정한 5%/98%이며, 지점별
# 밴드는 실측에서 저작한다. 밴드를 먼저 쓰고 통과할 때까지 늘리면 밴드가
# 느슨하다는 것만 증명된다.
Require-All $greybox @(
	'constexpr float ShadowThreshold = 0.05f;',
	'constexpr float HighlightThreshold = 0.98f;',
	'TEXT("corridor_dark")',
	'TEXT("beam_dust")',
	'TEXT("entity_rim")',
	'TEXT("cavity_wall")',
	'TEXT("residue_fifth_floor")',
	'TEXT("residue_corridor")',
	'TEXT("chase_post")',
	'TEXT("ripple_ring")',
	'MISSINGFLOOR_V5 PASS'
) '§11 V5 histogram points'
# 여덟 지점이어야 한다. 설계서가 여덟을 고정했다.
$v5Points = ([regex]::Matches(
	$greybox,
	'TEXT\("(corridor_dark|beam_dust|entity_rim|cavity_wall|residue_fifth_floor|residue_corridor|chase_post|ripple_ring)"\), ESetup::')).Count
if ($v5Points -ne 8) {
	throw "§11 V5는 여덟 지점이다. 현재 $v5Points 개."
}
$assertions++
# 지점 이름이 담고 있다고 주장하는 것을 프레임이 실제로 담고 있는지는 밴드가
# 증명하지 못한다. 첫 저작에서 residue_fifth_floor는 (60, 760)에서 +X/−Y를 보고
# 있었고 두 바닥 잔흔은 **카메라 뒤**에 있었다. 찍힌 것은 베이 벽과 슬래브 결
# 이었고, 끌림 자국 자재를 올려도 바닥 픽셀은 0.04/255만 움직였다. 검정이
# 만족시켜 버리는 밝기 하한과 같은 종류의 거짓 통과다. 그래서 시점과 잔흔
# 좌표를 같은 소스에서 읽어 화각 안에 있는지 직접 계산한다.
$playerCharacterHeader = Read-Source 'Source/IndieGame/Player/IGPlayerCharacter.h'
$playerCharacterSource = Read-Source 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
$eyeMatch = [regex]::Match(
	$playerCharacterHeader,
	'CameraBaseLocation\s*=\s*FVector\(\s*0\.0f,\s*0\.0f,\s*(?<z>-?[\d.]+)f\s*\)')
if (-not $eyeMatch.Success) {
	throw '플레이어 카메라의 눈높이 오프셋을 읽을 수 없다.'
}
$fovMatch = [regex]::Match(
	$playerCharacterSource, 'SetFieldOfView\(\s*(?<fov>[\d.]+)f\s*\)')
if (-not $fovMatch.Success) {
	throw '플레이어 카메라의 화각을 읽을 수 없다.'
}
$histogramRunner = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts/Run-MissingFloor-NightHistogram.ps1')
$resXMatch = [regex]::Match($histogramRunner, '\[int\]\$ResX\s*=\s*(?<v>\d+)')
$resYMatch = [regex]::Match($histogramRunner, '\[int\]\$ResY\s*=\s*(?<v>\d+)')
if (-not $resXMatch.Success -or -not $resYMatch.Success) {
	throw '스윕 해상도를 읽을 수 없다. 상하 화각을 계산할 수 없다.'
}
$halfHorizontal = [double]$fovMatch.Groups['fov'].Value / 2.0
$aspect = [double]$resXMatch.Groups['v'].Value / [double]$resYMatch.Groups['v'].Value
$halfVertical = [Math]::Atan(
	[Math]::Tan($halfHorizontal * [Math]::PI / 180.0) / $aspect) * 180.0 / [Math]::PI
# M_MissingFloorCavityScratches는 여기에 없다. 그 면은 바닥 공동 안, 베이 B의
# 석고판 뒤에 있고 밤4에 판이 떨어진 뒤에만 보인다 — 어떤 밤 지점도 그것을
# 담아서는 안 된다. 담기면 폭로가 미리 새는 것이다.
$residueVantages = @(
	@{ Point = 'residue_fifth_floor'; Setup = 'ResidueFifthFloor';
		Materials = @('M_MissingFloorDragTrails', 'M_MissingFloorDustJoint') },
	@{ Point = 'cavity_wall'; Setup = 'CavityWall';
		Materials = @('M_AnnexPressure') }
)
foreach ($vantage in $residueVantages) {
	# 패턴은 먼저 변수로 만든다. 메서드 인자 목록 안에서 쉼표는 배열이 아니라
	# 인자 구분자이므로, 여기서 -f를 직접 쓰면 두 번째 서식 인자가 Match()의
	# 세 번째 인자로 넘어가고 {1}은 채워지지 않는다.
	$vantagePattern = ('TEXT\("{0}"\), ESetup::{1},\s*' +
		'FVector\(\s*(?<x>-?[\d.]+)f,\s*(?<y>-?[\d.]+)f,\s*(?<z>-?[\d.]+)f\s*\),\s*' +
		'(?<yaw>-?[\d.]+)f,\s*(?<pitch>-?[\d.]+)f') -f $vantage.Point, $vantage.Setup
	$vantageMatch = [regex]::Match($greybox, $vantagePattern)
	if (-not $vantageMatch.Success) {
		throw ('{0} 시점의 위치와 방향을 읽을 수 없다.' -f $vantage.Point)
	}
	# 폰 위치가 아니라 눈 위치에서 재야 한다. 둘은 64 cm 차이고, 아래로 38°
	# 기운 시점에서 그 64 cm는 프레임 중심이 바닥에 닿는 거리를 124 cm에서
	# 206 cm로 옮긴다 — 처음 화각 계산이 어긋난 지점이 정확히 여기였다.
	$vantageX = [double]$vantageMatch.Groups['x'].Value
	$vantageY = [double]$vantageMatch.Groups['y'].Value
	$vantageEyeZ = [double]$vantageMatch.Groups['z'].Value +
		[double]$eyeMatch.Groups['z'].Value
	$vantageYaw = [double]$vantageMatch.Groups['yaw'].Value
	$vantagePitch = [double]$vantageMatch.Groups['pitch'].Value
	foreach ($residueMaterial in $vantage.Materials) {
		$placementPattern = ('(?:AddResidue|CreateBlock)\(\s*FVector\(\s*(?<x>-?[\d.]+)f,\s*' +
			'(?<y>-?[\d.]+)f,\s*(?<z>-?[\d.]+)f\s*\),\s*FVector\([^)]*\),\s*' +
			'(?:TexMat\()?TEXT\("{0}"\)') -f $residueMaterial
		$placement = [regex]::Match($worldScene, $placementPattern)
		if (-not $placement.Success) {
			throw "5층 잔흔의 배치를 읽을 수 없다: $residueMaterial"
		}
		$toResidueX = [double]$placement.Groups['x'].Value - $vantageX
		$toResidueY = [double]$placement.Groups['y'].Value - $vantageY
		$toResidueZ = [double]$placement.Groups['z'].Value - $vantageEyeZ
		$groundDistance = [Math]::Sqrt(
			$toResidueX * $toResidueX + $toResidueY * $toResidueY)
		$bearing = [Math]::Atan2($toResidueY, $toResidueX) * 180.0 / [Math]::PI
		$lateral = $bearing - $vantageYaw
		while ($lateral -gt 180.0) { $lateral -= 360.0 }
		while ($lateral -lt -180.0) { $lateral += 360.0 }
		$lateral = [Math]::Abs($lateral)
		$elevation = [Math]::Atan2($toResidueZ, $groundDistance) * 180.0 / [Math]::PI
		$vertical = [Math]::Abs($elevation - $vantagePitch)
		if ($lateral -ge $halfHorizontal) {
			throw ('{0} 시점이 {1}을 좌우 화각 밖에 둔다: {2:N1}° >= {3:N1}°' `
				-f $vantage.Point, $residueMaterial, $lateral, $halfHorizontal)
		}
		if ($vertical -ge $halfVertical) {
			throw ('{0} 시점이 {1}을 상하 화각 밖에 둔다: {2:N1}° >= {3:N1}°' `
				-f $vantage.Point, $residueMaterial, $vertical, $halfVertical)
		}
		$assertions++
	}
}
# 뷰포트 백버퍼 직접 읽기는 -RenderOffScreen에서 순수 검정을 돌려준다.
# 스크린샷 델리게이트가 실제 렌더된 프레임을 주는 유일한 경로다.
Require-All $greybox @(
	'UGameViewportClient::OnScreenshotCaptured().AddUObject(',
	'void AIGListenerGreyboxDirector::HandleHistogramScreenshot('
) '§11 V5 measurement path'
if ($greybox -match 'Viewport->ReadPixels\(') {
	throw '백버퍼 직접 읽기는 오프스크린에서 검정을 돌려준다. 델리게이트를 쓴다.'
}
$assertions++
# 전 프레임 검정은 어두운 프레임이 아니라 깨진 읽기다. 모든 암부 하한을
# 잘못된 이유로 만족시키므로 반드시 거부해야 한다.
if ($greybox -notmatch 'Shadow >= 0\.9995f') {
	throw '전 픽셀 검정 프레임을 거부하지 않으면 거짓 통과가 가능하다.'
}
$assertions++
# 프레임이 오지 않으면 매달리지 말고 진단을 낸다.
Require-All $greybox @(
	'constexpr float ShotTimeoutSeconds = 20.0f;',
	'no frame arrived for point'
) '§11 V5 watchdog'
# 프레임은 스윕이 직접 쓴다. UGameViewportClient는 OnScreenshotCaptured에
# **아무것도 바인딩돼 있지 않을 때만** PNG를 저장한다 — 엔진 주석이 「델리게이트가
# 구독돼 있으면 파일 대신 델리게이트를 쏜다」다. 이 스윕은 픽셀을 재려고 반드시
# 바인딩하므로, 파일 이름을 넘기는 요청은 조용히 아무 일도 하지 않는다. 그렇게
# 여덟 프레임이 예전 빌드의 것으로 디스크에 남아 있었다.
Require-All $greybox @(
	'void AIGListenerGreyboxDirector::WriteHistogramFrame(',
	'FImageUtils::SaveImageByExtension(',
	'const FImageView Frame(Colors.GetData(), Width, Height);',
	'FScreenshotRequest::RequestScreenshot(/*bInShowUI=*/true);',
	'MISSINGFLOOR_V5 frame:'
) '§11 V5 frames are written by the sweep'
# 측정한 그 비트맵으로 써야 숫자와 그림이 어긋날 수 없다. 판정 전에 쓴다 —
# 밴드를 벗어난 지점이야말로 누군가 보고 싶어 하는 프레임이다.
if ($greybox -notmatch 'WriteHistogramFrame\(Point\.Name, Width, Height, Colors\);[\s\S]{0,220}int32 ShadowPixels') {
	throw '프레임은 측정한 비트맵으로, 판정보다 먼저 기록해야 한다.'
}
$assertions++
# 파일 이름을 넘기는 요청은 델리게이트가 바인딩된 동안 아무 일도 하지 않는다.
if ($greybox -match 'CaptureShot\(\*FString::Printf\(TEXT\("v5-') {
	throw 'v5 프레임을 CaptureShot으로 요청하면 델리게이트가 파일 기록을 대체한다.'
}
$assertions++

# 저작 패스는 판정하지 않으므로 PASS를 주장해서도 안 된다.
if ($greybox -notmatch 'MISSINGFLOOR_V5 REPORT') {
	throw '보고 전용 패스는 PASS가 아니라 REPORT를 남겨야 한다.'
}
$assertions++
# 러너는 창을 띄울 수 있는 인자 조합을 실행 자체로 거부한다.
$histogramRunner = Read-Source 'Scripts/Run-MissingFloor-NightHistogram.ps1'
Require-All $histogramRunner @(
	"'-RenderOffScreen',",
	"if (`$arguments -notcontains '-RenderOffScreen') {",
	'-d3d12'
) '§11 V5 no-window guard'
if ($histogramRunner -match "'-nullrhi'") {
	throw '히스토그램 스윕은 -nullrhi로 돌 수 없다. 렌더가 없으면 측정도 없다.'
}
$assertions++

# §11 V2 오염 레이어. 수광 평면은 0.15cm만 띄우고 그림자·충돌을 끈다. 조명색과
# 접촉 그림자는 월드가 계산하므로 흔적만 주변 재질에 젖어든다.
$settledDust = Read-Source 'Source/IndieGame/Environment/IGSettledDustComponent.cpp'
$settledDustHeader = Read-Source 'Source/IndieGame/Environment/IGSettledDustComponent.h'
Require-All $settledDustHeader @(
	'static constexpr float SurfaceOffset = 0.15f;',
	'void ConfigureField('
) '§11 V2 settled dust contract'
Require-All $dust @(
	'enum class EIGDustPrintKind : uint8',
	'void ReportSettledPrint(',
	'void ClearSettledPrints();',
	'static constexpr float PrintMergeDistance = 26.0f;'
) '§11 V2 settled dust world model'
Require-All $settledDust @(
	'M_MissingFloorHandprints',
	'M_MissingFloorDragTrails',
	'Layer->SetCastShadow(false);',
	'Layer->SetReceivesDecals(false);',
	'Layer->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);',
	'Layer->bAffectDistanceFieldLighting = false;',
	'Layer->SetUsingAbsoluteLocation(true);',
	'Owner->AddInstanceComponent(Layer);',
	'Layer->RegisterComponent();',
	'FieldFloorZ + SurfaceOffset'
) '§11 V2 settled dust blending'
# 발자국이 남는 마스크: 두 종류의 마크가 서로 다른 크기여야 신발과 끌림이
# 구분된다. 같으면 바닥에 같은 도형만 깔린다.
if ($settledDustHeader -notmatch 'FootfallLengthCentimeters = 27\.0f' -or
	$settledDustHeader -notmatch 'DragLengthCentimeters = 62\.0f') {
	throw '신발 자국과 끌림 자국은 크기가 달라야 구분된다.'
}
$assertions++
# 흰 사각 그림으로 붙이지 않는다: 발광이나 불투명 기본 재질이면 계약 위반이다.
if ($settledDust -match 'M_(SignWhite|LightPanel|ScreenGlow|PaperClean)') {
	throw '오염 마크는 저자 잔여물 마스크만 쓴다(§11 V2).'
}
$assertions++
# 리포터는 먼지가 어디 있는지 몰라도 된다. 필드가 자기 범위로 걸러낸다.
Require-All $listener @(
	'EIGDustPrintKind::Drag'
) '§11 V2 entity drag mark'
$playerPrints = Read-Source 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
Require-All $playerPrints @(
	'EIGDustPrintKind::Footfall',
	'FootPrintDropCentimeters'
) '§11 V2 player footfall mark'
# 포획 리셋은 공기와 바닥을 함께 04:30으로 되돌린다.
if ($listener -notmatch 'Dust->ClearSettledPrints\(\);') {
	throw '포획 리셋은 바닥의 흔적도 지워야 한다(§5.4).'
}
$assertions++

# §11 V2 403호 3단계 노화: 프롤로그는 깨끗하고, 누적이며, 밤으로 구동된다.
Require-All $worldScene @(
	'void AIGPrologueWorldScene::SetUnit403AgeStage(',
	'void AIGPrologueWorldScene::ApplyUnit403AgeStage()',
	'Unit403AgeStageOne',
	'Unit403AgeStageTwo',
	'M_MissingFloorCavityScratches',
	'M_DecalDampWallpaper'
) '§11 V2 403 aging'
Require-All $nightPhaseScene @(
	'WorldScene->SetUnit403AgeStage('
) '§11 V2 aging wiring'
# 누적이 아니라 배타면 밤4에서 균열이 사라진다.
if ($worldScene -notmatch 'Plane->SetHiddenInGame\(Unit403AgeStage < 1\);' -or
	$worldScene -notmatch 'Plane->SetHiddenInGame\(Unit403AgeStage < 2\);') {
	throw '403호 노화는 누적이어야 한다. 손상은 이사 가지 않는다.'
}
$assertions++
# 4F 복도의 끌린 자국. 분전반 아래로 떠 있던 녹 합성판은 제거했다.
Require-All $worldScene @(
	'§11 V2 끌린 자국 (복도 러너)',
	'M_MissingFloorDragTrails'
) '§11 V2 corridor runner'

# §21.3 프로타주: 밴드 노이즈 900~4200Hz, 지속. 소음 0.25를 내는 유일한 지속
# 상호작용이므로 유일한 지속 큐를 갖는다. 들리지 않는 비용은 선택할 수 없다.
# 맨 float은 이 파일에 스물네 번 나온다. 밴드 끝이 어느 음에 붙는지
# 까지 적힌 줄을 본다.
Require-All $tone @(
	'CreateFrottageRub(',
	'IGFrottageRub',
	'RubNotes.Add({Start, 0.300f, 900.0f,',
	'RubNotes.Add({Start + 0.045f, 0.270f, 4200.0f,'
) '§21.3 frottage synthesis'
$rubBody = [regex]::Match(
	$tone,
	'(?s)UIGToneSequenceSoundWave\* UIGToneSequenceSoundWave::CreateFrottageRub\(.*?\n\}').Value
if ($rubBody -notmatch 'ConfigureNotes\(MoveTemp\(RubNotes\), true') {
	throw '프로타주는 홀드 내내 이어져야 하므로 루프다.'
}
$assertions++
$rubBands = [regex]::Matches($rubBody, 'RubNotes\.Add\(\{\s*[^,]+,\s*[0-9.]+f,\s*([0-9.]+)f,')
if ($rubBands.Count -lt 2) {
	throw "프로타주의 노트를 $($rubBands.Count)개만 읽었다. 대역을 볼 수 없다."
}
$assertions++
foreach ($band in $rubBands) {
	$value = [double]$band.Groups[1].Value
	if ($value -lt 900.0 -or $value -gt 4200.0) {
		throw "프로타주 대역은 900~4200Hz 안에 있어야 한다: $value Hz"
	}
	$assertions++
}
if ($rubBody -match 'EIGToneWaveform::(Sine|Triangle|SoftSquare)') {
	throw '흑연이 종이를 긁는 소리에는 음정이 없다.'
}
$assertions++
$evidence = Read-Source 'Source/IndieGame/Entity/IGMissingFloorEvidence.cpp'
$puzzleTwo = Read-Source 'Source/IndieGame/Entity/IGMissingFloorPuzzleTwoDirector.cpp'
Require-All $evidence @(
	'void AIGMissingFloorEvidence::SetSustainedRubCue(',
	'UIGToneSequenceSoundWave::CreateFrottageRub(this)',
	'Context.HoldProgress',
	'RubCueComponent->FadeOut('
) '§21.3 frottage route'
Require-All $puzzleTwo @(
	'CarbonLedger->SetSustainedRubCue(true);'
) '§21.3 frottage wiring'
# 손을 떼면 연필도 멈춘다. 놓아도 계속 울리면 남은 비용을 피할 수 없다.
if ($evidence -notmatch '(?s)EndInteraction_Implementation\(.*?StopRubCue\(\);') {
	throw '홀드를 놓으면 문지름이 멈춰야 한다.'
}
$assertions++

# §21.3 심박 소음화: 기존 심박 + 220Hz 로우패스, −6dB, 박동 동기.
Require-All $tone @(
	'CreateAudibleHeartbeat(',
	'IGAudibleHeartbeat'
) '§21.3 audible heartbeat synthesis'
$audibleBody = [regex]::Match(
	$tone,
	'(?s)UIGToneSequenceSoundWave\* UIGToneSequenceSoundWave::CreateAudibleHeartbeat\(.*?\n\}').Value
if ($audibleBody -notmatch '\* 0\.5f') {
	throw '심박 소음화는 −6dB, 즉 진폭 절반이어야 한다.'
}
$assertions++
# 220Hz 로우패스는 가산 합성에서 상위 부분음 제거로 실현된다. 88Hz가 남아
# 있으면 또렷한 심박이 그대로 방에 나가 전이가 들리지 않는다.
$audiblePartials = [regex]::Matches($audibleBody, 'Beat\.Add\(\{\s*[0-9.]+f,\s*[0-9.]+f,\s*([0-9.]+)f,')
if ($audiblePartials.Count -lt 2) {
	throw "심박 소음화의 부분음을 $($audiblePartials.Count)개만 읽었다. 상한을 볼 수 없다."
}
$assertions++
foreach ($partial in $audiblePartials) {
	if ([double]$partial.Groups[1].Value -gt 220.0) {
		throw ('심박 소음화는 220Hz 위를 남기지 않는다: ' +
			"$($partial.Groups[1].Value) Hz")
	}
	$assertions++
}
Require-All $stress @(
	'UIGToneSequenceSoundWave::CreateAudibleHeartbeat(',
	'Reported.Loudness > 0.0f',
	'EIGAudioBus::Player'
) '§21.3 audible heartbeat route'
# 험이 삼킨 심박이 들리면 엄폐가 통하지 않는다고 가르치게 된다.
if ($stress -notmatch '(?s)const FIGNoiseEvent Reported = Noise->ReportNoise\(.*?if \(Reported\.Loudness > 0\.0f\)') {
	throw '마스킹을 통과한 심박만 들려야 한다.'
}
$assertions++

# §10.3 끌림 2종: 콘크리트와 장판. 같은 소리면 어느 바닥인지 알 수 없다.
Require-All $tone @(
	'CreateEntityDragLoop(',
	'IGEntityDragLoopVinyl',
	'if (bVinyl)'
) '§10.3 drag variants'
$dragBody = [regex]::Match(
	$tone,
	'(?s)UIGToneSequenceSoundWave\* UIGToneSequenceSoundWave::CreateEntityDragLoop\(.*?\n\}').Value
$dragHalves = $dragBody -split 'if \(bVinyl\)', 2
if ($dragHalves.Count -ne 2) { throw '끌림 두 종의 분기를 찾을 수 없다.' }
$vinylHalf = ($dragHalves[1] -split '\telse\b', 2)[0]
$concreteHalf = ($dragHalves[1] -split '\telse\b', 2)[1]
$vinylLowest = 99999.0
foreach ($n in [regex]::Matches($vinylHalf, 'DragNotes\.Add\(\{\s*[0-9.]+f,\s*[0-9.]+f,\s*([0-9.]+)f,')) {
	$vinylLowest = [Math]::Min($vinylLowest, [double]$n.Groups[1].Value)
}
$concreteLowest = 99999.0
foreach ($n in [regex]::Matches($concreteHalf, 'DragNotes\.Add\(\{\s*[0-9.]+f,\s*[0-9.]+f,\s*([0-9.]+)f,')) {
	$concreteLowest = [Math]::Min($concreteLowest, [double]$n.Groups[1].Value)
}
# 장판은 슬래브 위 얇은 플라스틱이라 저역 럼블을 잃는다.
if ($vinylLowest -le $concreteLowest) {
	throw ('장판 끌림은 콘크리트보다 저역이 얕아야 한다: ' +
		"vinyl=$vinylLowest concrete=$concreteLowest")
}
$assertions++
Require-All $listener @(
	'void AIGListenerEntity::RefreshDragSurface()',
	'const FName VinylSurfaceTag(TEXT("Footstep.Vinyl"));',
	'UIGToneSequenceSoundWave::CreateEntityDragLoop(this, bVinyl)'
) '§10.3 drag surface route'
# 발소리 매트릭스와 같은 태그를 써야 두 시스템이 어긋나지 않는다.
$playerCharacter = Read-Source 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
if ($playerCharacter -notmatch 'VinylSurfaceTag\(TEXT\("Footstep\.Vinyl"\)\)') {
	throw '끌림과 발소리가 같은 표면 태그를 공유해야 한다.'
}
$assertions++

Write-Host (
	"MISSINGFLOOR_AUDIO_CONTRACT PASS assertions=$assertions " +
	'reverb_presets=2 dust_density_max=2.0') `
	-ForegroundColor Green
