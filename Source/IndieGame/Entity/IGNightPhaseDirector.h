#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sequence/IGObjectiveProvider.h"
#include "IGNightPhaseDirector.generated.h"

class AIGPrologueWorldScene;
class AIGPlayerCharacter;
class AIGListenerEntity;
class APlayerController;
class UIGMissingFloorNarrativeSubsystem;

DECLARE_MULTICAST_DELEGATE_OneParam(FIGHourActiveSignature, bool /*bActive*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FIGNightGoalSignature, int32 /*NightIndex*/);

/**
 * 새벽 04:30부터 05:30까지의 시간과 건물 출입을 관리한다.
 * 경과 시간을 게임 속 시각으로 환산한다. 밤의 일을 마친 것(CompleteNightGoal)과
 * 아침으로 넘어가는 것(ConcludeNight, 05:30)은 따로다. 일을 마쳐도 더 둘러볼 수 있고,
 * 403호 침대에서 「아침까지 쉬기」를 골라야 날이 샌다. 포획으로 돌아와도 경과 시간은 유지한다.
 *
 * 시간 흐름: 일시정지 메뉴는 세계째 멈춘다. 문서를 펼쳐 읽는 동안에는 이야기 시계만 선다
 * (그는 계속 움직인다). 퍼즐 조작과 짧은 연출 중에는 흐르지만, 05:30이 그 한가운데 오면
 * 손에 든 조작이나 연출이 끝날 때까지 새벽을 미룬다(최대 40초). 벽 안의 막간과 결말 연출은
 * 시계를 세운다. 잡히면 정신을 잃은 만큼 앞으로 민다(05:30 30초 전까지).
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGNightPhaseDirector
	: public AActor
	, public IIGObjectiveProvider
{
	GENERATED_BODY()

public:
	AIGNightPhaseDirector();

	/** Binds the scene whose envelope this director seals. */
	void Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer);

	/** Starts the hour: seals the building and begins counting. */
	UFUNCTION(BlueprintCallable, Category = "Night")
	void BeginTheHour(int32 NightIndex);

	/** 진행 시간을 초기화하지 않고 봉인된 야간 자동 저장을 복원한다. */
	void ResumeTheHour(int32 NightIndex, float ElapsedSeconds);

	/** 봉인된 건물을 열지 않고 엔딩 C 연출 동안 서사 시간을 멈춘다. */
	void SuspendForFailureEnding();
	/** 밤 4 범위의 서사 상태를 되돌린 뒤 같은 밤을 다시 시작한다. */
	void RestartTheHour(int32 NightIndex);

	/**
	 * 그 밤에 할 일을 마쳤다고 적는다. 새벽은 오지 않는다 — 발견한 뒤 둘러볼 시간이 있고,
	 * 403호 침대에서 스스로 밤을 끝낸다(ConcludeNight). 조용할 때 저장한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Night")
	void CompleteNightGoal();
	/**
	 * 지금 아침으로 넘어간다. 침대의 「아침까지 쉬기」와 결말처럼 이야기가 밤을 닫는 자리,
	 * 자동 검사만 부른다. 05:30과 같은 출구라 세계는 어느 길로 와도 같다.
	 */
	void ConcludeNight();
	/** 이 밤의 할 일을 적었다. 불러온 밤이면 저장된 비트로 되살린다. */
	bool IsGoalComplete() const { return bGoalComplete; }
	/** 할 일을 적을 때마다(불러온 밤을 되살릴 때도) 밤 번호와 함께 알린다. 침대가 「아침까지 쉬기」를 켠다. */
	FIGNightGoalSignature OnNightGoalRecorded;
	/** 채운 밤의 기록. 못 채운 밤은 다음 저녁에 같은 밤이 온다(§5.4). */
	static FName GoalBeatId(int32 NightIndex);
	/** 그날 황순금과 대화를 마쳤는지 저장해 낮 안내에 반영한다. */
	static FName DayConversationBeatId(int32 NightIndex);
	/**
	 * 다음 새벽은 눈을 감기지도, 아침 독백을 하지도 않는다. 엔딩 길에서
	 * 에필로그가 제 암전을 갖고 오므로 두 번 깜빡이면 안 된다.
	 */
	void SuppressNextMorningPresentation() { bMorningPresentationSuppressed = true; }
	/** 벽 안의 2분 40초는 이 밤의 시간이 아니다. 막간 동안 05:30이 서 있는다. */
	void SetHourPaused(bool bPaused) { bHourPaused = bPaused; }
	bool IsHourPaused() const { return bHourPaused; }
	/**
	 * 공동현관 잠금 소리만 낸다. 엔딩 B는 벽 앞에 앉은 채 05:30을 먼저 맞고,
	 * 진짜 새벽은 에필로그 아래서 소리 없이 온다.
	 */
	void PlayDawnLatchCue() { PlayEntranceLatch(); }

	UFUNCTION(BlueprintPure, Category = "Night")
	bool IsHourActive() const { return bHourActive; }
	bool IsFailureEndingSuspended() const { return bFailureEndingSuspended; }
	/**
	 * 05:30에 눈이 감기기 시작해서 다시 뜨기 시작할 때까지 참이다. 세계는 그
	 * 한가운데, 검은 화면 아래서 낮으로 바뀐다. 낮 전환을 받는 쪽은 이 동안
	 * 카메라 페이드를 건드리지 않는다.
	 */
	bool IsDawnTransitionInProgress() const { return bDawnTransitionInProgress; }

	UFUNCTION(BlueprintPure, Category = "Night")
	float GetHourElapsedSeconds() const { return HourElapsedSeconds; }

	/**
	 * 잡혀서 정신을 잃은 동안 흐른 시간. 그만큼 시계를 앞으로 민다(§5.4). 깨자마자
	 * 새벽이 와 눈이 감기면 깨어난 게 아니라서 05:30 앞 30초는 남긴다. 실제로 민
	 * 초를 돌려준다.
	 */
	float ConsumeHourSeconds(float Seconds);

	/** Minutes left in story time, for the objective line outside the hour. */
	UFUNCTION(BlueprintPure, Category = "Night")
	int32 GetStoryMinutesRemaining() const;

	// IIGObjectiveProvider
	virtual FText GetObjectiveText() const override;
	virtual float GetObjectiveProgress() const override;

	/**
	 * Fires on every hour boundary: true when the night seals, false at dawn.
	 * The composition root routes this to everything the boundary touches —
	 * entity dormancy, the booth lock, the day interactions.
	 */
	FIGHourActiveSignature OnHourActiveChanged;

	/**
	 * 한 시간이 실제로 몇 초인가(상한). 이야기 시간은 04:30~05:30이고, 실제로는 밤마다
	 * 32, 42, 55, 48분까지 둔다. 밤은 보통 할 일을 마친 사람이 침대에서 먼저 끝내고,
	 * 05:30은 막힌 사람을 낮(황순금의 힌트)으로 넘겨주는 끝이다.
	 *
	 * 임시값이다. 처음 하는 사람의 기록이 아직 없어서, 밤마다 하는 일(읽기, 퍼즐 조작,
	 * 돌아가는 길, 잡힌 뒤 복구)을 어림한 시간에 여유를 얹었다. 밤1 17~29분, 밤2 19~33분,
	 * 밤3 27~47분, 밤4 24~40분을 어림했다. 기록이 모이면 다시 맞춘다. 0.9.1까지는 22, 26,
	 * 32, 28분이었고 0.10.0 초안은 22, 31, 41, 34분이었다.
	 */
	static float GetHourDurationSeconds(int32 NightIndex)
	{
		switch (NightIndex)
		{
		case 1: return 1920.0f;
		case 2: return 2520.0f;
		case 3: return 3300.0f;
		case 4: return 2880.0f;
		default: return 1920.0f;
		}
	}
	/** 손목 시계. 경과 초를 이야기 시각(분, 04:30이 270)으로 옮긴다. */
	static int32 GetStoryMinuteAt(const int32 NightIndex, const float ElapsedSeconds)
	{
		const float Duration = GetHourDurationSeconds(NightIndex);
		const float Ratio = FMath::Clamp(ElapsedSeconds / Duration, 0.0f, 1.0f);
		return StoryStartMinutes
			+ FMath::FloorToInt(Ratio * (StoryEndMinutes - StoryStartMinutes));
	}
	static constexpr int32 StoryStartMinutes = 4 * 60 + 30;
	static constexpr int32 StoryEndMinutes = 5 * 60 + 30;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void TickHour();
	/**
	 * 새벽의 시각표. 그가 그 자리에 멈추고(숨기지 않는다), 눈이 감기고, 검은
	 * 화면 아래서 건물이 낮으로 바뀌고 잠금이 풀리는 소리가 아래에서 올라온
	 * 뒤에 눈을 뜬다. 엔딩 C의 새벽, 에필로그가 화면을 가진 엔딩 길, 무인
	 * 검증은 같은 결과를 즉시 만든다.
	 */
	void ReleaseAtDawn();
	/** 봉쇄를 풀고 낮을 알린다. 이 브로드캐스트가 그를 재운다. */
	void ApplyDawnWorld();
	/** 눈이 다 감긴 뒤. 세계를 바꾸고 낮의 자동 저장을 건다. */
	void FlipWorldUnderBlack();
	/** 공동현관 잠금. 1층의 딸깍과, 계단실을 타고 올라온 걸쇠 소리. */
	void PlayEntranceLatch();
	/** 눈을 뜬다. 목표 줄과 아침 독백이 같이 온다. */
	void FinishDawnPresentation();
	void PlayMorningLine();
	/** 눈을 감는 동안 그를 그 자리에 세운다. 포획도 이동도 없다. */
	void HoldListenersForDawn();
	void ReleaseHeldListeners();
	bool IsCaptureResetInFlight() const;
	/** 05:30이 와도 끝까지 둘 일이 있다(손에 든 퍼즐 조각, 진행 중인 짧은 연출). */
	bool IsDawnHeldByActivity() const;
	/** 종이를 펼쳐 놓고 서서 읽는 중이고, 그 종이로 세울 시간이 남았다. */
	bool IsReadingHold();
	APlayerController* GetDawnController() const;
	void ApplySealedPresentation(bool bSealed);
	void RequestMissingFloorAutosave(bool bAtNight);
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPrologueWorldScene> Scene;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPlayerCharacter> Player;

	float HourElapsedSeconds = 0.0f;
	/** 지금 밤의 길이. 밤이 시작될 때 GetHourDurationSeconds로 정한다. */
	float HourDurationSeconds = 1320.0f;
	bool bHourActive = false;
	bool bGoalComplete = false;
	bool bFailureEndingSuspended = false;
	bool bRestoringHour = false;
	bool bMorningPresentationSuppressed = false;
	bool bHourPaused = false;
	/** -IGListenerGreyboxProbe / -IGNightCapture: 새벽 직후의 세계를 같은 프레임에 본다. */
	bool bImmediateDawn = false;
	/** 새벽의 눈 감기가 화면을 쥐고 있다. FinishDawnPresentation이 놓는다. */
	bool bDawnTransitionInProgress = false;
	/** 05:30 뒤에 조작·연출이 끝나기를 기다린 시간. 밤마다 0부터 센다. */
	float DawnHoldSeconds = 0.0f;
	/** 지금 펼친 종이와, 그 종이로 시계를 세운 시간. 접으면 0으로 돌아간다. */
	TWeakObjectPtr<const class AIGReadableNote> PausedNote;
	float ReadingPauseSeconds = 0.0f;
	FTimerHandle HourTimer;
	FTimerHandle DawnTimer;
	FTimerHandle DawnWorldTimer;
	FTimerHandle DawnLatchTimer;
	TArray<TWeakObjectPtr<AIGListenerEntity>> DawnHeldListeners;
};
