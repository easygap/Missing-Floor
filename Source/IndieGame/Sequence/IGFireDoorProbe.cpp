#include "Sequence/IGFireDoorProbe.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGManagerPatrol.h"
#include "Entity/IGNightThreatDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGFireDoorWedge.h"
#include "Interaction/IGReadableNote.h"
#include "Interaction/IGSwingDoor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGInteractionComponent.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGFireDoorProbe
{
	/** 고임목을 내려다보는 자리와 고임목. 층 높이를 더해 쓴다. */
	const FVector WedgeViewFeet(-150.0f, -322.0f, 0.0f);
	const FVector WedgeLook(-212.0f, -351.0f, 3.0f);
	/** 그녀를 세워 두는 자리. 4층 복도 동쪽 끝이라 아래층 일에 끼지 않는다. */
	const FVector PlayerParked(520.0f, -305.0f, 900.0f);
	/** 2층 계단 참. 닫힌 2층 문 너머다. */
	const FVector LandingNoiseAt(-470.0f, -300.0f, 300.0f);
	/** 위층 사람을 세우는 2층 복도 동쪽. */
	const FVector ListenerStart(120.0f, -305.0f, 300.0f);
	/** 이보다 서쪽이면 닫힌 문을 뚫은 것이다. 문짝 평면 X -320에 두께 반을 뺐다. */
	constexpr float DoorPlaneX = -322.5f;
	/** 문 소리를 세는 반경. 문짝 가운데에서 잰다. */
	constexpr float NoiseWatchRadius = 260.0f;
}

AIGFireDoorProbe::AIGFireDoorProbe()
{
	// 검사기는 매 프레임 그녀와 위층 사람, 관리인을 잰다. -IGFireDoorProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGFireDoorProbe::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGListenerEntity* InListener,
	AIGManagerPatrol* InManager,
	UIGNoiseSubsystem* InNoise)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
	Manager = InManager;
	Noise = InNoise;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGFireDoorShots"));
	if (InNoise)
	{
		NoiseHandle = InNoise->OnNoiseReported.AddUObject(this, &AIGFireDoorProbe::HandleNoise);
	}
}

void AIGFireDoorProbe::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UIGNoiseSubsystem* Subsystem = Noise.Get())
	{
		Subsystem->OnNoiseReported.Remove(NoiseHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AIGFireDoorProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("FIRE_DOOR_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGFireDoorProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("FIRE_DOOR_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGFireDoorProbe::Stand(const FVector& Feet, const FVector& LookAt)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !PC)
	{
		return;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(Feet + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	const FVector Eye = Character->GetActorLocation() + Character->GetEyeOffsetFromActor();
	PC->SetControlRotation((LookAt - Eye).Rotation());
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
	}
}

void AIGFireDoorProbe::Shoot(const TCHAR* Name)
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
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FireDoorReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("FIRE_DOOR_SHOT %s"), *Path);
}

void AIGFireDoorProbe::HandleNoise(const FIGNoiseEvent& Event)
{
	if (FVector::Dist(Event.Location, NoiseWatchAt) <= IGFireDoorProbe::NoiseWatchRadius)
	{
		LoudestWatched = FMath::Max(LoudestWatched, Event.Loudness);
	}
}

void AIGFireDoorProbe::CheckStage()
{
	for (TActorIterator<AIGFireDoorWedge> It(GetWorld()); It; ++It)
	{
		Wedges.Add(*It);
	}
	Wedges.Sort([](const TWeakObjectPtr<AIGFireDoorWedge>& A, const TWeakObjectPtr<AIGFireDoorWedge>& B)
	{
		return A->GetActorLocation().Z < B->GetActorLocation().Z;
	});
	bool bThree = Wedges.Num() == 3;
	bool bWedgedOpen = bThree;
	bool bClearOfWall = bThree;
	for (int32 Index = 0; Index < Wedges.Num(); ++Index)
	{
		const AIGFireDoorWedge* Wedge = Wedges[Index].Get();
		const AIGSwingDoor* Door = Wedge ? Wedge->GetDoor() : nullptr;
		if (!Door)
		{
			bThree = false;
			continue;
		}
		bThree = bThree && FMath::IsNearlyEqual(Door->GetActorLocation().Z, 300.0f * (Index + 1), 1.0f);
		bWedgedOpen = bWedgedOpen && Wedge->IsWedged() && Door->IsOpen();
		// 열린 문짝의 복도 쪽 면과 레버 끝이 남쪽 벽을 뚫지 않는다. 회전한 상자 바운드는
		// 레버 두께를 경첩 쪽 모서리에도 얹어 실제보다 벽 쪽으로 2 cm 넘게 부푼다. 문짝 축
		// 기준의 실제 자리를 돌려서 잰다(복도 쪽 면 +2.5, 레버 끝 +8.7, 경첩에서 100~120 cm).
		const FTransform Pivot = Door->GetDoorPivot()->GetComponentTransform();
		const float LeverTipY = FMath::Min(
			Pivot.TransformPosition(FVector(8.7f, 112.5f, 100.0f)).Y,
			Pivot.TransformPosition(FVector(8.7f, 100.0f, 100.0f)).Y);
		const float HingeFootY = Pivot.TransformPosition(FVector(2.5f, 0.0f, 2.0f)).Y;
		const float FarFootY = Pivot.TransformPosition(FVector(2.5f, 120.0f, 2.0f)).Y;
		UE_LOG(LogIndieGame, Display, TEXT("FIRE_DOOR_LEAF z=%.0f lever_tip_y=%.1f hinge_foot_y=%.1f far_foot_y=%.1f"),
			Door->GetActorLocation().Z, LeverTipY, HingeFootY, FarFootY);
		// 허리 아래 걸레받이 앞면이 Y -368.9, 그 위 미장 앞면이 Y -371.2다.
		bClearOfWall = bClearOfWall && LeverTipY > -371.2f && HingeFootY > -368.9f && FarFootY > -368.9f;
	}
	Check(bThree, TEXT("three_fire_doors"));
	Check(bWedgedOpen, TEXT("wedged_open_at_start"));
	Check(bClearOfWall, TEXT("open_leaf_clears_south_wall"));

	bool bNote202 = false;
	bool bSign303 = false;
	for (TActorIterator<AIGReadableNote> It(GetWorld()); It; ++It)
	{
		const FVector At = It->GetActorLocation();
		bNote202 = bNote202 || (FVector::Dist(At, FVector(-30.0f, -237.35f, 300.0f)) < 1.0f
			&& It->GetBodyLines().Num() == 3);
		bSign303 = bSign303 || (FVector::Dist(At, FVector(78.0f, -237.35f, 600.0f)) < 1.0f
			&& It->GetBodyLines().Num() == 3);
	}
	Check(bNote202, TEXT("note_on_202_door"));
	Check(bSign303, TEXT("sign_on_303_door"));

	bool bBagShown = false;
	if (const AIGPrologueWorldScene* Building = Scene.Get())
	{
		TInlineComponentArray<UStaticMeshComponent*> Components(Building);
		for (const UStaticMeshComponent* Component : Components)
		{
			const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
			if (Mesh && Mesh->GetName() == TEXT("SM_DawnDeliveryBag"))
			{
				bBagShown = Component->IsVisible()
					&& Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
			}
		}
	}
	Check(bBagShown, TEXT("dawn_delivery_bag_at_night"));
}

void AIGFireDoorProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 220.0f)
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
	AIGManagerPatrol* Mok = Manager.Get();
	UIGInteractionComponent* Interaction = Character ? Character->FindComponentByClass<UIGInteractionComponent>() : nullptr;
	if (!Scene.IsValid() || !Character || !Upstairs || !Mok || !Interaction || !Noise.IsValid())
	{
		Check(false, TEXT("stage_ready"));
		Finish();
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	AIGFireDoorWedge* SecondWedge = Wedges.IsValidIndex(0) ? Wedges[0].Get() : nullptr;
	AIGFireDoorWedge* ThirdWedge = Wedges.IsValidIndex(1) ? Wedges[1].Get() : nullptr;
	AIGSwingDoor* SecondDoor = SecondWedge ? SecondWedge->GetDoor() : nullptr;
	AIGSwingDoor* ThirdDoor = ThirdWedge ? ThirdWedge->GetDoor() : nullptr;

	switch (Phase)
	{
	case 0:
		// 밤이 열리고 자막이 걷힐 때까지. 다른 괴이는 세워 두고 위층 사람은 재운다.
		if (PhaseSeconds < (bShots ? 12.0f : 1.0f))
		{
			return;
		}
		for (TActorIterator<AIGNightThreatDirector> It(GetWorld()); It; ++It)
		{
			It->SetActorTickEnabled(false);
		}
		Upstairs->SetDormant(true);
		Interaction->SetInteractionInputEnabled(true);
		if (UIGFlashlightComponent* Torch = Character->GetFlashlight())
		{
			Torch->SetAvailable(true);
			Torch->SetOn(true);
		}
		CheckStage();
		Next(bShots ? 10 : 1);
		break;

	// 렌더 확인용 장면. 시선을 돌린 직후 프레임은 흐려지므로 1.2초 기다렸다 찍는다.
	case 10:
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(FVector(-40.0f, -262.0f, 300.0f), FVector(-285.0f, -352.0f, 405.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("firedoor-wedged"));
			Next(11);
		}
		break;
	case 11:
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(FVector(-178.0f, -318.0f, 300.0f), FVector(-212.0f, -351.0f, 305.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("firedoor-wedge"));
			Next(12);
		}
		break;
	case 12:
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(FVector(-250.0f, -285.0f, 300.0f), FVector(-322.0f, -318.0f, 503.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("firedoor-closer"));
			Next(13);
		}
		break;
	case 13:
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(FVector(-40.0f, -305.0f, 300.0f), FVector(-75.0f, -237.0f, 432.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("door202"));
			Next(14);
		}
		break;
	case 14:
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(FVector(40.0f, -322.0f, 600.0f), FVector(125.0f, -240.0f, 680.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("door303-bag"));
			Next(1);
		}
		break;

	case 1:
		// 2층 고임목을 E로 툭 뺀다. 도어클로저가 문을 끌어당겨 쾅 닫힌다.
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(IGFireDoorProbe::WedgeViewFeet + FVector(0.0f, 0.0f, 300.0f),
				IGFireDoorProbe::WedgeLook + FVector(0.0f, 0.0f, 300.0f));
			return;
		}
		if (PhaseSeconds < 0.4f)
		{
			return;
		}
		Interaction->RefreshFocus();
		Check(SecondWedge && Interaction->GetFocusedActor() == SecondWedge, TEXT("wedge_takes_focus"));
		NoiseWatchAt = FVector(-320.0f, -305.0f, 400.0f);
		LoudestWatched = 0.0f;
		Interaction->PressInteraction();
		Interaction->ReleaseInteraction();
		Next(2);
		break;

	case 2:
		if (PhaseSeconds < 2.0f)
		{
			return;
		}
		Check(SecondWedge && !SecondWedge->IsWedged(), TEXT("tap_pulls_wedge"));
		Check(SecondDoor && SecondDoor->IsFullyClosed(), TEXT("tap_slams_door_shut"));
		UE_LOG(LogIndieGame, Display, TEXT("FIRE_DOOR_NOISE slam=%.2f"), LoudestWatched);
		Check(LoudestWatched >= 0.40f, TEXT("slam_is_loud"));
		Next(3);
		break;

	case 3:
		// 3층 고임목은 E를 1초 넘게 누르고 있는다. 문을 잡고 천천히 닫는다.
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(IGFireDoorProbe::WedgeViewFeet + FVector(0.0f, 0.0f, 600.0f),
				IGFireDoorProbe::WedgeLook + FVector(0.0f, 0.0f, 600.0f));
			return;
		}
		if (PhaseSeconds < 0.4f)
		{
			return;
		}
		Interaction->RefreshFocus();
		Check(ThirdWedge && Interaction->GetFocusedActor() == ThirdWedge, TEXT("third_wedge_takes_focus"));
		NoiseWatchAt = FVector(-320.0f, -305.0f, 700.0f);
		LoudestWatched = 0.0f;
		Interaction->PressInteraction();
		Next(4);
		break;

	case 4:
		if (PhaseSeconds < 1.25f)
		{
			return;
		}
		Interaction->ReleaseInteraction();
		Next(5);
		break;

	case 5:
		if (PhaseSeconds < 3.0f)
		{
			return;
		}
		Check(ThirdWedge && !ThirdWedge->IsWedged(), TEXT("hold_pulls_wedge"));
		Check(ThirdDoor && ThirdDoor->IsFullyClosed(), TEXT("hold_eases_door_shut"));
		UE_LOG(LogIndieGame, Display, TEXT("FIRE_DOOR_NOISE eased=%.2f"), LoudestWatched);
		Check(LoudestWatched > 0.0f && LoudestWatched <= 0.12f, TEXT("eased_close_is_quiet"));
		Next(bShots ? 15 : 6);
		break;

	case 15:
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(FVector(-150.0f, -300.0f, 300.0f), FVector(-320.0f, -305.0f, 410.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("firedoor-closed"));
			Next(16);
		}
		break;
	case 16:
		if (PhaseSeconds < DeltaSeconds * 1.5f)
		{
			Stand(FVector(-445.0f, -290.0f, 300.0f), FVector(-322.0f, -300.0f, 480.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("firedoor-closed-stairside"));
			Next(6);
		}
		break;

	case 6:
		// 닫힌 2층 문 너머 계단 참에서 소리가 난다. 위층 사람은 2층 복도 동쪽에 있다.
		Stand(IGFireDoorProbe::PlayerParked, IGFireDoorProbe::PlayerParked + FVector(-300.0f, 0.0f, 160.0f));
		Upstairs->SetDormant(false);
		Upstairs->ParkForBeat(IGFireDoorProbe::ListenerStart, 180.0f);
		ListenerMinX = Upstairs->GetActorLocation().X;
		bListenerKnocked = false;
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
		Next(7);
		break;

	case 7:
		if (PhaseSeconds >= 0.5f && PhaseSeconds - DeltaSeconds < 0.5f)
		{
			Noise->ReportNoiseUnmasked(IGFireDoorProbe::LandingNoiseAt, 0.6f, Character);
		}
		ListenerMinX = FMath::Min(ListenerMinX, Upstairs->GetActorLocation().X);
		if (!bListenerKnocked && Upstairs->GetListenerState() == EIGListenerState::Banging)
		{
			bListenerKnocked = true;
			ListenerKnockAt = Upstairs->GetActorLocation();
			UE_LOG(LogIndieGame, Display, TEXT("FIRE_DOOR_LISTENER knock_at=%s t=%.1f"),
				*ListenerKnockAt.ToCompactString(), PhaseSeconds);
		}
		if ((bListenerKnocked && PhaseSeconds >= 4.0f) || PhaseSeconds >= 25.0f)
		{
			Check(ListenerMinX > IGFireDoorProbe::DoorPlaneX, TEXT("listener_does_not_pass_closed_door"));
			Check(bListenerKnocked, TEXT("listener_knocks_at_closed_door"));
			Check(bListenerKnocked && FVector::Dist2D(ListenerKnockAt, FVector(-305.0f, -305.0f, 300.0f)) < 160.0f,
				TEXT("listener_knocks_in_front_of_door"));
			// §3.1 노크는 그의 몸 자리가 아니라 그 철문 문짝에서 난다.
			Check(bKnockSoundHeard && SecondDoor
				&& SecondDoor->GetKnockSurface() == EIGDoorKnockSurface::Steel
				&& FVector::Dist(KnockSoundAt, SecondDoor->GetKnockPoint(KnockSoundAt)) < 3.0f,
				TEXT("knock_sounds_from_steel_door_leaf"));
			UE_LOG(LogIndieGame, Display, TEXT("FIRE_DOOR_LISTENER knock_sound_at=%s"), *KnockSoundAt.ToCompactString());
			Check(SecondDoor && SecondDoor->IsFullyClosed(), TEXT("listener_leaves_door_shut"));
			Upstairs->SetDormant(true);
			Next(8);
		}
		break;

	case 8:
		// 셋째 밤. 관리인이 순찰을 나와 닫힌 2층 문을 열고 201호 창고로 간다.
		if (Narrative)
		{
			Narrative->SetNightIndex(3);
		}
		bManagerOpenedDoor = false;
		bManagerCrossedClosedDoor = false;
		Next(9);
		break;

	case 9:
	{
		const FVector At = Mok->GetActorLocation();
		const bool bOnSecondFloor = FMath::Abs(At.Z - 300.0f) < 60.0f;
		if (SecondDoor && bOnSecondFloor && FVector::Dist2D(At, FVector(-320.0f, -305.0f, 0.0f)) < 160.0f
			&& SecondDoor->IsOpen())
		{
			bManagerOpenedDoor = true;
		}
		if (SecondDoor && bOnSecondFloor && At.X > IGFireDoorProbe::DoorPlaneX + 5.0f
			&& SecondDoor->IsFullyClosed() && !bManagerOpenedDoor)
		{
			bManagerCrossedClosedDoor = true;
		}
		if (bOnSecondFloor && At.X > -200.0f)
		{
			UE_LOG(LogIndieGame, Display, TEXT("FIRE_DOOR_MANAGER through t=%.1f at=%s"), PhaseSeconds, *At.ToCompactString());
			Check(Mok->IsOnDuty(), TEXT("manager_on_duty_night_three"));
			Check(bManagerOpenedDoor, TEXT("manager_opens_closed_fire_door"));
			Check(!bManagerCrossedClosedDoor, TEXT("manager_never_walks_through_shut_door"));
			Finish();
		}
		else if (PhaseSeconds >= 110.0f)
		{
			UE_LOG(LogIndieGame, Display, TEXT("FIRE_DOOR_MANAGER stuck at=%s state=%d"),
				*At.ToCompactString(), static_cast<int32>(Mok->GetPatrolState()));
			Check(false, TEXT("manager_reaches_second_floor_corridor"));
			Finish();
		}
		break;
	}

	default:
		break;
	}
}
