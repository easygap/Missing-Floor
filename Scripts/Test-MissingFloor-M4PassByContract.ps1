<#
.SYNOPSIS
	§8 밤3 비트 3-7 「귀환길 — 응답 노크로 지나간다」 계약 검사.

.DESCRIPTION
	설계서가 이 비트에 붙인 말은 「은신 게임의 문법을 스스로 깨는 순간 — 회피
	대상이 애도 대상으로」다. 그러려면 세 가지가 동시에 참이어야 한다.

	하나, 밤3이 벽에서 끝나지 않아야 한다. T9가 확정되는 순간 밤을 끝내면
	귀환길이 없고, 귀환길이 없으면 이 비트가 없다.

	둘, 그가 실제로 길을 막고 서 있어야 한다. 폭 140cm 복도에 사람 하나를
	세우는 것이 이 비트의 전부이며, 텔레포트만 하면 그는 직전에 들은 소리를
	향해 기어가 버린다 — 반응을 지우는 것이 카메오를 성립시킨다.

	셋, 강제하지 않아야 한다. 대답하지 않고 몰래 지나가는 것도 정당한 해법이고
	언제나 그랬다. 다만 그것은 이 비트가 아니다 — 비트는 그가 기다리는 동안
	지나갈 때만 기록된다.

	05:30 신고는 밤이 어떻게 끝났든 이루어져야 한다. 목표로 끝났든 시간 초과로
	끝났든, 벽 뒤의 목소리를 신고하지 않은 주인공은 이 이야기에 없다.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Read-Source([string]$RelativePath) {
	Get-Content -Raw -Encoding UTF8 -LiteralPath (
		Join-Path $projectRoot $RelativePath)
}

$nightThreeHeader = Read-Source 'Source/IndieGame/Entity/IGMissingFloorNightThreeDirector.h'
$nightThree = Read-Source 'Source/IndieGame/Entity/IGMissingFloorNightThreeDirector.cpp'
$listenerHeader = Read-Source 'Source/IndieGame/Entity/IGListenerEntity.h'
$listener = Read-Source 'Source/IndieGame/Entity/IGListenerEntity.cpp'
$nightTwoBeat = Read-Source 'Source/IndieGame/Entity/IGMissingFloorNightTwoBeatDirector.cpp'
$nightLoop = Read-Source 'Source/IndieGame/Entity/IGNightLoopDirector.cpp'
$greybox = Read-Source 'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'
$greyboxHeader = Read-Source 'Source/IndieGame/Entity/IGListenerGreyboxDirector.h'
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

# --- 밤3은 벽에서 끝나지 않는다 --------------------------------------------
Require-All $nightThreeHeader @(
	'void ArmReturnPass();',
	'void NotifyCaptureReset();',
	'FIGNightThreeReturnedSignature OnReturnedHome;',
	'EIGNightThreeReturnStage GetReturnStage() const',
	'bool IsFigureInCorridor() const',
	'bool HasPassedWhileWaiting() const',
	'Passing,',
	'Home'
) '비트 3-7 surface'
Require-All $greybox @(
	'NightThree->ArmReturnPass();',
	'NightThree->OnReturnedHome.AddUObject(',
	'void AIGListenerGreyboxDirector::HandleNightThreeReturnedHome()'
) '비트 3-7 night ending'
# 05:30 신고는 새벽의 것이다. 집에 도착하는 것에 묶으면 시간 초과로 끝난 밤이
# 신고 없는 밤이 되고, 엔딩 계약이 거기서 무너진다.
Require-All $greybox @(
	'void AIGListenerGreyboxDirector::MakeNightThreeFirstReport()',
	'Narrative->WasFirstReportMade()',
	'Narrative->SetFirstReportMade(true);',
	'MakeNightThreeFirstReport();'
) '05:30 report belongs to dawn'

# --- 그가 길을 막고 선다 ---------------------------------------------------
# 복도 안쪽은 Y -375..-235로 폭이 140cm뿐이다. 계단코어(X=-277.5)와 403호 문(X=101)
# 사이에 세우므로 지나치는 일이 실제로 좁다. 그는 남쪽 벽에 귀를 대고 벽을 따라 움직이고,
# 멈춘 동안 몸 반지름이 26cm라 북쪽에 84cm가 남는다. 복도 가운데(Y -300)에 세우던 때는
# 남북 어느 쪽에도 몸이 지나갈 틈이 없었다(2026-10-19 실제 입력 검사).
Require-All $nightThree @(
	'const FVector ReturnPassPoint(-95.0f, -345.0f, FourthFloorZ);',
	'const FVector ReturnShufflePoint(-125.0f, -345.0f, FourthFloorZ);',
	'constexpr float ReturnBodyRadius = 26.0f;',
	'Listener->SetBodyRadiusForBeat(IGNightThree::ReturnBodyRadius);',
	'Listener->SetWaitHoldForBeat(IGNightThree::ReturnWaitHoldSeconds);',
	'Listener->SetBeatHold(true);',
	'constexpr float ReturnPassYaw = -90.0f;',
	'constexpr float PassClearanceCentimeters = 70.0f;',
	'const FName PassByBeatId(TEXT("Night3.PassBy"));'
) '비트 3-7 staging'
# ParkForBeat가 반응을 지운다. 이것이 없으면 P4의 세 탭 직후에 세운 그가
# 별관의 소리를 향해 기어 나간다 — 비트 2-1도 같은 이유로 같은 함수를 쓴다.
Require-All $listenerHeader @(
	'void ParkForBeat(const FVector& Where, float Yaw);'
) 'authored cameo park'
Require-All $listener @(
	'void AIGListenerEntity::ParkForBeat(const FVector& Where, const float Yaw)',
	'bReactingToSound = false;',
	'AnswerTapTimes.Reset();',
	'EnterState(EIGListenerState::Patrolling);'
) 'authored cameo park clears the reaction'
Require-All $nightThree @(
	'Listener->ParkForBeat(',
	'Listener->SetPatrolPoints(CorridorPatrolPoints);',
	'Listener->ResetToPatrolStart(/*bRaiseAggression=*/false);'
) '비트 3-7 borrowed figure'
Require-All $nightTwoBeat @(
	'Listener->ParkForBeat(IGNightTwo::FigureStagePoint, 90.0f);'
) '비트 2-1 uses the same park'

# --- 강제하지 않는다 -------------------------------------------------------
# 비트는 그가 기다리는 동안 지나갈 때만 기록된다. 몰래 지나가는 것은 막지
# 않으며, 그래도 밤은 403호에서 끝난다.
Require-All $nightThree @(
	'EIGListenerState::Waiting',
	'bPassedWhileWaiting = true;',
	'Narrative->MarkBeatPlayed(IGNightThree::PassByBeatId);',
	'bWasWestOfHim = true;'
) '비트 3-7 booking'
# 잡혀서 옮겨진 것을 도착으로 세면 잡히는 것이 목표 달성이 되고, 형체도 복도에
# 다시 세워야 한다.
Require-All $nightThree @(
	'void AIGMissingFloorNightThreeDirector::NotifyCaptureReset()',
	'bMustLeaveHomeAgain = true;',
	'bMustLeaveHomeAgain = false;'
) '비트 3-7 capture reset debt'
Require-All $nightLoop @(
	'TActorIterator<AIGMissingFloorNightThreeDirector> PassBeat(World);',
	'PassBeat->NotifyCaptureReset();'
) '비트 3-7 capture reset wiring'
# 새벽은 어떻게 왔든 복도를 비운다.
Require-All $nightThree @(
	'GetWorldTimerManager().ClearTimer(ReturnTimer);',
	'ReleaseReturnFigure();'
) '비트 3-7 dawn cleanup'

# --- 검증 -----------------------------------------------------------------
Require-All $greyboxHeader @(
	'NightThreePassContract,',
	'NightThreeHomeContract,',
	'bool bAnswerReachWasDormant = false;'
) '비트 3-7 probe steps'
Require-All $greybox @(
	'case EProbeStep::NightThreePassContract:',
	'case EProbeStep::NightThreeHomeContract:',
	'MISSINGFLOOR_N3PASS PASS',
	'the answer released her to dawn from the annex',
	'the answer did not stop him in the corridor',
	'walking past him while he waited did not book 3-7',
	'night 3 ended while she was still in the corridor',
	'Entity->SetDormant(bAnswerReachWasDormant);'
) '비트 3-7 probe'

Write-Host (
	"MISSINGFLOOR_M4_PASSBY_CONTRACT PASS assertions=$assertions " +
	'corridor_depth_cm=160 clearance_cm=70 night_ends_at=403 forced=0 ' +
	'report_owner=dawn') `
	-ForegroundColor Green
