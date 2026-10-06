#include "Player/IGPlayerController.h"
#include "Core/IGPlayRecord.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGAudioRenderProbe.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "AssetCompilingManager.h"
#include "Components/AudioComponent.h"
#include "CoreGlobals.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGListenerTuning.h"
#include "Entity/IGMissingFloorEpilogueDirector.h"
#include "Entity/IGMissingFloorFifthDawnDirector.h"
#include "InputCoreTypes.h"
#include "Interaction/IGReadableNote.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HighResScreenshot.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Core/IGLanguageSubsystem.h"
#include "Narrative/IGMissingFloorHints.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGStoryStateSubsystem.h"
#include "Player/IGCameraFeelModifier.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGInputBindingSubsystem.h"
#include "Player/IGFrontendMenuLayout.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGSettingsMenuLayout.h"
#include "Save/IGSaveSubsystem.h"
#include "ShaderCompiler.h"
#include "IndieGame.h"

namespace IGInputLocks
{
	/** 이 시간을 넘겨 잠금이 남아 있으면 누가 붙들고 있는지 한 번 찍는다. */
	constexpr double WatchdogSeconds = 12.0;
}

namespace IGAccessibilityMenu
{
	constexpr int32 RowCount = IGSettingsMenuLayout::AccessibilityRowCount;

	void ApplyNightDifficulty(UWorld* World, const EIGNightDifficulty Difficulty)
	{
		IGListenerTuning::SavePersistedDifficulty(Difficulty);
		if (World)
		{
			for (TActorIterator<AIGListenerEntity> It(World); It; ++It)
			{
				It->SetDifficulty(Difficulty);
			}
		}
	}
}

namespace IGSystemMenu
{
	constexpr int32 RowCount = IGFrontendMenuLayout::ActionCount;
}

namespace IGNightFive
{
	/**
	 * §9 「밤 5」의 30초. 대부분이 침묵이다 — 그것이 이 비트의 내용이다.
	 * 두 소리 사이의 9초가 「새 사건도, 갇힌 사람도 암시하지 않는다」를
	 * 지키는 방식이고, 뒤의 17초는 아무 일도 일어나지 않는다는 것을 확인하는
	 * 시간이다. 채울 수 있다는 이유로 채우면 이 슬롯은 다른 게임이 된다.
	 */
	constexpr float PollSeconds = 0.1f;
	constexpr float SignalAtSeconds = 3.2f;
	constexpr float AnswerAtSeconds = 12.6f;

	/** 그녀 자신의 손이므로 가깝고 마른 소리. */
	constexpr float SignalVolume = 0.86f;
	constexpr float CloseRadius = 4000.0f;
	constexpr float CloseFalloff = 6000.0f;
	/**
	 * 복도 끝이므로 작고 젖은 소리. 카메라 뒤 왼쪽 6m쯤에 두어 §10.4의 거리
	 * 문법을 태운다 — 멀수록 젖는다. 카메라 자리에서 내면 ENTITY 젖음이 하한에
	 * 붙어 그녀의 신호보다 말라 버리고, 그건 「같은 방」이라는 뜻이다.
	 */
	constexpr float AnswerVolume = 0.70f;
	constexpr float FarRadius = 200.0f;
	constexpr float FarFalloff = 2400.0f;
	constexpr float AnswerBehindCentimeters = 520.0f;
	constexpr float AnswerLeftCentimeters = 300.0f;
}

namespace IGDisplaySettings
{
	constexpr int32 RowCount = IGSettingsMenuLayout::DisplayRowCount;
	constexpr int32 WindowModeCount = 3;
	constexpr int32 ResolutionCount = 3;
	constexpr int32 QualityCount = 2;
	constexpr int32 FrameLimitCount = 3;
	const FIntPoint Resolutions[ResolutionCount] =
	{
		FIntPoint(1280, 720),
		FIntPoint(1920, 1080),
		FIntPoint(2560, 1440)
	};
	const float FrameLimits[FrameLimitCount] = {30.0f, 60.0f, 0.0f};
}

namespace IGAudioCalibration
{
	// 행을 번호로 세다가 하나 끼우면 밝기가 출력 방식이 된다. 이름을 준다.
	enum ERow : int32
	{
		Master = 0,
		Music,
		Ambience,
		Output,
		Brightness,
		TestKnock,
		Done,
		Count
	};
	constexpr int32 RowCount = ERow::Count;
	static_assert(
		RowCount == IGSettingsMenuLayout::AudioCalibrationRowCount,
		"소리와 밝기 행 이름과 화면의 줄 수가 어긋났다");
	constexpr int32 MusicStepCount = 5;
	constexpr int32 AmbienceStepCount = 5;
	// 음악은 0까지 내려간다. 작가가 얹은 것이라 없어도 사건은 남는다.
	constexpr float MusicValues[MusicStepCount] =
	{
		0.0f, 0.25f, 0.50f, 0.75f, 1.0f
	};
	// 환경음은 바닥이 있다. 건물이 내는 소리 자체가 단서다(§21.1).
	constexpr float AmbienceValues[AmbienceStepCount] =
	{
		0.40f, 0.55f, 0.70f, 0.85f, 1.0f
	};
	constexpr int32 VolumeStepCount = 7;
	constexpr int32 BrightnessStepCount = 5;
	constexpr const TCHAR* ConfigSection = TEXT("IndieGame.AudioOnboarding");
	constexpr float VolumeValues[VolumeStepCount] =
	{
		0.25f, 0.375f, 0.50f, 0.625f, 0.75f, 0.875f, 1.0f
	};
	constexpr float GammaValues[BrightnessStepCount] =
	{
		1.80f, 2.00f, 2.20f, 2.40f, 2.60f
	};
}

AIGPlayerController::AIGPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	// PlayerTick이 입력과 카메라 갱신을 맡는다. 메뉴가 닫혀도 끄면 안 된다.
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AIGPlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = false;
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	ApplyDefaultInputMapping();
	// 손맛 회전(노크·포획 킥, 공포 떨림)은 카메라 매니저 단계에서 얹는다.
	// 카메라 컴포넌트의 상대 회전은 폰 제어 회전에 덮여 화면에 안 나온다.
	if (PlayerCameraManager)
	{
		PlayerCameraManager->AddNewCameraModifier(UIGCameraFeelModifier::StaticClass());
	}
	BindSaveNotifications();
	LoadAudioCalibrationSettings();

	bNightFiveProbeRequested =
		FParse::Param(FCommandLine::Get(), TEXT("IGNightFiveProbe"));
	if (bNightFiveProbeRequested)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_NIGHT5_BOOT local=%d title=%d"),
			IsLocalController() ? 1 : 0,
			ShouldShowTitleMenu() ? 1 : 0);
	}

	if (IsLocalController() && ShouldShowTitleMenu())
	{
		SystemMenuSelection = 0;
		SetSystemMenuMode(EIGSystemMenuMode::Title);
		// 고지가 먼저다. 무엇이 나오는지 모른 채로 소리부터 맞추게 할 수 없다.
		ShowContentNoticeIfNeeded();
		StartHeadphoneRecommendationIfNeeded();
		if (bNightFiveProbeRequested)
		{
			StartNightFiveProbe();
		}
	}
	else
	{
		RefreshMenuHud();
	}

	if (IsLocalController() && IGAudioRenderProbe::RunIfRequested(this))
	{
		return;
	}
	if (IsLocalController()
		&& FParse::Param(
			FCommandLine::Get(),
			TEXT("IGFrontendShippingProbe")))
	{
		StartFrontendShippingProbe();
	}
	else if (IsLocalController()
		&& FParse::Param(
			FCommandLine::Get(),
			TEXT("IGAudioCalibrationPreview")))
	{
		StartAudioCalibrationPreviewProbe();
	}
	else if (IsLocalController()
		&& FParse::Param(
			FCommandLine::Get(),
			TEXT("IGMissingFloorEndingPreview")))
	{
		StartMissingFloorEndingPreviewProbe();
	}
}

void AIGPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindSaveNotifications();
	RestoreMenuWorldRendering();
	Super::EndPlay(EndPlayReason);
}

void AIGPlayerController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFrontendShippingProbe)
	{
		TickFrontendShippingProbe();
		return;
	}
	if (bAudioCalibrationPreviewProbe)
	{
		TickAudioCalibrationPreviewProbe();
		return;
	}
	if (bMissingFloorEndingPreviewProbe)
	{
		TickMissingFloorEndingPreviewProbe();
		return;
	}
	if (InputLockWatchdogNextReportTime > 0.0
		&& FPlatformTime::Seconds() >= InputLockWatchdogNextReportTime)
	{
		UE_LOG(
			LogIndieGame,
			Warning,
			TEXT("IG_INPUT_LOCK held for %.0fs by %s"),
			IGInputLocks::WatchdogSeconds,
			*DescribeInputLocks());
		InputLockWatchdogNextReportTime =
			FPlatformTime::Seconds() + IGInputLocks::WatchdogSeconds;
	}
	if (bJournalInputHeld && !bMissingFloorJournalVisible)
	{
		const UIGAccessibilitySubsystem* Accessibility =
			GetAccessibilitySubsystem();
		const double JournalHoldSeconds = 0.30 * (Accessibility
			? Accessibility->GetHoldDurationScale()
			: 1.0f);
		if (FPlatformTime::Seconds() - JournalInputPressedAt >= JournalHoldSeconds)
		{
			bJournalInputHeld = false;
			OpenMissingFloorJournal();
		}
	}
	if (bHeadphoneRecommendationVisible
		&& FPlatformTime::Seconds() >= HeadphoneRecommendationDeadline)
	{
		DismissHeadphoneRecommendation();
	}
	if (SystemMenuMode == EIGSystemMenuMode::AudioCalibration
		&& NextAudioCalibrationKnockTime > 0.0
		&& FPlatformTime::Seconds() >= NextAudioCalibrationKnockTime)
	{
		PlayAudioCalibrationKnock();
	}
	if (!bDisplaySettingsAwaitingConfirmation
		&& !bJournalInputHeld
		&& !bHeadphoneRecommendationVisible
		&& NextAudioCalibrationKnockTime <= 0.0)
	{
		return;
	}
	if (!bDisplaySettingsAwaitingConfirmation)
	{
		return;
	}
	const double Remaining = DisplayConfirmationDeadline - FPlatformTime::Seconds();
	if (Remaining <= 0.0)
	{
		RevertPendingDisplaySettings();
		return;
	}
	const int32 RoundedRemaining = FMath::Max(1, FMath::CeilToInt(Remaining));
	if (DisplayConfirmationSecondsRemaining != RoundedRemaining)
	{
		DisplayConfirmationSecondsRemaining = RoundedRemaining;
		RefreshMenuHud();
	}
}

void AIGPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent)
	{
		auto BindPausedAction = [this](
			const FName ActionName,
			void (ThisClass::*Handler)()) -> FInputActionBinding&
		{
			FInputActionBinding& Binding = InputComponent->BindAction(
				ActionName,
				IE_Pressed,
				this,
				Handler);
			Binding.bExecuteWhenPaused = true;
			return Binding;
		};
		// 메뉴 이동 키는 WASD·Space·패드 A·패드 B와 겹친다. 컨트롤러가 폰보다 먼저
		// 입력을 받으므로, 여기서 키를 삼키면 메뉴가 닫혀 있어도 점프·조사·두드리기가
		// 폰까지 가지 않는다. 메뉴는 열리는 순간 게임을 멈추고 폰의 바인딩은 멈춘 동안
		// 돌지 않으니, 키를 흘려보내도 메뉴 조작이 몸을 움직이지는 않는다.
		auto BindMenuNavigation = [&BindPausedAction](
			const FName ActionName,
			void (ThisClass::*Handler)())
		{
			BindPausedAction(ActionName, Handler).bConsumeInput = false;
		};
		BindPausedAction(TEXT("PauseMenu"), &ThisClass::ToggleSystemMenu);
		BindPausedAction(
			TEXT("AccessibilityMenu"),
			&ThisClass::ToggleAccessibilityMenu);
		BindMenuNavigation(
			TEXT("AccessibilityUp"),
			&ThisClass::MoveAccessibilitySelectionUp);
		BindMenuNavigation(
			TEXT("AccessibilityDown"),
			&ThisClass::MoveAccessibilitySelectionDown);
		BindMenuNavigation(
			TEXT("AccessibilityLeft"),
			&ThisClass::AdjustAccessibilityLeft);
		BindMenuNavigation(
			TEXT("AccessibilityRight"),
			&ThisClass::AdjustAccessibilityRight);
		BindMenuNavigation(
			TEXT("AccessibilityConfirm"),
			&ThisClass::ConfirmAccessibilitySelection);
		BindMenuNavigation(
			TEXT("AccessibilityClose"),
			&ThisClass::CloseAccessibilityMenu);
		FInputActionBinding& JournalPressed = InputComponent->BindAction(
			TEXT("Journal"),
			IE_Pressed,
			this,
			&ThisClass::BeginJournalInput);
		JournalPressed.bExecuteWhenPaused = true;
		FInputActionBinding& JournalReleased = InputComponent->BindAction(
			TEXT("Journal"),
			IE_Released,
			this,
			&ThisClass::EndJournalInput);
		JournalReleased.bExecuteWhenPaused = true;
		BindPausedAction(
			TEXT("JournalPrevious"),
			&ThisClass::MoveMissingFloorJournalPageLeft);
		BindPausedAction(
			TEXT("JournalNext"),
			&ThisClass::MoveMissingFloorJournalPageRight);
		InputComponent->BindAction(
			TEXT("RequestHint"),
			IE_Pressed,
			this,
			&ThisClass::RequestManualHint);
	}
}

bool AIGPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (bFrontendShippingProbe
		&& !Params.IsSimulatedInput()
		&& Params.Event == IE_Pressed)
	{
		++FrontendProbePressedEventCount;
	}
	if (CaptureKeyBindingInput(Params))
	{
		return true;
	}
	const bool bGamepad = Params.IsGamepad();
	const float AxisThreshold = bGamepad ? 0.30f : 0.01f;
	const bool bMeaningfulInput = !Params.IsSimulatedInput()
		&& (Params.Event == IE_Pressed
			|| Params.Event == IE_Repeat
			|| (Params.Event == IE_Axis
				&& (FMath::Abs(Params.AmountDepressed) > AxisThreshold
					|| Params.AmountDepressed2D.SizeSquared()
						> FMath::Square(AxisThreshold))));
	if (bHeadphoneRecommendationVisible
		&& !Params.IsSimulatedInput()
		&& Params.Event == IE_Pressed)
	{
		DismissHeadphoneRecommendation();
		return true;
	}
	if (bMeaningfulInput && bUsingGamepadForHud != bGamepad)
	{
		SetInputDevicePresentation(bGamepad);
	}
	if (!Params.IsSimulatedInput()
		&& Params.Event == IE_Axis
		&& (Params.Key == EKeys::MouseX || Params.Key == EKeys::MouseY))
	{
		UpdateMenuPointerHover();
	}
	if (!Params.IsSimulatedInput()
		&& Params.Event == IE_Pressed
		&& Params.Key == EKeys::LeftMouseButton
		&& HandleMenuPointerClick())
	{
		return true;
	}
	// 방향 패드의 메뉴 이동과 겹치므로 자유 조작 중에만 안내 키를 먼저 받는다.
	if (Params.Event == IE_Pressed && SystemMenuMode == EIGSystemMenuMode::Hidden
		&& !bAccessibilityMenuVisible && !bMissingFloorJournalVisible)
	{
		if (AIGReadableNote::GetOpenNote()
			&& (Params.Key == EKeys::MouseScrollUp || Params.Key == EKeys::MouseScrollDown))
		{
			if (AIGHorrorHUD* NoteHud = Cast<AIGHorrorHUD>(GetHUD()))
				NoteHud->MoveNotePage(Params.Key == EKeys::MouseScrollUp ? -1 : 1);
			return true;
		}
		const UIGInputBindingSubsystem* Bindings = GetGameInstance()->GetSubsystem<UIGInputBindingSubsystem>();
		AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(GetHUD());
		if (Bindings && Hud && Params.Key == Bindings->GetBoundKey(
			static_cast<int32>(EIGBindableAction::GameplayGuide), bGamepad))
		{
			if (Hud->CanShowGameplayGuide()) Hud->ToggleGameplayGuide();
			return true;
		}
	}
	return Super::InputKey(Params);
}

void AIGPlayerController::StartFrontendShippingProbe()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(
		CommandLine,
		TEXT("IGFrontendExpectedWidth="),
		FrontendProbeExpectedWidth);
	FParse::Value(
		CommandLine,
		TEXT("IGFrontendExpectedHeight="),
		FrontendProbeExpectedHeight);
	if (FrontendProbeExpectedWidth <= 0 || FrontendProbeExpectedHeight <= 0)
	{
		bFrontendShippingProbe = true;
		FailFrontendShippingProbe(TEXT("expected_resolution_missing"));
		return;
	}

	bFrontendShippingProbe = true;
	// 그려지는 모든 설정 행이 같은 위치에서 클릭되는지 확인한다.
	// 마지막 행이 묶음 범위에서 빠지면 키보드 선택만 이동하고 화면은 사라진다.
	const auto VerifySettingsRows = [this](const int32 RowCount,
		const int32 CategoryCount, auto GetCategory)
	{
		TArray<int32> Coverage;
		Coverage.Init(0, RowCount);
		for (int32 Category = 0; Category < CategoryCount; ++Category)
		{
			const auto Range = GetCategory(Category);
			for (int32 LocalRow = 0; LocalRow < Range.RowCount; ++LocalRow)
			{
				const int32 Row = Range.FirstRow + LocalRow;
				const auto Metrics = RowCount == IGSettingsMenuLayout::AccessibilityRowCount
					? IGSettingsMenuLayout::MakeAccessibilityPanelMetrics(
						FrontendProbeExpectedWidth, FrontendProbeExpectedHeight, Row)
					: IGSettingsMenuLayout::MakePanelMetrics(FrontendProbeExpectedWidth, FrontendProbeExpectedHeight);
				if (!Coverage.IsValidIndex(Row)) return false;
				++Coverage[Row];
				int32 HitRow = INDEX_NONE;
				bool bCategoryHit = false;
				const FVector2D Point((Metrics.ContentLeft + Metrics.ContentRight) * 0.5f,
					Metrics.OptionStartY + (LocalRow + 0.5f) * Metrics.OptionRowHeight);
				if (!IGSettingsMenuLayout::HitTestSettingsRow(Metrics, Point, Row,
					CategoryCount, GetCategory, HitRow, bCategoryHit)
					|| HitRow != Row || bCategoryHit) return false;
			}
		}
		for (const int32 Count : Coverage) if (Count != 1) return false;
		return true;
	};
	if (!VerifySettingsRows(IGSettingsMenuLayout::DisplayRowCount,
		IGSettingsMenuLayout::DisplayCategoryCount, IGSettingsMenuLayout::GetDisplayCategory)
		|| !VerifySettingsRows(IGSettingsMenuLayout::AccessibilityRowCount,
			IGSettingsMenuLayout::AccessibilityCategoryCount, IGSettingsMenuLayout::GetAccessibilityCategory))
	{
		FailFrontendShippingProbe(TEXT("settings_row_hit_coverage"));
		return;
	}
	FrontendProbeStep = 0;
	FrontendProbeLayoutSampleCount = 0;
	FrontendProbeMinimumElementCount = MAX_int32;
	FrontendProbePressedEventCount = 0;
	bFrontendDialogueDefaultVerified = false;
	bFrontendCaptionPauseVerified = false;
	FrontendCaptionPausedAt = 0.0;
	FrontendCaptionPauseSeconds = 0.0;
	bFrontendDialogueVerified = false;
	bFrontendDialogueSpeakerVerified = false;
	bFrontendDialogueContinuationVerified = false;
	bFrontendAccessibilityScreenshotRequested = false;
	bFrontendDisplayScreenshotRequested = false;
	bFrontendProbeCompilationDrained = false;
	FrontendProbeAwaitFrameSerial = 0;
	FrontendProbeDefaultScreenshotPath.Reset();
	FrontendProbeScreenshotPath.Reset();
	FrontendProbeTitleScreenshotPath.Reset();
	FrontendProbeBoundsMin = FVector2D(
		TNumericLimits<float>::Max(),
		TNumericLimits<float>::Max());
	FrontendProbeBoundsMax = FVector2D(
		TNumericLimits<float>::Lowest(),
		TNumericLimits<float>::Lowest());

	bAccessibilityMenuVisible = false;
	// 글자 크기 미리 보기가 있는 자막 묶음부터 실제 화면을 검사한다.
	AccessibilitySelection = IGSettingsMenuLayout::SoundCaptions;
	SystemMenuSelection = 0;
	SetSystemMenuMode(EIGSystemMenuMode::Hidden);
	SetInputDevicePresentation(false);
	const double Now = FPlatformTime::Seconds();
	FrontendProbeNextActionTime = Now + 0.75;
	FrontendProbeStepDeadline = Now + 8.0;
	SetActorTickEnabled(true);
}

void AIGPlayerController::TickFrontendShippingProbe()
{
	const double Now = FPlatformTime::Seconds();
	if (Now > FrontendProbeStepDeadline)
	{
		FailFrontendShippingProbe(FString::Printf(
			TEXT("timeout_step_%d"),
			FrontendProbeStep));
		return;
	}
	if (Now < FrontendProbeNextActionTime)
	{
		return;
	}
	if (!bFrontendProbeCompilationDrained)
	{
		// Offscreen editor runs can still be compiling materials when the first
		// settings frame is ready. Drain that work before collecting evidence so
		// engine progress text never contaminates a product-facing capture.
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		if (GEngine)
		{
			GEngine->bEnableOnScreenDebugMessages = false;
		}
		ConsoleCommand(TEXT("DisableAllScreenMessages"), true);
		bFrontendProbeCompilationDrained = true;
		const double SettledAt = FPlatformTime::Seconds();
		FrontendProbeNextActionTime = SettledAt + 0.40;
		FrontendProbeStepDeadline = SettledAt + 8.0;
		return;
	}
	auto WaitForInputProcessing = [this, Now](const int32 NextStep)
	{
		FrontendProbeStep = NextStep;
		FrontendProbeNextActionTime = Now + 0.05;
		FrontendProbeStepDeadline = Now + 4.0;
	};

	switch (FrontendProbeStep)
	{
	case 0:
		DispatchFrontendProbeKey(EKeys::F10);
		WaitForInputProcessing(1);
		return;
	case 1:
		if (!bAccessibilityMenuVisible || bUsingGamepadForHud)
		{
			FailFrontendShippingProbe(TEXT("keyboard_f10_open"));
			return;
		}
		ReleaseFrontendProbeKey(EKeys::F10);
		FrontendProbeStep = 2;
		AwaitFrontendProbeFrame();
		return;
	case 2:
		if (!bFrontendAccessibilityScreenshotRequested)
		{
			if (!TryCaptureFrontendProbeLayout(
				TEXT("accessibility_keyboard"),
				14))
			{
				return;
			}
			FString ScreenshotPath;
			if (FParse::Value(
				FCommandLine::Get(),
				TEXT("IGFrontendAccessibilityScreenshotPath="),
				ScreenshotPath))
			{
				ScreenshotPath.TrimQuotesInline();
				ScreenshotPath = FPaths::ConvertRelativePathToFull(ScreenshotPath);
				IFileManager::Get().MakeDirectory(
					*FPaths::GetPath(ScreenshotPath),
					true);
				FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
			}
			bFrontendAccessibilityScreenshotRequested = true;
			// Give the requested capture a complete frame before changing focus
			// to the other dense category.
			FrontendProbeNextActionTime = Now + 0.12;
			FrontendProbeStepDeadline = Now + 4.0;
			return;
		}
		// 메뉴에서 난이도를 바꾼 값이 저장되고 현재 적에게도 전달되는지 확인한다.
		{
			const EIGNightDifficulty Before = IGListenerTuning::LoadPersistedDifficulty();
			AccessibilitySelection = IGSettingsMenuLayout::NightDifficulty;
			ChangeAccessibilitySetting(1, false);
			const EIGNightDifficulty Expected = IGListenerTuning::ClampDifficulty(
				(static_cast<int32>(Before) + 1) % static_cast<int32>(EIGNightDifficulty::Count));
			bool bApplied = IGListenerTuning::LoadPersistedDifficulty() == Expected;
			for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
			{
				bApplied &= It->GetDifficulty() == Expected;
			}
			ChangeAccessibilitySetting(-1, false);
			if (!bApplied || IGListenerTuning::LoadPersistedDifficulty() != Before)
			{
				FailFrontendShippingProbe(TEXT("night_difficulty_setting"));
				return;
			}
		}
		AccessibilitySelection = IGSettingsMenuLayout::CognitiveAssist;
		DispatchFrontendProbeKey(EKeys::Gamepad_DPad_Down);
		WaitForInputProcessing(3);
		return;
	case 3:
		if (!bAccessibilityMenuVisible
			|| AccessibilitySelection != IGSettingsMenuLayout::SoundCaptions
			|| !bUsingGamepadForHud)
		{
			FailFrontendShippingProbe(TEXT("gamepad_dpad_down"));
			return;
		}
		ReleaseFrontendProbeKey(EKeys::Gamepad_DPad_Down);
		FrontendProbeStep = 4;
		AwaitFrontendProbeFrame();
		return;
	case 4:
		if (!TryCaptureFrontendProbeLayout(TEXT("accessibility_gamepad"), 14))
		{
			return;
		}
		DispatchFrontendProbeKey(EKeys::Up);
		WaitForInputProcessing(5);
		return;
	case 5:
		if (!bAccessibilityMenuVisible
			|| AccessibilitySelection != IGSettingsMenuLayout::CognitiveAssist
			|| bUsingGamepadForHud)
		{
			FailFrontendShippingProbe(TEXT("keyboard_up_return"));
			return;
		}
		ReleaseFrontendProbeKey(EKeys::Up);
		FrontendProbeStep = 6;
		AwaitFrontendProbeFrame();
		return;
	case 6:
		if (!TryCaptureFrontendProbeLayout(TEXT("accessibility_keyboard_return"), 13))
		{
			return;
		}
		DispatchFrontendProbeKey(EKeys::F10);
		WaitForInputProcessing(7);
		return;
	case 7:
		if (bAccessibilityMenuVisible)
		{
			FailFrontendShippingProbe(TEXT("keyboard_f10_close"));
			return;
		}
		ReleaseFrontendProbeKey(EKeys::F10);
		FrontendProbeStep = 8;
		FrontendProbeNextActionTime = Now + 0.10;
		FrontendProbeStepDeadline = Now + 4.0;
		return;
	case 8:
		DispatchFrontendProbeKey(EKeys::Gamepad_Special_Right);
		WaitForInputProcessing(9);
		return;
	case 9:
		if (!bAccessibilityMenuVisible || !bUsingGamepadForHud)
		{
			FailFrontendShippingProbe(TEXT("gamepad_menu_open"));
			return;
		}
		ReleaseFrontendProbeKey(EKeys::Gamepad_Special_Right);
		FrontendProbeStep = 10;
		AwaitFrontendProbeFrame();
		return;
	case 10:
		if (!TryCaptureFrontendProbeLayout(TEXT("accessibility_gamepad_reopen"), 14))
		{
			return;
		}
		DispatchFrontendProbeKey(EKeys::Gamepad_FaceButton_Right);
		WaitForInputProcessing(11);
		return;
	case 11:
		if (bAccessibilityMenuVisible)
		{
			FailFrontendShippingProbe(TEXT("gamepad_b_close"));
			return;
		}
		ReleaseFrontendProbeKey(EKeys::Gamepad_FaceButton_Right);
		FrontendProbeStep = 12;
		FrontendProbeNextActionTime = Now + 0.10;
		FrontendProbeStepDeadline = Now + 4.0;
		return;
	case 12:
		DispatchFrontendProbeKey(EKeys::Escape);
		WaitForInputProcessing(13);
		return;
	case 13:
		if (SystemMenuMode != EIGSystemMenuMode::Pause
			|| bUsingGamepadForHud)
		{
			FailFrontendShippingProbe(TEXT("keyboard_escape_pause"));
			return;
		}
		ReleaseFrontendProbeKey(EKeys::Escape);
		FrontendProbeStep = 14;
		AwaitFrontendProbeFrame();
		return;
	case 14:
		if (!TryCaptureFrontendProbeLayout(TEXT("pause_keyboard"), 8))
		{
			return;
		}
		DispatchFrontendProbeKey(EKeys::Gamepad_DPad_Down);
		WaitForInputProcessing(15);
		return;
	case 15:
		if (SystemMenuMode != EIGSystemMenuMode::Pause
			|| SystemMenuSelection != 2
			|| !bUsingGamepadForHud)
		{
			FailFrontendShippingProbe(TEXT("gamepad_pause_settings_row"));
			return;
		}
		ReleaseFrontendProbeKey(EKeys::Gamepad_DPad_Down);
		FrontendProbeStep = 16;
		AwaitFrontendProbeFrame();
		return;
	case 16:
		if (!TryCaptureFrontendProbeLayout(TEXT("pause_gamepad"), 8))
		{
			return;
		}
		DispatchFrontendProbeKey(EKeys::Gamepad_FaceButton_Bottom);
		WaitForInputProcessing(17);
		return;
	case 17:
		if (SystemMenuMode != EIGSystemMenuMode::DisplaySettings)
		{
			FailFrontendShippingProbe(TEXT("gamepad_a_display_open"));
			return;
		}
		{
			const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
			Scalability::FQualityLevels LowPreset;
			Scalability::FQualityLevels HighPreset;
			LowPreset.SetFromSingleQualityLevel(1);
			HighPreset.SetFromSingleQualityLevel(2);
			// 기본 높음이 낮음으로 보이거나 품질 변경이 내부 해상도를 낮추면 실패한다.
			if (!Settings || Settings->GetOverallScalabilityLevel() != 2 || DisplayQualityIndex != 1
				|| LowPreset.GetSingleQualityLevel() != 1 || HighPreset.GetSingleQualityLevel() != 2
				|| !FMath::IsNearlyEqual(LowPreset.ResolutionQuality, 100.0f)
				|| !FMath::IsNearlyEqual(HighPreset.ResolutionQuality, 100.0f))
			{
				FailFrontendShippingProbe(TEXT("native_quality_preset"));
				return;
			}
		}
		ReleaseFrontendProbeKey(EKeys::Gamepad_FaceButton_Bottom);
		// Performance has the most simultaneous display rows and is therefore
		// the strongest packaged screenshot for horizontal text fitting.
		DisplaySettingsSelection = IGSettingsMenuLayout::Quality;
		RefreshMenuHud();
		FrontendProbeStep = 18;
		AwaitFrontendProbeFrame();
		return;
	case 18:
		if (!TryCaptureFrontendProbeLayout(TEXT("display_gamepad"), 11))
		{
			return;
		}
		if (!bFrontendDisplayScreenshotRequested)
		{
			FString ScreenshotPath;
			if (FParse::Value(
				FCommandLine::Get(),
				TEXT("IGFrontendDisplayScreenshotPath="),
				ScreenshotPath))
			{
				ScreenshotPath.TrimQuotesInline();
				ScreenshotPath = FPaths::ConvertRelativePathToFull(ScreenshotPath);
				IFileManager::Get().MakeDirectory(
					*FPaths::GetPath(ScreenshotPath),
					true);
				FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
			}
			bFrontendDisplayScreenshotRequested = true;
		}
		DispatchFrontendProbeKey(EKeys::Gamepad_FaceButton_Right);
		WaitForInputProcessing(19);
		return;
	case 19:
		if (SystemMenuMode != EIGSystemMenuMode::Pause)
		{
			FailFrontendShippingProbe(TEXT("gamepad_b_display_back"));
			return;
		}
		ReleaseFrontendProbeKey(EKeys::Gamepad_FaceButton_Right);
		FrontendProbeStep = 20;
		AwaitFrontendProbeFrame();
		return;
	case 20:
		if (!TryCaptureFrontendProbeLayout(TEXT("pause_gamepad_return"), 8))
		{
			return;
		}
		DispatchFrontendProbeKey(EKeys::Gamepad_Special_Left);
		WaitForInputProcessing(21);
		return;
	case 21:
		if (SystemMenuMode != EIGSystemMenuMode::Hidden)
		{
			FailFrontendShippingProbe(TEXT("gamepad_view_pause_close"));
			return;
		}
		ReleaseFrontendProbeKey(EKeys::Gamepad_Special_Left);
		if (bAccessibilityMenuVisible
			|| SystemMenuMode != EIGSystemMenuMode::Hidden)
		{
			FailFrontendShippingProbe(TEXT("frontend_not_closed"));
			return;
		}
		if (UIGAccessibilitySubsystem* Accessibility =
			GetAccessibilitySubsystem())
		{
			FIGAccessibilitySettings Settings = Accessibility->GetSettings();
			Settings.bSubtitlesEnabled = true;
			Settings.bSoundCaptionsEnabled = true;
			if (FParse::Param(FCommandLine::Get(), TEXT("IGCaptionLifecycleReview")))
			{
				Settings.CaptionDurationScale = 1.0f;
			}
			Settings.CaptionSizeScale = 1.0f;
			Settings.CaptionBackgroundOpacity = 0.82f;
			Settings.CaptionSafeAreaScale = 0.90f;
			Accessibility->ApplySettings(Settings);
		}
		else
		{
			FailFrontendShippingProbe(TEXT("dialogue_accessibility_missing"));
			return;
		}
		if (AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
		{
			HorrorHUD->ShowDialogue(
				NSLOCTEXT("IGFrontendProbe", "DialogueSpeaker", "휴대폰"),
				NSLOCTEXT(
					"IGFrontendProbe",
					"DialogueDefaultKorean",
					"문자 신고가 접수되었습니다. 곧 담당 경찰관이 연락드리겠습니다."),
				EIGDialogueChannel::Device,
				5.0f,
				EIGDialoguePriority::Story);
			HorrorHUD->ShowAudioCaption(
				NSLOCTEXT(
					"IGFrontendProbe",
					"SoundCaption",
					"[천장에서 세 번 두드리는 소리]"),
				5.0f);
		}
		else
		{
			FailFrontendShippingProbe(TEXT("dialogue_hud_missing"));
			return;
		}
		FrontendProbeStep = 22;
		AwaitFrontendProbeFrame();
		// Capture the settled reading state, not an early frame from the 240 ms
		// entrance transition. This keeps visual evidence representative while
		// layout validation still observes the animated path frame-by-frame.
		FrontendProbeNextActionTime = Now + 0.30;
		return;
	case 22:
		if (!TryCaptureFrontendProbeLayout(
			TEXT("dialogue_default_scale"),
			5,
			false)
			|| !TryVerifyFrontendDialogueLayout(
				TEXT("dialogue_default"),
				1,
				2,
				false))
		{
			return;
		}
		// 소리 자막은 플레이 화면에서 실제로 그려져야 한다. 타이틀의 자막을
		// 없애는 변경이 접근성 기능까지 꺼 버리지 않았는지 먼저 확인한다.
		if (const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
		{
			if (!HorrorHUD->WasAudioCaptionDrawnInLastHudFrame())
			{
				FailFrontendShippingProbe(TEXT("gameplay_sound_caption_not_drawn"));
				return;
			}
		}
		else
		{
			FailFrontendShippingProbe(TEXT("dialogue_hud_missing"));
			return;
		}
		bFrontendDialogueDefaultVerified = true;
		if (FParse::Value(
			FCommandLine::Get(),
			TEXT("IGFrontendDefaultScreenshotPath="),
			FrontendProbeDefaultScreenshotPath))
		{
			FrontendProbeDefaultScreenshotPath.TrimQuotesInline();
			FrontendProbeDefaultScreenshotPath = FPaths::ConvertRelativePathToFull(
				FrontendProbeDefaultScreenshotPath);
			IFileManager::Get().MakeDirectory(
				*FPaths::GetPath(FrontendProbeDefaultScreenshotPath),
				true);
			FScreenshotRequest::RequestScreenshot(
				FrontendProbeDefaultScreenshotPath,
				true,
				false);
		}
		FrontendProbeStep = FParse::Param(FCommandLine::Get(), TEXT("IGCaptionLifecycleReview"))
			? 38 : 23;
		FrontendProbeNextActionTime = Now + 0.08;
		FrontendProbeStepDeadline = Now + 5.0;
		return;
	case 38:
		if (FrontendProbeDefaultScreenshotPath.IsEmpty())
		{
			FailFrontendShippingProbe(TEXT("caption_pause_screenshot_argument_missing"));
			return;
		}
		if (!FPaths::FileExists(FrontendProbeDefaultScreenshotPath))
		{
			FrontendProbeNextActionTime = Now + 0.05;
			return;
		}
		if (AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
		{
			// 캡처 저장 시간과 무관하게 정확히 5초짜리 자막을 멈춘다.
			HorrorHUD->ShowAudioCaption(
				NSLOCTEXT("IGFrontendProbe", "SoundCaption", "[천장에서 세 번 두드리는 소리]"),
				5.0f);
			if (!HorrorHUD->HasPendingAudioCaption())
			{
				FailFrontendShippingProbe(TEXT("caption_pause_queue_empty"));
				return;
			}
		}
		else
		{
			FailFrontendShippingProbe(TEXT("caption_pause_hud_missing"));
			return;
		}
		SetSystemMenuMode(EIGSystemMenuMode::Pause);
		FrontendCaptionPausedAt = Now;
		FrontendProbeStep = 39;
		AwaitFrontendProbeFrame();
		return;
	case 39:
		if (!TryCaptureFrontendProbeLayout(TEXT("caption_pause"), 8, false, false))
		{
			return;
		}
		if (const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
		{
			if (!GetWorld()->IsPaused() || HorrorHUD->WasAudioCaptionDrawnInLastHudFrame()
				|| !HorrorHUD->HasPendingAudioCaption())
			{
				FailFrontendShippingProbe(TEXT("caption_pause_not_preserved"));
				return;
			}
		}
		else
		{
			FailFrontendShippingProbe(TEXT("caption_pause_hud_missing"));
			return;
		}
		FScreenshotRequest::RequestScreenshot(
			FPaths::GetPath(FrontendProbeDefaultScreenshotPath) / TEXT("caption-paused.png"), true, false);
		FrontendProbeStep = 40;
		// 원래 수명보다 길게 기다려, 일시정지 중 시계가 흐르는 결함을 잡는다.
		FrontendProbeNextActionTime = FMath::Max(Now, FrontendCaptionPausedAt + 6.0);
		FrontendProbeStepDeadline = FrontendProbeNextActionTime + 5.0;
		return;
	case 40:
		if (const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
		{
			if (!GetWorld()->IsPaused() || HorrorHUD->WasAudioCaptionDrawnInLastHudFrame()
				|| !HorrorHUD->HasPendingAudioCaption())
			{
				FailFrontendShippingProbe(TEXT("caption_pause_expired_or_drawn"));
				return;
			}
		}
		else
		{
			FailFrontendShippingProbe(TEXT("caption_pause_hud_missing"));
			return;
		}
		FrontendCaptionPauseSeconds = Now - FrontendCaptionPausedAt;
		SetSystemMenuMode(EIGSystemMenuMode::Hidden);
		FrontendProbeStep = 41;
		AwaitFrontendProbeFrame();
		FrontendProbeNextActionTime = Now + 0.30;
		return;
	case 41:
		if (!TryCaptureFrontendProbeLayout(TEXT("caption_resume"), 5, false, false))
		{
			return;
		}
		if (const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
		{
			if (GetWorld()->IsPaused() || !HorrorHUD->WasAudioCaptionDrawnInLastHudFrame()
				|| !HorrorHUD->HasPendingAudioCaption() || FrontendCaptionPauseSeconds < 6.0)
			{
				FailFrontendShippingProbe(TEXT("caption_resume_not_drawn"));
				return;
			}
		}
		else
		{
			FailFrontendShippingProbe(TEXT("caption_pause_hud_missing"));
			return;
		}
		FScreenshotRequest::RequestScreenshot(
			FPaths::GetPath(FrontendProbeDefaultScreenshotPath) / TEXT("caption-resumed.png"), true, false);
		if (!FFileHelper::SaveStringToFile(FString::Printf(
			TEXT("MISSINGFLOOR_CAPTION_PAUSE PASS pause_seconds=%.3f gameplay_draw=1 pause_draw=0 resume_draw=1 queue_preserved=1\n"),
			FrontendCaptionPauseSeconds),
			*(FPaths::GetPath(FrontendProbeDefaultScreenshotPath) / TEXT("caption-pause.txt")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			FailFrontendShippingProbe(TEXT("caption_pause_receipt_write_failed"));
			return;
		}
		bFrontendCaptionPauseVerified = true;
		FrontendProbeStep = 23;
		FrontendProbeNextActionTime = Now + 0.08;
		FrontendProbeStepDeadline = Now + 5.0;
		return;
	case 23:
		if (!FrontendProbeDefaultScreenshotPath.IsEmpty()
			&& !FPaths::FileExists(FrontendProbeDefaultScreenshotPath))
		{
			FrontendProbeNextActionTime = Now + 0.05;
			return;
		}
		if (UIGAccessibilitySubsystem* Accessibility =
			GetAccessibilitySubsystem())
		{
			FIGAccessibilitySettings Settings = Accessibility->GetSettings();
			Settings.CaptionSizeScale = 2.0f;
			Settings.CaptionBackgroundOpacity = 0.92f;
			Settings.CaptionSafeAreaScale = 0.80f;
			Accessibility->ApplySettings(Settings);
		}
		else
		{
			FailFrontendShippingProbe(TEXT("dialogue_max_accessibility_missing"));
			return;
		}
		if (AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
		{
			HorrorHUD->ShowDialogue(
				NSLOCTEXT("IGFrontendProbe", "DialogueSpeaker", "휴대폰"),
				NSLOCTEXT(
					"IGFrontendProbe",
					"DialogueLongKorean",
					"통화가 연결되지 않습니다. 천장에서 두드리는 소리가 세 번 났고, 같은 순간 휴대폰 알람이 네 시 반을 알렸습니다. 이 문장은 큰 글자에서도 잘리지 않고 다음 페이지로 이어져야 합니다. 플레이어가 이동 중이어도 앞 문장을 덮어쓰지 않아야 합니다."),
				EIGDialogueChannel::Device,
				5.0f,
				EIGDialoguePriority::Critical);
		}
		else
		{
			FailFrontendShippingProbe(TEXT("dialogue_max_hud_missing"));
			return;
		}
		FrontendProbeStep = 24;
		AwaitFrontendProbeFrame();
		FrontendProbeNextActionTime = Now + 0.30;
		return;
	case 24:
		if (!TryCaptureFrontendProbeLayout(
			TEXT("dialogue_max_scale"),
			8,
			false)
			|| !TryVerifyFrontendDialogueLayout(
				TEXT("dialogue_max"),
				3,
				3,
				true))
		{
			return;
		}
		bFrontendDialogueVerified = true;
		bFrontendDialogueSpeakerVerified = true;
		bFrontendDialogueContinuationVerified = true;
		if (FParse::Value(
			FCommandLine::Get(),
			TEXT("IGFrontendScreenshotPath="),
			FrontendProbeScreenshotPath))
		{
			FrontendProbeScreenshotPath.TrimQuotesInline();
			FrontendProbeScreenshotPath = FPaths::ConvertRelativePathToFull(
				FrontendProbeScreenshotPath);
			IFileManager::Get().MakeDirectory(
				*FPaths::GetPath(FrontendProbeScreenshotPath),
				true);
			FScreenshotRequest::RequestScreenshot(
				FrontendProbeScreenshotPath,
				true,
				false);
			FrontendProbeStep = 25;
			FrontendProbeNextActionTime = Now + 0.08;
			FrontendProbeStepDeadline = Now + 5.0;
			return;
		}
		CompleteFrontendShippingProbe();
		return;
	case 25:
		if (!FPaths::FileExists(FrontendProbeScreenshotPath))
		{
			FrontendProbeNextActionTime = Now + 0.05;
			return;
		}
		if (!FParse::Value(
			FCommandLine::Get(),
			TEXT("IGFrontendTitleScreenshotPath="),
			FrontendProbeTitleScreenshotPath))
		{
			FailFrontendShippingProbe(TEXT("title_screenshot_argument_missing"));
			return;
		}
		FrontendProbeTitleScreenshotPath.TrimQuotesInline();
		FrontendProbeTitleScreenshotPath = FPaths::ConvertRelativePathToFull(
			FrontendProbeTitleScreenshotPath);
		IFileManager::Get().MakeDirectory(
			*FPaths::GetPath(FrontendProbeTitleScreenshotPath),
			true);
		// 캡처 저장이 오래 걸려도 전환 검사가 빈 자막 큐로 시작하지 않게 한다.
		if (AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
		{
			HorrorHUD->ShowAudioCaption(
				NSLOCTEXT(
					"IGFrontendProbe",
					"SoundCaption",
					"[천장에서 세 번 두드리는 소리]"),
				5.0f);
			if (!HorrorHUD->HasPendingAudioCaption())
			{
				FailFrontendShippingProbe(TEXT("gameplay_sound_caption_queue_empty"));
				return;
			}
		}
		else
		{
			FailFrontendShippingProbe(TEXT("dialogue_hud_missing"));
			return;
		}
		SystemMenuSelection = 0;
		SetSystemMenuMode(EIGSystemMenuMode::Title);
		SetInputDevicePresentation(false);
		FrontendProbeStep = 26;
		AwaitFrontendProbeFrame();
		// The normal 320 ms alpha entrance is part of the visual contract. Capture
		// its settled state while reduced-motion runs remain instant.
		FrontendProbeNextActionTime = Now + 0.38;
		FrontendProbeStepDeadline = Now + 5.0;
		return;
	case 26:
		if (SystemMenuMode != EIGSystemMenuMode::Title
			|| bCompatibleAutosaveAvailable
			|| SystemMenuSelection != 1)
		{
			FailFrontendShippingProbe(TEXT("title_first_run_state"));
			return;
		}
		// 제목 하나와 네 메뉴의 글자·클릭 영역이 타이틀의 필수 요소다.
		if (!TryCaptureFrontendProbeLayout(TEXT("title_first_run"), 9))
		{
			return;
		}
		if (AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
		{
			// 앞 단계의 자막이 실제 타이틀 프레임과 대기열 모두에서 사라져야 한다.
			if (HorrorHUD->WasAudioCaptionDrawnInLastHudFrame()
				|| HorrorHUD->HasPendingAudioCaption())
			{
				FailFrontendShippingProbe(TEXT("title_retained_gameplay_sound_caption"));
				return;
			}
			HorrorHUD->ShowAudioCaption(
				NSLOCTEXT(
					"IGFrontendProbe",
					"SoundCaption",
					"[천장에서 세 번 두드리는 소리]"),
				5.0f);
			if (HorrorHUD->HasPendingAudioCaption())
			{
				FailFrontendShippingProbe(TEXT("title_accepted_sound_caption"));
				return;
			}
		}
		else
		{
			FailFrontendShippingProbe(TEXT("title_hud_missing"));
			return;
		}
		FScreenshotRequest::RequestScreenshot(
			FrontendProbeTitleScreenshotPath,
			true,
			false);
		FrontendProbeStep = 27;
		FrontendProbeNextActionTime = Now + 0.08;
		FrontendProbeStepDeadline = Now + 5.0;
		return;
	case 27:
		if (!FPaths::FileExists(FrontendProbeTitleScreenshotPath))
		{
			FrontendProbeNextActionTime = Now + 0.05;
			return;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("IGFrontendCopyReview")))
		{
			SetSystemMenuMode(EIGSystemMenuMode::ContentNotice);
			FrontendProbeStep = 28;
			FrontendProbeNextActionTime = Now + 0.5;
			FrontendProbeStepDeadline = Now + 8.0;
			return;
		}
		CompleteFrontendShippingProbe();
		return;
	case 28:
	case 30:
	case 32:
	case 34:
	{
		const TCHAR* Names[] = {TEXT("notice.png"), TEXT("audio-brightness.png"),
			TEXT("night-difficulty.png"), TEXT("key-settings.png")};
		const FString Path = FPaths::GetPath(FrontendProbeTitleScreenshotPath)
			/ Names[(FrontendProbeStep - 28) / 2];
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		++FrontendProbeStep;
		FrontendProbeNextActionTime = Now + 0.3;
		FrontendProbeStepDeadline = Now + 8.0;
		return;
	}
	case 29:
		OpenAudioCalibration(false);
		FrontendProbeStep = 30;
		FrontendProbeNextActionTime = Now + 0.5;
		return;
	case 31:
		ToggleAccessibilityMenu();
		AccessibilitySelection = IGSettingsMenuLayout::NightDifficulty;
		RefreshMenuHud();
		FrontendProbeStep = 32;
		FrontendProbeNextActionTime = Now + 0.5;
		return;
	case 33:
		CloseAccessibilityMenu();
		OpenKeyBindings();
		FrontendProbeStep = 34;
		FrontendProbeNextActionTime = Now + 0.5;
		return;
	case 35:
		for (const TCHAR* Name : {TEXT("notice.png"), TEXT("audio-brightness.png"),
			TEXT("night-difficulty.png"), TEXT("key-settings.png")})
		{
			if (IFileManager::Get().FileSize(*(FPaths::GetPath(FrontendProbeTitleScreenshotPath) / Name)) < 10000)
			{
				FailFrontendShippingProbe(TEXT("copy_review_screenshot_missing"));
				return;
			}
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("IGSettingsLayoutReview")))
		{
			FrontendSettingsReviewIndex = 0;
			FrontendSettingsReviewSamples = 0;
			PrepareFrontendSettingsReviewRow();
			FrontendProbeStep = 36;
			AwaitFrontendProbeFrame();
			return;
		}
		CompleteFrontendShippingProbe();
		return;
	case 36:
		if (!TryCaptureFrontendProbeLayout(TEXT("settings_review"), 8, false, false))
		{
			return;
		}
		++FrontendSettingsReviewSamples;
		// 키보드 화면은 증거로 저장한다. 패드 화면도 같은 글자 검사를 거친다.
		FrontendSettingsReviewScreenshotPath.Reset();
		if (FrontendSettingsReviewIndex % 2 == 0)
		{
			FrontendSettingsReviewScreenshotPath = FPaths::GetPath(FrontendProbeTitleScreenshotPath)
				/ FString::Printf(TEXT("settings-row-%02d.png"), FrontendSettingsReviewIndex / 2);
			FScreenshotRequest::RequestScreenshot(FrontendSettingsReviewScreenshotPath, true, false);
		}
		FrontendProbeStep = 37;
		FrontendProbeNextActionTime = Now + 0.08;
		FrontendProbeStepDeadline = Now + 8.0;
		return;
	case 37:
		if (!FrontendSettingsReviewScreenshotPath.IsEmpty()
			&& !FPaths::FileExists(FrontendSettingsReviewScreenshotPath))
		{
			FrontendProbeNextActionTime = Now + 0.05;
			return;
		}
		++FrontendSettingsReviewIndex;
		if (FrontendSettingsReviewIndex < 2 * (IGSettingsMenuLayout::DisplayRowCount
			+ 2 * IGSettingsMenuLayout::AccessibilityRowCount
			+ 2 * IGSettingsMenuLayout::AudioCalibrationRowCount + 2))
		{
			PrepareFrontendSettingsReviewRow();
			FrontendProbeStep = 36;
			AwaitFrontendProbeFrame();
			return;
		}
		{
			const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD());
			const FString Failures = HorrorHUD ? HorrorHUD->GetTextAuditFailureReport() : TEXT("HUD가 없습니다.");
			const FString Receipt = FString::Printf(
				TEXT("MISSINGFLOOR_SETTINGS_LAYOUT %s samples=%d text_failures=%d\n%s"),
				Failures.IsEmpty() ? TEXT("PASS") : TEXT("FAIL"),
				FrontendSettingsReviewSamples, Failures.IsEmpty() ? 0 : 1, *Failures);
			FFileHelper::SaveStringToFile(Receipt,
				*(FPaths::GetPath(FrontendProbeTitleScreenshotPath) / TEXT("settings-layout.txt")),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			if (!Failures.IsEmpty())
			{
				FailFrontendShippingProbe(TEXT("settings_text_audit"));
				return;
			}
		}
		CompleteFrontendShippingProbe();
		return;
	default:
		FailFrontendShippingProbe(TEXT("invalid_step"));
		return;
	}
}

void AIGPlayerController::PrepareFrontendSettingsReviewRow()
{
	const int32 ReviewRow = FrontendSettingsReviewIndex / 2;
	const bool bGamepad = FrontendSettingsReviewIndex % 2 != 0;
	const int32 AccessibilityStart = IGSettingsMenuLayout::DisplayRowCount;
	const int32 AudioStart = AccessibilityStart + 2 * IGSettingsMenuLayout::AccessibilityRowCount;
	const int32 KeysStart = AudioStart + 2 * IGSettingsMenuLayout::AudioCalibrationRowCount;
	if (bAccessibilityMenuVisible) CloseAccessibilityMenu();
	SetSystemMenuMode(EIGSystemMenuMode::Title);
	if (ReviewRow < AccessibilityStart)
	{
		OpenDisplaySettings();
		DisplaySettingsSelection = ReviewRow;
		DisplayFrameLimitIndex = 2;
	}
	else if (ReviewRow < AudioStart)
	{
		const int32 CaptionPass = (ReviewRow - AccessibilityStart) / IGSettingsMenuLayout::AccessibilityRowCount;
		if (UIGAccessibilitySubsystem* Accessibility = GetAccessibilitySubsystem())
		{
			FIGAccessibilitySettings Settings = Accessibility->GetSettings();
			Settings.CaptionSizeScale = CaptionPass == 0 ? 1.0f : 2.0f;
			Settings.CaptionSafeAreaScale = 0.80f;
			Accessibility->ApplySettings(Settings);
		}
		ToggleAccessibilityMenu();
		AccessibilitySelection = (ReviewRow - AccessibilityStart) % IGSettingsMenuLayout::AccessibilityRowCount;
		if (AccessibilitySelection == IGSettingsMenuLayout::ResetDefaults)
			AccessibilityResetArmedUntil = FPlatformTime::Seconds() + 4.0;
	}
	else if (ReviewRow < KeysStart)
	{
		OpenAudioCalibration(false);
		AudioCalibrationSelection = (ReviewRow - AudioStart) % IGSettingsMenuLayout::AudioCalibrationRowCount;
		bHeadphoneOutput = ReviewRow - AudioStart < IGSettingsMenuLayout::AudioCalibrationRowCount;
		bAudioCalibrationFirstRun = !bHeadphoneOutput;
	}
	else
	{
		OpenKeyBindings();
		KeyBindingSelection = UIGInputBindingSubsystem::LookRowCount;
		bKeyBindingColumnGamepad = ReviewRow != KeysStart;
		bKeyBindingCapturing = true;
	}
	SetInputDevicePresentation(bGamepad);
	RefreshMenuHud();
}

void AIGPlayerController::DispatchFrontendProbeKey(const FKey& Key)
{
	const FInputDeviceId DeviceId = FInputDeviceId::CreateFromInternalId(0);
	FInputKeyEventArgs Pressed(
		nullptr,
		DeviceId,
		Key,
		IE_Pressed,
		1.0f,
		false,
		FPlatformTime::Cycles64());
	InputKey(Pressed);
}

void AIGPlayerController::ReleaseFrontendProbeKey(const FKey& Key)
{
	const FInputDeviceId DeviceId = FInputDeviceId::CreateFromInternalId(0);
	FInputKeyEventArgs Released(
		nullptr,
		DeviceId,
		Key,
		IE_Released,
		0.0f,
		false,
		FPlatformTime::Cycles64());
	InputKey(Released);
}

void AIGPlayerController::AwaitFrontendProbeFrame()
{
	FrontendProbeAwaitFrameSerial = 0;
	if (const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
	{
		FVector2D CanvasSize;
		FVector2D BoundsMin;
		FVector2D BoundsMax;
		int32 ElementCount = 0;
		bool bAllInsideCanvas = false;
		bool bAllInsideSettingsContainers = false;
		HorrorHUD->GetLayoutValidationSample(
			CanvasSize,
			BoundsMin,
			BoundsMax,
			ElementCount,
			bAllInsideCanvas,
			bAllInsideSettingsContainers,
			FrontendProbeAwaitFrameSerial);
	}
	const double Now = FPlatformTime::Seconds();
	FrontendProbeNextActionTime = Now + 0.05;
	FrontendProbeStepDeadline = Now + 4.0;
}

bool AIGPlayerController::TryCaptureFrontendProbeLayout(
	const TCHAR* PanelName,
	const int32 MinimumElementCount,
	const bool bIncludeInMinimumElementCoverage,
	const bool bIncludeInReceiptCoverage)
{
	const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD());
	if (!HorrorHUD)
	{
		return false;
	}
	const UGameViewportClient* Viewport = GetLocalPlayer() ? GetLocalPlayer()->ViewportClient : nullptr;
	const bool bExpectedWorldSuspended = FCString::Strncmp(PanelName, TEXT("accessibility"), 13) == 0
		|| FCString::Strncmp(PanelName, TEXT("display"), 7) == 0
		|| FCString::Strcmp(PanelName, TEXT("settings_review")) == 0;
	if (!Viewport || Viewport->bDisableWorldRendering != bExpectedWorldSuspended)
	{
		FailFrontendShippingProbe(FString::Printf(TEXT("%s_world_rendering"), PanelName));
		return false;
	}
	FVector2D CanvasSize;
	FVector2D BoundsMin;
	FVector2D BoundsMax;
	int32 ElementCount = 0;
	bool bAllInsideCanvas = false;
	bool bAllInsideSettingsContainers = false;
	uint64 FrameSerial = 0;
	if (!HorrorHUD->GetLayoutValidationSample(
		CanvasSize,
		BoundsMin,
		BoundsMax,
		ElementCount,
		bAllInsideCanvas,
		bAllInsideSettingsContainers,
		FrameSerial)
		|| FrameSerial <= FrontendProbeAwaitFrameSerial + (bIncludeInReceiptCoverage ? 0 : 1))
	{
		return false;
	}
	if (FMath::Abs(CanvasSize.X - FrontendProbeExpectedWidth) > 1.0f
		|| FMath::Abs(CanvasSize.Y - FrontendProbeExpectedHeight) > 1.0f)
	{
		FailFrontendShippingProbe(FString::Printf(
			TEXT("%s_canvas_%dx%d"),
			PanelName,
			FMath::RoundToInt(CanvasSize.X),
			FMath::RoundToInt(CanvasSize.Y)));
		return false;
	}
	if (!bAllInsideCanvas)
	{
		FailFrontendShippingProbe(FString::Printf(
			TEXT("%s_overflow_%d_%d_%d_%d"),
			PanelName,
			FMath::RoundToInt(BoundsMin.X),
			FMath::RoundToInt(BoundsMin.Y),
			FMath::RoundToInt(BoundsMax.X),
			FMath::RoundToInt(BoundsMax.Y)));
		return false;
	}
	if (!bAllInsideSettingsContainers)
	{
		FailFrontendShippingProbe(FString::Printf(
			TEXT("%s_container_overflow"),
			PanelName));
		return false;
	}
	if (ElementCount < MinimumElementCount)
	{
		FailFrontendShippingProbe(FString::Printf(
			TEXT("%s_elements_%d"),
			PanelName,
			ElementCount));
		return false;
	}

	FrontendProbeBoundsMin.X = FMath::Min(
		FrontendProbeBoundsMin.X,
		BoundsMin.X);
	FrontendProbeBoundsMin.Y = FMath::Min(
		FrontendProbeBoundsMin.Y,
		BoundsMin.Y);
	FrontendProbeBoundsMax.X = FMath::Max(
		FrontendProbeBoundsMax.X,
		BoundsMax.X);
	FrontendProbeBoundsMax.Y = FMath::Max(
		FrontendProbeBoundsMax.Y,
		BoundsMax.Y);
	if (bIncludeInMinimumElementCoverage)
	{
		FrontendProbeMinimumElementCount = FMath::Min(
			FrontendProbeMinimumElementCount,
			ElementCount);
	}
	if (bIncludeInReceiptCoverage) ++FrontendProbeLayoutSampleCount;
	FrontendProbeAwaitFrameSerial = FrameSerial;
	return true;
}

bool AIGPlayerController::TryVerifyFrontendDialogueLayout(
	const TCHAR* CaseName,
	const int32 MinimumLineCount,
	const int32 MaximumLineCount,
	const bool bExpectedContinuation)
{
	const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD());
	if (!HorrorHUD)
	{
		FailFrontendShippingProbe(FString::Printf(
			TEXT("%s_hud_missing"),
			CaseName));
		return false;
	}

	FVector2D PanelMinimum;
	FVector2D PanelMaximum;
	FVector2D CanvasSize;
	int32 LineCount = 0;
	bool bSpeakerVisible = false;
	bool bHasContinuation = false;
	bool bInsideSafeArea = false;
	uint64 RenderSerial = 0;
	const bool bValid = HorrorHUD->GetDialogueRenderSample(
		PanelMinimum,
		PanelMaximum,
		CanvasSize,
		LineCount,
		bSpeakerVisible,
		bHasContinuation,
		bInsideSafeArea,
		RenderSerial)
		&& FMath::Abs(CanvasSize.X - FrontendProbeExpectedWidth) <= 1.0f
		&& FMath::Abs(CanvasSize.Y - FrontendProbeExpectedHeight) <= 1.0f
		&& LineCount >= MinimumLineCount
		&& LineCount <= MaximumLineCount
		&& bSpeakerVisible
		&& bHasContinuation == bExpectedContinuation
		&& bInsideSafeArea
		&& RenderSerial > 0;
	// 「이어짐」 표시가 마지막 줄 끝 글자를 덮으면 상자 크기와 줄 수가 맞아도
	// 읽을 수 없다. 자막 200%에서 실제로 그랬다.
	const bool bContinuationClear = HorrorHUD->IsDialogueContinuationClear();
	if (!bValid || !bContinuationClear)
	{
		FailFrontendShippingProbe(FString::Printf(
			TEXT("%s_lines_%d_speaker_%d_cont_%d_safe_%d_clear_%d"),
			CaseName,
			LineCount,
			bSpeakerVisible ? 1 : 0,
			bHasContinuation ? 1 : 0,
			bInsideSafeArea ? 1 : 0,
			bContinuationClear ? 1 : 0));
		return false;
	}
	return true;
}

void AIGPlayerController::CompleteFrontendShippingProbe()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("IGCaptionLifecycleReview"))
		&& !bFrontendCaptionPauseVerified)
	{
		FailFrontendShippingProbe(TEXT("caption_pause_not_verified"));
		return;
	}
	if (FrontendProbeLayoutSampleCount != 11
		|| FrontendProbeMinimumElementCount < 8
		|| FrontendProbePressedEventCount != 11
		|| !bFrontendDialogueDefaultVerified
		|| !bFrontendDialogueVerified
		|| !bFrontendDialogueSpeakerVerified
		|| !bFrontendDialogueContinuationVerified)
	{
		FailFrontendShippingProbe(FString::Printf(
			TEXT("coverage_samples_%d_elements_%d_inputs_%d"),
			FrontendProbeLayoutSampleCount,
			FrontendProbeMinimumElementCount,
			FrontendProbePressedEventCount));
		return;
	}
	const bool bReceiptWritten = WriteFrontendShippingProbeReceipt(
		true,
		TEXT("complete"));
	bFrontendShippingProbe = false;
	FPlatformMisc::RequestExitWithStatus(true, bReceiptWritten ? 0 : 3);
}

void AIGPlayerController::FailFrontendShippingProbe(const FString& Reason)
{
	WriteFrontendShippingProbeReceipt(false, Reason);
	bFrontendShippingProbe = false;
	FPlatformMisc::RequestExitWithStatus(true, 2);
}

void AIGPlayerController::StartAudioCalibrationPreviewProbe()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(
		CommandLine,
		TEXT("IGAudioCalibrationExpectedWidth="),
		AudioCalibrationPreviewExpectedWidth);
	FParse::Value(
		CommandLine,
		TEXT("IGAudioCalibrationExpectedHeight="),
		AudioCalibrationPreviewExpectedHeight);
	FParse::Value(
		CommandLine,
		TEXT("IGAudioCalibrationScreenshotPath="),
		AudioCalibrationPreviewScreenshotPath);
	AudioCalibrationPreviewScreenshotPath.TrimQuotesInline();
	AudioCalibrationPreviewScreenshotPath = FPaths::ConvertRelativePathToFull(
		AudioCalibrationPreviewScreenshotPath);
	bAudioCalibrationPreviewProbe = true;
	if (AudioCalibrationPreviewExpectedWidth <= 0
		|| AudioCalibrationPreviewExpectedHeight <= 0
		|| AudioCalibrationPreviewScreenshotPath.IsEmpty())
	{
		FailAudioCalibrationPreviewProbe(TEXT("arguments_missing"));
		return;
	}

	bAudioCalibrationPreviewScreenshotRequested = false;
	bAudioCalibrationPreviewCompilationDrained = false;
	bAccessibilityMenuVisible = false;
	AudioCalibrationVolumeStep = 4;
	AudioCalibrationBrightnessStep = 2;
	AudioCalibrationReturnMode = EIGSystemMenuMode::Title;
	bAudioCalibrationSessionActive = true;
	bAudioCalibrationFirstRun = true;
	AudioCalibrationSelection = 0;
	SetInputDevicePresentation(false);
	SetSystemMenuMode(EIGSystemMenuMode::AudioCalibration);
	ApplyAudioCalibrationValues();
	PlayAudioCalibrationKnock();
	IFileManager::Get().MakeDirectory(
		*FPaths::GetPath(AudioCalibrationPreviewScreenshotPath),
		true);
	const double Now = FPlatformTime::Seconds();
	AudioCalibrationPreviewNextActionTime = Now + 0.75;
	AudioCalibrationPreviewDeadline = Now + 8.0;
	SetActorTickEnabled(true);
}

void AIGPlayerController::TickAudioCalibrationPreviewProbe()
{
	const double Now = FPlatformTime::Seconds();
	if (Now > AudioCalibrationPreviewDeadline)
	{
		FailAudioCalibrationPreviewProbe(TEXT("timeout"));
		return;
	}
	if (Now < AudioCalibrationPreviewNextActionTime)
	{
		return;
	}
	if (!bAudioCalibrationPreviewCompilationDrained)
	{
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		if (GEngine)
		{
			GEngine->bEnableOnScreenDebugMessages = false;
		}
		ConsoleCommand(TEXT("DisableAllScreenMessages"), true);
		bAudioCalibrationPreviewCompilationDrained = true;
		AudioCalibrationPreviewNextActionTime = Now + 0.40;
		AudioCalibrationPreviewDeadline = Now + 8.0;
		return;
	}
	if (!bAudioCalibrationPreviewScreenshotRequested)
	{
		const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD());
		const UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
			? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
			: nullptr;
		FVector2D CanvasSize;
		FVector2D BoundsMinimum;
		FVector2D BoundsMaximum;
		int32 ElementCount = 0;
		bool bInsideCanvas = false;
		bool bInsideSettingsContainers = false;
		uint64 FrameSerial = 0;
		const bool bLayoutReady = HorrorHUD
			&& SystemMenuMode == EIGSystemMenuMode::AudioCalibration
			&& AudioDirector
			&& FMath::IsNearlyEqual(
				AudioDirector->GetUserMasterVolume(),
				IGAudioCalibration::VolumeValues[AudioCalibrationVolumeStep],
				0.001f)
			&& AudioDirector->GetCalibrationKnockPlayCount() >= 1
			&& HorrorHUD->GetLayoutValidationSample(
				CanvasSize,
				BoundsMinimum,
				BoundsMaximum,
				ElementCount,
				bInsideCanvas,
				bInsideSettingsContainers,
				FrameSerial)
			&& FMath::Abs(
				CanvasSize.X - AudioCalibrationPreviewExpectedWidth) <= 1.0f
			&& FMath::Abs(
				CanvasSize.Y - AudioCalibrationPreviewExpectedHeight) <= 1.0f
			&& bInsideCanvas
			&& ElementCount >= 1
			&& FrameSerial > 0;
		if (!bLayoutReady)
		{
			AudioCalibrationPreviewNextActionTime = Now + 0.05;
			return;
		}
		FScreenshotRequest::RequestScreenshot(
			AudioCalibrationPreviewScreenshotPath,
			true,
			false);
		bAudioCalibrationPreviewScreenshotRequested = true;
		AudioCalibrationPreviewNextActionTime = Now + 0.08;
		return;
	}
	if (!FPaths::FileExists(AudioCalibrationPreviewScreenshotPath))
	{
		AudioCalibrationPreviewNextActionTime = Now + 0.05;
		return;
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("MISSINGFLOOR_AUDIO_CALIBRATION_PREVIEW PASS "
			"resolution=%dx%d volume_step=%d brightness_step=%d "
			"master_gain=%.3f hrtf_knock=1 path=%s"),
		AudioCalibrationPreviewExpectedWidth,
		AudioCalibrationPreviewExpectedHeight,
		AudioCalibrationVolumeStep,
		AudioCalibrationBrightnessStep,
		IGAudioCalibration::VolumeValues[AudioCalibrationVolumeStep],
		*AudioCalibrationPreviewScreenshotPath);
	bAudioCalibrationPreviewProbe = false;
	FPlatformMisc::RequestExitWithStatus(true, 0);
}

void AIGPlayerController::FailAudioCalibrationPreviewProbe(
	const FString& Reason) const
{
	UE_LOG(
		LogTemp,
		Error,
		TEXT("MISSINGFLOOR_AUDIO_CALIBRATION_PREVIEW FAIL reason=%s"),
		*Reason);
	FPlatformMisc::RequestExitWithStatus(true, 2);
}

void AIGPlayerController::AddInputLock(
	const FName Reason,
	const bool bLockMove,
	const bool bLockLook)
{
	if (Reason.IsNone() || (!bLockMove && !bLockLook))
	{
		return;
	}
	InputLockReasons.Add(Reason, TPair<bool, bool>(bLockMove, bLockLook));
	ApplyInputLocks();
}

void AIGPlayerController::RemoveInputLock(const FName Reason)
{
	if (InputLockReasons.Remove(Reason) > 0)
	{
		ApplyInputLocks();
	}
}

FString AIGPlayerController::DescribeInputLocks() const
{
	if (InputLockReasons.IsEmpty())
	{
		return TEXT("none");
	}
	TArray<FString> Names;
	Names.Reserve(InputLockReasons.Num());
	for (const TPair<FName, TPair<bool, bool>>& Entry : InputLockReasons)
	{
		Names.Add(FString::Printf(
			TEXT("%s(%s%s)"),
			*Entry.Key.ToString(),
			Entry.Value.Key ? TEXT("move") : TEXT(""),
			Entry.Value.Value ? TEXT("+look") : TEXT("")));
	}
	Names.Sort();
	return FString::Join(Names, TEXT(", "));
}

void AIGPlayerController::ApplyInputLocks()
{
	bool bMove = false;
	bool bLook = false;
	for (const TPair<FName, TPair<bool, bool>>& Entry : InputLockReasons)
	{
		bMove |= Entry.Value.Key;
		bLook |= Entry.Value.Value;
	}
	// 카운터를 0으로 되돌린 뒤 필요한 만큼만 다시 올린다. 이렇게 해야 두 번
	// 걸거나 해제 순서가 엇갈려도 남은 이름과 실제 무시 상태가 어긋나지 않는다.
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();
	if (bMove)
	{
		SetIgnoreMoveInput(true);
	}
	if (bLook)
	{
		SetIgnoreLookInput(true);
	}
	if (InputLockReasons.IsEmpty())
	{
		InputLockWatchdogNextReportTime = 0.0;
	}
	else if (InputLockWatchdogNextReportTime <= 0.0)
	{
		// 잠금이 오래 남아 있으면 누가 붙들고 있는지 로그로 말한다. 조작이
		// 죽었는데 아무것도 안 찍히는 상태가 이 결함을 추적 불가능하게 만든다.
		InputLockWatchdogNextReportTime =
			FPlatformTime::Seconds() + IGInputLocks::WatchdogSeconds;
		SetActorTickEnabled(true);
	}
}

void AIGPlayerController::StartMissingFloorEndingPreviewProbe()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(
		CommandLine,
		TEXT("IGEndingPreviewExpectedWidth="),
		MissingFloorEndingPreviewExpectedWidth);
	FParse::Value(
		CommandLine,
		TEXT("IGEndingPreviewExpectedHeight="),
		MissingFloorEndingPreviewExpectedHeight);
	FParse::Value(
		CommandLine,
		TEXT("IGEndingPreviewScreenshotPath="),
		MissingFloorEndingPreviewScreenshotPath);
	FParse::Value(
		CommandLine,
		TEXT("IGEndingPreviewElapsed="),
		MissingFloorEndingPreviewElapsedSeconds);
	MissingFloorEndingPreviewScreenshotPath.TrimQuotesInline();
	MissingFloorEndingPreviewScreenshotPath = FPaths::ConvertRelativePathToFull(
		MissingFloorEndingPreviewScreenshotPath);
	MissingFloorEndingPreviewElapsedSeconds = FMath::Clamp(
		MissingFloorEndingPreviewElapsedSeconds,
		0.0f,
		10.0f);
	if (MissingFloorEndingPreviewExpectedWidth <= 0
		|| MissingFloorEndingPreviewExpectedHeight <= 0
		|| MissingFloorEndingPreviewScreenshotPath.IsEmpty())
	{
		FailMissingFloorEndingPreviewProbe(TEXT("arguments_missing"));
		return;
	}

	bMissingFloorEndingPreviewProbe = true;
	bMissingFloorEndingPreviewScreenshotRequested = false;
	bMissingFloorEndingPreviewCompilationDrained = false;
	SystemMenuMode = EIGSystemMenuMode::Hidden;
	bAccessibilityMenuVisible = false;
	bMissingFloorJournalVisible = false;
	SetInputDevicePresentation(false);
	ApplyMenuInputMode();
	RefreshMenuHud();
	IFileManager::Get().MakeDirectory(
		*FPaths::GetPath(MissingFloorEndingPreviewScreenshotPath),
		true);
	MissingFloorEndingPreviewNextActionTime = FPlatformTime::Seconds() + 0.75;
	MissingFloorEndingPreviewDeadline = FPlatformTime::Seconds() + 10.0;
	SetActorTickEnabled(true);
}

void AIGPlayerController::TickMissingFloorEndingPreviewProbe()
{
	const double Now = FPlatformTime::Seconds();
	if (Now > MissingFloorEndingPreviewDeadline)
	{
		FailMissingFloorEndingPreviewProbe(TEXT("timeout"));
		return;
	}
	if (Now < MissingFloorEndingPreviewNextActionTime)
	{
		return;
	}
	if (!bMissingFloorEndingPreviewCompilationDrained)
	{
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		if (GEngine)
		{
			GEngine->bEnableOnScreenDebugMessages = false;
		}
		ConsoleCommand(TEXT("DisableAllScreenMessages"), true);
		bMissingFloorEndingPreviewCompilationDrained = true;
		MissingFloorEndingPreviewNextActionTime = Now + 0.40;
		MissingFloorEndingPreviewDeadline = Now + 8.0;
		return;
	}

	AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD());
	if (!HorrorHUD)
	{
		FailMissingFloorEndingPreviewProbe(TEXT("hud_missing"));
		return;
	}
	if (!HorrorHUD->IsMissingFloorFailureEndingVisible())
	{
		HorrorHUD->BeginMissingFloorFailureEnding(
			MissingFloorEndingPreviewElapsedSeconds);
		HorrorHUD->SetMissingFloorFailureRetryEnabled(
			MissingFloorEndingPreviewElapsedSeconds >= 3.2f);
		MissingFloorEndingPreviewNextActionTime = Now + 0.10;
		return;
	}

	if (!bMissingFloorEndingPreviewScreenshotRequested)
	{
		FVector2D CanvasSize;
		FVector2D BoundsMinimum;
		FVector2D BoundsMaximum;
		int32 ElementCount = 0;
		bool bInsideCanvas = false;
		bool bInsideSettingsContainers = false;
		uint64 FrameSerial = 0;
		const int32 MinimumElementCount =
			MissingFloorEndingPreviewElapsedSeconds >= 3.2f ? 9 : 7;
		const bool bLayoutReady = HorrorHUD->GetLayoutValidationSample(
				CanvasSize,
				BoundsMinimum,
				BoundsMaximum,
				ElementCount,
				bInsideCanvas,
				bInsideSettingsContainers,
				FrameSerial)
			&& FMath::Abs(
				CanvasSize.X - MissingFloorEndingPreviewExpectedWidth) <= 1.0f
			&& FMath::Abs(
				CanvasSize.Y - MissingFloorEndingPreviewExpectedHeight) <= 1.0f
			&& bInsideCanvas
			&& ElementCount >= MinimumElementCount
			&& FrameSerial > 0;
		if (!bLayoutReady)
		{
			MissingFloorEndingPreviewNextActionTime = Now + 0.05;
			return;
		}
		FScreenshotRequest::RequestScreenshot(
			MissingFloorEndingPreviewScreenshotPath,
			true,
			false);
		bMissingFloorEndingPreviewScreenshotRequested = true;
		MissingFloorEndingPreviewNextActionTime = Now + 0.08;
		return;
	}
	if (!FPaths::FileExists(MissingFloorEndingPreviewScreenshotPath))
	{
		MissingFloorEndingPreviewNextActionTime = Now + 0.05;
		return;
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("MISSINGFLOOR_ENDING_PREVIEW PASS resolution=%dx%d elapsed=%.2f path=%s"),
		MissingFloorEndingPreviewExpectedWidth,
		MissingFloorEndingPreviewExpectedHeight,
		MissingFloorEndingPreviewElapsedSeconds,
		*MissingFloorEndingPreviewScreenshotPath);
	bMissingFloorEndingPreviewProbe = false;
	FPlatformMisc::RequestExitWithStatus(true, 0);
}

void AIGPlayerController::FailMissingFloorEndingPreviewProbe(
	const FString& Reason) const
{
	UE_LOG(
		LogTemp,
		Error,
		TEXT("MISSINGFLOOR_ENDING_PREVIEW FAIL reason=%s"),
		*Reason);
	FPlatformMisc::RequestExitWithStatus(true, 2);
}

bool AIGPlayerController::WriteFrontendShippingProbeReceipt(
	const bool bSuccess,
	const FString& Reason) const
{
	FString ResultPath;
	if (!FParse::Value(
		FCommandLine::Get(),
		TEXT("IGFrontendResultPath="),
		ResultPath))
	{
		return false;
	}
	ResultPath.TrimQuotesInline();
	if (ResultPath.IsEmpty())
	{
		return false;
	}
	const FString FullPath = FPaths::ConvertRelativePathToFull(ResultPath);
	IFileManager::Get().MakeDirectory(
		*FPaths::GetPath(FullPath),
		true);
	FString Receipt;
	if (bSuccess)
	{
		Receipt = FString::Printf(
			TEXT("MISSINGFLOOR_FRONTEND PASS contract=4 resolution=%dx%d ")
			TEXT("keyboard_access=1 gamepad_access=1 dpad_down=1 ")
			TEXT("keyboard_up=1 gamepad_close=1 keyboard_pause=1 ")
			TEXT("gamepad_pause=1 display=1 title=1 first_run=1 ")
			TEXT("dialogue=1 dialogue_default=1 ")
			TEXT("speaker=1 continuation=1 default_scale=100 max_scale=200 ")
			TEXT("sound_lane=1 ")
			TEXT("samples=%d elements_min=%d ")
			TEXT("input_events=%d bounds=%d,%d,%d,%d"),
			FrontendProbeExpectedWidth,
			FrontendProbeExpectedHeight,
			FrontendProbeLayoutSampleCount,
			FrontendProbeMinimumElementCount,
			FrontendProbePressedEventCount,
			FMath::RoundToInt(FrontendProbeBoundsMin.X),
			FMath::RoundToInt(FrontendProbeBoundsMin.Y),
			FMath::RoundToInt(FrontendProbeBoundsMax.X),
			FMath::RoundToInt(FrontendProbeBoundsMax.Y));
	}
	else
	{
		FString SafeReason = Reason;
		SafeReason.ReplaceInline(TEXT("\r"), TEXT("_"));
		SafeReason.ReplaceInline(TEXT("\n"), TEXT("_"));
		SafeReason.ReplaceInline(TEXT(" "), TEXT("_"));
		Receipt = FString::Printf(
			TEXT("MISSINGFLOOR_FRONTEND FAIL reason=%s step=%d samples=%d inputs=%d"),
			*SafeReason,
			FrontendProbeStep,
			FrontendProbeLayoutSampleCount,
			FrontendProbePressedEventCount);
	}
	return FFileHelper::SaveStringToFile(
		Receipt,
		*FullPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AIGPlayerController::ToggleSystemMenu()
{
	// 엔딩 크레딧 동안 Esc는 크레딧을 건너뛰는 키다. 일시정지를 열지 않는다.
	if (const AIGHorrorHUD* CreditsHUD = Cast<AIGHorrorHUD>(GetHUD());
		CreditsHUD && CreditsHUD->IsEndCreditsActive())
	{
		return;
	}
	if (bMissingFloorJournalVisible)
	{
		CloseMissingFloorJournal();
		return;
	}
	if (bAccessibilityMenuVisible)
	{
		CloseAccessibilityMenu();
		return;
	}

	switch (SystemMenuMode)
	{
	case EIGSystemMenuMode::Title:
		if (bNewGameConfirmationArmed)
		{
			bNewGameConfirmationArmed = false;
			RefreshMenuHud();
		}
		// The title is an intentional gate. A stray Esc must not start the story.
		return;
	case EIGSystemMenuMode::Credits:
		ReturnFromCredits();
		return;
	case EIGSystemMenuMode::ContentNotice:
		// 읽고 넘어가는 화면이라 확인과 취소가 같은 뜻이다. 뒤로 갈 데가 없다.
		DismissContentNotice();
		return;
	case EIGSystemMenuMode::KeyBindings:
		if (bKeyBindingCapturing)
		{
			// 대기 중의 취소는 화면을 닫는 것이 아니라 그 한 칸을 포기하는 것이다.
			bKeyBindingCapturing = false;
			KeyBindingStatusText = FText::GetEmpty();
			RefreshMenuHud();
			return;
		}
		CloseKeyBindings();
		return;
	case EIGSystemMenuMode::AudioCalibration:
		CancelAudioCalibration();
		return;
	case EIGSystemMenuMode::DisplaySettings:
		if (bDisplaySettingsAwaitingConfirmation)
		{
			RevertPendingDisplaySettings();
		}
		else
		{
			ReturnFromDisplaySettings();
		}
		return;
	case EIGSystemMenuMode::Pause:
		PlayMenuTick(false);
		SetSystemMenuMode(EIGSystemMenuMode::Hidden);
		return;
	case EIGSystemMenuMode::Hidden:
	default:
		SystemMenuSelection = 0;
		PlayMenuTick(false);
		SetSystemMenuMode(EIGSystemMenuMode::Pause);
		return;
	}
}

void AIGPlayerController::ToggleAccessibilityMenu()
{
	if (bMissingFloorJournalVisible)
	{
		CloseMissingFloorJournal();
		return;
	}
	if (bAccessibilityMenuVisible)
	{
		CloseAccessibilityMenu();
		return;
	}

	bAccessibilityMenuVisible = true;
	bNewGameConfirmationArmed = false;
	PlayMenuTick(false);
	AccessibilityReturnMode = SystemMenuMode;
	bGameWasPausedBeforeAccessibility =
		UGameplayStatics::IsGamePaused(this);
	SetPause(true);
	ApplyMenuInputMode();
	RefreshMenuHud();
}

void AIGPlayerController::CloseAccessibilityMenu()
{
	if (bMissingFloorJournalVisible)
	{
		CloseMissingFloorJournal();
		return;
	}
	if (!bAccessibilityMenuVisible)
	{
		if (SystemMenuMode == EIGSystemMenuMode::Credits)
		{
			ReturnFromCredits();
		}
		else if (SystemMenuMode == EIGSystemMenuMode::Pause)
		{
			SetSystemMenuMode(EIGSystemMenuMode::Hidden);
		}
		else if (SystemMenuMode == EIGSystemMenuMode::DisplaySettings)
		{
			if (bDisplaySettingsAwaitingConfirmation)
			{
				RevertPendingDisplaySettings();
			}
			else
			{
				ReturnFromDisplaySettings();
			}
		}
		else if (SystemMenuMode == EIGSystemMenuMode::AudioCalibration)
		{
			CancelAudioCalibration();
		}
		return;
	}
	bAccessibilityMenuVisible = false;
	const bool bReturnsToSystemMenu =
		AccessibilityReturnMode == EIGSystemMenuMode::Title
		|| AccessibilityReturnMode == EIGSystemMenuMode::Pause
		|| AccessibilityReturnMode == EIGSystemMenuMode::AudioCalibration
		|| AccessibilityReturnMode == EIGSystemMenuMode::DisplaySettings;
	SetPause(bReturnsToSystemMenu || bGameWasPausedBeforeAccessibility);
	AccessibilityReturnMode = EIGSystemMenuMode::Hidden;
	ApplyMenuInputMode();
	RefreshMenuHud();
}

void AIGPlayerController::BeginJournalInput()
{
	if (bMissingFloorJournalVisible)
	{
		CloseMissingFloorJournal();
		return;
	}
	if (bAccessibilityMenuVisible
		|| SystemMenuMode != EIGSystemMenuMode::Hidden)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AIGMissingFloorFifthDawnDirector> It(World); It; ++It)
		{
			if (It->BeginReplaySkipInput())
			{
				return;
			}
		}
		// 에필로그도 같은 손짓을 쓴다. 두 장면이 동시에 살아 있는 경로는
		// 없으므로 순서만 정하면 충돌하지 않는다.
		for (TActorIterator<AIGMissingFloorEpilogueDirector> It(World); It; ++It)
		{
			if (It->BeginReplaySkipInput())
			{
				return;
			}
		}
	}
	if (IsMissingFloorNight())
	{
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloorJournal",
				"NightDenied",
				"지금은 그럴 때가 아니야."),
			2.2f);
		return;
	}
	if (const UIGAccessibilitySubsystem* Accessibility =
		GetAccessibilitySubsystem())
	{
		if (Accessibility->UsesToggleHoldInteractions())
		{
			OpenMissingFloorJournal();
			return;
		}
	}

	bJournalInputHeld = true;
	JournalInputPressedAt = FPlatformTime::Seconds();
	SetActorTickEnabled(true);
}

void AIGPlayerController::EndJournalInput()
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AIGMissingFloorFifthDawnDirector> It(World); It; ++It)
		{
			if (It->EndReplaySkipInput())
			{
				return;
			}
		}
		for (TActorIterator<AIGMissingFloorEpilogueDirector> It(World); It; ++It)
		{
			if (It->EndReplaySkipInput())
			{
				return;
			}
		}
	}
	bJournalInputHeld = false;
}

void AIGPlayerController::OpenMissingFloorJournal()
{
	if (bMissingFloorJournalVisible || IsMissingFloorNight())
	{
		return;
	}

	bMissingFloorJournalVisible = true;
	UIGPlayRecordSubsystem::Note(this, TEXT("journal"));
	MissingFloorJournalPage = 0;
	bGameWasPausedBeforeJournal = UGameplayStatics::IsGamePaused(this);
	PlayMissingFloorJournalPaperSound(1.0f);
	SetPause(true);
	ApplyMenuInputMode();
	RefreshMenuHud();
}

void AIGPlayerController::CloseMissingFloorJournal()
{
	if (!bMissingFloorJournalVisible)
	{
		return;
	}

	bMissingFloorJournalVisible = false;
	bJournalInputHeld = false;
	PlayMissingFloorJournalPaperSound(0.72f);
	SetPause(bGameWasPausedBeforeJournal);
	ApplyMenuInputMode();
	RefreshMenuHud();
}

void AIGPlayerController::MoveMissingFloorJournalPage(const int32 Direction)
{
	if (AIGReadableNote::GetOpenNote() && SystemMenuMode == EIGSystemMenuMode::Hidden
		&& !bAccessibilityMenuVisible && !bMissingFloorJournalVisible)
	{
		if (AIGHorrorHUD* NoteHud = Cast<AIGHorrorHUD>(GetHUD())) NoteHud->MoveNotePage(Direction);
		return;
	}
	if (!bMissingFloorJournalVisible || Direction == 0)
	{
		return;
	}

	const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD());
	const int32 PageCount = HorrorHUD
		? FMath::Max(1, HorrorHUD->GetMissingFloorJournalPageCount())
		: 1;
	MissingFloorJournalPage =
		(MissingFloorJournalPage + PageCount + FMath::Sign(Direction)) % PageCount;
	PlayMissingFloorJournalPaperSound(0.58f);
	RefreshMenuHud();
}

void AIGPlayerController::MoveMissingFloorJournalPageLeft()
{
	MoveMissingFloorJournalPage(-1);
}

void AIGPlayerController::MoveMissingFloorJournalPageRight()
{
	MoveMissingFloorJournalPage(1);
}

bool AIGPlayerController::IsMissingFloorNight() const
{
	if (const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
	{
		if (HorrorHUD->IsNightPresentation())
		{
			return true;
		}
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGMissingFloorNarrativeSubsystem* Narrative = GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	return Narrative && Narrative->IsHourSealed();
}

void AIGPlayerController::PlayMissingFloorJournalPaperSound(
	const float VolumeMultiplier) const
{
	if (UIGToneSequenceSoundWave* Paper =
		UIGToneSequenceSoundWave::CreateJournalPageTurn(
			const_cast<AIGPlayerController*>(this)))
	{
		UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
			? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
			: nullptr;
		if (AudioDirector)
		{
			AudioDirector->PrepareSound(Paper, EIGAudioBus::UI);
		}
		UAudioComponent* PaperVoice = UGameplayStatics::SpawnSound2D(
			this,
			Paper,
			FMath::Clamp(VolumeMultiplier, 0.0f, 1.0f));
		if (AudioDirector)
		{
			AudioDirector->RegisterComponent(PaperVoice, EIGAudioBus::UI);
		}
	}
}

void AIGPlayerController::PlayMenuTick(const bool bConfirm) const
{
	// 메뉴의 손맛. 나무를 손톱으로 톡 치는 소리라 화면이 게임 밖으로 튀지
	// 않는다. 종이 소리와 같은 길로 UI 버스에 올려 일시정지 중에도 난다.
	UIGToneSequenceSoundWave* Tick = UIGToneSequenceSoundWave::CreateMenuTick(
		const_cast<AIGPlayerController*>(this), bConfirm);
	if (!Tick)
	{
		return;
	}
	UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
		? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
		: nullptr;
	if (AudioDirector)
	{
		AudioDirector->PrepareSound(Tick, EIGAudioBus::UI);
	}
	UAudioComponent* Voice = UGameplayStatics::SpawnSound2D(
		this, Tick, bConfirm ? 0.55f : 0.42f);
	if (AudioDirector && Voice)
	{
		AudioDirector->RegisterComponent(Voice, EIGAudioBus::UI);
	}
}

void AIGPlayerController::MoveAccessibilitySelectionUp()
{
	if (!bAccessibilityMenuVisible)
	{
		MoveSystemMenuSelection(-1);
		return;
	}
	AccessibilitySelection =
		(AccessibilitySelection + IGAccessibilityMenu::RowCount - 1)
		% IGAccessibilityMenu::RowCount;
	PlayMenuTick(false);
	RefreshMenuHud();
}

void AIGPlayerController::MoveAccessibilitySelectionDown()
{
	if (!bAccessibilityMenuVisible)
	{
		MoveSystemMenuSelection(1);
		return;
	}
	AccessibilitySelection =
		(AccessibilitySelection + 1) % IGAccessibilityMenu::RowCount;
	PlayMenuTick(false);
	RefreshMenuHud();
}

void AIGPlayerController::AdjustAccessibilityLeft()
{
	if (!bAccessibilityMenuVisible)
	{
		if (SystemMenuMode == EIGSystemMenuMode::DisplaySettings)
		{
			AdjustDisplaySetting(-1);
		}
		else if (SystemMenuMode == EIGSystemMenuMode::AudioCalibration)
		{
			AdjustAudioCalibrationSetting(-1);
		}
		else if (SystemMenuMode == EIGSystemMenuMode::KeyBindings)
		{
			// 좌우는 값이 아니라 칸을 고른다. 키보드와 패드를 같은 화면에
			// 나란히 두고, 어느 쪽을 바꾸는지 손이 먼저 알게 한다.
			MoveKeyBindingColumn(-1);
		}
		return;
	}
	ChangeAccessibilitySetting(-1, false);
}

void AIGPlayerController::AdjustAccessibilityRight()
{
	if (!bAccessibilityMenuVisible)
	{
		if (SystemMenuMode == EIGSystemMenuMode::DisplaySettings)
		{
			AdjustDisplaySetting(1);
		}
		else if (SystemMenuMode == EIGSystemMenuMode::AudioCalibration)
		{
			AdjustAudioCalibrationSetting(1);
		}
		else if (SystemMenuMode == EIGSystemMenuMode::KeyBindings)
		{
			// 좌우는 값이 아니라 칸을 고른다. 키보드와 패드를 같은 화면에
			// 나란히 두고, 어느 쪽을 바꾸는지 손이 먼저 알게 한다.
			MoveKeyBindingColumn(1);
		}
		return;
	}
	ChangeAccessibilitySetting(1, false);
}

void AIGPlayerController::ConfirmAccessibilitySelection()
{
	if (!bAccessibilityMenuVisible)
	{
		ConfirmSystemMenuSelection();
		return;
	}
	ChangeAccessibilitySetting(1, true);
}

void AIGPlayerController::RequestManualHint()
{
	if (bAccessibilityMenuVisible
		|| SystemMenuMode != EIGSystemMenuMode::Hidden
		|| !GetWorld())
	{
		return;
	}
	// 힌트는 화면의 정답 칸이 아니라 유담의 속말이다(§19.4). 누를 때마다 지금
	// 걸린 자리를 한 단계씩 더 구체적으로 떠올린다. 진행이 바뀌면 처음부터다.
	// 설정에서 힌트를 끈 사람에게는 아무것도 하지 않는다.
	const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD());
	if (!HorrorHUD || !HorrorHUD->IsHintRequestAvailable() || !HorrorHUD->CanShowGameplayGuide())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastHintRequestTime < 0.8)
	{
		return;
	}
	LastHintRequestTime = Now;
	const FIGHintStep Step = IGMissingFloorHints::Resolve(this);
	if (!Step.IsValid())
	{
		return;
	}
	UIGPlayRecordSubsystem::Note(this, TEXT("hint"));
	if (Step.GoalId != HintGoalId)
	{
		HintGoalId = Step.GoalId;
		HintTier = 0;
	}
	const int32 Tier = FMath::Clamp(HintTier, 0, Step.Tiers.Num() - 1);
	AIGHorrorHUD::PushThought(this, Step.Tiers[Tier], 3.2f);
	HintTier = FMath::Min(Tier + 1, Step.Tiers.Num() - 1);
}

void AIGPlayerController::ChangeAccessibilitySetting(
	const int32 Direction,
	const bool bConfirm)
{
	if (!bAccessibilityMenuVisible)
	{
		return;
	}
	UIGAccessibilitySubsystem* Accessibility = GetAccessibilitySubsystem();
	if (!Accessibility)
	{
		return;
	}

	if (AccessibilitySelection == IGSettingsMenuLayout::ResetDefaults)
	{
		// 난이도까지 되돌리는 누름이다. 한 번 누르면 4초 동안 한 번 더 누르기를 기다린다.
		// 메뉴가 열려 있으면 게임 시간이 멈추므로 실제 시계로 잰다.
		const double Now = FPlatformTime::Seconds();
		if (bConfirm && Now >= AccessibilityResetArmedUntil)
		{
			AccessibilityResetArmedUntil = Now + 4.0;
			RefreshMenuHud();
			return;
		}
		if (bConfirm)
		{
			AccessibilityResetArmedUntil = -1.0;
			Accessibility->ResetToDefaults();
			IGAccessibilityMenu::ApplyNightDifficulty(GetWorld(), EIGNightDifficulty::Standard);
			if (AIGPlayerCharacter* PlayerCharacter =
				Cast<AIGPlayerCharacter>(GetPawn()))
			{
				PlayerCharacter->RefreshMicrophoneCaptureMode();
				PlayerCharacter->RefreshFieldOfView();
				PlayerCharacter->RefreshCameraTexture();
			}
		}
		RefreshMenuHud();
		return;
	}
	if (AccessibilitySelection == IGSettingsMenuLayout::CloseMenu)
	{
		if (bConfirm)
		{
			CloseAccessibilityMenu();
		}
		return;
	}

	FIGAccessibilitySettings Settings = Accessibility->GetSettings();
	switch (AccessibilitySelection)
	{
	case IGSettingsMenuLayout::NightDifficulty:
	{
		constexpr int32 ModeCount = static_cast<int32>(EIGNightDifficulty::Count);
		const int32 Current = static_cast<int32>(IGListenerTuning::LoadPersistedDifficulty());
		const EIGNightDifficulty Difficulty = IGListenerTuning::ClampDifficulty(
			(Current + (Direction < 0 ? ModeCount - 1 : 1)) % ModeCount);
		IGAccessibilityMenu::ApplyNightDifficulty(GetWorld(), Difficulty);
		break;
	}
	case IGSettingsMenuLayout::Hints:
		Settings.bHintsEnabled = !Settings.bHintsEnabled;
		break;
	case IGSettingsMenuLayout::ReducedCameraMotion:
		Settings.bReducedCameraMotion = !Settings.bReducedCameraMotion;
		break;
	case IGSettingsMenuLayout::ReducedFlicker:
		Settings.bReducedFlicker = !Settings.bReducedFlicker;
		break;
	case IGSettingsMenuLayout::FieldOfView:
		Settings.FieldOfViewDegrees = FMath::Clamp(
			Settings.FieldOfViewDegrees + (Direction < 0 ? -2.0f : 2.0f),
			68.0f,
			100.0f);
		break;
	case IGSettingsMenuLayout::ComfortVignette:
		Settings.ComfortVignetteStrength = FMath::Clamp(
			Settings.ComfortVignetteStrength + (Direction < 0 ? -0.25f : 0.25f),
			0.0f,
			1.0f);
		break;
	case IGSettingsMenuLayout::CameraTexture:
		Settings.CameraTextureStrength = FMath::Clamp(
			Settings.CameraTextureStrength + (Direction < 0 ? -0.25f : 0.25f),
			0.0f,
			1.0f);
		break;
	case IGSettingsMenuLayout::CenterDot:
		Settings.bAlwaysShowCenterDot = !Settings.bAlwaysShowCenterDot;
		break;
	case IGSettingsMenuLayout::DirectionalFearCues:
		Settings.bDirectionalFearCues = !Settings.bDirectionalFearCues;
		break;
	case IGSettingsMenuLayout::KnockRippleSubstitute:
		Settings.bKnockRippleSubstitute = !Settings.bKnockRippleSubstitute;
		break;
	case IGSettingsMenuLayout::KnockHapticSubstitute:
		Settings.bKnockHapticSubstitute = !Settings.bKnockHapticSubstitute;
		break;
	case IGSettingsMenuLayout::HeartbeatWarning:
		Settings.bHeartbeatWarning = !Settings.bHeartbeatWarning;
		break;
	case IGSettingsMenuLayout::CognitiveAssist:
		Settings.bCognitiveAssist = !Settings.bCognitiveAssist;
		break;
	case IGSettingsMenuLayout::SoundCaptions:
		Settings.bSoundCaptionsEnabled = !Settings.bSoundCaptionsEnabled;
		break;
	case IGSettingsMenuLayout::CaptionSize:
		Settings.CaptionSizeScale = FMath::Clamp(
			Settings.CaptionSizeScale + (Direction < 0 ? -0.10f : 0.10f),
			0.85f,
			2.0f);
		break;
	case IGSettingsMenuLayout::CaptionBackground:
		Settings.CaptionBackgroundOpacity = FMath::Clamp(
			Settings.CaptionBackgroundOpacity
				+ (Direction < 0 ? -0.10f : 0.10f),
			0.0f,
			1.0f);
		break;
	case IGSettingsMenuLayout::CaptionSafeArea:
		Settings.CaptionSafeAreaScale = FMath::Clamp(
			Settings.CaptionSafeAreaScale + (Direction < 0 ? -0.05f : 0.05f),
			0.80f,
			1.0f);
		break;
	case IGSettingsMenuLayout::CaptionDuration:
		Settings.CaptionDurationScale = FMath::Clamp(
			Settings.CaptionDurationScale + (Direction < 0 ? -0.25f : 0.25f),
			0.75f,
			2.0f);
		break;
	case IGSettingsMenuLayout::ToggleCrouch:
		Settings.bToggleCrouch = !Settings.bToggleCrouch;
		break;
	case IGSettingsMenuLayout::ToggleHold:
		Settings.bToggleHoldInteractions = !Settings.bToggleHoldInteractions;
		break;
	case IGSettingsMenuLayout::HoldDuration:
		Settings.HoldDurationScale = FMath::Clamp(
			Settings.HoldDurationScale + (Direction < 0 ? -0.25f : 0.25f),
			0.25f,
			1.0f);
		break;
	case IGSettingsMenuLayout::PromptKeys:
		Settings.bAlwaysShowPromptKeys = !Settings.bAlwaysShowPromptKeys;
		break;
	case IGSettingsMenuLayout::Haptics:
		Settings.bHapticsEnabled = !Settings.bHapticsEnabled;
		break;
	case IGSettingsMenuLayout::MicrophoneNoise:
		Settings.bMicrophoneNoiseEnabled = !Settings.bMicrophoneNoiseEnabled;
		break;
	default:
		return;
	}
	Accessibility->ApplySettings(Settings);
	if (AIGPlayerCharacter* PlayerCharacter =
		Cast<AIGPlayerCharacter>(GetPawn()))
	{
		PlayerCharacter->RefreshMicrophoneCaptureMode();
		PlayerCharacter->RefreshFieldOfView();
		PlayerCharacter->RefreshCameraTexture();
	}
	RefreshMenuHud();
}

void AIGPlayerController::MoveSystemMenuSelection(const int32 Direction)
{
	if (SystemMenuMode == EIGSystemMenuMode::DisplaySettings)
	{
		MoveDisplaySettingsSelection(Direction);
		return;
	}
	if (SystemMenuMode == EIGSystemMenuMode::AudioCalibration)
	{
		MoveAudioCalibrationSelection(Direction);
		return;
	}
	if (SystemMenuMode == EIGSystemMenuMode::KeyBindings)
	{
		MoveKeyBindingSelection(Direction);
		return;
	}
	if (SystemMenuMode == EIGSystemMenuMode::Hidden
		|| SystemMenuMode == EIGSystemMenuMode::Credits
		|| Direction == 0)
	{
		return;
	}
	if (bNightFivePlaying)
	{
		// 검정 화면 뒤의 메뉴를 더듬게 두지 않는다.
		return;
	}
	bNewGameConfirmationArmed = false;
	SystemMenuStatusText = FText::GetEmpty();
	bSystemMenuStatusIsError = false;

	// 화면 순서로 움직인다. 액션 인덱스 순서는 디스패치의 것이고, 위아래
	// 키는 눈의 것이다 — 밤 5는 액션 목록의 마지막이지만 화면에서는 이어하기
	// 바로 밑에 있으므로, 액션 순서로 돌면 선택이 화면을 건너뛴다.
	const bool bTitleMenu = SystemMenuMode == EIGSystemMenuMode::Title;
	const int32 VisibleCount = IGFrontendMenuLayout::GetVisibleActionCount(
		bTitleMenu,
		bCompatibleAutosaveAvailable,
		bNightFiveAvailable);
	if (VisibleCount <= 0)
	{
		return;
	}
	int32 VisibleSlot = IGFrontendMenuLayout::GetVisibleSlotForAction(
		SystemMenuSelection,
		bTitleMenu,
		bCompatibleAutosaveAvailable,
		bNightFiveAvailable);
	if (VisibleSlot == INDEX_NONE)
	{
		VisibleSlot = 0;
	}
	const int32 PreviousSelection = SystemMenuSelection;
	for (int32 Attempt = 0; Attempt < VisibleCount; ++Attempt)
	{
		VisibleSlot =
			(VisibleSlot + (Direction < 0 ? VisibleCount - 1 : 1)) % VisibleCount;
		const int32 Candidate = IGFrontendMenuLayout::GetActionForVisibleSlot(
			VisibleSlot,
			bTitleMenu,
			bCompatibleAutosaveAvailable,
			bNightFiveAvailable);
		if (Candidate != INDEX_NONE && IsSystemMenuRowEnabled(Candidate))
		{
			SystemMenuSelection = Candidate;
			break;
		}
	}
	if (SystemMenuSelection != PreviousSelection)
	{
		PlayMenuTick(false);
	}
	RefreshMenuHud();
}

void AIGPlayerController::ConfirmSystemMenuSelection()
{
	if (bNightFivePlaying)
	{
		// 30초를 끝까지 들을 의무는 없다. 어떤 확인 입력이든 타이틀로 돌린다.
		EndNightFive();
		return;
	}
	if (SystemMenuMode == EIGSystemMenuMode::Hidden)
	{
		return;
	}
	PlayMenuTick(true);
	if (SystemMenuMode == EIGSystemMenuMode::Credits)
	{
		ReturnFromCredits();
		return;
	}
	if (SystemMenuMode == EIGSystemMenuMode::ContentNotice)
	{
		DismissContentNotice();
		return;
	}
	if (SystemMenuMode == EIGSystemMenuMode::KeyBindings)
	{
		ConfirmKeyBindingSelection();
		return;
	}
	if (SystemMenuMode == EIGSystemMenuMode::DisplaySettings)
	{
		ConfirmDisplaySettingsSelection();
		return;
	}
	if (SystemMenuMode == EIGSystemMenuMode::AudioCalibration)
	{
		ConfirmAudioCalibrationSelection();
		return;
	}
	if (!IsSystemMenuRowEnabled(SystemMenuSelection))
	{
		return;
	}

	if (SystemMenuSelection == 0)
	{
		if (SystemMenuMode == EIGSystemMenuMode::Title)
		{
			ContinueLatestAutosave();
		}
		else
		{
			SetSystemMenuMode(EIGSystemMenuMode::Hidden);
		}
		return;
	}
	if (SystemMenuSelection == 1)
	{
		if (SystemMenuMode == EIGSystemMenuMode::Title)
		{
			if (bCompatibleAutosaveAvailable && !bNewGameConfirmationArmed)
			{
				bNewGameConfirmationArmed = true;
				RefreshMenuHud();
				return;
			}
			StartNewGame();
		}
		else if (IsMissingFloorNight())
		{
			// F9와 같은 잠금이다. 밤을 되감으면 잡힌 값과 놓친 밤의 값이 전부 사라진다.
			SystemMenuStatusText = NSLOCTEXT(
				"IGFrontend",
				"LoadLockedAtNight",
				"밤이 지나가기 전에는 불러올 수 없습니다.");
			bSystemMenuStatusIsError = true;
			RefreshMenuHud();
		}
		else
		{
			ContinueLatestAutosave();
		}
		return;
	}
	if (SystemMenuSelection == IGFrontendMenuLayout::NightFiveAction)
	{
		PlayNightFive();
		return;
	}
	if (SystemMenuSelection == 2)
	{
		OpenDisplaySettings();
		return;
	}
	if (SystemMenuSelection == 3)
	{
		CreditsReturnMode = SystemMenuMode;
		SetSystemMenuMode(EIGSystemMenuMode::Credits);
		return;
	}
	QuitToDesktop();
}

void AIGPlayerController::SetSystemMenuMode(const EIGSystemMenuMode NewMode)
{
	SystemMenuMode = NewMode;
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			const bool bKeepTitleSoundscape =
				NewMode == EIGSystemMenuMode::Title
				|| (NewMode == EIGSystemMenuMode::DisplaySettings
					&& DisplaySettingsReturnMode == EIGSystemMenuMode::Title)
				|| (NewMode == EIGSystemMenuMode::Credits
					&& CreditsReturnMode == EIGSystemMenuMode::Title);
			AudioDirector->SetTitleMode(bKeepTitleSoundscape);
		}
	}
	if (NewMode != EIGSystemMenuMode::Title)
	{
		bNewGameConfirmationArmed = false;
		bHeadphoneRecommendationVisible = false;
	}
	if (NewMode != EIGSystemMenuMode::DisplaySettings)
	{
		bDisplaySettingsApplied = false;
	}
	if (NewMode == EIGSystemMenuMode::Title
		|| NewMode == EIGSystemMenuMode::Pause)
	{
		bCompatibleAutosaveAvailable = HasCompatibleAutosave();
	bNightFiveAvailable = HasEndingBAutosave();
		const int32 LoadRow = NewMode == EIGSystemMenuMode::Title ? 0 : 1;
		if (!bCompatibleAutosaveAvailable && SystemMenuSelection == LoadRow)
		{
			SystemMenuSelection = (LoadRow + 1) % IGSystemMenu::RowCount;
		}
	}
	SetPause(NewMode != EIGSystemMenuMode::Hidden);
	ApplyMenuInputMode();
	RefreshMenuHud();
}

void AIGPlayerController::StartHeadphoneRecommendationIfNeeded()
{
	if (!IsLocalController()
		|| SystemMenuMode != EIGSystemMenuMode::Title
		|| FParse::Param(FCommandLine::Get(), TEXT("IGFrontendShippingProbe"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGAudioCalibrationPreview"))
		// 밤 5 검증은 타이틀 목록 그 자체를 검사한다. 첫 실행 온보딩이 메뉴를
		// 가져가면 어떤 행도 선택 가능하지 않다.
		|| FParse::Param(FCommandLine::Get(), TEXT("IGNightFiveProbe")))
	{
		return;
	}

	constexpr const TCHAR* Section = TEXT("IndieGame.AudioOnboarding");
	bool bAlreadyShown = false;
	if (GConfig)
	{
		GConfig->GetBool(
			Section,
			TEXT("HeadphoneRecommendationShown"),
			bAlreadyShown,
			GGameUserSettingsIni);
	}
	if (bAlreadyShown)
	{
		if (!bAudioCalibrationCompleted)
		{
			OpenAudioCalibration(true);
		}
		return;
	}

	// 첫 실행의 첫 소리는 보정 노크다. 이 문장 뒤에 곧 소리 맞추기로 넘어가므로
	// 타이틀 음악을 올렸다가 2.5초 만에 자르지 않는다. 보정을 마치고 타이틀로
	// 돌아올 때 처음 올린다.
	if (!bAudioCalibrationCompleted)
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
			? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
			: nullptr)
		{
			AudioDirector->SetTitleMode(false);
		}
	}
	bHeadphoneRecommendationVisible = true;
	HeadphoneRecommendationDeadline = FPlatformTime::Seconds() + 2.5;
	SetActorTickEnabled(true);
	RefreshMenuHud();
}

void AIGPlayerController::DismissHeadphoneRecommendation()
{
	if (!bHeadphoneRecommendationVisible)
	{
		return;
	}
	bHeadphoneRecommendationVisible = false;
	if (GConfig)
	{
		GConfig->SetBool(
			IGAudioCalibration::ConfigSection,
			TEXT("HeadphoneRecommendationShown"),
			true,
			GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	if (!bAudioCalibrationCompleted)
	{
		OpenAudioCalibration(true);
	}
	else
	{
		RefreshMenuHud();
	}
}

void AIGPlayerController::LoadAudioCalibrationSettings()
{
	if (GConfig)
	{
		GConfig->GetBool(
			IGAudioCalibration::ConfigSection,
			TEXT("CalibrationCompleted"),
			bAudioCalibrationCompleted,
			GGameUserSettingsIni);
		GConfig->GetInt(
			IGAudioCalibration::ConfigSection,
			TEXT("VolumeStep"),
			AudioCalibrationVolumeStep,
			GGameUserSettingsIni);
		GConfig->GetInt(
			IGAudioCalibration::ConfigSection,
			TEXT("BrightnessStep"),
			AudioCalibrationBrightnessStep,
			GGameUserSettingsIni);
		GConfig->GetBool(
			IGAudioCalibration::ConfigSection,
			TEXT("HeadphoneOutput"),
			bHeadphoneOutput,
			GGameUserSettingsIni);
		GConfig->GetInt(
			IGAudioCalibration::ConfigSection,
			TEXT("MusicStep"),
			AudioCalibrationMusicStep,
			GGameUserSettingsIni);
		GConfig->GetInt(
			IGAudioCalibration::ConfigSection,
			TEXT("AmbienceStep"),
			AudioCalibrationAmbienceStep,
			GGameUserSettingsIni);
	}
	AudioCalibrationMusicStep = FMath::Clamp(
		AudioCalibrationMusicStep, 0, IGAudioCalibration::MusicStepCount - 1);
	AudioCalibrationAmbienceStep = FMath::Clamp(
		AudioCalibrationAmbienceStep,
		0,
		IGAudioCalibration::AmbienceStepCount - 1);
	AudioCalibrationVolumeStep = FMath::Clamp(
		AudioCalibrationVolumeStep,
		0,
		IGAudioCalibration::VolumeStepCount - 1);
	AudioCalibrationBrightnessStep = FMath::Clamp(
		AudioCalibrationBrightnessStep,
		0,
		IGAudioCalibration::BrightnessStepCount - 1);
	ApplyAudioCalibrationValues();
}

void AIGPlayerController::OpenAudioCalibration(const bool bFirstRun)
{
	if (!bAudioCalibrationSessionActive)
	{
		AudioCalibrationReturnMode =
			SystemMenuMode == EIGSystemMenuMode::Pause
				? EIGSystemMenuMode::Pause
				: SystemMenuMode == EIGSystemMenuMode::DisplaySettings
					? EIGSystemMenuMode::DisplaySettings
					: EIGSystemMenuMode::Title;
		PreviousAudioCalibrationVolumeStep = AudioCalibrationVolumeStep;
		PreviousAudioCalibrationBrightnessStep = AudioCalibrationBrightnessStep;
		bPreviousHeadphoneOutput = bHeadphoneOutput;
		PreviousAudioCalibrationMusicStep = AudioCalibrationMusicStep;
		PreviousAudioCalibrationAmbienceStep = AudioCalibrationAmbienceStep;
		bAudioCalibrationSessionActive = true;
	}
	bAudioCalibrationFirstRun = bFirstRun;
	AudioCalibrationSelection = 0;
	SystemMenuStatusText = FText::GetEmpty();
	bSystemMenuStatusIsError = false;
	SetSystemMenuMode(EIGSystemMenuMode::AudioCalibration);
	NextAudioCalibrationKnockTime = FPlatformTime::Seconds() + 0.35;
	SetActorTickEnabled(true);
}

void AIGPlayerController::CancelAudioCalibration()
{
	if (UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
		? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
		: nullptr)
	{
		AudioDirector->StopCalibrationPreview();
	}
	if (!bAudioCalibrationSessionActive)
	{
		SetSystemMenuMode(AudioCalibrationReturnMode);
		return;
	}
	AudioCalibrationVolumeStep = PreviousAudioCalibrationVolumeStep;
	AudioCalibrationBrightnessStep = PreviousAudioCalibrationBrightnessStep;
	bHeadphoneOutput = bPreviousHeadphoneOutput;
	AudioCalibrationMusicStep = PreviousAudioCalibrationMusicStep;
	AudioCalibrationAmbienceStep = PreviousAudioCalibrationAmbienceStep;
	ApplyAudioCalibrationValues();
	// 첫 실행에서 Esc로 나간 것은 건너뛰겠다는 선택이다. 기록해 두지 않으면
	// 켤 때마다 보정 화면이 다시 떴다. 보정은 설정에서 언제든 다시 연다.
	if (bAudioCalibrationFirstRun && !bAudioCalibrationCompleted)
	{
		bAudioCalibrationCompleted = true;
		if (GConfig)
		{
			GConfig->SetBool(
				IGAudioCalibration::ConfigSection,
				TEXT("CalibrationCompleted"),
				true,
				GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
	}
	bAudioCalibrationSessionActive = false;
	bAudioCalibrationFirstRun = false;
	NextAudioCalibrationKnockTime = -1.0;
	SetSystemMenuMode(AudioCalibrationReturnMode);
}

void AIGPlayerController::CompleteAudioCalibration()
{
	if (UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
		? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
		: nullptr)
	{
		AudioDirector->StopCalibrationPreview();
	}
	bAudioCalibrationCompleted = true;
	if (GConfig)
	{
		GConfig->SetBool(
			IGAudioCalibration::ConfigSection,
			TEXT("CalibrationCompleted"),
			true,
			GGameUserSettingsIni);
		GConfig->SetInt(
			IGAudioCalibration::ConfigSection,
			TEXT("VolumeStep"),
			AudioCalibrationVolumeStep,
			GGameUserSettingsIni);
		GConfig->SetInt(
			IGAudioCalibration::ConfigSection,
			TEXT("BrightnessStep"),
			AudioCalibrationBrightnessStep,
			GGameUserSettingsIni);
		GConfig->SetBool(
			IGAudioCalibration::ConfigSection,
			TEXT("HeadphoneOutput"),
			bHeadphoneOutput,
			GGameUserSettingsIni);
		GConfig->SetInt(
			IGAudioCalibration::ConfigSection,
			TEXT("MusicStep"),
			AudioCalibrationMusicStep,
			GGameUserSettingsIni);
		GConfig->SetInt(
			IGAudioCalibration::ConfigSection,
			TEXT("AmbienceStep"),
			AudioCalibrationAmbienceStep,
			GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	bAudioCalibrationSessionActive = false;
	bAudioCalibrationFirstRun = false;
	NextAudioCalibrationKnockTime = -1.0;
	SetSystemMenuMode(AudioCalibrationReturnMode);
}

void AIGPlayerController::MoveAudioCalibrationSelection(const int32 Direction)
{
	if (SystemMenuMode != EIGSystemMenuMode::AudioCalibration || Direction == 0)
	{
		return;
	}
	AudioCalibrationSelection =
		(AudioCalibrationSelection
			+ (Direction < 0 ? IGAudioCalibration::RowCount - 1 : 1))
		% IGAudioCalibration::RowCount;
	PlayMenuTick(false);
	RefreshMenuHud();
}

void AIGPlayerController::AdjustAudioCalibrationSetting(const int32 Direction)
{
	if (SystemMenuMode != EIGSystemMenuMode::AudioCalibration || Direction == 0)
	{
		return;
	}
	const int32 Step = Direction < 0 ? -1 : 1;
	if (AudioCalibrationSelection == IGAudioCalibration::Master)
	{
		AudioCalibrationVolumeStep = FMath::Clamp(
			AudioCalibrationVolumeStep + Step,
			0,
			IGAudioCalibration::VolumeStepCount - 1);
		ApplyAudioCalibrationValues();
		NextAudioCalibrationKnockTime = FPlatformTime::Seconds() + 0.12;
		SetActorTickEnabled(true);
	}
	else if (AudioCalibrationSelection == IGAudioCalibration::Music
		|| AudioCalibrationSelection == IGAudioCalibration::Ambience)
	{
		const bool bMusic = AudioCalibrationSelection == IGAudioCalibration::Music;
		if (bMusic)
		{
			AudioCalibrationMusicStep = FMath::Clamp(
				AudioCalibrationMusicStep + Step,
				0,
				IGAudioCalibration::MusicStepCount - 1);
		}
		else
		{
			AudioCalibrationAmbienceStep = FMath::Clamp(
				AudioCalibrationAmbienceStep + Step,
				0,
				IGAudioCalibration::AmbienceStepCount - 1);
		}
		ApplyAudioCalibrationValues();
		// 메뉴 뒤의 월드는 멈춰 있어 이 두 버스에는 소리가 없다. 바꾼 값을 귀로
		// 확인하도록 그 버스의 소리를 잠깐 들려준다. 음악 0단은 무음이 맞다.
		if (UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
			? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
			: nullptr)
		{
			AudioDirector->PlayCalibrationPreview(bMusic);
		}
	}
	else if (AudioCalibrationSelection == IGAudioCalibration::Brightness)
	{
		AudioCalibrationBrightnessStep = FMath::Clamp(
			AudioCalibrationBrightnessStep + Step,
			0,
			IGAudioCalibration::BrightnessStepCount - 1);
		ApplyAudioCalibrationValues();
	}
	else if (AudioCalibrationSelection == IGAudioCalibration::Output)
	{
		// 둘 중 하나라 좌우 어느 쪽이든 뒤집힌다.
		bHeadphoneOutput = !bHeadphoneOutput;
		ApplyAudioCalibrationValues();
		// 바꾼 직후에 노크를 한 번 들려준다. 귀로 확인할 수 없는 음향
		// 설정은 화면에 글자만 바뀌는 것과 같다.
		NextAudioCalibrationKnockTime = FPlatformTime::Seconds() + 0.18;
		SetActorTickEnabled(true);
	}
	RefreshMenuHud();
}

void AIGPlayerController::ConfirmAudioCalibrationSelection()
{
	if (AudioCalibrationSelection < IGAudioCalibration::TestKnock)
	{
		AdjustAudioCalibrationSetting(1);
		return;
	}
	if (AudioCalibrationSelection == IGAudioCalibration::TestKnock)
	{
		PlayAudioCalibrationKnock();
		return;
	}
	CompleteAudioCalibration();
}

void AIGPlayerController::ApplyAudioCalibrationValues()
{
	IGAudio::SetOutputMode(
		bHeadphoneOutput
			? IGAudio::EIGOutputMode::Headphones
			: IGAudio::EIGOutputMode::Speakers);
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->SetUserMasterVolume(
				IGAudioCalibration::VolumeValues[AudioCalibrationVolumeStep]);
			AudioDirector->SetHeadphoneOutput(bHeadphoneOutput);
			AudioDirector->SetScoreUserVolume(
				IGAudioCalibration::MusicValues[AudioCalibrationMusicStep]);
			AudioDirector->SetAmbienceUserVolume(
				IGAudioCalibration::AmbienceValues[
					AudioCalibrationAmbienceStep]);
		}
	}
	ConsoleCommand(
		FString::Printf(
			TEXT("gamma %.2f"),
			IGAudioCalibration::GammaValues[AudioCalibrationBrightnessStep]),
		true);
}

void AIGPlayerController::PlayAudioCalibrationKnock()
{
	NextAudioCalibrationKnockTime = -1.0;
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->PlayCalibrationKnock();
		}
	}
}

void AIGPlayerController::OpenDisplaySettings()
{
	if (SystemMenuMode != EIGSystemMenuMode::Title
		&& SystemMenuMode != EIGSystemMenuMode::Pause
		&& SystemMenuMode != EIGSystemMenuMode::AudioCalibration)
	{
		return;
	}
	DisplaySettingsReturnMode = SystemMenuMode;
	DisplaySettingsSelection = 0;
	SystemMenuStatusText = FText::GetEmpty();
	bSystemMenuStatusIsError = false;
	RefreshStagedDisplaySettings();
	SetSystemMenuMode(EIGSystemMenuMode::DisplaySettings);
}

void AIGPlayerController::ReturnFromDisplaySettings()
{
	const EIGSystemMenuMode ReturnMode =
		DisplaySettingsReturnMode == EIGSystemMenuMode::Pause
			? EIGSystemMenuMode::Pause
			: DisplaySettingsReturnMode == EIGSystemMenuMode::AudioCalibration
				? EIGSystemMenuMode::AudioCalibration
				: EIGSystemMenuMode::Title;
	DisplaySettingsReturnMode = EIGSystemMenuMode::Title;
	SetSystemMenuMode(ReturnMode);
}

void AIGPlayerController::RefreshStagedDisplaySettings()
{
	UGameUserSettings* Settings = GEngine
		? GEngine->GetGameUserSettings()
		: nullptr;
	if (!Settings)
	{
		return;
	}

	switch (Settings->GetFullscreenMode())
	{
	case EWindowMode::WindowedFullscreen:
		DisplayWindowModeIndex = 1;
		break;
	case EWindowMode::Windowed:
		DisplayWindowModeIndex = 2;
		break;
	case EWindowMode::Fullscreen:
	default:
		DisplayWindowModeIndex = 0;
		break;
	}

	const FIntPoint CurrentResolution = Settings->GetScreenResolution();
	int64 NearestDistance = TNumericLimits<int64>::Max();
	for (int32 Index = 0; Index < IGDisplaySettings::ResolutionCount; ++Index)
	{
		const int64 DeltaX = static_cast<int64>(CurrentResolution.X)
			- IGDisplaySettings::Resolutions[Index].X;
		const int64 DeltaY = static_cast<int64>(CurrentResolution.Y)
			- IGDisplaySettings::Resolutions[Index].Y;
		const int64 Distance = DeltaX * DeltaX + DeltaY * DeltaY;
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			DisplayResolutionIndex = Index;
		}
	}

	DisplayQualityIndex = Settings->GetOverallScalabilityLevel() <= 1 ? 0 : 1;
	bDisplayVSync = Settings->IsVSyncEnabled();
	const float CurrentFrameLimit = Settings->GetFrameRateLimit();
	DisplayFrameLimitIndex = CurrentFrameLimit <= 0.0f
		? 2
		: CurrentFrameLimit <= 45.0f
			? 0
			: 1;
	bDisplaySettingsApplied = false;
}

void AIGPlayerController::MoveDisplaySettingsSelection(const int32 Direction)
{
	if (SystemMenuMode != EIGSystemMenuMode::DisplaySettings || Direction == 0)
	{
		return;
	}
	if (bDisplaySettingsAwaitingConfirmation)
	{
		DisplaySettingsSelection =
			DisplaySettingsSelection == IGSettingsMenuLayout::ApplyOrKeep
				? IGSettingsMenuLayout::BackOrRevert
				: IGSettingsMenuLayout::ApplyOrKeep;
		PlayMenuTick(false);
		RefreshMenuHud();
		return;
	}
	DisplaySettingsSelection =
		(DisplaySettingsSelection
			+ (Direction < 0 ? IGDisplaySettings::RowCount - 1 : 1))
		% IGDisplaySettings::RowCount;
	PlayMenuTick(false);
	RefreshMenuHud();
}

void AIGPlayerController::AdjustDisplaySetting(const int32 Direction)
{
	if (SystemMenuMode != EIGSystemMenuMode::DisplaySettings
		|| bDisplaySettingsAwaitingConfirmation
		|| Direction == 0)
	{
		return;
	}
	const auto Wrap = [Direction](const int32 Value, const int32 Count)
	{
		return (Value + (Direction < 0 ? Count - 1 : 1)) % Count;
	};
	switch (DisplaySettingsSelection)
	{
	case 0:
		DisplayWindowModeIndex = Wrap(
			DisplayWindowModeIndex,
			IGDisplaySettings::WindowModeCount);
		break;
	case 1:
		DisplayResolutionIndex = Wrap(
			DisplayResolutionIndex,
			IGDisplaySettings::ResolutionCount);
		break;
	case 2:
		DisplayQualityIndex = Wrap(
			DisplayQualityIndex,
			IGDisplaySettings::QualityCount);
		break;
	case 3:
		bDisplayVSync = !bDisplayVSync;
		break;
	case 4:
		DisplayFrameLimitIndex = Wrap(
			DisplayFrameLimitIndex,
			IGDisplaySettings::FrameLimitCount);
		break;
	case IGSettingsMenuLayout::Language:
		// 언어는 화면 설정 확인(10초 되돌리기)과 상관없이 바로 바꾸고 저장한다.
		if (UIGLanguageSubsystem* Language = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UIGLanguageSubsystem>()
			: nullptr)
		{
			Language->CycleCulture(Direction);
			PlayMenuTick(false);
		}
		RefreshMenuHud();
		return;
	default:
		return;
	}
	// 고른 즉시 적용한다. 값만 바꿔 두고 「변경 적용」 줄을 따로 찾아 눌러야
	// 반영되던 방식은, 눌러야 하는 줄이 화면 밖에 있으면 아무 일도 일어나지
	// 않는 것처럼 보인다. 화면을 못 보게 만들 수 있는 것(화면 모드·해상도)만
	// 적용 뒤 10초 확인을 띄우고, 나머지는 바로 저장한다.
	ApplyDisplaySettings();
	SystemMenuStatusText = FText::GetEmpty();
	bSystemMenuStatusIsError = false;
	RefreshMenuHud();
}

void AIGPlayerController::ConfirmDisplaySettingsSelection()
{
	if (SystemMenuMode != EIGSystemMenuMode::DisplaySettings)
	{
		return;
	}
	if (bDisplaySettingsAwaitingConfirmation)
	{
		if (DisplaySettingsSelection == IGSettingsMenuLayout::ApplyOrKeep)
		{
			ConfirmPendingDisplaySettings();
		}
		else
		{
			RevertPendingDisplaySettings();
		}
		return;
	}
	if (DisplaySettingsSelection <= IGSettingsMenuLayout::FrameLimit
		|| DisplaySettingsSelection == IGSettingsMenuLayout::Language)
	{
		AdjustDisplaySetting(1);
		return;
	}
	if (DisplaySettingsSelection == IGSettingsMenuLayout::AccessibilityPanel)
	{
		ToggleAccessibilityMenu();
		return;
	}
	if (DisplaySettingsSelection == IGSettingsMenuLayout::AudioCalibrationPanel)
	{
		OpenAudioCalibration(false);
		return;
	}
	if (DisplaySettingsSelection == IGSettingsMenuLayout::KeyBindingsPanel)
	{
		OpenKeyBindings();
		return;
	}
	if (DisplaySettingsSelection == IGSettingsMenuLayout::ApplyOrKeep)
	{
		ApplyDisplaySettings();
		return;
	}
	ReturnFromDisplaySettings();
}

void AIGPlayerController::ApplyDisplaySettings()
{
	UGameUserSettings* Settings = GEngine
		? GEngine->GetGameUserSettings()
		: nullptr;
	if (!Settings)
	{
		SystemMenuStatusText = NSLOCTEXT(
			"IGFrontend",
			"DisplaySettingsUnavailable",
			"화면 설정을 적용할 수 없습니다.");
		bSystemMenuStatusIsError = true;
		bDisplaySettingsApplied = false;
		RefreshMenuHud();
		return;
	}
	PreviousDisplayQualityLevel = Settings->GetOverallScalabilityLevel();
	if (PreviousDisplayQualityLevel < 0)
	{
		PreviousDisplayQualityLevel = 2;
	}
	bPreviousDisplayVSync = Settings->IsVSyncEnabled();
	PreviousDisplayFrameLimit = Settings->GetFrameRateLimit();

	const EWindowMode::Type WindowMode = DisplayWindowModeIndex == 1
		? EWindowMode::WindowedFullscreen
		: DisplayWindowModeIndex == 2
			? EWindowMode::Windowed
			: EWindowMode::Fullscreen;
	// 되돌릴 수 있어야 하는 것은 화면을 못 보게 만들 수 있는 둘뿐이다.
	// 품질·수직 동기화·프레임 제한은 잘못 골라도 화면이 살아 있으므로
	// 확인을 물을 이유가 없다.
	const bool bDisplayModeChanged =
		Settings->GetFullscreenMode() != WindowMode
		|| Settings->GetScreenResolution()
			!= IGDisplaySettings::Resolutions[DisplayResolutionIndex];

	Settings->SetFullscreenMode(WindowMode);
	Settings->SetScreenResolution(
		IGDisplaySettings::Resolutions[DisplayResolutionIndex]);
	// UE 5.8의 중간 품질은 Lumen Lite를 쓴다. 성능 모드에서도 간접광을 남긴다.
	Settings->SetOverallScalabilityLevel(DisplayQualityIndex == 0 ? 1 : 2);
	Settings->SetVSyncEnabled(bDisplayVSync);
	Settings->SetFrameRateLimit(
		IGDisplaySettings::FrameLimits[DisplayFrameLimitIndex]);
	Settings->ApplyResolutionSettings(false);
	Settings->ApplyNonResolutionSettings();

	if (!bDisplayModeChanged)
	{
		// 여기서 바로 디스크에 쓴다. 사람이 저장을 찾아 누르지 않아도
		// 다음 실행에 남아 있어야 한다.
		Settings->SaveSettings();
		bDisplaySettingsApplied = true;
		bDisplaySettingsAwaitingConfirmation = false;
		DisplayConfirmationSecondsRemaining = 0;
		SystemMenuStatusText = FText::GetEmpty();
		bSystemMenuStatusIsError = false;
		RefreshMenuHud();
		return;
	}

	bDisplaySettingsApplied = false;
	bDisplaySettingsAwaitingConfirmation = true;
	DisplaySettingsSelection = IGSettingsMenuLayout::ApplyOrKeep;
	DisplayConfirmationSecondsRemaining = 10;
	DisplayConfirmationDeadline = FPlatformTime::Seconds() + 10.0;
	SetActorTickEnabled(true);
	SystemMenuStatusText = FText::GetEmpty();
	bSystemMenuStatusIsError = false;
	RefreshMenuHud();
}

void AIGPlayerController::ConfirmPendingDisplaySettings()
{
	if (!bDisplaySettingsAwaitingConfirmation)
	{
		return;
	}
	if (UGameUserSettings* Settings = GEngine
		? GEngine->GetGameUserSettings()
		: nullptr)
	{
		Settings->ConfirmVideoMode();
		Settings->SaveSettings();
	}
	bDisplaySettingsAwaitingConfirmation = false;
	bDisplaySettingsApplied = true;
	DisplayConfirmationSecondsRemaining = 0;
	RefreshStagedDisplaySettings();
	bDisplaySettingsApplied = true;
	RefreshMenuHud();
}

void AIGPlayerController::RevertPendingDisplaySettings()
{
	if (!bDisplaySettingsAwaitingConfirmation)
	{
		return;
	}
	if (UGameUserSettings* Settings = GEngine
		? GEngine->GetGameUserSettings()
		: nullptr)
	{
		Settings->RevertVideoMode();
		Settings->SetOverallScalabilityLevel(PreviousDisplayQualityLevel);
		Settings->SetVSyncEnabled(bPreviousDisplayVSync);
		Settings->SetFrameRateLimit(PreviousDisplayFrameLimit);
		Settings->ApplyResolutionSettings(false);
		Settings->ApplyNonResolutionSettings();
		Settings->SaveSettings();
	}
	bDisplaySettingsAwaitingConfirmation = false;
	bDisplaySettingsApplied = false;
	DisplayConfirmationSecondsRemaining = 0;
	RefreshStagedDisplaySettings();
	SystemMenuStatusText = NSLOCTEXT(
		"IGFrontend",
		"DisplaySettingsReverted",
		"이전 화면 설정으로 되돌렸습니다.");
	bSystemMenuStatusIsError = false;
	RefreshMenuHud();
}

void AIGPlayerController::ReturnFromCredits()
{
	const EIGSystemMenuMode ReturnMode =
		CreditsReturnMode == EIGSystemMenuMode::Pause
			? EIGSystemMenuMode::Pause
			: EIGSystemMenuMode::Title;
	SetSystemMenuMode(ReturnMode);
}

void AIGPlayerController::StartNewGame()
{
	UIGSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (!GetWorld() || (SaveSubsystem && SaveSubsystem->IsBusy()))
	{
		return;
	}
	if (SaveSubsystem && !SaveSubsystem->ClearRotatingAutosaves())
	{
		SystemMenuStatusText = NSLOCTEXT(
			"IGFrontend",
			"NewGameDeleteFailed",
			"자동 저장을 지우지 못했습니다. 저장 공간과 폴더 권한을 확인하세요.");
		bSystemMenuStatusIsError = true;
		bNewGameConfirmationArmed = false;
		RefreshMenuHud();
		return;
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UIGStoryStateSubsystem* StoryState =
			GameInstance->GetSubsystem<UIGStoryStateSubsystem>())
		{
			StoryState->ClearStates(false);
		}
		// Game-instance subsystems outlive a new game inside one process, so
		// without this the previous run's truths and aggression tier leak in.
		if (UIGMissingFloorNarrativeSubsystem* MissingFloorState =
			GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
		{
			MissingFloorState->ResetNarrative();
		}
	}
	bNewGameConfirmationArmed = false;
	SystemMenuStatusText = FText::GetEmpty();
	bSystemMenuStatusIsError = false;

	const FName LevelName(*UGameplayStatics::GetCurrentLevelName(this, true));
	UGameplayStatics::OpenLevel(
		this,
		LevelName,
		true,
		TEXT("IGMissingFloor=1?IGNewGame=1"));
}

void AIGPlayerController::ContinueLatestAutosave()
{
	UIGSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (!SaveSubsystem || SaveSubsystem->IsBusy())
	{
		SystemMenuStatusText = NSLOCTEXT(
			"IGFrontend",
			"SaveOperationBusy",
			"저장 작업이 끝난 뒤 다시 시도하세요.");
		bSystemMenuStatusIsError = true;
		RefreshMenuHud();
		return;
	}
	if (SaveSubsystem->RequestLoadLatestAutosave())
	{
		SystemMenuStatusText = NSLOCTEXT(
			"IGFrontend",
			"LoadingAutosave",
			"최근 자동 저장을 불러오는 중입니다.");
		bSystemMenuStatusIsError = false;
		RefreshMenuHud();
		return;
	}
	bCompatibleAutosaveAvailable = HasCompatibleAutosave();
	bNightFiveAvailable = HasEndingBAutosave();
	SystemMenuStatusText = NSLOCTEXT(
		"IGFrontend",
		"CompatibleAutosaveMissing",
		"불러올 수 있는 자동 저장이 없습니다.");
	bSystemMenuStatusIsError = true;
	RefreshMenuHud();
}

void AIGPlayerController::QuitToDesktop()
{
	UKismetSystemLibrary::QuitGame(
		this,
		this,
		EQuitPreference::Quit,
		false);
}

void AIGPlayerController::BindSaveNotifications()
{
	if (UIGSaveSubsystem* SaveSubsystem = GetSaveSubsystem())
	{
		SaveSubsystem->OnSaveCompleted.AddUniqueDynamic(
			this,
			&ThisClass::HandleSaveCompleted);
		SaveSubsystem->OnLoadCompleted.AddUniqueDynamic(
			this,
			&ThisClass::HandleLoadCompleted);
	}
}

void AIGPlayerController::UnbindSaveNotifications()
{
	if (UIGSaveSubsystem* SaveSubsystem = GetSaveSubsystem())
	{
		SaveSubsystem->OnSaveCompleted.RemoveDynamic(
			this,
			&ThisClass::HandleSaveCompleted);
		SaveSubsystem->OnLoadCompleted.RemoveDynamic(
			this,
			&ThisClass::HandleLoadCompleted);
	}
}

void AIGPlayerController::HandleSaveCompleted(
	const bool bSuccess,
	FString SlotName)
{
	static_cast<void>(SlotName);
	if (bSuccess)
	{
		// §19.7. 수동 슬롯이 없는 게임이라 저장됐다는 사실을 어디선가는
		// 말해야 한다. 점 하나 0.8초, 그 이상은 §23이 금지한 상시 표시다.
		if (AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
		{
			HorrorHUD->ShowSaveIndicator();
		}
		return;
	}
	const FText FailureText = NSLOCTEXT(
		"IGFrontend",
		"SaveFailed",
		"저장에 실패했습니다. 저장 공간과 폴더 권한을 확인하세요.");
	if (SystemMenuMode == EIGSystemMenuMode::Hidden
		&& !bAccessibilityMenuVisible)
	{
		AIGHorrorHUD::PushThought(this, FailureText, 4.0f);
		return;
	}
	SystemMenuStatusText = FailureText;
	bSystemMenuStatusIsError = true;
	RefreshMenuHud();
}

void AIGPlayerController::HandleLoadCompleted(
	const bool bSuccess,
	FString SlotName,
	UIGSaveGame* SaveGame)
{
	static_cast<void>(SlotName);
	static_cast<void>(SaveGame);
	if (bSuccess)
	{
		return;
	}
	bCompatibleAutosaveAvailable = HasCompatibleAutosave();
	bNightFiveAvailable = HasEndingBAutosave();
	SystemMenuStatusText = NSLOCTEXT(
		"IGFrontend",
		"LoadFailed",
		"자동 저장을 불러오지 못했습니다. 파일이 손상됐거나 호환되지 않습니다.");
	bSystemMenuStatusIsError = true;
	if (SystemMenuMode == EIGSystemMenuMode::Hidden
		&& !bAccessibilityMenuVisible)
	{
		AIGHorrorHUD::PushThought(this, SystemMenuStatusText, 4.0f);
	}
	RefreshMenuHud();
}

void AIGPlayerController::RefreshMenuHud() const
{
	if (AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD()))
	{
		HorrorHUD->SetAccessibilityMenuState(
			bAccessibilityMenuVisible,
			AccessibilitySelection,
			AccessibilitySelection == IGSettingsMenuLayout::ResetDefaults
				? AccessibilityResetArmedUntil
				: -1.0);
		FIGSystemMenuPresentation Presentation;
		Presentation.bVisible =
			SystemMenuMode != EIGSystemMenuMode::Hidden;
		Presentation.bTitle = SystemMenuMode == EIGSystemMenuMode::Title;
		Presentation.bUseTitleBackdrop =
			SystemMenuMode == EIGSystemMenuMode::Title
			|| (SystemMenuMode == EIGSystemMenuMode::Credits
				&& CreditsReturnMode == EIGSystemMenuMode::Title);
		Presentation.bCredits = SystemMenuMode == EIGSystemMenuMode::Credits;
		Presentation.bContentNotice =
			SystemMenuMode == EIGSystemMenuMode::ContentNotice;
		Presentation.bKeyBindings =
			SystemMenuMode == EIGSystemMenuMode::KeyBindings;
		Presentation.KeyBindingSelection = KeyBindingSelection;
		Presentation.bKeyBindingCapturing = bKeyBindingCapturing;
		Presentation.bKeyBindingColumnGamepad = bKeyBindingColumnGamepad;
		Presentation.KeyBindingStatus = KeyBindingStatusText;
		Presentation.bKeyBindingStatusIsError = bKeyBindingStatusIsError;
		Presentation.bAudioCalibration =
			SystemMenuMode == EIGSystemMenuMode::AudioCalibration;
		Presentation.bDisplaySettings =
			SystemMenuMode == EIGSystemMenuMode::DisplaySettings;
		Presentation.bCanContinue = bCompatibleAutosaveAvailable;
		Presentation.bNightFiveAvailable = bNightFiveAvailable;
		Presentation.bNightFiveSpent = bNightFiveSpent;
		Presentation.bNightFivePlaying = bNightFivePlaying;
		Presentation.bConfirmNewGame = bNewGameConfirmationArmed;
		Presentation.bHeadphoneRecommendation =
			bHeadphoneRecommendationVisible;
		Presentation.bVSync = bDisplayVSync;
		Presentation.bDisplaySettingsApplied = bDisplaySettingsApplied;
		Presentation.bDisplaySettingsAwaitingConfirmation =
			bDisplaySettingsAwaitingConfirmation;
		Presentation.bStatusIsError = bSystemMenuStatusIsError;
		Presentation.SelectedRow = SystemMenuSelection;
		Presentation.DisplaySelectedRow = DisplaySettingsSelection;
		Presentation.AudioCalibrationSelectedRow = AudioCalibrationSelection;
		Presentation.AudioCalibrationVolumeStep = AudioCalibrationVolumeStep;
		Presentation.AudioCalibrationBrightnessStep =
			AudioCalibrationBrightnessStep;
		Presentation.bAudioCalibrationFirstRun = bAudioCalibrationFirstRun;
		Presentation.bHeadphoneOutput = bHeadphoneOutput;
		Presentation.AudioCalibrationMusicStep = AudioCalibrationMusicStep;
		Presentation.AudioCalibrationAmbienceStep = AudioCalibrationAmbienceStep;
		Presentation.WindowModeIndex = DisplayWindowModeIndex;
		Presentation.ResolutionIndex = DisplayResolutionIndex;
		Presentation.QualityIndex = DisplayQualityIndex;
		Presentation.FrameLimitIndex = DisplayFrameLimitIndex;
		Presentation.ConfirmationSecondsRemaining =
			DisplayConfirmationSecondsRemaining;
		Presentation.StatusText = SystemMenuStatusText;
		HorrorHUD->SetSystemMenuState(Presentation);
		HorrorHUD->SetMissingFloorJournalState(
			bMissingFloorJournalVisible,
			MissingFloorJournalPage);
		HorrorHUD->SetInputDevicePresentation(bUsingGamepadForHud);
	}
}

void AIGPlayerController::SetInputDevicePresentation(const bool bUsingGamepad)
{
	bUsingGamepadForHud = bUsingGamepad;
	ApplyMenuInputMode();
	RefreshMenuHud();
}

void AIGPlayerController::ApplyMenuInputMode()
{
	UpdateMenuWorldRendering();
	const bool bPointerMenuVisible = bAccessibilityMenuVisible
		|| SystemMenuMode != EIGSystemMenuMode::Hidden;
	const bool bInputLayerVisible = bPointerMenuVisible
		|| bMissingFloorJournalVisible;
	bShowMouseCursor = bPointerMenuVisible && !bUsingGamepadForHud;
	if (bInputLayerVisible)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
	}
	else
	{
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
	}
}

void AIGPlayerController::UpdateMenuWorldRendering()
{
	UGameViewportClient* Viewport = GetLocalPlayer() ? GetLocalPlayer()->ViewportClient : nullptr;
	if (!Viewport) return;
	// 설정은 화면을 덮는다. 보이지 않는 3D 장면의 조명과 그림자를 다시 그릴 필요가 없다.
	const bool bMenuCoversWorld = bAccessibilityMenuVisible
		|| SystemMenuMode == EIGSystemMenuMode::DisplaySettings
		|| SystemMenuMode == EIGSystemMenuMode::AudioCalibration
		|| SystemMenuMode == EIGSystemMenuMode::KeyBindings;
	if (bMenuCoversWorld && !bMenuWorldRenderingSuspended)
	{
		bWorldRenderingWasDisabled = Viewport->bDisableWorldRendering;
		Viewport->bDisableWorldRendering = true;
		bMenuWorldRenderingSuspended = true;
	}
	else if (!bMenuCoversWorld)
	{
		RestoreMenuWorldRendering();
	}
}

void AIGPlayerController::RestoreMenuWorldRendering()
{
	if (!bMenuWorldRenderingSuspended) return;
	if (UGameViewportClient* Viewport = GetLocalPlayer() ? GetLocalPlayer()->ViewportClient : nullptr)
	{
		Viewport->bDisableWorldRendering = bWorldRenderingWasDisabled;
	}
	bMenuWorldRenderingSuspended = false;
}

bool AIGPlayerController::TryGetMenuRowFromPointer(
	const int32 RowCount,
	const float MinimumStartY,
	const float StartYFraction,
	const float MinimumSpacing,
	const float MaximumSpacing,
	const float SpacingFraction,
	int32& OutRow) const
{
	OutRow = INDEX_NONE;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	GetViewportSize(ViewportWidth, ViewportHeight);
	float PointerX = 0.0f;
	float PointerY = 0.0f;
	if (ViewportWidth <= 0
		|| ViewportHeight <= 0
		|| !GetMousePosition(PointerX, PointerY)
		|| PointerX < ViewportWidth * 0.16f
		|| PointerX > ViewportWidth * 0.84f)
	{
		return false;
	}

	const float RowStart = FMath::Max(
		MinimumStartY,
		ViewportHeight * StartYFraction);
	const float RowSpacing = FMath::Clamp(
		ViewportHeight * SpacingFraction,
		MinimumSpacing,
		MaximumSpacing);
	const int32 Candidate = FMath::RoundToInt(
		(PointerY - RowStart) / RowSpacing);
	if (Candidate < 0
		|| Candidate >= RowCount
		|| FMath::Abs(PointerY - (RowStart + Candidate * RowSpacing))
			> RowSpacing * 0.46f)
	{
		return false;
	}
	OutRow = Candidate;
	return true;
}

bool AIGPlayerController::TryGetSystemMenuRowFromPointer(int32& OutRow) const
{
	OutRow = INDEX_NONE;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	GetViewportSize(ViewportWidth, ViewportHeight);
	float PointerX = 0.0f;
	float PointerY = 0.0f;
	if (ViewportWidth <= 0
		|| ViewportHeight <= 0
		|| !GetMousePosition(PointerX, PointerY))
	{
		return false;
	}

	OutRow = IGFrontendMenuLayout::HitTestAction(
		IGFrontendMenuLayout::MakeMetrics(ViewportWidth, ViewportHeight),
		FVector2D(PointerX, PointerY),
		SystemMenuMode == EIGSystemMenuMode::Title,
		bCompatibleAutosaveAvailable,
		bNightFiveAvailable);
	return OutRow != INDEX_NONE;
}

bool AIGPlayerController::TryGetDisplaySettingsRowFromPointer(
	int32& OutRow,
	bool& bOutCategoryHit) const
{
	OutRow = INDEX_NONE;
	bOutCategoryHit = false;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	GetViewportSize(ViewportWidth, ViewportHeight);
	float PointerX = 0.0f;
	float PointerY = 0.0f;
	if (ViewportWidth <= 0
		|| ViewportHeight <= 0
		|| !GetMousePosition(PointerX, PointerY))
	{
		return false;
	}
	return IGSettingsMenuLayout::HitTestSettingsRow(
		IGSettingsMenuLayout::MakePanelMetrics(ViewportWidth, ViewportHeight),
		FVector2D(PointerX, PointerY),
		DisplaySettingsSelection,
		IGSettingsMenuLayout::DisplayCategoryCount,
		IGSettingsMenuLayout::GetDisplayCategory,
		OutRow,
		bOutCategoryHit);
}

bool AIGPlayerController::TryGetAccessibilityRowFromPointer(
	int32& OutRow,
	bool& bOutCategoryHit) const
{
	OutRow = INDEX_NONE;
	bOutCategoryHit = false;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	GetViewportSize(ViewportWidth, ViewportHeight);
	float PointerX = 0.0f;
	float PointerY = 0.0f;
	if (ViewportWidth <= 0
		|| ViewportHeight <= 0
		|| !GetMousePosition(PointerX, PointerY))
	{
		return false;
	}
	return IGSettingsMenuLayout::HitTestSettingsRow(
		IGSettingsMenuLayout::MakeAccessibilityPanelMetrics(
			ViewportWidth, ViewportHeight, AccessibilitySelection),
		FVector2D(PointerX, PointerY),
		AccessibilitySelection,
		IGSettingsMenuLayout::AccessibilityCategoryCount,
		IGSettingsMenuLayout::GetAccessibilityCategory,
		OutRow,
		bOutCategoryHit);
}

void AIGPlayerController::UpdateMenuPointerHover()
{
	int32 Row = INDEX_NONE;
	bool bCategoryHit = false;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	GetViewportSize(ViewportWidth, ViewportHeight);
	if (bAccessibilityMenuVisible)
	{
		if (TryGetAccessibilityRowFromPointer(Row, bCategoryHit)
			&& !bCategoryHit
			&& AccessibilitySelection != Row)
		{
			AccessibilitySelection = Row;
			RefreshMenuHud();
		}
		return;
	}
	if (SystemMenuMode == EIGSystemMenuMode::DisplaySettings)
	{
		if (TryGetDisplaySettingsRowFromPointer(Row, bCategoryHit)
			&& !bCategoryHit
			&& (!bDisplaySettingsAwaitingConfirmation || Row >= 7)
			&& DisplaySettingsSelection != Row)
		{
			DisplaySettingsSelection = Row;
			RefreshMenuHud();
		}
		return;
	}
	if (SystemMenuMode == EIGSystemMenuMode::AudioCalibration)
	{
		// HUD가 그리는 줄과 같은 치수를 쓴다. 따로 적어 두었다가 두 줄 어긋났었다.
		const IGSettingsMenuLayout::FAudioCalibrationMetrics Layout =
			IGSettingsMenuLayout::MakeAudioCalibrationMetrics(
				ViewportWidth,
				ViewportHeight);
		if (TryGetMenuRowFromPointer(
				IGAudioCalibration::RowCount,
				Layout.RowTop + Layout.RowCenterOffset,
				0.0f,
				Layout.RowSpacing,
				Layout.RowSpacing,
				0.0f,
				Row)
			&& AudioCalibrationSelection != Row)
		{
			AudioCalibrationSelection = Row;
			RefreshMenuHud();
		}
		return;
	}
	if (SystemMenuMode == EIGSystemMenuMode::Title
		|| SystemMenuMode == EIGSystemMenuMode::Pause)
	{
		if (TryGetSystemMenuRowFromPointer(Row)
			&& IsSystemMenuRowEnabled(Row)
			&& SystemMenuSelection != Row)
		{
			SystemMenuSelection = Row;
			bNewGameConfirmationArmed = false;
			SystemMenuStatusText = FText::GetEmpty();
			bSystemMenuStatusIsError = false;
			RefreshMenuHud();
		}
	}
}

bool AIGPlayerController::HandleMenuPointerClick()
{
	if (!bAccessibilityMenuVisible
		&& SystemMenuMode == EIGSystemMenuMode::Hidden)
	{
		return false;
	}
	if (SystemMenuMode == EIGSystemMenuMode::Credits
		&& !bAccessibilityMenuVisible)
	{
		ReturnFromCredits();
		return true;
	}

	int32 Row = INDEX_NONE;
	bool bCategoryHit = false;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	GetViewportSize(ViewportWidth, ViewportHeight);
	if (bAccessibilityMenuVisible)
	{
		if (TryGetAccessibilityRowFromPointer(Row, bCategoryHit))
		{
			AccessibilitySelection = Row;
			if (!bCategoryHit)
			{
				ChangeAccessibilitySetting(1, true);
			}
			else
			{
				RefreshMenuHud();
			}
		}
		return true;
	}
	if (SystemMenuMode == EIGSystemMenuMode::DisplaySettings)
	{
		if (TryGetDisplaySettingsRowFromPointer(Row, bCategoryHit)
			&& (!bDisplaySettingsAwaitingConfirmation || Row >= 7))
		{
			DisplaySettingsSelection = Row;
			if (!bCategoryHit)
			{
				ConfirmDisplaySettingsSelection();
			}
			else
			{
				RefreshMenuHud();
			}
		}
		return true;
	}
	if (SystemMenuMode == EIGSystemMenuMode::AudioCalibration)
	{
		// HUD가 그리는 줄과 같은 치수를 쓴다. 따로 적어 두었다가 두 줄 어긋났었다.
		const IGSettingsMenuLayout::FAudioCalibrationMetrics Layout =
			IGSettingsMenuLayout::MakeAudioCalibrationMetrics(
				ViewportWidth,
				ViewportHeight);
		if (TryGetMenuRowFromPointer(
			IGAudioCalibration::RowCount,
			Layout.RowTop + Layout.RowCenterOffset,
			0.0f,
			Layout.RowSpacing,
			Layout.RowSpacing,
			0.0f,
			Row))
		{
			AudioCalibrationSelection = Row;
			ConfirmAudioCalibrationSelection();
		}
		return true;
	}
	if (TryGetSystemMenuRowFromPointer(Row)
		&& IsSystemMenuRowEnabled(Row))
	{
		if (SystemMenuSelection != Row)
		{
			bNewGameConfirmationArmed = false;
		}
		SystemMenuSelection = Row;
		ConfirmSystemMenuSelection();
	}
	return true;
}

namespace IGContentNotice
{
	const TCHAR* ConfigSection = TEXT("IndieGame.Onboarding");
	const TCHAR* ShownKey = TEXT("ContentNoticeShown");
}

void AIGPlayerController::OpenKeyBindings()
{
	KeyBindingsReturnMode = SystemMenuMode;
	KeyBindingSelection = 0;
	bKeyBindingCapturing = false;
	bKeyBindingColumnGamepad = bUsingGamepadForHud;
	KeyBindingStatusText = FText::GetEmpty();
	bKeyBindingStatusIsError = false;
	SetSystemMenuMode(EIGSystemMenuMode::KeyBindings);
}

void AIGPlayerController::CloseKeyBindings()
{
	bKeyBindingCapturing = false;
	KeyBindingStatusText = FText::GetEmpty();
	SetSystemMenuMode(
		KeyBindingsReturnMode == EIGSystemMenuMode::KeyBindings
			? EIGSystemMenuMode::Title
			: KeyBindingsReturnMode);
}

void AIGPlayerController::MoveKeyBindingSelection(const int32 Direction)
{
	if (SystemMenuMode != EIGSystemMenuMode::KeyBindings || bKeyBindingCapturing)
	{
		return;
	}
	// 시점 셋이 위에, 동사 목록이 가운데, 「전부 기본값으로」가 마지막이다.
	const int32 RowCount = UIGInputBindingSubsystem::LookRowCount
		+ UIGInputBindingSubsystem::GetActionCount() + 1;
	KeyBindingSelection =
		(KeyBindingSelection + Direction + RowCount) % RowCount;
	KeyBindingStatusText = FText::GetEmpty();
	bKeyBindingStatusIsError = false;
	PlayMenuTick(false);
	RefreshMenuHud();
}

void AIGPlayerController::MoveKeyBindingColumn(const int32 Direction)
{
	if (SystemMenuMode != EIGSystemMenuMode::KeyBindings
		|| bKeyBindingCapturing
		|| Direction == 0)
	{
		return;
	}
	// 시점 행에는 고를 칸이 없다. 좌우가 곧 값이다.
	if (KeyBindingSelection < UIGInputBindingSubsystem::LookRowCount)
	{
		UIGInputBindingSubsystem* Bindings = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UIGInputBindingSubsystem>()
			: nullptr;
		if (!Bindings)
		{
			return;
		}
		switch (KeyBindingSelection)
		{
		case 0:
			Bindings->AdjustMouseSensitivity(Direction);
			break;
		case 1:
			Bindings->AdjustGamepadSensitivity(Direction);
			break;
		case 2:
			Bindings->AdjustVerticalLookScale(Direction);
			break;
		default:
			Bindings->ToggleInvertLookY();
			break;
		}
		KeyBindingStatusText = FText::GetEmpty();
		bKeyBindingStatusIsError = false;
		RefreshMenuHud();
		return;
	}
	bKeyBindingColumnGamepad = Direction > 0;
	KeyBindingStatusText = FText::GetEmpty();
	bKeyBindingStatusIsError = false;
	RefreshMenuHud();
}

void AIGPlayerController::ConfirmKeyBindingSelection()
{
	UIGInputBindingSubsystem* Bindings = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGInputBindingSubsystem>()
		: nullptr;
	if (!Bindings)
	{
		return;
	}
	const int32 ActionIndex =
		KeyBindingSelection - UIGInputBindingSubsystem::LookRowCount;
	if (ActionIndex < 0)
	{
		// 감도는 좌우로 맞춘다. 반전은 켜고 끄는 것뿐이라 확인으로도 뒤집는다.
		if (KeyBindingSelection == 3)
		{
			Bindings->ToggleInvertLookY();
			KeyBindingStatusText = FText::GetEmpty();
		}
		else
		{
			KeyBindingStatusText = NSLOCTEXT(
				"IGHUD",
				"KeyBindingsUseArrows",
				"좌우 방향키로 값을 바꿀 수 있습니다.");
		}
		bKeyBindingStatusIsError = false;
		RefreshMenuHud();
		return;
	}
	if (ActionIndex >= UIGInputBindingSubsystem::GetActionCount())
	{
		Bindings->ResetToDefaults();
		KeyBindingStatusText = NSLOCTEXT(
			"IGHUD", "KeyBindingsReset", "전부 기본값으로 되돌렸습니다.");
		bKeyBindingStatusIsError = false;
		RefreshMenuHud();
		return;
	}
	bKeyBindingCapturing = true;
	KeyBindingStatusText = bKeyBindingColumnGamepad
		? NSLOCTEXT(
			"IGHUD", "KeyBindingsAwaitPad", "새 버튼을 누르세요. B를 누르면 취소합니다.")
		: NSLOCTEXT(
			"IGHUD", "KeyBindingsAwaitKey", "새 키를 누르세요. Esc를 누르면 취소합니다.");
	bKeyBindingStatusIsError = false;
	RefreshMenuHud();
}

bool AIGPlayerController::CaptureKeyBindingInput(const FInputKeyEventArgs& Params)
{
	if (!bKeyBindingCapturing
		|| SystemMenuMode != EIGSystemMenuMode::KeyBindings
		|| Params.IsSimulatedInput()
		|| Params.Event != IE_Pressed)
	{
		return false;
	}
	// 취소는 캡처보다 먼저 본다. 취소 키를 새 바인딩으로 삼으면 그 화면에서
	// 나갈 수 없다.
	if (Params.Key == EKeys::Escape || Params.Key == EKeys::Gamepad_FaceButton_Right)
	{
		bKeyBindingCapturing = false;
		KeyBindingStatusText = FText::GetEmpty();
		bKeyBindingStatusIsError = false;
		RefreshMenuHud();
		return true;
	}

	UIGInputBindingSubsystem* Bindings = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGInputBindingSubsystem>()
		: nullptr;
	if (!Bindings)
	{
		bKeyBindingCapturing = false;
		return true;
	}
	FText Failure;
	if (Bindings->TryRebind(
		KeyBindingSelection - UIGInputBindingSubsystem::LookRowCount,
		bKeyBindingColumnGamepad,
		Params.Key,
		Failure))
	{
		bKeyBindingCapturing = false;
		KeyBindingStatusText = FText::Format(
			NSLOCTEXT("IGHUD", "KeyBindingsBound", "‘{0}’ 키로 바꿨습니다."),
			AIGHorrorHUD::GetShortKeyLabel(Params.Key));
		bKeyBindingStatusIsError = false;
	}
	else
	{
		// 거절해도 대기 상태로 남는다. 다시 누르면 되는 것이지, 처음부터
		// 다시 들어와야 하는 것이 아니다.
		KeyBindingStatusText = Failure;
		bKeyBindingStatusIsError = true;
	}
	RefreshMenuHud();
	return true;
}

void AIGPlayerController::ShowContentNoticeIfNeeded()
{
	if (!IsLocalController()
		|| SystemMenuMode != EIGSystemMenuMode::Title
		|| FParse::Param(FCommandLine::Get(), TEXT("IGFrontendShippingProbe"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGAudioCalibrationPreview"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGNightFiveProbe")))
	{
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("IGContentNoticePreview")))
	{
		// 캡처용 강제 표시. 저장값을 읽지도 쓰지도 않는다.
		SetSystemMenuMode(EIGSystemMenuMode::ContentNotice);
		return;
	}

	bool bAlreadyShown = false;
	if (GConfig)
	{
		GConfig->GetBool(
			IGContentNotice::ConfigSection,
			IGContentNotice::ShownKey,
			bAlreadyShown,
			GGameUserSettingsIni);
	}
	if (bAlreadyShown)
	{
		return;
	}
	SetSystemMenuMode(EIGSystemMenuMode::ContentNotice);
}

void AIGPlayerController::DismissContentNotice()
{
	if (SystemMenuMode != EIGSystemMenuMode::ContentNotice)
	{
		return;
	}
	if (GConfig
		&& !FParse::Param(FCommandLine::Get(), TEXT("IGContentNoticePreview")))
	{
		GConfig->SetBool(
			IGContentNotice::ConfigSection,
			IGContentNotice::ShownKey,
			true,
			GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	SystemMenuSelection = 0;
	SetSystemMenuMode(EIGSystemMenuMode::Title);
	// 고지를 닫고 나서야 소리 맞추기로 넘어간다.
	StartHeadphoneRecommendationIfNeeded();
}

void AIGPlayerController::ShowTitleAfterEnding()
{
	if (!IsLocalController())
	{
		return;
	}
	SystemMenuSelection = 0;
	// 엔딩 카드 바로 뒤다. 타이틀 음악과 그의 노크가 곧장 들어오지 않게 한 번 비운다.
	if (UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
		? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
		: nullptr)
	{
		AudioDirector->ArmPostEndingTitle();
	}
	SetSystemMenuMode(EIGSystemMenuMode::Title);
}

bool AIGPlayerController::ShouldShowTitleMenu() const
{
	// 밤 5 검증은 타이틀 그 자체를 검사하므로 무인 실행에서도 타이틀이 필요하다.
	// 프런트엔드 출하 프로브가 이미 같은 예외를 쓰고 있다.
	if (FParse::Param(FCommandLine::Get(), TEXT("IGNightFiveProbe")))
	{
		return true;
	}
	if (FApp::IsUnattended() || IsRunningCommandlet())
	{
		return false;
	}
	const UWorld* World = GetWorld();
	if (World
		&& (World->URL.HasOption(TEXT("IGResumeSave"))
			|| World->URL.HasOption(TEXT("IGNewGame"))))
	{
		return false;
	}

	const TCHAR* CommandLine = FCommandLine::Get();
	if (FParse::Param(CommandLine, TEXT("IGSkipFrontend"))
		|| FParse::Param(CommandLine, TEXT("IGFrontendShippingProbe"))
		|| FParse::Param(CommandLine, TEXT("IGAudioCalibrationPreview")))
	{
		return false;
	}
	const FString CommandLineText(CommandLine);
	return !CommandLineText.Contains(TEXT("-IGCapture"), ESearchCase::IgnoreCase)
		&& !CommandLineText.Contains(TEXT("-IGDemo"), ESearchCase::IgnoreCase);
}

bool AIGPlayerController::HasCompatibleAutosave() const
{
	const UIGSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	return SaveSubsystem && SaveSubsystem->HasCompatibleAutosave();
}

bool AIGPlayerController::IsSystemMenuRowEnabled(const int32 Row) const
{
	if (Row < 0 || Row >= IGSystemMenu::RowCount
		|| SystemMenuMode == EIGSystemMenuMode::Credits
		|| SystemMenuMode == EIGSystemMenuMode::AudioCalibration
		|| SystemMenuMode == EIGSystemMenuMode::DisplaySettings
		|| SystemMenuMode == EIGSystemMenuMode::Hidden)
	{
		return false;
	}
	if (Row == IGFrontendMenuLayout::NightFiveAction)
	{
		// 흐려진 뒤에도 고를 수 있다. 없는 것은 엔딩 B가 없을 때뿐이다.
		return !IGFrontendMenuLayout::HidesNightFive(
			SystemMenuMode == EIGSystemMenuMode::Title,
			bNightFiveAvailable);
	}
	const int32 LoadRow =
		SystemMenuMode == EIGSystemMenuMode::Title ? 0 : 1;
	return Row != LoadRow || bCompatibleAutosaveAvailable;
}

void AIGPlayerController::RequestNightFiveProbeExit(const bool bFailed)
{
	bool bExitFailed = bFailed;
	if (bNightFiveCaptionReview)
	{
		bExitFailed |= NightFiveCaptionDrawMask != 3 || !bNightFiveCaptionReturnVerified;
		const FString Receipt = FString::Printf(
			TEXT("MISSINGFLOOR_CAPTION_NIGHT5 %s signal_draw=%d answer_draw=%d return_clear=%d unlock_bypass=1\n")
			TEXT("범위: 다섯째 밤 행의 해금 조회를 우회한 자막 표시 검사이며, 실제 엔딩 완주나 해금을 증명하지 않습니다.\n"),
			bExitFailed ? TEXT("FAIL") : TEXT("PASS"),
			(NightFiveCaptionDrawMask & 1) != 0 ? 1 : 0,
			(NightFiveCaptionDrawMask & 2) != 0 ? 1 : 0,
			bNightFiveCaptionReturnVerified ? 1 : 0);
		if (NightFiveCaptionResultPath.IsEmpty()
			|| !FFileHelper::SaveStringToFile(Receipt, *NightFiveCaptionResultPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			bExitFailed = true;
		}
	}
	if (NightFiveProbeTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(NightFiveProbeTicker);
		NightFiveProbeTicker.Reset();
	}
	EndNightFive();
	FPlatformMisc::RequestExitWithStatus(true, bExitFailed ? 2 : 0);
}

void AIGPlayerController::StartNightFiveProbe()
{
	bNightFiveCaptionReview = FParse::Param(FCommandLine::Get(), TEXT("IGCaptionLifecycleReview"));
	if (bNightFiveCaptionReview)
	{
		FParse::Value(FCommandLine::Get(), TEXT("IGCaptionResultPath="), NightFiveCaptionResultPath);
		FParse::Value(FCommandLine::Get(), TEXT("IGCaptionScreenshotDirectory="), NightFiveCaptionScreenshotDirectory);
		NightFiveCaptionResultPath.TrimQuotesInline();
		NightFiveCaptionScreenshotDirectory.TrimQuotesInline();
		if (NightFiveCaptionResultPath.IsEmpty() || NightFiveCaptionScreenshotDirectory.IsEmpty()
			|| FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
		{
			UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_NIGHT5 FAIL: caption review needs a renderer and evidence paths"));
			RequestNightFiveProbeExit(true);
			return;
		}
		NightFiveCaptionResultPath = FPaths::ConvertRelativePathToFull(NightFiveCaptionResultPath);
		NightFiveCaptionScreenshotDirectory = FPaths::ConvertRelativePathToFull(NightFiveCaptionScreenshotDirectory);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(NightFiveCaptionResultPath), true);
		IFileManager::Get().MakeDirectory(*NightFiveCaptionScreenshotDirectory, true);
		NightFiveCaptionDrawMask = 0;
		NightFiveCaptionReturnFrame = 0;
		bNightFiveCaptionReturnVerified = false;
		if (UIGAccessibilitySubsystem* Accessibility = GetAccessibilitySubsystem())
		{
			FIGAccessibilitySettings Settings = Accessibility->GetSettings();
			Settings.bSoundCaptionsEnabled = true;
			Settings.CaptionDurationScale = 1.0f;
			Settings.CaptionSizeScale = 1.0f;
			Accessibility->ApplySettings(Settings);
		}
		else
		{
			RequestNightFiveProbeExit(true);
			return;
		}
	}
	// --- 대응이 전단사인지 -------------------------------------------------
	// 밤 5는 액션 5이면서 화면 자리 1이다. 이 치환이 깨지면 플레이어가 누른
	// 줄과 실행되는 액션이 달라진다 — 조용히, 그리고 정확히 한 행씩.
	for (int32 Combination = 0; Combination < 4; ++Combination)
	{
		const bool bCanContinue = (Combination & 1) != 0;
		const bool bNightFive = (Combination & 2) != 0;
		const int32 VisibleCount = IGFrontendMenuLayout::GetVisibleActionCount(
			true,
			bCanContinue,
			bNightFive);
		const int32 Expected = IGFrontendMenuLayout::ActionCount
			- (bCanContinue ? 0 : 1)
			- (bNightFive ? 0 : 1);
		if (VisibleCount != Expected)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("MISSINGFLOOR_NIGHT5 FAIL: rows=%d expected=%d "
					"continue=%d nightfive=%d"),
				VisibleCount,
				Expected,
				bCanContinue ? 1 : 0,
				bNightFive ? 1 : 0);
			RequestNightFiveProbeExit(true);
			return;
		}
		for (int32 Slot = 0; Slot < VisibleCount; ++Slot)
		{
			const int32 Action = IGFrontendMenuLayout::GetActionForVisibleSlot(
				Slot,
				true,
				bCanContinue,
				bNightFive);
			const int32 RoundTrip = IGFrontendMenuLayout::GetVisibleSlotForAction(
				Action,
				true,
				bCanContinue,
				bNightFive);
			if (Action == INDEX_NONE || RoundTrip != Slot)
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("MISSINGFLOOR_NIGHT5 FAIL: slot=%d action=%d back=%d "
						"continue=%d nightfive=%d"),
					Slot,
					Action,
					RoundTrip,
					bCanContinue ? 1 : 0,
					bNightFive ? 1 : 0);
				RequestNightFiveProbeExit(true);
				return;
			}
		}
	}
	// 화면에서 밤 5는 이어하기 바로 밑이다. 그것이 「이어하기 목록에 한 줄」의
	// 구현이므로 자리 자체를 검사한다.
	if (IGFrontendMenuLayout::GetVisibleSlotForAction(
			IGFrontendMenuLayout::NightFiveAction,
			true,
			true,
			true)
		!= 1)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("MISSINGFLOOR_NIGHT5 FAIL: night five is not under continue"));
		RequestNightFiveProbeExit(true);
		return;
	}
	// 일시정지 메뉴에는 절대 없다. 메타 개입은 본편 바깥이다.
	if (!IGFrontendMenuLayout::HidesNightFive(false, true))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("MISSINGFLOOR_NIGHT5 FAIL: night five reachable while paused"));
		RequestNightFiveProbeExit(true);
		return;
	}

	// --- 30초 자체 ---------------------------------------------------------
	// 하네스는 세이브를 만들지 않는다(§14). 조회 결과만 덮어써서 그 행이 있는
	// 세계를 만든다 — 세이브 파일을 위조하는 것과는 다른 일이다.
	bNightFiveAvailable = HasEndingBAutosave();
	SystemMenuSelection = IGFrontendMenuLayout::NightFiveAction;
	NightFiveProbeStep = 0;
	NightFiveProbeSeconds = 0.0f;
	NightFiveProbeTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this,
			&AIGPlayerController::AdvanceNightFiveProbe));
}

bool AIGPlayerController::AdvanceNightFiveProbe(const float DeltaSeconds)
{
	NightFiveProbeSeconds += DeltaSeconds;
	if (bNightFiveCaptionReview && bNightFivePlaying)
	{
		// 실제 신호가 난 뒤 충분히 그려진 프레임을 잡는다. 두 자막은 별도로 확인한다.
		const float CueTimes[] = {IGNightFive::SignalAtSeconds, IGNightFive::AnswerAtSeconds};
		const TCHAR* CaptureNames[] = {TEXT("night-five-signal.png"), TEXT("night-five-answer.png")};
		for (int32 CueIndex = 0; CueIndex < 2; ++CueIndex)
		{
			const uint8 CueBit = static_cast<uint8>(1 << CueIndex);
			if ((NightFiveCaptionDrawMask & CueBit) != 0 || NightFiveSeconds < CueTimes[CueIndex] + 0.5f)
			{
				continue;
			}
			const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD());
			if (NightFiveSeconds > CueTimes[CueIndex] + 2.2f)
			{
				UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_NIGHT5 FAIL: caption %d was not drawn"), CueIndex);
				RequestNightFiveProbeExit(true);
				return false;
			}
			if (NightFiveCuesPlayed >= CueIndex + 1 && HorrorHUD
				&& HorrorHUD->WasAudioCaptionDrawnInLastHudFrame())
			{
				FScreenshotRequest::RequestScreenshot(
					NightFiveCaptionScreenshotDirectory / CaptureNames[CueIndex], true, false);
				NightFiveCaptionDrawMask |= CueBit;
			}
		}
	}
	switch (NightFiveProbeStep)
	{
	case 0:
		if (!IsSystemMenuRowEnabled(IGFrontendMenuLayout::NightFiveAction))
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("MISSINGFLOOR_NIGHT5 FAIL: the row was not selectable"));
			RequestNightFiveProbeExit(true);
			return false;
		}
		ConfirmSystemMenuSelection();
		if (!bNightFivePlaying)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("MISSINGFLOOR_NIGHT5 FAIL: confirming did not start it"));
			RequestNightFiveProbeExit(true);
			return false;
		}
		NightFiveProbeStep = 1;
		NightFiveProbeSeconds = 0.0f;
		return true;

	case 1:
		// 방금 낸 소리가 멈춘 타이틀 월드에서 실제로 울고 있는지. UI 소리가 아니면
		// 엔진이 시작시키지 않아서, 자막만 뜨고 노크는 들리지 않는다.
		if (UAudioComponent* Cue = NightFiveLastCue.Get())
		{
			NightFiveLastCue.Reset();
			if (!Cue->bIsUISound || !Cue->IsPlaying())
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("MISSINGFLOOR_NIGHT5 FAIL: night five cue is not playing while paused"));
				RequestNightFiveProbeExit(true);
				return false;
			}
		}
		// 두 소리가 저작된 시각에 나갔는지. 순서가 뒤집히면 대답이 먼저 온다.
		if (NightFiveProbeSeconds >= 14.0f && NightFiveCuesPlayed < 2)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("MISSINGFLOOR_NIGHT5 FAIL: cues=%d after %.1fs"),
				NightFiveCuesPlayed,
				NightFiveProbeSeconds);
			RequestNightFiveProbeExit(true);
			return false;
		}
		if (NightFiveProbeSeconds < NightFiveTotalSeconds - 1.0f)
		{
			if (!bNightFivePlaying)
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("MISSINGFLOOR_NIGHT5 FAIL: it ended early at %.1fs"),
					NightFiveProbeSeconds);
				RequestNightFiveProbeExit(true);
				return false;
			}
			// 아직 재생 중이다. 계속 틱해야 한다 — 여기서 false를 돌려주면
			// 프로브가 첫 대기에서 스스로 멈춘다.
			return true;
		}
		NightFiveProbeStep = 2;
		return true;

	default:
		if (bNightFivePlaying)
		{
			if (NightFiveProbeSeconds > NightFiveTotalSeconds + 3.0f)
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("MISSINGFLOOR_NIGHT5 FAIL: it never returned to the title"));
				RequestNightFiveProbeExit(true);
				return false;
			}
			// 아직 끝나지 않았다. 마지막 1초를 기다린다.
			return true;
		}
		if (NightFiveCuesPlayed != 2
			|| !bNightFiveSpent
			|| !IsSystemMenuRowEnabled(IGFrontendMenuLayout::NightFiveAction))
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("MISSINGFLOOR_NIGHT5 FAIL: cues=%d spent=%d selectable=%d"),
				NightFiveCuesPlayed,
				bNightFiveSpent ? 1 : 0,
				IsSystemMenuRowEnabled(IGFrontendMenuLayout::NightFiveAction)
					? 1
					: 0);
			RequestNightFiveProbeExit(true);
			return false;
		}
		if (bNightFiveCaptionReview)
		{
			if (NightFiveCaptionReturnFrame == 0)
			{
				NightFiveCaptionReturnFrame = GFrameCounter;
				return true;
			}
			if (GFrameCounter <= NightFiveCaptionReturnFrame + 2)
			{
				return true;
			}
			const AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(GetHUD());
			if (NightFiveCaptionDrawMask != 3 || !HorrorHUD
				|| HorrorHUD->WasAudioCaptionDrawnInLastHudFrame() || HorrorHUD->HasPendingAudioCaption()
				|| IFileManager::Get().FileSize(*(NightFiveCaptionScreenshotDirectory / TEXT("night-five-signal.png"))) < 128
				|| IFileManager::Get().FileSize(*(NightFiveCaptionScreenshotDirectory / TEXT("night-five-answer.png"))) < 128)
			{
				UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_NIGHT5 FAIL: caption evidence or title cleanup missing"));
				RequestNightFiveProbeExit(true);
				return false;
			}
			bNightFiveCaptionReturnVerified = true;
		}
		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_NIGHT5 PASS: row under continue in all four "
				"layouts, never while paused, two cues in %.1fs, dimmed but "
				"still selectable, no save file written"),
			NightFiveTotalSeconds);
		RequestNightFiveProbeExit(false);
		return false;
	}
}

FVector AIGPlayerController::NightFiveListenPoint() const
{
	// 타이틀에는 폰이 없다. 카메라 자리에서 재생하면 감쇠는 형식이 되고,
	// 거리는 §10.4의 리버브 센드가 말한다.
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	return ViewLocation;
}

FVector AIGPlayerController::NightFiveAnswerPoint() const
{
	// 복도 끝. 보는 방향의 뒤 왼쪽이라 그녀의 신호(카메라 자리)와 방향부터 갈린다.
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Forward = ViewRotation.Vector().GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	return ViewLocation
		- Forward * IGNightFive::AnswerBehindCentimeters
		- Right * IGNightFive::AnswerLeftCentimeters;
}

void AIGPlayerController::PlayNightFive()
{
	UWorld* World = GetWorld();
	if (!World || bNightFivePlaying || !bNightFiveAvailable)
	{
		return;
	}
	// 로딩이 없다. 레벨도, 세이브도, 새 액터도 만들지 않는다 — 타이틀 위에
	// 검정 한 장과 두 개의 소리를 올릴 뿐이다(§14: 실제 세이브 파일은 만들지
	// 않는다).
	bNightFivePlaying = true;
	NightFiveSeconds = 0.0f;
	NightFiveCuesPlayed = 0;
	// 같은 공간 잔향. 엔딩 B는 별관 복도 안이고, 그 대답은 복도 끝에서 왔다.
	if (UIGMissingFloorAudioSubsystem* AudioDirector =
		World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
	{
		AudioDirector->SetAcousticSpace(EIGAcousticSpace::Corridor);
		// 타이틀의 조율과 사냥 노크는 그동안 비킨다. 30초의 침묵이 이 슬롯의 내용이다.
		AudioDirector->HoldTitleSoundscape(true);
	}
	NightFiveLastCue.Reset();
	RefreshMenuHud();
	NightFiveTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this,
			&AIGPlayerController::AdvanceNightFive));
}

bool AIGPlayerController::AdvanceNightFive(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !bNightFivePlaying)
	{
		EndNightFive();
		return false;
	}
	NightFiveSeconds += DeltaSeconds;

	// 그녀 자신의 손. 게임이 기억한 마지막 입력이 그대로 돌아온다.
	if (NightFiveCuesPlayed == 0
		&& NightFiveSeconds >= IGNightFive::SignalAtSeconds)
	{
		NightFiveCuesPlayed = 1;
		// 타이틀이 월드를 멈춰 두었으므로 UI 소리로 내야 엔진이 시작시킨다.
		NightFiveLastCue = IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateAnswerKnockPattern(this, 0.0f),
			NightFiveListenPoint(),
			IGNightFive::SignalVolume,
			1.0f,
			IGNightFive::CloseRadius,
			IGNightFive::CloseFalloff,
			EIGAudioBus::Player,
			/*bPlayWhenPaused=*/true);
		AIGHorrorHUD::PushAudioCaption(
			this,
			NSLOCTEXT("IGMissingFloor", "NightFiveSignal", "둘, 쉬고, 하나"),
			3.0f);
		return true;
	}

	// 복도 끝에서 대답 둘. 새 사건이 아니라 이미 있었던 대답이다.
	if (NightFiveCuesPlayed == 1
		&& NightFiveSeconds >= IGNightFive::AnswerAtSeconds)
	{
		NightFiveCuesPlayed = 2;
		NightFiveLastCue = IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateWallKnockReply(this),
			NightFiveAnswerPoint(),
			IGNightFive::AnswerVolume,
			1.0f,
			IGNightFive::FarRadius,
			IGNightFive::FarFalloff,
			EIGAudioBus::Entity,
			/*bPlayWhenPaused=*/true);
		AIGHorrorHUD::PushAudioCaption(
			this,
			NSLOCTEXT("IGMissingFloor", "NightFiveAnswer", "복도 끝에서 대답하듯 두 번 두드리는 소리"),
			3.4f);
		return true;
	}

	if (NightFiveSeconds >= NightFiveTotalSeconds)
	{
		EndNightFive();
		return false;
	}
	return true;
}

void AIGPlayerController::EndNightFive()
{
	if (NightFiveTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(NightFiveTicker);
		NightFiveTicker.Reset();
	}
	if (!bNightFivePlaying)
	{
		return;
	}
	bNightFivePlaying = false;
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			// 타이틀의 조율과 노크가 처음 들어왔을 때처럼 다시 든다.
			AudioDirector->HoldTitleSoundscape(false);
		}
	}
	// 한 번 재생하면 흐려진다. 사라지지는 않는다 — 다시 들을 수 있다.
	bNightFiveSpent = true;
	NightFiveSeconds = 0.0f;
	// 타이틀로 복귀한다. 애초에 떠난 적이 없으므로 되돌릴 상태도 없다.
	SystemMenuSelection = IGFrontendMenuLayout::NightFiveAction;
	RefreshMenuHud();
}

bool AIGPlayerController::HasEndingBAutosave() const
{
	if (bNightFiveProbeRequested)
	{
		// 하네스 우회. 세이브를 만들거나 고치지 않고 **조회 결과만** 덮어써서
		// 그 행이 있는 세계를 만든다(§14: 실제 세이브 파일은 만들지 않는다).
		// 이 자리에 두는 이유는 메뉴가 새로 그려질 때마다 조회가 다시 돌기
		// 때문이다 — 플래그를 한 번 세워 두는 방식은 곧 지워진다.
		return true;
	}
	// 결말 B를 본 프로필이면 된다. 예전 빌드의 세이브(결말 뒤 아침이 저장된
	// 것)도 계속 인정한다.
	if (IGOnboardingMemory::HasSeenEnding(FName(TEXT("Ending.B"))))
	{
		return true;
	}
	const UIGSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	return SaveSubsystem && SaveSubsystem->HasEndingBAutosave();
}

UIGAccessibilitySubsystem*
AIGPlayerController::GetAccessibilitySubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
}

UIGSaveSubsystem* AIGPlayerController::GetSaveSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance
		? GameInstance->GetSubsystem<UIGSaveSubsystem>()
		: nullptr;
}

void AIGPlayerController::ApplyDefaultInputMapping() const
{
	if (!IsLocalController() || !DefaultMappingContext)
	{
		return;
	}

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			InputSubsystem->AddMappingContext(DefaultMappingContext, DefaultMappingPriority);
		}
	}
}
