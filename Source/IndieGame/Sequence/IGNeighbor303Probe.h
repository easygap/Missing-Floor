#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGNeighbor303Probe.generated.h"

class AIGListenerEntity;
class AIGListenerGreyboxDirector;
class AIGNightPhaseDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGStairNeighbor;

/**
 * -IGNeighbor303Probe 전용. 셋째 낮 계단의 303호를 겪는다(EXPANSION_PLAN §2.3).
 *
 * 셋째 낮으로 놓고 신고 문자를 건너뛰어 303호를 바로 올려 보낸다. 그녀는 문을 닫은 403호
 * 안에 있다. 303호가 올라오는 동안 3층과 4층 참의 센서등이 켜지고 발소리가 나야 하며, 4층
 * 계단 목에 서서 403호 쪽을 봐야 한다. 그녀가 복도로 나오면 멈춰 서서 세 줄을 나누고, 말이
 * 끝나면 계단으로 내려가 건물을 나가야 한다. 내려가는 뒤에 유담의 속말이 한 줄 뜬다.
 *
 * -IGNeighbor303Stairs를 같이 주면 그녀가 3층과 4층 사이 계단에 서 있다. 303호는 올라오다
 * 반 층 참 언저리에서 그녀를 보고 멈춰 말하고, 그 자리에서 돌아 내려가야 한다.
 *
 * -IGNeighbor303Shots를 같이 주면 렌더가 있는 실행에서 몇 장면을 찍는다.
 */
UCLASS()
class INDIEGAME_API AIGNeighbor303Probe : public AActor
{
	GENERATED_BODY()

public:
	AIGNeighbor303Probe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(
		AIGPrologueWorldScene* InScene,
		AIGPlayerCharacter* InPlayer,
		AIGListenerEntity* InListener,
		AIGListenerGreyboxDirector* InDirector);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Next(int32 NewPhase);
	void Stand(const FVector& Feet, const FVector& LookAt);
	void Shoot(const TCHAR* Name);
	bool SaidLine(const FText& Line) const;

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGListenerGreyboxDirector> Director;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	TWeakObjectPtr<AIGStairNeighbor> Neighbor;

	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	bool bShots = false;
	bool bOnStairs = false;
	bool bActed = false;
	int32 LampsBefore[4] = {0, 0, 0, 0};
	float LowestZ = 0.0f;
	/** 계단 쪽에서 그녀가 4층 참을 떠난 때(TotalSeconds). 음수면 아직이다. */
	float DescendStartedAt = -1.0f;
};
