#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGRoomLightSwitch.generated.h"

class UBoxComponent;
class UPointLightComponent;

/**
 * 403호 현관 옆 벽 스위치. 천장 등을 켜고 끈다. 켠 상태는 저장되고, 잠자리에
 * 들면 꺼진다. 밤에 켜면 딸깍 소리가 나지만 방 안에 어둠이 쌓이지 않는다.
 */
UCLASS()
class INDIEGAME_API AIGRoomLightSwitch : public AIGInteractableActor
{
	GENERATED_BODY()
public:
	AIGRoomLightSwitch();
	void BindLight(UPointLightComponent* InLight);
	bool IsLightOn() const { return bLightOn; }
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	void RefreshState();
	UFUNCTION() void HandleState(FGameplayTag Tag, bool bAdded);
	UPROPERTY() TObjectPtr<UBoxComponent> InteractionBox;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	bool bLightOn = false;
};
