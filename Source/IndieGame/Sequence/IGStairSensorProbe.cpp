#include "Sequence/IGStairSensorProbe.h"

#include "Components/CapsuleComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "Environment/IGStairSensorLights.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "EngineUtils.h"
#include "Interaction/IGReadableNote.h"
#include "IndieGame.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGStairSensorProbe
{
	// 2층 참 서쪽 계단 발치. 북쪽으로 걸으면 2.5층 참으로 오르는 계단을 탄다.
	const FVector SecondFloorStart(-520.0f, -300.0f, 300.0f);
	// 3층 참 서쪽. 동쪽으로 몇 걸음 걸어 2.5층 참으로 내려가는 계단 머리에 서고, 남쪽
	// 창을 보고 선다. 계단 아래는 등 뒤다. 서쪽에 서면 가운데 벽이 계단 아래를 가린다.
	const FVector ThirdFloorStart(-520.0f, -320.0f, 600.0f);
}

AIGStairSensorProbe::AIGStairSensorProbe()
{
	// 검사기는 매 프레임 그녀를 걷게 하고 등과 몸을 잰다. -IGStairSensorProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGStairSensorProbe::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGNightThreatDirector* InThreats,
	AIGNightPhaseDirector* InNightPhase,
	AIGListenerEntity* InListener)
{
	Scene = InScene;
	Player = InPlayer;
	Threats = InThreats;
	NightPhase = InNightPhase;
	Listener = InListener;
	Sensors = InScene ? InScene->GetStairSensorLights() : nullptr;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGStairSensorShots"));
}

void AIGStairSensorProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("STAIR_SENSOR_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGStairSensorProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("STAIR_SENSOR_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGStairSensorProbe::Shoot(const TCHAR* Name)
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
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StairSensorReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("STAIR_SENSOR_SHOT %s"), *Path);
}

FVector AIGStairSensorProbe::Feet() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		return FVector::ZeroVector;
	}
	return Character->GetActorLocation()
		- FVector(0.0f, 0.0f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

FVector AIGStairSensorProbe::Eye() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	return Character ? Character->GetActorLocation() + Character->GetEyeOffsetFromActor() : FVector::ZeroVector;
}

void AIGStairSensorProbe::Place(const FVector& InFeet, const float Yaw)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !PC)
	{
		return;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(InFeet + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	PC->SetControlRotation(FRotator(-6.0f, Yaw, 0.0f));
}

void AIGStairSensorProbe::Walk(const FVector& Direction)
{
	AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		return;
	}
	if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
	{
		PC->ResetIgnoreMoveInput();
	}
	Character->AddMovementInput(Direction.GetSafeNormal2D(), 1.0f);
}

void AIGStairSensorProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 150.0f)
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

	AIGPrologueWorldScene* Building = Scene.Get();
	AIGPlayerCharacter* Character = Player.Get();
	AIGNightThreatDirector* Threat = Threats.Get();
	AIGStairSensorLights* Lights = Sensors.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Building || !Character || !Threat || !Lights || !PC)
	{
		if (PhaseSeconds > 20.0f)
		{
			Check(false, TEXT("stage_ready"));
			Finish();
		}
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;

	switch (Phase)
	{
	case 0:
	{
		const AIGNightPhaseDirector* Hour = NightPhase.Get();
		const bool bStanding = Character->GetCharacterMovement()->MovementMode == MOVE_Walking;
		// 찍을 때는 밤이 열리며 뜨는 「첫째 밤」 자막과 검은 화면이 걷힐 때까지 기다린다.
		const float ReadySeconds = bShots ? 12.0f : 2.0f;
		if (!Hour || !Hour->IsHourActive() || !bStanding || PhaseSeconds < ReadySeconds)
		{
			if (PhaseSeconds > 40.0f)
			{
				Check(false, TEXT("night_one_ready"));
				Finish();
			}
			break;
		}
		Check(Lights->GetLampCount() == 4, TEXT("four_landing_lamps"));
		{
			// 1층 엘리베이터 옆 벽의 관리실 공지. 밤에 계단 등만 켜지는 이유가 거기 적혀 있다.
			bool bNoticePosted = false;
			for (TActorIterator<AIGReadableNote> It(GetWorld()); It; ++It)
			{
				bNoticePosted |= FVector::Dist(It->GetActorLocation(), FVector(699.95f, -172.0f, 152.0f)) < 5.0f
					&& It->GetBodyLines().Num() >= 3;
			}
			Check(bNoticePosted, TEXT("lights_out_notice_posted"));
		}
		Check(Narrative && Narrative->GetNightIndex() == 1, TEXT("night_one"));
		// 위층 사람은 4층에서 재운다. 그가 계단 소리를 듣고 내려오면 3층 참의 소개가
		// 긴장 예산에 막힌다. 그건 맞는 동작이지만 이 검사가 볼 것은 아니다.
		if (AIGListenerEntity* Upstairs = Listener.Get())
		{
			Upstairs->SetDormant(true);
		}
		PC->ResetIgnoreMoveInput();
		PC->ResetIgnoreLookInput();
		Character->SetCameraMotionEnabled(false);
		if (UIGFlashlightComponent* Torch = Character->GetFlashlight())
		{
			// 손전등을 켜 두면 어둠이 쌓이지 않는다. 어둑시니는 3층 참의 소개로만 선다.
			Torch->SetAvailable(true);
			Torch->SetOn(!bShots);
		}
		TArray<FVector> Climb;
		AIGPrologueWorldScene::GetStairClimbFeet(1, Climb);
		Check(Climb.Num() > 5 && Lights->FindLampForFeet(Climb[4]) == INDEX_NONE
				&& Lights->FindLampForFeet(Climb[5]) == INDEX_NONE,
			TEXT("half_landing_stays_dark"));
		Check(Lights->FindLampForFeet(AIGPrologueWorldScene::GetStairDoorwayFeet(3)) == INDEX_NONE,
			TEXT("corridor_doorway_is_outside_the_sensor"));
		Check(Lights->FindLampForFeet(IGStairSensorProbe::SecondFloorStart) == 1,
			TEXT("second_floor_landing_is_lamp_one"));
		Place(IGStairSensorProbe::SecondFloorStart, 90.0f);
		Next(1);
		break;
	}
	case 1:
		// 옮긴 몸이 바닥에 앉을 때까지. 순간이동은 움직임으로 치지 않는다.
		if (PhaseSeconds > 0.8f)
		{
			Check(!Lights->IsLampOn(1), TEXT("lamp_off_while_she_has_not_moved"));
			SwitchCountBefore = Lights->GetSwitchOnCount(1);
			Next(2);
		}
		break;
	case 2:
		Walk(FVector(0.0f, 1.0f, 0.0f));
		if (PhaseSeconds > 0.6f)
		{
			UE_LOG(LogIndieGame, Display, TEXT("STAIR_SENSOR_WALK feet=%s lamp=%d"),
				*Feet().ToCompactString(), Lights->FindLampForFeet(Feet()));
			Check(Lights->IsLampOn(1), TEXT("lamp_turns_on_when_she_walks"));
			Check(Lights->GetSwitchOnCount(1) == SwitchCountBefore + 1, TEXT("lamp_switches_once"));
			Check(Lights->GetLampIntensity(1) > 500.0f, TEXT("lamp_gives_light"));
			Check(!Lights->IsLampOn(0) && !Lights->IsLampOn(2) && !Lights->IsLampOn(3),
				TEXT("other_landings_stay_dark"));
			Next(3);
		}
		break;
	case 3:
		// 가만히 선다. 9초 동안은 켜져 있고 그 뒤에 꺼진다.
		if (PhaseSeconds > AIGStairSensorLights::HoldSeconds - 1.0f && PhaseSeconds - DeltaSeconds <= AIGStairSensorLights::HoldSeconds - 1.0f)
		{
			Check(Lights->IsLampOn(1), TEXT("lamp_holds_while_she_stands"));
		}
		if (PhaseSeconds > AIGStairSensorLights::HoldSeconds + 0.6f)
		{
			Check(!Lights->IsLampOn(1), TEXT("lamp_goes_out_after_hold"));
			Check(Lights->GetLampIntensity(1) <= 0.0f, TEXT("dark_lamp_gives_no_light"));
			Next(4);
		}
		break;
	case 4:
		// 1층 공용 차단기를 내리면 센서등도 죽는다.
		Walk(FVector(0.0f, -1.0f, 0.0f));
		if (PhaseSeconds > 0.4f)
		{
			Check(Lights->IsLampOn(1), TEXT("lamp_back_on_when_she_moves"));
			Building->SetCommonInspectionLightsEnabled(false);
			Check(!Lights->IsAnyLampOn(), TEXT("common_breaker_kills_sensors"));
			Check(Building->AreCommonInspectionLightsOff(), TEXT("common_lights_all_off"));
			Next(5);
		}
		break;
	case 5:
		Walk(FVector(0.0f, PhaseSeconds < 0.4f ? 1.0f : -1.0f, 0.0f));
		if (PhaseSeconds > 0.8f)
		{
			Check(!Lights->IsAnyLampOn(), TEXT("no_power_no_light"));
			Building->SetCommonInspectionLightsEnabled(true);
			Building->SetMissingFloorAnnexPower(false);
			Next(6);
		}
		break;
	case 6:
		// 넷째 밤 관리인이 내리는 차단기도 같다.
		Walk(FVector(0.0f, PhaseSeconds < 0.4f ? 1.0f : -1.0f, 0.0f));
		if (PhaseSeconds > 0.8f)
		{
			Check(!Lights->IsAnyLampOn(), TEXT("night_four_breaker_kills_sensors"));
			Building->SetMissingFloorAnnexPower(true);
			Check(Narrative && !Narrative->HasBeatPlayed(AIGNightThreatDirector::SensorIntroBeat),
				TEXT("intro_not_yet_played"));
			Place(IGStairSensorProbe::ThirdFloorStart, 0.0f);
			if (bShots && Character->GetFlashlight())
			{
				// 어둑시니는 손전등 빛 속의 구멍처럼 보인다. 그 모습을 찍는다.
				Character->GetFlashlight()->SetOn(true);
			}
			Next(7);
		}
		break;
	case 7:
		// 3층 참. 동쪽으로 몇 걸음 걷고 남쪽 창을 보고 선다.
		if (PhaseSeconds < 0.6f)
		{
			break;
		}
		if (PhaseSeconds < 1.1f)
		{
			Walk(FVector(1.0f, 0.0f, 0.0f));
			break;
		}
		PC->SetControlRotation(FRotator(-4.0f, -90.0f, 0.0f));
		Check(Lights->IsLampOn(2), TEXT("third_floor_lamp_on"));
		Next(8);
		break;
	case 8:
		// 오래된 센서등은 3초 만에 툭 꺼진다. 그 틈에 계단 아래 어둠에 무언가 선다.
		if (Threat->IsEoduksiniManifested())
		{
			FigureAt = Threat->GetEoduksiniLocation();
			UE_LOG(LogIndieGame, Display, TEXT("STAIR_SENSOR_INTRO after=%.2f figure=%s feet=%s"),
				PhaseSeconds, *FigureAt.ToCompactString(), *Feet().ToCompactString());
			Check(PhaseSeconds < AIGStairSensorLights::HoldSeconds - 2.0f, TEXT("third_floor_lamp_cuts_out_early"));
			Check(!Lights->IsLampOn(2), TEXT("lamp_is_out_when_it_stands"));
			Check(Threat->IsEoduksiniPinned(), TEXT("intro_figure_is_pinned"));
			Check(FigureAt.Z < Feet().Z - 60.0f, TEXT("it_stands_below_the_landing"));
			Check(Narrative && Narrative->HasBeatPlayed(AIGNightThreatDirector::SensorIntroBeat),
				TEXT("intro_marked_played"));
			Next(9);
		}
		else if (PhaseSeconds > 9.0f)
		{
			UE_LOG(LogIndieGame, Display, TEXT("STAIR_SENSOR_INTRO missing lamp=%d feet=%s"),
				Lights->IsLampOn(2) ? 1 : 0, *Feet().ToCompactString());
			Check(false, TEXT("something_stands_below_in_the_dark"));
			Finish();
		}
		break;
	case 9:
	{
		// 돌아서서 쳐다본다. 커지지만 계단을 거슬러 올라오지 않는다.
		const FVector Chest = Threat->GetEoduksiniLocation() + FVector(0.0f, 0.0f, 110.0f);
		PC->SetControlRotation((Chest - Eye()).Rotation());
		if (bShots && PhaseSeconds > 1.2f && PhaseSeconds - DeltaSeconds <= 1.2f)
		{
			Shoot(TEXT("sensor-intro-figure"));
		}
		// 찍을 때는 다 자라 허리를 숙일 때까지 더 쳐다본다. 붙박여 있어 다가오지 않는다.
		if (bShots && PhaseSeconds > 7.0f && PhaseSeconds - DeltaSeconds <= 7.0f)
		{
			Shoot(TEXT("sensor-intro-figure-grown"));
		}
		if (PhaseSeconds > (bShots ? 7.5f : 2.5f))
		{
			Check(Threat->IsEoduksiniManifested(), TEXT("stays_while_watched"));
			Check(FVector::Dist(Threat->GetEoduksiniLocation(), FigureAt) < 15.0f, TEXT("pinned_figure_does_not_climb"));
			Next(10);
		}
		break;
	}
	case 10:
		// 한 걸음 움직이면 등이 켜지고 그것은 없다.
		if (PhaseSeconds < 0.4f)
		{
			Walk(FVector(-1.0f, 0.0f, 0.0f));
		}
		if (!Threat->IsEoduksiniManifested() || PhaseSeconds > 1.6f)
		{
			Check(Lights->IsLampOn(2), TEXT("lamp_comes_back_on"));
			Check(!Threat->IsEoduksiniManifested(), TEXT("light_dismisses_it"));
			Next(11);
		}
		break;
	case 11:
		if (PhaseSeconds > 0.6f)
		{
			Shoot(TEXT("sensor-intro-gone"));
			Next(12);
		}
		break;
	case 12:
		if (PhaseSeconds > 0.5f)
		{
			if (!bShots)
			{
				Finish();
				break;
			}
			// 여기부터는 찍기만 한다. 2.5층 참에서 3층 참을 올려다본다.
			if (Character->GetFlashlight())
			{
				Character->GetFlashlight()->SetOn(false);
			}
			Place(FVector(-407.5f, 10.0f, 450.0f), -90.0f);
			Next(13);
		}
		break;
	case 13:
		// 계단을 두어 단 올라 3층 참 등의 감지 범위에 든다.
		if (PhaseSeconds > 0.6f && PhaseSeconds < 1.25f)
		{
			Walk(FVector(0.0f, -1.0f, 0.0f));
		}
		if (PhaseSeconds > 1.3f)
		{
			PC->SetControlRotation(FRotator(14.0f, -90.0f, 0.0f));
			Check(Lights->IsLampOn(2), TEXT("shots_landing_lamp_on"));
			Next(14);
		}
		break;
	case 14:
		if (PhaseSeconds > 1.0f && PhaseSeconds - DeltaSeconds <= 1.0f)
		{
			Shoot(TEXT("sensor-landing-lit"));
		}
		if (!Lights->IsLampOn(2) && PhaseSeconds > 2.0f)
		{
			Next(15);
		}
		else if (PhaseSeconds > 14.0f)
		{
			Check(false, TEXT("shots_landing_lamp_goes_out"));
			Finish();
		}
		break;
	case 15:
		if (PhaseSeconds > 1.0f)
		{
			Shoot(TEXT("sensor-landing-dark"));
			Next(16);
		}
		break;
	case 16:
		// 3층 참 등 바로 밑에 서서 갓을 올려다본다. 돔만 빛나고 감지 렌즈는 꺼진 채다.
		if (PhaseSeconds > 0.6f && PhaseSeconds - DeltaSeconds <= 0.6f)
		{
			Place(FVector(-430.0f, -330.0f, 600.0f), 180.0f);
		}
		if (PhaseSeconds > 1.2f && PhaseSeconds < 1.6f)
		{
			Walk(FVector(-1.0f, 0.0f, 0.0f));
		}
		if (PhaseSeconds > 1.8f && PhaseSeconds - DeltaSeconds <= 1.8f)
		{
			PC->SetControlRotation((Lights->GetLampLocation(2) - Eye()).Rotation());
		}
		if (PhaseSeconds > 3.0f && PhaseSeconds - DeltaSeconds <= 3.0f)
		{
			Check(Lights->IsLampOn(2), TEXT("shots_lamp_on_overhead"));
			Shoot(TEXT("sensor-lamp-on"));
		}
		if (PhaseSeconds > 3.6f)
		{
			// 1층 로비. 엘리베이터 옆 공지를 손전등으로 비춰 본다.
			if (Character->GetFlashlight())
			{
				Character->GetFlashlight()->SetOn(true);
			}
			Place(FVector(612.0f, -172.0f, 0.0f), 0.0f);
			Next(17);
		}
		break;
	case 17:
		if (PhaseSeconds > 0.8f && PhaseSeconds - DeltaSeconds <= 0.8f)
		{
			PC->SetControlRotation(FRotator(-3.0f, 0.0f, 0.0f));
		}
		if (PhaseSeconds > 2.0f)
		{
			Shoot(TEXT("notice-lights-out"));
			Next(18);
		}
		break;
	case 18:
		if (PhaseSeconds > 0.6f)
		{
			Finish();
		}
		break;
	default:
		break;
	}
}
