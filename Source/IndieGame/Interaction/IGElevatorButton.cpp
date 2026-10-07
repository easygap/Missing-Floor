#include "Interaction/IGElevatorButton.h"

#include "Components/BoxComponent.h"

AIGElevatorButton::AIGElevatorButton()
{
	PressArea = CreateDefaultSubobject<UBoxComponent>(TEXT("PressArea"));
	SetRootComponent(PressArea);
	PressArea->SetMobility(EComponentMobility::Movable);
	PressArea->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PressArea->SetCollisionResponseToAllChannels(ECR_Ignore);
	PressArea->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PressArea->SetGenerateOverlapEvents(false);
	PressArea->SetCanEverAffectNavigation(false);
	PressArea->SetBoxExtent(FVector(1.5f, 2.6f, 2.6f));
}

void AIGElevatorButton::Configure(
	AIGElevator* InElevator, const EIGElevatorButtonKind InKind, const int32 InFloor, const FVector& Extent)
{
	Elevator = InElevator;
	Kind = InKind;
	Floor = InFloor;
	PressArea->SetBoxExtent(Extent);
}

bool AIGElevatorButton::CanInteract_Implementation(AActor* Interactor) const
{
	const AIGElevator* Lift = Elevator.Get();
	return Super::CanInteract_Implementation(Interactor) && Lift && Lift->CanPress(Kind, Floor, Interactor);
}

FText AIGElevatorButton::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	const AIGElevator* Lift = Elevator.Get();
	return Lift ? Lift->GetButtonPrompt(Kind, Floor) : FText::GetEmpty();
}

void AIGElevatorButton::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	Super::CompleteInteraction_Implementation(Context);
	if (AIGElevator* Lift = Elevator.Get())
	{
		Lift->Press(Kind, Floor, Context.Interactor);
	}
}
