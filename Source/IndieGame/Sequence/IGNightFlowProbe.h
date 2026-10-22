#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGNightFlowProbe.generated.h"

class AIGListenerEntity;
class AIGMissingFloorEvidence;
class AIGNightPhaseDirector;
class AIGPlayerCharacter;
class AIGReadableNote;
class UIGMissingFloorNarrativeSubsystem;

/**
 * 밤의 일을 마친 것과 새벽을 나눈 흐름을 실제 감독으로 겪는 검사(-IGNightFlowProbe).
 * 그레이박스 검사 표시(-IGListenerGreyboxProbe) 없이 돌아서, 게임과 같은 길(목표를 적어도 밤이
 * 이어짐, 침대의 「아침까지 쉬기」, 문서를 읽는 동안 서는 시계, 05:30이 읽기를 기다림, 못 채운
 * 밤의 되풀이, 불러온 밤의 침대)을 본다. 장면 구성은 순간 이동과 상태 주입으로 하므로 사람의
 * 플레이 검수를 대신하지 않는다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGNightFlowProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGNightFlowProbe();
	void Configure(AIGPlayerCharacter* InPlayer);
	virtual void Tick(float DeltaSeconds) override;

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Next(int32 NewPhase);
	void Finish();
	bool Use(AActor* Target);
	void Stand(const FVector& Feet, const FVector& LookAt);
	AIGMissingFloorEvidence* FindEvidence(const TCHAR* Name) const;
	AIGReadableNote* FindNote() const;
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;
	bool IsDay() const;
	/** 관리실 책상의 밑장과 문자 사본: -1 감독 없음, 0 손에 들 수 없음, 1 둘 다 손에 든다. */
	int32 GetNightTwoPapersState() const;

	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGReadableNote> OpenedNote;
	int32 Phase = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	float MarkedElapsed = 0.0f;
	int32 Failures = 0;
	/** -IGNightFlowShots: 오프스크린 렌더가 있는 실행에서 몇 장면을 찍는다(Saved/NightFlowReview). */
	bool bShots = false;
	int32 ShotIndex = 0;
	bool bKnockShot = false;
	void Shoot(const TCHAR* Name);
};
