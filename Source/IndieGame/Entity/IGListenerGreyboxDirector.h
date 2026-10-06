#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sequence/IGObjectiveProvider.h"
#include "IGListenerGreyboxDirector.generated.h"

class AIGListenerEntity;
class AIGNightLoopDirector;
class AIGNightPhaseDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class UIGNoiseSubsystem;

/**
 * M1 vertical-slice stage for The Missing Floor (-IGListenerGreybox):
 * places the one upstairs in the real 4F corridor of the prologue villa,
 * strings its patrol along the corridor light fixtures, registers the first
 * hum-mask sources and pairs a night-loop director with the player's actual
 * wake pose. No legacy chapter content is altered; without the flag nothing
 * here exists.
 *
 * With -IGListenerGreyboxProbe it also runs the M1 smoke contract from
 * STORY_BIBLE_MISSING_FLOOR.md §4.5/§15: masking swallows sound, one sound
 * investigates, a second sound chases, touch captures, capture resets the
 * night and raises the aggression tier. Logs MISSINGFLOOR_GREYBOX PASS/FAIL
 * and exits with 0/1 for Scripts\Run-MissingFloor-Greybox.bat.
 */
UCLASS()
class INDIEGAME_API AIGListenerGreyboxDirector
	: public AActor
	, public IIGObjectiveProvider
{
	GENERATED_BODY()

public:
	AIGListenerGreyboxDirector();
	virtual void Tick(float DeltaSeconds) override;

	virtual FText GetObjectiveText() const override;
	virtual float GetObjectiveProgress() const override;

protected:
	virtual void BeginPlay() override;

private:
	void TrySetupStage();
	bool SetupStage();
	void StartProbe();
	void AdvanceProbe();
	void FailProbe(const FString& Reason);
	void PassProbe();
	void RequestExit(bool bFailed);
	/** 실제 완주 중 만든 저장으로 다른 프로세스에서 두 결말을 끝까지 확인한다. */
	void AdvanceEndingLifecycleProbe();
	void FinishEndingLifecycleProbe(bool bPassed, const FString& Reason);
	void WriteEndingCheckpointForProbe();
	UFUNCTION()
	void HandleEndingCheckpointSaved(bool bSuccess, FString SlotName);
	void RunArrivalProbe();
	void StartArrivalCapture();
	void AdvanceArrivalCapture();

	/** Emits a synthetic sound near the entity, as the probe's stand-in ear bait. */
	void EmitProbeNoise();

	class UIGMissingFloorNarrativeSubsystem* GetNarrative() const;
	void InitializeArrivalSequence();
	void SpawnArrivalInteractables(UStaticMesh* CubeMesh);
	void SpawnOptionalWitnesses(UStaticMesh* CubeMesh);
	/** 편의점 카운터의 나린. 아는 것이 늘면 하는 말이 달라진다. */
	FText GetNarinCounterLine() const;
	UFUNCTION()
	void HandleUsedListingRead(class AIGReadableNote* Note, bool bOpened);
	UFUNCTION()
	void HandleNeighborhoodDeliveryRead(class AIGReadableNote* Note, bool bOpened);
	void UpdateArrivalSequence();
	void HandleArrivalEvidence(class AIGMissingFloorEvidence* Evidence);
	/** 계약서와 이삿짐 상자 셋을 만지는 손 소리. 처음 여는 상자는 테이프부터 뜯는다. */
	void PlayArrivalHandSound(class AIGMissingFloorEvidence* Evidence, bool bFirstOpen);
	void RequestArrivalAutosave();
	bool AreArrivalBoxesOpened() const;

	/** Central hour-boundary wiring: dormancy, booth lock, day verbs. */
	void HandleHourActiveChanged(bool bActive);

	// -- README/night capture tour (-IGNightCapture) -----------------------
	void StartNightCapture();
	void AdvanceNightCapture();
	void EnterCaptureStep(int32 StepIndex);
	void CaptureTeleportPlayer(const FVector& Location, float Yaw, float Pitch);
	void CaptureParkEntity(const FVector& Location, float Yaw);
	void CaptureShot(const TCHAR* BaseName, bool bShowUI = true) const;
	void CaptureBeginBurst(const TCHAR* DirectoryName, float Seconds);

	// -- §11 V5 밤 구간 히스토그램 (-IGNightHistogram) ----------------------
	//
	// Eight authored night viewpoints, each measured for the fraction of pixels
	// that fall below 5% luminance and above 98%. The design fixes those bands
	// per point because the whole V1 lighting rework is a claim about contrast:
	// darks that fall genuinely black, and no blown highlights anywhere. A
	// screenshot proves neither. Counting pixels does.
	//
	// This is a pre-filter, not the art approval. Ratios inside their band mean
	// the frame is not broken; whether it is *good* still needs eyes.
	void StartHistogramSweep();
	void AdvanceHistogramSweep();
	void EnterHistogramPoint(int32 PointIndex);
	/**
	 * Measures the frame the screenshot pipeline actually captured.
	 *
	 * Reading the viewport back buffer directly returns pure black under
	 * -RenderOffScreen: there is no swap chain to read from, which is exactly
	 * why no window appears. The screenshot delegate hands over the rendered
	 * frame instead, and it is the same path that already writes the capture
	 * tour's PNGs, so it is proven to work headless.
	 */
	void HandleHistogramScreenshot(
		int32 Width,
		int32 Height,
		const TArray<FColor>& Colors);
	/**
	 * Writes one point's PNG from the bitmap that was just measured.
	 *
	 * The viewport client saves a screenshot itself only when nothing is bound to
	 * OnScreenshotCaptured; binding it to count pixels replaces the file write
	 * entirely. So the sweep had been measuring correctly and quietly leaving the
	 * eight frames on disk untouched from whatever build wrote them last.
	 */
	void WriteHistogramFrame(
		const TCHAR* PointName,
		int32 Width,
		int32 Height,
		const TArray<FColor>& Colors) const;
	void HandleNightOneSolved();
	void HandleNightTwoSolved();
	/** §8 비트 2-5: reaching 403 is what ends night two, not confirming T7. */
	void HandleNightTwoReturnedHome();
	void HandleNightThreeSolved();
	/** §8 비트 3-7: only 403's floor ends night three. */
	void HandleNightThreeReturnedHome();
	/**
	 * 대답을 들은 밤3의 05:30, 신호가 돌아오면 신고한다(§1 밤3 이후 현실 대응).
	 * 귀가로 끝났든 시간이 다 됐든 새벽의 일이다. 대답을 못 들은 밤3은 신고할
	 * 것이 없고, 그 밤은 다음 저녁에 되풀이된다.
	 */
	void MakeNightThreeFirstReport();
	void HandleFifthDawnCompleted();
	void HandleNightFourResolved();
	/** §22.3 선택적 목격 셋. 진실도 게이트도 건드리지 않는다. */
	void HandleWaterBowlExamined(class AIGMissingFloorEvidence* Evidence);
	void HandleSleepingPillsExamined(class AIGMissingFloorEvidence* Evidence);
	void HandleCigarettePackExamined(class AIGMissingFloorEvidence* Evidence);
	void HandleStoreRosterExamined(class AIGMissingFloorEvidence* Evidence);
	void HandleUnit401RadioExamined(class AIGMissingFloorEvidence* Evidence);
	void HandleUnit402ListenExamined(class AIGMissingFloorEvidence* Evidence);
	void HandleRoofDoorListenExamined(class AIGMissingFloorEvidence* Evidence);
	/** 소리 목격은 전부 같은 모양이다 — 부피만 세우고 그림은 두지 않는다. */
	class AIGMissingFloorEvidence* SpawnListeningVolume(
		class UStaticMesh* CubeMesh,
		const TCHAR* ActorName,
		const FVector& Location,
		const FVector& Extent,
		const FText& Prompt);
	/** 에필로그가 끝나면 게임은 타이틀로 돌아간다(§9). */
	void HandleEpilogueCompleted();
	void HandleSleepRequested(class AIGMissingFloorEvidence* Evidence);
	/** 눕는다 → 검은 화면 → 침대에서 네시 반. 자는 동안은 보여 주지 않는다. */
	void BeginNightAfterSleep(int32 NightIndex);
	void WakeIntoNight();
	/**
	 * 카드가 걷힌 뒤 밤을 여는 소리. 밤3은 옥상 철문, 밤4는 5층 벽의 드릴이고,
	 * 문 비트를 다 쓴 되풀이 밤2는 403호 천장의 정해진 자리에서 건물이 한 번 운다.
	 */
	void PlayNightOpeningSettle();
	/** 밤3을 여는 옥상 철문. 바람이 부풀고, 닫힌 문짝이 틀 안에서 덜컹인다. */
	void PlayNightThreeRoofGate();
	/** 밤4를 여는 5층 벽의 드릴. 밤1 옥상의 그 드릴이고, 목한수가 먼저 벽을 덮는다. */
	void PlayNightFourMokDrill();
	/** 드릴이 멎은 뒤 5층 바닥의 발소리 넷, 그리고 철문. 부를 때마다 한 걸음. */
	void PlayNightFourMokLeaving();
	/** 그 시간의 공동현관을 밀었다. 한 판에 한 번, 폰이 신호를 못 잡는다. */
	void HandleSealedEntranceTried(class AIGSwingDoor* Door);
	void PlayNoSignal();
	/** 1-7. 밤1을 채운 낮, 401호 문을 등지고 지나가면 황순금이 문틈으로 내다본다. */
	void PollHwangPeek();
	void PlayHwangPeek();
	/**
	 * 문이 열린 뒤의 시각표. 문짝은 움직이지 않으니 돌아보는 순간 그녀가 문을
	 * 닫는다. 대사가 아직이면 닫으면서 한다. 돌아보지 않으면 대사, 닫힘 순서다.
	 */
	void TickHwangPeekScene();
	/** 시선이 401호 문 쪽을 향하고 있다(HwangPeekFacingDot). */
	bool IsFacingHwangDoor() const;
	/**
	 * 밤1 프롤로그 0-5의 「쿵, 쿵, 쿵」. 카드가 걷힌 뒤 침대 위 천장에서 먹먹하게
	 * 난다. 복도의 그는 깨는 순간 첫 칸에서 두드리지 않는다.
	 */
	void PlayNightOneCeilingKnock();
	/** 밤1 프롤로그 0-5의 「드르륵」. 천장 노크 셋이 끝난 뒤 같은 자리에서 끌린다. */
	void PlayNightOneOpeningDrag();
	/** 노크와 끌림이 나는 침대 위 천장의 한 자리. */
	FVector GetNightOneCeilingPoint() const;
	/** 첫 밤, 노크를 들은 뒤 머리맡의 손전등을 켠다. 먼저 켰으면 아무것도 안 한다. */
	void ReachForNightOneTorch();
	/** P1이 맞물린 순간 그가 소리를 좇고 있으면 여유를 두지 않고 새벽으로 간다. */
	bool IsNightOneSolveUnderThreat() const;
	/** 밤1 결론 뒤의 새벽. 포획 암전이나 펼쳐 둔 종이 뒤로는 오지 않는다. */
	void FinishNightOneAtDawn();
	/** 밤1, 옥상에서 전동 드릴이 다섯 번 돌다 멈춘다. 목한수의 첫 흔적. */
	void PlayRoofDriverBeat();
	/** 렌치가 놓이고 독백이 읽힌 뒤에야 화면이 검어진다. */
	void StartEpilogueAfterGesture();
	/**
	 * 신고 뒤의 낮을 문자로 잇는다. 보낸 문자 → 112 접수 → 수십 초 뒤 경찰의
	 * 현장 확인 → 요구서가 붙는다 → 요구서를 읽으면 담당자에게 사진을 보낸다.
	 * 단계는 저장되는 비트로 기억해 불러온 낮에도 빠진 데부터 다시 받는다.
	 * 다음 문자는 대화 줄이 비었을 때만 민다. 진동과 글이 같은 순간이어야
	 * 폰이 울린 것으로 읽힌다.
	 */
	void ScheduleFirstReportTexts(float DelaySeconds);
	void AdvanceFirstReportTexts();
	/** 신호가 돌아오자마자 보내는 문자. 그녀가 무엇을 신고했는지가 화면에 남는다. */
	void PlayFirstReportSent();
	/** 신호가 돌아온 폰에 접수 문자가 온다. 새벽 독백 뒤에. */
	void PlayFirstReportReceipt();
	/** 날이 밝은 5층을 경찰이 보고 간 결과. 전화 대신 문자로 온다(§4.6). */
	void PlayFirstReportSiteCheck();
	/** 문자 한 통에 주머니 속 폰이 운다. */
	void PlayPhoneTextBuzz();
	/** 요구서를 읽었다. 담당자에게 사진을 보내는 문자가 뒤따른다. */
	void HandleEvictionNoticeRead(class AIGMissingFloorEvidence* Evidence);
	void HandleUnit401Knocked(class AIGMissingFloorEvidence* Evidence);
	/** 401호 문 너머의 말. 진행 상태마다 한 줄, 답이 아니라 어디를 볼지. */
	FText GetHwangDoorLine() const;
	/**
	 * 신고 뒤의 낮, 황순금에게 벽의 대답을 전하는 세 줄. 신고 문자가 나갔고
	 * 대화 줄이 비어 있을 때만 민다. 밀었으면 참이다.
	 */
	bool TryPlayHwangPermission();
	/** 탁자 위 폰(중고 거래 앱 화면)을 보일지 다시 정한다. 폰은 한 대다. */
	void RefreshTablePhone();

	UPROPERTY(Transient)
	TObjectPtr<AIGListenerEntity> Entity;

	UPROPERTY(Transient)
	TObjectPtr<AIGNightLoopDirector> NightLoop;

	UPROPERTY(Transient)
	TObjectPtr<AIGNightPhaseDirector> NightPhase;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorPuzzleOneDirector> PuzzleOne;

	UPROPERTY(Transient)
	TObjectPtr<class AIGNightOneBeatDirector> NightOneBeats;

	/** §8 비트 2-1. Owns the door knock, the peephole and the dragging away. */
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorNightTwoBeatDirector> NightTwoBeats;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorPuzzleTwoDirector> PuzzleTwo;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorNightThreeDirector> NightThree;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorFifthDawnDirector> FifthDawn;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorNightFourDirector> NightFour;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEpilogueDirector> Epilogue;

	/** 위층 사람 말고 밤에 오는 것 둘. 어둑시니와 문 밖의 손님. */
	UPROPERTY(Transient)
	TObjectPtr<class AIGNightThreatDirector> NightThreats;

	/** 밤3에 계단을 도는 관리인. */
	UPROPERTY(Transient)
	TObjectPtr<class AIGManagerPatrol> ManagerPatrol;

	/**
	 * §22.3의 선택적 목격 프롭. 어느 것도 진행을 잠그지 않으므로 생성에
	 * 실패해도 스테이지는 유효하다 — ValidateFixtures가 이것들을 묻지 않는
	 * 이유이며, 그 사실 자체가 「없어도 되는 것」이라는 설계의 표현이다.
	 */
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> WaterBowl;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> SleepingPills;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> CigarettePack;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> StoreRoster;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> Unit401Radio;

	/** 소리로만 확인되는 목격 셋. 전부 부피만 있고 그림은 없다. */
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> Unit402Listen;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> RoofDoorListen;

	/**
	 * §13 13행의 심기. 중고 거래 글 「달빛」에 오빠의 공구가 세트로 올라와
	 * 있다. 회수는 밤3의 자재 더미 위, 홀로 남은 조율 렌치다.
	 */
	UPROPERTY(Transient)
	TObjectPtr<class AIGReadableNote> UsedListingNote;
	UPROPERTY()
	TObjectPtr<class AIGReadableNote> NeighborhoodDeliveryNote;
	FTimerHandle NeighborhoodSoundTimer;
	double LastCounterTalkAt = -100.0;
	/** 입주 손 소리의 둘째 겹(내용물)과, 휴대전화 진동을 한 번에서 끊는 타이머. */
	FTimerHandle ArrivalHandSoundTimer;
	FTimerHandle ArrivalPhoneBuzzTimer;
	TWeakObjectPtr<class UAudioComponent> ArrivalPhoneBuzz;
	/** 콜드 오픈(§8 0-1). 검은 화면이 걷히고, 위에서 바퀴가 구른 뒤에 제목이 선다. */
	FTimerHandle ArrivalColdOpenSoundTimer;
	FTimerHandle ArrivalTitleCardTimer;
	/**
	 * 새 게임은 검은 화면으로 연다. 빌라가 지어지는 첫 프레임을 가리고, 무대가
	 * 서면 콜드 오픈이 걷는다. 다른 길로 무대가 섰거나 서지 못하면 바로 걷는다.
	 */
	bool bArrivalOpeningHeldBlack = false;
	void ReleaseArrivalOpeningBlack();
	/** 옥상 자물쇠를 만진 뒤 철문 너머에서 쇠붙이가 내려앉는다(§8 0-4). */
	FTimerHandle ArrivalRoofClinkTimer;
	/**
	 * 첫 저녁의 생활음(§8 0-2). 계단을 내려가는 발소리, 벽 속 배관, 다른 집
	 * 현관문. 밤에 위에서 나는 소리가 어긋나게 들리려면 아래와 옆을 먼저 들어
	 * 둬야 한다. 잠들면 끝난다.
	 */
	FTimerHandle ArrivalBaselineTimer;
	FTimerHandle ArrivalBaselineStepTimer;
	int32 LastArrivalBaselineKind = INDEX_NONE;
	int32 ArrivalBaselineStep = 0;
	uint32 ArrivalBaselineSeed = 0;
	TWeakObjectPtr<class UAudioComponent> ArrivalBaselinePipes[2];
	void ScheduleArrivalBaseline(float MinDelaySeconds, float MaxDelaySeconds);
	void PlayArrivalBaselineEvent();
	void StopArrivalBaseline();
	/** 402호 문 안의 정적을 들은 뒤, 톤이 거의 끝날 무렵의 한 줄. */
	FTimerHandle Unit402VacancyTimer;
	/** 프로브·캡처가 무대를 몰고 있다. 새 지연은 이때 즉시 경로로 간다. */
	bool IsScriptedRun() const;

	/** §20.3's two automatic safety nets: the world moving when nothing else is. */
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorMercyDirector> Mercy;

	/** Day interactions: the bed that ends the day, 401's door that talks. */
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> SleepTarget;

	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> Unit401Door;

	/** 출시 경로의 안전한 입주 저녁 프롤로그 소품. */
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> ArrivalContract;
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> ArrivalParcelBox;
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> ArrivalNotebookBox;
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> ArrivalVoicemailBox;
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> ArrivalStoreBell;
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> ArrivalUnit402Note;
	UPROPERTY(Transient)
	TObjectPtr<class AIGMissingFloorEvidence> ArrivalRoofLock;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPlayerCharacter> Player;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPrologueWorldScene> WorldScene;

	UPROPERTY(Transient)
	TObjectPtr<UIGNoiseSubsystem> NoiseSubsystem;

	/** 404 냉장고 험. 실패한 시도가 남긴 것을 걷어 낼 수 있어야 한다. */
	int32 FridgeHumHandle = INDEX_NONE;

	/** 복도 동쪽 끝 설비 벽장의 험. 위와 같은 이유로 손잡이를 든다. */
	int32 BoilerHumHandle = INDEX_NONE;

	/** 험 둘이 실제로 내는 소리. 마스킹과 같은 반경까지만 들린다. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> FridgeHumLoop;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BoilerHumLoop;

	/**
	 * 밤의 환경 베드 다섯: 4층 복도, 계단실, 5층 별관, 옥상, 1층 로비. 험 반경
	 * 밖이 무음이던 것을 채운다.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> NightAmbienceBeds;
	/** 밤 사이 한 번씩 나는 건물 소리. 밤이 거듭될수록, 05:30이 가까울수록 잦다. */
	FTimerHandle SettleTimerHandle;
	/** 바로 앞에 난 건물 소리의 종류. 같은 것이 연달아 나지 않게 한다. */
	int32 LastSettleKind = INDEX_NONE;
	/** 밤을 여는 손전등 딸깍. 눈을 뜨고 손이 머리맡을 찾는 만큼 늦다. */
	FTimerHandle NightWakeTorchTimer;
	FTimerHandle NightStartTimer;
	FTimerHandle NightSettleTimer;
	FTimerHandle RoofDriverTimer;
	FTimerHandle NightOneDragTimer;
	FTimerHandle NightOneDragFadeTimer;
	/**
	 * 401호 문 너머의 라디오(§8 0-4). 낮과 입주 저녁에는 문 앞에서 낮게
	 * 웅얼거리고 밤에는 꺼져 있다. 두드리면 멎었다가 대답이 끝난 뒤 돌아오고,
	 * 귀를 대면 잠시 커진다. 막간의 예불과 같은 재료다.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Unit401PrayerLoop;
	/** 귀를 댄 뒤 라디오를 제 크기로 돌린다. */
	FTimerHandle Unit401RadioFadeTimer;
	/** 401호 철문 노크의 둘째 손, 그리고 대답이 다 끝난 뒤 돌아오는 라디오. */
	FTimerHandle Unit401KnockTimer;
	FTimerHandle Unit401PrayerReturnTimer;
	/** 라디오를 Level까지 Seconds에 걸쳐 옮긴다. 0이면 멎는다. */
	void FadeUnit401Prayer(float Level, float Seconds);
	/** 지금 시간대에 라디오가 머무는 크기. 밤에는 0이다. */
	float GetUnit401PrayerRestLevel() const;
	/** 밤1 여는 시각표: 천장 노크, 첫 칸의 멈춤 해제, 손전등, 「…4층이 꼭대기인데.」 */
	FTimerHandle NightOneCeilingKnockTimer;
	FTimerHandle NightOneHoldTimer;
	FTimerHandle NightOneTorchTimer;
	FTimerHandle NightOneThoughtTimer;
	/** 밤1 결론 뒤: 위에서 안정기, 「…위에 뭐가 있어.」, 그리고 새벽. */
	FTimerHandle NightOneBallastTimer;
	FTimerHandle NightOneRealizationTimer;
	FTimerHandle NightOneDawnTimer;
	bool bNightOneDawnHeldByNote = false;
	/** 밤2 문 노크 전까지 복도 첫 칸의 그를 붙드는 한도. */
	FTimerHandle NightTwoDoorHoldTimer;
	/** 밤3·4를 여는 소리의 뒷부분(철문의 덜컹, 떠나는 발소리)과 한 줄. */
	FTimerHandle NightOpeningFollowTimer;
	FTimerHandle NightOpeningThoughtTimer;
	int32 NightOpeningStep = 0;
	/** 공동현관을 민 뒤 폰을 꺼내 걸어 보기까지. */
	FTimerHandle NoSignalTimer;
	/** 1-7 황순금의 기척. 401호 앞을 지나는지 보는 폴링과, 문이 열렸다 닫히는 순서. */
	FTimerHandle HwangPeekTimer;
	FTimerHandle HwangPeekSceneTimer;
	double HwangPeekStartedAt = 0.0;
	bool bHwangPeekLineShown = false;
	/** 신고 뒤 첫 노크의 세 줄이 대화 줄이 비기를 기다린다. */
	FTimerHandle HwangPermissionTimer;
	FTimerHandle EpilogueStartTimer;
	FTimerHandle ReportTimer;
	/** 신고 문자 줄기가 대화 줄이 비기를 기다린 시간, 줄이 빈 뒤 더 쉴 시간. */
	float ReportLaneWaitSeconds = 0.0f;
	float ReportPendingGapSeconds = 0.0f;
	/** 문자가 다 오기 전에 누우려 했다. 남은 문자를 짧은 간격으로 받는다. */
	bool bFirstReportRushed = false;
	/** 밤4, 물이 돌기 전에 5층 벽 앞에 섰는지 본다. 한 번 말하면 멎는다. */
	FTimerHandle NightFourWallCoverTimer;
	void PollNightFourWallCover();
	/** 저장된 그 시간을 이어 붙이는 중이다. 되풀이가 아니라서 여는 드릴을 다시 걸지 않는다. */
	bool bResumingSealedHour = false;
	int32 PendingNightIndex = 1;
	int32 SettleCounter = 0;
	void SpawnNightAmbienceBeds();
	void ScheduleNextSettle();
	void PlaySettleEvent();

	/**
	 * 지난 시도가 만들다 만 것을 치운다.
	 *
	 * 스폰에 이름을 지정하므로 같은 이름이 살아 있으면 다음 시도의 스폰이
	 * 실패한다. 치우지 않으면 재시도가 영영 통과하지 못한 채 6초 뒤
	 * 「월드 씬이나 플레이어가 없다」로 끝난다.
	 */
	void DestroyPartialStage();

	enum class EProbeStep : uint8
	{
		Inactive,
		AudioVisualContract,
		/** §20.2 tuning table, §20.4 difficulty modes, §5.6 noise heatmap. */
		DifficultyContract,
		/** §10.4 reverb grammar and the §11 V1 airborne-dust world model. */
		PerceptionContract,
		/** The torch beam actually populating and thickening over his lane. */
		BeamDustContract,
		PuzzleOneContract,
		SealContract,
		MaskingContract,
		InvestigateOnFirstSound,
		ChaseOnSecondSound,
		CaptureOnTouch,
		ResetAfterCapture,
		/** §20.3's two automatic safety nets: the reset hint and the 90 s clock. */
		MercyNetContract,
		/** §5.5 기록되지 않는 시간: the recording rule and its one exception. */
		RecordingRuleContract,
		Night1SightingStage,
		Night1SightingRestore,
		Night1Extinguisher,
		DayNightCycle,
		/** §8 비트 2-1: the knock at 403's own door, and what it puts on tape. */
		NightTwoDoorBeatContract,
		PuzzleTwoContract,
		/** §14 CCTV 채널 5: the one-shot render target's whole life cycle. */
		CctvChannelContract,
		/** §8 비트 2-5: leaving the booth drops the stack and starts the chase. */
		NightTwoReturnChaseContract,
		/** §8 비트 2-5: only 403's floor ends night two. */
		NightTwoHomeContract,
		DayTwoContract,
		NightThreeContract,
		/** §8 비트 3-7: the learned answer, knocked at nothing, reaching him. */
		AnswerReachContract,
		AnswerReachCadence,
		AnswerPairTap,
		AnswerFinalTap,
		AnswerContract,
		/** §8 비트 3-7: the learned answer stops him in her corridor. */
		NightThreePassContract,
		/** §8 비트 3-7: only 403's floor ends night three. */
		NightThreeHomeContract,
		/** §24 즉시 차단 19: the sealed hour turns F9 and the journal down. */
		SealedHourUiContract,
		NightFourContract,
		NightFourFailureRetryContract,
		NightFourWallContract,
		NightFourEndingContract,
		/** §35: 선택 뒤의 87초와, 진행을 잠그지 않는 목격 넷. */
		EpilogueContract,
		Done
	};

	EProbeStep ProbeStep = EProbeStep::Inactive;
	float StepDeadlineSeconds = 0.0f;
	float SetupRetrySeconds = 0.0f;
	int32 FailureRetryCaptureCountBefore = 0;
	bool bNightFourFailureRetryVerified = false;
	/** Latch so the mercy step fires its nets once and then waits for the paper. */
	bool bMercyNetsFired = false;

	/** §8 비트 3-7's cadence walk: the probe has to leave real gaps between taps. */
	double AnswerReachTapTwoAt = 0.0;
	int32 AnswerReachTapsSent = 0;
	bool bAnswerReachWasDormant = false;

	/**
	 * §14 CCTV 채널 5. The structural half of the contract runs anywhere: the
	 * channel must own nothing before the press, allocate one CIF target for the
	 * beat, cross the low shape through it, and release everything when it dies.
	 * The pixel half needs a real RHI and is measured only when asked for, because
	 * a scene capture under NullRHI returns black and black passes any floor —
	 * which is exactly how the §11 V5 sweep first fooled itself.
	 */
	int32 CctvCapturesAtLive = 0;
	int32 CctvCapturesAtDeath = 0;
	bool bCctvLiveSoundHeard = false;
	bool bCctvFeedProbeRequested = false;
	bool bCctvFeedMeasured = false;
	float CctvFeedBrightestLuma = 0.0f;
	float CctvFeedLitFraction = 0.0f;
	void MeasureCctvFeed(const class AIGCctvChannelFive* Channel);

	/** Capture-tour state; inert unless -IGNightCapture is on the command line. */
	bool bNightCaptureRequested = false;
	/** §11 V5 sweep; inert unless -IGNightHistogram is on the command line. */
	bool bHistogramRequested = false;
	/** Reports measurements without enforcing bands, for authoring them. */
	bool bHistogramReportOnly = false;
	int32 HistogramPointIndex = -1;
	float HistogramPointSeconds = 0.0f;
	int32 HistogramFailures = 0;
	int32 HistogramMeasured = 0;
	/** Set while a point's screenshot is in flight, so the timer stands down. */
	bool bHistogramShotPending = false;
	float HistogramShotWaitSeconds = 0.0f;
	FDelegateHandle HistogramScreenshotHandle;
	FTimerHandle HistogramTimer;
	bool bMercyNoteProbeRequested = false;
	bool bCaptureMetricsOnly = false;
	int32 CaptureStepIndex = -1;
	float CaptureStepSeconds = 0.0f;
	bool bCaptureBurstActive = false;
	FString CaptureBurstDirectory;
	int32 CaptureBurstFrame = 0;
	float CaptureBurstEndsAt = 0.0f;
	float CaptureBurstAccumulator = 0.0f;
	/** One-shot latches for timed actions inside the current capture step. */
	bool bCaptureActionADone = false;
	bool bCaptureActionBDone = false;
	bool bCaptureActionCDone = false;
	FTimerHandle CaptureTimer;
	FTimerHandle NightCaptureWarmupTimer;
	FVector ProbeNoiseLocation = FVector::ZeroVector;
	FVector ExpectedWakeLocation = FVector::ZeroVector;
	/** Trail sample the beam-dust step lit up, so it can be cleaned up after. */
	FVector ProbeDustTrailLocation = FVector::ZeroVector;
	/** Measured P3 wall discrimination, for the PASS receipt. */
	float ProbeHollowRingLevel = 0.0f;
	float ProbeHollowRingSeconds = 0.0f;
	float ProbeSolidRingSeconds = 0.0f;
	bool bStageReady = false;
	bool bProbeRequested = false;
	bool bArrivalProbeRequested = false;
	bool bArrivalCaptureRequested = false;
	int32 ArrivalCaptureStep = 0;
	void AdvanceReadingReview();
	/** 트레일러 촬영(-IGTrailerCapture). 장면마다 카메라를 옮기며 UI 없는 프레임을 한 장씩 남긴다. */
	bool AdvanceTrailerCapture();
	int32 TrailerShotIndex = 0;
	int32 TrailerShotFrame = 0;
	int32 TrailerNight = 0;
	int32 TrailerDroppedFrames = 0;
	TWeakObjectPtr<class ACameraActor> TrailerCamera;
	int32 ImmersionReviewFailures = 0;
	bool bProductionMode = false;
	FTimerHandle SetupTimer;
	FTimerHandle ArrivalCaptureTimer;
	FTimerHandle ProbeTimer;
	FString EndingProbeChoice;
	FString EndingCheckpointSlot;
	double EndingProbeStartedAt = 0.0;
	double EndingChoiceStartedAt = 0.0;
	double EndingEpilogueStartedAt = 0.0;
	double EndingEpilogueSeconds = 0.0;
	double EndingVigilSeconds = 0.0;
	int32 EndingLifecycleStep = 0;
	bool bEndingCheckpointWrite = false;
	bool bEndingProfileProbe = false;
	bool bEndingCheckpointRequested = false;
	bool bEndingLifecycleDone = false;
	bool bEndingVigilObserved = false;
	bool bEndingSevenSecondsObserved = false;
	bool bEndingRestored = false;
};
