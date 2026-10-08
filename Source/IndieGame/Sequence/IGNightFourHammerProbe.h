#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGNightFourHammerProbe.generated.h"

class AIGDoorLatch;
class AIGListenerEntity;
class AIGMissingFloorEvidence;
class AIGMissingFloorNightFourDirector;
class AIGNightLoopDirector;
class AIGNightThreatDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGSwingDoor;

/**
 * -IGNightFourHammerProbe 전용. 밤4 망치질 사이를 겪는다(EXPANSION_PLAN §2.3 넷째 밤).
 *
 * 밤4로 놓고 진실 셋(벽 속 사람, 살아 있었다, 대답을 기다린다)과 물소리 가면을 세운 뒤 5층
 * 벽을 친다. 셋째 타 뒤 정전에 건물 안 등이 다 죽어야 한다. 넷째 타 뒤에는 손이 저려 잠깐
 * 못 치고, 그 사이 5층 철문 밖에서 「유담아. 오빠야. 문 좀 열어 줘.」가 오고, 벽 안에서
 * 둘, 쉬고, 하나가 오면 다시 칠 수 있어야 한다. 빗장을 걸어 두었으므로 손님은 열쇠를 꽂다
 * 빗장에 걸려 간다. 그 뒤 정전 속에 어둑시니가 빚은 몸(SK_Eoduksini)으로 서야 하고, 다섯째 타에
 * 벽이 열리면 어둑시니와 손님이 다 그쳐야 한다.
 *
 * -IGNightFourHammerOpen을 주면 빗장을 걸지 않는다. 손님이 열고 들어와 잡으면 결말 C가 아니라
 * 침대로 돌아가야 하고(공격성 3 아래), 깨어난 403호도 깜깜해야 한다.
 *
 * -IGNightFourHammerShots를 같이 주면 렌더가 있는 실행에서 몇 장면을 찍는다.
 */
UCLASS()
class INDIEGAME_API AIGNightFourHammerProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGNightFourHammerProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer, AIGListenerEntity* InListener);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Next(int32 NewPhase);
	void Stand(const FVector& Feet, const FVector& LookAt);
	void Shoot(const TCHAR* Name);
	bool SaidLine(const FText& Line) const;
	/** 벽을 한 번 친다. 칠 수 없으면 거짓이다. */
	bool Strike();
	bool IsCapturedSince(double Seconds) const;

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGMissingFloorNightFourDirector> NightFour;
	TWeakObjectPtr<AIGNightThreatDirector> Threats;
	TWeakObjectPtr<AIGNightLoopDirector> NightLoop;
	TWeakObjectPtr<AIGSwingDoor> AnnexDoor;
	TWeakObjectPtr<AIGDoorLatch> AnnexBolt;

	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	bool bShots = false;
	bool bOpenDoor = false;
	bool bActed = false;
	int32 Struck = 0;
	bool bSawHeld = false;
	bool bSawCall = false;
	bool bSawCaughtOnBolt = false;
	double CaptureBaseline = 0.0;
};
