#include "Player/IGPlayerCharacter.h"
#include "Core/IGPlayRecord.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "AudioCaptureCore.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGMissingFloorFifthDawnDirector.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Environment/IGDustSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "Interaction/IGHidingSpot.h"
#include "Interaction/IGPickupItem.h"
#include "Interaction/IGSwingDoor.h"
#include "Narrative/IGStoryHelpers.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "Interaction/IGReadableNote.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGInputBindingSubsystem.h"
#include "Player/IGInteractionComponent.h"
#include "Player/IGPlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Player/IGCameraSensorComponent.h"
#include "Player/IGStressComponent.h"
#include "Save/IGSaveSubsystem.h"

namespace IGPlayerNoise
{
	/**
	 * The project's ordinary walk speed. Footstep loudness is measured against
	 * this fixed reference rather than the movement component's current cap, so
	 * a chapter that slows the player also makes them quieter — which is what
	 * moving carefully should mean.
	 */
	constexpr float ReferenceWalkSpeed = 300.0f;
	constexpr float SprintSpeed = 460.0f;
	constexpr float CrouchSpeed = 160.0f;
	constexpr float ListenSpeed = 80.0f;
	constexpr float SprintBreathThresholdSeconds = 3.5f;
	constexpr float ListenCommitSeconds = 0.8f;
	constexpr float MaximumBreathHoldSeconds = 4.0f;
	constexpr float WalkAcceleration = 1200.0f;
	// 키를 놓으면 서야 한다. 1200은 걷기에서 37cm를 미끄러졌고, 문 앞에서
	// 멈추려다 문에 닿는 조작감이 그것이었다. 2000이면 0.15초, 22cm.
	constexpr float WalkBraking = 2000.0f;
	constexpr float CrouchAcceleration = 900.0f;
	constexpr float CrouchBraking = 2200.0f;
	constexpr float SprintAcceleration = 1400.0f;
	// 달린 뒤에도 문 하나 폭 안에서 멈춘다. 900은 1.18m를 미끄러져
	// 모서리를 돌아 숨으려는 입력이 늦게 먹는 것처럼 느껴졌다.
	constexpr float SprintBraking = 1800.0f;
	// §27.3. 듣는 동안은 거의 서 있다. 멈추는 것도 빨라야 소리를 놓치지
	// 않는다 — 제동이 네 상태 중 가장 세다.
	constexpr float ListenAcceleration = 800.0f;
	constexpr float ListenBraking = 2600.0f;
	constexpr float CrouchTransitionSeconds = 0.35f;
	constexpr float CrouchTransitionSpeedScale = 0.5f;
	// §9. 계단 끝이나 턱에서 발을 뗀 직후, 또는 착지 직전에 누른 점프가 씹히지 않는
	// 여유. 둘 다 사람 손이 한 박자 늦거나 이른 만큼이다.
	constexpr float CoyoteSeconds = 0.12f;
	constexpr float JumpBufferSeconds = 0.12f;
	// 숨을 내쉰 뒤 다시 참기까지. 놓은 반동을 연타로 지우지 못한다.
	constexpr float BreathHoldCooldownSeconds = 0.8f;
	// 앞으로 이만큼(약 50도 안쪽) 향해야 달린다. 옆걸음·뒷걸음은 걷는 속도다.
	constexpr float SprintForwardDot = 0.64f;
	// 일어서려는데 이만큼 막혀 있으면 속말로 알린다. 문틀 밑을 지나며 잠깐 막힌
	// 정도로는 말하지 않는다.
	constexpr float BlockedStandNoticeSeconds = 0.35f;
	constexpr float KnockInputLockSeconds = 0.9f;
	constexpr float KnockSequenceResetSeconds = 1.8f;
	// §18.3 헤드밥 진폭. 자세마다 다르다 — 앉은 걸음이 선 걸음만큼 흔들리면
	// 화면이 자세를 말해 주지 않는다.
	constexpr float WalkBobAmplitude = 1.1f;
	constexpr float CrouchBobAmplitude = 0.5f;
	constexpr float SprintBobAmplitude = 2.4f;
	// 동작 감소에서 줄이되 0으로 만들지 않는다. 발걸음 리듬은 소음 학습의
	// 시각 보조라서, 없애면 편의가 아니라 정보를 뺏는 것이 된다.
	constexpr float ReducedMotionBobScale = 0.25f;
	// §18.3 패드 시점. 안쪽은 스틱이 가운데로 안 돌아오는 만큼을 버리고,
	// 바깥쪽은 대각선에서 원을 벗어나는 만큼을 접는다. 지수 2.2는 작은
	// 기울임을 더 작게 만들어 조준 없이 둘러보는 손에 맞춘다.
	constexpr float PadInnerDeadzone = 0.18f;
	constexpr float PadOuterDeadzone = 0.92f;
	constexpr float PadResponseExponent = 2.2f;
	// 초당 회전 상한. 스틱은 미는 동안 프레임마다 같은 값이 들어와서, 이걸
	// 안 걸면 프레임률이 높은 기계에서 그만큼 빨리 돈다.
	constexpr float PadMaximumTurnRateDegrees = 140.0f;
	constexpr float KnockCameraKickDegrees = 0.4f;
	constexpr float KnockCameraReturnSeconds = 0.18f;
	// 달릴 때 시야각이 조금 열리고, 착지 때 시점이 무릎만큼 내려앉는다. 속도감과
	// 무게감은 여기서 온다. 둘 다 동작 감소에서는 0이다.
	constexpr float SprintFieldOfViewBoost = 5.0f;
	constexpr float LandingDipMinCentimeters = 0.6f;
	constexpr float LandingDipMaxCentimeters = 3.2f;
	constexpr float CaptureCameraKickDegrees = 3.2f;
	constexpr float CaptureHapticIntensity = 0.70f;
	// §18.6 햅틱 계약. 표의 여섯 줄 중 셋이 여기 있다.
	constexpr float ImpactHapticIntensity = 0.55f;
	constexpr float ImpactHapticSeconds = 0.20f;
	constexpr float ChaseHapticIntensity = 0.25f;
	constexpr float ChaseHapticFadeInSeconds = 0.40f;
	constexpr float HeartbeatHapticIntensity = 0.10f;
	/** Quietest and loudest footfall reported to the noise bus (§5.1). */
	constexpr float MinimumFootstepLoudness = 0.06f;
	constexpr float MaximumFootstepLoudness = 0.18f;
	constexpr float CrouchFootstepLoudness = 0.05f;
	// §5.1: 홀드를 놓쳐서 손이 미끄러지는 소리. 쪽지 넘기는 것과 같은
	// 크기지만 다른 행동이라 각자 든다.
	constexpr float ForcedReleaseLoudness = 0.08f;
	// 손전등 스위치. 0.04면 1 m 남짓까지만 간다. 문 바로 너머에 붙은 것에게만 들린다.
	constexpr float FlashlightClickLoudness = 0.04f;
	constexpr float SprintFootstepLoudness = 0.50f;
	constexpr float ExhaustedSprintFootstepLoudness = 0.70f;
	constexpr float MicrophonePollSeconds = 0.08f;
	constexpr float MicrophoneCalibrationSeconds = 1.50f;
	constexpr float MicrophoneMinimumThreshold = 0.028f;
	constexpr float MicrophoneReportCooldownSeconds = 0.28f;
	/** Capsule centre to sole, so a print lands on the floor and not the knee. */
	constexpr float FootPrintDropCentimeters = 88.0f;
	const FName VinylSurfaceTag(TEXT("Footstep.Vinyl"));
	const FName ConcreteSurfaceTag(TEXT("Footstep.Concrete"));
	const FName MetalStairSurfaceTag(TEXT("Footstep.MetalStair"));
	const FName RooftopSurfaceTag(TEXT("Footstep.Rooftop"));
	const FName GypsumSurfaceTag(TEXT("Footstep.GypsumDebris"));

	/** 표면별 녹음 접두와 벌 수. 발소리와 착지가 같은 표를 읽어야 한 발이 두 소리를 내지 않는다. */
	const TCHAR* FootstepSamplePrefix(const EIGFootstepSurface Surface, int32& OutCount)
	{
		OutCount = 5;
		switch (Surface)
		{
		case EIGFootstepSurface::Vinyl: return TEXT("Foot_Vinyl");
		case EIGFootstepSurface::MetalStair: return TEXT("Foot_MetalStair");
		case EIGFootstepSurface::Rooftop: OutCount = 3; return TEXT("Foot_Rooftop");
		case EIGFootstepSurface::GypsumDebris: return TEXT("Foot_Gypsum");
		case EIGFootstepSurface::Water: OutCount = 3; return TEXT("Foot_Water");
		default: return TEXT("Foot_Concrete");
		}
	}
}

AIGPlayerCharacter::AIGPlayerCharacter()
{
	// Tick only powers optional camera motion; it stays off until enabled.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// 86cm 문에서 양옆 여유를 13cm씩 둔다. 눈높이와 문 크기는 유지한다.
	GetCapsuleComponent()->InitCapsuleSize(30.0f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	MovementComponent->bOrientRotationToMovement = false;
	MovementComponent->bUseControllerDesiredRotation = true;
	MovementComponent->MaxWalkSpeed = 300.0f;
	MovementComponent->MaxWalkSpeedCrouched = IGPlayerNoise::CrouchSpeed;
	MovementComponent->MaxAcceleration = IGPlayerNoise::WalkAcceleration;
	MovementComponent->BrakingDecelerationWalking = IGPlayerNoise::WalkBraking;
	// 제동은 §18.2의 제동값만 건다. 기본 지면 마찰 8에 제동 마찰 배율 2가 곱해져
	// 속도 × 16이 먼저 걸리는데, 달리기 460cm/s에서 그 값은 7360cm/s²라 문서가
	// 정한 900/1200/1500은 옆에서 아무 일도 못 했다. 「달린 대가가 코너에서
	// 청구된다」는 제동 마찰을 0으로 떼어야 실제로 청구된다.
	MovementComponent->bUseSeparateBrakingFriction = true;
	MovementComponent->BrakingFriction = 0.0f;
	MovementComponent->NavAgentProps.bCanCrouch = true;
	MovementComponent->NavAgentProps.bCanJump = true;
	// 실내의 낮은 짐은 넘되 계단 잠금을 건너뛰거나 1인칭 시점이 가벼워 보이지
	// 않을 정도로 점프 높이를 제한한다.
	MovementComponent->JumpZVelocity = 330.0f;
	MovementComponent->AirControl = 0.08f;
	MovementComponent->SetCrouchedHalfHeight(48.0f);
	// CharacterMovement's stock 750,000 push force is intended for heavy
	// physics gameplay. Against a 200 g slipper or an empty bottle it launches
	// the prop down the corridor from a light brush. Scale the impulse by mass
	// and cap the continuous touch force so small dressing can still be nudged
	// without exploding or spinning indefinitely.
	MovementComponent->bEnablePhysicsInteraction = true;
	MovementComponent->bPushForceScaledToMass = true;
	MovementComponent->bScalePushForceToVelocity = true;
	MovementComponent->InitialPushForceFactor = 250.0f;
	MovementComponent->PushForceFactor = 450.0f;
	MovementComponent->TouchForceFactor = 0.6f;
	MovementComponent->MinTouchForce = -1.0f;
	MovementComponent->MaxTouchForce = 120.0f;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(CameraBaseLocation);
	FirstPersonCamera->bUsePawnControlRotation = true;
	// ~standard 35mm feel; the default 90 reads wide-angle and warps depth.
	FirstPersonCamera->SetFieldOfView(78.0f);

	InteractionComponent = CreateDefaultSubobject<UIGInteractionComponent>(TEXT("InteractionComponent"));

	// The torch hangs off the camera so it aims where you look, but it has
	// its own sway on top of that — see UIGFlashlightComponent.
	Flashlight = CreateDefaultSubobject<UIGFlashlightComponent>(TEXT("Flashlight"));
	Flashlight->SetupAttachment(FirstPersonCamera);
	Flashlight->SetRelativeLocation(FVector(12.0f, 14.0f, -12.0f));
	// 손에 든 손전등은 눈보다 낮고 조금 아래를 본다. 그래야 서너 걸음 앞 바닥이
	// 원뿔 안에 들어온다 — 수평으로 쏘면 빛이 복도 끝에서야 닿는다.
	Flashlight->SetRelativeRotation(FRotator(-3.5f, 1.5f, 0.0f));

	StressComponent = CreateDefaultSubobject<UIGStressComponent>(TEXT("Stress"));
	CameraSensor = CreateDefaultSubobject<UIGCameraSensorComponent>(TEXT("CameraSensor"));
}

AIGPlayerCharacter::~AIGPlayerCharacter()
{
	StopMicrophoneCapture();
}

void AIGPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		AccessibilitySubsystem =
			GameInstance->GetSubsystem<UIGAccessibilitySubsystem>();
	}

	RefreshMicrophoneCaptureMode();
	// §19.8 노크 진동 대체는 소음 버스를 타고 온다. 응답 노크 코드에
	// 손을 대지 않으므로 §18.5의 무진동 규칙은 그대로 서 있다.
	if (UWorld* NoiseWorld = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise =
			NoiseWorld->GetSubsystem<UIGNoiseSubsystem>())
		{
			ForeignNoiseHandle = Noise->OnNoiseReported.AddUObject(
				this, &AIGPlayerCharacter::HandleForeignNoise);
			// 그의 노크·대답·추격·다가오는 걸음은 소음 버스에 없다. 대체 채널용
			// 신호로 따로 온다.
			ForeignCueHandle = Noise->OnPresentationCue.AddUObject(
				this, &AIGPlayerCharacter::HandleForeignNoise);
		}
	}
	// 저장된 시야각을 첫 프레임부터 건다. 설정을 켜 봐야 적용되면
	// 「저장이 안 됐다」로 읽힌다.
	RefreshFieldOfView();
	// 걸음 흔들림·숨·착지 내려앉음은 켜 주는 사람이 있어야 돌았다. 감독이
	// 안 켜 준 진입 경로(챕터 직행, 밤 루프 기상)는 화면이 굳은 채였다.
	SetCameraMotionEnabled(true);
	SetActorTickEnabled(true);
}

void AIGPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopMicrophoneCapture();
	if (const UWorld* NoiseWorld = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise =
			NoiseWorld->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->OnNoiseReported.Remove(ForeignNoiseHandle);
			Noise->OnPresentationCue.Remove(ForeignCueHandle);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AIGPlayerCharacter::OnStartCrouch(
	const float HalfHeightAdjust,
	const float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	UIGPlayRecordSubsystem::Note(this, TEXT("crouch"));
	CrouchTransitionRemaining = IGPlayerNoise::CrouchTransitionSeconds;
	CrouchCameraCompensationStart =
		AppliedCrouchCameraCompensation + ScaledHalfHeightAdjust;
	CrouchCameraCompensation = CrouchCameraCompensationStart;
	if (FirstPersonCamera)
	{
		// 캡슐 이동 중에는 자식의 월드 좌표 갱신이 미뤄진다. 먼저 반영해야
		// 같은 월드 위치로 판정되어 높이 보정이 생략되는 일이 없다.
		FirstPersonCamera->UpdateComponentToWorld();
		FVector CameraLocation = FirstPersonCamera->GetRelativeLocation();
		CameraLocation.Z += CrouchCameraCompensation
			- AppliedCrouchCameraCompensation;
		FirstPersonCamera->SetRelativeLocation(CameraLocation);
		AppliedCrouchCameraCompensation = CrouchCameraCompensation;
	}
	ApplyContextMovementSpeed();
}

void AIGPlayerCharacter::OnEndCrouch(
	const float HalfHeightAdjust,
	const float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CrouchTransitionRemaining = IGPlayerNoise::CrouchTransitionSeconds;
	CrouchCameraCompensationStart =
		AppliedCrouchCameraCompensation - ScaledHalfHeightAdjust;
	CrouchCameraCompensation = CrouchCameraCompensationStart;
	if (FirstPersonCamera)
	{
		FirstPersonCamera->UpdateComponentToWorld();
		FVector CameraLocation = FirstPersonCamera->GetRelativeLocation();
		CameraLocation.Z += CrouchCameraCompensation
			- AppliedCrouchCameraCompensation;
		FirstPersonCamera->SetRelativeLocation(CameraLocation);
		AppliedCrouchCameraCompensation = CrouchCameraCompensation;
	}
	ApplyContextMovementSpeed();
}

void AIGPlayerCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Feed the fear model how dark it is here. The probe traces, so it runs
	// a few times a second and the result is smoothed between samples.
	DarknessSampleTimer -= DeltaSeconds;
	if (DarknessSampleTimer <= 0.0f)
	{
		DarknessSampleTimer = 0.25f;
		// 불빛 판정은 한 번만 돈다. 손전등이 켜져 있으면 공포 모델의 어둠은 0이고,
		// 방의 어둠은 그대로 잰다.
		const float RoomDarkness = SampleAmbientDarkness(false);
		const bool bTorchLit = Flashlight && Flashlight->IsProvidingLight();
		CachedRoomDarkness = FMath::FInterpTo(CachedRoomDarkness, RoomDarkness, 1.0f, 0.55f);
		CachedDarkness = FMath::FInterpTo(CachedDarkness, bTorchLit ? 0.0f : RoomDarkness, 1.0f, 0.55f);
		if (StressComponent)
		{
			StressComponent->SetDarkness(CachedDarkness);
		}
	}

	UpdateContextualActions(DeltaSeconds);
	UpdateJumpAssist();
	UpdateStandBlock(DeltaSeconds);
	UpdateSprintDirection();
	UpdateCrouchTransition(DeltaSeconds);
	UpdateFootsteps(DeltaSeconds);
	if (StressComponent)
	{
		// 숨이 찬다. 3.5초 넘게 달리면 발소리가 커지는 §18.2의 같은 값으로
		// 숨소리도 오른다. 멈춘 뒤 고르는 건 스트레스 쪽이 한다.
		const float BreathLoad = (bSprinting && !bIsCrouched)
			? FMath::Clamp(
				(SprintActiveSeconds - IGPlayerNoise::SprintBreathThresholdSeconds) / 3.5f,
				0.0f,
				1.0f)
			: 0.0f;
		StressComponent->SetExertion(BreathLoad);
	}
	UpdateCaptureFeedback(DeltaSeconds);
	UpdateChaseHaptic(DeltaSeconds);
	UpdateCameraMotion(DeltaSeconds);
	UpdateCarriedItem(DeltaSeconds);
	UpdateMicrophoneNoise(DeltaSeconds);
}

float AIGPlayerCharacter::SampleAmbientDarkness(const bool bCountFlashlight) const
{
	// There is no cheap way to read scene luminance from gameplay code, so
	// darkness is inferred from the lights that can actually reach us: any
	// point/spot light within range and not occluded counts, weighted by
	// inverse-square falloff. It is approximate, and it only has to be good
	// enough to tell "lit corridor" from "dead stairwell".
	const UWorld* World = GetWorld();
	if (!World || !FirstPersonCamera)
	{
		return 0.0f;
	}

	const FVector EyeLocation = FirstPersonCamera->GetComponentLocation();

	// A working torch is enough light to keep the dark at bay — but only
	// while it is genuinely lit. IsProvidingLight() is false through a
	// brown-out, so a dying cell stops shielding the player from the dark.
	if (bCountFlashlight && Flashlight && Flashlight->IsProvidingLight())
	{
		return 0.0f;
	}

	float Illumination = 0.0f;
	for (TObjectIterator<UPointLightComponent> LightIterator; LightIterator; ++LightIterator)
	{
		const UPointLightComponent* Light = *LightIterator;
		if (!IsValid(Light) || Light->GetWorld() != World || !Light->IsVisible())
		{
			continue;
		}
		// 손에 든 손전등(스포트라이트도 점광원의 자식이다)은 방의 불빛이 아니다.
		if (Light->GetOwner() == this)
		{
			continue;
		}

		const FVector LightLocation = Light->GetComponentLocation();
		const float Distance = FVector::Dist(EyeLocation, LightLocation);
		if (Distance > Light->AttenuationRadius)
		{
			continue;
		}

		// Anything solid between us and the lamp contributes nothing.
		FHitResult Occlusion;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IGDarknessProbe), false, this);
		if (World->LineTraceSingleByChannel(
				Occlusion, EyeLocation, LightLocation, ECC_Visibility, QueryParams))
		{
			continue;
		}

		const float Falloff = 1.0f - FMath::Clamp(Distance / Light->AttenuationRadius, 0.0f, 1.0f);
		Illumination += Light->Intensity * Falloff * Falloff / 1000.0f;
	}

	// 1.0 lit is roughly "one corridor fluorescent, two metres away".
	return 1.0f - FMath::Clamp(Illumination, 0.0f, 1.0f);
}

void AIGPlayerCharacter::ToggleFlashlight()
{
	if (!Flashlight || !Flashlight->IsAvailable())
	{
		// 아직 손전등이 없다. 주머니를 더듬는 만큼만 고개가 숙는다.
		InteractPunch = FMath::Max(InteractPunch, 0.3f);
		SetCameraMotionEnabled(true);
		return;
	}

	const bool bNowOn = Flashlight->Toggle();
	UIGPlayRecordSubsystem::Note(this, TEXT("flashlight"));
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(GetActorLocation(), IGPlayerNoise::FlashlightClickLoudness, this);
		}
	}
	// The click is audible either way — a dead cell still clicks. 바코드
	// 스캐너 삐 소리를 높여 쓰던 것을 진짜 슬라이드 스위치 소리로 바꿨다.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateSwitchClick(this, bNowOn),
		GetActorLocation(),
		0.34f,
		1.0f,
		60.0f,
		320.0f,
		EIGAudioBus::Player);
}

void AIGPlayerCharacter::UpdateCarriedItem(const float DeltaSeconds)
{
	AActor* Carried = CarriedActor.Get();
	if (!IsValid(Carried) || !Controller || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// Held objects lag behind the view instead of being welded to it: the
	// faster the look, the further the item swings before settling back.
	const FRotator ControlRotation = Controller->GetControlRotation();
	const FRotator RotationDelta = (ControlRotation - PreviousControlRotation).GetNormalized();
	PreviousControlRotation = ControlRotation;

	CarrySwayOffset.Yaw = FMath::Clamp(
		CarrySwayOffset.Yaw - RotationDelta.Yaw * 0.55f, -14.0f, 14.0f);
	CarrySwayOffset.Pitch = FMath::Clamp(
		CarrySwayOffset.Pitch - RotationDelta.Pitch * 0.45f, -12.0f, 12.0f);
	CarrySwayOffset = FMath::RInterpTo(
		CarrySwayOffset, FRotator::ZeroRotator, DeltaSeconds, 6.0f);

	// A slow bob keeps the item alive in hand while standing still.
	const float BobPhase = BreathTime * 2.0f * UE_PI * 0.35f;
	const FVector CarryBob(
		0.0f,
		FMath::Sin(BobPhase) * 0.35f,
		FMath::Sin(BobPhase * 1.7f) * 0.5f);

	Carried->SetActorRelativeLocation(CarriedBaseLocation + CarryBob);
	Carried->SetActorRelativeRotation(CarriedBaseRotation + CarrySwayOffset);
}

void AIGPlayerCharacter::SetCameraMotionEnabled(const bool bEnabled)
{
	if (bCameraMotionEnabled == bEnabled)
	{
		return;
	}

	bCameraMotionEnabled = bEnabled;
	// Tick stays on regardless: the fear model samples darkness even while a
	// director owns the camera. UpdateCameraMotion gates itself on the flag.
	SetActorTickEnabled(true);

	if (!bEnabled && FirstPersonCamera)
	{
		FirstPersonCamera->SetRelativeLocation(
			CameraBaseLocation
				+ FVector(0.0f, 0.0f, CrouchCameraCompensation));
		AppliedCrouchCameraCompensation = CrouchCameraCompensation;
	}
}

AIGHidingSpot* AIGPlayerCharacter::GetHidingSpot() const
{
	return HidingSpot.Get();
}

bool AIGPlayerCharacter::IsInHidingSpot() const
{
	return HidingSpot.IsValid();
}

void AIGPlayerCharacter::SetHidingSpot(AIGHidingSpot* Spot)
{
	HidingSpot = Spot;
}

void AIGPlayerCharacter::LeaveHidingSpotImmediately()
{
	if (AIGHidingSpot* Spot = HidingSpot.Get())
	{
		Spot->ForceExit();
	}
	HidingSpot.Reset();
}

FVector AIGPlayerCharacter::GetEyeOffsetFromActor() const
{
	return FirstPersonCamera
		? FirstPersonCamera->GetComponentLocation() - GetActorLocation()
		: FVector(0.0f, 0.0f, BaseEyeHeight);
}

void AIGPlayerCharacter::PlayScareKick(const float Degrees)
{
	ScareCameraKick = FMath::Max(ScareCameraKick, FMath::Clamp(Degrees, 0.0f, 4.0f));
	SetCameraMotionEnabled(true);
}

void AIGPlayerCharacter::SetCaptureThreat(AIGListenerEntity* Threat)
{
	CaptureThreat = Threat;
}

bool AIGPlayerCharacter::HasPhysicalCaptureView() const
{
	return CaptureFeedbackRemainingSeconds > 0.0f && CaptureThreat.IsValid()
		&& CaptureThreat->HasPhysicalCaptureBody();
}

void AIGPlayerCharacter::PlayCaptureFeedback(const float DurationSeconds, const float CutSeconds)
{
	// 숨어 있다 잡히면 그 자리에서 끌려 나온다. 충돌과 이동을 먼저 돌려놓아야
	// 뒤따르는 리셋이 몸을 침대로 옮길 수 있다.
	LeaveHidingSpotImmediately();
	CaptureFeedbackDurationSeconds = FMath::Max(DurationSeconds, 0.05f);
	CaptureFeedbackRemainingSeconds = CaptureFeedbackDurationSeconds;
	CaptureStartRotation = GetControlRotation();
	// 몸이 닿고 1초 안에 화면을 끊는다. 천천히 어두워지면 잡힌 채 멈춰 선 몸을
	// 2초 가까이 보게 되고, 그게 공포가 아니라 정지 화면으로 읽혔다. 동작
	// 감소에서는 넘어지지 않고 잠깐 본 뒤 바로 끊는다.
	const bool bReducedMotion = AccessibilitySubsystem
		&& AccessibilitySubsystem->IsReducedCameraMotionEnabled();
	CaptureCutSeconds = FMath::Min(
		bReducedMotion ? 0.45f : (CutSeconds > 0.0f ? CutSeconds : 0.95f),
		CaptureFeedbackDurationSeconds);
	bCaptureCutDone = false;
	CaptureFovScale = 1.0f;
	CaptureImpactAlpha = 0.0f;
	CaptureTunnelAlpha = 0.0f;
	SetCameraMotionEnabled(true);
	if (Flashlight && HasPhysicalCaptureView())
	{
		// 부딪힌 손에서 손전등이 튕겨 나간다. 괴물은 바닥에 떨어진 빛 언저리에서만 보인다.
		Flashlight->PlayKnockLoose();
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(PlayerController->GetHUD()))
		{
			HorrorHUD->PlayCaptureEmbrace(CaptureFeedbackDurationSeconds);
		}

		if (CaptureForceFeedbackHandle > 0)
		{
			PlayerController->PlayDynamicForceFeedback(
				0.0f, 0.0f, true, true, true, true,
				EDynamicForceFeedbackAction::Stop,
				CaptureForceFeedbackHandle);
			CaptureForceFeedbackHandle = 0;
		}
		if (!AccessibilitySubsystem || AccessibilitySubsystem->AreHapticsEnabled())
		{
			CaptureForceFeedbackHandle = PlayerController->PlayDynamicForceFeedback(
				IGPlayerNoise::CaptureHapticIntensity,
				CaptureFeedbackDurationSeconds,
				true, true, true, true,
				EDynamicForceFeedbackAction::Start);
		}
	}
}

void AIGPlayerCharacter::UpdateCaptureFeedback(const float DeltaSeconds)
{
	if (CaptureFeedbackRemainingSeconds <= 0.0f || DeltaSeconds <= 0.0f)
	{
		return;
	}

	CaptureFeedbackRemainingSeconds = FMath::Max(
		0.0f,
		CaptureFeedbackRemainingSeconds - DeltaSeconds);
	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	const bool bReducedMotion = AccessibilitySubsystem
		&& AccessibilitySubsystem->IsReducedCameraMotionEnabled();
	const float Age = CaptureFeedbackDurationSeconds - CaptureFeedbackRemainingSeconds;
	const bool bPhysical = HasPhysicalCaptureView();
	if (PlayerController && bPhysical && !bReducedMotion)
	{
		// 0.14초 만에 고개가 그쪽으로 꺾인다. 부드럽게 돌아가면 잡힌 게 아니라
		// 돌아보는 것처럼 보였다. 넘어지는 동안에도 얼굴을 놓치지 않는다. 얼굴은
		// 한 번 물러났다가 달려들므로 가운데에 있어도 크기가 크게 변한다.
		const float TurnAlpha = FMath::SmoothStep(0.0f, 0.14f, Age);
		FRotator FocusRotation = (CaptureThreat->GetCaptureFaceLocation()
			- FirstPersonCamera->GetComponentLocation()).Rotation();
		FocusRotation.Pitch = FMath::Clamp(FocusRotation.Pitch, -60.0f, 70.0f);
		PlayerController->SetControlRotation(FQuat::Slerp(CaptureStartRotation.Quaternion(),
			FocusRotation.Quaternion(), TurnAlpha).Rotator());
	}
	if (bPhysical)
	{
		// 화면 충격은 닿는 순간 가장 크고 곧 가라앉는다. 시야는 끊기기 직전까지
		// 조여 온다. 동작 감소에서는 시야각을 건드리지 않는다.
		CaptureImpactAlpha = FMath::Exp(-Age * 6.0f);
		CaptureTunnelAlpha = FMath::SmoothStep(0.05f, CaptureCutSeconds, Age);
		CaptureFovScale = bReducedMotion
			? 1.0f
			: FMath::Lerp(1.0f, 0.80f, FMath::SmoothStep(0.05f, CaptureCutSeconds, Age));
		if (!bCaptureCutDone && Age >= CaptureCutSeconds)
		{
			// 얼굴이 닿기 전에 끊는다. 검은 화면 뒤로 소리만 이어진다.
			bCaptureCutDone = true;
			if (PlayerController && PlayerController->PlayerCameraManager)
			{
				PlayerController->PlayerCameraManager->SetManualCameraFade(
					1.0f, FLinearColor::Black, false);
			}
			if (UWorld* World = GetWorld())
			{
				if (UIGMissingFloorAudioSubsystem* AudioDirector =
					World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
				{
					AudioDirector->PlayCaptureCut();
				}
			}
			CaptureFovScale = 1.0f;
			CaptureImpactAlpha = 0.0f;
			CaptureTunnelAlpha = 0.0f;
		}
	}
	if (CaptureFeedbackRemainingSeconds <= 0.0f || bCaptureCutDone)
	{
		CaptureFovScale = 1.0f;
		CaptureImpactAlpha = 0.0f;
		CaptureTunnelAlpha = 0.0f;
	}
	if (CaptureFeedbackRemainingSeconds <= 0.0f && Flashlight)
	{
		Flashlight->ClearKnockLoose();
	}
	const bool bHapticsEnabled = !AccessibilitySubsystem
		|| AccessibilitySubsystem->AreHapticsEnabled();
	if (PlayerController && CaptureForceFeedbackHandle > 0)
	{
		if (bHapticsEnabled && CaptureFeedbackRemainingSeconds > 0.0f)
		{
			const float RemainingAlpha = CaptureFeedbackRemainingSeconds
				/ FMath::Max(CaptureFeedbackDurationSeconds, 0.05f);
			CaptureForceFeedbackHandle = PlayerController->PlayDynamicForceFeedback(
				IGPlayerNoise::CaptureHapticIntensity * RemainingAlpha,
				0.0f,
				true, true, true, true,
				EDynamicForceFeedbackAction::Update,
				CaptureForceFeedbackHandle);
		}
		else
		{
			PlayerController->PlayDynamicForceFeedback(
				0.0f, 0.0f, true, true, true, true,
				EDynamicForceFeedbackAction::Stop,
				CaptureForceFeedbackHandle);
			CaptureForceFeedbackHandle = 0;
		}
	}
}

void AIGPlayerCharacter::ApplyContextMovementSpeed()
{
	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!MovementComponent)
	{
		return;
	}

	const float TransitionScale = CrouchTransitionRemaining > 0.0f
		? IGPlayerNoise::CrouchTransitionSpeedScale
		: 1.0f;
	const float SurfaceScale = GetSurfaceMovementScale(LastFootstepSurface);
	MovementComponent->MaxWalkSpeedCrouched =
		IGPlayerNoise::CrouchSpeed * TransitionScale * SurfaceScale;

	if (bListening)
	{
		MovementComponent->MaxWalkSpeed =
			IGPlayerNoise::ListenSpeed * TransitionScale * SurfaceScale;
		MovementComponent->MaxAcceleration = IGPlayerNoise::ListenAcceleration;
		MovementComponent->BrakingDecelerationWalking =
			IGPlayerNoise::ListenBraking;
		return;
	}
	if (bSprinting && !bIsCrouched)
	{
		MovementComponent->MaxWalkSpeed =
			IGPlayerNoise::SprintSpeed * TransitionScale * SurfaceScale;
		MovementComponent->MaxAcceleration = IGPlayerNoise::SprintAcceleration;
		MovementComponent->BrakingDecelerationWalking = IGPlayerNoise::SprintBraking;
		return;
	}
	if (bIsCrouched)
	{
		MovementComponent->MaxWalkSpeed =
			IGPlayerNoise::ReferenceWalkSpeed * TransitionScale * SurfaceScale;
		MovementComponent->MaxAcceleration = IGPlayerNoise::CrouchAcceleration;
		MovementComponent->BrakingDecelerationWalking = IGPlayerNoise::CrouchBraking;
		return;
	}

	MovementComponent->MaxWalkSpeed =
		IGPlayerNoise::ReferenceWalkSpeed * TransitionScale * SurfaceScale;
	MovementComponent->MaxAcceleration = IGPlayerNoise::WalkAcceleration;
	MovementComponent->BrakingDecelerationWalking = IGPlayerNoise::WalkBraking;
}

void AIGPlayerCharacter::RefreshSprintState()
{
	bSprinting = bSprintInputHeld
		&& bSprintDirectionAllowed
		&& !bListening
		&& !bIsCrouched
		&& CrouchTransitionRemaining <= 0.0f;
	ApplyContextMovementSpeed();
}

void AIGPlayerCharacter::UpdateJumpAssist()
{
	const UWorld* World = GetWorld();
	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!World || !MovementComponent || !MovementComponent->IsMovingOnGround())
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	LastGroundedSeconds = Now;
	bCoyoteJumpReady = true;
	// 착지 직전에 누른 점프는 땅을 디딘 다음 프레임에 뛴다. Landed 안에서 뛰면
	// 착지 처리가 점프 입력을 곧바로 지운다.
	if (JumpBufferedUntilSeconds < Now)
	{
		return;
	}
	JumpBufferedUntilSeconds = -1.0;
	if (bIsCrouched || AIGReadableNote::GetOpenNote())
	{
		return;
	}
	bSprinting = false;
	ApplyContextMovementSpeed();
	Jump();
}

void AIGPlayerCharacter::UpdateStandBlock(const float DeltaSeconds)
{
	// 일어서라고 했는데 아직 앉아 있으면 머리 위가 막힌 것이다. 엔진은 자리가
	// 나는 대로 알아서 세우므로 여기서는 왜 안 일어나는지만 알려 준다.
	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	const bool bStandBlocked = MovementComponent
		&& bIsCrouched
		&& !MovementComponent->bWantsToCrouch;
	if (!bStandBlocked)
	{
		BlockedStandSeconds = 0.0f;
		if (!bIsCrouched)
		{
			bBlockedStandThoughtShown = false;
		}
		return;
	}
	BlockedStandSeconds += DeltaSeconds;
	if (bBlockedStandThoughtShown
		|| BlockedStandSeconds < IGPlayerNoise::BlockedStandNoticeSeconds)
	{
		return;
	}
	bBlockedStandThoughtShown = true;
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT("IGMissingFloor", "YudamStandBlocked", "머리 위가 막혀서 못 일어나겠어."),
		2.0f);
}

void AIGPlayerCharacter::UpdateSprintDirection()
{
	// 달리기는 앞으로만 한다. 뒷걸음으로 달리면 쫓아오는 것을 보면서 도망칠 수
	// 있어 쫓기는 긴장이 빠진다. 서 있을 때는 막지 않는다(먼저 Shift를 쥐고 출발한다).
	const FVector Input = GetLastMovementInputVector().GetSafeNormal2D();
	const bool bForward = Input.IsNearlyZero()
		|| FVector::DotProduct(Input, GetActorForwardVector().GetSafeNormal2D())
			>= IGPlayerNoise::SprintForwardDot;
	if (bForward == bSprintDirectionAllowed)
	{
		return;
	}
	bSprintDirectionAllowed = bForward;
	RefreshSprintState();
}

void AIGPlayerCharacter::UpdateCrouchTransition(const float DeltaSeconds)
{
	if (CrouchTransitionRemaining <= 0.0f || DeltaSeconds <= 0.0f)
	{
		return;
	}

	CrouchTransitionRemaining = FMath::Max(
		0.0f,
		CrouchTransitionRemaining - DeltaSeconds);
	const float Alpha = CrouchTransitionRemaining
		/ IGPlayerNoise::CrouchTransitionSeconds;
	// 곧게 내리면 엘리베이터처럼 읽힌다. 무릎이 천천히 굽기 시작해 끝에서 멎는다.
	const float EasedAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);
	CrouchCameraCompensation = CrouchCameraCompensationStart * EasedAlpha;
	if (!bCameraMotionEnabled && FirstPersonCamera)
	{
		// 연출 중 흔들림을 꺼도 자세 전환의 보정은 끝까지 돌려준다.
		// 그렇지 않으면 일어난 뒤 카메라만 48cm 낮은 곳에 남는다.
		FVector CameraLocation = FirstPersonCamera->GetRelativeLocation();
		CameraLocation.Z += CrouchCameraCompensation - AppliedCrouchCameraCompensation;
		FirstPersonCamera->SetRelativeLocation(CameraLocation);
		AppliedCrouchCameraCompensation = CrouchCameraCompensation;
	}
	if (CrouchTransitionRemaining <= 0.0f)
	{
		CrouchCameraCompensation = 0.0f;
		CrouchCameraCompensationStart = 0.0f;
		RefreshSprintState();
	}
}

void AIGPlayerCharacter::UpdateContextualActions(const float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	const bool bActuallySprinting = bSprinting
		&& !bIsCrouched
		&& MovementComponent
		&& MovementComponent->IsMovingOnGround()
		&& GetVelocity().Size2D() > IGPlayerNoise::ReferenceWalkSpeed + 10.0f;
	if (bActuallySprinting)
	{
		SprintActiveSeconds += DeltaSeconds;
		SprintRecoverySeconds = 0.0f;
	}
	else
	{
		SprintRecoverySeconds += DeltaSeconds;
		if (SprintRecoverySeconds >= 2.0f)
		{
			SprintActiveSeconds = FMath::Max(
				0.0f,
				SprintActiveSeconds - DeltaSeconds * 1.75f);
		}
	}

	if (bListening)
	{
		AActor* FocusedActor = InteractionComponent
			? InteractionComponent->GetFocusedActor()
			: nullptr;
		AIGMissingFloorNightThreeDirector* ListeningDirector = nullptr;
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AIGMissingFloorNightThreeDirector> It(World); It; ++It)
			{
				if (It->IsPlayerListenTarget(FocusedActor))
				{
					ListeningDirector = *It;
					break;
				}
			}
		}
		if (!ListeningDirector)
		{
			EndListen();
		}
		else if (!bListenTriggered)
		{
			ListenHeldSeconds += DeltaSeconds;
			if (ListenHeldSeconds >= IGPlayerNoise::ListenCommitSeconds)
			{
				bListenTriggered = ListeningDirector->TryPlayerListen(
					FocusedActor,
					this);
			}
		}
	}

	if (bHoldingBreath)
	{
		BreathHeldSeconds += DeltaSeconds;
		if (BreathHeldSeconds >= IGPlayerNoise::MaximumBreathHoldSeconds)
		{
			// 4초를 넘겨 터진 숨은 키를 다시 눌러야 참는다. 쥐고만 있으면 0.8초마다
			// 저절로 다시 참게 되어 한계가 없어진다.
			bHoldBreathInputHeld = false;
			FinishHoldBreath(true);
		}
	}
	else if (bHoldBreathInputHeld)
	{
		// 쉬는 틈에 눌러 둔 숨 참기는 틈이 끝나는 대로 이어서 참는다.
		BeginHoldBreath();
	}
}

void AIGPlayerCharacter::UpdateFootsteps(const float DeltaSeconds)
{
	// Footsteps are the player's voice in a game that hunts by sound, so the
	// cadence must not depend on the camera-bob flag the way it used to: a
	// director that never enabled camera motion would leave the player silent
	// and the one upstairs deaf.
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	const float GroundSpeed = GetVelocity().Size2D();
	if (!MovementComponent
		|| !MovementComponent->IsMovingOnGround()
		|| GroundSpeed <= 20.0f)
	{
		return;
	}

	TraveledDistanceAccum += GroundSpeed * DeltaSeconds;
	const int32 StepIndex = FMath::FloorToInt32(TraveledDistanceAccum / StepDistance);
	if (StepIndex == LastStepIndex)
	{
		return;
	}
	LastStepIndex = StepIndex;

	// Absolute speed, not speed normalized against MaxWalkSpeed: the flood
	// director lowers the cap, and a wader must not sound like a stroller
	// merely because the denominator moved with them.
	const float SpeedScale = FMath::Clamp(
		GroundSpeed / IGPlayerNoise::ReferenceWalkSpeed,
		0.0f,
		IGPlayerNoise::SprintSpeed / IGPlayerNoise::ReferenceWalkSpeed);
	PlayFootstep(SpeedScale);
}

void AIGPlayerCharacter::UpdateCameraMotion(const float DeltaSeconds)
{
	if (!bCameraMotionEnabled || !FirstPersonCamera || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	const float GroundSpeed = GetVelocity().Size2D();
	const bool bWalking =
		MovementComponent && MovementComponent->IsMovingOnGround() && GroundSpeed > 20.0f;
	// 자세의 목표 속도로 잰다. MaxWalkSpeed로 나누면 앉은 걸음은 상한이 160인데
	// 분모가 300이라 진폭이 절반이고, 엿듣기 걸음(80)은 걷기 진폭이 통째로
	// 나온다. 발소리(UpdateFootsteps)와 같은 실측 기준이어야 한다.
	const float StanceSpeed = bIsCrouched
		? IGPlayerNoise::CrouchSpeed
		: (bSprinting ? IGPlayerNoise::SprintSpeed : IGPlayerNoise::ReferenceWalkSpeed);
	const float SpeedScale = FMath::Clamp(
		GroundSpeed / FMath::Max(StanceSpeed, 1.0f), 0.0f, 1.0f);
	const bool bReducedMotion = AccessibilitySubsystem
		&& AccessibilitySubsystem->IsReducedCameraMotionEnabled();

	BreathTime += DeltaSeconds;

	FVector TargetOffset = FVector::ZeroVector;
	if (bWalking)
	{
		// 동작 감소에서도 남는다(§18.3). 다른 흔들림은 전부 꺼지지만 이
		// 하나는 화면이 발걸음을 세는 자리라서 줄이기만 한다.
		const float BobScale = bReducedMotion
			? IGPlayerNoise::ReducedMotionBobScale
			: 1.0f;
		// One full sine cycle spans two footsteps (left/right).
		const float StepPhase =
			(TraveledDistanceAccum / StepDistance) * UE_PI;
		TargetOffset.Z += -FMath::Abs(FMath::Sin(StepPhase))
			* GetHeadBobAmplitude() * SpeedScale * BobScale;
		TargetOffset.Y +=
			FMath::Sin(StepPhase) * 0.8f * SpeedScale * BobScale;
	}

	if (!bReducedMotion && !bHoldingBreath)
	{
		// Slow breathing sway; more noticeable while standing still. The rate is
		// the fear model's, so the chest visibly speeds up before the player has
		// worked out why they are frightened.
		const float BreathScale = FMath::Lerp(1.0f, 0.35f, SpeedScale);
		const float BreathsPerMinute =
			StressComponent ? StressComponent->GetBreathsPerMinute() : 13.0f;
		const float BreathHz = BreathsPerMinute / 60.0f;
		// 누적 시간에 바뀐 호흡수를 곱하면 오래 할수록 화면이 크게 튄다.
		// 지금 박자에서 이번 프레임만큼만 이어 간다.
		BreathPhase = FMath::Fmod(
			BreathPhase + DeltaSeconds * 2.0f * UE_PI * BreathHz, 2.0f * UE_PI);
		// 0.55~1.35cm는 서 있기만 해도 화면이 출렁여 멀미로 읽혔다. 공포가
		// 올라올 때 빨라지는 것이 정보지, 깊이가 정보는 아니다.
		const float BreathDepth = StressComponent
			? FMath::Lerp(0.30f, 0.85f, StressComponent->GetStress())
			: 0.30f;
		TargetOffset.Z +=
			FMath::Sin(BreathPhase)
			* BreathDepth
			* BreathScale;
	}

	// Pressing Interact nudges the head forward and down, then springs back.
	if (InteractPunch > KINDA_SMALL_NUMBER)
	{
		if (!bReducedMotion)
		{
			TargetOffset.X += InteractPunch * 2.4f;
			TargetOffset.Z -= InteractPunch * 1.6f;
		}
	}
	InteractPunch = FMath::FInterpTo(InteractPunch, 0.0f, DeltaSeconds, 7.0f);

	// 착지 후 내려앉음. 걸음 흔들림과 달리 동작 감소에서는 줄이지 않고 뺀다.
	if (!bReducedMotion && LandingDip > KINDA_SMALL_NUMBER)
	{
		TargetOffset.Z -= LandingDip;
	}
	LandingDip = FMath::FInterpTo(LandingDip, 0.0f, DeltaSeconds, 9.0f);
	if (!bReducedMotion && HasPhysicalCaptureView())
	{
		// 뒤로 넘어진다. 0.06초 버티다가 가속하며 떨어져 0.36초에 바닥에 닿고,
		// 그동안 뒤로도 조금 밀린다. 괴물은 넘어진 눈높이로 기어 들어온다.
		const float Age = CaptureFeedbackDurationSeconds - CaptureFeedbackRemainingSeconds;
		const float Fall = FMath::Clamp((Age - 0.06f) / 0.30f, 0.0f, 1.0f);
		TargetOffset.Z -= CaptureFallDropCentimeters * Fall * Fall;
		TargetOffset.X -= CaptureFallBackCentimeters * FMath::SmoothStep(0.0f, 0.36f, Age);
	}

	// 보간 없이 그대로 건다. 10/s 보간은 걸음 주파수(1.9~2.9Hz)의 저역 필터라
	// §18.3의 진폭을 걷기 64%, 달리기 47%로 깎아 화면에 냈다. 각 성분은 이미
	// 제 감쇠를 갖고 있어 여기서 한 번 더 부드럽게 할 이유가 없다.
	FirstPersonCamera->SetRelativeLocation(
		CameraBaseLocation + TargetOffset
			+ FVector(0.0f, 0.0f, CrouchCameraCompensation));
	AppliedCrouchCameraCompensation = CrouchCameraCompensation;

	// Fear tremor rides on the camera's own rotation rather than the control
	// rotation, so it shakes the view without fighting the player's aim.
	FRotator CameraRotation = FRotator::ZeroRotator;
	if (!bReducedMotion && StressComponent)
	{
		CameraRotation = StressComponent->GetTremor();
	}
	if (!bReducedMotion && KnockCameraKick > KINDA_SMALL_NUMBER)
	{
		CameraRotation.Pitch -= KnockCameraKick;
	}
	if (!bReducedMotion && ScareCameraKick > KINDA_SMALL_NUMBER)
	{
		CameraRotation.Pitch -= ScareCameraKick;
		CameraRotation.Roll += ScareCameraKick * 0.35f;
	}
	ScareCameraKick = FMath::FInterpTo(ScareCameraKick, 0.0f, DeltaSeconds, 7.0f);
	if (!bReducedMotion && CaptureFeedbackRemainingSeconds > 0.0f && HasPhysicalCaptureView())
	{
		// 넘어지며 옆으로 기울고, 바닥에 닿는 순간 한 번 튄다. 떨림은 부딪힌
		// 순간이 가장 크고 곧 잦아들지만 끊길 때까지 남는다.
		const float Age = CaptureFeedbackDurationSeconds - CaptureFeedbackRemainingSeconds;
		CameraRotation.Roll -= 13.0f * FMath::SmoothStep(0.10f, 0.40f, Age);
		const float Jolt = Age > 0.36f ? FMath::Exp(-(Age - 0.36f) * 14.0f) : 0.0f;
		CameraRotation.Pitch += 4.0f * Jolt;
		CameraRotation.Roll -= 4.5f * Jolt;
		const float Shake = 2.4f * FMath::Exp(-Age * 5.0f) + 0.35f;
		CameraRotation.Pitch += Shake * FMath::Sin(Age * 71.0f);
		CameraRotation.Yaw += Shake * 0.8f * FMath::Sin(Age * 53.0f + 1.3f);
		CameraRotation.Roll += Shake * 0.6f * FMath::Sin(Age * 89.0f + 2.1f);
	}
	else if (!bReducedMotion && CaptureFeedbackRemainingSeconds > 0.0f)
	{
		const float CaptureAlpha = CaptureFeedbackRemainingSeconds
			/ FMath::Max(CaptureFeedbackDurationSeconds, 0.05f);
		const float SmoothedCaptureAlpha = CaptureAlpha * CaptureAlpha
			* (3.0f - 2.0f * CaptureAlpha);
		CameraRotation.Pitch -= IGPlayerNoise::CaptureCameraKickDegrees
			* SmoothedCaptureAlpha;
	}
	KnockCameraKick = FMath::FInterpConstantTo(
		KnockCameraKick,
		0.0f,
		DeltaSeconds,
		IGPlayerNoise::KnockCameraKickDegrees
			/ IGPlayerNoise::KnockCameraReturnSeconds);
	// 컴포넌트 상대 회전은 bUsePawnControlRotation이 GetCameraView에서 폰 제어
	// 회전으로 덮어써 한 번도 화면에 나온 적이 없다. 노크 킥 0.4도도, 포획 킥
	// 3.2도도, 공포 떨림도 전부 여기서 죽어 있었다. 값은 모디파이어가 읽는다.
	if (bReducedMotion)
	{
		FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);
		CameraFeelRotation = FRotator::ZeroRotator;
	}
	else
	{
		CameraFeelRotation = CameraRotation;
	}

	// 달릴 때 시야각이 열린다. 접근성 시야각 위에 얹고, 걸음이 실제로 달리기
	// 속도에 닿았을 때만 연다. 동작 감소에서는 열지 않는다.
	const float TargetFovOffset =
		(!bReducedMotion && bSprinting && bWalking && SpeedScale > 0.85f)
			? IGPlayerNoise::SprintFieldOfViewBoost
			: 0.0f;
	SprintFovOffset = FMath::FInterpTo(SprintFovOffset, TargetFovOffset, DeltaSeconds, 5.0f);
	FirstPersonCamera->SetFieldOfView(BaseFieldOfView + SprintFovOffset);
}

void AIGPlayerCharacter::PlayFootstep(const float SpeedScale)
{
	// Deterministic per-step variation keeps the cadence from sounding looped.
	const uint32 StepHash = static_cast<uint32>(LastStepIndex) * 2654435761u;
	const float PitchVariation = 0.90f + 0.18f * ((StepHash >> 8) & 0xFF) / 255.0f;
	const EIGFootstepSurface Surface = ResolveFootstepSurface();
	if (LastFootstepSurface != Surface)
	{
		LastFootstepSurface = Surface;
		ApplyContextMovementSpeed();
	}
	LastFootstepNoiseLoudness = ResolveFootstepNoiseLoudness(Surface);
	const float AudibleLevel = FMath::Lerp(
		0.42f,
		1.0f,
		FMath::Clamp(LastFootstepNoiseLoudness / 0.72f, 0.0f, 1.0f));
	// 녹음이 있으면 녹음. 표면마다 셋~다섯 벌을 걸음 해시로 고른다. 합성기는
	// 피치를 안에서 걸고, 녹음은 재생 피치로 건다.
	int32 SampleCount = 5;
	const TCHAR* SamplePrefix = IGPlayerNoise::FootstepSamplePrefix(Surface, SampleCount);
	USoundBase* FootSample = IGAudio::SampleVariant(SamplePrefix, SampleCount, StepHash);
	// 발바닥 16cm 위. 앉으면 캡슐이 바닥으로 내려앉으므로 고정 80cm를 빼면
	// 발소리가 슬래브 속에서 나고 바닥에 가려 아래층 소리처럼 먹먹해진다.
	const float FootstepDrop = GetCapsuleComponent()
		? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 16.0f
		: 80.0f;
	IGAudio::SpawnOneShotAt(
		this,
		FootSample
			? FootSample
			: static_cast<USoundBase*>(UIGToneSequenceSoundWave::CreateSurfaceFootstep(
				this,
				Surface,
				PitchVariation,
				1.0f)),
		GetActorLocation() - FVector(0.0f, 0.0f, FootstepDrop),
		FootstepVolume * AudibleLevel * (0.72f + 0.28f * SpeedScale),
		FootSample ? PitchVariation : 1.0f,
		120.0f,
		900.0f,
		EIGAudioBus::Player);

	// The AI receives the exact §21.2 matrix value. Audio gain stays separate,
	// so changing a player's master volume can never change stealth balance.
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(
				GetActorLocation(),
				LastFootstepNoiseLoudness,
				this);
		}
	}

	// §11 V2 분진 퇴적: the fifth floor's settled plaster holds a print. Reported
	// unconditionally — the dust field discards marks that fall outside itself,
	// so this stays ignorant of where dust actually lies. Alternating the print
	// half a shoe left and right of centre makes a walked line read as a pair of
	// tracks rather than a single smeared stripe.
	if (UWorld* World = GetWorld())
	{
		if (UIGDustSubsystem* Dust = World->GetSubsystem<UIGDustSubsystem>())
		{
			const float Yaw = GetActorRotation().Yaw;
			const FVector Side = GetActorRightVector()
				* ((LastStepIndex % 2 == 0) ? 9.0f : -9.0f);
			Dust->ReportSettledPrint(
				GetActorLocation() + Side
					- FVector(0.0f, 0.0f, IGPlayerNoise::FootPrintDropCentimeters),
				Yaw,
				EIGDustPrintKind::Footfall);
		}
	}

	// Every footfall knocks the torch: alternate the kick left/right so the
	// beam walks with the body instead of floating.
	if (Flashlight
		&& Flashlight->IsOn()
		&& (!AccessibilitySubsystem
			|| !AccessibilitySubsystem->IsReducedCameraMotionEnabled()))
	{
		const float Side = (LastStepIndex % 2 == 0) ? 1.0f : -1.0f;
		Flashlight->AddImpulse(
			FRotator(-0.5f * SpeedScale, Side * 0.9f * SpeedScale, 0.0f));
	}
	if (bSprinting && !bIsCrouched)
	{
		PlayHapticFeedback(0.12f, 0.04f);
	}
}

EIGFootstepSurface AIGPlayerCharacter::ResolveFootstepSurface() const
{
	const FVector Location = GetActorLocation();
	if (Location.Z > 1100.0f && Location.Y > 430.0f)
	{
		if (const UWorld* World = GetWorld())
		{
			for (TActorIterator<AIGMissingFloorNightFourDirector> It(World); It; ++It)
			{
				if (It->IsWaterMaskPlaying())
				{
					return EIGFootstepSurface::Water;
				}
			}
		}
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IGFootstepSurface), false, this);
	const UWorld* World = GetWorld();
	if (World && World->LineTraceSingleByChannel(
		Hit,
		Location + FVector(0.0f, 0.0f, 12.0f),
		Location - FVector(0.0f, 0.0f, 145.0f),
		ECC_Visibility,
		QueryParams))
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		if (Component)
		{
			if (Component->ComponentHasTag(IGPlayerNoise::GypsumSurfaceTag))
			{
				return EIGFootstepSurface::GypsumDebris;
			}
			if (Component->ComponentHasTag(IGPlayerNoise::MetalStairSurfaceTag))
			{
				return EIGFootstepSurface::MetalStair;
			}
			if (Component->ComponentHasTag(IGPlayerNoise::RooftopSurfaceTag))
			{
				return EIGFootstepSurface::Rooftop;
			}
			if (Component->ComponentHasTag(IGPlayerNoise::VinylSurfaceTag))
			{
				return EIGFootstepSurface::Vinyl;
			}
		}
	}
	return EIGFootstepSurface::Concrete;
}

float AIGPlayerCharacter::GetSurfaceMovementScale(
	const EIGFootstepSurface Surface) const
{
	if (Surface == EIGFootstepSurface::GypsumDebris)
	{
		return 0.86f;
	}
	if (Surface == EIGFootstepSurface::Water)
	{
		return 0.78f;
	}
	return 1.0f;
}

float AIGPlayerCharacter::ResolveFootstepNoiseLoudness(
	const EIGFootstepSurface Surface) const
{
	struct FSurfaceLoudness
	{
		float Walk;
		float Crouch;
		float Sprint;
	};
	static constexpr FSurfaceLoudness Matrix[] =
	{
		{0.15f, 0.05f, 0.50f}, // vinyl
		{0.17f, 0.05f, 0.55f}, // concrete
		{0.22f, 0.08f, 0.62f}, // metal stair
		{0.14f, 0.05f, 0.48f}, // rooftop membrane
		{0.28f, 0.12f, 0.70f}, // gypsum debris
		{0.30f, 0.14f, 0.72f}  // water
	};
	const int32 Index = FMath::Clamp(
		static_cast<int32>(Surface),
		0,
		UE_ARRAY_COUNT(Matrix) - 1);
	if (bIsCrouched)
	{
		return Matrix[Index].Crouch;
	}
	if (bSprinting)
	{
		const float BreathLoad = FMath::Clamp(
			(SprintActiveSeconds - IGPlayerNoise::SprintBreathThresholdSeconds)
				/ 3.5f,
			0.0f,
			1.0f);
		return FMath::Min(1.0f, Matrix[Index].Sprint + BreathLoad * 0.20f);
	}
	return Matrix[Index].Walk;
}

void AIGPlayerCharacter::RefreshMicrophoneCaptureMode()
{
	const bool bShouldCapture = AccessibilitySubsystem
		&& AccessibilitySubsystem->IsMicrophoneNoiseEnabled();
	if (!bShouldCapture)
	{
		StopMicrophoneCapture();
		bMicrophoneOpenAttempted = false;
		return;
	}
	if (bMicrophoneCaptureRunning || bMicrophoneOpenAttempted)
	{
		return;
	}

	bMicrophoneOpenAttempted = true;
	MicrophoneCaptureSynth = new Audio::FAudioCaptureSynth();
	if (!MicrophoneCaptureSynth->OpenDefaultStream()
		|| !MicrophoneCaptureSynth->StartCapturing())
	{
		delete MicrophoneCaptureSynth;
		MicrophoneCaptureSynth = nullptr;
		AIGHorrorHUD::PushAudioCaption(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"MicrophoneUnavailable",
				"[마이크 입력을 열 수 없음]"),
			2.6f);
		return;
	}

	bMicrophoneCaptureRunning = true;
	MicrophonePollAccumulator = 0.0f;
	MicrophoneNoiseFloor = 0.012f;
	MicrophoneCalibrationRemaining = IGPlayerNoise::MicrophoneCalibrationSeconds;
	MicrophoneReportCooldown = 0.0f;
	MicrophoneScratchSamples.Reserve(4096);
	AIGHorrorHUD::PushAudioCaption(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"MicrophoneEnabled",
			"[마이크 소음 입력 켜짐 · 음성은 저장되지 않음]"),
		2.8f);
}

void AIGPlayerCharacter::UpdateMicrophoneNoise(const float DeltaSeconds)
{
	if (!bMicrophoneCaptureRunning || !MicrophoneCaptureSynth)
	{
		return;
	}
	MicrophoneReportCooldown = FMath::Max(
		0.0f,
		MicrophoneReportCooldown - DeltaSeconds);
	MicrophonePollAccumulator += DeltaSeconds;
	if (MicrophonePollAccumulator < IGPlayerNoise::MicrophonePollSeconds)
	{
		return;
	}
	MicrophonePollAccumulator = 0.0f;
	MicrophoneScratchSamples.Reset();
	if (!MicrophoneCaptureSynth->GetAudioData(MicrophoneScratchSamples)
		|| MicrophoneScratchSamples.IsEmpty())
	{
		return;
	}

	double SquareSum = 0.0;
	float Peak = 0.0f;
	for (const float Sample : MicrophoneScratchSamples)
	{
		const float Absolute = FMath::Abs(Sample);
		Peak = FMath::Max(Peak, Absolute);
		SquareSum += static_cast<double>(Sample) * Sample;
	}
	const float Rms = FMath::Sqrt(
		static_cast<float>(SquareSum / MicrophoneScratchSamples.Num()));
	// Samples have served their only purpose. Release contents before another
	// frame can observe them; no waveform, words or recording leave this scope.
	MicrophoneScratchSamples.Reset();

	if (MicrophoneCalibrationRemaining > 0.0f)
	{
		MicrophoneCalibrationRemaining = FMath::Max(
			0.0f,
			MicrophoneCalibrationRemaining - IGPlayerNoise::MicrophonePollSeconds);
		MicrophoneNoiseFloor = FMath::Lerp(
			MicrophoneNoiseFloor,
			FMath::Min(Rms, 0.08f),
			0.08f);
		return;
	}

	// The moving floor follows fans and device hiss slowly, while an actual
	// voice/impact rises too quickly to calibrate itself out of detection.
	if (Rms < MicrophoneNoiseFloor * 1.45f)
	{
		MicrophoneNoiseFloor = FMath::Lerp(
			MicrophoneNoiseFloor,
			Rms,
			0.015f);
	}
	const float Threshold = FMath::Max(
		IGPlayerNoise::MicrophoneMinimumThreshold,
		MicrophoneNoiseFloor * 2.8f + 0.008f);
	if (MicrophoneReportCooldown > 0.0f
		|| Rms <= Threshold
		|| Peak <= Threshold * 1.20f)
	{
		return;
	}

	const float Detection = FMath::Clamp(
		(Rms - Threshold) / FMath::Max(0.08f, 0.24f - Threshold),
		0.0f,
		1.0f);
	const float Loudness = FMath::Lerp(0.08f, 0.35f, Detection);
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(GetActorLocation(), Loudness, this);
		}
	}
	MicrophoneReportCooldown = IGPlayerNoise::MicrophoneReportCooldownSeconds;
}

void AIGPlayerCharacter::StopMicrophoneCapture()
{
	if (MicrophoneCaptureSynth)
	{
		if (MicrophoneCaptureSynth->IsCapturing())
		{
			MicrophoneCaptureSynth->StopCapturing();
		}
		delete MicrophoneCaptureSynth;
		MicrophoneCaptureSynth = nullptr;
	}
	bMicrophoneCaptureRunning = false;
	MicrophoneScratchSamples.Reset();
	MicrophonePollAccumulator = 0.0f;
	MicrophoneCalibrationRemaining = 0.0f;
	MicrophoneReportCooldown = 0.0f;
}

bool AIGPlayerCharacter::CarryActor(
	AActor* Item,
	const FVector& RelativeOffset,
	const FRotator& RelativeRotation)
{
	if (!Item || !FirstPersonCamera || CarriedActor.IsValid())
	{
		return false;
	}

	const bool bAttached = Item->AttachToComponent(
		FirstPersonCamera,
		FAttachmentTransformRules(
			EAttachmentRule::SnapToTarget,
			EAttachmentRule::SnapToTarget,
			EAttachmentRule::KeepWorld,
			false));
	if (!bAttached)
	{
		return false;
	}
	Item->SetActorRelativeLocation(RelativeOffset);
	Item->SetActorRelativeRotation(RelativeRotation);
	CarriedActor = Item;

	// Cache the rest pose; UpdateCarriedItem animates around it.
	CarriedBaseLocation = RelativeOffset;
	CarriedBaseRotation = RelativeRotation;
	CarrySwayOffset = FRotator::ZeroRotator;
	if (Controller)
	{
		PreviousControlRotation = Controller->GetControlRotation();
	}
	// Carrying something is reason enough to keep the camera alive.
	SetCameraMotionEnabled(true);
	return true;
}

bool AIGPlayerCharacter::ReleaseCarriedActor(AActor* ExpectedItem)
{
	if (!ExpectedItem || CarriedActor.Get() != ExpectedItem)
	{
		return false;
	}

	CarriedActor.Reset();
	CarriedBaseLocation = FVector::ZeroVector;
	CarriedBaseRotation = FRotator::ZeroRotator;
	CarrySwayOffset = FRotator::ZeroRotator;
	bHeavyBagInteractionProxyActive = false;
	HeavyBagRestLocation = FVector::ZeroVector;
	return true;
}

void AIGPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	bool bHasEnhancedMove = false;
	bool bHasEnhancedLook = false;
	bool bHasEnhancedInteract = false;
	bool bHasEnhancedFlashlight = false;
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveInputAction)
		{
			EnhancedInputComponent->BindAction(MoveInputAction, ETriggerEvent::Triggered, this, &AIGPlayerCharacter::Move);
			bHasEnhancedMove = true;
		}

		if (LookInputAction)
		{
			EnhancedInputComponent->BindAction(LookInputAction, ETriggerEvent::Triggered, this, &AIGPlayerCharacter::Look);
			bHasEnhancedLook = true;
		}

		if (FlashlightInputAction)
		{
			EnhancedInputComponent->BindAction(
				FlashlightInputAction,
				ETriggerEvent::Started,
				this,
				&AIGPlayerCharacter::ToggleFlashlight);
			bHasEnhancedFlashlight = true;
		}

		if (InteractInputAction)
		{
			EnhancedInputComponent->BindAction(
				InteractInputAction,
				ETriggerEvent::Started,
				this,
				&AIGPlayerCharacter::BeginInteraction);
			EnhancedInputComponent->BindAction(
				InteractInputAction,
				ETriggerEvent::Completed,
				this,
				&AIGPlayerCharacter::EndInteraction);
			EnhancedInputComponent->BindAction(
				InteractInputAction,
				ETriggerEvent::Canceled,
				this,
				&AIGPlayerCharacter::EndInteraction);
			bHasEnhancedInteract = true;
		}
	}

	// The prototype remains playable before Blueprint input assets exist. Once an
	// Enhanced Input action is assigned, that axis/action replaces its legacy fallback.
	if (!bHasEnhancedMove)
	{
		PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ThisClass::MoveForward);
		PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ThisClass::MoveRight);
	}

	if (!bHasEnhancedLook)
	{
		PlayerInputComponent->BindAxis(TEXT("Turn"), this, &ThisClass::Turn);
		PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ThisClass::LookUp);
	}

	if (!bHasEnhancedInteract)
	{
		PlayerInputComponent->BindAction(TEXT("Interact"), IE_Pressed, this, &ThisClass::BeginInteraction);
		PlayerInputComponent->BindAction(TEXT("Interact"), IE_Released, this, &ThisClass::EndInteraction);
	}

	if (!bHasEnhancedFlashlight)
	{
		PlayerInputComponent->BindAction(TEXT("Flashlight"), IE_Pressed, this, &ThisClass::ToggleFlashlight);
	}

	// These verbs are intentionally independent of Interact. In particular Q/B
	// must never open a door while it is also filing a timed P4 knock.
	PlayerInputComponent->BindAction(
		TEXT("Sprint"), IE_Pressed, this, &ThisClass::BeginSprint);
	PlayerInputComponent->BindAction(
		TEXT("Sprint"), IE_Released, this, &ThisClass::EndSprint);
	PlayerInputComponent->BindAction(
		TEXT("Jump"), IE_Pressed, this, &ThisClass::BeginJump);
	PlayerInputComponent->BindAction(
		TEXT("Jump"), IE_Released, this, &ThisClass::EndJump);
	PlayerInputComponent->BindAction(
		TEXT("Crouch"), IE_Pressed, this, &ThisClass::BeginCrouchInput);
	PlayerInputComponent->BindAction(
		TEXT("Crouch"), IE_Released, this, &ThisClass::EndCrouchInput);
	PlayerInputComponent->BindAction(
		TEXT("Knock"), IE_Pressed, this, &ThisClass::Knock);
	PlayerInputComponent->BindAction(
		TEXT("Listen"), IE_Pressed, this, &ThisClass::BeginListen);
	PlayerInputComponent->BindAction(
		TEXT("Listen"), IE_Released, this, &ThisClass::EndListen);
	PlayerInputComponent->BindAction(
		TEXT("HoldBreath"), IE_Pressed, this, &ThisClass::BeginHoldBreath);
	PlayerInputComponent->BindAction(
		TEXT("HoldBreath"), IE_Released, this, &ThisClass::EndHoldBreath);

	// Save/load remains available even when no Blueprint input asset or front
	// end menu has been authored yet. Autosaves are the only shipped slots.
	PlayerInputComponent->BindAction(
		TEXT("LoadAutosave"),
		IE_Pressed,
		this,
		&ThisClass::LoadLatestAutosave);
}

void AIGPlayerCharacter::BeginSprint()
{
	bSprintInputHeld = true;
	UIGPlayRecordSubsystem::Note(this, TEXT("sprint"));
	RefreshSprintState();
}

void AIGPlayerCharacter::EndSprint()
{
	bSprintInputHeld = false;
	bSprinting = false;
	ApplyContextMovementSpeed();
}

void AIGPlayerCharacter::BeginJump()
{
	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	const UWorld* World = GetWorld();
	if (AIGReadableNote::GetOpenNote() || !MovementComponent || !World || IsInHidingSpot())
	{
		return;
	}
	// 앉은 채로는 뛰지 않는다. 점프 키는 먼저 일어서는 데 쓴다. 머리 위가 막혔으면
	// 일어서기만 걸어 두고, 엔진이 자리가 나는 대로 세운다.
	if (bIsCrouched || MovementComponent->bWantsToCrouch)
	{
		bSprinting = false;
		CrouchTransitionRemaining = IGPlayerNoise::CrouchTransitionSeconds;
		UnCrouch();
		SetCameraMotionEnabled(true);
		ApplyContextMovementSpeed();
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (!MovementComponent->IsMovingOnGround())
	{
		// 걸어서 턱을 내려선 직후는 아직 땅으로 친다. 뛰어서 뜬 몸(JumpCurrentCount 1)은
		// 해당이 없다. 그 밖의 공중 입력은 착지할 때까지 잠깐 기억한다.
		if (MovementComponent->IsFalling()
			&& JumpCurrentCount == 0
			&& bCoyoteJumpReady
			&& Now - LastGroundedSeconds <= IGPlayerNoise::CoyoteSeconds)
		{
			bCoyoteJumpReady = false;
			bSprinting = false;
			ApplyContextMovementSpeed();
			LaunchCharacter(FVector(0.0f, 0.0f, MovementComponent->JumpZVelocity), false, true);
			return;
		}
		JumpBufferedUntilSeconds = Now + IGPlayerNoise::JumpBufferSeconds;
		return;
	}
	// 공중에서는 달리기 속도를 내리되 Shift가 눌려 있다는 사실은 남긴다.
	// 지우면 착지 뒤 IE_Pressed가 다시 오지 않아 걷기로 굳는다.
	bSprinting = false;
	bCoyoteJumpReady = false;
	ApplyContextMovementSpeed();
	Jump();
}

void AIGPlayerCharacter::EndJump()
{
	StopJumping();
}

void AIGPlayerCharacter::Landed(const FHitResult& Hit)
{
	const float ImpactSpeed = FMath::Abs(GetVelocity().Z);
	Super::Landed(Hit);
	// Shift를 쥔 채 뛰어내렸으면 땅에 닿는 순간 다시 달린다.
	RefreshSprintState();
	// 소리가 안 날 만큼 가벼운 착지도 시점은 내려앉는다. 카메라가 되돌아오는
	// 것은 UpdateCameraMotion이 한다.
	LandingDip = FMath::GetMappedRangeValueClamped(
		FVector2D(120.0f, 700.0f),
		FVector2D(
			IGPlayerNoise::LandingDipMinCentimeters,
			IGPlayerNoise::LandingDipMaxCentimeters),
		ImpactSpeed);
	if (ImpactSpeed < 210.0f)
	{
		return;
	}

	const float LandingLoudness = FMath::GetMappedRangeValueClamped(
		FVector2D(210.0f, 700.0f),
		FVector2D(0.14f, 0.58f),
		ImpactSpeed);
	const EIGFootstepSurface Surface = ResolveFootstepSurface();
	// 걸음과 같은 녹음을 낮고 무겁게. 걸음은 녹음인데 착지만 합성이면 같은
	// 바닥이 두 재질로 들린다.
	int32 SampleCount = 5;
	const TCHAR* SamplePrefix = IGPlayerNoise::FootstepSamplePrefix(Surface, SampleCount);
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleVariantOr(
			SamplePrefix,
			SampleCount,
			static_cast<uint32>(LastStepIndex + 977) * 2654435761u,
			[this, Surface]() -> USoundBase*
			{
				return UIGToneSequenceSoundWave::CreateSurfaceFootstep(this, Surface, 0.82f, 0.82f);
			}),
		// 바닥 면 위에서 시작한 오클루전 트레이스는 그 바닥에 걸린다.
		Hit.ImpactPoint + FVector(0.0f, 0.0f, 4.0f),
		FootstepVolume * FMath::Lerp(0.85f, 1.35f, LandingLoudness),
		0.86f,
		120.0f,
		1050.0f,
		EIGAudioBus::Player);
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(GetActorLocation(), LandingLoudness, this);
		}
	}
	PlayHapticFeedback(FMath::Clamp(LandingLoudness * 0.45f, 0.08f, 0.24f), 0.08f);
}

void AIGPlayerCharacter::BeginCrouchInput()
{
	bCrouchInputHeld = true;
	// 숨어 있는 동안 자세는 자리가 정한다. 침대 밑에서 일어서면 캡슐이 침대를 뚫는다.
	if (IsInHidingSpot())
	{
		return;
	}
	const bool bToggleCrouch = !AccessibilitySubsystem
		|| AccessibilitySubsystem->UsesToggleCrouch();
	if (bToggleCrouch)
	{
		ToggleCrouch();
		return;
	}

	bSprinting = false;
	CrouchTransitionRemaining = IGPlayerNoise::CrouchTransitionSeconds;
	Crouch();
	SetCameraMotionEnabled(true);
	ApplyContextMovementSpeed();
}

void AIGPlayerCharacter::EndCrouchInput()
{
	bCrouchInputHeld = false;
	if (!AccessibilitySubsystem || AccessibilitySubsystem->UsesToggleCrouch() || IsInHidingSpot())
	{
		return;
	}

	CrouchTransitionRemaining = IGPlayerNoise::CrouchTransitionSeconds;
	UnCrouch();
	SetCameraMotionEnabled(true);
	ApplyContextMovementSpeed();
}

void AIGPlayerCharacter::ToggleCrouch()
{
	bSprinting = false;
	CrouchTransitionRemaining = IGPlayerNoise::CrouchTransitionSeconds;
	// 앉아 있는지가 아니라 앉으려는지로 뒤집는다. 머리 위가 막혀 못 일어선 사이에
	// 다시 누르면 일어서기를 거둔다. bIsCrouched로 보면 UnCrouch만 거듭 부르다가
	// 자리가 나는 순간 원치 않게 일어섰다.
	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	const bool bWantsCrouch = MovementComponent
		? static_cast<bool>(MovementComponent->bWantsToCrouch)
		: bIsCrouched;
	if (bWantsCrouch)
	{
		UnCrouch();
	}
	else
	{
		Crouch();
	}
	SetCameraMotionEnabled(true);
	ApplyContextMovementSpeed();
}

void AIGPlayerCharacter::Knock()
{
	if (AIGReadableNote::GetOpenNote())
	{
		return;
	}
	const UWorld* CurrentWorld = GetWorld();
	if (!CurrentWorld
		|| CurrentWorld->GetTimeSeconds() < KnockInputLockedUntil)
	{
		return;
	}
	UIGPlayRecordSubsystem::Note(this, TEXT("knock"));
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AIGMissingFloorFifthDawnDirector> It(World); It; ++It)
		{
			if (It->RegisterPlayerKnock())
			{
				ApplyPlayerKnockFeedback();
				return;
			}
		}
		// 엔딩 B의 기다림. 앉은 자리 곁의 벽을 친다 — 어디를 보고 있든 팔 길이
		// 안에 벽이 있다.
		for (TActorIterator<AIGMissingFloorNightFourDirector> It(World); It; ++It)
		{
			if (It->RegisterVigilKnock())
			{
				ApplyPlayerKnockFeedback();
				return;
			}
		}
	}
	if (!InteractionComponent)
	{
		return;
	}
	AActor* FocusedActor = InteractionComponent->GetFocusedActor();
	if (!IsValid(FocusedActor))
	{
		// Nothing under the cursor. §7 P4 teaches 둘-쉬고-하나 and §8 비트 3-7
		// asks her to walk a corridor on it, so the answer cannot require an
		// authored surface to knock on — she knocks on whatever is beside her.
		// The tap costs her position either way: the reply buys a pause and then
		// sends him to the spot the answer came from.
		FVector KnockLocation = GetActorLocation();
		bool bSurfaceInReach = false;
		if (OfferAnswerKnock(GetActorLocation()))
		{
			bSurfaceInReach = true;
		}
		else if (FirstPersonCamera)
		{
			// 그가 없는 낮과 프롤로그에서도 Q는 손이 닿는 벽을 두드린다. 화면 아래
			// 힌트가 첫 프레임부터 「Q 두드리기」를 걸어 두는데 아무 일도 없으면
			// 키가 고장 난 줄 안다. 팔 길이(120cm) 안에 막는 면이 있어야 한다.
			const FVector ViewLocation = FirstPersonCamera->GetComponentLocation();
			const FVector ViewDirection = GetControlRotation().Vector();
			FHitResult Surface;
			FCollisionQueryParams SurfaceParams(SCENE_QUERY_STAT(IGPlayerKnockSurface), false, this);
			if (CurrentWorld->LineTraceSingleByChannel(
					Surface,
					ViewLocation,
					ViewLocation + ViewDirection * 120.0f,
					ECC_Visibility,
					SurfaceParams))
			{
				bSurfaceInReach = true;
				// 벽면 바로 앞 공기에서 난다. 면 위에서 시작한 오클루전 트레이스는
				// 제 벽에 걸려 주먹 소리가 벽 너머 소리처럼 먹먹해진다.
				KnockLocation = Surface.ImpactPoint - ViewDirection * 3.0f;
			}
		}
		if (bSurfaceInReach)
		{
			// 402호 문 안쪽의 노크가 쓰는 것과 같은 석고 녹음이다. 같은 벽을
			// 그는 녹음으로 치고 그녀는 합성으로 치면 두 손이 다른 벽을 친다.
			IGAudio::SpawnOneShotAt(
				this,
				IGAudio::SampleVariantOr(
					TEXT("Knock_Plaster"), 3,
					// 시각을 그대로 쓰면 4초 남짓마다 한 번 바뀌어 연달아 친 노크가 한
					// 녹음을 되풀이한다. 착지 소리처럼 섞어서 탭마다 갈리게 한다.
					static_cast<uint32>(CurrentWorld->GetTimeSeconds() * 977.0f) * 2654435761u,
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.0f); }),
				KnockLocation,
				0.82f,
				1.0f,
				160.0f,
				1400.0f,
				EIGAudioBus::Player);
			if (UIGNoiseSubsystem* Noise =
				GetWorld()->GetSubsystem<UIGNoiseSubsystem>())
			{
				Noise->ReportNoise(
					KnockLocation, AIGPlayerCharacter::KnockLoudness, this);
			}
			ApplyPlayerKnockFeedback();
		}
		return;
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AIGMissingFloorNightThreeDirector> It(World); It; ++It)
		{
			if (It->TryPlayerKnock(FocusedActor, this))
			{
				ApplyPlayerKnockFeedback();
				return;
			}
		}
		const FGameplayTag DoorInteractionTag =
			FGameplayTag::RequestGameplayTag(
				FName(TEXT("Interaction.Door")),
				false);
		if (!InteractionComponent->GetFocusedInteractionTag().MatchesTagExact(
				DoorInteractionTag))
		{
			return;
		}

		// Ordinary doors still answer the verb physically; they simply do not
		// advance a puzzle unless a chapter director owns that surface.
		// 현관문은 강철이다. 석고 소리로 철문을 치면 손이 벽을 친 줄 안다.
		// 소리는 주먹이 닿은 자리에서 난다. 문 액터의 원점은 바닥의 경첩 축이라
		// 거기서 내면 눈높이를 친 노크가 문턱 모서리에서 울린다. 시선이 문짝을
		// 비껴가면 걸쇠 자리를 친 것으로 본다. 소음 보고와 대답 판정은 예전 자리다.
		FVector DoorKnockLocation = FocusedActor->GetActorLocation();
		if (const AIGSwingDoor* KnockedDoor = Cast<AIGSwingDoor>(FocusedActor))
		{
			DoorKnockLocation = KnockedDoor->GetLatchSoundLocation();
		}
		if (FirstPersonCamera)
		{
			const FVector DoorViewLocation = FirstPersonCamera->GetComponentLocation();
			const FVector DoorViewDirection = GetControlRotation().Vector();
			FHitResult DoorFace;
			FCollisionQueryParams DoorFaceParams(SCENE_QUERY_STAT(IGPlayerKnockDoor), false, this);
			if (World->LineTraceSingleByChannel(
					DoorFace,
					DoorViewLocation,
					DoorViewLocation + DoorViewDirection * 250.0f,
					ECC_Visibility,
					DoorFaceParams)
				&& DoorFace.GetActor() == FocusedActor)
			{
				DoorKnockLocation = DoorFace.ImpactPoint - DoorViewDirection * 3.0f;
			}
		}
		IGAudio::SpawnOneShotFromActorAt(
			FocusedActor,
			IGAudio::SampleVariantOr(
				TEXT("Knock_Steel"), 3,
				static_cast<uint32>(World->GetTimeSeconds() * 977.0f) * 2654435761u,
				[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.0f); }),
			DoorKnockLocation,
			0.82f,
			1.0f,
			160.0f,
			1400.0f,
			EIGAudioBus::Player);
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(
				FocusedActor->GetActorLocation(),
				AIGPlayerCharacter::KnockLoudness,
				this);
		}
		// A door is a perfectly good thing to answer on, so the cadence counts
		// here too. Doing it after the noise report keeps the order honest: the
		// building hears the knock, and then he decides what it meant.
		OfferAnswerKnock(FocusedActor->GetActorLocation());
		// 현관문 밖의 손님도 같은 박자를 듣는다. 오빠의 노크면 물러간다.
		for (TActorIterator<AIGNightThreatDirector> It(World); It; ++It)
		{
			It->RegisterPlayerDoorKnock(FocusedActor);
		}
		ApplyPlayerKnockFeedback();
	}
}

bool AIGPlayerCharacter::OfferAnswerKnock(const FVector& Where)
{
	// Recognition only. Whoever called owns the sound, the noise report and the
	// feedback, so a knock is never heard twice for one tap.
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	bool bTaken = false;
	for (TActorIterator<AIGListenerEntity> It(World); It; ++It)
	{
		bTaken = It->TryAnswerKnock(Where) || bTaken;
	}
	return bTaken;
}

void AIGPlayerCharacter::ApplyPlayerKnockFeedback()
{
	InteractPunch = FMath::Max(InteractPunch, 0.45f);
	KnockCameraKick = IGPlayerNoise::KnockCameraKickDegrees;
	SetCameraMotionEnabled(true);
	if (const APlayerController* PlayerController =
		Cast<APlayerController>(Controller))
	{
		if (AIGHorrorHUD* HorrorHUD =
			Cast<AIGHorrorHUD>(PlayerController->GetHUD()))
		{
			HorrorHUD->PlayFirstPersonKnock();
		}
	}
	PlayHapticFeedback(0.35f, 0.06f);
	RegisterKnockSequenceTap();
}

void AIGPlayerCharacter::RegisterKnockSequenceTap()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	// §19.8 인지 지원에서 판정창이 넓어진다. 리듬을 못 맞추는 손에게
	// 「둘, 쉬고, 하나」가 통과할 수 없는 벽이 되면 안 된다.
	const float WindowScale = AccessibilitySubsystem
		? AccessibilitySubsystem->GetKnockWindowScale()
		: 1.0f;
	const float ResetWindow =
		IGPlayerNoise::KnockSequenceResetSeconds * WindowScale;
	if (LastKnockInputSeconds < 0.0
		|| Now - LastKnockInputSeconds > ResetWindow)
	{
		KnockSequenceTapCount = 0;
	}
	LastKnockInputSeconds = Now;
	++KnockSequenceTapCount;
	if (KnockSequenceTapCount >= 3)
	{
		KnockSequenceTapCount = 0;
		KnockInputLockedUntil = Now + IGPlayerNoise::KnockInputLockSeconds;
	}
}

void AIGPlayerCharacter::HandleForeignNoise(const FIGNoiseEvent& Event)
{
	// §19.8 노크 진동 대체. 내가 낸 소리는 이미 손에 오고, 여기서 다루는
	// 것은 건물이 낸 쪽이다.
	if (Event.Instigator.Get() == this || Event.Loudness <= 0.0f)
	{
		return;
	}
	if (!AccessibilitySubsystem
		|| !AccessibilitySubsystem->UsesKnockHapticSubstitute())
	{
		return;
	}
	PlayKnockSubstituteHaptic(Event.Loudness);
}

void AIGPlayerCharacter::PlayKnockSubstituteHaptic(const float Loudness) const
{
	// 소리 크기를 그대로 세기로 옮긴다. 멀리서 난 것과 문 밖의 것이 같은
	// 세기로 오면 대체 채널이 거리를 지워 버린다.
	PlayHapticFeedback(
		FMath::Clamp(Loudness * 0.45f, 0.10f, 0.45f),
		IGPlayerNoise::ImpactHapticSeconds);
}

void AIGPlayerCharacter::PlayImpactHaptic() const
{
	// §18.6 낙하물·충돌. 연출된 충격에만 붙는다 — 플레이어 자신의 착지는
	// 발소리 크기의 작은 펄스이고 표의 이 줄이 아니다.
	PlayHapticFeedback(
		IGPlayerNoise::ImpactHapticIntensity,
		IGPlayerNoise::ImpactHapticSeconds);
}

void AIGPlayerCharacter::PlayHeartbeatHaptic() const
{
	// §18.6 심박. 「0.10 ×2, 박동 동기」 — 럽과 덥에 하나씩이다. 길이는
	// 심박음의 음표 길이를 그대로 쓴다. 따로 적어 두면 소리와 진동이
	// 어긋나도 아무도 모른다.
	PlayHapticFeedback(IGPlayerNoise::HeartbeatHapticIntensity, 0.16f);
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FTimerHandle DubHandle;
	World->GetTimerManager().SetTimer(
		DubHandle,
		FTimerDelegate::CreateWeakLambda(
			const_cast<AIGPlayerCharacter*>(this),
			[this]()
			{
				PlayHapticFeedback(
					IGPlayerNoise::HeartbeatHapticIntensity, 0.13f);
			}),
		0.20f,
		false);
}

void AIGPlayerCharacter::SetChaseHaptic(const bool bActive)
{
	if (bChaseHapticActive == bActive)
	{
		return;
	}
	bChaseHapticActive = bActive;
	if (!bActive)
	{
		StopChaseHaptic();
		return;
	}
	// 페이드인은 Tick이 올린다. 추격이 시작되는 순간 손이 먼저 놀라면
	// 소리보다 진동이 먼저 말해 버린다.
	ChaseHapticAlpha = 0.0f;
	SetActorTickEnabled(true);
}

void AIGPlayerCharacter::StopChaseHaptic()
{
	bChaseHapticActive = false;
	ChaseHapticAlpha = 0.0f;
	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (PlayerController && ChaseForceFeedbackHandle > 0)
	{
		PlayerController->PlayDynamicForceFeedback(
			0.0f, 0.0f, true, true, true, true,
			EDynamicForceFeedbackAction::Stop,
			ChaseForceFeedbackHandle);
		ChaseForceFeedbackHandle = 0;
	}
}

void AIGPlayerCharacter::UpdateChaseHaptic(const float DeltaSeconds)
{
	if (!bChaseHapticActive)
	{
		return;
	}
	if (AccessibilitySubsystem && !AccessibilitySubsystem->AreHapticsEnabled())
	{
		StopChaseHaptic();
		return;
	}
	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (!PlayerController)
	{
		return;
	}
	ChaseHapticAlpha = FMath::Clamp(
		ChaseHapticAlpha
			+ DeltaSeconds / IGPlayerNoise::ChaseHapticFadeInSeconds,
		0.0f,
		1.0f);
	const float Intensity =
		IGPlayerNoise::ChaseHapticIntensity * ChaseHapticAlpha;
	if (ChaseForceFeedbackHandle > 0)
	{
		PlayerController->PlayDynamicForceFeedback(
			Intensity, -1.0f, true, true, true, true,
			EDynamicForceFeedbackAction::Update,
			ChaseForceFeedbackHandle);
		return;
	}
	ChaseForceFeedbackHandle = PlayerController->PlayDynamicForceFeedback(
		Intensity, -1.0f, true, true, true, true,
		EDynamicForceFeedbackAction::Start);
}

void AIGPlayerCharacter::PlayHapticFeedback(
	const float Intensity,
	const float DurationSeconds) const
{
	if ((AccessibilitySubsystem && !AccessibilitySubsystem->AreHapticsEnabled())
		|| Intensity <= 0.0f
		|| DurationSeconds <= 0.0f)
	{
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		PlayerController->PlayDynamicForceFeedback(
			FMath::Clamp(Intensity, 0.0f, 1.0f),
			DurationSeconds,
			false,
			false,
			true,
			true);
	}
}

void AIGPlayerCharacter::BeginListen()
{
	// 메뉴가 떼는 입력을 삼켰다면 이전 넘김이 남아 있을 수 있다. 새로 누르면 새로 판단한다.
	bListenRedirectedToInteraction = false;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AIGMissingFloorFifthDawnDirector> It(World); It; ++It)
		{
			if (It->SetPlayerListening(true))
			{
				if (UIGMissingFloorAudioSubsystem* AudioDirector =
					World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
				{
					AudioDirector->SetPlayerListening(true);
				}
				return;
			}
		}
	}
	if (bListening || !InteractionComponent)
	{
		return;
	}
	AActor* FocusedActor = InteractionComponent->GetFocusedActor();
	if (!IsValid(FocusedActor) || !GetWorld())
	{
		return;
	}

	for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
	{
		if (!It->IsPlayerListenTarget(FocusedActor))
		{
			continue;
		}
		bSprinting = false;
		bListening = true;
		UIGPlayRecordSubsystem::Note(this, TEXT("listen"));
		bListenTriggered = false;
		ListenHeldSeconds = 0.0f;
		ApplyContextMovementSpeed();
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->SetPlayerListening(true);
		}
		// 벽에 귀를 대는 옷과 머리카락. 이 자리의 소리는 이것 하나이고, 듣고
		// 있다는 것은 §21.4대로 세계가 6dB 내려가는 것으로 말한다.
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateClothSettle(this),
			FirstPersonCamera ? FirstPersonCamera->GetComponentLocation() : GetActorLocation(),
			0.42f,
			1.08f,
			60.0f,
			320.0f,
			EIGAudioBus::Player);
		return;
	}

	// 밤3 벽이 아닌 엿듣기 판정(401호 라디오, 402호 문, 저수조 등)은 보통
	// 상호작용 홀드로 듣는다. 패드 화면은 여기서 엿듣기 키를 안내하므로
	// 그 키도 같은 홀드로 넘긴다. 떼면 EndListen이 홀드를 놓는다.
	if (FocusedActor->ActorHasTag(FName(TEXT("MissingFloor.Verb.Listen"))))
	{
		bListenRedirectedToInteraction = true;
		BeginInteraction();
	}
}

void AIGPlayerCharacter::EndListen()
{
	if (bListenRedirectedToInteraction)
	{
		bListenRedirectedToInteraction = false;
		EndInteraction();
		return;
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AIGMissingFloorFifthDawnDirector> It(World); It; ++It)
		{
			if (It->SetPlayerListening(false))
			{
				if (UIGMissingFloorAudioSubsystem* AudioDirector =
					World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
				{
					AudioDirector->SetPlayerListening(false);
				}
				return;
			}
		}
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->SetPlayerListening(false);
		}
	}
	if (!bListening)
	{
		return;
	}
	bListening = false;
	bListenTriggered = false;
	ListenHeldSeconds = 0.0f;
	RefreshSprintState();
}

void AIGPlayerCharacter::BeginHoldBreath()
{
	bHoldBreathInputHeld = true;
	if (bHoldingBreath)
	{
		return;
	}
	// 방금 내쉬었다. 쉬는 틈 동안은 키를 쥐고 있다는 것만 남기고, 틈이 끝나면
	// UpdateContextualActions가 이어서 참게 한다.
	const UWorld* World = GetWorld();
	if (World
		&& World->GetTimeSeconds() - BreathReleasedSeconds
			< IGPlayerNoise::BreathHoldCooldownSeconds)
	{
		return;
	}
	bHoldingBreath = true;
	BreathHeldSeconds = 0.0f;
	UIGPlayRecordSubsystem::Note(this, TEXT("hold_breath"));
	// 설명은 「심박을 4초까지 지운다」였는데 실제로는 카메라 숨 흔들림만
	// 멈췄다. 심장은 계속 뛰고 계속 샜다. 이제 정말로 지운다.
	if (StressComponent)
	{
		StressComponent->BeginBreathHold(IGPlayerNoise::MaximumBreathHoldSeconds);
	}
}

void AIGPlayerCharacter::EndHoldBreath()
{
	bHoldBreathInputHeld = false;
	FinishHoldBreath(false);
}

void AIGPlayerCharacter::FinishHoldBreath(const bool bForcedRelease)
{
	if (!bHoldingBreath)
	{
		return;
	}
	const float HeldSeconds = BreathHeldSeconds;
	bHoldingBreath = false;
	BreathHeldSeconds = 0.0f;
	if (const UWorld* World = GetWorld())
	{
		BreathReleasedSeconds = World->GetTimeSeconds();
	}
	// 1초 넘게 참았으면 놓는 순간 1.5배 반동이 온다(§5.2). 잠깐 탭한 것은 반동 없이.
	if (StressComponent)
	{
		StressComponent->EndBreathHold(HeldSeconds >= 1.0f);
	}

	// The recoil is camera-first so accessibility can suppress it. A forced
	// four-second release also becomes a small real sound for the pursuer.
	if (HeldSeconds >= 1.0f)
	{
		BreathTime += 0.35f;
		InteractPunch = FMath::Max(InteractPunch, bForcedRelease ? 0.34f : 0.20f);
	}
	if (bForcedRelease)
	{
		if (UWorld* World = GetWorld())
		{
			if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
			{
				Noise->ReportNoise(
					GetActorLocation(),
					IGPlayerNoise::ForcedReleaseLoudness,
					this);
			}
		}
	}
}

void AIGPlayerCharacter::LoadLatestAutosave()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		if (const UIGMissingFloorNarrativeSubsystem* Narrative =
			GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>();
			Narrative && Narrative->IsHourSealed())
		{
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT(
					"IGSave",
					"MissingFloorLoadLocked",
					"지금은 되돌릴 수 없어."),
				2.2f);
			return;
		}
	}
	UIGSaveSubsystem* SaveSubsystem = GameInstance
		? GameInstance->GetSubsystem<UIGSaveSubsystem>()
		: nullptr;
	const bool bLoadStarted =
		SaveSubsystem && SaveSubsystem->RequestLoadLatestAutosave();
	AIGHorrorHUD::PushThought(
		this,
		bLoadStarted
			? NSLOCTEXT(
				"IGSave",
				"LoadingLatestAutosave",
				"최근 자동 저장을 불러옵니다.")
			: NSLOCTEXT(
				"IGSave",
				"NoAutosaveAvailable",
				"불러올 자동 저장이 없습니다."),
		2.2f);
}

void AIGPlayerCharacter::Move(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	const FVector2D MovementInput = Value.Get<FVector2D>();
	const FRotator ControlRotation = Controller->GetControlRotation();
	const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);

	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, MovementInput.Y);
	AddMovementInput(RightDirection, MovementInput.X);
}

void AIGPlayerCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookInput = Value.Get<FVector2D>();
	const float Sensitivity = GetLookSensitivity();
	AddControllerYawInput(LookInput.X * Sensitivity);
	AddControllerPitchInput(
		LookInput.Y * Sensitivity * GetVerticalLookScale()
			* (IsLookInverted() ? -1.0f : 1.0f));
}

void AIGPlayerCharacter::MoveForward(const float Value)
{
	if (!Controller || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Value);
}

void AIGPlayerCharacter::MoveRight(const float Value)
{
	if (!Controller || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Value);
}

void AIGPlayerCharacter::RefreshFieldOfView()
{
	if (!FirstPersonCamera)
	{
		return;
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	BaseFieldOfView = Accessibility ? Accessibility->GetFieldOfViewDegrees() : 78.0f;
	// 달리기로 열린 만큼은 그대로 얹는다. 설정을 바꾸는 순간 시야가 튀지 않는다.
	FirstPersonCamera->SetFieldOfView(BaseFieldOfView + SprintFovOffset);
}

void AIGPlayerCharacter::RefreshCameraTexture()
{
	if (CameraSensor)
	{
		CameraSensor->RefreshFromSettings();
	}
}

float AIGPlayerCharacter::GetLookSensitivity() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGInputBindingSubsystem* Controls = GameInstance
		? GameInstance->GetSubsystem<UIGInputBindingSubsystem>()
		: nullptr;
	if (!Controls)
	{
		return 1.0f;
	}
	// Turn/LookUp은 마우스와 스틱을 같은 축으로 받으므로 축만 봐서는 어느
	// 장치인지 알 수 없다. 게임이 이미 들고 있는 장치 판정을 그대로 쓴다.
	return IsUsingGamepadLook()
		? Controls->GetGamepadSensitivity()
		: Controls->GetMouseSensitivity();
}

float AIGPlayerCharacter::GetHeadBobAmplitude() const
{
	// 자세는 앉기가 먼저다. 앉은 채로 달릴 수는 없다.
	if (bIsCrouched)
	{
		return IGPlayerNoise::CrouchBobAmplitude;
	}
	return bSprinting
		? IGPlayerNoise::SprintBobAmplitude
		: IGPlayerNoise::WalkBobAmplitude;
}

float AIGPlayerCharacter::ShapeGamepadLookAxis(const float RawStick)
{
	const float Magnitude = FMath::Abs(RawStick);
	if (Magnitude <= IGPlayerNoise::PadInnerDeadzone)
	{
		return 0.0f;
	}
	const float Normalized = FMath::Clamp(
		(Magnitude - IGPlayerNoise::PadInnerDeadzone)
			/ (IGPlayerNoise::PadOuterDeadzone - IGPlayerNoise::PadInnerDeadzone),
		0.0f,
		1.0f);
	return FMath::Sign(RawStick)
		* FMath::Pow(Normalized, IGPlayerNoise::PadResponseExponent);
}

bool AIGPlayerCharacter::ApplyGamepadLook(const FKey& StickAxis, const bool bYaw)
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	const UWorld* World = GetWorld();
	if (!PlayerController || !PlayerController->PlayerInput || !World)
	{
		return false;
	}
	// Turn/LookUp 축은 마우스와 스틱을 함께 받아서 들어온 값만으로는 스틱을
	// 되돌릴 수 없다. 데드존은 원시 스틱 값에 걸어야 하므로 직접 읽는다.
	const float Shaped = ShapeGamepadLookAxis(
		PlayerController->PlayerInput->GetKeyValue(StickAxis));
	if (FMath::IsNearlyZero(Shaped))
	{
		return true;
	}
	// bEnableLegacyInputScales=False라서 여기 넣는 값이 곧 각도다.
	const float Degrees = Shaped
		* IGPlayerNoise::PadMaximumTurnRateDegrees
		* World->GetDeltaSeconds()
		* GetLookSensitivity();
	if (bYaw)
	{
		AddControllerYawInput(Degrees);
	}
	else
	{
		// 원시 스틱 값을 읽으므로 축 매핑의 배율은 여기 오지 않는다. 스틱을 위로
		// 밀면 +이고, 레거시 배율이 꺼져 있어 +피치가 곧 위를 보는 것이다. 반전은
		// 마우스와 같은 설정을 읽는다 — 패드만 안 뒤집히면 설정이 거짓말이다.
		AddControllerPitchInput(
			Degrees * GetVerticalLookScale() * (IsLookInverted() ? -1.0f : 1.0f));
	}
	return true;
}

void AIGPlayerCharacter::Turn(const float Value)
{
	if (IsUsingGamepadLook())
	{
		ApplyGamepadLook(EKeys::Gamepad_RightX, /*bYaw=*/true);
		return;
	}
	AddControllerYawInput(Value * GetLookSensitivity());
}

void AIGPlayerCharacter::LookUp(const float Value)
{
	if (IsUsingGamepadLook())
	{
		ApplyGamepadLook(EKeys::Gamepad_RightY, /*bYaw=*/false);
		return;
	}
	// 레거시 입력 배율이 꺼져 있어 +피치가 위다. 마우스를 위로 밀면 축 값이
	// +이므로 반전 설정만 부호를 바꾼다.
	AddControllerPitchInput(
		Value * GetLookSensitivity() * GetVerticalLookScale()
			* (IsLookInverted() ? -1.0f : 1.0f));
}

bool AIGPlayerCharacter::IsUsingGamepadLook() const
{
	const AIGPlayerController* IGController =
		Cast<AIGPlayerController>(GetController());
	return IGController && IGController->IsUsingGamepadForHud();
}

bool AIGPlayerCharacter::IsLookInverted() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGInputBindingSubsystem* Controls = GameInstance
		? GameInstance->GetSubsystem<UIGInputBindingSubsystem>()
		: nullptr;
	return Controls && Controls->IsLookInverted();
}

float AIGPlayerCharacter::GetVerticalLookScale() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGInputBindingSubsystem* Controls = GameInstance
		? GameInstance->GetSubsystem<UIGInputBindingSubsystem>()
		: nullptr;
	return Controls ? Controls->GetVerticalLookScale() : 1.0f;
}

void AIGPlayerCharacter::BeginInteraction()
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AIGMissingFloorNightFourDirector> It(World); It; ++It)
		{
			if (It->RequestFailureRetry())
			{
				return;
			}
		}
	}
	// While a note is open the interact key means "put it down", wherever the
	// player happens to be looking. Without this you can walk away from the
	// wall you took the note off and then have no way to close it.
	if (AIGReadableNote* OpenNote = AIGReadableNote::GetOpenNote())
	{
		OpenNote->PlayHandlingSound(false);
		OpenNote->Close();
		return;
	}
	// 숨어 있는 동안 E는 어디를 보고 있든 「나오기」다.
	if (AIGHidingSpot* Spot = HidingSpot.Get())
	{
		Spot->RequestExit();
		return;
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AIGMissingFloorFifthDawnDirector> It(World); It; ++It)
		{
			if (It->SetPlayerListening(true))
			{
				bInteractionRedirectedToInterludeListen = true;
				return;
			}
		}
	}

	if (InteractionComponent)
	{
		AActor* FocusedActor = InteractionComponent->GetFocusedActor();
		if (IsValid(FocusedActor) && GetWorld())
		{
			for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
			{
				// A knock surface advertises Q/B in the HUD; E/A must be inert so
				// one press cannot also count as a door interaction.
				if (It->IsPlayerKnockTarget(FocusedActor))
				{
					return;
				}
				if (It->IsPlayerListenTarget(FocusedActor))
				{
					bInteractionRedirectedToListen = true;
					BeginListen();
					return;
				}
			}
		}

		// Reaching out reads as a small forward dip of the head. 잡을 것이 없어도
		// 손은 나간다 — 아무 반응이 없으면 키가 죽은 줄 안다.
		InteractPunch = FMath::Max(InteractPunch, 0.45f);
		SetCameraMotionEnabled(true);
		if (FocusedActor)
		{
			InteractPunch = 1.0f;
			SetCameraMotionEnabled(true);

			const AIGPickupItem* CarriedPickup =
				Cast<AIGPickupItem>(CarriedActor.Get());
			const bool bProfileCCommitted =
				CarriedPickup
				&& CarriedPickup->PurchaseProfileOnPickup
					== EIGPurchaseProfile::ProfileC2LX2
				&& IGStory::HasState(
					this,
					FGameplayTag::RequestGameplayTag(
						FName(TEXT("State.CH01.Morning.WaterPurchased")),
						false));
			if (bProfileCCommitted && !bHeavyBagInteractionProxyActive)
			{
				// Static-proxy version of setting the 4 kg bag down before
				// using both hands. Input stays live; only the carried prop and
				// concrete-contact foley change state.
				bHeavyBagInteractionProxyActive = true;
				HeavyBagRestLocation = CarriedBaseLocation;
				CarriedBaseLocation += FVector(-8.0f, 2.0f, -48.0f);
				IGAudio::SpawnOneShotAt(
					this,
					UIGToneSequenceSoundWave::CreatePlasticBagSetDown(this),
					GetActorLocation() - FVector(0.0f, 0.0f, 88.0f),
					0.55f,
					1.0f,
					100.0f,
					700.0f,
					EIGAudioBus::Player);
			}
		}

		InteractionComponent->PressInteraction();
	}
}

void AIGPlayerCharacter::EndInteraction()
{
	if (bInteractionRedirectedToInterludeListen)
	{
		bInteractionRedirectedToInterludeListen = false;
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AIGMissingFloorFifthDawnDirector> It(World); It; ++It)
			{
				if (It->SetPlayerListening(false))
				{
					break;
				}
			}
		}
		return;
	}
	if (bInteractionRedirectedToListen)
	{
		bInteractionRedirectedToListen = false;
		EndListen();
		return;
	}
	if (InteractionComponent)
	{
		InteractionComponent->ReleaseInteraction();
	}
	if (bHeavyBagInteractionProxyActive)
	{
		CarriedBaseLocation = HeavyBagRestLocation;
		bHeavyBagInteractionProxyActive = false;
		HeavyBagRestLocation = FVector::ZeroVector;
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreatePlasticBagLift(this),
			GetActorLocation() - FVector(0.0f, 0.0f, 72.0f),
			0.42f,
			1.0f,
			100.0f,
			700.0f,
			EIGAudioBus::Player);
	}
}

