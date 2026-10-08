#include "Sequence/IGNightFourHammerProbe.h"

#include "Components/CapsuleComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "Entity/IGShadowFigure.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGDoorLatch.h"
#include "Interaction/IGSwingDoor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGNightFourHammerProbe
{
	/** 5층 동쪽 벽 앞, 망치를 드는 자리와 벽의 판 가운데. 월드 좌표다. */
	const FVector WallFeet(175.0f, 700.0f, 1200.0f);
	const FVector WallFace(246.0f, 700.0f, 1290.0f);
	/** 403호 침대 옆, 방 안. 4층 바닥 기준이 아니라 월드 좌표다. */
	constexpr float HomeFloorZ = 900.0f;
	/** 넷째 타 뒤 벽 안의 대답까지(밤4 디렉터의 3.6 + 4.8초). 이보다 일찍 풀리면 부름을 건너뛴 것이다. */
	constexpr float ExpectedReleaseSeconds = 8.4f;
}

AIGNightFourHammerProbe::AIGNightFourHammerProbe()
{
	// 검사기는 매 프레임 망치·손님·어둑시니의 상태를 잰다. -IGNightFourHammerProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGNightFourHammerProbe::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGListenerEntity* InListener)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGNightFourHammerShots"));
	bOpenDoor = FParse::Param(FCommandLine::Get(), TEXT("IGNightFourHammerOpen"));
}

void AIGNightFourHammerProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGNightFourHammerProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGNightFourHammerProbe::Next(const int32 NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
	bActed = false;
}

void AIGNightFourHammerProbe::Stand(const FVector& Feet, const FVector& LookAt)
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

void AIGNightFourHammerProbe::Shoot(const TCHAR* Name)
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
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NightFourHammerReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER_SHOT %s"), *Path);
}

bool AIGNightFourHammerProbe::SaidLine(const FText& Line) const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	return Hud && Hud->HasDialogueLineForTesting(Line.ToString());
}

bool AIGNightFourHammerProbe::Strike()
{
	AIGMissingFloorNightFourDirector* Director = NightFour.Get();
	AIGMissingFloorEvidence* Wall = Director ? Director->GetWallBreakTarget() : nullptr;
	if (!Wall || !Wall->IsInteractionEnabled())
	{
		return false;
	}
	Wall->OnExamined.Broadcast(Wall);
	++Struck;
	UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER strike=%d t=%.1f"), Struck, TotalSeconds);
	return true;
}

bool AIGNightFourHammerProbe::IsCapturedSince(const double Seconds) const
{
	const AIGNightLoopDirector* Loop = NightLoop.Get();
	return Loop && (Loop->IsCaptureResetInFlight() || Loop->GetLastCaptureSeconds() > Seconds);
}

void AIGNightFourHammerProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 300.0f)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	using namespace IGNightFourHammerProbe;
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	AIGPrologueWorldScene* Building = Scene.Get();
	AIGMissingFloorNightFourDirector* Director = NightFour.Get();
	AIGNightThreatDirector* Threat = Threats.Get();
	AIGMissingFloorEvidence* Wall = Director ? Director->GetWallBreakTarget() : nullptr;
	const FText HammerCall = NSLOCTEXT("IGMissingFloor", "GuestN4HammerCall", "유담아. 오빠야. 문 좀 열어 줘.");

	if (Phase > 0 && (!Narrative || !Building || !Director || !Threat || !Wall))
	{
		Check(false, TEXT("night4_actors_still_exist"));
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
		for (TActorIterator<AIGMissingFloorNightFourDirector> It(GetWorld()); It; ++It)
		{
			NightFour = *It;
		}
		for (TActorIterator<AIGNightThreatDirector> It(GetWorld()); It; ++It)
		{
			Threats = *It;
		}
		for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It)
		{
			NightLoop = *It;
		}
		for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
		{
			AnnexDoor = It->GetAnnexGate();
			AnnexBolt = It->GetAnnexBolt();
		}
		Director = NightFour.Get();
		Threat = Threats.Get();
		Check(Narrative && Building && Director && Threat && NightLoop.IsValid() && AnnexDoor.IsValid()
			&& AnnexBolt.IsValid() && Player.IsValid(), TEXT("night4_actors_resolved"));
		if (!Narrative || !Building || !Director || !Threat || !NightLoop.IsValid() || !AnnexDoor.IsValid()
			|| !AnnexBolt.IsValid() || !Player.IsValid())
		{
			Finish();
			return;
		}
		if (Listener.IsValid())
		{
			Listener->SetDormant(true);
		}
		if (UIGFlashlightComponent* Torch = Player->GetFlashlight())
		{
			Torch->SetAvailable(true);
			Torch->SetOn(true);
		}
		// 넷째 밤. 벽을 칠 수 있게 진실 셋을 세운다.
		Narrative->SetNightIndex(4);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::SomeoneInTheWall, EIGMissingFloorSource::PipeAuditionCriterion);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::SomeoneInTheWall, EIGMissingFloorSource::PipeWaterComparison);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::CarbonLedgerOriginal);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::AgentMoveOutMessage);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WaitingForAnAnswer, EIGMissingFloorSource::AnswerRhythmMaterials);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WaitingForAnAnswer, EIGMissingFloorSource::AnswerReturned);
		Check(Narrative->IsFinalChoiceUnlocked(), TEXT("final_choice_unlocked"));
		// 철문은 닫고, 빗장은 장면대로.
		AnnexDoor->ForceOpenState(false);
		AnnexDoor->SetLatched(!bOpenDoor, false);
		// 물소리 가면. 옥상 세척 배수 → 물탱크 우회 → 1층 이송 펌프.
		for (AIGMissingFloorEvidence* Control : {Director->GetCleaningDrain(), Director->GetFloatBypass(), Director->GetTransferPump()})
		{
			if (Control)
			{
				Control->OnExamined.Broadcast(Control);
			}
		}
		Check(Narrative->IsNightFourMaskRunning(), TEXT("water_mask_running"));
		Stand(WallFeet, WallFace);
		CaptureBaseline = NightLoop->GetLastCaptureSeconds();
		Next(1);
		break;
	}

	case 1:
		// 첫째에서 셋째 타. 셋째 뒤 0.7초에 차단기가 내려간다.
		if (!bActed && PhaseSeconds >= 0.5f)
		{
			bActed = true;
			Check(Wall->IsInteractionEnabled(), TEXT("wall_can_be_struck"));
		}
		for (int32 Swing = 0; Swing < 3; ++Swing)
		{
			const float At = 0.6f + Swing * 1.2f;
			if (PhaseSeconds >= At && PhaseSeconds - DeltaSeconds < At)
			{
				Strike();
			}
		}
		if (PhaseSeconds >= 4.4f)
		{
			Check(Struck == 3 && !Building->IsMissingFloorAnnexPowered(), TEXT("third_strike_cuts_the_power"));
			Check(Building->AreCommonInspectionLightsOff(), TEXT("corridor_lobby_and_sensor_lights_die"));
			Next(2);
		}
		break;

	case 2:
		// 넷째 타. 손이 저려 잠깐 못 치고, 그 사이 철문 밖에서 부른다. 벽 안의 대답 뒤 다시 든다.
		if (!bActed && PhaseSeconds >= 0.3f)
		{
			bActed = true;
			Check(Strike(), TEXT("fourth_strike"));
		}
		if (bActed && PhaseSeconds >= 1.0f && PhaseSeconds < 7.0f)
		{
			bSawHeld |= !Wall->IsInteractionEnabled();
		}
		bSawCall |= Threat->IsHammerCallActive() && Threat->GetGuestDoor() == EIGGuestDoor::Annex;
		if (PhaseSeconds >= 6.8f && PhaseSeconds - DeltaSeconds < 6.8f)
		{
			Check(bSawHeld, TEXT("hands_numb_after_fourth_strike"));
			Check(bSawCall, TEXT("someone_calls_at_the_annex_door"));
			Check(SaidLine(HammerCall), TEXT("brother_voice_asks_to_open"));
		}
		if (PhaseSeconds >= 8.7f && PhaseSeconds - DeltaSeconds < 8.7f)
		{
			Shoot(TEXT("hammer-wall-answer"));
		}
		if (bActed && PhaseSeconds >= 1.0f && Wall->IsInteractionEnabled())
		{
			Check(PhaseSeconds >= ExpectedReleaseSeconds - 0.6f, TEXT("hammer_back_after_wall_answers"));
			Next(3);
		}
		else if (PhaseSeconds >= 12.0f)
		{
			Check(false, TEXT("hammer_back_after_wall_answers"));
			Finish();
		}
		break;

	case 3:
		if (!bOpenDoor)
		{
			// 빗장. 열쇠를 꽂다 걸려 한마디 남기고 간다.
			bSawCaughtOnBolt |= Threat->GetGuestStage() == EIGGuestStage::CaughtOnLatch;
			if (Threat->GetGuestStage() == EIGGuestStage::Spent)
			{
				Check(bSawCaughtOnBolt && AnnexDoor->IsFullyClosed() && !IsCapturedSince(CaptureBaseline),
					TEXT("bolt_holds_the_hammer_call"));
				Next(4);
			}
			else if (PhaseSeconds >= 35.0f)
			{
				Check(false, TEXT("bolt_holds_the_hammer_call"));
				Finish();
			}
			break;
		}
		// 빗장 없음. 열고 들어와 잡는다. 공격성 3 아래라 결말 C가 아니라 침대로 간다.
		if (IsCapturedSince(CaptureBaseline))
		{
			Check(NightLoop->IsCaptureResetInFlight() && Narrative->GetEndingChoice().IsNone(),
				TEXT("hammer_phase_capture_goes_to_bed"));
			Next(6);
		}
		else if (PhaseSeconds >= 45.0f)
		{
			Check(false, TEXT("hammer_phase_capture_goes_to_bed"));
			Finish();
		}
		break;

	case 4:
		// 정전 속에 어둑시니가 선다. 쳐다보면 커진다.
		if (!bActed && FMath::FloorToInt(PhaseSeconds / 2.0f) != FMath::FloorToInt((PhaseSeconds - DeltaSeconds) / 2.0f))
		{
			UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER dark=%.2f room=%.2f dark_seconds=%.1f"),
				Player->GetDarkness(), Player->GetRoomDarkness(), Threat->GetDarknessSeconds());
		}
		if (!bActed && Threat->IsEoduksiniManifested())
		{
			bActed = true;
			PhaseSeconds = 0.0f;
			Check(true, TEXT("eoduksini_after_blackout"));
			// 등 뒤에 선 것은 빚은 몸이어야 한다. 몸을 못 읽으면 도형 윤곽으로 조용히 돌아가서
			// 메시가 빠져도 화면 말고는 티가 나지 않는다.
			bool bSculpted = false;
			for (TActorIterator<AIGShadowFigure> It(GetWorld()); It; ++It)
			{
				if (It->IsManifested() && !It->IsPaper())
				{
					bSculpted = It->IsSculpted();
				}
			}
			Check(bSculpted, TEXT("eoduksini_wears_sculpted_body"));
			Stand(WallFeet, Threat->GetEoduksiniLocation() + FVector(0.0f, 0.0f, 110.0f));
		}
		if (bActed && PhaseSeconds >= 1.2f && PhaseSeconds - DeltaSeconds < 1.2f)
		{
			Shoot(TEXT("hammer-eoduksini"));
		}
		if (bActed && PhaseSeconds >= 1.6f)
		{
			Stand(WallFeet, WallFace);
			Next(5);
		}
		else if (!bActed && PhaseSeconds >= 30.0f)
		{
			Check(false, TEXT("eoduksini_after_blackout"));
			Next(5);
		}
		break;

	case 5:
		// 다섯째 타. 벽이 열리면 어둠도 손님도 그친다.
		if (!bActed && PhaseSeconds >= 0.3f)
		{
			bActed = true;
			Check(Strike(), TEXT("fifth_strike"));
		}
		if (PhaseSeconds >= 2.0f)
		{
			Check(Narrative->IsNightFourWallOpened(), TEXT("wall_opens_on_fifth"));
			Check(!Threat->IsEoduksiniManifested()
				&& (Threat->GetGuestStage() == EIGGuestStage::Spent || Threat->GetGuestStage() == EIGGuestStage::Idle),
				TEXT("threats_stop_when_wall_opens"));
			Finish();
		}
		break;

	case 6:
		// 침대에서 깼다. 정전은 그대로라 403호 전등도 죽어 있어야 한다.
		if (NightLoop->IsCaptureResetInFlight() && PhaseSeconds < 20.0f)
		{
			return;
		}
		if (!bActed)
		{
			bActed = true;
			PhaseSeconds = 0.0f;
			const float FeetZ = Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Check(FMath::Abs(FeetZ - HomeFloorZ) < 60.0f, TEXT("woke_in_403"));
			Check(!Building->IsMissingFloorAnnexPowered() && !Building->IsAnyUnitLightVisibleForTesting(),
				TEXT("home_lights_dead_in_blackout"));
		}
		if (PhaseSeconds >= 1.0f && PhaseSeconds - DeltaSeconds < 1.0f)
		{
			Shoot(TEXT("blackout-403"));
		}
		if (PhaseSeconds >= 1.5f)
		{
			Finish();
		}
		break;

	default:
		break;
	}
}
