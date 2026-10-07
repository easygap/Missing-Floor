#include "Sequence/IGStairwellPresenceProbe.h"

#include "Components/CapsuleComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGStairwellPresence.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "IndieGame.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGPlayerCharacter.h"

AIGStairwellPresenceProbe::AIGStairwellPresenceProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGStairwellPresenceProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("STAIRWELL_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGStairwellPresenceProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("STAIRWELL_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGStairwellPresenceProbe::Shoot(const TCHAR* Name)
{
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StairwellReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("STAIRWELL_SHOT %s"), *Path);
}

FVector AIGStairwellPresenceProbe::Eye() const
{
	FVector Location;
	FRotator View;
	if (const APlayerController* PC = Controller.Get())
	{
		PC->GetPlayerViewPoint(Location, View);
	}
	return Location;
}

void AIGStairwellPresenceProbe::WalkToward(const FVector& Target)
{
	AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		return;
	}
	if (APlayerController* PC = Controller.Get())
	{
		PC->ResetIgnoreMoveInput();
	}
	Character->AddMovementInput((Target - Character->GetActorLocation()).GetSafeNormal2D(), 1.0f);
}

void AIGStairwellPresenceProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 120.0f)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	const auto Next = [this](const int32 NewPhase)
	{
		Phase = NewPhase;
		PhaseSeconds = 0.0f;
	};
	if (Phase == 0)
	{
		Controller = GetWorld()->GetFirstPlayerController();
		Player = Controller.IsValid() ? Cast<AIGPlayerCharacter>(Controller->GetPawn()) : nullptr;
		for (TActorIterator<AIGStairwellPresence> It(GetWorld()); It; ++It)
		{
			Presence = *It;
		}
		// 무대가 다 서고 첫 장면이 몸을 자리에 놓을 때까지 기다린다. 렌더가 있는 실행은
		// 이 시간이 길다.
		const bool bStanding = Player.IsValid()
			&& Player->GetCharacterMovement()->MovementMode == MOVE_Walking;
		if (!Player.IsValid() || !Presence.IsValid() || !bStanding || PhaseSeconds < 3.0f)
		{
			if (PhaseSeconds > 40.0f)
			{
				Check(false, TEXT("stage_ready"));
				Finish();
			}
			return;
		}
		AIGStairwellPresence::BuildDescentRoute(Route);
		const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Controller->ResetIgnoreMoveInput();
		Controller->ResetIgnoreLookInput();
		Player->SetCameraMotionEnabled(false);
		Player->SetActorLocation(Route[0] + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Player->GetCharacterMovement()->StopMovementImmediately();
		Controller->SetControlRotation((Route[1] - Route[0]).GetSafeNormal2D().Rotation());
		Presence->ForceArm();
		bShots = FParse::Param(FCommandLine::Get(), TEXT("IGStairwellShots"));
		if (bShots && Player->GetFlashlight())
		{
			// 밤처럼 손전등으로 본다.
			Player->GetFlashlight()->SetAvailable(true);
			Player->GetFlashlight()->SetOn(true);
		}
		Next(1);
		return;
	}
	AIGPlayerCharacter* Character = Player.Get();
	AIGStairwellPresence* Follower = Presence.Get();
	APlayerController* PC = Controller.Get();
	if (!Character || !Follower || !PC)
	{
		Check(false, TEXT("stage_alive"));
		Finish();
		return;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = Character->GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);
	if (Follower->IsFigureShown() && Follower->GetFigureFeet().Z < Feet.Z - 60.0f && Phase <= 2)
	{
		// 내려가는 그녀보다 아래에 서면 앞질러 건너온 것이다.
		bCrossed = true;
	}
	switch (Phase)
	{
	case 1:
	{
		// 계단 길을 따라 내려간다. 두 층 반쯤이면 충분하다. 옮긴 몸이 바닥에 닿을 때까지 기다린다.
		if (PhaseSeconds < 0.5f)
		{
			break;
		}
		if (RouteIndex < Route.Num())
		{
			const FVector Target = Route[RouteIndex];
			PC->SetControlRotation(FRotator(-8.0f, (Target - Feet).GetSafeNormal2D().Rotation().Yaw, 0.0f));
			WalkToward(Target);
			if (FVector::Dist2D(Feet, Target) < 28.0f && FMath::Abs(Feet.Z - Target.Z) < 50.0f)
			{
				++RouteIndex;
			}
		}
		if (PhaseSeconds > 12.0f || RouteIndex >= 12)
		{
			UE_LOG(LogIndieGame, Display, TEXT("STAIRWELL_WALK index=%d steps=%d active=%d at=%s figure=%s"),
				RouteIndex, Follower->GetStepsPlayed(), Follower->IsEncounterActive() ? 1 : 0,
				*Feet.ToCompactString(), *Follower->GetFigureFeet().ToCompactString());
			Check(Follower->IsEncounterActive(), TEXT("follower_begins_on_the_stairs"));
			Check(Follower->GetStepsPlayed() >= 4, TEXT("steps_follow_her_stride"));
			StepsBeforeStop = Follower->GetStepsPlayed();
			Next(2);
		}
		break;
	}
	case 2:
		// 멈춘다. 한 발이 더 난다.
		if (PhaseSeconds > 1.8f)
		{
			Check(Follower->GetExtraSteps() >= 1, TEXT("one_more_step_after_she_stops"));
			Check(!bCrossed, TEXT("never_overtakes_her"));
			Next(3);
		}
		break;
	case 3:
	{
		// 돌아서서 그를 보며 계단을 거슬러 오른다.
		const FVector Chest = Follower->GetFigureFeet() + FVector(0.0f, 0.0f, 90.0f);
		PC->SetControlRotation((Chest - Eye()).Rotation());
		// 눈에 들어오면 그 자리에 서서 본다. 찍을 때는 다가가지 않는다.
		if (RouteIndex > 1 && !(bShots && Follower->IsWatched()))
		{
			const FVector Target = Route[RouteIndex - 1];
			WalkToward(Target);
			if (FVector::Dist2D(Feet, Target) < 28.0f && FMath::Abs(Feet.Z - Target.Z) < 50.0f)
			{
				--RouteIndex;
			}
		}
		if (bShots && ShotsTaken == 0 && Follower->IsWatched())
		{
			Shoot(TEXT("stair-follower-watched"));
			++ShotsTaken;
		}
		if (Follower->HasLifted())
		{
			LiftedSeconds += DeltaSeconds;
			if (bShots && ShotsTaken == 1 && LiftedSeconds > 0.8f)
			{
				Shoot(TEXT("stair-follower-lifted"));
				++ShotsTaken;
			}
			if (!bShots || LiftedSeconds > 1.4f)
			{
				Check(Follower->IsFigureShown(), TEXT("stays_while_watched"));
				Next(4);
			}
		}
		else if (PhaseSeconds > 14.0f)
		{
			UE_LOG(LogIndieGame, Display, TEXT("STAIRWELL_LOOK figure=%s feet=%s shown=%d"),
				*Follower->GetFigureFeet().ToCompactString(), *Feet.ToCompactString(), Follower->IsFigureShown() ? 1 : 0);
			Check(false, TEXT("lifts_its_head_when_watched"));
			Finish();
		}
		break;
	}
	case 4:
		// 눈을 돌린다.
		if (PhaseSeconds < 0.1f)
		{
			PC->SetControlRotation(PC->GetControlRotation() + FRotator(0.0f, 180.0f, 0.0f));
		}
		if (PhaseSeconds > 1.0f && Phase == 4)
		{
			Check(!Follower->IsFigureShown() && !Follower->IsEncounterActive(), TEXT("gone_when_she_looks_away"));
			Check(Follower->ArePrintsShown(), TEXT("wet_prints_remain"));
			if (!bShots)
			{
				Finish();
				break;
			}
			// 돌아서서 그가 섰던 자리를 내려다본다.
			PC->SetControlRotation((Follower->GetFigureFeet() - Eye()).Rotation());
			Next(5);
		}
		break;
	case 5:
		if (PhaseSeconds > 1.4f)
		{
			Shoot(TEXT("stair-follower-prints"));
			Next(6);
		}
		break;
	case 6:
		if (PhaseSeconds > 0.6f)
		{
			Finish();
		}
		break;
	default:
		break;
	}
}
