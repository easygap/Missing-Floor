#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGStairSensorProbe.generated.h"

class AIGListenerEntity;
class AIGNightPhaseDirector;
class AIGNightThreatDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGStairSensorLights;

/**
 * -IGStairSensorProbe 전용. 첫째 밤의 계단탑에서 센서등과 어둑시니를 겪는다.
 *
 * 2층 참에서 걸으면 그 참의 등이 켜지고, 멈춰 서면 9초 뒤에 꺼지는지 본다. 반 층
 * 참과 복도 출입구 밖은 감지하지 않아야 한다. 1층 공용 차단기나 넷째 밤 차단기가
 * 내려가 있으면 걸어도 켜지지 않아야 한다. 마지막으로 3층 참에 멈춰 서서 등이 일찍
 * 꺼지면 계단 아래 어둠에 어둑시니가 서는지, 쳐다봐도 다가오지 않는지, 한 걸음
 * 움직여 등이 다시 켜지면 사라지는지 본다.
 *
 * -IGStairSensorShots를 같이 주면 렌더가 있는 실행에서 몇 장면을 찍는다.
 */
UCLASS()
class INDIEGAME_API AIGStairSensorProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGStairSensorProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(
		AIGPrologueWorldScene* InScene,
		AIGPlayerCharacter* InPlayer,
		AIGNightThreatDirector* InThreats,
		AIGNightPhaseDirector* InNightPhase,
		AIGListenerEntity* InListener);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Place(const FVector& Feet, float Yaw);
	void Walk(const FVector& Direction);
	FVector Feet() const;
	FVector Eye() const;
	void Shoot(const TCHAR* Name);

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGNightThreatDirector> Threats;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGStairSensorLights> Sensors;

	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	int32 SwitchCountBefore = 0;
	FVector FigureAt = FVector::ZeroVector;
	bool bShots = false;
};
