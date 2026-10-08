#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGDayScenesProbe.generated.h"

class AIGListenerEntity;
class AIGMissingFloorEvidence;
class AIGNightPhaseDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGReadableNote;

/**
 * -IGDayScenesProbe 전용. 새 이웃과 낮 장면을 겪는다(EXPANSION_PLAN §2.3~2.5).
 *
 * 입주 저녁에 401호를 두드리면 황순금이 어둠 이야기를 하는지 본다. 첫째 밤을 채워 첫째 낮으로
 * 넘어가면 403호 문에 303호 둘째 쪽지가 붙고, 폰의 동네장터에 「동네 이야기」 두 장이 생기고,
 * 연석의 오토바이에 정우가 앉아 있다가 말을 걸면 이름을 대야 한다. 둘째 밤을 거쳐 둘째 낮이
 * 되면 셋째 쪽지가 붙고, 동네 이야기가 세 장이 되고, 정우는 엄마 목소리 이야기를, 황순금은
 * 손 있는 날 이야기를 해야 한다. 마지막으로 현관문을 열어 쪽지가 문과 같이 도는지 본다.
 *
 * -IGDayScenesShots를 같이 주면 렌더가 있는 실행에서 몇 장면을 찍는다.
 *
 * -IGEpilogueDoorStill은 낮 장면을 건너뛰고 에필로그 「403호 문」 정지 화면만 찍는다
 * (Run-EpilogueDoorStill.ps1). 쪽지 셋을 떼고 마지막 쪽지만 붙인 문을 복도에서 찍는다.
 */
UCLASS()
class INDIEGAME_API AIGDayScenesProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGDayScenesProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer, AIGListenerEntity* InListener);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Next(int32 NewPhase);
	void Stand(const FVector& Feet, const FVector& LookAt);
	void Shoot(const TCHAR* Name);
	bool IsShown(const AIGReadableNote* Note) const;
	bool SaidLine(const FText& Line) const;
	bool IsDay() const;

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	TWeakObjectPtr<AIGMissingFloorEvidence> JeongwooTalk;
	TWeakObjectPtr<AIGMissingFloorEvidence> Unit401Door;
	TWeakObjectPtr<AIGReadableNote> Phone;
	TWeakObjectPtr<AIGReadableNote> Notes[3];

	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	bool bShots = false;
	bool bDoorStill = false;
	bool bActed = false;
	FVector NoteClosedAt = FVector::ZeroVector;
};
