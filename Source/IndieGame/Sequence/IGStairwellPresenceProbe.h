#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGStairwellPresenceProbe.generated.h"

class AIGPlayerCharacter;
class AIGStairwellPresence;
class APlayerController;

/**
 * -IGStairwellProbe 전용. 4층 출입구에서 계단을 걸어 내려가며 뒤따르는 발을 겪는다.
 *
 * 걷는 동안 발소리가 따라오는지, 멈추면 한 발이 더 나는지, 돌아서 보면 그 자리에
 * 서 있다가 고개를 드는지, 눈을 돌리면 사라지고 젖은 발자국이 남는지 본다. 그는
 * 한 번도 그녀를 앞질러 건너오지 않아야 한다.
 */
UCLASS()
class INDIEGAME_API AIGStairwellPresenceProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGStairwellPresenceProbe();
	virtual void Tick(float DeltaSeconds) override;

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void WalkToward(const FVector& Target);
	FVector Eye() const;

	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<AIGStairwellPresence> Presence;
	TArray<FVector> Route;
	int32 RouteIndex = 1;
	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	int32 StepsBeforeStop = 0;
	bool bCrossed = false;
	/** -IGStairwellShots: 렌더가 있는 실행에서 몇 순간을 찍는다. */
	bool bShots = false;
	int32 ShotsTaken = 0;
	float LiftedSeconds = 0.0f;
	void Shoot(const TCHAR* Name);
};
