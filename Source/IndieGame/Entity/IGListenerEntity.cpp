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
#include "Entity/IGLiningEntity.h"
#include "Interaction/IGBathroomRefuge.h"
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
	// 어깨 폭. 팔꿈치는 그보다 넓게 벌어지지만 벽에 닿는 것은 팔이다.
	constexpr float BodyRadius = 34.0f;
	// 벽 듣기 자리에서 이만큼 안에 멈춰 있으면 벽 자세를 튼다.
	constexpr float WallListenAnchorRadius = 30.0f;
	// 벽에서 떨어진 뒤 다시 붙지 않는 시간. 떨어지기가 끝난 바로 그 프레임에는 아직 기기
	// 전이라, 이것이 없으면 붙기와 떨어지기를 번갈아 틀며 제자리에 서 있게 된다.
	constexpr double WallReenterDelaySeconds = 1.5;
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
	/** 탭에 멈춰 서서 듣는 것은 45초에 두 번까지다. 그 뒤로는 곧장 친 자리로 온다. */
	constexpr double TapStallWindowSeconds = 45.0;
	constexpr int32 TapStallsPerWindow = 2;
	constexpr float SearchSeconds = 12.0f;
	constexpr float SearchRadius = 350.0f;
	constexpr float ChaseGiveUpSeconds = 8.0f;
	constexpr float CaptureHoldSeconds = 2.5f;
	// 감독이 없을 때 잡은 몸이 끊길 때까지 쓰는 시간. 몸싸움 소리의 첫 노크(1.48초)다.
	constexpr float CaptureCutSeconds = 1.48f;
	// 끊기는 때의 이만큼에 얼굴이 멈춘다. 나머지는 멈춰서 듣는 시간이다.
	constexpr float CaptureArriveFraction = 0.78f;
	constexpr float CaptureFaceStopCentimeters = 45.0f;
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
	/**
	 * 닫힌 방화문·욕실 문 앞에서 문면과 캡슐 가운데 사이(cm). 두드리는 자세는 손을 캡슐 가운데에서 93 cm 앞까지
	 * 뻗어, 캡슐이 문에 닿은 자리(33 cm)에서는 팔과 어깨가 문짝 속으로 들어가 반대쪽으로 나왔다. 손끝이 문면에 닿는
	 * 거리에 선다.
	 */
	constexpr float ClosedDoorStandOff = 92.0f;
	/**
	 * 두드리러 설 때 문짝 가운데 줄 위에서 문면까지. 문을 똑바로 보고 서면 바닥을 짚은 왼손(93.5 cm 앞)이 92 cm에서는
	 * 두드리는 내내 문짝 아랫면에 묻혔다. 비스듬히 서던 때는 덜 닿았다.
	 */
	constexpr float ClosedDoorKnockStand = 97.0f;
	/** 이보다 멀리서 문을 알아봤으면 다가가지 않고 그 자리에서 두드린다. */
	constexpr float ClosedDoorApproachLimit = 160.0f;
	/** 그 거리로 물러서는 빠르기와 다가가는 빠르기(cm/s). */
	constexpr float ClosedDoorRetreatSpeed = 300.0f;
	constexpr float ClosedDoorApproachSpeed = 120.0f;
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
	 * 욕실 ABS 문짝. 그의 손이 내는 나무 타격(석고 노크와 같은 녹음)을 높여 속 빈 판으로
	 * 낸다. 철문보다 가볍고 짧게 운다.
	 */
	const TCHAR* const HollowDoorSamples[DoorSteelHitCount] = {
		TEXT("Knock_Plaster_0"), TEXT("Knock_Plaster_1"), TEXT("Knock_Plaster_2")};
	constexpr float HollowDoorPitches[DoorSteelHitCount] = {1.16f, 1.12f, 1.18f};
	constexpr float HollowDoorVolume = 0.86f;
	/** 공동현관 유리문. 철문 녹음을 높이고 줄여 알루미늄 틀과 유리가 떨리는 소리로 쓴다. */
	constexpr float GlassDoorPitchScale = 1.38f;
	constexpr float GlassDoorVolumeScale = 0.62f;
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
	/**
	 * 계단탑 안에서 도는 빠르기. 평지(초당 160도)보다 빨리 돌아야 긴 몸이 옆으로 미끄러지지 않는다. 계단참에서 꺾는
	 * 자리는 자세 점으로 돌고(StepAlongPose), 이 빠르기는 계단과 곧은 걸음에서 방향을 바로잡을 때 쓴다.
	 */
	constexpr float StairTurnDegreesPerSecond = 540.0f;
	/**
	 * 계단참 자세 점 사이에서 1도 도는 데 드는 걸음 거리. 제자리에서 40~50도 도는 자세도 시간이 걸린다. 반 층 참을
	 * 다 도는 데 쫓는 빠르기로 0.85초쯤, 예전 원호와 같다.
	 */
	constexpr float PoseTurnCentimetersPerDegree = 2.0f;
	/**
	 * 몸 가운데에서 손끝까지. 기는 자세의 손목이 85 cm 앞에 있고 주먹이 그보다 몇 cm 더 나온다. 닫힌 문 앞에
	 * 서는 거리(ClosedDoorStandOff)와 같은 몸이다.
	 */
	constexpr float FrontReach = 92.0f;
	/** 몸 가운데에서 발끝까지. */
	constexpr float RearReach = 90.0f;
	/** 팔꿈치와 무릎이 몸 가운데에서 옆으로 벌어지는 폭. */
	constexpr float SideReach = 36.0f;
	/** 팔꿈치 관절 밖의 살. 걸으며 옆을 비켜 갈 때는 여기까지 본다. */
	constexpr float SideSkin = 8.0f;
	/** 옆을 비켜 가는 빠르기. 걸음과 섞여 옆으로 미끄러지는 것으로 보이지 않을 만큼이다. */
	constexpr float SidestepSpeed = 120.0f;
	/**
	 * 디딤판을 짚는 자리 밑을 훑는 상자. 몸 앞뒤로 디딤판 너비(25 cm)만큼 길어서, 어디서 훑든 가장 가까운 단 모서리에
	 * 걸린다. 옆으로는 얇아 계단 옆벽에 먼저 닿지 않는다.
	 */
	const FVector StairProbeHalfExtent(12.5f, 3.0f, 1.0f);
	/** 짚는 자리의 위아래로 이만큼 훑는다. 한 번에 두 단(33 cm) 넘게 어긋나지는 않는다. */
	constexpr float StairProbeReach = 45.0f;
	/** 계단탑 밖에서 훑는 높이. 문턱과 마루 턱까지만이다. */
	constexpr float FloorProbeReach = 15.0f;
	/**
	 * 짚는 뼈가 몸 바닥면에서 이보다 낮으면 바닥에 닿은 것으로 본다. 더 높이 든 뼈(두드리는 주먹, 골반)는 그만큼
	 * 바닥이 솟아야 닿는다.
	 */
	constexpr float FloorContactHeight = 15.0f;
	/** 몸을 디딤판 위로 띄우는 상한. */
	constexpr float StairLiftLimit = 35.0f;
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
	// 반지름은 어깨 폭이다. 팔꿈치는 그보다 넓게 벌어지지만, 90 cm 문틀과 폭 105 cm
	// 계단을 지나야 하는 몸이라 벽에 닿는 것은 팔이지 몸통이 아니다.
	Body->InitCapsuleSize(IGListener::BodyRadius, 58.0f);
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
	if (ListenerSkeletal)
	{
		ClipAudit.Start(TEXT("listener"));
	}
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
	ClipAudit.Report();
	Super::EndPlay(EndPlayReason);
}

void AIGListenerEntity::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 움직이기 전에 잰다. 지난 프레임에 그린 자세와 자리 그대로다.
	if (ClipAudit.IsOn() && !bDormant && !IsHidden())
	{
		ClipAudit.Measure(ListenerSkeletal.Get(), Body ? Body->GetComponentLocation() : GetActorLocation(), CachedPlayer.Get(),
			StaticEnum<EIGListenerState>()->GetNameStringByValue(static_cast<int64>(State))
				+ (FIGBuildingNav::IsInStairCore(GetFeetLocation()) ? TEXT(".stair") : TEXT("")));
	}
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
		bAtClosedDoor = false;
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
		StairBodyLift = 0.0f;
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
		else if (bAtClosedDoor)
		{
			PlayClosedDoorKnock();
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
		ClearNavPath();
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
			FaceDirection(GetListeningFacing(), DeltaSeconds);
		}
		// 손이 문면에 닿는 거리에 서기 전에는 치지 않는다. 시계를 붙잡아 두어야 문소리와 손이 닿는 순간이 맞는다.
		if (!SettleAtClosedDoor(DeltaSeconds))
		{
			StateSeconds = 0.0f;
			break;
		}
		// 문짝을 치는 녹음은 한 타씩이라 3연의 간격대로 이어 친다.
		if ((bAtHomeDoor || bAtClosedDoor)
			&& DoorSteelHitsPlayed < IGListener::DoorSteelHitCount
			&& StateSeconds >= IGListener::DoorSteelSpacingSeconds * DoorSteelHitsPlayed)
		{
			PlayDoorHit();
		}
		if (StateSeconds >= IGListener::BangSeconds)
		{
			EnterState(EIGListenerState::Listening);
		}
		break;

	case EIGListenerState::Listening:
		if (!AttentionDirection.IsNearlyZero())
		{
			FaceDirection(GetListeningFacing(), DeltaSeconds);
		}
		SettleAtClosedDoor(DeltaSeconds);
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
			else if (ArriveAtRoomDoor())
			{
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
			FaceDirection(GetListeningFacing(), DeltaSeconds);
		}
		SettleAtClosedDoor(DeltaSeconds);
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
			if (bArrived && ArriveAtRoomDoor())
			{
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
			// 닫힌 문 앞이면 그 문에 귀를 댄다. 문 너머 숨을 자리는 거기서 듣는다. 아니면 몸이 벽에 박히지
			// 않는 쪽으로 돌아선다. 복도에서는 숨을 자리 쪽 벽을 마주하지 못하고 복도를 따라 비스듬히 선다.
			if (bBlockedByClosedDoor)
			{
				bBlockedByClosedDoor = false;
				if (const AActor* Door = BlockingDoor.Get())
				{
					const FVector ToDoor = Door->GetComponentsBoundingBox().GetCenter() - GetActorLocation();
					SearchFacings[SearchSpotIndex] = FVector(ToDoor.X, ToDoor.Y, 0.0f).GetSafeNormal();
				}
			}
			else
			{
				SearchFacings[SearchSpotIndex] = FitFacing(SearchFacings[SearchSpotIndex]);
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
		&& State != EIGListenerState::FinaleLured
		&& GetWorld()->GetTimeSeconds() >= PerceptionGraceUntil)
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
				// 잠긴 욕실은 가구와 다르다. 그는 문을 열지 않으니 문 너머로 손을 넣지 못한다.
				// 덧댄 것이 움직이거나 누르는 동안에는 손을 대지 않는다. 그때 잡으면 포획이 둘 겹친다.
				if (bHeardInside
					&& HidingPlayer->GetHidingSpot()
					&& FVector::Dist2D(Player->GetActorLocation(), GetActorLocation())
						<= IGListener::HiddenReachRadius
					&& Tuning.bCaptureEnabled
					&& !AIGLiningEntity::IsAnyBusy(GetWorld()))
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
				&& (!bTouchCaptureSuppressed || bPursuing)
				&& !AIGLiningEntity::IsAnyBusy(GetWorld()))
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
	// 잡은 뒤 잠깐은 그녀가 내는 소리를 듣지 않는다. 바닥에서 일어나 숨을 고르는 소리에
	// 곧장 돌아오면 깨어난 것이 아니다. 건물과 연출이 내는 소리는 그대로 듣는다.
	if (Event.TimeSeconds < PerceptionGraceUntil)
	{
		const AActor* Source = Event.Instigator.Get();
		const bool bFromHer = Source
			&& (Source->IsA<AIGPlayerCharacter>()
				|| Cast<AIGPlayerCharacter>(Source->GetOwner())
				|| Cast<AIGPlayerCharacter>(Source->GetInstigator()));
		if (bFromHer)
		{
			return;
		}
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
		UE_LOG(LogIndieGame, Display, TEXT("LISTENER_HOLD broken loudness=%.2f at=%s by=%s"), Event.Loudness,
			*Event.Location.ToCompactString(), Event.Instigator.IsValid() ? *Event.Instigator->GetName() : TEXT("none"));
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
		* AnswerDecay[AnswerIndex] + BeatWaitHoldSeconds;
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
	if (bBeatHold != bHold)
	{
		UE_LOG(LogIndieGame, Display, TEXT("LISTENER_HOLD set=%d at=%s"), bHold ? 1 : 0, *GetActorLocation().ToCompactString());
	}
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
	if (AnswerTapTimes.Num() == 1)
	{
		// 새 박자의 첫 탭이다. 45초 안에 몇 번째로 그를 세우는지 센다.
		if (Now - TapStallWindowStart > IGListener::TapStallWindowSeconds)
		{
			TapStallWindowStart = Now;
			TapStallCount = 0;
		}
		++TapStallCount;
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
			if (TapStallCount > IGListener::TapStallsPerWindow)
			{
				InvestigateTapDirectly(KnockLocation, Now);
				return true;
			}
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
	if (TapStallCount > IGListener::TapStallsPerWindow)
	{
		InvestigateTapDirectly(KnockLocation, Now);
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

void AIGListenerEntity::InvestigateTapDirectly(const FVector& KnockLocation, const double Now)
{
	// 두드림에 몇 번이나 멈춰 서서 들었는데 아무것도 이어지지 않았다. 이제는 서서 듣지
	// 않고 친 자리로 곧장 온다. 이미 그쪽으로 가는 중이면 걸음을 끊지 않는다.
	const bool bFromAbove = NoteAnswerLocation(KnockLocation);
	LastHeardTime = Now;
	bReactingToSound = true;
	bSilentApproach = false;
	bObservationHold = false;
	if (State != EIGListenerState::Investigating)
	{
		EnterState(EIGListenerState::Investigating);
	}
	bCallFromAbove = bFromAbove;
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
		TapStallCount = 0;
		TapStallWindowStart = -1000.0;
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
	// 다른 자리로 옮겼으니 벽에서 떨어지던 동작은 이어지지 않는다.
	WallRiseUntilSeconds = -1000.0;
	WallReenterAfterSeconds = -1000.0;
	PatrolIndex = 0;
	// 연출이 세운 자리에서 새로 시작한다. 쫓던 길과 놓친 흔적, 경계를 남기면 카메오가
	// 시작하자마자 엉뚱한 층으로 기어간다.
	ClearNavPath();
	PlayerTrail.Reset();
	AlertUntilSeconds = -1000.0;
	// 이 한 줄이 카메오를 성립시킨다. 남겨 두면 그는 조사 중인 상태로 서 있다가
	// 아까 들은 자리로 기어간다.
	bReactingToSound = false;
	bAmbushArmed = false;
	AnswerTapTimes.Reset();
	TapStallCount = 0;
	TapStallWindowStart = -1000.0;
	EnterState(EIGListenerState::Patrolling);
}

void AIGListenerEntity::SetBodyRadiusForBeat(const float Radius)
{
	if (Body)
	{
		Body->SetCapsuleRadius(Radius > 0.0f ? Radius : IGListener::BodyRadius, true);
	}
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
	ClearNavPath();
	PlayerTrail.Reset();
	AlertUntilSeconds = -1000.0;
	StairPitchTarget = 0.0f;
	StairBodyPitch = 0.0f;
	StairBodyLift = 0.0f;
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

bool AIGListenerEntity::IsInCaptureGrace() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < PerceptionGraceUntil;
}

void AIGListenerEntity::ReturnAfterCapture(const float GraceSeconds)
{
	TeleportTo(SpawnLocation, GetActorRotation(), false, true);
	SetActorEnableCollision(true);
	PatrolIndex = 0;
	ClearNavPath();
	PlayerTrail.Reset();
	AlertUntilSeconds = -1000.0;
	StairPitchTarget = 0.0f;
	StairBodyPitch = 0.0f;
	StairBodyLift = 0.0f;
	bBeatHold = false;
	bReactingToSound = false;
	bAmbushArmed = false;
	bLungeArmed = false;
	const UWorld* World = GetWorld();
	PerceptionGraceUntil = World ? World->GetTimeSeconds() + GraceSeconds : -1000.0;
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
	bAtClosedDoor = false;
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
	RoomAhead = nullptr;
	// 집 안에서 들은 욕실 소리. 그는 벽을 따라 기지 않고 욕실 문 앞으로 간다.
	if (const AIGBathroomRefuge* Room = AIGBathroomRefuge::FindRoomAt(GetWorld(), Location))
	{
		if (!Room->IsInside(GetActorLocation()) && IsInsideHome(GetActorLocation()) && Room->GetDoor())
		{
			LastHeardLocation = Room->GetOutsideApproach() + FVector(0.0f, 0.0f, 60.0f);
			RoomAhead = const_cast<AIGBathroomRefuge*>(Room);
			return;
		}
	}
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
	bAtClosedDoor = false;
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
	const float DeltaSeconds,
	const bool bStopShortOfWalls)
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
	if (HoldForWallRise())
	{
		LastMoveSpeed = 0.0f;
		return false;
	}

	const FVector Direction = ToTarget / Distance;
	// 계단탑 안은 좁아서, 몸이 덜 돈 채 옆으로 미끄러지면 머리나 발이 벽에 박힌다. 계단에서처럼 빨리 돌고, 많이
	// 틀어져 있으면 돌 때까지 걸음을 늦춘다.
	const bool bInStairCore = FIGBuildingNav::IsInStairCore(GetFeetLocation());
	const FVector Step =
		Direction * FMath::Min(Speed * (bInStairCore ? TurnSpeedScale(Direction) : 1.0f) * DeltaSeconds, Distance);
	FaceDirection(Direction, DeltaSeconds, bInStairCore ? IGListener::StairTurnDegreesPerSecond : 160.0f);

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
	// 목표가 벽 바로 앞이면 손끝이 벽에 닿는 거리에서 닿은 것으로 친다. 캡슐(반지름 34 cm)이 벽에 닿을 때까지
	// 기면 앞으로 뻗은 손과 머리가 벽 속으로 50 cm 넘게 들어간다. 길 위의 점과 수색할 자리만 이렇게 간다.
	if (bStopShortOfWalls && Distance <= IGListener::FrontReach + 40.0f
		&& (IsBodyBlockedAlong(Direction, IGListener::FrontReach, 0.0f, 12.0f, false)
			|| IsBodyBlockedAlong(Direction, IGListener::FrontReach, 0.0f, 45.0f, false)))
	{
		LastMoveSpeed = 0.0f;
		StuckSeconds = 0.0f;
		return true;
	}
	// 앞에 닫힌 문(관리실 문, 계단실 방화문, 욕실 문)이 있으면 손이 닿는 거리에서 멈춘다. 캡슐이 문짝에 닿을
	// 때까지 기면 앞으로 뻗은 머리와 팔이 문짝 속으로 들어갔다. 403호 문 앞은 따로 선다(HomeDoorStandOffset).
	if (UWorld* World = GetWorld())
	{
		FCollisionObjectQueryParams Doors;
		Doors.AddObjectTypesToQuery(ECC_WorldStatic);
		Doors.AddObjectTypesToQuery(ECC_WorldDynamic);
		FHitResult Ahead;
		if (World->LineTraceSingleByObjectType(Ahead, Before, Before + Direction * (IGListener::ClosedDoorStandOff + Step.Size()),
				Doors, FCollisionQueryParams(SCENE_QUERY_STAT(IGListenerDoorAhead), false, this)))
		{
			// 비스듬히 스쳐 가는 문짝에는 서지 않는다. 문면을 마주하고 기어 갈 때만이다.
			AIGSwingDoor* Door = Cast<AIGSwingDoor>(Ahead.GetActor());
			if (Door && !Door->IsOpen() && Door != HomeDoor.Get() && Ahead.Distance <= IGListener::ClosedDoorStandOff
				&& FVector::DotProduct(-Direction, Ahead.ImpactNormal.GetSafeNormal2D()) > 0.5f)
			{
				bBlockedByClosedDoor = true;
				BlockingDoor = Door;
				LastMoveSpeed = 0.0f;
				StuckSeconds = 0.0f;
				return false;
			}
		}
	}
	// 팔꿈치와 손은 캡슐보다 옆으로 넓게 벌어진다. 길을 따라가는 걸음은 옆의 벽·가구가 너무 가까우면 반대쪽으로
	// 비켜 가고, 양쪽이 다 가까우면(문틀, 좁은 현관) 가운데로 간다.
	const FVector Sidestep = bStopShortOfWalls ? SideClearanceShift(Direction, DeltaSeconds) : FVector::ZeroVector;
	FHitResult SweepHit;
	AddActorWorldOffset(Step + Sidestep, true, &SweepHit);
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
	// 나가고, 두 계단 사이 벽을 사이에 둔 자리는 곧게 닿지 않는다.
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
	ClearNavPath();
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
		// 계단 위는 디딤판을 따라 움직이므로 같은 계단 위에서만 건너뛴다.
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
		NavPathPose.Add(Nodes[Node].bPose);
		NavPathYaw.Add(Nodes[Node].Yaw);
	}
	// 끝 점은 목표 그 자체다. 계단 위 목표는 가까운 디딤판 점까지만 간다.
	if (!FIGBuildingNav::IsInStairCore(GoalFeet)
		&& FVector::DistSquared(GoalFeet, NavPath.Last()) > FMath::Square(30.0f))
	{
		NavPath.Add(GoalFeet);
		NavPathOnStair.Add(false);
		NavPathPose.Add(false);
		NavPathYaw.Add(0.0f);
	}
	NavGoal = GoalFeet;
	NavPlannedSeconds = World->GetTimeSeconds();
	return NavPath.Num() > 0;
}

AIGSwingDoor* AIGListenerEntity::FindClosedFireDoorOnLeg(const FVector& FromFeet, const FVector& ToFeet)
{
	if (!bStairFireDoorResolved)
	{
		bStairFireDoorResolved = true;
		for (TActorIterator<AIGFireDoorWedge> It(GetWorld()); It; ++It)
		{
			StairFireDoors.Add(It->GetDoor());
		}
	}
	for (const TWeakObjectPtr<AIGSwingDoor>& Candidate : StairFireDoors)
	{
		AIGSwingDoor* Door = Candidate.Get();
		if (!Door || Door->IsOpen())
		{
			continue;
		}
		// 문짝은 경첩에서 액터의 +Y로 120 cm 뻗는다. 닫히면 X가 경첩과 같은 평면이다.
		// 층마다 같은 자리라 높이로 그 층의 문만 고른다.
		const FVector Hinge = Door->GetActorLocation();
		if (FMath::Abs(FromFeet.Z - Hinge.Z) > 150.0f && FMath::Abs(ToFeet.Z - Hinge.Z) > 150.0f)
		{
			continue;
		}
		const float FromSide = FromFeet.X - Hinge.X;
		const float ToSide = ToFeet.X - Hinge.X;
		if (FromSide * ToSide > 0.0f || FMath::IsNearlyEqual(FromSide, ToSide))
		{
			continue;
		}
		const float Alpha = FromSide / (FromSide - ToSide);
		const float CrossY = FMath::Lerp(FromFeet.Y, ToFeet.Y, Alpha);
		if (CrossY > Hinge.Y - 20.0f && CrossY < Hinge.Y + 140.0f)
		{
			return Door;
		}
	}
	return nullptr;
}

bool AIGListenerEntity::ArriveAtRoomDoor()
{
	AIGBathroomRefuge* Room = RoomAhead.Get();
	if (!Room)
	{
		return false;
	}
	RoomAhead = nullptr;
	AIGSwingDoor* Door = Room->GetDoor();
	if (Door && !Door->IsOpen())
	{
		BlockingDoor = Door;
		ArriveAtClosedDoor();
		return true;
	}
	// 문이 열려 있다. 안으로 든다.
	LastHeardLocation = Room->GetInsideApproach() + FVector(0.0f, 0.0f, 60.0f);
	return true;
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
	bAtClosedDoor = false;
	bClosedDoorSettled = false;
	KnockDoor = Cast<AIGSwingDoor>(BlockingDoor.Get());
	if (const AIGSwingDoor* Door = KnockDoor.Get())
	{
		// 엎드린 그가 손을 뻗는 높이에서 문짝을 친다. 소리는 그의 몸이 아니라 그 문에서 난다.
		DoorKnockPoint = Door->GetKnockPoint(GetActorLocation() + FVector(0.0f, 0.0f, 40.0f));
		bAtClosedDoor = true;
	}
	if (Now - LastClosedDoorKnockSeconds >= IGListener::CeilingKnockIntervalSeconds)
	{
		// §3.1 닫힌 방화문이나 욕실 문 너머에서 소리가 났다. 세 번 두드리고, 문에 귀를 대고 듣는다.
		LastClosedDoorKnockSeconds = Now;
		EnterState(EIGListenerState::Banging);
	}
	else
	{
		EnterState(EIGListenerState::Holding);
	}
}

bool AIGListenerEntity::SettleAtClosedDoor(const float DeltaSeconds)
{
	const AIGSwingDoor* Door = KnockDoor.Get();
	if (!bAtClosedDoor || !Door || Door->IsOpen())
	{
		bClosedDoorSettled = true;
		return true;
	}
	if (bClosedDoorSettled)
	{
		return true;
	}
	// 문짝 가운데 줄 위, 겉면에서 ClosedDoorKnockStand 떨어진 자리에 선다. 경첩 쪽으로 치우쳐 서면 바닥을 짚은 왼팔이
	// 문 옆 턱에 걸쳤다(403호 욕실 문 앞에서 현관 단 모서리).
	const FVector Here = GetActorLocation();
	FVector FromFace = Here - Door->GetKnockPoint(Here);
	FromFace.Z = 0.0f;
	if (FromFace.Size() > IGListener::ClosedDoorApproachLimit)
	{
		bClosedDoorSettled = true;
		return true;
	}
	FVector ToStand = Door->GetStandPoint(Here, IGListener::ClosedDoorKnockStand) - Here;
	ToStand.Z = 0.0f;
	const float Distance = ToStand.Size();
	if (Distance <= 2.0f)
	{
		bClosedDoorSettled = true;
	}
	else if (DeltaSeconds > 0.0f)
	{
		// 문에서 물러날 때는 빨리, 다가가거나 옆으로 옮길 때는 천천히.
		const FVector Direction = ToStand / Distance;
		const float Speed = FVector::DotProduct(Direction, FromFace.GetSafeNormal()) > 0.5f
			? IGListener::ClosedDoorRetreatSpeed
			: IGListener::ClosedDoorApproachSpeed;
		const float Step = FMath::Min(Distance, Speed * DeltaSeconds);
		// 가는 쪽이 계단 끝이거나 벽이면 거기서 선 것으로 친다. 그 자리에서 두드린다.
		if (!HasFloorBeneath(Here + Direction * Step))
		{
			bClosedDoorSettled = true;
		}
		else
		{
			FHitResult Hit;
			AddActorWorldOffset(Direction * Step, true, &Hit);
			bClosedDoorSettled = Hit.bBlockingHit;
		}
	}
	if (bClosedDoorSettled)
	{
		// 선 자리 앞 문짝을 친다. 소리도 거기서 난다.
		DoorKnockPoint = Door->GetKnockPoint(GetActorLocation() + FVector(0.0f, 0.0f, 40.0f));
	}
	return bClosedDoorSettled;
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
	if (HoldForWallRise())
	{
		LastMoveSpeed = 0.0f;
		return false;
	}
	// 네 발로 디딤판을 짚는다. 오를 때가 조금 더 느리다.
	const FVector Flat(Delta.X, Delta.Y, 0.0f);
	const float Scale = (Delta.Z > 2.0f
		? IGListener::StairClimbSpeedScale
		: IGListener::StairDescendSpeedScale) * TurnSpeedScale(Flat);
	const float Step = FMath::Min(Speed * Scale * DeltaSeconds, Distance);
	SetActorLocation(Feet + Delta / Distance * Step + FVector(0.0f, 0.0f, HalfHeight + 2.0f));
	if (!Flat.IsNearlyZero())
	{
		// 반 층 참에서 몸을 돌려 다음 계단으로 꺾는다. 평지보다 빨리 돌아야 몸이 옆으로
		// 미끄러지지 않는다.
		FaceDirection(Flat.GetSafeNormal(), DeltaSeconds, IGListener::StairTurnDegreesPerSecond);
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

bool AIGListenerEntity::StepAlongPose(
	const FVector& TargetFeet,
	const float TargetYaw,
	const float Speed,
	const float DeltaSeconds)
{
	bOnStairLeg = true;
	if (!PoseLegTarget.Equals(TargetFeet, 0.1f) || !FMath::IsNearlyEqual(PoseLegTargetYaw, TargetYaw, 0.1f))
	{
		PoseLegFrom = GetFeetLocation();
		PoseLegFromYaw = GetActorRotation().Yaw;
		PoseLegTarget = TargetFeet;
		PoseLegTargetYaw = TargetYaw;
		PoseLegDone = 0.0f;
	}
	if (HoldForWallRise())
	{
		LastMoveSpeed = 0.0f;
		return false;
	}
	// 옮겨 갈 거리와 도는 각 중 큰 쪽이 이 걸음의 길이다. 자리와 방향이 같이 끝난다.
	const float Turn = FMath::FindDeltaAngleDegrees(PoseLegFromYaw, TargetYaw);
	const float Length = FMath::Max3(static_cast<float>(FVector::Dist(PoseLegFrom, TargetFeet)),
		FMath::Abs(Turn) * IGListener::PoseTurnCentimetersPerDegree, 1.0f);
	const float Rate = Speed
		* (TargetFeet.Z > PoseLegFrom.Z + 2.0f ? IGListener::StairClimbSpeedScale : IGListener::StairDescendSpeedScale);
	PoseLegDone = FMath::Min(PoseLegDone + Rate * DeltaSeconds, Length);
	const float Alpha = PoseLegDone / Length;
	const float HalfHeight = Body ? Body->GetScaledCapsuleHalfHeight() : 58.0f;
	SetActorLocationAndRotation(
		FMath::Lerp(PoseLegFrom, TargetFeet, Alpha) + FVector(0.0f, 0.0f, HalfHeight + 2.0f),
		FRotator(0.0f, PoseLegFromYaw + Turn * Alpha, 0.0f));
	// 자세 점은 참 위라 평평하다. 계단 끝 단과 잇는 걸음만 단 높이만큼 기운다. 받친 자리가 다시 고른다.
	const FVector Delta = TargetFeet - PoseLegFrom;
	StairPitchTarget = FMath::Clamp(
		FMath::RadiansToDegrees(FMath::Atan2(Delta.Z, FMath::Max(Delta.Size2D(), 30.0f))),
		-IGListener::StairPitchLimit,
		IGListener::StairPitchLimit);
	LastMoveSpeed = DeltaSeconds > 0.0f ? Rate : 0.0f;
	StuckSeconds = 0.0f;
	if (Alpha < 1.0f)
	{
		return false;
	}
	PoseLegTarget = FVector(TNumericLimits<float>::Max());
	return true;
}

void AIGListenerEntity::ClearNavPath()
{
	NavPath.Reset();
	NavPathOnStair.Reset();
	NavPathPose.Reset();
	NavPathYaw.Reset();
	NavPathIndex = 0;
	PoseLegTarget = FVector(TNumericLimits<float>::Max());
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
		return CrawlTowards(Goal, Speed, DeltaSeconds, true);
	}
	const FVector GoalFeet = ResolveFloorBelow(World, Goal, this, CachedPlayer.Get());
	// 계단참에서 도는 중이면 그 길을 끝까지 돈다. 가운데에서 길을 다시 짜면 자세 없이 꺾여 손이 벽에 묻힌다.
	const bool bTurning = NavPath.IsValidIndex(NavPathIndex) && NavPathPose[NavPathIndex];
	// 도는 길에 막 들어서려는 참이면 쫓는 소리를 한 번 더 본다. 길을 짠 뒤 소리가 옮겨 갔으면 다른 쪽으로 돌아야
	// 한다. 그대로 돌면 출입구까지 나갔다가 거기서 되돌아서느라 제자리에서 반 바퀴 돌았다.
	const bool bEnteringTurn = bTurning && (NavPathIndex == 0 || !NavPathPose[NavPathIndex - 1])
		&& !PoseLegTarget.Equals(NavPath[NavPathIndex], 0.1f);
	if (!bTurning && CanCrawlStraightTo(GoalFeet))
	{
		ClearNavPath();
		const bool bArrived = CrawlTowards(Goal, Speed, DeltaSeconds, true);
		return bArrived || bBlockedByClosedDoor;
	}
	const double Now = World->GetTimeSeconds();
	const bool bReplan = !NavPath.IsValidIndex(NavPathIndex)
		|| (bEnteringTurn && State == EIGListenerState::Chasing
			&& FVector::DistSquared(GoalFeet, NavGoal) > FMath::Square(30.0f))
		|| (!bTurning
			&& (FVector::DistSquared(GoalFeet, NavGoal) > FMath::Square(IGListener::ReplanGoalShift)
				|| (State == EIGListenerState::Chasing
					&& Now - NavPlannedSeconds >= IGListener::ChaseReplanSeconds)));
	if (bReplan && !PlanNavPath(GoalFeet))
	{
		// 이어지는 길이 없다. 예전처럼 같은 층을 기어 간다.
		return CrawlTowards(Goal, Speed, DeltaSeconds, true);
	}
	const FVector Feet = GetFeetLocation();
	const FVector Target = NavPath[NavPathIndex];
	if (AIGSwingDoor* ClosedFireDoor = FindClosedFireDoorOnLeg(Feet, Target))
	{
		// 닫힌 방화문 앞. 문을 뚫고 지나가지 않는다.
		bBlockedByClosedDoor = true;
		BlockingDoor = ClosedFireDoor;
		ClearNavPath();
		return true;
	}
	// 계단탑 안은 미리 고른 길(계단 가운데, 층 참 가운데)만 지나므로 쓸지 않고 따라간다.
	// 디딤판 경사에서 층 참으로 올라선 몸은 참 바닥보다 몇 cm 낮아서, 쓸며 가면 참
	// 끝에 걸린다.
	const bool bStairLeg = NavPathOnStair[NavPathIndex]
		|| (NavPathIndex > 0 && NavPathOnStair[NavPathIndex - 1])
		|| FMath::Abs(Target.Z - Feet.Z) > 1.0f
		|| (FIGBuildingNav::IsInStairCore(Feet) && FIGBuildingNav::IsInStairCore(Target));
	bool bReached = false;
	if (NavPathPose[NavPathIndex])
	{
		bReached = StepAlongPose(Target, NavPathYaw[NavPathIndex], Speed, DeltaSeconds);
	}
	else if (bStairLeg)
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
		bReached = CrawlTowards(FlatTarget, Speed, DeltaSeconds, true);
		if (bBlockedByClosedDoor)
		{
			// 닫힌 문. 밀고 들어가지 않는다. 부르는 쪽이 문 앞에서 두드릴지 정한다.
			ClearNavPath();
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
		ClearNavPath();
		return true;
	}
	return false;
}

namespace
{
	/**
	 * 짚은 자리들(몸 가운데에서 앞뒤 거리, 디딤판 높이)의 윗볼록 껍질에서 몸 가운데를 걸치는 변의 기울기(도).
	 * 굳은 몸이 단 위에 얹히면 이 변에 걸쳐 눕는다. 한 경사 위에서는 그 경사, 평평한 참에서는 0이고, 경사가 바뀌는
	 * 단 끝에서는 무게 중심이 넘어갈 때 몸이 넘어간다. 짚은 자리가 둘이 안 되면 false.
	 */
	bool FindRestingPitch(TArray<FVector2D> Supports, float& OutPitch)
	{
		if (Supports.Num() < 2)
		{
			return false;
		}
		Supports.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
		TArray<FVector2D, TInlineAllocator<8>> Hull;
		for (const FVector2D& Point : Supports)
		{
			// 앞뒤 거리가 같은 두 자리(양손 따위)는 높은 쪽만 남긴다.
			if (!Hull.IsEmpty() && Point.X - Hull.Last().X < 1.0f)
			{
				if (Point.Y > Hull.Last().Y)
				{
					Hull.Pop();
				}
				else
				{
					continue;
				}
			}
			while (Hull.Num() >= 2)
			{
				const FVector2D& A = Hull[Hull.Num() - 2];
				const FVector2D& B = Hull.Last();
				// B가 A와 Point를 잇는 선 아래(또는 위)에 있으면 윗껍질이 아니다.
				if ((B.X - A.X) * (Point.Y - A.Y) - (B.Y - A.Y) * (Point.X - A.X) >= 0.0f)
				{
					Hull.Pop();
					continue;
				}
				break;
			}
			Hull.Add(Point);
		}
		int32 Edge = 0;
		while (Edge + 2 < Hull.Num() && Hull[Edge + 1].X < 0.0f)
		{
			++Edge;
		}
		const FVector2D& A = Hull[Edge];
		const FVector2D& B = Hull[FMath::Min(Edge + 1, Hull.Num() - 1)];
		if (B.X - A.X < 10.0f)
		{
			return false;
		}
		OutPitch = FMath::RadiansToDegrees(FMath::Atan((B.Y - A.Y) / (B.X - A.X)));
		return true;
	}
}

float AIGListenerEntity::TurnSpeedScale(const FVector& Direction) const
{
	const FVector Flat(Direction.X, Direction.Y, 0.0f);
	if (Flat.IsNearlyZero())
	{
		return 1.0f;
	}
	const float Error = FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Flat.Rotation().Yaw));
	// 5도까지는 그대로 가고, 35도 넘게 틀어져 있으면 거의 제자리에서 돈다. 기는 동작은 이어진다. 좁은 계단과 참에서는
	// 조금만 늦어도 머리가 벽에 닿는다. 계단탑 밖에서 늦추면 폭 140 cm 복도에서 거의 제자리로 돌며 머리와 다리가 양쪽
	// 벽을 쓸었다(4층 403호 문 앞). 걸으며 도는 편이 덜 닿는다.
	return FMath::GetMappedRangeValueClamped(FVector2D(5.0f, 35.0f), FVector2D(1.0f, 0.1f), Error);
}

bool AIGListenerEntity::IsBodyBlockedAlong(const FVector& Direction, const float Reach, const float SideOffset,
	const float Height, const bool bClosedDoorsBlock) const
{
	const UWorld* World = GetWorld();
	const FVector Forward = FVector(Direction.X, Direction.Y, 0.0f).GetSafeNormal();
	if (!World || Forward.IsNearlyZero())
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGListenerBodyRoom), false, this);
	if (const APawn* Player = CachedPlayer.Get())
	{
		Params.AddIgnoredActor(Player);
	}
	const FVector Side(-Forward.Y, Forward.X, 0.0f);
	const FVector From = GetFeetLocation() + FVector(0.0f, 0.0f, Height);
	const FVector To = From + Forward * Reach + Side * SideOffset;
	for (int32 Try = 0; Try < 4; ++Try)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
		{
			return false;
		}
		// 열린 문짝은 사람이 지나다니는 자리라 몸이 비켜 서지 않는다(관리실 문은 통로를 거의 다 막는다). 닫힌 문
		// 앞에 서는 일은 문마다 따로 맡는다(ClosedDoorStandOff, 403호 문 앞 자리).
		const AIGSwingDoor* Door = Cast<AIGSwingDoor>(Hit.GetActor());
		if (!Door || (bClosedDoorsBlock && !Door->IsOpen()))
		{
			return true;
		}
		Params.AddIgnoredActor(Door);
	}
	return false;
}

FVector AIGListenerEntity::SideClearanceShift(const FVector& Direction, const float DeltaSeconds) const
{
	const UWorld* World = GetWorld();
	const FVector Forward = FVector(Direction.X, Direction.Y, 0.0f).GetSafeNormal();
	if (!World || Forward.IsNearlyZero() || DeltaSeconds <= 0.0f)
	{
		return FVector::ZeroVector;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGListenerSideRoom), false, this);
	if (const APawn* Player = CachedPlayer.Get())
	{
		Params.AddIgnoredActor(Player);
	}
	// 몸 가운데, 팔꿈치, 손 자리에서 양옆으로 팔꿈치 살까지 긋는다. 모자란 만큼이 비켜야 할 거리다.
	const FVector Right(-Forward.Y, Forward.X, 0.0f);
	const FVector Base = GetFeetLocation() + FVector(0.0f, 0.0f, 12.0f);
	const float Reach = IGListener::SideReach + IGListener::SideSkin;
	float Short[2] = {0.0f, 0.0f};
	for (const float Along : {0.0f, 45.0f, 80.0f})
	{
		const FVector From = Base + Forward * Along;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, From, From + Right * (Side == 0 ? Reach : -Reach), ECC_Visibility, Params)
				&& !Hit.bStartPenetrating && Hit.Distance >= 1.0f)
			{
				Short[Side] = FMath::Max(Short[Side], Reach - Hit.Distance);
			}
		}
	}
	// 오른쪽이 모자라면 왼쪽으로, 양쪽이 다 모자라면 그 차이의 반만큼 가운데로. 한 프레임에 조금씩 옮긴다.
	const float Shift = FMath::Clamp((Short[1] - Short[0]) * 0.5f,
		-IGListener::SidestepSpeed * DeltaSeconds, IGListener::SidestepSpeed * DeltaSeconds);
	return Right * Shift;
}

FVector AIGListenerEntity::FitFacing(const FVector& Desired) const
{
	const FVector Wanted = FVector(Desired.X, Desired.Y, 0.0f).GetSafeNormal();
	// 계단 위에서는 앞뒤가 늘 디딤판에 막힌다. 계단 방향 그대로 둔다.
	if (Wanted.IsNearlyZero() || AIGPrologueWorldScene::IsOnStairFlight(GetActorLocation()))
	{
		return Desired;
	}
	// 손끝·머리·발끝과 팔꿈치·무릎 자리 중 막힌 곳 수.
	const auto CountBlocked = [this](const FVector& Facing)
	{
		int32 Blocked = 0;
		Blocked += IsBodyBlockedAlong(Facing, IGListener::FrontReach, 0.0f, 12.0f, true) ? 1 : 0;
		Blocked += IsBodyBlockedAlong(Facing, IGListener::FrontReach, 0.0f, 45.0f, true) ? 1 : 0;
		Blocked += IsBodyBlockedAlong(-Facing, IGListener::RearReach, 0.0f, 12.0f, true) ? 1 : 0;
		for (const float Side : {IGListener::SideReach, -IGListener::SideReach})
		{
			Blocked += IsBodyBlockedAlong(Facing, 50.0f, Side, 12.0f, true) ? 1 : 0;
			Blocked += IsBodyBlockedAlong(-Facing, 40.0f, Side, 12.0f, true) ? 1 : 0;
		}
		return Blocked;
	};
	// 지금 방향에서 그 방향까지 제자리로 도는 사이를 20도마다 본다. 고른 방향이 비어도 도는 동안 머리나 발이 벽을
	// 쓸고 지나갔다(4층 복도 동쪽 끝에서 83도를 돌며 종아리가 벽 속을 지났다).
	const float CurrentYaw = GetActorRotation().Yaw;
	const auto TurnIsClear = [&CountBlocked, CurrentYaw](const float Yaw)
	{
		const float Turn = FMath::FindDeltaAngleDegrees(CurrentYaw, Yaw);
		const int32 Steps = FMath::FloorToInt(FMath::Abs(Turn) / 20.0f);
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			if (CountBlocked(FRotator(0.0f, CurrentYaw + Turn * Step / (Steps + 1), 0.0f).Vector()) > 0)
			{
				return false;
			}
		}
		return true;
	};
	// 바라는 쪽에서 가까운 차례로 돌려 보며, 그 방향도 비고 돌아가는 사이도 비는 방향을 고른다. 그런 방향이 없고 지금
	// 방향이 비면 돌지 않고 그대로 듣는다. 다 막히면 가장 덜 막히는 방향이다. 복도(폭 140 cm)에서는 몸이 벽을 향해
	// 서지 못하고 복도를 따라 비스듬히 선다.
	static const float Offsets[] = {0.0f, 15.0f, -15.0f, 30.0f, -30.0f, 45.0f, -45.0f, 60.0f, -60.0f, 75.0f, -75.0f,
		90.0f, -90.0f, 120.0f, -120.0f, 150.0f, -150.0f, 180.0f};
	const float WantedYaw = Wanted.Rotation().Yaw;
	FVector Best = Wanted;
	int32 BestBlocked = TNumericLimits<int32>::Max();
	for (const float Offset : Offsets)
	{
		const FVector Facing = FRotator(0.0f, WantedYaw + Offset, 0.0f).Vector();
		const int32 Blocked = CountBlocked(Facing);
		if (Blocked == 0 && TurnIsClear(WantedYaw + Offset))
		{
			return Facing;
		}
		if (Blocked < BestBlocked)
		{
			BestBlocked = Blocked;
			Best = Facing;
		}
	}
	const FVector Current = FRotator(0.0f, CurrentYaw, 0.0f).Vector();
	return CountBlocked(Current) == 0 ? Current : Best;
}

FVector AIGListenerEntity::GetListeningFacing()
{
	// 닫힌 문과 403호 문 앞에서는 문을 본다. 서는 자리는 문마다 따로 맞춘다.
	if (bAtClosedDoor || bAtHomeDoor || AttentionDirection.IsNearlyZero())
	{
		return AttentionDirection;
	}
	const FVector Attention = AttentionDirection.GetSafeNormal2D();
	if (FVector::DotProduct(FittedAttentionFor, Attention) < 0.996f
		|| FVector::DistSquared2D(FittedAttentionAt, GetActorLocation()) > FMath::Square(10.0f))
	{
		FittedAttentionFor = Attention;
		FittedAttentionAt = GetActorLocation();
		FittedAttention = FitFacing(Attention);
	}
	return FittedAttention;
}

void AIGListenerEntity::GatherStairSupports(const float Pitch, const float Reach, TArray<FVector2D>& OutSupports) const
{
	OutSupports.Reset();
	const UWorld* World = GetWorld();
	if (!World || !Body || !ListenerSkeletal || StairHandBones.IsEmpty())
	{
		return;
	}
	// 띄우기 전의 몸. 메시는 캡슐 바닥에 붙어 있고 그 자리를 축으로 눕는다.
	const FTransform MeshToWorld =
		FTransform(FRotator(Pitch, 0.0f, 0.0f), FVector(0.0f, 0.0f, -58.0f)) * Body->GetComponentTransform();
	const FVector Origin = MeshToWorld.GetLocation();
	const FVector Forward = FVector(GetActorForwardVector().X, GetActorForwardVector().Y, 0.0f).GetSafeNormal();
	const float Slope = FMath::Tan(FMath::DegreesToRadians(Pitch));
	// 메시 좌표의 뼈 자리. 메시 원점이 몸 바닥면이라 Z가 곧 바닥면에서 뜬 높이다.
	TArray<FVector, TInlineAllocator<20>> Points;
	const auto Bone = [this](const int32 Index)
	{
		return ListenerSkeletal->GetBoneTransform(Index, FTransform::Identity).GetLocation();
	};
	for (int32 Side = 0; Side < StairHandBones.Num(); ++Side)
	{
		const FVector Knee = Bone(StairKneeBones[Side]);
		const FVector Ankle = Bone(StairAnkleBones[Side]);
		const FVector Hand = Bone(StairHandBones[Side]);
		const FVector Elbow = Bone(StairElbowBones[Side]);
		// 바닥을 짚은 팔은 위팔과 아래팔 가운데도 바닥에 닿는다(두드릴 때 버티는 팔).
		Points.Add(Hand);
		Points.Add((Hand + Elbow) * 0.5f);
		Points.Add(Elbow);
		Points.Add((Elbow + Bone(StairShoulderBones[Side])) * 0.5f);
		Points.Add(Knee);
		Points.Add(Ankle);
		Points.Add(Ankle + (Ankle - Knee).GetSafeNormal() * 14.0f);
		// 넓적다리와 종아리 가운데. 계단을 내려가기 시작할 때 무릎은 아랫단, 발목은 참 위에 있어 그 사이 다리가 참
		// 끝 모서리 밑으로 지나갔다.
		Points.Add((Bone(StairHipBones[Side]) + Knee) * 0.5f);
		Points.Add((Knee + Ankle) * 0.5f);
	}
	if (StairPelvisBone != INDEX_NONE)
	{
		Points.Add(Bone(StairPelvisBone));
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGListenerStairSupport), false, this);
	if (const APawn* Player = CachedPlayer.Get())
	{
		Params.AddIgnoredActor(Player);
	}
	const FQuat Facing = FRotator(0.0f, GetActorRotation().Yaw, 0.0f).Quaternion();
	const FCollisionShape Probe = FCollisionShape::MakeBox(IGListener::StairProbeHalfExtent);
	for (const FVector& Local : Points)
	{
		const FVector At = MeshToWorld.TransformPosition(Local);
		const float PlaneZ = Origin.Z + FVector::DotProduct(At - Origin, Forward) * Slope;
		FHitResult Hit;
		// 벽 속에서 시작하거나 옆면에 먼저 닿은 것은 짚을 자리가 아니다.
		if (!World->SweepSingleByChannel(Hit, FVector(At.X, At.Y, PlaneZ + Reach),
				FVector(At.X, At.Y, PlaneZ - Reach), Facing, ECC_Visibility, Probe, Params)
			|| Hit.bStartPenetrating || Hit.Normal.Z < 0.5f)
		{
			continue;
		}
		// 든 뼈는 든 만큼 바닥이 더 솟아야 닿는다. 그만큼 깎아 둔다.
		const float Clearance = FMath::Max(0.0f, Local.Z - IGListener::FloorContactHeight);
		OutSupports.Add(FVector2D(FVector::DotProduct(Hit.ImpactPoint - Origin, Forward), Hit.ImpactPoint.Z - Origin.Z - Clearance));
	}
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
	// 짚은 자리 밑 바닥에 맞춘다. 계단에서 캡슐은 디딤판 가운데를 잇는 선을 따라가서 단 모서리보다 8 cm 낮고,
	// 단 끝에서는 걸음 방향의 기울기가 몸 길이(190 cm)를 따라가지 못한다. 그대로 두면 무릎과 정강이가 단 모서리를
	// 파고든다. 캡슐은 높이를 따라 오르지 않아서, 계단탑 밖에서도 403호 현관 신발 단(8 cm) 같은 곳에서는 손과 팔이
	// 바닥에 묻힌다. 밖에서는 위아래 15 cm 안의 바닥만 본다. 그보다 높은 것은 가구 윗면이라 짚지 않는다.
	TArray<FVector2D> Supports;
	if (!bDormant && !IsHidden())
	{
		GatherStairSupports(StairBodyPitch, FIGBuildingNav::IsInStairCore(GetFeetLocation())
			? IGListener::StairProbeReach : IGListener::FloorProbeReach, Supports);
	}
	float RestingPitch = 0.0f;
	if (FindRestingPitch(Supports, RestingPitch))
	{
		StairPitchTarget = FMath::Clamp(RestingPitch, -IGListener::StairPitchLimit, IGListener::StairPitchLimit);
	}
	const float Previous = StairBodyPitch;
	const float PreviousLift = StairBodyLift;
	StairBodyPitch = FMath::FInterpTo(StairBodyPitch, StairPitchTarget, DeltaSeconds, 7.0f);
	if (FMath::Abs(StairBodyPitch) < 0.05f && FMath::Abs(StairPitchTarget) < 0.05f)
	{
		StairBodyPitch = 0.0f;
	}
	// 지금 기울기의 몸 바닥면보다 높은 디딤판이 있으면 그만큼 띄운다. 오를 때는 바로, 내릴 때는 천천히.
	const float Slope = FMath::Tan(FMath::DegreesToRadians(StairBodyPitch));
	float LiftTarget = 0.0f;
	for (const FVector2D& Support : Supports)
	{
		LiftTarget = FMath::Max(LiftTarget, Support.Y - Support.X * Slope);
	}
	LiftTarget = FMath::Min(LiftTarget, IGListener::StairLiftLimit);
	StairBodyLift = FMath::FInterpTo(StairBodyLift, LiftTarget, DeltaSeconds, LiftTarget > StairBodyLift ? 30.0f : 8.0f);
	if (StairBodyLift < 0.05f && LiftTarget <= 0.0f)
	{
		StairBodyLift = 0.0f;
	}
	if (!FMath::IsNearlyEqual(Previous, StairBodyPitch, 0.01f) || !FMath::IsNearlyEqual(PreviousLift, StairBodyLift, 0.01f))
	{
		// 메시 원점은 몸 가운데 바닥이다. 그 점을 축으로 눕히면 손은 윗단, 발은 아랫단에 닿는다.
		ListenerSkeletal->SetRelativeLocationAndRotation(
			FVector(0.0f, 0.0f, -58.0f + StairBodyLift), FRotator(StairBodyPitch, 0.0f, 0.0f));
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
	// 벽에 귀를 대는 자세. 없으면 벽 듣기는 평소 듣기로 내려간다.
	ListenWallInAnim = LoadAnim(TEXT("/Game/Meshes/A_ListenerCrawler_ListenWallIn.A_ListenerCrawler_ListenWallIn"));
	ListenWallAnim = LoadAnim(TEXT("/Game/Meshes/A_ListenerCrawler_ListenWall.A_ListenerCrawler_ListenWall"));
	ListenWallOutAnim = LoadAnim(TEXT("/Game/Meshes/A_ListenerCrawler_ListenWallOut.A_ListenerCrawler_ListenWallOut"));
	BangWallAnim = LoadAnim(TEXT("/Game/Meshes/A_ListenerCrawler_BangWall.A_ListenerCrawler_BangWall"));
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
	// 관통을 재는 검사 실행은 화면을 그리지 않는다(-nullrhi). 그대로면 뼈가 처음 자세에 멈춰 있어 실제 동작을 못 잰다.
	if (IGClipAudit::IsRequested())
	{
		Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
	Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	ListenerSkeletal = Component;
	for (const TCHAR* Side : {TEXT("l"), TEXT("r")})
	{
		StairHandBones.Add(Component->GetBoneIndex(FName(FString::Printf(TEXT("hand_%s"), Side))));
		StairElbowBones.Add(Component->GetBoneIndex(FName(FString::Printf(TEXT("lowerarm_%s"), Side))));
		StairShoulderBones.Add(Component->GetBoneIndex(FName(FString::Printf(TEXT("upperarm_%s"), Side))));
		StairHipBones.Add(Component->GetBoneIndex(FName(FString::Printf(TEXT("thigh_%s"), Side))));
		StairKneeBones.Add(Component->GetBoneIndex(FName(FString::Printf(TEXT("calf_%s"), Side))));
		StairAnkleBones.Add(Component->GetBoneIndex(FName(FString::Printf(TEXT("foot_%s"), Side))));
	}
	StairPelvisBone = Component->GetBoneIndex(TEXT("pelvis"));
	if (StairHandBones.Contains(INDEX_NONE) || StairElbowBones.Contains(INDEX_NONE) || StairShoulderBones.Contains(INDEX_NONE)
		|| StairHipBones.Contains(INDEX_NONE) || StairKneeBones.Contains(INDEX_NONE) || StairAnkleBones.Contains(INDEX_NONE))
	{
		StairHandBones.Reset();
		StairElbowBones.Reset();
		StairShoulderBones.Reset();
		StairHipBones.Reset();
		StairKneeBones.Reset();
		StairAnkleBones.Reset();
	}
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
	case EIGListenerBodyAnim::ListenWallIn: Sequence = ListenWallInAnim; break;
	case EIGListenerBodyAnim::ListenWall: Sequence = ListenWallAnim; break;
	case EIGListenerBodyAnim::ListenWallOut: Sequence = ListenWallOutAnim; break;
	case EIGListenerBodyAnim::BangWall: Sequence = BangWallAnim ? BangWallAnim.Get() : BangAnim.Get(); break;
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

bool AIGListenerEntity::HasWallPoseAnims() const
{
	return ListenWallInAnim && ListenWallAnim && ListenWallOutAnim;
}

bool AIGListenerEntity::IsInWallPose() const
{
	return ActiveBodyAnim == EIGListenerBodyAnim::ListenWallIn
		|| ActiveBodyAnim == EIGListenerBodyAnim::ListenWall
		|| ActiveBodyAnim == EIGListenerBodyAnim::BangWall;
}

void AIGListenerEntity::SetWallListenForBeat(const bool bEnable, const FVector& Anchor, const float Yaw)
{
	bWallListenForBeat = bEnable;
	if (bEnable)
	{
		WallListenAnchor = Anchor;
		WallListenYaw = Yaw;
		WallReenterAfterSeconds = -1000.0;
		return;
	}
	// 연출이 끝났다. 붙어 있던 몸은 떨어지는 동작부터 튼다.
	if (IsInWallPose())
	{
		HoldForWallRise();
	}
}

bool AIGListenerEntity::HoldForWallRise()
{
	const UWorld* World = GetWorld();
	if (!World || !ListenerSkeletal)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	if (IsInWallPose() && ListenWallOutAnim)
	{
		// 벽에 붙은 몸이 기기 전에 먼저 벽에서 떨어진다. 붙은 채로 미끄러지지 않는다.
		PlayBodyAnim(EIGListenerBodyAnim::ListenWallOut, false, 1.0f);
		WallRiseUntilSeconds = Now + ListenWallOutAnim->GetPlayLength();
		WallReenterAfterSeconds = WallRiseUntilSeconds + IGListener::WallReenterDelaySeconds;
	}
	return Now < WallRiseUntilSeconds;
}

void AIGListenerEntity::EnsureWallPose(const float Rate)
{
	// 벽과 나란히 둔다. 소리 쪽으로 몸을 돌리면 벽에서 떨어지니, 귀를 댄 채 듣는다.
	SetActorRotation(FRotator(0.0f, WallListenYaw, 0.0f));
	switch (ActiveBodyAnim)
	{
	case EIGListenerBodyAnim::ListenWall:
		ListenerSkeletal->SetPlayRate(Rate);
		return;
	case EIGListenerBodyAnim::ListenWallIn:
	case EIGListenerBodyAnim::BangWall:
		// 붙는 동작과 벽 치기는 끝까지 튼 뒤에 듣기로 넘어간다.
		if (ListenerSkeletal->IsPlaying())
		{
			return;
		}
		PlayBodyAnim(EIGListenerBodyAnim::ListenWall, true, Rate);
		return;
	default:
		PlayBodyAnim(EIGListenerBodyAnim::ListenWallIn, false, 1.0f);
		return;
	}
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
	const double PoseNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	// 벽에서 떨어지는 동안은 그 동작을 끝까지 튼다. 몸도 그동안 기지 않는다(HoldForWallRise).
	if (ActiveBodyAnim == EIGListenerBodyAnim::ListenWallOut && PoseNow < WallRiseUntilSeconds)
	{
		return;
	}
	const bool bAtWall = bWallListenForBeat
		&& HasWallPoseAnims()
		&& PoseNow >= WallReenterAfterSeconds
		&& FVector::DistSquared2D(GetActorLocation(), WallListenAnchor)
			<= FMath::Square(IGListener::WallListenAnchorRadius);
	switch (State)
	{
	case EIGListenerState::CaptureHold:
	{
		// 쓰러진 눈을 향해 기어 올라온다. 멀리서는 빠르고 가까워질수록 느려져, 끊기기
		// 조금 전 눈앞에서 멈춘다. 멈춘 뒤에는 꼼짝 않고 듣는다. 그녀의 숨소리를 듣는
		// 것이다. 물러났다 달려들거나 프레임을 끊어 튀는 동작은 쓰지 않는다. 바닥을
		// 두드리는 노크 하나에 화면이 끊긴다.
		const AIGPlayerCharacter* Victim = CaptureVictim.Get();
		const float CutSeconds = Victim && Victim->GetCaptureCutSeconds() > 0.0f
			? Victim->GetCaptureCutSeconds()
			: IGListener::CaptureCutSeconds;
		const float Arrive = FMath::Clamp(
			StateSeconds / (CutSeconds * IGListener::CaptureArriveFraction), 0.0f, 1.0f);
		PlayBodyAnim(EIGListenerBodyAnim::Crawl, true, 1.6f * FMath::Square(1.0f - Arrive));
		const FVector Wanted = FMath::Lerp(
			CaptureFaceStart, CaptureViewTarget, FMath::InterpEaseOut(0.0f, 1.0f, Arrive, 2.2f));
		ListenerSkeletal->AddWorldOffset(Wanted - GetCaptureFaceLocation());
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
		// 벽에 귀를 댄 채면 머리 앞 벽을 친다. 소리 쪽으로 돌아보지 않는다.
		if (bAtWall)
		{
			SetActorRotation(FRotator(0.0f, WallListenYaw, 0.0f));
			PlayBodyAnim(EIGListenerBodyAnim::BangWall, false, KnockRate);
			return;
		}
		if (!bClosedDoorSettled)
		{
			PlayBodyAnim(EIGListenerBodyAnim::Listen, true, 1.0f);
			return;
		}
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
		if (bAtWall)
		{
			EnsureWallPose(bWaitStirred ? 0.6f : 0.0f);
			return;
		}
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
	if (bAtWall && State != EIGListenerState::Chasing)
	{
		EnsureWallPose(1.0f);
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
	KnockDoor.Reset();
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
		PlayDoorHit();
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

void AIGListenerEntity::PlayClosedDoorKnock()
{
	// 문 노크는 밤2 대본 노크와 같은 간격이다. 티어 빠르기는 싣지 않는다.
	KnockRate = 1.0f;
	const AIGSwingDoor* Door = KnockDoor.Get();
	const EIGDoorKnockSurface Surface = Door ? Door->GetKnockSurface() : EIGDoorKnockSurface::Steel;
	const bool bHollow = Surface == EIGDoorKnockSurface::Hollow;
	const bool bRecorded = IGAudio::Sample(
		bHollow ? IGListener::HollowDoorSamples[0] : IGListener::DoorSteelSamples[0]) != nullptr;
	// 철문은 403호 문처럼 합성이 문짝 너머의 저역을 깐다. 녹음이 없으면 합성이 혼자 낸다.
	if (Surface == EIGDoorKnockSurface::Steel || !bRecorded)
	{
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateWallKnockTriple(this, IGListener::DoorKnockMuffle),
			DoorKnockPoint,
			bRecorded ? IGListener::DoorKnockUnderlayVolume : IGListener::DoorKnockVolume,
			1.0f,
			IGListener::DoorKnockInnerRadius,
			IGListener::DoorKnockFalloff,
			EIGAudioBus::Entity);
	}
	// 첫 타는 지금 친다. 나머지 둘은 두드리는 동안 TickState가 친다.
	DoorSteelHitsPlayed = bRecorded ? 0 : IGListener::DoorSteelHitCount;
	if (bRecorded)
	{
		PlayDoorHit();
	}
	if (UWorld* World = GetWorld())
	{
		if (UIGRecordingSubsystem* Recording =
			World->GetSubsystem<UIGRecordingSubsystem>())
		{
			Recording->RecordEntitySound(DoorKnockPoint, 0.55f, this);
		}
	}
	EmitPresentationCue(DoorKnockPoint, 1.0f, IGListener::DoorKnockInnerRadius + IGListener::DoorKnockFalloff);
	OnKnocked.Broadcast(DoorKnockPoint);
	if (IsNearForCaption(DoorKnockPoint))
	{
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			bHollow
				? NSLOCTEXT("IGMissingFloor", "EntityHollowDoorKnockCaption", "욕실 문을 세 번 두드리는 소리")
				: Surface == EIGDoorKnockSurface::Glass
					? NSLOCTEXT("IGMissingFloor", "EntityGlassDoorKnockCaption", "유리문을 세 번 두드리는 소리")
					: NSLOCTEXT("IGMissingFloor", "EntitySteelDoorKnockCaption", "철문을 세 번 두드리는 소리"),
			2.4f,
			DoorKnockPoint);
	}
	// 문 바로 너머에 있으면 놀란다. 403호 문과 같은 거리다.
	if (AIGPlayerCharacter* PlayerCharacter =
		Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		const FVector PlayerAt = PlayerCharacter->GetActorLocation();
		UIGStressComponent* Stress = PlayerCharacter->GetStress();
		if (Stress
			&& FMath::Abs(PlayerAt.Z - DoorKnockPoint.Z) <= FloorHeightThreshold
			&& FVector::Dist2D(PlayerAt, DoorKnockPoint) <= IGListener::DoorKnockStartleCentimeters)
		{
			Stress->ApplyScare(0.25f);
		}
	}
}

void AIGListenerEntity::PlayDoorHit()
{
	const int32 HitIndex = DoorSteelHitsPlayed;
	if (HitIndex < 0 || HitIndex >= IGListener::DoorSteelHitCount)
	{
		return;
	}
	++DoorSteelHitsPlayed;
	// 403호 문은 KnockDoor가 비어 있고 철문이다.
	const AIGSwingDoor* Door = KnockDoor.Get();
	const EIGDoorKnockSurface Surface = Door ? Door->GetKnockSurface() : EIGDoorKnockSurface::Steel;
	const bool bHollow = Surface == EIGDoorKnockSurface::Hollow;
	const bool bGlass = Surface == EIGDoorKnockSurface::Glass;
	if (USoundBase* Hit = IGAudio::Sample(
			bHollow ? IGListener::HollowDoorSamples[HitIndex] : IGListener::DoorSteelSamples[HitIndex]))
	{
		IGAudio::SpawnOneShotAt(
			this,
			Hit,
			DoorKnockPoint,
			bHollow ? IGListener::HollowDoorVolume
				: IGListener::DoorKnockVolume * (bGlass ? IGListener::GlassDoorVolumeScale : 1.0f),
			bHollow ? IGListener::HollowDoorPitches[HitIndex]
				: IGListener::DoorSteelPitches[HitIndex] * (bGlass ? IGListener::GlassDoorPitchScale : 1.0f),
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
		// 플레이어는 발목을 잡혀 바닥으로 끌려 내려가 괴물 쪽을 조금 올려다본다
		// (AIGPlayerCharacter의 포획 시점). 기는 자세의 얼굴은 위를 보고 있어서 이 각도에서
		// 가장 잘 읽힌다. 얼굴은 이 시선을 따라 눈앞 45cm까지 기어 올라와 멈춘다. 머리가
		// 통째로 화면에 들어오는 거리다. 20~28cm에서는 얼굴이 화면을 채워 뭉개진 덩어리로
		// 보였다. 주먹 쥔 두 팔을 가슴 앞으로 드는 덮치기 동작은 쓰지 않는다. 권투 자세로 읽혔다.
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
		CaptureViewTarget = CaptureFallenEye + CaptureStrikeLine * IGListener::CaptureFaceStopCentimeters;
		// 바닥에 떨어진 손전등 자리. 기어 오는 얼굴이 이 빛 속으로 들어온다.
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
