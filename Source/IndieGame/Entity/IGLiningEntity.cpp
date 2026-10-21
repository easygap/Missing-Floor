#include "Entity/IGLiningEntity.h"

#include "AnimationRuntime.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "IndieGame.h"
#include "Interaction/IGAccessHatch.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/IGHorrorHUD.h"

namespace IGLining
{
	const TCHAR* const BodyPath = TEXT("/Game/Meshes/SK_Lining.SK_Lining");
	const TCHAR* const LegKeys[AIGLiningEntity::LegCount] = {TEXT("fl"), TEXT("fr"), TEXT("bl"), TEXT("br")};
	// 다리마다 앞뒤(sx)와 좌우(sy). 왼쪽이 -Y다(rig_lining.py LEGS).
	constexpr float LegSx[AIGLiningEntity::LegCount] = {1.0f, 1.0f, -1.0f, -1.0f};
	constexpr float LegSy[AIGLiningEntity::LegCount] = {-1.0f, 1.0f, -1.0f, 1.0f};
	// 걸음 묶음. 대각선 둘씩 짚고 당긴다.
	constexpr int32 GroupLegs[2][2] = {{0, 3}, {1, 2}};

	// 몸판 바닥에서 몸 뼈까지(cm). 누를 때 이만큼 벽에서 떨어진 자리가 몸 가운데다.
	constexpr float PressStandOff = 21.0f;
	// 짚고 버틸 때 몸 가운데와 벽 사이.
	constexpr float BraceStandOff = 36.0f;
	// 틈 둘레를 짚는 자리. 틈 가장자리에서 이만큼 바깥이다.
	constexpr float BraceMarginAlong = 34.0f;
	constexpr float BraceMarginAcross = 40.0f;

	// 걸음. 한 번 당길 때 몸이 나가는 거리와 시간, 두 다리를 옮기는 시간.
	constexpr float StepLength = 48.0f;
	constexpr float PullSeconds = 0.95f;
	constexpr float SwingSeconds = 0.78f;
	constexpr float UnfoldSeconds = 1.05f;
	constexpr float PeelSeconds = 1.9f;
	constexpr float BraceSeconds = 0.95f;
	constexpr float PressSeconds = 1.45f;
	// 다리를 옮길 때 패드가 면에서 들리는 높이.
	constexpr float SwingLift = 26.0f;
	// 떨어지는 패드는 옮기기 전에 제자리에서 몸 쪽 가장자리부터 든다(스윙의 앞 PeelHold). 바깥 가장자리
	// (패드 가운데에서 PadOuterEdge)를 축으로 PeelTiltDegrees만큼 들리는 동안 물갈퀴와 패드 막이 먼저 벌어진다.
	constexpr float PeelHold = 0.16f;
	// 처음 벽에서 떨어질 때는 더 오래 붙어 있다가 떨어진다(자재 더미가 살아 있다는 걸 보이는 순간).
	constexpr float FirstPeelHold = 0.3f;
	constexpr float PeelTiltDegrees = 20.0f;
	constexpr float PadOuterEdge = 24.0f;
	// 패드 막 끝은 짚었던 면에 붙어 있다가 스윙이 이만큼 지나거나 이만큼(cm) 늘어나면 놓인다.
	constexpr float SkinRelease = 0.34f;
	constexpr float SkinStretchMax = 26.0f;
	// 몸 살이 처지는 깊이(cm). 면에 붙어 받칠 때, 다리로만 들고 있을 때, 넘지 않는 한도.
	constexpr float SagRest = 3.5f;
	constexpr float SagHung = 5.0f;
	constexpr float SagMax = 10.0f;
	// 물갈퀴가 느슨할 때 처지는 깊이, 패드가 떨어지기 직전에 벌어지는 거리, 한도(cm).
	constexpr float WebDroop = 7.0f;
	constexpr float WebOpen = 7.0f;
	constexpr float WebMax = 14.0f;
	// 무른 데의 용수철(고유 진동수 Hz, 감쇠비). 살은 무겁게 느리고 막은 가볍게 빠르다.
	constexpr float MassHz = 1.8f;
	constexpr float MassDamping = 0.38f;
	constexpr float WebHz = 2.4f;
	constexpr float WebDamping = 0.3f;
	constexpr float SkinHz = 4.5f;
	constexpr float SkinDamping = 0.28f;
	// 몸 둘레 막(치마): 틈을 누를 때 바깥으로 퍼지는 거리, 들려 있을 때 처지는 깊이, 한도(cm), 용수철.
	constexpr float SkirtSpread = 9.0f;
	constexpr float SkirtDroop = 8.0f;
	constexpr float SkirtMax = 16.0f;
	constexpr float SkirtHz = 2.2f;
	constexpr float SkirtDamping = 0.3f;
	// 누를 때 몸 살이 면을 따라 옆으로 밀려나는 거리(cm).
	constexpr float MassSpread = 3.5f;
	const TCHAR* const SkirtKeys[4] = {TEXT("f"), TEXT("b"), TEXT("l"), TEXT("r")};
	const FVector SkirtSides[4] = {FVector(1.0f, 0.0f, 0.0f), FVector(-1.0f, 0.0f, 0.0f), FVector(0.0f, -1.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f)};
	// 몸판 반치수(몸 기준, cm). 사람이 이 안에 들면 눌린다.
	const FVector BodyHalf(40.0f, 60.0f, 21.0f);
	// 다리 충돌 반지름. 마디를 감은 막 판과 지지대가 뼈 줄에서 떨어진 거리의 가운데(11.3 cm)와 4분의 3
	// (13.2 cm) 사이다(SK_Lining manifest의 collision_reference).
	constexpr float LegRadius = 12.0f;
	// 발 충돌 상자 반치수(X 바깥쪽, Y 옆, Z 짚은 면에서 위). 짚는 면에 눌려 퍼진 덩어리를 덮는다. 몸 쪽으로
	// 늘어진 막 끝은 얇아서 넣지 않는다.
	const FVector PadHalf(33.0f, 30.0f, 5.5f);
	constexpr float GripRadius = 30.0f;
	// 몸판 앞뒤(면의 법선 쪽)로는 몸에 실제로 닿을 만큼만 잡는다. 벽에서 한 걸음 물러선 사람까지
	// 덮치면 망치질하는 자리에서 늘 눌린다.
	constexpr float GripDepth = 12.0f;
	// 쉬는 동안 틈을 다시 보는 간격.
	constexpr float RestCheckSeconds = 0.25f;
	constexpr float HoldCheckSeconds = 0.5f;
	// 자막 간격.
	constexpr float CreakCaptionInterval = 4.5f;
	constexpr float CreakCueInterval = 1.6f;

	FQuat FrameRotation(const FVector& FromDir, const FVector& FromUp, const FVector& ToDir, const FVector& ToUp)
	{
		auto Basis = [](const FVector& Dir, const FVector& Up) {
			FVector X = Dir.GetSafeNormal();
			if (X.IsNearlyZero())
			{
				X = FVector::ForwardVector;
			}
			FVector Z = Up - X * FVector::DotProduct(Up, X);
			if (!Z.Normalize())
			{
				const FVector Alt = FMath::Abs(X.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
				Z = (Alt - X * FVector::DotProduct(Alt, X)).GetSafeNormal();
			}
			return FRotationMatrix::MakeFromXZ(X, Z).ToQuat();
		};
		return Basis(ToDir, ToUp) * Basis(FromDir, FromUp).Inverse();
	}

	FVector TwoBoneKnee(const FVector& Hip, const FVector& Ankle, const float L1, const float L2, const FVector& Pole)
	{
		const FVector To = Ankle - Hip;
		const float Raw = To.Size();
		const FVector Axis = Raw > KINDA_SMALL_NUMBER ? To / Raw : FVector::UpVector;
		const float Dist = FMath::Clamp(Raw, FMath::Abs(L1 - L2) + 0.5f, L1 + L2 - 0.5f);
		const float Along = (L1 * L1 - L2 * L2 + Dist * Dist) / (2.0f * Dist);
		const float Height = FMath::Sqrt(FMath::Max(L1 * L1 - Along * Along, 0.0f));
		FVector Bend = Pole - Axis * FVector::DotProduct(Pole, Axis);
		if (!Bend.Normalize())
		{
			Bend = (FVector::UpVector - Axis * Axis.Z).GetSafeNormal();
		}
		return Hip + Axis * Along + Bend * Height;
	}

	float Smooth(const float X)
	{
		const float T = FMath::Clamp(X, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	/** 접자를 닫은 다리(몸 기준, 엉덩이에서). rig_lining.py folded_joints와 같은 수다. */
	void FoldedJoints(const int32 Leg, const FVector& HipLocal, FVector& OutKnee, FVector& OutAnkle, FVector& OutPad)
	{
		const float Sy = LegSy[Leg];
		if (LegSx[Leg] > 0.0f)
		{
			OutKnee = HipLocal + FVector(-78.0f, 10.0f * Sy, 10.0f);
			OutAnkle = OutKnee + FVector(76.0f, 6.0f * Sy, -12.0f);
			OutPad = OutAnkle + FVector(7.0f, 11.0f * Sy, -14.0f);
		}
		else
		{
			OutKnee = HipLocal + FVector(78.0f, 12.0f * Sy, -10.0f);
			OutAnkle = OutKnee + FVector(-76.0f, 5.0f * Sy, 2.0f);
			OutPad = OutAnkle + FVector(-7.0f, 11.0f * Sy, -13.0f);
		}
	}

	/** -IGLiningDrawCollision이면 충돌 모양을 그린다(검사 캡처에서 보이는 몸과 맞춰 본다). */
	bool DrawCollision()
	{
		static const bool bDraw = FParse::Param(FCommandLine::Get(), TEXT("IGLiningDrawCollision"));
		return bDraw;
	}
}

AIGLiningEntity::AIGLiningEntity()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);
	BodyBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("BodyBlocker"));
	BodyBlocker->SetupAttachment(Root);
	BodyBlocker->SetBoxExtent(IGLining::BodyHalf);
	BodyBlocker->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	BodyBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyBlocker->SetCollisionObjectType(ECC_WorldDynamic);
	BodyBlocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	BodyBlocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	BodyBlocker->SetCanEverAffectNavigation(false);
	BodyBlocker->SetUsingAbsoluteRotation(true);
	SetActorHiddenInGame(true);
}

bool AIGLiningEntity::LoadBody()
{
	if (Body)
	{
		return true;
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, IGLining::BodyPath, nullptr, LOAD_NoWarn);
	if (!Mesh)
	{
		UE_LOG(LogIndieGame, Warning, TEXT("%s missing; the lining does not appear"), IGLining::BodyPath);
		return false;
	}
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	BodyBone = TEXT("body");
	const int32 BodyIndex = Ref.FindBoneIndex(BodyBone);
	if (BodyIndex == INDEX_NONE)
	{
		UE_LOG(LogIndieGame, Warning, TEXT("SK_Lining lacks the body bone"));
		return false;
	}
	RestBody = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, BodyIndex);
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		for (int32 Segment = 0; Segment < 3; ++Segment)
		{
			SegmentBones[Leg][Segment] = FName(*FString::Printf(TEXT("leg_%s_%d"), IGLining::LegKeys[Leg], Segment + 1));
			const int32 Index = Ref.FindBoneIndex(SegmentBones[Leg][Segment]);
			if (Index == INDEX_NONE)
			{
				UE_LOG(LogIndieGame, Warning, TEXT("SK_Lining lacks %s"), *SegmentBones[Leg][Segment].ToString());
				return false;
			}
			RestSegment[Leg][Segment] = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Index);
		}
		const FVector Hip = RestSegment[Leg][0].GetLocation();
		const FVector Knee = RestSegment[Leg][1].GetLocation();
		const FVector Ankle = RestSegment[Leg][2].GetLocation();
		RestOutward[Leg] = (Ankle - Knee).GetSafeNormal2D();
		// 패드 가운데는 발목에서 바깥으로 10 cm, 아래로 16 cm(rig_lining.py SEG3_OUT·SEG3_DOWN).
		RestPad[Leg] = Ankle + RestOutward[Leg] * 10.0f + FVector(0.0f, 0.0f, -16.0f);
		HipLocal[Leg] = Hip - RestBody.GetLocation();
		Length1[Leg] = FVector::Distance(Hip, Knee);
		Length2[Leg] = FVector::Distance(Knee, Ankle);
	}
	// 무른 뼈(몸 살·물갈퀴·패드 막). 없는 몸이면 다리만 놓는다.
	bHasSoftBones = true;
	for (int32 Leg = 0; Leg < LegCount && bHasSoftBones; ++Leg)
	{
		MassBones[Leg] = FName(*FString::Printf(TEXT("mass_%s"), IGLining::LegKeys[Leg]));
		WebBones[Leg] = FName(*FString::Printf(TEXT("web_%s"), IGLining::LegKeys[Leg]));
		SkinBones[Leg] = FName(*FString::Printf(TEXT("skin_%s"), IGLining::LegKeys[Leg]));
		const int32 MassIndex = Ref.FindBoneIndex(MassBones[Leg]);
		const int32 WebIndex = Ref.FindBoneIndex(WebBones[Leg]);
		const int32 SkinIndex = Ref.FindBoneIndex(SkinBones[Leg]);
		if (MassIndex == INDEX_NONE || WebIndex == INDEX_NONE || SkinIndex == INDEX_NONE)
		{
			bHasSoftBones = false;
			break;
		}
		MassRest[Leg] = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, MassIndex).GetRelativeTransform(RestBody);
		const FTransform WebPose = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, WebIndex);
		WebRestBody[Leg] = WebPose.GetRelativeTransform(RestBody);
		WebRestUpper[Leg] = WebPose.GetRelativeTransform(RestSegment[Leg][0]);
		SkinRest[Leg] = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, SkinIndex).GetRelativeTransform(RestSegment[Leg][2]);
	}
	for (int32 Side = 0; Side < SkirtCount && bHasSoftBones; ++Side)
	{
		SkirtBones[Side] = FName(*FString::Printf(TEXT("skirt_%s"), IGLining::SkirtKeys[Side]));
		const int32 SkirtIndex = Ref.FindBoneIndex(SkirtBones[Side]);
		if (SkirtIndex == INDEX_NONE)
		{
			bHasSoftBones = false;
			break;
		}
		SkirtRest[Side] = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, SkirtIndex).GetRelativeTransform(RestBody);
	}
	// 다리를 천장과 벽까지 펴면 쉬는 자세의 경계를 크게 벗어난다. 경계를 넉넉히 늘려 잘리지 않게 한다.
	Mesh->SetPositiveBoundsExtension(FVector(140.0f, 140.0f, 280.0f));
	Mesh->SetNegativeBoundsExtension(FVector(140.0f, 140.0f, 280.0f));

	Body = NewObject<UPoseableMeshComponent>(this, TEXT("LiningBody"));
	Body->SetupAttachment(Root);
	Body->SetSkinnedAssetAndUpdate(Mesh);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);
	Body->SetCastShadow(true);
	Body->SetRelativeLocation(-RestBody.GetLocation());
	Body->RegisterComponent();
	Body->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);

	// 다리·패드 충돌은 사람(Pawn)만 막는다. 짚을 데를 찾는 트레이스(WorldStatic)에는 걸리지 않는다.
	const auto BlockPawn = [this](UPrimitiveComponent* Shape)
	{
		Shape->SetupAttachment(Root);
		Shape->SetUsingAbsoluteLocation(true);
		Shape->SetUsingAbsoluteRotation(true);
		Shape->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
		Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Shape->SetCollisionObjectType(ECC_WorldDynamic);
		Shape->SetCollisionResponseToAllChannels(ECR_Ignore);
		Shape->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Shape->SetCanEverAffectNavigation(false);
		Shape->RegisterComponent();
	};
	for (int32 Index = 0; Index < LegCount * 2; ++Index)
	{
		UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(this, *FString::Printf(TEXT("LegBlocker%d"), Index));
		Capsule->SetCapsuleSize(IGLining::LegRadius, 30.0f);
		BlockPawn(Capsule);
		LegBlockers.Add(Capsule);
	}
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(this, *FString::Printf(TEXT("PadBlocker%d"), Index));
		Box->SetBoxExtent(IGLining::PadHalf);
		BlockPawn(Box);
		PadBlockers.Add(Box);
	}
	UE_LOG(LogIndieGame, Display, TEXT("LINING_BODY scale body=%s seg=%s rot body=%s seg=%s soft=%d"),
		*RestBody.GetScale3D().ToCompactString(), *RestSegment[0][0].GetScale3D().ToCompactString(),
		*RestBody.GetRotation().Rotator().ToCompactString(), *RestSegment[0][0].GetRotation().Rotator().ToCompactString(),
		bHasSoftBones ? 1 : 0);
	UE_LOG(LogIndieGame, Display, TEXT("LINING_BODY loaded rest body=%s hip_fl=%s knee_fl=%s ankle_fl=%s L1=%.1f L2=%.1f"),
		*RestBody.GetLocation().ToCompactString(),
		*RestSegment[0][0].GetLocation().ToCompactString(),
		*RestSegment[0][1].GetLocation().ToCompactString(),
		*RestSegment[0][2].GetLocation().ToCompactString(),
		Length1[0], Length2[0]);
	return true;
}

void AIGLiningEntity::RestOnSurface(const FVector& Point, const FVector& Normal, const FVector& Forward)
{
	if (!LoadBody())
	{
		return;
	}
	const FVector Up = Normal.GetSafeNormal();
	BodyRotation = IGLining::FrameRotation(FVector::ForwardVector, FVector::UpVector, Forward, Up);
	BodyCenter = Point + Up * IGLining::PressStandOff;
	for (FLeg& Leg : Legs)
	{
		Leg.Fold = 1.0f;
		Leg.bSwinging = false;
	}
	HeldGap = NAME_None;
	TargetGap = NAME_None;
	SetActorHiddenInGame(false);
	SetBlockersEnabled(true);
	SetState(EIGLiningState::Folded);
	ResetSoftParts();
	PoseBody();
	StartTicking();
	UE_LOG(LogIndieGame, Display, TEXT("LINING_REST center=%s bodybone=%s padfl=%s bounds=%s/%s visible=%d hidden=%d tick=%d mats=%d lod=%d"),
		*BodyCenter.ToCompactString(),
		*Body->GetBoneLocation(BodyBone).ToCompactString(),
		*Body->GetBoneLocation(SegmentBones[0][2]).ToCompactString(),
		*Body->Bounds.Origin.ToCompactString(), *Body->Bounds.BoxExtent.ToCompactString(),
		Body->IsVisible() ? 1 : 0, IsHidden() ? 1 : 0, Body->IsComponentTickEnabled() ? 1 : 0,
		Body->GetNumMaterials(), Body->GetPredictedLODLevel());
}

void AIGLiningEntity::PlaceHolding(const FName GapId)
{
	const FIGLiningGap* Gap = FindGap(GapId);
	if (!Gap || !LoadBody())
	{
		return;
	}
	const FVector N = Gap->Normal.GetSafeNormal();
	FVector U = Gap->Up - N * FVector::DotProduct(Gap->Up, N);
	if (!U.Normalize())
	{
		U = FVector::UpVector;
	}
	const FVector R = FVector::CrossProduct(N, U);
	BodyRotation = IGLining::FrameRotation(FVector::ForwardVector, FVector::UpVector, U, N);
	BodyCenter = Gap->BodyAnchor() + N * IGLining::PressStandOff;
	// 다리 넷은 틈 둘레를 짚고 있다(누를 때의 자리와 같다).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningPlace), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		const FVector Spot = Gap->BodyAnchor()
			+ U * IGLining::LegSx[Leg] * (Gap->Size.Y * 0.5f + IGLining::BraceMarginAlong)
			+ R * IGLining::LegSy[Leg] * (Gap->Size.X * 0.5f + IGLining::BraceMarginAcross);
		FVector Pad = Spot + N * 2.0f;
		FVector Normal = N;
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Spot + N * 60.0f, Spot - N * 30.0f,
				FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			Pad = Hit.ImpactPoint + Hit.ImpactNormal * 2.0f;
			Normal = Hit.ImpactNormal;
		}
		// 벽 앞에 판이나 상자가 서 있으면 벽에 수직으로 찾은 자리가 소품 앞면이 되거나, 틈이 바닥에 가까우면
		// 바닥 밑이 된다. 그러면 다리가 소품이나 바닥을 뚫는다. 엉덩이에서 그 자리까지 막혔으면 걸을 때처럼
		// 엉덩이에서 짚을 데를 찾는다.
		const FVector Hip = HipWorld(Leg, BodyCenter, BodyRotation);
		const FVector ToPad = Pad - Hip;
		FHitResult Block;
		if (ToPad.Size() > 10.0f && GetWorld()->LineTraceSingleByObjectType(Block, Hip, Pad - ToPad.GetSafeNormal() * 6.0f,
				FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			FVector Found;
			FVector FoundNormal;
			if (FindContact(Leg, BodyCenter, BodyRotation, FVector::ZeroVector, Found, FoundNormal))
			{
				Pad = Found;
				Normal = FoundNormal;
			}
		}
		FLeg& L = Legs[Leg];
		const float Fold = Gap->bTuckLowerLegs && IGLining::LegSx[Leg] < 0.0f ? 1.0f : 0.0f;
		L.Pad = Pad;
		L.Normal = Normal;
		L.FromPad = Pad;
		L.ToPad = Pad;
		L.FromNormal = Normal;
		L.ToNormal = Normal;
		L.bSwinging = false;
		L.SwingAlpha = 1.0f;
		L.Fold = Fold;
		L.FoldFrom = Fold;
		L.FoldTo = Fold;
	}
	bHasMoved = true;
	TargetGap = NAME_None;
	HeldGap = GapId;
	for (FIGLiningGap& Existing : Gaps)
	{
		if (Existing.Id == GapId)
		{
			Existing.bSealed = true;
			if (AIGAccessHatch* Hatch = Existing.Hatch.Get())
			{
				Hatch->ForceOpenState(true);
				Hatch->SetSealed(true);
			}
		}
	}
	SetObstructions(GapId, true);
	SetActorHiddenInGame(false);
	SetBlockersEnabled(true);
	SetState(EIGLiningState::Holding);
	HoldingCheckSeconds = 0.0f;
	bGapsDirty = false;
	ResetSoftParts();
	PoseBody();
	StartTicking();
	UE_LOG(LogIndieGame, Display, TEXT("LINING_PLACED %s center=%s"), *GapId.ToString(), *BodyCenter.ToCompactString());
}

void AIGLiningEntity::SetGap(const FIGLiningGap& Gap)
{
	for (FIGLiningGap& Existing : Gaps)
	{
		if (Existing.Id == Gap.Id)
		{
			const bool bSealed = Existing.bSealed;
			Existing = Gap;
			Existing.bSealed = Existing.bSealed || bSealed;
			bGapsDirty = true;
			return;
		}
	}
	Gaps.Add(Gap);
	bGapsDirty = true;
	RefreshGapDrafts();
}

void AIGLiningEntity::ClearGaps()
{
	Gaps.Reset();
	TargetGap = NAME_None;
	HeldGap = NAME_None;
	RefreshGapDrafts();
}

const FIGLiningGap* AIGLiningEntity::FindGap(const FName Id) const
{
	return Gaps.FindByPredicate([Id](const FIGLiningGap& Gap) { return Gap.Id == Id; });
}

void AIGLiningEntity::NotifyGapsChanged()
{
	bGapsDirty = true;
	RefreshGapDrafts();
	if (State == EIGLiningState::Folded || State == EIGLiningState::Holding)
	{
		StartTicking();
	}
}

void AIGLiningEntity::UnsealGap(const FName Id)
{
	for (FIGLiningGap& Gap : Gaps)
	{
		if (Gap.Id == Id)
		{
			Gap.bSealed = false;
			if (AIGAccessHatch* Hatch = Gap.Hatch.Get())
			{
				Hatch->SetSealed(false);
			}
		}
	}
	NotifyGapsChanged();
}

bool AIGLiningEntity::IsGapOpen(const FName Id) const
{
	const FIGLiningGap* Gap = FindGap(Id);
	if (!Gap || Gap->bSealed)
	{
		return false;
	}
	if (Gap->bAlwaysOpen)
	{
		return true;
	}
	const AIGAccessHatch* Hatch = Gap->Hatch.Get();
	return Hatch && Hatch->IsGapOpen();
}

bool AIGLiningEntity::IsAnyBusy(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AIGLiningEntity> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->IsBusy())
		{
			return true;
		}
	}
	return false;
}

bool AIGLiningEntity::IsMoving() const
{
	switch (State)
	{
	case EIGLiningState::Peeling:
	case EIGLiningState::Stepping:
	case EIGLiningState::Bracing:
	case EIGLiningState::Pressing:
	case EIGLiningState::Retreating:
		return true;
	case EIGLiningState::Spread:
		return StateSeconds < MoveSeconds;
	default:
		return false;
	}
}

FVector AIGLiningEntity::GetPadLocation(const int32 Leg) const
{
	return Leg >= 0 && Leg < LegCount ? Legs[Leg].Pad : FVector::ZeroVector;
}

FVector AIGLiningEntity::GetKneeLocation(const int32 Leg) const
{
	return Leg >= 0 && Leg < LegCount ? Legs[Leg].Knee : FVector::ZeroVector;
}

FVector AIGLiningEntity::GetMassOffset(const int32 Quadrant) const
{
	return Quadrant >= 0 && Quadrant < LegCount ? Mass[Quadrant].Offset : FVector::ZeroVector;
}

bool AIGLiningEntity::IsSkinStuck(const int32 Leg) const
{
	return Leg >= 0 && Leg < LegCount && bSkinStuck[Leg];
}

float AIGLiningEntity::GetSkinStretch(const int32 Leg) const
{
	return IsSkinStuck(Leg) ? FVector::Distance(SkinStuckWorld[Leg].GetLocation(), DefaultSkinWorld(Leg).GetLocation()) : 0.0f;
}

float AIGLiningEntity::GetSkirtSpread(const int32 Side) const
{
	return Side >= 0 && Side < SkirtCount
		? FVector::DotProduct(Skirt[Side].Offset, BodyRotation.RotateVector(IGLining::SkirtSides[Side])) : 0.0f;
}

float AIGLiningEntity::GetSkirtDroop(const int32 Side) const
{
	return Side >= 0 && Side < SkirtCount ? -Skirt[Side].Offset.Z : 0.0f;
}

bool AIGLiningEntity::IsPadHolding(const int32 Leg) const
{
	return Leg >= 0 && Leg < LegCount && (!Legs[Leg].bSwinging || Legs[Leg].SwingAlpha <= 0.0f);
}

void AIGLiningEntity::SetState(const EIGLiningState NewState)
{
	if (State == NewState)
	{
		return;
	}
	UE_LOG(LogIndieGame, Display, TEXT("LINING_STATE %d -> %d target=%s held=%s"),
		static_cast<int32>(State), static_cast<int32>(NewState), *TargetGap.ToString(), *HeldGap.ToString());
	State = NewState;
	StateSeconds = 0.0f;
	RefreshGapDrafts();
}

void AIGLiningEntity::StartTicking()
{
	if (State != EIGLiningState::Hidden)
	{
		SetActorTickEnabled(true);
	}
}

bool AIGLiningEntity::ChooseTarget()
{
	const FIGLiningGap* Best = nullptr;
	float BestDistance = 0.0f;
	for (const FIGLiningGap& Gap : Gaps)
	{
		if (Gap.Id == HeldGap || !IsGapOpen(Gap.Id))
		{
			continue;
		}
		const float Distance = FVector::Distance(Gap.Center, BodyCenter);
		// 넓은 틈이 먼저다. 넓이가 같으면 가까운 쪽.
		if (!Best || Gap.Area() > Best->Area() + 1.0f
			|| (FMath::IsNearlyEqual(Gap.Area(), Best->Area(), 1.0f) && Distance < BestDistance))
		{
			Best = &Gap;
			BestDistance = Distance;
		}
	}
	if (!Best)
	{
		return false;
	}
	BeginTravelTo(Best->Id);
	return true;
}

void AIGLiningEntity::BeginTravelTo(const FName GapId)
{
	const FIGLiningGap* Gap = FindGap(GapId);
	if (!Gap)
	{
		return;
	}
	if (!HeldGap.IsNone())
	{
		// 막아 둔 자리는 접착층이 눌린 채 남는다. 덮개는 이제 닫힌다.
		SetObstructions(HeldGap, false);
	}
	TargetGap = GapId;
	const FVector GoalNormal = Gap->Normal.GetSafeNormal();
	const FVector Goal = Gap->BodyAnchor() + GoalNormal * IGLining::BraceStandOff;
	BuildPath(BodyCenter, BodyRotation.GetUpVector(), Goal, GoalNormal);
	PathStartRotation = BodyRotation;
	FVector GoalForward = Gap->Up - GoalNormal * FVector::DotProduct(Gap->Up, GoalNormal);
	if (!GoalForward.Normalize())
	{
		GoalForward = FVector::UpVector;
	}
	PathGoalRotation = IGLining::FrameRotation(FVector::ForwardVector, FVector::UpVector, GoalForward, GoalNormal);
	PathTravelled = 0.0f;
	PhaseSeconds = 0.0f;
	BlockedPulls = 0;
	OnTargetChosen.Broadcast(this, GapId);
	UE_LOG(LogIndieGame, Display, TEXT("LINING_TARGET %s area=%.0f path=%.0f"), *GapId.ToString(), Gap->Area(), PathLength());
	if (!bHasMoved && State == EIGLiningState::Folded)
	{
		// 처음 움직인다. 목표 쪽 패드 하나가 벽에서 떨어진다.
		bHasMoved = true;
		const FVector Toward = (Goal - BodyCenter).GetSafeNormal();
		float BestDot = -2.0f;
		for (int32 Leg = 0; Leg < LegCount; ++Leg)
		{
			const FVector Out = BodyRotation.RotateVector(RestOutward[Leg]);
			const float Dot = FVector::DotProduct(Out, Toward);
			if (Dot > BestDot)
			{
				BestDot = Dot;
				PeelLeg = Leg;
			}
		}
		SetState(EIGLiningState::Peeling);
		MoveFromCenter = BodyCenter;
		PlayCue(TEXT("Lining_Peel"), 1, Legs[PeelLeg].Pad, 0.85f, 1.0f);
		double Dummy = -100.0;
		Caption(NSLOCTEXT("IGMissingFloor", "CaptionLiningPeel", "[무언가 벽에서 쩍 떨어지는 소리]"), Legs[PeelLeg].Pad, 2.6f, Dummy, 0.0f);
		OnFirstMove.Broadcast(this);
	}
	else
	{
		bHasMoved = true;
		SetState(EIGLiningState::Stepping);
		// 접혀 있던 다리는 먼저 편다. 짚고 있던 다리는 그대로 두고 당기기부터 한다.
		bool bAnyFolded = false;
		for (const FLeg& Leg : Legs)
		{
			bAnyFolded |= Leg.Fold > 0.5f;
		}
		bPulling = !bAnyFolded;
		PullFrom = 0.0f;
		PullTo = FMath::Min(IGLining::StepLength, PathLength());
		if (bAnyFolded)
		{
			for (int32 Leg = 0; Leg < LegCount; ++Leg)
			{
				FVector Pad, Normal;
				if (Legs[Leg].Fold > 0.5f && FindContact(Leg, BodyCenter, BodyRotation, (Goal - BodyCenter).GetSafeNormal(), Pad, Normal))
				{
					StartSwing(Leg, Pad, Normal, 0.0f);
					// 대각선 둘씩 편다. 먼저 편 둘이 면을 짚은 뒤에 나머지 둘이 펴진다.
					Legs[Leg].SwingAlpha = (Leg == 1 || Leg == 2) ? -1.0f : 0.0f;
				}
			}
		}
	}
	HeldGap = NAME_None;
	StartTicking();
}

void AIGLiningEntity::BuildPath(const FVector& Start, const FVector& StartUp, const FVector& Goal, const FVector& GoalNormal)
{
	// 벽에서 몸을 떼어 방 쪽으로 나왔다가, 목표 틈 앞에서 벽 쪽으로 들어간다.
	const FVector P0 = Start;
	const FVector P1 = Start + StartUp.GetSafeNormal() * 55.0f;
	const FVector P2 = Goal + GoalNormal * 70.0f;
	const FVector P3 = Goal;
	PathPoints.Reset();
	PathDistances.Reset();
	constexpr int32 Samples = 24;
	float Total = 0.0f;
	for (int32 Index = 0; Index <= Samples; ++Index)
	{
		const float T = static_cast<float>(Index) / Samples;
		const float U = 1.0f - T;
		const FVector Point = P0 * (U * U * U) + P1 * (3.0f * U * U * T) + P2 * (3.0f * U * T * T) + P3 * (T * T * T);
		if (!PathPoints.IsEmpty())
		{
			Total += FVector::Distance(PathPoints.Last(), Point);
		}
		PathPoints.Add(Point);
		PathDistances.Add(Total);
	}
}

FVector AIGLiningEntity::PathPoint(const float Distance) const
{
	if (PathPoints.IsEmpty())
	{
		return BodyCenter;
	}
	if (Distance <= 0.0f)
	{
		return PathPoints[0];
	}
	for (int32 Index = 1; Index < PathPoints.Num(); ++Index)
	{
		if (Distance <= PathDistances[Index])
		{
			const float Span = FMath::Max(PathDistances[Index] - PathDistances[Index - 1], KINDA_SMALL_NUMBER);
			return FMath::Lerp(PathPoints[Index - 1], PathPoints[Index], (Distance - PathDistances[Index - 1]) / Span);
		}
	}
	return PathPoints.Last();
}

FQuat AIGLiningEntity::BodyRotationAt(const float Alpha) const
{
	return FQuat::Slerp(PathStartRotation, PathGoalRotation, IGLining::Smooth(Alpha));
}

FVector AIGLiningEntity::HipWorld(const int32 Leg, const FVector& Center, const FQuat& Rotation) const
{
	return Center + Rotation.RotateVector(HipLocal[Leg]);
}

bool AIGLiningEntity::FindContact(
	const int32 Leg,
	const FVector& FutureBody,
	const FQuat& FutureRotation,
	const FVector& Travel,
	FVector& OutPad,
	FVector& OutNormal) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FVector Hip = HipWorld(Leg, FutureBody, FutureRotation);
	const float Reach = Length1[Leg] + Length2[Leg] + 18.0f;
	const float Ideal = Reach * 0.72f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningContact), false, this);
	if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(Player);
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	const FVector Side = FutureRotation.RotateVector(FVector(IGLining::LegSx[Leg] * 0.55f, IGLining::LegSy[Leg] * 0.85f, 0.0f)).GetSafeNormal();
	const FVector Up = FutureRotation.GetUpVector();
	const FVector Ahead = Travel.GetSafeNormal() * (IGLining::LegSx[Leg] > 0.0f ? 0.65f : 0.15f);
	// 옆 벽 → 천장 쪽 → 바닥 쪽 순으로 짚을 데를 찾는다.
	const FVector Candidates[] = {
		(Side + Ahead + Up * 0.25f).GetSafeNormal(),
		(Side + Ahead + Up * 0.9f).GetSafeNormal(),
		(Side * 0.6f + Ahead + Up * 1.4f).GetSafeNormal(),
		(Side + Ahead - Up * 0.5f).GetSafeNormal(),
		(Side * 0.4f + Ahead - Up * 1.2f).GetSafeNormal(),
		(Side * 0.6f - Ahead * 0.4f - FVector::UpVector).GetSafeNormal(),
	};
	bool bFound = false;
	float BestScore = TNumericLimits<float>::Max();
	for (const FVector& Direction : Candidates)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByObjectType(Hit, Hip, Hip + Direction * (Reach + 40.0f), Objects, Params))
		{
			continue;
		}
		const float Distance = FVector::Distance(Hip, Hit.ImpactPoint);
		if (Distance < 45.0f || Distance > Reach * 0.95f)
		{
			continue;
		}
		// 앞으로 갈 몸 자리를 보고 고르되, 지금 몸에서도 닿아야 한다. 안 그러면 발이 면에 닿지 못하고 몸이 따라올
		// 때까지 허공에 떠 있다.
		const FVector Pad = Hit.ImpactPoint + Hit.ImpactNormal * 2.0f;
		if (ReachTo(Leg, Pad, Hit.ImpactNormal, BodyCenter, BodyRotation) > 0.96f)
		{
			continue;
		}
		const float Score = FMath::Abs(Distance - Ideal);
		if (Score < BestScore)
		{
			BestScore = Score;
			OutPad = Pad;
			OutNormal = Hit.ImpactNormal;
			bFound = true;
		}
	}
	if (!bFound)
	{
		// 짚을 데가 없으면 엉덩이 바깥 바닥을 짚는다. 거기도 닿지 않으면 이번에는 옮기지 않는다.
		FHitResult Hit;
		const FVector Out = Hip + Side * 70.0f;
		if (World->LineTraceSingleByObjectType(Hit, Out + FVector(0, 0, 30.0f), Out - FVector(0, 0, 400.0f), Objects, Params)
			&& ReachTo(Leg, Hit.ImpactPoint + Hit.ImpactNormal * 2.0f, Hit.ImpactNormal, BodyCenter, BodyRotation) <= 0.96f)
		{
			OutPad = Hit.ImpactPoint + Hit.ImpactNormal * 2.0f;
			OutNormal = Hit.ImpactNormal;
			bFound = true;
		}
	}
	return bFound;
}

void AIGLiningEntity::StartSwing(const int32 Leg, const FVector& Pad, const FVector& Normal, const float FoldTarget)
{
	FLeg& State_ = Legs[Leg];
	// 접힌 다리는 접힌 패드 자리에서 출발한다(SolveLeg가 접힌 동안 그 자리를 적어 둔다).
	State_.FromPad = State_.Pad;
	State_.FromNormal = State_.Normal;
	State_.ToPad = Pad;
	State_.ToNormal = Normal.GetSafeNormal();
	State_.FoldFrom = State_.Fold;
	State_.FoldTo = FoldTarget;
	State_.SwingAlpha = 0.0f;
	State_.bSwinging = true;
	State_.LiftScale = 1.0f;
	// 패드 막 끝은 짚었던 면에 붙어 남는다. 패드가 들리고 나서야 놓인다(UpdateSoftParts).
	if (bHasSoftBones && bSegmentsPosed)
	{
		SkinStuckWorld[Leg] = DefaultSkinWorld(Leg);
		bSkinStuck[Leg] = true;
		Skin[Leg] = FSoftPoint();
	}
}

void AIGLiningEntity::UpdateSwings(const float DeltaSeconds)
{
	const float Duration = (State == EIGLiningState::Peeling ? IGLining::PeelSeconds * 0.55f
		: State == EIGLiningState::Spread ? FMath::Max(MoveSeconds * 0.5f, 0.6f)
		: IGLining::SwingSeconds) / PaceScale;
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		FLeg& Leg = Legs[Index];
		if (!Leg.bSwinging)
		{
			continue;
		}
		Leg.SwingAlpha = FMath::Min(1.0f, Leg.SwingAlpha + DeltaSeconds / Duration);
		// 앞 몫 동안 패드는 제자리에서 몸 쪽 가장자리만 든다(SolveLeg). 막이 먼저 벌어진 뒤에 옮긴다.
		const float Hold = PeelHoldFraction();
		const float T = IGLining::Smooth((Leg.SwingAlpha - Hold) / (1.0f - Hold));
		const FVector Lift = (Leg.FromNormal + Leg.ToNormal).GetSafeNormal() * IGLining::SwingLift * Leg.LiftScale * FMath::Sin(T * PI);
		Leg.Pad = FMath::Lerp(Leg.FromPad, Leg.ToPad, T) + Lift;
		Leg.Normal = FMath::Lerp(Leg.FromNormal, Leg.ToNormal, T).GetSafeNormal();
		Leg.Fold = FMath::Lerp(Leg.FoldFrom, Leg.FoldTo, T);
		if (Leg.SwingAlpha >= 1.0f)
		{
			Leg.bSwinging = false;
			Leg.Pad = Leg.ToPad;
			Leg.Normal = Leg.ToNormal;
			// 내려놓은 패드는 체중을 받아 한 번 더 눌렸다가 자리 잡는다. 더듬는 발은 가볍게 짚는다.
			Leg.PlantKick = Leg.LiftScale < 0.5f ? 0.45f : 1.0f;
			// 짚은 다리로 무게가 옮겨 가며 몸 살이 한 번 처졌다가 자리를 잡는다.
			KickSoftParts(Index, Leg.LiftScale < 0.5f ? 0.35f : 1.0f);
			PlayCue(TEXT("Lining_Plant"), 3, Leg.Pad, Leg.LiftScale < 0.5f ? 0.22f : 0.55f, FMath::FRandRange(0.92f, 1.06f));
		}
	}
}

void AIGLiningEntity::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StateSeconds += DeltaSeconds;
	switch (State)
	{
	case EIGLiningState::Hidden:
		SetActorTickEnabled(false);
		return;
	case EIGLiningState::Folded:
		HoldingCheckSeconds += DeltaSeconds;
		if (HoldingCheckSeconds >= IGLining::RestCheckSeconds || bGapsDirty)
		{
			HoldingCheckSeconds = 0.0f;
			bGapsDirty = false;
			ChooseTarget();
		}
		break;
	case EIGLiningState::Peeling:
		UpdatePeeling(DeltaSeconds);
		break;
	case EIGLiningState::Stepping:
		UpdateStepping(DeltaSeconds);
		break;
	case EIGLiningState::Bracing:
		UpdateBracing(DeltaSeconds);
		break;
	case EIGLiningState::Pressing:
		UpdatePressing(DeltaSeconds);
		break;
	case EIGLiningState::Holding:
		UpdateHolding(DeltaSeconds);
		break;
	case EIGLiningState::Spread:
		UpdateSpread(DeltaSeconds);
		break;
	case EIGLiningState::Retreating:
		UpdateRetreating(DeltaSeconds);
		break;
	case EIGLiningState::Gripping:
		break;
	}
	UpdateSwings(DeltaSeconds);
	if (State == EIGLiningState::Stepping || State == EIGLiningState::Bracing)
	{
		int32 Holding = 0;
		for (int32 Leg = 0; Leg < LegCount; ++Leg)
		{
			Holding += IsPadHolding(Leg) ? 1 : 0;
		}
		MinPadsHoldingWhileWalking = FMath::Min(MinPadsHoldingWhileWalking, Holding);
	}
	for (FLeg& Leg : Legs)
	{
		Leg.PlantKick = FMath::Max(0.0f, Leg.PlantKick - DeltaSeconds / 0.18f);
	}
	// 몸의 눌림은 순간이동하지 않고 따라간다(누르기 끝에서 납작해지고, 당길 때 늘어난다).
	BodySquash = FMath::VInterpTo(BodySquash, ComputeBodySquash(), DeltaSeconds, 7.0f);
	UpdateSoftParts(DeltaSeconds);
	PoseBody();
	CheckGrip();
	// 쉬는 동안은 틈만 본다. 자세가 안 바뀌니 몇 틱에 한 번이면 된다. 처진 살이나 막이 아직 흔들리면 매 틱 본다.
	if ((State == EIGLiningState::Folded || State == EIGLiningState::Holding || State == EIGLiningState::Spread)
		&& !IsMoving())
	{
		bool bAnySwing = false;
		for (const FLeg& Leg : Legs)
		{
			bAnySwing |= Leg.bSwinging;
		}
		PrimaryActorTick.TickInterval = bAnySwing || !AreSoftPartsSettled() ? 0.0f : 0.1f;
	}
	else
	{
		PrimaryActorTick.TickInterval = 0.0f;
	}
}

void AIGLiningEntity::UpdatePeeling(const float DeltaSeconds)
{
	const float Seconds = IGLining::PeelSeconds / PaceScale;
	FLeg& Leg = Legs[PeelLeg];
	// 처음 0.35는 패드가 벽에 붙은 채 몸이 조금 떨어지고, 그다음 다리가 펴져 옆을 짚는다.
	const float Alpha = StateSeconds / Seconds;
	if (Alpha < 0.35f)
	{
		// 몸이 벽에서 한 뼘도 안 되게 떨어지고, 붙어 있던 패드가 들린다.
		const float Lift = IGLining::Smooth(Alpha / 0.35f);
		BodyCenter = MoveFromCenter + BodyRotation.GetUpVector() * 5.0f * Lift;
		Leg.Fold = FMath::Lerp(1.0f, 0.86f, Lift);
	}
	else if (!Leg.bSwinging && Leg.Fold > 0.5f)
	{
		const FIGLiningGap* Gap = FindGap(TargetGap);
		const FVector Toward = Gap ? (Gap->Center - BodyCenter).GetSafeNormal() : BodyRotation.GetForwardVector();
		FVector Pad, Normal;
		if (FindContact(PeelLeg, BodyCenter, BodyRotation, Toward, Pad, Normal))
		{
			StartSwing(PeelLeg, Pad, Normal, 0.0f);
		}
		else
		{
			Leg.Fold = 0.0f;
		}
	}
	if (Alpha >= 1.0f && !Leg.bSwinging)
	{
		// 나머지 다리를 펴며 걷기 시작한다.
		SetState(EIGLiningState::Stepping);
		const FIGLiningGap* Gap = FindGap(TargetGap);
		const FVector Toward = Gap ? (Gap->Center - BodyCenter).GetSafeNormal() : BodyRotation.GetForwardVector();
		for (int32 Index = 0; Index < LegCount; ++Index)
		{
			FVector Pad, Normal;
			if (Index != PeelLeg && Legs[Index].Fold > 0.5f && FindContact(Index, BodyCenter, BodyRotation, Toward, Pad, Normal))
			{
				StartSwing(Index, Pad, Normal, 0.0f);
				// 떨어진 발의 대각선 짝이 먼저 펴고, 나머지 둘은 그 두 발이 면을 짚은 뒤에 옮긴다.
				Legs[Index].SwingAlpha = Index == LegCount - 1 - PeelLeg ? 0.0f : -1.0f;
			}
		}
		bPulling = false;
		PhaseSeconds = 0.0f;
		PathTravelled = 0.0f;
	}
}

void AIGLiningEntity::UpdateStepping(const float DeltaSeconds)
{
	PhaseSeconds += DeltaSeconds * PaceScale;
	const float Length = PathLength();
	if (bPulling)
	{
		float Alpha = FMath::Clamp(PhaseSeconds / IGLining::PullSeconds, 0.0f, 1.0f);
		const float Next = FMath::Lerp(PullFrom, PullTo, IGLining::Smooth(Alpha));
		// 짚은 발이 닿지 않을 만큼은 당기지 않는다. 발이 면에서 끌려가는 대신 이번 당기기를 거기서 끝내고 뒤처진
		// 발부터 옮긴다. 몇 번 잇달아 막히면(닿을 데가 없는 길) 그냥 당긴다.
		if (Next > PathTravelled && BlockedPulls < 3
			&& !PlantedLegsReach(PathPoint(Next), BodyRotationAt(Length > 0.0f ? Next / Length : 1.0f)))
		{
			Alpha = 1.0f;
			PullTo = PathTravelled;
			++BlockedPulls;
		}
		else
		{
			PathTravelled = Next;
			if (Alpha >= 1.0f)
			{
				BlockedPulls = 0;
			}
		}
		BodyCenter = PathPoint(PathTravelled);
		BodyRotation = BodyRotationAt(Length > 0.0f ? PathTravelled / Length : 1.0f);
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now - LastCreakAt > IGLining::CreakCueInterval)
		{
			LastCreakAt = Now;
			PlayCue(TEXT("Lining_Creak"), 3, BodyCenter, 0.7f, FMath::FRandRange(0.9f, 1.05f));
			Caption(NSLOCTEXT("IGMissingFloor", "CaptionLiningCreak", "[쇠붙이가 버티며 삐걱이는 소리]"), BodyCenter, 2.0f,
				LastCreakCaptionAt, IGLining::CreakCaptionInterval);
		}
		if (Alpha >= 1.0f)
		{
			if (PathTravelled >= Length - 1.0f)
			{
				// 다 왔다. 둘레를 짚는다.
				MoveFromCenter = BodyCenter;
				MoveFromRotation = BodyRotation;
				SetState(EIGLiningState::Bracing);
				const FIGLiningGap* Gap = FindGap(TargetGap);
				if (Gap)
				{
					const FVector N = Gap->Normal.GetSafeNormal();
					FVector U = Gap->Up - N * FVector::DotProduct(Gap->Up, N);
					U.Normalize();
					const FVector R = FVector::CrossProduct(N, U);
					MoveToCenter = Gap->BodyAnchor() + N * IGLining::BraceStandOff;
					MoveToRotation = IGLining::FrameRotation(FVector::ForwardVector, FVector::UpVector, U, N);
					for (int32 Leg = 0; Leg < LegCount; ++Leg)
					{
						const FVector Spot = Gap->BodyAnchor()
							+ U * IGLining::LegSx[Leg] * (Gap->Size.Y * 0.5f + IGLining::BraceMarginAlong)
							+ R * IGLining::LegSy[Leg] * (Gap->Size.X * 0.5f + IGLining::BraceMarginAcross);
						FVector Pad = Spot + N * 2.0f;
						FVector Normal = N;
						FHitResult Hit;
						FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningBrace), false, this);
						if (GetWorld()->LineTraceSingleByObjectType(Hit, Spot + N * 60.0f, Spot - N * 30.0f,
								FCollisionObjectQueryParams(ECC_WorldStatic), Params))
						{
							Pad = Hit.ImpactPoint + Hit.ImpactNormal * 2.0f;
							Normal = Hit.ImpactNormal;
						}
						StartSwing(Leg, Pad, Normal, Gap->bTuckLowerLegs && IGLining::LegSx[Leg] < 0.0f ? 1.0f : 0.0f);
						// 대각선 둘이 먼저 짚고, 그 둘이 면을 누른 뒤에 나머지 둘이 옮긴다(늘 두 발이 버틴다).
						Legs[Leg].SwingAlpha = (Leg == 1 || Leg == 2) ? -1.0f : 0.0f;
					}
				}
				else
				{
					MoveToCenter = BodyCenter;
					MoveToRotation = BodyRotation;
				}
				return;
			}
			// 당기기를 마쳤다. 뒤에 처진 두 다리를 앞으로 옮긴다.
			bPulling = false;
			PhaseSeconds = 0.0f;
			const FVector Travel = (PathPoint(FMath::Min(PathTravelled + IGLining::StepLength, Length)) - BodyCenter).GetSafeNormal();
			float GroupLag[2] = {0.0f, 0.0f};
			for (int32 Group = 0; Group < 2; ++Group)
			{
				for (const int32 Leg : IGLining::GroupLegs[Group])
				{
					GroupLag[Group] += FVector::DotProduct(BodyCenter - Legs[Leg].Pad, Travel);
					// 다 뻗은 다리가 있는 짝을 먼저 옮긴다(다음 당기기에서 그 발이 끌려가지 않게).
					if (LegReach(Leg, BodyCenter, BodyRotation) > 0.9f)
					{
						GroupLag[Group] += 1000.0f;
					}
				}
			}
			SwingGroup = GroupLag[0] >= GroupLag[1] ? 0 : 1;
			const float FutureDistance = FMath::Min(PathTravelled + IGLining::StepLength * 1.6f, Length);
			const FVector FutureBody = PathPoint(FutureDistance);
			const FQuat FutureRotation = BodyRotationAt(Length > 0.0f ? FutureDistance / Length : 1.0f);
			for (const int32 Leg : IGLining::GroupLegs[SwingGroup])
			{
				FVector Pad, Normal;
				if (FindContact(Leg, FutureBody, FutureRotation, Travel, Pad, Normal))
				{
					StartSwing(Leg, Pad, Normal, 0.0f);
				}
			}
		}
		return;
	}
	// 두 다리를 옮기는 중. 다 옮기면 다시 당긴다.
	bool bAnySwing = false;
	for (const FLeg& Leg : Legs)
	{
		bAnySwing |= Leg.bSwinging;
	}
	if (!bAnySwing && PhaseSeconds >= 0.2f)
	{
		bPulling = true;
		PhaseSeconds = 0.0f;
		PullFrom = PathTravelled;
		PullTo = FMath::Min(Length, PathTravelled + IGLining::StepLength);
		PlayCue(TEXT("Lining_Drag"), 2, BodyCenter, 0.5f, FMath::FRandRange(0.9f, 1.05f));
	}
}

void AIGLiningEntity::UpdateBracing(const float DeltaSeconds)
{
	const float Alpha = FMath::Clamp(StateSeconds * PaceScale / IGLining::BraceSeconds, 0.0f, 1.0f);
	BodyCenter = FMath::Lerp(MoveFromCenter, MoveToCenter, IGLining::Smooth(Alpha));
	BodyRotation = FQuat::Slerp(MoveFromRotation, MoveToRotation, IGLining::Smooth(Alpha));
	bool bAnySwing = false;
	for (const FLeg& Leg : Legs)
	{
		bAnySwing |= Leg.bSwinging;
	}
	if (Alpha >= 1.0f && !bAnySwing)
	{
		if (!IsGapOpen(TargetGap))
		{
			// 오는 사이에 덮개가 닫혔다. 바람이 새지 않으니 누르지 않고 그 자리에 멈춘다.
			UE_LOG(LogIndieGame, Display, TEXT("LINING_ARRIVED_CLOSED %s"), *TargetGap.ToString());
			TargetGap = NAME_None;
			HeldGap = NAME_None;
			SetState(EIGLiningState::Holding);
			HoldingCheckSeconds = 0.0f;
			OnSettled.Broadcast(this);
			return;
		}
		SetState(EIGLiningState::Pressing);
		SetObstructions(TargetGap, true);
		MoveFromCenter = BodyCenter;
		const FIGLiningGap* Gap = FindGap(TargetGap);
		MoveToCenter = Gap ? Gap->BodyAnchor() + Gap->Normal.GetSafeNormal() * IGLining::PressStandOff : BodyCenter;
		PlayCue(TEXT("Lining_Press"), 1, MoveToCenter, 0.85f, 0.94f);
		Caption(NSLOCTEXT("IGMissingFloor", "CaptionLiningPress", "[무언가 벽에 쩍 들러붙는 소리]"), MoveToCenter, 2.4f,
			LastPressCaptionAt, 3.0f);
	}
}

void AIGLiningEntity::UpdatePressing(const float DeltaSeconds)
{
	const float Alpha = FMath::Clamp(StateSeconds * PaceScale / IGLining::PressSeconds, 0.0f, 1.0f);
	// 한 번에 붙지 않고 두어 번 밀어붙인다.
	const float Shudder = FMath::Sin(Alpha * PI * 5.0f) * (1.0f - Alpha) * 2.5f;
	const FIGLiningGap* Gap = FindGap(TargetGap);
	const FVector N = Gap ? Gap->Normal.GetSafeNormal() : BodyRotation.GetUpVector();
	BodyCenter = FMath::Lerp(MoveFromCenter, MoveToCenter, IGLining::Smooth(Alpha)) + N * Shudder;
	if (const TObjectPtr<UAudioComponent>* Draft = GapDrafts.Find(TargetGap))
	{
		if (UAudioComponent* Audio = Draft->Get())
		{
			Audio->AdjustVolume(0.1f, FMath::Max(0.02f, 1.0f - IGLining::Smooth(Alpha)));
		}
	}
	if (Alpha >= 1.0f)
	{
		const FName Sealed = TargetGap;
		SealGap(Sealed);
		HeldGap = Sealed;
		TargetGap = NAME_None;
		SetState(EIGLiningState::Holding);
		HoldingCheckSeconds = 0.0f;
		OnGapSealed.Broadcast(this, Sealed);
		OnSettled.Broadcast(this);
	}
}

void AIGLiningEntity::UpdateHolding(const float DeltaSeconds)
{
	UpdateProbing(DeltaSeconds);
	HoldingCheckSeconds += DeltaSeconds;
	if (HoldingCheckSeconds >= IGLining::HoldCheckSeconds || bGapsDirty)
	{
		HoldingCheckSeconds = 0.0f;
		bGapsDirty = false;
		ChooseTarget();
	}
}

void AIGLiningEntity::UpdateProbing(const float DeltaSeconds)
{
	for (const FLeg& Leg : Legs)
	{
		if (Leg.bSwinging)
		{
			return;
		}
	}
	ProbeSeconds += DeltaSeconds * PaceScale;
	// 간격은 5~8초. 씨앗으로 정해 실행마다 같다.
	const float Interval = 5.0f + 3.0f * FMath::Frac(FMath::Sin(static_cast<float>(ProbeCount) * 12.9898f) * 43758.547f);
	if (ProbeSeconds < Interval)
	{
		return;
	}
	ProbeSeconds = 0.0f;
	const int32 Index = ProbeCount % LegCount;
	++ProbeCount;
	if (Index == PinnedLeg || Legs[Index].Fold > 0.5f)
	{
		return;
	}
	FLeg& Leg = Legs[Index];
	// 짚은 면을 따라 한 뼘 옆을 낮게 짚어 본다. 다음 번에는 제자리로 돌아온다.
	FVector Along = FVector::CrossProduct(Leg.Normal, BodyRotation.GetForwardVector());
	if (!Along.Normalize())
	{
		Along = FVector::CrossProduct(Leg.Normal, FVector::RightVector).GetSafeNormal();
	}
	const float Reach = (ProbeCount / LegCount) % 2 == 0 ? 9.0f : -9.0f;
	StartSwing(Index, Leg.Pad + Along * Reach, Leg.Normal, 0.0f);
	Leg.LiftScale = 0.28f;
	StartTicking();
}

FVector AIGLiningEntity::ComputeBodySquash() const
{
	switch (State)
	{
	// 몸을 크게 키워 펼침을 흉내 내지 않는다. 눌리면 두께만 줄고, 넓게 퍼지는 것은 몸 둘레 막과 살(무른 뼈)이 맡는다.
	case EIGLiningState::Stepping:
		if (bPulling)
		{
			// 당기기 첫머리에 몸을 웅크려 힘을 모은다.
			const float Alpha = FMath::Clamp(PhaseSeconds / IGLining::PullSeconds, 0.0f, 1.0f);
			const float Gather = 1.0f - FMath::Clamp(Alpha / 0.25f, 0.0f, 1.0f);
			return FVector(1.0f, 1.0f, 1.0f - 0.07f * Gather);
		}
		return FVector::OneVector;
	case EIGLiningState::Bracing:
		return FVector(1.0f, 1.0f, 0.96f);
	case EIGLiningState::Pressing:
	{
		const float Alpha = IGLining::Smooth(FMath::Clamp(StateSeconds * PaceScale / IGLining::PressSeconds, 0.0f, 1.0f));
		return FVector(1.0f, 1.0f, 1.0f - 0.16f * Alpha);
	}
	case EIGLiningState::Holding:
		if (!HeldGap.IsNone())
		{
			// 막은 채로 숨 쉬듯 조금씩 다시 눌러 붙인다.
			const UWorld* World = GetWorld();
			const float Time = World ? static_cast<float>(World->GetTimeSeconds()) : 0.0f;
			const float Breath = 0.02f * FMath::Sin(Time * 2.0f * PI * 0.28f);
			return FVector(1.0f, 1.0f, 0.85f + Breath);
		}
		return FVector::OneVector;
	case EIGLiningState::Spread:
		return FVector(1.0f, 1.0f, 0.93f);
	case EIGLiningState::Gripping:
		return FVector(1.0f, 1.0f, 0.88f);
	default:
		return FVector::OneVector;
	}
}

FVector AIGLiningEntity::SoftGravity() const
{
	const FVector Down(0.0f, 0.0f, -1.0f);
	const FVector Up = BodyRotation.GetUpVector();
	// 면에 붙어 있으면 면 쪽으로 누르는 성분은 그 면이 받는다. 면을 따라 흘러내리는 성분만 남는다.
	return Down - Up * FMath::Min(0.0f, FVector::DotProduct(Down, Up)) * SoftContact;
}

void AIGLiningEntity::UpdateSoftParts(const float DeltaSeconds)
{
	if (!bHasSoftBones || DeltaSeconds <= 0.0f)
	{
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	// 몸이 끌려가며 붙는 가속도. 살과 막은 그 반대쪽으로 뒤처졌다가 따라온다.
	const FVector Velocity = bSoftFresh ? FVector::ZeroVector : (BodyCenter - SoftLastCenter) / Dt;
	const FVector Accel = bSoftFresh ? FVector::ZeroVector : ((Velocity - SoftLastVelocity) / Dt).GetClampedToMaxSize(900.0f);
	SoftAcceleration = FMath::Lerp(SoftAcceleration, Accel, FMath::Min(1.0f, Dt * 20.0f));
	SoftLastCenter = BodyCenter;
	SoftLastVelocity = Velocity;
	bSoftFresh = false;
	const bool bOnSurface = State == EIGLiningState::Folded || State == EIGLiningState::Pressing || State == EIGLiningState::Holding
		|| State == EIGLiningState::Spread || State == EIGLiningState::Gripping;
	SoftContact = FMath::FInterpTo(SoftContact, bOnSurface ? 1.0f : 0.0f, Dt, 6.0f);
	const FVector Gravity = SoftGravity();
	const FVector Up = BodyRotation.GetUpVector();
	const float Sag = FMath::Lerp(IGLining::SagHung, IGLining::SagRest, SoftContact);
	// 틈을 누르는 정도(0~1). 누르는 동안 차오르고, 막은 채로는 다 눌려 있다. 5층에서 편 몸과 쉬는 더미는 덜 퍼진다.
	const float Press = State == EIGLiningState::Pressing
		? IGLining::Smooth(FMath::Clamp(StateSeconds * PaceScale / IGLining::PressSeconds, 0.0f, 1.0f))
		: State == EIGLiningState::Holding && !HeldGap.IsNone() ? 1.0f
		: State == EIGLiningState::Gripping ? 1.0f
		: State == EIGLiningState::Spread ? 0.7f
		: State == EIGLiningState::Folded ? 0.3f : 0.0f;
	const float Hold = PeelHoldFraction();
	// 0.1초 간격으로 틱할 때도 튀지 않게 잘게 나눠 적분한다.
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Dt * 120.0f), 1, 12);
	const float H = Dt / Steps;
	const auto Spring = [this, Steps, H](FSoftPoint& Point, const FVector& Target, const float Hz, const float Damping,
		const float Inertia, const float Limit)
	{
		const float Omega = 2.0f * PI * Hz;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Point.Velocity += ((Target - Point.Offset) * (Omega * Omega) - Point.Velocity * (2.0f * Damping * Omega)
				- SoftAcceleration * Inertia) * H;
			Point.Offset += Point.Velocity * H;
		}
		if (Point.Offset.SizeSquared() > Limit * Limit)
		{
			Point.Offset = Point.Offset.GetClampedToMaxSize(Limit);
			Point.Velocity *= 0.5f;
		}
	};
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		const FLeg& Leg = Legs[Index];
		// 받치던 다리가 들렸거나 접힌 사분면이 더 처진다.
		const bool bUnsupported = Leg.bSwinging || Leg.Fold > 0.5f;
		// 누르면 살이 면을 따라 그 사분면 쪽으로 밀려난다.
		const FVector Outward = BodyRotation.RotateVector(FVector(IGLining::LegSx[Index], IGLining::LegSy[Index], 0.0f).GetSafeNormal());
		Spring(Mass[Index], Gravity * Sag * (bUnsupported ? 1.3f : 0.8f) + Outward * IGLining::MassSpread * Press,
			IGLining::MassHz, IGLining::MassDamping, 0.9f, IGLining::SagMax);

		// 물갈퀴: 윗마디를 몸 쪽으로 접을수록 느슨해져 처지고, 패드가 떨어지기 직전에 몸에서 먼저 벌어진다.
		const FVector Upper = BodyRotation.UnrotateVector(Leg.Knee - Leg.Hip).GetSafeNormal();
		const float Slack = FMath::Clamp((0.85f - FVector::DotProduct(Upper, RestOutward[Index])) / 1.2f, 0.0f, 1.0f);
		const float Opening = Leg.bSwinging && Leg.SwingAlpha > 0.0f
			? FMath::Sin(PI * FMath::Clamp(Leg.SwingAlpha / (Hold + 0.12f), 0.0f, 1.0f)) * Leg.LiftScale
			: 0.0f;
		Spring(Web[Index], FVector(0.0f, 0.0f, -IGLining::WebDroop * (0.3f + 0.7f * Slack)) + Up * IGLining::WebOpen * Opening,
			IGLining::WebHz, IGLining::WebDamping, 1.0f, IGLining::WebMax);

		// 패드 막 끝: 붙어 있는 동안은 짚었던 면에 남는다. 놓이면 늘어났던 자리에서 패드 쪽으로 튕겨 돌아온다.
		if (bSkinStuck[Index])
		{
			const FVector Stretch = SkinStuckWorld[Index].GetLocation() - DefaultSkinWorld(Index).GetLocation();
			if (!Leg.bSwinging || Leg.SwingAlpha >= Hold + IGLining::SkinRelease - IGLining::PeelHold
				|| Stretch.Size() > IGLining::SkinStretchMax)
			{
				bSkinStuck[Index] = false;
				Skin[Index].Offset = Stretch.GetClampedToMaxSize(IGLining::SkinStretchMax);
				Skin[Index].Velocity = FVector::ZeroVector;
			}
		}
		else
		{
			Spring(Skin[Index], FVector::ZeroVector, IGLining::SkinHz, IGLining::SkinDamping, 0.6f, IGLining::SkinStretchMax);
		}
	}
	// 몸 둘레 막: 들려 있으면 무게로 처지고, 틈을 누를수록 면을 따라 바깥으로 퍼진다(몸을 키우지 않는다).
	for (int32 Side = 0; Side < SkirtCount; ++Side)
	{
		const FVector Outward = BodyRotation.RotateVector(IGLining::SkirtSides[Side]);
		const FVector Target = Outward * IGLining::SkirtSpread * Press + Gravity * IGLining::SkirtDroop * (1.0f - 0.7f * SoftContact);
		Spring(Skirt[Side], Target, IGLining::SkirtHz, IGLining::SkirtDamping, 1.0f, IGLining::SkirtMax);
	}
}

void AIGLiningEntity::ResetSoftParts()
{
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		Mass[Index] = FSoftPoint();
		Web[Index] = FSoftPoint();
		Skin[Index] = FSoftPoint();
		bSkinStuck[Index] = false;
	}
	for (FSoftPoint& Point : Skirt)
	{
		Point = FSoftPoint();
	}
	SoftAcceleration = FVector::ZeroVector;
	SoftLastVelocity = FVector::ZeroVector;
	bSoftFresh = true;
	// 옮겨 놓은 자리에서 이미 다 처진 채로 시작한다(첫 화면에서 살이 내려앉는 게 보이지 않게).
	for (int32 Step = 0; Step < 20; ++Step)
	{
		UpdateSoftParts(0.05f);
	}
}

void AIGLiningEntity::PoseSoftParts(const FTransform& BodyTransform)
{
	if (!bHasSoftBones)
	{
		return;
	}
	const FVector ToComponent = RestBody.GetLocation() - BodyCenter;
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		// 살은 몸 뼈(눌림 포함)를 따르다 처진 만큼 비킨다.
		FTransform MassPose = MassRest[Index] * BodyTransform;
		MassPose.AddToTranslation(Mass[Index].Offset);
		Body->SetBoneTransformByName(MassBones[Index], MassPose, EBoneSpaces::ComponentSpace);
		// 물갈퀴 가운데는 몸 옆구리와 윗마디 사이 가운데에 있다가 처지거나 벌어진다.
		FTransform Upper = SegmentWorld[Index][0];
		Upper.AddToTranslation(ToComponent);
		FTransform WebPose;
		WebPose.Blend(WebRestBody[Index] * BodyTransform, WebRestUpper[Index] * Upper, 0.5f);
		WebPose.AddToTranslation(Web[Index].Offset);
		Body->SetBoneTransformByName(WebBones[Index], WebPose, EBoneSpaces::ComponentSpace);
		// 패드 막 끝은 붙어 있는 동안 면에 남고, 놓인 뒤에는 늘어났던 만큼에서 패드로 돌아온다.
		FTransform SkinPose = bSkinStuck[Index] ? SkinStuckWorld[Index] : DefaultSkinWorld(Index);
		if (!bSkinStuck[Index])
		{
			SkinPose.AddToTranslation(Skin[Index].Offset);
			const float Back = FMath::Clamp(Skin[Index].Offset.Size() / IGLining::SkinStretchMax, 0.0f, 1.0f);
			SkinPose.SetRotation(FQuat::Slerp(SkinPose.GetRotation(), SkinStuckWorld[Index].GetRotation(), Back));
		}
		SkinPose.AddToTranslation(ToComponent);
		Body->SetBoneTransformByName(SkinBones[Index], SkinPose, EBoneSpaces::ComponentSpace);
	}
	for (int32 Side = 0; Side < SkirtCount; ++Side)
	{
		FTransform SkirtPose = SkirtRest[Side] * BodyTransform;
		SkirtPose.AddToTranslation(Skirt[Side].Offset);
		Body->SetBoneTransformByName(SkirtBones[Side], SkirtPose, EBoneSpaces::ComponentSpace);
	}
}

bool AIGLiningEntity::AreSoftPartsSettled() const
{
	if (!bHasSoftBones)
	{
		return true;
	}
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		if (bSkinStuck[Index] || Skin[Index].Offset.SizeSquared() > 0.25f || Skin[Index].Velocity.SizeSquared() > 0.25f
			|| Mass[Index].Velocity.SizeSquared() > 0.25f || Web[Index].Velocity.SizeSquared() > 0.25f)
		{
			return false;
		}
	}
	for (const FSoftPoint& Point : Skirt)
	{
		if (Point.Velocity.SizeSquared() > 0.25f)
		{
			return false;
		}
	}
	return true;
}

void AIGLiningEntity::KickSoftParts(const int32 Leg, const float Strength)
{
	const FVector Gravity = SoftGravity();
	if (!bHasSoftBones || Gravity.SizeSquared() < 0.04f)
	{
		return;
	}
	const FVector Down = Gravity.GetSafeNormal();
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		Mass[Index].Velocity += Down * (Index == Leg ? 32.0f : 18.0f) * Strength;
	}
	for (FSoftPoint& Point : Skirt)
	{
		Point.Velocity += Down * 22.0f * Strength;
	}
	Web[Leg].Velocity += Down * 24.0f * Strength;
}

float AIGLiningEntity::PeelHoldFraction() const
{
	return State == EIGLiningState::Peeling ? IGLining::FirstPeelHold : IGLining::PeelHold;
}

float AIGLiningEntity::LegReach(const int32 Leg, const FVector& Center, const FQuat& Rotation) const
{
	return ReachTo(Leg, Legs[Leg].Pad, Legs[Leg].Normal, Center, Rotation);
}

float AIGLiningEntity::ReachTo(const int32 Leg, const FVector& Pad, const FVector& Normal, const FVector& Center,
	const FQuat& Rotation) const
{
	const FVector Hip = HipWorld(Leg, Center, Rotation);
	// SolveLeg와 같은 수로 짚을 패드에서 발목 자리를 거꾸로 구한다.
	FVector PadForward = Pad - Hip;
	PadForward -= Normal * FVector::DotProduct(PadForward, Normal);
	if (!PadForward.Normalize())
	{
		PadForward = Rotation.RotateVector(RestOutward[Leg]);
	}
	const FQuat PadQuat = IGLining::FrameRotation(RestOutward[Leg], FVector::UpVector, PadForward, Normal);
	const FVector Ankle = Pad - PadQuat.RotateVector(RestPad[Leg] - RestSegment[Leg][2].GetLocation());
	return FVector::Distance(Hip, Ankle) / FMath::Max(Length1[Leg] + Length2[Leg], 1.0f);
}

bool AIGLiningEntity::PlantedLegsReach(const FVector& Center, const FQuat& Rotation) const
{
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		if (!Legs[Leg].bSwinging && Legs[Leg].Fold < 0.5f && Leg != PinnedLeg && LegReach(Leg, Center, Rotation) > 0.97f)
		{
			return false;
		}
	}
	return true;
}

FTransform AIGLiningEntity::DefaultSkinWorld(const int32 Leg) const
{
	return SkinRest[Leg] * SegmentWorld[Leg][2];
}

void AIGLiningEntity::SpreadTo(
	const FVector& InBodyCenter,
	const FVector& InBodyUp,
	const FVector& InBodyForward,
	const TArray<FVector>& Pads,
	const TArray<FVector>& PadNormals,
	const float Seconds)
{
	if (!LoadBody() || Pads.Num() != LegCount || PadNormals.Num() != LegCount)
	{
		return;
	}
	SetObstructions(HeldGap, false);
	MoveFromCenter = BodyCenter;
	MoveFromRotation = BodyRotation;
	MoveToCenter = InBodyCenter;
	MoveToRotation = IGLining::FrameRotation(FVector::ForwardVector, FVector::UpVector, InBodyForward, InBodyUp);
	MoveSeconds = FMath::Max(Seconds, 0.5f);
	SpreadPads = Pads;
	SpreadNormals = PadNormals;
	bHasMoved = true;
	SetState(EIGLiningState::Spread);
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		StartSwing(Leg, Pads[Leg], PadNormals[Leg], 0.0f);
		// 대각선 둘이 먼저 옮겨 짚고, 그 둘이 버티는 동안 나머지 둘이 따른다.
		Legs[Leg].SwingAlpha = (Leg == 1 || Leg == 2) ? -1.0f : 0.0f;
	}
	PlayCue(TEXT("Lining_Creak"), 3, InBodyCenter, 0.95f, 0.82f);
	Caption(NSLOCTEXT("IGMissingFloor", "CaptionLiningSpread", "[쇠붙이 여러 개가 한꺼번에 긁히는 소리]"), InBodyCenter, 2.8f,
		LastCreakCaptionAt, 0.0f);
	StartTicking();
}

void AIGLiningEntity::UpdateSpread(const float DeltaSeconds)
{
	const float Alpha = FMath::Clamp(StateSeconds / MoveSeconds, 0.0f, 1.0f);
	BodyCenter = FMath::Lerp(MoveFromCenter, MoveToCenter, IGLining::Smooth(Alpha));
	BodyRotation = FQuat::Slerp(MoveFromRotation, MoveToRotation, IGLining::Smooth(Alpha));
	if (Alpha >= 1.0f && StateSeconds - DeltaSeconds < MoveSeconds)
	{
		OnSettled.Broadcast(this);
	}
}

void AIGLiningEntity::PinKnee(const int32 Leg, const FVector& KneeWorld)
{
	if (Leg < 0 || Leg >= LegCount)
	{
		return;
	}
	PinnedLeg = Leg;
	PinnedKnee = KneeWorld;
	PlayCue(TEXT("Lining_FrameCatch"), 1, KneeWorld, 0.9f, 1.0f);
	StartTicking();
}

void AIGLiningEntity::ReleasePin()
{
	PinnedLeg = INDEX_NONE;
}

void AIGLiningEntity::RetreatTo(const FVector& Point, const FVector& Normal, const FVector& Forward, const float Seconds)
{
	if (!LoadBody())
	{
		return;
	}
	SetObstructions(HeldGap, false);
	MoveFromCenter = BodyCenter;
	MoveFromRotation = BodyRotation;
	MoveToCenter = Point + Normal.GetSafeNormal() * IGLining::PressStandOff;
	MoveToRotation = IGLining::FrameRotation(FVector::ForwardVector, FVector::UpVector, Forward, Normal.GetSafeNormal());
	MoveSeconds = FMath::Max(Seconds, 0.5f);
	for (FLeg& Leg : Legs)
	{
		Leg.bSwinging = false;
		Leg.FoldFrom = Leg.Fold;
	}
	HeldGap = NAME_None;
	TargetGap = NAME_None;
	SetState(EIGLiningState::Retreating);
	PlayCue(TEXT("Lining_Creak"), 3, BodyCenter, 0.8f, 0.86f);
	StartTicking();
}

void AIGLiningEntity::UpdateRetreating(const float DeltaSeconds)
{
	const float Alpha = FMath::Clamp(StateSeconds / MoveSeconds, 0.0f, 1.0f);
	const float T = IGLining::Smooth(Alpha);
	// 바닥 쪽으로 무게를 빼며 미끄러지듯 밀려난다.
	BodyCenter = FMath::Lerp(MoveFromCenter, MoveToCenter, T) - FVector::UpVector * FMath::Sin(T * PI) * 18.0f;
	BodyRotation = FQuat::Slerp(MoveFromRotation, MoveToRotation, T);
	for (FLeg& Leg : Legs)
	{
		Leg.Fold = FMath::Lerp(Leg.FoldFrom, 1.0f, FMath::Clamp(Alpha * 1.4f, 0.0f, 1.0f));
	}
	if (Alpha >= 1.0f)
	{
		ReleasePin();
		SetState(EIGLiningState::Folded);
		OnSettled.Broadcast(this);
	}
}

void AIGLiningEntity::Hide()
{
	SetObstructions(HeldGap, false);
	SetObstructions(TargetGap, false);
	SetState(EIGLiningState::Hidden);
	SetActorHiddenInGame(true);
	SetBlockersEnabled(false);
	ResetSoftParts();
	SetActorTickEnabled(false);
}

void AIGLiningEntity::ReleaseGrip()
{
	if (State == EIGLiningState::Gripping)
	{
		const EIGLiningState Resume = StateBeforeGrip;
		State = Resume;
		StartTicking();
	}
}

void AIGLiningEntity::PoseBody()
{
	if (!Body)
	{
		return;
	}
	SetActorLocation(BodyCenter, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(FRotator::ZeroRotator);
	const FVector Offset = RestBody.GetLocation();
	// 반입한 뼈는 쉬는 자세의 배율이 100이다(미터 뼈대를 센티미터로 받은 뿌리 배율). 그 배율에 몸의 눌림을
	// 곱한다. 눌림은 몸 기준 축(X 앞, Y 옆, Z 면 법선)이라 뼈의 축으로 옮겨 건다.
	const FQuat RestRotation = RestBody.GetRotation();
	FVector LocalSquash;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const FVector Dir = RestRotation.RotateVector(FVector(Axis == 0 ? 1.0f : 0.0f, Axis == 1 ? 1.0f : 0.0f, Axis == 2 ? 1.0f : 0.0f)).GetAbs();
		LocalSquash[Axis] = BodySquash.X * Dir.X + BodySquash.Y * Dir.Y + BodySquash.Z * Dir.Z;
	}
	const FVector Settle = BodyRotation.GetUpVector() * (1.0f - BodySquash.Z) * IGLining::PressStandOff;
	const FTransform BodyTransform(BodyRotation * RestRotation, Offset - Settle, RestBody.GetScale3D() * LocalSquash);
	Body->SetBoneTransformByName(BodyBone, BodyTransform, EBoneSpaces::ComponentSpace);
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		SolveLeg(Leg);
	}
	bSegmentsPosed = true;
	PoseSoftParts(BodyTransform);
	UpdateColliders();
}

void AIGLiningEntity::SolveLeg(const int32 Index)
{
	FLeg& Leg = Legs[Index];
	const FVector Up = BodyRotation.GetUpVector();
	const FVector Hip = HipWorld(Index, BodyCenter, BodyRotation);
	const FVector RestHip = RestSegment[Index][0].GetLocation();
	const FVector RestKnee = RestSegment[Index][1].GetLocation();
	const FVector RestAnkle = RestSegment[Index][2].GetLocation();
	const FVector OutwardWorld = BodyRotation.RotateVector(RestOutward[Index]);

	// 짚은 자세: 패드가 면을 따르고, 발목은 패드에서 거꾸로, 무릎은 두 마디 IK로 푼다.
	FVector PadForward = Leg.Pad - Hip;
	PadForward -= Leg.Normal * FVector::DotProduct(PadForward, Leg.Normal);
	if (!PadForward.Normalize())
	{
		PadForward = OutwardWorld;
	}
	FQuat PadQuat = IGLining::FrameRotation(RestOutward[Index], FVector::UpVector, PadForward, Leg.Normal);
	FVector PadPose = Leg.Pad - Leg.Normal * 1.8f * IGLining::Smooth(Leg.PlantKick);
	const float Hold = PeelHoldFraction();
	if (Leg.bSwinging && Leg.SwingAlpha > 0.0f && Leg.SwingAlpha < Hold + 0.2f)
	{
		// 붙어 있던 패드는 바깥 가장자리를 짚은 채 몸 쪽 가장자리부터 들린다. 그 틈으로 막이 먼저 벌어진다.
		const float Rise = Leg.SwingAlpha < Hold ? IGLining::Smooth(Leg.SwingAlpha / Hold)
			: 1.0f - IGLining::Smooth((Leg.SwingAlpha - Hold) / 0.2f);
		const FQuat Peel(FVector::CrossProduct(Leg.Normal, PadForward).GetSafeNormal(),
			FMath::DegreesToRadians(IGLining::PeelTiltDegrees) * Rise * Leg.LiftScale);
		const FVector Pivot = PadPose + PadForward * IGLining::PadOuterEdge;
		PadPose = Pivot + Peel.RotateVector(PadPose - Pivot);
		PadQuat = Peel * PadQuat;
	}
	FVector Ankle = PadPose - PadQuat.RotateVector(RestPad[Index] - RestAnkle);
	const FVector Pole = (OutwardWorld * 0.35f + Up * 0.9f).GetSafeNormal();
	FVector Knee = IGLining::TwoBoneKnee(Hip, Ankle, Length1[Index], Length2[Index], Pole);
	FQuat Segment3 = PadQuat;

	// 접은 자세(몸 기준 정해진 자리)와 섞는다.
	if (Leg.Fold > 0.001f)
	{
		FVector FoldKnee, FoldAnkle, FoldPad;
		IGLining::FoldedJoints(Index, HipLocal[Index], FoldKnee, FoldAnkle, FoldPad);
		const FVector WorldKnee = BodyCenter + BodyRotation.RotateVector(FoldKnee);
		const FVector WorldAnkle = BodyCenter + BodyRotation.RotateVector(FoldAnkle);
		const float F = IGLining::Smooth(Leg.Fold);
		Knee = FMath::Lerp(Knee, WorldKnee, F);
		Ankle = FMath::Lerp(Ankle, WorldAnkle, F);
		Segment3 = FQuat::Slerp(PadQuat, BodyRotation, F);
		if (Leg.Fold > 0.999f)
		{
			Leg.Pad = BodyCenter + BodyRotation.RotateVector(FoldPad);
			Leg.Normal = Up;
		}
	}
	if (PinnedLeg == Index)
	{
		Knee = PinnedKnee;
	}
	// 마디 길이를 지킨다(손이 닿지 않으면 다리를 곧게 뻗고 패드가 따라온다).
	Knee = Hip + (Knee - Hip).GetSafeNormal() * Length1[Index];
	Ankle = Knee + (Ankle - Knee).GetSafeNormal() * Length2[Index];

	const FVector Bend = Pole;
	const FQuat Segment1 = IGLining::FrameRotation(RestKnee - RestHip, FVector::UpVector, Knee - Hip, Bend);
	const FQuat Segment2 = IGLining::FrameRotation(RestAnkle - RestKnee, FVector::UpVector, Ankle - Knee, Bend);
	// 월드 자리로 적어 두고(물갈퀴·패드 막 뼈가 따른다) 컴포넌트 공간으로 놓는다.
	SegmentWorld[Index][0] = FTransform(Segment1 * RestSegment[Index][0].GetRotation(), Hip, RestSegment[Index][0].GetScale3D());
	SegmentWorld[Index][1] = FTransform(Segment2 * RestSegment[Index][1].GetRotation(), Knee, RestSegment[Index][1].GetScale3D());
	SegmentWorld[Index][2] = FTransform(Segment3 * RestSegment[Index][2].GetRotation(), Ankle, RestSegment[Index][2].GetScale3D());
	const FVector Offset = RestBody.GetLocation() - BodyCenter;
	for (int32 Segment = 0; Segment < 3; ++Segment)
	{
		FTransform Pose = SegmentWorld[Index][Segment];
		Pose.AddToTranslation(Offset);
		Body->SetBoneTransformByName(SegmentBones[Index][Segment], Pose, EBoneSpaces::ComponentSpace);
	}
	Leg.Hip = Hip;
	Leg.Knee = Knee;
	Leg.Ankle = Ankle;
	// 패드 덩어리는 끝 마디에 붙어 돈다(쉬는 자세의 발목→패드를 그대로 돌린다).
	Leg.PadCenter = Ankle + Segment3.RotateVector(RestPad[Index] - RestAnkle);
	Leg.PadFrame = Segment3 * FRotationMatrix::MakeFromXZ(RestOutward[Index], FVector::UpVector).ToQuat();
	// 짚고 있는 발이 짚은 자리에서 벗어났는가(다리가 닿지 않으면 마디 길이를 지키느라 발이 끌려온다).
	if (!Leg.bSwinging && Leg.Fold < 0.01f && PinnedLeg != Index)
	{
		MaxPadSlip = FMath::Max(MaxPadSlip, FVector::Distance(Leg.PadCenter, PadPose));
		if (MaxPadSlip >= NextSlipLog)
		{
			UE_LOG(LogIndieGame, Display, TEXT("LINING_SLIP leg=%d state=%d pulling=%d blocked=%d slip=%.1f reach=%.2f"),
				Index, static_cast<int32>(State), bPulling ? 1 : 0, BlockedPulls, MaxPadSlip, LegReach(Index, BodyCenter, BodyRotation));
			NextSlipLog = MaxPadSlip * 2.0f;
		}
	}
}

void AIGLiningEntity::UpdateColliders()
{
	// 몸판 상자는 눌리고 늘어난 몸을 따라가고(벽에 붙은 바닥면은 제자리), 처진 살 쪽으로 반만큼 비킨다.
	const FVector Extent = IGLining::BodyHalf * BodySquash;
	if (!BodyBlocker->GetUnscaledBoxExtent().Equals(Extent, 0.3f))
	{
		BodyBlocker->SetBoxExtent(Extent, false);
	}
	FVector Sag = FVector::ZeroVector;
	for (const FSoftPoint& Point : Mass)
	{
		Sag += Point.Offset;
	}
	const FVector BoxCenter = BodyCenter - BodyRotation.GetUpVector() * (1.0f - BodySquash.Z) * IGLining::PressStandOff
		+ Sag * (0.5f / LegCount);
	BodyBlocker->SetWorldLocationAndRotation(BoxCenter, BodyRotation, false, nullptr, ETeleportType::TeleportPhysics);
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		const FLeg& Leg = Legs[Index];
		const FVector Segments[2][2] = {{Leg.Hip, Leg.Knee}, {Leg.Knee, Leg.Ankle}};
		for (int32 Part = 0; Part < 2; ++Part)
		{
			UCapsuleComponent* Capsule = LegBlockers.IsValidIndex(Index * 2 + Part) ? LegBlockers[Index * 2 + Part].Get() : nullptr;
			if (!Capsule)
			{
				continue;
			}
			const FVector A = Segments[Part][0];
			const FVector B = Segments[Part][1];
			const FVector Dir = B - A;
			const float Half = Dir.Size() * 0.5f;
			Capsule->SetCapsuleSize(IGLining::LegRadius, FMath::Max(Half, IGLining::LegRadius + 1.0f));
			Capsule->SetWorldLocationAndRotation((A + B) * 0.5f, FRotationMatrix::MakeFromZ(Dir.GetSafeNormal()).ToQuat(),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
		// 패드 덩어리는 짚은 면에 붙은 납작한 상자다.
		if (UBoxComponent* Box = PadBlockers.IsValidIndex(Index) ? PadBlockers[Index].Get() : nullptr)
		{
			Box->SetWorldLocationAndRotation(Leg.PadCenter + Leg.PadFrame.GetUpVector() * IGLining::PadHalf.Z, Leg.PadFrame,
				false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
#if ENABLE_DRAW_DEBUG
	if (IGLining::DrawCollision() && GetWorld())
	{
		// 쉬는 동안은 0.1초에 한 번 틱하므로 선을 그만큼 남긴다.
		const float Life = PrimaryActorTick.TickInterval > 0.0f ? PrimaryActorTick.TickInterval + 0.05f : -1.0f;
		DrawDebugBox(GetWorld(), BodyBlocker->GetComponentLocation(), BodyBlocker->GetScaledBoxExtent(), BodyBlocker->GetComponentQuat(),
			FColor::Cyan, false, Life, 0, 1.0f);
		for (const UCapsuleComponent* Capsule : LegBlockers)
		{
			DrawDebugCapsule(GetWorld(), Capsule->GetComponentLocation(), Capsule->GetScaledCapsuleHalfHeight(),
				Capsule->GetScaledCapsuleRadius(), Capsule->GetComponentQuat(), FColor::Yellow, false, Life, 0, 1.0f);
		}
		for (const UBoxComponent* Box : PadBlockers)
		{
			DrawDebugBox(GetWorld(), Box->GetComponentLocation(), Box->GetScaledBoxExtent(), Box->GetComponentQuat(),
				FColor::Orange, false, Life, 0, 1.0f);
		}
	}
#endif
}

void AIGLiningEntity::SetBlockersEnabled(const bool bEnabled)
{
	const ECollisionEnabled::Type Mode = bEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
	BodyBlocker->SetCollisionEnabled(Mode);
	for (UCapsuleComponent* Capsule : LegBlockers)
	{
		Capsule->SetCollisionEnabled(Mode);
	}
	for (UBoxComponent* Box : PadBlockers)
	{
		Box->SetCollisionEnabled(Mode);
	}
}

void AIGLiningEntity::CheckGrip()
{
	if (!bGripEnabled || !IsMoving() || State == EIGLiningState::Gripping || State == EIGLiningState::Retreating)
	{
		return;
	}
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		return;
	}
	const FVector Chest = Player->GetActorLocation();
	bool bCaught = false;
	FVector CaughtBy = BodyCenter;
	// 옮기는 패드가 닿거나, 움직이는 몸판이 사람을 덮친다.
	for (const FLeg& Leg : Legs)
	{
		if (Leg.bSwinging && Leg.SwingAlpha > 0.35f && FVector::Distance(Leg.Pad, Chest) < IGLining::GripRadius + 24.0f)
		{
			bCaught = true;
			CaughtBy = Leg.Pad;
			break;
		}
	}
	if (!bCaught && (State == EIGLiningState::Stepping || State == EIGLiningState::Pressing || State == EIGLiningState::Bracing))
	{
		const FVector Local = BodyRotation.UnrotateVector(Chest - BodyCenter);
		bCaught = FMath::Abs(Local.X) < IGLining::BodyHalf.X + IGLining::GripRadius
			&& FMath::Abs(Local.Y) < IGLining::BodyHalf.Y + IGLining::GripRadius
			&& FMath::Abs(Local.Z) < IGLining::BodyHalf.Z + IGLining::GripDepth;
	}
	if (!bCaught)
	{
		return;
	}
	StateBeforeGrip = State;
	State = EIGLiningState::Gripping;
	UE_LOG(LogIndieGame, Display, TEXT("LINING_GRIP at=%s state=%d by=%s body=%s"), *Chest.ToCompactString(),
		static_cast<int32>(StateBeforeGrip), *CaughtBy.ToCompactString(), *BodyCenter.ToCompactString());
	PlayCue(TEXT("Lining_Press"), 1, Chest, 1.0f, 0.82f);
	OnGrippedPlayer.Broadcast(this);
}

void AIGLiningEntity::SetObstructions(const FName GapId, const bool bObstructed)
{
	if (GapId.IsNone())
	{
		return;
	}
	if (const FIGLiningGap* Gap = FindGap(GapId))
	{
		if (AIGAccessHatch* Hatch = Gap->Hatch.Get())
		{
			Hatch->SetObstructed(bObstructed);
		}
	}
}

void AIGLiningEntity::SealGap(const FName GapId)
{
	for (FIGLiningGap& Gap : Gaps)
	{
		if (Gap.Id == GapId)
		{
			Gap.bSealed = true;
			if (AIGAccessHatch* Hatch = Gap.Hatch.Get())
			{
				Hatch->SetSealed(true);
			}
			UE_LOG(LogIndieGame, Display, TEXT("LINING_SEALED %s"), *GapId.ToString());
		}
	}
	RefreshGapDrafts();
}

void AIGLiningEntity::PlayCue(const TCHAR* Sample, const int32 Variants, const FVector& Where, const float Volume, const float Pitch)
{
	if (!bAudible)
	{
		return;
	}
	USoundBase* Sound = nullptr;
	const FString Name(Sample);
	if (Variants > 1)
	{
		Sound = IGAudio::SampleVariant(*Name, Variants, CueSeed++);
	}
	else
	{
		Sound = IGAudio::Sample(*Name);
	}
	if (!Sound)
	{
		if (Name == TEXT("Lining_Plant"))
		{
			Sound = UIGToneSequenceSoundWave::CreateKickedPropKnock(this, false);
		}
		else if (Name == TEXT("Lining_Creak") || Name == TEXT("Lining_FrameCatch"))
		{
			Sound = UIGToneSequenceSoundWave::CreateSettleTimberCreak(this);
		}
		else
		{
			Sound = UIGToneSequenceSoundWave::CreatePackingTapeRip(this);
		}
	}
	IGAudio::SpawnExpendableOneShotAt(this, Sound, Where, Volume, Pitch, 120.0f, 1500.0f, EIGAudioBus::Entity);
}

void AIGLiningEntity::RefreshGapDrafts()
{
	TSet<FName> Wanted;
	if (State != EIGLiningState::Hidden && bAudible && GetWorld())
	{
		for (const FIGLiningGap& Gap : Gaps)
		{
			if (!Gap.bSealed && IsGapOpen(Gap.Id))
			{
				Wanted.Add(Gap.Id);
			}
		}
	}
	for (auto It = GapDrafts.CreateIterator(); It; ++It)
	{
		if (!Wanted.Contains(It.Key()))
		{
			if (UAudioComponent* Audio = It.Value().Get())
			{
				// 막히면 바람이 먹히듯 줄어 끊긴다.
				Audio->bAutoDestroy = true;
				Audio->FadeOut(0.6f, 0.0f);
			}
			It.RemoveCurrent();
		}
	}
	USoundBase* Wind = Wanted.Num() > 0 ? IGAudio::Sample(TEXT("Wind_Gap")) : nullptr;
	if (!Wind)
	{
		return;
	}
	for (const FName Id : Wanted)
	{
		const FIGLiningGap* Gap = FindGap(Id);
		if (!Gap || GapDrafts.Contains(Id))
		{
			continue;
		}
		UAudioComponent* Audio = NewObject<UAudioComponent>(this);
		Audio->bAutoActivate = false;
		Audio->bAutoDestroy = false;
		Audio->bAllowSpatialization = true;
		Audio->SetSound(Wind);
		Audio->AttenuationSettings = IGAudio::MakeAttenuation(this, 50.0f, 420.0f, EIGAudioBus::World);
		Audio->SetUsingAbsoluteLocation(true);
		Audio->RegisterComponent();
		Audio->SetWorldLocation(Gap->Center + Gap->Normal.GetSafeNormal() * 6.0f);
		// 넓은 틈일수록 낮게 운다.
		Audio->SetPitchMultiplier(FMath::GetMappedRangeValueClamped(FVector2D(30.0f, 90.0f), FVector2D(1.15f, 0.8f), FMath::Sqrt(Gap->Area())));
		Audio->FadeIn(1.2f, 0.16f);
		GapDrafts.Add(Id, Audio);
	}
}

void AIGLiningEntity::Caption(const FText& Text, const FVector& Where, const float Seconds, double& LastAt, const float MinInterval)
{
	const UWorld* World = GetWorld();
	if (!World || !bAudible)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now - LastAt < MinInterval)
	{
		return;
	}
	LastAt = Now;
	AIGHorrorHUD::PushAudioCaptionAt(this, Text, Seconds, Where);
}

FString AIGLiningEntity::FindLegObstruction(const int32 Leg) const
{
	UWorld* World = GetWorld();
	if (!World || Leg < 0 || Leg >= LegCount)
	{
		return FString();
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningLegCheck), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	const auto Blocked = [World, &Params, &Objects](const FVector& From, const FVector& To, const float EndSlack) -> FString
	{
		const FVector Delta = To - From;
		const float Length = Delta.Size();
		if (Length <= EndSlack + 1.0f)
		{
			return FString();
		}
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, From, To - Delta / Length * EndSlack, Objects, Params))
		{
			return FString::Printf(TEXT("%s@%s"), Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("none"),
				*Hit.ImpactPoint.ToCompactString());
		}
		return FString();
	};
	// 풀어 둔 마디 셋(엉덩이→무릎→발목→패드)을 그대로 따라간다.
	const FLeg& L = Legs[Leg];
	FString Found = Blocked(L.Hip, L.Knee, 0.0f);
	if (Found.IsEmpty())
	{
		Found = Blocked(L.Knee, L.Ankle, 0.0f);
	}
	if (Found.IsEmpty())
	{
		Found = Blocked(L.Ankle, L.Pad, 6.0f);
	}
	return Found;
}

void AIGLiningEntity::LogPose(const TCHAR* Tag) const
{
	if (!Body)
	{
		return;
	}
	FString Line = FString::Printf(TEXT("LINING_POSE %s actor=%s comp=%s"), Tag,
		*GetActorLocation().ToCompactString(), *Body->GetComponentLocation().ToCompactString());
	for (int32 Index = 0; Index < Body->GetNumBones(); ++Index)
	{
		const FName Name = Body->GetBoneName(Index);
		const FTransform Local = Body->BoneSpaceTransforms.IsValidIndex(Index) ? Body->BoneSpaceTransforms[Index] : FTransform::Identity;
		Line += FString::Printf(TEXT(" | %s w=%s l=%s nan=%d"), *Name.ToString(), *Body->GetBoneLocation(Name).ToCompactString(),
			*Local.GetLocation().ToCompactString(), Local.ContainsNaN() ? 1 : 0);
	}
	UE_LOG(LogIndieGame, Display, TEXT("%s"), *Line);
}

void AIGLiningEntity::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const TPair<FName, TObjectPtr<UAudioComponent>>& Draft : GapDrafts)
	{
		if (UAudioComponent* Audio = Draft.Value.Get())
		{
			Audio->Stop();
		}
	}
	GapDrafts.Reset();
	SetObstructions(HeldGap, false);
	SetObstructions(TargetGap, false);
	// 검사 실행마다 짚은 발이 끌려간 거리와 걷는 동안 버틴 발 수를 남긴다(관리실·302호·5층 어디서든).
	if (bHasMoved)
	{
		UE_LOG(LogIndieGame, Display, TEXT("LINING_CONTACT max_slip=%.1f min_hold=%d"), MaxPadSlip, MinPadsHoldingWhileWalking);
	}
	Super::EndPlay(EndPlayReason);
}
