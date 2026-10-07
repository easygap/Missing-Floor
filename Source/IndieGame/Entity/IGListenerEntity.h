#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Entity/IGBuildingNav.h"
#include "Entity/IGListenerTuning.h"
#include "Entity/IGNoiseSubsystem.h"
#include "IGListenerEntity.generated.h"

class AIGPlayerCharacter;
class AIGSwingDoor;
class UAudioComponent;
class UCapsuleComponent;
class UIGDustSubsystem;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UAnimSequence;

/** What the one upstairs is doing. See STORY_BIBLE_MISSING_FLOOR.md §4.5. */
UENUM(BlueprintType)
enum class EIGListenerState : uint8
{
	/** Crawling between patrol nodes. */
	Patrolling,
	/** Stationary, knocking three times. The player's masked window. */
	Banging,
	/** Stationary, listening. Hearing doubles. */
	Listening,
	/** Moving to the last heard sound. */
	Investigating,
	/** Arrived where it heard something; holding still and listening. */
	Holding,
	/** Burst pursuit toward the last sound. */
	Chasing,
	/**
	 * 소리를 놓쳤다. 놓친 자리 둘레의 문 앞과 숨을 자리, 그녀가 가던 쪽을 차례로
	 * 들러 귀를 댄다. 한 바퀴를 다 돌아야 순찰로 돌아가고, 돌아간 뒤에도 한동안은
	 * 더 오래, 더 낮게 듣는다.
	 */
	Searching,
	/** Frozen by an answer knock. Hope, while it lasts. */
	Waiting,
	/** Holding the caught player; the director owns the screen. */
	CaptureHold,
	/** Night-four authored pass: follows Mok, never diverts to or catches Yudam. */
	FinaleLured
};

DECLARE_MULTICAST_DELEGATE_OneParam(FIGPlayerCapturedSignature, APawn* /*Player*/);
/**
 * 그가 무엇이든 두드렸다(순찰 3연, 천장, 403호 문, 대답). 두드린 자리를 준다.
 * 이 소리는 소음 버스에 실리지 않으므로, 듣고 반응해야 하는 다른 존재는 이것을 구독한다.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FIGListenerKnockSignature, const FVector& /*KnockLocation*/);

/** 스켈레탈 몸이 재생하는 동작. rig_crawler.py의 액션 넷과 같은 이름이다. */
enum class EIGListenerBodyAnim : uint8
{
	None,
	Crawl,
	Listen,
	Bang,
	Lunge
};

/**
 * 위층 사람 — the one upstairs. Blind; hunts entirely by sound through the
 * IGNoiseSubsystem. It knocks, then listens; it investigates what it hears
 * and bursts into a chase when a sound answers twice. Catching the player is
 * not violence — it is an embrace and a walk toward the wall — and hands
 * control to the night-loop director, which resets the hour.
 *
 * 사진을 대조해 다듬은 스켈레탈 몸이 포복·듣기·노크·덮치기를 재생한다.
 * 접지 동작은 Blender에서 구우며, 거리별 LOD로 스키닝 정점 수를 줄인다.
 */
UCLASS()
class INDIEGAME_API AIGListenerEntity : public APawn
{
	GENERATED_BODY()

public:
	AIGListenerEntity();

	virtual void Tick(float DeltaSeconds) override;
	FVector GetCaptureFaceLocation() const;
	bool HasPhysicalCaptureBody() const { return ListenerSkeletal != nullptr; }
	void KeepCaptureVisible();

	UFUNCTION(BlueprintPure, Category = "Listener")
	EIGListenerState GetListenerState() const { return State; }
	/** 닫힌 403호 문 앞에 와 있다(두드리거나 듣는 중). 다른 괴이가 그 문을 쓰지 않게 본다. */
	bool IsAtHomeDoor() const { return bAtHomeDoor; }

	/** Capture escalation, 0..3. Raised by the director on each loop reset. */
	UFUNCTION(BlueprintPure, Category = "Listener")
	int32 GetAggressionTier() const { return AggressionTier; }

	UFUNCTION(BlueprintCallable, Category = "Listener")
	void SetAggressionTier(int32 Tier);

	/**
	 * Re-resolves §20.2 and §20.4 for the night that is starting. Called on
	 * waking and whenever the tier moves, so a difficulty change mid-run takes
	 * effect at the next night without touching saves (§20.4).
	 */
	void RefreshNightTuning();

	/** The numbers this night is actually running on. */
	const FIGListenerTuning& GetTuning() const { return Tuning; }

	EIGNightDifficulty GetDifficulty() const { return Difficulty; }

	/** 설정에서 바꾼 난이도를 현재 추격에도 바로 적용한다. 저장은 메뉴가 맡는다. */
	void SetDifficulty(EIGNightDifficulty NewDifficulty);

	/** Harness hook: forces a mode for one run without writing it back. */
	void SetDifficultyForTesting(EIGNightDifficulty NewDifficulty);

	/**
	 * World-space patrol stops. The entity crawls node to node, knocking and
	 * listening at each. With no nodes it haunts its spawn point in place.
	 */
	UFUNCTION(BlueprintCallable, Category = "Listener")
	void SetPatrolPoints(const TArray<FVector>& Points);

	/**
	 * The learned answer: two knocks, a rest, one. Freezes an approaching
	 * entity into Waiting — and tells it exactly where the answer came from.
	 * When hope runs out it investigates that spot.
	 */
	UFUNCTION(BlueprintCallable, Category = "Listener")
	void NotifyAnswerKnock(const FVector& KnockLocation);

	/**
	 * 둘-쉬고-하나의 인식기. 플레이어가 아무것도 조준하지 않고 두드릴 때
	 * 불리며, 세 번째 탭이 박자에 맞으면 NotifyAnswerKnock으로 넘긴다.
	 *
	 * §7 P4는 대답하는 법을 가르치고 §8 비트 3-7은 그것으로 복도를 지나가라고
	 * 한다. 그런데 이 게임에는 대답이 존재에게 닿는 경로가 아예 없었다 —
	 * Waiting 상태와 NotifyAnswerKnock은 구현돼 있었지만 부르는 사람이 없어서,
	 * 배운 문법을 P4의 지정된 벽 밖에서는 쓸 수 없었다.
	 *
	 * 대답은 공짜가 아니다. 기다림이 끝나면 그는 **대답이 온 자리**를 조사한다.
	 * 그래서 언제든 두드릴 수 있게 두어도 은신이 무너지지 않는다: 멈추게 하는
	 * 대가로 자기 위치를 준다.
	 *
	 * Returns true when the tap was taken as part of an answer — the caller then
	 * owns the sound and the feedback, and must not fall through to a door.
	 */
	bool TryAnswerKnock(const FVector& KnockLocation);

	/**
	 * 응답 박자의 유일한 정의. P4의 벽과 복도의 맨손 노크가 같은 창을 써야
	 * 하므로, 밤3 디렉터도 이 값을 참조한다.
	 */
	static constexpr double AnswerPairMinSeconds = 0.18;
	static constexpr double AnswerPairMaxSeconds = 0.65;
	static constexpr double AnswerRestMinSeconds = 0.68;
	static constexpr double AnswerRestMaxSeconds = 1.80;
	/** 마지막 탭에서 이만큼 지나면 시도가 처음부터 다시 시작된다. */
	static constexpr double AnswerSequenceResetSeconds = 3.0;

	/**
	 * 박자 맞추기 도움(§19.8)의 창 배율. 켜면 두 간격의 위쪽 끝과 새로
	 * 시작하는 간격이 늘어난다. 밤4 곁을 지키는 박자, P4의 벽, 복도의 대답이
	 * 모두 같은 배율을 쓴다.
	 */
	static double GetAnswerWindowScale(const UObject* WorldContext);
	/** 둘, 쉬고, 하나. 넓힌 창에서도 쉼은 짝보다 길어야 한다. */
	static bool MatchesAnswerCadence(double PairInterval, double RestInterval, double WindowScale);

	/**
	 * Walks him to a spot and lets him hold there, silently, without any sound
	 * having called him.
	 *
	 * §20.3's first safety net is a *witnessed* thing, not a hint: he stops in
	 * front of the wall that matters and puts his ear to it. The player is shown
	 * where to look and told nothing at all. Deliberately not routed through the
	 * noise path, so this can never escalate into a chase — a player who is
	 * already stuck must not be punished for being helped.
	 *
	 * 그는 층을 오르지 못한다. 벽이 위층에 있으면 위층 소리에 늘 가는 자리, 곧
	 * 계단 아래까지 가서 그쪽으로 고개를 들고 듣는다. 들숨도 조사 드론도 없이
	 * 순찰 속도로 가고, 다 들으면 두드리지 않고 가던 길로 돌아간다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Listener")
	void BeginObservationHold(const FVector& Target);

	/**
	 * 지금 관찰 연출을 걸어도 되는가. 무언가를 쫓거나 기다리거나 두드리는 그를
	 * 떼어 내면 도움이 위협을 바꾼다 — 추격이 공짜로 끝나거나 노크가 반만 난다.
	 * 아래층의 벽, 계단 아래를 모르는 위층의 벽도 가 볼 길이 없다.
	 *
	 * Witness는 지켜볼 사람의 자리다. 그의 층에 있어야 보이고, 그가 갈 길에서
	 * 떨어져 있어야 도움이 포획이 되지 않는다.
	 */
	bool CanBeginObservationHold(const FVector& Target, const FVector& Witness) const;

	/**
	 * 저작된 카메오를 위해 그를 한 자리에 세운다 — 위치, 방향, 그리고 **직전
	 * 소리에 대한 반응을 지운다.**
	 *
	 * 마지막 항목이 요점이다. 텔레포트만 하면 그는 여전히 조사 중이고, 다음
	 * 프레임부터 자기가 들은 자리를 향해 기어가 버린다 — 비트 2-1과 3-7은 둘
	 * 다 플레이어가 방금 소리를 낸 직후에 그를 세우므로, 지우지 않으면 카메오가
	 * 시작하자마자 화면 밖으로 걸어 나간다.
	 *
	 * 순찰 지점은 부르는 쪽이 먼저 넘긴다. 공격 티어는 건드리지 않는다: 연출은
	 * 실패가 아니다.
	 */
	void ParkForBeat(const FVector& Where, float Yaw);

	/** Returns the entity to its patrol start after a capture reset. */
	UFUNCTION(BlueprintCallable, Category = "Listener")
	void ResetToPatrolStart(bool bRaiseAggression);

	/**
	 * Day rest (§1: 낮 구간은 안전하다). Dormant, it is hidden, silent,
	 * tick-free and deaf — the daytime building must never knock. Waking
	 * puts it back at its patrol start with its earned impatience intact.
	 */
	UFUNCTION(BlueprintCallable, Category = "Listener")
	void SetDormant(bool bInDormant);

	UFUNCTION(BlueprintPure, Category = "Listener")
	bool IsDormant() const { return bDormant; }

	/**
	 * 다음 순찰 칸의 노크 한 번을 소리 없이 넘긴다. 밤1의 첫 노크는 천장이
	 * 내므로(§8 0-5) 깨는 순간 복도에서 한 번 더 두드리면 안 된다. 멈춰 서서
	 * 기다리는 시간과 그 뒤 청취는 그대로다. 휴면과 순찰 처음으로의 복귀가
	 * 걷어 낸다.
	 */
	void SilenceNextStopKnock() { bSilenceNextStopKnock = true; }

	/**
	 * 연출이 그를 세워 둔다(밤2 문 앞). 붙들린 동안은 순찰 칸에 닿아도 두드리지
	 * 않고, 걷기나 웅크린 걸음 같은 0.3 미만의 소리와 닫힌 집 안의 소리에는
	 * 돌아보지 않는다. 그 밖의 큰 소리는 연출을 깨고 평소처럼 듣는다. 손이 닿는
	 * 거리의 포획은 그대로다. 순찰 처음으로의 복귀, 휴면, 밤4 통과가 풀어 준다.
	 */
	void SetBeatHold(bool bHold);

	/**
	 * 403호 현관문과 그 안쪽(§4.5). 닫힌 문 너머의 소리를 쫓아오면 문을 부수지
	 * 않고 문 앞에서 세 번 두드리고, 기다렸다가, 떠난다. 문이 열려 있으면
	 * 평소처럼 들어온다.
	 */
	void SetHomeDoor(AIGSwingDoor* Door, const FBox& Interior);

	/**
	 * 위층으로 오르는 계단 아래. 그는 층을 오르지 못하므로 위에서 난 소리에는
	 * 쫓아가지 않고 여기까지 와서 위를 향해 두드린다. 정해 두지 않으면 소리
	 * 바로 아래로 간다.
	 */
	void SetStairFoot(const FVector& Location);

	/**
	 * 밤1 계단참 카메오 동안만 켠다. 켜 두면 듣고 쫓기 전(순찰·청취·두드림)에는
	 * 몸이 닿는 것만으로 잡지 않는다. 그 비트는 「소리 없이 곁을 지나면 모른다」를
	 * 가르치는 자리라, 좁은 참에서 스치기만 해도 잡히면 거꾸로 배운다.
	 */
	void SetTouchCaptureSuppressed(bool bSuppressed) { bTouchCaptureSuppressed = bSuppressed; }

	/**
	 * Runs the finale-only blind pass from StartLocation through RoutePoints.
	 * This is presentation locomotion, not a stealth failure: player collision,
	 * capture and ordinary noise retargeting stay disabled until the pawn exits.
	 * SpeedOverride가 0보다 크면 추격 속도 대신 그 속도로 기어간다. 밤4에서
	 * 목한수의 뒷걸음을 따라가려면 추격의 82%는 너무 빨라 그를 앞지른다.
	 */
	void BeginFinalePass(
		const FVector& StartLocation,
		const TArray<FVector>& RoutePoints,
		float SpeedOverride = -1.0f);

	bool IsFinalePassActive() const
	{
		return State == EIGListenerState::FinaleLured && !bDormant;
	}

	/** Fired once per catch; the night-loop director listens. */
	FIGPlayerCapturedSignature OnPlayerCaptured;

	/** 두드릴 때마다 한 번. 관리인 순찰이 듣고 굳는다. */
	FIGListenerKnockSignature OnKnocked;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Crawl speed between patrol nodes, cm/s. Slow enough to walk away from. */
	UPROPERTY(EditAnywhere, Category = "Listener|Movement", meta = (ClampMin = "0.0"))
	float CrawlSpeed = 110.0f;

	UPROPERTY(EditAnywhere, Category = "Listener|Movement", meta = (ClampMin = "0.0"))
	float InvestigateSpeed = 240.0f;

	/** Burst speed. 밤2부터 달리기와 같거나 빠르다. 뛰어서 벗어나는 게 아니라 소리를 끊어야 한다. */
	UPROPERTY(EditAnywhere, Category = "Listener|Movement", meta = (ClampMin = "0.0"))
	float ChaseSpeed = 460.0f;

	/** Touching distance that ends the night, in centimeters. */
	UPROPERTY(EditAnywhere, Category = "Listener|Movement", meta = (ClampMin = "0.0"))
	float CaptureRadius = 110.0f;

	/** Cross-floor sounds read farther away than line distance says. */
	UPROPERTY(EditAnywhere, Category = "Listener|Hearing", meta = (ClampMin = "1.0"))
	float CrossFloorDistancePenalty = 1.4f;

	/** Height difference that counts as another floor, in centimeters. */
	UPROPERTY(EditAnywhere, Category = "Listener|Hearing", meta = (ClampMin = "0.0"))
	float FloorHeightThreshold = 240.0f;

private:
	friend class AIGAudioPresentationProbe;
	// -- state machine ------------------------------------------------------
	void EnterState(EIGListenerState NewState);
	void TickState(float DeltaSeconds);
	void HandleNoise(const FIGNoiseEvent& Event);
	bool CanHear(const FIGNoiseEvent& Event) const;
	/** 대답 노크가 그에게 닿는가. 거리와 험 마스킹을 소음과 같은 귀로 잰다. */
	bool CanHearAnswerFrom(const FVector& KnockLocation) const;
	float HearingMultiplier() const;
	float ListenSecondsForTier() const;
	float WaitSecondsForTier() const;
	/**
	 * 둘-쉬고-하나가 대답으로 통하는가. P4에서 벽이 대답하기 전(T9 전)에는
	 * 그 박자가 아직 대답이 아니다(§4.3 규칙 6). 서사가 없는 시험장에서는 통한다.
	 */
	bool IsAnswerLearned() const;
	/**
	 * 박자를 치는 도중의 탭. 첫 두 탭에 조사와 추격으로 넘어가면 셋째 탭은 칠
	 * 틈도 없다. 멈춰서 박자를 끝까지 듣고, 틀리거나 끊기면 듣기가 끝난 뒤 그
	 * 자리를 보러 온다.
	 */
	void AttendAnswerTap(const FVector& KnockLocation, double Now);
	/** 연출이 건물을 침묵시킨 동안. P4의 8초가 대표다. */
	bool IsAuthoredSilenceActive() const;

	// -- 다른 층의 소리 (§8 3-3, 3-4) ---------------------------------------
	/**
	 * 위에서 난 소리를 듣고 계단 아래(또는 그 바로 아래)에 닿았다. bKeepHolding이면
	 * 이미 거기 엎드려 위를 듣던 중이다. 상태에 다시 들어가지 않고 청취만 처음부터
	 * 다시 잰다.
	 */
	void ArriveBelowUpperSound(bool bKeepHolding = false);
	void PlayCeilingKnock();

	// -- 닫힌 문 (§4.5) ------------------------------------------------------
	bool IsInsideHome(const FVector& Location) const;
	/**
	 * 집 안에서 난 소리인가. 문짝 자체의 소리(닫히는 소리, 안에서 문에 대고 친
	 * 노크)는 문면에서 나므로 낸 사람이 어느 쪽에 있는지로 가린다.
	 */
	bool IsHomeSoundAt(const FVector& Location, const AActor* Maker) const;
	/**
	 * 들은 자리를 적는다. 닫힌 문면에서 난 집 안의 소리는 문 안쪽으로 옮겨 적어,
	 * 문짝으로 달려들지 않고 문 앞에 와서 두드리게 한다.
	 */
	void NoteHeardLocation(const FVector& Location, bool bHomeSound);
	/**
	 * 대답(둘-쉬고-하나)이 온 자리를 들은 자리로 적는다. 소음과 같은 두 규칙이다.
	 * 위층에서 온 대답이면 계단 아래로 가고, 닫힌 문에 대고 안에서 친 대답이면 문
	 * 안쪽으로 옮겨 적는다. 문 안에서 친 것인지는 그 순간 그녀가 어디 있는지로
	 * 가리므로 대답을 들은 때 부른다. 위층이면 true — 조사에 들어간 뒤
	 * bCallFromAbove를 세우는 것은 부르는 쪽이다(EnterState가 지운다).
	 */
	bool NoteAnswerLocation(const FVector& Location);
	/** 쫓던 소리가 닫힌 403호 안에 있고 그는 밖에 있다. */
	bool ShouldGoToHomeDoor() const;
	/** 문짝 가운데 바닥점과 복도 쪽 방향. 문이 없으면 false. */
	bool GetHomeDoorFrame(FVector& OutDoorCenter, FVector& OutOutward) const;
	/** 밤2의 대본 노크가 첫 문 노크여야 한다. 그 전에는 문 앞에서 듣기만 한다. */
	bool CanKnockHomeDoor() const;
	void ArriveAtHomeDoor();
	void LeaveHomeDoor();
	void PlayHomeDoorKnock(bool bSingle);
	/** 철문 녹음 한 타. 3연은 두드리는 동안 0.62초마다 한 타씩 친다. */
	void PlayHomeDoorSteelHit();

	// -- locomotion ---------------------------------------------------------
	/** Sweeps toward Target; returns true on arrival (or when wedged). */
	bool CrawlTowards(const FVector& Target, float Speed, float DeltaSeconds);
	/**
	 * 어디든 간다. 같은 층에서 곧게 닿으면 기존 걸음(CrawlTowards)이고, 벽이나
	 * 층이 가로막으면 건물 길(BuildingNav)을 따라 계단탑으로 돌아간다. 계단에서는
	 * 디딤판 윗면을 따라 몸을 기울여 오르내린다. 닿으면 true.
	 */
	bool MoveTowardGoal(const FVector& Goal, float Speed, float DeltaSeconds);
	/** Goal까지 건물 길을 다시 짠다. 길이 없으면 false. */
	bool PlanNavPath(const FVector& Goal);
	/** 계단 띠와 반 층 참 위의 한 걸음. 쓸지 않고 디딤판 위 점을 잇는 선을 따른다. */
	bool StepAlongStair(const FVector& TargetFeet, float Speed, float DeltaSeconds);
	/** 같은 층에서 몸이 벽에 걸리지 않고 곧게 닿는가. */
	bool CanCrawlStraightTo(const FVector& Goal) const;
	/** 두 바닥점 사이를 몸과 같은 캡슐로 쓸어 본다. 막혔으면 막은 액터를 준다. */
	bool IsCrawlLineClear(const FVector& FromFeet, const FVector& ToFeet, const AActor** OutBlocker = nullptr) const;
	/** 캡슐 아래 바닥 높이. 액터 높이에서 캡슐 반 높이와 띄운 2 cm를 뺀다. */
	FVector GetFeetLocation() const;
	/** 계단 위에서는 몸이 경사를 따라 눕는다. 평지에 내려서면 천천히 편다. */
	void UpdateStairPitch(float DeltaSeconds);
	/**
	 * 4층 계단실 방화문(§4). 닫혀 있으면 그 문을 지나는 길은 막힌다. 문 앞까지 와서
	 * 두드리고 듣는다 — 403호 현관문과 같은 문법이다.
	 */
	bool LegCrossesClosedFireDoor(const FVector& FromFeet, const FVector& ToFeet);
	/**
	 * 닫힌 문(방화문, 관리실 문)에 막혔다. 그는 문을 열지도 부수지도 않는다. 문에 대고
	 * 세 번 두드리고, 귀를 대고 듣는다. 12초 안에 다시 막히면 듣기만 한다.
	 */
	void ArriveAtClosedDoor();
	/** 몸 밑 한 뼘 안에 바닥이 있는가. 계단 가장자리를 넘지 않게 걸음마다 본다. */
	bool HasFloorBeneath(const FVector& Location) const;
	/**
	 * 순찰·수색 걸음의 빠르기 배율. 한 걸음마다 다시 고르고 가끔 0으로 멈춰 듣는다.
	 * 같은 주기가 매끈하게 돌면 몇 번 보고 나서 익숙해진다. 추격에는 쓰지 않는다.
	 */
	float AdvanceGait(float DeltaSeconds);
	void FaceDirection(const FVector& Direction, float DeltaSeconds, float TurnDegreesPerSecond = 160.0f);
	const FVector* CurrentPatrolTarget() const;

	/**
	 * §5.6: picks the next patrol stop. With no heatmap weight it is the plain
	 * round of the route; as the weight rises the stops the player has been
	 * loud near start winning. Pure statistics, so the same play produces the
	 * same route.
	 */
	void AdvancePatrolIndex();

	/**
	 * §5.6 tier-3 ambush: goes to the hottest zone and waits there without
	 * knocking. The knock cycle disappearing is the tell — late in the game a
	 * quiet building is the dangerous one.
	 */
	bool TryBeginAmbush();

	// -- 놓친 뒤 ---------------------------------------------------------------
	/** 그녀가 낸 소리의 자리와 시각을 몇 개 기억한다. 어느 쪽으로 갔는지 읽는다. */
	void NotePlayerTrail(const FVector& Location, double Seconds);
	/**
	 * 마지막으로 들은 두 소리를 이어 그녀가 가던 쪽 몇 미터 앞의 건물 길 점을 고른다.
	 * 계단 쪽으로 가던 소리면 계단으로 이어진다. 읽을 만한 흔적이 없으면 false.
	 */
	bool PredictPlayerHeading(FVector& OutPoint) const;
	/** 수색에 들를 자리를 고른다. 놓친 자리에서 가까운 문 앞, 방, 숨을 자리 순이다. */
	void BuildSearchPlan();
	/** 한 번 놓쳤을 때 찾아다니는 시간. 밤이 갈수록, 화가 날수록 길다. */
	float SearchSecondsForNight() const;
	/** 수색을 마치고 돌아간 뒤 한동안은 더 천천히 기고 더 오래 듣는다. */
	bool IsAlert() const;

	// -- presentation -------------------------------------------------------
	void BuildGreyboxBody();
	/**
	 * 리깅된 몸. Scripts/blender/rig_crawler.py가 만든 SK_ListenerCrawler와
	 * 동작 넷(Crawl·Listen·Bang·Lunge)을 싣는다. 있으면 정적 셸과 스프라이트
	 * 카드는 만들지 않는다 — 카드는 정면에서만 사람이었고 옆에서는 판이었다.
	 */
	bool BuildSkeletalBody();
	void PlayBodyAnim(EIGListenerBodyAnim Anim, bool bLoop, float Rate);
	void UpdateSkeletalPose(float SpeedAlpha, float BodyRate, float DeltaSeconds);
	/** 기는 주기 재생 배율. 순찰 속도에서 1.0, 추격에서 상한. */
	float ComputeCrawlRate(float Speed) const;
	void UpdatePresentationLayer();
	void UpdatePresentationPose(float CurrentSpeed, float DeltaSeconds);
	void PlayKnockTriple();
	/**
	 * 노크 한 사이클의 빠르기와 세기를 정한다. 티어가 오를수록 빨라지고, 사이클마다
	 * 조금씩 흔들린다. 빠르기는 KnockRate에 남아 두드리는 동작도 같은 배율로 돈다.
	 * 돌려주는 값은 볼륨이다.
	 */
	float RollKnockTempo();
	void PlayPlasterSettle(float Volume = 0.6f, float Pitch = 1.0f);
	/**
	 * 자리를 잡으며 미장이 갈라진다. 듣는 자리에 엎드릴 때와 계단 아래에서 위의
	 * 소리에 몸을 고쳐 앉을 때 난다. 몇 초에 한 번뿐이다 — 위층 걸음마다 나면
	 * 0.4초마다 갈라진다.
	 */
	void PlayPlantSettle(float Volume);
	/**
	 * 조사 들숨. 조용히 다가오던 걸음(매복, 관찰)이 들킨 순간에도 이 숨이 처음으로
	 * 난다.
	 */
	void PlayAlertVocal();
	/**
	 * §19.8 대체 채널. 그가 낸 사건성 소리를 노크 진동·노크 파문에 알린다. 소음
	 * 버스와 따로 가므로 그 자신과 녹음, 히트맵은 이 신호를 모른다. 세기는 그녀
	 * 자리에 닿는 만큼으로 줄여 보낸다 — 먼 노크와 문 밖 노크가 같은 세기로 오면
	 * 대체 채널이 거리를 지운다. 보냈으면 true.
	 */
	bool EmitPresentationCue(const FVector& At, float Loudness, float AudibleRange);
	/** 방위 자막은 그녀 가까이에서 난 소리에만 붙인다. 자막은 기본값이 켜짐이다. */
	bool IsNearForCaption(const FVector& At) const;
	void UpdateDragLoop(float CurrentSpeed);

	/**
	 * §10.3 끌림 2종: swaps the crawl bed when he moves between tile and 장판.
	 * Traced from the floor beneath him with the same authored surface tags the
	 * §21.2 footstep matrix uses, so the two systems can never disagree about
	 * what he is dragging himself across.
	 */
	void RefreshDragSurface();
	void UpdateThreatPressure();

	/**
	 * Leaves the plaster dust his drag raises in the air (§11 V1). It is a
	 * report, not a render: the torch decides whether anyone ever sees it, and
	 * he does not know he is leaving a trail any more than he knows he is loud.
	 */
	void ReportDustTrail();

	void BeginCapture(APawn* Player);

	UPROPERTY(VisibleAnywhere, Category = "Listener|Components")
	TObjectPtr<UCapsuleComponent> Body;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> BodyBlocks;

	/** Continuous close/side shell and authored long-corridor front layer. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ListenerShell;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ListenerFrontCard;

	/** 리깅된 몸. 이것이 있으면 위 둘은 null이다. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ListenerSkeletal;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CrawlAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ListenAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> BangAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LungeAnim;

	EIGListenerBodyAnim ActiveBodyAnim = EIGListenerBodyAnim::None;
	FVector CaptureViewTarget = FVector::ZeroVector;
	/** 잡기 시작한 순간의 얼굴 위치. 여기서 CaptureViewTarget까지 달려든다. */
	FVector CaptureFaceStart = FVector::ZeroVector;
	/** 넘어진 눈의 위치와, 거기서 얼굴까지 이어지는 방향. 얼굴은 이 선 위에서 물러났다가 달려든다. */
	FVector CaptureFallenEye = FVector::ZeroVector;
	FVector CaptureStrikeLine = FVector::ForwardVector;
	TWeakObjectPtr<class AIGPlayerCharacter> CaptureVictim;
	/**
	 * 바닥에 떨어진 손전등이 얼굴을 아래에서 비추는 빛. 손전등이 꺼져 있었다면
	 * 더 어둡게 두되, 얼굴의 윤곽은 읽혀야 한다.
	 */
	UPROPERTY(Transient)
	TObjectPtr<class UPointLightComponent> CaptureKeyLight;
	FVector CaptureKeyLightLocation = FVector::ZeroVector;
	void SetCaptureKeyLight(bool bEnabled);
	float TexturePrefetchSeconds = 0.f;
	/** 추격 중 두 팔 거리 안에 들어오면 덮치는 동작으로 바꾼다. */
	bool bLungeArmed = false;

	/**
	 * 셸 석고 재질의 인스턴스. 숨과 잔떨림은 재질 WPO가 만들고, 상태 머신은
	 * 여기로 진폭만 넘긴다. 카드 4단계는 저작된 정지 프레임이라 그대로 둔다.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ListenerShellMid;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> ListenerPhaseMaterials;

	bool bFrontCardActive = false;
	int32 ListenerPhaseIndex = INDEX_NONE;
	float ListenerPhase = 0.0f;
	float PresentationSpeed = 0.0f;
	/** 재질 기본값과 같은 순찰 기준치에서 시작해 상태에 따라 보간된다. */
	float ShellBreathAmplitude = 0.45f;
	float ShellTremorAmplitude = 0.1f;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> DragLoopComponent;

	UPROPERTY(Transient)
	TObjectPtr<UIGNoiseSubsystem> NoiseSubsystem;

	UPROPERTY(Transient)
	TObjectPtr<UIGDustSubsystem> DustSubsystem;

	FDelegateHandle NoiseHandle;

	UPROPERTY(EditAnywhere, Category = "Listener|Patrol")
	TArray<FVector> PatrolPoints;

	EIGListenerState State = EIGListenerState::Patrolling;
	FIGListenerTuning Tuning;
	EIGNightDifficulty Difficulty = EIGNightDifficulty::Standard;
	/** Set when the ambush node has been chosen for this tier-3 stretch. */
	FVector AmbushLocation = FVector::ZeroVector;
	bool bAmbushArmed = false;
	/**
	 * 들은 소리 없이 가는 걸음(§5.6 매복, §20.3 관찰). 들숨도 조사 드론도 없이 순찰
	 * 속도로 기어가 엎드린다. 가는 길과 엎드린 자리에서만 살고, 무언가를 들으면
	 * 곧바로 풀린다.
	 */
	bool bSilentApproach = false;
	/** 그중 자비의 관찰. 다 들으면 두드리지 않고 순찰로 돌아간다. */
	bool bObservationHold = false;
	bool bDormant = false;
	/** SilenceNextStopKnock이 세운다. 다음 Banging 한 번이 소리 없이 지나간다. */
	bool bSilenceNextStopKnock = false;
	int32 AggressionTier = 0;
	int32 PatrolIndex = 0;
	FVector SpawnLocation = FVector::ZeroVector;
	FVector LastHeardLocation = FVector::ZeroVector;
	double LastHeardTime = -1000.0;
	/** Hearing something while already reacting to a sound means a chase. */
	bool bReactingToSound = false;
	FVector SearchAnchor = FVector::ZeroVector;
	FVector SearchTarget = FVector::ZeroVector;
	FVector AnswerKnockLocation = FVector::ZeroVector;

	/** The player's in-progress answer. Never more than the last three taps. */
	TArray<double> AnswerTapTimes;
	/**
	 * 이 밤에 대답이 통한 횟수. 두 번째부터 기다림이 짧아지고 네 번째부터는
	 * 대답이 오지 않는다. 같은 박자를 6초마다 두드리면 밤새 안 잡히던 구멍을
	 * 막는다. 포획 리셋에는 남고 밤이 바뀔 때 0이 된다.
	 */
	int32 AnswersThisNight = 0;
	TArray<FVector> FinaleRoutePoints;
	int32 FinaleRouteIndex = 0;
	/** BeginFinalePass가 넘긴 속도. 0 이하이면 추격 속도의 82%로 간다. */
	float FinaleSpeedOverride = -1.0f;
	/** SetBeatHold가 세운다. 연출이 끝나거나 큰 소리가 나면 풀린다. */
	bool bBeatHold = false;

	// -- 멈춰 선 자리의 맥락. 두드림·청취·제자리 청취 동안만 살아 있고, 다시
	// 움직이기 시작하면 EnterState가 지운다 --------------------------------
	/** 그 자리에서 고개를 돌릴 방향. 0이면 돌지 않는다. */
	FVector AttentionDirection = FVector::ZeroVector;
	/** 위층 소리 아래에서 천장을 향해 두드리는 중. */
	bool bKnockingUp = false;
	/** P4 전의 둘-쉬고-하나에 귀를 세웠다. 청취가 끝나면 그 자리를 보러 간다. */
	bool bCadenceEarsUp = false;
	/** 닫힌 403호 문 앞에 서 있다. */
	bool bAtHomeDoor = false;
	/** 문 앞에서 안쪽 소리에 한 번 더 두드렸다. 한 번뿐이다. */
	bool bDoorReknocked = false;
	/** 한 번 더 두드리기까지 남은 시간. 0 이하이면 예약이 없다. */
	float DoorReknockCountdown = -1.0f;
	/** 이번 문 노크에서 친 철문 타수. 3이면 남은 타가 없다. */
	int32 DoorSteelHitsPlayed = 3;
	/** 문 노크가 나는 자리. 두드리는 동안 그는 움직이지 않는다. */
	FVector DoorKnockPoint = FVector::ZeroVector;

	// -- 위층 소리. 조사 하나 동안만 산다 ---------------------------------------
	bool bHasStairFoot = false;
	FVector StairFoot = FVector::ZeroVector;
	bool bTouchCaptureSuppressed = false;
	/** 지금 조사가 위에서 난 소리 때문이다. */
	bool bCallFromAbove = false;
	/** 위에서 두 번 들렸다. 도착하면 두드린다. */
	bool bKnockUpOnArrival = false;
	FVector UpperSoundLocation = FVector::ZeroVector;
	/** 천장 노크는 12초에 한 번. 밤4 망치질마다 3연이 나면 안 된다. */
	double LastCeilingKnockSeconds = -1000.0;
	/** 자리를 잡으며 갈라진 미장의 마지막 시각(PlayPlantSettle). */
	double LastPlantSettleSeconds = -1000.0;

	/** 귀를 세운 탭의 시각과 자리. 같은 탭의 소음이 곧바로 뒤따른다. */
	double CadenceTapSeconds = -1000.0;
	FVector CadenceTapLocation = FVector::ZeroVector;

	// -- 403호 현관문 ---------------------------------------------------------
	TWeakObjectPtr<AIGSwingDoor> HomeDoor;
	FBox HomeInterior = FBox(ForceInit);
	/** 이번 걸음에 문짝에 부딪혔다. */
	bool bBlockedByHomeDoor = false;
	/** 문 앞을 떠난 뒤 집 안 소리를 흘려듣는 시한(게임 시간). */
	double HomeDoorIgnoreUntil = -1000.0;

	/** 포획 때 덮침 녹음의 숨 꼬리를 자르는 타이머. 노크 둘이 그 숨에 묻혔다. */
	FTimerHandle CaptureGrabTrimTimer;

	float StateSeconds = 0.0f;
	float SearchRetargetSeconds = 0.0f;
	float StuckSeconds = 0.0f;
	float LastMoveSpeed = 0.0f;
	float GaitCurrent = 1.0f;
	float GaitTarget = 1.0f;
	float GaitSecondsLeft = 0.0f;
	uint32 GaitStepCount = 0;
	FVector LastDustReportLocation = FVector::ZeroVector;
	float DustSiftCentimeters = 0.0f;
	float DragSurfacePollSeconds = 0.0f;
	bool bDustTrailSeeded = false;
	/** True while the crawl bed is the 장판 variant rather than tile. */
	bool bDragSurfaceIsVinyl = false;
	/** 계단 철판 위. 손바닥과 무릎이 디딜 때마다 철판이 운다. */
	bool bDragSurfaceIsMetalStair = false;

	// -- 층을 오가는 길 -------------------------------------------------------
	FIGBuildingNav BuildingNav;
	/** 지금 따라가는 길. 끝 점은 목표 자리 그 자체다. */
	TArray<FVector> NavPath;
	/** NavPath의 점 중 계단 위(디딤판을 따라야 하는) 점. */
	TArray<bool> NavPathOnStair;
	int32 NavPathIndex = 0;
	FVector NavGoal = FVector::ZeroVector;
	double NavPlannedSeconds = -1000.0;
	/** 이번 걸음이 계단 위였다. 몸을 경사대로 눕힌다. */
	bool bOnStairLeg = false;
	float StairPitchTarget = 0.0f;
	float StairBodyPitch = 0.0f;

	// -- 놓친 뒤 ---------------------------------------------------------------
	struct FHeardMark
	{
		FVector Location = FVector::ZeroVector;
		double Seconds = -1000.0;
	};
	/** 그녀가 낸 소리. 마지막 넷만. */
	TArray<FHeardMark> PlayerTrail;
	/** 소리가 끊긴 뒤 그녀가 가던 쪽으로 몇 미터 더 따라가 보는 자리. */
	FVector ChaseMomentumTarget = FVector::ZeroVector;
	bool bHasMomentumTarget = false;
	bool bMomentumTried = false;
	TArray<FVector> SearchSpots;
	/** 들러서 귀를 댈 방향. 숨을 자리면 그 가구를 향한다. */
	TArray<FVector> SearchFacings;
	TArray<bool> SearchSpotIsHiding;
	int32 SearchSpotIndex = 0;
	float SearchPauseLeft = 0.0f;
	bool bSearchPausing = false;
	float SearchBudgetSeconds = 0.0f;
	/** 수색을 마치고 돌아가는 동안의 경계. 이 시각까지 더 천천히 기고 더 오래 듣는다. */
	double AlertUntilSeconds = -1000.0;
	/** 4층 계단실 방화문. 처음 길을 짤 때 찾아 둔다. */
	TWeakObjectPtr<AIGSwingDoor> StairFireDoor;
	bool bStairFireDoorResolved = false;
	/** 이번 걸음이 닫힌 문(403호 현관문 말고)에 막혔다. */
	bool bBlockedByClosedDoor = false;
	/** 막은 문. 두드릴 방향을 잰다. */
	TWeakObjectPtr<AActor> BlockingDoor;
	/** 닫힌 문 노크는 12초에 한 번. 그 사이에는 문 앞에서 듣기만 한다. */
	double LastClosedDoorKnockSeconds = -1000.0;

	/** 그의 숨. 상태와 거리로 볼륨이 정해진다. 자는 동안은 0. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BreathLoopComponent;
	float BreathVolumeTarget = 0.0f;
	/** 기는 걸음 소리의 마지막 박자 칸. 네 자세 한 바퀴에 두 걸음. */
	int32 LastCrawlStepIndex = -1;
	/** 코앞에서 마주친 스팅어의 마지막 시각. 25초에 한 번. */
	double LastCloseCallSeconds = -1000.0;
	/** 대답 뒤의 기다림이 끝나 간다. 팔꿈치를 고쳐 짚고 숨이 돌아왔다. */
	bool bWaitStirred = false;
	/** 노크 사이클 번호. 사이클마다 빠르기와 세기를 조금씩 흔드는 씨앗이다. */
	int32 KnockSerial = 0;
	/** 이번 노크의 재생 배율. 두드리는 동작이 같은 배율로 돌아야 손과 소리가 맞는다. */
	float KnockRate = 1.0f;
	/** 순찰 노크 자막의 마지막 시각. 같은 문장은 12초에 한 번. */
	double LastKnockCaptionSeconds = -1000.0;
	/** 다가오는 걸음을 대체 채널에 보낸 마지막 시각. */
	double LastApproachCueSeconds = -1000.0;
	/** 숨어 있는 그녀의 소리를 코앞에서 들은 마지막 시각(§4). 이때만 숨은 자리를 연다. */
	double LastHeardHiddenPlayerSeconds = -1000.0;
	void UpdateBreathLoop(float Distance);
	void TryCloseCallStinger(const AIGPlayerCharacter* Player, float Distance);

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> CachedPlayer;
};
