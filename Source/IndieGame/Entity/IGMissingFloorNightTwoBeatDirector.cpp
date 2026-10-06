#include "Entity/IGMissingFloorNightTwoBeatDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGRecordingSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"

namespace IGNightTwo
{
	/**
	 * 4층 세계 좌표. 남쪽 벽이 Y=-225이므로 집
	 * 안쪽은 Y가 0에 가까운 쪽, 복도는 그 반대쪽이다. 층 높이는 씬이 들고
	 * 있고, 문·노크·문구멍은 그 위에서 잰다.
	 */
	constexpr float FourthFloorZ = AIGPrologueWorldScene::FourthFloorZ;
	const FVector DoorLocation(
		AIGPrologueWorldScene::HomeDoorX + AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f,
		AIGPrologueWorldScene::HomeDoorY,
		FourthFloorZ);
	// 아래 셋은 문에서 잰다. 예전에는 절대 좌표를 적고 관계는 주석에만
	// 두었는데, 그러면 문을 옮겼을 때 노크가 벽에서 나고 문구멍이
	// 복도를 본다. 복도는 Y가 작아지는 쪽이다.
	/** 노크는 복도 쪽 문짝에서 난다. 주먹 높이. */
	constexpr float KnockCorridorOffset = 7.0f;
	constexpr float KnockFistHeight = 112.0f;
	const FVector KnockLocation =
		DoorLocation + FVector(0.0f, -KnockCorridorOffset, KnockFistHeight);
	/** 문구멍은 문 안쪽, 눈높이. */
	constexpr float PeepholeInsideOffset = 5.0f;
	constexpr float PeepholeEyeHeight = 155.0f;
	const FVector PeepholeLocation =
		DoorLocation + FVector(0.0f, PeepholeInsideOffset, PeepholeEyeHeight);
	/** 그가 서는 자리 — 문에서 47 cm, 복도 안. */
	constexpr float FigureStandOffset = 47.0f;
	const FVector FigureStagePoint =
		DoorLocation + FVector(0.0f, -FigureStandOffset, 0.0f);
	/** 두 점 사이를 오가게 둔다. 서 있는 사람이 아니라 기다리는 사람이 된다. */
	const FVector FigureShufflePoint = DoorLocation + FVector(8.0f, -51.0f, 0.0f);
	/** 끌려가는 소리는 계단코어 쪽으로 멀어진다. */
	const FVector DragDepartPoint(-120.0f, -278.0f, FourthFloorZ);

	/**
	 * 04:30 직후가 아니라 6초 뒤다. 밤이 시작한 프레임에 노크가 나면 연출이
	 * 아니라 로딩의 일부로 읽힌다.
	 */
	constexpr float OpeningDelaySeconds = 6.0f;
	/**
	 * 인내 시간. 문구멍을 안 보는 플레이어도, 폰을 안 켜는 플레이어도 막히지
	 * 않는다 — 다만 테이프에는 남지 않는다. §20.3의 규칙은 퍼즐만이 아니라
	 * 비트에도 적용된다.
	 */
	constexpr float PeepholePatienceSeconds = 40.0f;
	constexpr float PhonePatienceSeconds = 55.0f;
	/** 폰이 놓인 뒤 3연까지의 숨. */
	constexpr float TripleDelaySeconds = 1.6f;
	/** 기다림. 이 침묵이 이 비트에서 가장 긴 시간이다. */
	constexpr float WaitAfterTripleSeconds = 3.4f;
	/** 끌려가는 소리가 복도를 빠져나가는 데 걸리는 시간. */
	constexpr float DragSeconds = 4.4f;
	constexpr float PollSeconds = 0.25f;

	/**
	 * §21.2 표의 값. 3연은 1.0 — 「건물이 그로 가득 차는」 2초이고, §5.5의
	 * 무음 길이 표가 그 1.0을 정확히 2.10초로 환산한다. 여는 노크 한 번은
	 * 그보다 작아야 한다: 그것은 부르는 소리이고 3연은 대답을 요구하는 소리다.
	 */
	constexpr float SingleKnockLoudness = 0.55f;
	constexpr float TripleKnockLoudness = 1.0f;
	constexpr float DragLoudness = 0.42f;

	/** 강철 현관문 한 장. 3연 때 그는 문에 더 붙어 있다. */
	constexpr float SingleKnockMuffle = 0.72f;
	constexpr float TripleKnockMuffle = 0.66f;

	constexpr float KnockVolume = 0.92f;
	constexpr float KnockInnerRadius = 220.0f;
	constexpr float KnockFalloff = 1500.0f;
	constexpr float DragVolume = 0.62f;

	/**
	 * 403호 현관문은 강철이다. 플레이어가 같은 문을 두드리면 철문 녹음이 나는데
	 * 그의 주먹만 석고벽 합성이어서, 그가 철문을 두드리는 유일한 장면이 가장
	 * 가짜처럼 들렸다. 녹음을 위에 얹고 합성은 문짝 너머의 저역으로 깐다.
	 */
	constexpr float SteelKnockVolume = 0.9f;
	constexpr float SteelKnockPitch = 0.84f;
	constexpr float SteelUnderlayVolume = 0.45f;
	constexpr int32 SteelTripleCount = 3;
	const TCHAR* const SteelTripleSamples[SteelTripleCount] = {
		TEXT("Knock_Steel_0"), TEXT("Knock_Steel_1"), TEXT("Knock_Steel_2")};
	constexpr float SteelTriplePitches[SteelTripleCount] = {0.84f, 0.80f, 0.86f};
	constexpr float SteelTripleVolume = 0.92f;
	/** 합성 3연, Entity_KnockTriple과 같은 간격. */
	constexpr float SteelTripleSpacingSeconds = 0.62f;

	/** 3연 첫 타 뒤 숨을 들이켜기까지. 놀람이 아니라 한기라서 0.4 아래다. */
	constexpr float AnswerGaspDelaySeconds = 0.35f;
	constexpr float AnswerGaspScare = 0.35f;
	/** 테이프에 남는 숨의 크기. 발소리보다 작다. */
	constexpr float AnswerGaspLoudness = 0.06f;
	/** 폰을 기다린 지 12초. 문구멍을 안 본 사람에게도 할 일을 한 번 말한다. */
	constexpr float PhoneNudgeSeconds = 12.0f;
	constexpr float DragMoveStepSeconds = 0.05f;

	/**
	 * 문구멍을 들여다본 뒤 문 바로 아래에서 석고가 갈라지기까지. 캄캄한 복도를
	 * 한 번 훑을 시간이다. 그는 기는 몸이라 문구멍 눈높이에 없다 — 보이지
	 * 않는데 바로 발밑에 있다는 것을 소리가 알려 준다.
	 */
	constexpr float PeepholeCrackDelaySeconds = 1.4f;
	/** 그가 문 앞 자리에서 이만큼 안에 있어야 갈라짐이 난다. */
	constexpr float PeepholeCrackReach = 120.0f;
	/** 문 하나 너머, 발밑. 0.4를 넘겨 숨이 걸린다. */
	constexpr float PeepholeCrackScare = 0.45f;

	const FName BeatId(TEXT("Night2.DoorKnock"));
	const FName PeepholeBeatId(TEXT("Night2.Peephole"));
	const FName ReturnBeatId(TEXT("Night2.ReturnChase"));

	// -- 비트 2-5 「귀환 추격」 ---------------------------------------------
	/**
	 * 관리실 오른쪽 벽. 관리실은 1층이라 Z가 작다. 쌓아 둔 자재가
	 * 무너지는 자리이며, 소리의 출처가 눈에 보이게 BuildLobby가 같은 좌표에
	 * 판재 더미를 세워 둔다.
	 */
	const FVector CollapseLocation(260.0f, -166.0f, 24.0f);
	/**
	 * 두 번. 이것이 이 비트의 전부다 — §5의 규칙이 「두 번 대답하는 소리는
	 * 누군가다」이므로, 무너지는 더미의 첫 조각과 나머지가 실제 AI를 추격으로
	 * 넘긴다. 전용 추격 코드는 한 줄도 없다. 0.55초는 반응 기억 10초 안이고,
	 * 한 번의 사고로 들릴 만큼 붙어 있다.
	 */
	constexpr float CollapseSecondImpactSeconds = 0.55f;
	/**
	 * 0.95는 §8 표의 【S】와 같은 값이다. 반경은 크기 × 2600 cm이므로 2470 cm까지
	 * 실리고, 층간 감쇠 1.4배를 물어도 4층 복도의 그에게 닿는다. 계산이 아니라
	 * 그렇게 되도록 고른 값이다 — 이 비트가 확실히 추격이 되어야 한다.
	 */
	constexpr float CollapseLoudness = 0.95f;
	constexpr float CollapseVolume = 1.0f;
	constexpr float CollapseInnerRadius = 260.0f;
	constexpr float CollapseFalloff = 2100.0f;
	/** 판재 더미의 첫 장과 나머지. 나머지가 무겁고 낮다. */
	constexpr float CollapseFirstPitch = 0.92f;
	constexpr float CollapseSecondPitch = 0.78f;
	/** 두 번째 충돌 뒤 판재 모서리에서 가루가 떨어지기까지. */
	constexpr float CollapseDustDelaySeconds = 0.45f;
	constexpr float CollapseDustVolume = 0.5f;

	/**
	 * 2-4 「공포가 아니라 한기」(【S】0.5). 원문을 다 읽은 몸이 식는다. 0.4부터는
	 * 들숨이 따라와 놀람이 되므로 그 아래다.
	 */
	constexpr float ReturnChillScare = 0.30f;
	/** 한기 뒤 위층 바닥이 한 번 운다. 곧 무너질 자리의 예고다. */
	constexpr float ReturnChillCreakSeconds = 1.4f;
	/** 관리실 책상 바로 위층 바닥. */
	const FVector ReturnCreakLocation(179.0f, -103.0f, 330.0f);

	/** 관리실은 Y -235..-75. 이 선을 넘으면 나선 것이다. */
	constexpr float BoothExitY = -242.0f;
	constexpr float BoothNorthY = -75.0f;
	/** 관리실 책상(밑장 자리). 관리실 안인지 가를 때 X 범위를 대신한다. */
	const FVector BoothDeskLocation(179.0f, -103.0f, 76.0f);
	constexpr float BoothDeskReach = 300.0f;
	const FName PuzzleTwoId(TEXT("P2"));
	const FName NightTwoGoalBeatId(TEXT("Night2.Goal"));
	/** 403호 실내는 4층 X -190..190, Y -235..235. */
	const FBox Unit403Interior(
		FVector(-190.0f, -235.0f, FourthFloorZ - 20.0f),
		FVector(190.0f, 235.0f, FourthFloorZ + 230.0f));
	constexpr float ReturnPollSeconds = 0.25f;
}

AIGMissingFloorNightTwoBeatDirector::AIGMissingFloorNightTwoBeatDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool AIGMissingFloorNightTwoBeatDirector::Configure(
	AIGPrologueWorldScene* InScene,
	AIGListenerEntity* InEntity,
	AIGPlayerCharacter* InPlayer,
	const TArray<FVector>& InCorridorPatrolPoints)
{
	UWorld* World = GetWorld();
	if (!World || !InScene || !InEntity)
	{
		return false;
	}
	Scene = InScene;
	Entity = InEntity;
	Player = InPlayer;
	CorridorPatrolPoints = InCorridorPatrolPoints;

	UStaticMesh* CubeMesh =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		return false;
	}

	// 문구멍. 카메라를 자르지 않는다 — 설계서가 이 건물 안의 어떤 이동도 컷으로
	// 대체하지 못하게 해 둔 것과 같은 이유다. 프롭은 그녀가 본 것을 말하고,
	// 복도의 형체는 실제로 거기 서 있으므로 문을 열면 그가 있다.
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Name = TEXT("MissingFloorNightTwoPeephole");
	Peephole = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGNightTwo::PeepholeLocation),
		SpawnParameters);
	if (!Peephole)
	{
		return false;
	}
	Peephole->Configure(
		CubeMesh,
		nullptr,
		FVector(3.0f, 2.4f, 3.0f),
		NSLOCTEXT("IGMissingFloor", "N2PeepholePrompt", "문구멍으로 내다보기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		0.0f,
		0.0f,
		/*bPresentationVisible=*/false);
	Peephole->OnExamined.AddUObject(
		this,
		&AIGMissingFloorNightTwoBeatDirector::HandlePeepholeExamined);
	// 노크가 나기 전에는 볼 이유가 없다. 밤새 문구멍이 켜져 있으면 비트가
	// 시작하기 전에 소진된다.
	Peephole->SetInteractionEnabled(false);
	return true;
}

void AIGMissingFloorNightTwoBeatDirector::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(StageTimer);
	GetWorldTimerManager().ClearTimer(ReturnTimer);
	GetWorldTimerManager().ClearTimer(CollapseTimer);
	GetWorldTimerManager().ClearTimer(DragFadeTimer);
	GetWorldTimerManager().ClearTimer(DragMoveTimer);
	GetWorldTimerManager().ClearTimer(AnswerKnockTimer);
	GetWorldTimerManager().ClearTimer(AnswerGaspTimer);
	GetWorldTimerManager().ClearTimer(PeepholeTimer);
	GetWorldTimerManager().ClearTimer(CollapseDustTimer);
	GetWorldTimerManager().ClearTimer(ReturnChillTimer);
	ReleaseFigure();
	Super::EndPlay(EndPlayReason);
}

UIGMissingFloorNarrativeSubsystem*
AIGMissingFloorNightTwoBeatDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
}

UIGNoiseSubsystem* AIGMissingFloorNightTwoBeatDirector::GetNoise() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UIGNoiseSubsystem>() : nullptr;
}

UIGRecordingSubsystem*
AIGMissingFloorNightTwoBeatDirector::GetRecording() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UIGRecordingSubsystem>() : nullptr;
}

void AIGMissingFloorNightTwoBeatDirector::SetHourActive(const bool bHourActive)
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const bool bIsNightTwo = Narrative && Narrative->GetNightIndex() == 2;
	const bool bAlreadyPlayed =
		bPlayed || (Narrative && Narrative->HasBeatPlayed(IGNightTwo::BeatId));

	if (!bHourActive || !bIsNightTwo || bAlreadyPlayed)
	{
		// 새벽이 오거나 다른 밤이면 문구멍을 닫고 형체를 순찰로 돌려보낸다.
		// 귀환 추격도 함께 내린다 — 시간 초과로 새벽이 왔다면 목표는 이미
		// 끝났고, 낮에 폴링을 계속할 이유가 없다.
		GetWorldTimerManager().ClearTimer(StageTimer);
		GetWorldTimerManager().ClearTimer(ReturnTimer);
		GetWorldTimerManager().ClearTimer(AnswerKnockTimer);
		GetWorldTimerManager().ClearTimer(AnswerGaspTimer);
		GetWorldTimerManager().ClearTimer(PeepholeTimer);
		GetWorldTimerManager().ClearTimer(ReturnChillTimer);
		if (ReturnStage != EIGNightTwoReturnStage::Home)
		{
			ReturnStage = EIGNightTwoReturnStage::Idle;
		}
		if (Peephole)
		{
			Peephole->SetInteractionEnabled(false);
		}
		ReleaseFigure();
		Stage = bAlreadyPlayed
			? EIGNightTwoBeatStage::Spent
			: EIGNightTwoBeatStage::Idle;
		// 문 앞 노크를 이미 겪은 되풀이 밤2도 끝낼 길은 있어야 한다.
		if (bHourActive && bIsNightTwo)
		{
			RearmReturnIfOwed();
		}
		return;
	}

	bPhoneNudged = false;
	EnterStage(EIGNightTwoBeatStage::Opening);
	GetWorldTimerManager().SetTimer(
		StageTimer,
		this,
		&AIGMissingFloorNightTwoBeatDirector::AdvanceStage,
		IGNightTwo::PollSeconds,
		true);
	RearmReturnIfOwed();
}

void AIGMissingFloorNightTwoBeatDirector::RearmReturnIfOwed()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative
		|| Narrative->GetNightIndex() != 2
		|| !Narrative->IsPuzzleSolved(IGNightTwo::PuzzleTwoId)
		|| Narrative->HasBeatPlayed(IGNightTwo::NightTwoGoalBeatId)
		|| ReturnStage != EIGNightTwoReturnStage::Idle)
	{
		return;
	}
	// 남은 일은 관리실에서 403호까지 돌아오는 길 하나다. 되풀이되는 새벽이니
	// 자재 더미도 다시 무너진다.
	ReturnStage = EIGNightTwoReturnStage::AwaitingBooth;
	bReturnChaseFired = false;
	bMustLeaveHomeAgain = false;
	GetWorldTimerManager().SetTimer(
		ReturnTimer,
		this,
		&AIGMissingFloorNightTwoBeatDirector::AdvanceReturn,
		IGNightTwo::ReturnPollSeconds,
		true);
}

void AIGMissingFloorNightTwoBeatDirector::EnterStage(
	const EIGNightTwoBeatStage NextStage)
{
	Stage = NextStage;
	StageSeconds = 0.0f;
}

void AIGMissingFloorNightTwoBeatDirector::AdvanceStage()
{
	StageSeconds += IGNightTwo::PollSeconds;

	switch (Stage)
	{
	case EIGNightTwoBeatStage::Opening:
		if (StageSeconds >= IGNightTwo::OpeningDelaySeconds)
		{
			PlayFirstKnock();
			EnterStage(EIGNightTwoBeatStage::AwaitingPeephole);
		}
		break;

	case EIGNightTwoBeatStage::AwaitingPeephole:
		// 봤거나, 안 봐도 시간이 지나면 넘어간다. 그는 그녀의 확인을 기다려
		// 주지 않는다.
		if (bPeepholeSeen
			|| StageSeconds >= IGNightTwo::PeepholePatienceSeconds)
		{
			EnterStage(EIGNightTwoBeatStage::AwaitingPhone);
		}
		break;

	case EIGNightTwoBeatStage::AwaitingPhone:
	{
		const UIGRecordingSubsystem* Recording = GetRecording();
		const bool bArmed = Recording && Recording->IsRecording();
		// 문구멍을 안 본 사람에게는 폰을 가리키는 말이 한 줄도 없었다. 그런
		// 플레이어에게 55초 뒤의 3연은 녹음 없이 지나가고 밤2의 심기가 통째로
		// 빠진다. 결론이 아니라 할 일로, 한 번만.
		if (!bArmed && !bPhoneNudged
			&& StageSeconds >= IGNightTwo::PhoneNudgeSeconds)
		{
			bPhoneNudged = true;
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT("IGMissingFloor", "N2PhoneNudgeThought", "녹음이라도 해 두자."),
				3.2f);
		}
		if (bArmed || StageSeconds >= IGNightTwo::PhonePatienceSeconds)
		{
			bRecordedAnswer = bArmed;
			EnterStage(EIGNightTwoBeatStage::Answering);
		}
		break;
	}

	case EIGNightTwoBeatStage::Answering:
	{
		// 3연 → 기다림 → 끌려가는 소리. 각 구간은 앞 구간이 끝난 시각으로만
		// 정해지므로 폴 간격이 바뀌어도 순서가 흐트러지지 않는다.
		const float TripleAt = IGNightTwo::TripleDelaySeconds;
		const float DragAt = TripleAt + IGNightTwo::WaitAfterTripleSeconds;
		const float DoneAt = DragAt + IGNightTwo::DragSeconds;
		if (KnockCount == 1 && StageSeconds >= TripleAt)
		{
			PlayAnswer();
		}
		else if (KnockCount == 2 && StageSeconds >= DragAt)
		{
			PlayDragAway();
		}
		else if (KnockCount >= 3 && StageSeconds >= DoneAt)
		{
			if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
			{
				Narrative->MarkBeatPlayed(IGNightTwo::BeatId);
			}
			bPlayed = true;
			GetWorldTimerManager().ClearTimer(StageTimer);
			if (Peephole)
			{
				Peephole->SetInteractionEnabled(false);
			}
			ReleaseFigure();
			EnterStage(EIGNightTwoBeatStage::Spent);
		}
		break;
	}

	default:
		GetWorldTimerManager().ClearTimer(StageTimer);
		break;
	}
}

void AIGMissingFloorNightTwoBeatDirector::PlayFirstKnock()
{
	// 한 번이다. 부르는 소리이고, 그녀를 문으로 데려오는 것이 전부다. 주먹이
	// 닿는 것은 강철 문짝이라 철문 녹음이 앞에 서고, 합성은 문짝 너머로 전해지는
	// 저역만 맡는다. 녹음이 없으면 합성이 혼자 예전 크기로 낸다.
	USoundBase* Steel = IGAudio::Sample(IGNightTwo::SteelTripleSamples[0]);
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateWallKnockSingle(
			this,
			IGNightTwo::SingleKnockMuffle),
		IGNightTwo::KnockLocation,
		Steel ? IGNightTwo::SteelUnderlayVolume : IGNightTwo::KnockVolume,
		1.0f,
		IGNightTwo::KnockInnerRadius,
		IGNightTwo::KnockFalloff,
		EIGAudioBus::Entity);
	if (Steel)
	{
		IGAudio::SpawnOneShotAt(
			this,
			Steel,
			IGNightTwo::KnockLocation,
			IGNightTwo::SteelKnockVolume,
			IGNightTwo::SteelKnockPitch,
			IGNightTwo::KnockInnerRadius,
			IGNightTwo::KnockFalloff,
			EIGAudioBus::Entity);
	}
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "N2DoorKnockCaption", "현관문 두드리는 소리"),
		2.0f,
		IGNightTwo::KnockLocation);
	// 계기는 소리가 아니라 위치다. 사흘째 벽에서 들리던 것이 이번엔 문이다.
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"N2DoorKnockThought",
			"이번엔 현관문이야."),
		4.2f);

	// §5.5가 거부할 것이 생기는 순간이다. 발신자가 존재여야 하므로 소음
	// 이벤트의 instigator를 그로 넘긴다 — 그것이 테이프의 공백을 만든다.
	if (UIGNoiseSubsystem* Noise = GetNoise())
	{
		Noise->ReportNoise(
			IGNightTwo::KnockLocation,
			IGNightTwo::SingleKnockLoudness,
			Entity.Get());
	}
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->ApplyScare(ScareAmount);
		}
		// 우리 문이다. 스트레스만 오르고 화면은 가만히 있을 수 없다.
		PlayerCharacter->PlayScareKick(1.3f);
	}

	KnockCount = 1;
	StageFigure();
	if (Peephole)
	{
		Peephole->SetInteractionEnabled(true);
	}
}

void AIGMissingFloorNightTwoBeatDirector::PlayAnswer()
{
	const bool bSteel = IGAudio::Sample(IGNightTwo::SteelTripleSamples[0]) != nullptr;
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateWallKnockTriple(
			this,
			IGNightTwo::TripleKnockMuffle),
		IGNightTwo::KnockLocation,
		bSteel ? IGNightTwo::SteelUnderlayVolume : IGNightTwo::KnockVolume,
		1.0f,
		IGNightTwo::KnockInnerRadius,
		IGNightTwo::KnockFalloff,
		EIGAudioBus::Entity);
	// 철문 세 타는 합성 3연과 같은 0.62초 간격으로 겹친다. 첫 타는 지금,
	// 나머지는 스스로 다음 타를 건다.
	AnswerSteelHits = 0;
	GetWorldTimerManager().ClearTimer(AnswerKnockTimer);
	if (bSteel)
	{
		PlayAnswerSteelHit();
	}
	GetWorldTimerManager().SetTimer(
		AnswerGaspTimer,
		this,
		&AIGMissingFloorNightTwoBeatDirector::PlayAnswerGasp,
		IGNightTwo::AnswerGaspDelaySeconds,
		false);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "N2TripleCaption", "문 너머에서 연달아 세 번 두드리는 소리"),
		2.4f,
		IGNightTwo::KnockLocation);
	// 1.0은 §21.2의 3연 값이고, §5.5의 표가 그것을 2.10초의 무음으로 옮긴다.
	// 아침에 그녀가 듣는 공백의 길이는 여기서 정해진다.
	if (UIGNoiseSubsystem* Noise = GetNoise())
	{
		Noise->ReportNoise(
			IGNightTwo::KnockLocation,
			IGNightTwo::TripleKnockLoudness,
			Entity.Get());
	}
	KnockCount = 2;
}

void AIGMissingFloorNightTwoBeatDirector::PlayDragAway()
{
	// 끌려가는 소리. 무엇이 끌려가는지는 밤4에 가서야 알게 되고, 지금은 복도가
	// 비어 가는 소리일 뿐이다. 4층 복도는 화강석 타일이라 비닐이 아니다.
	// 루프 파형이라 복도를 다 빠져나가는 시간에 맞춰 멎게 한다 — 예전엔 아무도
	// 안 끊어서 ENTITY 상한에 밀려날 때까지 문 앞에서 계속 긁었다.
	UAudioComponent* DragAway = IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateEntityDragLoop(this, /*bVinyl=*/false),
		IGNightTwo::FigureStagePoint,
		IGNightTwo::DragVolume,
		1.0f,
		IGNightTwo::KnockInnerRadius,
		IGNightTwo::KnockFalloff,
		EIGAudioBus::Entity);
	if (DragAway)
	{
		TWeakObjectPtr<UAudioComponent> WeakDrag(DragAway);
		GetWorldTimerManager().SetTimer(
			DragFadeTimer,
			FTimerDelegate::CreateWeakLambda(this, [WeakDrag]()
			{
				if (UAudioComponent* Loop = WeakDrag.Get())
				{
					Loop->FadeOut(1.2f, 0.0f);
				}
			}),
			FMath::Max(IGNightTwo::DragSeconds - 1.2f, 0.2f),
			false);
		// 자막이 「멀어짐」이라고 하는 소리는 실제로 멀어져야 한다. 문 앞에 박힌
		// 루프가 작아지기만 하면 멀어지는 것이 아니라 꺼지는 것이다. 계단코어
		// 쪽으로 옮기며 페이드한다.
		DragAwayLoop = DragAway;
		DragElapsedSeconds = 0.0f;
		GetWorldTimerManager().SetTimer(
			DragMoveTimer,
			this,
			&AIGMissingFloorNightTwoBeatDirector::AdvanceDragAway,
			IGNightTwo::DragMoveStepSeconds,
			true);
	}
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "N2DragCaption", "뭔가 끌리며 멀어지는 소리"),
		2.6f,
		IGNightTwo::FigureStagePoint);
	if (UIGNoiseSubsystem* Noise = GetNoise())
	{
		Noise->ReportNoise(
			IGNightTwo::FigureStagePoint,
			IGNightTwo::DragLoudness,
			Entity.Get());
	}
	// 소리만 멀어지는 것이 아니라 그도 멀어진다. 문을 열어 확인하는 플레이어가
	// 빈 복도를 봐야 한다. 문 앞 자리를 첫 칸으로 두면 그 자리에서 한 번 더
	// 두드리고 나서야 떠났다. 붙들린 채 계단코어 쪽으로 기어가고, 닿아도
	// 두드리지 않는다.
	if (AIGListenerEntity* Listener = Entity.Get())
	{
		Listener->SetPatrolPoints({IGNightTwo::DragDepartPoint});
	}
	KnockCount = 3;
}

void AIGMissingFloorNightTwoBeatDirector::PlayAnswerSteelHit()
{
	if (AnswerSteelHits >= IGNightTwo::SteelTripleCount)
	{
		return;
	}
	const int32 Hit = AnswerSteelHits++;
	if (USoundBase* Steel = IGAudio::Sample(IGNightTwo::SteelTripleSamples[Hit]))
	{
		IGAudio::SpawnOneShotAt(
			this,
			Steel,
			IGNightTwo::KnockLocation,
			IGNightTwo::SteelTripleVolume,
			IGNightTwo::SteelTriplePitches[Hit],
			IGNightTwo::KnockInnerRadius,
			IGNightTwo::KnockFalloff,
			EIGAudioBus::Entity);
	}
	if (AnswerSteelHits < IGNightTwo::SteelTripleCount)
	{
		GetWorldTimerManager().SetTimer(
			AnswerKnockTimer,
			this,
			&AIGMissingFloorNightTwoBeatDirector::PlayAnswerSteelHit,
			IGNightTwo::SteelTripleSpacingSeconds,
			false);
	}
}

void AIGMissingFloorNightTwoBeatDirector::PlayAnswerGasp()
{
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->ApplyScare(IGNightTwo::AnswerGaspScare);
			Stress->PlayGasp(/*bIgnoreCooldown=*/true);
		}
	}
	// §5.5는 그녀의 숨을 남긴다. 숨은 소음 이벤트가 아니라서 여기서 직접 적는다.
	// 아침의 테이프에서 3연이 비운 2.10초 한가운데 이 숨 하나가 있다.
	if (UIGRecordingSubsystem* Recording = GetRecording())
	{
		Recording->RecordPlayerBody(IGNightTwo::AnswerGaspLoudness);
	}
}

void AIGMissingFloorNightTwoBeatDirector::AdvanceDragAway()
{
	DragElapsedSeconds += IGNightTwo::DragMoveStepSeconds;
	UAudioComponent* Loop = DragAwayLoop.Get();
	if (!Loop || DragElapsedSeconds >= IGNightTwo::DragSeconds)
	{
		GetWorldTimerManager().ClearTimer(DragMoveTimer);
		return;
	}
	const float Alpha = FMath::Clamp(
		DragElapsedSeconds / IGNightTwo::DragSeconds, 0.0f, 1.0f);
	Loop->SetWorldLocation(FMath::Lerp(
		IGNightTwo::FigureStagePoint,
		IGNightTwo::DragDepartPoint,
		Alpha));
}

void AIGMissingFloorNightTwoBeatDirector::StageFigure()
{
	AIGListenerEntity* Listener = Entity.Get();
	if (!Listener || bFigureStaged)
	{
		return;
	}
	bFigureStaged = true;
	// 문을 향해 선다. 1-4의 계단 카메오와 같은 장치이고, 같은 이유로 순찰
	// 두 점을 준다 — 서 있는 것이 아니라 기다리는 것으로 읽혀야 한다.
	Listener->SetPatrolPoints({
		IGNightTwo::FigureStagePoint,
		IGNightTwo::FigureShufflePoint,
	});
	// ParkForBeat clears his reaction to the last sound as well as moving him.
	// Without that he stands outside 403 for one frame and then crawls off toward
	// whatever he last heard, which for a beat that opens with a knock is common.
	Listener->ParkForBeat(IGNightTwo::FigureStagePoint, 90.0f);
	// 대본대로 붙든다. 순찰 AI대로 두면 대본 노크 사이에 제 3연을 끼워 넣고,
	// 문구멍을 보러 장판을 걷는 발소리에 조사로, 두 번째 걸음에 추격으로 넘어갔다.
	Listener->SetBeatHold(true);
}

void AIGMissingFloorNightTwoBeatDirector::ReleaseFigure()
{
	if (!bFigureStaged)
	{
		return;
	}
	bFigureStaged = false;
	if (AIGListenerEntity* Listener = Entity.Get())
	{
		Listener->SetBeatHold(false);
		Listener->SetPatrolPoints(CorridorPatrolPoints);
		// 연출이었지 실패가 아니다. 공격 티어는 건드리지 않는다.
		Listener->ResetToPatrolStart(/*bRaiseAggression=*/false);
	}
}

// -- 비트 2-5 「귀환 추격」 -------------------------------------------------

void AIGMissingFloorNightTwoBeatDirector::ArmReturnChase()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || Narrative->GetNightIndex() != 2)
	{
		return;
	}
	if (ReturnStage != EIGNightTwoReturnStage::Idle)
	{
		return;
	}
	ReturnStage = EIGNightTwoReturnStage::AwaitingExit;
	// 밤은 끝나지 않았다. 목표가 바뀐 것을 한 줄로 말한다. 밑장은 책상에 남아
	// 있어서 「들고」 갈 것은 없다.
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"N2ReturnThought",
			"이제 403호까지 올라가야 해."),
		4.0f);
	// 지운 민원을 읽은 몸이 식는다. 놀람이 아니라 한기라서 숨은 걸리지 않는다.
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->ApplyScare(IGNightTwo::ReturnChillScare);
		}
	}
	GetWorldTimerManager().SetTimer(
		ReturnChillTimer,
		this,
		&AIGMissingFloorNightTwoBeatDirector::PlayReturnChill,
		IGNightTwo::ReturnChillCreakSeconds,
		false);
	GetWorldTimerManager().SetTimer(
		ReturnTimer,
		this,
		&AIGMissingFloorNightTwoBeatDirector::AdvanceReturn,
		IGNightTwo::ReturnPollSeconds,
		true);
}

void AIGMissingFloorNightTwoBeatDirector::PlayReturnChill()
{
	// 아직 관리실 안일 때만. 이미 나섰으면 자재가 대신 운다.
	if (ReturnStage != EIGNightTwoReturnStage::AwaitingExit)
	{
		return;
	}
	// 건물이 낸 소리다. 그에게 알리지 않는다 — 두 번 대답하는 소리가 되는 것은
	// 나서는 순간의 자재 더미다.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateSettleTimberCreak(this),
		IGNightTwo::ReturnCreakLocation,
		0.45f,
		0.9f,
		160.0f,
		1400.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "N2ReturnCeilingCreak", "천장에서 한 번 삐걱거리는 소리"),
		2.2f,
		IGNightTwo::ReturnCreakLocation);
}

bool AIGMissingFloorNightTwoBeatDirector::IsPlayerOutsideBooth() const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!PlayerCharacter)
	{
		return false;
	}
	const FVector Where = PlayerCharacter->GetActorLocation();
	// 1층에서 관리실 남쪽 선을 넘었을 때만. 다른 층에서의 Y는 아무 의미가 없다.
	// 2층 바닥(4층의 3분의 1)부터는 1층이 아니다 — 계단이 이어진 뒤로 2층 복도의
	// Y도 이 선 남쪽이다.
	return Where.Z < IGNightTwo::FourthFloorZ / 3.0f
		&& Where.Y < IGNightTwo::BoothExitY;
}

bool AIGMissingFloorNightTwoBeatDirector::IsPlayerInsideBooth() const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!PlayerCharacter)
	{
		return false;
	}
	const FVector Where = PlayerCharacter->GetActorLocation();
	return Where.Z < IGNightTwo::FourthFloorZ / 3.0f
		&& Where.Y >= IGNightTwo::BoothExitY
		&& Where.Y <= IGNightTwo::BoothNorthY
		&& FVector::Dist2D(Where, IGNightTwo::BoothDeskLocation) <= IGNightTwo::BoothDeskReach;
}

bool AIGMissingFloorNightTwoBeatDirector::IsPlayerInsideUnit403() const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	return PlayerCharacter
		&& IGNightTwo::Unit403Interior.IsInsideOrOn(
			PlayerCharacter->GetActorLocation());
}

void AIGMissingFloorNightTwoBeatDirector::AdvanceReturn()
{
	switch (ReturnStage)
	{
	case EIGNightTwoReturnStage::AwaitingBooth:
		if (IsPlayerInsideBooth())
		{
			// 관리실에 다시 들어섰다. 처음 풀었을 때와 같은 한 줄과 한기로 건다.
			GetWorldTimerManager().ClearTimer(ReturnTimer);
			ReturnStage = EIGNightTwoReturnStage::Idle;
			ArmReturnChase();
		}
		break;

	case EIGNightTwoReturnStage::AwaitingExit:
		if (IsPlayerOutsideBooth())
		{
			PlayMaterialCollapse();
			ReturnStage = EIGNightTwoReturnStage::Chased;
		}
		break;

	case EIGNightTwoReturnStage::Chased:
		// 리셋으로 침대에 돌아온 것은 도착이 아니다. 한 번 밖으로 나가야
		// 그 빚이 청산된다.
		if (bMustLeaveHomeAgain)
		{
			if (!IsPlayerInsideUnit403())
			{
				bMustLeaveHomeAgain = false;
			}
			break;
		}
		if (IsPlayerInsideUnit403())
		{
			ReturnStage = EIGNightTwoReturnStage::Home;
			GetWorldTimerManager().ClearTimer(ReturnTimer);
			GetWorldTimerManager().ClearTimer(ReturnChillTimer);
			if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
			{
				Narrative->MarkBeatPlayed(IGNightTwo::ReturnBeatId);
			}
			OnReturnedHome.Broadcast();
		}
		break;

	default:
		GetWorldTimerManager().ClearTimer(ReturnTimer);
		break;
	}
}

void AIGMissingFloorNightTwoBeatDirector::PlayMaterialCollapse()
{
	if (bReturnChaseFired)
	{
		return;
	}
	bReturnChaseFired = true;

	auto Impact = [this](const bool bSecond)
	{
		// 판재가 바닥을 치는 소리다. 예전에는 문짝이 문틀에 닿는 저역과 잠긴
		// 손잡이 덜컹을 빌려 써서, 안쪽 방 문 83cm 옆에서 「누가 문을 닫고
		// 손잡이를 흔든다」로 들렸다. 첫 장보다 나머지가 무겁고 낮다.
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateBoardStackFall(this),
			IGNightTwo::CollapseLocation,
			IGNightTwo::CollapseVolume,
			bSecond ? IGNightTwo::CollapseSecondPitch : IGNightTwo::CollapseFirstPitch,
			IGNightTwo::CollapseInnerRadius,
			IGNightTwo::CollapseFalloff);
		// 발신자 없음. 건물이 한 일이며, 파문 HUD가 「네가 냈다」고 말해서는
		// 안 된다 — 1-5의 소화기와 같은 규칙이다. 험 마스킹으로 깎지 않는다.
		// 밸브를 열어 둔 채 나서면 책상 둘레의 물소리가 이 소리를 깎아 한 번뿐인
		// 추격이 조용히 사라질 수 있었다. 험이 삼키는 것은 조용한 소리다.
		if (UIGNoiseSubsystem* Noise = GetNoise())
		{
			Noise->ReportNoiseUnmasked(
				IGNightTwo::CollapseLocation,
				IGNightTwo::CollapseLoudness,
				nullptr);
		}
	};

	Impact(/*bSecond=*/false);
	// 나머지가 무너지는 두 번째 소리. 이 한 번이 조사를 추격으로 바꾼다.
	FTimerDelegate SecondImpact;
	SecondImpact.BindLambda([this, Impact]() { Impact(true); });
	GetWorldTimerManager().SetTimer(
		CollapseTimer,
		SecondImpact,
		IGNightTwo::CollapseSecondImpactSeconds,
		false);
	// 다 쓰러진 판재 모서리에서 가루가 떨어진다. 가장 가는 소리라 무너진 뒤에
	// 남는다.
	GetWorldTimerManager().SetTimer(
		CollapseDustTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreatePlasterDustFall(this),
				IGNightTwo::CollapseLocation + FVector(0.0f, 0.0f, 90.0f),
				IGNightTwo::CollapseDustVolume,
				1.0f,
				120.0f,
				900.0f,
				EIGAudioBus::World);
		}),
		IGNightTwo::CollapseSecondImpactSeconds + IGNightTwo::CollapseDustDelaySeconds,
		false);

	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "N2CollapseCaption", "자재 무너지는 소리"),
		2.4f,
		IGNightTwo::CollapseLocation);
	// §18.6 낙하물·충돌. 건물이 무너뜨린 것이지 그녀가 낸 소리가 아니라서
	// 손에는 한 번만 온다.
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		PlayerCharacter->PlayImpactHaptic();
	}
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->ApplyScare(ChaseScareAmount);
		}
	}
}

void AIGMissingFloorNightTwoBeatDirector::NotifyCaptureReset()
{
	if (ReturnStage == EIGNightTwoReturnStage::Chased)
	{
		bMustLeaveHomeAgain = true;
	}
}

void AIGMissingFloorNightTwoBeatDirector::HandlePeepholeExamined(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->MarkBeatPlayed(IGNightTwo::PeepholeBeatId))
	{
		return;
	}
	bPeepholeSeen = true;
	// 문구멍 덮개를 민다. 문짝에 붙은 작은 금속이라 그녀 쪽에서만 들린다.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateSwitchClick(this, true),
		IGNightTwo::PeepholeLocation,
		0.22f,
		1.5f,
		30.0f,
		250.0f,
		EIGAudioBus::Player);
	// 밤의 복도에서 살아 있는 등은 계단 쪽 끝의 죽어 가는 것 하나다. 그는 기는
	// 몸이라 선 사람의 눈높이에는 없다. 보이는 것만 말한다. 점멸 감소를 켠
	// 사람에게는 그 등이 떨지 않으므로 「깜박인다」고 하지 않는다.
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"N2PeepholeThought1",
			"캄캄하네. 복도 끝 등 하나만 켜져 있어."),
		3.0f);
	GetWorldTimerManager().SetTimer(
		PeepholeTimer,
		this,
		&AIGMissingFloorNightTwoBeatDirector::PlayPeepholeAftermath,
		IGNightTwo::PeepholeCrackDelaySeconds,
		false);
}

void AIGMissingFloorNightTwoBeatDirector::PlayPeepholeAftermath()
{
	const UIGRecordingSubsystem* Recording = GetRecording();
	const bool bArmed = Recording && Recording->IsRecording();
	const bool bBeforeAnswer = Stage == EIGNightTwoBeatStage::AwaitingPeephole
		|| Stage == EIGNightTwoBeatStage::AwaitingPhone;
	if (!bBeforeAnswer || bArmed)
	{
		// 폰은 이미 돌고 있거나 3연이 시작됐다. 여기서 더할 것이 없다.
		return;
	}
	// 그가 문 앞 자리에 실제로 있을 때만 갈라진다. 없는 자리에서 그의 소리를
	// 내면 소리가 거짓말을 한다.
	const AIGListenerEntity* Listener = Entity.Get();
	if (bFigureStaged
		&& Listener
		&& !Listener->IsDormant()
		&& FVector::Dist2D(Listener->GetActorLocation(), IGNightTwo::FigureStagePoint)
			<= IGNightTwo::PeepholeCrackReach)
	{
		const FVector CrackAt = Listener->GetActorLocation() + FVector(0.0f, 0.0f, 20.0f);
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreatePlasterSettle(this),
			CrackAt,
			0.6f,
			1.0f,
			60.0f,
			700.0f,
			EIGAudioBus::Entity);
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "N2PeepholeCrackCaption", "문 바로 밖에서 벽 갈라지는 소리"),
			2.2f,
			CrackAt);
		if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
		{
			if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
			{
				Stress->ApplyScare(IGNightTwo::PeepholeCrackScare);
			}
			PlayerCharacter->PlayScareKick(0.6f);
		}
	}
	// 이 줄이 폰을 가리킨다. 설계서의 계기는 「증거를 만들 생각」이고,
	// 프롬프트를 새로 띄우는 대신 그녀가 스스로 그 생각에 도달하게 둔다.
	// 폰을 가리키는 말은 한 번이다 — 12초 뒤의 재촉은 이제 필요 없다. 문구멍을
	// 늦게 본 사람은 그 재촉을 이미 들었으니 같은 말을 또 하지 않는다.
	if (!bPhoneNudged)
	{
		bPhoneNudged = true;
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"N2PeepholeThought2",
				"폰 녹음이라도 켜 두자."),
			3.4f);
	}
}

void AIGMissingFloorNightTwoBeatDirector::AdvanceForTesting()
{
	// 프로브가 인내 시간을 기다리지 않게 해 준다. 단계를 건너뛰는 것이 아니라
	// 매번 현재 단계의 시계를 만료시키고 정상 경로를 호출하므로, 순서와 부수
	// 효과는 실제 재생과 동일하다. 상한은 단계 수보다 넉넉하게 잡되 무한이
	// 되지 않게 둔다 — 진행하지 못하는 상태를 조용히 도는 것보다 프로브가
	// 그것을 보고 실패하는 편이 낫다.
	constexpr int32 MaximumSteps = 12;
	const float FarPast =
		IGNightTwo::PeepholePatienceSeconds
		+ IGNightTwo::PhonePatienceSeconds
		+ IGNightTwo::TripleDelaySeconds
		+ IGNightTwo::WaitAfterTripleSeconds
		+ IGNightTwo::DragSeconds;
	for (int32 Step = 0; Step < MaximumSteps; ++Step)
	{
		if (Stage == EIGNightTwoBeatStage::Spent
			|| Stage == EIGNightTwoBeatStage::Idle)
		{
			return;
		}
		StageSeconds = FarPast;
		AdvanceStage();
	}
}
