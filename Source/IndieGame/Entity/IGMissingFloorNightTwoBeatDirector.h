#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGMissingFloorNightTwoBeatDirector.generated.h"

class AIGListenerEntity;
class AIGMissingFloorEvidence;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class UAudioComponent;
class UIGMissingFloorNarrativeSubsystem;
class UIGNoiseSubsystem;
class UIGRecordingSubsystem;

DECLARE_MULTICAST_DELEGATE(FIGNightTwoReturnedSignature);

/** Where beat 2-5 has got to. Independent of 2-1; they only share the night. */
UENUM()
enum class EIGNightTwoReturnStage : uint8
{
	/** T7 is not confirmed yet, or this is not night two. */
	Idle,
	/**
	 * 밑장은 이미 읽었는데 둘째 밤의 목표가 남았다. 첫째 밤에 관리실을 먼저
	 * 풀었거나, 풀고 나서 05:30을 넘긴 되풀이 밤이다. 관리실에 다시 들어서면
	 * 귀환이 걸린다.
	 */
	AwaitingBooth,
	/** T7 is in. The collapse is waiting for her to step out of the booth. */
	AwaitingExit,
	/** 낙하물 dropped, the building heard it twice, and she has to get home. */
	Chased,
	/** Back inside 403. The night's goal is done — this is what ends it. */
	Home
};

/** Where beat 2-1 has got to. */
UENUM()
enum class EIGNightTwoBeatStage : uint8
{
	/** Not night two, or the beat is already spent. */
	Idle,
	/** 04:30 has passed and the first knock is on its way. */
	Opening,
	/** One knock has landed. She has a peephole and a reason to use it. */
	AwaitingPeephole,
	/** She has seen him. The phone on the floor is now the pointed verb. */
	AwaitingPhone,
	/** 노크 3연, 기다림, 끌려가는 소리 — and the tape is running. */
	Answering,
	/** Played. Never again this run. */
	Spent
};

/**
 * 밤2 비트 2-1 「문 하나를 사이에 둔 첫 대면」
 * (STORY_BIBLE_MISSING_FLOOR.md §8 밤2, 【S】0.7).
 *
 * 04:30. 이번엔 노크가 403호 현관문에서 난다. 문구멍 — 복도에 그가 있다.
 * 유담이 폰 녹음을 켜고 문에 대어 둔다. 문 너머 노크 3연, 기다림,
 * 끌려가는 소리.
 *
 * Night one taught the rule from a distance: a thing in the stairwell, a
 * corridor away. This beat removes the distance and leaves one steel door, and
 * it is the first time the player is looked for rather than merely near.
 *
 * It is also §5.5's trigger. The recording rule needs something on the tape
 * worth refusing, and the design's original arming moment is this: she hears
 * him at her own door and decides to make evidence. Before this director the
 * phone was a prop the player had to think of unprompted, so the morning
 * playback could easily contain nothing but her own footsteps — the shape of
 * the beat without its content.
 *
 * Every step is patient rather than gated. A player who never looks through the
 * peephole, or never sets the phone down, still gets the three knocks and the
 * dragging; they simply do not get them on tape. §20.3's rule that the world
 * must keep moving applies to beats as well as to puzzles.
 *
 * The figure outside the door is the real entity, teleported and given a
 * two-point route, exactly as 1-4 stages the stairwell sighting. Opening the
 * door during the beat therefore shows him standing there, because he is.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGMissingFloorNightTwoBeatDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGMissingFloorNightTwoBeatDirector();

	/** Spawns the peephole against an already-built 403 and takes the route. */
	bool Configure(
		AIGPrologueWorldScene* InScene,
		AIGListenerEntity* InEntity,
		AIGPlayerCharacter* InPlayer,
		const TArray<FVector>& InCorridorPatrolPoints);

	/** The hour boundary. Night two arms the beat; anything else disarms it. */
	void SetHourActive(bool bHourActive);

	/**
	 * 비트 2-5 「귀환 추격」을 무장한다 — T7이 확정된 순간에 불린다.
	 *
	 * §8's night two does not end where the paper contradicts itself. It ends at
	 * 403's door, and the way home is the night's loudest minute: leaving the
	 * booth drops a stack of stored material, the building hears it twice, and
	 * two sounds are a someone — so the real AI commits to CHASE with no bespoke
	 * chase code, exactly as 1-5 earns INVESTIGATE. 【S】0.95, and §10's chase cue
	 * gets its first use of the game.
	 *
	 * Before this existed the night was completed the instant T7 confirmed and the
	 * player was released straight to dawn from inside the management booth, which
	 * skipped the whole beat.
	 */
	/**
	 * bWithConclusion이면 장부를 맞춘 결론과 「403호까지」 두 줄을 먼저 띄운다. 관리실 안쪽 방
	 * 장면(짐 줄기 감독)이 결론을 먼저 띄운 뒤 나서는 순간에 부를 때는 false다.
	 */
	void ArmReturnChase(bool bWithConclusion = true);
	/** 관리인이 잡아 집 문 앞까지 데려다 놓았다. 그 밤은 집에 돌아온 것으로 끝난다. */
	void CompleteReturnAfterEscort();

	/**
	 * 잡히면 403호 현관에서 깨어날 수도 있다(손님, 관리인). 그것이 「집에 도착」으로
	 * 세어지면 잡히는 것이 목표 달성이 되어 버리므로, 잡힌 뒤에는 한 번 밖으로
	 * 나갔다 와야 다시 집으로 인정한다.
	 */
	void NotifyCaptureReset();

	/** Fired once, when she is back inside 403. This is what ends night two. */
	FIGNightTwoReturnedSignature OnReturnedHome;

	// -- receipts for the probe and the contracts ---------------------------
	EIGNightTwoBeatStage GetStage() const { return Stage; }
	bool HasPlayed() const { return bPlayed; }
	/** True while the figure is standing in the corridor outside 403. */
	bool IsFigureAtDoor() const { return bFigureStaged; }
	AIGMissingFloorEvidence* GetPeephole() const { return Peephole; }
	int32 GetKnockCount() const { return KnockCount; }
	/** True when the phone was running when the three knocks landed. */
	bool WasRecordingDuringAnswer() const { return bRecordedAnswer; }

	EIGNightTwoReturnStage GetReturnStage() const { return ReturnStage; }
	/** True once the collapse has dropped and the building has heard it twice. */
	bool HasReturnChaseFired() const { return bReturnChaseFired; }
	/** True while a capture reset is still owed a trip out of 403 and back. */
	bool IsReturnOwedAnotherTrip() const { return bMustLeaveHomeAgain; }

	/** Harness hook: runs the beat without waiting out its patience timers. */
	void AdvanceForTesting();

	/** 【S】 for each beat, straight from the §8 table. */
	static constexpr float ScareAmount = 0.7f;
	static constexpr float ChaseScareAmount = 0.95f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void AdvanceStage();
	void EnterStage(EIGNightTwoBeatStage NextStage);
	void AdvanceReturn();
	void PlayMaterialCollapse();
	bool IsPlayerInsideUnit403() const;
	bool IsPlayerOutsideBooth() const;
	bool IsPlayerInsideBooth() const;
	/**
	 * 되풀이 밤의 귀환 재무장. P2 신호는 세션에 한 번만 나가서, 그 신호를 이미
	 * 써 버린 밤2는 끝낼 길이 없었다. 밤3의 RearmReturnPassForRepeatedNight와 같은
	 * 자리에서 저장된 사실로 다시 건다.
	 */
	void RearmReturnIfOwed();
	void PlayFirstKnock();
	void PlayAnswer();
	/** 3연의 철문 녹음 한 타. 0.62초 간격으로 스스로 다음 타를 건다. */
	void PlayAnswerSteelHit();
	/** 3연 첫 타 뒤에 들이켜는 숨. 녹음 중이면 공백 한가운데 이것만 남는다. */
	void PlayAnswerGasp();
	void PlayDragAway();
	/** 끌려가는 소리를 계단코어 쪽으로 옮긴다. */
	void AdvanceDragAway();
	void StageFigure();
	void ReleaseFigure();
	void HandlePeepholeExamined(AIGMissingFloorEvidence* Evidence);
	/**
	 * 문구멍 뒤 1.4초. 복도는 캄캄한데 그는 선 사람의 눈높이에 없다 — 문 바로
	 * 아래에서 석고가 갈라진다. 그가 실제로 거기 있을 때만 난다. 그다음에
	 * 폰을 가리키는 말이 온다.
	 */
	void PlayPeepholeAftermath();
	/** 2-4의 한기. T7 뒤 관리실 위층 바닥이 한 번 운다 — 곧 무너질 자리의 예고. */
	void PlayReturnChill();

	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;
	UIGNoiseSubsystem* GetNoise() const;
	UIGRecordingSubsystem* GetRecording() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPrologueWorldScene> Scene;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGListenerEntity> Entity;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPlayerCharacter> Player;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> Peephole;

	TArray<FVector> CorridorPatrolPoints;

	FTimerHandle StageTimer;
	FTimerHandle ReturnTimer;
	FTimerHandle CollapseTimer;
	/** 끌려가는 소리를 복도 끝에서 멎게 하는 시계. 루프 파형이라 잘라 줘야 한다. */
	FTimerHandle DragFadeTimer;
	FTimerHandle DragMoveTimer;
	FTimerHandle AnswerKnockTimer;
	FTimerHandle AnswerGaspTimer;
	FTimerHandle PeepholeTimer;
	/** 두 번째 충돌 뒤 판재에서 가루가 떨어지는 시각. */
	FTimerHandle CollapseDustTimer;
	FTimerHandle ReturnChillTimer;
	TWeakObjectPtr<UAudioComponent> DragAwayLoop;
	float DragElapsedSeconds = 0.0f;
	int32 AnswerSteelHits = 0;
	/** 폰을 가리키는 말은 한 번만. 문구멍을 안 본 사람에게는 이것이 유일한 줄이다. */
	bool bPhoneNudged = false;
	EIGNightTwoBeatStage Stage = EIGNightTwoBeatStage::Idle;
	EIGNightTwoReturnStage ReturnStage = EIGNightTwoReturnStage::Idle;
	float StageSeconds = 0.0f;
	int32 KnockCount = 0;
	bool bPlayed = false;
	bool bFigureStaged = false;
	bool bPeepholeSeen = false;
	bool bRecordedAnswer = false;
	bool bReturnChaseFired = false;
	bool bMustLeaveHomeAgain = false;
};
