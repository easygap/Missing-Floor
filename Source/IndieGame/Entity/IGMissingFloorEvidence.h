#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "Narrative/IGMissingFloorNarrativeTypes.h"
#include "IGMissingFloorEvidence.generated.h"

class AIGMissingFloorEvidence;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

DECLARE_MULTICAST_DELEGATE_OneParam(
	FIGEvidenceExaminedSignature,
	AIGMissingFloorEvidence* /*Evidence*/);

/**
 * 조사하면 단서와 독백, 소리를 함께 남기는 물건.
 * 밤에는 조사할 때 난 소리도 귀신에게 들린다. 계량기를 만질 때와
 * 차단기를 내릴 때처럼 행동마다 소음 크기를 따로 정한다.
 */
UCLASS(NotBlueprintable)
class INDIEGAME_API AIGMissingFloorEvidence : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGMissingFloorEvidence();

	/**
	 * Builds the interaction surface and authors what examining it means.
	 * A truth of None files nothing — used for props whose only job is to make
	 * something happen in the world (the breaker).
	 *
	 * SizeCentimeters는 메시를 맞출 **실제 바운딩 박스**다. 100cm 엔진 큐브를
	 * 가정하고 100으로 나누는 CreateBlock·ConfigurePrototypeVisuals와 다르다.
	 * 저작된 크기를 그대로 쓰려면 0을 넘겨라 — 여기에 (100,100,100)을 넘기면
	 * 스케일 1이 아니라 1m 정육면체가 된다.
	 */
	void Configure(
		UStaticMesh* Mesh,
		UMaterialInterface* Material,
		const FVector& SizeCentimeters,
		const FText& Prompt,
		const FText& Thought,
		EIGMissingFloorTruth Truth,
		EIGMissingFloorSource Source,
		float HoldSeconds,
		float NoiseLoudness,
		/**
		 * false면 표시용 메시를 그리지 않는다. 씬이 이미 그 자리에 제대로 된
		 * 소품을 세워 둔 경우, 액터는 상호작용만 맡고 그림은 씬에 맡긴다.
		 * 충돌은 그대로 남으므로 조준 트레이스는 계속 걸린다.
		 */
		bool bPresentationVisible = true);

	/** True once this record has been filed at least once. */
	UFUNCTION(BlueprintPure, Category = "Evidence")
	bool HasBeenExamined() const { return bExamined; }

	/**
	 * Turns the prop into a repeated-work surface (P2's frottage): each
	 * completed hold plays the next interim thought and makes its noise, and
	 * only the hold after the last interim line files the record and fires
	 * OnExamined. The pencil has to cross the page more than once.
	 */
	void SetProgressiveStages(TArray<FText> InStageThoughts);

	/**
	 * Gives this prop an audible cue for the whole length of its hold (§21.3
	 * 프로타주). Only the carbon ledger uses it: a sustained 0.25 that the player
	 * cannot hear is a cost they cannot choose, and every other prop here is a
	 * tap whose noise report already lands with the interaction.
	 */
	void SetSustainedRubCue(bool bEnabled);

	/** 종이 위의 복원 흔적을 홀드 진행과 연결한다. 끝낸 기록은 저장 상태에서 복구한다. */
	bool ConfigureProgressReveal(UMaterialInterface* Material, const FVector& Offset,
		const FVector2D& Size, const FText& FinishedPrompt);
	float GetRevealFraction() const { return RevealFraction; }
	virtual float GetInteractionHoldDuration_Implementation(AActor* Interactor) const override;

	UFUNCTION(BlueprintPure, Category = "Evidence")
	int32 GetCompletedStageCount() const { return CompletedStages; }

	UFUNCTION(BlueprintPure, Category = "Evidence")
	UStaticMeshComponent* GetPresentationMesh() const { return PresentationMesh; }

	/** Fires on every completed examination, including repeats. */
	FIGEvidenceExaminedSignature OnExamined;

	/** 마지막으로 끝낸 상호작용이 조준한 자리. 조준 정보가 없으면(검사) 액터 위치다. */
	FVector GetLastInteractionPoint() const { return LastInteractionPoint; }

	virtual void BeginInteraction_Implementation(
		const FIGInteractionContext& Context) override;
	virtual void UpdateInteraction_Implementation(
		const FIGInteractionContext& Context) override;
	virtual void CompleteInteraction_Implementation(
		const FIGInteractionContext& Context) override;
	virtual void EndInteraction_Implementation(
		const FIGInteractionContext& Context,
		EIGInteractionEndReason EndReason) override;

private:
	void StartRubCue();
	void StopRubCue();
	void RefreshReveal();
	void ReportRubNoise(const FIGInteractionContext& Context);

	/**
	 * 단순 충돌이 없는 메시(계산대 벨처럼 UCX 없이 만든 작은 소품)에 붙이는 조준 상자. 상호작용은
	 * 가시성 트레이스로 잡는데, 단순 충돌이 없으면 시선이 메시를 그냥 지나가 손을 댈 수 없었다.
	 */
	UPROPERTY(Transient)
	TObjectPtr<class UBoxComponent> AimVolume;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RevealSurface;
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> RevealMaterial;
	FText RevealFinishedPrompt;
	float RevealFraction = 0.f;
	float PartialStageProgress = 0.f;
	float HoldStartPartial = 0.f;
	double NextRubNoiseTime = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> RubCueComponent;

	bool bSustainedRubCue = false;

	UPROPERTY(VisibleAnywhere, Category = "Evidence")
	TObjectPtr<UStaticMeshComponent> PresentationMesh;

	UPROPERTY(EditAnywhere, Category = "Evidence")
	FText ExamineThought;

	UPROPERTY(EditAnywhere, Category = "Evidence")
	EIGMissingFloorTruth EvidenceTruth = EIGMissingFloorTruth::None;

	UPROPERTY(EditAnywhere, Category = "Evidence")
	EIGMissingFloorSource EvidenceSource = EIGMissingFloorSource::None;

	UPROPERTY(EditAnywhere, Category = "Evidence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExamineNoiseLoudness = 0.05f;

	/** Interim lines for a multi-hold surface; empty means one-and-done. */
	UPROPERTY(EditAnywhere, Category = "Evidence")
	TArray<FText> StageThoughts;

	int32 CompletedStages = 0;
	bool bExamined = false;
	FVector LastInteractionPoint = FVector::ZeroVector;
};
