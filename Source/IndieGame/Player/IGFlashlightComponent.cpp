#include "Player/IGFlashlightComponent.h"

#include "IndieGame.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Components/SpotLightComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Player/IGBeamDustComponent.h"
#include "Narrative/IGStoryStateSubsystem.h"
#include "Player/IGHorrorHUD.h"

namespace IGFlashlight
{
	// §9. 새 건전지 두 알로 켜 둘 수 있는 시간. 밤 하나를 내내 켜 두면 모자라고,
	// 필요할 때만 켜면 남는다.
	constexpr float FullChargeSeconds = 720.0f;
	// LED 손전등은 거의 끝까지 밝다가 마지막에 꺾인다. 이 아래부터 어두워진다.
	constexpr float DimmingStartsAt = 0.35f;
	// 다 닳아도 꺼지지 않는다. 발밑과 문손잡이는 보일 만큼 남겨 진행이 막히지 않는다.
	constexpr float EmptyCellFloor = 0.16f;
	// 이 아래부터 깜박인다. 속말도 여기서 한 번.
	constexpr float StutterStartsAt = 0.20f;
	// 꺼 두면 알칼리 전지가 조금 살아난다. 초당 0.2%, 한 번 끌 때마다 최대 5%.
	constexpr float RestRecoveryPerSecond = 0.002f;
	constexpr float RestRecoveryCap = 0.05f;
}

UIGFlashlightComponent::UIGFlashlightComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// The torch is unavailable for most of CH01. Sway and flicker only need a
	// frame update while its beam is visible.
	PrimaryComponentTick.bStartWithTickEnabled = false;

	Beam = CreateDefaultSubobject<USpotLightComponent>(TEXT("FlashlightBeam"));
	Beam->SetupAttachment(this);
	Beam->SetMobility(EComponentMobility::Movable);
	// 싸구려 LED 손전등. 차갑고 희며, 가운데 핫스팟이 좁고 바깥은 부드럽게 죽는다.
	Beam->SetInnerConeAngle(13.0f);
	Beam->SetOuterConeAngle(34.0f);
	Beam->SetAttenuationRadius(2600.0f);
	Beam->SetLightColor(FLinearColor(0.90f, 0.95f, 1.0f));
	Beam->SetSourceRadius(1.4f);
	Beam->SetSoftSourceRadius(3.0f);
	Beam->SetCastShadows(true);
	// 실내 공기가 서 있을 때 원뿔이 보여야 한다. 손전등만 볼류메트릭 그림자를 진다.
	Beam->SetVolumetricScatteringIntensity(2.2f);
	Beam->bCastVolumetricShadow = true;
	Beam->SetVisibility(false);

	Spill = CreateDefaultSubobject<USpotLightComponent>(TEXT("FlashlightSpill"));
	Spill->SetupAttachment(Beam);
	Spill->SetMobility(EComponentMobility::Movable);
	Spill->SetRelativeRotation(FRotator(-12.0f, 0.0f, 0.0f));
	// 같은 광원에서 퍼지는 주변 빛도 벽과 문에 가려져야 한다.
	// 전 방향 점광원 대신 넓은 원뿔 하나로 발밑과 가까운 문틀만 비춘다.
	Spill->SetInnerConeAngle(50.0f);
	Spill->SetOuterConeAngle(78.0f);
	Spill->SetAttenuationRadius(300.0f);
	Spill->SetLightColor(FLinearColor(0.92f, 0.95f, 1.0f));
	Spill->SetSourceRadius(1.4f);
	Spill->SetSoftSourceRadius(3.0f);
	Spill->SetCastShadows(true);
	Spill->SetVisibility(false);

	// Volumetric scattering above already gives the beam a body in the air.
	// The motes are the readable half: they glitter, and they cluster where he
	// has just dragged himself past (§11 V1).
	BeamDust = CreateDefaultSubobject<UIGBeamDustComponent>(TEXT("BeamDust"));
	BeamDust->SetupAttachment(this);
}

void UIGFlashlightComponent::BeginPlay()
{
	Super::BeginPlay();
	PreviousWorldRotation = GetComponentRotation();
	SetComponentTickEnabled(false);
	if (BeamDust && Beam)
	{
		BeamDust->SetBeamCone(Beam->OuterConeAngle);
	}
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			AccessibilitySubsystem =
				GameInstance->GetSubsystem<UIGAccessibilitySubsystem>();
			if (UIGStoryStateSubsystem* Story = GameInstance->GetSubsystem<UIGStoryStateSubsystem>())
			{
				Story->OnStoryStateTagChanged.AddUniqueDynamic(this, &ThisClass::HandleStoryStateChanged);
			}
		}
	}
	RefreshOwnership();
}

FGameplayTag UIGFlashlightComponent::GetOwnershipTag()
{
	return FGameplayTag::RequestGameplayTag(TEXT("State.MissingFloor.HasFlashlight"), false);
}

void UIGFlashlightComponent::RefreshOwnership()
{
	const UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UIGStoryStateSubsystem* Story = Instance ? Instance->GetSubsystem<UIGStoryStateSubsystem>() : nullptr;
	SetAvailable(Story && Story->HasState(GetOwnershipTag()));
}

void UIGFlashlightComponent::HandleStoryStateChanged(FGameplayTag StateTag, bool bAdded)
{
	if (StateTag == GetOwnershipTag())
	{
		RefreshOwnership();
	}
}

void UIGFlashlightComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* Instance = World->GetGameInstance())
		{
			if (UIGStoryStateSubsystem* Story = Instance->GetSubsystem<UIGStoryStateSubsystem>())
			{
				Story->OnStoryStateTagChanged.RemoveDynamic(this, &ThisClass::HandleStoryStateChanged);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool UIGFlashlightComponent::Toggle()
{
	SetOn(!bOn);
	return bOn;
}

void UIGFlashlightComponent::SetOn(const bool bNewOn)
{
	const bool bShouldBeOn = bNewOn && bAvailable;
	if (bOn == bShouldBeOn)
	{
		return;
	}

	bOn = bShouldBeOn;
	UE_LOG(LogIndieGame, Display, TEXT("Flashlight %s (available=%d battery=%.2f)"), bOn ? TEXT("on") : TEXT("off"), bAvailable ? 1 : 0, BatteryFraction);
	Beam->SetVisibility(bOn);
	Spill->SetVisibility(bOn);
	const UWorld* World = GetWorld();
	if (bOn)
	{
		// 꺼 둔 동안 살아난 만큼. 꺼진 동안은 틱이 없어서 켤 때 한 번에 셈한다.
		if (World && SwitchedOffSeconds >= 0.0)
		{
			const float RestedSeconds = static_cast<float>(World->GetTimeSeconds() - SwitchedOffSeconds);
			BatteryFraction = FMath::Min(
				1.0f,
				BatteryFraction + FMath::Min(
					RestedSeconds * IGFlashlight::RestRecoveryPerSecond,
					IGFlashlight::RestRecoveryCap));
		}
		SwitchedOffSeconds = -1.0;
		// 약해진 손전등을 껐다 켜면 주머니의 건전지와 갈아 끼운다.
		if (BatteryFraction < IGFlashlight::StutterStartsAt && SpareBatteries > 0)
		{
			--SpareBatteries;
			BatteryFraction = 1.0f;
			bLowBatteryNoticed = false;
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT("IGMissingFloor", "YudamBatteryChanged", "건전지를 새것으로 갈아 끼웠다."),
				2.6f);
		}
		PreviousWorldRotation = GetComponentRotation();
		Beam->SetIntensity(BeamIntensity * GetCellOutput());
		Spill->SetIntensity(520.0f * GetCellOutput());
		SetComponentTickEnabled(true);
		// Kick the beam so switching on reads as a hand movement.
		if (!AccessibilitySubsystem
			|| !AccessibilitySubsystem->IsReducedCameraMotionEnabled())
		{
			AddImpulse(FRotator(-1.6f, 2.2f, 0.0f));
		}
		return;
	}

	// Do not carry a stale scare impulse into the next switch-on. Keeping the
	// component asleep here removes a permanent per-frame update in CH01.
	SwitchedOffSeconds = World ? World->GetTimeSeconds() : -1.0;
	BrownOutTimer = 0.0f;
	KnockLooseAge = -1.0f;
	SwayOffset = FRotator::ZeroRotator;
	ImpulseOffset = FRotator::ZeroRotator;
	Beam->SetRelativeRotation(FRotator::ZeroRotator);
	if (BeamDust)
	{
		// No beam, no motes. Dust that survived the switch-off would be the one
		// thing visible in a black corridor.
		BeamDust->ClearBeam();
	}
	SetComponentTickEnabled(false);
}

void UIGFlashlightComponent::SetAvailable(const bool bNewAvailable)
{
	bAvailable = bNewAvailable;
	if (!bAvailable)
	{
		SetOn(false);
	}
}

void UIGFlashlightComponent::RefillBattery(const float Fraction)
{
	BatteryFraction = FMath::Clamp(BatteryFraction + Fraction, 0.0f, 1.0f);
	if (BatteryFraction > IGFlashlight::StutterStartsAt)
	{
		bLowBatteryNoticed = false;
	}
	if (bOn)
	{
		Beam->SetIntensity(BeamIntensity * GetCellOutput());
		Spill->SetIntensity(520.0f * GetCellOutput());
	}
}

float UIGFlashlightComponent::GetCellOutput() const
{
	const float Charge = FMath::Clamp(BatteryFraction / IGFlashlight::DimmingStartsAt, 0.0f, 1.0f);
	const float Knee = Charge * Charge * (3.0f - 2.0f * Charge);
	return FMath::Lerp(IGFlashlight::EmptyCellFloor, 1.0f, Knee);
}

void UIGFlashlightComponent::AddImpulse(const FRotator& Impulse)
{
	const bool bReducedMotion = AccessibilitySubsystem
		&& AccessibilitySubsystem->IsReducedCameraMotionEnabled();
	if (!bOn || bReducedMotion)
	{
		return;
	}
	ImpulseOffset += Impulse;
	ImpulseOffset.Pitch = FMath::Clamp(ImpulseOffset.Pitch, -9.0f, 9.0f);
	ImpulseOffset.Yaw = FMath::Clamp(ImpulseOffset.Yaw, -9.0f, 9.0f);
}

void UIGFlashlightComponent::TriggerBrownOut(const float DurationSeconds)
{
	if (AccessibilitySubsystem
		&& AccessibilitySubsystem->IsReducedFlickerEnabled())
	{
		BrownOutTimer = 0.0f;
		return;
	}
	if (bOn)
	{
		BrownOutTimer = FMath::Max(
			BrownOutTimer,
			FMath::Max(0.0f, DurationSeconds));
		AddImpulse(FRotator(2.4f, -3.0f, 0.0f));
	}
}

void UIGFlashlightComponent::PlayKnockLoose()
{
	if (!bOn || (AccessibilitySubsystem
		&& AccessibilitySubsystem->IsReducedCameraMotionEnabled()))
	{
		return;
	}
	KnockLooseAge = 0.0f;
	KnockLooseStartPitch = Beam->GetComponentRotation().Pitch;
	SwayOffset = FRotator::ZeroRotator;
	ImpulseOffset = FRotator::ZeroRotator;
}

void UIGFlashlightComponent::ClearKnockLoose()
{
	if (KnockLooseAge < 0.0f)
	{
		return;
	}
	KnockLooseAge = -1.0f;
	PreviousWorldRotation = GetComponentRotation();
	Beam->SetRelativeRotation(FRotator::ZeroRotator);
}

void UIGFlashlightComponent::TickComponent(
	const float DeltaSeconds,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);

	if (!bOn)
	{
		// Defensive self-healing for state changes made by future callers.
		SetComponentTickEnabled(false);
		return;
	}

	UpdateSway(DeltaSeconds);
	// 켜 둔 만큼 닳는다. 바닥값은 GetCellOutput이 지키므로 0까지 내려가도 된다.
	BatteryFraction = FMath::Max(0.0f, BatteryFraction - DeltaSeconds / IGFlashlight::FullChargeSeconds);
	if (!bLowBatteryNoticed && BatteryFraction < IGFlashlight::StutterStartsAt)
	{
		bLowBatteryNoticed = true;
		AIGHorrorHUD::PushThought(
			this,
			SpareBatteries > 0
				? NSLOCTEXT("IGMissingFloor", "YudamFlashlightLowSpare", "불빛이 약해졌어. 잠깐 끄고 건전지를 갈자.")
				: NSLOCTEXT("IGMissingFloor", "YudamFlashlightLow", "불빛이 약해졌어. 건전지가 다 돼 가나 봐."),
			3.0f);
	}
	float Flicker = SampleFlicker(DeltaSeconds) * GetCellOutput();
	if (KnockLooseAge >= 0.52f)
	{
		// 바닥에 떨어진 손전등은 빛의 절반쯤을 바닥에 빼앗긴다. 닿는 순간에만 한 번
		// 더 어두워진다. 되풀이하면 점멸이 된다.
		const bool bLandingDip = KnockLooseAge < 0.64f
			&& !(AccessibilitySubsystem && AccessibilitySubsystem->IsReducedFlickerEnabled());
		Flicker *= bLandingDip ? 0.35f : 0.5f;
	}
	Beam->SetIntensity(BeamIntensity * Flicker);
	Spill->SetIntensity(520.0f * Flicker);

	if (BeamDust)
	{
		// Drive from the beam, not from this component: the sway offset lives on
		// the light, and the dust has to hang in the cone that is actually lit.
		const FTransform BeamTransform = Beam->GetComponentTransform();
		BeamDust->UpdateBeam(
			BeamTransform.GetLocation(),
			BeamTransform.GetUnitAxis(EAxis::X),
			Flicker);
	}
}

void UIGFlashlightComponent::UpdateSway(const float DeltaSeconds)
{
	if (DeltaSeconds <= SMALL_NUMBER)
	{
		return;
	}
	if (KnockLooseAge >= 0.0f)
	{
		// 손을 떠난 빛은 시선을 따라오지 않는다. 0.16초 만에 천장으로 튀어 천장을
		// 옆으로 쓸고, 0.5초에 옆 바닥으로 떨어진 뒤 조금 튀다 멈춘다. 높이는 시선과
		// 상관없이 실제 천장과 바닥을 기준으로 잡는다. 시선을 기준으로 하면 바닥의
		// 괴물을 내려다보는 동안 "천장"이 괴물 머리가 되어 하얗게 타 버렸다. 몸은 떨어진
		// 빛이 번진 가장자리에서만 보인다.
		KnockLooseAge += DeltaSeconds;
		const float Age = KnockLooseAge;
		float WorldPitch = 0.0f;
		float YawOffset = 0.0f;
		if (Age < 0.16f)
		{
			const float Rise = FMath::InterpEaseOut(0.0f, 1.0f, Age / 0.16f, 2.0f);
			WorldPitch = FMath::Lerp(KnockLooseStartPitch, 55.0f, Rise);
			YawOffset = 30.0f * Rise;
		}
		else if (Age < 0.30f)
		{
			const float Sweep = FMath::SmoothStep(0.16f, 0.30f, Age);
			WorldPitch = FMath::Lerp(55.0f, 45.0f, Sweep);
			YawOffset = FMath::Lerp(30.0f, -48.0f, Sweep);
		}
		else if (Age < 0.50f)
		{
			WorldPitch = FMath::Lerp(45.0f, -55.0f, FMath::SmoothStep(0.30f, 0.50f, Age));
			YawOffset = -48.0f;
		}
		else
		{
			const float Since = Age - 0.50f;
			const float Settle = FMath::Exp(-Since * 9.0f);
			WorldPitch = -55.0f + 7.0f * Settle * FMath::Sin(Since * 38.0f);
			YawOffset = -48.0f + 3.0f * Settle * FMath::Sin(Since * 31.0f);
		}
		PreviousWorldRotation = GetComponentRotation();
		Beam->SetWorldRotation(FRotator(WorldPitch, PreviousWorldRotation.Yaw + YawOffset, 0.0f));
		return;
	}

	// 프레임당 회전량 대신 초당 회전량을 쓴다. 같은 속도로 고개를 돌리면
	// 30fps와 120fps에서도 빛이 같은 만큼 뒤따라와야 한다.
	const FRotator CurrentRotation = GetComponentRotation();
	const FRotator ViewDelta = (CurrentRotation - PreviousWorldRotation).GetNormalized();
	PreviousWorldRotation = CurrentRotation;
	constexpr float LagSeconds = 1.4f / 60.0f;
	const float LagScale = LagSeconds / DeltaSeconds;

	const bool bReducedMotion = AccessibilitySubsystem
		&& AccessibilitySubsystem->IsReducedCameraMotionEnabled();
	const FRotator TargetSway = bReducedMotion
		? FRotator::ZeroRotator
		: FRotator(
			FMath::Clamp(-ViewDelta.Pitch * LagScale, -6.0f, 6.0f),
			FMath::Clamp(-ViewDelta.Yaw * LagScale, -7.0f, 7.0f),
			0.0f);
	SwayOffset = FMath::Lerp(
		SwayOffset, TargetSway, 1.0f - FMath::Exp(-SwayFollowSpeed * DeltaSeconds));
	if (bReducedMotion)
	{
		ImpulseOffset = FRotator::ZeroRotator;
	}
	ImpulseOffset = FMath::Lerp(
		ImpulseOffset, FRotator::ZeroRotator, 1.0f - FMath::Exp(-4.5f * DeltaSeconds));

	Beam->SetRelativeRotation(SwayOffset + ImpulseOffset);
}

float UIGFlashlightComponent::SampleFlicker(const float DeltaSeconds)
{
	FlickerTime += DeltaSeconds;
	if (AccessibilitySubsystem
		&& AccessibilitySubsystem->IsReducedFlickerEnabled())
	{
		BrownOutTimer = 0.0f;
		FlickerValue = 1.0f;
		return FlickerValue;
	}

	// Hash-based value noise: deterministic, no allocation, and cheap enough
	// to run every frame. Two octaves give a ripple plus a slower wander.
	auto Hash01 = [this](const int32 Step)
	{
		uint32 Value = static_cast<uint32>(Step) * 2654435761u + NoiseCounter;
		Value ^= Value >> 15;
		Value *= 2246822519u;
		Value ^= Value >> 13;
		return (Value & 0xFFFF) / 65535.0f;
	};

	const float FastStep = FlickerTime * 18.0f;
	const float SlowStep = FlickerTime * 2.4f;
	const float Fast = FMath::Lerp(
		Hash01(FMath::FloorToInt(FastStep)),
		Hash01(FMath::FloorToInt(FastStep) + 1),
		FMath::Frac(FastStep));
	const float Slow = FMath::Lerp(
		Hash01(FMath::FloorToInt(SlowStep) + 7919),
		Hash01(FMath::FloorToInt(SlowStep) + 7920),
		FMath::Frac(SlowStep));

	// 싸구려 손전등의 잔떨림. 건전지가 20% 아래로 내려가면 떨림이 커지고 끊김이 잦아진다.
	const float Dying = 1.0f - FMath::Clamp(BatteryFraction / IGFlashlight::StutterStartsAt, 0.0f, 1.0f);
	const float Weakness = 0.18f + 0.45f * Dying;
	const float Ripple = 1.0f - (0.03f + 0.30f * Weakness) * (0.6f * Fast + 0.4f * Slow);

	// Very rare, short brown-outs are presentation-only and always recover.
	// 다 닳아 가는 건전지는 2초에 한 번꼴로 끊긴다. 끊겨도 늘 돌아온다.
	if (BrownOutTimer > 0.0f)
	{
		BrownOutTimer -= DeltaSeconds;
		return Ripple * 0.12f;
	}
	if (Fast > 0.997f - 0.03f * Dying && Slow < 0.22f + 0.5f * Dying)
	{
		BrownOutTimer = 0.05f + (0.10f + 0.12f * Dying) * Slow;
	}
	FlickerValue = Ripple;
	return FlickerValue;
}
