#include "Sequence/IGCaptureVarietyProbe.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGManagerPatrol.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "EngineUtils.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGDoorLatch.h"
#include "Interaction/IGDroppedFlashlight.h"
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

namespace IGCaptureVarietyProbe
{
	/** 403호 방 안, 현관을 보는 자리. 4층 바닥 기준. */
	const FVector HomeFeet(0.0f, -60.0f, 0.0f);
	const FVector HomeDoorFace(131.0f, -225.0f, 120.0f);
	/** 4층 복도 가운데 줄(Y -305, 건물 길찾기의 복도 줄). 서쪽에서 동쪽으로 걷는다. */
	const FVector PathA(-200.0f, -305.0f, 0.0f);
	const FVector PathB(0.0f, -305.0f, 0.0f);
	const FVector PathC(200.0f, -305.0f, 0.0f);
	const FVector CaptureSpot(420.0f, -305.0f, 0.0f);
	/** 어둑시니 검사의 불빛 자리와 어둠 자리. */
	const FVector LitSpot(-100.0f, -305.0f, 0.0f);
	const FVector DarkSpot(420.0f, -305.0f, 0.0f);
	/** 관리인에게 잡히는 자리. 서쪽을 본다. */
	const FVector ManagerSpot(300.0f, -305.0f, 0.0f);
	/** 한 자리에 서서 지나온 자리를 남기는 시간. 방 밝기가 따라오는 시간도 겸한다. */
	constexpr float FootholdDwellSeconds = 1.6f;
	/** 잡히는 동안 찍는 때. */
	constexpr float ShotTimes[] = {0.5f, 1.0f, 1.45f};
	const FGameplayTag& StairKeyTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
			FName(TEXT("State.MissingFloor.HasStairKey")), false);
		return Tag;
	}
	static FVector Floor4(const FVector& Local)
	{
		return Local + FVector(0.0f, 0.0f, AIGPrologueWorldScene::FourthFloorZ);
	}
}

AIGCaptureVarietyProbe::AIGCaptureVarietyProbe()
{
	// 검사기는 매 프레임 포획의 단계와 시선, 화면 덮임을 잰다. -IGCaptureVarietyProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGCaptureVarietyProbe::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGListenerEntity* InListener)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGCaptureVarietyShots"));
}

void AIGCaptureVarietyProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("CAPTUREVARIETY_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGCaptureVarietyProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("CAPTUREVARIETY_PROBE %s failures=%d"),
		Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGCaptureVarietyProbe::Next(const int32 NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
	bActed = false;
	ShotIndex = 0;
}

void AIGCaptureVarietyProbe::Stand(const FVector& Feet, const FVector& LookAt)
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
}

void AIGCaptureVarietyProbe::Shoot(const TCHAR* Name)
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
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CaptureVarietyReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("CAPTUREVARIETY_SHOT %s"), *Path);
}

void AIGCaptureVarietyProbe::ShootDuringCapture(const TCHAR* Kind, const float Seconds)
{
	using namespace IGCaptureVarietyProbe;
	if (ShotIndex < static_cast<int32>(UE_ARRAY_COUNT(ShotTimes)) && Seconds >= ShotTimes[ShotIndex])
	{
		Shoot(*FString::Printf(TEXT("capture-%s-%d"), Kind, ShotIndex + 1));
		++ShotIndex;
	}
}

bool AIGCaptureVarietyProbe::IsMoveLocked() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	const APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	return PC && PC->IsMoveInputIgnored();
}

void AIGCaptureVarietyProbe::NoteWake(const TCHAR* Kind)
{
	const AIGPlayerCharacter* Character = Player.Get();
	const AIGNightLoopDirector* Loop = NightLoop.Get();
	WakeLocation = Character ? Character->GetActorLocation() : FVector::ZeroVector;
	bSawMoveLockAfterWake = IsMoveLocked();
	const FVector Spot = Loop ? Loop->GetLastCaptureSpot() : FVector::ZeroVector;
	UE_LOG(LogIndieGame, Display,
		TEXT("CAPTUREVARIETY_WAKE %s at=(%.0f,%.0f,%.0f) caught=(%.0f,%.0f,%.0f) lost=%.1f move_locked=%d"),
		Kind, WakeLocation.X, WakeLocation.Y, WakeLocation.Z, Spot.X, Spot.Y, Spot.Z,
		Loop ? Loop->GetLastLostSeconds() : -1.0f, bSawMoveLockAfterWake ? 1 : 0);
}

void AIGCaptureVarietyProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	using namespace IGCaptureVarietyProbe;
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 240.0f)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	AIGNightLoopDirector* Loop = NightLoop.Get();
	UGameInstance* GameInstance = GetGameInstance();
	UIGMissingFloorNarrativeSubsystem* Narrative = GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
	UIGStoryStateSubsystem* Story = GameInstance
		? GameInstance->GetSubsystem<UIGStoryStateSubsystem>() : nullptr;
	UIGFlashlightComponent* Torch = Character ? Character->GetFlashlight() : nullptr;
	const bool bInFlight = Loop && Loop->IsCaptureResetInFlight();
	if (bInFlight && Character)
	{
		// 잡히는 동안 화면은 기울지도 흔들리지도 않는다. 고개는 돌아가도 된다.
		const FRotator Feel = Character->GetCameraFeelRotation();
		if (FMath::Abs(Feel.Roll) > 0.01f || FMath::Abs(Feel.Yaw) > 0.01f)
		{
			Check(false, TEXT("capture_view_never_shakes"));
		}
	}

	if (Phase == 0)
	{
		if (PhaseSeconds < (bShots ? 12.0f : 1.0f))
		{
			return;
		}
		for (TActorIterator<AIGNightThreatDirector> It(GetWorld()); It; ++It) { Threats = *It; }
		for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It) { NightLoop = *It; }
		for (TActorIterator<AIGNightPhaseDirector> It(GetWorld()); It; ++It) { NightPhase = *It; }
		for (TActorIterator<AIGManagerPatrol> It(GetWorld()); It; ++It) { Manager = *It; }
		for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It) { NightThree = *It; }
		const bool bReady = Threats.IsValid() && NightLoop.IsValid() && NightPhase.IsValid() && Manager.IsValid()
			&& NightThree.IsValid() && Listener.IsValid() && Character && PC && Narrative && Story && Torch
			&& Scene.IsValid();
		Check(bReady, TEXT("stage_resolved"));
		Check(bReady && NightPhase->IsHourActive(), TEXT("hour_is_running"));
		if (!bReady)
		{
			Finish();
			return;
		}
		Listener->SetDormant(true);
		// 손전등은 실제로 챙긴 것처럼 소유 기록부터 남긴다. 떨어뜨렸다 주울 때 그 기록을 본다.
		Story->AddState(UIGFlashlightComponent::GetOwnershipTag());
		Torch->RefreshOwnership();
		Torch->SetOn(true);
		Narrative->SetNightIndex(2);
		Next(10);
		return;
	}

	switch (Phase)
	{
	// --- 1. 손님 -------------------------------------------------------------
	case 10:
		if (!bActed)
		{
			bActed = true;
			Stand(Floor4(HomeFeet), Floor4(HomeDoorFace));
		}
		else if (PhaseSeconds >= 1.5f)
		{
			HourBefore = NightPhase->GetHourElapsedSeconds();
			Threats->StartHomeGuestForProbe();
			Next(11);
		}
		break;
	case 11:
		if (!bActed && PhaseSeconds >= 2.0f)
		{
			bActed = true;
			if (AIGSwingDoor* Door = Scene->GetHomeDoor())
			{
				Door->BeginScriptedSwing(true, false);
			}
		}
		if (bInFlight)
		{
			CaptureStartedAt = GetWorld()->GetTimeSeconds();
			Next(12);
		}
		else if (PhaseSeconds > 6.0f)
		{
			Check(false, TEXT("guest_captures_at_open_door"));
			Next(20);
		}
		break;
	case 12:
		ShootDuringCapture(TEXT("guest"), PhaseSeconds);
		if (!bInFlight)
		{
			NoteWake(TEXT("guest"));
			Shoot(TEXT("wake-guest-1"));
			const FVector Entry = AIGNightLoopDirector::GetHomeEntryWake().GetLocation();
			Check(FVector::Dist2D(WakeLocation, Entry) <= 40.0f, TEXT("guest_wakes_in_home_entry"));
			Check(FVector::Dist(WakeLocation, Loop->GetWakeTransform().GetLocation()) > 150.0f,
				TEXT("guest_not_sent_to_bed"));
			const AIGDoorLatch* Latch = nullptr;
			for (TActorIterator<AIGDoorLatch> It(GetWorld()); It; ++It)
			{
				if (It->GetDoor() == Scene->GetHomeDoor()) { Latch = *It; }
			}
			Check(Latch && Latch->IsBroken(), TEXT("guest_tears_latch"));
			Check(Threats->GetFallenPaperCount() >= 3, TEXT("guest_scatters_papers_in_entry"));
			Check(Threats->IsGuestReturnOwed(), TEXT("guest_returns_that_night"));
			Check(FMath::IsNearlyEqual(Loop->GetLastLostSeconds(), 60.0f, 0.5f)
				&& NightPhase->GetHourElapsedSeconds() - HourBefore >= 60.0f, TEXT("guest_costs_a_minute"));
			Check(bSawMoveLockAfterWake, TEXT("guest_wakes_on_the_floor"));
			Next(13);
		}
		break;
	case 13:
		if (!bActed && PhaseSeconds >= 2.2f)
		{
			bActed = true;
			Shoot(TEXT("wake-guest-2"));
			Check(!IsMoveLocked(), TEXT("guest_walks_after_getting_up"));
			Check(Scene->GetHomeDoor() && Scene->GetHomeDoor()->IsFullyClosed(), TEXT("guest_door_slammed_shut"));
			Next(20);
		}
		break;

	// --- 2. 관리인 -----------------------------------------------------------
	case 20:
		if (!bActed)
		{
			bActed = true;
			Narrative->SetNightIndex(3);
			Story->AddState(StairKeyTag());
			NightThree->RefreshKeyringAvailability();
			Stand(Floor4(ManagerSpot), Floor4(ManagerSpot) + FVector(-300.0f, 0.0f, 150.0f));
		}
		else if (Manager->IsOnDuty() && PhaseSeconds >= 1.0f)
		{
			Check(NightThree->GetKeyring() && NightThree->GetKeyring()->IsHidden(), TEXT("manager_key_in_hand_first"));
			HourBefore = NightPhase->GetHourElapsedSeconds();
			Manager->BeginCatchForProbe();
			Next(21);
		}
		else if (PhaseSeconds > 5.0f)
		{
			Check(false, TEXT("manager_goes_on_duty"));
			Next(30);
		}
		break;
	case 21:
		if (bInFlight)
		{
			if (CaptureStartedAt < 0.0 || PhaseSeconds < 0.05f)
			{
				CaptureStartedAt = GetWorld()->GetTimeSeconds();
			}
			ShootDuringCapture(TEXT("manager"), PhaseSeconds);
			const FLinearColor Cut = Character->GetCaptureCutColour();
			if (PC->PlayerCameraManager && PC->PlayerCameraManager->FadeAmount > 0.3f && Cut.R > 0.9f)
			{
				bSawGlare = true;
			}
			bActed = true;
		}
		else if (bActed)
		{
			NoteWake(TEXT("manager"));
			Shoot(TEXT("wake-manager-1"));
			const FVector Entry = AIGNightLoopDirector::GetHomeEntryWake().GetLocation();
			Check(FVector::Dist2D(WakeLocation, Entry) <= 40.0f, TEXT("manager_brings_her_home"));
			Check(!bSawMoveLockAfterWake, TEXT("manager_wakes_standing"));
			Check(bSawGlare, TEXT("manager_glare_not_black"));
			Check(!Story->HasState(StairKeyTag()), TEXT("manager_takes_stair_key"));
			Check(NightThree->GetKeyring() && !NightThree->GetKeyring()->IsHidden(), TEXT("manager_keyring_back_in_booth"));
			Check(FMath::IsNearlyEqual(Loop->GetLastLostSeconds(), 180.0f, 0.5f)
				&& NightPhase->GetHourElapsedSeconds() - HourBefore >= 180.0f, TEXT("manager_costs_three_minutes"));
			Next(22);
		}
		else if (PhaseSeconds > 4.0f)
		{
			Check(false, TEXT("manager_captures"));
			Next(30);
		}
		break;
	case 22:
		if (PhaseSeconds >= 2.6f)
		{
			Shoot(TEXT("wake-manager-2"));
			// 남은 검사 동안 돌아다니지 않게 세운다.
			Manager->SetActorTickEnabled(false);
			Next(30);
		}
		break;

	// --- 3. 어둑시니 ----------------------------------------------------------
	case 30:
		if (!bActed)
		{
			bActed = true;
			for (int32 Index = 0; Index < Scene->GetCorridorFixtureCount(); ++Index)
			{
				Scene->SetFixtureLive(Index, false, true);
			}
			ProbeLamp = NewObject<UPointLightComponent>(this, TEXT("ProbeLamp"));
			ProbeLamp->SetMobility(EComponentMobility::Movable);
			ProbeLamp->SetIntensity(6000.0f);
			ProbeLamp->SetAttenuationRadius(420.0f);
			ProbeLamp->SetCastShadows(false);
			ProbeLamp->RegisterComponent();
			ProbeLamp->SetWorldLocation(Floor4(LitSpot) + FVector(0.0f, 0.0f, 220.0f));
			Stand(Floor4(LitSpot), Floor4(LitSpot) + FVector(300.0f, 0.0f, 150.0f));
		}
		else if (PhaseSeconds >= FootholdDwellSeconds)
		{
			UE_LOG(LogIndieGame, Display, TEXT("CAPTUREVARIETY_ROOM lit=%.2f"), Character->GetRoomDarkness());
			ProbeLamp->SetVisibility(false);
			Next(31);
		}
		break;
	case 31:
		if (!bActed)
		{
			bActed = true;
			Stand(Floor4(DarkSpot), Floor4(DarkSpot) + FVector(300.0f, 0.0f, 150.0f));
		}
		else if (PhaseSeconds >= FootholdDwellSeconds)
		{
			UE_LOG(LogIndieGame, Display, TEXT("CAPTUREVARIETY_ROOM dark=%.2f"), Character->GetRoomDarkness());
			Torch->RefillBattery(1.0f);
			Torch->SetOn(true);
			BatteryBefore = Torch->GetBatteryFraction();
			HourBefore = NightPhase->GetHourElapsedSeconds();
			MaxCapturePitch = -90.0f;
			Threats->BeginEoduksiniCaptureForProbe(Floor4(DarkSpot) + FVector(150.0f, 0.0f, 0.0f));
			Next(32);
		}
		break;
	case 32:
		if (bInFlight)
		{
			ShootDuringCapture(TEXT("eoduksini"), PhaseSeconds);
			MaxCapturePitch = FMath::Max(MaxCapturePitch, FRotator::NormalizeAxis(PC->GetControlRotation().Pitch));
			bActed = true;
		}
		else if (bActed)
		{
			NoteWake(TEXT("eoduksini"));
			Shoot(TEXT("wake-eoduksini-1"));
			Check(FVector::Dist2D(WakeLocation, Floor4(LitSpot)) <= 60.0f, TEXT("eoduksini_wakes_under_light"));
			Check(MaxCapturePitch >= 25.0f, TEXT("eoduksini_forces_look_up"));
			UE_LOG(LogIndieGame, Display, TEXT("CAPTUREVARIETY_EODUKSINI pitch=%.1f battery=%.2f->%.2f"),
				MaxCapturePitch, BatteryBefore, Torch->GetBatteryFraction());
			Check(Torch->GetBatteryFraction() <= BatteryBefore - 0.3f, TEXT("eoduksini_drains_battery"));
			Check(FMath::IsNearlyEqual(Loop->GetLastLostSeconds(), 90.0f, 0.5f)
				&& NightPhase->GetHourElapsedSeconds() - HourBefore >= 90.0f, TEXT("eoduksini_costs_ninety_seconds"));
			Check(!Threats->IsEoduksiniManifested(), TEXT("eoduksini_gone_when_she_wakes"));
			Check(bSawMoveLockAfterWake, TEXT("eoduksini_wakes_on_the_floor"));
			Next(33);
		}
		else if (PhaseSeconds > 4.0f)
		{
			Check(false, TEXT("eoduksini_captures"));
			Next(40);
		}
		break;
	case 33:
		if (PhaseSeconds >= 2.4f)
		{
			Shoot(TEXT("wake-eoduksini-2"));
			for (int32 Index = 0; Index < Scene->GetCorridorFixtureCount(); ++Index)
			{
				Scene->SetFixtureLive(Index, true, true);
			}
			Next(40);
		}
		break;

	// --- 4. 위층 사람 ---------------------------------------------------------
	case 40:
	{
		// 서쪽에서 동쪽으로 걸어온 길을 남긴다. 셋째 자리에서 잡힌 자리까지는 2.2m, 둘째
		// 자리까지는 4.2m다. 3m 넘게 끌려가면 둘째 자리에서 깬다.
		const FVector Path[] = {PathA, PathB, PathC};
		const int32 Step = FMath::FloorToInt(PhaseSeconds / FootholdDwellSeconds);
		if (Step < 3)
		{
			if (!bActed || Step != ShotIndex)
			{
				bActed = true;
				ShotIndex = Step;
				Stand(Floor4(Path[Step]), Floor4(Path[Step]) + FVector(300.0f, 0.0f, 150.0f));
			}
			break;
		}
		Stand(Floor4(CaptureSpot), Floor4(CaptureSpot) + FVector(300.0f, 0.0f, 120.0f));
		Torch->RefillBattery(1.0f);
		Torch->SetOn(true);
		HourBefore = NightPhase->GetHourElapsedSeconds();
		TierBefore = Listener->GetAggressionTier();
		Listener->SetDormant(false);
		Listener->TeleportTo(Floor4(CaptureSpot) + FVector(90.0f, 0.0f, 60.0f), FRotator(0.0f, 180.0f, 0.0f), false, true);
		Listener->CaptureForProbe(Character);
		Next(41);
		break;
	}
	case 41:
		if (bInFlight)
		{
			ShootDuringCapture(TEXT("listener"), PhaseSeconds);
			bActed = true;
		}
		else if (bActed)
		{
			NoteWake(TEXT("listener"));
			Shoot(TEXT("wake-listener-1"));
			Check(FVector::Dist2D(WakeLocation, Floor4(CaptureSpot)) >= 290.0f, TEXT("listener_drags_her_back"));
			Check(FVector::Dist2D(WakeLocation, Floor4(PathB)) <= 60.0f, TEXT("listener_wakes_on_her_own_path"));
			Check(FMath::Abs(FRotator::NormalizeAxis(PC->GetControlRotation().Yaw)) <= 30.0f,
				TEXT("listener_wakes_facing_where_she_was_caught"));
			const AIGDroppedFlashlight* Dropped = Loop->GetDroppedFlashlight();
			Check(Dropped && Torch->IsDropped() && !Torch->IsAvailable(), TEXT("listener_torch_dropped"));
			Check(Dropped && Dropped->IsLit()
				&& FVector::Dist2D(Dropped->GetActorLocation(), Floor4(CaptureSpot)) <= 80.0f,
				TEXT("listener_torch_lit_where_caught"));
			Check(Listener->GetAggressionTier() == TierBefore + 1, TEXT("listener_raises_tier"));
			Check(FMath::IsNearlyEqual(Loop->GetLastLostSeconds(), 120.0f, 0.5f)
				&& NightPhase->GetHourElapsedSeconds() - HourBefore >= 120.0f, TEXT("listener_costs_two_minutes"));
			Check(bSawMoveLockAfterWake, TEXT("listener_wakes_on_the_floor"));
			Check(Listener->IsInCaptureGrace(), TEXT("listener_grace_after_wake"));
			Next(42);
		}
		else if (PhaseSeconds > 4.0f)
		{
			Check(false, TEXT("listener_captures"));
			Finish();
		}
		break;
	case 42:
		if (!bActed && PhaseSeconds >= 2.0f)
		{
			bActed = true;
			Shoot(TEXT("wake-listener-2"));
			Check(!IsMoveLocked(), TEXT("listener_walks_after_getting_up"));
			if (UIGNoiseSubsystem* Noise = GetWorld()->GetSubsystem<UIGNoiseSubsystem>())
			{
				Noise->ReportNoise(Character->GetActorLocation(), 1.0f, Character);
			}
		}
		else if (bActed && PhaseSeconds >= 3.2f)
		{
			const EIGListenerState State = Listener->GetListenerState();
			Check(State != EIGListenerState::Investigating && State != EIGListenerState::Chasing
				&& State != EIGListenerState::Holding, TEXT("listener_grace_ignores_loud_noise"));
			Next(43);
		}
		break;
	case 43:
	{
		AIGDroppedFlashlight* Dropped = Loop->GetDroppedFlashlight();
		if (!Dropped)
		{
			Check(false, TEXT("dropped_torch_still_there"));
			Finish();
			break;
		}
		if (!bActed)
		{
			bActed = true;
			Listener->SetDormant(true);
			const FVector At = Dropped->GetActorLocation();
			Stand(FVector(At.X - 75.0f, At.Y, Floor4(FVector::ZeroVector).Z), At);
			Shoot(TEXT("dropped-torch"));
		}
		else if (PhaseSeconds >= 0.4f)
		{
			UIGInteractionComponent* Interaction = Character->FindComponentByClass<UIGInteractionComponent>();
			if (Interaction)
			{
				Interaction->SetInteractionInputEnabled(true);
				Interaction->RefreshFocus();
			}
			const bool bFocused = Interaction && Interaction->GetFocusedActor() == Dropped;
			Check(bFocused, TEXT("dropped_torch_takes_focus"));
			if (bFocused)
			{
				// 줍기는 0.25초 쥐어야 한다. 바로 떼면 취소된다.
				Interaction->PressInteraction();
			}
			Next(44);
		}
		break;
	}
	case 44:
		if (!bActed && PhaseSeconds >= 0.5f)
		{
			bActed = true;
			if (UIGInteractionComponent* Interaction = Character->FindComponentByClass<UIGInteractionComponent>())
			{
				Interaction->ReleaseInteraction();
			}
		}
		else if (bActed && PhaseSeconds >= 1.0f)
		{
			Check(Torch->IsAvailable() && !Torch->IsDropped() && Torch->IsOn() && !Loop->GetDroppedFlashlight(),
				TEXT("picked_up_torch_is_back_and_lit"));
			Finish();
		}
		break;
	default:
		break;
	}
}
