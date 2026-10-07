#include "Entity/IGListenerEntity.h"

#include "IndieGame.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/IGDustSubsystem.h"
#include "Interaction/IGFireDoorWedge.h"
#include "Interaction/IGHidingSpot.h"
#include "Interaction/IGSwingDoor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGRecordingSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Player/IGStressComponent.h"
#include "TimerManager.h"

namespace IGListener
{
	// §4. 숨은 자리 안의 소리는 이 거리 안에서 들었을 때만 그 자리를 연다. 그 밖에서
	// 들은 소리는 평소처럼 그 자리로 찾아오게 할 뿐이다.
	constexpr float HiddenDetectRadius = 220.0f;
	// 숨은 소리를 들은 뒤 이만큼 안에 손이 닿으면 끌어낸다. 그 뒤로 숨죽이고 있으면
	// 그는 자리 앞에서 듣다가 떠난다.
	constexpr double HiddenDetectMemorySeconds = 2.0;
	// 숨은 자리 앞에서 손이 닿는 거리. 가구에 막혀 몸이 더 들어오지 못하므로 수평으로 잰다.
	constexpr float HiddenReachRadius = 180.0f;
	// Timing of the idle cycle. The triple knock is the player's masked
	// window to move; keep these in step with CreateWallKnockTriple.
	constexpr float BangSeconds = 2.1f;
	constexpr float BangMasking = 0.3f;
	/** 한 밤에 대답이 통하는 횟수. 그 뒤로는 벽의 잔향만 돌아온다. */
	constexpr int32 AnswersPerNight = 3;
	constexpr float SearchSeconds = 12.0f;
	constexpr float SearchRadius = 350.0f;
	constexpr float ChaseGiveUpSeconds = 8.0f;
	constexpr float CaptureHoldSeconds = 2.5f;
	/** Hearing bonus while deliberately listening. */
	constexpr float ListeningGain = 2.0f;
	constexpr float HoldingGain = 1.5f;
	/** A second sound within this window of the first confirms the prey. */
	constexpr float ReactionMemorySeconds = 10.0f;

	/**
	 * Drag distance between audible dust sifts (§10.3 분진 낙하). About two
	 * corridor bays: often enough that a moving entity keeps shedding evidence,
	 * rare enough that the sift stays an event.
	 */
	constexpr float DustSiftIntervalCentimeters = 260.0f;
	/** The thinnest cue in the game. It hints; it never announces. */
	constexpr float DustSiftVolume = 0.42f;

	/** How often the floor under him is re-traced while he moves, in seconds. */
	constexpr float DragSurfacePollInterval = 0.30f;
	/** Sheet vinyl, tagged by the world scene for the §21.2 footstep matrix. */
	const FName VinylSurfaceTag(TEXT("Footstep.Vinyl"));
	/** 계단탑의 철판 디딤판. 씬이 같은 발소리 표로 붙인다. */
	const FName MetalStairSurfaceTag(TEXT("Footstep.MetalStair"));

	/**
	 * 연출이 세워 둔 그를 깨우는 크기. 걷기 0.15와 웅크린 걸음 0.05, 천천히 여는
	 * 문 0.1은 넘지 못하고 맨손 노크 0.30부터 넘는다.
	 */
	constexpr float BeatHoldBreakLoudness = 0.3f;

	/** 위층 소리 아래의 천장 노크 간격. 망치질 한 번마다 대답하지 않는다. */
	constexpr double CeilingKnockIntervalSeconds = 12.0;
	/** 4층 천장 바로 밑. 순찰 높이(바닥+60)에서 170 cm 위다. */
	constexpr float CeilingKnockHeight = 170.0f;

	/** 문 앞에 서는 자리. 밤2 대본의 인물 자리와 같은 47 cm다. */
	constexpr float HomeDoorStandOffset = 47.0f;
	/** 노크는 복도 쪽 문짝 겉면에서 난다. */
	constexpr float HomeDoorKnockOffset = 7.0f;
	/** 문 앞을 떠난 뒤 닫힌 집 안의 소리를 흘려듣는 시간. */
	constexpr double HomeDoorIgnoreSeconds = 25.0;
	/** 안쪽 소리에 한 번 더 두드리기까지. */
	constexpr float DoorReknockDelaySeconds = 0.5f;
	/**
	 * 밤2 대본의 문 노크와 같은 짜임이다. 철문 녹음이 앞에 서고 합성은 문짝 너머의
	 * 저역을 깐다. 그 밤에 처음 들은 노크가 이 소리였음을 알아듣게 한다.
	 */
	constexpr float DoorKnockMuffle = 0.66f;
	constexpr float DoorKnockSingleMuffle = 0.72f;
	constexpr float DoorKnockVolume = 0.92f;
	constexpr float DoorKnockUnderlayVolume = 0.45f;
	constexpr float DoorKnockInnerRadius = 220.0f;
	constexpr float DoorKnockFalloff = 1500.0f;
	constexpr int32 DoorSteelHitCount = 3;
	const TCHAR* const DoorSteelSamples[DoorSteelHitCount] = {
		TEXT("Knock_Steel_0"), TEXT("Knock_Steel_1"), TEXT("Knock_Steel_2")};
	constexpr float DoorSteelPitches[DoorSteelHitCount] = {0.84f, 0.80f, 0.86f};
	/** 합성 3연과 같은 간격. */
	constexpr float DoorSteelSpacingSeconds = 0.62f;
	/**
	 * 문 앞 복도에서 문 노크에 숨이 걸리는 거리. 두어 칸이다. 집 안에서는 거리와
	 * 상관없이 걸린다. 순찰 노크에는 놀람이 없다 — 이 노크가 무서운 것은 그녀의
	 * 문이라서다.
	 */
	constexpr float DoorKnockStartleCentimeters = 600.0f;
	/** 자리를 잡으며 갈라지는 미장은 이 간격 안에 되풀이하지 않는다. */
	constexpr double PlantSettleIntervalSeconds = 3.0;

	/** §5.6 매복. 평소 도착 청취(5초)의 두 배를 넘게 엎드려 기다린다. */
	constexpr float AmbushHoldSeconds = 12.0f;
	/** §20.3 관찰. 지켜볼 틈이 있을 만큼 귀를 대고 있다가 가던 길로 돌아간다. */
	constexpr float ObservationHoldSeconds = 10.0f;
	/** 관찰하러 가는 길이 지켜보는 그녀에게서 떨어져 있어야 하는 거리. 복도 폭의 두 배쯤. */
	constexpr float ObservationClearanceCentimeters = 300.0f;
	/** §19.8 방위 자막 거리. 자막은 기본값이 켜짐이라 먼 소리까지 적으면 밤새 글이 뜬다. */
	constexpr float NearCaptionCentimeters = 1600.0f;
	/** 순찰 노크 자막은 같은 문장을 이 간격 안에 되풀이하지 않는다. */
	constexpr double KnockCaptionIntervalSeconds = 12.0;
	/** 다가오는 걸음은 이 거리 안에서만, 이 간격으로 대체 채널에 보낸다. */
	constexpr float ApproachCueCentimeters = 600.0f;
	constexpr double ApproachCueIntervalSeconds = 1.5;
	constexpr double ChaseApproachCueIntervalSeconds = 0.75;

	// -- 놓친 뒤 -----------------------------------------------------------------
	// 소리가 끊겼다고 바로 멈추지 않는다. 마지막 소리 자리에 닿고도 조용하면 그녀가
	// 가던 쪽으로 몇 미터 더 따라가 본다. 그래도 없으면 둘레를 뒤진다.
	constexpr double ChaseMomentumAfterSeconds = 1.2;
	/** 이보다 오래 떨어진 두 소리는 이어서 방향을 읽지 않는다. */
	constexpr double TrailLinkSeconds = 4.0;
	constexpr int32 TrailCapacity = 4;
	/** 가던 쪽으로 따라가 보는 거리(건물 길). */
	constexpr float MomentumMinDistance = 250.0f;
	constexpr float MomentumMaxDistance = 800.0f;
	/** 수색에 들르는 자리 수와, 놓친 자리에서 그 자리들까지의 길 거리. */
	constexpr int32 SearchSpotCount = 5;
	constexpr float SearchReach = 950.0f;
	/** 숨을 자리는 놓친 자리에서 이만큼 안이면 들른다. */
	constexpr float SearchHidingReach = 1000.0f;
	/** 한 자리에 귀를 대는 시간. 숨을 자리 앞에서는 더 오래, 더 가까이 듣는다. */
	constexpr float SearchPauseSeconds = 2.1f;
	constexpr float SearchHidingPauseSeconds = 3.6f;
	/** 찾아다니는 걸음. 조사보다 느리고 순찰보다 빠르다. 소리를 내지 않으려는 걸음이다. */
	constexpr float SearchSpeedScale = 0.62f;
	/** 수색이 끝나고 돌아간 뒤의 경계. 더 천천히 기고 더 오래, 더 멀리 듣는다. */
	constexpr double AlertSeconds = 45.0;
	constexpr float AlertCrawlScale = 0.8f;
	constexpr float AlertHearingGain = 1.2f;
	constexpr float AlertListenScale = 1.3f;
	/** 걸음 사이 수색 중의 귀. 멈춰 듣는 동안은 제자리 청취와 같다. */
	constexpr float SearchMovingGain = 1.15f;

	// -- 계단 ------------------------------------------------------------------
	// 네 발로 디딤판을 짚고 오른다. 오를 때는 조금 느리고 내려갈 때는 거의 그대로다.
	constexpr float StairClimbSpeedScale = 0.82f;
	constexpr float StairDescendSpeedScale = 0.92f;
	/** 몸이 경사를 따라 눕는 한계. 한 층 열여덟 단이 34도쯤이다. */
	constexpr float StairPitchLimit = 38.0f;
	/** 쫓는 동안 길을 다시 짜는 간격. 그녀가 계단을 오르내리면 길도 따라 바뀐다. */
	constexpr double ChaseReplanSeconds = 0.8;
	/** 다른 목표면 길을 새로 짠다. */
	constexpr float ReplanGoalShift = 140.0f;
}

AIGListenerEntity::AIGListenerEntity()
{
	PrimaryActorTick.bCanEverTick = true;

	Body = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Body"));
	// Low and wide: an upper body on elbows with the legs trailing behind.
	// 반지름은 어깨 폭이다. 팔꿈치는 그보다 넓게 벌어지지만, 90 cm 문틀과 계단
	// 띠(105 cm)를 지나야 하는 몸이라 벽에 닿는 것은 팔이지 몸통이 아니다.
	Body->InitCapsuleSize(34.0f, 58.0f);
	Body->SetCollisionProfileName(TEXT("Pawn"));
	Body->SetCanEverAffectNavigation(false);
	SetRootComponent(Body);

	// The entity is never player-possessed and never uses a controller brain;
	// the state machine below is the whole mind.
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void AIGListenerEntity::BeginPlay()
{
	Super::BeginPlay();

	SpawnLocation = GetActorLocation();
	SearchAnchor = SpawnLocation;
	BuildGreyboxBody();
	// 층을 오가는 길. 계단 치수는 씬이 든 정적 값이라 씬이 서기 전에도 짤 수 있다.
	BuildingNav.Build();

	if (UWorld* World = GetWorld())
	{
		NoiseSubsystem = World->GetSubsystem<UIGNoiseSubsystem>();
		if (NoiseSubsystem)
		{
			NoiseHandle = NoiseSubsystem->OnNoiseReported.AddUObject(
				this, &AIGListenerEntity::HandleNoise);
		}

		// The crawl bed runs for the entity's whole life; movement only
		// changes its volume, so silence always means "it stopped".
		DragLoopComponent = NewObject<UAudioComponent>(this);
		DragLoopComponent->RegisterComponent();
		DragLoopComponent->AttachToComponent(
			Body, FAttachmentTransformRules::KeepRelativeTransform);
		DragLoopComponent->SetSound(
			UIGToneSequenceSoundWave::CreateEntityDragLoop(this));
		DragLoopComponent->AttenuationSettings = IGAudio::MakeAttenuation(
			this,
			220.0f,
			2400.0f,
			EIGAudioBus::Entity);
		DragLoopComponent->bAllowSpatialization = true;
		DragLoopComponent->SetVolumeMultiplier(0.0f);
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->RegisterPersistentBed(
				DragLoopComponent,
				EIGAudioBus::Entity);
		}
		DragLoopComponent->Play();

		// 숨은 늘 쉰다. 멈춰 있어도 가까우면 들린다 — 끌림이 0인 자리에서 그가
		// 거기 있다는 것을 알려 주는 유일한 소리다.
		BreathLoopComponent = NewObject<UAudioComponent>(this);
		BreathLoopComponent->RegisterComponent();
		BreathLoopComponent->AttachToComponent(
			Body, FAttachmentTransformRules::KeepRelativeTransform);
		BreathLoopComponent->SetRelativeLocation(FVector(30.0f, 0.0f, 20.0f));
		BreathLoopComponent->SetSound(IGAudio::SampleOr(
			TEXT("Entity_Breath_Loop"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateEntityBreathLoop(this); }));
		BreathLoopComponent->AttenuationSettings = IGAudio::MakeAttenuation(
			this,
			160.0f,
			1500.0f,
			EIGAudioBus::Entity);
		BreathLoopComponent->bAllowSpatialization = true;
		// 배수는 1이고 상태별 볼륨은 페이더가 든다(UpdateBreathLoop). 배수를 0으로
		// 두고 AdjustVolume으로 올리던 때는 곱이 늘 0이라 이 숨이 한 번도 안
		// 들렸다 — 「멈춰 있어도 가까우면 들린다」는 위 문장이 그동안 거짓이었다.
		BreathLoopComponent->SetVolumeMultiplier(1.0f);
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->PrepareSound(BreathLoopComponent->Sound, EIGAudioBus::Entity);
			AudioDirector->RegisterPersistentBed(
				BreathLoopComponent,
				EIGAudioBus::Entity);
		}
	}

	// §20.4: the mode is a user setting, read once when he wakes into the world.
	// A run started from the harness can override it without writing it back.
	Difficulty = IGListenerTuning::ResolveActiveDifficulty();
	RefreshNightTuning();

	EnterState(EIGListenerState::Patrolling);
}

void AIGListenerEntity::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(CaptureGrabTrimTimer);
	if (NoiseSubsystem)
	{
		NoiseSubsystem->OnNoiseReported.Remove(NoiseHandle);
		// Never leave the building masked by a dead entity's knock window.
		NoiseSubsystem->SetGlobalMasking(0.0f);
	}
	// 듣던 그가 사라지면 세계가 참던 숨도 돌아온다.
	if (UWorld* World = GetWorld();
		World && !bDormant
		&& (State == EIGListenerState::Listening || State == EIGListenerState::Holding))
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->SetEntityListening(false);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AIGListenerEntity::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StateSeconds += DeltaSeconds;
	TickState(DeltaSeconds);
	TexturePrefetchSeconds -= DeltaSeconds;
	if (!bDormant && ListenerSkeletal && CachedPlayer.IsValid() && TexturePrefetchSeconds <= 0.f)
	{
		TexturePrefetchSeconds = 1.f;
		if (FVector::DistSquared(GetActorLocation(), CachedPlayer->GetActorLocation()) < FMath::Square(500.f))
		{
			// 가까워지는 몸의 텍스처만 미리 올린다. 시간 제한이 끝나면 일반 스트리밍으로 돌아간다.
			ListenerSkeletal->PrestreamTextures(5.f, false);
		}
	}
	UpdatePresentationLayer();
	UpdateStairPitch(DeltaSeconds);
	UpdatePresentationPose(LastMoveSpeed, DeltaSeconds);
	UpdateDragLoop(LastMoveSpeed);
	// Only while he is actually moving, and only a few times a second: a trace
	// per frame for a sound that changes at a doorway would be pure waste.
	if (LastMoveSpeed > 1.0f)
	{
		DragSurfacePollSeconds += DeltaSeconds;
		if (DragSurfacePollSeconds >= IGListener::DragSurfacePollInterval)
		{
			DragSurfacePollSeconds = 0.0f;
			RefreshDragSurface();
		}
	}
	UpdateThreatPressure();
	ReportDustTrail();
	LastMoveSpeed = 0.0f;
}

// -- state machine ---------------------------------------------------------

void AIGListenerEntity::EnterState(const EIGListenerState NewState)
{
	// Leaving the knock window always clears its masking, whatever comes next.
	if (State == EIGListenerState::Banging && NoiseSubsystem)
	{
		NoiseSubsystem->SetGlobalMasking(0.0f);
	}

	// 소리 없이 가는 걸음은 가는 길과 엎드린 자리에서만 산다. 아래 음악 매핑이
	// 이 값을 읽으므로 상태를 바꾸기 전에 내린다.
	if (NewState != EIGListenerState::Investigating
		&& NewState != EIGListenerState::Holding)
	{
		bSilentApproach = false;
		bObservationHold = false;
	}

	const EIGListenerState PreviousState = State;
	State = NewState;
	StateSeconds = 0.0f;
	StuckSeconds = 0.0f;
	if (NewState != EIGListenerState::Chasing)
	{
		bLungeArmed = false;
	}
	// 위층 소리의 부름은 조사 하나 동안만 유효하다. 새 상태로 들어갈 때마다
	// 지우고, 위층 소리를 들은 쪽이 조사에 들어간 뒤 다시 세운다.
	bCallFromAbove = false;
	bKnockUpOnArrival = false;
	// 멈춰 선 자리의 맥락은 두드림·청취·제자리 청취가 이어지는 동안만 산다.
	if (NewState != EIGListenerState::Banging
		&& NewState != EIGListenerState::Listening
		&& NewState != EIGListenerState::Holding)
	{
		AttentionDirection = FVector::ZeroVector;
		bKnockingUp = false;
		bCadenceEarsUp = false;
		bAtHomeDoor = false;
		DoorReknockCountdown = -1.0f;
		DoorSteelHitsPlayed = IGListener::DoorSteelHitCount;
	}

	// 붙잡히는 동안에도 같은 몸이 남는다. 평면 팔 그림으로 바꿔치기하지 않는다.
	if (NewState == EIGListenerState::CaptureHold)
	{
		SetActorHiddenInGame(false);
	}
	else if (PreviousState == EIGListenerState::CaptureHold)
	{
		SetCaptureKeyLight(false);
		// 낮에는 잠들어 있어야 하므로 휴면 상태를 그대로 따른다.
		SetActorHiddenInGame(bDormant);
		if (ListenerSkeletal)
		{
			ListenerSkeletal->SetRelativeLocationAndRotation(FVector(0, 0, -58), FRotator::ZeroRotator);
		}
		StairBodyPitch = 0.0f;
		StairPitchTarget = 0.0f;
	}

	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			EIGAudioThreatState AudioState = EIGAudioThreatState::Calm;
			switch (NewState)
			{
			case EIGListenerState::Banging:
				AudioState = EIGAudioThreatState::Banging;
				break;
			case EIGListenerState::Listening:
				AudioState = EIGAudioThreatState::Listening;
				break;
			case EIGListenerState::Investigating:
			case EIGListenerState::Holding:
				// 들은 것 없이 가는 걸음(매복, 관찰)은 평소 순찰처럼 들린다. 조사
				// 드론이 켜지면 아무것도 듣지 못한 그가 들킨 소리를 낸다.
				AudioState = bSilentApproach
					? EIGAudioThreatState::Calm
					: EIGAudioThreatState::Investigating;
				break;
			case EIGListenerState::Searching:
				AudioState = EIGAudioThreatState::Investigating;
				break;
			case EIGListenerState::Chasing:
				AudioState = EIGAudioThreatState::Chasing;
				break;
			case EIGListenerState::CaptureHold:
				AudioState = EIGAudioThreatState::Captured;
				break;
			case EIGListenerState::FinaleLured:
				AudioState = EIGAudioThreatState::Finale;
				break;
			case EIGListenerState::Patrolling:
			case EIGListenerState::Waiting:
			default:
				break;
			}
			AudioDirector->SetThreatState(AudioState);
			// 조사 끝의 제자리 청취도 청취 창이다. 음악은 그대로 두고, 세계가 숨을
			// 참는 −6dB만 LISTENING과 같이 건다. 엎드린 매복과 관찰도 여기에 든다 —
			// 조용한 건물이 위험하다는 것을 믹스가 말한다.
			AudioDirector->SetEntityListening(
				NewState == EIGListenerState::Listening
				|| NewState == EIGListenerState::Holding);
			// §18.6 CHASE 진입. 소리가 상태를 바꾸는 자리에서 손도 함께
			// 바꾼다 — 둘을 떼어 두면 추격이 끝났는데 패드만 계속 우는
			// 상태가 만들어진다.
			if (AIGPlayerCharacter* PlayerCharacter =
				Cast<AIGPlayerCharacter>(
					UGameplayStatics::GetPlayerPawn(this, 0)))
			{
				PlayerCharacter->SetChaseHaptic(
					AudioState == EIGAudioThreatState::Chasing);
			}
		}
	}

	// 상태가 바뀌는 소리. 예전엔 조사도 추격도 소리 없이 시작됐다 — 음악이
	// 바뀌는 것 말고는 그가 무엇을 들었는지 알 길이 없었다.
	if (!bDormant)
	{
		const bool bWasIdle =
			PreviousState == EIGListenerState::Patrolling
			|| PreviousState == EIGListenerState::Banging
			|| PreviousState == EIGListenerState::Listening
			|| PreviousState == EIGListenerState::Waiting;
		// 들은 것 없이 나선 걸음(매복, 관찰)은 들숨으로 시작하지 않는다. 들킨 순간에
		// HandleNoise가 처음으로 이 숨을 낸다.
		if (NewState == EIGListenerState::Investigating && bWasIdle && !bSilentApproach)
		{
			PlayAlertVocal();
		}
		else if (NewState == EIGListenerState::Chasing && PreviousState != EIGListenerState::Chasing)
		{
			// §19.8 대체 채널. 음악과 타격이 없는 침묵 속에서도 끌림은 빨라진다.
			const FVector ChaseAt = GetActorLocation() + FVector(30.0f, 0.0f, 30.0f);
			EmitPresentationCue(ChaseAt, 0.9f, 2840.0f);
			if (IsNearForCaption(ChaseAt))
			{
				AIGHorrorHUD::PushAudioCaptionAt(
					this,
					NSLOCTEXT("IGMissingFloor", "EntityChaseCaption", "기어 오는 소리가 빨라진다"),
					2.0f,
					ChaseAt);
			}
			bool bPlayedChaseStinger = false;
			if (UWorld* World = GetWorld())
			{
				if (UIGMissingFloorAudioSubsystem* AudioDirector =
					World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
				{
					bPlayedChaseStinger = AudioDirector->PlayStinger(
						EIGStinger::ChaseStart, GetActorLocation() + FVector(30.0f, 0.0f, 30.0f));
				}
			}
			// 굳은 몸이 튀어 나가며 미장이 갈라진다(§4.6이 허락한 그의 소리). 침묵
			// 중이라 타격이 안 났으면 이것도 내지 않는다.
			if (bPlayedChaseStinger)
			{
				PlayPlasterSettle();
			}
			if (AIGPlayerCharacter* PlayerCharacter =
				Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
				bPlayedChaseStinger && PlayerCharacter)
			{
				// 추격 진입은 몸으로도 온다. 가까울수록 크게.
				const float Distance = FVector::Dist(
					PlayerCharacter->GetActorLocation(), GetActorLocation());
				const float Near = FMath::Clamp(1.0f - Distance / 1600.0f, 0.0f, 1.0f);
				if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
				{
					Stress->ApplyScare(0.35f + 0.30f * Near);
				}
				PlayerCharacter->PlayScareKick(0.8f + 1.2f * Near);
			}
		}
		else if ((PreviousState == EIGListenerState::Chasing
				&& NewState == EIGListenerState::Waiting)
			|| (PreviousState == EIGListenerState::Searching
				&& NewState == EIGListenerState::Patrolling))
		{
			// 대답 노크에 얼어붙은 순간, 그리고 그가 뒤지기를 그만두고 돌아서는 순간에
			// 그녀가 숨을 내쉰다. 소리를 놓친 순간에는 아니다 — 그때부터 그는 둘레를
			// 뒤진다. 멀리 있던 그녀는 그가 돌아선 줄 모른다.
			if (AIGPlayerCharacter* PlayerCharacter =
				Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
			{
				const bool bNear = NewState == EIGListenerState::Waiting
					|| FVector::Dist(PlayerCharacter->GetActorLocation(), GetActorLocation()) <= 1800.0f;
				if (UIGStressComponent* Stress = PlayerCharacter->GetStress();
					Stress && bNear)
				{
					Stress->PlayReliefExhale();
				}
			}
		}
	}

	// 첫 칸에 닿기 전에 소리를 듣고 돌아섰다면 그 약속은 버린다. 밤 한가운데의
	// 노크를 대신 지우면 안 된다.
	if (NewState != EIGListenerState::Banging && NewState != EIGListenerState::Patrolling)
	{
		bSilenceNextStopKnock = false;
	}

	switch (NewState)
	{
	case EIGListenerState::Banging:
		if (bSilenceNextStopKnock)
		{
			// 밤1 첫 노크는 천장이 낸다(§8 0-5). 여기서는 서서 기다리기만 한다.
			bSilenceNextStopKnock = false;
			break;
		}
		if (NoiseSubsystem)
		{
			NoiseSubsystem->SetGlobalMasking(IGListener::BangMasking);
		}
		if (bAtHomeDoor)
		{
			PlayHomeDoorKnock(/*bSingle=*/false);
		}
		else if (bKnockingUp)
		{
			PlayCeilingKnock();
		}
		else
		{
			PlayKnockTriple();
		}
		break;

	case EIGListenerState::Listening:
	case EIGListenerState::Holding:
		// It plants itself; the shell settles. 매복으로 엎드릴 때는 반만 갈라진다.
		PlayPlantSettle(bSilentApproach && !bObservationHold ? 0.3f : 0.6f);
		break;

	case EIGListenerState::Searching:
		// 놓친 자리는 그가 선 자리가 아니라 마지막으로 들은 자리다.
		SearchAnchor = LastHeardLocation;
		SearchTarget = SearchAnchor;
		SearchRetargetSeconds = 0.0f;
		BuildSearchPlan();
		break;

	case EIGListenerState::Patrolling:
		bReactingToSound = false;
		NavPath.Reset();
		NavPathOnStair.Reset();
		break;

	case EIGListenerState::Chasing:
		bHasMomentumTarget = false;
		bMomentumTried = false;
		break;

	case EIGListenerState::Waiting:
		// The learned family answer does not stun a monster. It makes Doha
		// plant both elbows and listen like a person expecting the next knock.
		ListenerPhase = 1.0f;
		ListenerPhaseIndex = INDEX_NONE;
		bWaitStirred = false;
		break;

	default:
		break;
	}
}

void AIGListenerEntity::TickState(const float DeltaSeconds)
{
	switch (State)
	{
	case EIGListenerState::Patrolling:
	{
		const FVector* Target = CurrentPatrolTarget();
		if (!Target)
		{
			// 연출이 붙든 동안은 제자리에서 숨만 쉰다.
			if (bBeatHold)
			{
				break;
			}
			// No route: haunt the spawn point in place.
			EnterState(EIGListenerState::Banging);
			break;
		}
		// 수색을 마치고 돌아가는 길이면 다른 층에서 계단을 타고 올라온다. 돌아간 뒤에도
		// 한동안은 더 천천히 긴다.
		const float PatrolSpeed = CrawlSpeed * (IsAlert() ? IGListener::AlertCrawlScale : 1.0f);
		if (MoveTowardGoal(*Target, PatrolSpeed * AdvanceGait(DeltaSeconds), DeltaSeconds))
		{
			// 밤2 문 앞의 그는 대본의 노크 사이에 제 노크를 끼워 넣지 않는다.
			if (bBeatHold)
			{
				break;
			}
			EnterState(EIGListenerState::Banging);
		}
		break;
	}

	case EIGListenerState::Banging:
		if (!AttentionDirection.IsNearlyZero())
		{
			FaceDirection(AttentionDirection, DeltaSeconds);
		}
		// 문짝을 치는 철문 녹음은 한 타씩이라 3연의 간격대로 이어 친다.
		if (bAtHomeDoor
			&& DoorSteelHitsPlayed < IGListener::DoorSteelHitCount
			&& StateSeconds >= IGListener::DoorSteelSpacingSeconds * DoorSteelHitsPlayed)
		{
			PlayHomeDoorSteelHit();
		}
		if (StateSeconds >= IGListener::BangSeconds)
		{
			EnterState(EIGListenerState::Listening);
		}
		break;

	case EIGListenerState::Listening:
		if (!AttentionDirection.IsNearlyZero())
		{
			FaceDirection(AttentionDirection, DeltaSeconds);
		}
		if (DoorReknockCountdown > 0.0f)
		{
			DoorReknockCountdown -= DeltaSeconds;
			if (DoorReknockCountdown <= 0.0f)
			{
				DoorReknockCountdown = -1.0f;
				PlayHomeDoorKnock(/*bSingle=*/true);
				// 한 번 더 두드렸으니 처음부터 다시 듣는다.
				StateSeconds = 0.0f;
			}
			break;
		}
		if (StateSeconds >= ListenSecondsForTier())
		{
			// 문 앞에서는 매복으로 가지 않는다. 두드렸고, 기다렸으니 떠난다.
			if (bAtHomeDoor)
			{
				LeaveHomeDoor();
				break;
			}
			// 박자에 귀를 세웠는데 그 뒤로 아무것도 이어지지 않았다. 대답이 아니라
			// 소리였으니 그 자리를 보러 간다. 갈 자리는 박자를 들을 때 적어 두었다
			// (NoteAnswerLocation) — 위층에서 친 박자면 계단 아래, 닫힌 문에 대고 친
			// 박자면 문 앞이다. 듣는 동안 움직이지 않았으니 위층인지만 다시 잰다.
			if (bCadenceEarsUp)
			{
				const bool bFromAbove =
					CadenceTapLocation.Z - GetActorLocation().Z > FloorHeightThreshold;
				bReactingToSound = true;
				EnterState(EIGListenerState::Investigating);
				bCallFromAbove = bFromAbove;
				break;
			}
			// Tier 3 stops walking the route and goes to sit on the player's
			// habit instead (§5.6). Everything else takes the next stop.
			if (!TryBeginAmbush())
			{
				AdvancePatrolIndex();
				EnterState(EIGListenerState::Patrolling);
			}
		}
		break;

	case EIGListenerState::Investigating:
	{
		// 위층 소리를 향해 가던 중에 연출이 건물을 멈추면(P4의 8초) 그도 멈춘다.
		// 그 침묵 속에서 그의 걸음만 올라오면 벽의 대답이 묻힌다.
		if (bCallFromAbove && IsAuthoredSilenceActive())
		{
			break;
		}
		FVector DoorCenter = FVector::ZeroVector;
		FVector Outward = FVector::ZeroVector;
		if (ShouldGoToHomeDoor() && GetHomeDoorFrame(DoorCenter, Outward))
		{
			const FVector DoorFront =
				DoorCenter + Outward * IGListener::HomeDoorStandOffset;
			const bool bArrived =
				CrawlTowards(DoorFront, InvestigateSpeed, DeltaSeconds);
			if (bBlockedByHomeDoor
				|| (bArrived && FVector::Dist2D(GetActorLocation(), DoorFront) <= 120.0f))
			{
				ArriveAtHomeDoor();
			}
			else if (bArrived)
			{
				EnterState(EIGListenerState::Holding);
			}
			break;
		}
		// 들은 것 없이 가는 걸음은 순찰과 같은 빠르기다. 끌림과 걸음이 평소처럼
		// 들려야 매복이 조사와 갈린다.
		if (MoveTowardGoal(
				LastHeardLocation,
				bSilentApproach ? CrawlSpeed : InvestigateSpeed,
				DeltaSeconds))
		{
			if (bBlockedByClosedDoor)
			{
				ArriveAtClosedDoor();
			}
			else if (bCallFromAbove)
			{
				ArriveBelowUpperSound();
			}
			else
			{
				EnterState(EIGListenerState::Holding);
			}
		}
		break;
	}

	case EIGListenerState::Holding:
	{
		if (!AttentionDirection.IsNearlyZero())
		{
			FaceDirection(AttentionDirection, DeltaSeconds);
		}
		// §20.2 「INVESTIGATE 도착 청취」. 표는 밤마다 6·6·5·5초라고 적어 두었는데
		// 실제로는 상수 6초만 읽고 있었다. 매복과 관찰은 들은 자리가 아니라 기다리는
		// 자리라 따로 잰다.
		const float HoldFor = bObservationHold
			? IGListener::ObservationHoldSeconds
			: bSilentApproach
				? IGListener::AmbushHoldSeconds
				: FMath::Max(Tuning.InvestigateHoldSeconds, 0.5f);
		if (StateSeconds >= HoldFor)
		{
			// 밤2 대본 전의 문 앞에서는 듣기만 하고 떠난다.
			if (bAtHomeDoor)
			{
				LeaveHomeDoor();
				break;
			}
			// 귀를 대 보았고, 아무것도 없었다. 두드리지 않고 가던 길로 돌아간다 —
			// 여기서 노크가 나면 위층의 그녀에게는 들켰다는 말이 된다.
			if (bObservationHold)
			{
				EnterState(EIGListenerState::Patrolling);
				break;
			}
			// 매복이 끝났다. 엎드려 있던 자리에서 세 번 두드려 거기 있었다는 것을
			// 드러낸다. 이어지는 청취 뒤에는 매복이 이미 쓰였으니 다음 칸으로 간다.
			if (bSilentApproach)
			{
				EnterState(EIGListenerState::Banging);
				break;
			}
			EnterState(EIGListenerState::Searching);
		}
		break;
	}

	case EIGListenerState::Chasing:
	{
		// §4.5 그는 문을 부수지 않는다. 쫓던 소리가 닫힌 403호 안으로 들어가면
		// 문 앞까지 와서 두드린다. 문짝을 밀며 추격 음악만 이어지던 자리다.
		FVector DoorCenter = FVector::ZeroVector;
		FVector Outward = FVector::ZeroVector;
		if (ShouldGoToHomeDoor() && GetHomeDoorFrame(DoorCenter, Outward))
		{
			const FVector DoorFront =
				DoorCenter + Outward * IGListener::HomeDoorStandOffset;
			const bool bArrived = CrawlTowards(DoorFront, ChaseSpeed, DeltaSeconds);
			if (bBlockedByHomeDoor
				|| (bArrived && FVector::Dist2D(GetActorLocation(), DoorFront) <= 120.0f))
			{
				ArriveAtHomeDoor();
				break;
			}
		}
		else
		{
			// 소리가 끊겨도 바로 서지 않는다. 마지막 소리 자리에 닿았는데 조용하면 그녀가
			// 가던 쪽으로 몇 미터 더 간다. 계단 쪽으로 가던 소리면 계단을 탄다.
			const double Quiet = GetWorld()->GetTimeSeconds() - LastHeardTime;
			const FVector Goal = bHasMomentumTarget ? ChaseMomentumTarget : LastHeardLocation;
			const bool bArrived = MoveTowardGoal(Goal, ChaseSpeed, DeltaSeconds);
			if (bArrived && bBlockedByClosedDoor)
			{
				// 문 너머로 달아났다. 문을 부수지 않는다. 두드리고 듣는다.
				ArriveAtClosedDoor();
				break;
			}
			if (bArrived && !bMomentumTried && Quiet >= IGListener::ChaseMomentumAfterSeconds)
			{
				bMomentumTried = true;
				bHasMomentumTarget = PredictPlayerHeading(ChaseMomentumTarget);
			}
		}
		// 소음 이벤트와 같은 게임 시간이다. 일시정지 중에는 둘 다 멈춘다.
		const double SilenceSeconds =
			GetWorld()->GetTimeSeconds() - LastHeardTime;
		if (SilenceSeconds >= IGListener::ChaseGiveUpSeconds)
		{
			EnterState(EIGListenerState::Searching);
		}
		break;
	}

	case EIGListenerState::Searching:
	{
		// 놓친 자리 둘레를 차례로 들른다. 가던 쪽의 길목, 숨을 만한 가구, 문 앞과 빈방.
		// 닿으면 멈춰서 귀를 대고, 숨을 자리 앞에서는 더 오래 듣는다. 그 사이에 무엇이든
		// 들리면 다시 조사와 추격이다(HandleNoise). 다 돌았거나 시간이 다하면 그제야
		// 순찰로 돌아가는데, 돌아가는 동안에도 한동안은 더 낮게 기고 더 오래 듣는다.
		// 시간은 놓친 자리 둘레에 닿고부터 잰다. 다른 층에서 내려오는 동안 다 써 버리면
		// 도착하자마자 돌아선다.
		if (FIGBuildingNav::FloorOfFeet(GetFeetLocation().Z)
				== FIGBuildingNav::FloorOfFeet(SearchAnchor.Z - 60.0f)
			&& FVector::Dist2D(GetActorLocation(), SearchAnchor) < 1500.0f)
		{
			SearchBudgetSeconds -= DeltaSeconds;
		}
		UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
			? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
			: nullptr;
		if (!SearchSpots.IsValidIndex(SearchSpotIndex) || SearchBudgetSeconds <= 0.0f)
		{
			if (bSearchPausing && AudioDirector)
			{
				AudioDirector->SetEntityListening(false);
			}
			bSearchPausing = false;
			bReactingToSound = false;
			AlertUntilSeconds = GetWorld()->GetTimeSeconds() + IGListener::AlertSeconds;
			EnterState(EIGListenerState::Patrolling);
			break;
		}
		if (bSearchPausing)
		{
			AttentionDirection = SearchFacings[SearchSpotIndex];
			FaceDirection(AttentionDirection, DeltaSeconds);
			SearchPauseLeft -= DeltaSeconds;
			if (SearchPauseLeft <= 0.0f)
			{
				bSearchPausing = false;
				++SearchSpotIndex;
				if (AudioDirector)
				{
					AudioDirector->SetEntityListening(false);
				}
			}
			break;
		}
		if (MoveTowardGoal(
				SearchSpots[SearchSpotIndex],
				InvestigateSpeed * IGListener::SearchSpeedScale,
				DeltaSeconds))
		{
			// 닫힌 문 앞이면 그 문에 귀를 댄다. 문 너머 숨을 자리는 거기서 듣는다.
			if (bBlockedByClosedDoor)
			{
				bBlockedByClosedDoor = false;
				if (const AActor* Door = BlockingDoor.Get())
				{
					const FVector ToDoor = Door->GetComponentsBoundingBox().GetCenter() - GetActorLocation();
					SearchFacings[SearchSpotIndex] = FVector(ToDoor.X, ToDoor.Y, 0.0f).GetSafeNormal();
				}
			}
			bSearchPausing = true;
			// 같은 길이로 멈추면 박자를 외운다. 들른 순서로 조금씩 흔든다.
			const uint32 PauseHash = static_cast<uint32>(SearchSpotIndex + 1 + KnockSerial * 7) * 2654435761u;
			const float Sway = 0.85f + 0.3f * ((PauseHash >> 8) & 0xFF) / 255.0f;
			SearchPauseLeft = (SearchSpotIsHiding[SearchSpotIndex]
				? IGListener::SearchHidingPauseSeconds
				: IGListener::SearchPauseSeconds) * Sway;
			PlayPlantSettle(SearchSpotIsHiding[SearchSpotIndex] ? 0.32f : 0.45f);
			if (AudioDirector)
			{
				// 그가 멈춰 듣는 동안 건물도 숨을 참는다(청취 창의 −6dB).
				AudioDirector->SetEntityListening(true);
			}
		}
		break;
	}

	case EIGListenerState::Waiting:
	{
		const float WaitFor = WaitSecondsForTier();
		// 희망이 닳는 것을 몸이 먼저 말한다. 끝나기 2초 전에 팔꿈치를 고쳐 짚고 숨이
		// 돌아온다. 노크로 알리지는 않는다 — 그의 노크 하나는 다시 대답하라는 말로
		// 읽히는데, 기다리는 동안의 대답은 받지 않는다.
		const float StirAt = WaitFor >= 4.0f ? WaitFor - 2.0f : WaitFor * 0.5f;
		if (!bWaitStirred && StateSeconds >= StirAt)
		{
			bWaitStirred = true;
			PlayPlasterSettle(0.42f, 0.94f);
		}
		// Hope, then the spot the answer came from.
		if (StateSeconds >= WaitFor)
		{
			// 갈 자리는 대답을 들을 때 적어 두었다(NotifyAnswerKnock). 닫힌 문에 대고
			// 안에서 한 대답이면 그 뒤 그녀가 집을 나섰어도 문 앞으로 가서 두드리고,
			// 위층에서 온 대답이면 계단 아래까지 가서 그쪽으로 고개를 든다. 기다리는
			// 동안 움직이지 않았으니 위층인지만 다시 잰다.
			const bool bFromAbove =
				AnswerKnockLocation.Z - GetActorLocation().Z > FloorHeightThreshold;
			bReactingToSound = true;
			EnterState(EIGListenerState::Investigating);
			bCallFromAbove = bFromAbove;
		}
		break;
	}

	case EIGListenerState::CaptureHold:
		// With a director bound, the reset (and the aggression raise) is its
		// call; the self-reset only covers a directorless test arena.
		if (StateSeconds >= IGListener::CaptureHoldSeconds
			&& !OnPlayerCaptured.IsBound())
		{
			ResetToPatrolStart(true);
		}
		break;

	case EIGListenerState::FinaleLured:
	{
		if (!FinaleRoutePoints.IsValidIndex(FinaleRouteIndex))
		{
			SetDormant(true);
			break;
		}
		const float FinaleSpeed = FinaleSpeedOverride > 0.0f
			? FinaleSpeedOverride
			: ChaseSpeed * 0.82f;
		if (CrawlTowards(
				FinaleRoutePoints[FinaleRouteIndex],
				FinaleSpeed,
				DeltaSeconds))
		{
			++FinaleRouteIndex;
		}
		break;
	}
	}

	// Touching the player ends the night. 예전엔 조사·추격·수색 셋에서만 잡았다.
	// 두드리는 2.1초, 듣는 8초, 서서 기다리는 6초 동안 110cm 안에 서 있어도
	// 아무 일이 없었다 — 그의 팔이 닿는 자리는 상태를 가리지 않는다. 대답 뒤의
	// 기다림(Waiting)만 약속이라 예외다.
	if (!bDormant
		&& State != EIGListenerState::Waiting
		&& State != EIGListenerState::CaptureHold
		&& State != EIGListenerState::FinaleLured)
	{
		if (!CachedPlayer.IsValid())
		{
			for (TActorIterator<AIGPlayerCharacter> It(GetWorld()); It; ++It)
			{
				CachedPlayer = *It;
				break;
			}
		}
		if (APawn* Player = CachedPlayer.Get())
		{
			const float Distance =
				FVector::Dist(Player->GetActorLocation(), GetActorLocation());
			// 들리는 것과 손이 닿는 것은 다르다. 문·벽 너머의 같은 반경에서는
			// 덮치지도 잡지도 않는다. 가까운 순간에만 검사한다.
			bool bReachable = false;
			if (Distance <= CaptureRadius * 2.3f)
			{
				FCollisionQueryParams ReachParams(SCENE_QUERY_STAT(IGListenerReach), false, this);
				ReachParams.AddIgnoredActor(Player);
				FHitResult ReachHit;
				bReachable = !GetWorld()->LineTraceSingleByChannel(
					ReachHit, GetActorLocation(), Player->GetActorLocation(),
					ECC_Visibility, ReachParams);
			}
			// §4. 숨은 사람은 손이 닿는 것만으로는 잡히지 않는다. 장롱 문짝은 손을 막고
			// 침대는 막지 못하지만, 어느 쪽이든 그는 소리로만 안다. 코앞에서 소리를 냈을
			// 때만 그 자리를 열어 끌어낸다.
			const AIGPlayerCharacter* HidingPlayer = Cast<AIGPlayerCharacter>(Player);
			if (HidingPlayer && HidingPlayer->IsConcealedInHidingSpot())
			{
				bLungeArmed = false;
				const bool bHeardInside = GetWorld()->GetTimeSeconds() - LastHeardHiddenPlayerSeconds
					<= IGListener::HiddenDetectMemorySeconds;
				if (bHeardInside
					&& FVector::Dist2D(Player->GetActorLocation(), GetActorLocation())
						<= IGListener::HiddenReachRadius
					&& Tuning.bCaptureEnabled)
				{
					BeginCapture(Player);
				}
				return;
			}
			// 듣기만 하는 밤: he reaches the sound and holds there, and that is
			// where it ends. Touching costs nothing, so the night can never be
			// taken away — the story, the puzzles and all three endings stay
			// exactly the same (§20.4).
			// 두 팔 길이 안. 덮치는 동작이 먼저 오고 그 끝에 포옹이 온다.
			if (State == EIGListenerState::Chasing)
			{
				bLungeArmed = bReachable;
			}
			// 계단참 카메오 동안은 들은 뒤(조사·멈춤·추격·수색)에만 손이 닿는다.
			const bool bPursuing = State == EIGListenerState::Investigating
				|| State == EIGListenerState::Holding
				|| State == EIGListenerState::Chasing
				|| State == EIGListenerState::Searching;
			if (Distance <= CaptureRadius && bReachable && Tuning.bCaptureEnabled
				&& (!bTouchCaptureSuppressed || bPursuing))
			{
				BeginCapture(Player);
			}
		}
	}
}

void AIGListenerEntity::HandleNoise(const FIGNoiseEvent& Event)
{
	// Its own presentation sounds never enter the bus, so no self-filter is
	// needed beyond ignoring events it instigated by design.
	if (Event.Instigator.Get() == this)
	{
		return;
	}
	// 낮의 그는 잠들어 있다. 몸이 없는 자리에서 들으면 음악과 패드만 깨어난다.
	// 새벽에 눈을 감는 반 초 동안 멈춰 세운 그(Tick 꺼짐)도 듣지 않는다. 상태
	// 기계는 서 있는데 소리만 받으면 들숨과 추격 타격이 새벽 위로 난다.
	if (bDormant || !IsActorTickEnabled())
	{
		return;
	}
	if (State == EIGListenerState::Waiting
		|| State == EIGListenerState::CaptureHold
		|| State == EIGListenerState::FinaleLured)
	{
		return;
	}
	const AIGSwingDoor* Door = HomeDoor.Get();
	const bool bHomeSound = Door
		&& !Door->IsOpen()
		&& IsHomeSoundAt(Event.Location, Event.Instigator.Get());
	// 문 앞에서 두드리고 기다렸다가 떠났다. 닫힌 집 안의 소리에 곧장 되돌아오면
	// 「떠난다」가 없어진다.
	if (bHomeSound && Event.TimeSeconds < HomeDoorIgnoreUntil)
	{
		return;
	}
	if (!CanHear(Event))
	{
		return;
	}
	if (const AIGPlayerCharacter* HiddenPlayer = Cast<AIGPlayerCharacter>(Event.Instigator.Get());
		HiddenPlayer
		&& HiddenPlayer->IsConcealedInHidingSpot()
		&& FVector::DistSquared(Event.Location, GetActorLocation())
			<= FMath::Square(IGListener::HiddenDetectRadius))
	{
		LastHeardHiddenPlayerSeconds = Event.TimeSeconds;
	}
	// 귀를 세우게 한 그 탭의 소리다. 대답 인식이 소음 보고보다 먼저 불리므로
	// 같은 프레임에 같은 자리에서 한 번 더 들어온다. 이미 듣고 있는 소리다.
	if (bCadenceEarsUp
		&& FMath::Abs(Event.TimeSeconds - CadenceTapSeconds) <= 0.01
		&& FVector::DistSquared(Event.Location, CadenceTapLocation) <= FMath::Square(60.0f))
	{
		return;
	}
	if (bBeatHold)
	{
		// 작은 소리는 흘려듣는다. 닫힌 집 안의 소리도 그렇다. 그는 이미 그 문
		// 앞에 서 있고, 풀어 주면 순찰로 돌다가 대본 노크 사이에 제 노크를 끼운다.
		if (bHomeSound || Event.Loudness < IGListener::BeatHoldBreakLoudness)
		{
			return;
		}
		bBeatHold = false;
	}
	// 닫힌 문 너머의 소리에는 문 앞에서 대답한다. 쫓아 들어가지도, 추격으로
	// 넘어가지도 않는다. 한 번 더 두드리는 것은 청취 중 한 번뿐이다.
	if (bAtHomeDoor && bHomeSound)
	{
		if (State == EIGListenerState::Listening
			&& !bDoorReknocked
			&& CanKnockHomeDoor())
		{
			bDoorReknocked = true;
			DoorReknockCountdown = IGListener::DoorReknockDelaySeconds;
		}
		return;
	}

	const double Now = Event.TimeSeconds;
	const bool bSecondSound =
		bReactingToSound
		&& (Now - LastHeardTime) <= IGListener::ReactionMemorySeconds;

	NoteHeardLocation(Event.Location, bHomeSound);
	LastHeardTime = Now;
	// 소리 없이 다가오던 걸음(매복, 관찰)이 무언가를 들었다. 여기서부터는 평소의
	// 조사다. 드론과 들숨은 이 순간에 처음 난다.
	const bool bWasLyingInWait = bSilentApproach;
	bSilentApproach = false;
	bObservationHold = false;

	if (Event.Instigator.IsValid()
		&& Event.Instigator->IsA<AIGPlayerCharacter>())
	{
		CachedPlayer = Cast<APawn>(Event.Instigator.Get());
		NotePlayerTrail(Event.Location, Now);
	}
	// 새 소리가 들렸다. 가던 쪽을 짐작해 따라가던 걸음은 거기서 접고 소리로 간다.
	bHasMomentumTarget = false;
	bMomentumTried = false;

	// 위에서 난 소리. 그는 계단을 기어오르지 못하고(CrawlTowards는 Z를 버린다)
	// 자기가 갇혔던 층에는 올라가지 않는다(§8 3-3, §13). 닿을 수 없는 추격을
	// 거는 대신 계단 아래까지 와서 듣고, 두 번 들렸으면 위를 향해 두드린다.
	// 쫓던 그녀가 계단을 올라가 버린 경우도 같다. 아래층 소리(밤2 1층의
	// 붕괴)는 지금처럼 추격이 된다.
	//
	// 1~4층은 이제 계단탑으로 이어져 있다. 그 사이의 소리에는 계단을 타고 간다
	// (MoveTowardGoal). 이 갈래는 옥상과 5층의 소리만이다.
	const bool bUnreachableAbove =
		Event.Location.Z - GetActorLocation().Z > FloorHeightThreshold
		&& (!BuildingNav.IsBuilt()
			|| FIGBuildingNav::FloorOfFeet(Event.Location.Z - 60.0f) >= 4);
	if (bUnreachableAbove)
	{
		const FVector Below = bHasStairFoot ? StairFoot : Event.Location;
		// 계단 아래 자리는 4층 복도다. 그가 다른 층에 있으면 계단을 타고 거기까지 온다.
		LastHeardLocation = bHasStairFoot ? StairFoot : FVector(Below.X, Below.Y, GetActorLocation().Z);
		UpperSoundLocation = Event.Location;
		const bool bKnockUp = bSecondSound
			|| State == EIGListenerState::Chasing
			|| (bCallFromAbove && bKnockUpOnArrival);
		bReactingToSound = true;
		const bool bAlreadyBelow =
			(State == EIGListenerState::Holding
				|| State == EIGListenerState::Listening
				|| State == EIGListenerState::Banging)
			&& FVector::Dist2D(GetActorLocation(), LastHeardLocation) <= 40.0f;
		if (bAlreadyBelow)
		{
			// 이미 계단 아래에 있다. 다시 기어 올 것 없이 그 자리에서 몸을 고쳐
			// 앉거나(미장 갈라지는 소리) 위를 향해 두드린다. 두드리는 중이면 그대로다.
			// 엎드려 듣던 중이면 계속 듣는다. 소리 없이 와 엎드려 있던 그(매복, 관찰)만은
			// 들킨 순간이라 제자리 청취에 다시 들어가 조사로 바뀐다.
			if (State != EIGListenerState::Banging)
			{
				bKnockUpOnArrival = bKnockUp;
				ArriveBelowUpperSound(
					/*bKeepHolding=*/State == EIGListenerState::Holding && !bWasLyingInWait);
			}
			return;
		}
		// 조용히 가던 걸음이면 같은 조사라도 다시 들어가 음악을 평소 조사로 돌린다.
		if (State != EIGListenerState::Investigating || bWasLyingInWait)
		{
			EnterState(EIGListenerState::Investigating);
		}
		bCallFromAbove = true;
		bKnockUpOnArrival = bKnockUp;
		return;
	}

	if (State == EIGListenerState::Chasing)
	{
		return; // Already committed; the new location is enough.
	}

	if (bSecondSound && Tuning.bChaseEnabled)
	{
		// A sound that answers twice is a someone.
		EnterState(EIGListenerState::Chasing);
		return;
	}

	bReactingToSound = true;
	EnterState(EIGListenerState::Investigating);
	// 엎드려 있던 자리에서 돌아선다. 매복이 들킨 순간이자 그녀가 들킨 순간이다.
	if (bWasLyingInWait)
	{
		PlayAlertVocal();
	}
}

bool AIGListenerEntity::CanHear(const FIGNoiseEvent& Event) const
{
	if (Event.Loudness <= 0.0f)
	{
		return false;
	}
	float Distance = FVector::Dist(Event.Location, GetActorLocation());
	const float HeightGap =
		FMath::Abs(Event.Location.Z - GetActorLocation().Z);
	if (HeightGap > FloorHeightThreshold)
	{
		// Another floor: the structure eats some of the sound.
		Distance *= CrossFloorDistancePenalty;
	}
	// §20.2 기본 청취 반경. The night sensitivity scales his ear, not the sound:
	// a hammer still carries as far as a hammer carries, and the same footstep
	// simply reaches him from farther away as the nights go on.
	return Distance
		<= Event.Radius * HearingMultiplier() * Tuning.HearingSensitivity;
}

float AIGListenerEntity::HearingMultiplier() const
{
	switch (State)
	{
	case EIGListenerState::Listening:
		return IGListener::ListeningGain;
	case EIGListenerState::Holding:
		return IGListener::HoldingGain;
	case EIGListenerState::Searching:
		// 멈춰서 귀를 대는 동안은 제자리 청취와 같고, 옮겨 가는 동안에도 평소보다 예민하다.
		return bSearchPausing ? IGListener::HoldingGain : IGListener::SearchMovingGain;
	case EIGListenerState::Patrolling:
		return IsAlert() ? IGListener::AlertHearingGain : 1.0f;
	default:
		return 1.0f;
	}
}

float AIGListenerEntity::ListenSecondsForTier() const
{
	// §20.2 gives the night base and §4.3-7 the tier axis; the tuning table has
	// already multiplied them, so there is one number left to obey. 수색을 마치고
	// 돌아가는 동안에는 칸마다 더 오래 듣는다.
	return Tuning.ListenWindowSeconds * (IsAlert() ? IGListener::AlertListenScale : 1.0f);
}

float AIGListenerEntity::WaitSecondsForTier() const
{
	// Hope wears out: each reset it waits less on an answer. 조용한 밤 stretches
	// every one of those waits by half again — more time to answer, same fear.
	// 한 밤 안에서도 두 번째 대답은 0.6배, 세 번째는 0.35배만 기다린다.
	static constexpr float Seconds[4] = {20.0f, 12.0f, 6.0f, 6.0f};
	static constexpr float AnswerDecay[3] = {1.0f, 0.6f, 0.35f};
	const int32 AnswerIndex = FMath::Clamp(AnswersThisNight - 1, 0, 2);
	const int32 EffectiveTier = Difficulty == EIGNightDifficulty::Hasty
		? FMath::Max(AggressionTier, 1) : AggressionTier;
	return Seconds[FMath::Clamp(EffectiveTier, 0, 3)] * Tuning.WaitScale
		* AnswerDecay[AnswerIndex];
}

// -- public controls -------------------------------------------------------

void AIGListenerEntity::SetAggressionTier(const int32 Tier)
{
	AggressionTier = FMath::Clamp(Tier, 0, 3);
	// The tier is one of the three axes, so the resolved numbers move with it.
	RefreshNightTuning();
}

void AIGListenerEntity::BeginObservationHold(const FVector& Target)
{
	if (bDormant || State == EIGListenerState::CaptureHold
		|| State == EIGListenerState::FinaleLured)
	{
		// Never interrupt a capture or the authored finale pass to be helpful.
		return;
	}
	// Keep his own floor: he crawls, and the state machine cannot drag him
	// through a slab to reach a spot the mercy net picked. 위층의 벽을 자기 층에
	// 눌러 찍으면 엉뚱한 벽에 가 붙는다 — 밤3의 공동 벽이면 403호 현관 앞이다.
	// 그래서 위층 소리에 늘 가는 자리, 계단 아래까지만 가서 그쪽으로 고개를 든다.
	const float Rise = Target.Z - GetActorLocation().Z;
	const bool bAbove = Rise > FloorHeightThreshold;
	if (Rise < -FloorHeightThreshold || (bAbove && !bHasStairFoot))
	{
		return;
	}
	const FVector Destination = bAbove ? StairFoot : Target;
	LastHeardLocation =
		FVector(Destination.X, Destination.Y, GetActorLocation().Z);
	UpperSoundLocation = Target;
	bReactingToSound = false;
	// 들은 소리가 없으니 들숨도 조사 드론도 없다. 순찰 빠르기로 가서 엎드린다.
	bSilentApproach = true;
	bObservationHold = true;
	EnterState(EIGListenerState::Investigating);
	// 위를 향한 부름은 EnterState가 지우므로 들어간 뒤에 세운다. 닿으면 두드리지
	// 않고 듣기만 한다(ArriveBelowUpperSound).
	bCallFromAbove = bAbove;
	bKnockUpOnArrival = false;
}

bool AIGListenerEntity::CanBeginObservationHold(
	const FVector& Target,
	const FVector& Witness) const
{
	// 도움이 위협을 바꾸면 안 된다. 순찰하거나 제 노크 뒤에 듣는 중일 때만 된다.
	// 소리에 반응하는 중(박자에 귀를 세움, 위층 소리 아래의 청취 포함)이거나 연출이
	// 붙든 그, 문 앞의 그는 떼어 내지 않는다.
	if (bDormant || bBeatHold || bAtHomeDoor || bReactingToSound
		|| (State != EIGListenerState::Patrolling
			&& State != EIGListenerState::Listening))
	{
		return false;
	}
	const FVector Here = GetActorLocation();
	const float Rise = Target.Z - Here.Z;
	const bool bAbove = Rise > FloorHeightThreshold;
	if (Rise < -FloorHeightThreshold || (bAbove && !bHasStairFoot))
	{
		return false;
	}
	// 목격이어야 도움이다. 그녀가 다른 층에 있으면 보이는 것은 없고, 그가 듣는
	// 동안 세계가 숨을 참는 것만 닿는다 — 볼 곳이 아니라 들킨 신호다.
	if (FMath::Abs(Witness.Z - Here.Z) > FloorHeightThreshold)
	{
		return false;
	}
	// 가는 길이 그녀 곁을 지나면 도움이 포획이 된다. 좁은 복도라 넉넉히 비킨다.
	const FVector Destination = bAbove ? StairFoot : Target;
	const float Clearance = FMath::PointDistToSegment(
		FVector(Witness.X, Witness.Y, 0.0f),
		FVector(Here.X, Here.Y, 0.0f),
		FVector(Destination.X, Destination.Y, 0.0f));
	return Clearance >= IGListener::ObservationClearanceCentimeters;
}

void AIGListenerEntity::RefreshNightTuning()
{
	int32 NightIndex = IGListenerTuning::FirstNight;
	if (const UWorld* World = GetWorld())
	{
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (const UIGMissingFloorNarrativeSubsystem* Narrative =
				GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
			{
				NightIndex = Narrative->GetNightIndex();
			}
		}
	}
	// 난이도를 올렸다 내렸을 때 플레이 중 쌓인 단계가 바뀌지 않게 한다.
	const int32 EffectiveTier = Difficulty == EIGNightDifficulty::Hasty
		? FMath::Max(AggressionTier, 1) : AggressionTier;
	Tuning = IGListenerTuning::Resolve(NightIndex, Difficulty, EffectiveTier);
	ChaseSpeed = Tuning.ChaseSpeed;

	// 듣기만 하는 밤에서는 이미 시작된 추격과 포획도 성립하지 않는다. 모드를
	// 밤 중간에 바꿔도 다음 판정부터 즉시 지켜져야 한다.
	if (!Tuning.bChaseEnabled && State == EIGListenerState::Chasing)
	{
		EnterState(EIGListenerState::Investigating);
	}
	if (!Tuning.bTierThreeAmbushAllowed)
	{
		bAmbushArmed = false;
	}
}

void AIGListenerEntity::SetDifficulty(
	const EIGNightDifficulty NewDifficulty)
{
	Difficulty = IGListenerTuning::ClampDifficulty(static_cast<int32>(NewDifficulty));
	RefreshNightTuning();
}

void AIGListenerEntity::SetDifficultyForTesting(
	const EIGNightDifficulty NewDifficulty)
{
	SetDifficulty(NewDifficulty);
}

void AIGListenerEntity::SetPatrolPoints(const TArray<FVector>& Points)
{
	PatrolPoints = Points;
	PatrolIndex = 0;
}

void AIGListenerEntity::SetBeatHold(const bool bHold)
{
	bBeatHold = bHold;
}

void AIGListenerEntity::SetHomeDoor(AIGSwingDoor* Door, const FBox& Interior)
{
	HomeDoor = Door;
	HomeInterior = Interior;
}

void AIGListenerEntity::SetStairFoot(const FVector& Location)
{
	StairFoot = Location;
	bHasStairFoot = true;
}

bool AIGListenerEntity::TryAnswerKnock(const FVector& KnockLocation)
{
	const UWorld* World = GetWorld();
	// 새벽에 멈춰 세운 그(Tick 꺼짐)는 잠든 그와 같이 대답을 받지 않는다.
	if (!World || bDormant || !IsActorTickEnabled())
	{
		return false;
	}
	// Nothing to answer while it is holding her, and the night-four authored
	// pass must not be divertible by a knock.
	if (State == EIGListenerState::CaptureHold
		|| State == EIGListenerState::FinaleLured)
	{
		return false;
	}
	// Out of earshot the taps are just taps on a wall. Checked before the tap is
	// recorded so a sequence started two floors away cannot be completed here.
	if (!CanHearAnswerFrom(KnockLocation))
	{
		return false;
	}
	// 이미 기다리는 중이면 탭은 받되 기다림을 늘리지 않는다. 같은 박자를 6초마다
	// 두드리면 밤새 얼어 있던 구멍이 여기였다.
	if (State == EIGListenerState::Waiting)
	{
		return true;
	}

	const double Now = World->GetTimeSeconds();
	const double WindowScale = GetAnswerWindowScale(this);
	if (AnswerTapTimes.Num() > 0
		&& Now - AnswerTapTimes.Last() > AnswerSequenceResetSeconds * WindowScale)
	{
		// Too long a gap: this is the beginning of a new attempt, not the end of
		// the old one. The player owns the silence between taps (§7 P4).
		AnswerTapTimes.Reset();
	}
	AnswerTapTimes.Add(Now);
	while (AnswerTapTimes.Num() > 3)
	{
		AnswerTapTimes.RemoveAt(0);
	}
	if (AnswerTapTimes.Num() < 3)
	{
		AttendAnswerTap(KnockLocation, Now);
		return true;
	}

	const double PairInterval = AnswerTapTimes[1] - AnswerTapTimes[0];
	const double RestInterval = AnswerTapTimes[2] - AnswerTapTimes[1];
	const bool bCadenceMatches =
		MatchesAnswerCadence(PairInterval, RestInterval, WindowScale);
	AnswerTapTimes.Reset();
	// 닫힌 집 안에서 친 박자도 소리다. 소음을 흘려듣는 자리에서는 박자도 흘려듣는다
	// (HandleNoise). 붙들린 그는 문 안에서 친 둘-쉬고-하나에 밤2 대본을 깨지 않는다.
	// 음성사서함이 가르친 박자라 그 밤에 문에 대고 치는 사람이 많다. 문 앞을 막 떠난
	// 그도 그 박자에 되돌아오지 않는다.
	const AIGSwingDoor* Door = HomeDoor.Get();
	const bool bIgnoredHomeCadence = Door
		&& !Door->IsOpen()
		&& (bBeatHold || Now < HomeDoorIgnoreUntil)
		&& IsHomeSoundAt(KnockLocation, UGameplayStatics::GetPlayerPawn(this, 0));
	if (bCadenceMatches && !bIgnoredHomeCadence)
	{
		if (IsAnswerLearned())
		{
			NotifyAnswerKnock(KnockLocation);
		}
		else if (!bBeatHold
			&& !bAtHomeDoor
			&& (State == EIGListenerState::Patrolling
				|| State == EIGListenerState::Banging
				|| State == EIGListenerState::Listening))
		{
			// P4 전에는 아직 대답이 아니다(§4.3 규칙 6). 박자에 멈춰 귀를 세우지만
			// 기다리지도, 답하지도 않는다. 두 배로 듣는 그 몇 초 동안 한 발만 떼도
			// 두 번째 소리가 되고, 가만히 있으면 청취가 끝난 뒤 그 자리를 보러 온다.
			// 이미 무언가를 쫓는 중이면 박자는 그냥 소리다. 연출이 붙든 그도 서 있던
			// 자리를 지킨다 — 붙듦을 푸는 것은 소음의 몫이다.
			NoteAnswerLocation(KnockLocation);
			LastHeardTime = Now;
			bReactingToSound = true;
			CadenceTapSeconds = Now;
			CadenceTapLocation = KnockLocation;
			EnterState(EIGListenerState::Listening);
			bCadenceEarsUp = true;
		}
	}
	else if (!bCadenceMatches)
	{
		// 틀린 박자의 셋째 탭도 앞의 두 탭과 한 덩어리 소리다. 여기서 추격으로
		// 넘어가지 않고, 듣기가 끝나면 그 자리를 보러 온다.
		AttendAnswerTap(KnockLocation, Now);
	}
	// Either way the tap was a knock aimed at him, so the caller keeps it.
	return true;
}

double AIGListenerEntity::GetAnswerWindowScale(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	return Accessibility
		? FMath::Max(1.0, static_cast<double>(Accessibility->GetKnockWindowScale()))
		: 1.0;
}

bool AIGListenerEntity::MatchesAnswerCadence(
	const double PairInterval,
	const double RestInterval,
	const double WindowScale)
{
	const double Scale = FMath::Max(1.0, WindowScale);
	return PairInterval >= AnswerPairMinSeconds
		&& PairInterval <= AnswerPairMaxSeconds * Scale
		&& RestInterval >= AnswerRestMinSeconds
		&& RestInterval <= AnswerRestMaxSeconds * Scale
		&& RestInterval > PairInterval;
}

void AIGListenerEntity::AttendAnswerTap(const FVector& KnockLocation, const double Now)
{
	// 연출이 붙든 그, 문 앞에 선 그는 박자를 따로 듣지 않는다. 쫓는 그에게
	// 탭은 그냥 새 자리다. 이때의 탭은 평소 소리 규칙(HandleNoise)을 탄다.
	if (bBeatHold || bAtHomeDoor)
	{
		return;
	}
	if (State != EIGListenerState::Patrolling
		&& State != EIGListenerState::Banging
		&& State != EIGListenerState::Listening
		&& State != EIGListenerState::Investigating
		&& State != EIGListenerState::Holding
		&& State != EIGListenerState::Searching)
	{
		return;
	}
	const AIGSwingDoor* Door = HomeDoor.Get();
	if (Door
		&& !Door->IsOpen()
		&& Now < HomeDoorIgnoreUntil
		&& IsHomeSoundAt(KnockLocation, UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		return;
	}
	NoteAnswerLocation(KnockLocation);
	LastHeardTime = Now;
	bReactingToSound = true;
	bSilentApproach = false;
	bObservationHold = false;
	CadenceTapSeconds = Now;
	CadenceTapLocation = KnockLocation;
	if (State != EIGListenerState::Listening)
	{
		EnterState(EIGListenerState::Listening);
	}
	bCadenceEarsUp = true;
}

bool AIGListenerEntity::IsAnswerLearned() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UIGMissingFloorNarrativeSubsystem* Narrative = GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	// T9는 벽이 같은 리듬으로 대답해야 확정된다. 서사가 없는 시험장에서는
	// 예전처럼 곧바로 통한다.
	return !Narrative
		|| Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer);
}

bool AIGListenerEntity::IsAuthoredSilenceActive() const
{
	const UWorld* World = GetWorld();
	const UIGMissingFloorAudioSubsystem* AudioDirector = World
		? World->GetSubsystem<UIGMissingFloorAudioSubsystem>()
		: nullptr;
	return AudioDirector && AudioDirector->IsAuthoredSilence();
}

bool AIGListenerEntity::CanHearAnswerFrom(const FVector& KnockLocation) const
{
	// The answer only reaches it within ordinary hearing of a knock-loud
	// sound; whispering the code from another floor does nothing. 험도 대답을
	// 가린다 — 냉장고 옆에서 친 노크가 그에게는 닿는데 같은 노크의 소음은
	// 험에 삼켜지던 모순을 없앤다. 소음과 대답은 같은 귀로 듣는다.
	// 그의 노크 3연이 거는 전역 마스킹(-0.3)은 뺀다. 그 창에 친 대답도 대답이다 —
	// 기계 옆의 험만 대답을 삼킨다.
	const float Masking = NoiseSubsystem
		? FMath::Max(0.0f, NoiseSubsystem->GetMaskingAt(KnockLocation) - NoiseSubsystem->GetGlobalMasking())
		: 0.0f;
	FIGNoiseEvent Probe;
	Probe.Location = KnockLocation;
	Probe.Loudness = FMath::Max(0.0f, 0.35f - Masking);
	Probe.Radius = Probe.Loudness * UIGNoiseSubsystem::CarryPerLoudness;
	if (!CanHear(Probe))
	{
		return false;
	}
	return true;
}

void AIGListenerEntity::NotifyAnswerKnock(const FVector& KnockLocation)
{
	if (!CanHearAnswerFrom(KnockLocation))
	{
		return;
	}
	// 한 밤에 세 번. 네 번째부터 대답은 오지 않고 벽의 잔향만 남는다 — 그리고
	// 그는 그 자리를 들었다.
	if (AnswersThisNight >= IGListener::AnswersPerNight)
	{
		// 들은 자리는 소음과 같은 규칙으로 적는다. 문에 대고 안에서 친 대답이면 문
		// 앞에 와서 두드리고, 위층에서 친 대답이면 계단 아래까지 온다.
		const bool bFromAbove = NoteAnswerLocation(KnockLocation);
		bReactingToSound = true;
		const bool bWasLyingInWait = bSilentApproach;
		bSilentApproach = false;
		bObservationHold = false;
		// 쫓던 중이면 새 자리만으로 충분하다. 다만 위층의 대답은 닿을 수 없는 추격이
		// 되니 계단 아래까지 와서 위를 향해 두드린다(HandleNoise와 같다).
		const bool bWasChasing = State == EIGListenerState::Chasing;
		if (!bWasChasing || bFromAbove)
		{
			EnterState(EIGListenerState::Investigating);
			bCallFromAbove = bFromAbove;
			bKnockUpOnArrival = bFromAbove && bWasChasing;
			if (bWasLyingInWait)
			{
				PlayAlertVocal();
			}
		}
		return;
	}
	++AnswersThisNight;

	AnswerKnockLocation = KnockLocation;
	// 기다림이 끝나면 갈 자리를 지금 적는다. 문 안에서 친 대답인지는 대답한 그 순간
	// 그녀가 어디 있었는지로 가린다. 기다리는 동안에는 아무 소리도 이 자리를 바꾸지
	// 않는다(HandleNoise는 Waiting을 듣지 않는다).
	NoteAnswerLocation(KnockLocation);
	EnterState(EIGListenerState::Waiting);
	// It answers: two knocks. The reply every tester should get chills from.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateWallKnockReply(this),
		GetActorLocation(),
		0.9f,
		1.0f,
		240.0f,
		2600.0f,
		EIGAudioBus::Entity);
	// §19.8. 소리로 못 듣는 손에게도 대답이 통했다는 것이 닿아야 한다.
	EmitPresentationCue(GetActorLocation(), 0.6f, 2840.0f);
	OnKnocked.Broadcast(GetActorLocation());
	if (IsNearForCaption(GetActorLocation()))
	{
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "EntityAnswerReplyCaption", "대답하듯 두 번 두드리는 소리"),
			2.2f,
			GetActorLocation());
	}
}

void AIGListenerEntity::SetDormant(const bool bInDormant)
{
	if (bDormant == bInDormant)
	{
		return;
	}
	bDormant = bInDormant;
	if (!bDormant && ListenerSkeletal)
	{
		ListenerSkeletal->PrestreamTextures(10.f, false);
	}

	SetActorHiddenInGame(bDormant);
	SetActorEnableCollision(!bDormant);
	// Tick off is the whole dormancy: no state machine, no knocking, no
	// hearing, no threat pressure. Cheapest possible daytime.
	SetActorTickEnabled(!bDormant);

	if (bDormant)
	{
		if (DragLoopComponent)
		{
			DragLoopComponent->SetVolumeMultiplier(0.0f);
		}
		// 잠들면 숨도 멎는다. Tick이 꺼지므로 UpdateBreathLoop이 내려 주지 못한다.
		if (BreathLoopComponent && BreathLoopComponent->IsPlaying())
		{
			BreathLoopComponent->FadeOut(0.5f, 0.0f);
		}
		BreathVolumeTarget = 0.0f;
		// 추격 중에 새벽이 오면 스코어는 Calm으로 내려가는데 손은 계속 울고 있었다.
		// 패드를 켜고 끄는 곳은 EnterState뿐인데, 잠들 때는 상태를 거치지 않는다.
		if (AIGPlayerCharacter* PlayerCharacter =
			Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
		{
			PlayerCharacter->SetChaseHaptic(false);
		}
		bBeatHold = false;
		HomeDoorIgnoreUntil = -1000.0;
		// A knock window must never outlive the knocker into the day.
		if (NoiseSubsystem)
		{
			NoiseSubsystem->SetGlobalMasking(0.0f);
			// §5.6: 밤이 끝나면 히트맵은 절반으로 감쇠한다. 어제의 습관이 오늘
			// 완전히 사라지지는 않는다 — 조용히 걷기로 바꿨더라도 어제 시끄러웠던
			// 복도는 여전히 조금 더 위험하다.
			NoiseSubsystem->DecayHeatmapForNewNight();
		}
		bAmbushArmed = false;
		bSilentApproach = false;
		bObservationHold = false;
		bSilenceNextStopKnock = false;
		AnswersThisNight = 0;
		if (UWorld* World = GetWorld())
		{
			if (UIGMissingFloorAudioSubsystem* AudioDirector =
				World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
			{
				AudioDirector->SetThreatState(EIGAudioThreatState::Calm);
				// 엎드린 매복은 음악이 이미 Calm이라 위 호출이 청취 창을 걷지 못한다.
				AudioDirector->SetEntityListening(false);
				AudioDirector->SetEntityDistance(MAX_flt);
			}
		}
	}
	else
	{
		// Wake at the route start, impatience preserved.
		ResetToPatrolStart(/*bRaiseAggression=*/false);
	}
}

void AIGListenerEntity::BeginFinalePass(
	const FVector& StartLocation,
	const TArray<FVector>& RoutePoints,
	const float SpeedOverride)
{
	if (RoutePoints.Num() == 0)
	{
		return;
	}

	// Do not route through SetDormant(false): normal waking deliberately resets
	// to the patrol start, while this one authored beat begins behind Mok.
	bDormant = false;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(false);
	SetActorTickEnabled(true);
	TeleportTo(StartLocation, GetActorRotation(), false, true);
	FinaleRoutePoints = RoutePoints;
	FinaleRouteIndex = 0;
	FinaleSpeedOverride = SpeedOverride;
	bBeatHold = false;
	bReactingToSound = false;
	CachedPlayer.Reset();
	EnterState(EIGListenerState::FinaleLured);
}

void AIGListenerEntity::ParkForBeat(const FVector& Where, const float Yaw)
{
	// 연출 좌표는 바닥면 높이로 적혀 있다. 그대로 세우면 캡슐 중심이 바닥에
	// 놓여 몸 절반이 슬래브에 묻힌다. 문을 열어 본 플레이어가 그걸 봤다.
	FVector Parked = Where;
	if (UWorld* World = GetWorld())
	{
		FHitResult Floor;
		FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(IGListenerParkFloor), false, this);
		if (World->LineTraceSingleByChannel(
				Floor,
				Where + FVector(0.0f, 0.0f, 60.0f),
				Where - FVector(0.0f, 0.0f, 200.0f),
				ECC_Visibility,
				FloorParams))
		{
			Parked.Z = Floor.ImpactPoint.Z + Body->GetScaledCapsuleHalfHeight() + 2.0f;
		}
	}
	TeleportTo(Parked, FRotator(0.0f, Yaw, 0.0f), false, true);
	SetActorEnableCollision(true);
	PatrolIndex = 0;
	// 연출이 세운 자리에서 새로 시작한다. 쫓던 길과 놓친 흔적, 경계를 남기면 카메오가
	// 시작하자마자 엉뚱한 층으로 기어간다.
	NavPath.Reset();
	NavPathOnStair.Reset();
	PlayerTrail.Reset();
	AlertUntilSeconds = -1000.0;
	// 이 한 줄이 카메오를 성립시킨다. 남겨 두면 그는 조사 중인 상태로 서 있다가
	// 아까 들은 자리로 기어간다.
	bReactingToSound = false;
	bAmbushArmed = false;
	AnswerTapTimes.Reset();
	EnterState(EIGListenerState::Patrolling);
}

void AIGListenerEntity::ResetToPatrolStart(const bool bRaiseAggression)
{
	if (bRaiseAggression)
	{
		SetAggressionTier(AggressionTier + 1);
	}
	TeleportTo(SpawnLocation, GetActorRotation(), false, true);
	SetActorEnableCollision(true);
	PatrolIndex = 0;
	NavPath.Reset();
	NavPathOnStair.Reset();
	PlayerTrail.Reset();
	AlertUntilSeconds = -1000.0;
	StairPitchTarget = 0.0f;
	StairBodyPitch = 0.0f;
	FinaleRoutePoints.Reset();
	FinaleRouteIndex = 0;
	FinaleSpeedOverride = -1.0f;
	bBeatHold = false;
	bReactingToSound = false;
	bAmbushArmed = false;
	// 04:30으로 되감기면 문 앞을 떠난 기억도, 위층을 향한 노크의 간격도 처음부터다.
	HomeDoorIgnoreUntil = -1000.0;
	LastCeilingKnockSeconds = -1000.0;
	// 포획으로 되감긴 04:30에는 첫 칸에서 다시 두드린다. 천장 노크는 한 판에 한 번이다.
	bSilenceNextStopKnock = false;
	// The hour restarts at 04:30, so the air restarts with it (§5.4). Leaving
	// the lane behind would let a reset player read a path nobody walked.
	bDustTrailSeeded = false;
	if (UWorld* World = GetWorld())
	{
		if (UIGDustSubsystem* Dust = World->GetSubsystem<UIGDustSubsystem>())
		{
			Dust->ClearDisturbances();
			// The floor goes back to 04:30 too. A swept fifth floor is how the
			// player knows the hour really did restart, and leaving last loop's
			// tracks would have them searching where they have not been.
			Dust->ClearSettledPrints();
		}
	}
	// §5.6: the habit survives the reset. He does not forget where you have been
	// loud just because the clock went back to half past four — that memory is
	// the whole point of the heatmap, and it only halves when a night ends.
	RefreshNightTuning();
	EnterState(EIGListenerState::Patrolling);
}

// -- 다른 층의 소리와 닫힌 문 --------------------------------------------------

void AIGListenerEntity::ArriveBelowUpperSound(const bool bKeepHolding)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	// 두 번 들렸을 때만 위를 향해 두드린다. 밸브 한 번은 그를 계단 아래까지
	// 불러낼 뿐이고, 벽을 연달아 두드리는 급한 길은 발밑에서 올라오는 노크를
	// 산다(§7 P3). 연출이 건물을 멈춘 동안(P4의 8초)과 12초 안의 되풀이는
	// 듣기만 한다. 밤4 망치질마다 3연이 따라오면 안 된다.
	const bool bKnockUp = bKnockUpOnArrival
		&& !IsAuthoredSilenceActive()
		&& Now - LastCeilingKnockSeconds >= IGListener::CeilingKnockIntervalSeconds;
	// 소리가 난 쪽으로 고개를 든다. 보지 못하니 듣는 방향이 곧 얼굴 방향이다.
	const FVector ToSound = UpperSoundLocation - GetActorLocation();
	AttentionDirection = FVector(ToSound.X, ToSound.Y, 0.0f).GetSafeNormal();
	bKnockingUp = bKnockUp;
	bCadenceEarsUp = false;
	bAtHomeDoor = false;
	if (bKnockUp)
	{
		LastCeilingKnockSeconds = Now;
		EnterState(EIGListenerState::Banging);
	}
	else if (bKeepHolding && State == EIGListenerState::Holding)
	{
		// 위층 걸음마다 상태에 다시 들어가면 0.4초마다 미장이 갈라진다. 청취만
		// 처음부터 다시 재고, 몸을 고쳐 앉는 소리는 몇 초에 한 번이다.
		StateSeconds = 0.0f;
		PlayPlantSettle(0.6f);
	}
	else
	{
		EnterState(EIGListenerState::Holding);
	}
}

bool AIGListenerEntity::IsInsideHome(const FVector& Location) const
{
	return HomeInterior.IsValid && HomeInterior.IsInsideOrOn(Location);
}

bool AIGListenerEntity::IsHomeSoundAt(
	const FVector& Location,
	const AActor* Maker) const
{
	if (IsInsideHome(Location))
	{
		return true;
	}
	const AIGSwingDoor* Door = HomeDoor.Get();
	if (!Door)
	{
		return false;
	}
	// 문면의 소리만 따진다. 복도 멀리서 난 소리를 집 안 사람이 냈을 리 없다.
	if (FVector::DistSquared2D(Location, Door->GetActorLocation())
		> FMath::Square(200.0f))
	{
		return false;
	}
	// 문짝이 낸 소리는 문을 여닫은 사람이 어느 쪽에 있는지로 가린다.
	if (Maker == Door)
	{
		Maker = UGameplayStatics::GetPlayerPawn(this, 0);
	}
	const APawn* MakerPawn = Cast<APawn>(Maker);
	return MakerPawn && IsInsideHome(MakerPawn->GetActorLocation());
}

void AIGListenerEntity::NoteHeardLocation(
	const FVector& Location,
	const bool bHomeSound)
{
	LastHeardLocation = Location;
	FVector DoorCenter = FVector::ZeroVector;
	FVector Outward = FVector::ZeroVector;
	if (bHomeSound
		&& !IsInsideHome(Location)
		&& GetHomeDoorFrame(DoorCenter, Outward))
	{
		LastHeardLocation =
			DoorCenter - Outward * 25.0f + FVector(0.0f, 0.0f, 60.0f);
	}
}

bool AIGListenerEntity::NoteAnswerLocation(const FVector& Location)
{
	// 위에서 온 대답. 대답한 자리 바로 아래로 기어가면 엉뚱한 벽에 끼인다 — 5층
	// 공동 벽 아래는 403호 현관 앞이다. 위층 소리에 늘 가는 자리, 계단 아래로 간다.
	if (Location.Z - GetActorLocation().Z > FloorHeightThreshold
		&& (!BuildingNav.IsBuilt() || FIGBuildingNav::FloorOfFeet(Location.Z - 60.0f) >= 4))
	{
		const FVector Below = bHasStairFoot ? StairFoot : Location;
		LastHeardLocation = bHasStairFoot ? StairFoot : FVector(Below.X, Below.Y, GetActorLocation().Z);
		UpperSoundLocation = Location;
		return true;
	}
	// 문에 대고 친 대답은 경첩 자리로 온다. 집 안 상자 밖이라 그대로 적으면 문
	// 앞에 와서 두드리지 않고 문짝에 부딪혀 끼인다.
	const AIGSwingDoor* Door = HomeDoor.Get();
	NoteHeardLocation(
		Location,
		Door && !Door->IsOpen()
			&& IsHomeSoundAt(Location, UGameplayStatics::GetPlayerPawn(this, 0)));
	return false;
}

bool AIGListenerEntity::GetHomeDoorFrame(
	FVector& OutDoorCenter,
	FVector& OutOutward) const
{
	const AIGSwingDoor* Door = HomeDoor.Get();
	if (!Door)
	{
		return false;
	}
	// 경첩이 액터 원점이고 문짝은 액터의 +Y로 뻗으며, 바깥면(복도 쪽)은 +X다.
	OutOutward = Door->GetActorForwardVector().GetSafeNormal2D();
	OutDoorCenter = Door->GetActorLocation()
		+ Door->GetActorRightVector()
			* (AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f);
	return !OutOutward.IsNearlyZero();
}

bool AIGListenerEntity::ShouldGoToHomeDoor() const
{
	const AIGSwingDoor* Door = HomeDoor.Get();
	const UWorld* World = GetWorld();
	if (!Door || !World || Door->IsOpen() || !bReactingToSound)
	{
		return false;
	}
	if (FMath::Abs(Door->GetActorLocation().Z - GetActorLocation().Z)
		> FloorHeightThreshold)
	{
		return false;
	}
	return IsInsideHome(LastHeardLocation)
		&& !IsInsideHome(GetActorLocation())
		&& World->GetTimeSeconds() >= HomeDoorIgnoreUntil;
}

bool AIGListenerEntity::CanKnockHomeDoor() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UIGMissingFloorNarrativeSubsystem* Narrative = GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	if (!Narrative)
	{
		return true;
	}
	// 밤2 비트 2-1의 노크가 이 문에서 난 첫 노크여야 한다(「이번엔 현관문에서
	// 들렸다」). 그 비트는 끌려가는 소리까지 끝나야 기록되므로 비트 도중에도
	// 닫혀 있다.
	return Narrative->HasBeatPlayed(FName(TEXT("Night2.DoorKnock")))
		|| Narrative->GetNightIndex() >= 3;
}

void AIGListenerEntity::ArriveAtHomeDoor()
{
	FVector DoorCenter = FVector::ZeroVector;
	FVector Outward = FVector::ZeroVector;
	if (!GetHomeDoorFrame(DoorCenter, Outward))
	{
		EnterState(EIGListenerState::Holding);
		return;
	}
	// 문을 향해 돌아선다. 걸어온 방향 그대로 두드리면 옆벽을 치는 그림이 된다.
	AttentionDirection = -Outward;
	bAtHomeDoor = true;
	bDoorReknocked = false;
	bKnockingUp = false;
	bCadenceEarsUp = false;
	if (CanKnockHomeDoor())
	{
		// 세 번 두드리고, 문 앞에서 듣는다(§4.5).
		EnterState(EIGListenerState::Banging);
	}
	else
	{
		// 밤1과 밤2 대본 전. 문 앞에 서서 소리 없이 듣기만 한다.
		EnterState(EIGListenerState::Holding);
	}
}

void AIGListenerEntity::LeaveHomeDoor()
{
	// 두드렸고, 기다렸으니 떠난다. 닫힌 집 안의 소리는 한동안 흘려듣는다.
	// 곧장 되돌아오면 떠난 것이 아니다.
	if (const UWorld* World = GetWorld())
	{
		HomeDoorIgnoreUntil =
			World->GetTimeSeconds() + IGListener::HomeDoorIgnoreSeconds;
	}
	bReactingToSound = false;
	AdvancePatrolIndex();
	EnterState(EIGListenerState::Patrolling);
	// 문 안쪽에서 숨죽이던 그녀가 그제야 숨을 내쉰다.
	if (AIGPlayerCharacter* PlayerCharacter =
		Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		if (IsInsideHome(PlayerCharacter->GetActorLocation()))
		{
			if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
			{
				Stress->PlayReliefExhale();
			}
		}
	}
}

// -- locomotion ------------------------------------------------------------

float AIGListenerEntity::AdvanceGait(const float DeltaSeconds)
{
	// 걸음마다 빠르기를 새로 고른다. 수색 궤적처럼 해시로 골라서 같은 판은 같은
	// 박자로 기고 캡처가 매번 같은 자리를 찍는다. 여덟 걸음에 한 번꼴로 0.5~1.2초
	// 멈춘다. 몸이 서면 애니메이션은 듣는 자세로 넘어가고 끌림 소리도 끊긴다.
	// 나머지 걸음을 조금 빠르게 골라(0.9~1.35배) 평균 순찰 속도는 예전의 95%쯤이다.
	GaitSecondsLeft -= DeltaSeconds;
	if (GaitSecondsLeft <= 0.0f)
	{
		const uint32 Hash = (++GaitStepCount) * 2654435761u;
		const float Roll = ((Hash >> 8) & 0xFFFF) / 65535.0f;
		const float Span = ((Hash >> 22) & 0x3FF) / 1023.0f;
		if (Roll < 0.125f)
		{
			GaitTarget = 0.0f;
			GaitSecondsLeft = 0.5f + 0.7f * Span;
		}
		else
		{
			GaitTarget = 0.9f + 0.45f * Span;
			GaitSecondsLeft = 0.45f + 0.35f * Roll;
		}
	}
	GaitCurrent = FMath::FInterpTo(GaitCurrent, GaitTarget, DeltaSeconds, 9.0f);
	return GaitCurrent;
}

bool AIGListenerEntity::CrawlTowards(
	const FVector& Target,
	const float Speed,
	const float DeltaSeconds)
{
	bBlockedByHomeDoor = false;
	FVector ToTarget = Target - GetActorLocation();
	// Crawling cannot climb: steer on the floor plane and let collisions
	// keep it honest about stairs it has not been routed through.
	ToTarget.Z = 0.0f;
	const float Distance = ToTarget.Size();
	if (Distance <= 24.0f)
	{
		return true;
	}

	const FVector Direction = ToTarget / Distance;
	const FVector Step =
		Direction * FMath::Min(Speed * DeltaSeconds, Distance);
	FaceDirection(Direction, DeltaSeconds);

	const FVector Before = GetActorLocation();
	// 계단이 층을 잇게 된 뒤로 그가 기는 바닥 끝에 내려가는 단이 있다. 기는
	// 몸은 높이를 버리고 미끄러지므로, 그대로 두면 계단 위 허공을 기어 간다.
	// 걸음 끝에 바닥이 없으면 벽에 막힌 것과 같이 서게 한다. 지금 선 자리에
	// 바닥이 없을 때(순간이동 직후 따위)는 막지 않는다 — 그 자리를 벗어나야 한다.
	if (!HasFloorBeneath(Before + Step) && HasFloorBeneath(Before))
	{
		LastMoveSpeed = 0.0f;
		StuckSeconds += DeltaSeconds;
		if (StuckSeconds >= 1.5f)
		{
			StuckSeconds = 0.0f;
			return true;
		}
		return false;
	}
	FHitResult SweepHit;
	AddActorWorldOffset(Step, true, &SweepHit);
	// 닫힌 403호 문짝에 막혔는지. 벽에 걸린 것과 문 앞에 닿은 것은 다르다.
	bBlockedByHomeDoor = SweepHit.bBlockingHit
		&& HomeDoor.IsValid()
		&& SweepHit.GetActor() == HomeDoor.Get();
	// 다른 문(관리실 문, 계단실 방화문)에 막혔다. 문짝에 대고 미끄러지지 않는다.
	if (SweepHit.bBlockingHit && !bBlockedByHomeDoor && Cast<AIGSwingDoor>(SweepHit.GetActor()))
	{
		bBlockedByClosedDoor = true;
		BlockingDoor = SweepHit.GetActor();
	}
	const float Moved =
		FVector::Dist2D(Before, GetActorLocation());
	LastMoveSpeed = DeltaSeconds > 0.0f ? Moved / DeltaSeconds : 0.0f;

	// Wedged against geometry: treat the spot as reached instead of
	// grinding on a wall forever. The search sweep will wander it free.
	if (Moved < Speed * DeltaSeconds * 0.15f)
	{
		StuckSeconds += DeltaSeconds;
		if (StuckSeconds >= 1.5f)
		{
			UE_LOG(LogIndieGame, Verbose, TEXT("LISTENER_WEDGED at=%s toward=%s blocker=%s"),
				*GetActorLocation().ToCompactString(), *Target.ToCompactString(),
				SweepHit.GetComponent() ? *SweepHit.GetComponent()->GetReadableName() : TEXT("floor-edge"));
			StuckSeconds = 0.0f;
			return true;
		}
	}
	else
	{
		StuckSeconds = 0.0f;
	}
	return false;
}

bool AIGListenerEntity::HasFloorBeneath(const FVector& Location) const
{
	const UWorld* World = GetWorld();
	if (!World || !Body)
	{
		return true;
	}
	// 몸 바닥에서 12 cm 아래까지만 본다. 문턱은 바닥이고, 한 단(16.7 cm) 아래는
	// 허공이다. 구의 아랫면이 그 깊이에서 멈추도록 반지름만큼 덜 내린다. 구로
	// 훑는 것은 슬래브 이음매의 실금으로 빠지지 않게 하려는 것이다.
	constexpr float ProbeRadius = 8.0f;
	const float Reach = Body->GetScaledCapsuleHalfHeight() + 12.0f - ProbeRadius;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGListenerFloor), false, this);
	if (const APawn* Player = CachedPlayer.Get())
	{
		Params.AddIgnoredActor(Player);
	}
	FHitResult Hit;
	return World->SweepSingleByChannel(
		Hit,
		Location,
		Location - FVector(0.0f, 0.0f, Reach),
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(ProbeRadius),
		Params);
}

void AIGListenerEntity::AdvancePatrolIndex()
{
	const int32 NodeCount = PatrolPoints.Num();
	if (NodeCount <= 0)
	{
		PatrolIndex = 0;
		return;
	}

	const int32 NextIndex = (PatrolIndex + 1) % NodeCount;
	const UWorld* World = GetWorld();
	UIGNoiseSubsystem* Noise = NoiseSubsystem;
	if (Tuning.HeatmapWeight <= 0.0f || !Noise || !World || NodeCount < 3)
	{
		// 밤1은 히트맵 가중이 0이다. 첫 밤의 순찰은 배울 수 있는 순서여야 한다.
		PatrolIndex = NextIndex;
		return;
	}

	// The route order is the baseline and the heatmap is a bias on top: the next
	// stop keeps a head start so patrols still read as a round, and a stop only
	// jumps the queue when the player has genuinely been loud near it.
	int32 BestIndex = NextIndex;
	float BestScore = 1.0f;
	for (int32 Offset = 0; Offset < NodeCount; ++Offset)
	{
		const int32 Candidate = (PatrolIndex + 1 + Offset) % NodeCount;
		if (Candidate == PatrolIndex)
		{
			// Standing still is not a patrol.
			continue;
		}
		const float Heat = Noise->GetHeatAt(PatrolPoints[Candidate]);
		// The in-order stop starts at 1.0; anything else has to out-argue it.
		const float Score = (Candidate == NextIndex ? 1.0f : 0.0f)
			+ Heat * Tuning.HeatmapWeight * 2.0f;
		if (Score > BestScore)
		{
			BestScore = Score;
			BestIndex = Candidate;
		}
	}
	PatrolIndex = BestIndex;
}

bool AIGListenerEntity::TryBeginAmbush()
{
	if (!Tuning.bTierThreeAmbushAllowed || AggressionTier < 3 || bAmbushArmed)
	{
		return false;
	}
	UIGNoiseSubsystem* Noise = NoiseSubsystem;
	if (!Noise)
	{
		return false;
	}
	FVector Hottest = FVector::ZeroVector;
	float Heat = 0.0f;
	if (!Noise->GetHottestZone(Hottest, Heat) || Heat < 0.5f)
	{
		// Without a habit there is nothing to lie in wait for, and guessing
		// would make the ambush feel arbitrary instead of earned.
		return false;
	}

	// Keep his own floor: the hottest zone is a statistic, and dragging himself
	// through a slab to reach it is not something the state machine can do.
	// 습관이 위층에 있으면(밤3의 5층 퍼즐) 그 아래 자기 층 자리는 슬래브 너머라
	// 엉뚱한 벽에 가 붙는다. 위층 소리에 늘 가는 자리, 그녀가 내려올 계단 아래에
	// 엎드린다. 아래층의 습관은 갈 길이 없으니 매복하지 않는다.
	const float Rise = Hottest.Z - GetActorLocation().Z;
	const bool bHabitAbove = Rise > FloorHeightThreshold;
	if (Rise < -FloorHeightThreshold || (bHabitAbove && !bHasStairFoot))
	{
		return false;
	}
	const FVector Spot = bHabitAbove ? StairFoot : Hottest;
	AmbushLocation = FVector(Spot.X, Spot.Y, GetActorLocation().Z);
	bAmbushArmed = true;
	// Investigating walks him there; arriving hands over to Holding. 들숨도 조사
	// 드론도 없이 순찰 빠르기로 기어가 조용히 엎드린다. 남는 경고는 심장과 압박
	// 층, 가라앉은 숨, 그리고 세계가 참는 숨(청취 창의 −6dB)뿐이다. 다 기다리면
	// 그 자리에서 두드린다.
	LastHeardLocation = AmbushLocation;
	bReactingToSound = false;
	bSilentApproach = true;
	bObservationHold = false;
	EnterState(EIGListenerState::Investigating);
	return true;
}

void AIGListenerEntity::FaceDirection(
	const FVector& Direction,
	const float DeltaSeconds,
	const float TurnDegreesPerSecond)
{
	if (Direction.IsNearlyZero())
	{
		return;
	}
	const FRotator Current = GetActorRotation();
	const FRotator Desired = Direction.Rotation();
	const FRotator Next(
		0.0f,
		FMath::FixedTurn(Current.Yaw, Desired.Yaw, TurnDegreesPerSecond * DeltaSeconds),
		0.0f);
	SetActorRotation(Next);
}

// -- 층을 오가는 길 -------------------------------------------------------------

namespace
{
	/**
	 * 목표 자리 밑의 바닥. 소리 자리는 그녀의 몸 가운데 높이이고 순찰 점은 그의 몸
	 * 가운데 높이라, 그대로는 어느 층인지 가를 수 없다. 내려 그어 바닥을 찾는다.
	 */
	FVector ResolveFloorBelow(
		const UWorld* World,
		const FVector& Point,
		const AActor* Self,
		const AActor* Player)
	{
		if (World)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(IGListenerGoalFloor), false, Self);
			if (Player)
			{
				Params.AddIgnoredActor(Player);
			}
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(
					Hit,
					Point + FVector(0.0f, 0.0f, 20.0f),
					Point - FVector(0.0f, 0.0f, 260.0f),
					ECC_Visibility,
					Params))
			{
				return FVector(Point.X, Point.Y, Hit.ImpactPoint.Z);
			}
		}
		return FVector(Point.X, Point.Y, Point.Z - 96.0f);
	}
}

FVector AIGListenerEntity::GetFeetLocation() const
{
	const float HalfHeight = Body ? Body->GetScaledCapsuleHalfHeight() : 58.0f;
	return GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight + 2.0f);
}

bool AIGListenerEntity::CanCrawlStraightTo(const FVector& GoalFeet) const
{
	const FVector Feet = GetFeetLocation();
	if (FIGBuildingNav::FloorOfFeet(GoalFeet.Z) != FIGBuildingNav::FloorOfFeet(Feet.Z)
		|| FMath::Abs(GoalFeet.Z - Feet.Z) > 30.0f)
	{
		return false;
	}
	// 계단탑을 드나드는 걸음은 늘 길로 간다. 층 참 끝에서 곧게 가면 계단 위 허공으로
	// 나가고, 두 띠 사이 벽을 사이에 둔 자리는 곧게 닿지 않는다.
	const bool bFeetInCore = FIGBuildingNav::IsInStairCore(Feet);
	if (bFeetInCore != FIGBuildingNav::IsInStairCore(GoalFeet)
		|| (bFeetInCore
			&& (AIGPrologueWorldScene::IsOnStairFlight(GoalFeet)
				|| AIGPrologueWorldScene::IsOnStairFlight(Feet))))
	{
		return false;
	}
	const AActor* Blocker = nullptr;
	if (IsCrawlLineClear(Feet, GoalFeet, &Blocker))
	{
		return true;
	}
	// 닫힌 403호 문짝에 막히는 것은 곧은 길이다. 문 앞에서 두드리는 일은 문이 맡는다.
	return HomeDoor.IsValid() && Blocker == HomeDoor.Get();
}

bool AIGListenerEntity::IsCrawlLineClear(
	const FVector& FromFeet,
	const FVector& ToFeet,
	const AActor** OutBlocker) const
{
	const UWorld* World = GetWorld();
	if (!World || !Body)
	{
		return true;
	}
	// 실제로 기어 가는 몸과 같은 캡슐로 쓸어 본다. 가는 선으로 재면 문틀 모서리를
	// 스치는 길도 곧은 길이 되어, 몸이 그 모서리에 걸려 선다. 바닥 이음매에 걸리지
	// 않게 아래쪽만 조금 띄운다.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGListenerStraight), false, this);
	if (const APawn* Player = CachedPlayer.Get())
	{
		Params.AddIgnoredActor(Player);
	}
	const float Radius = Body->GetScaledCapsuleRadius() + 1.0f;
	const float HalfHeight = FMath::Max(Body->GetScaledCapsuleHalfHeight() - 4.0f, Radius);
	const FVector Lift(0.0f, 0.0f, Body->GetScaledCapsuleHalfHeight() + 6.0f);
	FHitResult Hit;
	if (!World->SweepSingleByChannel(
			Hit,
			FromFeet + Lift,
			ToFeet + Lift,
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight),
			Params))
	{
		return true;
	}
	if (OutBlocker)
	{
		*OutBlocker = Hit.GetActor();
	}
	return false;
}

bool AIGListenerEntity::PlanNavPath(const FVector& GoalFeet)
{
	NavPath.Reset();
	NavPathOnStair.Reset();
	NavPathIndex = 0;
	UWorld* World = GetWorld();
	if (!World || !BuildingNav.IsBuilt())
	{
		return false;
	}
	const FVector Feet = GetFeetLocation();
	const int32 From = BuildingNav.FindNearest(World, Feet, this);
	const int32 To = BuildingNav.FindNearest(World, GoalFeet, this);
	TArray<int32> Route;
	if (From == INDEX_NONE || To == INDEX_NONE || !BuildingNav.FindPath(From, To, Route))
	{
		return false;
	}
	const TArray<FIGBuildingNavNode>& Nodes = BuildingNav.GetNodes();
	// 이미 첫 점을 지나 둘째 점 쪽에 있으면 첫 점으로 되돌아가지 않는다. 벽을 사이에
	// 두고 있으면 지름길이 아니라 벽 뚫기라 되돌아간다.
	if (Route.Num() >= 2)
	{
		const FVector First = Nodes[Route[0]].Feet;
		const FVector Second = Nodes[Route[1]].Feet;
		// 계단 위는 디딤판을 따라 움직이므로 같은 띠 위에서만 건너뛴다.
		const bool bStairSkip = Nodes[Route[0]].bOnStair && Nodes[Route[1]].bOnStair;
		if (FVector::Dist(Feet, Second) < FVector::Dist(First, Second) + 10.0f
			&& (bStairSkip
				|| (FMath::Abs(Second.Z - Feet.Z) < 20.0f && IsCrawlLineClear(Feet, Second))))
		{
			Route.RemoveAt(0);
		}
	}
	for (const int32 Node : Route)
	{
		NavPath.Add(Nodes[Node].Feet);
		NavPathOnStair.Add(Nodes[Node].bOnStair);
	}
	// 끝 점은 목표 그 자체다. 계단 위 목표는 가까운 디딤판 점까지만 간다.
	if (!FIGBuildingNav::IsInStairCore(GoalFeet)
		&& FVector::DistSquared(GoalFeet, NavPath.Last()) > FMath::Square(30.0f))
	{
		NavPath.Add(GoalFeet);
		NavPathOnStair.Add(false);
	}
	NavGoal = GoalFeet;
	NavPlannedSeconds = World->GetTimeSeconds();
	return NavPath.Num() > 0;
}

bool AIGListenerEntity::LegCrossesClosedFireDoor(const FVector& FromFeet, const FVector& ToFeet)
{
	if (!bStairFireDoorResolved)
	{
		bStairFireDoorResolved = true;
		for (TActorIterator<AIGFireDoorWedge> It(GetWorld()); It; ++It)
		{
			StairFireDoor = It->GetDoor();
			break;
		}
	}
	const AIGSwingDoor* Door = StairFireDoor.Get();
	if (!Door || Door->IsOpen())
	{
		return false;
	}
	// 문짝은 경첩에서 액터의 +Y로 120 cm 뻗는다. 닫히면 X가 경첩과 같은 평면이다.
	const FVector Hinge = Door->GetActorLocation();
	if (FMath::Abs(FromFeet.Z - Hinge.Z) > 150.0f && FMath::Abs(ToFeet.Z - Hinge.Z) > 150.0f)
	{
		return false;
	}
	const float FromSide = FromFeet.X - Hinge.X;
	const float ToSide = ToFeet.X - Hinge.X;
	if (FromSide * ToSide > 0.0f || FMath::IsNearlyEqual(FromSide, ToSide))
	{
		return false;
	}
	const float Alpha = FromSide / (FromSide - ToSide);
	const float CrossY = FMath::Lerp(FromFeet.Y, ToFeet.Y, Alpha);
	return CrossY > Hinge.Y - 20.0f && CrossY < Hinge.Y + 140.0f;
}

void AIGListenerEntity::ArriveAtClosedDoor()
{
	bBlockedByClosedDoor = false;
	if (const AActor* Door = BlockingDoor.Get())
	{
		const FVector ToDoor = Door->GetComponentsBoundingBox().GetCenter() - GetActorLocation();
		AttentionDirection = FVector(ToDoor.X, ToDoor.Y, 0.0f).GetSafeNormal();
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	bKnockingUp = false;
	bCadenceEarsUp = false;
	bAtHomeDoor = false;
	if (Now - LastClosedDoorKnockSeconds >= IGListener::CeilingKnockIntervalSeconds)
	{
		// §3.1 닫힌 방화문 너머에서 소리가 났다. 세 번 두드리고, 문에 귀를 대고 듣는다.
		LastClosedDoorKnockSeconds = Now;
		EnterState(EIGListenerState::Banging);
	}
	else
	{
		EnterState(EIGListenerState::Holding);
	}
}

bool AIGListenerEntity::StepAlongStair(
	const FVector& TargetFeet,
	const float Speed,
	const float DeltaSeconds)
{
	const FVector Feet = GetFeetLocation();
	const FVector Delta = TargetFeet - Feet;
	const float Distance = Delta.Size();
	bOnStairLeg = true;
	const float HalfHeight = Body ? Body->GetScaledCapsuleHalfHeight() : 58.0f;
	if (Distance <= 6.0f)
	{
		// 점에 닿으면 높이를 그 점에 맞춘다. 다음 칸이 평지면 쓸며 가는 걸음이 이어받는다.
		SetActorLocation(TargetFeet + FVector(0.0f, 0.0f, HalfHeight + 2.0f));
		return true;
	}
	// 네 발로 디딤판을 짚는다. 오를 때가 조금 더 느리다.
	const float Scale = Delta.Z > 2.0f
		? IGListener::StairClimbSpeedScale
		: IGListener::StairDescendSpeedScale;
	const float Step = FMath::Min(Speed * Scale * DeltaSeconds, Distance);
	SetActorLocation(Feet + Delta / Distance * Step + FVector(0.0f, 0.0f, HalfHeight + 2.0f));
	const FVector Flat(Delta.X, Delta.Y, 0.0f);
	if (!Flat.IsNearlyZero())
	{
		// 반 층 참에서 몸을 돌려 다음 띠로 꺾는다. 평지보다 빨리 돌아야 몸이 옆으로
		// 미끄러지지 않는다.
		FaceDirection(Flat.GetSafeNormal(), DeltaSeconds, 330.0f);
		StairPitchTarget = FMath::Clamp(
			FMath::RadiansToDegrees(FMath::Atan2(Delta.Z, Flat.Size())),
			-IGListener::StairPitchLimit,
			IGListener::StairPitchLimit);
	}
	LastMoveSpeed = DeltaSeconds > 0.0f ? Step / DeltaSeconds : 0.0f;
	StuckSeconds = 0.0f;
	if (Distance - Step <= 6.0f)
	{
		SetActorLocation(TargetFeet + FVector(0.0f, 0.0f, HalfHeight + 2.0f));
		return true;
	}
	return false;
}

bool AIGListenerEntity::MoveTowardGoal(
	const FVector& Goal,
	const float Speed,
	const float DeltaSeconds)
{
	bBlockedByClosedDoor = false;
	UWorld* World = GetWorld();
	if (!World || !BuildingNav.IsBuilt())
	{
		return CrawlTowards(Goal, Speed, DeltaSeconds);
	}
	const FVector GoalFeet = ResolveFloorBelow(World, Goal, this, CachedPlayer.Get());
	if (CanCrawlStraightTo(GoalFeet))
	{
		NavPath.Reset();
		NavPathOnStair.Reset();
		NavPathIndex = 0;
		const bool bArrived = CrawlTowards(Goal, Speed, DeltaSeconds);
		return bArrived || bBlockedByClosedDoor;
	}
	const double Now = World->GetTimeSeconds();
	const bool bReplan = !NavPath.IsValidIndex(NavPathIndex)
		|| FVector::DistSquared(GoalFeet, NavGoal) > FMath::Square(IGListener::ReplanGoalShift)
		|| (State == EIGListenerState::Chasing
			&& Now - NavPlannedSeconds >= IGListener::ChaseReplanSeconds);
	if (bReplan && !PlanNavPath(GoalFeet))
	{
		// 이어지는 길이 없다. 예전처럼 같은 층을 기어 간다.
		return CrawlTowards(Goal, Speed, DeltaSeconds);
	}
	const FVector Feet = GetFeetLocation();
	const FVector Target = NavPath[NavPathIndex];
	if (LegCrossesClosedFireDoor(Feet, Target))
	{
		// 닫힌 방화문 앞. 문을 뚫고 지나가지 않는다.
		bBlockedByClosedDoor = true;
		BlockingDoor = StairFireDoor.Get();
		NavPath.Reset();
		NavPathOnStair.Reset();
		NavPathIndex = 0;
		return true;
	}
	// 계단탑 안은 미리 고른 길(띠 가운데, 층 참 가운데)만 지나므로 쓸지 않고 따라간다.
	// 디딤판 경사에서 층 참으로 올라선 몸은 참 바닥보다 몇 cm 낮아서, 쓸며 가면 참
	// 끝에 걸린다.
	const bool bStairLeg = NavPathOnStair[NavPathIndex]
		|| (NavPathIndex > 0 && NavPathOnStair[NavPathIndex - 1])
		|| FMath::Abs(Target.Z - Feet.Z) > 1.0f
		|| (FIGBuildingNav::IsInStairCore(Feet) && FIGBuildingNav::IsInStairCore(Target));
	bool bReached = false;
	if (bStairLeg)
	{
		bReached = StepAlongStair(Target, Speed, DeltaSeconds);
	}
	else
	{
		// 평지 칸은 원래 걸음 그대로 쓸며 간다. 끝 점(목표 자체)은 목표의 높이를 써야
		// 문 앞 판정이 맞는다.
		const bool bLast = NavPathIndex == NavPath.Num() - 1;
		const FVector FlatTarget = bLast
			? Goal
			: FVector(Target.X, Target.Y, GetActorLocation().Z);
		bReached = CrawlTowards(FlatTarget, Speed, DeltaSeconds);
		if (bBlockedByClosedDoor)
		{
			// 닫힌 문. 밀고 들어가지 않는다. 부르는 쪽이 문 앞에서 두드릴지 정한다.
			NavPath.Reset();
			NavPathOnStair.Reset();
			NavPathIndex = 0;
			return true;
		}
	}
	if (!bReached)
	{
		return false;
	}
	++NavPathIndex;
	if (NavPathIndex >= NavPath.Num())
	{
		NavPath.Reset();
		NavPathOnStair.Reset();
		NavPathIndex = 0;
		return true;
	}
	return false;
}

void AIGListenerEntity::UpdateStairPitch(const float DeltaSeconds)
{
	if (!bOnStairLeg)
	{
		StairPitchTarget = 0.0f;
	}
	bOnStairLeg = false;
	if (!ListenerSkeletal || State == EIGListenerState::CaptureHold)
	{
		return;
	}
	const float Previous = StairBodyPitch;
	StairBodyPitch = FMath::FInterpTo(StairBodyPitch, StairPitchTarget, DeltaSeconds, 7.0f);
	if (FMath::Abs(StairBodyPitch) < 0.05f && FMath::Abs(StairPitchTarget) < 0.05f)
	{
		StairBodyPitch = 0.0f;
	}
	if (!FMath::IsNearlyEqual(Previous, StairBodyPitch, 0.01f))
	{
		// 메시 원점은 몸 가운데 바닥이다. 그 점을 축으로 눕히면 손은 윗단, 발은 아랫단에 닿는다.
		ListenerSkeletal->SetRelativeRotation(FRotator(StairBodyPitch, 0.0f, 0.0f));
	}
}

// -- 놓친 뒤 -------------------------------------------------------------------

void AIGListenerEntity::NotePlayerTrail(const FVector& Location, const double Seconds)
{
	// 같은 순간 같은 자리의 소리(한 소리가 두 번 보고된 것)는 한 번만 적는다.
	if (PlayerTrail.Num() > 0
		&& FMath::Abs(PlayerTrail.Last().Seconds - Seconds) < 0.05
		&& FVector::DistSquared(PlayerTrail.Last().Location, Location) < FMath::Square(20.0f))
	{
		return;
	}
	FHeardMark& Mark = PlayerTrail.AddDefaulted_GetRef();
	Mark.Location = Location;
	Mark.Seconds = Seconds;
	while (PlayerTrail.Num() > IGListener::TrailCapacity)
	{
		PlayerTrail.RemoveAt(0);
	}
}

bool AIGListenerEntity::PredictPlayerHeading(FVector& OutPoint) const
{
	const UWorld* World = GetWorld();
	if (!World || !BuildingNav.IsBuilt() || PlayerTrail.Num() < 2)
	{
		return false;
	}
	// 가장 최근 소리와, 이어 볼 만한 것 중 가장 먼저 난 소리를 잇는다. 연달은 발소리
	// 둘은 너무 가까워서 방향이 흔들린다.
	const FHeardMark& Last = PlayerTrail.Last();
	const FHeardMark* Earlier = nullptr;
	for (int32 Index = PlayerTrail.Num() - 2; Index >= 0; --Index)
	{
		if (Last.Seconds - PlayerTrail[Index].Seconds > IGListener::TrailLinkSeconds)
		{
			break;
		}
		Earlier = &PlayerTrail[Index];
	}
	if (!Earlier)
	{
		return false;
	}
	const FVector Heading = Last.Location - Earlier->Location;
	const FVector Flat(Heading.X, Heading.Y, 0.0f);
	if (Flat.Size() < 40.0f && FMath::Abs(Heading.Z) < 60.0f)
	{
		return false;
	}
	const FVector FlatDirection = Flat.GetSafeNormal();
	const float Rising = FMath::Abs(Heading.Z) >= 60.0f ? FMath::Sign(Heading.Z) : 0.0f;
	const FVector LastFeet = ResolveFloorBelow(World, Last.Location, this, CachedPlayer.Get());
	const int32 Start = BuildingNav.FindNearest(World, LastFeet, this);
	if (Start == INDEX_NONE)
	{
		return false;
	}
	TArray<TPair<int32, float>> Reach;
	BuildingNav.GatherWithin(Start, IGListener::MomentumMaxDistance, Reach);
	const TArray<FIGBuildingNavNode>& Nodes = BuildingNav.GetNodes();
	float BestScore = 0.35f;
	int32 Best = INDEX_NONE;
	for (const TPair<int32, float>& Entry : Reach)
	{
		if (Entry.Value < IGListener::MomentumMinDistance)
		{
			continue;
		}
		const FVector ToNode = Nodes[Entry.Key].Feet - LastFeet;
		const FVector ToNodeFlat(ToNode.X, ToNode.Y, 0.0f);
		float Score = FlatDirection.IsNearlyZero() || ToNodeFlat.IsNearlyZero()
			? 0.0f
			: FVector::DotProduct(ToNodeFlat.GetSafeNormal(), FlatDirection);
		// 계단을 오르내리던 소리면 같은 쪽 층으로 이어지는 점이 앞선다.
		if (Rising != 0.0f && FMath::Abs(ToNode.Z) > 60.0f)
		{
			Score += FMath::Sign(ToNode.Z) == Rising ? 0.8f : -0.8f;
		}
		Score -= Entry.Value / IGListener::MomentumMaxDistance * 0.15f;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Entry.Key;
		}
	}
	if (Best == INDEX_NONE)
	{
		return false;
	}
	const float HalfHeight = Body ? Body->GetScaledCapsuleHalfHeight() : 58.0f;
	OutPoint = Nodes[Best].Feet + FVector(0.0f, 0.0f, HalfHeight + 2.0f);
	return true;
}

void AIGListenerEntity::BuildSearchPlan()
{
	SearchSpots.Reset();
	SearchFacings.Reset();
	SearchSpotIsHiding.Reset();
	SearchSpotIndex = 0;
	bSearchPausing = false;
	SearchPauseLeft = 0.0f;
	SearchBudgetSeconds = SearchSecondsForNight();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float HalfHeight = Body ? Body->GetScaledCapsuleHalfHeight() + 2.0f : 60.0f;
	const FVector AnchorFeet = ResolveFloorBelow(World, SearchAnchor, this, CachedPlayer.Get());
	const int32 AnchorFloor = FIGBuildingNav::FloorOfFeet(AnchorFeet.Z);

	struct FCandidate
	{
		FVector Spot;
		FVector Facing;
		bool bHiding;
		float Score;
	};
	TArray<FCandidate> Candidates;

	// 숨을 만한 가구. 놓친 자리 가까이, 그녀의 마지막 소리 가까이일수록 먼저 들른다.
	for (TActorIterator<AIGHidingSpot> It(World); It; ++It)
	{
		const AIGHidingSpot* Spot = *It;
		const FVector Approach = Spot->GetApproachLocation();
		if (FIGBuildingNav::FloorOfFeet(Approach.Z) != AnchorFloor)
		{
			continue;
		}
		// 닫힌 403호 안은 뒤지지 않는다. 그 문 앞에서는 두드리고 기다렸다 떠난다(§4.5).
		if (IsInsideHome(Approach) && HomeDoor.IsValid() && !HomeDoor->IsOpen())
		{
			continue;
		}
		const float Distance = FVector::Dist(Approach, AnchorFeet);
		if (Distance > IGListener::SearchHidingReach)
		{
			continue;
		}
		float Score = 1.25f - Distance / 1500.0f;
		for (const FHeardMark& Mark : PlayerTrail)
		{
			if (FVector::Dist2D(Mark.Location, Approach) < 400.0f)
			{
				Score += 0.6f;
				break;
			}
		}
		FVector Facing = Spot->GetActorLocation() - Approach;
		Facing.Z = 0.0f;
		Candidates.Add({Approach + FVector(0.0f, 0.0f, HalfHeight), Facing.GetSafeNormal(), true, Score});
	}

	// 문 앞, 빈방, 복도 끝.
	const int32 Start = BuildingNav.IsBuilt()
		? BuildingNav.FindNearest(World, AnchorFeet, this)
		: INDEX_NONE;
	if (Start != INDEX_NONE)
	{
		TArray<TPair<int32, float>> Reach;
		BuildingNav.GatherWithin(Start, IGListener::SearchReach, Reach);
		const TArray<FIGBuildingNavNode>& Nodes = BuildingNav.GetNodes();
		for (const TPair<int32, float>& Entry : Reach)
		{
			const FIGBuildingNavNode& Node = Nodes[Entry.Key];
			if (!Node.bLookout || Node.bOnStair)
			{
				continue;
			}
			FVector Facing = Node.Feet - AnchorFeet;
			Facing.Z = 0.0f;
			// 복도의 문 앞이면 문을 본다. 귀를 문짝에 댄다.
			if (Node.Feet.Y > -290.0f && Node.Feet.Y < -240.0f)
			{
				Facing = FVector(0.0f, 1.0f, 0.0f);
			}
			Candidates.Add({
				Node.Feet + FVector(0.0f, 0.0f, HalfHeight),
				Facing.GetSafeNormal(),
				false,
				1.0f - Entry.Value / IGListener::SearchReach});
		}
	}

	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });
	TArray<FCandidate> Chosen;
	for (const FCandidate& Candidate : Candidates)
	{
		bool bDuplicate = false;
		for (const FCandidate& Taken : Chosen)
		{
			if (FVector::DistSquared(Taken.Spot, Candidate.Spot) < FMath::Square(120.0f))
			{
				bDuplicate = true;
				break;
			}
		}
		if (!bDuplicate)
		{
			Chosen.Add(Candidate);
		}
		if (Chosen.Num() >= IGListener::SearchSpotCount)
		{
			break;
		}
	}

	// 순서: 그녀가 가던 쪽을 먼저, 나머지는 가까운 것부터 이어서.
	FVector From = GetActorLocation();
	FVector Heading = FVector::ZeroVector;
	if (PredictPlayerHeading(Heading))
	{
		SearchSpots.Add(Heading);
		const FVector Toward = Heading - SearchAnchor;
		SearchFacings.Add(FVector(Toward.X, Toward.Y, 0.0f).GetSafeNormal());
		SearchSpotIsHiding.Add(false);
		From = Heading;
	}
	while (Chosen.Num() > 0)
	{
		int32 Nearest = 0;
		for (int32 Index = 1; Index < Chosen.Num(); ++Index)
		{
			if (FVector::DistSquared(Chosen[Index].Spot, From)
				< FVector::DistSquared(Chosen[Nearest].Spot, From))
			{
				Nearest = Index;
			}
		}
		SearchSpots.Add(Chosen[Nearest].Spot);
		SearchFacings.Add(Chosen[Nearest].Facing.IsNearlyZero()
			? GetActorForwardVector()
			: Chosen[Nearest].Facing);
		SearchSpotIsHiding.Add(Chosen[Nearest].bHiding);
		From = Chosen[Nearest].Spot;
		Chosen.RemoveAtSwap(Nearest);
	}
	// 갈 자리가 하나도 없으면(건물 길 밖) 놓친 자리에라도 가서 듣는다.
	if (SearchSpots.Num() == 0)
	{
		SearchSpots.Add(SearchAnchor);
		SearchFacings.Add(GetActorForwardVector());
		SearchSpotIsHiding.Add(false);
	}
	UE_LOG(LogIndieGame, Display, TEXT("LISTENER_SEARCH spots=%d budget=%.1f anchor=%s"),
		SearchSpots.Num(), SearchBudgetSeconds, *SearchAnchor.ToCompactString());
	for (int32 Index = 0; Index < SearchSpots.Num(); ++Index)
	{
		UE_LOG(LogIndieGame, Verbose, TEXT("LISTENER_SEARCH_SPOT %d at=%s hiding=%d"),
			Index, *SearchSpots[Index].ToCompactString(), SearchSpotIsHiding[Index] ? 1 : 0);
	}
}

float AIGListenerEntity::SearchSecondsForNight() const
{
	int32 Night = 1;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UIGMissingFloorNarrativeSubsystem* Narrative =
				GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
		{
			Night = FMath::Clamp(Narrative->GetNightIndex(), 1, 4);
		}
	}
	// 첫 밤은 규칙을 배우는 밤이라 짧게, 갈수록 집요하게. 잡힐수록(티어) 더 오래 뒤진다.
	float Seconds = 24.0f;
	switch (Night)
	{
	case 2: Seconds = 32.0f; break;
	case 3: Seconds = 38.0f; break;
	case 4: Seconds = 44.0f; break;
	default: break;
	}
	Seconds += 3.0f * FMath::Clamp(AggressionTier, 0, 3);
	if (Difficulty == EIGNightDifficulty::Quiet)
	{
		Seconds *= 0.75f;
	}
	return Seconds;
}

bool AIGListenerEntity::IsAlert() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < AlertUntilSeconds;
}

const FVector* AIGListenerEntity::CurrentPatrolTarget() const
{
	if (PatrolPoints.Num() == 0)
	{
		return nullptr;
	}
	return &PatrolPoints[PatrolIndex % PatrolPoints.Num()];
}

// -- presentation ----------------------------------------------------------

bool AIGListenerEntity::BuildSkeletalBody()
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(
		nullptr, TEXT("/Game/Meshes/SK_ListenerCrawler.SK_ListenerCrawler"));
	if (!Mesh)
	{
		return false;
	}
	const auto LoadAnim = [](const TCHAR* Path)
	{
		return LoadObject<UAnimSequence>(nullptr, Path);
	};
	CrawlAnim = LoadAnim(TEXT("/Game/Meshes/A_ListenerCrawler_Crawl.A_ListenerCrawler_Crawl"));
	ListenAnim = LoadAnim(TEXT("/Game/Meshes/A_ListenerCrawler_Listen.A_ListenerCrawler_Listen"));
	BangAnim = LoadAnim(TEXT("/Game/Meshes/A_ListenerCrawler_Bang.A_ListenerCrawler_Bang"));
	LungeAnim = LoadAnim(TEXT("/Game/Meshes/A_ListenerCrawler_Lunge.A_ListenerCrawler_Lunge"));
	if (!CrawlAnim || !ListenAnim)
	{
		// 기는 동작 없는 뼈대는 서 있는 조각이다. 정적 셸이 낫다.
		UE_LOG(LogIndieGame, Warning, TEXT("SK_ListenerCrawler animations missing; static shell fallback"));
		CrawlAnim = nullptr;
		ListenAnim = nullptr;
		return false;
	}

	USkeletalMeshComponent* Component =
		NewObject<USkeletalMeshComponent>(this, TEXT("ListenerSkeletalBody"));
	Component->RegisterComponent();
	Component->AttachToComponent(
		Body, FAttachmentTransformRules::KeepRelativeTransform);
	Component->SetSkeletalMesh(Mesh);
	Component->PrestreamTextures(10.f, false);
	// 메시 원점은 바닥 중심, 머리가 +X. 캡슐 원점은 바닥에서 58cm.
	Component->SetRelativeLocation(FVector(0.0f, 0.0f, -58.0f));
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(true);
	// 기는 동작이 바운드 밖으로 팔을 뻗는다. 잘리면 손이 사라진다.
	Component->SetBoundsScale(1.8f);
	// 화면 밖에서도 박자를 지킨다. 발소리가 동작 위상에서 나오기 때문이다.
	Component->VisibilityBasedAnimTickOption =
		EVisibilityBasedAnimTickOption::AlwaysTickPose;
	Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	ListenerSkeletal = Component;
	PlayBodyAnim(EIGListenerBodyAnim::Listen, true, 1.0f);
	return true;
}

void AIGListenerEntity::PlayBodyAnim(
	const EIGListenerBodyAnim Anim,
	const bool bLoop,
	const float Rate)
{
	if (!ListenerSkeletal)
	{
		return;
	}
	UAnimSequence* Sequence = nullptr;
	switch (Anim)
	{
	case EIGListenerBodyAnim::Crawl: Sequence = CrawlAnim; break;
	case EIGListenerBodyAnim::Listen: Sequence = ListenAnim; break;
	case EIGListenerBodyAnim::Bang: Sequence = BangAnim ? BangAnim.Get() : ListenAnim.Get(); break;
	case EIGListenerBodyAnim::Lunge: Sequence = LungeAnim ? LungeAnim.Get() : CrawlAnim.Get(); break;
	default: break;
	}
	if (!Sequence)
	{
		return;
	}
	if (ActiveBodyAnim != Anim)
	{
		ListenerSkeletal->PlayAnimation(Sequence, bLoop);
		ActiveBodyAnim = Anim;
	}
	ListenerSkeletal->SetPlayRate(Rate);
}

float AIGListenerEntity::ComputeCrawlRate(const float Speed) const
{
	// Crawl 한 주기는 1.2초. 순찰 속도(110)에서 1.0, 추격에서는 2.8까지만 —
	// 그 위는 팔이 떨리는 것으로 보이지 빨라 보이지 않는다.
	const float Reference = FMath::Max(CrawlSpeed, 1.0f);
	return FMath::Clamp(0.35f + 0.65f * (Speed / Reference), 0.35f, 2.8f);
}

void AIGListenerEntity::UpdateSkeletalPose(
	const float SpeedAlpha,
	const float BodyRate,
	const float DeltaSeconds)
{
	if (!ListenerSkeletal)
	{
		return;
	}
	switch (State)
	{
	case EIGListenerState::CaptureHold:
	{
		// 넘어지는 동안 한 번 물러나 웅크렸다가, 점점 빨라지며 눈앞으로 달려들고 화면이
		// 끊기는 순간 얼굴이 닿는다. 도착해서 머무는 시간은 없다. 눈앞에서 서성이면
		// 무섭지 않고 어색했다. 0.12초마다 0.045초씩 멈춰 서서 한 장씩 찍어 넘긴 인형처럼
		// 끊겨 보인다.
		const AIGPlayerCharacter* Victim = CaptureVictim.Get();
		const float CutSeconds = Victim && Victim->GetCaptureCutSeconds() > 0.0f
			? Victim->GetCaptureCutSeconds()
			: 0.95f;
		const bool bFrozen = FMath::Fmod(StateSeconds, 0.12f) < 0.045f;
		PlayBodyAnim(EIGListenerBodyAnim::Crawl, true, bFrozen ? 0.0f : 2.6f);
		if (!bFrozen)
		{
			const float Phase = FMath::Clamp(StateSeconds / CutSeconds, 0.0f, 1.0f);
			const FVector Coil = CaptureFallenEye + CaptureStrikeLine * 70.0f - FVector(0, 0, 8);
			const FVector Wanted = Phase < 0.45f
				? FMath::Lerp(CaptureFaceStart, Coil,
					FMath::InterpEaseOut(0.0f, 1.0f, Phase / 0.45f, 2.0f))
				: FMath::Lerp(Coil, CaptureViewTarget,
					FMath::Pow((Phase - 0.45f) / 0.55f, 2.4f));
			ListenerSkeletal->AddWorldOffset(Wanted - GetCaptureFaceLocation());
		}
		if (CaptureKeyLight)
		{
			// 손전등이 바닥에 떨어진 뒤에야 얼굴에 빛이 닿는다.
			const UIGFlashlightComponent* Torch = Victim ? Victim->GetFlashlight() : nullptr;
			const float Peak = Torch && Torch->IsProvidingLight() ? 260.0f : 110.0f;
			CaptureKeyLight->SetIntensity(
				Peak * FMath::SmoothStep(0.30f * CutSeconds, 0.60f * CutSeconds, StateSeconds));
		}
		return;
	}
	case EIGListenerState::Banging:
		// 노크 소리와 같은 2.1초짜리 동작. 한 번 재생하고 듣기로 넘어간다. 티어가
		// 올라 노크가 빨라지면 손도 같은 배율로 빨라져야 소리와 닿는 순간이 맞는다.
		PlayBodyAnim(EIGListenerBodyAnim::Bang, false, KnockRate);
		return;
	case EIGListenerState::Chasing:
		if (bLungeArmed)
		{
			PlayBodyAnim(EIGListenerBodyAnim::Lunge, false, 1.0f);
			return;
		}
		break;
	case EIGListenerState::Waiting:
		// 대답에 얼어붙는다. 숨도 멈춘 것처럼 보이도록 재생을 세운다. 기다림이
		// 닳아 가면 숨이 느리게 돌아온다.
		PlayBodyAnim(EIGListenerBodyAnim::Listen, true, bWaitStirred ? 0.6f : 0.0f);
		return;
	default:
		break;
	}
	if (SpeedAlpha > 0.03f)
	{
		PlayBodyAnim(EIGListenerBodyAnim::Crawl, true, BodyRate);
		return;
	}
	// 멈춰 있다. 추격 중이라 거칠면 숨이 빠르다.
	const float ListenRate = State == EIGListenerState::Chasing ? 1.8f : 1.0f;
	PlayBodyAnim(EIGListenerBodyAnim::Listen, true, ListenRate);
}

void AIGListenerEntity::BuildGreyboxBody()
{
	if (BuildSkeletalBody())
	{
		return;
	}
	// The release path is one authored static crawl pose built from the
	// ImageGen anatomy sheet. It keeps contact shadow, flashlight parallax and
	// a continuous human silhouette; the primitive assembly below is a safe
	// editor/source-only fallback while generated assets are unavailable.
	if (UStaticMesh* ListenerMesh = LoadObject<UStaticMesh>(
			nullptr,
			TEXT("/Game/Meshes/SM_ListenerEntityCrawl.SM_ListenerEntityCrawl")))
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(
			this, TEXT("ListenerBody"));
		Component->RegisterComponent();
		Component->AttachToComponent(
			Body, FAttachmentTransformRules::KeepRelativeTransform);
		Component->SetStaticMesh(ListenerMesh);
		// The capsule origin is 58 cm above the floor. 접지 오프셋은 메시의
		// 가장 낮은 점에서 계산한다 — 예전 절차 셸은 발끝이 Z -31이라 -27을
		// 박아 두었는데, TRELLIS.2에서 다듬은 셸은 원점이 바닥 중심이라 그
		// 값을 그대로 쓰면 58 cm 떠 버린다.
		constexpr float CapsuleOriginAboveFloor = 58.0f;
		const FBoxSphereBounds MeshBounds = ListenerMesh->GetBounds();
		const float LowestZ = MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z;
		Component->SetRelativeLocation(
			FVector(0.0f, 0.0f, -CapsuleOriginAboveFloor - LowestZ));
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(true);
		if (UMaterialInterface* PlasterMaterial = LoadObject<UMaterialInterface>(
				nullptr,
				TEXT("/Game/Prototype/Materials/"
					 "M_MissingFloorListenerPlasterUV."
					 "M_MissingFloorListenerPlasterUV")))
		{
			// 석고도 숨을 쉰다. WPO 진폭 파라미터(BreathAmplitude,
			// TremorAmplitude)를 상태 머신이 조종할 수 있게 셸만 MID로 감싼다.
			ListenerShellMid =
				UMaterialInstanceDynamic::Create(PlasterMaterial, Component);
			Component->SetMaterial(
				0,
				ListenerShellMid
					? static_cast<UMaterialInterface*>(ListenerShellMid)
					: PlasterMaterial);
		}
		ListenerShell = Component;
		BodyBlocks.Add(Component);

		// The chase is staged head-on in a long, narrow corridor. Preserve the
		// human anatomy from the approved ImageGen front view with one lit,
		// masked PBR layer at that authored angle. The continuous 3D shell stays
		// hidden-but-shadow-casting underneath, so the card never creates a flat
		// rectangular shadow and the floor contact remains physically grounded.
		UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		UMaterialInterface* FrontMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/Prototype/Materials/M_SpriteListenerFront."
				 "M_SpriteListenerFront"));
		static const TCHAR* PhaseMaterialPaths[] = {
			TEXT("/Game/Prototype/Materials/M_SpriteListenerCrawl0."
				 "M_SpriteListenerCrawl0"),
			TEXT("/Game/Prototype/Materials/M_SpriteListenerCrawl1."
				 "M_SpriteListenerCrawl1"),
			TEXT("/Game/Prototype/Materials/M_SpriteListenerCrawl2."
				 "M_SpriteListenerCrawl2"),
			TEXT("/Game/Prototype/Materials/M_SpriteListenerCrawl3."
				 "M_SpriteListenerCrawl3"),
		};
		for (const TCHAR* PhasePath : PhaseMaterialPaths)
		{
			if (UMaterialInterface* PhaseMaterial =
				LoadObject<UMaterialInterface>(nullptr, PhasePath))
			{
				ListenerPhaseMaterials.Add(PhaseMaterial);
			}
		}
		if (ListenerPhaseMaterials.Num() != UE_ARRAY_COUNT(PhaseMaterialPaths))
		{
			// Never advance a partial sequence: one absent phase would make the
			// identity and lighting flash. The approved still is a safe fallback.
			ListenerPhaseMaterials.Reset();
		}
		UMaterialInterface* CardMaterial =
			ListenerPhaseMaterials.Num() > 0
				? ListenerPhaseMaterials[0].Get()
				: FrontMaterial;
		if (PlaneMesh && CardMaterial)
		{
			UStaticMeshComponent* FrontCard = NewObject<UStaticMeshComponent>(
				this, TEXT("ListenerFrontCard"));
			FrontCard->RegisterComponent();
			FrontCard->AttachToComponent(
				Body, FAttachmentTransformRules::KeepRelativeTransform);
			FrontCard->SetStaticMesh(PlaneMesh);
			FrontCard->SetMaterial(0, CardMaterial);
			// Roll makes texture V vertical; +90 yaw makes the plane normal
			// follow the pawn's +X forward axis. At 85 cm card height the crop's
			// lower 9% margin resolves to the floor at this exact center height.
			// A 128 cm width stays inside the 4F corridor instead of clipping
			// through a dwelling wall when the patrol line hugs one side.
			FrontCard->SetRelativeLocation(FVector(38.0f, 0.0f, -23.0f));
			FrontCard->SetRelativeRotation(FRotator(0.0f, 90.0f, 90.0f));
			FrontCard->SetRelativeScale3D(FVector(1.28f, 0.85f, 1.0f));
			FrontCard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			FrontCard->SetCanEverAffectNavigation(false);
			FrontCard->SetCastShadow(false);
			FrontCard->SetHiddenInGame(true);
			ListenerFrontCard = FrontCard;
			BodyBlocks.Add(FrontCard);
		}
		return;
	}

	UStaticMesh* Cube =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Cube || !Sphere)
	{
		return;
	}

	struct FBlock
	{
		UStaticMesh* Mesh = nullptr;
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector Scale = FVector::OneVector;
	};

	// Upper body raised on its elbows, faceless head, legs trailing flat.
	// Proportions matter more than detail: the silhouette must read as a
	// person the moment a flashlight edge clips it.
	const FBlock Blocks[] =
	{
		{Cube, FVector(6.0f, 0.0f, -8.0f), FRotator(-16.0f, 0.0f, 0.0f), FVector(0.52f, 0.40f, 0.30f)},
		{Sphere, FVector(34.0f, 0.0f, 14.0f), FRotator::ZeroRotator, FVector(0.20f, 0.22f, 0.26f)},
		{Cube, FVector(24.0f, -20.0f, -22.0f), FRotator(24.0f, 8.0f, 0.0f), FVector(0.42f, 0.10f, 0.10f)},
		{Cube, FVector(24.0f, 20.0f, -22.0f), FRotator(24.0f, -8.0f, 0.0f), FVector(0.42f, 0.10f, 0.10f)},
		{Cube, FVector(-38.0f, -9.0f, -34.0f), FRotator(-3.0f, 4.0f, 0.0f), FVector(0.58f, 0.11f, 0.09f)},
		{Cube, FVector(-38.0f, 9.0f, -34.0f), FRotator(-3.0f, -4.0f, 0.0f), FVector(0.58f, 0.11f, 0.09f)},
	};

	int32 BlockIndex = 0;
	for (const FBlock& Block : Blocks)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(
			this,
			*FString::Printf(TEXT("ListenerBlock%d"), BlockIndex++));
		Component->RegisterComponent();
		Component->AttachToComponent(
			Body, FAttachmentTransformRules::KeepRelativeTransform);
		Component->SetStaticMesh(Block.Mesh);
		Component->SetRelativeLocation(Block.Location);
		Component->SetRelativeRotation(Block.Rotation);
		Component->SetRelativeScale3D(Block.Scale);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);

		// Source-only fallback: bone-grey and fully rough under a flashlight.
		if (UMaterialInterface* BaseMaterial = Component->GetMaterial(0))
		{
			UMaterialInstanceDynamic* Mid =
				UMaterialInstanceDynamic::Create(BaseMaterial, Component);
			Mid->SetVectorParameterValue(
				TEXT("Color"), FLinearColor(0.62f, 0.60f, 0.56f));
			Component->SetMaterial(0, Mid);
		}
		BodyBlocks.Add(Component);
	}
}

void AIGListenerEntity::UpdatePresentationLayer()
{
	if (!ListenerShell || !ListenerFrontCard)
	{
		return;
	}

	if (!CachedPlayer.IsValid())
	{
		for (TActorIterator<AIGPlayerCharacter> It(GetWorld()); It; ++It)
		{
			CachedPlayer = *It;
			break;
		}
	}

	const APawn* Player = CachedPlayer.Get();
	bool bUseFrontCard = false;
	if (Player)
	{
		FVector ToPlayer = Player->GetActorLocation() - GetActorLocation();
		ToPlayer.Z = 0.0f;
		const float Distance = ToPlayer.Size();
		const float Facing = Distance > KINDA_SMALL_NUMBER
			? FVector::DotProduct(GetActorForwardVector(), ToPlayer / Distance)
			: 1.0f;

		// Hysteresis prevents one-frame popping at either threshold. The PBR
		// layer remains over the grounded 3D shadow shell through the narrow
		// head-on chase, then yields before the 110 cm capture boundary or as
		// soon as the player gets a readable side angle.
		if (bFrontCardActive)
		{
			bUseFrontCard = Distance > 125.0f && Facing > 0.35f;
		}
		else
		{
			bUseFrontCard = Distance > 160.0f && Facing > 0.60f;
		}
	}

	if (bUseFrontCard == bFrontCardActive)
	{
		return;
	}
	bFrontCardActive = bUseFrontCard;
	ListenerFrontCard->SetHiddenInGame(!bFrontCardActive);
	ListenerShell->SetHiddenInGame(bFrontCardActive);
	ListenerShell->SetCastHiddenShadow(bFrontCardActive);
}

void AIGListenerEntity::UpdatePresentationPose(
	const float CurrentSpeed,
	const float DeltaSeconds)
{
	const bool bSkeletal = ListenerSkeletal != nullptr;
	if (!bSkeletal && (!ListenerShell || !ListenerFrontCard))
	{
		return;
	}

	// Smooth speed before it controls cadence. A single blocked sweep must not
	// snap a crawling shoulder from full extension straight into an idle pose.
	PresentationSpeed = State == EIGListenerState::Waiting
		? 0.0f
		: FMath::FInterpTo(
			PresentationSpeed, CurrentSpeed, DeltaSeconds, 7.5f);
	const float SpeedAlpha = FMath::Clamp(
		PresentationSpeed / FMath::Max(ChaseSpeed, 1.0f), 0.0f, 1.0f);
	const float BodyRate = bSkeletal ? ComputeCrawlRate(PresentationSpeed) : 0.0f;
	if (SpeedAlpha > 0.01f)
	{
		// 스켈레탈은 1.2초 주기에 팔꿈치가 두 번 닿는다. 위상 4가 한 주기다.
		const float FramesPerSecond = bSkeletal
			? BodyRate * (4.0f / 1.2f)
			: FMath::Lerp(1.6f, 6.0f, SpeedAlpha);
		ListenerPhase = FMath::Fmod(
			ListenerPhase + DeltaSeconds * FramesPerSecond, 4.0f);
		// 팔꿈치가 바닥을 치는 자세(0과 2)마다 한 걸음. 끌림 루프는 속도만 말하고
		// 걸음은 박자를 말한다. 변주는 걸음 번호 해시의 피치로.
		const int32 StepIndex = FMath::FloorToInt(ListenerPhase * 0.5f);
		if (StepIndex != LastCrawlStepIndex && SpeedAlpha > 0.08f && !bDormant)
		{
			LastCrawlStepIndex = StepIndex;
			const uint32 StepHash = static_cast<uint32>(StateSeconds * 37.0f + ListenerPhase * 1000.0f) * 2654435761u;
			const float Pitch = 0.94f + 0.12f * ((StepHash >> 8) & 0xFF) / 255.0f;
			// 걸음은 소모음이다. 추격 속도에서 0.2초마다 쏟아져도 스팅어·덮침·들숨을
			// 밀어내지 못하고, 자리가 모자라면 걸음끼리 밀린다.
			const FVector StepAt = GetActorLocation() + FVector(20.0f, 0.0f, -40.0f);
			IGAudio::SpawnExpendableOneShotAt(
				this,
				IGAudio::SampleVariantOr(
					TEXT("Entity_CrawlStep"), 3, StepHash,
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateEntityCrawlStep(this, bDragSurfaceIsVinyl); }),
				StepAt,
				0.35f + 0.55f * SpeedAlpha,
				Pitch,
				200.0f,
				2200.0f,
				EIGAudioBus::Entity);
			// 계단 철판을 짚는 손바닥과 무릎. 사람 발소리보다 낮고 둔하게, 계단실을 타고
			// 위아래 층까지 울린다. 문 너머로 들리는 이 소리가 그가 계단에 있다는 신호다.
			if (bDragSurfaceIsMetalStair)
			{
				IGAudio::SpawnExpendableOneShotAt(
					this,
					IGAudio::SampleVariantOr(
						TEXT("Foot_MetalStair"), 5, StepHash >> 5,
						[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSurfaceFootstep(this, EIGFootstepSurface::MetalStair, 0.72f, 0.9f); }),
					StepAt,
					0.5f + 0.45f * SpeedAlpha,
					Pitch * 0.74f,
					260.0f,
					3200.0f,
					EIGAudioBus::Entity);
			}
			// §19.8 「존재의 노크·접근」. 6m 안에서 기는 걸음만 대체 채널에 보낸다.
			// 자막은 붙이지 않는다 — 기본값이 켜짐이라 밤새 글이 뜨고 위치를 거저 준다.
			const double StepNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			const double CueInterval = State == EIGListenerState::Chasing
				? IGListener::ChaseApproachCueIntervalSeconds
				: IGListener::ApproachCueIntervalSeconds;
			if (StepNow - LastApproachCueSeconds >= CueInterval
				&& EmitPresentationCue(StepAt, 0.3f, IGListener::ApproachCueCentimeters))
			{
				LastApproachCueSeconds = StepNow;
			}
		}
	}

	if (bSkeletal)
	{
		UpdateSkeletalPose(SpeedAlpha, BodyRate, DeltaSeconds);
		return;
	}

	if (ListenerPhaseMaterials.Num() == 4)
	{
		const int32 PhaseIndex =
			FMath::Clamp(FMath::FloorToInt(ListenerPhase), 0, 3);
		if (PhaseIndex != ListenerPhaseIndex)
		{
			ListenerFrontCard->SetMaterial(
				0, ListenerPhaseMaterials[PhaseIndex]);
			ListenerPhaseIndex = PhaseIndex;
		}
	}

	// 생체 신호 진폭. 흉곽 팽창과 잔떨림 자체는 셸 재질의 WPO가 만들고,
	// 여기서는 상태에 맞는 진폭만 정한다. 들을 때는 저도 숨을 죽이고, 쫓을
	// 때는 거칠어지고, 대답 노크에 얼어붙은 Waiting은 완전한 정지다 — 배운
	// 답이 통했다는 확인을 몸으로 보여 준다.
	float TargetBreath = 0.45f;
	float TargetTremor = 0.1f;
	switch (State)
	{
	case EIGListenerState::Waiting:
		TargetBreath = 0.0f;
		TargetTremor = 0.0f;
		// 기다림이 닳아 가면 멎었던 숨이 조금 돌아온다.
		if (bWaitStirred)
		{
			TargetBreath = 0.16f;
		}
		break;
	case EIGListenerState::Listening:
	case EIGListenerState::Holding:
		TargetBreath = 0.16f;
		TargetTremor = 0.05f;
		break;
	case EIGListenerState::Chasing:
		TargetBreath = 0.8f;
		TargetTremor = 0.3f;
		break;
	default:
		break;
	}
	// 얼어붙는 쪽은 사람이 숨을 삼키는 속도, 풀리는 쪽은 한 호흡.
	const float InterpSpeed =
		State == EIGListenerState::Waiting && !bWaitStirred ? 9.0f : 3.0f;
	ShellBreathAmplitude = FMath::FInterpTo(
		ShellBreathAmplitude, TargetBreath, DeltaSeconds, InterpSpeed);
	ShellTremorAmplitude = FMath::FInterpTo(
		ShellTremorAmplitude, TargetTremor, DeltaSeconds, InterpSpeed);
	if (ListenerShellMid)
	{
		ListenerShellMid->SetScalarParameterValue(
			TEXT("BreathAmplitude"), ShellBreathAmplitude);
		ListenerShellMid->SetScalarParameterValue(
			TEXT("TremorAmplitude"), ShellTremorAmplitude);
	}

	// The source poses supply elbow/leg changes. These sub-centimetre motions
	// blend their weight across frames without lifting the crop from the floor.
	// 카드의 숨 바운스도 같은 진폭을 따른다: 셸이 멎는데 카드만 계속
	// 오르내리면 1.6m 경계에서 정지의 의미가 새어 버린다.
	const float TimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const float BreathScale = ShellBreathAmplitude / 0.45f;
	const float Breath = FMath::Sin(TimeSeconds * 1.35f) * BreathScale;
	const float Stride = FMath::Sin(ListenerPhase * HALF_PI);
	const float WeightShift = Stride * SpeedAlpha;
	ListenerFrontCard->SetRelativeLocation(FVector(
		38.0f,
		WeightShift * 1.6f,
		-23.0f + Breath * 0.18f));
	ListenerFrontCard->SetRelativeRotation(FRotator(
		0.0f,
		90.0f + WeightShift * 0.6f,
		90.0f + WeightShift * 0.9f));
	ListenerShell->SetRelativeLocation(FVector(
		0.0f, WeightShift * 0.55f, -27.0f));
	ListenerShell->SetRelativeRotation(FRotator(
		0.0f, WeightShift * 0.3f, 0.0f));
}

float AIGListenerEntity::RollKnockTempo()
{
	// 티어가 오를수록 노크가 급해진다. 짧아진 청취 창을 박자로 먼저 알린다. 사이클마다
	// 빠르기(±2%)와 세기가 조금씩 흔들려서, 한 밤에 수십 번 나도 배경음이 되지 않는다.
	static constexpr float TierKnockTempo[4] = {1.00f, 1.05f, 1.10f, 1.16f};
	const int32 EffectiveTier = Difficulty == EIGNightDifficulty::Hasty
		? FMath::Max(AggressionTier, 1) : AggressionTier;
	const uint32 KnockHash = static_cast<uint32>(++KnockSerial) * 2654435761u;
	const float Sway = 0.98f + 0.04f * ((KnockHash >> 8) & 0xFF) / 255.0f;
	KnockRate = TierKnockTempo[FMath::Clamp(EffectiveTier, 0, 3)] * Sway;
	return 0.86f + 0.14f * ((KnockHash >> 16) & 0xFF) / 255.0f;
}

void AIGListenerEntity::PlayKnockTriple()
{
	const float KnockVolume = RollKnockTempo();
	const FVector KnockAt = GetActorLocation() + FVector(0.0f, 0.0f, 40.0f);
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Entity_KnockTriple"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockTriple(this, 0.0f); }),
		KnockAt,
		KnockVolume,
		KnockRate,
		300.0f,
		3600.0f,
		EIGAudioBus::Entity);
	// §5.5. 밤2의 대본 노크만 테이프에 구멍을 내고 있었다. 그가 순찰 중에
	// 두드리는 것도 같은 소리다 — 어느 밤에 켜 둔 폰이든 그의 자리는 빈다.
	UWorld* World = GetWorld();
	if (World)
	{
		if (UIGRecordingSubsystem* Recording =
			World->GetSubsystem<UIGRecordingSubsystem>())
		{
			Recording->RecordEntitySound(GetActorLocation(), 0.55f, this);
		}
	}
	// §19.8. 「그가 두드리는 동안 움직여라」는 이 소리로 배우는 규칙이다. 듣기
	// 어려운 손에게도 두드리는 창이 닿아야 한다. 자막은 12초에 한 번만.
	EmitPresentationCue(KnockAt, 1.0f, 3900.0f);
	OnKnocked.Broadcast(KnockAt);
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - LastKnockCaptionSeconds >= IGListener::KnockCaptionIntervalSeconds
		&& IsNearForCaption(KnockAt))
	{
		LastKnockCaptionSeconds = Now;
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "EntityKnockCaption", "세 번 두드리는 소리"),
			2.4f,
			KnockAt);
	}
}

void AIGListenerEntity::PlayCeilingKnock()
{
	// 4층 천장 바로 밑을 친다. 위층에서는 발밑 슬래브를 타고 먹먹하게 올라온다.
	// 같은 녹음을 벽 너머로 걸러 둔 판이다. 순찰 노크와 같은 손이라 티어 빠르기도 같다.
	const FVector KnockPoint =
		GetActorLocation() + FVector(0.0f, 0.0f, IGListener::CeilingKnockHeight);
	const float KnockVolume = RollKnockTempo();
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Entity_KnockTriple_Muffled"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockTriple(this, 0.78f); }),
		KnockPoint,
		KnockVolume,
		KnockRate,
		300.0f,
		3600.0f,
		EIGAudioBus::Entity);
	if (UWorld* World = GetWorld())
	{
		if (UIGRecordingSubsystem* Recording =
			World->GetSubsystem<UIGRecordingSubsystem>())
		{
			Recording->RecordEntitySound(KnockPoint, 0.55f, this);
		}
	}
	// §19.8. 위층의 그녀에게 방위 딱지가 「아래」를 붙인다. 천장 노크는 이미
	// 12초에 한 번이라 자막 간격을 따로 두지 않는다.
	EmitPresentationCue(KnockPoint, 0.8f, 3900.0f);
	OnKnocked.Broadcast(KnockPoint);
	if (IsNearForCaption(KnockPoint))
	{
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "EntityCeilingKnockCaption", "먹먹하게 세 번 두드리는 소리"),
			2.4f,
			KnockPoint);
	}
}

void AIGListenerEntity::PlayHomeDoorKnock(const bool bSingle)
{
	FVector DoorCenter = FVector::ZeroVector;
	FVector Outward = FVector::ZeroVector;
	if (!GetHomeDoorFrame(DoorCenter, Outward))
	{
		if (!bSingle)
		{
			PlayKnockTriple();
		}
		return;
	}
	// 복도 쪽 문짝 겉면, 엎드린 그가 손을 뻗는 높이.
	const FVector KnockPoint =
		FVector(DoorCenter.X, DoorCenter.Y, GetActorLocation().Z + 40.0f)
		+ Outward * IGListener::HomeDoorKnockOffset;
	DoorKnockPoint = KnockPoint;
	// 문 노크는 밤2 대본 노크와 같은 간격이어야 알아듣는다. 티어 빠르기를 싣지 않고,
	// 두드리는 동작도 제 속도로 돈다.
	KnockRate = 1.0f;
	// 철문 녹음이 없으면 합성이 혼자 예전 크기로 낸다.
	const bool bSteel = IGAudio::Sample(IGListener::DoorSteelSamples[0]) != nullptr;
	USoundBase* Knock = bSingle
		? static_cast<USoundBase*>(UIGToneSequenceSoundWave::CreateWallKnockSingle(
			this, IGListener::DoorKnockSingleMuffle))
		: static_cast<USoundBase*>(UIGToneSequenceSoundWave::CreateWallKnockTriple(
			this, IGListener::DoorKnockMuffle));
	IGAudio::SpawnOneShotAt(
		this,
		Knock,
		KnockPoint,
		bSteel ? IGListener::DoorKnockUnderlayVolume : IGListener::DoorKnockVolume,
		1.0f,
		IGListener::DoorKnockInnerRadius,
		IGListener::DoorKnockFalloff,
		EIGAudioBus::Entity);
	// 첫 타는 지금 친다. 3연의 나머지 둘은 두드리는 동안 TickState가 친다.
	DoorSteelHitsPlayed = bSteel ? 0 : IGListener::DoorSteelHitCount;
	if (bSteel)
	{
		PlayHomeDoorSteelHit();
		if (bSingle)
		{
			DoorSteelHitsPlayed = IGListener::DoorSteelHitCount;
		}
	}
	// 대답해 두고 403호를 빠져나간 그녀에게도 기다림이 끝나면 이 노크가 난다. 방위
	// 자막은 다른 노크처럼 가까이에서만 붙인다.
	if (IsNearForCaption(KnockPoint))
	{
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			bSingle
				? NSLOCTEXT("IGMissingFloor", "EntityDoorKnockOnceCaption", "문 너머에서 한 번 두드리는 소리")
				: NSLOCTEXT("IGMissingFloor", "EntityDoorKnockCaption", "문 너머에서 연달아 세 번 두드리는 소리"),
			bSingle ? 1.6f : 2.4f,
			KnockPoint);
	}
	// §19.8. 문 너머의 노크도 진동과 파문으로 온다.
	EmitPresentationCue(
		KnockPoint,
		bSingle ? 0.7f : 1.0f,
		IGListener::DoorKnockInnerRadius + IGListener::DoorKnockFalloff);
	OnKnocked.Broadcast(KnockPoint);
	if (UWorld* World = GetWorld())
	{
		if (UIGRecordingSubsystem* Recording =
			World->GetSubsystem<UIGRecordingSubsystem>())
		{
			Recording->RecordEntitySound(KnockPoint, 0.55f, this);
		}
	}
	// 놀라는 것은 그 문 안에 있거나 같은 층 문 앞에 있을 때다. 계단이나 5층에서
	// 멀리 듣는 노크에 숨이 걸리면 안 된다.
	if (AIGPlayerCharacter* PlayerCharacter =
		Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		const FVector PlayerAt = PlayerCharacter->GetActorLocation();
		const bool bAtThisDoor =
			FMath::Abs(PlayerAt.Z - KnockPoint.Z) <= FloorHeightThreshold
			&& FVector::Dist2D(PlayerAt, KnockPoint)
				<= IGListener::DoorKnockStartleCentimeters;
		UIGStressComponent* Stress = PlayerCharacter->GetStress();
		if (Stress && (IsInsideHome(PlayerAt) || bAtThisDoor))
		{
			Stress->ApplyScare(bSingle ? 0.2f : 0.25f);
		}
	}
}

void AIGListenerEntity::PlayHomeDoorSteelHit()
{
	const int32 Hit = DoorSteelHitsPlayed;
	if (Hit < 0 || Hit >= IGListener::DoorSteelHitCount)
	{
		return;
	}
	++DoorSteelHitsPlayed;
	if (USoundBase* Steel = IGAudio::Sample(IGListener::DoorSteelSamples[Hit]))
	{
		IGAudio::SpawnOneShotAt(
			this,
			Steel,
			DoorKnockPoint,
			IGListener::DoorKnockVolume,
			IGListener::DoorSteelPitches[Hit],
			IGListener::DoorKnockInnerRadius,
			IGListener::DoorKnockFalloff,
			EIGAudioBus::Entity);
	}
}

void AIGListenerEntity::PlayPlasterSettle(const float Volume, const float Pitch)
{
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreatePlasterSettle(this),
		GetActorLocation(),
		Volume,
		Pitch,
		160.0f,
		1200.0f,
		EIGAudioBus::Entity);
}

void AIGListenerEntity::PlayPlantSettle(const float Volume)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - LastPlantSettleSeconds < IGListener::PlantSettleIntervalSeconds)
	{
		return;
	}
	LastPlantSettleSeconds = Now;
	PlayPlasterSettle(Volume);
}

void AIGListenerEntity::PlayAlertVocal()
{
	// 녹음은 일곱 초 동안 네 번 헐떡인다. 이제 걸음에 밀리지 않으니 첫 숨에서
	// 직접 끊는다(§21.3의 한 숨). 피치를 조금 내려 같은 원본을 쓰는 유담의
	// 들숨과도 갈라 둔다.
	const FVector BreathAt = GetActorLocation() + FVector(30.0f, 0.0f, 30.0f);
	IGAudio::FadeOutAfter(
		IGAudio::SpawnOneShotAt(
			this,
			IGAudio::SampleOr(
				TEXT("Entity_Alert"),
				[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateEntityAlertVocal(this); }),
			BreathAt,
			0.9f,
			0.9f,
			220.0f,
			2400.0f,
			EIGAudioBus::Entity),
		1.0f,
		0.25f);
	// §19.8. 그가 무언가를 들었다는 것이 소리로만 오면 듣기 어려운 손은 모른다.
	EmitPresentationCue(BreathAt, 0.5f, 2620.0f);
	if (IsNearForCaption(BreathAt))
	{
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "EntityAlertCaption", "숨을 들이켜는 소리"),
			1.6f,
			BreathAt);
	}
}

bool AIGListenerEntity::EmitPresentationCue(
	const FVector& At,
	const float Loudness,
	const float AudibleRange)
{
	if (bDormant || !NoiseSubsystem || Loudness <= 0.0f)
	{
		return false;
	}
	const APawn* Listener = CachedPlayer.IsValid()
		? CachedPlayer.Get()
		: UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Listener)
	{
		return false;
	}
	// 그녀 자리에 닿는 만큼. 소리의 감쇠 거리를 넘으면 들리지 않은 것이다.
	const float Distance = FVector::Dist(Listener->GetActorLocation(), At);
	const float Heard = Loudness
		* FMath::Clamp(1.0f - Distance / FMath::Max(AudibleRange, 1.0f), 0.0f, 1.0f);
	if (Heard < 0.05f)
	{
		return false;
	}
	NoiseSubsystem->BroadcastPresentationCue(At, Heard, this);
	return true;
}

bool AIGListenerEntity::IsNearForCaption(const FVector& At) const
{
	const APawn* Listener = CachedPlayer.IsValid()
		? CachedPlayer.Get()
		: UGameplayStatics::GetPlayerPawn(this, 0);
	return Listener
		&& FVector::DistSquared(Listener->GetActorLocation(), At)
			<= FMath::Square(IGListener::NearCaptionCentimeters);
}

void AIGListenerEntity::UpdateDragLoop(const float CurrentSpeed)
{
	if (!DragLoopComponent)
	{
		return;
	}
	// Louder and faster the harder it pulls itself. At rest: true silence,
	// which is the scariest volume it has.
	// 밤4에 목한수를 따라가는 느린 걸음은 추격 속도에 대면 끌림이 묻힌다.
	// 그 걸음의 기준은 정해 준 속도다.
	const float ReferenceSpeed =
		State == EIGListenerState::FinaleLured && FinaleSpeedOverride > 0.0f
			? FinaleSpeedOverride * 1.4f
			: ChaseSpeed;
	const float SpeedRatio = ReferenceSpeed > 0.0f
		? FMath::Clamp(CurrentSpeed / ReferenceSpeed, 0.0f, 1.0f)
		: 0.0f;
	DragLoopComponent->SetVolumeMultiplier(SpeedRatio * 0.9f);
	DragLoopComponent->SetPitchMultiplier(0.85f + 0.45f * SpeedRatio);
}

void AIGListenerEntity::RefreshDragSurface()
{
	const UWorld* World = GetWorld();
	if (!World || !DragLoopComponent)
	{
		return;
	}

	const FVector Origin = GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(IGListenerDragSurface),
		false,
		this);
	bool bVinyl = false;
	bool bMetalStair = false;
	if (World->LineTraceSingleByChannel(
		Hit,
		Origin + FVector(0.0f, 0.0f, 12.0f),
		Origin - FVector(0.0f, 0.0f, 120.0f),
		ECC_Visibility,
		QueryParams))
	{
		if (const UPrimitiveComponent* Component = Hit.GetComponent())
		{
			bVinyl = Component->ComponentHasTag(IGListener::VinylSurfaceTag);
			bMetalStair = Component->ComponentHasTag(IGListener::MetalStairSurfaceTag);
		}
	}
	// 계단 철판은 끌림 루프를 바꾸지 않는다. 짚을 때마다 철판이 우는 소리를 걸음에 얹는다.
	bDragSurfaceIsMetalStair = bMetalStair;
	if (bVinyl == bDragSurfaceIsVinyl)
	{
		return;
	}
	bDragSurfaceIsVinyl = bVinyl;

	// Swapping the wave mid-loop restarts the crawl cycle, which is the right
	// seam: the palm plant that lands on the new floor is the one that sounds
	// different. Volume is left to UpdateDragLoop so a stopped entity stays
	// silent through the change.
	DragLoopComponent->SetSound(
		UIGToneSequenceSoundWave::CreateEntityDragLoop(this, bVinyl));
	DragLoopComponent->Play();
}

void AIGListenerEntity::ReportDustTrail()
{
	// Asleep in the day, and still while he knocks or listens: dust rises from
	// the drag, so a stationary body raises none.
	if (bDormant || LastMoveSpeed <= 1.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!DustSubsystem)
	{
		DustSubsystem = World->GetSubsystem<UIGDustSubsystem>();
		if (!DustSubsystem)
		{
			return;
		}
	}

	// He crawls, so the plaster comes off low. 40 cm above the floor is where a
	// torch held at chest height cuts through it.
	const FVector DragHeight = GetActorLocation() - FVector(0.0f, 0.0f, 18.0f);
	const float MovedCentimeters = bDustTrailSeeded
		? static_cast<float>(FVector::Dist(LastDustReportLocation, DragHeight))
		: 0.0f;
	if (bDustTrailSeeded && MovedCentimeters < UIGDustSubsystem::MergeDistance)
	{
		return;
	}

	// A burst scrapes more wall than a patrol crawl does, so a chase leaves the
	// brightest lane — the one the player most needs to read afterwards.
	const float DragStrength = FMath::Clamp(LastMoveSpeed / 160.0f, 0.45f, 1.0f);
	DustSubsystem->ReportDisturbance(DragHeight, DragStrength);
	// §11 V2: the same pass also presses the settled dust. His mark is a smear
	// across the direction of travel, not a footprint — and it is the one thing
	// on the fifth floor that says something came through here on its elbows.
	DustSubsystem->ReportSettledPrint(
		FVector(DragHeight.X, DragHeight.Y, DragHeight.Z),
		GetActorRotation().Yaw,
		EIGDustPrintKind::Drag);

	// One audible sift every few meters, never every sample: a continuous hiss
	// would sit on top of the crawl bed and stop being information. The cue is
	// the invitation — you hear powder fall somewhere, you raise the torch, and
	// the lane tells you which way he went.
	DustSiftCentimeters += MovedCentimeters;
	if (DustSiftCentimeters >= IGListener::DustSiftIntervalCentimeters)
	{
		DustSiftCentimeters = 0.0f;
		const UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>();
		// The two authored silences are holes in the mix, not quiet passages.
		// Nothing crawls into them, and he is holding still through both anyway.
		if (!AudioDirector || !AudioDirector->IsAuthoredSilence())
		{
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreatePlasterDustFall(this),
				DragHeight + FVector(0.0f, 0.0f, 24.0f),
				IGListener::DustSiftVolume,
				1.0f,
				190.0f,
				2200.0f,
				EIGAudioBus::Entity);
		}
	}

	LastDustReportLocation = DragHeight;
	bDustTrailSeeded = true;
}

void AIGListenerEntity::UpdateThreatPressure()
{
	const AIGPlayerCharacter* Player =
		Cast<AIGPlayerCharacter>(CachedPlayer.Get());
	if (!Player)
	{
		return;
	}
	UIGStressComponent* Stress = Player->GetStress();
	if (!Stress)
	{
		return;
	}

	float Pressure = 0.0f;
	const float Distance =
		FVector::Dist(Player->GetActorLocation(), GetActorLocation());
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->SetEntityDistance(Distance);
		}
	}
	switch (State)
	{
	case EIGListenerState::Chasing:
		Pressure = 0.85f;
		break;
	case EIGListenerState::Investigating:
	case EIGListenerState::Holding:
		Pressure = 0.45f * FMath::Clamp(1.0f - Distance / 1200.0f, 0.0f, 1.0f);
		break;
	case EIGListenerState::Searching:
		Pressure = 0.25f * FMath::Clamp(1.0f - Distance / 900.0f, 0.0f, 1.0f);
		break;
	case EIGListenerState::Listening:
	case EIGListenerState::Banging:
		// 순찰 중이라도 6m 안에서 두드리고 듣는 그는 압박이다. 예전엔 코앞에서
		// 노크 3연을 쳐도 심장이 잠잠했다.
		Pressure = 0.35f * FMath::Clamp(1.0f - Distance / 600.0f, 0.0f, 1.0f);
		break;
	default:
		break;
	}
	if (Pressure > 0.0f)
	{
		// The component decays pressure on its own; only pushes are sent.
		Stress->SetThreatPressure(Pressure);
	}
	UpdateBreathLoop(Distance);
	TryCloseCallStinger(Player, Distance);
}

void AIGListenerEntity::UpdateBreathLoop(const float Distance)
{
	if (!BreathLoopComponent)
	{
		return;
	}
	float Target = 0.0f;
	if (!bDormant)
	{
		switch (State)
		{
		case EIGListenerState::Chasing: Target = 0.85f; break;
		// 들은 것 없이 다가와 엎드린 그는 숨을 죽인다. 남는 경고는 심장과 압박 층,
		// 그리고 이 가라앉은 숨이다.
		case EIGListenerState::Investigating: Target = bSilentApproach ? 0.10f : 0.45f; break;
		case EIGListenerState::Holding: Target = bSilentApproach ? 0.10f : 0.34f; break;
		case EIGListenerState::Searching: Target = 0.34f; break;
		case EIGListenerState::Listening: Target = 0.26f; break;
		case EIGListenerState::Banging:
		case EIGListenerState::Patrolling: Target = 0.20f; break;
		case EIGListenerState::Waiting: Target = bWaitStirred ? 0.30f : 0.10f; break;
		default: Target = 0.0f; break;
		}
	}
	if (!FMath::IsNearlyEqual(Target, BreathVolumeTarget, 0.02f))
	{
		BreathVolumeTarget = Target;
		// 기다림이 닳아 돌아오는 숨은 천천히 오른다. 한 번에 올리면 들킨 소리가 된다.
		const float BreathFade =
			State == EIGListenerState::Waiting && bWaitStirred ? 1.5f : 0.6f;
		// 0으로 가는 페이드는 엔진이 끝에서 정지다. 다시 낼 때는 FadeIn으로
		// 되살리고, 도는 중이면 페이더만 옮긴다.
		if (Target <= 0.01f)
		{
			if (BreathLoopComponent->IsPlaying())
			{
				BreathLoopComponent->FadeOut(0.6f, 0.0f);
			}
		}
		else if (!BreathLoopComponent->IsPlaying())
		{
			BreathLoopComponent->FadeIn(BreathFade, Target);
		}
		else
		{
			BreathLoopComponent->AdjustVolume(BreathFade, Target);
		}
	}
	// 추격 중엔 숨이 빠르다. 루프 속도는 못 바꾸니 피치로.
	BreathLoopComponent->SetPitchMultiplier(State == EIGListenerState::Chasing ? 1.22f : 1.0f);
}

void AIGListenerEntity::TryCloseCallStinger(const AIGPlayerCharacter* Player, const float Distance)
{
	// 코앞에서 마주쳤다: 3.2m 안, 시야 안, 그가 깨어서 움직이거나 두드리는 중. 25초에
	// 한 번. 「이미 본 형상의 위치 변화」(STORY_DIRECTION §7)를 소리로 찍는다.
	UWorld* World = GetWorld();
	if (!World || !Player || bDormant || IsHidden() || Distance > 320.0f)
	{
		return;
	}
	if (State == EIGListenerState::CaptureHold
		|| State == EIGListenerState::Chasing
		|| State == EIGListenerState::FinaleLured
		|| State == EIGListenerState::Waiting
		// 그가 듣는 창은 조용해야 한다. 여기서 터지는 타격은 세계가 숨을 참는 믹스를
		// 깨고, 심박을 소음으로 밀어 올려 판정까지 만든다.
		|| State == EIGListenerState::Listening
		|| State == EIGListenerState::Holding)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now - LastCloseCallSeconds < 25.0)
	{
		return;
	}
	const FVector Eye = Player->GetPawnViewLocation();
	const FVector CueLocation = GetCaptureFaceLocation();
	const FVector ToHim = CueLocation - Eye;
	const FVector View = Player->GetControlRotation().Vector();
	if (FVector::DotProduct(View, ToHim.GetSafeNormal()) < 0.55f)
	{
		return;
	}
	// 고개 방향만 맞아도 벽 너머에서 놀라던 것을 막는다. 실제 눈높이에서
	// 얼굴까지 문이나 벽이 가로막으면 마주친 것이 아니다.
	FCollisionQueryParams SightParams(SCENE_QUERY_STAT(IGCloseCallSight), false, this);
	SightParams.AddIgnoredActor(Player);
	if (World->LineTraceTestByChannel(Eye, CueLocation, ECC_Visibility, SightParams))
	{
		return;
	}
	UIGMissingFloorAudioSubsystem* AudioDirector =
		World->GetSubsystem<UIGMissingFloorAudioSubsystem>();
	if (!AudioDirector || !AudioDirector->PlayStinger(EIGStinger::CloseCall, CueLocation))
	{
		return;
	}
	LastCloseCallSeconds = Now;
	if (UIGStressComponent* Stress = Player->GetStress())
	{
		// 스팅어 혼자 심박을 소음(0.85)으로 만들지 않는다. 0.70까지만 올려 숨을
		// 참거나 물러설 두 초쯤을 남긴다. 들숨은 스트레스와 따로 낸다.
		const float Headroom = FMath::Max(0.0f, 0.70f - Stress->GetStress());
		Stress->ApplyScare(FMath::Min(0.45f, Headroom));
		Stress->PlayGasp();
	}
	const_cast<AIGPlayerCharacter*>(Player)->PlayScareKick(1.4f);
}

void AIGListenerEntity::SetCaptureKeyLight(const bool bEnabled)
{
	if (bEnabled && !CaptureKeyLight)
	{
		CaptureKeyLight = NewObject<UPointLightComponent>(this, TEXT("CaptureKeyLight"));
		CaptureKeyLight->SetMobility(EComponentMobility::Movable);
		CaptureKeyLight->SetIntensity(0.0f);
		// 빛이 멀리 닿지 않아야 물러난 얼굴은 어둠에 묻히고, 달려드는 얼굴만 빛 속으로 들어온다.
		CaptureKeyLight->SetAttenuationRadius(80.0f);
		CaptureKeyLight->SetLightColor(FLinearColor(0.72f, 0.82f, 1.0f));
		CaptureKeyLight->SetSourceRadius(6.0f);
		CaptureKeyLight->SetCastShadows(false);
		CaptureKeyLight->SetSpecularScale(0.4f);
		CaptureKeyLight->RegisterComponent();
	}
	if (CaptureKeyLight)
	{
		// 켤 때는 어둡게 시작한다. 밝기는 잡는 동안 매 프레임 올린다.
		CaptureKeyLight->SetIntensity(0.0f);
		CaptureKeyLight->SetWorldLocation(CaptureKeyLightLocation);
		CaptureKeyLight->SetVisibility(bEnabled);
	}
}

void AIGListenerEntity::KeepCaptureVisible()
{
	if (State != EIGListenerState::CaptureHold) return;
	bDormant = false;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(false);
	SetActorTickEnabled(true);
}

FVector AIGListenerEntity::GetCaptureFaceLocation() const
{
	return ListenerSkeletal && ListenerSkeletal->DoesSocketExist(TEXT("head"))
		? ListenerSkeletal->GetSocketLocation(TEXT("head"))
		: GetActorLocation() + GetActorForwardVector() * 65.0f + FVector(0, 0, 15);
}

void AIGListenerEntity::BeginCapture(APawn* Player)
{
	if (State == EIGListenerState::CaptureHold)
	{
		return;
	}
	// 숨어 있던 사람은 먼저 자리 밖으로 끌려 나온다. 넘어지는 눈높이와 얼굴이 덮치는
	// 자리는 그다음에 잰다. 순서가 바뀌면 가구 속 눈을 기준으로 덮친다.
	if (AIGPlayerCharacter* HiddenPlayer = Cast<AIGPlayerCharacter>(Player))
	{
		HiddenPlayer->LeaveHidingSpotImmediately();
	}
	EnterState(EIGListenerState::CaptureHold);
	if (Player)
	{
		const FVector Direction = (Player->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		SetActorRotation(Direction.Rotation());
		// 플레이어는 뒤로 넘어져 바닥 가까이에서 괴물 쪽을 조금 올려다본다(AIGPlayerCharacter의
		// 포획 시점). 기는 자세의 얼굴은 위를 보고 있어서 이 각도에서 가장 잘 읽힌다. 얼굴은
		// 이 시선 위에서 70cm까지 물러났다가 눈앞 20cm로 달려든다. 주먹 쥔 두 팔을 가슴
		// 앞으로 드는 덮치기 동작은 쓰지 않는다. 권투 자세로 읽혔다.
		// 넘어지는 눈높이는 선 사람 기준으로 잰다. 침대 밑에서 끌려 나와 아직 앉은
		// 몸을 엔진의 앉은 눈높이로 재면 덮치는 얼굴이 바닥 밑으로 들어간다.
		FVector StandingEye = Player->GetPawnViewLocation();
		if (const AIGPlayerCharacter* Victim = Cast<AIGPlayerCharacter>(Player))
		{
			if (const UCapsuleComponent* Capsule = Victim->GetCapsuleComponent())
			{
				const float FeetZ = Victim->GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
				StandingEye.Z = FeetZ + Victim->GetDefaultHalfHeight() + Victim->GetCameraBaseLocation().Z;
			}
		}
		CaptureFallenEye = StandingEye
			+ Direction * AIGPlayerCharacter::CaptureFallBackCentimeters
			- FVector(0, 0, AIGPlayerCharacter::CaptureFallDropCentimeters);
		CaptureStrikeLine = (-Direction * 34.0f + FVector(0, 0, 10)).GetSafeNormal();
		CaptureViewTarget = CaptureFallenEye + CaptureStrikeLine * 20.0f;
		// 바닥에 떨어진 손전등 자리. 달려드는 얼굴이 이 빛 속으로 들어온다.
		CaptureKeyLightLocation = CaptureViewTarget - FVector(0, 0, 18) + Direction * 4.0f;
	}
	CaptureVictim = Cast<AIGPlayerCharacter>(Player);
	CaptureFaceStart = GetCaptureFaceLocation();
	if (ListenerSkeletal)
	{
		ListenerSkeletal->PrestreamTextures(3.f, false);
		PlayBodyAnim(EIGListenerBodyAnim::Crawl, true, 2.6f);
	}
	SetCaptureKeyLight(true);

	if (AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(Player))
	{
		Character->SetCaptureThreat(this);
		if (UIGStressComponent* Stress = Character->GetStress())
		{
			Stress->ApplyScare(1.0f);
		}
	}

	// 몸이 닿는 순간 저역을 겹치고, 가까운 마찰과 숨은 아래의 건조한 음원으로 낸다.
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			if (AudioDirector->PlayStinger(
				EIGStinger::Capture,
				Player ? Player->GetActorLocation() : GetActorLocation()))
			{
				// 덮침 녹음은 3.45초이고 뒤 절반이 거친 숨이다. 그 숨이 1.48초와
				// 1.90초의 노크 둘을 덮었다. 접촉 타격만 남기고 노크 앞에서 걷는다.
				// 0으로 가는 페이드는 정지다. 다시 올리지 않는다.
				const TWeakObjectPtr<UAudioComponent> Grab =
					AudioDirector->GetStingerComponent().Get();
				World->GetTimerManager().SetTimer(
					CaptureGrabTrimTimer,
					FTimerDelegate::CreateWeakLambda(this, [Grab]()
					{
						if (UAudioComponent* GrabComponent = Grab.Get())
						{
							if (GrabComponent->IsPlaying())
							{
								GrabComponent->FadeOut(0.25f, 0.0f);
							}
						}
					}),
					1.25f,
					false);
			}
		}
	}

	// 복도 잔향을 빼서 귓가의 마찰, 끊긴 숨, 암전 속의 노크 둘이 바로 들리게 한다.
	// 노크 둘은 밤3에 벽이 돌려주는 대답과 같은 재료, 같은 0.42초 간격이다.
	IGAudio::SpawnDryOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateCaptureStruggle(this),
		Player ? Player->GetActorLocation() : GetActorLocation(),
		1.0f,
		1.0f,
		120.0f,
		600.0f,
		EIGAudioBus::Entity);

	OnPlayerCaptured.Broadcast(Player);
}
