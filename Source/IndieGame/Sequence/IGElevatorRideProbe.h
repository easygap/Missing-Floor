#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGElevatorRideProbe.generated.h"

class AIGElevator;
class AIGPlayerCharacter;
class APlayerController;

/**
 * -IGElevatorRideProbe 전용. 실제 무대에서 승강기를 타 보고 종료한다.
 *
 * 4층에서 칸을 불러 타고 1층까지 내려간 뒤, 2층에서 다시 불러 3층으로 올라간다.
 * 칸에 들고 나는 것은 이동 입력으로 걷는다. 칸이 움직이는 동안 몸이 칸 바닥을
 * 딛고 따라가는지(바닥과 발의 틈, 이동 기반), 한 번이라도 순간이동하는지 잰다.
 * 그 시간에는 버튼이 눌려도 칸이 서 있는지도 본다.
 */
UCLASS()
class INDIEGAME_API AIGElevatorRideProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGElevatorRideProbe();
	virtual void Tick(float DeltaSeconds) override;

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void PlaceAtLanding(int32 Landing);
	void WalkToward(const FVector& Target);
	void TrackRide(float DeltaSeconds);
	/** -IGElevatorReview: 정해 둔 자리에서 화면을 찍는다. 렌더가 있는 실행에서만 쓴다. */
	void TickReview(float DeltaSeconds);
	void Shoot(const TCHAR* Name);
	/** -IGElevatorOverloadProbe: 만원을 실제로 겪는다. -IGElevatorOverloadLeave면 부저가 울 때 내린다. */
	void TickOverload(float DeltaSeconds);

	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<AIGElevator> Elevator;
	int32 Phase = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	int32 Failures = 0;
	float RideSeconds = 0.0f;
	float MaxFloorGap = 0.0f;
	/** 한 틱의 이동을 그 틱의 시간으로 나눈 속도(cm/s). 프레임 길이와 무관하게 순간이동을 잡는다. */
	float MaxPawnStep = 0.0f;
	float MaxCabStep = 0.0f;
	int32 RideSamples = 0;
	int32 OffBaseSamples = 0;
	float HeldCabZ = 0.0f;
	FVector LastPawnLocation = FVector::ZeroVector;
	float LastCabZ = 0.0f;
	bool bHasLast = false;
	bool bReview = false;
	bool bOverload = false;
	bool bLeave = false;
	float MinCabZ = 0.0f;
	float MaxCabZ = 0.0f;
	int32 MaxFigures = 0;
	bool bSawBack = false;
	bool bSawAbsent = false;
	bool bDoorsOpenAtFive = false;
	/** -IGElevatorShots: 만원의 몇 순간을 찍는다(렌더가 있는 실행에서만). */
	bool bShots = false;
	int32 ShotsTaken = 0;
	int32 ShotIndex = 0;
};
