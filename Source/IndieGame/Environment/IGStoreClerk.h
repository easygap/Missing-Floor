#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGStoreClerk.generated.h"

class UPoseableMeshComponent;
class UStaticMeshComponent;

/** 나린이 계산대에서 하는 일. 몸짓과 그 몸짓이 내는 소리가 같은 시계로 간다. */
enum class EIGClerkTask : uint8
{
	None,
	/** 포스 화면을 들여다보며 몇 번 눌러 기록을 찾는다. */
	SearchRecord,
	/** 출력을 누르고 프린터 쪽으로 돌아 영수증을 뽑아 뜯고, 손님 앞에 놓는다. */
	PrintReceipt
};

/** 일하는 도중 바깥(자막·영수증)이 맞춰야 할 순간. */
enum class EIGClerkCue : uint8
{
	/** 단말기를 처음 눌렀다. */
	FirstTap,
	/** 프린터가 돌기 시작했다. */
	PrintStarted,
	/** 손님 앞 계산대에 영수증을 내려놓았다. */
	ReceiptPlaced,
	/** 하던 일을 마치고 손님 쪽으로 돌아섰다. */
	Finished
};

/**
 * 계산대 안에서 손님을 바라보는 나린. 가려진 동안에는 포즈 계산을 쉰다.
 *
 * 리그는 몸통과 머리 두 마디뿐이라 손은 움직이지 않는다. 대신 어디를 보고 몸을 어디로 기울이는지로
 * 일을 보여 준다. 단말기를 누를 때마다 몸이 조금 숙여지고 그 순간에 삑 소리가 나며, 프린터 쪽으로
 * 돌아 종이가 나오는 동안 들여다보다 뜯을 때 몸이 짧게 들린다.
 */
UCLASS()
class INDIEGAME_API AIGStoreClerk : public AActor
{
	GENERATED_BODY()

public:
	AIGStoreClerk();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 일을 시작한다. 하던 일이 있으면 거두고 새로 한다. 끝날 때까지 걸리는 시간(초)을 돌려준다.
	 * 신호는 그 순간에 한 번씩 오고, 마지막은 늘 Finished다(거두면 오지 않는다).
	 */
	float StartTask(EIGClerkTask Task, TFunction<void(EIGClerkCue)> OnCue);
	/** 하던 일을 그 자리에서 거둔다. 나던 소리와 나오던 종이도 같이 거둔다. */
	void CancelTask();
	bool IsBusy() const { return CurrentTask != EIGClerkTask::None; }
	/** 영수증 출력 일에서 프린터가 돌기 시작한 뒤다. 검사가 자막 없이도 장면을 잡을 수 있게 둔다. */
	bool IsPrinting() const { return CurrentTask == EIGClerkTask::PrintReceipt && FiredEvents >= 2; }

	/** 뽑은 영수증을 손님 앞 계산대에 놓는 자리. 프린터 바로 앞이다. */
	static FTransform GetReceiptRestTransform();
	/** 프린터 출구. 종이가 나오고 출력 소리가 나는 자리다. */
	static FVector GetPrinterExit();
	/** 포스 직원 화면. 단말기 누르는 소리가 나는 자리다. */
	static FVector GetPosScreen();

private:
	/** 손님이 가까이 있거나 일하는 동안에만 틱을 켠다. */
	void UpdateTickState();
	void ApplyPose(double Now);
	void FireCuesUpTo(float TaskSeconds);
	void PlayBeep();
	void UpdateReceiptStrip(float TaskSeconds);
	void FinishTask();

	FTimerHandle FacingTimer;
	UPROPERTY() TObjectPtr<UPoseableMeshComponent> Body;
	/** 프린터에서 밀려 나오는 감열지. 뜯으면 사라지고 계산대 위의 영수증이 대신 선다. */
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ReceiptStrip;
	TWeakObjectPtr<class UAudioComponent> PrintSound;

	FTransform RefTorso = FTransform::Identity;
	FTransform RefHead = FTransform::Identity;
	bool bHasRig = false;

	EIGClerkTask CurrentTask = EIGClerkTask::None;
	TFunction<void(EIGClerkCue)> TaskCue;
	double TaskStartedAt = -1.0;
	/** 일을 마치고 쉬는 자세로 돌아가는 시작 시각과 그때의 몸짓 세기. */
	double RelaxStartedAt = -1.0;
	float RelaxFromWeight = 0.0f;
	/** 이번 일에서 이미 낸 신호와 소리의 수. 틱이 늦게 와도 하나씩 차례로 낸다. */
	int32 FiredEvents = 0;
	float CurrentYaw = 0.0f;
};
