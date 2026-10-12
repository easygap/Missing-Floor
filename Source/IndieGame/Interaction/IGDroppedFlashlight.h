#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGDroppedFlashlight.generated.h"

class AIGNightPhaseDirector;
class USpotLightComponent;
class UStaticMeshComponent;

/**
 * 위층 사람에게 잡히며 떨어뜨린 손전등(§5.4). 잡힌 자리 바닥에 옆으로 누워 있고,
 * 켜 둔 채 떨어뜨렸으면 바닥을 따라 빛을 낸다. 깨어난 자리에서 그 빛이 보이고,
 * 다시 쓰려면 그 자리로 돌아가 주워야 한다. 새벽이 오면 저절로 손에 돌아온다.
 * 저장하지 않는다. 불러온 판에서는 손에 들려 있다.
 */
UCLASS()
class INDIEGAME_API AIGDroppedFlashlight : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGDroppedFlashlight();

	/** 바닥에 놓는다. bInLit이면 렌즈 쪽으로 빛을 낸다. */
	void Configure(bool bInLit);
	bool IsLit() const { return bLit; }
	/** 몸통 아래가 바닥 점에 닿게 놓는다. */
	void PlaceOnFloor(const FVector& FloorPoint);

	/** 주인 손에 돌려주고 사라진다. 새벽과 주울 때 쓴다. */
	void ReturnToOwner(bool bSwitchOn);

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleHourActiveChanged(bool bActive);

	UPROPERTY(VisibleAnywhere, Category = "Flashlight")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Flashlight")
	TObjectPtr<USpotLightComponent> Beam;

	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	FDelegateHandle HourActiveHandle;
	bool bLit = false;
};
