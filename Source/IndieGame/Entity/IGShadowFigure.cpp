#include "Entity/IGShadowFigure.h"

#include "AnimationRuntime.h"
#include "Components/MeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "IndieGame.h"
#include "Kismet/GameplayStatics.h"
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
	// 종이 손님의 빚은 몸(rig_eoduksini.py --keep-color). 뼈 이름은 어둑시니와 같다.
	const TCHAR* const PaperBodyPath = TEXT("/Game/Meshes/SK_PaperGuest.SK_PaperGuest");
	// 그 몸의 얼굴 앞(컴포넌트 공간, cm). 머리 앞면이 X 11.5, 얼굴 가운데가 188 cm다.
	const FVector PaperFaceLocation(11.8f, 0.0f, 188.0f);
	// 숙이는 동안 늘어진 팔이 앞으로 나오는 비율. 다 숙이면 손끝이 6도쯤 앞으로 온다.
	constexpr float ArmReachRatio = 0.2f;
	// 다 숙이면 머리가 쉬는 자세의 경계 상자보다 50 cm 앞으로 나간다. 상자를 그만큼
	// 키워 둬야 숙인 머리만 화면에 걸릴 때 몸이 통째로 컬링되지 않는다.
	constexpr float SculptedBoundsScale = 2.8f;
	// 배율 1일 때의 키와, 빚은 몸이 없을 때 쓰는 허리 높이(cm).
	constexpr float RestHeight = 200.0f;
	constexpr float RestWaist = 100.0f;
	// 머리를 천장에서 이만큼 띄운다.
	constexpr float CeilingGap = 6.0f;
	// 움직일 때 벽에 닿는지 보는 몸통 공(배율 1, cm). 무릎 공은 문턱과 계단 한 단을 넘는 높이에 둔다.
	constexpr float SlideRadius = 22.0f;
	constexpr float StepClearance = 20.0f;
	constexpr float ChestHeight = 130.0f;
	// 손님 발밑 바닥을 찾는 둘레(배율 1, cm)와 위아래로 찾는 범위. 둘레는 바닥에 끌리는 종이 자락 끝이다.
	// 403호 현관 화강석 단(8 cm)을 넘을 때 자락이 단 속에 묻혔다.
	constexpr float FootprintRadius = 30.0f;
	constexpr float FloorProbeReach = 20.0f;
	// 바닥 높이를 따라 오르내리는 빠르기(cm/s). 오를 때는 자락이 묻히기 전에 올라선다.
	constexpr float FloorRiseSpeed = 120.0f;
	constexpr float FloorSettleSpeed = 60.0f;

	// 보이는 메시에 처음 닿는 자리. 숨은 막과 트리거는 빼고 다시 굴린다.
	bool SweepVisible(UWorld& World, const FVector& From, const FVector& To, const float Radius,
		FCollisionQueryParams Params, FHitResult& OutHit)
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
		for (int32 Try = 0; Try < 4; ++Try)
		{
			const bool bHit = Radius > 0.0f
				? World.SweepSingleByObjectType(OutHit, From, To, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params)
				: World.LineTraceSingleByObjectType(OutHit, From, To, Objects, Params);
			if (!bHit)
			{
				return false;
			}
			const UPrimitiveComponent* Component = OutHit.GetComponent();
			if (Cast<UMeshComponent>(Component) && Component->IsVisible() && !Component->bHiddenInGame)
			{
				return true;
			}
			Params.AddIgnoredComponent(Component);
		}
		return false;
	}

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
	bFollowFloor = true;
	ClipAudit.Start(TEXT("guest"));
	// 빚은 몸이 있으면 그것을 입히고 얼굴에 403호 문의 자석만 붙인다. 손님은 오빠와
	// 상관없는 이 건물의 것이라, 몸의 종이에는 단서가 적힌 종이가 없다.
	if (AttachSculptedBody(IGShadow::PaperBodyPath))
	{
		if (UStaticMesh* Magnet = LoadObject<UStaticMesh>(
				nullptr, TEXT("/Game/Meshes/SM_DoorDeliveryMagnet.SM_DoorDeliveryMagnet"), nullptr, LOAD_NoWarn))
		{
			UStaticMeshComponent* Face = NewObject<UStaticMeshComponent>(this, TEXT("PaperFace"));
			Face->SetupAttachment(SculptedBody.Get());
			Face->SetStaticMesh(Magnet);
			// 자석의 앞면은 -Y다. 몸의 앞(+X)을 보게 돌리고, 9×6.5 cm를 얼굴만 하게 키운다.
			SculptedFaceRest = FTransform(FRotator(0.0f, 90.0f, 0.0f), IGShadow::PaperFaceLocation, FVector(1.8f));
			Face->SetRelativeTransform(SculptedFaceRest);
			Face->SetMobility(EComponentMobility::Movable);
			Face->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			Face->SetCanEverAffectNavigation(false);
			Face->SetCastShadow(false);
			Face->RegisterComponent();
			SculptedFace = Face;
		}
		ApplyPose();
		SetActorHiddenInGame(!bManifested);
		return;
	}
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
	if (AttachSculptedBody(IGShadow::SculptedBodyPath))
	{
		ClipAudit.Start(TEXT("eoduksini"));
		bSlideAlongWalls = true;
		ApplyPose();
		SetActorHiddenInGame(!bManifested);
	}
}

bool AIGShadowFigure::AttachSculptedBody(const TCHAR* Path)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Path, nullptr, LOAD_NoWarn);
	if (!Mesh)
	{
		UE_LOG(LogIndieGame, Warning, TEXT("%s missing; the figure keeps the primitive outline"), Path);
		return false;
	}
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	const int32 SpineIndex = Skeleton.FindBoneIndex(IGShadow::SpineBone());
	const int32 ArmIndex[2] = {Skeleton.FindBoneIndex(IGShadow::ArmBone(0)), Skeleton.FindBoneIndex(IGShadow::ArmBone(1))};
	if (SpineIndex == INDEX_NONE || ArmIndex[0] == INDEX_NONE || ArmIndex[1] == INDEX_NONE)
	{
		UE_LOG(LogIndieGame, Warning, TEXT("%s lacks spine/arm bones; the figure keeps the primitive outline"), Path);
		return false;
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
	return true;
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
	if (bFollowFloor)
	{
		FollowFloor(0.0f);
	}
	ProbeCeiling();
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
	return GetActorLocation() + FVector(0.0f, 0.0f, 135.0f * GetBodyScale());
}

float AIGShadowFigure::GetBodyScale() const
{
	const float Scale = FMath::Lerp(IGShadow::MinScale, IGShadow::MaxScale, CurrentGrowth);
	// 허리를 축으로 숙인 머리 꼭대기. 다 자란 2.9 m 몸은 숙여도 2.7 m라 복도 천장(2.5 m)을 뚫었다.
	const float Waist = SculptedBody ? SpineRest.GetLocation().Z : IGShadow::RestWaist;
	const float Lean = FMath::DegreesToRadians(IGShadow::MaxLeanDegrees * CurrentGrowth);
	const float HeadTop = Waist + (IGShadow::RestHeight - Waist) * FMath::Cos(Lean);
	const float Room = (CeilingRoom - IGShadow::CeilingGap) / FMath::Max(HeadTop, 1.0f);
	return FMath::Max(IGShadow::MinScale, FMath::Min(Scale, Room));
}

void AIGShadowFigure::ProbeCeiling()
{
	UWorld* World = GetWorld();
	const FVector Feet = GetActorLocation();
	CeilingProbedAt = Feet;
	CeilingRoom = 1000.0f;
	if (!World)
	{
		return;
	}
	// 발에서 조금 띄워 쏜다. 계단 아래 선 몸이면 위층 계단 밑면이 천장이다.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGShadowCeiling), false, this);
	FHitResult Hit;
	const FVector From = Feet + FVector(0.0f, 0.0f, 30.0f);
	if (IGShadow::SweepVisible(*World, From, Feet + FVector(0.0f, 0.0f, 600.0f), 0.0f, Params, Hit) && !Hit.bStartPenetrating)
	{
		CeilingRoom = Hit.ImpactPoint.Z - Feet.Z;
	}
}

FVector AIGShadowFigure::SlideAlongWalls(const FVector& From, const FVector& To) const
{
	UWorld* World = GetWorld();
	if (!World || FVector::DistSquared(From, To) < 0.01f)
	{
		return To;
	}
	const float Scale = GetBodyScale();
	const float Radius = IGShadow::SlideRadius * Scale;
	// 무릎 높이 공은 문턱과 계단 한 단 위를 지나고, 가슴 높이 공은 천장 아래에 둔다.
	const float Low = Radius + IGShadow::StepClearance;
	const float High = FMath::Max(Low, FMath::Min(IGShadow::ChestHeight * Scale, CeilingRoom - Radius - 4.0f));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGShadowSlide), false, this);
	if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(Player);
	}
	FVector Start = From;
	FVector Move = To - From;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		float Time = 1.0f;
		FVector Normal = FVector::ZeroVector;
		for (const float Height : {Low, High})
		{
			const FVector Lift(0.0f, 0.0f, Height);
			FHitResult Hit;
			// 이미 벽 속에서 시작하면(나타난 자리가 벽에 걸친 때) 막지 않는다. 빠져나와야 한다.
			if (IGShadow::SweepVisible(*World, Start + Lift, Start + Move + Lift, Radius, Params, Hit)
				&& !Hit.bStartPenetrating && Hit.Time < Time)
			{
				Time = Hit.Time;
				Normal = Hit.Normal.GetSafeNormal2D();
			}
		}
		Start += Move * Time;
		if (Time >= 1.0f || Normal.IsNearlyZero())
		{
			return Start;
		}
		// 닿은 면에서 살짝 떼고, 남은 걸음은 그 면을 따라 미끄러뜨린다.
		Start += Normal * 0.5f;
		Move = FVector::VectorPlaneProject(Move * (1.0f - Time), Normal);
		Move.Z = 0.0f;
	}
	return Start;
}

void AIGShadowFigure::FollowFloor(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FVector Here = GetActorLocation();
	const float Reach = IGShadow::FootprintRadius * GetBodyScale();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGShadowFloor), false, this);
	if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(Player);
	}
	// 가운데와 자락 둘레 여덟 곳 밑에서 가장 높은 바닥. 벽 속에서 시작한 선과 비탈은 셈하지 않는다.
	bool bFound = false;
	float Floor = Here.Z;
	for (int32 Index = -1; Index < 8; ++Index)
	{
		const float Angle = UE_PI * 0.25f * Index;
		const FVector Foot = Index < 0 ? Here : Here + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Reach;
		const FVector Span(0.0f, 0.0f, IGShadow::FloorProbeReach);
		FHitResult Hit;
		if (IGShadow::SweepVisible(*World, Foot + Span, Foot - Span, 0.0f, Params, Hit)
			&& !Hit.bStartPenetrating && Hit.ImpactNormal.Z > 0.7f)
		{
			Floor = bFound ? FMath::Max(Floor, Hit.ImpactPoint.Z) : Hit.ImpactPoint.Z;
			bFound = true;
		}
	}
	if (!bFound)
	{
		return;
	}
	const float Z = DeltaSeconds > 0.0f
		? FMath::FInterpConstantTo(Here.Z, Floor, DeltaSeconds,
			Floor > Here.Z ? IGShadow::FloorRiseSpeed : IGShadow::FloorSettleSpeed)
		: Floor;
	if (!FMath::IsNearlyEqual(Z, Here.Z, 0.01f))
	{
		SetActorLocation(FVector(Here.X, Here.Y, Z));
	}
}

void AIGShadowFigure::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bManifested)
	{
		SetActorTickEnabled(false);
		return;
	}
	// 움직이기 전에 잰다. 지난 프레임에 그린 자세와 자리 그대로다. 바닥을 따르는 몸은 높이가 목표와
	// 달라도 서 있는 것이다.
	if (ClipAudit.IsOn())
	{
		const bool bArrived = bFollowFloor
			? FVector::DistSquared2D(GetActorLocation(), TargetLocation) <= 1.0f
			: GetActorLocation().Equals(TargetLocation, 1.0f);
		ClipAudit.Measure(SculptedBody, GetChestLocation(), UGameplayStatics::GetPlayerPawn(this, 0),
			FString::Printf(TEXT("grow%.1f%s"), CurrentGrowth, bArrived ? TEXT("") : TEXT(".move")));
	}
	CurrentGrowth = FMath::FInterpTo(CurrentGrowth, TargetGrowth, DeltaSeconds, IGShadow::GrowthFollowSpeed);
	if (MoveSpeed > 0.0f)
	{
		const FVector Here = GetActorLocation();
		// 바닥을 따르는 몸은 높이를 바닥이 정한다. 목표 높이로 끌려 내려가면 단 위에서 자락이 묻힌다.
		const FVector Goal = bFollowFloor ? FVector(TargetLocation.X, TargetLocation.Y, Here.Z) : TargetLocation;
		const FVector Next = FMath::VInterpConstantTo(Here, Goal, DeltaSeconds, MoveSpeed);
		SetActorLocation(bSlideAlongWalls ? SlideAlongWalls(Here, Next) : Next);
	}
	if (bFollowFloor)
	{
		FollowFloor(DeltaSeconds);
	}
	if (MoveSpeed > 0.0f && FVector::DistSquared2D(GetActorLocation(), CeilingProbedAt) > FMath::Square(20.0f))
	{
		ProbeCeiling();
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

void AIGShadowFigure::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClipAudit.Report();
	Super::EndPlay(EndPlayReason);
}

void AIGShadowFigure::ApplyPose()
{
	const float Scale = GetBodyScale();
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
	if (SculptedFace)
	{
		// 얼굴 자석은 허리 뼈에 붙은 것처럼 같이 숙인다.
		SculptedFace->SetRelativeTransform(SculptedFaceRest.GetRelativeTransform(SpineRest) * Waist);
	}
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
