#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGBathroomProbe.generated.h"

class AIGBathroomRefuge;
class AIGListenerEntity;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGSwingDoor;
class UIGNoiseSubsystem;
struct FIGNoiseEvent;

/**
 * -IGBathroomProbe 전용. 403호 욕실에 숨어 본다(EXPANSION_PLAN §4).
 *
 * 밖에서 욕실 문을 열고 들어가 안에서 닫고, 로제트 단추로 잠그면 숨은 것이 되는지 본다.
 * 403호 현관을 열어 둔 채 위층 사람을 방 안에 세우고 욕실 안에서 소리를 내면, 그는 벽을
 * 더듬지 않고 욕실 문 앞에 와서 두드려야 한다. 셋째 밤 손님이 현관으로 들어오는 순간
 * 욕실 안에서 소리를 내면, 손님은 잠긴 손잡이만 덜컥거리고 그냥 나가야 한다. 마지막으로
 * 잠금을 풀고 안에서 문을 연다.
 *
 * -IGBathroomShots를 같이 주면 렌더가 있는 실행에서 몇 장면을 찍는다.
 */
UCLASS()
class INDIEGAME_API AIGBathroomProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGBathroomProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(
		AIGPrologueWorldScene* InScene,
		AIGPlayerCharacter* InPlayer,
		AIGListenerEntity* InListener,
		UIGNoiseSubsystem* InNoise);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Stand(const FVector& Feet, const FVector& LookAt);
	/** E를 툭 누른다. 초점이 Expected여야 누른다. */
	bool Tap(const AActor* Expected, const TCHAR* FocusCheck);
	void Shoot(const TCHAR* Name);
	void HandleNoise(const FIGNoiseEvent& Event);

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<UIGNoiseSubsystem> Noise;
	TWeakObjectPtr<AIGBathroomRefuge> Refuge;
	TWeakObjectPtr<AIGSwingDoor> Door;
	FDelegateHandle NoiseHandle;

	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	bool bListenerKnocked = false;
	bool bListenerEntered = false;
	FVector ListenerKnockAt = FVector::ZeroVector;
	/** 위층 사람의 노크가 난 자리(그의 몸 자리가 아니라 소리가 난 자리). */
	bool bKnockSoundHeard = false;
	bool bKnockSoundBound = false;
	FVector KnockSoundAt = FVector::ZeroVector;
	bool bGuestOpened = false;
	bool bRattleHeard = false;
	bool bShots = false;
};
