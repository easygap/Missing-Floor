#pragma once

#include "CoreMinimal.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "GameFramework/Character.h"
#include "Entity/IGNoiseSubsystem.h"
#include "IGPlayerCharacter.generated.h"

namespace Audio
{
	class FAudioCaptureSynth;
}

class UCameraComponent;
class UIGAccessibilitySubsystem;
class UIGCameraSensorComponent;
class UIGFlashlightComponent;
class UIGInteractionComponent;
class UIGStressComponent;
class UInputAction;
struct FInputActionValue;

/** First-person player pawn with asset-driven Enhanced Input bindings. */
UCLASS()
class INDIEGAME_API AIGPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/**
	 * §5.1 노크. 주먹이 석고보드를 때리는 값이다.
	 *
	 * 밤3의 벽 노크도 같은 값을 쓴다 — 같은 동작이라 상수가 둘일 이유가
	 * 없고, 둘이면 한쪽만 조정했을 때 같은 주먹이 밤마다 다르게 들린다.
	 */
	static constexpr float KnockLoudness = 0.30f;

	AIGPlayerCharacter();
	virtual ~AIGPlayerCharacter() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Player|Components")
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

	UFUNCTION(BlueprintPure, Category = "Player|Components")
	UIGInteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

	UFUNCTION(BlueprintPure, Category = "Player|Components")
	UIGFlashlightComponent* GetFlashlight() const { return Flashlight; }

	UFUNCTION(BlueprintPure, Category = "Player|Components")
	UIGStressComponent* GetStress() const { return StressComponent; }

	/**
	 * 둘-쉬고-하나를 들을 수 있는 존재에게 이 탭을 건넨다. 인식만 하며, 소리와
	 * 소음 보고와 피드백은 부른 쪽이 소유한다 — 한 번의 탭이 두 번 들리지
	 * 않도록. 아무도 받지 않으면 false.
	 */
	bool OfferAnswerKnock(const FVector& Where);

	/**
	 * Enables the procedural head-bob/breath sway and footstep cadence.
	 * Kept off while a director owns the camera (lying in bed, getting up).
	 */
	UFUNCTION(BlueprintCallable, Category = "Player|Camera")
	void SetCameraMotionEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Player|Camera")
	/**
	 * 잡혔을 때의 시점. 고개가 괴물 얼굴로 꺾이고, 뒤로 넘어져 바닥에서 올려다보다
	 * CutSeconds에 화면이 검게 끊긴다. CutSeconds를 비우면 0.95초다.
	 */
	void PlayCaptureFeedback(float DurationSeconds = 1.2f, float CutSeconds = -1.0f);
	/** 넘어질 때 시점이 떨어지는 깊이와 뒤로 밀리는 거리(cm). 괴물이 얼굴을 들이밀 자리가 여기서 나온다. */
	static constexpr float CaptureFallDropCentimeters = 112.0f;
	static constexpr float CaptureFallBackCentimeters = 26.0f;
	/** 잡힌 동안 좁아지는 시야각 배율(1이 평소). */
	float GetCaptureFovScale() const { return CaptureFovScale; }
	/** 0~1. 부딪힌 순간 치솟았다 가라앉는 화면 충격. 카메라 모디파이어가 후처리로 쓴다. */
	float GetCaptureImpactAlpha() const { return CaptureImpactAlpha; }
	/** 0~1. 끊기기 직전까지 조여 오는 시야. */
	float GetCaptureTunnelAlpha() const { return CaptureTunnelAlpha; }
	/**
	 * 0~1. 어둠 속에 선 것이 커지는 동안 화면 가장자리가 먹히는 정도. 괴이 감독이
	 * 목표를 주고, 틱에서 천천히 따라간다. 사라지면 0을 준다.
	 */
	void SetThreatVignetteTarget(const float Alpha) { ThreatVignetteTarget = FMath::Clamp(Alpha, 0.0f, 1.0f); }
	float GetThreatVignetteAlpha() const { return ThreatVignetteAlpha; }
	/** 이번 포획에서 화면이 끊기는 시각(초). 괴물은 이 순간에 얼굴이 닿도록 달려든다. 음수면 아직 모른다. */
	float GetCaptureCutSeconds() const { return CaptureCutSeconds; }
	void SetCaptureThreat(class AIGListenerEntity* Threat);
	bool HasPhysicalCaptureView() const;

	/** Parents an item to the camera at the given relative pose (held item). */
	UFUNCTION(BlueprintCallable, Category = "Player|Carry")
	bool CarryActor(AActor* Item, const FVector& RelativeOffset, const FRotator& RelativeRotation);

	/** Clears the hand only when it still owns ExpectedItem. */
	UFUNCTION(BlueprintCallable, Category = "Player|Carry")
	bool ReleaseCarriedActor(AActor* ExpectedItem);

	UFUNCTION(BlueprintPure, Category = "Player|Carry")
	AActor* GetCarriedActor() const { return CarriedActor.Get(); }

	/** Applies the persisted/command-line microphone mode immediately. */
	void RefreshMicrophoneCaptureMode();

	/**
	 * §19.8 노크 진동 대체. 존재가 낸 소리를 손으로 옮긴다.
	 * §18.5의 무진동 규칙은 기본값에서 그대로다 — 켠 사람만 받는다.
	 */
	void PlayKnockSubstituteHaptic(float Loudness) const;

	/** §18.6 낙하물·충돌. 연출된 충격에만 붙는다. */
	void PlayImpactHaptic() const;

	/** §18.6 심박. 스트레스가 높을 때 럽과 덥에 하나씩. */
	void PlayHeartbeatHaptic() const;

	/** §18.6 CHASE 진입. 지속 진동을 페이드인으로 올리고 내린다. */
	void SetChaseHaptic(bool bActive);

	/** 접근성 설정의 시야각을 카메라에 건다. 설정이 바뀔 때마다 부른다. */
	void RefreshFieldOfView();

	/** 접근성 설정의 화면 질감을 후처리에 건다. 설정이 바뀔 때마다 부른다. */
	void RefreshCameraTexture();
	/**
	 * 이번 프레임의 손맛 회전(노크 킥·포획 킥·공포 떨림). 카메라 컴포넌트에
	 * 상대 회전을 주면 bUsePawnControlRotation이 GetCameraView에서 폰 제어
	 * 회전으로 덮어써 화면에 안 나온다. UIGCameraFeelModifier가 카메라 매니저
	 * 단계에서 이 값을 시점에 얹는다.
	 */
	FRotator GetCameraFeelRotation() const { return CameraFeelRotation; }
	/** 놀람의 카메라 킥. 아래로 꺾이고 살짝 기운다. 노크 킥보다 크고 느리게 돌아온다. */
	void PlayScareKick(float Degrees);

	UFUNCTION(BlueprintPure, Category = "Player|Audio")
	bool IsMicrophoneCaptureRunning() const { return bMicrophoneCaptureRunning; }

	/** HUD의 상황 안내가 알려 준 동작을 실제로 해 봤는지 본다. */
	bool IsHoldingBreath() const { return bHoldingBreath; }
	bool IsSprinting() const { return bSprinting; }

	/** 숨어 있는 자리. 드나드는 중에도 그 자리를 돌려준다. */
	class AIGHidingSpot* GetHidingSpot() const;
	bool IsInHidingSpot() const;
	/** 숨는 자리가 부른다. 들어갈 때 자리를, 다 나왔을 때 nullptr를 준다. */
	void SetHidingSpot(class AIGHidingSpot* Spot);
	/** 숨어 있으면 그 자리에서 바로 꺼낸다. 잡힘과 장면 전환이 부른다. */
	void LeaveHidingSpotImmediately();
	/** 몸의 원점에서 눈(카메라)까지. 숨는 자리가 몸을 어디에 둘지 정할 때 쓴다. */
	FVector GetEyeOffsetFromActor() const;

	/** 지금 서 있는 자리의 어둠(0~1). 켜진 손전등은 어둠을 0으로 만든다. */
	float GetDarkness() const { return CachedDarkness; }
	/** 손전등을 뺀 어둠. 방이나 복도에 불이 켜져 있으면 낮다. */
	float GetRoomDarkness() const { return CachedRoomDarkness; }

	UFUNCTION(BlueprintPure, Category = "Player|Audio")
	EIGFootstepSurface GetLastFootstepSurface() const { return LastFootstepSurface; }

	UFUNCTION(BlueprintPure, Category = "Player|Audio")
	float GetLastFootstepNoiseLoudness() const { return LastFootstepNoiseLoudness; }

	/**
	 * Harness hook for §24's 즉시 차단 19. F9 must not restore inside a sealed
	 * hour; the probe asks for the restore and then reads which refusal the
	 * game gave, because silence is also what a broken binding looks like.
	 */
	void LoadLatestAutosaveForTesting() { LoadLatestAutosave(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

private:
	friend class AIGGameplayRealismProbe;
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void MoveForward(float Value);
	void MoveRight(float Value);
	/** §18.3. 지금 시점을 미는 것이 스틱인가. */
	bool IsUsingGamepadLook() const;
	bool IsLookInverted() const;
	float GetVerticalLookScale() const;

	/** §18.3 헤드밥 진폭. 걷기·앉기·달리기가 각각 다르다. */
	float GetHeadBobAmplitude() const;

	/** §18.3 데드존과 응답 곡선. 원시 스틱 값을 -1~1로 다시 편다. */
	static float ShapeGamepadLookAxis(float RawStick);

	/** 스틱 하나를 읽어 초당 회전 상한 안에서 시점을 민다. */
	bool ApplyGamepadLook(const FKey& StickAxis, bool bYaw);

	/** §18.3. 지금 쓰는 장치의 시점 감도. 마우스와 패드를 따로 둔다. */
	float GetLookSensitivity() const;
	void Turn(float Value);
	void LookUp(float Value);
	void BeginInteraction();
	void EndInteraction();
	void BeginSprint();
	void EndSprint();
	void BeginJump();
	void EndJump();
	void BeginCrouchInput();
	void EndCrouchInput();
	void ToggleCrouch();
	void Knock();
	void BeginListen();
	void EndListen();
	void BeginHoldBreath();
	void EndHoldBreath();
	void ToggleFlashlight();
	void LoadLatestAutosave();
	void ApplyContextMovementSpeed();
	void RefreshSprintState();
	void UpdateCrouchTransition(float DeltaSeconds);
	void UpdateContextualActions(float DeltaSeconds);
	/** 발을 뗀 직후의 점프와 착지 직전에 미리 누른 점프를 받아 준다(§9). */
	void UpdateJumpAssist();
	/** 일어서려는데 머리 위가 막혔으면 한 번 알려 준다. */
	void UpdateStandBlock(float DeltaSeconds);
	/** 옆·뒤로 움직이는 동안은 달리지 않는다. */
	void UpdateSprintDirection();
	void FinishHoldBreath(bool bForcedRelease);
	void ApplyPlayerKnockFeedback();
	void RegisterKnockSequenceTap();
	void HandleForeignNoise(const FIGNoiseEvent& Event);
	FDelegateHandle ForeignNoiseHandle;
	/** 존재의 연출 소리(§19.8 대체 채널). 소음 버스와 따로 온다. */
	FDelegateHandle ForeignCueHandle;
	void StopChaseHaptic();
	void UpdateChaseHaptic(float DeltaSeconds);
	void PlayHapticFeedback(float Intensity, float DurationSeconds) const;
	/**
	 * Samples how dark it is where the player stands, for the stress model.
	 * bCountFlashlight가 false면 손전등을 빼고 방의 불빛만 잰다(어둑시니가 쓴다).
	 */
	float SampleAmbientDarkness(bool bCountFlashlight = true) const;
	/** Footstep cadence and its noise report; runs whether or not the camera bobs. */
	void UpdateFootsteps(float DeltaSeconds);
	void UpdateCaptureFeedback(float DeltaSeconds);
	void UpdateCameraMotion(float DeltaSeconds);
	void UpdateCarriedItem(float DeltaSeconds);
	void PlayFootstep(float SpeedScale);
	EIGFootstepSurface ResolveFootstepSurface() const;
	float GetSurfaceMovementScale(EIGFootstepSurface Surface) const;
	float ResolveFootstepNoiseLoudness(EIGFootstepSurface Surface) const;
	void UpdateMicrophoneNoise(float DeltaSeconds);
	void StopMicrophoneCapture();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIGInteractionComponent> InteractionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIGFlashlightComponent> Flashlight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIGStressComponent> StressComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIGCameraSensorComponent> CameraSensor;

	UPROPERTY(Transient)
	TObjectPtr<UIGAccessibilitySubsystem> AccessibilitySubsystem;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> MoveInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> LookInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> InteractInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Player|Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> FlashlightInputAction;

	/**
	 * 한 번 발을 디딜 때 이동하는 거리(cm).
	 * 위아래 흔들림은 abs(sin)이므로 한 걸음마다 반복된다.
	 * §18.3의 걷기 주기 0.52초 × 이동 속도 300cm/s = 156cm.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Player|Camera", meta = (AllowPrivateAccess = "true", ClampMin = "10.0", Units = "cm"))
	float StepDistance = 156.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Player|Camera", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float FootstepVolume = 0.34f;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> CarriedActor;

	FVector CameraBaseLocation = FVector(0.0f, 0.0f, 64.0f);
	FRotator CameraFeelRotation = FRotator::ZeroRotator;
	float ScareCameraKick = 0.0f;
	float TraveledDistanceAccum = 0.0f;
	float BreathTime = 0.0f;
	float BreathPhase = 0.0f;
	float SprintActiveSeconds = 0.0f;
	float SprintRecoverySeconds = 0.0f;
	float ListenHeldSeconds = 0.0f;
	float BreathHeldSeconds = 0.0f;
	float CrouchTransitionRemaining = 0.0f;
	float CrouchCameraCompensation = 0.0f;
	float CrouchCameraCompensationStart = 0.0f;
	float AppliedCrouchCameraCompensation = 0.0f;
	float KnockCameraKick = 0.0f;
	/** 착지 직후 시점이 내려앉는 깊이(cm). 무릎이 접히는 만큼이고 곧 되돌아온다. */
	float LandingDip = 0.0f;
	/** 달릴 때 열리는 시야각(도). 접근성 시야각 위에 얹는다. */
	float SprintFovOffset = 0.0f;
	float BaseFieldOfView = 78.0f;
	float CaptureFeedbackDurationSeconds = 0.0f;
	bool bChaseHapticActive = false;
	float ChaseHapticAlpha = 0.0f;
	int32 ChaseForceFeedbackHandle = 0;
	float CaptureFeedbackRemainingSeconds = 0.0f;
	UPROPERTY(Transient) TWeakObjectPtr<class AIGListenerEntity> CaptureThreat;
	FRotator CaptureStartRotation = FRotator::ZeroRotator;
	uint64 CaptureForceFeedbackHandle = 0;
	float CaptureCutSeconds = -1.0f;
	bool bCaptureCutDone = false;
	float CaptureFovScale = 1.0f;
	float CaptureImpactAlpha = 0.0f;
	float CaptureTunnelAlpha = 0.0f;
	float ThreatVignetteTarget = 0.0f;
	float ThreatVignetteAlpha = 0.0f;
	double LastKnockInputSeconds = -1.0;
	double KnockInputLockedUntil = -1.0;
	int32 LastStepIndex = 0;
	int32 KnockSequenceTapCount = 0;
	bool bCameraMotionEnabled = false;
	bool bSprintInputHeld = false;
	bool bSprinting = false;
	bool bCrouchInputHeld = false;
	bool bListening = false;
	bool bListenTriggered = false;
	bool bHoldingBreath = false;
	/** 숨 참기 키를 쥐고 있다. 내쉰 직후의 쉬는 틈이 끝나면 다시 참는다. */
	bool bHoldBreathInputHeld = false;
	double BreathReleasedSeconds = -10.0;
	/** 마지막으로 땅을 디딘 시각과, 공중에서 미리 누른 점프가 유효한 시각. */
	double LastGroundedSeconds = -10.0;
	double JumpBufferedUntilSeconds = -1.0;
	bool bCoyoteJumpReady = false;
	/** 일어서라고 한 뒤에도 앉아 있던 시간. 속말은 앉을 때마다 한 번이다. */
	float BlockedStandSeconds = 0.0f;
	bool bBlockedStandThoughtShown = false;
	bool bSprintDirectionAllowed = true;
	TWeakObjectPtr<class AIGHidingSpot> HidingSpot;
	bool bInteractionRedirectedToListen = false;
	bool bInteractionRedirectedToInterludeListen = false;
	/** 패드 엿듣기 키를 밤3 벽이 아닌 엿듣기 판정에서 눌러 상호작용 홀드로 넘긴 중. */
	bool bListenRedirectedToInteraction = false;
	bool bHeavyBagInteractionProxyActive = false;
	FVector HeavyBagRestLocation = FVector::ZeroVector;

	/** Direct capture path: samples are reduced to an envelope, never retained. */
	Audio::FAudioCaptureSynth* MicrophoneCaptureSynth = nullptr;
	TArray<float> MicrophoneScratchSamples;
	float MicrophonePollAccumulator = 0.0f;
	float MicrophoneNoiseFloor = 0.012f;
	float MicrophoneCalibrationRemaining = 0.0f;
	float MicrophoneReportCooldown = 0.0f;
	float LastFootstepNoiseLoudness = 0.0f;
	EIGFootstepSurface LastFootstepSurface = EIGFootstepSurface::Concrete;
	bool bMicrophoneCaptureRunning = false;
	bool bMicrophoneOpenAttempted = false;

	/** Decaying kick applied when an interaction is pressed. */
	float InteractPunch = 0.0f;
	/** Throttles the darkness probe: it traces, so it does not run per frame. */
	float DarknessSampleTimer = 0.0f;
	float CachedDarkness = 0.0f;
	/** 손전등을 뺀 어둠. 방에 불이 켜져 있는지만 본다. */
	float CachedRoomDarkness = 0.0f;
	/** Held-item inertia: lag offset and the previous view rotation driving it. */
	FRotator CarrySwayOffset = FRotator::ZeroRotator;
	FRotator PreviousControlRotation = FRotator::ZeroRotator;
	FRotator CarriedBaseRotation = FRotator::ZeroRotator;
	FVector CarriedBaseLocation = FVector::ZeroVector;
};
