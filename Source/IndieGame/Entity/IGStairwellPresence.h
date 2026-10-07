#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGStairwellPresence.generated.h"

class AIGPlayerCharacter;
class UAnimSequence;
class UDecalComponent;
class UIGMissingFloorNarrativeSubsystem;
class UIGSaveGame;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;

/**
 * 뒤따르는 발. 밤2에 한 번, 계단탑에서 그녀보다 한 층 반 뒤에서 걷는 발소리다.
 *
 * 그녀가 계단을 오르내리면 같은 걸음으로 한 띠 뒤에서 철판을 밟는 소리가 난다.
 * 멈추면 한 발이 더 난다. 두 띠 사이가 벽이라 몸은 꺾이는 참에 가서야 보인다.
 * 돌아보면 허리가 꺾인 사람이 고개를 숙이고 서 있다. 보는 동안은 움직이지 않고,
 * 오래 보면 경련하다 고개를 든다. 얼굴이 없다. 눈을 돌리면 없다. 그 자리에는
 * 젖은 작업화 자국 한 켤레가 남고, 다음 낮까지 마르지 않는다.
 *
 * 쫓지도 잡지도 않는다. 위층 사람이 가까이 있거나 무언가를 쫓는 동안에는
 * 시작하지 않고, 시작한 뒤에도 그가 다가오면 물러난다. 긴장은 한 번에 하나다.
 */
UCLASS(NotBlueprintable)
class INDIEGAME_API AIGStairwellPresence : public AActor
{
	GENERATED_BODY()

public:
	AIGStairwellPresence();
	virtual void Tick(float DeltaSeconds) override;

	/** 계단탑 길(4층 출입구부터 1층 출입구까지 발 높이 점)을 받는다. */
	void Configure(const TArray<FVector>& InRoute);
	/**
	 * 4층 출입구에서 1층 출입구까지 내려가는 길. 중간 층에서는 복도 쪽 출입구로
	 * 나가지 않고 층 참을 지나 다음 띠로 꺾는다.
	 */
	static void BuildDescentRoute(TArray<FVector>& OutRoute);
	bool IsEncounterActive() const { return Stage == EStage::Following || Stage == EStage::Watched || Stage == EStage::Withdrawing; }
	/** 검사용. 조건을 건너뛰고 다음 걸음에서 시작한다. */
	void ForceArm() { bForceArmed = true; }
	FVector GetFigureFeet() const;
	bool IsFigureShown() const;
	bool HasLifted() const { return bLifted; }
	bool IsWatched() const { return Stage == EStage::Watched; }
	bool ArePrintsShown() const;
	int32 GetStepsPlayed() const { return StepsPlayed; }
	int32 GetExtraSteps() const { return ExtraSteps; }

	static const FName StartedBeat;
	static const FName HeardBeat;
	static const FName SeenBeat;
	static const FName DayPrintsBeat;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EStage : uint8
	{
		Idle,
		Following,
		Watched,
		Withdrawing,
		Spent
	};

	void Poll();
	bool CanBegin() const;
	bool IsListenerClear(bool bStarting) const;
	void Begin();
	void Withdraw(const TCHAR* Reason);
	void Finish();
	void PlayStep(bool bExtra);
	void PlaceFigure();
	/** 디딤판 윗면. 길은 띠의 기울기를 따라 그은 선이라 단 사이에 뜬다. */
	bool FindTreadTop(const FVector& Near, FVector& OutTop) const;
	void LeavePrints();
	bool IsFigureVisible() const;
	bool IsPointOnScreen(const FVector& Point, float MinDot) const;
	/** 길 위의 거리(4층 출입구에서 cm). 발 높이가 맞는 띠만 본다. INDEX_NONE이면 계단 밖. */
	float ProjectOntoRoute(const FVector& Feet) const;
	FVector RoutePoint(float Distance, FVector* OutForward = nullptr) const;
	void SaveEncounter() const;
	AIGPlayerCharacter* GetPlayer() const;
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;

	UFUNCTION()
	void HandleLoadCompleted(bool bSuccess, FString SlotName, UIGSaveGame* SaveGame);

	UPROPERTY(VisibleAnywhere, Category = "Stairwell")
	TObjectPtr<USkeletalMeshComponent> Figure;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDecalComponent>> Prints;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PrintMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> TwitchAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LiftAnim;

	TArray<FVector> Route;
	TArray<float> RouteDistance;
	FTimerHandle PollTimer;
	EStage Stage = EStage::Idle;
	float SceneSeconds = 0.0f;
	float PlayerRouteDistance = 0.0f;
	float LastRouteDistance = 0.0f;
	/**
	 * 따라오는 쪽. 시작할 때 그녀가 내려가고 있었으면 +1(그는 위에 있다), 오르고 있었으면
	 * -1이다. 그녀가 돌아서 다가와도 그는 앞질러 건너오지 않고 한 띠 떨어진 채 물러선다.
	 */
	float FollowSign = 1.0f;
	float FollowerDistance = 0.0f;
	float TravelSinceStep = 0.0f;
	float StepDelay = -1.0f;
	float QuietSeconds = 0.0f;
	float WatchedSeconds = 0.0f;
	float ClearSeconds = 0.0f;
	float PrintsAge = -1.0f;
	float DayLookSeconds = 0.0f;
	int32 StepsPlayed = 0;
	int32 ExtraSteps = 0;
	int32 CaptureCountAtStart = 0;
	bool bExtraPending = false;
	bool bTwitched = false;
	bool bLifted = false;
	bool bForceArmed = false;
	/** 검사로 시작한 판. 새벽·밤 조건으로 물러나지 않는다. */
	bool bForcedRun = false;
	bool bConfigured = false;
};
