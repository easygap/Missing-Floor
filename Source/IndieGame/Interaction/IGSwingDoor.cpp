#include "Interaction/IGSwingDoor.h"
#include "Core/IGPlayRecord.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGNoiseSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/IGStoryHelpers.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGOnboardingMemory.h"

AIGSwingDoor::AIGSwingDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	HingeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HingeRoot"));
	SetRootComponent(HingeRoot);

	DoorPivot = CreateDefaultSubobject<USceneComponent>(TEXT("DoorPivot"));
	DoorPivot->SetupAttachment(HingeRoot);

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(DoorPivot);
	DoorMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	DoorMesh->SetGenerateOverlapEvents(false);
	DoorMesh->SetCanEverAffectNavigation(false);
	DoorMesh->SetMobility(EComponentMobility::Movable);

	HandleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandleMesh"));
	HandleMesh->SetupAttachment(DoorPivot);
	HandleMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	HandleMesh->SetGenerateOverlapEvents(false);
	HandleMesh->SetCanEverAffectNavigation(false);
	HandleMesh->SetMobility(EComponentMobility::Movable);

	OpenPrompt = NSLOCTEXT("IGSwingDoor", "OpenPrompt", "문 열기");
	ClosePrompt = NSLOCTEXT("IGSwingDoor", "ClosePrompt", "문 닫기");
}

void AIGSwingDoor::ConfigurePrototypeVisuals(
	UStaticMesh* CubeMesh,
	UMaterialInterface* DoorMaterial,
	UMaterialInterface* HandleMaterial,
	const FVector& PanelSize)
{
	LeafSize = PanelSize;
	if (!CubeMesh)
	{
		return;
	}

	DoorMesh->SetStaticMesh(CubeMesh);
	DoorMesh->SetMaterial(0, DoorMaterial);
	DoorMesh->SetRelativeLocation(FVector(0.0f, PanelSize.Y * 0.5f, PanelSize.Z * 0.5f));
	DoorMesh->SetRelativeScale3D(PanelSize / 100.0f);

	// Lever handle bar on a rose plate.
	HandleMesh->SetStaticMesh(CubeMesh);
	HandleMesh->SetMaterial(0, HandleMaterial);
	HandleMesh->SetRelativeLocation(FVector(
		-PanelSize.X * 0.5f - 4.0f,
		PanelSize.Y - 16.0f,
		PanelSize.Z * 0.47f));
	HandleMesh->SetRelativeScale3D(FVector(0.028f, 0.15f, 0.03f));

	// Korean fire-door detailing: recessed panel grooves, digital lock,
	// peephole, hinge knuckles and a door closer arm up top.
	int32 DetailIndex = 0;
	auto AddDetail = [this, CubeMesh, &DetailIndex](
		UMaterialInterface* Material, const FVector& Center, const FVector& Size,
		UStaticMesh* MeshOverride = nullptr)
	{
		UStaticMeshComponent* Detail = NewObject<UStaticMeshComponent>(
			this,
			*FString::Printf(TEXT("SwingDoorDetail%d"), DetailIndex++));
		Detail->SetupAttachment(DoorPivot);
		Detail->SetStaticMesh(MeshOverride ? MeshOverride : CubeMesh);
		Detail->SetMaterial(0, Material);
		Detail->SetRelativeLocation(Center);
		Detail->SetRelativeScale3D(Size / 100.0f);
		Detail->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Detail->SetGenerateOverlapEvents(false);
		Detail->SetCanEverAffectNavigation(false);
		Detail->SetMobility(EComponentMobility::Movable);
		Detail->RegisterComponent();
		return Detail;
	};

	// A Korean unit door is a flat charcoal slab broken by ONE brushed band
	// running its full height, inset from the handle edge, with small dark
	// squares punched down it. Horizontal grooves would read as a Western
	// panelled door, which is why there are none.
	const float FaceX = -PanelSize.X * 0.5f - 0.7f;
	const float BandY = PanelSize.Y * 0.66f;
	AddDetail(HandleMaterial,
		FVector(FaceX, BandY, PanelSize.Z * 0.5f),
		FVector(0.8f, 13.0f, PanelSize.Z - 16.0f));
	for (int32 SquareIndex = 0; SquareIndex < 5; ++SquareIndex)
	{
		AddDetail(DoorMaterial,
			FVector(FaceX - 0.8f, BandY, PanelSize.Z * (0.18f + 0.16f * SquareIndex)),
			FVector(0.7f, 5.0f, 5.0f));
	}
	// Digital door lock above the handle, with a faint standby diode.
	AddDetail(HandleMaterial,
		FVector(FaceX - 1.8f, PanelSize.Y - 16.0f, PanelSize.Z * 0.58f),
		FVector(4.5f, 9.0f, 16.0f));
	AddDetail(DoorMaterial,
		FVector(FaceX - 4.2f, PanelSize.Y - 16.0f, PanelSize.Z * 0.62f),
		FVector(0.6f, 5.5f, 7.0f));
	// Peephole.
	AddDetail(HandleMaterial,
		FVector(FaceX, PanelSize.Y * 0.5f, PanelSize.Z * 0.78f),
		FVector(1.2f, 3.0f, 3.0f));
	// Hinge knuckles on the pivot edge.
	AddDetail(HandleMaterial,
		FVector(0.0f, 1.5f, PanelSize.Z * 0.85f), FVector(4.5f, 4.5f, 9.0f));
	AddDetail(HandleMaterial,
		FVector(0.0f, 1.5f, PanelSize.Z * 0.15f), FVector(4.5f, 4.5f, 9.0f));
	// Door closer arm on the interior top corner.
	AddDetail(HandleMaterial,
		FVector(PanelSize.X * 0.5f + 2.5f, 18.0f, PanelSize.Z - 8.0f),
		FVector(5.0f, 24.0f, 6.0f));
	AddDetail(HandleMaterial,
		FVector(PanelSize.X * 0.5f + 5.0f, 34.0f, PanelSize.Z - 5.0f),
		FVector(2.5f, 30.0f, 2.5f));
}

void AIGSwingDoor::ConfigureFramedGlassVisuals(
	UStaticMesh* CubeMesh,
	UMaterialInterface* GlassMaterial,
	UMaterialInterface* FrameMaterial,
	const FVector& PanelSize)
{
	bFramedGlass = true;
	LeafSize = PanelSize;
	if (!CubeMesh)
	{
		return;
	}

	// Two glass insets in an aluminum frame with a mid rail and push bar.
	DoorMesh->SetStaticMesh(CubeMesh);
	DoorMesh->SetMaterial(0, GlassMaterial);
	// The surrounding frame provides contact shadow; casting the full
	// translucent slab as opaque made the open lobby doorway look boarded up.
	DoorMesh->SetCastShadow(false);
	DoorMesh->SetRelativeLocation(FVector(0.0f, PanelSize.Y * 0.5f, PanelSize.Z * 0.5f));
	DoorMesh->SetRelativeScale3D(
		FVector(PanelSize.X * 0.55f, PanelSize.Y - 14.0f, PanelSize.Z - 14.0f) / 100.0f);

	HandleMesh->SetStaticMesh(CubeMesh);
	HandleMesh->SetMaterial(0, FrameMaterial);
	HandleMesh->SetRelativeLocation(FVector(
		-PanelSize.X * 0.5f - 3.5f,
		PanelSize.Y * 0.5f,
		PanelSize.Z * 0.47f));
	HandleMesh->SetRelativeScale3D(FVector(0.03f, (PanelSize.Y - 26.0f) / 100.0f, 0.045f));

	struct FFramePiece
	{
		FVector Center;
		FVector Size;
	};
	const FFramePiece FramePieces[] = {
		// Hinge stile, lock stile, bottom rail, top rail, mid rail.
		{{0.0f, 4.0f, PanelSize.Z * 0.5f}, {PanelSize.X, 8.0f, PanelSize.Z}},
		{{0.0f, PanelSize.Y - 4.0f, PanelSize.Z * 0.5f}, {PanelSize.X, 8.0f, PanelSize.Z}},
		{{0.0f, PanelSize.Y * 0.5f, 5.0f}, {PanelSize.X, PanelSize.Y, 10.0f}},
		{{0.0f, PanelSize.Y * 0.5f, PanelSize.Z - 4.0f}, {PanelSize.X, PanelSize.Y, 8.0f}},
		{{0.0f, PanelSize.Y * 0.5f, PanelSize.Z * 0.42f}, {PanelSize.X + 1.0f, PanelSize.Y, 9.0f}},
	};
	int32 FramePieceIndex = 0;
	for (const FFramePiece& Piece : FramePieces)
	{
		UStaticMeshComponent* Frame = NewObject<UStaticMeshComponent>(
			this,
			*FString::Printf(TEXT("DoorFrame_%d"), FramePieceIndex++));
		Frame->SetupAttachment(DoorPivot);
		Frame->SetStaticMesh(CubeMesh);
		Frame->SetMaterial(0, FrameMaterial);
		Frame->SetRelativeLocation(Piece.Center);
		Frame->SetRelativeScale3D(Piece.Size / 100.0f);
		Frame->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Frame->SetGenerateOverlapEvents(false);
		Frame->SetCanEverAffectNavigation(false);
		Frame->SetMobility(EComponentMobility::Movable);
		Frame->RegisterComponent();
	}
}

void AIGSwingDoor::SetLeverMesh(
	UStaticMesh* LeverMesh,
	UMaterialInterface* Material,
	const FVector& PanelSize)
{
	if (!LeverMesh)
	{
		return;
	}

	// Pitch 90 lays the rose flat against the outer (-X) face; the lever arm
	// then sweeps back toward the hinge the way a real lever does.
	HandleMesh->SetStaticMesh(LeverMesh);
	HandleMesh->SetMaterial(0, Material);
	HandleMesh->SetRelativeLocation(FVector(
		-PanelSize.X * 0.5f,
		PanelSize.Y - 11.0f,
		PanelSize.Z * 0.47f));
	HandleMesh->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	HandleMesh->SetRelativeScale3D(FVector::OneVector);
}

void AIGSwingDoor::ConfigureAuthoredLeaf(
	UStaticMesh* LeafMesh, UStaticMesh* HardwareMesh, const FVector& PanelSize)
{
	LeafSize = PanelSize;
	if (!LeafMesh)
	{
		return;
	}

	DoorMesh->SetStaticMesh(LeafMesh);
	DoorMesh->EmptyOverrideMaterials();
	DoorMesh->SetRelativeLocation(FVector(0.0f, PanelSize.Y * 0.5f, 0.0f));
	DoorMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	DoorMesh->SetRelativeScale3D(FVector::OneVector);
	// 레버·도어락은 문짝과 원점이 같은 별도 메시다. 문짝과 같은 자세로 달면
	// 같은 축으로 돈다. 구운 재질을 쓰므로 상자 손잡이의 덮개 재질은 비운다.
	// 철물 메시가 없으면 상자 손잡이도 비운다 — 문짝 메시와 맞지 않는다.
	HandleMesh->EmptyOverrideMaterials();
	HandleMesh->SetStaticMesh(HardwareMesh);
	HandleMesh->SetRelativeLocation(DoorMesh->GetRelativeLocation());
	HandleMesh->SetRelativeRotation(DoorMesh->GetRelativeRotation());
	HandleMesh->SetRelativeScale3D(FVector::OneVector);
	HandleMesh->SetVisibility(HardwareMesh != nullptr, true);
}

void AIGSwingDoor::SetRequirements(TArray<FIGDoorRequirement>&& InRequirements)
{
	Requirements = MoveTemp(InRequirements);
}

void AIGSwingDoor::BeginPlay()
{
	Super::BeginPlay();

	if (!InteractionTag.IsValid())
	{
		InteractionTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interaction.Door")), false);
	}
}

void AIGSwingDoor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const bool bFinished = DoorAnimation.Advance(DeltaSeconds);
	DoorPivot->SetRelativeRotation(FRotator(0.0f, DoorAnimation.CurrentValue, 0.0f));

	if (!bOpen)
	{
		const APlayerController* PlayerController =
			GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		FVector ClosestPoint = FVector::ZeroVector;
		const float DistanceToLeaf = Pawn
			? DoorMesh->GetDistanceToCollision(Pawn->GetActorLocation(), ClosestPoint)
			: -1.0f;
		if (DistanceToLeaf >= 0.0f && DistanceToLeaf < 45.0f)
		{
			// 문이 닫힐 자리에 서 있으면 다시 연다. 회전하는 문짝은 스윕이
			// 안 되므로 움직이는 동안 캐릭터를 밀지 않게 해 둔다.
			bOpen = true;
			bSuppressNextCloseThud = true;
			DoorAnimation.Begin(
				DoorPivot->GetRelativeRotation().Yaw,
				OpenYaw,
				FMath::Max(0.3f, SwingDuration * 0.55f));
			UpdateLeafCollision();
			return;
		}
	}

	if (bFinished)
	{
		UpdateLeafCollision();
		SetActorTickEnabled(false);
		if (!bOpen && !bSuppressNextCloseThud)
		{
			// 문짝이 문틀에 닿는 자리에서 닫힌다. 조용히 닫으면 걸쇠만 작고 가볍게
			// 물리고, 알루미늄 유리문은 철문보다 높고 얇게 운다.
			const bool bEasedShut = CloseThudVolume < 0.5f;
			IGAudio::SpawnOneShotFromActorAt(
				this,
				IGAudio::SampleOr(
					TEXT("Door_Steel_Close"),
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateDoorThud(this); }),
				GetStrikeSoundLocation(),
				CloseThudVolume * (bFramedGlass ? 0.8f : 1.0f),
				GetDoorVoice()
					* (bFramedGlass ? 1.12f : 1.0f)
					* (bEasedShut ? 1.03f : 0.98f)
					* FMath::FRandRange(0.97f, 1.03f));
		}
		bSuppressNextCloseThud = false;
	}
}

bool AIGSwingDoor::IsLocked() const
{
	return FindUnmetRequirement() != nullptr;
}

const FIGDoorRequirement* AIGSwingDoor::FindUnmetRequirement() const
{
	for (const FIGDoorRequirement& Requirement : Requirements)
	{
		if (Requirement.RequiredState.IsValid()
			&& !IGStory::HasState(this, Requirement.RequiredState))
		{
			return &Requirement;
		}
	}
	return nullptr;
}

bool AIGSwingDoor::CanInteract_Implementation(AActor* Interactor) const
{
	// 움직이는 동안도 초점을 놓지 않는다. 놓으면 1.8초 동안 프롬프트가 꺼지고
	// 조준점이 회색으로 돌아갔다가 반대 동사로 튀어 올라온다. 입력은 아래에서
	// 애니메이션 중이면 그냥 흘린다.
	return Super::CanInteract_Implementation(Interactor);
}

FText AIGSwingDoor::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	if (const FIGDoorRequirement* Unmet = FindUnmetRequirement(); Unmet && !bOpen)
	{
		return Unmet->LockedPrompt.IsEmpty() ? OpenPrompt : Unmet->LockedPrompt;
	}
	const FText& BasePrompt = bOpen ? ClosePrompt : OpenPrompt;
	if (QuietOpenHoldSeconds <= 0.0f)
	{
		return BasePrompt;
	}
	// The quiet/loud verb pair only matters if the player can discover it.
	// One short suffix teaches it everywhere without a tutorial screen.
	// 한 번 조용히 열어 본 뒤로는 모든 문에서 방법 설명을 뗀다.
	if (IGOnboardingMemory::IsQuietDoorLearned())
	{
		return BasePrompt;
	}
	return FText::Format(
		NSLOCTEXT("IGSwingDoor", "HoldHintFormat", "{0} (길게 누르면 조용히)"),
		BasePrompt);
}

float AIGSwingDoor::GetInteractionHoldDuration_Implementation(AActor* Interactor) const
{
	// A door that will not open has nothing to ease: press it and let it
	// rattle at once, rather than making the player fill a progress bar to
	// learn that it is sealed.
	if (!bOpen && FindUnmetRequirement() != nullptr)
	{
		return 0.0f;
	}
	return QuietOpenHoldSeconds;
}

void AIGSwingDoor::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	Super::CompleteInteraction_Implementation(Context);
	if (DoorAnimation.bActive)
	{
		return;
	}

	if (!bOpen)
	{
		if (const FIGDoorRequirement* Unmet = FindUnmetRequirement())
		{
			// 잠긴 문은 걸쇠가 운다. 붙들린 문은 걸쇠가 풀리는데도 문짝이 서기만
			// 한다. 여기서 열쇠 소리를 내면 흔한 잠긴 문이 된다.
			USoundBase* TriedSound = Unmet->bHeldShut
				? static_cast<USoundBase*>(UIGToneSequenceSoundWave::CreateHeldDoorPush(this))
				: IGAudio::SampleOr(
					TEXT("Lock_Rattle"),
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateLockedRattle(this); });
			IGAudio::SpawnOneShotFromActorAt(
				this,
				TriedSound,
				GetLatchSoundLocation(),
				Unmet->bHeldShut ? 0.8f : 0.9f,
				FMath::FRandRange(0.96f, 1.04f));
			// Yanking a sealed door is the loudest thing a locked door does.
			// The hour has to be a place where trying the exit costs you.
			ReportSwingNoise(NormalSwingLoudness);
			if (!Unmet->LockedThought.IsEmpty())
			{
				AIGHorrorHUD::PushThought(this, Unmet->LockedThought, 3.2f);
			}
			OnLockedAttempt.Broadcast(this);
			return;
		}
	}

	// Reaching completion means the hold ran its course (or a director/capture
	// tour called this directly): the careful, quiet swing. 닫힐 때도 경첩은 운다.
	// 플레이어가 끝까지 눌러 연 경우만 익힌 것으로 센다. 연출이 대신 연 문은 아니다.
	if (const APawn* InteractorPawn = Cast<APawn>(Context.Interactor.Get());
		InteractorPawn && InteractorPawn->IsPlayerControlled() && Context.HeldDuration > 0.0f)
	{
		IGOnboardingMemory::MarkQuietDoorLearned();
		UIGPlayRecordSubsystem::Note(this, TEXT("quiet_door"));
	}
	ReleaseLatchForOpening();
	BeginSwing(
		!bOpen,
		true,
		false,
		QuietSwingLoudness,
		QuietSwingDurationScale);
}

void AIGSwingDoor::EndInteraction_Implementation(
	const FIGInteractionContext& Context,
	const EIGInteractionEndReason EndReason)
{
	Super::EndInteraction_Implementation(Context, EndReason);

	// Only a deliberate early release is a tap. FocusLost fires from merely
	// looking away mid-hold and Cancelled from cinematics and input locks —
	// neither should slam a door.
	if (EndReason != EIGInteractionEndReason::Released)
	{
		return;
	}
	if (QuietOpenHoldSeconds <= 0.0f || DoorAnimation.bActive)
	{
		return;
	}
	if (!bOpen && FindUnmetRequirement() != nullptr)
	{
		// Locked: the press already rattled through CompleteInteraction.
		return;
	}

	ReleaseLatchForOpening();
	BeginSwing(!bOpen, true, false, NormalSwingLoudness, 1.0f);
}

FVector AIGSwingDoor::GetLatchSoundLocation() const
{
	// 문짝의 좌표계는 경첩 축이 원점이고 문짝이 +Y로 뻗으며 두께는 X 가운데다.
	// 문짝을 따라 돌므로 열린 문의 걸쇠도 제자리에서 운다.
	return DoorPivot->GetComponentTransform().TransformPosition(
		FVector(0.0f, LeafSize.Y - 12.0f, LeafSize.Z * 0.47f));
}

FVector AIGSwingDoor::GetHingeSoundLocation() const
{
	return DoorPivot->GetComponentTransform().TransformPosition(
		FVector(0.0f, 8.0f, LeafSize.Z * 0.8f));
}

FVector AIGSwingDoor::GetStrikeSoundLocation() const
{
	return DoorPivot->GetComponentTransform().TransformPosition(
		FVector(0.0f, LeafSize.Y - 8.0f, LeafSize.Z * 0.5f));
}

float AIGSwingDoor::GetDoorVoice() const
{
	return 0.96f
		+ 0.08f * static_cast<float>(GetTypeHash(GetFName()) % 1000u) / 999.0f;
}

void AIGSwingDoor::UpdateLeafCollision()
{
	DoorMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	if (bOpen || DoorAnimation.bActive)
	{
		// 열린 문도 바라보고 닫을 수 있어야 한다. 시선 판정은 남기고,
		// 통행과 회전 중 끼임을 막기 위해 캐릭터 충돌만 끈다.
		DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		DoorMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
}

void AIGSwingDoor::ForceOpenState(const bool bInOpen)
{
	DoorAnimation = FIGDoorAnimation();
	bOpen = bInOpen;
	bEverOpened = bEverOpened || bOpen;
	bSuppressNextCloseThud = false;
	UpdateLeafCollision();
	DoorPivot->SetRelativeRotation(FRotator(0.0f, bOpen ? OpenYaw : 0.0f, 0.0f));
	SetActorTickEnabled(false);
}

bool AIGSwingDoor::BeginScriptedSwing(
	const bool bInOpen,
	const bool bPlayCreak,
	const bool bSuppressCloseThud)
{
	// An authored swing is the building moving, not the player: it still
	// sounds, at the ordinary loudness.
	return BeginSwing(
		bInOpen,
		bPlayCreak,
		bSuppressCloseThud,
		NormalSwingLoudness,
		1.0f);
}

bool AIGSwingDoor::BeginScriptedSwingWithLoudness(
	const bool bInOpen,
	const float Loudness,
	const float DurationScale,
	const bool bPlayCreak)
{
	return BeginSwing(
		bInOpen,
		bPlayCreak,
		false,
		FMath::Clamp(Loudness, 0.0f, 1.0f),
		FMath::Max(DurationScale, 0.05f));
}

void AIGSwingDoor::SetLatched(const bool bInLatched, const bool bPlaySound)
{
	if (bLatched == bInLatched)
	{
		return;
	}
	bLatched = bInLatched;
	OnLatchChanged.Broadcast();
	if (!bPlaySound)
	{
		return;
	}
	// 철제 걸쇠가 받침에 끼거나 빠지는 짧은 쇳소리. 거는 쪽이 조금 높다.
	IGAudio::SpawnOneShotFromActorAt(
		this,
		IGAudio::SampleOr(
			TEXT("Lock_Open"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSwitchClick(this, bLatched); }),
		GetLatchSoundLocation() + FVector(0.0f, 0.0f, 22.0f),
		0.45f,
		bLatched ? 1.24f : 1.12f,
		90.0f,
		700.0f);
	ReportSwingNoise(0.05f);
}

void AIGSwingDoor::ReleaseLatchForOpening()
{
	if (!bOpen && bLatched)
	{
		SetLatched(false, true);
	}
}

bool AIGSwingDoor::BeginSwing(
	const bool bInOpen,
	const bool bPlayCreak,
	const bool bSuppressCloseThud,
	const float Loudness,
	const float DurationScale)
{
	if (DoorAnimation.bActive || bOpen == bInOpen)
	{
		return false;
	}

	bOpen = bInOpen;
	bSuppressNextCloseThud = !bOpen && bSuppressCloseThud;
	CloseThudVolume = DurationScale > 1.0f ? 0.22f : 0.9f;
	DoorAnimation.Begin(
		DoorPivot->GetRelativeRotation().Yaw,
		bOpen ? OpenYaw : 0.0f,
		SwingDuration * FMath::Max(DurationScale, 0.05f));
	UpdateLeafCollision();
	SetActorTickEnabled(true);

	// 문마다 목소리가 조금 다르고, 같은 문도 매번 똑같이 울지는 않는다. 403호 문과
	// 관리실 문이 같은 녹음이어도 같은 문으로 들리지 않게.
	const float DoorVoice = GetDoorVoice();
	if (bPlayCreak && bOpen && bFramedGlass)
	{
		// 유리문에는 강철 걸쇠도 나무 경첩도 없다. 알루미늄 푸시바가 끝까지 들어가는
		// 딸깍 하나다. 강철 걸쇠 녹음을 높고 작게 쓴다.
		if (USoundBase* PushBar = IGAudio::Sample(TEXT("Door_Steel_Open")))
		{
			IGAudio::SpawnOneShotFromActorAt(
				this,
				PushBar,
				GetLatchSoundLocation(),
				DurationScale > 1.0f ? 0.22f : 0.40f,
				1.12f * FMath::FRandRange(0.97f, 1.03f),
				120.0f,
				900.0f);
		}
	}
	else if (bPlayCreak && bOpen)
	{
		// 손잡이가 경첩보다 먼저다. 잠겨 있던 문은 첫 개방에서 자물쇠가 한 번
		// 돌아가고, 그 뒤로는 걸쇠만 풀린다. 천천히 열면 걸쇠도 조심스럽다.
		const FVector LatchLocation = GetLatchSoundLocation();
		if (!bEverOpened && Requirements.Num() > 0)
		{
			if (USoundBase* Unlock = IGAudio::Sample(TEXT("Lock_Open")))
			{
				IGAudio::SpawnOneShotFromActorAt(
					this, Unlock, LatchLocation, 0.7f, DoorVoice, 120.0f, 900.0f);
			}
		}
		if (USoundBase* Latch = IGAudio::Sample(TEXT("Door_Steel_Open")))
		{
			IGAudio::SpawnOneShotFromActorAt(
				this,
				Latch,
				LatchLocation,
				DurationScale > 1.0f ? 0.30f : 0.55f,
				(DurationScale > 1.0f ? 0.94f : 1.0f) * DoorVoice * FMath::FRandRange(0.98f, 1.02f),
				120.0f,
				900.0f);
		}
	}
	bEverOpened = bEverOpened || bOpen;

	if (bPlayCreak && !bFramedGlass)
	{
		// A slow leaf creaks more softly than a shoved one. 닫힐 때는 반대로
		// 내려가는 삐걱이고, 열릴 때보다 조금 작다 — 걸쇠 소리가 뒤에 따로 온다.
		// 삐걱은 문짝이 매달린 경첩 쪽 위에서 운다.
		IGAudio::SpawnOneShotFromActorAt(
			this,
			IGAudio::SampleOr(
				bOpen ? TEXT("Door_Creak_0") : TEXT("Door_Creak_1"),
				[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateDoorCreak(this, !bOpen); }),
			GetHingeSoundLocation(),
			(DurationScale > 1.0f ? 0.45f : 0.8f) * (bOpen ? 1.0f : 0.7f) * FMath::FRandRange(0.9f, 1.0f),
			// 천천히 열면 낮고 길게 운다.
			(DurationScale > 1.0f ? 0.82f : 1.0f) * DoorVoice * FMath::FRandRange(0.95f, 1.05f));
	}

	// One report per committed swing, at the start of motion: this funnel is
	// the only exactly-once, tick-free moment, and every caller passes through
	// it — player tap, player hold, and every authored swing.
	ReportSwingNoise(Loudness);

	return true;
}

void AIGSwingDoor::ReportSwingNoise(const float Loudness) const
{
	if (Loudness <= 0.0f)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(
				DoorMesh ? DoorMesh->GetComponentLocation() : GetActorLocation(),
				Loudness,
				const_cast<AIGSwingDoor*>(this));
		}
	}
}
