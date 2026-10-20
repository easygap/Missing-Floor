#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGLeftoverTarget.generated.h"

class AIGLeftoverTarget;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FIGLeftoverTargetEvent, AIGLeftoverTarget*);

/**
 * 「302호에 남은 짐」에서 손을 대는 자리 하나. 조준 상자와 (있으면) 보이는 메시다.
 *
 * 누르면 바로 끝나거나(HoldSeconds 0) 길게 눌러야 끝난다(OnCompleted). 길게 누를 자리에서
 * 일찍 떼면 OnTapped다. 조각을 돌리기(짧게)와 뒤집기(길게)처럼 한 자리에서 두 가지를 하는
 * 데 쓴다. 무엇을 하는지는 감독이 정한다. 이 액터는 소리와 소음을 내고 알리기만 한다.
 */
UCLASS(NotBlueprintable)
class INDIEGAME_API AIGLeftoverTarget : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGLeftoverTarget();

	/** 조준 상자(반 길이 cm), 안내, 길게 누르는 시간, 끝났을 때 낼 소음(0..1). */
	void Configure(const FVector& FocusExtent, const FText& Prompt, float HoldSeconds, float NoiseLoudness);
	/** 짧게 떼었을 때도 무언가 한다(OnTapped). 그때의 안내 뒤에 붙는다. */
	void SetTapAction(bool bEnabled) { bTapAction = bEnabled; }
	/** 보이는 메시. Relative는 액터 기준. 충돌은 없다. */
	UStaticMeshComponent* SetVisual(UStaticMesh* Mesh, const FTransform& Relative, UMaterialInterface* Material = nullptr);
	UStaticMeshComponent* GetVisual() const { return Visual; }
	/** 끝났을 때 생각 한 줄(비우면 없다). */
	void SetThought(const FText& InThought) { Thought = InThought; }
	/** 이름표. 감독이 어떤 자리인지 가른다. */
	FName Key;
	int32 Index = INDEX_NONE;

	FIGLeftoverTargetEvent OnCompleted;
	FIGLeftoverTargetEvent OnTapped;

	virtual float GetInteractionHoldDuration_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;
	virtual void EndInteraction_Implementation(
		const FIGInteractionContext& Context,
		EIGInteractionEndReason EndReason) override;

private:
	void ReportNoise(float Loudness) const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Focus;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Visual;

	FText Thought;
	float NoiseLoudness = 0.05f;
	bool bTapAction = false;
	bool bCompletedThisPress = false;
};
