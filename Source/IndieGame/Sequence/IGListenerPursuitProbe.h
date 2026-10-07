#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGListenerPursuitProbe.generated.h"

class AIGListenerEntity;
class AIGPlayerCharacter;
class UIGNoiseSubsystem;

/**
 * -IGListenerPursuitProbe 전용. 위층 사람이 층을 오가며 쫓고, 놓친 뒤 둘레를 뒤지는지
 * 실제 무대에서 잰다.
 *
 * 밤2의 관리실 붕괴처럼 1층에서 큰 소리를 두 번 낸다. 4층의 그가 계단탑을 타고
 * 1층까지 내려와야 하고, 내려오는 동안 몸이 디딤판 위에 붙어 있어야 하며(공중에
 * 뜨거나 바닥에 묻히지 않는다) 한 번도 순간이동하지 않아야 한다. 소리가 끊긴 뒤에는
 * 곧바로 순찰로 돌아가지 않고 여러 자리를 들러 귀를 대야 한다. 마지막으로 그녀가
 * 계단을 뛰어 올라가는 발소리를 따라 다시 4층까지 올라오는지 본다.
 */
UCLASS()
class INDIEGAME_API AIGListenerPursuitProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGListenerPursuitProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(AIGListenerEntity* InEntity, AIGPlayerCharacter* InPlayer, UIGNoiseSubsystem* InNoise);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Report(const FVector& Location, float Loudness);
	void Track(float DeltaSeconds);
	float FeetZ() const;

	TWeakObjectPtr<AIGListenerEntity> Entity;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<UIGNoiseSubsystem> Noise;

	int32 Phase = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	int32 Failures = 0;

	FVector LastLocation = FVector::ZeroVector;
	bool bHasLast = false;
	float MaxStepPerTick = 0.0f;
	float LastTickDistance = 0.0f;
	float MaxFloorGapOnStairs = 0.0f;
	float MaxBodyPitch = 0.0f;
	int32 StairSamples = 0;
	float LowestFeetZ = 100000.0f;
	float HighestFeetZ = -100000.0f;
	float TrackLogSeconds = 0.0f;

	// 수색 관찰
	TArray<FVector> PauseSpots;
	float StillSeconds = 0.0f;
	float SearchingSeconds = 0.0f;
	float SilenceStartedAt = 0.0f;
	bool bReturnedToPatrolEarly = false;

	// 오르는 발소리
	TArray<FVector> ClimbNoises;
	int32 ClimbNoiseIndex = 0;
	float ClimbNoiseSeconds = 0.0f;
};
