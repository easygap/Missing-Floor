#include "Entity/IGMissingFloorEvidence.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"

namespace IGMissingFloorEvidenceAudio
{
	/**
	 * The rub swells as the letters come up, but never loudly: §5.1 already
	 * charges 0.25 to the noise bus for this, and the cue's job is to let the
	 * player feel that charge, not to add a second one.
	 */
	constexpr float RubStartVolume = 0.42f;
	constexpr float RubEndVolume = 0.78f;
	constexpr float RubInnerRadius = 80.0f;
	constexpr float RubFalloff = 900.0f;
	constexpr float RubFadeSeconds = 0.12f;
}

AIGMissingFloorEvidence::AIGMissingFloorEvidence()
{
	PresentationMesh = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("Presentation"));
	SetRootComponent(PresentationMesh);
	// Small clues must not catch the player's capsule: block the visibility
	// trace the interaction uses and ignore the pawn entirely.
	PresentationMesh->SetCollisionProfileName(
		UCollisionProfile::BlockAllDynamic_ProfileName);
	PresentationMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	PresentationMesh->SetCanEverAffectNavigation(false);
	// Runtime-spawned evidence is configured after component registration.
	// Keep it movable only while assigning the mesh; SetStaticMesh on a Static
	// component emits a PIE warning and can leave render state stale.
	PresentationMesh->SetMobility(EComponentMobility::Movable);
}

void AIGMissingFloorEvidence::Configure(
	UStaticMesh* Mesh,
	UMaterialInterface* Material,
	const FVector& SizeCentimeters,
	const FText& Prompt,
	const FText& Thought,
	const EIGMissingFloorTruth Truth,
	const EIGMissingFloorSource Source,
	const float HoldSeconds,
	const float NoiseLoudness,
	const bool bPresentationVisible)
{
	// 가시성만 끈다. 충돌 프로필은 그대로라 상호작용이 쓰는 가시성 트레이스는
	// 계속 이 액터에 맞는다.
	PresentationMesh->SetVisibility(bPresentationVisible);
	if (Mesh)
	{
		const FVector AuthoredCenter = GetActorLocation();
		// 가져온 스캔 재질은 Cook 또는 에디터 게임 실행에서 Nanite 셰이더 순열이
		// 항상 준비된다고 보장할 수 없다. 검증된 대체 LOD를 사용해 증거 소품이
		// 사라지거나 회색 기본 재질로 바뀌지 않게 한다.
		PresentationMesh->bDisallowNanite = true;
		PresentationMesh->SetStaticMesh(Mesh);
		// 엔진 기본 도형과 스캔 소품을 저작된 실제 크기에 맞춘다. 모든 원본 메시를
		// 중심이 원점인 100cm 큐브로 가정하면 가져온 증거가 뜨거나 바닥을 뚫고,
		// 그레이박스 크기로 되돌아간다.
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const FVector MeshSize = Bounds.BoxExtent * 2.0f;
		if (MeshSize.GetMin() > KINDA_SMALL_NUMBER)
		{
			// 0은 「저작된 크기 그대로」다. 이 함수는 100cm 큐브를 가정하지
			// 않으므로 (100,100,100)으로는 스케일 1을 얻을 수 없다.
			const FVector Scale = SizeCentimeters.IsNearlyZero()
				? FVector::OneVector
				: SizeCentimeters / MeshSize;
			PresentationMesh->SetRelativeScale3D(Scale);
			// PresentationMesh가 액터 루트이므로 루트에 SetRelativeLocation을 호출하면
			// 액터의 월드 위치가 바뀐다. 저작한 중심은 유지하고, 스케일과 회전을 적용한
			// 메시 원점 오프셋만 액터 위치에서 보정한다.
			const FVector WorldOriginOffset = GetActorRotation().RotateVector(
				Bounds.Origin * Scale);
			SetActorLocation(AuthoredCenter - WorldOriginOffset, false, nullptr,
				ETeleportType::TeleportPhysics);
		}
	}
	if (Material)
	{
		const int32 SlotCount = FMath::Max(PresentationMesh->GetNumMaterials(), 1);
		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			PresentationMesh->SetMaterial(Slot, Material);
		}
	}
	PresentationMesh->SetMobility(EComponentMobility::Static);

	InteractionPrompt = Prompt;
	ExamineThought = Thought;
	EvidenceTruth = Truth;
	EvidenceSource = Source;
	InteractionHoldDuration = FMath::Max(HoldSeconds, 0.0f);
	ExamineNoiseLoudness = FMath::Clamp(NoiseLoudness, 0.0f, 1.0f);
}

void AIGMissingFloorEvidence::SetProgressiveStages(TArray<FText> InStageThoughts)
{
	StageThoughts = MoveTemp(InStageThoughts);
	CompletedStages = 0;
}

void AIGMissingFloorEvidence::SetSustainedRubCue(const bool bEnabled)
{
	bSustainedRubCue = bEnabled;
	if (!bEnabled)
	{
		StopRubCue();
	}
}

bool AIGMissingFloorEvidence::ConfigureProgressReveal(UMaterialInterface* Material,
	const FVector& Offset, const FVector2D& Size, const FText& FinishedPrompt)
{
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!Material || !Plane || RevealSurface) { return false; }
	RevealSurface = NewObject<UStaticMeshComponent>(this, TEXT("ImpressionSurface"));
	RevealSurface->SetupAttachment(PresentationMesh);
	RevealSurface->SetMobility(EComponentMobility::Movable);
	RevealSurface->SetStaticMesh(Plane);
	RevealSurface->SetRelativeLocation(Offset);
	RevealSurface->SetRelativeRotation(FRotator(0, 180, 0));
	RevealSurface->SetRelativeScale3D(FVector(Size.X / 100.f, Size.Y / 100.f, 1));
	RevealSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RevealSurface->SetCastShadow(false);
	RevealSurface->SetAffectDistanceFieldLighting(false);
	RevealSurface->SetCanEverAffectNavigation(false);
	RevealMaterial = UMaterialInstanceDynamic::Create(Material, this);
	RevealSurface->SetMaterial(0, RevealMaterial);
	RevealSurface->RegisterComponent();
	RevealFinishedPrompt = FinishedPrompt;
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (UIGMissingFloorNarrativeSubsystem* Narrative = Instance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
		{
			if (EvidenceSource != EIGMissingFloorSource::None && Narrative->HasSource(EvidenceTruth, EvidenceSource))
			{
				CompletedStages = StageThoughts.Num();
				bExamined = true;
				InteractionPrompt = RevealFinishedPrompt;
			}
		}
	}
	RefreshReveal();
	return true;
}

float AIGMissingFloorEvidence::GetInteractionHoldDuration_Implementation(AActor* Interactor) const
{
	const float Duration = Super::GetInteractionHoldDuration_Implementation(Interactor);
	if (!RevealMaterial) { return Duration; }
	return bExamined ? 0.f : FMath::Max(.05f, Duration * (1.f - PartialStageProgress));
}

void AIGMissingFloorEvidence::RefreshReveal()
{
	RevealFraction = bExamined ? 1.f : (CompletedStages + PartialStageProgress) / FMath::Max(1, StageThoughts.Num() + 1);
	if (RevealMaterial) { RevealMaterial->SetScalarParameterValue(TEXT("Reveal"), RevealFraction); }
}

void AIGMissingFloorEvidence::ReportRubNoise(const FIGInteractionContext& Context)
{
	UWorld* World = GetWorld();
	if (!bSustainedRubCue || bExamined || !World || World->GetTimeSeconds() < NextRubNoiseTime) { return; }
	NextRubNoiseTime = World->GetTimeSeconds() + .35;
	if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
	{
		Noise->ReportNoise(GetActorLocation(), ExamineNoiseLoudness, Context.Interactor);
	}
}

void AIGMissingFloorEvidence::BeginInteraction_Implementation(
	const FIGInteractionContext& Context)
{
	Super::BeginInteraction_Implementation(Context);
	HoldStartPartial = PartialStageProgress;
	ReportRubNoise(Context);
	StartRubCue();
}

void AIGMissingFloorEvidence::UpdateInteraction_Implementation(
	const FIGInteractionContext& Context)
{
	Super::UpdateInteraction_Implementation(Context);
	if (RevealMaterial && !bExamined)
	{
		// 놓았다가 다시 잡아도 이미 칠한 흑연이 사라지지 않는다.
		PartialStageProgress = FMath::Max(PartialStageProgress,
			FMath::Lerp(HoldStartPartial, 1.f, FMath::Clamp(Context.HoldProgress, 0.f, 1.f)));
		RefreshReveal();
	}
	ReportRubNoise(Context);
	if (RubCueComponent)
	{
		// The stroke presses harder as the date surfaces. §21.3 asks for 입력
		// 속도 연동; the shipped interaction is a hold, so its own progress is
		// the only speed there is to follow.
		RubCueComponent->SetVolumeMultiplier(FMath::Lerp(
			IGMissingFloorEvidenceAudio::RubStartVolume,
			IGMissingFloorEvidenceAudio::RubEndVolume,
			FMath::Clamp(Context.HoldProgress, 0.0f, 1.0f)));
	}
}

void AIGMissingFloorEvidence::EndInteraction_Implementation(
	const FIGInteractionContext& Context,
	const EIGInteractionEndReason EndReason)
{
	Super::EndInteraction_Implementation(Context, EndReason);
	// Letting go stops the pencil. Releasing early is a real choice — the noise
	// already happened, and the sound stopping is how the player knows the
	// remaining cost is theirs to avoid.
	StopRubCue();
}

void AIGMissingFloorEvidence::StartRubCue()
{
	if (!bSustainedRubCue || RubCueComponent || (RevealMaterial && bExamined))
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UIGToneSequenceSoundWave* Rub =
		UIGToneSequenceSoundWave::CreateFrottageRub(this);
	if (!Rub)
	{
		return;
	}
	RubCueComponent = NewObject<UAudioComponent>(this);
	RubCueComponent->RegisterComponent();
	RubCueComponent->AttachToComponent(
		GetRootComponent(),
		FAttachmentTransformRules::KeepWorldTransform);
	RubCueComponent->SetWorldLocation(GetActorLocation());
	RubCueComponent->SetSound(Rub);
	RubCueComponent->bAutoDestroy = true;
	// The player's own hand: PLAYER bus, so §10.4 rings the room the ledger is
	// in rather than pretending the rubbing arrived through the building.
	RubCueComponent->AttenuationSettings = IGAudio::MakeAttenuation(
		this,
		IGMissingFloorEvidenceAudio::RubInnerRadius,
		IGMissingFloorEvidenceAudio::RubFalloff,
		EIGAudioBus::Player);
	RubCueComponent->bAllowSpatialization = true;
	RubCueComponent->SetVolumeMultiplier(
		IGMissingFloorEvidenceAudio::RubStartVolume);
	if (UIGMissingFloorAudioSubsystem* AudioDirector =
		World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
	{
		AudioDirector->RegisterComponent(RubCueComponent, EIGAudioBus::Player);
	}
	RubCueComponent->Play();
}

void AIGMissingFloorEvidence::StopRubCue()
{
	if (!RubCueComponent)
	{
		return;
	}
	// A short fade, not a cut: graphite lifting off paper has a tail, and a hard
	// stop on a loop clicks.
	RubCueComponent->FadeOut(IGMissingFloorEvidenceAudio::RubFadeSeconds, 0.0f);
	RubCueComponent = nullptr;
}

void AIGMissingFloorEvidence::CompleteInteraction_Implementation(
	const FIGInteractionContext& Context)
{
	Super::CompleteInteraction_Implementation(Context);
	LastInteractionPoint = Context.HitResult.bBlockingHit
		? FVector(Context.HitResult.ImpactPoint)
		: GetActorLocation();
	const bool bRereadingReveal = RevealMaterial && bExamined;

	// Interim stages: work done, noise made, nothing filed yet. The sound
	// costs the same whether or not this pass finished the rubbing — that
	// is the risk the design charges for evidence (§7).
	if (CompletedStages < StageThoughts.Num())
	{
		const FText& InterimThought = StageThoughts[CompletedStages];
		++CompletedStages;
		PartialStageProgress = 0.f;
		RefreshReveal();
		if (UWorld* World = GetWorld())
		{
			if (ExamineNoiseLoudness > 0.0f)
			{
				if (UIGNoiseSubsystem* Noise =
					World->GetSubsystem<UIGNoiseSubsystem>())
				{
					Noise->ReportNoise(
						GetActorLocation(),
						ExamineNoiseLoudness,
						Context.Interactor);
				}
			}
		}
		if (!InterimThought.IsEmpty())
		{
			AIGHorrorHUD::PushThought(this, InterimThought, 3.2f);
		}
		return;
	}

	bExamined = true;
	PartialStageProgress = 0.f;
	RefreshReveal();
	if (RevealMaterial) { InteractionPrompt = RevealFinishedPrompt; }

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UIGMissingFloorNarrativeSubsystem* Narrative =
				GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
			{
				// Repeat examinations are harmless: AddUnique means a record is
				// filed once, and confirmation is recomputed from the records.
				Narrative->RegisterTruthSource(EvidenceTruth, EvidenceSource);
			}
		}
		if (ExamineNoiseLoudness > 0.0f && !bRereadingReveal)
		{
			if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
			{
				Noise->ReportNoise(
					GetActorLocation(),
					ExamineNoiseLoudness,
					Context.Interactor);
			}
		}
	}

	if (!ExamineThought.IsEmpty())
	{
		AIGHorrorHUD::PushThought(this, ExamineThought, 3.6f);
	}

	OnExamined.Broadcast(this);
}
