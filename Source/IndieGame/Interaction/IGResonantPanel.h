#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGResonantPanel.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * 벽의 들뜬 보수판. 눌렀다 손을 떼면 1.6초 뒤에 판이 튀어나오며 소리가 난다.
 * 그 사이에 숨거나 다른 길로 빠지면, 그는 그녀가 아니라 판 쪽으로 간다.
 */
UCLASS()
class INDIEGAME_API AIGResonantPanel final : public AIGInteractableActor
{

	GENERATED_BODY()

public:
	AIGResonantPanel();
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ReleasePanel();
	UPROPERTY()
	TObjectPtr<UBoxComponent> InteractionBounds;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> PanelMesh;
	TWeakObjectPtr<AActor> LastInteractor;
	FTimerHandle ReleaseTimer;
	double NextInteractionAt = 0;
	float MovementSeconds = 0;
	bool bPressedDuringHour = false;
	int32 CaptureCountAtPress = 0;
};
