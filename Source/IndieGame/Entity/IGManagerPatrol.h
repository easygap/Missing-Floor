#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGManagerPatrol.generated.h"

class AIGListenerEntity;
class AIGNightLoopDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGSwingDoor;
class UAnimSequence;
class USceneComponent;
class USkeletalMeshComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UIGMissingFloorNarrativeSubsystem;
class UIGNoiseSubsystem;
struct FIGNoiseEvent;

UENUM()
enum class EIGManagerPatrolState : uint8
{
	/** 관리실 안쪽 방. 몸은 없다. */
	Resting,
	/** 정해 둔 길을 돈다. */
	Patrolling,
	/** 들은 자리, 언뜻 본 자리로 간다. */
	Investigating,
	/** 그녀를 봤다. */
	Chasing,
	/** 놓친 자리에서 둘러본다. */
	Searching,
	/** 4층 복도 앞에서 서서 손전등만 비춘다. */
	Staring,
	/** 위층 사람이 두드리는 소리에 굳었다. */
	Frozen,
	/** 관리실로 서둘러 돌아간다. */
	Retreating,
	/** 손목을 잡았다. */
	Catching
};

/**
 * 밤3의 관리인 목한수(EXPANSION_PLAN §3).
 *
 * 1층 관리실에서 나와 계단탑으로 2층과 3층 복도를 돌고, 4층 계단 목에서
 * 손전등만 비추고 돌아선다. 4층 복도에는 들어가지 않는다. 손전등 원뿔 안에
 * 든 사람을 보고, 큰 소리가 나면 그리로 간다. 바짝 붙으면 등 뒤도 안다.
 * 잡히면 손목을 잡히고 손전등이 얼굴에 들이대진 채 방에서 깬다. 추격 없음
 * 난이도에서는 비추기만 하고 돌아간다.
 *
 * 말은 하지 않는다. 밤4 전까지 그는 유담에게 한 마디도 하지 않는다. 자막은
 * 슬리퍼와 열쇠 꾸러미 같은 소리뿐이다.
 *
 * 위층 사람이 두드리면 그 자리에서 굳고, 관리실로 서둘러 돌아간다. 벽 안의
 * 대답(T9)을 들은 뒤에는 나오지 않는다. 그 밤의 남은 길은 위층 사람의 것이다.
 *
 * 몸에는 충돌이 없다. 길은 계단탑과 복도의 빈 자리만 지나는 점 그래프다. 몸은
 * 리깅한 스켈레탈(SK_MokHansooPatrol)이 슬리퍼를 끌며 걷고, 쫓을 때는 허둥지둥
 * 뛴다. 서서 훑을 때와 노크에 굳을 때, 손목을 잡을 때 동작이 따로 있다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGManagerPatrol : public AActor
{
	GENERATED_BODY()

public:
	AIGManagerPatrol();

	void Configure(
		AIGPrologueWorldScene* InScene,
		AIGPlayerCharacter* InPlayer,
		AIGListenerEntity* InListener,
		AIGNightLoopDirector* InNightLoop);

	virtual void Tick(float DeltaSeconds) override;

	EIGManagerPatrolState GetPatrolState() const { return State; }
	bool IsOnDuty() const { return bOnDuty; }
	/** 쫓거나 잡는 중. 그동안 다른 괴이는 끼어들지 않는다. */
	bool IsPursuing() const;
	/** 월드에 쫓고 있는 관리인이 있는가. 괴이 감독이 긴장 예산을 잴 때 쓴다. */
	static bool IsAnyPursuing(const UWorld* World);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FPatrolNode
	{
		FVector Feet = FVector::ZeroVector;
		bool bOnStair = false;
		TArray<int32> Links;
	};

	struct FRouteStop
	{
		int32 Node = INDEX_NONE;
		float DwellSeconds = 0.0f;
		/** 머무는 동안 비출 방향. bFixedLook이면 그쪽을 중심으로 훑고, 아니면 걸어온 방향을 훑는다. */
		float LookYaw = 0.0f;
		bool bFixedLook = false;
	};

	void BuildGraph();
	int32 AddNode(const FVector& Feet, bool bOnStair);
	void LinkNodes(int32 A, int32 B);
	/** 다익스트라. 노드가 오십 개 남짓이라 매번 새로 푼다. */
	bool FindPath(int32 From, int32 To, TArray<int32>& OutPath) const;
	/** 발 높이를 무겁게 쳐서 다른 층 노드를 고르지 않는다. */
	int32 FindNearestNode(const FVector& Feet) const;
	void SetDestination(int32 Node);
	/** 다음 순찰 자리로. 4층 목 근처에 위층 사람이 있으면 그 자리를 건너뛴다. */
	void AdvanceRoute();

	/** 그 시간이 밤3이고 대답을 아직 못 들었을 때만 순찰한다. 0.25초마다 본다. */
	void RefreshDuty();
	void GoOnDuty();
	void GoOffDuty();
	void ShowBody(bool bShow);
	/** 안쪽 방 자리로 옮긴다. 걷던 길은 버린다. */
	void SnapToRestNode();

	void StartRound();
	void EnterRestingAtDoor(float RestSeconds);
	void BeginInvestigation(const FVector& Where, bool bAlarmed);
	void BeginChase();
	void BeginSearch();
	void BeginRetreat(float RestAfterSeconds);
	void BeginCatch();
	void FinishCatch();

	void TickResting(float DeltaSeconds);
	void TickWalking(float DeltaSeconds, float Speed);
	void TickChase(float DeltaSeconds);
	void TickDwell(float DeltaSeconds);
	void UpdateSenses(float DeltaSeconds);
	/** 한 걸음 옮긴다. 도착했으면 참이다. */
	bool StepToward(const FVector& Target, float Speed, float DeltaSeconds);
	void FaceYaw(float DesiredYaw, float DeltaSeconds, float DegreesPerSecond = 220.0f);
	void AimTorch(const FVector& WorldTarget, float DeltaSeconds);
	void RelaxTorch(float DeltaSeconds);
	void AnimateGait(float DeltaSeconds, float Speed);
	/** 리깅한 몸이 있으면 싣는다. 없으면 예전 정적 조각 둘로 남는다. */
	bool BuildSkeletalBody();
	void PlayBodyAnim(UAnimSequence* Sequence, bool bLoop, float Rate);
	/** 4층 계단실 방화문. 닫혀 있으면 그 문을 열고 복도를 비추지 않는다. 문 앞 참에서 선다. */
	bool IsFireDoorClosed() const;
	/** 4층에서 서는 자리. 방화문이 닫혀 있으면 문 안쪽 참이다. */
	int32 GetFourthFloorStandNode() const;
	/** 놓친 자리 둘레에서 들러 볼 점들. 같은 층의 방과 복도 끝이다. */
	void QueueSearchStops(const FVector& Around);
	/** 숨은 그녀를 꺼내러 갈 자리. 숨은 가구 앞 바닥이다. */
	FVector GetHideApproachFeet() const;
	void PlayStep();
	void PlayKeys(bool bForceCaption);
	void Caption(const FText& Text, const FVector& Where, double& LastShown, float MinGapSeconds);

	bool IsPlayerVisibleInTorch(float& OutDistance) const;
	bool IsPlayerCloseBehind() const;
	bool IsPlayerTorchOnMe() const;
	bool HasClearLine(const FVector& From, const FVector& To) const;
	FVector GetFeet() const { return GetActorLocation(); }
	FVector GetPlayerFeet() const;
	/** 4층 복도(목 기둥 동쪽)는 그가 들어가지 않는 곳이다. */
	static bool IsForbiddenFourthFloor(const FVector& Feet);
	bool IsPlayerWatching(const FVector& Where) const;
	bool IsCaptureAllowed() const;
	void ThinkOnce(const TCHAR* BeatId, const FText& Thought);
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;

	void HandleNoise(const FIGNoiseEvent& Event);
	void HandleListenerKnock(const FVector& Where);

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Root;
	/** 몸과 손전등을 함께 흔든다. 메시의 정면은 -X라 몸만 180도 돌려 붙인다. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> BodyPivot;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Workwear;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HeadHands;
	UPROPERTY(Transient)
	TObjectPtr<USpotLightComponent> Torch;
	/** 리깅한 몸. 있으면 위 정적 조각 둘은 숨긴다. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> BodySkeletal;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> WalkAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> RunAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LookAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FreezeAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> GrabAnim;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActiveAnim;
	TWeakObjectPtr<AIGSwingDoor> FireDoor;
	bool bFireDoorResolved = false;
	/** 수색에서 들를 점과, 지금 몇 번째인가. */
	TArray<int32> SearchStops;
	int32 SearchStopCursor = 0;
	/** 숨는 걸 보고 꺼내러 간 지 얼마나 됐나. 가구에 닿지 못하면 놓아 준다. */
	double HideApproachStartSeconds = -100.0;

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGNightLoopDirector> NightLoop;
	TWeakObjectPtr<UIGNoiseSubsystem> NoiseSubsystem;
	FDelegateHandle NoiseHandle;
	FDelegateHandle KnockHandle;
	FTimerHandle DutyTimer;

	TArray<FPatrolNode> Nodes;
	TArray<FRouteStop> Route;
	int32 RestNode = INDEX_NONE;
	int32 FourthFloorDoorNode = INDEX_NONE;

	EIGManagerPatrolState State = EIGManagerPatrolState::Resting;
	bool bOnDuty = false;
	bool bBodyShown = false;
	int32 RouteCursor = 0;
	TArray<int32> Path;
	int32 PathCursor = 0;
	/** 길 끝에서 마지막으로 걸어갈 자리. 노드가 아닐 때(조사한 소리)만 쓴다. */
	FVector FinalTarget = FVector::ZeroVector;
	bool bHasFinalTarget = false;

	double StateStartSeconds = 0.0;
	double RestUntilSeconds = 0.0;
	float DwellRemaining = 0.0f;
	float DwellBaseYaw = 0.0f;
	float DwellElapsed = 0.0f;
	float RestAfterRetreat = 40.0f;
	int32 RoundCount = 0;

	/** 0..1. 1이 되면 쫓는다. 원뿔 밖이면 천천히 빠진다. */
	float Exposure = 0.0f;
	FVector LastKnownPlayerFeet = FVector::ZeroVector;
	double LastSeenSeconds = -100.0;
	double LastRepathSeconds = -100.0;
	/** 숨는 걸 봤는가. 숨기 직전 1초 안에 원뿔이나 곁에 있었으면 그 자리를 연다. */
	bool bSawPlayerHide = false;
	FVector HideSpotFeet = FVector::ZeroVector;
	bool bWasPlayerConcealed = false;

	int32 SeenCaptureCount = 0;
	double CatchStartSeconds = 0.0;
	bool bCatchResolved = false;

	float DistanceSinceStep = 0.0f;
	float GaitPhase = 0.0f;
	uint32 StepSeed = 0x6D6F6B21u;
	double NextKeysSeconds = 0.0;
	double LastStepCaptionSeconds = -100.0;
	double LastKeysCaptionSeconds = -100.0;
	double LastAlarmCaptionSeconds = -100.0;
	FRotator TorchRelative = FRotator(-8.0f, 0.0f, 0.0f);
};
