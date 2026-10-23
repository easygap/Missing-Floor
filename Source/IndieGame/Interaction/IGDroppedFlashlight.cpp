#include "Interaction/IGDroppedFlashlight.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "EngineUtils.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGPlayerCharacter.h"

namespace IGDroppedFlashlight
{
	// 손에 든 빛(7600cd)보다 낮게 둔다. 바닥에 붙은 렌즈는 가까운 벽과 바닥을 하얗게
	// 태워 손전등보다 그 둘레가 먼저 보였다.
	constexpr float BeamIntensity = 2600.0f;
	constexpr float BeamRange = 1400.0f;
}

AIGDroppedFlashlight::AIGDroppedFlashlight()
{
	PrimaryActorTick.bCanEverTick = false;
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlashlightBody"));
	SetRootComponent(Body);
	Body->SetMobility(EComponentMobility::Movable);
	// 밟아도 걸리지 않는다. 시선 레이만 맞으면 된다.
	Body->SetCollisionProfileName(TEXT("BlockAll"));
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Meshes/SM_PocketFlashlight.SM_PocketFlashlight")))
	{
		Body->SetStaticMesh(Mesh);
	}

	Beam = CreateDefaultSubobject<USpotLightComponent>(TEXT("FlashlightBeam"));
	Beam->SetupAttachment(Body);
	Beam->SetMobility(EComponentMobility::Movable);
	Beam->SetInnerConeAngle(13.0f);
	Beam->SetOuterConeAngle(34.0f);
	Beam->SetAttenuationRadius(IGDroppedFlashlight::BeamRange);
	Beam->SetLightColor(FLinearColor(0.90f, 0.95f, 1.0f));
	Beam->SetSourceRadius(1.4f);
	Beam->SetCastShadows(true);
	Beam->SetIntensity(IGDroppedFlashlight::BeamIntensity);
	Beam->SetVisibility(false);

	InteractionHoldDuration = 0.25f;
	InteractionPrompt = NSLOCTEXT("IGFlashlight", "DroppedPrompt", "줍기");
}

void AIGDroppedFlashlight::Configure(const bool bInLit)
{
	bLit = bInLit;
	// 저작 메시는 렌즈가 +X를 본다(build_pocket_flashlight.py). 빛은 렌즈 끝에서 나간다.
	if (const UStaticMesh* Mesh = Body->GetStaticMesh())
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		Beam->SetRelativeLocation(FVector(Bounds.Origin.X + Bounds.BoxExtent.X + 0.5f, 0.0f, Bounds.Origin.Z));
	}
	Beam->SetVisibility(bLit);
}

void AIGDroppedFlashlight::PlaceOnFloor(const FVector& FloorPoint)
{
	float Lift = 2.0f;
	if (const UStaticMesh* Mesh = Body->GetStaticMesh())
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		Lift = Bounds.BoxExtent.Z - Bounds.Origin.Z;
	}
	SetActorLocation(FloorPoint + FVector(0.0f, 0.0f, Lift + 0.2f));
}

void AIGDroppedFlashlight::BeginPlay()
{
	Super::BeginPlay();
	InteractionTag = FGameplayTag::RequestGameplayTag(TEXT("Interaction.Pickup"), false);
	for (TActorIterator<AIGNightPhaseDirector> It(GetWorld()); It; ++It)
	{
		NightPhase = *It;
		HourActiveHandle = It->OnHourActiveChanged.AddUObject(
			this, &AIGDroppedFlashlight::HandleHourActiveChanged);
		break;
	}
}

void AIGDroppedFlashlight::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AIGNightPhaseDirector* Phase = NightPhase.Get())
	{
		Phase->OnHourActiveChanged.Remove(HourActiveHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AIGDroppedFlashlight::HandleHourActiveChanged(const bool bActive)
{
	// 새벽이 오면 밤은 끝났다. 낮의 복도에 손전등을 남겨 두지 않는다.
	if (!bActive)
	{
		ReturnToOwner(false);
	}
}

void AIGDroppedFlashlight::ReturnToOwner(const bool bSwitchOn)
{
	for (TActorIterator<AIGPlayerCharacter> It(GetWorld()); It; ++It)
	{
		if (UIGFlashlightComponent* Torch = It->GetFlashlight())
		{
			Torch->SetDropped(false);
			if (bSwitchOn)
			{
				Torch->SetOn(true);
			}
		}
		break;
	}
	Destroy();
}

bool AIGDroppedFlashlight::CanInteract_Implementation(AActor* Interactor) const
{
	const AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(Interactor);
	return Super::CanInteract_Implementation(Interactor)
		&& Character && Character->GetFlashlight() && Character->GetFlashlight()->IsDropped();
}

void AIGDroppedFlashlight::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	AActor* Interactor = Context.Interactor.Get();
	if (!CanInteract_Implementation(Interactor))
	{
		return;
	}
	const FVector At = GetActorLocation();
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreatePickupRustle(this),
		At, 0.5f, 1.0f, 90.0f, 600.0f, EIGAudioBus::Player);
	if (UIGNoiseSubsystem* Noise = GetWorld()->GetSubsystem<UIGNoiseSubsystem>())
	{
		Noise->ReportNoise(At, 0.14f, Interactor);
	}
	// 켜진 채 떨어져 있던 것은 켜진 채로 손에 든다.
	ReturnToOwner(bLit);
}
