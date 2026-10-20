#include "Interaction/IGLeftoverTarget.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Player/IGHorrorHUD.h"

AIGLeftoverTarget::AIGLeftoverTarget()
{
	Focus = CreateDefaultSubobject<UBoxComponent>(TEXT("Focus"));
	SetRootComponent(Focus);
	Focus->SetMobility(EComponentMobility::Movable);
	Focus->InitBoxExtent(FVector(10.0f));
	// 조준 트레이스(가시성)만 받는다. 몸은 지나간다.
	Focus->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Focus->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Focus->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Focus->SetCanEverAffectNavigation(false);
	Focus->SetHiddenInGame(true);
	Focus->SetGenerateOverlapEvents(false);
}

void AIGLeftoverTarget::Configure(
	const FVector& FocusExtent,
	const FText& Prompt,
	const float HoldSeconds,
	const float InNoiseLoudness)
{
	Focus->SetBoxExtent(FocusExtent.ComponentMax(FVector(1.0f)));
	InteractionPrompt = Prompt;
	InteractionHoldDuration = FMath::Max(0.0f, HoldSeconds);
	NoiseLoudness = FMath::Clamp(InNoiseLoudness, 0.0f, 1.0f);
}

UStaticMeshComponent* AIGLeftoverTarget::SetVisual(
	UStaticMesh* Mesh,
	const FTransform& Relative,
	UMaterialInterface* Material)
{
	if (!Visual)
	{
		Visual = NewObject<UStaticMeshComponent>(this, TEXT("Visual"));
		Visual->SetupAttachment(Focus);
		Visual->SetMobility(EComponentMobility::Movable);
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->SetCanEverAffectNavigation(false);
		Visual->SetGenerateOverlapEvents(false);
		Visual->bDisallowNanite = true;
		Visual->RegisterComponent();
	}
	Visual->SetStaticMesh(Mesh);
	Visual->SetRelativeTransform(Relative);
	if (Material)
	{
		Visual->SetMaterial(0, Material);
	}
	return Visual;
}

float AIGLeftoverTarget::GetInteractionHoldDuration_Implementation(AActor* Interactor) const
{
	return FMath::Max(0.0f, InteractionHoldDuration);
}

void AIGLeftoverTarget::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	Super::CompleteInteraction_Implementation(Context);
	bCompletedThisPress = true;
	ReportNoise(NoiseLoudness);
	if (!Thought.IsEmpty())
	{
		AIGHorrorHUD::PushThought(this, Thought, 3.6f);
	}
	OnCompleted.Broadcast(this);
}

void AIGLeftoverTarget::EndInteraction_Implementation(
	const FIGInteractionContext& Context,
	const EIGInteractionEndReason EndReason)
{
	Super::EndInteraction_Implementation(Context, EndReason);
	const bool bTap = bTapAction && !bCompletedThisPress
		&& EndReason == EIGInteractionEndReason::Released
		&& InteractionHoldDuration > 0.0f;
	bCompletedThisPress = false;
	if (bTap)
	{
		ReportNoise(NoiseLoudness * 0.6f);
		OnTapped.Broadcast(this);
	}
}

void AIGLeftoverTarget::ReportNoise(const float Loudness) const
{
	if (Loudness <= 0.0f)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(GetActorLocation(), Loudness, const_cast<AIGLeftoverTarget*>(this));
		}
	}
}
