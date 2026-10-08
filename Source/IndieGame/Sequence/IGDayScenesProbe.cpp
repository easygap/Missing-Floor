#include "Sequence/IGDayScenesProbe.h"

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
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGNightPhaseDirector.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGInteractable.h"
#include "Interaction/IGReadableNote.h"
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
			JeongwooTalk->OnExamined.Broadcast(JeongwooTalk.Get());
			Check(Narrative->HasBeatPlayed(FName(TEXT("Day.Jeongwoo.Intro")))
				&& SaidLine(NSLOCTEXT("IGMissingFloor", "JeongwooIntro",
					"저 202호 살아요, 정우예요. 새벽에 오토바이 소리 나면 저예요. 죄송해요.")),
				TEXT("jeongwoo_introduces_himself_day1"));
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
			JeongwooTalk->OnExamined.Broadcast(JeongwooTalk.Get());
			Check(Narrative->HasBeatPlayed(FName(TEXT("Day.Jeongwoo.Guest")))
				&& SaidLine(NSLOCTEXT("IGMissingFloor", "JeongwooGuest3", "동네장터에도 올렸어요. 403호도 걸쇠 꼭 거세요.")),
				TEXT("jeongwoo_tells_mother_voice_day2"));
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
			Finish();
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
			Finish();
		}
		break;

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
