#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Player/IGSettingsMenuLayout.h"
#include "Player/IGHudGuidance.h"
#include "Player/IGContextTips.h"
#include "IGHorrorHUD.generated.h"

class AIGListenerEntity;
class UAudioComponent;
class AIGMissingFloorMercyDirector;
class AIGReadableNote;
class IIGObjectiveProvider;
class UFont;
class UTexture2D;
class UFontFace;
class UIGInteractionComponent;

/** HUD text roles; each maps to a font rasterized at its native pixel size. */
enum class EIGHudTextRole : uint8
{
	Objective,
	Prompt,
	Thought,
	Dialogue,
	Speaker,
	Hint
};

/** Visual and accessibility contract for a line presented in the lower HUD. */
enum class EIGDialogueChannel : uint8
{
	/** Ji-woon's unvoiced inner monologue. This is story text, not a subtitle. */
	InnerVoice,
	/** Text-first exchange whose copy is the primary delivery channel. */
	Conversation,
	/** Subtitle paired with recorded speech; follows the subtitle toggle. */
	VoiceSubtitle,
	/** Diegetic machine or phone response. */
	Device
};

/**
 * §9 에필로그의 한 장면. 디렉터가 시각표를 들고, HUD는 지금 어느 장면인지만
 * 받아 그린다. 화면은 카메라 페이드가 이미 검게 만들어 두었으므로 여기서
 * 하는 일은 그 검정 위에 무엇을 얹느냐가 전부다.
 */
enum class EIGMissingFloorEpilogueScene : uint8
{
	None,
	/** 소리만 지나간다. 글자도 그림도 없다. */
	Montage,
	/** 에필로그 1 — 도하의 공방. 화면은 손과 현만(엔딩 A). */
	Workshop,
	/** 에필로그 2 — 가을의 달빛빌라(엔딩 A). */
	Autumn,
	/** 마지막 신 — 비어 있는 서비스 베이(엔딩 B). */
	ServiceBay,
	/** 두 엔딩 공통. 목격한 만큼 문장이 선명해진다(§22.3). */
	News,
	/** 마지막 카드 한 줄. */
	Card
};

/** Higher-priority lines may briefly interrupt and then resume a lower one. */
enum class EIGDialoguePriority : uint8
{
	Ambient,
	Story,
	Critical
};

/** Small value object kept outside UObject reflection to avoid per-line allocation churn. */
struct FIGDialogueMessage
{
	FText Speaker;
	FText Line;
	EIGDialogueChannel Channel = EIGDialogueChannel::InnerVoice;
	EIGDialoguePriority Priority = EIGDialoguePriority::Story;
	float MinimumDurationSeconds = 0.0f;
	double QueuedAt = 0.0;
	bool bContinuation = false;
};

/** Queued non-dialogue audio description; authored timings remain the source of truth. */
struct FIGAudioCaptionMessage
{
	FText Caption;
	float DurationSeconds = 0.0f;
	double QueuedAt = 0.0;
};

/** Snapshot of controller-owned front-end state consumed by the native HUD. */
struct FIGSystemMenuPresentation
{
	bool bVisible = false;
	bool bTitle = false;
	/** Title key art is also retained when credits were opened from the title. */
	bool bUseTitleBackdrop = false;
	bool bCredits = false;
	/** 첫 실행 콘텐츠 고지. 제작 정보와 같은 전체 화면 계층을 쓴다. */
	bool bContentNotice = false;
	/** §19.8 키 재설정 화면. 행·칸·대기 상태는 컨트롤러가 소유한다. */
	bool bKeyBindings = false;
	int32 KeyBindingSelection = 0;
	bool bKeyBindingCapturing = false;
	bool bKeyBindingColumnGamepad = false;
	FText KeyBindingStatus;
	bool bKeyBindingStatusIsError = false;
	bool bAudioCalibration = false;
	bool bDisplaySettings = false;
	bool bCanContinue = false;
	/** §9 「밤 5」: 엔딩 B를 본 세이브가 있을 때만 그 행이 존재한다. */
	bool bNightFiveAvailable = false;
	/** 한 번 재생하면 흐려진다. 다시 들을 수는 있다. */
	bool bNightFiveSpent = false;
	/** 검정 화면 30초. 재생 중에는 메뉴를 그리지 않는다. */
	bool bNightFivePlaying = false;
	bool bConfirmNewGame = false;
	bool bHeadphoneRecommendation = false;
	bool bVSync = true;
	bool bDisplaySettingsApplied = false;
	bool bDisplaySettingsAwaitingConfirmation = false;
	bool bStatusIsError = false;
	int32 SelectedRow = 0;
	int32 DisplaySelectedRow = 0;
	int32 AudioCalibrationSelectedRow = 0;
	int32 AudioCalibrationVolumeStep = 6;
	int32 AudioCalibrationBrightnessStep = 2;
	bool bAudioCalibrationFirstRun = false;
	bool bHeadphoneOutput = true;
	int32 AudioCalibrationMusicStep = 4;
	int32 AudioCalibrationAmbienceStep = 4;
	int32 WindowModeIndex = 0;
	int32 ResolutionIndex = 1;
	int32 QualityIndex = 1;
	int32 FrameLimitIndex = 1;
	int32 ConfirmationSecondsRemaining = 0;
	FText StatusText;
};

/**
 * Lightweight native HUD for the prologue.
 * It intentionally avoids widget assets so the first playable build always has
 * interaction feedback, objectives, inner-voice lines and control hints.
 *
 * Korean text renders through bundled runtime composite fonts so typography
 * and glyph metrics stay identical in Editor and packaged builds. A system
 * font remains a development-only fallback for partial source checkouts.
 */
enum class EIGBindableAction : uint8;

UCLASS()
class INDIEGAME_API AIGHorrorHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	/** 잠깐 목표와 조작법을 다시 본다. 다시 누르면 바로 닫힌다. */
	void ToggleGameplayGuide();
	bool CanShowGameplayGuide() const;
	/**
	 * 에필로그 뒤의 엔딩 크레딧. 검은 화면 위로 제작 정보가 올라가고, 다 올라가거나
	 * 확인 키를 누르면 타이틀로 넘어간다. 도시의 새벽 소리가 낮게 깔린다.
	 */
	void StartEndCredits();
	bool IsEndCreditsActive() const { return bEndCreditsActive; }
	/** 지금 표시 언어가 한국어인가. 한국어로 인쇄된 그림을 그대로 쓸지 가른다. */
	static bool IsKoreanCulture();
	/** 줄 높이 측정용 글자. 표시 언어마다 번역으로 바꾼다. */
	static FString GetLineHeightSample();
	/**
	 * 키의 짧은 이름. 패드는 Xbox 배열 이름, 키보드는 키캡에 적힌 이름이다.
	 * 엔진 키 이름은 언어마다 번역돼서(간체의 「Tab键」) 다른 키와 모양이 어긋난다.
	 */
	static FText GetShortKeyLabel(const FKey& Key);
	/** 힌트 키가 지금 뭔가를 하는가. 설정에서 끄면 안내에도 적지 않는다. */
	bool IsHintRequestAvailable() const;
	/** 막힌 것 같을 때 힌트 키를 한 번 알려 준다. 힌트를 끈 사람에게는 띄우지 않는다. */
	void OfferHintTip();

	/** Pushes a short inner-voice line onto the local player's HUD. */
	static void PushThought(
		const UObject* WorldContext,
		const FText& Thought,
		float DurationSeconds = 3.5f);

	/**
	 * Presents a speaker-aware line without taking movement or camera control.
	 * Text-first channels remain visible because hiding them would remove story;
	 * only VoiceSubtitle follows the player's subtitle toggle.
	 */
	static void PushDialogue(
		const UObject* WorldContext,
		const FText& Speaker,
		const FText& Line,
		EIGDialogueChannel Channel = EIGDialogueChannel::Conversation,
		float MinimumDurationSeconds = 0.0f,
		EIGDialoguePriority Priority = EIGDialoguePriority::Story);

	/**
	 * 소리가 난 자리를 아는 자막. §10.5는 자막 레인에 방위를 병기하라고
	 * 적어 두었는데, 그동안은 대사에 「뒤쪽」을 손으로 써 넣는 것이
	 * 전부였다. 손으로 쓴 방위는 플레이어가 돌아서면 그대로 틀린다.
	 *
	 * 화면 안에 있는 소리에는 방위를 붙이지 않는다. 보이는 것을 굳이
	 * 적으면 읽을 것만 늘어난다.
	 */
	static void PushAudioCaptionAt(
		const UObject* WorldContext,
		const FText& Caption,
		float DurationSeconds,
		const FVector& SourceLocation);

	/**
	 * 시점 기준 방위 딱지. 붙일 것이 없으면 빈 문자열이다.
	 * 위아래가 이 게임의 정체성이라 고도가 좌우를 이긴다(§10.5).
	 */
	static FText MakeSoundBearingTag(
		const UObject* WorldContext,
		const FVector& SourceLocation);

	/**
	 * 한 줄이 화면에 머무는 시간. 큐를 쌓는 쪽이 장면 길이를 잴 때 쓴다 —
	 * 접근성 배율은 빼고 잰다.
	 */
	static float EstimateDialogueSeconds(
		const FText& Line,
		float MinimumDurationSeconds = 0.0f);

	/** Shows a non-dialogue sound caption when the accessibility option is on. */
	static void PushAudioCaption(
		const UObject* WorldContext,
		const FText& Caption,
		float DurationSeconds = 2.0f);

	/** Shows a short grayscale edge wave pointing toward an authored fear cue. */
	static void PushFearDirection(
		const UObject* WorldContext,
		const FVector& WorldLocation,
		float DurationSeconds = 1.1f);

	/** Starts the authored first-person contact/recoil sequence for a valid knock. */
	void PlayFirstPersonKnock();

	/**
	 * 실제 괴물의 접촉과 암전 동안 일반 HUD를 가린다.
	 */
	void PlayCaptureEmbrace(float DurationSeconds = 1.2f);

	/**
	 * 침대에서 시야와 입력이 돌아올 때까지 일반 HUD를 가린다.
	 * 화면 위에 손 그림을 겹치지 않는다.
	 */
	void PlayCaptureWakeEcho(
		int32 CaptureCount,
		float VisualDurationSeconds = 0.68f,
		float OwnershipDurationSeconds = 0.68f);

	/**
	 * Returns text bounds from the most recently completed HUD frame. The
	 * packaged frontend probe uses this to prove that native menu copy stayed
	 * inside the real Shipping canvas at every supported resolution.
	 */
	bool GetLayoutValidationSample(
		FVector2D& OutCanvasSize,
		FVector2D& OutBoundsMin,
		FVector2D& OutBoundsMax,
		int32& OutElementCount,
		bool& bOutAllInsideCanvas,
		bool& bOutAllInsideSettingsContainers,
		uint64& OutFrameSerial) const;

	/** 실제로 그린 글자의 겹침·영역 이탈을 배포본 검사 결과에도 남긴다. */
	FString GetTextAuditFailureReport() const { return FString::Join(TextAuditFailures, TEXT("\n")); }
	/** 마지막 HUD 프레임에 소리 자막을 실제로 그렸는지 검사한다. */
	bool WasAudioCaptionDrawnInLastHudFrame() const { return bAudioCaptionDrawnInLastHudFrame; }
	bool HasPendingAudioCaption() const { return !CurrentAudioCaption.IsEmpty() || AudioCaptionQueue.Num() > 0; }

	/** Last lower-third dialogue layout actually drawn by the Shipping probe. */
	bool GetDialogueRenderSample(
		FVector2D& OutPanelMinimum,
		FVector2D& OutPanelMaximum,
		FVector2D& OutCanvasSize,
		int32& OutLineCount,
		bool& bOutSpeakerVisible,
		bool& bOutHasContinuation,
		bool& bOutInsideSafeArea,
		uint64& OutFrameSerial) const;

	/** 마지막으로 그린 대화 상자에서 「이어짐」 표시가 마지막 줄과 겹치지 않았는지. */
	bool IsDialogueContinuationClear() const { return bDialogueLastContinuationClear; }

	/** 문서의 페이지를 넘긴다. 다시 펼치면 첫 장부터 읽는다. */
	void MoveNotePage(int32 Direction);
	int32 GetNotePageIndex() const { return NotePageIndex; }
	int32 GetNotePageCount() const { return NotePageCount; }
	bool IsNoteTextWithinPaper() const { return bNoteTextWithinPaper; }

	/** 소음 파문 링이 지금 화면에 걸려 있는지. 실행 검사가 쓴다. */
	bool IsNoiseRippleActive() const;

	/**
	 * Shows a reusable story-transition card over a fading black scrim.
	 * All copy is supplied by the caller so the HUD remains chapter-agnostic.
	 */
	static void ShowChapterCard(
		const UObject* WorldContext,
		const FText& Eyebrow,
		const FText& Title,
		const FText& Subtitle,
		float DurationSeconds = 4.2f);

	void ShowThought(const FText& Thought, float DurationSeconds);
	void ShowDialogue(
		const FText& Speaker,
		const FText& Line,
		EIGDialogueChannel Channel,
		float MinimumDurationSeconds,
		EIGDialoguePriority Priority);
	void ShowAudioCaption(const FText& Caption, float DurationSeconds);
	void ShowFearDirection(const FVector& WorldLocation, float DurationSeconds);
	/** §19.7. 저장됐다는 흔적 하나. 0.8초 뒤에 사라진다. */
	void ShowSaveIndicator();
	void PresentChapterCard(
		const FText& Eyebrow,
		const FText& Title,
		const FText& Subtitle,
		float DurationSeconds);

	/**
	 * Binds the objective source used after the wake-up sequence reaches free
	 * roam. The object must implement IIGObjectiveProvider.
	 */
	void SetObjectiveProvider(UObject* InObjectiveProvider);

	/** Progress reported by the currently bound story objective provider. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	float GetObjectiveProgress() const;

	UFUNCTION(BlueprintPure, Category = "HUD")
	bool SupportsKoreanText() const { return KoreanFontMedium != nullptr; }

	/**
	 * 없는 층 night austerity (§11 V4): during the hour the objective line is
	 * not shown at all. Pushed by the night director; off leaves every legacy
	 * chapter's HUD byte-identical.
	 */
	void SetNightPresentation(bool bInNightPresentation)
	{
		bNightPresentation = bInNightPresentation;
	}

	UFUNCTION(BlueprintPure, Category = "HUD")
	bool IsNightPresentation() const { return bNightPresentation; }

	/**
	 * The fifth-dawn interlude owns a black frame. Ordinary crosshair, focus,
	 * objective and dialogue stay out; directional accessibility captions may
	 * still draw because sound is the only image in that scene.
	 */
	void SetSensoryInterludePresentation(bool bEnabled)
	{
		bSensoryInterludePresentation = bEnabled;
	}
	void SetSensoryInterludeSkipState(
		bool bAvailable,
		float Progress,
		bool bInProgress,
		float RequiredHoldSeconds,
		bool bToggleMode)
	{
		bSensoryInterludeSkipAvailable = bAvailable;
		SensoryInterludeSkipProgress = FMath::Clamp(Progress, 0.0f, 1.0f);
		bSensoryInterludeSkipInProgress = bInProgress;
		SensoryInterludeSkipHoldSeconds = FMath::Max(RequiredHoldSeconds, 0.25f);
		bSensoryInterludeSkipToggleMode = bToggleMode;
	}
	/** 서사가 소유하는 실패 연출이며 입력 처리는 밤 디렉터에 남긴다. */
	void BeginMissingFloorFailureEnding(float InitialElapsedSeconds = 0.0f);
	void SetMissingFloorFailureRetryEnabled(bool bEnabled)
	{
		bMissingFloorFailureRetryEnabled = bEnabled;
	}
	void EndMissingFloorFailureEnding();
	bool IsMissingFloorFailureEndingVisible() const
	{
		return bMissingFloorFailureEndingVisible;
	}

	/**
	 * §9 에필로그의 장면 하나를 건다. 디렉터가 장면이 바뀔 때마다 부르고,
	 * 흐른 시간은 HUD가 직접 잰다 — 실패 엔딩과 같은 규칙이라 프레임마다
	 * 상태를 밀어 넣는 경로가 하나도 늘지 않는다.
	 */
	void BeginMissingFloorEpilogueScene(
		EIGMissingFloorEpilogueScene Scene,
		const FText& Heading,
		const TArray<FText>& BodyLines,
		const FText& Footnote);
	void EndMissingFloorEpilogue();
	bool IsMissingFloorEpilogueVisible() const
	{
		return MissingFloorEpilogueScene != EIGMissingFloorEpilogueScene::None;
	}
	EIGMissingFloorEpilogueScene GetMissingFloorEpilogueScene() const
	{
		return MissingFloorEpilogueScene;
	}
	/** 계약 스크립트가 지금 화면의 문장을 그대로 읽는다. */
	const TArray<FText>& GetMissingFloorEpilogueLinesForTesting() const
	{
		return MissingFloorEpilogueBodyLines;
	}

	/** Native, asset-independent accessibility panel driven by the controller. */
	/** ResetArmedUntil은 FPlatformTime 초다. 그때까지 「기본값으로 초기화」가 한 번 더 누르기를 기다린다. */
	void SetAccessibilityMenuState(bool bVisible, int32 SelectedRow, double ResetArmedUntil = -1.0);
	/** Native title, pause and credits presentation shared by packaged builds. */
	void SetSystemMenuState(const FIGSystemMenuPresentation& Presentation);
	/**
	 * Daylight-only evidence journal for 없는 층. The controller owns pause and
	 * input; the HUD only renders the requested page from the saved provenance.
	 */
	void SetMissingFloorJournalState(bool bVisible, int32 PageIndex);
	bool IsMissingFloorJournalVisible() const { return bMissingFloorJournalVisible; }
	/**
	 * Harness hook: the line currently on screen, so a probe can assert *which*
	 * refusal the game gave instead of inferring it from an absence. §24's
	 * 즉시 차단 19 is about the sealed hour turning F9 and the journal down, and
	 * "nothing happened" is also what a broken binding looks like.
	 */
	FText GetActiveDialogueLineForTesting() const
	{
		return bHasCurrentDialogue ? CurrentDialogue.Line : FText::GetEmpty();
	}
	/**
	 * PushThought queues; it does not preempt. Reading only the line on screen
	 * returns whatever was already speaking, so a probe that wants to know
	 * whether the refusal was *given* has to look at the queue too.
	 */
	bool HasDialogueLineForTesting(const FString& Line) const
	{
		if (bHasCurrentDialogue && CurrentDialogue.Line.ToString().Equals(Line))
		{
			return true;
		}
		for (const FIGDialogueMessage& Queued : DialogueQueue)
		{
			if (Queued.Line.ToString().Equals(Line))
			{
				return true;
			}
		}
		return false;
	}
	/**
	 * 대화 줄이 비었는가. 폰 진동처럼 소리와 글이 같은 순간이어야 하는 장면은
	 * 줄이 빌 때까지 기다렸다가 민다. 줄 뒤에 서면 진동만 먼저 울린다.
	 */
	bool IsDialogueLaneIdle() const
	{
		return !bHasCurrentDialogue && DialogueQueue.IsEmpty();
	}
	int32 GetMissingFloorJournalPageCount() const;
	void SetInputDevicePresentation(bool bInUsingGamepad)
	{
		bUsingGamepad = bInUsingGamepad;
	}

protected:
	virtual void BeginPlay() override;
	/** Unbinds the noise-bus listener; HUDs are recreated per controller. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ResolveInteractionComponent();
	void InitializeKoreanFont();
	void InitializeFrontendMenuTextures();
	void InitializeDialogueSurfaceTextures();
	void InitializeAudioCalibrationTexture();
	void InitializeMissingFloorJournalTextures();
	void InitializeFirstPersonActionTextures();
	UFontFace* LoadBundledFontFace(
		const TCHAR* RelativePath,
		const TCHAR* FontFaceName);
	UFont* MakeRuntimeFont(UFontFace* FontFace, int32 PixelSize, const TCHAR* FontName);
	/**
	 * 일본어와 중국어 글꼴. 번들 파일(UI/Fonts/NotoSans*-Regular.otf)이 있으면
	 * 그것을, 없으면 윈도우 기본 글꼴을 읽는다. 한 번 읽은 글꼴은 다시 읽지 않는다.
	 */
	UFontFace* LoadCjkFontFace(const FString& Culture);
	/** 표시 언어가 바뀌면 역할별 글꼴을 다시 만든다. 한자와 가나는 그 언어의 글꼴로 그린다. */
	void HandleCultureChanged();
	UFont* GetFontForRole(EIGHudTextRole TextRole) const;
	FText GetObjectiveText() const;
	void DrawCenteredText(
		const FText& Text,
		float ScreenY,
		const FLinearColor& Color,
		EIGHudTextRole TextRole,
		float TextScale = 1.0f);
	void DrawLeftAlignedText(
		const FText& Text,
		const FVector2D& Position,
		const FLinearColor& Color,
		EIGHudTextRole TextRole,
		float TextScale = 1.0f,
		bool bUseOutline = false);
	void DrawRightAlignedText(
		const FText& Text,
		const FVector2D& Position,
		const FLinearColor& Color,
		EIGHudTextRole TextRole,
		float TextScale = 1.0f,
		bool bUseOutline = false);
	void BeginLayoutValidationSample();
	/** -IGTextAudit: 이번 프레임 글자 상자를 모은다. 다음 프레임 첫머리에 판정한다. */
	void RecordTextAudit(const FString& Text, const FVector2D& Min, const FVector2D& Max);
	/** 글자가 이 판 안에 있어야 한다. 그리기 순서대로 쌓고 끝나면 꺼낸다. */
	void PushTextAuditContainer(const FVector2D& Min, const FVector2D& Max);
	void PopTextAuditContainer();
	void FinishTextAuditFrame();
	void RecordLayoutValidationRect(
		const FVector2D& Minimum,
		const FVector2D& Maximum);
	void ValidateSettingsTextRect(
		const FVector2D& Minimum,
		const FVector2D& Maximum,
		const FVector2D& ContainerMinimum,
		const FVector2D& ContainerMaximum);
	void FinalizeLayoutValidationSample();
	/**
	 * 화면 가운데 점. 기본은 조사할 대상을 겨눴을 때만 뜬다. 접근성 설정에서
	 * 항상 띄우게 하면 멀미 나는 손에 기준점 하나를 남긴다.
	 */
	void DrawCenterDot(bool bFocused);
	bool DrawChapterCard(double CurrentTime);
	/** Full-screen reading panel for whatever note is currently open. */
	void DrawNotePanel();
	TWeakObjectPtr<AIGReadableNote> ReadingLayoutNote;
	uint32 ReadingLayoutRevision = 0;
	FVector2D ReadingLayoutSize = FVector2D::ZeroVector;
	float ReadingLayoutScale = 0;
	TArray<TArray<FString>> ReadingTextPages;
	int32 NotePageIndex = 0;
	int32 NotePageCount = 1;
	bool bNoteTextWithinPaper = false;
	/** 어두운 세로 휴대폰 화면. 중고 거래 글처럼 폰으로 보는 기록을 여기에 띄운다. */
	void DrawPhoneNotificationPanel(const AIGReadableNote& Note);
	void DrawFearDirection(double CurrentTime);
	void DrawFirstPersonKnock(double CurrentTime);
	/** 포획 연출이 HUD 전체 프레임을 점유하는 동안 true를 반환한다. */
	bool DrawCaptureEmbrace(double CurrentTime);
	/** 포획 뒤 기상 잔상이 HUD 전체 프레임을 점유하는 동안 true를 반환한다. */
	bool DrawCaptureWakeEcho(double CurrentTime);
	/** 재관람 전용 우회 안내. 자막은 기존 하단 안전 영역을 그대로 사용한다. */
	void DrawSensoryInterludeSkip();
	bool DrawMissingFloorFailureEnding(double CurrentTime);
	/** §19.8 키 재설정 목록. 설정 화면과 같은 계층을 쓴다. */
	void DrawKeyBindingsPanel();
	/** §9 에필로그가 HUD 전체 프레임을 점유하는 동안 true를 반환한다. */
	bool DrawMissingFloorEpilogue(double CurrentTime);
	/**
	 * One expanding arc at the screen edge, sized by how far the sound the
	 * player just made actually carries (§5.1). No numbers, no meter: the ring
	 * is the entire channel, which is why reduced motion keeps it (pinned at
	 * its final radius) instead of suppressing it.
	 */
	void DrawNoiseRipple(double CurrentTime);
	/** 숨어 있는 동안의 가림막. 장롱은 세로 문틈, 침대 밑은 가로로 긴 틈만 남긴다. */
	void DrawHidingMask();
	void HandleNoiseReported(const struct FIGNoiseEvent& Event);
	/** Draws a scalable anti-aliased surface without allocating a Slate widget. */
	void DrawRoundedHudSurface(
		const FVector2D& Position,
		const FVector2D& Size,
		float CornerRadius,
		const FLinearColor& Color) const;
	/** Adds the authored optical-film grain while keeping rounded corners clean. */
	void DrawDialogueFilm(
		const FVector2D& Position,
		const FVector2D& Size,
		float CornerRadius,
		float Alpha) const;
	bool DrawDialoguePanel(double CurrentTime, float& OutPanelTop);
	/** 자막은 넘겨받은 게임 시간 대신 자막 시계(AdvanceAudioCaptionClock)로 잰다. */
	bool DrawAudioCaption(double CurrentTime, float MaximumBottomY, float* OutPanelTop = nullptr);
	/**
	 * 자막 시계를 지금까지 흘리고 그 값을 돌려준다. 일시정지에는 멈추지만
	 * 타이틀(밤 5 포함)이 떠 있으면 흐른다 — 타이틀은 월드를 멈춘 채로 소리를 낸다.
	 */
	double AdvanceAudioCaptionClock();
	void EnqueueDialogue(FIGDialogueMessage&& Message, double CurrentTime);
	void ActivateDialogue(FIGDialogueMessage&& Message, double CurrentTime);
	void AdvanceDialogueQueue(double CurrentTime);
	void SuspendDialoguePresentation(double CurrentTime);
	void ResumeDialoguePresentation(double CurrentTime);
	/** 접근성 설정의 자막 표시 시간 배율. 없으면 1이다. */
	float GetCaptionDurationScale() const;
	float CalculateDialogueDuration(
		const FString& Line,
		float MinimumDurationSeconds) const;
	float GetResolutionTextScale(float UserScale) const;
	void PrepareDialoguePage(
		float TextScale,
		float MaximumWidth,
		int32 MaximumLines,
		double CurrentTime);
	void ActivateAudioCaption(FIGAudioCaptionMessage&& Message, double CurrentTime);
	void AdvanceAudioCaptionQueue(double CurrentTime);
	float MeasureTextWidth(const FString& Text, UFont* Font, float TextScale) const;
	float MeasureTextHeight(const FString& Text, UFont* Font, float TextScale) const;
	float GetFittedTextScale(
		const FText& Text,
		EIGHudTextRole TextRole,
		float PreferredScale,
		float MaximumWidth,
		float MinimumScale = 0.5f) const;
	int32 FindFittingCaptionPrefix(
		const FString& Text,
		UFont* Font,
		float TextScale,
		float MaximumWidth) const;
	void WrapHudText(
		const FString& Source,
		UFont* Font,
		float TextScale,
		float MaximumWidth,
		int32 MaximumLines,
		TArray<FString>& OutLines,
		FString& OutRemainder) const;
	void DrawAccessibilityPanel();
	void DrawSystemMenuPanel();
	void DrawAudioCalibrationPanel();
	void DrawDisplaySettingsPanel();
	void DrawSettingsShell(
		const IGSettingsMenuLayout::FPanelMetrics& Metrics,
		const FText& Title,
		const FText& Subtitle,
		const FText& ContextLabel);
	void DrawSettingsCategoryRow(
		const IGSettingsMenuLayout::FPanelMetrics& Metrics,
		int32 CategoryIndex,
		const FText& Label,
		bool bSelected);
	void DrawSettingsOptionRow(
		const IGSettingsMenuLayout::FPanelMetrics& Metrics,
		int32 LocalRow,
		const FText& Label,
		const FText& Value,
		bool bSelected,
		bool bAdjustable);
	float DrawSettingsDetailText(
		const FString& Text,
		const FVector2D& Position,
		float MaximumWidth,
		float TextScale,
		const FLinearColor& Color);
	void DrawSettingsFooterText(
		const IGSettingsMenuLayout::FPanelMetrics& Metrics,
		const FText& Text);
	void DrawMissingFloorJournalPanel();
	UTexture2D* GetMissingFloorJournalThumbnail(int32 ThumbnailType) const;
	/** Screen-space bracket that snaps around whatever is currently focused. */
	/** 지금 묶인 키의 짧은 이름. 프롬프트와 힌트 줄이 읽는다. */
	FText GetBoundKeyLabel(EIGBindableAction Action, bool bGamepad) const;
	void UpdateFocusBracket(AActor* FocusedActor, float DeltaSeconds);
	void DrawFocusBracket(const FLinearColor& Color, float Progress);
	/** 카메라 페이드가 화면을 거의 덮었다. 검은 화면 위에는 조준점도 안내도 두지 않는다. */
	bool IsCameraFadedOut() const;
	/** 마친 조사 수를 보고 첫 조작 안내를 끝내고, 익힌 동작을 프로필에 센다. */
	void NoteCompletedInteractions(const UIGInteractionComponent* Interaction);
	/** 지금 상황에 맞는 조작 안내 한 줄을 고르고 시간을 센다. */
	void UpdateContextTips(float DeltaSeconds, bool bLaneFree);
	void DrawContextTip(float Alpha);
	/**
	 * 한국어로만 손글씨가 적힌 쪽지(다섯 번 잡힌 뒤의 메모, 문 아래 메모)를
	 * 다른 언어로 켠 사람이 가까이서 내려다보면 그 글을 속말로 한 번 읽어 준다.
	 * 한국어판은 종이를 직접 읽으므로 아무것도 띄우지 않는다.
	 */
	void UpdatePrintedTextReading();
	/** 크레딧을 그린다. 끝났으면 false를 돌려주고 타이틀을 부른다. */
	bool DrawEndCredits();
	void FinishEndCredits();
	/** 입주 저녁 첫 안내와 F1 안내가 쓰는 조작 줄. bFull이면 전부, 아니면 첫 안내 두 줄만. */
	void DrawControlsGuide(float Alpha, bool bFull);

	bool SupportsKorean() const { return KoreanFontMedium != nullptr; }


	/** Aged-paper sheet the reading panel is printed on. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> NotePaperTexture;

	/** ImageGen-derived, low-contrast optical grain used by dialogue surfaces. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DialogueFilmTexture;

	/** Text-free ImageGen key art; every title/menu glyph stays runtime-localized. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FrontendTitleBackgroundTexture;

	/** One 256x1 alpha ramp replaces stepped shade strips and extra draw calls. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FrontendShadeTexture;

	/** ImageGen 파생 저조도 벽면. 보정 안내와 눈금은 런타임에서 그린다. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> AudioCalibrationWallTexture;

	/**
	 * §9 에필로그의 세 정지 화면. 없으면 글자만 남는다 — 에필로그의 뜻은
	 * 문장에 있으므로 그림이 빠져도 장면이 무너지지 않아야 한다.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> EpilogueWorkshopTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> EpilogueAutumnTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> EpilogueServiceBayTexture;

	/** ImageGen-derived blank ledger paper. All Korean copy remains runtime text. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MissingFloorJournalTexture;

	/** ImageGen-derived M0 hand phases; screen-space only, never world geometry. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> FirstPersonKnockFrames;

	/** Existing world textures sampled as restrained evidence-card thumbnails. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> JournalMeterTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> JournalPlasterTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> JournalTankTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> JournalMetalTexture;

	/** Runtime 9-slice mask; one 64 px allocation shared by every HUD surface. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> HudRoundedMaskTexture;

	/** Bundled faces keep typography and glyph metrics stable after packaging. */
	UPROPERTY(Transient)
	TObjectPtr<UFontFace> KoreanBodyFontFace;

	UPROPERTY(Transient)
	TObjectPtr<UFontFace> KoreanEmphasisFontFace;

	UPROPERTY(Transient)
	TObjectPtr<UFontFace> KoreanDisplayFontFace;

	/** Per-role fonts rasterized at native size so Hangul stays crisp. */
	UPROPERTY(Transient)
	TObjectPtr<UFont> KoreanFontLarge;

	/** Dedicated 64 px display face; scaling the 24 px HUD role looked soft. */
	UPROPERTY(Transient)
	TObjectPtr<UFont> KoreanFrontendTitleFont;

	UPROPERTY(Transient)
	TObjectPtr<UFont> KoreanFontMedium;

	UPROPERTY(Transient)
	TObjectPtr<UFont> KoreanFontSmall;

	/** 15 px sender/status face used by the phone presentation contract. */
	UPROPERTY(Transient)
	TObjectPtr<UFont> KoreanPhoneMetaFont;

	/** 문화권 이름(ja, zh-Hans, zh-Hant)별 한자·가나 글꼴. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UFontFace>> CjkFontFaces;
	/** 지금 역할별 글꼴에 붙어 있는 한자·가나 글꼴. 없으면 한국어와 영어만 그린다. */
	UPROPERTY(Transient)
	TObjectPtr<UFontFace> ActiveCjkFontFace;
	/** 글꼴 모음(.ttc) 안에서 쓸 서체 번호. 문화권별로 CjkFontFaces와 짝이다. */
	TMap<FString, int32> CjkFontSubFaces;
	int32 ActiveCjkSubFaceIndex = 0;
	FDelegateHandle CultureChangedHandle;

	TWeakObjectPtr<UIGInteractionComponent> InteractionComponent;
	TWeakObjectPtr<UObject> ObjectiveProvider;

	TWeakObjectPtr<class UIGNoiseSubsystem> NoiseSubsystem;
	FDelegateHandle NoiseReportedHandle;
	/** 존재의 연출 소리(§19.8 대체 채널). 소음 버스와 따로 온다. */
	FDelegateHandle PresentationCueHandle;

	FIGDialogueMessage CurrentDialogue;
	TArray<FIGDialogueMessage> DialogueQueue;
	TArray<FString> CurrentDialogueLines;
	bool bHasCurrentDialogue = false;
	bool bCurrentDialogueHasContinuation = false;
	double DialogueStartTime = 0.0;
	double DialogueEndTime = -1.0;
	double DialogueOccludedAt = -1.0;
	float DialogueLayoutScale = -1.0f;
	float DialogueLayoutWidth = -1.0f;
	int32 DialogueLayoutMaximumLines = 0;

	FVector2D DialogueLastPanelMinimum = FVector2D::ZeroVector;
	FVector2D DialogueLastPanelMaximum = FVector2D::ZeroVector;
	FVector2D DialogueLastCanvasSize = FVector2D::ZeroVector;
	int32 DialogueLastLineCount = 0;
	bool bDialogueLastSpeakerVisible = false;
	bool bDialogueLastHasContinuation = false;
	bool bDialogueLastContinuationClear = true;
	bool bDialogueLastInsideSafeArea = false;
	uint64 DialogueLastRenderSerial = 0;

	FText CurrentAudioCaption;
	bool bAudioCaptionDrawnInLastHudFrame = false;
	TArray<FIGAudioCaptionMessage> AudioCaptionQueue;
	double AudioCaptionStartTime = 0.0;
	double AudioCaptionEndTime = -1.0;
	/** 자막 시계. 시작·끝·대기열이 모두 이 값으로 재진다. */
	double AudioCaptionClockSeconds = 0.0;
	/** 자막 시계를 마지막으로 흘린 월드의 멈춤 무관 시각. */
	double AudioCaptionClockSampledAt = -1.0;

	FVector FearCueWorldLocation = FVector::ZeroVector;
	double FearCueStartTime = 0.0;
	double FearCueEndTime = -1.0;

	/**
	 * Single-slot ripple state. One slot on purpose: footsteps report every
	 * footfall and a panicking heart every beat, so a per-event ring would
	 * strobe the screen edge. The louder event wins; quieter ones are dropped.
	 */
	FVector RippleWorldLocation = FVector::ZeroVector;
	float RippleRadiusCentimeters = 0.0f;
	float RippleLoudness = 0.0f;
	double RippleStartTime = 0.0;
	void DrawSaveIndicator(double CurrentTime);
	/**
	 * 밤의 시각. 목표 줄은 없지만 05:30이 온다는 것은 몸이 알아야 한다 —
	 * 잡혀도 시계는 계속 가고, 그 값이 곧 포획의 값이다(§5.4). 답도
	 * 게이지도 아니고 손목의 시계다. 왼쪽 아래, 저장 점의 반대편.
	 */
	void DrawNightClock();
	double SaveIndicatorEndTime = -1.0;
	double RippleEndTime = -1.0;
	/** §19.8. 이 링이 내가 낸 소리인가, 건물이 낸 소리인가. */
	bool bRippleIsForeign = false;

	double FirstPersonKnockStartTime = -1.0;
	double CaptureEmbraceStartTime = -1.0;
	double CaptureEmbraceEndTime = -1.0;
	double CaptureWakeEchoStartTime = -1.0;
	double CaptureWakeEchoEndTime = -1.0;
	FIGHudGuidance Guidance;
	FIGContextTips ContextTips;
	/** 지난 프레임에 아래쪽 줄(대사·소리 자막)이 차 있었는가. 조작 안내는 이 줄이 빌 때만 센다. */
	bool bLowerLaneBusyLastFrame = false;
	/** 조작 안내가 자막에 자리를 내줄 때 툭 끊기지 않게 따로 페이드한다. */
	float ControlsLaneAlpha = 1.0f;
	int32 SeenCompletedInteractions = 0;
	bool bControlsIntroRecorded = false;
	/** 기록된 출처 수. 처음 늘어날 때 기록 보기 안내를 띄운다. -1이면 아직 못 읽었다. */
	int32 SeenSourceCount = -1;
	TWeakObjectPtr<AIGListenerEntity> ListenerForTips;
	/** 막힌 밤의 세계 반응(90초 규칙)이 처음 일어날 때 힌트 키를 한 번 알려 준다. */
	TWeakObjectPtr<AIGMissingFloorMercyDirector> MercyForTips;
	TWeakObjectPtr<class AIGNightLoopDirector> NightLoopForNotes;
	bool bReadCaptureMercyNote = false;
	bool bEndCreditsActive = false;
#if !UE_BUILD_SHIPPING
	/** -IGEndCreditsPreview: 크레딧을 띄워 한 장 찍고 끝낸다. 언어별 글자 확인용. */
	bool bEndCreditsPreview = false;
	bool bEndCreditsPreviewShot = false;
	FString EndCreditsPreviewPath;
#endif
	/** 실제 시계(FPlatformTime) 기준. 크레딧 동안 월드가 멈춰 있어도 흐른다. */
	double EndCreditsStartTime = 0.0;
	double EndCreditsFinishTime = -1.0;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> EndCreditsBed;
	bool bReadDoorMercyNote = false;
	int32 SeenMercyResponses = -1;
	/** 첫 밤이 시작된 뒤 흐른 시간. 앉기 안내는 첫 소동이 가라앉은 뒤에 띄운다. */
	float NightTipClock = 0.0f;
	/**
	 * 잡혔다가 깨어날 때마다 오른다. 밤의 목표 키에 섞여서, 조작이 돌아오는
	 * 첫 프레임에 손목의 시계가 한 번 떠오른다 — 잡힌 값이 시간이라면 그
	 * 시간을 볼 수 있어야 한다(§5.4).
	 */
	int32 NightClockRevealSerial = 0;
#if !UE_BUILD_SHIPPING
	double FirstPersonKnockPreviewNextTime = 0.0;
	double CaptureEmbracePreviewNextTime = 0.0;
	double CaptureWakeEchoPreviewNextTime = 0.0;
	bool bFirstPersonKnockPreview = false;
	bool bCaptureEmbracePreview = false;
	bool bCaptureWakeEchoPreview = false;
#endif

	FText ChapterCardEyebrow;
	FText ChapterCardTitle;
	FText ChapterCardSubtitle;
	double ChapterCardStartTime = 0.0;
	double ChapterCardEndTime = -1.0;

	FVector2D FocusBracketMin = FVector2D::ZeroVector;
	FVector2D FocusBracketMax = FVector2D::ZeroVector;
	TWeakObjectPtr<AActor> FocusBracketTarget;
	float FocusBracketAlpha = 0.0f;
	float FocusBracketAcquireElapsed = 0.0f;
	double LastHudDrawTime = 0.0;
	FVector2D LayoutValidationCanvasSize = FVector2D::ZeroVector;
	FVector2D LayoutValidationBoundsMin = FVector2D::ZeroVector;
	FVector2D LayoutValidationBoundsMax = FVector2D::ZeroVector;
	int32 LayoutValidationElementCount = 0;
	uint64 LayoutValidationFrameSerial = 0;
	bool bLayoutValidationAllInsideCanvas = false;
	bool bLayoutValidationAllInsideSettingsContainers = false;
	bool bLayoutValidationSampleReady = false;
	bool bLayoutValidationEnabled = false;
	/**
	 * 번역 글자 점검. 글자끼리 겹치거나 화면·판 밖으로 나가면 TEXT_AUDIT 줄을
	 * 남긴다. 같은 문제는 한 번만 적는다. 언어·해상도·글자 크기를 바꿔 가며 돌린다.
	 */
	bool bTextAuditEnabled = false;
	/** 흘러가는 크레딧처럼 화면 밖에서 들어오는 글자는 화면 밖 판정에서 뺀다. */
	bool bTextAuditScrolling = false;
	struct FTextAuditEntry
	{
		FString Text;
		FVector2D Min = FVector2D::ZeroVector;
		FVector2D Max = FVector2D::ZeroVector;
		int32 Container = INDEX_NONE;
		bool bScrolling = false;
	};
	TArray<FTextAuditEntry> TextAuditEntries;
	TArray<FBox2D> TextAuditContainers;
	TArray<int32> TextAuditContainerStack;
	TSet<uint32> TextAuditReported;
	TArray<FString> TextAuditFailures;
	FVector2D TextAuditCanvasSize = FVector2D::ZeroVector;
	int32 AccessibilitySelectedRow = 0;
	double AccessibilityResetArmedUntil = -1.0;
	int32 SystemMenuSelectedRow = 0;
	int32 MissingFloorJournalPageIndex = 0;
	bool bNightPresentation = false;
	bool bSensoryInterludePresentation = false;
	bool bSensoryInterludeSkipAvailable = false;
	bool bSensoryInterludeSkipInProgress = false;
	bool bSensoryInterludeSkipToggleMode = false;
	float SensoryInterludeSkipProgress = 0.0f;
	float SensoryInterludeSkipHoldSeconds = 2.0f;
	/** 건너뛰기 칩이 처음 뜬 시각(FPlatformTime). 4초 뒤 걷히고, 누르는 동안만 다시 선다. */
	double SensoryInterludeSkipShownAt = -1.0;
	bool bMissingFloorFailureEndingVisible = false;
	bool bMissingFloorFailureRetryEnabled = false;
	double MissingFloorFailureEndingStartedAt = 0.0;
	EIGMissingFloorEpilogueScene MissingFloorEpilogueScene =
		EIGMissingFloorEpilogueScene::None;
	double MissingFloorEpilogueSceneStartedAt = 0.0;
	FText MissingFloorEpilogueHeading;
	TArray<FText> MissingFloorEpilogueBodyLines;
	FText MissingFloorEpilogueFootnote;
	bool bAccessibilityMenuVisible = false;
	bool bSystemMenuVisible = false;
	bool bMissingFloorJournalVisible = false;
	bool bSystemMenuIsTitle = false;
	bool bSystemMenuUseTitleBackdrop = false;
	bool bSystemMenuIsCredits = false;
	bool bSystemMenuIsContentNotice = false;
	bool bSystemMenuIsKeyBindings = false;
	int32 SystemMenuKeyBindingSelection = 0;
	bool bSystemMenuKeyBindingCapturing = false;
	bool bSystemMenuKeyBindingColumnGamepad = false;
	FText SystemMenuKeyBindingStatus;
	bool bSystemMenuKeyBindingStatusIsError = false;
	bool bSystemMenuIsAudioCalibration = false;
	bool bSystemMenuIsDisplaySettings = false;
	bool bSystemMenuCanContinue = false;
	bool bSystemMenuNightFiveAvailable = false;
	bool bSystemMenuNightFiveSpent = false;
	bool bSystemMenuNightFivePlaying = false;
	bool bSystemMenuConfirmNewGame = false;
	bool bSystemMenuHeadphoneRecommendation = false;
	bool bSystemMenuVSync = true;
	bool bDisplaySettingsApplied = false;
	bool bDisplaySettingsAwaitingConfirmation = false;
	bool bSystemMenuStatusIsError = false;
	int32 DisplaySettingsSelectedRow = 0;
	int32 AudioCalibrationSelectedRow = 0;
	int32 AudioCalibrationVolumeStep = 6;
	int32 AudioCalibrationBrightnessStep = 2;
	bool bSystemMenuAudioCalibrationFirstRun = false;
	bool bSystemMenuHeadphoneOutput = true;
	int32 SystemMenuAudioCalibrationMusicStep = 4;
	int32 SystemMenuAudioCalibrationAmbienceStep = 4;
	int32 DisplayWindowModeIndex = 0;
	int32 DisplayResolutionIndex = 1;
	int32 DisplayQualityIndex = 1;
	int32 DisplayFrameLimitIndex = 1;
	int32 DisplayConfirmationSecondsRemaining = 0;
	FText SystemMenuStatusText;
	double SystemMenuOpenedAt = -1.0;
	bool bUsingGamepad = false;
};
