<#
.SYNOPSIS
	§8 밤2 비트 2-1 「문 하나를 사이에 둔 첫 대면」 계약 검사.

.DESCRIPTION
	이 비트는 두 가지를 동시에 진다. 하나는 연출이다 — 밤1이 거리를 두고
	가르친 규칙에서 거리를 없애고 강철 문 한 장만 남긴다. 다른 하나는 기능이다:
	§5.5의 녹음 규칙은 거부할 것이 테이프에 있어야 성립하고, 설계서가 지정한
	무장 계기가 바로 이 노크다.

	그래서 세 가지를 검사한다.

	하나, 순서와 값 — 한 번의 노크, 문구멍, 폰, 3연, 기다림, 끌려가는 소리.
	3연의 크기는 1.0이어야 한다. §5.5의 무음 길이 표가 그 1.0을 정확히
	2.10초로 옮기고, 아침에 그녀가 듣는 공백의 길이가 거기서 정해진다.

	둘, 막히지 않음 — 문구멍을 안 보거나 폰을 안 켜는 플레이어도 비트를
	끝까지 받는다. 다만 테이프에는 남지 않는다.

	셋, 형체는 빌린 것 — 복도의 그는 실제 존재이고, 비트가 놓아줄 때
	공격 티어를 건드리지 않고 순찰로 돌아가야 한다.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Read-Source([string]$RelativePath) {
	Get-Content -Raw -Encoding UTF8 -LiteralPath (
		Join-Path $projectRoot $RelativePath)
}

$beatHeader = Read-Source 'Source/IndieGame/Entity/IGMissingFloorNightTwoBeatDirector.h'
$beat = Read-Source 'Source/IndieGame/Entity/IGMissingFloorNightTwoBeatDirector.cpp'
$greybox = Read-Source 'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'
$greyboxHeader = Read-Source 'Source/IndieGame/Entity/IGListenerGreyboxDirector.h'
$puzzleTwo = Read-Source 'Source/IndieGame/Entity/IGMissingFloorPuzzleTwoDirector.cpp'
$sceneHeader = Read-Source 'Source/IndieGame/Core/IGPrologueWorldScene.h'
$recording = Read-Source 'Source/IndieGame/Narrative/IGRecordingSubsystem.cpp'
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

# --- 좌표. 노크는 403호 현관문에서 난다 -------------------------------------
# 문은 씬이 세우고 이 비트는 문에서 잰다. 슬래브 높이도 씬이 든다. 한동안
# 여기서 절대 좌표를 통째로 요구했는데, 그러면 문을 옮겼을 때 계약이 옛
# 자리를 지킨다 — 노크가 벽에서 나고 문구멍이 복도를 보는 상태를 통과시킨다.
Require-All $sceneHeader @(
	'static constexpr float FourthFloorZ',
	'static constexpr float HomeDoorX',
	'static constexpr float HomeDoorY'
) '현관문의 주인'
Require-All $beat @(
	'constexpr float FourthFloorZ = AIGPrologueWorldScene::FourthFloorZ;',
	'AIGPrologueWorldScene::HomeDoorX',
	'AIGPrologueWorldScene::HomeDoorY',
	'AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f',
	'const FVector FigureShufflePoint = DoorLocation + FVector(8.0f, -51.0f, 0.0f);',
	'const FVector DragDepartPoint(-120.0f, -278.0f, FourthFloorZ)'
) '비트 2-1 coordinates'

# 문에서 재는 세 점. 값과 방향을 함께 본다 — 복도는 Y가 작아지는 쪽이므로
# 노크와 형체는 빼고 문구멍은 더한다. 부호가 뒤집히면 노크는 집 안에서 나고
# 문구멍은 복도를 본다.
Require-All $beat @(
	'constexpr float KnockCorridorOffset = 7.0f;',
	'constexpr float KnockFistHeight = 112.0f;',
	'DoorLocation + FVector(0.0f, -KnockCorridorOffset, KnockFistHeight)',
	'constexpr float PeepholeInsideOffset = 5.0f;',
	'constexpr float PeepholeEyeHeight = 155.0f;',
	'DoorLocation + FVector(0.0f, PeepholeInsideOffset, PeepholeEyeHeight)',
	'constexpr float FigureStandOffset = 47.0f;',
	'DoorLocation + FVector(0.0f, -FigureStandOffset, 0.0f)'
) '문에서 재는 세 점'

# §5.5의 폰은 관리실이 아니라 403호 안에 있다. 처음 넣을 때 슬래브 높이를
# 빠뜨려서 9미터 아래 관리실 바닥에 놓여 있었다.
Require-All $puzzleTwo @(
	'constexpr float FourthFloorZ = AIGPrologueWorldScene::FourthFloorZ;',
	'const FVector PhoneAtDoorLocation(150.0f, -196.0f, FourthFloorZ + 4.0f);'
) '§5.5 phone placement'

# --- 순서. 한 번 → 문구멍 → 폰 → 3연 → 기다림 → 끌림 ----------------------
Require-All $beatHeader @(
	'Idle,',
	'Opening,',
	'AwaitingPeephole,',
	'AwaitingPhone,',
	'Answering,',
	'Spent'
) '비트 2-1 stage order'
Require-All $beat @(
	'void AIGMissingFloorNightTwoBeatDirector::PlayFirstKnock()',
	'void AIGMissingFloorNightTwoBeatDirector::PlayAnswer()',
	'void AIGMissingFloorNightTwoBeatDirector::PlayDragAway()',
	'CreateWallKnockSingle(',
	'CreateWallKnockTriple(',
	'CreateEntityDragLoop(this, /*bVinyl=*/false)',
	'KnockCount = 1;',
	'KnockCount = 2;',
	'KnockCount = 3;'
) '비트 2-1 cue order'

# --- 값. 3연이 1.0이어야 §5.5의 2.10초 무음이 나온다 -----------------------
Require-All $beat @(
	'constexpr float SingleKnockLoudness = 0.55f;',
	'constexpr float TripleKnockLoudness = 1.0f;',
	'constexpr float DragLoudness = 0.42f;',
	'constexpr float SingleKnockMuffle = 0.72f;',
	'constexpr float TripleKnockMuffle = 0.66f;'
) '비트 2-1 noise table'
Require-All $recording @(
	'constexpr float MaximumSuppressedSeconds = 2.10f;'
) '§5.5 gap length table'

# 【S】0.7 — §8 표의 값 그대로.
Require-All $beatHeader @(
	'static constexpr float ScareAmount = 0.7f;'
) '비트 2-1 scare value'
Require-All $beat @(
	'Stress->ApplyScare(ScareAmount);'
) '비트 2-1 scare wiring'

# --- 막히지 않음 -----------------------------------------------------------
# 문구멍도 폰도 인내 시간이 있다. 비트가 플레이어의 확인을 기다려 정지하면
# §20.3이 금지하는 그 상태가 된다.
Require-All $beat @(
	'constexpr float PeepholePatienceSeconds = 40.0f;',
	'constexpr float PhonePatienceSeconds = 55.0f;',
	'StageSeconds >= IGNightTwo::PeepholePatienceSeconds',
	'StageSeconds >= IGNightTwo::PhonePatienceSeconds',
	'constexpr float OpeningDelaySeconds = 6.0f;',
	'constexpr float WaitAfterTripleSeconds = 3.4f;'
) '비트 2-1 patience'

# --- 형체는 빌린 것 --------------------------------------------------------
# ParkForBeat는 위치만 옮기지 않고 직전 소리에 대한 반응까지 지운다. 이 비트는
# 노크로 시작하므로, 지우지 않으면 그가 문 앞에 한 프레임 서 있다가 자기가 들은
# 자리로 기어 나간다.
Require-All $beat @(
	'Listener->ParkForBeat(IGNightTwo::FigureStagePoint, 90.0f);',
	'IGNightTwo::FigureStagePoint,',
	'Listener->SetPatrolPoints(CorridorPatrolPoints);',
	'Listener->ResetToPatrolStart(/*bRaiseAggression=*/false);'
) '비트 2-1 borrowed figure'
# 복도의 그가 소리의 발신자다. 그래야 §5.5가 그것을 거부한다.
Require-All $beat @(
	'Noise->ReportNoise(',
	'Entity.Get());'
) '비트 2-1 noise instigator'

# --- 1회 한정 -------------------------------------------------------------
Require-All $beat @(
	'const FName BeatId(TEXT("Night2.DoorKnock"));',
	'const FName PeepholeBeatId(TEXT("Night2.Peephole"));',
	'Narrative->MarkBeatPlayed(IGNightTwo::BeatId)',
	'Narrative->MarkBeatPlayed(IGNightTwo::PeepholeBeatId)'
) '비트 2-1 once per run'
# 밤2가 아니면 무장하지 않고, 문구멍은 노크 전에 열려 있지 않다.
Require-All $beat @(
	'Narrative->GetNightIndex() == 2',
	'Peephole->SetInteractionEnabled(false);',
	'Peephole->SetInteractionEnabled(true);'
) '비트 2-1 arming'

# --- 배선과 검증 ----------------------------------------------------------
Require-All $greyboxHeader @(
	'NightTwoDoorBeatContract,',
	'TObjectPtr<class AIGMissingFloorNightTwoBeatDirector> NightTwoBeats;'
) 'beat director wiring'
Require-All $greybox @(
	'NightTwoBeats = World->SpawnActor<AIGMissingFloorNightTwoBeatDirector>(',
	'NightTwoBeats->SetHourActive(bActive);',
	'case EProbeStep::NightTwoDoorBeatContract:',
	'MISSINGFLOOR_N2DOOR PASS',
	'NightTwoBeats->AdvanceForTesting();',
	'the figure stayed at the door after the beat',
	'the armed phone was not running for the answer',
	'the triple knock left only'
) 'runtime beat probe'

# --- 비트 2-5 「귀환 추격」 -------------------------------------------------
# 밤2는 종이가 모순되는 자리가 아니라 403호 문에서 끝난다. 원문 복원이 밤을
# 끝내면 관리실 안에서 새벽으로 풀려나고 이 비트가 통째로 사라진다. T7은
# 밤3의 열쇠 고리 옆에서 닫힌다(2026-09-10).
# 0.10.0: 안쪽 방 장면 뒤에 거는 귀환은 장부 결론을 다시 말하지 않는다(bWithConclusion=false).
Require-All $beatHeader @(
	'void ArmReturnChase(bool bWithConclusion = true);',
	'void NotifyCaptureReset();',
	'FIGNightTwoReturnedSignature OnReturnedHome;',
	'AwaitingExit,',
	'Chased,',
	'Home',
	'static constexpr float ChaseScareAmount = 0.95f;'
) '비트 2-5 surface'
Require-All $greybox @(
	'NightTwoBeats->ArmReturnChase();',
	'NightTwoBeats->OnReturnedHome.AddUObject(',
	'void AIGListenerGreyboxDirector::HandleNightTwoReturnedHome()',
	'NightPhase->CompleteNightGoal();'
) '비트 2-5 night ending'
# 잡혀서 403호 안에서 깨어난 것을 도착으로 세면 잡히는 것이 목표 달성이 된다.
# 한 번 나갔다 와야 한다.
Require-All $beat @(
	'void AIGMissingFloorNightTwoBeatDirector::NotifyCaptureReset()',
	'bMustLeaveHomeAgain = true;',
	'bMustLeaveHomeAgain = false;'
) '비트 2-5 capture reset debt'
$nightLoop = Read-Source 'Source/IndieGame/Entity/IGNightLoopDirector.cpp'
Require-All $nightLoop @(
	'TActorIterator<AIGMissingFloorNightTwoBeatDirector> ReturnBeat(World);',
	'ReturnBeat->NotifyCaptureReset();'
) '비트 2-5 capture reset wiring'

# 두 번의 소리. 이것이 이 비트의 전부다 — 규칙이 「두 번 대답하는 소리는
# 누군가다」이므로 전용 추격 코드가 한 줄도 필요하지 않다.
Require-All $beat @(
	'constexpr float CollapseSecondImpactSeconds = 0.55f;',
	'constexpr float CollapseLoudness = 0.95f;',
	'const FVector CollapseLocation(260.0f, -166.0f, 24.0f);',
	'constexpr float BoothExitY = -242.0f;',
	'IGNightTwo::CollapseLoudness,',
	'nullptr);',
	'const FName ReturnBeatId(TEXT("Night2.ReturnChase"));'
) '비트 2-5 collapse'
$entity = Read-Source 'Source/IndieGame/Entity/IGListenerEntity.cpp'
Require-All $entity @(
	'if (bSecondSound && Tuning.bChaseEnabled)',
	'EnterState(EIGListenerState::Chasing);'
) 'second sound is a someone'

# 소리의 출처가 눈에 보여야 한다. 영구 프롭이므로 아침에도 그대로 서 있다.
$scene = Read-Source 'Source/IndieGame/Core/IGPrologueWorldScene.cpp'
Require-All $scene @(
	'CreateBlock(',
	'FVector(267, -164, 59.74f), FVector(75, 150, 100),'
	'PropMesh(TEXT("SM_GypsumCutBoard"))'
	'CreateProp(TEXT("SM_WorkPaintCan"), FVector(230, -198, 0)'
) '비트 2-5 material stack'

Require-All $greyboxHeader @(
	'NightTwoReturnChaseContract,',
	'NightTwoHomeContract,'
) '비트 2-5 probe steps'
Require-All $greybox @(
	'case EProbeStep::NightTwoReturnChaseContract:',
	'case EProbeStep::NightTwoHomeContract:',
	'MISSINGFLOOR_N2CHASE PASS',
	'restoring the original released her to dawn from the booth',
	'night 2 ended while she was still out of 403',
	'the collapse did not move the building',
	'Entity->GetListenerState() != EIGListenerState::Patrolling'
) '비트 2-5 probe'

Write-Host (
	"MISSINGFLOOR_M3_DOOR_BEAT_CONTRACT PASS assertions=$assertions " +
	'cues=3 single=0.55 triple=1.00 drag=0.42 gap_seconds=2.10 scare=0.7|0.95 ' +
	'patience=40|55 plays=1 collapse_impacts=2 night_ends_at=403') `
	-ForegroundColor Green
