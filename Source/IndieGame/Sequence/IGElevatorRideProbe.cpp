#include "Sequence/IGElevatorRideProbe.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
#include "Entity/IGElevatorOverloadDirector.h"
#include "Interaction/IGElevator.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGPlayerCharacter.h"

namespace IGElevatorRideProbe
{
	/** 칸이 움직이는 동안 발과 칸 바닥 사이 틈의 상한. 캐릭터 이동은 바닥 위 2 cm 남짓에 뜬다. */
	constexpr float FloorGapLimit = 5.0f;
	/** 이보다 빨리 옮겨졌으면 걸음이나 칸의 움직임이 아니라 순간이동이다(cm/s). 걷기는 400,
	    줄이 미끄러질 때 칸은 420까지 낸다. */
	constexpr float TeleportSpeed = 720.0f;
	/** 1층에서 4층까지, 4층에서 1층까지 걸리는 시간의 하한. 분당 60 m 남짓이다. */
	constexpr float ThreeFloorSeconds = 7.0f;
	constexpr float CallTimeout = 30.0f;
	constexpr float WalkTimeout = 8.0f;
}

AIGElevatorRideProbe::AIGElevatorRideProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGElevatorRideProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("ELEVATOR_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGElevatorRideProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("ELEVATOR_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGElevatorRideProbe::PlaceAtLanding(const int32 Landing)
{
	AIGPlayerCharacter* Character = Player.Get();
	const AIGElevator* Lift = Elevator.Get();
	if (!Character || !Lift)
	{
		return;
	}
	const FVector Front = Lift->GetLandingFrontWorldLocation(Landing);
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(Front + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (APlayerController* PC = Controller.Get())
	{
		// 승강기 문을 본다.
		PC->SetControlRotation((Lift->GetCabFloorWorldLocation() - Front).GetSafeNormal2D().Rotation());
	}
	bHasLast = false;
}

void AIGElevatorRideProbe::WalkToward(const FVector& Target)
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
	const FVector Direction = (Target - Character->GetActorLocation()).GetSafeNormal2D();
	Character->AddMovementInput(Direction, 1.0f);
}

void AIGElevatorRideProbe::TrackRide(const float DeltaSeconds)
{
	AIGPlayerCharacter* Character = Player.Get();
	AIGElevator* Lift = Elevator.Get();
	if (!Character || !Lift)
	{
		return;
	}
	const FVector Location = Character->GetActorLocation();
	const float CabZ = Lift->GetCabFloorWorldLocation().Z;
	if (bHasLast)
	{
		const float Seconds = FMath::Max(DeltaSeconds, 1.0f / 240.0f);
		MaxPawnStep = FMath::Max(MaxPawnStep, FVector::Dist(Location, LastPawnLocation) / Seconds);
		MaxCabStep = FMath::Max(MaxCabStep, FMath::Abs(CabZ - LastCabZ) / Seconds);
	}
	LastPawnLocation = Location;
	LastCabZ = CabZ;
	bHasLast = true;
	if (!Lift->IsMoving())
	{
		return;
	}
	++RideSamples;
	const float Feet = Location.Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	MaxFloorGap = FMath::Max(MaxFloorGap, FMath::Abs(Feet - CabZ));
	const UPrimitiveComponent* Base = Cast<UPrimitiveComponent>(Character->GetMovementBaseObject());
	if (!Base || Base->GetOwner() != Lift || Character->GetCharacterMovement()->MovementMode != MOVE_Walking)
	{
		++OffBaseSamples;
	}
}

void AIGElevatorRideProbe::Tick(const float DeltaSeconds)
{
	using namespace IGElevatorRideProbe;
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 180.0f)
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
		for (TActorIterator<AIGElevator> It(GetWorld()); It; ++It)
		{
			Elevator = *It;
		}
		if (!Player.IsValid() || !Elevator.IsValid() || !Elevator->GetCabRoot())
		{
			if (PhaseSeconds > 30.0f)
			{
				Check(false, TEXT("stage_ready"));
				Finish();
			}
			return;
		}
		// 첫 장면이 걸어 둔 입력 잠금과 대사를 풀고 무대를 1층 칸으로 맞춘다.
		Controller->ResetIgnoreMoveInput();
		Controller->ResetIgnoreLookInput();
		Player->SetCameraMotionEnabled(false);
		Elevator->SetHourDead(false);
		Elevator->ResetForNewRide();
		bReview = FParse::Param(FCommandLine::Get(), TEXT("IGElevatorReview"));
		if (bReview)
		{
			Next(100);
			return;
		}
		bOverload = FParse::Param(FCommandLine::Get(), TEXT("IGElevatorOverloadProbe"));
		if (bOverload)
		{
			bLeave = FParse::Param(FCommandLine::Get(), TEXT("IGElevatorOverloadLeave"));
			bShots = FParse::Param(FCommandLine::Get(), TEXT("IGElevatorShots"));
			// 첫 밤을 지난 낮, 승강기는 한 번 타 본 뒤다.
			if (UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
			{
				Narrative->SetHourSealed(false);
				Narrative->SetNightIndex(1);
				Narrative->MarkBeatPlayed(AIGElevatorOverloadDirector::FirstRideBeatId);
				Check(!Narrative->HasBeatPlayed(AIGElevatorOverloadDirector::BeatId), TEXT("overload_not_yet_played"));
			}
			PlaceAtLanding(3);
			Next(200);
			return;
		}
		PlaceAtLanding(3);
		Next(1);
		return;
	}
	if (bReview)
	{
		TickReview(DeltaSeconds);
		return;
	}
	if (bOverload)
	{
		TickOverload(DeltaSeconds);
		return;
	}
	AIGElevator* Lift = Elevator.Get();
	AIGPlayerCharacter* Character = Player.Get();
	if (!Lift || !Character)
	{
		Check(false, TEXT("stage_alive"));
		Finish();
		return;
	}
	TrackRide(DeltaSeconds);

	switch (Phase)
	{
	case 1:
		if (PhaseSeconds > 0.5f)
		{
			Check(Lift->GetStoppedLanding() == 0 && Lift->AreDoorsClosed(), TEXT("cab_waits_on_ground_floor"));
			Lift->Press(EIGElevatorButtonKind::HallCall, 3, Character);
			MaxCabStep = 0.0f;
			RideSeconds = 0.0f;
			Next(2);
		}
		break;
	case 2:
		// 빈 칸이 1층에서 4층까지 올라온다.
		RideSeconds += DeltaSeconds;
		if (Lift->GetStoppedLanding() == 3 && Lift->AreDoorsFullyOpen())
		{
			UE_LOG(LogIndieGame, Display, TEXT("ELEVATOR_CALL seconds=%.2f max_cab_speed=%.1f"), RideSeconds, MaxCabStep);
			Check(RideSeconds > ThreeFloorSeconds, TEXT("call_travels_through_the_shaft"));
			Check(MaxCabStep < 130.0f, TEXT("cab_moves_without_jumps"));
			Next(3);
		}
		else if (PhaseSeconds > CallTimeout)
		{
			Check(false, TEXT("call_arrives"));
			Finish();
		}
		break;
	case 3:
		WalkToward(Lift->GetCabFloorWorldLocation());
		if (Lift->IsPawnInsideCab(Character)
			&& FVector::Dist2D(Character->GetActorLocation(), Lift->GetCabFloorWorldLocation()) < 35.0f)
		{
			Check(true, TEXT("walk_into_cab_4f"));
			Next(4);
		}
		else if (PhaseSeconds > WalkTimeout)
		{
			Check(false, TEXT("walk_into_cab_4f"));
			Finish();
		}
		break;
	case 4:
		if (PhaseSeconds > 0.3f)
		{
			Lift->Press(EIGElevatorButtonKind::Floor, 0, Character);
			MaxFloorGap = 0.0f;
			MaxPawnStep = 0.0f;
			MaxCabStep = 0.0f;
			RideSamples = 0;
			OffBaseSamples = 0;
			RideSeconds = 0.0f;
			Next(5);
		}
		break;
	case 5:
		if (Lift->IsMoving())
		{
			RideSeconds += DeltaSeconds;
		}
		if (Lift->GetStoppedLanding() == 0 && Lift->AreDoorsFullyOpen())
		{
			const float Feet = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			UE_LOG(LogIndieGame, Display,
				TEXT("ELEVATOR_RIDE down seconds=%.2f samples=%d max_gap=%.2f max_speed=%.1f max_cab_speed=%.1f off_base=%d feet=%.1f"),
				RideSeconds, RideSamples, MaxFloorGap, MaxPawnStep, MaxCabStep, OffBaseSamples, Feet);
			Check(RideSeconds > ThreeFloorSeconds, TEXT("ride_down_takes_real_time"));
			Check(RideSamples > 200, TEXT("ride_down_sampled"));
			Check(MaxFloorGap < FloorGapLimit, TEXT("rider_stands_on_cab_floor"));
			Check(OffBaseSamples == 0, TEXT("rider_based_on_cab"));
			Check(MaxPawnStep < TeleportSpeed, TEXT("rider_never_teleports"));
			Check(FMath::Abs(Feet) < 6.0f, TEXT("arrives_at_lobby_level"));
			Next(6);
		}
		else if (PhaseSeconds > CallTimeout)
		{
			Check(false, TEXT("ride_down_arrives"));
			Finish();
		}
		break;
	case 6:
		WalkToward(Lift->GetLandingFrontWorldLocation(0));
		if (Character->GetActorLocation().X < 650.0f)
		{
			Check(!Lift->IsPawnInsideCab(Character), TEXT("walk_out_to_lobby"));
			Next(7);
		}
		else if (PhaseSeconds > WalkTimeout)
		{
			Check(false, TEXT("walk_out_to_lobby"));
			Finish();
		}
		break;
	case 7:
		if (PhaseSeconds < 0.1f)
		{
			// 그 시간. 버튼은 눌리지만 칸은 선 자리에서 문을 닫고 있다.
			Lift->SetHourDead(true);
			HeldCabZ = Lift->GetCabZ();
			Lift->Press(EIGElevatorButtonKind::HallCall, 1, Character);
		}
		else if (PhaseSeconds > 3.0f)
		{
			Check(FMath::IsNearlyEqual(Lift->GetCabZ(), HeldCabZ, 0.5f) && Lift->AreDoorsClosed() && !Lift->IsMoving(),
				TEXT("hour_dead_cab_stays"));
			Lift->SetHourDead(false);
			PlaceAtLanding(1);
			Next(8);
		}
		break;
	case 8:
		if (PhaseSeconds > 0.5f)
		{
			Lift->Press(EIGElevatorButtonKind::HallCall, 1, Character);
			Next(9);
		}
		break;
	case 9:
		if (Lift->GetStoppedLanding() == 1 && Lift->AreDoorsFullyOpen())
		{
			Next(10);
		}
		else if (PhaseSeconds > CallTimeout)
		{
			Check(false, TEXT("call_arrives_2f"));
			Finish();
		}
		break;
	case 10:
		WalkToward(Lift->GetCabFloorWorldLocation());
		if (Lift->IsPawnInsideCab(Character)
			&& FVector::Dist2D(Character->GetActorLocation(), Lift->GetCabFloorWorldLocation()) < 35.0f)
		{
			Check(true, TEXT("walk_into_cab_2f"));
			Lift->Press(EIGElevatorButtonKind::Floor, 2, Character);
			MaxFloorGap = 0.0f;
			MaxPawnStep = 0.0f;
			RideSamples = 0;
			OffBaseSamples = 0;
			Next(11);
		}
		else if (PhaseSeconds > WalkTimeout)
		{
			Check(false, TEXT("walk_into_cab_2f"));
			Finish();
		}
		break;
	case 11:
		if (Lift->GetStoppedLanding() == 2 && Lift->AreDoorsFullyOpen())
		{
			UE_LOG(LogIndieGame, Display,
				TEXT("ELEVATOR_RIDE up samples=%d max_gap=%.2f max_speed=%.1f off_base=%d"),
				RideSamples, MaxFloorGap, MaxPawnStep, OffBaseSamples);
			Check(RideSamples > 60 && MaxFloorGap < FloorGapLimit && OffBaseSamples == 0, TEXT("ride_up_on_cab_floor"));
			Check(MaxPawnStep < TeleportSpeed, TEXT("ride_up_never_teleports"));
			Next(12);
		}
		else if (PhaseSeconds > CallTimeout)
		{
			Check(false, TEXT("ride_up_arrives"));
			Finish();
		}
		break;
	case 12:
		WalkToward(Lift->GetLandingFrontWorldLocation(2));
		if (Character->GetActorLocation().X < 650.0f)
		{
			const float Feet = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			UE_LOG(LogIndieGame, Display, TEXT("ELEVATOR_EXIT 3f feet=%.1f at=%s"), Feet, *Character->GetActorLocation().ToCompactString());
			Check(FMath::Abs(Feet - AIGElevator::GetLandingZ(2)) < 6.0f, TEXT("walk_out_on_third_floor"));
			Finish();
		}
		else if (PhaseSeconds > WalkTimeout)
		{
			Check(false, TEXT("walk_out_on_third_floor"));
			Finish();
		}
		break;
	default:
		break;
	}
}

void AIGElevatorRideProbe::Shoot(const TCHAR* Name)
{
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ElevatorReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("ELEVATOR_SHOT %s"), *Path);
}

void AIGElevatorRideProbe::TickReview(const float DeltaSeconds)
{
	AIGElevator* Lift = Elevator.Get();
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Controller.Get();
	if (!Lift || !Character || !PC)
	{
		Finish();
		return;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const auto Pose = [&](const FVector& Feet, const float Yaw, const float Pitch)
	{
		Character->SetActorLocation(Feet + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Character->GetCharacterMovement()->StopMovementImmediately();
		PC->SetControlRotation(FRotator(Pitch, Yaw, 0.0f));
	};
	const FVector CabCenter = Lift->GetCabFloorWorldLocation();
	// 한 장마다: 자리를 잡고 1.6초 노출이 가라앉기를 기다렸다가 찍고, 0.4초 뒤 다음 장.
	struct FShot
	{
		const TCHAR* Name;
		int32 CabLanding;
	};
	static const FShot Shots[] = {
		{TEXT("lift-4f-corridor"), 0},
		{TEXT("lift-4f-open"), 3},
		{TEXT("lift-cab-mirror"), 3},
		{TEXT("lift-cab-cop"), 3},
		{TEXT("lift-cab-crowd"), 3},
		{TEXT("lift-lobby"), 0},
		{TEXT("lift-2f-corridor"), 1},
	};
	if (ShotIndex >= UE_ARRAY_COUNT(Shots))
	{
		Finish();
		return;
	}
	const FShot& Shot = Shots[ShotIndex];
	if (Phase == 100)
	{
		// 칸을 그 층으로 부른다. 이미 서 있으면 문만 연다.
		if (Lift->GetStoppedLanding() != Shot.CabLanding)
		{
			Lift->Press(EIGElevatorButtonKind::HallCall, Shot.CabLanding, Character);
		}
		Phase = 101;
		PhaseSeconds = 0.0f;
		return;
	}
	if (Phase == 101)
	{
		const bool bThere = Lift->GetStoppedLanding() == Shot.CabLanding;
		if (!bThere && PhaseSeconds < 40.0f)
		{
			return;
		}
		const FString Name(Shot.Name);
		const float LandingZ = AIGElevator::GetLandingZ(Shot.CabLanding);
		if (Name == TEXT("lift-4f-corridor"))
		{
			Pose(FVector(380.0f, -305.0f, 900.0f), 0.0f, -2.0f);
		}
		else if (Name == TEXT("lift-4f-open"))
		{
			Lift->Press(EIGElevatorButtonKind::HallCall, 3, Character);
			Pose(FVector(560.0f, -300.0f, 900.0f), 0.0f, -4.0f);
		}
		else if (Name == TEXT("lift-cab-mirror"))
		{
			Lift->SetReflection(EIGElevatorReflection::Normal);
			Pose(FVector(CabCenter.X - 30.0f, CabCenter.Y + 12.0f, CabCenter.Z), 0.0f, -6.0f);
		}
		else if (Name == TEXT("lift-cab-cop"))
		{
			Pose(FVector(CabCenter.X + 25.0f, CabCenter.Y + 20.0f, CabCenter.Z), -140.0f, -12.0f);
		}
		else if (Name == TEXT("lift-cab-crowd"))
		{
			// 만원 사건의 거울. 뒷모습과 거울에만 서 있는 사람 다섯.
			Lift->SetReflection(EIGElevatorReflection::Back);
			Lift->AddReflectionFigure(FVector(-48.0f, 30.0f, 0.0f), 85.0f);
			Lift->AddReflectionFigure(FVector(46.0f, 28.0f, 0.0f), 95.0f);
			Lift->AddReflectionFigure(FVector(-20.0f, 42.0f, 0.0f), 92.0f);
			Lift->AddReflectionFigure(FVector(22.0f, 44.0f, 0.0f), 88.0f);
			Lift->AddReflectionFigure(FVector(-52.0f, -18.0f, 0.0f), 100.0f);
			Lift->ScriptSetFullLamp(true);
			Pose(FVector(CabCenter.X - 30.0f, CabCenter.Y + 12.0f, CabCenter.Z), 0.0f, -6.0f);
		}
		else if (Name == TEXT("lift-lobby"))
		{
			Lift->ClearReflectionFigures();
			Lift->SetReflection(EIGElevatorReflection::Normal);
			Lift->ScriptSetFullLamp(false);
			Pose(FVector(520.0f, -300.0f, LandingZ), 0.0f, -3.0f);
		}
		else if (Name == TEXT("lift-2f-corridor"))
		{
			Pose(FVector(150.0f, -305.0f, LandingZ), 0.0f, -2.0f);
		}
		Phase = 102;
		PhaseSeconds = 0.0f;
		return;
	}
	if (Phase == 102)
	{
		// 문이 열려 있어야 하는 장은 문을 다시 붙잡아 둔다.
		if (FString(Shot.Name).StartsWith(TEXT("lift-cab")) && PhaseSeconds < 0.1f)
		{
			Lift->Press(EIGElevatorButtonKind::Open, 0, Character);
		}
		if (PhaseSeconds > 1.6f)
		{
			Shoot(Shot.Name);
			Phase = 103;
			PhaseSeconds = 0.0f;
		}
		return;
	}
	if (Phase == 103 && PhaseSeconds > 0.4f)
	{
		++ShotIndex;
		Phase = 100;
		PhaseSeconds = 0.0f;
	}
}

void AIGElevatorRideProbe::TickOverload(const float DeltaSeconds)
{
	using namespace IGElevatorRideProbe;
	AIGElevator* Lift = Elevator.Get();
	AIGPlayerCharacter* Character = Player.Get();
	if (!Lift || !Character)
	{
		Check(false, TEXT("stage_alive"));
		Finish();
		return;
	}
	TrackRide(DeltaSeconds);
	const auto Next = [this](const int32 NewPhase)
	{
		Phase = NewPhase;
		PhaseSeconds = 0.0f;
	};
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>();
	switch (Phase)
	{
	case 200:
		if (PhaseSeconds > 0.5f)
		{
			Lift->Press(EIGElevatorButtonKind::HallCall, 3, Character);
			Next(201);
		}
		break;
	case 201:
		if (Lift->GetStoppedLanding() == 3 && Lift->AreDoorsFullyOpen())
		{
			Next(202);
		}
		else if (PhaseSeconds > CallTimeout)
		{
			Check(false, TEXT("overload_call_arrives"));
			Finish();
		}
		break;
	case 202:
		WalkToward(Lift->GetCabFloorWorldLocation());
		if (Lift->IsPawnInsideCab(Character)
			&& FVector::Dist2D(Character->GetActorLocation(), Lift->GetCabFloorWorldLocation()) < 35.0f)
		{
			Lift->Press(EIGElevatorButtonKind::Floor, 0, Character);
			MaxFloorGap = 0.0f;
			MaxPawnStep = 0.0f;
			RideSamples = 0;
			OffBaseSamples = 0;
			MinCabZ = MaxCabZ = Lift->GetCabZ();
			Next(203);
		}
		else if (PhaseSeconds > WalkTimeout)
		{
			Check(false, TEXT("overload_walk_in"));
			Finish();
		}
		break;
	case 203:
		if (PhaseSeconds > 0.3f)
		{
			Check(Lift->IsScripted(), TEXT("overload_begins_on_floor_button"));
			Check(Narrative && Narrative->HasBeatPlayed(AIGElevatorOverloadDirector::BeatId), TEXT("overload_marked_once"));
			Next(204);
		}
		break;
	case 204:
		// 부저가 우는 동안. 거울이 등을 보이고 사람이 는다.
		bSawBack |= Lift->GetReflection() == EIGElevatorReflection::Back;
		MaxFigures = FMath::Max(MaxFigures, Lift->GetReflectionFigures().Num());
		if (bShots && ShotsTaken == 0 && PhaseSeconds > 1.5f)
		{
			Shoot(TEXT("overload-buzzer-doors"));
			++ShotsTaken;
		}
		if (bShots && ShotsTaken == 1 && Lift->GetReflectionFigures().Num() >= 3)
		{
			Shoot(TEXT("overload-mirror-crowd"));
			++ShotsTaken;
		}
		if (bLeave && PhaseSeconds > 4.0f)
		{
			WalkToward(Lift->GetLandingFrontWorldLocation(3));
			if (Character->GetActorLocation().X < 640.0f)
			{
				Check(bSawBack && MaxFigures >= 1, TEXT("overload_mirror_crowds"));
				Next(210);
			}
		}
		else if (!bLeave && Lift->IsMoving())
		{
			// 거울을 보고 있으면 느리게 는다. 그래도 버티는 10초 안에 셋은 선다.
			Check(bSawBack && MaxFigures >= 3, TEXT("overload_mirror_crowds"));
			Check(Lift->AreDoorsClosed() && Lift->IsPawnInsideCab(Character), TEXT("overload_carries_her"));
			Next(205);
		}
		else if (PhaseSeconds > 20.0f)
		{
			Check(false, TEXT("overload_decides"));
			Finish();
		}
		break;
	case 205:
		// 4층을 지나 「5」까지 오른다. 문이 열리면 거울은 비어 있다.
		MaxCabZ = FMath::Max(MaxCabZ, Lift->GetCabZ());
		bSawAbsent |= Lift->GetReflection() == EIGElevatorReflection::Absent;
		bDoorsOpenAtFive |= Lift->AreDoorsFullyOpen() && FMath::IsNearlyEqual(Lift->GetCabZ(), AIGElevator::OverrunZ, 2.0f);
		if (bShots && ShotsTaken == 2 && Lift->GetCabZ() > AIGElevator::GetLandingZ(3) + 150.0f)
		{
			// 층 표시가 5를 가리킨다. 문 쪽을 본다.
			if (APlayerController* PC = Controller.Get())
			{
				PC->SetControlRotation(FRotator(-4.0f, 180.0f, 0.0f));
			}
			Shoot(TEXT("overload-display-five"));
			++ShotsTaken;
		}
		if (bShots && ShotsTaken == 3 && bDoorsOpenAtFive)
		{
			Shoot(TEXT("overload-five-wall"));
			++ShotsTaken;
		}
		if (bShots && ShotsTaken == 4 && bDoorsOpenAtFive && Lift->GetReflection() == EIGElevatorReflection::Absent)
		{
			if (APlayerController* PC = Controller.Get())
			{
				PC->SetControlRotation(FRotator(-6.0f, 0.0f, 0.0f));
			}
			++ShotsTaken;
		}
		else if (bShots && ShotsTaken == 5)
		{
			Shoot(TEXT("overload-empty-mirror"));
			++ShotsTaken;
		}
		if (bDoorsOpenAtFive && Lift->AreDoorsClosed() && Lift->IsMoving())
		{
			MinCabZ = Lift->GetCabZ();
			Next(206);
		}
		else if (PhaseSeconds > 40.0f)
		{
			Check(false, TEXT("overload_reaches_five"));
			Finish();
		}
		break;
	case 206:
		// 줄이 미끄러진 깊이만 잰다. 떨어지는 데 0.5초가 안 걸리고, 그 뒤 1.6초를
		// 서 있다가 누른 층으로 내려간다.
		if (PhaseSeconds < 1.2f)
		{
			MinCabZ = FMath::Min(MinCabZ, Lift->GetCabZ());
		}
		if (Lift->GetStoppedLanding() == 0 && Lift->AreDoorsFullyOpen())
		{
			const float Feet = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			UE_LOG(LogIndieGame, Display,
				TEXT("ELEVATOR_OVERLOAD_RIDE max_cab=%.1f min_after_five=%.1f figures=%d gap=%.2f speed=%.1f off_base=%d feet=%.1f"),
				MaxCabZ, MinCabZ, MaxFigures, MaxFloorGap, MaxPawnStep, OffBaseSamples, Feet);
			Check(FMath::IsNearlyEqual(MaxCabZ, AIGElevator::OverrunZ, 1.0f), TEXT("overload_goes_past_fourth"));
			Check(MinCabZ < AIGElevator::OverrunZ - 30.0f && MinCabZ > AIGElevator::OverrunZ - 60.0f, TEXT("overload_cable_slips"));
			Check(bDoorsOpenAtFive && bSawAbsent, TEXT("overload_opens_on_the_wall_mirror_empty"));
			Check(MaxFloorGap < 12.0f && OffBaseSamples == 0, TEXT("overload_rider_stays_on_floor"));
			Check(MaxPawnStep < TeleportSpeed, TEXT("overload_never_teleports"));
			Check(FMath::Abs(Feet) < 6.0f, TEXT("overload_delivers_pressed_floor"));
			Next(207);
		}
		else if (PhaseSeconds > 40.0f)
		{
			Check(false, TEXT("overload_returns"));
			Finish();
		}
		break;
	case 207:
		if (PhaseSeconds > 2.0f)
		{
			Check(!Lift->IsScripted(), TEXT("overload_releases_the_lift"));
			Check(Lift->GetReflection() == EIGElevatorReflection::Absent, TEXT("mirror_stays_empty_until_she_leaves"));
			Next(208);
		}
		break;
	case 208:
		WalkToward(Lift->GetLandingFrontWorldLocation(0));
		if (Character->GetActorLocation().X < 640.0f && PhaseSeconds > 0.5f)
		{
			Next(209);
		}
		else if (PhaseSeconds > WalkTimeout)
		{
			Check(false, TEXT("overload_walk_out"));
			Finish();
		}
		break;
	case 209:
		if (PhaseSeconds > 0.5f)
		{
			Check(Lift->GetReflection() == EIGElevatorReflection::Normal, TEXT("mirror_returns_after_exit"));
			Finish();
		}
		break;
	case 210:
		// 내린 쪽. 빈 칸이 「5」로 올라갔다가 돌아와 문을 연다.
		MaxCabZ = FMath::Max(MaxCabZ, Lift->GetCabZ());
		if (MaxCabZ >= AIGElevator::OverrunZ - 1.0f && Lift->GetStoppedLanding() == 3 && Lift->AreDoorsFullyOpen())
		{
			const float Feet = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Check(FMath::Abs(Feet - AIGElevator::GetLandingZ(3)) < 6.0f, TEXT("left_behind_on_fourth"));
			Check(Lift->GetReflectionFigures().Num() == 0, TEXT("left_behind_mirror_cleared"));
			Next(211);
		}
		else if (PhaseSeconds > 45.0f)
		{
			Check(false, TEXT("empty_cab_goes_to_five_and_back"));
			Finish();
		}
		break;
	case 211:
		if (PhaseSeconds > 2.5f)
		{
			Check(!Lift->IsScripted(), TEXT("left_behind_releases_the_lift"));
			Finish();
		}
		break;
	default:
		break;
	}
}
