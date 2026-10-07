#include "Entity/IGListenerGreyboxDirector.h"

#include "Core/IGPrologueWorldScene.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGMissingFloorEpilogueDirector.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGOnboardingMemory.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGPlayerController.h"
#include "Save/IGSaveGame.h"
#include "Save/IGSaveSubsystem.h"
#include "Serialization/JsonSerializer.h"

namespace IGEndingReview
{
	const FName Checkpoint(TEXT("Checkpoint.MissingFloor.EndingChoice"));

	bool IsChoiceCheckpoint(const UIGSaveGame* Save)
	{
		if (!Save || Save->Progress.CheckpointTag.GetTagName() != Checkpoint) { return false; }
		const FIGMissingFloorNightState& Night = Save->Progress.MissingFloorNarrative.Night;
		return Night.NightIndex == 4 && Night.bTheHourSealed
			&& Night.NightFourControlOrder.Num() == 3 && Night.NightFourWallStrikeCount == 5
			&& Night.bNightFourWallOpened && Night.bFirstReportMade && !Night.bSecondReportMade
			&& Night.EndingChoice.IsNone()
			&& Night.CompletedBeats.Contains(FName(TEXT("Night4.ChoiceOffered")));
	}
}

void AIGListenerGreyboxDirector::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bEndingLifecycleDone) { return; }
	if (FPlatformTime::Seconds() - EndingProbeStartedAt > (bEndingCheckpointWrite ? 420.0 : 180.0))
	{
		FinishEndingLifecycleProbe(false, TEXT("엔딩 검사 시간 초과"));
		return;
	}
	if (!bEndingCheckpointWrite) { AdvanceEndingLifecycleProbe(); }
}

void AIGListenerGreyboxDirector::WriteEndingCheckpointForProbe()
{
	if (bEndingCheckpointRequested) { return; }
	bEndingCheckpointRequested = true;
	// 이 지점은 FullGame의 퍼즐·추격·밤4 재시도·벽 대치 검사를 모두 지난 뒤다.
	// 지금 서사 상태를 그대로 저장하며 엔딩이나 증거를 만들어 넣지 않는다.
	GetWorldTimerManager().ClearTimer(ProbeTimer);
	UIGSaveSubsystem* Save = GetGameInstance()->GetSubsystem<UIGSaveSubsystem>();
	if (!Save) { FinishEndingLifecycleProbe(false, TEXT("저장 서브시스템 없음")); return; }
	Save->OnSaveCompleted.AddDynamic(this, &AIGListenerGreyboxDirector::HandleEndingCheckpointSaved);
	if (!Save->RequestAutosave(
		FGameplayTag::RequestGameplayTag(FName(TEXT("Chapter.MissingFloor")), false),
		GetWorld()->GetOutermost()->GetFName(),
		FGameplayTag::RequestGameplayTag(IGEndingReview::Checkpoint, false)))
	{
		FinishEndingLifecycleProbe(false, TEXT("선택 직전 자동 저장 요청 실패"));
	}
}

void AIGListenerGreyboxDirector::HandleEndingCheckpointSaved(const bool bSuccess, FString SlotName)
{
	if (bEndingLifecycleDone) { return; }
	if (!bSuccess) { FinishEndingLifecycleProbe(false, TEXT("선택 직전 자동 저장 쓰기 실패")); return; }
	const UIGSaveSubsystem* Save = GetGameInstance()->GetSubsystem<UIGSaveSubsystem>();
	if (!Save || Save->IsBusy()) { return; }
	const UIGSaveGame* Written = Cast<UIGSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!IGEndingReview::IsChoiceCheckpoint(Written)) { return; }
	EndingCheckpointSlot = SlotName;
	FinishEndingLifecycleProbe(true, TEXT("실제 완주 과정의 선택 직전 저장을 디스크에서 확인함"));
}

void AIGListenerGreyboxDirector::AdvanceEndingLifecycleProbe()
{
	AIGPlayerController* Controller = Cast<AIGPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!Controller) { return; }
	const FName ExpectedEnding(*FString::Printf(TEXT("Ending.%s"), *EndingProbeChoice));
	const bool bEndingB = EndingProbeChoice == TEXT("B");
	if (bEndingProfileProbe)
	{
		// 새 프로세스의 타이틀이 실제 프로필을 읽어 다섯째 밤 행을 만드는지 본다.
		// IGNightFiveProbe의 가짜 해금 분기는 쓰지 않는다.
		if (EndingLifecycleStep++ == 0) { Controller->ShowTitleAfterEnding(); return; }
		bool bExperienced = false;
		GConfig->GetBool(TEXT("IndieGame.MissingFloorProfile"), TEXT("EpilogueExperienced"),
			bExperienced, GGameUserSettingsIni);
		const bool bPassed = !IGOnboardingMemory::IsSessionOnly()
			&& IGOnboardingMemory::HasSeenEnding(ExpectedEnding)
			&& IGOnboardingMemory::HasSeenEnding(FName(TEXT("Ending.B"))) == bEndingB
			&& Controller->IsNightFiveAvailable() == bEndingB
			&& Controller->IsSystemMenuRowEnabled(5) == bEndingB && bExperienced;
		FinishEndingLifecycleProbe(bPassed, bPassed
			? TEXT("새 프로세스에서 엔딩 기록과 다섯째 밤 해금이 유지됨")
			: TEXT("새 프로세스에서 엔딩 기록 또는 다섯째 밤 해금이 사라짐"));
		return;
	}
	if (!bStageReady) { return; }
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	UIGSaveSubsystem* Save = GetGameInstance()->GetSubsystem<UIGSaveSubsystem>();
	if (!Narrative || !Save || !NightFour || !NightPhase || !Epilogue || !Player.IsValid())
	{
		FinishEndingLifecycleProbe(false, TEXT("엔딩 검사의 실제 게임 무대가 준비되지 않음")); return;
	}
	const double Now = FPlatformTime::Seconds();
	if (EndingLifecycleStep == 0)
	{
		const UIGSaveGame* Loaded = Save->GetLastLoadedSave();
		bool bSameSources = IGEndingReview::IsChoiceCheckpoint(Loaded);
		if (bSameSources)
		{
			const FIGMissingFloorNarrativeSnapshot& Restored = Narrative->GetSnapshot();
			const FIGMissingFloorNarrativeSnapshot& Original = Loaded->Progress.MissingFloorNarrative;
			bSameSources = Restored.Truths.Num() == Original.Truths.Num()
				&& Restored.Night.NightFourControlOrder == Original.Night.NightFourControlOrder
				&& Restored.Night.Witnesses == Original.Night.Witnesses;
			for (const FIGMissingFloorTruthRecord& Record : Original.Truths)
			{
				const FIGMissingFloorTruthRecord* Found = Restored.Truths.FindByPredicate(
					[&Record](const FIGMissingFloorTruthRecord& Value) { return Value.TruthTag == Record.TruthTag; });
				bSameSources = bSameSources && Found && Found->SourceIds == Record.SourceIds
					&& Found->bConfirmed == Record.bConfirmed;
			}
		}
		if (!bSameSources || !GetWorld()->URL.HasOption(TEXT("IGResumeSave"))
			|| Narrative->GetNightIndex() != 4 || !Narrative->GetEndingChoice().IsNone()
			|| Narrative->GetNightFourWallStrikeCount() != 5 || !Narrative->WasFirstReportMade()
			|| Narrative->WasSecondReportMade()
			|| !Narrative->IsFinalChoiceUnlocked() || !NightPhase->IsHourActive()
			|| !NightFour->IsFinalConfrontationComplete()
			|| !WorldScene.IsValid() || !WorldScene->IsMissingFloorCavityOpen())
		{
			FinishEndingLifecycleProbe(false, TEXT("정상 불러오기 경로에서 밤4의 증거·벽·선택지가 복원되지 않음")); return;
		}
		if (IGOnboardingMemory::HasSeenEnding(FName(TEXT("Ending.A")))
			|| IGOnboardingMemory::HasSeenEnding(FName(TEXT("Ending.B"))))
		{
			FinishEndingLifecycleProbe(false, TEXT("분기 검사는 엔딩을 본 적 없는 독립 프로필이어야 함")); return;
		}
		AIGMissingFloorEvidence* Target = bEndingB ? NightFour->GetEndingBTarget() : NightFour->GetEndingATarget();
		if (!Target || Target->IsHidden() || !Target->IsInteractionEnabled())
		{
			FinishEndingLifecycleProbe(false, TEXT("불러온 게임에서 엔딩 선택 대상이 비활성 상태임")); return;
		}
		bEndingRestored = true;
		EndingChoiceStartedAt = Now;
		FIGInteractionContext Context;
		Context.Interactor = Player.Get();
		Context.TargetActor = Target;
		Context.HoldProgress = 1.0f;
		IIGInteractable::Execute_CompleteInteraction(Target, Context);
		if (Narrative->GetEndingChoice() != ExpectedEnding)
		{
			FinishEndingLifecycleProbe(false, TEXT("복원된 선택 대상이 결말을 확정하지 못함")); return;
		}
		EndingLifecycleStep = 1;
	}
	if (EndingLifecycleStep == 1)
	{
		if (bEndingB && NightFour->IsEndingBVigilActive())
		{
			bEndingVigilObserved = true;
			EndingVigilSeconds = Now - EndingChoiceStartedAt;
			if (EndingVigilSeconds >= 7.0 && !Narrative->WasSecondReportMade()
				&& !Epilogue->IsActive() && !Controller->IsMoveInputIgnored()
				&& !Controller->IsLookInputIgnored())
			{
				bEndingSevenSecondsObserved = true;
			}
		}
		if (!Epilogue->IsActive()) { return; }
		if (Epilogue->GetEndingId() != ExpectedEnding || Epilogue->IsReplaySkipAvailable()
			|| !Narrative->WasFirstReportMade() || !Narrative->WasSecondReportMade()
			|| NightPhase->IsHourActive()
			|| (bEndingB && (!bEndingVigilObserved || !bEndingSevenSecondsObserved || EndingVigilSeconds < 29.5)))
		{
			FinishEndingLifecycleProbe(false, TEXT("실제 기다림·두 번째 신고·초회 에필로그 전환이 어긋남")); return;
		}
		EndingEpilogueStartedAt = Now;
		EndingLifecycleStep = 2;
	}
	if (EndingLifecycleStep == 2)
	{
		EndingEpilogueSeconds = Now - EndingEpilogueStartedAt;
		if (Epilogue->IsActive()) { return; }
		const AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD());
		const bool bPassed = EndingEpilogueSeconds >= (bEndingB ? 70.0 : 86.0)
			&& Epilogue->GetPlayedSceneCount() == (bEndingB ? 4 : 5)
			&& Hud && !Hud->IsMissingFloorEpilogueVisible()
			&& IGOnboardingMemory::HasSeenEnding(ExpectedEnding)
			&& Controller->IsNightFiveAvailable() == bEndingB
			&& Controller->IsSystemMenuRowEnabled(5) == bEndingB;
		FinishEndingLifecycleProbe(bPassed, bPassed
			? TEXT("실제 에필로그 종료와 타이틀의 다섯째 밤 상태를 확인함")
			: TEXT("에필로그의 재생 시간·장면 수·타이틀 복귀·프로필 기록이 어긋남"));
	}
}

void AIGListenerGreyboxDirector::FinishEndingLifecycleProbe(const bool bPassed, const FString& Reason)
{
	if (bEndingLifecycleDone) { return; }
	bEndingLifecycleDone = true;
	SetActorTickEnabled(false);
	GetWorldTimerManager().ClearTimer(ProbeTimer);
	const AIGPlayerController* Controller = Cast<AIGPlayerController>(GetWorld()->GetFirstPlayerController());
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("probe"), TEXT("MISSINGFLOOR_ENDING_LIFECYCLE"));
	Result->SetStringField(TEXT("status"), bPassed ? TEXT("PASS") : TEXT("FAIL"));
	Result->SetStringField(TEXT("mode"), bEndingCheckpointWrite ? TEXT("checkpoint")
		: bEndingProfileProbe ? TEXT("profile") : TEXT("ending"));
	Result->SetStringField(TEXT("ending"), EndingProbeChoice);
	Result->SetStringField(TEXT("reason"), Reason);
	Result->SetStringField(TEXT("saveSlot"), EndingCheckpointSlot);
	const FConfigBranch* SettingsBranch = GConfig
		? GConfig->FindBranch(FName(TEXT("GameUserSettings")), GGameUserSettingsIni) : nullptr;
	Result->SetStringField(TEXT("profilePath"), SettingsBranch
		? FPaths::ConvertRelativePathToFull(SettingsBranch->IniPath) : FString());
	Result->SetBoolField(TEXT("restoredThroughSaveSubsystem"), bEndingRestored);
	Result->SetBoolField(TEXT("vigilObserved"), bEndingVigilObserved);
	Result->SetBoolField(TEXT("sevenSecondWaitObserved"), bEndingSevenSecondsObserved);
	Result->SetBoolField(TEXT("endingASeen"), IGOnboardingMemory::HasSeenEnding(FName(TEXT("Ending.A"))));
	Result->SetBoolField(TEXT("endingBSeen"), IGOnboardingMemory::HasSeenEnding(FName(TEXT("Ending.B"))));
	Result->SetBoolField(TEXT("nightFiveAvailable"), Controller && Controller->IsNightFiveAvailable());
	Result->SetNumberField(TEXT("vigilSeconds"), EndingVigilSeconds);
	Result->SetNumberField(TEXT("epilogueSeconds"), EndingEpilogueSeconds);
	Result->SetNumberField(TEXT("epilogueScenes"), Epilogue ? Epilogue->GetPlayedSceneCount() : 0);
	FString Serialized;
	FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Serialized));
	FString Receipt;
	const bool bHasReceipt = FParse::Value(FCommandLine::Get(), TEXT("IGEndingResultPath="), Receipt);
	Receipt.TrimQuotesInline();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Receipt), true);
	const bool bWritten = bHasReceipt && FFileHelper::SaveStringToFile(Serialized, *Receipt,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_ENDING_LIFECYCLE %s: %s"),
		bPassed && bWritten ? TEXT("PASS") : TEXT("FAIL"), *Reason);
	FPlatformMisc::RequestExitWithStatus(false, bPassed && bWritten ? 0 : 1);
}
