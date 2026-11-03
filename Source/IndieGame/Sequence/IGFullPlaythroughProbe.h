#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sequence/IGInputRoute.h"
#include "IGFullPlaythroughProbe.generated.h"

class AIGListenerGreyboxDirector;
class AIGNightPhaseDirector;
class AIGPlayerCharacter;
class UIGMissingFloorNarrativeSubsystem;

/**
 * -IGFullPlaythrough 전용. 새 게임을 처음부터 실제 키·마우스 입력으로만 해 본다.
 *
 * 순간 이동, 상호작용 직접 호출, 목표 채우기, 검사용 시작은 쓰지 않는다. 게임이 띄운 목표를 사람이
 * 하듯 차례로 한다. 물건 앞에 설 자리와 가는 길은 그때그때 실제 충돌로 찾는다. 가구를 옮겨도 검사를
 * 고칠 필요가 없고, 길이 정말 막히면 막혔다고 적는다. 계단은 씬의 디딤판 점을 밟고, 닫힌 문은 앞에
 * 서서 E를 누른다(FIGInputRoute).
 *
 * 결과는 어디까지 갔는가다. 단계마다 PLAYTHROUGH_STAGE 줄을 남기고, 걷다 막히거나 기다리던 일이
 * 오지 않으면 길과 설 자리를 다시 찾아 세 번까지 해 본다. 그래도 안 되면 그 자리에서 멈추고
 * PLAYTHROUGH FAIL과 막힌 곳을 적는다. 단계마다 화면을 한 장씩 남긴다(Saved/FullPlaythrough).
 *
 * 지금은 입주 저녁부터 셋째 밤을 지나 셋째 아침까지 간다. 그다음 낮과 밤은 차례로 넓힌다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGFullPlaythroughProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGFullPlaythroughProbe();
	virtual void Tick(float DeltaSeconds) override;
	void Configure(AIGPlayerCharacter* InPlayer, AIGListenerGreyboxDirector* InDirector);

private:
	/** 차례로 하는 일 하나. 시작할 때 입력 길을 채운다. 그때 있는 액터를 찾으려고 늦게 짓는다. */
	struct FLeg
	{
		FString Name;
		TFunction<void()> Build;
	};

	/** 길 찾기 격자. 칸은 처음 볼 때 잰다. */
	struct FPlanGrid
	{
		FVector2D Origin = FVector2D::ZeroVector;
		int32 Width = 0;
		int32 Height = 0;
		/** 이 높이 사이의 바닥만 쓴다. 출발과 목표 바닥 높이에 여유를 둔다. */
		float BandMin = 0.0f;
		float BandMax = 0.0f;
		/** 0 아직 안 봄, 1 막힘, 2 설 수 있음. */
		TArray<uint8> State;
		/** 벽이나 가구에 붙어 있어 여유가 없는 칸. */
		TArray<uint8> Tight;
		TArray<float> Floor;
	};

	void Check(bool bPass, const FString& Name);
	void Finish();
	void Shoot(const FString& Name);
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;
	bool HasBeat(const TCHAR* Beat) const;
	AActor* FindNamed(const FString& Name) const;
	/** 그 종류의 첫 액터. 무대에 하나씩만 서는 감독·씬을 찾는다. */
	template <typename T>
	T* FindFirst() const;
	bool IsTalkBusy() const;
	/** 게임이 「아침까지 쉬기」를 막는 동안인가. 쫓기거나 손님이 있거나, 위층 사람이 문 앞에 있다. */
	bool IsRestBlocked() const;
	FString GetObjective() const;
	void AddLeg(const FString& Name, TFunction<void()> Build);
	void StartNextLeg();

	/** 단계 이름을 남기고 화면을 한 장 찍는다. */
	void Stage(const FString& Name);
	/**
	 * Goal(발 높이)까지 걷는다. 길은 실제 충돌로 찾고, 걷다 막히면 그 자리에서 다시 찾는다. 사람이 길을 막고 있으면
	 * Refuge로 물러나 기다린다. 밤 복도에서 위층 사람을 지나 보내는 자리다(집 현관 안).
	 */
	void GoTo(const FVector& Goal, float Tolerance, const FString& Name, TOptional<FVector> Refuge = TOptional<FVector>());
	TFunction<void()> MakeGo(FVector Goal, float Tolerance, FString Name, int32 Attempt, int32 Waits,
		TOptional<FVector> Refuge);
	/** 정해 둔 점(발 높이)을 차례로 밟는다. 계단처럼 격자 한 장으로 못 찾는 길이다. */
	void Follow(const TArray<FVector>& Points, float Tolerance, const FString& Name);
	TFunction<void()> MakeFollow(TArray<FVector> Points, float Tolerance, FString Name, int32 Attempt);
	/** 계단탑으로 FromFloor 출입구에서 ToFloor 출입구까지 오르거나 내려간다. */
	void TakeStairs(int32 FromFloor, int32 ToFloor);
	/**
	 * 물건을 쓴다. 겨눌 수 있는 자리를 찾아 가서 겨누고, 그 물건이 요구하는 만큼 E를 누른 뒤 Done을
	 * 기다린다. Stand를 주면 그 자리(발 높이)에 선다. Presses는 한 자리에서 거듭 누르는 횟수다(먹지를
	 * 몇 번에 걸쳐 문지르는 것처럼). Done이 되면 남은 누르기는 건너뛴다.
	 */
	void Use(const FString& Name, TFunction<AActor*()> FindTarget, const FVector& AimOffset,
		TFunction<bool()> Done, float Timeout, TOptional<FVector> Stand = TOptional<FVector>(), int32 Presses = 1);
	void UseNamed(const FString& Name, const FString& ActorName, const FVector& AimOffset,
		TFunction<bool()> Done, float Timeout, TOptional<FVector> Stand = TOptional<FVector>(), int32 Presses = 1);
	TFunction<void()> MakeUse(FString Name, TFunction<AActor*()> FindTarget, FVector AimOffset,
		TFunction<bool()> Done, float Timeout, TOptional<FVector> Stand, int32 Presses, int32 Attempt, int32 Waits = 0);
	/**
	 * 길이 막혔을 때 잠깐 기다렸다가 같은 일을 다시 짓는다. 문 앞에 서 있던 사람이 비키기를 기다리는 것과
	 * 같다. 기다릴 만큼 기다렸으면 false.
	 */
	bool WaitAndRetry(const FString& Name, int32 Waits, TFunction<void()> Again, const FString& Why,
		TOptional<FVector> Refuge = TOptional<FVector>());
	/** 사람이 E를 누르고 있어야 하는 시간. 물건의 홀드 시간에 접근성 배율을 곱한다. */
	float GetHoldSeconds(AActor* Target) const;
	/**
	 * 겨누고 한 번 누른다. Remaining이 남았으면 됐는지 보고, 아직이면 한숨 돌린 뒤 다음 누르기를 새 일로
	 * 끼워 넣는다. 마지막 누르기 뒤에는 Done을 기다린다.
	 */
	void QueuePress(const FString& Name, AActor* Target, const FVector& Aim, float Hold, TFunction<bool()> Done,
		float Timeout, int32 Remaining);
	void WaitUntil(TFunction<bool()> Done, float Timeout, const FString& Name);
	void WaitSeconds(float Seconds, const FString& Name);
	/** 말이 다 끝나기를 기다린다. 사람도 듣고 나서 움직인다. */
	void Listen(const FString& Name, float Timeout);
	/** 겨누지 않고 키만 누른다(펼친 종이 덮기). */
	void PressKey(const FString& Name, const FKey& Key, float Seconds);
	/** 앉아 있으면 C를 눌러 일어선다. 책상 밑 같은 데서 나오면 앉은 채다. */
	void StandUp(const FString& Name);
	/** 닫힌 문 앞(Stand, 발 높이)에 서서 걸쇠를 겨누고 E로 연다. 열려 있으면 넘어간다. */
	void OpenDoor(const FString& Name, const FString& DoorName, const FVector& Stand);
	bool HasStoryState(const TCHAR* Tag) const;

	void BuildArrival();
	void BuildNightOne();
	void BuildDayOne();
	void BuildNightTwo();
	void BuildDayTwo();
	void BuildNightThree();

	void PrepareQuery();
	bool ProbeFloor(const FVector2D& At, float AroundZ, float& OutFloor) const;
	bool IsBodyClear(const FVector2D& At, float FloorZ, float ExtraRadius) const;
	uint8 Evaluate(int32 Index, float AroundZ);
	int32 CellOf(const FVector2D& At) const;
	FVector2D CellCenter(int32 Index) const;
	bool PlanPath(const FVector& Goal, TArray<FVector>& OutPoints, FString& OutWhy);
	bool IsStraightClear(int32 From, int32 To);
	/** 길을 못 찾았을 때, 닿은 칸들 가장자리에서 무엇이 몸을 막았는지 센다. */
	FString DescribeBlockers(const TArray<uint8>& Closed, const FVector2D& Goal);
	/** Target을 Aim에서 겨눌 수 있는 자리들(발 높이). 지금 자리에서 가까운 차례로. */
	void FindStandSpots(const AActor* Target, const FVector& Aim, TArray<FVector>& OutSpots);

	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerGreyboxDirector> Director;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	FIGInputRoute Route;
	TArray<FLeg> Legs;
	int32 LegIndex = -1;
	FString CurrentLeg;
	FString CurrentStage;
	/** 지금 일이 막혔을 때 다시 해 볼 일. 없으면 막힌 자리에서 멈춘다. */
	TFunction<void()> Retry;
	TFunction<void()> PendingRetry;
	FString AbortReason;
	int32 Failures = 0;
	int32 Retries = 0;
	int32 ShotIndex = 0;
	bool bStarted = false;
	bool bFinished = false;
	float TotalSeconds = 0.0f;
	float LimitSeconds = 1800.0f;
	float NextPositionLog = 0.0f;
	/** 누르는 동안 초점이 바뀌는지 본다. 홀드는 초점을 잃으면 끊긴다. */
	TWeakObjectPtr<AActor> LastFocus;
	double WallStartSeconds = 0.0;

	/** 몸과 눈. 처음 틱에서 잰다. */
	float BodyRadius = 30.0f;
	float BodyHalfHeight = 96.0f;
	float EyeOffset = 64.0f;
	float ReachDistance = 220.0f;

	FPlanGrid Grid;
	/** 길을 못 찾았을 때 지도와 경계 칸까지 적는다. 기다리며 다시 찾는 동안은 처음과 마지막만 적는다. */
	bool bVerbosePlan = true;
	FCollisionQueryParams BodyQuery;
	FCollisionResponseParams BodyResponse;
	TEnumAsByte<ECollisionChannel> BodyChannel = ECC_Pawn;
};
