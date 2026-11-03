#include "Entity/IGWalkerLegIK.h"

#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "BonePose.h"
#include "CollisionQueryParams.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Entity/IGClipAudit.h"
#include "IndieGame.h"

namespace IGWalkerLegIK
{
	const FName ThighBones[2] = {TEXT("thigh_l"), TEXT("thigh_r")};
	const FName CalfBones[2] = {TEXT("calf_l"), TEXT("calf_r")};
	const FName FootBones[2] = {TEXT("foot_l"), TEXT("foot_r")};
	const FName UpperArmBones[2] = {TEXT("upperarm_l"), TEXT("upperarm_r")};
	const FName LowerArmBones[2] = {TEXT("lowerarm_l"), TEXT("lowerarm_r")};
	const FName HandBones[2] = {TEXT("hand_l"), TEXT("hand_r")};
	/** 무릎은 몸 앞(+X)으로, 바깥으로 조금 벌려 굽힌다. */
	constexpr float KneeOutward = 0.12f;
	/**
	 * 팔 바깥면을 잴 때 관절에 더하는 굵기(cm). SK_MokHansooPatrol 작업복 소매는 팔꿈치·손목께에서 뼈 바깥 6 cm 남짓,
	 * 손은 5 cm, 손전등을 쥔 오른손은 손전등까지 10 cm 남짓 나온다. 손끝은 손목에서 손 뼈 길이만큼 앞이다.
	 */
	constexpr float SleeveRadius = 6.5f;
	constexpr float HandRadius = 5.5f;
	constexpr float TorchHandRadius = 10.0f;
	constexpr float WalkerHandLength = 8.5f;
	/** 팔을 이보다 더 모으면 소매가 옆구리 속으로 많이 들어간다. */
	constexpr float MaxArmTuckDegrees = 14.0f;
	/** 양옆 빈 데를 재는 거리와, 팔 바깥면을 벽에서 띄울 틈(IGWalkerArms::UpdateRoom). */
	constexpr float ArmRoomReach = 55.0f;
	constexpr float ArmWallGap = 2.0f;
}

namespace IGStairFeet
{
	/** 발목에서 뒤꿈치와 발끝까지(cm). SK_MokHansooPatrol 슬리퍼가 9.8 cm, 23.5 cm이고 바닥 훑기 간격만큼 더 잡는다. */
	constexpr float HeelReach = 11.0f;
	constexpr float ToeReach = 25.0f;
	/** 발 가운데. 여기가 디딤판에 얹혀야 디딘 것이다. */
	constexpr float ArchAhead = 6.0f;
	/** 발목에서 발 안쪽과 바깥 바짓단까지. 바짓단이 발목 바깥 17.7 cm까지 발에 붙어 움직인다. */
	constexpr float FootInward = 7.0f;
	constexpr float FootOutward = 18.0f;
	/** 바닥을 훑는 간격과 몸 발밑 높이에서 위아래로 훑는 범위. */
	constexpr float FloorSampleStep = 1.5f;
	constexpr float FloorTraceUp = 80.0f;
	constexpr float FloorTraceDown = 110.0f;
	/** 밑에 바닥이 없다(빈 데). */
	constexpr float FloorMissing = -1.0e6f;
	/** 벽이나 덩어리 속이다. 발을 둘 수 없다. FloorMissing보다 작아 바닥 없음으로도 읽힌다. */
	constexpr float FloorBlocked = -2.0e6f;
	/** 몸 뒤, 앞, 옆으로 이만큼 떨어진 바닥까지 보고, 높이가 이만큼 다르면 고르지 않은 바닥이다. */
	constexpr float UnevenProbeBehind = 50.0f;
	constexpr float UnevenProbeAhead = 100.0f;
	constexpr float UnevenProbeSide = 60.0f;
	constexpr float UnevenProbeRise = 5.0f;
	/** 계단 걸음을 풀 때 몸을 세우는 빠르기(cm/s). 몸을 다 세운 뒤에 다리를 걷는 애니메이션으로 돌린다. */
	constexpr float BodyRiseSpeed = 40.0f;
	/** 평지에 이만큼 있어야 계단 걸음을 푼다. 반 층 참을 건너는 동안은 이어 간다. */
	constexpr float StairFlatHoldSeconds = 0.6f;
	constexpr float StairBlendSeconds = 0.2f;
	/** 평지 한 걸음. 걷는 애니메이션 보폭(46 cm)의 반이다. 디딤판 폭을 못 읽으면 이 폭으로 딛는다. */
	constexpr float FlatStepLength = 23.0f;
	/** 이보다 빠르면(수평 cm/s) 디딤판을 둘씩 딛는다. */
	constexpr float TwoTreadSpeed = 150.0f;
	/** 한 틱에 이보다 많이 옮겼으면 순간 이동이다. */
	constexpr float StairTeleportDistance = 80.0f;
	/** 두 발이 다 디딘 채 적어도 이만큼은 있는다. */
	constexpr float DoubleSupportSeconds = 0.04f;
	/** 걷는 애니메이션이 골반을 낮춰 둔 만큼(rig_walker의 crouch). */
	constexpr float GaitCrouch = 2.5f;
	/** 다리를 이 비율까지만 편다. */
	constexpr float LegReachRatio = 0.97f;
	/** 내딛는 발은 몸에서 다리 길이의 이 비율 안에 놓는다. */
	constexpr float StepReachRatio = 0.7f;
	constexpr float MaxBodyDrop = 20.0f;
	const FName MetalStairTag(TEXT("Footstep.MetalStair"));

	/** 디딤판 앞면 사이 거리. 앞면이 둘 넘게 보이지 않으면 0이다. */
	float MeasureGoing(const TArray<float>& Floor)
	{
		TArray<float, TInlineAllocator<16>> Risers;
		for (int32 Index = 0; Index + 1 < Floor.Num(); ++Index)
		{
			if (Floor[Index] > FloorMissing && Floor[Index + 1] > FloorMissing
				&& FMath::Abs(Floor[Index + 1] - Floor[Index]) > 4.0f)
			{
				Risers.Add((Index + 0.5f) * FloorSampleStep);
			}
		}
		TArray<float, TInlineAllocator<16>> Gaps;
		for (int32 Index = 0; Index + 1 < Risers.Num(); ++Index)
		{
			const float Gap = Risers[Index + 1] - Risers[Index];
			if (Gap > 15.0f && Gap < 45.0f)
			{
				Gaps.Add(Gap);
			}
		}
		if (Gaps.IsEmpty())
		{
			return 0.0f;
		}
		Gaps.Sort();
		return Gaps[Gaps.Num() / 2];
	}
}

namespace
{
	/**
	 * 엉덩이에서 발목까지 두 마디를 풀어 발목을 애니메이션 자리에서 Target 쪽으로 Alpha만큼 옮긴다. 무릎은
	 * 애니메이션이 굽힌 쪽에서 Pole 쪽으로, 발은 애니메이션 자세에서 FootTurn만큼 같이 옮긴다. Alpha가 0이면
	 * 애니메이션 다리 그대로다.
	 */
	void SolveWalkerLeg(FCSPose<FCompactPose>& Pose, const FCompactPoseBoneIndex Thigh, const FCompactPoseBoneIndex Calf,
		const FCompactPoseBoneIndex Foot, const FVector& Target, const FVector& Pole, const FQuat& FootTurn,
		const float Alpha, TArray<FBoneTransform, TInlineAllocator<3>>& OutBones)
	{
		const FTransform ThighPose = Pose.GetComponentSpaceTransform(Thigh);
		const FTransform CalfPose = Pose.GetComponentSpaceTransform(Calf);
		const FTransform FootPose = Pose.GetComponentSpaceTransform(Foot);
		const FVector Hip = ThighPose.GetLocation();
		const FVector Knee = CalfPose.GetLocation();
		const FVector Ankle = FootPose.GetLocation();
		const double Upper = FVector::Dist(Hip, Knee);
		const double Lower = FVector::Dist(Knee, Ankle);
		const FVector ToGoal = FMath::Lerp(Ankle, Target, static_cast<double>(Alpha)) - Hip;
		const double Distance = ToGoal.Size();
		if (Upper < 1.0 || Lower < 1.0 || Distance < 1.0)
		{
			return;
		}
		const FVector Axis = ToGoal / Distance;
		// 닿지 않는 자리면 그쪽으로 다리를 다 편다.
		const double Reach = FMath::Clamp(Distance, FMath::Abs(Upper - Lower) + 1.0, Upper + Lower - 0.05);
		FVector AnimBend = Knee - (Hip + Axis * FVector::DotProduct(Knee - Hip, Axis));
		FVector PoleBend = Pole - Axis * FVector::DotProduct(Pole, Axis);
		const bool bAnimBend = AnimBend.Normalize();
		const bool bPoleBend = PoleBend.Normalize();
		if (!bAnimBend && !bPoleBend)
		{
			return;
		}
		FVector Bend = !bAnimBend ? PoleBend : (!bPoleBend ? AnimBend : FMath::Lerp(AnimBend, PoleBend, static_cast<double>(Alpha)));
		if (!Bend.Normalize())
		{
			Bend = PoleBend;
		}
		const double Along = (Upper * Upper - Lower * Lower + Reach * Reach) / (2.0 * Reach);
		const double Rise = FMath::Sqrt(FMath::Max(0.0, Upper * Upper - Along * Along));
		const FVector NewKnee = Hip + Axis * Along + Bend * Rise;
		const FVector NewAnkle = Hip + Axis * Reach;
		const FQuat ThighTurn = FQuat::FindBetweenNormals((Knee - Hip) / Upper, (NewKnee - Hip).GetSafeNormal());
		const FQuat CalfTurn = FQuat::FindBetweenNormals(
			ThighTurn.RotateVector((Ankle - Knee) / Lower), (NewAnkle - NewKnee).GetSafeNormal());
		FTransform NewThigh = ThighPose;
		NewThigh.SetRotation((ThighTurn * ThighPose.GetRotation()).GetNormalized());
		FTransform NewCalf = CalfPose;
		NewCalf.SetLocation(NewKnee);
		NewCalf.SetRotation((CalfTurn * ThighTurn * CalfPose.GetRotation()).GetNormalized());
		FTransform NewFoot = FootPose;
		NewFoot.SetLocation(NewAnkle);
		NewFoot.SetRotation((FQuat::Slerp(FQuat::Identity, FootTurn, Alpha) * FootPose.GetRotation()).GetNormalized());
		OutBones.Add(FBoneTransform(Thigh, NewThigh));
		OutBones.Add(FBoneTransform(Calf, NewCalf));
		OutBones.Add(FBoneTransform(Foot, NewFoot));
	}

	/**
	 * 어깨를 축으로 팔을 몸 쪽으로 모아 팔꿈치·손목·손끝 바깥면이 몸 가운데에서 Limit 안에 들게 한다. 몸 앞뒤 축으로만
	 * 돌리니 애니메이션의 팔 흔들기는 그대로 남는다. 모으면 소매 안쪽이 옆구리에 조금 묻히지만 팔에 가려 보이지 않는다.
	 */
	void TuckWalkerArm(FCSPose<FCompactPose>& Pose, const FCompactPoseBoneIndex UpperArm, const FCompactPoseBoneIndex LowerArm,
		const FCompactPoseBoneIndex Hand, const int32 Side, const float Limit, TArray<FBoneTransform, TInlineAllocator<3>>& OutBones)
	{
		const FTransform ShoulderPose = Pose.GetComponentSpaceTransform(UpperArm);
		const FVector Shoulder = ShoulderPose.GetLocation();
		const FVector Elbow = Pose.GetComponentSpaceTransform(LowerArm).GetLocation();
		const FVector Wrist = Pose.GetComponentSpaceTransform(Hand).GetLocation();
		const FVector Tip = Wrist + (Wrist - Elbow).GetSafeNormal() * IGWalkerLegIK::WalkerHandLength;
		const float Outward = Side == 0 ? -1.0f : 1.0f;
		// 바깥으로 얼마나 나갔나를 어깨에서 잰다. 팔이 어깨 밑으로 늘어져 있으니 앞뒤 축으로 몸 쪽으로 돌리면 줄어든다.
		float Tuck = 0.0f;
		const FVector Points[3] = {Elbow, Wrist, Tip};
		const float Radii[3] = {IGWalkerLegIK::SleeveRadius, IGWalkerLegIK::SleeveRadius,
			Side == 0 ? IGWalkerLegIK::HandRadius : IGWalkerLegIK::TorchHandRadius};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const double Across = Outward * (Points[Index].Y - Shoulder.Y);
			const double Below = Shoulder.Z - Points[Index].Z;
			const double Reach = FMath::Sqrt(Across * Across + Below * Below);
			const double Allowed = Limit - Radii[Index] - Outward * Shoulder.Y;
			if (Reach < 1.0 || Across <= Allowed)
			{
				continue;
			}
			const double Angle = FMath::Atan2(Across, Below);
			Tuck = FMath::Max(Tuck, static_cast<float>(Angle - FMath::Asin(FMath::Clamp(Allowed / Reach, -1.0, 1.0))));
		}
		Tuck = FMath::Min(Tuck, FMath::DegreesToRadians(IGWalkerLegIK::MaxArmTuckDegrees));
		if (Tuck <= 0.0f)
		{
			return;
		}
		// 앞(+X) 축으로 돌리면 어깨 밑의 점이 +Y로 간다. 왼팔은 +Y가 몸 쪽이고 오른팔은 -Y가 몸 쪽이다.
		FTransform NewShoulder = ShoulderPose;
		NewShoulder.SetRotation((FQuat(FVector::ForwardVector, -Outward * Tuck) * ShoulderPose.GetRotation()).GetNormalized());
		OutBones.Add(FBoneTransform(UpperArm, NewShoulder));
	}

	struct FIGWalkerAnimProxy final : public FAnimSingleNodeInstanceProxy
	{
		explicit FIGWalkerAnimProxy(UAnimInstance* InAnimInstance)
			: FAnimSingleNodeInstanceProxy(InAnimInstance)
		{
		}

		virtual void PreUpdate(UAnimInstance* InAnimInstance, const float DeltaSeconds) override
		{
			FAnimSingleNodeInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
			const FIGWalkerIKRequest& Request = CastChecked<UIGWalkerAnimInstance>(InAnimInstance)->GetIK();
			const USkeletalMeshComponent* Mesh = InAnimInstance->GetSkelMeshComponent();
			Alpha = Mesh ? Request.Alpha : 0.0f;
			bArms = false;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				ArmLimits[Side] = Request.ArmLimit[Side];
				bArms = bArms || (Mesh && ArmLimits[Side] < FIGWalkerIKRequest::ArmFreeReach);
			}
			if (Alpha <= 0.0f && !bArms)
			{
				return;
			}
			if (BoundMesh != Mesh->GetSkeletalMeshAsset())
			{
				BoundMesh = Mesh->GetSkeletalMeshAsset();
				for (int32 Side = 0; Side < 2; ++Side)
				{
					Bones[Side][0] = Mesh->GetBoneIndex(IGWalkerLegIK::ThighBones[Side]);
					Bones[Side][1] = Mesh->GetBoneIndex(IGWalkerLegIK::CalfBones[Side]);
					Bones[Side][2] = Mesh->GetBoneIndex(IGWalkerLegIK::FootBones[Side]);
					ArmBones[Side][0] = Mesh->GetBoneIndex(IGWalkerLegIK::UpperArmBones[Side]);
					ArmBones[Side][1] = Mesh->GetBoneIndex(IGWalkerLegIK::LowerArmBones[Side]);
					ArmBones[Side][2] = Mesh->GetBoneIndex(IGWalkerLegIK::HandBones[Side]);
				}
			}
			// 이 틱에 몸을 다 옮긴 뒤의 메시 자리다(관리인이 메시보다 먼저 틱한다).
			const FTransform& MeshToWorld = GetComponentTransform();
			const float MeshYaw = MeshToWorld.Rotator().Yaw;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				Targets[Side] = MeshToWorld.InverseTransformPosition(Request.Ankle[Side]);
				FootTurns[Side] = FQuat(FVector::UpVector,
					FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(MeshYaw, Request.FootYaw[Side])));
			}
		}

		virtual bool Evaluate(FPoseContext& Output) override
		{
			const bool bEvaluated = FAnimSingleNodeInstanceProxy::Evaluate(Output);
			if (Alpha <= 0.0f && !bArms)
			{
				return bEvaluated;
			}
			const FBoneContainer& Required = Output.Pose.GetBoneContainer();
			const auto Compact = [&Required](const int32 MeshIndex)
			{
				return MeshIndex == INDEX_NONE
					? FCompactPoseBoneIndex(INDEX_NONE)
					: Required.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
			};
			FCSPose<FCompactPose> Pose;
			Pose.InitPose(Output.Pose);
			bool bChanged = false;
			TArray<FBoneTransform, TInlineAllocator<3>> Chain;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FCompactPoseBoneIndex Thigh = Compact(Bones[Side][0]);
				const FCompactPoseBoneIndex Calf = Compact(Bones[Side][1]);
				const FCompactPoseBoneIndex Foot = Compact(Bones[Side][2]);
				if (Alpha > 0.0f && Thigh.IsValid() && Calf.IsValid() && Foot.IsValid())
				{
					Chain.Reset();
					const float Outward = Side == 0 ? -IGWalkerLegIK::KneeOutward : IGWalkerLegIK::KneeOutward;
					SolveWalkerLeg(Pose, Thigh, Calf, Foot, Targets[Side], FVector(1.0f, Outward, 0.0f), FootTurns[Side],
						Alpha, Chain);
					if (!Chain.IsEmpty())
					{
						Pose.LocalBlendCSBoneTransforms(Chain, 1.0f);
						bChanged = true;
					}
				}
				const FCompactPoseBoneIndex UpperArm = Compact(ArmBones[Side][0]);
				const FCompactPoseBoneIndex LowerArm = Compact(ArmBones[Side][1]);
				const FCompactPoseBoneIndex Hand = Compact(ArmBones[Side][2]);
				if (ArmLimits[Side] < FIGWalkerIKRequest::ArmFreeReach && UpperArm.IsValid() && LowerArm.IsValid()
					&& Hand.IsValid())
				{
					Chain.Reset();
					TuckWalkerArm(Pose, UpperArm, LowerArm, Hand, Side, ArmLimits[Side], Chain);
					if (!Chain.IsEmpty())
					{
						Pose.LocalBlendCSBoneTransforms(Chain, 1.0f);
						bChanged = true;
					}
				}
			}
			if (bChanged)
			{
				FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(Pose, Output.Pose);
			}
			return bEvaluated;
		}

		float Alpha = 0.0f;
		bool bArms = false;
		FVector Targets[2] = {FVector::ZeroVector, FVector::ZeroVector};
		FQuat FootTurns[2] = {FQuat::Identity, FQuat::Identity};
		float ArmLimits[2] = {FIGWalkerIKRequest::ArmFreeReach, FIGWalkerIKRequest::ArmFreeReach};
		int32 Bones[2][3] = {{INDEX_NONE, INDEX_NONE, INDEX_NONE}, {INDEX_NONE, INDEX_NONE, INDEX_NONE}};
		int32 ArmBones[2][3] = {{INDEX_NONE, INDEX_NONE, INDEX_NONE}, {INDEX_NONE, INDEX_NONE, INDEX_NONE}};
		const USkeletalMesh* BoundMesh = nullptr;
	};
}

FAnimInstanceProxy* UIGWalkerAnimInstance::CreateAnimInstanceProxy()
{
	return new FIGWalkerAnimProxy(this);
}

bool FIGStairFeet::Bind(const USkeletalMeshComponent& Mesh)
{
	bBound = false;
	Owner = Mesh.GetOwner();
	const USkeletalMesh* Asset = Mesh.GetSkeletalMeshAsset();
	if (!Asset)
	{
		return false;
	}
	const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
	const TArray<FTransform>& LocalPose = Skeleton.GetRefBonePose();
	const auto RestLocation = [&Skeleton, &LocalPose](const FName Name, FVector& OutLocation)
	{
		int32 Index = Skeleton.FindBoneIndex(Name);
		if (Index == INDEX_NONE)
		{
			return false;
		}
		FTransform Pose = FTransform::Identity;
		for (; Index != INDEX_NONE; Index = Skeleton.GetParentIndex(Index))
		{
			Pose = Pose * LocalPose[Index];
		}
		OutLocation = Pose.GetLocation();
		return true;
	};
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FVector Knee = FVector::ZeroVector;
		if (!RestLocation(IGWalkerLegIK::ThighBones[Side], RestHip[Side])
			|| !RestLocation(IGWalkerLegIK::CalfBones[Side], Knee)
			|| !RestLocation(IGWalkerLegIK::FootBones[Side], RestAnkle[Side]))
		{
			return false;
		}
		LegLength[Side] = FVector::Dist(RestHip[Side], Knee) + FVector::Dist(Knee, RestAnkle[Side]);
		FootBones[Side] = IGWalkerLegIK::FootBones[Side];
	}
	// 메시 원점이 발밑이라 선 자세의 발목 높이가 곧 발바닥에서 발목까지다.
	AnkleHeight = 0.5f * (RestAnkle[0].Z + RestAnkle[1].Z);
	bBound = AnkleHeight > 1.0f;
	return bBound;
}

void FIGStairFeet::Reset()
{
	bEngaged = false;
	Alpha = 0.0f;
	FlatSeconds = 0.0f;
	PlantedSeconds = 0.0f;
	LastSwing = INDEX_NONE;
	Going = 0.0f;
	BodyDrop = 0.0f;
	bHasLastBody = false;
	PlanarSpeed = 0.0f;
	for (FFoot& Foot : Feet)
	{
		Foot = FFoot();
	}
}

void FIGStairFeet::SetPathAhead(const TConstArrayView<FVector> Points)
{
	PathAhead.Reset();
	PathAhead.Append(Points.GetData(), Points.Num());
}

FVector FIGStairFeet::AimAlongPath(const FVector& Body, const float Distance) const
{
	FVector At(Body.X, Body.Y, 0.0f);
	FVector Direction = Heading;
	float Left = Distance;
	for (const FVector& Next : PathAhead)
	{
		const FVector Leg(Next.X - At.X, Next.Y - At.Y, 0.0f);
		const float Length = Leg.Size();
		if (Length < 1.0f)
		{
			continue;
		}
		Direction = Leg / Length;
		if (Left <= Length)
		{
			At += Direction * Left;
			Left = 0.0f;
			break;
		}
		At += Leg;
		Left -= Length;
	}
	At += Direction * Left;
	const FVector Chord(At.X - Body.X, At.Y - Body.Y, 0.0f);
	return Chord.SizeSquared() > 25.0f ? Chord.GetSafeNormal() : Heading;
}

FIGWalkerIKRequest FIGStairFeet::GetRequest() const
{
	FIGWalkerIKRequest Request;
	Request.Alpha = Alpha;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		Request.Ankle[Side] = Feet[Side].Ankle;
		Request.FootYaw[Side] = Feet[Side].Yaw;
	}
	return Request;
}

void FIGStairFeet::Update(const UWorld& World, const USkeletalMeshComponent& Mesh, const FTransform& Body,
	const float DeltaSeconds, const float AnimPhase)
{
	if (!bBound || DeltaSeconds <= 0.0f)
	{
		return;
	}
	const FVector BodyFeet = Body.GetLocation();
	if (bHasLastBody && FVector::DistSquared(BodyFeet, LastBody) > FMath::Square(IGStairFeet::StairTeleportDistance))
	{
		Reset();
	}
	const FVector Moved = bHasLastBody ? BodyFeet - LastBody : FVector::ZeroVector;
	LastBody = BodyFeet;
	bHasLastBody = true;
	const FVector Planar(Moved.X, Moved.Y, 0.0f);
	PlanarSpeed = FMath::FInterpTo(PlanarSpeed, Planar.Size() / DeltaSeconds, DeltaSeconds, 10.0f);
	const float Yaw = Body.Rotator().Yaw;
	Heading = Planar.SizeSquared() > FMath::Square(0.3f)
		? Planar.GetSafeNormal()
		: FRotator(0.0f, Yaw, 0.0f).Vector();

	const bool bStairs = IsOnStairs(World, BodyFeet, Heading);
	FlatSeconds = bStairs ? 0.0f : FlatSeconds + DeltaSeconds;
	if (bStairs && !bEngaged)
	{
		if (Alpha <= 0.0f)
		{
			PlantFromAnimation(World, Mesh, BodyFeet, AnimPhase);
		}
		bEngaged = true;
		if (IGClipAudit::IsTraceRequested())
		{
			UE_LOG(LogIndieGame, Display, TEXT("STAIR_FEET engage at=%s alpha=%.2f"), *BodyFeet.ToCompactString(), Alpha);
		}
	}
	else if (bEngaged && FlatSeconds >= IGStairFeet::StairFlatHoldSeconds && !Feet[0].bSwing && !Feet[1].bSwing)
	{
		bEngaged = false;
		if (IGClipAudit::IsTraceRequested())
		{
			UE_LOG(LogIndieGame, Display, TEXT("STAIR_FEET release at=%s drop=%.1f"), *BodyFeet.ToCompactString(), BodyDrop);
		}
	}
	// 풀 때는 몸을 먼저 세운다. 낮춘 몸 그대로 다리를 걷는 애니메이션으로 돌리면 애니메이션의 발이 바닥 밑으로 내려간다.
	const bool bHoldLegs = bEngaged || BodyDrop > 0.5f;
	Alpha = FMath::FInterpConstantTo(Alpha, bHoldLegs ? 1.0f : 0.0f, DeltaSeconds, 1.0f / IGStairFeet::StairBlendSeconds);
	if (Alpha <= 0.0f)
	{
		BodyDrop = 0.0f;
		return;
	}
	StepFeet(World, BodyFeet, DeltaSeconds);
	if (bEngaged)
	{
		UpdateBodyDrop(FTransform(FRotator(0.0f, Yaw, 0.0f), BodyFeet), DeltaSeconds);
	}
	else
	{
		BodyDrop = FMath::FInterpConstantTo(BodyDrop, 0.0f, DeltaSeconds, IGStairFeet::BodyRiseSpeed);
	}
}

float FIGStairFeet::TraceFloor(const UWorld& World, const FVector& At, const float AroundZ, bool* bOutStair) const
{
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(IGStairFeet), false, Owner.Get());
	FHitResult Hit;
	// 문과 손수레 같은 움직이는 것은 디딜 바닥이 아니다.
	if (!World.LineTraceSingleByObjectType(Hit,
			FVector(At.X, At.Y, AroundZ + IGStairFeet::FloorTraceUp),
			FVector(At.X, At.Y, AroundZ - IGStairFeet::FloorTraceDown),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return IGStairFeet::FloorMissing;
	}
	if (Hit.bStartPenetrating)
	{
		return IGStairFeet::FloorBlocked;
	}
	if (Hit.ImpactNormal.Z < 0.7f)
	{
		return IGStairFeet::FloorMissing;
	}
	if (bOutStair && Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(IGStairFeet::MetalStairTag))
	{
		*bOutStair = true;
	}
	return Hit.ImpactPoint.Z;
}

void FIGStairFeet::SampleFloor(const UWorld& World, const FVector& Origin, const FVector& Direction, const float Start,
	const float End, const float AroundZ, TArray<float>& OutFloor) const
{
	OutFloor.Reset();
	const int32 Count = FMath::Max(0, FMath::FloorToInt((End - Start) / IGStairFeet::FloorSampleStep)) + 1;
	OutFloor.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		OutFloor.Add(TraceFloor(World, Origin + Direction * (Start + Index * IGStairFeet::FloorSampleStep), AroundZ));
	}
}

bool FIGStairFeet::IsOnStairs(const UWorld& World, const FVector& Body, const FVector& Direction) const
{
	float Low = TNumericLimits<float>::Max();
	float High = TNumericLimits<float>::Lowest();
	bool bStair = false;
	const auto Probe = [&](const FVector& At)
	{
		const float Floor = TraceFloor(World, At, Body.Z, &bStair);
		if (Floor > IGStairFeet::FloorMissing)
		{
			Low = FMath::Min(Low, Floor);
			High = FMath::Max(High, Floor);
		}
	};
	// 걷는 애니메이션의 앞발은 몸 앞 50 cm까지 나간다. 그보다 먼저 계단을 봐야 앞발이 첫 단에 들어가지 않는다.
	// 계단 첫 단 앞에서 꺾어 드는 길이라 옆도 본다.
	for (float Along = -IGStairFeet::UnevenProbeBehind; Along <= IGStairFeet::UnevenProbeAhead; Along += 25.0f)
	{
		Probe(Body + Direction * Along);
	}
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Direction) * IGStairFeet::UnevenProbeSide;
	Probe(Body + Side);
	Probe(Body - Side);
	return bStair && High - Low >= IGStairFeet::UnevenProbeRise;
}

void FIGStairFeet::PlantFromAnimation(
	const UWorld& World, const USkeletalMeshComponent& Mesh, const FVector& Body, const float AnimPhase)
{
	const float MeshYaw = Mesh.GetComponentRotation().Yaw;
	const FVector Facing = FRotator(0.0f, MeshYaw, 0.0f).Vector();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FFoot& Foot = Feet[Side];
		Foot = FFoot();
		Foot.Yaw = MeshYaw;
		const FVector Ankle = Mesh.GetSocketLocation(FootBones[Side]);
		// 발바닥 밑에서 가장 높은 바닥에 얹는다.
		float Floor = IGStairFeet::FloorMissing;
		for (const float Along : {-IGStairFeet::HeelReach, 0.0f, IGStairFeet::ArchAhead, IGStairFeet::ToeReach})
		{
			Floor = FMath::Max(Floor, TraceFloor(World, Ankle + Facing * Along, Body.Z));
		}
		Foot.Sole = Floor > IGStairFeet::FloorMissing ? Floor : Body.Z;
		Foot.Ankle = FVector(Ankle.X, Ankle.Y, Foot.Sole + AnkleHeight);
	}
	GaitPhase = FMath::Fmod(FMath::Max(AnimPhase, 0.0f), 1.0f);
	// 걷는 애니메이션에서 방금 디뎠거나 들고 있던 발. 다음은 다른 발이다.
	LastSwing = GaitPhase >= 0.1f && GaitPhase < 0.6f ? 0 : 1;
	PlantedSeconds = 0.0f;
	Going = 0.0f;
	BodyDrop = 0.0f;
}

void FIGStairFeet::StepFeet(const UWorld& World, const FVector& Body, const float DeltaSeconds)
{
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FFoot& Foot = Feet[Side];
		if (!Foot.bSwing)
		{
			continue;
		}
		AdvanceSwing(World, Side, Foot, DeltaSeconds);
		if (!Foot.bSwing)
		{
			LastSwing = Side;
			PlantedSeconds = 0.0f;
		}
	}
	// 걷는 애니메이션에서 오른발은 0.6~1, 왼발은 0.1~0.5에 든다.
	if (Feet[1].bSwing)
	{
		GaitPhase = 0.6f + 0.4f * Feet[1].SwingTime;
		return;
	}
	if (Feet[0].bSwing)
	{
		GaitPhase = 0.1f + 0.4f * Feet[0].SwingTime;
		return;
	}
	// 두 발이 다 디뎠다. 걷는 애니메이션은 다음 발을 드는 자리까지만 간다.
	if (LastSwing == 1 && GaitPhase >= 0.6f)
	{
		GaitPhase -= 1.0f;
	}
	GaitPhase = FMath::Min(GaitPhase + DeltaSeconds / (2.0f * StepSeconds), LastSwing == 1 ? 0.1f : 0.6f);
	PlantedSeconds += DeltaSeconds;
	if (PlantedSeconds < IGStairFeet::DoubleSupportSeconds)
	{
		return;
	}

	const float StepGoing = Going > 0.0f ? Going : IGStairFeet::FlatStepLength;
	const int32 Treads = PlanarSpeed > IGStairFeet::TwoTreadSpeed ? 2 : 1;
	// 몸이 뒷발에서 이만큼 앞서면 뒷발을 든다. 든 발이 가는 동안 몸이 디딤판 폭의 0.8배, 내려놓은 발은 몸 앞
	// 0.45배라 한 발이 디딤판 두 개씩 옮긴다.
	const float LiftBehind = 0.75f * Treads * StepGoing;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Heading);
	const bool bMoving = PlanarSpeed > 15.0f;
	int32 Due = INDEX_NONE;
	float DueScore = 0.0f;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector Offset = Feet[Side].Ankle - Body;
		const float Behind = -(Offset.X * Heading.X + Offset.Y * Heading.Y);
		const float Stray = FMath::Abs(FVector::DotProduct(Offset, Right) - RestAnkle[Side].Y);
		const bool bLagging = bMoving && Behind >= LiftBehind;
		// 돌거나 멈추다 다리가 꼬였으면 서 있어도 고쳐 딛는다.
		const bool bAstray = Stray > 16.0f || Behind > LiftBehind + 25.0f || Behind < -(LiftBehind + 30.0f);
		if (!bLagging && !bAstray)
		{
			continue;
		}
		const float Score = Behind + Stray + (Side == LastSwing ? 0.0f : 10.0f);
		if (Due == INDEX_NONE || Score > DueScore)
		{
			Due = Side;
			DueScore = Score;
		}
	}
	if (Due != INDEX_NONE)
	{
		StartSwing(World, Due, Body, Treads);
	}
}

void FIGStairFeet::StartSwing(const UWorld& World, const int32 Side, const FVector& Body, const int32 Treads)
{
	using namespace IGStairFeet;
	FFoot& Foot = Feet[Side];
	const FFoot& Other = Feet[1 - Side];
	const float Speed = FMath::Max(PlanarSpeed, 30.0f);
	// 몸에서 이보다 멀리 발을 놓지 않는다. 물러나다 되돌아설 때 둘씩 딛는 걸음이 66 cm 앞 계단에 발을 놓자 다리가
	// 거의 누워 종아리에 붙은 바짓단 뒤쪽이 참 모서리 속으로 처졌다.
	const float MaxReach = StepReachRatio * LegLength[Side];
	const auto Plan = [&](const float StepGoing, float& OutSwingSeconds)
	{
		OutSwingSeconds = FMath::Clamp(0.8f * Treads * StepGoing / Speed, 0.16f, 0.45f);
		return FMath::Min(Speed * OutSwingSeconds + 0.45f * Treads * StepGoing, MaxReach);
	};
	// 길이 꺾이면 발은 길을 따라 놓는다. 지금 가는 쪽으로 내밀면 계단 앞에서 꺾을 때 옆을 보고 디딘 발의 바짓단이
	// 첫 단 앞면에 들어가고, 빨리 걸으면 모퉁이 너머 벽 속에 발을 놓는다.
	float SwingSeconds = 0.0f;
	const FVector Toward = AimAlongPath(Body, Plan(Going > 0.0f ? Going : FlatStepLength, SwingSeconds));
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Toward);
	const FVector Track = Body + Right * RestAnkle[Side].Y;
	// 발 가운데 줄과 안쪽, 바깥 바짓단 줄을 같이 훑는다.
	const FVector Outward = Right * (Side == 0 ? -1.0f : 1.0f);
	constexpr float BehindStart = -40.0f;
	const float AheadEnd = Treads * 60.0f + 50.0f;
	TArray<float> Floor;
	TArray<float> Inner;
	TArray<float> Outer;
	SampleFloor(World, Track, Toward, BehindStart, AheadEnd, Body.Z, Floor);
	SampleFloor(World, Track - Outward * FootInward, Toward, BehindStart, AheadEnd, Body.Z, Inner);
	SampleFloor(World, Track + Outward * FootOutward, Toward, BehindStart, AheadEnd, Body.Z, Outer);
	Going = MeasureGoing(Floor);
	const float StepGoing = Going > 0.0f ? Going : FlatStepLength;
	StepSeconds = Treads * StepGoing / Speed;
	const float Desired = Plan(StepGoing, SwingSeconds);
	Foot.SwingSeconds = SwingSeconds;

	// 발이 다 얹히는 자리 가운데 Desired에 가장 가까운 곳. 발 둘레 어디도 벽 속이 아니어야 하고 발 가운데 줄은 끝까지
	// 바닥이어야 하며, 발 둘레 밑 가장 높은 바닥이 발 가운데 밑에도 있어야 한다. 발끝이 다음 단 앞면에, 뒤꿈치가
	// 윗단 앞면에, 바짓단이 옆 단에 들어가는 자리는 발 가운데가 낮아 빠진다. 가까이 없으면 몸 쪽으로 걸음을 줄인다.
	const int32 HeelSamples = FMath::CeilToInt(HeelReach / FloorSampleStep);
	const int32 ToeSamples = FMath::CeilToInt(ToeReach / FloorSampleStep);
	const int32 ArchSamples = FMath::RoundToInt(ArchAhead / FloorSampleStep);
	const float Window = 0.6f * Treads * StepGoing + 4.0f;
	int32 Best = INDEX_NONE;
	float BestCost = TNumericLimits<float>::Max();
	float BestSole = Other.Sole;
	for (int32 Index = HeelSamples; Index + ToeSamples < Floor.Num(); ++Index)
	{
		const float Along = BehindStart + Index * FloorSampleStep;
		const float Cost = FMath::Abs(Along - Desired) + (FMath::Abs(Along - Desired) > Window ? 1000.0f : 0.0f);
		if (Cost >= BestCost || Along < 0.0f || Along > FMath::Min(Desired + Window, MaxReach))
		{
			continue;
		}
		float Top = FloorMissing;
		bool bSolid = true;
		for (int32 Sample = Index - HeelSamples; Sample <= Index + ToeSamples && bSolid; ++Sample)
		{
			bSolid = Floor[Sample] > FloorMissing && Inner[Sample] > FloorBlocked && Outer[Sample] > FloorBlocked;
			Top = FMath::Max3(Top, Floor[Sample], FMath::Max(Inner[Sample], Outer[Sample]));
		}
		if (!bSolid || Floor[Index + ArchSamples] < Top - 0.75f
			|| FMath::Abs(Top - Other.Sole) > Treads * 20.0f + 6.0f)
		{
			continue;
		}
		Best = Index;
		BestCost = Cost;
		BestSole = Top;
	}
	const float Landing = Best != INDEX_NONE ? BehindStart + Best * FloorSampleStep : Desired;
	if (Best == INDEX_NONE)
	{
		// 디딜 자리를 못 찾았다. 바라던 자리 발바닥 밑 가장 높은 바닥에 놓는다.
		const int32 Center = FMath::RoundToInt((Desired - BehindStart) / FloorSampleStep);
		for (int32 Sample = Center - HeelSamples; Sample <= Center + ToeSamples; ++Sample)
		{
			if (Floor.IsValidIndex(Sample) && Floor[Sample] > FloorMissing)
			{
				BestSole = Best == INDEX_NONE ? Floor[Sample] : FMath::Max(BestSole, Floor[Sample]);
				Best = Sample;
			}
		}
	}
	const FVector To = Track + Toward * Landing;
	Foot.bSwing = true;
	Foot.SwingTime = 0.0f;
	Foot.From = Foot.Ankle;
	Foot.FromSole = Foot.Sole;
	Foot.FromYaw = Foot.Yaw;
	Foot.To = FVector(To.X, To.Y, BestSole + AnkleHeight);
	Foot.ToSole = BestSole;
	// 디딜 자리는 그쪽을 보는 발로 골랐다.
	Foot.ToYaw = Toward.Rotation().Yaw;
	Foot.Lift = Treads > 1 ? 6.0f : 3.5f;
	if (IGClipAudit::IsTraceRequested())
	{
		UE_LOG(LogIndieGame, Display,
			TEXT("STAIR_STEP side=%d from=%s to=%s sole=%.1f>%.1f going=%.1f treads=%d fit=%d seconds=%.2f"),
			Side, *Foot.From.ToCompactString(), *Foot.To.ToCompactString(), Foot.FromSole, Foot.ToSole, Going, Treads,
			Landing == Desired ? 0 : 1, Foot.SwingSeconds);
	}

	// 든 발이 지나는 길 밑 바닥. 발바닥 앞뒤 끝까지 훑어 둔다.
	FVector Path = Foot.To - Foot.From;
	Path.Z = 0.0f;
	Foot.PathLength = Path.Size();
	const FVector PathDirection = Foot.PathLength > 1.0f ? Path / Foot.PathLength : Toward;
	Foot.PathStart = -HeelReach - FloorSampleStep;
	SampleFloor(World, FVector(Foot.From.X, Foot.From.Y, Body.Z), PathDirection, Foot.PathStart,
		Foot.PathLength + ToeReach + FloorSampleStep, Body.Z, Foot.PathFloor);
}

void FIGStairFeet::AdvanceSwing(const UWorld& World, const int32 Side, FFoot& Foot, const float DeltaSeconds) const
{
	using namespace IGStairFeet;
	Foot.SwingTime = FMath::Min(1.0f, Foot.SwingTime + DeltaSeconds / Foot.SwingSeconds);
	const float Time = Foot.SwingTime;
	const float Rise = Foot.ToSole - Foot.FromSole;
	// 오를 때는 발을 먼저 들고 나서 내밀고, 내려갈 때는 먼저 내밀고 나서 내린다.
	float Along = FMath::SmoothStep(0.0f, 1.0f, Time);
	float Up = Along;
	if (Rise > 3.0f)
	{
		Along = FMath::SmoothStep(0.15f, 1.0f, Time);
		Up = FMath::SmoothStep(0.0f, 0.7f, Time);
	}
	else if (Rise < -3.0f)
	{
		Along = FMath::SmoothStep(0.0f, 0.8f, Time);
		Up = FMath::SmoothStep(0.3f, 1.0f, Time);
	}
	const FVector At = FMath::Lerp(Foot.From, Foot.To, Along);
	float Sole = Foot.FromSole + Rise * Up + Foot.Lift * FMath::Sin(PI * Time);
	// 발바닥 밑을 지나는 디딤판보다 늘 높다. 내려앉는 마지막에만 틈을 줄인다. 디딜 자리를 고를 때보다 앞뒤를
	// 바닥 훑기 간격만큼 좁혀 본다. 그래야 디딘 자리에서 다음 단 앞면을 밟은 것으로 잘못 보지 않는다.
	const float Clearance = 2.0f * (1.0f - FMath::SmoothStep(0.8f, 1.0f, Time));
	const float Center = Along * Foot.PathLength;
	const int32 First = FMath::Max(0,
		FMath::CeilToInt((Center - HeelReach + FloorSampleStep - Foot.PathStart) / FloorSampleStep));
	const int32 Last = FMath::Min(Foot.PathFloor.Num() - 1,
		FMath::FloorToInt((Center + ToeReach - FloorSampleStep - Foot.PathStart) / FloorSampleStep));
	float Under = FloorMissing;
	for (int32 Sample = First; Sample <= Last; ++Sample)
	{
		Under = FMath::Max(Under, Foot.PathFloor[Sample]);
	}
	// 발은 들어 올리는 동안 다 돌려 둔다. 길 밑 바닥은 가는 쪽을 보는 발로 훑었으니, 도는 동안 비스듬한 발끝과
	// 바깥으로 벌어진 바짓단 밑은 지금 발이 향한 대로 따로 본다. 돌던 발끝이 첫 단 앞면을 쓸었다.
	Foot.Yaw = Foot.FromYaw + FMath::FindDeltaAngleDegrees(Foot.FromYaw, Foot.ToYaw) * FMath::SmoothStep(0.0f, 0.6f, Time);
	const FVector Facing = FRotator(0.0f, Foot.Yaw, 0.0f).Vector();
	const FVector Outward = FVector::CrossProduct(FVector::UpVector, Facing) * (Side == 0 ? -1.0f : 1.0f);
	// 3 cm마다 본다. 8 cm마다 보니 참 끝 모서리가 그 사이에 들어 바짓단이 참에 묻힌 채 내려갔다.
	for (float Forward = -HeelReach; Forward <= ToeReach; Forward += 3.0f)
	{
		for (const float Across : {-FootInward, 0.0f, 0.5f * FootOutward, FootOutward})
		{
			Under = FMath::Max(Under, TraceFloor(World, At + Facing * Forward + Outward * Across, At.Z - AnkleHeight));
		}
	}
	if (Under > FloorMissing)
	{
		Sole = FMath::Max(Sole, Under + Clearance);
	}
	if (Time >= 1.0f)
	{
		Foot.bSwing = false;
		Sole = Foot.ToSole;
		Foot.Yaw = Foot.ToYaw;
	}
	Foot.Sole = Sole;
	Foot.Ankle = FVector(At.X, At.Y, Sole + AnkleHeight);
}

void FIGStairFeet::UpdateBodyDrop(const FTransform& Body, const float DeltaSeconds)
{
	float Need = 0.0f;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector Hip = Body.TransformPosition(RestHip[Side]) - FVector(0.0f, 0.0f, IGStairFeet::GaitCrouch);
		const FVector ToHip = Hip - Feet[Side].Ankle;
		const float Across = FVector2D(ToHip.X, ToHip.Y).Size();
		const float Reach = IGStairFeet::LegReachRatio * LegLength[Side];
		const float Height = Across < Reach ? FMath::Sqrt(Reach * Reach - Across * Across) : 0.0f;
		Need = FMath::Max(Need, static_cast<float>(ToHip.Z) - Height);
	}
	Need = FMath::Clamp(Need, 0.0f, IGStairFeet::MaxBodyDrop);
	// 뒷발이 멀어질 때는 바로 낮추고, 다시 가까워지면 천천히 일어선다.
	BodyDrop = FMath::FInterpTo(BodyDrop, Need, DeltaSeconds, Need > BodyDrop ? 14.0f : 4.0f);
}

bool FIGWalkerStairBody::Attach(USkeletalMeshComponent& Body, AActor& Owner)
{
	bAttached = false;
	Mesh = &Body;
	Body.SetAnimInstanceClass(UIGWalkerAnimInstance::StaticClass());
	// 발 자리는 그 틱에 몸을 옮긴 뒤에 정한다. 메시가 먼저 틱하면 한 프레임 전 자리로 그린다.
	Body.AddTickPrerequisiteActor(&Owner);
	RestLocation = Body.GetRelativeLocation();
	bAttached = Stairs.Bind(Body);
	return bAttached;
}

void FIGWalkerStairBody::Reset()
{
	Stairs.Reset();
	ArmLimit[0] = ArmLimit[1] = FIGWalkerIKRequest::ArmFreeReach;
	USkeletalMeshComponent* Body = Mesh.Get();
	if (!Body)
	{
		return;
	}
	Body->SetRelativeLocation(RestLocation);
	if (UIGWalkerAnimInstance* Anim = Cast<UIGWalkerAnimInstance>(Body->GetAnimInstance()))
	{
		Anim->SetIK(FIGWalkerIKRequest());
	}
}

void FIGWalkerStairBody::Tick(const USceneComponent& Feet, const TConstArrayView<FVector> Ahead, const bool bGait,
	const float DeltaSeconds, const bool bTuckArms)
{
	USkeletalMeshComponent* Body = Mesh.Get();
	UIGWalkerAnimInstance* Anim = Body ? Cast<UIGWalkerAnimInstance>(Body->GetAnimInstance()) : nullptr;
	const UWorld* World = Body ? Body->GetWorld() : nullptr;
	if (!bAttached || !Anim || !World)
	{
		return;
	}
	Stairs.SetPathAhead(Ahead);
	const float Length = Anim->GetLength();
	Stairs.Update(*World, *Body, FTransform(FRotator(0.0f, Feet.GetComponentRotation().Yaw, 0.0f), Feet.GetComponentLocation()),
		DeltaSeconds, bGait && Length > 0.0f ? Anim->GetCurrentTime() / Length : 0.0f);
	if (bTuckArms)
	{
		UpdateArmRoom(*World, Feet, DeltaSeconds);
	}
	else
	{
		ArmLimit[0] = ArmLimit[1] = FIGWalkerIKRequest::ArmFreeReach;
	}
	FIGWalkerIKRequest Request = Stairs.GetRequest();
	Request.ArmLimit[0] = ArmLimit[0];
	Request.ArmLimit[1] = ArmLimit[1];
	Anim->SetIK(Request);
	Body->SetRelativeLocation(RestLocation - FVector(0.0f, 0.0f, Stairs.GetBodyDrop()));
	if (Stairs.IsActive() && bGait && Length > 0.0f)
	{
		// 계단에서는 걷는 애니메이션을 딛는 발에 맞춰 돌린다. 팔과 몸통이 다리와 같은 박자로 간다.
		Anim->SetPlayRate(0.0f);
		Anim->SetPosition(Stairs.GetGaitPhase() * Length, false);
	}
}

void IGWalkerArms::UpdateRoom(const UWorld& World, const FVector& Center, const float Yaw, const FCollisionQueryParams& Params,
	const float DeltaSeconds, float (&InOutLimit)[2])
{
	const FRotator Facing(0.0f, Yaw, 0.0f);
	const FVector Forward = Facing.Vector();
	const FVector Right = FRotationMatrix(Facing).GetScaledAxis(EAxis::Y);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector Out = Right * (Side == 0 ? -1.0f : 1.0f) * IGWalkerLegIK::ArmRoomReach;
		float Room = IGWalkerLegIK::ArmRoomReach;
		for (const float Ahead : {-8.0f, 8.0f, 24.0f, 40.0f})
		{
			for (const float Height : {85.0f, 105.0f, 125.0f})
			{
				const FVector From = Center + Forward * Ahead + FVector(0.0f, 0.0f, Height);
				FHitResult Hit;
				if (World.LineTraceSingleByChannel(Hit, From, From + Out, ECC_Visibility, Params) && !Hit.bStartPenetrating)
				{
					Room = FMath::Min(Room, Hit.Distance);
				}
			}
		}
		// 좁아질 때는 바로 모으고, 넓어지면 천천히 편다.
		const float Goal = Room - IGWalkerLegIK::ArmWallGap;
		InOutLimit[Side] = InOutLimit[Side] >= FIGWalkerIKRequest::ArmFreeReach
			? Goal
			: FMath::FInterpTo(InOutLimit[Side], Goal, DeltaSeconds, Goal < InOutLimit[Side] ? 25.0f : 6.0f);
	}
}

void FIGWalkerStairBody::UpdateArmRoom(const UWorld& World, const USceneComponent& Feet, const float DeltaSeconds)
{
	// 302호 판 서쪽 끝과 북벽 사이를 빠져나오던 정우의 소매가 북벽에 8 cm 들어갔다.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGWalkerArmRoom), false, Feet.GetOwner());
	if (const APlayerController* Controller = World.GetFirstPlayerController())
	{
		Params.AddIgnoredActor(Controller->GetPawn());
	}
	IGWalkerArms::UpdateRoom(World, Feet.GetComponentLocation() - FVector(0.0f, 0.0f, Stairs.GetBodyDrop()),
		Feet.GetComponentRotation().Yaw, Params, DeltaSeconds, ArmLimit);
}
