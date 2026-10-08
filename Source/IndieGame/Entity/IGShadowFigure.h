#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGShadowFigure.generated.h"

class UMaterialInterface;
class UPoseableMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 어둠이 사람 꼴을 한 것. 어둑시니와 문 밖의 손님이 같은 틀을 쓴다.
 *
 * 처음에는 기본 도형 몇 개로 지은 윤곽뿐이다. 손님은 그 위에 종이를 입고(DressAsPaper),
 * 어둑시니는 빚어 만든 몸으로 갈아입는다(DressAsEoduksini). 얼굴은 없다. 손전등에 비치면
 * 젖은 검정이 빛을 조금 되쏘고, 어둠 속에서는 등 뒤의 희미한 빛을 가리는 것으로만
 * 보인다. 크기(Growth 0~1)가 커지면 1.1 m에서 2.9 m까지 자라고 상체가 앞으로
 * 숙어진다. 보이는 동안만 틱이 돈다.
 */
UCLASS()
class INDIEGAME_API AIGShadowFigure : public AActor
{
	GENERATED_BODY()

public:
	AIGShadowFigure();

	virtual void Tick(float DeltaSeconds) override;

	/** 나타난다. 바닥 위 Location에서 Growth 크기로, YawDegrees를 본다. */
	void Manifest(const FVector& Location, float YawDegrees, float Growth);
	/** 사라진다. 어둠에 녹듯 바로 꺼진다. */
	void Vanish();
	bool IsManifested() const { return bManifested; }

	/** 디렉터가 정하는 목표. 몸은 그쪽으로 부드럽게 따라간다. */
	void SetTargetGrowth(float Growth) { TargetGrowth = FMath::Clamp(Growth, 0.0f, 1.0f); }
	void SetTargetLocation(const FVector& Location, float SpeedCmPerSecond);
	void SetTargetYaw(float YawDegrees) { TargetYaw = YawDegrees; }
	/** 보는 사람이 있는 동안 몸이 미세하게 떤다. */
	void SetTrembling(bool bInTrembling) { bTrembling = bInTrembling; }

	/**
	 * 손님의 몸으로 갈아입힌다. 공동현관 옆의 「원룸 있습니다」 전단, 입주민 안내문,
	 * 누렇게 바랜 빈 종이가 겹겹이 붙어 사람 꼴을 이룬다. 얼굴 자리에는 403호 문의
	 * 통닭집 자석이 붙어 있다. 한 번만 입힌다.
	 */
	void DressAsPaper();
	bool IsPaper() const { return bPaper; }

	/**
	 * 어둑시니의 몸으로 갈아입힌다. 원기둥과 구 대신 빚어 만든 몸(SK_Eoduksini)이 선다.
	 * 허리는 뼈를 직접 돌려 숙이고, 팔은 숙인 어깨에 매달린 채 늘어져 있다. 몸을 못 읽으면
	 * 도형 윤곽이 그대로 남는다. 한 번만 입힌다.
	 */
	void DressAsEoduksini();
	bool IsSculpted() const { return SculptedBody != nullptr; }

	float GetGrowth() const { return CurrentGrowth; }
	/** 가슴 높이. 시선과 손전등이 닿는지 볼 때 쓴다. */
	FVector GetChestLocation() const;

private:
	void ApplyPose();
	/** 빚은 몸의 허리를 숙이고 팔을 늘어뜨린다. 각도는 도 단위다. */
	void PoseSculptedBody(float LeanDegrees, float TrembleDegrees);
	UStaticMeshComponent* AddPart(
		const TCHAR* Name,
		USceneComponent* Parent,
		UStaticMesh* Mesh,
		const FVector& Location,
		const FRotator& Rotation,
		const FVector& Scale);

	UPROPERTY(VisibleAnywhere, Category = "Shadow")
	TObjectPtr<USceneComponent> FigureRoot;

	/** 허리. 자라면 이 축으로 앞으로 숙인다. */
	UPROPERTY(VisibleAnywhere, Category = "Shadow")
	TObjectPtr<USceneComponent> Spine;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ShadowMaterial;

	/** 종이 몸이 입은 낱장들. 런타임에 붙인다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Sheets;

	/** 어둑시니의 빚은 몸. DressAsEoduksini가 붙인다. */
	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> SculptedBody;

	/** 빚은 몸의 쉬는 자세(컴포넌트 공간). 숙일 때마다 여기서부터 돌린다. */
	FTransform SpineRest;
	FTransform ArmRest[2];

	FVector TargetLocation = FVector::ZeroVector;
	float MoveSpeed = 0.0f;
	float TargetGrowth = 0.0f;
	float CurrentGrowth = 0.0f;
	float TargetYaw = 0.0f;
	float TremblePhase = 0.0f;
	bool bTrembling = false;
	bool bManifested = false;
	bool bPaper = false;
};
