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
#include "Entity/IGListenerEntity.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "GameFramework/Controller.h"
#include "Engine/SkeletalMesh.h"
#include "Interaction/IGFireDoorWedge.h"
#include "Interaction/IGHidingSpot.h"
#include "Interaction/IGSwingDoor.h"
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
	}
	GetWorldTimerManager().SetTimer(
		DutyTimer, this, &AIGManagerPatrol::RefreshDuty, IGManagerPatrol::DutyCheckSeconds, true);
}

void AIGManagerPatrol::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DutyTimer);
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

int32 AIGManagerPatrol::AddNode(const FVector& Feet, const bool bOnStair)
{
	FPatrolNode& Node = Nodes.AddDefaulted_GetRef();
	Node.Feet = Feet;
	Node.bOnStair = bOnStair;
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
	const bool bShouldPatrol = !bScripted
		&& Narrative
		&& Narrative->GetNightIndex() == 3
		&& Narrative->IsHourSealed()
		&& !Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer)
		&& Player.IsValid();
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
	// 누가 잡았든 침대 리셋이 걸리면 그는 안쪽 방으로 돌아가 있다. 깨는 동안
	// 복도에 서 있으면 깬 사람 눈앞에서 순간이동한 사람이 된다.
	if (const AIGNightLoopDirector* Loop = NightLoop.Get())
	{
		if (Loop->IsCaptureResetInFlight() || Loop->GetCaptureCount() != SeenCaptureCount)
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

bool AIGManagerPatrol::IsCaptureAllowed() const
{
	// 난이도는 위층 사람의 튜닝이 든다. 그가 잡지 않는 밤이면 아무도 잡지 않는다.
	const AIGListenerEntity* Upstairs = Listener.Get();
	return !Upstairs || Upstairs->GetTuning().bCaptureEnabled;
}

void AIGManagerPatrol::ThinkOnce(const TCHAR* BeatId, const FText& Thought)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || Narrative->HasBeatPlayed(FName(BeatId)))
	{
		return;
	}
	Narrative->MarkBeatPlayed(FName(BeatId));
	AIGHorrorHUD::PushThought(this, Thought, 3.2f);
}

// -- 상태 전환 ---------------------------------------------------------------

void AIGManagerPatrol::StartRound()
{
	++RoundCount;
	RouteCursor = 0;
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
	SetDestination(Route.IsValidIndex(0) ? Route[0].Node : RestNode);
}

void AIGManagerPatrol::EnterRestingAtDoor(const float RestSeconds)
{
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
	if (State == EIGManagerPatrolState::Chasing
		|| State == EIGManagerPatrolState::Catching
		|| State == EIGManagerPatrolState::Frozen
		|| State == EIGManagerPatrolState::Retreating
		|| State == EIGManagerPatrolState::Resting)
	{
		return;
	}
	State = EIGManagerPatrolState::Investigating;
	StateStartSeconds = GetWorld()->GetTimeSeconds();
	const FVector Target = IsForbiddenFourthFloor(Where)
		? Nodes[FourthFloorDoorNode].Feet
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
	const bool bWasChasing = State == EIGManagerPatrolState::Chasing;
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
	const FVector Target = IsForbiddenFourthFloor(LastKnownPlayerFeet)
		? Nodes[GetFourthFloorStandNode()].Feet
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
			|| FMath::Abs(Nodes[Index].Feet.Z - Nodes[Start].Feet.Z) > 40.0f
			|| IsForbiddenFourthFloor(Nodes[Index].Feet))
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
	SetDestination(RestNode);
}

void AIGManagerPatrol::BeginCatch()
{
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
	// 숨은 자리를 열어 끌어낸다. 숨는 걸 봤을 때만 여기까지 온다.
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
	Character->PlayScareKick(1.6f);
	PlayKeys(false);
}

void AIGManagerPatrol::FinishCatch()
{
	bCatchResolved = true;
	AIGPlayerCharacter* Character = Player.Get();
	if (Character && IsCaptureAllowed())
	{
		if (AIGNightLoopDirector* Loop = NightLoop.Get())
		{
			Loop->RequestExternalCapture(Character);
		}
		// 리셋이 걸리면 RefreshDuty가 안쪽 방으로 돌려보낸다. 걸리지 못했으면(밤4
		// 가면처럼 다른 대본이 잡고 있으면) 그냥 돌아간다.
		if (!NightLoop.IsValid() || !NightLoop->IsCaptureResetInFlight())
		{
			BeginRetreat(IGManagerPatrol::CaptureRestSeconds);
		}
		return;
	}
	// 추격 없음. 비추기만 하고 손을 놓은 채 돌아간다.
	BeginRetreat(IGManagerPatrol::CaptureRestSeconds);
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
		IGAudio::SpawnOneShotAt(
			this, UIGToneSequenceSoundWave::CreateDoorCreak(this, true),
			GetFeet() + FVector(0.0f, 30.0f, 110.0f), 0.5f, 0.92f, 200.0f, 1500.0f, EIGAudioBus::World);
		EnterRestingAtDoor(RestAfterRetreat);
		return;
	case EIGManagerPatrolState::Patrolling:
	{
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

bool AIGManagerPatrol::IsFireDoorClosed() const
{
	if (!bFireDoorResolved)
	{
		AIGManagerPatrol* Self = const_cast<AIGManagerPatrol*>(this);
		Self->bFireDoorResolved = true;
		for (TActorIterator<AIGFireDoorWedge> It(GetWorld()); It; ++It)
		{
			Self->FireDoor = It->GetDoor();
			break;
		}
	}
	const AIGSwingDoor* Door = FireDoor.Get();
	return Door && !Door->IsOpen();
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
		// 아무것도 없었다. 원래 돌던 길의 다음 자리로 간다.
		State = EIGManagerPatrolState::Patrolling;
		StateStartSeconds = GetWorld()->GetTimeSeconds();
		Exposure = 0.0f;
		SetDestination(Route[FMath::Clamp(RouteCursor, 0, Route.Num() - 1)].Node);
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
		if (!bSawPlayerHide)
		{
			LastKnownPlayerFeet = PlayerFeet;
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
	if (bSawPlayerHide && Character->IsConcealedInHidingSpot())
	{
		if (FVector::Dist2D(Feet, HideSpotFeet) <= IGManagerPatrol::HideReach
			&& FMath::Abs(HideSpotFeet.Z - Feet.Z) <= IGManagerPatrol::CatchHeight)
		{
			BeginCatch();
			return;
		}
	}
	else if (Planar <= IGManagerPatrol::CatchReach
		&& FMath::Abs(PlayerFeet.Z - Feet.Z) <= IGManagerPatrol::CatchHeight
		&& HasClearLine(Feet + FVector(0.0f, 0.0f, 120.0f), Character->GetActorLocation()))
	{
		BeginCatch();
		return;
	}

	// 4층 복도로는 따라가지 않는다. 목에서 서서 비춘다.
	if (IsForbiddenFourthFloor(PlayerFeet))
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
		AimTorch(Character->GetActorLocation(), DeltaSeconds);
		return;
	}

	// 같은 바닥에서 막힌 데 없이 보이면 곧장 간다. 아니면 그녀와 가까운 노드로 길을 잡는다.
	// 계단 위에서는 곧장 가지 않는다 — 높이를 버리고 미끄러지면 단 위로 뜬다.
	const FVector Target = bSawPlayerHide ? HideSpotFeet : PlayerFeet;
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
	AimTorch(Character->GetActorLocation(), DeltaSeconds);
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
	const float DistanceToPlayer = FVector::Dist(Feet, GetPlayerFeet());

	// 처음 들리는 거리. 열쇠와 슬리퍼가 누구인지 그녀가 먼저 안다.
	if (bBodyShown && DistanceToPlayer < 1400.0f)
	{
		ThinkOnce(TEXT("Night3.MokFirstHeard"),
			NSLOCTEXT("IGMissingFloor", "YudamMokFirstHeard", "관리인 아저씨다. 이 시간에 손전등을 들고."));
	}

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
	// 4층 복도에 선 그녀는 비추기만 한다. 그 복도로는 들어가지 않는다.
	if (Exposure >= 1.0f && !IsForbiddenFourthFloor(GetPlayerFeet()))
	{
		BeginChase();
		return;
	}
	if (Exposure >= 0.35f)
	{
		ThinkOnce(TEXT("Night3.MokTorchWarning"),
			NSLOCTEXT("IGMissingFloor", "YudamMokTorchWarning", "저 불빛 안에 들어가면 들켜."));
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
	if (State == EIGManagerPatrolState::Chasing)
	{
		LastKnownPlayerFeet = GetPlayerFeet();
		LastSeenSeconds = FMath::Max(LastSeenSeconds, GetWorld()->GetTimeSeconds() - 1.0);
		return;
	}
	// 소리 난 바닥으로 간다. 소리는 발 높이보다 높은 데서 나기도 해서 바닥으로 내려 잡는다.
	FVector Floor = Event.Location;
	Floor.Z = GetPlayerFeet().Z;
	if (FMath::Abs(Event.Location.Z - Floor.Z) > 250.0f)
	{
		Floor.Z = Event.Location.Z - 100.0f;
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
	bool bMetal = false;
	if (const UWorld* World = GetWorld())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(IGManagerPatrolStep), false, this);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(
				Hit, Feet + FVector(0.0f, 0.0f, 30.0f), Feet - FVector(0.0f, 0.0f, 40.0f),
				ECC_Visibility, Params)
			&& Hit.GetComponent()
			&& Hit.GetComponent()->ComponentHasTag(FName(TEXT("Footstep.MetalStair"))))
		{
			bMetal = true;
		}
	}
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
