#include "Entity/IGNightLoopDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorMercyDirector.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGMissingFloorNightTwoBeatDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IndieGame.h"
#include "Materials/MaterialInterface.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
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
	// §6. 침대에서 눈을 뜬 뒤 조작이 돌아오기까지. 화면은 그 뒤로도 천천히 밝아지지만
	// 그동안 둘러보고 걸을 수 있다. 암전 1.25초와 합쳐 1.6초다.
	constexpr float MaxWakeLockSeconds = 0.35f;
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
		if (CaptureCount >= IGNightLoop::MercyNoteCaptureThreshold)
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
		// 이 포획은 엔딩 C가 단독으로 처리한다. 일반 침대 리셋까지 시작하면
		// 하나의 멀티캐스트에서 서로 충돌하는 두 타임라인이 진행된다.
		return;
	}
	BeginCaptureReset(Character, false);
}

void AIGNightLoopDirector::RequestExternalCapture(APawn* Player)
{
	AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(Player);
	if (bResetInFlight || !Character)
	{
		return;
	}
	// 밤4 가면 대치 중에는 결말 C가 포획을 혼자 맡는다. 다른 괴이가 침대 리셋을
	// 걸면 타임라인 둘이 부딪친다. 괴이 감독도 그동안은 나오지 않는다.
	if (const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		Narrative && Narrative->GetNightIndex() == 4 && Narrative->IsNightFourMaskRunning())
	{
		return;
	}
	BeginCaptureReset(Character, true);
}

void AIGNightLoopDirector::BeginCaptureReset(AIGPlayerCharacter* Character, const bool bExternal)
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	bResetInFlight = true;
	bExternalCaptureInFlight = bExternal;
	LastCaptureSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const int32 PersistedCaptureCount = Narrative
		? Narrative->GetCaptureCount()
		: 0;
	CaptureCount = FMath::Max(CaptureCount + 1, PersistedCaptureCount + 1);
	CapturedPlayer = Character;
	Character->GetCharacterMovement()->StopMovementImmediately();
	if (bExternal)
	{
		// 석고 손자국은 위층 사람의 것이다. 지난번 포획의 몸이 남아 있으면 그 몸이
		// 다시 덮치는 화면이 나오므로 함께 비운다.
		Character->SetCaptureThreat(nullptr);
	}
	else
	{
		SpawnCaptureHandprint(Character);
	}

	// Without an authored wake point the first capture teaches us one: the
	// spot the player stood when the night began is better than nothing,
	// but directors in real maps must call SetWakeTransform.
	if (!bWakeTransformSet)
	{
		WakeTransform = Character->GetActorTransform();
		bWakeTransformSet = true;
	}

	if (APlayerController* Controller =
		Cast<APlayerController>(Character->GetController()))
	{
		// 처음 잡힐 때만 조금 길게 보여 준다. 되풀이될수록 짧게 끊어 같은 장면에
		// 지치지 않게 한다. 끊긴 뒤의 암전과 소리는 그대로다.
		const float CutSeconds = CaptureCount <= 1 ? 0.95f : (CaptureCount == 2 ? 0.75f : 0.55f);
		Character->PlayCaptureFeedback(FadeOutSeconds, CutSeconds);
		Character->DisableInput(Controller);
		if (Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StopCameraFade();
		}
	}
	// 화면은 캐릭터가 끊는다(PlayCaptureFeedback). 몸이 없는 포획처럼 캐릭터가
	// 끊지 못한 경우에만 여기서 짧게 닫는다. 이미 검으면 건드리지 않는다.
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
							.15f, FLinearColor::Black, false, true);
					}
				}
			}
		}), 1.0f, false);

	GetWorldTimerManager().SetTimer(
		ResetTimer,
		this,
		&AIGNightLoopDirector::FinishReset,
		FMath::Max(FadeOutSeconds, 0.05f),
		false);
}

void AIGNightLoopDirector::FinishReset()
{
	GetWorldTimerManager().ClearTimer(CaptureFadeTimer);
	bool bWakeRecoveryScheduled = false;
	AIGPlayerCharacter* Character = CapturedPlayer.Get();
	if (Character)
	{
		Character->TeleportTo(
			WakeTransform.GetLocation(),
			WakeTransform.Rotator(),
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
			Controller->SetControlRotation(WakeTransform.Rotator());
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
				Controller->PlayerCameraManager->StartCameraFade(
					1.0f, 0.0f, GetWakeFadeInSeconds(), FLinearColor::Black,
					/*bShouldFadeAudio=*/false, /*bHoldWhenFinished=*/false);
			}

			// A single duvet settle anchors the teleport at the bed. It must not
			// repeat the two capture knocks or add a failure sting.
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateClothSettle(this),
				WakeTransform.GetLocation(),
				0.72f,
				0.94f,
				75.0f,
				480.0f);
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
		// The tier lives in the narrative snapshot, not on the pawn, so a
		// quit-and-resume cannot hand the player back a patient pursuer.
		// 다른 괴이에게 잡힌 것은 그의 성과가 아니다. 단계는 그대로 두고 자리만 되돌린다.
		if (bExternalCaptureInFlight)
		{
			Entity->ResetToPatrolStart(/*bRaiseAggression=*/false);
		}
		else if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
		{
			Entity->SetAggressionTier(Narrative->RecordCapture());
			Entity->ResetToPatrolStart(/*bRaiseAggression=*/false);
		}
		else
		{
			Entity->ResetToPatrolStart(/*bRaiseAggression=*/true);
		}
	}
	bExternalCaptureInFlight = false;

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
		// §8 비트 2-5: the reset puts her back in her own bed, and that must not
		// count as having carried the ledger home. She owes the night one more
		// trip out of 403 and back.
		TActorIterator<AIGMissingFloorNightTwoBeatDirector> ReturnBeat(World);
		if (ReturnBeat)
		{
			ReturnBeat->NotifyCaptureReset();
		}
		// §8 비트 3-7 owes the same debt, and it also has to put him back in the
		// corridor: the reset sent him to his patrol start along with her.
		TActorIterator<AIGMissingFloorNightThreeDirector> PassBeat(World);
		if (PassBeat)
		{
			PassBeat->NotifyCaptureReset();
		}
	}

	if (bWakeRecoveryScheduled && Character)
	{
		// 눈이 뜨이며 끊겼던 숨을 한 번에 들이켠다. 괴물이 순찰 자리로 돌아간
		// 뒤라야 포획 중의 숨 막음이 풀려 있다.
		if (UIGStressComponent* Stress = Character->GetStress())
		{
			Stress->PlayGasp(/*bIgnoreCooldown=*/true);
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
	if (CaptureCount < IGNightLoop::MercyNoteCaptureThreshold
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
	// 순간부터 둘러보고 걸을 수 있다. 밝아지는 페이드는 그대로 흐른다.
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
