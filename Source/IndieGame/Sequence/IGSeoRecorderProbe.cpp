#include "Sequence/IGSeoRecorderProbe.h"

#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGListenerGreyboxDirector.h"
#include "Entity/IGMissingFloorEpilogueDirector.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGSeoRecorder.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGSeoRecorderProbe
{
	/** 5층 철문 안쪽, 첫 판재 더미 앞. 내려다보면 판재 밑 녹음기 끈이 보인다. */
	const FVector AnnexFeet(-30.0f, 488.0f, 1200.0f);
	/** 셋째 낮 서일영이 섰던 샛길 입구 바로 앞의 약봉투. */
	const FVector PillsLocation(1234.0f, -690.0f, 0.2f);
	/** 약봉투를 찍는 자리. 한 걸음 떨어져 손전등으로 내려다본다. */
	const FVector PillsViewFeet(1196.0f, -628.0f, 0.0f);
	/** 첫째 밤 제목 카드가 걷히고 노출이 가라앉을 때까지. */
	constexpr float ShotSettleSeconds = 6.0f;
	constexpr float PlaybackTimeoutSeconds = 150.0f;
	constexpr float ProbeTimeoutSeconds = 240.0f;

	FText Line(const int32 Index)
	{
		switch (Index)
		{
		case 1: return NSLOCTEXT("IGMissingFloor", "SeoRecorderLine1", "7월 26일 새벽 4시 25분. 403호 서일영입니다. 지금 위에 올라가 봅니다.");
		case 2: return NSLOCTEXT("IGMissingFloor", "SeoRecorderLine2", "저기요. 거기 누구 있어요?");
		case 3: return NSLOCTEXT("IGMissingFloor", "SeoRecorderLine3", "미안하다는 말 말고요. 매일 이 시간이잖아요. 몇 달째인지 알아요?");
		case 4: return NSLOCTEXT("IGMissingFloor", "SeoRecorderLine4", "관리실은 여기 아무도 안 산대요. 그럼 당신은 뭔데요?");
		case 5: return NSLOCTEXT("IGMissingFloor", "SeoRecorderLine5", "…저기요. 괜찮아요?");
		default: return FText::GetEmpty();
		}
	}
	FText Thought()
	{
		return NSLOCTEXT("IGMissingFloor", "SeoRecorderThought", "오빠를 두고 그냥 내려간 거야?");
	}
	FText PillsRecorderThought()
	{
		return NSLOCTEXT("IGMissingFloor", "WitnessPillsThoughtRecorder", "서일영… 녹음기에서 이름 대던 그 사람이네.");
	}
	FText PillsAlleyThought()
	{
		return NSLOCTEXT("IGMissingFloor", "WitnessPillsThoughtAlley", "‘서일영 님’이라고 적혀 있네. 골목에 서 있던 그 사람 건가?");
	}

	bool Contains(const TArray<FText>& Lines, const FText& Wanted)
	{
		const FString Text = Wanted.ToString();
		return Lines.ContainsByPredicate([&Text](const FText& Candidate) { return Candidate.ToString().Equals(Text); });
	}
}

AIGSeoRecorderProbe::AIGSeoRecorderProbe()
{
	// 검사기는 매 프레임 화면에 뜬 말을 모은다. -IGSeoRecorderProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGSeoRecorderProbe::Configure(AIGPlayerCharacter* InPlayer, AIGListenerGreyboxDirector* InDirector)
{
	Player = InPlayer;
	Director = InDirector;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGSeoRecorderShots"));
}

void AIGSeoRecorderProbe::Stand(const FVector& Feet, const FVector& LookAt)
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

void AIGSeoRecorderProbe::Shoot(const TCHAR* Name)
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
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SeoRecorderReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("SEORECORDER_SHOT %s"), *Path);
}

void AIGSeoRecorderProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("SEORECORDER_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGSeoRecorderProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("SEORECORDER_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGSeoRecorderProbe::Next(const int32 NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
}

bool AIGSeoRecorderProbe::SaidLine(const FText& Text) const
{
	return SeenLines.Contains(Text.ToString());
}

void AIGSeoRecorderProbe::NoteLines()
{
	using namespace IGSeoRecorderProbe;
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	if (!Hud)
	{
		return;
	}
	TArray<FText> Lines = {Thought(), PillsRecorderThought(), PillsAlleyThought()};
	for (int32 Index = 1; Index <= 5; ++Index)
	{
		Lines.Add(Line(Index));
	}
	for (const FText& Text : Lines)
	{
		if (Hud->HasDialogueLineForTesting(Text.ToString()))
		{
			SeenLines.Add(Text.ToString());
		}
	}
}

void AIGSeoRecorderProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > IGSeoRecorderProbe::ProbeTimeoutSeconds)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	NoteLines();
	using namespace IGSeoRecorderProbe;
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	AIGListenerGreyboxDirector* Greybox = Director.Get();
	AIGMissingFloorEvidence* Pills = Greybox ? Greybox->GetSleepingPillsForTesting() : nullptr;
	AIGSeoRecorder* Machine = Recorder.Get();

	switch (Phase)
	{
	case 0:
	{
		if (PhaseSeconds < 1.0f)
		{
			return;
		}
		for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
		{
			NightThree = *It;
		}
		for (TActorIterator<AIGMissingFloorNightFourDirector> It(GetWorld()); It; ++It)
		{
			NightFour = *It;
		}
		for (TActorIterator<AIGMissingFloorEpilogueDirector> It(GetWorld()); It; ++It)
		{
			Epilogue = *It;
		}
		Recorder = NightThree.IsValid() ? NightThree->GetSeoRecorder() : nullptr;
		Machine = Recorder.Get();
		Check(Narrative && NightThree.IsValid() && NightFour.IsValid() && Epilogue.IsValid(), TEXT("directors_resolved"));
		Check(Machine != nullptr, TEXT("recorder_under_the_boards"));
		Check(Pills != nullptr, TEXT("pills_spawned"));
		if (!Narrative || !Machine || !Pills || !NightThree.IsValid() || !NightFour.IsValid() || !Epilogue.IsValid())
		{
			Finish();
			return;
		}
		Check(Machine->IsAvailable(), TEXT("recorder_can_be_taken_at_night"));
		Check(Machine->GetPickup()
			&& FVector::Dist2D(Machine->GetPickup()->GetActorLocation(), AIGSeoRecorder::GetRestingLocation()) < 12.0f,
			TEXT("pickup_covers_the_end_sticking_out"));
		Check(Pills->IsHidden() && !Pills->GetActorEnableCollision(), TEXT("pills_absent_before_the_alley"));
		// 5층 안, 판재 더미 앞에 세운다. 녹음기 소리는 유담 손에서 난다.
		Stand(AnnexFeet, AIGSeoRecorder::GetRestingLocation() + FVector(0.0f, -6.0f, 2.0f));
		if (bShots)
		{
			// 찍는 동안 위층 사람이 끼어들지 않게 재운다. 판재 밑은 손전등이 비춰야 보인다.
			for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
			{
				It->SetDormant(true);
			}
			if (AIGPlayerCharacter* Character = Player.Get())
			{
				if (UIGFlashlightComponent* Torch = Character->GetFlashlight())
				{
					Torch->SetAvailable(true);
					Torch->SetOn(true);
				}
			}
			Next(10);
			return;
		}
		Machine->StartPlaybackForTesting();
		Next(1);
		return;
	}

	case 10:
		if (PhaseSeconds < ShotSettleSeconds)
		{
			return;
		}
		// 스크린샷은 이 프레임 끝에 찍힌다. 같은 프레임에 녹음기를 집으면 사라진 판재 밑이 찍힌다.
		Shoot(TEXT("seorecorder-boards"));
		Next(12);
		return;

	case 12:
		if (PhaseSeconds < 0.3f)
		{
			return;
		}
		if (Machine)
		{
			Machine->StartPlaybackForTesting();
		}
		Next(1);
		return;

	case 1:
		if (!Machine)
		{
			Finish();
			return;
		}
		if (PhaseSeconds < 2.5f)
		{
			return;
		}
		// 트는 중에 새벽이 왔다. 멈추고, 목격은 없고, 녹음기는 제자리다.
		Check(Machine->IsPlaying(), TEXT("playback_runs"));
		Machine->SetHourActive(false);
		Check(!Machine->IsPlaying(), TEXT("dawn_stops_the_playback"));
		Check(!Narrative->HasWitness(EIGMissingFloorWitness::SeoVoiceRecorder), TEXT("half_heard_leaves_no_witness"));
		Check(!Machine->IsAvailable(), TEXT("not_taken_by_day"));
		Machine->SetHourActive(true);
		Check(Machine->IsAvailable(), TEXT("back_under_the_boards"));
		Machine->StartPlaybackForTesting();
		Next(2);
		return;

	case 2:
		if (!Machine)
		{
			Finish();
			return;
		}
		if (Machine->IsPlaying())
		{
			if (PhaseSeconds >= PlaybackTimeoutSeconds)
			{
				Check(false, TEXT("playback_finishes"));
				Finish();
			}
			return;
		}
		Next(3);
		return;

	case 3:
		// 다음 틱에 마지막 독백이 큐에 들어 있는지 본다.
		if (PhaseSeconds < 0.2f)
		{
			return;
		}
		Check(Narrative->HasWitness(EIGMissingFloorWitness::SeoVoiceRecorder), TEXT("heard_to_the_end"));
		Check(Machine && Machine->GetCueIndexForTesting() == 14, TEXT("every_cue_ran"));
		Check(SaidLine(Line(1)) && SaidLine(Line(2)) && SaidLine(Line(3)) && SaidLine(Line(4)) && SaidLine(Line(5)),
			TEXT("five_recorded_lines"));
		Check(SaidLine(Thought()), TEXT("yudam_asks_if_he_left_doha"));
		Check(Machine && !Machine->IsAvailable(), TEXT("recorder_taken"));

		// 골목 건너 남자가 비켜서다가 약봉투를 떨어뜨린다. 그 남자가 서 있던 자리에 생긴다.
		NightThree->OnDistantSeoDropped.Broadcast();
		Check(!Pills->IsHidden() && Pills->GetActorEnableCollision(), TEXT("pills_dropped_where_he_stood"));
		Check(FVector::Dist(Pills->GetActorLocation(), PillsLocation) < 5.0f, TEXT("pills_on_the_side_lane_asphalt"));
		if (bShots)
		{
			Stand(PillsViewFeet, PillsLocation);
			Next(11);
			return;
		}
		Pills->OnExamined.Broadcast(Pills);
		Check(Narrative->HasWitness(EIGMissingFloorWitness::SeoSleepingPills), TEXT("pills_witness"));
		Next(4);
		return;

	case 11:
		if (PhaseSeconds < ShotSettleSeconds)
		{
			return;
		}
		Shoot(TEXT("seorecorder-pills"));
		Next(13);
		return;

	case 13:
		if (PhaseSeconds < 0.3f)
		{
			return;
		}
		if (Pills)
		{
			Pills->OnExamined.Broadcast(Pills);
		}
		Check(Narrative->HasWitness(EIGMissingFloorWitness::SeoSleepingPills), TEXT("pills_witness"));
		Next(4);
		return;

	case 4:
	{
		if (PhaseSeconds < 0.2f)
		{
			return;
		}
		Check(SaidLine(PillsRecorderThought()) && !SaidLine(PillsAlleyThought()),
			TEXT("name_on_the_bag_recognized_from_the_recorder"));
		TArray<FText> Replies;
		NightFour->BuildConfrontationReplyLinesForTesting(Replies);
		Check(Contains(Replies, NSLOCTEXT("IGMissingFloor", "ConfrontationReplySeoRecorder", "403호 그 사람 내려가고 나서 아저씨는 뭐 했어요?")),
			TEXT("confrontation_asks_what_he_did_after"));
		Check(!Contains(Replies, NSLOCTEXT("IGMissingFloor", "ConfrontationReplyPills", "서일영 씨도 알아요? 그날 무슨 일이 있었는지.")),
			TEXT("confrontation_drops_the_pills_question"));
		const TArray<FText> News = Epilogue->BuildNewsLinesForTesting();
		Check(Contains(News, NSLOCTEXT("IGMissingFloor", "EpilogueNews4Recorder", "전 세입자 서모씨는 경찰에 출석해 유족이 제출한 녹음기가 자신의 것이라고 인정했다")),
			TEXT("news_reports_the_recorder"));
		Finish();
		return;
	}

	default:
		return;
	}
}
