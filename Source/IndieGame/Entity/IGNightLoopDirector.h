#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGNightLoopDirector.generated.h"

class AIGDroppedFlashlight;
class AIGListenerEntity;
class AIGPlayerCharacter;
class UStaticMeshComponent;
enum class EIGCaptureKind : uint8;

/** 깨어나기 직전, 화면이 아직 덮여 있을 때 한 번. 잡은 괴이가 남길 흔적을 여기서 남긴다. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FIGCaptureSettledSignature, EIGCaptureKind /*Kind*/, const FVector& /*CaptureSpot*/);

/**
 * 붙잡힌 뒤의 흐름을 맡는다(STORY_BIBLE_MISSING_FLOOR.md §5.4). 게임 오버 화면도
 * 침대 복귀도 없다. 잡은 괴이마다 끊기는 모습과 깨어나는 자리, 남는 값이 다르고,
 * 그 밤에 한 일은 그대로 남는다. 잃는 것은 그 밤의 시간과 눈에 보이는 무언가다.
 *
 * - 위층 사람: 지나온 길로 3m 넘게 끌려간 자리에서 깬다. 손전등은 잡힌 자리에
 *   떨어져 있고, 그는 한 단계 더 급해진다.
 * - 어둑시니: 마지막으로 불빛 아래 섰던 자리에서 깬다. 건전지가 준다.
 * - 손님: 그 문 안쪽 현관 바닥에서 깬다(손님이 자리를 정해 준다).
 * - 관리인: 403호 현관 안에 선 채로 정신이 든다(관리인이 자리를 정해 준다).
 *
 * 잡힌 자리 벽에는 석고 손자국이 남는다(위층 사람만).
 */
UCLASS()
class INDIEGAME_API AIGNightLoopDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGNightLoopDirector();
	virtual void Tick(float DeltaSeconds) override;
	static FVector GetMercyNoteRestLocation();

	/**
	 * 밤마다 눈을 뜨는 403호 침대. 밤의 시작과 결말 C의 재도전이 여기서 연다.
	 * 포획은 여기로 돌려보내지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "NightLoop")
	void SetWakeTransform(const FTransform& Transform);
	bool HasWakeTransform() const { return bWakeTransformSet; }
	const FTransform& GetWakeTransform() const { return WakeTransform; }

	UFUNCTION(BlueprintCallable, Category = "NightLoop")
	void RegisterEntity(AIGListenerEntity* Entity);

	/**
	 * 위층 사람이 아닌 괴이가 잡았다. 그의 공격성 단계는 올리지 않는다. 그는 그
	 * 자리에 없었다. WakeAt을 주면 거기서 깬다(손님은 그 문 안 현관, 관리인은 403호
	 * 현관). 비우면 지나온 길에서 고른다.
	 */
	void RequestExternalCapture(APawn* Player, EIGCaptureKind Kind, const FTransform* WakeAt = nullptr);

	/** 403호 현관 안, 문을 보고 선 자리. 손님과 관리인이 그녀를 여기 둔다. */
	static FTransform GetHomeEntryWake();

	FIGCaptureSettledSignature OnCaptureSettled;

	/** 마지막으로 잡힌 자리와 깨어난 자리. 검사가 읽는다. */
	FVector GetLastCaptureSpot() const { return LastCaptureSpot; }
	FVector GetLastWakeLocation() const { return CaptureWake.GetLocation(); }
	EIGCaptureKind GetLastCaptureKind() const { return CaptureKindInFlight; }
	/** 마지막 포획으로 밀린 시계(초). */
	float GetLastLostSeconds() const { return LastLostSeconds; }
	/** 위층 사람에게 잡혀 떨어뜨린 손전등. 주웠거나 새벽이 왔으면 비어 있다. */
	AIGDroppedFlashlight* GetDroppedFlashlight() const { return DroppedFlashlight.Get(); }

	/** 마지막으로 잡힌 게임 시각. 다른 괴이들이 숨 돌릴 틈(§7)을 잰다. 없으면 음수. */
	double GetLastCaptureSeconds() const { return LastCaptureSeconds; }

	UFUNCTION(BlueprintPure, Category = "NightLoop")
	int32 GetCaptureCount() const { return CaptureCount; }

	UFUNCTION(BlueprintPure, Category = "NightLoop")
	int32 GetCaptureHandprintCount() const { return CaptureHandprints.Num(); }

	/** 암전·이동·짧은 기상 잔향 중에는 true다. */
	UFUNCTION(BlueprintPure, Category = "NightLoop")
	bool IsCaptureResetInFlight() const { return bResetInFlight; }

	/** 5회 포획 뒤 401호 문 아래에 남은 실물 메모 상태. */
	UFUNCTION(BlueprintPure, Category = "NightLoop")
	bool IsMercyNoteVisible() const;

	UFUNCTION(BlueprintPure, Category = "NightLoop")
	bool IsMercyNoteSliding() const { return bMercyNoteSliding; }

	UFUNCTION(BlueprintPure, Category = "NightLoop")
	FVector GetMercyNoteLocation() const;

	/** 자동 검증 한정: 첫 포획을 다섯 번째 포획으로 시작한다. */
	void PrimeMercyNoteCaptureProbe();

	/** 미디어 캡처 한정: 저장·포획 횟수 없이 슬라이드를 재생한다. */
	void PlayMercyNoteCapturePreview();

	/** 포획 횟수를 더하지 않고 403호 침대로 옮긴다. 결말 C의 재도전이 쓴다. */
	bool RestorePlayerAtWakePoint(AIGPlayerCharacter* Character) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

private:
	void HandlePlayerCaptured(APawn* Player);
	/** 모든 포획이 함께 쓰는 끊김과 깨어남. */
	void BeginCaptureReset(AIGPlayerCharacter* Character, EIGCaptureKind Kind, const FTransform* WakeAt);
	/** 잡힌 자리에서 깨어날 자리를 고른다. 화면이 끊기기 전에 정해 둔다. */
	FTransform ChooseCaptureWake(const AIGPlayerCharacter& Character, EIGCaptureKind Kind) const;
	void DropFlashlightAt(AIGPlayerCharacter& Character);
	void ReleaseComeToLock();
	void FinishReset();
	void FinishWakeRecovery();
	/** 포획 암전을 걷고 조작을 돌려준다. 정상 복귀가 끊긴 자리에서만 부른다. */
	void AbortCaptureBlackout(const TCHAR* Reason);
	bool SpawnCaptureHandprint(AIGPlayerCharacter* Character);
	bool InitializeMercyNote();
	void QueueMercyNoteReveal();
	void BeginMercyNoteSlide();
	void SetMercyNoteAtRest();
	float GetWakeFadeInSeconds() const;
	float GetWakeEchoSeconds() const;
	float GetWakeRecoverySeconds() const;
	class UIGMissingFloorNarrativeSubsystem* GetNarrative() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGListenerEntity> ListenerEntity;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPlayerCharacter> CapturedPlayer;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CaptureHandprints;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> MercyNote;

	TWeakObjectPtr<AIGDroppedFlashlight> DroppedFlashlight;

	FTransform WakeTransform = FTransform::Identity;
	bool bWakeTransformSet = false;
	bool bResetInFlight = false;
	bool bMercyNoteRevealed = false;
	bool bMercyNoteSliding = false;
	int32 CaptureCount = 0;
	/**
	 * 위층 사람에게 잡힌 횟수. 다섯 번째 쪽지(「소리를 줄여라. 걔는 눈이 없어.」)는
	 * 그에 대한 쪽지라 이것만 센다. 서사 기록의 포획 수도 같은 것을 센다.
	 */
	int32 ListenerCaptureCount = 0;
	/** 지금(또는 마지막) 포획을 한 괴이. 0이 위층 사람이다. */
	EIGCaptureKind CaptureKindInFlight{};
	/** 이번 포획의 깨어날 자리. 잡히는 순간 정한다. */
	FTransform CaptureWake = FTransform::Identity;
	FVector LastCaptureSpot = FVector::ZeroVector;
	float LastLostSeconds = 0.0f;
	/** 바닥에서 몸을 일으키는 동안 걷기만 막아 둔 이름이 걸려 있다. */
	bool bComeToLockHeld = false;
	double LastCaptureSeconds = -1000.0;
	float MercyNoteSlideElapsedSeconds = 0.0f;
	FTimerHandle ResetTimer;
	FTimerHandle CaptureFadeTimer;
	FTimerHandle WakeRecoveryTimer;
	FTimerHandle ComeToTimer;
	FTimerHandle WakeThoughtTimer;
	FTimerHandle MercyNoteRevealTimer;
};
