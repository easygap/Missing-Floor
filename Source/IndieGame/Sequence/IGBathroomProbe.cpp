#include "Sequence/IGBathroomProbe.h"

#include "Components/CapsuleComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGNoiseSubsystem.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGBathroomRefuge.h"
#include "Interaction/IGSwingDoor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGStoryStateSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGInteractionComponent.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGBathroomProbe
{
	/** 403호 쪽 문 앞, 욕실 안 깊은 자리, 잠금 단추, 열린 문짝 가운데. 4층 바닥 기준. */
	const FVector OutsideFeet(140.0f, -135.0f, 0.0f);
	const FVector InsideFeet(300.0f, -108.0f, 0.0f);
	const FVector DoorFace(197.0f, -135.0f, 100.0f);
	const FVector LockButton(200.4f, -108.9f, 99.0f);
	const FVector OpenLeaf(232.0f, -172.0f, 100.0f);
	/** 욕실 안에서 소리가 나는 자리. 몸 높이 90 cm. */
	const FVector RoomNoise(320.0f, -140.0f, 90.0f);
	/** 위층 사람을 세우는 403호 방 안 북서쪽. */
	const FVector ListenerStart(-40.0f, 60.0f, 0.0f);
}

AIGBathroomProbe::AIGBathroomProbe()
{
	// 검사기는 매 프레임 그녀와 문, 위층 사람을 잰다. -IGBathroomProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGBathroomProbe::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGListenerEntity* InListener,
	UIGNoiseSubsystem* InNoise)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
	Noise = InNoise;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGBathroomShots"));
	if (InNoise)
	{
		NoiseHandle = InNoise->OnNoiseReported.AddUObject(this, &AIGBathroomProbe::HandleNoise);
	}
}

void AIGBathroomProbe::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UIGNoiseSubsystem* Subsystem = Noise.Get())
	{
		Subsystem->OnNoiseReported.Remove(NoiseHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AIGBathroomProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("BATHROOM_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGBathroomProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("BATHROOM_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGBathroomProbe::Stand(const FVector& Feet, const FVector& LookAt)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !PC)
	{
		return;
	}
	const FVector Floor(0.0f, 0.0f, AIGPrologueWorldScene::FourthFloorZ);
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(Feet + Floor + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr,
		ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	const FVector Eye = Character->GetActorLocation() + Character->GetEyeOffsetFromActor();
	PC->SetControlRotation((LookAt + Floor - Eye).Rotation());
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
	}
}

bool AIGBathroomProbe::Tap(const AActor* Expected, const TCHAR* FocusCheck)
{
	AIGPlayerCharacter* Character = Player.Get();
	UIGInteractionComponent* Interaction = Character ? Character->FindComponentByClass<UIGInteractionComponent>() : nullptr;
	if (!Interaction)
	{
		Check(false, FocusCheck);
		return false;
	}
	Interaction->SetInteractionInputEnabled(true);
	Interaction->RefreshFocus();
	const bool bFocused = Expected && Interaction->GetFocusedActor() == Expected;
	Check(bFocused, FocusCheck);
	if (!bFocused)
	{
		return false;
	}
	Interaction->PressInteraction();
	Interaction->ReleaseInteraction();
	return true;
}

void AIGBathroomProbe::Shoot(const TCHAR* Name)
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
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BathroomReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("BATHROOM_SHOT %s"), *Path);
}

void AIGBathroomProbe::HandleNoise(const FIGNoiseEvent& Event)
{
	// 욕실 문 손잡이를 밖에서 돌려 본 소리(0.3). 문이 낸 소리로 보고되고, 문 앞 공기에 조금
	// 먹혀(0.06) 0.24쯤으로 들어온다. 그녀의 숨과 심장(0.04)과는 넉넉히 갈린다.
	if (Phase == 7 && Event.Loudness >= 0.2f && Event.Instigator.Get() == Door.Get())
	{
		if (!bRattleHeard)
		{
			UE_LOG(LogIndieGame, Display, TEXT("BATHROOM_GUEST rattle loud=%.2f"), Event.Loudness);
		}
		bRattleHeard = true;
	}
}

void AIGBathroomProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 330.0f)
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
	AIGPlayerCharacter* Character = Player.Get();
	AIGListenerEntity* Upstairs = Listener.Get();
	AIGPrologueWorldScene* Building = Scene.Get();
	if (!Character || !Upstairs || !Building || !Noise.IsValid())
	{
		Check(false, TEXT("stage_ready"));
		Finish();
		return;
	}
	AIGBathroomRefuge* Room = Refuge.Get();
	AIGSwingDoor* RoomDoor = Door.Get();
	AIGSwingDoor* HomeDoor = Building->GetHomeDoor();
	const FVector Floor(0.0f, 0.0f, AIGPrologueWorldScene::FourthFloorZ);
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;

	switch (Phase)
	{
	case 0:
	{
		if (PhaseSeconds < (bShots ? 12.0f : 1.0f))
		{
			return;
		}
		for (TActorIterator<AIGBathroomRefuge> It(GetWorld()); It; ++It)
		{
			Refuge = *It;
			Door = It->GetDoor();
		}
		Room = Refuge.Get();
		RoomDoor = Door.Get();
		Check(Room && RoomDoor, TEXT("bathroom_refuge_and_door"));
		Check(RoomDoor && RoomDoor->IsFullyClosed() && !RoomDoor->IsLatched(), TEXT("door_closed_unlocked_at_start"));
		Check(!Character->IsConcealedInHidingSpot(), TEXT("not_hidden_at_start"));
		if (!Room || !RoomDoor || !HomeDoor)
		{
			Finish();
			return;
		}
		// 셋째 밤으로 둔다. 손님이 오는 밤이고, 밤2의 첫 대면 비트와 엮이지 않는다.
		if (Narrative)
		{
			Narrative->SetNightIndex(3);
		}
		Upstairs->SetDormant(true);
		if (UIGFlashlightComponent* Torch = Character->GetFlashlight())
		{
			Torch->SetAvailable(true);
			Torch->SetOn(true);
		}
		if (UIGStoryStateSubsystem* Story = GetGameInstance()->GetSubsystem<UIGStoryStateSubsystem>())
		{
			Story->AddState(FGameplayTag::RequestGameplayTag(TEXT("State.MissingFloor.BathroomLightOn"), false));
		}
		Next(bShots ? 20 : 1);
		break;
	}

	// 렌더 확인용 장면.
	case 20:
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(FVector(40.0f, -60.0f, 0.0f), FVector(200.0f, -150.0f, 110.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("bathroom-door-closed"));
			Next(1);
		}
		break;

	case 1:
		// 403호 쪽에서 욕실 문을 연다. 안으로(동쪽) 열린다.
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(IGBathroomProbe::OutsideFeet, IGBathroomProbe::DoorFace);
			return;
		}
		if (PhaseSeconds < 0.3f)
		{
			return;
		}
		Tap(RoomDoor, TEXT("door_takes_focus_from_room"));
		Next(2);
		break;

	case 2:
		if (PhaseSeconds < 1.8f)
		{
			return;
		}
		Check(RoomDoor->IsOpen() && RoomDoor->GetComponentsBoundingBox().GetCenter().X > 205.0f,
			TEXT("door_opens_into_bathroom"));
		Next(bShots ? 21 : 3);
		break;

	case 21:
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(FVector(150.0f, -128.0f, 0.0f), FVector(330.0f, -120.0f, 95.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("bathroom-doorway"));
			Next(3);
		}
		break;

	case 3:
		// 안에 들어가 문을 닫는다. 문짝이 도는 자리에서 비켜 선다.
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(IGBathroomProbe::InsideFeet, IGBathroomProbe::OpenLeaf);
			return;
		}
		if (PhaseSeconds < 0.3f)
		{
			return;
		}
		Check(Room->IsInside(Character->GetActorLocation()), TEXT("player_inside_bathroom"));
		Tap(RoomDoor, TEXT("open_door_takes_focus_inside"));
		Next(4);
		break;

	case 4:
		if (PhaseSeconds < 1.8f)
		{
			return;
		}
		Check(RoomDoor->IsFullyClosed(), TEXT("door_closes_from_inside"));
		Next(41);
		break;

	case 41:
		// 로제트 가운데 단추를 눌러 잠근다.
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(IGBathroomProbe::InsideFeet, IGBathroomProbe::LockButton);
			return;
		}
		if (PhaseSeconds < 0.3f)
		{
			return;
		}
		Tap(Room, TEXT("lock_button_takes_focus"));
		Next(40);
		break;

	case 40:
		if (PhaseSeconds < 0.5f)
		{
			return;
		}
		Check(RoomDoor->IsLatched(), TEXT("button_locks_door"));
		Check(Character->IsConcealedInHidingSpot() && Character->IsInLockedRoom() && !Character->GetHidingSpot(),
			TEXT("locked_bathroom_hides_her"));
		Next(bShots ? 22 : 5);
		break;

	case 22:
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(FVector(330.0f, -150.0f, 0.0f), FVector(198.0f, -120.0f, 100.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("bathroom-locked-inside"));
			Next(23);
		}
		break;
	case 23:
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(FVector(250.0f, -170.0f, 0.0f), FVector(300.0f, -60.0f, 90.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("bathroom-basin-toilet"));
			Next(5);
		}
		break;

	case 5:
	{
		// 현관을 열어 두고 위층 사람을 방 안에 세운다. 욕실 안에서 소리가 난다.
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(IGBathroomProbe::InsideFeet, IGBathroomProbe::LockButton);
			HomeDoor->ForceOpenState(true);
			Upstairs->SetDormant(false);
			Upstairs->ParkForBeat(IGBathroomProbe::ListenerStart + Floor, -40.0f);
			bListenerKnocked = false;
			bListenerEntered = false;
			bKnockSoundHeard = false;
			if (!bKnockSoundBound)
			{
				bKnockSoundBound = true;
				Upstairs->OnKnocked.AddWeakLambda(this, [this](const FVector& At)
				{
					bKnockSoundHeard = true;
					KnockSoundAt = At;
				});
			}
			return;
		}
		if (PhaseSeconds >= 0.5f && PhaseSeconds - DeltaSeconds < 0.5f)
		{
			Noise->ReportNoiseUnmasked(IGBathroomProbe::RoomNoise + Floor, 0.6f, nullptr);
		}
		if (Room->IsInside(Upstairs->GetActorLocation()))
		{
			bListenerEntered = true;
		}
		if (!bListenerKnocked && Upstairs->GetListenerState() == EIGListenerState::Banging)
		{
			bListenerKnocked = true;
			ListenerKnockAt = Upstairs->GetActorLocation();
			UE_LOG(LogIndieGame, Display, TEXT("BATHROOM_LISTENER knock_at=%s t=%.1f"),
				*ListenerKnockAt.ToCompactString(), PhaseSeconds);
		}
		if ((bListenerKnocked && PhaseSeconds >= 5.0f) || PhaseSeconds >= 25.0f)
		{
			Check(bListenerKnocked, TEXT("listener_knocks_on_bathroom_door"));
			Check(bListenerKnocked && FVector::Dist2D(ListenerKnockAt, FVector(165.0f, -135.0f, 0.0f)) < 90.0f,
				TEXT("listener_knocks_in_front_of_bathroom"));
			// §3.1 노크는 그의 몸 자리가 아니라 욕실 문짝(속 빈 ABS)에서 난다.
			Check(bKnockSoundHeard
				&& RoomDoor->GetKnockSurface() == EIGDoorKnockSurface::Hollow
				&& FVector::Dist(KnockSoundAt, RoomDoor->GetKnockPoint(KnockSoundAt)) < 3.0f,
				TEXT("knock_sounds_from_hollow_bathroom_door"));
			UE_LOG(LogIndieGame, Display, TEXT("BATHROOM_LISTENER knock_sound_at=%s"), *KnockSoundAt.ToCompactString());
			Check(!bListenerEntered, TEXT("listener_stays_out_of_locked_bathroom"));
			Check(RoomDoor->IsFullyClosed() && RoomDoor->IsLatched() && Character->IsConcealedInHidingSpot(),
				TEXT("still_locked_and_hidden_after_knock"));
			Upstairs->SetDormant(true);
			HomeDoor->ForceOpenState(false);
			Next(6);
		}
		break;
	}

	case 6:
		// 손님을 기다린다. 그 시간이 열리고 150초가 지나고 집에 25초 있었으면 현관을 두드린다.
		if (HomeDoor->IsOpen() && !bGuestOpened)
		{
			bGuestOpened = true;
			UE_LOG(LogIndieGame, Display, TEXT("BATHROOM_GUEST door_opened t=%.1f"), TotalSeconds);
			Next(7);
		}
		else if (PhaseSeconds >= 220.0f)
		{
			Check(false, TEXT("guest_comes_in"));
			Finish();
		}
		break;

	case 7:
		// 들어온 손님이 욕실 안 소리를 듣는다. 잠긴 손잡이만 덜컥거리고 가야 한다.
		if (PhaseSeconds >= 0.8f && PhaseSeconds - DeltaSeconds < 0.8f)
		{
			const FIGNoiseEvent Made = Noise->ReportNoise(IGBathroomProbe::RoomNoise + Floor, 0.8f, Character);
			UE_LOG(LogIndieGame, Display, TEXT("BATHROOM_GUEST made_noise loud=%.2f global_mask=%.2f mask_here=%.2f"),
				Made.Loudness, Noise->GetGlobalMasking(), Noise->GetMaskingAt(IGBathroomProbe::RoomNoise + Floor));
		}
		if (!Room->IsInside(Character->GetActorLocation()))
		{
			Check(false, TEXT("guest_never_reaches_her"));
			Finish();
			return;
		}
		if (PhaseSeconds >= 3.0f && !HomeDoor->IsOpen() && RoomDoor->IsFullyClosed())
		{
			UE_LOG(LogIndieGame, Display, TEXT("BATHROOM_GUEST left t=%.1f rattle=%d"), PhaseSeconds, bRattleHeard ? 1 : 0);
			Check(bGuestOpened, TEXT("guest_comes_in"));
			Check(bRattleHeard, TEXT("guest_rattles_locked_bathroom"));
			Check(RoomDoor->IsLatched() && Character->IsConcealedInHidingSpot(), TEXT("guest_leaves_her_hidden"));
			Next(8);
		}
		else if (PhaseSeconds >= 45.0f)
		{
			Check(false, TEXT("guest_leaves"));
			Finish();
		}
		break;

	case 8:
		// 잠금을 풀고 안에서 연다.
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(IGBathroomProbe::InsideFeet, IGBathroomProbe::LockButton);
			return;
		}
		if (PhaseSeconds < 0.3f)
		{
			return;
		}
		Tap(Room, TEXT("lock_button_takes_focus_again"));
		Next(9);
		break;

	case 9:
		if (PhaseSeconds < 0.5f)
		{
			return;
		}
		Check(!RoomDoor->IsLatched() && !Character->IsConcealedInHidingSpot(), TEXT("unlock_ends_hiding"));
		Finish();
		break;

	default:
		break;
	}
}
