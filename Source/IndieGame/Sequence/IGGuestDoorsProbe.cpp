#include "Sequence/IGGuestDoorsProbe.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGDoorLatch.h"
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

namespace IGGuestDoorsProbe
{
	/** 401·402호 문 앞(복도 쪽)과 403호 문 앞, 노크 높이는 보지 않는다. */
	const FVector Door401Front(-150.0f, -245.0f, 0.0f);
	const FVector Door402Front(-30.0f, -245.0f, 0.0f);
	const FVector Door403Front(131.0f, -233.0f, 0.0f);
	/** 5층 철문 문짝 가운데의 옥상 쪽과 문간. */
	const FVector AnnexKnockFront(129.0f, 444.5f, 0.0f);
	const FVector AnnexThresholdFront(129.0f, 407.5f, 0.0f);
	/** 403호 방 안, 현관을 보는 자리와 문간. 4층 바닥 기준. */
	const FVector HomeFeet(0.0f, -60.0f, 0.0f);
	const FVector HomeDoorwayFeet(131.0f, -205.0f, 0.0f);
	/** 5층 안. 빗장 앞, 방 가운데, 문에서 먼 북동쪽(벽체 칸막이 앞, 안으로 열린 문짝이 문간을 가리지 않는 쪽). 5층 바닥 기준. */
	const FVector BoltFeet(150.0f, 545.0f, 0.0f);
	const FVector BoltFace(162.0f, 456.0f, 132.0f);
	const FVector AnnexMiddleFeet(0.0f, 760.0f, 0.0f);
	const FVector AnnexCornerFeet(200.0f, 820.0f, 0.0f);
	const FVector AnnexDoorFace(130.0f, 452.0f, 100.0f);
	constexpr float AnnexFloorZ = 1200.0f;
	/** 문 앞이라고 볼 거리. */
	constexpr float NearDoor = 40.0f;
}

AIGGuestDoorsProbe::AIGGuestDoorsProbe()
{
	// 검사기는 매 프레임 손님의 단계와 노크 자리, 문을 잰다. -IGGuestDoorsProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGGuestDoorsProbe::Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer, AIGListenerEntity* InListener)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGGuestDoorsShots"));
	bLure = FParse::Param(FCommandLine::Get(), TEXT("IGGuestLure"));
}

void AIGGuestDoorsProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("GUESTDOORS_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGGuestDoorsProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("GUESTDOORS_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGGuestDoorsProbe::Next(const int32 NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
	bActed = false;
}

void AIGGuestDoorsProbe::Stand(const FVector& Feet, const FVector& LookAt)
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

bool AIGGuestDoorsProbe::Tap(const AActor* Expected, const TCHAR* FocusCheck)
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

void AIGGuestDoorsProbe::Shoot(const TCHAR* Name)
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
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GuestDoorsReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("GUESTDOORS_SHOT %s"), *Path);
}

bool AIGGuestDoorsProbe::IsCapturedSince(const double Seconds) const
{
	const AIGNightLoopDirector* Loop = NightLoop.Get();
	return Loop && (Loop->IsCaptureResetInFlight() || Loop->GetLastCaptureSeconds() > Seconds);
}

namespace IGGuestDoorsProbe
{
	static bool HasMesh(const AActor* Actor, const TCHAR* MeshName)
	{
		if (!Actor)
		{
			return false;
		}
		TArray<UStaticMeshComponent*> Meshes;
		Actor->GetComponents(Meshes);
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			if (Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName() == MeshName)
			{
				return true;
			}
		}
		return false;
	}

	static bool Near(const FVector& Point, const FVector& Front)
	{
		return FVector::Dist2D(Point, Front) < NearDoor;
	}
}

void AIGGuestDoorsProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 560.0f)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	AIGNightThreatDirector* Director = Threats.Get();
	if (Director)
	{
		const uint8 Stage = static_cast<uint8>(Director->GetGuestStage());
		if (Stage != LastStage)
		{
			LastStage = Stage;
			const FVector Knock = Director->GetGuestKnockLocation();
			UE_LOG(LogIndieGame, Display, TEXT("GUESTDOORS_STAGE %s door=%d knock=(%.0f,%.0f,%.0f) t=%.1f"),
				*StaticEnum<EIGGuestStage>()->GetNameStringByValue(Stage),
				static_cast<int32>(Director->GetGuestDoor()), Knock.X, Knock.Y, Knock.Z, TotalSeconds);
		}
	}
	if (Phase == 0)
	{
		if (PhaseSeconds < (bShots ? 12.0f : 1.0f))
		{
			return;
		}
		AIGPrologueWorldScene* Building = Scene.Get();
		for (TActorIterator<AIGNightThreatDirector> It(GetWorld()); It; ++It)
		{
			Threats = *It;
		}
		for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It)
		{
			NightLoop = *It;
		}
		for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
		{
			AnnexDoor = It->GetAnnexGate();
			RoofDoor = It->GetStairGate();
			AnnexBolt = It->GetAnnexBolt();
		}
		HomeDoor = Building ? Building->GetHomeDoor() : nullptr;
		Check(Threats.IsValid() && NightLoop.IsValid() && HomeDoor.IsValid() && AnnexDoor.IsValid() && RoofDoor.IsValid()
			&& AnnexBolt.IsValid(), TEXT("doors_and_bolt_resolved"));
		if (!Threats.IsValid() || !NightLoop.IsValid() || !HomeDoor.IsValid() || !AnnexDoor.IsValid() || !AnnexBolt.IsValid()
			|| !Player.IsValid() || !Listener.IsValid())
		{
			Finish();
			return;
		}
		Check(IGGuestDoorsProbe::HasMesh(AnnexDoor.Get(), TEXT("SM_AnnexDoorLeaf")), TEXT("annex_leaf_authored"));
		Check(IGGuestDoorsProbe::HasMesh(RoofDoor.Get(), TEXT("SM_RooftopDoorLeaf"))
			&& IGGuestDoorsProbe::HasMesh(RoofDoor.Get(), TEXT("SM_RooftopDoorSign")), TEXT("roof_leaf_and_sign_authored"));
		Check(AnnexBolt->GetDoor() == AnnexDoor.Get() && !AnnexDoor->IsLatched()
			&& IGGuestDoorsProbe::HasMesh(AnnexBolt.Get(), TEXT("SM_DoorBarrelBoltPin")), TEXT("bolt_on_annex_door_unlatched"));
		if (UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
		{
			Narrative->SetNightIndex(3);
		}
		Listener->SetDormant(true);
		if (UIGFlashlightComponent* Torch = Player->GetFlashlight())
		{
			Torch->SetAvailable(true);
			Torch->SetOn(true);
		}
		CaptureBaseline = NightLoop->GetLastCaptureSeconds();
		Next(bShots && !bLure ? 20 : 1);
		return;
	}
	if (bLure)
	{
		TickLure();
	}
	else
	{
		TickDefault();
	}
}

void AIGGuestDoorsProbe::TickDefault()
{
	using namespace IGGuestDoorsProbe;
	AIGNightThreatDirector* Director = Threats.Get();
	AIGSwingDoor* Home = HomeDoor.Get();
	AIGSwingDoor* Annex = AnnexDoor.Get();
	AIGPlayerCharacter* Character = Player.Get();
	if (!Director || !Home || !Annex || !Character || !AnnexBolt.IsValid())
	{
		Check(false, TEXT("stage_ready"));
		Finish();
		return;
	}
	const FVector Home4F(0.0f, 0.0f, AIGPrologueWorldScene::FourthFloorZ);
	const FVector Annex5F(0.0f, 0.0f, AnnexFloorZ);
	const EIGGuestStage Stage = Director->GetGuestStage();
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>();

	switch (Phase)
	{
	// 렌더 확인용 장면. 빗장(풀림·걸림), 5층 철문 옥상 쪽, 옥상 철문 안내판, 안으로 열린 5층 철문.
	case 20:
		if (!bActed)
		{
			bActed = true;
			Stand(BoltFeet + Annex5F, BoltFace + Annex5F);
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("guestdoors-bolt-open"));
			Next(21);
		}
		break;
	case 21:
		if (!bActed && PhaseSeconds >= 0.3f)
		{
			bActed = true;
			Tap(AnnexBolt.Get(), TEXT("bolt_takes_focus_for_shot"));
		}
		else if (bActed && PhaseSeconds >= 1.5f)
		{
			Shoot(TEXT("guestdoors-bolt-closed"));
			Next(22);
		}
		break;
	case 22:
		if (!bActed)
		{
			bActed = true;
			Annex->SetLatched(false, false);
			Stand(FVector(130.0f, 300.0f, 0.0f) + Annex5F, AnnexDoorFace + Annex5F);
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("guestdoors-annex-outside"));
			Next(23);
		}
		break;
	case 23:
		if (!bActed)
		{
			bActed = true;
			Stand(FVector(-277.5f, 120.0f, 1150.0f), FVector(-277.5f, 220.0f, 1347.0f));
		}
		else if (PhaseSeconds >= 1.2f)
		{
			Shoot(TEXT("guestdoors-roof-sign"));
			Next(24);
		}
		break;
	case 24:
		if (!bActed)
		{
			bActed = true;
			Annex->ForceOpenState(true);
			Stand(FVector(160.0f, 650.0f, 0.0f) + Annex5F, FVector(115.0f, 455.0f, 110.0f) + Annex5F);
		}
		else if (PhaseSeconds >= 1.2f)
		{
			// 스크린샷은 요청한 프레임이 끝날 때 잡힌다. 문은 다음 단계에서 닫는다.
			Shoot(TEXT("guestdoors-annex-open"));
			Next(1);
		}
		break;

	case 1:
		// 셋째 밤. 403호 안에서 현관을 보고 서 있는다. 걸쇠는 걸어 둔다.
		Annex->ForceOpenState(false);
		Stand(HomeFeet + Home4F, Door403Front + Home4F + FVector(0.0f, 0.0f, 100.0f));
		Home->ForceOpenState(false);
		Home->SetLatched(true, false);
		Next(2);
		break;

	case 2:
		if (Stage == EIGGuestStage::Canvassing)
		{
			Check(Director->GetGuestDoor() == EIGGuestDoor::Home && Near(Director->GetGuestKnockLocation(), Door401Front),
				TEXT("canvass_starts_at_401"));
			Next(3);
		}
		else if (Stage != EIGGuestStage::Idle || PhaseSeconds >= 240.0f)
		{
			Check(false, TEXT("canvass_starts_at_401"));
			Finish();
		}
		break;

	case 3:
		if (Stage == EIGGuestStage::Canvassing)
		{
			bSaw402 |= Near(Director->GetGuestKnockLocation(), Door402Front);
		}
		else if (Stage == EIGGuestStage::Knocking)
		{
			Check(bSaw402, TEXT("canvass_moves_to_402"));
			Check(Near(Director->GetGuestKnockLocation(), Door403Front), TEXT("guest_reaches_403_after_neighbors"));
			Next(4);
		}
		else
		{
			Check(false, TEXT("guest_reaches_403_after_neighbors"));
			Finish();
		}
		break;

	case 4:
		bSawCaught |= Stage == EIGGuestStage::CaughtOnLatch;
		if (Stage == EIGGuestStage::Spent)
		{
			Check(bSawCaught, TEXT("latch_holds_after_canvass"));
			Check(Home->IsFullyClosed() && !IsCapturedSince(CaptureBaseline), TEXT("night3_guest_leaves_her_alone"));
			Next(5);
		}
		else if (PhaseSeconds >= 40.0f)
		{
			Check(false, TEXT("latch_holds_after_canvass"));
			Finish();
		}
		break;

	case 5:
		// 넷째 밤. 5층에 들어가 철문 안쪽 빗장을 건다.
		if (!bActed)
		{
			bActed = true;
			if (Narrative)
			{
				Narrative->SetNightIndex(4);
			}
			Home->SetLatched(false, false);
			Annex->ForceOpenState(false);
			Annex->SetLatched(false, false);
			Stand(BoltFeet + Annex5F, BoltFace + Annex5F);
		}
		else if (PhaseSeconds >= 0.4f)
		{
			Tap(AnnexBolt.Get(), TEXT("bolt_takes_focus"));
			Check(Annex->IsLatched(), TEXT("bolt_latches_from_inside"));
			Stand(AnnexMiddleFeet + Annex5F, AnnexDoorFace + Annex5F);
			Next(6);
		}
		break;

	case 6:
		if (Stage != EIGGuestStage::Idle && Stage != EIGGuestStage::Spent)
		{
			Check(Director->GetGuestDoor() == EIGGuestDoor::Annex, TEXT("night4_guest_comes_to_annex_door"));
			Check(Stage == EIGGuestStage::Knocking && Near(Director->GetGuestKnockLocation(), AnnexKnockFront),
				TEXT("annex_guest_knocks_on_closed_door"));
			Next(7);
		}
		else if (PhaseSeconds >= 120.0f)
		{
			Check(false, TEXT("night4_guest_comes_to_annex_door"));
			Finish();
		}
		break;

	case 7:
		bSawKeypad |= Stage == EIGGuestStage::Keypad;
		if (Stage == EIGGuestStage::CaughtOnLatch)
		{
			Check(bSawKeypad, TEXT("annex_guest_tries_keys"));
			Check(Annex->IsFullyClosed() && Annex->IsLatched(), TEXT("bolt_holds_annex_door"));
			Next(8);
		}
		else if (Stage == EIGGuestStage::Inside || Stage == EIGGuestStage::Spent || PhaseSeconds >= 40.0f)
		{
			Check(false, TEXT("bolt_holds_annex_door"));
			Finish();
		}
		break;

	case 8:
		if (Stage == EIGGuestStage::Spent)
		{
			Check(Annex->IsFullyClosed() && !IsCapturedSince(CaptureBaseline), TEXT("annex_guest_leaves_her_alone"));
			Finish();
		}
		else if (PhaseSeconds >= 15.0f)
		{
			Check(false, TEXT("annex_guest_leaves_her_alone"));
			Finish();
		}
		break;

	default:
		break;
	}
}

void AIGGuestDoorsProbe::TickLure()
{
	using namespace IGGuestDoorsProbe;
	AIGNightThreatDirector* Director = Threats.Get();
	AIGSwingDoor* Home = HomeDoor.Get();
	AIGSwingDoor* Annex = AnnexDoor.Get();
	if (!Director || !Home || !Annex || !Player.IsValid())
	{
		Check(false, TEXT("stage_ready"));
		Finish();
		return;
	}
	const FVector Home4F(0.0f, 0.0f, AIGPrologueWorldScene::FourthFloorZ);
	const FVector Annex5F(0.0f, 0.0f, AnnexFloorZ);
	const EIGGuestStage Stage = Director->GetGuestStage();
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>();
	FVector Body = FVector::ZeroVector;
	const bool bBody = Director->GetGuestBodyLocation(Body);

	switch (Phase)
	{
	case 1:
		// 셋째 밤. 403호 현관 안쪽 문간에서 복도 서쪽(401호 쪽)을 보고 선다. 걸쇠는 풀어 둔다.
		Stand(HomeDoorwayFeet + Home4F, Door401Front + Home4F + FVector(0.0f, -30.0f, 100.0f));
		Home->ForceOpenState(false);
		Home->SetLatched(false, false);
		Next(2);
		break;

	case 2:
		if (Stage == EIGGuestStage::Canvassing)
		{
			Next(3);
		}
		else if (Stage != EIGGuestStage::Idle || PhaseSeconds >= 240.0f)
		{
			Check(false, TEXT("lure_canvass_starts"));
			Finish();
		}
		break;

	case 3:
		// 401호를 두드리고 1초 뒤 현관을 연다.
		if (!bActed && PhaseSeconds >= 1.0f)
		{
			bActed = true;
			Home->ForceOpenState(true);
		}
		else if (bActed && PhaseSeconds >= 1.3f)
		{
			Check(Stage == EIGGuestStage::Canvassing && bBody && FVector::Dist2D(Body, Door401Front) < 60.0f,
				TEXT("lure_body_appears_at_401"));
			LureFirstBody = Body;
			Next(4);
		}
		break;

	case 4:
		if (PhaseSeconds >= 0.8f)
		{
			Check(bBody && Body.X > LureFirstBody.X + 40.0f, TEXT("lure_body_comes_to_403"));
			Home->ForceOpenState(false);
			Home->SetLatched(true, false);
			Next(5);
		}
		break;

	case 5:
		if (PhaseSeconds >= 0.3f)
		{
			Check(Stage == EIGGuestStage::Knocking && !bBody && Near(Director->GetGuestKnockLocation(), Door403Front),
				TEXT("closing_door_turns_lure_into_knock"));
			Next(6);
		}
		break;

	case 6:
		bSawCaught |= Stage == EIGGuestStage::CaughtOnLatch;
		if (Stage == EIGGuestStage::Spent)
		{
			Check(bSawCaught && !IsCapturedSince(CaptureBaseline), TEXT("lured_guest_caught_on_latch_and_leaves"));
			Next(7);
		}
		else if (PhaseSeconds >= 40.0f)
		{
			Check(false, TEXT("lured_guest_caught_on_latch_and_leaves"));
			Finish();
		}
		break;

	case 7:
		// 넷째 밤. 5층 철문을 열어 둔 채 문에서 먼 구석에 선다. 빗장은 풀어 둔다.
		if (Narrative)
		{
			Narrative->SetNightIndex(4);
		}
		Home->SetLatched(false, false);
		Annex->SetLatched(false, false);
		Annex->ForceOpenState(true);
		Stand(AnnexCornerFeet + Annex5F, AnnexDoorFace + Annex5F);
		bSawCaught = false;
		Next(8);
		break;

	case 8:
		if (Stage == EIGGuestStage::AtOpenDoor)
		{
			Check(Director->GetGuestDoor() == EIGGuestDoor::Annex && bBody && FVector::Dist2D(Body, AnnexThresholdFront) < 30.0f,
				TEXT("open_annex_door_guest_waits_in_doorway"));
			Next(9);
		}
		else if ((Stage != EIGGuestStage::Idle && Stage != EIGGuestStage::Spent) || PhaseSeconds >= 120.0f)
		{
			Check(false, TEXT("open_annex_door_guest_waits_in_doorway"));
			Finish();
		}
		break;

	case 9:
		// 들어가도 되느냐고 묻고 난 뒤에 문을 닫는다. 장면은 닫기 전 프레임에 찍는다.
		if (PhaseSeconds >= 2.4f && PhaseSeconds - GetWorld()->GetDeltaSeconds() < 2.4f)
		{
			Shoot(TEXT("guestdoors-annex-doorway"));
		}
		if (!bActed && PhaseSeconds >= 2.6f)
		{
			bActed = true;
			Check(Stage == EIGGuestStage::AtOpenDoor, TEXT("doorway_guest_waits_for_answer"));
			Annex->ForceOpenState(false);
		}
		else if (bActed && PhaseSeconds >= 2.9f)
		{
			Check(Stage == EIGGuestStage::Knocking && !bBody, TEXT("closing_annex_door_sends_guest_outside"));
			CaptureBaseline = NightLoop.IsValid() ? NightLoop->GetLastCaptureSeconds() : 0.0;
			Next(10);
		}
		break;

	case 10:
		bSawKeypad |= Stage == EIGGuestStage::Keypad;
		bSawInside |= Stage == EIGGuestStage::Inside;
		if (IsCapturedSince(CaptureBaseline))
		{
			Check(bSawKeypad && bSawInside, TEXT("unbolted_annex_door_opens_with_key"));
			Check(true, TEXT("guest_catches_her_in_annex"));
			Finish();
		}
		else if (PhaseSeconds >= 60.0f)
		{
			Check(bSawKeypad && bSawInside, TEXT("unbolted_annex_door_opens_with_key"));
			Check(false, TEXT("guest_catches_her_in_annex"));
			Finish();
		}
		break;

	default:
		break;
	}
}
