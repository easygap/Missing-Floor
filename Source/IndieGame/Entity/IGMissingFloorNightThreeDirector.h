#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Narrative/IGMissingFloorNarrativeTypes.h"
#include "IGMissingFloorNightThreeDirector.generated.h"

class AIGDoorLatch;
class AIGListenerEntity;
class AIGMissingFloorEvidence;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGReadableNote;
class AIGSwingDoor;
class UAudioComponent;
class UStaticMeshComponent;
class UIGMissingFloorNarrativeSubsystem;

DECLARE_MULTICAST_DELEGATE(FIGNightThreeSolvedSignature);
DECLARE_MULTICAST_DELEGATE(FIGNightThreeReturnedSignature);

/** Where beat 3-7 has got to. */
UENUM()
enum class EIGNightThreeReturnStage : uint8
{
	/** T9 is not in yet, or this is not night three. */
	Idle,
	/** He is in the corridor between the stair core and her door. */
	Passing,
	/** Back inside 403. The night's goal is done. */
	Home
};

/** 낮의 서일영 한 컷이 어디까지 왔는가. 반사 정보가 필요 없는 내부 상태다. */
enum class EIGDistantSeoStage : uint8
{
	/** 낮이 아니거나, 이미 한 번 보였다. */
	Idle,
	/** 편의점에 들어서기를 기다린다. */
	Armed,
	/** 골목 건너 샛길 입구에 서 있다. 아직 그녀가 알아보지 못했다. */
	Shown,
	/** 그녀가 한 번 똑바로 봤다. 눈을 돌리면 없다. */
	Seen,
	/** 다가오는 그녀 앞에서 샛길 안쪽으로 비켜선다. */
	Retreating
};

/**
 * 밤3 「조율」 — the night the player climbs to the floor that is not there
 * (STORY_BIBLE_MISSING_FLOOR.md §7 P3/P4, §8 밤3).
 *
	 * The release route is physical: visible 4F stair -> roof door -> 6.4 m
	 * tank-side passage -> annex door, with no portal actor. The one upstairs
	 * never follows — he cannot enter the room he was walled into, which is why
	 * the fifth floor is the quietest place.
 *
 * P3 is telling three identical walls apart by sound. The tuner's notebook
 * gives the criterion (a wall with resonance is a wall with a cavity); the
 * riser valve gives the patient instrument (water moving behind one bay);
 * a fist gives the reckless one. Either way the middle bay is the answer,
 * and with the criterion it confirms the location recorded by T6.
 *
 * P4 is the answer. With T6 known and the family rhythm learned, the wall
 * accepts 둘-쉬고-하나 — and after eight seconds of nothing, it comes back
 * through the studs. That is T9, and the night's goal.
 *
 * The director also owns the daytime truth papers (shipping labels, the
 * forum printout, Hwang Sun-geum's tally journal) because they feed the
 * same truth board this night completes.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGMissingFloorNightThreeDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGMissingFloorNightThreeDirector();

	/**
	 * 벽 안에서 돌아오는 둘, 쉬고, 하나. 밤3의 대답(T9)과 밤4 망치질 사이의 대답이 같은
	 * 손이라 한곳에서 낸다. 셋째 노크는 Owner의 타이머로 늦게 오고, OutTimers(둘)가 있으면
	 * 그 핸들을 쥐여 준다. 녹음 샘플이 없으면 합성 패턴 하나로 대신한다.
	 */
	static void PlayWallAnswerKnocks(AActor* Owner, FTimerHandle* OutTimers = nullptr);
	/** 그 소리가 나는 자리. 벽 가운데 칸, 스터드 너머다. */
	static FVector GetWallAnswerLocation();

	/**
	 * Spawns the gate, the annex contents and the day papers.
	 *
	 * Also takes the pursuer, the pawn and the corridor route, because §8 비트
	 * 3-7 stages the return past him and has to hand the route back afterwards.
	 */
	bool Configure(
		AIGPrologueWorldScene* InScene,
		AIGListenerEntity* InEntity = nullptr,
		AIGPlayerCharacter* InPlayer = nullptr,
		const TArray<FVector>& InCorridorPatrolPoints = TArray<FVector>());

	/**
	 * 비트 3-7 「귀환길」을 무장한다 — P4의 대답이 돌아온 순간에 불린다.
	 *
	 * §8's night three does not end at the wall either. T9 arms the walk home,
	 * and he is standing in the 4F corridor between the stair core and 403 —
	 * knocking, in a corridor 160 cm deep. She has just been taught 둘-쉬고-하나
	 * by P4; answering freezes him into Waiting and she walks past a thing that
	 * stopped to listen for her. 「회피 대상이 애도 대상으로」.
	 *
	 * Nothing forces the answer. Slipping past him unheard is a legitimate
	 * solution and always was — it simply is not this beat.
	 */
	void ArmReturnPass();

	/** 포획 리셋은 침대로 되돌린다. 그것이 도착으로 세어지면 안 된다. */
	void NotifyCaptureReset();

	/** Fired once, when she is back inside 403. This is what ends night three. */
	FIGNightThreeReturnedSignature OnReturnedHome;

	EIGNightThreeReturnStage GetReturnStage() const { return ReturnStage; }
	/** True while he is standing in the corridor on the way home. */
	bool IsFigureInCorridor() const { return bFigureStaged; }
	/** True once she has walked past him while he was waiting on an answer. */
	bool HasPassedWhileWaiting() const { return bPassedWhileWaiting; }
	FVector GetReturnPassPoint() const;

	/** Day/night boundary: reveals the journal once T7 is known by day. */
	void SetHourActive(bool bHourActive);

	/**
	 * 자재 위의 조율 렌치를 챙겼거나 밤4 벽이 열렸으면 치운다. 막간이 끝나는
	 * 검은 화면에서 그레이박스가 한 번 더 부른다.
	 */
	void RefreshTuningHammerAvailability();

	/** Fired once, when T9 crosses — the night-3 goal. */
	FIGNightThreeSolvedSignature OnSolved;

	/**
	 * Context verbs used by the player's dedicated Q/B and listen inputs.
	 * Keeping these outside CompleteInteraction prevents opening a door and
	 * filing one of P4's timed taps from the same E/A press.
	 */
	bool IsPlayerKnockTarget(const AActor* FocusedActor) const;
	bool IsPlayerListenTarget(const AActor* FocusedActor) const;
	bool TryPlayerKnock(AActor* FocusedActor, AActor* NoiseInstigator = nullptr);
	bool TryPlayerListen(AActor* FocusedActor, AActor* NoiseInstigator = nullptr);

	/** Probe queries. */
	bool ValidateFixtures() const;
	AIGSwingDoor* GetStairGate() const { return StairGate; }
	AIGSwingDoor* GetAnnexGate() const { return AnnexGate; }
	/** 5층 철문 안쪽 빗장. 손님이 열쇠를 돌려도 이게 걸려 있으면 문이 걸린다. */
	AIGDoorLatch* GetAnnexBolt() const { return AnnexBolt; }
	AIGMissingFloorEvidence* GetKeyring() const { return Keyring; }
	AIGReadableNote* GetTunerNotebook() const { return TunerNotebook; }
	AIGMissingFloorEvidence* GetRiserValve() const { return RiserValve; }
	AIGMissingFloorEvidence* GetWallListen(int32 BayIndex) const;

	/**
	 * World position of the one bay with the cavity behind it, for §20.3's
	 * observation net. Returns false before the fifth floor is dressed, and
	 * false once P3 is solved — a player who already knows does not need to be
	 * shown, and showing them anyway would read as the game not listening.
	 */
	bool GetCavityWallObservationPoint(FVector& OutLocation) const;
	AIGMissingFloorEvidence* GetImpactMark() const { return ImpactMark; }
	AIGMissingFloorEvidence* GetAnswerTarget() const { return AnswerTarget; }
	AIGReadableNote* GetLabelsNote() const { return LabelsNote; }
	AIGReadableNote* GetForumNote() const { return ForumNote; }
	AIGReadableNote* GetJournalNote() const { return JournalNote; }
	bool IsValveOpen() const { return bValveOpen; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RefreshKeyringAvailability();
	void HandleKeyringTaken(AIGMissingFloorEvidence* Evidence);
	void HandleValveOpened(AIGMissingFloorEvidence* Evidence);
	void HandleWallListened(int32 BayIndex);
	void HandlePlasterDatingExamined(AIGMissingFloorEvidence* Evidence);
	UFUNCTION()
	void HandleAnnexRecognitionZone(class AIGZoneTrigger* Zone);
	void HandleTuningHammerExamined(AIGMissingFloorEvidence* Evidence);
	/** 공구 카트를 한 번 민다. 입주 저녁 천장 너머에서 구르던 그 바퀴 소리가 난다. */
	void HandleTunerCartPushed(AIGMissingFloorEvidence* Evidence);
	void HandleWorkGloveExamined(AIGMissingFloorEvidence* Evidence);
	void HandleTankAuditionExamined(AIGMissingFloorEvidence* Evidence);

	/**
	 * Plays what the wall actually sounds like when an ear settles on it: the
	 * cavity's long mass-air-mass ring or a solid board's dead thud, plus the
	 * riser water at whatever band the structure left it in. The monologue that
	 * follows confirms this; it must never be the only thing that says it.
	 */
	void PlayWallListenResponse(int32 BayIndex, bool bHollow);
	/**
	 * P3의 비교가 끝났는가. 공동 칸과 다른 칸 하나를 귀든 주먹이든 한 번씩
	 * 들어 봐야 「이 벽만」이라고 말할 수 있다. 밸브 전의 청음은 세 벽이 다
	 * 같아서 세지 않는다.
	 */
	bool HasComparedWalls() const;
	void HandleWallKnocked(int32 BayIndex);
	void HandleImpactMarkExamined(AIGMissingFloorEvidence* Evidence);
	void HandleAnswerKnock(AIGMissingFloorEvidence* Evidence);
	void DeliverWallAnswer();
	/**
	 * 대답을 기다리며 걷어 둔 라이저 물길을 되살린다. 밸브를 연 뒤로 물은
	 * 밤낮없이 흐른다. 대답이 온 뒤에도, 05:30에 끊긴 뒤에도 여기로 온다.
	 */
	void ResumeRiserFlow(float FadeInSeconds);
	void EndAnswerSilence();
	/** 대답 뒤의 여운. 공기가 돌아오기 시작한 직후에 숨과 한 줄이 온다. */
	void AnswerAftermath();
	void HandleTruthConfirmed(EIGMissingFloorTruth Truth);
	void AdvanceReturn();
	void StageReturnFigure();
	void ReleaseReturnFigure();
	/**
	 * 대답을 받고도 05:30을 넘긴 밤3이 되풀이될 때 귀환길을 다시 세운다.
	 * T9는 새로 확정되지 않으므로 OnSolved가 다시 오지 않는다.
	 */
	void RearmReturnPassForRepeatedNight();
	/** 복도의 그가 그녀 눈앞에 있는가. 가르침 한 줄의 조건이다. */
	bool IsReturnFigureInSight() const;
	bool IsPlayerInsideUnit403() const;
	void RefreshAnswerTargetAvailability();
	void RefreshJournalAvailability(bool bHourActive);
	void SetJournalShown(bool bShown);
	/**
	 * 황순금이 일지를 401호 문에 거는 아침. 새벽 연출과 대사가 다 지나가고,
	 * 그녀가 그 문을 보고 있지 않을 때 문이 열렸다 닫힌다.
	 */
	void TickJournalHandover();
	void PlayJournalHandoverSound();
	bool IsJournalDoorInView() const;
	void RefreshDistantSeoVisibility();
	/** 서일영의 한 컷. 틱 대신 짧은 타이머로 그녀의 시선과 거리를 본다. */
	void TickDistantSeo();
	void HideDistantSeo(bool bSeenOnce);

	UFUNCTION()
	void HandleNotebookRead(AIGReadableNote* Note, bool bOpened);

	UFUNCTION()
	void HandleLabelsRead(AIGReadableNote* Note, bool bOpened);

	UFUNCTION()
	void HandleForumRead(AIGReadableNote* Note, bool bOpened);

	UFUNCTION()
	void HandleJournalRead(AIGReadableNote* Note, bool bOpened);

	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPrologueWorldScene> Scene;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGListenerEntity> Entity;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPlayerCharacter> Player;

	/** §8 비트 3-7. Handed back to him when the beat lets go of the corridor. */
	TArray<FVector> CorridorPatrolPoints;

	FTimerHandle ReturnTimer;
	EIGNightThreeReturnStage ReturnStage = EIGNightThreeReturnStage::Idle;
	bool bFigureStaged = false;
	bool bPassedWhileWaiting = false;
	bool bWasWestOfHim = false;
	bool bMustLeaveHomeAgain = false;
	/** 되풀이 밤에 침대에서 다시 세운 귀환길. 독백이 다르다. */
	bool bRepeatedPass = false;
	/** 복도에서 그를 처음 본 순간의 한 줄. 잡힌 뒤에는 한 번 더. */
	bool bReturnHintShown = false;
	bool bCaughtDuringReturn = false;
	/** 옆을 지난 뒤 몇 걸음 더 가면 참았던 숨이 나간다. */
	bool bReliefPending = false;
	/** 문턱이 아니라 방 안에 머문 시간. 돌아볼 틈을 둔다. */
	float HomeDwellSeconds = 0.0f;
	FTimerHandle RepeatPassTimer;
	FTimerHandle ReturnThoughtTimer;

	UPROPERTY(Transient)
	TObjectPtr<AIGSwingDoor> StairGate;

	UPROPERTY(Transient)
	TObjectPtr<AIGSwingDoor> AnnexGate;

	UPROPERTY(Transient)
	TObjectPtr<AIGDoorLatch> AnnexBolt;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> Keyring;

	UPROPERTY(Transient)
	TObjectPtr<AIGReadableNote> TunerNotebook;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> TuningHammer;

	/** ImageGen-derived, fully 3D workshop cart; never an interactive sprite. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> TunerToolCart;

	/** 카트를 미는 판정. 그림은 없고 한 번 밀면 꺼진다. */
	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> TunerCartPush;
	/** 밀린 카트가 한 뼘 구르다 서는 동안의 위치와, 바퀴가 멎은 뒤의 한 줄. */
	FTimerHandle TunerCartRollTimer;
	FTimerHandle TunerCartThoughtTimer;
	FVector TunerCartRollFrom = FVector::ZeroVector;
	FVector TunerCartRollTo = FVector::ZeroVector;
	double TunerCartRollStartSeconds = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> RiserValve;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AIGMissingFloorEvidence>> WallListens;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AIGMissingFloorEvidence>> WallKnocks;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> ImpactMark;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> AnswerTarget;

	/**
	 * T5 두 번째 출처. 새 벽이 언제 발라졌는지는 벽 자신이 말한다.
	 *
	 * 세 칸 전부에 둔다. 벽 전체가 한 번에 발린 새 벽이라 어느 칸에서 읽어도
	 * 같은 사실이고, 공동 칸에만 두면 「여기가 그 벽이다」를 공짜로 주어
	 * P3가 무너진다. 반대로 한 칸에만 두면 기록 화면이 말하는 자리와
	 * 실제로 읽는 자리가 어긋난다.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AIGMissingFloorEvidence>> PlasterDatings;

	/** T8 두 번째 출처. 갈증으로 죽은 사람의 벽 하나 옆에 물 2톤이 있었다. */
	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> TankAudition;

	/** §13 12행의 회수. 채널 5에서 본 화각에 직접 서는 자리. */
	UPROPERTY(Transient)
	TObjectPtr<class AIGZoneTrigger> AnnexRecognitionZone;

	/** §22.3 선택적 목격. 자재 더미 위에 남은 큰 손의 장갑 한 짝. */
	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> WorkGlove;


	UPROPERTY(Transient)
	TObjectPtr<AIGReadableNote> LabelsNote;

	UPROPERTY(Transient)
	TObjectPtr<AIGReadableNote> ForumNote;

	UPROPERTY(Transient)
	TObjectPtr<AIGReadableNote> JournalNote;

	/** Water moving in the riser once the valve opens. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> RiserFlow;

	/** Day-three fixed-camera figure; never used as a close or interactive NPC. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DistantSeo;

	/** 한 번 들어 본 칸. 밸브 뒤의 귀와 주먹만 센다. */
	TSet<int32> ComparedBays;
	/** 밸브 전에 귀를 대 본 칸. 세 벽이 다 똑같다는 말은 세 칸을 다 들은 뒤에 한다. */
	TSet<int32> DryListenedBays;

	FTimerHandle JournalHandoverTimer;
	FTimerHandle JournalHandoverSoundTimer;

	FTimerHandle SeoWatchTimer;
	EIGDistantSeoStage SeoStage = EIGDistantSeoStage::Idle;
	float SeoSeenSeconds = 0.0f;
	float SeoAwaySeconds = 0.0f;
	float SeoRetreatSeconds = 0.0f;
	FVector SeoRetreatTarget = FVector::ZeroVector;

	FDelegateHandle TruthHandle;
	FTimerHandle AnswerTimer;
	/** 벽 안에서 돌아오는 둘-쉬고-하나의 둘째와 셋째 타격. */
	FTimerHandle AnswerHitTimers[2];
	/** P4 탭마다 녹음을 바꿔 친다. 같은 녹음이 세 번이면 손이 아니라 기계다. */
	uint32 AnswerTapSerial = 0;
	FTimerHandle AnswerSilenceReleaseTimer;
	FTimerHandle AnswerAftermathTimer;
	/** 대답 동안 걷어 둔 라이저 물소리를 되살린다. */
	FTimerHandle RiserResumeTimer;
	/**
	 * 벽 청음의 물과 저수조 출렁임은 루프 파형이다. 한 번에 하나만 두고
	 * 독백이 끝날 무렵 걷는다. 놓아두면 들은 벽마다 밤새 흐른다.
	 */
	TWeakObjectPtr<UAudioComponent> WallWaterVoice;
	TWeakObjectPtr<UAudioComponent> TankSloshVoice;
	FTimerHandle WallWaterFadeTimer;
	FTimerHandle TankSloshFadeTimer;
	TArray<double> AnswerTapTimes;
	bool bValveOpen = false;
	bool bAnswerPending = false;
	bool bAnswerDelivered = false;
	bool bSolvedAnnounced = false;
	bool bAnswerTargetAnnounced = false;
	/** 편의점을 나서는 그녀의 눈을 끄는 발소리 하나. 한 번 세울 때마다 한 번. */
	bool bSeoAnnounced = false;
	bool bHourCurrentlyActive = true;
};
