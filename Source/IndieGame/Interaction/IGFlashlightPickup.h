#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGFlashlightPickup.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/** 현관에 놓인 손전등. 소유 기록에 따라 물건과 손에 든 빛을 함께 복원한다. */
UCLASS()
class INDIEGAME_API AIGFlashlightPickup : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGFlashlightPickup();
	void ConfigureVisuals(UStaticMesh* Mesh, UMaterialInterface* Material, bool bFallbackCylinder = false);
	UStaticMeshComponent* GetMeshComponent() const { return MeshComponent; }
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;
	void RefreshOwnership();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleStoryStateChanged(FGameplayTag StateTag, bool bAdded);
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MeshComponent;
};
