#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGRoomLightSwitch.generated.h"

class UBoxComponent;
class UPointLightComponent;

/**
 * 403호 현관 옆 벽 스위치. 천장 등을 켜고 끈다. 켠 상태는 저장되고, 잠자리에
 * 들면 꺼진다. 밤에 켜면 딸깍 소리가 나지만 방 안에 어둠이 쌓이지 않는다.
 * 욕실 문 옆 스위치도 같은 클래스다(ConfigureSwitch).
 */
UCLASS()
class INDIEGAME_API AIGRoomLightSwitch : public AIGInteractableActor
{
	GENERATED_BODY()
public:
	AIGRoomLightSwitch();
	void BindLight(UPointLightComponent* InLight);
	/**
	 * 다른 등에 쓸 때(욕실). 켠 상태를 기억할 이야기 상태 태그, 안내 문구, 켰을 때
	 * 세기를 바꾼다. BindLight보다 먼저 부른다. 부르지 않으면 403호 방 등이다.
	 */
	void ConfigureSwitch(FName InStateTag, const FText& InOnPrompt, const FText& InOffPrompt, float InOnIntensity);
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
	FName StateTagName = TEXT("State.MissingFloor.HomeLightOn");
	FText OnPrompt;
	FText OffPrompt;
	float OnIntensity = 950.0f;
};
