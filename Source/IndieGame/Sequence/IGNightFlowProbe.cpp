#include "Sequence/IGNightFlowProbe.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "IndieGame.h"
#include "Interaction/IGInteractable.h"
#include "Interaction/IGReadableNote.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace IGNightFlowProbe
{
	const FName NightOneGoal(TEXT("Night1.Goal"));
	const FName NightTwoGoal(TEXT("Night2.Goal"));
	const FName AboveConfirmed(TEXT("Night1.AboveConfirmed"));
	/** 403호 침대 옆, 방 안 바닥. */
	const FVector BedSideFeet(-60.0f, 60.0f, 900.0f);
	const FVector BedLookAt(-140.0f, 98.0f, 940.0f);
	/** 4층 복도, 안정기 소리가 가장 큰 천장 밑. */
	const FVector CeilingFeet(300.0f, -305.0f, 900.0f);
	const FVector CeilingLookAt(300.0f, -305.0f, 1125.0f);
}

AIGNightFlowProbe::AIGNightFlowProbe()
{
	PrimaryActorTick.bCanEverTick = true;
}

UIGMissingFloorNarrativeSubsystem* AIGNightFlowProbe::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
}

void AIGNightFlowProbe::Shoot(const TCHAR* Name)
{
	if (!bShots)
	{
		return;
	}
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NightFlowReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, FString::Printf(TEXT("%02d_%s.png"), ShotIndex++, Name)), false, false);
}

void AIGNightFlowProbe::Configure(AIGPlayerCharacter* InPlayer)
{
	Player = InPlayer;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGNightFlowShots"));
	UWorld* World = GetWorld();
	for (TActorIterator<AIGNightPhaseDirector> It(World); It; ++It)
	{
		NightPhase = *It;
	}
	for (TActorIterator<AIGListenerEntity> It(World); It; ++It)
	{
		Listener = *It;
	}
	// 손님과 어둑시니는 이 검사의 대상이 아니다. 위층 사람은 둔다(천장 대답이 그를 부른다).
	for (TActorIterator<AIGNightThreatDirector> It(World); It; ++It)
	{
		It->SetActorTickEnabled(false);
		World->GetTimerManager().ClearAllTimersForObject(*It);
	}
	Check(NightPhase.IsValid() && Player.IsValid(), TEXT("stage_ready"));
	if (!NightPhase.IsValid() || !Player.IsValid())
	{
		Finish();
	}
}

void AIGNightFlowProbe::Check(const bool bCondition, const TCHAR* Name)
{
	if (!bCondition)
	{
		++Failures;
	}
	UE_LOG(LogIndieGame, Display, TEXT("NIGHTFLOW_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGNightFlowProbe::Next(const int32 NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
}

void AIGNightFlowProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("NIGHTFLOW_PROBE %s failures=%d seconds=%.1f"),
		Failures ? TEXT("FAIL") : TEXT("PASS"), Failures, TotalSeconds);
	FString ResultPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("IGMissingFloorResultPath="), ResultPath))
	{
		ResultPath.TrimQuotesInline();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ResultPath), true);
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("MISSINGFLOOR_NIGHTFLOW %s failures=%d seconds=%.1f\n"),
			Failures ? TEXT("FAIL") : TEXT("PASS"), Failures, TotalSeconds), *ResultPath);
	}
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

bool AIGNightFlowProbe::Use(AActor* Actor)
{
	AIGPlayerCharacter* Character = Player.Get();
	if (!Actor || !Character || !Actor->GetClass()->ImplementsInterface(UIGInteractable::StaticClass())
		|| !IIGInteractable::Execute_CanInteract(Actor, Character))
	{
		return false;
	}
	FIGInteractionContext Context;
	Context.Interactor = Character;
	Context.TargetActor = Actor;
	Context.HeldDuration = IIGInteractable::Execute_GetInteractionHoldDuration(Actor, Character);
	Context.HoldProgress = 1.0f;
	IIGInteractable::Execute_BeginInteraction(Actor, Context);
	IIGInteractable::Execute_CompleteInteraction(Actor, Context);
	IIGInteractable::Execute_EndInteraction(Actor, Context, EIGInteractionEndReason::Completed);
	return true;
}

void AIGNightFlowProbe::Stand(const FVector& Feet, const FVector& LookAt)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !Controller)
	{
		return;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(Feet + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	const FVector Eye = Character->GetActorLocation() + Character->GetEyeOffsetFromActor();
	Controller->SetControlRotation((LookAt - Eye).Rotation());
}

AIGMissingFloorEvidence* AIGNightFlowProbe::FindEvidence(const TCHAR* Name) const
{
	for (TActorIterator<AIGMissingFloorEvidence> It(GetWorld()); It; ++It)
	{
		if (It->GetName() == Name)
		{
			return *It;
		}
	}
	return nullptr;
}

AIGReadableNote* AIGNightFlowProbe::FindNote() const
{
	AIGPlayerCharacter* Character = Player.Get();
	for (TActorIterator<AIGReadableNote> It(GetWorld()); It; ++It)
	{
		if (Character && It->GetClass()->ImplementsInterface(UIGInteractable::StaticClass())
			&& IIGInteractable::Execute_CanInteract(*It, Character))
		{
			return *It;
		}
	}
	return nullptr;
}

bool AIGNightFlowProbe::IsDay() const
{
	return NightPhase.IsValid() && !NightPhase->IsHourActive() && !NightPhase->IsDawnTransitionInProgress();
}

void AIGNightFlowProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 600.0f)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	AIGNightPhaseDirector* Hour = NightPhase.Get();
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Hour || !Narrative)
	{
		Finish();
		return;
	}
	AIGMissingFloorEvidence* Bed = FindEvidence(TEXT("MissingFloorSleepTarget"));
	switch (Phase)
	{
	case 0:
		// 그레이박스 무대가 밤1을 연다.
		if (Hour->IsHourActive() && Narrative->GetNightIndex() == 1 && PhaseSeconds > 1.0f)
		{
			Check(Bed && !Bed->IsInteractionEnabled(), TEXT("night_bed_closed_before_goal"));
			Hour->CompleteNightGoal();
			Next(1);
		}
		else if (PhaseSeconds > 20.0f)
		{
			Check(false, TEXT("night_one_started"));
			Finish();
		}
		return;
	case 1:
	{
		Check(Hour->IsHourActive(), TEXT("goal_does_not_bring_dawn"));
		Check(Narrative->HasBeatPlayed(IGNightFlowProbe::NightOneGoal), TEXT("goal_recorded"));
		Check(Bed && Bed->IsInteractionEnabled(), TEXT("night_bed_open_after_goal"));
		const FText Prompt = Bed && Player.IsValid()
			? IIGInteractable::Execute_GetInteractionPrompt(Bed, Player.Get()) : FText::GetEmpty();
		Check(Prompt.ToString() == NSLOCTEXT("IGMissingFloor", "NightRestPrompt", "아침까지 쉬기").ToString(),
			TEXT("night_bed_prompt_is_rest"));
		AIGMissingFloorEvidence* Ceiling = FindEvidence(TEXT("MissingFloorNightOneCeilingListen"));
		Check(Ceiling && Ceiling->IsInteractionEnabled(), TEXT("night_one_ceiling_listen_open"));
		if (bShots)
		{
			// 침대를 본 화면(쉬기 안내와 시계 아래 줄)과 천장 밑 화면을 찍고 나서 귀를 기울인다.
			Stand(IGNightFlowProbe::BedSideFeet, IGNightFlowProbe::BedLookAt);
			Next(101);
			return;
		}
		Stand(IGNightFlowProbe::CeilingFeet, IGNightFlowProbe::CeilingLookAt);
		Check(Use(Ceiling), TEXT("night_one_ceiling_listened"));
		Check(Narrative->HasBeatPlayed(IGNightFlowProbe::AboveConfirmed), TEXT("night_one_above_confirmed"));
		Check(Ceiling && !Ceiling->IsInteractionEnabled(), TEXT("night_one_ceiling_listen_once"));
		// 귀를 뗀 뒤 403호로 들어간다. 그는 그녀가 아니라 천장 밑 소리 자리로 와야 한다.
		Stand(IGNightFlowProbe::BedSideFeet, IGNightFlowProbe::BedLookAt);
		Next(2);
		return;
	}
	case 101:
		// 밤 제목 카드(4.2초와 페이드)가 걷힌 뒤에 찍는다.
		if (PhaseSeconds < 6.0f)
		{
			return;
		}
		Shoot(TEXT("night_rest_prompt"));
		Stand(IGNightFlowProbe::CeilingFeet, IGNightFlowProbe::CeilingLookAt);
		Next(102);
		return;
	case 102:
		if (PhaseSeconds < 0.5f)
		{
			return;
		}
		Shoot(TEXT("ceiling_listen_prompt"));
		Next(103);
		return;
	case 103:
	{
		if (PhaseSeconds < 0.3f)
		{
			return;
		}
		AIGMissingFloorEvidence* Ceiling = FindEvidence(TEXT("MissingFloorNightOneCeilingListen"));
		Check(Use(Ceiling), TEXT("night_one_ceiling_listened"));
		Check(Narrative->HasBeatPlayed(IGNightFlowProbe::AboveConfirmed), TEXT("night_one_above_confirmed"));
		Check(Ceiling && !Ceiling->IsInteractionEnabled(), TEXT("night_one_ceiling_listen_once"));
		Next(104);
		return;
	}
	case 104:
		// 생각 줄이 다 나타난 뒤 한 장 찍고 403호로 들어간다. 노크 자막은 다음 단계에서 찍는다.
		if (PhaseSeconds < 1.2f)
		{
			return;
		}
		Shoot(TEXT("night_one_above_thought"));
		Stand(IGNightFlowProbe::BedSideFeet, IGNightFlowProbe::BedLookAt);
		Next(2);
		return;
	case 2:
		// 2.6초 뒤 천장에서 한 번 울리고, 그는 4층 복도의 그 소리로 온다.
		if (bShots && !bKnockShot && PhaseSeconds > 1.6f)
		{
			bKnockShot = true;
			Shoot(TEXT("night_one_knock_caption"));
		}
		if (PhaseSeconds < 3.4f)
		{
			return;
		}
		if (AIGListenerEntity* Entity = Listener.Get())
		{
			const EIGListenerState State = Entity->GetListenerState();
			UE_LOG(LogIndieGame, Display, TEXT("NIGHTFLOW_LISTENER state=%d dormant=%d"), static_cast<int32>(State), Entity->IsDormant() ? 1 : 0);
			Check(!Entity->IsDormant()
				&& (State == EIGListenerState::Investigating || State == EIGListenerState::Holding
					|| State == EIGListenerState::Searching || State == EIGListenerState::Listening
					|| State == EIGListenerState::Chasing),
				TEXT("ceiling_answer_draws_listener"));
			// 이제 그를 재운다. 문밖이 조용해진 뒤에 눕는 경우만 본다.
			Entity->SetDormant(true);
		}
		Stand(IGNightFlowProbe::BedSideFeet, IGNightFlowProbe::BedLookAt);
		Check(Use(Bed), TEXT("night_bed_rest_used"));
		Next(3);
		return;
	case 3:
		if (Hour->IsHourActive() && PhaseSeconds < 5.0f)
		{
			return;
		}
		Check(!Hour->IsHourActive(), TEXT("rest_brings_dawn"));
		Next(4);
		return;
	case 4:
		if (!IsDay())
		{
			if (PhaseSeconds > 10.0f)
			{
				Check(false, TEXT("morning_arrives"));
				Finish();
			}
			return;
		}
		{
			const FText Prompt = Bed && Player.IsValid()
				? IIGInteractable::Execute_GetInteractionPrompt(Bed, Player.Get()) : FText::GetEmpty();
			Check(Bed && Bed->IsInteractionEnabled()
				&& Prompt.ToString() == NSLOCTEXT("IGMissingFloor", "SleepPrompt", "침대에 눕기").ToString(),
				TEXT("day_bed_prompt_is_sleep"));
		}
		// 낮의 침대로 둘째 밤을 연다.
		Check(Use(Bed), TEXT("day_bed_sleep_used"));
		Next(5);
		return;
	case 5:
		if (!(Hour->IsHourActive() && Narrative->GetNightIndex() == 2))
		{
			if (PhaseSeconds > 15.0f)
			{
				Check(false, TEXT("night_two_started"));
				Finish();
			}
			return;
		}
		if (AIGListenerEntity* Entity = Listener.Get())
		{
			Entity->SetDormant(true);
		}
		Check(Bed && !Bed->IsInteractionEnabled(), TEXT("night_two_bed_closed_before_goal"));
		// 문서를 읽는 동안 이야기 시계가 서는가.
		{
			AIGReadableNote* Note = FindNote();
			Check(Note != nullptr, TEXT("readable_note_found"));
			MarkedElapsed = Hour->GetHourElapsedSeconds();
			Check(Note && Use(Note) && AIGReadableNote::GetOpenNote() != nullptr, TEXT("note_opened"));
			OpenedNote = Note;
		}
		Next(6);
		return;
	case 6:
		if (PhaseSeconds < 3.0f)
		{
			return;
		}
		Check(FMath::IsNearlyEqual(Hour->GetHourElapsedSeconds(), MarkedElapsed, 0.3f), TEXT("reading_pauses_clock"));
		if (AIGReadableNote* Note = OpenedNote.Get())
		{
			Note->Close();
		}
		MarkedElapsed = Hour->GetHourElapsedSeconds();
		Next(7);
		return;
	case 7:
		if (PhaseSeconds < 2.0f)
		{
			return;
		}
		Check(Hour->GetHourElapsedSeconds() > MarkedElapsed + 1.0f, TEXT("clock_runs_after_reading"));
		// 05:30 30초 앞까지 민다(포획 복구와 같은 길). 그다음 문서를 다시 펼친다.
		Hour->ConsumeHourSeconds(100000.0f);
		if (AIGReadableNote* Note = FindNote())
		{
			Use(Note);
			OpenedNote = Note;
		}
		Next(8);
		return;
	case 8:
		// 읽는 동안에는 05:30이 오지 않는다.
		if (PhaseSeconds < 40.0f)
		{
			if (!Hour->IsHourActive())
			{
				Check(false, TEXT("dawn_waits_for_reading"));
				Next(9);
			}
			return;
		}
		Check(Hour->IsHourActive(), TEXT("dawn_waits_for_reading"));
		if (AIGReadableNote* Note = OpenedNote.Get())
		{
			Note->Close();
		}
		Next(9);
		return;
	case 9:
		if (Hour->IsHourActive() && PhaseSeconds < 40.0f)
		{
			return;
		}
		Check(!Hour->IsHourActive(), TEXT("dawn_comes_after_reading"));
		Check(!Narrative->HasBeatPlayed(IGNightFlowProbe::NightTwoGoal), TEXT("missed_night_not_marked"));
		Next(10);
		return;
	case 10:
		if (!IsDay())
		{
			return;
		}
		// 못 채운 밤은 다음 저녁에 같은 밤이 온다.
		Check(Use(Bed), TEXT("missed_day_sleep_used"));
		Next(11);
		return;
	case 11:
		if (!Hour->IsHourActive())
		{
			if (PhaseSeconds > 15.0f)
			{
				Check(false, TEXT("missed_night_repeats"));
				Finish();
			}
			return;
		}
		Check(Narrative->GetNightIndex() == 2, TEXT("missed_night_repeats"));
		// 일을 마친 뒤 저장한 밤을 불러온 것처럼 되살린다: 아침으로 넘기고 같은 밤을 이어 연다.
		Hour->CompleteNightGoal();
		Hour->ConcludeNight();
		Next(12);
		return;
	case 12:
		if (!IsDay())
		{
			return;
		}
		Hour->ResumeTheHour(2, 120.0f);
		Check(Hour->IsHourActive() && Hour->IsGoalComplete(), TEXT("resume_restores_goal"));
		Check(Bed && Bed->IsInteractionEnabled(), TEXT("resume_reopens_night_bed"));
		Finish();
		return;
	default:
		Finish();
		return;
	}
}
