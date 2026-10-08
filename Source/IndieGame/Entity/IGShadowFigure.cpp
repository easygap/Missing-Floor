#include "Entity/IGShadowFigure.h"

#include "AnimationRuntime.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "IndieGame.h"
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
	// 어둑시니의 빚은 몸(rig_eoduksini.py). 허리에서 숙이는 spine과 어깨에 매달린 두 팔.
	const TCHAR* const SculptedBodyPath = TEXT("/Game/Meshes/SK_Eoduksini.SK_Eoduksini");
	// 숙이는 동안 늘어진 팔이 앞으로 나오는 비율. 다 숙이면 손끝이 6도쯤 앞으로 온다.
	constexpr float ArmReachRatio = 0.2f;
	// 다 숙이면 머리가 쉬는 자세의 경계 상자보다 50 cm 앞으로 나간다. 상자를 그만큼
	// 키워 둬야 숙인 머리만 화면에 걸릴 때 몸이 통째로 컬링되지 않는다.
	constexpr float SculptedBoundsScale = 2.8f;

	FName SpineBone()
	{
		static const FName Name(TEXT("spine"));
		return Name;
	}

	FName ArmBone(const int32 Side)
	{
		static const FName Names[2] = {FName(TEXT("arm_l")), FName(TEXT("arm_r"))};
		return Names[Side];
	}
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
	// 붙는 것은 이 건물 사람이면 다 본 종이다. 공동현관 옆 「원룸 있습니다」 전단, 입주민
	// 안내문, 누렇게 바랜 빈 종이. 장부·영수증·운송장처럼 이야기의 단서가 적힌 종이는
	// 쓰지 않는다. 손님은 오빠와 상관없는 이 건물의 것이고, 단서가 엉뚱한 데서 보이면
	// 단서가 아니게 된다.
	UStaticMesh* Notice = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_RentalNoticeA4.SM_RentalNoticeA4"), nullptr, LOAD_NoWarn);
	enum class ESheetKind : uint8
	{
		RentalNotice,
		ResidentNotice,
		OldPaper,
		CleanPaper
	};
	UMaterialInterface* const ResidentNotice =
		Load(TEXT("/Game/Prototype/Materials/M_LobbyContactNotice.M_LobbyContactNotice"));
	UMaterialInterface* const CleanPaper = Load(TEXT("/Game/Prototype/Materials/M_PaperClean.M_PaperClean"));
	struct FSheet
	{
		bool bOnSpine;
		FVector Location;
		FRotator Rotation;
		FVector2D SizeCm;
		ESheetKind Kind;
	};
	// 기본 몸(2 m) 기준 자리. 허리 위는 Spine 좌표(허리 Z 100이 원점)다. 몸통이
	// 타원 기둥이라 판판한 종이는 가운데만 닿고 가장자리가 들뜬다. 자리는 낱장의
	// 어느 점도 몸 부품 안으로 들어가지 않게 잡았다(다 자라 숙인 자세까지).
	// 손에 붙은 두 장은 손끝에서 바닥까지 늘어진 긴 종잇조각이다.
	const FSheet Layout[] = {
		{false, FVector(24.0f, -6.5f, 70.5f), FRotator(4.0f, -10.0f, 9.0f), FVector2D(21.0f, 29.7f), ESheetKind::RentalNotice},
		{false, FVector(21.0f, 12.5f, 37.5f), FRotator(-6.0f, 25.0f, -14.0f), FVector2D(15.0f, 21.0f), ESheetKind::ResidentNotice},
		{false, FVector(-24.5f, 2.0f, 60.0f), FRotator(3.0f, 180.0f, 6.0f), FVector2D(21.0f, 29.7f), ESheetKind::RentalNotice},
		{false, FVector(-6.0f, 20.0f, 48.0f), FRotator(0.0f, 90.0f, -10.0f), FVector2D(18.0f, 24.0f), ESheetKind::OldPaper},
		{false, FVector(-6.0f, -21.0f, 22.5f), FRotator(5.0f, -90.0f, 12.0f), FVector2D(21.0f, 29.7f), ESheetKind::RentalNotice},
		{false, FVector(25.0f, -1.5f, 11.0f), FRotator(-8.0f, 4.0f, 3.0f), FVector2D(12.0f, 9.0f), ESheetKind::CleanPaper},
		{true, FVector(19.5f, 2.0f, 30.0f), FRotator(2.0f, 0.0f, -7.0f), FVector2D(15.0f, 21.0f), ESheetKind::RentalNotice},
		{true, FVector(14.5f, -10.0f, 52.0f), FRotator(-4.0f, -35.0f, 16.0f), FVector2D(10.0f, 7.0f), ESheetKind::CleanPaper},
		{true, FVector(-19.0f, 0.0f, 38.0f), FRotator(0.0f, 180.0f, 4.0f), FVector2D(15.0f, 21.0f), ESheetKind::ResidentNotice},
		{true, FVector(-4.0f, 14.0f, 24.0f), FRotator(0.0f, 90.0f, 8.0f), FVector2D(14.0f, 20.0f), ESheetKind::OldPaper},
		{true, FVector(14.0f, -26.0f, -87.0f), FRotator(0.0f, 0.0f, 2.0f), FVector2D(8.0f, 26.0f), ESheetKind::CleanPaper},
		{true, FVector(14.0f, 26.0f, -83.0f), FRotator(0.0f, 0.0f, -3.0f), FVector2D(7.0f, 18.0f), ESheetKind::CleanPaper},
		{true, FVector(18.5f, 4.0f, 58.0f), FRotator(0.0f, 8.0f, 3.0f), FVector2D(9.0f, 6.0f), ESheetKind::OldPaper}};
	int32 Index = 0;
	for (const FSheet& Sheet : Layout)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(
			this, *FString::Printf(TEXT("PaperSheet_%d"), Index));
		Component->SetupAttachment(Sheet.bOnSpine ? Spine.Get() : FigureRoot.Get());
		Component->SetRelativeLocation(Sheet.Location);
		if (Sheet.Kind == ESheetKind::RentalNotice && Notice)
		{
			// 전단 메시는 21×29.7 cm, 인쇄면이 -Y다. 먼저 90도 돌려 인쇄면을 배치 표의
			// 바깥(+X)으로 맞춘 뒤 표의 기울기를 얹는다.
			Component->SetStaticMesh(Notice);
			Component->SetRelativeRotation(FQuat(Sheet.Rotation) * FQuat(FRotator(0.0f, 90.0f, 0.0f)));
			Component->SetRelativeScale3D(FVector(Sheet.SizeCm.X / 21.0f, 1.0f, Sheet.SizeCm.Y / 29.7f));
		}
		else
		{
			// 원통 몸 앞의 얇은 낱장. 큐브는 100 cm다.
			Component->SetStaticMesh(Cube);
			Component->SetRelativeRotation(Sheet.Rotation);
			Component->SetRelativeScale3D(FVector(0.004f, Sheet.SizeCm.X / 100.0f, Sheet.SizeCm.Y / 100.0f));
			UMaterialInterface* Print = Sheet.Kind == ESheetKind::ResidentNotice ? ResidentNotice
				: Sheet.Kind == ESheetKind::OldPaper ? ShadowMaterial.Get()
				: CleanPaper;
			if (Print)
			{
				Component->SetMaterial(0, Print);
			}
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

void AIGShadowFigure::DressAsEoduksini()
{
	if (SculptedBody || bPaper)
	{
		return;
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, IGShadow::SculptedBodyPath, nullptr, LOAD_NoWarn);
	if (!Mesh)
	{
		UE_LOG(LogIndieGame, Warning, TEXT("SK_Eoduksini missing; eoduksini keeps the primitive outline"));
		return;
	}
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	const int32 SpineIndex = Skeleton.FindBoneIndex(IGShadow::SpineBone());
	const int32 ArmIndex[2] = {Skeleton.FindBoneIndex(IGShadow::ArmBone(0)), Skeleton.FindBoneIndex(IGShadow::ArmBone(1))};
	if (SpineIndex == INDEX_NONE || ArmIndex[0] == INDEX_NONE || ArmIndex[1] == INDEX_NONE)
	{
		UE_LOG(LogIndieGame, Warning, TEXT("SK_Eoduksini lacks spine/arm bones; eoduksini keeps the primitive outline"));
		return;
	}
	SpineRest = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, SpineIndex);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		ArmRest[Side] = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, ArmIndex[Side]);
	}

	UPoseableMeshComponent* Body = NewObject<UPoseableMeshComponent>(this, TEXT("SculptedBody"));
	Body->SetupAttachment(FigureRoot.Get());
	Body->SetSkinnedAssetAndUpdate(Mesh);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);
	Body->SetCastShadow(true);
	Body->SetBoundsScale(IGShadow::SculptedBoundsScale);
	Body->RegisterComponent();
	// 배우 틱이 자세를 정한 다음에 뼈가 갱신되어야 한 프레임 늦지 않는다.
	Body->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
	Body->SetComponentTickEnabled(bManifested);
	SculptedBody = Body;
	// 도형 윤곽은 숨긴다. 시선과 숨소리는 배우 자리 기준이라 그대로 맞는다.
	for (UStaticMeshComponent* Part : Parts)
	{
		if (Part)
		{
			Part->SetVisibility(false);
		}
	}
	ApplyPose();
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
	if (SculptedBody)
	{
		SculptedBody->SetComponentTickEnabled(true);
	}
}

void AIGShadowFigure::Vanish()
{
	bManifested = false;
	bTrembling = false;
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);
	if (SculptedBody)
	{
		SculptedBody->SetComponentTickEnabled(false);
	}
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
	if (SculptedBody)
	{
		PoseSculptedBody(IGShadow::MaxLeanDegrees * CurrentGrowth, Tremble);
	}
}

void AIGShadowFigure::PoseSculptedBody(const float LeanDegrees, const float TrembleDegrees)
{
	// 허리 뼈의 머리를 축으로 상체를 앞(+X)으로 숙인다. 도형 몸의 Spine과 같은 회전이다.
	const FQuat Lean(FRotator(-LeanDegrees, 0.0f, TrembleDegrees));
	const FVector Pivot = SpineRest.GetLocation();
	FTransform Waist = SpineRest;
	Waist.SetRotation(Lean * SpineRest.GetRotation());
	SculptedBody->SetBoneTransformByName(IGShadow::SpineBone(), Waist, EBoneSpaces::ComponentSpace);
	// 팔은 숙인 어깨를 따라 나오되 상체만큼 돌지 않고 아래로 늘어진다. 손끝만 조금 앞으로 온다.
	// 아래로 향한 팔은 피치를 올려야 손끝이 앞으로 나온다.
	const FQuat Reach(FRotator(LeanDegrees * IGShadow::ArmReachRatio, 0.0f, TrembleDegrees));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FTransform Arm = ArmRest[Side];
		Arm.SetLocation(Pivot + Lean.RotateVector(ArmRest[Side].GetLocation() - Pivot));
		Arm.SetRotation(Reach * ArmRest[Side].GetRotation());
		SculptedBody->SetBoneTransformByName(IGShadow::ArmBone(Side), Arm, EBoneSpaces::ComponentSpace);
	}
}
