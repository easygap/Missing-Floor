#include "Sequence/IGGameplayRealismProbe.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGNoiseSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "InputKeyEventArgs.h"
#include "Interaction/IGReadableNote.h"
#include "Interaction/IGSwingDoor.h"
#include "Materials/MaterialInterface.h"
#include "Player/IGInteractionComponent.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"
#include "Player/IGHudGuidance.h"

AIGGameplayRealismProbe::AIGGameplayRealismProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

UBoxComponent* AIGGameplayRealismProbe::AddBlock(const FVector& Location, const FVector& Extent)
{
	UBoxComponent* Block = NewObject<UBoxComponent>(this);
	Block->SetBoxExtent(Extent);
	Block->SetCollisionProfileName(TEXT("BlockAll"));
	Block->SetCanEverAffectNavigation(false);
	Block->RegisterComponent();
	Block->SetWorldLocation(Location);
	return Block;
}

void AIGGameplayRealismProbe::SendKey(const FKey& Key, const bool bPressed)
{
	const FInputKeyEventArgs Event(nullptr, FInputDeviceId::CreateFromInternalId(0), Key,
		bPressed ? IE_Pressed : IE_Released, bPressed ? 1.0f : 0.0f, false, FPlatformTime::Cycles64());
	Controller->InputKey(Event);
}

void AIGGameplayRealismProbe::SendMouseY(const float Delta)
{
	// 실제 마우스와 같은 축 이벤트다. 축 매핑 배율, LookUp, 레거시 입력 배율 설정을
	// 모두 거쳐야 위아래가 뒤집힌 것을 잡는다.
	const FInputKeyEventArgs Event(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::MouseY,
		Delta, 1.0f / 60.0f, 1, FPlatformTime::Cycles64());
	Controller->InputKey(Event);
}

void AIGGameplayRealismProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("REALISM_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGGameplayRealismProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Seconds += DeltaSeconds;
	if (Phase == 0)
	{
		Controller = GetWorld()->GetFirstPlayerController();
		Player = Controller.IsValid() ? Cast<AIGPlayerCharacter>(Controller->GetPawn()) : nullptr;
		if (!Player.IsValid())
		{
			if (Seconds > 15.0f) { Check(false, TEXT("player_spawn")); FPlatformMisc::RequestExitWithStatus(false, 1); }
			return;
		}
		AddBlock(FVector(0, 0, -12), FVector(3000, 3000, 12));
		Player->SetActorLocation(FVector(0, 0, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
		Controller->SetControlRotation(FRotator::ZeroRotator);
		Controller->ResetIgnoreMoveInput();
		Controller->ResetIgnoreLookInput();
		Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Player->SetCameraMotionEnabled(false);
		Phase = 20; Seconds = 0;
	}
	else if (Phase == 20 && Seconds > 0.5f)
	{
		// 마우스를 위로 밀면 위를 본다. 0.2.4까지 축 매핑이 -1이라 반대로 돌았다.
		Controller->SetControlRotation(FRotator::ZeroRotator);
		SendMouseY(40.0f);
		Phase = 21; Seconds = 0;
	}
	else if (Phase == 21 && Seconds > 0.15f)
	{
		PitchAfterMouseUp = FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch);
		UE_LOG(LogIndieGame, Display, TEXT("REALISM_LOOK mouse_up_pitch=%.3f"), PitchAfterMouseUp);
		Check(PitchAfterMouseUp > 0.5f, TEXT("mouse_up_looks_up"));
		SendMouseY(-80.0f);
		Phase = 22; Seconds = 0;
	}
	else if (Phase == 22 && Seconds > 0.15f)
	{
		const float Pitch = FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch);
		UE_LOG(LogIndieGame, Display, TEXT("REALISM_LOOK mouse_down_pitch=%.3f"), Pitch);
		Check(Pitch < PitchAfterMouseUp - 0.5f && Pitch < 0.0f, TEXT("mouse_down_looks_down"));
		Controller->SetControlRotation(FRotator::ZeroRotator);
		Phase = 1; Seconds = 0;
	}
	else if (Phase == 1 && Seconds > 0.5f)
	{
		SendKey(EKeys::LeftShift, true); SendKey(EKeys::W, true);
		Phase = 2; Seconds = 0;
	}
	else if (Phase == 2 && Seconds > 1.3f)
	{
		Check(Player->GetVelocity().Size2D() > 390.0f, TEXT("sprint_key_reaches_speed"));
		BrakeStart = Player->GetActorLocation();
		SendKey(EKeys::W, false);
		Phase = 3; Seconds = 0;
	}
	else if (Phase == 3 && (Player->GetVelocity().Size2D() < 1.0f || Seconds > 0.8f))
	{
		const float Distance = FVector::Dist2D(BrakeStart, Player->GetActorLocation());
		UE_LOG(LogIndieGame, Display, TEXT("REALISM_BRAKE distance_cm=%.2f seconds=%.3f"), Distance, Seconds);
		Check(Distance > 5.0f && Distance < 75.0f && Seconds < 0.45f, TEXT("sprint_release_stops_before_door_width"));
		SendKey(EKeys::LeftShift, false);
		Phase = 4; Seconds = 0;
	}
	else if (Phase == 4 && Seconds > 0.2f)
	{
		// 86cm 문을 중심에서 10cm 비껴 진입한다. 실제 이동으로 문틀 끼임을 확인한다.
		Player->SetActorLocation(FVector(0, 10, 98));
		Player->GetCharacterMovement()->StopMovementImmediately();
		Controller->SetControlRotation(FRotator::ZeroRotator);
		AddBlock(FVector(160, 143, 110), FVector(8, 100, 110));
		AddBlock(FVector(160, -143, 110), FVector(8, 100, 110));
		SendKey(EKeys::W, true);
		Phase = 5; Seconds = 0;
	}
	else if (Phase == 5 && Seconds > 1.6f)
	{
		SendKey(EKeys::W, false);
		Check(Player->GetActorLocation().X > 230.f, TEXT("offset_doorway_passage"));
		Player->GetCharacterMovement()->StopMovementImmediately();
		Phase = 6; Seconds = 0;
	}
	else if (Phase == 6 && Seconds > 0.5f)
	{
		StandingEyeHeight = Player->FindComponentByClass<UCameraComponent>()->GetComponentLocation().Z;
		UE_LOG(LogIndieGame, Display, TEXT("REALISM_VIEW standing eye=%.2f actor=%.2f relative=%.2f"), StandingEyeHeight, Player->GetActorLocation().Z, Player->GetFirstPersonCamera()->GetRelativeLocation().Z);
		Player->Crouch();
		Phase = 7; Seconds = 0;
	}
	else if (Phase == 7 && Seconds > 0.65f)
	{
		const float EyeDrop = StandingEyeHeight - Player->FindComponentByClass<UCameraComponent>()->GetComponentLocation().Z;
		UE_LOG(LogIndieGame, Display, TEXT("REALISM_VIEW crouched=%d eye_drop_cm=%.2f"), Player->bIsCrouched, EyeDrop);
		UE_LOG(LogIndieGame, Display, TEXT("REALISM_VIEW crouching actor=%.2f relative=%.2f"), Player->GetActorLocation().Z, Player->GetFirstPersonCamera()->GetRelativeLocation().Z);
		Check(Player->bIsCrouched && FMath::IsNearlyEqual(EyeDrop, 48.f, 1.f),
			TEXT("crouch_lowers_view_with_camera_motion_disabled"));
		Player->UnCrouch();
		Phase = 8; Seconds = 0;
	}
	else if (Phase == 8 && Seconds > 0.65f)
	{
		UE_LOG(LogIndieGame, Display, TEXT("REALISM_VIEW restored actor=%.2f relative=%.2f crouched=%d"), Player->GetActorLocation().Z, Player->GetFirstPersonCamera()->GetRelativeLocation().Z, Player->bIsCrouched);
		Check(!Player->bIsCrouched && FMath::IsNearlyEqual(Player->FindComponentByClass<UCameraComponent>()->GetComponentLocation().Z, StandingEyeHeight, 1.f),
			TEXT("uncrouch_restores_view_with_camera_motion_disabled"));
		CheckDoorRoundtrip();
		CheckPresentationTiming();
		CheckInteractionsAndCapture();
		Phase = 30; Seconds = 0;
	}
	else if (Phase == 30 && Seconds > 1.0f)
	{
		// 앞 검사의 문소리 링이 끝난 뒤에 본다. 걷기(0.15)는 발소리로만 듣고, 노크(0.3)부터
		// 링이 생긴다. 걷는 내내 깜빡이던 원호가 화면 결함으로 읽혔다(2026-10-01).
		AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD());
		UIGNoiseSubsystem* Noise = GetWorld()->GetSubsystem<UIGNoiseSubsystem>();
		Check(Hud && Noise && !Hud->IsNoiseRippleActive(), TEXT("ripple_idle_before_noise"));
		if (Hud && Noise)
		{
			const FVector At = Player->GetActorLocation();
			// 마스킹이 섞이면 값이 흔들리므로 HUD의 기준만 본다.
			Noise->ReportNoiseUnmasked(At, 0.15f, Player.Get());
			Check(!Hud->IsNoiseRippleActive(), TEXT("walking_draws_no_ripple"));
			Noise->ReportNoiseUnmasked(At, AIGPlayerCharacter::KnockLoudness, Player.Get());
			Check(Hud->IsNoiseRippleActive(), TEXT("knock_draws_ripple"));
		}
		UE_LOG(LogIndieGame, Display, TEXT("REALISM_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
		Phase = 9;
		FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
	}
}

void AIGGameplayRealismProbe::CheckDoorRoundtrip()
{
	UIGInteractionComponent* Interaction = Player->FindComponentByClass<UIGInteractionComponent>();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AIGSwingDoor* Door = GetWorld()->SpawnActor<AIGSwingDoor>(FVector(700, 600, 0), FRotator::ZeroRotator, Spawn);
	Door->ConfigurePrototypeVisuals(
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")),
		LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/M_Stucco_X.M_Stucco_X")),
		LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/M_Stucco_X.M_Stucco_X")),
		FVector(5, 86, 200));
	UStaticMeshComponent* Leaf = Door->FindComponentByClass<UStaticMeshComponent>();
	auto AimAtLeaf = [&]()
	{
		const FVector Normal = Door->GetDoorPivot()->GetComponentRotation().RotateVector(FVector(-1, 0, 0));
		const FVector Target = Leaf->GetComponentLocation() + FVector(0, 0, 55);
		FVector Stand = Target + Normal * 125.f;
		Stand.Z = 98.f;
		Player->SetActorLocation(Stand);
		Player->GetCharacterMovement()->StopMovementImmediately();
		Controller->SetControlRotation((Target - Player->GetPawnViewLocation()).Rotation());
		Controller->PlayerCameraManager->UpdateCamera(0.f);
		Interaction->RefreshFocus();
	};
	auto TapAndFinish = [&]()
	{
		// 상호작용 대상을 직접 호출하면 초점 판정이 꺼지는 결함을 놓친다.
		Interaction->PressInteraction();
		Interaction->ReleaseInteraction();
		for (int32 Frame = 0; Frame < 130; ++Frame)
		{
			if (Door->IsActorTickEnabled()) { Door->Tick(1.f / 60.f); }
		}
	};
	Interaction->SetInteractionInputEnabled(true);
	Door->ForceOpenState(false);
	AimAtLeaf();
	Check(Interaction->GetFocusedActor() == Door, TEXT("closed_door_can_be_focused"));
	TapAndFinish();
	Check(Door->IsOpen(), TEXT("door_opens_from_interaction_input"));
	AimAtLeaf();
	Check(Interaction->GetFocusedActor() == Door, TEXT("open_door_can_be_focused"));
	Check(Leaf->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore,
		TEXT("open_door_does_not_snag_player"));
	TapAndFinish();
	Check(!Door->IsOpen(), TEXT("open_door_closes_from_interaction_input"));
	Check(Leaf->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block
		&& Leaf->GetCollisionEnabled() != ECollisionEnabled::NoCollision,
		TEXT("closed_door_restores_blocking_collision"));
	// 닫히는 문에 선 플레이어를 밀거나 가두지 않아야 한다.
	Door->ForceOpenState(true);
	Door->BeginScriptedSwing(false, false, true);
	Player->SetActorLocation(FVector(700, 643, 98));
	for (int32 Frame = 0; Frame < 130; ++Frame)
	{
		if (Door->IsActorTickEnabled()) { Door->Tick(1.f / 60.f); }
	}
	Check(Door->IsOpen() && Leaf->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore,
		TEXT("closing_door_reopens_in_occupied_doorway"));
	Door->Destroy();
	Interaction->CancelInteraction();
	Interaction->RefreshFocus();
}

void AIGGameplayRealismProbe::CheckPresentationTiming()
{
	UIGStressComponent* Stress = Player->FindComponentByClass<UIGStressComponent>();
	UIGAccessibilitySubsystem* Accessibility = GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>();
	const FIGAccessibilitySettings Original = Accessibility->GetSettings();
	FIGAccessibilitySettings Settings = Original;
	Settings.bReducedCameraMotion = false;
	Settings.bHeartbeatWarning = true;
	Accessibility->ApplySettings(Settings);
	Player->GetCharacterMovement()->StopMovementImmediately();
	Player->SetCameraMotionEnabled(true);
	// 오래 플레이한 뒤 긴장이 바뀌어도 호흡에 맞춰 천천히 움직여야 한다.
	for (const int32 Rate : {30, 60, 120})
	{
		const float Step = 1.f / Rate;
		Stress->Stress = .5f;
		for (int32 Frame = 0; Frame < Rate * 900; ++Frame) { Player->UpdateCameraMotion(Step); }
		float PreviousZ = Player->GetFirstPersonCamera()->GetRelativeLocation().Z;
		float MaxSpeed = 0.f;
		for (int32 Frame = 0; Frame < Rate * 10; ++Frame)
		{
			Stress->Stress = FMath::Lerp(.5f, 1.f, Frame / (Rate * 10.f));
			Player->UpdateCameraMotion(Step);
			const float Z = Player->GetFirstPersonCamera()->GetRelativeLocation().Z;
			MaxSpeed = FMath::Max(MaxSpeed, FMath::Abs(Z - PreviousZ) / Step);
			PreviousZ = Z;
		}
		UE_LOG(LogIndieGame, Display, TEXT("REALISM_BREATH rate=%d max_cm_per_second=%.3f"), Rate, MaxSpeed);
		Check(MaxSpeed < 4.f, TEXT("long_session_breath_has_no_camera_jumps"));
	}
	Stress->Stress = 1.f;
	Stress->BeatPhase = .5f;
	Check(FMath::IsNearlyEqual(Stress->GetHeartbeatWarningScale(), 1.4f, .005f),
		TEXT("heartbeat_warning_follows_audio_phase"));
	Stress->SuppressHeartbeat(1.f, false);
	Check(FMath::IsNearlyEqual(Stress->GetHeartbeatWarningScale(), 1.f),
		TEXT("silenced_heartbeat_does_not_flash_warning"));
	Stress->HeartbeatSuppressionRemaining = 0.f;
	Stress->BeatPhase = 0.f;
	Stress->Stress = 0.f;
	Player->TraveledDistanceAccum = 0.f;
	Player->LastStepIndex = 0;
	// 걷기 속도(175 cm/s)와 걷기 보폭(91 cm)이면 0.52초에 한 걸음이다.
	Player->GetCharacterMovement()->Velocity = FVector(175, 0, 0);
	for (int32 Frame = 0; Frame < 312; ++Frame) { Player->UpdateFootsteps(1.f / 60.f); }
	UE_LOG(LogIndieGame, Display, TEXT("REALISM_FOOTSTEPS walk_seconds=5.2 steps=%d"), Player->LastStepIndex);
	Check(Player->LastStepIndex >= 9 && Player->LastStepIndex <= 11,
		TEXT("walking_footsteps_keep_half_second_cadence"));
	Player->GetCharacterMovement()->StopMovementImmediately();
	Player->SetCameraMotionEnabled(false);
	UIGFlashlightComponent* Flashlight = Player->FindComponentByClass<UIGFlashlightComponent>();
	const FRotator OriginalFlashlightRotation = Flashlight->GetRelativeRotation();
	for (const int32 Rate : {30, 60, 120})
	{
		Flashlight->PreviousWorldRotation = FRotator::ZeroRotator;
		Flashlight->SwayOffset = FRotator::ZeroRotator;
		Flashlight->ImpulseOffset = FRotator::ZeroRotator;
		for (int32 Frame = 1; Frame <= Rate; ++Frame)
		{
			Flashlight->SetWorldRotation(FRotator(30.f * Frame / Rate, 90.f * Frame / Rate, 0.f));
			Flashlight->UpdateSway(1.f / Rate);
		}
		UE_LOG(LogIndieGame, Display, TEXT("REALISM_FLASHLIGHT rate=%d yaw=%.3f pitch=%.3f"),
			Rate, Flashlight->SwayOffset.Yaw, Flashlight->SwayOffset.Pitch);
		Check(FMath::IsNearlyEqual(Flashlight->SwayOffset.Yaw, -2.1f, .03f)
			&& FMath::IsNearlyEqual(Flashlight->SwayOffset.Pitch, -.7f, .03f),
			TEXT("flashlight_turn_lag_matches_across_frame_rates"));
	}
	Settings.bReducedCameraMotion = true;
	Accessibility->ApplySettings(Settings);
	for (int32 Frame = 0; Frame < 120; ++Frame) { Flashlight->UpdateSway(1.f / 60.f); }
	Check(Flashlight->SwayOffset.IsNearlyZero(.01f), TEXT("reduced_motion_settles_flashlight"));
	Flashlight->SetRelativeRotation(OriginalFlashlightRotation);
	Flashlight->PreviousWorldRotation = Flashlight->GetComponentRotation();
	Accessibility->ApplySettings(Original);
}

void AIGGameplayRealismProbe::CheckInteractionsAndCapture()
{
	FIGHudGuidance Guide;
	Guide.Update(0, TEXT("짐을 푼다"), true);
	Check(Guide.ObjectiveAlpha() == 1 && Guide.ControlsAlpha() == 1, TEXT("guide_first_arrival_visible"));
	Guide.Update(8, TEXT("짐을 푼다"), true);
	Check(Guide.ObjectiveAlpha() == 0 && Guide.ControlsAlpha() == 1, TEXT("guide_objective_expires_first"));
	Guide.Update(13, TEXT("짐을 푼다"), true);
	Check(Guide.ControlsAlpha() == 0, TEXT("guide_tutorial_expires"));
	Guide.Update(0, TEXT("관리실에 간다"), false);
	Check(Guide.ObjectiveAlpha() == 1 && Guide.ControlsAlpha() == 0, TEXT("guide_new_goal_without_tutorial_repeat"));
	Guide.ToggleRecall();
	Guide.Update(9.8f, TEXT("관리실에 간다"), false);
	Check(Guide.ControlsAlpha() > 0 && Guide.ControlsAlpha() < 1, TEXT("guide_recall_fades"));
	Guide.Update(.3f, TEXT("관리실에 간다"), false);
	Check(Guide.ControlsAlpha() == 0 && Guide.ObjectiveAlpha() == 0, TEXT("guide_recall_expires"));
	Guide.ToggleRecall(); Guide.ToggleRecall();
	Check(Guide.ControlsAlpha() == 0, TEXT("guide_second_press_closes"));
	Guide.ToggleRecall(); Guide.Interrupt();
	Check(Guide.ControlsAlpha() == 0 && Guide.ObjectiveAlpha() == 0, TEXT("guide_capture_interrupts"));
	Player->GetCharacterMovement()->StopMovementImmediately();
	Player->GetCharacterMovement()->DisableMovement();
	Player->SetActorLocation(FVector(0, 0, 90));
	Controller->SetControlRotation(FRotator::ZeroRotator);
	Controller->PlayerCameraManager->UpdateCamera(0.0f);
	FVector Eye; FRotator View;
	Controller->GetPlayerViewPoint(Eye, View);
	Check(AIGHorrorHUD::MakeSoundBearingTag(this, Eye + FVector(75, 0, -50)).IsEmpty(),
		TEXT("nearby_door_knock_is_not_below_floor"));
	Check(AIGHorrorHUD::MakeSoundBearingTag(this, FVector(100, 0, 0)).IsEmpty(),
		TEXT("same_floor_footsteps_are_not_below_floor"));
	Check(AIGHorrorHUD::MakeSoundBearingTag(this, Eye + FVector(60, 0, 160)).ToString() == TEXT("위"),
		TEXT("upstairs_sound_keeps_above_caption"));
	Check(AIGHorrorHUD::MakeSoundBearingTag(this, Eye + FVector(60, 0, -330)).ToString() == TEXT("아래"),
		TEXT("downstairs_sound_keeps_below_caption"));
	Check(AIGHorrorHUD::MakeSoundBearingTag(this, Eye + FVector(-150, 0, -50)).ToString() == TEXT("뒤"),
		TEXT("same_floor_sound_behind_keeps_direction"));
	UIGInteractionComponent* Interaction = Player->FindComponentByClass<UIGInteractionComponent>();
	Check(Interaction != nullptr, TEXT("interaction_component"));
	if (!Interaction) { return; }
	Interaction->SetInteractionInputEnabled(true);
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AIGReadableNote* Note = GetWorld()->SpawnActor<AIGReadableNote>(Eye + FVector(130, 8, 0), FRotator::ZeroRotator, Spawn);
	Note->ConfigurePrototypeVisuals(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")),
		LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/M_Stucco_X.M_Stucco_X")),
		FVector(4, 4, 10));
	Interaction->RefreshFocus();
	Check(Interaction->GetFocusedActor() == Note, TEXT("small_visible_prop_focus"));
	// 구체 스캔은 모서리를 스치지만 눈과 물체 사이를 막는 얇은 문틀.
	UBoxComponent* Occluder = AddBlock(Eye + FVector(110, 7, 0), FVector(2, 3, 15));
	Interaction->RefreshFocus();
	Check(Interaction->GetFocusedActor() != Note, TEXT("occluded_prop_rejected"));
	Occluder->DestroyComponent(); Note->Destroy();
	Interaction->RefreshFocus();
	AIGListenerEntity* Entity = GetWorld()->SpawnActor<AIGListenerEntity>(FVector(90, 0, 58), FRotator(0, 180, 0), Spawn);
	Entity->SetActorTickEnabled(false);
	Entity->SetDifficultyForTesting(EIGNightDifficulty::Standard);
	Entity->ParkForBeat(FVector(90, 0, 58), 180.0f);
	UBoxComponent* Door = AddBlock(FVector(45, 0, 110), FVector(4, 60, 110));
	Entity->Tick(0.016f);
	Check(Entity->GetListenerState() != EIGListenerState::CaptureHold, TEXT("closed_door_blocks_capture"));
	Door->DestroyComponent();
	Entity->Tick(0.016f);
	Check(Entity->GetListenerState() == EIGListenerState::CaptureHold, TEXT("open_door_allows_capture"));
	Check(!Entity->IsHidden(), TEXT("capture_keeps_physical_body_visible"));
	USkeletalMeshComponent* Body = Entity->FindComponentByClass<USkeletalMeshComponent>();
	Check(Body && Body->GetSkeletalMeshAsset() && Body->GetNumLODs() == 4, TEXT("runtime_character_has_four_lods"));
	if (Body)
	{
		for (int32 Frame = 0; Frame < 48; ++Frame)
		{
			Body->TickAnimation(1.0f / 60.0f, false);
			Body->RefreshBoneTransforms();
			Entity->Tick(1.0f / 60.0f);
		}
		// 잡힌 사람은 뒤로 넘어져 바닥 가까이에서 올려다본다. 얼굴은 넘어진 눈 앞에서
		// 물러났다가 달려든다. 서 있던 눈높이가 아니라 넘어진 눈을 기준으로 잰다.
		const FVector Back = (Player->GetActorLocation() - Entity->GetActorLocation()).GetSafeNormal2D();
		const FVector FallenEye = Player->GetPawnViewLocation()
			+ Back * AIGPlayerCharacter::CaptureFallBackCentimeters
			- FVector(0, 0, AIGPlayerCharacter::CaptureFallDropCentimeters);
		const FVector FaceFromEye = Entity->GetCaptureFaceLocation() - FallenEye;
		Check(FaceFromEye.Size() > 15.0f && FaceFromEye.Size() < 90.0f
			&& FVector::DotProduct(FaceFromEye.GetSafeNormal2D(), -Back) > 0.5f
			&& FMath::Abs(FaceFromEye.Z) < 45.0f, TEXT("capture_face_stays_in_front_of_camera"));
		Entity->SetDormant(true);
		Entity->KeepCaptureVisible();
		Check(!Entity->IsHidden() && Entity->IsActorTickEnabled()
			&& Entity->GetListenerState() == EIGListenerState::CaptureHold,
			TEXT("failure_ending_keeps_capture_pose"));
	}
	Entity->Destroy();
}
