#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGNightThreatDirector.generated.h"

class AIGListenerEntity;
class AIGMissingFloorEvidence;
class AIGNightLoopDirector;
class AIGNightPhaseDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGShadowFigure;
class AIGSwingDoor;
class UAudioComponent;
class UIGMissingFloorNarrativeSubsystem;
struct FIGNoiseEvent;

/** 문 밖의 손님이 어디까지 왔는가. */
UENUM()
enum class EIGGuestStage : uint8
{
	Idle,
	/** 두드리고 말을 건다. 둘-쉬고-하나로 대답하면 물러간다. */
	Knocking,
	/** 밖에서 도어락 번호를 누른다. 걸쇠를 걸 마지막 틈이다. */
	Keypad,
	/** 문이 걸쇠에 걸렸다. 한 번 더 말하고 발소리 없이 간다. */
	CaughtOnLatch,
	/** 문이 열렸다. 몸이 들어와 찾는다. */
	Inside,
	/** 이번 밤에는 끝났다. */
	Spent
};

/**
 * 위층 사람 말고 밤에 오는 것 둘(EXPANSION_PLAN §3).
 *
 * 어둑시니 — 손전등을 끈 채 어둠에 오래 있으면 등 뒤나 옆의 어둠이 뭉쳐 선다.
 * 쳐다볼수록 커지고 다가오며, 손전등을 비추면 두 배로 큰다. 3초 동안 눈을
 * 돌리거나 불이 켜진 곳에 서면 사라진다. 다 자란 채 2.2 m 안에 들면 삼킨다.
 *
 * 손님 — 밤2부터 한 밤에 한 번, 403호에 오래 있으면 현관을 두드리고 아는
 * 사람의 목소리로 문을 열어 달라고 한다. 대답하지 않으면 도어락을 누른다.
 * 걸쇠를 걸었으면 문이 한 뼘에서 걸리고, 아니면 들어와 찾는다. 오빠의 노크
 * (둘-쉬고-하나)로 대답하면 두 번만 두드리고 간다.
 *
 * 긴장 예산(§7): 위층 사람이 쫓는 동안, 잡혔다 깬 뒤 40초 동안은 둘 다 오지
 * 않는다. 한 번에 하나만 나온다. 틱 없이 0.1초 타이머로 돈다.
 */
UCLASS()
class INDIEGAME_API AIGNightThreatDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGNightThreatDirector();

	void Configure(
		AIGPrologueWorldScene* InScene,
		AIGPlayerCharacter* InPlayer,
		AIGListenerEntity* InListener,
		AIGNightLoopDirector* InNightLoop,
		AIGNightPhaseDirector* InNightPhase);

	/** 플레이어가 문을 두드렸다. 현관문이면 손님에게 대답한 것인지 본다. */
	void RegisterPlayerDoorKnock(const AActor* KnockedActor);

	bool IsEoduksiniManifested() const;
	EIGGuestStage GetGuestStage() const { return GuestStage; }
	float GetDarknessSeconds() const { return DarknessSeconds; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Update();
	bool IsQuietWindow() const;
	FVector GetPlayerEye() const;
	bool IsPlayerLookingAt(const FVector& Target, float HalfAngleDegrees, const AActor* Ignored) const;
	bool IsTorchOn(const FVector& Target) const;
	bool HasLineOfSight(const FVector& From, const FVector& To, const AActor* Ignored) const;
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;
	/** 한 판에 한 번 하는 속말. 서사 기록에 남겨 이어하기에도 되풀이하지 않는다. */
	void ThinkOnce(const TCHAR* BeatId, const FText& Thought, float DelaySeconds = 0.0f);

	// 어둑시니
	void UpdateEoduksini(float DeltaSeconds);
	bool TryManifestEoduksini();
	bool FindManifestSpot(FVector& OutLocation) const;
	bool IsValidManifestSpot(const FVector& Candidate, const FVector& Eye, FVector& OutFloor) const;
	void DismissEoduksini(bool bSilently);
	float GetDarknessThresholdSeconds() const;

	// 손님
	void UpdateGuest(float DeltaSeconds);
	bool CanStartGuest() const;
	void StartGuest();
	void GuestKnock(int32 Count, float Volume);
	void GuestSpeak(int32 LineIndex);
	void OpenDoorForGuest();
	void BeginGuestCapture();
	void EndGuest(bool bCloseDoor);
	/** 문구멍을 손님에게 빌려 쓴다(밤2 비트가 끝난 뒤에만). */
	void SetPeepholeOffered(bool bOffered);
	void HandlePeepholeExamined(AIGMissingFloorEvidence* Evidence);
	bool IsPlayerInHome() const;
	FVector GetDoorOutside() const;
	void HandleNoise(const FIGNoiseEvent& Event);

	UPROPERTY(Transient)
	TObjectPtr<AIGShadowFigure> Eoduksini;

	UPROPERTY(Transient)
	TObjectPtr<AIGShadowFigure> Guest;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> EoduksiniBreath;

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> PlayerPawn;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGNightLoopDirector> NightLoop;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	TWeakObjectPtr<AIGSwingDoor> HomeDoor;
	TWeakObjectPtr<AIGMissingFloorEvidence> Peephole;
	FDelegateHandle PeepholeHandle;

	FTimerHandle UpdateTimer;
	FDelegateHandle NoiseHandle;
	double LastUpdateSeconds = -1.0;

	// 어둑시니
	float DarknessSeconds = 0.0f;
	float EoduksiniGrowth = 0.0f;
	float EoduksiniUnseenSeconds = 0.0f;
	double EoduksiniCooldownUntil = 0.0;
	double EoduksiniCaptureSeconds = -1.0;
	bool bEoduksiniWarned = false;

	// 손님
	EIGGuestStage GuestStage = EIGGuestStage::Idle;
	int32 GuestNight = 0;
	float GuestElapsed = 0.0f;
	float HomeDwellSeconds = 0.0f;
	int32 GuestStep = 0;
	TArray<double> PlayerDoorKnockTimes;
	bool bGuestAnswered = false;
	bool bPlayerHiddenWhenOpened = false;
	bool bGuestHeardPlayer = false;
	bool bGuestCapturing = false;
	double GuestCaptureSeconds = -1.0;
	float GuestRustleSeconds = 0.0f;
	FVector LastGuestLocation = FVector::ZeroVector;
};
