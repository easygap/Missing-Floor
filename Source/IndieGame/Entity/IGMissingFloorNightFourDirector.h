#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGMissingFloorNightFourDirector.generated.h"

class AIGMissingFloorEvidence;
class AIGPrologueWorldScene;
class AIGListenerEntity;
class AIGMissingFloorFifthDawnDirector;
class APawn;
class UAudioComponent;
class UIGMissingFloorNarrativeSubsystem;
class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE(FIGNightFourResolvedSignature);

/**
 * Night 4 vertical slice for 없는 층 (story bible v2.2 §7 P5, §8 night 4).
 *
 * The hydraulic mask is one physically named cleaning circuit, not four
 * arbitrary valves: roof cleaning drain -> float-valve bypass -> ground-floor
 * transfer pump. First activation order is persisted, while every order can
 * still reach the same running state. Five separate hammer interactions then
 * remove the real middle gypsum panel. The two final targets change only the
 * mourning choice; discovery and the second report remain common state.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGMissingFloorNightFourDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGMissingFloorNightFourDirector();
	virtual void Tick(float DeltaSeconds) override;

	bool Configure(AIGPrologueWorldScene* InScene);
	void SetHourActive(bool bHourActive);
	bool ValidateFixtures() const;

	AIGMissingFloorEvidence* GetEvictionNotice() const { return EvictionNotice; }
	/**
	 * 요구서를 붙인다. 자리는 경찰의 현장 확인 문자가 온 뒤의 낮이다
	 * (§8 3-8·3-9). 그레이박스 감독이 신고 문자 줄기 끝에서 부른다.
	 * bAnnounce면 403호 안에 있을 때만 종이 소리를 낸다. 한 번만 붙고
	 * 저장에 남는다.
	 */
	void PostEvictionNotice(bool bAnnounce);
	bool IsEvictionNoticePosted() const;
	/**
	 * 요구서 붙이는 소리를 들려줄 자리인가. 403호 안이다. 문 하나 너머에서
	 * 종이 소리를 듣고 나가 보게 한다. 복도에 서 있을 때 붙이면 보이지 않는
	 * 사람이 곁에서 종이를 붙이는 꼴이 된다.
	 */
	static bool CanHearEvictionPosting(const FVector& ListenerLocation);
	AIGMissingFloorEvidence* GetCleaningDrain() const { return CleaningDrain; }
	AIGMissingFloorEvidence* GetFloatBypass() const { return FloatBypass; }
	AIGMissingFloorEvidence* GetTransferPump() const { return TransferPump; }
	AIGMissingFloorEvidence* GetWallBreakTarget() const { return WallBreakTarget; }
	/**
	 * 벽이 열리는 자리. 그레이박스 프로브가 여기 마스킹을 재는데,
	 * 좌표를 프로브 쪽에 적어 두면 벽을 옮겼을 때 옛 자리를 재면서
	 * 통과한다.
	 */
	static FVector GetWallBreakLocation();
	/** 401호 안. 황순금이 벽 너머로 대답하는 자리이고, 막간의 7월 29일도 여기서 온다. */
	static FVector GetUnit401ReplyLocation();
	AIGMissingFloorEvidence* GetEndingATarget() const { return EndingATarget; }
	AIGMissingFloorEvidence* GetEndingBTarget() const { return EndingBTarget; }

	bool WasHydraulicAlarmTriggered() const { return bHydraulicAlarmTriggered; }
	/** 렌더 재질에 반영된 전원·운전·고장등 비트. 순회 검사에서 실제 출력을 읽는다. */
	int32 GetPumpLampMask() const;
	/**
	 * 막간 「다섯 번째 새벽」은 여기서 돈다. 벽이 열리고 오빠를 본 직후 눈을
	 * 감기고, 다섯 새벽을 산 뒤 눈을 뜨면 공동 너머의 노크와 목한수가 온다.
	 * 밤3 아침의 브리핑이던 것이 본 것의 결과가 된다(§8 막간).
	 */
	void SetFifthDawn(AIGMissingFloorFifthDawnDirector* InFifthDawn);
	/** 막간이 끝났다. 그레이박스 감독이 OnCompleted를 여기로 넘긴다. */
	void HandleInterludeCompleted();
	bool IsAwaitingInterlude() const { return bAwaitingInterlude; }
	bool IsWaterMaskPlaying() const;
	bool IsFinalRevealPlaying() const { return bFinalRevealActive; }
	bool IsFinalConfrontationComplete() const
	{
		return bFinalConfrontationComplete;
	}
	int32 GetFinalRevealStage() const { return FinalRevealStage; }

	/**
	 * Places the authored finale meshes for the repository's screenshot tour.
	 * This changes no narrative, puzzle, save or AI state.
	 */
	void SetFinaleCapturePreview(bool bShowCavity, bool bShowMok);

	/** Tier-3 capture during night 4 uses the same common-discovery state. */
	bool ResolveFailureEnding();

	/**
	 * The §20.4 substitute route to ending C: dawn arrived on night four with
	 * the wall still closed. 듣기만 하는 밤 has no captures, so the tier never
	 * reaches three and the ordinary route above can never fire — without this
	 * the accessibility mode would be missing an ending, and §20.5 requires all
	 * three to stay reachable in every mode.
	 */
	bool ResolveDawnFailureEnding();
	/**
	 * 부작용 없는 같은 판정. 새벽 디렉터가 눈을 감을지 정하기 전에 묻는다 —
	 * 실패의 새벽은 매물 화면이 제 암전을 가지고 오므로 새벽이 따로 감기지 않는다.
	 */
	bool WouldResolveDawnFailureEnding() const;

	/** Shared tail of both ending-C routes: capture record, staging, cards. */
	bool CommitFailureEnding(bool bRecordCapture);

	/** 엔딩 C 동안 상호작용을 소비하고, 카드가 완전히 열린 뒤에만 재시도한다. */
	bool RequestFailureRetry();
	bool IsFailureEndingActive() const { return bFailureEndingActive; }
	bool IsFailureRetryEnabled() const { return bFailureRetryEnabled; }
	/** 무화면 출시 계약에서 엔딩 카드 타이머만 완료 상태로 진행한다. */
	bool CompleteFailurePresentationForProbe();

	/**
	 * 엔딩 B의 기다림에서 노크 한 번을 받는다. 캐릭터의 Knock()이 맨 앞에서
	 * 부르고, 참이면 소리는 이 안에서 이미 났다. 기다림 밖에서는 거짓이라
	 * 보통 노크가 그대로 간다.
	 */
	bool RegisterVigilKnock();
	/** 저장·이어하기 검사는 기다림을 생략하지 않고 현재 상태만 읽는다. */
	bool IsEndingBVigilActive() const { return bEndingBVigilActive; }

	FIGNightFourResolvedSignature OnResolved;

	/**
	 * 가면 구간에 어둑시니나 철문 밖의 손님에게 잡혔고 공격성이 3에 닿았다. 위층 사람에게
	 * 잡힌 것과 같은 결말 C다. 밤 루프 감독이 부른다.
	 */
	void HandleMaskExternalCapture(APawn* Player) { HandleNightFourCapture(Player); }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class AIGGameplayRealismProbe;
	void HandleEvictionNotice(AIGMissingFloorEvidence* Evidence);
	void HandleCleaningDrain(AIGMissingFloorEvidence* Evidence);
	void HandleFloatBypass(AIGMissingFloorEvidence* Evidence);
	void HandleTransferPump(AIGMissingFloorEvidence* Evidence);
	void HandleWallStrike(AIGMissingFloorEvidence* Evidence);
	/** 선택이 열리는 프레임에 한 번. 양쪽을 다 보는 값을 낮춘다(§22.4). */
	/**
	 * §22.3. 목한수 앞에서 유담이 무엇을 말할 수 있는가는 그 회차에 무엇을
	 * 봤느냐로 정해진다. 아무것도 못 봤으면 아무 말도 하지 않는다 — 없는
	 * 말을 쥐여 주지 않는 것이 이 절의 규칙이다.
	 *
	 * 최대 세 줄이다. 그 이상은 목격이 아니라 목록 낭독이 된다. 앞은 그를
	 * 겨냥하는 서류이고 **마지막 줄은 가능하면 사람**이다 — 증거로 시작해
	 * 사람으로 끝나야 이 장면이 고발이 아니라 애도가 된다.
	 */
	void BuildConfrontationReplyLines(TArray<FText>& OutLines) const;

	void RequestEndingChoiceAutosave();
	void HandleEndingA(AIGMissingFloorEvidence* Evidence);
	void HandleEndingB(AIGMissingFloorEvidence* Evidence);
	void HandleNightFourCapture(APawn* Player);
	void BeginFailureListing();
	void EnableFailureRetry();
	void ResetAfterFailureEnding();
	void ActivateControl(FName ControlId, AIGMissingFloorEvidence* Evidence);
	/** 순서가 틀렸다. 어느 쪽이 틀렸느냐로 소리가 갈리고, 인터록이 선다. */
	void HandleControlMisorder(FName ControlId, AIGMissingFloorEvidence* Evidence);
	void ReleaseControlLockout();
	void StartWaterMaskIfReady();
	bool BuildFinaleVisuals();
	void SetCavityRevealVisible(bool bVisible);
	void SetMokVisible(bool bVisible);
	FRotator GetMokRotation() const;
	void UpdateFinaleDetailLayers();
	void BeginCavityReveal();
	void UpdateCavityReveal(float DeltaSeconds);
	void AdvanceCavityReveal();
	void BeginSilenceBeat();
	void BeginInterludeInsideTheWall();
	void ResumeAfterInterlude();
	void PlayDistantReply();
	void PresentMokHansoo();
	void BeginEntityPass();
	void TriggerBlackout();
	void CompleteConfrontation();
	/**
	 * §5.5 녹음. 망치를 들기 전에 폰 녹음을 켠다(이미 돌고 있으면 그대로).
	 * 벽이 열리는 순간 규칙이 풀리므로, 그 뒤의 소리가 처음으로 담긴다.
	 */
	void StartWallRecording();
	/** §8 4-5b. 대치가 끝난 정적 속에서 폰을 본다. 녹음이 처음으로 무언가를 담았다. */
	void PlayRecordingLiftBeat();
	void ResetFinaleTimers();
	/**
	 * 이 밤이 아직 살아 있는가. 05:30에 눈이 감기기 시작하면 끝난 밤이다.
	 * 세계가 낮으로 바뀌는 SetHourActive(false)는 다 감긴 뒤(0.48초)라서,
	 * 그 사이에 터지는 연출 타이머와 선택은 새벽 디렉터를 보고 물러선다.
	 */
	bool IsHourLive() const;
	/** 벽을 연 채 되풀이된 밤4에서, 플레이어가 공동 앞에 다시 서면 리빌을 잇는다. */
	void PollRepeatedReveal();
	void UpdateMokRetreat(float DeltaSeconds);
	void UpdateEndingHammer(float DeltaSeconds);
	void UpdateRoofValveMotion(float DeltaSeconds);
	void RefreshPresentation();
	void FinishEnding(FName EndingId);
	/**
	 * 엔딩 B 「곁에 앉아 대답한다」의 기다림(§9 B). 앉아서 둘-쉬고-하나를
	 * 두드리면 복도 끝에서 대답 둘이 온다. 05:30에 공동현관 잠금이 풀리고
	 * 신고 연결음이 나면 그때 결말을 알린다. 프로브는 기다리지 않는다.
	 */
	bool BeginEndingBVigil();
	void PlayVigilReply();
	void BeginVigilDawn();
	void PlayVigilLatch();
	void PlayVigilRingback();
	void EndEndingBVigil();
	/** 5타 뒤 잠깐. 손전등이 죽었다 살아나는 사이에 판이 내려앉는다. */
	void OpenWallAfterFinalStrike();
	void SettleWallDust();
	/**
	 * 3타 뒤 잠깐. 목한수가 한 층 아래 계단 들머리에서 차단기를 내린다.
	 * 딸깍이 계단실로 올라오고 머리 위 등이 튀며 꺼진다. 망치와 같은
	 * 프레임에 꺼지면 원인이 내 망치로 읽힌다.
	 */
	void CutAnnexPower();
	/** 내린 사람이 계단을 내려간다. 발소리 넷이 멀어진다. */
	void PlayPowerCutStep();
	/**
	 * 넷째 타 뒤 잠깐(EXPANSION_PLAN §2.3 넷째 밤). 5층 철문 밖에서 오빠 목소리가 부른다.
	 * 괴이 감독의 손님이 맡는다. 부름이 왔든 못 왔든 벽 안의 대답 시각을 잡는다.
	 */
	void BeginHammerCallAtDoor();
	/** 벽 안에서 둘, 쉬고, 하나. 밤3의 대답과 같은 손이다. 이때 망치를 다시 든다. */
	void PlayWallAnswerBetweenStrikes();
	/** 인터록이 선 동안 배관이 잦아드는 소리. 세 번 울고 조용해진다. */
	void PlayControlSettleKnock();
	/** P5가 연 물길. 물 베드가 켜지고 꺼지는 자리마다 같이 부른다. */
	void SetRiserFlowPlaying(bool bPlay, float FadeSeconds = 0.0f);
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPrologueWorldScene> Scene;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGListenerEntity> Listener;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGMissingFloorFifthDawnDirector> FifthDawn;
	/** 엔딩 C 암전과 이동 잠금을 걷는다. 정상 복귀가 끊긴 자리에서만 부른다. */
	void AbortFailureBlackout(const TCHAR* Reason);

	TWeakObjectPtr<class AIGPlayerCharacter> FailurePlayer;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> EvictionNotice;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> CleaningDrain;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> FloatBypass;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> TransferPump;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> WallBreakTarget;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> EndingATarget;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> EndingBTarget;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> WaterMaskBed;

	/** Non-interactive plumbing and pump parts that keep P5 physically legible. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> EquipmentVisuals;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PumpSelector;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> PumpLamps;

	/** 세척 회로의 순서표. 회로를 만든 사람이 펌프 선택반 옆에 붙여 둔 것. */
	UPROPERTY(Transient)
	TObjectPtr<class AIGReadableNote> ProcedureSheet;

	/** Four separate material groups, all authored 3D and sharing one origin. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CavityRevealVisuals;

	/** Workwear, head/hands and gypsum board; never a near-field sprite. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> MokVisuals;

	/** Lit masked detail over the continuous cavity shadow/silhouette meshes. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CavityDetailCard;

	/** Face/jacket only; the 95 cm board, lower body and shadow remain 3D. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> MokDetailCard;

	/** 생성 통짜 메시가 잡혔는지. 카드와 조각 수 계약이 여기에 따라 갈린다. */
	bool bCavityFigureAuthored = false;
	bool bMokFigureAuthored = false;

	/** 유해·목한수 표현이 빠짐없이 세워졌는지. 생성 메시와 절차 셸의 계약이 다르다. */
	bool HasFinaleFigures() const;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> EndingHammerVisual;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> EndingPhoneVisual;

	bool bHourCurrentlyActive = false;
	bool bWaterMaskActive = false;
	bool bHydraulicAlarmTriggered = false;
	bool bResolvedBroadcast = false;
	bool bFinalRevealActive = false;
	/** 벽이 열린 뒤 첫 1초가 지나 두 번째 완전 침묵(§10.2 ②)을 걸었는지. */
	bool bRevealSilenceApplied = false;
	bool bFinalConfrontationComplete = false;
	bool bMokRetreatActive = false;
	bool bEndingHammerMoving = false;
	bool bValveMotionActive = false;
	float ValveAngles[2] = {0.f, 0.f};
	float ValveTargetAngles[2] = {0.f, 0.f};
	FTransform ValveClosedTransforms[2];
	bool bCavityPresentationVisible = false;
	bool bMokPresentationVisible = false;
	bool bFailureEndingActive = false;
	bool bFailureRetryEnabled = false;
	bool bControlLockoutActive = false;
	bool bEvictionAnnounced = false;
	bool bAwaitingInterlude = false;
	int32 FinalRevealStage = INDEX_NONE;
	float RevealAttentionSeconds = 0.0f;
	float RevealStageElapsedSeconds = 0.0f;
	float MokRetreatSeconds = 0.0f;
	float EndingHammerSeconds = 0.0f;
	int32 WaterMaskHumHandle = INDEX_NONE;
	FTimerHandle DistantReplyTimer;
	FTimerHandle MokRevealTimer;
	FTimerHandle EntityPassTimer;
	FTimerHandle BlackoutTimer;
	FTimerHandle BlackoutRestoreTimer;
	FTimerHandle FailureListingTimer;
	FTimerHandle FailureRetryTimer;
	FTimerHandle ControlLockoutTimer;
	FTimerHandle InterludeTimer;
	/** 벽을 연 채 되풀이된 밤4. 공동 앞에 다시 설 때까지 리빌을 미룬다. */
	FTimerHandle RepeatRevealPollTimer;
	/** 넘치는 탱크의 출렁임은 루프 파형이다. 경보 독백만큼 울리고 걷는다. */
	TWeakObjectPtr<UAudioComponent> OverflowSloshVoice;
	FTimerHandle OverflowSloshFadeTimer;

	/** 엔딩 B의 기다림. 선택한 순간부터 신고 연결음까지. */
	bool bEndingBVigilActive = false;
	bool bVigilDawnReached = false;
	int32 VigilReplyCount = 0;
	TArray<double> VigilTapTimes;
	/** 탭마다 석고 녹음을 돌려 친다. 1.2초 안의 세 탭이 한 녹음이면 손이 아니라 기계다. */
	uint32 VigilTapSerial = 0;
	TWeakObjectPtr<class AIGPlayerCharacter> VigilPlayer;
	FTimerHandle VigilReplyTimer;
	FTimerHandle VigilDawnTimer;

	/** 5타를 친 뒤 판이 실제로 내려앉기 전까지. 이 사이에는 더 칠 수 없다. */
	bool bWallOpeningPending = false;
	FTimerHandle WallOpenTimer;
	FTimerHandle WallDustTimer;
	/** 대치가 끝나고 폰을 보기까지(§8 4-5b). 먼저 고르면 걷힌다. */
	FTimerHandle RecordingLiftTimer;

	/** 3타의 정전. 도는 동안에는 RefreshPresentation이 먼저 불을 끄지 않는다. */
	FTimerHandle PowerCutTimer;
	FTimerHandle PowerCutStepTimer;
	int32 PowerCutStepIndex = 0;
	/** 4타에 401호가 답하기까지. 망치 꼬리가 걷힌 뒤에 온다. */
	FTimerHandle Unit401ReplyTimer;
	/** 넷째 타 뒤 손이 저려 잠깐 못 친다. 그 사이 철문 밖의 부름과 벽 안의 대답이 온다. */
	bool bHammerHeldForCall = false;
	FTimerHandle HammerCallTimer;
	FTimerHandle WallAnswerTimer;
	FTimerHandle WallAnswerHitTimers[2];
	/** 인터록이 선 배관. 틀린 손잡이가 있던 자리에서 잦아든다. */
	FTimerHandle ControlSettleTimer;
	int32 ControlSettleIndex = 0;
	FVector ControlSettleLocation = FVector::ZeroVector;

	/** 관리실 토출관과 4층 설비 벽장의 수류. 물 베드와 같이 돌고 같이 멎는다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> RiserFlowBeds;

	/** 목한수가 물러서며 딛는 발. 시간이 아니라 움직인 거리로 센다. */
	FVector MokLastStepLocation = FVector::ZeroVector;
	float MokStepDistance = 0.0f;
	int32 MokStepIndex = 0;
	/** 댄 줄이 아직 화면에 있어 그의 통과를 미룬 횟수. */
	int32 EntityPassWaits = 0;
};
