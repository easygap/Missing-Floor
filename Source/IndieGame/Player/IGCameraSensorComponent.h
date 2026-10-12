#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IGCameraSensorComponent.generated.h"

class UMaterialInstanceDynamic;
class UPostProcessComponent;

/**
 * 1인칭 화면에 약한 렌즈 왜곡(M_PP_CameraSensor)을 입힌다.
 *
 * 소품의 표면과 글씨가 보이도록 노이즈와 전면 흐림은 쓰지 않는다.
 * 세기는 접근성 설정 「렌즈 왜곡」을 따른다. 매 프레임 갱신할 값은 없으며,
 * 설정이 바뀌면 플레이어가 RefreshFromSettings를 부른다.
 */
UCLASS(ClassGroup = (IndieGame))
class INDIEGAME_API UIGCameraSensorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIGCameraSensorComponent();

	virtual void BeginPlay() override;

	/** 접근성 설정을 다시 읽어 재질에 넘긴다. */
	void RefreshFromSettings();

	/** 마지막으로 재질에 넘긴 세기. 0이면 효과가 꺼져 있다. */
	float GetAppliedStrength() const { return AppliedStrength; }

private:

	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> SensorPostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SensorMaterial;

	float AppliedStrength = -1.0f;
};
