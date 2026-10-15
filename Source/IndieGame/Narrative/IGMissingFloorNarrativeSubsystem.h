#pragma once

#include "CoreMinimal.h"
#include "Narrative/IGMissingFloorNarrativeTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IGMissingFloorNarrativeSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FIGMissingFloorTruthSignature, EIGMissingFloorTruth);

/**
 * The truth router for 없는 층 (STORY_BIBLE_MISSING_FLOOR.md §12).
 *
 * Every truth is confirmed the same way: by crossing **two independent
 * categories** of evidence. That uniformity is the whole reason this is a
 * table rather than a chain of bespoke predicates — see the rule table in the
 * .cpp, which is the single place the design's crossing rules live.
 *
 * Confirmation is always derived from the recorded sources, never latched, so
 * restoring a save (or hand-editing one) cannot produce a truth whose evidence
 * is missing. The final choice is gated on T6 + T7 + T9 and nothing else.
 */
UCLASS()
class INDIEGAME_API UIGMissingFloorNarrativeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The registered tag for a truth, or an invalid tag if unregistered. */
	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	static FGameplayTag GetTruthTag(EIGMissingFloorTruth Truth);

	/** The serialized name for an evidence record. */
	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	static FName GetSourceId(EIGMissingFloorSource Source);

	/**
	 * Records one piece of evidence and re-derives the truth it feeds.
	 * Returns true when this record was the one that completed the crossing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	bool RegisterTruthSource(EIGMissingFloorTruth Truth, EIGMissingFloorSource Source);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool HasTruth(EIGMissingFloorTruth Truth) const;

	/** True once this exact record has been observed. */
	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool HasSource(EIGMissingFloorTruth Truth, EIGMissingFloorSource Source) const;

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	int32 GetConfirmedTruthCount() const;

	/**
	 * Every distinct source filed against every truth, added up.
	 *
	 * §20.3's two automatic safety nets both key off "no new source": progress in
	 * this game is not distance covered or doors opened, it is having found one
	 * more thing that says something. A player who has learned nothing new in
	 * ninety seconds is stuck no matter how much walking they did.
	 */
	UFUNCTION(BlueprintPure, Category = "Missing Floor|Narrative")
	int32 GetTotalSourceCount() const;

	/** T6 + T7 + T9 — the wall, the life inside it, and the waiting. */
	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool IsFinalChoiceUnlocked() const;

	// -- 선택적 목격 (§22.3) -----------------------------------------------

	/** The serialized name for one optional sighting. */
	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	static FName GetWitnessId(EIGMissingFloorWitness Witness);

	/**
	 * 본 것을 적어 둔다. 처음 본 것이면 true.
	 *
	 * 진실도 게이트도 건드리지 않으므로 `RegisterTruthSource`와 달리
	 * 반환값에 진행이 걸리지 않는다. 호출부는 두 번 적히지 않게만 쓰면 된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	bool RecordWitness(EIGMissingFloorWitness Witness);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool HasWitness(EIGMissingFloorWitness Witness) const;

	/** 몇 개를 봤는지. 플레이어에게 보여 주는 숫자가 아니라 문장 선택용이다. */
	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	int32 GetWitnessCount() const { return Snapshot.Night.Witnesses.Num(); }

	// -- 편의점 물 ----------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	EIGPurchaseProfile GetStorePurchaseProfile() const
	{
		return Snapshot.StorePurchaseProfile;
	}

	/** 손에 든 병을 적는다. 계산한 뒤에는 부르는 쪽이 더 부르지 않는다. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	void SetStorePurchaseProfile(EIGPurchaseProfile Profile);

	// -- the hour's persistent runtime facts (§5.4) ------------------------

	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	void SetNightIndex(int32 NightIndex);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	int32 GetNightIndex() const { return Snapshot.Night.NightIndex; }

	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	void SetHourSealed(bool bSealed);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool IsHourSealed() const { return Snapshot.Night.bTheHourSealed; }

	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	void SetNightElapsedSeconds(float Seconds);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	float GetNightElapsedSeconds() const { return Snapshot.Night.NightElapsedSeconds; }

	/** Counts the capture and returns the resulting aggression tier (0..3). */
	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	int32 RecordCapture();

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	int32 GetAggressionTier() const { return Snapshot.Night.AggressionTier; }

	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	void SetAggressionTier(int32 Tier);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	int32 GetCaptureCount() const { return Snapshot.Night.CaptureCount; }

	/** Returns false when the beat had already been played. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	bool MarkBeatPlayed(FName BeatId);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool HasBeatPlayed(FName BeatId) const;

	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	bool MarkPuzzleSolved(FName PuzzleId);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool IsPuzzleSolved(FName PuzzleId) const;

	// -- night 4: hydraulic mask, wall and endings -------------------------

	/** Records a P5 control once, preserving the first-activation order. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	bool ActivateNightFourControl(FName ControlId);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool HasNightFourControl(FName ControlId) const;

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool IsNightFourMaskRunning() const;

	const TArray<FName>& GetNightFourControlOrder() const
	{
		return Snapshot.Night.NightFourControlOrder;
	}

	/** 실제로 휘두른 망치 한 번을 더하고 그 수를 돌려준다. 확장판은 빗나간 것도 센다. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	int32 RecordNightFourWallStrike();

	/**
	 * 벽을 어디까지 뜯었나(0..5). 확장판은 유효 단계, 망치질을 시작한 확장 전 저장은 예전
	 * 다섯 번 순서의 망치질 수다. 정전·대답·개방은 이 값으로 정한다.
	 */
	int32 GetNightFourWallProgress() const;

	/** 망치질 수의 상한. 빗나간 것까지 세므로 다섯보다 넉넉하다. */
	static constexpr int32 MaxNightFourWallStrikes = 99;

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	int32 GetNightFourWallStrikeCount() const
	{
		return Snapshot.Night.NightFourWallStrikeCount;
	}

	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	void SetNightFourWallOpened(bool bOpened);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool IsNightFourWallOpened() const { return Snapshot.Night.bNightFourWallOpened; }

	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	void SetFirstReportMade(bool bMade);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool WasFirstReportMade() const { return Snapshot.Night.bFirstReportMade; }

	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	void SetSecondReportMade(bool bMade);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool WasSecondReportMade() const { return Snapshot.Night.bSecondReportMade; }

	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	void SetFifthDawnInterludeCompleted(bool bCompleted);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	bool WasFifthDawnInterludeCompleted() const
	{
		return Snapshot.Night.bFifthDawnInterludeCompleted;
	}

	/** Accepts only Ending.A/B/C and never overwrites an existing choice. */
	UFUNCTION(BlueprintCallable, Category = "Narrative|MissingFloor")
	bool SelectEnding(FName EndingId);

	UFUNCTION(BlueprintPure, Category = "Narrative|MissingFloor")
	FName GetEndingChoice() const { return Snapshot.Night.EndingChoice; }

	/**
	 * 밤 4에서 소비된 플레이 상태만 되돌린다. 확인한 진실, 첫 신고,
	 * 다섯 번째 새벽의 기억과 누적 포획은 이미 일어난 역사로 보존한다.
	 * 확장판에서 걷어 낸 벽의 층과 조립·조사는 그대로 남는다.
	 */
	void ResetNightFourForRetry();

	// -- 302호에 남은 짐 ----------------------------------------------------

	const FIGLeftoverState& GetLeftover() const { return Snapshot.Leftover; }
	/** 단계·정우 상태처럼 이름 목록이 아닌 칸을 고칠 때. */
	FIGLeftoverState& MutableLeftover() { return Snapshot.Leftover; }
	/** 목록에 이름을 더한다. 처음 더하면 true. */
	bool AddLeftover(EIGLeftoverList List, FName Name);
	bool RemoveLeftover(EIGLeftoverList List, FName Name);
	bool HasLeftover(EIGLeftoverList List, FName Name) const;
	/** 확장 전 판을 이어 온 저장이다. */
	bool IsLegacyRun() const { return Snapshot.Leftover.Version <= 0; }

	// -- 들은 말 -----------------------------------------------------------

	/** 끝까지 들은 대사를 남긴다. 바로 앞 줄과 같으면 다시 적지 않는다. */
	void RecordHeardLine(const FText& Speaker, const FText& Line);
	const TArray<FIGHeardLine>& GetHeardLines() const { return Snapshot.Heard; }
	static constexpr int32 MaximumHeardLines = 80;

	// -- persistence -------------------------------------------------------

	const FIGMissingFloorNarrativeSnapshot& GetSnapshot() const { return Snapshot; }
	void RestoreSnapshot(const FIGMissingFloorNarrativeSnapshot& InSnapshot);
	void ResetNarrative();

	/** Fires once, when a truth first crosses. */
	FIGMissingFloorTruthSignature OnTruthConfirmed;

	/**
	 * Current snapshot schema. Bumped only with a matching migration.
	 *
	 * 3 — §22.3 선택적 목격이 밤 상태에 덧붙었다. 마이그레이션이 따로 없는
	 * 것이 마이그레이션이다: v2 저장은 아무것도 못 본 회차로 읽히고, 그게
	 * 실제로 그 회차의 사실이다.
	 * 4 — 「302호에 남은 짐」(Leftover)이 붙었다. 3 이하 저장은 Version 0(확장 전 판)으로
	 * 읽고, 밤4 망치질을 1~4번 한 저장은 예전 다섯 번 순서로 끝내게 표시한다.
	 */
	static constexpr int32 SnapshotSchemaVersion = 4;

private:
	/** Rebuilds bConfirmed on every record from its sources alone. */
	void RecomputeConfirmations(bool bBroadcastNewlyConfirmed);
	/** Drops unknown tags and duplicate records left by an older build. */
	void NormalizeSnapshot();
	FIGMissingFloorTruthRecord* FindRecord(const FGameplayTag& TruthTag);
	TArray<FName>* LeftoverList(EIGLeftoverList List);
	const TArray<FName>* LeftoverList(EIGLeftoverList List) const;
	const FIGMissingFloorTruthRecord* FindRecord(const FGameplayTag& TruthTag) const;

	FIGMissingFloorNarrativeSnapshot Snapshot;
};
