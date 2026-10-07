#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGHidingSpot.generated.h"

class AIGPlayerCharacter;
class APlayerController;
class UBoxComponent;

/** 숨은 자리에서 밖이 어떻게 보이는가. HUD가 가림막 모양을 고른다. */
UENUM()
enum class EIGHidingView : uint8
{
	/** 장롱 문틈. 세로로 좁은 틈만 남는다. */
	DoorGap,
	/** 침대 밑. 위는 침대 바닥, 아래는 방바닥이라 가로로 길게 보인다. */
	UnderBed
};

/**
 * 숨는 자리(§4). E로 들어가고 안에서 E를 다시 누르면 나온다.
 *
 * 들고 나는 0.45초 동안만 시선이 걸리고, 안에서는 정해진 각도 안에서 둘러본다.
 * 몸은 충돌을 끄고 눈 바로 아래의 가구 바닥에 세우며, 눈높이는 카메라만 내려
 * 맞춘다. 그동안 낸 소리는 UIGNoiseSubsystem이 0.55배로 줄여 내보낸다. 숨 참기와
 * 손전등은 그대로 된다. 잡히면 그 자리에서 바로 끌려 나온다(ForceExit).
 */
UCLASS()
class INDIEGAME_API AIGHidingSpot : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGHidingSpot();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 자리를 정한다. 좌표는 이 액터 기준이고 +X가 밖을 내다보는 방향이다.
	 * 상호작용 상자(FocusCenterLocal, FocusExtent)는 가구 앞면보다 2~3 cm 앞에
	 * 얇게 둔다. 가구 메시가 시선 추적을 먼저 막기 때문이다.
	 */
	void Configure(
		EIGHidingView InView,
		const FVector& InEyeLocal,
		const FVector& InExitLocal,
		const FVector& InFocusCenterLocal,
		const FVector& InFocusExtent,
		float InYawLimitDegrees,
		float InPitchLimitDegrees,
		bool bInRequiresCrouch);

	bool IsOccupied() const { return Occupant.IsValid(); }
	/** 다 들어가 앉아 있다. 드나드는 중이면 false다. */
	bool IsPlayerInside() const { return Phase == EPhase::Hidden; }
	/** 가구 안에 있다. 드나드는 0.45초도 포함하고, 앉기를 기다리는 동안은 뺀다. */
	bool IsOccupantConcealed() const
	{
		return Phase == EPhase::Entering || Phase == EPhase::Hidden || Phase == EPhase::Exiting;
	}
	/**
	 * 숨은 눈이 밖을 내다보는 자리. 가구 앞면 바로 바깥의 눈높이다. 가구 메시와
	 * 상호작용 상자가 시선 추적을 막으므로, 숨은 사람이 무엇을 볼 수 있는지는
	 * 이 점에서 잰다.
	 */
	FVector GetPeekLocation() const;
	EIGHidingView GetView() const { return View; }
	/**
	 * 가구 앞 바닥. 숨은 사람이 나와 서는 자리이고, 찾는 쪽이 귀를 대러 오는
	 * 자리다. 높이는 이 자리가 놓인 바닥이다.
	 */
	FVector GetApproachLocation() const
	{
		return GetActorTransform().TransformPosition(FVector(ExitLocal.X, ExitLocal.Y, 0.0f));
	}

	/** 가림막의 짙기(0~1). 드나드는 동안 차오르고 빠진다. */
	float GetMaskAlpha() const;
	/** 안에서 고개를 돌린 정도(-1~1). 문틈이 반대쪽으로 밀려 보이게 한다. */
	float GetPeekYawAlpha() const;
	/** 들어가 있은 시간. 어둠이 쌓이는 것을 재는 쪽이 읽는다. */
	float GetHiddenSeconds() const { return Phase == EPhase::Hidden ? PhaseSeconds : 0.0f; }

	/** 안에서 E를 눌렀다. */
	void RequestExit();
	/** 잡히거나 리셋될 때 그 자리에서 바로 꺼낸다. 조작과 충돌을 모두 돌려놓는다. */
	void ForceExit();

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EPhase : uint8
	{
		Idle,
		/** 침대 밑처럼 앉아야 하는 자리. 엔진이 다음 프레임에 앉힌다. */
		Crouching,
		Entering,
		Hidden,
		Exiting
	};

	void BeginEnter(AIGPlayerCharacter* Player);
	void StartEnterTransition();
	void FinishEnter();
	/** bPlaceAtExit가 false면 몸을 옮기지 않는다(다른 연출이 이미 옮겼다). */
	void FinishExit(bool bPlaceAtExit);
	void SetLookLocked(bool bLocked);
	void SetViewLimited(bool bLimited);
	void ReportSpotNoise(float Loudness);
	void PlaySpotSound(bool bEntering);
	/** 숨은 동안 몸이 설 자리. 눈 바로 아래, 이 자리가 놓인 바닥 위다. */
	FVector GetHiddenBodyLocation(const AIGPlayerCharacter& Player) const;
	/** 그 자리에 선 몸에서 눈 위치까지 카메라를 내리는 양(cm). */
	float GetHiddenCameraLift(const AIGPlayerCharacter& Player, const FVector& BodyLocation) const;
	/** 몸을 옮기고 그 자리를 기억한다. 다음 틱에 다른 연출이 옮겼는지 본다. */
	void PlaceOccupant(AIGPlayerCharacter& Player, const FVector& Location);
	FVector GetExitWorldLocation(const AIGPlayerCharacter& Player) const;

	UPROPERTY(VisibleAnywhere, Category = "Hiding")
	TObjectPtr<USceneComponent> SpotRoot;

	UPROPERTY(VisibleAnywhere, Category = "Hiding")
	TObjectPtr<UBoxComponent> FocusBox;

	TWeakObjectPtr<AIGPlayerCharacter> Occupant;
	EPhase Phase = EPhase::Idle;
	EIGHidingView View = EIGHidingView::DoorGap;
	FVector EyeLocal = FVector::ZeroVector;
	FVector ExitLocal = FVector(80.0f, 0.0f, 0.0f);
	float YawLimitDegrees = 35.0f;
	float PitchLimitDegrees = 15.0f;
	bool bRequiresCrouch = false;
	bool bHurriedEntry = false;
	float PhaseSeconds = 0.0f;
	FVector TransitionFrom = FVector::ZeroVector;
	FVector TransitionTo = FVector::ZeroVector;
	FVector LastPlacedLocation = FVector::ZeroVector;
	float LiftFrom = 0.0f;
	float LiftTo = 0.0f;
	/** 들어가려고 이 자리가 앉혔다. 못 들어가고 끝나면 다시 세운다. */
	bool bCrouchedForEntry = false;
	/** 잠근 컨트롤러. 몸이 먼저 사라져도 이걸로 풀어 준다. */
	TWeakObjectPtr<APlayerController> LockedController;
	TWeakObjectPtr<APlayerController> LimitedController;
	FRotator RotationFrom = FRotator::ZeroRotator;
	FRotator RotationTo = FRotator::ZeroRotator;
	float SavedYawMin = 0.0f;
	float SavedYawMax = 359.999f;
	float SavedPitchMin = -89.9f;
	float SavedPitchMax = 89.9f;
	bool bViewLimited = false;
	bool bLookLocked = false;
};
