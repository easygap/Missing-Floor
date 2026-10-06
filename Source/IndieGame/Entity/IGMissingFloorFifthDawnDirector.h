#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "IGMissingFloorFifthDawnDirector.generated.h"

class AIGPlayerCharacter;
class UAudioComponent;

DECLARE_MULTICAST_DELEGATE(FIGFifthDawnCompletedSignature);

/**
 * 「다섯 번째 새벽」 — a fixed 160-second sensory memory between nights 3/4.
 *
 * The camera is black and the ordinary HUD is suppressed, but this is not a
 * loading screen: Q/B still adds a player-owned knock and E/RT still leans
 * into the outside layer. Historical cues remain fixed, so input can never
 * rewrite Hwang's 7/29 reply or manufacture one on the final dawn.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGMissingFloorFifthDawnDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGMissingFloorFifthDawnDirector();
	virtual void Tick(float DeltaSeconds) override;

	/** Starts the canonical timeline and takes movement, not look/input. */
	bool StartInterlude(AIGPlayerCharacter* InPlayer);

	/** Player-owned experiential layer; neither method advances the history. */
	bool RegisterPlayerKnock();
	bool SetPlayerListening(bool bListening);
	/** 재관람 전용 우회 입력. 초회차이거나 막간 밖이면 false를 반환한다. */
	bool BeginReplaySkipInput();
	bool EndReplaySkipInput();

	bool IsActive() const { return bActive; }
	bool IsReplaySkipAvailable() const { return bReplaySkipAvailable; }

	/** Release/probe receipt for all authored day boundaries and the final cut. */
	bool ValidateTimeline() const;
	/** Runs start/input/finish plumbing without waiting 160 seconds in CI. */
	bool CompleteImmediatelyForProbe();

	FIGFifthDawnCompletedSignature OnCompleted;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 슬롯 암전과 이동 잠금을 걷는다. 정상 복귀가 끊긴 자리에서만 부른다. */
	void AbortSlotBlackout(const TCHAR* Reason);

	void FireCue(int32 CueIndex);
	void ScheduleNextCue();
	void HandleNextCue();
	void FinishInterlude(bool bPersistExperience = true);
	/**
	 * 새벽이 바뀔 때 예불을 한 번 끊었다가 더 멀고 먹먹하게 되돌린다. 날짜
	 * 자막 없이도 다섯 번의 새벽이 귀로 갈린다.
	 */
	void DipPrayerForDawn(int32 DawnIndex, float ReturnDelaySeconds);
	void ReturnPrayer();
	/** 역사층의 소리 하나를 몇 초 뒤에 낸다. 막간이 끝나면 같이 걷힌다. */
	void ScheduleInterludeSound(float DelaySeconds, const FTimerDelegate& Sound);
	/** 타이머와 베드를 걷는다. 끝과 EndPlay가 같은 길로 정리한다. */
	void ReleaseInterludeAudio();
	void SetSensoryHud(bool bEnabled) const;
	void UpdateSensoryHudSkip() const;
	void PushDirectionCaption(const FText& Caption, float Seconds) const;
	float GetReplaySkipDurationSeconds() const;
	bool UsesToggleSkipInput() const;
	bool HasExperiencedInterludeProfile() const;
	void PersistInterludeExperience() const;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> WaterBed;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> PrayerBed;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BreathBed;

	TWeakObjectPtr<AIGPlayerCharacter> Player;
	FTimerHandle CueTimerHandle;
	FTimerHandle PrayerReturnTimer;
	/** 처음 보는 사람에게도 건너뛰기를 여는 타이머(§6). */
	FTimerHandle FirstViewSkipTimer;
	TArray<FTimerHandle> InterludeSoundTimers;
	/** 눈을 감은 자리와 그때 보던 방향. 역사층의 소리를 이 축에 놓는다. */
	FVector InterludeOrigin = FVector::ZeroVector;
	FVector InterludeForward = FVector::ForwardVector;
	FVector InterludeRight = FVector::RightVector;
	float WaterBaseVolume = 0.28f;
	float PrayerBaseVolume = 0.12f;
	int32 PrayerDawnIndex = 0;
	double StartWorldSeconds = 0.0;
	float ElapsedSeconds = 0.0f;
	uint32 FiredCueMask = 0;
	int32 PlayerKnockCount = 0;
	int32 NextCueIndex = 1;
	float ReplaySkipProgress = 0.0f;
	bool bActive = false;
	bool bPlayerListening = false;
	bool bReplaySkipAvailable = false;
	bool bReplaySkipInputActive = false;
	bool bReplaySkipRewinding = false;
	bool bReplayAvailabilityForcedForSession = false;
};
