#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGFireDoorWedge.generated.h"

class AIGSwingDoor;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 계단실 방화문을 괴어 둔 나무 고임목(§4). 우리나라 빌라 방화문은 대개 이렇게
 * 열려 있다. 빼면 문이 닫힌다.
 *
 * - 그냥 빼면 도어클로저가 문을 끌어당겨 쾅 닫힌다(소리 0.42).
 * - 문을 잡은 채 E를 길게 누르면 천천히 닫힌다(0.08).
 *
 * 닫힌 방화문은 위층 사람의 몸을 막고, 계단실 안의 작은 소리를 삼킨다. 고임목을
 * 뺀 뒤의 문은 여느 문처럼 열고 닫는다.
 */
UCLASS()
class INDIEGAME_API AIGFireDoorWedge : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGFireDoorWedge();

	/**
	 * 괼 문과 생김새. 문은 열린 채로 두고 이 액터가 닫기를 맡는다. 고임목
	 * 자체는 이 액터 자리에 놓인다.
	 */
	void Configure(
		AIGSwingDoor* InDoor,
		UStaticMesh* CubeMesh,
		UMaterialInterface* WoodMaterial);

	/** 문이 닫혔을 때 작은 소리를 삼킬 계단실 쪽 자리. */
	void SetStairwellCenter(const FVector& InStairwellCenter) { StairwellCenter = InStairwellCenter; }

	bool IsWedged() const { return bWedged; }

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;
	virtual void EndInteraction_Implementation(
		const FIGInteractionContext& Context,
		EIGInteractionEndReason EndReason) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void PullWedge(bool bQuiet);
	void RefreshStairwellMuffle();

	UPROPERTY(VisibleAnywhere, Category = "FireDoor")
	TObjectPtr<USceneComponent> WedgeRoot;

	UPROPERTY(VisibleAnywhere, Category = "FireDoor")
	TObjectPtr<UBoxComponent> FocusBox;

	UPROPERTY(VisibleAnywhere, Category = "FireDoor")
	TObjectPtr<UStaticMeshComponent> WedgeMesh;

	TWeakObjectPtr<AIGSwingDoor> Door;
	FVector StairwellCenter = FVector::ZeroVector;
	FTimerHandle MuffleTimer;
	int32 MuffleHandle = INDEX_NONE;
	bool bWedged = true;
};
