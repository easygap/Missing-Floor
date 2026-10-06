#include "Interaction/IGBatteryPickup.h"

#include "Player/IGFlashlightComponent.h"
#include "Player/IGPlayerCharacter.h"

namespace IGBattery
{
	// 이보다 약하면 주운 자리에서 바로 갈아 끼운다. 그 위면 주머니에 넣어 둔다.
	constexpr float SwapNowBelow = 0.6f;
}

AIGBatteryPickup::AIGBatteryPickup()
{
	PickupMode = EIGPickupMode::Pocket;
	SetInteractionPrompt(NSLOCTEXT("IGMissingFloor", "BatteryPickupPrompt", "건전지 챙기기"));
}

void AIGBatteryPickup::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	const AIGPlayerCharacter* Player = Cast<AIGPlayerCharacter>(Context.Interactor);
	UIGFlashlightComponent* Torch = Player ? Player->GetFlashlight() : nullptr;
	const bool bSwapNow = Torch
		&& Torch->IsAvailable()
		&& Torch->GetBatteryFraction() < IGBattery::SwapNowBelow;
	ThoughtOnPickup = bSwapNow
		? NSLOCTEXT("IGMissingFloor", "YudamBatterySwapped", "건전지다. 바로 갈아 끼웠다.")
		: NSLOCTEXT("IGMissingFloor", "YudamBatteryPocketed", "건전지다. 챙겨 두자.");

	const bool bAlreadyTaken = WasPickedUp();
	Super::CompleteInteraction_Implementation(Context);
	if (bAlreadyTaken || !WasPickedUp() || !Torch)
	{
		return;
	}
	if (bSwapNow)
	{
		Torch->RefillBattery(1.0f);
	}
	else
	{
		Torch->AddSpareBattery();
	}
}
