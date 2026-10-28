#include "Entity/IGManagerPatrol.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGLiningEntity.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Entity/IGWalkerKit.h"
#include "GameFramework/Controller.h"
#include "Engine/SkeletalMesh.h"
#include "Interaction/IGFireDoorWedge.h"
#include "Interaction/IGHidingSpot.h"
#include "Interaction/IGSwingDoor.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGMissingFloorLeftoverDirector.h"
#include "Narrative/IGStoryStateSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"
#include "TimerManager.h"

namespace IGManagerPatrol
{
	constexpr float DutyCheckSeconds = 0.25f;
	/** 그 시간이 시작하고 처음 나오기까지. 깨고 6초의 바람과 철문(3-1)을 지나서다. */
	constexpr float FirstRestSeconds = 24.0f;
	/** 한 바퀴 돌고 안쪽 방에 들어가 있는 시간. 바퀴마다 조금씩 다르다. */
	constexpr float RoundRestSeconds = 38.0f;
	constexpr float RoundRestJitterSeconds = 16.0f;
	/** 잡고 난 뒤, 노크에 쫓겨 들어간 뒤. */
	constexpr float CaptureRestSeconds = 40.0f;
	constexpr float KnockRestSeconds = 45.0f;

	// 걸음. 슬리퍼를 끄는 노인이다. 돌 때는 걷는 그녀(175)보다 느리다. 쫓을 때는
	// 허둥지둥 뛰어 걷는 그녀보다 빠르고, 달리는 그녀(410)는 못 따라잡는다.
	// 걸어서 도망치면 잡힌다.
	constexpr float WalkSpeed = 105.0f;
	constexpr float InvestigateSpeed = 140.0f;
	constexpr float ChaseSpeed = 340.0f;
	constexpr float RetreatSpeed = 200.0f;
	constexpr float WalkStride = 52.0f;
	constexpr float RunStride = 74.0f;

	// 손전등. 노란 구형 손전등이라 그녀의 흰 LED와 색으로 갈린다. 빛은 허리 앞에
	// 쥔 오른손의 렌즈에서 나간다(SK_MokHansooPatrol의 쥔 자세와 같은 자리).
	constexpr float TorchIntensity = 5200.0f;
	constexpr float TorchRange = 1600.0f;
	constexpr float TorchInnerCone = 12.0f;
	constexpr float TorchOuterCone = 25.0f;
	const FVector TorchOffset(40.0f, 19.0f, 96.0f);

	// 리깅한 몸의 걸음 주기. Scripts/blender/rig_walker.py의 보폭과 같다.
	/** Walk 한 주기(1.2초)에 두 걸음 46 cm씩. */
	constexpr float WalkCycleSpeed = 0.92f * 100.0f / 1.2f;
	/** Run 한 주기(0.8초)에 두 걸음 66 cm씩. */
	constexpr float RunCycleSpeed = 1.32f * 100.0f / 0.8f;
	/** 숨는 걸 보고 그 가구 앞까지 가는 데 주는 시간. 못 닿으면 놓친 것이다. */
	constexpr double HideApproachTimeoutSeconds = 9.0;
	/** 가구 앞에서 손이 닿는 거리. */
	constexpr float HideReach = 95.0f;
	/** 수색에서 더 들러 볼 점의 수와 거리. */
	constexpr int32 SearchExtraStops = 2;
	constexpr float SearchStopRadius = 900.0f;
	constexpr float SearchStopDwellSeconds = 3.0f;

	// 보는 것. 원뿔 안 12 m, 반각 24도. 가까울수록 빨리 알아본다.
	constexpr float SightRange = 1200.0f;
	constexpr float SightHalfAngle = 24.0f;
	/** 등 뒤라도 이만큼 붙으면 인기척을 안다. */
	constexpr float CloseSenseRadius = 140.0f;
	/** 놓치고 나서 찾으러 가기까지. */
	constexpr float LostSightSeconds = 3.5f;
	/** 들은 소리가 이보다 작으면 그냥 지나간다. 걷는 발소리(0.2 남짓)는 못 듣는다. */
	constexpr float HearingThreshold = 0.3f;
	/** 한 층 넘게 떨어진 소리는 듣지 않는다. */
	constexpr float HearingHeight = 340.0f;
	/** 위층 사람의 노크가 이 안이면 굳는다. */
	constexpr float KnockReach = 1500.0f;
	constexpr float FreezeSeconds = 2.5f;
	constexpr float StareSeconds = 5.0f;
	/** 2·3층 방화문을 여는 소리. 조심하지 않는다. 그의 건물이다. */
	constexpr float FireDoorOpenLoudness = 0.3f;
	/** 이 높이 위의 방화문이 4층 문이다. 그 문은 열지 않는다. */
	constexpr float FourthFloorFireDoorZ = 800.0f;
	constexpr float InvestigateDwellSeconds = 4.0f;
	constexpr float SearchDwellSeconds = 6.0f;
	/** 손이 닿는 거리. 발끼리 잰다. */
	constexpr float CatchReach = 80.0f;
	constexpr float CatchHeight = 70.0f;
	/** 잡고 나서 화면이 끊기기까지. 손전등이 얼굴에 들이대지는 시간이다. */
	constexpr float CatchHoldSeconds = 0.55f;
	/** 추격 없음 난이도에서 비추고 서 있는 시간. */
	constexpr float HarmlessHoldSeconds = 1.3f;

	/** 소리가 들리는 거리. 자막도 이 안에서만 띄운다. */
	constexpr float AudibleDistance = 1600.0f;

	// 밤2 손수레 심부름. 손수레는 그의 앞 60 cm에 손잡이를 그에게 두고 붙는다. 미는 걸음은
	// 느리고, 바퀴 하나가 일정하게 끽끽거린다. 사슬은 6~9초마다 한 번씩 철컥거린다.
	constexpr float CartPushSpeed = 50.0f;
	const FVector CartHoldOffset(60.0f, 0.0f, 0.0f);
	constexpr double CartTurnSeconds = 5.0;
	constexpr double CartParkSeconds = 2.0;
	constexpr double ChainIntervalMin = 6.0;
	constexpr double ChainIntervalJitter = 3.0;
	constexpr double SqueakIntervalSeconds = 0.85;
	constexpr float ErrandCaptionGapSeconds = 7.0f;

	// 열쇠 찾기(밤3, 한 번). 도어락 번호를 누르고 문이 걸리는지 보기까지, 열쇠를 꺼내 자물쇠를
	// 돌리기까지, 문간에서 비추는 시간, 걸린 문 앞에서 서 있는 시간, 문을 닫고 돌아서기까지.
	constexpr double HuntKeypadSeconds = 2.0;
	constexpr double HuntKeysToSwingSeconds = 1.1;
	constexpr double HuntThresholdSeconds = 3.2;
	constexpr double HuntCaughtWaitSeconds = 2.4;
	constexpr double HuntCloseWaitSeconds = 1.2;
	/** 문간에서 좌우로 훑는 폭. */
	constexpr float HuntSweepDegrees = 40.0f;
	/** 유담이 이보다 가까이 있으면 옥상 철문을 닫지 않는다. 닫으면 문간에 선 몸을 문짝이 뚫고 지나간다. */
	constexpr float HuntDoorCloseClearance = 260.0f;
	const FName KeyHuntBeat(TEXT("Night3.MokKeyHunt"));
}

AIGManagerPatrol::AIGManagerPatrol()
{
	// 순찰은 걸음과 손전등, 시야 판정이 매 프레임 일이다. 당번이 아닐 때는 꺼 둔다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(Root);
	BodyPivot->SetMobility(EComponentMobility::Movable);

	const auto MakeBodyPart = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(BodyPivot);
		Part->SetMobility(EComponentMobility::Movable);
		// 메시 정면이 -X다. 액터의 +X가 그가 보는 쪽이 되게 몸만 돌려 붙인다.
		Part->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(true);
		return Part;
	};
	Workwear = MakeBodyPart(TEXT("Workwear"));
	HeadHands = MakeBodyPart(TEXT("HeadHands"));

	Torch = CreateDefaultSubobject<USpotLightComponent>(TEXT("Torch"));
	Torch->SetupAttachment(BodyPivot);
	Torch->SetMobility(EComponentMobility::Movable);
	Torch->SetRelativeLocation(IGManagerPatrol::TorchOffset);
	Torch->SetRelativeRotation(TorchRelative);
	Torch->SetInnerConeAngle(IGManagerPatrol::TorchInnerCone);
	Torch->SetOuterConeAngle(IGManagerPatrol::TorchOuterCone);
	Torch->SetAttenuationRadius(IGManagerPatrol::TorchRange);
	Torch->SetIntensity(IGManagerPatrol::TorchIntensity);
	Torch->SetLightColor(FLinearColor(1.0f, 0.86f, 0.64f));
	Torch->SetSourceRadius(2.0f);
	Torch->SetSoftSourceRadius(4.0f);
	// 그림자가 없으면 원뿔이 슬래브를 뚫고 아래층 바닥에 빛자국을 남긴다.
	Torch->SetCastShadows(true);
	Torch->SetVolumetricScatteringIntensity(1.6f);
	Torch->SetVisibility(false);
}

void AIGManagerPatrol::BeginPlay()
{
	Super::BeginPlay();
	if (UStaticMesh* WorkwearMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Meshes/SM_MokHansooWorkwear.SM_MokHansooWorkwear")))
	{
		Workwear->SetStaticMesh(WorkwearMesh);
	}
	if (UStaticMesh* HeadHandsMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Meshes/SM_MokHansooHeadHands.SM_MokHansooHeadHands")))
	{
		HeadHands->SetStaticMesh(HeadHandsMesh);
	}
	// 밤4의 목한수와 같은 재질이다. 작업복은 검은 도장, 얼굴과 손은 바랜 종이색.
	if (UMaterialInterface* Dark = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Prototype/Materials/M_PlasticDark.M_PlasticDark")))
	{
		Workwear->SetMaterial(0, Dark);
	}
	if (UMaterialInterface* Pale = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Prototype/Materials/M_PaperOld.M_PaperOld")))
	{
		HeadHands->SetMaterial(0, Pale);
	}
	BuildSkeletalBody();
	ShowBody(false);
	BuildGraph();
}

bool AIGManagerPatrol::BuildSkeletalBody()
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(
		nullptr, TEXT("/Game/Meshes/SK_MokHansooPatrol.SK_MokHansooPatrol"), nullptr, LOAD_NoWarn);
	if (!Mesh)
	{
		return false;
	}
	const auto LoadAnim = [](const TCHAR* Name) -> UAnimSequence*
	{
		return LoadObject<UAnimSequence>(
			nullptr, *FString::Printf(TEXT("/Game/Meshes/A_MokHansooPatrol_%s.A_MokHansooPatrol_%s"), Name, Name),
			nullptr, LOAD_NoWarn);
	};
	IdleAnim = LoadAnim(TEXT("Idle"));
	WalkAnim = LoadAnim(TEXT("Walk"));
	RunAnim = LoadAnim(TEXT("Run"));
	LookAnim = LoadAnim(TEXT("Look"));
	FreezeAnim = LoadAnim(TEXT("Freeze"));
	GrabAnim = LoadAnim(TEXT("Grab"));
	if (!IdleAnim || !WalkAnim)
	{
		// 걷지 못하는 뼈대는 서 있는 조각이다. 정적 조각이 낫다.
		UE_LOG(LogTemp, Warning, TEXT("SK_MokHansooPatrol animations missing; static pieces kept"));
		return false;
	}
	USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(this, TEXT("MokPatrolBody"));
	Component->SetupAttachment(BodyPivot);
	Component->SetMobility(EComponentMobility::Movable);
	Component->RegisterComponent();
	Component->SetSkeletalMesh(Mesh);
	// 리깅한 몸은 정면이 +X, 원점이 발밑 가운데다. 액터와 그대로 맞는다.
	Component->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCanEverAffectNavigation(false);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCastShadow(true);
	// 걸음 동작이 바운드 밖으로 팔과 발을 낸다.
	Component->SetBoundsScale(1.4f);
	Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	BodySkeletal = Component;
	Workwear->SetStaticMesh(nullptr);
	HeadHands->SetStaticMesh(nullptr);
	PlayBodyAnim(IdleAnim, true, 1.0f);
	return true;
}

void AIGManagerPatrol::PlayBodyAnim(UAnimSequence* Sequence, const bool bLoop, const float Rate)
{
	if (!BodySkeletal || !Sequence)
	{
		return;
	}
	if (ActiveAnim != Sequence)
	{
		BodySkeletal->PlayAnimation(Sequence, bLoop);
		ActiveAnim = Sequence;
	}
	BodySkeletal->SetPlayRate(Rate);
}

void AIGManagerPatrol::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGListenerEntity* InListener,
	AIGNightLoopDirector* InNightLoop)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
	NightLoop = InNightLoop;
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			NoiseSubsystem = Noise;
			NoiseHandle = Noise->OnNoiseReported.AddUObject(this, &AIGManagerPatrol::HandleNoise);
		}
	}
	if (InListener)
	{
		KnockHandle = InListener->OnKnocked.AddUObject(this, &AIGManagerPatrol::HandleListenerKnock);
	}
	if (InNightLoop)
	{
		SeenCaptureCount = InNightLoop->GetCaptureCount();
		CaptureSettledHandle = InNightLoop->OnCaptureSettled.AddUObject(
			this, &AIGManagerPatrol::HandleCaptureSettled);
	}
	GetWorldTimerManager().SetTimer(
		DutyTimer, this, &AIGManagerPatrol::RefreshDuty, IGManagerPatrol::DutyCheckSeconds, true);
}

void AIGManagerPatrol::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DutyTimer);
	GetWorldTimerManager().ClearTimer(EscortTimer);
	if (AIGNightLoopDirector* Loop = NightLoop.Get())
	{
		Loop->OnCaptureSettled.Remove(CaptureSettledHandle);
	}
	if (UIGNoiseSubsystem* Noise = NoiseSubsystem.Get())
	{
		Noise->OnNoiseReported.Remove(NoiseHandle);
	}
	if (AIGListenerEntity* Upstairs = Listener.Get())
	{
		Upstairs->OnKnocked.Remove(KnockHandle);
	}
	Super::EndPlay(EndPlayReason);
}

// -- 길 ----------------------------------------------------------------------

int32 AIGManagerPatrol::AddErrandNode(const FVector& Feet)
{
	const int32 Index = AddNode(Feet, false, false);
	Nodes[Index].bErrandOnly = true;
	return Index;
}

bool AIGManagerPatrol::IsNodeUsable(const int32 Index) const
{
	return Nodes.IsValidIndex(Index)
		&& !(Nodes[Index].bHuntOnly && !bKeyHunt)
		&& !(Nodes[Index].bErrandOnly && !bErrand);
}

int32 AIGManagerPatrol::AddNode(const FVector& Feet, const bool bOnStair, const bool bHuntOnly)
{
	FPatrolNode& Node = Nodes.AddDefaulted_GetRef();
	Node.Feet = Feet;
	Node.bOnStair = bOnStair;
	Node.bHuntOnly = bHuntOnly;
	return Nodes.Num() - 1;
}

void AIGManagerPatrol::LinkNodes(const int32 A, const int32 B)
{
	if (Nodes.IsValidIndex(A) && Nodes.IsValidIndex(B) && A != B)
	{
		Nodes[A].Links.AddUnique(B);
		Nodes[B].Links.AddUnique(A);
	}
}

void AIGManagerPatrol::BuildGraph()
{
	Nodes.Reset();
	Route.Reset();

	// 1층. 관리실 문 앞 연결통로에서 나와 주차장을 지나 계단탑으로 간다.
	RestNode = AddNode(FVector(165.0f, -275.0f, 0.0f), false);
	const int32 Connector = AddNode(FVector(-40.0f, -300.0f, 0.0f), false);
	const int32 Bay = AddNode(FVector(-230.0f, -295.0f, 0.0f), false);
	LinkNodes(RestNode, Connector);
	LinkNodes(Connector, Bay);
	// 밤2 손수레 심부름 길. 안쪽 방 간이침대 곁에서 문 안쪽, 문 바깥(관리실), 관리실 문 안쪽을
	// 지나 문 밖으로. 손수레는 필로티 뒷벽에 사슬로 묶여 있고, 돌아와서는 관리실 문 동쪽
	// 연결통로에 세운다. 이 점들은 심부름 때만 쓴다(밤3 순찰은 안쪽 방에 몸이 없다).
	ErrandCotNode = AddErrandNode(FVector(40.0f, 30.0f, 0.0f));
	ErrandInnerInNode = AddErrandNode(FVector(240.0f, 15.0f, 0.0f));
	ErrandInnerOutNode = AddErrandNode(FVector(240.0f, -118.0f, 0.0f));
	ErrandOfficeNode = AddErrandNode(FVector(165.0f, -200.0f, 0.0f));
	ErrandCartNode = AddErrandNode(FVector(-205.0f, -345.0f, 0.0f));
	ErrandParkNode = AddErrandNode(FVector(235.0f, -290.0f, 0.0f));
	LinkNodes(ErrandCotNode, ErrandInnerInNode);
	LinkNodes(ErrandInnerInNode, ErrandInnerOutNode);
	LinkNodes(ErrandInnerOutNode, ErrandOfficeNode);
	LinkNodes(ErrandOfficeNode, RestNode);
	LinkNodes(Bay, ErrandCartNode);
	LinkNodes(RestNode, ErrandParkNode);

	// 계단탑. 출입구는 층 노드로 나눠 쓴다.
	int32 Doorways[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	Doorways[0] = AddNode(AIGPrologueWorldScene::GetStairDoorwayFeet(0), false);
	LinkNodes(Bay, Doorways[0]);
	for (int32 Floor = 0; Floor < 3; ++Floor)
	{
		TArray<FVector> Climb;
		AIGPrologueWorldScene::GetStairClimbFeet(Floor, Climb);
		int32 Previous = Doorways[Floor];
		for (int32 Index = 1; Index + 1 < Climb.Num(); ++Index)
		{
			const int32 Step = AddNode(Climb[Index], true);
			LinkNodes(Previous, Step);
			Previous = Step;
		}
		Doorways[Floor + 1] = AddNode(Climb.Last(), false);
		LinkNodes(Previous, Doorways[Floor + 1]);
	}
	FourthFloorDoorNode = Doorways[3];

	// 2층. 201호 문 앞에서 창고 안으로, 복도 끝까지.
	const float SecondZ = AIGPrologueWorldScene::GetStoreyFloorZ(1);
	const int32 Front201 = AddNode(FVector(-150.0f, -305.0f, SecondZ), false);
	const int32 Door201 = AddNode(FVector(-150.0f, -222.0f, SecondZ), false);
	const int32 Inside201 = AddNode(FVector(-118.0f, -110.0f, SecondZ), false);
	const int32 Middle2 = AddNode(FVector(100.0f, -305.0f, SecondZ), false);
	const int32 East2 = AddNode(FVector(375.0f, -305.0f, SecondZ), false);
	LinkNodes(Doorways[1], Front201);
	LinkNodes(Front201, Door201);
	LinkNodes(Door201, Inside201);
	LinkNodes(Front201, Middle2);
	LinkNodes(Middle2, East2);

	// 3층. 302호 빈집과 복도 끝.
	const float ThirdZ = AIGPrologueWorldScene::GetStoreyFloorZ(2);
	const int32 Front301 = AddNode(FVector(-150.0f, -305.0f, ThirdZ), false);
	const int32 Front302 = AddNode(FVector(-30.0f, -305.0f, ThirdZ), false);
	const int32 Door302 = AddNode(FVector(-30.0f, -222.0f, ThirdZ), false);
	const int32 Inside302 = AddNode(FVector(10.0f, -70.0f, ThirdZ), false);
	const int32 East3 = AddNode(FVector(375.0f, -305.0f, ThirdZ), false);
	LinkNodes(Doorways[2], Front301);
	LinkNodes(Front301, Front302);
	LinkNodes(Front302, Door302);
	LinkNodes(Door302, Inside302);
	LinkNodes(Front302, East3);

	// 한 바퀴. 각 자리 사이는 가장 짧은 길로 걷는다. 4층에서는 복도를 비추기만 한다.
	const auto AddStop = [this](const int32 Node, const float Dwell, const float LookYaw, const bool bFixed)
	{
		FRouteStop& Stop = Route.AddDefaulted_GetRef();
		Stop.Node = Node;
		Stop.DwellSeconds = Dwell;
		Stop.LookYaw = LookYaw;
		Stop.bFixedLook = bFixed;
	};
	AddStop(Inside201, 3.5f, 0.0f, false);
	AddStop(East2, 2.5f, 0.0f, true);
	AddStop(Inside302, 3.5f, 0.0f, false);
	AddStop(East3, 2.5f, 0.0f, true);
	AddStop(FourthFloorDoorNode, IGManagerPatrol::StareSeconds, 0.0f, true);
	AddStop(RestNode, 0.0f, 0.0f, false);

	// 열쇠를 찾으러 갈 때만 밟는 길(밤3, 한 번). 4층 복도 서쪽 끝 옥상 계단 밑에서 403호 문
	// 동쪽까지, 옥상 계단과 옥상 철문, 물탱크 통로, 5층 철문 앞이다. 403호 문은 바깥으로 열려
	// 문짝이 복도 서쪽을 쓸고 지나서 문 동쪽에 선다. 옥상 철문은 계단 쪽으로 열려서 문선
	// 110 cm 아래 단에서 연다. 5층 철문은 안으로 열려 문 앞 한 걸음에 선다. 손전등을 든 손이
	// 몸 앞 40 cm라 그보다 붙어 서면 닫힌 문짝 너머를 비춘다.
	const float FourthZ = AIGPrologueWorldScene::GetStoreyFloorZ(3);
	const int32 RoofStairFoot = AddNode(FVector(-262.0f, -292.0f, FourthZ), false, true);
	Home403Node = AddNode(FVector(240.0f, -295.0f, FourthZ), false, true);
	const int32 LowerFlight = AddNode(FVector(-277.5f, -238.0f, FourthZ), true, true);
	const int32 HalfLanding = AddNode(FVector(-277.5f, -150.0f, FourthZ + 72.0f), true, true);
	const int32 HalfLandingEnd = AddNode(FVector(-277.5f, -125.0f, FourthZ + 72.0f), true, true);
	const int32 UpperFlight = AddNode(FVector(-277.5f, -100.0f, 980.0f), true, true);
	const int32 RoofDoorInside = AddNode(FVector(-277.5f, 110.0f, 1142.3f), true, true);
	const int32 TopTread = AddNode(FVector(-277.5f, 208.5f, 1200.0f), true, true);
	const int32 RoofOutside = AddNode(FVector(-277.5f, 245.0f, 1200.0f), false, true);
	const int32 PassageWest = AddNode(FVector(-200.0f, 225.0f, 1200.0f), false, true);
	const int32 PassageEast = AddNode(FVector(130.0f, 220.0f, 1200.0f), false, true);
	AnnexFrontNode = AddNode(FVector(130.0f, 390.0f, 1200.0f), false, true);
	LinkNodes(Doorways[3], RoofStairFoot);
	LinkNodes(RoofStairFoot, Home403Node);
	LinkNodes(RoofStairFoot, LowerFlight);
	LinkNodes(LowerFlight, HalfLanding);
	LinkNodes(HalfLanding, HalfLandingEnd);
	LinkNodes(HalfLandingEnd, UpperFlight);
	LinkNodes(UpperFlight, RoofDoorInside);
	LinkNodes(RoofDoorInside, TopTread);
	LinkNodes(TopTread, RoofOutside);
	LinkNodes(RoofOutside, PassageWest);
	LinkNodes(PassageWest, PassageEast);
	LinkNodes(PassageEast, AnnexFrontNode);

	HuntStops.Reset();
	const auto AddHuntStop = [this](const int32 Node, const EHuntAction Action)
	{
		FHuntStop& Stop = HuntStops.AddDefaulted_GetRef();
		Stop.Node = Node;
		Stop.Action = Action;
	};
	AddHuntStop(FourthFloorDoorNode, EHuntAction::None);
	AddHuntStop(Home403Node, EHuntAction::HomeDoor);
	AddHuntStop(RoofStairFoot, EHuntAction::None);
	AddHuntStop(RoofDoorInside, EHuntAction::RoofDoorOpen);
	AddHuntStop(RoofOutside, EHuntAction::RoofSweep);
	AddHuntStop(AnnexFrontNode, EHuntAction::AnnexDoor);
	AddHuntStop(RoofDoorInside, EHuntAction::RoofDoorClose);
	AddHuntStop(RestNode, EHuntAction::None);
}

bool AIGManagerPatrol::FindPath(const int32 From, const int32 To, TArray<int32>& OutPath) const
{
	OutPath.Reset();
	if (!Nodes.IsValidIndex(From) || !Nodes.IsValidIndex(To))
	{
		return false;
	}
	TArray<float> Cost;
	TArray<int32> Previous;
	TArray<bool> Done;
	Cost.Init(TNumericLimits<float>::Max(), Nodes.Num());
	Previous.Init(INDEX_NONE, Nodes.Num());
	Done.Init(false, Nodes.Num());
	Cost[From] = 0.0f;
	for (int32 Pass = 0; Pass < Nodes.Num(); ++Pass)
	{
		int32 Best = INDEX_NONE;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (!Done[Index] && Cost[Index] < TNumericLimits<float>::Max()
				&& (Best == INDEX_NONE || Cost[Index] < Cost[Best]))
			{
				Best = Index;
			}
		}
		if (Best == INDEX_NONE || Best == To)
		{
			break;
		}
		Done[Best] = true;
		for (const int32 Next : Nodes[Best].Links)
		{
			if (!IsNodeUsable(Next))
			{
				continue;
			}
			const float Candidate = Cost[Best] + FVector::Dist(Nodes[Best].Feet, Nodes[Next].Feet);
			if (Candidate < Cost[Next])
			{
				Cost[Next] = Candidate;
				Previous[Next] = Best;
			}
		}
	}
	if (Cost[To] == TNumericLimits<float>::Max())
	{
		return false;
	}
	for (int32 Walk = To; Walk != INDEX_NONE; Walk = Previous[Walk])
	{
		OutPath.Insert(Walk, 0);
	}
	return true;
}

int32 AIGManagerPatrol::FindNearestNode(const FVector& Feet) const
{
	// 벽 너머의 점은 가까워도 고르지 않는다. 바로 쫓던 길에서 노드로 돌아올 때 벽을
	// 뚫고 그 점으로 걸어 들어갔다. 보이는 점이 하나도 없을 때만 가장 가까운 점이다.
	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	int32 BestSeen = INDEX_NONE;
	float BestSeenScore = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (!IsNodeUsable(Index))
		{
			continue;
		}
		const FVector Delta = Nodes[Index].Feet - Feet;
		// 높이 차를 세 배로 친다. 바로 위층 노드가 가까워 보여도 고르지 않는다.
		const float Score = FMath::Square(Delta.X) + FMath::Square(Delta.Y) + FMath::Square(Delta.Z * 3.0f);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
		if (Score < BestSeenScore
			&& HasClearLine(Feet + FVector(0.0f, 0.0f, 60.0f), Nodes[Index].Feet + FVector(0.0f, 0.0f, 60.0f)))
		{
			BestSeenScore = Score;
			BestSeen = Index;
		}
	}
	return BestSeen != INDEX_NONE ? BestSeen : Best;
}

void AIGManagerPatrol::SetDestination(const int32 Node)
{
	bHasFinalTarget = false;
	// 걷던 길이 있으면 지금 향하던 점에서 이어 잡는다. 가장 가까운 점이 지나온
	// 단일 때가 있어서, 그것부터 잡으면 계단 위에서 몇 단 되돌아갔다 온다.
	const int32 Start = PathCursor < Path.Num() ? Path[PathCursor] : FindNearestNode(GetFeet());
	if (!FindPath(Start, Node, Path))
	{
		Path.Reset();
		Path.Add(Node);
	}
	PathCursor = 0;
}

// -- 당번 --------------------------------------------------------------------

UIGMissingFloorNarrativeSubsystem* AIGManagerPatrol::GetNarrative() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
}

void AIGManagerPatrol::RefreshDuty()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	// 프로브와 캡처 투어는 정해진 순서로 장면을 밟는다. 돌아다니는 사람이 끼면
	// 같은 프레임이 매번 다르다.
	const bool bScripted =
		FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreyboxProbe"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGNightCapture"));
	const bool bShouldPatrol = bErrand || (!bScripted
		&& Narrative
		&& Narrative->GetNightIndex() == 3
		&& Narrative->IsHourSealed()
		&& !Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer)
		&& Player.IsValid());
	if (bShouldPatrol && !bOnDuty)
	{
		GoOnDuty();
	}
	else if (!bShouldPatrol && bOnDuty)
	{
		GoOffDuty();
	}
	if (!bOnDuty)
	{
		return;
	}
	// 누가 잡았든 그녀가 잡히면 그는 안쪽 방으로 돌아가 있다. 깨는 동안 복도에 서
	// 있으면 깬 사람 눈앞에서 순간이동한 사람이 된다. 그가 잡는 중이면 화면이 다
	// 덮일 때까지 그 자리에 있다(HandleCaptureSettled).
	// 심부름 중에 다른 것이 그녀를 잡으면 그는 하던 일을 한다. 그가 잡았으면 화면이 다 덮인
	// 뒤 HandleCaptureSettled가 심부름을 끝낸다.
	if (bErrand)
	{
		if (const AIGNightLoopDirector* Loop = NightLoop.Get())
		{
			SeenCaptureCount = Loop->GetCaptureCount();
		}
		return;
	}
	if (const AIGNightLoopDirector* Loop = NightLoop.Get())
	{
		const bool bHisCatch = State == EIGManagerPatrolState::Catching && Loop->IsCaptureResetInFlight();
		if (!bHisCatch
			&& (Loop->IsCaptureResetInFlight() || Loop->GetCaptureCount() != SeenCaptureCount))
		{
			SeenCaptureCount = Loop->GetCaptureCount();
			if (State != EIGManagerPatrolState::Resting)
			{
				EnterRestingAtDoor(IGManagerPatrol::CaptureRestSeconds);
			}
			else
			{
				RestUntilSeconds = FMath::Max(
					RestUntilSeconds,
					GetWorld()->GetTimeSeconds() + IGManagerPatrol::CaptureRestSeconds);
			}
		}
	}
}

void AIGManagerPatrol::GoOnDuty()
{
	bOnDuty = true;
	bKeyHunt = false;
	bHuntAction = false;
	bSawInAnnex = false;
	RoundCount = 0;
	Exposure = 0.0f;
	bSawPlayerHide = false;
	SnapToRestNode();
	State = EIGManagerPatrolState::Resting;
	StateStartSeconds = GetWorld()->GetTimeSeconds();
	RestUntilSeconds = StateStartSeconds + IGManagerPatrol::FirstRestSeconds;
	ShowBody(false);
	SetActorTickEnabled(true);
}

void AIGManagerPatrol::GoOffDuty()
{
	bOnDuty = false;
	bKeyHunt = false;
	bHuntAction = false;
	bSawInAnnex = false;
	State = EIGManagerPatrolState::Resting;
	ShowBody(false);
	SetActorTickEnabled(false);
	SnapToRestNode();
}

void AIGManagerPatrol::ShowBody(const bool bShow)
{
	bBodyShown = bShow;
	Workwear->SetVisibility(bShow);
	HeadHands->SetVisibility(bShow);
	if (BodySkeletal)
	{
		BodySkeletal->SetVisibility(bShow);
		// 안쪽 방에 있는 동안은 뼈대도 쉰다.
		BodySkeletal->bPauseAnims = !bShow;
	}
	Torch->SetVisibility(bShow);
}

void AIGManagerPatrol::SnapToRestNode()
{
	// 걷던 길이 남아 있으면 다음 순찰이 그 길에서 향하던 점부터 이어 잡는다.
	// 그 점이 3층 계단참이면 관리실에서 벽을 뚫고 곧장 올라간다.
	Path.Reset();
	PathCursor = 0;
	bHasFinalTarget = false;
	DwellRemaining = 0.0f;
	if (Nodes.IsValidIndex(RestNode))
	{
		SetActorLocation(Nodes[RestNode].Feet);
	}
}

bool AIGManagerPatrol::IsPursuing() const
{
	return bOnDuty
		&& (State == EIGManagerPatrolState::Chasing
			|| State == EIGManagerPatrolState::Catching
			|| State == EIGManagerPatrolState::Searching);
}

bool AIGManagerPatrol::IsAnyPursuing(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AIGManagerPatrol> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->IsPursuing())
		{
			return true;
		}
	}
	return false;
}

bool AIGManagerPatrol::IsAnyOnErrand(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AIGManagerPatrol> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->IsOnErrand())
		{
			return true;
		}
	}
	return false;
}

bool AIGManagerPatrol::IsAnyOnKeyHunt(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AIGManagerPatrol> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->IsOnDuty() && It->IsOnKeyHunt())
		{
			return true;
		}
	}
	return false;
}

bool AIGManagerPatrol::IsCaptureAllowed() const
{
	// 난이도는 위층 사람의 튜닝이 든다. 그가 잡지 않는 밤이면 아무도 잡지 않는다.
	const AIGListenerEntity* Upstairs = Listener.Get();
	return !Upstairs || Upstairs->GetTuning().bCaptureEnabled;
}

void AIGManagerPatrol::ThinkOnce(const TCHAR* BeatId, const FText& Thought)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || Narrative->HasBeatPlayed(FName(BeatId))
		|| AIGMissingFloorLeftoverDirector::IsAnySceneActive(GetWorld()))
	{
		return;
	}
	Narrative->MarkBeatPlayed(FName(BeatId));
	FIGDialogueScope Scope;
	Scope.Owner = this;
	Scope.Context = FName(BeatId);
	Scope.Lifetime = EIGDialogueLifetime::Reaction;
	Scope.Interruption = EIGDialogueInterruption::Drop;
	// 한 번만 하는 말이라 이미 뜬 줄은 자리를 떠나도 끝까지 둔다. 4 m를 벗어나면 읽는 도중에 지워져, 불빛에서
	// 비켜서라는 셋째 밤 경고가 비켜서는 사이 잘리고 다시 나오지 않았다.
	Scope.bKeepOnceShown = true;
	const TWeakObjectPtr<AIGManagerPatrol> WeakThis(this);
	const FVector ObservedAt = GetPlayerFeet();
	Scope.StillValid = [WeakThis, ObservedAt]()
	{
		return WeakThis.IsValid() && WeakThis->IsOnDuty()
			&& FVector::DistSquared(WeakThis->GetPlayerFeet(), ObservedAt) < FMath::Square(400.0f)
			&& !AIGMissingFloorLeftoverDirector::IsAnySceneActive(WeakThis->GetWorld());
	};
	AIGHorrorHUD::PushThoughtScoped(this, Thought, 3.2f, MoveTemp(Scope));
}

// -- 상태 전환 ---------------------------------------------------------------

void AIGManagerPatrol::StartRound()
{
	++RoundCount;
	RouteCursor = 0;
	LeaveRoom();
	SetDestination(Route.IsValidIndex(0) ? Route[0].Node : RestNode);
}

void AIGManagerPatrol::LeaveRoom()
{
	Exposure = 0.0f;
	State = EIGManagerPatrolState::Patrolling;
	StateStartSeconds = GetWorld()->GetTimeSeconds();
	ShowBody(true);
	// 안쪽 방 문이 열리고 열쇠 꾸러미가 먼저 들린다. 몸은 그 소리 뒤에 있다.
	IGAudio::SpawnOneShotAt(
		this, UIGToneSequenceSoundWave::CreateDoorCreak(this, false),
		GetFeet() + FVector(0.0f, 30.0f, 110.0f), 0.55f, 0.92f, 200.0f, 1500.0f, EIGAudioBus::World);
	Caption(
		NSLOCTEXT("IGMissingFloor", "CaptionMokDoor", "[관리실 문이 열리는 소리]"),
		GetFeet(), LastAlarmCaptionSeconds, 0.0f);
	PlayKeys(false);
}

void AIGManagerPatrol::EnterRestingAtDoor(const float RestSeconds)
{
	// 열쇠 찾기는 안쪽 방으로 돌아오면 끝난다. 다음부터는 평소 길이다.
	bKeyHunt = false;
	bHuntAction = false;
	bSawInAnnex = false;
	State = EIGManagerPatrolState::Resting;
	StateStartSeconds = GetWorld()->GetTimeSeconds();
	RestUntilSeconds = StateStartSeconds + RestSeconds;
	Exposure = 0.0f;
	bSawPlayerHide = false;
	if (bBodyShown)
	{
		ShowBody(false);
	}
	SnapToRestNode();
}

void AIGManagerPatrol::BeginInvestigation(const FVector& Where, const bool bAlarmed)
{
	const float StorageFloorZ = AIGPrologueWorldScene::GetStoreyFloorZ(2);
	if (bAvoidStorageFloor && Where.Z > StorageFloorZ - 30.0f && Where.Z < StorageFloorZ + 270.0f)
	{
		return;
	}
	if (State == EIGManagerPatrolState::Chasing
		|| State == EIGManagerPatrolState::Catching
		|| State == EIGManagerPatrolState::Frozen
		|| State == EIGManagerPatrolState::Retreating
		|| State == EIGManagerPatrolState::Resting)
	{
		return;
	}
	if (bErrand && bErrandCartAttached)
	{
		AttachErrandCart(false, false);
	}
	UE_LOG(LogTemp, Display, TEXT("MANAGER_INVESTIGATE at=%s where=%s alarmed=%d"),
		*GetFeet().ToCompactString(), *Where.ToCompactString(), bAlarmed ? 1 : 0);
	State = EIGManagerPatrolState::Investigating;
	StateStartSeconds = GetWorld()->GetTimeSeconds();
	const FVector Target = IsForbiddenNow(Where)
		? Nodes[FourthFloorDoorNode].Feet
		: (IsInsideAnnex(Where) && Nodes.IsValidIndex(AnnexFrontNode) && bKeyHunt)
			? Nodes[AnnexFrontNode].Feet
			: Where;
	SetDestination(FindNearestNode(Target));
	// 노드에서 소리 난 자리까지는 같은 층이고 막힌 데가 없을 때만 곧장 걷는다.
	if (!Path.IsEmpty() && FMath::Abs(Nodes[Path.Last()].Feet.Z - Target.Z) < 40.0f
		&& HasClearLine(Nodes[Path.Last()].Feet + FVector(0.0f, 0.0f, 90.0f), Target + FVector(0.0f, 0.0f, 90.0f)))
	{
		FinalTarget = FVector(Target.X, Target.Y, Nodes[Path.Last()].Feet.Z);
		bHasFinalTarget = true;
	}
	DwellRemaining = IGManagerPatrol::InvestigateDwellSeconds;
	DwellElapsed = 0.0f;
	if (bAlarmed)
	{
		// 발소리가 멎었다가 그쪽으로 돈다. 바로 쫓지 않는다 — 몸을 숨길 틈이다.
		Caption(
			NSLOCTEXT("IGMissingFloor", "CaptionMokStopped", "[열쇠 소리가 뚝 멎는다]"),
			GetFeet(), LastAlarmCaptionSeconds, 6.0f);
	}
}

void AIGManagerPatrol::BeginChase()
{
	// 위층 사람이 쫓는 동안 둘이 같이 쫓지 않는다(§7). 그쪽이 끝나면 다시 본다.
	if (const AIGListenerEntity* Upstairs = Listener.Get())
	{
		if (Upstairs->GetListenerState() == EIGListenerState::Chasing)
		{
			BeginInvestigation(GetPlayerFeet(), true);
			return;
		}
	}
	if (bErrand && bErrandCartAttached)
	{
		AttachErrandCart(false, false);
	}
	const bool bWasChasing = State == EIGManagerPatrolState::Chasing;
	if (!bWasChasing)
	{
		UE_LOG(LogTemp, Display, TEXT("MANAGER_CHASE at=%s player=%s exposure=%.2f errand=%d"),
			*GetFeet().ToCompactString(), *GetPlayerFeet().ToCompactString(), Exposure, bErrand ? 1 : 0);
	}
	State = EIGManagerPatrolState::Chasing;
	StateStartSeconds = GetWorld()->GetTimeSeconds();
	LastSeenSeconds = StateStartSeconds;
	LastKnownPlayerFeet = GetPlayerFeet();
	LastRepathSeconds = -100.0;
	if (!bWasChasing)
	{
		Caption(
			NSLOCTEXT("IGMissingFloor", "CaptionMokQuicken", "[발소리가 빨라진다]"),
			GetFeet(), LastAlarmCaptionSeconds, 4.0f);
		PlayKeys(false);
		if (AIGPlayerCharacter* Character = Player.Get())
		{
			if (UIGStressComponent* Stress = Character->GetStress())
			{
				Stress->ApplyScare(0.45f);
			}
		}
	}
}

void AIGManagerPatrol::BeginSearch()
{
	State = EIGManagerPatrolState::Searching;
	StateStartSeconds = GetWorld()->GetTimeSeconds();
	bSawPlayerHide = false;
	const FVector Target = IsForbiddenNow(LastKnownPlayerFeet)
		? Nodes[GetFourthFloorStandNode()].Feet
		: (IsInsideAnnex(LastKnownPlayerFeet) && Nodes.IsValidIndex(AnnexFrontNode) && bKeyHunt)
			? Nodes[AnnexFrontNode].Feet
			: LastKnownPlayerFeet;
	QueueSearchStops(Target);
	SetDestination(FindNearestNode(Target));
	DwellRemaining = IGManagerPatrol::SearchDwellSeconds;
	DwellElapsed = 0.0f;
}

void AIGManagerPatrol::QueueSearchStops(const FVector& Around)
{
	// 놓친 자리에서 끝내지 않는다. 그 층의 방과 복도 끝을 손전등으로 한 번씩 더 훑는다.
	// 손전등을 든 사람이 사람을 찾는 순서다 — 가까운 문간부터.
	SearchStops.Reset();
	SearchStopCursor = 0;
	const int32 Start = FindNearestNode(Around);
	if (!Nodes.IsValidIndex(Start))
	{
		return;
	}
	TArray<TPair<int32, float>> Candidates;
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Index == Start || Index == RestNode || Nodes[Index].bOnStair
			|| !IsNodeUsable(Index)
			|| FMath::Abs(Nodes[Index].Feet.Z - Nodes[Start].Feet.Z) > 40.0f
			|| IsForbiddenNow(Nodes[Index].Feet)
			|| IsInsideAnnex(Nodes[Index].Feet))
		{
			continue;
		}
		const float Distance = FVector::Dist(Nodes[Index].Feet, Nodes[Start].Feet);
		if (Distance > 150.0f && Distance < IGManagerPatrol::SearchStopRadius)
		{
			Candidates.Emplace(Index, Distance);
		}
	}
	Candidates.Sort([](const TPair<int32, float>& A, const TPair<int32, float>& B) { return A.Value < B.Value; });
	for (const TPair<int32, float>& Candidate : Candidates)
	{
		SearchStops.Add(Candidate.Key);
		if (SearchStops.Num() >= IGManagerPatrol::SearchExtraStops)
		{
			break;
		}
	}
}

void AIGManagerPatrol::BeginRetreat(const float RestAfterSeconds)
{
	State = EIGManagerPatrolState::Retreating;
	StateStartSeconds = GetWorld()->GetTimeSeconds();
	RestAfterRetreat = RestAfterSeconds;
	Exposure = 0.0f;
	// 심부름 중이면 안쪽 방으로 돌아가 눕는다. 손수레를 밀고 있었으면 관리실 앞에 세운다.
	if (bErrand)
	{
		if (bErrandCartAttached)
		{
			AttachErrandCart(false);
		}
		SetErrandStep(EErrandStep::Returning);
		SetDestination(ErrandCotNode);
		return;
	}
	SetDestination(RestNode);
}

void AIGManagerPatrol::BeginCatch()
{
	UE_LOG(LogTemp, Display, TEXT("MANAGER_CATCH at=%s player=%s"), *GetFeet().ToCompactString(), *GetPlayerFeet().ToCompactString());
	State = EIGManagerPatrolState::Catching;
	StateStartSeconds = GetWorld()->GetTimeSeconds();
	CatchStartSeconds = StateStartSeconds;
	bCatchResolved = false;
	AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		return;
	}
	PlayBodyAnim(GrabAnim ? GrabAnim.Get() : IdleAnim.Get(), false, 1.0f);
	if (!IsCaptureAllowed())
	{
		// 추격 없음 난이도. 손을 대지 않는다. 손전등을 얼굴(숨었으면 가구)에 비추고
		// 잠시 서 있다가 돌아간다.
		if (UIGStressComponent* Stress = Character->GetStress())
		{
			Stress->ApplyScare(0.4f);
		}
		return;
	}
	// 숨은 자리를 열어 끌어낸다. 숨는 걸 봤을 때만 여기까지 온다. 손을 휘두르지도
	// 말을 하지도 않는다. 손전등을 얼굴에 들이대고 손목을 쥔다. 그녀의 눈이 그 빛에
	// 하얗게 멀어 가는 것이 이 포획의 전부다(§8 3-1b).
	Character->LeaveHidingSpotImmediately();
	IGAudio::SpawnOneShotAt(
		this, UIGToneSequenceSoundWave::CreatePickupRustle(this),
		Character->GetActorLocation(), 1.0f, 0.72f, 120.0f, 900.0f, EIGAudioBus::World);
	Caption(
		NSLOCTEXT("IGMissingFloor", "CaptionMokGrab", "[손목을 낚아채는 소리]"),
		Character->GetActorLocation(), LastAlarmCaptionSeconds, 0.0f);
	if (UIGStressComponent* Stress = Character->GetStress())
	{
		Stress->ApplyScare(0.85f);
	}
	PlayKeys(false);
	if (AIGNightLoopDirector* Loop = NightLoop.Get())
	{
		Character->SetCaptureLookTarget(this, GetFeet() + FVector(0.0f, 0.0f, 150.0f) - GetActorLocation());
		const FTransform Wake = AIGNightLoopDirector::GetHomeEntryWake();
		Loop->RequestExternalCapture(Character, EIGCaptureKind::Manager, &Wake);
	}
}

void AIGManagerPatrol::BeginCatchForProbe()
{
	AIGPlayerCharacter* Character = Player.Get();
	if (!bOnDuty || !Character)
	{
		return;
	}
	const FVector Forward = Character->GetControlRotation().Vector().GetSafeNormal2D();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const FVector Feet = Character->GetActorLocation()
		- FVector(0.0f, 0.0f, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 96.0f)
		+ Forward * 80.0f;
	SetActorLocationAndRotation(Feet, (-Forward).Rotation());
	if (!bBodyShown)
	{
		ShowBody(true);
	}
	BeginCatch();
}

void AIGManagerPatrol::FinishCatch()
{
	bCatchResolved = true;
	AIGPlayerCharacter* Character = Player.Get();
	// 포획이 걸렸으면 화면이 다 덮일 때 안쪽 방으로 돌아간다(HandleCaptureSettled).
	// 걸리지 못했으면(밤4 가면처럼 다른 대본이 잡고 있으면) 그냥 돌아간다.
	if (Character && IsCaptureAllowed() && NightLoop.IsValid() && NightLoop->IsCaptureResetInFlight())
	{
		return;
	}
	// 추격 없음. 비추기만 하고 손을 놓은 채 돌아간다.
	BeginRetreat(IGManagerPatrol::CaptureRestSeconds);
}

void AIGManagerPatrol::HandleCaptureSettled(const EIGCaptureKind Kind, const FVector& CaptureSpot)
{
	if (!bOnDuty)
	{
		return;
	}
	const bool bHisCatch = Kind == EIGCaptureKind::Manager;
	if (const AIGNightLoopDirector* Loop = NightLoop.Get())
	{
		SeenCaptureCount = Loop->GetCaptureCount();
	}
	if (bErrand)
	{
		// 심부름 중 남이 잡은 포획에는 하던 일을 한다. 그가 잡았으면 그녀를 집에 데려다 놓고
		// 안쪽 방에 눕는다. 계단 열쇠는 밤2에 아직 그녀 손에 없다.
		if (!bHisCatch)
		{
			return;
		}
		FinishErrand(true);
		PlayEscortAftermath(false);
		return;
	}
	EnterRestingAtDoor(IGManagerPatrol::CaptureRestSeconds);
	if (!bHisCatch)
	{
		return;
	}
	// 옥상과 창고 열쇠를 거둬 간다. 열쇠 꾸러미는 관리실 상판 제자리로 돌아간다.
	bool bKeyTaken = false;
	const FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("State.MissingFloor.HasStairKey")), false);
	if (UIGStoryStateSubsystem* Story = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGStoryStateSubsystem>()
		: nullptr; Story && KeyTag.IsValid() && Story->HasState(KeyTag))
	{
		Story->RemoveState(KeyTag);
		bKeyTaken = true;
		for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
		{
			It->RefreshKeyringAvailability();
			break;
		}
	}
	PlayEscortAftermath(bKeyTaken);
}

void AIGManagerPatrol::PlayEscortAftermath(const bool bKeyTaken)
{
	// 문밖, 노크하는 손 높이. 깨어난 그녀는 문을 보고 서 있다.
	const FVector Outside(
		AIGPrologueWorldScene::HomeDoorX + AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f,
		AIGPrologueWorldScene::HomeDoorY - 40.0f,
		AIGPrologueWorldScene::FourthFloorZ + 110.0f);
	// 데려다 놓고 가는 소리와 그 뒤의 생각은 깨어난 밤의 것이다. 그 사이 날이 밝았으면 접는다.
	TFunction<bool()> SameScene = AIGHorrorHUD::MakeSceneGuard(this);
	GetWorldTimerManager().SetTimer(
		EscortTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, Outside, bKeyTaken, SameScene]()
		{
			if (!SameScene())
			{
				return;
			}
			IGAudio::SpawnOneShotAt(
				this, UIGToneSequenceSoundWave::CreateKeyRingJingle(this, true),
				Outside, 0.5f, 1.0f, 120.0f, 1200.0f, EIGAudioBus::World);
			AIGHorrorHUD::PushAudioCaptionAt(
				this, NSLOCTEXT("IGMissingFloor", "CaptionMokKeys", "[열쇠 꾸러미가 짤랑거리는 소리]"),
				2.0f, Outside);
			GetWorldTimerManager().SetTimer(
				EscortTimer,
				FTimerDelegate::CreateWeakLambda(this, [this, Outside, bKeyTaken, SameScene]()
				{
					if (!SameScene())
					{
						return;
					}
					IGAudio::SpawnOneShotAt(
						this, UIGToneSequenceSoundWave::CreateSlipperScuff(this),
						Outside - FVector(80.0f, 60.0f, 104.0f), 0.45f, 0.95f, 160.0f, 1400.0f,
						EIGAudioBus::World);
					AIGHorrorHUD::PushAudioCaptionAt(
						this, NSLOCTEXT("IGMissingFloor", "CaptionMokSlippers", "[슬리퍼를 끄는 발소리]"),
						2.2f, Outside - FVector(80.0f, 60.0f, 0.0f));
					AIGHorrorHUD::PushThought(
						this,
						bKeyTaken
							? NSLOCTEXT("IGMissingFloor", "YudamMokTookKeys", "열쇠 꾸러미를 가져갔네.")
							: NSLOCTEXT("IGMissingFloor", "YudamMokBroughtHome", "집까지 데려다 놓고 갔네."),
						3.2f);
				}),
				1.5f,
				false);
		}),
		0.9f,
		false);
}

// -- 매 프레임 ---------------------------------------------------------------

void AIGManagerPatrol::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bOnDuty || !Player.IsValid())
	{
		return;
	}
	switch (State)
	{
	case EIGManagerPatrolState::Resting:
		TickResting(DeltaSeconds);
		return;
	case EIGManagerPatrolState::Catching:
	{
		const double Elapsed = GetWorld()->GetTimeSeconds() - CatchStartSeconds;
		if (AIGPlayerCharacter* Character = Player.Get())
		{
			// 손전등을 얼굴에 들이댄다.
			FaceYaw((Character->GetActorLocation() - GetFeet()).Rotation().Yaw, DeltaSeconds, 540.0f);
			AimTorch(Character->GetPawnViewLocation(), DeltaSeconds * 4.0f);
		}
		const float Hold = IsCaptureAllowed()
			? IGManagerPatrol::CatchHoldSeconds
			: IGManagerPatrol::HarmlessHoldSeconds;
		if (!bCatchResolved && Elapsed >= Hold)
		{
			FinishCatch();
		}
		return;
	}
	case EIGManagerPatrolState::Frozen:
	{
		if (GetWorld()->GetTimeSeconds() - StateStartSeconds >= IGManagerPatrol::FreezeSeconds)
		{
			Caption(
				NSLOCTEXT("IGMissingFloor", "CaptionMokHurries", "[슬리퍼 소리가 급히 멀어진다]"),
				GetFeet(), LastAlarmCaptionSeconds, 0.0f);
			BeginRetreat(IGManagerPatrol::KnockRestSeconds);
		}
		return;
	}
	case EIGManagerPatrolState::Chasing:
		UpdateSenses(DeltaSeconds);
		if (State == EIGManagerPatrolState::Chasing)
		{
			TickChase(DeltaSeconds);
		}
		return;
	case EIGManagerPatrolState::Retreating:
		TickWalking(DeltaSeconds, IGManagerPatrol::RetreatSpeed);
		return;
	default:
		break;
	}

	UpdateSenses(DeltaSeconds);
	if (State == EIGManagerPatrolState::Chasing || State == EIGManagerPatrolState::Catching)
	{
		return;
	}
	if (bErrand && State == EIGManagerPatrolState::Patrolling)
	{
		TickErrand(DeltaSeconds);
		return;
	}
	if (bKeyHunt && bHuntAction && State == EIGManagerPatrolState::Patrolling)
	{
		TickHuntAction(DeltaSeconds);
		return;
	}
	if (DwellRemaining > 0.0f && PathCursor >= Path.Num() && !bHasFinalTarget)
	{
		TickDwell(DeltaSeconds);
		return;
	}
	const float Speed = State == EIGManagerPatrolState::Patrolling
		? IGManagerPatrol::WalkSpeed
		: IGManagerPatrol::InvestigateSpeed;
	TickWalking(DeltaSeconds, Speed);
}

void AIGManagerPatrol::TickResting(const float DeltaSeconds)
{
	if (GetWorld()->GetTimeSeconds() < RestUntilSeconds)
	{
		return;
	}
	// 그녀가 관리실 문을 보고 있거나 곁에 있으면 기다린다. 문간에서 사람이
	// 생겨나는 것을 보이지 않는다.
	if (IsPlayerWatching(Nodes[RestNode].Feet)
		|| FVector::Dist(GetPlayerFeet(), Nodes[RestNode].Feet) < 500.0f)
	{
		RestUntilSeconds = GetWorld()->GetTimeSeconds() + 2.0f;
		return;
	}
	// 나서는 길에 책상을 본다. 열쇠 꾸러미가 없으면 평소 길을 버리고 찾으러 간다.
	if (ShouldStartKeyHunt())
	{
		StartKeyHunt();
		return;
	}
	StartRound();
}

void AIGManagerPatrol::TickWalking(const float DeltaSeconds, const float Speed)
{
	if (PathCursor < Path.Num())
	{
		const FVector Target = Nodes[Path[PathCursor]].Feet;
		if (StepToward(Target, Speed, DeltaSeconds))
		{
			++PathCursor;
		}
		AnimateGait(DeltaSeconds, Speed);
		RelaxTorch(DeltaSeconds);
		return;
	}
	if (bHasFinalTarget)
	{
		if (StepToward(FinalTarget, Speed, DeltaSeconds))
		{
			bHasFinalTarget = false;
		}
		AnimateGait(DeltaSeconds, Speed);
		RelaxTorch(DeltaSeconds);
		return;
	}

	// 길 끝에 닿았다.
	AnimateGait(DeltaSeconds, 0.0f);
	switch (State)
	{
	case EIGManagerPatrolState::Retreating:
		if (IsPlayerWatching(GetFeet()))
		{
			// 보는 눈 앞에서 문으로 사라지지 않는다. 등을 돌린 채 서 있는다.
			RelaxTorch(DeltaSeconds);
			return;
		}
		if (bErrand)
		{
			FinishErrand(false);
			return;
		}
		IGAudio::SpawnOneShotAt(
			this, UIGToneSequenceSoundWave::CreateDoorCreak(this, true),
			GetFeet() + FVector(0.0f, 30.0f, 110.0f), 0.5f, 0.92f, 200.0f, 1500.0f, EIGAudioBus::World);
		EnterRestingAtDoor(RestAfterRetreat);
		return;
	case EIGManagerPatrolState::Patrolling:
	{
		if (bErrand)
		{
			ArriveErrandStop();
			return;
		}
		if (bKeyHunt)
		{
			// 열쇠 찾기 마지막 지점은 관리실 문이다. 유담이 보고 있으면 문으로 들어가지 않는다.
			if (HuntStops.IsValidIndex(HuntCursor) && HuntStops[HuntCursor].Node == RestNode)
			{
				if (IsPlayerWatching(GetFeet()))
				{
					return;
				}
				IGAudio::SpawnOneShotAt(
					this, UIGToneSequenceSoundWave::CreateDoorCreak(this, true),
					GetFeet() + FVector(0.0f, 30.0f, 110.0f), 0.5f, 0.92f, 200.0f, 1500.0f, EIGAudioBus::World);
				UE_LOG(LogTemp, Display, TEXT("MANAGER_KEYHUNT done"));
				EnterRestingAtDoor(IGManagerPatrol::RoundRestSeconds);
				return;
			}
			if (!bHuntAction)
			{
				BeginHuntAction();
			}
			return;
		}
		const FRouteStop& Stop = Route[FMath::Clamp(RouteCursor, 0, Route.Num() - 1)];
		if (Stop.Node == RestNode)
		{
			if (IsPlayerWatching(GetFeet()))
			{
				return;
			}
			IGAudio::SpawnOneShotAt(
				this, UIGToneSequenceSoundWave::CreateDoorCreak(this, true),
				GetFeet() + FVector(0.0f, 30.0f, 110.0f), 0.5f, 0.92f, 200.0f, 1500.0f, EIGAudioBus::World);
			const float Jitter = static_cast<float>((RoundCount * 7919) % 100) / 100.0f;
			EnterRestingAtDoor(IGManagerPatrol::RoundRestSeconds
				+ IGManagerPatrol::RoundRestJitterSeconds * Jitter);
			return;
		}
		DwellRemaining = Stop.DwellSeconds;
		DwellElapsed = 0.0f;
		DwellBaseYaw = Stop.bFixedLook ? Stop.LookYaw : GetActorRotation().Yaw;
		if (DwellRemaining <= 0.0f)
		{
			AdvanceRoute();
		}
		return;
	}
	case EIGManagerPatrolState::Investigating:
	case EIGManagerPatrolState::Searching:
		if (DwellRemaining > 0.0f)
		{
			DwellBaseYaw = GetActorRotation().Yaw;
		}
		return;
	default:
		return;
	}
}

void AIGManagerPatrol::ResolveFireDoors() const
{
	if (bFireDoorResolved)
	{
		return;
	}
	bFireDoorResolved = true;
	for (TActorIterator<AIGFireDoorWedge> It(GetWorld()); It; ++It)
	{
		FireDoors.Add(It->GetDoor());
	}
}

bool AIGManagerPatrol::IsFireDoorClosed() const
{
	ResolveFireDoors();
	for (const TWeakObjectPtr<AIGSwingDoor>& Candidate : FireDoors)
	{
		const AIGSwingDoor* Door = Candidate.Get();
		if (Door && Door->GetActorLocation().Z > IGManagerPatrol::FourthFloorFireDoorZ)
		{
			return !Door->IsOpen();
		}
	}
	return false;
}

AIGSwingDoor* AIGManagerPatrol::FindFireDoorAhead(const FVector& From, const FVector& To) const
{
	ResolveFireDoors();
	for (const TWeakObjectPtr<AIGSwingDoor>& Candidate : FireDoors)
	{
		AIGSwingDoor* Door = Candidate.Get();
		// 4층 문은 평소 열지 않는다(닫힌 문이 핑계가 된다). 열쇠를 찾으러 갈 때는 연다.
		if (!Door || (Door->GetActorLocation().Z > IGManagerPatrol::FourthFloorFireDoorZ && !bKeyHunt))
		{
			continue;
		}
		// 위층 사람·303호와 같은 판정이다. 다 열린 문은 남쪽 벽에 붙어 길을 막지 않는다.
		if (IGWalker::IsStairDoorAhead(*Door, From, To))
		{
			return Door;
		}
	}
	return nullptr;
}

int32 AIGManagerPatrol::GetFourthFloorStandNode() const
{
	// 방화문이 닫혀 있으면 그는 그 문을 열지 않는다. 문 안쪽 참에 서서 문을 비춘다.
	// 4층 복도는 그가 피하는 곳이다. 닫힌 문은 그에게 핑계가 된다.
	if (IsFireDoorClosed() && Nodes.IsValidIndex(FourthFloorDoorNode))
	{
		for (const int32 Linked : Nodes[FourthFloorDoorNode].Links)
		{
			if (Nodes[Linked].Feet.X < Nodes[FourthFloorDoorNode].Feet.X - 30.0f)
			{
				return Linked;
			}
		}
	}
	return FourthFloorDoorNode;
}

void AIGManagerPatrol::AdvanceRoute()
{
	RouteCursor = (RouteCursor + 1) % Route.Num();
	int32 Guard = Route.Num();
	while (bAvoidStorageFloor && Guard-- > 0 && IsStorageFloorNode(Route[RouteCursor].Node))
	{
		RouteCursor = (RouteCursor + 1) % Route.Num();
	}
	// 4층 목은 위층 사람이 그 근처에 있으면 건너뛴다. 그는 그것이 있는 데로 가지 않는다.
	if (Route[RouteCursor].Node == FourthFloorDoorNode)
	{
		if (const AIGListenerEntity* Upstairs = Listener.Get())
		{
			if (!Upstairs->IsDormant() && !Upstairs->IsHidden()
				&& FVector::Dist(Upstairs->GetActorLocation(), Nodes[FourthFloorDoorNode].Feet) < 900.0f)
			{
				RouteCursor = (RouteCursor + 1) % Route.Num();
			}
		}
	}
	SetDestination(Route[RouteCursor].Node == FourthFloorDoorNode
		? GetFourthFloorStandNode()
		: Route[RouteCursor].Node);
}

void AIGManagerPatrol::TickDwell(const float DeltaSeconds)
{
	DwellElapsed += DeltaSeconds;
	DwellRemaining -= DeltaSeconds;
	AnimateGait(DeltaSeconds, 0.0f);
	// 서서 둘러본다. 몸은 천천히, 손전등은 좌우로 크게 훑는다.
	const float Sweep = FMath::Sin(DwellElapsed * 1.6f) * 50.0f;
	FaceYaw(DwellBaseYaw + Sweep * 0.5f, DeltaSeconds, 90.0f);
	TorchRelative = FMath::RInterpTo(
		TorchRelative, FRotator(-4.0f, Sweep * 0.5f, 0.0f), DeltaSeconds, 3.0f);
	Torch->SetRelativeRotation(TorchRelative);

	if (State == EIGManagerPatrolState::Patrolling && Nodes.IsValidIndex(FourthFloorDoorNode)
		&& Route.IsValidIndex(RouteCursor) && Route[RouteCursor].Node == FourthFloorDoorNode)
	{
		// 4층 복도 앞에서는 발을 들이지 않는다. 그녀가 그 복도에 있으면 그것을 안다.
		if (IsForbiddenFourthFloor(GetPlayerFeet()))
		{
			ThinkOnce(TEXT("Night3.MokAvoidsFourth"),
				NSLOCTEXT("IGMissingFloor", "YudamMokAvoidsFourth", "4층 복도로는 안 들어와."));
		}
	}
	if (DwellRemaining > 0.0f)
	{
		return;
	}
	DwellRemaining = 0.0f;
	switch (State)
	{
	case EIGManagerPatrolState::Patrolling:
		AdvanceRoute();
		return;
	case EIGManagerPatrolState::Investigating:
	case EIGManagerPatrolState::Searching:
	{
		// 수색이면 둘레의 다음 자리로. 다 훑었으면 그제야 돌던 길로 돌아간다.
		if (State == EIGManagerPatrolState::Searching && SearchStops.IsValidIndex(SearchStopCursor))
		{
			SetDestination(SearchStops[SearchStopCursor++]);
			DwellRemaining = IGManagerPatrol::SearchStopDwellSeconds;
			DwellElapsed = 0.0f;
			return;
		}
		// 아무것도 없었다. 원래 돌던 길의 다음 자리로 간다. 열쇠를 찾던 중이면 그 자리로.
		State = EIGManagerPatrolState::Patrolling;
		StateStartSeconds = GetWorld()->GetTimeSeconds();
		Exposure = 0.0f;
		bHuntAction = false;
		if (bErrand)
		{
			ResumeErrandPath();
			return;
		}
		SetDestination(bKeyHunt && HuntStops.IsValidIndex(HuntCursor)
			? HuntStops[HuntCursor].Node
			: Route[FMath::Clamp(RouteCursor, 0, Route.Num() - 1)].Node);
		return;
	}
	case EIGManagerPatrolState::Staring:
		BeginRetreat(IGManagerPatrol::RoundRestSeconds);
		return;
	default:
		return;
	}
}

void AIGManagerPatrol::TickChase(const float DeltaSeconds)
{
	AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const FVector PlayerFeet = GetPlayerFeet();

	// 숨었다. 숨는 걸 봤으면 그 자리로 가서 연다. 못 봤으면 놓친 것이다.
	if (Character->IsConcealedInHidingSpot())
	{
		if (!bWasPlayerConcealed)
		{
			bWasPlayerConcealed = true;
			bSawPlayerHide = Now - LastSeenSeconds <= 1.0
				&& FVector::Dist(GetFeet(), PlayerFeet) < 600.0f;
			// 숨은 가구 앞. 그녀가 E를 누른 자리(2 m까지 떨어질 수 있다)도, 가구 속 몸
			// 자리도 아니다. 거기라야 문을 열고 손을 넣는다.
			HideSpotFeet = GetHideApproachFeet();
			HideApproachStartSeconds = Now;
		}
		// 가구 앞에 끝내 닿지 못했다. 놓아 준다.
		if (bSawPlayerHide && Now - HideApproachStartSeconds > IGManagerPatrol::HideApproachTimeoutSeconds)
		{
			bSawPlayerHide = false;
			LastKnownPlayerFeet = HideSpotFeet;
			BeginSearch();
			return;
		}
		// 숨는 걸 못 봤으면 그녀가 어디 들어갔는지 모른다. 마지막으로 본 자리부터 찾는다.
		if (!bSawPlayerHide)
		{
			BeginSearch();
			return;
		}
	}
	else
	{
		bWasPlayerConcealed = false;
		bSawPlayerHide = false;
	}

	if (Now - LastSeenSeconds > IGManagerPatrol::LostSightSeconds && !bSawPlayerHide)
	{
		BeginSearch();
		return;
	}

	// 손이 닿는다. 숨은 그녀는 가구 앞에 서면 닿는다.
	const FVector Feet = GetFeet();
	const float Planar = FVector::Dist2D(Feet, PlayerFeet);
	// 쫓아가는 자리는 마지막으로 보거나 들은 자리다. 보이는 동안에는 매 프레임 그녀 발밑으로
	// 갱신된다. 예전에는 놓친 뒤 3.5초 동안 벽 너머의 그녀를 그대로 따라갔다.
	const FVector ChaseGoal = bSawPlayerHide ? HideSpotFeet : LastKnownPlayerFeet;
	// 손전등도 같다. 보이는 동안에는 그녀를, 놓친 뒤에는 마지막으로 본 자리를 비춘다. 다 와서
	// 발밑을 비추게 되면 걸을 때처럼 앞을 비춘다.
	auto AimChaseTorch = [&](const float Step)
	{
		if (Now - LastSeenSeconds <= 0.25)
		{
			AimTorch(Character->GetActorLocation(), Step);
		}
		else if (FVector::Dist2D(Feet, ChaseGoal) > 150.0f)
		{
			AimTorch(ChaseGoal + FVector(0.0f, 0.0f, 110.0f), Step);
		}
		else
		{
			RelaxTorch(Step);
		}
	};
	// 덧댄 것이 움직이거나 누르는 동안에는 손을 뻗지 않는다. 그때 잡으면 포획이 둘 겹친다.
	const bool bLiningBusy = AIGLiningEntity::IsAnyBusy(GetWorld());
	if (bSawPlayerHide && Character->IsConcealedInHidingSpot())
	{
		if (FVector::Dist2D(Feet, HideSpotFeet) <= IGManagerPatrol::HideReach
			&& FMath::Abs(HideSpotFeet.Z - Feet.Z) <= IGManagerPatrol::CatchHeight
			&& !bLiningBusy)
		{
			BeginCatch();
			return;
		}
	}
	else if (Planar <= IGManagerPatrol::CatchReach
		&& FMath::Abs(PlayerFeet.Z - Feet.Z) <= IGManagerPatrol::CatchHeight
		&& HasClearLine(Feet + FVector(0.0f, 0.0f, 120.0f), Character->GetActorLocation())
		&& !bLiningBusy)
	{
		BeginCatch();
		return;
	}

	// 5층 안으로는 따라 들어가지 않는다. 문 앞까지 와서 비추다가 문을 쾅 닫고 내려간다.
	// 열쇠를 찾으러 온 길이 아니면 그 문 앞으로 가는 길도 없다. 그냥 돌아간다.
	if (IsInsideAnnex(ChaseGoal))
	{
		if (!bKeyHunt || !Nodes.IsValidIndex(AnnexFrontNode))
		{
			BeginRetreat(IGManagerPatrol::KnockRestSeconds);
			return;
		}
		const FVector Front = Nodes[AnnexFrontNode].Feet;
		if (FVector::Dist2D(Feet, Front) < 20.0f && FMath::Abs(Feet.Z - Front.Z) < 40.0f)
		{
			SlamAnnexAndFlee();
			return;
		}
		if (Path.IsEmpty() || Path.Last() != AnnexFrontNode)
		{
			SetDestination(AnnexFrontNode);
		}
		TickWalking(DeltaSeconds, IGManagerPatrol::ChaseSpeed);
		AimChaseTorch(DeltaSeconds);
		return;
	}

	// 4층 복도로는 따라가지 않는다. 목에서 서서 비춘다.
	if (IsForbiddenNow(ChaseGoal))
	{
		const int32 StandNode = GetFourthFloorStandNode();
		if (FVector::Dist(Feet, Nodes[StandNode].Feet) < 10.0f)
		{
			State = EIGManagerPatrolState::Staring;
			StateStartSeconds = Now;
			Path.Reset();
			PathCursor = 0;
			bHasFinalTarget = false;
			DwellRemaining = IGManagerPatrol::StareSeconds;
			DwellElapsed = 0.0f;
			DwellBaseYaw = 0.0f;
			ThinkOnce(TEXT("Night3.MokAvoidsFourth"),
				NSLOCTEXT("IGMissingFloor", "YudamMokAvoidsFourth", "4층 복도로는 안 들어와."));
			return;
		}
		if (Path.IsEmpty() || Path.Last() != StandNode)
		{
			SetDestination(StandNode);
		}
		TickWalking(DeltaSeconds, IGManagerPatrol::ChaseSpeed);
		AimChaseTorch(DeltaSeconds);
		return;
	}

	// 같은 바닥에서 막힌 데 없이 보이면 곧장 간다. 아니면 그녀와 가까운 노드로 길을 잡는다.
	// 계단 위에서는 곧장 가지 않는다 — 높이를 버리고 미끄러지면 단 위로 뜬다.
	const FVector Target = ChaseGoal;
	const bool bSameFloor = FMath::Abs(Target.Z - Feet.Z) < 30.0f
		&& !AIGPrologueWorldScene::IsOnStairFlight(Target)
		&& !AIGPrologueWorldScene::IsOnStairFlight(Feet);
	if (bSameFloor && HasClearLine(Feet + FVector(0.0f, 0.0f, 60.0f), Target + FVector(0.0f, 0.0f, 60.0f))
		&& HasClearLine(Feet + FVector(0.0f, 0.0f, 140.0f), Target + FVector(0.0f, 0.0f, 140.0f)))
	{
		Path.Reset();
		PathCursor = 0;
		bHasFinalTarget = false;
		StepToward(FVector(Target.X, Target.Y, Feet.Z), IGManagerPatrol::ChaseSpeed, DeltaSeconds);
		AnimateGait(DeltaSeconds, IGManagerPatrol::ChaseSpeed);
	}
	else
	{
		if (Now - LastRepathSeconds > 0.5 || PathCursor >= Path.Num())
		{
			LastRepathSeconds = Now;
			SetDestination(FindNearestNode(Target));
		}
		TickWalking(DeltaSeconds, IGManagerPatrol::ChaseSpeed);
	}
	AimChaseTorch(DeltaSeconds);
}

// -- 밤2 손수레 심부름 ----------------------------------------------------------

void AIGManagerPatrol::StartCartErrand(
	AIGSwingDoor* InnerDoor,
	AIGSwingDoor* BoothDoor,
	AActor* Cart,
	const FTransform& ParkTransform,
	const float FetchSeconds)
{
	if (bErrand || !Nodes.IsValidIndex(ErrandCotNode))
	{
		return;
	}
	ErrandInnerDoor = InnerDoor;
	ErrandBoothDoor = BoothDoor;
	ErrandCart = Cart;
	ErrandCartPark = ParkTransform;
	ErrandFetchSeconds = FMath::Max(5.0f, FetchSeconds);
	bErrand = true;
	bErrandCartAttached = false;
	bErrandDoorClosedBehind = false;
	bKeyHunt = false;
	bHuntAction = false;
	bOnDuty = true;
	Exposure = 0.0f;
	bSawPlayerHide = false;
	Path.Reset();
	PathCursor = 0;
	bHasFinalTarget = false;
	DwellRemaining = 0.0f;
	SetActorLocation(Nodes[ErrandCotNode].Feet);
	SetActorRotation(FRotator(0.0f, -45.0f, 0.0f));
	State = EIGManagerPatrolState::Patrolling;
	StateStartSeconds = GetWorld()->GetTimeSeconds();
	ShowBody(true);
	PlayKeys(true);
	SetErrandStep(EErrandStep::Leaving);
	SetDestination(RestNode);
	SetActorTickEnabled(true);
	UE_LOG(LogTemp, Display, TEXT("MANAGER_ERRAND start fetch=%.0f"), ErrandFetchSeconds);
}

void AIGManagerPatrol::SetAvoidingStorage(const bool bAvoid)
{
	bAvoidStorageFloor = bAvoid;
	if (!bAvoid || !bOnDuty || bErrand || bKeyHunt)
	{
		return;
	}
	// 3층을 돌던 중이거나 3층으로 가던 중이면 그 길을 접고 안쪽 방으로 돌아간다.
	const bool bOnStorageFloor = FMath::Abs(GetFeet().Z - AIGPrologueWorldScene::GetStoreyFloorZ(2)) < 60.0f;
	const bool bHeadingThere = Route.IsValidIndex(RouteCursor) && IsStorageFloorNode(Route[RouteCursor].Node);
	if ((bOnStorageFloor || bHeadingThere)
		&& (State == EIGManagerPatrolState::Patrolling || State == EIGManagerPatrolState::Investigating
			|| State == EIGManagerPatrolState::Staring))
	{
		BeginRetreat(IGManagerPatrol::RoundRestSeconds);
	}
}

bool AIGManagerPatrol::IsStorageFloorNode(const int32 Node) const
{
	return Nodes.IsValidIndex(Node)
		&& FMath::Abs(Nodes[Node].Feet.Z - AIGPrologueWorldScene::GetStoreyFloorZ(2)) < 60.0f;
}

bool AIGManagerPatrol::IsErrandReturning() const
{
	return bErrand
		&& (ErrandStep == EErrandStep::Turning
			|| ErrandStep == EErrandStep::Pushing
			|| ErrandStep == EErrandStep::Parking);
}

void AIGManagerPatrol::SetErrandFetchSecondsForTesting(const float Seconds)
{
	ErrandFetchSeconds = FMath::Max(0.5f, Seconds);
}

void AIGManagerPatrol::SetErrandStep(const EErrandStep Step)
{
	ErrandStep = Step;
	ErrandStepStart = GetWorld()->GetTimeSeconds();
	NextErrandSoundSeconds = ErrandStepStart + 1.0;
	UE_LOG(LogTemp, Display, TEXT("MANAGER_ERRAND step=%d at %s"), static_cast<int32>(Step), *GetFeet().ToCompactString());
}

int32 AIGManagerPatrol::GetErrandStepTarget() const
{
	switch (ErrandStep)
	{
	case EErrandStep::Leaving: return RestNode;
	case EErrandStep::ToCart: return ErrandCartNode;
	case EErrandStep::Pushing: return ErrandParkNode;
	case EErrandStep::Returning: return ErrandCotNode;
	default: return INDEX_NONE;
	}
}

void AIGManagerPatrol::ResumeErrandPath()
{
	// 조사나 수색에서 돌아왔다. 사슬을 풀던 중이면 손수레 앞으로, 밀던 중이면 손수레를 다시
	// 잡을 수 없으니(조사하러 손을 놓았다) 그 자리에 세우고 안쪽 방으로 간다.
	switch (ErrandStep)
	{
	case EErrandStep::Fetching:
		SetDestination(ErrandCartNode);
		break;
	case EErrandStep::Turning:
	case EErrandStep::Pushing:
	case EErrandStep::Parking:
		// 손수레는 놓아 둔 자리에 남는다. 아침에 다시 가져가면 된다.
		SetErrandStep(EErrandStep::Returning);
		SetDestination(ErrandCotNode);
		break;
	default:
		if (GetErrandStepTarget() != INDEX_NONE)
		{
			SetDestination(GetErrandStepTarget());
		}
		break;
	}
}

bool AIGManagerPatrol::WaitForErrandDoor(const float DeltaSeconds)
{
	if (PathCursor >= Path.Num())
	{
		return false;
	}
	const int32 Next = Path[PathCursor];
	const int32 Here = FindNearestNode(GetFeet());
	AIGSwingDoor* Door = nullptr;
	// 안쪽 방 문은 안팎 두 점 사이, 관리실 문은 관리실 안쪽 점과 문 밖 점 사이에 있다.
	const auto Crosses = [&](const int32 A, const int32 B)
	{
		return (Here == A && Next == B) || (Here == B && Next == A)
			|| (Next == B && FVector::Dist2D(GetFeet(), Nodes[A].Feet) < 60.0f)
			|| (Next == A && FVector::Dist2D(GetFeet(), Nodes[B].Feet) < 60.0f);
	};
	if (Crosses(ErrandInnerInNode, ErrandInnerOutNode))
	{
		Door = ErrandInnerDoor.Get();
	}
	else if (Crosses(ErrandOfficeNode, RestNode))
	{
		Door = ErrandBoothDoor.Get();
	}
	if (!Door || (Door->IsOpen() && !Door->IsSwinging()))
	{
		return false;
	}
	if (Door->IsFullyClosed())
	{
		Door->BeginScriptedSwingWithLoudness(true, IGManagerPatrol::FireDoorOpenLoudness, 1.0f);
	}
	FaceYaw((Door->GetComponentsBoundingBox().GetCenter() - GetFeet()).Rotation().Yaw, DeltaSeconds);
	AnimateGait(DeltaSeconds, 0.0f);
	return true;
}

void AIGManagerPatrol::TickErrand(const float DeltaSeconds)
{
	const double Now = GetWorld()->GetTimeSeconds();
	switch (ErrandStep)
	{
	case EErrandStep::Fetching:
	{
		// 손수레 앞에 서서 사슬을 푼다. 손전등은 손수레를 비춘다.
		AnimateGait(DeltaSeconds, 0.0f);
		FaceYaw(90.0f, DeltaSeconds, 120.0f);
		if (const AActor* Cart = ErrandCart.Get())
		{
			AimTorch(Cart->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f), DeltaSeconds);
		}
		if (Now >= NextErrandSoundSeconds)
		{
			PlayErrandChain();
			StepSeed = StepSeed * 1664525u + 1013904223u;
			NextErrandSoundSeconds = Now + IGManagerPatrol::ChainIntervalMin
				+ IGManagerPatrol::ChainIntervalJitter * static_cast<double>(StepSeed % 100u) / 100.0;
		}
		if (Now - ErrandStepStart >= ErrandFetchSeconds)
		{
			AttachErrandCart(true);
			SetErrandStep(EErrandStep::Turning);
			OnErrandReturning.Broadcast();
		}
		return;
	}
	case EErrandStep::Turning:
	{
		// 손수레를 끌어내 관리실 쪽으로 돌린다.
		AnimateGait(DeltaSeconds, 20.0f);
		FaceYaw(5.0f, DeltaSeconds, 22.0f);
		RelaxTorch(DeltaSeconds);
		if (Now >= NextErrandSoundSeconds)
		{
			PlayErrandSqueak();
			NextErrandSoundSeconds = Now + IGManagerPatrol::SqueakIntervalSeconds;
		}
		if (Now - ErrandStepStart >= IGManagerPatrol::CartTurnSeconds)
		{
			SetErrandStep(EErrandStep::Pushing);
			SetDestination(ErrandParkNode);
		}
		return;
	}
	case EErrandStep::Parking:
	{
		AnimateGait(DeltaSeconds, 0.0f);
		RelaxTorch(DeltaSeconds);
		if (Now - ErrandStepStart >= IGManagerPatrol::CartParkSeconds)
		{
			SetErrandStep(EErrandStep::Returning);
			SetDestination(ErrandCotNode);
		}
		return;
	}
	default:
		break;
	}
	if (WaitForErrandDoor(DeltaSeconds))
	{
		return;
	}
	// 안쪽 방에 들어서면 문을 닫는다. 그 뒤로는 문을 다시 열지 않는다.
	if (ErrandStep == EErrandStep::Returning && !bErrandDoorClosedBehind
		&& FVector::Dist2D(GetFeet(), Nodes[ErrandInnerInNode].Feet) < 40.0f)
	{
		if (AIGSwingDoor* Door = ErrandInnerDoor.Get(); Door && Door->IsOpen() && !Door->IsSwinging())
		{
			Door->BeginScriptedSwingWithLoudness(false, IGManagerPatrol::FireDoorOpenLoudness, 1.2f);
		}
		bErrandDoorClosedBehind = true;
	}
	const bool bPushing = ErrandStep == EErrandStep::Pushing;
	if (bPushing && Now >= NextErrandSoundSeconds)
	{
		PlayErrandSqueak();
		NextErrandSoundSeconds = Now + IGManagerPatrol::SqueakIntervalSeconds;
	}
	TickWalking(DeltaSeconds, bPushing ? IGManagerPatrol::CartPushSpeed : IGManagerPatrol::WalkSpeed);
}

void AIGManagerPatrol::ArriveErrandStop()
{
	switch (ErrandStep)
	{
	case EErrandStep::Leaving:
		OnErrandOutside.Broadcast();
		SetErrandStep(EErrandStep::ToCart);
		SetDestination(ErrandCartNode);
		return;
	case EErrandStep::ToCart:
		SetErrandStep(EErrandStep::Fetching);
		PlayErrandChain();
		return;
	case EErrandStep::Pushing:
		AttachErrandCart(false);
		SetErrandStep(EErrandStep::Parking);
		return;
	case EErrandStep::Returning:
		if (IsPlayerWatching(GetFeet()))
		{
			return;
		}
		FinishErrand(false);
		return;
	default:
		return;
	}
}

void AIGManagerPatrol::AttachErrandCart(const bool bAttach, const bool bPark)
{
	AActor* Cart = ErrandCart.Get();
	if (!Cart || bErrandCartAttached == bAttach)
	{
		return;
	}
	bErrandCartAttached = bAttach;
	if (bAttach)
	{
		// 손잡이(손수레 +X)가 그를 향하고, 몸은 그의 앞 60 cm에 온다.
		Cart->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
		Cart->SetActorRelativeLocation(IGManagerPatrol::CartHoldOffset);
		Cart->SetActorRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
		IGAudio::SpawnOneShotAt(
			this,
			IGAudio::SampleOr(TEXT("Lock_Rattle"), [this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateLockedRattle(this); }),
			Cart->GetActorLocation() + FVector(0.0f, 0.0f, 20.0f), 0.6f, 0.78f, 200.0f, 1600.0f, EIGAudioBus::World);
		Caption(
			NSLOCTEXT("IGMissingFloor", "CaptionMokChainDrop", "[사슬이 바닥에 떨어지는 소리]"),
			Cart->GetActorLocation(), LastErrandCaptionSeconds, 0.0f);
		return;
	}
	Cart->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (bPark)
	{
		Cart->SetActorTransform(ErrandCartPark);
		return;
	}
	// 쫓거나 살피러 손을 놓았다. 그 자리 바닥에 선다.
	FVector Where = Cart->GetActorLocation();
	Where.Z = GetFeet().Z;
	Cart->SetActorLocation(Where);
}

void AIGManagerPatrol::FinishErrand(const bool bCaught)
{
	if (!bErrand)
	{
		return;
	}
	if (bErrandCartAttached)
	{
		AttachErrandCart(false);
	}
	if (AIGSwingDoor* Door = ErrandInnerDoor.Get(); Door && Door->IsOpen())
	{
		if (Door->IsSwinging())
		{
			Door->ForceOpenState(false);
		}
		else
		{
			Door->BeginScriptedSwingWithLoudness(false, IGManagerPatrol::FireDoorOpenLoudness, 1.2f);
		}
	}
	bErrand = false;
	ErrandStep = EErrandStep::None;
	State = EIGManagerPatrolState::Resting;
	Exposure = 0.0f;
	ShowBody(false);
	GoOffDuty();
	UE_LOG(LogTemp, Display, TEXT("MANAGER_ERRAND done caught=%d"), bCaught ? 1 : 0);
	OnErrandFinished.Broadcast(bCaught);
}

void AIGManagerPatrol::PlayErrandChain()
{
	const AActor* Cart = ErrandCart.Get();
	const FVector Where = Cart ? Cart->GetActorLocation() + FVector(0.0f, 0.0f, 25.0f) : GetFeet() + FVector(0.0f, 60.0f, 40.0f);
	StepSeed = StepSeed * 1664525u + 1013904223u;
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(TEXT("Lock_Rattle"), [this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateLockedRattle(this); }),
		Where, 0.55f, 0.82f + static_cast<float>(StepSeed % 7u) * 0.02f, 200.0f, 1700.0f, EIGAudioBus::World);
	Caption(
		NSLOCTEXT("IGMissingFloor", "CaptionMokChain", "[쇠사슬이 철컥거리는 소리]"),
		Where, LastErrandCaptionSeconds, IGManagerPatrol::ErrandCaptionGapSeconds);
}

void AIGManagerPatrol::PlayErrandSqueak()
{
	const AActor* Cart = ErrandCart.Get();
	const FVector Where = Cart ? Cart->GetActorLocation() + FVector(0.0f, 0.0f, 10.0f) : GetFeet();
	StepSeed = StepSeed * 1664525u + 1013904223u;
	IGAudio::SpawnExpendableOneShotAt(
		this,
		IGAudio::SampleOr(TEXT("Cart_Squeak"), [this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateToolCartRoll(this); }),
		Where, 0.5f, 0.95f + static_cast<float>(StepSeed % 9u) * 0.012f, 200.0f, 1800.0f, EIGAudioBus::World);
	Caption(
		NSLOCTEXT("IGMissingFloor", "CaptionMokCartSqueak", "[손수레 바퀴가 끽끽거리는 소리]"),
		Where, LastErrandCaptionSeconds, IGManagerPatrol::ErrandCaptionGapSeconds);
}

// -- 열쇠 찾기 ---------------------------------------------------------------

bool AIGManagerPatrol::ShouldStartKeyHunt() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	UIGStoryStateSubsystem* Story = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGStoryStateSubsystem>()
		: nullptr;
	const FGameplayTag KeyTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("State.MissingFloor.HasStairKey")), false);
	// 손님이 와 있는 동안에는 열쇠를 찾으러 나서지 않는다. 둘 다 403호 문으로 와서 부딪쳤다.
	for (TActorIterator<AIGNightThreatDirector> It(GetWorld()); It; ++It)
	{
		const EIGGuestStage VisitStage = It->GetGuestStage();
		if (VisitStage != EIGGuestStage::Idle && VisitStage != EIGGuestStage::Spent)
		{
			return false;
		}
	}
	return Narrative && Story && KeyTag.IsValid() && Story->HasState(KeyTag)
		&& !Narrative->HasBeatPlayed(IGManagerPatrol::KeyHuntBeat)
		&& Nodes.IsValidIndex(Home403Node)
		&& Nodes.IsValidIndex(AnnexFrontNode)
		&& !HuntStops.IsEmpty();
}

void AIGManagerPatrol::StartKeyHuntForTesting()
{
	if (!bOnDuty || HuntStops.IsEmpty())
	{
		return;
	}
	if (State != EIGManagerPatrolState::Resting)
	{
		EnterRestingAtDoor(0.0f);
	}
	StartKeyHunt();
}

void AIGManagerPatrol::ResolveHuntDoors()
{
	if (const AIGPrologueWorldScene* Building = Scene.Get())
	{
		HomeDoor = Building->GetHomeDoor();
	}
	for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
	{
		RoofDoor = It->GetStairGate();
		AnnexDoor = It->GetAnnexGate();
		break;
	}
}

void AIGManagerPatrol::StartKeyHunt()
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->MarkBeatPlayed(IGManagerPatrol::KeyHuntBeat);
	}
	ResolveHuntDoors();
	bKeyHunt = true;
	bHuntAction = false;
	bSawInAnnex = false;
	HuntCursor = 0;
	UE_LOG(LogTemp, Display, TEXT("MANAGER_KEYHUNT start"));
	LeaveRoom();
	SetDestination(HuntStops[0].Node);
}

void AIGManagerPatrol::AdvanceKeyHunt()
{
	bHuntAction = false;
	++HuntCursor;
	if (!HuntStops.IsValidIndex(HuntCursor))
	{
		BeginRetreat(IGManagerPatrol::RoundRestSeconds);
		return;
	}
	// 4층 복도로 들어간다. 유담이 소리가 들리는 거리에 있으면 관리인이 뭘 찾으러 왔는지 알아챈다.
	if (HuntStops[HuntCursor].Node == Home403Node
		&& FVector::Dist(GetFeet(), GetPlayerFeet()) < IGManagerPatrol::AudibleDistance)
	{
		ThinkOnce(TEXT("Night3.MokKeyHunt.Fourth"),
			NSLOCTEXT("IGMissingFloor", "YudamMokKeyHunt", "4층까지 올라오네. 열쇠 찾으러 온 거야."));
	}
	UE_LOG(LogTemp, Display, TEXT("MANAGER_KEYHUNT stop=%d"), HuntCursor);
	SetDestination(HuntStops[HuntCursor].Node);
}

void AIGManagerPatrol::BeginHuntAction()
{
	if (!HuntStops.IsValidIndex(HuntCursor) || HuntStops[HuntCursor].Action == EHuntAction::None)
	{
		AdvanceKeyHunt();
		return;
	}
	bHuntAction = true;
	HuntActionStep = 0;
	HuntActionStart = GetWorld()->GetTimeSeconds();
}

void AIGManagerPatrol::TickHuntAction(const float DeltaSeconds)
{
	using namespace IGManagerPatrol;
	const double Now = GetWorld()->GetTimeSeconds();
	const double Elapsed = Now - HuntActionStart;
	const auto NextStep = [this, Now](const int32 Step)
	{
		HuntActionStep = Step;
		HuntActionStart = Now;
	};
	// 걸린 문. 열쇠나 번호가 맞아도 안쪽 걸쇠·빗장이 문을 한 뼘에서 잡는다. 두 번 덜컥거린다.
	const auto RattleCaught = [this](AIGSwingDoor& Door, const FText& Text)
	{
		for (int32 Rattle = 0; Rattle < 2; ++Rattle)
		{
			FTimerHandle RattleTimer;
			GetWorldTimerManager().SetTimer(
				RattleTimer,
				FTimerDelegate::CreateWeakLambda(this, [this, Where = Door.GetLatchSoundLocation()]()
				{
					IGAudio::SpawnOneShotAt(
						this,
						IGAudio::SampleOr(TEXT("Lock_Rattle"),
							[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateLockedRattle(this); }),
						Where, 0.9f, FMath::FRandRange(0.94f, 1.04f), 160.0f, 1400.0f, EIGAudioBus::World);
				}),
				0.05f + 0.55f * Rattle,
				false);
		}
		AIGHorrorHUD::PushAudioCaptionAt(this, Text, 2.2f, Door.GetLatchSoundLocation());
	};
	// 문간에서 안을 비춘다. 손전등이 좌우로 훑고, 그 안에 서 있는 사람은 들킨다(감지는 Tick에서 한다).
	const auto SweepInto = [this, DeltaSeconds, Elapsed](const FVector& Inside)
	{
		const FVector Eye = GetFeet() + FVector(0.0f, 0.0f, 150.0f);
		const float Sweep = FMath::Sin(static_cast<float>(Elapsed) * 1.4f) * HuntSweepDegrees;
		const FVector Direction = (Inside - Eye).RotateAngleAxis(Sweep, FVector::UpVector);
		FaceYaw((Inside - Eye).Rotation().Yaw, DeltaSeconds, 120.0f);
		AimTorch(Eye + Direction, DeltaSeconds);
	};
	AnimateGait(DeltaSeconds, 0.0f);
	switch (HuntStops[HuntCursor].Action)
	{
	case EHuntAction::HomeDoor:
	{
		AIGSwingDoor* Door = HomeDoor.Get();
		if (!Door)
		{
			AdvanceKeyHunt();
			return;
		}
		const FVector Latch = Door->GetLatchSoundLocation();
		if (HuntActionStep == 0)
		{
			// 새 세입자 도어락 번호는 관리인이 정해 준 것이다. 번호를 누른다.
			FaceYaw((Latch - GetFeet()).Rotation().Yaw, DeltaSeconds);
			IGAudio::SpawnOneShotAt(
				this, UIGToneSequenceSoundWave::CreateDoorlockCode(this, true),
				Latch + FVector(0.0f, -6.0f, 0.0f), 0.6f, 1.0f, 120.0f, 1400.0f, EIGAudioBus::World);
			Caption(
				NSLOCTEXT("IGMissingFloor", "CaptionGuestKeypad", "[밖에서 도어락 번호 누르는 소리]"),
				Latch, LastAlarmCaptionSeconds, 0.0f);
			NextStep(1);
			return;
		}
		if (HuntActionStep == 1)
		{
			FaceYaw((Latch - GetFeet()).Rotation().Yaw, DeltaSeconds);
			if (Elapsed < HuntKeypadSeconds)
			{
				return;
			}
			if (!Door->IsOpen() && Door->IsLatched())
			{
				RattleCaught(*Door, NSLOCTEXT("IGMissingFloor", "CaptionGuestLatch", "[걸쇠에 문이 걸려 덜컥거린다]"));
				if (AIGPlayerCharacter* Character = Player.Get();
					Character && FVector::Dist2D(Character->GetActorLocation(), Latch) < 700.0f
					&& FMath::Abs(Character->GetActorLocation().Z - Latch.Z) < 200.0f)
				{
					if (UIGStressComponent* Stress = Character->GetStress())
					{
						Stress->ApplyScare(0.45f);
					}
					Character->PlayScareKick(1.4f);
				}
				UE_LOG(LogTemp, Display, TEXT("MANAGER_KEYHUNT home latched"));
				NextStep(10);
				return;
			}
			if (!Door->IsOpen())
			{
				Door->BeginScriptedSwing(true);
			}
			UE_LOG(LogTemp, Display, TEXT("MANAGER_KEYHUNT home opened"));
			NextStep(2);
			return;
		}
		if (HuntActionStep == 2)
		{
			SweepInto(FVector(110.0f, -110.0f, AIGPrologueWorldScene::FourthFloorZ + 95.0f));
			if (Elapsed >= HuntThresholdSeconds)
			{
				if (Door->IsOpen() && !Door->IsSwinging()
					&& FVector::Dist(GetPlayerFeet(), Door->GetActorLocation()) > HuntDoorCloseClearance)
				{
					Door->BeginScriptedSwing(false);
				}
				NextStep(3);
			}
			return;
		}
		if ((HuntActionStep == 3 && Elapsed >= HuntCloseWaitSeconds)
			|| (HuntActionStep == 10 && Elapsed >= HuntCaughtWaitSeconds))
		{
			AdvanceKeyHunt();
		}
		return;
	}

	case EHuntAction::RoofDoorOpen:
	{
		AIGSwingDoor* Door = RoofDoor.Get();
		if (!Door)
		{
			AdvanceKeyHunt();
			return;
		}
		FaceYaw(90.0f, DeltaSeconds);
		if (Door->IsOpen())
		{
			// 열렸거나 열리는 중이다. 다 열리면 나선다.
			if (!Door->IsSwinging())
			{
				AdvanceKeyHunt();
			}
			return;
		}
		if (HuntActionStep == 0)
		{
			PlayKeys(true);
			NextStep(1);
			return;
		}
		if (HuntActionStep == 1 && Elapsed >= HuntKeysToSwingSeconds)
		{
			Door->BeginScriptedSwing(true);
			UE_LOG(LogTemp, Display, TEXT("MANAGER_KEYHUNT roof door opened"));
			NextStep(2);
		}
		return;
	}

	case EHuntAction::RoofSweep:
		// 물탱크 통로를 동쪽으로 훑는다.
		SweepInto(GetFeet() + FVector(600.0f, -20.0f, 120.0f));
		if (Elapsed >= HuntThresholdSeconds)
		{
			AdvanceKeyHunt();
		}
		return;

	case EHuntAction::AnnexDoor:
	{
		AIGSwingDoor* Door = AnnexDoor.Get();
		if (!Door)
		{
			AdvanceKeyHunt();
			return;
		}
		const FVector Inside(110.0f, 660.0f, 1290.0f);
		if (HuntActionStep == 0)
		{
			FaceYaw(90.0f, DeltaSeconds);
			if (Door->IsOpen())
			{
				NextStep(2);
				return;
			}
			PlayKeys(true);
			NextStep(1);
			return;
		}
		if (HuntActionStep == 1)
		{
			FaceYaw(90.0f, DeltaSeconds);
			if (Elapsed < HuntKeysToSwingSeconds)
			{
				return;
			}
			if (Door->IsLatched())
			{
				// 열쇠는 돌았는데 안쪽 빗장에 걸린다. 관리인은 더 밀지 않는다.
				RattleCaught(*Door, NSLOCTEXT("IGMissingFloor", "CaptionGuestBolt", "[빗장에 걸려 문이 덜컹거린다]"));
				UE_LOG(LogTemp, Display, TEXT("MANAGER_KEYHUNT annex bolted"));
				NextStep(10);
				return;
			}
			Door->BeginScriptedSwing(true);
			UE_LOG(LogTemp, Display, TEXT("MANAGER_KEYHUNT annex opened"));
			NextStep(2);
			return;
		}
		if (HuntActionStep == 2)
		{
			// 문간에서만 비춘다. 5층 안으로는 끝까지 들어가지 않는다.
			SweepInto(Inside);
			if (Elapsed >= HuntThresholdSeconds)
			{
				if (Door->IsOpen() && !Door->IsSwinging())
				{
					Door->BeginScriptedSwing(false);
				}
				NextStep(3);
			}
			return;
		}
		if (HuntActionStep == 3 && Elapsed >= HuntCloseWaitSeconds)
		{
			AdvanceKeyHunt();
			return;
		}
		if (HuntActionStep == 10 && Elapsed >= HuntCaughtWaitSeconds)
		{
			if (IsInsideAnnex(GetPlayerFeet()))
			{
				ThinkOnce(TEXT("Night3.MokBoltHeld"),
					NSLOCTEXT("IGMissingFloor", "YudamMokBoltHeld", "빗장 걸어 두길 잘했다."));
			}
			AdvanceKeyHunt();
		}
		return;
	}

	case EHuntAction::RoofDoorClose:
	{
		AIGSwingDoor* Door = RoofDoor.Get();
		if (!Door)
		{
			AdvanceKeyHunt();
			return;
		}
		FaceYaw(90.0f, DeltaSeconds);
		if (HuntActionStep == 0)
		{
			// 내려가면서 문단속을 한다. 유담이 문간 가까이 있으면 닫지 않는다.
			if (Door->IsOpen() && !Door->IsSwinging()
				&& FVector::Dist(GetPlayerFeet(), Door->GetActorLocation()) > HuntDoorCloseClearance)
			{
				Door->BeginScriptedSwing(false);
				UE_LOG(LogTemp, Display, TEXT("MANAGER_KEYHUNT roof door closed"));
			}
			NextStep(1);
			return;
		}
		if (!Door->IsSwinging())
		{
			AdvanceKeyHunt();
		}
		return;
	}

	default:
		AdvanceKeyHunt();
		return;
	}
}

void AIGManagerPatrol::SlamAnnexAndFlee()
{
	// 문을 쾅 닫고 서둘러 내려간다. 안에 있는 사람은 잡지 않는다.
	bSawInAnnex = false;
	if (AIGSwingDoor* Door = AnnexDoor.Get(); Door && Door->IsOpen() && !Door->IsSwinging())
	{
		Door->BeginScriptedSwingWithLoudness(false, 0.42f, 0.55f, false);
		Caption(
			NSLOCTEXT("IGMissingFloor", "CaptionMokSlamsAnnex", "[철문이 쾅 닫히는 소리]"),
			Door->GetLatchSoundLocation(), LastAlarmCaptionSeconds, 0.0f);
	}
	ThinkOnce(TEXT("Night3.MokAvoidsAnnex"),
		NSLOCTEXT("IGMissingFloor", "YudamMokAvoidsAnnex", "5층에는 안 들어와."));
	Caption(
		NSLOCTEXT("IGMissingFloor", "CaptionMokHurries", "[슬리퍼 소리가 급히 멀어진다]"),
		GetFeet(), LastAlarmCaptionSeconds, 0.0f);
	UE_LOG(LogTemp, Display, TEXT("MANAGER_KEYHUNT annex slammed"));
	BeginRetreat(IGManagerPatrol::KnockRestSeconds);
}

bool AIGManagerPatrol::IsForbiddenNow(const FVector& Feet) const
{
	return !bKeyHunt && IsForbiddenFourthFloor(Feet);
}

bool AIGManagerPatrol::IsInsideAnnex(const FVector& Feet)
{
	// 5층 증축부 안(씬 BuildFifthFloorAnnex). 문선 Y 452.5 안쪽이다.
	return Feet.X > -390.0f && Feet.X < 390.0f
		&& Feet.Y > 460.0f && Feet.Y < 940.0f
		&& Feet.Z > 1150.0f && Feet.Z < 1450.0f;
}

// -- 감각 --------------------------------------------------------------------

void AIGManagerPatrol::UpdateSenses(const float DeltaSeconds)
{
	AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const FVector Feet = GetFeet();

	float LitDistance = 0.0f;
	const bool bLit = IsPlayerVisibleInTorch(LitDistance);
	const bool bClose = IsPlayerCloseBehind();
	if (bLit || bClose)
	{
		LastSeenSeconds = Now;
		LastKnownPlayerFeet = GetPlayerFeet();
		const float Near = bClose ? 1.0f : 1.0f - FMath::Clamp(LitDistance / IGManagerPatrol::SightRange, 0.0f, 1.0f);
		Exposure = FMath::Min(1.0f, Exposure + DeltaSeconds * (0.9f + 2.6f * Near));
	}
	else
	{
		Exposure = FMath::Max(0.0f, Exposure - DeltaSeconds * 0.5f);
	}

	if (State == EIGManagerPatrolState::Chasing)
	{
		return;
	}
	// 열쇠를 찾다가 5층 문간에서 안을 비췄는데 유담이 서 있다. 안으로 들어가지도, 확인하러
	// 다가가지도 않는다. 문이 다 열리면 쾅 닫고 서둘러 내려간다.
	const AIGSwingDoor* Gate = AnnexDoor.Get();
	if (bKeyHunt && (bLit || bClose) && Gate && Gate->IsOpen() && IsInsideAnnex(GetPlayerFeet())
		&& Nodes.IsValidIndex(AnnexFrontNode)
		&& FVector::Dist2D(Feet, Nodes[AnnexFrontNode].Feet) < 60.0f
		&& FMath::Abs(Feet.Z - Nodes[AnnexFrontNode].Feet.Z) < 40.0f)
	{
		bSawInAnnex = true;
	}
	if (bSawInAnnex)
	{
		if (!Gate || !Gate->IsSwinging())
		{
			SlamAnnexAndFlee();
		}
		return;
	}
	// 4층 복도에 있는 유담은 비추기만 하고 복도로는 들어가지 않는다(열쇠를 찾을 때만 예외).
	if (Exposure >= 1.0f && !IsForbiddenNow(GetPlayerFeet()))
	{
		BeginChase();
		return;
	}
	if (Exposure >= 0.35f)
	{
		ThinkOnce(TEXT("Night3.MokTorchWarning"),
			NSLOCTEXT("IGMissingFloor", "YudamMokTorchWarning", "저 불빛에 오래 비치면 들켜."));
		if (State == EIGManagerPatrolState::Patrolling)
		{
			BeginInvestigation(GetPlayerFeet(), true);
		}
		return;
	}
	// 그녀의 손전등이 그를 비추면 그도 안다. 빛이 오는 쪽으로 간다.
	if ((State == EIGManagerPatrolState::Patrolling || State == EIGManagerPatrolState::Searching)
		&& IsPlayerTorchOnMe())
	{
		Exposure = FMath::Max(Exposure, 0.5f);
		BeginInvestigation(GetPlayerFeet(), true);
	}
}

bool AIGManagerPatrol::IsPlayerVisibleInTorch(float& OutDistance) const
{
	const AIGPlayerCharacter* Character = Player.Get();
	if (!Character || !bBodyShown || Character->IsConcealedInHidingSpot())
	{
		return false;
	}
	const FVector Origin = Torch->GetComponentLocation();
	const FVector Chest = Character->GetActorLocation();
	const FVector Head = Character->GetPawnViewLocation();
	const FVector ToChest = Chest - Origin;
	OutDistance = ToChest.Size();
	if (OutDistance > IGManagerPatrol::SightRange)
	{
		return false;
	}
	// 원뿔은 손전등이 향한 쪽이다. 가슴이나 머리 중 하나라도 원뿔 안이면 비친 것이다.
	const FVector Beam = Torch->GetForwardVector();
	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(IGManagerPatrol::SightHalfAngle));
	const bool bChestInCone = FVector::DotProduct(Beam, ToChest.GetSafeNormal()) >= CosLimit;
	const bool bHeadInCone = FVector::DotProduct(Beam, (Head - Origin).GetSafeNormal()) >= CosLimit;
	if (!bChestInCone && !bHeadInCone)
	{
		return false;
	}
	return HasClearLine(Origin, bChestInCone ? Chest : Head);
}

bool AIGManagerPatrol::IsPlayerCloseBehind() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	if (!Character || !bBodyShown || Character->IsConcealedInHidingSpot())
	{
		return false;
	}
	const FVector Feet = GetFeet();
	const FVector PlayerFeet = GetPlayerFeet();
	return FVector::Dist2D(Feet, PlayerFeet) <= IGManagerPatrol::CloseSenseRadius
		&& FMath::Abs(PlayerFeet.Z - Feet.Z) < 60.0f
		&& HasClearLine(Feet + FVector(0.0f, 0.0f, 120.0f), Character->GetActorLocation());
}

bool AIGManagerPatrol::IsPlayerTorchOnMe() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	if (!Character || !bBodyShown)
	{
		return false;
	}
	const UIGFlashlightComponent* PlayerTorch = Character->GetFlashlight();
	if (!PlayerTorch || !PlayerTorch->IsProvidingLight())
	{
		return false;
	}
	FVector ViewLocation = Character->GetPawnViewLocation();
	FRotator ViewRotation = Character->GetControlRotation();
	if (const AController* Controller = Character->GetController())
	{
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	const FVector Face = GetFeet() + FVector(0.0f, 0.0f, 150.0f);
	const FVector ToFace = Face - ViewLocation;
	if (ToFace.Size() > 1500.0f)
	{
		return false;
	}
	// 빛이 얼굴에 닿았거나, 9 m 안에서 불빛 자체가 보인다.
	const bool bBeamOnFace = FVector::DotProduct(ViewRotation.Vector(), ToFace.GetSafeNormal()) >= 0.92f;
	if (!bBeamOnFace && ToFace.Size() > 900.0f)
	{
		return false;
	}
	return HasClearLine(Face, ViewLocation);
}

bool AIGManagerPatrol::HasClearLine(const FVector& From, const FVector& To) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGManagerPatrolSight), false, this);
	if (const AIGPlayerCharacter* Character = Player.Get())
	{
		Params.AddIgnoredActor(Character);
	}
	if (const AIGListenerEntity* Upstairs = Listener.Get())
	{
		Params.AddIgnoredActor(Upstairs);
	}
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
}

FVector AIGManagerPatrol::GetHideApproachFeet() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	if (const AIGHidingSpot* Spot = Character ? Character->GetHidingSpot() : nullptr)
	{
		return Spot->GetApproachLocation();
	}
	return GetPlayerFeet();
}

FVector AIGManagerPatrol::GetPlayerFeet() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		return FVector::ZeroVector;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 96.0f;
	return Character->GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);
}

bool AIGManagerPatrol::IsForbiddenFourthFloor(const FVector& Feet)
{
	// 4층 슬래브 위이고 목(계단탑 출입구 노드 X -305)보다 더 들어간 복도다.
	return Feet.Z > AIGPrologueWorldScene::FourthFloorZ - 60.0f
		&& Feet.Z < AIGPrologueWorldScene::FourthFloorZ + 200.0f
		&& Feet.X > -290.0f;
}

bool AIGManagerPatrol::IsPlayerWatching(const FVector& Where) const
{
	const AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		return false;
	}
	FVector ViewLocation = Character->GetPawnViewLocation();
	FRotator ViewRotation = Character->GetControlRotation();
	if (const AController* Controller = Character->GetController())
	{
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	const FVector Chest = Where + FVector(0.0f, 0.0f, 120.0f);
	const FVector ToChest = Chest - ViewLocation;
	if (ToChest.Size() > 2000.0f
		|| FVector::DotProduct(ViewRotation.Vector(), ToChest.GetSafeNormal()) < 0.45f)
	{
		return false;
	}
	return HasClearLine(ViewLocation, Chest);
}

// -- 소리 --------------------------------------------------------------------

void AIGManagerPatrol::HandleNoise(const FIGNoiseEvent& Event)
{
	if (!bOnDuty || !bBodyShown || Event.Loudness < IGManagerPatrol::HearingThreshold)
	{
		return;
	}
	const AIGPlayerCharacter* Character = Player.Get();
	if (!Character || Event.Instigator.Get() != Character)
	{
		return;
	}
	const FVector Ear = GetFeet() + FVector(0.0f, 0.0f, 150.0f);
	if (FMath::Abs(Event.Location.Z - Ear.Z) > IGManagerPatrol::HearingHeight
		|| FVector::Dist(Event.Location, Ear) > Event.Radius)
	{
		return;
	}
	// 소리 난 바닥으로 간다. 소리는 발 높이보다 높은 데서 나기도 해서 바닥으로 내려 잡는다.
	FVector Floor = Event.Location;
	Floor.Z = GetPlayerFeet().Z;
	if (FMath::Abs(Event.Location.Z - Floor.Z) > 250.0f)
	{
		Floor.Z = Event.Location.Z - 100.0f;
	}
	// 쫓는 중에 들린 소리는 그 소리가 난 자리를 알려 줄 뿐이다. 그녀가 지금 어디 있는지는 모른다.
	if (State == EIGManagerPatrolState::Chasing)
	{
		LastKnownPlayerFeet = Floor;
		LastSeenSeconds = FMath::Max(LastSeenSeconds, GetWorld()->GetTimeSeconds() - 1.0);
		return;
	}
	BeginInvestigation(Floor, true);
}

void AIGManagerPatrol::HandleListenerKnock(const FVector& Where)
{
	if (!bOnDuty || !bBodyShown
		|| State == EIGManagerPatrolState::Catching
		|| State == EIGManagerPatrolState::Frozen
		|| State == EIGManagerPatrolState::Retreating
		|| State == EIGManagerPatrolState::Resting)
	{
		return;
	}
	if (FVector::Dist(Where, GetFeet()) > IGManagerPatrol::KnockReach)
	{
		return;
	}
	// 그도 듣는다. 열쇠 소리가 멎고, 손전등이 소리 난 쪽으로 올라간다.
	State = EIGManagerPatrolState::Frozen;
	PlayBodyAnim(FreezeAnim ? FreezeAnim.Get() : IdleAnim.Get(), true, 1.0f);
	StateStartSeconds = GetWorld()->GetTimeSeconds();
	Exposure = 0.0f;
	const FVector ToKnock = Where - (GetFeet() + FVector(0.0f, 0.0f, 150.0f));
	SetActorRotation(FRotator(0.0f, ToKnock.Rotation().Yaw, 0.0f));
	TorchRelative = FRotator(FMath::Clamp(ToKnock.Rotation().Pitch, -10.0f, 35.0f), 0.0f, 0.0f);
	Torch->SetRelativeRotation(TorchRelative);
	Caption(
		NSLOCTEXT("IGMissingFloor", "CaptionMokStopped", "[열쇠 소리가 뚝 멎는다]"),
		GetFeet(), LastAlarmCaptionSeconds, 0.0f);
	if (FVector::Dist(GetFeet(), GetPlayerFeet()) < 1500.0f)
	{
		ThinkOnce(TEXT("Night3.MokHeardKnock"),
			NSLOCTEXT("IGMissingFloor", "YudamMokHeardKnock", "저 사람도 들은 거야. 지금 그 소리."));
	}
}

// -- 몸 ----------------------------------------------------------------------

bool AIGManagerPatrol::StepToward(const FVector& Target, const float Speed, const float DeltaSeconds)
{
	const FVector From = GetFeet();
	// 2·3층 방화문이 닫혀 있으면 연다. 몸에 충돌이 없으니 여기서 막지 않으면 문을 뚫고
	// 지나간다. 문이 다 열릴 때까지 문 앞에 서서 문 쪽을 본다.
	if (AIGSwingDoor* Door = FindFireDoorAhead(From, Target))
	{
		if (Door->IsFullyClosed())
		{
			Door->BeginScriptedSwingWithLoudness(true, IGManagerPatrol::FireDoorOpenLoudness, 1.0f);
		}
		const FVector ToDoor = Door->GetComponentsBoundingBox().GetCenter() - From;
		FaceYaw(ToDoor.Rotation().Yaw, DeltaSeconds);
		return false;
	}
	const FVector Delta = Target - From;
	const float Distance = Delta.Size();
	if (Distance <= 2.0f)
	{
		SetActorLocation(Target);
		return true;
	}
	const float Travel = FMath::Min(Speed * DeltaSeconds, Distance);
	const FVector Next = From + Delta / Distance * Travel;
	SetActorLocation(Next);
	const FVector Planar(Delta.X, Delta.Y, 0.0f);
	if (Planar.SizeSquared() > 1.0f)
	{
		FaceYaw(Planar.Rotation().Yaw, DeltaSeconds);
	}
	DistanceSinceStep += FVector::Dist2D(From, Next);
	const float Stride = Speed > IGManagerPatrol::InvestigateSpeed + 10.0f
		? IGManagerPatrol::RunStride
		: IGManagerPatrol::WalkStride;
	if (DistanceSinceStep >= Stride)
	{
		DistanceSinceStep = 0.0f;
		PlayStep();
	}
	if (GetWorld()->GetTimeSeconds() >= NextKeysSeconds)
	{
		PlayKeys(false);
	}
	return Travel >= Distance - 0.01f;
}

void AIGManagerPatrol::FaceYaw(const float DesiredYaw, const float DeltaSeconds, const float DegreesPerSecond)
{
	const FRotator Current = GetActorRotation();
	const float Delta = FMath::FindDeltaAngleDegrees(Current.Yaw, DesiredYaw);
	const float Turn = FMath::Clamp(Delta, -DegreesPerSecond * DeltaSeconds, DegreesPerSecond * DeltaSeconds);
	SetActorRotation(FRotator(0.0f, Current.Yaw + Turn, 0.0f));
}

void AIGManagerPatrol::AimTorch(const FVector& WorldTarget, const float DeltaSeconds)
{
	const FVector Local = BodyPivot->GetComponentTransform().InverseTransformPosition(WorldTarget)
		- IGManagerPatrol::TorchOffset;
	FRotator Desired = Local.Rotation();
	Desired.Yaw = FMath::Clamp(Desired.Yaw, -60.0f, 60.0f);
	Desired.Pitch = FMath::Clamp(Desired.Pitch, -45.0f, 35.0f);
	Desired.Roll = 0.0f;
	TorchRelative = FMath::RInterpTo(TorchRelative, Desired, DeltaSeconds, 6.0f);
	Torch->SetRelativeRotation(TorchRelative);
}

void AIGManagerPatrol::RelaxTorch(const float DeltaSeconds)
{
	// 걸을 때는 발 앞 3~4 m를 비춘다.
	TorchRelative = FMath::RInterpTo(TorchRelative, FRotator(-9.0f, 0.0f, 0.0f), DeltaSeconds, 2.5f);
	Torch->SetRelativeRotation(TorchRelative);
}

void AIGManagerPatrol::AnimateGait(const float DeltaSeconds, const float Speed)
{
	if (BodySkeletal)
	{
		// 리깅한 몸. 걸음 주기가 실제 빠르기와 맞게 재생 배율을 정한다. 서 있을 때는
		// 하던 일에 맞는 자세다 — 노크에 굳고, 문간에서 훑고, 쉬면 숨만 쉰다.
		BodyPivot->SetRelativeLocation(FVector::ZeroVector);
		BodyPivot->SetRelativeRotation(FRotator::ZeroRotator);
		if (State == EIGManagerPatrolState::Catching)
		{
			return;
		}
		if (Speed > IGManagerPatrol::InvestigateSpeed + 10.0f && RunAnim)
		{
			PlayBodyAnim(RunAnim, true, FMath::Clamp(Speed / IGManagerPatrol::RunCycleSpeed, 0.6f, 2.6f));
		}
		else if (Speed > 1.0f)
		{
			PlayBodyAnim(WalkAnim, true, FMath::Clamp(Speed / IGManagerPatrol::WalkCycleSpeed, 0.5f, 2.4f));
		}
		else if (State == EIGManagerPatrolState::Frozen && FreezeAnim)
		{
			PlayBodyAnim(FreezeAnim, true, 1.0f);
		}
		else if ((DwellRemaining > 0.0f || State == EIGManagerPatrolState::Staring) && LookAnim)
		{
			PlayBodyAnim(LookAnim, true, 1.0f);
		}
		else
		{
			PlayBodyAnim(IdleAnim, true, 1.0f);
		}
		return;
	}
	// 걸음마다 몸이 조금 오르내리고 좌우로 기운다. 메시에 걸음 동작이 없어서 몸통만 흔든다.
	if (Speed > 1.0f)
	{
		const float Stride = Speed > IGManagerPatrol::InvestigateSpeed + 10.0f
			? IGManagerPatrol::RunStride
			: IGManagerPatrol::WalkStride;
		GaitPhase += DeltaSeconds * Speed / Stride * PI;
	}
	else
	{
		GaitPhase = FMath::FInterpTo(GaitPhase, FMath::RoundToFloat(GaitPhase / PI) * PI, DeltaSeconds, 4.0f);
	}
	const float Bob = FMath::Abs(FMath::Sin(GaitPhase)) * (Speed > 150.0f ? 2.4f : 1.4f);
	const float Sway = FMath::Sin(GaitPhase) * (Speed > 150.0f ? 2.0f : 1.2f);
	BodyPivot->SetRelativeLocation(FVector(0.0f, 0.0f, Bob));
	BodyPivot->SetRelativeRotation(FRotator(0.0f, 0.0f, Sway));
}

void AIGManagerPatrol::PlayStep()
{
	const FVector Feet = GetFeet();
	// 밟은 표면을 본다. 계단 철판이면 쇳소리, 나머지는 슬리퍼를 끄는 소리다.
	const bool bMetal = IGWalker::FindSurface(GetWorld(), Feet, this) == EIGFootstepSurface::MetalStair;
	StepSeed = StepSeed * 1664525u + 1013904223u;
	const float Loud = State == EIGManagerPatrolState::Chasing || State == EIGManagerPatrolState::Retreating
		? 0.72f : 0.5f;
	USoundBase* Sound = bMetal
		? IGAudio::SampleVariantOr(
			TEXT("Foot_MetalStair"), 5, StepSeed,
			[this]() -> USoundBase*
			{
				return UIGToneSequenceSoundWave::CreateSurfaceFootstep(
					this, EIGFootstepSurface::MetalStair, 0.88f, 0.8f);
			})
		: static_cast<USoundBase*>(UIGToneSequenceSoundWave::CreateSlipperScuff(this));
	IGAudio::SpawnExpendableOneShotAt(
		this, Sound, Feet + FVector(0.0f, 0.0f, 6.0f), Loud,
		0.92f + static_cast<float>(StepSeed % 9u) * 0.01f,
		200.0f, 1800.0f, EIGAudioBus::World);
	if (!bMetal)
	{
		Caption(
			NSLOCTEXT("IGMissingFloor", "CaptionMokSlippers", "[슬리퍼를 끄는 발소리]"),
			Feet, LastStepCaptionSeconds, 10.0f);
	}
}

void AIGManagerPatrol::PlayKeys(const bool bForceCaption)
{
	const double Now = GetWorld()->GetTimeSeconds();
	// 거리만 재면 다른 층에서 서 있는 관리인까지 알아본다. 열쇠 소리가 실제로 나고,
	// 같은 층의 열린 통로로 들을 수 있을 때만 알아본다. 구출 장면의 대화를 가로채지 않는다.
	const FVector SoundAt = GetFeet() + FVector(0.0f, 0.0f, 95.0f);
	const FVector ListenerAt = Player.IsValid() ? Player->GetPawnViewLocation() : FVector::ZeroVector;
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (Player.IsValid() && bBodyShown && !bErrand && Narrative && Narrative->GetNightIndex() == 3
		&& FMath::Abs(GetFeet().Z - GetPlayerFeet().Z) < 180.0f
		&& FVector::DistSquared(SoundAt, ListenerAt) < FMath::Square(1400.0f))
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ManagerHeard), false, this);
		Params.AddIgnoredActor(Player.Get());
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, SoundAt, ListenerAt, ECC_Visibility, Params))
		{
			ThinkOnce(TEXT("Night3.MokFirstHeard"),
				NSLOCTEXT("IGMissingFloor", "YudamMokFirstHeard", "관리인 아저씨네. 어제 관리실 들어간 걸 알아챘나?"));
		}
	}
	StepSeed = StepSeed * 1664525u + 1013904223u;
	NextKeysSeconds = Now + 5.0 + static_cast<double>(StepSeed % 40u) * 0.1;
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateKeyRingJingle(this, (StepSeed & 1u) != 0u),
		GetFeet() + FVector(0.0f, 0.0f, 95.0f), 0.42f, 1.0f, 160.0f, 1500.0f, EIGAudioBus::World);
	Caption(
		NSLOCTEXT("IGMissingFloor", "CaptionMokKeys", "[열쇠 꾸러미가 짤랑거리는 소리]"),
		GetFeet(), LastKeysCaptionSeconds, bForceCaption ? 0.0f : 12.0f);
}

void AIGManagerPatrol::Caption(
	const FText& Text,
	const FVector& Where,
	double& LastShown,
	const float MinGapSeconds)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastShown < MinGapSeconds
		|| FVector::Dist(Where, GetPlayerFeet()) > IGManagerPatrol::AudibleDistance)
	{
		return;
	}
	LastShown = Now;
	AIGHorrorHUD::PushAudioCaptionAt(this, Text, 2.0f, Where);
}
