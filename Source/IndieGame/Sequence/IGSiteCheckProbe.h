#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGSiteCheckProbe.generated.h"

class AIGListenerEntity;
class AIGListenerGreyboxDirector;
class AIGNightPhaseDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGSiteCheckVisit;
class AIGSwingDoor;

/**
 * -IGSiteCheckProbe 전용. 신고 다음 날 아침, 경찰관과 관리인이 5층 벽을 보러 오는 장면을 겪는다.
 *
 * 셋째 낮으로 놓고 신고 문자를 건너뛰어 출동부터 시작한다. 그녀는 옥상 난간 너머에서 옥상
 * 철문을 보고 있다. 관리인이 닫힌 옥상 문과 5층 문을 열쇠로 열고 둘이 올라와야 한다. 관리인이
 * 5층 문을 열 때 그녀는 5층 안에 들어가 있다. 경찰관이 가운데 칸 벽을 세 번 두드리고 귀를 댄
 * 뒤 돌아서서 말하고, 비트가 남아야 한다. 그녀가 4층으로 내려가면 둘은 사라지고 두 문은
 * 닫혀야 한다.
 *
 * -IGSiteCheckAway를 같이 주면 그녀는 문 닫은 403호에 있다. 둘은 5층에서 2분을 기다리다
 * 저희끼리 두드리고, 결과는 문자로 와야 한다.
 *
 * -IGSiteCheckShots를 같이 주면 렌더가 있는 실행에서 몇 장면을 찍는다.
 */
UCLASS()
class INDIEGAME_API AIGSiteCheckProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGSiteCheckProbe();
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
	void Look(const FVector& LookAt);
	void Shoot(const TCHAR* Name);
	bool SaidLine(const FText& Line) const;
	void NoteLines();

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGListenerGreyboxDirector> Director;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	TWeakObjectPtr<AIGSiteCheckVisit> Visit;
	TWeakObjectPtr<AIGSwingDoor> StairGate;
	TWeakObjectPtr<AIGSwingDoor> AnnexGate;

	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	bool bShots = false;
	bool bAway = false;
	bool bLeaveDuringTalk = false;
	bool bActed = false;
	bool bShotTaken = false;
	bool bDoorShotTaken = false;
	bool bStairGateOpened = false;
	bool bAnnexGateOpened = false;
	/** 화면에 한 번이라도 떴던 말. HUD는 지나간 말을 기억하지 않는다. */
	TSet<FString> SeenLines;
};
