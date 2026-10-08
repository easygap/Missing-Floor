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
 * 문이 닫혀 있을 때만 걸 수 있다. 5층 철문 안쪽의 빗장도 이 액터가 맡는다.
 */
UCLASS()
class INDIEGAME_API AIGDoorLatch : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGDoorLatch();

	/** 걸쇠가 맡을 문과 생김새. 액터는 문짝 안쪽 면, 손잡이 위에 놓는다. */
	void Configure(AIGSwingDoor* InDoor, UStaticMesh* CubeMesh, UMaterialInterface* Material);

	/**
	 * 저작 메시 빗장(5층 철문). 몸통과 막대는 원점이 같고, 걸면 막대만 +X로 Travel만큼 민다.
	 * 액터의 +X가 문틀 쪽, +Y가 문 면 바깥이다. 문구는 걸 때와 풀 때 것을 받는다.
	 */
	void ConfigureAuthored(
		AIGSwingDoor* InDoor,
		UStaticMesh* HousingMesh,
		UStaticMesh* PinMesh,
		float InTravelCm,
		const FText& InLockPrompt,
		const FText& InUnlockPrompt);

	AIGSwingDoor* GetDoor() const { return Door.Get(); }

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void BindDoor(AIGSwingDoor* InDoor);
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
	/** 걸면 막대가 문틀 쪽으로 밀려 나가는 거리와, 풀린 막대의 자리. */
	float BoltTravelCm = 4.5f;
	FVector BoltRestOffset = FVector(0.0f, -0.9f, 0.0f);
	/** 비어 있으면 현관 걸쇠 문구를 쓴다. */
	FText LockPrompt;
	FText UnlockPrompt;
};
