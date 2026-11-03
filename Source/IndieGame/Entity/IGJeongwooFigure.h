#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Entity/IGClipAudit.h"
#include "Entity/IGWalkerKit.h"
#include "Entity/IGWalkerLegIK.h"
#include "IGJeongwooFigure.generated.h"

class UAnimSequence;
class UCapsuleComponent;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 202호 정우의 몸(SK_Jeongwoo, rig_walker.py --profile mover). 헬멧을 쓴 채 다니는 배달 기사다.
 *
 * 걷고, 상자를 안고 걷고, 쪼그려 앉고, 판을 받치고, 판 뒤에서 판을 짚고, 문을 잡는다. 어디로 가고 무엇을 할지는
 * 짐 줄기 감독(AIGMissingFloorLeftoverDirector)이 정한다. 이 액터는 정해 준 길을 걷고
 * 다 걸었으면 알린다. 길은 점을 이은 것이고, 높이가 30 cm 넘게 바뀌는 구간은 계단이다.
 * 몸은 플레이어만 막는다. 틱은 걷는 동안에만 켠다(관통을 재는 검사에서는 보이는 동안 늘 켠다).
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
		LookBack,
		/** 좁은 구석에 발을 모으고 서서 두 손으로 판 뒷면을 짚는다(302호 판 뒤). */
		Hold
	};

	AIGJeongwooFigure();

	/** 몸과 동작을 불러온다. 몸이나 걷기·서기가 없으면 false이고 정우는 나오지 않는다. */
	bool LoadBody();
	bool HasBody() const { return bBodyLoaded; }

	/** 그 자리에 나타난다. 걷고 있었으면 멈춘다. */
	void Appear(const FVector& Feet, float Yaw, EPose Pose);
	void Vanish();
	bool IsShown() const { return bShown; }

	/**
	 * 점을 따라 걷는다. bCarry면 상자를 안고 걷는다. 다 걸으면 EndPose로 서고 OnArrived를 알린다.
	 * ArriveYaw가 있으면 다 걸은 뒤 제자리에서 그쪽으로 돌아선 다음 EndPose로 선다.
	 */
	void WalkRoute(const TArray<FVector>& Points, bool bCarry, EPose EndPose, float Speed = 95.0f, TOptional<float> ArriveYaw = {});
	/** 걷거나 다 걸은 자리에서 돌아서는 중이다. */
	bool IsWalking() const { return bWalking || bTurning; }
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

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 걷거나 돌아선다. 몸은 여기서 다 옮긴다. */
	void TickWalk(float DeltaSeconds);
	void PlayPose(EPose Pose, float Rate);
	UAnimSequence* AnimFor(EPose Pose) const;
	void PlayStep();
	/** 다 걷고(돌아서고) 선다. */
	void FinishArrival();
	/**
	 * 안은 상자는 발보다 65 cm 앞까지 나온다. 길이 꺾이는 점에서는 몸이 들어오던 쪽을 본 채 돌아서므로, 그 앞이
	 * 막혀 있으면 막힌 만큼 점을 들어오던 쪽으로 물린다(계단참 벽에 상자가 19 cm 묻혔다).
	 */
	void ClearCornersForBoxes(TArray<FVector>& Points) const;
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
	/** 다 걸은 자리에서 TurnToYaw로 돌아서는 중. */
	bool bTurning = false;
	TOptional<float> TurnToYaw;
	/**
	 * 첫 구간이 보는 쪽에서 크게 꺾여, 걷기 전에 제자리에서 DepartYaw로 돌아서는 중. 걸으면서 돌면 첫걸음이 옆으로
	 * 나가 곁의 판을 디뎠다.
	 */
	bool bDeparting = false;
	float DepartYaw = 0.0f;
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
	/** 계단에서는 걷는 애니메이션의 다리 대신 한 단씩 디딘다. */
	FIGWalkerStairBody StairBody;
	/** 검사 실행에서 몸이 벽·문·계단을 지나는지 잰다(IGClipAudit). */
	FIGClipAudit ClipAudit;
};
