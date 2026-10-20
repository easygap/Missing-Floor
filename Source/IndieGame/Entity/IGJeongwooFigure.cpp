#include "Entity/IGJeongwooFigure.h"

#include "Animation/AnimSequence.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGWalkerKit.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"

namespace IGJeongwoo
{
	// rig_walker.py mover: Walk 32프레임에 두 걸음 50 cm씩, Carry 36프레임에 두 걸음 40 cm씩.
	constexpr float WalkCycleSeconds = 32.0f / 30.0f;
	constexpr float WalkCycleSpeed = 100.0f / WalkCycleSeconds;
	constexpr float CarryCycleSeconds = 36.0f / 30.0f;
	constexpr float CarryCycleSpeed = 80.0f / CarryCycleSeconds;
	/** 계단은 디딤판 한 칸에 한 걸음이다(IGWalker). 상자를 안고는 더 느리다. */
	constexpr float CapsuleRadius = 26.0f;
	/** 안은 상자(SM_StorageBox 50x38x34, 원점 바닥)의 자리. 배 앞에 대고 두 손으로 옆을 쥔다. */
	const FVector BoxHold(40.0f, 0.0f, 95.0f);
	constexpr float BoxStack = 34.2f;
}

AIGJeongwooFigure::AIGJeongwooFigure()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	FeetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Feet"));
	SetRootComponent(FeetRoot);
	FeetRoot->SetMobility(EComponentMobility::Movable);

	// 플레이어 몸만 막는다. 시야와 상호작용 선은 지나간다.
	Blocker = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(FeetRoot);
	Blocker->InitCapsuleSize(IGJeongwoo::CapsuleRadius, IGWalker::AdultCapsuleHalfHeight);
	Blocker->SetRelativeLocation(FVector(0.0f, 0.0f, IGWalker::AdultCapsuleHalfHeight));
	Blocker->SetCollisionObjectType(ECC_WorldDynamic);
	Blocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	Blocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Blocker->SetCanEverAffectNavigation(false);

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(FeetRoot);
	// 리깅한 몸은 정면이 +X, 원점이 발밑 가운데다.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCastShadow(true);
	Body->SetBoundsScale(1.5f);
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->SetVisibility(false);

	SetActorHiddenInGame(true);
}

bool AIGJeongwooFigure::LoadBody()
{
	if (bBodyLoaded)
	{
		return true;
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(
		nullptr, TEXT("/Game/Meshes/SK_Jeongwoo.SK_Jeongwoo"), nullptr, LOAD_NoWarn);
	const auto Load = [](const TCHAR* Name) -> UAnimSequence*
	{
		return LoadObject<UAnimSequence>(
			nullptr, *FString::Printf(TEXT("/Game/Meshes/A_Jeongwoo_%s.A_Jeongwoo_%s"), Name, Name),
			nullptr, LOAD_NoWarn);
	};
	const struct { EPose Pose; const TCHAR* Name; } Table[] = {
		{EPose::Idle, TEXT("Idle")}, {EPose::Walk, TEXT("Walk")}, {EPose::Carry, TEXT("Carry")},
		{EPose::CarryIdle, TEXT("CarryIdle")}, {EPose::Crouch, TEXT("Crouch")}, {EPose::Brace, TEXT("Brace")},
		{EPose::HoldDoor, TEXT("HoldDoor")}, {EPose::LookBack, TEXT("LookBack")}, {EPose::Hold, TEXT("Hold")},
	};
	for (const auto& Entry : Table)
	{
		if (UAnimSequence* Anim = Load(Entry.Name))
		{
			Anims.Add(static_cast<uint8>(Entry.Pose), Anim);
		}
	}
	if (!Mesh || !Anims.Contains(static_cast<uint8>(EPose::Idle)) || !Anims.Contains(static_cast<uint8>(EPose::Walk)))
	{
		UE_LOG(LogTemp, Warning, TEXT("SK_Jeongwoo or its animations are missing; Jeongwoo stays a voice"));
		return false;
	}
	Body->SetSkeletalMesh(Mesh);
	if (UStaticMesh* BoxMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Meshes/SM_StorageBox.SM_StorageBox"), nullptr, LOAD_NoWarn))
	{
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UStaticMeshComponent* Box = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("HeldBox%d"), Index));
			Box->SetupAttachment(FeetRoot);
			Box->SetStaticMesh(BoxMesh);
			Box->SetMobility(EComponentMobility::Movable);
			Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Box->SetCanEverAffectNavigation(false);
			Box->SetCastShadow(true);
			Box->SetRelativeLocation(IGJeongwoo::BoxHold + FVector(0.0f, 0.0f, IGJeongwoo::BoxStack * Index));
			Box->SetRelativeRotation(FRotator(0.0f, Index == 0 ? 0.0f : 6.0f, 0.0f));
			Box->SetVisibility(false);
			Box->RegisterComponent();
			Boxes.Add(Box);
		}
	}
	bBodyLoaded = true;
	return true;
}

void AIGJeongwooFigure::Appear(const FVector& Feet, const float Yaw, const EPose Pose)
{
	if (!LoadBody())
	{
		return;
	}
	bWalking = false;
	bTurning = false;
	TurnToYaw.Reset();
	Route = FIGWalkerRoute();
	SetActorLocationAndRotation(Feet, FRotator(0.0f, Yaw, 0.0f));
	SetActorHiddenInGame(false);
	Body->SetVisibility(true);
	bShown = true;
	SetCarriedBoxes(CarriedBoxes);
	SetPose(Pose);
	SetActorTickEnabled(false);
	RefreshBlocker();
}

void AIGJeongwooFigure::Vanish()
{
	bWalking = false;
	bTurning = false;
	TurnToYaw.Reset();
	bShown = false;
	SetActorTickEnabled(false);
	Body->SetVisibility(false);
	for (UStaticMeshComponent* Box : Boxes)
	{
		Box->SetVisibility(false);
	}
	Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	bBlockerDeferred = false;
	SetActorHiddenInGame(true);
}

void AIGJeongwooFigure::SetBlocksPlayer(const bool bBlocks)
{
	bBlocksPlayer = bBlocks;
	if (bShown)
	{
		RefreshBlocker();
	}
}

void AIGJeongwooFigure::RefreshBlocker()
{
	bBlockerDeferred = false;
	if (!bShown || !bBlocksPlayer || bWalking || bTurning)
	{
		Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}
	const ACharacter* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const UCapsuleComponent* PlayerCapsule = Player ? Player->GetCapsuleComponent() : nullptr;
	if (PlayerCapsule)
	{
		const FVector Delta = PlayerCapsule->GetComponentLocation() - Blocker->GetComponentLocation();
		const float Reach = Blocker->GetScaledCapsuleRadius() + PlayerCapsule->GetScaledCapsuleRadius();
		const float Height = Blocker->GetScaledCapsuleHalfHeight() + PlayerCapsule->GetScaledCapsuleHalfHeight();
		if (Delta.SizeSquared2D() < FMath::Square(Reach) && FMath::Abs(Delta.Z) < Height)
		{
			Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			bBlockerDeferred = true;
			SetActorTickEnabled(true);
			return;
		}
	}
	Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void AIGJeongwooFigure::SetCarriedBoxes(const int32 Count)
{
	CarriedBoxes = FMath::Clamp(Count, 0, 2);
	for (int32 Index = 0; Index < Boxes.Num(); ++Index)
	{
		Boxes[Index]->SetVisibility(bShown && Index < CarriedBoxes);
	}
}

void AIGJeongwooFigure::WalkRoute(const TArray<FVector>& Points, const bool bCarry, const EPose EndPose, const float Speed,
	const TOptional<float> ArriveYaw)
{
	bTurning = false;
	TurnToYaw = ArriveYaw;
	if (!bShown || Points.Num() < 2)
	{
		if (!Points.IsEmpty() && bShown)
		{
			SetActorLocation(Points.Last());
		}
		if (ArriveYaw.IsSet())
		{
			FaceYaw(*ArriveYaw);
		}
		TurnToYaw.Reset();
		SetPose(EndPose);
		OnArrived.Broadcast();
		return;
	}
	Route.StairRise = IGWalker::StairSegmentRise;
	Route.Set(Points);
	Travelled = 0.0f;
	WalkSpeed = Speed;
	bCarryWalk = bCarry;
	ArrivePose = EndPose;
	bWalking = true;
	StepPhase = 0.0f;
	RefreshBlocker();
	SetActorTickEnabled(true);
}

void AIGJeongwooFigure::SetPose(const EPose Pose)
{
	CurrentPose = Pose;
	PlayPose(Pose, 1.0f);
}

void AIGJeongwooFigure::FaceYaw(const float Yaw)
{
	SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
}

void AIGJeongwooFigure::FaceTowards(const FVector& Where)
{
	const FVector Delta = Where - GetActorLocation();
	if (Delta.SizeSquared2D() > 1.0f)
	{
		FaceYaw(Delta.GetSafeNormal2D().Rotation().Yaw);
	}
}

UAnimSequence* AIGJeongwooFigure::AnimFor(const EPose Pose) const
{
	if (const TObjectPtr<UAnimSequence>* Found = Anims.Find(static_cast<uint8>(Pose)))
	{
		return Found->Get();
	}
	// 없는 동작은 가장 가까운 것으로 대신한다.
	const EPose Fallback = (Pose == EPose::Carry) ? EPose::Walk : EPose::Idle;
	const TObjectPtr<UAnimSequence>* Found = Anims.Find(static_cast<uint8>(Fallback));
	return Found ? Found->Get() : nullptr;
}

void AIGJeongwooFigure::PlayPose(const EPose Pose, const float Rate)
{
	UAnimSequence* Sequence = AnimFor(Pose);
	if (!Sequence)
	{
		return;
	}
	if (ActiveAnim != Sequence)
	{
		Body->PlayAnimation(Sequence, Pose != EPose::LookBack);
		ActiveAnim = Sequence;
	}
	Body->SetPlayRate(Rate);
}

void AIGJeongwooFigure::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bTurning)
	{
		// 다 걸은 자리에서 돌아선다. 발을 옮기며 돌도록 걷기를 느리게 튼다.
		const float TargetYaw = TurnToYaw.Get(GetActorRotation().Yaw);
		const float Yaw = IGWalker::TurnYaw(GetActorRotation().Yaw, TargetYaw, DeltaSeconds, 6.0f);
		SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
		PlayPose(EPose::Walk, 0.5f);
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, TargetYaw)) < 2.0f)
		{
			SetActorRotation(FRotator(0.0f, TargetYaw, 0.0f));
			FinishArrival();
		}
		return;
	}
	if (!bWalking)
	{
		if (bBlockerDeferred)
		{
			RefreshBlocker();
			if (bBlockerDeferred)
			{
				return;
			}
		}
		SetActorTickEnabled(false);
		return;
	}
	FVector Direction = FVector::ForwardVector;
	bool bOnStair = false;
	Route.At(Travelled, GetActorLocation(), &Direction, &bOnStair);
	const float Step = (bOnStair ? IGWalker::StairWalkSpeed * (bCarryWalk ? 0.8f : 1.0f) : WalkSpeed) * DeltaSeconds;
	Travelled = FMath::Min(Travelled + Step, Route.Length());
	const FVector At = Route.At(Travelled, GetActorLocation(), &Direction, &bOnStair);
	SetActorLocation(At);
	if (Direction.SizeSquared2D() > KINDA_SMALL_NUMBER)
	{
		const float Yaw = IGWalker::TurnYaw(GetActorRotation().Yaw, Direction.GetSafeNormal2D().Rotation().Yaw, DeltaSeconds, 6.0f);
		SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
	}
	const float CycleSpeed = bCarryWalk ? IGJeongwoo::CarryCycleSpeed : IGJeongwoo::WalkCycleSpeed;
	const float HalfCycle = (bCarryWalk ? IGJeongwoo::CarryCycleSeconds : IGJeongwoo::WalkCycleSeconds) * 0.5f;
	const float Rate = FMath::Clamp((Step / FMath::Max(DeltaSeconds, 1.0e-4f)) / CycleSpeed, 0.4f, 1.6f);
	PlayPose(bCarryWalk ? EPose::Carry : EPose::Walk, Rate);
	if (IGWalker::AdvanceStepPhase(StepPhase, DeltaSeconds, Rate, HalfCycle))
	{
		PlayStep();
	}
	if (Travelled >= Route.Length() - 0.5f)
	{
		bWalking = false;
		if (TurnToYaw.IsSet() && FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, *TurnToYaw)) > 2.0f)
		{
			bTurning = true;
			return;
		}
		FinishArrival();
	}
}

void AIGJeongwooFigure::FinishArrival()
{
	bWalking = false;
	bTurning = false;
	TurnToYaw.Reset();
	SetActorTickEnabled(false);
	SetPose(ArrivePose);
	RefreshBlocker();
	OnArrived.Broadcast();
}

void AIGJeongwooFigure::PlayStep()
{
	const FVector Feet = GetActorLocation();
	const bool bMetal = IGWalker::FindSurface(GetWorld(), Feet, this) == EIGFootstepSurface::MetalStair;
	// 배달 기사의 부츠. 운동화보다 무겁다.
	IGWalker::PlayFootstep(
		this,
		bMetal ? EIGFootstepSurface::MetalStair : EIGFootstepSurface::Concrete,
		StepSeed,
		Feet,
		bMetal ? 0.46f : 0.36f,
		0.9f,
		bMetal ? 0.78f : 0.66f,
		bMetal ? 0.62f : 0.52f);
	++StepCount;
}
