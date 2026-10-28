#include "Sequence/IGHorrorRegressionProbe.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "Entity/IGSeoRecorder.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGBathroomRefuge.h"
#include "Interaction/IGDoorLatch.h"
#include "Interaction/IGInteractionTypes.h"
#include "Interaction/IGSwingDoor.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"

namespace IGHorrorRegressionProbe
{
	/** 403호 방 안, 현관에서 2 m쯤 들어온 자리. 4층 바닥 기준. */
	const FVector HomeFeet(0.0f, -60.0f, 0.0f);
	/** 욕실 안 깊은 자리. 4층 바닥 기준. */
	const FVector BathroomFeet(300.0f, -108.0f, 0.0f);
	/** 4층 복도. 그녀는 서쪽 끝, 위층 사람은 동쪽에 세우고 노크는 그 사이에서 난다. */
	const FVector CorridorPlayerFeet(-480.0f, -305.0f, 0.0f);
	const FVector CorridorListenerFeet(380.0f, -305.0f, 0.0f);
	const FVector CorridorKnock(200.0f, -305.0f, 100.0f);
	/** 5층 증축부 가운데. 월드 좌표다. */
	const FVector AnnexMiddleFeet(0.0f, 760.0f, 1200.0f);
	/** 위층 사람의 노크를 기다리는 데 쓰는 시각. 한 박자 묶음은 3초가 지나면 끊긴다. */
	constexpr float SingleTapGapSeconds = 3.4f;
	constexpr float GuestTimeoutSeconds = 60.0f;
	/** 앞 장면의 추격 여운(20초)이나 포획 유예(40초)가 끝나기를 기다리는 한도. */
	constexpr float QuietWaitSeconds = 45.0f;
	/** 어둠 시험 후보. 손전등을 끄고 서서 가장 어두운 곳을 쓴다. 월드 좌표다. */
	const FVector DarkCandidates[] = {
		FVector(200.0f, 820.0f, 1200.0f),
		FVector(-320.0f, 900.0f, 1200.0f),
		FVector(320.0f, 520.0f, 1200.0f),
		FVector(-320.0f, 520.0f, 1200.0f),
		FVector(0.0f, -60.0f, AIGPrologueWorldScene::FourthFloorZ),
		FVector(300.0f, -108.0f, AIGPrologueWorldScene::FourthFloorZ),
		FVector(-480.0f, -305.0f, AIGPrologueWorldScene::FourthFloorZ),
	};
	constexpr float ProbeTimeoutSeconds = 900.0f;

	FText EarlyRhythmThought()
	{
		return NSLOCTEXT("IGMissingFloor", "EarlyRhythmThought", "둘, 쉬고, 하나… 아직은 아무 대답이 없네.");
	}
	FText OpenDoorLine()
	{
		return NSLOCTEXT("IGMissingFloor", "GuestN2OpenDoor", "언니, 저 나린이에요. 문이 열려 있길래요. 지갑 두고 가셨어요.");
	}
	FText LatchTornThought()
	{
		return NSLOCTEXT("IGMissingFloor", "YudamGuestLatchTorn", "걸쇠가 뜯겨 나갔네. 또 오면 욕실 문이라도 잠그자.");
	}
	FText RecorderThought()
	{
		return NSLOCTEXT("IGMissingFloor", "SeoRecorderThought", "오빠를 두고 그냥 내려간 거야?");
	}
	FVector OnFourth(const FVector& Feet)
	{
		return Feet + FVector(0.0f, 0.0f, AIGPrologueWorldScene::FourthFloorZ);
	}
}

AIGHorrorRegressionProbe::AIGHorrorRegressionProbe()
{
	// 검사기는 매 프레임 손님 단계와 화면에 뜬 말을 본다. -IGHorrorRegressionProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGHorrorRegressionProbe::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGListenerEntity* InListener)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
}

void AIGHorrorRegressionProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("HORROR_REGRESSION_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGHorrorRegressionProbe::Finish()
{
	if (Step < 0)
	{
		return;
	}
	Step = -1;
	UE_LOG(LogIndieGame, Display, TEXT("HORROR_REGRESSION_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGHorrorRegressionProbe::Next(const int32 NewStep)
{
	UE_LOG(LogIndieGame, Display, TEXT("HORROR_REGRESSION_STEP %d -> %d at %.1f"), Step, NewStep, TotalSeconds);
	Step = NewStep;
	StepSeconds = 0.0f;
	bActed = false;
}

void AIGHorrorRegressionProbe::Stand(const FVector& Feet, const FVector& LookAt)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character)
	{
		return;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(Feet + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr,
		ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	if (PC)
	{
		const FVector Eye = Character->GetActorLocation() + Character->GetEyeOffsetFromActor();
		PC->SetControlRotation((LookAt - Eye).Rotation());
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->UpdateCamera(0.0f);
		}
	}
}

void AIGHorrorRegressionProbe::NoteLines()
{
	using namespace IGHorrorRegressionProbe;
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	if (!Hud)
	{
		return;
	}
	for (const FText& Text : {EarlyRhythmThought(), OpenDoorLine(), LatchTornThought(), RecorderThought()})
	{
		if (Hud->HasDialogueLineForTesting(Text.ToString()))
		{
			SeenLines.Add(Text.ToString());
		}
	}
}

bool AIGHorrorRegressionProbe::Saw(const FText& Line) const
{
	return SeenLines.Contains(Line.ToString());
}

void AIGHorrorRegressionProbe::RequestQuietAutosave()
{
	AIGNightThreatDirector::RequestAutosaveWhenQuiet(
		this,
		FGameplayTag::RequestGameplayTag(TEXT("Chapter.MissingFloor"), false),
		GetWorld()->GetOutermost()->GetFName(),
		FGameplayTag::RequestGameplayTag(TEXT("Checkpoint.MissingFloor.Night"), false));
}

void AIGHorrorRegressionProbe::SetTorch(const bool bOn)
{
	if (AIGPlayerCharacter* Character = Player.Get())
	{
		if (UIGFlashlightComponent* Torch = Character->GetFlashlight())
		{
			Torch->SetAvailable(true);
			Torch->SetOn(bOn);
		}
	}
}

void AIGHorrorRegressionProbe::LockBathroom()
{
	if (AIGBathroomRefuge* Room = Refuge.Get())
	{
		FIGInteractionContext Context;
		Context.Interactor = Player.Get();
		Room->CompleteInteraction_Implementation(Context);
	}
}

void AIGHorrorRegressionProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	using namespace IGHorrorRegressionProbe;
	if (Step < 0)
	{
		return;
	}
	StepSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	NoteLines();
	if (TotalSeconds > ProbeTimeoutSeconds)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}

	AIGPlayerCharacter* Character = Player.Get();
	AIGListenerEntity* Upstairs = Listener.Get();
	AIGNightThreatDirector* Threats = Threat.Get();
	AIGMissingFloorNightThreeDirector* Three = NightThree.Get();
	AIGNightLoopDirector* Loops = Loop.Get();
	AIGSwingDoor* Front = HomeDoor.Get();
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	if (Step > 0 && (!Character || !Upstairs || !Threats || !Three || !Loops || !Front || !Narrative))
	{
		Check(false, TEXT("stage_lost"));
		Finish();
		return;
	}
	const EIGGuestStage GuestStage = Threats ? Threats->GetGuestStage() : EIGGuestStage::Idle;
	const bool bGuestGone = GuestStage == EIGGuestStage::Idle || GuestStage == EIGGuestStage::Spent;
	if (static_cast<int32>(GuestStage) != LastGuestStage)
	{
		LastGuestStage = static_cast<int32>(GuestStage);
		UE_LOG(LogIndieGame, Display, TEXT("HORROR_REGRESSION guest_stage=%d at %.1f"), LastGuestStage, TotalSeconds);
	}

	switch (Step)
	{
	case 0:
	{
		if (StepSeconds < 1.0f)
		{
			return;
		}
		for (TActorIterator<AIGNightThreatDirector> It(GetWorld()); It; ++It) { Threat = *It; break; }
		for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It) { NightThree = *It; break; }
		for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It) { Loop = *It; break; }
		for (TActorIterator<AIGNightPhaseDirector> It(GetWorld()); It; ++It) { Phase = *It; break; }
		for (TActorIterator<AIGSeoRecorder> It(GetWorld()); It; ++It) { Recorder = *It; break; }
		for (TActorIterator<AIGBathroomRefuge> It(GetWorld()); It; ++It) { Refuge = *It; break; }
		if (const AIGPrologueWorldScene* Building = Scene.Get())
		{
			HomeDoor = Building->GetHomeDoor();
		}
		for (TActorIterator<AIGDoorLatch> It(GetWorld()); It; ++It)
		{
			if (It->GetDoor() == HomeDoor.Get())
			{
				HomeLatch = *It;
			}
		}
		const bool bReady = Threat.IsValid() && NightThree.IsValid() && Loop.IsValid() && Phase.IsValid()
			&& Recorder.IsValid() && Refuge.IsValid() && HomeDoor.IsValid() && HomeLatch.IsValid()
			&& Character && Upstairs && Narrative;
		Check(bReady, TEXT("stage_ready"));
		if (!bReady)
		{
			Finish();
			return;
		}
		Narrative->SetNightIndex(3);
		Upstairs->SetDormant(true);
		SetTorch(true);
		Next(10);
		break;
	}

	// 1. 벽의 대답을 기다리는 동안 ---------------------------------------------------------
	case 10:
		if (Threats->IsQuietForProbe())
		{
			Check(true, TEXT("quiet_before_gates"));
			Three->SetAnswerPendingForProbe(true);
			Next(11);
		}
		else if (StepSeconds > 30.0f)
		{
			Check(false, TEXT("quiet_before_gates"));
			Next(20);
		}
		break;
	case 11:
		if (StepSeconds >= 0.3f)
		{
			Check(!Threats->IsQuietForProbe(), TEXT("wall_answer_blocks_other_threats"));
			Check(Threats->IsEventInProgressForProbe(), TEXT("wall_answer_counts_as_event"));
			RequestQuietAutosave();
			Check(Threats->IsAutosavePendingForProbe(), TEXT("autosave_waits_for_wall_answer"));
			Three->SetAnswerPendingForProbe(false);
			Next(12);
		}
		break;
	case 12:
		if (StepSeconds >= 0.5f)
		{
			Check(!Threats->IsAutosavePendingForProbe(), TEXT("autosave_written_after_wall_answer"));
			Check(Threats->IsQuietForProbe(), TEXT("quiet_after_wall_answer"));
			Next(20);
		}
		break;

	// 2. 단서보다 먼저 친 박자 ----------------------------------------------------------------
	case 20:
		if (!bActed)
		{
			bActed = true;
			Stand(AnnexMiddleFeet, AnnexMiddleFeet + FVector(300.0f, 0.0f, 90.0f));
		}
		if (StepSeconds >= 0.5f)
		{
			Check(!Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer), TEXT("no_answer_before_early_rhythm"));
			Taps = 0;
			Next(21);
		}
		break;
	case 21:
	{
		// 둘(0.40초) 쉬고(0.80초) 하나. 대답의 박자 그대로다.
		static constexpr float TapTimes[3] = {0.0f, 0.40f, 1.20f};
		while (Taps < 3 && StepSeconds >= TapTimes[Taps])
		{
			Three->NoteEarlyRhythmTap(Character->GetActorLocation());
			++Taps;
		}
		if (Taps == 3)
		{
			Next(22);
		}
		break;
	}
	case 22:
		if (StepSeconds >= 2.6f)
		{
			Check(Narrative->HasBeatPlayed(FName(TEXT("Night3.EarlyRhythm.N3"))), TEXT("early_rhythm_noticed"));
			Check(Saw(EarlyRhythmThought()), TEXT("early_rhythm_thought_shown"));
			Check(!Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer), TEXT("early_rhythm_does_not_answer"));
			Check(!Three->IsAwaitingWallAnswer(), TEXT("early_rhythm_no_wall_wait"));
			Next(30);
		}
		break;

	// 3. 한 번씩 치는 노크로 위층 사람 세우기 --------------------------------------------------
	case 30:
		Stand(OnFourth(CorridorPlayerFeet), OnFourth(CorridorKnock));
		Upstairs->SetDormant(false);
		Upstairs->ParkForBeat(OnFourth(CorridorListenerFeet), 180.0f);
		Next(31);
		break;
	case 31:
		if (StepSeconds >= 0.5f)
		{
			Check(Upstairs->TryAnswerKnock(OnFourth(CorridorKnock)), TEXT("listener_hears_single_tap"));
			Check(Upstairs->GetListenerState() == EIGListenerState::Listening, TEXT("first_tap_stops_listener"));
			Next(32);
		}
		break;
	case 32:
		if (StepSeconds >= SingleTapGapSeconds)
		{
			Upstairs->TryAnswerKnock(OnFourth(CorridorKnock));
			Check(Upstairs->GetListenerState() == EIGListenerState::Listening, TEXT("second_tap_still_stops_listener"));
			Next(33);
		}
		break;
	case 33:
		if (StepSeconds >= SingleTapGapSeconds)
		{
			Upstairs->TryAnswerKnock(OnFourth(CorridorKnock));
			const EIGListenerState State = Upstairs->GetListenerState();
			UE_LOG(LogIndieGame, Display, TEXT("HORROR_REGRESSION listener_state=%d"), static_cast<int32>(State));
			Check(State == EIGListenerState::Investigating, TEXT("third_tap_sends_listener_over"));
			Next(34);
		}
		break;
	case 34:
		Upstairs->SetDormant(true);
		Next(35);
		break;
	case 35:
		// 위층 사람이 조사하러 간 뒤 20초는 다른 괴이가 오지 않는다. 손님 시험은 그 뒤에 한다.
		if (Threats->IsQuietForProbe() || StepSeconds > QuietWaitSeconds)
		{
			Check(Threats->IsQuietForProbe(), TEXT("quiet_after_pursuit_afterglow"));
			Next(40);
		}
		break;

	// 4. 첫 노크에 문을 열어 미룬 방문과 다시 열린 시간 -----------------------------------------
	case 40:
		if (!bActed)
		{
			bActed = true;
			Stand(OnFourth(HomeFeet), OnFourth(FVector(120.0f, -230.0f, 100.0f)));
			Front->ForceOpenState(false);
			Front->SetLatched(false, false);
			CapturesAtStart = Loops->GetCaptureCount();
		}
		if (StepSeconds >= 0.5f && (Threats->IsQuietForProbe() || StepSeconds > QuietWaitSeconds))
		{
			Check(Threats->IsQuietForProbe(), TEXT("quiet_before_deferred_visit"));
			Threats->StartHomeGuestForProbe();
			Next(41);
		}
		break;
	case 41:
		if (StepSeconds >= 0.3f)
		{
			Front->ForceOpenState(true);
			Next(42);
		}
		break;
	case 42:
		if (StepSeconds >= 0.5f)
		{
			Check(Threats->IsGuestStartDeferred(), TEXT("open_at_first_knock_defers_visit"));
			Check(bGuestGone, TEXT("deferred_visit_withdraws"));
			Check(Loops->GetCaptureCount() == CapturesAtStart, TEXT("deferred_visit_no_capture"));
			// 미뤄 둔 저장도 시간이 다시 열리면 버린다.
			Three->SetAnswerPendingForProbe(true);
			RequestQuietAutosave();
			Check(Threats->IsAutosavePendingForProbe(), TEXT("autosave_pending_before_restart"));
			if (AIGNightPhaseDirector* Hour = Phase.Get())
			{
				Hour->RestartTheHour(3);
			}
			Three->SetAnswerPendingForProbe(false);
			Next(43);
		}
		break;
	case 43:
		if (StepSeconds >= 1.0f)
		{
			Check(!Threats->IsGuestStartDeferred(), TEXT("hour_restart_clears_deferred_visit"));
			Check(!Threats->IsAutosavePendingForProbe(), TEXT("hour_restart_drops_pending_autosave"));
			Check(!Threats->IsGuestReturnOwed(), TEXT("hour_restart_clears_owed_return"));
			Upstairs->SetDormant(true);
			Front->ForceOpenState(false);
			Next(50);
		}
		break;

	// 5. 열어 둔 현관 -----------------------------------------------------------------------
	case 50:
		if (!bActed)
		{
			bActed = true;
			Narrative->SetNightIndex(2);
			Stand(OnFourth(HomeFeet), OnFourth(FVector(120.0f, -230.0f, 100.0f)));
			Front->SetLatched(false, false);
			Front->ForceOpenState(true);
			CapturesAtStart = Loops->GetCaptureCount();
		}
		if (StepSeconds >= 0.5f && (Threats->IsQuietForProbe() || StepSeconds > QuietWaitSeconds))
		{
			Check(Threats->IsQuietForProbe(), TEXT("quiet_before_open_door_visit"));
			Threats->StartGuestForProbe();
			Next(51);
		}
		break;
	case 51:
		if (StepSeconds >= 0.3f)
		{
			Check(GuestStage == EIGGuestStage::AtOpenDoor, TEXT("open_front_door_guest_stands_in_doorway"));
			Next(52);
		}
		break;
	case 52:
		if (StepSeconds >= 2.5f)
		{
			Check(Saw(OpenDoorLine()), TEXT("open_door_line_fits_open_door"));
			Front->ForceOpenState(false);
			Next(53);
		}
		break;
	case 53:
		if (StepSeconds >= 0.3f)
		{
			Check(GuestStage == EIGGuestStage::Knocking || GuestStage == EIGGuestStage::Keypad,
				TEXT("closing_door_sends_guest_outside"));
			Front->SetLatched(true, false);
			Next(54);
		}
		break;
	case 54:
		if (bGuestGone || StepSeconds > GuestTimeoutSeconds)
		{
			Check(GuestStage == EIGGuestStage::Spent, TEXT("latched_guest_leaves"));
			Check(Loops->GetCaptureCount() == CapturesAtStart, TEXT("latched_door_no_capture"));
			Front->SetLatched(false, false);
			Next(60);
		}
		break;

	// 6. 셋째 밤 옆집을 두드리는 사이 열었다 닫은 문 --------------------------------------------
	case 60:
		if (!bActed)
		{
			bActed = true;
			Narrative->SetNightIndex(3);
			Stand(OnFourth(HomeFeet), OnFourth(FVector(120.0f, -230.0f, 100.0f)));
			Front->ForceOpenState(false);
			Front->SetLatched(false, false);
			CapturesAtStart = Loops->GetCaptureCount();
		}
		if (StepSeconds >= 0.5f && (Threats->IsQuietForProbe() || StepSeconds > QuietWaitSeconds))
		{
			Check(Threats->IsQuietForProbe(), TEXT("quiet_before_lure_visit"));
			Threats->StartGuestForProbe();
			Check(Threats->GetGuestStage() == EIGGuestStage::Canvassing, TEXT("night_three_guest_canvasses"));
			Next(61);
		}
		break;
	case 61:
		// 402호 문 앞에 섰을 때(7.6초) 연다.
		if (StepSeconds >= 8.0f)
		{
			Front->ForceOpenState(true);
			Next(62);
		}
		break;
	case 62:
		// 알아채고 바로 닫기 시작한다. 실제 손으로 닫는 것처럼 1.3초에 걸쳐 돈다.
		if (StepSeconds >= 0.5f)
		{
			Front->BeginScriptedSwingWithLoudness(false, 0.0f, 1.3f);
			Next(63);
		}
		break;
	case 63:
		if (StepSeconds >= 2.0f)
		{
			Check(GuestStage == EIGGuestStage::Knocking || GuestStage == EIGGuestStage::Keypad,
				TEXT("closing_in_time_blocks_lure"));
			Check(Loops->GetCaptureCount() == CapturesAtStart, TEXT("lure_closed_no_capture"));
			Front->SetLatched(true, false);
			Next(64);
		}
		break;
	case 64:
		if (bGuestGone || StepSeconds > GuestTimeoutSeconds)
		{
			Check(GuestStage == EIGGuestStage::Spent, TEXT("lured_guest_leaves_after_latch"));
			Front->SetLatched(false, false);
			Next(70);
		}
		break;

	// 7. 걸쇠가 뜯긴 밤의 재방문과 욕실 --------------------------------------------------------
	case 70:
		if (!bActed)
		{
			bActed = true;
			Narrative->SetNightIndex(2);
			Stand(OnFourth(HomeFeet), OnFourth(FVector(120.0f, -230.0f, 100.0f)));
			Front->ForceOpenState(false);
			Front->SetLatched(false, false);
			CapturesAtStart = Loops->GetCaptureCount();
		}
		if (StepSeconds >= 0.5f && (Threats->IsQuietForProbe() || StepSeconds > QuietWaitSeconds))
		{
			Check(Threats->IsQuietForProbe(), TEXT("quiet_before_capture_visit"));
			Threats->StartHomeGuestForProbe();
			Next(71);
		}
		break;
	case 71:
		if (Loops->GetCaptureCount() > CapturesAtStart && !Loops->IsCaptureResetInFlight())
		{
			Next(72);
		}
		else if (StepSeconds > GuestTimeoutSeconds)
		{
			Check(false, TEXT("guest_captures_visible_player"));
			Next(80);
		}
		break;
	case 72:
		if (StepSeconds >= 5.0f)
		{
			Check(HomeLatch.IsValid() && HomeLatch->IsBroken(), TEXT("guest_capture_tears_latch"));
			Check(Threats->IsGuestReturnOwed(), TEXT("guest_owes_return_visit"));
			Check(Saw(LatchTornThought()), TEXT("latch_torn_points_to_bathroom"));
			Check(GuestStage == EIGGuestStage::Idle, TEXT("spent_visit_resets_for_return"));
			Next(73);
		}
		break;
	case 73:
		if (!bActed)
		{
			bActed = true;
			Stand(OnFourth(BathroomFeet), OnFourth(FVector(197.0f, -135.0f, 100.0f)));
			if (AIGBathroomRefuge* Room = Refuge.Get(); Room && Room->GetDoor())
			{
				Room->GetDoor()->ForceOpenState(false);
			}
		}
		// 잡힌 뒤 40초는 손님도 다시 오지 않는다. 실제 재방문은 몇 분 뒤라 그 안에 든다.
		if (StepSeconds >= 0.4f && (Threats->IsQuietForProbe() || StepSeconds > QuietWaitSeconds))
		{
			Check(Threats->IsQuietForProbe(), TEXT("quiet_before_return_visit"));
			LockBathroom();
			const AIGBathroomRefuge* Room = Refuge.Get();
			Check(Room && Room->IsSealed(), TEXT("bathroom_locked"));
			Check(Character->IsConcealedInHidingSpot(), TEXT("hidden_in_locked_bathroom"));
			CapturesAtStart = Loops->GetCaptureCount();
			bSawInside = false;
			// 잡힌 뒤라 현관은 열려 있을 수도 있다. 문 상태를 그대로 보고 보낸다.
			Threats->StartGuestForProbe();
			Next(74);
		}
		break;
	case 74:
		bSawInside |= GuestStage == EIGGuestStage::Inside;
		if ((StepSeconds > 2.0f && bGuestGone) || StepSeconds > GuestTimeoutSeconds)
		{
			Check(bSawInside, TEXT("return_visit_comes_inside"));
			Check(Loops->GetCaptureCount() == CapturesAtStart, TEXT("locked_bathroom_survives_return_visit"));
			Check(GuestStage == EIGGuestStage::Spent, TEXT("return_visit_leaves"));
			Check(!Threats->IsGuestReturnOwed(), TEXT("no_further_visit_owed"));
			LockBathroom();
			Next(80);
		}
		break;

	// 8. 잡힌 뒤의 어둠, 첫째 밤의 어둠 ---------------------------------------------------------
	case 80:
	{
		// 후보마다 0.7초씩 서 본다. 어둠 값은 0.25초마다 다시 잰다.
		constexpr int32 CandidateCount = UE_ARRAY_COUNT(DarkCandidates);
		if (!bActed)
		{
			bActed = true;
			Narrative->SetNightIndex(3);
			Upstairs->SetDormant(true);
			SetTorch(false);
			Stand(DarkCandidates[SpotIndex], DarkCandidates[SpotIndex] + FVector(-300.0f, 0.0f, 90.0f));
		}
		if (StepSeconds < 0.7f)
		{
			break;
		}
		const float Darkness = Character->GetDarkness();
		UE_LOG(LogIndieGame, Display, TEXT("HORROR_REGRESSION spot=%d dark=%.2f"), SpotIndex, Darkness);
		if (Darkness > BestDarkness)
		{
			BestDarkness = Darkness;
			DarkSpot = DarkCandidates[SpotIndex];
		}
		if (++SpotIndex < CandidateCount)
		{
			Next(80);
			break;
		}
		Check(BestDarkness >= 0.7f, TEXT("dark_spot_found"));
		Next(86);
		break;
	}
	case 86:
		if (!bActed)
		{
			bActed = true;
			Stand(DarkSpot, DarkSpot + FVector(-300.0f, 0.0f, 90.0f));
			SetTorch(false);
			CapturesAtStart = Loops->GetCaptureCount();
		}
		if (StepSeconds >= 0.5f)
		{
			// 마주 보는 쪽 150 cm 앞에 세운다. 가장 어두운 자리가 복도 끝이면 그 앞이 벽이라, 벽까지 잰 거리에서 몸
			// 반지름만큼 뺀 자리에 세운다. 예전에는 서쪽 벽 속에 세워 몸이 벽을 뚫고 다가왔다.
			FVector Spot = DarkSpot + FVector(-150.0f, 0.0f, 0.0f);
			FHitResult Wall;
			const FVector Chest(0.0f, 0.0f, 100.0f);
			if (GetWorld()->LineTraceSingleByChannel(Wall, DarkSpot + Chest, Spot + Chest, ECC_Visibility,
					FCollisionQueryParams(SCENE_QUERY_STAT(IGHorrorEoduksiniSpot), false, Character)))
			{
				Spot = DarkSpot + FVector(-FMath::Max(0.0f, Wall.Distance - 45.0f), 0.0f, 0.0f);
			}
			Threats->BeginEoduksiniCaptureForProbe(Spot);
			Next(81);
		}
		break;
	case 81:
		if (Loops->GetCaptureCount() > CapturesAtStart && !Loops->IsCaptureResetInFlight())
		{
			Next(82);
		}
		else if (StepSeconds > 20.0f)
		{
			Check(false, TEXT("eoduksini_capture_settles"));
			Next(90);
		}
		break;
	case 82:
		if (!bActed)
		{
			bActed = true;
			Stand(DarkSpot, DarkSpot + FVector(-300.0f, 0.0f, 90.0f));
			SetTorch(false);
		}
		if (StepSeconds >= 12.0f)
		{
			UE_LOG(LogIndieGame, Display, TEXT("HORROR_REGRESSION grace dark=%.2f dark_seconds=%.2f"),
				Character->GetDarkness(), Threats->GetDarknessSeconds());
			Check(!Threats->IsQuietForProbe(), TEXT("capture_grace_keeps_others_away"));
			Check(Threats->GetDarknessSeconds() < 0.01f, TEXT("no_darkness_buildup_in_grace"));
			Next(83);
		}
		break;
	case 83:
		if (GetWorld()->GetTimeSeconds() - Loops->GetLastCaptureSeconds() < 41.0)
		{
			break;
		}
		if (!bActed)
		{
			bActed = true;
			StepSeconds = 0.0f;
		}
		if (StepSeconds >= 4.0f)
		{
			UE_LOG(LogIndieGame, Display, TEXT("HORROR_REGRESSION after_grace dark=%.2f dark_seconds=%.2f"),
				Character->GetDarkness(), Threats->GetDarknessSeconds());
			Check(Character->GetDarkness() >= 0.7f, TEXT("dark_spot_still_dark"));
			Check(Threats->GetDarknessSeconds() > 1.0f, TEXT("darkness_builds_after_grace"));
			Narrative->SetNightIndex(1);
			Next(84);
		}
		break;
	case 84:
		if (StepSeconds >= 4.0f)
		{
			Check(!Narrative->HasBeatPlayed(AIGNightThreatDirector::SensorIntroBeat), TEXT("sensor_intro_not_played_yet"));
			Check(Threats->GetDarknessSeconds() < 0.01f, TEXT("night_one_waits_for_sensor_intro"));
			Narrative->MarkBeatPlayed(AIGNightThreatDirector::SensorIntroBeat);
			Next(85);
		}
		break;
	case 85:
		if (StepSeconds >= 4.0f)
		{
			Check(Threats->GetDarknessSeconds() > 1.0f, TEXT("night_one_darkness_after_intro"));
			Narrative->SetNightIndex(3);
			SetTorch(true);
			Next(90);
		}
		break;

	// 9. 녹음기 ----------------------------------------------------------------------------
	case 90:
		if (!bActed)
		{
			bActed = true;
			Stand(AnnexMiddleFeet, AnnexMiddleFeet + FVector(300.0f, 0.0f, 90.0f));
		}
		if (StepSeconds >= 0.5f)
		{
			if (AIGSeoRecorder* Tape = Recorder.Get())
			{
				Check(!Tape->IsPlaying(), TEXT("recorder_idle_before_play"));
				Tape->StartPlaybackForTesting();
			}
			Next(91);
		}
		break;
	case 91:
		if (StepSeconds >= 4.0f)
		{
			Check(Recorder.IsValid() && Recorder->IsPlaying(), TEXT("recorder_plays"));
			Check(!Threats->IsQuietForProbe(), TEXT("recorder_blocks_other_threats"));
			RequestQuietAutosave();
			Check(Threats->IsAutosavePendingForProbe(), TEXT("autosave_waits_for_recorder"));
			CapturesAtStart = Loops->GetCaptureCount();
			Threats->BeginEoduksiniCaptureForProbe(AnnexMiddleFeet + FVector(150.0f, 0.0f, 0.0f));
			Next(92);
		}
		break;
	case 92:
		if (Recorder.IsValid() && !Recorder->IsPlaying())
		{
			Check(!Narrative->HasWitness(EIGMissingFloorWitness::SeoVoiceRecorder), TEXT("capture_stops_recorder_without_witness"));
			Next(93);
		}
		else if (StepSeconds > 20.0f)
		{
			Check(false, TEXT("capture_stops_recorder"));
			Next(100);
		}
		break;
	case 93:
		// 위협 감독은 0.1초 타이머로 미뤄 둔 저장을 내보낸다. 포획 복구가 끝난 그 프레임에 보면 한 틱
		// 이르다(렌더링을 켠 실시간 실행에서 걸렸다). 복구가 끝나고 1초 뒤에 본다.
		if (Loops->IsCaptureResetInFlight())
		{
			bActed = false;
			if (StepSeconds > 30.0f)
			{
				Check(false, TEXT("capture_recovers_after_recorder"));
				Next(100);
			}
			break;
		}
		if (!bActed)
		{
			bActed = true;
			SettledSeconds = StepSeconds;
		}
		if (StepSeconds >= 3.0f && StepSeconds - SettledSeconds >= 1.0f)
		{
			Check(Loops->GetCaptureCount() > CapturesAtStart, TEXT("recorder_capture_counted"));
			Check(!Threats->IsAutosavePendingForProbe(), TEXT("autosave_written_after_recorder_and_capture"));
			if (AIGSeoRecorder* Tape = Recorder.Get())
			{
				Tape->StartPlaybackForTesting();
			}
			Next(94);
		}
		break;
	case 94:
		if (StepSeconds > 2.0f && Recorder.IsValid() && !Recorder->IsPlaying())
		{
			if (!bActed)
			{
				bActed = true;
				StepSeconds = 2.0f;
			}
		}
		if (bActed && StepSeconds >= 3.0f)
		{
			Check(Narrative->HasWitness(EIGMissingFloorWitness::SeoVoiceRecorder), TEXT("recorder_heard_to_end"));
			Check(Saw(RecorderThought()), TEXT("recorder_thought_after_end"));
			Next(100);
		}
		else if (StepSeconds > 180.0f)
		{
			Check(false, TEXT("recorder_finishes"));
			Next(100);
		}
		break;

	case 100:
		Finish();
		break;

	default:
		break;
	}
}
