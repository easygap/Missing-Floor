#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "IGMissingFloorNarrativeTypes.generated.h"

/**
 * The ten truths of 없는 층 (STORY_BIBLE_MISSING_FLOOR.md §12).
 *
 * Values are numbered explicitly and must never be renumbered: they are not
 * serialized themselves, but they index the rule table whose source names are.
 */
UENUM(BlueprintType)
enum class EIGMissingFloorTruth : uint8
{
	None = 0,
	/** T1 — the fifth floor was lived in. */
	LivedUpstairs = 1,
	/** T2 — the tenant was Baek Doha. */
	TenantIdentity = 2,
	/** T3 — the pre-dawn noise was him coming home. */
	NoiseWasHomecoming = 3,
	/** T4 — there was a struggle on the landing that morning. */
	LandingStruggle = 4,
	/** T5 — Mok Hansu finished the wall that day. */
	WallSealedThatDay = 5,
	/** T6 — there is a person between the studs. */
	SomeoneInTheWall = 6,
	/** T7 — whatever was in the wall was still alive. */
	WasStillAlive = 7,
	/** T8 — he knocked for five nights and died of thirst. */
	FiveNightsOfThirst = 8,
	/** T9 — he is waiting for an answer. */
	WaitingForAnAnswer = 9,
	/** T10 — Mok Hansu is still covering it up. */
	StillCoveringIt = 10
};

/**
 * Independent evidence records. Names, not values, are serialized, so entries
 * may be appended freely; an unknown name in an old save simply matches no
 * category. Grouped by the truth whose categories they feed.
 */
UENUM(BlueprintType)
enum class EIGMissingFloorSource : uint8
{
	None = 0,

	// T1 — meter cabinet and the reading sheet.
	MeterFifthDial = 1,
	MeterReadingSheet = 2,

	// T2 — shipping labels and the notebook left on five.
	ShippingLabels = 3,
	TunerNotebookName = 4,

	// T3 — the noise forum and his work hours.
	NoiseForumPosts = 5,
	TunerWorkSchedule = 6,

	// T4 — the last post and the mark on the landing.
	ForumFinalPost = 7,
	LandingImpactMark = 8,

	// T5 — the board delivery receipt and the age of the plaster.
	BoardDeliveryReceipt = 9,
	FreshPlasterDating = 10,

	// T6 — the criterion, then the location (patiently by water, or by fist).
	PipeAuditionCriterion = 11,
	PipeWaterComparison = 12,
	WallEchoByHand = 13,

	// T7 — the carbon-copy original and the agent's move-out message.
	CarbonLedgerOriginal = 14,
	AgentMoveOutMessage = 15,

	// T8 — the tally that thins out, and two tonnes of water next door.
	KnockTallyJournal = 16,
	TankWaterAudition = 17,

	// T9 — knowing the rhythm, and being answered.
	AnswerRhythmMaterials = 18,
	AnswerReturned = 19,

	// T10 — the eviction notice and the hand on the breaker.
	EvictionWarning = 20,
	BreakerCutIntervention = 21,

	// P4 clue provenance. Two of these derive AnswerRhythmMaterials; keeping
	// them separate makes the design's two-source promise auditable in saves.
	AnswerRhythmVoicemail = 22,
	AnswerRhythmNotebook = 23,
	AnswerRhythmJournal = 24
};

/**
 * 선택적 목격 (§22.3). 진실을 열지 않고 게이트에도 참여하지 않는다.
 *
 * 확인한 만큼 유담의 독백과 목한수 대치 문장, 엔딩 뉴스 자막이 구체화될
 * 뿐이다. 그래서 여기 있는 것들은 `EIGMissingFloorSource`와 같은 표에
 * 들어가지 않는다 — 한 줄이라도 섞이면 「본 사람만 풀 수 있는 퍼즐」이
 * 되고, 그건 §7의 공정성 약속을 깬다.
 *
 * 수집률·업적·완료 퍼센트는 없다(§23). 무엇을 놓쳤는지도 알려 주지
 * 않는다. 이름으로 직렬화하므로 뒤에 덧붙이는 것은 언제든 안전하다.
 */
UENUM(BlueprintType)
enum class EIGMissingFloorWitness : uint8
{
	None = 0,
	/** 골목 건너편에 서 있던 서일영이 두고 간 약봉투. */
	SeoSleepingPills = 1,
	/** 401호 문 앞 창턱의 물그릇. 황순금이 벽에게 놓아 두던 것. */
	HwangWaterBowl = 2,
	/** 관리실 안쪽 방 문틈에 덧댄 방음재. */
	BoothSoundproofing = 3,
	/** 옥상 물탱크 옆, 눌러 끈 담배 여섯 개비. */
	RooftopCigarettePack = 4,
	/** 관리실 벽 달력. 7월 26일에 동그라미, 그 주 나머지가 비었다. */
	BoothWallCalendar = 5,
	/** CCTV 녹화기의 빈 하드 베이. 채널 5가 남지 않는 진짜 이유. */
	RecorderEmptyBay = 6,
	/** 5층 자재 더미 위의 작업 장갑 한 짝. 큰 손 것이다. */
	AnnexWorkGlove = 7,
	/** 편의점 계산대의 야간 근무표. 나린이 그 새벽마다 서 있었다. */
	StoreNightRoster = 8,
	/** 401호 문 너머 라디오. 황순금이 벽에게 틀어 주던 소리. */
	Unit401DoorRadio = 9,
	/** 402호 문 너머의 공기. 냉장고 험이 없다 — 전기가 끊긴 집이다. */
	Unit402Silence = 10,
	/** 옥상 철문 안쪽의 실제 바람. 「물탱크 바람 소리」의 반증. */
	RoofDoorWind = 11,
	/** 관리실 안쪽 방에서 새는 기계 험. 쓰는 방이라는 뜻이다. */
	BoothInnerRoomHum = 12,
	/** 5층 판재 더미 밑의 녹음기. 서일영이 그 새벽 올라오며 켜 둔 것을 끝까지 들었다. */
	SeoVoiceRecorder = 13,
	/** 관리실 안쪽 방에서 떼어 낸 덮개 조각과 벽에서 움직인 것을 봤다. */
	InnerRoomCoverPieces = 14,
	/** 맞춘 덮개를 5층 배관 점검 자리에 대어 봤다. 떼어 가고 덧판으로 막은 자리다. */
	InspectionCoverMatched = 15
};

/**
 * 편의점 물 세 가지. 계산대 가격표와 봉투가 이 값을 따라간다.
 * 세이브에는 이름으로 들어가므로 이름을 바꾸면 옛 저장에서 고른 물이 사라진다.
 */
UENUM(BlueprintType)
enum class EIGPurchaseProfile : uint8
{
	Unset = 0,
	/** 새벽샘물 500mL 두 병. */
	ProfileA500MlX2 = 1,
	/** 한강수 1L 한 병. */
	ProfileB1LX1 = 2,
	/** 맑은산 2L 두 병. 봉투가 4kg이라 두 손을 쓸 때는 내려놓는다. */
	ProfileC2LX2 = 3
};

/** One truth and the provenance behind it. Confirmation is always derived. */
USTRUCT(BlueprintType)
struct INDIEGAME_API FIGMissingFloorTruthRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	FGameplayTag TruthTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FName> SourceIds;

	/**
	 * Recomputed from SourceIds on every write and on restore. Never set
	 * directly: a truth must not be able to survive without its sources.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bConfirmed = false;
};

/**
 * The hour's runtime facts that must survive a quit (§5.4). The M1 entity and
 * night-loop director keep these in plain actor members, which silently reset
 * the pursuer to tier 0 on resume; this is where they belong instead.
 */
USTRUCT(BlueprintType)
struct INDIEGAME_API FIGMissingFloorNightState
{
	GENERATED_BODY()

	/** 0 = prologue evening, 1..4 = the four nights. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	int32 NightIndex = 0;

	/** The one upstairs grows impatient with every capture; 0..3. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	int32 AggressionTier = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	int32 CaptureCount = 0;

	/** True while 04:30–05:30 holds the building shut. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bTheHourSealed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	float NightElapsedSeconds = 0.0f;

	/** Scripted beats already played once (§8), by author-facing name. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FName> CompletedBeats;

	/** P1..P5 solved, by author-facing name. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FName> SolvedPuzzles;

	/**
	 * 밤4 P5에서 돌린 조작의 순서. 세척 배수 → 부자밸브 우회 → 이송 펌프 순서로 맞게 돌린
	 * 것만 들어간다. 틀린 조작은 경보와 인터록만 남기고 여기 적히지 않는다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FName> NightFourControlOrder;

	/** 벽에 휘두른 망치 수(엉뚱한 데를 친 것 포함, 0..99). 뜯은 단계는 Leftover.WallStage에 따로 있다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	int32 NightFourWallStrikeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bNightFourWallOpened = false;

	/** First report after night 3 and second report after discovery are separate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bFirstReportMade = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bSecondReportMade = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bFifthDawnInterludeCompleted = false;

	/** None, Ending.A, Ending.B or Ending.C. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	FName EndingChoice;

	/**
	 * 확인한 선택적 목격, 이름으로(§22.3). 진행에 어떤 게이트도 걸지
	 * 않으므로 비어 있는 옛 저장은 그대로 유효하다 — 그 회차는 유담이
	 * 아무것도 더 못 본 회차일 뿐이다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FName> Witnesses;
};

/** 「302호에 남은 짐」에서 이름으로 적는 목록. */
UENUM()
enum class EIGLeftoverList : uint8
{
	/** 손에 든 적이 있는 물증과 물건(Piece1..3, Frame, ToolPouch, DohaBox, JeongwooGloves). */
	Taken,
	/** 403호에 가져다 둔 것. */
	Stored,
	/** 살펴본 것. 두고 온 것도 들어간다. 일지와 대사는 이것만 본다. */
	Examined,
	/** 열어 둔 점검구(공간.이름). */
	HatchesOpen,
	/** 덧댄 것이 눌러 막은 틈. 다시 뜯으면 빠진다. */
	GapsSealed,
	/** 밤4에 걷어 낸 벽의 층과 푼 고정점. */
	WallLayers
};

/**
 * 「302호에 남은 짐」(0.10.0 확장). 물증·점검구·302호 작업·정우·밤4 해체의 진행이다.
 * 불안정한 물리 좌표 대신 단계와 이름으로만 적는다. 장면 실행 여부는 CompletedBeats의
 * Leftover.* 비트가 맡는다.
 */
USTRUCT(BlueprintType)
struct INDIEGAME_API FIGLeftoverState
{
	GENERATED_BODY()

	/** 0이면 확장 전 저장을 이어 온 판, 1이면 확장판에서 시작한 판. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	int32 Version = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FName> Taken;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FName> Stored;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FName> Examined;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FName> HatchesOpen;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FName> GapsSealed;

	/** 덮개 조립: 0 안 함, 1 테두리에 다 맞춤, 2 방수막을 들춰 막힌 통풍구까지 봄. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	int32 AssemblyStage = 0;

	/** 조립한 덮개를 5층 원래 자리에 대어 봤다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bSiteMatched = false;

	/** 덧댄 것이 마지막으로 막고 있던 틈(공간.이름). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	FName LiningHeldGap;

	/** 302호 쐐기: None, Brace(맞는 자리), Slipped(미끄러져 빠짐). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	FName WedgeSocket;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bWardrobeMoved = false;

	/** 정우: None, Trapped, Freed, LeftTogether, SelfEscaped. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	FName JeongwooState;

	/** 밤4 해체의 유효 단계 0..5. 실제 망치질 수는 NightFourWallStrikeCount에 따로 있다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	int32 WallStage = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FName> WallLayers;

	/** 5층 보강 프레임을 덧댄 것의 관절에 걸었다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bFramePlaced = false;

	/** 확장 전 판에서 이미 망치질을 시작한 저장. 그 판은 예전 다섯 번 순서로 끝낸다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bLegacyWallSequence = false;
};

/** 끝까지 들은 대사 한 줄. 기록장의 「들은 말」 쪽에 다시 보인다. */
USTRUCT(BlueprintType)
struct INDIEGAME_API FIGHeardLine
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	FText Speaker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	FText Line;

	/** 들은 때. 기록장이 밤과 낮이 바뀌는 자리에 줄을 긋는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	int32 NightIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	bool bAtNight = false;
};

/** 없는 층의 서사 상태 전부. FIGProgressSnapshot에 통째로 실린다. */
USTRUCT(BlueprintType)
struct INDIEGAME_API FIGMissingFloorNarrativeSnapshot
{
	GENERATED_BODY()

	/** 정규화보다 먼저 읽는다. 구조가 바뀌면 이 번호를 보고 옮긴다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	int32 SchemaVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FIGMissingFloorTruthRecord> Truths;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	FIGMissingFloorNightState Night;

	/**
	 * 편의점에서 고른 물. 계산 전에는 다른 병으로 바꿔 들 수 있고, 계산하면
	 * 굳는다. 불러온 뒤 손에 돌아올 병과 보일 봉투를 이것으로 고른다.
	 *
	 * 2026-09-28 전에는 REBIRTH 스냅샷에 들어 있었다. 그 칸을 지웠으므로
	 * 그 전 저장은 고르기 전(Unset)으로 읽힌다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	EIGPurchaseProfile StorePurchaseProfile = EIGPurchaseProfile::Unset;

	/** 「302호에 남은 짐」. 스키마 4에서 생겼다. 그 전 저장은 Version 0으로 읽힌다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	FIGLeftoverState Leftover;

	/** 들은 말. 오래된 줄부터 지운다. 이 칸이 생기기 전 저장은 빈 기록으로 읽힌다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Narrative")
	TArray<FIGHeardLine> Heard;
};
