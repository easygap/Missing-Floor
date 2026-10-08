#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGDoorAnimation.h"
#include "Interaction/IGInteractableActor.h"
#include "IGSwingDoor.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** One gate condition for a lockable door, checked in order. */
USTRUCT(BlueprintType)
struct INDIEGAME_API FIGDoorRequirement
{
	GENERATED_BODY()

	/** Story state that must be present for the door to open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FGameplayTag RequiredState;

	/** Interaction prompt shown while this requirement is unmet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FText LockedPrompt;

	/** Inner-voice line pushed when the player tries the locked door. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FText LockedThought;

	/**
	 * 잠긴 게 아니라 붙들린 문. 걸쇠는 풀리는데 문짝이 움직이지 않는다. 그 시간의
	 * 공동현관이 이렇다. 열쇠 달그락 대신 밀리다 서는 소리를 낸다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	bool bHeldShut = false;
};

/** 두드리면 나는 결. 위층 사람이 닫힌 문 앞에서 이걸 보고 친다. */
enum class EIGDoorKnockSurface : uint8
{
	/** 세대문·방화문·관리실 문·5층 철문. 기본이다. */
	Steel,
	/** 속이 빈 ABS 문짝. 403호 욕실 문이다. */
	Hollow,
	/** 알루미늄 틀의 유리문. 공동현관이다. */
	Glass
};

class AIGSwingDoor;
/** 조건이 안 맞는 문을 당겼다. 문 밖에서 이어 받을 반응이 있을 때 묶는다. */
DECLARE_MULTICAST_DELEGATE_OneParam(FIGSwingDoorLockedAttempt, AIGSwingDoor*);

/**
 * Hinged interactable door. The actor location is the hinge axis; the panel
 * extends along +Y in local space. Optional story-state requirements keep it
 * locked with contextual feedback until the player is ready to leave.
 */
UCLASS(Blueprintable)
class INDIEGAME_API AIGSwingDoor : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGSwingDoor();
	virtual void Tick(float DeltaSeconds) override;

	/** Builds the panel/handle meshes; call once before or during BeginPlay. */
	void ConfigurePrototypeVisuals(
		UStaticMesh* CubeMesh,
		UMaterialInterface* DoorMaterial,
		UMaterialInterface* HandleMaterial,
		const FVector& PanelSize);

	/**
	 * Storefront-style variant: aluminum frame, mid rail, push bar and two
	 * glass insets, so the glass door reads as a door rather than a hole.
	 */
	void ConfigureFramedGlassVisuals(
		UStaticMesh* CubeMesh,
		UMaterialInterface* GlassMaterial,
		UMaterialInterface* FrameMaterial,
		const FVector& PanelSize);

	void SetRequirements(TArray<FIGDoorRequirement>&& InRequirements);

	/** 잠긴(또는 붙들린) 문을 당긴 직후. 독백이 먼저 뜨고 그 뒤에 알린다. */
	FIGSwingDoorLockedAttempt OnLockedAttempt;

	/** Swing direction/extent; negative yaw opens toward local -Y. */
	void SetOpenYaw(float InOpenYaw) { OpenYaw = InOpenYaw; }

	/** Rotating leaf pivot; dressing attached here follows the swing. */
	USceneComponent* GetDoorPivot() const { return DoorPivot; }

	/** 속이 빈 가벼운 문짝(욕실 ABS 문)으로 둔다. 두드리는 소리가 철문과 다르다. */
	void SetHollowLeaf(const bool bInHollow) { bHollowLeaf = bInHollow; }
	EIGDoorKnockSurface GetKnockSurface() const
	{
		return bFramedGlass ? EIGDoorKnockSurface::Glass
			: bHollowLeaf ? EIGDoorKnockSurface::Hollow
			: EIGDoorKnockSurface::Steel;
	}

	/**
	 * 문짝에서 From에 가장 가까운 자리, From 쪽 겉면. 높이는 From의 높이를 문짝 안으로
	 * 당긴다. 문 너머에서 두드리는 소리를 그 문에서 내게 할 때 쓴다.
	 */
	FVector GetKnockPoint(const FVector& From) const;

	/**
	 * Swaps the box handle for an authored lever mesh (rose plate + swept
	 * lever, modelled in centimeters with the rose facing +Z).
	 */
	void SetLeverMesh(UStaticMesh* LeverMesh, UMaterialInterface* Material, const FVector& PanelSize);

	/**
	 * Blender에서 구운 왼손 문짝(SM_UnitDoorLeafWideL)을 통째로 쓴다. 그 메시는
	 * 원점이 바닥 중심, 앞면이 -Y, 힌지가 +X 쪽이라 yaw -90으로 놓으면 힌지
	 * 축이 이 액터의 원점에, 문짝은 +Y로, 바깥면은 -X로 온다. 레버·도어락은
	 * 원점이 같은 SM_UnitDoorHardwareWideL이라 손잡이 컴포넌트에 문짝과 같은
	 * 자세로 달아 함께 돌린다. 상세 블록은 만들지 않는다.
	 * ConfigurePrototypeVisuals 대신 부른다.
	 */
	void ConfigureAuthoredLeaf(
		UStaticMesh* LeafMesh, UStaticMesh* HardwareMesh, const FVector& PanelSize);

	/**
	 * 문짝과 같이 도는 장식 메시를 하나 단다. 원점과 축이 문짝 메시와 같아야 한다(방화문
	 * 양면 스티커). 충돌과 그림자는 없다. ConfigureAuthoredLeaf 뒤에 부른다.
	 */
	UStaticMeshComponent* AddLeafDressing(UStaticMesh* Mesh);

	/**
	 * 도어클로저 팔 두 마디(방화문). 클로저 몸통은 문짝 메시에 붙어 있고, 팔은 문짝의
	 * 피니언에서, 막대는 문틀 머리 밑의 슈에서 나와 팔꿈치에서 만난다. 문이 돌 때마다 두
	 * 원의 교점으로 팔꿈치를 다시 찾는다. 좌표는 이 액터 기준이고(경첩 축이 원점, 닫힌
	 * 문짝은 +Y), PinionClosed는 문이 닫혔을 때의 피니언 자리다. 두 메시 모두 원점이
	 * 회전축이고 +X로 뻗는다. 팔꿈치는 닫힌 문짝의 -X 쪽(계단 쪽)으로 꺾인다.
	 */
	void SetCloserArms(
		UStaticMesh* ArmMesh,
		UStaticMesh* RodMesh,
		const FVector& PinionClosed,
		const FVector& ShoeLocation,
		float ArmLength,
		float RodLength);

	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsOpen() const { return bOpen; }

	/** 닫힌 채 멈춰 있다. 열리기 시작하면 bOpen이 먼저 바뀌므로 닫히는 도중도 제외한다. */
	bool IsFullyClosed() const { return !bOpen && !DoorAnimation.bActive; }

	/** 문짝이 아직 돌고 있다. 지나가려는 몸은 다 열릴 때까지 기다린다. */
	bool IsSwinging() const { return DoorAnimation.bActive; }

	/**
	 * 걸쇠 자리. 손잡이 쪽 문선, 문짝 두께 한가운데, 손잡이 높이다. 저작 문짝은
	 * 원점이 바닥이라 문짝 원점에서 소리를 내면 문 소리가 문턱에서 났다.
	 */
	FVector GetLatchSoundLocation() const;

	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsLocked() const;

	/** Immediately snaps the leaf open or shut without audio or events. */
	UFUNCTION(BlueprintCallable, Category = "Door|Script")
	void ForceOpenState(bool bInOpen);

	/**
	 * Starts a lock-bypassing authored swing. A scripted close can play only
	 * the hinge creak by enabling bPlayCreak and suppressing its final thud.
	 */
	UFUNCTION(BlueprintCallable, Category = "Door|Script")
	bool BeginScriptedSwing(
		bool bInOpen,
		bool bPlayCreak = true,
		bool bSuppressCloseThud = false);

	/**
	 * 연출이 문을 움직이되 소리 크기와 빠르기를 정한다. 고임목을 뺀 방화문처럼
	 * 쾅 닫히기도(0.42, 빠르게) 손으로 잡아 천천히 닫히기도(0.08) 한다.
	 */
	bool BeginScriptedSwingWithLoudness(
		bool bInOpen,
		float Loudness,
		float DurationScale,
		bool bPlayCreak = true);

	/**
	 * 안쪽 걸쇠(§4). 걸려 있으면 밖에서 도어락을 풀어도 문이 한 뼘에서 걸린다.
	 * 안에서 여는 사람에게는 걸리지 않는다. 문을 여는 순간 같이 풀린다.
	 */
	void SetLatched(bool bInLatched, bool bPlaySound = true);
	/** 잠긴 손잡이를 밖에서 돌려 본다. 두 번 덜컥거리고 그 소리가 건물에 들린다(손님, 욕실). */
	void PlayHandleRattle();
	bool IsLatched() const { return bLatched; }
	/** 걸쇠가 걸리거나 풀렸다. 걸쇠 모양을 그리는 쪽이 듣는다. */
	FSimpleMulticastDelegate OnLatchChanged;

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual float GetInteractionHoldDuration_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;
	virtual void EndInteraction_Implementation(
		const FIGInteractionContext& Context,
		EIGInteractionEndReason EndReason) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door|Components")
	TObjectPtr<USceneComponent> HingeRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door|Components")
	TObjectPtr<USceneComponent> DoorPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door|Components")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door|Components")
	TObjectPtr<UStaticMeshComponent> HandleMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = "-179.0", ClampMax = "179.0"))
	float OpenYaw = -100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = "0.05", Units = "s"))
	float SwingDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	FText OpenPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	FText ClosePrompt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	TArray<FIGDoorRequirement> Requirements;

	/**
	 * The 없는 층 verb (§5.3): holding eases the leaf open and barely sounds,
	 * a tap yanks it and carries. Zero disables the hold entirely and restores
	 * press-to-open, which is what a locked door does so its rattle is instant.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Noise", meta = (ClampMin = "0.0", Units = "s"))
	float QuietOpenHoldSeconds = 1.4f;

	/** Reported loudness for an eased open/close, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float QuietSwingLoudness = 0.1f;

	/** Reported loudness for a yanked open/close, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NormalSwingLoudness = 0.35f;

	/** How much longer an eased swing takes than a yanked one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Noise", meta = (ClampMin = "1.0"))
	float QuietSwingDurationScale = 1.8f;

private:
	/** 걸쇠를 풀어 두고 연다. 안에서 여는 플레이어의 두 길(눌러 열기·길게 열기)이 부른다. */
	void ReleaseLatchForOpening();
	bool bLatched = false;
	void UpdateLeafCollision();
	/** 경첩 쪽 위. 삐걱은 문짝이 매달린 자리에서 운다. */
	FVector GetHingeSoundLocation() const;
	/** 닫힐 때 문짝이 문틀에 닿는 자리. */
	FVector GetStrikeSoundLocation() const;
	/** 문마다 조금씩 다른 음높이. 같은 녹음을 쓰는 문 둘이 같은 문으로 들리지 않게. */
	float GetDoorVoice() const;
	const FIGDoorRequirement* FindUnmetRequirement() const;
	bool BeginSwing(
		bool bInOpen,
		bool bPlayCreak,
		bool bSuppressCloseThud,
		float Loudness,
		float DurationScale);
	/** Reports one sound to the building's ear; silent when the bus is absent. */
	void ReportSwingNoise(float Loudness) const;

	FIGDoorAnimation DoorAnimation;
	bool bOpen = false;
	bool bSuppressNextCloseThud = false;
	// 조용히 닫았을 때 걸쇠도 같은 힘으로 닫힌다.
	float CloseThudVolume = 0.9f;
	/** 한 번이라도 열린 적이 있는가. 잠겨 있던 문의 첫 개방에만 자물쇠가 돈다. */
	bool bEverOpened = false;
	/** 유리문. 걸쇠·자물쇠 녹음은 강철 세대문의 것이라 여기서는 안 낸다. */
	bool bFramedGlass = false;
	/** 속이 빈 ABS 문짝. 두드리면 철문보다 가볍고 높게 운다. */
	bool bHollowLeaf = false;
	/** 문짝 치수(두께, 폭, 높이). 문 소리가 날 자리를 여기서 잰다. */
	FVector LeafSize = FVector(6.0f, 84.0f, 204.0f);

	/** 문이 돌았으면 클로저 팔을 다시 맞춘다. 팔이 없는 문에서는 아무것도 안 한다. */
	void UpdateCloserArms();

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CloserArm;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CloserRod;

	FVector CloserPinion = FVector::ZeroVector;
	FVector CloserShoe = FVector::ZeroVector;
	float CloserArmLength = 0.0f;
	float CloserRodLength = 0.0f;
	/** 팔꿈치가 꺾이는 쪽. 처음 맞출 때 정하고, 문이 도는 동안 뒤집히지 않는다. */
	float CloserElbowSide = 0.0f;
};
