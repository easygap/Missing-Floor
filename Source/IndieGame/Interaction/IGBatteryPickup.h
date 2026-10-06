#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGPickupItem.h"
#include "IGBatteryPickup.generated.h"

/**
 * 손전등 건전지 두 알. 손전등이 약해져 있으면 그 자리에서 갈아 끼우고, 아직
 * 쓸 만하면 주머니에 넣어 둔다. 주머니의 건전지는 약해진 손전등을 껐다 켤 때
 * 갈아 끼운다(UIGFlashlightComponent::SetOn).
 */
UCLASS()
class INDIEGAME_API AIGBatteryPickup : public AIGPickupItem
{
	GENERATED_BODY()

public:
	AIGBatteryPickup();

	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;
};
