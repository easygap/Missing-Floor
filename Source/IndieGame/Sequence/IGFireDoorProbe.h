#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGFireDoorProbe.generated.h"

class AIGFireDoorWedge;
class AIGListenerEntity;
class AIGManagerPatrol;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class UIGNoiseSubsystem;
struct FIGNoiseEvent;

/**
 * -IGFireDoorProbe 전용. 2·3·4층 계단 목의 방화문과 고임목을 겪는다.
 *
 * 세 층 모두 고임목으로 괴어 열려 있고 열린 문짝이 남쪽 벽을 긁지 않는지 본다. 2층
 * 고임목을 E로 툭 빼면 문이 쾅 닫히고(0.42), 3층 고임목은 E를 1초 누르고 있으면 조용히
 * 닫히는지(0.08) 본다. 닫힌 2층 문 너머 계단 참에서 소리가 나면 위층 사람이 문을 뚫지
 * 않고 문 앞에서 두드리는지, 셋째 밤의 관리인은 닫힌 2층 문을 열고 지나가는지 본다.
 * 202호 쪽지, 303호 안내문, 그 시간의 새벽배송 가방도 그 자리에 있는지 같이 본다.
 *
 * -IGFireDoorShots를 같이 주면 렌더가 있는 실행에서 몇 장면을 찍는다.
 */
UCLASS()
class INDIEGAME_API AIGFireDoorProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGFireDoorProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(
		AIGPrologueWorldScene* InScene,
		AIGPlayerCharacter* InPlayer,
		AIGListenerEntity* InListener,
		AIGManagerPatrol* InManager,
		UIGNoiseSubsystem* InNoise);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	/** 발을 Feet에 두고 눈이 LookAt을 보게 선다. */
	void Stand(const FVector& Feet, const FVector& LookAt);
	void Shoot(const TCHAR* Name);
	void HandleNoise(const FIGNoiseEvent& Event);
	/** 처음 한 번. 문 셋, 종이 둘, 가방을 센다. */
	void CheckStage();

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGManagerPatrol> Manager;
	TWeakObjectPtr<UIGNoiseSubsystem> Noise;
	/** 아래층부터 2·3·4층. */
	TArray<TWeakObjectPtr<AIGFireDoorWedge>> Wedges;
	FDelegateHandle NoiseHandle;

	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	/** 지켜보는 문 둘레에서 난 가장 큰 소리. */
	FVector NoiseWatchAt = FVector::ZeroVector;
	float LoudestWatched = 0.0f;
	float ListenerMinX = 0.0f;
	bool bListenerKnocked = false;
	FVector ListenerKnockAt = FVector::ZeroVector;
	/** 위층 사람의 노크가 난 자리(그의 몸 자리가 아니라 소리가 난 자리). */
	bool bKnockSoundHeard = false;
	bool bKnockSoundBound = false;
	FVector KnockSoundAt = FVector::ZeroVector;
	bool bManagerOpenedDoor = false;
	bool bManagerCrossedClosedDoor = false;
	bool bShots = false;
};
