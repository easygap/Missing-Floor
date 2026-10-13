#include "Entity/IGSiteCheckVisit.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Animation/AnimSequence.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/IGOnScreen.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Entity/IGWalkerKit.h"
#include "Environment/IGDustSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/IGSwingDoor.h"
#include "Kismet/GameplayStatics.h"
#include "Player/IGHorrorHUD.h"

namespace IGSiteCheckVisit
{
	// 걸음. 옥상 계단은 디딤판 22 cm, 단 17 cm라 가파르다. 둘 다 천천히 오른다.
	constexpr float FlatSpeed = 85.0f;
	constexpr float StairSpeed = 38.0f;
	/** 수평 1 cm에 이보다 많이 오르내리는 구간은 계단이다. 옥상 계단은 0.77, 아래 네 단은 0.82다. */
	constexpr float StairSlope = 0.3f;

	// 리깅한 몸의 걸음 주기. Scripts/blender/rig_walker.py와 같다. 경찰관은 resident와 같은
	// 32프레임에 두 걸음 50 cm씩, 관리인은 patrol의 36프레임에 46 cm씩이다.
	constexpr float OfficerCycleSeconds = 32.0f / 30.0f;
	constexpr float OfficerCycleSpeed = 100.0f / OfficerCycleSeconds;
	constexpr float ManagerCycleSeconds = 36.0f / 30.0f;
	constexpr float ManagerCycleSpeed = 92.0f / ManagerCycleSeconds;
	/** 계단에서는 초당 한 걸음 반이다. */
	constexpr float StairStepsPerSecond = 1.5f;

	/** 경찰관은 관리인 뒤로 이만큼 떨어져 따라간다. */
	constexpr float FollowGap = 130.0f;
	/** 길 앞 이 거리 안에 그녀가 서 있으면 비켜 줄 때까지 선다. */
	constexpr float YieldLookAhead = 60.0f;
	constexpr float YieldRadius = 62.0f;

	/** 어깨가 넓은 어른 남자 둘이다. 303호보다 한 치 크다. */
	constexpr float CapsuleRadius = 25.0f;
	constexpr float CapsuleHalfHeight = 86.0f;
	/** 경찰관 머리 높이. 그녀가 그를 볼 수 있는지 잴 때 쓴다. */
	constexpr float HeadHeight = 160.0f;

	// 문. 관리인이 열쇠를 꺼내고 이만큼 뒤에 자물쇠가 돈다.
	constexpr double KeysToSwingSeconds = 1.1;
	/** 문짝이 쓸고 가는 쪽에서 다가오면 문짝 길이보다 멀리 선다. 반대쪽에서는 손잡이 앞이다. */
	constexpr float SweepSideClearance = 110.0f;
	constexpr float FarSideClearance = 45.0f;
	/** 안에서 빗장이 걸려 있으면 이 간격으로 두드린다. */
	constexpr double DoorKnockInterval = 5.0;

	// 노크. A_PolicePatrol_Knock은 66프레임이고 주먹이 22·30·38프레임에 벽에 닿는다.
	constexpr float KnockContacts[3] = {22.0f / 30.0f, 30.0f / 30.0f, 38.0f / 30.0f};
	constexpr double AskAt = 2.4;
	/** A_PolicePatrol_Listen(60프레임)으로 넘어가 귀를 대고, 대답이 없는 걸 확인할 때까지. */
	constexpr double ListenAt = 3.3;
	constexpr double CheckedAt = 8.4;
	/** 그녀가 곁에 오고 이만큼 뒤에 두드린다. 보기 전에 끝나지 않게. */
	constexpr double WithHerLeadSeconds = 1.5;
	/** 그녀가 옥상에 없는 시간이 이만큼 쌓이면 둘이서 확인한다. */
	constexpr float WaitAloneSeconds = 120.0f;

	/** 말 사이 한 숨. */
	constexpr double LineGapSeconds = 0.5;
	/** 말 줄이 안 비어도 앞의 말이 떠 있을 시간보다 이만큼 지나면 다음 말을 민다. */
	constexpr double LineLaneSlackSeconds = 2.0;
	/** 사진을 찍으며 이만큼 남아 있다가 그녀가 그대로 있으면 걸어 내려간다. */
	constexpr double LingerSeconds = 30.0;
	constexpr double ShutterTimes[2] = {1.6, 9.0};

	/** 그녀가 이 거리 안이면 문을 닫지 않는다. 문간에 선 몸을 문짝이 뚫는다. */
	constexpr float GateRestoreClearance = 260.0f;
	/** 걸어 나가는 몸은 이만큼 안 보이면 그 자리에서 거둔다. */
	constexpr double UnseenVanishSeconds = 1.5;

	// 5층 부속동 안쪽. 바닥은 Z 1200이다.
	constexpr float AnnexMinX = -390.0f;
	constexpr float AnnexMaxX = 390.0f;
	constexpr float AnnexMinY = 462.0f;
	constexpr float AnnexMaxY = 940.0f;
	constexpr float RoofZ = 1200.0f;
	/** 그녀의 발이 이보다 높으면 옥상에 올라와 있다. */
	constexpr float RoofLevelZ = 1100.0f;

	/** 철문 두 짝의 문선. 문짝은 경첩에서 +X로 85·88 cm이고, 옥상 문은 계단 쪽으로, 5층 문은 안으로 열린다. */
	constexpr float StairGateLineY = 220.0f;
	constexpr float StairGateMinX = -322.0f;
	constexpr float StairGateMaxX = -233.0f;
	constexpr float AnnexGateLineY = 452.5f;
	constexpr float AnnexGateMinX = 85.0f;
	constexpr float AnnexGateMaxX = 175.0f;

	/** 그녀가 볼 수 있는 거리. 이보다 멀면 보여도 알아보지 못한다고 친다. */
	constexpr float SpawnSightDistance = 1800.0f;
	constexpr float WithHerSightDistance = 700.0f;

	const FVector ManagerSpot(205.0f, 540.0f, RoofZ);
	const FVector OfficerSpot(216.0f, 696.0f, RoofZ);
	/** 노크가 닿는 자리. 가운데 칸 석고 면(X 250)의 주먹 높이다. */
	const FVector KnockPoint(250.0f, 698.0f, 1318.0f);
	/** 관리인이 바라보는 5층 철문 문간. */
	const FVector AnnexDoorway(130.0f, 452.5f, RoofZ);

	static float FourthFloorZ() { return AIGPrologueWorldScene::GetStoreyFloorZ(3); }

	/** 4층 복도 서쪽 끝, 옥상 계단 밑. 관리인이 앞, 경찰관이 계단탑 출입구 쪽에 선다. */
	static FVector ManagerStart() { return FVector(-262.0f, -292.0f, FourthFloorZ()); }
	static FVector OfficerStart() { return FVector(-298.0f, -340.0f, FourthFloorZ()); }

	/**
	 * 옥상 계단 밑에서 5층 문 안까지. 아래 네 단(18 cm)과 참, 위 열네 단(17 cm)의 디딤판
	 * 가운데를 잇는다. 옥상 철문을 나서면 난간 사이 120 cm 통로의 한가운데(Y 220)를 걷고,
	 * 문틀 기둥(X -227.5)을 비켜 돌아선다.
	 */
	static void AppendClimb(TArray<FVector>& Points)
	{
		const float Z4 = FourthFloorZ();
		Points.Add(FVector(-277.5f, -238.0f, Z4));
		Points.Add(FVector(-277.5f, -150.0f, Z4 + 72.0f));
		Points.Add(FVector(-277.5f, -125.0f, Z4 + 72.0f));
		Points.Add(FVector(-277.5f, -100.0f, 980.0f));
		Points.Add(FVector(-277.5f, 186.0f, 1201.0f));
		Points.Add(FVector(-277.5f, 208.5f, RoofZ));
		Points.Add(FVector(-277.5f, 245.0f, RoofZ));
		Points.Add(FVector(-200.0f, 225.0f, RoofZ));
		Points.Add(FVector(130.0f, 220.0f, RoofZ));
		Points.Add(FVector(130.0f, 415.0f, RoofZ));
		Points.Add(FVector(130.0f, 500.0f, RoofZ));
	}
}

AIGSiteCheckVisit::AIGSiteCheckVisit()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	const auto MakeWalker = [this](
		const TCHAR* Name,
		TObjectPtr<USceneComponent>& OutFeet,
		TObjectPtr<UCapsuleComponent>& OutBlocker,
		TObjectPtr<USkeletalMeshComponent>& OutBody)
	{
		OutFeet = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("%sFeet"), Name));
		OutFeet->SetupAttachment(Root);
		OutFeet->SetMobility(EComponentMobility::Movable);

		// 플레이어 몸만 막는다. 시야와 상호작용 선은 지나간다.
		OutBlocker = CreateDefaultSubobject<UCapsuleComponent>(*FString::Printf(TEXT("%sBlocker"), Name));
		OutBlocker->SetupAttachment(OutFeet);
		OutBlocker->InitCapsuleSize(IGSiteCheckVisit::CapsuleRadius, IGSiteCheckVisit::CapsuleHalfHeight);
		OutBlocker->SetRelativeLocation(FVector(0.0f, 0.0f, IGSiteCheckVisit::CapsuleHalfHeight));
		OutBlocker->SetCollisionObjectType(ECC_WorldDynamic);
		OutBlocker->SetCollisionResponseToAllChannels(ECR_Ignore);
		OutBlocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		OutBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OutBlocker->SetCanEverAffectNavigation(false);

		OutBody = CreateDefaultSubobject<USkeletalMeshComponent>(*FString::Printf(TEXT("%sBody"), Name));
		OutBody->SetupAttachment(OutFeet);
		// 리깅한 몸은 정면이 +X, 원점이 발밑 가운데다.
		OutBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OutBody->SetCanEverAffectNavigation(false);
		OutBody->SetGenerateOverlapEvents(false);
		OutBody->SetCastShadow(true);
		// 걸음과 노크 동작이 바운드 밖으로 팔과 발을 낸다.
		OutBody->SetBoundsScale(1.4f);
		OutBody->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		OutBody->SetVisibility(false);
	};
	MakeWalker(TEXT("Officer"), OfficerFeet, OfficerBlocker, OfficerBody);
	MakeWalker(TEXT("Manager"), ManagerFeet, ManagerBlocker, ManagerBody);

	Officer.Feet = OfficerFeet;
	Officer.Blocker = OfficerBlocker;
	Officer.Body = OfficerBody;
	Officer.CycleSpeed = IGSiteCheckVisit::OfficerCycleSpeed;
	Officer.HalfCycleSeconds = IGSiteCheckVisit::OfficerCycleSeconds * 0.5f;
	Officer.StepSeed = 112u;
	Manager.Feet = ManagerFeet;
	Manager.Blocker = ManagerBlocker;
	Manager.Body = ManagerBody;
	Manager.CycleSpeed = IGSiteCheckVisit::ManagerCycleSpeed;
	Manager.HalfCycleSeconds = IGSiteCheckVisit::ManagerCycleSeconds * 0.5f;
	Manager.StepSeed = 4031u;

	SetActorHiddenInGame(true);
}

bool AIGSiteCheckVisit::LoadBodies()
{
	const auto LoadAnim = [](const TCHAR* Path) -> UAnimSequence*
	{
		return LoadObject<UAnimSequence>(nullptr, Path, nullptr, LOAD_NoWarn);
	};
	USkeletalMesh* OfficerMesh = LoadObject<USkeletalMesh>(
		nullptr, TEXT("/Game/Meshes/SK_PolicePatrol.SK_PolicePatrol"), nullptr, LOAD_NoWarn);
	USkeletalMesh* ManagerMesh = LoadObject<USkeletalMesh>(
		nullptr, TEXT("/Game/Meshes/SK_MokHansooPatrol.SK_MokHansooPatrol"), nullptr, LOAD_NoWarn);
	Officer.IdleAnim = LoadAnim(TEXT("/Game/Meshes/A_PolicePatrol_Idle.A_PolicePatrol_Idle"));
	Officer.WalkAnim = LoadAnim(TEXT("/Game/Meshes/A_PolicePatrol_Walk.A_PolicePatrol_Walk"));
	KnockAnim = LoadAnim(TEXT("/Game/Meshes/A_PolicePatrol_Knock.A_PolicePatrol_Knock"));
	ListenAnim = LoadAnim(TEXT("/Game/Meshes/A_PolicePatrol_Listen.A_PolicePatrol_Listen"));
	Manager.IdleAnim = LoadAnim(TEXT("/Game/Meshes/A_MokHansooPatrol_Idle.A_MokHansooPatrol_Idle"));
	Manager.WalkAnim = LoadAnim(TEXT("/Game/Meshes/A_MokHansooPatrol_Walk.A_MokHansooPatrol_Walk"));
	if (!OfficerMesh || !ManagerMesh || !Officer.IdleAnim || !Officer.WalkAnim || !KnockAnim || !ListenAnim
		|| !Manager.IdleAnim || !Manager.WalkAnim)
	{
		UE_LOG(LogTemp, Warning, TEXT("SK_PolicePatrol, SK_MokHansooPatrol or their animations are missing; the site check stays a text"));
		return false;
	}
	OfficerBody->SetSkeletalMesh(OfficerMesh);
	ManagerBody->SetSkeletalMesh(ManagerMesh);
	Animations = {Officer.IdleAnim, Officer.WalkAnim, Manager.IdleAnim, Manager.WalkAnim};
	return true;
}

void AIGSiteCheckVisit::SetGates(AIGSwingDoor* InStairGate, AIGSwingDoor* InAnnexGate)
{
	StairGate = InStairGate;
	AnnexGate = InAnnexGate;
}

FVector AIGSiteCheckVisit::GetOfficerSpotFeet()
{
	return IGSiteCheckVisit::OfficerSpot;
}

FVector AIGSiteCheckVisit::GetManagerSpotFeet()
{
	return IGSiteCheckVisit::ManagerSpot;
}

bool AIGSiteCheckVisit::IsInAnnex(const FVector& Feet)
{
	using namespace IGSiteCheckVisit;
	return Feet.X > AnnexMinX && Feet.X < AnnexMaxX
		&& Feet.Y > AnnexMinY && Feet.Y < AnnexMaxY
		&& Feet.Z > RoofZ - 50.0f && Feet.Z < RoofZ + 150.0f;
}

FVector AIGSiteCheckVisit::GetOfficerFeet() const
{
	return OfficerFeet->GetComponentLocation();
}

FVector AIGSiteCheckVisit::GetManagerFeet() const
{
	return ManagerFeet->GetComponentLocation();
}

void AIGSiteCheckVisit::SetRoute(FWalker& Walker, const TArray<FVector>& Points) const
{
	Walker.Path.StairSlope = IGSiteCheckVisit::StairSlope;
	Walker.Path.Set(Points);
	Walker.Travelled = 0.0f;
}

FVector AIGSiteCheckVisit::RoutePoint(
	const FWalker& Walker, const float Distance, FVector* OutDirection, bool* bOutOnStair) const
{
	return Walker.Path.At(Distance, Walker.Feet->GetComponentLocation(), OutDirection, bOutOnStair);
}

void AIGSiteCheckVisit::Place(FWalker& Walker, const FVector& Feet, const float Yaw) const
{
	Walker.Feet->SetWorldLocationAndRotation(Feet, FRotator(0.0f, Yaw, 0.0f));
}

void AIGSiteCheckVisit::Show(FWalker& Walker, const bool bVisible)
{
	Walker.bShown = bVisible;
	Walker.Body->SetVisibility(bVisible);
	Walker.Blocker->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (!bVisible)
	{
		Walker.Body->Stop();
		Walker.ActiveAnim = nullptr;
		Walker.bWalking = false;
	}
	SetActorHiddenInGame(!Officer.bShown && !Manager.bShown);
}

void AIGSiteCheckVisit::PlayBody(FWalker& Walker, UAnimSequence* Sequence, const float Rate, const bool bLoop)
{
	IGWalker::PlayBody(Walker.Body, Walker.ActiveAnim, Sequence, Rate, bLoop);
}

void AIGSiteCheckVisit::Turn(FWalker& Walker, const float TargetYaw, const float DeltaSeconds, const float Speed) const
{
	Walker.Feet->SetWorldRotation(FRotator(
		0.0f, IGWalker::TurnYaw(Walker.Feet->GetComponentRotation().Yaw, TargetYaw, DeltaSeconds, Speed), 0.0f));
}

void AIGSiteCheckVisit::TurnToward(FWalker& Walker, const FVector& Target, const float DeltaSeconds, const float Speed) const
{
	const FVector To = Target - Walker.Feet->GetComponentLocation();
	if (To.SizeSquared2D() > 1.0f)
	{
		Turn(Walker, To.GetSafeNormal2D().Rotation().Yaw, DeltaSeconds, Speed);
	}
}

bool AIGSiteCheckVisit::GetPlayerFeet(FVector& OutFeet) const
{
	const ACharacter* Character = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Character)
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	OutFeet = Character->GetActorLocation()
		- FVector(0.0f, 0.0f, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f);
	return true;
}

bool AIGSiteCheckVisit::GetPlayerEyes(FVector& OutEyes) const
{
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return false;
	}
	const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	OutEyes = PC && PC->PlayerCameraManager
		? PC->PlayerCameraManager->GetCameraLocation()
		: Pawn->GetPawnViewLocation();
	return true;
}

bool AIGSiteCheckVisit::CanPlayerSee(const FVector& Location, const float MaxDistance) const
{
	FVector Eyes;
	if (!GetPlayerEyes(Eyes) || FVector::Dist(Eyes, Location) > MaxDistance)
	{
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IGSiteCheckSight), false, this);
	Query.AddIgnoredActor(UGameplayStatics::GetPlayerPawn(this, 0));
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Eyes, Location, ECC_Visibility, Query);
}

bool AIGSiteCheckVisit::IsPlayerWithThem() const
{
	FVector Feet;
	if (!GetPlayerFeet(Feet))
	{
		return false;
	}
	if (IsInAnnex(Feet))
	{
		return true;
	}
	return Officer.bShown
		&& CanPlayerSee(GetOfficerFeet() + FVector(0.0f, 0.0f, IGSiteCheckVisit::HeadHeight),
			IGSiteCheckVisit::WithHerSightDistance);
}

bool AIGSiteCheckVisit::IsPlayerOnRoof() const
{
	FVector Feet;
	if (!GetPlayerFeet(Feet))
	{
		return false;
	}
	// 옥상과 5층, 그리고 옥상 계단 위. 계단 위에서는 노크가 바로 머리 위다.
	const bool bOnRoofStair = Feet.X > -335.0f && Feet.X < -220.0f
		&& Feet.Y > -240.0f && Feet.Y < 225.0f
		&& Feet.Z > IGSiteCheckVisit::FourthFloorZ() + 30.0f;
	return Feet.Z > IGSiteCheckVisit::RoofLevelZ || bOnRoofStair;
}

bool AIGSiteCheckVisit::IsDialogueLaneIdle() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	return !Hud || Hud->IsDialogueLaneIdle();
}

bool AIGSiteCheckVisit::IsPlayerInTheWay(const FWalker& Walker) const
{
	FVector PlayerFeet;
	if (!GetPlayerFeet(PlayerFeet))
	{
		return false;
	}
	const FVector Ahead = RoutePoint(Walker, FMath::Min(Walker.Travelled + IGSiteCheckVisit::YieldLookAhead, RouteLength(Walker)));
	return FVector::Dist2D(Ahead, PlayerFeet) < IGSiteCheckVisit::YieldRadius
		&& FMath::Abs(Ahead.Z - PlayerFeet.Z) < 120.0f;
}

bool AIGSiteCheckVisit::TryBegin()
{
	using namespace IGSiteCheckVisit;
	if (Stage != EStage::Hidden || !OfficerBody->GetSkeletalMeshAsset() || !ManagerBody->GetSkeletalMeshAsset())
	{
		return false;
	}
	// 4층 복도 끝에서 그 자리를 보고 있으면 거기서 사람이 솟는다. 볼 수 없을 때 나온다.
	for (const FVector& Start : {ManagerStart(), OfficerStart()})
	{
		if (CanPlayerSee(Start + FVector(0.0f, 0.0f, 150.0f), SpawnSightDistance)
			|| CanPlayerSee(Start + FVector(0.0f, 0.0f, 40.0f), SpawnSightDistance))
		{
			return false;
		}
	}
	TArray<FVector> Points;
	Points.Add(ManagerStart());
	AppendClimb(Points);
	Points.Add(ManagerSpot);
	SetRoute(Manager, Points);
	Points.Reset();
	Points.Add(OfficerStart());
	AppendClimb(Points);
	Points.Add(FVector(150.0f, 620.0f, RoofZ));
	Points.Add(OfficerSpot);
	SetRoute(Officer, Points);

	FVector Direction = FVector::ForwardVector;
	RoutePoint(Manager, 0.0f, &Direction);
	Place(Manager, Manager.Path.Points[0], Direction.GetSafeNormal2D().Rotation().Yaw);
	RoutePoint(Officer, 0.0f, &Direction);
	Place(Officer, Officer.Path.Points[0], Direction.GetSafeNormal2D().Rotation().Yaw);
	Show(Manager, true);
	Show(Officer, true);
	PlayBody(Manager, Manager.IdleAnim, 1.0f);
	PlayBody(Officer, Officer.IdleAnim, 1.0f);
	Stage = EStage::Climbing;
	StageStartedAt = GetWorld()->GetTimeSeconds();
	KeysOutAt = -1.0;
	SetActorTickEnabled(true);
	UE_LOG(LogTemp, Display, TEXT("SITECHECK begin manager_route=%.0f officer_route=%.0f"),
		RouteLength(Manager), RouteLength(Officer));
	return true;
}

void AIGSiteCheckVisit::Hurry()
{
	bHurry = true;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Stage == EStage::Climbing
		&& !IsPlayerOnRoof()
		&& !IGOnScreen::WasRecentlySeen(Officer.Body, 0.3f)
		&& !IGOnScreen::WasRecentlySeen(Manager.Body, 0.3f))
	{
		// 아무도 안 보는 계단 위다. 문을 열어 두고 제자리로 옮긴다.
		for (const TWeakObjectPtr<AIGSwingDoor>& Gate : {StairGate, AnnexGate})
		{
			if (AIGSwingDoor* Door = Gate.Get())
			{
				if (!Door->IsOpen())
				{
					Door->ForceOpenState(true);
					(Door == StairGate.Get() ? bOpenedStairGate : bOpenedAnnexGate) = true;
				}
			}
		}
		Manager.Travelled = RouteLength(Manager);
		Officer.Travelled = RouteLength(Officer);
		Place(Manager, IGSiteCheckVisit::ManagerSpot, (IGSiteCheckVisit::AnnexDoorway - IGSiteCheckVisit::ManagerSpot).Rotation().Yaw);
		Place(Officer, IGSiteCheckVisit::OfficerSpot, 0.0f);
		PlayBody(Manager, Manager.IdleAnim, 1.0f);
		PlayBody(Officer, Officer.IdleAnim, 1.0f);
		BeginKnocking(Now);
		return;
	}
	if (Stage == EStage::Waiting)
	{
		BeginKnocking(Now);
	}
}

void AIGSiteCheckVisit::Vanish()
{
	if (Stage == EStage::Hidden)
	{
		return;
	}
	Show(Officer, false);
	Show(Manager, false);
	// 밤이 오면 기다리지 않는다. 관리인이 연 문은 닫혀 있던 대로 돌린다.
	if (bOpenedStairGate && StairGate.IsValid())
	{
		StairGate->ForceOpenState(false);
	}
	if (bOpenedAnnexGate && AnnexGate.IsValid())
	{
		AnnexGate->ForceOpenState(false);
	}
	bOpenedStairGate = false;
	bOpenedAnnexGate = false;
	const bool bWasOut = Stage != EStage::Gone;
	Stage = EStage::Gone;
	SetActorTickEnabled(false);
	UE_LOG(LogTemp, Display, TEXT("SITECHECK vanish"));
	if (bWasOut)
	{
		OnGone.Broadcast(this);
	}
}

void AIGSiteCheckVisit::Withdraw()
{
	Show(Officer, false);
	Show(Manager, false);
	Stage = EStage::Gone;
	StageStartedAt = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Display, TEXT("SITECHECK withdraw witnessed=%d"), bWitnessed ? 1 : 0);
	if (RestoreGates())
	{
		SetActorTickEnabled(false);
	}
	OnGone.Broadcast(this);
}

bool AIGSiteCheckVisit::RestoreGates()
{
	FVector PlayerFeet = FVector::ZeroVector;
	const bool bHasPlayer = GetPlayerFeet(PlayerFeet);
	const auto TryClose = [this, bHasPlayer, &PlayerFeet](AIGSwingDoor* Door, bool& bOpened)
	{
		if (!bOpened)
		{
			return;
		}
		if (!Door || !Door->IsOpen())
		{
			bOpened = false;
			return;
		}
		const FVector Latch = Door->GetLatchSoundLocation();
		if (Door->IsSwinging()
			|| (bHasPlayer && FVector::Dist(PlayerFeet, Latch) < IGSiteCheckVisit::GateRestoreClearance)
			|| CanPlayerSee(Latch, 2400.0f))
		{
			return;
		}
		// 들리는 데 있으면 닫히는 소리를 낸다. 관리인이 나가며 닫은 문이다.
		if (bHasPlayer && FVector::Dist(PlayerFeet, Latch) < 1600.0f)
		{
			Door->BeginScriptedSwing(false, true, false);
		}
		else
		{
			Door->ForceOpenState(false);
		}
		bOpened = false;
	};
	TryClose(StairGate.Get(), bOpenedStairGate);
	TryClose(AnnexGate.Get(), bOpenedAnnexGate);
	return !bOpenedStairGate && !bOpenedAnnexGate;
}

bool AIGSiteCheckVisit::OpenGateAhead(const float DeltaSeconds, const double Now)
{
	// 관리인만 열쇠를 쥔다. 길이 문선을 지나기 전, 문짝이 쓸지 않는 자리에서 선다.
	using namespace IGSiteCheckVisit;
	const float Step = 40.0f;
	const FVector Here = RoutePoint(Manager, Manager.Travelled);
	const FVector Ahead = RoutePoint(Manager, FMath::Min(Manager.Travelled + Step, RouteLength(Manager)));
	struct FGateLine
	{
		AIGSwingDoor* Door;
		float LineY;
		float MinX;
		float MaxX;
		/** 문짝이 열리며 쓰는 쪽. 옥상 문은 계단 쪽(-Y), 5층 문은 안쪽(+Y)이다. */
		float SweepSign;
		bool* bOpened;
	};
	const FGateLine Lines[2] = {
		{StairGate.Get(), StairGateLineY, StairGateMinX, StairGateMaxX, -1.0f, &bOpenedStairGate},
		{AnnexGate.Get(), AnnexGateLineY, AnnexGateMinX, AnnexGateMaxX, 1.0f, &bOpenedAnnexGate}};
	for (const FGateLine& Line : Lines)
	{
		AIGSwingDoor* Door = Line.Door;
		if (!Door || (Door->IsOpen() && !Door->IsSwinging()))
		{
			continue;
		}
		if (FMath::Abs(Here.Z - RoofZ) > 120.0f || Here.X < Line.MinX - 60.0f || Here.X > Line.MaxX + 60.0f)
		{
			continue;
		}
		const float Side = FMath::Sign(Here.Y - Line.LineY);
		const float Clearance = Side == Line.SweepSign ? SweepSideClearance : FarSideClearance;
		const float Gap = FMath::Abs(Here.Y - Line.LineY);
		const bool bApproaching = FMath::Abs(Ahead.Y - Line.LineY) < Gap || Side != FMath::Sign(Ahead.Y - Line.LineY);
		if (!bApproaching || Gap > Clearance + Step)
		{
			continue;
		}
		if (Gap > Clearance)
		{
			// 아직 몇 걸음 남았다. 걷되 문선 앞에서 멈춘다.
			return true;
		}
		// 문 앞이다.
		PlayBody(Manager, Manager.IdleAnim, 1.0f);
		const FVector Handle = Door->GetLatchSoundLocation();
		TurnToward(Manager, Handle, DeltaSeconds, 6.0f);
		if (Door->IsSwinging())
		{
			return false;
		}
		if (KeysOutAt < 0.0)
		{
			KeysOutAt = Now;
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateKeyRingJingle(this, Door == AnnexGate.Get()),
				Handle - FVector(0.0f, 0.0f, 25.0f),
				0.55f, 1.0f, 120.0f, 1300.0f, EIGAudioBus::World);
			return false;
		}
		if (Now - KeysOutAt < KeysToSwingSeconds)
		{
			return false;
		}
		if (Door->IsLatched())
		{
			// 안에서 빗장이 걸렸다. 그녀가 안에 있다. 두드리고 기다린다.
			if (Now - LastDoorKnockAt >= DoorKnockInterval)
			{
				LastDoorKnockAt = Now;
				++DoorKnocks;
				Door->PlayHandleRattle();
				if (DoorKnocks == 2)
				{
					AIGHorrorHUD::PushDialogue(
						this,
						NSLOCTEXT("IGMissingFloor", "MokHansooName", "목한수"),
						NSLOCTEXT("IGMissingFloor", "SiteCheckBolted", "안에 누구 있어요? 문 좀 열어 봐요."),
						EIGDialogueChannel::Conversation,
						0.0f,
						EIGDialoguePriority::Story);
				}
			}
			return false;
		}
		Door->BeginScriptedSwing(true, true, false);
		*Line.bOpened = true;
		KeysOutAt = -1.0;
		UE_LOG(LogTemp, Display, TEXT("SITECHECK gate_open %s"), *Door->GetName());
		return false;
	}
	return true;
}

bool AIGSiteCheckVisit::Walk(FWalker& Walker, const float DeltaSeconds, const bool bHold)
{
	if (Walker.Travelled >= RouteLength(Walker))
	{
		Walker.bWalking = false;
		return true;
	}
	if (bHold || IsPlayerInTheWay(Walker))
	{
		Walker.bWalking = false;
		PlayBody(Walker, Walker.IdleAnim, 1.0f);
		return false;
	}
	FVector Direction = FVector::ForwardVector;
	bool bOnStair = false;
	RoutePoint(Walker, Walker.Travelled, &Direction, &bOnStair);
	const float Speed = bOnStair ? IGSiteCheckVisit::StairSpeed : IGSiteCheckVisit::FlatSpeed;
	Walker.Travelled = FMath::Min(Walker.Travelled + Speed * DeltaSeconds, RouteLength(Walker));
	const FVector At = RoutePoint(Walker, Walker.Travelled, &Direction, &bOnStair);
	Walker.Feet->SetWorldLocation(At);
	if (Direction.SizeSquared2D() > KINDA_SMALL_NUMBER)
	{
		Turn(Walker, Direction.GetSafeNormal2D().Rotation().Yaw, DeltaSeconds, 6.0f);
	}
	const float Rate = bOnStair
		? IGSiteCheckVisit::StairStepsPerSecond * Walker.HalfCycleSeconds
		: IGSiteCheckVisit::FlatSpeed / Walker.CycleSpeed;
	PlayBody(Walker, Walker.WalkAnim, Rate);
	Walker.bWalking = true;
	if (IGWalker::AdvanceStepPhase(Walker.StepPhase, DeltaSeconds, Rate, Walker.HalfCycleSeconds))
	{
		PlayStep(Walker, bOnStair);
	}
	return Walker.Travelled >= RouteLength(Walker);
}

void AIGSiteCheckVisit::PlayStep(FWalker& Walker, const bool bOnStair)
{
	const FVector Feet = Walker.Feet->GetComponentLocation();
	// 밟은 표면을 본다. 옥상 계단은 철판, 옥상은 방수층, 5층은 석고 분진이다. 구둣발이라
	// 운동화보다 조금 크다.
	const EIGFootstepSurface Surface = IGWalker::FindSurface(GetWorld(), Feet, this);
	IGWalker::PlayFootstep(
		this, Surface, Walker.StepSeed, Feet, Surface == EIGFootstepSurface::MetalStair ? 0.42f : 0.34f,
		0.95f, 0.8f, 0.55f);
	++StepCount;
	// 5층 바닥의 분진에는 신발 자국이 남는다. 다른 바닥은 분진 영역이 알아서 거른다.
	if (UIGDustSubsystem* Dust = GetWorld()->GetSubsystem<UIGDustSubsystem>())
	{
		const FRotator Facing = Walker.Feet->GetComponentRotation();
		const FVector Side = FRotationMatrix(Facing).GetUnitAxis(EAxis::Y) * ((StepCount % 2 == 0) ? 9.0f : -9.0f);
		Dust->ReportSettledPrint(Feet + Side, Facing.Yaw, EIGDustPrintKind::Footfall);
	}
	// 철판 계단을 오르는 구둣발이 처음 들리고 몸은 안 보이면 한 번 자막을 단다.
	if (Stage == EStage::Climbing && bOnStair && Surface == EIGFootstepSurface::MetalStair && !bCaptionedSteps
		&& !IGOnScreen::WasRecentlySeen(Walker.Body, 0.2f))
	{
		FVector PlayerFeet;
		if (GetPlayerFeet(PlayerFeet) && FVector::Dist(PlayerFeet, Feet) < 1100.0f)
		{
			bCaptionedSteps = true;
			AIGHorrorHUD::PushAudioCaptionAt(this,
				NSLOCTEXT("IGMissingFloor", "SiteCheckStepsCaption", "철계단을 오르는 두 사람의 발소리"),
				2.8f, Feet + FVector(0.0f, 0.0f, 10.0f));
		}
	}
}

void AIGSiteCheckVisit::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetWorld()->GetTimeSeconds();
	switch (Stage)
	{
	case EStage::Climbing: TickClimbing(DeltaSeconds, Now); break;
	case EStage::Waiting: TickWaiting(DeltaSeconds, Now); break;
	case EStage::Knocking: TickKnocking(DeltaSeconds, Now); break;
	case EStage::Talking: TickTalking(DeltaSeconds, Now); break;
	case EStage::Lingering: TickLingering(DeltaSeconds, Now); break;
	case EStage::Leaving: TickLeaving(DeltaSeconds, Now); break;
	case EStage::Gone:
		if (RestoreGates())
		{
			SetActorTickEnabled(false);
		}
		break;
	default:
		break;
	}
}

void AIGSiteCheckVisit::TickClimbing(const float DeltaSeconds, const double Now)
{
	using namespace IGSiteCheckVisit;
	const bool bManagerFree = OpenGateAhead(DeltaSeconds, Now);
	const bool bManagerArrived = Walk(Manager, DeltaSeconds, !bManagerFree);
	if (bManagerArrived)
	{
		Turn(Manager, (AnnexDoorway - GetManagerFeet()).Rotation().Yaw, DeltaSeconds, 3.0f);
	}
	// 경찰관은 관리인 뒤를 따른다. 관리인이 제자리에 서면 제 길을 간다. 닫힌 문은 기다린다.
	bool bOfficerHold = !bManagerArrived && FVector::Dist(GetOfficerFeet(), GetManagerFeet()) < FollowGap
		&& Officer.Travelled < Manager.Travelled + 200.0f;
	const FVector OfficerAhead = RoutePoint(Officer, FMath::Min(Officer.Travelled + 60.0f, RouteLength(Officer)));
	for (const TWeakObjectPtr<AIGSwingDoor>& Gate : {StairGate, AnnexGate})
	{
		const AIGSwingDoor* Door = Gate.Get();
		if (Door && !(Door->IsOpen() && !Door->IsSwinging())
			&& FVector::Dist2D(OfficerAhead, Door->GetLatchSoundLocation()) < 90.0f
			&& FMath::Abs(OfficerAhead.Z - Door->GetActorLocation().Z) < 150.0f)
		{
			bOfficerHold = true;
		}
	}
	const bool bOfficerArrived = Walk(Officer, DeltaSeconds, bOfficerHold);
	if (bOfficerArrived)
	{
		Turn(Officer, 0.0f, DeltaSeconds, 3.0f);
	}
	if (bManagerArrived && bOfficerArrived)
	{
		PlayBody(Manager, Manager.IdleAnim, 1.0f);
		PlayBody(Officer, Officer.IdleAnim, 1.0f);
		UE_LOG(LogTemp, Display, TEXT("SITECHECK arrived seconds=%.1f steps=%d"), Now - StageStartedAt, StepCount);
		Stage = EStage::Waiting;
		StageStartedAt = Now;
		if (bHurry)
		{
			BeginKnocking(Now);
		}
	}
}

void AIGSiteCheckVisit::TickWaiting(const float DeltaSeconds, const double Now)
{
	using namespace IGSiteCheckVisit;
	Turn(Officer, 0.0f, DeltaSeconds, 3.0f);
	FVector PlayerFeet;
	const bool bWithHer = IsPlayerWithThem();
	// 관리인은 문간을 본다. 그녀가 들어오면 그녀를 본다. 벽 쪽은 보지 않는다.
	if (bWithHer && GetPlayerFeet(PlayerFeet))
	{
		TurnToward(Manager, PlayerFeet, DeltaSeconds, 2.5f);
	}
	else
	{
		Turn(Manager, (AnnexDoorway - ManagerSpot).Rotation().Yaw, DeltaSeconds, 2.5f);
	}
	if (bWithHer)
	{
		if (WithHerSince < 0.0)
		{
			WithHerSince = Now;
		}
		if (Now - WithHerSince >= WithHerLeadSeconds)
		{
			BeginKnocking(Now);
		}
		return;
	}
	WithHerSince = -1.0;
	if (!IsPlayerOnRoof())
	{
		WaitedAlone += DeltaSeconds;
	}
	if (bHurry || WaitedAlone >= WaitAloneSeconds)
	{
		BeginKnocking(Now);
	}
}

void AIGSiteCheckVisit::BeginKnocking(const double Now)
{
	Stage = EStage::Knocking;
	StageStartedAt = Now;
	KnockCount = 0;
	bCaptionedKnock = false;
	Place(Officer, IGSiteCheckVisit::OfficerSpot, 0.0f);
	Officer.Travelled = RouteLength(Officer);
	PlayBody(Officer, KnockAnim, 1.0f, false);
	UE_LOG(LogTemp, Display, TEXT("SITECHECK knock with_her=%d hurry=%d waited_alone=%.0f"),
		IsPlayerWithThem() ? 1 : 0, bHurry ? 1 : 0, WaitedAlone);
}

void AIGSiteCheckVisit::PlayWallKnock(const int32 Index)
{
	// 밤에 그녀가 두드리던 것과 같은 석고 녹음이다. 칸의 차이는 벽이 대답하는 꼬리에만 있다.
	// 가운데 칸이라 속이 빈 울림이 길게 남는다. 그 뒤에는 아무것도 오지 않는다.
	const FVector At = IGSiteCheckVisit::KnockPoint + FVector(0.0f, 0.0f, Index * 1.5f);
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Knock_Plaster_0"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.0f); }),
		At,
		0.85f,
		0.98f + Index * 0.02f,
		160.0f,
		1500.0f,
		EIGAudioBus::World);
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateWallCavityResponse(this, true),
		At,
		0.9f,
		1.0f,
		90.0f,
		620.0f,
		EIGAudioBus::World);
	++KnockCount;
	// 그녀가 곁에 없으면 소리만 간다. 들리는 데 있으면 한 번 자막을 단다.
	FVector PlayerFeet;
	if (!bCaptionedKnock && !IsPlayerWithThem() && GetPlayerFeet(PlayerFeet)
		&& FVector::Dist(PlayerFeet, At) < 1400.0f)
	{
		bCaptionedKnock = true;
		AIGHorrorHUD::PushAudioCaptionAt(this,
			NSLOCTEXT("IGMissingFloor", "SiteCheckKnockCaption", "벽을 세 번 두드리는 소리"),
			2.6f, At);
	}
}

void AIGSiteCheckVisit::TickKnocking(const float DeltaSeconds, const double Now)
{
	using namespace IGSiteCheckVisit;
	const double Elapsed = Now - StageStartedAt;
	const double Before = Elapsed - DeltaSeconds;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Elapsed >= KnockContacts[Index] && Before < KnockContacts[Index])
		{
			PlayWallKnock(Index);
		}
	}
	if (Elapsed >= AskAt && Before < AskAt && IsPlayerWithThem())
	{
		AIGHorrorHUD::PushDialogue(
			this,
			NSLOCTEXT("IGMissingFloor", "PoliceOfficerSpeaker", "경찰관"),
			NSLOCTEXT("IGMissingFloor", "SiteCheckAsk", "계세요? 안에 누구 있어요?"),
			EIGDialogueChannel::Conversation,
			0.0f,
			EIGDialoguePriority::Story);
	}
	if (Elapsed >= ListenAt && Before < ListenAt)
	{
		PlayBody(Officer, ListenAnim, 1.0f, false);
	}
	// 관리인은 문간이나 그녀를 본다.
	FVector PlayerFeet;
	if (IsPlayerWithThem() && GetPlayerFeet(PlayerFeet))
	{
		TurnToward(Manager, PlayerFeet, DeltaSeconds, 2.0f);
	}
	if (Elapsed >= CheckedAt)
	{
		FinishChecked(Now);
	}
}

void AIGSiteCheckVisit::FinishChecked(const double Now)
{
	bWitnessed = IsPlayerWithThem();
	PlayBody(Officer, Officer.IdleAnim, 1.0f);
	UE_LOG(LogTemp, Display, TEXT("SITECHECK checked witnessed=%d knocks=%d"), bWitnessed ? 1 : 0, KnockCount);
	OnChecked.Broadcast(this, bWitnessed);
	if (!bWitnessed)
	{
		// 아무도 안 보는 5층이다. 그녀가 안 볼 때 거두고, 결과는 문자로 간다.
		Stage = EStage::Lingering;
		StageStartedAt = Now;
		return;
	}
	Stage = EStage::Talking;
	StageStartedAt = Now;
	LineIndex = 0;
	NextLineAt = Now + 0.6;
	LineDeadlineAt = Now + 6.0;
}

float AIGSiteCheckVisit::PushLine(const int32 Index)
{
	const FText OfficerName = NSLOCTEXT("IGMissingFloor", "PoliceOfficerSpeaker", "경찰관");
	const FText Mok = NSLOCTEXT("IGMissingFloor", "MokHansooName", "목한수");
	const FText Yudam = NSLOCTEXT("IGMissingFloor", "YudamCounterSpeaker", "백유담");
	FText Speaker;
	FText Line;
	switch (Index)
	{
	case 0:
		Speaker = OfficerName;
		Line = NSLOCTEXT("IGMissingFloor", "SiteCheckNoAnswer", "속이 비긴 했는데, 지금은 안에서 아무 반응이 없네요.");
		break;
	case 1:
		Speaker = Yudam;
		Line = NSLOCTEXT("IGMissingFloor", "SiteCheckYudamNight", "밤에는 대답했어요. 제가 두드리면 똑같이요.");
		break;
	case 2:
		Speaker = Mok;
		Line = NSLOCTEXT("IGMissingFloor", "SiteCheckMokPipes", "창고라니까요. 밤이면 물탱크 배관이 울려요.");
		break;
	case 3:
		Speaker = OfficerName;
		Line = NSLOCTEXT("IGMissingFloor", "SiteCheckWarrant", "보내 주신 자료는 담당 팀에서 보고 있어요. 벽은 저희가 다시 와서 볼 테니까, 그때까지 손대지 마세요.");
		break;
	case 4:
		Speaker = OfficerName;
		Line = NSLOCTEXT("IGMissingFloor", "SiteCheckCallUs", "다시 소리 나면 바로 연락 주세요. 저희는 사진 몇 장만 더 찍고 내려갈게요.");
		break;
	default:
		return 0.0f;
	}
	AIGHorrorHUD::PushDialogue(this, Speaker, Line, EIGDialogueChannel::Conversation, 0.0f, EIGDialoguePriority::Story);
	// 다음 줄은 이 줄이 화면에서 내려가면 민다. 자막을 오래 두는 설정이면 그만큼 늦다.
	float CaptionScale = 1.0f;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UIGAccessibilitySubsystem* Accessibility = GameInstance->GetSubsystem<UIGAccessibilitySubsystem>())
		{
			CaptionScale = Accessibility->GetCaptionDurationScale();
		}
	}
	return AIGHorrorHUD::EstimateDialogueSeconds(Line) * CaptionScale;
}

void AIGSiteCheckVisit::TickTalking(const float DeltaSeconds, const double Now)
{
	using namespace IGSiteCheckVisit;
	constexpr int32 LineCount = 5;
	if (!IsPlayerOnRoof())
	{
		// 말하는 중에 내려갔다. 남은 말은 접는다.
		Stage = EStage::Lingering;
		StageStartedAt = Now;
		return;
	}
	FVector PlayerFeet;
	const bool bHasPlayer = GetPlayerFeet(PlayerFeet);
	if (bHasPlayer)
	{
		TurnToward(Officer, PlayerFeet, DeltaSeconds, 2.5f);
	}
	// 관리인은 제 말을 할 때만 경찰관을 본다. 벽은 끝까지 안 본다.
	if (LineIndex == 3 && Now < LineDeadlineAt)
	{
		TurnToward(Manager, GetOfficerFeet(), DeltaSeconds, 3.0f);
	}
	else if (bHasPlayer)
	{
		TurnToward(Manager, PlayerFeet, DeltaSeconds, 2.0f);
	}
	// 앞의 말이 내려가면 다음 말을 민다. 줄이 끝내 안 비면(다른 말이 끼었다) 넉넉히 기다린 뒤 민다.
	const bool bLaneReady = Now >= NextLineAt && (IsDialogueLaneIdle() || Now >= LineDeadlineAt);
	if (LineIndex < LineCount)
	{
		if (bLaneReady)
		{
			const float Seconds = PushLine(LineIndex++);
			NextLineAt = Now + LineGapSeconds;
			LineDeadlineAt = Now + Seconds + LineLaneSlackSeconds;
		}
		return;
	}
	if (!bThoughtPushed && bLaneReady)
	{
		bThoughtPushed = true;
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT("IGMissingFloor", "SiteCheckMokThought", "아저씨는 벽 쪽은 쳐다보지도 않네."),
			3.2f);
		Stage = EStage::Lingering;
		StageStartedAt = Now;
		ShutterCount = 0;
	}
}

void AIGSiteCheckVisit::TickLingering(const float DeltaSeconds, const double Now)
{
	using namespace IGSiteCheckVisit;
	const double Elapsed = Now - StageStartedAt;
	// 경찰관은 벽을 찍는다. 관리인은 문간을 본다.
	Turn(Officer, 0.0f, DeltaSeconds, 2.0f);
	Turn(Manager, (AnnexDoorway - ManagerSpot).Rotation().Yaw, DeltaSeconds, 2.0f);
	if (bWitnessed && ShutterCount < 2 && Elapsed >= ShutterTimes[ShutterCount])
	{
		++ShutterCount;
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateCameraShutterTriple(this),
			GetOfficerFeet() + FVector(18.0f, 0.0f, 150.0f),
			0.32f, 1.0f, 80.0f, 900.0f, EIGAudioBus::World);
	}
	// 그녀가 옥상을 떠났다. 5층은 아래에서 안 보인다.
	if (!IsPlayerOnRoof() && !IGOnScreen::WasRecentlySeen(Officer.Body, 0.3f) && !IGOnScreen::WasRecentlySeen(Manager.Body, 0.3f))
	{
		Withdraw();
		return;
	}
	if (Elapsed >= LingerSeconds)
	{
		BeginLeaving(Now);
	}
}

void AIGSiteCheckVisit::BeginLeaving(const double Now)
{
	using namespace IGSiteCheckVisit;
	// 올라온 길을 거꾸로 걷는다. 관리인이 앞서고(열쇠를 쥐었다) 경찰관이 따른다.
	TArray<FVector> Climb;
	AppendClimb(Climb);
	TArray<FVector> Points;
	Points.Add(GetManagerFeet());
	for (int32 Index = Climb.Num() - 1; Index >= 0; --Index)
	{
		Points.Add(Climb[Index]);
	}
	Points.Add(FVector(-290.0f, -300.0f, FourthFloorZ()));
	// 계단탑 출입구에서 3층까지. 한 층의 길은 [아래층 출입구, …, 위층 출입구]다.
	TArray<FVector> Down;
	AIGPrologueWorldScene::GetStairClimbFeet(2, Down);
	for (int32 Index = Down.Num() - 1; Index >= 0; --Index)
	{
		Points.Add(Down[Index]);
	}
	SetRoute(Manager, Points);
	Points[0] = GetOfficerFeet();
	Points.Insert(FVector(150.0f, 620.0f, RoofZ), 1);
	SetRoute(Officer, Points);
	Stage = EStage::Leaving;
	StageStartedAt = Now;
	Manager.LastSeenAt = Now;
	Officer.LastSeenAt = Now;
	UE_LOG(LogTemp, Display, TEXT("SITECHECK leave"));
}

void AIGSiteCheckVisit::TickLeaving(const float DeltaSeconds, const double Now)
{
	using namespace IGSiteCheckVisit;
	if (Manager.bShown)
	{
		const bool bFree = OpenGateAhead(DeltaSeconds, Now);
		Walk(Manager, DeltaSeconds, !bFree);
	}
	if (Officer.bShown)
	{
		const bool bHold = Manager.bShown && FVector::Dist(GetOfficerFeet(), GetManagerFeet()) < FollowGap
			&& Manager.Travelled < RouteLength(Manager);
		Walk(Officer, DeltaSeconds, bHold);
	}
	// 그녀 눈에서 벗어난 몸은 그 자리에서 거둔다. 길 끝에 닿았으면 안 보일 때 거둔다.
	FVector PlayerFeet;
	const bool bHasPlayer = GetPlayerFeet(PlayerFeet);
	for (FWalker* Walker : {&Manager, &Officer})
	{
		if (!Walker->bShown)
		{
			continue;
		}
		if (IGOnScreen::WasRecentlySeen(Walker->Body, 0.25f))
		{
			Walker->LastSeenAt = Now;
			continue;
		}
		const bool bNear = bHasPlayer && FVector::Dist(PlayerFeet, Walker->Feet->GetComponentLocation()) < 400.0f;
		if (!bNear && Now - Walker->LastSeenAt >= UnseenVanishSeconds)
		{
			Show(*Walker, false);
		}
	}
	if (!Manager.bShown && !Officer.bShown)
	{
		Withdraw();
	}
}
