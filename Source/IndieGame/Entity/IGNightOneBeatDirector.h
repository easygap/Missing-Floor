#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGNightOneBeatDirector.generated.h"

class AIGListenerEntity;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGZoneTrigger;
class UIGMissingFloorNarrativeSubsystem;
class UIGNoiseSubsystem;

/**
 * 밤1의 두 스크립트 비트 (STORY_BIBLE_MISSING_FLOOR.md §8 밤1).
 *
 * 1-4 첫 목격 — 계단 입구 존이 그를 3.5층 참 북서쪽 구석에 올린다. 북쪽 벽에
 * 귀를 대고 등을 보인다. 4층에서 내려오는 사람은 두 띠 사이 벽 때문에 참에
 * 내려서야 그를 보고, 계속 내려가려면 팔 길이 안을 지나야 한다. 그동안 그는
 * 쫓기 전에는 닿기만으로 잡지 않는다 — 소리 없이 지나가면 모른다는 것이 이
 * 비트가 가르치는 규칙이다.
 *
 * 1-5 강제 조우 — the fire-cabinet zone knocks the extinguisher off its
 * bracket. The clatter is a 0.6 sound the whole corridor hears, INVESTIGATE
 * plays out on the real AI with no bespoke chase code, and the distribution
 * board's hum pocket is the escape the player is meant to discover. Either
 * outcome — slipping away or being caught and reset — teaches the rule.
 *
 * Both beats are once-per-run narrative facts (MarkBeatPlayed), so a capture
 * reset does not replay them as jump scares.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGNightOneBeatDirector : public AActor
{
	GENERATED_BODY()

public:
	static FVector GetSightingZoneCenter();
	static FVector GetSightingStagePoint();
	static FVector GetSightingShufflePoint();
	/** 그를 지나 서쪽 띠로 두 단 내려선 자리. 프로브가 「지나갔다」를 만들 때 쓴다. */
	static FVector GetSightingPassPoint();
	AIGNightOneBeatDirector();

	/** Arms both beat zones against an already-built corridor. */
	bool Configure(
		AIGPrologueWorldScene* InScene,
		AIGListenerEntity* InEntity,
		AIGPlayerCharacter* InPlayer,
		const TArray<FVector>& InCorridorPatrolPoints);

	/** Probe queries. */
	bool IsSightingStaged() const { return bSightingStaged; }
	bool HasSightingCompleted() const { return bSightingCompleted; }
	bool HasExtinguisherBeatFired() const { return bExtinguisherBeatFired; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleSightingZone(AIGZoneTrigger* Zone);

	UFUNCTION()
	void HandleExtinguisherZone(AIGZoneTrigger* Zone);

	/**
	 * 1-6 402호의 노크. 소화기 비트 뒤에 402호 문 앞을 지나면 문 안쪽에서
	 * 두 번 두드린다. 402호는 빈집이다 — 그가 어디에나 있을 수 있다는 것을
	 * 형체 없이 알려 주는 한 번뿐인 소리. 추격도 조사도 부르지 않는다.
	 */
	UFUNCTION()
	void HandleUnit402KnockZone(AIGZoneTrigger* Zone);
	void PlayUnit402SecondKnock();

	/** 그녀가 반 층 참을 지나 내려갔는지 0.2초마다 본다. 존이 울린 뒤부터 끝날 때까지다. */
	void PollSightingDescent();
	bool HasPlayerDescendedPastLanding() const;

	/**
	 * 존은 한 번만 울리므로, 그를 화면 밖에서 옮길 수 있을 때까지 여기서 다시
	 * 본다. 보이는 곳에서 사라지면 순간이동이다(§4.6).
	 */
	void TryStageSighting();
	bool CanStageSightingUnseen() const;
	void StageSighting();
	/** 45초 폴백. 복도 첫 칸이 플레이어 곁이거나 시야 안이면 미룬다. */
	void TryFallbackRestore();
	bool CanRestoreSightingUnseen() const;
	void RestoreSightingEntity();
	/** 자리가 플레이어 시야 원뿔 안에 있고 가려지지 않았는가. Subject가 있으면 그 몸이 그려졌는지로 본다. */
	bool IsInPlayerView(const FVector& Location, const AActor* Subject) const;
	/** 프로브와 캡처는 상태를 동기적으로 밟는다. 조건을 기다리지 않는다. */
	static bool IsScriptedRun();
	void PlayExtinguisherImpact();
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPrologueWorldScene> Scene;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGListenerEntity> Entity;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	/** 소화기 뒤 복도 등이 한 번 죽는다. 한 밤에 한 번. */
	FTimerHandle FixtureDeathTimer;
	bool bFixtureDeathFired = false;
	void KillFixtureBehindPlayer();

	UPROPERTY(Transient)
	TObjectPtr<AIGZoneTrigger> SightingZone;

	UPROPERTY(Transient)
	TObjectPtr<AIGZoneTrigger> ExtinguisherZone;

	UPROPERTY(Transient)
	TObjectPtr<AIGZoneTrigger> Unit402KnockZone;
	FTimerHandle Unit402KnockTimer;

	/** The route the entity returns to once its cameo on the landing ends. */
	TArray<FVector> CorridorPatrolPoints;

	int32 BreakerPanelHumHandle = 0;

	/** 배전반이 내는 소리. 마스킹과 같은 반경까지만 들린다. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BreakerPanelHumLoop;
	bool bSightingStaged = false;
	bool bSightingCompleted = false;
	bool bExtinguisherBeatFired = false;
	FTimerHandle SightingFallbackTimer;
	FTimerHandle SightingRetryTimer;
	FTimerHandle SightingStepTimer;
	FTimerHandle SightingDescentTimer;
	/** 계단 입구 위의 등. 그가 계단참에 있는 동안 죽어 있고, 끝나면 돌아온다. */
	int32 SightingThroatFixture = INDEX_NONE;
	FTimerHandle ImpactTimer;
};
