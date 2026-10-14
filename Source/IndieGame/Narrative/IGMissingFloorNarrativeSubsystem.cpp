#include "Narrative/IGMissingFloorNarrativeSubsystem.h"

#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Engine/World.h"

namespace IGMissingFloorNarrative
{
	const FName NightFourDrain(TEXT("P5.RoofCleaningDrain"));
	const FName NightFourFloatBypass(TEXT("P5.RoofFloatBypass"));
	const FName NightFourTransferPump(TEXT("P5.TransferPump"));
	const FName EndingA(TEXT("Ending.A"));
	const FName EndingB(TEXT("Ending.B"));
	const FName EndingC(TEXT("Ending.C"));

	static bool IsNightFourControl(const FName ControlId)
	{
		return ControlId == NightFourDrain
			|| ControlId == NightFourFloatBypass
			|| ControlId == NightFourTransferPump;
	}

	static bool IsEnding(const FName EndingId)
	{
		return EndingId == EndingA
			|| EndingId == EndingB
			|| EndingId == EndingC;
	}

	/** 모르는 이름을 읽으면 엔진이 열거형 끝값을 넣는다. 그걸 걸러 낸다. */
	static bool IsKnownPurchaseProfile(const EIGPurchaseProfile Profile)
	{
		return static_cast<uint8>(Profile)
			<= static_cast<uint8>(EIGPurchaseProfile::ProfileC2LX2);
	}

	/**
	 * One evidence category: any one of these records satisfies it. Most
	 * categories hold a single record; T6's location category holds two
	 * because the design offers a patient route (compare the water through
	 * the pipes) and a reckless one (knock on the wall and be heard).
	 */
	struct FCategory
	{
		TArray<EIGMissingFloorSource> AcceptedSources;
	};

	/**
	 * A truth crosses when EVERY category has at least one record. This table
	 * is the executable form of STORY_BIBLE_MISSING_FLOOR.md §12 — the crossing
	 * rules live here and nowhere else.
	 */
	struct FRule
	{
		EIGMissingFloorTruth Truth = EIGMissingFloorTruth::None;
		const TCHAR* TagName = nullptr;
		TArray<FCategory> Categories;
	};

	static const TArray<FRule>& RuleTable()
	{
		static const TArray<FRule> Rules = {
			{
				EIGMissingFloorTruth::LivedUpstairs,
				TEXT("Truth.MissingFloor.LivedUpstairs"),
				{
					{{EIGMissingFloorSource::MeterFifthDial}},
					{{EIGMissingFloorSource::MeterReadingSheet}},
				},
			},
			{
				EIGMissingFloorTruth::TenantIdentity,
				TEXT("Truth.MissingFloor.TenantIdentity"),
				{
					{{EIGMissingFloorSource::ShippingLabels}},
					{{EIGMissingFloorSource::TunerNotebookName}},
				},
			},
			{
				EIGMissingFloorTruth::NoiseWasHomecoming,
				TEXT("Truth.MissingFloor.NoiseWasHomecoming"),
				{
					{{EIGMissingFloorSource::NoiseForumPosts}},
					{{EIGMissingFloorSource::TunerWorkSchedule}},
				},
			},
			{
				EIGMissingFloorTruth::LandingStruggle,
				TEXT("Truth.MissingFloor.LandingStruggle"),
				{
					{{EIGMissingFloorSource::ForumFinalPost}},
					{{EIGMissingFloorSource::LandingImpactMark}},
				},
			},
			{
				EIGMissingFloorTruth::WallSealedThatDay,
				TEXT("Truth.MissingFloor.WallSealedThatDay"),
				{
					{{EIGMissingFloorSource::BoardDeliveryReceipt}},
					{{EIGMissingFloorSource::FreshPlasterDating}},
				},
			},
			{
				EIGMissingFloorTruth::SomeoneInTheWall,
				TEXT("Truth.MissingFloor.SomeoneInTheWall"),
				{
					// Knowing what a hollow wall sounds like...
					{{EIGMissingFloorSource::PipeAuditionCriterion}},
					// ...and finding which wall it is, by water or by fist.
					{{
						EIGMissingFloorSource::PipeWaterComparison,
						EIGMissingFloorSource::WallEchoByHand,
					}},
				},
			},
			{
				EIGMissingFloorTruth::WasStillAlive,
				TEXT("Truth.MissingFloor.WasStillAlive"),
				{
					{{EIGMissingFloorSource::CarbonLedgerOriginal}},
					{{EIGMissingFloorSource::AgentMoveOutMessage}},
				},
			},
			{
				EIGMissingFloorTruth::FiveNightsOfThirst,
				TEXT("Truth.MissingFloor.FiveNightsOfThirst"),
				{
					{{EIGMissingFloorSource::KnockTallyJournal}},
					{{EIGMissingFloorSource::TankWaterAudition}},
				},
			},
			{
				EIGMissingFloorTruth::WaitingForAnAnswer,
				TEXT("Truth.MissingFloor.WaitingForAnAnswer"),
				{
					{{EIGMissingFloorSource::AnswerRhythmMaterials}},
					{{EIGMissingFloorSource::AnswerReturned}},
				},
			},
			{
				EIGMissingFloorTruth::StillCoveringIt,
				TEXT("Truth.MissingFloor.StillCoveringIt"),
				{
					{{EIGMissingFloorSource::EvictionWarning}},
					{{EIGMissingFloorSource::BreakerCutIntervention}},
				},
			},
		};
		return Rules;
	}

	static const FRule* FindRule(const EIGMissingFloorTruth Truth)
	{
		return RuleTable().FindByPredicate([Truth](const FRule& Rule)
		{
			return Rule.Truth == Truth;
		});
	}

	static const FRule* FindRuleByTag(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid())
		{
			return nullptr;
		}
		return RuleTable().FindByPredicate([&Tag](const FRule& Rule)
		{
			return UIGMissingFloorNarrativeSubsystem::GetTruthTag(Rule.Truth)
				.MatchesTagExact(Tag);
		});
	}

	/**
	 * Serialized evidence names. Strings, not enum values, so appending to
	 * EIGMissingFloorSource can never reinterpret an existing save.
	 */
	/**
	 * 선택적 목격의 직렬화 이름(§22.3). 증거 이름과 접두사 공간을 나눠 쓴다 —
	 * 저장 파일을 열어 본 사람이 무엇이 퍼즐이고 무엇이 그냥 본 것인지
	 * 구분할 수 있어야 한다.
	 */
	static const TCHAR* WitnessName(const EIGMissingFloorWitness Witness)
	{
		switch (Witness)
		{
		case EIGMissingFloorWitness::SeoSleepingPills:
			return TEXT("Seen.SeoSleepingPills");
		case EIGMissingFloorWitness::HwangWaterBowl:
			return TEXT("Seen.HwangWaterBowl");
		case EIGMissingFloorWitness::BoothSoundproofing:
			return TEXT("Seen.BoothSoundproofing");
		case EIGMissingFloorWitness::RooftopCigarettePack:
			return TEXT("Seen.RooftopCigarettePack");
		case EIGMissingFloorWitness::BoothWallCalendar:
			return TEXT("Seen.BoothWallCalendar");
		case EIGMissingFloorWitness::RecorderEmptyBay:
			return TEXT("Seen.RecorderEmptyBay");
		case EIGMissingFloorWitness::AnnexWorkGlove:
			return TEXT("Seen.AnnexWorkGlove");
		case EIGMissingFloorWitness::StoreNightRoster:
			return TEXT("Seen.StoreNightRoster");
		case EIGMissingFloorWitness::Unit401DoorRadio:
			return TEXT("Seen.Unit401DoorRadio");
		case EIGMissingFloorWitness::Unit402Silence:
			return TEXT("Heard.Unit402Silence");
		case EIGMissingFloorWitness::RoofDoorWind:
			return TEXT("Heard.RoofDoorWind");
		case EIGMissingFloorWitness::BoothInnerRoomHum:
			return TEXT("Heard.BoothInnerRoomHum");
		case EIGMissingFloorWitness::SeoVoiceRecorder:
			return TEXT("Heard.SeoVoiceRecorder");
		case EIGMissingFloorWitness::InnerRoomCoverPieces:
			return TEXT("Seen.InnerRoomCoverPieces");
		case EIGMissingFloorWitness::InspectionCoverMatched:
			return TEXT("Seen.InspectionCoverMatched");
		default:
			return nullptr;
		}
	}

	static const TCHAR* SourceName(const EIGMissingFloorSource Source)
	{
		switch (Source)
		{
		case EIGMissingFloorSource::MeterFifthDial:
			return TEXT("Lobby.MeterFifthDial");
		case EIGMissingFloorSource::MeterReadingSheet:
			return TEXT("Office.MeterReadingSheet");
		case EIGMissingFloorSource::ShippingLabels:
			return TEXT("Estate.ShippingLabels");
		case EIGMissingFloorSource::TunerNotebookName:
			return TEXT("Fifth.TunerNotebookName");
		case EIGMissingFloorSource::NoiseForumPosts:
			return TEXT("Forum.NoisePosts");
		case EIGMissingFloorSource::TunerWorkSchedule:
			return TEXT("Fifth.TunerWorkSchedule");
		case EIGMissingFloorSource::ForumFinalPost:
			return TEXT("Forum.FinalPost");
		case EIGMissingFloorSource::LandingImpactMark:
			return TEXT("Fifth.LandingImpactMark");
		case EIGMissingFloorSource::BoardDeliveryReceipt:
			return TEXT("Office.BoardDeliveryReceipt");
		case EIGMissingFloorSource::FreshPlasterDating:
			return TEXT("Fifth.FreshPlasterDating");
		case EIGMissingFloorSource::PipeAuditionCriterion:
			return TEXT("Fifth.PipeAuditionCriterion");
		case EIGMissingFloorSource::PipeWaterComparison:
			return TEXT("Fifth.PipeWaterComparison");
		case EIGMissingFloorSource::WallEchoByHand:
			return TEXT("Fifth.WallEchoByHand");
		case EIGMissingFloorSource::CarbonLedgerOriginal:
			return TEXT("Office.CarbonLedgerOriginal");
		case EIGMissingFloorSource::AgentMoveOutMessage:
			return TEXT("Office.AgentMoveOutMessage");
		case EIGMissingFloorSource::KnockTallyJournal:
			return TEXT("Unit401.KnockTallyJournal");
		case EIGMissingFloorSource::TankWaterAudition:
			return TEXT("Roof.TankWaterAudition");
		case EIGMissingFloorSource::AnswerRhythmMaterials:
			return TEXT("Player.AnswerRhythmMaterials");
		case EIGMissingFloorSource::AnswerReturned:
			return TEXT("Fifth.AnswerReturned");
		case EIGMissingFloorSource::EvictionWarning:
			return TEXT("Office.EvictionWarning");
		case EIGMissingFloorSource::BreakerCutIntervention:
			return TEXT("Fifth.BreakerCutIntervention");
		case EIGMissingFloorSource::AnswerRhythmVoicemail:
			return TEXT("Phone.AnswerRhythmVoicemail");
		case EIGMissingFloorSource::AnswerRhythmNotebook:
			return TEXT("Fifth.AnswerRhythmNotebook");
		case EIGMissingFloorSource::AnswerRhythmJournal:
			return TEXT("Unit401.AnswerRhythmJournal");
		default:
			return nullptr;
		}
	}
}

FGameplayTag UIGMissingFloorNarrativeSubsystem::GetTruthTag(
	const EIGMissingFloorTruth Truth)
{
	const IGMissingFloorNarrative::FRule* Rule =
		IGMissingFloorNarrative::FindRule(Truth);
	if (!Rule || !Rule->TagName)
	{
		return FGameplayTag();
	}
	// Every tag in this project comes from Config/DefaultGameplayTags.ini and
	// is requested without erroring. A missing row therefore yields an invalid
	// tag and silently disables the truth — the .ini rows are mandatory.
	return FGameplayTag::RequestGameplayTag(FName(Rule->TagName), false);
}

FName UIGMissingFloorNarrativeSubsystem::GetSourceId(
	const EIGMissingFloorSource Source)
{
	const TCHAR* Name = IGMissingFloorNarrative::SourceName(Source);
	return Name ? FName(Name) : NAME_None;
}

FName UIGMissingFloorNarrativeSubsystem::GetWitnessId(
	const EIGMissingFloorWitness Witness)
{
	const TCHAR* Name = IGMissingFloorNarrative::WitnessName(Witness);
	return Name ? FName(Name) : NAME_None;
}

bool UIGMissingFloorNarrativeSubsystem::RecordWitness(
	const EIGMissingFloorWitness Witness)
{
	const FName WitnessId = GetWitnessId(Witness);
	if (WitnessId.IsNone() || Snapshot.Night.Witnesses.Contains(WitnessId))
	{
		return false;
	}
	Snapshot.Night.Witnesses.Add(WitnessId);
	// 진실을 다시 계산하지 않는다. 목격은 어떤 교차에도 들어가지 않으므로
	// 여기서 RecomputeConfirmations를 부르면 「본 것이 진실을 열 수도 있다」는
	// 잘못된 인상을 코드에 남긴다.
	return true;
}

bool UIGMissingFloorNarrativeSubsystem::HasWitness(
	const EIGMissingFloorWitness Witness) const
{
	const FName WitnessId = GetWitnessId(Witness);
	return !WitnessId.IsNone() && Snapshot.Night.Witnesses.Contains(WitnessId);
}

bool UIGMissingFloorNarrativeSubsystem::RegisterTruthSource(
	const EIGMissingFloorTruth Truth,
	const EIGMissingFloorSource Source)
{
	const FGameplayTag TruthTag = GetTruthTag(Truth);
	const FName SourceId = GetSourceId(Source);
	if (!TruthTag.IsValid() || SourceId.IsNone())
	{
		return false;
	}

	FIGMissingFloorTruthRecord* Record = FindRecord(TruthTag);
	if (!Record)
	{
		FIGMissingFloorTruthRecord NewRecord;
		NewRecord.TruthTag = TruthTag;
		Record = &Snapshot.Truths[Snapshot.Truths.Add(MoveTemp(NewRecord))];
	}

	const bool bWasConfirmed = Record->bConfirmed;
	Record->SourceIds.AddUnique(SourceId);
	RecomputeConfirmations(/*bBroadcastNewlyConfirmed=*/true);

	// FindRecord again: RecomputeConfirmations may reallocate nothing today,
	// but reading through a pointer taken before a mutation is a trap worth
	// not leaving behind.
	const FIGMissingFloorTruthRecord* Updated = FindRecord(TruthTag);
	return Updated && Updated->bConfirmed && !bWasConfirmed;
}

bool UIGMissingFloorNarrativeSubsystem::HasTruth(
	const EIGMissingFloorTruth Truth) const
{
	const FIGMissingFloorTruthRecord* Record = FindRecord(GetTruthTag(Truth));
	return Record && Record->bConfirmed;
}

bool UIGMissingFloorNarrativeSubsystem::HasSource(
	const EIGMissingFloorTruth Truth,
	const EIGMissingFloorSource Source) const
{
	const FIGMissingFloorTruthRecord* Record = FindRecord(GetTruthTag(Truth));
	return Record && Record->SourceIds.Contains(GetSourceId(Source));
}

int32 UIGMissingFloorNarrativeSubsystem::GetTotalSourceCount() const
{
	int32 Count = 0;
	for (const FIGMissingFloorTruthRecord& Record : Snapshot.Truths)
	{
		Count += Record.SourceIds.Num();
	}
	return Count;
}

int32 UIGMissingFloorNarrativeSubsystem::GetConfirmedTruthCount() const
{
	int32 Count = 0;
	for (const FIGMissingFloorTruthRecord& Record : Snapshot.Truths)
	{
		if (Record.bConfirmed)
		{
			++Count;
		}
	}
	return Count;
}

bool UIGMissingFloorNarrativeSubsystem::IsFinalChoiceUnlocked() const
{
	return HasTruth(EIGMissingFloorTruth::SomeoneInTheWall)
		&& HasTruth(EIGMissingFloorTruth::WasStillAlive)
		&& HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer);
}

// -- 편의점 물 ----------------------------------------------------------------

void UIGMissingFloorNarrativeSubsystem::SetStorePurchaseProfile(
	const EIGPurchaseProfile Profile)
{
	Snapshot.StorePurchaseProfile =
		IGMissingFloorNarrative::IsKnownPurchaseProfile(Profile)
			? Profile
			: EIGPurchaseProfile::Unset;
}

// -- the hour's persistent runtime facts ------------------------------------

void UIGMissingFloorNarrativeSubsystem::SetNightIndex(const int32 NightIndex)
{
	const int32 Clamped = FMath::Clamp(NightIndex, 0, 4);
	if (Clamped != Snapshot.Night.NightIndex)
	{
		// §20.2: 공격성 티어는 밤이 끝나면 1로 하강한다. Impatience earned by
		// last night's captures does not carry in full, or a bad night 2 would
		// make nights 3 and 4 unplayable. It descends rather than resets: a
		// player who was never caught still starts at 0.
		Snapshot.Night.AggressionTier =
			FMath::Min(Snapshot.Night.AggressionTier, 1);
	}
	Snapshot.Night.NightIndex = Clamped;
}

void UIGMissingFloorNarrativeSubsystem::SetHourSealed(const bool bSealed)
{
	Snapshot.Night.bTheHourSealed = bSealed;
	if (!bSealed)
	{
		Snapshot.Night.NightElapsedSeconds = 0.0f;
	}
}

void UIGMissingFloorNarrativeSubsystem::SetNightElapsedSeconds(const float Seconds)
{
	Snapshot.Night.NightElapsedSeconds = FMath::Max(Seconds, 0.0f);
}

int32 UIGMissingFloorNarrativeSubsystem::RecordCapture()
{
	++Snapshot.Night.CaptureCount;
	Snapshot.Night.AggressionTier =
		FMath::Clamp(Snapshot.Night.AggressionTier + 1, 0, 3);
	return Snapshot.Night.AggressionTier;
}

void UIGMissingFloorNarrativeSubsystem::SetAggressionTier(const int32 Tier)
{
	Snapshot.Night.AggressionTier = FMath::Clamp(Tier, 0, 3);
}

bool UIGMissingFloorNarrativeSubsystem::MarkBeatPlayed(const FName BeatId)
{
	if (BeatId.IsNone() || Snapshot.Night.CompletedBeats.Contains(BeatId))
	{
		return false;
	}
	Snapshot.Night.CompletedBeats.Add(BeatId);
	return true;
}

bool UIGMissingFloorNarrativeSubsystem::HasBeatPlayed(const FName BeatId) const
{
	return Snapshot.Night.CompletedBeats.Contains(BeatId);
}

bool UIGMissingFloorNarrativeSubsystem::MarkPuzzleSolved(const FName PuzzleId)
{
	if (PuzzleId.IsNone() || Snapshot.Night.SolvedPuzzles.Contains(PuzzleId))
	{
		return false;
	}
	Snapshot.Night.SolvedPuzzles.Add(PuzzleId);
	return true;
}

bool UIGMissingFloorNarrativeSubsystem::IsPuzzleSolved(const FName PuzzleId) const
{
	return Snapshot.Night.SolvedPuzzles.Contains(PuzzleId);
}

bool UIGMissingFloorNarrativeSubsystem::ActivateNightFourControl(
	const FName ControlId)
{
	if (!IGMissingFloorNarrative::IsNightFourControl(ControlId)
		|| Snapshot.Night.NightFourControlOrder.Contains(ControlId))
	{
		return false;
	}
	Snapshot.Night.NightFourControlOrder.Add(ControlId);
	return true;
}

bool UIGMissingFloorNarrativeSubsystem::HasNightFourControl(
	const FName ControlId) const
{
	return IGMissingFloorNarrative::IsNightFourControl(ControlId)
		&& Snapshot.Night.NightFourControlOrder.Contains(ControlId);
}

bool UIGMissingFloorNarrativeSubsystem::IsNightFourMaskRunning() const
{
	return HasNightFourControl(IGMissingFloorNarrative::NightFourDrain)
		&& HasNightFourControl(IGMissingFloorNarrative::NightFourFloatBypass)
		&& HasNightFourControl(IGMissingFloorNarrative::NightFourTransferPump);
}

int32 UIGMissingFloorNarrativeSubsystem::RecordNightFourWallStrike()
{
	// 실제로 휘두른 수다. 확장판에서는 엉뚱한 데를 친 것도 센다. 벽을 어디까지 뜯었는지는
	// 유효 단계(Leftover.WallStage)가 따로 센다.
	Snapshot.Night.NightFourWallStrikeCount = FMath::Clamp(
		Snapshot.Night.NightFourWallStrikeCount + 1,
		0,
		MaxNightFourWallStrikes);
	return Snapshot.Night.NightFourWallStrikeCount;
}

int32 UIGMissingFloorNarrativeSubsystem::GetNightFourWallProgress() const
{
	if (Snapshot.Night.bNightFourWallOpened)
	{
		return 5;
	}
	return Snapshot.Leftover.bLegacyWallSequence
		? FMath::Min(Snapshot.Night.NightFourWallStrikeCount, 5)
		: Snapshot.Leftover.WallStage;
}

void UIGMissingFloorNarrativeSubsystem::SetNightFourWallOpened(
	const bool bOpened)
{
	Snapshot.Night.bNightFourWallOpened = bOpened;
	if (bOpened)
	{
		// 유효 단계는 벽을 실제로 뜯은 쪽(밤4 감독)이 적는다. 열린 벽의 진행은
		// GetNightFourWallProgress가 5로 읽는다.
		Snapshot.Night.NightFourWallStrikeCount = FMath::Max(Snapshot.Night.NightFourWallStrikeCount, 5);
	}
}

void UIGMissingFloorNarrativeSubsystem::SetFirstReportMade(const bool bMade)
{
	Snapshot.Night.bFirstReportMade = bMade;
}

void UIGMissingFloorNarrativeSubsystem::SetSecondReportMade(const bool bMade)
{
	Snapshot.Night.bSecondReportMade = bMade;
}

void UIGMissingFloorNarrativeSubsystem::SetFifthDawnInterludeCompleted(
	const bool bCompleted)
{
	Snapshot.Night.bFifthDawnInterludeCompleted = bCompleted;
}

bool UIGMissingFloorNarrativeSubsystem::SelectEnding(const FName EndingId)
{
	if (!IGMissingFloorNarrative::IsEnding(EndingId)
		|| !Snapshot.Night.EndingChoice.IsNone())
	{
		return false;
	}
	Snapshot.Night.EndingChoice = EndingId;
	return true;
}

void UIGMissingFloorNarrativeSubsystem::ResetNightFourForRetry()
{
	Snapshot.Night.NightIndex = 4;
	Snapshot.Night.AggressionTier = 1;
	Snapshot.Night.bTheHourSealed = true;
	Snapshot.Night.NightElapsedSeconds = 0.0f;
	// P5(물길 세 조작)는 남긴다. 결말 C를 보고 다시 하는 밤4에서 이미 푼 퍼즐을 또
	// 풀게 하지 않는다. 밤이 열리면 물길 소리가 바로 돈다. 벽과 망치질, 정전, 대치는
	// 처음부터다(§34).
	Snapshot.Night.NightFourWallStrikeCount = 0;
	Snapshot.Night.bNightFourWallOpened = false;
	Snapshot.Night.bSecondReportMade = false;
	Snapshot.Night.EndingChoice = NAME_None;
	Snapshot.Night.CompletedBeats.Remove(FName(TEXT("Night4.PowerCut")));
	Snapshot.Night.CompletedBeats.Remove(FName(TEXT("Night4.FinalReveal")));
	Snapshot.Night.CompletedBeats.Remove(FName(TEXT("Night4.FinalConfrontation")));
	Snapshot.Night.CompletedBeats.Remove(FName(TEXT("Night4.ChoiceOffered")));
	Snapshot.Night.CompletedBeats.Remove(FName(TEXT("Night4.SecondReport")));
	// 확장판: 걷어 낸 층과 푼 고정점, 건 프레임, 유효 단계, 조립과 조사는 남긴다. 다시 하는
	// 밤에 같은 벽을 또 뜯게 하지 않는다. 3단계를 넘긴 판은 밤4 디렉터가 시간을 다시 열 때
	// 정전을 걸어 둔 채로 이어 간다(바깥층이 이미 없다).
	// 예전 다섯 번 순서로 하던 판은 망치질 수가 0으로 돌아가므로 새 순서로 다시 시작한다.
	FIGLeftoverState& Leftover = Snapshot.Leftover;
	if (Leftover.bLegacyWallSequence)
	{
		Leftover.bLegacyWallSequence = false;
		Leftover.WallLayers.Reset();
		Leftover.WallStage = 0;
		Leftover.bFramePlaced = false;
	}
}

TArray<FName>* UIGMissingFloorNarrativeSubsystem::LeftoverList(const EIGLeftoverList List)
{
	FIGLeftoverState& Leftover = Snapshot.Leftover;
	switch (List)
	{
	case EIGLeftoverList::Taken: return &Leftover.Taken;
	case EIGLeftoverList::Stored: return &Leftover.Stored;
	case EIGLeftoverList::Examined: return &Leftover.Examined;
	case EIGLeftoverList::HatchesOpen: return &Leftover.HatchesOpen;
	case EIGLeftoverList::GapsSealed: return &Leftover.GapsSealed;
	case EIGLeftoverList::WallLayers: return &Leftover.WallLayers;
	default: return nullptr;
	}
}

const TArray<FName>* UIGMissingFloorNarrativeSubsystem::LeftoverList(const EIGLeftoverList List) const
{
	return const_cast<UIGMissingFloorNarrativeSubsystem*>(this)->LeftoverList(List);
}

bool UIGMissingFloorNarrativeSubsystem::AddLeftover(const EIGLeftoverList List, const FName Name)
{
	TArray<FName>* Names = LeftoverList(List);
	if (!Names || Name.IsNone() || Names->Contains(Name))
	{
		return false;
	}
	Names->Add(Name);
	return true;
}

bool UIGMissingFloorNarrativeSubsystem::RemoveLeftover(const EIGLeftoverList List, const FName Name)
{
	TArray<FName>* Names = LeftoverList(List);
	return Names && Names->Remove(Name) > 0;
}

bool UIGMissingFloorNarrativeSubsystem::HasLeftover(const EIGLeftoverList List, const FName Name) const
{
	const TArray<FName>* Names = LeftoverList(List);
	return Names && Names->Contains(Name);
}

// -- persistence -----------------------------------------------------------

void UIGMissingFloorNarrativeSubsystem::RestoreSnapshot(
	const FIGMissingFloorNarrativeSnapshot& InSnapshot)
{
	Snapshot = InSnapshot;

	// Read the version before touching anything. Version 0 means the save
	// predates this subsystem: nothing to migrate, just start empty rather
	// than trusting default-constructed records.
	if (Snapshot.SchemaVersion <= 0)
	{
		Snapshot.Truths.Reset();
	}
	if (Snapshot.SchemaVersion < 4)
	{
		// 확장 전 판. 302호 짐 이야기는 그 판이 다음에 여는 밤과 낮부터 붙는다. 이미 망치질을
		// 시작한 밤4 저장은 예전 다섯 번 순서로 끝낸다(새 괴이 교습을 중간에 끼우지 않는다).
		Snapshot.Leftover = FIGLeftoverState();
		Snapshot.Leftover.bLegacyWallSequence = Snapshot.Night.NightIndex >= 4
			&& !Snapshot.Night.bNightFourWallOpened
			&& Snapshot.Night.NightFourWallStrikeCount > 0;
	}

	NormalizeSnapshot();
	// A restore must not announce truths the player learned last session.
	RecomputeConfirmations(/*bBroadcastNewlyConfirmed=*/false);
	Snapshot.SchemaVersion = SnapshotSchemaVersion;
}

void UIGMissingFloorNarrativeSubsystem::ResetNarrative()
{
	Snapshot = FIGMissingFloorNarrativeSnapshot();
	Snapshot.Leftover.Version = 1;
	// The opening voicemail is character memory, not a collectible. Seed it on
	// reset as well as restore so a new-game reset cannot erase knowledge that
	// the prologue has already established before the first playable frame.
	NormalizeSnapshot();
	RecomputeConfirmations(/*bBroadcastNewlyConfirmed=*/false);
	Snapshot.SchemaVersion = SnapshotSchemaVersion;
}

// -- internals -------------------------------------------------------------

void UIGMissingFloorNarrativeSubsystem::RecomputeConfirmations(
	const bool bBroadcastNewlyConfirmed)
{
	TArray<EIGMissingFloorTruth> NewlyConfirmed;
	for (FIGMissingFloorTruthRecord& Record : Snapshot.Truths)
	{
		const IGMissingFloorNarrative::FRule* Rule =
			IGMissingFloorNarrative::FindRuleByTag(Record.TruthTag);
		if (!Rule)
		{
			Record.bConfirmed = false;
			continue;
		}

		// Every category must hold at least one record. Derived, never
		// latched: a save missing its sources loses the truth with it.
		bool bAllCategoriesMet = Rule->Categories.Num() > 0;
		for (const IGMissingFloorNarrative::FCategory& Category : Rule->Categories)
		{
			bool bCategoryMet = false;
			for (const EIGMissingFloorSource Source : Category.AcceptedSources)
			{
				if (Record.SourceIds.Contains(GetSourceId(Source)))
				{
					bCategoryMet = true;
					break;
				}
			}
			if (!bCategoryMet)
			{
				bAllCategoriesMet = false;
				break;
			}
		}

		if (bAllCategoriesMet && !Record.bConfirmed)
		{
			NewlyConfirmed.Add(Rule->Truth);
		}
		Record.bConfirmed = bAllCategoriesMet;
	}

	if (!bBroadcastNewlyConfirmed)
	{
		return;
	}
	for (const EIGMissingFloorTruth Truth : NewlyConfirmed)
	{
		OnTruthConfirmed.Broadcast(Truth);
		if (UWorld* World = GetWorld())
		{
			if (UIGMissingFloorAudioSubsystem* AudioDirector =
				World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
			{
				AudioDirector->PlayTruthConfirmation(
					GetConfirmedTruthCount());
			}
		}
	}
}

void UIGMissingFloorNarrativeSubsystem::NormalizeSnapshot()
{
	TSet<FName> SeenTags;
	for (int32 Index = Snapshot.Truths.Num() - 1; Index >= 0; --Index)
	{
		FIGMissingFloorTruthRecord& Record = Snapshot.Truths[Index];
		const bool bKnown =
			IGMissingFloorNarrative::FindRuleByTag(Record.TruthTag) != nullptr;
		// A tag that is no longer registered arrives here already stripped by
		// ClearInvalidTags, so an empty tag means "dropped by config".
		if (!bKnown || SeenTags.Contains(Record.TruthTag.GetTagName()))
		{
			Snapshot.Truths.RemoveAt(Index);
			continue;
		}
		SeenTags.Add(Record.TruthTag.GetTagName());

		// Drop evidence names this build no longer knows so a downgraded save
		// cannot keep a truth alive through a record that means nothing.
		for (int32 SourceIndex = Record.SourceIds.Num() - 1; SourceIndex >= 0; --SourceIndex)
		{
			bool bRecognized = false;
			// The three P4 provenance rows are real serialized sources too. The
			// previous upper bound stopped at BreakerCutIntervention and silently
			// discarded voicemail/notebook/journal on every restore.
			for (uint8 Raw = 1;
				Raw <= static_cast<uint8>(EIGMissingFloorSource::AnswerRhythmJournal);
				++Raw)
			{
				if (Record.SourceIds[SourceIndex]
					== GetSourceId(static_cast<EIGMissingFloorSource>(Raw)))
				{
					bRecognized = true;
					break;
				}
			}
			if (!bRecognized)
			{
				Record.SourceIds.RemoveAt(SourceIndex);
			}
		}
	}

	// The opening voicemail is guaranteed starting knowledge. Save loading may
	// finish after the night-three fixtures configure, so relying on that actor
	// to register the source creates an order-dependent loss on real resumes.
	// Restoring the baseline here keeps old, empty and current saves equivalent
	// without confirming T9: the reply is still a separate required category.
	const FGameplayTag AnswerTruthTag =
		GetTruthTag(EIGMissingFloorTruth::WaitingForAnAnswer);
	const FName VoicemailSourceId =
		GetSourceId(EIGMissingFloorSource::AnswerRhythmVoicemail);
	if (AnswerTruthTag.IsValid() && !VoicemailSourceId.IsNone())
	{
		FIGMissingFloorTruthRecord* AnswerRecord = FindRecord(AnswerTruthTag);
		if (!AnswerRecord)
		{
			FIGMissingFloorTruthRecord NewRecord;
			NewRecord.TruthTag = AnswerTruthTag;
			AnswerRecord =
				&Snapshot.Truths[Snapshot.Truths.Add(MoveTemp(NewRecord))];
		}
		AnswerRecord->SourceIds.AddUnique(VoicemailSourceId);
	}

	// 이 빌드가 모르는 목격 이름과 중복은 버린다. 문장 하나가 늘어나는
	// 일이라 손해는 작지만, 모르는 이름을 남겨 두면 나중에 그 이름을 다른
	// 뜻으로 다시 쓸 때 옛 저장이 조용히 그 문장을 켠다.
	{
		TSet<FName> SeenWitnesses;
		for (int32 Index = Snapshot.Night.Witnesses.Num() - 1; Index >= 0; --Index)
		{
			const FName Candidate = Snapshot.Night.Witnesses[Index];
			bool bRecognized = false;
			for (uint8 Raw = 1;
				Raw <= static_cast<uint8>(EIGMissingFloorWitness::InspectionCoverMatched);
				++Raw)
			{
				if (Candidate == GetWitnessId(static_cast<EIGMissingFloorWitness>(Raw)))
				{
					bRecognized = true;
					break;
				}
			}
			if (!bRecognized || SeenWitnesses.Contains(Candidate))
			{
				Snapshot.Night.Witnesses.RemoveAt(Index);
				continue;
			}
			SeenWitnesses.Add(Candidate);
		}
	}

	Snapshot.Night.NightIndex = FMath::Clamp(Snapshot.Night.NightIndex, 0, 4);
	Snapshot.Night.AggressionTier =
		FMath::Clamp(Snapshot.Night.AggressionTier, 0, 3);
	Snapshot.Night.CaptureCount = FMath::Max(Snapshot.Night.CaptureCount, 0);
	Snapshot.Night.NightElapsedSeconds =
		FMath::Max(Snapshot.Night.NightElapsedSeconds, 0.0f);

	TArray<FName> NormalizedControls;
	for (const FName ControlId : Snapshot.Night.NightFourControlOrder)
	{
		if (IGMissingFloorNarrative::IsNightFourControl(ControlId))
		{
			NormalizedControls.AddUnique(ControlId);
		}
	}
	Snapshot.Night.NightFourControlOrder = MoveTemp(NormalizedControls);
	Snapshot.Night.NightFourWallStrikeCount = FMath::Clamp(
		Snapshot.Night.NightFourWallStrikeCount,
		0,
		MaxNightFourWallStrikes);
	Snapshot.Leftover.WallStage = FMath::Clamp(Snapshot.Leftover.WallStage, 0, 5);
	if (Snapshot.Night.bNightFourWallOpened)
	{
		Snapshot.Night.NightFourWallStrikeCount = FMath::Max(Snapshot.Night.NightFourWallStrikeCount, 5);
	}
	else if (Snapshot.Leftover.WallStage >= 5)
	{
		// 열리지 않은 벽에 다 뜯은 단계가 남을 수는 없다. 마지막 철물 앞에서 이어 간다.
		Snapshot.Leftover.WallStage = 4;
	}
	if (!IGMissingFloorNarrative::IsEnding(Snapshot.Night.EndingChoice))
	{
		Snapshot.Night.EndingChoice = NAME_None;
	}
	if (!IGMissingFloorNarrative::IsKnownPurchaseProfile(Snapshot.StorePurchaseProfile))
	{
		Snapshot.StorePurchaseProfile = EIGPurchaseProfile::Unset;
	}
}

FIGMissingFloorTruthRecord* UIGMissingFloorNarrativeSubsystem::FindRecord(
	const FGameplayTag& TruthTag)
{
	if (!TruthTag.IsValid())
	{
		return nullptr;
	}
	return Snapshot.Truths.FindByPredicate(
		[&TruthTag](const FIGMissingFloorTruthRecord& Record)
		{
			return Record.TruthTag.MatchesTagExact(TruthTag);
		});
}

const FIGMissingFloorTruthRecord* UIGMissingFloorNarrativeSubsystem::FindRecord(
	const FGameplayTag& TruthTag) const
{
	if (!TruthTag.IsValid())
	{
		return nullptr;
	}
	return Snapshot.Truths.FindByPredicate(
		[&TruthTag](const FIGMissingFloorTruthRecord& Record)
		{
			return Record.TruthTag.MatchesTagExact(TruthTag);
		});
}
