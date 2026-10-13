#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Entity/IGWalkerKit.h"
#include "IGSiteCheckVisit.generated.h"

class AIGSiteCheckVisit;
class AIGSwingDoor;
class UAnimSequence;
class UCapsuleComponent;
class USkeletalMeshComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(FIGSiteCheckChecked, AIGSiteCheckVisit*, bool /*bWitnessed*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FIGSiteCheckEvent, AIGSiteCheckVisit*);

/**
 * 신고 다음 날 아침의 현장 확인(설정집 §1 밤3 이후 현실 대응).
 *
 * 순찰 경찰관 하나와 관리인 목한수가 4층 옥상 계단 밑에서 나와 옥상 철문과 5층 철문을
 * 관리인 열쇠로 열고 들어간다. 경찰관은 가운데 칸 벽 앞에 서고, 관리인은 문 쪽을 보고
 * 선다. 유담이 5층에 와 있으면 경찰관이 벽을 세 번 두드리고 귀를 댄 뒤 돌아서서 말한다.
 * 낮의 벽은 대답하지 않는다. 끝내 안 올라오면 둘이서 확인하고, 노크는 아래층까지 희미하게
 * 들린다. 결과는 문자로 온다.
 *
 * 말을 마치면 경찰관은 벽 사진을 몇 장 더 찍고, 유담이 옥상을 떠나면 둘은 사라진다.
 * 오래 같이 있으면 계단으로 걸어 내려간다. 관리인이 연 문은 그녀가 보지 않을 때 닫는다.
 *
 * 몸은 리깅한 SK_PolicePatrol과 SK_MokHansooPatrol이다. 충돌은 플레이어 몸만 막는다.
 * 틱은 나와 있는 동안에만 켠다. 비트와 문자는 디렉터가 쥐고, 벽 앞의 말은 이 액터가 띄운다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGSiteCheckVisit : public AActor
{
	GENERATED_BODY()

public:
	enum class EStage : uint8
	{
		/** 아직 안 나왔다. */
		Hidden,
		/** 옥상 계단을 올라 5층으로 간다. */
		Climbing,
		/** 제자리에 서서 그녀를 기다린다. */
		Waiting,
		/** 벽을 두드리고 귀를 댄다. */
		Knocking,
		/** 돌아서서 말한다. */
		Talking,
		/** 사진을 찍으며 남아 있다. */
		Lingering,
		/** 계단으로 걸어 내려간다. */
		Leaving,
		/** 갔다. 연 문을 아직 못 닫았으면 그것만 기다린다. */
		Gone
	};

	AIGSiteCheckVisit();

	/** 두 사람의 몸과 동작을 불러온다. 하나라도 없으면 false이고 문자로만 확인한다. */
	bool LoadBodies();
	/** 관리인이 열고 닫을 옥상 철문과 5층 철문. */
	void SetGates(AIGSwingDoor* InStairGate, AIGSwingDoor* InAnnexGate);
	/** 4층 옥상 계단 밑에서 나와 올라가기 시작한다. 그녀가 그 자리를 보고 있으면 false다. */
	bool TryBegin();
	/** 그녀를 기다리지 않는다. 아직 오는 중이고 아무도 안 보면 제자리로 옮겨 바로 두드린다. */
	void Hurry();
	/** 밤이 왔다. 그 자리에서 없애고 연 문을 닫는다. */
	void Vanish();

	virtual void Tick(float DeltaSeconds) override;

	EStage GetStage() const { return Stage; }
	bool IsOut() const { return Stage != EStage::Hidden && Stage != EStage::Gone; }
	bool WasWitnessed() const { return bWitnessed; }
	FVector GetOfficerFeet() const;
	FVector GetManagerFeet() const;
	/** 검사용. */
	int32 GetKnockCount() const { return KnockCount; }
	int32 GetStepCount() const { return StepCount; }
	int32 GetSpokenLineCount() const { return LineIndex; }

	/** 벽에 귀를 대고 난 뒤. bWitnessed면 그녀가 옆에서 봤다. 디렉터가 비트를 남긴다. */
	FIGSiteCheckChecked OnChecked;
	/** 둘 다 사라졌다. */
	FIGSiteCheckEvent OnGone;

	/** 경찰관이 서는 자리. 가운데 칸 벽 앞이다. */
	static FVector GetOfficerSpotFeet();
	/** 관리인이 서는 자리. 첫 칸 앞에서 문 쪽을 본다. */
	static FVector GetManagerSpotFeet();
	/** 5층 철문 안이나 그 앞에서 그녀가 두 사람을 볼 수 있는가. */
	static bool IsInAnnex(const FVector& Feet);

private:
	/** 한 사람의 걸음. 컴포넌트와 동작은 액터의 UPROPERTY가 쥔다. */
	struct FWalker
	{
		USceneComponent* Feet = nullptr;
		UCapsuleComponent* Blocker = nullptr;
		USkeletalMeshComponent* Body = nullptr;
		UAnimSequence* IdleAnim = nullptr;
		UAnimSequence* WalkAnim = nullptr;
		UAnimSequence* ActiveAnim = nullptr;
		/** 동작 배속 1일 때 몸이 나아가는 빠르기(cm/s)와 한 걸음 시간. */
		float CycleSpeed = 100.0f;
		float HalfCycleSeconds = 0.5f;
		/** 옥상 계단은 디딤판 22 cm에 단 17 cm라, 수평 1 cm에 0.3 cm 넘게 오르면 계단이다. */
		FIGWalkerRoute Path;
		float Travelled = 0.0f;
		float StepPhase = 0.0f;
		uint32 StepSeed = 0u;
		bool bWalking = false;
		bool bShown = false;
		double LastSeenAt = 0.0;
	};

	void SetRoute(FWalker& Walker, const TArray<FVector>& Points) const;
	FVector RoutePoint(const FWalker& Walker, float Distance, FVector* OutDirection = nullptr, bool* bOutOnStair = nullptr) const;
	static float RouteLength(const FWalker& Walker) { return Walker.Path.Length(); }
	/** 한 프레임 걷는다. 다 왔으면 true. */
	bool Walk(FWalker& Walker, float DeltaSeconds, bool bHold);
	void PlayBody(FWalker& Walker, UAnimSequence* Sequence, float Rate, bool bLoop = true);
	void PlayStep(FWalker& Walker, bool bOnStair);
	void Turn(FWalker& Walker, float TargetYaw, float DeltaSeconds, float Speed) const;
	void TurnToward(FWalker& Walker, const FVector& Target, float DeltaSeconds, float Speed) const;
	void Place(FWalker& Walker, const FVector& Feet, float Yaw) const;
	void Show(FWalker& Walker, bool bVisible);
	bool IsPlayerInTheWay(const FWalker& Walker) const;

	/** 관리인이 문 앞에서 열쇠로 연다. 열려 있거나 다 열렸으면 true. */
	bool OpenGateAhead(float DeltaSeconds, double Now);
	void TickClimbing(float DeltaSeconds, double Now);
	void TickWaiting(float DeltaSeconds, double Now);
	void TickKnocking(float DeltaSeconds, double Now);
	void TickTalking(float DeltaSeconds, double Now);
	void TickLingering(float DeltaSeconds, double Now);
	void TickLeaving(float DeltaSeconds, double Now);
	void BeginKnocking(double Now);
	void BeginLeaving(double Now);
	void FinishChecked(double Now);
	void PlayWallKnock(int32 Index);
	/** 벽 앞의 말 한 줄을 띄우고, 그 줄이 화면에 머물 시간을 돌려준다. */
	float PushLine(int32 Index);
	/** 두 사람을 거두고 연 문을 닫으러 간다. */
	void Withdraw();
	/** 관리인이 연 문을 그녀가 안 볼 때 닫는다. 다 닫았으면 true. */
	bool RestoreGates();

	bool GetPlayerFeet(FVector& OutFeet) const;
	bool GetPlayerEyes(FVector& OutEyes) const;
	bool CanPlayerSee(const FVector& Location, float MaxDistance) const;
	/** 그녀가 두 사람 곁에 있다. 5층 안이거나, 문간에서 경찰관이 보인다. */
	bool IsPlayerWithThem() const;
	bool IsPlayerOnRoof() const;
	bool IsDialogueLaneIdle() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> OfficerFeet;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCapsuleComponent> OfficerBlocker;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> OfficerBody;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> ManagerFeet;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCapsuleComponent> ManagerBlocker;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> ManagerBody;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Animations;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> KnockAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ListenAnim;

	TWeakObjectPtr<AIGSwingDoor> StairGate;
	TWeakObjectPtr<AIGSwingDoor> AnnexGate;

	FWalker Officer;
	FWalker Manager;

	EStage Stage = EStage::Hidden;
	double StageStartedAt = 0.0;
	/** 그녀가 곁에 있기 시작한 때. 음수면 없다. */
	double WithHerSince = -1.0;
	/** 그녀가 옥상에 없던 시간을 더한다. 오래 안 오면 둘이서 확인한다. */
	float WaitedAlone = 0.0f;
	bool bHurry = false;
	bool bWitnessed = false;
	bool bOpenedStairGate = false;
	bool bOpenedAnnexGate = false;
	/** 문 앞에서 열쇠를 꺼낸 때. 음수면 아직이다. */
	double KeysOutAt = -1.0;
	/** 안에서 빗장이 걸려 있어 두드려 본 횟수. */
	int32 DoorKnocks = 0;
	double LastDoorKnockAt = -100.0;
	int32 KnockCount = 0;
	int32 LineIndex = 0;
	double NextLineAt = -1.0;
	double LineDeadlineAt = -1.0;
	int32 ShutterCount = 0;
	int32 StepCount = 0;
	bool bCaptionedSteps = false;
	bool bCaptionedKnock = false;
	bool bThoughtPushed = false;
};
