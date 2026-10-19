#include "Sequence/IGDayScenesProbe.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGListenerGreyboxDirector.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGNightPhaseDirector.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Kismet/GameplayStatics.h"
#include "Interaction/IGInteractable.h"
#include "Interaction/IGReadableNote.h"
#include "Interaction/IGSwingDoor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGInteractionComponent.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGPlayerController.h"
#include "Save/IGSaveGame.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGDayScenesProbe
{
	/** 403호 방 안, 현관을 보는 자리. 4층 바닥 기준. */
	const FVector HomeFeet(0.0f, -60.0f, 0.0f);
	/** 4층 복도에서 403호 문을 보는 자리와 쪽지 높이. */
	const FVector CorridorFeet(131.0f, -335.0f, 0.0f);
	const FVector DoorNotesFace(131.0f, -237.0f, 155.0f);
	/** 공동현관 앞에서 연석의 오토바이(동쪽을 보고 선다)를 마주 보는 자리. 말을 걸 만한 거리다. */
	const FVector CurbFeet(570.0f, -500.0f, 4.0f);
	const FVector CurbRiderFace(395.0f, -472.0f, 115.0f);
	// 편의점 계산대 앞 손님 자리에서 프린터 앞에 놓인 영수증을 본다(AIGStoreClerk::GetReceiptRestTransform).
	const FVector StoreFeet(2640.0f, -296.0f, 6.0f);
	const FVector StoreReceiptFace(2580.0f, -221.0f, 99.0f);
	constexpr float DawnTimeoutSeconds = 40.0f;
	/**
	 * 에필로그 「403호 문」을 찍는 카메라(4층 바닥 기준)와 가로 화각. 플레이어 카메라는 매 틱 화각을
	 * 되돌리고 세로 화각을 지키므로 따로 세운 카메라로 찍는다. 문 위 호수 표찰이 들지 않는 높이다.
	 */
	const FVector DoorStillEye(200.0f, -365.0f, 138.0f);
	const FVector DoorStillLook(118.0f, -237.0f, 102.0f);
	constexpr float DoorStillFov = 50.0f;
}

AIGDayScenesProbe::AIGDayScenesProbe()
{
	// 검사기는 매 프레임 낮과 밤의 경계와 쪽지·정우·폰을 잰다. -IGDayScenesProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	// 기록장은 게임을 멈춘다. 그 화면을 찍으려면 멈춘 동안에도 돌아야 한다.
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGDayScenesProbe::Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer, AIGListenerEntity* InListener)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
	bDoorStill = FParse::Param(FCommandLine::Get(), TEXT("IGEpilogueDoorStill"));
	bShots = bDoorStill || FParse::Param(FCommandLine::Get(), TEXT("IGDayScenesShots"));
}

void AIGDayScenesProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("DAYSCENES_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGDayScenesProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("DAYSCENES_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGDayScenesProbe::Next(const int32 NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
	bActed = false;
}

void AIGDayScenesProbe::Stand(const FVector& Feet, const FVector& LookAt)
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

void AIGDayScenesProbe::Shoot(const TCHAR* Name)
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
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DayScenesReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("DAYSCENES_SHOT %s"), *Path);
}

bool AIGDayScenesProbe::IsShown(const AIGReadableNote* Note) const
{
	return Note && !Note->IsHidden() && Note->IsInteractionEnabled();
}

bool AIGDayScenesProbe::SaidLine(const FText& Line) const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	return Hud && Hud->HasDialogueLineForTesting(Line.ToString());
}

bool AIGDayScenesProbe::IsDay() const
{
	const AIGNightPhaseDirector* HourDirector = NightPhase.Get();
	return HourDirector && !HourDirector->IsHourActive() && !HourDirector->IsDawnTransitionInProgress();
}

void AIGDayScenesProbe::Tick(const float DeltaSeconds)
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
	using namespace IGDayScenesProbe;
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	AIGPrologueWorldScene* Building = Scene.Get();
	AIGSwingDoor* HomeDoor = Building ? Building->GetHomeDoor() : nullptr;
	AIGNightPhaseDirector* Hour = NightPhase.Get();
	const FVector Floor4(0.0f, 0.0f, AIGPrologueWorldScene::FourthFloorZ);

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
		for (TActorIterator<AIGMissingFloorEvidence> It(GetWorld()); It; ++It)
		{
			if (It->GetName() == TEXT("MissingFloorJeongwooTalk"))
			{
				JeongwooTalk = *It;
			}
			else if (It->GetName() == TEXT("MissingFloorUnit401Door"))
			{
				Unit401Door = *It;
			}

		}
		for (TActorIterator<AIGReadableNote> It(GetWorld()); It; ++It)
		{
			const FString Name = It->GetName();
			if (Name == TEXT("MissingFloorUsedListingNote"))
			{
				Phone = *It;
			}

			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (Name == FString::Printf(TEXT("MissingFloorDoor403Note%d"), Index + 1))
				{
					Notes[Index] = *It;
				}
			}
		}
		Hour = NightPhase.Get();
		Check(Narrative && Hour && HomeDoor && JeongwooTalk.IsValid() && Unit401Door.IsValid() && Phone.IsValid()
			&& Notes[0].IsValid() && Notes[1].IsValid() && Notes[2].IsValid(), TEXT("day_scene_actors_resolved"));
		if (!Narrative || !Hour || !HomeDoor || !JeongwooTalk.IsValid() || !Unit401Door.IsValid() || !Phone.IsValid()
			|| !Notes[0].IsValid() || !Notes[1].IsValid() || !Notes[2].IsValid() || !Player.IsValid())
		{
			Finish();
			return;
		}
		bool bOnDoor = true;
		for (const TWeakObjectPtr<AIGReadableNote>& Note : Notes)
		{
			bOnDoor &= Note->GetRootComponent()->GetAttachParent() == HomeDoor->GetDoorPivot();
		}
		Check(bOnDoor, TEXT("notes_hang_on_home_door"));
		if (Listener.IsValid())
		{
			Listener->SetDormant(true);
		}
		if (bDoorStill)
		{
			// 그림만 찍는다. 둘째 밤을 바로 넘겨 낮의 복도에서 찍는다.
			Narrative->SetNightIndex(2);
			HomeDoor->ForceOpenState(false);
			Stand(HomeFeet + Floor4, FVector(131.0f, -237.0f, 100.0f) + Floor4);
			if (!Hour->IsHourActive())
			{
				Hour->BeginTheHour(2);
			}
			Next(1);
			break;
		}
		if (UIGFlashlightComponent* Torch = Player->GetFlashlight())
		{
			Torch->SetAvailable(true);
			Torch->SetOn(true);
		}
		// 입주 저녁. 첫 쪽지만 붙어 있고, 황순금은 인사 뒤에 어둠 이야기를 한다.
		Narrative->SetNightIndex(0);
		Unit401Door->OnExamined.Broadcast(Unit401Door.Get());
		Check(IsShown(Notes[0].Get()) && !IsShown(Notes[1].Get()) && !IsShown(Notes[2].Get()),
			TEXT("only_first_note_on_arrival"));
		Check(SaidLine(NSLOCTEXT("IGMissingFloor", "Hwang401DarkWarning", "새벽에 깜깜한 데서 뭐가 보이거든 쳐다보지 말아요.")),
			TEXT("hwang_warns_about_the_dark_on_arrival"));
		// 첫째 밤. 이사 온 날 골목에서 정우와 스쳤다고 치고, 403호 안에서 밤을 채운다.
		Narrative->SetNightIndex(1);
		Narrative->MarkBeatPlayed(FName(TEXT("Neighborhood.ScooterNearMiss")));
		HomeDoor->ForceOpenState(false);
		Stand(HomeFeet + Floor4, FVector(131.0f, -237.0f, 100.0f) + Floor4);
		if (!Hour->IsHourActive())
		{
			Hour->BeginTheHour(1);
		}
		Next(1);
		break;
	}

	case 1:
		if (!bActed && PhaseSeconds >= 1.0f)
		{
			bActed = true;
			if (!bDoorStill)
			{
				Check(Hour->IsHourActive() && !IsShown(Notes[1].Get()), TEXT("night1_no_second_note_yet"));
			}
			Hour->CompleteNightGoal();
			Hour->ConcludeNight();
		}
		else if (bActed && IsDay())
		{
			Next(bDoorStill ? 9 : 2);
		}
		else if (PhaseSeconds >= DawnTimeoutSeconds)
		{
			Check(false, TEXT("dawn_after_night1"));
			Finish();
		}
		break;

	case 2:
		// 첫째 낮. 쪽지가 하나 더 붙고 폰에는 동네 이야기 두 장이 생긴다. 정우가 앉으면 말을 건다.
		if (PhaseSeconds < 2.0f)
		{
			return;
		}
		if (!bActed)
		{
			bActed = true;
			Check(IsShown(Notes[0].Get()) && IsShown(Notes[1].Get()) && !IsShown(Notes[2].Get())
				&& Narrative->HasBeatPlayed(FName(TEXT("Day.Note303.Second"))), TEXT("second_note_posted_on_day1"));
			Check(Phone->GetExtraPhonePages().Num() == 2, TEXT("phone_board_two_posts_on_day1"));
		}
		if (JeongwooTalk->IsInteractionEnabled())
		{
			Check(true, TEXT("jeongwoo_waits_on_scooter_day1"));
			// 말은 오토바이 옆에서 건다. 들리는 거리 밖에서 건 말은 줄을 서지 않는다.
			Stand(CurbFeet, CurbRiderFace);
			// 아래는 대화를 바로 부르지만, 플레이어는 상호작용 시선으로 오토바이 위의 상자를 잡아야 말을 건다.
			UIGInteractionComponent* Interaction = Player.IsValid()
				? Player->FindComponentByClass<UIGInteractionComponent>() : nullptr;
			if (Interaction)
			{
				Interaction->RefreshFocus();
			}
			Check(Interaction && Interaction->GetFocusedActor() == JeongwooTalk.Get(),
				TEXT("jeongwoo_talk_focused_at_scooter"));
			JeongwooTalk->OnExamined.Broadcast(JeongwooTalk.Get());
			Check(Narrative->HasBeatPlayed(FName(TEXT("Day.Jeongwoo.Intro")))
				&& SaidLine(NSLOCTEXT("IGMissingFloor", "JeongwooIntro",
					"저 202호 살아요, 정우예요. 새벽에 오토바이 소리 나면 저예요. 죄송해요.")),
				TEXT("jeongwoo_introduces_himself_day1"));
			if (!bShots)
			{
				// 쪽지 붙이는 소리는 403호 안에서만 들린다. 렌더 실행은 오토바이 앞을 찍고 나서 돌아간다.
				Stand(HomeFeet + Floor4, FVector(131.0f, -237.0f, 100.0f) + Floor4);
			}
			Next(3);
		}
		else if (PhaseSeconds >= 12.0f)
		{
			Check(false, TEXT("jeongwoo_waits_on_scooter_day1"));
			Next(3);
		}
		break;

	case 3:
		if (bShots && !bActed)
		{
			bActed = true;
			Stand(CurbFeet, CurbRiderFace);
		}
		if (bShots && PhaseSeconds >= 1.5f && PhaseSeconds - DeltaSeconds < 1.5f)
		{
			Shoot(TEXT("dayscenes-jeongwoo-curb"));
		}
		// 쪽지 붙이는 소리(9초 뒤)를 들을 때까지 403호 안에 둔다.
		if (bShots && PhaseSeconds >= 1.7f && PhaseSeconds - DeltaSeconds < 1.7f)
		{
			Stand(HomeFeet + Floor4, FVector(131.0f, -237.0f, 100.0f) + Floor4);
		}
		if (PhaseSeconds >= 10.0f)
		{
			Hour->BeginTheHour(2);
			Next(4);
		}
		break;

	case 4:
		if (!bActed && PhaseSeconds >= 2.5f)
		{
			bActed = true;
			Check(Hour->IsHourActive() && !JeongwooTalk->IsInteractionEnabled() && !IsShown(Notes[2].Get()),
				TEXT("night2_jeongwoo_gone_no_third_note"));
			Hour->CompleteNightGoal();
			Hour->ConcludeNight();
		}
		else if (bActed && IsDay())
		{
			Next(5);
		}
		else if (PhaseSeconds >= DawnTimeoutSeconds)
		{
			Check(false, TEXT("dawn_after_night2"));
			Finish();
		}
		break;

	case 5:
		// 둘째 낮. 셋째 쪽지, 동네 이야기 세 장, 정우의 엄마 목소리, 황순금의 손 있는 날.
		if (PhaseSeconds < 2.0f)
		{
			return;
		}
		if (!bActed)
		{
			bActed = true;
			Check(IsShown(Notes[0].Get()) && IsShown(Notes[1].Get()) && IsShown(Notes[2].Get())
				&& Narrative->HasBeatPlayed(FName(TEXT("Day.Note303.Third"))), TEXT("third_note_posted_on_day2"));
			Check(Phone->GetExtraPhonePages().Num() == 3
				&& Phone->GetExtraPhonePages()[0].Title.ToString().Equals(
					NSLOCTEXT("IGMissingFloor", "PostGuestTitle", "새벽에 엄마 목소리로 문 두드리면 열지 마세요").ToString()),
				TEXT("phone_board_jeongwoo_post_on_top_day2"));
			// 화면 없는 실행에서는 대사 큐가 줄지 않아 꽉 찬 큐의 뒷줄이 밀려난다. 줄은 렌더 실행에서
			// 보고 여기서는 비트만 본다.
			Unit401Door->OnExamined.Broadcast(Unit401Door.Get());
			Check(Narrative->HasBeatPlayed(FName(TEXT("Day.Hwang.SonDay"))), TEXT("hwang_son_day_on_day2"));
		}
		if (JeongwooTalk->IsInteractionEnabled())
		{
			Stand(CurbFeet, CurbRiderFace);
			JeongwooTalk->OnExamined.Broadcast(JeongwooTalk.Get());
			Check(Narrative->HasBeatPlayed(FName(TEXT("Day.Jeongwoo.Guest")))
				&& SaidLine(NSLOCTEXT("IGMissingFloor", "JeongwooGuest3", "동네장터에도 올렸어요. 403호도 걸쇠 꼭 거세요.")),
				TEXT("jeongwoo_tells_mother_voice_day2"));
			if (!bShots)
			{
				Stand(HomeFeet + Floor4, FVector(131.0f, -237.0f, 100.0f) + Floor4);
			}
			Next(6);
		}
		else if (PhaseSeconds >= 12.0f)
		{
			Check(false, TEXT("jeongwoo_tells_mother_voice_day2"));
			Next(6);
		}
		break;

	case 6:
		// 쪽지 붙이는 소리(새벽 9초 뒤)를 403호 안에서 듣고 나서, 문을 열면 쪽지도 같이 도는지 본다.
		if (!bActed)
		{
			bActed = true;
			if (bShots)
			{
				Stand(CorridorFeet + Floor4, DoorNotesFace + Floor4);
			}
		}
		if (bShots && PhaseSeconds >= 1.5f && PhaseSeconds - DeltaSeconds < 1.5f)
		{
			Shoot(TEXT("dayscenes-door403-notes"));
		}
		if (PhaseSeconds >= 9.5f && PhaseSeconds - DeltaSeconds < 9.5f)
		{
			NoteClosedAt = Notes[0]->GetActorLocation();
			HomeDoor->ForceOpenState(true);
		}
		if (PhaseSeconds >= 10.0f)
		{
			Check(FVector::Dist(Notes[0]->GetActorLocation(), NoteClosedAt) > 30.0f, TEXT("notes_swing_with_door"));
			HomeDoor->ForceOpenState(false);
			Next(bShots ? 7 : 8);
		}
		break;

	case 7:
		// 폰의 동네 이야기 세 장(정우의 글, 계단 센서등, 작년 403호 글). 번역 길이도 여기서 본다.
		if (!bActed && PhaseSeconds >= 0.5f)
		{
			bActed = true;
			FIGInteractionContext Context;
			Context.Interactor = Player.Get();
			Context.TargetActor = Phone.Get();
			Context.HoldProgress = 1.0f;
			IIGInteractable::Execute_CompleteInteraction(Phone.Get(), Context);
		}
		for (int32 Page = 0; Page < 3; ++Page)
		{
			// 장을 넘긴 다음 프레임 뒤에 찍는다. 스크린샷은 요청한 프레임 끝의 화면이다.
			const float TurnAt = 1.0f + Page * 1.2f;
			if (bActed && PhaseSeconds >= TurnAt && PhaseSeconds - DeltaSeconds < TurnAt)
			{
				const APlayerController* PC = GetWorld()->GetFirstPlayerController();
				if (AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr)
				{
					Hud->MoveNotePage(1);
				}
			}
			const float ShootAt = TurnAt + 0.8f;
			if (PhaseSeconds >= ShootAt && PhaseSeconds - DeltaSeconds < ShootAt)
			{
				Shoot(Page == 0 ? TEXT("dayscenes-phone-board") : Page == 1 ? TEXT("dayscenes-phone-board-2")
					: TEXT("dayscenes-phone-board-3"));
			}
		}
		if (PhaseSeconds >= 4.8f)
		{
			Phone->Close();
			Next(8);
		}
		break;

	case 8:
		// 303호 셋째 쪽지 읽기 화면.
		if (!bShots)
		{
			Next(10);
			break;
		}
		if (!bActed && PhaseSeconds >= 0.5f)
		{
			bActed = true;
			FIGInteractionContext Context;
			Context.Interactor = Player.Get();
			Context.TargetActor = Notes[2].Get();
			Context.HoldProgress = 1.0f;
			IIGInteractable::Execute_CompleteInteraction(Notes[2].Get(), Context);
		}
		if (PhaseSeconds >= 1.5f && PhaseSeconds - DeltaSeconds < 1.5f)
		{
			Shoot(TEXT("dayscenes-note303-third"));
		}
		if (PhaseSeconds >= 2.0f)
		{
			Notes[2]->Close();
			Next(10);
		}
		break;

	case 10:
	{
		// 둘째 낮, 편의점. 택배 얘기를 들은 뒤 누가 찾아갔는지 물으면 나린이 포스에서 수령 기록을 찾아
		// 영수증을 뽑아 준다. 화면 없는 실행에서는 대사 큐가 줄지 않으니 줄은 렌더 실행에서만 본다.
		// 호출벨 액터는 본편 실행에만 있어서, 벨에 이어지는 말을 감독에게 바로 시킨다.
		if (!Greybox.IsValid())
		{
			for (TActorIterator<AIGListenerGreyboxDirector> It(GetWorld()); It; ++It)
			{
				Greybox = *It;
				Receipt = It->GetStoreParcelReceiptForTesting();
			}
		}
		AIGListenerGreyboxDirector* Counter = Greybox.Get();
		AIGReadableNote* Slip = Receipt.Get();
		if (!bActed)
		{
			bActed = true;
			Check(Counter && Slip, TEXT("store_receipt_actors_resolved"));
			if (!Counter || !Slip)
			{
				Finish();
				break;
			}
			Check(!IsShown(Slip), TEXT("no_receipt_before_asking"));
			// 벨은 계산대 앞에서 누른다. 나린의 말과 영수증 장면은 그 자리에 있어야 이어진다.
			Stand(StoreFeet, StoreReceiptFace);
			// 뒤편 택배 안내문은 읽고 왔다고 친다. 첫 벨에는 택배 얘기를 한다.
			Narrative->MarkBeatPlayed(FName(TEXT("Neighborhood.Delivery")));
			Counter->TalkAtStoreCounterForTesting();
			Check(Narrative->HasBeatPlayed(FName(TEXT("Neighborhood.DeliveryDiscussed")))
				&& !Narrative->HasBeatPlayed(FName(TEXT("Neighborhood.ParcelLedger"))) && !IsShown(Slip),
				TEXT("delivery_talk_comes_first"));
			break;
		}
		if (!Counter || !Slip)
		{
			break;
		}
		if (PhaseSeconds >= 7.5f && SecondBellAt < 0.0f)
		{
			// 벨 사이 7초가 지나야 다음 말을 한다. 그래도 나린이 앞의 말을 하는 중이면 묻지 않으니(렌더 실행),
			// 그 말이 끝나면 다시 누른다. 화면이 없는 실행은 줄이 줄지 않으니 바로 묻는다.
			const bool bNarinTalking = AIGHorrorHUD::IsPresentingDialogue(this)
				&& AIGHorrorHUD::HasDialogueInContext(this, FName(TEXT("Store.CounterTalk")));
			const bool bAsked = Counter->TalkAtStoreCounterForTesting();
			if (bNarinTalking)
			{
				if (!bBellRefusalChecked)
				{
					bBellRefusalChecked = true;
					Check(!bAsked, TEXT("bell_waits_while_narin_talks"));
				}
				if (!bAsked && PhaseSeconds < 40.0f)
				{
					break;
				}
			}
			SecondBellAt = PhaseSeconds;
			Check(bAsked, TEXT("second_bell_after_seven_seconds"));
			Check(Narrative->HasBeatPlayed(FName(TEXT("Neighborhood.ParcelLedger"))) && !IsShown(Slip),
				TEXT("receipt_question_then_search"));
			if (bShots)
			{
				Check(SaidLine(NSLOCTEXT("IGMissingFloor", "NarinReceiptAnswer", "포스에 남아 있을 거예요. 잠깐만요, 찾아볼게요.")),
					TEXT("narin_looks_up_the_pos"));
			}
		}
		if (SecondBellAt < 0.0f)
		{
			break;
		}
		// 단말기는 「찾아볼게요」가 지나간 뒤, 찾은 날짜를 말하고 나서 영수증이 나온다. 나올 때까지
		// 기다리고, 그 뒤 순서는 나온 때부터 센다.
		if (PrintedAt < 0.0f)
		{
			if (IsShown(Slip))
			{
				PrintedAt = PhaseSeconds;
				Check(PhaseSeconds > SecondBellAt + 4.8f + 2.6f, TEXT("receipt_waits_for_the_talk"));
				if (bShots)
				{
					// 영수증은 「뽑아 드릴게요」까지 다 들은 뒤에 나온다. 그 말이 들은 말 기록에 이미 있어야 한다.
					const FText Found = NSLOCTEXT("IGMissingFloor", "NarinReceiptFound", "작년 8월 2일이네요. 건물주 아저씨가 대신 받아 가셨어요. 한 장 뽑아 드릴게요.");
					Check(Narrative->GetHeardLines().ContainsByPredicate([&Found](const FIGHeardLine& Entry) { return Entry.Line.EqualTo(Found); }),
						TEXT("narin_reads_the_date"));
				}
			}
			else if (PhaseSeconds > SecondBellAt + 45.0f)
			{
				Check(false, TEXT("receipt_printed_on_counter"));
				Finish();
			}
			break;
		}
		if (PrintedAt < 0.0f)
		{
			break;
		}
		const float SincePrint = PhaseSeconds - PrintedAt;
		const auto After = [SincePrint, DeltaSeconds](const float At)
		{
			return SincePrint >= At && SincePrint - DeltaSeconds < At;
		};
		if (After(0.0f))
		{
			Check(true, TEXT("receipt_printed_on_counter"));
		}
		if (After(0.9f))
		{
			Shoot(TEXT("dayscenes-store-receipt-counter"));
		}
		if (After(1.5f))
		{
			FIGInteractionContext Context;
			Context.Interactor = Player.Get();
			Context.TargetActor = Slip;
			Context.HoldProgress = 1.0f;
			IIGInteractable::Execute_CompleteInteraction(Slip, Context);
			Check(Slip->IsOpen() && Narrative->HasBeatPlayed(FName(TEXT("Neighborhood.ParcelReceipt"))),
				TEXT("reading_receipt_keeps_it"));
		}
		if (After(2.5f))
		{
			Shoot(TEXT("dayscenes-store-receipt"));
		}
		if (After(3.1f))
		{
			Slip->Close();
			Check(!IsShown(Slip), TEXT("receipt_leaves_counter_after_reading"));
			if (bShots)
			{
				Check(SaidLine(NSLOCTEXT("IGMissingFloor", "StoreReceiptThought", "오빠가 부탁했다고?")),
					TEXT("yudam_doubts_the_request"));
			}
		}
		if (After(11.0f))
		{
			// 셋째 낮부터 나린은 그 영수증 얘기를 하며 본 대로 말해 주겠다고 한다.
			Narrative->SetNightIndex(3);
			Counter->TalkAtStoreCounterForTesting();
			Check(!IsShown(Slip), TEXT("receipt_stays_taken"));
			if (bShots)
			{
				Check(SaidLine(NSLOCTEXT("IGMissingFloor", "NarinLineLastDayReceipt",
					"그 영수증 잘 챙겨 둬요. 필요하면 저도 본 대로 말할게요. 무슨 일 있으면 바로 이쪽으로 오세요.")),
					TEXT("narin_offers_to_say_what_she_saw"));
			}
		}
		if (SincePrint >= 12.0f)
		{
			Next(bShots ? 15 : 20);
		}
		break;
	}

	case 15:
	{
		// 기록장의 「들은 말」. 나린과 나눈 말이 가장 최근 쪽에 남았는지 화면으로 본다.
		AIGPlayerController* Controller = Cast<AIGPlayerController>(GetWorld()->GetFirstPlayerController());
		AIGHorrorHUD* Hud = Controller ? Cast<AIGHorrorHUD>(Controller->GetHUD()) : nullptr;
		if (!Controller || !Hud)
		{
			Check(false, TEXT("heard_lines_reach_the_journal"));
			Next(20);
			break;
		}
		++JournalFrames;
		if (JournalFrames == 1)
		{
			Check(Narrative->GetHeardLines().Num() > 0
				&& Hud->GetMissingFloorJournalPageCount() > Hud->GetMissingFloorJournalTruthPageCount(),
				TEXT("heard_lines_reach_the_journal"));
			Controller->OpenMissingFloorJournalForTesting();
		}
		else if (JournalFrames == 4)
		{
			// 첫 쪽에서 ←를 누르면 가장 최근에 들은 말이 있는 쪽이다.
			Controller->MoveMissingFloorJournalPageForTesting(-1);
		}
		else if (JournalFrames == 30)
		{
			Shoot(TEXT("dayscenes-journal-heard"));
		}
		else if (JournalFrames >= 40)
		{
			Controller->CloseMissingFloorJournalForTesting();
			Next(20);
		}
		break;
	}

	case 20:
	{
		// 대사 규칙. 화면 없이 큐를 직접 넘기며 본다(그리는 프레임이 하는 일과 같다).
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
		if (!Hud)
		{
			Check(false, TEXT("dialogue_rules_hud"));
			Finish();
			break;
		}
		const double T0 = GetWorld()->GetTimeSeconds();
		const FText Speaker = FText::FromString(TEXT("검사"));
		const auto Scoped = [](const FName Context, const EIGDialogueLifetime Lifetime)
		{
			FIGDialogueScope Scope;
			Scope.Context = Context;
			Scope.Lifetime = Lifetime;
			return Scope;
		};
		const FText LineA = FText::FromString(TEXT("규칙 검사 반응 하나."));
		const FText LineB = FText::FromString(TEXT("규칙 검사 이야기 둘."));

		// 0. 새 설치에서는 소리 설명이 꺼져 있다. 보통 화면에는 대사만 오르고 소리를 글로 풀지 않는다.
		//    아래 소리 자막 규칙은 설정에서 「중요한 소리」를 고른 사람의 화면으로 본다.
		UGameInstance* GameInstance = GetGameInstance();
		UIGAccessibilitySubsystem* Accessibility = GameInstance
			? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>() : nullptr;
		Hud->ClearDialogueForTesting();
		AIGHorrorHUD::PushAudioCaption(this, LineA, 2.0f);
		Check(Accessibility && !Accessibility->AreSoundCaptionsEnabled() && !Hud->HasPendingAudioCaption(),
			TEXT("sound_captions_off_by_default"));
		const auto ChooseSoundCaptions = [Accessibility](const bool bOn, const bool bAllSounds)
		{
			if (Accessibility)
			{
				FIGAccessibilitySettings Settings = Accessibility->GetSettings();
				Settings.bSoundCaptionsEnabled = bOn;
				Settings.bBackgroundSoundCaptions = bAllSounds;
				Accessibility->ApplySettings(Settings);
			}
		};
		ChooseSoundCaptions(true, false);

		// 1. 사건을 거두면 떠 있는 줄과 밀린 줄이 함께 사라진다.
		Hud->ClearDialogueForTesting();
		AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineA, EIGDialogueChannel::Conversation, 0.0f,
			EIGDialoguePriority::Story, Scoped(TEXT("Rule.X"), EIGDialogueLifetime::Reaction));
		AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineA, EIGDialogueChannel::InnerVoice, 0.0f,
			EIGDialoguePriority::Story, Scoped(TEXT("Rule.X"), EIGDialogueLifetime::Reaction));
		AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineB, EIGDialogueChannel::Conversation, 0.0f,
			EIGDialoguePriority::Story, Scoped(TEXT("Rule.Y"), EIGDialogueLifetime::Story));
		Hud->AdvanceDialogueForTesting(T0);
		AIGHorrorHUD::CancelDialogue(this, TEXT("Rule.X"));
		Hud->AdvanceDialogueForTesting(T0 + 0.05);
		Check(!Hud->HasDialogueInContextForTesting(TEXT("Rule.X"))
			&& Hud->GetActiveDialogueLineForTesting().ToString() == LineB.ToString(), TEXT("cancel_removes_current_and_queued"));

		// 2. 짧은 반응은 메뉴에 한참 가려졌다 돌아오면 다시 보이지 않는다. 이야기 대사는 남은 시간부터 잇는다.
		Hud->ClearDialogueForTesting();
		AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineA, EIGDialogueChannel::Conversation, 0.0f,
			EIGDialoguePriority::Story, Scoped(TEXT("Rule.R"), EIGDialogueLifetime::Reaction));
		Hud->AdvanceDialogueForTesting(T0);
		Hud->SuspendDialogueForTesting(T0 + 1.0);
		Hud->ResumeDialogueForTesting(T0 + 4.0);
		Check(Hud->GetQueuedDialogueCountForTesting() == 0, TEXT("stale_reaction_dropped_after_menu"));
		Hud->ClearDialogueForTesting();
		AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineB, EIGDialogueChannel::Conversation, 0.0f,
			EIGDialoguePriority::Story, Scoped(TEXT("Rule.S"), EIGDialogueLifetime::Story));
		Hud->AdvanceDialogueForTesting(T0);
		const double EndBefore = Hud->GetDialogueEndTimeForTesting();
		Hud->SuspendDialogueForTesting(T0 + 1.0);
		Hud->ResumeDialogueForTesting(T0 + 4.0);
		Check(Hud->GetActiveDialogueLineForTesting().ToString() == LineB.ToString()
			&& FMath::IsNearlyEqual(Hud->GetDialogueEndTimeForTesting(), EndBefore + 3.0, 0.01),
			TEXT("story_line_resumes_after_menu"));

		// 3. 같은 말을 거듭 내도 처음 뜬 때부터 한 번 읽을 시간까지만 둔다.
		Hud->ClearDialogueForTesting();
		AIGHorrorHUD::PushDialogue(this, Speaker, LineB);
		Hud->AdvanceDialogueForTesting(T0);
		const double FirstEnd = Hud->GetDialogueEndTimeForTesting();
		for (int32 Repeat = 0; Repeat < 6; ++Repeat)
		{
			AIGHorrorHUD::PushDialogue(this, Speaker, LineB);
		}
		Check(FMath::IsNearlyEqual(Hud->GetDialogueEndTimeForTesting(), FirstEnd, 0.01), TEXT("repeat_does_not_extend"));

		// 4. 지금 행동에 대한 반응은 오래 밀리면(이미 지난 상황) 뜨지 않고 버려진다.
		Hud->ClearDialogueForTesting();
		AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineB, EIGDialogueChannel::Conversation, 0.0f,
			EIGDialoguePriority::Story, Scoped(TEXT("Rule.Story"), EIGDialogueLifetime::Story));
		AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineA, EIGDialogueChannel::Conversation, 0.0f,
			EIGDialoguePriority::Story, Scoped(TEXT("Rule.Late"), EIGDialogueLifetime::Reaction));
		Hud->AdvanceDialogueForTesting(T0);
		Hud->AdvanceDialogueForTesting(T0 + 12.0);
		Check(Hud->GetQueuedDialogueCountForTesting() == 0, TEXT("late_reaction_expires"));

		// 5. 낸 쪽이 사라지면 그 줄도 사라지고, 끝 알림은 한 번만 온다.
		Hud->ClearDialogueForTesting();
		int32 Ended = 0;
		EIGDialogueEndReason LastReason = EIGDialogueEndReason::Completed;
		AActor* LineOwner = GetWorld()->SpawnActor<AActor>();
		{
			FIGDialogueScope Scope = Scoped(TEXT("Rule.Owner"), EIGDialogueLifetime::Story);
			Scope.Owner = LineOwner;
			Scope.OnEnded = [&Ended, &LastReason](const EIGDialogueEndReason Reason) { ++Ended; LastReason = Reason; };
			AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineB, EIGDialogueChannel::Conversation, 0.0f,
				EIGDialoguePriority::Story, MoveTemp(Scope));
		}
		Hud->AdvanceDialogueForTesting(T0);
		if (LineOwner)
		{
			LineOwner->Destroy();
		}
		Hud->AdvanceDialogueForTesting(T0 + 0.1);
		Hud->AdvanceDialogueForTesting(T0 + 0.2);
		Check(Hud->GetQueuedDialogueCountForTesting() == 0 && Ended == 1 && LastReason == EIGDialogueEndReason::Cancelled,
			TEXT("owner_gone_cancels_once"));

		// 6. 다 읽으면 Completed로 한 번 끝난다.
		Hud->ClearDialogueForTesting();
		Ended = 0;
		{
			FIGDialogueScope Scope = Scoped(TEXT("Rule.Done"), EIGDialogueLifetime::Story);
			Scope.OnEnded = [&Ended, &LastReason](const EIGDialogueEndReason Reason) { ++Ended; LastReason = Reason; };
			AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineB, EIGDialogueChannel::Conversation, 0.0f,
				EIGDialoguePriority::Story, MoveTemp(Scope));
		}
		Hud->AdvanceDialogueForTesting(T0);
		Hud->AdvanceDialogueForTesting(T0 + 30.0);
		Hud->AdvanceDialogueForTesting(T0 + 31.0);
		Check(Ended == 1 && LastReason == EIGDialogueEndReason::Completed, TEXT("completed_ends_once"));

		// 7. 이미 사라진 쪽이 늦게 낸 말(멈춘 타이머·늦은 로딩)은 큐에 들어가지 않는다.
		Hud->ClearDialogueForTesting();
		{
			FIGDialogueScope Scope = Scoped(TEXT("Rule.Late"), EIGDialogueLifetime::Story);
			Scope.StillValid = []() { return false; };
			AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineB, EIGDialogueChannel::Conversation, 0.0f,
				EIGDialoguePriority::Story, MoveTemp(Scope));
		}
		Check(Hud->GetQueuedDialogueCountForTesting() == 0, TEXT("ended_scene_push_rejected"));

		// 8. 장면이 바뀌면(밤 시작·새벽·붙잡힘) 떠 있는 줄과 밀린 줄, 소리 자막이 모두 사라진다.
		AIGHorrorHUD::PushDialogue(this, Speaker, LineA);
		AIGHorrorHUD::PushDialogue(this, Speaker, LineB);
		AIGHorrorHUD::PushAudioCaption(this, LineA, 2.0f);
		AIGHorrorHUD::ResetDialogueForSceneChange(this);
		Check(Hud->GetQueuedDialogueCountForTesting() == 0 && !Hud->HasPendingAudioCaption(), TEXT("scene_change_clears_lines"));

		// 9. 끝까지 보인 이야기 대사만 「들은 말」에 남는다. 짧은 반응과 거둔 말은 남지 않는다.
		{
			const FText Heard = FText::FromString(TEXT("규칙 검사 끝까지 들은 말."));
			const FText Brief = FText::FromString(TEXT("규칙 검사 짧은 반응."));
			const FText Dropped = FText::FromString(TEXT("규칙 검사 거둔 말."));
			Hud->ClearDialogueForTesting();
			AIGHorrorHUD::PushDialogueScoped(this, Speaker, Heard, EIGDialogueChannel::Conversation, 0.0f,
				EIGDialoguePriority::Story, Scoped(TEXT("Rule.Heard"), EIGDialogueLifetime::Story));
			AIGHorrorHUD::PushDialogueScoped(this, Speaker, Brief, EIGDialogueChannel::Conversation, 0.0f,
				EIGDialoguePriority::Story, Scoped(TEXT("Rule.Brief"), EIGDialogueLifetime::Reaction));
			AIGHorrorHUD::PushDialogueScoped(this, Speaker, Dropped, EIGDialogueChannel::Conversation, 0.0f,
				EIGDialoguePriority::Story, Scoped(TEXT("Rule.Dropped"), EIGDialogueLifetime::Story));
			Hud->AdvanceDialogueForTesting(T0);
			Hud->AdvanceDialogueForTesting(T0 + 2.5);
			Hud->AdvanceDialogueForTesting(T0 + 5.0);
			AIGHorrorHUD::CancelDialogue(this, TEXT("Rule.Dropped"));
			const TArray<FIGHeardLine>& Log = Narrative->GetHeardLines();
			const auto Logged = [&Log](const FText& Line)
			{
				return Log.ContainsByPredicate([&Line](const FIGHeardLine& Entry) { return Entry.Line.EqualTo(Line); });
			};
			Check(Log.Num() > 0 && Log.Last().Line.EqualTo(Heard) && Log.Last().Speaker.EqualTo(Speaker),
				TEXT("heard_log_keeps_completed_story_line"));
			Check(!Logged(Brief) && !Logged(Dropped), TEXT("heard_log_skips_reactions_and_cancelled"));

			// 들은 말은 저장 파일에 실려 돌아온다. 실제 저장과 같은 직렬화(SaveGame 속성)를 거친다.
			UIGSaveGame* Saved = NewObject<UIGSaveGame>(this);
			Saved->Progress.MissingFloorNarrative = Narrative->GetSnapshot();
			TArray<uint8> Bytes;
			const bool bWritten = UGameplayStatics::SaveGameToMemory(Saved, Bytes);
			const UIGSaveGame* Loaded = bWritten ? Cast<UIGSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes)) : nullptr;
			const TArray<FIGHeardLine>* Restored = Loaded ? &Loaded->Progress.MissingFloorNarrative.Heard : nullptr;
			Check(Restored && Restored->Num() == Log.Num() && Restored->Num() > 0
				&& Restored->Last().Line.EqualTo(Heard) && Restored->Last().Speaker.EqualTo(Speaker),
				TEXT("heard_log_survives_save_and_load"));
		}

		// 10. 마주 보고 하는 말은 화자에게서 멀어지면 끊기고, 돌아와 다시 말을 걸면 끊긴 데부터 잇는다.
		if (AIGPlayerCharacter* Walker = Player.Get())
		{
			const FVector Here = Walker->GetActorLocation();
			const auto MoveListener = [Walker](const FVector& Where)
			{
				Walker->SetActorLocation(Where, false, nullptr, ETeleportType::TeleportPhysics);
			};
			const auto TalkScope = [&Here]()
			{
				FIGDialogueScope Scope;
				Scope.Context = TEXT("Rule.Talk");
				Scope.SpeakerLocation = Here;
				Scope.EarshotRadius = 600.0f;
				Scope.bReplayIfInterrupted = true;
				return Scope;
			};
			Hud->ClearDialogueForTesting();
			AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineA, EIGDialogueChannel::Conversation, 0.0f,
				EIGDialoguePriority::Story, TalkScope());
			AIGHorrorHUD::PushDialogueScoped(this, Speaker, LineB, EIGDialogueChannel::Conversation, 0.0f,
				EIGDialoguePriority::Story, TalkScope());
			Hud->AdvanceDialogueForTesting(T0);
			MoveListener(Here + FVector(0.0f, 0.0f, 3000.0f));
			Hud->AdvanceDialogueForTesting(T0 + 0.1);
			Check(Hud->GetQueuedDialogueCountForTesting() == 0 && Hud->HasInterruptedTalkForTesting(TEXT("Rule.Talk")),
				TEXT("walking_away_interrupts_talk"));
			MoveListener(Here);
			const bool bResumed = AIGHorrorHUD::ResumeInterruptedTalk(this, TEXT("Rule.Talk"));
			Hud->AdvanceDialogueForTesting(T0 + 0.2);
			Check(bResumed && Hud->GetQueuedDialogueCountForTesting() == 2
				&& Hud->GetActiveDialogueLineForTesting().ToString() == LineA.ToString(),
				TEXT("talk_resumes_where_it_stopped"));
			MoveListener(Here + FVector(0.0f, 0.0f, 3000.0f));
			Hud->AdvanceDialogueForTesting(T0 + 0.3);
			AIGHorrorHUD::CancelDialogue(this, TEXT("Rule.Talk"));
			Check(!Hud->HasInterruptedTalkForTesting(TEXT("Rule.Talk")) && Hud->GetQueuedDialogueCountForTesting() == 0,
				TEXT("cancel_clears_held_talk"));
			MoveListener(Here);

			// 11. 소리 자막은 그 소리가 들리는 거리 안에서만 뜨고, 그 자리를 벗어나면 내린다.
			Hud->ClearDialogueForTesting();
			AIGHorrorHUD::PushAudioCaptionScoped(this, FText::FromString(TEXT("[규칙 검사 먼 소리]")), 2.0f,
				Here + FVector(3000.0f, 0.0f, 0.0f), TEXT("Rule.Far"), 900.0f);
			Check(!Hud->HasPendingAudioCaption(), TEXT("far_sound_caption_not_shown"));
			AIGHorrorHUD::PushAudioCaptionScoped(this, FText::FromString(TEXT("[규칙 검사 가까운 소리]")), 5.0f,
				Here, TEXT("Rule.Near"), 900.0f);
			const bool bNearShown = Hud->HasPendingAudioCaption();
			MoveListener(Here + FVector(0.0f, 0.0f, 3000.0f));
			Hud->AdvanceAudioCaptionsForTesting();
			Check(bNearShown && !Hud->HasPendingAudioCaption(), TEXT("caption_drops_when_out_of_earshot"));
			MoveListener(Here);

			// 12. 가게 단말기 같은 생활 소리는 「중요한 소리」에서 뜨지 않고 「모든 소리」에서만 뜬다.
			const FText LifeSound = FText::FromString(TEXT("[규칙 검사 생활 소리]"));
			Hud->ClearDialogueForTesting();
			AIGHorrorHUD::PushAudioCaptionWhileAudible(this, LifeSound, 2.0f, Here, TEXT("Rule.Life"),
				[]() { return true; }, EIGSoundCaptionWeight::Background);
			const bool bHiddenWhenImportant = !Hud->HasPendingAudioCaption();
			ChooseSoundCaptions(true, true);
			AIGHorrorHUD::PushAudioCaptionWhileAudible(this, LifeSound, 2.0f, Here, TEXT("Rule.Life"),
				[]() { return true; }, EIGSoundCaptionWeight::Background);
			Check(bHiddenWhenImportant && Hud->HasPendingAudioCaption(), TEXT("life_sounds_only_with_all_sounds"));
			Hud->ClearDialogueForTesting();
		}
		ChooseSoundCaptions(false, false);
		Finish();
		break;
	}

	case 9:
	{
		// 에필로그 「403호 문」. 화난 쪽지 셋은 떼고 하늘색 마지막 쪽지만 붙인다. 쪽지 글씨는
		// 읽히지 않는 거리에서 찍는다(그림에는 한글을 굽지 않는다). HUD와 몸, 손전등은 뺀다.
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (!bActed)
		{
			bActed = true;
			for (const TWeakObjectPtr<AIGReadableNote>& Note : Notes)
			{
				Note->SetActorHiddenInGame(true);
			}
			UStaticMesh* LastMesh = LoadObject<UStaticMesh>(
				nullptr, TEXT("/Game/Meshes/SM_Note303Last.SM_Note303Last"), nullptr, LOAD_NoWarn);
			Check(LastMesh != nullptr, TEXT("last_note_mesh_loads"));
			if (LastMesh)
			{
				UStaticMeshComponent* LastNote = NewObject<UStaticMeshComponent>(HomeDoor, TEXT("EpilogueNote303Last"));
				LastNote->SetStaticMesh(LastMesh);
				LastNote->SetMobility(EComponentMobility::Movable);
				LastNote->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				LastNote->SetCastShadow(false);
				LastNote->SetupAttachment(HomeDoor->GetDoorPivot());
				LastNote->SetRelativeLocation(FVector(2.52f, AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f, 0.0f));
				LastNote->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
				LastNote->RegisterComponent();
			}
			HomeDoor->ForceOpenState(false);
			if (UIGFlashlightComponent* Torch = Player->GetFlashlight())
			{
				Torch->SetOn(false);
			}
			Player->SetActorHiddenInGame(true);
			if (AHUD* Hud = PC ? PC->GetHUD() : nullptr)
			{
				Hud->bShowHUD = false;
			}
			ACameraActor* StillCamera = GetWorld()->SpawnActor<ACameraActor>(
				ACameraActor::StaticClass(),
				FTransform((DoorStillLook - DoorStillEye).Rotation(), DoorStillEye + Floor4));
			if (StillCamera && PC)
			{
				UCameraComponent* Lens = StillCamera->GetCameraComponent();
				Lens->SetFieldOfView(DoorStillFov);
				Lens->SetConstraintAspectRatio(false);
				Lens->bOverrideAspectRatioAxisConstraint = true;
				Lens->SetAspectRatioAxisConstraint(EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV);
				PC->SetViewTargetWithBlend(StillCamera, 0.0f);
			}
		}
		// 노출과 시간 누적 안티에일리어싱이 가라앉은 뒤에 찍는다.
		if (PhaseSeconds >= 4.0f && PhaseSeconds - DeltaSeconds < 4.0f)
		{
			Shoot(TEXT("epilogue-door-note"));
		}
		if (PhaseSeconds >= 4.6f)
		{
			Finish();
		}
		break;
	}

	default:
		break;
	}
}
