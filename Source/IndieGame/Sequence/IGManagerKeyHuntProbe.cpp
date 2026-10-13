#include "Sequence/IGManagerKeyHuntProbe.h"

#include "Components/CapsuleComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGManagerPatrol.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGHidingSpot.h"
#include "Interaction/IGInteractable.h"
#include "Interaction/IGSwingDoor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGStoryStateSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGManagerKeyHuntProbe
{
	/** 403호 방 안, 장롱 앞. 현관에서는 안 보이는 자리다. */
	const FVector HomeFeet(-100.0f, 0.0f, 900.0f);
	/**
	 * 5층 안, 철문 정면으로 2.2 m. 문간에서 비추면 바로 보인다. 판재 더미 모서리(X 60)에
	 * 붙여 세웠더니 몸이 더미 위로 밀려 올라갔고, 열린 문짝에 가려서 안 보였다.
	 */
	const FVector AnnexFeet(130.0f, 640.0f, 1200.0f);
	/** 관리인이 403호 문 동쪽에 서는 자리와 5층 철문 앞. */
	const FVector HomeStand(240.0f, -295.0f, 900.0f);
	const FVector AnnexStand(130.0f, 390.0f, 1200.0f);
	constexpr float ReachTolerance = 40.0f;
	constexpr float ProbeTimeoutSeconds = 420.0f;
	/** 카트 뒤 화면을 찍기 전에 셰이더와 노출이 가라앉기를 기다린다. */
	constexpr float HiddenViewSettleSeconds = 6.0f;
	/** 5층 철문이 열리고 손전등이 안을 한 번 훑을 즈음. */
	constexpr float SweepShotDelaySeconds = 1.6f;
}

AIGManagerKeyHuntProbe::AIGManagerKeyHuntProbe()
{
	// 관리인이 도는 동안 매 프레임 자리와 문을 본다. -IGManagerKeyHuntProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGManagerKeyHuntProbe::Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer)
{
	Scene = InScene;
	Player = InPlayer;
	bOpen = FParse::Param(FCommandLine::Get(), TEXT("IGManagerKeyHuntOpen"));
	bHidden = !bOpen && FParse::Param(FCommandLine::Get(), TEXT("IGManagerKeyHuntHidden"));
	bShots = bHidden && FParse::Param(FCommandLine::Get(), TEXT("IGManagerKeyHuntShots"));
}

void AIGManagerKeyHuntProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("KEYHUNT_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGManagerKeyHuntProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("KEYHUNT_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGManagerKeyHuntProbe::Stand(const FVector& Feet, const float Yaw)
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
	if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
	{
		PC->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
	}
}

void AIGManagerKeyHuntProbe::Shoot(const TCHAR* Name)
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
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("KeyHuntReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("KEYHUNT_SHOT %s"), *Path);
}

void AIGManagerKeyHuntProbe::SetTorch(const bool bOn)
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

void AIGManagerKeyHuntProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	using namespace IGManagerKeyHuntProbe;
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > ProbeTimeoutSeconds)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	AIGManagerPatrol* Mok = Manager.Get();
	AIGSwingDoor* Home = HomeDoor.Get();
	AIGSwingDoor* Roof = RoofDoor.Get();
	AIGSwingDoor* Annex = AnnexDoor.Get();

	switch (Phase)
	{
	case 0:
	{
		if (PhaseSeconds < 1.0f)
		{
			return;
		}
		for (TActorIterator<AIGManagerPatrol> It(GetWorld()); It; ++It)
		{
			Manager = *It;
		}
		for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It)
		{
			NightLoop = *It;
		}
		for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
		{
			RoofDoor = It->GetStairGate();
			AnnexDoor = It->GetAnnexGate();
			CartSpot = It->GetCartHidingSpot();
		}
		// 다른 괴이는 멈추고 관리인만 돌린다. 어둑시니와 손님은 틱이 아니라 타이머로 돌아서 틱만
		// 끄면 계속 나온다. 카트 뒤 어둠에 1분쯤 있으니 어둑시니가 숨은 자리 앞에 나타났다.
		for (TActorIterator<AIGNightThreatDirector> It(GetWorld()); It; ++It)
		{
			It->SetActorTickEnabled(false);
			GetWorldTimerManager().ClearAllTimersForObject(*It);
		}
		for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
		{
			It->SetDormant(true);
		}
		HomeDoor = Scene.IsValid() ? Scene->GetHomeDoor() : nullptr;
		Check(Narrative && Manager.IsValid() && NightLoop.IsValid() && HomeDoor.IsValid() && RoofDoor.IsValid()
			&& AnnexDoor.IsValid() && CartSpot.IsValid(), TEXT("actors_resolved"));
		if (!Narrative || !Manager.IsValid() || !NightLoop.IsValid() || !HomeDoor.IsValid()
			|| !RoofDoor.IsValid() || !AnnexDoor.IsValid() || !CartSpot.IsValid())
		{
			Finish();
			return;
		}
		// 셋째 밤. 유담이 관리실 책상에서 계단 열쇠 꾸러미를 가져갔다.
		Narrative->SetNightIndex(3);
		if (UIGStoryStateSubsystem* Story = GetGameInstance()->GetSubsystem<UIGStoryStateSubsystem>())
		{
			Story->AddState(FGameplayTag::RequestGameplayTag(FName(TEXT("State.MissingFloor.HasStairKey")), false));
		}
		RoofDoor->ForceOpenState(false);
		AnnexDoor->ForceOpenState(false);
		HomeDoor->ForceOpenState(false);
		if (bOpen)
		{
			HomeDoor->SetLatched(false, false);
			AnnexDoor->SetLatched(false, false);
			Stand(AnnexFeet, 90.0f);
		}
		else if (bHidden)
		{
			// 카트 앞에 서서 숨는다. 들어가 앉는 데 1초가 안 걸린다.
			HomeDoor->SetLatched(true, false);
			AnnexDoor->SetLatched(false, false);
			const FVector Approach = CartSpot->GetApproachLocation();
			Stand(Approach, (CartSpot->GetActorLocation() - Approach).Rotation().Yaw);
			FIGInteractionContext Context;
			Context.Interactor = Player.Get();
			Context.TargetActor = CartSpot.Get();
			Context.HoldProgress = 1.0f;
			Check(IIGInteractable::Execute_CanInteract(CartSpot.Get(), Player.Get()), TEXT("cart_spot_usable"));
			IIGInteractable::Execute_CompleteInteraction(CartSpot.Get(), Context);
		}
		else
		{
			HomeDoor->SetLatched(true, false);
			AnnexDoor->SetLatched(true, false);
			Stand(HomeFeet, 180.0f);
		}
		CapturesAtStart = NightLoop->GetCaptureCount();
		Phase = bHidden ? 10 : 1;
		PhaseSeconds = 0.0f;
		return;
	}

	case 10:
		// 카트 뒤에 다 앉을 때까지 기다린다.
		if (!CartSpot->IsPlayerInside())
		{
			if (PhaseSeconds > 5.0f)
			{
				Check(false, TEXT("hidden_behind_cart"));
				Finish();
			}
			return;
		}
		Check(Player.IsValid() && Player->IsConcealedInHidingSpot(), TEXT("hidden_behind_cart"));
		if (bShots)
		{
			// 카트 너머로 뭐가 보이는지 손전등을 켜고 한 장 찍는다. 관리인이 오기 전에 끈다.
			SetTorch(true);
			Phase = 11;
			PhaseSeconds = 0.0f;
			return;
		}
		Phase = 1;
		PhaseSeconds = 0.0f;
		return;

	case 11:
		if (PhaseSeconds < HiddenViewSettleSeconds)
		{
			return;
		}
		Shoot(TEXT("keyhunt-cart-hidden"));
		Phase = 12;
		PhaseSeconds = 0.0f;
		return;

	case 12:
		// 찍는 요청은 프레임 끝에 처리된다. 같은 프레임에 끄면 꺼진 화면이 찍힌다.
		SetTorch(false);
		Phase = 1;
		PhaseSeconds = 0.0f;
		return;

	case 1:
		if (!Mok->IsOnDuty())
		{
			if (PhaseSeconds > 10.0f)
			{
				Check(false, TEXT("manager_on_duty"));
				Finish();
			}
			return;
		}
		Mok->StartKeyHuntForTesting();
		Check(Mok->IsOnKeyHunt(), TEXT("key_hunt_starts"));
		Phase = 2;
		PhaseSeconds = 0.0f;
		return;

	case 2:
	{
		const FVector At = Mok->GetActorLocation();
		bReachedHome |= FVector::Dist(At, HomeStand) < ReachTolerance;
		bReachedAnnex |= FVector::Dist(At, AnnexStand) < ReachTolerance;
		bEnteredAnnex |= At.Y > 470.0f && At.Z > 1150.0f;
		if (Home->IsOpen())
		{
			bHomeOpened = true;
		}
		else if (bHomeOpened && !Home->IsSwinging())
		{
			bHomeClosedAgain = true;
		}
		bRoofOpened |= Roof->IsOpen();
		if (Annex->IsOpen())
		{
			bAnnexOpened = true;
			AnnexOpenedAt = AnnexOpenedAt < 0.0f ? TotalSeconds : AnnexOpenedAt;
		}
		if (bShots && !bShotSweep && AnnexOpenedAt >= 0.0f && TotalSeconds - AnnexOpenedAt >= SweepShotDelaySeconds)
		{
			bShotSweep = true;
			Shoot(TEXT("keyhunt-cart-sweep"));
		}
		else if (bAnnexOpened && !Annex->IsSwinging())
		{
			bAnnexClosedAgain = true;
		}
		bRetreated |= Mok->GetPatrolState() == EIGManagerPatrolState::Retreating;
		if (static_cast<int32>(Mok->GetPatrolState()) != LoggedState)
		{
			LoggedState = static_cast<int32>(Mok->GetPatrolState());
			UE_LOG(LogIndieGame, Display, TEXT("KEYHUNT_STATE t=%.1f state=%d at=(%.0f,%.0f,%.0f) hunt=%d"),
				TotalSeconds, LoggedState, At.X, At.Y, At.Z, Mok->IsOnKeyHunt() ? 1 : 0);
		}
		// 유담은 그 자리에 계속 서 있다. 밀려나지 않게 매 프레임 다시 세운다.
		if (bOpen)
		{
			Stand(AnnexFeet, 90.0f);
		}
		if (Mok->GetPatrolState() != EIGManagerPatrolState::Resting || Mok->IsOnKeyHunt())
		{
			return;
		}
		UE_LOG(LogIndieGame, Display, TEXT("KEYHUNT_DONE t=%.1f home=%d roof=%d annex=%d"),
			TotalSeconds, bHomeOpened ? 1 : 0, bRoofOpened ? 1 : 0, bAnnexOpened ? 1 : 0);
		Check(bReachedHome, TEXT("manager_comes_to_403_door"));
		Check(bRoofOpened, TEXT("manager_unlocks_roof_door"));
		Check(!bEnteredAnnex, TEXT("manager_never_enters_annex"));
		Check(NightLoop->GetCaptureCount() == CapturesAtStart, TEXT("no_capture"));
		Check(Narrative->HasBeatPlayed(FName(TEXT("Night3.MokKeyHunt"))), TEXT("hunt_beat_recorded"));
		if (bOpen)
		{
			Check(bHomeOpened && bHomeClosedAgain && !Home->IsOpen(), TEXT("unlatched_home_opened_and_closed"));
			Check(bReachedAnnex, TEXT("manager_comes_to_annex_door"));
			Check(bAnnexOpened && bAnnexClosedAgain && !Annex->IsOpen(), TEXT("annex_opened_then_slammed"));
			Check(bRetreated, TEXT("manager_hurries_away"));
			Check(Narrative->HasBeatPlayed(FName(TEXT("Night3.MokAvoidsAnnex"))), TEXT("yudam_sees_he_wont_come_in"));
		}
		else if (bHidden)
		{
			Check(!bHomeOpened && Home->IsLatched(), TEXT("latch_holds_home_door"));
			Check(bReachedAnnex, TEXT("manager_comes_to_annex_door"));
			Check(bAnnexOpened && bAnnexClosedAgain && !Annex->IsOpen(), TEXT("annex_opened_and_closed_quietly"));
			Check(!bRetreated && !Narrative->HasBeatPlayed(FName(TEXT("Night3.MokAvoidsAnnex"))),
				TEXT("manager_never_sees_her_behind_cart"));
			Check(CartSpot->IsPlayerInside(), TEXT("still_behind_cart"));
			Check(!Roof->IsOpen(), TEXT("roof_door_closed_again"));
		}
		else
		{
			Check(!bHomeOpened && Home->IsLatched(), TEXT("latch_holds_home_door"));
			Check(bReachedAnnex, TEXT("manager_comes_to_annex_door"));
			Check(!bAnnexOpened && Annex->IsLatched(), TEXT("bolt_holds_annex_door"));
			Check(!Roof->IsOpen(), TEXT("roof_door_closed_again"));
		}
		Finish();
		return;
	}

	default:
		return;
	}
}
