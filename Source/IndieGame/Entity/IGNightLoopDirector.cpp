#include "Entity/IGNightLoopDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorMercyDirector.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGMissingFloorNightTwoBeatDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IndieGame.h"
#include "Interaction/IGDroppedFlashlight.h"
#include "Materials/MaterialInterface.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGPlayerController.h"
#include "Player/IGStressComponent.h"
#include "TimerManager.h"

namespace IGNightLoop
{
	constexpr int32 MercyNoteCaptureThreshold = 5;
	constexpr float MercyNoteRevealDelaySeconds = 0.18f;
	constexpr float MercyNoteSlideSeconds = 0.82f;
	const FVector MercyNoteStartLocation(-150.0f, -239.0f, 900.12f);
	const FVector MercyNoteRestLocation(-150.0f, -269.5f, 900.12f);
	constexpr float MercyNoteStartYaw = -0.5f;
	constexpr float MercyNoteRestYaw = -3.5f;
	// §6. 눈을 뜬 뒤 둘러볼 수 있기까지. 화면은 그 뒤로도 천천히 밝아진다. 바닥에서
	// 몸을 일으키는 동안(ComeToSeconds)은 걷기만 막는다.
	constexpr float MaxWakeLockSeconds = 0.35f;

	/**
	 * 괴이마다 끊기는 때와 깨어나는 모양. 순서는 EIGCaptureKind와 같다.
	 * Cut은 잡히고 화면이 끊기기까지, Hold는 끊긴 뒤 다른 자리로 옮기기까지,
	 * ComeTo는 바닥에서 몸을 일으키는 시간(0이면 선 채로 정신이 든다), Lost는
	 * 정신을 잃은 동안 흐른 시간이다. 손목시계가 그만큼 건너뛴다.
	 */
	struct FCaptureSpec
	{
		float CutSeconds;
		float HoldSeconds;
		float ComeToSeconds;
		float LostSeconds;
	};
	constexpr FCaptureSpec CaptureSpecs[] = {
		// 위층 사람. 몸싸움 소리의 첫 노크(1.48초)에 끊기고 둘째 노크는 검은 화면에서 듣는다.
		{1.48f, 0.92f, 1.5f, 120.0f},
		// 어둑시니. 고개가 1.2초에 걸쳐 들리고, 빛과 소리가 다 빠진 뒤 끊긴다.
		{1.70f, 1.00f, 1.6f, 90.0f},
		// 손님. 돌아보면 문간에 종이 몸이 서 있고, 문이 닫히는 소리에 끊긴다.
		{1.30f, 0.80f, 1.3f, 60.0f},
		// 관리인. 손전등에 눈이 멀고 손목을 잡힌다. 403호까지 끌려가는 동안 눈은 하얗다.
		{1.20f, 1.60f, 0.0f, 180.0f},
		// 덧댄 것. 판과 몸 사이에 눌려 숨이 막힌다. 몸이 물러난 자리 옆 넓은 데서 깬다.
		{1.10f, 0.90f, 1.4f, 60.0f},
	};
	const FCaptureSpec& SpecFor(const EIGCaptureKind Kind)
	{
		const int32 Index = FMath::Clamp(
			static_cast<int32>(Kind), 0, static_cast<int32>(UE_ARRAY_COUNT(CaptureSpecs)) - 1);
		return CaptureSpecs[Index];
	}
	// 위층 사람은 지나온 길로 이만큼 넘게 끌고 간다. 그만큼 지나온 자리가 없으면
	// 절반이라도 끌고, 그것도 없으면 잡힌 자리에서 깬다.
	constexpr float DragMinDistance = 300.0f;
	constexpr float FootholdMaxAgeSeconds = 120.0f;
	// 어둑시니는 마지막으로 불빛 아래 섰던 자리로 돌려보낸다(방 어둠 0.45 이하).
	constexpr float LitRoomDarkness = 0.45f;
	constexpr float LitFootholdMaxAgeSeconds = 240.0f;
	constexpr float EoduksiniBatteryCost = 0.35f;
	// 깨어난 뒤 위층 사람이 듣지도 잡지도 않는 시간.
	constexpr float ListenerGraceSeconds = 12.0f;
	// 깨어난 뒤 속말이 나오기까지. 눈이 다 뜨이고 몸을 일으킨 뒤다.
	constexpr float WakeThoughtDelaySeconds = 2.4f;
	const FName ComeToLockName(TEXT("CaptureComeTo"));
}

AIGNightLoopDirector::AIGNightLoopDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

FVector AIGNightLoopDirector::GetMercyNoteRestLocation()
{
	return IGNightLoop::MercyNoteRestLocation;
}

void AIGNightLoopDirector::BeginPlay()
{
	Super::BeginPlay();
	InitializeMercyNote();
	if (const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		CaptureCount = FMath::Max(CaptureCount, Narrative->GetCaptureCount());
		ListenerCaptureCount = FMath::Max(ListenerCaptureCount, Narrative->GetCaptureCount());
		if (ListenerCaptureCount >= IGNightLoop::MercyNoteCaptureThreshold)
		{
			SetMercyNoteAtRest();
		}
	}

	// A placed director adopts any entity already in the world; a spawner
	// that creates both can also pair them explicitly via RegisterEntity.
	if (!ListenerEntity.IsValid())
	{
		for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
		{
			RegisterEntity(*It);
			break;
		}
	}
}

void AIGNightLoopDirector::SetWakeTransform(const FTransform& Transform)
{
	WakeTransform = Transform;
	bWakeTransformSet = true;
}

bool AIGNightLoopDirector::RestorePlayerAtWakePoint(
	AIGPlayerCharacter* Character) const
{
	if (!bWakeTransformSet || !IsValid(Character))
	{
		return false;
	}
	Character->TeleportTo(
		WakeTransform.GetLocation(),
		WakeTransform.Rotator(),
		false,
		true);
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
	if (APlayerController* Controller =
		Cast<APlayerController>(Character->GetController()))
	{
		Controller->SetControlRotation(WakeTransform.Rotator());
	}
	return true;
}

void AIGNightLoopDirector::RegisterEntity(AIGListenerEntity* Entity)
{
	if (!Entity || ListenerEntity.Get() == Entity)
	{
		return;
	}
	ListenerEntity = Entity;
	Entity->OnPlayerCaptured.AddUObject(
		this, &AIGNightLoopDirector::HandlePlayerCaptured);
}

void AIGNightLoopDirector::HandlePlayerCaptured(APawn* Player)
{
	if (bResetInFlight)
	{
		return;
	}
	AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(Player);
	if (!Character)
	{
		return;
	}
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (Narrative
		&& Narrative->GetNightIndex() == 4
		&& Narrative->GetAggressionTier() >= 3
		&& Narrative->IsNightFourMaskRunning())
	{
		// 이 포획은 엔딩 C가 단독으로 처리한다. 일반 포획 흐름까지 시작하면
		// 하나의 멀티캐스트에서 서로 충돌하는 두 타임라인이 진행된다.
		return;
	}
	BeginCaptureReset(Character, EIGCaptureKind::Listener, nullptr);
}

FTransform AIGNightLoopDirector::GetHomeEntryWake()
{
	// 문짝 가운데에서 75cm 안쪽. 문은 안쪽(+Y)으로 열리지만 닫힌 뒤에 옮긴다. 높이는
	// 깨울 때 바닥을 재서 맞춘다.
	return FTransform(
		FRotator(0.0f, -90.0f, 0.0f),
		FVector(
			AIGPrologueWorldScene::HomeDoorX + AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f,
			AIGPrologueWorldScene::HomeDoorY + 75.0f,
			AIGPrologueWorldScene::FourthFloorZ + 100.0f));
}

void AIGNightLoopDirector::RequestExternalCapture(
	APawn* Player,
	const EIGCaptureKind Kind,
	const FTransform* WakeAt)
{
	AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(Player);
	if (bResetInFlight || !Character)
	{
		return;
	}
	// 밤4 가면 대치 중에는 위층 사람에게 잡힌 것과 똑같이 다룬다(HandlePlayerCaptured).
	// 괴이 감독은 정전 뒤 망치질 동안에만 나온다(어둑시니, 철문 밖의 부름). 공격성이 3에
	// 닿았으면 결말 C가 혼자 맡고, 아니면 평소처럼 깨어난다. 둘을 같이 걸면 타임라인
	// 둘이 부딪친다.
	if (const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		Narrative && Narrative->GetNightIndex() == 4 && Narrative->IsNightFourMaskRunning()
		&& Narrative->GetAggressionTier() >= 3)
	{
		for (TActorIterator<AIGMissingFloorNightFourDirector> It(GetWorld()); It; ++It)
		{
			It->HandleMaskExternalCapture(Character);
		}
		return;
	}
	BeginCaptureReset(Character, Kind, WakeAt);
}

void AIGNightLoopDirector::BeginCaptureReset(
	AIGPlayerCharacter* Character,
	const EIGCaptureKind Kind,
	const FTransform* WakeAt)
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	bResetInFlight = true;
	CaptureKindInFlight = Kind;
	LastCaptureSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const int32 PersistedCaptureCount = Narrative
		? Narrative->GetCaptureCount()
		: 0;
	CaptureCount = FMath::Max(CaptureCount + 1, PersistedCaptureCount + 1);
	if (Kind == EIGCaptureKind::Listener)
	{
		ListenerCaptureCount = FMath::Max(ListenerCaptureCount + 1, PersistedCaptureCount + 1);
	}
	CapturedPlayer = Character;
	LastCaptureSpot = Character->GetActorLocation();
	// 깰 자리는 끊기기 전에 정한다. 끊긴 뒤에는 몸이 이미 쓰러져 있다.
	CaptureWake = WakeAt ? *WakeAt : ChooseCaptureWake(*Character, Kind);
	Character->GetCharacterMovement()->StopMovementImmediately();
	if (Kind == EIGCaptureKind::Listener)
	{
		SpawnCaptureHandprint(Character);
	}
	else
	{
		// 석고 손자국은 위층 사람의 것이다. 지난번 포획의 몸이 남아 있으면 그 몸이
		// 다시 덮치는 화면이 나오므로 함께 비운다.
		Character->SetCaptureThreat(nullptr);
	}

	// Without an authored wake point the first capture teaches us one: the
	// spot the player stood when the night began is better than nothing,
	// but directors in real maps must call SetWakeTransform.
	if (!bWakeTransformSet)
	{
		WakeTransform = Character->GetActorTransform();
		bWakeTransformSet = true;
	}

	const IGNightLoop::FCaptureSpec& Spec = IGNightLoop::SpecFor(Kind);
	if (APlayerController* Controller =
		Cast<APlayerController>(Character->GetController()))
	{
		// 남아 있던 페이드를 먼저 걷는다. 포획 시점이 거는 빛 빠짐과 눈부심이 그 뒤에 온다.
		if (Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StopCameraFade();
		}
		Character->PlayCaptureFeedback(Spec.CutSeconds + Spec.HoldSeconds, Spec.CutSeconds, Kind);
		Character->DisableInput(Controller);
	}
	// 화면은 캐릭터가 끊는다(PlayCaptureFeedback). 캐릭터가 끊지 못한 경우에만 여기서
	// 짧게 닫는다. 이미 덮였으면 건드리지 않는다.
	GetWorldTimerManager().SetTimer(CaptureFadeTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (AIGPlayerCharacter* Pawn = CapturedPlayer.Get())
			{
				if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
				{
					if (PC->PlayerCameraManager && PC->PlayerCameraManager->FadeAmount < 0.99f)
					{
						PC->PlayerCameraManager->StartCameraFade(0.f, 1.f,
							.15f, Pawn->GetCaptureCutColour(), false, true);
					}
				}
			}
		}), Spec.CutSeconds + 0.1f, false);

	GetWorldTimerManager().SetTimer(
		ResetTimer,
		this,
		&AIGNightLoopDirector::FinishReset,
		FMath::Max(Spec.CutSeconds + Spec.HoldSeconds, 0.05f),
		false);
}

FTransform AIGNightLoopDirector::ChooseCaptureWake(
	const AIGPlayerCharacter& Character,
	const EIGCaptureKind Kind) const
{
	const FVector Spot = Character.GetActorLocation();
	FVector Location = Spot;
	FRotator Rotation(0.0f, Character.GetControlRotation().Yaw, 0.0f);
	bool bFound = Kind == EIGCaptureKind::Eoduksini
		&& Character.FindLitFoothold(
			IGNightLoop::LitRoomDarkness, IGNightLoop::LitFootholdMaxAgeSeconds, Location, Rotation);
	if (!bFound)
	{
		bFound = Character.FindFootholdAway(
				Spot, IGNightLoop::DragMinDistance, IGNightLoop::FootholdMaxAgeSeconds, Location, Rotation)
			|| Character.FindFootholdAway(
				Spot, IGNightLoop::DragMinDistance * 0.5f, IGNightLoop::FootholdMaxAgeSeconds, Location, Rotation);
	}
	if (bFound && Kind == EIGCaptureKind::Listener)
	{
		// 끌려온 쪽, 잡힌 자리를 보고 깬다. 떨어뜨린 손전등의 빛이 거기 있다.
		const FVector Back = Spot - Location;
		if (!Back.IsNearlyZero(1.0f))
		{
			Rotation = FRotator(0.0f, Back.Rotation().Yaw, 0.0f);
		}
	}
	return FTransform(Rotation, Location);
}

void AIGNightLoopDirector::DropFlashlightAt(AIGPlayerCharacter& Character)
{
	UIGFlashlightComponent* Torch = Character.GetFlashlight();
	UWorld* World = GetWorld();
	if (!World || !Torch || !Torch->IsAvailable() || DroppedFlashlight.IsValid())
	{
		return;
	}
	const FVector Spot = Character.GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGCaptureDroppedTorch), false, &Character);
	Params.AddIgnoredActor(this);
	if (const AIGListenerEntity* Entity = ListenerEntity.Get())
	{
		Params.AddIgnoredActor(Entity);
	}
	FHitResult Floor;
	if (!World->LineTraceSingleByChannel(
			Floor, Spot, Spot - FVector(0.0f, 0.0f, 300.0f), ECC_Visibility, Params))
	{
		return;
	}
	// 렌즈가 깨어날 자리 쪽을 보게 눕힌다. 그쪽에서 빛이 보여야 찾으러 온다. 정면으로
	// 겨누면 눈부시기만 해서 조금 비튼다.
	const FVector Toward = (CaptureWake.GetLocation() - Spot).GetSafeNormal2D();
	const float Yaw = Toward.IsNearlyZero()
		? Character.GetActorRotation().Yaw + 150.0f
		: Toward.Rotation().Yaw + 20.0f;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AIGDroppedFlashlight* Dropped = World->SpawnActor<AIGDroppedFlashlight>(
		AIGDroppedFlashlight::StaticClass(),
		FTransform(FRotator(0.0f, Yaw, 0.0f), Floor.ImpactPoint),
		SpawnParameters);
	if (!Dropped)
	{
		return;
	}
	Dropped->Configure(Torch->IsOn());
	Dropped->PlaceOnFloor(Floor.ImpactPoint);
	Torch->SetDropped(true);
	DroppedFlashlight = Dropped;
}

void AIGNightLoopDirector::ReleaseComeToLock()
{
	GetWorldTimerManager().ClearTimer(ComeToTimer);
	if (!bComeToLockHeld)
	{
		return;
	}
	bComeToLockHeld = false;
	const UWorld* World = GetWorld();
	if (AIGPlayerController* Controller = World
		? Cast<AIGPlayerController>(World->GetFirstPlayerController())
		: nullptr)
	{
		Controller->RemoveInputLock(IGNightLoop::ComeToLockName);
	}
}

void AIGNightLoopDirector::FinishReset()
{
	GetWorldTimerManager().ClearTimer(CaptureFadeTimer);
	const EIGCaptureKind Kind = CaptureKindInFlight;
	const IGNightLoop::FCaptureSpec& Spec = IGNightLoop::SpecFor(Kind);
	bool bWakeRecoveryScheduled = false;
	bool bTorchDropped = false;
	AIGPlayerCharacter* Character = CapturedPlayer.Get();
	if (Character)
	{
		// 잡힌 자리에서 끝낼 일. 몸을 옮기기 전, 아직 그 자리에 있을 때 한다.
		if (Kind == EIGCaptureKind::Listener)
		{
			DropFlashlightAt(*Character);
			bTorchDropped = DroppedFlashlight.IsValid();
		}
		else if (Kind == EIGCaptureKind::Eoduksini)
		{
			if (UIGFlashlightComponent* Torch = Character->GetFlashlight())
			{
				Torch->DrainBattery(IGNightLoop::EoduksiniBatteryCost);
			}
		}

		// 정해 둔 자리에 바닥을 재서 세운다. 지나온 자리는 실제로 서 있던 높이라 그대로
		// 맞고, 손님과 관리인이 준 현관은 여기서 바닥에 붙는다.
		FVector WakeLocation = CaptureWake.GetLocation();
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(IGCaptureWakeFloor), false, Character);
			FloorParams.AddIgnoredActor(this);
			FHitResult Floor;
			if (GetWorld()->LineTraceSingleByChannel(
					Floor,
					WakeLocation + FVector(0.0f, 0.0f, 40.0f),
					WakeLocation - FVector(0.0f, 0.0f, 260.0f),
					ECC_Visibility,
					FloorParams))
			{
				WakeLocation.Z = Floor.ImpactPoint.Z + Capsule->GetScaledCapsuleHalfHeight() + 0.5f;
			}
		}
		CaptureWake.SetLocation(WakeLocation);
		Character->TeleportTo(
			WakeLocation,
			CaptureWake.Rotator(),
			false,
			true);
		if (UCharacterMovementComponent* Movement =
			Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}

		if (APlayerController* Controller =
			Cast<APlayerController>(Character->GetController()))
		{
			Controller->SetControlRotation(CaptureWake.Rotator());
			const float WakeEchoSeconds = GetWakeEchoSeconds();
			const float WakeRecoverySeconds = GetWakeRecoverySeconds();
			if (AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(Controller->GetHUD()))
			{
				HorrorHUD->PlayCaptureWakeEcho(
					CaptureCount,
					WakeEchoSeconds,
					WakeRecoverySeconds);
			}
			if (Controller->PlayerCameraManager)
			{
				// 끊긴 색에서 돌아온다. 관리인의 손전등에 하얗게 먼 눈은 하얀 데서 천천히
				// 돌아오고, 어둑시니가 가져간 소리는 빛과 함께 돌아온다.
				Controller->PlayerCameraManager->StartCameraFade(
					1.0f, 0.0f, GetWakeFadeInSeconds(), Character->GetCaptureCutColour(),
					/*bShouldFadeAudio=*/Kind == EIGCaptureKind::Eoduksini,
					/*bHoldWhenFinished=*/false);
			}
			if (Spec.ComeToSeconds > 0.0f)
			{
				// 바닥에서 몸을 일으킨다. 둘러볼 수는 있고 걷지는 못한다.
				Character->PlayComeTo(Spec.ComeToSeconds);
				if (AIGPlayerController* IGController = Cast<AIGPlayerController>(Controller))
				{
					IGController->AddInputLock(
						IGNightLoop::ComeToLockName, /*bLockMove=*/true, /*bLockLook=*/false);
					bComeToLockHeld = true;
					GetWorldTimerManager().SetTimer(
						ComeToTimer,
						this,
						&AIGNightLoopDirector::ReleaseComeToLock,
						Spec.ComeToSeconds,
						false);
				}
				// 바닥에서 옷이 쓸리는 소리 하나. 실패의 효과음은 없다.
				IGAudio::SpawnOneShotAt(
					this,
					UIGToneSequenceSoundWave::CreateClothSettle(this),
					WakeLocation,
					0.72f,
					0.94f,
					75.0f,
					480.0f);
			}
			GetWorldTimerManager().SetTimer(
				WakeRecoveryTimer,
				this,
				&AIGNightLoopDirector::FinishWakeRecovery,
				WakeRecoverySeconds,
				false);
			bWakeRecoveryScheduled = true;
		}
	}

	if (AIGListenerEntity* Entity = ListenerEntity.Get())
	{
		// 단계는 서사 기록에 있다. 그만두고 다시 불러와도 느긋한 그가 돌아오지 않는다.
		// 다른 괴이에게 잡힌 것은 그의 성과가 아니다. 단계는 그대로 두고 자리만 돌린다.
		if (Kind == EIGCaptureKind::Listener)
		{
			UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
			Entity->SetAggressionTier(Narrative
				? Narrative->RecordCapture()
				: Entity->GetAggressionTier() + 1);
		}
		Entity->ReturnAfterCapture(IGNightLoop::ListenerGraceSeconds);
	}

	// 정신을 잃은 동안 흐른 시간. 깨어나며 손목시계를 보면 그만큼 지나 있다.
	// 추격 없음에서 덧댄 것에 눌린 것은 밀려난 것뿐이다. 시간은 흐르지 않는다.
	float LostSeconds = Spec.LostSeconds;
	if (Kind == EIGCaptureKind::Lining)
	{
		if (const AIGListenerEntity* Upstairs = ListenerEntity.Get(); Upstairs && !Upstairs->GetTuning().bCaptureEnabled)
		{
			LostSeconds = 0.0f;
		}
	}
	LastLostSeconds = 0.0f;
	for (TActorIterator<AIGNightPhaseDirector> It(GetWorld()); It; ++It)
	{
		LastLostSeconds = It->ConsumeHourSeconds(LostSeconds);
		break;
	}

	QueueMercyNoteReveal();

	// §20.3-1: two resets with nothing learned in between and the world adds one
	// more thing to look at. The fifth-capture note above is the third net and a
	// separate beat; these two never stand in for each other.
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<AIGMissingFloorMercyDirector> It(World); It; ++It)
		{
			It->NotifyCaptureReset();
			break;
		}
		// §8 비트 2-5: 정신을 잃고 옮겨진 것은 장부를 들고 돌아온 것이 아니다. 403호
		// 안에서 깨어났으면 한 번 나갔다 와야 한다.
		TActorIterator<AIGMissingFloorNightTwoBeatDirector> ReturnBeat(World);
		if (ReturnBeat)
		{
			ReturnBeat->NotifyCaptureReset();
		}
		// §8 비트 3-7도 같은 빚을 진다. 그는 순찰 처음으로 돌아갔으니 복도에 다시 세운다.
		TActorIterator<AIGMissingFloorNightThreeDirector> PassBeat(World);
		if (PassBeat)
		{
			PassBeat->NotifyCaptureReset();
		}
	}

	// 잡은 괴이가 제 흔적을 남긴다(손님의 전단과 걸쇠, 관리인이 가져간 열쇠). 화면은
	// 아직 덮여 있다.
	OnCaptureSettled.Broadcast(Kind, LastCaptureSpot);

	if (bWakeRecoveryScheduled && Character)
	{
		// 눈이 뜨이며 끊겼던 숨을 한 번에 들이켠다. 괴물이 순찰 자리로 돌아간
		// 뒤라야 포획 중의 숨 막음이 풀려 있다.
		if (UIGStressComponent* Stress = Character->GetStress())
		{
			Stress->PlayGasp(/*bIgnoreCooldown=*/true);
		}
		// 무엇을 잃었는지 한 줄. 손님과 관리인은 저마다 남긴 것으로 말한다.
		FText Thought;
		if (Kind == EIGCaptureKind::Listener)
		{
			Thought = bTorchDropped
				? NSLOCTEXT("IGMissingFloor", "YudamWakeTorchDropped", "손전등을 저기 떨어뜨렸네.")
				: NSLOCTEXT("IGMissingFloor", "YudamWakeDragged", "여기까지 끌려왔네.");
		}
		else if (Kind == EIGCaptureKind::Eoduksini)
		{
			Thought = NSLOCTEXT("IGMissingFloor", "YudamWakeUnderLight", "내가 언제 여기까지 왔지?");
		}
		else if (Kind == EIGCaptureKind::Lining)
		{
			Thought = NSLOCTEXT("IGMissingFloor", "YudamWakePressed", "숨이 막혔어. 저게 지나가는 자리엔 서 있으면 안 돼.");
		}
		if (!Thought.IsEmpty())
		{
			GetWorldTimerManager().SetTimer(
				WakeThoughtTimer,
				FTimerDelegate::CreateWeakLambda(this, [this, Thought]()
				{
					AIGHorrorHUD::PushThought(this, Thought, 2.8f);
				}),
				IGNightLoop::WakeThoughtDelaySeconds,
				false);
		}
	}

	if (!bWakeRecoveryScheduled)
	{
		// 여기까지 왔다는 것은 폰이나 컨트롤러가 암전 사이에 사라졌다는
		// 뜻이다. 포획 암전은 bHoldWhenFinished로 걸어 두므로 아무도 풀지
		// 않으면 화면이 검은 채로 남는다 — 이벤트도 프롬프트도 없이 게임이
		// 끝난 것처럼 보이는 상태다. 기상 복귀가 그 자리를 정리한다.
		FinishWakeRecovery();
	}
}

void AIGNightLoopDirector::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bMercyNoteSliding || !MercyNote)
	{
		SetActorTickEnabled(false);
		return;
	}

	MercyNoteSlideElapsedSeconds += FMath::Max(0.0f, DeltaSeconds);
	const float Alpha = FMath::Clamp(
		MercyNoteSlideElapsedSeconds / IGNightLoop::MercyNoteSlideSeconds,
		0.0f,
		1.0f);
	const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);
	MercyNote->SetWorldLocation(FMath::Lerp(
		IGNightLoop::MercyNoteStartLocation,
		IGNightLoop::MercyNoteRestLocation,
		SmoothAlpha));
	MercyNote->SetWorldRotation(FRotator(
		0.0f,
		FMath::Lerp(
			IGNightLoop::MercyNoteStartYaw,
			IGNightLoop::MercyNoteRestYaw,
			SmoothAlpha),
		0.0f));

	if (Alpha >= 1.0f)
	{
		SetMercyNoteAtRest();
	}
}

void AIGNightLoopDirector::FinishWakeRecovery()
{
	AIGPlayerCharacter* Character = CapturedPlayer.Get();
	APlayerController* Controller = Character
		? Cast<APlayerController>(Character->GetController())
		: nullptr;
	if (Character && Controller)
	{
		Character->EnableInput(Controller);
	}
	else if (bResetInFlight)
	{
		// 암전은 열렸지만 조작을 되돌려 줄 상대가 없다. 화면은 살아 있고
		// 입력만 죽은 상태로 남지 않게 컨트롤러 쪽에서라도 풀어 둔다.
		AbortCaptureBlackout(TEXT("wake recovery finished without a pawn"));
	}

	CapturedPlayer = nullptr;
	bResetInFlight = false;
}

void AIGNightLoopDirector::AbortCaptureBlackout(const TCHAR* Reason)
{
	GetWorldTimerManager().ClearTimer(CaptureFadeTimer);
	ReleaseComeToLock();
	UWorld* World = GetWorld();
	AIGPlayerCharacter* Character = CapturedPlayer.Get();
	APlayerController* Controller = Character
		? Cast<APlayerController>(Character->GetController())
		: nullptr;
	// 폰이 사라졌어도 화면을 가진 컨트롤러는 남아 있다. 암전은 컨트롤러의
	// 카메라 매니저가 들고 있으므로 그쪽에서 걷는다.
	if (!Controller && World)
	{
		Controller = World->GetFirstPlayerController();
	}
	if (Controller && Controller->PlayerCameraManager)
	{
		Controller->PlayerCameraManager->StopCameraFade();
	}
	if (Character && Controller)
	{
		Character->EnableInput(Controller);
	}
	UE_LOG(
		LogIndieGame,
		Warning,
		TEXT("IG_CAPTURE_RESET aborted blackout: %s (controller=%s)"),
		Reason,
		Controller ? TEXT("yes") : TEXT("none"));
}

void AIGNightLoopDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(CaptureFadeTimer);
	GetWorldTimerManager().ClearTimer(WakeThoughtTimer);
	ReleaseComeToLock();
	// 리셋 도중에 디렉터가 사라지면 타이머와 함께 복귀도 사라진다.
	if (bResetInFlight && EndPlayReason != EEndPlayReason::LevelTransition
		&& EndPlayReason != EEndPlayReason::EndPlayInEditor
		&& EndPlayReason != EEndPlayReason::Quit)
	{
		AbortCaptureBlackout(TEXT("director destroyed during a capture reset"));
	}
	bResetInFlight = false;
	CapturedPlayer = nullptr;
	Super::EndPlay(EndPlayReason);
}

bool AIGNightLoopDirector::IsMercyNoteVisible() const
{
	return MercyNote
		&& MercyNote->IsVisible()
		&& !MercyNote->bHiddenInGame;
}

FVector AIGNightLoopDirector::GetMercyNoteLocation() const
{
	return MercyNote
		? MercyNote->GetComponentLocation()
		: FVector::ZeroVector;
}

void AIGNightLoopDirector::PrimeMercyNoteCaptureProbe()
{
	CaptureCount = IGNightLoop::MercyNoteCaptureThreshold - 1;
	ListenerCaptureCount = IGNightLoop::MercyNoteCaptureThreshold - 1;
	bMercyNoteRevealed = false;
	bMercyNoteSliding = false;
	GetWorldTimerManager().ClearTimer(MercyNoteRevealTimer);
	SetActorTickEnabled(false);
	if (MercyNote)
	{
		MercyNote->SetWorldLocation(IGNightLoop::MercyNoteStartLocation);
		MercyNote->SetWorldRotation(FRotator(
			0.0f, IGNightLoop::MercyNoteStartYaw, 0.0f));
		MercyNote->SetVisibility(false, true);
		MercyNote->SetHiddenInGame(true, true);
	}
}

void AIGNightLoopDirector::PlayMercyNoteCapturePreview()
{
	if (!MercyNote)
	{
		InitializeMercyNote();
	}
	if (!MercyNote)
	{
		return;
	}
	bMercyNoteRevealed = true;
	GetWorldTimerManager().ClearTimer(MercyNoteRevealTimer);
	BeginMercyNoteSlide();
}

bool AIGNightLoopDirector::InitializeMercyNote()
{
	if (MercyNote)
	{
		return true;
	}

	UStaticMesh* NoteMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/Meshes/SM_CaptureMercyNote.SM_CaptureMercyNote"));
	UMaterialInterface* NoteMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Prototype/Materials/M_CaptureMercyNote."
			"M_CaptureMercyNote"));
	if (!NoteMesh || !NoteMaterial)
	{
		return false;
	}

	MercyNote = NewObject<UStaticMeshComponent>(
		this,
		TEXT("CaptureMercyNote"));
	if (!MercyNote)
	{
		return false;
	}
	MercyNote->SetMobility(EComponentMobility::Movable);
	MercyNote->SetStaticMesh(NoteMesh);
	MercyNote->SetMaterial(0, NoteMaterial);
	MercyNote->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	MercyNote->SetGenerateOverlapEvents(false);
	MercyNote->SetCanEverAffectNavigation(false);
	// 종이처럼 얇은 이동 그림자는 테라조 위에 긴 시간축 잔상을 남긴다.
	// 알베도와 거친 정도의 대비만으로도 종이가 바닥에 붙어 보인다.
	MercyNote->SetCastShadow(false);
	MercyNote->SetReceivesDecals(false);
	MercyNote->SetCullDistance(850.0f);
	MercyNote->SetAffectDistanceFieldLighting(false);
	MercyNote->ComponentTags.AddUnique(FName(TEXT("MissingFloor.CaptureMercyNote")));
	AddInstanceComponent(MercyNote);
	MercyNote->RegisterComponent();
	MercyNote->SetWorldLocation(IGNightLoop::MercyNoteStartLocation);
	MercyNote->SetWorldRotation(FRotator(
		0.0f, IGNightLoop::MercyNoteStartYaw, 0.0f));
	MercyNote->SetVisibility(false, true);
	MercyNote->SetHiddenInGame(true, true);
	return true;
}

void AIGNightLoopDirector::QueueMercyNoteReveal()
{
	// 관리인이나 어둑시니에게 잡힌 것까지 세면 눈으로 찾는 상대 앞에서 「눈이 없어」가 나왔다.
	if (ListenerCaptureCount < IGNightLoop::MercyNoteCaptureThreshold
		|| bMercyNoteRevealed
		|| !MercyNote)
	{
		return;
	}

	bMercyNoteRevealed = true;
	GetWorldTimerManager().SetTimer(
		MercyNoteRevealTimer,
		this,
		&AIGNightLoopDirector::BeginMercyNoteSlide,
		IGNightLoop::MercyNoteRevealDelaySeconds,
		false);
}

void AIGNightLoopDirector::BeginMercyNoteSlide()
{
	if (!MercyNote)
	{
		return;
	}

	MercyNoteSlideElapsedSeconds = 0.0f;
	bMercyNoteSliding = true;
	MercyNote->SetWorldLocation(IGNightLoop::MercyNoteStartLocation);
	MercyNote->SetWorldRotation(FRotator(
		0.0f, IGNightLoop::MercyNoteStartYaw, 0.0f));
	MercyNote->SetHiddenInGame(false, true);
	MercyNote->SetVisibility(true, true);
	SetActorTickEnabled(true);

	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreatePaperDoorSlide(this),
		IGNightLoop::MercyNoteRestLocation + FVector(0.0f, 0.0f, 8.0f),
		0.78f,
		0.96f,
		90.0f,
		720.0f,
		EIGAudioBus::World);
}

void AIGNightLoopDirector::SetMercyNoteAtRest()
{
	if (!MercyNote)
	{
		return;
	}
	bMercyNoteRevealed = true;
	bMercyNoteSliding = false;
	MercyNoteSlideElapsedSeconds = IGNightLoop::MercyNoteSlideSeconds;
	MercyNote->SetWorldLocation(IGNightLoop::MercyNoteRestLocation);
	MercyNote->SetWorldRotation(FRotator(
		0.0f, IGNightLoop::MercyNoteRestYaw, 0.0f));
	MercyNote->ResetSceneVelocity();
	MercyNote->SetHiddenInGame(false, true);
	MercyNote->SetVisibility(true, true);
	SetActorTickEnabled(false);
}

bool AIGNightLoopDirector::SpawnCaptureHandprint(AIGPlayerCharacter* Character)
{
	UWorld* World = GetWorld();
	if (!World || !Character)
	{
		return false;
	}

	const FVector TraceStart = Character->GetActorLocation()
		+ FVector(0.0f, 0.0f, 20.0f);
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(IGMissingFloorCaptureHandprint),
		false,
		Character);
	QueryParams.AddIgnoredActor(this);
	if (const AIGListenerEntity* Entity = ListenerEntity.Get())
	{
		QueryParams.AddIgnoredActor(Entity);
	}

	FHitResult BestHit;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	constexpr int32 WallProbeCount = 8;
	constexpr float WallProbeDistance = 260.0f;
	for (int32 ProbeIndex = 0; ProbeIndex < WallProbeCount; ++ProbeIndex)
	{
		const float AngleRadians = (2.0f * UE_PI * ProbeIndex) / WallProbeCount;
		const FVector Direction(
			FMath::Cos(AngleRadians),
			FMath::Sin(AngleRadians),
			0.0f);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(
				Hit,
				TraceStart,
				TraceStart + Direction * WallProbeDistance,
				ECC_Visibility,
				QueryParams)
			|| FMath::Abs(Hit.ImpactNormal.Z) > 0.35f)
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared(
			TraceStart,
			Hit.ImpactPoint);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestHit = Hit;
		}
	}
	if (!BestHit.bBlockingHit)
	{
		return false;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* HandprintMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Prototype/Materials/M_MissingFloorHandprints."
			"M_MissingFloorHandprints"));
	if (!CubeMesh || !HandprintMaterial)
	{
		return false;
	}

	CaptureHandprints.RemoveAllSwap(
		[](const TObjectPtr<UStaticMeshComponent>& Handprint)
		{
			return !IsValid(Handprint);
		});
	constexpr int32 MaximumCaptureHandprints = 12;
	while (CaptureHandprints.Num() >= MaximumCaptureHandprints)
	{
		if (UStaticMeshComponent* Oldest = CaptureHandprints[0])
		{
			Oldest->DestroyComponent();
		}
		CaptureHandprints.RemoveAt(0);
	}

	UStaticMeshComponent* Handprint = NewObject<UStaticMeshComponent>(
		this,
		MakeUniqueObjectName(
			this,
			UStaticMeshComponent::StaticClass(),
			TEXT("CaptureHandprint")));
	if (!Handprint)
	{
		return false;
	}
	Handprint->SetStaticMesh(CubeMesh);
	Handprint->SetMaterial(0, HandprintMaterial);
	Handprint->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Handprint->SetGenerateOverlapEvents(false);
	Handprint->SetCanEverAffectNavigation(false);
	Handprint->SetCastShadow(false);
	Handprint->SetReceivesDecals(false);
	Handprint->ComponentTags.AddUnique(
		FName(TEXT("MissingFloor.CaptureHandprint")));
	AddInstanceComponent(Handprint);
	Handprint->RegisterComponent();

	const float Variant = static_cast<float>((CaptureCount * 37) % 11) / 10.0f;
	const float TwistDegrees = FMath::Lerp(-7.0f, 8.0f, Variant);
	const FQuat AlignToWall = FQuat::FindBetweenNormals(
		FVector::ForwardVector,
		BestHit.ImpactNormal);
	const FQuat SurfaceTwist(
		BestHit.ImpactNormal,
		FMath::DegreesToRadians(TwistDegrees));
	Handprint->SetWorldLocationAndRotation(
		BestHit.ImpactPoint + BestHit.ImpactNormal * 0.25f,
		SurfaceTwist * AlignToWall);
	Handprint->SetWorldScale3D(FVector(
		0.003f,
		FMath::Lerp(0.52f, 0.62f, Variant),
		FMath::Lerp(0.62f, 0.74f, 1.0f - Variant)));
	CaptureHandprints.Add(Handprint);
	return true;
}

float AIGNightLoopDirector::GetWakeFadeInSeconds() const
{
	if (CaptureCount <= 1)
	{
		return 3.0f;
	}
	if (CaptureCount == 2)
	{
		return 2.2f;
	}
	if (CaptureCount <= 4)
	{
		return 1.4f;
	}
	return 0.4f;
}

float AIGNightLoopDirector::GetWakeEchoSeconds() const
{
	if (CaptureCount <= 1)
	{
		return 0.68f;
	}
	if (CaptureCount == 2)
	{
		return 0.48f;
	}
	if (CaptureCount <= 4)
	{
		return 0.30f;
	}
	return 0.16f;
}

float AIGNightLoopDirector::GetWakeRecoverySeconds() const
{
	// 예전에는 화면이 다 밝아질 때까지 조작을 잠갔다. 첫 포획은 암전까지 합쳐 5초
	// 넘게 손을 놓아야 했고, 잡힐 때마다 그 시간을 다시 기다렸다. 이제 눈을 뜨는
	// 순간부터 둘러볼 수 있다. 바닥에서 깬 몸은 일어서는 동안만 걷지 못한다.
	return FMath::Min(
		FMath::Max(GetWakeFadeInSeconds(), GetWakeEchoSeconds()),
		IGNightLoop::MaxWakeLockSeconds);
}

UIGMissingFloorNarrativeSubsystem* AIGNightLoopDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
}
