#include "Entity/IGClipAudit.h"

#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "IndieGame.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"

namespace IGClipAudit
{
	// 살 표본 수. 정점을 고르게 건너뛰며 뽑는다. 프레임마다 이만큼 선을 긋는다.
	constexpr int32 MaxSamples = 400;
	// 이보다 짧게 지나간 것은 화면에서 안 보인다(손바닥이 바닥에 얹힌 정도).
	constexpr float VisibleDepth = 2.0f;
	// 보고에 줄줄이 적는 부위 수와 원인 수.
	constexpr int32 ReportedParts = 12;
	constexpr int32 ReportedCauses = 24;

	bool IsRequested()
	{
		static const bool bRequested = []
		{
			const TCHAR* const Flags[] = {
				TEXT("IGClipAudit"), TEXT("IGListenerPursuitProbe"), TEXT("IGGuestDoorsProbe"),
				TEXT("IGCaptureVarietyProbe"), TEXT("IGHorrorRegressionProbe"), TEXT("IGStairSensorProbe"),
				TEXT("IGNightFourHammerProbe"), TEXT("IGBathroomProbe"), TEXT("IGFireDoorProbe"), TEXT("IGNightFlowProbe")};
			for (const TCHAR* Flag : Flags)
			{
				if (FParse::Param(FCommandLine::Get(), Flag))
				{
					return true;
				}
			}
			return false;
		}();
		return bRequested;
	}

	bool IsTraceRequested()
	{
		static const bool bTrace = FParse::Param(FCommandLine::Get(), TEXT("IGClipAuditTrace"));
		return bTrace;
	}

	// 몸 둘레를 나누는 칸. 5도마다 하나.
	constexpr int32 FootprintSectors = 72;

	// 보이는 메시에 처음 막히는 자리. 숨은 막이나 트리거, 충돌만 있는 상자는 빼고 다시 긋는다.
	bool TraceVisible(UWorld& World, const FVector& From, const FVector& To, FCollisionQueryParams Params, FHitResult& OutHit)
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
		for (int32 Try = 0; Try < 6; ++Try)
		{
			if (!World.LineTraceSingleByObjectType(OutHit, From, To, Objects, Params))
			{
				return false;
			}
			const UPrimitiveComponent* Component = OutHit.GetComponent();
			const AActor* Actor = OutHit.GetActor();
			if (Cast<UMeshComponent>(Component) && Component->IsVisible() && !Component->bHiddenInGame
				&& !(Actor && Actor->IsHidden()))
			{
				return true;
			}
			Params.AddIgnoredComponent(Component);
		}
		return false;
	}

	FString HitName(const FHitResult& Hit)
	{
		const AActor* Actor = Hit.GetActor();
		const UPrimitiveComponent* Component = Hit.GetComponent();
		return FString::Printf(TEXT("%s.%s"), Actor ? *Actor->GetName() : TEXT("none"),
			Component ? *Component->GetName() : TEXT("none"));
	}
}

bool FIGSkinSamples::IsPreparedFor(const USkinnedMeshComponent& Mesh) const
{
	return Mesh.GetSkinnedAsset() && PreparedAsset.Get() == Mesh.GetSkinnedAsset();
}

void FIGSkinSamples::Prepare(USkinnedMeshComponent& Mesh, const int32 MaxSamples)
{
	PreparedAsset = Mesh.GetSkinnedAsset();
	Samples.Reset();
	VertexCount = 0;
	FSkeletalMeshRenderData* RenderData = Mesh.GetSkeletalMeshRenderData();
	FSkinWeightVertexBuffer* Weights = Mesh.GetSkinWeightBuffer(0);
	const FSkeletalMeshLODRenderData* Lod = RenderData && RenderData->LODRenderData.Num() > 0 ? &RenderData->LODRenderData[0] : nullptr;
	if (!Lod || !Weights || !Lod->StaticVertexBuffers.PositionVertexBuffer.GetVertexData() || !Weights->GetNeedsCPUAccess()
		|| !Weights->GetDataVertexBuffer() || !Weights->GetDataVertexBuffer()->GetWeightData())
	{
		return;
	}
	VertexCount = static_cast<int32>(Lod->GetNumVertices());
	const int32 Stride = FMath::Max(1, VertexCount / FMath::Max(1, MaxSamples));
	const uint32 Influences = Weights->GetMaxBoneInfluences();
	for (int32 Vertex = 0; Vertex < VertexCount; Vertex += Stride)
	{
		int32 Section = 0;
		int32 InSection = 0;
		Lod->GetSectionFromVertexIndex(Vertex, Section, InSection);
		if (!Lod->RenderSections.IsValidIndex(Section))
		{
			continue;
		}
		const TArray<FBoneIndexType>& BoneMap = Lod->RenderSections[Section].BoneMap;
		int32 Best = INDEX_NONE;
		uint16 BestWeight = 0;
		for (uint32 Influence = 0; Influence < Influences; ++Influence)
		{
			const uint16 Weight = Weights->GetBoneWeight(static_cast<uint32>(Vertex), Influence);
			const int32 Local = static_cast<int32>(Weights->GetBoneIndex(static_cast<uint32>(Vertex), Influence));
			if (Weight > BestWeight && BoneMap.IsValidIndex(Local))
			{
				BestWeight = Weight;
				Best = BoneMap[Local];
			}
		}
		if (Best != INDEX_NONE)
		{
			Samples.Add({Vertex, Best});
		}
	}
}

void FIGSkinSamples::Evaluate(USkinnedMeshComponent& Mesh, TArray<FVector>& OutPositions) const
{
	OutPositions.Reset(Samples.Num());
	FSkeletalMeshRenderData* RenderData = Mesh.GetSkeletalMeshRenderData();
	FSkinWeightVertexBuffer* Weights = Mesh.GetSkinWeightBuffer(0);
	if (Samples.IsEmpty() || !RenderData || RenderData->LODRenderData.Num() == 0 || !Weights)
	{
		return;
	}
	const FSkeletalMeshLODRenderData& Lod = RenderData->LODRenderData[0];
	TArray<FMatrix44f> RefToLocals;
	Mesh.CacheRefToLocalMatrices(RefToLocals);
	const FTransform& ToWorld = Mesh.GetComponentTransform();
	for (const FSample& Sample : Samples)
	{
		OutPositions.Add(ToWorld.TransformPosition(FVector(
			USkinnedMeshComponent::GetSkinnedVertexPosition(&Mesh, Sample.Vertex, Lod, *Weights, RefToLocals))));
	}
}

void FIGClipAudit::Start(const TCHAR* InWho)
{
	bOn = IGClipAudit::IsRequested();
	Who = InWho;
}

void FIGClipAudit::Prepare(USkinnedMeshComponent& Mesh)
{
	const USkinnedAsset* Asset = Mesh.GetSkinnedAsset();
	PreparedAsset = Asset;
	Parents.Reset();
	Handles.Reset();
	BoneNames.Reset();
	const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
	const TArray<FTransform>& RefPose = Skeleton.GetRefBonePose();
	TArray<FTransform> RefComponent;
	for (int32 Bone = 0; Bone < Skeleton.GetNum(); ++Bone)
	{
		const int32 Parent = Skeleton.GetParentIndex(Bone);
		Parents.Add(Parent);
		BoneNames.Add(Skeleton.GetBoneName(Bone).ToString());
		RefComponent.Add(Parent == INDEX_NONE ? RefPose[Bone] : RefPose[Bone] * RefComponent[Parent]);
		// 뼈대 뿌리와, 그 아래 원점에 그대로 놓인 root 뼈는 바닥에 닿은 손잡이다.
		Handles.Add(Parent == INDEX_NONE || (Handles[Parent] && RefComponent[Bone].GetLocation().Size() < 1.0f));
	}
	Skin.Prepare(Mesh, IGClipAudit::MaxSamples);
	if (Skin.Samples.IsEmpty())
	{
		UE_LOG(LogIndieGame, Display, TEXT("CLIP_AUDIT_READY who=%s bones=%d skin=unavailable"), *Who, Parents.Num());
		return;
	}
	UE_LOG(LogIndieGame, Display, TEXT("CLIP_AUDIT_READY who=%s bones=%d vertices=%d samples=%d"),
		*Who, Parents.Num(), Skin.VertexCount, Skin.Samples.Num());
}

void FIGClipAudit::Note(const FString& Key, const float Depth, const FString& Phase, const FVector& From, const FVector& To,
	const FVector& Impact, TSet<FString>& FrameKeys)
{
	FPart& Part = Parts.FindOrAdd(Key);
	bool bAlready = false;
	FrameKeys.Add(Key, &bAlready);
	if (!bAlready)
	{
		if (Part.Frames++ == 0)
		{
			UE_LOG(LogIndieGame, Display, TEXT("CLIP_AUDIT_FIRST who=%s key=%s phase=%s depth=%.1f from=%s to=%s impact=%s"),
				*Who, *Key, *Phase, Depth, *From.ToCompactString(), *To.ToCompactString(), *Impact.ToCompactString());
		}
	}
	if (Depth > Part.Deepest)
	{
		Part.Deepest = Depth;
		Part.Phase = Phase;
		Part.At = Impact;
	}
	if (Depth > Deepest)
	{
		Deepest = Depth;
		DeepestKey = Key;
	}
}

void FIGClipAudit::Measure(USkinnedMeshComponent* Mesh, const FVector& Core, const AActor* Ignored, const FString& Phase)
{
	if (!bOn || !Mesh || !Mesh->IsRegistered() || !Mesh->IsVisible() || !Mesh->GetSkinnedAsset())
	{
		return;
	}
	UWorld* World = Mesh->GetWorld();
	const AActor* Owner = Mesh->GetOwner();
	if (!World || (Owner && Owner->IsHidden()))
	{
		return;
	}
	if (PreparedAsset.Get() != Mesh->GetSkinnedAsset())
	{
		Prepare(*Mesh);
	}
	const int32 BoneCount = FMath::Min(Parents.Num(), Mesh->GetNumBones());
	if (BoneCount <= 1)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGClipAudit), false, Owner);
	if (Ignored)
	{
		Params.AddIgnoredActor(Ignored);
	}
	TArray<FVector, TInlineAllocator<64>> Joints;
	Joints.SetNum(BoneCount);
	for (int32 Bone = 0; Bone < BoneCount; ++Bone)
	{
		Joints[Bone] = Mesh->GetBoneTransform(Bone).GetLocation();
	}
	TSet<FString> FrameKeys;
	float FrameDepth = 0.0f;
	FString FrameCause;
	FString FrameKey;

	// 뼈 마디. 손잡이 뼈는 바닥에 놓여 있어 그 자식은 Core에서 긋는다. 마디가 막히면 그 아래 뼈와 살은
	// 통째로 묻힌 것으로 보고 따로 세지 않는다.
	TArray<bool, TInlineAllocator<64>> Buried;
	Buried.Init(false, BoneCount);
	for (int32 Bone = 1; Bone < BoneCount; ++Bone)
	{
		const int32 Parent = Parents[Bone];
		if (Parent == INDEX_NONE || Parent >= BoneCount)
		{
			continue;
		}
		if (!Handles[Parent] && Buried[Parent])
		{
			Buried[Bone] = true;
			continue;
		}
		const FVector From = Handles[Parent] ? Core : Joints[Parent];
		const FVector To = Joints[Bone];
		FHitResult Hit;
		if (FVector::DistSquared(From, To) < 4.0f || !IGClipAudit::TraceVisible(*World, From, To, Params, Hit))
		{
			continue;
		}
		// 손잡이 뼈는 몸이 아니다. Core에서 그 뼈까지는 Core가 물체 속일 때(문짝 속을 지나는 몸)만 센다. 다른 마디는
		// 출발점이 면에 얹혀 거리 0으로 맞은 것(닿은 손바닥 따위)만 뺀다.
		if (Handles[Bone] ? !Hit.bStartPenetrating : (Hit.Distance < 0.5f && !Hit.bStartPenetrating))
		{
			continue;
		}
		// 마디 끝에서 거꾸로 그어 빠져나오는 자리를 찾는다. 못 찾으면 관절이 물체 속이다.
		FHitResult Exit;
		const bool bExit = Hit.GetComponent() && Hit.GetComponent()->LineTraceComponent(
			Exit, To, Hit.ImpactPoint, FCollisionQueryParams(SCENE_QUERY_STAT(IGClipAuditExit), false));
		const float Depth = FVector::Distance(Hit.ImpactPoint, bExit && !Exit.bStartPenetrating ? Exit.ImpactPoint : To);
		Buried[Bone] = !Handles[Bone];
		if (Depth >= IGClipAudit::VisibleDepth)
		{
			const FString Key = Handles[Bone] ? FString::Printf(TEXT("core/%s"), *IGClipAudit::HitName(Hit))
				: FString::Printf(TEXT("bone/%s/%s"), *BoneNames[Bone], *IGClipAudit::HitName(Hit));
			Note(Key, Depth, Phase, From, To, Hit.ImpactPoint, FrameKeys);
			if (Depth > FrameDepth)
			{
				FrameDepth = Depth;
				FrameCause = IGClipAudit::HitName(Hit);
				FrameKey = Key;
			}
		}
	}

	// 살. 정점을 끄는 뼈의 관절에서 정점까지 긋는다. 그 뼈가 손잡이면 Core에서 긋는다.
	TArray<FVector> SkinPositions;
	Skin.Evaluate(*Mesh, SkinPositions);
	for (int32 Index = 0; Index < SkinPositions.Num(); ++Index)
	{
		const int32 SampleBone = Skin.Samples[Index].Bone;
		if (SampleBone >= BoneCount || Buried[SampleBone])
		{
			continue;
		}
		const FVector& SkinPoint = SkinPositions[Index];
		const FVector From = Handles[SampleBone] ? Core : Joints[SampleBone];
		FHitResult Hit;
		if (!IGClipAudit::TraceVisible(*World, From, SkinPoint, Params, Hit) || Hit.bStartPenetrating || Hit.Distance < 0.5f)
		{
			continue;
		}
		const float Depth = FVector::Distance(Hit.ImpactPoint, SkinPoint);
		if (Depth >= IGClipAudit::VisibleDepth)
		{
			const FString Key = FString::Printf(TEXT("skin/%s/%s"), *BoneNames[SampleBone], *IGClipAudit::HitName(Hit));
			Note(Key, Depth, Phase, From, SkinPoint, Hit.ImpactPoint, FrameKeys);
			if (Depth > FrameDepth)
			{
				FrameDepth = Depth;
				FrameCause = IGClipAudit::HitName(Hit);
				FrameKey = Key;
			}
		}
	}
	++Frames;
	FramesOver2 += FrameDepth >= 2.0f ? 1 : 0;
	FramesOver5 += FrameDepth >= 5.0f ? 1 : 0;
	if (FrameDepth >= 5.0f)
	{
		++Causes.FindOrAdd(FString::Printf(TEXT("%s %s"), *FrameCause, *Phase));
	}
	if (!IGClipAudit::IsTraceRequested() || !Owner)
	{
		return;
	}
	const FTransform Frame(FRotator(0.0f, Owner->GetActorRotation().Yaw, 0.0f), Owner->GetActorLocation());
	if (Phase.Contains(TEXT(".stair")) || FrameDepth >= IGClipAudit::VisibleDepth)
	{
		UE_LOG(LogIndieGame, Display, TEXT("CLIP_TRACE who=%s phase=%s at=%.1f,%.1f,%.1f yaw=%.1f depth=%.1f key=%s"), *Who,
			*Phase, Frame.GetLocation().X, Frame.GetLocation().Y, Frame.GetLocation().Z, Owner->GetActorRotation().Yaw,
			FrameDepth, FrameKey.IsEmpty() ? TEXT("none") : *FrameKey);
	}
	// 기어 가는 몸만 모은다. 듣거나 두드리는 자세는 팔 자리가 다르다.
	if (!Phase.StartsWith(TEXT("Chasing")))
	{
		return;
	}
	if (Footprint.IsEmpty())
	{
		Footprint.Init(FVector::ZeroVector, IGClipAudit::FootprintSectors);
	}
	const auto Gather = [this, &Frame](const FVector& Point)
	{
		const FVector Local = Frame.InverseTransformPosition(Point);
		const float Degrees = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
		const int32 Sector = FMath::Clamp(FMath::FloorToInt((Degrees + 180.0f) / 5.0f), 0, IGClipAudit::FootprintSectors - 1);
		if (Local.SizeSquared2D() > Footprint[Sector].SizeSquared2D())
		{
			Footprint[Sector] = Local;
		}
	};
	for (const FVector& Joint : Joints)
	{
		Gather(Joint);
	}
	for (const FVector& SkinPoint : SkinPositions)
	{
		Gather(SkinPoint);
	}
}

void FIGClipAudit::Report() const
{
	if (!bOn || Frames == 0)
	{
		return;
	}
	UE_LOG(LogIndieGame, Display, TEXT("CLIP_AUDIT who=%s frames=%d over2=%d over5=%d max=%.1f worst=%s"),
		*Who, Frames, FramesOver2, FramesOver5, Deepest, DeepestKey.IsEmpty() ? TEXT("none") : *DeepestKey);
	TArray<TPair<FString, FPart>> Sorted;
	for (const TPair<FString, FPart>& Part : Parts)
	{
		Sorted.Add(Part);
	}
	Sorted.Sort([](const TPair<FString, FPart>& A, const TPair<FString, FPart>& B) { return A.Value.Deepest > B.Value.Deepest; });
	for (int32 Index = 0; Index < FMath::Min(Sorted.Num(), IGClipAudit::ReportedParts); ++Index)
	{
		const FPart& Part = Sorted[Index].Value;
		UE_LOG(LogIndieGame, Display, TEXT("CLIP_AUDIT_PART who=%s key=%s frames=%d max=%.1f phase=%s at=%s"),
			*Who, *Sorted[Index].Key, Part.Frames, Part.Deepest, *Part.Phase, *Part.At.ToCompactString());
	}
	TArray<TPair<FString, int32>> ByFrames = Causes.Array();
	ByFrames.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });
	for (int32 Index = 0; Index < FMath::Min(ByFrames.Num(), IGClipAudit::ReportedCauses); ++Index)
	{
		UE_LOG(LogIndieGame, Display, TEXT("CLIP_AUDIT_CAUSE who=%s cause=%s frames=%d"), *Who, *ByFrames[Index].Key,
			ByFrames[Index].Value);
	}
	for (int32 Sector = 0; Sector < Footprint.Num(); ++Sector)
	{
		const FVector& Point = Footprint[Sector];
		UE_LOG(LogIndieGame, Display, TEXT("CLIP_FOOTPRINT who=%s deg=%d r=%.1f x=%.1f y=%.1f z=%.1f"), *Who,
			Sector * 5 - 180, Point.Size2D(), Point.X, Point.Y, Point.Z);
	}
}
