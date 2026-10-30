#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGMissingFloorLeftoverDirector.generated.h"

/** 남은 짐 줄기의 대사 사건 이름. 사건이 넘어가면 그 이름의 떠 있는 줄과 밀린 줄을 거둔다. */
namespace IGLeftoverDialogue
{
	/** 판 뒤에 갇힌 정우의 부름과 그에 대한 반응. 판이 풀리면 거둔다. */
	inline const FName Trapped(TEXT("Leftover.Rescue.Trapped"));
	/** 판이 풀려 밀고 나오겠다는 말. 이 말이 끝난 뒤에 정우가 판을 민다. */
	inline const FName Released(TEXT("Leftover.Rescue.Released"));
	/** 꺼낸 직후 정우의 인사. */
	inline const FName Freed(TEXT("Leftover.Rescue.Freed"));
	/** 첫 당기기에 상자가 걸렸다는 말. 상자가 빠지면 거둔다. */
	inline const FName BoxStuck(TEXT("Leftover.Box.Stuck"));
	/** 정우가 판을 받치는 동안의 말. 받치기를 그만두면 거둔다. */
	inline const FName Brace(TEXT("Leftover.Brace"));
	/** 문을 잡고 기다린다는 말. 정우가 내려가면 거둔다. */
	inline const FName DoorWait(TEXT("Leftover.DoorWait"));
	/** 쐐기가 미끄러졌다는 말. 제자리에 물리면 거둔다. */
	inline const FName Wedge(TEXT("Leftover.Wedge"));
	/** 덮개가 안 맞는 까닭. 조각이 자리에 물리면 거둔다. */
	inline const FName Fit(TEXT("Leftover.Assembly.Fit"));
	/** 5층 덧판에 덮개를 대 보고 하는 말. 이 말이 떠 있는 동안 덮개를 덧판 앞에 든다. */
	inline const FName SiteMatch(TEXT("Leftover.Annex.SiteMatch"));
	/** 살피거나 집어 든 물건에 대한 속말. 다른 것을 또 살피면 앞 말을 거둔다. */
	inline const FName Examine(TEXT("Leftover.Examine"));
	/** 밤2 관리인이 깼다(숨어야 한다). 손수레 심부름을 나가면 거둔다. */
	inline const FName Wake(TEXT("Leftover.N2.Wake"));
	/** 밤2 손수레가 돌아온다(나가야 한다). 심부름이 끝나면 거둔다. */
	inline const FName CartBack(TEXT("Leftover.N2.CartBack"));
}

struct FIGDialogueScope;
class AIGAccessHatch;
class AIGJeongwooFigure;
class AIGLeftoverTarget;
class AIGLiningEntity;
class AIGListenerEntity;
class AIGManagerPatrol;
class AIGMissingFloorNightTwoBeatDirector;
class AIGNightLoopDirector;
class AIGNightPhaseDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGReadableNote;
class AIGSwingDoor;
class UIGMissingFloorNarrativeSubsystem;
class USpotLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
enum class EIGCaptureKind : uint8;

/**
 * 「302호에 남은 짐」(0.10.0). 수거 공지와 옮겨지는 짐, 관리실 안쪽 방, 302호의 정우, 떼어 낸
 * 덮개(P6), 남겨 둘 틈(P7), 셋째 괴이 덧댄 것의 자리를 한곳에서 쥔다.
 *
 * 흐름(낮은 NightIndex 기준, 밤 N 다음 낮이 NightIndex N이다).
 * - 낮1: 1층에 「대형 폐기물 수거 안내」가 붙는다. 날짜를 줄 긋고 목요일 아침으로 고쳤다.
 *   정우가 관리실 안쪽 방의 「주인 없는 짐」을 302호로 나르다 3층 방화문에서 막힌다. 문을
 *   잡아 주면 같이 302호로 가고, 상자 하나에서 오빠 이름을 본다. 정우가 그 상자를 따로 둔다.
 * - 밤2: 장부를 맞춘 뒤(P2) 안쪽 방에서 목한수가 깬다. 빈 손수레를 가지러 필로티로 나간
 *   사이 안쪽 방에 들어간다. 작업대와 벽 방수막의 덮개 조각 둘, 정우의 장갑 주머니, 두
 *   점검구와 벽에 접힌 덧댄 것. 손수레 소리가 돌아오기 전에 나와야 한다. 관리실을 나서면
 *   예전처럼 귀환 추격이 시작된다.
 * - 낮2: 장갑 주머니를 보여 주면 정우가 302호 짐을 새벽 네 시쯤 빼러 오겠다고 한다.
 * - 밤3: 정우가 상자를 빼다 04:30에 갇혔다. 덧댄 것이 북벽 작업 틈을 누르며 마감판을 밀어
 *   길을 막았다. 쐐기로 판을 받치고, 장롱을 밀어 큰 점검구를 열면 덧댄 것이 옮겨 가고 길이
 *   열린다. 정우가 판을 받치는 동안 오빠 상자를 빼낼 수도 있다. 남은 덮개 조각과 금속 틀이
 *   열린 길 옆 마대에 있다. 302호 상자 뚜껑에서 덮개를 맞추고 5층 원래 자리에 대 본다.
 * - 낮3: 필로티 적치 자리가 비었다. 수거차가 새벽에 다녀갔다.
 * - 밤4: 덧댄 것은 5층에 있다. 벽 해체 단계는 밤4 감독이 이 감독의 5층 함수를 부른다.
 *
 * 저장은 서사 스냅샷의 Leftover(FIGLeftoverState)와 Leftover.* 비트다. 좌표는 적지 않는다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGMissingFloorLeftoverDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGMissingFloorLeftoverDirector();

	bool Configure(
		AIGPrologueWorldScene* InScene,
		AIGPlayerCharacter* InPlayer,
		AIGListenerEntity* InListener,
		AIGNightLoopDirector* InNightLoop,
		AIGNightPhaseDirector* InNightPhase,
		AIGMissingFloorNightTwoBeatDirector* InNightTwoBeats,
		AIGManagerPatrol* InManager);

	/** 그 시간이 열리거나 닫혔다(GBD가 부른다). 공간마다 문·괴이·사람을 다시 놓는다. */
	void SetHourActive(bool bActive);

	/**
	 * 밤2 P2가 풀렸다. 안쪽 방이 깨는 장면을 맡으면 true. false면 부르는 쪽이 예전처럼 곧장
	 * 귀환 추격을 건다(관리인이 없거나, 이미 한 밤이거나, 옛 저장의 이어 하기).
	 */
	bool BeginNightTwoInnerRoom();
	/** 둘째 밤, 목한수가 관리실 안쪽 방에서 자고 있다(깨기 전). 문 너머 뒤척이는 소리의 근거다. */
	bool IsManagerAsleepInInnerRoom() const;
	/** 둘째 밤, 목한수가 깨어 안쪽 방을 나서는 중이다(깨는 소리부터 심부름이 끝날 때까지). */
	bool IsManagerStirring() const { return NightTwoStage == ENightTwo::Waking || NightTwoStage == ENightTwo::Errand; }

	/**
	 * 낮의 정우(오토바이)에게 말을 걸었다. 이 감독이 맡는 줄이면 띄우고 true. 예전 줄
	 * (소개·손님 이야기·걸쇠)은 부르는 쪽이 띄운다. bKnown은 이름을 들은 회차인가.
	 */
	/** SayLine을 주면 그 줄로 말한다(말 건 자리를 떠나면 끊기는 낮 대화의 범위를 부르는 쪽이 정한다). */
	bool TryJeongwooTalk(bool bKnown, bool bGuestTalkDone,
		const TFunction<void(const FText&, const FText&)>& SayLine = nullptr);

	/** 지금 정우나 관리인 장면이 돌고 있다. 그동안 다른 괴이는 오지 않는다. */
	bool IsSceneActive() const;
	static bool IsAnySceneActive(const UWorld* World);
	/** 05:30이 와도 끝까지 둘 일이 있다. 조립대에서 손에 든 덮개 조각, 진행 중인 장면. */
	static bool IsHoldingDawn(const UWorld* World);
	/** 힌트용: 그녀가 302호 조립대 곁에 있다. */
	bool IsPlayerAtAssemblyTable() const;
	/** 힌트용: 그녀가 5층 배관 점검 자리(덧판) 앞에 있다. */
	bool IsPlayerAtInspectionSite() const;
	/**
	 * 검사용. 이 밤 302호를 불러온 직후처럼 되돌리고(판은 처음 자리, 단계는 Idle) 곧바로 한 번 살핀다. 서사 기록은
	 * 그대로다. 기다리면 그사이 관리인이 문간의 플레이어를 보고 쫓아와 뒤 검사가 엉킨다.
	 */
	void SimulateStorageReloadForTesting() { StorageStage = EStorage::Idle; SetBoardPose(0, false); PollStorage(); }
	/** 힌트용: 그녀가 3층 302호 안이나 문 앞 복도에 있다. */
	bool IsPlayerNearStorage() const;
	/** 힌트용: 그녀가 지금 302호 방 안에 있다(판과 덧댄 것이 눈에 드는 자리). */
	bool IsPlayerInStorageRoom() const { return StorageRoomBoxContainsPlayer(); }
	/** 셋째 밤 갇힌 정우가 있는 302호에 들어가 판과 그 너머 구석을 본 적이 있다. 힌트가 이것을 보고 말을 고른다. */
	static const FName SawStorageRoomBeat;
	/**
	 * 힌트용 조립 단계. -1 맞출 것이 없음, 0 조각을 맞추는 중, 1 다 맞춤, 2 뒤집음, 3 막을 들춤,
	 * 4 덮개를 챙겼고 아직 5층 덧판에 대 보지 않음(덧판이 붙어 있을 때만).
	 */
	int32 GetAssemblyHintStage() const;
	/**
	 * 그녀가 실제로 본 자리. 대사와 힌트를 고를 때만 쓰고 진행 조건으로 쓰지 않는다. 저장에 남고,
	 * 그 자리를 이미 다룬 저장(대 보기, 벽 단계, 틀 세움)은 본 것으로 친다.
	 */
	static const FName SeenAnnexInspectionBeat;
	static const FName SeenBraceFrameBeat;

	// -- 밤4 벽 해체가 부르는 5층 덧댄 것(IGMissingFloorLeftoverAnnex.cpp) -----------
	/** 5층 벽에 놓는다(밤4 시작, 불러오기, 재도전). 지난 단계면 그 단계의 자리에 곧장 놓는다. */
	void PrepareAnnexLining(int32 WallStage, bool bBracePlaced);
	/** 아직 5층에 놓이지 않았으면 지금 저장의 단계대로 놓는다. */
	void EnsureAnnexLining();
	/** 1단계: 이음 테이프가 뜯겨 덧판 이음매가 들떴다. 덧댄 것이 그 틈으로 간다. */
	void OpenAnnexSeam();
	/** 2단계: 남쪽 덧판을 떼어 배관 점검구가 열렸다. 덧댄 것이 옮겨 가 벽과 천장 사이로 몸을 편다. */
	void OpenAnnexInspection();
	/** 천장까지 다 폈다. 이때부터 철골 틀로 무릎 마디를 걸 수 있다. */
	bool IsAnnexSpread() const;
	/** 철골 틀 위 런너가 무릎 마디를 걸었다. 2단계의 마무리다. */
	void PinAnnexKnee();
	/** 3단계: 바깥층이 떨어져 새 틈이 났다. 걸린 몸이 버티며 틀을 민다. */
	void StrainAnnexLining();
	/** 4단계: 마지막 판을 열기 전, 틀과 함께 작업할 자리에서 밀려난다. bInstant면 곧장 그 자리다. */
	void PushOutAnnexLining(bool bInstant);
	/** 벽이 열렸다. 그 뒤로는 돌아오지 않는다. */
	void HideAnnexLining();
	/** 5층 덧댄 것이 움직이는 중이다. 그동안 다른 포획은 오지 않는다. */
	bool IsAnnexLiningBusy() const;
	/** 덧판 자리(배관 점검구)가 아직 덮여 있다. */
	bool IsInspectionOverlayOn() const;
	/** 철골 틀이 무릎 마디를 거는 자리. 밤4 감독이 틀을 여기에 맞춰 세운다. */
	static FVector GetAnnexKneePin();
	AIGLiningEntity* GetLining() const { return Lining; }

	/** 검사용. */
	AIGSwingDoor* GetInnerDoor() const { return InnerDoor; }
	AIGReadableNote* GetCollectionNotice() const { return CollectionNotice; }
	AIGReadableNote* GetCabinetLedger() const { return CabinetLedger; }
	AIGJeongwooFigure* GetJeongwoo() const { return Jeongwoo; }
	AActor* GetHandCart() const { return Cart; }
	AIGAccessHatch* GetHatch(FName Id) const;
	AIGLeftoverTarget* FindTarget(FName Key) const;
	/** 검사용. 세운 소품 하나(없으면 null). */
	UStaticMeshComponent* GetPropForTesting(const FName Key) const { return Props.FindRef(Key); }
	FName GetStorageStage() const;
	/** 검사용: 정우가 판 앞에 서서 두 손에 힘을 실었다(이때부터 상자를 당길 수 있다). */
	bool IsBraceHeld() const;
	/** 검사용: 판 뒤에 엎어 둔 정우의 폰 손전등이 켜 있다. */
	bool IsPhoneTorchOn() const;
	USpotLightComponent* GetPhoneTorchForTesting() const { return PhoneTorch; }
	bool IsInnerRoomErrandRunning() const;
	/**
	 * 검사용: 깨어나는 안쪽 방 장면을 접고 곧장 귀환 추격을 건다. 비트는 남기지 않는다(겪지 않은
	 * 장면이다). 회색 상자 계약이 예전 2-5를 그대로 보게 한다. 안쪽 방은 -IGLeftoverProbe가 본다.
	 */
	bool SkipNightTwoInnerRoomForTesting();
	/** 검사용: 낮1 짐 장면을 곧바로 시작한다(계단 3층 아래 참에 정우를 세운다). */
	void StartDayOneBoxesForTesting();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** 세운 액터(덧댄 것, 정우, 문, 점검구, 손대는 자리, 손수레, 공지)를 같이 거둔다. */
	virtual void Destroyed() override;

private:
	// -- 공통 -------------------------------------------------------------------
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;
	int32 GetNightIndex() const;
	bool IsHourActive() const;
	bool IsLegacyRun() const;
	bool HasTaken(FName Item) const;
	void Take(FName Item);
	bool HasExamined(FName Item) const;
	void MarkExamined(FName Item);
	bool PlayBeat(FName Beat);
	bool HasBeat(FName Beat) const;
	/** 셋째 밤 302호에서 정우 말에 붙일 이름. 아직 이름을 못 들었으면 「302호 안」이다. */
	const FText& StorageVoice() const;
	void Say(const FText& Speaker, const FText& Line, float MinSeconds = 0.0f);
	void Think(const FText& Line, float Seconds = 3.6f);
	void Caption(const FText& Line, const FVector& Where, float Seconds = 2.4f);
	/** 사건 이름을 붙인 대사. bStory면 상황이 바뀌어도 남는 이야기 대사, 아니면 지금 행동에 대한 짧은 반응이다. */
	void SayIn(FName Context, const FText& Speaker, const FText& Line, bool bStory = false);
	void ThinkIn(FName Context, const FText& Line, float Seconds = 3.6f, bool bStory = false);
	/**
	 * 살피거나 집어 든 물건에 대한 속말. 플레이어가 한 일의 대답이라 떠 있는 말을 잠깐 미루고 바로 뜬다. 다른 것을
	 * 또 살피면 앞 말은 거둔다. 여럿을 잇달아 살피면 속말이 줄을 서서 십여 초 뒤에 뜨거나 버려졌다.
	 */
	void ThinkAboutItem(const FText& Line, float Seconds = 3.6f);
	/** 지금 해야 할 일을 알리는 속말(숨어야 해, 나가야 해). 바로 뜨고, StillValid가 깨지면(상황이 지나면) 거둔다. */
	void ThinkUrgent(FName Context, const FText& Line, float Seconds, TFunction<bool()> StillValid);
	void CaptionIn(FName Context, const FText& Line, const FVector& Where, float Seconds = 2.4f);
	/**
	 * 말이 화면에서 끝나면(또는 거둬지면) Then을 부른다. 화면이 없는 실행이나 큐가 막힌 때를 위해 FallbackSeconds 뒤에도
	 * 한 번 부른다. Then은 한 번만 불린다.
	 */
	void SayThen(FName Context, const FText& Speaker, const FText& Line, float FallbackSeconds, TFunction<void()> Then);
	/**
	 * Say·SayIn·SayThen이 대사를 내는 곳. 녹음이 있으면 유담의 말은 머리에서, 정우의 말은 그 몸에서 나고,
	 * 판 뒤에 갇혀 있는 동안은 판이 고역을 먹는다.
	 */
	void Speak(const FText& Speaker, const FText& Line, FIGDialogueScope Scope, float MinSeconds);
	/** 그 사건의 떠 있는 줄과 밀린 줄, 소리 자막을 거둔다. */
	void Hush(FName Context);
	void PlaySound(USoundBase* Sound, const FVector& Where, float Volume, float Pitch = 1.0f, float Inner = 120.0f, float Outer = 1400.0f);
	void PhoneBuzz();
	FVector GetPlayerFeet() const;
	bool IsPlayerNear(const FVector& Where, float Radius, float MaxHeight = 200.0f) const;
	AIGLeftoverTarget* SpawnTarget(FName Key, const FVector& Location, const FRotator& Rotation, const FVector& Extent,
		const FText& Prompt, float HoldSeconds, float Noise);
	UStaticMeshComponent* SpawnProp(FName Key, UStaticMesh* Mesh, const FTransform& Transform, bool bCollision);
	void ShowProp(FName Key, bool bShow);
	void SetTargetLive(AIGLeftoverTarget* Target, bool bLive);
	void RefreshAll();
	void HandleGripped(AIGLiningEntity* Entity);
	void HandleCaptureSettled(EIGCaptureKind Kind, const FVector& Spot);
	void HandleLiningSealed(AIGLiningEntity* Entity, FName GapId);
	void HandleLiningTarget(AIGLiningEntity* Entity, FName GapId);
	void HandleLiningFirstMove(AIGLiningEntity* Entity);
	void HandleHatchOpened(AIGAccessHatch* Hatch);
	void HandleHatchClosed(AIGAccessHatch* Hatch);
	void HandleHatchReopened(AIGAccessHatch* Hatch);
	FName HatchId(const AIGAccessHatch* Hatch) const;
	/** 공간 하나의 틈들을 덧댄 것에 다시 준다. */
	void LoadGapsFor(FName Space);
	void PlaceLining();
	void SpreadAnnexLining(float Seconds);
	void PollPlayer();

	// -- 안쪽 방(관리실) -----------------------------------------------------------
	void BuildInnerRoom();
	void RefreshInnerRoom();
	void LockInnerDoor(bool bLocked);
	void SetBoothHooksEnabled(bool bEnabled);
	void PlayInnerRoomWake();
	void StartManagerErrand();
	void HandleErrandOutside();
	void HandleErrandReturning();
	void HandleErrandFinished(bool bCaught);
	void ArmNightTwoReturn(bool bWithThought);
	void PollNightTwo();
	void HandleInnerTarget(AIGLeftoverTarget* Target);
	void PlaceCoverPieceOnWall();
	UFUNCTION()
	void HandleLedgerRead(AIGReadableNote* Note, bool bOpened);
	UFUNCTION()
	void HandleNoticeRead(AIGReadableNote* Note, bool bOpened);

	// -- 낮 ------------------------------------------------------------------
	void BuildDayProps();
	void RefreshDay();
	void PollDayOneBoxes();
	void BeginDayOneBoxes();
	void HandleDayOneArrived();
	void HandleDayTarget(AIGLeftoverTarget* Target);
	void FinishDayOneBoxes();
	void RefreshLoadingArea();

	// -- 302호(밤3) ----------------------------------------------------------------
	void BuildStorage();
	void RefreshStorage();
	void PollStorage();
	void BeginStorageTrap();
	void HandleStorageTarget(AIGLeftoverTarget* Target);
	void HandleStorageTap(AIGLeftoverTarget* Target);
	void SetBoardPose(int32 Pose, bool bAnimate);
	/** 판(과 그 밑에 끼인 상자)을 옮기는 60 Hz 타이머. 다 옮기면 스스로 멈춘다. */
	void TickBoardMove();
	/** 판이 다 돈 뒤 정우가 구석에서 걸어 나온다. */
	void WalkJeongwooOut();
	void TryPlaceWedge(int32 Socket);
	void PushWardrobe();
	void TickWardrobeMove();
	void ReleaseBoard();
	/** 「판이 풀렸어요」가 끝나기를 기다린다. 화면이 그려지지 않으면 읽었을 시간 뒤에 꺼낸다. */
	void WatchReleaseLine();
	void FreeJeongwoo();
	void JeongwooBraceForBox();
	void PullDohaBox();
	void JeongwooLeave();
	/** 받치던 판을 놓는다(그녀가 자리를 떴거나 오래 안 당겼다). 정우는 다시 문을 잡으러 간다. */
	void JeongwooStopBracing();
	/** 판 앞에 선 정우가 두 손에 힘을 싣는다. 판이 한 번 삐걱하고, 그때 당기라고 한다. */
	void JeongwooTakeHold();
	/** 판을 밀어 낸 정우가 그 자리에 쪼그려 숨을 고른다. 다 고르면 일어나 걸어 나온다. */
	void JeongwooCatchBreath();
	/** 판 밑 상자를 당긴 만큼 미끄러뜨린다. bTakeAtEnd면 다 빠진 뒤 숨긴다(그녀가 챙겼다). */
	void SlideDohaBox(float Distance, float Seconds, bool bTakeAtEnd);
	void TickBoxSlide();
	/** 5층 덧판에 대 본 덮개를 몇 초 들고 있다가 벽 밑에 기대 둔다. */
	void TickSiteHold();
	/** 정우가 먼저 계단으로 내려간다. bTogether면 같이 나온 것이다. */
	void JeongwooGoDownstairs(bool bTogether);
	/** 손에 들고 있던 조각을 조립대 옆에 내려놓는다(자리를 떴거나 새벽이 왔다). */
	void PutDownHeldPiece();
	void SelfEscapeAtDawn();
	void UpdateJeongwooArrival();
	void HandleJeongwooArrived();
	bool StorageRoomBoxContainsPlayer() const;

	// -- 덮개 조립(P6) ------------------------------------------------------------
	void BuildAssembly();
	void RefreshAssembly();
	void HandleAssemblyTarget(AIGLeftoverTarget* Target);
	void HandleAssemblyTap(AIGLeftoverTarget* Target);
	void HoldPiece(int32 Piece);
	void ApplyHeldTransform();
	void TryFit(int32 Slot);
	void SeatPiece(int32 Piece);
	void SetClamp(int32 Clamp, int32 Pose);
	void FinishAssemblyFlip();
	void LiftMembrane();
	void TickMembraneLift();
	void TakeAssembledCover();
	/** 5층 구석 덧판과 기대 둔 철골 틀을 그녀가 봤는지 본다(손전등이 비추는 시야 안, 가려지지 않음). */
	void PollSightings();
	/** 403호 책상에 둔 오빠 공구 주머니. 가져와 둔 회차에만 놓이고 살펴볼 수 있다. */
	void RefreshHomePouch();
	void HandleHomePouch(AIGLeftoverTarget* Target);
	/** 정우가 빼다 403호 문 앞에 둔 오빠 상자(셋째 밤에 같이 못 뺐을 때). */
	void RefreshDeliveredBox();
	void HandleDeliveredBox(AIGLeftoverTarget* Target);
	FTransform PieceHoverTransform(int32 Piece) const;
	void HandleSiteMatch(AIGLeftoverTarget* Target);

	UPROPERTY(Transient) TObjectPtr<AIGPrologueWorldScene> Scene;
	UPROPERTY(Transient) TObjectPtr<AIGPlayerCharacter> Player;
	UPROPERTY(Transient) TObjectPtr<AIGListenerEntity> Listener;
	UPROPERTY(Transient) TObjectPtr<AIGNightLoopDirector> NightLoop;
	UPROPERTY(Transient) TObjectPtr<AIGNightPhaseDirector> NightPhase;
	UPROPERTY(Transient) TObjectPtr<AIGMissingFloorNightTwoBeatDirector> NightTwoBeats;
	UPROPERTY(Transient) TObjectPtr<AIGManagerPatrol> Manager;
	UPROPERTY(Transient) TObjectPtr<AIGLiningEntity> Lining;
	UPROPERTY(Transient) TObjectPtr<AIGJeongwooFigure> Jeongwoo;
	UPROPERTY(Transient) TObjectPtr<AIGSwingDoor> InnerDoor;
	UPROPERTY(Transient) TObjectPtr<AIGSwingDoor> BoothDoor;
	UPROPERTY(Transient) TObjectPtr<AActor> Cart;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<AIGAccessHatch>> Hatches;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<AIGLeftoverTarget>> Targets;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UStaticMeshComponent>> Props;
	/** 보일 때 몸을 막는 소품(만들 때 정한다). */
	TSet<FName> BlockingProps;
	UPROPERTY(Transient) TObjectPtr<AIGReadableNote> CollectionNotice;
	UPROPERTY(Transient) TObjectPtr<AIGReadableNote> CabinetLedger;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> PropRoot;
	/**
	 * 정우의 폰 손전등. 판 뒤 구석 바닥에 화면을 아래로 엎어 두어 천장을 비춘다. 갇혀 있는 동안과
	 * 풀려나 숨을 고르는 동안 켜 있고, 일어나며 폰을 집어 든다.
	 */
	UPROPERTY(Transient) TObjectPtr<USpotLightComponent> PhoneTorch;
	/** 덮개 조립대의 틀(누운 채 뒷면이 위)과 고정쇠 넷, 조각 셋. */
	UPROPERTY(Transient) TObjectPtr<USceneComponent> FrameRoot;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Clamps;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> PieceMeshes;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> FlapMeshes;

	FName CurrentSpace;
	bool bHourActive = false;
	bool bConfigured = false;
	FTimerHandle PollTimer;
	FTimerHandle SceneTimer;
	FTimerHandle WakeTimer;
	/** 셋째 날 아침 정우의 문자. 장면 타이머와 따로 둔다(다른 장면이 그 타이머를 다시 걸면 문자가 사라졌다). */
	FTimerHandle MorningTextTimer;
	/** 쐐기가 장판에서 미끄러진 횟수. 두 번째부터 「또 빠졌어」라고 한다. */
	int32 WedgeSlips = 0;
	FTimerHandle BoardTimer;
	FTimerHandle JeongwooWalkTimer;
	FTimerHandle WardrobeTimer;
	FTimerHandle MembraneTimer;
	FTimerHandle FitFeedbackTimer;
	FDelegateHandle CaptureSettledHandle;

	// 밤2
	enum class ENightTwo : uint8 { Idle, Waking, Errand, Done };
	ENightTwo NightTwoStage = ENightTwo::Idle;
	bool bReturnArmed = false;
	bool bLiningGripPending = false;

	// 낮1
	enum class EDayOne : uint8 { Idle, Climbing, AtDoor, Walking, Unloading, Talking, Done };
	EDayOne DayOneStage = EDayOne::Idle;
	double DayOneStageAt = 0.0;
	bool bDayOneDoorHeld = false;

	// 302호(밤3)
	enum class EStorage : uint8 { Idle, Trapped, Released, Fallen, Freed, Bracing, Leaving, Gone };
	EStorage StorageStage = EStorage::Idle;
	double StorageStageAt = 0.0;
	/** 판 자세: 0 몸에 눌린 채 기댐, 1 받친 채 기댐, 2 미끄러져 더 누움, 3 비켜 둠. */
	int32 BoardPose = 0;
	FTransform BoardFrom;
	FTransform BoardTo;
	FTransform BoxFrom;
	FTransform BoxTo;
	double BoardMoveStart = 0.0;
	float BoardMoveSeconds = 0.0f;
	bool bWedgeInHand = false;
	/** 문을 잡고 있다가 다시 불려 와 「판 잡을게요」를 이미 했다. 판 앞에 서면 짧게만 말한다. */
	bool bBraceAgainSpoken = false;
	/** 판 앞에 선 정우가 손에 힘을 실었다. 그 전에는 상자를 당길 수 없고 당기라는 말도 하지 않는다. */
	bool bBraceHeld = false;
	/** 구출 뒤 숨을 다 고르고 일어났다. 그 전에는 받쳐 달라고 부를 수 없다(쪼그린 채 판 뒤에 있다). */
	bool bJeongwooUp = false;
	FTimerHandle BraceTimer;
	int32 DohaBoxPulls = 0;
	/** 판 밑에서 끌려 나오는 상자. 놓인 자리(밀린 판 밑)에서 당긴 쪽으로 미끄러진다. */
	FTimerHandle BoxSlideTimer;
	/** 덧판에 덮개를 대 보기 시작한 시각(아니면 -1). */
	FTimerHandle SiteHoldTimer;
	double SiteHoldStart = -1.0;
	/** 대 본 덮개를 내려놓기 시작한 시각(아니면 -1). */
	double SiteSetDownStart = -1.0;
	/** 대 본 지 1.8초에 보이는 것을 한 번 말한다. 장면 타이머는 다음 장면이 다시 걸어서 따로 둔다. */
	bool bSiteLinePending = false;
	FVector BoxSlideFrom = FVector::ZeroVector;
	FVector BoxSlideTo = FVector::ZeroVector;
	double BoxSlideStart = -1.0;
	float BoxSlideSeconds = 0.0f;
	bool bBoxSlideTake = false;
	FVector WardrobeFrom = FVector::ZeroVector;
	double WardrobeMoveStart = -1.0;

	// 조립
	int32 HeldPiece = INDEX_NONE;
	int32 HeldRotation = 0;
	bool bHeldFlipped = true;
	int32 PieceRotation[3] = {2, 1, 3};
	bool bPieceFlipped[3] = {true, true, true};
	int32 WrongFits = 0;
	double FitFeedbackUntil = 0.0;
	int32 FitFeedbackPiece = INDEX_NONE;
	double MembraneLiftStart = -1.0;
};
