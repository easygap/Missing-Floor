#include "Interaction/IGHidingSpot.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Entity/IGNoiseSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGPlayerController.h"

namespace IGHiding
{
	// §4. 들고 나는 데 걸리는 시간. 이 동안만 시선이 걸린다.
	constexpr float TransitionSeconds = 0.45f;
	// 장롱 문짝. 달리다 뛰어들면 두 배로 삐걱인다.
	constexpr float DoorLoudness = 0.12f;
	constexpr float HurriedDoorLoudness = 0.25f;
	// 침대 밑은 문짝이 없다. 바닥에 몸이 끌리는 소리만 난다.
	constexpr float UnderBedLoudness = 0.06f;
	// 안에서 낸 소리가 밖으로 나가는 비율.
	constexpr float MuffleScale = 0.55f;
	// 이만큼 빨리 움직이던 중이면 서두른 것으로 친다(걷기 300보다 빠르다).
	constexpr float HurriedSpeed = 340.0f;
	// 숨어 있는 몸이 이보다 멀리 옮겨졌으면 다른 연출이 데려간 것이다.
	constexpr float ExternalMoveTolerance = 60.0f;
	// 숨은 사람의 시선은 가구 앞면에서 이만큼 바깥에서 잰다.
	constexpr float PeekOutsideFront = 6.0f;
	const FName LookLockReason(TEXT("HidingSpot"));
}

AIGHidingSpot::AIGHidingSpot()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SpotRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SpotRoot"));
	SetRootComponent(SpotRoot);

	// 시선 추적만 받는 얇은 상자. 사람과 카메라는 그대로 지나간다.
	FocusBox = CreateDefaultSubobject<UBoxComponent>(TEXT("FocusBox"));
	FocusBox->SetupAttachment(SpotRoot);
	FocusBox->SetBoxExtent(FVector(2.0f, 40.0f, 80.0f));
	FocusBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FocusBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	FocusBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	FocusBox->SetCanEverAffectNavigation(false);
	FocusBox->SetGenerateOverlapEvents(false);

	InteractionPrompt = NSLOCTEXT("IGMissingFloor", "HideWardrobePrompt", "장롱에 숨기");
}

void AIGHidingSpot::Configure(
	const EIGHidingView InView,
	const FVector& InEyeLocal,
	const FVector& InExitLocal,
	const FVector& InFocusCenterLocal,
	const FVector& InFocusExtent,
	const float InYawLimitDegrees,
	const float InPitchLimitDegrees,
	const bool bInRequiresCrouch)
{
	View = InView;
	EyeLocal = InEyeLocal;
	ExitLocal = InExitLocal;
	YawLimitDegrees = FMath::Clamp(InYawLimitDegrees, 5.0f, 80.0f);
	PitchLimitDegrees = FMath::Clamp(InPitchLimitDegrees, 5.0f, 60.0f);
	bRequiresCrouch = bInRequiresCrouch;
	FocusBox->SetRelativeLocation(InFocusCenterLocal);
	FocusBox->SetBoxExtent(InFocusExtent);
	InteractionPrompt = View == EIGHidingView::UnderBed
		? NSLOCTEXT("IGMissingFloor", "HideUnderBedPrompt", "침대 밑에 숨기")
		: NSLOCTEXT("IGMissingFloor", "HideWardrobePrompt", "장롱에 숨기");
}

bool AIGHidingSpot::CanInteract_Implementation(AActor* Interactor) const
{
	if (!Super::CanInteract_Implementation(Interactor))
	{
		return false;
	}
	const AIGPlayerCharacter* Player = Cast<AIGPlayerCharacter>(Interactor);
	if (!Player)
	{
		return false;
	}
	// 안에 있는 사람에게는 「나오기」로 보인다.
	if (Phase == EPhase::Hidden)
	{
		return Occupant.Get() == Player;
	}
	return Phase == EPhase::Idle && !Player->IsInHidingSpot();
}

FText AIGHidingSpot::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	if (Phase == EPhase::Hidden)
	{
		return NSLOCTEXT("IGMissingFloor", "HideExitPrompt", "나오기");
	}
	return Super::GetInteractionPrompt_Implementation(Interactor);
}

void AIGHidingSpot::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	Super::CompleteInteraction_Implementation(Context);
	AIGPlayerCharacter* Player = Cast<AIGPlayerCharacter>(Context.Interactor);
	if (!Player)
	{
		return;
	}
	if (Phase == EPhase::Hidden && Occupant.Get() == Player)
	{
		RequestExit();
		return;
	}
	if (Phase == EPhase::Idle && !Player->IsInHidingSpot())
	{
		BeginEnter(Player);
	}
}

void AIGHidingSpot::BeginEnter(AIGPlayerCharacter* Player)
{
	Occupant = Player;
	Player->SetHidingSpot(this);
	bHurriedEntry = Player->GetVelocity().Size2D() >= IGHiding::HurriedSpeed;
	PhaseSeconds = 0.0f;
	bCrouchedForEntry = false;
	SetActorTickEnabled(true);
	// 침대 밑은 앉아야 들어간다. 선 채로 눌렀으면 먼저 앉히고, 엔진이 앉힌
	// 다음 프레임부터 미끄러져 들어간다. 이동을 끈 뒤에는 앉지 못한다.
	if (bRequiresCrouch && !Player->bIsCrouched)
	{
		Player->Crouch();
		bCrouchedForEntry = true;
		Phase = EPhase::Crouching;
		return;
	}
	StartEnterTransition();
}

void AIGHidingSpot::StartEnterTransition()
{
	AIGPlayerCharacter* Player = Occupant.Get();
	if (!Player)
	{
		ForceExit();
		return;
	}
	Phase = EPhase::Entering;
	PhaseSeconds = 0.0f;
	Player->SetActorEnableCollision(false);
	if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	TransitionFrom = Player->GetActorLocation();
	LastPlacedLocation = TransitionFrom;
	TransitionTo = GetHiddenBodyLocation(*Player);
	LiftFrom = Player->GetHidingCameraLift();
	LiftTo = GetHiddenCameraLift(*Player, TransitionTo);
	if (const AController* Controller = Player->GetController())
	{
		RotationFrom = Controller->GetControlRotation();
	}
	RotationTo = FRotator(0.0f, GetActorRotation().Yaw, 0.0f);
	SetLookLocked(true);
	PlaySpotSound(true);
	ReportSpotNoise(View == EIGHidingView::UnderBed
		? IGHiding::UnderBedLoudness
		: (bHurriedEntry ? IGHiding::HurriedDoorLoudness : IGHiding::DoorLoudness));
}

void AIGHidingSpot::FinishEnter()
{
	AIGPlayerCharacter* Player = Occupant.Get();
	if (!Player)
	{
		ForceExit();
		return;
	}
	Phase = EPhase::Hidden;
	PhaseSeconds = 0.0f;
	PlaceOccupant(*Player, TransitionTo);
	Player->SetHidingCameraLift(LiftTo);
	if (AController* Controller = Player->GetController())
	{
		Controller->SetControlRotation(RotationTo);
	}
	SetLookLocked(false);
	SetViewLimited(true);
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->SetInstigatorMuffle(Player, IGHiding::MuffleScale);
		}
	}
}

void AIGHidingSpot::RequestExit()
{
	AIGPlayerCharacter* Player = Occupant.Get();
	if (Phase != EPhase::Hidden || !Player)
	{
		return;
	}
	Phase = EPhase::Exiting;
	PhaseSeconds = 0.0f;
	SetViewLimited(false);
	SetLookLocked(true);
	// 문을 여는 소리는 바깥에서 난다. 줄이지 않는다.
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->SetInstigatorMuffle(Player, 1.0f);
		}
	}
	TransitionFrom = Player->GetActorLocation();
	LastPlacedLocation = TransitionFrom;
	TransitionTo = GetExitWorldLocation(*Player);
	LiftFrom = Player->GetHidingCameraLift();
	LiftTo = 0.0f;
	if (const AController* Controller = Player->GetController())
	{
		RotationFrom = Controller->GetControlRotation();
	}
	RotationTo = FRotator(0.0f, GetActorRotation().Yaw, 0.0f);
	PlaySpotSound(false);
	ReportSpotNoise(View == EIGHidingView::UnderBed ? IGHiding::UnderBedLoudness : IGHiding::DoorLoudness);
}

void AIGHidingSpot::FinishExit(const bool bPlaceAtExit)
{
	AIGPlayerCharacter* Player = Occupant.Get();
	// 앉기를 기다리다 끝났으면 가구 안에 들어간 적이 없다. 몸을 옮기지 않는다.
	const bool bWasInside = IsOccupantConcealed();
	const bool bUndoCrouch = bCrouchedForEntry && !bWasInside;
	Phase = EPhase::Idle;
	PhaseSeconds = 0.0f;
	bCrouchedForEntry = false;
	SetActorTickEnabled(false);
	SetViewLimited(false);
	SetLookLocked(false);
	Occupant.Reset();
	if (!Player)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->SetInstigatorMuffle(Player, 1.0f);
		}
	}
	Player->SetHidingCameraLift(0.0f);
	Player->SetActorEnableCollision(true);
	if (bUndoCrouch)
	{
		// 들어가려고 앉혔는데 못 들어갔다. 앉으려던 마음을 거두지 않으면 나중에 머리
		// 위가 비는 순간 저절로 앉는다.
		Player->UnCrouch();
	}
	if (bPlaceAtExit && bWasInside)
	{
		// 나오는 자리에 무엇이 서 있으면 TeleportTo가 가까운 빈자리를 찾는다. 그마저
		// 없으면 그 자리에 두고 이동 컴포넌트가 밀어낸다.
		const FVector ExitWorld = GetExitWorldLocation(*Player);
		if (!Player->TeleportTo(ExitWorld, FRotator(0.0f, GetActorRotation().Yaw, 0.0f)))
		{
			Player->SetActorLocation(ExitWorld, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	// 이동은 이 자리가 들어갈 때 껐다. 끈 쪽이 되살린다.
	if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
		bWasInside && Movement && Movement->MovementMode == MOVE_None)
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	Player->SetHidingSpot(nullptr);
}

void AIGHidingSpot::ForceExit()
{
	if (Phase == EPhase::Idle && !Occupant.IsValid())
	{
		return;
	}
	FinishExit(true);
}

void AIGHidingSpot::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AIGPlayerCharacter* Player = Occupant.Get();
	if (!Player)
	{
		FinishExit(false);
		return;
	}
	PhaseSeconds += DeltaSeconds;
	switch (Phase)
	{
	case EPhase::Crouching:
		if (Player->bIsCrouched)
		{
			StartEnterTransition();
		}
		else if (PhaseSeconds > 0.5f)
		{
			// 앉지 못했다(머리 위가 이미 막혔거나 이동이 꺼져 있다). 들어가지 않는다.
			FinishExit(false);
		}
		break;

	case EPhase::Entering:
	case EPhase::Exiting:
	{
		// 드나드는 사이에 다른 연출이 몸을 옮겼거나(포획 리셋, 기상 자리) 이동을
		// 되살렸으면 그쪽을 따른다. 그대로 보간하면 옮겨 놓은 몸을 다시 끌고 온다.
		const UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
		const bool bMovedAway = FVector::DistSquared(Player->GetActorLocation(), LastPlacedLocation)
			> FMath::Square(IGHiding::ExternalMoveTolerance);
		if (bMovedAway || (Movement && Movement->MovementMode != MOVE_None))
		{
			FinishExit(!bMovedAway);
			break;
		}
		const float Alpha = FMath::Clamp(PhaseSeconds / IGHiding::TransitionSeconds, 0.0f, 1.0f);
		const float Eased = Alpha * Alpha * (3.0f - 2.0f * Alpha);
		PlaceOccupant(*Player, FMath::Lerp(TransitionFrom, TransitionTo, Eased));
		Player->SetHidingCameraLift(FMath::Lerp(LiftFrom, LiftTo, Eased));
		if (AController* Controller = Player->GetController())
		{
			// 짧은 쪽으로 돈다. 성분별 보간은 179도에서 -179도로 가며 한 바퀴를 돈다.
			const FRotator Turn = (RotationTo - RotationFrom).GetNormalized();
			Controller->SetControlRotation(RotationFrom + Turn * Eased);
		}
		if (Alpha >= 1.0f)
		{
			if (Phase == EPhase::Entering)
			{
				FinishEnter();
			}
			else
			{
				FinishExit(true);
			}
		}
		break;
	}

	case EPhase::Hidden:
	{
		// 숨어 있는 동안 다른 연출이 몸을 옮기거나 이동을 되살렸으면(포획 리셋, 장면
		// 전환) 그쪽을 따른다. 충돌이 꺼진 채 걷게 두면 바닥을 뚫고 떨어진다.
		// 옮겨 갔으면 그 자리에 둔다. 제자리에서 이동만 되살아났으면 가구 속에서
		// 충돌이 켜지지 않게 나오는 자리로 꺼낸다.
		const UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
		const bool bMovedAway = FVector::DistSquared(Player->GetActorLocation(), TransitionTo)
			> FMath::Square(IGHiding::ExternalMoveTolerance);
		if (bMovedAway || (Movement && Movement->MovementMode != MOVE_None))
		{
			FinishExit(!bMovedAway);
		}
		break;
	}

	default:
		break;
	}
}

float AIGHidingSpot::GetMaskAlpha() const
{
	const float Alpha = FMath::Clamp(PhaseSeconds / IGHiding::TransitionSeconds, 0.0f, 1.0f);
	switch (Phase)
	{
	case EPhase::Entering: return Alpha;
	case EPhase::Hidden: return 1.0f;
	case EPhase::Exiting: return 1.0f - Alpha;
	default: return 0.0f;
	}
}

float AIGHidingSpot::GetPeekYawAlpha() const
{
	const AIGPlayerCharacter* Player = Occupant.Get();
	const AController* Controller = Player ? Player->GetController() : nullptr;
	if (Phase != EPhase::Hidden || !Controller)
	{
		return 0.0f;
	}
	const float Delta = FRotator::NormalizeAxis(Controller->GetControlRotation().Yaw - GetActorRotation().Yaw);
	return FMath::Clamp(Delta / YawLimitDegrees, -1.0f, 1.0f);
}

void AIGHidingSpot::SetLookLocked(const bool bLocked)
{
	if (bLookLocked == bLocked)
	{
		return;
	}
	if (bLocked)
	{
		const AIGPlayerCharacter* Player = Occupant.Get();
		APlayerController* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
		if (!Controller)
		{
			return;
		}
		// 컨트롤러의 무시 카운터를 직접 올리면 다른 잠금이 바뀔 때 0으로 돌아간다.
		// 이름 붙은 잠금으로 건다.
		if (AIGPlayerController* Owned = Cast<AIGPlayerController>(Controller))
		{
			Owned->AddInputLock(IGHiding::LookLockReason, false, true);
		}
		else
		{
			Controller->SetIgnoreLookInput(true);
		}
		LockedController = Controller;
		bLookLocked = true;
		return;
	}
	// 몸이 먼저 사라졌어도 잠근 컨트롤러는 남아 있다. 그쪽에서 푼다.
	bLookLocked = false;
	APlayerController* Controller = LockedController.Get();
	LockedController.Reset();
	if (AIGPlayerController* Owned = Cast<AIGPlayerController>(Controller))
	{
		Owned->RemoveInputLock(IGHiding::LookLockReason);
	}
	else if (Controller)
	{
		Controller->SetIgnoreLookInput(false);
	}
}

void AIGHidingSpot::SetViewLimited(const bool bLimited)
{
	if (bViewLimited == bLimited)
	{
		return;
	}
	// 거는 쪽은 지금 몸의 컨트롤러를, 푸는 쪽은 걸 때 기억해 둔 컨트롤러를 쓴다.
	// 몸이 먼저 사라져도 시야 제한이 남지 않는다.
	const AIGPlayerCharacter* Player = Occupant.Get();
	APlayerController* Controller = bLimited
		? (Player ? Cast<APlayerController>(Player->GetController()) : nullptr)
		: LimitedController.Get();
	APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
	if (!Camera)
	{
		bViewLimited = false;
		LimitedController.Reset();
		return;
	}
	bViewLimited = bLimited;
	LimitedController = bLimited ? Controller : nullptr;
	if (bLimited)
	{
		SavedYawMin = Camera->ViewYawMin;
		SavedYawMax = Camera->ViewYawMax;
		SavedPitchMin = Camera->ViewPitchMin;
		SavedPitchMax = Camera->ViewPitchMax;
		const float Facing = GetActorRotation().Yaw;
		Camera->ViewYawMin = FRotator::ClampAxis(Facing - YawLimitDegrees);
		Camera->ViewYawMax = FRotator::ClampAxis(Facing + YawLimitDegrees);
		Camera->ViewPitchMin = -PitchLimitDegrees;
		Camera->ViewPitchMax = PitchLimitDegrees;
		return;
	}
	Camera->ViewYawMin = SavedYawMin;
	Camera->ViewYawMax = SavedYawMax;
	Camera->ViewPitchMin = SavedPitchMin;
	Camera->ViewPitchMax = SavedPitchMax;
}

void AIGHidingSpot::ReportSpotNoise(const float Loudness)
{
	UWorld* World = GetWorld();
	UIGNoiseSubsystem* Noise = World ? World->GetSubsystem<UIGNoiseSubsystem>() : nullptr;
	if (Noise && Occupant.IsValid())
	{
		Noise->ReportNoise(GetActorLocation(), Loudness, Occupant.Get());
	}
}

void AIGHidingSpot::PlaySpotSound(const bool bEntering)
{
	const FVector Where = GetActorTransform().TransformPosition(EyeLocal);
	if (View == EIGHidingView::UnderBed)
	{
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateClothSettle(this),
			Where,
			0.42f,
			bEntering ? 0.92f : 1.0f,
			80.0f,
			600.0f,
			EIGAudioBus::Player);
		return;
	}
	// 들어갈 때는 문을 안에서 당겨 닫고, 나올 때는 밀어 연다.
	const float Volume = bHurriedEntry && bEntering ? 0.62f : 0.42f;
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleVariantOr(
			TEXT("Door_Creak"),
			2,
			bEntering ? 1u : 2u,
			[this, bEntering]() -> USoundBase*
			{
				return UIGToneSequenceSoundWave::CreateDoorCreak(this, bEntering);
			}),
		Where,
		Volume,
		bEntering ? 1.18f : 1.08f,
		90.0f,
		900.0f,
		EIGAudioBus::World);
}

FVector AIGHidingSpot::GetHiddenBodyLocation(const AIGPlayerCharacter& Player) const
{
	// 몸은 눈 바로 아래, 이 자리가 놓인 바닥에 선다. 눈높이는 카메라만 내려 맞춘다.
	// 몸째 눈에 맞춰 내리면 침대 밑에서는 바닥 아래로 가라앉고, 「집 안에 있나」
	// 「몇 층에 있나」 같은 위치 판정이 모두 틀어진다. 충돌은 꺼 두었으므로 가구와
	// 겹쳐도 끼지 않는다.
	const FVector EyeWorld = GetActorTransform().TransformPosition(EyeLocal);
	const FVector CameraBase = FRotator(0.0f, GetActorRotation().Yaw, 0.0f)
		.RotateVector(Player.GetCameraBaseLocation());
	const UCapsuleComponent* Capsule = Player.GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.0f;
	return FVector(EyeWorld.X - CameraBase.X, EyeWorld.Y - CameraBase.Y, GetActorLocation().Z + HalfHeight);
}

float AIGHidingSpot::GetHiddenCameraLift(const AIGPlayerCharacter& Player, const FVector& BodyLocation) const
{
	// 앉을 때 잠깐 얹히는 보정은 0.35초면 빠지므로 여기 넣지 않는다.
	const FVector EyeWorld = GetActorTransform().TransformPosition(EyeLocal);
	return EyeWorld.Z - (BodyLocation.Z + Player.GetCameraBaseLocation().Z);
}

void AIGHidingSpot::PlaceOccupant(AIGPlayerCharacter& Player, const FVector& Location)
{
	Player.SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	LastPlacedLocation = Player.GetActorLocation();
}

FVector AIGHidingSpot::GetPeekLocation() const
{
	return GetActorTransform().TransformPosition(FVector(IGHiding::PeekOutsideFront, 0.0f, EyeLocal.Z));
}

FVector AIGHidingSpot::GetExitWorldLocation(const AIGPlayerCharacter& Player) const
{
	// ExitLocal은 바닥 위의 한 점이다. 몸의 원점은 캡슐 가운데라 반 높이만큼 올린다.
	FVector ExitWorld = GetActorTransform().TransformPosition(ExitLocal);
	if (const UCapsuleComponent* Capsule = Player.GetCapsuleComponent())
	{
		ExitWorld.Z += Capsule->GetScaledCapsuleHalfHeight() + 2.0f;
	}
	return ExitWorld;
}

void AIGHidingSpot::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ForceExit();
	Super::EndPlay(EndPlayReason);
}
