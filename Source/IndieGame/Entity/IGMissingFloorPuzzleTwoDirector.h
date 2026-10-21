#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Narrative/IGMissingFloorNarrativeTypes.h"
#include "IGMissingFloorPuzzleTwoDirector.generated.h"

class AIGCctvChannelFive;
class UAudioComponent;
class AIGMissingFloorEvidence;
class AIGPrologueWorldScene;
class AIGReadableNote;
class AIGSwingDoor;
class UIGMissingFloorNarrativeSubsystem;

DECLARE_MULTICAST_DELEGATE(FIGPuzzleTwoSolvedSignature);

/**
 * P2 「먹지 원장」 — the night the paper contradicts itself
 * (STORY_BIBLE_MISSING_FLOOR.md §7 P2, §8 밤2).
 *
 * The management booth's fair-copy ledger says "물탱크 배관 소음. 조치
 * 완료." Under it sits the carbon pad, and pressure does not lie: three
 * passes of frottage — each a sustained sound the one upstairs can hear —
 * restore the original complaints, dated after the day the tenant
 * "moved out". Crossed with the estate agent's move-out message, that is
 * T7: whatever was in the wall was still alive.
 *
 * The booth also holds the two beats that need no puzzle: the CCTV monitor
 * whose channel selector has one button too many, and the inner room's
 * door gap lined with egg-crate foam. Neither files evidence — they are
 * the night's images, played once.
 *
 * The booth door is the day/night valve: locked while Mok Hansu keeps his
 * daytime post, open in the hour when he hides in the soundproofed room.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGMissingFloorPuzzleTwoDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGMissingFloorPuzzleTwoDirector();

	/** Spawns the booth door and its contents against a built lobby. */
	bool Configure(AIGPrologueWorldScene* InScene);

	/** Day locks the booth; the hour opens it. */
	void SetHourActive(bool bHourActive);
	/** 목한수가 안쪽 방에서 자고 있다 / 깨어 나서는 중이다. 짐 감독이 그 밤의 단계를 쥐고 있다. */
	bool IsManagerAsleepInside() const;
	bool IsManagerStirring() const;

	/**
	 * 밤2의 목표. 접수철 밑장과 부동산 문자 사본을 둘 다 읽어 날짜의 모순(T7)이 닫히면 한 번
	 * 울린다. 어느 쪽을 먼저 읽어도 된다. 문자 사본은 밤2부터 같은 책상에 있다.
	 */
	FIGPuzzleTwoSolvedSignature OnSolved;

	/** Probe queries. */
	bool ValidateFixtures() const;
	AIGSwingDoor* GetBoothDoor() const { return BoothDoor; }
	AIGMissingFloorEvidence* GetCarbonLedger() const { return CarbonLedger; }
	AIGReadableNote* GetAgentMessageNote() const { return AgentMessageNote; }
	AIGMissingFloorEvidence* GetCctvSelector() const { return CctvSelector; }
	AIGCctvChannelFive* GetCctvChannelFive() const { return CctvChannelFive; }
	AIGMissingFloorEvidence* GetBoothRiserValve() const { return BoothRiserValve; }
	bool IsBoothValveOpen() const { return bBoothValveOpen; }

	/**
	 * 그녀의 폰이 탁자를 떠나 있는가. 밤2 동안과 그 아침에는 현관 바닥에서
	 * 녹음하고, 재생하는 동안은 손에 있다. 폰은 한 대라서 그동안 탁자의 폰
	 * (중고 거래 앱 화면)은 비어 있어야 한다.
	 */
	bool IsPhoneInUse() const { return bPhoneInUse; }
	FSimpleMulticastDelegate OnPhoneInUseChanged;
	/** 아침에 테이프를 손에 들고 듣는 중이다. 그동안 다른 방의 소리를 얹지 않는다. */
	bool IsPhonePlayingBack() const { return bPhoneInHand; }

	/**
	 * §13 「물탱크 바람 소리예요」의 심기. 입주한 다음부터 관리실 문 옆에 관리인이
	 * 붙여 둔 쪽지가 있다. 퇴거 요구서가 붙은 뒤로는 떼어 냈다. 요구서는 낮
	 * 한가운데 붙으므로 그레이박스 감독이 그 뒤에 한 번 더 부른다.
	 */
	void RefreshBoothNotice();
	AIGReadableNote* GetBoothNotice() const { return BoothNotice; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleCarbonRestored(AIGMissingFloorEvidence* Evidence);
	void HandleCctvExamined(AIGMissingFloorEvidence* Evidence);

	/**
	 * §5.5 비트 2-1과 2-6, on one prop. During the hour the phone arms itself
	 * against the door; in the morning the same prop plays the take back and she
	 * hears her own footsteps and exactly as much silence as there was knocking.
	 */
	void HandlePhoneRecorder(AIGMissingFloorEvidence* Evidence);

	/** Keeps the phone's prompt honest about which of the two it is offering. */
	void RefreshPhonePrompt();
	/**
	 * 폰은 밤2의 물건이다. 입주 날이나 밤1에 켜 두면 밤1의 순찰 노크가 테이프를
	 * 먼저 비우고, 아침의 절망(2-6)이 순서를 건너뛰어 먼저 온다.
	 */
	void RefreshPhoneOffer();
	/** 05:30에 신호가 돌아오면 바닥의 폰이 한 번 떤다. 재생하러 오라는 부름이다. */
	void PlayMorningPhoneBuzz();
	/** 재생하는 폰은 손에 들린다. 바닥에서 1.5m 떨어진 스피커는 낮 베드에 묻힌다. */
	FVector GetPhoneInHandLocation(const AIGMissingFloorEvidence* Evidence) const;
	/** 부동산 문자 사본은 늘 관리실 책상에 있다. */
	void RefreshAgentNoteAvailability();
	void HandleFoamExamined(AIGMissingFloorEvidence* Evidence);
	/**
	 * 1층 배관 밸브. 열면 관에 물이 흐르고 그 소리가 책상을 덮는다 — 밸브라는
	 * 동사의 첫 수업. 밤3의 5층 밸브와 밤4의 세 손잡이가 같은 손이다(§7 P2).
	 */
	void HandleBoothValveOpened(AIGMissingFloorEvidence* Evidence);
	/** 낮에 그가 도로 잠근다. 물도 마스킹도 같이 걷힌다. */
	void CloseBoothValve();
	void HandleWallCalendarExamined(AIGMissingFloorEvidence* Evidence);
	void HandleRecorderBayExamined(AIGMissingFloorEvidence* Evidence);
	void HandleInnerRoomListenExamined(AIGMissingFloorEvidence* Evidence);
	UFUNCTION()
	void HandleBoardReceiptsRead(class AIGReadableNote* Note, bool bOpened);
	UFUNCTION()
	void HandleFairCopyRead(class AIGReadableNote* Note, bool bOpened);
	/** §13 밤2 회수. 낮에 본 매물이 관리실 책상에서 돈으로 적혀 있다. */
	UFUNCTION()
	void HandleCashMemoRead(class AIGReadableNote* Note, bool bOpened);
	void HandleTruthConfirmed(EIGMissingFloorTruth Truth);

	UFUNCTION()
	void HandleAgentNoteRead(AIGReadableNote* Note, bool bOpened);

	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPrologueWorldScene> Scene;

	UPROPERTY(Transient)
	TObjectPtr<AIGSwingDoor> BoothDoor;

	UPROPERTY(Transient)
	TObjectPtr<AIGReadableNote> FairCopyLedger;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> CarbonLedger;

	/** §5.5's phone: armed at the door by night, played back in the morning. */
	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> PhoneRecorder;

	bool bPhonePlayedBack = false;
	bool bHourActiveCached = false;
	/** 재생하는 동안 폰은 바닥이 아니라 손에 있다. 다 들으면 탁자로 돌아간다. */
	bool bPhoneInHand = false;
	bool bPhoneInUse = false;
	/** 채널 5가 찢어진 뒤에야 글이 온다. 보는 동안은 읽게 하지 않는다. */
	FTimerHandle CctvThoughtTimer;
	/** 재생의 결론은 공백 뒤에 온다. */
	FTimerHandle PhoneThoughtTimer;
	/** 안쪽 방에서 누가 뒤척였다는 생각은 그 밤에 한 번이다. */
	bool bInnerRoomThoughtSaid = false;
	FTimerHandle PhoneReturnTimer;
	FTimerHandle PhoneBuzzTimer;
	FTimerHandle PhoneBuzzCutTimer;

	UPROPERTY(Transient)
	TObjectPtr<AIGReadableNote> CashMemo;

	UPROPERTY(Transient)
	TObjectPtr<AIGReadableNote> AgentMessageNote;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> CctvSelector;

	/** §14's one cut: the render-target channel behind the fifth button. */
	UPROPERTY(Transient)
	TObjectPtr<AIGCctvChannelFive> CctvChannelFive;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> FoamGap;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> BoothRiserValve;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BoothRiserFlow;

	int32 BoothValveHumHandle = INDEX_NONE;
	bool bBoothValveOpen = false;

	/** T5 첫 번째 출처. 같은 품목이 두 날짜로 두 번 실려 왔다. */
	UPROPERTY(Transient)
	TObjectPtr<class AIGReadableNote> BoardReceipts;

	/** §22.3 선택적 목격 둘. 진실을 열지 않고 문장만 구체화한다. */
	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> WallCalendar;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> RecorderBay;

	/** 같은 문에서 보는 것과 듣는 것. 문틈은 눈이고 이쪽은 귀다. */
	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> InnerRoomListen;
	/**
	 * 문 너머의 기계 험 뒤에 삐걱임과 천 스치는 소리가 온다. 독백은 다 들은
	 * 뒤에 뜬다 — 소리보다 결론이 먼저 오지 않게.
	 */
	FTimerHandle InnerRoomCreakTimer;
	FTimerHandle InnerRoomClothTimer;
	FTimerHandle InnerRoomThoughtTimer;

	/** 관리실 문 옆 쪽지. 목한수가 게임 안에서 처음으로 하는 말이다. */
	UPROPERTY(Transient)
	TObjectPtr<AIGReadableNote> BoothNotice;
	/** 밤4 감독이 요구서를 붙였는가. 쪽지는 그 전까지만 붙어 있다. */
	bool IsEvictionNoticeUp() const;

	FDelegateHandle TruthHandle;
	bool bSolvedAnnounced = false;
};
