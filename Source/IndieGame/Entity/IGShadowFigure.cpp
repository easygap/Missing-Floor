#include "Entity/IGShadowFigure.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace IGShadow
{
	// 크기 0일 때와 1일 때의 배율. 기본 몸은 2 m라 1.1 m에서 2.9 m까지 자란다.
	constexpr float MinScale = 0.55f;
	constexpr float MaxScale = 1.45f;
	// 다 자라면 상체가 이만큼 앞으로 숙어진다(도). 보는 사람 위로 덮어 온다.
	constexpr float MaxLeanDegrees = 28.0f;
	// 몸이 목표 크기와 방향을 따라가는 빠르기.
	constexpr float GrowthFollowSpeed = 3.0f;
	constexpr float YawFollowSpeed = 2.4f;
	// 보는 동안의 떨림. 크게 자랄수록 커진다.
	constexpr float TrembleDegrees = 1.6f;
	constexpr float TrembleHz = 7.0f;
}

AIGShadowFigure::AIGShadowFigure()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	FigureRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FigureRoot"));
	SetRootComponent(FigureRoot);
	FigureRoot->SetMobility(EComponentMobility::Movable);

	Spine = CreateDefaultSubobject<USceneComponent>(TEXT("Spine"));
	Spine->SetupAttachment(FigureRoot);
	Spine->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cylinder = CylinderFinder.Object;
	UStaticMesh* Sphere = SphereFinder.Object;

	// 기본 몸 2 m. 원통·구는 100 cm 크기에 원점이 가운데다.
	// 허리 아래는 발이 보이지 않는 통짜, 그 위로 좁은 몸통, 너무 긴 목과 작은 머리,
	// 바닥까지 늘어진 팔.
	AddPart(TEXT("Lower"), FigureRoot, Cylinder, FVector(0.0f, 0.0f, 52.0f), FRotator::ZeroRotator, FVector(0.46f, 0.38f, 1.05f));
	AddPart(TEXT("Torso"), Spine, Cylinder, FVector(0.0f, 0.0f, 33.0f), FRotator::ZeroRotator, FVector(0.36f, 0.26f, 0.62f));
	AddPart(TEXT("Neck"), Spine, Cylinder, FVector(3.0f, 0.0f, 74.0f), FRotator(-8.0f, 0.0f, 0.0f), FVector(0.07f, 0.07f, 0.24f));
	AddPart(TEXT("Head"), Spine, Sphere, FVector(7.0f, 0.0f, 92.0f), FRotator::ZeroRotator, FVector(0.20f, 0.18f, 0.25f));
	AddPart(TEXT("ArmL"), Spine, Cylinder, FVector(6.0f, -22.0f, -2.0f), FRotator(-6.0f, 0.0f, -4.0f), FVector(0.065f, 0.065f, 1.26f));
	AddPart(TEXT("ArmR"), Spine, Cylinder, FVector(6.0f, 22.0f, -2.0f), FRotator(-6.0f, 0.0f, 4.0f), FVector(0.065f, 0.065f, 1.26f));
	AddPart(TEXT("HandL"), Spine, Sphere, FVector(12.0f, -26.0f, -66.0f), FRotator::ZeroRotator, FVector(0.07f, 0.05f, 0.17f));
	AddPart(TEXT("HandR"), Spine, Sphere, FVector(12.0f, 26.0f, -66.0f), FRotator::ZeroRotator, FVector(0.07f, 0.05f, 0.17f));

	SetActorHiddenInGame(true);
}

UStaticMeshComponent* AIGShadowFigure::AddPart(
	const TCHAR* Name,
	USceneComponent* Parent,
	UStaticMesh* Mesh,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& Scale)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(Parent);
	Part->SetStaticMesh(Mesh);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeRotation(Rotation);
	Part->SetRelativeScale3D(Scale);
	Part->SetMobility(EComponentMobility::Movable);
	// 몸은 아무것도 막지 않는다. 시선 추적도 지나간다(손님은 문간을 막지 않는다).
	Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(true);
	Parts.Add(Part);
	return Part;
}

void AIGShadowFigure::Manifest(const FVector& Location, const float YawDegrees, const float Growth)
{
	if (!ShadowMaterial)
	{
		// 젖은 검정. 손전등에 비치면 가장자리만 희미하게 되쏜다.
		ShadowMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Prototype/Materials/M_PlasticDark.M_PlasticDark"), nullptr, LOAD_NoWarn);
		for (UStaticMeshComponent* Part : Parts)
		{
			if (Part && ShadowMaterial)
			{
				Part->SetMaterial(0, ShadowMaterial);
			}
		}
	}
	bManifested = true;
	TargetLocation = Location;
	MoveSpeed = 0.0f;
	TargetYaw = YawDegrees;
	TargetGrowth = FMath::Clamp(Growth, 0.0f, 1.0f);
	CurrentGrowth = TargetGrowth;
	SetActorLocationAndRotation(Location, FRotator(0.0f, YawDegrees, 0.0f));
	ApplyPose();
	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);
}

void AIGShadowFigure::Vanish()
{
	bManifested = false;
	bTrembling = false;
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);
}

void AIGShadowFigure::SetTargetLocation(const FVector& Location, const float SpeedCmPerSecond)
{
	TargetLocation = Location;
	MoveSpeed = FMath::Max(0.0f, SpeedCmPerSecond);
}

FVector AIGShadowFigure::GetChestLocation() const
{
	const float Scale = FMath::Lerp(IGShadow::MinScale, IGShadow::MaxScale, CurrentGrowth);
	return GetActorLocation() + FVector(0.0f, 0.0f, 135.0f * Scale);
}

void AIGShadowFigure::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bManifested)
	{
		SetActorTickEnabled(false);
		return;
	}
	CurrentGrowth = FMath::FInterpTo(CurrentGrowth, TargetGrowth, DeltaSeconds, IGShadow::GrowthFollowSpeed);
	if (MoveSpeed > 0.0f)
	{
		SetActorLocation(FMath::VInterpConstantTo(GetActorLocation(), TargetLocation, DeltaSeconds, MoveSpeed));
	}
	const float Yaw = FMath::FInterpTo(
		GetActorRotation().Yaw,
		GetActorRotation().Yaw + FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, TargetYaw),
		DeltaSeconds,
		IGShadow::YawFollowSpeed);
	SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
	TremblePhase += DeltaSeconds * IGShadow::TrembleHz * 2.0f * UE_PI;
	ApplyPose();
}

void AIGShadowFigure::ApplyPose()
{
	const float Scale = FMath::Lerp(IGShadow::MinScale, IGShadow::MaxScale, CurrentGrowth);
	FigureRoot->SetRelativeScale3D(FVector(Scale));
	const float Tremble = bTrembling
		? IGShadow::TrembleDegrees * CurrentGrowth * FMath::Sin(TremblePhase)
		: 0.0f;
	Spine->SetRelativeRotation(FRotator(-IGShadow::MaxLeanDegrees * CurrentGrowth, 0.0f, Tremble));
}
