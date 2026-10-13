#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGManagerKeyHuntProbe.generated.h"

class AIGHidingSpot;
class AIGManagerPatrol;
class AIGNightLoopDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGSwingDoor;

/**
 * -IGManagerKeyHuntProbe 전용. 셋째 밤, 유담이 계단 열쇠 꾸러미를 가져간 뒤 관리인이 그
 * 열쇠를 찾으러 도는 경로를 확인한다(EXPANSION_PLAN §3.4).
 *
 * 기본은 걸쇠를 건 403호 안에 유담이 있고 5층 철문에는 빗장이 걸려 있다. 관리인은
 * 4층 복도에 들어와 403호 도어락을 누르지만 문은 걸쇠에 걸린 채 열리지 않아야 한다.
 * 옥상 철문을 열쇠로 열고, 5층 철문 앞까지 가서 빗장에 막히고, 내려가며 옥상 문을
 * 닫고, 관리실로 돌아가야 한다. 잡히는 일은 없다.
 *
 * -IGManagerKeyHuntOpen을 같이 주면 걸쇠도 빗장도 없고 유담은 5층 안에 서 있다. 관리인은
 * 403호 문을 열어 비춰 보고 닫고, 5층 철문을 열어 유담을 보면 들어오지 않고 문을 쾅
 * 닫고 내려가야 한다.
 *
 * -IGManagerKeyHuntHidden이면 403호에는 걸쇠가 걸려 있고 5층 철문에는 빗장이 없다. 유담은
 * 5층 공구 카트 뒤에 숨어 있다. 관리인은 5층 철문을 열어 비춰 보고, 아무도 못 찾고 문을 닫고
 * 내려가야 한다. -IGManagerKeyHuntShots를 더하면 카트 뒤에서 본 화면과 관리인이 문간에서
 * 비추는 화면을 Saved/KeyHuntReview에 찍는다.
 */
UCLASS()
class INDIEGAME_API AIGManagerKeyHuntProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGManagerKeyHuntProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Stand(const FVector& Feet, float Yaw);
	void Shoot(const TCHAR* Name);
	void SetTorch(bool bOn);

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGManagerPatrol> Manager;
	TWeakObjectPtr<AIGNightLoopDirector> NightLoop;
	TWeakObjectPtr<AIGSwingDoor> HomeDoor;
	TWeakObjectPtr<AIGSwingDoor> RoofDoor;
	TWeakObjectPtr<AIGSwingDoor> AnnexDoor;
	TWeakObjectPtr<AIGHidingSpot> CartSpot;

	int32 Phase = 0;
	int32 Failures = 0;
	/** 상태가 바뀔 때마다 로그에 남긴다. 걸리면 어디서 맴도는지 보인다. */
	int32 LoggedState = -1;
	int32 CapturesAtStart = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	bool bOpen = false;
	bool bHidden = false;
	bool bShots = false;
	bool bShotSweep = false;
	/** 5층 철문이 처음 열린 때(TotalSeconds). 열리지 않았으면 음수다. */
	float AnnexOpenedAt = -1.0f;
	bool bHomeOpened = false;
	bool bHomeClosedAgain = false;
	bool bReachedHome = false;
	bool bRoofOpened = false;
	bool bReachedAnnex = false;
	bool bAnnexOpened = false;
	bool bAnnexClosedAgain = false;
	bool bEnteredAnnex = false;
	bool bRetreated = false;
};
