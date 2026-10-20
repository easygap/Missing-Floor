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
 * 몸통·머리와 좌우 위팔·아래팔·손 리그다(rig_store_clerk.py). 단말기는 화면 쪽(오른쪽) 손이, 프린터와
 * 영수증은 프린터 쪽(왼쪽) 손이 맡는다. 화면을 누르는 손끝이 화면에 닿을 때 삑 소리가 나고, 다른 손은
 * 그동안 계산대 모서리를 짚는다. 영수증을 뽑을 때는 계산대로 다가서서 종이가 다 나오기를 기다렸다가
 * 출구 가까이를 집어 뜯고, 그 종이를 들고 가 손님 앞 상판에 내려놓는다. 고개는 손이 가는 곳을, 일이
 * 없을 때는 손님을 본다. 팔은 두 관절 IK로 매 프레임 푼다. 기본 자세가 팔을 벌린 자세라 서 있을 때
 * 내린 팔도 IK가 만든다.
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
	EIGClerkTask GetTask() const { return CurrentTask; }
	/** 지금 하는 일을 시작하고 지난 시간(초). 일이 없으면 -1이다. */
	float GetTaskSeconds() const;
	/** 영수증 출력 일에서 프린터가 돌기 시작한 뒤다. 검사가 자막 없이도 장면을 잡을 수 있게 둔다. */
	bool IsPrinting() const { return CurrentTask == EIGClerkTask::PrintReceipt && FiredEvents >= 2; }

	/** 뽑은 영수증을 손님 앞 계산대에 놓는 자리. 프린터 바로 앞이다. */
	static FTransform GetReceiptRestTransform();
	/** 프린터 출구. 종이가 나오고 출력 소리가 나는 자리다. */
	static FVector GetPrinterExit();
	/** 포스 직원 화면. 단말기 누르는 소리가 나는 자리다. */
	static FVector GetPosScreen();

	/** 손 하나가 갈 곳(모두 월드). 손끝 자리, 손목에서 손끝으로 향하는 쪽, 손바닥이 향할 쪽, 팔꿈치가 향할 쪽. */
	struct FHandGoal
	{
		FVector Tip = FVector::ZeroVector;
		FVector Pointing = FVector::DownVector;
		FVector Palm = FVector::DownVector;
		FVector Pole = FVector::BackwardVector;
	};

private:
	/** 손님이 가까이 있거나 일하는 동안에만 틱을 켠다. */
	void UpdateTickState();
	void ApplyPose(double Now);
	void FireCuesUpTo(float TaskSeconds);
	void PlayBeep();
	void UpdateReceiptStrip(float TaskSeconds);
	void FinishTask();

	/** 팔 하나의 뼈 이름과 기준 자세(컴포넌트 공간). */
	struct FClerkArm
	{
		FName Upper;
		FName Lower;
		FName Hand;
		FTransform RefUpper = FTransform::Identity;
		FTransform RefLower = FTransform::Identity;
		FTransform RefHand = FTransform::Identity;
		/** 기준 자세의 손가락 끝(손끝 뼈 자리). */
		FVector RefTip = FVector::ZeroVector;
		/** 기준 자세에서 손바닥이 향하는 쪽. 허벅지 쪽(몸 안쪽)으로 본다. */
		FVector RefPalm = FVector::ZeroVector;
		float UpperLength = 0.0f;
		float LowerLength = 0.0f;
		float HandLength = 0.0f;
		/** 이 팔이 몸 가운데에서 어느 쪽(컴포넌트 공간 X 부호)에 있는지. */
		float Side = 0.0f;
	};
	/** 일과 시각에 따라 두 손이 갈 곳. 쉬는 자세와 섞기 전의 값이다. */
	void TaskHands(float TaskSeconds, FHandGoal OutGoals[2]) const;
	/** 서 있을 때 몸 옆에 내린 손. */
	FHandGoal RestingHand(int32 Index) const;
	/** 일하지 않는 손. 계산대 뒷모서리 안쪽 상판을 손끝으로 짚는다. */
	FHandGoal CounterHand(int32 Index) const;
	/** 몸 바깥쪽(월드). 팔꿈치가 벌어지는 쪽이다. */
	FVector Outward(int32 Index) const;
	void SolveArm(int32 Index, const FQuat& TorsoDelta, const FVector& TorsoJoint, const FHandGoal& Goal);
	/** 뜯은 영수증을 집은 손끝에 매단다. 내려놓는 순간 계산대 위 영수증과 같은 자리·같은 크기가 된다. */
	void UpdateCarriedReceipt(float TaskSeconds);

	FClerkArm Arms[2];
	bool bHasArms = false;
	/** 단말기를 누르는 손(포스 화면 쪽 어깨). 다른 손은 프린터와 영수증을 맡는다. */
	int32 WorkingHand = 0;
	/** 마지막으로 푼 손끝 자리(월드). 든 영수증을 여기에 매단다. */
	FVector SolvedTip[2] = {FVector::ZeroVector, FVector::ZeroVector};
	/** 계산대 앞 원래 자리. 일하는 동안은 계산대 쪽으로 다가선다. */
	FVector HomeLocation = FVector::ZeroVector;
	/** 일하던 마지막 프레임의 손과 자세. 일을 마치면 여기서 쉬는 자세로 돌아간다. */
	FHandGoal HeldGoals[2];
	bool bHeldGoals = false;
	float HeldLean = 0.0f;
	float HeldStep = 0.0f;
	/** 고개를 돌리고 숙인 각(도). 볼 곳이 바뀌면 목이 따라 돈다. */
	float HeadYaw = 0.0f;
	float HeadAim = 0.0f;
	/** 손마다 마지막으로 손 닿음·집기 파고듦을 검사 로그에 남긴 시각. */
	double LastReachLogAt[2] = {-1.0, -1.0};

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
