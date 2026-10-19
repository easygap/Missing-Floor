#include "Sequence/IGStoreCounterProbe.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGListenerGreyboxDirector.h"
#include "Entity/IGManagerPatrol.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "Environment/IGStoreClerk.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGReadableNote.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGStoreCounterProbe
{
	// 가게 서쪽 유리 앞 골목(자동문 2413,-457 바깥)과 문 안쪽, 계산대 앞 손님 자리(벨 2690,-216 앞).
	const FVector AlleyFeet(2250.0f, -457.0f, 6.0f);
	const FVector DoorInside(2480.0f, -457.0f, 6.0f);
	const FVector CounterFeet(2655.0f, -300.0f, 6.0f);
	const FVector BellAim(2690.0f, -216.0f, 104.0f);
	/** 나린과 포스·프린터를 한 화면에 담는 자리. */
	const FVector WorkAim(2595.0f, -185.0f, 118.0f);
	/** 계산대 가운데. 가게 안인지 잴 때 쓴다(감독의 StoreCounterCenter와 같은 자리다). */
	const FVector CounterMiddle(2600.0f, -229.0f, 100.0f);
}

AIGStoreCounterProbe::AIGStoreCounterProbe()
{
	// -IGStoreCounterProbe로만 선다. 입력을 넣고 대사·자막·소리를 매 프레임 지켜본다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGStoreCounterProbe::Configure(AIGPlayerCharacter* InPlayer, AIGListenerGreyboxDirector* InDirector)
{
	Player = InPlayer;
	Director = InDirector;
	FParse::Value(FCommandLine::Get(), TEXT("IGStoreCounterProbe="), Variant);
	if (Variant.IsEmpty())
	{
		Variant = TEXT("stay");
	}
	bRecord = FParse::Param(FCommandLine::Get(), TEXT("IGStoreCounterRecord"));
}

void AIGStoreCounterProbe::Check(const bool bCondition, const FString& Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("STORE_CHECK %s %s"), *Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGStoreCounterProbe::Event(const FString& What)
{
	const UWorld* World = GetWorld();
	UE_LOG(LogIndieGame, Display, TEXT("STORE_AV t=%.3f %s"), World ? World->GetTimeSeconds() : 0.0, *What);
}

void AIGStoreCounterProbe::Shoot(const FString& Name)
{
	if (!bRecord)
	{
		return;
	}
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StoreCounterAV"), Variant));
	IFileManager::Get().MakeDirectory(*Directory, true);
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, Name + TEXT(".png")), false, false);
	Event(FString::Printf(TEXT("shot %s"), *Name));
}

void AIGStoreCounterProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Route.ReleaseKeys();
	if (bRecording)
	{
		const FString Directory = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StoreCounterAV"), Variant));
		IFileManager::Get().MakeDirectory(*Directory, true);
		UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, TEXT("mix"), Directory);
		Event(TEXT("record_stop"));
		bRecording = false;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("STORE_PROBE %s variant=%s failures=%d seconds=%.1f"),
		Failures ? TEXT("FAIL") : TEXT("PASS"), *Variant, Failures, TotalSeconds);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

bool AIGStoreCounterProbe::IsReceiptShown() const
{
	const AIGReadableNote* Note = Receipt.Get();
	return Note && !Note->IsHidden();
}

bool AIGStoreCounterProbe::IsStoreTalkBusy() const
{
	return AIGHorrorHUD::HasDialogueInContext(this, FName(TEXT("Store.CounterTalk")))
		|| AIGHorrorHUD::HasDialogueInContext(this, FName(TEXT("Store.Receipt")));
}

void AIGStoreCounterProbe::WatchState()
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	if (Hud)
	{
		const FString Line = Hud->GetActiveDialogueLineForTesting().ToString();
		if (Line != LastDialogue)
		{
			const FString Speaker = Hud->GetActiveDialogueSpeakerForTesting().ToString();
			Event(Line.IsEmpty() ? FString(TEXT("dialogue_end")) : FString::Printf(TEXT("dialogue speaker=\"%s\" line=\"%s\""), *Speaker, *Line));
			if (!Line.IsEmpty() && Speaker.Contains(TEXT("유담")))
			{
				++YudamQuestions;
			}
			LastDialogue = Line;
		}
		const FString Caption = Hud->GetActiveAudioCaptionForTesting().ToString();
		if (Caption != LastCaption)
		{
			Event(Caption.IsEmpty() ? FString(TEXT("caption_end")) : FString::Printf(TEXT("caption \"%s\""), *Caption));
			LastCaption = Caption;
			bAnyCaptionSeen |= !Caption.IsEmpty();
			bTerminalCaptionSeen |= Caption.Contains(TEXT("단말기"));
			bPrinterCaptionSeen |= Caption.Contains(TEXT("영수증"));
		}
	}
	const bool bReceipt = IsReceiptShown();
	if (bReceipt != bLastReceipt)
	{
		Event(bReceipt ? TEXT("receipt_shown") : TEXT("receipt_hidden"));
		if (bReceipt && ReceiptShownAt < 0.0)
		{
			ReceiptShownAt = World ? World->GetTimeSeconds() : 0.0;
		}
		bLastReceipt = bReceipt;
	}
	const bool bBusy = Clerk.IsValid() && Clerk->IsBusy();
	if (bBusy != bLastClerkBusy)
	{
		Event(bBusy ? TEXT("clerk_busy") : TEXT("clerk_idle"));
		if (bBusy && ClerkFirstBusyAt < 0.0)
		{
			ClerkFirstBusyAt = World ? World->GetTimeSeconds() : 0.0;
		}
		bLastClerkBusy = bBusy;
	}
	if (World && Player.IsValid() && World->GetTimeSeconds() - LastPositionLogAt >= 0.5)
	{
		LastPositionLogAt = World->GetTimeSeconds();
		const FVector At = Player->GetActorLocation();
		const FRotator View = PC ? PC->GetControlRotation() : FRotator::ZeroRotator;
		Event(FString::Printf(TEXT("player at=%.1f,%.1f,%.1f yaw=%.1f pitch=%.1f counter_dist=%.0f"), At.X, At.Y, At.Z,
			View.Yaw, View.Pitch, FVector::Dist2D(At, IGStoreCounterProbe::CounterMiddle)));
	}
}

void AIGStoreCounterProbe::BuildRoute()
{
	using namespace IGStoreCounterProbe;
	TWeakObjectPtr<AIGStoreCounterProbe> WeakThis(this);
	Route.Reset(Player.Get(), [WeakThis](const bool bPass, const FString& Name)
	{
		if (AIGStoreCounterProbe* Self = WeakThis.Get())
		{
			Self->Check(bPass, Name);
		}
	});
	const auto Now = [WeakThis]()
	{
		const AIGStoreCounterProbe* Self = WeakThis.Get();
		const UWorld* World = Self ? Self->GetWorld() : nullptr;
		return World ? World->GetTimeSeconds() : 0.0;
	};
	Route.Walk(DoorInside, 45.0f, TEXT("walk_in_door"));
	Route.Walk(CounterFeet, 35.0f, TEXT("walk_to_counter"));
	Route.Aim(BellAim, TEXT("aim_bell"));
	Route.Press(0.25f, TEXT("bell_delivery"), Bell.Get());
	Route.Until([WeakThis, Now]()
	{
		AIGStoreCounterProbe* Self = WeakThis.Get();
		if (Self && Self->FirstBellAt < 0.0)
		{
			Self->FirstBellAt = Now();
		}
		return Self && Self->LastDialogue.IsEmpty() && !Self->IsStoreTalkBusy() && Now() - Self->FirstBellAt > 1.0;
	}, 30.0f, TEXT("delivery_talk_heard"));
	Route.Until([WeakThis, Now]() { return WeakThis.IsValid() && Now() - WeakThis->FirstBellAt > 7.4; }, 12.0f, TEXT("bell_rest"));
	Route.Aim(BellAim, TEXT("aim_bell_again"));
	Route.Press(0.25f, TEXT("bell_receipt"), Bell.Get());
	if (Variant == TEXT("leave"))
	{
		// 나린이 단말기를 누르기 시작하면 가게를 나간다. 나린은 혼자 찾아 뽑아 둔다.
		Route.Until([WeakThis]() { return WeakThis.IsValid() && WeakThis->ClerkFirstBusyAt > 0.0; }, 30.0f, TEXT("clerk_starts_search"));
		Route.Walk(DoorInside, 45.0f, TEXT("leave_to_door"));
		Route.Walk(AlleyFeet, 45.0f, TEXT("leave_to_alley"));
		Route.Wait(16.0f, TEXT("outside_while_working"));
		Route.Until([WeakThis]()
		{
			return WeakThis.IsValid() && WeakThis->LastCaption.IsEmpty() && !WeakThis->IsStoreTalkBusy();
		}, 4.0f, TEXT("nothing_lingers_outside"));
		Route.Walk(DoorInside, 45.0f, TEXT("return_to_door"));
		Route.Walk(CounterFeet, 35.0f, TEXT("return_to_counter"));
		Route.Until([WeakThis]() { return WeakThis.IsValid() && WeakThis->IsReceiptShown(); }, 30.0f, TEXT("receipt_waits_on_counter"));
	}
	else
	{
		Route.Aim(WorkAim, TEXT("watch_narin"));
		if (Variant == TEXT("spam"))
		{
			// 일하는 동안 벨을 거듭 누른다. 질문이 늘거나 일이 겹치면 안 된다.
			Route.Until([WeakThis]() { return WeakThis.IsValid() && WeakThis->ClerkFirstBusyAt > 0.0; }, 30.0f, TEXT("clerk_starts_search"));
			Route.Until([WeakThis]()
			{
				if (AIGStoreCounterProbe* Self = WeakThis.Get())
				{
					Self->YudamQuestionsAtSpam = Self->YudamQuestions;
				}
				return true;
			}, 1.0f, TEXT("count_questions"));
			for (int32 Press = 0; Press < 3; ++Press)
			{
				Route.Aim(BellAim, FString::Printf(TEXT("aim_bell_spam_%d"), Press));
				Route.Press(0.2f, FString::Printf(TEXT("bell_spam_%d"), Press), Bell.Get());
				Route.Wait(0.9f, FString::Printf(TEXT("bell_spam_gap_%d"), Press));
			}
			Route.Aim(WorkAim, TEXT("watch_narin_again"));
		}
		Route.Until([WeakThis]() { return WeakThis.IsValid() && WeakThis->IsReceiptShown(); }, 45.0f, TEXT("receipt_placed"));
		Route.Until([WeakThis]() { return WeakThis.IsValid() && !WeakThis->Clerk->IsBusy() && !WeakThis->IsStoreTalkBusy(); }, 15.0f,
			TEXT("narin_done"));
	}
	Route.Aim(AIGStoreClerk::GetReceiptRestTransform().GetLocation() + FVector(0.0f, 0.0f, 0.5f), TEXT("aim_receipt"));
	Route.Press(0.25f, TEXT("read_receipt"), Receipt.Get());
	Route.Wait(3.0f, TEXT("reading"));
	Route.Press(0.15f, TEXT("close_receipt"));
	Route.Wait(2.5f, TEXT("thought_after_reading"));
	Route.Walk(DoorInside, 45.0f, TEXT("walk_out_door"));
	Route.Walk(AlleyFeet, 45.0f, TEXT("walk_to_alley"));
	Route.Wait(6.0f, TEXT("outside_wait"));
	Route.Until([WeakThis]()
	{
		return WeakThis.IsValid() && WeakThis->LastCaption.IsEmpty() && !WeakThis->IsStoreTalkBusy() && !WeakThis->Clerk->IsBusy();
	}, 3.0f, TEXT("nothing_lingers_after_leaving"));
	Route.Walk(DoorInside, 45.0f, TEXT("walk_back_in"));
	Route.Walk(CounterFeet, 35.0f, TEXT("back_at_counter"));
	Route.Wait(3.0f, TEXT("no_replay_window"));
	Route.Until([WeakThis]()
	{
		return WeakThis.IsValid() && !WeakThis->Clerk->IsBusy() && !WeakThis->LastCaption.Contains(TEXT("영수증"))
			&& !WeakThis->LastCaption.Contains(TEXT("단말기"));
	}, 1.0f, TEXT("no_replay_on_return"));
	Route.Aim(BellAim, TEXT("aim_bell_last"));
	Route.Press(0.25f, TEXT("bell_after_receipt"), Bell.Get());
	Route.Wait(1.0f, TEXT("bell_after_wait"));
	Route.Until([WeakThis]() { return WeakThis.IsValid() && WeakThis->LastDialogue.IsEmpty() && !WeakThis->IsStoreTalkBusy(); },
		25.0f, TEXT("last_talk_heard"));
}

void AIGStoreCounterProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	TotalSeconds += DeltaSeconds;
	PhaseSeconds += DeltaSeconds;
	if (TotalSeconds > 260.0f)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	UWorld* World = GetWorld();
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	if (Phase == 0)
	{
		// 셰이더가 다 서고 장면이 자리 잡을 때까지 기다린다.
		if (PhaseSeconds < (bRecord ? 10.0f : 1.5f))
		{
			return;
		}
		for (TActorIterator<AIGStoreClerk> It(World); It; ++It)
		{
			Clerk = *It;
		}
		for (TActorIterator<AIGMissingFloorEvidence> It(World); It; ++It)
		{
			if (It->GetName() == TEXT("MissingFloorArrivalStoreBell"))
			{
				Bell = *It;
			}
		}
		for (TActorIterator<AIGReadableNote> It(World); It; ++It)
		{
			if (It->GetName() == TEXT("StoreParcelReceipt"))
			{
				Receipt = *It;
			}
		}
		Check(Narrative && Player.IsValid() && Clerk.IsValid() && Bell.IsValid() && Receipt.IsValid(),
			FString::Printf(TEXT("store_actors_resolved narrative=%d player=%d clerk=%d bell=%d receipt=%d"), Narrative ? 1 : 0,
				Player.IsValid() ? 1 : 0, Clerk.IsValid() ? 1 : 0, Bell.IsValid() ? 1 : 0, Receipt.IsValid() ? 1 : 0));
		if (!Narrative || !Player.IsValid() || !Clerk.IsValid() || !Bell.IsValid() || !Receipt.IsValid())
		{
			Finish();
			return;
		}
		// 준비(상태): 밤으로 선 무대면 밤을 채우고 끝내 낮으로 넘긴다(가게는 낮과 저녁에 연다).
		for (TActorIterator<AIGNightPhaseDirector> It(World); It; ++It)
		{
			if (It->IsHourActive())
			{
				It->CompleteNightGoal();
				It->ConcludeNight();
			}
			NightPhase = *It;
		}
		Phase = 1;
		PhaseSeconds = 0.0f;
		return;
	}
	if (Phase == 1)
	{
		const bool bDay = NightPhase.IsValid() && !NightPhase->IsHourActive() && !NightPhase->IsDawnTransitionInProgress();
		if (!bDay)
		{
			if (PhaseSeconds > 45.0f)
			{
				Check(false, TEXT("day_before_store"));
				Finish();
			}
			return;
		}
		if (PhaseSeconds < 4.0f)
		{
			return;
		}
		// 준비(상태): 위협은 재운다. 나린과 인사했고 뒤편 택배 안내문을 읽었다.
		for (TActorIterator<AIGListenerEntity> It(World); It; ++It)
		{
			It->SetDormant(true);
		}
		for (TActorIterator<AIGNightThreatDirector> It(World); It; ++It)
		{
			It->SetActorTickEnabled(false);
		}
		for (TActorIterator<AIGManagerPatrol> It(World); It; ++It)
		{
			It->SetActorTickEnabled(false);
		}
		Narrative->MarkBeatPlayed(FName(TEXT("Arrival.Store")));
		Narrative->MarkBeatPlayed(FName(TEXT("Neighborhood.Delivery")));
		AIGHorrorHUD::ResetDialogueForSceneChange(this);
		// 준비(순간 이동): 가게 서쪽 골목, 자동문을 본다.
		APlayerController* PC = Cast<APlayerController>(Player->GetController());
		const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Player->SetActorLocation(IGStoreCounterProbe::AlleyFeet + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr,
			ETeleportType::TeleportPhysics);
		Player->GetCharacterMovement()->StopMovementImmediately();
		if (PC)
		{
			PC->SetControlRotation(FRotator(-5.0f, 0.0f, 0.0f));
		}
		Event(FString::Printf(TEXT("setup variant=%s day=%d teleport=alley flags=Arrival.Store,Neighborhood.Delivery record=%d bell_enabled=%d"),
			*Variant, Narrative->GetNightIndex(), bRecord ? 1 : 0, Bell->IsInteractionEnabled() ? 1 : 0));
		BuildRoute();
		if (bRecord)
		{
			UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 240.0f);
			bRecording = true;
			Event(TEXT("record_start"));
		}
		Phase = 2;
		PhaseSeconds = 0.0f;
		return;
	}
	WatchState();
	// 그때그때 찍는다. 단말기를 누르는 몸, 종이가 나오는 프린터, 손님 앞에 놓인 영수증.
	if (bRecord && World)
	{
		const double Now = World->GetTimeSeconds();
		if (!bShotTap && ClerkFirstBusyAt > 0.0 && Now - ClerkFirstBusyAt > 0.55 && Variant != TEXT("leave"))
		{
			bShotTap = true;
			Shoot(TEXT("01_narin_taps_pos"));
		}
		// 자막이 아니라 프린터가 실제로 돌기 시작한 때를 잡는다. 소리 설명은 기본으로 꺼져 있다.
		if (!bShotPrint && Clerk.IsValid() && Clerk->IsPrinting())
		{
			bShotPrint = true;
			PrintStartedAt = Now;
		}
		if (bShotPrint && !bShotPrinting && PrintStartedAt > 0.0 && Now - PrintStartedAt > 0.9)
		{
			bShotPrinting = true;
			Shoot(TEXT("02_receipt_printing"));
		}
		if (!bShotPlaced && ReceiptShownAt > 0.0 && Now - ReceiptShownAt > 0.4)
		{
			bShotPlaced = true;
			Shoot(TEXT("03_receipt_on_counter"));
		}
	}
	if (Route.Tick(DeltaSeconds))
	{
		if (Narrative)
		{
			Check(Narrative->HasBeatPlayed(FName(TEXT("Neighborhood.ParcelReceipt"))), TEXT("receipt_was_read"));
		}
		if (Variant == TEXT("spam"))
		{
			Check(YudamQuestionsAtSpam >= 0 && YudamQuestions == YudamQuestionsAtSpam + 1,
				FString::Printf(TEXT("bell_spam_adds_no_question questions=%d at_spam=%d"), YudamQuestions, YudamQuestionsAtSpam));
		}
		Check(ReceiptShownAt > 0.0, TEXT("receipt_appeared"));
		// 가게 일하는 소리는 「모든 소리」를 고른 사람에게만 글로 뜬다. 기본 화면에는 소리 설명이 없다.
		const UGameInstance* GameInstance = GetGameInstance();
		const UIGAccessibilitySubsystem* Accessibility = GameInstance
			? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>() : nullptr;
		const bool bCaptionsOn = Accessibility && Accessibility->AreSoundCaptionsEnabled();
		const bool bAllSounds = Accessibility && Accessibility->AreBackgroundSoundCaptionsEnabled();
		UE_LOG(LogIndieGame, Display, TEXT("STORE_CAPTIONS mode=%s any=%d terminal=%d printer=%d"),
			bAllSounds ? TEXT("all") : bCaptionsOn ? TEXT("important") : TEXT("off"),
			bAnyCaptionSeen ? 1 : 0, bTerminalCaptionSeen ? 1 : 0, bPrinterCaptionSeen ? 1 : 0);
		if (!bCaptionsOn)
		{
			Check(!bAnyCaptionSeen, TEXT("default_screen_has_no_sound_captions"));
		}
		else if (!bAllSounds)
		{
			Check(!bTerminalCaptionSeen && !bPrinterCaptionSeen, TEXT("important_sounds_skip_store_work"));
		}
		else if (Variant == TEXT("leave"))
		{
			// 프린터는 밖에 나가 있는 동안 돌았다. 들리지 않은 소리는 글로도 뜨지 않는다.
			Check(!bPrinterCaptionSeen, TEXT("all_sounds_skips_printer_heard_from_outside"));
		}
		else
		{
			Check(bTerminalCaptionSeen && bPrinterCaptionSeen, TEXT("all_sounds_describes_store_work"));
		}
		Shoot(TEXT("04_back_at_counter"));
		Finish();
	}
}
