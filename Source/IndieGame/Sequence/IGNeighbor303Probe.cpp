#include "Sequence/IGNeighbor303Probe.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGListenerGreyboxDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGStairNeighbor.h"
#include "Environment/IGStairSensorLights.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGSwingDoor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGNeighbor303Probe
{
	/** 403호 방 안, 현관을 보는 자리. 4층 바닥 기준. */
	const FVector HomeFeet(0.0f, -60.0f, 0.0f);
	/** 4층 복도, 403호 문에서 계단 쪽으로 몇 걸음 나온 자리. 4층 바닥 기준. */
	const FVector CorridorFeet(40.0f, -318.0f, 0.0f);
	/**
	 * 3층과 4층 사이, 반 층 참에서 4층으로 오르는 동쪽 계단의 한가운데. 계단 길은 디딤판
	 * 윗면 가운데를 잇는 경사라 그 위에 세운다. 월드 좌표다.
	 */
	const FVector StairFeet(-407.5f, -135.0f, 826.0f);
	const FVector StairLookAt(-407.5f, 40.0f, 760.0f);
	/** 4층 참, 그 계단을 내려다보는 자리. 여기서 걸어 내려가야 센서등이 그녀를 보고 켜진다. */
	const FVector LandingFeet(-407.5f, -265.0f, 900.0f);
	/** 303호가 3층에서 반 층 참으로 오르는 계단에 들어서면 그녀가 내려가기 시작한다. */
	constexpr float DescendWhenNeighborAbove = 640.0f;
	constexpr float DescendSeconds = 1.6f;
	constexpr float ArriveTimeoutSeconds = 45.0f;
	constexpr float LeaveTimeoutSeconds = 25.0f;
	constexpr float GoneTimeoutSeconds = 90.0f;
}

AIGNeighbor303Probe::AIGNeighbor303Probe()
{
	// 검사기는 매 프레임 303호의 자리와 단계를 잰다. -IGNeighbor303Probe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGNeighbor303Probe::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGListenerEntity* InListener,
	AIGListenerGreyboxDirector* InDirector)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
	Director = InDirector;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGNeighbor303Shots"));
	bOnStairs = FParse::Param(FCommandLine::Get(), TEXT("IGNeighbor303Stairs"));
}

void AIGNeighbor303Probe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("NEIGHBOR303_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGNeighbor303Probe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("NEIGHBOR303_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGNeighbor303Probe::Next(const int32 NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
	bActed = false;
}

void AIGNeighbor303Probe::Stand(const FVector& Feet, const FVector& LookAt)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !PC)
	{
		return;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(Feet + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr,
		ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	const FVector Eye = Character->GetActorLocation() + Character->GetEyeOffsetFromActor();
	PC->SetControlRotation((LookAt - Eye).Rotation());
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
	}
}

void AIGNeighbor303Probe::Shoot(const TCHAR* Name)
{
	if (!bShots)
	{
		return;
	}
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Neighbor303Review")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("NEIGHBOR303_SHOT %s"), *Path);
}

bool AIGNeighbor303Probe::SaidLine(const FText& Line) const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	return Hud && Hud->HasDialogueLineForTesting(Line.ToString());
}

void AIGNeighbor303Probe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 240.0f)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	using namespace IGNeighbor303Probe;
	using EStage = AIGStairNeighbor::EStage;
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	AIGPrologueWorldScene* Building = Scene.Get();
	AIGSwingDoor* HomeDoor = Building ? Building->GetHomeDoor() : nullptr;
	AIGStairSensorLights* Lamps = Building ? Building->GetStairSensorLights() : nullptr;
	const FVector Floor4(0.0f, 0.0f, AIGPrologueWorldScene::FourthFloorZ);
	AIGStairNeighbor* Walker = Neighbor.Get();
	const FName MetBeat(TEXT("Day.Neighbor303.Stairs"));
	const FText Hello = NSLOCTEXT("IGMissingFloor", "Neighbor303Hello", "403호 맞죠? 저 303호예요.");
	const FText Earplugs = NSLOCTEXT(
		"IGMissingFloor",
		"Neighbor303Earplugs",
		"저 여기 2년 살았어요. 작년 여름에도 새벽마다 위에서 쿵쿵거렸어요. 귀마개 끼고 잤죠.");

	if (Phase >= 1 && Phase <= 4 && !Walker)
	{
		Check(false, TEXT("neighbor303_still_exists"));
		Finish();
		return;
	}

	switch (Phase)
	{
	case 0:
	{
		if (PhaseSeconds < (bShots ? 12.0f : 1.0f))
		{
			return;
		}
		for (TActorIterator<AIGNightPhaseDirector> It(GetWorld()); It; ++It)
		{
			NightPhase = *It;
		}
		AIGListenerGreyboxDirector* Greybox = Director.Get();
		Check(Narrative && Greybox && NightPhase.IsValid() && HomeDoor && Lamps && Player.IsValid(),
			TEXT("neighbor303_actors_resolved"));
		if (!Narrative || !Greybox || !NightPhase.IsValid() || !HomeDoor || !Lamps || !Player.IsValid())
		{
			Finish();
			return;
		}
		if (Listener.IsValid())
		{
			Listener->SetDormant(true);
		}
		// 셋째 밤을 채우고 셋째 낮으로 넘어간다. 첫 신고 진실이 없으니 신고 문자 줄기는 돌지
		// 않는다. 303호는 디렉터의 검사용 입구로 바로 올려 보낸다.
		Narrative->SetNightIndex(3);
		if (NightPhase->IsHourActive())
		{
			NightPhase->CompleteNightGoal();
		}
		Next(9);
		break;
	}

	case 9:
	{
		if (NightPhase->IsHourActive() || NightPhase->IsDawnTransitionInProgress())
		{
			if (PhaseSeconds >= 40.0f)
			{
				Check(false, TEXT("day_three_is_day"));
				Finish();
			}
			return;
		}
		Check(Narrative->GetNightIndex() == 3, TEXT("day_three_is_day"));
		AIGListenerGreyboxDirector* Greybox = Director.Get();
		if (bOnStairs)
		{
			Stand(LandingFeet, StairLookAt);
		}
		else
		{
			HomeDoor->ForceOpenState(false);
			Stand(HomeFeet + Floor4, FVector(131.0f, -237.0f, 100.0f) + Floor4);
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			LampsBefore[Index] = Lamps->GetSwitchOnCount(Index);
		}
		Greybox->StartStairNeighborForTesting();
		Neighbor = Greybox->GetStairNeighborForTesting();
		Walker = Neighbor.Get();
		Check(Walker && Walker->GetStage() == EStage::Arriving
			&& FVector::Dist(Walker->GetFeet(), AIGStairNeighbor::GetStartFeet()) < 30.0f,
			TEXT("neighbor303_leaves_her_door"));
		if (!Walker)
		{
			Finish();
			return;
		}
		Next(1);
		break;
	}

	case 1:
		// 올라온다. 문 닫힌 403호 안에서는 못 보니 계단 목까지 와서 서야 한다. 계단에 서 있으면
		// 그 전에 마주친다.
		// 계단 쪽은 303호가 3층 위 계단에 들어서면 4층 참에서 걸어 내려간다. 걸음마다 옮겨야
		// 센서등이 움직임으로 본다.
		if (bOnStairs && DescendStartedAt < 0.0f && Walker->GetFeet().Z > DescendWhenNeighborAbove)
		{
			DescendStartedAt = TotalSeconds;
		}
		if (bOnStairs && DescendStartedAt >= 0.0f && TotalSeconds - DescendStartedAt <= DescendSeconds + DeltaSeconds)
		{
			const float Alpha = FMath::Clamp((TotalSeconds - DescendStartedAt) / DescendSeconds, 0.0f, 1.0f);
			Stand(FMath::Lerp(LandingFeet, StairFeet, Alpha), StairLookAt);
		}
		if (Walker->GetStage() == EStage::Arriving)
		{
			if (PhaseSeconds >= ArriveTimeoutSeconds)
			{
				Check(false, TEXT("neighbor303_arrives"));
				Finish();
			}
			return;
		}
		Check(Walker->GetStepCount() >= 10, TEXT("neighbor303_footsteps_while_climbing"));
		Check(Lamps->GetSwitchOnCount(2) > LampsBefore[2], TEXT("third_floor_sensor_lamp_sees_her"));
		if (bOnStairs)
		{
			const FVector Feet = Walker->GetFeet();
			Check(Walker->GetStage() == EStage::Talking
				&& Feet.Z > AIGPrologueWorldScene::GetStoreyFloorZ(2) + 60.0f
				&& Feet.Z < AIGPrologueWorldScene::FourthFloorZ - 60.0f,
				TEXT("neighbor303_meets_her_on_the_stairs"));
			Check(Narrative->HasBeatPlayed(MetBeat) && SaidLine(Hello) && SaidLine(Earplugs),
				TEXT("neighbor303_three_lines_on_the_stairs"));
			Next(3);
		}
		else
		{
			Check(Walker->GetStage() == EStage::Waiting
				&& FVector::Dist(Walker->GetFeet(), AIGStairNeighbor::GetWaitingFeet()) < 20.0f,
				TEXT("neighbor303_waits_at_fourth_floor_neck"));
			Check(Lamps->GetSwitchOnCount(3) > LampsBefore[3], TEXT("fourth_floor_sensor_lamp_sees_her"));
			Check(!Narrative->HasBeatPlayed(MetBeat), TEXT("no_talk_through_closed_door"));
			Next(2);
		}
		break;

	case 2:
		// 그녀가 문을 열고 복도로 나온다.
		if (!bActed && PhaseSeconds >= 1.0f)
		{
			bActed = true;
			HomeDoor->ForceOpenState(true);
			Stand(CorridorFeet + Floor4, AIGStairNeighbor::GetWaitingFeet() + FVector(0.0f, 0.0f, 150.0f));
		}
		if (bActed && Walker->GetStage() == EStage::Talking)
		{
			Check(Narrative->HasBeatPlayed(MetBeat) && SaidLine(Hello) && SaidLine(Earplugs),
				TEXT("neighbor303_three_lines_at_the_neck"));
			Next(3);
		}
		else if (PhaseSeconds >= 4.0f)
		{
			Check(false, TEXT("neighbor303_meets_her_at_the_neck"));
			Next(3);
		}
		break;

	case 3:
		if (PhaseSeconds >= 1.5f && PhaseSeconds - DeltaSeconds < 1.5f)
		{
			Shoot(bOnStairs ? TEXT("neighbor303-stairs") : TEXT("neighbor303-neck"));
		}
		if (Walker->GetStage() == EStage::Leaving)
		{
			Check(PhaseSeconds >= 9.0f, TEXT("neighbor303_stays_for_her_lines"));
			LowestZ = Walker->GetFeet().Z;
			Next(4);
		}
		else if (PhaseSeconds >= LeaveTimeoutSeconds)
		{
			Check(false, TEXT("neighbor303_leaves_after_talking"));
			Finish();
		}
		break;

	case 4:
		LowestZ = FMath::Min(LowestZ, Walker->GetFeet().Z);
		if (PhaseSeconds >= 1.6f && PhaseSeconds - DeltaSeconds < 1.6f)
		{
			Shoot(bOnStairs ? TEXT("neighbor303-stairs-leaving") : TEXT("neighbor303-leaving"));
		}
		// 디렉터는 1초마다 보고, 내려가기 시작한 3초 뒤에 속말을 민다.
		if (PhaseSeconds >= 5.5f && PhaseSeconds - DeltaSeconds < 5.5f)
		{
			Check(SaidLine(NSLOCTEXT("IGMissingFloor", "Neighbor303AfterThought", "작년 여름에도 들었대.")),
				TEXT("yudam_thinks_after_she_leaves"));
		}
		if (Walker->GetStage() == EStage::Gone)
		{
			Check(Walker->IsHidden() && !Walker->IsActorTickEnabled(), TEXT("neighbor303_gone_and_still"));
			Check(LowestZ < 20.0f, TEXT("neighbor303_walked_down_to_the_ground_floor"));
			Finish();
		}
		else if (PhaseSeconds >= GoneTimeoutSeconds)
		{
			Check(false, TEXT("neighbor303_leaves_the_building"));
			Finish();
		}
		break;

	default:
		break;
	}
}
