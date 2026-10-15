#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Entity/IGWalkerKit.h"
#include "IGJeongwooFigure.generated.h"

class UAnimSequence;
class UCapsuleComponent;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 202호 정우의 몸(SK_Jeongwoo, rig_walker.py --profile mover). 헬멧을 쓴 채 다니는 배달 기사다.
 *
 * 걷고, 상자를 안고 걷고, 쪼그려 앉고, 판을 받치고, 문을 잡는다. 어디로 가고 무엇을 할지는
 * 짐 줄기 감독(AIGMissingFloorLeftoverDirector)이 정한다. 이 액터는 정해 준 길을 걷고
 * 다 걸었으면 알린다. 길은 점을 이은 것이고, 높이가 30 cm 넘게 바뀌는 구간은 계단이다.
 * 몸은 플레이어만 막는다. 틱은 걷는 동안에만 켠다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGJeongwooFigure : public AActor
{
	GENERATED_BODY()

public:
	enum class EPose : uint8
	{
		Idle,
		Walk,
		Carry,
		CarryIdle,
		Crouch,
		Brace,
		HoldDoor,
		LookBack
	};

	AIGJeongwooFigure();

	/** 몸과 동작을 불러온다. 몸이나 걷기·서기가 없으면 false이고 정우는 나오지 않는다. */
	bool LoadBody();
	bool HasBody() const { return bBodyLoaded; }

	/** 그 자리에 나타난다. 걷고 있었으면 멈춘다. */
	void Appear(const FVector& Feet, float Yaw, EPose Pose);
	void Vanish();
	bool IsShown() const { return bShown; }

	/** 점을 따라 걷는다. bCarry면 상자를 안고 걷는다. 다 걸으면 EndPose로 서고 OnArrived를 알린다. */
	void WalkRoute(const TArray<FVector>& Points, bool bCarry, EPose EndPose, float Speed = 95.0f);
	bool IsWalking() const { return bWalking; }
	void SetPose(EPose Pose);
	EPose GetPose() const { return CurrentPose; }
	void FaceYaw(float Yaw);
	void FaceTowards(const FVector& Where);

	/** 안고 있는 상자 수(0..2). 상자 메시는 가슴 앞에 쌓인다. */
	void SetCarriedBoxes(int32 Count);
	int32 GetCarriedBoxes() const { return CarriedBoxes; }
	/** 플레이어를 막을지(좁은 문간에서는 끈다). */
	void SetBlocksPlayer(bool bBlocks);

	FVector GetFeet() const { return GetActorLocation(); }
	/** 검사용. 뼈 자리를 읽는다. */
	USkeletalMeshComponent* GetBody() const { return Body; }
	FVector GetHeadLocation() const { return GetActorLocation() + FVector(0.0f, 0.0f, 165.0f); }
	/** 검사용. 걸으며 낸 발소리 수. */
	int32 GetStepCount() const { return StepCount; }

	FSimpleMulticastDelegate OnArrived;

	virtual void Tick(float DeltaSeconds) override;

private:
	void PlayPose(EPose Pose, float Rate);
	UAnimSequence* AnimFor(EPose Pose) const;
	void PlayStep();
	/**
	 * 몸막이를 지금 상태에 맞춘다. 걷는 동안은 끈다(밀고 지나가면 그녀가 떠밀려 판 옆에 끼었다).
	 * 멈춘 자리에 그녀가 겹쳐 서 있으면 켜는 순간 그 안에 갇히므로 비킬 때까지 미룬다.
	 */
	void RefreshBlocker();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> FeetRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCapsuleComponent> Blocker;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Boxes;

	UPROPERTY(Transient)
	TMap<uint8, TObjectPtr<UAnimSequence>> Anims;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActiveAnim;

	FIGWalkerRoute Route;
	float Travelled = 0.0f;
	float WalkSpeed = 95.0f;
	bool bWalking = false;
	bool bCarryWalk = false;
	bool bShown = false;
	bool bBodyLoaded = false;
	bool bBlocksPlayer = true;
	/** 그녀가 비키기를 기다리며 몸막이를 미뤄 둔 상태. 틱이 다시 본다. */
	bool bBlockerDeferred = false;
	EPose CurrentPose = EPose::Idle;
	EPose ArrivePose = EPose::Idle;
	int32 CarriedBoxes = 0;
	float StepPhase = 0.0f;
	uint32 StepSeed = 202u;
	int32 StepCount = 0;
};
