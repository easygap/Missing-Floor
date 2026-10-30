#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Entity/IGClipAudit.h"
#include "IGManagerPatrol.generated.h"

class AIGListenerEntity;
class AIGNightLoopDirector;
enum class EIGCaptureKind : uint8;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGSwingDoor;
class UAnimSequence;
class USceneComponent;
class USkeletalMeshComponent;
class USpotLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UIGMissingFloorNarrativeSubsystem;
class UIGNoiseSubsystem;
struct FIGNoiseEvent;

/** 밤2 손수레 심부름을 마쳤다. bCaught면 그녀를 붙잡아 집에 데려다 놓고 끝났다. */
DECLARE_MULTICAST_DELEGATE_OneParam(FIGManagerErrandFinished, bool /*bCaught*/);

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
 * 열쇠 꾸러미가 없어진 걸 알면 한 번(밤3) 평소 길을 버리고 열쇠를 찾으러 간다.
 * 그때만 4층 복도에 들어와 403호 도어락을 누르고, 걸쇠가 걸려 있으면 문이 걸린 채
 * 돌아선다. 걸쇠가 없으면 문을 열고 문간에서 비춘다. 이어 옥상 철문을 열쇠로 열고
 * 물탱크 통로를 비춘 뒤 5층 철문을 본다. 빗장이 걸려 있으면 손잡이만 덜컥거리고,
 * 아니면 문을 열고 문간에서 비춘다. 5층 안으로는 끝내 들어가지 않는다. 안에 사람이
 * 보이면 문을 쾅 닫고 내려간다.
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
	/** 열쇠를 찾으러 도는 중이다. 안쪽 방으로 돌아가면 끝난다. */
	bool IsOnKeyHunt() const { return bKeyHunt; }
	/** 검사 전용. 열쇠가 없어진 걸 안 것처럼 곧바로 찾으러 나선다. */
	void StartKeyHuntForTesting();
	/** 검사 전용. 그녀 앞 80cm에 서서 곧바로 손목을 잡는다. */
	void BeginCatchForProbe();
	/** 쫓거나 잡는 중. 그동안 다른 괴이는 끼어들지 않는다. */
	bool IsPursuing() const;
	/** 월드에 쫓고 있는 관리인이 있는가. 괴이 감독이 긴장 예산을 잴 때 쓴다. */
	static bool IsAnyPursuing(const UWorld* World);
	/** 월드에 열쇠를 찾으러 도는 관리인이 있는가. 그동안 손님은 403호 문을 두드리지 않는다. */
	static bool IsAnyOnKeyHunt(const UWorld* World);

	/**
	 * 밤2, 관리실 장부를 맞춰 본 뒤(P2). 안쪽 방에서 깨어 문을 열고 필로티로 빈 손수레를
	 * 가지러 나간다. 사슬을 한참 풀고, 손수레를 밀고 돌아와 관리실 앞에 세우고, 안쪽 방에
	 * 다시 눕는다. 보고 듣고 쫓고 잡는 것은 밤3 순찰과 같다. Cart는 필로티에 손잡이를 접어 세워 묶어 둔
	 * 손수레, ParkTransform은 돌아와 다시 접어 세워 둘 자리다. OpenMesh는 펴서 끌 때, FoldedMesh는 세워 둘 때의
	 * 모습이다. 안쪽 방 문은 감독이 미리 연다.
	 */
	void StartCartErrand(
		AIGSwingDoor* InnerDoor,
		AIGSwingDoor* BoothDoor,
		AActor* Cart,
		const FTransform& ParkTransform,
		float FetchSeconds,
		UStaticMesh* OpenMesh = nullptr,
		UStaticMesh* FoldedMesh = nullptr);
	bool IsOnErrand() const { return bErrand; }
	/**
	 * 밤3, 302호에 정우가 갇혀 있는 동안은 3층(302호와 복도 끝)을 돌지 않고 3층 소리도 보러 가지
	 * 않는다. 그가 302호 문간에서 정우를 보는 장면을 만들지 않는다. 2층과 4층, 열쇠 찾기는 평소대로다.
	 * 3층을 돌던 중이면 안쪽 방으로 돌아간다. 쫓는 중이면 그 일을 마친 뒤다.
	 */
	void SetAvoidingStorage(bool bAvoid);
	bool IsAvoidingStorage() const { return bAvoidStorageFloor; }
	/** 사슬을 다 풀고 손수레를 밀며 돌아오는 중이다(바퀴 소리가 난다). */
	bool IsErrandReturning() const;
	/** 관리실 문을 나섰다. */
	FSimpleMulticastDelegate OnErrandOutside;
	/** 사슬을 풀고 손수레를 밀기 시작했다. 여기서부터 바퀴 소리가 다가온다. */
	FSimpleMulticastDelegate OnErrandReturning;
	FIGManagerErrandFinished OnErrandFinished;
	/** 검사 전용: 사슬 푸는 시간을 바꾼다. 이미 풀고 있으면 남은 시간도 줄인다. */
	void SetErrandFetchSecondsForTesting(float Seconds);
	static bool IsAnyOnErrand(const UWorld* World);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FPatrolNode
	{
		FVector Feet = FVector::ZeroVector;
		bool bOnStair = false;
		/** 열쇠를 찾으러 갈 때만 밟는 자리(4층 복도, 옥상 계단과 옥상). 평소 길은 여기를 지나지 않는다. */
		bool bHuntOnly = false;
		/** 밤2 손수레 심부름 때만 밟는 자리(안쪽 방, 관리실 안, 필로티 손수레 앞). */
		bool bErrandOnly = false;
		TArray<int32> Links;
	};

	/** 열쇠를 찾으러 가서 하는 일. */
	enum class EHuntAction : uint8
	{
		None,
		/** 403호 도어락을 누른다. 걸쇠가 있으면 문이 걸리고, 없으면 열어서 문간에서 비춘다. */
		HomeDoor,
		/** 닫혀 있으면 옥상 철문을 열쇠로 연다. */
		RoofDoorOpen,
		/** 옥상 문을 나서서 물탱크 통로를 훑는다. */
		RoofSweep,
		/** 5층 철문을 본다. 빗장이면 덜컥거리고, 아니면 열어서 문간에서만 비춘다. */
		AnnexDoor,
		/** 내려가며 옥상 철문을 닫는다. */
		RoofDoorClose
	};

	struct FHuntStop
	{
		int32 Node = INDEX_NONE;
		EHuntAction Action = EHuntAction::None;
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
	int32 AddNode(const FVector& Feet, bool bOnStair, bool bHuntOnly = false);
	int32 AddErrandNode(const FVector& Feet);
	/** 지금 길 찾기에 쓸 수 있는 점인가. 열쇠 찾기·심부름 전용 점은 그때만 쓴다. */
	bool IsNodeUsable(int32 Index) const;
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
	/** 안쪽 방 문이 열리고 열쇠 소리가 먼저 난다. 순찰이든 열쇠 찾기든 같다. */
	void LeaveRoom();
	/** 밤3, 유담이 계단 열쇠를 가져갔고 아직 찾으러 간 적이 없다. */
	bool ShouldStartKeyHunt() const;
	void StartKeyHunt();
	/** 열쇠 찾기 다음 지점으로 간다. 마지막이면 안쪽 방으로 돌아간다. */
	void AdvanceKeyHunt();
	/** 도착한 지점에서 할 일을 시작한다. 할 일이 없으면 바로 다음 지점으로 간다. */
	void BeginHuntAction();
	void TickHuntAction(float DeltaSeconds);
	/** 403호·옥상·5층 철문을 한 번 찾아 둔다. */
	void ResolveHuntDoors();
	/** 5층 안에서 유담을 봤다. 들어가지 않고 문을 쾅 닫고 내려간다. */
	void SlamAnnexAndFlee();
	/** 4층 복도는 관리인이 평소에 들어가지 않는 곳이다. 열쇠를 찾는 동안만 예외다. */
	bool IsForbiddenNow(const FVector& Feet) const;
	/** 5층 증축부 안. 관리인은 끝까지 들어가지 않는다. */
	static bool IsInsideAnnex(const FVector& Feet);
	void EnterRestingAtDoor(float RestSeconds);
	void BeginInvestigation(const FVector& Where, bool bAlarmed);
	void BeginChase();
	void BeginSearch();
	void BeginRetreat(float RestAfterSeconds);
	void BeginCatch();
	void FinishCatch();
	/**
	 * 포획 화면이 다 덮였다(밤 루프 감독). 누가 잡았든 안쪽 방으로 돌아가 있고,
	 * 그가 잡았으면 그녀를 403호 현관에 두고 계단 열쇠를 거둬 간다.
	 */
	void HandleCaptureSettled(EIGCaptureKind Kind, const FVector& CaptureSpot);
	/** 403호 문밖에서 열쇠 꾸러미, 도어락, 멀어지는 슬리퍼. 데려다 놓고 가는 소리다. */
	void PlayEscortAftermath(bool bKeyTaken);

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
	/** 계단실 방화문 셋을 처음 한 번 모은다. */
	void ResolveFireDoors() const;
	/**
	 * 이 걸음이 지나는 2·3층 방화문 가운데 닫혀 있거나 아직 도는 문. 그는 그 문을 열고
	 * 다 열릴 때까지 문 앞에 선다. 4층 문은 열지 않는다(GetFourthFloorStandNode).
	 */
	AIGSwingDoor* FindFireDoorAhead(const FVector& From, const FVector& To) const;
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

	/** 밤2 손수레 심부름의 단계. */
	enum class EErrandStep : uint8
	{
		None,
		/** 안쪽 방에서 관리실 문 밖까지. */
		Leaving,
		/** 연결통로와 필로티를 지나 손수레 앞까지. */
		ToCart,
		/** 손수레를 묶은 사슬을 푼다. 한참 걸린다. */
		Fetching,
		/** 손수레를 끌어내 돌린다. 바퀴가 끽끽거린다. */
		Turning,
		/** 손수레를 밀고 관리실 앞까지 온다. */
		Pushing,
		/** 손수레를 세워 둔다. */
		Parking,
		/** 안쪽 방으로 들어가 눕는다. */
		Returning
	};
	void TickErrand(float DeltaSeconds);
	void SetErrandStep(EErrandStep Step);
	/** 걷던 길 끝에 닿았다. 심부름의 다음 단계로 넘어간다. */
	void ArriveErrandStop();
	/** 조사나 수색에서 돌아왔다. 하던 단계의 목적지로 다시 길을 잡는다. */
	void ResumeErrandPath();
	int32 GetErrandStepTarget() const;
	/** 지나야 할 문이 닫혀 있으면 열고 다 열릴 때까지 선다. 서 있는 동안 true. */
	bool WaitForErrandDoor(float DeltaSeconds);
	void FinishErrand(bool bCaught);
	/** 손수레를 잡거나 놓는다. 놓을 때 bPark면 세워 둘 자리에, 아니면 그 자리 바닥에 둔다. */
	void AttachErrandCart(bool bAttach, bool bPark = true);
	void PlayErrandChain();
	void PlayErrandSqueak();

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
	/** 밤2 심부름에서 펴서 끄는 손수레와 다시 접어 세운 손수레. 심부름 동안 한쪽은 아무도 쓰지 않아 여기서 붙든다. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ErrandCartOpenMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ErrandCartFoldedMesh;
	/** 검사 실행에서 몸이 벽·문·소품을 지나는지 잰다(IGClipAudit). */
	FIGClipAudit ClipAudit;
	/** 2·3·4층 계단실 방화문. */
	mutable TArray<TWeakObjectPtr<AIGSwingDoor>> FireDoors;
	mutable bool bFireDoorResolved = false;
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
	FDelegateHandle CaptureSettledHandle;
	FTimerHandle DutyTimer;
	FTimerHandle EscortTimer;

	TArray<FPatrolNode> Nodes;
	TArray<FRouteStop> Route;
	int32 RestNode = INDEX_NONE;
	int32 FourthFloorDoorNode = INDEX_NONE;

	/** 밤3 302호 장면 동안 3층을 돌지 않는다. */
	bool bAvoidStorageFloor = false;
	bool IsStorageFloorNode(int32 Node) const;
	/** 밤2 손수레 심부름. */
	bool bErrand = false;
	EErrandStep ErrandStep = EErrandStep::None;
	TWeakObjectPtr<AIGSwingDoor> ErrandInnerDoor;
	TWeakObjectPtr<AIGSwingDoor> ErrandBoothDoor;
	TWeakObjectPtr<AActor> ErrandCart;
	FTransform ErrandCartPark = FTransform::Identity;
	float ErrandFetchSeconds = 110.0f;
	double ErrandStepStart = 0.0;
	double NextErrandSoundSeconds = 0.0;
	double LastErrandCaptionSeconds = -100.0;
	bool bErrandCartAttached = false;
	bool bErrandDoorClosedBehind = false;
	int32 ErrandCotNode = INDEX_NONE;
	int32 ErrandInnerInNode = INDEX_NONE;
	int32 ErrandInnerOutNode = INDEX_NONE;
	int32 ErrandOfficeNode = INDEX_NONE;
	int32 ErrandCartNode = INDEX_NONE;
	int32 ErrandParkNode = INDEX_NONE;

	/** 열쇠 찾기. 길과 자리, 문 셋, 지금 하는 일. */
	TArray<FHuntStop> HuntStops;
	int32 HuntCursor = 0;
	int32 Home403Node = INDEX_NONE;
	int32 AnnexFrontNode = INDEX_NONE;
	bool bKeyHunt = false;
	bool bHuntAction = false;
	double HuntActionStart = 0.0;
	int32 HuntActionStep = 0;
	/** 5층 문간에서 안에 있는 유담을 봤다. 문이 다 열리면 쾅 닫고 내려간다. */
	bool bSawInAnnex = false;
	TWeakObjectPtr<AIGSwingDoor> HomeDoor;
	TWeakObjectPtr<AIGSwingDoor> RoofDoor;
	TWeakObjectPtr<AIGSwingDoor> AnnexDoor;

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
