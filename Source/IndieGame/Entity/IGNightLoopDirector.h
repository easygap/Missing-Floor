#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGNightLoopDirector.generated.h"

class AIGListenerEntity;
class AIGPlayerCharacter;
class UStaticMeshComponent;

/**
 * 그 시간의 포획 결과를 관리한다. 잡혀도 게임 오버 화면을 띄우지 않고
 * 가까운 두 번의 노크와 함께 암전한 뒤 네시 반의 침대로 돌려보낸다.
 * 복귀할 때 위층 사람의 공격성 티어를 높이고 다시 화면을 연다.
 *
 * 리셋 뒤 남는 상태와 되돌릴 상태는 STORY_BIBLE_MISSING_FLOOR.md §5.4를
 * 따른다. M1은 포획 벽에 세션 동안 남는 석고 손자국을 만들며, 문과 프롭의
 * 세부 롤백은 M2에서 처리한다.
 */
UCLASS()
class INDIEGAME_API AIGNightLoopDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGNightLoopDirector();
	virtual void Tick(float DeltaSeconds) override;
	static FVector GetMercyNoteRestLocation();

	/** 포획된 플레이어가 다시 눈을 뜨는 403호 침대 옆 위치. */
	UFUNCTION(BlueprintCallable, Category = "NightLoop")
	void SetWakeTransform(const FTransform& Transform);
	bool HasWakeTransform() const { return bWakeTransformSet; }
	const FTransform& GetWakeTransform() const { return WakeTransform; }

	UFUNCTION(BlueprintCallable, Category = "NightLoop")
	void RegisterEntity(AIGListenerEntity* Entity);

	/**
	 * 위층 사람이 아닌 다른 괴이가 잡았다(어둑시니, 손님). 침대로 돌아가는 것은
	 * 같지만 그의 공격성 단계는 올리지 않는다. 그는 그 자리에 없었다.
	 */
	void RequestExternalCapture(APawn* Player);

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

	/** 포획 횟수를 추가하지 않고 기존 403호 기상 앵커로 복귀시킨다. */
	bool RestorePlayerAtWakePoint(AIGPlayerCharacter* Character) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/**
	 * 플레이어를 옮기기 전 포옹 암전 시간(초). 화면은 0.55~0.95초에 이미 끊긴다.
	 * 끊긴 뒤의 검은 화면이 1초 넘게 이어지면 연출이 아니라 멈춘 화면으로 읽힌다(§6).
	 */
	UPROPERTY(EditAnywhere, Category = "NightLoop", meta = (ClampMin = "0.0"))
	float FadeOutSeconds = 1.25f;

private:
	void HandlePlayerCaptured(APawn* Player);
	/** 위층 사람의 포획과 다른 괴이의 포획이 함께 쓰는 암전과 리셋. */
	void BeginCaptureReset(AIGPlayerCharacter* Character, bool bExternal);
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

	FTransform WakeTransform = FTransform::Identity;
	bool bWakeTransformSet = false;
	bool bResetInFlight = false;
	bool bMercyNoteRevealed = false;
	bool bMercyNoteSliding = false;
	int32 CaptureCount = 0;
	/** 이번 리셋이 위층 사람이 아닌 괴이의 것이다. 공격성 단계를 올리지 않는다. */
	bool bExternalCaptureInFlight = false;
	double LastCaptureSeconds = -1000.0;
	float MercyNoteSlideElapsedSeconds = 0.0f;
	FTimerHandle ResetTimer;
	FTimerHandle CaptureFadeTimer;
	FTimerHandle WakeRecoveryTimer;
	FTimerHandle MercyNoteRevealTimer;
};
