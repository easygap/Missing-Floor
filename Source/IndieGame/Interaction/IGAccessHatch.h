#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGAccessHatch.generated.h"

class AIGAccessHatch;
class UAudioComponent;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FIGAccessHatchEvent, AIGAccessHatch*);

/**
 * 벽의 점검구(관리실 안쪽 방, 302호, 5층). 경첩 문짝 하나와 틀이다.
 *
 * 덮개가 열려 있고 안이 막히지 않았으면 「열린 틈」이다. 벽 속 공간과 이어진 틈은 덧댄 것이
 * 와서 몸으로 누른다(AIGLiningEntity). 누른 자리는 검은 접착층과 방음재로 막혀(Sealed) 바람이
 * 더는 새지 않는다. 막힌 틈은 손으로 뜯어내야 다시 열린다.
 *
 * 몸이나 다리가 개구부에 걸려 있는 동안(Obstructed) 문짝은 걸린 각도까지만 닫히고 부딪혀
 * 튕긴다. 다리가 빠지면 그때 닫힌다. 걸쇠·자물쇠는 없다.
 *
 * 좌표: 액터 자리가 개구부 가운데(벽 면), 액터 +X가 방 쪽으로 나온 법선, +Z가 위다.
 * 경첩은 로컬 -Y 쪽 세로 변(bHingeOnLeft)이거나 +Y 쪽이다. 문짝은 방 쪽(+X)으로 연다.
 */
UCLASS(NotBlueprintable)
class INDIEGAME_API AIGAccessHatch : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGAccessHatch();

	/**
	 * 크기(가로·세로 cm)와 생김새. 메시가 없으면 상자 판으로 대신한다. FrameMesh·DoorMesh는
	 * 원점이 개구부 가운데(문짝은 경첩 축 가운데)이고 +X가 방 쪽이다.
	 */
	void Configure(
		const FVector2D& OpeningSizeCm,
		bool bInHingeOnLeft,
		UStaticMesh* FrameMesh,
		UStaticMesh* DoorMesh,
		UMaterialInterface* PaintMaterial,
		UMaterialInterface* SealMaterial,
		const FText& InOpenPrompt,
		const FText& InClosePrompt);

	/** 틈의 넓이(cm²). 덧댄 것은 넓은 틈부터 고른다. */
	float GetOpeningArea() const { return OpeningSize.X * OpeningSize.Y; }
	FVector2D GetOpeningSize() const { return OpeningSize; }
	FVector GetOpeningCenter() const { return GetActorLocation(); }
	FVector GetOpeningNormal() const { return GetActorForwardVector(); }

	/** 문짝이 열린 쪽에 있다(다 열리지 않아도 30도 넘게). */
	bool IsOpen() const { return bOpen; }
	/** 바람이 통하는 열린 틈이다. */
	bool IsGapOpen() const { return bOpen && !bSealed; }
	bool IsSealed() const { return bSealed; }

	/** 덧댄 것이 눌러 막았다(접착층과 방음재가 개구부를 메운다). false면 뜯어낸 상태로 돌린다. */
	void SetSealed(bool bInSealed);
	/** 막힌 자리를 손으로 뜯어 다시 열 수 있게 할지. 기본은 허용. */
	void SetReopenAllowed(bool bAllowed) { bReopenAllowed = bAllowed; }

	/** 몸이나 다리가 개구부에 걸렸다. 문짝은 BlockedYaw(도, 0이 닫힘)보다 더 닫히지 않는다. */
	void SetObstructed(bool bInObstructed, float BlockedYawDegrees = 38.0f);
	bool IsObstructed() const { return bObstructed; }

	/**
	 * 덧댄 것 몸판이 문짝이 도는 자리에 있다. 열리는 문짝은 이 각(도)에서 멈추고, 풀리면 마저 열린다. 다 열린 문짝은
	 * 밀지 않는다(덧댄 것이 길을 비킨다).
	 */
	void SetOpenLimit(float LimitYawDegrees);
	void ClearOpenLimit();
	bool HasOpenLimit() const { return OpenLimitYaw < 180.0f; }
	/** 지금 문짝 각(도, 0이 닫힘). */
	float GetDoorYaw() const { return CurrentYaw; }

	/** 문짝(열리면 경첩 쪽 벽 앞에 선다). 덧댄 것이 몸판과 다리를 비킬 때 본다. */
	UStaticMeshComponent* GetDoorPanel() const { return DoorPanel; }
	/** 다 연 각(도). */
	static float GetOpenYaw();
	/** 문짝을 그 각(도)만큼 열었을 때 문짝 면 위 점들(경첩에서 끝까지, 위아래 셋, 월드). 덧댄 것이 걸리는 각을 잰다. */
	void GetDoorSamplePoints(float YawDegrees, TArray<FVector>& OutPoints) const;
	/**
	 * 문짝을 그 각(도)만큼 열었을 때의 판. 경첩 축 가운데(OutHinge)에서 끝 변 쪽(OutAlong)으로 OutWidth, 위(OutUp)로
	 * 위아래 OutHeight의 절반씩이다. 덧댄 것이 열리는 중인 문짝 대신 다 열린 문짝을 보고 자리를 정할 때 쓴다.
	 */
	void GetDoorPlane(float YawDegrees, FVector& OutHinge, FVector& OutAlong, FVector& OutUp, float& OutWidth, float& OutHeight) const;

	/** 소리와 이벤트 없이 상태만 맞춘다(불러오기·연출 시작). */
	void ForceOpenState(bool bInOpen);
	/** 연출이 문짝을 연다(소리는 낸다). */
	void OpenByScript();

	/** 바람 소리 자리(틈이 열렸을 때 빨려 드는 소리). */
	void SetAirSound(bool bEnabled);
	/** 닫힌 문짝 둘레로 새는 바람을 잠깐 낸다. 막혀 있던 점검구가 처음 드러날 때. */
	void PlayDraftThroughSeams(float Seconds);

	FIGAccessHatchEvent OnOpened;
	FIGAccessHatchEvent OnClosed;
	/** 닫으려 했지만 다리에 걸려 튕겼다. */
	FIGAccessHatchEvent OnCloseBlocked;
	/** 막힌 자리를 뜯어 다시 열었다. */
	FIGAccessHatchEvent OnReopened;

	virtual void Tick(float DeltaSeconds) override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual float GetInteractionHoldDuration_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void BeginSwing(bool bInOpen, bool bQuiet);
	void ApplyDoorYaw(float Yaw);
	void RefreshSealVisual();
	void RefreshAirMotes(float DeltaSeconds);
	void PlayHatchSound(const TCHAR* SampleName, float Volume, float Pitch) const;
	void EnsureAirLoop();
	void EndDraftThroughSeams();
	FVector GetHingeWorld() const;

	UPROPERTY(VisibleAnywhere, Category = "Hatch")
	TObjectPtr<USceneComponent> HatchRoot;

	UPROPERTY(VisibleAnywhere, Category = "Hatch")
	TObjectPtr<UBoxComponent> FocusBox;

	UPROPERTY(VisibleAnywhere, Category = "Hatch")
	TObjectPtr<USceneComponent> DoorPivot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FrameParts;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DoorPanel;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SealPlug;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> AirMotes;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AirLoop;

	FTimerHandle DraftTimer;

	FVector2D OpeningSize = FVector2D(45.0f, 45.0f);
	FText OpenPrompt;
	FText ClosePrompt;
	bool bHingeOnLeft = true;
	bool bOpen = false;
	bool bSealed = false;
	bool bReopenAllowed = true;
	bool bObstructed = false;
	bool bAirSound = true;
	float BlockedYaw = 38.0f;
	/** 열리는 문짝이 넘지 않는 각(도). 180이면 제한이 없다. */
	float OpenLimitYaw = 180.0f;
	float CurrentYaw = 0.0f;
	float TargetYaw = 0.0f;
	/** 걸려서 튕기는 동안의 남은 시간. */
	float BounceSeconds = 0.0f;
	float SwingSpeed = 220.0f;
	float MoteClock = 0.0f;
};
