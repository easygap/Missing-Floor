#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGStairNeighbor.generated.h"

class AIGStairNeighbor;
class UAnimSequence;
class UCapsuleComponent;
class USkeletalMeshComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FIGStairNeighborEvent, AIGStairNeighbor*);

/**
 * 303호(EXPANSION_PLAN §2.3 셋째 낮, §2.4). 3교대로 일하는 간호사다.
 *
 * 셋째 낮 오후, 출근하는 길에 403호를 보려고 계단을 올라온다. 계단이나 4층 계단 목에서
 * 유담과 마주치면 멈춰 서서 말하고, 말이 끝나면 계단으로 내려가 건물을 나간다. 끝내
 * 못 마주치면 계단 목에서 한참 서 있다가 그냥 내려간다. 문은 두드리지 않는다. 자기
 * 집 문에도 노크하지 말아 달라고 써 붙인 사람이다.
 *
 * 길은 303호 문 앞에서 3층 계단 목과 계단을 지나 4층 계단 목까지다. 내려갈 때는 선
 * 자리에서 그 길을 거꾸로 걷고, 3층부터는 1층 출입구까지 계단으로 내려가 주차장
 * 쪽으로 나간다. 계단에서는 디딤판 한 칸에 한 걸음이다. 몸은 리깅한 SK_Neighbor303이고
 * 충돌은 플레이어 몸만 막는다. 길을 막고 서 있으면 비켜 줄 때까지 선다.
 *
 * 말과 비트는 디렉터가 쥔다. 이 액터는 걷고, 마주친 순간을 알리고, 떠나라면 떠난다.
 * 틱은 나와 있는 동안에만 켠다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGStairNeighbor : public AActor
{
	GENERATED_BODY()

public:
	enum class EStage : uint8
	{
		/** 아직 안 나왔다. */
		Hidden,
		/** 303호 앞에서 4층 계단 목까지 올라온다. */
		Arriving,
		/** 4층 계단 목에서 403호 쪽을 보고 서 있다. */
		Waiting,
		/** 멈춰 서서 그녀를 보고 말한다. */
		Talking,
		/** 계단으로 내려가 건물을 나간다. */
		Leaving,
		/** 나갔다. */
		Gone
	};

	AIGStairNeighbor();

	/** 몸과 동작을 불러온다. 하나라도 없으면 false이고 이 사람은 나오지 않는다. */
	bool LoadBody();
	/** 303호 문 앞에서 나와 올라오기 시작한다. */
	void BeginArrival();
	/** 말을 마쳤거나 기다리다 지쳤다. 계단으로 내려가 건물을 나간다. */
	void Leave();
	/** 밤이 왔다. 그 자리에서 없앤다. */
	void Vanish();

	virtual void Tick(float DeltaSeconds) override;

	EStage GetStage() const { return Stage; }
	bool IsOut() const { return Stage != EStage::Hidden && Stage != EStage::Gone; }
	double GetMetAt() const { return MetAt; }
	FVector GetFeet() const { return GetActorLocation(); }
	/** 검사용. 걸으며 낸 발소리 수. */
	int32 GetStepCount() const { return StepCount; }

	/** 그녀와 마주쳐 멈춰 섰다. 디렉터가 말을 띄운다. */
	FIGStairNeighborEvent OnMet;
	/** 계단 목에서 기다리다 못 보고 내려가기 시작했다. */
	FIGStairNeighborEvent OnGaveUp;
	/** 건물을 나갔다. */
	FIGStairNeighborEvent OnGone;

	/** 303호 문 앞, 나오는 발자리. */
	static FVector GetStartFeet();
	/** 4층 계단 목에서 403호 쪽을 보고 서는 발자리. */
	static FVector GetWaitingFeet();
	/** 그녀가 이 자리에 있으면 303호 문 앞이 보일 수 있다. 그동안은 나오지 않는다. */
	static bool CanWatchThirdFloorDoor(const FVector& PlayerFeet);

private:
	void SetRoute(const TArray<FVector>& Points);
	FVector RoutePoint(float Distance, FVector* OutDirection = nullptr, bool* bOutOnStair = nullptr) const;
	float GetRouteLength() const { return RouteDistance.IsEmpty() ? 0.0f : RouteDistance.Last(); }
	void Walk(float DeltaSeconds);
	bool CheckMeet(double Now);
	bool CanSeePlayer() const;
	bool IsPlayerInTheWay() const;
	FVector GetPlayerEyes(const APawn& Pawn) const;
	bool GetPlayerFeet(FVector& OutFeet) const;
	void PlayBody(UAnimSequence* Sequence, float Rate);
	void PlayStep(bool bOnStair);
	void TurnTowards(float TargetYaw, float DeltaSeconds, float Speed);
	void Show(bool bVisible);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> FeetRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCapsuleComponent> Blocker;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> WalkAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActiveAnim;

	EStage Stage = EStage::Hidden;
	/** 지금 걷는 길과 그 길의 누적 거리(cm). */
	TArray<FVector> Route;
	TArray<float> RouteDistance;
	float Travelled = 0.0f;
	/** 올라온 길. 도중에 멈췄다 내려갈 때 거꾸로 걷는다. */
	TArray<FVector> ArrivalRoute;
	TArray<float> ArrivalDistance;
	float ArrivalTravelled = 0.0f;
	/** 다 내려간 뒤 그녀 눈에 아직 보이면 조금 더 걷는다. */
	float ExitWalkSeconds = 0.0f;
	float StepPhase = 0.0f;
	uint32 StepSeed = 303u;
	int32 StepCount = 0;
	bool bCaptionedSteps = false;
	double StageStartedAt = 0.0;
	double MetAt = -1.0;
	double LastSightCheckAt = -1.0;
};
