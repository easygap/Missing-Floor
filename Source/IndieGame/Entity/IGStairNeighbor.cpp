#include "Entity/IGStairNeighbor.h"

#include "Animation/AnimSequence.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/IGFireDoorWedge.h"
#include "Interaction/IGSwingDoor.h"
#include "Kismet/GameplayStatics.h"
#include "Player/IGHorrorHUD.h"

namespace IGStairNeighbor
{
	// 걸음. 출근길이라 복도에서는 보통 걸음이고, 계단은 디딤판 한 칸에 한 걸음이다.
	// 계단 길은 디딤판 윗면 가운데를 잇는 경사라, 한 단 26.7 cm를 0.67초에 간다.
	constexpr float FlatSpeed = 100.0f;
	constexpr float StairSpeed = 40.0f;
	/** 이보다 높이가 많이 바뀌는 구간은 계단이다. */
	constexpr float StairSegmentRise = 30.0f;

	// 리깅한 몸의 걸음 주기. Scripts/blender/rig_walker.py의 resident Walk(32프레임,
	// 두 걸음 50 cm씩)와 같다.
	constexpr float WalkCycleSeconds = 32.0f / 30.0f;
	constexpr float HalfCycleSeconds = WalkCycleSeconds * 0.5f;
	constexpr float WalkCycleSpeed = 100.0f / WalkCycleSeconds;
	/** 계단에서는 초당 한 걸음 반. 보폭이 짧아 동작을 조금 늦춘다. */
	constexpr float StairAnimRate = 1.5f * HalfCycleSeconds;

	// 마주침. 그녀가 보이는 거리 안에서 서로 가린 것이 없으면 멈춰 선다.
	constexpr float MeetDistance = 430.0f;
	constexpr float MeetMaxHeight = 260.0f;
	constexpr double SightCheckSeconds = 0.15;
	/** 머리 높이. 160 cm 몸이다. */
	constexpr float HeadHeight = 150.0f;

	/** 계단 목에서 이만큼 서 있다가 그냥 내려간다. */
	constexpr double WaitSeconds = 90.0;
	/** 다 내려간 뒤 그녀 눈에 아직 보이면 이만큼 더 걷는다. */
	constexpr float ExitWalkMaxSeconds = 3.0f;
	/** 디렉터가 떠나라는 말을 잊어도 이만큼 서 있으면 내려간다. */
	constexpr double TalkLimitSeconds = 30.0;

	/** 길 앞 이 거리 안에 그녀가 서 있으면 비켜 줄 때까지 선다. */
	constexpr float YieldLookAhead = 55.0f;
	constexpr float YieldRadius = 60.0f;

	constexpr float CapsuleRadius = 24.0f;
	constexpr float CapsuleHalfHeight = 80.0f;

	/** 닫힌 방화문을 손으로 미는 소리 크기. 관리인보다 조심스럽다. */
	constexpr float FireDoorOpenLoudness = 0.12f;
}

AIGStairNeighbor::AIGStairNeighbor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	FeetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Feet"));
	SetRootComponent(FeetRoot);
	FeetRoot->SetMobility(EComponentMobility::Movable);

	// 플레이어 몸만 막는다. 시야와 상호작용 선은 지나간다.
	Blocker = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(FeetRoot);
	Blocker->InitCapsuleSize(IGStairNeighbor::CapsuleRadius, IGStairNeighbor::CapsuleHalfHeight);
	Blocker->SetRelativeLocation(FVector(0.0f, 0.0f, IGStairNeighbor::CapsuleHalfHeight));
	Blocker->SetCollisionObjectType(ECC_WorldDynamic);
	Blocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	Blocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Blocker->SetCanEverAffectNavigation(false);

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(FeetRoot);
	// 리깅한 몸은 정면이 +X, 원점이 발밑 가운데다. 액터와 그대로 맞는다.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCastShadow(true);
	// 걸음 동작이 바운드 밖으로 팔과 발을 낸다.
	Body->SetBoundsScale(1.4f);
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->SetVisibility(false);

	SetActorHiddenInGame(true);
}

bool AIGStairNeighbor::LoadBody()
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(
		nullptr, TEXT("/Game/Meshes/SK_Neighbor303.SK_Neighbor303"), nullptr, LOAD_NoWarn);
	IdleAnim = LoadObject<UAnimSequence>(
		nullptr, TEXT("/Game/Meshes/A_Neighbor303_Idle.A_Neighbor303_Idle"), nullptr, LOAD_NoWarn);
	WalkAnim = LoadObject<UAnimSequence>(
		nullptr, TEXT("/Game/Meshes/A_Neighbor303_Walk.A_Neighbor303_Walk"), nullptr, LOAD_NoWarn);
	if (!Mesh || !IdleAnim || !WalkAnim)
	{
		UE_LOG(LogTemp, Warning, TEXT("SK_Neighbor303 or its animations are missing; 303 stays home"));
		return false;
	}
	Body->SetSkeletalMesh(Mesh);
	return true;
}

FVector AIGStairNeighbor::GetStartFeet()
{
	// 303호 문(X 78, 문짝 바깥면 Y -237) 앞 반 걸음.
	return FVector(78.0f, -290.0f, AIGPrologueWorldScene::GetStoreyFloorZ(2));
}

FVector AIGStairNeighbor::GetWaitingFeet()
{
	// 4층 계단 목 기둥 안쪽에서 한 뼘 더 복도 쪽. 403호 문까지 4 m 남짓이다.
	return AIGPrologueWorldScene::GetStairDoorwayFeet(3) + FVector(26.0f, 0.0f, 0.0f);
}

bool AIGStairNeighbor::CanWatchThirdFloorDoor(const FVector& PlayerFeet)
{
	// 3층 참이나 복도에 서 있으면 303호 문 앞이 보일 수 있다. 계단 위에서는 벽에 가린다.
	return FMath::Abs(PlayerFeet.Z - AIGPrologueWorldScene::GetStoreyFloorZ(2)) < 150.0f
		&& !AIGPrologueWorldScene::IsOnStairFlight(PlayerFeet);
}

void AIGStairNeighbor::SetRoute(const TArray<FVector>& Points)
{
	Route = Points;
	RouteDistance.Reset();
	float Total = 0.0f;
	RouteDistance.Add(0.0f);
	for (int32 Index = 1; Index < Route.Num(); ++Index)
	{
		Total += FVector::Dist(Route[Index - 1], Route[Index]);
		RouteDistance.Add(Total);
	}
	Travelled = 0.0f;
}

FVector AIGStairNeighbor::RoutePoint(const float Distance, FVector* OutDirection, bool* bOutOnStair) const
{
	if (Route.IsEmpty())
	{
		return GetActorLocation();
	}
	if (Route.Num() == 1)
	{
		return Route[0];
	}
	int32 Segment = Route.Num() - 2;
	for (int32 Index = 0; Index + 1 < Route.Num(); ++Index)
	{
		if (Distance <= RouteDistance[Index + 1])
		{
			Segment = Index;
			break;
		}
	}
	const FVector& From = Route[Segment];
	const FVector& To = Route[Segment + 1];
	const float Length = RouteDistance[Segment + 1] - RouteDistance[Segment];
	const float Alpha = Length > KINDA_SMALL_NUMBER
		? FMath::Clamp((Distance - RouteDistance[Segment]) / Length, 0.0f, 1.0f)
		: 1.0f;
	if (OutDirection)
	{
		*OutDirection = (To - From).GetSafeNormal();
	}
	if (bOutOnStair)
	{
		*bOutOnStair = FMath::Abs(To.Z - From.Z) > IGStairNeighbor::StairSegmentRise;
	}
	return FMath::Lerp(From, To, Alpha);
}

void AIGStairNeighbor::BeginArrival()
{
	if (Stage != EStage::Hidden || !Body->GetSkeletalMeshAsset())
	{
		return;
	}
	// 303호 앞 → 3층 계단 목 → 계단 → 4층 계단 목 → 403호 쪽을 보고 서는 자리.
	TArray<FVector> Points;
	Points.Add(GetStartFeet());
	TArray<FVector> Climb;
	AIGPrologueWorldScene::GetStairClimbFeet(2, Climb);
	Points.Append(Climb);
	Points.Add(GetWaitingFeet());
	SetRoute(Points);
	ArrivalRoute = Route;
	ArrivalDistance = RouteDistance;
	ArrivalTravelled = 0.0f;
	FVector Direction = FVector::ForwardVector;
	SetActorLocationAndRotation(RoutePoint(0.0f, &Direction), FRotator(0.0f, Direction.GetSafeNormal2D().Rotation().Yaw, 0.0f));
	MetAt = -1.0;
	bCaptionedSteps = false;
	StepPhase = 0.0f;
	Stage = EStage::Arriving;
	StageStartedAt = GetWorld()->GetTimeSeconds();
	Show(true);
	PlayBody(WalkAnim, IGStairNeighbor::FlatSpeed / IGStairNeighbor::WalkCycleSpeed);
	SetActorTickEnabled(true);
	UE_LOG(LogTemp, Display, TEXT("NEIGHBOR303 arrive route=%.0f"), GetRouteLength());
}

void AIGStairNeighbor::Leave()
{
	if (Stage == EStage::Hidden || Stage == EStage::Gone || Stage == EStage::Leaving)
	{
		return;
	}
	// 올라온 길의 어느 구간에 서 있는지 보고 거기서부터 거꾸로 걷는다. 303호 앞 복도에서
	// 멈췄다면 3층 계단 목으로 바로 간다.
	TArray<FVector> Points;
	Points.Add(GetActorLocation());
	int32 Segment = 0;
	for (int32 Index = 0; Index + 1 < ArrivalDistance.Num(); ++Index)
	{
		if (ArrivalTravelled >= ArrivalDistance[Index])
		{
			Segment = Index;
		}
	}
	for (int32 Index = FMath::Max(Segment, 1); Index >= 1; --Index)
	{
		Points.Add(ArrivalRoute[Index]);
	}
	// 3층 계단 목에서 1층 출입구까지. 한 층의 길은 [아래층 출입구, …, 위층 출입구]다.
	for (int32 Floor = 1; Floor >= 0; --Floor)
	{
		TArray<FVector> Climb;
		AIGPrologueWorldScene::GetStairClimbFeet(Floor, Climb);
		for (int32 Index = Climb.Num() - 2; Index >= 0; --Index)
		{
			Points.Add(Climb[Index]);
		}
	}
	// 계단탑을 나와 주차장 칸을 지나 연결통로 쪽으로. 관리인이 도는 길과 같다.
	Points.Add(FVector(-230.0f, -295.0f, 0.0f));
	Points.Add(FVector(-40.0f, -300.0f, 0.0f));
	SetRoute(Points);
	ExitWalkSeconds = 0.0f;
	Stage = EStage::Leaving;
	StageStartedAt = GetWorld()->GetTimeSeconds();
	SetActorTickEnabled(true);
	UE_LOG(LogTemp, Display, TEXT("NEIGHBOR303 leave route=%.0f from_segment=%d"), GetRouteLength(), Segment);
}

void AIGStairNeighbor::Vanish()
{
	if (Stage == EStage::Hidden || Stage == EStage::Gone)
	{
		return;
	}
	Stage = EStage::Gone;
	Show(false);
	SetActorTickEnabled(false);
	UE_LOG(LogTemp, Display, TEXT("NEIGHBOR303 vanish"));
}

void AIGStairNeighbor::Show(const bool bVisible)
{
	SetActorHiddenInGame(!bVisible);
	Body->SetVisibility(bVisible);
	Blocker->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (!bVisible)
	{
		Body->Stop();
		ActiveAnim = nullptr;
	}
}

void AIGStairNeighbor::PlayBody(UAnimSequence* Sequence, const float Rate)
{
	if (!Sequence)
	{
		return;
	}
	if (ActiveAnim != Sequence)
	{
		Body->PlayAnimation(Sequence, true);
		ActiveAnim = Sequence;
	}
	Body->SetPlayRate(Rate);
}

void AIGStairNeighbor::TurnTowards(const float TargetYaw, const float DeltaSeconds, const float Speed)
{
	FRotator Rotation = GetActorRotation();
	const float Delta = FMath::FindDeltaAngleDegrees(Rotation.Yaw, TargetYaw);
	Rotation.Yaw += Delta * FMath::Clamp(DeltaSeconds * Speed, 0.0f, 1.0f);
	Rotation.Pitch = 0.0f;
	Rotation.Roll = 0.0f;
	SetActorRotation(Rotation);
}

FVector AIGStairNeighbor::GetPlayerEyes(const APawn& Pawn) const
{
	if (const APlayerController* PC = Cast<APlayerController>(Pawn.GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			return PC->PlayerCameraManager->GetCameraLocation();
		}
	}
	return Pawn.GetPawnViewLocation();
}

bool AIGStairNeighbor::GetPlayerFeet(FVector& OutFeet) const
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

bool AIGStairNeighbor::CanSeePlayer() const
{
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return false;
	}
	const FVector Eyes = GetPlayerEyes(*Pawn);
	const FVector Head = GetActorLocation() + FVector(0.0f, 0.0f, IGStairNeighbor::HeadHeight);
	if (FVector::Dist(Eyes, Head) > IGStairNeighbor::MeetDistance
		|| FMath::Abs(Eyes.Z - Head.Z) > IGStairNeighbor::MeetMaxHeight)
	{
		return false;
	}
	// 벽이나 닫힌 문이 가리면 서로 못 본다. 가까워도 마찬가지다.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IGStairNeighborSight), false, this);
	Query.AddIgnoredActor(Pawn);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Head, Eyes, ECC_Visibility, Query);
}

bool AIGStairNeighbor::CheckMeet(const double Now)
{
	if (Now - LastSightCheckAt < IGStairNeighbor::SightCheckSeconds)
	{
		return false;
	}
	LastSightCheckAt = Now;
	if (!CanSeePlayer())
	{
		return false;
	}
	if (Stage == EStage::Arriving)
	{
		ArrivalTravelled = Travelled;
	}
	Stage = EStage::Talking;
	MetAt = Now;
	StageStartedAt = Now;
	PlayBody(IdleAnim, 1.0f);
	UE_LOG(LogTemp, Display, TEXT("NEIGHBOR303 met at=(%.0f,%.0f,%.0f) travelled=%.0f"),
		GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, ArrivalTravelled);
	OnMet.Broadcast(this);
	return true;
}

bool AIGStairNeighbor::IsPlayerInTheWay() const
{
	FVector PlayerFeet;
	if (!GetPlayerFeet(PlayerFeet))
	{
		return false;
	}
	const FVector Ahead = RoutePoint(FMath::Min(Travelled + IGStairNeighbor::YieldLookAhead, GetRouteLength()));
	return FVector::Dist2D(Ahead, PlayerFeet) < IGStairNeighbor::YieldRadius
		&& FMath::Abs(Ahead.Z - PlayerFeet.Z) < 120.0f;
}

void AIGStairNeighbor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetWorld()->GetTimeSeconds();
	switch (Stage)
	{
	case EStage::Arriving:
		if (CheckMeet(Now))
		{
			return;
		}
		Walk(DeltaSeconds);
		ArrivalTravelled = Travelled;
		if (Travelled >= GetRouteLength())
		{
			// 계단 목에 닿았다. 403호 쪽을 보고 선다.
			Stage = EStage::Waiting;
			StageStartedAt = Now;
			PlayBody(IdleAnim, 1.0f);
			UE_LOG(LogTemp, Display, TEXT("NEIGHBOR303 waiting"));
		}
		break;
	case EStage::Waiting:
		TurnTowards(0.0f, DeltaSeconds, 3.0f);
		if (CheckMeet(Now))
		{
			return;
		}
		if (Now - StageStartedAt >= IGStairNeighbor::WaitSeconds)
		{
			UE_LOG(LogTemp, Display, TEXT("NEIGHBOR303 gave up"));
			OnGaveUp.Broadcast(this);
			Leave();
		}
		break;
	case EStage::Talking:
	{
		FVector PlayerFeet;
		if (GetPlayerFeet(PlayerFeet))
		{
			const FVector ToPlayer = PlayerFeet - GetActorLocation();
			if (ToPlayer.SizeSquared2D() > 1.0f)
			{
				TurnTowards(ToPlayer.GetSafeNormal2D().Rotation().Yaw, DeltaSeconds, 3.0f);
			}
		}
		if (Now - StageStartedAt >= IGStairNeighbor::TalkLimitSeconds)
		{
			Leave();
		}
		break;
	}
	case EStage::Leaving:
		Walk(DeltaSeconds);
		if (Travelled >= GetRouteLength())
		{
			// 그녀가 아직 보고 있으면 조금 더 걸어 연결통로로 빠진다.
			if (Body->WasRecentlyRendered(0.25f) && ExitWalkSeconds < IGStairNeighbor::ExitWalkMaxSeconds)
			{
				ExitWalkSeconds += DeltaSeconds;
				const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
				SetActorLocation(GetActorLocation() + Forward * IGStairNeighbor::FlatSpeed * DeltaSeconds);
				break;
			}
			Stage = EStage::Gone;
			Show(false);
			SetActorTickEnabled(false);
			UE_LOG(LogTemp, Display, TEXT("NEIGHBOR303 gone"));
			OnGone.Broadcast(this);
		}
		break;
	default:
		break;
	}
}

void AIGStairNeighbor::Walk(const float DeltaSeconds)
{
	FVector Direction = FVector::ForwardVector;
	bool bOnStair = false;
	const FVector From = RoutePoint(Travelled, &Direction, &bOnStair);
	const FVector To = RoutePoint(FMath::Min(Travelled + 60.0f, GetRouteLength()));

	// 닫힌 방화문 앞이면 문을 밀어 열고 다 열릴 때까지 선다. 몸에 충돌이 없어 그냥 걸으면
	// 문짝을 뚫는다. 관리인과 같은 판정이다(문짝은 경첩에서 +Y로 120 cm).
	for (TActorIterator<AIGFireDoorWedge> It(GetWorld()); It; ++It)
	{
		AIGSwingDoor* Door = It->GetDoor();
		if (!Door || (Door->IsOpen() && !Door->IsSwinging()))
		{
			continue;
		}
		const FVector Hinge = Door->GetActorLocation();
		if (FMath::Abs(From.Z - Hinge.Z) > 150.0f && FMath::Abs(To.Z - Hinge.Z) > 150.0f)
		{
			continue;
		}
		const float FromSide = From.X - Hinge.X;
		const float ToSide = To.X - Hinge.X;
		bool bAhead = Door->IsSwinging() && FMath::Abs(FromSide) < 60.0f
			&& From.Y > Hinge.Y - 20.0f && From.Y < Hinge.Y + 140.0f;
		if (!bAhead && FromSide * ToSide < 0.0f)
		{
			const float CrossY = FMath::Lerp(From.Y, To.Y, FromSide / (FromSide - ToSide));
			bAhead = CrossY > Hinge.Y - 20.0f && CrossY < Hinge.Y + 140.0f;
		}
		if (bAhead)
		{
			if (Door->IsFullyClosed())
			{
				Door->BeginScriptedSwingWithLoudness(true, IGStairNeighbor::FireDoorOpenLoudness, 1.0f);
			}
			TurnTowards((Door->GetComponentsBoundingBox().GetCenter() - From).Rotation().Yaw, DeltaSeconds, 6.0f);
			PlayBody(IdleAnim, 1.0f);
			return;
		}
	}

	// 길을 막고 서 있으면 비켜 줄 때까지 선다.
	if (IsPlayerInTheWay())
	{
		PlayBody(IdleAnim, 1.0f);
		return;
	}
	const float Speed = bOnStair ? IGStairNeighbor::StairSpeed : IGStairNeighbor::FlatSpeed;
	Travelled = FMath::Min(Travelled + Speed * DeltaSeconds, GetRouteLength());
	const FVector At = RoutePoint(Travelled, &Direction, &bOnStair);
	SetActorLocation(At);
	if (Direction.SizeSquared2D() > KINDA_SMALL_NUMBER)
	{
		TurnTowards(Direction.GetSafeNormal2D().Rotation().Yaw, DeltaSeconds, 6.0f);
	}
	const float Rate = bOnStair
		? IGStairNeighbor::StairAnimRate
		: IGStairNeighbor::FlatSpeed / IGStairNeighbor::WalkCycleSpeed;
	PlayBody(WalkAnim, Rate);
	const float Before = StepPhase;
	StepPhase += DeltaSeconds * Rate / IGStairNeighbor::HalfCycleSeconds;
	if (FMath::FloorToInt(StepPhase) != FMath::FloorToInt(Before))
	{
		PlayStep(bOnStair);
	}
}

void AIGStairNeighbor::PlayStep(const bool bOnStair)
{
	const FVector Feet = GetActorLocation();
	// 밟은 표면을 본다. 계단 철판이면 쇳소리, 나머지는 운동화가 돌바닥을 딛는 소리다.
	bool bMetal = false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IGStairNeighborStep), false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(
			Hit, Feet + FVector(0.0f, 0.0f, 30.0f), Feet - FVector(0.0f, 0.0f, 40.0f), ECC_Visibility, Query)
		&& Hit.GetComponent()
		&& Hit.GetComponent()->ComponentHasTag(FName(TEXT("Footstep.MetalStair"))))
	{
		bMetal = true;
	}
	StepSeed = StepSeed * 1664525u + 1013904223u;
	USoundBase* Sound = bMetal
		? IGAudio::SampleVariantOr(TEXT("Foot_MetalStair"), 5, StepSeed,
			[this]() -> USoundBase*
			{
				return UIGToneSequenceSoundWave::CreateSurfaceFootstep(this, EIGFootstepSurface::MetalStair, 0.82f, 0.6f);
			})
		: IGAudio::SampleVariantOr(TEXT("Foot_Concrete"), 5, StepSeed,
			[this]() -> USoundBase*
			{
				return UIGToneSequenceSoundWave::CreateSurfaceFootstep(this, EIGFootstepSurface::Concrete, 0.7f, 0.5f);
			});
	IGAudio::SpawnExpendableOneShotAt(
		this, Sound, Feet + FVector(0.0f, 0.0f, 6.0f), bMetal ? 0.4f : 0.32f,
		0.97f + static_cast<float>(StepSeed % 9u) * 0.01f, 150.0f, 1500.0f, EIGAudioBus::World);
	++StepCount;
	// 올라오는 철판 발소리가 처음 들리고 몸은 아직 안 보이면 한 번 자막을 단다.
	if (Stage == EStage::Arriving && bMetal && bOnStair && !bCaptionedSteps && !Body->WasRecentlyRendered(0.2f))
	{
		FVector PlayerFeet;
		if (GetPlayerFeet(PlayerFeet) && PlayerFeet.Z > Feet.Z + 60.0f && FVector::Dist2D(PlayerFeet, Feet) < 900.0f)
		{
			bCaptionedSteps = true;
			AIGHorrorHUD::PushAudioCaptionAt(this,
				NSLOCTEXT("IGMissingFloor", "Neighbor303StepsCaption", "계단을 올라오는 발소리"),
				2.6f, Feet + FVector(0.0f, 0.0f, 10.0f));
		}
	}
}
