#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGDoorLatch.generated.h"

class AIGSwingDoor;
class UBoxComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * 현관문 안쪽의 걸쇠(§4). 문짝 손잡이 위에 달린 작은 쇠막대다. 걸면 밖에서
 * 도어락을 풀어도 문이 한 뼘에서 걸리고(손님), 안에서 문을 열면 같이 풀린다.
 * 문이 닫혀 있을 때만 걸 수 있다.
 */
UCLASS()
class INDIEGAME_API AIGDoorLatch : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGDoorLatch();

	/** 걸쇠가 맡을 문과 생김새. 액터는 문짝 안쪽 면, 손잡이 위에 놓는다. */
	void Configure(AIGSwingDoor* InDoor, UStaticMesh* CubeMesh, UMaterialInterface* Material);

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RefreshBoltPose();

	UPROPERTY(VisibleAnywhere, Category = "Latch")
	TObjectPtr<USceneComponent> LatchRoot;

	UPROPERTY(VisibleAnywhere, Category = "Latch")
	TObjectPtr<UBoxComponent> FocusBox;

	UPROPERTY(VisibleAnywhere, Category = "Latch")
	TObjectPtr<UStaticMeshComponent> Plate;

	UPROPERTY(VisibleAnywhere, Category = "Latch")
	TObjectPtr<UStaticMeshComponent> Bolt;

	TWeakObjectPtr<AIGSwingDoor> Door;
};
