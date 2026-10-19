#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Interaction/IGInteractionTypes.h"
#include "TimerManager.h"
#include "IGInteractionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FIGInteractionFocusChangedSignature,
	AActor*, PreviousActor,
	AActor*, NewActor);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FIGInteractionStartedSignature,
	AActor*, TargetActor);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FIGInteractionProgressSignature,
	AActor*, TargetActor,
	float, HoldProgress);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FIGInteractionEndedSignature,
	AActor*, TargetActor,
	EIGInteractionEndReason, EndReason);

/**
 * Player-owned interaction scanner and input state machine.
 * Focus stays on a low-frequency timer. Tick is enabled only while a hold is
 * active so completion time is not quantized to the scan interval.
 */
UCLASS(ClassGroup = (Interaction), meta = (BlueprintSpawnableComponent))
class INDIEGAME_API UIGInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIGInteractionComponent();

	/** Call from the input action's Started/Pressed trigger. */
	UFUNCTION(BlueprintCallable, Category = "Interaction|Input")
	void PressInteraction();

	/** Call from the input action's Completed/Released trigger. */
	UFUNCTION(BlueprintCallable, Category = "Interaction|Input")
	void ReleaseInteraction();

	/** Explicit cancellation for menus, possession changes, cinematics, etc. */
	UFUNCTION(BlueprintCallable, Category = "Interaction|Input")
	void CancelInteraction();

	/** Global gate used by cinematics and player state transitions. Disabling cancels safely. */
	UFUNCTION(BlueprintCallable, Category = "Interaction|Input")
	void SetInteractionInputEnabled(bool bEnabled);

	/** Performs an immediate camera-centred trace outside the regular timer cadence. */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void RefreshFocus();

	/** 손이 닿는 시선 거리(cm). 조준 감사가 같은 거리로 잰다. */
	float GetTraceDistance() const { return TraceDistance; }

	/**
	 * 숨겨 둔 채 꺼 둔 상호작용 물체. 다음 밤이나 다음 단계에 쓸 표적을 미리 세워 두고 숨기는데,
	 * 숨겨도 충돌은 남아 시선을 막았다. 넷째 밤 벽 표적이 셋째 밤 내내 빈 벽 칸 앞을 가려 그 칸에
	 * 귀를 대거나 대답하는 조준이 하나도 잡히지 않았다. 초점과 조준 감사는 이런 것을 건너뛴다.
	 */
	static bool IsDormantInteractable(AActor* Actor, AActor* Interactor);
	/** 시선 하나가 건너뛸 수 있는 잠든 표적 수. 한자리에 겹쳐 세운 것도 서넛이면 끝난다. */
	static constexpr int32 MaxDormantSkips = 4;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	AActor* GetFocusedActor() const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	FText GetFocusedPrompt() const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	FGameplayTag GetFocusedInteractionTag() const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	bool IsInteracting() const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	float GetHoldProgress() const;

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
	FIGInteractionFocusChangedSignature OnFocusChanged;

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
	FIGInteractionStartedSignature OnInteractionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
	FIGInteractionProgressSignature OnInteractionProgress;

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
	FIGInteractionEndedSignature OnInteractionEnded;

	/** 끝까지 마친 상호작용 수. HUD가 첫 조사로 조작 안내를 끝내고 안내를 익혔는지 센다. */
	int32 GetCompletedInteractionCount() const { return CompletedInteractionCount; }
	/** 마지막으로 끝까지 마친 상호작용의 대상. 무엇을 익혔는지(엿듣기 등) 가린다. */
	AActor* GetLastCompletedTarget() const { return LastCompletedTarget.Get(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	void HandleUpdateTimer();
	bool TryStartFocusedInteraction();
	void BufferInteractionPress();
	void TryConsumeBufferedPress();
	void ClearBufferedPress();
	bool GetInteractionViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
	/** Returns the hit actor when it is an interactable that currently accepts input. */
	AActor* ResolveInteractable(const FHitResult& HitResult, AActor* Interactor) const;
	void SetFocusedActor(AActor* NewActor, const FHitResult& NewHitResult);
	void UpdateActiveInteraction();
	void CompleteActiveInteraction();
	void FinishActiveInteraction(EIGInteractionEndReason EndReason, bool bCompleted);
	void BeginHoldProgressRewind(float HoldProgress, float HoldDuration);
	void ClearHoldProgressRewind();
	void ResetActiveState();
	float GetActiveHeldDuration() const;
	FIGInteractionContext MakeContext(
		AActor* TargetActor,
		const FHitResult& HitResult,
		const FGameplayTag& Tag,
		float HeldDuration,
		float HoldProgress);

	int32 CompletedInteractionCount = 0;
	TWeakObjectPtr<AActor> LastCompletedTarget;

	UPROPERTY(EditAnywhere, Category = "Interaction|Trace", meta = (ClampMin = "50.0", Units = "cm"))
	float TraceDistance = 220.0f;

	UPROPERTY(EditAnywhere, Category = "Interaction|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, Category = "Interaction|Trace")
	bool bTraceComplex = false;

	/**
	 * Radius of the forgiving sweep used when the centre ray misses. Small
	 * props on a shelf are otherwise almost impossible to target while walking.
	 */
	UPROPERTY(EditAnywhere, Category = "Interaction|Trace", meta = (ClampMin = "0.0", Units = "cm"))
	float FocusSweepRadius = 12.0f;

	/** Clamped at runtime to 10-15 Hz. Default is 12.5 Hz. */
	UPROPERTY(EditAnywhere, Category = "Interaction|Trace", meta = (ClampMin = "0.0667", ClampMax = "0.1", Units = "s"))
	float FocusUpdateInterval = 0.0333f;

	/** Grace window for a tap that lands while focus or the prior hold is settling. */
	UPROPERTY(EditAnywhere, Category = "Interaction|Input", meta = (ClampMin = "0.0", ClampMax = "0.25", Units = "s"))
	float InputBufferSeconds = 0.12f;

	/** Cancelled hold progress retreats at this fraction of its forward rate. */
	UPROPERTY(EditAnywhere, Category = "Interaction|Input", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float HoldRewindSpeedScale = 0.6f;

	/** Has no effect in Shipping or Test builds. */
	UPROPERTY(EditAnywhere, Category = "Interaction|Debug", meta = (DevelopmentOnly))
	bool bDrawDebugTrace = false;

	TWeakObjectPtr<AActor> FocusedActor;
	TWeakObjectPtr<AActor> ActiveActor;
	FHitResult FocusedHitResult;
	FHitResult ActiveHitResult;
	FGameplayTag ActiveInteractionTag;
	FTimerHandle UpdateTimerHandle;
	float ActiveHoldDuration = 0.0f;
	float ActiveStartTime = 0.0f;
	float RewindStartProgress = 0.0f;
	float RewindSourceHoldDuration = 0.0f;
	float RewindStartTime = 0.0f;
	float BufferedPressExpiresAt = 0.0f;
	uint32 InteractionGeneration = 0;
	bool bInteractionInputEnabled = true;
	bool bFocusScanInProgress = false;
	bool bInteractionPressed = false;
	bool bInteractionActive = false;
	bool bInteractionPressBuffered = false;
	/** A hold that continues after the physical key is released. */
	bool bToggleHoldLatched = false;
	bool bFinalizingInteraction = false;
};
