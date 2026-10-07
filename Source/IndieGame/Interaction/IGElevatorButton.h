#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGElevator.h"
#include "Interaction/IGInteractableActor.h"
#include "IGElevatorButton.generated.h"

class UBoxComponent;

/**
 * 승강기 버튼 하나. 층마다 있는 호출판과 칸 조작반의 버튼이 각자 이 액터다.
 * 조사 줄은 이 상자를 맞히고, 누르면 승강기에 그대로 넘긴다. 조작반 버튼은
 * 칸에 붙어 같이 움직인다.
 */
UCLASS()
class INDIEGAME_API AIGElevatorButton : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGElevatorButton();

	void Configure(AIGElevator* InElevator, EIGElevatorButtonKind InKind, int32 InFloor, const FVector& Extent);
	EIGElevatorButtonKind GetKind() const { return Kind; }
	int32 GetFloor() const { return Floor; }

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Elevator")
	TObjectPtr<UBoxComponent> PressArea;

	TWeakObjectPtr<AIGElevator> Elevator;
	EIGElevatorButtonKind Kind = EIGElevatorButtonKind::HallCall;
	int32 Floor = 0;
};
