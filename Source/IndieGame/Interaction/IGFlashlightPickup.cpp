#include "Interaction/IGFlashlightPickup.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGStoryStateSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Save/IGSaveSubsystem.h"

AIGFlashlightPickup::AIGFlashlightPickup()
{
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlashlightBody"));
	SetRootComponent(MeshComponent);
	MeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
	MeshComponent->SetGenerateOverlapEvents(false);
	MeshComponent->SetCanEverAffectNavigation(false);
	MeshComponent->SetMobility(EComponentMobility::Movable);
	InteractionHoldDuration = 0.25f;
	InteractionPrompt = NSLOCTEXT("IGFlashlight", "PickupPrompt", "손전등 챙기기");
}

void AIGFlashlightPickup::ConfigureVisuals(UStaticMesh* Mesh, UMaterialInterface* Material, const bool bFallbackCylinder)
{
	MeshComponent->SetStaticMesh(Mesh);
	MeshComponent->EmptyOverrideMaterials();
	if (Material) { MeshComponent->SetMaterial(0, Material); }
	MeshComponent->SetRelativeScale3D(bFallbackCylinder ? FVector(0.05f, 0.05f, 0.17f) : FVector::OneVector);
	if (bFallbackCylinder) { MeshComponent->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f)); }
}

void AIGFlashlightPickup::BeginPlay()
{
	Super::BeginPlay();
	InteractionTag = FGameplayTag::RequestGameplayTag(TEXT("Interaction.Pickup"), false);
	if (UIGStoryStateSubsystem* Story = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGStoryStateSubsystem>() : nullptr)
	{
		Story->OnStoryStateTagChanged.AddUniqueDynamic(this, &ThisClass::HandleStoryStateChanged);
	}
	RefreshOwnership();
}

void AIGFlashlightPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (UIGStoryStateSubsystem* Story = Instance->GetSubsystem<UIGStoryStateSubsystem>())
		{
			Story->OnStoryStateTagChanged.RemoveDynamic(this, &ThisClass::HandleStoryStateChanged);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AIGFlashlightPickup::HandleStoryStateChanged(FGameplayTag StateTag, bool bAdded)
{
	if (StateTag == UIGFlashlightComponent::GetOwnershipTag()) { RefreshOwnership(); }
}

void AIGFlashlightPickup::RefreshOwnership()
{
	const UGameInstance* Instance = GetGameInstance();
	const UIGStoryStateSubsystem* Story = Instance ? Instance->GetSubsystem<UIGStoryStateSubsystem>() : nullptr;
	const bool bOwned = Story && Story->HasState(UIGFlashlightComponent::GetOwnershipTag());
	SetActorHiddenInGame(bOwned);
	SetActorEnableCollision(!bOwned);
	SetInteractionEnabled(!bOwned);
}

bool AIGFlashlightPickup::CanInteract_Implementation(AActor* Interactor) const
{
	const AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(Interactor);
	return Super::CanInteract_Implementation(Interactor) && Character && Character->GetFlashlight()
		&& !Character->GetFlashlight()->IsAvailable();
}

void AIGFlashlightPickup::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	if (!CanInteract_Implementation(Context.Interactor.Get())) { return; }
	AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(Context.Interactor.Get());
	UIGStoryStateSubsystem* Story = GetGameInstance()->GetSubsystem<UIGStoryStateSubsystem>();
	if (!Story || !Story->AddState(UIGFlashlightComponent::GetOwnershipTag())) { return; }
	Character->GetFlashlight()->RefreshOwnership();
	Character->GetFlashlight()->SetOn(true);
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreatePickupRustle(this),
		GetActorLocation(), 0.5f, 1.0f, 90.0f, 600.0f, EIGAudioBus::Player);
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateSwitchClick(this, true),
		GetActorLocation(), 0.3f, 1.0f, 60.0f, 320.0f, EIGAudioBus::Player);
	if (UIGNoiseSubsystem* Noise = GetWorld()->GetSubsystem<UIGNoiseSubsystem>())
	{
		Noise->ReportNoise(GetActorLocation(), 0.14f, Character);
	}
	AIGHorrorHUD::PushThought(this,
		NSLOCTEXT("IGFlashlight", "PickedUp", "작동은 하네. 챙겨 가자."), 3.0f);
	// 주운 직후에도 저장된다. 장 진도나 취침 조건에는 손전등을 넣지 않는다.
	if (UIGSaveSubsystem* Save = GetGameInstance()->GetSubsystem<UIGSaveSubsystem>())
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative =
			GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>();
		const TCHAR* Checkpoint = Narrative && Narrative->IsHourSealed() ? TEXT("Checkpoint.MissingFloor.Night")
			: Narrative && Narrative->GetNightIndex() > 0 ? TEXT("Checkpoint.MissingFloor.Day")
			: TEXT("Checkpoint.MissingFloor.Arrival");
		Save->RequestAutosave(FGameplayTag::RequestGameplayTag(TEXT("Chapter.MissingFloor"), false),
			GetWorld()->GetOutermost()->GetFName(), FGameplayTag::RequestGameplayTag(Checkpoint, false));
	}
}
