#include "Player/IGInteractionComponent.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "Interaction/IGInteractable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Templates/UnrealTemplate.h"

namespace IGInteraction
{
	// 10~15Hz로 훑으면 시선이 지나간 뒤 60~100ms 뒤에 브래킷이 따라와 프롬프트가
	// 늦게 켜지고 늦게 꺼진다. 30Hz면 한 프레임 반이다.
	constexpr float MinUpdateInterval = 1.0f / 60.0f;
	constexpr float MaxUpdateInterval = 1.0f / 10.0f;
	// 스윕 후보 점수. 시선에서 벗어난 거리에 앞뒤 거리를 8:1로 더한다 — 멀리 있는
	// 정중앙 소품이 코앞의 살짝 빗나간 소품을 이기지 않게. 지금 초점인 것은 조금
	// 봐준다. 둘이 엇비슷할 때 프레임마다 번갈아 잡히면 브래킷이 떤다.
	constexpr float SweepDistanceWeight = 0.125f;
	constexpr float SweepStickyBonus = 6.0f;
}

bool UIGInteractionComponent::IsDormantInteractable(AActor* Actor, AActor* Interactor)
{
	return IsValid(Actor)
		&& Actor->IsHidden()
		&& Actor->GetClass()->ImplementsInterface(UIGInteractable::StaticClass())
		&& !IIGInteractable::Execute_CanInteract(Actor, Interactor);
}

UIGInteractionComponent::UIGInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;
}

void UIGInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	FocusUpdateInterval = FMath::Clamp(
		FocusUpdateInterval,
		IGInteraction::MinUpdateInterval,
		IGInteraction::MaxUpdateInterval);
	InputBufferSeconds = FMath::Clamp(InputBufferSeconds, 0.0f, 0.25f);
	HoldRewindSpeedScale = FMath::Max(0.1f, HoldRewindSpeedScale);
	SetComponentTickEnabled(false);

	RefreshFocus();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			UpdateTimerHandle,
			this,
			&ThisClass::HandleUpdateTimer,
			FocusUpdateInterval,
			true);
	}
}

void UIGInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateTimerHandle);
	}

	bInteractionPressed = false;
	ClearBufferedPress();
	ClearHoldProgressRewind();
	FinishActiveInteraction(EIGInteractionEndReason::OwnerEndPlay, false);
	FocusedActor.Reset();
	FocusedHitResult = FHitResult();

	Super::EndPlay(EndPlayReason);
}

void UIGInteractionComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateActiveInteraction();
}

void UIGInteractionComponent::PressInteraction()
{
	if (!bInteractionInputEnabled || !IsActive())
	{
		return;
	}
	if (bFinalizingInteraction)
	{
		BufferInteractionPress();
		return;
	}
	if (bInteractionActive && bToggleHoldLatched)
	{
		// The same input that latched a hold also gives the player an immediate,
		// predictable way to cancel it before completion.
		++InteractionGeneration;
		bInteractionPressed = false;
		FinishActiveInteraction(EIGInteractionEndReason::Cancelled, false);
		return;
	}
	if (bInteractionPressed)
	{
		return;
	}

	bInteractionPressed = true;
	RefreshFocus();
	if (!bInteractionPressed || !bInteractionInputEnabled)
	{
		return;
	}
	if (!TryStartFocusedInteraction())
	{
		BufferInteractionPress();
	}
}

bool UIGInteractionComponent::TryStartFocusedInteraction()
{
	if (!bInteractionInputEnabled
		|| !bInteractionPressed
		|| bInteractionActive
		|| bFinalizingInteraction)
	{
		return false;
	}

	AActor* Target = FocusedActor.Get();
	AActor* Interactor = GetOwner();
	const uint32 AttemptGeneration = ++InteractionGeneration;
	if (!IsValid(Target)
		|| !Target->GetClass()->ImplementsInterface(UIGInteractable::StaticClass())
		|| !IIGInteractable::Execute_CanInteract(Target, Interactor))
	{
		return false;
	}

	const auto IsAttemptValid = [this, Target, AttemptGeneration]()
	{
		return bInteractionInputEnabled
			&& bInteractionPressed
			&& !bFinalizingInteraction
			&& InteractionGeneration == AttemptGeneration
			&& FocusedActor.Get() == Target
			&& IsValid(Target);
	};

	if (!IsAttemptValid())
	{
		return false;
	}

	const FGameplayTag TargetInteractionTag =
		IIGInteractable::Execute_GetInteractionTag(Target, Interactor);
	if (!IsAttemptValid())
	{
		return false;
	}

	float TargetHoldDuration = FMath::Max(
		0.0f,
		IIGInteractable::Execute_GetInteractionHoldDuration(Target, Interactor));
	if (!IsAttemptValid())
	{
		return false;
	}
	bool bUseToggleHold = false;
	if (TargetHoldDuration > KINDA_SMALL_NUMBER)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (const UIGAccessibilitySubsystem* Accessibility =
					GameInstance->GetSubsystem<UIGAccessibilitySubsystem>())
				{
					TargetHoldDuration *= Accessibility->GetHoldDurationScale();
					bUseToggleHold = Accessibility->UsesToggleHoldInteractions();
				}
			}
		}
	}

	ActiveActor = Target;
	ActiveHitResult = FocusedHitResult;
	ActiveInteractionTag = TargetInteractionTag;
	ActiveHoldDuration = TargetHoldDuration;
	ActiveStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	bInteractionActive = true;
	bToggleHoldLatched = bUseToggleHold;
	ClearBufferedPress();
	ClearHoldProgressRewind();
	SetComponentTickEnabled(true);

	const FIGInteractionContext Context = MakeContext(
		Target,
		ActiveHitResult,
		ActiveInteractionTag,
		0.0f,
		ActiveHoldDuration <= KINDA_SMALL_NUMBER ? 1.0f : 0.0f);
	IIGInteractable::Execute_BeginInteraction(Target, Context);

	if (!IsValid(Target))
	{
		FinishActiveInteraction(EIGInteractionEndReason::Cancelled, false);
		return true;
	}

	if (!bInteractionActive || ActiveActor.Get() != Target)
	{
		return true;
	}

	OnInteractionStarted.Broadcast(Target);

	// The callback is allowed to cancel or destroy its target.
	if (bInteractionActive
		&& ActiveActor.Get() == Target
		&& ActiveHoldDuration <= KINDA_SMALL_NUMBER)
	{
		CompleteActiveInteraction();
	}
	return true;
}

void UIGInteractionComponent::ReleaseInteraction()
{
	if (!bInteractionPressed)
	{
		return;
	}

	bInteractionPressed = false;
	if (bToggleHoldLatched && bInteractionActive)
	{
		return;
	}

	++InteractionGeneration;
	FinishActiveInteraction(EIGInteractionEndReason::Released, false);
}

void UIGInteractionComponent::CancelInteraction()
{
	if (!bInteractionPressed && !bInteractionActive && !bInteractionPressBuffered)
	{
		return;
	}

	++InteractionGeneration;
	bInteractionPressed = false;
	ClearBufferedPress();
	FinishActiveInteraction(EIGInteractionEndReason::Cancelled, false);
}

void UIGInteractionComponent::SetInteractionInputEnabled(const bool bEnabled)
{
	if (bInteractionInputEnabled == bEnabled)
	{
		return;
	}

	bInteractionInputEnabled = bEnabled;
	++InteractionGeneration;

	if (!bEnabled)
	{
		bInteractionPressed = false;
		ClearBufferedPress();
		ClearHoldProgressRewind();
		FinishActiveInteraction(EIGInteractionEndReason::Cancelled, false);
		SetFocusedActor(nullptr, FHitResult());
	}
	else
	{
		RefreshFocus();
	}
}

void UIGInteractionComponent::RefreshFocus()
{
	if (bFocusScanInProgress)
	{
		// The regular 10-15 Hz timer satisfies this deferred request without recursion.
		return;
	}

	TGuardValue<bool> FocusScanGuard(bFocusScanInProgress, true);

	if (!bInteractionInputEnabled || !IsActive())
	{
		SetFocusedActor(nullptr, FHitResult());
		return;
	}

	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !IsValid(OwnerActor))
	{
		SetFocusedActor(nullptr, FHitResult());
		return;
	}

	FVector TraceStart;
	FRotator ViewRotation;
	if (!GetInteractionViewPoint(TraceStart, ViewRotation))
	{
		SetFocusedActor(nullptr, FHitResult());
		return;
	}

	const uint32 ScanGeneration = InteractionGeneration;
	const FVector TraceEnd = TraceStart + ViewRotation.Vector() * FMath::Max(0.0f, TraceDistance);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IGInteractionTrace), bTraceComplex, OwnerActor);
	QueryParams.AddIgnoredActor(OwnerActor);

	FHitResult HitResult;
	bool bBlockingHit = World->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		TraceChannel,
		QueryParams);
	for (int32 Skip = 0; Skip < UIGInteractionComponent::MaxDormantSkips && bBlockingHit
		&& UIGInteractionComponent::IsDormantInteractable(HitResult.GetActor(), OwnerActor); ++Skip)
	{
		QueryParams.AddIgnoredActor(HitResult.GetActor());
		bBlockingHit = World->LineTraceSingleByChannel(
			HitResult,
			TraceStart,
			TraceEnd,
			TraceChannel,
			QueryParams);
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (bDrawDebugTrace)
	{
		const FVector DebugEnd = bBlockingHit ? HitResult.ImpactPoint : TraceEnd;
		DrawDebugLine(
			World,
			TraceStart,
			DebugEnd,
			bBlockingHit ? FColor::Green : FColor::Red,
			false,
			FocusUpdateInterval,
			0,
			1.0f);
	}
#endif

	AActor* Candidate = bBlockingHit ? ResolveInteractable(HitResult, OwnerActor) : nullptr;
	FHitResult CandidateHit = HitResult;

	// Blueprint CanInteract may cancel input, destroy the actor, or trigger a nested scan.
	if (!bInteractionInputEnabled || !IsActive() || InteractionGeneration != ScanGeneration)
	{
		return;
	}

	// Forgiving pass: the centre ray alone makes small props (a bottle on a
	// wire shelf, a wallet on a desk) feel impossible to hit while moving.
	// Sweep a small sphere and take the interactable closest to the view ray.
	if (!Candidate && FocusSweepRadius > 0.0f)
	{
		TArray<FHitResult> SweepHits;
		World->SweepMultiByChannel(
			SweepHits,
			TraceStart,
			TraceEnd,
			FQuat::Identity,
			TraceChannel,
			FCollisionShape::MakeSphere(FocusSweepRadius),
			QueryParams);

		const FVector ViewDirection = ViewRotation.Vector();
		const AActor* CurrentFocus = FocusedActor.Get();
		float BestScore = TNumericLimits<float>::Max();
		for (const FHitResult& SweepHit : SweepHits)
		{
			AActor* SweptActor = ResolveInteractable(SweepHit, OwnerActor);
			if (!SweptActor)
			{
				continue;
			}

			// 구체의 가장자리가 선반이나 문틀을 돌아가도 눈에서 물체까지는
			// 열려 있어야 한다. 가려진 손잡이와 벽 뒤 기록이 선택되면 안 된다.
			FHitResult VisibilityHit;
			const FVector SightEnd = SweepHit.ImpactPoint
				+ (SweepHit.ImpactPoint - TraceStart).GetSafeNormal() * 1.0f;
			bool bSightBlocked = World->LineTraceSingleByChannel(
				VisibilityHit, TraceStart, SightEnd, TraceChannel, QueryParams);
			for (int32 Skip = 0; Skip < UIGInteractionComponent::MaxDormantSkips && bSightBlocked
				&& VisibilityHit.GetActor() != SweptActor
				&& UIGInteractionComponent::IsDormantInteractable(VisibilityHit.GetActor(), OwnerActor); ++Skip)
			{
				QueryParams.AddIgnoredActor(VisibilityHit.GetActor());
				bSightBlocked = World->LineTraceSingleByChannel(
					VisibilityHit, TraceStart, SightEnd, TraceChannel, QueryParams);
			}
			if (bSightBlocked && VisibilityHit.GetActor() != SweptActor)
			{
				continue;
			}

			// Perpendicular distance from the aim ray: the nearer the centre
			// of the screen, the stronger the claim on focus. 앞뒤 거리도 조금
			// 더하고, 지금 초점인 것은 조금 봐준다.
			const FVector ToHit = SweepHit.ImpactPoint - TraceStart;
			const float AlongRay = FVector::DotProduct(ToHit, ViewDirection);
			const float Deviation = (ToHit - ViewDirection * AlongRay).Size();
			float Score = Deviation
				+ FMath::Max(AlongRay, 0.0f) * IGInteraction::SweepDistanceWeight;
			if (SweptActor == CurrentFocus)
			{
				Score -= IGInteraction::SweepStickyBonus;
			}
			if (Score < BestScore)
			{
				BestScore = Score;
				Candidate = SweptActor;
				CandidateHit = SweepHit;
			}
		}

		if (!bInteractionInputEnabled || !IsActive() || InteractionGeneration != ScanGeneration)
		{
			return;
		}
	}

	if (!IsValid(Candidate))
	{
		Candidate = nullptr;
	}

	SetFocusedActor(Candidate, Candidate ? CandidateHit : FHitResult());
}

AActor* UIGInteractionComponent::ResolveInteractable(
	const FHitResult& HitResult,
	AActor* Interactor) const
{
	AActor* HitActor = HitResult.GetActor();
	if (!IsValid(HitActor)
		|| !HitActor->GetClass()->ImplementsInterface(UIGInteractable::StaticClass()))
	{
		return nullptr;
	}

	return IIGInteractable::Execute_CanInteract(HitActor, Interactor) ? HitActor : nullptr;
}

AActor* UIGInteractionComponent::GetFocusedActor() const
{
	return FocusedActor.Get();
}

FText UIGInteractionComponent::GetFocusedPrompt() const
{
	AActor* Target = FocusedActor.Get();
	if (!IsValid(Target) || !Target->GetClass()->ImplementsInterface(UIGInteractable::StaticClass()))
	{
		return FText::GetEmpty();
	}

	return IIGInteractable::Execute_GetInteractionPrompt(Target, GetOwner());
}

FGameplayTag UIGInteractionComponent::GetFocusedInteractionTag() const
{
	AActor* Target = FocusedActor.Get();
	if (!IsValid(Target) || !Target->GetClass()->ImplementsInterface(UIGInteractable::StaticClass()))
	{
		return FGameplayTag();
	}

	return IIGInteractable::Execute_GetInteractionTag(Target, GetOwner());
}

bool UIGInteractionComponent::IsInteracting() const
{
	return bInteractionActive && ActiveActor.IsValid();
}

float UIGInteractionComponent::GetHoldProgress() const
{
	if (bInteractionActive)
	{
		if (ActiveHoldDuration <= KINDA_SMALL_NUMBER)
		{
			return 1.0f;
		}

		return FMath::Clamp(
			GetActiveHeldDuration() / ActiveHoldDuration,
			0.0f,
			1.0f);
	}

	const UWorld* World = GetWorld();
	if (!World
		|| RewindStartProgress <= KINDA_SMALL_NUMBER
		|| RewindSourceHoldDuration <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float Elapsed = FMath::Max(
		0.0f,
		World->GetTimeSeconds() - RewindStartTime);
	return FMath::Max(
		0.0f,
		RewindStartProgress
			- Elapsed * HoldRewindSpeedScale / RewindSourceHoldDuration);
}

void UIGInteractionComponent::HandleUpdateTimer()
{
	if (!bInteractionInputEnabled || !IsActive())
	{
		return;
	}

	if (bInteractionActive && !ActiveActor.IsValid())
	{
		FinishActiveInteraction(EIGInteractionEndReason::Cancelled, false);
	}

	RefreshFocus();
	TryConsumeBufferedPress();
}

void UIGInteractionComponent::BufferInteractionPress()
{
	const UWorld* World = GetWorld();
	if (!World || InputBufferSeconds <= 0.0f)
	{
		return;
	}

	bInteractionPressBuffered = true;
	BufferedPressExpiresAt = World->GetTimeSeconds() + InputBufferSeconds;
}

void UIGInteractionComponent::TryConsumeBufferedPress()
{
	if (!bInteractionPressBuffered
		|| bFinalizingInteraction
		|| bInteractionActive
		|| !bInteractionInputEnabled)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() > BufferedPressExpiresAt)
	{
		ClearBufferedPress();
		return;
	}

	const bool bWasPhysicallyPressed = bInteractionPressed;
	bInteractionPressed = true;
	if (!TryStartFocusedInteraction())
	{
		bInteractionPressed = bWasPhysicallyPressed;
		return;
	}

	// A released buffered tap must behave exactly like a fresh press/release.
	// Instant targets already completed synchronously; hold targets receive the
	// ordinary early-release path (for example, a loud rather than quiet door).
	if (!bWasPhysicallyPressed)
	{
		bInteractionPressed = false;
		if (bInteractionActive && !bToggleHoldLatched)
		{
			++InteractionGeneration;
			FinishActiveInteraction(EIGInteractionEndReason::Released, false);
		}
	}
}

void UIGInteractionComponent::ClearBufferedPress()
{
	bInteractionPressBuffered = false;
	BufferedPressExpiresAt = 0.0f;
}

bool UIGInteractionComponent::GetInteractionViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor))
	{
		return false;
	}

	if (const APlayerController* PlayerController = Cast<APlayerController>(OwnerActor))
	{
		PlayerController->GetPlayerViewPoint(OutLocation, OutRotation);
		return true;
	}

	if (const APawn* OwnerPawn = Cast<APawn>(OwnerActor))
	{
		if (const APlayerController* PlayerController = Cast<APlayerController>(OwnerPawn->GetController()))
		{
			PlayerController->GetPlayerViewPoint(OutLocation, OutRotation);
			return true;
		}
	}

	OwnerActor->GetActorEyesViewPoint(OutLocation, OutRotation);
	return true;
}

void UIGInteractionComponent::SetFocusedActor(AActor* NewActor, const FHitResult& NewHitResult)
{
	AActor* PreviousActor = FocusedActor.Get();
	if (PreviousActor == NewActor)
	{
		FocusedHitResult = NewHitResult;
		return;
	}

	FocusedActor = NewActor;
	FocusedHitResult = NewHitResult;
	++InteractionGeneration;

	if (bInteractionActive && ActiveActor.Get() != NewActor)
	{
		FinishActiveInteraction(EIGInteractionEndReason::FocusLost, false);
	}

	OnFocusChanged.Broadcast(PreviousActor, NewActor);
}

void UIGInteractionComponent::UpdateActiveInteraction()
{
	AActor* Target = ActiveActor.Get();
	if (!bInteractionActive
		|| (!bInteractionPressed && !bToggleHoldLatched)
		|| !IsValid(Target))
	{
		return;
	}

	const float HeldDuration = GetActiveHeldDuration();
	const float HoldProgress = ActiveHoldDuration <= KINDA_SMALL_NUMBER
		? 1.0f
		: FMath::Clamp(HeldDuration / ActiveHoldDuration, 0.0f, 1.0f);
	const FIGInteractionContext Context = MakeContext(
		Target,
		ActiveHitResult,
		ActiveInteractionTag,
		HeldDuration,
		HoldProgress);

	IIGInteractable::Execute_UpdateInteraction(Target, Context);

	if (!IsValid(Target))
	{
		FinishActiveInteraction(EIGInteractionEndReason::Cancelled, false);
		return;
	}

	if (!bInteractionActive || ActiveActor.Get() != Target)
	{
		return;
	}

	OnInteractionProgress.Broadcast(Target, HoldProgress);

	if (bInteractionActive
		&& ActiveActor.Get() == Target
		&& HoldProgress >= 1.0f)
	{
		CompleteActiveInteraction();
	}
}

void UIGInteractionComponent::CompleteActiveInteraction()
{
	FinishActiveInteraction(EIGInteractionEndReason::Completed, true);
}

void UIGInteractionComponent::FinishActiveInteraction(
	const EIGInteractionEndReason EndReason,
	const bool bCompleted)
{
	if (!bInteractionActive || bFinalizingInteraction)
	{
		return;
	}

	bFinalizingInteraction = true;
	AActor* Target = ActiveActor.Get();
	const float HeldDuration = GetActiveHeldDuration();
	const float HoldProgress = bCompleted
		? 1.0f
		: (ActiveHoldDuration <= KINDA_SMALL_NUMBER
			? 0.0f
			: FMath::Clamp(HeldDuration / ActiveHoldDuration, 0.0f, 1.0f));
	const float CompletedHoldDuration = ActiveHoldDuration;
	const FIGInteractionContext Context = MakeContext(
		Target,
		ActiveHitResult,
		ActiveInteractionTag,
		HeldDuration,
		HoldProgress);

	// Clear first so callbacks cannot complete or end the same interaction twice.
	ResetActiveState();
	SetComponentTickEnabled(false);
	if (!bCompleted
		&& bInteractionInputEnabled
		&& HoldProgress > KINDA_SMALL_NUMBER
		&& EndReason != EIGInteractionEndReason::OwnerEndPlay)
	{
		BeginHoldProgressRewind(HoldProgress, CompletedHoldDuration);
	}

	if (IsValid(Target) && Target->GetClass()->ImplementsInterface(UIGInteractable::StaticClass()))
	{
		if (bCompleted)
		{
			++CompletedInteractionCount;
			LastCompletedTarget = Target;
			IIGInteractable::Execute_CompleteInteraction(Target, Context);
		}

		if (IsValid(Target))
		{
			IIGInteractable::Execute_EndInteraction(Target, Context, EndReason);
		}
	}

	OnInteractionEnded.Broadcast(IsValid(Target) ? Target : nullptr, EndReason);
	bFinalizingInteraction = false;
}

void UIGInteractionComponent::BeginHoldProgressRewind(
	const float HoldProgress,
	const float HoldDuration)
{
	const UWorld* World = GetWorld();
	if (!World || HoldDuration <= KINDA_SMALL_NUMBER)
	{
		ClearHoldProgressRewind();
		return;
	}

	RewindStartProgress = FMath::Clamp(HoldProgress, 0.0f, 1.0f);
	RewindSourceHoldDuration = HoldDuration;
	RewindStartTime = World->GetTimeSeconds();
}

void UIGInteractionComponent::ClearHoldProgressRewind()
{
	RewindStartProgress = 0.0f;
	RewindSourceHoldDuration = 0.0f;
	RewindStartTime = 0.0f;
}

void UIGInteractionComponent::ResetActiveState()
{
	bInteractionActive = false;
	bToggleHoldLatched = false;
	ActiveActor.Reset();
	ActiveHitResult = FHitResult();
	ActiveInteractionTag = FGameplayTag();
	ActiveHoldDuration = 0.0f;
	ActiveStartTime = 0.0f;
}

float UIGInteractionComponent::GetActiveHeldDuration() const
{
	const UWorld* World = GetWorld();
	return World ? FMath::Max(0.0f, World->GetTimeSeconds() - ActiveStartTime) : 0.0f;
}

FIGInteractionContext UIGInteractionComponent::MakeContext(
	AActor* TargetActor,
	const FHitResult& HitResult,
	const FGameplayTag& Tag,
	const float HeldDuration,
	const float HoldProgress)
{
	FIGInteractionContext Context;
	Context.Interactor = GetOwner();
	Context.SourceComponent = this;
	Context.TargetActor = TargetActor;
	Context.HitResult = HitResult;
	Context.InteractionTag = Tag;
	Context.HeldDuration = FMath::Max(0.0f, HeldDuration);
	Context.HoldProgress = FMath::Clamp(HoldProgress, 0.0f, 1.0f);
	return Context;
}
