#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGGuestDoorsProbe.generated.h"

class AIGDoorLatch;
class AIGListenerEntity;
class AIGNightLoopDirector;
class AIGNightThreatDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGSwingDoor;

/**
 * -IGGuestDoorsProbe 전용. 손님이 403호 말고 다른 문에도 오는지 본다(EXPANSION_PLAN §3.3).
 *
 * 기본: 셋째 밤, 403호에 걸쇠를 걸고 기다리면 손님은 401호와 402호 문을 차례로 두드린 뒤
 * 403호로 와야 하고, 걸쇠에 걸려 돌아가야 한다. 이어 넷째 밤, 5층에 들어가 철문 안쪽 빗장을
 * 걸고 기다리면 손님은 5층 철문을 두드리고 열쇠를 꽂은 뒤 빗장에 걸려 돌아가야 한다.
 *
 * -IGGuestLure: 셋째 밤 손님이 401호를 두드리는 사이 403호 문을 열면 몸이 그 문 앞에 나타나
 * 이쪽으로 와야 하고, 닫으면 몸이 사라지고 403호를 두드려야 한다. 이어 넷째 밤, 5층 철문을
 * 열어 둔 채 안에 있으면 손님은 문간에 서야 하고, 닫으면 밖에서 두드리다가 빗장이 없으니 열고
 * 들어와 그녀를 잡아야 한다.
 *
 * -IGGuestDoorsShots를 같이 주면 렌더가 있는 실행에서 몇 장면을 찍는다.
 */
UCLASS()
class INDIEGAME_API AIGGuestDoorsProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGGuestDoorsProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer, AIGListenerEntity* InListener);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	/** 발 자리(월드)에 세우고 LookAt을 보게 한다. */
	void Stand(const FVector& Feet, const FVector& LookAt);
	/** E를 툭 누른다. 초점이 Expected여야 누른다. */
	bool Tap(const AActor* Expected, const TCHAR* FocusCheck);
	void Shoot(const TCHAR* Name);
	void Next(int32 NewPhase);
	bool IsCapturedSince(double Seconds) const;
	void TickDefault();
	void TickLure();

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGNightThreatDirector> Threats;
	TWeakObjectPtr<AIGNightLoopDirector> NightLoop;
	TWeakObjectPtr<AIGSwingDoor> HomeDoor;
	TWeakObjectPtr<AIGSwingDoor> AnnexDoor;
	TWeakObjectPtr<AIGSwingDoor> RoofDoor;
	TWeakObjectPtr<AIGDoorLatch> AnnexBolt;

	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	bool bShots = false;
	bool bLure = false;
	uint8 LastStage = 0;
	bool bSaw402 = false;
	bool bSawKeypad = false;
	bool bSawCaught = false;
	bool bSawInside = false;
	bool bActed = false;
	double CaptureBaseline = 0.0;
	FVector LureFirstBody = FVector::ZeroVector;
};
