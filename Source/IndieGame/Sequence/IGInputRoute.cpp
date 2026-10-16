#include "Sequence/IGInputRoute.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "IndieGame.h"
#include "Interaction/IGSwingDoor.h"
#include "Player/IGInteractionComponent.h"
#include "Player/IGPlayerCharacter.h"

void FIGInputRoute::Reset(AIGPlayerCharacter* InPlayer, TFunction<void(bool, const FString&)> InCheck)
{
	ReleaseKeys();
	Player = InPlayer;
	Check = MoveTemp(InCheck);
	Steps.Reset();
	Index = 0;
	StepSeconds = 0.0f;
	StallSeconds = 0.0f;
	bBlockLogged = false;
}

void FIGInputRoute::Walk(const FVector& Point, const float Tolerance, const FString& Name)
{
	FStep Step;
	Step.Kind = EStep::Walk;
	Step.Point = Point;
	Step.Value = Tolerance;
	Step.Name = Name;
	Steps.Add(MoveTemp(Step));
}

void FIGInputRoute::Aim(const FVector& Point, const FString& Name)
{
	FStep Step;
	Step.Kind = EStep::Aim;
	Step.Point = Point;
	Step.Name = Name;
	Steps.Add(MoveTemp(Step));
}

void FIGInputRoute::Press(const float Seconds, const FString& Name, AActor* Expect, const FKey& Key)
{
	FStep Step;
	Step.Kind = EStep::Press;
	Step.Value = Seconds;
	Step.Name = Name;
	Step.Expect = Expect;
	Step.Key = Key;
	Steps.Add(MoveTemp(Step));
}

void FIGInputRoute::Wait(const float Seconds, const FString& Name)
{
	FStep Step;
	Step.Kind = EStep::Wait;
	Step.Value = Seconds;
	Step.Name = Name;
	Steps.Add(MoveTemp(Step));
}

void FIGInputRoute::Until(TFunction<bool()> Done, const float TimeoutSeconds, const FString& Name)
{
	FStep Step;
	Step.Kind = EStep::Until;
	Step.Value = TimeoutSeconds;
	Step.Name = Name;
	Step.Done = MoveTemp(Done);
	Steps.Add(MoveTemp(Step));
}

void FIGInputRoute::SendKey(const FKey& Key, const bool bPressed)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}
	PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key,
		bPressed ? IE_Pressed : IE_Released, bPressed ? 1.0f : 0.0f, false, FPlatformTime::Cycles64()));
}

void FIGInputRoute::SendMouse(const float X, const float Y)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}
	if (!FMath::IsNearlyZero(X))
	{
		PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::MouseX, X, 1.0f / 60.0f, 1,
			FPlatformTime::Cycles64()));
	}
	if (!FMath::IsNearlyZero(Y))
	{
		PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::MouseY, Y, 1.0f / 60.0f, 1,
			FPlatformTime::Cycles64()));
	}
}

void FIGInputRoute::ReleaseKeys()
{
	if (bForwardDown)
	{
		SendKey(EKeys::W, false);
		bForwardDown = false;
	}
	if (bKeyDown && Steps.IsValidIndex(Index))
	{
		SendKey(Steps[Index].Key, false);
	}
	bKeyDown = false;
}

AActor* FIGInputRoute::GetFocusedActor() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	const UIGInteractionComponent* Interaction = Character ? Character->FindComponentByClass<UIGInteractionComponent>() : nullptr;
	return Interaction ? Interaction->GetFocusedActor() : nullptr;
}

FVector FIGInputRoute::CameraLocation() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	const APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	return PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
}

FRotator FIGInputRoute::CameraRotation() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	const APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	return PC ? PC->GetControlRotation() : FRotator::ZeroRotator;
}

bool FIGInputRoute::Calibrate()
{
	// 마우스 X·Y를 한 번씩 4만큼 넣고 시점이 몇 도 도는지 잰다.
	switch (CalibrateStage)
	{
	case 0:
		CalibrateYaw = CameraRotation().Yaw;
		CalibratePitch = CameraRotation().Pitch;
		SendMouse(4.0f, 0.0f);
		CalibrateStage = 1;
		return false;
	case 1:
		MouseYawScale = FMath::FindDeltaAngleDegrees(CalibrateYaw, CameraRotation().Yaw) / 4.0f;
		CalibratePitch = CameraRotation().Pitch;
		SendMouse(0.0f, 4.0f);
		CalibrateStage = 2;
		return false;
	case 2:
		MousePitchScale = FMath::FindDeltaAngleDegrees(CalibratePitch, CameraRotation().Pitch) / 4.0f;
		CalibrateStage = 3;
		UE_LOG(LogIndieGame, Display, TEXT("INPUT_MOUSE yaw_per_unit=%.4f pitch_per_unit=%.4f"), MouseYawScale, MousePitchScale);
		if (Check)
		{
			Check(FMath::Abs(MouseYawScale) > 0.001f && FMath::Abs(MousePitchScale) > 0.001f, TEXT("input_mouse_turns_view"));
		}
		return true;
	default:
		return true;
	}
}

bool FIGInputRoute::StepAim(const FStep& Step)
{
	const FRotator Want = (Step.Point - CameraLocation()).Rotation();
	const FRotator Now = CameraRotation();
	const float YawErr = FMath::FindDeltaAngleDegrees(Now.Yaw, Want.Yaw);
	const float PitchErr = FMath::FindDeltaAngleDegrees(Now.Pitch, Want.Pitch);
	if (FMath::Abs(YawErr) < 1.2f && FMath::Abs(PitchErr) < 1.2f)
	{
		return true;
	}
	// 한 틱에 오차의 절반만 돌린다(사람 손처럼 넘치지 않게).
	SendMouse(
		FMath::IsNearlyZero(MouseYawScale) ? 0.0f : FMath::Clamp(YawErr * 0.5f / MouseYawScale, -40.0f, 40.0f),
		FMath::IsNearlyZero(MousePitchScale) ? 0.0f : FMath::Clamp(PitchErr * 0.5f / MousePitchScale, -40.0f, 40.0f));
	return false;
}

bool FIGInputRoute::StepWalk(const FStep& Step, const float DeltaSeconds)
{
	AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		return true;
	}
	FVector To = Step.Point - Character->GetActorLocation();
	To.Z = 0.0f;
	if (To.Size() < Step.Value)
	{
		if (bForwardDown)
		{
			SendKey(EKeys::W, false);
			bForwardDown = false;
		}
		StallSeconds = 0.0f;
		return true;
	}
	const FRotator Now = CameraRotation();
	const float YawErr = FMath::FindDeltaAngleDegrees(Now.Yaw, To.Rotation().Yaw);
	const float PitchErr = FMath::FindDeltaAngleDegrees(Now.Pitch, -8.0f);
	SendMouse(
		FMath::IsNearlyZero(MouseYawScale) ? 0.0f : FMath::Clamp(YawErr * 0.5f / MouseYawScale, -40.0f, 40.0f),
		FMath::IsNearlyZero(MousePitchScale) ? 0.0f : FMath::Clamp(PitchErr * 0.3f / MousePitchScale, -20.0f, 20.0f));
	const bool bFacing = FMath::Abs(YawErr) < 25.0f;
	if (bFacing != bForwardDown)
	{
		SendKey(EKeys::W, bFacing);
		bForwardDown = bFacing;
	}
	if (bForwardDown && Character->GetVelocity().Size2D() < 5.0f)
	{
		StallSeconds += DeltaSeconds;
		if (StallSeconds > 1.0f && Cast<AIGSwingDoor>(GetFocusedActor()))
		{
			SendKey(EKeys::E, true);
			SendKey(EKeys::E, false);
			StallSeconds = 0.0f;
		}
		else if (StallSeconds > 1.0f && !bBlockLogged)
		{
			bBlockLogged = true;
			const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
			const FVector Start = Character->GetActorLocation();
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(InputRouteBlock), false, Character);
			if (Capsule && Character->GetWorld()->SweepSingleByChannel(Hit, Start, Start + To.GetSafeNormal() * 30.0f,
					FQuat::Identity, Capsule->GetCollisionObjectType(),
					FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params))
			{
				UE_LOG(LogIndieGame, Display, TEXT("INPUT_BLOCK at=%s actor=%s comp=%s"), *Start.ToCompactString(),
					Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("none"),
					Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("none"));
			}
		}
	}
	else
	{
		StallSeconds = 0.0f;
		bBlockLogged = false;
	}
	return false;
}

bool FIGInputRoute::Tick(const float DeltaSeconds)
{
	if (!Calibrate())
	{
		return false;
	}
	if (!Steps.IsValidIndex(Index))
	{
		return true;
	}
	FStep& Step = Steps[Index];
	StepSeconds += DeltaSeconds;
	bool bDone = false;
	switch (Step.Kind)
	{
	case EStep::Walk:
		bDone = StepWalk(Step, DeltaSeconds);
		if (!bDone && StepSeconds > 25.0f)
		{
			if (Check)
			{
				Check(false, Step.Name);
			}
			UE_LOG(LogIndieGame, Display, TEXT("INPUT_STUCK %s at=%s goal=%s"), *Step.Name,
				Player.IsValid() ? *Player->GetActorLocation().ToCompactString() : TEXT("none"), *Step.Point.ToCompactString());
			ReleaseKeys();
			bDone = true;
		}
		break;
	case EStep::Aim:
		bDone = StepAim(Step);
		if (!bDone && StepSeconds > 6.0f)
		{
			if (Check)
			{
				Check(false, Step.Name);
			}
			bDone = true;
		}
		break;
	case EStep::Press:
		if (!bKeyDown)
		{
			// 초점은 10~15 Hz 타이머로 고쳐진다. 누르는 순간 게임이 하듯 한 번 다시 잡고 본다.
			if (AIGPlayerCharacter* Character = Player.Get())
			{
				if (UIGInteractionComponent* Interaction = Character->FindComponentByClass<UIGInteractionComponent>())
				{
					Interaction->RefreshFocus();
				}
			}
			if (Step.Expect.IsValid() && Check)
			{
				const AActor* Focus = GetFocusedActor();
				Check(Focus == Step.Expect.Get(), Step.Name + TEXT("_focus"));
				if (Focus != Step.Expect.Get())
				{
					// 눈에서 겨눈 쪽으로 무엇이 먼저 걸리는지 같이 적는다.
					FHitResult Hit;
					const FVector Eye = CameraLocation();
					const FVector End = Eye + CameraRotation().Vector() * 300.0f;
					FCollisionQueryParams Params(SCENE_QUERY_STAT(InputRouteFocus), false, Player.Get());
					const bool bHit = Player.IsValid() && Player->GetWorld()->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Params);
					UE_LOG(LogIndieGame, Display, TEXT("INPUT_FOCUS %s want=%s got=%s trace=%s comp=%s dist=%.0f"), *Step.Name,
						*Step.Expect->GetName(), Focus ? *Focus->GetName() : TEXT("none"),
						bHit && Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("none"),
						bHit && Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("none"), bHit ? Hit.Distance : -1.0f);
				}
			}
			SendKey(Step.Key, true);
			bKeyDown = true;
			StepSeconds = 0.0f;
		}
		else if (StepSeconds >= Step.Value)
		{
			SendKey(Step.Key, false);
			bKeyDown = false;
			bDone = true;
		}
		break;
	case EStep::Wait:
		bDone = StepSeconds >= Step.Value;
		break;
	case EStep::Until:
		bDone = Step.Done && Step.Done();
		if (bDone && Check)
		{
			Check(true, Step.Name);
		}
		else if (!bDone && StepSeconds > Step.Value)
		{
			if (Check)
			{
				Check(false, Step.Name);
			}
			bDone = true;
		}
		break;
	}
	if (bDone)
	{
		UE_LOG(LogIndieGame, Display, TEXT("INPUT_STEP %s at=%s"), *Step.Name,
			Player.IsValid() ? *Player->GetActorLocation().ToCompactString() : TEXT("none"));
		++Index;
		StepSeconds = 0.0f;
	}
	return false;
}
