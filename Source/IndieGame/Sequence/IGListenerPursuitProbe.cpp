#include "Sequence/IGListenerPursuitProbe.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Engine/PointLight.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "IndieGame.h"
#include "Interaction/IGSwingDoor.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGPlayerCharacter.h"

namespace IGPursuitProbe
{
	/** 밤2 관리실의 무너진 자재 자리. 관리실 바닥 위 몸 높이다. */
	const FVector CollapseAt(200.0f, -170.0f, 96.0f);
	/** 그녀는 옥상 통로에 둔다. 그가 오를 수 없는 자리라 손이 닿지 않는다. */
	const FVector PlayerParked(-60.0f, 220.0f, 1298.0f);
	/** 한 틱에 이보다 많이 옮겨졌으면 걸음이 아니라 순간이동이다. */
	constexpr float TeleportStep = 60.0f;
	/** 계단 위에서 몸 밑 바닥과의 틈 상한. 디딤판 가운데를 잇는 선이라 반 단까지는 흔들린다. */
	constexpr float StairFloorGapLimit = 22.0f;

	/**
	 * 촬영 카메라. 4층 참에서 동쪽 계단을 내려다보는 것, 3·4층 사이 서쪽 계단 위에서 반 층 참 쪽을 보는 것,
	 * 3층 복도 출입구에서 참을 보는 것.
	 */
	struct FShotCamera
	{
		FVector Location;
		FRotator Rotation;
		FVector Lamp;
	};
	const FShotCamera ShotCameras[] = {
		{FVector(-407.5f, -330.0f, 1040.0f), FRotator(-30.0f, 90.0f, 0.0f), FVector(-407.5f, -200.0f, 980.0f)},
		{FVector(-522.5f, -140.0f, 880.0f), FRotator(-28.0f, 90.0f, 0.0f), FVector(-500.0f, -20.0f, 860.0f)},
		{FVector(-300.0f, -300.0f, 760.0f), FRotator(-25.0f, 180.0f, 0.0f), FVector(-420.0f, -300.0f, 760.0f)},
	};
}

AIGListenerPursuitProbe::AIGListenerPursuitProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGListenerPursuitProbe::Configure(
	AIGListenerEntity* InEntity,
	AIGPlayerCharacter* InPlayer,
	UIGNoiseSubsystem* InNoise)
{
	Entity = InEntity;
	Player = InPlayer;
	Noise = InNoise;
}

void AIGListenerPursuitProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("PURSUIT_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGListenerPursuitProbe::Report(const FVector& Location, const float Loudness)
{
	if (UIGNoiseSubsystem* Subsystem = Noise.Get())
	{
		Subsystem->ReportNoiseUnmasked(Location, Loudness, Player.Get());
	}
}

float AIGListenerPursuitProbe::FeetZ() const
{
	const AIGListenerEntity* Listener = Entity.Get();
	return Listener ? Listener->GetActorLocation().Z - 60.0f : 0.0f;
}

void AIGListenerPursuitProbe::Shoot(const TCHAR* Name)
{
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PursuitReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	LastShotSeconds = TotalSeconds;
	ShotsTaken.Add(FName(Name));
	if (const AIGListenerEntity* Listener = Entity.Get())
	{
		UE_LOG(LogIndieGame, Display, TEXT("PURSUIT_SHOT %s at=%s"), *Path, *Listener->GetActorLocation().ToCompactString());
	}
}

void AIGListenerPursuitProbe::UpdateShots()
{
	UWorld* World = GetWorld();
	const AIGListenerEntity* Listener = Entity.Get();
	if (!bShots || !World || !Listener)
	{
		return;
	}
	if (ShotCameras.IsEmpty())
	{
		for (const IGPursuitProbe::FShotCamera& Spec : IGPursuitProbe::ShotCameras)
		{
			ACameraActor* Camera = World->SpawnActor<ACameraActor>(Spec.Location, Spec.Rotation);
			if (Camera)
			{
				Camera->GetCameraComponent()->SetFieldOfView(80.0f);
			}
			ShotCameras.Add(Camera);
			if (APointLight* Lamp = World->SpawnActor<APointLight>(Spec.Lamp, FRotator::ZeroRotator))
			{
				Lamp->PointLightComponent->SetMobility(EComponentMobility::Movable);
				Lamp->PointLightComponent->SetIntensity(2400.0f);
				Lamp->PointLightComponent->SetAttenuationRadius(700.0f);
				Lamp->PointLightComponent->SetCastShadows(true);
			}
		}
	}
	// 내려오는 길(3단계)만 찍는다. 반 층 참 카메라는 3·4층 사이, 참 카메라는 3층 참이다.
	if (Phase != 3)
	{
		return;
	}
	const FVector At = Listener->GetActorLocation();
	const float Feet = FeetZ();
	// 동쪽 계단과 반 층 참 동쪽은 4층 참에서, 반 층 참 서쪽과 서쪽 계단은 서쪽 계단 위에서, 3층 참은 출입구에서
	// 본다. 서쪽 계단 위 카메라에서는 두 계단 사이 벽이 반 층 참 동쪽을 가린다.
	const int32 Want = Feet < 615.0f ? 2 : (At.X > -440.0f ? 0 : 1);
	if (Want != ActiveShotCamera && ShotCameras.IsValidIndex(Want) && ShotCameras[Want].IsValid())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			PC->SetViewTargetWithBlend(ShotCameras[Want].Get(), 0.0f);
			// 밤 제목과 안내 글자가 화면을 덮지 않게 한다.
			if (AHUD* Hud = PC->GetHUD())
			{
				Hud->bShowHUD = false;
			}
		}
		ActiveShotCamera = Want;
		// 카메라를 바꾼 프레임은 아직 예전 화면이다.
		LastShotSeconds = TotalSeconds;
		return;
	}
	if (TotalSeconds - LastShotSeconds < 0.12)
	{
		return;
	}
	struct FMoment
	{
		const TCHAR* Name;
		bool bNow;
	};
	const FMoment Moments[] = {
		{TEXT("pursuit-east-flight"), Feet > 790.0f && Feet < 840.0f && At.X > -460.0f},
		{TEXT("pursuit-half-turn-east"), FMath::Abs(Feet - 750.0f) < 4.0f && At.X > -440.0f && At.Y > 10.0f},
		{TEXT("pursuit-half-turn-north"), FMath::Abs(Feet - 750.0f) < 4.0f && FMath::Abs(At.X + 465.0f) < 12.0f},
		{TEXT("pursuit-half-turn-west"), FMath::Abs(Feet - 750.0f) < 4.0f && At.X < -490.0f && At.Y > 10.0f},
		{TEXT("pursuit-west-flight"), Feet > 712.0f && Feet < 742.0f && At.X < -470.0f},
		{TEXT("pursuit-landing-west"), FMath::Abs(Feet - 600.0f) < 4.0f && At.X < -500.0f && At.Y < -270.0f},
		{TEXT("pursuit-landing-south"), FMath::Abs(Feet - 600.0f) < 4.0f && FMath::Abs(At.X + 465.0f) < 12.0f && At.Y < -300.0f},
		{TEXT("pursuit-landing-east"), FMath::Abs(Feet - 600.0f) < 4.0f && At.X > -430.0f && At.Y < -270.0f},
	};
	for (const FMoment& Moment : Moments)
	{
		if (Moment.bNow && !ShotsTaken.Contains(FName(Moment.Name)))
		{
			Shoot(Moment.Name);
			return;
		}
	}
}

void AIGListenerPursuitProbe::Track(const float DeltaSeconds)
{
	AIGListenerEntity* Listener = Entity.Get();
	if (!Listener)
	{
		return;
	}
	const FVector Location = Listener->GetActorLocation();
	LastTickDistance = bHasLast ? FVector::Dist(Location, LastLocation) : 0.0f;
	if (bHasLast)
	{
		MaxStepPerTick = FMath::Max(MaxStepPerTick, LastTickDistance);
	}
	LastLocation = Location;
	bHasLast = true;
	const float Feet = FeetZ();
	LowestFeetZ = FMath::Min(LowestFeetZ, Feet);
	HighestFeetZ = FMath::Max(HighestFeetZ, Feet);

	if (AIGPrologueWorldScene::IsOnStairFlight(Location))
	{
		++StairSamples;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(IGPursuitFloorGap), false, Listener);
		if (AIGPlayerCharacter* Character = Player.Get())
		{
			Params.AddIgnoredActor(Character);
		}
		if (GetWorld()->LineTraceSingleByChannel(
				Hit, Location, Location - FVector(0.0f, 0.0f, 250.0f), ECC_Visibility, Params))
		{
			MaxFloorGapOnStairs = FMath::Max(MaxFloorGapOnStairs, FMath::Abs(Feet - Hit.ImpactPoint.Z));
		}
		if (const USkeletalMeshComponent* Mesh = Listener->FindComponentByClass<USkeletalMeshComponent>())
		{
			MaxBodyPitch = FMath::Max(MaxBodyPitch, FMath::Abs(Mesh->GetRelativeRotation().Pitch));
		}
	}

	TrackLogSeconds += DeltaSeconds;
	if (TrackLogSeconds >= 0.5f)
	{
		TrackLogSeconds = 0.0f;
		const USkeletalMeshComponent* Mesh = Listener->FindComponentByClass<USkeletalMeshComponent>();
		UE_LOG(LogIndieGame, Display, TEXT("PURSUIT_TRACK t=%.1f phase=%d state=%d at=%s pitch=%.1f"),
			TotalSeconds, Phase, static_cast<int32>(Listener->GetListenerState()),
			*Location.ToCompactString(), Mesh ? Mesh->GetRelativeRotation().Pitch : 0.0f);
	}
}

void AIGListenerPursuitProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	AIGListenerEntity* Listener = Entity.Get();
	AIGPlayerCharacter* Character = Player.Get();
	if (!Listener || !Character || !Noise.IsValid())
	{
		Check(false, TEXT("stage_actors"));
		Finish();
		return;
	}
	if (Phase >= 1)
	{
		Track(DeltaSeconds);
		UpdateShots();
	}
	const EIGListenerState State = Listener->GetListenerState();
	if (State == EIGListenerState::CaptureHold)
	{
		Check(false, TEXT("no_capture_during_probe"));
		Finish();
		return;
	}

	switch (Phase)
	{
	case 0:
		if (PhaseSeconds < 0.5f)
		{
			return;
		}
		// 밤2의 속도와 귀로 잰다. 관리실 붕괴가 밤2의 일이다.
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UIGMissingFloorNarrativeSubsystem* Narrative =
					GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
			{
				Narrative->SetNightIndex(2);
			}
		}
		Listener->RefreshNightTuning();
		bShots = FParse::Param(FCommandLine::Get(), TEXT("IGListenerPursuitShots"));
		Character->GetCharacterMovement()->StopMovementImmediately();
		Character->SetActorLocation(IGPursuitProbe::PlayerParked, false, nullptr, ETeleportType::TeleportPhysics);
		Listener->ParkForBeat(FVector(60.0f, -305.0f, AIGPrologueWorldScene::FourthFloorZ), 180.0f);
		// 밤2에 그녀는 관리실 문을 열고 들어가 있다. 열어 둔 문으로 그가 들어와 책상
		// 밑을 뒤지는지 본다.
		for (TActorIterator<AIGSwingDoor> It(GetWorld()); It; ++It)
		{
			if (It->GetName().Contains(TEXT("BoothDoor")))
			{
				It->ForceOpenState(true);
			}
		}
		Phase = 1;
		PhaseSeconds = 0.0f;
		break;

	case 1:
		if (PhaseSeconds >= 1.0f && PhaseSeconds - DeltaSeconds < 1.0f)
		{
			Report(IGPursuitProbe::CollapseAt, 0.95f);
		}
		if (PhaseSeconds >= 1.55f)
		{
			Report(IGPursuitProbe::CollapseAt + FVector(30.0f, 20.0f, 0.0f), 0.95f);
			SilenceStartedAt = TotalSeconds;
			Phase = 2;
			PhaseSeconds = 0.0f;
		}
		break;

	case 2:
		if (State == EIGListenerState::Chasing)
		{
			Check(true, TEXT("collapse_starts_chase"));
			Phase = 3;
			PhaseSeconds = 0.0f;
		}
		else if (PhaseSeconds > 2.0f)
		{
			Check(false, TEXT("collapse_starts_chase"));
			Finish();
		}
		break;

	case 3:
		if (FeetZ() < 60.0f)
		{
			UE_LOG(LogIndieGame, Display, TEXT("PURSUIT_DESCENT seconds=%.2f"), PhaseSeconds);
			Check(true, TEXT("descends_stairs_to_first_floor"));
			// 4층 복도에서 1층까지 길은 30 m 남짓이다. 벽 끝이나 문틀에 걸려 서지 않았다면
			// 추격 빠르기로 열 몇 초 안이다.
			Check(PhaseSeconds <= 16.0f, TEXT("descends_without_getting_stuck"));
			Phase = 4;
			PhaseSeconds = 0.0f;
		}
		else if (PhaseSeconds > 30.0f)
		{
			Check(false, TEXT("descends_stairs_to_first_floor"));
			Finish();
		}
		break;

	case 4:
		// 소리가 끊긴 뒤. 곧바로 순찰로 돌아가면 실패다. 둘레를 뒤져야 한다.
		if (State == EIGListenerState::Patrolling)
		{
			bReturnedToPatrolEarly = true;
		}
		if (State == EIGListenerState::Searching)
		{
			Check(!bReturnedToPatrolEarly, TEXT("no_patrol_before_search"));
			Phase = 5;
			PhaseSeconds = 0.0f;
		}
		else if (PhaseSeconds > 25.0f)
		{
			Check(false, TEXT("enters_search_after_losing_sound"));
			Finish();
		}
		break;

	case 5:
	{
		// 수색. 여러 자리에서 멈춰 귀를 대는지 본다.
		const float Speed = DeltaSeconds > 0.0f ? LastTickDistance / DeltaSeconds : 0.0f;
		if (State == EIGListenerState::Searching)
		{
			SearchingSeconds += DeltaSeconds;
		}
		StillSeconds = Speed < 5.0f ? StillSeconds + DeltaSeconds : 0.0f;
		if (StillSeconds >= 1.2f)
		{
			bool bNew = true;
			for (const FVector& Spot : PauseSpots)
			{
				if (FVector::Dist2D(Spot, Listener->GetActorLocation()) < 150.0f)
				{
					bNew = false;
				}
			}
			if (bNew)
			{
				PauseSpots.Add(Listener->GetActorLocation());
				UE_LOG(LogIndieGame, Display, TEXT("PURSUIT_SEARCH_PAUSE at=%s"),
					*Listener->GetActorLocation().ToCompactString());
			}
		}
		if (State == EIGListenerState::Patrolling && SearchingSeconds < 12.0f)
		{
			Check(false, TEXT("search_lasts_long_enough"));
			Finish();
			break;
		}
		if (PauseSpots.Num() >= 2 && SearchingSeconds >= 12.0f)
		{
			Check(true, TEXT("search_lasts_long_enough"));
			Check(PauseSpots.Num() >= 2, TEXT("search_listens_at_several_spots"));
			// 이제 그녀가 1층에서 계단을 뛰어 올라간다.
			ClimbNoises.Reset();
			for (int32 Floor = 0; Floor < 3; ++Floor)
			{
				TArray<FVector> Feet;
				AIGPrologueWorldScene::GetStairClimbFeet(Floor, Feet);
				for (int32 Index = Floor == 0 ? 0 : 1; Index < Feet.Num(); ++Index)
				{
					ClimbNoises.Add(Feet[Index] + FVector(0.0f, 0.0f, 96.0f));
				}
			}
			ClimbNoises.Add(FVector(-180.0f, -305.0f, AIGPrologueWorldScene::FourthFloorZ + 96.0f));
			ClimbNoises.Add(FVector(60.0f, -305.0f, AIGPrologueWorldScene::FourthFloorZ + 96.0f));
			ClimbNoiseIndex = 0;
			ClimbNoiseSeconds = 0.0f;
			Phase = 6;
			PhaseSeconds = 0.0f;
		}
		else if (PhaseSeconds > 60.0f)
		{
			Check(false, TEXT("search_listens_at_several_spots"));
			Finish();
		}
		break;
	}

	case 6:
		ClimbNoiseSeconds += DeltaSeconds;
		if (ClimbNoises.IsValidIndex(ClimbNoiseIndex) && ClimbNoiseSeconds >= 0.45f)
		{
			ClimbNoiseSeconds = 0.0f;
			// 철판 계단을 뛰는 발소리(0.62).
			Report(ClimbNoises[ClimbNoiseIndex], 0.62f);
			++ClimbNoiseIndex;
			SilenceStartedAt = TotalSeconds;
		}
		if (FeetZ() > AIGPrologueWorldScene::FourthFloorZ - 50.0f && !ClimbNoises.IsValidIndex(ClimbNoiseIndex))
		{
			UE_LOG(LogIndieGame, Display, TEXT("PURSUIT_ASCENT seconds=%.2f"), PhaseSeconds);
			Check(true, TEXT("follows_footsteps_up_to_fourth_floor"));
			Phase = 7;
			PhaseSeconds = 0.0f;
			bReturnedToPatrolEarly = false;
		}
		else if (PhaseSeconds > 45.0f)
		{
			Check(false, TEXT("follows_footsteps_up_to_fourth_floor"));
			Finish();
		}
		break;

	case 7:
		// 마지막 발소리 뒤 12초. 쫓던 쪽으로 더 가 보거나 뒤지고 있어야 한다.
		if (State == EIGListenerState::Patrolling)
		{
			bReturnedToPatrolEarly = true;
		}
		if (TotalSeconds - SilenceStartedAt >= 12.0f)
		{
			Check(!bReturnedToPatrolEarly, TEXT("does_not_give_up_on_silence"));
			Finish();
		}
		break;

	default:
		break;
	}
}

void AIGListenerPursuitProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Check(MaxStepPerTick < IGPursuitProbe::TeleportStep, TEXT("no_teleport"));
	Check(StairSamples > 30, TEXT("walked_on_stair_flights"));
	Check(MaxFloorGapOnStairs < IGPursuitProbe::StairFloorGapLimit, TEXT("body_stays_on_treads"));
	Check(MaxBodyPitch > 18.0f, TEXT("body_tilts_with_the_slope"));
	UE_LOG(LogIndieGame, Display,
		TEXT("PURSUIT_METRICS max_step=%.1f stair_samples=%d max_gap=%.1f max_pitch=%.1f feet_z=[%.0f,%.0f] pauses=%d"),
		MaxStepPerTick, StairSamples, MaxFloorGapOnStairs, MaxBodyPitch, LowestFeetZ, HighestFeetZ, PauseSpots.Num());
	UE_LOG(LogIndieGame, Display, TEXT("PURSUIT_PROBE %s failures=%d"),
		Failures == 0 ? TEXT("PASS") : TEXT("FAIL"), Failures);
	Phase = -1;
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Failures == 0 ? 0 : 1);
}
