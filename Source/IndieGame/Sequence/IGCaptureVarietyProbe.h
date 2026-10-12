#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGCaptureVarietyProbe.generated.h"

class AIGListenerEntity;
class AIGManagerPatrol;
class AIGMissingFloorNightThreeDirector;
class AIGNightLoopDirector;
class AIGNightPhaseDirector;
class AIGNightThreatDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class UPointLightComponent;

/**
 * -IGCaptureVarietyProbe 전용. 괴이 넷에게 차례로 잡혀 보고, 잡힌 뒤가 저마다 다른지
 * 잰다(STORY_BIBLE_MISSING_FLOOR.md §5.4). 침대로 돌아가는 일은 없어야 한다.
 *
 * 1. 손님(둘째 밤): 403호 현관 안에서 깨고, 걸쇠가 뜯기고, 종이가 흩어지고, 그 밤에
 *    한 번 더 오기로 하고, 시계가 60초 밀린다.
 * 2. 관리인(셋째 밤): 403호 현관 안에 선 채로 깨고, 계단 열쇠를 거둬 가 관리실 상판에
 *    열쇠 꾸러미가 돌아오고, 시계가 180초 밀린다. 끊기는 색이 손전등 빛이다.
 * 3. 어둑시니: 마지막으로 불빛 아래 섰던 자리에서 깨고, 건전지가 0.35 줄고, 시계가
 *    90초 밀린다. 잡히는 동안 시선이 위로 들린다.
 * 4. 위층 사람: 지나온 길로 3m 넘게 끌려간 자리에서 잡힌 쪽을 보고 깨고, 손전등은 잡힌
 *    자리 바닥에 켜진 채 떨어져 있고, 단계가 하나 오르고, 시계가 120초 밀리고, 깬 뒤
 *    12초 동안은 큰 소리에도 오지 않는다. 떨어진 손전등을 주우면 켜진 채 손에 돌아온다.
 *
 * 깨어난 뒤 바닥에서 일어나는 동안은 걷지 못하고(관리인은 선 채라 바로 걷는다), 그
 * 뒤에는 걸을 수 있어야 한다. -IGCaptureVarietyShots를 같이 주면 렌더가 있는 실행에서
 * 잡히는 동안과 깨어난 뒤를 찍는다(Saved/CaptureVarietyReview).
 */
UCLASS()
class INDIEGAME_API AIGCaptureVarietyProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGCaptureVarietyProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer, AIGListenerEntity* InListener);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Next(int32 NewPhase);
	/** 발 자리(월드)에 세우고 LookAt을 보게 한다. */
	void Stand(const FVector& Feet, const FVector& LookAt);
	void Shoot(const TCHAR* Name);
	/** 잡히는 동안 정해 둔 때마다 한 장씩 찍는다. */
	void ShootDuringCapture(const TCHAR* Kind, float Seconds);
	bool IsMoveLocked() const;
	/** 포획이 끝나 조작이 돌아온 뒤 처음 부른다. 깬 자리와 값을 기록한다. */
	void NoteWake(const TCHAR* Kind);

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGNightThreatDirector> Threats;
	TWeakObjectPtr<AIGNightLoopDirector> NightLoop;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	TWeakObjectPtr<AIGManagerPatrol> Manager;
	TWeakObjectPtr<AIGMissingFloorNightThreeDirector> NightThree;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> ProbeLamp;

	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	bool bShots = false;
	bool bActed = false;
	bool bWokeNoted = false;
	int32 ShotIndex = 0;
	double CaptureStartedAt = -1.0;
	float HourBefore = 0.0f;
	float BatteryBefore = 1.0f;
	int32 TierBefore = 0;
	float MaxCapturePitch = -90.0f;
	bool bSawGlare = false;
	bool bSawMoveLockAfterWake = false;
	FVector WakeLocation = FVector::ZeroVector;
};
