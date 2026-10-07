#include "Player/IGHorrorHUD.h"
#include "Player/IGInputBindingSubsystem.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "UnrealClient.h"
#include "Audio/IGAudioHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "CanvasItem.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Core/IGLanguageSubsystem.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorMercyDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGListenerTuning.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Fonts/CompositeFont.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IndieGame.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Interaction/IGHidingSpot.h"
#include "Interaction/IGReadableNote.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGInteractionComponent.h"
#include "Player/IGOnboardingMemory.h"
#include "Player/IGFrontendMenuLayout.h"
#include "Player/IGPlayerController.h"
#include "Player/IGPlayerCharacter.h"
#include "Sequence/IGObjectiveProvider.h"
#include "UObject/UObjectGlobals.h"

namespace IGHorrorHUD
{
	constexpr int32 HudRoundedMaskTextureSize = 64;
	constexpr int32 MaximumDialogueQueueDepth = 6;
	constexpr int32 MaximumAudioCaptionQueueDepth = 4;
	constexpr double StoryDialogueMaximumQueueAge = 14.0;
	constexpr double AmbientDialogueMaximumQueueAge = 6.0;
	constexpr float DialogueGlyphsPerSecond = 11.5f;

	/**
	 * 언어마다 한 글자에 담긴 말의 양이 달라 같은 줄을 읽는 속도가 다르다.
	 * 한국어 11.5자/초를 기준으로, 넷플릭스 자막 지침의 성인 읽기 속도 비율을
	 * 따라 영어는 빠르게, 일본어와 중국어는 느리게 센다.
	 */
	float GetReadingRateScale()
	{
		const FString Language = FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName();
		if (Language == TEXT("en"))
		{
			return 1.5f;
		}
		if (Language == TEXT("ja"))
		{
			return 0.6f;
		}
		if (Language == TEXT("zh"))
		{
			return 0.8f;
		}
		return 1.0f;
	}
	constexpr float DialogueMinimumSeconds = 2.2f;
	constexpr float DialogueMaximumSeconds = 9.0f;
	constexpr float FocusAcquireDelaySeconds = 0.09f;
	constexpr float FocusAcquireRevealSeconds = 0.09f;
	constexpr double FirstPersonKnockDurationSeconds = 0.22;
	constexpr int32 FirstPersonKnockFrameCount = 4;
	constexpr double CaptureEmbraceDurationSeconds = 1.2;

	/**
	 * The noise ripple (§5.1). One slot, deliberately short, with a minimum
	 * gap so a running player gets a pulse per few steps rather than a strobe.
	 */
	// §19.7 저장 표시. 점 하나 0.8초다. §23이 금지한 것은 「상시 표시」이지
	// 저장됐다는 사실 자체가 아니다 — 수동 슬롯이 없는 게임에서 아무 표시도
	// 없으면 「저장이 됐나」를 물을 데가 없다.
	constexpr double SaveIndicatorSeconds = 0.8;
	constexpr float SaveIndicatorRadius = 3.0f;
	constexpr double NoiseRippleDurationSeconds = 0.85;
	constexpr double NoiseRippleRetriggerSeconds = 0.34;
	/** Arc geometry: segment count, sweep at full carry, and stroke weight. */
	constexpr int32 NoiseRippleSegmentCount = 14;
	// 내 소리는 노크(§5.1 표 0.3) 이상일 때만 링을 그린다. 걷기(0.15)까지 그리면
	// 걷는 내내 화면 위쪽에 원호가 깜빡여, 처음 하는 사람에게는 화면 결함으로 보였다.
	constexpr float NoiseRippleMinimumOwnLoudness = 0.3f;
	constexpr float NoiseRippleMaximumSweepDegrees = 78.0f;
	constexpr float NoiseRippleMinimumSweepDegrees = 16.0f;
	constexpr float NoiseRippleMaximumThickness = 2.6f;

	// Native font sizes per text role. Presentation scale is applied once for the
	// current resolution and accessibility setting, then reused for measurement
	// and drawing so Korean wrapping stays pixel-consistent.
	constexpr int32 LargeFontSize = 24;
	constexpr int32 FrontendTitleFontSize = 64;
	constexpr int32 MediumFontSize = 20;
	constexpr int32 SmallFontSize = 18;
	constexpr int32 PhoneMetaFontSize = 15;

	const FLinearColor Shadow(0.0f, 0.0f, 0.0f, 0.9f);
	const FLinearColor PaleGray(0.82f, 0.84f, 0.82f, 0.95f);
	const FLinearColor MutedGray(0.62f, 0.64f, 0.62f, 0.9f);
	const FLinearColor RedAccent(0.72f, 0.08f, 0.06f, 1.0f);
	const FLinearColor ThoughtBlue(0.74f, 0.78f, 0.86f, 1.0f);
	const FLinearColor DialogueIvory(0.88f, 0.87f, 0.81f, 1.0f);
	const FLinearColor DialogueTeal(0.42f, 0.64f, 0.59f, 1.0f);

	// 타이틀은 빌라의 새벽빛과 글자만 남긴다. 경고 색은 실제 확인·오류에만 쓴다.
	const FLinearColor FrontendInk(0.018f, 0.024f, 0.025f, 1.0f);
	const FLinearColor FrontendIvory(0.89f, 0.88f, 0.83f, 1.0f);
	const FLinearColor FrontendMuted(0.55f, 0.57f, 0.54f, 1.0f);
	const FLinearColor FrontendOxide(0.62f, 0.25f, 0.21f, 1.0f);

	// 설정 전용 색은 의미 단위로 묶는다. 행마다 임의의 RGB를 넣지 않아야
	// 선택·경고·완료 상태의 대비를 한 곳에서 조정할 수 있다.
	const FLinearColor SettingsPanel(0.035f, 0.043f, 0.043f, 0.985f);
	const FLinearColor SettingsRail(0.024f, 0.030f, 0.030f, 0.97f);
	const FLinearColor SettingsRaised(0.072f, 0.083f, 0.081f, 0.96f);
	const FLinearColor SettingsSelected(0.115f, 0.132f, 0.128f, 0.98f);
	const FLinearColor SettingsDivider(0.22f, 0.24f, 0.23f, 0.52f);
	const FLinearColor SettingsPrimary(0.90f, 0.91f, 0.87f, 1.0f);
	const FLinearColor SettingsSecondary(0.61f, 0.64f, 0.61f, 0.96f);
	const FLinearColor SettingsAccent(0.76f, 0.26f, 0.20f, 1.0f);
	const FLinearColor SettingsSuccess(0.46f, 0.71f, 0.58f, 1.0f);

	enum class EJournalLane : int32
	{
		Administration = 0,
		Life = 1,
		Personal = 2,
	};

	enum class EJournalThumbnail : int32
	{
		Document = 0,
		Meter = 1,
		Plaster = 2,
		Tank = 3,
		Metal = 4,
	};

	struct FJournalEntryDefinition
	{
		FName SourceId;
		EJournalLane Lane = EJournalLane::Life;
		FText Title;
		FText Excerpt;
		FText WhereWhen;
		EJournalThumbnail Thumbnail = EJournalThumbnail::Document;
	};

	/**
	 * This table presents observation, never interpretation. Confirmation lines
	 * are derived later from the narrative snapshot, so UI copy cannot award a
	 * truth or silently become a second puzzle router.
	 */
	static const TArray<FJournalEntryDefinition>& JournalEntries()
	{
		static const TArray<FJournalEntryDefinition> Entries = {
			{TEXT("Lobby.MeterFifthDial"), EJournalLane::Administration,
				NSLOCTEXT("IGJournal", "MeterFifthDial.Title", "다섯 번째 계량기"),
				NSLOCTEXT("IGJournal", "MeterFifthDial.Excerpt", "복도등과는 따로 연결돼 있다. 이름표 없는 차단기를 올리면 원판이 돈다."),
				NSLOCTEXT("IGJournal", "MeterFifthDial.Where", "공동현관 계량기함 · 첫째 밤"), EJournalThumbnail::Meter},
			{TEXT("Office.MeterReadingSheet"), EJournalLane::Administration,
				NSLOCTEXT("IGJournal", "MeterReadingSheet.Title", "검침 기록지"),
				NSLOCTEXT("IGJournal", "MeterReadingSheet.Excerpt", "다섯 번째 칸. 63 · 58 · 61 · 0"),
				NSLOCTEXT("IGJournal", "MeterReadingSheet.Where", "관리실 사본 · 첫째 밤"), EJournalThumbnail::Document},
			{TEXT("Office.BoardDeliveryReceipt"), EJournalLane::Administration,
				NSLOCTEXT("IGJournal", "BoardDeliveryReceipt.Title", "자재 반입 영수증"),
				NSLOCTEXT("IGJournal", "BoardDeliveryReceipt.Excerpt", "석고보드 12.5T 12장씩, 7/26과 7/27"),
				NSLOCTEXT("IGJournal", "BoardDeliveryReceipt.Where", "관리실 책상 · 둘째 밤"), EJournalThumbnail::Document},
			{TEXT("Office.CarbonLedgerOriginal"), EJournalLane::Administration,
				NSLOCTEXT("IGJournal", "CarbonLedgerOriginal.Title", "민원 접수철 밑장"),
				NSLOCTEXT("IGJournal", "CarbonLedgerOriginal.Excerpt", "7/27 · 401호: 벽에서 쿵쿵. 사람 소리 같음."),
				NSLOCTEXT("IGJournal", "CarbonLedgerOriginal.Where", "관리실 책상 · 둘째 밤"), EJournalThumbnail::Document},
			{TEXT("Office.AgentMoveOutMessage"), EJournalLane::Administration,
				NSLOCTEXT("IGJournal", "AgentMoveOutMessage.Title", "부동산 문자 사본"),
				NSLOCTEXT("IGJournal", "AgentMoveOutMessage.Excerpt", "사장님, 옥탑 짐은 다 뺐습니다. (7/26 14:02)"),
				NSLOCTEXT("IGJournal", "AgentMoveOutMessage.Where", "관리실 책상 · 둘째 밤"), EJournalThumbnail::Document},
			{TEXT("Office.EvictionWarning"), EJournalLane::Administration,
				NSLOCTEXT("IGJournal", "EvictionWarning.Title", "퇴거 통보문"),
				NSLOCTEXT("IGJournal", "EvictionWarning.Excerpt", "시설 무단 조작으로 이번 주 안에 퇴거. 내일 07:00 옥상 누수 공사."),
				NSLOCTEXT("IGJournal", "EvictionWarning.Where", "4층 복도 · 넷째 날 낮"), EJournalThumbnail::Document},

			{TEXT("Forum.NoisePosts"), EJournalLane::Life,
				NSLOCTEXT("IGJournal", "NoisePosts.Title", "층간소음 게시글"),
				NSLOCTEXT("IGJournal", "NoisePosts.Excerpt", "6/30 새벽 네 시만 되면 위에서 뭘 질질 끕니다."),
				NSLOCTEXT("IGJournal", "NoisePosts.Where", "1층 게시판 인쇄본"), EJournalThumbnail::Document},
			{TEXT("Forum.FinalPost"), EJournalLane::Life,
				NSLOCTEXT("IGJournal", "FinalPost.Title", "마지막 게시글"),
				NSLOCTEXT("IGJournal", "FinalPost.Excerpt", "7/26 03:12 오늘은 올라가 봅니다. 사람인지 뭔지 얼굴이나 보죠."),
				NSLOCTEXT("IGJournal", "FinalPost.Where", "1층 게시판 인쇄본"), EJournalThumbnail::Document},
			{TEXT("Fifth.LandingImpactMark"), EJournalLane::Life,
				NSLOCTEXT("IGJournal", "LandingImpactMark.Title", "계단참에 남은 자국"),
				NSLOCTEXT("IGJournal", "LandingImpactMark.Excerpt", "철골 모서리와 바닥에 같은 검은 얼룩이 묻어 있다."),
				NSLOCTEXT("IGJournal", "LandingImpactMark.Where", "5층 계단참 · 셋째 밤"), EJournalThumbnail::Metal},
			{TEXT("Fifth.FreshPlasterDating"), EJournalLane::Life,
				NSLOCTEXT("IGJournal", "FreshPlasterDating.Title", "덧댄 벽"),
				NSLOCTEXT("IGJournal", "FreshPlasterDating.Excerpt", "안쪽 석고보드는 바싹 말랐는데, 바깥 실리콘은 아직 덜 굳었다."),
				NSLOCTEXT("IGJournal", "FreshPlasterDating.Where", "5층 공동벽 · 셋째 밤"), EJournalThumbnail::Plaster},
			{TEXT("Fifth.PipeWaterComparison"), EJournalLane::Life,
				NSLOCTEXT("IGJournal", "PipeWaterComparison.Title", "배관에서 들은 소리"),
				NSLOCTEXT("IGJournal", "PipeWaterComparison.Excerpt", "한쪽에서는 물이 흐르고, 다른 쪽에서는 속이 빈 듯한 소리가 난다."),
				NSLOCTEXT("IGJournal", "PipeWaterComparison.Where", "5층 점검구 쪽 벽 · 셋째 밤"), EJournalThumbnail::Metal},
			{TEXT("Fifth.WallEchoByHand"), EJournalLane::Life,
				NSLOCTEXT("IGJournal", "WallEchoByHand.Title", "직접 두드린 벽"),
				NSLOCTEXT("IGJournal", "WallEchoByHand.Excerpt", "한쪽 벽만 오래 울린다. 다른 벽은 두드리면 소리가 금방 끊긴다."),
				NSLOCTEXT("IGJournal", "WallEchoByHand.Where", "5층 · 셋째 밤"), EJournalThumbnail::Plaster},
			{TEXT("Unit401.KnockTallyJournal"), EJournalLane::Life,
				NSLOCTEXT("IGJournal", "KnockTallyJournal.Title", "황순금 소리 일지"),
				NSLOCTEXT("IGJournal", "KnockTallyJournal.Excerpt", "7/27 위에서 다섯 번 · 7/31 오늘은 세 번."),
				NSLOCTEXT("IGJournal", "KnockTallyJournal.Where", "401호 · 셋째 날 낮"), EJournalThumbnail::Document},
			{TEXT("Roof.TankWaterAudition"), EJournalLane::Life,
				NSLOCTEXT("IGJournal", "TankWaterAudition.Title", "물탱크 안내판"),
				NSLOCTEXT("IGJournal", "TankWaterAudition.Excerpt", "용량 2,000 L. 물 높이를 가리키는 바늘이 위쪽에 있었다."),
				NSLOCTEXT("IGJournal", "TankWaterAudition.Where", "옥상 · 셋째 밤"), EJournalThumbnail::Tank},
			{TEXT("Fifth.AnswerReturned"), EJournalLane::Life,
				NSLOCTEXT("IGJournal", "AnswerReturned.Title", "벽 너머에서 돌아온 소리"),
				NSLOCTEXT("IGJournal", "AnswerReturned.Excerpt", "둘, 쉬고, 하나."),
				NSLOCTEXT("IGJournal", "AnswerReturned.Where", "5층 공동벽 · 셋째 밤"), EJournalThumbnail::Plaster},
			{TEXT("Fifth.BreakerCutIntervention"), EJournalLane::Life,
				NSLOCTEXT("IGJournal", "BreakerCutIntervention.Title", "나간 전기"),
				NSLOCTEXT("IGJournal", "BreakerCutIntervention.Excerpt", "망치 세 번째에 5층 불이 나갔다."),
				NSLOCTEXT("IGJournal", "BreakerCutIntervention.Where", "5층 · 넷째 밤"), EJournalThumbnail::Metal},

			{TEXT("Estate.ShippingLabels"), EJournalLane::Personal,
				NSLOCTEXT("IGJournal", "ShippingLabels.Title", "배송 라벨"),
				NSLOCTEXT("IGJournal", "ShippingLabels.Excerpt", "받는 분 백도하 / 무영로 27-3 달빛빌라 옥탑"),
				NSLOCTEXT("IGJournal", "ShippingLabels.Where", "403호 책상 · 입주일"), EJournalThumbnail::Document},
			{TEXT("Fifth.TunerNotebookName"), EJournalLane::Personal,
				NSLOCTEXT("IGJournal", "TunerNotebookName.Title", "조율 수첩"),
				NSLOCTEXT("IGJournal", "TunerNotebookName.Excerpt", "백도하 조율 수첩"),
				NSLOCTEXT("IGJournal", "TunerNotebookName.Where", "5층 벽 틈 · 셋째 밤"), EJournalThumbnail::Document},
			{TEXT("Fifth.TunerWorkSchedule"), EJournalLane::Personal,
				NSLOCTEXT("IGJournal", "TunerWorkSchedule.Title", "작업 시간표"),
				NSLOCTEXT("IGJournal", "TunerWorkSchedule.Excerpt", "월 서초 공연장 · 공연 끝나고 조율 02:30"),
				NSLOCTEXT("IGJournal", "TunerWorkSchedule.Where", "조율 수첩 · 셋째 밤"), EJournalThumbnail::Document},
			{TEXT("Fifth.PipeAuditionCriterion"), EJournalLane::Personal,
				NSLOCTEXT("IGJournal", "PipeAuditionCriterion.Title", "소리를 적어 둔 메모"),
				NSLOCTEXT("IGJournal", "PipeAuditionCriterion.Excerpt", "옥탑 벽 확인: 빈 곳은 낮게 울리고 소리가 오래 감."),
				NSLOCTEXT("IGJournal", "PipeAuditionCriterion.Where", "조율 수첩 여백 · 셋째 밤"), EJournalThumbnail::Document},
			{TEXT("Phone.AnswerRhythmVoicemail"), EJournalLane::Personal,
				NSLOCTEXT("IGJournal", "AnswerRhythmVoicemail.Title", "마지막 음성 메시지"),
				NSLOCTEXT("IGJournal", "AnswerRhythmVoicemail.Excerpt", "문 두드리면 알지? 둘, 하나."),
				NSLOCTEXT("IGJournal", "AnswerRhythmVoicemail.Where", "휴대폰 · 입주 전"), EJournalThumbnail::Metal},
			{TEXT("Fifth.AnswerRhythmNotebook"), EJournalLane::Personal,
				NSLOCTEXT("IGJournal", "AnswerRhythmNotebook.Title", "수첩에 그려진 박자"),
				NSLOCTEXT("IGJournal", "AnswerRhythmNotebook.Excerpt", "●●  —  ●"),
				NSLOCTEXT("IGJournal", "AnswerRhythmNotebook.Where", "조율 수첩 여백 · 셋째 밤"), EJournalThumbnail::Document},
			{TEXT("Unit401.AnswerRhythmJournal"), EJournalLane::Personal,
				NSLOCTEXT("IGJournal", "AnswerRhythmJournal.Title", "일지에 적힌 박자"),
				NSLOCTEXT("IGJournal", "AnswerRhythmJournal.Excerpt", "7/29 저쪽이 하던 대로 둘, 쉬고, 하나. 그랬더니 조용하데."),
				NSLOCTEXT("IGJournal", "AnswerRhythmJournal.Where", "401호 · 셋째 날 낮"), EJournalThumbnail::Document},
		};
		return Entries;
	}

	static float SmoothStep01(const float Value)
	{
		const float Clamped = FMath::Clamp(Value, 0.0f, 1.0f);
		return Clamped * Clamped * (3.0f - 2.0f * Clamped);
	}
}

void AIGHorrorHUD::BeginPlay()
{
	Super::BeginPlay();
	bLayoutValidationEnabled = FParse::Param(
		FCommandLine::Get(),
		TEXT("IGFrontendShippingProbe"))
		|| FParse::Param(
			FCommandLine::Get(),
			TEXT("IGAudioCalibrationPreview"))
		|| FParse::Param(
			FCommandLine::Get(),
			TEXT("IGMissingFloorEndingPreview"));
	InitializeKoreanFont();
	CultureChangedHandle = FInternationalization::Get().OnCultureChanged().AddUObject(
		this, &AIGHorrorHUD::HandleCultureChanged);
	InitializeFrontendMenuTextures();
	// 이 프로필은 첫 조작 안내를 이미 봤다. 새 게임을 다시 시작해도 조작표를 또 펼치지 않는다.
	if (IGOnboardingMemory::IsControlsIntroDone())
	{
		Guidance.SkipTutorial();
		bControlsIntroRecorded = true;
	}

	// Optional: absent until Scripts/Prepare-AIArt.ps1 has produced it, in
	// which case the reading panel falls back to a flat fill.
	NotePaperTexture = LoadObject<UTexture2D>(
		nullptr, TEXT("/Game/Prototype/Textures/T_PaperClean_V2_D.T_PaperClean_V2_D"));
	InitializeDialogueSurfaceTextures();
	InitializeAudioCalibrationTexture();
	InitializeMissingFloorJournalTextures();
	InitializeFirstPersonActionTextures();
#if !UE_BUILD_SHIPPING
	if (FParse::Value(FCommandLine::Get(), TEXT("IGEndCreditsPreview="), EndCreditsPreviewPath))
	{
		EndCreditsPreviewPath.TrimQuotesInline();
		bEndCreditsPreview = true;
		StartEndCredits();
	}
	bFirstPersonKnockPreview = FParse::Param(
		FCommandLine::Get(),
		TEXT("IGM0KnockPreview"));
	bCaptureEmbracePreview = FParse::Param(
		FCommandLine::Get(),
		TEXT("IGM1CapturePreview"));
	bCaptureWakeEchoPreview = FParse::Param(
		FCommandLine::Get(),
		TEXT("IGM1WakeEchoPreview"));
#endif
	bTextAuditEnabled = FParse::Param(FCommandLine::Get(), TEXT("IGTextAudit"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGSettingsLayoutReview"));

	ResolveInteractionComponent();

	if (UWorld* World = GetWorld())
	{
		// The noise bus is a world subsystem, not a game-instance one: every
		// other subsystem lookup in this file goes through the game instance
		// and would silently return null here.
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			NoiseSubsystem = Noise;
			NoiseReportedHandle = Noise->OnNoiseReported.AddUObject(
				this, &AIGHorrorHUD::HandleNoiseReported);
			// 그의 노크·대답·추격·다가오는 걸음은 소음 버스에 없다. 대체 채널용
			// 신호로 따로 오고, 링은 켠 사람에게만 가늘게 그린다.
			PresentationCueHandle = Noise->OnPresentationCue.AddUObject(
				this, &AIGHorrorHUD::HandleNoiseReported);
		}
	}
	if (const AIGPlayerController* IndieController =
		Cast<AIGPlayerController>(GetOwningPlayerController()))
	{
		IndieController->RefreshMenuHud();
	}
}

void AIGHorrorHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FInternationalization::Get().OnCultureChanged().Remove(CultureChangedHandle);
	CultureChangedHandle.Reset();
	if (UIGNoiseSubsystem* Noise = NoiseSubsystem.Get())
	{
		Noise->OnNoiseReported.Remove(NoiseReportedHandle);
		Noise->OnPresentationCue.Remove(PresentationCueHandle);
	}
	NoiseReportedHandle.Reset();
	PresentationCueHandle.Reset();
	NoiseSubsystem = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AIGHorrorHUD::HandleNoiseReported(const FIGNoiseEvent& Event)
{
	// Only the player's own sounds get a ring. 존재의 소리는 소음 버스가 아니라
	// OnPresentationCue로 따로 오고, 버스에는 연출용 미끼 소리도 실린다. 그녀가
	// 내지 않은 소리에 「네가 냈다」는 링을 그리면 틀린 규칙을 가르친다.
	//
	// §19.8 두드리는 소리를 화면에 표시를 켜면 그 규칙이 뒤집힌다. 소리를 못 듣는 손에게
	// 존재의 노크는 아무것도 아닌 것이 되므로, 링을 그리되 **두께로** 나눠
	// 내 소리와 구분한다 — 색으로만 나누면 색각에서 다시 사라진다.
	const APawn* OwningPawn = GetOwningPawn();
	const bool bMine = OwningPawn && Event.Instigator.Get() == OwningPawn;
	const UIGAccessibilitySubsystem* RippleAccessibility =
		GetGameInstance()
			? GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>()
			: nullptr;
	const bool bSubstituting = !bMine
		&& RippleAccessibility
		&& RippleAccessibility->UsesKnockRippleSubstitute();
	if (!bMine && !bSubstituting)
	{
		return;
	}
	if (Event.Loudness <= 0.0f)
	{
		return;
	}
	// 조용한 동작(걷기, 천천히 여닫기, 쪽지)은 발소리로만 듣는다. 마스킹 뒤 값이라
	// 냉장고 곁에서 낸 노크도 묻히면 링이 없다.
	if (bMine && Event.Loudness + KINDA_SMALL_NUMBER < IGHorrorHUD::NoiseRippleMinimumOwnLoudness)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// 이벤트 시각과 같은 게임 시간이다. HUD의 다른 타이머도 모두 게임 시간이라
	// 일시정지 뒤에도 나이가 어긋나지 않는다.
	const double Now = World->GetTimeSeconds();
	const bool bRippleLive = Now < RippleEndTime;
	if (bRippleLive)
	{
		const bool bRetriggerBlocked =
			Now - RippleStartTime < IGHorrorHUD::NoiseRippleRetriggerSeconds;
		// A quieter sound never interrupts a louder ring, and nothing
		// interrupts a ring that only just started.
		if (bRetriggerBlocked || Event.Loudness <= RippleLoudness)
		{
			return;
		}
	}

	RippleWorldLocation = Event.Location;
	RippleRadiusCentimeters = Event.Radius;
	RippleLoudness = Event.Loudness;
	RippleStartTime = Now;
	RippleEndTime = Now + IGHorrorHUD::NoiseRippleDurationSeconds;
	bRippleIsForeign = !bMine;
}

bool AIGHorrorHUD::IsNoiseRippleActive() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < RippleEndTime;
}

void AIGHorrorHUD::InitializeDialogueSurfaceTextures()
{
	DialogueFilmTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Game/Prototype/Textures/T_HudDialogueFilm_D.T_HudDialogueFilm_D"));

	constexpr int32 TextureSize = IGHorrorHUD::HudRoundedMaskTextureSize;
	constexpr float SourceRadius = TextureSize * 0.25f;
	TArray64<uint8> PixelBytes;
	PixelBytes.SetNumZeroed(TextureSize * TextureSize * sizeof(FColor));
	FColor* Pixels = reinterpret_cast<FColor*>(PixelBytes.GetData());
	for (int32 Row = 0; Row < TextureSize; ++Row)
	{
		for (int32 Column = 0; Column < TextureSize; ++Column)
		{
			const FVector2D PixelCenter(
				static_cast<float>(Column) + 0.5f,
				static_cast<float>(Row) + 0.5f);
			const FVector2D NearestCornerCenter(
				FMath::Clamp(PixelCenter.X, SourceRadius, TextureSize - SourceRadius),
				FMath::Clamp(PixelCenter.Y, SourceRadius, TextureSize - SourceRadius));
			const float SignedDistance =
				(PixelCenter - NearestCornerCenter).Size() - SourceRadius;
			const float Coverage = 1.0f - IGHorrorHUD::SmoothStep01(
				(SignedDistance + 1.0f) * 0.5f);
			Pixels[Column + Row * TextureSize] = FLinearColor(
				1.0f,
				1.0f,
				1.0f,
				Coverage).ToFColorSRGB();
		}
	}

	const FName TextureName = MakeUniqueObjectName(
		GetTransientPackage(),
		UTexture2D::StaticClass(),
		TEXT("HudRoundedMask"));
	HudRoundedMaskTexture = UTexture2D::CreateTransient(
		TextureSize,
		TextureSize,
		PF_B8G8R8A8,
		TextureName,
		PixelBytes);
	if (HudRoundedMaskTexture)
	{
		HudRoundedMaskTexture->Filter = TF_Bilinear;
		HudRoundedMaskTexture->AddressX = TA_Clamp;
		HudRoundedMaskTexture->AddressY = TA_Clamp;
		HudRoundedMaskTexture->NeverStream = true;
		HudRoundedMaskTexture->UpdateResource();
	}
}

void AIGHorrorHUD::InitializeFrontendMenuTextures()
{
	FrontendTitleBackgroundTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Game/UI/Textures/T_TitleBackground_D.T_TitleBackground_D"));
	if (!FrontendTitleBackgroundTexture)
	{
		UE_LOG(
			LogIndieGame,
			Warning,
			TEXT("Title key art is unavailable; front end will use its safe fallback."));
	}

	// §9 에필로그의 세 정지 화면. 없으면 그 장면은 글자만 남는다.
	EpilogueWorkshopTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Game/UI/Textures/T_EpilogueWorkshop_D.T_EpilogueWorkshop_D"));
	EpilogueAutumnTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Game/UI/Textures/T_EpilogueAutumn_D.T_EpilogueAutumn_D"));
	EpilogueServiceBayTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Game/UI/Textures/T_EpilogueServiceBay_D.T_EpilogueServiceBay_D"));
	if (!EpilogueWorkshopTexture || !EpilogueAutumnTexture
		|| !EpilogueServiceBayTexture)
	{
		UE_LOG(
			LogIndieGame,
			Warning,
			TEXT("Epilogue stills are unavailable; those scenes draw text only."));
	}

	constexpr int32 ShadeWidth = 256;
	TArray64<uint8> PixelBytes;
	PixelBytes.SetNumZeroed(ShadeWidth * sizeof(FColor));
	FColor* Pixels = reinterpret_cast<FColor*>(PixelBytes.GetData());
	for (int32 Column = 0; Column < ShadeWidth; ++Column)
	{
		const float U = (Column + 0.5f) / ShadeWidth;
		const float Coverage = FMath::Pow(1.0f - U, 2.15f);
		Pixels[Column] = FColor(
			0,
			0,
			0,
			FMath::RoundToInt(255.0f * Coverage));
	}
	const FName ShadeName = MakeUniqueObjectName(
		GetTransientPackage(),
		UTexture2D::StaticClass(),
		TEXT("FrontendShade"));
	FrontendShadeTexture = UTexture2D::CreateTransient(
		ShadeWidth,
		1,
		PF_B8G8R8A8,
		ShadeName,
		PixelBytes);
	if (FrontendShadeTexture)
	{
		FrontendShadeTexture->Filter = TF_Bilinear;
		FrontendShadeTexture->AddressX = TA_Clamp;
		FrontendShadeTexture->AddressY = TA_Clamp;
		FrontendShadeTexture->NeverStream = true;
		FrontendShadeTexture->UpdateResource();
	}
}

void AIGHorrorHUD::InitializeAudioCalibrationTexture()
{
	AudioCalibrationWallTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Game/Prototype/Textures/T_AudioCalibrationWall_D."
			"T_AudioCalibrationWall_D"));
}

void AIGHorrorHUD::InitializeMissingFloorJournalTextures()
{
	MissingFloorJournalTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Game/Prototype/Textures/T_MissingFloorJournalPaper_D."
			"T_MissingFloorJournalPaper_D"));
	JournalMeterTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Game/Prototype/Textures/T_MeterBox_D.T_MeterBox_D"));
	JournalPlasterTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Game/Prototype/Textures/T_MissingFloorDryPlaster_D."
			"T_MissingFloorDryPlaster_D"));
	// 썸네일은 증거가 나온 자리에서 플레이어가 본 표면이어야 한다. 옥상
	// 저수조는 M_UtilityTankSteel의 새틴 강판이다.
	JournalTankTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Game/Prototype/Textures/T_UtilityTankSteel_D."
			"T_UtilityTankSteel_D"));
	// 5층 계단참과 1층 분전반의 금속은 전부 CC0 사진 캡처를 쓴다.
	// create_textured_materials._load_texture가 절차 텍스처보다 캡처를
	// 먼저 고르므로 T_MetalBrushed_D가 아니라 이쪽이다.
	JournalMetalTexture = LoadObject<UTexture2D>(
		nullptr,
		TEXT("/Game/Prototype/Textures/T_Photo_MetalBrushed_D."
			"T_Photo_MetalBrushed_D"));
}

void AIGHorrorHUD::InitializeFirstPersonActionTextures()
{
	static const TCHAR* KnockTexturePaths[] = {
		TEXT("/Game/Prototype/Textures/T_FPHandKnock0_D.T_FPHandKnock0_D"),
		TEXT("/Game/Prototype/Textures/T_FPHandKnock1_D.T_FPHandKnock1_D"),
		TEXT("/Game/Prototype/Textures/T_FPHandKnock2_D.T_FPHandKnock2_D"),
		TEXT("/Game/Prototype/Textures/T_FPHandKnock3_D.T_FPHandKnock3_D"),
	};
	FirstPersonKnockFrames.SetNum(IGHorrorHUD::FirstPersonKnockFrameCount);
	for (int32 FrameIndex = 0;
		FrameIndex < IGHorrorHUD::FirstPersonKnockFrameCount;
		++FrameIndex)
	{
		FirstPersonKnockFrames[FrameIndex] = LoadObject<UTexture2D>(
			nullptr,
			KnockTexturePaths[FrameIndex]);
	}

}

void AIGHorrorHUD::PlayFirstPersonKnock()
{
	if (const UWorld* World = GetWorld())
	{
		FirstPersonKnockStartTime = World->GetTimeSeconds();
	}
	// 두드리기 안내를 보고 두드렸다. 아무 데나 두드린 것은 세지 않는다.
	if (!InteractionComponent.IsValid())
	{
		ResolveInteractionComponent();
	}
	const UIGInteractionComponent* Interaction = InteractionComponent.Get();
	const AActor* Focused = Interaction ? Interaction->GetFocusedActor() : nullptr;
	if (Focused && Focused->ActorHasTag(FName(TEXT("MissingFloor.Verb.Knock"))))
	{
		IGOnboardingMemory::AddPromptUse(TEXT("Knock"));
	}
}

void AIGHorrorHUD::PlayCaptureEmbrace(const float DurationSeconds)
{
	Guidance.Interrupt();
	if (const UWorld* World = GetWorld())
	{
		CaptureEmbraceStartTime = World->GetTimeSeconds();
		CaptureEmbraceEndTime = CaptureEmbraceStartTime + FMath::Max(
			0.05f,
			DurationSeconds);
	}
}

void AIGHorrorHUD::PlayCaptureWakeEcho(
	const int32 CaptureCount,
	const float VisualDurationSeconds,
	const float OwnershipDurationSeconds)
{
	if (const UWorld* World = GetWorld())
	{
		const float SafeVisualDuration = FMath::Max(0.05f, VisualDurationSeconds);
		const float SafeOwnershipDuration = FMath::Max(
			SafeVisualDuration,
			OwnershipDurationSeconds);
		CaptureWakeEchoStartTime = World->GetTimeSeconds();
		CaptureWakeEchoEndTime =
			CaptureWakeEchoStartTime + SafeOwnershipDuration;
		Guidance.Interrupt();
		// 조작이 돌아오는 첫 프레임에 키가 바뀐 것으로 읽혀 시계가 7초 뜬다.
		++NightClockRevealSerial;
	}
}

UFontFace* AIGHorrorHUD::LoadBundledFontFace(
	const TCHAR* RelativePath,
	const TCHAR* FontFaceName)
{
	FString FontPath = FPaths::Combine(
		FPlatformProcess::BaseDir(),
		TEXT("UI/Fonts"),
		RelativePath);
	if (!FPaths::FileExists(FontPath))
	{
		// Editor targets keep authored binaries beside the module instead of
		// copying them into the project root. Shipping stages the same files
		// next to the executable through RuntimeDependencies.
		FontPath = FPaths::Combine(
			FPaths::ProjectDir(),
			TEXT("Source/IndieGame/UI/Fonts"),
			RelativePath);
	}
	TArray<uint8> FontBytes;
	if (!FPaths::FileExists(FontPath)
		|| !FFileHelper::LoadFileToArray(FontBytes, *FontPath))
	{
		return nullptr;
	}

	UFontFace* FontFace = NewObject<UFontFace>(this, FontFaceName);
	FontFace->LoadingPolicy = EFontLoadingPolicy::Inline;
	FontFace->Hinting = EFontHinting::Default;
	FontFace->SourceFilename = FontPath;
	FontFace->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(FontBytes));
	return FontFace;
}

UFont* AIGHorrorHUD::MakeRuntimeFont(
	UFontFace* FontFace,
	const int32 PixelSize,
	const TCHAR* FontName)
{
	if (!FontFace)
	{
		return nullptr;
	}
	UFont* RuntimeFont = NewObject<UFont>(this, MakeUniqueObjectName(this, UFont::StaticClass(), FontName));
	RuntimeFont->FontCacheType = EFontCacheType::Runtime;
	RuntimeFont->LegacyFontSize = PixelSize;
	FCompositeFont& Composite = RuntimeFont->GetMutableInternalCompositeFont();
	FTypefaceEntry& TypefaceEntry = Composite.DefaultTypeface.Fonts.AddDefaulted_GetRef();
	TypefaceEntry.Name = TEXT("Regular");
	TypefaceEntry.Font = FFontData(FontFace);
	if (ActiveCjkFontFace)
	{
		// 한국어 글꼴에는 한자가 없고, 일본어 가나는 있지만 모양이 다르다.
		// 한자, 가나, 전각 문장 부호를 그 언어의 글꼴로 넘긴다.
		FCompositeSubFont& SubFont = Composite.SubTypefaces.AddDefaulted_GetRef();
		FTypefaceEntry& CjkEntry = SubFont.Typeface.Fonts.AddDefaulted_GetRef();
		CjkEntry.Name = TEXT("Regular");
		CjkEntry.Font = FFontData(ActiveCjkFontFace.Get(), ActiveCjkSubFaceIndex);
		const TPair<int32, int32> Ranges[] = {
			{0x3000, 0x303F},  // 전각 문장 부호
			{0x3040, 0x30FF},  // 히라가나, 가타카나
			{0x3100, 0x312F},  // 주음부호
			{0x31F0, 0x31FF},  // 가타카나 확장
			{0x3400, 0x4DBF},  // 한자 확장 A
			{0x4E00, 0x9FFF},  // 한자
			{0xF900, 0xFAFF},  // 호환 한자
			{0xFE30, 0xFE4F},  // 세로쓰기 호환 문장 부호
			{0xFF00, 0xFFEF},  // 전각·반각 형태
		};
		for (const TPair<int32, int32>& Range : Ranges)
		{
			SubFont.CharacterRanges.Add(FInt32Range(
				FInt32Range::BoundsType::Inclusive(Range.Key),
				FInt32Range::BoundsType::Inclusive(Range.Value)));
		}
	}
	return RuntimeFont;
}

UFontFace* AIGHorrorHUD::LoadCjkFontFace(const FString& Culture)
{
	if (const TObjectPtr<UFontFace>* Cached = CjkFontFaces.Find(Culture))
	{
		return Cached->Get();
	}
	struct FCandidate
	{
		const TCHAR* Culture;
		const TCHAR* Bundled;
		const TCHAR* System[3];
		int32 SystemFace[3];
	};
	// 번들은 OFL인 Noto Sans 지역 서브셋이다. 시스템 글꼴은 개발 PC와 번들이
	// 빠진 빌드에서만 쓰는 대체다(윈도우 10/11에 기본으로 들어 있다).
	// 글꼴 모음에서는 화면용 UI 서체를 고른다. Yu Gothic Medium(0번)은 가나가
	// 전각 폭이라 글자 사이가 벌어져 보이고, Yu Gothic UI(1번)는 가나 폭이 좁다.
	// YaHei와 JhengHei의 UI 서체는 줄 높이가 게임 글자에 맞다.
	static const FCandidate Candidates[] = {
		{TEXT("ja"), TEXT("NotoSansJP-Regular.otf"), {TEXT("YuGothM.ttc"), TEXT("meiryo.ttc"), TEXT("msgothic.ttc")}, {1, 2, 2}},
		{TEXT("zh-Hans"), TEXT("NotoSansSC-Regular.otf"), {TEXT("msyh.ttc"), TEXT("simsun.ttc"), nullptr}, {1, 0, 0}},
		{TEXT("zh-Hant"), TEXT("NotoSansTC-Regular.otf"), {TEXT("msjh.ttc"), TEXT("mingliub.ttc"), nullptr}, {1, 0, 0}},
	};
	UFontFace* Result = nullptr;
	for (const FCandidate& Candidate : Candidates)
	{
		if (Culture != Candidate.Culture)
		{
			continue;
		}
		const FString FaceName = FString::Printf(TEXT("CjkFontFace_%s"), *Culture.Replace(TEXT("-"), TEXT("_")));
		Result = LoadBundledFontFace(Candidate.Bundled, *FaceName);
		if (!Result)
		{
			FString FontsDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
			FontsDirectory = FontsDirectory.IsEmpty()
				? TEXT("C:/Windows/Fonts")
				: FPaths::Combine(FontsDirectory, TEXT("Fonts"));
			for (int32 SystemIndex = 0; SystemIndex < UE_ARRAY_COUNT(Candidate.System); ++SystemIndex)
			{
				const TCHAR* SystemFile = Candidate.System[SystemIndex];
				TArray<uint8> FontBytes;
				const FString FontPath = SystemFile ? FPaths::Combine(FontsDirectory, SystemFile) : FString();
				if (FontPath.IsEmpty() || !FPaths::FileExists(FontPath)
					|| !FFileHelper::LoadFileToArray(FontBytes, *FontPath))
				{
					continue;
				}
				Result = NewObject<UFontFace>(this, *FaceName);
				Result->LoadingPolicy = EFontLoadingPolicy::Inline;
				Result->Hinting = EFontHinting::Default;
				Result->SourceFilename = FontPath;
				Result->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(FontBytes));
				CjkFontSubFaces.Add(Culture, Candidate.SystemFace[SystemIndex]);
				UE_LOG(LogIndieGame, Display, TEXT("CJK HUD font for %s loaded from the system: %s (face %d)"),
					*Culture, *FontPath, Candidate.SystemFace[SystemIndex]);
				break;
			}
		}
		break;
	}
	if (!Result && Culture != TEXT("ko") && Culture != TEXT("en"))
	{
		UE_LOG(LogIndieGame, Warning, TEXT("No CJK font found for %s; kanji and kana will not render."), *Culture);
	}
	CjkFontFaces.Add(Culture, Result);
	return Result;
}

void AIGHorrorHUD::HandleCultureChanged()
{
	// 역할별 글꼴과 한 번 계산해 둔 줄바꿈을 버리고 새 언어로 다시 만든다.
	InitializeKoreanFont();
	CurrentDialogueLines.Reset();
}

void AIGHorrorHUD::InitializeKoreanFont()
{
	const FString Culture = FInternationalization::Get().GetCurrentCulture()->GetName();
	FString CjkCulture;
	if (Culture.StartsWith(TEXT("ja")))
	{
		CjkCulture = TEXT("ja");
	}
	else if (Culture.StartsWith(TEXT("zh")))
	{
		CjkCulture = Culture.Contains(TEXT("Hant")) || Culture.Contains(TEXT("TW")) || Culture.Contains(TEXT("HK"))
			? TEXT("zh-Hant")
			: TEXT("zh-Hans");
	}
	ActiveCjkFontFace = CjkCulture.IsEmpty() ? nullptr : LoadCjkFontFace(CjkCulture);
	ActiveCjkSubFaceIndex = CjkFontSubFaces.FindRef(CjkCulture);
	if (KoreanBodyFontFace && KoreanEmphasisFontFace && KoreanDisplayFontFace)
	{
		// 두 번째부터는 얼굴은 그대로 두고 역할별 글꼴만 새로 만든다.
		KoreanFontLarge = MakeRuntimeFont(KoreanDisplayFontFace.Get(), IGHorrorHUD::LargeFontSize, TEXT("KoreanFontLarge"));
		KoreanFrontendTitleFont = MakeRuntimeFont(KoreanDisplayFontFace.Get(), IGHorrorHUD::FrontendTitleFontSize, TEXT("KoreanFrontendTitleFont"));
		KoreanFontMedium = MakeRuntimeFont(KoreanEmphasisFontFace.Get(), IGHorrorHUD::MediumFontSize, TEXT("KoreanFontMedium"));
		KoreanFontSmall = MakeRuntimeFont(KoreanBodyFontFace.Get(), IGHorrorHUD::SmallFontSize, TEXT("KoreanFontSmall"));
		KoreanPhoneMetaFont = MakeRuntimeFont(KoreanBodyFontFace.Get(), IGHorrorHUD::PhoneMetaFontSize, TEXT("KoreanPhoneMetaFont"));
		return;
	}
	KoreanBodyFontFace = LoadBundledFontFace(
		TEXT("Pretendard-Regular.otf"),
		TEXT("KoreanBodyFontFace"));
	KoreanEmphasisFontFace = LoadBundledFontFace(
		TEXT("Pretendard-SemiBold.otf"),
		TEXT("KoreanEmphasisFontFace"));
	KoreanDisplayFontFace = LoadBundledFontFace(
		TEXT("GowunBatang-Bold.ttf"),
		TEXT("KoreanDisplayFontFace"));
	if (KoreanBodyFontFace && KoreanEmphasisFontFace && KoreanDisplayFontFace)
	{
		KoreanFontLarge = MakeRuntimeFont(
			KoreanDisplayFontFace.Get(),
			IGHorrorHUD::LargeFontSize,
			TEXT("KoreanFontLarge"));
		KoreanFrontendTitleFont = MakeRuntimeFont(
			KoreanDisplayFontFace.Get(),
			IGHorrorHUD::FrontendTitleFontSize,
			TEXT("KoreanFrontendTitleFont"));
		KoreanFontMedium = MakeRuntimeFont(
			KoreanEmphasisFontFace.Get(),
			IGHorrorHUD::MediumFontSize,
			TEXT("KoreanFontMedium"));
		KoreanFontSmall = MakeRuntimeFont(
			KoreanBodyFontFace.Get(),
			IGHorrorHUD::SmallFontSize,
			TEXT("KoreanFontSmall"));
		KoreanPhoneMetaFont = MakeRuntimeFont(
			KoreanBodyFontFace.Get(),
			IGHorrorHUD::PhoneMetaFontSize,
			TEXT("KoreanPhoneMetaFont"));
		UE_LOG(LogIndieGame, Display, TEXT("Bundled Korean HUD type system loaded."));
		return;
	}

	// Development fallback only. A packaged build stages the bundled faces via
	// RuntimeDependencies, while this path keeps the HUD usable in a partial
	// source checkout and reports the missing asset in the log.
	FString FontsDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
	FontsDirectory = FontsDirectory.IsEmpty()
		? TEXT("C:/Windows/Fonts")
		: FPaths::Combine(FontsDirectory, TEXT("Fonts"));

	const TCHAR* CandidateFonts[] = {
		TEXT("malgun.ttf"),      // Malgun Gothic: ships with every Windows 10/11
		TEXT("malgunbd.ttf"),
		TEXT("NanumGothic.ttf"),
		TEXT("gulim.ttc"),
		TEXT("batang.ttc"),
	};

	for (const TCHAR* FontFileName : CandidateFonts)
	{
		const FString FontPath = FPaths::Combine(FontsDirectory, FontFileName);
		TArray<uint8> FontBytes;
		if (!FPaths::FileExists(FontPath) || !FFileHelper::LoadFileToArray(FontBytes, *FontPath))
		{
			continue;
		}

		UFontFace* FontFace = NewObject<UFontFace>(
			this, MakeUniqueObjectName(this, UFontFace::StaticClass(), TEXT("KoreanFontFace")));
		FontFace->LoadingPolicy = EFontLoadingPolicy::Inline;
		FontFace->Hinting = EFontHinting::Default;
		FontFace->SourceFilename = FontPath;
		FontFace->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(FontBytes));

		KoreanFontLarge = MakeRuntimeFont(
			FontFace, IGHorrorHUD::LargeFontSize, TEXT("KoreanFontLarge"));
		KoreanFrontendTitleFont = MakeRuntimeFont(
			FontFace,
			IGHorrorHUD::FrontendTitleFontSize,
			TEXT("KoreanFrontendTitleFont"));
		KoreanFontMedium = MakeRuntimeFont(
			FontFace, IGHorrorHUD::MediumFontSize, TEXT("KoreanFontMedium"));
		KoreanFontSmall = MakeRuntimeFont(
			FontFace, IGHorrorHUD::SmallFontSize, TEXT("KoreanFontSmall"));
		KoreanPhoneMetaFont = MakeRuntimeFont(
			FontFace,
			IGHorrorHUD::PhoneMetaFontSize,
			TEXT("KoreanPhoneMetaFont"));

		UE_LOG(LogIndieGame, Display, TEXT("HUD Korean font loaded: %s"), *FontPath);
		return;
	}

	UE_LOG(
		LogIndieGame,
		Warning,
		TEXT("No Korean-capable system font found; HUD falls back to ASCII strings."));
}

UFont* AIGHorrorHUD::GetFontForRole(const EIGHudTextRole TextRole) const
{
	if (SupportsKorean())
	{
		switch (TextRole)
		{
		case EIGHudTextRole::Objective:
			return KoreanFontLarge.Get();
		case EIGHudTextRole::Prompt:
			return KoreanFontMedium.Get();
		case EIGHudTextRole::Thought:
		case EIGHudTextRole::Dialogue:
		case EIGHudTextRole::Speaker:
		case EIGHudTextRole::Hint:
		default:
			return KoreanFontSmall.Get();
		}
	}

	if (!GEngine)
	{
		return nullptr;
	}
	return TextRole == EIGHudTextRole::Hint
		|| TextRole == EIGHudTextRole::Speaker
		? GEngine->GetSmallFont()
		: GEngine->GetMediumFont();
}

void AIGHorrorHUD::PushThought(
	const UObject* WorldContext,
	const FText& Thought,
	const float DurationSeconds)
{
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (AIGHorrorHUD* HorrorHUD = PlayerController
		? Cast<AIGHorrorHUD>(PlayerController->GetHUD())
		: nullptr)
	{
		HorrorHUD->ShowDialogue(
			FText::GetEmpty(),
			Thought,
			EIGDialogueChannel::InnerVoice,
			DurationSeconds,
			EIGDialoguePriority::Story);
	}
}

void AIGHorrorHUD::ShowThought(const FText& Thought, const float DurationSeconds)
{
	ShowDialogue(
		FText::GetEmpty(),
		Thought,
		EIGDialogueChannel::InnerVoice,
		DurationSeconds,
		EIGDialoguePriority::Story);
}

void AIGHorrorHUD::PushDialogue(
	const UObject* WorldContext,
	const FText& Speaker,
	const FText& Line,
	const EIGDialogueChannel Channel,
	const float MinimumDurationSeconds,
	const EIGDialoguePriority Priority)
{
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(
			WorldContext,
			EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const APlayerController* PlayerController =
		World ? World->GetFirstPlayerController() : nullptr;
	if (AIGHorrorHUD* HorrorHUD = PlayerController
		? Cast<AIGHorrorHUD>(PlayerController->GetHUD())
		: nullptr)
	{
		HorrorHUD->ShowDialogue(
			Speaker,
			Line,
			Channel,
			MinimumDurationSeconds,
			Priority);
	}
}

void AIGHorrorHUD::ShowDialogue(
	const FText& Speaker,
	const FText& Line,
	const EIGDialogueChannel Channel,
	const float MinimumDurationSeconds,
	const EIGDialoguePriority Priority)
{
	const UWorld* World = GetWorld();
	if (!World || Line.IsEmpty())
	{
		return;
	}

	if (Channel == EIGDialogueChannel::VoiceSubtitle)
	{
		const UGameInstance* GameInstance = World->GetGameInstance();
		const UIGAccessibilitySubsystem* Accessibility = GameInstance
			? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
			: nullptr;
		if (!Accessibility || !Accessibility->AreSubtitlesEnabled())
		{
			return;
		}
	}

	FIGDialogueMessage Message;
	Message.Speaker = Speaker;
	Message.Line = Line;
	Message.Channel = Channel;
	Message.Priority = Priority;
	Message.MinimumDurationSeconds = FMath::Max(0.0f, MinimumDurationSeconds);
	const double CurrentTime = World->GetTimeSeconds();
	Message.QueuedAt = CurrentTime;
	EnqueueDialogue(MoveTemp(Message), CurrentTime);
}

float AIGHorrorHUD::GetCaptionDurationScale() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	return Accessibility ? Accessibility->GetCaptionDurationScale() : 1.0f;
}

float AIGHorrorHUD::EstimateDialogueSeconds(
	const FText& Line,
	const float MinimumDurationSeconds)
{
	int32 VisibleGlyphs = 0;
	for (const TCHAR Character : Line.ToString())
	{
		if (!FChar::IsWhitespace(Character))
		{
			++VisibleGlyphs;
		}
	}
	return FMath::Max(
		FMath::Clamp(
			1.15f + VisibleGlyphs
				/ (IGHorrorHUD::DialogueGlyphsPerSecond * IGHorrorHUD::GetReadingRateScale()),
			IGHorrorHUD::DialogueMinimumSeconds,
			IGHorrorHUD::DialogueMaximumSeconds),
		MinimumDurationSeconds);
}

float AIGHorrorHUD::CalculateDialogueDuration(
	const FString& Line,
	const float MinimumDurationSeconds) const
{
	int32 VisibleGlyphs = 0;
	for (const TCHAR Character : Line)
	{
		if (!FChar::IsWhitespace(Character))
		{
			++VisibleGlyphs;
		}
	}
	const float ReadingDuration = FMath::Clamp(
		1.15f + VisibleGlyphs
			/ (IGHorrorHUD::DialogueGlyphsPerSecond * IGHorrorHUD::GetReadingRateScale()),
		IGHorrorHUD::DialogueMinimumSeconds,
		IGHorrorHUD::DialogueMaximumSeconds);
	// 배율은 상한 뒤에 곱한다. 안에서 곱하면 길게 잡아 봐야 상한에서
	// 잘려 설정이 아무 일도 안 하는 것처럼 보인다.
	return FMath::Max(ReadingDuration, MinimumDurationSeconds)
		* GetCaptionDurationScale();
}

void AIGHorrorHUD::ActivateDialogue(
	FIGDialogueMessage&& Message,
	const double CurrentTime)
{
	CurrentDialogue = MoveTemp(Message);
	bHasCurrentDialogue = true;
	bCurrentDialogueHasContinuation = false;
	CurrentDialogueLines.Reset();
	DialogueLayoutScale = -1.0f;
	DialogueLayoutWidth = -1.0f;
	DialogueLayoutMaximumLines = 0;
	DialogueStartTime = CurrentTime;
	DialogueEndTime = CurrentTime + CalculateDialogueDuration(
		CurrentDialogue.Line.ToString(),
		CurrentDialogue.MinimumDurationSeconds);
}

void AIGHorrorHUD::EnqueueDialogue(
	FIGDialogueMessage&& Message,
	const double CurrentTime)
{
	const FString IncomingLine = Message.Line.ToString();
	const FString IncomingSpeaker = Message.Speaker.ToString();
	const EIGDialogueChannel IncomingChannel = Message.Channel;
	const auto IsSameMessage = [
		&IncomingLine,
		&IncomingSpeaker,
		IncomingChannel](
		const FIGDialogueMessage& Candidate)
	{
		return Candidate.Channel == IncomingChannel
			&& Candidate.Line.ToString().Equals(IncomingLine)
			&& Candidate.Speaker.ToString().Equals(IncomingSpeaker);
	};

	if (bHasCurrentDialogue
		&& CurrentDialogue.Channel == Message.Channel
		&& IsSameMessage(CurrentDialogue))
	{
		DialogueEndTime = FMath::Max(
			DialogueEndTime,
			CurrentTime + CalculateDialogueDuration(
				IncomingLine,
				Message.MinimumDurationSeconds));
		return;
	}
	for (FIGDialogueMessage& Queued : DialogueQueue)
	{
		if (Queued.Channel == Message.Channel && IsSameMessage(Queued))
		{
			Queued.MinimumDurationSeconds = FMath::Max(
				Queued.MinimumDurationSeconds,
				Message.MinimumDurationSeconds);
			Queued.QueuedAt = CurrentTime;
			return;
		}
	}

	if (!bHasCurrentDialogue)
	{
		ActivateDialogue(MoveTemp(Message), CurrentTime);
		return;
	}

	if (static_cast<uint8>(Message.Priority)
		> static_cast<uint8>(CurrentDialogue.Priority))
	{
		CurrentDialogue.MinimumDurationSeconds = FMath::Max(
			0.8f,
			static_cast<float>(DialogueEndTime - CurrentTime));
		CurrentDialogue.QueuedAt = CurrentTime;
		DialogueQueue.Insert(MoveTemp(CurrentDialogue), 0);
		if (DialogueQueue.Num() > IGHorrorHUD::MaximumDialogueQueueDepth)
		{
			DialogueQueue.RemoveAt(DialogueQueue.Num() - 1);
		}
		ActivateDialogue(MoveTemp(Message), CurrentTime);
		return;
	}

	if (DialogueQueue.Num() >= IGHorrorHUD::MaximumDialogueQueueDepth)
	{
		int32 RemovalIndex = INDEX_NONE;
		for (int32 Index = DialogueQueue.Num() - 1; Index >= 0; --Index)
		{
			if (static_cast<uint8>(DialogueQueue[Index].Priority)
				<= static_cast<uint8>(Message.Priority))
			{
				RemovalIndex = Index;
				break;
			}
		}
		if (RemovalIndex == INDEX_NONE)
		{
			return;
		}
		DialogueQueue.RemoveAt(RemovalIndex);
	}
	DialogueQueue.Add(MoveTemp(Message));
}

void AIGHorrorHUD::AdvanceDialogueQueue(const double CurrentTime)
{
	if (bHasCurrentDialogue && CurrentTime < DialogueEndTime)
	{
		return;
	}
	bHasCurrentDialogue = false;
	CurrentDialogueLines.Reset();

	while (!DialogueQueue.IsEmpty())
	{
		FIGDialogueMessage Next = MoveTemp(DialogueQueue[0]);
		DialogueQueue.RemoveAt(0);
		// 자막을 오래 두는 설정이면 앞줄이 그만큼 길게 머문다. 기다릴 수 있는
		// 시간도 같이 늘려야 한꺼번에 쌓인 대화의 끝줄이 조용히 버려지지 않는다.
		const double MaximumAge = (Next.Priority == EIGDialoguePriority::Ambient
			? IGHorrorHUD::AmbientDialogueMaximumQueueAge
			: IGHorrorHUD::StoryDialogueMaximumQueueAge)
			* FMath::Max(1.0f, GetCaptionDurationScale());
		if (!Next.bContinuation && CurrentTime - Next.QueuedAt > MaximumAge)
		{
			continue;
		}
		ActivateDialogue(MoveTemp(Next), CurrentTime);
		return;
	}
}

void AIGHorrorHUD::SuspendDialoguePresentation(const double CurrentTime)
{
	if (bHasCurrentDialogue && DialogueOccludedAt < 0.0)
	{
		DialogueOccludedAt = CurrentTime;
	}
}

void AIGHorrorHUD::ResumeDialoguePresentation(const double CurrentTime)
{
	if (DialogueOccludedAt < 0.0)
	{
		return;
	}
	const double OccludedDuration = FMath::Max(0.0, CurrentTime - DialogueOccludedAt);
	DialogueStartTime += OccludedDuration;
	DialogueEndTime += OccludedDuration;
	for (FIGDialogueMessage& Queued : DialogueQueue)
	{
		Queued.QueuedAt += OccludedDuration;
	}
	DialogueOccludedAt = -1.0;
}

namespace
{
	// 다른 층에서 들리는 소리는 좌우보다 위아래를 먼저 알려 준다.
	constexpr float SoundBearingElevationDegrees = 25.0f;
	constexpr float SoundBearingBodyMargin = 40.0f;
	// 화면 안이라고 볼 각. 이 안쪽은 눈이 이미 알고 있으므로 적지 않는다.
	constexpr float SoundBearingOnScreenDegrees = 50.0f;
	constexpr float SoundBearingBehindDegrees = 130.0f;
	constexpr float SoundBearingMinimumDistance = 40.0f;
}

FText AIGHorrorHUD::MakeSoundBearingTag(
	const UObject* WorldContext,
	const FVector& SourceLocation)
{
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(
			WorldContext,
			EGetWorldErrorMode::ReturnNull)
		: nullptr;
	APlayerController* PlayerController =
		World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return FText::GetEmpty();
	}
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector ToSource = SourceLocation - ViewLocation;
	if (ToSource.SizeSquared()
		< SoundBearingMinimumDistance * SoundBearingMinimumDistance)
	{
		// 발밑에서 난 소리에 방위를 붙이면 고개만 돌려도 딱지가 뒤집힌다.
		return FText::GetEmpty();
	}

	const float Elevation = FMath::RadiansToDegrees(
		FMath::Asin(FMath::Clamp(
			ToSource.GetSafeNormal().Z, -1.0f, 1.0f)));
	const AIGPlayerCharacter* Listener = Cast<AIGPlayerCharacter>(PlayerController->GetPawn());
	const UCapsuleComponent* Capsule = Listener ? Listener->GetCapsuleComponent() : nullptr;
	const float BodyCenterZ = Listener ? Listener->GetActorLocation().Z : ViewLocation.Z - 64.0f;
	const float BodyHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 98.0f;
	// 가까운 문의 노크나 같은 층의 발소리는 눈보다 낮다. 각도만 보면
	// 전부 '아래'로 표시되므로 캐릭터의 몸 높이를 벗어났는지도 확인한다.
	const bool bOutsideBodyHeight = FMath::Abs(SourceLocation.Z - BodyCenterZ)
		> BodyHalfHeight + SoundBearingBodyMargin;
	if (bOutsideBodyHeight && FMath::Abs(Elevation) >= SoundBearingElevationDegrees)
	{
		return Elevation > 0.0f
			? NSLOCTEXT("IGHUD", "SoundBearingAbove", "위")
			: NSLOCTEXT("IGHUD", "SoundBearingBelow", "아래");
	}

	const float RelativeYaw = FMath::FindDeltaAngleDegrees(
		ViewRotation.Yaw,
		ToSource.Rotation().Yaw);
	const float AbsoluteYaw = FMath::Abs(RelativeYaw);
	if (AbsoluteYaw <= SoundBearingOnScreenDegrees)
	{
		return FText::GetEmpty();
	}
	if (AbsoluteYaw >= SoundBearingBehindDegrees)
	{
		return NSLOCTEXT("IGHUD", "SoundBearingBehind", "뒤");
	}
	return RelativeYaw > 0.0f
		? NSLOCTEXT("IGHUD", "SoundBearingRight", "오른쪽")
		: NSLOCTEXT("IGHUD", "SoundBearingLeft", "왼쪽");
}

void AIGHorrorHUD::PushAudioCaptionAt(
	const UObject* WorldContext,
	const FText& Caption,
	const float DurationSeconds,
	const FVector& SourceLocation)
{
	const FText Bearing = MakeSoundBearingTag(WorldContext, SourceLocation);
	if (Bearing.IsEmpty())
	{
		PushAudioCaption(WorldContext, Caption, DurationSeconds);
		return;
	}
	PushAudioCaption(
		WorldContext,
		FText::Format(
			NSLOCTEXT("IGHUD", "SoundCaptionWithBearing", "[{0}] {1}"),
			Bearing,
			Caption),
		DurationSeconds);
}

void AIGHorrorHUD::PushAudioCaption(
	const UObject* WorldContext,
	const FText& Caption,
	const float DurationSeconds)
{
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(
			WorldContext,
			EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	if (!Accessibility || !Accessibility->AreSoundCaptionsEnabled())
	{
		return;
	}

	const APlayerController* PlayerController =
		World ? World->GetFirstPlayerController() : nullptr;
	if (AIGHorrorHUD* HorrorHUD = PlayerController
		? Cast<AIGHorrorHUD>(PlayerController->GetHUD())
		: nullptr)
	{
		HorrorHUD->ShowAudioCaption(Caption, DurationSeconds);
	}
}

double AIGHorrorHUD::AdvanceAudioCaptionClock()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return AudioCaptionClockSeconds;
	}
	// 자막 시계. 게임 시간처럼 일시정지에 멈춰서, 메뉴를 닫으면 자막이 멈췄던
	// 자리에서 이어진다. 타이틀은 월드를 멈춘 채로 노크와 대답, 밤 5를 들려주므로
	// 타이틀이 떠 있는 동안에는 흐른다. 지난 호출 뒤의 시간을 지금 상태로 세니
	// DrawHUD가 매 프레임 부른다.
	const double Now = World->GetUnpausedTimeSeconds();
	const bool bRunning = !World->IsPaused()
		|| (bSystemMenuVisible && bSystemMenuIsTitle);
	if (AudioCaptionClockSampledAt >= 0.0 && bRunning)
	{
		AudioCaptionClockSeconds += FMath::Max(0.0, Now - AudioCaptionClockSampledAt);
	}
	AudioCaptionClockSampledAt = Now;
	return AudioCaptionClockSeconds;
}

void AIGHorrorHUD::ShowAudioCaption(
	const FText& Caption,
	const float DurationSeconds)
{
	const UWorld* World = GetWorld();
	if (!World || Caption.IsEmpty())
	{
		return;
	}
	// 타이틀 배경의 소리와 이전 플레이 자막이 메뉴 위로 올라오지 않게 한다.
	// 다섯째 밤의 검정 화면은 실제 사건을 재생하므로 자막을 유지한다.
	if (bSystemMenuVisible && bSystemMenuIsTitle && !bSystemMenuNightFivePlaying)
	{
		return;
	}
	const double CurrentTime = AdvanceAudioCaptionClock();
	const float ClampedDuration =
		FMath::Max(0.8f, DurationSeconds) * GetCaptionDurationScale();
	if (!CurrentAudioCaption.IsEmpty()
		&& CurrentTime < AudioCaptionEndTime
		&& CurrentAudioCaption.ToString().Equals(Caption.ToString()))
	{
		AudioCaptionEndTime = FMath::Max(
			AudioCaptionEndTime,
			CurrentTime + ClampedDuration);
		return;
	}

	FIGAudioCaptionMessage Message;
	Message.Caption = Caption;
	Message.DurationSeconds = ClampedDuration;
	Message.QueuedAt = CurrentTime;
	if (CurrentAudioCaption.IsEmpty() || CurrentTime >= AudioCaptionEndTime)
	{
		ActivateAudioCaption(MoveTemp(Message), CurrentTime);
		return;
	}
	for (FIGAudioCaptionMessage& Queued : AudioCaptionQueue)
	{
		if (Queued.Caption.ToString().Equals(Caption.ToString()))
		{
			Queued.DurationSeconds = FMath::Max(
				Queued.DurationSeconds,
				ClampedDuration);
			Queued.QueuedAt = CurrentTime;
			return;
		}
	}
	if (AudioCaptionQueue.Num() >= IGHorrorHUD::MaximumAudioCaptionQueueDepth)
	{
		AudioCaptionQueue.RemoveAt(0);
	}
	AudioCaptionQueue.Add(MoveTemp(Message));
}

void AIGHorrorHUD::ActivateAudioCaption(
	FIGAudioCaptionMessage&& Message,
	const double CurrentTime)
{
	CurrentAudioCaption = MoveTemp(Message.Caption);
	AudioCaptionStartTime = CurrentTime;
	AudioCaptionEndTime = CurrentTime + FMath::Max(0.8f, Message.DurationSeconds);
}

void AIGHorrorHUD::AdvanceAudioCaptionQueue(const double CurrentTime)
{
	if (!CurrentAudioCaption.IsEmpty() && CurrentTime < AudioCaptionEndTime)
	{
		return;
	}
	CurrentAudioCaption = FText::GetEmpty();
	while (!AudioCaptionQueue.IsEmpty())
	{
		FIGAudioCaptionMessage Next = MoveTemp(AudioCaptionQueue[0]);
		AudioCaptionQueue.RemoveAt(0);
		// A caption that would be badly detached from its sound is safer to drop
		// than to present as a false current event.
		if (CurrentTime - Next.QueuedAt > 3.0)
		{
			continue;
		}
		ActivateAudioCaption(MoveTemp(Next), CurrentTime);
		return;
	}
}

void AIGHorrorHUD::PushFearDirection(
	const UObject* WorldContext,
	const FVector& WorldLocation,
	const float DurationSeconds)
{
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(
			WorldContext,
			EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	if (!Accessibility || !Accessibility->UsesDirectionalFearCues())
	{
		return;
	}

	const APlayerController* PlayerController =
		World ? World->GetFirstPlayerController() : nullptr;
	if (AIGHorrorHUD* HorrorHUD = PlayerController
		? Cast<AIGHorrorHUD>(PlayerController->GetHUD())
		: nullptr)
	{
		HorrorHUD->ShowFearDirection(WorldLocation, DurationSeconds);
	}
}

void AIGHorrorHUD::ShowSaveIndicator()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	SaveIndicatorEndTime =
		World->GetTimeSeconds() + IGHorrorHUD::SaveIndicatorSeconds;
}

void AIGHorrorHUD::DrawNightClock()
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UIGMissingFloorNarrativeSubsystem* Narrative = GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	if (!Canvas || !Narrative)
	{
		return;
	}
	// 04:30에서 시작해 05:30에 문이 열린다. 한 시간의 실제 길이는 밤마다 다르다.
	const int32 TotalMinutes = AIGNightPhaseDirector::GetStoryMinuteAt(
		Narrative->GetNightIndex(), Narrative->GetNightElapsedSeconds());
	const FText ClockText = FText::FromString(FString::Printf(
		TEXT("%02d:%02d"), TotalMinutes / 60, TotalMinutes % 60));
	const float Scale = FMath::Clamp(
		FMath::Min(Canvas->ClipX / 1920.0f, Canvas->ClipY / 1080.0f),
		0.67f,
		2.0f);
	FLinearColor Colour = IGHorrorHUD::MutedGray;
	Colour.A *= 0.7f * Guidance.ObjectiveAlpha();
	DrawLeftAlignedText(
		ClockText,
		FVector2D(42.0f * Scale, Canvas->ClipY - 60.0f * Scale),
		Colour,
		EIGHudTextRole::Objective,
		GetResolutionTextScale(0.85f),
		false);
}

void AIGHorrorHUD::DrawSaveIndicator(const double CurrentTime)
{
	if (!Canvas || CurrentTime >= SaveIndicatorEndTime)
	{
		return;
	}
	// 마지막 4분의 1만 흐려진다. 깜빡이면 그건 알림이고, 이건 알림이
	// 아니라 흔적이다.
	const double Remaining = SaveIndicatorEndTime - CurrentTime;
	const float Alpha = static_cast<float>(FMath::Clamp(
		Remaining / (IGHorrorHUD::SaveIndicatorSeconds * 0.25), 0.0, 1.0));
	const float Scale = FMath::Clamp(
		FMath::Min(Canvas->ClipX / 1920.0f, Canvas->ClipY / 1080.0f),
		0.67f,
		2.0f);
	const float Radius = IGHorrorHUD::SaveIndicatorRadius * Scale;
	// 오른쪽 아래 구석. 브래킷도 자막도 쓰지 않는 자리다.
	const FVector2D Centre(
		Canvas->ClipX - 42.0f * Scale,
		Canvas->ClipY - 42.0f * Scale);
	FLinearColor DotColour = IGHorrorHUD::MutedGray;
	DotColour.A *= Alpha;
	FCanvasTileItem Dot(
		Centre - FVector2D(Radius, Radius),
		FVector2D(Radius * 2.0f, Radius * 2.0f),
		DotColour);
	Dot.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Dot);
}

void AIGHorrorHUD::ShowFearDirection(
	const FVector& WorldLocation,
	const float DurationSeconds)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FearCueWorldLocation = WorldLocation;
	FearCueStartTime = World->GetTimeSeconds();
	FearCueEndTime = FearCueStartTime + FMath::Max(0.35f, DurationSeconds);
}

void AIGHorrorHUD::SetAccessibilityMenuState(
	const bool bVisible,
	const int32 SelectedRow,
	const double ResetArmedUntil)
{
	bAccessibilityMenuVisible = bVisible;
	AccessibilityResetArmedUntil = ResetArmedUntil;
	// 행 수를 숫자로 박아 두면 행이 늘 때마다 뒤쪽 행이 선택되지 않는다. 8월 11일에
	// 17행일 때 박은 16이 23행이 된 뒤까지 남아, 「기타」의 행을 누르면 강조는 안 되고
	// 마우스 판정만 맞아서 「기본값으로 초기화」가 확인 없이 실행됐다.
	AccessibilitySelectedRow = FMath::Clamp(
		SelectedRow, 0, IGSettingsMenuLayout::AccessibilityRowCount - 1);
}

void AIGHorrorHUD::SetSystemMenuState(
	const FIGSystemMenuPresentation& Presentation)
{
	if (Presentation.bVisible && Presentation.bTitle && !Presentation.bNightFivePlaying
		&& (!bSystemMenuVisible || !bSystemMenuIsTitle || bSystemMenuNightFivePlaying))
	{
		// 일시정지 복귀에서는 보존하고, 타이틀로 돌아올 때만 지난 사건을 비운다.
		CurrentAudioCaption = FText::GetEmpty();
		AudioCaptionQueue.Reset();
		AudioCaptionEndTime = -1.0;
	}
	const bool bPresentationChanged = Presentation.bVisible
		&& (!bSystemMenuVisible
			|| bSystemMenuIsTitle != Presentation.bTitle
			|| bSystemMenuIsCredits != Presentation.bCredits
			|| bSystemMenuIsContentNotice != Presentation.bContentNotice
			|| bSystemMenuIsKeyBindings != Presentation.bKeyBindings
			|| SystemMenuKeyBindingSelection != Presentation.KeyBindingSelection
			|| bSystemMenuKeyBindingCapturing != Presentation.bKeyBindingCapturing
			|| bSystemMenuKeyBindingColumnGamepad
				!= Presentation.bKeyBindingColumnGamepad
			|| bSystemMenuIsAudioCalibration != Presentation.bAudioCalibration
			|| bSystemMenuIsDisplaySettings != Presentation.bDisplaySettings
			|| bSystemMenuUseTitleBackdrop != Presentation.bUseTitleBackdrop);
	if (bPresentationChanged)
	{
		SystemMenuOpenedAt = FPlatformTime::Seconds();
	}
	else if (!Presentation.bVisible)
	{
		SystemMenuOpenedAt = -1.0;
	}
	bSystemMenuVisible = Presentation.bVisible;
	bSystemMenuIsTitle = Presentation.bTitle;
	bSystemMenuUseTitleBackdrop = Presentation.bUseTitleBackdrop;
	bSystemMenuIsCredits = Presentation.bCredits;
	bSystemMenuIsContentNotice = Presentation.bContentNotice;
	bSystemMenuIsKeyBindings = Presentation.bKeyBindings;
	SystemMenuKeyBindingSelection = Presentation.KeyBindingSelection;
	bSystemMenuKeyBindingCapturing = Presentation.bKeyBindingCapturing;
	bSystemMenuKeyBindingColumnGamepad = Presentation.bKeyBindingColumnGamepad;
	SystemMenuKeyBindingStatus = Presentation.KeyBindingStatus;
	bSystemMenuKeyBindingStatusIsError = Presentation.bKeyBindingStatusIsError;
	bSystemMenuIsAudioCalibration = Presentation.bAudioCalibration;
	bSystemMenuIsDisplaySettings = Presentation.bDisplaySettings;
	SystemMenuSelectedRow = FMath::Clamp(
		Presentation.SelectedRow,
		0,
		IGFrontendMenuLayout::ActionCount - 1);
	bSystemMenuCanContinue = Presentation.bCanContinue;
	bSystemMenuNightFiveAvailable = Presentation.bNightFiveAvailable;
	bSystemMenuNightFiveSpent = Presentation.bNightFiveSpent;
	bSystemMenuNightFivePlaying = Presentation.bNightFivePlaying;
	bSystemMenuConfirmNewGame = Presentation.bConfirmNewGame;
	bSystemMenuHeadphoneRecommendation =
		Presentation.bHeadphoneRecommendation;
	// 접근성 쪽과 같은 이유로 숫자를 박지 않는다. 8로 박혀 있어서 10행이 된 뒤로는
	// 「돌아가기」에 강조가 가지 않았다.
	DisplaySettingsSelectedRow = FMath::Clamp(
		Presentation.DisplaySelectedRow,
		0,
		IGSettingsMenuLayout::DisplayRowCount - 1);
	// 소리와 밝기는 일곱 줄이다. 3으로 잘려서 「화면 밝기」부터 아래 줄을 고르면
	// 강조가 「출력 장치」에 남아 있었다.
	AudioCalibrationSelectedRow = FMath::Clamp(
		Presentation.AudioCalibrationSelectedRow,
		0,
		IGSettingsMenuLayout::AudioCalibrationRowCount - 1);
	AudioCalibrationVolumeStep = FMath::Clamp(
		Presentation.AudioCalibrationVolumeStep,
		0,
		6);
	AudioCalibrationBrightnessStep = FMath::Clamp(
		Presentation.AudioCalibrationBrightnessStep,
		0,
		4);
	bSystemMenuAudioCalibrationFirstRun =
		Presentation.bAudioCalibrationFirstRun;
	bSystemMenuHeadphoneOutput = Presentation.bHeadphoneOutput;
	SystemMenuAudioCalibrationMusicStep = FMath::Clamp(
		Presentation.AudioCalibrationMusicStep, 0, 4);
	SystemMenuAudioCalibrationAmbienceStep = FMath::Clamp(
		Presentation.AudioCalibrationAmbienceStep, 0, 4);
	DisplayWindowModeIndex = FMath::Clamp(Presentation.WindowModeIndex, 0, 2);
	DisplayResolutionIndex = FMath::Clamp(Presentation.ResolutionIndex, 0, 2);
	DisplayQualityIndex = FMath::Clamp(Presentation.QualityIndex, 0, 1);
	DisplayFrameLimitIndex = FMath::Clamp(Presentation.FrameLimitIndex, 0, 2);
	bSystemMenuVSync = Presentation.bVSync;
	bDisplaySettingsApplied = Presentation.bDisplaySettingsApplied;
	bDisplaySettingsAwaitingConfirmation =
		Presentation.bDisplaySettingsAwaitingConfirmation;
	DisplayConfirmationSecondsRemaining =
		FMath::Max(0, Presentation.ConfirmationSecondsRemaining);
	SystemMenuStatusText = Presentation.StatusText;
	bSystemMenuStatusIsError = Presentation.bStatusIsError;
}

void AIGHorrorHUD::SetMissingFloorJournalState(
	const bool bVisible,
	const int32 PageIndex)
{
	bMissingFloorJournalVisible = bVisible;
	MissingFloorJournalPageIndex = FMath::Clamp(
		PageIndex,
		0,
		FMath::Max(0, GetMissingFloorJournalPageCount() - 1));
}

int32 AIGHorrorHUD::GetMissingFloorJournalPageCount() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGMissingFloorNarrativeSubsystem* Narrative = GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	if (!Narrative)
	{
		return 1;
	}

	TSet<FName> ObservedSources;
	for (const FIGMissingFloorTruthRecord& Record :
		Narrative->GetSnapshot().Truths)
	{
		ObservedSources.Append(Record.SourceIds);
	}

	int32 Counts[3] = {0, 0, 0};
	for (const IGHorrorHUD::FJournalEntryDefinition& Entry :
		IGHorrorHUD::JournalEntries())
	{
		if (ObservedSources.Contains(Entry.SourceId))
		{
			++Counts[static_cast<int32>(Entry.Lane)];
		}
	}
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	const float UserTextScale = Accessibility
		? Accessibility->GetCaptionSizeScale()
		: 1.0f;
	const int32 CardsPerLanePerPage = UserTextScale > 1.50f
		? 1
		: UserTextScale > 1.15f ? 2 : 3;
	return FMath::Max(
		1,
		FMath::Max3(
			FMath::DivideAndRoundUp(Counts[0], CardsPerLanePerPage),
			FMath::DivideAndRoundUp(Counts[1], CardsPerLanePerPage),
			FMath::DivideAndRoundUp(Counts[2], CardsPerLanePerPage)));
}

void AIGHorrorHUD::ShowChapterCard(
	const UObject* WorldContext,
	const FText& Eyebrow,
	const FText& Title,
	const FText& Subtitle,
	const float DurationSeconds)
{
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (AIGHorrorHUD* HorrorHUD = PlayerController
		? Cast<AIGHorrorHUD>(PlayerController->GetHUD())
		: nullptr)
	{
		HorrorHUD->PresentChapterCard(Eyebrow, Title, Subtitle, DurationSeconds);
	}
}

void AIGHorrorHUD::PresentChapterCard(
	const FText& Eyebrow,
	const FText& Title,
	const FText& Subtitle,
	const float DurationSeconds)
{
	const UWorld* World = GetWorld();
	if (!World || Title.IsEmpty())
	{
		return;
	}

	ChapterCardEyebrow = Eyebrow;
	ChapterCardTitle = Title;
	ChapterCardSubtitle = Subtitle;
	ChapterCardStartTime = World->GetTimeSeconds();
	ChapterCardEndTime = ChapterCardStartTime + FMath::Max(1.8f, DurationSeconds);
}

void AIGHorrorHUD::BeginMissingFloorFailureEnding(
	const float InitialElapsedSeconds)
{
	bMissingFloorFailureEndingVisible = true;
	bMissingFloorFailureRetryEnabled = false;
	MissingFloorFailureEndingStartedAt = GetWorld()
		? GetWorld()->GetTimeSeconds()
			- FMath::Max(InitialElapsedSeconds, 0.0f)
		: 0.0;
}

void AIGHorrorHUD::EndMissingFloorFailureEnding()
{
	bMissingFloorFailureEndingVisible = false;
	bMissingFloorFailureRetryEnabled = false;
	MissingFloorFailureEndingStartedAt = 0.0;
}

void AIGHorrorHUD::SetObjectiveProvider(UObject* InObjectiveProvider)
{
	ObjectiveProvider = IsValid(InObjectiveProvider)
		&& InObjectiveProvider->GetClass()->ImplementsInterface(
			UIGObjectiveProvider::StaticClass())
		? InObjectiveProvider
		: nullptr;
}

FText AIGHorrorHUD::GetBoundKeyLabel(
	const EIGBindableAction Action,
	const bool bGamepad) const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGInputBindingSubsystem* Bindings = GameInstance
		? GameInstance->GetSubsystem<UIGInputBindingSubsystem>()
		: nullptr;
	const FKey Key = Bindings
		? Bindings->GetBoundKey(static_cast<int32>(Action), bGamepad)
		: FKey();
	if (!Key.IsValid())
	{
		return NSLOCTEXT("IGHUD", "KeyLabelNone", "없음");
	}
	return GetShortKeyLabel(Key);
}

FText AIGHorrorHUD::GetShortKeyLabel(const FKey& Key)
{
	// 패드는 Xbox 배열의 짧은 이름으로. 「Gamepad Face Button Bottom」은 힌트 줄에
	// 못 들어간다. 키보드는 키캡에 적힌 이름으로 둔다. 엔진 이름은 언어마다
	// 번역되어 간체에서만 「Tab键」처럼 모양이 달라진다.
	struct FShortLabel
	{
		FKey Key;
		const TCHAR* Label;
	};
	static const FShortLabel ShortLabels[] = {
		{EKeys::Gamepad_FaceButton_Bottom, TEXT("A")},
		{EKeys::Gamepad_FaceButton_Right, TEXT("B")},
		{EKeys::Gamepad_FaceButton_Left, TEXT("X")},
		{EKeys::Gamepad_FaceButton_Top, TEXT("Y")},
		{EKeys::Gamepad_LeftShoulder, TEXT("LB")},
		{EKeys::Gamepad_RightShoulder, TEXT("RB")},
		{EKeys::Gamepad_LeftTrigger, TEXT("LT")},
		{EKeys::Gamepad_RightTrigger, TEXT("RT")},
		{EKeys::Gamepad_LeftThumbstick, TEXT("L3")},
		{EKeys::Gamepad_RightThumbstick, TEXT("R3")},
		{EKeys::Gamepad_DPad_Up, TEXT("D↑")},
		{EKeys::Gamepad_DPad_Down, TEXT("D↓")},
		{EKeys::Gamepad_DPad_Left, TEXT("D←")},
		{EKeys::Gamepad_DPad_Right, TEXT("D→")},
		{EKeys::Gamepad_Special_Left, TEXT("View")},
		{EKeys::Gamepad_Special_Right, TEXT("Menu")},
		{EKeys::LeftShift, TEXT("Shift")},
		{EKeys::RightShift, TEXT("R-Shift")},
		{EKeys::LeftControl, TEXT("Ctrl")},
		{EKeys::RightControl, TEXT("R-Ctrl")},
		{EKeys::LeftAlt, TEXT("Alt")},
		{EKeys::SpaceBar, TEXT("Space")},
		{EKeys::LeftMouseButton, TEXT("LMB")},
		{EKeys::RightMouseButton, TEXT("RMB")},
		{EKeys::MiddleMouseButton, TEXT("MMB")},
		{EKeys::ThumbMouseButton, TEXT("M4")},
		{EKeys::ThumbMouseButton2, TEXT("M5")},
		{EKeys::CapsLock, TEXT("Caps")},
		{EKeys::Tab, TEXT("Tab")},
		{EKeys::Enter, TEXT("Enter")},
		{EKeys::Escape, TEXT("Esc")},
		{EKeys::BackSpace, TEXT("Backspace")},
		{EKeys::Delete, TEXT("Del")},
		{EKeys::Insert, TEXT("Ins")},
		{EKeys::Home, TEXT("Home")},
		{EKeys::End, TEXT("End")},
		{EKeys::PageUp, TEXT("PgUp")},
		{EKeys::PageDown, TEXT("PgDn")},
		{EKeys::Up, TEXT("↑")},
		{EKeys::Down, TEXT("↓")},
		{EKeys::Left, TEXT("←")},
		{EKeys::Right, TEXT("→")},
		{EKeys::RightAlt, TEXT("R-Alt")},
	};
	for (const FShortLabel& Short : ShortLabels)
	{
		if (Short.Key == Key)
		{
			return FText::FromString(Short.Label);
		}
	}
	return Key.GetDisplayName();
}

bool AIGHorrorHUD::CanShowGameplayGuide() const
{
	const APawn* Pawn = GetOwningPawn();
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	return Pawn && Pawn->InputEnabled() && !bSystemMenuVisible && !bAccessibilityMenuVisible
		&& !bMissingFloorJournalVisible && !bSensoryInterludePresentation && !AIGReadableNote::GetOpenNote()
		&& Now >= CaptureEmbraceEndTime && Now >= CaptureWakeEchoEndTime && !IsCameraFadedOut();
}

bool AIGHorrorHUD::IsCameraFadedOut() const
{
	const APlayerController* Controller = GetOwningPlayerController();
	const APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
	// 흰 페이드는 없다. 이 게임의 페이드는 전부 잠, 층 이동, 포획의 검정이다.
	return Camera && Camera->FadeAmount >= 0.55f;
}

void AIGHorrorHUD::NoteCompletedInteractions(const UIGInteractionComponent* Interaction)
{
	if (!Interaction)
	{
		return;
	}
	const int32 Completed = Interaction->GetCompletedInteractionCount();
	if (Completed <= SeenCompletedInteractions)
	{
		// 폰이 새로 붙으면 수를 0부터 다시 센다.
		SeenCompletedInteractions = Completed;
		return;
	}
	SeenCompletedInteractions = Completed;
	// 첫 물건을 조사했다고 조작표를 바로 내리지 않는다. 점프·숙이기·손전등 안내를
	// 읽을 시간이 남아야 한다. 조작표는 실제로 떠 있던 시간이 차면 내려가고 F1로 다시 본다.
	const AActor* Target = Interaction->GetLastCompletedTarget();
	const bool bListen = Target && Target->ActorHasTag(FName(TEXT("MissingFloor.Verb.Listen")));
	IGOnboardingMemory::AddPromptUse(bListen ? TEXT("Listen") : TEXT("Interact"));
}

void AIGHorrorHUD::UpdateContextTips(const float DeltaSeconds, const bool bLaneFree)
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	const AIGPlayerCharacter* Player = Cast<AIGPlayerCharacter>(GetOwningPawn());
	if (Narrative && Player)
	{
		// 안내하기 전에 스스로 해 봤다면 그 안내는 필요 없다.
		if (Player->bIsCrouched) ContextTips.Acknowledge(EIGContextTip::Crouch);
		if (Player->IsHoldingBreath()) ContextTips.Acknowledge(EIGContextTip::HoldBreath);
		if (Player->IsSprinting()) ContextTips.Acknowledge(EIGContextTip::Sprint);

		const bool bNight = bNightPresentation && Narrative->GetNightIndex() >= 1;
		NightTipClock = bNight ? NightTipClock + FMath::Max(0.0f, DeltaSeconds) : 0.0f;
		if (bNight)
		{
			// 첫 밤을 여는 노크와 끌리는 소리가 지나간 뒤에 한 번.
			if (NightTipClock > 25.0f)
			{
				ContextTips.Offer(EIGContextTip::Crouch);
			}
			if (!ListenerForTips.IsValid())
			{
				for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
				{
					ListenerForTips = *It;
					break;
				}
			}
			if (const AIGListenerEntity* Listener = ListenerForTips.Get())
			{
				const EIGListenerState State = Listener->GetListenerState();
				const float DistanceSquared = FVector::DistSquared(
					Listener->GetActorLocation(), Player->GetActorLocation());
				if (State == EIGListenerState::Chasing)
				{
					ContextTips.Offer(EIGContextTip::Sprint);
				}
				else if (DistanceSquared < FMath::Square(700.0f)
					&& (State == EIGListenerState::Listening
						|| State == EIGListenerState::Holding
						|| State == EIGListenerState::Banging))
				{
					ContextTips.Offer(EIGContextTip::HoldBreath);
				}
			}
		}
		// 새 단서 없이 90초가 지나 세계가 먼저 움직였다. 막혔다는 뜻이니
		// 힌트 키가 있다는 것을 한 번 알린다.
		if (!MercyForTips.IsValid())
		{
			for (TActorIterator<AIGMissingFloorMercyDirector> It(GetWorld()); It; ++It)
			{
				MercyForTips = *It;
				break;
			}
		}
		if (const AIGMissingFloorMercyDirector* Mercy = MercyForTips.Get())
		{
			const int32 Responses = Mercy->GetResponseCount();
			if (SeenMercyResponses >= 0 && Responses > SeenMercyResponses)
			{
				OfferHintTip();
			}
			SeenMercyResponses = Responses;
		}
		// 낮에 처음 단서가 적히면 기록을 펼쳐 볼 수 있다는 것만 알린다.
		const int32 Sources = Narrative->GetTotalSourceCount();
		if (SeenSourceCount >= 0 && Sources > SeenSourceCount && !bNightPresentation
			&& !Narrative->IsHourSealed())
		{
			ContextTips.Offer(EIGContextTip::Journal);
		}
		SeenSourceCount = Sources;
	}
	ContextTips.Update(DeltaSeconds, bLaneFree);
}

void AIGHorrorHUD::StartEndCredits()
{
	if (bEndCreditsActive)
	{
		return;
	}
	bEndCreditsActive = true;
	EndCreditsStartTime = FPlatformTime::Seconds();
	EndCreditsFinishTime = -1.0;
	Guidance.Interrupt();
	ContextTips.Interrupt();
	// 밤마다 지워졌던 바깥 소리가 크레딧에서 돌아온다. 아주 낮게.
	if (USoundBase* Bed = IGAudio::Sample(TEXT("Bed_City_Night")))
	{
		EndCreditsBed = UGameplayStatics::SpawnSound2D(this, Bed, 0.22f, 1.0f, 0.0f, nullptr, true, false);
		if (EndCreditsBed)
		{
			EndCreditsBed->FadeIn(4.0f, 0.22f);
		}
	}
}

void AIGHorrorHUD::FinishEndCredits()
{
	if (!bEndCreditsActive)
	{
		return;
	}
	bEndCreditsActive = false;
	if (EndCreditsBed)
	{
		EndCreditsBed->FadeOut(1.2f, 0.0f);
		EndCreditsBed = nullptr;
	}
	if (AIGPlayerController* Controller = Cast<AIGPlayerController>(GetOwningPlayerController()))
	{
		Controller->ShowTitleAfterEnding();
	}
}

bool AIGHorrorHUD::DrawEndCredits()
{
	if (!Canvas)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	const float Elapsed = static_cast<float>(Now - EndCreditsStartTime);
	const float Scale = FMath::Clamp(Canvas->ClipY / 1080.0f, 0.67f, 2.0f);

	// 확인 키로 건너뛴다. 끝났다는 걸 알린 게임에서 크레딧을 붙들어 두지 않는다.
	if (EndCreditsFinishTime < 0.0 && Elapsed > 1.0f)
	{
		if (const APlayerController* Controller = GetOwningPlayerController())
		{
			const FKey SkipKeys[] = {
				EKeys::Enter, EKeys::Escape, EKeys::SpaceBar, EKeys::E,
				EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right,
				EKeys::Gamepad_Special_Right, EKeys::LeftMouseButton
			};
			for (const FKey& Key : SkipKeys)
			{
				if (Controller->WasInputKeyJustPressed(Key))
				{
					EndCreditsFinishTime = Now + 0.6;
					break;
				}
			}
		}
	}

	FCanvasTileItem Black(FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor::Black);
	Black.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Black);

	// 결말의 마지막 그림을 아주 어둡게 깔아 둔다. A는 증축 층을 뜯어내는 가을의
	// 빌라, B는 비어 있는 서비스 베이다. 글자를 읽는 데 방해가 되지 않을 만큼만.
	const UIGMissingFloorNarrativeSubsystem* CreditsNarrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	UTexture2D* Backdrop = CreditsNarrative
		&& CreditsNarrative->GetEndingChoice() == FName(TEXT("Ending.B"))
			? EpilogueServiceBayTexture.Get()
			: EpilogueAutumnTexture.Get();
	if (Backdrop && Backdrop->GetSizeX() > 0 && Backdrop->GetSizeY() > 0)
	{
		// 화면을 꽉 채우되 비율은 지킨다. 넘치는 쪽을 잘라 낸다.
		const float ScreenAspect = Canvas->ClipX / FMath::Max(1.0f, Canvas->ClipY);
		const float ImageAspect = static_cast<float>(Backdrop->GetSizeX()) / Backdrop->GetSizeY();
		FVector2D UV0(0.0f, 0.0f);
		FVector2D UV1(1.0f, 1.0f);
		if (ImageAspect > ScreenAspect)
		{
			const float Keep = ScreenAspect / ImageAspect;
			UV0.X = (1.0f - Keep) * 0.5f;
			UV1.X = UV0.X + Keep;
		}
		else
		{
			// 세로 그림은 위쪽을 남긴다. 가을 빌라에서 봐야 할 것은 뜯겨 나가는 옥상 층이다.
			const float Keep = ImageAspect / ScreenAspect;
			UV0.Y = (1.0f - Keep) * 0.12f;
			UV1.Y = UV0.Y + Keep;
		}
		FCanvasTileItem Still(
			FVector2D::ZeroVector,
			Backdrop->GetResource(),
			FVector2D(Canvas->ClipX, Canvas->ClipY),
			UV0,
			UV1,
			FLinearColor(1.0f, 1.0f, 1.0f, 0.22f * FMath::SmoothStep(0.0f, 3.0f, Elapsed)));
		Still.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Still);
	}

	struct FCreditLine
	{
		FText Text;
		EIGHudTextRole Role;
		float TextScale;
		float GapAfter;
	};
	const FCreditLine Lines[] = {
		{NSLOCTEXT("IGHUD", "CreditsGameTitle", "Missing Floor"), EIGHudTextRole::Objective, 1.35f, 90.0f},
		{NSLOCTEXT("IGHUD", "CreditsDeveloper", "기획 · 개발    easygap"), EIGHudTextRole::Prompt, 1.0f, 46.0f},
		{NSLOCTEXT("IGHUD", "CreditsEngine", "제작 도구    Unreal Engine 5.8"), EIGHudTextRole::Hint, 1.0f, 38.0f},
		{NSLOCTEXT("IGHUD", "CreditsMaterials", "일부 재질    ambientCG · CC0"), EIGHudTextRole::Hint, 1.0f, 38.0f},
		{NSLOCTEXT("IGHUD", "CreditsProps", "일부 소품    Poly Haven · CC0"), EIGHudTextRole::Hint, 1.0f, 38.0f},
		{NSLOCTEXT("IGHUD", "CreditsSounds", "일부 소리    OpenGameArt · Kenney · Owlish Media · CC0"), EIGHudTextRole::Hint, 1.0f, 38.0f},
		{NSLOCTEXT("IGHUD", "CreditsFonts", "글꼴    Pretendard · 고운바탕 · SIL OFL"), EIGHudTextRole::Hint, 1.0f, 140.0f},
		{NSLOCTEXT("IGHUD", "EndCreditsThanks", "끝까지 플레이해 주셔서 감사합니다."), EIGHudTextRole::Prompt, 1.0f, 0.0f},
	};
	float ContentHeight = 0.0f;
	for (const FCreditLine& Line : Lines)
	{
		ContentHeight += Line.GapAfter * Scale;
	}
	// 맨 아래에서 올라와 마지막 줄이 화면 가운데에 멈춘다. 멈춘 뒤 4초 두고 걷힌다.
	constexpr float Speed = 42.0f;
	const float Travel = Canvas->ClipY * 0.5f + ContentHeight;
	const float ScrollSeconds = Travel / (Speed * Scale);
	const float Offset = FMath::Min(Elapsed * Speed * Scale, Travel);
	if (EndCreditsFinishTime < 0.0 && Elapsed >= ScrollSeconds + 4.0f)
	{
		EndCreditsFinishTime = Now + 1.6;
	}
	const float FadeIn = FMath::SmoothStep(0.0f, 1.5f, Elapsed);
	const float FadeOut = EndCreditsFinishTime < 0.0
		? 1.0f
		: FMath::Clamp(static_cast<float>((EndCreditsFinishTime - Now) / 1.6), 0.0f, 1.0f);
	const float Alpha = FadeIn * FadeOut;
	float Y = Canvas->ClipY - Offset;
	for (const FCreditLine& Line : Lines)
	{
		if (Y > -80.0f * Scale && Y < Canvas->ClipY + 20.0f)
		{
			FLinearColor Color = Line.Role == EIGHudTextRole::Hint ? IGHorrorHUD::MutedGray : IGHorrorHUD::PaleGray;
			Color.A *= Alpha;
			DrawCenteredText(Line.Text, Y, Color, Line.Role, Line.TextScale * Scale);
		}
		Y += Line.GapAfter * Scale;
	}
#if !UE_BUILD_SHIPPING
	// 미리 보기는 크레딧이 절반쯤 올라왔을 때 한 장 찍고 끝낸다.
	if (bEndCreditsPreview && !bEndCreditsPreviewShot && Offset >= Travel * 0.55f)
	{
		bEndCreditsPreviewShot = true;
		FScreenshotRequest::RequestScreenshot(EndCreditsPreviewPath, false, false);
		FTimerHandle Quit;
		GetWorldTimerManager().SetTimer(Quit, FTimerDelegate::CreateWeakLambda(this, []()
		{
			FPlatformMisc::RequestExit(false);
		}), 1.5f, false);
	}
#endif
	if (EndCreditsFinishTime >= 0.0 && Now >= EndCreditsFinishTime)
	{
		FinishEndCredits();
		return false;
	}
	return true;
}

void AIGHorrorHUD::UpdatePrintedTextReading()
{
	if (IsKoreanCulture() || (bReadCaptureMercyNote && bReadDoorMercyNote))
	{
		return;
	}
	const APlayerController* Controller = GetOwningPlayerController();
	if (!Controller || !GetWorld())
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	// 발치의 종이를 내려다보고 있을 때만. 지나가며 곁눈으로 본 것은 읽은 게 아니다.
	auto IsReading = [&ViewLocation, &ViewRotation](const FVector& Where)
	{
		const FVector ToPaper = Where - ViewLocation;
		return ToPaper.SizeSquared() <= FMath::Square(190.0f)
			&& FVector::DotProduct(ToPaper.GetSafeNormal(), ViewRotation.Vector()) >= 0.93f;
	};
	if (!bReadCaptureMercyNote)
	{
		if (!NightLoopForNotes.IsValid())
		{
			for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It)
			{
				NightLoopForNotes = *It;
				break;
			}
		}
		const AIGNightLoopDirector* Loop = NightLoopForNotes.Get();
		if (Loop && Loop->IsMercyNoteVisible() && !Loop->IsMercyNoteSliding()
			&& IsReading(Loop->GetMercyNoteLocation()))
		{
			bReadCaptureMercyNote = true;
			PushThought(
				this,
				NSLOCTEXT("IGMissingFloor", "CaptureMercyNoteRead", "“소리를 줄여라. 걔는 눈이 없어.”"),
				3.4f);
		}
	}
	if (!bReadDoorMercyNote)
	{
		const AIGMissingFloorMercyDirector* Mercy = MercyForTips.Get();
		if (Mercy && Mercy->IsNoteVisible() && !Mercy->IsNoteSliding()
			&& IsReading(Mercy->GetNoteLocation()))
		{
			bReadDoorMercyNote = true;
			PushThought(
				this,
				NSLOCTEXT("IGMissingFloor", "MercyNoteUnderDoorRead", "“낮에 와. 문 열어 둘게.”"),
				3.0f);
		}
	}
}

void AIGHorrorHUD::DrawContextTip(const float Alpha)
{
	FText Line;
	switch (ContextTips.GetCurrent())
	{
	case EIGContextTip::Crouch:
		Line = FText::Format(
			NSLOCTEXT("IGHUD", "TipCrouch", "[ {0} ]  앉아서 걷기  ·  발소리가 작아진다"),
			GetBoundKeyLabel(EIGBindableAction::Crouch, bUsingGamepad));
		break;
	case EIGContextTip::HoldBreath:
		Line = FText::Format(
			NSLOCTEXT("IGHUD", "TipHoldBreath", "[ {0} ]  숨 참기  ·  오래 참으면 숨이 거칠어진다"),
			GetBoundKeyLabel(EIGBindableAction::HoldBreath, bUsingGamepad));
		break;
	case EIGContextTip::Sprint:
		Line = FText::Format(
			NSLOCTEXT("IGHUD", "TipSprint", "[ {0} ]  달리기  ·  발소리도 커진다"),
			GetBoundKeyLabel(EIGBindableAction::Sprint, bUsingGamepad));
		break;
	case EIGContextTip::Journal:
		Line = FText::Format(
			NSLOCTEXT("IGHUD", "TipJournal", "[ {0} ]  조사 기록  ·  낮에 확인할 수 있다"),
			GetBoundKeyLabel(EIGBindableAction::Journal, bUsingGamepad));
		break;
	case EIGContextTip::Hint:
		Line = FText::Format(
			NSLOCTEXT("IGHUD", "TipHint", "[ {0} ]  힌트  ·  막혔을 때 누르기"),
			GetBoundKeyLabel(EIGBindableAction::RequestHint, bUsingGamepad));
		break;
	default:
		return;
	}
	FLinearColor Color = IGHorrorHUD::PaleGray;
	Color.A *= Alpha;
	DrawCenteredText(Line, Canvas->ClipY - 76.0f, Color, EIGHudTextRole::Hint);
}

void AIGHorrorHUD::DrawControlsGuide(const float Alpha, const bool bFull)
{
	FLinearColor Color = IGHorrorHUD::MutedGray;
	Color.A *= Alpha;
	if (!bFull)
	{
		// 입주 저녁 첫 안내. 걷고, 둘러보고, 조사하는 것만 알려 준다.
		// 나머지는 쓸 일이 생길 때 상황 안내가 한 줄씩 꺼낸다.
		const FText Basics = bUsingGamepad
			? FText::Format(
				NSLOCTEXT("IGHUD", "GuideBasicsGamepad", "LS 이동  ·  RS 둘러보기  ·  {0} 조사"),
				GetBoundKeyLabel(EIGBindableAction::Interact, true))
			: FText::Format(
				NSLOCTEXT("IGHUD", "GuideBasicsKeyboard", "WASD 이동  ·  마우스로 둘러보기  ·  {0} 조사"),
				GetBoundKeyLabel(EIGBindableAction::Interact, false));
		const FText Recall = FText::Format(
			NSLOCTEXT("IGHUD", "GuideRecallOpen", "{0} 목표와 조작 다시 보기"),
			GetBoundKeyLabel(EIGBindableAction::GameplayGuide, bUsingGamepad));
		DrawCenteredText(Basics, Canvas->ClipY - 76.0f, Color, EIGHudTextRole::Hint);
		DrawCenteredText(Recall, Canvas->ClipY - 40.0f, Color, EIGHudTextRole::Hint);
		return;
	}

	const FText Movement = FText::Format(
		bUsingGamepad
			? NSLOCTEXT("IGHUD", "GuideMovementGamepad", "LS 이동  ·  {0} 달리기  ·  {1} 앉기  ·  {2} 점프")
			: NSLOCTEXT("IGHUD", "GuideMovementKeyboard", "WASD 이동  ·  {0} 달리기  ·  {1} 앉기  ·  {2} 점프"),
		GetBoundKeyLabel(EIGBindableAction::Sprint, bUsingGamepad),
		GetBoundKeyLabel(EIGBindableAction::Crouch, bUsingGamepad),
		GetBoundKeyLabel(EIGBindableAction::Jump, bUsingGamepad));
	const FText Actions = FText::Format(
		NSLOCTEXT("IGHUD", "GuideActions", "{0} 조사  ·  {1} 두드리기  ·  {2} 숨 참기  ·  {3} 손전등"),
		GetBoundKeyLabel(EIGBindableAction::Interact, bUsingGamepad),
		GetBoundKeyLabel(EIGBindableAction::Knock, bUsingGamepad),
		GetBoundKeyLabel(EIGBindableAction::HoldBreath, bUsingGamepad),
		GetBoundKeyLabel(EIGBindableAction::Flashlight, bUsingGamepad));
	// 지금 누르면 실제로 뭔가 되는 키만 적는다. 밤에 기록 키를 적어 두면
	// 눌러 본 사람은 “지금은 그럴 때가 아니야.”만 듣는다.
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	const bool bJournalAvailable = !bNightPresentation && !(Narrative && Narrative->IsHourSealed());
	TArray<FText> RecallParts;
	RecallParts.Add(FText::Format(
		NSLOCTEXT("IGHUD", "GuideRecallClose", "{0} 안내 닫기"),
		GetBoundKeyLabel(EIGBindableAction::GameplayGuide, bUsingGamepad)));
	if (bJournalAvailable)
	{
		RecallParts.Add(FText::Format(
			NSLOCTEXT("IGHUD", "GuideRecallJournal", "{0} 조사 기록"),
			GetBoundKeyLabel(EIGBindableAction::Journal, bUsingGamepad)));
	}
	if (IsHintRequestAvailable())
	{
		RecallParts.Add(FText::Format(
			NSLOCTEXT("IGHUD", "GuideRecallHint", "{0} 힌트"),
			GetBoundKeyLabel(EIGBindableAction::RequestHint, bUsingGamepad)));
	}
	const FText Recall = FText::Join(NSLOCTEXT("IGHUD", "GuideSeparator", "  ·  "), RecallParts);
	DrawCenteredText(Movement, Canvas->ClipY - 112.0f, Color, EIGHudTextRole::Hint);
	DrawCenteredText(Actions, Canvas->ClipY - 76.0f, Color, EIGHudTextRole::Hint);
	DrawCenteredText(Recall, Canvas->ClipY - 40.0f, Color, EIGHudTextRole::Hint);
}

bool AIGHorrorHUD::IsKoreanCulture()
{
	return FInternationalization::Get().GetCurrentCulture()->GetTwoLetterISOLanguageName() == TEXT("ko");
}

FString AIGHorrorHUD::GetLineHeightSample()
{
	// 줄 높이를 잴 때 쓰는 글자. 그 언어에서 가장 키가 큰 글자와 영문 대소문자를
	// 섞어 둔다. 한국어 표본으로 한자를 재면 획이 아래위로 잘린다.
	return NSLOCTEXT("IGHUD", "LineHeightSample", "한Ag").ToString();
}

bool AIGHorrorHUD::IsHintRequestAvailable() const
{
	const UIGAccessibilitySubsystem* Accessibility = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	return !Accessibility || Accessibility->AreHintsEnabled();
}

void AIGHorrorHUD::OfferHintTip()
{
	if (IsHintRequestAvailable())
	{
		ContextTips.Offer(EIGContextTip::Hint);
	}
}

void AIGHorrorHUD::ToggleGameplayGuide()
{
	if (CanShowGameplayGuide()) Guidance.ToggleRecall();
}

void AIGHorrorHUD::DrawHUD()
{
	Super::DrawHUD();
	bAudioCaptionDrawnInLastHudFrame = false;
	if (bTextAuditEnabled)
	{
		FinishTextAuditFrame();
	}
	// 자막 시계는 프레임마다 그 순간의 멈춤 상태로 센다. 일시정지 메뉴처럼 자막을
	// 그리지 않는 화면에서도 세어야 메뉴를 닫을 때 멈춘 시간이 한꺼번에 들어오지 않는다.
	AdvanceAudioCaptionClock();

	if (!bShowHUD || !Canvas || !GEngine)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const double CurrentTime = World ? World->GetTimeSeconds() : 0.0;
#if !UE_BUILD_SHIPPING
	if (bFirstPersonKnockPreview
		&& CurrentTime >= FirstPersonKnockPreviewNextTime)
	{
		PlayFirstPersonKnock();
		FirstPersonKnockPreviewNextTime = CurrentTime + 0.62;
	}
	if (bCaptureEmbracePreview
		&& CurrentTime >= CaptureEmbracePreviewNextTime)
	{
		PlayCaptureEmbrace(
			static_cast<float>(IGHorrorHUD::CaptureEmbraceDurationSeconds));
		CaptureEmbracePreviewNextTime = CurrentTime + 1.8;
	}
	if (bCaptureWakeEchoPreview
		&& CurrentTime >= CaptureWakeEchoPreviewNextTime)
	{
		PlayCaptureWakeEcho(1, 0.68f);
		CaptureWakeEchoPreviewNextTime = CurrentTime + 1.45;
	}
#endif
	BeginLayoutValidationSample();
	if (bEndCreditsActive)
	{
		SuspendDialoguePresentation(CurrentTime);
		bTextAuditScrolling = true;
		DrawEndCredits();
		bTextAuditScrolling = false;
		LastHudDrawTime = CurrentTime;
		FinalizeLayoutValidationSample();
		return;
	}
	if (DrawCaptureEmbrace(CurrentTime))
	{
		// 실제 몸의 접촉과 암전이 끝날 때까지 일반 안내를 가린다.
		SuspendDialoguePresentation(CurrentTime);
		LastHudDrawTime = CurrentTime;
		FinalizeLayoutValidationSample();
		return;
	}
	if (DrawCaptureWakeEcho(CurrentTime))
	{
		// 침대에서 시야가 돌아오는 동안 입력과 안내를 함께 보류한다.
		SuspendDialoguePresentation(CurrentTime);
		LastHudDrawTime = CurrentTime;
		FinalizeLayoutValidationSample();
		return;
	}
	if (bAccessibilityMenuVisible)
	{
		Guidance.DismissRecall();
		SuspendDialoguePresentation(CurrentTime);
		DrawAccessibilityPanel();
		FinalizeLayoutValidationSample();
		return;
	}
	if (bSystemMenuVisible)
	{
		Guidance.DismissRecall();
		SuspendDialoguePresentation(CurrentTime);
		DrawSystemMenuPanel();
		FinalizeLayoutValidationSample();
		return;
	}
	if (bMissingFloorJournalVisible)
	{
		Guidance.DismissRecall();
		SuspendDialoguePresentation(CurrentTime);
		DrawMissingFloorJournalPanel();
		FinalizeLayoutValidationSample();
		return;
	}
	if (DrawMissingFloorEpilogue(CurrentTime))
	{
		// 막간과 같은 규칙이다. 화면은 에필로그가 통째로 갖되, 환경음 자막은
		// 그리는 것을 허락한다 — 몽타주 구간에서는 그것이 유일한 그림이다.
		SuspendDialoguePresentation(CurrentTime);
		DrawAudioCaption(CurrentTime, Canvas->ClipY - 24.0f);
		LastHudDrawTime = CurrentTime;
		FinalizeLayoutValidationSample();
		return;
	}
	if (DrawMissingFloorFailureEnding(CurrentTime))
	{
		SuspendDialoguePresentation(CurrentTime);
		LastHudDrawTime = CurrentTime;
		FinalizeLayoutValidationSample();
		return;
	}

	if (DrawChapterCard(CurrentTime))
	{
		// 에필로그와 같은 규칙이다. 카드는 대사를 멈추지만 카드 밑에서 난 소리의
		// 자막은 그린다 — 04:30 알람도 입주 카드 밑의 소리도 카드 동안 만료됐다.
		SuspendDialoguePresentation(CurrentTime);
		DrawAudioCaption(CurrentTime, Canvas->ClipY - 24.0f);
		LastHudDrawTime = CurrentTime;
		FinalizeLayoutValidationSample();
		return;
	}
	ResumeDialoguePresentation(CurrentTime);
	if (bSensoryInterludePresentation)
	{
		// Camera fade supplies the black image. The HUD contributes only the
		// optional directional sound caption; no crosshair or objective can
		// make the scene read as ordinary gameplay with a missing render.
		SuspendDialoguePresentation(CurrentTime);
		DrawSensoryInterludeSkip();
		DrawAudioCaption(CurrentTime, Canvas->ClipY - 24.0f);
		LastHudDrawTime = CurrentTime;
		FinalizeLayoutValidationSample();
		return;
	}
	if (!InteractionComponent.IsValid())
	{
		ResolveInteractionComponent();
	}

	const UIGInteractionComponent* Interaction = InteractionComponent.Get();
	const bool bHasFocus = Interaction && IsValid(Interaction->GetFocusedActor());

	const float DeltaSeconds = LastHudDrawTime > 0.0
		? FMath::Clamp(static_cast<float>(CurrentTime - LastHudDrawTime), 0.0f, 0.2f)
		: 0.0f;
	LastHudDrawTime = CurrentTime;

	// 읽는 동안에는 종이 뒤에 조준점과 목표를 남기지 않는다.
	if (AIGReadableNote::GetOpenNote())
	{
		Guidance.DismissRecall();
		SuspendDialoguePresentation(CurrentTime);
		DrawNotePanel();
		DrawAudioCaption(CurrentTime, Canvas->ClipY - 24.0f);
		FinalizeLayoutValidationSample();
		return;
	}
	ResumeDialoguePresentation(CurrentTime);
	// 가림막은 자막과 안내보다 먼저 깐다. 숨은 채로도 글은 읽혀야 한다.
	DrawHidingMask();

	const FText Objective = GetObjectiveText();
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
	const bool bTutorialAllowed = !bNightPresentation && (!Narrative
		|| (Narrative->GetNightIndex() == 0 && !Narrative->HasBeatPlayed(TEXT("Arrival.Complete"))));
	// 밤의 시계는 05:00, 05:20, 05:25에 한 번씩 손목을 본다. 매분은 아니다 —
	// 밤의 HUD는 조용해야 한다. 잡혔다 깨어날 때도 한 번(NightClockRevealSerial).
	int32 NightClockBand = 0;
	if (bNightPresentation && Narrative)
	{
		const int32 StoryMinute = AIGNightPhaseDirector::GetStoryMinuteAt(
			Narrative->GetNightIndex(), Narrative->GetNightElapsedSeconds());
		NightClockBand = StoryMinute >= 5 * 60 + 25 ? 3
			: (StoryMinute >= 5 * 60 + 20 ? 2 : (StoryMinute >= 5 * 60 ? 1 : 0));
	}
	const FString ObjectiveKey = bNightPresentation
		? FString::Printf(TEXT("Night.%d.%d.%d"), Narrative ? Narrative->GetNightIndex() : 1, NightClockBand, NightClockRevealSerial)
		: Objective.ToString();
	const bool bGuideAllowed = CanShowGameplayGuide();
	NoteCompletedInteractions(Interaction);
	if (bGuideAllowed)
	{
		// 대사나 소리 자막에 가려 있던 동안은 첫 안내의 20초에서 빼지 않는다.
		Guidance.Update(DeltaSeconds, ObjectiveKey, bTutorialAllowed, !bLowerLaneBusyLastFrame);
		if (Guidance.WasTutorialCompleted() && !bControlsIntroRecorded)
		{
			bControlsIntroRecorded = true;
			IGOnboardingMemory::MarkControlsIntroDone();
		}
	}
	else
	{
		ContextTips.Interrupt();
	}
	const float ObjectiveAlpha = bGuideAllowed ? Guidance.ObjectiveAlpha() : 0.0f;
	const float ControlsAlpha = bGuideAllowed ? Guidance.ControlsAlpha() : 0.0f;

	// 페이드로 화면이 검게 덮인 동안에는 조준점도 초점 틀도 안내 문구도 없다.
	// 잠든 사이, 층을 옮기는 사이의 검은 화면에 「조사」가 떠 있으면 연출이 깨진다.
	const bool bFadedOut = IsCameraFadedOut();
	UpdateFocusBracket(bHasFocus && !bFadedOut ? Interaction->GetFocusedActor() : nullptr, DeltaSeconds);
	DrawFirstPersonKnock(CurrentTime);
	if (!bFadedOut)
	{
		DrawCenterDot(bHasFocus);
	}
	const float HoldProgress = Interaction ? Interaction->GetHoldProgress() : 0.0f;
	if (FocusBracketAlpha > 0.01f)
	{
		DrawFocusBracket(
			IGHorrorHUD::RedAccent,
			HoldProgress);
	}
	DrawFearDirection(CurrentTime);
	DrawNoiseRipple(CurrentTime);
	DrawSaveIndicator(CurrentTime);

	if (ObjectiveAlpha > 0.01f)
	{
		if (bNightPresentation)
		{
			DrawNightClock();
		}
		else if (!Objective.IsEmpty())
		{
			FLinearColor Color = IGHorrorHUD::PaleGray;
			Color.A *= ObjectiveAlpha;
			DrawCenteredText(Objective, 42.0f, Color, EIGHudTextRole::Objective);
		}
	}

	// 대사와 소리 자막의 실제 윗변을 먼저 잰다. 자막을 크게 써도 조사 안내가
	// 그 뒤에 가려지지 않도록 같은 프레임의 배치를 나눠 쓴다.
	float DialoguePanelTop = Canvas->ClipY;
	const bool bDialogueVisible = DrawDialoguePanel(CurrentTime, DialoguePanelTop);
	const float DialogueLaneGap = 14.0f * FMath::Clamp(
		Canvas->ClipY / 1080.0f,
		0.85f,
		2.0f);
	float AudioCaptionPanelTop = Canvas->ClipY;
	const bool bAudioCaptionVisible = DrawAudioCaption(
		CurrentTime,
		bDialogueVisible
			? DialoguePanelTop - DialogueLaneGap
			: Canvas->ClipY - 54.0f,
		&AudioCaptionPanelTop);

	// 초점이 잡힌 대상의 조사 안내와 누르기 진행.
	if (bHasFocus && !bFadedOut)
	{
		const FText FocusedPrompt = Interaction->GetFocusedPrompt();
		if (!FocusedPrompt.IsEmpty())
		{
			const AActor* FocusedActor = Interaction->GetFocusedActor();
			const bool bKnockVerb = FocusedActor
				&& FocusedActor->ActorHasTag(
					FName(TEXT("MissingFloor.Verb.Knock")));
			const bool bListenVerb = FocusedActor
				&& FocusedActor->ActorHasTag(
					FName(TEXT("MissingFloor.Verb.Listen")));
			// 키 이름은 지금 묶인 키다. 재설정 화면이 있는데 프롬프트가 「E」로
			// 굳어 있으면 키를 바꾼 사람은 화면이 시키는 대로 눌러도 아무 일이 없다.
			// 엿듣기는 키보드에서 E 홀드가 문맥 전환되므로 Interact 키를 보인다.
			const EIGBindableAction PromptAction = bKnockVerb
				? EIGBindableAction::Knock
				: (bListenVerb && bUsingGamepad)
					? EIGBindableAction::Listen
					: EIGBindableAction::Interact;
			// 같은 동작을 세 번 해 본 뒤로는 키 이름을 떼고 대상만 남긴다.
			// 키를 새로 묶으면 다시 배워야 하므로, 키를 바꾸면 기억도 소용이 없게
			// 하는 대신 「항상 표시」 설정을 둔다.
			const TCHAR* PromptKind = bKnockVerb
				? TEXT("Knock")
				: (bListenVerb ? TEXT("Listen") : TEXT("Interact"));
			const UIGAccessibilitySubsystem* PromptAccessibility = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>()
				: nullptr;
			const bool bShowKey = !IGOnboardingMemory::HasLearnedPrompt(PromptKind)
				|| (PromptAccessibility && PromptAccessibility->GetSettings().bAlwaysShowPromptKeys);
			const FText KeyLabel = GetBoundKeyLabel(PromptAction, bUsingGamepad);
			const FText PromptFormat = bKnockVerb
				? bUsingGamepad
					? NSLOCTEXT("IGHUD", "KnockPromptFormatGamepad", "[ {1} ]  {0}")
					: NSLOCTEXT("IGHUD", "KnockPromptFormatKeyboard", "[ {1} ]  {0}")
				: bListenVerb
					? bUsingGamepad
						? NSLOCTEXT("IGHUD", "ListenPromptFormatGamepad", "[ {1} ]  {0}")
						: NSLOCTEXT("IGHUD", "ListenPromptFormatKeyboard", "[ {1} ]  {0}")
					: bUsingGamepad
						? NSLOCTEXT("IGHUD", "PromptFormatGamepad", "[ {1} ]  {0}")
						: NSLOCTEXT("IGHUD", "PromptFormatKeyboard", "[ {1} ]  {0}");
			const FText Prompt = bShowKey
				? FText::Format(PromptFormat, FocusedPrompt, KeyLabel)
				: FocusedPrompt;
			// 문 바로 앞에서는 브래킷 하단이 화면 밖으로 나간다. 안내 문구는
			// 글자 높이와 하단 조작 안내 여백을 빼고 화면 안에 남긴다.
			const float PromptHeight = MeasureTextHeight(
				Prompt.ToString(), GetFontForRole(EIGHudTextRole::Prompt), 1.f);
			const float LowerGuideReserve = ControlsAlpha > 0.01f
				? 138.f
				: (ContextTips.Alpha() > 0.01f ? 100.f : 54.f);
			float PromptBottomEdge = Canvas->ClipY - LowerGuideReserve;
			if (bDialogueVisible)
			{
				PromptBottomEdge = FMath::Min(PromptBottomEdge, DialoguePanelTop - DialogueLaneGap);
			}
			if (bAudioCaptionVisible)
			{
				PromptBottomEdge = FMath::Min(PromptBottomEdge, AudioCaptionPanelTop - DialogueLaneGap);
			}
			const float PromptBottomLimit = FMath::Max(0.f, PromptBottomEdge - PromptHeight);
			const float PromptTop = FMath::Clamp(FMath::Max(
				(Canvas->ClipY * 0.5f) + 54.0f,
				FocusBracketAlpha > 0.01f ? FocusBracketMax.Y + 14.0f : 0.0f), 0.f, PromptBottomLimit);
			FLinearColor PromptColor = IGHorrorHUD::RedAccent;
			// 익힌 뒤의 안내는 한 단계 낮춘다. 대상 이름은 여전히 읽혀야 한다.
			PromptColor.A *= bShowKey ? 1.0f : 0.82f;
			DrawCenteredText(
				Prompt,
				PromptTop,
				PromptColor,
				EIGHudTextRole::Prompt);
		}
	}


	// 아래쪽 줄은 대사와 소리 자막이 먼저 쓴다. 자막이 뜨면 조작 안내는 바로
	// 비키고, 자막이 걷히면 천천히 돌아온다. 툭 튀어나오면 눈이 그리로 간다.
	const bool bLowerLaneBusy = bDialogueVisible || bAudioCaptionVisible;
	bLowerLaneBusyLastFrame = bLowerLaneBusy;
	ControlsLaneAlpha = bLowerLaneBusy
		? 0.0f
		: FMath::FInterpTo(ControlsLaneAlpha, 1.0f, DeltaSeconds, 6.0f);
	if (bGuideAllowed)
	{
		UpdateContextTips(DeltaSeconds, !bLowerLaneBusy && !bFadedOut && ControlsAlpha <= 0.01f);
		UpdatePrintedTextReading();
	}
	if (!bLowerLaneBusy && !bFadedOut)
	{
		if (ControlsAlpha > 0.01f)
		{
			DrawControlsGuide(ControlsAlpha * ControlsLaneAlpha, Guidance.IsRecalled());
		}
		else if (bGuideAllowed && ContextTips.Alpha() > 0.01f)
		{
			DrawContextTip(ContextTips.Alpha() * ControlsLaneAlpha);
		}
	}

	FinalizeLayoutValidationSample();
}

bool AIGHorrorHUD::GetLayoutValidationSample(
	FVector2D& OutCanvasSize,
	FVector2D& OutBoundsMin,
	FVector2D& OutBoundsMax,
	int32& OutElementCount,
	bool& bOutAllInsideCanvas,
	bool& bOutAllInsideSettingsContainers,
	uint64& OutFrameSerial) const
{
	if (!bLayoutValidationEnabled || !bLayoutValidationSampleReady)
	{
		return false;
	}
	OutCanvasSize = LayoutValidationCanvasSize;
	OutBoundsMin = LayoutValidationBoundsMin;
	OutBoundsMax = LayoutValidationBoundsMax;
	OutElementCount = LayoutValidationElementCount;
	bOutAllInsideCanvas = bLayoutValidationAllInsideCanvas;
	bOutAllInsideSettingsContainers =
		bLayoutValidationAllInsideSettingsContainers;
	OutFrameSerial = LayoutValidationFrameSerial;
	return true;
}

bool AIGHorrorHUD::GetDialogueRenderSample(
	FVector2D& OutPanelMinimum,
	FVector2D& OutPanelMaximum,
	FVector2D& OutCanvasSize,
	int32& OutLineCount,
	bool& bOutSpeakerVisible,
	bool& bOutHasContinuation,
	bool& bOutInsideSafeArea,
	uint64& OutFrameSerial) const
{
	if (DialogueLastRenderSerial == 0
		|| DialogueLastLineCount <= 0
		|| DialogueLastPanelMaximum.X <= DialogueLastPanelMinimum.X
		|| DialogueLastPanelMaximum.Y <= DialogueLastPanelMinimum.Y)
	{
		return false;
	}
	OutPanelMinimum = DialogueLastPanelMinimum;
	OutPanelMaximum = DialogueLastPanelMaximum;
	OutCanvasSize = DialogueLastCanvasSize;
	OutLineCount = DialogueLastLineCount;
	bOutSpeakerVisible = bDialogueLastSpeakerVisible;
	bOutHasContinuation = bDialogueLastHasContinuation;
	bOutInsideSafeArea = bDialogueLastInsideSafeArea;
	OutFrameSerial = DialogueLastRenderSerial;
	return true;
}

void AIGHorrorHUD::BeginLayoutValidationSample()
{
	if (!bLayoutValidationEnabled)
	{
		return;
	}
	LayoutValidationCanvasSize = FVector2D(Canvas->ClipX, Canvas->ClipY);
	LayoutValidationBoundsMin = FVector2D(
		TNumericLimits<float>::Max(),
		TNumericLimits<float>::Max());
	LayoutValidationBoundsMax = FVector2D(
		TNumericLimits<float>::Lowest(),
		TNumericLimits<float>::Lowest());
	LayoutValidationElementCount = 0;
	bLayoutValidationAllInsideCanvas = true;
	bLayoutValidationAllInsideSettingsContainers = true;
	bLayoutValidationSampleReady = false;
}

void AIGHorrorHUD::RecordTextAudit(
	const FString& Text,
	const FVector2D& Min,
	const FVector2D& Max)
{
	if (!bTextAuditEnabled || !Canvas || Text.TrimStartAndEnd().IsEmpty())
	{
		return;
	}
	FTextAuditEntry& Entry = TextAuditEntries.AddDefaulted_GetRef();
	Entry.Text = Text.Replace(TEXT("\n"), TEXT(" / "));
	Entry.Min = Min;
	Entry.Max = Max;
	Entry.bScrolling = bTextAuditScrolling;
	Entry.Container = TextAuditContainerStack.Num() > 0
		? TextAuditContainerStack.Last()
		: INDEX_NONE;
	TextAuditCanvasSize = FVector2D(Canvas->ClipX, Canvas->ClipY);
}

void AIGHorrorHUD::PushTextAuditContainer(const FVector2D& Min, const FVector2D& Max)
{
	if (bTextAuditEnabled)
	{
		TextAuditContainerStack.Add(TextAuditContainers.Add(FBox2D(Min, Max)));
	}
}

void AIGHorrorHUD::PopTextAuditContainer()
{
	if (TextAuditContainerStack.Num() > 0)
	{
		TextAuditContainerStack.Pop();
	}
}

void AIGHorrorHUD::FinishTextAuditFrame()
{
	const FString Culture = FInternationalization::Get().GetCurrentCulture()->GetName();
	auto Report = [this, &Culture](const TCHAR* Kind, const FString& First, const FString& Second)
	{
		const uint32 Key = HashCombine(
			GetTypeHash(FString(Kind)),
			HashCombine(GetTypeHash(First), GetTypeHash(Second)));
		if (TextAuditReported.Contains(Key))
		{
			return;
		}
		TextAuditReported.Add(Key);
		TextAuditFailures.Add(FString::Printf(
			TEXT("TEXT_AUDIT %s culture=%s canvas=%.0fx%.0f \"%s\" \"%s\""),
			Kind, *Culture, TextAuditCanvasSize.X, TextAuditCanvasSize.Y,
			*First.Left(80), *Second.Left(80)));
		UE_LOG(LogTemp, Warning, TEXT("TEXT_AUDIT %s culture=%s canvas=%.0fx%.0f \"%s\" \"%s\""),
			Kind, *Culture, TextAuditCanvasSize.X, TextAuditCanvasSize.Y,
			*First.Left(80), *Second.Left(80));
	};
	// 상자 높이에는 줄 간격이 들어 있어 이웃 줄과 조금 겹치는 것은 정상이다.
	// 작은 쪽 높이의 30%를 넘게 겹쳐야 글자가 실제로 겹친 것으로 본다.
	constexpr float Tolerance = 1.5f;
	for (int32 First = 0; First < TextAuditEntries.Num(); ++First)
	{
		const FTextAuditEntry& A = TextAuditEntries[First];
		if (!A.bScrolling && (A.Min.X < -Tolerance || A.Min.Y < -Tolerance
			|| A.Max.X > TextAuditCanvasSize.X + Tolerance
			|| A.Max.Y > TextAuditCanvasSize.Y + Tolerance))
		{
			Report(TEXT("OFFSCREEN"), A.Text, FString());
		}
		if (TextAuditContainers.IsValidIndex(A.Container))
		{
			const FBox2D& Panel = TextAuditContainers[A.Container];
			if (A.Min.X < Panel.Min.X - Tolerance || A.Min.Y < Panel.Min.Y - Tolerance
				|| A.Max.X > Panel.Max.X + Tolerance || A.Max.Y > Panel.Max.Y + Tolerance)
			{
				Report(TEXT("OUTSIDE_PANEL"), A.Text, FString::Printf(
					TEXT("text=%.0f,%.0f-%.0f,%.0f panel=%.0f,%.0f-%.0f,%.0f"),
					A.Min.X, A.Min.Y, A.Max.X, A.Max.Y,
					Panel.Min.X, Panel.Min.Y, Panel.Max.X, Panel.Max.Y));
			}
		}
		for (int32 Second = First + 1; Second < TextAuditEntries.Num(); ++Second)
		{
			const FTextAuditEntry& B = TextAuditEntries[Second];
			const float OverlapX = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X);
			const float OverlapY = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);
			const float SmallerHeight = FMath::Min(A.Max.Y - A.Min.Y, B.Max.Y - B.Min.Y);
			if (OverlapX > 3.0f && OverlapY > SmallerHeight * 0.3f)
			{
				Report(TEXT("OVERLAP"), A.Text, B.Text);
			}
		}
	}
	TextAuditEntries.Reset();
	TextAuditContainers.Reset();
	TextAuditContainerStack.Reset();
}

void AIGHorrorHUD::RecordLayoutValidationRect(
	const FVector2D& Minimum,
	const FVector2D& Maximum)
{
	if (!bLayoutValidationEnabled || !Canvas)
	{
		return;
	}
	LayoutValidationBoundsMin.X = FMath::Min(
		LayoutValidationBoundsMin.X,
		Minimum.X);
	LayoutValidationBoundsMin.Y = FMath::Min(
		LayoutValidationBoundsMin.Y,
		Minimum.Y);
	LayoutValidationBoundsMax.X = FMath::Max(
		LayoutValidationBoundsMax.X,
		Maximum.X);
	LayoutValidationBoundsMax.Y = FMath::Max(
		LayoutValidationBoundsMax.Y,
		Maximum.Y);
	++LayoutValidationElementCount;
	constexpr float PixelTolerance = 1.5f;
	bLayoutValidationAllInsideCanvas =
		bLayoutValidationAllInsideCanvas
		&& Minimum.X >= -PixelTolerance
		&& Minimum.Y >= -PixelTolerance
		&& Maximum.X <= Canvas->ClipX + PixelTolerance
		&& Maximum.Y <= Canvas->ClipY + PixelTolerance;
}

void AIGHorrorHUD::FinalizeLayoutValidationSample()
{
	if (!bLayoutValidationEnabled)
	{
		return;
	}
	if (LayoutValidationElementCount <= 0)
	{
		LayoutValidationBoundsMin = FVector2D::ZeroVector;
		LayoutValidationBoundsMax = FVector2D::ZeroVector;
		bLayoutValidationAllInsideCanvas = false;
	}
	++LayoutValidationFrameSerial;
	bLayoutValidationSampleReady = true;
}

float AIGHorrorHUD::GetResolutionTextScale(const float UserScale) const
{
	if (!Canvas)
	{
		return FMath::Clamp(UserScale, 0.85f, 2.0f);
	}
	const float ResolutionScale = FMath::Clamp(
		Canvas->ClipY / 1080.0f,
		0.85f,
		2.0f);
	return FMath::Clamp(UserScale, 0.85f, 2.0f) * ResolutionScale;
}

void AIGHorrorHUD::PrepareDialoguePage(
	const float TextScale,
	const float MaximumWidth,
	const int32 MaximumLines,
	const double CurrentTime)
{
	if (!bHasCurrentDialogue
		|| (CurrentDialogueLines.Num() > 0
			&& FMath::IsNearlyEqual(DialogueLayoutScale, TextScale, 0.01f)
			&& FMath::IsNearlyEqual(DialogueLayoutWidth, MaximumWidth, 1.0f)
			&& DialogueLayoutMaximumLines == MaximumLines))
	{
		return;
	}

	TArray<FString> Lines;
	FString Remainder;
	WrapHudText(
		CurrentDialogue.Line.ToString(),
		GetFontForRole(EIGHudTextRole::Dialogue),
		TextScale,
		MaximumWidth,
		MaximumLines,
		Lines,
		Remainder);
	if (Lines.IsEmpty())
	{
		Lines.Add(CurrentDialogue.Line.ToString());
	}

	if (!Remainder.IsEmpty())
	{
		FIGDialogueMessage Continuation = CurrentDialogue;
		Continuation.Line = FText::FromString(Remainder);
		Continuation.MinimumDurationSeconds = 0.0f;
		Continuation.QueuedAt = CurrentTime;
		Continuation.bContinuation = true;
		DialogueQueue.Insert(MoveTemp(Continuation), 0);
		if (DialogueQueue.Num() > IGHorrorHUD::MaximumDialogueQueueDepth)
		{
			DialogueQueue.RemoveAt(DialogueQueue.Num() - 1);
		}

		CurrentDialogue.Line = FText::FromString(FString::Join(Lines, TEXT("\n")));
		bCurrentDialogueHasContinuation = true;
		DialogueStartTime = CurrentTime;
		DialogueEndTime = CurrentTime + CalculateDialogueDuration(
			CurrentDialogue.Line.ToString(),
			CurrentDialogue.MinimumDurationSeconds);
	}
	CurrentDialogueLines = MoveTemp(Lines);
	DialogueLayoutScale = TextScale;
	DialogueLayoutWidth = MaximumWidth;
	DialogueLayoutMaximumLines = MaximumLines;
}

void AIGHorrorHUD::DrawRoundedHudSurface(
	const FVector2D& Position,
	const FVector2D& Size,
	const float CornerRadius,
	const FLinearColor& Color) const
{
	if (!Canvas || Size.X <= 0.0f || Size.Y <= 0.0f || Color.A <= 0.001f)
	{
		return;
	}

	if (!HudRoundedMaskTexture || !HudRoundedMaskTexture->GetResource())
	{
		FCanvasTileItem Fallback(Position, Size, Color);
		Fallback.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Fallback);
		return;
	}

	const float Radius = FMath::Clamp(
		CornerRadius,
		1.0f,
		FMath::Min(Size.X, Size.Y) * 0.5f);
	const float XStops[] = {
		Position.X,
		Position.X + Radius,
		Position.X + Size.X - Radius,
		Position.X + Size.X,
	};
	const float YStops[] = {
		Position.Y,
		Position.Y + Radius,
		Position.Y + Size.Y - Radius,
		Position.Y + Size.Y,
	};
	constexpr float UvStops[] = {0.0f, 0.25f, 0.75f, 1.0f};
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Column = 0; Column < 3; ++Column)
		{
			const FVector2D TileSize(
				XStops[Column + 1] - XStops[Column],
				YStops[Row + 1] - YStops[Row]);
			if (TileSize.X <= 0.01f || TileSize.Y <= 0.01f)
			{
				continue;
			}
			FCanvasTileItem Tile(
				FVector2D(XStops[Column], YStops[Row]),
				HudRoundedMaskTexture->GetResource(),
				TileSize,
				FVector2D(UvStops[Column], UvStops[Row]),
				FVector2D(UvStops[Column + 1], UvStops[Row + 1]),
				Color);
			Tile.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Tile);
		}
	}
}

void AIGHorrorHUD::DrawSensoryInterludeSkip()
{
	if (!Canvas || !bSensoryInterludeSkipAvailable)
	{
		SensoryInterludeSkipShownAt = -1.0;
		return;
	}
	// 다시 보는 장면에서 건너뛸 수 있다는 것만 알리면 된다. 2분 40초 내내 모서리에
	// 떠 있으면 장면을 가린다. 처음 4초만 보이고, 누르는 동안에는 다시 선다.
	const double Now = FPlatformTime::Seconds();
	if (SensoryInterludeSkipShownAt < 0.0)
	{
		SensoryInterludeSkipShownAt = Now;
	}
	const bool bPressing = bSensoryInterludeSkipInProgress || SensoryInterludeSkipProgress > 0.001f;
	const float ChipAlpha = bPressing
		? 1.0f
		: 1.0f - FMath::SmoothStep(4.0f, 4.6f, static_cast<float>(Now - SensoryInterludeSkipShownAt));
	if (ChipAlpha <= 0.01f)
	{
		return;
	}

	const float Scale = FMath::Clamp(Canvas->ClipY / 1080.0f, 0.68f, 1.35f);
	const float SafeInset = FMath::Max(24.0f * Scale, Canvas->ClipX * 0.035f);
	const float PanelWidth = FMath::Min(338.0f * Scale, Canvas->ClipX - SafeInset * 2.0f);
	const float PanelHeight = 68.0f * Scale;
	const FVector2D PanelPosition(
		Canvas->ClipX - SafeInset - PanelWidth,
		SafeInset);
	DrawRoundedHudSurface(
		PanelPosition,
		FVector2D(PanelWidth, PanelHeight),
		12.0f * Scale,
		FLinearColor(0.012f, 0.016f, 0.015f, 0.78f * ChipAlpha));

	// 건너뛰기는 기록 보기 키다. 키를 바꾼 사람에게 TAB을 누르라고 하지 않는다.
	const FText SkipKey = GetBoundKeyLabel(EIGBindableAction::Journal, bUsingGamepad);
	const FText Label = bSensoryInterludeSkipToggleMode
		? FText::Format(
			bSensoryInterludeSkipInProgress
				? NSLOCTEXT("IGHUD", "ReplaySkipToggleCancel", "{0}  다시 눌러 취소")
				: NSLOCTEXT("IGHUD", "ReplaySkipToggleStart", "{0}  눌러 건너뛰기"),
			SkipKey)
		: FText::Format(
			NSLOCTEXT("IGHUD", "ReplaySkipHold", "{0}  {1}초 눌러 건너뛰기"),
			SkipKey,
			FText::AsNumber(SensoryInterludeSkipHoldSeconds));
	const float TextScale = GetFittedTextScale(
		Label,
		EIGHudTextRole::Hint,
		0.82f * Scale,
		PanelWidth - 34.0f * Scale,
		0.58f * Scale);
	DrawLeftAlignedText(
		Label,
		PanelPosition + FVector2D(17.0f * Scale, 13.0f * Scale),
		FLinearColor(0.78f, 0.79f, 0.75f, 0.94f * ChipAlpha),
		EIGHudTextRole::Hint,
		TextScale);

	const FVector2D TrackPosition =
		PanelPosition + FVector2D(17.0f * Scale, PanelHeight - 15.0f * Scale);
	const FVector2D TrackSize(PanelWidth - 34.0f * Scale, 3.0f * Scale);
	DrawRoundedHudSurface(
		TrackPosition,
		TrackSize,
		1.5f * Scale,
		FLinearColor(0.25f, 0.27f, 0.25f, 0.72f * ChipAlpha));
	if (SensoryInterludeSkipProgress > 0.001f)
	{
		DrawRoundedHudSurface(
			TrackPosition,
			FVector2D(TrackSize.X * SensoryInterludeSkipProgress, TrackSize.Y),
			1.5f * Scale,
			FLinearColor(0.63f, 0.21f, 0.18f, 0.96f));
	}
}

void AIGHorrorHUD::BeginMissingFloorEpilogueScene(
	const EIGMissingFloorEpilogueScene Scene,
	const FText& Heading,
	const TArray<FText>& BodyLines,
	const FText& Footnote)
{
	// 장면이 바뀌면 건너뛰기 안내를 다시 보여 준다.
	SensoryInterludeSkipShownAt = -1.0;
	MissingFloorEpilogueScene = Scene;
	MissingFloorEpilogueHeading = Heading;
	MissingFloorEpilogueBodyLines = BodyLines;
	MissingFloorEpilogueFootnote = Footnote;
	MissingFloorEpilogueSceneStartedAt = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: 0.0;
}

void AIGHorrorHUD::EndMissingFloorEpilogue()
{
	MissingFloorEpilogueScene = EIGMissingFloorEpilogueScene::None;
	MissingFloorEpilogueHeading = FText::GetEmpty();
	MissingFloorEpilogueBodyLines.Reset();
	MissingFloorEpilogueFootnote = FText::GetEmpty();
	MissingFloorEpilogueSceneStartedAt = 0.0;
}

bool AIGHorrorHUD::DrawMissingFloorEpilogue(const double CurrentTime)
{
	if (!Canvas
		|| MissingFloorEpilogueScene == EIGMissingFloorEpilogueScene::None)
	{
		return false;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	const float UserTextScale = Accessibility
		? Accessibility->GetCaptionSizeScale()
		: 1.0f;
	const bool bReducedMotion = Accessibility
		&& Accessibility->IsReducedCameraMotionEnabled();
	const float Scale = FMath::Clamp(
		FMath::Min(Canvas->ClipY / 1080.0f, Canvas->ClipX / 1920.0f),
		0.62f,
		1.35f);
	const float TypeScale = Scale * FMath::Clamp(UserTextScale, 0.85f, 2.0f);
	const float Elapsed = FMath::Max(
		static_cast<float>(CurrentTime - MissingFloorEpilogueSceneStartedAt),
		0.0f);
	// 장면이 바뀌는 속도가 감정의 속도다. 모션 감소에서는 이동만 없애고
	// 페이드는 남긴다 — 컷으로 갈리면 몽타주가 점멸로 읽힌다.
	const float Entrance = IGHorrorHUD::SmoothStep01(Elapsed / 1.15f);

	FCanvasTileItem Blackout(
		FVector2D::ZeroVector,
		FVector2D(Canvas->ClipX, Canvas->ClipY),
		FLinearColor(0.004f, 0.005f, 0.005f, 1.0f));
	Canvas->DrawItem(Blackout);

	if (MissingFloorEpilogueScene == EIGMissingFloorEpilogueScene::Montage)
	{
		// 소리만 지나가는 구간이다. 자막을 켠 플레이어에게만 글자가 있다.
		DrawSensoryInterludeSkip();
		return true;
	}

	if (MissingFloorEpilogueScene == EIGMissingFloorEpilogueScene::Card)
	{
		// 마지막 한 줄. 같은 입력으로 바로 마칠 수 있으니 건너뛰기 안내만 남긴다.
		const FText Card = MissingFloorEpilogueBodyLines.Num() > 0
			? MissingFloorEpilogueBodyLines[0]
			: FText::GetEmpty();
		DrawSensoryInterludeSkip();
		if (!Card.IsEmpty())
		{
			DrawCenteredText(
				Card,
				Canvas->ClipY * 0.5f - 22.0f * TypeScale,
				FLinearColor(0.88f, 0.87f, 0.83f, Entrance),
				EIGHudTextRole::Prompt,
				1.06f * TypeScale);
			RecordLayoutValidationRect(
				FVector2D(Canvas->ClipX * 0.10f, Canvas->ClipY * 0.5f - 34.0f * TypeScale),
				FVector2D(Canvas->ClipX * 0.90f, Canvas->ClipY * 0.5f + 34.0f * TypeScale));
		}
		return true;
	}

	const float SafeInset = FMath::Max(30.0f * Scale, Canvas->ClipX * 0.072f);
	const float ContentLeft = SafeInset;
	const float ContentWidth = Canvas->ClipX - SafeInset * 2.0f;

	// 정지 화면은 있으면 얹고 없으면 넘어간다. 에필로그의 뜻은 문장에
	// 있으므로 텍스처가 아직 임포트되지 않은 빌드에서도 장면이 성립한다.
	UTexture2D* SceneTexture = nullptr;
	switch (MissingFloorEpilogueScene)
	{
	case EIGMissingFloorEpilogueScene::Workshop:
		SceneTexture = EpilogueWorkshopTexture;
		break;
	case EIGMissingFloorEpilogueScene::Autumn:
		SceneTexture = EpilogueAutumnTexture;
		break;
	case EIGMissingFloorEpilogueScene::ServiceBay:
		SceneTexture = EpilogueServiceBayTexture;
		break;
	default:
		break;
	}

	float PenY = Canvas->ClipY * 0.16f;
	if (SceneTexture && SceneTexture->GetResource())
	{
		// 판을 원본 비례로 맞춘다. 고정 높이를 쓰면 세로 사진이 가로로
		// 눌려 건물이 납작해지는데, 그건 이 화면에서 가장 눈에 띄는 거짓말이다.
		const float SourceWidth =
			FMath::Max(static_cast<float>(SceneTexture->GetSizeX()), 1.0f);
		const float SourceHeight =
			FMath::Max(static_cast<float>(SceneTexture->GetSizeY()), 1.0f);
		const float SourceAspect = SourceWidth / SourceHeight;
		const float MaximumHeight = FMath::Clamp(
			Canvas->ClipY * 0.46f,
			190.0f * Scale,
			560.0f * Scale);
		float PlateWidth = ContentWidth;
		float PlateHeight = PlateWidth / SourceAspect;
		if (PlateHeight > MaximumHeight)
		{
			PlateHeight = MaximumHeight;
			PlateWidth = PlateHeight * SourceAspect;
		}
		const float PlateLeft = ContentLeft + (ContentWidth - PlateWidth) * 0.5f;
		// 아주 느린 밀기. 사진이 아니라 기억이라는 신호이고, 모션 감소에서는
		// 그 이동만 0이 된다.
		const float Drift = bReducedMotion
			? 0.0f
			: (1.0f - Entrance) * 16.0f * Scale;
		FCanvasTileItem Plate(
			FVector2D(PlateLeft, PenY - Drift),
			SceneTexture->GetResource(),
			FVector2D(PlateWidth, PlateHeight),
			FVector2D(0.0f, 0.0f),
			FVector2D(1.0f, 1.0f),
			FLinearColor(0.82f, 0.83f, 0.80f, Entrance));
		Plate.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Plate);
		RecordLayoutValidationRect(
			FVector2D(PlateLeft, PenY - Drift),
			FVector2D(PlateLeft + PlateWidth, PenY - Drift + PlateHeight));
		PenY += PlateHeight + 40.0f * Scale;
	}
	else
	{
		PenY = Canvas->ClipY * 0.30f;
	}

	if (!MissingFloorEpilogueHeading.IsEmpty())
	{
		DrawLeftAlignedText(
			MissingFloorEpilogueHeading,
			FVector2D(ContentLeft, PenY),
			FLinearColor(0.56f, 0.55f, 0.51f, 0.92f * Entrance),
			EIGHudTextRole::Speaker,
			GetFittedTextScale(
				MissingFloorEpilogueHeading,
				EIGHudTextRole::Speaker,
				0.76f * TypeScale,
				ContentWidth,
				0.76f * TypeScale * 0.7f));
		PenY += 40.0f * FMath::Max(Scale, TypeScale * 0.72f);
	}

	// 글자를 키우거나 번역이 길면 기사 한 줄이 화면 폭을 넘는다. 폭에서 줄을
	// 바꾸고, 같은 문장 안의 줄은 조금 좁게 붙인다.
	const float LineStride = 42.0f * FMath::Max(Scale, TypeScale * 0.74f);
	UFont* EpilogueBodyFont = GetFontForRole(EIGHudTextRole::Dialogue);
	for (const FText& Line : MissingFloorEpilogueBodyLines)
	{
		if (Line.IsEmpty())
		{
			PenY += LineStride * 0.55f;
			continue;
		}
		TArray<FString> Wrapped;
		FString Remainder;
		WrapHudText(
			Line.ToString(),
			EpilogueBodyFont,
			0.90f * TypeScale,
			ContentWidth,
			3,
			Wrapped,
			Remainder);
		if (Wrapped.IsEmpty())
		{
			Wrapped.Add(Line.ToString());
		}
		for (int32 Piece = 0; Piece < Wrapped.Num(); ++Piece)
		{
			DrawLeftAlignedText(
				FText::FromString(Wrapped[Piece]),
				FVector2D(ContentLeft, PenY),
				FLinearColor(0.88f, 0.87f, 0.83f, Entrance),
				EIGHudTextRole::Dialogue,
				0.90f * TypeScale);
			PenY += Piece + 1 < Wrapped.Num() ? LineStride * 0.78f : LineStride;
		}
	}

	if (!MissingFloorEpilogueFootnote.IsEmpty())
	{
		PenY += 12.0f * Scale;
		DrawLeftAlignedText(
			MissingFloorEpilogueFootnote,
			FVector2D(ContentLeft, PenY),
			FLinearColor(0.62f, 0.61f, 0.57f, 0.90f * Entrance),
			EIGHudTextRole::Hint,
			GetFittedTextScale(
				MissingFloorEpilogueFootnote,
				EIGHudTextRole::Hint,
				0.74f * TypeScale,
				ContentWidth,
				0.74f * TypeScale * 0.7f));
		PenY += 34.0f * FMath::Max(Scale, TypeScale * 0.70f);
	}

	RecordLayoutValidationRect(
		FVector2D(ContentLeft, Canvas->ClipY * 0.16f),
		FVector2D(ContentLeft + ContentWidth, FMath::Min(PenY, Canvas->ClipY)));
	DrawSensoryInterludeSkip();
	return true;
}

bool AIGHorrorHUD::DrawMissingFloorFailureEnding(const double CurrentTime)
{
	if (!Canvas || !bMissingFloorFailureEndingVisible)
	{
		return false;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	const float UserTextScale = Accessibility
		? Accessibility->GetCaptionSizeScale()
		: 1.0f;
	const bool bReducedMotion = Accessibility
		&& Accessibility->IsReducedCameraMotionEnabled();
	const float Scale = FMath::Clamp(
		FMath::Min(Canvas->ClipY / 1080.0f, Canvas->ClipX / 1920.0f),
		0.62f,
		1.35f);
	const float TypeScale = Scale * FMath::Clamp(UserTextScale, 0.85f, 2.0f);
	const float Elapsed = FMath::Max(
		static_cast<float>(CurrentTime - MissingFloorFailureEndingStartedAt),
		0.0f);
	const float EntranceAlpha = bReducedMotion
		? 1.0f
		: IGHorrorHUD::SmoothStep01(Elapsed / 0.42f);
	const float ScrollAlpha = bReducedMotion
		? (Elapsed >= 3.1f ? 1.0f : 0.0f)
		: IGHorrorHUD::SmoothStep01((Elapsed - 2.55f) / 0.72f);

	FCanvasTileItem Blackout(
		FVector2D::ZeroVector,
		FVector2D(Canvas->ClipX, Canvas->ClipY),
		FLinearColor(0.002f, 0.003f, 0.003f, 1.0f));
	Canvas->DrawItem(Blackout);

	const float SafeInset = FMath::Max(18.0f * Scale, Canvas->ClipY * 0.035f);
	const FVector2D PanelSize(
		FMath::Min(760.0f * Scale, Canvas->ClipX - SafeInset * 2.0f),
		FMath::Min(900.0f * Scale, Canvas->ClipY - SafeInset * 2.0f));
	const FVector2D PanelPosition(
		(Canvas->ClipX - PanelSize.X) * 0.5f,
		(Canvas->ClipY - PanelSize.Y) * 0.5f);
	PushTextAuditContainer(PanelPosition, PanelPosition + PanelSize);
	DrawRoundedHudSurface(
		PanelPosition + FVector2D(0.0f, 9.0f * Scale),
		PanelSize,
		24.0f * Scale,
		FLinearColor(0.0f, 0.0f, 0.0f, 0.46f * EntranceAlpha));
	DrawRoundedHudSurface(
		PanelPosition,
		PanelSize,
		22.0f * Scale,
		FLinearColor(0.91f, 0.90f, 0.86f, EntranceAlpha));

	const float Padding = 34.0f * Scale;
	const float ContentLeft = PanelPosition.X + Padding;
	const float ContentWidth = PanelSize.X - Padding * 2.0f;
	// 번역이 길어도 판 밖으로 나가지 않게 줄마다 폭에 맞춰 줄인다.
	auto FitScale = [this](
		const FText& Text,
		const EIGHudTextRole TextRole,
		const float PreferredScale,
		const float MaximumWidth)
	{
		return GetFittedTextScale(
			Text, TextRole, PreferredScale, MaximumWidth, PreferredScale * 0.6f);
	};
	const FText AppSection = NSLOCTEXT("IGHUD", "EndingCAppSection", "주거  ·  빌라");
	const float AppSectionScale =
		FitScale(AppSection, EIGHudTextRole::Speaker, 0.82f * TypeScale, ContentWidth);
	DrawLeftAlignedText(
		AppSection,
		FVector2D(ContentLeft, PanelPosition.Y + 22.0f * Scale),
		FLinearColor(0.20f, 0.22f, 0.20f, 0.88f * EntranceAlpha),
		EIGHudTextRole::Speaker,
		AppSectionScale);
	// 글자를 키우면 머리줄도 같이 내려간다. 구분선이 글자를 긋지 않게 한다.
	const float TopBarHeight = FMath::Max(
		68.0f * Scale,
		22.0f * Scale
			+ MeasureTextHeight(
				AppSection.ToString(),
				GetFontForRole(EIGHudTextRole::Speaker),
				AppSectionScale)
			+ 10.0f * Scale);
	DrawRoundedHudSurface(
		FVector2D(ContentLeft, PanelPosition.Y + TopBarHeight - 2.0f * Scale),
		FVector2D(ContentWidth, 1.0f * Scale),
		0.5f * Scale,
		FLinearColor(0.36f, 0.37f, 0.34f, 0.24f * EntranceAlpha));

	const float BodyTop = PanelPosition.Y + TopBarHeight + 18.0f * Scale;
	const float HeroHeight = FMath::Clamp(
		PanelSize.Y * 0.27f,
		150.0f * Scale,
		238.0f * Scale);
	const float HeroAlpha = EntranceAlpha * (1.0f - ScrollAlpha);
	if (HeroAlpha > 0.01f && FrontendTitleBackgroundTexture
		&& FrontendTitleBackgroundTexture->GetResource())
	{
		const float HeroTravel = bReducedMotion ? 0.0f : 24.0f * Scale * ScrollAlpha;
		FCanvasTileItem Hero(
			FVector2D(ContentLeft, BodyTop - HeroTravel),
			FrontendTitleBackgroundTexture->GetResource(),
			FVector2D(ContentWidth, HeroHeight),
			FVector2D(0.20f, 0.26f),
			FVector2D(1.00f, 0.74f),
			FLinearColor(0.92f, 0.94f, 0.91f, HeroAlpha));
		Hero.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Hero);
		RecordLayoutValidationRect(
			FVector2D(ContentLeft, BodyTop - HeroTravel),
			FVector2D(ContentLeft + ContentWidth, BodyTop - HeroTravel + HeroHeight));
		// 칩은 글자 폭에 맞춰 넓힌다. 「Unit 403」은 「403호」보다 길다.
		const FText UnitChip = NSLOCTEXT("IGHUD", "EndingCUnitChip", "403호");
		const float ChipTextScale = 0.66f * Scale;
		const float ChipTextWidth = MeasureTextWidth(
			UnitChip.ToString(),
			GetFontForRole(EIGHudTextRole::Hint),
			ChipTextScale);
		const float ChipWidth = FMath::Max(72.0f * Scale, ChipTextWidth + 30.0f * Scale);
		DrawRoundedHudSurface(
			FVector2D(ContentLeft + 14.0f * Scale, BodyTop + 14.0f * Scale - HeroTravel),
			FVector2D(ChipWidth, 31.0f * Scale),
			15.5f * Scale,
			FLinearColor(0.05f, 0.06f, 0.055f, 0.80f * HeroAlpha));
		DrawLeftAlignedText(
			UnitChip,
			FVector2D(
				ContentLeft + 14.0f * Scale + (ChipWidth - ChipTextWidth) * 0.5f,
				BodyTop + 20.0f * Scale - HeroTravel),
			FLinearColor(0.91f, 0.90f, 0.84f, HeroAlpha),
			EIGHudTextRole::Hint,
			ChipTextScale);
	}

	const float ListingAlpha = EntranceAlpha * (1.0f - ScrollAlpha);
	if (ListingAlpha > 0.01f)
	{
		float PenY = BodyTop + HeroHeight + 28.0f * Scale;
		const FText ListingTitle =
			NSLOCTEXT("IGHUD", "EndingCListingTitle", "무영로 달빛빌라 403호");
		DrawLeftAlignedText(
			ListingTitle,
			FVector2D(ContentLeft, PenY),
			FLinearColor(0.095f, 0.105f, 0.095f, ListingAlpha),
			EIGHudTextRole::Prompt,
			FitScale(ListingTitle, EIGHudTextRole::Prompt, 0.98f * TypeScale, ContentWidth));
		PenY += 45.0f * FMath::Max(Scale, TypeScale * 0.72f);
		const FText ListingCopy =
			NSLOCTEXT("IGHUD", "EndingCListingCopy", "채광 좋은 남향, 즉시 입주 가능");
		DrawLeftAlignedText(
			ListingCopy,
			FVector2D(ContentLeft, PenY),
			FLinearColor(0.25f, 0.27f, 0.24f, 0.88f * ListingAlpha),
			EIGHudTextRole::Dialogue,
			FitScale(ListingCopy, EIGHudTextRole::Dialogue, 0.83f * TypeScale, ContentWidth));
		PenY += 42.0f * FMath::Max(Scale, TypeScale * 0.72f);
		const FText ListingMeta =
			NSLOCTEXT("IGHUD", "EndingCListingMeta", "빌라  ·  4층  ·  남향");
		DrawLeftAlignedText(
			ListingMeta,
			FVector2D(ContentLeft, PenY),
			FLinearColor(0.38f, 0.40f, 0.37f, 0.78f * ListingAlpha),
			EIGHudTextRole::Hint,
			FitScale(ListingMeta, EIGHudTextRole::Hint, 0.70f * TypeScale, ContentWidth));
	}

	const float CommentAlpha = EntranceAlpha * ScrollAlpha;
	if (CommentAlpha > 0.01f)
	{
		const float MotionOffset = bReducedMotion
			? 0.0f
			: (1.0f - ScrollAlpha) * 28.0f * Scale;
		float PenY = BodyTop + MotionOffset;
		const FText ScrolledTitle =
			NSLOCTEXT("IGHUD", "EndingCScrolledTitle", "무영로 달빛빌라 403호");
		DrawLeftAlignedText(
			ScrolledTitle,
			FVector2D(ContentLeft, PenY),
			FLinearColor(0.095f, 0.105f, 0.095f, CommentAlpha),
			EIGHudTextRole::Prompt,
			FitScale(ScrolledTitle, EIGHudTextRole::Prompt, 0.90f * TypeScale, ContentWidth));
		PenY += 48.0f * FMath::Max(Scale, TypeScale * 0.72f);
		const FText CommentHeading =
			NSLOCTEXT("IGHUD", "EndingCCommentHeading", "입주자 후기");
		DrawLeftAlignedText(
			CommentHeading,
			FVector2D(ContentLeft, PenY),
			FLinearColor(0.31f, 0.33f, 0.30f, 0.90f * CommentAlpha),
			EIGHudTextRole::Speaker,
			FitScale(CommentHeading, EIGHudTextRole::Speaker, 0.72f * TypeScale, ContentWidth));
		PenY += 39.0f * FMath::Max(Scale, TypeScale * 0.72f);

		const float CommentHeight = FMath::Min(
			244.0f * Scale + 34.0f * FMath::Max(UserTextScale - 1.0f, 0.0f),
			PanelPosition.Y + PanelSize.Y - PenY - 118.0f * Scale);
		DrawRoundedHudSurface(
			FVector2D(ContentLeft, PenY),
			FVector2D(ContentWidth, CommentHeight),
			16.0f * Scale,
			FLinearColor(0.84f, 0.83f, 0.78f, 0.78f * CommentAlpha));
		RecordLayoutValidationRect(
			FVector2D(ContentLeft, PenY),
			FVector2D(ContentLeft + ContentWidth, PenY + CommentHeight));
		const float CommentInset = 24.0f * Scale;
		const float CommentTextWidth = ContentWidth - CommentInset * 2.0f;
		const FText CommentOne =
			NSLOCTEXT("IGHUD", "EndingCCommentLineOne", "이 집 새벽마다 뭐 두드리는 소리 나요.");
		const FText CommentTwo =
			NSLOCTEXT("IGHUD", "EndingCCommentLineTwo", "두 명이서 하는 것 같아요.");
		// 한 사람이 쓴 두 줄이라 글자 크기를 같이 맞춘다.
		const float CommentScale = FMath::Min(
			FitScale(CommentOne, EIGHudTextRole::Dialogue, 0.84f * TypeScale, CommentTextWidth),
			FitScale(CommentTwo, EIGHudTextRole::Dialogue, 0.84f * TypeScale, CommentTextWidth));
		DrawLeftAlignedText(
			CommentOne,
			FVector2D(ContentLeft + CommentInset, PenY + 27.0f * Scale),
			FLinearColor(0.11f, 0.12f, 0.105f, CommentAlpha),
			EIGHudTextRole::Dialogue,
			CommentScale);
		DrawLeftAlignedText(
			CommentTwo,
			FVector2D(
				ContentLeft + CommentInset,
				PenY + 27.0f * Scale + 43.0f * FMath::Max(Scale, TypeScale * 0.72f)),
			FLinearColor(0.11f, 0.12f, 0.105f, CommentAlpha),
			EIGHudTextRole::Dialogue,
			CommentScale);
		const FText CommentMeta =
			NSLOCTEXT("IGHUD", "EndingCCommentMeta", "방금 전  ·  조회 17");
		DrawLeftAlignedText(
			CommentMeta,
			FVector2D(
				ContentLeft + CommentInset,
				PenY + CommentHeight - 37.0f * Scale),
			FLinearColor(0.39f, 0.40f, 0.37f, 0.80f * CommentAlpha),
			EIGHudTextRole::Hint,
			FitScale(CommentMeta, EIGHudTextRole::Hint, 0.66f * TypeScale, CommentTextWidth));
	}

	if (bMissingFloorFailureRetryEnabled)
	{
		const float FooterHeight = 84.0f * Scale;
		const float FooterY = PanelPosition.Y + PanelSize.Y - FooterHeight;
		DrawRoundedHudSurface(
			FVector2D(PanelPosition.X + 2.0f * Scale, FooterY),
			FVector2D(PanelSize.X - 4.0f * Scale, FooterHeight - 2.0f * Scale),
			20.0f * Scale,
			FLinearColor(0.075f, 0.086f, 0.080f, 0.98f));
		RecordLayoutValidationRect(
			FVector2D(PanelPosition.X + 2.0f * Scale, FooterY),
			FVector2D(
				PanelPosition.X + PanelSize.X - 2.0f * Scale,
				FooterY + FooterHeight - 2.0f * Scale));
		// 조사 키를 바꾼 사람에게 「E」를 누르라고 하면 안 된다. 지금 묶인 키를 쓴다.
		const FText RetryText = FText::Format(
			NSLOCTEXT("IGHUD", "EndingCRetryFormat", "{0}  넷째 밤 다시 시작"),
			GetBoundKeyLabel(EIGBindableAction::Interact, bUsingGamepad));
		const float RetryScale = GetFittedTextScale(
			RetryText,
			EIGHudTextRole::Prompt,
			0.86f * TypeScale,
			ContentWidth,
			0.58f * Scale);
		const float RetryWidth = MeasureTextWidth(
			RetryText.ToString(),
			GetFontForRole(EIGHudTextRole::Prompt),
			RetryScale);
		DrawLeftAlignedText(
			RetryText,
			FVector2D(
				PanelPosition.X + (PanelSize.X - RetryWidth) * 0.5f,
				FooterY + 24.0f * Scale),
			FLinearColor(0.90f, 0.89f, 0.84f, 1.0f),
			EIGHudTextRole::Prompt,
			RetryScale);
	}

	PopTextAuditContainer();
	RecordLayoutValidationRect(PanelPosition, PanelPosition + PanelSize);
	return true;
}

void AIGHorrorHUD::DrawDialogueFilm(
	const FVector2D& Position,
	const FVector2D& Size,
	const float CornerRadius,
	const float Alpha) const
{
	if (!Canvas || !DialogueFilmTexture || !DialogueFilmTexture->GetResource()
		|| Size.X <= 0.0f || Size.Y <= 0.0f || Alpha <= 0.001f)
	{
		return;
	}

	const float Radius = FMath::Clamp(
		CornerRadius,
		0.0f,
		FMath::Min(Size.X, Size.Y) * 0.5f);
	const auto DrawFilmRegion = [this, Position, Size, Alpha](
		const FVector2D& RegionPosition,
		const FVector2D& RegionSize)
	{
		if (RegionSize.X <= 0.01f || RegionSize.Y <= 0.01f)
		{
			return;
		}
		const FVector2D Uv0(
			(RegionPosition.X - Position.X) / Size.X,
			(RegionPosition.Y - Position.Y) / Size.Y);
		const FVector2D Uv1(
			(RegionPosition.X + RegionSize.X - Position.X) / Size.X,
			(RegionPosition.Y + RegionSize.Y - Position.Y) / Size.Y);
		FCanvasTileItem Grain(
			RegionPosition,
			DialogueFilmTexture->GetResource(),
			RegionSize,
			Uv0,
			Uv1,
			FLinearColor(0.78f, 0.86f, 0.82f, Alpha));
		Grain.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Grain);
	};

	// Three rectangles are the inexpensive equivalent of a rounded clip: the
	// curved corner squares remain owned by the 9-slice base surface.
	DrawFilmRegion(
		FVector2D(Position.X + Radius, Position.Y),
		FVector2D(FMath::Max(0.0f, Size.X - Radius * 2.0f), Size.Y));
	DrawFilmRegion(
		FVector2D(Position.X, Position.Y + Radius),
		FVector2D(Radius, FMath::Max(0.0f, Size.Y - Radius * 2.0f)));
	DrawFilmRegion(
		FVector2D(Position.X + Size.X - Radius, Position.Y + Radius),
		FVector2D(Radius, FMath::Max(0.0f, Size.Y - Radius * 2.0f)));
}

bool AIGHorrorHUD::DrawDialoguePanel(
	const double CurrentTime,
	float& OutPanelTop)
{
	OutPanelTop = Canvas ? Canvas->ClipY : 0.0f;
	if (!Canvas)
	{
		return false;
	}
	AdvanceDialogueQueue(CurrentTime);
	if (!bHasCurrentDialogue)
	{
		return false;
	}

	const UGameInstance* GameInstance = GetWorld()
		? GetWorld()->GetGameInstance()
		: nullptr;
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	while (bHasCurrentDialogue
		&& CurrentDialogue.Channel == EIGDialogueChannel::VoiceSubtitle
		&& (!Accessibility || !Accessibility->AreSubtitlesEnabled()))
	{
		DialogueEndTime = CurrentTime;
		AdvanceDialogueQueue(CurrentTime);
	}
	if (!bHasCurrentDialogue)
	{
		return false;
	}

	const FIGAccessibilitySettings Settings = Accessibility
		? Accessibility->GetSettings()
		: FIGAccessibilitySettings();
	const float ResolutionScale = FMath::Clamp(
		Canvas->ClipY / 1080.0f,
		0.85f,
		2.0f);
	const float TextScale = GetResolutionTextScale(Settings.CaptionSizeScale);
	const float SafeAreaScale = Settings.CaptionSafeAreaScale;
	const float SafeWidth = Canvas->ClipX * SafeAreaScale;
	const float PanelWidth = FMath::Min(
		FMath::Clamp(
			Canvas->ClipX * 0.56f,
			520.0f * ResolutionScale,
			840.0f * ResolutionScale),
		FMath::Max(280.0f, SafeWidth - 48.0f * ResolutionScale));
	const float HorizontalPadding = 30.0f * ResolutionScale;
	const float MaximumTextWidth = FMath::Max(
		220.0f,
		PanelWidth - HorizontalPadding * 2.0f);
	const int32 MaximumLines = Settings.CaptionSizeScale > 1.25f ? 3 : 2;
	PrepareDialoguePage(
		TextScale,
		MaximumTextWidth,
		MaximumLines,
		CurrentTime);
	if (CurrentDialogueLines.IsEmpty())
	{
		return false;
	}

	UFont* BodyFont = GetFontForRole(EIGHudTextRole::Dialogue);
	UFont* SpeakerFont = GetFontForRole(EIGHudTextRole::Speaker);
	float BodyRawWidth = 0.0f;
	float BodyRawHeight = 19.0f;
	if (BodyFont)
	{
		Canvas->StrLen(BodyFont, *GetLineHeightSample(), BodyRawWidth, BodyRawHeight, true);
	}
	float SpeakerRawWidth = 0.0f;
	float SpeakerRawHeight = 14.0f;
	if (SpeakerFont)
	{
		Canvas->StrLen(
			SpeakerFont,
			*GetLineHeightSample(),
			SpeakerRawWidth,
			SpeakerRawHeight,
			true);
	}
	const float BodyHeight = FMath::Max(16.0f, BodyRawHeight * TextScale);
	const float LineStep = BodyHeight * (
		CurrentDialogueLines.Num() >= 3 ? 1.36f : 1.32f);
	const bool bHasSpeaker = !CurrentDialogue.Speaker.IsEmpty();
	const float SpeakerScale = TextScale * 0.78f;
	const float SpeakerHeight = bHasSpeaker
		? FMath::Max(11.0f, SpeakerRawHeight * SpeakerScale)
		: 0.0f;
	const float SpeakerChipHeight = bHasSpeaker
		? FMath::Max(24.0f * ResolutionScale, SpeakerHeight + 10.0f * ResolutionScale)
		: 0.0f;
	const float HeaderGap = bHasSpeaker ? 10.0f * ResolutionScale : 0.0f;
	const float TopPadding = 14.0f * ResolutionScale;
	// 「이어짐」은 마지막 줄 아래 오른쪽 구석에 선다. 자막을 키우면 이 표시도
	// 같이 커지는데 아래 여백은 26px로 고정이어서, 자막 크기 200%에서 표시가
	// 꽉 찬 마지막 줄의 끝 글자를 덮었다. 여백을 표시의 실제 높이로 잡는다.
	const FText ContinuationLabel = NSLOCTEXT(
		"IGHorrorHUD",
		"DialogueContinues",
		"이어짐");
	const float ContinuationScale = TextScale * 0.72f;
	float ContinuationRawWidth = 0.0f;
	float ContinuationRawHeight = 0.0f;
	if (bCurrentDialogueHasContinuation && SpeakerFont)
	{
		Canvas->StrLen(
			SpeakerFont,
			ContinuationLabel.ToString(),
			ContinuationRawWidth,
			ContinuationRawHeight,
			true);
	}
	const float ContinuationHeight = ContinuationRawHeight * ContinuationScale;
	const float BottomPadding = bCurrentDialogueHasContinuation
		? FMath::Max(
			26.0f * ResolutionScale,
			ContinuationHeight + 13.0f * ResolutionScale)
		: 19.0f * ResolutionScale;
	const float LinesHeight = BodyHeight
		+ LineStep * FMath::Max(0, CurrentDialogueLines.Num() - 1);
	const float PanelHeight = TopPadding + SpeakerChipHeight + HeaderGap
		+ LinesHeight + BottomPadding;
	const float SafeHorizontalInset = Canvas->ClipX * (1.0f - SafeAreaScale) * 0.5f;
	const float SafeVerticalInset = Canvas->ClipY * (1.0f - SafeAreaScale) * 0.5f;

	const double Elapsed = CurrentTime - DialogueStartTime;
	const double Remaining = DialogueEndTime - CurrentTime;
	const float FadeIn = IGHorrorHUD::SmoothStep01(
		static_cast<float>(Elapsed / 0.24));
	const float FadeOut = IGHorrorHUD::SmoothStep01(
		static_cast<float>(Remaining / 0.16));
	const float Alpha = FMath::Min(FadeIn, FadeOut);
	const bool bReducedMotion = Accessibility
		&& Accessibility->IsReducedCameraMotionEnabled();
	const float TravelY = bReducedMotion
		? 0.0f
		: (1.0f - FadeIn) * 10.0f * ResolutionScale;
	const float PanelX = (Canvas->ClipX - PanelWidth) * 0.5f;
	const float PanelY = FMath::Clamp(
		Canvas->ClipY - SafeVerticalInset - PanelHeight
			- 42.0f * ResolutionScale - TravelY,
		SafeVerticalInset + 24.0f * ResolutionScale,
		Canvas->ClipY - SafeVerticalInset - PanelHeight
			- 24.0f * ResolutionScale);
	OutPanelTop = PanelY;

	FLinearColor Accent = IGHorrorHUD::DialogueTeal;
	if (CurrentDialogue.Channel == EIGDialogueChannel::InnerVoice)
	{
		Accent = IGHorrorHUD::ThoughtBlue;
	}
	else if (CurrentDialogue.Channel == EIGDialogueChannel::Device)
	{
		Accent = FLinearColor(0.50f, 0.70f, 0.62f, 1.0f);
	}
	else if (CurrentDialogue.Priority == EIGDialoguePriority::Critical)
	{
		Accent = IGHorrorHUD::RedAccent;
	}
	Accent.A = Alpha;
	const float SurfaceAlpha = Settings.CaptionBackgroundOpacity * Alpha;
	const float CornerRadius = 10.0f * ResolutionScale;
	if (SurfaceAlpha > 0.001f)
	{
		DrawRoundedHudSurface(
			FVector2D(PanelX, PanelY + 8.0f * ResolutionScale),
			FVector2D(PanelWidth, PanelHeight),
			CornerRadius + 2.0f * ResolutionScale,
			FLinearColor(0.0f, 0.0f, 0.0f, SurfaceAlpha * 0.23f));
		DrawRoundedHudSurface(
			FVector2D(PanelX, PanelY + 3.0f * ResolutionScale),
			FVector2D(PanelWidth, PanelHeight),
			CornerRadius,
			FLinearColor(0.0f, 0.0f, 0.0f, SurfaceAlpha * 0.38f));
		FLinearColor BorderColor = Accent;
		BorderColor.A = SurfaceAlpha * 0.28f;
		DrawRoundedHudSurface(
			FVector2D(PanelX, PanelY),
			FVector2D(PanelWidth, PanelHeight),
			CornerRadius,
			BorderColor);
		const float BorderInset = FMath::Max(1.0f, ResolutionScale);
		DrawRoundedHudSurface(
			FVector2D(PanelX + BorderInset, PanelY + BorderInset),
			FVector2D(
				PanelWidth - BorderInset * 2.0f,
				PanelHeight - BorderInset * 2.0f),
			FMath::Max(2.0f, CornerRadius - BorderInset),
			FLinearColor(0.012f, 0.017f, 0.016f, SurfaceAlpha));
		DrawDialogueFilm(
			FVector2D(PanelX + BorderInset, PanelY + BorderInset),
			FVector2D(
				PanelWidth - BorderInset * 2.0f,
				PanelHeight - BorderInset * 2.0f),
			FMath::Max(2.0f, CornerRadius - BorderInset),
			SurfaceAlpha * 0.14f);
	}
	FLinearColor KeylineColor = Accent;
	KeylineColor.A = Alpha * 0.52f;
	FCanvasTileItem Keyline(
		FVector2D(PanelX + HorizontalPadding, PanelY),
		FVector2D(56.0f * ResolutionScale, FMath::Max(1.0f, ResolutionScale)),
		KeylineColor);
	Keyline.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Keyline);
	RecordLayoutValidationRect(
		FVector2D(PanelX, PanelY),
		FVector2D(PanelX + PanelWidth, PanelY + PanelHeight));

	float PenY = PanelY + TopPadding;
	const bool bUseTextOutline = Settings.CaptionBackgroundOpacity < 0.42f;
	if (bHasSpeaker)
	{
		const float SpeakerTextWidth = MeasureTextWidth(
			CurrentDialogue.Speaker.ToString(),
			SpeakerFont,
			SpeakerScale);
		const float SpeakerChipWidth = FMath::Min(
			PanelWidth - HorizontalPadding * 2.0f,
			SpeakerTextWidth + 20.0f * ResolutionScale);
		FLinearColor SpeakerChipColor = Accent;
		SpeakerChipColor.R *= 0.24f;
		SpeakerChipColor.G *= 0.24f;
		SpeakerChipColor.B *= 0.24f;
		SpeakerChipColor.A = Alpha
			* Settings.CaptionBackgroundOpacity
			* 0.34f;
		DrawRoundedHudSurface(
			FVector2D(PanelX + HorizontalPadding, PenY),
			FVector2D(SpeakerChipWidth, SpeakerChipHeight),
			SpeakerChipHeight * 0.5f,
			SpeakerChipColor);
		FLinearColor SpeakerColor = Accent;
		SpeakerColor.A = Alpha * 0.96f;
		DrawLeftAlignedText(
			CurrentDialogue.Speaker,
			FVector2D(
				PanelX + HorizontalPadding + 10.0f * ResolutionScale,
				PenY + (SpeakerChipHeight - SpeakerHeight) * 0.5f),
			SpeakerColor,
			EIGHudTextRole::Speaker,
			SpeakerScale,
			bUseTextOutline);
		PenY += SpeakerChipHeight + HeaderGap;
	}
	FLinearColor BodyColor = CurrentDialogue.Channel == EIGDialogueChannel::InnerVoice
		? IGHorrorHUD::ThoughtBlue
		: IGHorrorHUD::DialogueIvory;
	BodyColor.A = Alpha;
	for (int32 LineIndex = 0; LineIndex < CurrentDialogueLines.Num(); ++LineIndex)
	{
		DrawLeftAlignedText(
			FText::FromString(CurrentDialogueLines[LineIndex]),
			FVector2D(
				PanelX + HorizontalPadding,
				PenY + LineIndex * LineStep),
			BodyColor,
			EIGHudTextRole::Dialogue,
			TextScale,
			bUseTextOutline);
	}
	bool bContinuationClear = true;
	if (bCurrentDialogueHasContinuation)
	{
		FLinearColor ContinuationColor = Accent;
		ContinuationColor.A *= 0.72f;
		const float ContinuationY = PanelY + PanelHeight
			- ContinuationHeight
			- 7.0f * ResolutionScale;
		const float ContinuationLeft = PanelX + PanelWidth - HorizontalPadding
			- ContinuationRawWidth * ContinuationScale
			- 16.0f * ResolutionScale;
		// 표시와 마지막 줄이 실제로 떨어져 있는지 배포판 검사가 본다.
		const float LastLineBottom = PenY
			+ LineStep * (CurrentDialogueLines.Num() - 1)
			+ BodyHeight;
		const float LastLineRight = PanelX + HorizontalPadding
			+ MeasureTextWidth(CurrentDialogueLines.Last(), BodyFont, TextScale);
		bContinuationClear = ContinuationY >= LastLineBottom
			|| ContinuationLeft >= LastLineRight;
		FCanvasTileItem ContinuationRule(
			FVector2D(
				ContinuationLeft,
				ContinuationY + ContinuationHeight * 0.52f),
			FVector2D(9.0f * ResolutionScale, FMath::Max(1.0f, ResolutionScale)),
			ContinuationColor);
		ContinuationRule.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(ContinuationRule);
		DrawLeftAlignedText(
			ContinuationLabel,
			FVector2D(
				PanelX + PanelWidth - HorizontalPadding
					- ContinuationRawWidth * ContinuationScale,
				ContinuationY),
			ContinuationColor,
			EIGHudTextRole::Speaker,
			ContinuationScale,
			bUseTextOutline);
	}

	DialogueLastPanelMinimum = FVector2D(PanelX, PanelY);
	DialogueLastPanelMaximum = FVector2D(PanelX + PanelWidth, PanelY + PanelHeight);
	DialogueLastCanvasSize = FVector2D(Canvas->ClipX, Canvas->ClipY);
	DialogueLastLineCount = CurrentDialogueLines.Num();
	bDialogueLastSpeakerVisible = bHasSpeaker;
	bDialogueLastHasContinuation = bCurrentDialogueHasContinuation;
	bDialogueLastContinuationClear = bContinuationClear;
	bDialogueLastInsideSafeArea =
		PanelX >= SafeHorizontalInset - 1.0f
		&& PanelX + PanelWidth <= Canvas->ClipX - SafeHorizontalInset + 1.0f
		&& PanelY >= SafeVerticalInset - 1.0f
		&& PanelY + PanelHeight <= Canvas->ClipY - SafeVerticalInset + 1.0f;
	++DialogueLastRenderSerial;
	return true;
}

bool AIGHorrorHUD::DrawAudioCaption(
	const double /*GameTime*/,
	const float MaximumBottomY,
	float* OutPanelTop)
{
	if (OutPanelTop)
	{
		*OutPanelTop = Canvas ? Canvas->ClipY : 0.0f;
	}
	if (!Canvas)
	{
		return false;
	}
	// 부르는 쪽의 게임 시간은 타이틀과 밤 5에서 멈춰 있다. 자막은 ShowAudioCaption이
	// 찍은 것과 같은 자막 시계로 잰다.
	const double CurrentTime = AdvanceAudioCaptionClock();
	AdvanceAudioCaptionQueue(CurrentTime);
	if (CurrentAudioCaption.IsEmpty() || CurrentTime >= AudioCaptionEndTime)
	{
		return false;
	}
	const UGameInstance* GameInstance = GetWorld()
		? GetWorld()->GetGameInstance()
		: nullptr;
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	if (!Accessibility || !Accessibility->AreSoundCaptionsEnabled())
	{
		return false;
	}
	const double Elapsed = CurrentTime - AudioCaptionStartTime;
	const double Remaining = AudioCaptionEndTime - CurrentTime;
	const float Alpha = FMath::Clamp(
		FMath::Min(
			static_cast<float>(Elapsed / 0.12),
			static_cast<float>(Remaining / 0.28)),
		0.0f,
		1.0f);
	const FIGAccessibilitySettings Settings = Accessibility->GetSettings();
	const float ResolutionScale = FMath::Clamp(
		Canvas->ClipY / 1080.0f,
		0.85f,
		2.0f);
	const float CaptionScale = GetResolutionTextScale(Settings.CaptionSizeScale) * 0.88f;
	const float SafeAreaScale = Settings.CaptionSafeAreaScale;
	const float SafeWidth = Canvas->ClipX * SafeAreaScale;
	const float MaximumPanelWidth = FMath::Min(
		FMath::Clamp(
			Canvas->ClipX * 0.42f,
			280.0f * ResolutionScale,
			680.0f * ResolutionScale),
		FMath::Max(240.0f, SafeWidth - 48.0f * ResolutionScale));
	const float HorizontalPadding = 18.0f * ResolutionScale;
	const float IconLaneWidth = 28.0f * ResolutionScale;
	const float MaximumTextWidth = FMath::Max(
		180.0f,
		MaximumPanelWidth - HorizontalPadding * 2.0f - IconLaneWidth);
	const int32 MaximumCaptionLines = Settings.CaptionSizeScale > 1.25f ? 3 : 2;
	FString DisplayCaption = CurrentAudioCaption.ToString().TrimStartAndEnd();
	// Authored captions keep square brackets in data for transcripts and
	// fallback surfaces. This lane already has a waveform glyph, so repeating
	// the same semantic marker on screen adds noise without adding meaning.
	// 자막 전체가 괄호 한 쌍으로 싸여 있을 때만 벗긴다. 방향 표시([뒤])가 붙은
	// 자막까지 벗기면 「뒤] [철문…」처럼 괄호가 엇갈린다. 일본어와 중국어
	// 번역이 쓰는 전각 괄호도 같은 규칙이다.
	static const TCHAR* const OpenBrackets = TEXT("[［【");
	static const TCHAR* const CloseBrackets = TEXT("]］】");
	if (DisplayCaption.Len() >= 2)
	{
		const TCHAR* Open = FCString::Strchr(OpenBrackets, DisplayCaption[0]);
		const int32 Pair = Open ? static_cast<int32>(Open - OpenBrackets) : INDEX_NONE;
		const FString Inner = DisplayCaption.Mid(1, DisplayCaption.Len() - 2);
		bool bInnerClosed = false;
		for (const TCHAR Character : Inner)
		{
			bInnerClosed |= FCString::Strchr(CloseBrackets, Character) != nullptr;
		}
		if (Pair != INDEX_NONE
			&& DisplayCaption[DisplayCaption.Len() - 1] == CloseBrackets[Pair]
			&& !bInnerClosed)
		{
			DisplayCaption = Inner.TrimStartAndEnd();
		}
	}
	TArray<FString> Lines;
	FString Remainder;
	WrapHudText(
		DisplayCaption,
		GetFontForRole(EIGHudTextRole::Dialogue),
		CaptionScale,
		MaximumTextWidth,
		MaximumCaptionLines,
		Lines,
		Remainder);
	if (Lines.IsEmpty())
	{
		return false;
	}
	if (!Remainder.IsEmpty())
	{
		FIGAudioCaptionMessage Continuation;
		Continuation.Caption = FText::FromString(Remainder);
		Continuation.DurationSeconds = FMath::Max(
			1.2f,
			static_cast<float>(AudioCaptionEndTime - CurrentTime));
		Continuation.QueuedAt = CurrentTime;
		AudioCaptionQueue.Insert(MoveTemp(Continuation), 0);
		if (AudioCaptionQueue.Num() > IGHorrorHUD::MaximumAudioCaptionQueueDepth)
		{
			AudioCaptionQueue.RemoveAt(AudioCaptionQueue.Num() - 1);
		}
		CurrentAudioCaption = FText::FromString(FString::Join(Lines, TEXT("\n")));
	}
	UFont* CaptionFont = GetFontForRole(EIGHudTextRole::Dialogue);
	float RawWidth = 0.0f;
	float RawHeight = 19.0f;
	if (CaptionFont)
	{
		Canvas->StrLen(CaptionFont, *GetLineHeightSample(), RawWidth, RawHeight, true);
	}
	float LongestLineWidth = 0.0f;
	for (const FString& Line : Lines)
	{
		LongestLineWidth = FMath::Max(
			LongestLineWidth,
			MeasureTextWidth(Line, CaptionFont, CaptionScale));
	}
	const float PanelWidth = FMath::Min(
		MaximumPanelWidth,
		FMath::Max(
			280.0f * ResolutionScale,
			LongestLineWidth + HorizontalPadding * 2.0f + IconLaneWidth));
	const float BodyHeight = FMath::Max(16.0f, RawHeight * CaptionScale);
	const float LineStep = BodyHeight * (Lines.Num() >= 3 ? 1.34f : 1.29f);
	const float VerticalPadding = 11.0f * ResolutionScale;
	const float PanelHeight = VerticalPadding * 2.0f + BodyHeight
		+ LineStep * FMath::Max(0, Lines.Num() - 1);
	const float SafeVerticalInset = Canvas->ClipY * (1.0f - SafeAreaScale) * 0.5f;
	const float MinimumPanelY = SafeVerticalInset + 28.0f * ResolutionScale;
	const float PanelY = FMath::Max(
		MinimumPanelY,
		MaximumBottomY - PanelHeight);
	if (OutPanelTop)
	{
		*OutPanelTop = PanelY;
	}
	const float PanelX = (Canvas->ClipX - PanelWidth) * 0.5f;
	const float SurfaceAlpha = Settings.CaptionBackgroundOpacity * Alpha;
	const float CornerRadius = PanelHeight * 0.22f;
	DrawRoundedHudSurface(
		FVector2D(PanelX, PanelY + 4.0f * ResolutionScale),
		FVector2D(PanelWidth, PanelHeight),
		CornerRadius,
		FLinearColor(0.0f, 0.0f, 0.0f, SurfaceAlpha * 0.34f));
	DrawRoundedHudSurface(
		FVector2D(PanelX, PanelY),
		FVector2D(PanelWidth, PanelHeight),
		CornerRadius,
		FLinearColor(0.018f, 0.026f, 0.024f, SurfaceAlpha));
	DrawDialogueFilm(
		FVector2D(PanelX, PanelY),
		FVector2D(PanelWidth, PanelHeight),
		CornerRadius,
		SurfaceAlpha * 0.11f);
	RecordLayoutValidationRect(
		FVector2D(PanelX, PanelY),
		FVector2D(PanelX + PanelWidth, PanelY + PanelHeight));
	FLinearColor WaveColor = IGHorrorHUD::DialogueTeal;
	WaveColor.A = Alpha * 0.78f;
	const float WaveCenterY = PanelY + PanelHeight * 0.5f;
	const float WaveHeights[] = {5.0f, 11.0f, 16.0f, 8.0f};
	for (int32 BarIndex = 0; BarIndex < UE_ARRAY_COUNT(WaveHeights); ++BarIndex)
	{
		const float BarHeight = WaveHeights[BarIndex] * ResolutionScale;
		FCanvasTileItem WaveBar(
			FVector2D(
				PanelX + HorizontalPadding + BarIndex * 4.0f * ResolutionScale,
				WaveCenterY - BarHeight * 0.5f),
			FVector2D(FMath::Max(1.0f, 1.5f * ResolutionScale), BarHeight),
			WaveColor);
		WaveBar.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(WaveBar);
	}
	FLinearColor CaptionColor = FLinearColor(0.80f, 0.81f, 0.77f, 1.0f);
	CaptionColor.A = Alpha;
	const bool bUseTextOutline = Settings.CaptionBackgroundOpacity < 0.42f;
	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		DrawLeftAlignedText(
			FText::FromString(Lines[LineIndex]),
			FVector2D(
				PanelX + HorizontalPadding + IconLaneWidth,
				PanelY + VerticalPadding + LineIndex * LineStep),
			CaptionColor,
			EIGHudTextRole::Dialogue,
			CaptionScale,
			bUseTextOutline);
	}
	bAudioCaptionDrawnInLastHudFrame = true;
	return true;
}

void AIGHorrorHUD::ValidateSettingsTextRect(
	const FVector2D& Minimum,
	const FVector2D& Maximum,
	const FVector2D& ContainerMinimum,
	const FVector2D& ContainerMaximum)
{
	if (!bLayoutValidationEnabled)
	{
		return;
	}
	constexpr float PixelTolerance = 1.5f;
	bLayoutValidationAllInsideSettingsContainers =
		bLayoutValidationAllInsideSettingsContainers
		&& Minimum.X >= ContainerMinimum.X - PixelTolerance
		&& Minimum.Y >= ContainerMinimum.Y - PixelTolerance
		&& Maximum.X <= ContainerMaximum.X + PixelTolerance
		&& Maximum.Y <= ContainerMaximum.Y + PixelTolerance;
}

void AIGHorrorHUD::DrawFirstPersonKnock(const double CurrentTime)
{
	if (!Canvas
		|| FirstPersonKnockFrames.Num()
			!= IGHorrorHUD::FirstPersonKnockFrameCount)
	{
		return;
	}
	for (const UTexture2D* Frame : FirstPersonKnockFrames)
	{
		if (!Frame || !Frame->GetResource())
		{
			return;
		}
	}

	const UIGInteractionComponent* Interaction = InteractionComponent.Get();
	const AActor* FocusedActor = Interaction
		? Interaction->GetFocusedActor()
		: nullptr;
	const bool bKnockReady = IsValid(FocusedActor)
		&& FocusedActor->ActorHasTag(FName(TEXT("MissingFloor.Verb.Knock")));
	const float ActionAge = FirstPersonKnockStartTime >= 0.0
		? static_cast<float>(CurrentTime - FirstPersonKnockStartTime)
		: -1.0f;
	const bool bActionActive = ActionAge >= 0.0f
		&& ActionAge < IGHorrorHUD::FirstPersonKnockDurationSeconds;
	if (!bActionActive && !bKnockReady)
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	const bool bReducedMotion = Accessibility
		&& Accessibility->IsReducedCameraMotionEnabled();

	const float SpriteSize = FMath::Clamp(
		Canvas->ClipY * 2.0f,
		960.0f,
		2880.0f);
	const FVector2D DrawSize(SpriteSize, SpriteSize);
	const FVector2D DrawPosition(
		Canvas->ClipX * 0.5f - SpriteSize * 0.17f,
		Canvas->ClipY * 0.5f - SpriteSize * 0.17f);

	auto DrawFrame = [this, &DrawPosition, &DrawSize](
		const int32 FrameIndex,
		const float Alpha)
	{
		if (Alpha <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		FCanvasTileItem FrameTile(
			DrawPosition,
			FirstPersonKnockFrames[FrameIndex]->GetResource(),
			DrawSize,
			FLinearColor(0.84f, 0.87f, 0.90f, FMath::Clamp(Alpha, 0.0f, 1.0f)));
		FrameTile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(FrameTile);
	};

	if (!bActionActive)
	{
		// The player sees their raised hand before committing the noisy verb.
		// A very small 0<->1 blend keeps it alive without becoming a weapon idle.
		const float ReadyBlend = bReducedMotion
			? 0.0f
			: 0.07f + 0.05f * (
				0.5f + 0.5f * FMath::Sin(static_cast<float>(CurrentTime) * 2.1f));
		const float ReadyAlpha = 0.72f * FMath::Clamp(
			FocusBracketAlpha,
			0.0f,
			1.0f);
		DrawFrame(0, ReadyAlpha * (1.0f - ReadyBlend));
		DrawFrame(1, ReadyAlpha * ReadyBlend);
		return;
	}

	const float NormalizedAge = FMath::Clamp(
		ActionAge / static_cast<float>(IGHorrorHUD::FirstPersonKnockDurationSeconds),
		0.0f,
		1.0f);
	const float Visibility = bKnockReady
		? 0.96f
		: 0.96f * (1.0f - IGHorrorHUD::SmoothStep01(
			(NormalizedAge - 0.68f) / 0.32f));
	if (bReducedMotion)
	{
		// Keep the tactile replacement cue but remove apparent arm travel.
		DrawFrame(2, Visibility);
		return;
	}

	// Audio and noise are emitted on the input frame, so the contact pose is
	// frame zero here. The authored recoil then blends back to the ready hand.
	if (NormalizedAge < 0.42f)
	{
		const float Blend = IGHorrorHUD::SmoothStep01(NormalizedAge / 0.42f);
		DrawFrame(2, Visibility * (1.0f - Blend));
		DrawFrame(3, Visibility * Blend);
	}
	else
	{
		const float Blend = IGHorrorHUD::SmoothStep01(
			(NormalizedAge - 0.42f) / 0.58f);
		DrawFrame(3, Visibility * (1.0f - Blend));
		DrawFrame(0, Visibility * Blend);
	}
}

bool AIGHorrorHUD::DrawCaptureEmbrace(const double CurrentTime)
{
	// 접촉은 월드의 3D 몸과 카메라가 맡는다. 화면에 별도 팔을 덧씌우지 않는다.
	return Canvas && CaptureEmbraceStartTime >= 0.0
		&& CurrentTime >= CaptureEmbraceStartTime && CurrentTime < CaptureEmbraceEndTime;
}

bool AIGHorrorHUD::DrawCaptureWakeEcho(const double CurrentTime)
{
	// 침대의 실제 시야가 돌아올 때까지 안내만 가린다.
	return Canvas && CaptureWakeEchoStartTime >= 0.0
		&& CurrentTime >= CaptureWakeEchoStartTime && CurrentTime < CaptureWakeEchoEndTime;
}

float AIGHorrorHUD::MeasureTextWidth(
	const FString& Text,
	UFont* Font,
	const float TextScale) const
{
	if (!Canvas || !Font || Text.IsEmpty())
	{
		return 0.0f;
	}

	float Width = 0.0f;
	float Height = 0.0f;
	Canvas->StrLen(Font, Text, Width, Height, true);
	return Width * FMath::Max(0.5f, TextScale);
}

float AIGHorrorHUD::MeasureTextHeight(
	const FString& Text,
	UFont* Font,
	const float TextScale) const
{
	if (!Canvas || !Font || Text.IsEmpty())
	{
		return 0.0f;
	}
	float Width = 0.0f;
	float Height = 0.0f;
	Canvas->StrLen(Font, Text, Width, Height, true);
	return Height * FMath::Max(0.5f, TextScale);
}

float AIGHorrorHUD::GetFittedTextScale(
	const FText& Text,
	const EIGHudTextRole TextRole,
	const float PreferredScale,
	const float MaximumWidth,
	const float MinimumScale) const
{
	UFont* Font = GetFontForRole(TextRole);
	if (!Font || Text.IsEmpty() || MaximumWidth <= 0.0f)
	{
		return FMath::Max(0.5f, PreferredScale);
	}

	const float SafePreferredScale = FMath::Max(0.5f, PreferredScale);
	const float RawWidth = MeasureTextWidth(Text.ToString(), Font, 1.0f);
	if (RawWidth <= KINDA_SMALL_NUMBER)
	{
		return SafePreferredScale;
	}
	return FMath::Clamp(
		MaximumWidth / RawWidth,
		FMath::Max(0.5f, MinimumScale),
		SafePreferredScale);
}

int32 AIGHorrorHUD::FindFittingCaptionPrefix(
	const FString& Text,
	UFont* Font,
	const float TextScale,
	const float MaximumWidth) const
{
	if (Text.IsEmpty() || !Font || MaximumWidth <= 0.0f)
	{
		return 0;
	}
	if (MeasureTextWidth(Text, Font, TextScale) <= MaximumWidth)
	{
		return Text.Len();
	}

	int32 BestLength = 0;
	int32 Low = 1;
	int32 High = Text.Len();
	while (Low <= High)
	{
		const int32 CandidateLength = Low + ((High - Low) / 2);
		if (MeasureTextWidth(Text.Left(CandidateLength), Font, TextScale)
			<= MaximumWidth)
		{
			BestLength = CandidateLength;
			Low = CandidateLength + 1;
		}
		else
		{
			High = CandidateLength - 1;
		}
	}
	return FMath::Max(1, BestLength);
}

void AIGHorrorHUD::WrapHudText(
	const FString& Source,
	UFont* Font,
	const float TextScale,
	const float MaximumWidth,
	const int32 MaximumLines,
	TArray<FString>& OutLines,
	FString& OutRemainder) const
{
	OutLines.Reset();
	OutRemainder.Reset();
	if (!Font || MaximumWidth <= 0.0f || MaximumLines <= 0)
	{
		OutRemainder = Source;
		return;
	}

	// 숫자와 로마자는 한 덩어리로 넘긴다. "11점"이 "1 / 1점"으로, "02:30"이
	// "02: / 30"으로 갈라지면 다른 숫자로 읽힌다.
	auto IsRunCharacter = [](const TCHAR Character)
	{
		return (Character >= TEXT('0') && Character <= TEXT('9'))
			|| (Character >= TEXT('A') && Character <= TEXT('Z'))
			|| (Character >= TEXT('a') && Character <= TEXT('z'))
			|| (Character >= 0xFF10 && Character <= 0xFF19)
			|| (Character >= 0xFF21 && Character <= 0xFF3A)
			|| (Character >= 0xFF41 && Character <= 0xFF5A)
			|| Character == TEXT(':') || Character == TEXT('.')
			|| Character == TEXT(',') || Character == TEXT('%')
			|| Character == TEXT('-') || Character == TEXT('/');
	};

	// 한 줄에 들어가는 만큼을 고른다. 쉼표나 띄어쓰기에서 끊고, 일본어와
	// 중국어의 줄바꿈 금칙을 지킨다.
	auto ChooseBreak = [&](const FString& Text, const float Width)
	{
		int32 BreakIndex = FindFittingCaptionPrefix(Text, Font, TextScale, Width);
		BreakIndex = FMath::Clamp(BreakIndex, 1, Text.Len());
		const int32 MinimumEditorialBreak = FMath::Max(1, BreakIndex / 2);
		for (int32 Index = BreakIndex - 1; Index >= MinimumEditorialBreak; --Index)
		{
			const TCHAR Character = Text[Index];
			if (FChar::IsWhitespace(Character))
			{
				BreakIndex = Index;
				break;
			}
			if (FCString::Strchr(TEXT(".,!?;:…。！？、，"), Character)
				&& !(Index + 1 < Text.Len() && IsRunCharacter(Character) && IsRunCharacter(Text[Index + 1])))
			{
				BreakIndex = Index + 1;
				break;
			}
		}
		if (BreakIndex > 1 && BreakIndex < Text.Len()
			&& IsRunCharacter(Text[BreakIndex - 1]) && IsRunCharacter(Text[BreakIndex]))
		{
			int32 RunStart = BreakIndex - 1;
			while (RunStart > 0 && IsRunCharacter(Text[RunStart - 1]))
			{
				--RunStart;
			}
			if (RunStart >= 1)
			{
				BreakIndex = RunStart;
			}
		}
		// 닫는 문장 부호, 작은 가나, 장음은 줄 앞에 오지 않고 여는 괄호는 줄 끝에
		// 남지 않는다. 이름 사이의 가운뎃점도 양쪽을 붙여 둔다. 앞 글자를 다음 줄로 넘긴다.
		static const TCHAR* NoLineStart =
			TEXT("、。，．・：；？！）」』】〕〉》］｝ーぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ…‥”’");
		static const TCHAR* NoLineEnd = TEXT("（「『【〔〈《［｛“‘・");
		for (int32 Guard = 0; Guard < 3 && BreakIndex > 1 && BreakIndex < Text.Len(); ++Guard)
		{
			const bool bBadStart = FCString::Strchr(NoLineStart, Text[BreakIndex]) != nullptr;
			const bool bBadEnd = FCString::Strchr(NoLineEnd, Text[BreakIndex - 1]) != nullptr;
			if (!bBadStart && !bBadEnd)
			{
				break;
			}
			--BreakIndex;
		}
		return BreakIndex;
	};

	// 문단 하나를 줄로 나눈다. Ends에는 각 줄이 문단에서 끝나는 위치를 담는다.
	auto BreakParagraph = [&](const FString& Paragraph, const float Width,
		TArray<FString>& Lines, TArray<int32>& Ends)
	{
		Lines.Reset();
		Ends.Reset();
		int32 Start = 0;
		while (Start < Paragraph.Len())
		{
			while (Start < Paragraph.Len() && FChar::IsWhitespace(Paragraph[Start]))
			{
				++Start;
			}
			if (Start >= Paragraph.Len())
			{
				break;
			}
			const FString Rest = Paragraph.Mid(Start);
			if (MeasureTextWidth(Rest, Font, TextScale) <= Width)
			{
				Lines.Add(Rest.TrimEnd());
				Ends.Add(Paragraph.Len());
				break;
			}
			const int32 BreakIndex = ChooseBreak(Rest, Width);
			FString Line = Rest.Left(BreakIndex).TrimEnd();
			if (Line.IsEmpty())
			{
				Line = Rest.Left(1);
				Start += 1;
			}
			else
			{
				Start += BreakIndex;
			}
			Lines.Add(MoveTemp(Line));
			Ends.Add(Start);
		}
	};

	// 마지막 줄에 한두 글자나 짧은 한 단어만 남는 줄. "い。", "받음."처럼 남으면
	// 문장이 끝난 줄 알았다가 한 번 더 읽게 된다.
	auto IsLonelyLastLine = [&](const FString& Line)
	{
		const FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.Len() <= 2)
		{
			return true;
		}
		return !Trimmed.Contains(TEXT(" "))
			&& MeasureTextWidth(Trimmed, Font, TextScale) < MaximumWidth * 0.22f;
	};

	FString Remaining = Source;
	Remaining.ReplaceInline(TEXT("\r"), TEXT(""));
	Remaining = Remaining.TrimStartAndEnd();
	TArray<FString> Lines;
	TArray<int32> Ends;
	while (!Remaining.IsEmpty() && OutLines.Num() < MaximumLines)
	{
		const int32 NewlineIndex = Remaining.Find(TEXT("\n"));
		const int32 ParagraphLength = NewlineIndex == INDEX_NONE
			? Remaining.Len()
			: NewlineIndex;
		const FString Paragraph = Remaining.Left(ParagraphLength).TrimStartAndEnd();
		const FString FollowingParagraphs = NewlineIndex == INDEX_NONE
			? FString()
			: Remaining.Mid(NewlineIndex);
		if (Paragraph.IsEmpty())
		{
			Remaining = FollowingParagraphs.TrimStart();
			continue;
		}

		BreakParagraph(Paragraph, MaximumWidth, Lines, Ends);
		if (Lines.Num() >= 2 && IsLonelyLastLine(Lines.Last()))
		{
			// 줄 수는 그대로 두고 폭만 조금씩 좁혀 끝줄에 글자를 나눠 준다.
			for (const float Factor : { 0.93f, 0.86f, 0.80f })
			{
				TArray<FString> Balanced;
				TArray<int32> BalancedEnds;
				BreakParagraph(Paragraph, MaximumWidth * Factor, Balanced, BalancedEnds);
				if (Balanced.Num() == Lines.Num() && !IsLonelyLastLine(Balanced.Last()))
				{
					Lines = MoveTemp(Balanced);
					Ends = MoveTemp(BalancedEnds);
					break;
				}
			}
		}

		const int32 Room = MaximumLines - OutLines.Num();
		if (Lines.Num() <= Room)
		{
			OutLines.Append(Lines);
			Remaining = FollowingParagraphs.TrimStart();
			continue;
		}
		for (int32 Index = 0; Index < Room; ++Index)
		{
			OutLines.Add(Lines[Index]);
		}
		Remaining = (Paragraph.Mid(Ends[Room - 1]).TrimStart() + FollowingParagraphs).TrimStart();
		break;
	}
	OutRemainder = Remaining.TrimStartAndEnd();
}

void AIGHorrorHUD::DrawFearDirection(const double CurrentTime)
{
	if (!Canvas || CurrentTime >= FearCueEndTime)
	{
		return;
	}
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Direction =
		(FearCueWorldLocation - ViewLocation).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const FVector Forward = ViewRotation.Vector();
	const FVector Right = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);
	const float ForwardAmount = FVector::DotProduct(Direction, Forward);
	const float RightAmount = FVector::DotProduct(Direction, Right);
	const float Angle = FMath::Atan2(RightAmount, ForwardAmount);
	const FVector2D Radial(FMath::Sin(Angle), -FMath::Cos(Angle));
	const FVector2D Tangent(-Radial.Y, Radial.X);
	const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	const FVector2D WaveCenter = Center + FVector2D(
		Radial.X * Canvas->ClipX * 0.42f,
		Radial.Y * Canvas->ClipY * 0.39f);

	const double Elapsed = CurrentTime - FearCueStartTime;
	const double Remaining = FearCueEndTime - CurrentTime;
	const float Alpha = FMath::Clamp(
		FMath::Min(
			static_cast<float>(Elapsed / 0.12),
			static_cast<float>(Remaining / 0.30)),
		0.0f,
		1.0f);
	const FLinearColor CueColor(0.72f, 0.74f, 0.72f, Alpha * 0.78f);
	constexpr int32 SegmentCount = 6;
	constexpr float SegmentLength = 8.0f;
	constexpr float WaveAmplitude = 4.0f;
	FVector2D Previous = WaveCenter
		- Tangent * (SegmentCount * SegmentLength * 0.5f);
	for (int32 Index = 1; Index <= SegmentCount; ++Index)
	{
		const float Across =
			(Index - SegmentCount * 0.5f) * SegmentLength;
		const float Wave = Index == SegmentCount
			? 0.0f
			: (Index % 2 == 0 ? WaveAmplitude : -WaveAmplitude);
		const FVector2D Next = WaveCenter
			+ Tangent * Across
			+ Radial * Wave;
		FCanvasLineItem Line(Previous, Next);
		Line.SetColor(CueColor);
		Line.LineThickness = 2.0f;
		Canvas->DrawItem(Line);
		Previous = Next;
	}
}

void AIGHorrorHUD::DrawHidingMask()
{
	const AIGPlayerCharacter* Player = Cast<AIGPlayerCharacter>(GetOwningPawn());
	const AIGHidingSpot* Spot = Player ? Player->GetHidingSpot() : nullptr;
	if (!Spot || !Canvas)
	{
		return;
	}
	const float MaskAlpha = Spot->GetMaskAlpha();
	if (MaskAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const float Width = Canvas->ClipX;
	const float Height = Canvas->ClipY;
	// 가장자리를 여덟 겹으로 흐린다. 칼같이 자른 틈은 화면 효과로 읽힌다.
	constexpr int32 FeatherSteps = 8;
	auto Band = [this, MaskAlpha](const float X, const float Y, const float BandWidth, const float BandHeight, const float Opacity)
	{
		if (BandWidth > 0.5f && BandHeight > 0.5f && Opacity > 0.0f)
		{
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, Opacity * MaskAlpha), X, Y, BandWidth, BandHeight);
		}
	};
	if (Spot->GetView() == EIGHidingView::DoorGap)
	{
		// 장롱 문틈. 고개를 돌리면 틈이 반대쪽으로 밀린다.
		const float GapCenter = Width * (0.5f - 0.16f * Spot->GetPeekYawAlpha());
		const float GapHalf = Width * 0.055f;
		const float Feather = Width * 0.035f;
		const float LeftEdge = GapCenter - GapHalf - Feather;
		const float RightEdge = GapCenter + GapHalf + Feather;
		const float StepWidth = Feather / FeatherSteps;
		Band(0.0f, 0.0f, LeftEdge, Height, 1.0f);
		Band(RightEdge, 0.0f, Width - RightEdge, Height, 1.0f);
		for (int32 Step = 0; Step < FeatherSteps; ++Step)
		{
			const float Opacity = 1.0f - (Step + 0.5f) / FeatherSteps;
			Band(LeftEdge + Step * StepWidth, 0.0f, StepWidth, Height, Opacity);
			Band(RightEdge - (Step + 1) * StepWidth, 0.0f, StepWidth, Height, Opacity);
		}
		// 문짝 위아래는 경첩과 선반에 가려 더 어둡다.
		Band(LeftEdge, 0.0f, RightEdge - LeftEdge, Height * 0.08f, 0.55f);
		Band(LeftEdge, Height * 0.92f, RightEdge - LeftEdge, Height * 0.08f, 0.55f);
		return;
	}
	// 침대 밑. 위는 침대 바닥이 덮고, 아래 끝은 방바닥이 코앞이다.
	const float TopEdge = Height * 0.36f;
	const float Feather = Height * 0.07f;
	const float StepHeight = Feather / FeatherSteps;
	Band(0.0f, 0.0f, Width, TopEdge, 1.0f);
	for (int32 Step = 0; Step < FeatherSteps; ++Step)
	{
		Band(0.0f, TopEdge + Step * StepHeight, Width, StepHeight, 1.0f - (Step + 0.5f) / FeatherSteps);
	}
	Band(0.0f, Height * 0.9f, Width, Height * 0.1f, 0.45f);
	// 양 끝은 침대 다리와 이불 자락.
	Band(0.0f, TopEdge, Width * 0.06f, Height - TopEdge, 0.6f);
	Band(Width * 0.94f, TopEdge, Width * 0.06f, Height - TopEdge, 0.6f);
}

void AIGHorrorHUD::DrawNoiseRipple(const double CurrentTime)
{
	if (!Canvas || CurrentTime >= RippleEndTime || RippleLoudness <= 0.0f)
	{
		return;
	}
	const APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	// Which way the sound went out. Deliberately duplicated from
	// DrawFearDirection rather than shared: the accessibility contract pins
	// that function's exact expressions.
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FVector Direction = (RippleWorldLocation - ViewLocation).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		// A sound made exactly at the camera — a footstep, usually — still
		// deserves a ring; put it straight ahead.
		Direction = ViewRotation.Vector();
	}

	const FVector RippleForward = ViewRotation.Vector();
	const FVector RippleRight = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);
	const float RippleAngle = FMath::Atan2(
		FVector::DotProduct(Direction, RippleRight),
		FVector::DotProduct(Direction, RippleForward));
	const FVector2D EdgeNormal(FMath::Sin(RippleAngle), -FMath::Cos(RippleAngle));
	const FVector2D ScreenCenter(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);

	// How far the sound carries, normalized: a footstep is a short scratch of
	// an arc, a hammer blow is a wide bow. Radius is post-masking, so a sound
	// swallowed by a fridge hum never reaches this function at all.
	const float CarryRatio = FMath::Clamp(
		RippleRadiusCentimeters / UIGNoiseSubsystem::CarryPerLoudness,
		0.0f,
		1.0f);
	const float SweepDegrees = FMath::Lerp(
		IGHorrorHUD::NoiseRippleMinimumSweepDegrees,
		IGHorrorHUD::NoiseRippleMaximumSweepDegrees,
		CarryRatio);

	const float Age = static_cast<float>(CurrentTime - RippleStartTime);
	const float Life = FMath::Clamp(
		Age / static_cast<float>(IGHorrorHUD::NoiseRippleDurationSeconds),
		0.0f,
		1.0f);
	const float FadeIn = IGHorrorHUD::SmoothStep01(Age / 0.1f);
	const float FadeOut = 1.0f - IGHorrorHUD::SmoothStep01((Life - 0.45f) / 0.55f);
	const float Alpha = FMath::Clamp(FadeIn * FadeOut, 0.0f, 1.0f);
	if (Alpha <= 0.01f)
	{
		return;
	}

	const UIGAccessibilitySubsystem* Accessibility = nullptr;
	if (const UWorld* World = GetWorld())
	{
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			Accessibility = GameInstance->GetSubsystem<UIGAccessibilitySubsystem>();
		}
	}
	const bool bReducedMotion =
		Accessibility && Accessibility->IsReducedCameraMotionEnabled();

	// The expansion is the ring's whole grammar, so reduced motion pins it at
	// its final radius and keeps the fade instead of dropping the element:
	// suppressing it would remove the only channel that carries loudness.
	const float Expansion = bReducedMotion ? 1.0f : FMath::Sqrt(Life);
	const float RadiusX = Canvas->ClipX * FMath::Lerp(0.30f, 0.455f, Expansion);
	const float RadiusY = Canvas->ClipY * FMath::Lerp(0.28f, 0.425f, Expansion);

	const FLinearColor RippleColor(
		0.78f,
		0.80f,
		0.78f,
		Alpha * FMath::Lerp(0.34f, 0.70f, CarryRatio));
	const float BaseAngle = FMath::Atan2(EdgeNormal.X, -EdgeNormal.Y);
	const float HalfSweep = FMath::DegreesToRadians(SweepDegrees) * 0.5f;

	FVector2D Previous = FVector2D::ZeroVector;
	for (int32 Index = 0; Index <= IGHorrorHUD::NoiseRippleSegmentCount; ++Index)
	{
		const float T =
			static_cast<float>(Index) / IGHorrorHUD::NoiseRippleSegmentCount;
		const float Angle = BaseAngle + FMath::Lerp(-HalfSweep, HalfSweep, T);
		const FVector2D Point = ScreenCenter + FVector2D(
			FMath::Sin(Angle) * RadiusX,
			-FMath::Cos(Angle) * RadiusY);
		if (Index > 0)
		{
			// Thin toward the ends so the arc reads as a wave rather than a
			// gauge with hard stops.
			const float EndTaper = FMath::Sin(T * UE_PI);
			FCanvasLineItem Line(Previous, Point);
			Line.SetColor(RippleColor);
			// §19.8. 존재가 낸 소리는 내 소리보다 가늘게 그린다. 색이
			// 아니라 두께로 나눠야 색각에서도 남는다.
			const float ForeignScale = bRippleIsForeign ? 0.45f : 1.0f;
			Line.LineThickness = FMath::Max(
				1.0f,
				IGHorrorHUD::NoiseRippleMaximumThickness * EndTaper
					* ForeignScale);
			Canvas->DrawItem(Line);
		}
		Previous = Point;
	}

	// Never recorded for layout validation: a screen-edge arc is outside the
	// caption-safe rect by design, and recording it would widen the bounds the
	// packaged frontend probe asserts.
}

void AIGHorrorHUD::DrawSettingsShell(
	const IGSettingsMenuLayout::FPanelMetrics& Metrics,
	const FText& Title,
	const FText& Subtitle,
	const FText& ContextLabel)
{
	if (!Canvas)
	{
		return;
	}

	const float Scale = Metrics.Scale;
	DrawRoundedHudSurface(
		Metrics.PanelPosition + FVector2D(0.0f, 10.0f * Scale),
		Metrics.PanelSize,
		Metrics.CornerRadius + 2.0f * Scale,
		FLinearColor(0.0f, 0.0f, 0.0f, 0.56f));
	DrawRoundedHudSurface(
		Metrics.PanelPosition,
		Metrics.PanelSize,
		Metrics.CornerRadius,
		IGHorrorHUD::SettingsPanel);
	// The rail tint belongs to the body only. Extending it behind the header
	// and footer creates a false clipping boundary through otherwise valid copy.
	FCanvasTileItem RailFill(
		FVector2D(Metrics.PanelPosition.X, Metrics.HeaderBottom),
		FVector2D(
			Metrics.RailRight - Metrics.PanelPosition.X,
			Metrics.FooterTop - Metrics.HeaderBottom),
		IGHorrorHUD::SettingsRail);
	RailFill.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(RailFill);

	FCanvasTileItem HeaderLine(
		FVector2D(Metrics.PanelPosition.X, Metrics.HeaderBottom),
		FVector2D(Metrics.PanelSize.X, FMath::Max(1.0f, Scale)),
		IGHorrorHUD::SettingsDivider);
	HeaderLine.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(HeaderLine);
	FCanvasTileItem RailLine(
		FVector2D(Metrics.RailRight, Metrics.HeaderBottom),
		FVector2D(FMath::Max(1.0f, Scale), Metrics.FooterTop - Metrics.HeaderBottom),
		IGHorrorHUD::SettingsDivider);
	RailLine.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(RailLine);
	FCanvasTileItem FooterLine(
		FVector2D(Metrics.PanelPosition.X, Metrics.FooterTop),
		FVector2D(Metrics.PanelSize.X, FMath::Max(1.0f, Scale)),
		IGHorrorHUD::SettingsDivider);
	FooterLine.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(FooterLine);

	const float HeaderLeft = Metrics.PanelPosition.X + 32.0f * Scale;
	const float HeaderRight =
		Metrics.PanelPosition.X + Metrics.PanelSize.X - 32.0f * Scale;
	const float HeaderGap = 24.0f * Scale;
	const float ContextScale = GetFittedTextScale(
		ContextLabel,
		EIGHudTextRole::Hint,
		0.84f * Scale,
		FMath::Min(280.0f * Scale, Metrics.PanelSize.X * 0.28f),
		0.64f * Scale);
	const float ContextWidth = MeasureTextWidth(
		ContextLabel.ToString(),
		GetFontForRole(EIGHudTextRole::Hint),
		ContextScale);
	const float HeaderTextWidth = FMath::Max(
		180.0f * Scale,
		HeaderRight - ContextWidth - HeaderGap - HeaderLeft);
	const float TitleScale = GetFittedTextScale(
		Title,
		EIGHudTextRole::Objective,
		1.02f * Scale,
		HeaderTextWidth,
		0.74f * Scale);
	const float SubtitleScale = GetFittedTextScale(
		Subtitle,
		EIGHudTextRole::Hint,
		0.84f * Scale,
		HeaderTextWidth,
		0.62f * Scale);
	const FVector2D HeaderContainerMinimum(
		HeaderLeft,
		Metrics.PanelPosition.Y + 10.0f * Scale);
	const FVector2D HeaderContainerMaximum(
		HeaderLeft + HeaderTextWidth,
		Metrics.HeaderBottom - 8.0f * Scale);
	const FVector2D ContextContainerMinimum(
		HeaderRight - ContextWidth,
		Metrics.PanelPosition.Y + 10.0f * Scale);
	const FVector2D ContextContainerMaximum(
		HeaderRight,
		Metrics.HeaderBottom - 8.0f * Scale);
	UFont* TitleFont = GetFontForRole(EIGHudTextRole::Objective);
	UFont* SubtitleFont = GetFontForRole(EIGHudTextRole::Hint);
	const float TitleHeight = MeasureTextHeight(
		Title.ToString(),
		TitleFont,
		TitleScale);
	const float SubtitleHeight = MeasureTextHeight(
		Subtitle.ToString(),
		SubtitleFont,
		SubtitleScale);
	const float TitleY = Metrics.PanelPosition.Y + 19.0f * Scale;
	const float SubtitleY = FMath::Max(
		Metrics.PanelPosition.Y + 64.0f * Scale,
		TitleY + TitleHeight + 8.0f * Scale);
	ValidateSettingsTextRect(
		FVector2D(HeaderLeft, TitleY),
		FVector2D(
			HeaderLeft + MeasureTextWidth(
				Title.ToString(),
				TitleFont,
				TitleScale),
			TitleY + TitleHeight),
		HeaderContainerMinimum,
		HeaderContainerMaximum);
	ValidateSettingsTextRect(
		FVector2D(HeaderLeft, SubtitleY),
		FVector2D(
			HeaderLeft + MeasureTextWidth(
				Subtitle.ToString(),
				SubtitleFont,
				SubtitleScale),
			SubtitleY + SubtitleHeight),
		HeaderContainerMinimum,
		HeaderContainerMaximum);
	ValidateSettingsTextRect(
		FVector2D(HeaderRight - ContextWidth, Metrics.PanelPosition.Y + 34.0f * Scale),
		FVector2D(
			HeaderRight,
			Metrics.PanelPosition.Y + 34.0f * Scale
				+ IGHorrorHUD::SmallFontSize * ContextScale * 1.35f),
		ContextContainerMinimum,
		ContextContainerMaximum);

	DrawLeftAlignedText(
		Title,
		FVector2D(HeaderLeft, TitleY),
		IGHorrorHUD::SettingsPrimary,
		EIGHudTextRole::Objective,
		TitleScale);
	DrawLeftAlignedText(
		Subtitle,
		FVector2D(
			HeaderLeft + 1.0f * Scale,
			SubtitleY),
		IGHorrorHUD::SettingsSecondary,
		EIGHudTextRole::Hint,
		SubtitleScale);
	DrawRightAlignedText(
		ContextLabel,
		FVector2D(
			HeaderRight,
			Metrics.PanelPosition.Y + 34.0f * Scale),
		IGHorrorHUD::SettingsSecondary,
		EIGHudTextRole::Hint,
		ContextScale);
	DrawLeftAlignedText(
		NSLOCTEXT("IGHUD", "SettingsCategoryHeading", "설정 항목"),
		FVector2D(
			Metrics.RailLeft + 10.0f * Scale,
			Metrics.HeaderBottom + 17.0f * Scale),
		IGHorrorHUD::SettingsSecondary,
		EIGHudTextRole::Hint,
		0.75f * Scale);
	DrawLeftAlignedText(
		NSLOCTEXT("IGHUD", "SettingsOptionsHeading", "세부 설정"),
		FVector2D(
			Metrics.ContentLeft,
			Metrics.HeaderBottom + 17.0f * Scale),
		IGHorrorHUD::SettingsSecondary,
		EIGHudTextRole::Hint,
		0.75f * Scale);

	RecordLayoutValidationRect(
		Metrics.PanelPosition,
		Metrics.PanelPosition + Metrics.PanelSize);
}

void AIGHorrorHUD::DrawSettingsCategoryRow(
	const IGSettingsMenuLayout::FPanelMetrics& Metrics,
	const int32 CategoryIndex,
	const FText& Label,
	const bool bSelected)
{
	const float Scale = Metrics.Scale;
	const FVector2D Position(
		Metrics.RailLeft,
		Metrics.CategoryStartY + CategoryIndex * Metrics.CategoryRowHeight);
	const FVector2D Size(
		Metrics.RailRight - Metrics.RailLeft - 14.0f * Scale,
		Metrics.CategoryRowHeight - 6.0f * Scale);
	if (bSelected)
	{
		DrawRoundedHudSurface(
			Position,
			Size,
			7.0f * Scale,
			IGHorrorHUD::SettingsRaised);
	}
	const float CategoryTextScale = GetFittedTextScale(
		Label,
		bSelected ? EIGHudTextRole::Prompt : EIGHudTextRole::Hint,
		0.94f * Scale,
		Size.X - 32.0f * Scale,
		0.64f * Scale);
	UFont* CategoryFont = GetFontForRole(
		bSelected ? EIGHudTextRole::Prompt : EIGHudTextRole::Hint);
	const float CategoryTextWidth = MeasureTextWidth(
		Label.ToString(),
		CategoryFont,
		CategoryTextScale);
	const float CategoryTextHeight = MeasureTextHeight(
		Label.ToString(),
		CategoryFont,
		CategoryTextScale);
	const float CategoryTextY = Position.Y
		+ FMath::Max(0.0f, (Size.Y - CategoryTextHeight) * 0.5f);
	ValidateSettingsTextRect(
		FVector2D(Position.X + 16.0f * Scale, CategoryTextY),
		FVector2D(
			Position.X + 16.0f * Scale + CategoryTextWidth,
			CategoryTextY + CategoryTextHeight),
		Position,
		Position + Size);
	DrawLeftAlignedText(
		Label,
		FVector2D(Position.X + 16.0f * Scale, CategoryTextY),
		bSelected
			? IGHorrorHUD::SettingsPrimary
			: IGHorrorHUD::SettingsSecondary,
		bSelected ? EIGHudTextRole::Prompt : EIGHudTextRole::Hint,
		CategoryTextScale);
}

void AIGHorrorHUD::DrawSettingsOptionRow(
	const IGSettingsMenuLayout::FPanelMetrics& Metrics,
	const int32 LocalRow,
	const FText& Label,
	const FText& Value,
	const bool bSelected,
	const bool bAdjustable)
{
	const float Scale = Metrics.Scale;
	const FVector2D Position(
		Metrics.ContentLeft,
		Metrics.OptionStartY + LocalRow * Metrics.OptionRowHeight);
	const FVector2D Size(
		Metrics.ContentRight - Metrics.ContentLeft,
		Metrics.OptionRowHeight - 7.0f * Scale);
	if (bSelected)
	{
		DrawRoundedHudSurface(
			Position,
			Size,
			8.0f * Scale,
			IGHorrorHUD::SettingsSelected);
	}
	else
	{
		FCanvasTileItem Divider(
			FVector2D(Position.X + 14.0f * Scale, Position.Y + Size.Y),
			FVector2D(Size.X - 28.0f * Scale, 1.0f),
			IGHorrorHUD::SettingsDivider);
		Divider.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Divider);
	}

	UFont* ValueFont = GetFontForRole(EIGHudTextRole::Hint);
	const FString ValueText = bAdjustable
		? FString::Printf(TEXT("−  %s  +"), *Value.ToString())
		: Value.ToString();
	const float ValueScale = 0.88f * Scale;
	const float ValueWidth = Value.IsEmpty() || !ValueFont
		? 0.0f
		: MeasureTextWidth(ValueText, ValueFont, ValueScale);
	const float LabelWidthLimit = FMath::Max(
		120.0f * Scale,
		Size.X - ValueWidth - 64.0f * Scale);
	const float LabelTextScale = GetFittedTextScale(
		Label,
		bSelected ? EIGHudTextRole::Prompt : EIGHudTextRole::Hint,
		0.96f * Scale,
		LabelWidthLimit,
		0.62f * Scale);
	const float LabelWidth = MeasureTextWidth(
		Label.ToString(),
		GetFontForRole(
			bSelected ? EIGHudTextRole::Prompt : EIGHudTextRole::Hint),
		LabelTextScale);
	UFont* LabelFont = GetFontForRole(
		bSelected ? EIGHudTextRole::Prompt : EIGHudTextRole::Hint);
	const float LabelHeight = MeasureTextHeight(
		Label.ToString(),
		LabelFont,
		LabelTextScale);
	const float ValueHeight = MeasureTextHeight(
		ValueText,
		ValueFont,
		ValueScale);
	const float LabelY = Position.Y
		+ FMath::Max(0.0f, (Size.Y - LabelHeight) * 0.5f);
	const float ValueY = Position.Y
		+ FMath::Max(0.0f, (Size.Y - ValueHeight) * 0.5f);
	ValidateSettingsTextRect(
		FVector2D(Position.X + 18.0f * Scale, LabelY),
		FVector2D(
			Position.X + 18.0f * Scale + LabelWidth,
			LabelY + LabelHeight),
		Position,
		Position + Size);
	if (!Value.IsEmpty())
	{
		ValidateSettingsTextRect(
			FVector2D(
				Position.X + Size.X - 18.0f * Scale - ValueWidth,
				ValueY),
			FVector2D(
				Position.X + Size.X - 18.0f * Scale,
				ValueY + ValueHeight),
			Position,
			Position + Size);
	}
	DrawLeftAlignedText(
		Label,
		FVector2D(Position.X + 18.0f * Scale, LabelY),
		bSelected
			? IGHorrorHUD::SettingsPrimary
			: IGHorrorHUD::SettingsSecondary,
		bSelected ? EIGHudTextRole::Prompt : EIGHudTextRole::Hint,
		LabelTextScale);
	if (!Value.IsEmpty())
	{
		DrawRightAlignedText(
			FText::FromString(ValueText),
			FVector2D(
				Position.X + Size.X - 18.0f * Scale,
				ValueY),
			bSelected
				? IGHorrorHUD::SettingsPrimary
				: IGHorrorHUD::SettingsSecondary,
			EIGHudTextRole::Hint,
			ValueScale);
	}
}

float AIGHorrorHUD::DrawSettingsDetailText(
	const FString& Text,
	const FVector2D& Position,
	const float MaximumWidth,
	const float TextScale,
	const FLinearColor& Color)
{
	UFont* Font = GetFontForRole(EIGHudTextRole::Hint);
	if (!Font || Text.IsEmpty() || MaximumWidth <= 0.0f)
	{
		return Position.Y;
	}

	float FitScale = TextScale;
	TArray<FString> Lines;
	FString Remainder;
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		Lines.Reset();
		Remainder.Reset();
		WrapHudText(Text, Font, FitScale, MaximumWidth, 2, Lines, Remainder);
		if (Remainder.IsEmpty())
		{
			break;
		}
		FitScale *= 0.90f;
	}
	if (!Remainder.IsEmpty())
	{
		// 두 줄에 담기지 않으면 세 줄로 보여 준다. 설명을 중간에서 자르지 않는다.
		Lines.Reset();
		Remainder.Reset();
		WrapHudText(Text, Font, FitScale, MaximumWidth, 3, Lines, Remainder);
	}

	float Bottom = Position.Y;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const float DetailLineHeight = MeasureTextHeight(
			Lines[Index],
			Font,
			FitScale);
		const FVector2D LinePosition = Position + FVector2D(
			0.0f,
			Index * FMath::Max(
				IGHorrorHUD::SmallFontSize * FitScale * 1.48f,
				DetailLineHeight + 6.0f * FitScale));
		Bottom = FMath::Max(Bottom, LinePosition.Y + DetailLineHeight);
		const float LineWidth = MeasureTextWidth(
			Lines[Index],
			Font,
			FitScale);
		ValidateSettingsTextRect(
			LinePosition,
			LinePosition + FVector2D(
				LineWidth,
				DetailLineHeight),
			Position,
			FVector2D(
				Position.X + MaximumWidth,
				Position.Y + IGHorrorHUD::SmallFontSize * FitScale * 4.5f));
		DrawLeftAlignedText(
			FText::FromString(Lines[Index]),
			LinePosition,
			Color,
			EIGHudTextRole::Hint,
			FitScale);
	}
	return Bottom;
}

void AIGHorrorHUD::DrawSettingsFooterText(
	const IGSettingsMenuLayout::FPanelMetrics& Metrics,
	const FText& Text)
{
	const float Scale = Metrics.Scale;
	const float HorizontalPadding = 26.0f * Scale;
	const float MaximumWidth = Metrics.PanelSize.X - HorizontalPadding * 2.0f;
	const float FooterScale = GetFittedTextScale(
		Text,
		EIGHudTextRole::Hint,
		0.82f * Scale,
		MaximumWidth,
		0.58f * Scale);
	const float FooterTextHeight = MeasureTextHeight(
		Text.ToString(),
		GetFontForRole(EIGHudTextRole::Hint),
		FooterScale);
	const FVector2D FooterTextPosition(
		Metrics.PanelPosition.X + HorizontalPadding,
		Metrics.FooterTop + FMath::Max(
			0.0f,
			(Metrics.PanelPosition.Y + Metrics.PanelSize.Y
				- Metrics.FooterTop - FooterTextHeight) * 0.5f));
	ValidateSettingsTextRect(
		FooterTextPosition,
		FooterTextPosition + FVector2D(
			MeasureTextWidth(
				Text.ToString(),
				GetFontForRole(EIGHudTextRole::Hint),
				FooterScale),
			FooterTextHeight),
		FVector2D(
			Metrics.PanelPosition.X + HorizontalPadding,
			Metrics.FooterTop),
		FVector2D(
			Metrics.PanelPosition.X + Metrics.PanelSize.X - HorizontalPadding,
			Metrics.PanelPosition.Y + Metrics.PanelSize.Y));
	DrawLeftAlignedText(
		Text,
		FooterTextPosition,
		IGHorrorHUD::SettingsSecondary,
		EIGHudTextRole::Hint,
		FooterScale);
}

void AIGHorrorHUD::DrawAccessibilityPanel()
{
	if (!Canvas)
	{
		return;
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	if (!Accessibility)
	{
		return;
	}
	const FIGAccessibilitySettings Settings = Accessibility->GetSettings();
	const IGSettingsMenuLayout::FPanelMetrics Metrics =
		IGSettingsMenuLayout::MakeAccessibilityPanelMetrics(
			Canvas->ClipX, Canvas->ClipY, AccessibilitySelectedRow);
	const float Scale = Metrics.Scale;
	const auto OnOff = [](const bool bEnabled)
	{
		return bEnabled
			? NSLOCTEXT("IGHUD", "SettingOn", "켬").ToString()
			: NSLOCTEXT("IGHUD", "SettingOff", "끔").ToString();
	};

	const EIGNightDifficulty Difficulty = IGListenerTuning::LoadPersistedDifficulty();
	const FString DifficultyLabel = IGListenerTuning::GetDifficultyLabel(Difficulty).ToString();
	const FText DifficultyDescriptions[] = {
		NSLOCTEXT("IGHUD", "DifficultyDescQuiet", "위층 사람이 소리를 잘 알아채지 못하고, 쫓아오는 속도도 느려집니다."),
		NSLOCTEXT("IGHUD", "DifficultyDescStandard", "작은 소리에도 주의하며 돌아다녀야 합니다. 기본 난이도입니다."),
		NSLOCTEXT("IGHUD", "DifficultyDescHasty", "위층 사람이 작은 소리도 잘 듣고 오래 쫓아옵니다."),
		NSLOCTEXT("IGHUD", "DifficultyDescListenOnly", "위층 사람이 소리를 듣고 다가오지만 쫓거나 붙잡지는 않습니다. 퍼즐과 결말은 그대로 즐길 수 있습니다.")
	};
	const auto InputMode = [](const bool bToggle)
	{
		return bToggle
			? NSLOCTEXT("IGHUD", "InputModeToggle", "한 번 눌러 전환").ToString()
			: NSLOCTEXT("IGHUD", "InputModeHold", "누르는 동안").ToString();
	};
	const auto Percent = [](const float Value)
	{
		return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.0f));
	};

	const FString Labels[] =
	{
		NSLOCTEXT("IGHUD", "A11yDifficulty", "난이도").ToString(),
		NSLOCTEXT("IGHUD", "A11yHints", "힌트").ToString(),
		NSLOCTEXT("IGHUD", "A11yReducedMotion", "화면 흔들림 줄이기").ToString(),
		NSLOCTEXT("IGHUD", "A11yReducedFlicker", "빛 깜빡임 줄이기").ToString(),
		NSLOCTEXT("IGHUD", "A11yFieldOfView", "시야각").ToString(),
		NSLOCTEXT("IGHUD", "A11yVignette", "화면 가장자리 어둡게").ToString(),
		NSLOCTEXT("IGHUD", "A11yCameraTexture", "화면 질감").ToString(),
		NSLOCTEXT("IGHUD", "A11yCenterDot", "화면 가운데 점").ToString(),
		NSLOCTEXT("IGHUD", "A11yFearDirection", "소리가 나는 방향 표시").ToString(),
		NSLOCTEXT("IGHUD", "A11yKnockRing", "두드리는 소리를 화면에 표시").ToString(),
		NSLOCTEXT("IGHUD", "A11yKnockHaptic", "두드리는 소리를 진동으로 알림").ToString(),
		NSLOCTEXT("IGHUD", "A11yHeartbeat", "심장 박동 표시").ToString(),
		NSLOCTEXT("IGHUD", "A11yKnockAssist", "박자 맞추기 도움").ToString(),
		NSLOCTEXT("IGHUD", "A11ySoundCaptions", "소리 자막").ToString(),
		NSLOCTEXT("IGHUD", "A11yCaptionSize", "자막 글자 크기").ToString(),
		NSLOCTEXT("IGHUD", "A11yCaptionBackground", "자막 배경 진하기").ToString(),
		NSLOCTEXT("IGHUD", "A11yCaptionSafeArea", "자막 표시 영역").ToString(),
		NSLOCTEXT("IGHUD", "A11yCaptionDuration", "자막 표시 시간").ToString(),
		NSLOCTEXT("IGHUD", "A11yCrouchInput", "앉기 입력 방식").ToString(),
		NSLOCTEXT("IGHUD", "A11yHoldInput", "길게 누르기 방식").ToString(),
		NSLOCTEXT("IGHUD", "A11yHoldDuration", "길게 누르는 시간").ToString(),
		NSLOCTEXT("IGHUD", "A11yPromptKeys", "조작 키 표시").ToString(),
		NSLOCTEXT("IGHUD", "A11yHaptics", "컨트롤러 진동").ToString(),
		NSLOCTEXT("IGHUD", "A11yMicrophone", "마이크 소리 사용").ToString(),
		NSLOCTEXT("IGHUD", "A11yReset", "기본값으로 초기화").ToString(),
		NSLOCTEXT("IGHUD", "A11yClose", "닫기").ToString()
	};
	const FString Values[] =
	{
		DifficultyLabel,
		OnOff(Settings.bHintsEnabled),
		OnOff(Settings.bReducedCameraMotion),
		OnOff(Settings.bReducedFlicker),
		FString::Printf(
			TEXT("%d°"),
			FMath::RoundToInt(Settings.FieldOfViewDegrees)),
		Percent(Settings.ComfortVignetteStrength),
		Percent(Settings.CameraTextureStrength),
		Settings.bAlwaysShowCenterDot
			? NSLOCTEXT("IGHUD", "CenterDotAlways", "항상").ToString()
			: NSLOCTEXT("IGHUD", "CenterDotFocus", "물건을 볼 때").ToString(),
		OnOff(Settings.bDirectionalFearCues),
		OnOff(Settings.bKnockRippleSubstitute),
		OnOff(Settings.bKnockHapticSubstitute),
		OnOff(Settings.bHeartbeatWarning),
		OnOff(Settings.bCognitiveAssist),
		OnOff(Settings.bSoundCaptionsEnabled),
		Percent(Settings.CaptionSizeScale),
		Percent(Settings.CaptionBackgroundOpacity),
		Percent(Settings.CaptionSafeAreaScale),
		Percent(Settings.CaptionDurationScale),
		InputMode(Settings.bToggleCrouch),
		InputMode(Settings.bToggleHoldInteractions),
		Percent(Settings.HoldDurationScale),
		Settings.bAlwaysShowPromptKeys
			? NSLOCTEXT("IGHUD", "PromptKeysAlways", "항상").ToString()
			: NSLOCTEXT("IGHUD", "PromptKeysLearning", "익힐 때까지").ToString(),
		OnOff(Settings.bHapticsEnabled),
		OnOff(Settings.bMicrophoneNoiseEnabled),
		// 초기화는 두 번 눌러야 한다. 첫 번째 누름 뒤 4초 동안만 이 문구가 뜬다.
		AccessibilitySelectedRow == IGSettingsMenuLayout::ResetDefaults
			&& FPlatformTime::Seconds() < AccessibilityResetArmedUntil
			? NSLOCTEXT("IGHUD", "AccessibilityResetArmed", "한 번 더 누르면 초기화").ToString()
			: FString(),
		FString()
	};
	const FText Descriptions[] =
	{
		DifficultyDescriptions[static_cast<int32>(Difficulty)],
		NSLOCTEXT("IGHUD", "A11yHintsDesc", "힌트 키를 누를 때마다 지금 어디를 보면 되는지 조금씩 더 자세히 알려 줍니다. 끄면 힌트 키를 눌러도 아무 일도 일어나지 않습니다."),
		NSLOCTEXT("IGHUD", "A11yReducedMotionDesc", "걷거나 쫓길 때 화면이 덜 흔들리게 합니다."),
		NSLOCTEXT("IGHUD", "A11yReducedFlickerDesc", "손전등과 조명이 빠르게 깜빡이는 효과를 줄입니다."),
		NSLOCTEXT("IGHUD", "A11yFieldOfViewDesc", "한 화면에 보이는 범위를 조절합니다. 화면이 답답하거나 어지럽다면 편한 값으로 맞춰 보세요."),
		NSLOCTEXT("IGHUD", "A11yVignetteDesc", "화면 가장자리를 어둡게 합니다. 움직일 때 주변 풍경이 덜 보이게 할 수 있습니다."),
		NSLOCTEXT("IGHUD", "A11yCameraTextureDesc", "화면 가장자리에 약한 렌즈 왜곡 효과를 더합니다. 눈이 피로하면 낮춰 주세요."),
		NSLOCTEXT("IGHUD", "A11yCenterDotDesc", "평소에는 조사할 물건을 겨눌 때만 화면 가운데에 점이 뜹니다. 어지럽다면 항상 띄워 두세요. 화면을 볼 때 기준점이 됩니다."),
		NSLOCTEXT("IGHUD", "A11yFearDirectionDesc", "중요한 소리가 나면 화면 가장자리에 그 방향을 표시합니다."),
		NSLOCTEXT("IGHUD", "A11yKnockRingDesc", "위층 사람이 낸 소리는 얇은 원으로, 내가 낸 소리는 굵은 원으로 표시합니다."),
		NSLOCTEXT("IGHUD", "A11yKnockHapticDesc", "위층 사람이 소리를 내면 게임패드가 진동합니다. 멀리서 나는 소리일수록 진동도 약해집니다."),
		NSLOCTEXT("IGHUD", "A11yHeartbeatDesc", "긴장했을 때 화면 가장자리가 심장 박동에 맞춰 움직입니다."),
		NSLOCTEXT("IGHUD", "A11yKnockAssistDesc", "박자가 조금 늦거나 빨라도 맞은 것으로 칩니다."),
		NSLOCTEXT("IGHUD", "A11ySoundCaptionsDesc", "두드리는 소리나 발소리처럼 진행에 필요한 소리를 글로 보여 줍니다."),
		NSLOCTEXT("IGHUD", "A11yCaptionSizeDesc", "대화와 소리 자막의 글자 크기를 함께 바꿉니다. 아래에서 미리 볼 수 있습니다."),
		NSLOCTEXT("IGHUD", "A11yCaptionBackgroundDesc", "자막 뒤의 검은 배경을 얼마나 진하게 표시할지 정합니다."),
		NSLOCTEXT("IGHUD", "A11yCaptionSafeAreaDesc", "값을 줄이면 자막이 화면 안쪽에 표시됩니다."),
		NSLOCTEXT("IGHUD", "A11yCaptionDurationDesc", "자막이 사라지는 시간을 조절합니다. 읽을 시간이 부족하면 값을 높여 주세요."),
		NSLOCTEXT("IGHUD", "A11yCrouchInputDesc", "앉기 키를 한 번 눌러 전환하거나, 누르고 있는 동안만 유지하도록 선택합니다."),
		NSLOCTEXT("IGHUD", "A11yHoldInputDesc", "상호작용을 길게 누르는 대신 한 번 눌러 시작하고 다시 눌러 취소할 수 있습니다."),
		NSLOCTEXT("IGHUD", "A11yHoldDurationDesc", "문을 열거나 조사할 때 버튼을 얼마나 오래 누를지 정합니다."),
		NSLOCTEXT("IGHUD", "A11yPromptKeysDesc", "항상으로 두면 조사 안내 앞에 누를 키를 늘 붙입니다. 익힐 때까지로 두면 같은 동작을 세 번 한 뒤부터는 키 이름을 빼고 보여 줍니다."),
		NSLOCTEXT("IGHUD", "A11yHapticsDesc", "게임패드 진동을 켜거나 끕니다."),
		NSLOCTEXT("IGHUD", "A11yMicrophoneDesc", "켜면 내 목소리와 주변 소리에 위층 사람이 반응합니다. 꺼 두면 마이크를 사용하지 않습니다."),
		NSLOCTEXT("IGHUD", "A11yResetDesc", "이 화면의 설정을 처음 상태로 되돌립니다."),
		NSLOCTEXT("IGHUD", "A11yCloseDesc", "설정을 저장하고 이전 화면으로 돌아갑니다.")
	};
	static_assert(UE_ARRAY_COUNT(Labels) == IGSettingsMenuLayout::AccessibilityRowCount, "접근성 행 이름 수가 행 수와 다르다");
	static_assert(UE_ARRAY_COUNT(Values) == IGSettingsMenuLayout::AccessibilityRowCount, "접근성 값 수가 행 수와 다르다");
	static_assert(UE_ARRAY_COUNT(Descriptions) == IGSettingsMenuLayout::AccessibilityRowCount, "접근성 설명 수가 행 수와 다르다");
	const FString CategoryLabels[] =
	{
		NSLOCTEXT("IGHUD", "A11yCategoryGameplay", "게임 진행").ToString(),
		NSLOCTEXT("IGHUD", "A11yCategoryMotion", "화면 효과").ToString(),
		NSLOCTEXT("IGHUD", "A11yCategorySound", "소리 알림").ToString(),
		NSLOCTEXT("IGHUD", "A11yCategoryCaptions", "자막").ToString(),
		NSLOCTEXT("IGHUD", "A11yCategoryInput", "조작").ToString(),
		NSLOCTEXT("IGHUD", "A11yCategoryGeneral", "기타").ToString()
	};

	FCanvasTileItem Scrim(
		FVector2D::ZeroVector,
		FVector2D(Canvas->ClipX, Canvas->ClipY),
		FLinearColor(0.0f, 0.0f, 0.0f, 0.94f));
	Scrim.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Scrim);
	DrawSettingsShell(
		Metrics,
		NSLOCTEXT("IGHUD", "AccessibilityTitle", "접근성 설정"),
		NSLOCTEXT("IGHUD", "AccessibilitySubtitle", "글자, 소리 안내, 조작을 편하게 맞춰 주세요."),
		NSLOCTEXT("IGHUD", "AccessibilitySavedImmediately", "변경 즉시 저장"));

	const int32 ActiveCategory = IGSettingsMenuLayout::FindCategoryForRow(
		AccessibilitySelectedRow,
		IGSettingsMenuLayout::AccessibilityCategoryCount,
		IGSettingsMenuLayout::GetAccessibilityCategory);
	const IGSettingsMenuLayout::FCategoryRange ActiveRange =
		IGSettingsMenuLayout::GetAccessibilityCategory(ActiveCategory);
	for (int32 Category = 0;
		Category < IGSettingsMenuLayout::AccessibilityCategoryCount;
		++Category)
	{
		DrawSettingsCategoryRow(
			Metrics,
			Category,
			FText::FromString(CategoryLabels[Category]),
			Category == ActiveCategory);
	}
	for (int32 LocalRow = 0; LocalRow < ActiveRange.RowCount; ++LocalRow)
	{
		const int32 Row = ActiveRange.FirstRow + LocalRow;
		DrawSettingsOptionRow(
			Metrics,
			LocalRow,
			FText::FromString(Labels[Row]),
			FText::FromString(Values[Row]),
			Row == AccessibilitySelectedRow,
			Row < IGSettingsMenuLayout::ResetDefaults);
	}

	const float DetailTop = Metrics.OptionStartY
		+ ActiveRange.RowCount * Metrics.OptionRowHeight
		+ 18.0f * Scale;
	const float DetailBottom = Metrics.FooterTop - 18.0f * Scale;
	const float DetailHeight = ActiveCategory == 3
		? DetailBottom - DetailTop
		: FMath::Min(150.0f * Scale, DetailBottom - DetailTop);
	float DescriptionBottom = DetailTop;
	if (DetailTop < DetailBottom - 54.0f * Scale)
	{
		DrawRoundedHudSurface(
			FVector2D(Metrics.ContentLeft, DetailTop),
			FVector2D(
				Metrics.ContentRight - Metrics.ContentLeft,
				DetailHeight),
			8.0f * Scale,
			IGHorrorHUD::SettingsRaised);
		if (ActiveCategory != 3)
		{
			DrawLeftAlignedText(
				NSLOCTEXT("IGHUD", "SettingsEffectHeading", "설명"),
				FVector2D(Metrics.ContentLeft + 18.0f * Scale, DetailTop + 13.0f * Scale),
				IGHorrorHUD::SettingsSecondary, EIGHudTextRole::Hint, 0.72f * Scale);
		}
		DescriptionBottom = DrawSettingsDetailText(
			Descriptions[AccessibilitySelectedRow].ToString(),
			FVector2D(
				Metrics.ContentLeft + 18.0f * Scale,
				DetailTop + (ActiveCategory == 3 ? 14.0f : 41.0f) * Scale),
			Metrics.ContentRight - Metrics.ContentLeft - 36.0f * Scale,
			0.82f * Scale,
			IGHorrorHUD::SettingsPrimary);
	}

	// 자막 카테고리에서는 설정 설명만으로 결과를 상상하게 하지 않는다.
	// 선택한 크기·배경·안전 영역을 같은 화면의 실제 렌더링으로 확인한다.
	const float PreviewTextScale = GetResolutionTextScale(Settings.CaptionSizeScale);
	const float PreviewWidth = (Metrics.ContentRight - Metrics.ContentLeft - 36.0f * Scale)
		* Settings.CaptionSafeAreaScale;
	const FText PreviewBodyText = NSLOCTEXT("IGHUD", "CaptionPreviewBody", "[위] 천장에서 뭔가 끄는 소리");
	UFont* PreviewFont = GetFontForRole(EIGHudTextRole::Dialogue);
	float PreviewRawWidth = 0.0f;
	float PreviewRawHeight = 19.0f;
	if (PreviewFont)
	{
		Canvas->StrLen(
			PreviewFont,
			*GetLineHeightSample(),
			PreviewRawWidth,
			PreviewRawHeight,
			true);
	}
	TArray<FString> PreviewLines;
	FString PreviewRemainder;
	WrapHudText(
		PreviewBodyText.ToString(),
		PreviewFont,
		PreviewTextScale,
		PreviewWidth - 36.0f * Scale,
		2,
		PreviewLines,
		PreviewRemainder);
	if (PreviewLines.IsEmpty())
	{
		PreviewLines.Add(PreviewBodyText.ToString());
	}
	const float PreviewLineHeight = FMath::Max(
		16.0f,
		PreviewRawHeight * PreviewTextScale * 1.20f);
	const float PreviewBodyHeight =
		PreviewLineHeight * PreviewLines.Num();
	// 이름표 높이는 실제 글꼴로 잰다. 14px로 박아 두었더니 이름표가 본문
	// 첫 줄을 덮었고, 한자·가나 글꼴에서는 더 크게 겹쳤다.
	UFont* PreviewSpeakerFont = GetFontForRole(EIGHudTextRole::Speaker);
	const float PreviewSpeakerHeight = PreviewSpeakerFont
		? MeasureTextHeight(GetLineHeightSample(), PreviewSpeakerFont, PreviewTextScale * 0.78f)
		: 14.0f * PreviewTextScale * 0.78f;
	const float PreviewHeight = FMath::Max(
		48.0f * Scale,
		PreviewSpeakerHeight + PreviewBodyHeight + 23.0f * Scale);
	if (ActiveCategory == 3)
	{
		const float PreviewX = Metrics.ContentLeft
			+ (Metrics.ContentRight - Metrics.ContentLeft - PreviewWidth) * 0.5f;
		const float PreviewY = FMath::Max(
			DescriptionBottom + 12.0f * Scale,
			DetailTop + DetailHeight - PreviewHeight - 12.0f * Scale);
		ValidateSettingsTextRect(
			FVector2D(PreviewX, PreviewY),
			FVector2D(
				PreviewX + PreviewWidth,
				PreviewY + PreviewHeight),
			FVector2D(Metrics.ContentLeft, DetailTop),
			FVector2D(Metrics.ContentRight, DetailTop + DetailHeight));
		DrawRoundedHudSurface(
			FVector2D(PreviewX, PreviewY),
			FVector2D(PreviewWidth, PreviewHeight),
			5.0f * Scale,
			FLinearColor(
				0.018f,
				0.021f,
				0.020f,
				Settings.CaptionBackgroundOpacity));
		RecordLayoutValidationRect(
			FVector2D(PreviewX, PreviewY),
			FVector2D(PreviewX + PreviewWidth, PreviewY + PreviewHeight));
		DrawLeftAlignedText(
			NSLOCTEXT("IGHUD", "CaptionPreviewSpeaker", "미리 보기"),
			FVector2D(
				PreviewX + 18.0f * Scale,
				PreviewY + 7.0f * Scale),
			IGHorrorHUD::ThoughtBlue,
			EIGHudTextRole::Speaker,
			PreviewTextScale * 0.78f,
			true);
		for (int32 LineIndex = 0;
			LineIndex < PreviewLines.Num();
			++LineIndex)
		{
			const FVector2D PreviewLinePosition(
				PreviewX + 18.0f * Scale,
				PreviewY + 8.0f * Scale
					+ PreviewSpeakerHeight + 5.0f * Scale
					+ LineIndex * PreviewLineHeight);
			ValidateSettingsTextRect(
				PreviewLinePosition,
				PreviewLinePosition + FVector2D(
					MeasureTextWidth(
						PreviewLines[LineIndex],
						PreviewFont,
						PreviewTextScale),
					PreviewRawHeight * PreviewTextScale),
				FVector2D(PreviewX, PreviewY),
				FVector2D(
					PreviewX + PreviewWidth,
					PreviewY + PreviewHeight));
			DrawLeftAlignedText(
				FText::FromString(PreviewLines[LineIndex]),
				PreviewLinePosition,
				IGHorrorHUD::SettingsPrimary,
				EIGHudTextRole::Dialogue,
				PreviewTextScale,
				true);
		}
	}

	DrawSettingsFooterText(
		Metrics,
		bUsingGamepad
				? NSLOCTEXT(
					"IGHUD",
					"AccessibilityControlsGamepad",
					"D-pad 이동·값 변경  ·  A 선택  ·  B 닫기")
				: NSLOCTEXT(
					"IGHUD",
					"AccessibilityControlsKeyboard",
					"방향키 이동·값 변경  ·  Enter 선택  ·  Esc/F10 닫기"));
}

UTexture2D* AIGHorrorHUD::GetMissingFloorJournalThumbnail(
	const int32 ThumbnailType) const
{
	switch (static_cast<IGHorrorHUD::EJournalThumbnail>(ThumbnailType))
	{
	case IGHorrorHUD::EJournalThumbnail::Meter:
		return JournalMeterTexture;
	case IGHorrorHUD::EJournalThumbnail::Plaster:
		return JournalPlasterTexture;
	case IGHorrorHUD::EJournalThumbnail::Tank:
		return JournalTankTexture;
	case IGHorrorHUD::EJournalThumbnail::Metal:
		return JournalMetalTexture;
	case IGHorrorHUD::EJournalThumbnail::Document:
	default:
		return NotePaperTexture;
	}
}

void AIGHorrorHUD::DrawMissingFloorJournalPanel()
{
	if (!Canvas)
	{
		return;
	}

	const float ScreenWidth = Canvas->ClipX;
	const float ScreenHeight = Canvas->ClipY;
	const float ResolutionScale = FMath::Clamp(
		FMath::Min(ScreenWidth / 1920.0f, ScreenHeight / 1080.0f),
		0.68f,
		1.35f);
	DrawRect(
		FLinearColor(0.004f, 0.006f, 0.007f, 0.97f),
		0.0f,
		0.0f,
		ScreenWidth,
		ScreenHeight);

	const FVector2D PaperOrigin(ScreenWidth * 0.035f, ScreenHeight * 0.045f);
	const FVector2D PaperSize(ScreenWidth * 0.93f, ScreenHeight * 0.90f);
	DrawRect(
		FLinearColor(0.0f, 0.0f, 0.0f, 0.50f),
		PaperOrigin.X + 7.0f * ResolutionScale,
		PaperOrigin.Y + 10.0f * ResolutionScale,
		PaperSize.X,
		PaperSize.Y);
	if (MissingFloorJournalTexture)
	{
		DrawTexture(
			MissingFloorJournalTexture,
			PaperOrigin.X,
			PaperOrigin.Y,
			PaperSize.X,
			PaperSize.Y,
			0.0f,
			0.0f,
			1.0f,
			1.0f,
			FLinearColor(0.79f, 0.77f, 0.70f, 1.0f),
			BLEND_Opaque);
	}
	else
	{
		DrawRect(
			FLinearColor(0.73f, 0.70f, 0.62f, 1.0f),
			PaperOrigin.X,
			PaperOrigin.Y,
			PaperSize.X,
			PaperSize.Y);
	}
	// The texture carries tactile variation; this wash makes the runtime text
	// pass contrast at 720p without bleaching that grain into a generic panel.
	DrawRect(
		FLinearColor(0.08f, 0.075f, 0.06f, 0.10f),
		PaperOrigin.X,
		PaperOrigin.Y,
		PaperSize.X,
		PaperSize.Y);

	PushTextAuditContainer(PaperOrigin, PaperOrigin + PaperSize);
	const FLinearColor Ink(0.075f, 0.070f, 0.060f, 0.98f);
	const FLinearColor FaintInk(0.18f, 0.19f, 0.18f, 0.78f);
	const FLinearColor BlueRule(0.20f, 0.29f, 0.31f, 0.56f);
	const FLinearColor OxideRed(0.43f, 0.12f, 0.09f, 0.82f);
	auto DrawPaperText = [this](
		const FText& Text,
		const FVector2D& Position,
		const FLinearColor& Color,
		const EIGHudTextRole TextRole,
		const float TextScale)
	{
		UFont* Font = GetFontForRole(TextRole);
		if (!Canvas || !Font || Text.IsEmpty())
		{
			return;
		}
		const float SafeScale = FMath::Max(0.5f, TextScale);
		FCanvasTextItem Item(Position, Text, Font, Color);
		Item.Scale = FVector2D(SafeScale);
		Canvas->DrawItem(Item);
		if (bLayoutValidationEnabled || bTextAuditEnabled)
		{
			float Width = 0.0f;
			float Height = 0.0f;
			Canvas->StrLen(Font, Text.ToString(), Width, Height, true);
			RecordLayoutValidationRect(
				Position,
				Position + FVector2D(Width * SafeScale, Height * SafeScale));
			RecordTextAudit(
				Text.ToString(),
				Position,
				Position + FVector2D(Width * SafeScale, Height * SafeScale));
		}
	};
	auto DrawCenteredPaperText = [this, &DrawPaperText](
		const FText& Text,
		const float Y,
		const FLinearColor& Color,
		const EIGHudTextRole TextRole,
		const float TextScale)
	{
		UFont* Font = GetFontForRole(TextRole);
		float Width = 0.0f;
		float Height = 0.0f;
		if (!Canvas || !Font || Text.IsEmpty())
		{
			return;
		}
		Canvas->StrLen(Font, Text.ToString(), Width, Height, true);
		DrawPaperText(
			Text,
			FVector2D((Canvas->ClipX - Width * TextScale) * 0.5f, Y),
			Color,
			TextRole,
			TextScale);
	};
	const float OuterMargin = 30.0f * ResolutionScale;
	const float HeaderTop = PaperOrigin.Y + 20.0f * ResolutionScale;
	// 글자 크기는 해상도를 따르고, 위치는 실제로 잰 글자 높이에서 잡는다.
	// 고정 배율과 고정 간격을 섞어 두었더니 720p에서 제목이 부제를 덮고,
	// 글자를 키우면 발췌 줄끼리 포개졌다.
	const FString HeightSample = GetLineHeightSample();
	const auto LineHeightFor = [this, &HeightSample](
		const EIGHudTextRole TextRole,
		const float TextScale)
	{
		return MeasureTextHeight(HeightSample, GetFontForRole(TextRole), TextScale);
	};
	const float HeaderTitleScale = 1.08f * ResolutionScale;
	const float HeaderSubtitleScale = 0.78f * ResolutionScale;
	DrawPaperText(
		NSLOCTEXT("IGHUD", "MissingFloorJournalTitle", "조사 기록"),
		FVector2D(PaperOrigin.X + OuterMargin, HeaderTop),
		Ink,
		EIGHudTextRole::Objective,
		HeaderTitleScale);
	const float SubtitleTop = HeaderTop
		+ LineHeightFor(EIGHudTextRole::Objective, HeaderTitleScale) * 0.95f;
	DrawPaperText(
		NSLOCTEXT("IGHUD", "MissingFloorJournalSubtitle", "달빛빌라에서 알아낸 것들"),
		FVector2D(PaperOrigin.X + OuterMargin, SubtitleTop),
		FaintInk,
		EIGHudTextRole::Hint,
		HeaderSubtitleScale);
	const float HeaderBottom = SubtitleTop
		+ LineHeightFor(EIGHudTextRole::Hint, HeaderSubtitleScale);

	const UGameInstance* GameInstance = GetGameInstance();
	const UIGMissingFloorNarrativeSubsystem* Narrative = GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	const float UserTextScale = Accessibility
		? Accessibility->GetCaptionSizeScale()
		: 1.0f;
	TSet<FName> ObservedSources;
	if (Narrative)
	{
		for (const FIGMissingFloorTruthRecord& Record :
			Narrative->GetSnapshot().Truths)
		{
			for (const FName SourceId : Record.SourceIds)
			{
				ObservedSources.Add(SourceId);
			}
		}
	}

	TArray<const IGHorrorHUD::FJournalEntryDefinition*> Lanes[3];
	for (const IGHorrorHUD::FJournalEntryDefinition& Entry :
		IGHorrorHUD::JournalEntries())
	{
		if (ObservedSources.Contains(Entry.SourceId))
		{
			Lanes[static_cast<int32>(Entry.Lane)].Add(&Entry);
		}
	}
	const int32 PageCount = FMath::Max(1, GetMissingFloorJournalPageCount());
	const int32 SafePage = FMath::Clamp(
		MissingFloorJournalPageIndex,
		0,
		PageCount - 1);
	const int32 ObservedCount =
		Lanes[0].Num() + Lanes[1].Num() + Lanes[2].Num();
	const FText CountText = FText::Format(
		NSLOCTEXT("IGHUD", "JournalCountFormat", "{0}개 기록  ·  {1} / {2}"),
		FText::AsNumber(ObservedCount),
		FText::AsNumber(SafePage + 1),
		FText::AsNumber(PageCount));
	const float CountScale = 0.75f * ResolutionScale;
	DrawPaperText(
		CountText,
		FVector2D(
			PaperOrigin.X + PaperSize.X - OuterMargin
				- MeasureTextWidth(
					CountText.ToString(),
					GetFontForRole(EIGHudTextRole::Hint),
					CountScale),
			HeaderTop + 5.0f * ResolutionScale),
		FaintInk,
		EIGHudTextRole::Hint,
		CountScale);

	const float ContentLeft = PaperOrigin.X + OuterMargin;
	const float ContentRight = PaperOrigin.X + PaperSize.X - OuterMargin;
	const float ContentTop = FMath::Max(
		PaperOrigin.Y + 82.0f * ResolutionScale,
		HeaderBottom + 12.0f * ResolutionScale);
	const float FooterScale = 0.78f * ResolutionScale;
	const float FooterLineHeight = LineHeightFor(EIGHudTextRole::Hint, FooterScale);
	const float FooterHeight = FooterLineHeight + 20.0f * ResolutionScale;
	const float ContentBottom = PaperOrigin.Y + PaperSize.Y - FooterHeight;
	const float LaneGap = 13.0f * ResolutionScale;
	const float LaneWidth =
		(ContentRight - ContentLeft - LaneGap * 2.0f) / 3.0f;
	const float LaneTitleScale = 0.90f * ResolutionScale;
	const float LaneTitleHeight = LineHeightFor(EIGHudTextRole::Speaker, LaneTitleScale);
	const float LaneHeaderHeight = LaneTitleHeight + 9.0f * ResolutionScale;
	const float CardGap = 9.0f * ResolutionScale;
	const int32 CardsPerLanePerPage = UserTextScale > 1.50f
		? 1
		: UserTextScale > 1.15f ? 2 : 3;
	const float CardHeight =
		(ContentBottom - ContentTop - LaneHeaderHeight
			- CardGap * FMath::Max(0, CardsPerLanePerPage - 1))
		/ CardsPerLanePerPage;
	const FText LaneTitles[3] = {
		NSLOCTEXT("IGHUD", "JournalLaneAdministration", "건물 서류"),
		NSLOCTEXT("IGHUD", "JournalLaneLife", "소리와 흔적"),
		NSLOCTEXT("IGHUD", "JournalLanePersonal", "오빠의 물건"),
	};

	for (int32 LaneIndex = 0; LaneIndex < 3; ++LaneIndex)
	{
		const float LaneX = ContentLeft + LaneIndex * (LaneWidth + LaneGap);
		DrawPaperText(
			LaneTitles[LaneIndex],
			FVector2D(LaneX + 3.0f * ResolutionScale, ContentTop),
			LaneIndex == 0 ? OxideRed : Ink,
			EIGHudTextRole::Speaker,
			GetFittedTextScale(
				LaneTitles[LaneIndex],
				EIGHudTextRole::Speaker,
				LaneTitleScale,
				LaneWidth - 6.0f * ResolutionScale,
				LaneTitleScale * 0.7f));
		DrawRect(
			LaneIndex == 0 ? OxideRed : BlueRule,
			LaneX,
			ContentTop + LaneTitleHeight + 3.0f * ResolutionScale,
			LaneWidth,
			LaneIndex == 0 ? 1.4f : 1.0f);
		if (LaneIndex > 0)
		{
			DrawRect(
				FLinearColor(0.10f, 0.16f, 0.17f, 0.16f),
				LaneX - LaneGap * 0.5f,
				ContentTop,
				1.0f,
				ContentBottom - ContentTop);
		}
	}

	TMap<FName, FVector2D> VisibleCardCenters;
	for (int32 LaneIndex = 0; LaneIndex < 3; ++LaneIndex)
	{
		const int32 FirstEntry = SafePage * CardsPerLanePerPage;
		for (int32 Slot = 0; Slot < CardsPerLanePerPage; ++Slot)
		{
			const int32 EntryIndex = FirstEntry + Slot;
			if (!Lanes[LaneIndex].IsValidIndex(EntryIndex))
			{
				continue;
			}
			const float LaneX = ContentLeft + LaneIndex * (LaneWidth + LaneGap);
			const float CardY = ContentTop + LaneHeaderHeight
				+ Slot * (CardHeight + CardGap);
			VisibleCardCenters.Add(
				Lanes[LaneIndex][EntryIndex]->SourceId,
				FVector2D(LaneX + LaneWidth * 0.5f, CardY + CardHeight * 0.5f));
		}
	}

	// A faint pencil stroke is the only deduction visualization. It appears
	// only after the router confirms a truth and only when both source cards
	// are on this page; no line itself reveals a missing record.
	if (Narrative)
	{
		for (const FIGMissingFloorTruthRecord& Record :
			Narrative->GetSnapshot().Truths)
		{
			if (!Record.bConfirmed)
			{
				continue;
			}
			TArray<FVector2D> Points;
			for (const FName SourceId : Record.SourceIds)
			{
				if (const FVector2D* Center = VisibleCardCenters.Find(SourceId))
				{
					Points.Add(*Center);
				}
			}
			if (Points.Num() >= 2)
			{
				FVector2D Direction = Points[1] - Points[0];
				Direction.Normalize();
				const FVector2D Start = Points[0] + Direction * LaneWidth * 0.40f;
				const FVector2D End = Points[1] - Direction * LaneWidth * 0.40f;
				DrawLine(
					Start.X,
					Start.Y,
					End.X,
					End.Y,
					FLinearColor(0.34f, 0.10f, 0.08f, 0.42f),
					1.6f * ResolutionScale);
			}
		}
	}

	UFont* BodyFont = GetFontForRole(EIGHudTextRole::Dialogue);
	const float CardTextScale = FMath::Clamp(
		ResolutionScale * 0.92f * UserTextScale,
		0.64f,
		1.45f);
	for (int32 LaneIndex = 0; LaneIndex < 3; ++LaneIndex)
	{
		const int32 FirstEntry = SafePage * CardsPerLanePerPage;
		for (int32 Slot = 0; Slot < CardsPerLanePerPage; ++Slot)
		{
			const int32 EntryIndex = FirstEntry + Slot;
			if (!Lanes[LaneIndex].IsValidIndex(EntryIndex))
			{
				continue;
			}
			const IGHorrorHUD::FJournalEntryDefinition& Entry =
				*Lanes[LaneIndex][EntryIndex];
			const float LaneX = ContentLeft + LaneIndex * (LaneWidth + LaneGap);
			const float CardY = ContentTop + LaneHeaderHeight
				+ Slot * (CardHeight + CardGap);
			DrawRoundedHudSurface(
				FVector2D(LaneX, CardY),
				FVector2D(LaneWidth, CardHeight),
				8.0f * ResolutionScale,
				FLinearColor(0.83f, 0.81f, 0.74f, 0.76f));
			DrawRect(
				FLinearColor(0.16f, 0.18f, 0.17f, 0.18f),
				LaneX,
				CardY + CardHeight - 1.0f,
				LaneWidth,
				1.0f);

			const float Padding = 10.0f * ResolutionScale;
			const float ThumbnailSize = FMath::Clamp(
				CardHeight * 0.38f,
				44.0f * ResolutionScale,
				82.0f * ResolutionScale);
			if (UTexture2D* Thumbnail = GetMissingFloorJournalThumbnail(
				static_cast<int32>(Entry.Thumbnail)))
			{
				DrawTexture(
					Thumbnail,
					LaneX + Padding,
					CardY + Padding,
					ThumbnailSize,
					ThumbnailSize,
					0.08f,
					0.08f,
					0.84f,
					0.84f,
					FLinearColor(0.57f, 0.56f, 0.51f, 0.90f),
					BLEND_Opaque);
				DrawRect(
					FLinearColor(0.08f, 0.08f, 0.07f, 0.26f),
					LaneX + Padding,
					CardY + Padding + ThumbnailSize - 1.0f,
					ThumbnailSize,
					1.0f);
			}

			const float TextX = LaneX + Padding + ThumbnailSize
				+ 9.0f * ResolutionScale;
			const float TextWidth = LaneWidth - (TextX - LaneX) - Padding;
			// 제목과 장소 줄은 한 줄이다. 번역이 길면 카드 폭에 맞춰 줄인다.
			const float CardTitleScale = GetFittedTextScale(
				Entry.Title,
				EIGHudTextRole::Speaker,
				CardTextScale * 0.86f,
				TextWidth,
				CardTextScale * 0.86f * 0.7f);
			DrawPaperText(
				Entry.Title,
				FVector2D(TextX, CardY + Padding - 1.0f * ResolutionScale),
				Ink,
				EIGHudTextRole::Speaker,
				CardTitleScale);

			TArray<FString> ExcerptLines;
			FString ExcerptRemainder;
			// 발췌는 카드에 들어가는 만큼 줄을 쓴다. 두 줄로 잘라 나머지를 버리면
			// 한국어보다 긴 영어 번역에서 문서의 뒷말이 사라졌다.
			const float WhereScale = GetFittedTextScale(
				Entry.WhereWhen,
				EIGHudTextRole::Hint,
				CardTextScale * 0.62f,
				LaneWidth - Padding * 2.0f,
				CardTextScale * 0.62f * 0.7f);
			const float WhereHeight = LineHeightFor(EIGHudTextRole::Hint, WhereScale);
			const float ExcerptScale = CardTextScale * 0.72f;
			const float ExcerptStep = LineHeightFor(EIGHudTextRole::Dialogue, ExcerptScale) * 1.04f;
			const float ExcerptTop = Padding - 1.0f * ResolutionScale
				+ LineHeightFor(EIGHudTextRole::Speaker, CardTitleScale) + 3.0f * ResolutionScale;
			const float ExcerptBottom = CardHeight - WhereHeight - 10.0f * ResolutionScale;
			const int32 ExcerptMaxLines = FMath::Clamp(
				FMath::FloorToInt((ExcerptBottom - ExcerptTop) / FMath::Max(ExcerptStep, 1.0f)),
				1,
				10);
			WrapHudText(
				Entry.Excerpt.ToString(),
				BodyFont,
				ExcerptScale,
				TextWidth,
				ExcerptMaxLines,
				ExcerptLines,
				ExcerptRemainder);
			// 카드에 다 들어가지 않으면 끝에 줄임표를 달아 뒷말이 있다는 걸 남긴다.
			if (!ExcerptRemainder.IsEmpty() && ExcerptLines.Num() > 0)
			{
				ExcerptLines.Last() += TEXT("…");
			}
			float TextY = CardY + ExcerptTop;
			for (const FString& Line : ExcerptLines)
			{
				DrawPaperText(
					FText::FromString(Line),
					FVector2D(TextX, TextY),
					FaintInk,
					EIGHudTextRole::Dialogue,
					ExcerptScale);
				TextY += ExcerptStep;
			}
			DrawPaperText(
				Entry.WhereWhen,
				FVector2D(
					LaneX + Padding,
					CardY + CardHeight - WhereHeight - 5.0f * ResolutionScale),
				FaintInk,
				EIGHudTextRole::Hint,
				WhereScale);
		}
	}

	if (ObservedCount == 0)
	{
		DrawCenteredPaperText(
			NSLOCTEXT("IGHUD", "MissingFloorJournalEmpty", "아직 적어 둔 게 없다."),
			ContentTop + (ContentBottom - ContentTop) * 0.48f,
			FaintInk,
			EIGHudTextRole::Dialogue,
			0.90f * ResolutionScale);
	}

	DrawCenteredPaperText(
		bUsingGamepad
				? NSLOCTEXT(
					"IGHUD",
					"MissingFloorJournalControlsGamepad",
					"D-pad  기록 넘기기  ·  Y 닫기")
				: NSLOCTEXT(
					"IGHUD",
					"MissingFloorJournalControlsKeyboard",
					"← / →  기록 넘기기  ·  Tab 닫기"),
		PaperOrigin.Y + PaperSize.Y - FooterLineHeight - 9.0f * ResolutionScale,
		FaintInk,
		EIGHudTextRole::Hint,
		FooterScale);
	PopTextAuditContainer();
	RecordLayoutValidationRect(PaperOrigin, PaperOrigin + PaperSize);
}

void AIGHorrorHUD::DrawAudioCalibrationPanel()
{
	if (!Canvas)
	{
		return;
	}
	const IGSettingsMenuLayout::FAudioCalibrationMetrics Layout =
		IGSettingsMenuLayout::MakeAudioCalibrationMetrics(Canvas->ClipX, Canvas->ClipY);
	const float Scale = Layout.Scale;
	const FVector2D PanelSize = Layout.PanelSize;
	const FVector2D PanelOrigin = Layout.PanelPosition;
	PushTextAuditContainer(PanelOrigin, PanelOrigin + PanelSize);

	DrawRoundedHudSurface(
		PanelOrigin,
		PanelSize,
		18.0f * Scale,
		FLinearColor(0.016f, 0.019f, 0.020f, 0.98f));
	if (AudioCalibrationWallTexture
		&& AudioCalibrationWallTexture->GetResource())
	{
		FCanvasTileItem Wall(
			PanelOrigin + FVector2D(2.0f, 2.0f) * Scale,
			AudioCalibrationWallTexture->GetResource(),
			PanelSize - FVector2D(4.0f, 4.0f) * Scale,
			FLinearColor(0.60f, 0.62f, 0.63f, 0.34f));
		Wall.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Wall);
	}
	FCanvasTileItem Divider(
		FVector2D(PanelOrigin.X + PanelSize.X * 0.50f, Layout.DividerTop),
		FVector2D(1.0f, Layout.DividerBottom - Layout.DividerTop),
		FLinearColor(0.42f, 0.45f, 0.45f, 0.24f));
	Divider.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Divider);

	DrawCenteredText(
		NSLOCTEXT("IGHUD", "AudioCalibrationTitle", "소리와 밝기"),
		PanelOrigin.Y + 30.0f * Scale,
		IGHorrorHUD::PaleGray,
		EIGHudTextRole::Objective,
		1.08f * Scale);
	DrawCenteredText(
		bSystemMenuAudioCalibrationFirstRun
			? NSLOCTEXT("IGHUD", "AudioCalibrationFirstRunSubtitle", "시작하기 전에 소리 크기와 화면 밝기를 맞춰 주세요.")
			: NSLOCTEXT("IGHUD", "AudioCalibrationSubtitle", "헤드폰이나 모니터를 바꿨다면 다시 맞춰 주세요."),
		PanelOrigin.Y + 74.0f * Scale,
		IGHorrorHUD::MutedGray,
		EIGHudTextRole::Hint,
		0.90f * Scale);

	const float LeftX = PanelOrigin.X + 54.0f * Scale;
	const float RightX = PanelOrigin.X + PanelSize.X * 0.55f;
	const float ContentY = PanelOrigin.Y + 144.0f * Scale;
	// 두 칸의 폭. 가운데 구분선과 판 오른쪽 끝을 넘지 않는다.
	const float LeftColumnWidth =
		PanelOrigin.X + PanelSize.X * 0.50f - 24.0f * Scale - LeftX;
	const float RightColumnWidth =
		PanelOrigin.X + PanelSize.X - 40.0f * Scale - RightX;
	// 설명은 두 줄로 나뉘어 있다. 번역에서 가장 긴 줄이 칸을 넘으면 글자를 줄인다.
	auto FitBlockScale = [this](
		const FText& Text,
		const float PreferredScale,
		const float MaximumWidth)
	{
		TArray<FString> Lines;
		Text.ToString().ParseIntoArrayLines(Lines);
		UFont* Font = GetFontForRole(EIGHudTextRole::Hint);
		float Widest = 0.0f;
		for (const FString& Line : Lines)
		{
			Widest = FMath::Max(Widest, MeasureTextWidth(Line, Font, 1.0f));
		}
		return Widest > KINDA_SMALL_NUMBER
			? FMath::Clamp(MaximumWidth / Widest, PreferredScale * 0.7f, PreferredScale)
			: PreferredScale;
	};
	const FText KnockLabel =
		NSLOCTEXT("IGHUD", "AudioCalibrationKnockLabel", "위층에서 두드리는 소리");
	DrawLeftAlignedText(
		KnockLabel,
		FVector2D(LeftX, ContentY),
		IGHorrorHUD::ThoughtBlue,
		EIGHudTextRole::Prompt,
		GetFittedTextScale(
			KnockLabel, EIGHudTextRole::Prompt, Scale, LeftColumnWidth, 0.7f * Scale));
	const FText KnockInstruction =
		NSLOCTEXT("IGHUD", "AudioCalibrationKnockInstruction", "두드리는 소리가 또렷하게 들리면서도\n깜짝 놀라지 않을 만큼 맞춰 주세요.");
	DrawLeftAlignedText(
		KnockInstruction,
		FVector2D(LeftX, ContentY + 38.0f * Scale),
		IGHorrorHUD::PaleGray,
		EIGHudTextRole::Hint,
		FitBlockScale(KnockInstruction, 0.86f * Scale, LeftColumnWidth));

	const float MeterY = ContentY + 116.0f * Scale;
	const float MeterGap = 10.0f * Scale;
	const float MeterWidth = FMath::Min(
		38.0f * Scale,
		(PanelSize.X * 0.39f - MeterGap * 6.0f) / 7.0f);
	for (int32 Step = 0; Step < 7; ++Step)
	{
		const bool bFilled = Step <= AudioCalibrationVolumeStep;
		FCanvasTileItem Bar(
			FVector2D(LeftX + Step * (MeterWidth + MeterGap), MeterY),
			FVector2D(MeterWidth, 8.0f * Scale),
			bFilled
				? FLinearColor(0.62f, 0.70f, 0.72f, 0.92f)
				: FLinearColor(0.20f, 0.22f, 0.22f, 0.72f));
		Bar.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Bar);
	}

	const FText ShadowLabel =
		NSLOCTEXT("IGHUD", "AudioCalibrationShadowLabel", "어두운 화면 확인");
	DrawLeftAlignedText(
		ShadowLabel,
		FVector2D(RightX, ContentY),
		IGHorrorHUD::ThoughtBlue,
		EIGHudTextRole::Prompt,
		GetFittedTextScale(
			ShadowLabel, EIGHudTextRole::Prompt, Scale, RightColumnWidth, 0.7f * Scale));
	const FText ShadowInstruction =
		NSLOCTEXT("IGHUD", "AudioCalibrationShadowInstruction", "가운데 칸이 희미하게 보이도록 맞춰 주세요.\n왼쪽 칸은 배경과 구분되지 않아야 합니다.");
	DrawLeftAlignedText(
		ShadowInstruction,
		FVector2D(RightX, ContentY + 38.0f * Scale),
		IGHorrorHUD::PaleGray,
		EIGHudTextRole::Hint,
		FitBlockScale(ShadowInstruction, 0.86f * Scale, RightColumnWidth));
	const FLinearColor ShadowPatches[] =
	{
		FLinearColor(0.006f, 0.007f, 0.008f, 1.0f),
		FLinearColor(0.018f, 0.020f, 0.021f, 1.0f),
		FLinearColor(0.042f, 0.045f, 0.046f, 1.0f)
	};
	// 첫 칸과 같은 기준 블랙을 아래에 이어 붙인다. 벽면 무늬의 경계로
	// 첫 칸을 찾는 편법을 막고, 실제로 둘째 계조가 보이는지만 판단하게 한다.
	FCanvasTileItem ShadowBacking(
		FVector2D(RightX, ContentY + 100.0f * Scale),
		FVector2D(242.0f, 70.0f) * Scale,
		ShadowPatches[0]);
	ShadowBacking.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(ShadowBacking);
	for (int32 PatchIndex = 0; PatchIndex < 3; ++PatchIndex)
	{
		FCanvasTileItem Patch(
			FVector2D(
				RightX + PatchIndex * 86.0f * Scale,
				ContentY + 100.0f * Scale),
			FVector2D(70.0f, 70.0f) * Scale,
			ShadowPatches[PatchIndex]);
		Patch.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Patch);
	}

	const FString Labels[] =
	{
		NSLOCTEXT("IGHUD", "AudioCal.MasterVolume", "전체 소리").ToString(),
		NSLOCTEXT("IGHUD", "AudioCal.Music", "배경 음악").ToString(),
		NSLOCTEXT("IGHUD", "AudioCal.Ambience", "환경음").ToString(),
		NSLOCTEXT("IGHUD", "AudioCal.ListeningOn", "출력 장치").ToString(),
		NSLOCTEXT("IGHUD", "AudioCal.DisplayBrightness", "화면 밝기").ToString(),
		NSLOCTEXT("IGHUD", "AudioCal.PlayKnockAgain", "다시 듣기").ToString(),
		bSystemMenuAudioCalibrationFirstRun
			? NSLOCTEXT("IGHUD", "AudioCal.SaveAndContinue", "저장하고 시작하기").ToString()
			: NSLOCTEXT("IGHUD", "AudioCal.SaveAndBack", "저장하고 돌아가기").ToString()
	};
	static_assert(
		UE_ARRAY_COUNT(Labels) == IGSettingsMenuLayout::AudioCalibrationRowCount,
		"소리와 밝기 줄 수가 마우스 판정과 어긋났다");
	const float RowStartY = Layout.RowTop;
	const float RowSpacing = Layout.RowSpacing;
	for (int32 Row = 0; Row < UE_ARRAY_COUNT(Labels); ++Row)
	{
		const bool bSelected = Row == AudioCalibrationSelectedRow;
		FString Label = FString(bSelected ? TEXT(">  ") : TEXT("   ")) + Labels[Row];
		if (Row == 0 || Row == 1 || Row == 2 || Row == 4)
		{
			const int32 Step =
				Row == 0 ? AudioCalibrationVolumeStep
				: Row == 1 ? SystemMenuAudioCalibrationMusicStep
				: Row == 2 ? SystemMenuAudioCalibrationAmbienceStep
				: AudioCalibrationBrightnessStep;
			const int32 Count = Row == 0 ? 7 : 5;
			FString Dots;
			for (int32 Dot = 0; Dot < Count; ++Dot)
			{
				Dots += Dot == Step ? TEXT("●") : TEXT("○");
			}
			Label += FString::Printf(TEXT("    < %s >"), *Dots);
		}
		else if (Row == 3)
		{
			const FString Output = bSystemMenuHeadphoneOutput
				? NSLOCTEXT("IGHUD", "AudioCal.Headphones", "헤드폰").ToString()
				: NSLOCTEXT("IGHUD", "AudioCal.Speakers", "스피커").ToString();
			Label += FString::Printf(TEXT("    < %s >"), *Output);
		}
		const FText RowText = FText::FromString(Label);
		const EIGHudTextRole RowRole = bSelected ? EIGHudTextRole::Prompt : EIGHudTextRole::Hint;
		DrawCenteredText(
			RowText,
			RowStartY + Row * RowSpacing,
			bSelected ? IGHorrorHUD::RedAccent : IGHorrorHUD::PaleGray,
			RowRole,
			GetFittedTextScale(RowText, RowRole, 0.92f * Scale,
				PanelSize.X - 72.0f * Scale, 0.65f * Scale));
	}
	FText CalibrationNote;
	if (AudioCalibrationSelectedRow == 1 || AudioCalibrationSelectedRow == 2)
	{
		CalibrationNote = NSLOCTEXT("IGHUD", "AudioCalibrationBusNote", "배경 음악과 환경음은 따로 조절할 수 있습니다. 두드리는 소리와 위층 사람이 내는 소리는 전체 소리 크기를 따릅니다.");
	}
	if (AudioCalibrationSelectedRow == 3)
	{
		CalibrationNote = bSystemMenuHeadphoneOutput
					? NSLOCTEXT(
						"IGHUD", "AudioCalibrationOutputHeadphones",
						"헤드폰에 맞는 입체 음향을 사용합니다.")
					: NSLOCTEXT(
						"IGHUD", "AudioCalibrationOutputSpeakers",
						"스피커에 맞게 소리를 재생합니다. 방향을 구분하기 어렵다면 소리 자막을 켜 주세요.");
	}
	// 설명은 두 줄까지 쓴다. 긴 번역이 판 밖으로 나가거나 아래 조작 안내를 덮지 않는다.
	DrawSettingsDetailText(CalibrationNote.ToString(),
		FVector2D(PanelOrigin.X + 36.0f * Scale, Layout.NoteTop),
		PanelSize.X - 72.0f * Scale, 0.78f * Scale, IGHorrorHUD::PaleGray);
	const FText CalibrationControls = bUsingGamepad
				? NSLOCTEXT(
					"IGHUD", "AudioCalibrationControlsGamepad",
					"D-pad 항목·조정  ·  A 선택  ·  B 취소")
				: NSLOCTEXT(
					"IGHUD", "AudioCalibrationControlsKeyboard",
					"방향키/WASD 항목·조정  ·  Enter 선택  ·  Esc 취소  ·  마우스 선택");
	DrawCenteredText(
		CalibrationControls,
		Layout.FooterTop,
		IGHorrorHUD::MutedGray,
		EIGHudTextRole::Hint,
		GetFittedTextScale(CalibrationControls, EIGHudTextRole::Hint, 0.78f * Scale,
			PanelSize.X - 72.0f * Scale, 0.56f * Scale));
	PopTextAuditContainer();
	RecordLayoutValidationRect(PanelOrigin, PanelOrigin + PanelSize);
}

void AIGHorrorHUD::DrawDisplaySettingsPanel()
{
	if (!Canvas)
	{
		return;
	}
	const IGSettingsMenuLayout::FPanelMetrics Metrics =
		IGSettingsMenuLayout::MakePanelMetrics(Canvas->ClipX, Canvas->ClipY);
	const float Scale = Metrics.Scale;
	const FString WindowModes[] =
	{
		NSLOCTEXT("IGHUD", "Display.Fullscreen", "전체 화면").ToString(),
		NSLOCTEXT("IGHUD", "Display.Borderless", "테두리 없는 창").ToString(),
		NSLOCTEXT("IGHUD", "Display.Windowed", "창 모드").ToString()
	};
	const FString Resolutions[] =
	{
		TEXT("1280 x 720"),
		TEXT("1920 x 1080"),
		TEXT("2560 x 1440")
	};
	const FString Qualities[] =
	{
		NSLOCTEXT("IGHUD", "Display.Low", "낮음").ToString(),
		NSLOCTEXT("IGHUD", "Display.High", "높음").ToString()
	};
	const FString FrameLimits[] =
	{
		TEXT("30 FPS"),
		TEXT("60 FPS"),
		NSLOCTEXT("IGHUD", "Display.Unlimited", "제한 없음").ToString()
	};
	const FString Labels[] =
	{
		NSLOCTEXT("IGHUD", "Display.DisplayMode", "화면 모드").ToString(),
		NSLOCTEXT("IGHUD", "Display.Resolution", "해상도").ToString(),
		NSLOCTEXT("IGHUD", "Display.GraphicsQuality", "그래픽 품질").ToString(),
		NSLOCTEXT("IGHUD", "Display.VSync", "수직 동기화").ToString(),
		NSLOCTEXT("IGHUD", "Display.FrameLimit", "프레임 제한").ToString(),
		// 지금 언어를 못 읽는 사람도 찾을 수 있게 영어를 같이 적는다.
		NSLOCTEXT("IGHUD", "Display.Language", "언어 · Language").ToString(),
		NSLOCTEXT("IGHUD", "Display.Accessibility", "접근성 설정").ToString(),
		NSLOCTEXT("IGHUD", "Display.AudioBrightness", "소리와 밝기").ToString(),
		NSLOCTEXT("IGHUD", "Display.ControlsRebinding", "조작 설정").ToString(),
		bDisplaySettingsAwaitingConfirmation
			? NSLOCTEXT("IGHUD", "Display.KeepTheseSettings", "이 설정 유지").ToString()
			: NSLOCTEXT("IGHUD", "Display.ReapplyDisplay", "화면 설정 다시 적용").ToString(),
		bDisplaySettingsAwaitingConfirmation
			? NSLOCTEXT("IGHUD", "Display.RevertSettings", "이전 설정으로 되돌리기").ToString()
			: NSLOCTEXT("IGHUD", "Display.Back", "돌아가기").ToString()
	};
	const FString Values[] =
	{
		WindowModes[DisplayWindowModeIndex],
		Resolutions[DisplayResolutionIndex],
		Qualities[DisplayQualityIndex],
		(bSystemMenuVSync
			? NSLOCTEXT("IGHUD", "SettingOn", "켬")
			: NSLOCTEXT("IGHUD", "SettingOff", "끔")).ToString(),
		FrameLimits[DisplayFrameLimitIndex],
		UIGLanguageSubsystem::GetNativeLanguageName(
			GetGameInstance() && GetGameInstance()->GetSubsystem<UIGLanguageSubsystem>()
				? GetGameInstance()->GetSubsystem<UIGLanguageSubsystem>()->GetCurrentCulture()
				: FString(TEXT("ko"))).ToString(),
		NSLOCTEXT("IGHUD", "Display.Open", "열기").ToString(),
		NSLOCTEXT("IGHUD", "Display.Open", "열기").ToString(),
		NSLOCTEXT("IGHUD", "Display.Open", "열기").ToString(),
		FString(),
		FString()
	};
	const FString Descriptions[] =
	{
		NSLOCTEXT("IGHUD", "Display.WindowModeDesc", "전체 화면이나 창 모드로 바꿉니다. 바꾼 뒤 10초 동안 확인하지 않으면 이전 설정으로 돌아갑니다.").ToString(),
		NSLOCTEXT("IGHUD", "Display.ResolutionDesc", "화면 해상도를 바꿉니다. 모니터 해상도와 맞추면 가장 선명하게 보입니다.").ToString(),
		NSLOCTEXT("IGHUD", "Display.QualityDesc", "그림자와 반사 등의 품질을 조절합니다. 게임이 버벅인다면 낮춰 보세요.").ToString(),
		NSLOCTEXT("IGHUD", "Display.VSyncDesc", "화면이 가로로 갈라져 보이는 현상을 줄입니다. 조작 반응이 조금 느려질 수 있습니다.").ToString(),
		NSLOCTEXT("IGHUD", "Display.FrameLimitDesc", "초당 표시할 화면 수의 최대값을 정합니다. 값을 낮추면 발열과 전력 사용을 줄일 수 있습니다.").ToString(),
		NSLOCTEXT("IGHUD", "Display.LanguageDesc", "화면에 나오는 글의 언어를 바꿉니다. 바로 적용됩니다.").ToString(),
		NSLOCTEXT("IGHUD", "Display.AccessibilityDesc", "난이도, 자막, 화면 흔들림, 조작 도움을 설정합니다.").ToString(),
		NSLOCTEXT("IGHUD", "Display.AudioBrightnessDesc", "소리 크기와 화면 밝기를 맞춥니다.").ToString(),
		NSLOCTEXT("IGHUD", "Display.ControlsDesc", "동작별 키와 게임패드 버튼을 바꿉니다. Esc와 F10은 바꿀 수 없습니다.").ToString(),
		NSLOCTEXT("IGHUD", "Display.ReapplyDesc", "현재 화면 설정을 다시 적용합니다.").ToString(),
		NSLOCTEXT("IGHUD", "Display.BackDesc", "이전 화면으로 돌아갑니다.").ToString()
	};
	const FString CategoryLabels[] =
	{
		NSLOCTEXT("IGHUD", "Display.Display", "화면").ToString(),
		NSLOCTEXT("IGHUD", "Display.Performance", "성능").ToString(),
		NSLOCTEXT("IGHUD", "Display.General", "일반").ToString(),
		NSLOCTEXT("IGHUD", "Display.Changes", "변경 사항").ToString()
	};

	DrawSettingsShell(
		Metrics,
		NSLOCTEXT("IGHUD", "DisplaySettingsTitle", "화면 설정"),
		NSLOCTEXT("IGHUD", "DisplaySettingsSubtitle", "해상도, 그래픽 품질, 언어를 바꿀 수 있습니다."),
		NSLOCTEXT("IGHUD", "DisplayImmediateStatus", "바꾸는 즉시 적용되고 저장됩니다"));

	const int32 ActiveCategory = IGSettingsMenuLayout::FindCategoryForRow(
		DisplaySettingsSelectedRow,
		IGSettingsMenuLayout::DisplayCategoryCount,
		IGSettingsMenuLayout::GetDisplayCategory);
	const IGSettingsMenuLayout::FCategoryRange ActiveRange =
		IGSettingsMenuLayout::GetDisplayCategory(ActiveCategory);
	for (int32 Category = 0;
		Category < IGSettingsMenuLayout::DisplayCategoryCount;
		++Category)
	{
		DrawSettingsCategoryRow(
			Metrics,
			Category,
			FText::FromString(CategoryLabels[Category]),
			Category == ActiveCategory);
	}
	for (int32 LocalRow = 0; LocalRow < ActiveRange.RowCount; ++LocalRow)
	{
		const int32 Row = ActiveRange.FirstRow + LocalRow;
		DrawSettingsOptionRow(
			Metrics,
			LocalRow,
			FText::FromString(Labels[Row]),
			FText::FromString(Values[Row]),
			Row == DisplaySettingsSelectedRow,
			Row <= IGSettingsMenuLayout::FrameLimit || Row == IGSettingsMenuLayout::Language);
	}

	const float DetailTop = Metrics.OptionStartY
		+ ActiveRange.RowCount * Metrics.OptionRowHeight
		+ 22.0f * Scale;
	const float DetailBottom = Metrics.FooterTop - 22.0f * Scale;
	const float DetailHeight = FMath::Min(
		150.0f * Scale,
		DetailBottom - DetailTop);
	if (DetailTop < DetailBottom - 62.0f * Scale)
	{
		DrawRoundedHudSurface(
			FVector2D(Metrics.ContentLeft, DetailTop),
			FVector2D(
				Metrics.ContentRight - Metrics.ContentLeft,
				DetailHeight),
			8.0f * Scale,
			IGHorrorHUD::SettingsRaised);
		DrawLeftAlignedText(
			NSLOCTEXT("IGHUD", "DisplaySettingEffect", "선택한 항목"),
			FVector2D(
				Metrics.ContentLeft + 18.0f * Scale,
				DetailTop + 15.0f * Scale),
			IGHorrorHUD::SettingsSecondary,
			EIGHudTextRole::Hint,
			0.72f * Scale);
		DrawSettingsDetailText(
			Descriptions[DisplaySettingsSelectedRow],
			FVector2D(
				Metrics.ContentLeft + 18.0f * Scale,
				DetailTop + 45.0f * Scale),
			Metrics.ContentRight - Metrics.ContentLeft - 36.0f * Scale,
			0.84f * Scale,
			IGHorrorHUD::SettingsPrimary);
	}

	if (bDisplaySettingsAwaitingConfirmation)
	{
		DrawLeftAlignedText(
			FText::Format(
				NSLOCTEXT(
					"IGHUD",
					"DisplaySettingsConfirmCountdown",
					"이 화면 설정을 유지할까요? {0}초 뒤 자동으로 되돌립니다."),
				FText::AsNumber(DisplayConfirmationSecondsRemaining)),
			FVector2D(
				Metrics.ContentLeft + 18.0f * Scale,
				DetailTop + DetailHeight - 31.0f * Scale),
			IGHorrorHUD::SettingsAccent,
			EIGHudTextRole::Hint,
			0.82f * Scale);
	}
	else if (bDisplaySettingsApplied)
	{
		DrawLeftAlignedText(
			NSLOCTEXT("IGHUD", "DisplaySettingsApplied", "설정을 적용했습니다."),
			FVector2D(
				Metrics.ContentLeft + 18.0f * Scale,
				DetailTop + DetailHeight - 31.0f * Scale),
			IGHorrorHUD::SettingsSuccess,
			EIGHudTextRole::Hint,
			0.82f * Scale);
	}
	else if (!SystemMenuStatusText.IsEmpty())
	{
		DrawLeftAlignedText(
			SystemMenuStatusText,
			FVector2D(
				Metrics.ContentLeft + 18.0f * Scale,
				DetailTop + DetailHeight - 31.0f * Scale),
			bSystemMenuStatusIsError
				? IGHorrorHUD::SettingsAccent
				: IGHorrorHUD::SettingsPrimary,
			EIGHudTextRole::Hint,
			0.82f * Scale);
	}

	DrawSettingsFooterText(
		Metrics,
		bDisplaySettingsAwaitingConfirmation
			? bUsingGamepad
				? NSLOCTEXT("IGHUD", "DisplaySettingsConfirmControlsGamepad", "A 유지  ·  B/View 자동 복원  ·  아래 항목에서 되돌리기")
				: NSLOCTEXT("IGHUD", "DisplaySettingsConfirmControlsKeyboard", "Enter/클릭 유지  ·  Esc 자동 복원  ·  아래 항목에서 되돌리기")
			: bUsingGamepad
				? NSLOCTEXT(
					"IGHUD",
					"DisplaySettingsControlsGamepad",
					"D-pad 이동·값 변경  ·  A 선택  ·  B/View 취소")
				: NSLOCTEXT(
					"IGHUD",
					"DisplaySettingsControlsKeyboard",
					"방향키/WASD 이동·값 변경  ·  Enter 선택  ·  Esc 취소  ·  마우스 선택"));
}

namespace
{
	FText MakeBindingColumnHeader(
		const bool bGamepadColumn,
		const bool bSelectedIsGamepad)
	{
		// 고른 칸에 꺾쇠를 붙인다. 색맹 프로필에서도 어느 칸인지 읽힌다.
		const bool bSelected = bGamepadColumn == bSelectedIsGamepad;
		const FText Base = bGamepadColumn
			? (NSLOCTEXT("IGHUD", "KeyBindingsColumnPad", "게임패드"))
			: (NSLOCTEXT("IGHUD", "KeyBindingsColumnKeys", "키보드"));
		return bSelected
			? FText::Format(
				NSLOCTEXT("IGHUD", "KeyBindingsColumnActive", "[ {0} ]"), Base)
			: Base;
	}
}

void AIGHorrorHUD::DrawKeyBindingsPanel()
{
	if (!Canvas)
	{
		return;
	}
	const bool bPadHints = bUsingGamepad;
	const IGFrontendMenuLayout::FMetrics Metrics =
		IGFrontendMenuLayout::MakeMetrics(Canvas->ClipX, Canvas->ClipY);
	const float Scale = Metrics.Scale;
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGInputBindingSubsystem* Bindings = GameInstance
		? GameInstance->GetSubsystem<UIGInputBindingSubsystem>()
		: nullptr;
	if (!Bindings)
	{
		return;
	}

	DrawLeftAlignedText(
		NSLOCTEXT("IGHUD", "KeyBindingsContext", "설정 · 조작"),
		FVector2D(Metrics.ContentLeft, Metrics.TitleTop),
		IGHorrorHUD::SettingsSecondary,
		EIGHudTextRole::Hint,
		0.82f * Scale);
	DrawLeftAlignedText(
		NSLOCTEXT("IGHUD", "KeyBindingsTitle", "키 설정"),
		FVector2D(Metrics.ContentLeft, Metrics.TitleTop + 26.0f * Scale),
		IGHorrorHUD::SettingsPrimary,
		EIGHudTextRole::Prompt,
		1.05f * Scale);

	// 시점 셋. 동사 목록보다 위에 둔다 — 1인칭에서 손에 가장 먼저 걸리는
	// 것이 감도이고, 여기서 못 맞추면 그 뒤의 어떤 키 배열도 소용없다.
	const float LookRowStride = 28.0f * Scale;
	const float LookFirstY = Metrics.TitleTop + 62.0f * Scale;
	const float ValueX = Metrics.ContentLeft + 300.0f * Scale;
	// 동사 이름 칸. 번역이 길면 옆 칸을 덮기 전에 줄인다.
	const float LabelColumnWidth = ValueX - Metrics.ContentLeft - 20.0f * Scale;
	FNumberFormattingOptions TwoDecimals;
	TwoDecimals.MinimumFractionalDigits = 2;
	TwoDecimals.MaximumFractionalDigits = 2;
	for (int32 Row = 0; Row < UIGInputBindingSubsystem::LookRowCount; ++Row)
	{
		const bool bSelected = Row == SystemMenuKeyBindingSelection;
		const float RowY = LookFirstY + Row * LookRowStride;
		FText Label;
		FText Value;
		switch (Row)
		{
		case 0:
			Label = NSLOCTEXT("IGHUD", "LookMouse", "마우스 감도");
			Value = FText::AsNumber(Bindings->GetMouseSensitivity(), &TwoDecimals);
			break;
		case 1:
			Label = NSLOCTEXT("IGHUD", "LookPad", "패드 감도");
			Value = FText::AsNumber(Bindings->GetGamepadSensitivity(), &TwoDecimals);
			break;
		case 2:
			Label = NSLOCTEXT("IGHUD", "LookVertical", "세로 감도 배율");
			Value = FText::AsNumber(Bindings->GetVerticalLookScale(), &TwoDecimals);
			break;
		default:
			Label = NSLOCTEXT("IGHUD", "LookInvert", "상하 반전");
			Value = Bindings->IsLookInverted()
				? (NSLOCTEXT("IGHUD", "LookInvertOn", "켬"))
				: (NSLOCTEXT("IGHUD", "LookInvertOff", "끔"));
			break;
		}
		DrawLeftAlignedText(
			bSelected ? FText::FromString(TEXT(">")) : FText::GetEmpty(),
			FVector2D(Metrics.ContentLeft - 16.0f * Scale, RowY),
			IGHorrorHUD::SettingsAccent,
			EIGHudTextRole::Hint,
			1.05f * Scale);
		DrawLeftAlignedText(
			Label,
			FVector2D(Metrics.ContentLeft, RowY),
			bSelected ? IGHorrorHUD::SettingsPrimary : IGHorrorHUD::SettingsSecondary,
			EIGHudTextRole::Hint,
			GetFittedTextScale(
				Label, EIGHudTextRole::Hint, 1.05f * Scale, LabelColumnWidth, 0.7f * Scale));
		// 고른 행에만 꺾쇠를 붙여 좌우로 움직이는 행임을 알린다.
		DrawLeftAlignedText(
			bSelected
				? FText::Format(
					NSLOCTEXT("IGHUD", "LookValueSelected", "‹ {0} ›"),
					Value)
				: Value,
			FVector2D(ValueX, RowY),
			bSelected ? IGHorrorHUD::SettingsPrimary : IGHorrorHUD::SettingsSecondary,
			EIGHudTextRole::Hint,
			1.05f * Scale);
	}

	// 열 머리글. 지금 고른 칸을 밝게 둔다 — 색만으로 알리지 않기 위해
	// 고른 칸에는 꺾쇠를 함께 그린다(§24 즉시 차단 22).
	const float ColumnKeyboardX = Metrics.ContentLeft + 300.0f * Scale;
	const float ColumnGamepadX = Metrics.ContentLeft + 470.0f * Scale;
	// 칸 폭. 번역된 키 이름이나 입력 대기 문구가 옆 칸을 덮지 않게 한다.
	const float KeyboardColumnWidth = ColumnGamepadX - ColumnKeyboardX - 14.0f * Scale;
	const float GamepadColumnWidth = FMath::Max(
		KeyboardColumnWidth,
		Canvas->ClipX - Metrics.ContentLeft - ColumnGamepadX);
	const float HeaderY = LookFirstY
		+ UIGInputBindingSubsystem::LookRowCount * LookRowStride
		+ 16.0f * Scale;
	const FText KeyboardHeader =
		MakeBindingColumnHeader(false, bSystemMenuKeyBindingColumnGamepad);
	const FText GamepadHeader =
		MakeBindingColumnHeader(true, bSystemMenuKeyBindingColumnGamepad);
	DrawLeftAlignedText(
		KeyboardHeader,
		FVector2D(ColumnKeyboardX, HeaderY),
		bSystemMenuKeyBindingColumnGamepad
			? IGHorrorHUD::SettingsSecondary
			: IGHorrorHUD::SettingsAccent,
		EIGHudTextRole::Hint,
		GetFittedTextScale(
			KeyboardHeader, EIGHudTextRole::Hint, 0.84f * Scale, KeyboardColumnWidth, 0.6f * Scale));
	DrawLeftAlignedText(
		GamepadHeader,
		FVector2D(ColumnGamepadX, HeaderY),
		bSystemMenuKeyBindingColumnGamepad
			? IGHorrorHUD::SettingsAccent
			: IGHorrorHUD::SettingsSecondary,
		EIGHudTextRole::Hint,
		GetFittedTextScale(
			GamepadHeader, EIGHudTextRole::Hint, 0.84f * Scale, GamepadColumnWidth, 0.6f * Scale));

	const int32 ActionCount = UIGInputBindingSubsystem::GetActionCount();
	const float RowStride = 30.0f * Scale;
	const float FirstRowY = HeaderY + 26.0f * Scale;
	for (int32 Row = 0; Row < ActionCount; ++Row)
	{
		const FIGBindableActionInfo& Info =
			UIGInputBindingSubsystem::GetActionInfo(Row);
		const bool bSelected =
			Row + UIGInputBindingSubsystem::LookRowCount
				== SystemMenuKeyBindingSelection;
		const float RowY = FirstRowY + Row * RowStride;
		// 선택 표시는 색이 아니라 모양이다.
		DrawLeftAlignedText(
			bSelected
				? FText::FromString(TEXT(">"))
				: FText::GetEmpty(),
			FVector2D(Metrics.ContentLeft - 16.0f * Scale, RowY),
			IGHorrorHUD::SettingsAccent,
			EIGHudTextRole::Hint,
			1.05f * Scale);
		DrawLeftAlignedText(
			Info.Label,
			FVector2D(Metrics.ContentLeft, RowY),
			bSelected ? IGHorrorHUD::SettingsPrimary : IGHorrorHUD::SettingsSecondary,
			EIGHudTextRole::Hint,
			GetFittedTextScale(
				Info.Label, EIGHudTextRole::Hint, 1.05f * Scale, LabelColumnWidth, 0.7f * Scale));

		for (int32 Column = 0; Column < 2; ++Column)
		{
			const bool bGamepadColumn = Column == 1;
			const FKey Bound = Bindings->GetBoundKey(Row, bGamepadColumn);
			const bool bCapturingHere = bSystemMenuKeyBindingCapturing
				&& bSelected
				&& bGamepadColumn == bSystemMenuKeyBindingColumnGamepad;
			FText Shown;
			if (bCapturingHere)
			{
				Shown = NSLOCTEXT("IGHUD", "KeyBindingsPressNow", "[ 누르세요 ]");
			}
			else if (!Bound.IsValid() && !bGamepadColumn
				&& Row == static_cast<int32>(EIGBindableAction::Listen))
			{
				Shown = FText::Format(
					NSLOCTEXT("IGHUD", "KeyBindingsListenHold", "{0} 길게"),
					GetBoundKeyLabel(EIGBindableAction::Interact, false));
			}
			else if (!Bound.IsValid())
			{
				// 빈 칸은 「없음」이라고 적는다. 비워 두면 고장으로 읽힌다.
				Shown = NSLOCTEXT("IGHUD", "KeyBindingsNone", "없음");
			}
			else
			{
				Shown = GetBoundKeyLabel(static_cast<EIGBindableAction>(Row), bGamepadColumn);
			}
			const bool bOverridden = !Bindings->IsDefaultBinding(Row, bGamepadColumn);
			const FText CellText = bOverridden
				? FText::Format(
					NSLOCTEXT("IGHUD", "KeyBindingsChanged", "{0} *"),
					Shown)
				: Shown;
			DrawLeftAlignedText(
				CellText,
				FVector2D(bGamepadColumn ? ColumnGamepadX : ColumnKeyboardX, RowY),
				bCapturingHere
					? IGHorrorHUD::SettingsAccent
					: bSelected
						? IGHorrorHUD::SettingsPrimary
						: IGHorrorHUD::SettingsSecondary,
				EIGHudTextRole::Hint,
				GetFittedTextScale(
					CellText,
					EIGHudTextRole::Hint,
					1.05f * Scale,
					bGamepadColumn ? GamepadColumnWidth : KeyboardColumnWidth,
					0.7f * Scale));
		}
	}

	// 마지막 행: 전부 기본값으로.
	const int32 ResetRow =
		ActionCount + UIGInputBindingSubsystem::LookRowCount;
	const float ResetY = FirstRowY + ActionCount * RowStride + 8.0f * Scale;
	const bool bResetSelected = SystemMenuKeyBindingSelection == ResetRow;
	DrawLeftAlignedText(
		bResetSelected ? FText::FromString(TEXT(">")) : FText::GetEmpty(),
		FVector2D(Metrics.ContentLeft - 16.0f * Scale, ResetY),
		IGHorrorHUD::SettingsAccent,
		EIGHudTextRole::Hint,
		1.05f * Scale);
	DrawLeftAlignedText(
		NSLOCTEXT("IGHUD", "KeyBindingsResetRow", "전부 기본값으로"),
		FVector2D(Metrics.ContentLeft, ResetY),
		bResetSelected ? IGHorrorHUD::SettingsPrimary : IGHorrorHUD::SettingsSecondary,
		EIGHudTextRole::Hint,
		1.05f * Scale);

	// 고른 행의 설명. §18.1이 그 동사에 대해 말하는 것을 그대로 보여 준다.
	const float DetailY = ResetY + 34.0f * Scale;
	// 설명 줄은 화면 양쪽 여백 안에 맞춘다.
	const float DetailWidth = Canvas->ClipX - Metrics.ContentLeft * 2.0f;
	auto DrawDetailLine = [this, DetailWidth, &Metrics](
		const FText& Text,
		const float Y,
		const FLinearColor& Color,
		const float PreferredScale)
	{
		DrawLeftAlignedText(
			Text,
			FVector2D(Metrics.ContentLeft, Y),
			Color,
			EIGHudTextRole::Hint,
			GetFittedTextScale(
				Text, EIGHudTextRole::Hint, PreferredScale, DetailWidth, PreferredScale * 0.7f));
	};
	const int32 DetailAction =
		SystemMenuKeyBindingSelection - UIGInputBindingSubsystem::LookRowCount;
	if (SystemMenuKeyBindingSelection < UIGInputBindingSubsystem::LookRowCount)
	{
		DrawDetailLine(
			NSLOCTEXT("IGHUD", "LookDetail", "마우스와 게임패드 감도는 따로 조절합니다. 세로 감도 배율을 높이면 위아래로 더 빠르게 움직입니다."),
			DetailY,
			IGHorrorHUD::SettingsSecondary,
			0.82f * Scale);
	}
	else if (DetailAction >= 0 && DetailAction < ActionCount)
	{
		DrawDetailLine(
			UIGInputBindingSubsystem::GetActionInfo(DetailAction).Description,
			DetailY,
			IGHorrorHUD::SettingsSecondary,
			0.82f * Scale);
	}
	if (!SystemMenuKeyBindingStatus.IsEmpty())
	{
		DrawDetailLine(
			SystemMenuKeyBindingStatus,
			DetailY + 24.0f * Scale,
			bSystemMenuKeyBindingStatusIsError
				? IGHorrorHUD::SettingsAccent
				: IGHorrorHUD::SettingsSuccess,
			0.82f * Scale);
	}

	DrawDetailLine(
		NSLOCTEXT("IGHUD", "KeyBindingsFixedNote", "Esc(일시정지)와 F10(접근성 설정)은 바꿀 수 없습니다. 패드 버튼은 Xbox 기준입니다."),
		DetailY + 48.0f * Scale,
		IGHorrorHUD::SettingsSecondary,
		0.78f * Scale);

	DrawLeftAlignedText(
		bPadHints
				? NSLOCTEXT(
					"IGHUD",
					"KeyBindingsControlsPad",
					"십자키 ↑↓ 항목 이동 · ←→ 값 변경/장치 선택 · A 바꾸기 · B 돌아가기")
				: NSLOCTEXT(
					"IGHUD",
					"KeyBindingsControlsKeys",
					"↑↓ 항목 이동 · ←→ 값 변경/장치 선택 · Enter 바꾸기 · Esc 돌아가기"),
		FVector2D(Metrics.ContentLeft, Metrics.FooterTop),
		IGHorrorHUD::SettingsSecondary,
		EIGHudTextRole::Hint,
		0.84f * Scale);
	RecordLayoutValidationRect(
		FVector2D(Metrics.ContentLeft, Metrics.TitleTop),
		FVector2D(
			Metrics.ContentLeft + Metrics.ContentWidth,
			FMath::Min(Metrics.FooterTop + 24.0f, Canvas->ClipY)));
}

void AIGHorrorHUD::DrawSystemMenuPanel()
{
	if (!Canvas)
	{
		return;
	}

	// §9 「밤 5」. 로딩 없이 검정 화면. 타이틀 키아트도 메뉴 행도 그리지 않고,
	// 비언어음 자막 레인만 남긴다 — 30초 동안 화면에 있는 것은 그것뿐이다.
	// 여기서 검정을 직접 칠하는 이유는 타이틀에는 페이드할 카메라가 없다는 것.
	if (bSystemMenuNightFivePlaying)
	{
		FCanvasTileItem NightScrim(
			FVector2D::ZeroVector,
			FVector2D(Canvas->ClipX, Canvas->ClipY),
			FLinearColor::Black);
		NightScrim.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(NightScrim);
		DrawAudioCaption(AudioCaptionClockSeconds, Canvas->ClipY - 24.0f);
		return;
	}
	if (bSystemMenuIsKeyBindings)
	{
		FCanvasTileItem BindingScrim(
			FVector2D::ZeroVector,
			FVector2D(Canvas->ClipX, Canvas->ClipY),
			FLinearColor(0.004f, 0.006f, 0.007f, 0.985f));
		BindingScrim.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(BindingScrim);
		DrawKeyBindingsPanel();
		return;
	}
	if (bSystemMenuIsAudioCalibration || bSystemMenuIsDisplaySettings)
	{
		// These two tools own their complete visual hierarchy and deliberately
		// avoid inheriting the title screen's floor-datum ornament.
		FCanvasTileItem ToolScrim(
			FVector2D::ZeroVector,
			FVector2D(Canvas->ClipX, Canvas->ClipY),
			FLinearColor(0.004f, 0.006f, 0.007f, 0.985f));
		ToolScrim.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(ToolScrim);
		if (bSystemMenuIsAudioCalibration)
		{
			DrawAudioCalibrationPanel();
		}
		else
		{
			DrawDisplaySettingsPanel();
		}
		return;
	}

	const IGFrontendMenuLayout::FMetrics Metrics =
		IGFrontendMenuLayout::MakeMetrics(Canvas->ClipX, Canvas->ClipY);
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	const bool bReducedMotion = Accessibility
		&& Accessibility->IsReducedCameraMotionEnabled();
	const double Now = FPlatformTime::Seconds();
	const float EntranceAlpha = bReducedMotion || SystemMenuOpenedAt < 0.0
		? 1.0f
		: IGHorrorHUD::SmoothStep01(static_cast<float>(
			(Now - SystemMenuOpenedAt) / 0.32));

	auto WithAlpha = [EntranceAlpha](FLinearColor Color, const float Multiplier = 1.0f)
	{
		Color.A *= EntranceAlpha * Multiplier;
		return Color;
	};

	// The title uses one text-free environmental still. Other menu modes retain
	// the frozen playfield so pausing never destroys the player's spatial memory.
	if (bSystemMenuUseTitleBackdrop)
	{
		FCanvasTileItem Base(
			FVector2D::ZeroVector,
			FVector2D(Canvas->ClipX, Canvas->ClipY),
			IGHorrorHUD::FrontendInk);
		Canvas->DrawItem(Base);
		if (FrontendTitleBackgroundTexture
			&& FrontendTitleBackgroundTexture->GetResource())
		{
			const float ScreenAspect = Canvas->ClipX / FMath::Max(Canvas->ClipY, 1.0f);
			constexpr float SourceAspect = 16.0f / 9.0f;
			FVector2D Uv0(0.0f, 0.0f);
			FVector2D Uv1(1.0f, 1.0f);
			if (ScreenAspect < SourceAspect)
			{
				const float VisibleWidth = ScreenAspect / SourceAspect;
				const float LeftBias = (1.0f - VisibleWidth) * 0.20f;
				Uv0.X = LeftBias;
				Uv1.X = LeftBias + VisibleWidth;
			}
			else if (ScreenAspect > SourceAspect)
			{
				const float VisibleHeight = SourceAspect / ScreenAspect;
				Uv0.Y = (1.0f - VisibleHeight) * 0.5f;
				Uv1.Y = Uv0.Y + VisibleHeight;
			}

			FCanvasTileItem KeyArt(
				FVector2D::ZeroVector,
				FrontendTitleBackgroundTexture->GetResource(),
				FVector2D(Canvas->ClipX, Canvas->ClipY),
				Uv0,
				Uv1,
				FLinearColor(1.0f, 1.0f, 1.0f, EntranceAlpha));
			KeyArt.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(KeyArt);
		}

		FCanvasTileItem ArtTint(
			FVector2D::ZeroVector,
			FVector2D(Canvas->ClipX, Canvas->ClipY),
			FLinearColor(0.01f, 0.014f, 0.015f, 0.16f));
		ArtTint.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(ArtTint);
	}
	else
	{
		FCanvasTileItem PauseScrim(
			FVector2D::ZeroVector,
			FVector2D(Canvas->ClipX, Canvas->ClipY),
			FLinearColor(0.008f, 0.011f, 0.012f, 0.68f));
		PauseScrim.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(PauseScrim);
	}

	// A single bilinear alpha ramp protects legibility without the visible bands
	// produced by overlapping rectangles or the cost of a blur/retainer pass.
	if (FrontendShadeTexture && FrontendShadeTexture->GetResource())
	{
		FCanvasTileItem LeftShade(
			FVector2D::ZeroVector,
			FrontendShadeTexture->GetResource(),
			FVector2D(Canvas->ClipX * 0.64f, Canvas->ClipY),
			FLinearColor(
				1.0f,
				1.0f,
				1.0f,
				bSystemMenuUseTitleBackdrop ? 0.68f : 0.58f));
		LeftShade.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(LeftShade);
	}
	else
	{
		FCanvasTileItem LeftShadeFallback(
			FVector2D::ZeroVector,
			FVector2D(Canvas->ClipX * 0.48f, Canvas->ClipY),
			FLinearColor(0.006f, 0.009f, 0.010f, 0.38f));
		LeftShadeFallback.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(LeftShadeFallback);
	}

	const float SupportScale = FMath::Max(0.90f, Metrics.Scale);
	const float TitleScale = FMath::Max(0.76f, Metrics.Scale);
	auto DrawDisplayTitle = [
		this,
		&WithAlpha](
			const FText& Text,
			const FVector2D& Position,
			const float Scale)
	{
		UFont* Font = KoreanFrontendTitleFont
			? KoreanFrontendTitleFont.Get()
			: GetFontForRole(EIGHudTextRole::Objective);
		if (!Canvas || !Font || Text.IsEmpty())
		{
			return 0.0f;
		}
		float EffectiveScale = KoreanFrontendTitleFont
			? Scale
			: Scale * 2.45f;
		float Width = 0.0f;
		float Height = 0.0f;
		Canvas->StrLen(Font, Text.ToString(), Width, Height, true);
		// 영문 제목도 작은 창에서 메뉴 영역 안에 들어오게 한다.
		EffectiveScale = FMath::Min(EffectiveScale,
			(Canvas->ClipX * 0.48f - Position.X) / FMath::Max(1.0f, Width));
		FCanvasTextItem Item(
			Position,
			Text,
			Font,
			WithAlpha(IGHorrorHUD::FrontendIvory));
		Item.Scale = FVector2D(EffectiveScale);
		Item.EnableShadow(
			WithAlpha(FLinearColor(0.0f, 0.0f, 0.0f, 0.82f)),
			FVector2D(1.0f, 2.0f));
		if (bLayoutValidationEnabled)
		{
			RecordLayoutValidationRect(
				Position,
				Position + FVector2D(
					Width * EffectiveScale,
					Height * EffectiveScale));
		}
		Canvas->DrawItem(Item);
		return Height * EffectiveScale;
	};

	const FVector2D HeaderOrigin(Metrics.ContentLeft, Metrics.TitleTop);
	if (bSystemMenuIsContentNotice)
	{
		// 일반 경고는 사용자가 아니라 책임을 보호한다. 그래서 「점멸이
		// 있습니다」로 끝내지 않고 **무엇이 나오는지**와 **그것을 어느
		// 설정으로 줄일 수 있는지**를 같은 화면에서 말한다.
		DrawLeftAlignedText(
			NSLOCTEXT("IGHUD", "NoticeContext", "플레이 전에"),
			HeaderOrigin,
			WithAlpha(IGHorrorHUD::FrontendMuted),
			EIGHudTextRole::Hint,
			0.82f * SupportScale);
		const FVector2D NoticeTitleOrigin =
			HeaderOrigin + FVector2D(0.0f, 24.0f * Metrics.Scale);
		DrawDisplayTitle(
			NSLOCTEXT("IGHUD", "NoticeTitle", "플레이 안내"),
			NoticeTitleOrigin,
			0.78f * TitleScale);

		struct FNoticeLine
		{
			FText Text;
			bool bHeading;
		};
		const FNoticeLine NoticeLines[] =
		{
			{NSLOCTEXT("IGHUD", "NoticeSensory", "화면과 소리"), true},
			{NSLOCTEXT("IGHUD", "NoticeSensory1", "어두운 곳에서 무언가 갑자기 다가오거나 큰 소리가 날 수 있습니다. 화면 흔들림과 조명 깜빡임도 있습니다."), false},
			{NSLOCTEXT("IGHUD", "NoticeSensory2", "화면이나 소리가 불편하면 잠시 쉬거나 설정을 조절해 주세요."), false},
			{NSLOCTEXT("IGHUD", "NoticeThemes", "다루는 내용"), true},
			{NSLOCTEXT("IGHUD", "NoticeThemes1", "시신이 나오는 장면과, 사람이 죽어 가는"
					" 소리를 듣는 구간이 있습니다."), false},
			{NSLOCTEXT("IGHUD", "NoticeThemes2", "층간소음 분쟁과 불법 증축, 실종 사건을 다루는 허구의 이야기입니다."), false},
			{NSLOCTEXT("IGHUD", "NoticeControls", "불편하다면"), true},
			{NSLOCTEXT("IGHUD", "NoticeControls1", "접근성 설정에서 화면 흔들림과 빛 깜빡임을 줄일 수 있습니다."), false},
			{NSLOCTEXT("IGHUD", "NoticeControls2", "쫓기는 게 부담스럽다면 난이도를 ‘추격 없음’으로 바꿔 주세요."
					" 어느 난이도에서도 모든 결말을 볼 수 있습니다."), false},
		};

		// 번역은 한국어보다 길다. 왼쪽 그늘 안에서 줄을 바꾼다.
		const float NoticeWidth = FMath::Max(
			Metrics.ContentWidth,
			Canvas->ClipX * 0.58f - Metrics.ContentLeft);
		float NoticePenY = Metrics.MenuTop - 18.0f * SupportScale;
		for (const FNoticeLine& Line : NoticeLines)
		{
			if (Line.bHeading)
			{
				NoticePenY += 10.0f * SupportScale;
			}
			const EIGHudTextRole LineRole =
				Line.bHeading ? EIGHudTextRole::Prompt : EIGHudTextRole::Hint;
			const float LineScale = (Line.bHeading ? 0.94f : 0.88f) * SupportScale;
			TArray<FString> Wrapped;
			FString Overflow;
			WrapHudText(
				Line.Text.ToString(),
				GetFontForRole(LineRole),
				LineScale,
				NoticeWidth,
				3,
				Wrapped,
				Overflow);
			if (Wrapped.IsEmpty())
			{
				Wrapped.Add(Line.Text.ToString());
			}
			for (const FString& Piece : Wrapped)
			{
				DrawLeftAlignedText(
					FText::FromString(Piece),
					FVector2D(Metrics.ContentLeft, NoticePenY),
					WithAlpha(
						Line.bHeading
							? IGHorrorHUD::FrontendIvory
							: IGHorrorHUD::FrontendMuted),
					LineRole,
					LineScale);
				NoticePenY += (Line.bHeading ? 30.0f : 25.0f) * SupportScale;
			}
			if (!Line.bHeading)
			{
				NoticePenY += 2.0f * SupportScale;
			}
		}

		DrawLeftAlignedText(
			bUsingGamepad
					? NSLOCTEXT(
						"IGHUD", "NoticeControlsGamepad",
						"A  계속  ·  Y  접근성 설정 열기")
					: NSLOCTEXT(
						"IGHUD", "NoticeControlsKeyboard",
						"Enter  계속  ·  F10  접근성 설정 열기"),
			FVector2D(Metrics.ContentLeft, Metrics.FooterTop),
			WithAlpha(IGHorrorHUD::FrontendIvory),
			EIGHudTextRole::Hint,
			0.86f * SupportScale);
		RecordLayoutValidationRect(
			FVector2D(Metrics.ContentLeft, HeaderOrigin.Y),
			FVector2D(
				Metrics.ContentLeft + Metrics.ContentWidth,
				FMath::Min(Metrics.FooterTop + 24.0f, Canvas->ClipY)));
		return;
	}
	if (bSystemMenuIsCredits)
	{
		DrawLeftAlignedText(
			NSLOCTEXT("IGHUD", "CreditsContext", "제작 정보 · 2026"),
			HeaderOrigin,
			WithAlpha(IGHorrorHUD::FrontendMuted),
			EIGHudTextRole::Hint,
			0.82f * SupportScale);
		const FVector2D CreditsTitleOrigin =
			HeaderOrigin + FVector2D(0.0f, 24.0f * Metrics.Scale);
		const float CreditsTitleHeight = DrawDisplayTitle(
			NSLOCTEXT("IGHUD", "CreditsTitle", "만든 사람"),
			CreditsTitleOrigin,
			0.78f * TitleScale);
		DrawLeftAlignedText(
			NSLOCTEXT("IGHUD", "CreditsGameTitle", "Missing Floor"),
			FVector2D(
				HeaderOrigin.X + 2.0f,
				CreditsTitleOrigin.Y
					+ CreditsTitleHeight
					+ 2.0f * Metrics.Scale),
			WithAlpha(IGHorrorHUD::FrontendMuted),
			EIGHudTextRole::Hint,
			0.82f * SupportScale);

		const FText CreditLines[] =
		{
			NSLOCTEXT("IGHUD", "CreditsDeveloper", "기획 · 개발    easygap"),
			NSLOCTEXT("IGHUD", "CreditsEngine", "제작 도구    Unreal Engine 5.8"),
			NSLOCTEXT("IGHUD", "CreditsMaterials", "일부 재질    ambientCG · CC0"),
			NSLOCTEXT("IGHUD", "CreditsProps", "일부 소품    Poly Haven · CC0"),
			NSLOCTEXT("IGHUD", "CreditsSounds", "일부 소리    OpenGameArt · Kenney · Owlish Media · CC0"),
			NSLOCTEXT("IGHUD", "CreditsFonts", "글꼴    Pretendard · 고운바탕 · SIL OFL"),
			FText::FromString(TEXT("Copyright 2026 easygap. All rights reserved."))
		};
		for (int32 Line = 0; Line < UE_ARRAY_COUNT(CreditLines); ++Line)
		{
			DrawLeftAlignedText(
				CreditLines[Line],
				FVector2D(
					Metrics.ContentLeft,
					Metrics.MenuTop + Line * 34.0f * SupportScale),
				WithAlpha(
					Line == 0
						? IGHorrorHUD::FrontendIvory
						: IGHorrorHUD::FrontendMuted),
				Line == 0
					? EIGHudTextRole::Prompt
					: EIGHudTextRole::Hint,
				Line == 0
					? SupportScale
					: SupportScale * 0.94f);
		}
		DrawLeftAlignedText(
			bUsingGamepad
					? NSLOCTEXT("IGHUD", "CreditsBackGamepad", "B  돌아가기")
					: NSLOCTEXT("IGHUD", "CreditsBackKeyboard", "Esc 또는 Enter  돌아가기"),
			FVector2D(Metrics.ContentLeft, Metrics.FooterTop),
			WithAlpha(IGHorrorHUD::FrontendMuted),
			EIGHudTextRole::Hint,
			0.86f * SupportScale);
		return;
	}

	DrawLeftAlignedText(
		bSystemMenuIsTitle
			? FText::GetEmpty()
			: NSLOCTEXT("IGHUD", "PauseContext", "메뉴"),
		HeaderOrigin,
		WithAlpha(IGHorrorHUD::FrontendMuted),
		EIGHudTextRole::Hint,
		0.82f * SupportScale);
	const FVector2D MainTitleOrigin =
		HeaderOrigin + FVector2D(0.0f, 24.0f * Metrics.Scale);
	const float MainTitleHeight = DrawDisplayTitle(
		bSystemMenuIsTitle
			? NSLOCTEXT("IGHUD", "MainTitle", "Missing Floor")
			: NSLOCTEXT("IGHUD", "PauseTitle", "잠시 멈춤"),
		MainTitleOrigin,
		TitleScale);
	if (!bSystemMenuIsTitle)
	{
		DrawLeftAlignedText(
			NSLOCTEXT("IGHUD", "PauseSubtitle", "진행 중"),
			FVector2D(HeaderOrigin.X + 2.0f,
				MainTitleOrigin.Y + MainTitleHeight + 2.0f * Metrics.Scale),
			WithAlpha(IGHorrorHUD::FrontendMuted),
			EIGHudTextRole::Hint,
			0.76f * SupportScale);
	}

	TArray<FString> MessageSources;
	FLinearColor MessageColor = IGHorrorHUD::FrontendIvory;
	if (bSystemMenuIsTitle && bSystemMenuHeadphoneRecommendation)
	{
		MessageSources = {
			NSLOCTEXT("IGHUD", "Menu.ThisGameIsMadeToBeHeardOnHeadphones", "헤드폰을 쓰면 소리가 나는 방향을 구분하기 쉽습니다.").ToString(),
			NSLOCTEXT("IGHUD", "Menu.PressAnyKeyToSkip", "아무 키를 누르면 건너뜁니다.").ToString()
		};
		MessageColor = IGHorrorHUD::ThoughtBlue;
	}
	else if (bSystemMenuIsTitle && bSystemMenuConfirmNewGame)
	{
		MessageSources = {
			NSLOCTEXT("IGHUD", "Menu.ThisOverwritesYourAutosave", "자동 저장을 덮어씁니다.").ToString(),
			NSLOCTEXT("IGHUD", "Menu.SelectNewGameAgainToStart", "한 번 더 누르면 새 게임을 시작합니다").ToString()
		};
		MessageColor = IGHorrorHUD::FrontendOxide;
	}
	else if (!SystemMenuStatusText.IsEmpty())
	{
		MessageSources.Add(SystemMenuStatusText.ToString());
		MessageColor = bSystemMenuStatusIsError
			? IGHorrorHUD::FrontendOxide
			: IGHorrorHUD::FrontendIvory;
	}
	// 안내 문장은 메뉴 행 바로 위에 붙는다. 번역이 길면 그늘 안에서 줄을
	// 바꾸고, 늘어난 줄 수만큼 위로 올린다.
	const float MessageWidth = FMath::Max(
		Metrics.ContentWidth,
		Canvas->ClipX * 0.56f - Metrics.ContentLeft);
	TArray<FString> MessageLines;
	UFont* MessageFont = GetFontForRole(EIGHudTextRole::Hint);
	for (const FString& MessageSource : MessageSources)
	{
		TArray<FString> Wrapped;
		FString Remainder;
		if (MessageFont)
		{
			WrapHudText(
				MessageSource,
				MessageFont,
				SupportScale,
				MessageWidth,
				2,
				Wrapped,
				Remainder);
		}
		if (Wrapped.IsEmpty())
		{
			Wrapped.Add(MessageSource);
		}
		MessageLines.Append(Wrapped);
	}
	if (!MessageLines.IsEmpty())
	{
		const float MessageStep = 21.0f * SupportScale;
		const float MessageStartY = Metrics.MessageTop
			- FMath::Max(0, MessageLines.Num() - 2) * MessageStep;
		for (int32 Line = 0; Line < MessageLines.Num(); ++Line)
		{
			DrawLeftAlignedText(
				FText::FromString(MessageLines[Line]),
				FVector2D(
					Metrics.ContentLeft,
					MessageStartY + Line * MessageStep),
				WithAlpha(MessageColor),
				EIGHudTextRole::Hint,
				SupportScale);
		}
	}

	const FString TitleRows[] =
	{
		NSLOCTEXT("IGHUD", "Menu.Continue", "이어하기").ToString(),
		bSystemMenuCanContinue
			? NSLOCTEXT("IGHUD", "Menu.NewGame", "새 게임").ToString()
			: NSLOCTEXT("IGHUD", "Menu.StartGame", "게임 시작").ToString(),
		NSLOCTEXT("IGHUD", "Menu.Settings", "설정").ToString(),
		NSLOCTEXT("IGHUD", "Menu.Credits", "제작 정보").ToString(),
		NSLOCTEXT("IGHUD", "Menu.Quit", "게임 종료").ToString(),
		// §9. 있을 수 없는 슬롯. 라벨은 밤 이름 하나뿐이고 아무 설명도 달지
		// 않는다 — 발견한 사람만 아는 것이 이 30초의 전부다.
		NSLOCTEXT("IGHUD", "Menu.Night5", "다섯째 밤").ToString()
	};
	const FString PauseRows[] =
	{
		NSLOCTEXT("IGHUD", "Menu.Resume", "계속하기").ToString(),
		NSLOCTEXT("IGHUD", "Menu.LoadLatestAutosave", "최근 자동 저장 불러오기").ToString(),
		NSLOCTEXT("IGHUD", "Menu.Settings", "설정").ToString(),
		NSLOCTEXT("IGHUD", "Menu.Credits", "제작 정보").ToString(),
		NSLOCTEXT("IGHUD", "Menu.Quit", "게임 종료").ToString(),
		// 일시정지 메뉴에는 밤 5가 없다. 자리만 채운다.
		FString()
	};

	const int32 LoadRow = bSystemMenuIsTitle ? 0 : 1;
	for (int32 ActionRow = 0;
		ActionRow < IGFrontendMenuLayout::ActionCount;
		++ActionRow)
	{
		const int32 VisibleSlot =
			IGFrontendMenuLayout::GetVisibleSlotForAction(
				ActionRow,
				bSystemMenuIsTitle,
				bSystemMenuCanContinue,
				bSystemMenuNightFiveAvailable);
		if (VisibleSlot == INDEX_NONE)
		{
			continue;
		}

		const bool bEnabled =
			ActionRow != LoadRow || bSystemMenuCanContinue;
		const bool bSelected = ActionRow == SystemMenuSelectedRow;
		// §9: 한 번 재생하면 흐려진다. 사라지지는 않는다 — 다시 들을 수 있다.
		const bool bDimmed = ActionRow == IGFrontendMenuLayout::NightFiveAction
			&& bSystemMenuNightFiveSpent;
		FString Label =
			bSystemMenuIsTitle ? TitleRows[ActionRow] : PauseRows[ActionRow];
		if (bSystemMenuIsTitle
			&& ActionRow == 1
			&& bSystemMenuConfirmNewGame)
		{
			Label = NSLOCTEXT("IGHUD", "Menu.ConfirmNewGame", "새 게임 확인").ToString();
		}
		if (!bEnabled)
		{
			Label += NSLOCTEXT("IGHUD", "Menu.NoSave", "  · 저장 없음").ToString();
		}

		const FVector2D RowPosition = Metrics.GetRowPosition(VisibleSlot);

		const FLinearColor LabelColor = bEnabled
			? IGHorrorHUD::FrontendIvory
			: IGHorrorHUD::FrontendMuted;
		// 흐려진 밤 5는 여전히 선택 가능하므로 비활성 색이 아니라 밝기만 낮춘다.
		const float DisabledMultiplier =
			(bEnabled ? 1.0f : 0.52f) * (bDimmed ? 0.55f : 1.0f)
			* (bSelected ? 1.0f : 0.70f);
		const FText LabelText = FText::FromString(Label);
		const EIGHudTextRole RowRole = bSelected
			? EIGHudTextRole::Prompt
			: EIGHudTextRole::Hint;
		const float PreferredRowScale = bSelected
			? SupportScale * 1.10f
			: SupportScale;
		// 행 폭은 정해져 있다. 긴 번역은 행 안에 들어올 만큼만 줄이고,
		// 줄어든 만큼 아래로 내려 행 가운데에 둔다.
		const float RowTextScale = GetFittedTextScale(
			LabelText,
			RowRole,
			PreferredRowScale,
			Metrics.ContentWidth - 21.0f * Metrics.Scale,
			SupportScale * 0.6f);
		UFont* RowFont = GetFontForRole(RowRole);
		const float TextHeight = MeasureTextHeight(Label, RowFont, RowTextScale);
		DrawLeftAlignedText(
			LabelText,
			FVector2D(
				Metrics.ContentLeft,
				RowPosition.Y + (Metrics.RowHeight - TextHeight) * 0.5f),
			WithAlpha(LabelColor, DisabledMultiplier),
			RowRole,
			RowTextScale);

		const FBox2D HitBox = Metrics.GetRowHitBox(VisibleSlot);
		RecordLayoutValidationRect(HitBox.Min, HitBox.Max);
	}

	if (!bSystemMenuIsTitle || bSystemMenuConfirmNewGame)
	{
		const FText SystemControls = bUsingGamepad
			? bSystemMenuIsTitle
				? NSLOCTEXT("IGHUD", "TitleConfirmControlsGamepad", "A 시작  ·  View 취소")
				: NSLOCTEXT("IGHUD", "SystemMenuControlsGamepad", "D-pad 이동  ·  A 선택  ·  View 돌아가기")
			: bSystemMenuIsTitle
				? NSLOCTEXT("IGHUD", "TitleConfirmControlsKeyboard", "Enter 시작  ·  Esc 취소")
				: NSLOCTEXT("IGHUD", "SystemMenuControlsKeyboard", "↑↓ 이동  ·  Enter 선택  ·  Esc 돌아가기");
		DrawLeftAlignedText(
			SystemControls,
			FVector2D(Metrics.ContentLeft, Metrics.FooterTop),
			WithAlpha(IGHorrorHUD::FrontendMuted),
			EIGHudTextRole::Hint,
			0.84f * SupportScale);
	}
}


void AIGHorrorHUD::ResolveInteractionComponent()
{
	APawn* Pawn = GetOwningPawn();
	InteractionComponent = Pawn
		? Pawn->FindComponentByClass<UIGInteractionComponent>()
		: nullptr;
}

FText AIGHorrorHUD::GetObjectiveText() const
{
	const IIGObjectiveProvider* Provider =
		Cast<IIGObjectiveProvider>(ObjectiveProvider.Get());
	return Provider ? Provider->GetObjectiveText() : FText::GetEmpty();
}

float AIGHorrorHUD::GetObjectiveProgress() const
{
	const IIGObjectiveProvider* Provider =
		Cast<IIGObjectiveProvider>(ObjectiveProvider.Get());
	return Provider
		? FMath::Clamp(Provider->GetObjectiveProgress(), 0.0f, 1.0f)
		: 0.0f;
}

bool AIGHorrorHUD::DrawChapterCard(const double CurrentTime)
{
	if (!Canvas || ChapterCardTitle.IsEmpty() || CurrentTime >= ChapterCardEndTime)
	{
		if (CurrentTime >= ChapterCardEndTime)
		{
			ChapterCardEyebrow = FText::GetEmpty();
			ChapterCardTitle = FText::GetEmpty();
			ChapterCardSubtitle = FText::GetEmpty();
		}
		return false;
	}

	constexpr double FadeInSeconds = 0.65;
	constexpr double FadeOutSeconds = 0.8;
	const double Elapsed = CurrentTime - ChapterCardStartTime;
	const double Remaining = ChapterCardEndTime - CurrentTime;
	const float FadeAlpha = FMath::Clamp(
		static_cast<float>(FMath::Min(Elapsed / FadeInSeconds, Remaining / FadeOutSeconds)),
		0.0f,
		1.0f);
	const float SmoothAlpha = FadeAlpha * FadeAlpha * (3.0f - (2.0f * FadeAlpha));

	FCanvasTileItem Scrim(
		FVector2D::ZeroVector,
		FVector2D(Canvas->ClipX, Canvas->ClipY),
		FLinearColor(0.0f, 0.0f, 0.0f, 0.96f * SmoothAlpha));
	Scrim.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Scrim);

	FLinearColor EyebrowColor = IGHorrorHUD::MutedGray;
	EyebrowColor.A = SmoothAlpha;
	FLinearColor TitleColor = IGHorrorHUD::PaleGray;
	TitleColor.A = SmoothAlpha;
	FLinearColor SubtitleColor = IGHorrorHUD::ThoughtBlue;
	SubtitleColor.A = SmoothAlpha;

	if (!ChapterCardEyebrow.IsEmpty())
	{
		DrawCenteredText(
			ChapterCardEyebrow,
			(Canvas->ClipY * 0.5f) - 52.0f,
			EyebrowColor,
			EIGHudTextRole::Hint);
	}
	DrawCenteredText(
		ChapterCardTitle,
		(Canvas->ClipY * 0.5f) - 12.0f,
		TitleColor,
		EIGHudTextRole::Objective);
	if (!ChapterCardSubtitle.IsEmpty())
	{
		DrawCenteredText(
			ChapterCardSubtitle,
			(Canvas->ClipY * 0.5f) + 34.0f,
			SubtitleColor,
			EIGHudTextRole::Thought);
	}

	return true;
}

void AIGHorrorHUD::DrawCenteredText(
	const FText& Text,
	const float ScreenY,
	const FLinearColor& Color,
	const EIGHudTextRole TextRole,
	const float TextScale)
{
	if (!Canvas || Text.IsEmpty())
	{
		return;
	}

	UFont* Font = GetFontForRole(TextRole);
	if (!Font)
	{
		return;
	}

	FCanvasTextItem TextItem(
		FVector2D(Canvas->ClipX * 0.5f, ScreenY),
		Text,
		Font,
		Color);
	TextItem.bCentreX = true;
	TextItem.Scale = FVector2D(FMath::Max(0.5f, TextScale));
	if (bLayoutValidationEnabled || bTextAuditEnabled)
	{
		float TextWidth = 0.0f;
		float TextHeight = 0.0f;
		Canvas->StrLen(Font, Text.ToString(), TextWidth, TextHeight, true);
		const FVector2D ScaledSize(
			TextWidth * TextItem.Scale.X,
			TextHeight * TextItem.Scale.Y);
		const FVector2D TextMin((Canvas->ClipX - ScaledSize.X) * 0.5f, ScreenY);
		const FVector2D TextMax((Canvas->ClipX + ScaledSize.X) * 0.5f, ScreenY + ScaledSize.Y);
		RecordLayoutValidationRect(TextMin, TextMax);
		RecordTextAudit(Text.ToString(), TextMin, TextMax);
	}

	// A one-pixel outline keeps small Hangul legible on bright surfaces;
	// larger text reads better with a soft drop shadow instead.
	FLinearColor EffectColor = IGHorrorHUD::Shadow;
	EffectColor.A *= Color.A;
	if (TextRole == EIGHudTextRole::Hint || TextRole == EIGHudTextRole::Prompt)
	{
		TextItem.bOutlined = true;
		TextItem.OutlineColor = EffectColor;
	}
	else
	{
		TextItem.EnableShadow(EffectColor, FVector2D(1.0f, 1.0f));
	}

	Canvas->DrawItem(TextItem);
}

void AIGHorrorHUD::DrawLeftAlignedText(
	const FText& Text,
	const FVector2D& Position,
	const FLinearColor& Color,
	const EIGHudTextRole TextRole,
	const float TextScale,
	const bool bUseOutline)
{
	if (!Canvas || Text.IsEmpty())
	{
		return;
	}
	UFont* Font = GetFontForRole(TextRole);
	if (!Font)
	{
		return;
	}

	const float SafeTextScale = FMath::Max(0.5f, TextScale);
	FCanvasTextItem TextItem(Position, Text, Font, Color);
	TextItem.Scale = FVector2D(SafeTextScale);
	FLinearColor EffectColor = IGHorrorHUD::Shadow;
	EffectColor.A *= Color.A;
	if (bUseOutline)
	{
		TextItem.bOutlined = true;
		TextItem.OutlineColor = EffectColor;
	}
	else
	{
		TextItem.EnableShadow(EffectColor, FVector2D(1.0f, 1.0f));
	}

	if (bLayoutValidationEnabled || bTextAuditEnabled)
	{
		float TextWidth = 0.0f;
		float TextHeight = 0.0f;
		Canvas->StrLen(Font, Text.ToString(), TextWidth, TextHeight, true);
		const FVector2D TextMax = Position + FVector2D(
			TextWidth * SafeTextScale,
			TextHeight * SafeTextScale);
		RecordLayoutValidationRect(Position, TextMax);
		RecordTextAudit(Text.ToString(), Position, TextMax);
	}
	Canvas->DrawItem(TextItem);
}

void AIGHorrorHUD::DrawRightAlignedText(
	const FText& Text,
	const FVector2D& Position,
	const FLinearColor& Color,
	const EIGHudTextRole TextRole,
	const float TextScale,
	const bool bUseOutline)
{
	if (!Canvas || Text.IsEmpty())
	{
		return;
	}
	UFont* Font = GetFontForRole(TextRole);
	if (!Font)
	{
		return;
	}

	const float SafeTextScale = FMath::Max(0.5f, TextScale);
	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	Canvas->StrLen(Font, Text.ToString(), TextWidth, TextHeight, true);
	const FVector2D DrawPosition(
		Position.X - TextWidth * SafeTextScale,
		Position.Y);
	FCanvasTextItem TextItem(DrawPosition, Text, Font, Color);
	TextItem.Scale = FVector2D(SafeTextScale);
	FLinearColor EffectColor = IGHorrorHUD::Shadow;
	EffectColor.A *= Color.A;
	if (bUseOutline)
	{
		TextItem.bOutlined = true;
		TextItem.OutlineColor = EffectColor;
	}
	else
	{
		TextItem.EnableShadow(EffectColor, FVector2D(1.0f, 1.0f));
	}

	if (bLayoutValidationEnabled)
	{
		const FVector2D Size(
			TextWidth * SafeTextScale,
			TextHeight * SafeTextScale);
		RecordLayoutValidationRect(
			DrawPosition,
			DrawPosition + Size);
	}
	RecordTextAudit(
		Text.ToString(),
		DrawPosition,
		DrawPosition + FVector2D(TextWidth, TextHeight) * SafeTextScale);
	Canvas->DrawItem(TextItem);
}

void AIGHorrorHUD::DrawCenterDot(const bool bFocused)
{
	if (!Canvas)
	{
		return;
	}
	const UIGAccessibilitySubsystem* Accessibility = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	const bool bAlways = Accessibility && Accessibility->GetSettings().bAlwaysShowCenterDot;
	if (!bFocused && !bAlways)
	{
		// 겨눈 것이 없으면 화면 가운데는 비워 둔다. 총 조준선 같은 괄호가
		// 늘 떠 있으면 복도가 게임 화면처럼 보인다.
		return;
	}
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const float Size = FMath::Max(3.0f, Canvas->ClipY / 360.0f);
	const FLinearColor Color = bFocused ? IGHorrorHUD::RedAccent : IGHorrorHUD::PaleGray;
	DrawRect(IGHorrorHUD::Shadow, CenterX - Size * 0.5f + 1.0f, CenterY - Size * 0.5f + 1.0f, Size, Size);
	DrawRect(Color, CenterX - Size * 0.5f, CenterY - Size * 0.5f, Size, Size);
}

void AIGHorrorHUD::UpdateFocusBracket(AActor* FocusedActor, const float DeltaSeconds)
{
	if (!Canvas)
	{
		return;
	}

	if (!IsValid(FocusedActor))
	{
		FocusBracketTarget.Reset();
		FocusBracketAcquireElapsed = 0.0f;
		FocusBracketAlpha = FMath::FInterpTo(FocusBracketAlpha, 0.0f, DeltaSeconds, 12.0f);
		return;
	}
	if (FocusBracketTarget.Get() != FocusedActor)
	{
		FocusBracketTarget = FocusedActor;
		FocusBracketAcquireElapsed = 0.0f;
		FocusBracketAlpha = 0.0f;
	}
	FocusBracketAcquireElapsed += FMath::Max(0.0f, DeltaSeconds);

	// Project the focused actor's bounds and fit a box around them, so the
	// reticle visibly grabs the object instead of just recolouring a dot.
	FVector Origin = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	FocusedActor->GetActorBounds(true, Origin, Extent);

	FVector2D ScreenMin(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D ScreenMax(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	bool bAnyCornerVisible = false;
	for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
	{
		const FVector Corner(
			Origin.X + ((CornerIndex & 1) ? Extent.X : -Extent.X),
			Origin.Y + ((CornerIndex & 2) ? Extent.Y : -Extent.Y),
			Origin.Z + ((CornerIndex & 4) ? Extent.Z : -Extent.Z));
		const FVector Projected = Canvas->Project(Corner);
		if (Projected.Z <= 0.0f)
		{
			continue; // behind the camera
		}
		bAnyCornerVisible = true;
		ScreenMin.X = FMath::Min(ScreenMin.X, Projected.X);
		ScreenMin.Y = FMath::Min(ScreenMin.Y, Projected.Y);
		ScreenMax.X = FMath::Max(ScreenMax.X, Projected.X);
		ScreenMax.Y = FMath::Max(ScreenMax.Y, Projected.Y);
	}

	if (!bAnyCornerVisible)
	{
		FocusBracketAlpha = FMath::FInterpTo(FocusBracketAlpha, 0.0f, DeltaSeconds, 12.0f);
		return;
	}

	// Keep the bracket readable: never smaller than the crosshair, never so
	// large that it frames the whole screen.
	const FVector2D Center = (ScreenMin + ScreenMax) * 0.5f;
	FVector2D HalfSize = (ScreenMax - ScreenMin) * 0.5f + FVector2D(6.0f, 6.0f);
	HalfSize.X = FMath::Clamp(HalfSize.X, 14.0f, Canvas->ClipX * 0.30f);
	HalfSize.Y = FMath::Clamp(HalfSize.Y, 14.0f, Canvas->ClipY * 0.34f);

	const FVector2D TargetMin = Center - HalfSize;
	const FVector2D TargetMax = Center + HalfSize;
	if (FocusBracketAlpha <= 0.01f)
	{
		FocusBracketMin = TargetMin;
		FocusBracketMax = TargetMax;
	}
	else
	{
		FocusBracketMin = FMath::Vector2DInterpTo(FocusBracketMin, TargetMin, DeltaSeconds, 18.0f);
		FocusBracketMax = FMath::Vector2DInterpTo(FocusBracketMax, TargetMax, DeltaSeconds, 18.0f);
	}
	FocusBracketAlpha = FMath::Clamp(
		(FocusBracketAcquireElapsed - IGHorrorHUD::FocusAcquireDelaySeconds)
			/ IGHorrorHUD::FocusAcquireRevealSeconds,
		0.0f,
		1.0f);
}

void AIGHorrorHUD::DrawFocusBracket(const FLinearColor& Color, const float Progress)
{
	if (!Canvas)
	{
		return;
	}

	FLinearColor BracketColor = Color;
	BracketColor.A *= FMath::Clamp(FocusBracketAlpha, 0.0f, 1.0f);
	const float CloseAlpha = FMath::Clamp(Progress, 0.0f, 1.0f);
	const FVector2D Center = (FocusBracketMin + FocusBracketMax) * 0.5f;
	const FVector2D ClosedHalfSize(7.0f, 7.0f);
	const FVector2D DrawMin = FMath::Lerp(
		FocusBracketMin,
		Center - ClosedHalfSize,
		CloseAlpha);
	const FVector2D DrawMax = FMath::Lerp(
		FocusBracketMax,
		Center + ClosedHalfSize,
		CloseAlpha);
	const float CornerX = FMath::Min(18.0f, (DrawMax.X - DrawMin.X) * 0.34f);
	const float CornerY = FMath::Min(18.0f, (DrawMax.Y - DrawMin.Y) * 0.34f);
	constexpr float Thickness = 2.0f;

	auto DrawCorner = [this, &BracketColor](
		const FVector2D& Pivot, const float DirectionX, const float DirectionY,
		const float LengthX, const float LengthY)
	{
		DrawLine(Pivot.X, Pivot.Y, Pivot.X + DirectionX * LengthX, Pivot.Y,
			BracketColor, Thickness);
		DrawLine(Pivot.X, Pivot.Y, Pivot.X, Pivot.Y + DirectionY * LengthY,
			BracketColor, Thickness);
	};

	DrawCorner(DrawMin, 1.0f, 1.0f, CornerX, CornerY);
	DrawCorner(FVector2D(DrawMax.X, DrawMin.Y), -1.0f, 1.0f, CornerX, CornerY);
	DrawCorner(FVector2D(DrawMin.X, DrawMax.Y), 1.0f, -1.0f, CornerX, CornerY);
	DrawCorner(DrawMax, -1.0f, -1.0f, CornerX, CornerY);
}

void AIGHorrorHUD::MoveNotePage(const int32 Direction)
{
	const AIGReadableNote* Note = AIGReadableNote::GetOpenNote();
	if (!Note || Direction == 0 || ReadingLayoutNote.Get() != Note
		|| ReadingLayoutRevision != Note->GetPresentationRevision()) return;
	const int32 PreviousPage = NotePageIndex;
	NotePageIndex = FMath::Clamp(NotePageIndex + FMath::Sign(Direction), 0, NotePageCount - 1);
	// 실제로 넘어갔을 때만 종이가 운다. 끝 장에서 더 미는 입력은 조용하다.
	if (NotePageIndex != PreviousPage)
	{
		Note->NotifyPageTurned(GetOwningPawn());
	}
}

void AIGHorrorHUD::DrawNotePanel()
{
	AIGReadableNote* Note = AIGReadableNote::GetOpenNote();
	if (!Note || !Canvas) return;
	if (Note->UsesPhoneNotificationPresentation())
	{
		DrawPhoneNotificationPanel(*Note);
		return;
	}
	UFont* BodyFont = GetFontForRole(EIGHudTextRole::Prompt);
	UFont* HintFont = GetFontForRole(EIGHudTextRole::Hint);
	if (!BodyFont || !HintFont) return;
	const UIGAccessibilitySubsystem* Accessibility = GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>();
	const float UserScale = Accessibility ? Accessibility->GetCaptionSizeScale() : 1.f;
	const float Scale = GetResolutionTextScale(UserScale);
	const float ScreenScale = FMath::Clamp(Canvas->ClipY / 1080.f, .65f, 1.5f);
	// 읽기 그림은 한국어로 인쇄된 원본이다. 한국어에서는 그림이 첫 장이다. 다른
	// 언어에서는 번역된 글을 먼저 보여 주고 원본 그림은 마지막 장에 둔다.
	UTexture2D* Artwork = Note->GetReadingArtwork();
	const bool HasArtwork = Artwork != nullptr;
	const bool bArtworkFirst = HasArtwork && IsKoreanCulture();
	const float PaperHeight = FMath::Min(Canvas->ClipY * .80f, 860.f * ScreenScale);
	const float PaperWidth = FMath::Min(Canvas->ClipX * .86f,
		HasArtwork ? PaperHeight * 1.385f : PaperHeight * .707f);
	const float Height = HasArtwork ? PaperWidth / 1.385f : PaperHeight;
	const FVector2D Origin((Canvas->ClipX-PaperWidth)*.5f, (Canvas->ClipY-Height)*.5f);
	const float Margin = PaperWidth * .075f;
	const float TextWidth = PaperWidth - Margin*2;
	const float LineHeight = MeasureTextHeight(GetLineHeightSample(), BodyFont, Scale) * 1.4f;
	const float HeaderHeight = MeasureTextHeight(GetLineHeightSample(), BodyFont, Scale) * 1.6f;
	const float BodyTop = Origin.Y + Margin + HeaderHeight + 16*ScreenScale;
	const float BodyBottom = Origin.Y + Height - Margin;
	const int32 LinesPerPage = FMath::Max(1, FMath::FloorToInt((BodyBottom-BodyTop)/LineHeight));
	const FVector2D LayoutSize(PaperWidth, Height);
	if (ReadingLayoutNote.Get() != Note || ReadingLayoutRevision != Note->GetPresentationRevision()
		|| !ReadingLayoutSize.Equals(LayoutSize, .1f) || !FMath::IsNearlyEqual(ReadingLayoutScale, Scale))
	{
		const bool Reopened = ReadingLayoutNote.Get() != Note || ReadingLayoutRevision != Note->GetPresentationRevision();
		ReadingLayoutNote = Note;
		ReadingLayoutRevision = Note->GetPresentationRevision();
		ReadingLayoutSize = LayoutSize;
		ReadingLayoutScale = Scale;
		ReadingTextPages.Reset();
		TArray<FString> Lines;
		for (const FText& Paragraph : Note->GetBodyLines())
		{
			if (Paragraph.IsEmpty()) { Lines.Add(FString()); continue; }
			TArray<FString> Wrapped;
			FString Remaining;
			WrapHudText(Paragraph.ToString(), BodyFont, Scale, TextWidth, MAX_int32, Wrapped, Remaining);
			Lines.Append(Wrapped);
		}
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			if (Index % LinesPerPage == 0) ReadingTextPages.AddDefaulted();
			ReadingTextPages.Last().Add(Lines[Index]);
		}
		if (ReadingTextPages.IsEmpty()) ReadingTextPages.AddDefaulted();
		NotePageCount = ReadingTextPages.Num() + int32(HasArtwork);
		if (Reopened) NotePageIndex = bArtworkFirst && UserScale > 1.15f ? 1 : 0;
		NotePageIndex = FMath::Clamp(NotePageIndex, 0, NotePageCount-1);
	}
	const FLinearColor Ink(.075f, .080f, .078f, 1);
	DrawRect(FLinearColor(0,0,0,.72f), 0, 0, Canvas->ClipX, Canvas->ClipY);
	DrawRect(FLinearColor(0,0,0,.55f), Origin.X+5*ScreenScale, Origin.Y+7*ScreenScale, PaperWidth, Height);
	const bool ArtworkPage = HasArtwork
		&& NotePageIndex == (bArtworkFirst ? 0 : NotePageCount - 1);
	UTexture2D* Paper = ArtworkPage ? Artwork : NotePaperTexture.Get();
	if (Paper)
		DrawTexture(Paper, Origin.X, Origin.Y, PaperWidth, Height, 0, 0, 1, 1, FLinearColor::White, BLEND_Opaque);
	else
		DrawRect(FLinearColor(.91f,.91f,.89f,1), Origin.X, Origin.Y, PaperWidth, Height);
	bNoteTextWithinPaper = true;
	PushTextAuditContainer(Origin, Origin + FVector2D(PaperWidth, Height));
	if (!ArtworkPage)
	{
		const FString Title = Note->GetTitle().ToString();
		const float TitleScale = FMath::Min(Scale, TextWidth/FMath::Max(1.f, MeasureTextWidth(Title, BodyFont, 1.f)));
		FCanvasTextItem Header(Origin+FVector2D(Margin,Margin), FText::FromString(Title), BodyFont, Ink);
		Header.Scale = FVector2D(TitleScale);
		Canvas->DrawItem(Header);
		RecordTextAudit(Title, Origin + FVector2D(Margin, Margin), Origin + FVector2D(Margin, Margin)
			+ FVector2D(MeasureTextWidth(Title, BodyFont, TitleScale), MeasureTextHeight(Title, BodyFont, TitleScale)));
		DrawRect(FLinearColor(.25f,.28f,.29f,.5f), Origin.X+Margin,
			Origin.Y+Margin+HeaderHeight, TextWidth, ScreenScale);
		const TArray<FString>& Lines = ReadingTextPages[NotePageIndex-int32(bArtworkFirst)];
		float Y = BodyTop;
		for (const FString& Line : Lines)
		{
			FCanvasTextItem Text(FVector2D(Origin.X+Margin,Y), FText::FromString(Line), BodyFont, Ink);
			Text.Scale = FVector2D(Scale);
			Canvas->DrawItem(Text);
			RecordTextAudit(Line, FVector2D(Origin.X + Margin, Y), FVector2D(Origin.X + Margin, Y)
				+ FVector2D(MeasureTextWidth(Line, BodyFont, Scale), MeasureTextHeight(Line, BodyFont, Scale)));
			bNoteTextWithinPaper &= MeasureTextWidth(Line,BodyFont,Scale) <= TextWidth+.5f
				&& Y+MeasureTextHeight(Line,BodyFont,Scale) <= BodyBottom+.5f;
			Y += LineHeight;
		}
	}
	PopTextAuditContainer();
	// 안내는 종이 바깥에 둔다. 큰 글씨가 본문과 겹치거나 인쇄처럼 보이지 않는다.
	FString Footer;
	if (NotePageCount > 1)
	{
		const FText Navigation = bUsingGamepad
			? NSLOCTEXT("IGHUD", "NoteNavigationGamepad", "방향 패드 좌우")
			: NSLOCTEXT("IGHUD", "NoteNavigationKeyboard", "← → / 휠");
		Footer = FText::Format(
			ArtworkPage
				? (bArtworkFirst
					? NSLOCTEXT("IGHUD", "NotePageArtworkFormat", "[ {0} ]  {1} / {2} · 다음 장은 본문")
					: NSLOCTEXT("IGHUD", "NotePageOriginalFormat", "[ {0} ]  {1} / {2} · 한국어 원본"))
				: NSLOCTEXT("IGHUD", "NotePageFormat", "[ {0} ]  {1} / {2}"),
			Navigation,
			FText::AsNumber(NotePageIndex + 1),
			FText::AsNumber(NotePageCount)).ToString() + TEXT("    ");
	}
	Footer += FText::Format(NSLOCTEXT("IGHUD", "NoteCloseBound", "[ {0} ]  덮기"),
		GetBoundKeyLabel(EIGBindableAction::Interact,bUsingGamepad)).ToString();
	const float FooterScale = FMath::Min(.80f*Scale,
		(Canvas->ClipX*.90f)/FMath::Max(1.f,MeasureTextWidth(Footer,HintFont,1.f)));
	const float FooterWidth = MeasureTextWidth(Footer,HintFont,FooterScale);
	FCanvasTextItem Hint(FVector2D((Canvas->ClipX-FooterWidth)*.5f,Origin.Y+Height+14*ScreenScale),
		FText::FromString(Footer),HintFont,FLinearColor(.8f,.81f,.78f,1));
	Hint.Scale = FVector2D(FooterScale);
	Canvas->DrawItem(Hint);
}

void AIGHorrorHUD::DrawPhoneNotificationPanel(const AIGReadableNote& Note)
{
	if (!Canvas)
	{
		return;
	}

	const float ScreenWidth = Canvas->ClipX;
	const float ScreenHeight = Canvas->ClipY;
	DrawRect(
		FLinearColor(0.0f, 0.0f, 0.0f, 0.84f),
		0.0f,
		0.0f,
		ScreenWidth,
		ScreenHeight);

	const float PhoneHeight = FMath::Clamp(ScreenHeight * 0.82f, 500.0f, 700.0f);
	const float PhoneWidth = FMath::Clamp(PhoneHeight * 0.58f, 280.0f, 400.0f);
	const FVector2D PhoneOrigin(
		(ScreenWidth - PhoneWidth) * 0.5f,
		(ScreenHeight - PhoneHeight) * 0.5f);
	constexpr float Bezel = 8.0f;
	const FVector2D ScreenOrigin = PhoneOrigin + FVector2D(Bezel, Bezel);
	const float InnerWidth = PhoneWidth - Bezel * 2.0f;
	const float InnerHeight = PhoneHeight - Bezel * 2.0f;
	PushTextAuditContainer(ScreenOrigin, ScreenOrigin + FVector2D(InnerWidth, InnerHeight));

	DrawRect(
		FLinearColor(0.0f, 0.0f, 0.0f, 0.62f),
		PhoneOrigin.X + 7.0f,
		PhoneOrigin.Y + 10.0f,
		PhoneWidth,
		PhoneHeight);
	DrawRect(
		FLinearColor(0.025f, 0.029f, 0.032f, 1.0f),
		PhoneOrigin.X,
		PhoneOrigin.Y,
		PhoneWidth,
		PhoneHeight);
	DrawRect(
		FLinearColor(0.035f, 0.055f, 0.064f, 1.0f),
		ScreenOrigin.X,
		ScreenOrigin.Y,
		InnerWidth,
		InnerHeight);
	DrawRect(
		FLinearColor(0.12f, 0.15f, 0.16f, 1.0f),
		ScreenWidth * 0.5f - 18.0f,
		PhoneOrigin.Y + 4.0f,
		36.0f,
		3.0f);
	DrawRect(
		FLinearColor(0.005f, 0.008f, 0.010f, 1.0f),
		ScreenWidth * 0.5f - 3.0f,
		ScreenOrigin.Y + 7.0f,
		6.0f,
		6.0f);

	UFont* BodyFont = GetFontForRole(EIGHudTextRole::Prompt);
	UFont* MetaFont = KoreanPhoneMetaFont
		? KoreanPhoneMetaFont.Get()
		: GetFontForRole(EIGHudTextRole::Hint);
	if (!BodyFont || !MetaFont)
	{
		return;
	}

	const FLinearColor PrimaryText(0.91f, 0.94f, 0.94f, 1.0f);
	const FLinearColor SecondaryText(0.63f, 0.69f, 0.70f, 1.0f);
	const FLinearColor Accent(0.18f, 0.66f, 0.57f, 1.0f);
	auto DrawPhoneText = [this](
		const FText& Text,
		UFont* Font,
		const float X,
		const float Y,
		const FLinearColor& Color,
		const float TextScale)
	{
		FCanvasTextItem Item(FVector2D(X, Y), Text, Font, Color);
		Item.Scale = FVector2D(TextScale);
		Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), FVector2D(1.0f, 1.0f));
		Canvas->DrawItem(Item);
		if (bTextAuditEnabled)
		{
			float Width = 0.0f;
			float Height = 0.0f;
			Canvas->StrLen(Font, Text.ToString(), Width, Height, true);
			RecordTextAudit(
				Text.ToString(),
				FVector2D(X, Y),
				FVector2D(X + Width * TextScale, Y + Height * TextScale));
		}
	};
	// 폰 화면은 폭이 좁다. 번역이 길면 이 폭 안에 들어올 만큼 줄인다.
	auto FitPhoneScale = [this](
		const FText& Text,
		UFont* Font,
		const float PreferredScale,
		const float MaximumWidth)
	{
		const float RawWidth = MeasureTextWidth(Text.ToString(), Font, 1.0f);
		return RawWidth > KINDA_SMALL_NUMBER
			? FMath::Clamp(MaximumWidth / RawWidth, PreferredScale * 0.7f, PreferredScale)
			: PreferredScale;
	};

	// 탁자 위 폰은 그 시간(04:30~05:30)에는 손에 없다. 이사 온 날 저녁이
	// 아니면 낮에 보는 화면이다.
	const UGameInstance* PhoneGameInstance = GetGameInstance();
	const UIGMissingFloorNarrativeSubsystem* PhoneNarrative = PhoneGameInstance
		? PhoneGameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	const bool bDaytime = PhoneNarrative && PhoneNarrative->GetNightIndex() > 0;
	const float ContentLeft = ScreenOrigin.X + 16.0f;
	DrawPhoneText(
		FText::FromString(bDaytime ? TEXT("14:17") : TEXT("19:48")),
		MetaFont,
		ContentLeft,
		ScreenOrigin.Y + 9.0f,
		PrimaryText,
		1.0f);
	const FText StatusText = FText::FromString(TEXT("LTE   76%"));
	float StatusWidth = 0.0f;
	float StatusHeight = 0.0f;
	Canvas->StrLen(MetaFont, StatusText.ToString(), StatusWidth, StatusHeight);
	DrawPhoneText(
		StatusText,
		MetaFont,
		ScreenOrigin.X + InnerWidth - StatusWidth - 14.0f,
		ScreenOrigin.Y + 9.0f,
		SecondaryText,
		1.0f);

	const float NotificationX = ScreenOrigin.X + 11.0f;
	const float NotificationY = ScreenOrigin.Y + 48.0f;
	const float NotificationWidth = InnerWidth - 22.0f;
	const float TextLeft = NotificationX + 16.0f;
	const float TextWidth = NotificationWidth - 32.0f;

	// 본문은 폰 화면 폭에서 줄을 바꾼다. 한국어도 한 줄에 다 들어가지 않는다.
	// 빈 줄은 문단 사이를 반 줄 띄운다. 마지막 줄은 거래 상태다.
	constexpr float BodyScale = 0.8f;
	float SampleWidth = 0.0f;
	float SampleHeight = 20.0f;
	Canvas->StrLen(BodyFont, *GetLineHeightSample(), SampleWidth, SampleHeight, true);
	const float BodyLineHeight = SampleHeight * BodyScale * 1.18f;
	struct FPhoneLine
	{
		FString Text;
		bool bStatus = false;
	};
	TArray<FPhoneLine> BodyLines;
	const TArray<FText>& Lines = Note.GetBodyLines();
	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		if (Lines[LineIndex].IsEmpty())
		{
			BodyLines.Add(FPhoneLine());
			continue;
		}
		TArray<FString> Wrapped;
		FString Remainder;
		WrapHudText(
			Lines[LineIndex].ToString(),
			BodyFont,
			BodyScale,
			TextWidth,
			3,
			Wrapped,
			Remainder);
		for (FString& Piece : Wrapped)
		{
			FPhoneLine Wrap;
			Wrap.Text = MoveTemp(Piece);
			Wrap.bStatus = LineIndex == Lines.Num() - 1;
			BodyLines.Add(MoveTemp(Wrap));
		}
	}
	float BodyHeight = 0.0f;
	for (const FPhoneLine& Line : BodyLines)
	{
		BodyHeight += Line.Text.IsEmpty() ? BodyLineHeight * 0.5f : BodyLineHeight;
	}
	const float BodyTop = NotificationY + 98.0f;
	const float NotificationHeight = FMath::Clamp(
		BodyTop + BodyHeight + 18.0f - NotificationY,
		FMath::Min(250.0f, InnerHeight * 0.47f),
		InnerHeight - 92.0f);
	DrawRect(
		FLinearColor(0.065f, 0.086f, 0.092f, 0.98f),
		NotificationX,
		NotificationY,
		NotificationWidth,
		NotificationHeight);
	DrawRect(
		Accent,
		NotificationX,
		NotificationY,
		4.0f,
		NotificationHeight);
	DrawRect(
		Accent,
		NotificationX + 15.0f,
		NotificationY + 15.0f,
		28.0f,
		28.0f);
	// 동네 중고 거래 앱에서 오빠 공구를 찾아본 화면이다. 알림이 아니다.
	const float HeaderTextWidth = NotificationWidth - 51.0f - 12.0f;
	const FText AppName = NSLOCTEXT("IGHUD", "PhoneMarketApp", "동네장터");
	DrawPhoneText(
		AppName,
		MetaFont,
		NotificationX + 51.0f,
		NotificationY + 10.0f,
		PrimaryText,
		FitPhoneScale(AppName, MetaFont, 1.0f, HeaderTextWidth));
	const FText SearchLabel =
		NSLOCTEXT("IGHUD", "PhoneMarketSearch", "‘조율 공구’ 관련 글");
	DrawPhoneText(
		SearchLabel,
		MetaFont,
		NotificationX + 51.0f,
		NotificationY + 33.0f,
		SecondaryText,
		FitPhoneScale(SearchLabel, MetaFont, 1.0f, HeaderTextWidth));

	DrawPhoneText(
		Note.GetTitle(),
		BodyFont,
		TextLeft,
		NotificationY + 63.0f,
		PrimaryText,
		FitPhoneScale(Note.GetTitle(), BodyFont, 0.94f, TextWidth));
	float PenY = BodyTop;
	for (const FPhoneLine& Line : BodyLines)
	{
		if (Line.Text.IsEmpty())
		{
			PenY += BodyLineHeight * 0.5f;
			continue;
		}
		DrawPhoneText(
			FText::FromString(Line.Text),
			BodyFont,
			TextLeft,
			PenY,
			Line.bStatus ? Accent : PrimaryText,
			BodyScale);
		PenY += BodyLineHeight;
	}
	PopTextAuditContainer();
	const FText Hint = FText::Format(
		NSLOCTEXT("IGHUD", "PhoneCloseFormat", "[ {0} ]  휴대폰 내려놓기"),
		GetBoundKeyLabel(EIGBindableAction::Interact, bUsingGamepad));
	DrawCenteredText(
		Hint,
		FMath::Min(ScreenHeight - 26.0f, PhoneOrigin.Y + PhoneHeight + 16.0f),
		IGHorrorHUD::PaleGray,
		EIGHudTextRole::Hint);
}
