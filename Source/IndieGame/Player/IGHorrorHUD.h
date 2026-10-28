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
	/** 두 엔딩 공통. 403호 문에 303호의 마지막 쪽지가 붙어 있다. */
	DoorNote,
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

/** 대사가 끝난 까닭. 음성과 다음 행동, 들은 기록이 이것을 보고 갈린다. */
enum class EIGDialogueEndReason : uint8
{
	/** 정한 시간(녹음된 말이면 그 소리)을 다 채웠다. */
	Completed,
	/** 플레이어가 넘겼다. */
	Skipped,
	/** 사건이 넘어가거나 장면이 바뀌어 거뒀다. */
	Cancelled,
	/** 화면에 오르기 전에 너무 오래 밀려 버렸다. */
	Expired
};

/** 상황이 바뀌었을 때 대사를 어떻게 다룰지. */
enum class EIGDialogueLifetime : uint8
{
	/** 이야기 대사. 사건 이름으로 거두거나 장면이 바뀔 때만 버린다. */
	Story,
	/** 지금 행동에 대한 짧은 반응. 오래 밀리거나 한참 가려졌다 돌아오면 버린다. */
	Reaction
};

/** 메뉴·기록·연출에 가려졌다가 다시 보일 때. */
enum class EIGDialogueInterruption : uint8
{
	/** 남은 시간부터 잇는다. */
	Resume,
	/** 처음부터 다시 보인다. */
	Restart,
	/** 버린다. */
	Drop
};

/**
 * 대사가 어느 장면·사건에서 나왔고 언제까지 유효한지. 비워 두면 예전처럼 시간만으로 다룬다.
 * 긴 대사를 나눈 다음 쪽도 같은 범위를 가진다.
 */
struct FIGDialogueScope
{
	/** 장면·사건 이름(예: Leftover.Box.Stuck). CancelDialogue로 한꺼번에 거둔다. */
	FName Context;
	/** 대사를 낸 쪽. 사라지면 대사도 거둔다(늦게 끝난 타이머가 지난 장면에 말을 넣지 않게). */
	TWeakObjectPtr<const UObject> Owner;
	/** 화면에 올리기 직전과 떠 있는 동안 본다. false면 거둔다. */
	TFunction<bool()> StillValid;
	/**
	 * 화면에 오른 뒤로는 StillValid로 거두지 않는다(밀린 줄만 거른다). 들고 가는 물건의 감상이나 한 번만 하는 경고는
	 * 플레이어가 그 자리를 떠나도 끝까지 읽혀야 한다.
	 */
	bool bKeepOnceShown = false;
	EIGDialogueLifetime Lifetime = EIGDialogueLifetime::Story;
	EIGDialogueInterruption Interruption = EIGDialogueInterruption::Resume;
	/** 마지막 쪽이 끝나거나 거둬질 때 한 번 부른다. 자막을 끈 녹음 대사처럼 화면에 오르지 않으면 부르지 않는다. */
	TFunction<void(EIGDialogueEndReason)> OnEnded;
	/** 녹음된 말이면 그 소리. 소리가 끝나면 자막도 끝나고, 가려져도 시간을 미루지 않는다. */
	TWeakObjectPtr<class UAudioComponent> Voice;
	/**
	 * 녹음된 말이면 그 줄이 화면에 오를 때 소리를 내는 함수(IGVoice가 만든다). 밀린 줄은
	 * 차례가 와야 소리가 나고, 거두거나 끊긴 줄은 소리도 멈춘다. 끊겼다가 다시 들려줄 때는
	 * 처음부터 다시 낸다.
	 */
	TFunction<class UAudioComponent*()> StartVoice;
	/**
	 * 마주 보고 하는 말이면 말하는 사람과 들리는 거리. 플레이어가 그 밖으로 나가면 그 사건의
	 * 떠 있는 줄과 밀린 줄을 함께 거둔다. bReplayIfInterrupted면 거둔 줄을 들고 있다가, 다시
	 * 말을 걸 때(ResumeInterruptedTalk) 남은 데부터 낸다. 끝 알림은 그때까지 미룬다.
	 */
	TWeakObjectPtr<const AActor> SpeakerActor;
	/** 말하는 사람이 액터가 아니면(계산대 너머의 나린) 그 자리. */
	TOptional<FVector> SpeakerLocation;
	float EarshotRadius = 0.0f;
	/**
	 * 말이 들리는 곳에 있는가. 주면 거리 대신 이것으로 판단한다. 가게처럼 벽과 유리로 나뉜 곳은 반지름이
	 * 문밖 골목까지 넘어가서, 가게를 나간 뒤에도 나린의 말이 이어졌다.
	 */
	TFunction<bool()> InEarshot;
	bool bReplayIfInterrupted = false;
};

/** Small value object kept outside UObject reflection to avoid per-line allocation churn. */
struct FIGDialogueMessage
{
	/** 나눈 쪽들이 공유하는 대사 번호. 같은 문장을 다른 장면에서 말해도 서로 거두지 않는다. */
	uint64 MessageSerial = 0;
	FText Speaker;
	FText Line;
	EIGDialogueChannel Channel = EIGDialogueChannel::InnerVoice;
	EIGDialoguePriority Priority = EIGDialoguePriority::Story;
	float MinimumDurationSeconds = 0.0f;
	double QueuedAt = 0.0;
	bool bContinuation = false;
	/** 화면 폭에 맞춰 나누기 전 원문. 들은 말 기록과 다시 들려주기가 쓴다. */
	FText FullLine;
	FIGDialogueScope Scope;
	/** 낼 때 주인이 있었는가. 처음부터 비어 있던 약한 포인터와 사라진 주인을 가린다. */
	bool bHasOwner = false;
	/** 녹음된 말이었는가. 소리가 사라져도(끝나도) 이 줄은 녹음 대사로 다룬다. */
	bool bVoiced = false;
	/** 화면에 올랐다(FIGDialogueScope::bKeepOnceShown). */
	bool bShown = false;
	/** 이 줄(나눈 줄이면 앞쪽)에서 소리를 이미 냈다. 다음 쪽이 같은 소리를 다시 내지 않게. */
	bool bVoiceStarted = false;
	/** 소리가 제 길이대로 끝날 시각. 그 전에 멎으면 끝까지 들은 것으로 치지 않는다. */
	double VoiceExpectedEnd = -1.0;
};

/** 소리 자막의 무게. 설정이 「중요한 소리」면 배경 소리는 글로 띄우지 않는다. */
enum class EIGSoundCaptionWeight : uint8
{
	/** 위협이 다가오거나, 대답이 오거나, 사건이 일어난 소리. 진행에 필요하다. */
	Important,
	/** 건물과 동네의 평범한 소리, 같은 몸이 되풀이하는 소리. 「모든 소리」에서만 보인다. */
	Background
};

/** Queued non-dialogue audio description; authored timings remain the source of truth. */
struct FIGAudioCaptionMessage
{
	/** 설정을 「중요한 소리」로 내리면 떠 있는 배경 소리 자막과 밀린 것도 같이 내린다. */
	EIGSoundCaptionWeight Weight = EIGSoundCaptionWeight::Important;
	/**
	 * 이 자막이 설명하는 실제 소리. 묶여 있으면 그 소리가 나는 동안 떠 있고, 제 길이를 다 내기 전에
	 * 멎으면(사건이 끊겨 소리를 거뒀으면) 바로 내린다. 다 울린 소리는 읽을 시간만큼만 남는다.
	 */
	TWeakObjectPtr<UAudioComponent> Sound;
	bool bBoundToSound = false;
	/**
	 * 자리를 아는데 소리를 넘겨받지 못한 자막. 같은 자리에서 막 난 소리를 찾아 묶는다. 자막을 소리보다
	 * 먼저 내는 곳이 있어 떠오른 뒤 잠깐 더 찾는다.
	 */
	bool bSeekSound = false;
	/** 묶인 소리가 제 길이대로 끝나는 자막 시계 시각. 반복하는 소리면 음수라 멎는 순간이 끝이다. */
	double SoundExpectedEnd = -1.0;
	/** 방향 딱지를 붙이기 전의 문구. 딱지는 그릴 때마다 지금 시점으로 다시 잰다. */
	FText Caption;
	float DurationSeconds = 0.0f;
	double QueuedAt = 0.0;
	/** 대사와 같은 사건 이름. CancelDialogue가 함께 거둔다. */
	FName Context;
	/** 소리가 난 자리와 들리는 거리. 0보다 크면 그 밖으로 벗어난 플레이어에게는 띄우지 않고, 떠 있던 것도 내린다. */
	TOptional<FVector> SourceLocation;
	float AudibleRadius = 0.0f;
	/** 소리 난 자리로 방향 딱지([위]·[뒤])와 방향 표시를 붙인다. */
	bool bShowBearing = false;
	/** 지금 그 소리가 들리는 곳인가(가게 안처럼 방으로 가르는 경우). 주면 거리보다 먼저 본다. */
	TFunction<bool()> Audible;
	/** 긴 자막을 나눈 다음 쪽. 앞쪽이 떠 있던 동안 밀렸다고 버리지 않는다. */
	bool bContinuation = false;
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
	/** H를 한 번 더 누르면 풀이가 나온다고 아래 줄에 띄운다(매번). */
	void ShowHintAnswerTip();

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

	/** 장면·사건 범위를 붙인 대사. 범위가 끝나거나 조건이 깨지면 밀린 줄과 떠 있는 줄을 거둔다. */
	static void PushDialogueScoped(
		const UObject* WorldContext,
		const FText& Speaker,
		const FText& Line,
		EIGDialogueChannel Channel,
		float MinimumDurationSeconds,
		EIGDialoguePriority Priority,
		FIGDialogueScope Scope);
	/** 범위를 붙인 혼잣말. */
	static void PushThoughtScoped(
		const UObject* WorldContext,
		const FText& Thought,
		float DurationSeconds,
		FIGDialogueScope Scope);
	/**
	 * H로 청한 힌트. 유담의 속말로 내지만, 플레이어가 직접 청한 말이라 떠 있는 말을 잠깐 미루고 바로 띄우고
	 * 줄이 차도 버리지 않는다. 녹음된 말이 떠 있으면 그 말은 끊긴다.
	 */
	static void PushHint(const UObject* WorldContext, const FText& Hint, float DurationSeconds = 3.2f);
	/** 범위를 붙인 소리 자막. CancelDialogue가 같은 이름의 대사와 함께 거둔다. */
	static void PushAudioCaptionScoped(
		const UObject* WorldContext,
		const FText& Caption,
		float DurationSeconds,
		const FVector& SourceLocation,
		FName Context,
		float AudibleRadius = 0.0f);
	/**
	 * 범위를 붙인 소리 자막인데 들리는 곳을 거리 대신 조건으로 판단한다(벽으로 나뉜 방).
	 * 가게 단말기처럼 진행과 상관없는 생활 소리는 Background로 낸다. 「모든 소리」에서만 보인다.
	 */
	static void PushAudioCaptionWhileAudible(
		const UObject* WorldContext,
		const FText& Caption,
		float DurationSeconds,
		const FVector& SourceLocation,
		FName Context,
		TFunction<bool()> Audible,
		EIGSoundCaptionWeight Weight = EIGSoundCaptionWeight::Important);
	/**
	 * 실제로 낸 소리에 묶은 자막. 그 소리가 지금 나고 있을 때만 세우고, 나는 동안은 떠 있다가 제 길이를
	 * 다 내기 전에 멎으면 거둔다. 다 울린 짧은 소리는 읽을 시간만큼 남는다. 앞부분만 쓰고 페이드로
	 * 거두는 소리는 PlannedSoundSeconds에 쓸 길이를 준다(0이면 소리 길이). 들리는 곳은 Audible이 있으면
	 * 그것으로, 없으면 AudibleRadius로 본다(0이면 소리 감쇠에 맡긴다). 오디오 장치가 아예 없는 PC에서는
	 * 소리를 낼 수 없으니 SourceLocation에서 난 소리로 보고 읽을 시간만큼 띄운다. 듣지 못하는 사람에게
	 * 자막까지 빼앗지 않는다.
	 */
	static void PushAudioCaptionForSound(
		const UObject* WorldContext,
		UAudioComponent* Sound,
		const FVector& SourceLocation,
		const FText& Caption,
		float DurationSeconds,
		EIGSoundCaptionWeight Weight = EIGSoundCaptionWeight::Important,
		FName Context = NAME_None,
		float AudibleRadius = 0.0f,
		float PlannedSoundSeconds = 0.0f,
		TFunction<bool()> Audible = nullptr);
	/** 그 사건의 대사와 소리 자막(떠 있는 것과 밀린 것)을 거둔다. */
	static void CancelDialogue(
		const UObject* WorldContext,
		FName Context,
		EIGDialogueEndReason Reason = EIGDialogueEndReason::Cancelled);
	/** 그 쪽이 낸 대사를 모두 거둔다(녹음기 중단 같은). */
	static void CancelDialogueFrom(const UObject* WorldContext, const UObject* Owner);
	/**
	 * 장면이 바뀌었다(밤이 시작하거나 날이 밝음, 붙잡힘, 그 밤 다시 하기). 떠 있는 대사와 밀린 대사,
	 * 소리 자막을 모두 거둔다. 새 장면의 대사는 이 뒤에 낸다.
	 */
	static void ResetDialogueForSceneChange(const UObject* WorldContext);
	/**
	 * 말하는 사람에게서 멀어져 끊긴 대화가 그 사건 이름으로 남아 있으면 남은 줄을 다시 낸다.
	 * 다시 낸 줄이 있으면 true. 대화를 여는 쪽이 새 대사를 고르기 전에 부른다.
	 */
	static bool ResumeInterruptedTalk(const UObject* WorldContext, FName Context);
	/**
	 * 지금 장면 표. 장면이 바뀌면(ResetDialogueForSceneChange) 거짓이 된다. 나중에 말을 낼 타이머를
	 * 걸 때 받아 두었다가 낼 때 본다. 장면이 바뀌며 화면의 줄은 지웠어도, 그 전에 걸린 타이머가
	 * 늦게 울려 지난 장면의 말을 새 장면에 넣는 것까지는 막지 못했다.
	 */
	static TFunction<bool()> MakeSceneGuard(const UObject* WorldContext);
	/** 화면이 실제로 그려지고 있다(그리는 프레임이 대사 줄을 넘긴다). 화면 없는 검사에서는 거짓이다. */
	static bool IsPresentingDialogue(const UObject* WorldContext);
	/** 그 사건의 줄이 떠 있거나 밀려 있다. */
	static bool HasDialogueInContext(const UObject* WorldContext, FName Context);
	/**
	 * 배경 혼잣말이 아닌 줄(이야기·중요)이 떠 있거나 밀려 있다. 05:30 새벽 유예가 본다. 새벽은 장면을
	 * 바꾸며 줄을 지우므로, 하던 말을 마칠 때까지 정해 둔 유예 안에서 기다린다.
	 */
	static bool IsHoldingStoryDialogue(const UObject* WorldContext);

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
	 * 건물과 동네의 평범한 소리처럼 진행에 쓰이지 않는 소리의 자막. 소리 자막을
	 * 「모든 소리」로 고른 사람에게만 보인다.
	 */
	static void PushBackgroundAudioCaptionAt(
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
	/**
	 * 지금 떠 있는 줄과 밀려 있는 줄이 다 지나가기까지 남은 시간. 대사 뒤에 이어지는 소리를 큐에
	 * 맞출 때 쓴다. HUD가 없으면 0이다.
	 */
	static float GetDialogueBacklogSeconds(const UObject* WorldContext);
	/**
	 * 결말 장면 글(머리글·본문·각주)을 다 읽는 데 드는 시간. 한꺼번에 떠오르므로 대사와 같은 읽기 속도로
	 * 한 번에 세고, 자막 표시 시간 설정을 따른다. 글이 없으면 0이다.
	 */
	static float EstimateEpilogueReadSeconds(const UObject* WorldContext, const FText& Heading,
		const TArray<FText>& Lines, const FText& Footnote);

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
	void ShowDialogueScoped(
		const FText& Speaker,
		const FText& Line,
		EIGDialogueChannel Channel,
		float MinimumDurationSeconds,
		EIGDialoguePriority Priority,
		FIGDialogueScope&& Scope);
	void ShowAudioCaption(const FText& Caption, float DurationSeconds);
	void ShowAudioCaptionInContext(
		const FText& Caption,
		float DurationSeconds,
		FName Context,
		const TOptional<FVector>& SourceLocation = TOptional<FVector>(),
		float AudibleRadius = 0.0f,
		bool bShowBearing = false,
		TFunction<bool()> Audible = nullptr,
		EIGSoundCaptionWeight Weight = EIGSoundCaptionWeight::Important,
		UAudioComponent* Sound = nullptr,
		float PlannedSoundSeconds = 0.0f);
	/**
	 * 묶인 소리가 지금 자막을 세워도 되는 상태인가. 묶이지 않은 자막은 늘 그렇다. 소리가 제 길이를 다
	 * 내기 전에 멎었으면(거둔 소리) 거짓이다.
	 */
	bool IsAudioCaptionSoundAlive(const FIGAudioCaptionMessage& Message, double CurrentTime) const;
	/**
	 * 떠 있는 자막이 아직 소리에 묶이지 않았으면 같은 자리에서 막 난 소리를 찾아 묶는다. 떠오른 뒤
	 * 0.35초까지 찾고, 그래도 없으면 소리 없이 뜬 자막으로 기록에 한 번 남긴다.
	 */
	void TryBindCurrentAudioCaptionSound(double CurrentTime);
	/** 소리 없이 뜬 것으로 이미 기록한 자막. 같은 줄을 되풀이해 남기지 않는다. */
	TSet<FString> LoggedUnboundAudioCaptions;
	/** 그 소리를 지금 플레이어가 들을 수 있는가. 조건이 있으면 조건, 없으면 거리. 자리나 거리를 모르면 들린다고 본다. */
	bool IsAudioCaptionInRange(const TOptional<FVector>& SourceLocation, float AudibleRadius,
		const TFunction<bool()>& Audible = nullptr) const;
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
	/**
	 * 서사가 소유하는 실패 연출이며 입력 처리는 밤 디렉터에 남긴다. bWallStillClosed는
	 * 붙잡히지 않고 벽이 닫힌 채 새벽을 맞은 길이다. 그 길의 후기는 두 사람의 노크를 말하지
	 * 않는다. 유담은 벽 안에 없다.
	 */
	void BeginMissingFloorFailureEnding(float InitialElapsedSeconds = 0.0f, bool bWallStillClosed = false);
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
	/** 검사용. 떠 있는 대사·소리 자막과 밀린 줄을 모두 걷는다(새 문장 진열 검사가 한 줄만 띄워 찍을 때). */
	void ClearDialogueForTesting()
	{
		ClearAllDialogueAndCaptions();
	}
	/** 검사용. 화면 없이 대사 큐를 그 시각으로 넘긴다(그리는 프레임이 하는 일과 같다). */
	void AdvanceDialogueForTesting(const double Time) { AdvanceDialogueQueue(Time); }
	/** 검사용. 명시적으로 줄을 나눈 대사를 한 쪽에 한 줄씩 넘긴다. 화면 렌더링은 하지 않는다. */
	void PrepareDialoguePageForTesting(const double Time) { PrepareDialoguePage(1.0f, 10000.0f, 1, Time); }
	/** 검사용. 메뉴·기록에 가려졌다가 다시 보이는 것을 흉내 낸다. */
	void SuspendDialogueForTesting(const double Time) { SuspendDialoguePresentation(Time); }
	void ResumeDialogueForTesting(const double Time) { ResumeDialoguePresentation(Time); }
	double GetDialogueEndTimeForTesting() const { return bHasCurrentDialogue ? DialogueEndTime : -1.0; }
	double GetDialogueStartTimeForTesting() const { return DialogueStartTime; }
	int32 GetQueuedDialogueCountForTesting() const { return DialogueQueue.Num() + (bHasCurrentDialogue ? 1 : 0); }
	/** 검사용. 떠 있는 줄의 녹음 소리. */
	class UAudioComponent* GetActiveDialogueVoiceForTesting() const
	{
		return bHasCurrentDialogue ? CurrentDialogue.Scope.Voice.Get() : nullptr;
	}
	/** 검사용. 떠 있는 줄이 화면에 그려지는가(음성 대사 자막을 끄면 소리만 난다). */
	bool IsActiveDialogueDrawnForTesting() const;
	/** 검사용. 소리 자막 줄을 그리는 프레임처럼 넘긴다. */
	void AdvanceAudioCaptionsForTesting() { AdvanceAudioCaptionQueue(AdvanceAudioCaptionClock()); }
	/** 검사용. 장면이 몇 번 바뀌었는가. */
	int32 GetDialogueSceneSerialForTesting() const { return DialogueSceneSerial; }
	/** 검사용. 화자에게서 멀어져 끊긴 채 들고 있는 대화가 있는가. */
	bool HasInterruptedTalkForTesting(FName Context) const { return InterruptedTalks.Contains(Context); }
	/** 검사용. 지금 떠 있거나 밀려 있는 줄 가운데 그 사건 이름의 것이 있는가. */
	bool HasDialogueInContextForTesting(FName Context) const
	{
		if (bHasCurrentDialogue && CurrentDialogue.Scope.Context == Context)
		{
			return true;
		}
		for (const FIGDialogueMessage& Queued : DialogueQueue)
		{
			if (Queued.Scope.Context == Context)
			{
				return true;
			}
		}
		return false;
	}
	/** 검사용. 밤 제목 카드를 바로 걷는다(화면을 찍는 검사가 카드가 끝나기를 기다리지 않게). */
	void ClearChapterCardForTesting()
	{
		ChapterCardTitle = FText::GetEmpty();
		ChapterCardEndTime = -1.0;
	}

	FText GetActiveDialogueLineForTesting() const
	{
		return bHasCurrentDialogue ? CurrentDialogue.Line : FText::GetEmpty();
	}
	FText GetActiveDialogueSpeakerForTesting() const
	{
		return bHasCurrentDialogue ? CurrentDialogue.Speaker : FText::GetEmpty();
	}
	/** 검사용. 지금 떠 있는 소리 자막(방향 딱지 없이, 나눈 줄이면 지금 쪽). */
	FText GetActiveAudioCaptionForTesting() const { return CurrentAudioCaption; }
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
	/** 기록장의 「조사 기록」 쪽수. 그 뒤에 「들은 말」 쪽이 붙는다. */
	int32 GetMissingFloorJournalTruthPageCount() const;
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
	/** EffectStrength는 외곽선·그림자의 진하기다. 조사 안내는 얇은 외곽선을 반쯤만 써서 밝은 바닥에서도 읽힌다. */
	void DrawLeftAlignedText(
		const FText& Text,
		const FVector2D& Position,
		const FLinearColor& Color,
		EIGHudTextRole TextRole,
		float TextScale = 1.0f,
		bool bUseOutline = false,
		float EffectStrength = 1.0f);
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
	/** 낸 쪽이 살아 있고 조건이 맞는가. */
	static bool IsDialogueStillValid(const FIGDialogueMessage& Message);
	/** 끝난 줄의 알림을 모아 둔다. 큐를 다 고친 뒤 FlushDialogueEndCallbacks로 부른다(알림이 새 줄을 낼 수 있다). */
	void EndDialogueMessage(FIGDialogueMessage& Message, EIGDialogueEndReason Reason);
	void FlushDialogueEndCallbacks();
	/** 떠 있는 줄이 아직 맞는지 보고, 녹음된 말이면 소리 길이에 자막을 맞춘다. */
	void ValidateCurrentDialogue(double CurrentTime);
	void CancelDialogueWhere(TFunctionRef<bool(const FIGDialogueMessage&)> Match, EIGDialogueEndReason Reason);
	void CancelAudioCaptionsIn(FName Context);
	void ClearAllDialogueAndCaptions();
	/** 마주 보고 하는 말인데 플레이어가 들리는 거리 밖에 있다. */
	bool IsOutOfEarshot(const FIGDialogueMessage& Message) const;
	/** 그 사건의 떠 있는 줄과 밀린 줄을 거둔다. 다시 들려줄 대화는 들고 있고, 아니면 끝낸다. */
	void InterruptTalk(FName Context);
	void HoldOrCancelInterrupted(FIGDialogueMessage&& Message);
	/** 끝까지 보인 대사를 기록장의 「들은 말」에 남긴다. 혼잣말과 짧은 반응은 남기지 않는다. */
	void RecordHeardLine(const FIGDialogueMessage& Message) const;
	/** 그 줄이 낸 소리를 멈추고 놓는다. 다시 들려줄 줄은 처음부터 다시 낸다. */
	static void StopDialogueVoice(FIGDialogueMessage& Message);
	/** 떠 있는 녹음 대사의 소리가 제 길이보다 먼저 멎었다(밖에서 멈췄다). */
	bool IsCurrentVoiceCutShort(double CurrentTime) const;
	static AIGHorrorHUD* FindHorrorHud(const UObject* WorldContext);
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
	/** 같은 소리로 볼 것인가. 문구가 같아도 사건이나 자리가 다르면 다른 소리다. */
	static bool IsSameAudioCaptionSource(
		FName ContextA,
		const TOptional<FVector>& SourceA,
		FName ContextB,
		const TOptional<FVector>& SourceB);
	/** 소리 자막 칸 왼쪽의 방향 표시. 자리를 알면 소리 난 쪽을 가리키고, 모르면 한가운데 점만 찍는다. */
	void DrawAudioCaptionBearingGlyph(const FVector2D& Center, float Radius, float Alpha);
	/** 떠 있는 대사의 녹음 소리가 실제로 나고 있는 동안 화자 이름 옆에 소리 크기를 그린다. */
	void DrawDialogueVoiceLevel(const FVector2D& Origin, float Height, float Alpha);
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
	/** 지금 묶인 키의 짧은 이름. 프롬프트와 힌트 줄이 읽는다. */
	FText GetBoundKeyLabel(EIGBindableAction Action, bool bGamepad) const;
	/** 겨눈 대상이 바뀌면 조사 안내를 잠깐 늦게, 짧게 맺히게 한다(스치는 시선마다 깜빡이지 않게). */
	void UpdateFocusReveal(AActor* FocusedActor, float DeltaSeconds);
	/**
	 * 조준점 아래의 조사 안내. 키 표시(얇은 테두리 안의 키 이름)와 동사 한 줄, 그 아래 작은 보조 줄.
	 * 길게 누르는 동안 키 표시의 테두리가 네 모서리부터 이어져 닫힌다. KeyLabel이 비면 동사만 둔다.
	 * bHoldAction이면 키 옆에 작은 「길게」를 붙이고, bHintIsHoldAction이면 보조 줄이 길게 눌러 하는
	 * 다른 동작이라 그 앞에 붙인다. 긴 조작 설명 문장 대신 쓰는 표시다.
	 */
	void DrawInteractionPrompt(
		const FText& Action,
		const FText& Hint,
		const FText& KeyLabel,
		float HoldProgress,
		float Top,
		float TextScale,
		float Alpha,
		bool bHoldAction = false,
		bool bHintIsHoldAction = false);
	/** 글자 둘레에 흐린 그림자를 여러 번 겹쳐 그린다. 굵은 외곽선 없이 밝은 바닥 위에서도 읽힌다. */
	void DrawPromptHalo(const FText& Text, const FVector2D& Position, EIGHudTextRole TextRole, float TextScale, float Alpha);
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

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> EpilogueDoorNoteTexture;

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
	/** 끝난 줄의 알림. 큐를 고치는 중에 부르지 않으려고 모아 둔다. */
	TArray<TPair<TFunction<void(EIGDialogueEndReason)>, EIGDialogueEndReason>> PendingDialogueEndCallbacks;
	/** 떠 있는 소리 자막의 사건 이름. */
	FName CurrentAudioCaptionContext;
	/** 화자에게서 멀어져 끊긴 대화. 사건 이름마다 남은 줄(끝 알림은 아직 부르지 않았다). */
	TMap<FName, TArray<FIGDialogueMessage>> InterruptedTalks;
	/** 장면이 바뀔 때마다 하나씩 오른다. MakeSceneGuard가 이것을 들고 간다. */
	int32 DialogueSceneSerial = 0;
	uint64 NextDialogueMessageSerial = 0;
	/** 마지막으로 그린 「들은 말」 쪽 수. 글자 크기와 화면 폭에 따라 달라서 그릴 때 센다. */
	mutable int32 JournalHeardPageCount = 0;
	TArray<FString> CurrentDialogueLines;
	bool bHasCurrentDialogue = false;
	bool bCurrentDialogueHasContinuation = false;
	double DialogueStartTime = 0.0;
	double DialogueEndTime = -1.0;
	/** 대사 시계를 마지막으로 본 시각. 끝난 줄이 화면에 몇 초 있었는지 로그에 남길 때 쓴다. */
	double LastDialogueClock = 0.0;
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
	/** 떠 있는 소리 자막의 자리와 들리는 거리. 그 밖으로 나가면 내린다. */
	TOptional<FVector> CurrentAudioCaptionSource;
	float CurrentAudioCaptionRadius = 0.0f;
	/** 떠 있는 소리 자막에 소리 난 쪽을 붙인다. 고개를 돌리면 딱지도 따라 바뀐다. */
	bool bCurrentAudioCaptionShowsBearing = false;
	/** 떠 있는 소리 자막이 들리는 곳인지 보는 조건(없으면 거리로 본다). */
	TFunction<bool()> CurrentAudioCaptionAudible;
	/** 떠 있는 자막을 이미 화면 폭에 맞춰 나눴다. 나눈 앞쪽을 다시 나누지 않는다. */
	bool bCurrentAudioCaptionPaged = false;
	/** 떠 있는 자막의 무게와 묶인 소리. 다음 쪽도 같은 소리라 그대로 물려받는다. */
	EIGSoundCaptionWeight CurrentAudioCaptionWeight = EIGSoundCaptionWeight::Important;
	TWeakObjectPtr<UAudioComponent> CurrentAudioCaptionSound;
	bool bCurrentAudioCaptionBound = false;
	bool bCurrentAudioCaptionSeekSound = false;
	double CurrentAudioCaptionSoundEnd = -1.0;
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

	TWeakObjectPtr<AActor> FocusRevealTarget;
	float FocusRevealAlpha = 0.0f;
	float FocusRevealElapsed = 0.0f;
	double LastHudDrawTime = 0.0;
	/** 마지막으로 프레임을 그린 실제 시각. 멈춘 화면(메뉴·기록장)에서도 흐른다. */
	double LastFrameDrawnRealSeconds = -1.0;
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
	/** 결말 C가 붙잡힌 길이 아니라 벽이 닫힌 새벽의 길이다. */
	bool bMissingFloorFailureEndingAtDawn = false;
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
