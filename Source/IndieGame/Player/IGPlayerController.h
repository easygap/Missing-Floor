#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFramework/PlayerController.h"
#include "IGPlayerController.generated.h"

class UAudioComponent;
class UInputMappingContext;
class UIGAccessibilitySubsystem;
class UIGSaveGame;
class UIGSaveSubsystem;
struct FInputKeyEventArgs;

enum class EIGSystemMenuMode : uint8
{
	Hidden,
	Title,
	Pause,
	AudioCalibration,
	DisplaySettings,
	Credits,
	/**
	 * §24 즉시 차단 12의 광과민 항목과 「플레이 전에 알아 두실 것」을 실제
	 * 화면으로 세운 자리. 첫 실행에 한 번 뜨고, 무엇이 나오는지와 그것을
	 * 어느 설정으로 줄일 수 있는지를 같은 화면에서 말한다.
	 */
	ContentNotice,
	/** §19.8 키 재설정. 설정 화면에서 들어가고 그리로 돌아간다. */
	KeyBindings
};

/** Owns local-player input context setup and future player-facing UI coordination. */
UCLASS()
class INDIEGAME_API AIGPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AIGPlayerController();
	bool IsUsingGamepadForHud() const { return bUsingGamepadForHud; }
	/** Re-pushes menu state after the native HUD is constructed or replaced. */
	void RefreshMenuHud() const;

	/**
	 * §9 에필로그가 끝난 자리에서 타이틀을 연다.
	 *
	 * 엔딩 뒤에는 이어서 걸어 다닐 세계가 없다. 일시정지가 아니라 타이틀인
	 * 이유도 그것이다 — 엔딩 B를 본 세이브라면 「밤 5」 행이 여기서 처음
	 * 보인다.
	 */
	void ShowTitleAfterEnding();
	/** 타이틀에 실제로 노출되는 추가 밤과 메뉴 항목의 상태. */
	bool IsNightFiveAvailable() const { return bNightFiveAvailable; }
	bool IsSystemMenuRowEnabled(int32 Row) const;

	/**
	 * §24 즉시 차단 12 · 「플레이 전에 알아 두실 것」. 첫 실행에 한 번만
	 * 뜨고, 무엇이 나오는지와 그것을 어느 설정으로 줄일 수 있는지를 같은
	 * 화면에서 말한다. 일반 경고는 사용자가 아니라 책임을 보호한다.
	 */
	void ShowContentNoticeIfNeeded();
	void DismissContentNotice();
	/**
	 * 소리 자막이 예전 기본값으로 켜져 있던 프로필이면 타이틀에서 접근성 설정을 그 줄에 맞춰 연다. 닫으면
	 * 그때의 값을 직접 고른 것으로 남긴다. 일괄로 끄거나 고른 것으로 치지 않고 한 번 묻는 자리다.
	 */
	void ShowSoundCaptionConfirmationIfNeeded();
	/**
	 * 화면 검사. 설정 파일에 소리 자막 값을 차례로 써 넣고 다시 읽어, 직접 끔·중요한 소리·모든 소리·예전
	 * 프로필(켬과 끔)이 각각 어떻게 읽히고 같은 입구로 낸 소리 설명이 뜨는지 본다. 실패하면 이유를,
	 * 통과하면 빈 문자열을 돌려주고 OutReceipt에 결과 한 줄을 쓴다.
	 */
	FString VerifySoundCaptionSettingCases(FString& OutReceipt);
	/** 화면 검사에서 천장 노크를 낼 자리. 플레이어 머리 위 한 층이다. */
	FVector GetFrontendProbeCeilingLocation() const;

	/** §19.8 키 재설정 화면. 열림·이동·바인딩 대기·되돌리기. */
	void OpenKeyBindings();
	void CloseKeyBindings();
	void MoveKeyBindingSelection(int32 Direction);
	void MoveKeyBindingColumn(int32 Direction);
	void ConfirmKeyBindingSelection();
	/** 대기 중이면 눌린 키를 받아 본다. 처리했으면 true. */
	bool CaptureKeyBindingInput(const struct FInputKeyEventArgs& Params);

	/**
	 * Harness hook for §24's 즉시 차단 19. The sealed hour has to turn the
	 * evidence journal down, and a probe proves that by asking for it the way
	 * the key does and then reading the screen.
	 */
	void OpenMissingFloorJournalForTesting() { OpenMissingFloorJournal(); }
	/** 검사용. 기록장 쪽을 넘기고(←가 -1) 닫는다. 키를 누른 것과 같은 길로 간다. */
	void MoveMissingFloorJournalPageForTesting(const int32 Direction) { MoveMissingFloorJournalPage(Direction); }
	void CloseMissingFloorJournalForTesting() { CloseMissingFloorJournal(); }

	/**
	 * 이름표를 단 조작 잠금.
	 *
	 * `SetIgnoreMoveInput`은 컨트롤러 전역 **누적 카운터**다. 계단 전환,
	 * CH01 기억 경계, 기상 시퀀스가 각자 자기 불리언으로 그 카운터를 올리고
	 * 내리는데, 셋 중 하나라도 해제를 건너뛰면 카운터가 0으로 돌아오지
	 * 않는다. 그때 플레이어에게는 이벤트도 프롬프트도 없이 조작만 죽은
	 * 화면이 남고, 어느 시스템이 붙들고 있는지 알 방법이 없다.
	 *
	 * 이름으로 걸고 이름으로 푼다. 실제 무시 상태는 남은 이름이 있는지로만
	 * 정해지므로 두 번 걸어도, 순서가 엇갈려도 카운터가 어긋나지 않는다.
	 * 같은 이름을 두 번 걸면 두 번째는 아무 일도 하지 않는다.
	 */
	void AddInputLock(FName Reason, bool bLockMove = true, bool bLockLook = true);
	void RemoveInputLock(FName Reason);
	/** 지금 조작을 붙들고 있는 이름들. 진단용. */
	FString DescribeInputLocks() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupInputComponent() override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

private:
	void ApplyDefaultInputMapping() const;
	void ToggleSystemMenu();
	void ToggleAccessibilityMenu();
	void CloseAccessibilityMenu();
	void BeginJournalInput();
	void EndJournalInput();
	void OpenMissingFloorJournal();
	void CloseMissingFloorJournal();
	void MoveMissingFloorJournalPage(int32 Direction);
	void MoveMissingFloorJournalPageLeft();
	void MoveMissingFloorJournalPageRight();
	bool IsMissingFloorNight() const;
	void PlayMissingFloorJournalPaperSound(float VolumeMultiplier = 1.0f) const;
	/** 메뉴 칸을 옮기고(false) 고르는(true) 소리. UI 버스라 일시정지 중에도 난다. */
	void PlayMenuTick(bool bConfirm) const;
	void MoveAccessibilitySelectionUp();
	void MoveAccessibilitySelectionDown();
	void AdjustAccessibilityLeft();
	void AdjustAccessibilityRight();
	void ConfirmAccessibilitySelection();
	void RequestManualHint();
	void ChangeAccessibilitySetting(int32 Direction, bool bConfirm);
	void MoveSystemMenuSelection(int32 Direction);
	void ConfirmSystemMenuSelection();
	void SetSystemMenuMode(EIGSystemMenuMode NewMode);
	void StartHeadphoneRecommendationIfNeeded();
	void DismissHeadphoneRecommendation();
	void LoadAudioCalibrationSettings();
	void OpenAudioCalibration(bool bFirstRun);
	void CancelAudioCalibration();
	void CompleteAudioCalibration();
	void MoveAudioCalibrationSelection(int32 Direction);
	void AdjustAudioCalibrationSetting(int32 Direction);
	void ConfirmAudioCalibrationSelection();
	void ApplyAudioCalibrationValues();
	void PlayAudioCalibrationKnock();
	void OpenDisplaySettings();
	void ReturnFromDisplaySettings();
	void RefreshStagedDisplaySettings();
	void MoveDisplaySettingsSelection(int32 Direction);
	void AdjustDisplaySetting(int32 Direction);
	void ConfirmDisplaySettingsSelection();
	void ApplyDisplaySettings();
	void ConfirmPendingDisplaySettings();
	void RevertPendingDisplaySettings();
	void ReturnFromCredits();
	void StartNewGame();
	void ContinueLatestAutosave();
	void QuitToDesktop();
	void BindSaveNotifications();
	void UnbindSaveNotifications();
	UFUNCTION()
	void HandleSaveCompleted(bool bSuccess, FString SlotName);
	UFUNCTION()
	void HandleLoadCompleted(
		bool bSuccess,
		FString SlotName,
		UIGSaveGame* SaveGame);
	void SetInputDevicePresentation(bool bUsingGamepad);
	void ApplyMenuInputMode();
	void UpdateMenuWorldRendering();
	void RestoreMenuWorldRendering();
	void UpdateMenuPointerHover();
	bool HandleMenuPointerClick();
	bool TryGetMenuRowFromPointer(
		int32 RowCount,
		float MinimumStartY,
		float StartYFraction,
		float MinimumSpacing,
		float MaximumSpacing,
		float SpacingFraction,
		int32& OutRow) const;
	/** Hit tests the exact responsive rectangles drawn by the native front end. */
	bool TryGetSystemMenuRowFromPointer(int32& OutRow) const;
	bool TryGetDisplaySettingsRowFromPointer(
		int32& OutRow,
		bool& bOutCategoryHit) const;
	bool TryGetAccessibilityRowFromPointer(
		int32& OutRow,
		bool& bOutCategoryHit) const;
	void StartFrontendShippingProbe();
	void TickFrontendShippingProbe();
	void DispatchFrontendProbeKey(const FKey& Key);
	void ReleaseFrontendProbeKey(const FKey& Key);
	void AwaitFrontendProbeFrame();
	bool TryCaptureFrontendProbeLayout(
		const TCHAR* PanelName,
		int32 MinimumElementCount,
		bool bIncludeInMinimumElementCoverage = true,
		bool bIncludeInReceiptCoverage = true);
	/** 모든 설정 줄을 실제 글꼴로 그린 뒤 검사한다. 일반 실행에서는 부르지 않는다. */
	void PrepareFrontendSettingsReviewRow();
	bool TryVerifyFrontendDialogueLayout(
		const TCHAR* CaseName,
		int32 MinimumLineCount,
		int32 MaximumLineCount,
		bool bExpectedContinuation);
	void CompleteFrontendShippingProbe();
	void FailFrontendShippingProbe(const FString& Reason);
	void StartAudioCalibrationPreviewProbe();
	void TickAudioCalibrationPreviewProbe();
	void FailAudioCalibrationPreviewProbe(const FString& Reason) const;
	void StartMissingFloorEndingPreviewProbe();
	void TickMissingFloorEndingPreviewProbe();
	void FailMissingFloorEndingPreviewProbe(const FString& Reason) const;
	bool WriteFrontendShippingProbeReceipt(
		bool bSuccess,
		const FString& Reason) const;
	bool ShouldShowTitleMenu() const;
	bool HasCompatibleAutosave() const;
	bool HasEndingBAutosave() const;

	/**
	 * §9 「밤 5」 — 메타 개입은 본편 바깥, 엔딩 이후, 이 한 번이다.
	 *
	 * 엔딩 B를 본 세이브가 있으면 타이틀의 이어하기 밑에 행 하나가 조용히
	 * 늘어난다. 고르면 로딩 없이 검정 화면 30초: 그녀 자신의 둘-쉬고-하나,
	 * 긴 침묵, 그리고 B에서 복도 끝에서 돌아온 대답 둘이 같은 공간 잔향으로
	 * 다시 온다. 새 사건도, 갇힌 사람도 암시하지 않는다.
	 *
	 * 금지된 것을 명시한다: 세이브 파일을 만들거나 고치지 않고, 가짜 크래시를
	 * 내지 않고, 창 제목을 바꾸지 않는다. 신뢰를 깨는 메타는 전부 제외다.
	 */
	void PlayNightFive();
	bool IsNightFivePlaying() const { return bNightFivePlaying; }
	bool IsNightFiveSpent() const { return bNightFiveSpent; }
	int32 GetNightFiveCuesPlayed() const { return NightFiveCuesPlayed; }
	/** 30초. 이 길이가 §9의 약속이다. */
	static constexpr float NightFiveTotalSeconds = 30.0f;
	UIGAccessibilitySubsystem* GetAccessibilitySubsystem() const;
	UIGSaveSubsystem* GetSaveSubsystem() const;

	/** Mapping context assigned by the player Blueprint or data asset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true", ClampMin = "0"))
	int32 DefaultMappingPriority = 0;

	int32 AccessibilitySelection = 0;
	/** 「기본값으로 초기화」를 한 번 누른 뒤 두 번째 누름을 기다리는 끝 시각(FPlatformTime 초). */
	double AccessibilityResetArmedUntil = -1.0;
	/** 힌트 사다리. 같은 단계에서 누를수록 다음 단계를 떠올린다. */
	FName HintGoalId;
	int32 HintTier = 0;
	double LastHintRequestTime = -10.0;
	/** 마지막 단계(할 일 그대로)를 말하기 전에 한 번 물었다. 그때 시각. 진행이 바뀌면 지운다. */
	double HintAnswerOfferedAt = -100.0;
	bool bHintAnswerRevealed = false;
	int32 SystemMenuSelection = 0;
	int32 MissingFloorJournalPage = 0;
	EIGSystemMenuMode SystemMenuMode = EIGSystemMenuMode::Hidden;
	EIGSystemMenuMode KeyBindingsReturnMode = EIGSystemMenuMode::Title;
	int32 KeyBindingSelection = 0;
	/** 참이면 다음 입력이 새 키다. 화면이 「누르세요」로 바뀐다. */
	bool bKeyBindingCapturing = false;
	bool bKeyBindingColumnGamepad = false;
	FText KeyBindingStatusText;
	bool bKeyBindingStatusIsError = false;
	EIGSystemMenuMode CreditsReturnMode = EIGSystemMenuMode::Title;
	EIGSystemMenuMode AudioCalibrationReturnMode = EIGSystemMenuMode::Title;
	EIGSystemMenuMode DisplaySettingsReturnMode = EIGSystemMenuMode::Title;
	EIGSystemMenuMode AccessibilityReturnMode = EIGSystemMenuMode::Hidden;
	int32 DisplaySettingsSelection = 0;
	int32 AudioCalibrationSelection = 0;
	int32 AudioCalibrationVolumeStep = 6;
	int32 AudioCalibrationBrightnessStep = 2;
	int32 PreviousAudioCalibrationVolumeStep = 6;
	int32 PreviousAudioCalibrationBrightnessStep = 2;
	/** §10.5. 기본은 헤드폰이다. 스피커도 막지 않되 말은 하게 한다. */
	bool bHeadphoneOutput = true;
	bool bPreviousHeadphoneOutput = true;
	int32 AudioCalibrationMusicStep = 4;
	int32 AudioCalibrationAmbienceStep = 4;
	int32 PreviousAudioCalibrationMusicStep = 4;
	int32 PreviousAudioCalibrationAmbienceStep = 4;
	int32 DisplayWindowModeIndex = 0;
	int32 DisplayResolutionIndex = 1;
	int32 DisplayQualityIndex = 1;
	int32 DisplayFrameLimitIndex = 1;
	int32 PreviousDisplayQualityLevel = 2;
	int32 DisplayConfirmationSecondsRemaining = 0;
	bool bAccessibilityMenuVisible = false;
	/** 예전 프로필에 소리 자막을 묻느라 연 접근성 설정이다. 닫으면 지금 값을 고른 것으로 남긴다. */
	bool bSoundCaptionConfirmationOpen = false;
	bool bMenuWorldRenderingSuspended = false;
	bool bWorldRenderingWasDisabled = false;
	bool bGameWasPausedBeforeAccessibility = false;
	bool bMissingFloorJournalVisible = false;
	bool bJournalInputHeld = false;
	bool bGameWasPausedBeforeJournal = false;
	bool bUsingGamepadForHud = false;
	bool bCompatibleAutosaveAvailable = false;

	/** §9 「밤 5」. 세션 상태다 — 재실행하면 다시 흐려지지 않은 채로 있다. */
	bool bNightFiveAvailable = false;
	bool bNightFiveSpent = false;
	bool bNightFivePlaying = false;
	float NightFiveSeconds = 0.0f;
	int32 NightFiveCuesPlayed = 0;
	/**
	 * 엔진 티커다. 월드 타이머가 아니다 — 타이틀 메뉴는 월드를 일시정지시키므로
	 * 월드 타이머로는 이 30초가 **한 프레임도 진행하지 않는다.** 코어 티커는
	 * 일시정지와 무관하게 돌고, 뒤의 프롤로그 월드를 깨우지도 않는다.
	 */
	FTSTicker::FDelegateHandle NightFiveTicker;
	bool AdvanceNightFive(float DeltaSeconds);
	void EndNightFive();
	FVector NightFiveListenPoint() const;
	FVector NightFiveAnswerPoint() const;
	/** 방금 낸 밤 5 소리. 검사가 멈춘 월드에서도 실제로 울고 있는지 본다. */
	TWeakObjectPtr<UAudioComponent> NightFiveLastCue;
	/**
	 * -IGNightFiveProbe. 두 가지를 검사한다.
	 *
	 * 하나는 순수 논리다: 액션 인덱스와 화면 자리의 대응이 (이어하기 유무) ×
	 * (밤 5 유무) 네 조합 모두에서 전단사인지. 밤 5는 액션 목록의 마지막이면서
	 * 화면에서는 두 번째이므로, 이 대응이 깨지면 다른 행이 눌린다.
	 *
	 * 다른 하나는 30초 자체다. 소리 두 개가 저작된 시각에 나가고, 30초에
	 * 끝나고, 끝난 뒤 행이 흐려지지만 여전히 고를 수 있는지.
	 */
	void StartNightFiveProbe();
	bool AdvanceNightFiveProbe(float DeltaSeconds);
	void RequestNightFiveProbeExit(bool bFailed);
	bool bNightFiveProbeRequested = false;
	/** 별도 자막 검사에서만 실제 렌더와 캡처를 기록한다. 해금 검사는 아니다. */
	bool bNightFiveCaptionReview = false;
	uint8 NightFiveCaptionDrawMask = 0;
	uint64 NightFiveCaptionReturnFrame = 0;
	bool bNightFiveCaptionReturnVerified = false;
	FString NightFiveCaptionResultPath;
	FString NightFiveCaptionScreenshotDirectory;
	int32 NightFiveProbeStep = 0;
	float NightFiveProbeSeconds = 0.0f;
	FTSTicker::FDelegateHandle NightFiveProbeTicker;
	bool bNewGameConfirmationArmed = false;
	bool bHeadphoneRecommendationVisible = false;
	bool bAudioCalibrationCompleted = false;
	bool bAudioCalibrationFirstRun = false;
	bool bAudioCalibrationSessionActive = false;
	bool bDisplayVSync = true;
	bool bDisplaySettingsApplied = false;
	bool bDisplaySettingsAwaitingConfirmation = false;
	bool bSystemMenuStatusIsError = false;
	bool bPreviousDisplayVSync = true;
	bool bFrontendShippingProbe = false;
	bool bFrontendProbeCompilationDrained = false;
	bool bAudioCalibrationPreviewProbe = false;
	bool bAudioCalibrationPreviewScreenshotRequested = false;
	bool bAudioCalibrationPreviewCompilationDrained = false;
	/** 이름표 -> (이동을 막는가, 시야를 막는가). 비면 조작이 열려 있다. */
	TMap<FName, TPair<bool, bool>> InputLockReasons;
	double InputLockWatchdogNextReportTime = 0.0;

	void ApplyInputLocks();

	bool bMissingFloorEndingPreviewProbe = false;
	bool bMissingFloorEndingPreviewScreenshotRequested = false;
	bool bMissingFloorEndingPreviewCompilationDrained = false;
	bool bFrontendDialogueDefaultVerified = false;
	bool bFrontendCaptionPauseVerified = false;
	double FrontendCaptionPausedAt = 0.0;
	double FrontendCaptionPauseSeconds = 0.0;
	bool bFrontendDialogueVerified = false;
	bool bFrontendDialogueSpeakerVerified = false;
	bool bFrontendDialogueContinuationVerified = false;
	bool bFrontendAccessibilityScreenshotRequested = false;
	bool bFrontendDisplayScreenshotRequested = false;
	int32 FrontendProbeStep = 0;
	int32 FrontendProbeExpectedWidth = 0;
	int32 FrontendProbeExpectedHeight = 0;
	int32 FrontendProbeLayoutSampleCount = 0;
	int32 FrontendProbeMinimumElementCount = MAX_int32;
	int32 FrontendProbePressedEventCount = 0;
	int32 FrontendSettingsReviewIndex = 0;
	int32 FrontendSettingsReviewSamples = 0;
	FString FrontendSettingsReviewScreenshotPath;
	int32 AudioCalibrationPreviewExpectedWidth = 0;
	int32 AudioCalibrationPreviewExpectedHeight = 0;
	int32 MissingFloorEndingPreviewExpectedWidth = 0;
	int32 MissingFloorEndingPreviewExpectedHeight = 0;
	uint64 FrontendProbeAwaitFrameSerial = 0;
	double FrontendProbeNextActionTime = 0.0;
	double FrontendProbeStepDeadline = 0.0;
	double AudioCalibrationPreviewNextActionTime = 0.0;
	double AudioCalibrationPreviewDeadline = 0.0;
	double MissingFloorEndingPreviewNextActionTime = 0.0;
	double MissingFloorEndingPreviewDeadline = 0.0;
	double NextAudioCalibrationKnockTime = -1.0;
	FVector2D FrontendProbeBoundsMin = FVector2D::ZeroVector;
	FVector2D FrontendProbeBoundsMax = FVector2D::ZeroVector;
	FString FrontendProbeDefaultScreenshotPath;
	/** 소리 설명을 직접 켠 접근성 시연 화면. 기본 화면과 따로 남긴다. */
	FString FrontendProbeCaptionScreenshotPath;
	FString FrontendProbeScreenshotPath;
	FString FrontendProbeTitleScreenshotPath;
	FString AudioCalibrationPreviewScreenshotPath;
	FString MissingFloorEndingPreviewScreenshotPath;
	float MissingFloorEndingPreviewElapsedSeconds = 0.8f;
	float PreviousDisplayFrameLimit = 60.0f;
	double DisplayConfirmationDeadline = 0.0;
	double JournalInputPressedAt = 0.0;
	double HeadphoneRecommendationDeadline = 0.0;
	FText SystemMenuStatusText;
};
