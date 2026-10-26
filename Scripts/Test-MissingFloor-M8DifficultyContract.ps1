[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Read-Source([string]$RelativePath) {
	Get-Content -Raw -Encoding UTF8 -LiteralPath (
		Join-Path $projectRoot $RelativePath)
}

$tuningHeader = Read-Source 'Source/IndieGame/Entity/IGListenerTuning.h'
$tuning = Read-Source 'Source/IndieGame/Entity/IGListenerTuning.cpp'
$noiseHeader = Read-Source 'Source/IndieGame/Entity/IGNoiseSubsystem.h'
$noise = Read-Source 'Source/IndieGame/Entity/IGNoiseSubsystem.cpp'
$listener = Read-Source 'Source/IndieGame/Entity/IGListenerEntity.cpp'
$narrative = Read-Source 'Source/IndieGame/Narrative/IGMissingFloorNarrativeSubsystem.cpp'
$nightFour = Read-Source 'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.cpp'
$nightPhase = Read-Source 'Source/IndieGame/Entity/IGNightPhaseDirector.cpp'
$greybox = Read-Source 'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'
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

# §20.2 튜닝 테이블. 네 밤의 일곱 파라미터가 문서의 초기값과 같아야 한다.
Require-All $tuning @(
	'constexpr float NightHearingRadius[] = {900.0f, 1100.0f, 1100.0f, 1300.0f};',
	'constexpr float NightListenWindow[] = {8.0f, 8.0f, 7.0f, 6.0f};',
	'constexpr int32 NightPatrolNodes[] = {6, 11, 14, 18};',
	'constexpr float NightChaseMultiplier[] = {3.0f, 4.2f, 4.2f, 4.6f};',
	'constexpr float NightInvestigateHold[] = {6.0f, 6.0f, 5.0f, 5.0f};',
	'constexpr float NightHeatmapWeight[] = {0.0f, 0.3f, 0.5f, 0.7f};',
	'constexpr bool NightAmbushAllowed[] = {false, false, true, true};'
) '§20.2 tuning table'

# 티어는 밤 값에 곱해지는 별도 축이다. 대체하면 밤별 곡선이 사라진다.
Require-All $tuning @(
	'constexpr float TierListenScale[] = {1.0f, 0.75f, 0.625f, 0.5f};',
	'NightListenWindow[Night] * TierListenScale[Tier]'
) '§20.2 tier axis'

# §20.4 네 모드. 이름도 세계의 언어여야 하므로 라벨까지 검사한다.
Require-All $tuningHeader @(
	'Quiet,',
	'Standard,',
	'Hasty,',
	'ListenOnly,'
) '§20.4 difficulty modes'
Require-All $tuning @(
	'"쉬움"',
	'"보통"',
	'"어려움"',
	'"추격 없음"'
) '§20.4 mode labels'
Require-All $tuning @(
	'Tuning.HearingSensitivity *= 0.75f;',
	'Tuning.ChaseSpeed *= 0.8f;',
	'Tuning.WaitScale = 1.5f;',
	'Tuning.HeatmapWeight = FMath::Min(1.0f, Tuning.HeatmapWeight + 0.2f);',
	'Tuning.bChaseEnabled = false;',
	'Tuning.bCaptureEnabled = false;'
) '§20.4 modifiers'
# 난이도는 저장본이 아니라 사용자 설정에 남는다. 저장본이 잠그면
# "언제든 변경할 수 있다"가 깨진다.
if ($tuning -notmatch 'GGameUserSettingsIni') {
	throw '난이도는 GameUserSettings.ini에 남아야 한다.'
}
$assertions++
if ($tuning -match 'Snapshot\.') {
	throw '난이도가 진행 세이브에 들어가면 모드 변경이 저장본을 잠근다.'
}
$assertions++
# 명령행 오버라이드는 저장값을 덮어쓰지 않는다.
$overrideBlock = [regex]::Match(
	$tuning,
	'(?s)EIGNightDifficulty ResolveActiveDifficulty\(\).*?\n\t\}').Value
if ($overrideBlock -match 'SavePersistedDifficulty') {
	throw '명령행 오버라이드가 저장값을 덮어써서는 안 된다.'
}
$assertions++

# 밸런스 기준은 항상 기본이며, 마이크 모드는 난이도가 아니다(§5.7).
if ($tuning -match 'Microphone|bMicrophone') {
	throw '마이크 모드는 난이도 축이 아니다(§20.4).'
}
$assertions++

# §5.6 적응 청각. 생성형이 아니라 순수 통계이며 재현 가능해야 한다.
Require-All $noiseHeader @(
	'float GetHeatAt(',
	'bool GetHottestZone(',
	'void DecayHeatmapForNewNight();',
	'void ResetHeatmap();',
	'static constexpr float HeatZoneSize = 400.0f;',
	'static constexpr float HeatNightDecay = 0.5f;'
) '§5.6 heatmap API'
# 배열이어야 순회 순서가 고정된다. TMap 해시 순서는 결정적이지 않으므로
# 같은 플레이가 다른 매복 지점을 만들 수 있다.
if ($noiseHeader -match 'TMap<FIntVector') {
	throw '히트맵은 해시 순서에 의존하면 재현 가능하지 않다.'
}
$assertions++
Require-All $noise @(
	'AccumulateHeat(Location, Effective);',
	'Zone.Heat = FMath::Min(Zone.Heat + Loudness, HeatSaturation);',
	'Zone.Heat *= HeatNightDecay;',
	'Zone.Heat > Best->Heat'
) '§5.6 heatmap statistics'
# 마스킹에 삼켜진 소리는 애초에 나지 않았으므로 열도 남기지 않는다.
$reportBody = [regex]::Match(
	$noise,
	'(?s)FIGNoiseEvent UIGNoiseSubsystem::ReportNoise\(.*?\n\}').Value
if ($reportBody.IndexOf('AccumulateHeat') -lt $reportBody.IndexOf('Event.Loudness = Effective;')) {
	throw '히트맵은 마스킹을 통과한 소리만 기억해야 한다.'
}
$assertions++

# 존재는 해결된 숫자만 읽는다.
Require-All $listener @(
	'void AIGListenerEntity::RefreshNightTuning()',
	'ChaseSpeed = Tuning.ChaseSpeed;',
	'Tuning.HearingSensitivity',
	'Tuning.WaitScale',
	'bSecondSound && Tuning.bChaseEnabled',
	'Distance <= CaptureRadius && bReachable && Tuning.bCaptureEnabled',
	'void AIGListenerEntity::AdvancePatrolIndex()',
	'bool AIGListenerEntity::TryBeginAmbush()',
	'NoiseSubsystem->DecayHeatmapForNewNight();'
) '§20.2/§5.6 entity integration'
# 밤1의 히트맵 가중은 0이다. 첫 밤의 순찰은 배울 수 있는 순서여야 한다.
if ($listener -notmatch 'Tuning\.HeatmapWeight <= 0\.0f') {
	throw '히트맵 가중이 0인 밤에는 통계를 보지 않아야 한다.'
}
$assertions++
# 매복은 티어3에서만, 그리고 습관이 실제로 있을 때만 성립한다.
$ambushBody = [regex]::Match(
	$listener,
	'(?s)bool AIGListenerEntity::TryBeginAmbush\(\).*?\n\}').Value
foreach ($needle in @('AggressionTier < 3', 'Heat < 0.5f', 'EIGListenerState::Investigating')) {
	if (-not $ambushBody.Contains($needle)) {
		throw "티어3 매복에서 $needle 가 사라졌다."
	}
	$assertions++
}

# §20.2: 밤이 끝나면 티어는 1로 하강한다. 초기화가 아니라 하강이다.
Require-All $narrative @(
	'FMath::Min(Snapshot.Night.AggressionTier, 1)'
) '§20.2 tier descent'

# §20.4: 듣기만 하는 밤에도 엔딩 A·B·C가 모두 도달 가능해야 한다(§20.5).
Require-All $nightFour @(
	'bool AIGMissingFloorNightFourDirector::ResolveDawnFailureEnding()',
	'Narrative->IsNightFourWallOpened()',
	'CommitFailureEnding(/*bRecordCapture=*/false)',
	'CommitFailureEnding(/*bRecordCapture=*/true)'
) '§20.4 ending C substitution'
Require-All $nightPhase @(
	'ResolveDawnFailureEnding()'
) '§20.4 dawn route wiring'
# 새벽 경로는 포획이 아니다. 포획으로 기록하면 없는 포획이 티어를 올린다.
$dawnBody = [regex]::Match(
	$nightFour,
	'(?s)bool AIGMissingFloorNightFourDirector::ResolveDawnFailureEnding\(\).*?\n\}').Value
# 호출만 본다. 인자 이름 bRecordCapture는 여기서 걸려서는 안 된다.
if ($dawnBody -match '->RecordCapture\(') {
	throw '새벽 경로가 포획을 기록하면 포획 없는 모드의 티어가 올라간다.'
}
$assertions++

# §20.3 좌절 방지 안전망 두 개. 어느 것도 정답을 말하지 않는다. 막힌
# 플레이어에게 주는 것은 답이 아니라 볼 곳이다.
$mercy = Read-Source 'Source/IndieGame/Entity/IGMissingFloorMercyDirector.cpp'
$mercyHeader = Read-Source 'Source/IndieGame/Entity/IGMissingFloorMercyDirector.h'
Require-All $mercyHeader @(
	'static constexpr float StuckResponseSeconds = 90.0f;',
	'static constexpr int32 ResetsForEnvironmentHint = 2;',
	'PipeCry',
	'EarToWall',
	'NoteUnderDoor'
) '§20.3 safety net thresholds'
Require-All $mercy @(
	'void AIGMissingFloorMercyDirector::NotifyCaptureReset()',
	'ResetsSinceNewSource < ResetsForEnvironmentHint',
	'StuckSeconds < StuckResponseSeconds',
	'Narrative->GetTotalSourceCount()',
	'EntityActor->BeginObservationHold(Observation)',
	'UIGToneSequenceSoundWave::CreatePipeWaterFlow('
) '§20.3 safety net behaviour'
# 진전은 걸은 거리가 아니라 새 출처다. 다른 기준을 쓰면 두 층을 헤매고도
# 막히지 않은 것으로 판정된다.
if ($mercy -notmatch 'SourceCount != LastSourceCount') {
	throw '안전망은 새 출처를 기준으로 삼아야 한다(§20.3).'
}
$assertions++
# 새 출처가 생기면 두 그물 모두 물러선다.
if ($mercy -notmatch '(?s)SourceCount != LastSourceCount.*?ResetsSinceNewSource = 0;') {
	throw '새 출처는 리셋 카운터까지 물러세워야 한다.'
}
$assertions++
# 노트나 설정 중은 막힌 것이 아니다. 압박 시계와 같은 규칙이다.
if ($mercy -notmatch 'World->IsPaused\(\)') {
	throw '일시정지 중에는 90초 시계가 멈춰야 한다(§19.7).'
}
$assertions++
# 같은 넛지를 연달아 두 번 쓰면 플레이어가 무시하도록 학습된다. 세 응답을
# 라운드 로빈으로 돌리고, 조건이 안 되는 응답은 건너뛴다.
Require-All $mercy @(
	'static const EIGMercyResponse Order[] =',
	'StartIndex = (Index + 1) % OrderCount;',
	'bool AIGMissingFloorMercyDirector::TryNoteUnderDoor()'
) '§20.3 response rotation'
# 문 아래 메모는 밤에 한 번뿐이다. 이웃이지 힌트 자판기가 아니다.
if ($mercy -notmatch 'if \(bNoteDelivered \|\| bNoteSliding') {
	throw '문 아래 메모는 밤에 한 번만 밀려야 한다.'
}
$assertions++
# 같은 수첩에서 찢은 종이라 형태와 질감은 공유하되, 인쇄된 문장은 각자의 것이다.
# 다섯 번째 포획 메모의 재질을 그대로 쓰면 그 비트의 문장이 반복되고 무게가
# 소모된다. 메시 이름이 부분 문자열로 걸리지 않도록 재질 경로만 본다.
if ($mercy -match 'Materials/M_CaptureMercyNote') {
	throw '90초 메모는 다섯 번째 포획 메모의 인쇄 재질을 쓰지 않는다.'
}
$assertions++
Require-All $mercy @(
	'M_MercyNoteUnderDoor',
	'SM_CaptureMercyNote'
) '§20.3 note material and shape'
# 문장은 퍼즐이 아니라 그녀에게로 보낸다. 정답 어휘가 들어오면 계약이 막는다.
$signTextures = Read-Source 'Scripts/Create-SignTextures.ps1'
Require-All $signTextures @(
	"'T_MercyNoteUnderDoor_D.png'",
	"'낮에 와.'",
	"'문 열어 둘게.'",
	'$captureMercyNotePaper'
) '§20.3 note lettering'
# 두 메모가 같은 문장을 쓰면 안 된다.
if ($signTextures -notmatch "'소리를 줄여라\.'") {
	throw '다섯 번째 포획 메모의 문장이 사라졌다.'
}
$assertions++
$underDoorBlock = [regex]::Match(
	$signTextures,
	"(?s)-FileName 'T_MercyNoteUnderDoor_D\.png'.*?
    \}").Value
foreach ($forbidden in @('소리를 줄여라', '걔는 눈이 없어', '5층', '벽', '밸브')) {
	if ($underDoorBlock.Contains($forbidden)) {
		throw "메모는 답을 말하지 않고 반복하지 않는다. 금지 어휘: $forbidden"
	}
	$assertions++
}
# 다섯 번째 포획 메모와 같은 규율: 상호작용·윤곽선·그림자·데칼 없음.
Require-All $mercy @(
	'Note->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);',
	'Note->SetCastShadow(false);',
	'Note->SetReceivesDecals(false);'
) '§20.3 note blending discipline'
# 정답을 말하지 않는다: 목표 표시나 힌트 문구, 단계 해금이 없어야 한다.
foreach ($forbidden in @('PushThought', 'PushDialogue', 'SetObjective', 'MarkPuzzleSolved', 'RequestHint')) {
	if ($mercy.Contains($forbidden)) {
		throw "안전망은 답을 말하지 않는다. 금지된 호출: $forbidden"
	}
	$assertions++
}
# 도움을 받는 플레이어가 그 때문에 벌받아서는 안 된다.
$holdBody = [regex]::Match(
	$listener,
	'(?s)void AIGListenerEntity::BeginObservationHold\(.*?\n\}').Value
if ($holdBody -notmatch 'bReactingToSound = false;') {
	throw '관찰 연출이 추격으로 번지면 안 된다.'
}
$assertions++
# 다섯 번째 포획 메모는 세 번째 그물이고 별개 비트다. 서로 대신하지 않는다.
$nightLoop = Read-Source 'Source/IndieGame/Entity/IGNightLoopDirector.cpp'
Require-All $nightLoop @(
	'QueueMercyNoteReveal();',
	'It->NotifyCaptureReset();'
) '§20.3 reset wiring'

# 런타임 프로브가 표를 실제로 해결해 대조한다.
Require-All $greybox @(
	'case EProbeStep::DifficultyContract:',
	'MISSINGFLOOR_DIFFICULTY PASS',
	'IGListenerTuning::Resolve(',
	'EIGNightDifficulty::ListenOnly',
	'GetHottestZone(HottestCenter, HottestHeat)',
	'case EProbeStep::MercyNetContract:',
	'MISSINGFLOOR_MERCY PASS'
) 'runtime difficulty and mercy probe'

Write-Host (
	"MISSINGFLOOR_M8_DIFFICULTY_CONTRACT PASS assertions=$assertions " +
	'nights=4 modes=4 heatmap_decay=0.5 ending_c_routes=2 ' +
	'mercy_nets=2 mercy_responses=3 stuck_seconds=90') `
	-ForegroundColor Green
