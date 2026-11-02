#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "IGWalkerLegIK.generated.h"

class AActor;
class USceneComponent;
class USkeletalMeshComponent;
class UWorld;

/** rig_walker 뼈대의 다리·팔 IK 요청. 발목은 월드 자리다. 애니메이션을 갱신하기 직전에 메시 좌표로 옮긴다. */
struct FIGWalkerIKRequest
{
	/** 0이면 애니메이션 다리 그대로다. */
	float Alpha = 0.0f;
	/** 왼발(0)과 오른발(1) 발목. */
	FVector Ankle[2] = {FVector::ZeroVector, FVector::ZeroVector};
	/** 발이 향한 월드 yaw(도). 디딘 발은 몸이 돌아도 따라 돌지 않는다. */
	float FootYaw[2] = {0.0f, 0.0f};
	/**
	 * 왼팔(0)과 오른팔(1) 바깥면이 몸 가운데에서 나갈 수 있는 거리(cm). 넘으면 어깨를 축으로 팔을 몸 쪽으로 모은다.
	 * ArmFreeReach 이상이면 모으지 않는다.
	 */
	float ArmLimit[2] = {ArmFreeReach, ArmFreeReach};

	static constexpr float ArmFreeReach = 1000.0f;
};

/**
 * 애니메이션 하나를 돌리는 재생기에 다리·팔 IK를 얹었다. 넓적다리와 종아리를 돌려 발목을 요청한 자리에 두고,
 * 발은 애니메이션이 둔 자세를 요청한 yaw로만 돌린다. 무릎은 몸 앞쪽으로 굽힌다. 켜고 끌 때는 관절 회전을
 * 섞지 않고 발목 자리와 무릎 쪽을 애니메이션에서 요청한 곳으로 옮긴다. 회전을 섞으면 두 자리 사이에서 발이
 * 바닥 밑으로 처진다. 좁은 데서는 팔을 어깨에서 몸 쪽으로 모아 팔꿈치와 손이 요청한 폭 안에 들게 한다.
 */
UCLASS(Transient, NotBlueprintable)
class INDIEGAME_API UIGWalkerAnimInstance : public UAnimSingleNodeInstance
{
	GENERATED_BODY()

public:
	void SetIK(const FIGWalkerIKRequest& InRequest) { Request = InRequest; }
	const FIGWalkerIKRequest& GetIK() const { return Request; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	FIGWalkerIKRequest Request;
};

/**
 * 계단에서 두 발을 디딤판에 번갈아 딛는다. 평지에서는 꺼져 있어 걷는 애니메이션의 다리가 그대로 보인다.
 *
 * 걷는 애니메이션은 평지 보폭(46 cm)으로 발을 내밀어서, 몸이 계단 기울기를 따라 오르면 앞발이 윗단 속에 묻힌다.
 * 몸 앞뒤 바닥 높이가 고르지 않고 그 바닥이 계단이면 켜진다. 뒷발이 몸에서 일정 거리 뒤처지면 그 발을 들어
 * 한 단(뛸 때는 두 단) 위나 아래 디딤판에 놓는다. 놓을 자리는 발 밑 바닥을 훑어, 발끝이 다음 단 앞면에도 뒤꿈치가
 * 윗단 앞면에도 들지 않고 발 가운데가 디딤판에 얹히는 곳으로 고른다. 든 발은 지나는 디딤판 모서리보다 늘
 * 높게 간다. 몸은 낮은 발에 다리가 닿을 만큼 낮추고, 걷는 애니메이션은 발에 맞춰 돌려 팔과 몸통이 다리와 같이 간다.
 */
class INDIEGAME_API FIGStairFeet
{
public:
	/** 뼈대에서 다리 길이와 선 자세의 엉덩이·발목 자리를 읽는다. 다리 뼈가 없으면 false다. */
	bool Bind(const USkeletalMeshComponent& Mesh);
	/** 순간 이동했거나 몸을 숨겼다. 발을 버리고 애니메이션 다리로 돌아간다. */
	void Reset();
	/**
	 * 몸이 앞으로 지날 점들(발밑 자리). 꺾이는 길에서는 발을 지금 가는 쪽이 아니라 길을 따라 놓는다. 비어 있으면
	 * 지금 가는 쪽으로 놓는다.
	 */
	void SetPathAhead(TConstArrayView<FVector> Points);
	/**
	 * 몸을 옮긴 뒤 틱마다 부른다. Body는 발밑 가운데 자리와 몸이 보는 yaw다. AnimPhase는 지금 걷는 애니메이션의 재생
	 * 위치(0..1)로, 계단에 들어설 때 어느 발부터 이을지 정한다.
	 */
	void Update(const UWorld& World, const USkeletalMeshComponent& Mesh, const FTransform& Body, float DeltaSeconds,
		float AnimPhase);

	bool IsActive() const { return Alpha > 0.0f; }
	FIGWalkerIKRequest GetRequest() const;
	/** 몸을 이만큼 내린다. 낮은 디딤판에 남은 발까지 다리가 닿게 한다. */
	float GetBodyDrop() const { return BodyDrop * Alpha; }
	/** 걷는 애니메이션을 둘 재생 위치(0..1). 오른발은 0.6~1, 왼발은 0.1~0.5에 든다. */
	float GetGaitPhase() const { return GaitPhase; }

private:
	struct FFoot
	{
		/** 발목 자리(월드). 디딘 발은 머물고, 든 발은 From에서 To로 간다. */
		FVector Ankle = FVector::ZeroVector;
		/** 발바닥 높이. */
		float Sole = 0.0f;
		/** 발이 향한 월드 yaw. 디딜 자리는 이 방향으로 발을 놓고 고른다. */
		float Yaw = 0.0f;
		bool bSwing = false;
		FVector From = FVector::ZeroVector;
		FVector To = FVector::ZeroVector;
		float FromSole = 0.0f;
		float ToSole = 0.0f;
		float FromYaw = 0.0f;
		float ToYaw = 0.0f;
		/** 0..1. */
		float SwingTime = 0.0f;
		float SwingSeconds = 0.3f;
		float Lift = 3.5f;
		/** 든 발이 지나는 길 밑 바닥. From에서 To 쪽으로 PathStart부터 바닥 훑기 간격마다. */
		TArray<float> PathFloor;
		float PathStart = 0.0f;
		float PathLength = 0.0f;
	};

	float TraceFloor(const UWorld& World, const FVector& At, float AroundZ, bool* bOutStair = nullptr) const;
	/** 몸에서 길을 따라 Distance만큼 간 자리를 몸에서 곧게 보는 방향. */
	FVector AimAlongPath(const FVector& Body, float Distance) const;
	void SampleFloor(const UWorld& World, const FVector& Origin, const FVector& Direction, float Start, float End,
		float AroundZ, TArray<float>& OutFloor) const;
	/** 몸 앞뒤 바닥이 고르지 않고 그중에 계단이 있다. */
	bool IsOnStairs(const UWorld& World, const FVector& Body, const FVector& Direction) const;
	void PlantFromAnimation(const UWorld& World, const USkeletalMeshComponent& Mesh, const FVector& Body, float AnimPhase);
	void StepFeet(const UWorld& World, const FVector& Body, float DeltaSeconds);
	void StartSwing(const UWorld& World, int32 Side, const FVector& Body, int32 Treads);
	void AdvanceSwing(const UWorld& World, int32 Side, FFoot& Foot, float DeltaSeconds) const;
	void UpdateBodyDrop(const FTransform& Body, float DeltaSeconds);

	bool bBound = false;
	FName FootBones[2];
	/** 선 자세에서 메시 좌표의 엉덩이(넓적다리 뿌리)와 발목. */
	FVector RestHip[2] = {FVector::ZeroVector, FVector::ZeroVector};
	FVector RestAnkle[2] = {FVector::ZeroVector, FVector::ZeroVector};
	float LegLength[2] = {0.0f, 0.0f};
	/** 발바닥에서 발목까지. */
	float AnkleHeight = 0.0f;
	TWeakObjectPtr<const AActor> Owner;

	FFoot Feet[2];
	bool bEngaged = false;
	float Alpha = 0.0f;
	float FlatSeconds = 0.0f;
	float PlantedSeconds = 0.0f;
	int32 LastSwing = INDEX_NONE;
	/** 마지막으로 읽은 디딤판 폭. 한 걸음 거리와 뒷발을 드는 때를 이걸로 잰다. */
	float Going = 0.0f;
	/** 한 걸음에 걸리는 시간. 두 발이 다 디딘 사이에 걷는 애니메이션을 다음 발 드는 자리까지 돌린다. */
	float StepSeconds = 0.4f;
	float GaitPhase = 0.0f;
	float BodyDrop = 0.0f;
	FVector LastBody = FVector::ZeroVector;
	bool bHasLastBody = false;
	FVector Heading = FVector::ForwardVector;
	float PlanarSpeed = 0.0f;
	TArray<FVector, TInlineAllocator<4>> PathAhead;
};

/**
 * 정해진 길을 걷는 사람(rig_walker 뼈대)의 몸에 계단 걸음을 붙인다. 계단에서는 걷는 애니메이션의 다리 대신
 * 디딤판을 한 단씩 딛고, 낮은 단의 발이 닿게 몸을 낮추고, 애니메이션을 딛는 발에 맞춰 돌린다. 몸 메시는 발밑
 * 컴포넌트에 회전 없이 붙어 있어야 한다.
 */
class INDIEGAME_API FIGWalkerStairBody
{
public:
	/**
	 * 다리 IK 재생기를 끼우고, 몸 메시가 주인 액터 다음에 틱하게 한다. 메시를 넣은 뒤 부른다. 다리 뼈가 없으면 false고
	 * 평지 걷는 애니메이션 그대로다.
	 */
	bool Attach(USkeletalMeshComponent& Body, AActor& Owner);
	/** 몸을 숨기거나 순간 이동시켰다. 발을 버리고 애니메이션 다리로 돌아간다. */
	void Reset();
	/** 몸을 옮긴 뒤 틱마다 부른다. Ahead는 앞으로 지날 길 점, bGait는 걷는 애니메이션을 트는 중인가. */
	void Tick(const USceneComponent& Feet, TConstArrayView<FVector> Ahead, bool bGait, float DeltaSeconds);
	/** 계단 걸음이 다리를 쥐고 있다. 다 풀릴 때까지는 멈춰 서도 틱을 끄지 않는다. */
	bool IsActive() const { return Stairs.IsActive(); }

private:
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	FIGStairFeet Stairs;
	/** 붙일 때 몸 메시가 발밑 컴포넌트에서 놓인 자리. 계단에서는 여기서 몸을 낮춘다. */
	FVector RestLocation = FVector::ZeroVector;
	bool bAttached = false;
};
