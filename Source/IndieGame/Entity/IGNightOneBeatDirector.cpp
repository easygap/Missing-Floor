#include "Entity/IGNightOneBeatDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGNoiseSubsystem.h"
#include "GameFramework/Controller.h"
#include "Interaction/IGZoneTrigger.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"
#include "TimerManager.h"

namespace IGNightOne
{
	// Beat ids in the persistent night state: once played, never replayed.
	const FName SightingBeatId(TEXT("Night1.Sighting"));
	const FName ExtinguisherBeatId(TEXT("Night1.Extinguisher"));
	const FName Unit402KnockBeatId(TEXT("Night1.Unit402Knock"));

	/** 402호 문 앞 복도. 문은 X -30, 복도 면 Y -235. */
	const FVector Unit402KnockZoneCenter(-30.0f, -300.0f, 1010.0f);
	const FVector Unit402KnockZoneExtent(70.0f, 62.0f, 110.0f);
	/** 노크는 문 안쪽 40 cm에서 난다. 문짝이 먹은 소리다. */
	const FVector Unit402KnockSource(-30.0f, -200.0f, 1000.0f);
	constexpr float Unit402SecondKnockSeconds = 0.74f;

	/**
	 * The stair-throat trigger, at the 4F mouth of the down flight. The
	 * half-landing itself is not visible from here — the shaft turn hides it —
	 * so staging on entry never moves anything inside the player's view.
	 */
	const FVector SightingZoneCenter(-300.0f, -305.0f, 1010.0f);
	const FVector SightingZoneExtent(45.0f, 62.0f, 110.0f);

	/**
	 * 그가 서는 자리. 4층과 3층 사이 반 층 참(Z 750)의 북서쪽 구석, 북쪽 벽에
	 * 귀를 대고 내려오는 사람에게 등을 보인다. 4층에서 내려오는 동쪽 띠에서는
	 * 두 띠 사이 벽에 가려 보이지 않다가, 참에 내려서서 서쪽으로 돌아야 보인다.
	 * 계속 내려가려면 그 곁 1 m를 지나야 한다. 참은 깊이 140 cm라 몸이 닿지는
	 * 않는다 — 그의 몸은 북쪽 벽에서 10 cm 떨어진 데까지, 지나가는 사람은 남쪽
	 * 가장자리를 밟는다. 높이는 참 윗면에 그의 몸 반높이를 더한 것이다.
	 */
	const FVector SightingStagePoint(-528.0f, 52.0f, 808.0f);
	const FVector SightingShufflePoint(-496.0f, 52.0f, 808.0f);
	/**
	 * 그를 지나 내려갔다고 보는 선. 반 층 참에서 서쪽 띠로 두 단쯤 내려선
	 * 높이(몸 중심 Z 790)이고, 계단탑 안이어야 한다. 프로브는 이 아래 단에 세운다.
	 */
	constexpr float SightingDescentZ = 790.0f;
	const FVector SightingPassPoint(-522.0f, -147.5f, 765.0f);
	constexpr float SightingDescentPollSeconds = 0.2f;

	/** Give up on the cameo if the player retreats and never descends. */
	constexpr float SightingFallbackSeconds = 45.0f;

	/**
	 * 그를 계단참에 올리는 조건. 시야 원뿔 밖이거나 벽 뒤라 그려지지 않았고,
	 * 플레이어 곁이 아니어야 한다. 곁에서 옮기면 숨과 끌림이 귀 옆에서 끊긴다.
	 */
	constexpr float SightingRetrySeconds = 0.5f;
	constexpr float SightingOffscreenDot = 0.3f;
	constexpr float SightingMinimumDistance = 300.0f;
	/** 다시 볼 때는 플레이어가 아직 계단 입구에 있어야 한다(수평 250cm, 같은 층). */
	constexpr float SightingRetryReach = 250.0f;
	constexpr float SightingRetryHeight = 110.0f;
	/** 폴백은 복도 첫 칸이 플레이어에게서 이만큼 떨어져 있을 때만 그를 돌려놓는다. */
	constexpr float FallbackClearance = 450.0f;
	constexpr float FallbackRetrySeconds = 3.0f;

	/** In front of the fire cabinet, spanning the corridor walkway. */
	const FVector ExtinguisherZoneCenter(232.0f, -320.0f, 1010.0f);
	const FVector ExtinguisherZoneExtent(60.0f, 55.0f, 110.0f);

	/** The fall takes about this long to reach tile from the bracket. */
	constexpr float ImpactDelaySeconds = 0.45f;
	/** §5.1: a dropped prop is a 0.6 — the whole corridor hears it. */
	constexpr float ImpactLoudness = 0.6f;

	/**
	 * The distribution board's hum pocket, the escape the beat teaches.
	 * Placed at the panel's corridor face, low enough that a player standing
	 * in front of it is inside the radius.
	 */
	const FVector BreakerPanelHumLocation(-90.0f, -245.0f, 1000.0f);
	constexpr float BreakerPanelHumRadius = 200.0f;
	constexpr float BreakerPanelHumMasking = 0.2f;
}

AIGNightOneBeatDirector::AIGNightOneBeatDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

FVector AIGNightOneBeatDirector::GetSightingZoneCenter()
{
	return IGNightOne::SightingZoneCenter;
}

FVector AIGNightOneBeatDirector::GetSightingStagePoint()
{
	return IGNightOne::SightingStagePoint;
}

FVector AIGNightOneBeatDirector::GetSightingShufflePoint()
{
	return IGNightOne::SightingShufflePoint;
}

FVector AIGNightOneBeatDirector::GetSightingPassPoint()
{
	return IGNightOne::SightingPassPoint;
}

bool AIGNightOneBeatDirector::Configure(
	AIGPrologueWorldScene* InScene,
	AIGListenerEntity* InEntity,
	AIGPlayerCharacter* InPlayer,
	const TArray<FVector>& InCorridorPatrolPoints)
{
	UWorld* World = GetWorld();
	if (!World || !InScene || !InEntity || !InPlayer)
	{
		return false;
	}
	Scene = InScene;
	Entity = InEntity;
	Player = InPlayer;
	CorridorPatrolPoints = InCorridorPatrolPoints;

	// Second hum pocket after the fridge: the corridor breaker panel. Owned
	// here because it is this beat's escape hatch; released on teardown.
	if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
	{
		BreakerPanelHumHandle = Noise->RegisterHumSource(
			IGNightOne::BreakerPanelHumLocation,
			IGNightOne::BreakerPanelHumRadius,
			IGNightOne::BreakerPanelHumMasking);
		// 이 비트의 탈출구는 귀로 찾는 것이다. 배전반이 조용하면 플레이어는
		// 여기 설 이유를 붙잡히고 나서야 안다.
		BreakerPanelHumLoop = IGAudio::SpawnHumLoopAt(
			this,
			TEXT("NightOneBreakerHum"),
			IGNightOne::BreakerPanelHumLocation,
			IGNightOne::BreakerPanelHumRadius);
	}

	// Both zones deliberately carry no story tag: the tag would self-consume
	// on a save load and silently disarm the beats. Once-per-run bookkeeping
	// lives in the narrative snapshot instead.
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnParameters.Name = TEXT("Night1SightingZone");
	SightingZone = World->SpawnActor<AIGZoneTrigger>(
		AIGZoneTrigger::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGNightOne::SightingZoneCenter),
		SpawnParameters);
	if (!SightingZone)
	{
		return false;
	}
	SightingZone->SetZoneExtent(IGNightOne::SightingZoneExtent);
	SightingZone->RequiredNightIndex = 1;
	// 세 존 모두 그 시간에만 발동한다. 밤1 뒤의 낮에도 밤 번호는 1이다.
	SightingZone->bRequireSealedHour = true;
	SightingZone->OnZoneTriggered.AddDynamic(
		this, &AIGNightOneBeatDirector::HandleSightingZone);

	SpawnParameters.Name = TEXT("Night1ExtinguisherZone");
	ExtinguisherZone = World->SpawnActor<AIGZoneTrigger>(
		AIGZoneTrigger::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGNightOne::ExtinguisherZoneCenter),
		SpawnParameters);
	if (!ExtinguisherZone)
	{
		return false;
	}
	ExtinguisherZone->SetZoneExtent(IGNightOne::ExtinguisherZoneExtent);
	ExtinguisherZone->RequiredNightIndex = 1;
	ExtinguisherZone->bRequireSealedHour = true;
	ExtinguisherZone->OnZoneTriggered.AddDynamic(
		this, &AIGNightOneBeatDirector::HandleExtinguisherZone);

	SpawnParameters.Name = TEXT("Night1Unit402KnockZone");
	Unit402KnockZone = World->SpawnActor<AIGZoneTrigger>(
		AIGZoneTrigger::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGNightOne::Unit402KnockZoneCenter),
		SpawnParameters);
	if (Unit402KnockZone)
	{
		Unit402KnockZone->SetZoneExtent(IGNightOne::Unit402KnockZoneExtent);
		Unit402KnockZone->RequiredNightIndex = 1;
		Unit402KnockZone->RequiredNarrativeBeat = IGNightOne::ExtinguisherBeatId;
		Unit402KnockZone->bRequireSealedHour = true;
		Unit402KnockZone->OnZoneTriggered.AddDynamic(
			this, &AIGNightOneBeatDirector::HandleUnit402KnockZone);
	}

	return true;
}

void AIGNightOneBeatDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(SightingFallbackTimer);
	GetWorldTimerManager().ClearTimer(SightingRetryTimer);
	GetWorldTimerManager().ClearTimer(ImpactTimer);
	GetWorldTimerManager().ClearTimer(Unit402KnockTimer);
	GetWorldTimerManager().ClearTimer(SightingStepTimer);
	GetWorldTimerManager().ClearTimer(SightingDescentTimer);
	GetWorldTimerManager().ClearTimer(FixtureDeathTimer);
	if (BreakerPanelHumHandle != 0)
	{
		if (UWorld* World = GetWorld())
		{
			if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
			{
				Noise->UnregisterHumSource(BreakerPanelHumHandle);
			}
		}
		BreakerPanelHumHandle = 0;
	}
	if (BreakerPanelHumLoop)
	{
		BreakerPanelHumLoop->Stop();
		BreakerPanelHumLoop->DestroyComponent();
		BreakerPanelHumLoop = nullptr;
	}
	if (bSightingStaged && !bSightingCompleted)
	{
		RestoreSightingEntity();
	}
	Super::EndPlay(EndPlayReason);
}

// -- 1-4 first sighting ----------------------------------------------------

void AIGNightOneBeatDirector::HandleSightingZone(AIGZoneTrigger* Zone)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->MarkBeatPlayed(IGNightOne::SightingBeatId))
	{
		return;
	}
	// 카메오는 그녀가 실제로 그를 지나 내려가는 순간 끝난다. 계단이 이어진 뒤로는
	// 그 순간을 알려 줄 순간이동이 없으니 자리로 본다.
	GetWorldTimerManager().SetTimer(
		SightingDescentTimer,
		this,
		&AIGNightOneBeatDirector::PollSightingDescent,
		IGNightOne::SightingDescentPollSeconds,
		true);
	TryStageSighting();
}

bool AIGNightOneBeatDirector::IsScriptedRun()
{
	return FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreyboxProbe"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGNightCapture"));
}

void AIGNightOneBeatDirector::TryStageSighting()
{
	if (bSightingStaged || bSightingCompleted)
	{
		GetWorldTimerManager().ClearTimer(SightingRetryTimer);
		return;
	}
	if (IsScriptedRun())
	{
		StageSighting();
		return;
	}
	// 새벽이 먼저 오면 카메오는 접는다. 폴백이 돌려놓을 것도 없다.
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || Narrative->GetNightIndex() != 1 || !Narrative->IsHourSealed())
	{
		GetWorldTimerManager().ClearTimer(SightingRetryTimer);
		return;
	}
	if (CanStageSightingUnseen())
	{
		GetWorldTimerManager().ClearTimer(SightingRetryTimer);
		StageSighting();
		return;
	}
	// 기다리는 동안은 아무 소리도 따로 내지 않는다. 그가 없는 자리에서 나는
	// 소리는 이 게임의 귀 규칙을 거스른다.
	if (!GetWorldTimerManager().IsTimerActive(SightingRetryTimer))
	{
		GetWorldTimerManager().SetTimer(
			SightingRetryTimer,
			this,
			&AIGNightOneBeatDirector::TryStageSighting,
			IGNightOne::SightingRetrySeconds,
			true);
	}
}

bool AIGNightOneBeatDirector::CanStageSightingUnseen() const
{
	const AIGListenerEntity* Listener = Entity.Get();
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!Listener || !PlayerCharacter || Listener->IsDormant())
	{
		return false;
	}
	// 소리를 좇는 중이면 데려오지 않는다. 그가 향하던 소리가 거짓이 된다.
	const EIGListenerState State = Listener->GetListenerState();
	if (State != EIGListenerState::Patrolling
		&& State != EIGListenerState::Banging
		&& State != EIGListenerState::Listening)
	{
		return false;
	}
	const FVector PlayerLocation = PlayerCharacter->GetActorLocation();
	if (FVector::Dist2D(PlayerLocation, IGNightOne::SightingZoneCenter)
			> IGNightOne::SightingRetryReach
		|| FMath::Abs(PlayerLocation.Z - IGNightOne::SightingZoneCenter.Z)
			> IGNightOne::SightingRetryHeight)
	{
		return false;
	}
	const FVector ListenerLocation = Listener->GetActorLocation();
	if (FVector::Dist(ListenerLocation, PlayerLocation) < IGNightOne::SightingMinimumDistance)
	{
		return false;
	}
	return !IsInPlayerView(ListenerLocation, Listener);
}

bool AIGNightOneBeatDirector::IsInPlayerView(
	const FVector& Location,
	const AActor* Subject) const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	UWorld* World = GetWorld();
	if (!PlayerCharacter || !World)
	{
		return false;
	}
	FVector ViewLocation = PlayerCharacter->GetPawnViewLocation();
	FRotator ViewRotation = PlayerCharacter->GetControlRotation();
	if (const AController* Controller = PlayerCharacter->GetController())
	{
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	const FVector ToLocation = (Location - ViewLocation).GetSafeNormal();
	if (FVector::DotProduct(ViewRotation.Vector(), ToLocation)
		< IGNightOne::SightingOffscreenDot)
	{
		return false;
	}
	if (Subject)
	{
		// 원뿔 안이어도 벽 뒤라 그려지지 않았으면 보이지 않은 것이다.
		return Subject->WasRecentlyRendered(0.2f);
	}
	// 빈 자리는 몸이 없으니 벽이 가리는지 직접 본다.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NightOneSightingView), false, PlayerCharacter);
	if (const AIGListenerEntity* Listener = Entity.Get())
	{
		Params.AddIgnoredActor(Listener);
	}
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(
		Hit, ViewLocation, Location, ECC_Visibility, Params);
}

void AIGNightOneBeatDirector::StageSighting()
{
	AIGListenerEntity* Listener = Entity.Get();
	AIGPrologueWorldScene* WorldScene = Scene.Get();
	if (!Listener || !WorldScene)
	{
		return;
	}

	bSightingStaged = true;
	// The dying west fixture flickers right over the stair throat; a staged
	// reveal must not fight it.
	WorldScene->SuspendCorridorFlicker(true);

	// 북쪽 벽을 보고 귀를 댄다 — 내려오는 사람에게 등을 보인다. 순찰 두 점은
	// 같은 벽을 따라 참 위에서만 움직이게 하고, 참 가장자리 밖은 그의 기는
	// 걸음이 넘지 못한다(CrawlTowards).
	// 지나가는 사람이 팔 길이 안을 스친다. 소리를 내지 않으면 그는 모른다는 것이
	// 이 비트가 가르치는 규칙이라, 쫓기 전에는 닿기만으로 잡지 않게 한다.
	Listener->TeleportTo(
		IGNightOne::SightingStagePoint,
		FRotator(0.0f, 90.0f, 0.0f),
		false,
		true);
	Listener->SetTouchCaptureSuppressed(true);
	Listener->SetPatrolPoints({
		IGNightOne::SightingStagePoint,
		IGNightOne::SightingShufflePoint,
	});

	// 형체가 있기 전에 소리가 있어야 한다. 계단참에서 기는 걸음 둘, 그리고
	// 계단 입구의 등이 죽는다 — 내려가면 통로 끝이 실루엣이 된다. 첫 목격이
	// 소리 없는 텔레포트였다.
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleVariantOr(
			TEXT("Entity_CrawlStep"), 3, 0x51u,
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateEntityCrawlStep(this, false); }),
		IGNightOne::SightingStagePoint + FVector(0.0f, 0.0f, 20.0f),
		0.7f,
		1.0f,
		200.0f,
		1600.0f,
		EIGAudioBus::Entity);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "SightingCrawlCaption", "기는 소리"),
		2.0f,
		IGNightOne::SightingStagePoint);
	GetWorldTimerManager().SetTimer(
		SightingStepTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			IGAudio::SpawnOneShotAt(
				this,
				IGAudio::SampleVariantOr(
					TEXT("Entity_CrawlStep"), 3, 0x9Bu,
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateEntityCrawlStep(this, false); }),
				IGNightOne::SightingShufflePoint + FVector(0.0f, 0.0f, 20.0f),
				0.6f,
				1.0f,
				200.0f,
				1600.0f,
				EIGAudioBus::Entity);
		}),
		0.85f,
		false);
	SightingThroatFixture = INDEX_NONE;
	float NearestSquared = FMath::Square(260.0f);
	for (int32 Index = 0; Index < WorldScene->GetCorridorFixtureCount(); ++Index)
	{
		const float DistanceSquared = FVector::DistSquared2D(
			WorldScene->GetCorridorFixtureLocation(Index),
			IGNightOne::SightingZoneCenter);
		if (DistanceSquared < NearestSquared)
		{
			NearestSquared = DistanceSquared;
			SightingThroatFixture = Index;
		}
	}
	if (SightingThroatFixture != INDEX_NONE)
	{
		WorldScene->SetFixtureLive(SightingThroatFixture, false, true);
	}

	GetWorldTimerManager().SetTimer(
		SightingFallbackTimer,
		this,
		&AIGNightOneBeatDirector::TryFallbackRestore,
		IGNightOne::SightingFallbackSeconds,
		false);
}

bool AIGNightOneBeatDirector::HasPlayerDescendedPastLanding() const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!PlayerCharacter)
	{
		return false;
	}
	// 계단탑 안에서 반 층 참보다 두 단 아래.
	const FVector Where = PlayerCharacter->GetActorLocation();
	return AIGPrologueWorldScene::IsInsideStairCore(Where)
		&& Where.Z < IGNightOne::SightingDescentZ;
}

void AIGNightOneBeatDirector::PollSightingDescent()
{
	if (bSightingCompleted)
	{
		GetWorldTimerManager().ClearTimer(SightingDescentTimer);
		return;
	}
	if (!HasPlayerDescendedPastLanding())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(SightingDescentTimer);
	// 그를 옮기지 못한 채 계단을 내려갔다. 이 밤의 첫 목격은 없던 일이다.
	if (!bSightingStaged)
	{
		GetWorldTimerManager().ClearTimer(SightingRetryTimer);
		bSightingCompleted = true;
		return;
	}
	// 내려가며 그를 지나치는 순간, 벽에 귀를 댄 그의 껍질이 한 번 갈라진다.
	// 감각 규칙 1(§4.3 「눈앞을 지나가도 소리가 없으면 모른다」)을 가르치는
	// 자리라 「들었다」의 들숨은 내지 않는다. 조용히 지나간 사람에게 그 숨을
	// 들려주면 조용해도 들킨다고 거꾸로 배운다. 정말 소리를 냈다면 그의 청각이
	// 이미 조사로 넘어가며 제 들숨을 냈다.
	if (AIGListenerEntity* Listener = Entity.Get())
	{
		const EIGListenerState State = Listener->GetListenerState();
		const bool bAlreadyHeard = State == EIGListenerState::Investigating
			|| State == EIGListenerState::Holding
			|| State == EIGListenerState::Chasing
			|| State == EIGListenerState::Searching;
		if (!bAlreadyHeard)
		{
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreatePlasterSettle(this),
				Listener->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f),
				0.8f,
				0.96f,
				200.0f,
				1600.0f,
				EIGAudioBus::Entity);
			AIGHorrorHUD::PushAudioCaptionAt(
				this,
				NSLOCTEXT("IGMissingFloor", "SightingSettleCaption", "벽에 금 가는 소리"),
				2.0f,
				Listener->GetActorLocation());
		}
		AIGHorrorHUD::PushFearDirection(this, Listener->GetActorLocation());
	}
	// 놀람은 그녀의 몫이다(§8 1-4 【S】0.55). 들숨 대신 가는 균열음이라 몸의
	// 움찔도 그만큼 작다.
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->ApplyScare(0.5f);
		}
		PlayerCharacter->PlayScareKick(1.0f);
	}
	RestoreSightingEntity();
}

void AIGNightOneBeatDirector::TryFallbackRestore()
{
	if (!bSightingStaged || bSightingCompleted)
	{
		return;
	}
	if (!IsScriptedRun() && !CanRestoreSightingUnseen())
	{
		GetWorldTimerManager().SetTimer(
			SightingFallbackTimer,
			this,
			&AIGNightOneBeatDirector::TryFallbackRestore,
			IGNightOne::FallbackRetrySeconds,
			false);
		return;
	}
	RestoreSightingEntity();
}

bool AIGNightOneBeatDirector::CanRestoreSightingUnseen() const
{
	const AIGListenerEntity* Listener = Entity.Get();
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	// 낮이면 그는 잠들어 보이지 않는다. 옮길 몸도 볼 사람도 없다.
	if (!Listener || !PlayerCharacter || Listener->IsDormant())
	{
		return true;
	}
	const EIGListenerState State = Listener->GetListenerState();
	if (State == EIGListenerState::Chasing
		|| State == EIGListenerState::CaptureHold
		|| State == EIGListenerState::FinaleLured)
	{
		return false;
	}
	if (CorridorPatrolPoints.Num() > 0)
	{
		// 계단 입구에서 지켜보던 플레이어 곁에 불이 켜지며 그가 나타나면 안 된다.
		const FVector ReturnPoint = CorridorPatrolPoints[0];
		const FVector PlayerLocation = PlayerCharacter->GetActorLocation();
		if (FMath::Abs(PlayerLocation.Z - ReturnPoint.Z) < 200.0f
			&& FVector::Dist2D(PlayerLocation, ReturnPoint) < IGNightOne::FallbackClearance)
		{
			return false;
		}
		if (IsInPlayerView(ReturnPoint, nullptr))
		{
			return false;
		}
	}
	return !IsInPlayerView(Listener->GetActorLocation(), Listener);
}

void AIGNightOneBeatDirector::RestoreSightingEntity()
{
	if (!bSightingStaged || bSightingCompleted)
	{
		return;
	}
	bSightingCompleted = true;
	GetWorldTimerManager().ClearTimer(SightingFallbackTimer);
	GetWorldTimerManager().ClearTimer(SightingDescentTimer);

	if (AIGListenerEntity* Listener = Entity.Get())
	{
		Listener->SetTouchCaptureSuppressed(false);
		Listener->SetPatrolPoints(CorridorPatrolPoints);
		// 공격 티어는 건드리지 않는다. 카메오는 연출이지 실패가 아니다. 순찰
		// 처음으로 되감지도 않는다 — 밤 한가운데 먼지 흔적과 발자국이 지워지면
		// 시간이 되감긴 것처럼 보인다. 자고 있으면 다음 밤이 제자리에 세운다.
		if (!Listener->IsDormant())
		{
			if (CorridorPatrolPoints.Num() > 0)
			{
				Listener->ParkForBeat(CorridorPatrolPoints[0], 0.0f);
			}
			else
			{
				Listener->ResetToPatrolStart(/*bRaiseAggression=*/false);
			}
		}
	}
	if (AIGPrologueWorldScene* WorldScene = Scene.Get())
	{
		if (SightingThroatFixture != INDEX_NONE)
		{
			WorldScene->SetFixtureLive(SightingThroatFixture, true, true);
			SightingThroatFixture = INDEX_NONE;
		}
		WorldScene->SuspendCorridorFlicker(false);
	}
}

// -- 1-6 402호의 노크 ------------------------------------------------------

void AIGNightOneBeatDirector::HandleUnit402KnockZone(AIGZoneTrigger* Zone)
{
	// 소화기 이후에 돌아올 때 들린다. 먼저 문 앞을 지나도 트리거가 소모되지 않는다.
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || Narrative->GetNightIndex() != 1
		|| !Narrative->HasBeatPlayed(IGNightOne::ExtinguisherBeatId)
		|| !Narrative->MarkBeatPlayed(IGNightOne::Unit402KnockBeatId))
	{
		return;
	}
	const auto Knock = [this]()
	{
		IGAudio::SpawnOneShotAt(
			this,
			IGAudio::SampleVariantOr(
				TEXT("Knock_Plaster"), 3, static_cast<uint32>(GetWorld()->GetTimeSeconds() * 977.0f) * 2654435761u,
				[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.6f); }),
			IGNightOne::Unit402KnockSource,
			0.7f,
			0.78f,
			140.0f,
			1100.0f,
			EIGAudioBus::World);
	};
	Knock();
	// 빈집 안에서 난 첫 노크에 몸이 먼저 굳는다. 0.4 아래라 헐떡이지는 않고
	// 움찔로 끝난다. 소화기 직후라 더 올리면 심박이 소리로 샌다.
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->ApplyScare(0.22f);
		}
		PlayerCharacter->PlayScareKick(0.5f);
	}
	GetWorldTimerManager().SetTimer(
		Unit402KnockTimer,
		this,
		&AIGNightOneBeatDirector::PlayUnit402SecondKnock,
		IGNightOne::Unit402SecondKnockSeconds,
		false);
	AIGHorrorHUD::PushFearDirection(this, IGNightOne::Unit402KnockSource);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "Unit402KnockCaption", "402호 안에서 두 번 두드리는 소리"),
		2.2f,
		IGNightOne::Unit402KnockSource);
}

void AIGNightOneBeatDirector::PlayUnit402SecondKnock()
{
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleVariantOr(
			TEXT("Knock_Plaster"), 3, 0x3Fu,
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.6f); }),
		IGNightOne::Unit402KnockSource,
		0.62f,
		0.74f,
		140.0f,
		1100.0f,
		EIGAudioBus::World);
}

// -- 1-5 forced encounter --------------------------------------------------

void AIGNightOneBeatDirector::HandleExtinguisherZone(AIGZoneTrigger* Zone)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative
		|| !Narrative->MarkBeatPlayed(IGNightOne::ExtinguisherBeatId))
	{
		return;
	}

	AIGPrologueWorldScene* WorldScene = Scene.Get();
	if (!WorldScene || !WorldScene->DropCorridorExtinguisher())
	{
		return;
	}
	bExtinguisherBeatFired = true;

	// The clang and the noise report belong to the impact, not the release:
	// the bracket lets go silently and the floor answers half a second later.
	GetWorldTimerManager().SetTimer(
		ImpactTimer,
		this,
		&AIGNightOneBeatDirector::PlayExtinguisherImpact,
		IGNightOne::ImpactDelaySeconds,
		false);
}

void AIGNightOneBeatDirector::PlayExtinguisherImpact()
{
	UWorld* World = GetWorld();
	AIGPrologueWorldScene* WorldScene = Scene.Get();
	if (!World || !WorldScene)
	{
		return;
	}
	const FVector Impact = WorldScene->GetCorridorExtinguisherLocation();

	// A steel cylinder on granite tile: the low body thud and the thin
	// rattling ring, composed from the existing factories.
	if (USoundBase* Drop = IGAudio::Sample(TEXT("Extinguisher_Drop")))
	{
		IGAudio::SpawnOneShotAt(this, Drop, Impact, 1.0f, 1.0f, 200.0f, 2400.0f, EIGAudioBus::World);
	}
	else
	{
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateDoorThud(this),
			Impact,
			1.0f,
			0.82f);
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateLockedRattle(this),
			Impact,
			0.8f,
			0.68f);
	}

	// No instigator: the building did this, and the ripple HUD must not tell
	// the player "you made that sound".
	if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
	{
		Noise->ReportNoise(Impact, IGNightOne::ImpactLoudness, nullptr);
	}
	// 쇠통이 떨어지는데 몸이 가만히 있을 수는 없다. 그리고 2.2초 뒤, 등 뒤의
	// 등이 죽는다 — 시야 밖에서 꺼지는 조명(STORY_DIRECTION §7 허용).
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->ApplyScare(0.35f);
		}
		PlayerCharacter->PlayScareKick(1.2f);
	}
	AIGHorrorHUD::PushFearDirection(this, Impact);
	if (!bFixtureDeathFired)
	{
		GetWorldTimerManager().SetTimer(
			FixtureDeathTimer,
			this,
			&AIGNightOneBeatDirector::KillFixtureBehindPlayer,
			2.2f,
			false);
	}
}

void AIGNightOneBeatDirector::KillFixtureBehindPlayer()
{
	if (bFixtureDeathFired)
	{
		return;
	}
	AIGPrologueWorldScene* WorldScene = Scene.Get();
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!WorldScene || !PlayerCharacter)
	{
		return;
	}
	// 등 뒤, 가장 가까운 등. 시야 안의 등을 죽이면 「원인이 보이는 놀람」이 된다.
	const FVector PlayerLocation = PlayerCharacter->GetActorLocation();
	const FVector View = PlayerCharacter->GetControlRotation().Vector().GetSafeNormal2D();
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < WorldScene->GetCorridorFixtureCount(); ++Index)
	{
		const FVector Fixture = WorldScene->GetCorridorFixtureLocation(Index);
		FVector ToFixture = Fixture - PlayerLocation;
		ToFixture.Z = 0.0f;
		const float Distance = ToFixture.Size();
		if (Distance < 60.0f || Distance > 900.0f
			|| FVector::DotProduct(View, ToFixture / Distance) > 0.15f)
		{
			continue;
		}
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	if (Best == INDEX_NONE)
	{
		return;
	}
	bFixtureDeathFired = true;
	const FVector FixtureLocation = WorldScene->GetCorridorFixtureLocation(Best);
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Ballast_Tick"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateFluorescentBallastSnap(this); }),
		FixtureLocation,
		1.0f,
		1.0f,
		200.0f,
		1600.0f,
		EIGAudioBus::World);
	WorldScene->SetFixtureLive(Best, false, true);
	AIGHorrorHUD::PushFearDirection(this, FixtureLocation);
	if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
	{
		Stress->ApplyScare(0.30f);
	}
	PlayerCharacter->PlayScareKick(0.7f);
}

UIGMissingFloorNarrativeSubsystem* AIGNightOneBeatDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
}
