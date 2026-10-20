#include "Sequence/IGSiteCheckProbe.h"

#include "Components/CapsuleComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGListenerGreyboxDirector.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGSiteCheckVisit.h"
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

namespace IGSiteCheckProbe
{
	/** 옥상 난간 북쪽. 옥상 철문과 물탱크 옆 통로가 한눈에 보인다. */
	const FVector RoofFeet(-120.0f, 365.0f, 1200.0f);
	const FVector RoofDoorLook(-277.5f, 220.0f, 1300.0f);
	/** 5층 안, 석고보드 더미 사이. 문간과 가운데 칸이 다 보인다. */
	const FVector AnnexFeet(-40.0f, 700.0f, 1200.0f);
	const FVector AnnexDoorLook(130.0f, 452.5f, 1290.0f);
	const FVector WallLook(240.0f, 650.0f, 1330.0f);
	/** 403호 방 안, 현관을 보는 자리. 4층 바닥 기준. */
	const FVector HomeFeet(0.0f, -60.0f, 0.0f);
	/** 4층 복도, 403호 문 앞. 4층 바닥 기준. */
	const FVector CorridorFeet(40.0f, -318.0f, 0.0f);
	constexpr float ClimbTimeoutSeconds = 90.0f;
	constexpr float AwayTimeoutSeconds = 200.0f;
	constexpr float GoneTimeoutSeconds = 12.0f;
}

AIGSiteCheckProbe::AIGSiteCheckProbe()
{
	// 검사기는 매 프레임 두 사람의 자리와 단계를 잰다. -IGSiteCheckProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGSiteCheckProbe::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGListenerEntity* InListener,
	AIGListenerGreyboxDirector* InDirector)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
	Director = InDirector;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGSiteCheckShots"));
	bAway = FParse::Param(FCommandLine::Get(), TEXT("IGSiteCheckAway"));
	bLeaveDuringTalk = FParse::Param(FCommandLine::Get(), TEXT("IGSiteCheckLeaveDuringTalk"));
}

void AIGSiteCheckProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("SITECHECK_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGSiteCheckProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("SITECHECK_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGSiteCheckProbe::Next(const int32 NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
	bActed = false;
	bShotTaken = false;
}

void AIGSiteCheckProbe::Stand(const FVector& Feet, const FVector& LookAt)
{
	AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		return;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(Feet + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr,
		ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	Look(LookAt);
}

void AIGSiteCheckProbe::Look(const FVector& LookAt)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !PC)
	{
		return;
	}
	const FVector Eye = Character->GetActorLocation() + Character->GetEyeOffsetFromActor();
	PC->SetControlRotation((LookAt - Eye).Rotation());
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
	}
}

void AIGSiteCheckProbe::Shoot(const TCHAR* Name)
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
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SiteCheckReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("SITECHECK_SHOT %s"), *Path);
}

bool AIGSiteCheckProbe::SaidLine(const FText& Line) const
{
	return SeenLines.Contains(Line.ToString());
}

void AIGSiteCheckProbe::NoteLines()
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	if (!Hud)
	{
		return;
	}
	const FText Lines[] = {
		NSLOCTEXT("IGMissingFloor", "FirstReportDispatch", "순찰 경찰관이 지금 그쪽으로 가고 있습니다. 옥상은 관리인분이 열어 주기로 했어요."),
		NSLOCTEXT("IGMissingFloor", "SiteCheckAsk", "계세요? 안에 누구 있어요?"),
		NSLOCTEXT("IGMissingFloor", "SiteCheckNoAnswer", "속이 비긴 했는데, 지금은 안에서 아무 반응이 없네요."),
		NSLOCTEXT("IGMissingFloor", "SiteCheckYudamNight", "밤에는 대답했어요. 제가 두드리면 똑같이요."),
		NSLOCTEXT("IGMissingFloor", "SiteCheckMokPipes", "창고라니까요. 밤이면 물탱크 배관이 울려요."),
		NSLOCTEXT("IGMissingFloor", "SiteCheckWarrant", "보내 주신 자료는 담당 팀에서 보고 있어요. 벽은 저희가 다시 와서 볼 테니까, 그때까지 손대지 마세요."),
		NSLOCTEXT("IGMissingFloor", "SiteCheckCallUs", "다시 소리 나면 바로 연락 주세요. 저희는 사진 몇 장만 더 찍고 내려갈게요."),
		NSLOCTEXT("IGMissingFloor", "SiteCheckMokThought", "아저씨는 벽 쪽은 쳐다보지도 않네."),
		NSLOCTEXT("IGMissingFloor", "FirstReportSiteCheck", "옥탑은 확인했습니다. 지금은 안에서 소리나 움직임이 없어요. 보내 주신 자료로 추가 확인 중입니다. 다시 소리가 나면 바로 연락 주시고, 벽에는 손대지 마세요.")};
	for (const FText& Line : Lines)
	{
		if (Hud->HasDialogueLineForTesting(Line.ToString()))
		{
			SeenLines.Add(Line.ToString());
		}
	}
}

void AIGSiteCheckProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 320.0f)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	NoteLines();
	using namespace IGSiteCheckProbe;
	using EStage = AIGSiteCheckVisit::EStage;
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	AIGPrologueWorldScene* Building = Scene.Get();
	AIGSwingDoor* HomeDoor = Building ? Building->GetHomeDoor() : nullptr;
	const FVector Floor4(0.0f, 0.0f, AIGPrologueWorldScene::FourthFloorZ);
	AIGSiteCheckVisit* People = Visit.Get();
	AIGSwingDoor* RoofDoor = StairGate.Get();
	AIGSwingDoor* FifthDoor = AnnexGate.Get();
	const FName SiteCheckBeat(TEXT("Night3.SiteCheck"));
	if (RoofDoor && RoofDoor->IsOpen())
	{
		bStairGateOpened = true;
	}
	if (FifthDoor && FifthDoor->IsOpen())
	{
		bAnnexGateOpened = true;
	}

	if (Phase >= 1 && Phase <= 5 && !People)
	{
		Check(false, TEXT("visit_still_exists"));
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
		for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
		{
			StairGate = It->GetStairGate();
			AnnexGate = It->GetAnnexGate();
		}
		Check(Narrative && Director.IsValid() && NightPhase.IsValid() && HomeDoor && Player.IsValid()
			&& StairGate.IsValid() && AnnexGate.IsValid(), TEXT("sitecheck_actors_resolved"));
		if (!Narrative || !Director.IsValid() || !NightPhase.IsValid() || !HomeDoor || !Player.IsValid()
			|| !StairGate.IsValid() || !AnnexGate.IsValid())
		{
			Finish();
			return;
		}
		if (Listener.IsValid())
		{
			Listener->SetDormant(true);
		}
		// 셋째 밤을 채우고 셋째 낮으로 넘어간다. 첫 신고 진실이 없으니 신고 문자 줄기는 돌지
		// 않는다. 현장 확인은 디렉터의 검사용 입구로 바로 시작한다.
		Narrative->SetNightIndex(3);
		if (NightPhase->IsHourActive())
		{
			NightPhase->CompleteNightGoal();
			NightPhase->ConcludeNight();
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
		// 두 철문은 닫혀 있다. 관리인이 열쇠로 열어야 한다.
		RoofDoor->ForceOpenState(false);
		FifthDoor->ForceOpenState(false);
		bStairGateOpened = false;
		bAnnexGateOpened = false;
		if (bAway)
		{
			HomeDoor->ForceOpenState(false);
			Stand(HomeFeet + Floor4, FVector(131.0f, -237.0f, 100.0f) + Floor4);
		}
		else
		{
			Stand(RoofFeet, RoofDoorLook);
		}
		Director->StartSiteCheckVisitForTesting();
		Visit = Director->GetSiteCheckVisitForTesting();
		People = Visit.Get();
		Check(People && People->GetStage() == EStage::Climbing, TEXT("visit_starts_at_the_roof_stair"));
		Check(Narrative->HasBeatPlayed(FName(TEXT("Night3.SiteCheckDispatched"))), TEXT("dispatch_beat_recorded"));
		if (!People)
		{
			Finish();
			return;
		}
		Next(1);
		break;
	}

	case 1:
		// 올라온다. 옥상 쪽은 관리인이 옥상 문을 나서는 모습을 보고, 5층 문을 열 때 안에 들어가 있는다.
		if (!bAway)
		{
			const FVector Manager = People->GetManagerFeet();
			if (!bShotTaken && Manager.Z > 1190.0f && Manager.Y > 235.0f && Manager.X < -60.0f)
			{
				bShotTaken = true;
				Shoot(TEXT("sitecheck-roof"));
			}
			if (!bActed && FifthDoor->IsSwinging())
			{
				bActed = true;
				Stand(AnnexFeet, AnnexDoorLook);
			}
			if (bActed && !bDoorShotTaken && FifthDoor->IsOpen() && !FifthDoor->IsSwinging()
				&& People->GetManagerFeet().Y > 470.0f)
			{
				bDoorShotTaken = true;
				Shoot(TEXT("sitecheck-door"));
			}
		}
		if (People->GetStage() == EStage::Climbing)
		{
			if (PhaseSeconds >= ClimbTimeoutSeconds)
			{
				Check(false, TEXT("visit_arrives"));
				Finish();
			}
			return;
		}
		Check(bStairGateOpened && bAnnexGateOpened, TEXT("manager_unlocks_both_gates"));
		Check(People->GetStepCount() >= 20, TEXT("footsteps_on_the_way_up"));
		Check(FVector::Dist(People->GetOfficerFeet(), AIGSiteCheckVisit::GetOfficerSpotFeet()) < 20.0f
			&& FVector::Dist(People->GetManagerFeet(), AIGSiteCheckVisit::GetManagerSpotFeet()) < 20.0f,
			TEXT("both_reach_their_spots"));
		Next(2);
		break;

	case 2:
		// 두드리고 귀를 댄다. 옆에 있으면 곧바로, 403호에 있으면 2분 뒤에.
		if (!bAway && People->GetStage() == EStage::Knocking)
		{
			if (!bShotTaken)
			{
				bShotTaken = true;
				Look(WallLook);
			}
		}
		if (People->GetStage() == EStage::Waiting || People->GetStage() == EStage::Knocking)
		{
			if (PhaseSeconds >= (bAway ? AwayTimeoutSeconds : 20.0f))
			{
				Check(false, TEXT("officer_checks_the_wall"));
				Finish();
			}
			if (bShots && !bAway && People->GetStage() == EStage::Knocking && People->GetKnockCount() == 2 && !bActed)
			{
				bActed = true;
				Shoot(TEXT("sitecheck-knock"));
			}
			return;
		}
		Check(People->GetKnockCount() == 3, TEXT("three_knocks_on_the_cavity_bay"));
		Check(Narrative->HasBeatPlayed(SiteCheckBeat), TEXT("site_check_beat_recorded"));
		if (bAway)
		{
			Check(!People->WasWitnessed(), TEXT("checked_without_her"));
			Check(SaidLine(NSLOCTEXT("IGMissingFloor", "FirstReportSiteCheck", "옥탑은 확인했습니다. 지금은 안에서 소리나 움직임이 없어요. 보내 주신 자료로 추가 확인 중입니다. 다시 소리가 나면 바로 연락 주시고, 벽에는 손대지 마세요.")),
				TEXT("result_arrives_as_a_text"));
			Next(5);
		}
		else
		{
			Check(People->WasWitnessed() && People->GetStage() == EStage::Talking, TEXT("officer_turns_to_talk"));
			Check(SaidLine(NSLOCTEXT("IGMissingFloor", "SiteCheckAsk", "계세요? 안에 누구 있어요?")), TEXT("officer_calls_into_the_wall"));
			Next(3);
		}
		break;

	case 3:
		if (bLeaveDuringTalk && People->GetSpokenLineCount() >= 1)
		{
			Check(AIGHorrorHUD::HasDialogueInContext(this, TEXT("SiteCheck.Visit")),
				TEXT("site_dialogue_is_present_before_leaving"));
			Stand(CorridorFeet + Floor4, FVector(400.0f, -318.0f, 1000.0f));
			Next(5);
			break;
		}
		// 말을 나눈다. 다섯 줄과 유담의 속말 하나.
		Look(People->GetOfficerFeet() + FVector(0.0f, 0.0f, 150.0f));
		if (!bShotTaken && PhaseSeconds >= 2.0f)
		{
			bShotTaken = true;
			Shoot(TEXT("sitecheck-talk"));
		}
		if (People->GetStage() == EStage::Talking)
		{
			if (PhaseSeconds >= 60.0f)
			{
				Check(false, TEXT("talk_ends"));
				Finish();
			}
			return;
		}
		Check(SaidLine(NSLOCTEXT("IGMissingFloor", "SiteCheckNoAnswer", "속이 비긴 했는데, 지금은 안에서 아무 반응이 없네요."))
			&& SaidLine(NSLOCTEXT("IGMissingFloor", "SiteCheckYudamNight", "밤에는 대답했어요. 제가 두드리면 똑같이요."))
			&& SaidLine(NSLOCTEXT("IGMissingFloor", "SiteCheckMokPipes", "창고라니까요. 밤이면 물탱크 배관이 울려요."))
			&& SaidLine(NSLOCTEXT("IGMissingFloor", "SiteCheckWarrant", "보내 주신 자료는 담당 팀에서 보고 있어요. 벽은 저희가 다시 와서 볼 테니까, 그때까지 손대지 마세요."))
			&& SaidLine(NSLOCTEXT("IGMissingFloor", "SiteCheckCallUs", "다시 소리 나면 바로 연락 주세요. 저희는 사진 몇 장만 더 찍고 내려갈게요.")),
			TEXT("five_lines_at_the_wall"));
		Check(!SaidLine(NSLOCTEXT("IGMissingFloor", "FirstReportSiteCheck", "옥탑은 확인했습니다. 지금은 안에서 소리나 움직임이 없어요. 보내 주신 자료로 추가 확인 중입니다. 다시 소리가 나면 바로 연락 주시고, 벽에는 손대지 마세요.")),
			TEXT("no_result_text_when_she_saw_it"));
		Next(4);
		break;

	case 4:
		// 속말이 지나가면 4층으로 내려간다. 두 사람은 사라지고 문은 닫힌다.
		if (PhaseSeconds < 4.5f)
		{
			if (PhaseSeconds >= 4.0f && PhaseSeconds - DeltaSeconds < 4.0f)
			{
				Check(SaidLine(NSLOCTEXT("IGMissingFloor", "SiteCheckMokThought", "아저씨는 벽 쪽은 쳐다보지도 않네.")),
					TEXT("yudam_notices_mok_never_looks_at_the_wall"));
			}
			return;
		}
		if (!bActed)
		{
			bActed = true;
			Stand(CorridorFeet + Floor4, FVector(400.0f, -318.0f, 1000.0f));
		}
		Next(5);
		break;

	case 5:
		if (People->GetStage() != EStage::Gone)
		{
			if (PhaseSeconds >= GoneTimeoutSeconds)
			{
				Check(false, TEXT("visit_withdraws_when_she_leaves"));
				Finish();
			}
			return;
		}
		if (PhaseSeconds < 1.0f)
		{
			return;
		}
		Check(People->IsHidden(), TEXT("visit_hidden"));
		Check(!RoofDoor->IsOpen() && !FifthDoor->IsOpen(), TEXT("gates_closed_again"));
		Check(!People->IsActorTickEnabled(), TEXT("visit_stops_ticking"));
		Check(!AIGHorrorHUD::HasDialogueInContext(this, TEXT("SiteCheck.Visit")),
			TEXT("site_dialogue_does_not_follow_her_downstairs"));
		Finish();
		break;

	default:
		break;
	}
}
