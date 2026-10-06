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
	// 바닥까지 늘어진 팔. 손이 앞으로 나와 있어서 팔도 아래로 갈수록 앞과 바깥으로 기운다.
	AddPart(TEXT("Lower"), FigureRoot, Cylinder, FVector(0.0f, 0.0f, 52.0f), FRotator::ZeroRotator, FVector(0.46f, 0.38f, 1.05f));
	AddPart(TEXT("Torso"), Spine, Cylinder, FVector(0.0f, 0.0f, 33.0f), FRotator::ZeroRotator, FVector(0.36f, 0.26f, 0.62f));
	AddPart(TEXT("Neck"), Spine, Cylinder, FVector(3.0f, 0.0f, 74.0f), FRotator(-8.0f, 0.0f, 0.0f), FVector(0.07f, 0.07f, 0.24f));
	AddPart(TEXT("Head"), Spine, Sphere, FVector(7.0f, 0.0f, 92.0f), FRotator::ZeroRotator, FVector(0.20f, 0.18f, 0.25f));
	AddPart(TEXT("ArmL"), Spine, Cylinder, FVector(6.0f, -22.0f, -2.0f), FRotator(6.0f, 0.0f, 4.0f), FVector(0.065f, 0.065f, 1.26f));
	AddPart(TEXT("ArmR"), Spine, Cylinder, FVector(6.0f, 22.0f, -2.0f), FRotator(6.0f, 0.0f, -4.0f), FVector(0.065f, 0.065f, 1.26f));
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

void AIGShadowFigure::DressAsPaper()
{
	if (bPaper)
	{
		return;
	}
	bPaper = true;
	auto Load = [](const TCHAR* Path) -> UMaterialInterface*
	{
		return LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn);
	};
	// 몸통은 누렇게 바랜 종이가 겹겹이다.
	if (UMaterialInterface* OldPaper = Load(TEXT("/Game/Prototype/Materials/M_PaperOld.M_PaperOld")))
	{
		ShadowMaterial = OldPaper;
		for (UStaticMeshComponent* Part : Parts)
		{
			if (Part)
			{
				Part->SetMaterial(0, OldPaper);
			}
		}
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"), nullptr, LOAD_NoWarn);
	if (!Cube)
	{
		return;
	}
	// 현관문에 붙던 것들. 같은 종이가 두 번 붙지 않게 돌려 쓴다.
	UMaterialInterface* const Prints[] = {
		Load(TEXT("/Game/Prototype/Materials/MI_DoorPrints401.MI_DoorPrints401")),
		Load(TEXT("/Game/Prototype/Materials/MI_ShippingLabels.MI_ShippingLabels")),
		Load(TEXT("/Game/Prototype/Materials/M_UtilityMeterLabel.M_UtilityMeterLabel")),
		Load(TEXT("/Game/Prototype/Materials/MI_RentalNoticeA4.MI_RentalNoticeA4")),
		Load(TEXT("/Game/Prototype/Materials/M_LobbyContactNotice.M_LobbyContactNotice")),
		Load(TEXT("/Game/Prototype/Materials/MI_DoorPrints402.MI_DoorPrints402")),
		Load(TEXT("/Game/Prototype/Materials/M_LobbyWaterNotice.M_LobbyWaterNotice")),
		Load(TEXT("/Game/Prototype/Materials/M_BoothReceipts.M_BoothReceipts")),
		Load(TEXT("/Game/Prototype/Materials/M_PaperClean.M_PaperClean")),
		Load(TEXT("/Game/Prototype/Materials/M_LobbyForumPrint.M_LobbyForumPrint"))};
	struct FSheet
	{
		bool bOnSpine;
		FVector Location;
		FRotator Rotation;
		FVector2D SizeCm;
	};
	// 기본 몸(2 m) 기준 자리. 허리 위는 Spine 좌표(허리 Z 100이 원점)다. 몸통이
	// 타원 기둥이라 판판한 종이는 가운데만 닿고 가장자리가 들뜬다. 자리는 낱장의
	// 어느 점도 몸 부품 안으로 들어가지 않게 잡았다(다 자라 숙인 자세까지).
	// 손에 붙은 두 장은 손끝에서 바닥까지 늘어진 영수증이다.
	const FSheet Layout[] = {
		{false, FVector(24.0f, -6.5f, 70.5f), FRotator(4.0f, -10.0f, 9.0f), FVector2D(21.0f, 29.7f)},
		{false, FVector(21.0f, 12.5f, 37.5f), FRotator(-6.0f, 25.0f, -14.0f), FVector2D(15.0f, 21.0f)},
		{false, FVector(-24.5f, 2.0f, 60.0f), FRotator(3.0f, 180.0f, 6.0f), FVector2D(21.0f, 29.7f)},
		{false, FVector(-6.0f, 20.0f, 48.0f), FRotator(0.0f, 90.0f, -10.0f), FVector2D(18.0f, 24.0f)},
		{false, FVector(-6.0f, -21.0f, 22.5f), FRotator(5.0f, -90.0f, 12.0f), FVector2D(21.0f, 29.7f)},
		{false, FVector(25.0f, -1.5f, 11.0f), FRotator(-8.0f, 4.0f, 3.0f), FVector2D(12.0f, 9.0f)},
		{true, FVector(19.5f, 2.0f, 30.0f), FRotator(2.0f, 0.0f, -7.0f), FVector2D(15.0f, 21.0f)},
		{true, FVector(14.5f, -10.0f, 52.0f), FRotator(-4.0f, -35.0f, 16.0f), FVector2D(10.0f, 7.0f)},
		{true, FVector(-19.0f, 0.0f, 38.0f), FRotator(0.0f, 180.0f, 4.0f), FVector2D(15.0f, 21.0f)},
		{true, FVector(-4.0f, 14.0f, 24.0f), FRotator(0.0f, 90.0f, 8.0f), FVector2D(14.0f, 20.0f)},
		{true, FVector(14.0f, -26.0f, -87.0f), FRotator(0.0f, 0.0f, 2.0f), FVector2D(8.0f, 26.0f)},
		{true, FVector(14.0f, 26.0f, -83.0f), FRotator(0.0f, 0.0f, -3.0f), FVector2D(7.0f, 18.0f)},
		{true, FVector(18.5f, 4.0f, 58.0f), FRotator(0.0f, 8.0f, 3.0f), FVector2D(9.0f, 6.0f)}};
	int32 Index = 0;
	for (const FSheet& Sheet : Layout)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(
			this, *FString::Printf(TEXT("PaperSheet_%d"), Index));
		Component->SetupAttachment(Sheet.bOnSpine ? Spine.Get() : FigureRoot.Get());
		Component->SetStaticMesh(Cube);
		// 원통 몸 앞의 얇은 낱장. 큐브는 100 cm다.
		Component->SetRelativeLocation(Sheet.Location);
		Component->SetRelativeRotation(Sheet.Rotation);
		Component->SetRelativeScale3D(FVector(0.004f, Sheet.SizeCm.X / 100.0f, Sheet.SizeCm.Y / 100.0f));
		if (UMaterialInterface* Print = Prints[Index % UE_ARRAY_COUNT(Prints)])
		{
			Component->SetMaterial(0, Print);
		}
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(false);
		Component->RegisterComponent();
		Sheets.Add(Component);
		++Index;
	}
	// 얼굴 자리에는 403호 문에 붙어 있던 통닭집 자석이 붙는다.
	if (UStaticMesh* Magnet = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Meshes/SM_DoorDeliveryMagnet.SM_DoorDeliveryMagnet"), nullptr, LOAD_NoWarn))
	{
		UStaticMeshComponent* Face = NewObject<UStaticMeshComponent>(this, TEXT("PaperFace"));
		Face->SetupAttachment(Spine.Get());
		Face->SetStaticMesh(Magnet);
		Face->SetRelativeLocation(FVector(17.5f, 0.0f, 92.0f));
		// 자석의 앞면은 -Y다. 몸의 앞(+X)을 보게 돌린다.
		Face->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
		// 9×6.5 cm 자석을 얼굴만 하게 키운다.
		Face->SetRelativeScale3D(FVector(1.8f));
		Face->SetMobility(EComponentMobility::Movable);
		Face->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Face->SetCanEverAffectNavigation(false);
		Face->SetCastShadow(false);
		Face->RegisterComponent();
		Sheets.Add(Face);
	}
	SetActorHiddenInGame(!bManifested);
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
