#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGBathroomRefuge.generated.h"

class AIGSwingDoor;
class UBoxComponent;

/**
 * 403호 욕실(EXPANSION_PLAN §4). 문을 닫고 잠그면 숨는 자리가 된다.
 *
 * 이 액터는 욕실 쪽 문손잡이 로제트의 누름 잠금 단추 자리에 선다. E로 누르면 문이
 * 잠기고 다시 누르면 풀린다. 안에서 문을 열면 같이 풀린다(AIGSwingDoor의 걸쇠 규칙).
 *
 * 그녀가 욕실 안에 있고 문이 다 닫혀 잠겨 있으면 숨은 것이다. 손님은 잠긴 문을 열지
 * 않고 손잡이만 돌려 보고 돌아간다. 위층 사람은 문 앞에서 두드리고 듣는다. 안에서 낸
 * 소리는 숨는 가구처럼 반경 0.55배로 나간다. 어둠은 숨어 있어도 쌓인다. 불을 켜 두면
 * 문 아래 루버로 빛이 샌다.
 *
 * 틱 없이 0.2초 타이머로 숨음을 다시 잰다.
 */
UCLASS(NotBlueprintable)
class INDIEGAME_API AIGBathroomRefuge : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGBathroomRefuge();

	/**
	 * 욕실 문과 욕실 안(월드 상자), 문 바깥 앞자리와 문 안쪽 자리(발 높이). 몸이 욕실에
	 * 드나들 때는 이 두 점을 거친다.
	 */
	void Configure(AIGSwingDoor* InDoor, const FBox& InRoom, const FVector& InOutsideApproach, const FVector& InInsideApproach);

	bool IsInside(const FVector& Location) const { return Room.IsValid && Room.IsInsideOrOn(Location); }
	AIGSwingDoor* GetDoor() const { return Door.Get(); }
	const FVector& GetOutsideApproach() const { return OutsideApproach; }
	const FVector& GetInsideApproach() const { return InsideApproach; }
	/** 문이 다 닫혀 잠겨 있다. */
	bool IsSealed() const;
	/** 그 자리를 품은 욕실. 없으면 nullptr. */
	static AIGBathroomRefuge* FindRoomAt(const UWorld* World, const FVector& Location);

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RefreshConcealment();

	UPROPERTY(VisibleAnywhere, Category = "Refuge")
	TObjectPtr<UBoxComponent> FocusBox;

	TWeakObjectPtr<AIGSwingDoor> Door;
	FBox Room = FBox(ForceInit);
	FVector OutsideApproach = FVector::ZeroVector;
	FVector InsideApproach = FVector::ZeroVector;
	FTimerHandle ConcealTimer;
	bool bConcealed = false;
};
