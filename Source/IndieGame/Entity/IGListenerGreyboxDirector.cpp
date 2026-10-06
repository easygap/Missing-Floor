#include "Entity/IGListenerGreyboxDirector.h"

#include "AssetCompilingManager.h"
#include "Containers/Ticker.h"
#include "IndieGame.h"
#include "Audio/IGAmbienceSoundWave.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Core/IGGameInstance.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Interaction/IGReadableNote.h"
#include "Interaction/IGSwingDoor.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "Player/IGHorrorHUD.h"
#include "Kismet/GameplayStatics.h"
#include "Player/IGPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/IGStressComponent.h"
#include "ShaderCompiler.h"
#include "Sound/SoundWave.h"
#include "UnrealClient.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGMissingFloorEpilogueDirector.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorFifthDawnDirector.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "Entity/IGMissingFloorMercyDirector.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGMissingFloorNightTwoBeatDirector.h"
#include "Entity/IGMissingFloorPuzzleOneDirector.h"
#include "Entity/IGMissingFloorPuzzleTwoDirector.h"
#include "Entity/IGNightOneBeatDirector.h"
#include "Interaction/IGZoneTrigger.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Environment/IGCctvChannelFive.h"
#include "Environment/IGDustSubsystem.h"
#include "Environment/IGSettledDustComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Player/IGBeamDustComponent.h"
#include "Player/IGFlashlightComponent.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGRecordingSubsystem.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGInteractionComponent.h"
#include "InputKeyEventArgs.h"
#include "Save/IGSaveSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"

namespace IGListenerGreybox
{
	// 복도를 세운 것과 같은 상수를 쓴다. 이제 주석이 아니라 코드가 그렇다.
	constexpr float FourthFloorZ = AIGPrologueWorldScene::FourthFloorZ;
	constexpr float EntityHalfHeight = 58.0f;
	// 403호 냉장고 험. §5.1의 첫 마스킹 주머니다. 자리는 냉장고에게
	// 묻는다 — 좌표를 여기 다시 적으면 냉장고만 옮겨지고 험은 옛
	// 자리에 남는다. 기계 몸통 한가운데 높이만 여기서 정한다.
	constexpr float FridgeHumHeightOffset = 60.0f;
	constexpr float FridgeHumRadius = 200.0f;
	constexpr float FridgeHumMasking = 0.2f;

	// §5.1의 세 번째 험. 복도 동쪽 끝 설비 벽장이고, 자리는 벽장에게 묻는다.
	// 반경과 마스킹은 다른 둘과 같아야 한다 — 기계마다 다르면 플레이어가
	// 배운 「기계 옆이 안전하다」가 기계마다 다른 규칙이 된다.
	constexpr float BoilerHumRadius = 200.0f;
	constexpr float BoilerHumMasking = 0.2f;

	constexpr float SetupRetryIntervalSeconds = 0.3f;
	constexpr float SetupGiveUpSeconds = 6.0f;
	constexpr float ProbePollSeconds = 0.25f;

	/**
	 * 낮의 밤 베드. 안정기 떨림과 벽 사이 공기는 그 시간의 소리라서 낮에는
	 * 거의 없어야 한다 — 세계가 멀쩡하다는 감각은 낮의 몫이다(§10.2). 0이
	 * 아닌 것은 녹음 베드 위에 얹힌 두 번째 복도 공기가 통째로 빠지면 낮이
	 * 진공이 되기 때문이다.
	 */
	constexpr float NightBedDayScale = 0.22f;
	/**
	 * 밤1 여는 시각표(§8 0-5, 1-1). 기준은 깨는 순간이다. 카드(4.2초)와 알람이
	 * 먼저이고, 카드가 걷힌 뒤에야 천장이 세 번 두드린다. 카드 밑에서 나는
	 * 소리는 아무도 보지 못한다.
	 */
	constexpr float NightOneCeilingKnockSeconds = 4.6f;
	/** 천장 노크가 시작되고 이만큼 뒤에 같은 자리가 끌린다. 노크 셋(2.6초)이 먼저 끝난다. */
	constexpr float NightOneDragDelaySeconds = 4.6f;
	constexpr float NightOneDragSeconds = 1.35f;
	/** 첫 밤, 노크가 멎고 손이 머리맡을 더듬는다. 노크 기준 초, 그리고 딸깍까지. */
	constexpr float NightOneTorchReachSeconds = 1.9f;
	constexpr float NightOneTorchClickSeconds = 0.6f;
	/** 끌림이 시작되고 이만큼 뒤에 유담이 천장을 보며 한 줄 생각한다. */
	constexpr float NightOneTopFloorThoughtSeconds = 1.5f;

	/**
	 * 밤1 결론(§8 1-6). T1이 맞물리면 조율음이 먼저 울리고, 그 뒤에 위에서
	 * 안정기가 켜진다. 독백 한 줄을 읽을 시간을 두고 새벽이 온다.
	 */
	constexpr float NightOneBallastCueSeconds = 1.4f;
	constexpr float NightOneRealizationSeconds = 2.6f;
	constexpr float NightOneDawnDelaySeconds = 6.6f;
	/** 검침표를 펼친 채였다면 내려놓은 뒤 이만큼. 멈춰 있던 독백이 그제야 뜬다. */
	constexpr float NightOneDawnAfterNoteSeconds = 2.8f;
	constexpr float NightOneDawnPollSeconds = 0.5f;
	/** 그가 이 안에서 소리를 좇고 있으면 여유를 두지 않는다. 그 틈에 잡힌다. */
	constexpr float NightOneSolveThreatDistance = 900.0f;

	/**
	 * 밤2~4를 여는 소리(§8 2-1·3-1·4-1). 밤2는 NightTwoBeats가 6초에 현관문을
	 * 두드린다. 그가 문 앞에 세워지기 전까지 복도 첫 칸에 붙들어 두는 한도.
	 */
	constexpr float NightTwoDoorHoldSeconds = 6.75f;
	/** 밤3·밤4와 문 비트를 다 쓴 되풀이 밤2를 여는 소리. 카드(4.2초)가 걷힌 뒤다. */
	constexpr float NightOpeningSettleSeconds = 6.0f;
	/** 밤3 옥상 철문의 문짝 가운데. 둘째 돌풍(1.65초)이 부풀 때 문이 덜컹인다. */
	const FVector NightThreeRoofGate(-277.5f, 220.0f, 1300.0f);
	constexpr float NightThreeRoofRattleSeconds = 1.8f;
	/**
	 * 밤4. 드릴(10.9초)이 멎고 조금 뒤 5층 바닥의 발소리 넷이 철문 쪽으로 가고
	 * 문이 닫힌다. 발소리 자리는 벽 앞에서 5층 철문 안쪽까지다.
	 */
	constexpr float MokLeavesAfterDrillSeconds = 11.6f;
	constexpr float MokStepSpacingSeconds = 0.55f;
	constexpr int32 MokStepCount = 4;
	const FVector MokStepsFrom(236.0f, 660.0f, 1205.0f);
	const FVector MokStepsTo(140.0f, 480.0f, 1205.0f);
	const FVector MokAnnexDoor(130.0f, 452.5f, 1300.0f);
	/** 드릴이 멎은 직후의 한 줄. 요구서의 일곱 시와 지금의 네 시 반. */
	constexpr float NightFourDrillThoughtSeconds = 11.9f;
	const FName NightFourDrillBeat(TEXT("Night4.MokDrill"));

	/** 공동현관을 민 독백이 뜨고 폰을 꺼내 걸어 보기까지. 소리가 글보다 먼저다. */
	constexpr float NoSignalDelaySeconds = 2.0f;
	const FName NoSignalBeat(TEXT("Hour.NoSignal"));

	/**
	 * 1-7. 밤1을 채운 낮, 401호 문을 등지고 지나가면 문이 조금 열렸다 닫힌다.
	 * 문짝은 움직이지 않는 증거라 보고 있을 때는 열지 않는다. 소리는 복도 쪽
	 * 문면에서 낸다 — 공유 월드에서 401호 문 안쪽은 403호 방이다.
	 */
	const FName HwangPeekBeat(TEXT("Night1.HwangPeek"));
	const FVector HwangPeekDoor(-140.0f, -246.0f, 1010.0f);
	constexpr float HwangPeekPollSeconds = 0.5f;
	constexpr float HwangPeekMinDistance = 120.0f;
	constexpr float HwangPeekMaxDistance = 700.0f;
	/** 문을 보고 있지 않다: 시선과 문 방향의 내적이 이보다 작다. */
	constexpr float HwangPeekFacingDot = 0.2f;
	constexpr float HwangPeekLineSeconds = 0.9f;
	constexpr float HwangPeekCloseSeconds = 3.6f;
	/**
	 * 문이 열려 있는 동안 시선을 보는 간격. 내적 0.2는 문이 화각 밖에 있을
	 * 때라, 돌아보는 도중에 닫히는 소리가 먼저 난다.
	 */
	constexpr float HwangPeekScenePollSeconds = 0.1f;
	/** 걸쇠와 문 열리는 소리(0.6~0.7초)가 거의 다 난 뒤에야 닫을 수 있다. */
	constexpr float HwangPeekMinOpenSeconds = 0.5f;

	/**
	 * 신고 뒤의 낮, 황순금에게 벽의 대답을 전하는 첫 노크. 대화 줄이 빌 때까지
	 * 기다리되, 문 앞을 떠나거나 너무 오래 걸리면 접고 다음 노크에 넘긴다.
	 */
	const FName HwangPermissionBeat(TEXT("Day.Hwang.Permission"));
	constexpr float HwangPermissionPollSeconds = 0.25f;
	constexpr float HwangPermissionMaxWaitSeconds = 45.0f;
	constexpr float HwangPermissionReach = 400.0f;

	/**
	 * 콜드 오픈(§8 0-1, §26.2). 검은 화면에서 계약서가 놓인 방이 먼저 드러나고,
	 * 위에서 바퀴 하나가 구르다 멎은 뒤에 제목이 선다. 카드가 먼저 오면 소리가
	 * 카드 밑에 묻힌다. 제목은 페이드인까지 4.3초 안에 선다.
	 */
	constexpr float ArrivalOpeningFadeSeconds = 1.2f;
	constexpr float ArrivalCasterRollSeconds = 1.5f;
	constexpr float ArrivalTitleCardSeconds = 3.6f;
	/** 옥상 자물쇠를 흔들고 이만큼 뒤, 철문 너머에서 쇠붙이가 내려앉는다. */
	constexpr float ArrivalRoofClinkSeconds = 1.5f;

	/**
	 * 401호 라디오의 자리. 문짝 바로 앞 복도 쪽, 귀를 댈 높이다. 공유 월드에서
	 * 401호 문 안쪽은 403호 방이라 소리를 문 너머에 두면 방 안에서 들린다.
	 * 문 앞에 두고 필터로 철문을 씌운다.
	 */
	const FVector Unit401PrayerLocation(-150.0f, -244.0f, 1030.0f);
	constexpr float Unit401PrayerVolume = 0.46f;
	/** 지나가며 듣는 크기. 귀를 대면 페이더가 1까지 오른다. */
	constexpr float Unit401PrayerDayLevel = 0.3f;
	constexpr float Unit401PrayerDoorLowPassHz = 1200.0f;

	/** 402호 메모와 문 안의 정적을 한데 묶는 한 줄. 에필로그의 냉장고 문장이 이것을 되받는다. */
	static FText Unit402VacancyHeardThought()
	{
		return NSLOCTEXT(
			"IGMissingFloor",
			"Unit402VacancyThought",
			"메모는 붙어 있는데, 안에서 냉장고 소리도 안 나네.");
	}
}

AIGListenerGreyboxDirector::AIGListenerGreyboxDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AIGListenerGreyboxDirector::BeginPlay()
{
	Super::BeginPlay();
	bProductionMode = GetWorld()
		&& (GetWorld()->URL.HasOption(TEXT("IGMissingFloor"))
			|| FParse::Param(FCommandLine::Get(), TEXT("IGMissingFloor")));

	bProbeRequested =
		FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreyboxProbe"));
	bEndingCheckpointWrite = FParse::Param(FCommandLine::Get(), TEXT("IGEndingCheckpointWrite"));
	bEndingProfileProbe = FParse::Param(FCommandLine::Get(), TEXT("IGEndingProfileRead"));
	FParse::Value(FCommandLine::Get(), TEXT("IGEndingResume="), EndingProbeChoice);
	if (bEndingCheckpointWrite || bEndingProfileProbe || !EndingProbeChoice.IsEmpty())
	{
		EndingProbeStartedAt = FPlatformTime::Seconds();
		SetActorTickEnabled(true);
		if ((bEndingCheckpointWrite && !bProbeRequested)
			|| (!bEndingCheckpointWrite && (bProbeRequested
				|| (EndingProbeChoice != TEXT("A") && EndingProbeChoice != TEXT("B"))
				|| FParse::Param(FCommandLine::Get(), TEXT("IGSkipFrontend"))
				|| FParse::Param(FCommandLine::Get(), TEXT("IGFreshOnboarding")))))
		{
			FinishEndingLifecycleProbe(false, TEXT("엔딩 검사의 실행 인자가 잘못됨"));
			return;
		}
		if (bEndingProfileProbe) { return; }
	}
	bArrivalProbeRequested =
		FParse::Param(FCommandLine::Get(), TEXT("IGArrivalProbe"));
	bArrivalCaptureRequested =
		FParse::Param(FCommandLine::Get(), TEXT("IGArrivalCapture"));
	bNightCaptureRequested =
		FParse::Param(FCommandLine::Get(), TEXT("IGNightCapture"));
	bCaptureMetricsOnly = FParse::Param(FCommandLine::Get(), TEXT("IGCaptureMetricsOnly"));
	bMercyNoteProbeRequested =
		FParse::Param(FCommandLine::Get(), TEXT("IGM65MercyNoteProbe"));
	bHistogramRequested =
		FParse::Param(FCommandLine::Get(), TEXT("IGNightHistogram"));
	bHistogramReportOnly =
		FParse::Param(FCommandLine::Get(), TEXT("IGNightHistogramReport"));
	bCctvFeedProbeRequested =
		FParse::Param(FCommandLine::Get(), TEXT("IGCctvFeedProbe"));
	// 저장 검사는 새 프로세스에서 실제 불러오기와 맵 이동을 거친다.
	// 입주 장면을 먼저 만들면 초기 자동 저장이 검사할 파일을 덮을 수 있다.
	if ((FParse::Param(FCommandLine::Get(), TEXT("IGArrivalSaveRead"))
			|| !EndingProbeChoice.IsEmpty())
		&& !GetWorld()->URL.HasOption(TEXT("IGResumeSave")))
	{
		UIGSaveSubsystem* Save = GetGameInstance()->GetSubsystem<UIGSaveSubsystem>();
		if (!Save || !Save->RequestLoadLatestAutosave())
		{
			if (!EndingProbeChoice.IsEmpty())
			{
				FinishEndingLifecycleProbe(false, TEXT("선택 직전 저장을 불러오지 못함"));
			}
			else { FailProbe(TEXT("다시 실행한 게임에서 자동 저장을 불러오지 못함")); }
		}
		return;
	}

	// 새 게임은 검은 화면으로 연다(§8 0-1). 빌라가 지어지는 첫 프레임을 가리고,
	// 무대가 서면 콜드 오픈이 걷는다. 무대가 서기 전에 검게 하지 않으면 걷히는
	// 페이드가 이미 보이던 방을 한 번 깜빡인다.
	if (bProductionMode && !IsScriptedRun())
	{
		const UIGMissingFloorNarrativeSubsystem* OpeningNarrative = GetNarrative();
		APlayerController* OpeningController = GetWorld()->GetFirstPlayerController();
		if (OpeningNarrative && OpeningNarrative->GetNightIndex() == 0
			&& !OpeningNarrative->HasBeatPlayed(FName(TEXT("Arrival.Started")))
			&& OpeningController && OpeningController->PlayerCameraManager)
		{
			OpeningController->PlayerCameraManager->SetManualCameraFade(
				1.0f, FLinearColor::Black, /*bInFadeAudio=*/false);
			bArrivalOpeningHeldBlack = true;
		}
	}

	// The procedural villa and the player pawn appear over the first frames;
	// poll briefly instead of assuming a build order.
	GetWorldTimerManager().SetTimer(
		SetupTimer,
		this,
		&AIGListenerGreyboxDirector::TrySetupStage,
		IGListenerGreybox::SetupRetryIntervalSeconds,
		true);
}

void AIGListenerGreyboxDirector::TrySetupStage()
{
	SetupRetrySeconds += IGListenerGreybox::SetupRetryIntervalSeconds;
	if (SetupStage())
	{
		GetWorldTimerManager().ClearTimer(SetupTimer);
		bStageReady = true;
		// 입주가 아닌 길로 무대가 섰다면 검은 화면을 여기서 걷는다.
		ReleaseArrivalOpeningBlack();
		if (!EndingProbeChoice.IsEmpty()) { return; }
		if (FParse::Param(FCommandLine::Get(), TEXT("IGArrivalSaveRead")))
		{
			const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
			bool bRestored = bProductionMode && Narrative && WorldScene.IsValid()
				&& Narrative->GetNightIndex() == 0 && !Narrative->IsHourSealed()
				&& !WorldScene->IsTheHourSealed() && Entity && Entity->IsDormant()
				&& SleepTarget && SleepTarget->IsInteractionEnabled()
				&& ArrivalContract && ArrivalContract->IsInteractionEnabled()
				&& AreArrivalBoxesOpened();
			for (const TCHAR* Beat : {TEXT("Arrival.Started"), TEXT("Arrival.Contract"),
				TEXT("Arrival.Store"), TEXT("Arrival.Unit401"), TEXT("Arrival.Unit402"),
				TEXT("Arrival.RoofDoor"), TEXT("Arrival.Complete"), TEXT("Neighborhood.Delivery")})
			{
				bRestored = bRestored && Narrative->HasBeatPlayed(FName(Beat));
			}
			if (!bRestored)
			{
				FailProbe(TEXT("다시 실행한 게임에서 입주 조사와 취침 상태가 복구되지 않음"));
				return;
			}
			RequestExit(false);
			return;
		}
		// The capture tour, the V5 sweep and the probe are mutually exclusive
		// drivers of the same stage. The two that need real pixels win.
		if (bNightCaptureRequested)
		{
			float WarmupSeconds = 0.0f;
			FParse::Value(FCommandLine::Get(), TEXT("IGPerformanceWarmupSeconds="), WarmupSeconds);
			WarmupSeconds = FMath::IsFinite(WarmupSeconds)
				? FMath::Clamp(WarmupSeconds, 0.0f, 600.0f) : 0.0f;
			if (WarmupSeconds <= 0.0f)
			{
				StartNightCapture();
			}
			else
			{
				// 성능 검수의 준비 시간은 게임 배속이나 고정 프레임 수로 세지 않는다.
				// 무대를 그대로 그리다가 지정한 실제 시간이 지나면 같은 경로를 시작한다.
				const double StartedAt = FPlatformTime::Seconds();
				const bool bHourWasPaused = NightPhase && NightPhase->IsHourPaused();
				const bool bEntityTickWasEnabled = Entity && Entity->IsActorTickEnabled();
				if (NightPhase) { NightPhase->SetHourPaused(true); }
				if (Entity) { Entity->SetActorTickEnabled(false); }
				UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_PERFORMANCE_WARMUP START seconds=%.2f"), WarmupSeconds);
				GetWorldTimerManager().SetTimer(NightCaptureWarmupTimer,
					FTimerDelegate::CreateWeakLambda(this, [this, StartedAt, WarmupSeconds, bHourWasPaused, bEntityTickWasEnabled]()
					{
						const double Elapsed = FPlatformTime::Seconds() - StartedAt;
						if (Elapsed < WarmupSeconds) { return; }
						GetWorldTimerManager().ClearTimer(NightCaptureWarmupTimer);
						if (NightPhase) { NightPhase->SetHourPaused(bHourWasPaused); }
						if (Entity) { Entity->SetActorTickEnabled(bEntityTickWasEnabled); }
						UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_PERFORMANCE_WARMUP DONE seconds=%.2f"), Elapsed);
						StartNightCapture();
					}), 0.1f, true);
			}
		}
		else if (bHistogramRequested)
		{
			StartHistogramSweep();
		}
		else if (bArrivalCaptureRequested)
		{
			StartArrivalCapture();
		}
		else if (bArrivalProbeRequested)
		{
			RunArrivalProbe();
		}
		else if (bProbeRequested)
		{
			StartProbe();
		}
		return;
	}
	if (SetupRetrySeconds >= IGListenerGreybox::SetupGiveUpSeconds)
	{
		GetWorldTimerManager().ClearTimer(SetupTimer);
		ReleaseArrivalOpeningBlack();
		if (bArrivalCaptureRequested || bArrivalProbeRequested)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("MISSINGFLOOR_ARRIVAL FAIL: stage setup timed out capture=%d"),
				bArrivalCaptureRequested ? 1 : 0);
			RequestExit(true);
		}
		else if (bProbeRequested)
		{
			FailProbe(TEXT("stage setup timed out (world scene or player missing)"));
		}
	}
}

void AIGListenerGreyboxDirector::SpawnNightAmbienceBeds()
{
	UWorld* World = GetWorld();
	AIGPrologueWorldScene* Scene = WorldScene.Get();
	if (!World || !Scene || NightAmbienceBeds.Num() > 0)
	{
		return;
	}
	UIGMissingFloorAudioSubsystem* AudioDirector =
		World->GetSubsystem<UIGMissingFloorAudioSubsystem>();

	// 복도 베드는 조명 기구들의 한가운데. 계단실은 반층 참, 5층은 별관 한가운데,
	// 옥상은 계단탑 문과 별관 문 사이, 로비는 켜 둔 우편함 등 아래.
	FVector CorridorCenter = FVector::ZeroVector;
	const int32 FixtureCount = Scene->GetCorridorFixtureCount();
	for (int32 Index = 0; Index < FixtureCount; ++Index)
	{
		CorridorCenter += Scene->GetCorridorFixtureLocation(Index);
	}
	if (FixtureCount > 0)
	{
		CorridorCenter /= static_cast<float>(FixtureCount);
	}
	else
	{
		CorridorCenter = FVector(200.0f, -305.0f, IGListenerGreybox::FourthFloorZ + 120.0f);
	}
	struct FBedSpec
	{
		const TCHAR* Name;
		EIGAmbienceMode Mode;
		FVector Location;
		float Volume;
		float InnerRadius;
		float Falloff;
		uint32 Seed;
		const TCHAR* SampleName;
		float SampleVolume;
	};
	const FBedSpec Specs[] = {
		{TEXT("NightBedCorridor"), EIGAmbienceMode::CorridorNight,
			FVector(CorridorCenter.X, CorridorCenter.Y, IGListenerGreybox::FourthFloorZ + 140.0f),
			0.55f, 700.0f, 1900.0f, 0x7A11C0DEu, TEXT("Bed_Corridor"), 0.12f},
		{TEXT("NightBedStairwell"), EIGAmbienceMode::Stairwell,
			FVector(-465.0f, 35.0f, IGListenerGreybox::FourthFloorZ - 30.0f),
			0.60f, 260.0f, 1100.0f, 0x51A1B2C3u, TEXT("Wind_Gap"), 0.055f},
		// 5층과 옥상은 녹음을 쓰지 않는다. 가진 녹음이 복도 공기와 문틈 바람뿐이라
		// 그걸 깔면 어느 층에 올라가도 공기가 같다. 5층은 덜 지은 증축층의 먼지
		// 바람과 목재, 옥상은 박자 없이 부풀다 죽는 바깥바람이다. 둘 다 반경을
		// 좁게 둬서 슬래브 아래 4층과 403호에는 먹먹하게만 샌다.
		{TEXT("NightBedUpperFloor"), EIGAmbienceMode::UpperFloor,
			FVector(0.0f, 700.0f, IGListenerGreybox::FourthFloorZ + 420.0f),
			0.40f, 380.0f, 700.0f, 0x9C0FFEE1u, nullptr, 0.0f},
		{TEXT("NightBedRooftop"), EIGAmbienceMode::StreetWind,
			FVector(-60.0f, 300.0f, IGListenerGreybox::FourthFloorZ + 430.0f),
			0.55f, 380.0f, 700.0f, 0x2F0F1A3Du, nullptr, 0.0f},
		// 밤2의 로비와 관리실. 같은 건물의 공용 공간이라 복도와 같은 공기를 낮게
		// 깐다. 막힌 유리문 너머 도로는 도로 베드가 따로 준다. 기계 험은 두지
		// 않는다 — 「기계 옆은 들키지 않는다」는 험 존 셋만의 규칙이다. 관리실에는
		// 문 너머로 희미하게만 든다. 안쪽 방의 험은 귀를 대야 들린다.
		{TEXT("NightBedLobby"), EIGAmbienceMode::CorridorNight,
			FVector(560.0f, -265.0f, 150.0f),
			0.40f, 260.0f, 700.0f, 0x6A3B7C11u, TEXT("Bed_Corridor"), 0.09f},
	};
	for (const FBedSpec& Spec : Specs)
	{
		USoundBase* Wave = Spec.SampleName ? IGAudio::Sample(Spec.SampleName) : nullptr;
		const bool bRecorded = Wave != nullptr;
		if (!Wave)
		{
			UIGAmbienceSoundWave* Fallback = NewObject<UIGAmbienceSoundWave>(this);
			Fallback->Configure(Spec.Mode, Spec.Seed);
			Wave = Fallback;
		}
		UAudioComponent* Bed = NewObject<UAudioComponent>(this, Spec.Name);
		Bed->RegisterComponent();
		Bed->SetWorldLocation(Spec.Location);
		Bed->SetSound(Wave);
		// 녹음은 합성 베드보다 원음이 크다. 노크의 빈자리를 덮지 않게 섞는다.
		Bed->SetVolumeMultiplier(bRecorded ? Spec.SampleVolume : Spec.Volume);
		Bed->bAutoActivate = false;
		Bed->AttenuationSettings = IGAudio::MakeAttenuation(
			this, Spec.InnerRadius, Spec.Falloff, EIGAudioBus::World);
		Bed->bAllowSpatialization = true;
		if (AudioDirector)
		{
			AudioDirector->PrepareSound(Wave, EIGAudioBus::World);
			AudioDirector->RegisterPersistentBed(Bed, EIGAudioBus::World);
		}
		// 같은 녹음을 층마다 같은 위치에서 재생하면 위아래 소리가 달라붙는다.
		const USoundWave* RecordedWave = Cast<USoundWave>(Wave);
		const float ClipSeconds = RecordedWave ? RecordedWave->Duration : 0.0f;
		const float StartOffset = bRecorded
			? FMath::Fmod(static_cast<float>(Spec.Seed % 1100u) * 0.01f,
				FMath::Max(0.1f, ClipSeconds)) : 0.0f;
		// 낮에 깔린다. 볼륨 배수는 그대로 두고 페이더만 낮춰 두면, 밤이 열릴 때
		// HandleHourActiveChanged가 검은 화면 아래서 올린다.
		Bed->FadeIn(0.05f, IGListenerGreybox::NightBedDayScale, StartOffset);
		UE_LOG(LogIndieGame, Display, TEXT("NIGHT_BED %s recorded=%d sound=%s volume=%.3f"),
			Spec.Name, bRecorded, *Wave->GetName(), Bed->VolumeMultiplier);
		NightAmbienceBeds.Add(Bed);
	}
}

void AIGListenerGreyboxDirector::ScheduleNextSettle()
{
	// 규칙적이면 시계가 되고 시계는 무섭지 않다. 간격은 끝까지 무작위다. 밤이
	// 거듭될수록 건물이 잦아진다 — 밤1은 60~120초, 밤4는 35~80초. 한 밤 안에서도
	// 05:12를 넘기면 조여 와서 05:27쯤엔 어느 밤이든 20~50초다. 05:30을 향해
	// 좁혀 오는 것이 귀로 온다. 시간이 멈춘 막간과 낮에는 그 밤의 기본 간격이다.
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const float NightTension = FMath::Clamp(
		static_cast<float>((Narrative ? Narrative->GetNightIndex() : 1) - 1) / 3.0f,
		0.0f,
		1.0f);
	float Tight = 0.0f;
	if (NightPhase && NightPhase->IsHourActive())
	{
		Tight = FMath::SmoothStep(840.0f, 1140.0f, NightPhase->GetHourElapsedSeconds());
	}
	const float Delay = FMath::FRandRange(
		FMath::Lerp(FMath::Lerp(60.0f, 35.0f, NightTension), 20.0f, Tight),
		FMath::Lerp(FMath::Lerp(120.0f, 80.0f, NightTension), 50.0f, Tight));
	GetWorldTimerManager().SetTimer(
		SettleTimerHandle, this, &AIGListenerGreyboxDirector::PlaySettleEvent, Delay, false);
}

void AIGListenerGreyboxDirector::PlaySettleEvent()
{
	UWorld* World = GetWorld();
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	UIGMissingFloorAudioSubsystem* AudioDirector =
		World ? World->GetSubsystem<UIGMissingFloorAudioSubsystem>() : nullptr;
	// 그가 귀를 세우고 있거나 대답에 멈춰 있거나 붙잡은 동안은 3초씩 미룬다. 그 창에
	// 건물이 울면 그가 들을 소리와 섞여 규칙이 흐려지고, 붙잡힌 순간의 노크 둘이 묻힌다.
	if (Entity && !Entity->IsDormant())
	{
		const EIGListenerState ListenerState = Entity->GetListenerState();
		if (ListenerState == EIGListenerState::Listening
			|| ListenerState == EIGListenerState::Holding
			|| ListenerState == EIGListenerState::Waiting
			|| ListenerState == EIGListenerState::CaptureHold
			|| (AudioDirector
				&& AudioDirector->GetThreatState() == EIGAudioThreatState::Captured))
		{
			GetWorldTimerManager().SetTimer(
				SettleTimerHandle, this, &AIGListenerGreyboxDirector::PlaySettleEvent, 3.0f, false);
			return;
		}
	}
	ScheduleNextSettle();
	// 밤에만. 낮에 그는 자고 건물도 잔다.
	if (!World || !PlayerCharacter || !Entity || Entity->IsDormant())
	{
		return;
	}
	if (AudioDirector && AudioDirector->IsAuthoredSilence())
	{
		return;
	}

	// 순번이 아니라 제비뽑기다. 바로 앞에 난 것은 다시 뽑지 않고, 밤이 깊을수록
	// 석고 알갱이보다 배관과 먼 문이 잦다.
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const float NightTension = FMath::Clamp(
		static_cast<float>((Narrative ? Narrative->GetNightIndex() : 1) - 1) / 3.0f,
		0.0f,
		1.0f);
	float KindWeights[4] = {
		FMath::Lerp(1.0f, 1.3f, NightTension),
		1.0f,
		FMath::Lerp(1.3f, 0.7f, NightTension),
		FMath::Lerp(0.5f, 1.0f, NightTension)};
	if (LastSettleKind >= 0 && LastSettleKind < 4)
	{
		KindWeights[LastSettleKind] = 0.0f;
	}
	float Pick = FMath::FRandRange(
		0.0f, KindWeights[0] + KindWeights[1] + KindWeights[2] + KindWeights[3]);
	int32 Kind = 0;
	for (int32 KindIndex = 0; KindIndex < 4; ++KindIndex)
	{
		if (KindWeights[KindIndex] <= 0.0f)
		{
			continue;
		}
		Kind = KindIndex;
		if (Pick < KindWeights[KindIndex])
		{
			break;
		}
		Pick -= KindWeights[KindIndex];
	}
	LastSettleKind = Kind;
	++SettleCounter;
	const bool bFarDoor = Kind == 3;

	// 위에서 난다. 훅이 「위에서 나는 소리」다(§10.5). 방위는 매번 다르게, 밤이
	// 깊을수록 머리 가까이 내려온다. 옥상이나 5층에 서 있으면 건물은 슬래브
	// 아래에 있으니 소리도 발밑에서 온다. 먼 문은 이름대로 멀다.
	const FVector PlayerLocation = PlayerCharacter->GetActorLocation();
	const bool bAboveBuilding =
		(AudioDirector && AudioDirector->GetAcousticSpace() == EIGAcousticSpace::Open)
		|| PlayerLocation.Z > IGListenerGreybox::FourthFloorZ + 250.0f;
	const float BearingYaw = FMath::FRandRange(0.0f, 2.0f * UE_PI);
	const FVector Bearing(FMath::Cos(BearingYaw), FMath::Sin(BearingYaw), 0.0f);
	float Reach = 0.0f;
	float Rise = 0.0f;
	if (bAboveBuilding)
	{
		Reach = bFarDoor ? FMath::FRandRange(600.0f, 900.0f) : FMath::FRandRange(350.0f, 600.0f);
		Rise = bFarDoor ? -300.0f : -280.0f;
	}
	else if (bFarDoor)
	{
		Reach = FMath::FRandRange(450.0f, 700.0f);
		Rise = FMath::FRandRange(360.0f, 460.0f);
	}
	else
	{
		Reach = FMath::FRandRange(
			FMath::Lerp(120.0f, 0.0f, NightTension),
			FMath::Lerp(320.0f, 200.0f, NightTension));
		Rise = FMath::FRandRange(
			FMath::Lerp(260.0f, 225.0f, NightTension),
			FMath::Lerp(330.0f, 270.0f, NightTension));
	}
	const FVector Location = PlayerLocation + Bearing * Reach + FVector(0.0f, 0.0f, Rise);

	// 방위는 자막 앞에 [위]·[아래]로 붙는다. 문장에 또 적으면 두 번 읽힌다.
	USoundBase* Wave = nullptr;
	FText Caption;
	switch (Kind)
	{
	case 0:
		Wave = UIGToneSequenceSoundWave::CreateSettlePipeKnock(this);
		Caption = NSLOCTEXT("IGNight", "SettlePipe", "배관에서 딱 하는 소리");
		break;
	case 1:
		Wave = IGAudio::SampleVariantOr(
			TEXT("Settle_Creak"), 2, static_cast<uint32>(SettleCounter) * 2654435761u,
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSettleTimberCreak(this); });
		Caption = NSLOCTEXT("IGNight", "SettleCreak", "나무 삐걱거리는 소리");
		break;
	case 2:
		Wave = UIGToneSequenceSoundWave::CreateSettlePlasterTick(this);
		Caption = NSLOCTEXT("IGNight", "SettleTick", "벽에 금 가는 소리");
		break;
	default:
		Wave = UIGToneSequenceSoundWave::CreateSettleFarDoorSlam(this);
		Caption = NSLOCTEXT("IGNight", "SettleSlam", "멀리서 문 닫히는 소리");
		break;
	}
	// 같은 파형도 매번 높이와 세기가 조금씩 다르다. 밤이 깊을수록 조금 크고 조금
	// 낮아서 무겁게 들린다.
	const float Weight = FMath::Lerp(1.0f, 1.2f, NightTension);
	const float Depth = FMath::Lerp(1.0f, 0.92f, NightTension);
	if (bFarDoor)
	{
		IGAudio::SpawnOneShotAt(
			this, Wave, Location, 0.72f * Weight, FMath::FRandRange(0.92f, 1.02f) * Depth,
			260.0f, 2200.0f, EIGAudioBus::World);
	}
	else
	{
		IGAudio::SpawnOneShotAt(
			this, Wave, Location, FMath::FRandRange(0.50f, 0.64f) * Weight,
			FMath::FRandRange(0.93f, 1.07f) * Depth, 180.0f, 1700.0f, EIGAudioBus::World);
	}
	AIGHorrorHUD::PushAudioCaptionAt(this, Caption, 2.0f, Location);
}

void AIGListenerGreyboxDirector::DestroyPartialStage()
{
	// 세우는 순서의 역순일 필요는 없다. 서로를 붙들고 있지 않고, 각자
	// EndPlay에서 자기 것만 정리한다.
	AActor* const Built[] = {
		Entity.Get(), NightLoop.Get(), NightPhase.Get(), PuzzleOne.Get(),
		NightOneBeats.Get(), NightTwoBeats.Get(), PuzzleTwo.Get(),
		NightThree.Get(), Mercy.Get(), FifthDawn.Get(), Epilogue.Get(),
		NightFour.Get(), NightThreats.Get(), SleepTarget.Get(), Unit401Door.Get(),
		UsedListingNote.Get(), NeighborhoodDeliveryNote.Get(),
		// SpawnOptionalWitnesses가 세우는 다섯. 지금은 마지막 실패 경로보다
		// 뒤에 있어 새어 나갈 수 없지만, 그 사이에 실패가 하나 생기면 이름이
		// 살아남아 재시도를 막는다.
		WaterBowl.Get(), SleepingPills.Get(), CigarettePack.Get(),
		StoreRoster.Get(), Unit401Radio.Get(),
		// SpawnArrivalInteractables가 세우는 일곱. 그 안의 람다도 이름을
		// 붙여 스폰하므로 남으면 재시도가 같은 이름에 막힌다.
		ArrivalContract.Get(), ArrivalParcelBox.Get(), ArrivalNotebookBox.Get(),
		ArrivalVoicemailBox.Get(), ArrivalStoreBell.Get(),
		ArrivalUnit402Note.Get(), ArrivalRoofLock.Get()};
	for (AActor* Actor : Built)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	Entity = nullptr;
	NightLoop = nullptr;
	NightPhase = nullptr;
	PuzzleOne = nullptr;
	NightOneBeats = nullptr;
	NightTwoBeats = nullptr;
	PuzzleTwo = nullptr;
	NightThree = nullptr;
	Mercy = nullptr;
	FifthDawn = nullptr;
	Epilogue = nullptr;
	NightFour = nullptr;
	NightThreats = nullptr;
	SleepTarget = nullptr;
	Unit401Door = nullptr;
	UsedListingNote = nullptr;
	NeighborhoodDeliveryNote = nullptr;
	GetWorldTimerManager().ClearTimer(NeighborhoodSoundTimer);
	GetWorldTimerManager().ClearTimer(ArrivalHandSoundTimer);
	GetWorldTimerManager().ClearTimer(ArrivalPhoneBuzzTimer);
	for (FTimerHandle* ArrivalTimer : {
		&ArrivalColdOpenSoundTimer, &ArrivalTitleCardTimer, &ArrivalRoofClinkTimer,
		&Unit402VacancyTimer, &Unit401KnockTimer, &Unit401PrayerReturnTimer})
	{
		GetWorldTimerManager().ClearTimer(*ArrivalTimer);
	}
	StopArrivalBaseline();
	WaterBowl = nullptr;
	SleepingPills = nullptr;
	CigarettePack = nullptr;
	StoreRoster = nullptr;
	Unit401Radio = nullptr;
	ArrivalContract = nullptr;
	ArrivalParcelBox = nullptr;
	ArrivalNotebookBox = nullptr;
	ArrivalVoicemailBox = nullptr;
	ArrivalStoreBell = nullptr;
	ArrivalUnit402Note = nullptr;
	ArrivalRoofLock = nullptr;

	// 험은 액터가 아니라 구독이다. 지우지 않으면 시도마다 하나씩 쌓인다.
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			for (int32* Handle : {&FridgeHumHandle, &BoilerHumHandle})
			{
				if (*Handle != INDEX_NONE)
				{
					Noise->UnregisterHumSource(*Handle);
				}
			}
		}
	}
	FridgeHumHandle = INDEX_NONE;
	BoilerHumHandle = INDEX_NONE;

	// 밤 베드와 건물 소리 시계. 이름 붙인 컴포넌트라 남기면 재시도의 NewObject가
	// 같은 이름에 막힌다.
	GetWorldTimerManager().ClearTimer(SettleTimerHandle);
	LastSettleKind = INDEX_NONE;
	GetWorldTimerManager().ClearTimer(NightWakeTorchTimer);
	GetWorldTimerManager().ClearTimer(NightOneDragTimer);
	GetWorldTimerManager().ClearTimer(NightOneDragFadeTimer);
	GetWorldTimerManager().ClearTimer(Unit401RadioFadeTimer);
	for (FTimerHandle* NightOneTimer : {
		&NightOneCeilingKnockTimer, &NightOneHoldTimer, &NightOneTorchTimer,
		&NightOneThoughtTimer, &NightOneBallastTimer, &NightOneRealizationTimer,
		&NightOneDawnTimer})
	{
		GetWorldTimerManager().ClearTimer(*NightOneTimer);
	}
	bNightOneDawnHeldByNote = false;
	// 밤2~4를 여는 소리, 공동현관 뒤의 폰, 황순금의 기척과 신고 뒤의 대화.
	for (FTimerHandle* OpeningTimer : {
		&NightSettleTimer, &NightTwoDoorHoldTimer, &NightOpeningFollowTimer,
		&NightOpeningThoughtTimer, &NoSignalTimer, &HwangPeekTimer,
		&HwangPeekSceneTimer, &HwangPermissionTimer})
	{
		GetWorldTimerManager().ClearTimer(*OpeningTimer);
	}
	NightOpeningStep = 0;
	bHwangPeekLineShown = false;
	bResumingSealedHour = false;
	GetWorldTimerManager().ClearTimer(NightFourWallCoverTimer);
	for (UAudioComponent* Bed : NightAmbienceBeds)
	{
		if (IsValid(Bed))
		{
			Bed->Stop();
			Bed->DestroyComponent();
		}
	}
	NightAmbienceBeds.Reset();
	// 소리도 같이 걷는다. 남겨 두면 다음 시도에서 같은 자리에 하나 더 얹혀
	// 마스킹은 그대로인데 소리만 두 배가 된다. 401호 라디오도 이름 붙인 루프다.
	for (TObjectPtr<UAudioComponent>* Loop : {&FridgeHumLoop, &BoilerHumLoop, &Unit401PrayerLoop})
	{
		if (*Loop)
		{
			(*Loop)->Stop();
			(*Loop)->DestroyComponent();
			*Loop = nullptr;
		}
	}
}

bool AIGListenerGreyboxDirector::SetupStage()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 이 함수는 중간 어디서든 false로 빠진다. 빠진 자리까지 세운 것을
	// 남겨 두면 다음 시도가 같은 이름으로 스폰하다 실패한다.
	DestroyPartialStage();

	if (!WorldScene.IsValid())
	{
		for (TActorIterator<AIGPrologueWorldScene> It(World); It; ++It)
		{
			WorldScene = *It;
			break;
		}
	}
	if (!Player.IsValid())
	{
		for (TActorIterator<AIGPlayerCharacter> It(World); It; ++It)
		{
			Player = *It;
			break;
		}
	}
	const AIGPrologueWorldScene* Scene = WorldScene.Get();
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!Scene || !PlayerCharacter
		|| Scene->GetCorridorFixtureCount() < 2)
	{
		return false;
	}

	NoiseSubsystem = World->GetSubsystem<UIGNoiseSubsystem>();
	if (!NoiseSubsystem)
	{
		return false;
	}

	// Patrol stops ride the corridor light fixtures, west to east: the
	// authored corridor is the route, no coordinates duplicated here.
	const float WalkZ =
		IGListenerGreybox::FourthFloorZ + IGListenerGreybox::EntityHalfHeight + 2.0f;
	TArray<FVector> PatrolPoints;
	for (int32 Index = 0; Index < Scene->GetCorridorFixtureCount(); ++Index)
	{
		const FVector Fixture = Scene->GetCorridorFixtureLocation(Index);
		PatrolPoints.Add(FVector(Fixture.X, Fixture.Y, WalkZ));
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.Name = TEXT("ListenerEntityGreybox");
	Entity = World->SpawnActor<AIGListenerEntity>(
		AIGListenerEntity::StaticClass(),
		FTransform(FRotator::ZeroRotator, PatrolPoints[0]),
		SpawnParameters);
	if (!Entity)
	{
		return false;
	}
	Entity->SetPatrolPoints(PatrolPoints);
	// §4.5 닫힌 403호 앞에서 두드리고, 기다렸다가, 떠난다. 집 안의 Y 하한은 문면
	// 안쪽이라 문 바로 앞 복도에 선 그녀는 집 안으로 치지 않는다.
	Entity->SetHomeDoor(
		Scene->GetHomeDoor(),
		FBox(
			FVector(-190.0f, AIGPrologueWorldScene::HomeDoorY + 10.0f,
				AIGPrologueWorldScene::FourthFloorZ - 20.0f),
			FVector(190.0f, 235.0f, AIGPrologueWorldScene::FourthFloorZ + 230.0f)));
	// 5층으로 오르는 계단 입구 바로 앞 복도. 그는 층을 오르지 못해 위층 소리에는
	// 여기까지 와서 위를 향해 두드린다(§8 3-3, 3-4).
	Entity->SetStairFoot(FVector(-277.5f, -290.0f, WalkZ));

	FActorSpawnParameters LoopParameters;
	LoopParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	LoopParameters.Name = TEXT("NightLoopDirectorGreybox");
	NightLoop = World->SpawnActor<AIGNightLoopDirector>(
		AIGNightLoopDirector::StaticClass(),
		FTransform::Identity,
		LoopParameters);
	if (!NightLoop)
	{
		return false;
	}
	NightLoop->RegisterEntity(Entity);
	// The half-past-four bed is wherever the player actually woke.
	NightLoop->SetWakeTransform(PlayerCharacter->GetActorTransform());
	if (bMercyNoteProbeRequested)
	{
		NightLoop->PrimeMercyNoteCaptureProbe();
	}
	ExpectedWakeLocation = PlayerCharacter->GetActorLocation();

	// 자리를 먼저 잡아 두고 마스킹과 소리가 같은 값을 읽는다. 각자 계산하면
	// 한쪽만 옮겨도 조용히 어긋난다.
	const FVector FridgeHumLocation =
		Scene->GetFridgeLocation()
			+ FVector(0.0f, 0.0f, IGListenerGreybox::FridgeHumHeightOffset);
	FridgeHumHandle = NoiseSubsystem->RegisterHumSource(
		FridgeHumLocation,
		IGListenerGreybox::FridgeHumRadius,
		IGListenerGreybox::FridgeHumMasking);
	FridgeHumLoop = IGAudio::SpawnHumLoopAt(
		this,
		TEXT("GreyboxFridgeHum"),
		FridgeHumLocation,
		IGListenerGreybox::FridgeHumRadius);

	const FVector BoilerHumLocation =
		AIGPrologueWorldScene::GetBoilerCupboardLocation();
	BoilerHumHandle = NoiseSubsystem->RegisterHumSource(
		BoilerHumLocation,
		IGListenerGreybox::BoilerHumRadius,
		IGListenerGreybox::BoilerHumMasking);
	BoilerHumLoop = IGAudio::SpawnHumLoopAt(
		this,
		TEXT("GreyboxBoilerHum"),
		BoilerHumLocation,
		IGListenerGreybox::BoilerHumRadius);

	// 험 반경 2m 밖의 복도는 통째로 무음이었다. 복도·계단실·5층에 베드를 깔고
	// 건물이 밤 사이 한 번씩 소리를 내게 한다.
	SpawnNightAmbienceBeds();
	ScheduleNextSettle();

	// A resumed session hands the pursuer back at the impatience it had earned.
	if (const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Entity->SetAggressionTier(Narrative->GetAggressionTier());
	}

	// The hour itself. Capture and demo tours must stay finite walks, so they
	// never get sealed in — the same exemption SpawnReturnBoundary already
	// makes for the CH01 return boundary.
	const bool bCaptureTour =
		FParse::Param(FCommandLine::Get(), TEXT("IGCapture"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGDemo"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGDemoFrames"));
	if (!bCaptureTour)
	{
		FActorSpawnParameters PhaseParameters;
		PhaseParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PhaseParameters.Name = TEXT("NightPhaseDirectorGreybox");
		NightPhase = World->SpawnActor<AIGNightPhaseDirector>(
			AIGNightPhaseDirector::StaticClass(),
			FTransform::Identity,
			PhaseParameters);
		if (!NightPhase)
		{
			return false;
		}
		NightPhase->Configure(const_cast<AIGPrologueWorldScene*>(Scene), PlayerCharacter);
		// Bind the hour boundary BEFORE the first seal so the initial
		// broadcast reaches every listener this director wires up.
		NightPhase->OnHourActiveChanged.AddUObject(
			this, &AIGListenerGreyboxDirector::HandleHourActiveChanged);
		// 그 시간의 공동현관을 밀면 폰이 신호를 못 잡는다(§1 규칙 1). 문은 씬의
		// 것이라 셋업을 다시 돌 때마다 묶음이 쌓이지 않게 먼저 푼다.
		if (AIGSwingDoor* Entrance = Scene->GetBuildingDoor())
		{
			Entrance->OnLockedAttempt.RemoveAll(this);
			Entrance->OnLockedAttempt.AddUObject(
				this, &AIGListenerGreyboxDirector::HandleSealedEntranceTried);
		}
	}

	// P1 lives in the lobby, which the scene has finished building by now.
	FActorSpawnParameters PuzzleParameters;
	PuzzleParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PuzzleParameters.Name = TEXT("MissingFloorPuzzleOneDirector");
	PuzzleOne = World->SpawnActor<AIGMissingFloorPuzzleOneDirector>(
		AIGMissingFloorPuzzleOneDirector::StaticClass(),
		FTransform::Identity,
		PuzzleParameters);
	if (!PuzzleOne
		|| !PuzzleOne->Configure(const_cast<AIGPrologueWorldScene*>(Scene)))
	{
		return false;
	}

	// 밤1 scripted beats: the stair-landing sighting and the extinguisher
	// tutorial. Handed the corridor route so the cameo can put the entity
	// back exactly where the night stage runs it.
	FActorSpawnParameters BeatParameters;
	BeatParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	BeatParameters.Name = TEXT("NightOneBeatDirector");
	NightOneBeats = World->SpawnActor<AIGNightOneBeatDirector>(
		AIGNightOneBeatDirector::StaticClass(),
		FTransform::Identity,
		BeatParameters);
	if (!NightOneBeats
		|| !NightOneBeats->Configure(
			const_cast<AIGPrologueWorldScene*>(Scene),
			Entity,
			PlayerCharacter,
			PatrolPoints))
	{
		return false;
	}

	// 밤2 비트 2-1: the knock at 403's own front door. Handed the same corridor
	// route, because the figure at the peephole is the real entity on loan and
	// has to go back to its patrol when the beat lets go of it.
	FActorSpawnParameters NightTwoBeatParameters;
	NightTwoBeatParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	NightTwoBeatParameters.Name = TEXT("MissingFloorNightTwoBeatDirector");
	NightTwoBeats = World->SpawnActor<AIGMissingFloorNightTwoBeatDirector>(
		AIGMissingFloorNightTwoBeatDirector::StaticClass(),
		FTransform::Identity,
		NightTwoBeatParameters);
	if (!NightTwoBeats
		|| !NightTwoBeats->Configure(
			const_cast<AIGPrologueWorldScene*>(Scene),
			Entity,
			PlayerCharacter,
			PatrolPoints))
	{
		return false;
	}

	// 밤2: the management booth and P2. Spawned for every night so free
	// exploration is never fenced off; the narrative, not the walls, decides
	// which night the evidence matters.
	FActorSpawnParameters PuzzleTwoParameters;
	PuzzleTwoParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PuzzleTwoParameters.Name = TEXT("MissingFloorPuzzleTwoDirector");
	PuzzleTwo = World->SpawnActor<AIGMissingFloorPuzzleTwoDirector>(
		AIGMissingFloorPuzzleTwoDirector::StaticClass(),
		FTransform::Identity,
		PuzzleTwoParameters);
	if (!PuzzleTwo
		|| !PuzzleTwo->Configure(const_cast<AIGPrologueWorldScene*>(Scene)))
	{
		return false;
	}

	// 밤3: the gate, the annex, and the two puzzles that end in an answer.
	FActorSpawnParameters NightThreeParameters;
	NightThreeParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	NightThreeParameters.Name = TEXT("MissingFloorNightThreeDirector");
	NightThree = World->SpawnActor<AIGMissingFloorNightThreeDirector>(
		AIGMissingFloorNightThreeDirector::StaticClass(),
		FTransform::Identity,
		NightThreeParameters);
	if (!NightThree
		|| !NightThree->Configure(
			const_cast<AIGPrologueWorldScene*>(Scene),
			Entity,
			PlayerCharacter,
			PatrolPoints))
	{
		return false;
	}

	// §20.3's two automatic safety nets. Spawned after night three so it can be
	// handed the one puzzle whose key wall the entity can be seen listening at.
	FActorSpawnParameters MercyParameters;
	MercyParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	MercyParameters.Name = TEXT("MissingFloorMercyDirector");
	Mercy = World->SpawnActor<AIGMissingFloorMercyDirector>(
		AIGMissingFloorMercyDirector::StaticClass(),
		FTransform::Identity,
		MercyParameters);
	if (!Mercy)
	{
		return false;
	}
	Mercy->Configure(Entity, NightThree);

	FActorSpawnParameters FifthDawnParameters;
	FifthDawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FifthDawnParameters.Name = TEXT("MissingFloorFifthDawnDirector");
	FifthDawn = World->SpawnActor<AIGMissingFloorFifthDawnDirector>(
		AIGMissingFloorFifthDawnDirector::StaticClass(),
		FTransform::Identity,
		FifthDawnParameters);
	if (!FifthDawn || !FifthDawn->ValidateTimeline())
	{
		return false;
	}

	FActorSpawnParameters EpilogueParameters;
	EpilogueParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	EpilogueParameters.Name = TEXT("MissingFloorEpilogueDirector");
	Epilogue = World->SpawnActor<AIGMissingFloorEpilogueDirector>(
		AIGMissingFloorEpilogueDirector::StaticClass(),
		FTransform::Identity,
		EpilogueParameters);
	if (!Epilogue || !AIGMissingFloorEpilogueDirector::ValidateTimelines())
	{
		return false;
	}
	Epilogue->OnCompleted.AddUObject(
		this, &AIGListenerGreyboxDirector::HandleEpilogueCompleted);

	// Night 4: the persisted three-control cleaning circuit, five physical
	// wall strikes and the two spatial mourning choices.
	FActorSpawnParameters NightFourParameters;
	NightFourParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	NightFourParameters.Name = TEXT("MissingFloorNightFourDirector");
	NightFour = World->SpawnActor<AIGMissingFloorNightFourDirector>(
		AIGMissingFloorNightFourDirector::StaticClass(),
		FTransform::Identity,
		NightFourParameters);
	if (!NightFour
		|| !NightFour->Configure(const_cast<AIGPrologueWorldScene*>(Scene)))
	{
		return false;
	}

	// 어둑시니와 문 밖의 손님. 어느 것도 진행을 잠그지 않으므로 생성에 실패해도
	// 스테이지는 유효하다. 시간이 흐르지 않는 둘러보기(캡처 투어)에서는 깨지 않는다.
	FActorSpawnParameters ThreatParameters;
	ThreatParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ThreatParameters.Name = TEXT("MissingFloorNightThreatDirector");
	NightThreats = World->SpawnActor<AIGNightThreatDirector>(
		AIGNightThreatDirector::StaticClass(),
		FTransform::Identity,
		ThreatParameters);
	if (NightThreats)
	{
		NightThreats->Configure(
			const_cast<AIGPrologueWorldScene*>(Scene),
			PlayerCharacter,
			Entity,
			NightLoop,
			NightPhase);
	}

	// Night goals: each puzzle announces itself once; the hour decides
	// whether that ends the night.
	PuzzleOne->OnSolved.AddUObject(
		this, &AIGListenerGreyboxDirector::HandleNightOneSolved);
	PuzzleTwo->OnSolved.AddUObject(
		this, &AIGListenerGreyboxDirector::HandleNightTwoSolved);
	NightTwoBeats->OnReturnedHome.AddUObject(
		this, &AIGListenerGreyboxDirector::HandleNightTwoReturnedHome);
	NightThree->OnSolved.AddUObject(
		this, &AIGListenerGreyboxDirector::HandleNightThreeSolved);
	NightThree->OnReturnedHome.AddUObject(
		this, &AIGListenerGreyboxDirector::HandleNightThreeReturnedHome);
	FifthDawn->OnCompleted.AddUObject(
		this, &AIGListenerGreyboxDirector::HandleFifthDawnCompleted);
	NightFour->SetFifthDawn(FifthDawn);
	NightFour->OnResolved.AddUObject(
		this, &AIGListenerGreyboxDirector::HandleNightFourResolved);
	if (AIGMissingFloorEvidence* Eviction = NightFour->GetEvictionNotice())
	{
		// 요구서를 읽으면 신고 문자 줄기가 이어진다(사진 문자와 마지막 한 줄).
		Eviction->OnExamined.AddUObject(
			this, &AIGListenerGreyboxDirector::HandleEvictionNoticeRead);
	}

	// Day verbs. The bed advances the cycle; 401's door answers it.
	UStaticMesh* CubeMesh =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh)
	{
		FActorSpawnParameters DayParameters;
		DayParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		DayParameters.Name = TEXT("MissingFloorSleepTarget");
		SleepTarget = World->SpawnActor<AIGMissingFloorEvidence>(
			AIGMissingFloorEvidence::StaticClass(),
			FTransform(
				FRotator::ZeroRotator,
				FVector(-140.0f, 98.0f, 956.0f)),
			DayParameters);
		if (SleepTarget)
		{
			SleepTarget->Configure(
				CubeMesh,
				nullptr,
				FVector(88.0f, 180.0f, 18.0f),
				NSLOCTEXT("IGMissingFloor", "SleepPrompt", "침대에 눕기"),
				FText::GetEmpty(),
				EIGMissingFloorTruth::None,
				EIGMissingFloorSource::None,
				1.2f,
				0.05f,
			/*bPresentationVisible=*/false);
			SleepTarget->OnExamined.AddUObject(
				this, &AIGListenerGreyboxDirector::HandleSleepRequested);
		}

		// 노크 판정은 손잡이에서 가슴 높이까지(Z 975..1035)만 받는다. 그 위
		// 귀 높이는 라디오 엿듣기 판정이 같은 면에 따로 있다. 둘이 같은 두께에
		// 높이만 나뉘어 있어서 어느 쪽도 다른 쪽을 가리지 못한다. 가로는 문짝
		// 가운데에서 손잡이(X -118) 쪽으로 넓힌다(X -162..-112). 왼쪽
		// X -182..-162는 나중에 문에 걸리는 황순금 일지 자리라 비워 둔다.
		DayParameters.Name = TEXT("MissingFloorUnit401Door");
		Unit401Door = World->SpawnActor<AIGMissingFloorEvidence>(
			AIGMissingFloorEvidence::StaticClass(),
			FTransform(
				FRotator::ZeroRotator,
				FVector(-137.0f, -237.0f, 1005.0f)),
			DayParameters);
		if (Unit401Door)
		{
			Unit401Door->Configure(
				CubeMesh,
				nullptr,
				FVector(50.0f, 3.0f, 60.0f),
				NSLOCTEXT("IGMissingFloor", "Unit401Prompt", "401호 문 두드리기"),
				FText::GetEmpty(),
				EIGMissingFloorTruth::None,
				EIGMissingFloorSource::None,
				0.0f,
				0.15f,
				/*bPresentationVisible=*/false);
			Unit401Door->OnExamined.AddUObject(
				this, &AIGListenerGreyboxDirector::HandleUnit401Knocked);
		}

		SpawnOptionalWitnesses(CubeMesh);

	// §13 13행. 낮에 폰으로 보는 글이라 종이가 아니라 폰 화면으로 띄운다.
	// 진실 표에 들어가지 않는다 — 이건 출처가 아니라 심기다.
	{
		FActorSpawnParameters ListingParameters;
		ListingParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ListingParameters.Name = TEXT("MissingFloorUsedListingNote");
		UsedListingNote = World->SpawnActor<AIGReadableNote>(
			AIGReadableNote::StaticClass(),
			FTransform(
				FRotator(0.0f, -12.0f, 0.0f),
				FVector(-64.0f, -180.0f, 974.8f)),
			ListingParameters);
		if (UsedListingNote)
		{
			UMaterialInterface* PhoneMaterial = LoadObject<UMaterialInterface>(
				nullptr,
				TEXT("/Game/Prototype/Materials/M_PlasticDark.M_PlasticDark"));
			UsedListingNote->ConfigurePrototypeVisuals(
				CubeMesh, PhoneMaterial, FVector(7.0f, 14.0f, 1.6f));
			UsedListingNote->SetPhoneNotificationPresentation();
			UsedListingNote->SetInteractionPrompt(
				NSLOCTEXT("IGMissingFloor", "UsedListingPrompt", "휴대폰 보기"));
			UsedListingNote->SetNoteText(
				NSLOCTEXT("IGMissingFloor", "UsedListingTitle", "달빛  ·  동네 중고 거래"),
				{
					NSLOCTEXT("IGMissingFloor", "UsedListing1", "피아노 조율 공구 일괄 (튜닝해머 외 11점)"),
					NSLOCTEXT("IGMissingFloor", "UsedListing2", "무영동  ·  직거래만  ·  30,000원"),
					FText::GetEmpty(),
					NSLOCTEXT("IGMissingFloor", "UsedListing3", "세입자가 두고 간 짐 정리합니다. 상태 좋아요."),
					FText::GetEmpty(),
					NSLOCTEXT("IGMissingFloor", "UsedListing4", "작년 8월 12일  ·  거래 완료"),
				});
			UsedListingNote->OnReadStateChanged.AddDynamic(
				this, &AIGListenerGreyboxDirector::HandleUsedListingRead);
			// 그녀의 폰은 한 대다. 밤2에 녹음하러 현관 바닥에 내려놓았거나 아침에
			// 들고 재생하는 동안은 탁자에 없다. 다 들으면 탁자로 돌아온다. 그 시간
			// 동안은 시간 경계(HandleHourActiveChanged)가 다시 본다.
			if (PuzzleTwo)
			{
				PuzzleTwo->OnPhoneInUseChanged.AddUObject(
					this, &AIGListenerGreyboxDirector::RefreshTablePhone);
			}
			RefreshTablePhone();
		}
	}

	if (bProductionMode)
		{
			SpawnArrivalInteractables(CubeMesh);
		}
	}

	// The initial hour state fired before these actors existed; apply it to
	// them now that they do. Without a night phase (capture tours) everything
	// keeps its natural default and nothing is put to sleep.
	if (NightPhase)
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (bProductionMode && Narrative)
		{
			if (Narrative->GetNightIndex() <= 0)
			{
				HandleHourActiveChanged(false);
				InitializeArrivalSequence();
			}
			else if (Narrative->IsHourSealed())
			{
				bResumingSealedHour = true;
				NightPhase->ResumeTheHour(
					Narrative->GetNightIndex(),
					Narrative->GetNightElapsedSeconds());
				bResumingSealedHour = false;
			}
			else
			{
				HandleHourActiveChanged(false);
				if (APlayerController* Controller = World->GetFirstPlayerController())
				{
					if (AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD()))
					{
						Hud->SetNightPresentation(false);
						Hud->SetObjectiveProvider(NightPhase);
					}
				}
			}
		}
		else
		{
			NightPhase->BeginTheHour(/*NightIndex=*/1);
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("MISSINGFLOOR_GREYBOX stage ready: %d patrol stops, entity at %s, "
			"hour_sealed=%d"),
		PatrolPoints.Num(),
		*PatrolPoints[0].ToCompactString(),
		Scene->IsTheHourSealed() ? 1 : 0);
	return true;
}

void AIGListenerGreyboxDirector::SpawnOptionalWitnesses(UStaticMesh* CubeMesh)
{
	UWorld* World = GetWorld();
	if (!World || !CubeMesh)
	{
		return;
	}

	UStaticMesh* BowlMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_WitnessWaterBowl.SM_WitnessWaterBowl"));
	UStaticMesh* EnvelopeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_WitnessPrescriptionEnvelope.SM_WitnessPrescriptionEnvelope"));
	UStaticMesh* ButtsMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_WitnessCigaretteButts.SM_WitnessCigaretteButts"));
	UStaticMesh* RosterMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_WitnessStoreRoster.SM_WitnessStoreRoster"));

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 뒤편 통로에서도 입주 조사를 시작할 수 있다. 안내문은 본 줄거리의 필수 조건이 아니다.
	Parameters.Name = TEXT("NeighborhoodDeliveryNote");
	NeighborhoodDeliveryNote = World->SpawnActor<AIGReadableNote>(AIGReadableNote::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(2045, -1468.5f, 151)), Parameters);
	if (NeighborhoodDeliveryNote)
	{
		NeighborhoodDeliveryNote->ConfigurePrototypeVisuals(CubeMesh,
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/M_NeighborhoodDelivery.M_NeighborhoodDelivery")),
			FVector(36, 0.5, 48));
		NeighborhoodDeliveryNote->SetInteractionPrompt(NSLOCTEXT("IGMissingFloor", "DeliveryPrompt", "택배 안내문 읽기"));
		NeighborhoodDeliveryNote->SetNoteText(NSLOCTEXT("IGMissingFloor", "DeliveryTitle", "택배 기사님께"), {
			NSLOCTEXT("IGMissingFloor", "Delivery1", "달빛빌라 옥탑"),
			NSLOCTEXT("IGMissingFloor", "Delivery2", "집에 없으면 맞은편 새벽24 편의점에 맡겨 주세요."),
			NSLOCTEXT("IGMissingFloor", "Delivery3", "맡기신 뒤 받는 분께 연락 부탁드립니다."),
			NSLOCTEXT("IGMissingFloor", "Delivery4", "달빛빌라 관리실  목한수") });
		NeighborhoodDeliveryNote->OnReadStateChanged.AddDynamic(this, &AIGListenerGreyboxDirector::HandleNeighborhoodDeliveryRead);
	}

	// 401호 문선과 계단 사이의 빈 바닥. 종이까지 포함한 실제 메시 크기로 놓는다.
	Parameters.Name = TEXT("MissingFloorWitnessWaterBowl");
	WaterBowl = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(-208.0f, -255.0f, 902.32f)),
		Parameters);
	if (WaterBowl)
	{
		WaterBowl->Configure(
			BowlMesh,
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/MI_WitnessWaterBowl.MI_WitnessWaterBowl")),
			FVector::ZeroVector,
			NSLOCTEXT("IGMissingFloor", "WitnessBowlPrompt", "물그릇"),
			NSLOCTEXT(
				"IGMissingFloor",
				"WitnessBowlThought",
				"고양이 물그릇인가? 물은 깨끗하네."),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			0.9f,
			0.04f);
		WaterBowl->OnExamined.AddUObject(
			this, &AIGListenerGreyboxDirector::HandleWaterBowlExamined);
	}

	// 골목 이쪽 보도에 떨어진 납작한 약봉투. 인쇄된 이름까지만 목격 근거가 된다.
	Parameters.Name = TEXT("MissingFloorWitnessSleepingPills");
	SleepingPills = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator(0.0f, 18.0f, 0.0f), FVector(-40.0f, -449.0f, .20f)),
		Parameters);
	if (SleepingPills)
	{
		SleepingPills->Configure(
			EnvelopeMesh,
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/MI_WitnessPrescriptionEnvelope.MI_WitnessPrescriptionEnvelope")),
			FVector::ZeroVector,
			NSLOCTEXT("IGMissingFloor", "WitnessPillsPrompt", "떨어진 약봉투"),
			NSLOCTEXT(
				"IGMissingFloor",
				"WitnessPillsThought",
				"‘서일영 님’이라고 적혀 있다."),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			0.9f,
			0.05f);
		SleepingPills->OnExamined.AddUObject(
			this, &AIGListenerGreyboxDirector::HandleSleepingPillsExamined);
	}

	// 옥상 탱크 기초 위. 기초는 (0,-25) 중심에 360×360×100이라 윗면이
	// Z=1300이고 X는 -180..180이다. 탱크 몸통(X -153..153)을 벗어난
	// 동쪽 턱 27 cm 위에 눕힌다 — 앉으면 딱 무릎 옆이 되는 자리다.
	Parameters.Name = TEXT("MissingFloorWitnessCigarettePack");
	CigarettePack = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator(0.0f, -34.0f, 0.0f), FVector(168.0f, -25.0f, 1300.42f)),
		Parameters);
	if (CigarettePack)
	{
		CigarettePack->Configure(
			ButtsMesh,
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/MI_WitnessCigaretteButts.MI_WitnessCigaretteButts")),
			FVector::ZeroVector,
			NSLOCTEXT("IGMissingFloor", "WitnessPackPrompt", "담배꽁초"),
			// 대치의 「거기 앉아서 쉬던 사람이에요」와 에필로그의 「오빠가 쉬던
			// 자리」가 이 한 줄에 기댄다.
			NSLOCTEXT(
				"IGMissingFloor",
				"WitnessPackThought",
				"꽁초 끝을 다 납작하게 눌러 놨네. 오빠도 이렇게 껐는데."),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			0.9f,
			0.04f);
		CigarettePack->OnExamined.AddUObject(
			this, &AIGListenerGreyboxDirector::HandleCigarettePackExamined);
	}

	// 편의점 계산대 상판. 몸통(Z 6..96) 위에 강판 상판이 Z 96..99로 얹혀
	// 있다. 금전등록기(X 2596..2644)와 서쪽 소품(X 2494..2506) 사이의 빈
	// 자리에 클립보드를 눕힌다.
	Parameters.Name = TEXT("MissingFloorWitnessStoreRoster");
	StoreRoster = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator(0.0f, -9.0f, 0.0f), FVector(2556.0f, -193.0f, 99.68f)),
		Parameters);
	if (StoreRoster)
	{
		StoreRoster->Configure(
			RosterMesh,
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/MI_WitnessStoreRoster.MI_WitnessStoreRoster")),
			FVector::ZeroVector,
			NSLOCTEXT("IGMissingFloor", "WitnessRosterPrompt", "야간 근무표"),
			NSLOCTEXT(
				"IGMissingFloor",
				"WitnessRosterThought",
				"나린 씨는 이번 주도 평일 밤 근무네."),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			0.9f,
			0.04f);
		StoreRoster->OnExamined.AddUObject(
			this, &AIGListenerGreyboxDirector::HandleStoreRosterExamined);
	}

	// 작은 단서는 가까이서 읽는다. 먼 층까지 거리장·간접광 갱신에 넣지 않는다.
	// 얇은 봉투와 근무표는 받침 면의 그림자를 쓰고, 그릇·꽁초의 그림자는 남긴다.
	for (AIGMissingFloorEvidence* Evidence : {WaterBowl.Get(), SleepingPills.Get(), CigarettePack.Get(), StoreRoster.Get()})
	{
		if (!Evidence) continue;
		if (UStaticMeshComponent* Visual = Cast<UStaticMeshComponent>(Evidence->GetRootComponent()))
		{
			Visual->SetCullDistance(1600.0f);
			Visual->SetAffectDistanceFieldLighting(false);
			Visual->SetAffectDynamicIndirectLighting(false);
			if (Evidence == SleepingPills || Evidence == StoreRoster) Visual->SetCastShadow(false);
		}
	}

	// 401호 문 너머. 부피만 세우고 그림은 두지 않는다 — 여기 있는 것은
	// 물건이 아니라 소리다. 노크 판정 위, 귀를 댈 높이에만 문과 같은 면에
	// 둔다(Z 1041..1063, 402호 듣기 판정과 같은 높이). 예전처럼 문 앞 복도
	// 쪽에 크게 세우면 서서 문을 어디로 겨눠도 이 판정이 먼저 맞아서 노크를
	// 할 수 없었다. 위를 더 올리지 않는다 — 캡처 9가 호수판을 보려고 위로
	// 11도 드는데, 그 시선이 훑는 구체에 걸려 엿듣기 프롬프트가 찍힌다.
	Parameters.Name = TEXT("MissingFloorWitnessUnit401Radio");
	Unit401Radio = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(-150.0f, -237.0f, 1052.0f)),
		Parameters);
	if (Unit401Radio)
	{
		Unit401Radio->Configure(
			CubeMesh,
			nullptr,
			FVector(30.0f, 3.0f, 22.0f),
			NSLOCTEXT(
				"IGMissingFloor", "WitnessRadioPrompt", "401호 문에 귀 대기"),
			FText::GetEmpty(),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			1.0f,
			0.03f,
			/*bPresentationVisible=*/false);
		Unit401Radio->Tags.AddUnique(FName(TEXT("MissingFloor.Verb.Listen")));
		Unit401Radio->OnExamined.AddUObject(
			this, &AIGListenerGreyboxDirector::HandleUnit401RadioExamined);
	}

	// 그 문 너머의 소리(§8 0-4). 귀를 대는 판정은 위의 부피가 받고 여기서는
	// 소리만 세운다. 문 앞을 지나면 낮게 들리고, 밤에는 꺼져 있다가 새벽에
	// 돌아온다 — HandleHourActiveChanged가 켜고 끈다. 막간의 예불과 같은
	// 파형이라, 벽 속의 새벽에 들리는 소리를 이 문 앞에서 먼저 듣는다.
	Unit401PrayerLoop = NewObject<UAudioComponent>(this, TEXT("Unit401PrayerLoop"));
	if (Unit401PrayerLoop)
	{
		Unit401PrayerLoop->bAutoActivate = false;
		Unit401PrayerLoop->bAutoDestroy = false;
		Unit401PrayerLoop->RegisterComponent();
		Unit401PrayerLoop->SetWorldLocation(IGListenerGreybox::Unit401PrayerLocation);
		Unit401PrayerLoop->SetSound(UIGToneSequenceSoundWave::CreateMuffledPrayerRadio(this));
		// 배수는 0이 아닌 값에 두고 페이더로만 움직인다. 배수가 0이면
		// AdjustVolume으로는 영영 들리지 않는다.
		Unit401PrayerLoop->SetVolumeMultiplier(IGListenerGreybox::Unit401PrayerVolume);
		Unit401PrayerLoop->AttenuationSettings = IGAudio::MakeAttenuation(
			this, 40.0f, 200.0f, EIGAudioBus::World);
		Unit401PrayerLoop->bAllowSpatialization = true;
		// 복도에 선 사람과 소리 사이에는 벽이 없어 차폐가 걸리지 않는다.
		// 철문은 필터로 씌운다.
		Unit401PrayerLoop->SetLowPassFilterEnabled(true);
		Unit401PrayerLoop->SetLowPassFilterFrequency(
			IGListenerGreybox::Unit401PrayerDoorLowPassHz);
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->RegisterPersistentBed(Unit401PrayerLoop, EIGAudioBus::World);
		}
	}

	// 메모보다 위쪽, 귀를 댈 높이에만 듣기 판정을 둔다. 문 앞을 크게 막으면
	// 보이는 종이를 겨눠도 보이지 않는 듣기 영역이 먼저 잡힌다.
	Unit402Listen = SpawnListeningVolume(
		CubeMesh,
		TEXT("MissingFloorWitnessUnit402Listen"),
		FVector(-30.0f, -237.15f, 1052.0f),
		FVector(38.0f, 0.2f, 30.0f),
		NSLOCTEXT(
			"IGMissingFloor", "WitnessUnit402Prompt", "402호 문에 귀 대기"));
	if (Unit402Listen)
	{
		Unit402Listen->OnExamined.AddUObject(
			this, &AIGListenerGreyboxDirector::HandleUnit402ListenExamined);
	}

	// 옥상 철문 안쪽. 계단탑 문(힌지 -320, 220)을 지나 옥상으로 나선 자리다.
	RoofDoorListen = SpawnListeningVolume(
		CubeMesh,
		TEXT("MissingFloorWitnessRoofDoorListen"),
		FVector(-300.0f, 250.0f, 1290.0f),
		FVector(40.0f, 40.0f, 60.0f),
		NSLOCTEXT(
			"IGMissingFloor", "WitnessRoofWindPrompt", "옥상 문에 귀 대기"));
	if (RoofDoorListen)
	{
		RoofDoorListen->OnExamined.AddUObject(
			this, &AIGListenerGreyboxDirector::HandleRoofDoorListenExamined);
	}
}

AIGMissingFloorEvidence* AIGListenerGreyboxDirector::SpawnListeningVolume(
	UStaticMesh* CubeMesh,
	const TCHAR* ActorName,
	const FVector& Location,
	const FVector& Extent,
	const FText& Prompt)
{
	UWorld* World = GetWorld();
	if (!World || !CubeMesh)
	{
		return nullptr;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.Name = ActorName;
	AIGMissingFloorEvidence* Volume = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, Location),
		Parameters);
	if (!Volume)
	{
		return nullptr;
	}
	// 소음 0.03은 귀를 대는 값이다(§5.1의 엿듣기). 그림은 두지 않는다 —
	// 여기 있는 것은 물건이 아니라 소리다.
	Volume->Configure(
		CubeMesh,
		nullptr,
		Extent,
		Prompt,
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		1.0f,
		0.03f,
		/*bPresentationVisible=*/false);
	Volume->Tags.AddUnique(FName(TEXT("MissingFloor.Verb.Listen")));
	return Volume;
}

void AIGListenerGreyboxDirector::HandleStoreRosterExamined(
	AIGMissingFloorEvidence* Evidence)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RecordWitness(EIGMissingFloorWitness::StoreNightRoster);
	}
}

void AIGListenerGreyboxDirector::HandleUnit402ListenExamined(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	const bool bFirstHeard =
		Narrative->RecordWitness(EIGMissingFloorWitness::Unit402Silence);
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateVacantUnitTone(this),
		Evidence ? Evidence->GetActorLocation() : GetActorLocation(),
		0.58f,
		1.0f,
		90.0f,
		380.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushAudioCaption(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"WitnessUnit402Caption",
			"[안에서 아무 소리도 안 난다]"),
		2.8f);
	if (!bFirstHeard)
	{
		return;
	}
	// 험이 없다는 것은 헤드폰이 아니면 알아채기 어렵다. 처음 들었을 때 한 줄로
	// 짚는다. 메모를 읽었으면 메모와 한데 묶고, 메모 쪽이 먼저 되받았으면 소리만
	// 짚는다. 둘 다 아니면 메모를 읽을 때로 미룬다. 추론은 말하지 않는다 —
	// 밤1 이 문 안의 노크와 에필로그의 냉장고 문장이 나머지를 한다.
	FText Line;
	if (Narrative->HasBeatPlayed(FName(TEXT("Arrival.Unit402")))
		&& Narrative->MarkBeatPlayed(FName(TEXT("Witness.Unit402Vacancy"))))
	{
		Line = IGListenerGreybox::Unit402VacancyHeardThought();
	}
	else if (Narrative->HasBeatPlayed(FName(TEXT("Witness.Unit402Vacancy"))))
	{
		Line = NSLOCTEXT(
			"IGMissingFloor",
			"WitnessUnit402Thought",
			"냉장고 소리도 안 나네.");
	}
	if (Line.IsEmpty())
	{
		return;
	}
	if (IsScriptedRun())
	{
		AIGHorrorHUD::PushThought(this, Line, 3.4f);
		return;
	}
	// 톤(3.2초)이 거의 다 흐른 뒤에 말한다. 듣는 동안 글이 먼저 답하지 않게.
	GetWorldTimerManager().SetTimer(
		Unit402VacancyTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, Line]()
		{
			AIGHorrorHUD::PushThought(this, Line, 3.4f);
		}),
		2.9f,
		false);
}

void AIGListenerGreyboxDirector::HandleRoofDoorListenExamined(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	Narrative->RecordWitness(EIGMissingFloorWitness::RoofDoorWind);
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateRoofDoorGust(this),
		Evidence ? Evidence->GetActorLocation() : GetActorLocation(),
		0.66f,
		1.0f,
		150.0f,
		900.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushAudioCaption(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"WitnessRoofWindCaption",
			"[바깥] 바람이 세졌다 약해지는 소리"),
		3.0f);
	// 짚는 것은 박자다. 밤4 대치의 「바람이 둘, 쉬고, 하나로 불어요?」가 이
	// 한 줄을 되받는다. 천장에서 두드리는 소리를 아직 못 들은 저녁에는 앞
	// 문장만 한다.
	AIGHorrorHUD::PushThought(
		this,
		Narrative->GetNightIndex() >= 1
			? NSLOCTEXT(
				"IGMissingFloor",
				"WitnessRoofWindThought",
				"바람 소리야. 천장에서 들은 건 분명히 두드리는 소리였는데.")
			: NSLOCTEXT(
				"IGMissingFloor",
				"WitnessRoofWindThoughtFirstEvening",
				"그냥 바람 소리네."),
		4.6f);
}

void AIGListenerGreyboxDirector::HandleUnit401RadioExamined(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	Narrative->RecordWitness(EIGMissingFloorWitness::Unit401DoorRadio);

	// 문 너머의 소리는 이미 §10.3에 있다. 말은 끝까지 알아들을 수 없고 가락만
	// 염불처럼 오르내린다. 문 앞의 라디오가 귀를 댄 동안 커졌다가 제 크기로
	// 돌아간다. 밤이면 그 시간의 새벽 예불이고, 귀를 떼면 다시 들리지 않는다.
	// 새로 틀지 않으니 여러 번 귀를 대도 루프가 쌓이지 않는다.
	GetWorldTimerManager().ClearTimer(Unit401PrayerReturnTimer);
	FadeUnit401Prayer(1.0f, 0.6f);
	GetWorldTimerManager().SetTimer(
		Unit401RadioFadeTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			FadeUnit401Prayer(GetUnit401PrayerRestLevel(), 2.5f);
		}),
		5.2f,
		false);
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"WitnessRadioThought",
			"안에서 라디오 소리가 난다. 불경 방송인가?"),
		4.2f);
}

void AIGListenerGreyboxDirector::HandleWaterBowlExamined(
	AIGMissingFloorEvidence* Evidence)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RecordWitness(EIGMissingFloorWitness::HwangWaterBowl);
	}
}

void AIGListenerGreyboxDirector::HandleSleepingPillsExamined(
	AIGMissingFloorEvidence* Evidence)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RecordWitness(EIGMissingFloorWitness::SeoSleepingPills);
	}
}

void AIGListenerGreyboxDirector::HandleCigarettePackExamined(
	AIGMissingFloorEvidence* Evidence)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RecordWitness(EIGMissingFloorWitness::RooftopCigarettePack);
	}
}

void AIGListenerGreyboxDirector::SpawnArrivalInteractables(UStaticMesh* CubeMesh)
{
	UWorld* World = GetWorld();
	if (!World || !CubeMesh)
	{
		return;
	}

	UMaterialInterface* Paper = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Prototype/Materials/M_PaperClean.M_PaperClean"));
	UMaterialInterface* Contract = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Prototype/Materials/M_ArrivalContract.M_ArrivalContract"));
	UMaterialInterface* Cardboard = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Prototype/Materials/M_MovingBoxCardboardUV.M_MovingBoxCardboardUV"));
	UMaterialInterface* Metal = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Prototype/Materials/M_StainlessUV.M_StainlessUV"));
	UStaticMesh* MovingBoxMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/Photo/Props/cardboard_box_01/cardboard_box_01_1k/StaticMeshes/cardboard_box_01_1k.cardboard_box_01_1k"),
		nullptr,
		LOAD_NoWarn);

	auto SpawnEvidence = [this, World, CubeMesh](
		const TCHAR* Name,
		const FVector& Location,
		const FVector& Size,
		UStaticMesh* PresentationMesh,
		UMaterialInterface* Material,
		const FText& Prompt,
		const FText& Thought,
		const float HoldSeconds,
		const float Loudness)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = FName(Name);
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AIGMissingFloorEvidence* Evidence =
			World->SpawnActor<AIGMissingFloorEvidence>(
				AIGMissingFloorEvidence::StaticClass(),
				FTransform(FRotator::ZeroRotator, Location),
				Parameters);
		if (Evidence)
		{
			Evidence->Configure(
				PresentationMesh ? PresentationMesh : CubeMesh,
				Material,
				Size,
				Prompt,
				Thought,
				EIGMissingFloorTruth::None,
				EIGMissingFloorSource::None,
				HoldSeconds,
				Loudness);
			Evidence->OnExamined.AddUObject(
				this,
				&AIGListenerGreyboxDirector::HandleArrivalEvidence);
		}
		return Evidence;
	};

	ArrivalContract = SpawnEvidence(
		TEXT("MissingFloorArrivalContract"),
		FVector(-85.0f, -178.0f, WorldScene->GetDeskSurfaceWorldZ() + 0.18f),
		FVector(21.0f, 29.7f, 0.12f),
		CubeMesh,
		Contract ? Contract : Paper,
		NSLOCTEXT("IGMissingFloor", "ArrivalContractPrompt", "임대차계약서 보기"),
		NSLOCTEXT(
			"IGMissingFloor",
			"ArrivalContractThought",
			"403호. 옥상은 같이 쓰는 거라고 돼 있네."),
		0.7f,
		0.02f);
	if (ArrivalContract)
	{
		// 얇은 종이는 두꺼운 물체처럼 그림자가 뜨지 않게 한다.
		ArrivalContract->GetPresentationMesh()->SetCastShadow(false);
		ArrivalContract->GetPresentationMesh()->SetAffectDistanceFieldLighting(false);
	}
	ArrivalParcelBox = SpawnEvidence(
		TEXT("MissingFloorArrivalParcelBox"),
		FVector(20.0f, 70.0f, 924.0f),
		FVector(48.0f, 38.0f, 48.0f),
		MovingBoxMesh,
		Cardboard,
		NSLOCTEXT("IGMissingFloor", "ArrivalParcelPrompt", "반송된 택배 상자 열기"),
		NSLOCTEXT(
			"IGMissingFloor",
			"ArrivalParcelThought",
			"백도하, 달빛빌라 501호… 주소를 못 찾았다고 돌아온 택배야."),
		0.9f,
		0.12f);
	ArrivalNotebookBox = SpawnEvidence(
		TEXT("MissingFloorArrivalNotebookBox"),
		FVector(82.0f, 145.0f, 918.0f),
		FVector(56.0f, 42.0f, 36.0f),
		MovingBoxMesh,
		Cardboard,
		NSLOCTEXT("IGMissingFloor", "ArrivalNotebookPrompt", "조율 공구 상자 열기"),
		NSLOCTEXT(
			"IGMissingFloor",
			"ArrivalNotebookThought",
			"공방에 남아 있던 오빠 공구다. 늘 쓰던 튜닝 해머만 없네."),
		1.0f,
		0.13f);
	ArrivalVoicemailBox = SpawnEvidence(
		TEXT("MissingFloorArrivalVoicemailBox"),
		FVector(125.0f, 62.0f, 913.0f),
		FVector(42.0f, 34.0f, 26.0f),
		MovingBoxMesh,
		Cardboard,
		NSLOCTEXT("IGMissingFloor", "ArrivalVoicemailPrompt", "예전 휴대폰 상자 열기"),
		NSLOCTEXT(
			"IGMissingFloor",
			"ArrivalVoicemailThought",
			"“유담아, 나 이사했어. 집이 좀 이상하긴 한데… 와 보면 알아. 문 두드리면 알지? 둘, 하나.” 뒤에서 뭔가 긁히는 소리가 들린다."),
		0.8f,
		0.09f);
	ArrivalStoreBell = SpawnEvidence(
		TEXT("MissingFloorArrivalStoreBell"),
		FVector(2690.0f, -216.0f, 101.55f),
		FVector(8.6f, 8.6f, 5.1f),
		LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_ServiceBell.SM_ServiceBell")),
		nullptr,
		NSLOCTEXT("IGMissingFloor", "ArrivalStoreBellPrompt", "계산대 호출벨 누르기"),
		FText::GetEmpty(),
		0.0f,
		0.10f);
	ArrivalUnit402Note = SpawnEvidence(
		TEXT("MissingFloorArrivalUnit402Note"),
		FVector(-30.0f, -237.04f, 1018.0f),
		FVector::ZeroVector,
		LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_NeighborMemo402.SM_NeighborMemo402")),
		nullptr,
		NSLOCTEXT("IGMissingFloor", "Arrival402Prompt", "402호 문에 붙은 메모 읽기"),
		NSLOCTEXT(
			"IGMissingFloor",
			"Arrival402Thought",
			"새벽마다 위에서 뭘 끄는 소리가 납니다. 혹시 같은 소리 들으시면 관리실에 말씀해 주세요. 402호"),
		0.5f,
		0.01f);
	if (ArrivalUnit402Note)
	{
		UStaticMeshComponent* NoteSurface = ArrivalUnit402Note->GetPresentationMesh();
		NoteSurface->SetCastShadow(false);
		NoteSurface->SetAffectDistanceFieldLighting(false);
		NoteSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// 얇은 종이의 볼록 충돌은 가져오는 과정에서 납작해질 수 있다.
		// 읽기는 같은 크기의 별도 상자 판정으로 받고, 플레이어 이동은 막지 않는다.
		UBoxComponent* ReadArea = NewObject<UBoxComponent>(ArrivalUnit402Note, TEXT("ReadArea"));
		ArrivalUnit402Note->AddInstanceComponent(ReadArea);
		ReadArea->SetupAttachment(NoteSurface);
		ReadArea->SetMobility(EComponentMobility::Static);
		ReadArea->SetBoxExtent(FVector(7.4f, 0.1f, 5.25f));
		ReadArea->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ReadArea->SetCollisionResponseToAllChannels(ECR_Ignore);
		ReadArea->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		ReadArea->SetGenerateOverlapEvents(false);
		ReadArea->SetCanEverAffectNavigation(false);
		ReadArea->RegisterComponent();
	}
	ArrivalRoofLock = SpawnEvidence(
		TEXT("MissingFloorArrivalRoofLock"),
		FVector(-277.5f, 213.0f, 1300.0f),
		FVector(12.0f, 4.0f, 20.0f),
		CubeMesh,
		Metal,
		NSLOCTEXT("IGMissingFloor", "ArrivalRoofLockPrompt", "옥상 문 자물쇠 보기"),
		NSLOCTEXT(
			"IGMissingFloor",
			"ArrivalRoofLockThought",
			"자물쇠가 채워져 있네. 옥상은 같이 쓴다더니."),
		0.8f,
		0.08f);
}

void AIGListenerGreyboxDirector::InitializeArrivalSequence()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	const bool bFirstEntry = Narrative->MarkBeatPlayed(FName(TEXT("Arrival.Started")));
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		PlayerCharacter->SetCameraMotionEnabled(true);
	}
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		if (AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD()))
		{
			Hud->SetNightPresentation(false);
			Hud->SetObjectiveProvider(this);
		}
	}
	if (bFirstEntry)
	{
		// 콜드 오픈(§8 0-1, §26.2). 검은 화면이 걷히면 계약서가 놓인 방이고, 위에서
		// 공구 카트 바퀴 하나가 구르다 멎는다. 제목은 그 뒤에 선다 — 카드가 먼저
		// 덮으면 소리가 카드 밑에 묻힌다. 밤3 5층에서 그 카트를 밀면 같은 바퀴가
		// 구른다. 지금은 아직 윗집 생활 소음으로 들린다.
		const FVector Above = Player.IsValid()
			? Player->GetActorLocation() + FVector(40.0f, 60.0f, 310.0f)
			: FVector(-40.0f, 60.0f, 1280.0f);
		auto PlayCasterRoll = [this, Above]()
		{
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateToolCartRoll(this),
				Above,
				0.58f,
				1.0f,
				90.0f,
				900.0f,
				EIGAudioBus::World);
			AIGHorrorHUD::PushAudioCaptionAt(
				this,
				NSLOCTEXT("IGMissingFloor", "ArrivalCasterCaption", "바퀴 구르다 멈추는 소리"),
				2.6f,
				Above);
		};
		auto ShowTitleCard = [this]()
		{
			AIGHorrorHUD::ShowChapterCard(
				this,
				NSLOCTEXT("IGMissingFloor", "ArrivalEyebrow", "입주 첫날 · 20:47"),
				NSLOCTEXT("IGMissingFloor", "ArrivalTitle", "Missing Floor"),
				NSLOCTEXT("IGMissingFloor", "ArrivalSubtitle", "오빠의 마지막 주소, 달빛빌라 501호"),
				4.6f);
		};
		if (IsScriptedRun())
		{
			// 캡처와 프로브는 예전처럼 같은 프레임에 카드를 세운다. 근접 사진이
			// 8.4초 뒤에 시작하므로 카드가 늦으면 그 사진을 덮는다.
			ShowTitleCard();
			PlayCasterRoll();
		}
		else
		{
			if (bArrivalOpeningHeldBlack)
			{
				if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
				{
					if (Controller->PlayerCameraManager)
					{
						Controller->PlayerCameraManager->StartCameraFade(
							1.0f,
							0.0f,
							IGListenerGreybox::ArrivalOpeningFadeSeconds,
							FLinearColor::Black,
							/*bShouldFadeAudio=*/false,
							/*bHoldWhenFinished=*/false);
					}
				}
				bArrivalOpeningHeldBlack = false;
			}
			GetWorldTimerManager().SetTimer(
				ArrivalColdOpenSoundTimer,
				FTimerDelegate::CreateWeakLambda(this, [PlayCasterRoll]() { PlayCasterRoll(); }),
				IGListenerGreybox::ArrivalCasterRollSeconds,
				false);
			GetWorldTimerManager().SetTimer(
				ArrivalTitleCardTimer,
				FTimerDelegate::CreateWeakLambda(this, [ShowTitleCard]() { ShowTitleCard(); }),
				IGListenerGreybox::ArrivalTitleCardSeconds,
				false);
		}
		RequestArrivalAutosave();
	}
	ReleaseArrivalOpeningBlack();
	// 첫 저녁의 생활음은 제목과 카드가 다 지나간 뒤에 시작한다.
	if (!IsScriptedRun() && !Narrative->HasBeatPlayed(FName(TEXT("Arrival.Slept"))))
	{
		ScheduleArrivalBaseline(25.0f, 35.0f);
	}
	UpdateArrivalSequence();
}

bool AIGListenerGreyboxDirector::AreArrivalBoxesOpened() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative
		&& Narrative->HasBeatPlayed(FName(TEXT("Arrival.Box.Parcel")))
		&& Narrative->HasBeatPlayed(FName(TEXT("Arrival.Box.Notebook")))
		&& Narrative->HasBeatPlayed(FName(TEXT("Arrival.Box.Voicemail")));
}

void AIGListenerGreyboxDirector::UpdateArrivalSequence()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || Narrative->GetNightIndex() != 0)
	{
		return;
	}
	const bool bContract = Narrative->HasBeatPlayed(FName(TEXT("Arrival.Contract")));
	const bool bBoxes = AreArrivalBoxesOpened();
	const bool bStore = Narrative->HasBeatPlayed(FName(TEXT("Arrival.Store")));
	const bool bUnit401 = Narrative->HasBeatPlayed(FName(TEXT("Arrival.Unit401")));
	const bool bUnit402 = Narrative->HasBeatPlayed(FName(TEXT("Arrival.Unit402")));
	const bool bRoof = Narrative->HasBeatPlayed(FName(TEXT("Arrival.RoofDoor")));
	if (ArrivalContract)
	{
		ArrivalContract->SetInteractionEnabled(true);
		if (bContract) ArrivalContract->SetInteractionPrompt(NSLOCTEXT("IGMissingFloor", "ArrivalContractAgain", "임대차계약서 다시 보기"));
	}
	if (ArrivalParcelBox)
	{
		ArrivalParcelBox->SetInteractionEnabled(true);
		if (Narrative->HasBeatPlayed(FName(TEXT("Arrival.Box.Parcel"))))
			ArrivalParcelBox->SetInteractionPrompt(NSLOCTEXT("IGMissingFloor", "ArrivalParcelAgain", "송장 주소 다시 확인하기"));
	}
	if (ArrivalNotebookBox)
	{
		ArrivalNotebookBox->SetInteractionEnabled(true);
		if (Narrative->HasBeatPlayed(FName(TEXT("Arrival.Box.Notebook"))))
			ArrivalNotebookBox->SetInteractionPrompt(NSLOCTEXT("IGMissingFloor", "ArrivalNotebookAgain", "조율 공구 다시 보기"));
	}
	if (ArrivalVoicemailBox)
	{
		ArrivalVoicemailBox->SetInteractionEnabled(true);
		if (Narrative->HasBeatPlayed(FName(TEXT("Arrival.Box.Voicemail"))))
			ArrivalVoicemailBox->SetInteractionPrompt(NSLOCTEXT("IGMissingFloor", "ArrivalVoicemailAgain", "음성 메시지 다시 보기"));
	}
	if (ArrivalStoreBell)
	{
		ArrivalStoreBell->SetInteractionEnabled(true);
	}
	if (Unit401Door)
	{
		Unit401Door->SetInteractionEnabled(!bUnit401);
	}
	if (ArrivalUnit402Note)
	{
		ArrivalUnit402Note->SetInteractionEnabled(true);
		if (bUnit402) ArrivalUnit402Note->SetInteractionPrompt(NSLOCTEXT("IGMissingFloor", "Arrival402Again", "402호 메모 다시 읽기"));
	}
	if (ArrivalRoofLock)
	{
		ArrivalRoofLock->SetInteractionEnabled(!bRoof);
	}

	if (bContract && bBoxes && bStore && bUnit401 && bUnit402 && bRoof
		&& Narrative->MarkBeatPlayed(FName(TEXT("Arrival.Complete"))))
	{
		// 밤마다 우는 04:30 알람의 이유는 여기서 한 번만 말한다. 에필로그의
		// 「네 시 반 알람은 지웠다.」가 이 줄을 되받는다.
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"ArrivalReadyForBed",
				"내일 관리인한테 물어보자. 알람은 네 시 반. 오빠가 집에 돌아오던 시간이다."),
			5.0f);
		RequestArrivalAutosave();
	}
	if (SleepTarget)
	{
		SleepTarget->SetInteractionEnabled(
			Narrative->HasBeatPlayed(FName(TEXT("Arrival.Complete"))));
	}
}

namespace IGListenerGreybox
{
	/**
	 * 나린과 인사를 나눈 뒤다. 뒤편 안내문을 먼저 읽고 와도 자기소개가 먼저다.
	 * 누구 동생인지도 모르는 사람에게 「오빠분이 부탁했다」고 말하면 안 된다.
	 * 입주가 끝나야 밤이 오므로 밤 번호가 있으면 이미 만난 것이다.
	 */
	static bool HasMetNarin(const UIGMissingFloorNarrativeSubsystem& Narrative)
	{
		return Narrative.GetNightIndex() > 0
			|| Narrative.HasBeatPlayed(FName(TEXT("Arrival.Store")));
	}

	/** 뒤편 택배 안내문을 읽었고 아직 그 얘기를 하지 않았다. */
	static bool IsNarinDeliveryTalkDue(const UIGMissingFloorNarrativeSubsystem& Narrative)
	{
		return HasMetNarin(Narrative)
			&& Narrative.HasBeatPlayed(FName(TEXT("Neighborhood.Delivery")))
			&& !Narrative.HasBeatPlayed(FName(TEXT("Neighborhood.DeliveryDiscussed")));
	}

	/**
	 * 낮3(밤 번호 2), 관리실 대장을 보고 온 다음 날에 한 번. 입주 날 잠겨 있던
	 * 옥상 문을 묻는다. 질문과 대답이 이 한 조건을 같이 봐야 짝이 맞는다.
	 */
	const FName NarinRoofBeat(TEXT("Day3.NarinRoof"));
	static bool IsNarinRoofTalkDue(const UIGMissingFloorNarrativeSubsystem& Narrative)
	{
		return Narrative.GetNightIndex() == 2
			&& Narrative.IsPuzzleSolved(FName(TEXT("P2")))
			&& !Narrative.HasBeatPlayed(NarinRoofBeat);
	}
}

void AIGListenerGreyboxDirector::HandleArrivalEvidence(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Evidence)
	{
		return;
	}
	if (Evidence == ArrivalStoreBell)
	{
		// 벨은 누를 때마다 운다. 7초 간격은 나린과 나누는 말에만 건다.
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateServiceBellDing(this),
			Evidence->GetActorLocation(),
			0.45f,
			1.0f,
			60.0f,
			900.0f,
			EIGAudioBus::World);
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now - LastCounterTalkAt < 7.0) { return; }
		LastCounterTalkAt = Now;
		const bool bDeliveryQuestion = IGListenerGreybox::IsNarinDeliveryTalkDue(*Narrative);
		const bool bRoofQuestion = !bDeliveryQuestion
			&& IGListenerGreybox::IsNarinRoofTalkDue(*Narrative);
		AIGHorrorHUD::PushDialogue(this,
			NSLOCTEXT("IGMissingFloor", "YudamCounterSpeaker", "백유담"),
			bDeliveryQuestion
				? NSLOCTEXT("IGMissingFloor", "NarinDeliveryQuestion", "뒤에 붙은 택배 안내문 보고 왔는데요. 백도하 앞으로 온 택배도 여기 맡긴 적 있어요?")
				: bRoofQuestion
				? NSLOCTEXT("IGMissingFloor", "NarinRoofQuestion", "저희 빌라 옥상 문이요, 원래 잠가 둬요? 자물쇠가 채워져 있던데.")
				: (Narrative->GetNightIndex() == 0
					? (Narrative->HasBeatPlayed(FName(TEXT("Arrival.Store")))
						? NSLOCTEXT("IGMissingFloor", "NarinRepeatQuestion", "혹시 생각나는 게 더 있으세요?")
						: NSLOCTEXT("IGMissingFloor", "NarinFirstQuestion", "달빛빌라 403호에 이사 왔어요. 백도하라는 사람 아세요? 제 오빠예요."))
					: NSLOCTEXT("IGMissingFloor", "NarinLaterQuestion", "저 왔어요. 혹시 그 뒤로 뭐 들으신 건 없어요?")),
			EIGDialogueChannel::Conversation, 0.0f, EIGDialoguePriority::Story);
		AIGHorrorHUD::PushDialogue(this,
			NSLOCTEXT("IGMissingFloor", "NarinSpeaker", "한나린"),
			GetNarinCounterLine(), EIGDialogueChannel::Conversation, 0.0f, EIGDialoguePriority::Story);
		if (bDeliveryQuestion)
		{
			Narrative->MarkBeatPlayed(FName(TEXT("Neighborhood.DeliveryDiscussed")));
			RequestArrivalAutosave();
		}
		// 대답이 조건을 먼저 읽은 뒤에 찍는다. 옥상 얘기는 한 번이면 된다.
		if (bRoofQuestion)
		{
			Narrative->MarkBeatPlayed(IGListenerGreybox::NarinRoofBeat);
		}
	}
	// 손 소리는 다음 날 다시 볼 때도 난다. 그래서 밤 번호로 돌려보내기 전에,
	// 비트를 찍기 전에 처음 여는지부터 본다.
	if (Evidence == ArrivalContract || Evidence == ArrivalParcelBox
		|| Evidence == ArrivalNotebookBox || Evidence == ArrivalVoicemailBox)
	{
		const TCHAR* OpenedBeat = Evidence == ArrivalParcelBox ? TEXT("Arrival.Box.Parcel")
			: Evidence == ArrivalNotebookBox ? TEXT("Arrival.Box.Notebook")
			: Evidence == ArrivalVoicemailBox ? TEXT("Arrival.Box.Voicemail")
			: TEXT("Arrival.Contract");
		// 계약서의 전제(지상 4층, 옥상 창고 금지)는 한국어로 인쇄된 종이에만 있다.
		// 다른 언어로 켠 사람에게는 처음 읽을 때 그 줄을 속말로 한 번 읽어 준다.
		if (Evidence == ArrivalContract
			&& !Narrative->HasBeatPlayed(FName(OpenedBeat))
			&& !AIGHorrorHUD::IsKoreanCulture())
		{
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT(
					"IGMissingFloor",
					"ArrivalContractPrintedRead",
					"지상 4층. 옥상은 같이 쓰되, 창고랑 기계실은 들어가지 말라고 돼 있네."),
				3.6f);
		}
		PlayArrivalHandSound(Evidence, !Narrative->HasBeatPlayed(FName(OpenedBeat)));
	}
	// 402호 메모. 빈집이라는 말을 나린에게 들었거나 문 안의 정적을 들었다면 한
	// 번 되받는다. 밤 번호로 돌려보내기 전에 본다 — 다음 날 다시 읽어도 된다.
	// 증거가 메모 문장을 먼저 띄우고 이 방송을 보내므로 그다음 줄로 선다.
	if (Evidence == ArrivalUnit402Note
		&& !Narrative->HasBeatPlayed(FName(TEXT("Witness.Unit402Vacancy"))))
	{
		const bool bHeardSilence =
			Narrative->HasWitness(EIGMissingFloorWitness::Unit402Silence);
		if ((bHeardSilence || Narrative->HasBeatPlayed(FName(TEXT("Arrival.Store"))))
			&& Narrative->MarkBeatPlayed(FName(TEXT("Witness.Unit402Vacancy"))))
		{
			AIGHorrorHUD::PushThought(
				this,
				bHeardSilence
					? IGListenerGreybox::Unit402VacancyHeardThought()
					: NSLOCTEXT(
						"IGMissingFloor",
						"Arrival402VacantThought",
						"빈집이라더니. 메모만 붙어 있네."),
				3.4f);
		}
	}
	if (Narrative->GetNightIndex() != 0)
	{
		return;
	}
	FName Beat;
	if (Evidence == ArrivalContract)
	{
		Beat = FName(TEXT("Arrival.Contract"));
	}
	else if (Evidence == ArrivalParcelBox)
	{
		Beat = FName(TEXT("Arrival.Box.Parcel"));
	}
	else if (Evidence == ArrivalNotebookBox)
	{
		Beat = FName(TEXT("Arrival.Box.Notebook"));
	}
	else if (Evidence == ArrivalVoicemailBox)
	{
		Beat = FName(TEXT("Arrival.Box.Voicemail"));
	}
	else if (Evidence == ArrivalStoreBell)
	{
		Beat = FName(TEXT("Arrival.Store"));
	}
	else if (Evidence == ArrivalUnit402Note)
	{
		Beat = FName(TEXT("Arrival.Unit402"));
	}
	else if (Evidence == ArrivalRoofLock)
	{
		Beat = FName(TEXT("Arrival.RoofDoor"));
		// 잠긴 자물쇠를 흔드는 자기 손 소리가 먼저다. 그 뒤 철문 너머에서 쇠붙이
		// 하나가 콘크리트에 내려앉는다(§8 0-4). 밤3 5층에서 집는 조율 렌치와 같은
		// 쇠라, 그날 렌치를 들면 이 소리가 손에서 되돌아온다. 창고 물건으로 넘기는
		// 독백은 자물쇠 독백 다음 줄로 선다 — 계약서 3조가 옥상 창고를 말한다.
		IGAudio::SpawnOneShotAt(
			this,
			IGAudio::SampleOr(TEXT("Lock_Rattle"), [this]() -> USoundBase*
			{
				return UIGToneSequenceSoundWave::CreateLockedRattle(this);
			}),
			Evidence->GetActorLocation(),
			0.5f,
			0.95f,
			60.0f,
			700.0f,
			EIGAudioBus::Player);
		if (!Narrative->HasBeatPlayed(Beat))
		{
			// 옥상 쪽 바닥에서 한 뼘 위. 철문이 사이에 있어 차폐가 780Hz 밑만 남긴다.
			const FVector Behind =
				Evidence->GetActorLocation() + FVector(0.0f, 260.0f, -60.0f);
			auto PlayRoofClink = [this, Behind]()
			{
				IGAudio::SpawnOneShotAt(
					this,
					UIGToneSequenceSoundWave::CreateTuningWrenchClink(this),
					Behind,
					0.55f,
					1.0f,
					80.0f,
					1000.0f,
					EIGAudioBus::World);
				AIGHorrorHUD::PushAudioCaptionAt(
					this,
					NSLOCTEXT(
						"IGMissingFloor",
						"ArrivalRoofClinkCaption",
						"쇠붙이 내려놓는 소리"),
					2.6f,
					Behind);
			};
			if (IsScriptedRun())
			{
				PlayRoofClink();
			}
			else
			{
				GetWorldTimerManager().SetTimer(
					ArrivalRoofClinkTimer,
					FTimerDelegate::CreateWeakLambda(this, [PlayRoofClink]() { PlayRoofClink(); }),
					IGListenerGreybox::ArrivalRoofClinkSeconds,
					false);
			}
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT(
					"IGMissingFloor",
					"ArrivalRoofRationalize",
					"창고에서 짐 정리하나 보네."),
				3.0f);
		}
	}
	if (!Beat.IsNone() && Narrative->MarkBeatPlayed(Beat))
	{
		RequestArrivalAutosave();
	}
	UpdateArrivalSequence();
}

void AIGListenerGreyboxDirector::PlayArrivalHandSound(
	AIGMissingFloorEvidence* Evidence,
	const bool bFirstOpen)
{
	if (!Evidence)
	{
		return;
	}
	// §26.2가 튜토리얼 팝업 대신 약속한 「서로 다른 물리 반응」이다. 처음 여는
	// 상자는 테이프부터 뜯고, 다시 볼 때는 손만 넣는다. 안에서 나는 소리는
	// 상자마다 다르다 — 송장 종이, 공구 쇠붙이, 전화기 진동. 전부 자기 손이
	// 내는 소리라 PLAYER 버스다. 상자 끄는 소리는 첫 컷에서 위층의 것이라 쓰지 않는다.
	const FVector At = Evidence->GetActorLocation();
	if (Evidence == ArrivalContract)
	{
		IGAudio::SpawnOneShotAt(
			this,
			IGAudio::SampleOr(TEXT("Paper_Turn_0"), [this]() -> USoundBase*
			{
				return UIGToneSequenceSoundWave::CreateJournalPageTurn(this);
			}),
			At, 0.36f, 1.0f, 60.0f, 600.0f, EIGAudioBus::Player);
		return;
	}
	const bool bParcel = Evidence == ArrivalParcelBox;
	const bool bTools = Evidence == ArrivalNotebookBox;
	const bool bPhone = Evidence == ArrivalVoicemailBox;
	if (!bParcel && !bTools && !bPhone)
	{
		return;
	}
	if (bFirstOpen)
	{
		IGAudio::SpawnOneShotAt(
			this, UIGToneSequenceSoundWave::CreatePackingTapeRip(this),
			At, 0.50f, 1.0f, 60.0f, 600.0f, EIGAudioBus::Player);
	}
	else if (bTools)
	{
		// 공구 두루마리의 천을 젖힌다.
		IGAudio::SpawnOneShotAt(
			this, UIGToneSequenceSoundWave::CreateClothSettle(this),
			At, 0.40f, 1.0f, 60.0f, 600.0f, EIGAudioBus::Player);
	}
	else
	{
		IGAudio::SpawnOneShotAt(
			this, UIGToneSequenceSoundWave::CreatePickupRustle(this),
			At, bParcel ? 0.45f : 0.30f, 1.0f, 60.0f, 600.0f, EIGAudioBus::Player);
	}

	// 안의 소리는 손이 닿은 뒤에 난다. 테이프를 뜯었다면 그만큼 늦게.
	const float ContentsDelay = bFirstOpen ? 0.36f : 0.20f;
	if (bParcel)
	{
		GetWorldTimerManager().SetTimer(
			ArrivalHandSoundTimer,
			FTimerDelegate::CreateWeakLambda(this, [this, At]()
			{
				// 송장을 편다. 계약서 종이보다 두껍고 낮게.
				IGAudio::SpawnOneShotAt(
					this,
					IGAudio::SampleOr(TEXT("Paper_Turn_1"), [this]() -> USoundBase*
					{
						return UIGToneSequenceSoundWave::CreateJournalPageTurn(this);
					}),
					At, 0.30f, 0.80f, 60.0f, 600.0f, EIGAudioBus::Player);
			}),
			ContentsDelay,
			false);
	}
	else if (bTools)
	{
		GetWorldTimerManager().SetTimer(
			ArrivalHandSoundTimer,
			FTimerDelegate::CreateWeakLambda(this, [this, At]()
			{
				IGAudio::SpawnOneShotAt(
					this, UIGToneSequenceSoundWave::CreateToolRollClink(this),
					At, 0.40f, 1.0f, 60.0f, 600.0f, EIGAudioBus::Player);
			}),
			ContentsDelay,
			false);
	}
	else if (bFirstOpen)
	{
		// 처음 꺼낸 전화기를 켜면 한 번 떨리고 만다. 녹음은 12초 내내 울리므로
		// 0.4초에 시작하는 첫 떨림만 쓰고 끊는다.
		GetWorldTimerManager().SetTimer(
			ArrivalHandSoundTimer,
			FTimerDelegate::CreateWeakLambda(this, [this, At]()
			{
				if (UAudioComponent* Previous = ArrivalPhoneBuzz.Get())
				{
					Previous->Stop();
				}
				ArrivalPhoneBuzz = IGAudio::SpawnOneShotAt(
					this, IGAudio::Sample(TEXT("Phone_Vibrate")),
					At, 0.22f, 1.0f, 60.0f, 600.0f, EIGAudioBus::Player);
				if (!ArrivalPhoneBuzz.IsValid())
				{
					return;
				}
				GetWorldTimerManager().SetTimer(
					ArrivalPhoneBuzzTimer,
					FTimerDelegate::CreateWeakLambda(this, [this]()
					{
						if (UAudioComponent* Buzz = ArrivalPhoneBuzz.Get())
						{
							Buzz->FadeOut(0.08f, 0.0f);
						}
						ArrivalPhoneBuzz.Reset();
					}),
					0.68f,
					false);
			}),
			0.20f,
			false);
	}
}

void AIGListenerGreyboxDirector::HandleUsedListingRead(
	AIGReadableNote* Note,
	const bool bOpened)
{
	if (!bOpened)
	{
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative
		|| !Narrative->MarkBeatPlayed(FName(TEXT("Day.UsedListing"))))
	{
		return;
	}
	// 진실을 열지 않는다. 열두 점이 팔렸다는 사실은 5층에 하나만 남은
	// 렌치를 만났을 때 비로소 뜻이 생긴다(§13 13행). 짚는 것은 판 사람의
	// 이름이 이 건물 이름이라는 것까지다. 날짜는 화면에 있고, 관리실 책상의
	// 현금 메모(밤2)가 나머지를 잇는다. 메모를 먼저 본 회차에는 거꾸로 잇는다.
	const bool bMemoSeen = Narrative->HasBeatPlayed(FName(TEXT("Night2.CashMemo")));
	AIGHorrorHUD::PushThought(
		this,
		bMemoSeen
			? NSLOCTEXT(
				"IGMissingFloor",
				"UsedListingMemoThought",
				"관리실 책상 메모에 있던 그 거래네. 8월 12일, 3만 원.")
			: NSLOCTEXT(
				"IGMissingFloor",
				"UsedListingThought",
				"오빠 공구랑 똑같다. 판 사람이… ‘달빛’?"),
		4.4f);
}

void AIGListenerGreyboxDirector::HandleNeighborhoodDeliveryRead(AIGReadableNote* Note, bool bOpened)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative) { return; }
	if (bOpened)
	{
		if (Narrative->MarkBeatPlayed(FName(TEXT("Neighborhood.Delivery")))) { RequestArrivalAutosave(); }
		return;
	}
	if (!Narrative->MarkBeatPlayed(FName(TEXT("Neighborhood.RearDoor")))) { return; }
	RequestArrivalAutosave();
	GetWorldTimerManager().SetTimer(NeighborhoodSoundTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		const FVector Door(1510, -1475, 108);
		if (!Player.IsValid() || FVector::DistSquared(Player->GetActorLocation(), Door) > FMath::Square(1200.f)) { return; }
		USoundBase* Sound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/S_Door_Steel_Close.S_Door_Steel_Close"));
		IGAudio::SpawnOneShotAt(this, Sound, Door, 0.36f, 0.87f, 100.f, 1700.f, EIGAudioBus::World);
		AIGHorrorHUD::PushAudioCaptionAt(this,
			NSLOCTEXT("IGMissingFloor", "RearDoorCaption", "철문 닫히는 소리"), 2.5f, Door);
	}), 1.4f, false);
}

FText AIGListenerGreyboxDirector::GetNarinCounterLine() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const int32 NightIndex = Narrative ? Narrative->GetNightIndex() : 0;
	if (Narrative && IGListenerGreybox::IsNarinDeliveryTalkDue(*Narrative))
	{
		return NSLOCTEXT("IGMissingFloor", "NarinDeliveryAnswer",
			"작년 여름에 몇 번 맡겼어요. 마지막 건 건물주 아저씨가 찾아가셨고요. 오빠분이 부탁했다고 하던데요.");
	}


	// 마지막 방문의 대화는 이후 나린이 진술하게 되는 계기다.
	if (NightIndex >= 3
		|| (Narrative && Narrative->HasTruth(EIGMissingFloorTruth::StillCoveringIt)))
	{
		return NSLOCTEXT(
			"IGMissingFloor",
			"NarinLineLastDay",
			"아직은요. 저 오늘도 밤새 있으니까, 무슨 일 있으면 바로 이쪽으로 오세요.");
	}
	// 입주 날 「옥상은 같이 쓴다더니」 하고 돌아선 그 문이다. 자물쇠를 바꾼
	// 때가 오빠가 사라진 여름이라는 것은 말하는 사람도 모른다.
	if (Narrative && IGListenerGreybox::IsNarinRoofTalkDue(*Narrative))
	{
		return NSLOCTEXT(
			"IGMissingFloor",
			"NarinLineRoof",
			"옥상요? 작년 여름에 건물주 아저씨가 자물쇠를 싹 바꿨대요. 열쇠는 관리실에나 있을걸요.");
	}
	if (NightIndex >= 1)
	{
		return NSLOCTEXT(
			"IGMissingFloor",
			"NarinLineMidWeek",
			"아직 소식 없으세요? 저도 단골들한테 물어봤는데, 최근에 봤다는 사람은 없더라고요.");
	}
	if (Narrative && Narrative->HasBeatPlayed(FName(TEXT("Arrival.Store"))))
	{
		return NSLOCTEXT("IGMissingFloor", "NarinRepeatAnswer", "지금은 그 정도예요. 다른 손님들한테도 한번 물어볼게요.");
	}
	// 이전 세입자들도 오래 살지 못했다는 사실만 먼저 알린다. 옆집 402호가 비어
	// 있다는 것도 여기서 듣는다 — 402호 문의 메모는 떠난 사람이 붙인 것이고,
	// 밤1에 그 문 안에서 나는 노크는 빈집에서 난다.
	return NSLOCTEXT(
		"IGMissingFloor",
		"ArrivalNarinLine",
		"아, 큰 가방 메고 다니시던 분? 작년 여름쯤부터 안 오시던데요. 403호로 오셨어요? 그 집은 올해만 벌써 세 번째네요. 옆집도 봄부터 비어 있고요.");
}

void AIGListenerGreyboxDirector::RequestArrivalAutosave()
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GetWorld();
	UIGSaveSubsystem* SaveSubsystem = GameInstance
		? GameInstance->GetSubsystem<UIGSaveSubsystem>()
		: nullptr;
	if (SaveSubsystem && World)
	{
		SaveSubsystem->RequestAutosave(
			FGameplayTag::RequestGameplayTag(FName(TEXT("Chapter.MissingFloor")), false),
			World->GetOutermost()->GetFName(),
			FGameplayTag::RequestGameplayTag(
				FName(TEXT("Checkpoint.MissingFloor.Arrival")),
				false));
	}
}

FText AIGListenerGreyboxDirector::GetObjectiveText() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!bProductionMode || !Narrative || Narrative->GetNightIndex() != 0)
	{
		return FText::GetEmpty();
	}
	// 콜드 오픈 동안은 목표를 띄우지 않는다. 제목이 선 뒤에 처음 뜬다.
	if (GetWorld() && GetWorld()->GetTimerManager().IsTimerActive(ArrivalTitleCardTimer))
	{
		return FText::GetEmpty();
	}
	if (GetObjectiveProgress() < 0.5f)
	{
		return NSLOCTEXT("IGMissingFloor", "ArrivalObjectiveExplore", "짐 풀고 빌라와 편의점 둘러보기");
	}
	if (!Narrative->HasBeatPlayed(FName(TEXT("Arrival.Contract"))))
	{
		return NSLOCTEXT("IGMissingFloor", "ArrivalObjectiveContract", "책상 위 임대차계약서 보기");
	}
	if (!AreArrivalBoxesOpened())
	{
		return NSLOCTEXT("IGMissingFloor", "ArrivalObjectiveBoxes", "오빠가 남긴 짐 살펴보기");
	}
	if (!Narrative->HasBeatPlayed(FName(TEXT("Arrival.Store"))))
	{
		return NSLOCTEXT("IGMissingFloor", "ArrivalObjectiveStore", "편의점 직원에게 오빠 소식 묻기");
	}
	if (!Narrative->HasBeatPlayed(FName(TEXT("Arrival.Unit401")))
		|| !Narrative->HasBeatPlayed(FName(TEXT("Arrival.Unit402"))))
	{
		return NSLOCTEXT("IGMissingFloor", "ArrivalObjectiveNeighbors", "401호, 402호 앞 살펴보기");
	}
	if (!Narrative->HasBeatPlayed(FName(TEXT("Arrival.RoofDoor"))))
	{
		return NSLOCTEXT("IGMissingFloor", "ArrivalObjectiveRoof", "계약서에 적힌 옥상 문 확인하기");
	}
	return NSLOCTEXT("IGMissingFloor", "ArrivalObjectiveSleep", "403호로 돌아가서 자기");
}

float AIGListenerGreyboxDirector::GetObjectiveProgress() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!bProductionMode || !Narrative || Narrative->GetNightIndex() != 0)
	{
		return 0.0f;
	}
	int32 Completed = 0;
	for (const TCHAR* Beat : {
		TEXT("Arrival.Contract"), TEXT("Arrival.Box.Parcel"),
		TEXT("Arrival.Box.Notebook"), TEXT("Arrival.Box.Voicemail"),
		TEXT("Arrival.Store"), TEXT("Arrival.Unit401"),
		TEXT("Arrival.Unit402"), TEXT("Arrival.RoofDoor")})
	{
		Completed += Narrative->HasBeatPlayed(FName(Beat)) ? 1 : 0;
	}
	return static_cast<float>(Completed) / 8.0f;
}

UIGMissingFloorNarrativeSubsystem* AIGListenerGreyboxDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
}

bool AIGListenerGreyboxDirector::IsScriptedRun() const
{
	return bProbeRequested || bArrivalProbeRequested || bArrivalCaptureRequested
		|| bNightCaptureRequested || bHistogramRequested || bCctvFeedProbeRequested
		|| bMercyNoteProbeRequested;
}

void AIGListenerGreyboxDirector::ReleaseArrivalOpeningBlack()
{
	if (!bArrivalOpeningHeldBlack)
	{
		return;
	}
	bArrivalOpeningHeldBlack = false;
	APlayerController* Controller =
		GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Controller && Controller->PlayerCameraManager)
	{
		Controller->PlayerCameraManager->StartCameraFade(
			1.0f,
			0.0f,
			0.4f,
			FLinearColor::Black,
			/*bShouldFadeAudio=*/false,
			/*bHoldWhenFinished=*/false);
	}
}

float AIGListenerGreyboxDirector::GetUnit401PrayerRestLevel() const
{
	return NightPhase && NightPhase->IsHourActive()
		? 0.0f
		: IGListenerGreybox::Unit401PrayerDayLevel;
}

void AIGListenerGreyboxDirector::FadeUnit401Prayer(const float Level, const float Seconds)
{
	UAudioComponent* Radio = Unit401PrayerLoop.Get();
	if (!IsValid(Radio))
	{
		return;
	}
	// 0으로 가는 페이드는 엔진에서 정지다. 멈춘 루프는 FadeIn으로만 다시 산다.
	if (Level <= KINDA_SMALL_NUMBER)
	{
		if (Radio->IsPlaying())
		{
			Radio->FadeOut(Seconds, 0.0f);
		}
		return;
	}
	if (Radio->IsPlaying())
	{
		Radio->AdjustVolume(Seconds, Level);
	}
	else
	{
		Radio->FadeIn(Seconds, Level);
	}
}

void AIGListenerGreyboxDirector::ScheduleArrivalBaseline(
	const float MinDelaySeconds,
	const float MaxDelaySeconds)
{
	GetWorldTimerManager().SetTimer(
		ArrivalBaselineTimer,
		this,
		&AIGListenerGreyboxDirector::PlayArrivalBaselineEvent,
		FMath::FRandRange(MinDelaySeconds, MaxDelaySeconds),
		false);
}

void AIGListenerGreyboxDirector::StopArrivalBaseline()
{
	GetWorldTimerManager().ClearTimer(ArrivalBaselineTimer);
	GetWorldTimerManager().ClearTimer(ArrivalBaselineStepTimer);
	for (TWeakObjectPtr<UAudioComponent>& Pipe : ArrivalBaselinePipes)
	{
		// 물소리는 루프 파형이다. 원샷으로 틀었어도 끄지 않으면 영영 돈다.
		if (UAudioComponent* Flow = Pipe.Get())
		{
			Flow->FadeOut(0.3f, 0.0f);
		}
		Pipe.Reset();
	}
}

void AIGListenerGreyboxDirector::PlayArrivalBaselineEvent()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || Narrative->GetNightIndex() != 0
		|| Narrative->HasBeatPlayed(FName(TEXT("Arrival.Slept")))
		|| !Entity || !Entity->IsDormant()
		|| (NightPhase && NightPhase->IsHourActive()))
	{
		StopArrivalBaseline();
		return;
	}
	// 4층에 있을 때만 난다. 편의점·골목·계단에서는 이 건물 소리가 제자리에서
	// 들리지 않고, 계단 위에서는 보이지 않는 사람이 옆을 지나가게 된다.
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	const FVector PlayerAt = PlayerCharacter
		? PlayerCharacter->GetActorLocation()
		: FVector::ZeroVector;
	if (!PlayerCharacter || PlayerAt.Z < IGListenerGreybox::FourthFloorZ + 20.0f)
	{
		ScheduleArrivalBaseline(10.0f, 15.0f);
		return;
	}
	const FVector Stairwell(-465.0f, -150.0f, IGListenerGreybox::FourthFloorZ);
	const bool bNearStairwell = FVector::Dist2D(PlayerAt, Stairwell) < 400.0f;
	// 같은 소리가 두 번 잇따르면 시계가 된다. 계단참 곁에서는 발소리를 고르지 않는다.
	int32 Kind = FMath::RandRange(0, 2);
	for (int32 Attempt = 0; Attempt < 3
		&& (Kind == LastArrivalBaselineKind || (Kind == 0 && bNearStairwell)); ++Attempt)
	{
		Kind = (Kind + 1) % 3;
	}
	LastArrivalBaselineKind = Kind;
	ArrivalBaselineSeed = static_cast<uint32>(GetWorld()->GetTimeSeconds() * 977.0f);
	ArrivalBaselineStep = 0;
	GetWorldTimerManager().ClearTimer(ArrivalBaselineStepTimer);
	switch (Kind)
	{
	case 0:
	{
		// 누가 3층에서 계단을 내려간다. 다섯 걸음, 한참 뒤 1층 공동현관.
		// 3층 참에서 동쪽 띠를 따라 북쪽으로 내려가는 디딤판 위다.
		auto StepAt = [](const int32 Step)
		{
			return FVector(-407.5f, -215.0f + 40.0f * static_cast<float>(Step),
				IGListenerGreybox::FourthFloorZ - 300.0f - 30.0f * static_cast<float>(Step));
		};
		auto AdvanceStairs = [this, StepAt]()
		{
			const int32 Step = ArrivalBaselineStep++;
			if (Step < 5)
			{
				IGAudio::SpawnOneShotAt(
					this,
					IGAudio::SampleVariantOr(
						TEXT("Foot_Concrete"), 5, ArrivalBaselineSeed + static_cast<uint32>(Step),
						[this]() -> USoundBase*
						{
							return UIGToneSequenceSoundWave::CreateFootstep(this, 0.9f, 0.5f);
						}),
					StepAt(Step),
					FMath::FRandRange(0.26f, 0.32f),
					FMath::FRandRange(0.95f, 1.02f),
					150.0f,
					1400.0f,
					EIGAudioBus::World);
				return;
			}
			if (Step < 8)
			{
				return;
			}
			GetWorldTimerManager().ClearTimer(ArrivalBaselineStepTimer);
			IGAudio::SpawnOneShotAt(
				this,
				IGAudio::SampleOr(TEXT("Door_Steel_Close"), [this]() -> USoundBase*
				{
					return UIGToneSequenceSoundWave::CreateDoorThud(this);
				}),
				FVector(604.0f, -385.0f, 110.0f),
				0.22f,
				0.9f,
				200.0f,
				1700.0f,
				EIGAudioBus::World);
		};
		AdvanceStairs();
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "BaselineStairsCaption", "계단을 내려가는 발소리"),
			2.4f,
			StepAt(0));
		GetWorldTimerManager().SetTimer(
			ArrivalBaselineStepTimer,
			FTimerDelegate::CreateWeakLambda(this, [AdvanceStairs]() { AdvanceStairs(); }),
			0.46f,
			true);
		break;
	}
	case 1:
	{
		// 어느 집이 물을 내렸다. 벽 속 관을 타고 4층 바닥 높이에서 3층으로
		// 내려간다. 위로 가는 물은 없다.
		const FVector Cupboard = AIGPrologueWorldScene::GetBoilerCupboardLocation();
		const FVector Upper(Cupboard.X, Cupboard.Y, IGListenerGreybox::FourthFloorZ + 40.0f);
		const FVector Lower(Cupboard.X, Cupboard.Y, IGListenerGreybox::FourthFloorZ - 260.0f);
		ArrivalBaselinePipes[0] = IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreatePipeWaterFlow(this, 2),
			Upper,
			0.45f,
			1.0f,
			120.0f,
			1100.0f,
			EIGAudioBus::World);
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "BaselinePipeCaption", "벽 속 배관으로 물 내려가는 소리"),
			2.2f,
			Upper);
		GetWorldTimerManager().SetTimer(
			ArrivalBaselineStepTimer,
			FTimerDelegate::CreateWeakLambda(this, [this, Lower]()
			{
				ArrivalBaselinePipes[1] = IGAudio::SpawnOneShotAt(
					this,
					UIGToneSequenceSoundWave::CreatePipeWaterFlow(this, 2),
					Lower,
					0.32f,
					1.0f,
					120.0f,
					1100.0f,
					EIGAudioBus::World);
				GetWorldTimerManager().SetTimer(
					ArrivalBaselineStepTimer,
					FTimerDelegate::CreateWeakLambda(this, [this]()
					{
						for (TWeakObjectPtr<UAudioComponent>& Pipe : ArrivalBaselinePipes)
						{
							if (UAudioComponent* Flow = Pipe.Get())
							{
								Flow->FadeOut(1.6f, 0.0f);
							}
							Pipe.Reset();
						}
					}),
					3.7f,
					false);
			}),
			0.8f,
			false);
		break;
	}
	default:
	{
		// 3층 어느 집 현관문이 닫힌다. 이 건물의 문은 전부 철문이다.
		const FVector ThirdFloorDoor(-300.0f, -305.0f, IGListenerGreybox::FourthFloorZ - 230.0f);
		IGAudio::SpawnOneShotAt(
			this,
			IGAudio::SampleOr(TEXT("Door_Steel_Close"), [this]() -> USoundBase*
			{
				return UIGToneSequenceSoundWave::CreateDoorThud(this);
			}),
			ThirdFloorDoor,
			0.2f,
			1.06f,
			150.0f,
			1400.0f,
			EIGAudioBus::World);
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "BaselineDoorCaption", "현관문 닫히는 소리"),
			2.0f,
			ThirdFloorDoor);
		break;
	}
	}
	ScheduleArrivalBaseline(40.0f, 75.0f);
}

void AIGListenerGreyboxDirector::HandleHourActiveChanged(const bool bActive)
{
	GetWorldTimerManager().ClearTimer(HwangPeekTimer);
	GetWorldTimerManager().ClearTimer(HwangPermissionTimer);
	GetWorldTimerManager().ClearTimer(NightFourWallCoverTimer);
	if (!bActive)
	{
		// 대답을 들은 밤3이면 어떻게 끝났든 05:30에 신고한다. 불러온 낮이면
		// 덜 온 신고 문자를 이어 받는다.
		MakeNightThreeFirstReport();
		// 1-7. 밤1을 채운 낮, 401호 앞을 지나가면 황순금이 문틈으로 내다본다.
		const UIGMissingFloorNarrativeSubsystem* PeekNarrative = GetNarrative();
		if (!bProbeRequested && !bNightCaptureRequested && !bArrivalCaptureRequested
			&& PeekNarrative
			&& PeekNarrative->GetNightIndex() == 1
			&& PeekNarrative->HasBeatPlayed(AIGNightPhaseDirector::GoalBeatId(1))
			&& !PeekNarrative->HasBeatPlayed(IGListenerGreybox::HwangPeekBeat))
		{
			GetWorldTimerManager().SetTimer(
				HwangPeekTimer,
				this,
				&AIGListenerGreyboxDirector::PollHwangPeek,
				IGListenerGreybox::HwangPeekPollSeconds,
				true);
		}
	}
	// 밤의 베드는 밤에만 온전하다. 눈을 감는 검은 화면 아래서 2초에 걸쳐
	// 차오르고, 새벽에는 문이 열리는 소리와 함께 3초에 걸쳐 가라앉는다.
	for (UAudioComponent* Bed : NightAmbienceBeds)
	{
		if (!IsValid(Bed))
		{
			continue;
		}
		const float Level = bActive ? 1.0f : IGListenerGreybox::NightBedDayScale;
		if (!Bed->IsPlaying())
		{
			Bed->FadeIn(bActive ? 2.0f : 0.05f, Level);
			continue;
		}
		Bed->AdjustVolume(bActive ? 2.0f : 3.0f, Level);
	}
	// 401호 라디오는 낮의 소리다. 밤이 열리는 검은 화면 아래서 멎고, 새벽이면
	// 돌아온다. 그 시간의 예불은 문에 귀를 댄 사람만 듣는다.
	GetWorldTimerManager().ClearTimer(Unit401PrayerReturnTimer);
	GetWorldTimerManager().ClearTimer(Unit401RadioFadeTimer);
	FadeUnit401Prayer(
		bActive ? 0.0f : IGListenerGreybox::Unit401PrayerDayLevel,
		bActive ? 1.5f : 3.0f);
	// One boundary, every consequence, in one place: the entity sleeps by
	// day, the booth locks by day, and the day verbs vanish by night.
	if (Entity)
	{
		Entity->SetDormant(!bActive);
	}
	if (PuzzleOne)
	{
		PuzzleOne->SetHourActive(bActive);
	}
	if (NightTwoBeats)
	{
		NightTwoBeats->SetHourActive(bActive);
	}
	if (PuzzleTwo)
	{
		PuzzleTwo->SetHourActive(bActive);
	}
	if (NightThree)
	{
		NightThree->SetHourActive(bActive);
	}
	if (NightFour)
	{
		NightFour->SetHourActive(bActive);
	}
	if (Mercy)
	{
		Mercy->SetHourActive(bActive);
	}
	if (SleepTarget)
	{
		SleepTarget->SetInteractionEnabled(!bActive);
	}
	if (Unit401Door)
	{
		Unit401Door->SetInteractionEnabled(!bActive);
	}
	if (ArrivalStoreBell)
	{
		// 프롤로그가 끝난 뒤에도 편의점은 낮마다 열려 있다. 입주 시퀀스가
		// 밤 0에서만 돌기 때문에 그 뒤로는 아무도 이 문을 다시 켜 주지
		// 않았고, 나린은 첫날 한 마디만 하고 사라진 사람이 되어 있었다.
		const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (Narrative && Narrative->GetNightIndex() >= 1)
		{
			ArrivalStoreBell->SetInteractionEnabled(!bActive);
		}
	}
	// 폰은 한 대다. 그 시간에는 머리맡에서 알람으로 울고 그 뒤로는 손에 있다.
	RefreshTablePhone();

	// 밤4, 벽이 닫힌 채 시작하는 그 시간. 잠에서 깬 밤도, 잠을 거치지 않는
	// 엔딩 C의 재시도도, 불러온 밤도 여기를 지난다. 캡처 화면과 프로브에는
	// 띄우지 않는다.
	const UIGMissingFloorNarrativeSubsystem* WallNarrative = GetNarrative();
	if (bActive && !bProbeRequested && !bNightCaptureRequested
		&& WallNarrative
		&& WallNarrative->GetNightIndex() == 4
		&& !WallNarrative->IsNightFourWallOpened())
	{
		// 물이 돌기 전의 벽은 두드릴 수 없다. 물보다 벽을 먼저 찾아간 사람이
		// 고장으로 읽지 않게 벽 앞에서 한 번 이유를 떠올린다. 물이 돌거나 벽이
		// 열리면 스스로 멎는다.
		if (!WallNarrative->IsNightFourMaskRunning())
		{
			GetWorldTimerManager().SetTimer(
				NightFourWallCoverTimer,
				this,
				&AIGListenerGreyboxDirector::PollNightFourWallCover,
				0.5f,
				true);
		}
		// 04:30, 목한수가 먼저 벽을 덮는다. 재시도도 04:30으로 돌아가니 드릴이
		// 다시 난다. 저장에서 이어 붙인 밤은 되풀이가 아니다. 다른 밤의 여는
		// 소리처럼 다시 내지 않는다.
		if (!bResumingSealedHour)
		{
			GetWorldTimerManager().SetTimer(
				NightSettleTimer,
				this,
				&AIGListenerGreyboxDirector::PlayNightOpeningSettle,
				IGListenerGreybox::NightOpeningSettleSeconds,
				false);
		}
	}
}

void AIGListenerGreyboxDirector::HandleNightOneSolved()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!NightPhase || !Narrative || Narrative->GetNightIndex() != 1
		|| !NightPhase->IsHourActive())
	{
		return;
	}
	// 프로브는 같은 프레임에 새벽이 서 있어야 한다. 쫓기는 중이면 예전처럼
	// 곧장 새벽이다 — 위를 올려다볼 틈을 주면 그 틈에 잡힌다.
	if (bProbeRequested || IsNightOneSolveUnderThreat())
	{
		NightPhase->CompleteNightGoal();
		return;
	}
	// 목표는 지금 기록한다. 기다리는 사이 05:30이 먼저 와도 채운 밤이다.
	Narrative->MarkBeatPlayed(AIGNightPhaseDirector::GoalBeatId(1));
	bNightOneDawnHeldByNote = false;
	// 로비에서는 4층 천장 위의 험이 닿지 않는다. 조율음이 멎은 뒤 불이 들어오는
	// 순간만 계단실을 타고 내려온다(§8 1-6, §7 「천장 너머에서 안정기가 운다」).
	GetWorldTimerManager().SetTimer(
		NightOneBallastTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (PuzzleOne && NightPhase && NightPhase->IsHourActive())
			{
				PuzzleOne->PlayBallastFromAbove();
			}
		}),
		IGListenerGreybox::NightOneBallastCueSeconds,
		false);
	GetWorldTimerManager().SetTimer(
		NightOneRealizationTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!NightPhase || !NightPhase->IsHourActive())
			{
				return;
			}
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT("IGMissingFloor", "NightOneSolvedThought", "…위에 뭐가 있어."),
				3.2f);
		}),
		IGListenerGreybox::NightOneRealizationSeconds,
		false);
	GetWorldTimerManager().SetTimer(
		NightOneDawnTimer,
		this,
		&AIGListenerGreyboxDirector::FinishNightOneAtDawn,
		IGListenerGreybox::NightOneDawnDelaySeconds,
		false);
}

bool AIGListenerGreyboxDirector::IsNightOneSolveUnderThreat() const
{
	if (NightLoop && NightLoop->IsCaptureResetInFlight())
	{
		return true;
	}
	if (!Entity || Entity->IsDormant())
	{
		return false;
	}
	const EIGListenerState State = Entity->GetListenerState();
	if (State == EIGListenerState::Chasing || State == EIGListenerState::CaptureHold)
	{
		return true;
	}
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	return PlayerCharacter
		&& (State == EIGListenerState::Investigating
			|| State == EIGListenerState::Holding
			|| State == EIGListenerState::Searching)
		&& FVector::Dist(Entity->GetActorLocation(), PlayerCharacter->GetActorLocation())
			< IGListenerGreybox::NightOneSolveThreatDistance;
}

void AIGListenerGreyboxDirector::FinishNightOneAtDawn()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!NightPhase || !NightPhase->IsHourActive() || !Narrative
		|| Narrative->GetNightIndex() != 1)
	{
		// 05:30이 먼저 왔다. 목표는 이미 기록돼 있다.
		bNightOneDawnHeldByNote = false;
		return;
	}
	// 포획 암전 위에 새벽 암전을 겹치지 않는다. 읽던 종이 뒤로 새벽이 지나가게
	// 두지도 않는다 — 검침표가 마지막 조각이면 내려놓은 뒤에야 독백이 뜬다.
	const bool bNoteOpen = AIGReadableNote::GetOpenNote() != nullptr;
	if ((NightLoop && NightLoop->IsCaptureResetInFlight()) || bNoteOpen)
	{
		if (bNoteOpen)
		{
			bNightOneDawnHeldByNote = true;
		}
		GetWorldTimerManager().SetTimer(
			NightOneDawnTimer,
			this,
			&AIGListenerGreyboxDirector::FinishNightOneAtDawn,
			IGListenerGreybox::NightOneDawnPollSeconds,
			false);
		return;
	}
	if (bNightOneDawnHeldByNote)
	{
		bNightOneDawnHeldByNote = false;
		GetWorldTimerManager().SetTimer(
			NightOneDawnTimer,
			this,
			&AIGListenerGreyboxDirector::FinishNightOneAtDawn,
			IGListenerGreybox::NightOneDawnAfterNoteSeconds,
			false);
		return;
	}
	NightPhase->CompleteNightGoal();
}

void AIGListenerGreyboxDirector::HandleNightTwoSolved()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!NightPhase || !Narrative || Narrative->GetNightIndex() != 2)
	{
		return;
	}
	// §8 밤2 does not end where the paper contradicts itself; it ends at 403's
	// door. T7 arms 비트 2-5 instead of releasing her to dawn from inside the
	// booth, and the walk home decides the night.
	if (NightTwoBeats)
	{
		NightTwoBeats->ArmReturnChase();
		return;
	}
	// No beat director: complete rather than trap her in a night with no exit.
	NightPhase->CompleteNightGoal();
}

void AIGListenerGreyboxDirector::HandleNightTwoReturnedHome()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (NightPhase && Narrative && Narrative->GetNightIndex() == 2)
	{
		NightPhase->CompleteNightGoal();
	}
}

void AIGListenerGreyboxDirector::HandleNightThreeSolved()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!NightPhase || !Narrative || Narrative->GetNightIndex() != 3)
	{
		return;
	}
	// §8 밤3 does not end at the wall. T9 arms 비트 3-7 and he is standing in
	// the corridor between the stair core and her door; the walk past him is the
	// rest of the night.
	if (NightThree)
	{
		NightThree->ArmReturnPass();
		return;
	}
	// No director: complete rather than trap her on a floor with no exit.
	MakeNightThreeFirstReport();
	NightPhase->CompleteNightGoal();
}

void AIGListenerGreyboxDirector::HandleNightThreeReturnedHome()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (NightPhase && Narrative && Narrative->GetNightIndex() == 3)
	{
		NightPhase->CompleteNightGoal();
	}
}

namespace IGListenerGreybox
{
	// 05:30 신고 뒤의 문자 줄기(§1 밤3 이후 현실 대응). 단계마다 저장되는 비트로
	// 기억해야 새벽 자동 저장 직후에 끈 게임도 빠진 문자부터 다시 받는다.
	const FName ReportSentBeat(TEXT("Night3.ReportSent"));
	const FName ReportReceiptBeat(TEXT("Night3.ReportReceipt"));
	const FName SiteCheckBeat(TEXT("Night3.SiteCheck"));
	const FName EvictionPhotoBeat(TEXT("Day.EvictionPhoto"));
	const FName EvictionDeadlineBeat(TEXT("Day.EvictionDeadline"));
	constexpr float ReportLanePollSeconds = 0.25f;
	/** 새벽 암전 뒤 「문이 열린다. 아침이다.」가 먼저 줄에 서도록 기다리는 시간. */
	constexpr float ReportDawnLeadSeconds = 2.4f;
	/** 접근성 배율 2배에서 새벽 독백과 보낸 문자가 다 지나가는 시간보다 길다. */
	constexpr float ReportLaneMaxWaitSeconds = 24.0f;
	/** 앞의 말이 사라지고 첫 문자가 뜨기까지 한 숨. */
	constexpr float ReportLeadGapSeconds = 0.5f;
	/** 보낸 문자가 사라지고 접수 문자가 오기까지. */
	constexpr float ReportReceiptGapSeconds = 1.4f;
	/** 접수 문자 뒤 경찰의 현장 확인 문자까지. 낮이 한 번 흐를 만큼. */
	constexpr float SiteCheckGapSeconds = 28.0f;
	/** 현장 확인 문자 뒤 요구서가 붙기까지. 경찰이 다녀간 것을 관리인도 봤다. */
	constexpr float EvictionPostGapSeconds = 7.0f;
	/** 403호로 돌아오기를 기다리며 다시 보는 간격. */
	constexpr float EvictionPostPollSeconds = 0.5f;
	/** 사진 문자와 마지막 한 줄 사이. */
	constexpr float EvictionDeadlineGapSeconds = 0.4f;
	/** 문자가 다 오기 전에 누우려 했으면 이보다 오래 쉬지 않는다. */
	constexpr float ReportRushedGapSeconds = 0.8f;

	enum class EFirstReportText : uint8
	{
		None,
		Sent,
		Receipt,
		SiteCheck,
		EvictionPosted,
		EvictionPhoto,
		EvictionDeadline,
	};

	/** 지금 받을 차례인 신고 문자. 받을 것이 없으면 None이다. */
	static EFirstReportText NextFirstReportText(
		const UIGMissingFloorNarrativeSubsystem& Narrative,
		const AIGMissingFloorNightFourDirector* NightFourDirector)
	{
		if (Narrative.GetNightIndex() != 3
			|| !Narrative.WasFirstReportMade()
			|| !Narrative.HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer))
		{
			return EFirstReportText::None;
		}
		if (!Narrative.HasBeatPlayed(ReportSentBeat))
		{
			return EFirstReportText::Sent;
		}
		if (!Narrative.HasBeatPlayed(ReportReceiptBeat))
		{
			return EFirstReportText::Receipt;
		}
		if (!Narrative.HasBeatPlayed(SiteCheckBeat))
		{
			return EFirstReportText::SiteCheck;
		}
		if (NightFourDirector && !NightFourDirector->IsEvictionNoticePosted())
		{
			return EFirstReportText::EvictionPosted;
		}
		// 여기부터는 요구서를 읽은 뒤의 일이다.
		if (!Narrative.HasSource(
			EIGMissingFloorTruth::StillCoveringIt,
			EIGMissingFloorSource::EvictionWarning))
		{
			return EFirstReportText::None;
		}
		if (!Narrative.HasBeatPlayed(EvictionPhotoBeat))
		{
			return EFirstReportText::EvictionPhoto;
		}
		if (!Narrative.HasBeatPlayed(EvictionDeadlineBeat))
		{
			return EFirstReportText::EvictionDeadline;
		}
		return EFirstReportText::None;
	}

	/** 문자가 잠시 멎어도 요구서를 읽고 후속 신고를 마치기 전에는 밤4로 가지 않는다. */
	static bool HasCompletedFirstReportDay(const UIGMissingFloorNarrativeSubsystem& Narrative)
	{
		return Narrative.WasFirstReportMade()
			&& Narrative.HasBeatPlayed(ReportSentBeat)
			&& Narrative.HasBeatPlayed(ReportReceiptBeat)
			&& Narrative.HasBeatPlayed(SiteCheckBeat)
			&& Narrative.HasSource(
				EIGMissingFloorTruth::StillCoveringIt,
				EIGMissingFloorSource::EvictionWarning)
			&& Narrative.HasBeatPlayed(EvictionPhotoBeat)
			&& Narrative.HasBeatPlayed(EvictionDeadlineBeat);
	}

	/** 화면에 대사가 떠 있거나 줄을 서 있다. */
	static bool IsDialogueLaneBusy(const UWorld* World)
	{
		const APlayerController* Controller = World
			? World->GetFirstPlayerController()
			: nullptr;
		const AIGHorrorHUD* Hud = Controller
			? Cast<AIGHorrorHUD>(Controller->GetHUD())
			: nullptr;
		return Hud && !Hud->IsDialogueLaneIdle();
	}

	/**
	 * 요구서의 「내일 오전 7시」를 본 회차인가. 담당자에게 보낸 사진 문자가
	 * 그 시각을 적고, 요구서를 읽으면 단서 기록의 요구서 항목에도 남는다.
	 */
	static bool KnowsEvictionDeadline(const UIGMissingFloorNarrativeSubsystem& Narrative)
	{
		return Narrative.HasBeatPlayed(EvictionPhotoBeat)
			|| Narrative.HasSource(
				EIGMissingFloorTruth::StillCoveringIt,
				EIGMissingFloorSource::EvictionWarning);
	}
}

void AIGListenerGreyboxDirector::MakeNightThreeFirstReport()
{
	// 신고는 신호가 돌아오는 05:30의 일이라 귀가에 묶지 않고 새벽에 묶는다.
	// 대답을 들었으면 그를 지나쳐 돌아왔든 시간이 다 됐든 신고한다. 대답을
	// 못 들은 밤3에는 신고할 것이 없다. 그 밤은 다음 저녁에 되풀이되고,
	// 대답을 들은 새벽이 신고를 맡는다. 밤4는 현장 보존 고지를 어길 수는
	// 있어도 주인공이 신고를 잊어서 생기는 밤이어서는 안 된다.
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative
		|| Narrative->GetNightIndex() != 3
		|| !Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer))
	{
		return;
	}
	if (Narrative->WasFirstReportMade())
	{
		// 이미 한 신고다. 문자가 덜 왔으면(새벽 자동 저장 직후에 끈 게임,
		// 대답 없이 신고가 소진된 예전 저장) 빠진 데부터 이어 받는다.
		if (!bProbeRequested)
		{
			ScheduleFirstReportTexts(IGListenerGreybox::ReportDawnLeadSeconds);
		}
		return;
	}
	Narrative->SetFirstReportMade(true);
	Narrative->MarkBeatPlayed(FName(TEXT("Night3.FirstReport")));
	if (bProbeRequested)
	{
		// 프로브는 새벽 프레임에 낮을 끝까지 밟는다. 문자는 띄우지 않고 기록만
		// 남긴 채 요구서를 바로 붙인다. 뒤의 낮 단계가 곧바로 요구서를 읽는다.
		Narrative->MarkBeatPlayed(IGListenerGreybox::ReportSentBeat);
		Narrative->MarkBeatPlayed(IGListenerGreybox::ReportReceiptBeat);
		Narrative->MarkBeatPlayed(IGListenerGreybox::SiteCheckBeat);
		if (NightFour)
		{
			NightFour->PostEvictionNotice(/*bAnnounce=*/false);
		}
		return;
	}
	// 신고는 불리언이 아니다. 새벽 독백이 지나가면 보낸 문자와 접수 문자가
	// 화면에 남는다. 밤4의 두 번째 신고와 엔딩 A의 근거가 이 줄기다.
	ScheduleFirstReportTexts(IGListenerGreybox::ReportDawnLeadSeconds);
}

void AIGListenerGreyboxDirector::ScheduleFirstReportTexts(const float DelaySeconds)
{
	ReportLaneWaitSeconds = 0.0f;
	ReportPendingGapSeconds = IGListenerGreybox::ReportLeadGapSeconds;
	GetWorldTimerManager().SetTimer(
		ReportTimer,
		this,
		&AIGListenerGreyboxDirector::AdvanceFirstReportTexts,
		FMath::Max(DelaySeconds, 0.05f),
		false);
}

void AIGListenerGreyboxDirector::AdvanceFirstReportTexts()
{
	using IGListenerGreybox::EFirstReportText;
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const EFirstReportText Next = Narrative && NightPhase && !NightPhase->IsHourActive()
		? IGListenerGreybox::NextFirstReportText(*Narrative, NightFour.Get())
		: EFirstReportText::None;
	if (Next == EFirstReportText::None)
	{
		bFirstReportRushed = false;
		return;
	}
	const auto Rearm = [this](const float Seconds)
	{
		GetWorldTimerManager().SetTimer(
			ReportTimer,
			this,
			&AIGListenerGreyboxDirector::AdvanceFirstReportTexts,
			Seconds,
			false);
	};
	// 다른 말이 떠 있으면 끝나기를 기다린다. 줄 뒤에 세우면 진동이 먼저 울리고
	// 글은 몇 초 뒤에 뜨거나, 오래 밀리면 아예 버려진다.
	if (IGListenerGreybox::IsDialogueLaneBusy(GetWorld())
		&& ReportLaneWaitSeconds < IGListenerGreybox::ReportLaneMaxWaitSeconds)
	{
		ReportLaneWaitSeconds += IGListenerGreybox::ReportLanePollSeconds;
		Rearm(IGListenerGreybox::ReportLanePollSeconds);
		return;
	}
	ReportLaneWaitSeconds = 0.0f;
	if (ReportPendingGapSeconds > 0.0f)
	{
		const float Gap = bFirstReportRushed
			? FMath::Min(ReportPendingGapSeconds, IGListenerGreybox::ReportRushedGapSeconds)
			: ReportPendingGapSeconds;
		ReportPendingGapSeconds = 0.0f;
		Rearm(Gap);
		return;
	}
	switch (Next)
	{
	case EFirstReportText::Sent:
		PlayFirstReportSent();
		ReportPendingGapSeconds = IGListenerGreybox::ReportReceiptGapSeconds;
		break;
	case EFirstReportText::Receipt:
		PlayFirstReportReceipt();
		ReportPendingGapSeconds = IGListenerGreybox::SiteCheckGapSeconds;
		break;
	case EFirstReportText::SiteCheck:
		PlayFirstReportSiteCheck();
		ReportPendingGapSeconds = IGListenerGreybox::EvictionPostGapSeconds;
		break;
	case EFirstReportText::EvictionPosted:
		// 요구서는 그녀가 403호 안에 있을 때 붙는다. 문 너머로 종이 소리를 듣고
		// 나가 보게 된다. 밖에 나가 있으면 돌아올 때까지 기다린다. 모르는 새
		// 붙어 있으면 그대로 지나치기 쉽다.
		if (Player.IsValid()
			&& !AIGMissingFloorNightFourDirector::CanHearEvictionPosting(
				Player->GetActorLocation()))
		{
			Rearm(IGListenerGreybox::EvictionPostPollSeconds);
			return;
		}
		NightFour->PostEvictionNotice(/*bAnnounce=*/true);
		// 관리실 문 옆의 「바람 소리」 쪽지는 요구서를 붙인 날 떼어 낸다.
		if (PuzzleTwo)
		{
			PuzzleTwo->RefreshBoothNotice();
		}
		break;
	case EFirstReportText::EvictionPhoto:
		// 신고한 사람은 기다리되 손을 놓고 있지 않는다(§1). 종이를 찍어 담당
		// 수사관에게 보낸다. 누수 보수에 석고보드를 올린 것까지.
		Narrative->MarkBeatPlayed(IGListenerGreybox::EvictionPhotoBeat);
		AIGHorrorHUD::PushDialogue(
			this,
			NSLOCTEXT("IGMissingFloor", "ReportSentSpeaker", "보낸 문자"),
			NSLOCTEXT(
				"IGMissingFloor",
				"EvictionPhotoSent",
				"[사진] 관리인이 문에 붙인 종이입니다. 내일 오전 7시에 옥상 누수 공사를 한답니다. 그 전에 5층 벽을 꼭 확인해 주세요."),
			EIGDialogueChannel::Device,
			0.0f,
			EIGDialoguePriority::Story);
		ReportPendingGapSeconds = IGListenerGreybox::EvictionDeadlineGapSeconds;
		break;
	case EFirstReportText::EvictionDeadline:
		// 밤4에 벽을 여는 이유는 이 한 줄이다. 신고는 했고, 증거 인멸이 먼저 온다.
		Narrative->MarkBeatPlayed(IGListenerGreybox::EvictionDeadlineBeat);
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT("IGMissingFloor", "EvictionDeadlineThought", "일곱 시에 벽을 덮으면… 그 전에 와 줄 수 있을까."),
			3.2f);
		break;
	default:
		break;
	}
	// 방금 민 줄이 끝나기를 기다렸다가 다음 차례를 본다.
	Rearm(IGListenerGreybox::ReportLanePollSeconds);
}

void AIGListenerGreyboxDirector::PlayFirstReportSent()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->MarkBeatPlayed(IGListenerGreybox::ReportSentBeat))
	{
		return;
	}
	// 밤새 죽어 있던 폰이다(공동현관 독백). 초자연을 납득시키려 하지 않고
	// 주소와 사람과 들은 것만 적는다(§1).
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT("IGMissingFloor", "FirstReportSignalThought", "신호 잡힌다. 112에 문자부터 보내자."),
		2.6f);
	AIGHorrorHUD::PushDialogue(
		this,
		NSLOCTEXT("IGMissingFloor", "ReportSentSpeaker", "보낸 문자"),
		NSLOCTEXT(
			"IGMissingFloor",
			"FirstReportSent",
			"무영로 27-3 달빛빌라입니다. 옥상에 방이 하나 있어요. 작년 7월에 실종된 오빠 백도하가 살던 곳인데, 벽 안에서 두드리는 소리가 나요. 사람일 수도 있어요. 와 주세요."),
		EIGDialogueChannel::Device,
		0.0f,
		EIGDialoguePriority::Story);
}

void AIGListenerGreyboxDirector::PlayFirstReportReceipt()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->MarkBeatPlayed(IGListenerGreybox::ReportReceiptBeat))
	{
		return;
	}
	PlayPhoneTextBuzz();
	AIGHorrorHUD::PushDialogue(
		this,
		NSLOCTEXT("IGMissingFloor", "ReportSpeaker", "112"),
		NSLOCTEXT(
			"IGMissingFloor",
			"FirstReportReceipt",
			"[112 문자신고] 신고가 접수됐습니다. 담당 경찰관이 확인 후 연락드리겠습니다."),
		EIGDialogueChannel::Device,
		0.0f,
		EIGDialoguePriority::Story);
}

void AIGListenerGreyboxDirector::PlayFirstReportSiteCheck()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->MarkBeatPlayed(IGListenerGreybox::SiteCheckBeat))
	{
		return;
	}
	// §1: 날이 밝은 5층에는 응답도 체온도 없다. 1년 전 실종에 닫힌 벽이라
	// 긴급 개방 근거가 서지 않고, 절차는 영장으로 넘어간다. 경찰을 무능하게
	// 그리지 않는다. 밤의 확신과 낮에 내밀 수 있는 물증 사이의 간격이다.
	PlayPhoneTextBuzz();
	AIGHorrorHUD::PushDialogue(
		this,
		NSLOCTEXT("IGMissingFloor", "SiteCheckSpeaker", "무영경찰서 실종수사팀"),
		NSLOCTEXT(
			"IGMissingFloor",
			"FirstReportSiteCheck",
			"옥탑은 확인했습니다. 지금은 안에서 소리나 움직임이 없어요. 보내 주신 자료로 추가 확인 중입니다. 다시 소리가 나면 바로 연락 주시고, 벽에는 손대지 마세요."),
		EIGDialogueChannel::Device,
		0.0f,
		EIGDialoguePriority::Story);
}

void AIGListenerGreyboxDirector::PlayPhoneTextBuzz()
{
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!PlayerCharacter)
	{
		return;
	}
	UAudioComponent* Buzz = IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Phone_Vibrate"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreatePhoneVibrationUnfinished(this); }),
		PlayerCharacter->GetActorLocation(),
		0.5f,
		1.0f,
		60.0f,
		400.0f,
		EIGAudioBus::Player);
	if (!Buzz)
	{
		return;
	}
	// 주머니 속 폰이다. 걸어가도 몸에 붙어 운다. 그 자리에 두면 복도에 폰이
	// 남아 우는 것처럼 들린다.
	Buzz->AttachToComponent(
		PlayerCharacter->GetRootComponent(),
		FAttachmentTransformRules::KeepWorldTransform);
	// 녹음은 전화가 올 때처럼 1.2초씩 여섯 번, 12초를 운다. 문자는 한 번이면
	// 된다. 첫 떨림(0.45초부터)만 남기고 걷는다. 아침에 바닥의 폰이 떠는
	// 길이와 같다.
	IGAudio::FadeOutAfter(Buzz, 0.9f, 0.25f);
}

void AIGListenerGreyboxDirector::HandleEvictionNoticeRead(
	AIGMissingFloorEvidence* Evidence)
{
	// 종이의 독백(사유와 기한)이 먼저 뜨고, 사진 문자와 마지막 한 줄이 뒤를 잇는다.
	if (!bProbeRequested)
	{
		ScheduleFirstReportTexts(0.3f);
	}
}

void AIGListenerGreyboxDirector::HandleNightFourResolved()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!NightPhase || !Narrative || Narrative->GetNightIndex() != 4)
	{
		return;
	}
	// 성공 엔딩 A/B는 같은 세계의 발견 사실로 돌아온다. C는 신고하거나
	// 새벽에 건물을 여는 대신 시간을 멈추고 밤 4 재시도를 제공하므로
	// 이 완료 경로에서 의도적으로 제외한다.
	Narrative->SetSecondReportMade(true);
	Narrative->MarkBeatPlayed(FName(TEXT("Night4.SecondReport")));
	NightPhase->SuppressNextMorningPresentation();

	if (bProbeRequested)
	{
		// 프로브는 선택한 프레임에 새벽이 와 있는지 본다.
		NightPhase->CompleteNightGoal();
		StartEpilogueAfterGesture();
		return;
	}
	// 건물은 에필로그가 화면을 덮은 뒤에 푼다. 그 전에 풀면 렌치를 놓는
	// 동안 5층 등이 켜지고 낮 목표 줄이 뜬다. 여기서는 시계만 세운다 —
	// 존재는 대치 끝에서 이미 잠들었다.
	NightPhase->SetHourPaused(true);
	// A는 렌치가 제자리에 놓이는 0.85초와 그 독백 한 줄을, B는 연결음과 전화를
	// 받는 딸깍을 카메라가 살아 있을 때 겪어야 한다. 같은 프레임에 에필로그가
	// 화면을 검게 칠하면 선택은 했는데 한 것을 못 본다.
	GetWorldTimerManager().SetTimer(
		EpilogueStartTimer,
		this,
		&AIGListenerGreyboxDirector::StartEpilogueAfterGesture,
		3.4f,
		false);
}

void AIGListenerGreyboxDirector::StartEpilogueAfterGesture()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (Narrative)
	{
		// 다섯째 밤 줄은 이 기록을 본다. 결말 뒤에는 자동 저장을 남기지 않으므로
		// 세이브에서 결말을 읽을 수 없다.
		IGOnboardingMemory::MarkEndingSeen(Narrative->GetEndingChoice());
	}
	if (Epilogue && Narrative)
	{
		Epilogue->StartEpilogue(Player.Get(), Narrative->GetEndingChoice());
	}
	// 새벽은 에필로그의 검은 화면 아래서 온다. 에필로그가 못 열렸어도 밤은
	// 끝내야 한다 — 세워 둔 시계를 풀 사람이 여기밖에 없다. 프로브는 이미
	// 풀고 들어오므로 건너뛴다.
	if (NightPhase && NightPhase->IsHourActive())
	{
		NightPhase->CompleteNightGoal();
	}
}

void AIGListenerGreyboxDirector::HandleEpilogueCompleted()
{
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	APlayerController* Controller = PlayerCharacter
		? Cast<APlayerController>(PlayerCharacter->GetController())
		: nullptr;
	if (!Controller)
	{
		if (UWorld* World = GetWorld())
		{
			Controller = World->GetFirstPlayerController();
		}
	}
	if (AIGPlayerController* IGController = Cast<AIGPlayerController>(Controller))
	{
		// 타이틀이 자기 배경을 그리므로 암전은 여기서 걷는다. 이동 잠금은
		// 남겨 둔다 — 뒤에서 걸어 다니는 사람이 있으면 타이틀이 아니다.
		if (IGController->PlayerCameraManager)
		{
			IGController->PlayerCameraManager->StopCameraFade();
		}
		// 엔딩 카드 다음은 크레딧이다. 크레딧이 끝나면 HUD가 타이틀을 부른다.
		// 무인 검사와 프로브는 곧장 타이틀로 간다.
		AIGHorrorHUD* HorrorHUD = Cast<AIGHorrorHUD>(IGController->GetHUD());
		if (HorrorHUD && !bProbeRequested && !FApp::IsUnattended())
		{
			HorrorHUD->StartEndCredits();
		}
		else
		{
			IGController->ShowTitleAfterEnding();
		}
	}
}

void AIGListenerGreyboxDirector::HandleFifthDawnCompleted()
{
	// 막간은 밤4의 벽 안에서 돈다(§8 막간, 2026-09-10). 여기서는 밤4에
	// 넘겨줄 뿐이다 — 밤3의 잠자리는 보통 밤처럼 밤4로 간다.
	// 화면이 아직 검을 때 자재 위의 렌치를 치운다. 선택이 열리면 렌치는 벽
	// 앞의 한 자루뿐이다.
	if (NightThree)
	{
		NightThree->RefreshTuningHammerAvailability();
	}
	if (NightFour)
	{
		NightFour->HandleInterludeCompleted();
	}
}

void AIGListenerGreyboxDirector::HandleSleepRequested(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!NightPhase || !Narrative || NightPhase->IsHourActive()
		|| GetWorldTimerManager().IsTimerActive(NightStartTimer))
	{
		return;
	}
	if (bProductionMode && Narrative->GetNightIndex() == 0)
	{
		if (!Narrative->HasBeatPlayed(FName(TEXT("Arrival.Complete"))))
		{
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT("IGMissingFloor", "ArrivalSleepBlocked", "아직 볼 게 남았어."),
				2.6f);
			return;
		}
		// 첫 저녁의 생활음은 여기서 끝난다. 밤에 그 자리는 위층이 갖는다.
		StopArrivalBaseline();
		Narrative->MarkBeatPlayed(FName(TEXT("Arrival.Slept")));
		BeginNightAfterSleep(1);
		return;
	}
	// 자동 문자가 없는 것과 낮에 할 일을 마친 것은 다르다. 요구서를 아직
	// 읽지 않았을 때도 문자 줄기는 잠시 멎으므로 취침 조건을 따로 확인한다.
	if (Narrative->GetNightIndex() == 3
		&& Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer))
	{
		if (!IGListenerGreybox::HasCompletedFirstReportDay(*Narrative))
		{
			if (!Narrative->WasFirstReportMade())
			{
				MakeNightThreeFirstReport();
			}
			const IGListenerGreybox::EFirstReportText Next =
				IGListenerGreybox::NextFirstReportText(*Narrative, NightFour.Get());
			if (Next == IGListenerGreybox::EFirstReportText::None)
			{
				AIGHorrorHUD::PushThought(
					this,
					NSLOCTEXT("IGMissingFloor", "EvictionNoticeSleepBlocked", "문에 붙은 종이부터 확인해 보자."),
					2.6f);
			}
			else if (!bProbeRequested && (!bFirstReportRushed
				|| !GetWorldTimerManager().IsTimerActive(ReportTimer)))
			{
				bFirstReportRushed = true;
				ScheduleFirstReportTexts(0.05f);
			}
			return;
		}
		// 마지막 독백의 비트는 화면에 띄울 때 기록된다. 그 문장이 끝나기 전에
		// 침대 암전으로 덮지 않는다. 프로브는 대사의 재생 시간만 생략한다.
		if (!bProbeRequested && IGListenerGreybox::IsDialogueLaneBusy(GetWorld()))
		{
			return;
		}
	}
	if (Narrative->GetNightIndex() == 3
		&& !Narrative->WasFifthDawnInterludeCompleted()
		&& bProbeRequested)
	{
		// 계약 연습. 막간 자체는 밤4의 벽 안에서 돈다(§8 막간). 프로브는
		// 여기서 한 번 돌려 시각표·입력·즉시 종료를 확인하고 완료 표시를
		// 남긴다 — 그래서 밤4의 리빌은 프로브에서 막간을 건너뛴다.
		if (!FifthDawn
			|| !FifthDawn->ValidateTimeline()
			|| !FifthDawn->StartInterlude(Player.Get())
			|| !FifthDawn->RegisterPlayerKnock()
			|| !FifthDawn->SetPlayerListening(true)
			|| !FifthDawn->SetPlayerListening(false)
			|| !FifthDawn->CompleteImmediatelyForProbe())
		{
			FailProbe(TEXT("fifth-dawn start/input/finish contract failed"));
			return;
		}
	}
	const int32 CurrentNight = Narrative->GetNightIndex();
	int32 NextNight = FMath::Clamp(CurrentNight + 1, 1, 4);
	const bool bGoalMet = CurrentNight < 1
		|| Narrative->HasBeatPlayed(AIGNightPhaseDirector::GoalBeatId(CurrentNight));
	if (!bGoalMet)
	{
		// 못 채운 밤은 다음 저녁에 같은 밤이 온다. 그는 한 티어 더 급하다 —
		// 잡히는 값이 죽음이 아니라 시간이려면 시간이 정말로 사라져야 한다(§5.4).
		// 밤4의 05:30은 여기 오지 않는다. 벽이 닫힌 새벽은 엔딩 C가 갖는다.
		NextNight = CurrentNight;
		Narrative->SetAggressionTier(Narrative->GetAggressionTier() + 1);
	}
	BeginNightAfterSleep(NextNight);
}

void AIGListenerGreyboxDirector::BeginNightAfterSleep(const int32 NightIndex)
{
	PendingNightIndex = NightIndex;
	if (bProbeRequested)
	{
		// 프로브는 잠든 프레임에 밤이 서 있어야 한다.
		WakeIntoNight();
		return;
	}
	// 눕는 순간 눈을 감긴다. 눕고 나서도 서 있던 자리에 그대로 서서 카드를
	// 보는 것은 잠이 아니라 로딩이었다. 눈을 뜨면 침대이고 알람이 울린다.
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	APlayerController* Controller = PlayerCharacter
		? Cast<APlayerController>(PlayerCharacter->GetController())
		: nullptr;
	if (Controller && Controller->PlayerCameraManager)
	{
		Controller->PlayerCameraManager->StartCameraFade(
			0.0f,
			1.0f,
			0.6f,
			FLinearColor::Black,
			/*bShouldFadeAudio=*/false,
			/*bHoldWhenFinished=*/true);
	}
	// 눕는 소리 하나. 포획 뒤 침대에서 깰 때와 같은 이불 소리다.
	if (PlayerCharacter)
	{
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateClothSettle(this),
			PlayerCharacter->GetActorLocation(),
			0.5f,
			0.94f,
			75.0f,
			480.0f);
	}
	GetWorldTimerManager().SetTimer(
		NightStartTimer,
		this,
		&AIGListenerGreyboxDirector::WakeIntoNight,
		0.75f,
		false);
}

void AIGListenerGreyboxDirector::WakeIntoNight()
{
	if (!NightPhase)
	{
		return;
	}
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	APlayerController* Controller = PlayerCharacter
		? Cast<APlayerController>(PlayerCharacter->GetController())
		: nullptr;
	if (!bProbeRequested && PlayerCharacter && NightLoop
		&& NightLoop->HasWakeTransform())
	{
		// 포획 뒤에 깨는 자리와 같은 자리다. 밤은 언제나 침대에서 시작한다.
		const FTransform& Wake = NightLoop->GetWakeTransform();
		PlayerCharacter->TeleportTo(
			Wake.GetLocation(), Wake.Rotator(), false, true);
		if (UCharacterMovementComponent* Movement =
			PlayerCharacter->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		if (Controller)
		{
			Controller->SetControlRotation(Wake.Rotator());
		}
	}
	NightPhase->BeginTheHour(PendingNightIndex);
	const FVector At = PlayerCharacter
		? PlayerCharacter->GetActorLocation()
		: GetActorLocation();
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateAlarmFirstNote(this),
		At,
		0.58f,
		0.94f,
		80.0f,
		650.0f,
		EIGAudioBus::Player);
	// 알람은 머리맡의 폰이 운다. 밤마다 같은 자막이다 — 밤1의 노크는 알람과
	// 같이 나지 않고, 카드가 걷힌 뒤 천장에서 제 자막을 따로 단다.
	AIGHorrorHUD::PushAudioCaption(
		this,
		NSLOCTEXT("IGMissingFloor", "NightAlarmCaption", "[04:30 알람]"),
		2.7f);
	if (bProbeRequested)
	{
		return;
	}
	if (Controller && Controller->PlayerCameraManager)
	{
		Controller->PlayerCameraManager->StartCameraFade(
			1.0f,
			0.0f,
			1.4f,
			FLinearColor::Black,
			/*bShouldFadeAudio=*/false,
			/*bHoldWhenFinished=*/false);
	}
	if (PendingNightIndex == 4)
	{
		// 망치는 낮에 샀다(§8 밤4 낮). 벽 앞의 「망치로 두드리기」가 처음 보는
		// 물건을 쥐여 주지 않게, 눈을 뜨는 자리에서 한 번 챙긴다.
		UIGMissingFloorNarrativeSubsystem* HammerNarrative = GetNarrative();
		if (HammerNarrative
			&& HammerNarrative->MarkBeatPlayed(FName(TEXT("Night4.HammerPacked"))))
		{
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT(
					"IGMissingFloor",
					"NightFourHammerThought",
					"낮에 철물점에서 사 온 망치를 챙겼다."),
				4.0f);
		}
		// 벽 앞의 한 줄과 여는 드릴은 HandleHourActiveChanged가 건다. 잠을
		// 거치지 않는 엔딩 C의 재시도도 그 자리를 지난다.
	}
	// 밤은 방향으로 시작하고, 여는 자리가 밤마다 5층 벽 쪽으로 한 칸씩 다가간다.
	// 밤1 침대 위 천장, 밤2 현관문, 밤3 옥상 철문, 밤4 5층 벽(PlayNightOpeningSettle).
	if (PendingNightIndex == 1)
	{
		// 첫 밤은 프롤로그 0-5 그대로다: 카드가 걷히고 나서 천장에서 쿵, 쿵, 쿵.
		// 드르륵. 그리고 「…4층이 꼭대기인데.」 그는 깨는 순간 복도 첫 칸에 서지만
		// 거기서는 두드리지 않는다 — 첫 노크는 천장이 낸다. 천장이 끌리기
		// 시작할 때까지는 첫 칸에 붙들어 두어서, 복도의 노크가 천장 소리와
		// 겹치지 않고 그 뒤에 온다.
		if (Entity)
		{
			Entity->SilenceNextStopKnock();
			Entity->SetBeatHold(true);
		}
		GetWorldTimerManager().SetTimer(
			NightOneHoldTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (Entity)
				{
					Entity->SetBeatHold(false);
				}
			}),
			IGListenerGreybox::NightOneCeilingKnockSeconds
				+ IGListenerGreybox::NightOneDragDelaySeconds,
			false);
		GetWorldTimerManager().SetTimer(
			NightOneCeilingKnockTimer,
			this,
			&AIGListenerGreyboxDirector::PlayNightOneCeilingKnock,
			IGListenerGreybox::NightOneCeilingKnockSeconds,
			false);
		// 손전등은 이삿짐에서 꺼내 머리맡에 둔 채다. 이 게임에서 손전등이 있다는
		// 것을 배우는 자리는 여기 하나라, 한 판에 한 번 어둠 속에서 노크를 듣고
		// 나서야 손이 간다. 되풀이된 밤은 켜진 채로 깬다.
		const UIGMissingFloorNarrativeSubsystem* OpeningNarrative = GetNarrative();
		if (PlayerCharacter && OpeningNarrative
			&& !OpeningNarrative->HasBeatPlayed(FName(TEXT("Night1.TopFloorThought"))))
		{
			if (UIGFlashlightComponent* Torch = PlayerCharacter->GetFlashlight())
			{
				Torch->SetOn(false);
			}
			GetWorldTimerManager().SetTimer(
				NightOneTorchTimer,
				this,
				&AIGListenerGreyboxDirector::ReachForNightOneTorch,
				IGListenerGreybox::NightOneCeilingKnockSeconds
					+ IGListenerGreybox::NightOneTorchReachSeconds,
				false);
		}
		// 밤1, 위에서 누가 일한다. 옥상 탱크 매니폴드에서 전동 드릴이 다섯 번
		// 돌다 멈춘다 — 목한수의 첫 흔적. 밤4의 한 마디를 두 밤의 노동으로 번다.
		GetWorldTimerManager().SetTimer(
			RoofDriverTimer,
			this,
			&AIGListenerGreyboxDirector::PlayRoofDriverBeat,
			42.0f,
			false);
	}
	else if (PendingNightIndex == 2 && NightTwoBeats
		&& NightTwoBeats->GetStage() == EIGNightTwoBeatStage::Opening)
	{
		// 밤2는 여섯 초 뒤 현관문이 연다(NightTwoBeats). 같은 순간 천장이 뒤틀리면
		// 「이번엔 문이다」가 흐려지고, 깨자마자 복도 첫 칸의 3연이 나면 알람과 문
		// 사이의 여섯 초가 비지 않는다. 첫 칸의 노크를 삼키고 붙들어 둔다. 문 앞에
		// 세워지면 NightTwoBeats가 붙들고 놓는다. 무슨 이유로든 안 세워졌으면 푼다.
		if (Entity)
		{
			Entity->SilenceNextStopKnock();
			Entity->SetBeatHold(true);
		}
		GetWorldTimerManager().SetTimer(
			NightTwoDoorHoldTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (Entity && !(NightTwoBeats && NightTwoBeats->IsFigureAtDoor()))
				{
					Entity->SetBeatHold(false);
				}
			}),
			IGListenerGreybox::NightTwoDoorHoldSeconds,
			false);
	}
	else if (PendingNightIndex != 4)
	{
		// 밤3, 그리고 문 비트를 다 쓴 되풀이 밤2. 여는 소리는 밤마다 다르다.
		// 밤4의 드릴은 이미 BeginTheHour의 시간 경계에서 걸렸다.
		GetWorldTimerManager().SetTimer(
			NightSettleTimer,
			this,
			&AIGListenerGreyboxDirector::PlayNightOpeningSettle,
			IGListenerGreybox::NightOpeningSettleSeconds,
			false);
	}
	// 눈을 뜨면 손이 먼저 손전등을 찾는다. BeginTheHour가 불을 켜 두지만 그건 잠든
	// 사이의 일이라, 한 번 내려놓았다가 검은 화면이 걷히기 시작할 즈음 딸깍과 함께
	// 켠다. 알람 첫 음이 끝난 뒤다. 첫 밤의 첫 회차는 위에서 노크를 듣고서야 손이
	// 가므로(ReachForNightOneTorch) 이미 꺼져 있고, 여기서는 아무것도 하지 않는다.
	UIGFlashlightComponent* WakeTorch = PlayerCharacter ? PlayerCharacter->GetFlashlight() : nullptr;
	if (WakeTorch && WakeTorch->IsAvailable() && WakeTorch->IsOn())
	{
		WakeTorch->SetOn(false);
		GetWorldTimerManager().SetTimer(
			NightWakeTorchTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				AIGPlayerCharacter* Holder = Player.Get();
				UIGFlashlightComponent* Light = Holder ? Holder->GetFlashlight() : nullptr;
				if (!NightPhase || !NightPhase->IsHourActive()
					|| !Light || !Light->IsAvailable() || Light->IsOn())
				{
					return;
				}
				Light->SetOn(true);
				IGAudio::SpawnOneShotAt(
					this,
					UIGToneSequenceSoundWave::CreateSwitchClick(this, true),
					Holder->GetActorLocation(),
					0.30f,
					1.0f,
					60.0f,
					320.0f,
					EIGAudioBus::Player);
			}),
			0.35f,
			false);
	}
}

FVector AIGListenerGreyboxDirector::GetNightOneCeilingPoint() const
{
	// 침대 위 천장. 걸어가 볼 수 있는 자리라야 소리가 장소가 된다.
	const FVector Bed = NightLoop && NightLoop->HasWakeTransform()
		? NightLoop->GetWakeTransform().GetLocation()
		: (Player.IsValid() ? Player->GetActorLocation() : GetActorLocation());
	return Bed + FVector(30.0f, 40.0f, 300.0f);
}

void AIGListenerGreyboxDirector::PlayNightOneCeilingKnock()
{
	if (!NightPhase || !NightPhase->IsHourActive()
		|| (NightLoop && NightLoop->IsCaptureResetInFlight()))
	{
		return;
	}
	const FVector Above = GetNightOneCeilingPoint();
	// 벽 너머로 듣는 그 노크다. 슬래브 한 장이 고음을 먹고, 소리는 정수리
	// 위에서 온다(§8 1-1 「첫 노크는 벽 너머 로우패스」).
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Entity_KnockTriple_Muffled"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockTriple(this, 0.78f); }),
		Above,
		0.9f,
		1.0f,
		200.0f,
		1500.0f,
		EIGAudioBus::Entity);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "NightOneCeilingKnockCaption", "천장에서 세 번 두드리는 소리"),
		2.6f,
		Above);
	AIGHorrorHUD::PushFearDirection(this, Above);
	// 몸이 먼저 굳는다. 숨을 들이켜는 것은 끌림 쪽이다.
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->ApplyScare(0.2f);
		}
	}
	GetWorldTimerManager().SetTimer(
		NightOneDragTimer,
		this,
		&AIGListenerGreyboxDirector::PlayNightOneOpeningDrag,
		IGListenerGreybox::NightOneDragDelaySeconds,
		false);
}

void AIGListenerGreyboxDirector::ReachForNightOneTorch()
{
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	UIGFlashlightComponent* Torch = PlayerCharacter
		? PlayerCharacter->GetFlashlight()
		: nullptr;
	// 먼저 켰으면 배울 것이 없다.
	if (!NightPhase || !NightPhase->IsHourActive()
		|| !Torch || !Torch->IsAvailable() || Torch->IsOn())
	{
		return;
	}
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"NightTorchThought",
			"손전등은 머리맡에 꺼내 뒀다."),
		2.4f);
	// 손이 닿는 만큼 늦게 딸깍. 스위치 소리는 F로 켤 때와 같다.
	GetWorldTimerManager().SetTimer(
		NightOneTorchTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			AIGPlayerCharacter* Holder = Player.Get();
			UIGFlashlightComponent* Light = Holder ? Holder->GetFlashlight() : nullptr;
			if (!NightPhase || !NightPhase->IsHourActive()
				|| !Light || !Light->IsAvailable() || Light->IsOn())
			{
				return;
			}
			Light->SetOn(true);
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateSwitchClick(this, true),
				Holder->GetActorLocation(),
				0.34f,
				1.0f,
				60.0f,
				320.0f,
				EIGAudioBus::Player);
		}),
		IGListenerGreybox::NightOneTorchClickSeconds,
		false);
}

void AIGListenerGreyboxDirector::PlayNightOneOpeningDrag()
{
	if (!NightPhase || !NightPhase->IsHourActive()
		|| (NightLoop && NightLoop->IsCaptureResetInFlight()))
	{
		return;
	}
	const FVector Above = GetNightOneCeilingPoint();
	// 끌림은 루프 파형이다. 한 번 긁고 멎게 잘라 낸다 — 놓아두면 밤새 천장에서 긁는다.
	UAudioComponent* Drag = IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateEntityDragLoop(this, /*bVinyl=*/false),
		Above,
		0.58f,
		0.92f,
		200.0f,
		1500.0f,
		EIGAudioBus::Entity);
	if (Drag)
	{
		TWeakObjectPtr<UAudioComponent> WeakDrag(Drag);
		GetWorldTimerManager().SetTimer(
			NightOneDragFadeTimer,
			FTimerDelegate::CreateWeakLambda(this, [WeakDrag]()
			{
				if (UAudioComponent* Loop = WeakDrag.Get())
				{
					Loop->FadeOut(0.45f, 0.0f);
				}
			}),
			IGListenerGreybox::NightOneDragSeconds,
			false);
	}
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "NightOneOpeningDragCaption", "천장에서 뭔가 끄는 소리"),
		2.4f,
		Above);
	AIGHorrorHUD::PushFearDirection(this, Above);
	// 유담이 천장을 본다.
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->ApplyScare(0.22f);
		}
		PlayerCharacter->PlayScareKick(0.6f);
	}
	// 끌림이 멎을 즈음 한 줄. 계약서의 「지상 4층」과 옥상 문을 본 저녁이
	// 여기서 묶인다. 한 판에 한 번이라 되풀이된 밤에는 소리만 남는다.
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (Narrative && !Narrative->HasBeatPlayed(FName(TEXT("Night1.TopFloorThought"))))
	{
		GetWorldTimerManager().SetTimer(
			NightOneThoughtTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UIGMissingFloorNarrativeSubsystem* Story = GetNarrative();
				if (!NightPhase || !NightPhase->IsHourActive() || !Story
					|| (NightLoop && NightLoop->IsCaptureResetInFlight())
					|| !Story->MarkBeatPlayed(FName(TEXT("Night1.TopFloorThought"))))
				{
					return;
				}
				AIGHorrorHUD::PushThought(
					this,
					NSLOCTEXT("IGMissingFloor", "NightOneTopFloorThought", "…4층이 꼭대기인데."),
					3.2f);
			}),
			IGListenerGreybox::NightOneTopFloorThoughtSeconds,
			false);
	}
}

void AIGListenerGreyboxDirector::PlayRoofDriverBeat()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!NightPhase || !NightPhase->IsHourActive() || !Narrative
		|| !Narrative->MarkBeatPlayed(FName(TEXT("Night1.RoofDriver"))))
	{
		return;
	}
	// 옥상 매니폴드(5, 140, 1300) 위. 4층 복도에서 3미터 위다.
	const FVector RoofManifold(5.0f, 140.0f, 1330.0f);
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateCordlessDriverRun(this),
		RoofManifold,
		0.7f,
		1.0f,
		400.0f,
		4200.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "RoofDriverCaption", "전동 드릴 소리, 다섯 번 돌다 멈춘다"),
		3.0f,
		RoofManifold);
}

void AIGListenerGreyboxDirector::PlayNightOpeningSettle()
{
	// 밤1의 천장 노크처럼, 포획 암전이 화면을 쥔 동안에는 밤을 열지 않는다.
	if (!NightPhase || !NightPhase->IsHourActive()
		|| (NightLoop && NightLoop->IsCaptureResetInFlight()))
	{
		return;
	}
	// 밤3은 옥상 철문이, 밤4는 5층 벽의 드릴이 연다. 대답을 받은 뒤의 되풀이
	// 밤3은 복도에 선 그가 열고, 벽이 열린 밤4는 공동 앞의 침묵이 연다.
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const int32 NightIndex = Narrative ? Narrative->GetNightIndex() : 0;
	if (NightIndex == 3)
	{
		if (!Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer))
		{
			PlayNightThreeRoofGate();
		}
		return;
	}
	if (NightIndex == 4)
	{
		if (!Narrative->IsNightFourWallOpened())
		{
			PlayNightFourMokDrill();
		}
		return;
	}
	// 플레이어 기준이 아니라 403호 천장의 정해진 자리다. 걸어가 볼 수 있는
	// 소리라야 장소가 된다.
	const FVector Bed = NightLoop && NightLoop->HasWakeTransform()
		? NightLoop->GetWakeTransform().GetLocation()
		: (Player.IsValid() ? Player->GetActorLocation() : GetActorLocation());
	const FVector Above = Bed + FVector(40.0f, 0.0f, 300.0f);
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateSettleTimberCreak(this),
		Above,
		0.62f,
		1.0f,
		160.0f,
		1400.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "NightOpeningCreak", "나무 삐걱거리는 소리"),
		2.2f,
		Above);
}

void AIGListenerGreyboxDirector::PlayNightThreeRoofGate()
{
	// 밤3의 목적지를 소리가 먼저 가리킨다. 계단 끝의 옥상 철문. 바람이 부풀고
	// 문짝이 틀 안에서 덜컹인다. 그의 소리가 아니라 바람이라 박자가 없다 —
	// P4를 앞둔 귀가 노크로 세지 않는다. 겁주는 자리가 아니라 몸은 건드리지 않는다.
	const FVector Gate = IGListenerGreybox::NightThreeRoofGate;
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateRoofDoorGust(this),
		Gate,
		0.7f,
		1.0f,
		300.0f,
		2000.0f,
		EIGAudioBus::World);
	GetWorldTimerManager().SetTimer(
		NightOpeningFollowTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			// 그새 붙잡혔으면 덜컹을 버린다. 포획의 노크 둘 위로 쇳소리가 얹힌다.
			if (!NightPhase || !NightPhase->IsHourActive()
				|| (NightLoop && NightLoop->IsCaptureResetInFlight()))
			{
				return;
			}
			const FVector RattleAt = IGListenerGreybox::NightThreeRoofGate;
			// 열려 있는 문은 덜컹이지 않는다. 바람만 계단으로 내려온다.
			const AIGSwingDoor* StairGate = NightThree ? NightThree->GetStairGate() : nullptr;
			if (StairGate && StairGate->IsOpen())
			{
				AIGHorrorHUD::PushAudioCaptionAt(
					this,
					NSLOCTEXT("IGMissingFloor", "NightThreeRoofWindCaption", "옥상 문틈으로 바람 드는 소리"),
					2.4f,
					RattleAt);
				return;
			}
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateLockedRattle(this),
				RattleAt,
				0.55f,
				0.92f,
				300.0f,
				2000.0f,
				EIGAudioBus::World);
			AIGHorrorHUD::PushAudioCaptionAt(
				this,
				NSLOCTEXT("IGMissingFloor", "NightThreeRoofGateCaption", "바람에 옥상 철문 덜컹거리는 소리"),
				2.4f,
				RattleAt);
		}),
		IGListenerGreybox::NightThreeRoofRattleSeconds,
		false);
}

void AIGListenerGreyboxDirector::PlayNightFourMokDrill()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!NightPhase || !NightPhase->IsHourActive() || !Narrative
		|| Narrative->GetNightIndex() != 4 || Narrative->IsNightFourWallOpened())
	{
		return;
	}
	// 04:30, 건물이 잠기자마자 목한수가 먼저 벽을 덮는다(§1). 밤1 옥상에서 다섯 번
	// 돌다 멈추던 그 드릴이 이번엔 5층 벽에서 돈다. 자막도 밤1과 글자까지 같아서
	// 같은 소리라는 걸 귀와 눈이 함께 알아본다. 그를 5층으로 끌어올리면 안 되니
	// 소음으로는 알리지 않는다. 밤1 드릴과 같다.
	const FVector Wall = AIGMissingFloorNightFourDirector::GetWallBreakLocation();
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateCordlessDriverRun(this),
		Wall,
		0.85f,
		0.97f,
		500.0f,
		4200.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "NightFourDriverCaption", "전동 드릴 소리, 다섯 번 돌다 멈춘다"),
		3.0f,
		Wall);
	// 드릴이 멎으면 사람이 벽 앞을 떠난다. 곧장 5층으로 올라간 플레이어가 빈
	// 별채를 보고 헛것을 들었다고 여기지 않게, 떠나는 소리까지 낸다.
	NightOpeningStep = 0;
	GetWorldTimerManager().SetTimer(
		NightOpeningFollowTimer,
		this,
		&AIGListenerGreyboxDirector::PlayNightFourMokLeaving,
		IGListenerGreybox::MokStepSpacingSeconds,
		true,
		IGListenerGreybox::MokLeavesAfterDrillSeconds);
	// 한 판에 한 번. 되풀이된 밤4에는 소리만 남는다. 「일곱 시」는 요구서를 본
	// 회차만 안다. 안 읽고 잠든 회차에도 소리만 남는다.
	if (!Narrative->HasBeatPlayed(IGListenerGreybox::NightFourDrillBeat)
		&& IGListenerGreybox::KnowsEvictionDeadline(*Narrative))
	{
		GetWorldTimerManager().SetTimer(
			NightOpeningThoughtTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				// 포획 암전이 덮었으면 버린다. 침대에서 깨자마자 드릴 이야기를 하게
				// 된다. 비트를 찍지 않으니 다음에 밤4가 열릴 때 다시 온다.
				UIGMissingFloorNarrativeSubsystem* Story = GetNarrative();
				if (!NightPhase || !NightPhase->IsHourActive() || !Story
					|| (NightLoop && NightLoop->IsCaptureResetInFlight())
					|| !IGListenerGreybox::KnowsEvictionDeadline(*Story)
					|| !Story->MarkBeatPlayed(IGListenerGreybox::NightFourDrillBeat))
				{
					return;
				}
				// 요구서의 「내일 07:00」과 지금 시각만 말한다. 누가 무엇을 하는지는
				// 방금 들은 소리가 말했다.
				AIGHorrorHUD::PushThought(
					this,
					NSLOCTEXT(
						"IGMissingFloor",
						"NightFourDrillThought",
						"일곱 시라더니, 지금 네 시 반인데."),
					3.4f);
			}),
			IGListenerGreybox::NightFourDrillThoughtSeconds,
			false);
	}
}

void AIGListenerGreyboxDirector::PlayNightFourMokLeaving()
{
	// 새벽이 왔거나 포획 암전이 덮었으면 남은 걸음과 철문을 버린다. 붙잡힌 순간의
	// 노크 둘 위로 발소리가 얹히면 누구의 소리인지 흐려진다.
	if (!NightPhase || !NightPhase->IsHourActive()
		|| (NightLoop && NightLoop->IsCaptureResetInFlight()))
	{
		GetWorldTimerManager().ClearTimer(NightOpeningFollowTimer);
		return;
	}
	const int32 Step = NightOpeningStep++;
	if (Step < IGListenerGreybox::MokStepCount)
	{
		// 구두가 석고 부스러기를 밟으며 철문 쪽으로 간다. 걸음마다 작아진다.
		const FVector StepAt = FMath::Lerp(
			IGListenerGreybox::MokStepsFrom,
			IGListenerGreybox::MokStepsTo,
			(Step + 1) / static_cast<float>(IGListenerGreybox::MokStepCount));
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateSurfaceFootstep(
				this,
				EIGFootstepSurface::GypsumDebris,
				0.92f,
				0.6f - 0.1f * Step),
			StepAt,
			0.55f - 0.08f * Step,
			1.0f,
			200.0f,
			1800.0f,
			EIGAudioBus::World);
		if (Step == 0)
		{
			AIGHorrorHUD::PushAudioCaptionAt(
				this,
				NSLOCTEXT("IGMissingFloor", "NightFourMokStepsCaption", "철문 쪽으로 가는 발소리"),
				2.2f,
				StepAt);
		}
		return;
	}
	GetWorldTimerManager().ClearTimer(NightOpeningFollowTimer);
	// 5층 철문이 닫힌다. 열어 둔 문이면 닫히는 소리를 내지 않는다.
	const AIGSwingDoor* AnnexGate = NightThree ? NightThree->GetAnnexGate() : nullptr;
	if (AnnexGate && AnnexGate->IsOpen())
	{
		return;
	}
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Door_Steel_Close"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateDoorThud(this); }),
		IGListenerGreybox::MokAnnexDoor,
		0.45f,
		0.9f,
		200.0f,
		2000.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "NightFourAnnexDoorCaption", "5층 철문 닫히는 소리"),
		1.8f,
		IGListenerGreybox::MokAnnexDoor);
}

void AIGListenerGreyboxDirector::HandleSealedEntranceTried(AIGSwingDoor* Door)
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!NightPhase || !NightPhase->IsHourActive() || !Narrative
		|| Narrative->HasBeatPlayed(IGListenerGreybox::NoSignalBeat)
		|| GetWorldTimerManager().IsTimerActive(NoSignalTimer))
	{
		return;
	}
	// 밤2에는 폰이 403호 현관 바닥에서 녹음 중이다. 손에 없는 폰은 걸 수 없다.
	if (PuzzleTwo && PuzzleTwo->IsPhoneInUse())
	{
		return;
	}
	// 밀리다 서는 문 앞에서 폰을 꺼낸다(§1 규칙 1, §8 1-2). 한 판에 한 번.
	// 독백을 읽을 틈을 두고 통화 실패음이 먼저, 「폰도 안 터진다.」가 뒤에 온다.
	if (bProbeRequested)
	{
		PlayNoSignal();
		return;
	}
	GetWorldTimerManager().SetTimer(
		NoSignalTimer,
		this,
		&AIGListenerGreyboxDirector::PlayNoSignal,
		IGListenerGreybox::NoSignalDelaySeconds,
		false);
}

void AIGListenerGreyboxDirector::PlayNoSignal()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!NightPhase || !NightPhase->IsHourActive() || !Narrative || !PlayerCharacter
		|| !Narrative->MarkBeatPlayed(IGListenerGreybox::NoSignalBeat))
	{
		return;
	}
	// 손에 든 폰의 소리다. 문을 흔들 때 이미 소음으로 알렸으니 더 알리지 않는다.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateCallFailTone(this),
		PlayerCharacter->GetActorLocation(),
		0.5f,
		1.0f,
		60.0f,
		400.0f,
		EIGAudioBus::Player);
	AIGHorrorHUD::PushAudioCaption(
		this,
		NSLOCTEXT("IGMissingFloor", "NoSignalCaption", "[휴대폰 통화 실패음]"),
		2.2f);
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT("IGMissingFloor", "NoSignalThought", "폰도 안 터지네."),
		2.6f);
}

void AIGListenerGreyboxDirector::PollHwangPeek()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || (NightPhase && NightPhase->IsHourActive())
		|| Narrative->HasBeatPlayed(IGListenerGreybox::HwangPeekBeat))
	{
		GetWorldTimerManager().ClearTimer(HwangPeekTimer);
		return;
	}
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	const APlayerController* Controller = PlayerCharacter
		? Cast<APlayerController>(PlayerCharacter->GetController())
		: nullptr;
	if (!Controller || AIGReadableNote::GetOpenNote() != nullptr)
	{
		return;
	}
	// 4층 복도에서 문을 지나쳐 등진 채일 때만. 보고 있으면 문짝이 안 움직이는 게
	// 보인다.
	const FVector At = PlayerCharacter->GetActorLocation();
	const FVector Door = IGListenerGreybox::HwangPeekDoor;
	if (At.Z < IGListenerGreybox::FourthFloorZ + 50.0f
		|| At.Z > IGListenerGreybox::FourthFloorZ + 200.0f
		|| At.Y > Door.Y - 10.0f)
	{
		return;
	}
	const float Distance = FVector::Dist2D(At, Door);
	if (Distance < IGListenerGreybox::HwangPeekMinDistance
		|| Distance > IGListenerGreybox::HwangPeekMaxDistance
		|| IsFacingHwangDoor())
	{
		return;
	}
	// 다른 말이 떠 있으면 기다린다. 겹치면 누가 한 말인지 흐려진다.
	const AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD());
	if (Hud && !Hud->IsDialogueLaneIdle())
	{
		return;
	}
	if (!Narrative->MarkBeatPlayed(IGListenerGreybox::HwangPeekBeat))
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(HwangPeekTimer);
	PlayHwangPeek();
}

void AIGListenerGreyboxDirector::PlayHwangPeek()
{
	// 걸쇠가 돌고 철문이 한 뼘 열린다. 밤새 복도에서 난 소리를 다 들은 사람이
	// 있다는 첫 기척이다. 놀래는 자리가 아니라 스트레스는 건드리지 않는다.
	const FVector Door = IGListenerGreybox::HwangPeekDoor;
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Lock_Open"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSwitchClick(this, false); }),
		Door,
		0.3f,
		1.05f,
		90.0f,
		1100.0f,
		EIGAudioBus::World);
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Door_Steel_Open"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateDoorCreak(this, false); }),
		Door,
		0.32f,
		0.92f,
		90.0f,
		1100.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "Hwang401PeekCaption", "401호 문 살짝 열리는 소리"),
		2.2f,
		Door);
	// 소리와 자막을 들으면 돌아보는 게 보통이다. 문짝은 씬에 박힌 채라 그 뒤는
	// 시선을 보며 잇는다(TickHwangPeekScene).
	const UWorld* World = GetWorld();
	HwangPeekStartedAt = World ? World->GetTimeSeconds() : 0.0;
	bHwangPeekLineShown = false;
	GetWorldTimerManager().SetTimer(
		HwangPeekSceneTimer,
		this,
		&AIGListenerGreyboxDirector::TickHwangPeekScene,
		IGListenerGreybox::HwangPeekScenePollSeconds,
		true);
}

void AIGListenerGreyboxDirector::TickHwangPeekScene()
{
	const UWorld* World = GetWorld();
	if (!World || (NightPhase && NightPhase->IsHourActive()))
	{
		GetWorldTimerManager().ClearTimer(HwangPeekSceneTimer);
		return;
	}
	const double Elapsed = World->GetTimeSeconds() - HwangPeekStartedAt;
	// 문 쪽으로 고개가 돌았다. 문틈의 사람이 그걸 보고 문을 당긴다. 할 말은
	// 닫으면서 한다 — 밤새 누가 듣고 있었다는 것은 남아야 한다. 열리는 소리가
	// 다 나기 전에는 닫지 않는다. 겹치면 연 소리인지 닫은 소리인지 흐려진다.
	const bool bTurnedToDoor = Elapsed >= IGListenerGreybox::HwangPeekMinOpenSeconds
		&& IsFacingHwangDoor();
	if (!bHwangPeekLineShown
		&& (bTurnedToDoor || Elapsed >= IGListenerGreybox::HwangPeekLineSeconds))
	{
		bHwangPeekLineShown = true;
		AIGHorrorHUD::PushDialogue(
			this,
			NSLOCTEXT("IGMissingFloor", "HwangSpeaker", "황순금"),
			NSLOCTEXT("IGMissingFloor", "Hwang401Peek", "새벽에 나와 있었죠? 들어가서 눈 좀 붙여요."),
			EIGDialogueChannel::Conversation,
			0.0f,
			EIGDialoguePriority::Story);
	}
	if (!bTurnedToDoor && Elapsed < IGListenerGreybox::HwangPeekCloseSeconds)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(HwangPeekSceneTimer);
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Door_Steel_Close"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateDoorThud(this); }),
		IGListenerGreybox::HwangPeekDoor,
		0.26f,
		0.95f,
		90.0f,
		1100.0f,
		EIGAudioBus::World);
}

bool AIGListenerGreyboxDirector::IsFacingHwangDoor() const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	const APlayerController* Controller = PlayerCharacter
		? Cast<APlayerController>(PlayerCharacter->GetController())
		: nullptr;
	if (!Controller)
	{
		return false;
	}
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ToDoor = (IGListenerGreybox::HwangPeekDoor - ViewLocation).GetSafeNormal2D();
	return FVector::DotProduct(ViewRotation.Vector().GetSafeNormal2D(), ToDoor)
		>= IGListenerGreybox::HwangPeekFacingDot;
}

FText AIGListenerGreyboxDirector::GetHwangDoorLine() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return NSLOCTEXT(
			"IGMissingFloor",
			"Hwang401Greeting",
			"403호 새로 온 아가씨죠? 밤엔 문 좀 살살 닫아 줘요. 여기 소리가 잘 울려서.");
	}
	// §7 난이도 보정의 낮 1단계. 퍼즐마다 한 줄씩, 답이 아니라 어디를 볼지만.
	// 가장 앞선 상태부터 본다 — 뒤의 밤에 앞의 밤 힌트를 되풀이하지 않도록.
	const int32 NightIndex = Narrative->GetNightIndex();
	const bool bAlive = Narrative->HasTruth(EIGMissingFloorTruth::WasStillAlive);
	const bool bInWall = Narrative->HasTruth(EIGMissingFloorTruth::SomeoneInTheWall);
	const bool bAnswered = Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer);
	// T7은 P2를 푸는 순간 같이 서고(PuzzleTwo::HandleTruthConfirmed), 밤3은 P2
	// 없이 오지 않는다. 그래서 대답(T9)을 들었는데 T7이 없는 낮은 없다.
	if (bAnswered && !Narrative->IsNightFourWallOpened())
	{
		// 절차는 모른다. 30년 산 사람이 몸으로 아는 것만 말한다.
		return NSLOCTEXT(
			"IGMissingFloor",
			"Hwang401HintP5",
			"내일 아침에 또 벽을 막는대요. 물탱크 청소할 땐 배관 소리가 어찌나 큰지, 벽을 쳐도 안 들리던데.");
	}
	if (bInWall && !bAnswered)
	{
		// 7월 29일, 그녀가 벽에 대답한 날(§2). 일지의 같은 날과 같은 이야기다.
		return NSLOCTEXT(
			"IGMissingFloor",
			"Hwang401HintP4",
			"벽이 하도 울길래 나도 똑같이 두드려 줬어요. 그랬더니 그날 밤은 조용하더라고요.");
	}
	if (!bInWall && NightIndex >= 2
		&& Narrative->IsPuzzleSolved(FName(TEXT("P2"))))
	{
		return NSLOCTEXT(
			"IGMissingFloor",
			"Hwang401HintP3",
			"관리인 없어요? 열쇠는 늘 책상 위에 두던데. 못 들어가게 하면 나한테 말해요.");
	}
	if (bAlive)
	{
		// 위의 갈래가 T7 뒤의 낮을 다 가져간다. 여기는 벽이 열린 뒤에만 닿는다.
		return NSLOCTEXT(
			"IGMissingFloor",
			"Hwang401AfterT7",
			"그날도 들었어요. 사람이 없다는데, 난 분명히 들었다니까요.");
	}
	// 밤1을 채운 다음 낮(NightIndex 1)부터 관리실 대장을 가리킨다. P3·P5 힌트가
	// 다음 밤을 앞서 가리키는데 P2만 한 칸 늦어서, 밤2를 한 번 실패한 뒤에야
	// 관리실로 갈 이유가 생겼다. 관리실은 낮에 잠겨 있으니 밤의 일이 된다.
	if (NightIndex >= 2
		|| (NightIndex == 1 && Narrative->HasTruth(EIGMissingFloorTruth::LivedUpstairs)))
	{
		return NSLOCTEXT(
			"IGMissingFloor",
			"Hwang401HintP2",
			"몇 번을 전화했는데 맨날 배관 소리래요. 장부에 적기는 했나 좀 봐 줘요.");
	}
	if (NightIndex == 1
		&& !Narrative->HasTruth(EIGMissingFloorTruth::LivedUpstairs))
	{
		return NSLOCTEXT(
			"IGMissingFloor",
			"Hwang401HintP1",
			"전기 나갔으면 1층 계량기함 봐요. 검침표도 거기 붙어 있어요.");
	}
	if (NightIndex >= 1)
	{
		return NSLOCTEXT(
			"IGMissingFloor",
			"Hwang401Neutral",
			"잠은 좀 잤어요? 물이라도 한 잔 마시고 가요.");
	}
	return NSLOCTEXT(
		"IGMissingFloor",
		"Hwang401Greeting",
		"403호 새로 온 아가씨죠? 밤엔 문 좀 살살 닫아 줘요. 여기 소리가 잘 울려서.");
}

void AIGListenerGreyboxDirector::HandleUnit401Knocked(
	AIGMissingFloorEvidence* Evidence)
{
	// 401호 철문을 손등으로 가볍게 두 번. 그녀는 문 너머로 답한다 — 퍼즐이
	// 기대는 낮의 힌트 창구다(§7 난이도 보정). 석고벽의 「대답 둘」(0.42초)은
	// 벽 속의 서명음이라 여기서 쓰지 않는다. 문에서 먼저 배우면 닳는다.
	const FVector DoorAt = Evidence ? Evidence->GetActorLocation() : GetActorLocation();
	const uint32 KnockSeed = static_cast<uint32>(GetWorld()->GetTimeSeconds() * 977.0f);
	auto KnockOnce = [this, DoorAt](const uint32 Seed, const float Volume, const float Pitch)
	{
		IGAudio::SpawnOneShotAt(
			this,
			IGAudio::SampleVariantOr(TEXT("Knock_Steel"), 3, Seed, [this]() -> USoundBase*
			{
				return UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.0f);
			}),
			DoorAt,
			Volume,
			Pitch,
			120.0f,
			1000.0f,
			EIGAudioBus::Player);
	};
	KnockOnce(KnockSeed, 0.55f, 1.0f);
	if (!IsScriptedRun())
	{
		GetWorldTimerManager().SetTimer(
			Unit401KnockTimer,
			FTimerDelegate::CreateWeakLambda(this, [KnockOnce, KnockSeed]()
			{
				KnockOnce(KnockSeed + 1u, 0.5f, 0.97f);
			}),
			0.24f,
			false);
	}
	// 문 안의 라디오가 멎고, 대답이 다 끝나면 다시 켜진다.
	if (Unit401PrayerLoop && Unit401PrayerLoop->IsPlaying())
	{
		GetWorldTimerManager().ClearTimer(Unit401RadioFadeTimer);
		FadeUnit401Prayer(0.0f, 0.8f);
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "Arrival401PrayerStops", "안에서 들리던 불경 소리가 멈춘다"),
			2.2f,
			IGListenerGreybox::Unit401PrayerLocation);
		GetWorldTimerManager().SetTimer(
			Unit401PrayerReturnTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				const APlayerController* Controller =
					GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
				const AIGHorrorHUD* Hud =
					Controller ? Cast<AIGHorrorHUD>(Controller->GetHUD()) : nullptr;
				// 신고 뒤의 대화가 줄이 비기를 기다리는 중이면 그 대화가 먼저다.
				if ((Hud && !Hud->IsDialogueLaneIdle())
					|| GetWorldTimerManager().IsTimerActive(HwangPermissionTimer))
				{
					return;
				}
				GetWorldTimerManager().ClearTimer(Unit401PrayerReturnTimer);
				FadeUnit401Prayer(GetUnit401PrayerRestLevel(), 3.0f);
			}),
			0.5f,
			true,
			2.5f);
	}

	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const FText Speaker =
		NSLOCTEXT("IGMissingFloor", "HwangSpeaker", "황순금");
	// 힌트 창구이기 전에 7월 29일에 벽에 대답한 사람이다(§3.5). 한 번씩만 오는
	// 대화 둘을 평소 줄 앞에 세운다. 대사 큐는 차례로 띄우지만 자막 배율이 크면
	// 네 번째 줄은 줄을 서다 버려진다. 한 번에 세 줄을 넘기지 않는다.
	const bool bAnswered = Narrative
		&& Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer);
	if (bAnswered
		&& !Narrative->IsNightFourWallOpened()
		&& Narrative->WasFirstReportMade()
		&& !Narrative->HasBeatPlayed(IGListenerGreybox::HwangPermissionBeat))
	{
		// §8 밤4 낮. 신고를 마친 유담이 벽의 대답을 전한다(TryPlayHwangPermission).
		// 새벽 독백이나 신고 문자가 떠 있으면 문 앞에서 줄이 비기를 기다린다.
		// 그동안 라디오는 멎은 채다.
		if (!TryPlayHwangPermission())
		{
			const double AskedAt = GetWorld()->GetTimeSeconds();
			GetWorldTimerManager().SetTimer(
				HwangPermissionTimer,
				FTimerDelegate::CreateWeakLambda(this, [this, DoorAt, AskedAt]()
				{
					const UWorld* World = GetWorld();
					const UIGMissingFloorNarrativeSubsystem* Story = GetNarrative();
					const AIGPlayerCharacter* Knocker = Player.Get();
					// 밤이 됐거나, 문 앞을 떠났거나, 너무 오래 걸리면 접는다. 비트를
					// 찍지 않았으니 다음 노크가 다시 받는다.
					if (!World || !Story || !Knocker
						|| (NightPhase && NightPhase->IsHourActive())
						|| Story->HasBeatPlayed(IGListenerGreybox::HwangPermissionBeat)
						|| World->GetTimeSeconds() - AskedAt
							> IGListenerGreybox::HwangPermissionMaxWaitSeconds
						|| FVector::Dist2D(Knocker->GetActorLocation(), DoorAt)
							> IGListenerGreybox::HwangPermissionReach
						|| FMath::Abs(Knocker->GetActorLocation().Z - DoorAt.Z) > 150.0f)
					{
						GetWorldTimerManager().ClearTimer(HwangPermissionTimer);
						return;
					}
					TryPlayHwangPermission();
				}),
				IGListenerGreybox::HwangPermissionPollSeconds,
				true);
		}
	}
	else
	{
		// 일지가 문에 걸린 뒤 처음 두드리면 자기가 적은 것부터 말한다.
		const AIGReadableNote* Journal = NightThree ? NightThree->GetJournalNote() : nullptr;
		if (Narrative && !bAnswered
			&& Narrative->HasTruth(EIGMissingFloorTruth::WasStillAlive)
			&& Journal && !Journal->IsHidden()
			&& Narrative->MarkBeatPlayed(FName(TEXT("Day.Hwang.Journal"))))
		{
			AIGHorrorHUD::PushDialogue(
				this,
				Speaker,
				NSLOCTEXT(
					"IGMissingFloor",
					"Hwang401JournalGiven",
					"문고리에 걸어 둔 거 읽어 봐요. 사람이 없다는데, 난 분명히 들었다니까요."),
				EIGDialogueChannel::Conversation,
				0.0f,
				EIGDialoguePriority::Story);
		}
		AIGHorrorHUD::PushDialogue(
			this,
			Speaker,
			GetHwangDoorLine(),
			EIGDialogueChannel::Conversation,
			0.0f,
			EIGDialoguePriority::Story);
		if (Narrative && Narrative->GetNightIndex() >= 1
			&& Narrative->GetNightIndex() <= 3)
		{
			Narrative->MarkBeatPlayed(AIGNightPhaseDirector::DayConversationBeatId(
				Narrative->GetNightIndex()));
		}
	}

	if (bProductionMode && Narrative && Narrative->GetNightIndex() == 0
		&& Narrative->MarkBeatPlayed(FName(TEXT("Arrival.Unit401"))))
	{
		RequestArrivalAutosave();
		UpdateArrivalSequence();
	}
}

bool AIGListenerGreyboxDirector::TryPlayHwangPermission()
{
	// 112 문자가 먼저 나가고 나서다. 신고를 마친 유담이 전하는 말이라 새벽에
	// 곧장 두드려도 신고 문자보다 앞서지 않는다. 다른 말이 떠 있으면 세 줄을
	// 그 뒤에 세우지 않는다 — 셋째 줄이 기다리다 버려지면 비트만 남는다.
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative
		|| !Narrative->HasBeatPlayed(IGListenerGreybox::ReportSentBeat)
		|| IGListenerGreybox::IsDialogueLaneBusy(GetWorld())
		|| !Narrative->MarkBeatPlayed(IGListenerGreybox::HwangPermissionBeat))
	{
		return false;
	}
	GetWorldTimerManager().ClearTimer(HwangPermissionTimer);
	Narrative->MarkBeatPlayed(AIGNightPhaseDirector::DayConversationBeatId(
		Narrative->GetNightIndex()));
	const FText Speaker =
		NSLOCTEXT("IGMissingFloor", "HwangSpeaker", "황순금");
	// 물값 한 줄이 공용 설비를 여는 허락이다. P5 힌트는 이번에는 건너뛰고 다음
	// 노크부터 준다.
	AIGHorrorHUD::PushDialogue(
		this,
		NSLOCTEXT("IGMissingFloor", "YudamCounterSpeaker", "백유담"),
		NSLOCTEXT(
			"IGMissingFloor",
			"YudamToHwangAnswer",
			"벽 안에서 대답이 왔어요. 둘, 쉬고, 하나. 오빠가 문 두드리던 박자예요."),
		EIGDialogueChannel::Conversation,
		0.0f,
		EIGDialoguePriority::Story);
	AIGHorrorHUD::PushDialogue(
		this,
		Speaker,
		NSLOCTEXT("IGMissingFloor", "Hwang401IsItYourBrother", "그게 아가씨 오빠였어요?"),
		EIGDialogueChannel::Conversation,
		0.0f,
		EIGDialoguePriority::Story);
	AIGHorrorHUD::PushDialogue(
		this,
		Speaker,
		NSLOCTEXT("IGMissingFloor", "Hwang401Permission", "꺼내 줘요. 물 필요하면 써요. 그깟 물값, 내가 낼 테니까."),
		EIGDialogueChannel::Conversation,
		0.0f,
		EIGDialoguePriority::Story);
	return true;
}

void AIGListenerGreyboxDirector::RefreshTablePhone()
{
	AIGReadableNote* Listing = UsedListingNote.Get();
	if (!Listing)
	{
		return;
	}
	// 그녀의 폰은 한 대다. 밤2의 현관 바닥이나 재생하는 손에 있을 때, 그 시간
	// 내내(머리맡의 알람, 공동현관에서 꺼내 드는 폰), 밤4 벽 앞에서 녹음을 켜
	// 둔 동안에는 탁자에 없다. 녹음은 그 시간 안에서만 켜지고 꺼지므로 시간
	// 경계에서 다시 보면 된다.
	const UWorld* World = GetWorld();
	const UIGRecordingSubsystem* Recording = World
		? World->GetSubsystem<UIGRecordingSubsystem>()
		: nullptr;
	const bool bAway = (PuzzleTwo && PuzzleTwo->IsPhoneInUse())
		|| (NightPhase && NightPhase->IsHourActive())
		|| (Recording && Recording->IsRecording());
	Listing->SetActorHiddenInGame(bAway);
	Listing->SetActorEnableCollision(!bAway);
	Listing->SetInteractionEnabled(!bAway);
}

void AIGListenerGreyboxDirector::PollNightFourWallCover()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !NightPhase || !NightPhase->IsHourActive()
		|| Narrative->GetNightIndex() != 4
		|| Narrative->IsNightFourMaskRunning()
		|| Narrative->IsNightFourWallOpened())
	{
		GetWorldTimerManager().ClearTimer(NightFourWallCoverTimer);
		return;
	}
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!PlayerCharacter)
	{
		return;
	}
	// 5층 벽 칸 앞. 아래층에서 같은 X·Y를 지나가도 걸리지 않게 높이도 본다.
	const FVector FromWall = PlayerCharacter->GetActorLocation()
		- AIGMissingFloorNightFourDirector::GetWallBreakLocation();
	if (FromWall.Size2D() >= 230.0f || FMath::Abs(FromWall.Z) >= 150.0f)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(NightFourWallCoverTimer);
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"NightFourWallNeedsCover",
			"지금 치면 건물 사람들이 다 듣겠어. 소리를 덮을 게 필요해."),
		3.5f);
}

// -- probe -----------------------------------------------------------------

void AIGListenerGreyboxDirector::MeasureCctvFeed(
	const AIGCctvChannelFive* Channel)
{
	// Read the channel's own render target rather than a screenshot of the
	// monitor. It proves the thing that can actually fail — that the scene
	// capture rendered the annex — without depending on where the probe's camera
	// happens to be pointing, and it is the only reading available for a beat
	// that plays once.
	UTextureRenderTarget2D* Target = Channel
		? Channel->GetFeedForTesting()
		: nullptr;
	if (!Target)
	{
		return;
	}
	TArray<FColor> Samples;
	if (!UKismetRenderingLibrary::ReadRenderTarget(this, Target, Samples, false)
		|| Samples.Num() == 0)
	{
		return;
	}

	// Rec. 709 luma, and "lit" is any pixel above a twentieth — the annex has one
	// bulb, so most of this frame is legitimately dark and an average would hide
	// a black capture behind a correctly dark one.
	int32 LitPixels = 0;
	float Brightest = 0.0f;
	for (const FColor& Sample : Samples)
	{
		const float Luma =
			(0.2126f * Sample.R + 0.7152f * Sample.G + 0.0722f * Sample.B)
			/ 255.0f;
		Brightest = FMath::Max(Brightest, Luma);
		LitPixels += Luma > 0.05f ? 1 : 0;
	}
	CctvFeedBrightestLuma = Brightest;
	CctvFeedLitFraction =
		static_cast<float>(LitPixels) / static_cast<float>(Samples.Num());
	bCctvFeedMeasured = true;

	// Numbers say the frame is not black; they cannot say whether the shot reads.
	// The frame is written out so the composition can be judged by eye later
	// without re-running an engine for a beat that plays once.
	UKismetRenderingLibrary::ExportRenderTarget(
		this,
		Target,
		FPaths::ProjectDir() / TEXT("Docs/Media"),
		TEXT("cctv5-feed.png"));
}

void AIGListenerGreyboxDirector::StartProbe()
{
	ProbeStep = EProbeStep::AudioVisualContract;
	StepDeadlineSeconds = 0.0f;
	GetWorldTimerManager().SetTimer(
		ProbeTimer,
		this,
		&AIGListenerGreyboxDirector::AdvanceProbe,
		IGListenerGreybox::ProbePollSeconds,
		true);
}

void AIGListenerGreyboxDirector::AdvanceProbe()
{
	if (!Entity || !NightLoop || !NoiseSubsystem)
	{
		FailProbe(TEXT("stage actors disappeared mid-probe"));
		return;
	}
	StepDeadlineSeconds += IGListenerGreybox::ProbePollSeconds;

	switch (ProbeStep)
	{
	case EProbeStep::AudioVisualContract:
	{
		UIGMissingFloorAudioSubsystem* AudioDirector =
			GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>();
		FString Failure;
		if (!AudioDirector || !AudioDirector->ValidateContract(Failure))
		{
			FailProbe(FString::Printf(
				TEXT("M6 audio graph invalid: %s"),
				AudioDirector ? *Failure : TEXT("subsystem missing")));
			return;
		}
		const bool bVoiceCapsMatch =
			AudioDirector->GetVoiceCap(EIGAudioBus::Entity) == 4
			&& AudioDirector->GetVoiceCap(EIGAudioBus::Player) == 6
			&& AudioDirector->GetVoiceCap(EIGAudioBus::Puzzle) == 6
			&& AudioDirector->GetVoiceCap(EIGAudioBus::World) == 12;
		const bool bTitleWindowMatches =
			UIGMissingFloorAudioSubsystem::IsTitleReplyTime(
				FDateTime(2026, 8, 11, 4, 30))
			&& UIGMissingFloorAudioSubsystem::IsTitleReplyTime(
				FDateTime(2026, 8, 11, 5, 30))
			&& !UIGMissingFloorAudioSubsystem::IsTitleReplyTime(
				FDateTime(2026, 8, 11, 5, 31));
		if (!bVoiceCapsMatch || !bTitleWindowMatches)
		{
			FailProbe(TEXT("M6 voice caps or title reply window drifted"));
			return;
		}
		AudioDirector->SetAuthoredSilence(true);
		const bool bSilenceMixMatches = FMath::IsNearlyEqual(
			AudioDirector->GetEffectiveBusDecibels(EIGAudioBus::Score),
			-96.0f,
			0.01f)
			&& FMath::IsNearlyEqual(
				AudioDirector->GetEffectiveBusDecibels(EIGAudioBus::World),
				-24.0f,
				0.01f);
		AudioDirector->SetAuthoredSilence(false);
		AudioDirector->SetPlayerListening(true);
		const bool bListeningDuckMatches = FMath::IsNearlyEqual(
			AudioDirector->GetEffectiveBusDecibels(EIGAudioBus::World),
			-14.0f,
			0.01f);
		AudioDirector->SetPlayerListening(false);
		AudioDirector->SetEntityDistance(500.0f);
		const bool bNearDuckMatches = FMath::IsNearlyEqual(
			AudioDirector->GetEffectiveBusDecibels(EIGAudioBus::Player),
			-7.0f,
			0.01f);
		AudioDirector->SetEntityDistance(MAX_flt);
		if (!bSilenceMixMatches || !bListeningDuckMatches || !bNearDuckMatches)
		{
			FailProbe(TEXT("M6 silence or dynamic ducking values drifted"));
			return;
		}
		ProbeStep = EProbeStep::DifficultyContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::DifficultyContract:
	{
		// §20.2 is a table of authored numbers, so the only honest test is to
		// resolve it and compare. A static text assertion can read the literals
		// but cannot prove the three axes multiply in the right order.
		const float ExpectedSensitivity[] = {1.0f, 1100.0f / 900.0f, 1100.0f / 900.0f, 1300.0f / 900.0f};
		const float ExpectedListen[] = {8.0f, 8.0f, 7.0f, 6.0f};
		const int32 ExpectedNodes[] = {6, 11, 14, 18};
		const float ExpectedChase[] = {330.0f, 462.0f, 462.0f, 506.0f};
		const float ExpectedHold[] = {6.0f, 6.0f, 5.0f, 5.0f};
		const float ExpectedHeat[] = {0.0f, 0.3f, 0.5f, 0.7f};
		const bool ExpectedAmbush[] = {false, false, true, true};
		for (int32 Night = 1; Night <= 4; ++Night)
		{
			const FIGListenerTuning Base = IGListenerTuning::Resolve(
				Night,
				EIGNightDifficulty::Standard,
				0);
			const int32 Index = Night - 1;
			const bool bRowMatches =
				FMath::IsNearlyEqual(Base.HearingSensitivity, ExpectedSensitivity[Index], 0.001f)
				&& FMath::IsNearlyEqual(Base.ListenWindowSeconds, ExpectedListen[Index], 0.001f)
				&& Base.PatrolNodeCount == ExpectedNodes[Index]
				&& FMath::IsNearlyEqual(Base.ChaseSpeed, ExpectedChase[Index], 0.01f)
				&& FMath::IsNearlyEqual(Base.InvestigateHoldSeconds, ExpectedHold[Index], 0.001f)
				&& FMath::IsNearlyEqual(Base.HeatmapWeight, ExpectedHeat[Index], 0.001f)
				&& Base.bTierThreeAmbushAllowed == ExpectedAmbush[Index]
				&& Base.bCaptureEnabled
				&& Base.bChaseEnabled;
			if (!bRowMatches)
			{
				FailProbe(FString::Printf(
					TEXT("§20.2 tuning row for night %d drifted"),
					Night));
				return;
			}
		}

		// The tier is a separate axis that multiplies the night value, not a
		// replacement for it: night 3 at tier 3 is 7 × 0.5, not 4.
		const FIGListenerTuning Night3Tier3 = IGListenerTuning::Resolve(
			3,
			EIGNightDifficulty::Standard,
			3);
		if (!FMath::IsNearlyEqual(Night3Tier3.ListenWindowSeconds, 3.5f, 0.001f))
		{
			FailProbe(FString::Printf(
				TEXT("tier axis is not multiplying the night value: %.2fs"),
				Night3Tier3.ListenWindowSeconds));
			return;
		}

		const FIGListenerTuning Quiet = IGListenerTuning::Resolve(
			4,
			EIGNightDifficulty::Quiet,
			0);
		const FIGListenerTuning Hasty = IGListenerTuning::Resolve(
			2,
			EIGNightDifficulty::Hasty,
			0);
		const FIGListenerTuning ListenOnly = IGListenerTuning::Resolve(
			4,
			EIGNightDifficulty::ListenOnly,
			3);
		const bool bQuietMatches =
			FMath::IsNearlyEqual(Quiet.HearingSensitivity, (1300.0f / 900.0f) * 0.75f, 0.001f)
			&& FMath::IsNearlyEqual(Quiet.ChaseSpeed, 506.0f * 0.8f, 0.01f)
			&& FMath::IsNearlyEqual(Quiet.WaitScale, 1.5f, 0.001f);
		const bool bHastyMatches =
			FMath::IsNearlyEqual(Hasty.ListenWindowSeconds, 7.0f, 0.001f)
			&& FMath::IsNearlyEqual(Hasty.HeatmapWeight, 0.5f, 0.001f);
		const bool bListenOnlyMatches =
			!ListenOnly.bChaseEnabled
			&& !ListenOnly.bCaptureEnabled
			&& !ListenOnly.bTierThreeAmbushAllowed;
		if (!bQuietMatches || !bHastyMatches || !bListenOnlyMatches)
		{
			FailProbe(TEXT("§20.4 difficulty modifiers drifted"));
			return;
		}

		// §5.6 is pure statistics, so it has to be reproducible: same reports in,
		// same hottest zone out, halved by a night, gone on reset.
		UIGNoiseSubsystem* Noise = NoiseSubsystem;
		if (!Noise)
		{
			FailProbe(TEXT("noise subsystem missing for the heatmap contract"));
			return;
		}
		Noise->ResetHeatmap();
		const float PreviousMasking = Noise->GetGlobalMasking();
		Noise->SetGlobalMasking(0.0f);
		const FVector ColdSpot = ProbeNoiseLocation + FVector(4000.0f, 0.0f, 0.0f);
		const FVector WarmSpot = ProbeNoiseLocation;
		const FVector HotSpot =
			ProbeNoiseLocation + FVector(UIGNoiseSubsystem::HeatZoneSize * 3.0f, 0.0f, 0.0f);
		Noise->ReportNoise(WarmSpot, 0.30f, nullptr);
		// Deliberately past the saturation point: a zone the player has been loud
		// in fifty times must not out-weigh one they were loud in fifteen times,
		// or the ambush would chase an outlier instead of a habit.
		const int32 SaturatingReports = FMath::CeilToInt(
			UIGNoiseSubsystem::HeatSaturation / 0.5f) + 2;
		for (int32 Repeat = 0; Repeat < SaturatingReports; ++Repeat)
		{
			Noise->ReportNoise(HotSpot, 0.50f, nullptr);
		}
		const float WarmHeat = Noise->GetHeatAt(WarmSpot);
		const float HotHeat = Noise->GetHeatAt(HotSpot);
		const float ColdHeat = Noise->GetHeatAt(ColdSpot);
		FVector HottestCenter = FVector::ZeroVector;
		float HottestHeat = 0.0f;
		const bool bFoundHottest =
			Noise->GetHottestZone(HottestCenter, HottestHeat);
		// The hottest zone must be the one that was hammered, not the first one
		// touched, or an ambush would sit where the player merely walked once.
		const bool bHottestIsHot = bFoundHottest
			&& FVector::Dist2D(HottestCenter, HotSpot)
				< UIGNoiseSubsystem::HeatZoneSize;
		const bool bSaturates = HotHeat >= 0.99f;
		const int32 ZonesBeforeDecay = Noise->GetHeatZoneCount();
		Noise->DecayHeatmapForNewNight();
		const float DecayedHot = Noise->GetHeatAt(HotSpot);
		Noise->ResetHeatmap();
		const int32 ZonesAfterReset = Noise->GetHeatZoneCount();
		Noise->SetGlobalMasking(PreviousMasking);
		if (ColdHeat > 0.0f || WarmHeat <= 0.0f || HotHeat <= WarmHeat
			|| !bHottestIsHot || !bSaturates
			|| ZonesBeforeDecay != 2 || ZonesAfterReset != 0
			|| !FMath::IsNearlyEqual(DecayedHot, HotHeat * 0.5f, 0.01f))
		{
			FailProbe(FString::Printf(
				TEXT("§5.6 heatmap drifted: cold=%.2f warm=%.2f hot=%.2f "
					"decayed=%.2f zones=%d reset=%d hottest=%d"),
				ColdHeat,
				WarmHeat,
				HotHeat,
				DecayedHot,
				ZonesBeforeDecay,
				ZonesAfterReset,
				bHottestIsHot ? 1 : 0));
			return;
		}

		// And the entity honours the mode it was given, not just the table.
		AIGListenerEntity* EntityActor = Entity.Get();
		if (!EntityActor)
		{
			FailProbe(TEXT("entity missing for the difficulty contract"));
			return;
		}
		const EIGNightDifficulty RestoreDifficulty =
			EntityActor->GetDifficulty();
		const int32 BeforeTier = EntityActor->GetAggressionTier();
		const float BeforeWindow = EntityActor->GetTuning().ListenWindowSeconds;
		EntityActor->SetDifficulty(EIGNightDifficulty::Hasty);
		EntityActor->SetDifficulty(RestoreDifficulty);
		if (EntityActor->GetAggressionTier() != BeforeTier
			|| !FMath::IsNearlyEqual(EntityActor->GetTuning().ListenWindowSeconds, BeforeWindow))
		{
			FailProbe(TEXT("난이도를 바꿨다가 되돌리면 적의 반응 단계가 달라짐"));
			return;
		}
		EntityActor->SetDifficultyForTesting(EIGNightDifficulty::ListenOnly);
		const bool bEntityListenOnly =
			!EntityActor->GetTuning().bCaptureEnabled
			&& !EntityActor->GetTuning().bChaseEnabled;
		EntityActor->SetDifficultyForTesting(RestoreDifficulty);
		const bool bEntityRestored =
			EntityActor->GetTuning().bCaptureEnabled
			&& EntityActor->GetDifficulty() == RestoreDifficulty;
		if (!bEntityListenOnly || !bEntityRestored)
		{
			FailProbe(TEXT("the entity does not follow §20.4 at runtime"));
			return;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_DIFFICULTY PASS: §20.2 four nights, "
				"tier axis multiplies (night3 tier3 = %.2fs), "
				"quiet/hasty/listen-only honoured, heatmap saturates and halves"),
			Night3Tier3.ListenWindowSeconds);

		ProbeStep = EProbeStep::PerceptionContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::PerceptionContract:
	{
		UIGMissingFloorAudioSubsystem* AudioDirector =
			GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>();
		UIGDustSubsystem* Dust = GetWorld()->GetSubsystem<UIGDustSubsystem>();
		AIGPlayerCharacter* PlayerCharacter = Player.Get();
		UIGFlashlightComponent* Torch = PlayerCharacter
			? PlayerCharacter->GetFlashlight()
			: nullptr;
		if (!AudioDirector || !Dust || !Torch)
		{
			FailProbe(TEXT("perception subsystems or the torch are missing"));
			return;
		}

		// §10.4: only stair treads are the stairwell, only the roof slab is
		// outside, and everything else in the building is corridor.
		const bool bSpaceMapMatches =
			UIGMissingFloorAudioSubsystem::ClassifyAcousticSpace(
				EIGFootstepSurface::MetalStair) == EIGAcousticSpace::Stairwell
			&& UIGMissingFloorAudioSubsystem::ClassifyAcousticSpace(
				EIGFootstepSurface::Rooftop) == EIGAcousticSpace::Open
			&& UIGMissingFloorAudioSubsystem::ClassifyAcousticSpace(
				EIGFootstepSurface::Concrete) == EIGAcousticSpace::Corridor
			&& UIGMissingFloorAudioSubsystem::ClassifyAcousticSpace(
				EIGFootstepSurface::Vinyl) == EIGAcousticSpace::Corridor
			&& UIGMissingFloorAudioSubsystem::ClassifyAcousticSpace(
				EIGFootstepSurface::GypsumDebris) == EIGAcousticSpace::Corridor;
		AudioDirector->SetAcousticSpace(EIGAcousticSpace::Stairwell);
		const bool bStairwellApplied =
			AudioDirector->GetAcousticSpace() == EIGAcousticSpace::Stairwell;
		AudioDirector->SetAcousticSpace(EIGAcousticSpace::Open);
		const bool bOpenIsDry =
			AudioDirector->GetAcousticSpace() == EIGAcousticSpace::Open
			&& AudioDirector->GetAcousticPreset(EIGAcousticSpace::Open) == nullptr;
		AudioDirector->SetAcousticSpace(EIGAcousticSpace::Corridor);
		const bool bPresetsBuilt =
			AudioDirector->GetAcousticPreset(EIGAcousticSpace::Corridor) != nullptr
			&& AudioDirector->GetAcousticPreset(EIGAcousticSpace::Stairwell)
				!= nullptr;
		if (!bSpaceMapMatches || !bStairwellApplied || !bOpenIsDry
			|| !bPresetsBuilt)
		{
			FailProbe(TEXT("§10.4 acoustic space routing drifted"));
			return;
		}

		// §11 V1: one stir doubles the air where it happened, changes nothing a
		// room away, and merges rather than piling up along a slow crawl.
		Dust->ClearDisturbances();
		const FVector Stir = ProbeNoiseLocation;
		const bool bStartsClean = Dust->GetLiveDisturbanceCount() == 0
			&& FMath::IsNearlyEqual(
				Dust->GetDensityMultiplierAt(Stir), 1.0f, 0.001f);
		Dust->ReportDisturbance(Stir, 1.0f);
		const bool bDoublesAtStir = FMath::IsNearlyEqual(
			Dust->GetDensityMultiplierAt(Stir),
			UIGDustSubsystem::MaxDensityMultiplier,
			0.02f);
		const bool bOrdinaryAwayFromStir = FMath::IsNearlyEqual(
			Dust->GetDensityMultiplierAt(
				Stir + FVector(UIGDustSubsystem::DisturbanceRadius * 2.0f, 0, 0)),
			1.0f,
			0.001f);
		Dust->ReportDisturbance(
			Stir + FVector(UIGDustSubsystem::MergeDistance * 0.5f, 0.0f, 0.0f),
			1.0f);
		const bool bMergesNearby = Dust->GetLiveDisturbanceCount() == 1;
		Dust->ReportDisturbance(Stir + FVector(320.0f, 0.0f, 0.0f), 1.0f);
		const bool bKeepsSeparateLane = Dust->GetLiveDisturbanceCount() == 2;
		Dust->ClearDisturbances();
		const bool bResetForgets = Dust->GetLiveDisturbanceCount() == 0;
		if (!bStartsClean || !bDoublesAtStir || !bOrdinaryAwayFromStir
			|| !bMergesNearby || !bKeepsSeparateLane || !bResetForgets)
		{
			FailProbe(TEXT("§11 V1 airborne dust model drifted"));
			return;
		}

		// §7 P3 is decided by one audible fact: 속이 찬 벽은 짧게 죽고, 빈 벽은
		// 길게 운다. Render both answers and compare the energy left in the last
		// third of each. A thought bubble claiming the difference while the two
		// walls sound alike would be the puzzle failing silently, and no static
		// assertion can catch that — only the samples can.
		// Both answers are measured over the same absolute window — half a second
		// to one second after the ear lands. A ratio of the two would be
		// meaningless here: the solid wall has stopped producing samples by then,
		// so the denominator is zero and any ratio reads as a fake number. What
		// matters is a fact in two parts. At half a second the cavity must still
		// be plainly audible, and the solid wall must already be gone.
		constexpr int32 SampleRateHz = 48000;
		constexpr int32 WindowStartSample = SampleRateHz / 2;
		constexpr int32 WindowEndSample = SampleRateHz;
		// About -44 dBFS: quiet, but unmistakably a note rather than a floor.
		constexpr float AudibleFloor = 200.0f;
		float HollowLevel = 0.0f;
		float SolidLevel = 0.0f;
		float HollowLength = 0.0f;
		float SolidLength = 0.0f;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bHollow = Pass == 0;
			UIGToneSequenceSoundWave* Response =
				UIGToneSequenceSoundWave::CreateWallCavityResponse(this, bHollow);
			if (!Response)
			{
				FailProbe(TEXT("wall cavity response failed to synthesize"));
				return;
			}
			const float Length = Response->GetConfiguredDurationSeconds();
			TArray<uint8> Pcm;
			Response->OnGeneratePCMAudio(Pcm, WindowEndSample);
			const int32 SampleCount =
				Pcm.Num() / static_cast<int32>(sizeof(int16));
			const int16* Samples =
				reinterpret_cast<const int16*>(Pcm.GetData());
			double WindowSum = 0.0;
			int32 WindowSamples = 0;
			for (int32 Index = WindowStartSample; Index < SampleCount; ++Index)
			{
				WindowSum += FMath::Abs(static_cast<double>(Samples[Index]));
				++WindowSamples;
			}
			// A wave that ended before the window contributes no samples, which
			// is itself the answer: it is silent there.
			const float Level = WindowSamples > 0
				? static_cast<float>(WindowSum / WindowSamples)
				: 0.0f;
			if (bHollow)
			{
				HollowLevel = Level;
				HollowLength = Length;
			}
			else
			{
				SolidLevel = Level;
				SolidLength = Length;
			}
		}
		if (HollowLevel < AudibleFloor)
		{
			FailProbe(FString::Printf(
				TEXT("cavity is not still ringing at half a second: level=%.1f"),
				HollowLevel));
			return;
		}
		if (SolidLevel >= AudibleFloor)
		{
			FailProbe(FString::Printf(
				TEXT("solid wall has not died by half a second: level=%.1f"),
				SolidLevel));
			return;
		}
		if (SolidLength > 0.40f || HollowLength < 1.40f)
		{
			FailProbe(FString::Printf(
				TEXT("wall ring lengths drifted: hollow=%.2fs solid=%.2fs"),
				HollowLength,
				SolidLength));
			return;
		}
		ProbeHollowRingLevel = HollowLevel;
		ProbeHollowRingSeconds = HollowLength;
		ProbeSolidRingSeconds = SolidLength;

		// Arm the beam over a fresh lane and let it tick once before asserting.
		const UCameraComponent* Camera = PlayerCharacter->GetFirstPersonCamera();
		const FVector CameraLocation = Camera
			? Camera->GetComponentLocation()
			: PlayerCharacter->GetActorLocation();
		const FVector CameraForward = Camera
			? Camera->GetForwardVector()
			: PlayerCharacter->GetActorForwardVector();
		ProbeDustTrailLocation = CameraLocation + CameraForward * 260.0f;
		Dust->ReportDisturbance(ProbeDustTrailLocation, 1.0f);
		// 밤 시작부터 켜져 있을 수 있다. 새 빔 검사 전에 이전 앵커를 비운다.
		Torch->SetOn(false);
		Torch->SetAvailable(true);
		Torch->SetOn(true);
		ProbeStep = EProbeStep::BeamDustContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::BeamDustContract:
	{
		AIGPlayerCharacter* PlayerCharacter = Player.Get();
		UIGFlashlightComponent* Torch = PlayerCharacter
			? PlayerCharacter->GetFlashlight()
			: nullptr;
		UIGBeamDustComponent* BeamDust = Torch ? Torch->GetBeamDust() : nullptr;
		UIGDustSubsystem* Dust = GetWorld()->GetSubsystem<UIGDustSubsystem>();
		if (!Torch || !BeamDust || !Dust)
		{
			FailProbe(TEXT("beam dust component is missing from the torch"));
			return;
		}
		if (!Torch->IsOn())
		{
			FailProbe(TEXT("the torch would not stay lit for the dust contract"));
			return;
		}

		// Ordinary air already carries a cloud; his lane carries twice as much,
		// and the extra motes are anchored to the lane rather than sprinkled.
		const int32 LitMotes = BeamDust->GetActiveMoteCount();
		const float LaneDensity = BeamDust->GetBeamDensityMultiplier();
		if (LitMotes < UIGBeamDustComponent::BaseMoteCount)
		{
			FailProbe(FString::Printf(
				TEXT("lit beam carries only %d motes"),
				LitMotes));
			return;
		}
		if (LaneDensity < 1.5f
			|| LitMotes < UIGBeamDustComponent::BaseMoteCount
				+ UIGBeamDustComponent::TrailMoteCount / 2)
		{
			FailProbe(FString::Printf(
				TEXT("his lane did not thicken the beam: x%.2f, %d motes"),
				LaneDensity,
				LitMotes));
			return;
		}

		Torch->SetOn(false);
		if (BeamDust->GetActiveMoteCount() != 0)
		{
			FailProbe(TEXT("motes survived the torch going out"));
			return;
		}
		// §11 V2. 개수뿐 아니라 실제 인스턴스의 위치·회전과 층별 필터를 검사한다.
		const AIGPrologueWorldScene* SceneActor = WorldScene.Get();
		UIGSettledDustComponent* DustField = SceneActor
			? SceneActor->FindComponentByClass<UIGSettledDustComponent>()
			: nullptr;
		if (!DustField || !DustField->IsFieldReady())
		{
			FailProbe(TEXT("the fifth-floor settled dust field is missing"));
			return;
		}
		Dust->ClearSettledPrints();
		const FVector InsideField(0.0f, 700.0f, 1200.0f);
		const FVector OutsideField(190.0f, -305.0f, 900.0f);
		Dust->ReportSettledPrint(InsideField, 0.0f, EIGDustPrintKind::Footfall);
		Dust->ReportSettledPrint(
			InsideField + FVector(120.0f, 0.0f, 0.0f),
			90.0f,
			EIGDustPrintKind::Drag);
		Dust->ReportSettledPrint(OutsideField, 0.0f, EIGDustPrintKind::Footfall);
		Dust->ReportSettledPrint(
			InsideField - FVector(0, 0, 300), 0.0f, EIGDustPrintKind::Footfall);
		DustField->TickComponent(0.25f, LEVELTICK_All, nullptr);
		if (DustField->GetDrawnFootfallCount() != 1 || DustField->GetDrawnDragCount() != 1)
		{
			FailProbe(TEXT("먼지 영역 밖이나 다른 층의 흔적이 5층에 표시됨"));
			return;
		}
		// 같은 자국을 옮겨도 개수는 그대로다. 화면의 위치와 방향은 갱신돼야 한다.
		const FVector MovedPrint = InsideField
			+ FVector(UIGDustSubsystem::PrintMergeDistance * 0.4f, 0, 0);
		Dust->ReportSettledPrint(
			MovedPrint,
			12.0f,
			EIGDustPrintKind::Footfall);
		DustField->TickComponent(0.25f, LEVELTICK_All, nullptr);
		TArray<UInstancedStaticMeshComponent*> DustLayers;
		SceneActor->GetComponents(DustLayers);
		bool bMovedPrintDrawn = false;
		for (UInstancedStaticMeshComponent* Layer : DustLayers)
		{
			if (Layer->GetFName() != FName(TEXT("SettledDustFootfalls")))
			{
				continue;
			}
			FTransform RenderedPrint;
			bMovedPrintDrawn = Layer->GetInstanceCount() == 1
				&& Layer->GetInstanceTransform(0, RenderedPrint, true)
				&& RenderedPrint.GetLocation().Equals(
					MovedPrint + FVector(0, 0, UIGSettledDustComponent::SurfaceOffset), 0.01f)
				&& FMath::IsNearlyEqual(RenderedPrint.Rotator().Yaw, 12.0f, 0.01f);
			break;
		}
		const int32 ReportedPrints = Dust->GetSettledPrintCount();
		if (ReportedPrints != 4 || !bMovedPrintDrawn)
		{
			FailProbe(TEXT("같은 개수에서 바뀐 발자국의 위치나 방향이 화면에 반영되지 않음"));
			return;
		}
		// 배열이 꽉 찬 뒤 오래된 자국을 교체해도 새 발자국을 그려야 한다.
		for (int32 PrintIndex = 0; PrintIndex < UIGDustSubsystem::MaxSettledPrints; ++PrintIndex)
		{
			Dust->ReportSettledPrint(
				FVector(2000.0f + 40.0f * PrintIndex, -305.0f, 1200.0f),
				0.0f, EIGDustPrintKind::Footfall);
		}
		DustField->TickComponent(0.25f, LEVELTICK_All, nullptr);
		const bool bEvictedMarksGone = DustField->GetDrawnFootfallCount() == 0
			&& DustField->GetDrawnDragCount() == 0;
		Dust->ReportSettledPrint(InsideField, 0.0f, EIGDustPrintKind::Footfall);
		DustField->TickComponent(0.25f, LEVELTICK_All, nullptr);
		const bool bFullPoolUpdated = Dust->GetSettledPrintCount() == UIGDustSubsystem::MaxSettledPrints
			&& DustField->GetDrawnFootfallCount() == 1;
		Dust->ClearSettledPrints();
		DustField->TickComponent(0.25f, LEVELTICK_All, nullptr);
		const int32 PrintsAfterReset = Dust->GetSettledPrintCount();
		if (!bEvictedMarksGone || !bFullPoolUpdated || PrintsAfterReset != 0
			|| DustField->GetDrawnFootfallCount() != 0 || DustField->GetDrawnDragCount() != 0)
		{
			FailProbe(TEXT("가득 찬 흔적 배열의 교체 또는 초기화가 화면에 반영되지 않음"));
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_DUST PASS movement rotation floor_filter capacity reset"));

		// §11 V2 403호 3단계 노화: cumulative, and clean in the prologue.
		AIGPrologueWorldScene* MutableScene = WorldScene.Get();
		if (!MutableScene)
		{
			FailProbe(TEXT("world scene missing for the aging contract"));
			return;
		}
		const int32 RestoreAgeStage = MutableScene->GetUnit403AgeStage();
		MutableScene->SetUnit403AgeStage(0);
		const int32 CleanPlanes = MutableScene->GetUnit403AgingPlaneCount();
		MutableScene->SetUnit403AgeStage(1);
		const int32 StageOnePlanes = MutableScene->GetUnit403AgingPlaneCount();
		MutableScene->SetUnit403AgeStage(2);
		const int32 StageTwoPlanes = MutableScene->GetUnit403AgingPlaneCount();
		MutableScene->SetUnit403AgeStage(RestoreAgeStage);
		if (CleanPlanes != 0 || StageOnePlanes <= 0
			|| StageTwoPlanes <= StageOnePlanes)
		{
			FailProbe(FString::Printf(
				TEXT("403 aging is not cumulative: %d/%d/%d planes"),
				CleanPlanes,
				StageOnePlanes,
				StageTwoPlanes));
			return;
		}

		Torch->SetAvailable(false);
		Dust->ClearDisturbances();

		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_PERCEPTION PASS: corridor/stairwell reverb, "
				"cavity still ringing at 0.5s (level=%.0f, %.2fs vs solid %.2fs), "
				"settled dust bounded and 403 aging cumulative, "
				"dust x%.2f over his lane, %d/%d motes lit, dry when dark"),
			ProbeHollowRingLevel,
			ProbeHollowRingSeconds,
			ProbeSolidRingSeconds,
			LaneDensity,
			LitMotes,
			UIGBeamDustComponent::MaxMoteCount);

		ProbeStep = EProbeStep::PuzzleOneContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::PuzzleOneContract:
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (!PuzzleOne || !Narrative)
		{
			FailProbe(TEXT("P1 director or narrative subsystem is missing"));
			return;
		}
		if (!PuzzleOne->ValidateFixtures())
		{
			FailProbe(TEXT("P1 fixtures were not all placed"));
			return;
		}
		// 회로가 꺼졌을 때 멈추고, 켜졌을 때 수평 원판만 도는지 확인한다.
		const FVector MeterProbeSavedLocation = Player->GetActorLocation();
		Player->SetActorLocation(FVector(505, -300, 98));
		UStaticMeshComponent* Rotor = WorldScene->GetFifthMeterDisc();
		const FRotator RotorBefore = Rotor->GetRelativeRotation();
		WorldScene->AdvanceUtilityMeters(12.f, false, true);
		const bool bOffStopped = RotorBefore.Equals(Rotor->GetRelativeRotation(), .01f);
		WorldScene->AdvanceUtilityMeters(12.f, true, true);
		const FRotator RotorAfter = Rotor->GetRelativeRotation();
		const bool bNearRate = FMath::IsNearlyEqual(WorldScene->GetUtilityMeterUpdateInterval(), 1.f/60.f);
		Player->SetActorLocation(FVector(505, -300, 1098));
		WorldScene->AdvanceUtilityMeters(12.f, true, true);
		const bool bFarStopped = RotorAfter.Equals(Rotor->GetRelativeRotation(), .01f);
		const bool bFarRate = FMath::IsNearlyEqual(WorldScene->GetUtilityMeterUpdateInterval(), .25f);
		Player->SetActorLocation(FVector(505, -300, 98));
		WorldScene->AdvanceUtilityMeters(0.f, false, true);
		const bool bFarPhaseKept = Rotor->GetRelativeRotation().Equals(RotorAfter + FRotator(0,12.f*1.44f,0), .01f);
		Player->SetActorLocation(MeterProbeSavedLocation);
		if (!bOffStopped || !bFarStopped || !bNearRate || !bFarRate || !bFarPhaseKept || RotorBefore.Equals(RotorAfter, .01f)
			|| FMath::Abs(Rotor->GetUpVector().Z) < .99f
			|| PuzzleOne->GetMeterAction()->GetInteractionHoldDuration_Implementation(Player.Get()) < 1.f)
		{
			FailProbe(TEXT("P1 rotor power, horizontal axis, distance or observation hold failed")); return;
		}
		UE_LOG(LogTemp, Display, TEXT("UTILITY_METER PASS stopped_off=1 rotates_on=1 horizontal=1 distance_cull=1 observation_hold=1"));
		// 공용 회로를 분리하고 전원 전후를 비교해야 계량기를 증거로 남긴다.
		const FIGMissingFloorNarrativeSnapshot BeforeExperiment = Narrative->GetSnapshot();
		const FTransform CommonSwitchBefore = WorldScene->GetCommonBreakerToggle()->GetRelativeTransform();
		const FTransform UnnamedSwitchBefore = WorldScene->GetUnnamedBreakerToggle()->GetRelativeTransform();
		FIGInteractionContext Experiment;
		Experiment.Interactor = Player.Get();
		PuzzleOne->SetHourActive(false);
		PuzzleOne->GetCommonLightAction()->CompleteInteraction_Implementation(Experiment);
		WorldScene->SetFixtureLive(0, true, true);
		if (!WorldScene->AreCommonInspectionLightsOff()) { FailProbe(TEXT("낮의 공용 전원 차단 또는 발광 상태 불일치")); return; }
		PuzzleOne->GetCommonLightAction()->CompleteInteraction_Implementation(Experiment);
		if (WorldScene->AreCommonInspectionLightsOff()) { FailProbe(TEXT("낮의 공용 전원 복구 실패")); return; }
		UE_LOG(LogTemp, Display, TEXT("CIRCUIT_POWER PASS daylight=1 emissive=1 override_guard=1 near60=1 far4=1 phase_kept=1"));
		PuzzleOne->SetHourActive(true);
		PuzzleOne->GetMeterAction()->CompleteInteraction_Implementation(Experiment);
		if (Narrative->HasSource(EIGMissingFloorTruth::LivedUpstairs, EIGMissingFloorSource::MeterFifthDial))
		{
			FailProbe(TEXT("P1 filed meter evidence without separating the common circuit")); return;
		}
		PuzzleOne->GetCommonLightAction()->CompleteInteraction_Implementation(Experiment);
		PuzzleOne->GetMeterAction()->CompleteInteraction_Implementation(Experiment);
		PuzzleOne->GetBreakerAction()->CompleteInteraction_Implementation(Experiment);
		if (!WorldScene->GetCommonBreakerToggle()->GetRelativeLocation().Equals(CommonSwitchBefore.GetLocation(), .01f)
			|| !WorldScene->GetUnnamedBreakerToggle()->GetRelativeLocation().Equals(UnnamedSwitchBefore.GetLocation(), .01f)
			|| WorldScene->GetCommonBreakerToggle()->GetRelativeRotation().Equals(CommonSwitchBefore.Rotator(), .1f)
			|| WorldScene->GetUnnamedBreakerToggle()->GetRelativeRotation().Equals(UnnamedSwitchBefore.Rotator(), .1f)
			|| WorldScene->GetFifthMeterDisc()->Mobility != EComponentMobility::Movable)
		{
			FailProbe(TEXT("P1 switch or meter presentation cannot move")); return;
		}
		if (Narrative->HasSource(EIGMissingFloorTruth::LivedUpstairs, EIGMissingFloorSource::MeterFifthDial))
		{
			FailProbe(TEXT("P1 switch alone completed the experiment")); return;
		}
		PuzzleOne->GetMeterAction()->CompleteInteraction_Implementation(Experiment);
		if (!Narrative->HasSource(EIGMissingFloorTruth::LivedUpstairs, EIGMissingFloorSource::MeterFifthDial)
			|| Narrative->HasTruth(EIGMissingFloorTruth::LivedUpstairs))
		{
			FailProbe(TEXT("P1 requires both observations and an independent reading sheet")); return;
		}
		PuzzleOne->GetBreakerAction()->CompleteInteraction_Implementation(Experiment);
		PuzzleOne->GetCommonLightAction()->CompleteInteraction_Implementation(Experiment);
		PuzzleOne->SetHourActive(false);
		Narrative->RestoreSnapshot(BeforeExperiment);
		UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_P1_EXPERIMENT PASS isolation=1 off_on=1 reversible=1"));
		// 기록 하나만으로는 결론을 내릴 수 없다.
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::LivedUpstairs,
			EIGMissingFloorSource::MeterFifthDial);
		if (Narrative->HasTruth(EIGMissingFloorTruth::LivedUpstairs))
		{
			FailProbe(TEXT("T1 confirmed from a single evidence record"));
			return;
		}
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::LivedUpstairs,
			EIGMissingFloorSource::MeterReadingSheet);
		if (!Narrative->HasTruth(EIGMissingFloorTruth::LivedUpstairs))
		{
			FailProbe(TEXT("T1 did not confirm after crossing both records"));
			return;
		}
		// The final choice must not open on an unrelated truth.
		if (Narrative->IsFinalChoiceUnlocked())
		{
			FailProbe(TEXT("final choice unlocked without T6/T7/T9"));
			return;
		}
		// Confirmation is derived, never latched: dropping the records must
		// drop the truth with them.
		FIGMissingFloorNarrativeSnapshot Stripped = Narrative->GetSnapshot();
		for (FIGMissingFloorTruthRecord& Record : Stripped.Truths)
		{
			Record.SourceIds.Reset();
		}
		Narrative->RestoreSnapshot(Stripped);
		if (Narrative->HasTruth(EIGMissingFloorTruth::LivedUpstairs))
		{
			FailProbe(TEXT("T1 survived a snapshot with no evidence records"));
			return;
		}
		ProbeStep = EProbeStep::SealContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::SealContract:
	{
		const AIGPrologueWorldScene* SealedScene = WorldScene.Get();
		if (!SealedScene || !NightPhase)
		{
			FailProbe(TEXT("night phase did not arm with the stage"));
			return;
		}
		// The hour must be holding, the shutter must exist, and the release
		// tag must actually be registered — an unregistered tag would leave
		// the sealed entrance silently openable.
		if (!NightPhase->IsHourActive() || !SealedScene->IsTheHourSealed())
		{
			FailProbe(TEXT("the hour did not seal the building"));
			return;
		}
		if (!SealedScene->HasNightSealGeometry())
		{
			FailProbe(TEXT("connector night gate geometry is missing"));
			return;
		}
		if (!FGameplayTag::RequestGameplayTag(
			FName(TEXT("State.MissingFloor.Night.MorningCame")), false).IsValid())
		{
			FailProbe(TEXT("night release tag is not registered in DefaultGameplayTags"));
			return;
		}
		// Morning has to be the same exit whichever way it arrives.
		NightPhase->CompleteNightGoal();
		if (NightPhase->IsHourActive() || SealedScene->IsTheHourSealed())
		{
			FailProbe(TEXT("completing the night goal did not release the seal"));
			return;
		}
		// Re-seal for the hunting steps: the entity's rules are what the rest
		// of this probe measures.
		NightPhase->BeginTheHour(1);
		if (!SealedScene->IsTheHourSealed())
		{
			FailProbe(TEXT("the hour could not be re-armed after dawn"));
			return;
		}
		ProbeStep = EProbeStep::MaskingContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::MaskingContract:
	{
		// Synchronous contract: global masking swallows a quiet sound whole,
		// the fridge pocket masks its surroundings, and an unmasked report
		// carries loudness into radius.
		NoiseSubsystem->SetGlobalMasking(0.4f);
		const FIGNoiseEvent Masked = NoiseSubsystem->ReportNoise(
			Entity->GetActorLocation() + FVector(120.0f, 0.0f, 0.0f), 0.3f);
		NoiseSubsystem->SetGlobalMasking(0.0f);
		if (Masked.Loudness > 0.0f)
		{
			FailProbe(TEXT("global masking failed to swallow a 0.3 sound"));
			return;
		}
		// 험의 중심이 아니라 냉장고에 서서 잰다. 플레이어는 좌표를
		// 보고 숨지 않는다 — 기계를 보고 숨는다. 둘이 갈라지면
		// 여기서 걸린다.
		if (!WorldScene.IsValid()
			|| NoiseSubsystem->GetMaskingAt(
				WorldScene->GetFridgeLocation()) <= 0.0f)
		{
			FailProbe(TEXT("fridge hum pocket does not cover the fridge"));
			return;
		}

		// First bait: a single modest sound a short crawl from the entity.
		ProbeNoiseLocation =
			Entity->GetActorLocation() + FVector(0.0f, -40.0f, 0.0f)
			+ FVector(260.0f, 0.0f, 0.0f);
		EmitProbeNoise();
		ProbeStep = EProbeStep::InvestigateOnFirstSound;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::InvestigateOnFirstSound:
		if (Entity->GetListenerState() == EIGListenerState::Investigating
			|| Entity->GetListenerState() == EIGListenerState::Holding)
		{
			const UIGMissingFloorAudioSubsystem* AudioDirector =
				GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>();
			if (!AudioDirector
				|| AudioDirector->GetThreatState()
					!= EIGAudioThreatState::Investigating)
			{
				FailProbe(TEXT("M6 score did not follow investigation state"));
				return;
			}
			EmitProbeNoise();
			ProbeStep = EProbeStep::ChaseOnSecondSound;
			StepDeadlineSeconds = 0.0f;
			break;
		}
		if (StepDeadlineSeconds > 3.0f)
		{
			FailProbe(TEXT("first sound did not trigger Investigating"));
		}
		break;

	case EProbeStep::ChaseOnSecondSound:
		if (Entity->GetListenerState() == EIGListenerState::Chasing)
		{
			const UIGMissingFloorAudioSubsystem* AudioDirector =
				GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>();
			if (!AudioDirector
				|| AudioDirector->GetThreatState()
					!= EIGAudioThreatState::Chasing)
			{
				FailProbe(TEXT("M6 score did not follow chase state"));
				return;
			}
			// Touch: hand the player to the pursuer.
			if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
			{
				PlayerCharacter->TeleportTo(
					Entity->GetActorLocation()
						+ Entity->GetActorForwardVector() * 70.0f,
					PlayerCharacter->GetActorRotation(),
					false,
					true);
			}
			ProbeStep = EProbeStep::CaptureOnTouch;
			StepDeadlineSeconds = 0.0f;
			break;
		}
		if (StepDeadlineSeconds > 3.0f)
		{
			FailProbe(TEXT("second sound did not escalate to Chasing"));
		}
		break;

	case EProbeStep::CaptureOnTouch:
		if (NightLoop->GetCaptureCount() >= 1)
		{
			ProbeStep = EProbeStep::ResetAfterCapture;
			StepDeadlineSeconds = 0.0f;
			break;
		}
		if (StepDeadlineSeconds > 5.0f)
		{
			FailProbe(TEXT("touch did not capture the player"));
		}
		break;

	case EProbeStep::ResetAfterCapture:
	{
		AIGPlayerCharacter* PlayerCharacter = Player.Get();
		const bool bPlayerBackAtBed =
			PlayerCharacter
			&& FVector::Dist(
				PlayerCharacter->GetActorLocation(),
				ExpectedWakeLocation) <= 200.0f;
		const bool bTierRaised = Entity->GetAggressionTier() == 1;
		const bool bCaptureHandprintLeft =
			NightLoop->GetCaptureHandprintCount() >= 1;
		const bool bWakeRecoveryFinished =
			!NightLoop->IsCaptureResetInFlight();
		const bool bInputRestored =
			PlayerCharacter && PlayerCharacter->InputEnabled();
		const bool bMercyNoteReady =
			!bMercyNoteProbeRequested
			|| (NightLoop->IsMercyNoteVisible()
				&& !NightLoop->IsMercyNoteSliding()
				&& FVector::Dist(
					NightLoop->GetMercyNoteLocation(),
					AIGNightLoopDirector::GetMercyNoteRestLocation()) <= 1.0f);
		if (bPlayerBackAtBed
			&& bTierRaised
			&& bCaptureHandprintLeft
			&& bWakeRecoveryFinished
			&& bInputRestored
			&& bMercyNoteReady)
		{
			if (bMercyNoteProbeRequested)
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("MISSINGFLOOR_M65_MERCY_NOTE PASS: "
						"capture=5 slide=1 world_note=1 ui=0"));
			}
			ProbeStep = EProbeStep::MercyNetContract;
			StepDeadlineSeconds = 0.0f;
			break;
		}
		if (StepDeadlineSeconds > 6.0f)
		{
			if (bMercyNoteProbeRequested)
			{
				FailProbe(FString::Printf(
					TEXT("reset incomplete (atBed=%d tier=%d handprints=%d recovery=%d input=%d mercy=%d)"),
					bPlayerBackAtBed ? 1 : 0,
					Entity->GetAggressionTier(),
					NightLoop->GetCaptureHandprintCount(),
					bWakeRecoveryFinished ? 1 : 0,
					bInputRestored ? 1 : 0,
					bMercyNoteReady ? 1 : 0));
			}
			else
			{
				FailProbe(FString::Printf(
					TEXT("reset incomplete (atBed=%d tier=%d handprints=%d recovery=%d input=%d)"),
					bPlayerBackAtBed ? 1 : 0,
					Entity->GetAggressionTier(),
					NightLoop->GetCaptureHandprintCount(),
					bWakeRecoveryFinished ? 1 : 0,
					bInputRestored ? 1 : 0));
			}
		}
		break;
	}

	case EProbeStep::MercyNetContract:
	{
		// §20.3's two automatic nets. The properties worth proving are the ones
		// that make them mercy rather than noise: they key off learning, not
		// walking; they stand down the moment something is learned; they never
		// repeat the same nudge twice running; and they never say the answer.
		AIGMissingFloorMercyDirector* MercyActor = Mercy.Get();
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (!MercyActor || !Narrative)
		{
			FailProbe(TEXT("mercy director or narrative missing"));
			return;
		}
		if (!FMath::IsNearlyEqual(
				AIGMissingFloorMercyDirector::StuckResponseSeconds,
				90.0f,
				0.01f)
			|| AIGMissingFloorMercyDirector::ResetsForEnvironmentHint != 2)
		{
			FailProbe(TEXT("§20.3 thresholds drifted from 90 s and 2 resets"));
			return;
		}

		// The paper takes about a second to come out from under the door, so the
		// separation question can only be asked once it has settled. Measuring
		// mid-slide compares two notes that are both still at the threshold.
		if (bMercyNetsFired)
		{
			if (MercyActor->IsNoteSliding())
			{
				if (StepDeadlineSeconds > 6.0f)
				{
					FailProbe(TEXT("the note never finished sliding"));
				}
				break;
			}
			// Both notes at rest: the five-capture note's authored resting spot
			// is the one the M6.5 contract pins, so compare against that.
			const float SettledSeparation = FVector::Dist2D(
				MercyActor->GetNoteLocation(),
				AIGNightLoopDirector::GetMercyNoteRestLocation());
			if (SettledSeparation < 20.0f)
			{
				FailProbe(FString::Printf(
					TEXT("the two notes rest %.1f cm apart and overlap"),
					SettledSeparation));
				return;
			}
			UE_LOG(
				LogTemp,
				Display,
				TEXT("MISSINGFLOOR_MERCY PASS: 90s clock, 2-reset hint, "
					"responses=%d never repeating, note rests %.1f cm clear of "
					"the five-capture note, stands down on a new source"),
				MercyActor->GetResponseCount(),
				SettledSeparation);
			if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
			{
				PlayerCharacter->TeleportTo(
					AIGNightOneBeatDirector::GetSightingZoneCenter(),
					PlayerCharacter->GetActorRotation(),
					false,
					true);
			}
			ProbeStep = EProbeStep::RecordingRuleContract;
			StepDeadlineSeconds = 0.0f;
			break;
		}

		// The reset that got us here was the first; §20.3-1 wants two in a row
		// with nothing learned in between, so exactly one more must fire it.
		const int32 HintsBefore = MercyActor->GetResetHintCount();
		MercyActor->NotifyCaptureReset();
		const int32 HintsAfter = MercyActor->GetResetHintCount();
		if (HintsAfter != HintsBefore + 1)
		{
			FailProbe(FString::Printf(
				TEXT("two consecutive resets did not add observation material "
					"(%d -> %d)"),
				HintsBefore,
				HintsAfter));
			return;
		}

		// Alternation: the same nudge twice running would train the player to
		// ignore it. Asking three times in a row must never repeat, and on a
		// night where only some responses are available the rotation has to fall
		// through rather than stall.
		// 이 검사는 밤1에서 돈다. 벽에 귀를 대는 그는 밤3의 5층 벽이라 나오면 틀렸고,
		// 메모를 쓴 밤1에는 배관만 남으므로 그때의 되풀이만 허용한다.
		EIGMercyResponse PreviousKind = MercyActor->GetLastResponse();
		for (int32 Attempt = 0; Attempt < 3; ++Attempt)
		{
			const int32 ResponsesBefore = MercyActor->GetResponseCount();
			if (!MercyActor->ForceWorldResponseForTesting()
				|| MercyActor->GetResponseCount() != ResponsesBefore + 1
				|| MercyActor->GetLastResponse() == EIGMercyResponse::None)
			{
				FailProbe(FString::Printf(
					TEXT("the world would not respond on attempt %d"),
					Attempt));
				return;
			}
			const EIGMercyResponse Kind = MercyActor->GetLastResponse();
			if (Kind == EIGMercyResponse::EarToWall
				|| PreviousKind == EIGMercyResponse::EarToWall)
			{
				FailProbe(TEXT("ear-to-wall fired on a night without the fifth-floor wall"));
				return;
			}
			const bool bOnlyPipesLeft =
				Kind == EIGMercyResponse::PipeCry && MercyActor->IsNoteDelivered();
			if (Kind == PreviousKind && !bOnlyPipesLeft)
			{
				FailProbe(TEXT("the same nudge fired twice running"));
				return;
			}
			PreviousKind = Kind;
		}

		// The note is once a night: asking again must not produce a second sheet.
		if (!MercyActor->IsNoteDelivered())
		{
			FailProbe(TEXT("the note never came under the door"));
			return;
		}

		// And the load-bearing property: learning one thing stands both nets
		// down. Without this a player making progress would still be nudged,
		// which reads as the game not watching them.
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::LivedUpstairs,
			EIGMissingFloorSource::MeterReadingSheet);
		MercyActor->NotifyCaptureReset();
		const bool bStandsDownOnProgress =
			MercyActor->GetResetHintCount() == HintsAfter;
		if (!bStandsDownOnProgress)
		{
			FailProbe(TEXT("a new source did not stand the reset net down"));
			return;
		}

		// Everything synchronous is proven. The paper is still moving, so the
		// step re-enters until it settles and then measures the separation.
		bMercyNetsFired = true;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::RecordingRuleContract:
	{
		// §5.5. The rule has one job and one exception, and both have to be true
		// or the whole climax stops meaning anything: her own sounds survive, his
		// do not, and the gap is exactly as long as what it replaced.
		UIGRecordingSubsystem* Recording =
			GetWorld()->GetSubsystem<UIGRecordingSubsystem>();
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (!Recording || !Narrative)
		{
			FailProbe(TEXT("recording subsystem or narrative missing"));
			return;
		}
		if (Narrative->IsNightFourWallOpened())
		{
			FailProbe(TEXT("the wall is already open before night four"));
			return;
		}

		// Two of her sounds either side of one of his.
		Recording->ClearTake();
		Recording->RecordForTesting(0.5f, 0.15f, /*bFromEntity=*/false);
		Recording->RecordForTesting(2.0f, 1.00f, /*bFromEntity=*/true);
		Recording->RecordForTesting(4.0f, 0.15f, /*bFromEntity=*/false);
		const bool bHerSoundsKept = Recording->GetSurvivingCount() == 2;
		const bool bHisSoundRefused = Recording->GetSuppressedCount() == 1;
		// 정확히 그 길이만큼의 무음: a full-loudness knock leaves the longest gap
		// the table allows, and the tape must account for every second of it.
		const float Gap = Recording->GetSuppressedSeconds();
		const bool bGapIsExact = FMath::IsNearlyEqual(Gap, 2.10f, 0.01f);
		const bool bPlaysBack =
			Recording->PlayBack(FVector(0.0f, 0.0f, 1000.0f));
		if (!bHerSoundsKept || !bHisSoundRefused || !bGapIsExact || !bPlaysBack)
		{
			FailProbe(FString::Printf(
				TEXT("§5.5 rule drifted: kept=%d refused=%d gap=%.2fs played=%d"),
				Recording->GetSurvivingCount(),
				Recording->GetSuppressedCount(),
				Gap,
				bPlaysBack ? 1 : 0));
			return;
		}

		// The one exception. Opening the wall in night four lifts the rule, and
		// the first sound the machine keeps is what ending A reports.
		Narrative->SetNightIndex(4);
		Narrative->SetNightFourWallOpened(true);
		if (!Recording->IsRuleLifted())
		{
			FailProbe(TEXT("the wall opened and the rule did not lift"));
			return;
		}
		Recording->ClearTake();
		Recording->RecordForTesting(0.5f, 1.00f, /*bFromEntity=*/true);
		const bool bLiftedKeepsHim =
			Recording->GetSuppressedCount() == 0
			&& Recording->GetSurvivingCount() == 1;
		// Put the night back the way the probe found it; later steps own it.
		Narrative->ResetNightFourForRetry();
		Narrative->SetNightIndex(1);
		Recording->ClearTake();
		if (!bLiftedKeepsHim)
		{
			FailProbe(TEXT("the lifted rule still refused his sound"));
			return;
		}
		if (Recording->IsRuleLifted())
		{
			FailProbe(TEXT("the rule stayed lifted after the night was reset"));
			return;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_RECORDING PASS: her sounds kept, his refused, "
				"%.2fs of exact silence, lifted once by the night-four wall"),
			Gap);

		ProbeStep = EProbeStep::Night1SightingStage;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::Night1SightingStage:
	{
		const bool bStaged = NightOneBeats && NightOneBeats->IsSightingStaged();
		const bool bOnLanding =
			FVector::Dist(
				Entity->GetActorLocation(),
				AIGNightOneBeatDirector::GetSightingStagePoint()) <= 250.0f;
		if (bStaged && bOnLanding)
		{
			// 그를 지나 서쪽 띠로 내려선다.
			if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
			{
				PlayerCharacter->TeleportTo(
					AIGNightOneBeatDirector::GetSightingPassPoint(),
					PlayerCharacter->GetActorRotation(),
					false,
					true);
			}
			ProbeStep = EProbeStep::Night1SightingRestore;
			StepDeadlineSeconds = 0.0f;
			break;
		}
		if (StepDeadlineSeconds > 4.0f)
		{
			FailProbe(FString::Printf(
				TEXT("sighting did not stage (staged=%d onLanding=%d)"),
				bStaged ? 1 : 0,
				bOnLanding ? 1 : 0));
		}
		break;
	}

	case EProbeStep::Night1SightingRestore:
	{
		const bool bCompleted =
			NightOneBeats && NightOneBeats->HasSightingCompleted();
		const bool bBackOnRoute =
			FVector::Dist(
				Entity->GetActorLocation(),
				FVector(-180.0f, -305.0f, 960.0f)) <= 320.0f;
		if (bCompleted && bBackOnRoute)
		{
			// 402호를 먼저 지나도 노크는 소화기 이후 방문을 기다린다.
			CaptureTeleportPlayer(FVector(-30, -300, 998), 0, 0);
			for (TActorIterator<AIGZoneTrigger> It(GetWorld()); It; ++It)
			{
				if (It->RequiredNarrativeBeat == FName(TEXT("Night1.Extinguisher")) && It->WasTriggered())
				{
					FailProbe(TEXT("402호 노크가 선행 사건 전에 소모됨"));
					return;
				}
			}
			// The forced tutorial: stand at the fire cabinet.
			if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
			{
				PlayerCharacter->TeleportTo(
					FVector(232.0f, -290.0f, 1010.0f),
					PlayerCharacter->GetActorRotation(),
					false,
					true);
			}
			ProbeStep = EProbeStep::Night1Extinguisher;
			StepDeadlineSeconds = 0.0f;
			break;
		}
		if (StepDeadlineSeconds > 8.0f)
		{
			FailProbe(FString::Printf(
				TEXT("sighting cameo did not end (completed=%d back=%d)"),
				bCompleted ? 1 : 0,
				bBackOnRoute ? 1 : 0));
		}
		break;
	}

	case EProbeStep::Night1Extinguisher:
	{
		const AIGPrologueWorldScene* SceneNow = WorldScene.Get();
		const bool bDropped =
			SceneNow && SceneNow->IsCorridorExtinguisherDropped();
		const bool bBeatFired =
			NightOneBeats && NightOneBeats->HasExtinguisherBeatFired();
		// CaptureHold also proves it heard the clatter — being caught while
		// standing at the noise is the tutorial's other legitimate outcome.
		const EIGListenerState State = Entity->GetListenerState();
		const bool bReacted =
			State == EIGListenerState::Investigating
			|| State == EIGListenerState::Holding
			|| State == EIGListenerState::Chasing
			|| State == EIGListenerState::CaptureHold;
		if (bDropped && bBeatFired && bReacted)
		{
			CaptureTeleportPlayer(FVector(-30, -300, 998), 0, 0);
			if (!GetNarrative()->HasBeatPlayed(FName(TEXT("Night1.Unit402Knock"))))
			{
				FailProbe(TEXT("소화기 이후 재방문에서 402호 노크 누락"));
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("NIGHT1_SEQUENCE PASS early_visit_retained=1 return_knock=1"));
			if (!NightPhase)
			{
				FailProbe(TEXT("night phase missing for the cycle contract"));
				return;
			}
			// End night 1 through the goal exit and verify the day.
			NightPhase->CompleteNightGoal();
			ProbeStep = EProbeStep::DayNightCycle;
			StepDeadlineSeconds = 0.0f;
			break;
		}
		if (StepDeadlineSeconds > 6.0f)
		{
			FailProbe(FString::Printf(
				TEXT("extinguisher beat incomplete (dropped=%d fired=%d state=%d)"),
				bDropped ? 1 : 0,
				bBeatFired ? 1 : 0,
				static_cast<int32>(State)));
		}
		break;
	}

	case EProbeStep::DayNightCycle:
	{
		const AIGPrologueWorldScene* SceneNow = WorldScene.Get();
		const bool bDay = NightPhase && !NightPhase->IsHourActive();
		const bool bUnsealed = SceneNow && !SceneNow->IsTheHourSealed();
		const bool bEntityAsleep = Entity->IsDormant();
		if (bDay && bUnsealed && bEntityAsleep)
		{
			// The day holds. Go to bed and expect night 2 to begin with the
			// pursuer awake again.
			if (!SleepTarget || !Unit401Door)
			{
				FailProbe(TEXT("sleep target missing"));
				return;
			}
			FIGInteractionContext SleepContext;
			SleepContext.Interactor = Player.Get();
			SleepContext.HoldProgress = 1.0f;
			const FText BeforeConversation = NightPhase->GetObjectiveText();
			SleepContext.TargetActor = Unit401Door;
			IIGInteractable::Execute_CompleteInteraction(Unit401Door, SleepContext);
			if (!GetNarrative()->HasBeatPlayed(AIGNightPhaseDirector::DayConversationBeatId(1))
				|| NightPhase->GetObjectiveText().EqualTo(BeforeConversation)
				|| !NightPhase->GetObjectiveText().EqualTo(
					NSLOCTEXT("IGMissingFloor", "ArrivalObjectiveSleep", "403호로 돌아가서 자기")))
			{
				FailProbe(TEXT("401호 대화를 마쳐도 낮 목표가 취침으로 바뀌지 않음"));
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_DAYOBJECTIVE PASS: 401호 대화 뒤 취침 안내"));
			SleepContext.TargetActor = SleepTarget;
			IIGInteractable::Execute_CompleteInteraction(
				SleepTarget, SleepContext);

			UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
			const bool bNightTwo =
				NightPhase->IsHourActive()
				&& Narrative
				&& Narrative->GetNightIndex() == 2
				&& !Entity->IsDormant();
			if (!bNightTwo)
			{
				FailProbe(TEXT("sleeping did not begin night 2"));
				return;
			}
			ProbeStep = EProbeStep::NightTwoDoorBeatContract;
			StepDeadlineSeconds = 0.0f;
			break;
		}
		if (StepDeadlineSeconds > 4.0f)
		{
			FailProbe(FString::Printf(
				TEXT("dawn incomplete (day=%d unsealed=%d asleep=%d)"),
				bDay ? 1 : 0,
				bUnsealed ? 1 : 0,
				bEntityAsleep ? 1 : 0));
		}
		break;
	}

	case EProbeStep::NightTwoDoorBeatContract:
	{
		// §8 비트 2-1. The beat has to arm itself on night two without being
		// asked, put a figure outside 403, and leave the three knocks on the
		// tape as the refusal §5.5's morning playback is built on.
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		UIGRecordingSubsystem* Recording =
			GetWorld()->GetSubsystem<UIGRecordingSubsystem>();
		AIGMissingFloorEvidence* Peephole =
			NightTwoBeats ? NightTwoBeats->GetPeephole() : nullptr;
		if (!NightTwoBeats || !Narrative || !Recording || !Peephole)
		{
			FailProbe(TEXT("§8 비트 2-1 director or its peephole is missing"));
			return;
		}
		if (NightTwoBeats->HasPlayed())
		{
			FailProbe(TEXT("the door beat played before night two armed it"));
			return;
		}

		// The phone is the player's verb, so the probe plays the player: arm the
		// recording, then let the beat run without waiting out its patience.
		Recording->ClearTake();
		Recording->StartRecording();
		NightTwoBeats->AdvanceForTesting();

		if (!NightTwoBeats->HasPlayed()
			|| NightTwoBeats->GetStage() != EIGNightTwoBeatStage::Spent)
		{
			FailProbe(FString::Printf(
				TEXT("§8 비트 2-1 did not finish: stage=%d knocks=%d"),
				static_cast<int32>(NightTwoBeats->GetStage()),
				NightTwoBeats->GetKnockCount()));
			return;
		}
		// One knock to bring her to the door, the triple through it, the drag.
		if (NightTwoBeats->GetKnockCount() != 3)
		{
			FailProbe(FString::Printf(
				TEXT("§8 비트 2-1 played %d of its 3 cues"),
				NightTwoBeats->GetKnockCount()));
			return;
		}
		if (!NightTwoBeats->WasRecordingDuringAnswer())
		{
			FailProbe(TEXT("the armed phone was not running for the answer"));
			return;
		}
		// The figure was on loan. It must be back on its corridor route, or
		// night 2's patrol runs a two-point shuffle outside one door all hour.
		if (NightTwoBeats->IsFigureAtDoor())
		{
			FailProbe(TEXT("the figure stayed at the door after the beat"));
			return;
		}
		// §5.5's payoff: his knocks are on the log and every one is refused.
		// 2.10 s is what the loudness table gives a full-loudness triple, and
		// the morning gap is exactly that long.
		const int32 Suppressed = Recording->GetSuppressedCount();
		const float SuppressedSeconds = Recording->GetSuppressedSeconds();
		if (Suppressed < 2 || Suppressed != Recording->GetRecordedCount())
		{
			FailProbe(FString::Printf(
				TEXT("the door beat left %d of %d events on the tape"),
				Recording->GetRecordedCount() - Suppressed,
				Recording->GetRecordedCount()));
			return;
		}
		if (SuppressedSeconds < 2.10f)
		{
			FailProbe(FString::Printf(
				TEXT("the triple knock left only %.2fs of silence"),
				SuppressedSeconds));
			return;
		}
		// Once per run. A capture reset must not replay it as a jump scare.
		if (!Narrative->HasBeatPlayed(FName(TEXT("Night2.DoorKnock"))))
		{
			FailProbe(TEXT("the door beat did not book itself"));
			return;
		}
		Recording->StopRecording();
		Recording->ClearTake();

		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_N2DOOR PASS: knock at 403, figure staged and "
				"released, 3 cues, %d refused events, %.2fs of silence"),
			Suppressed,
			SuppressedSeconds);

		// Into the booth, whose door the hour has opened.
		if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
		{
			PlayerCharacter->TeleportTo(
				FVector(170.0f, -150.0f, 92.0f),
				PlayerCharacter->GetActorRotation(),
				false,
				true);
		}
		ProbeStep = EProbeStep::PuzzleTwoContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::PuzzleTwoContract:
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (!PuzzleTwo || !Narrative || !PuzzleTwo->ValidateFixtures())
		{
			FailProbe(TEXT("P2 fixtures were not all placed"));
			return;
		}

		AIGMissingFloorEvidence* Carbon = PuzzleTwo->GetCarbonLedger();
		AIGReadableNote* AgentNote = PuzzleTwo->GetAgentMessageNote();
		AIGMissingFloorEvidence* Cctv = PuzzleTwo->GetCctvSelector();
		if (!Carbon || !AgentNote || !Cctv)
		{
			FailProbe(TEXT("P2 interactables unresolved"));
			return;
		}

		FIGInteractionContext Context;
		Context.Interactor = Player.Get();
		Context.HoldProgress = 1.0f;

		// The booth's own valve: water on the riser masks the desk, which is
		// the verb night 3 and night 4 build on. Open it first, the way a
		// careful player would, and prove the rub is swallowed where she sits.
		AIGMissingFloorEvidence* BoothValve = PuzzleTwo->GetBoothRiserValve();
		if (!BoothValve || !BoothValve->IsInteractionEnabled())
		{
			FailProbe(TEXT("the booth riser valve was not available on night 2"));
			return;
		}
		Context.TargetActor = BoothValve;
		IIGInteractable::Execute_CompleteInteraction(BoothValve, Context);
		if (!PuzzleTwo->IsBoothValveOpen()
			|| NoiseSubsystem->GetMaskingAt(Carbon->GetActorLocation()) < 0.29f)
		{
			FailProbe(TEXT("the booth valve did not put water over the desk"));
			return;
		}

		// 중간에 손을 떼도 칠한 흔적과 남은 작업 시간이 보존된다.
		Context.TargetActor = Carbon;
		const float FullRubDuration = Carbon->GetInteractionHoldDuration_Implementation(Player.Get());
		Carbon->BeginInteraction_Implementation(Context);
		Context.HoldProgress = .5f;
		Carbon->UpdateInteraction_Implementation(Context);
		Carbon->EndInteraction_Implementation(Context, EIGInteractionEndReason::Cancelled);
		if (!FMath::IsNearlyEqual(Carbon->GetRevealFraction(), 1.f / 6.f, .002f)
			|| !FMath::IsNearlyEqual(Carbon->GetInteractionHoldDuration_Implementation(Player.Get()), FullRubDuration * .5f, .002f)
			|| Carbon->GetCompletedStageCount() != 0 || Carbon->HasBeenExamined())
		{
			FailProbe(TEXT("접수철 중단 후 진행 보존 실패")); return;
		}
		Carbon->BeginInteraction_Implementation(Context);
		Carbon->UpdateInteraction_Implementation(Context);
		Carbon->EndInteraction_Implementation(Context, EIGInteractionEndReason::Cancelled);
		if (!FMath::IsNearlyEqual(Carbon->GetRevealFraction(), .25f, .002f))
		{
			FailProbe(TEXT("접수철 남은 작업 재개 실패")); return;
		}
		Context.HoldProgress = 1.f;
		// 두 줄만 복원했을 때는 증거를 확정하지 않는다.
		IIGInteractable::Execute_CompleteInteraction(Carbon, Context);
		IIGInteractable::Execute_CompleteInteraction(Carbon, Context);
		if (Narrative->HasSource(
			EIGMissingFloorTruth::WasStillAlive,
			EIGMissingFloorSource::CarbonLedgerOriginal))
		{
			FailProbe(TEXT("carbon original filed before the final pass"));
			return;
		}
		// ...and the third files the original.
		IIGInteractable::Execute_CompleteInteraction(Carbon, Context);
		if (!FMath::IsNearlyEqual(Carbon->GetRevealFraction(), 1.f)
			|| Carbon->GetInteractionHoldDuration_Implementation(Player.Get()) != 0.f)
		{
			FailProbe(TEXT("복원한 접수철의 읽기 전환 실패")); return;
		}
		// 이미 읽은 증거로 액터를 다시 만들면 복원된 종이와 즉시 읽기가 돌아온다.
		AIGMissingFloorEvidence* RestoredPad = GetWorld()->SpawnActor<AIGMissingFloorEvidence>();
		if (!RestoredPad) { FailProbe(TEXT("접수철 복구 검사 액터 생성 실패")); return; }
		RestoredPad->Configure(Carbon->GetPresentationMesh()->GetStaticMesh(), Carbon->GetPresentationMesh()->GetMaterial(0), FVector::ZeroVector,
			FText::GetEmpty(), FText::GetEmpty(), EIGMissingFloorTruth::WasStillAlive,
			EIGMissingFloorSource::CarbonLedgerOriginal, FullRubDuration, .25f);
		RestoredPad->SetProgressiveStages({FText::GetEmpty(), FText::GetEmpty()});
		const bool bRestored = RestoredPad->ConfigureProgressReveal(
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/M_ComplaintImpression.M_ComplaintImpression")),
			FVector::ZeroVector, FVector2D(19.8f,27.7f), FText::GetEmpty())
			&& RestoredPad->HasBeenExamined() && FMath::IsNearlyEqual(RestoredPad->GetRevealFraction(), 1.f)
			&& RestoredPad->GetInteractionHoldDuration_Implementation(Player.Get()) == 0.f;
		RestoredPad->Destroy();
		if (!bRestored) { FailProbe(TEXT("저장된 접수철 복원 상태 재구성 실패")); return; }
		UE_LOG(LogTemp, Display, TEXT("BOOTH_RUB_PROGRESS PASS interrupted=1 resumed=1 revealed=1 instant_reread=1 restored=1"));
		if (!Narrative->HasSource(
			EIGMissingFloorTruth::WasStillAlive,
			EIGMissingFloorSource::CarbonLedgerOriginal))
		{
			FailProbe(TEXT("three frottage passes did not restore the original"));
			return;
		}
		if (Narrative->HasTruth(EIGMissingFloorTruth::WasStillAlive))
		{
			FailProbe(TEXT("T7 confirmed from the carbon record alone"));
			return;
		}

		if (Narrative->IsPuzzleSolved(FName(TEXT("P2"))))
		{
			FailProbe(TEXT("P2 completed before comparing the move-out date"));
			return;
		}
		if (AgentNote->IsHidden() || !AgentNote->IsInteractionEnabled())
		{
			FailProbe(TEXT("the printed message must already be on the desk"));
			return;
		}
		Context.TargetActor = AgentNote;
		IIGInteractable::Execute_CompleteInteraction(AgentNote, Context);
		if (!Narrative->HasTruth(EIGMissingFloorTruth::WasStillAlive)
			|| !Narrative->IsPuzzleSolved(FName(TEXT("P2"))))
		{
			FailProbe(TEXT("P2 date comparison did not complete")); return;
		}
		IIGInteractable::Execute_CompleteInteraction(AgentNote, Context);
		// §8 밤2 ends at 403's door, not here. The restored original arms 비트
		// 2-5 and the hour has to still be running, or the return chase never
		// happens.
		if (!NightPhase->IsHourActive())
		{
			FailProbe(TEXT("restoring the original released her to dawn from the booth"));
			return;
		}
		if (!NightTwoBeats
			|| NightTwoBeats->GetReturnStage()
				!= EIGNightTwoReturnStage::AwaitingExit)
		{
			FailProbe(FString::Printf(
				TEXT("§8 비트 2-5 did not arm on the restored original: stage=%d"),
				NightTwoBeats
					? static_cast<int32>(NightTwoBeats->GetReturnStage())
					: -1));
			return;
		}

		// §14 상시 렌더 금지. Before the press the channel must cost the frame
		// nothing at all: no render target, no capture, no picture. Pressed while
		// she is still at the desk, because that is where the monitor is.
		AIGCctvChannelFive* Channel = PuzzleTwo->GetCctvChannelFive();
		if (!Channel)
		{
			FailProbe(TEXT("§14 channel five was never built with the booth"));
			return;
		}
		if (Channel->GetState() != EIGCctvChannelState::Idle
			|| Channel->HasFeed()
			|| Channel->IsOnScreen()
			|| Channel->GetCaptureCount() != 0)
		{
			FailProbe(FString::Printf(
				TEXT("§14 상시 렌더 금지 broken before the press: "
					"state=%d feed=%d onscreen=%d captures=%d"),
				static_cast<int32>(Channel->GetState()),
				Channel->HasFeed() ? 1 : 0,
				Channel->IsOnScreen() ? 1 : 0,
				Channel->GetCaptureCount()));
			return;
		}

		// The one-shot CCTV beat books itself exactly once.
		Context.TargetActor = Cctv;
		IIGInteractable::Execute_CompleteInteraction(Cctv, Context);
		if (!Narrative->HasBeatPlayed(FName(TEXT("Night2.CCTV"))))
		{
			FailProbe(TEXT("CCTV channel-five beat did not book"));
			return;
		}
		if (!Channel->HasFeed() || !Channel->IsOnScreen())
		{
			FailProbe(TEXT("the fifth button did not put a picture on the monitor"));
			return;
		}
		// CIF, 또는 진단 배율을 곱한 CIF. 배율이 없는 실행에서는 정확히 352×288.
		const FIntPoint Resolution = Channel->GetFeedResolution();
		const FIntPoint ExpectedResolution = Channel->GetExpectedFeedResolution();
		if (Resolution != ExpectedResolution
			|| ExpectedResolution.X % 352 != 0
			|| ExpectedResolution.Y % 288 != 0)
		{
			FailProbe(FString::Printf(
				TEXT("channel five is not a CIF channel: %dx%d expected %dx%d"),
				Resolution.X,
				Resolution.Y,
				ExpectedResolution.X,
				ExpectedResolution.Y));
			return;
		}

		CctvCapturesAtLive = 0;
		CctvCapturesAtDeath = 0;
		bCctvLiveSoundHeard = false;
		bCctvFeedMeasured = false;
		CctvFeedBrightestLuma = 0.0f;
		CctvFeedLitFraction = 0.0f;
		ProbeStep = EProbeStep::CctvChannelContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::NightTwoReturnChaseContract:
	{
		// §8 비트 2-5. Leaving the booth has to drop the stack, the building has
		// to hear it twice — which is what makes the real AI commit to CHASE —
		// and only arriving back inside 403 may end the night.
		if (!NightTwoBeats || !NightPhase)
		{
			FailProbe(TEXT("§8 비트 2-5 director disappeared"));
			return;
		}
		if (!NightTwoBeats->HasReturnChaseFired())
		{
			if (StepDeadlineSeconds > 4.0f)
			{
				FailProbe(TEXT("leaving the booth did not drop the material"));
				return;
			}
			break;
		}
		if (NightTwoBeats->GetReturnStage() == EIGNightTwoReturnStage::Chased
			&& !NightPhase->IsHourActive())
		{
			FailProbe(TEXT("night 2 ended while she was still out of 403"));
			return;
		}
		// The chase is the real AI reacting to two sounds. Give it a moment to
		// commit, then check it is hunting rather than still patrolling.
		if (StepDeadlineSeconds < 1.5f)
		{
			break;
		}
		const bool bHunting = !Entity->IsDormant()
			&& Entity->GetListenerState() != EIGListenerState::Patrolling;
		if (!bHunting)
		{
			FailProbe(FString::Printf(
				TEXT("the collapse did not move the building: state=%d"),
				static_cast<int32>(Entity->GetListenerState())));
			return;
		}

		// Home. Being teleported here by a capture reset would not have counted;
		// that path owes the night another trip out and back.
		if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
		{
			PlayerCharacter->TeleportTo(
				FVector(60.0f, -120.0f, 992.0f),
				PlayerCharacter->GetActorRotation(),
				false,
				true);
		}
		ProbeStep = EProbeStep::NightTwoHomeContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::NightTwoHomeContract:
	{
		if (!NightTwoBeats || !NightPhase)
		{
			FailProbe(TEXT("§8 비트 2-5 director disappeared before dawn"));
			return;
		}
		const bool bHome =
			NightTwoBeats->GetReturnStage() == EIGNightTwoReturnStage::Home;
		if (!bHome || NightPhase->IsHourActive() || !Entity->IsDormant())
		{
			if (StepDeadlineSeconds > 6.0f)
			{
				FailProbe(FString::Printf(
					TEXT("§8 비트 2-5 did not close: home=%d hour=%d dormant=%d"),
					bHome ? 1 : 0,
					NightPhase->IsHourActive() ? 1 : 0,
					Entity->IsDormant() ? 1 : 0));
				return;
			}
			break;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_N2CHASE PASS: T7 armed the return, the booth exit "
				"dropped the stack, two sounds moved the building, and 403 "
				"ended the night"));

		ProbeStep = EProbeStep::DayTwoContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::CctvChannelContract:
	{
		AIGCctvChannelFive* Channel =
			PuzzleTwo ? PuzzleTwo->GetCctvChannelFive() : nullptr;
		if (!Channel)
		{
			FailProbe(TEXT("channel five disappeared mid-beat"));
			return;
		}

		// 화면이 살아 있는 동안 복도가 한 번 운다 — latched, because the poll
		// must not have to land on the frame it fired.
		bCctvLiveSoundHeard = bCctvLiveSoundHeard || Channel->HasLiveSoundPlayed();
		if (Channel->GetState() == EIGCctvChannelState::Live)
		{
			CctvCapturesAtLive = Channel->GetCaptureCount();
			// Read the target while there is still a picture in it. Once is
			// enough, and the beat is not repeatable so there is no second chance.
			// 2.80 s past the press is 0.44 through the live window: the bulb has
			// settled and the first dropout is over, so the reading and the
			// exported frame are the corridor as the player sees it.
			if (bCctvFeedProbeRequested && !bCctvFeedMeasured
				&& StepDeadlineSeconds >= 2.80f)
			{
				MeasureCctvFeed(Channel);
			}
		}
		if (Channel->GetState() == EIGCctvChannelState::Collapsing
			&& CctvCapturesAtDeath == 0)
		{
			CctvCapturesAtDeath = Channel->GetCaptureCount();
		}
		if (!Channel->IsSpent())
		{
			// Acquire 0.32 + live 5.60 + collapse 0.86 = 6.78 s of channel.
			if (StepDeadlineSeconds > 12.0f)
			{
				FailProbe(FString::Printf(
					TEXT("channel five never died: state=%d after %.1fs"),
					static_cast<int32>(Channel->GetState()),
					StepDeadlineSeconds));
				return;
			}
			break;
		}

		// Spent. Everything the beat allocated has to be gone again (§14).
		if (Channel->HasFeed() || Channel->IsOnScreen())
		{
			FailProbe(FString::Printf(
				TEXT("§14 the dead channel is still allocated: "
					"feed=%d onscreen=%d"),
				Channel->HasFeed() ? 1 : 0,
				Channel->IsOnScreen() ? 1 : 0));
			return;
		}
		if (!bCctvLiveSoundHeard)
		{
			FailProbe(TEXT("the corridor never sounded while the picture was up"));
			return;
		}
		// 12 fps over the 5.92 s the picture is up is about 71 renders. The band
		// is wide enough for frame pacing and narrow enough to catch either
		// failure that matters: a capture stuck off, or one running every frame.
		const int32 Captures = Channel->GetCaptureCount();
		if (Captures < 40 || Captures > 110)
		{
			FailProbe(FString::Printf(
				TEXT("channel five captured %d frames; expected about 71 "
					"(12 fps for 5.92 s)"),
				Captures));
			return;
		}
		if (CctvCapturesAtDeath != 0 && Captures != CctvCapturesAtDeath)
		{
			FailProbe(FString::Printf(
				TEXT("the capture kept rendering through the collapse: %d -> %d"),
				CctvCapturesAtDeath,
				Captures));
			return;
		}
		// 1회 한정, 반복 재생 불가 — enforced by the actor, not only by the beat.
		if (Channel->Play())
		{
			FailProbe(TEXT("channel five played a second time"));
			return;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_CCTV5 PASS: 352x288 allocated on the press, "
				"%d captures, the corridor sounded once, released on death, "
				"second press refused"),
			Captures);

		if (bCctvFeedProbeRequested)
		{
			// The picture itself. Reported separately because it needs a real RHI,
			// and reported as FAIL rather than silence when the read comes back
			// black — an all-black capture satisfies every structural check above.
			const bool bNullRhi =
				FParse::Param(FCommandLine::Get(), TEXT("nullrhi"));
			if (bNullRhi)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("MISSINGFLOOR_CCTV5_FEED SKIP: -nullrhi renders no "
						"scene capture. Re-run with -RenderOffScreen and no "
						"-nullrhi to measure the picture."));
			}
			else if (!bCctvFeedMeasured)
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("MISSINGFLOOR_CCTV5_FEED FAIL: the render target could "
						"not be read while the channel was live"));
			}
			else if (CctvFeedBrightestLuma < 0.08f || CctvFeedLitFraction < 0.02f)
			{
				UE_LOG(
					LogTemp,
					Error,
					TEXT("MISSINGFLOOR_CCTV5_FEED FAIL: the channel rendered "
						"black. brightest=%.4f lit=%.4f"),
					CctvFeedBrightestLuma,
					CctvFeedLitFraction);
			}
			else
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("MISSINGFLOOR_CCTV5_FEED PASS: brightest=%.4f "
						"lit=%.4f of the frame"),
					CctvFeedBrightestLuma,
					CctvFeedLitFraction);
			}
		}

		// Step out of the booth into the connector. §8 비트 2-5's collapse fires
		// on the player's own position, so this is the walk home starting, not a
		// poke at the beat.
		if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
		{
			PlayerCharacter->TeleportTo(
				FVector(190.0f, -300.0f, 92.0f),
				PlayerCharacter->GetActorRotation(),
				false,
				true);
		}
		ProbeStep = EProbeStep::NightTwoReturnChaseContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::DayTwoContract:
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (!NightThree || !Narrative || !NightThree->ValidateFixtures())
		{
			FailProbe(TEXT("night-3 fixtures were not all placed"));
			return;
		}

		FIGInteractionContext Context;
		Context.Interactor = Player.Get();
		Context.HoldProgress = 1.0f;

		// The day papers feed the truth board: the labels alone name no one
		// (T2 needs the notebook), the printout carries both the noise war
		// and its last morning, and the journal is earned by T7 and daylight.
		AIGReadableNote* Labels = NightThree->GetLabelsNote();
		AIGReadableNote* Forum = NightThree->GetForumNote();
		AIGReadableNote* Journal = NightThree->GetJournalNote();
		if (!Labels || !Forum || !Journal)
		{
			FailProbe(TEXT("day papers unresolved"));
			return;
		}
		// 날짜 대조를 마친 다음 낮에는 황순금의 기록을 받을 수 있다.
		if (Journal->IsHidden() == Narrative->HasTruth(EIGMissingFloorTruth::WasStillAlive))
		{
			FailProbe(TEXT("day journal visibility did not follow the date evidence"));
			return;
		}

		Context.TargetActor = Labels;
		IIGInteractable::Execute_CompleteInteraction(Labels, Context);
		IIGInteractable::Execute_CompleteInteraction(Labels, Context);
		Context.TargetActor = Forum;
		IIGInteractable::Execute_CompleteInteraction(Forum, Context);
		IIGInteractable::Execute_CompleteInteraction(Forum, Context);

		if (Narrative->HasTruth(EIGMissingFloorTruth::TenantIdentity))
		{
			FailProbe(TEXT("T2 confirmed from the labels alone"));
			return;
		}

		// To bed: night 3 begins.
		if (SleepTarget)
		{
			Context.TargetActor = SleepTarget;
			IIGInteractable::Execute_CompleteInteraction(SleepTarget, Context);
		}
		if (!NightPhase || !NightPhase->IsHourActive()
			|| Narrative->GetNightIndex() != 3)
		{
			FailProbe(TEXT("sleeping did not begin night 3"));
			return;
		}
		ProbeStep = EProbeStep::AnswerReachContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::NightThreeContract:
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (!NightThree || !Narrative)
		{
			FailProbe(TEXT("night-3 stage lost mid-contract"));
			return;
		}

		FIGInteractionContext Context;
		Context.Interactor = Player.Get();
		Context.HoldProgress = 1.0f;

		// The keyring in the open booth unlocks the gate as a saved fact.
		AIGMissingFloorEvidence* Key = NightThree->GetKeyring();
		AIGSwingDoor* Gate = NightThree->GetStairGate();
		AIGSwingDoor* AnnexGate = NightThree->GetAnnexGate();
		if (!Key || !Gate || !AnnexGate)
		{
			FailProbe(TEXT("keyring or either physical gate unresolved"));
			return;
		}
		if (!Gate->IsLocked() || !AnnexGate->IsLocked())
		{
			FailProbe(TEXT("a rooftop gate stood open before the keyring"));
			return;
		}
		// 실제 조준에 쓰는 단순 충돌로도 열쇠에 닿아야 집을 수 있다.
		const FVector KeyCenter = Key->GetComponentsBoundingBox().GetCenter();
		FHitResult KeyHit;
		FCollisionQueryParams KeyQuery(SCENE_QUERY_STAT(KeyPickupProbe), false, Player.Get());
		if (!GetWorld()->LineTraceSingleByChannel(KeyHit,
			KeyCenter + FVector(0, -70, 70), KeyCenter - FVector(0, 0, 1), ECC_Visibility, KeyQuery)
			|| KeyHit.GetActor() != Key)
		{
			FailProbe(TEXT("keyring cannot be targeted through the gameplay visibility trace")); return;
		}
		Context.TargetActor = Key;
		IIGInteractable::Execute_CompleteInteraction(Key, Context);
		if (Gate->IsLocked() || AnnexGate->IsLocked() || !Key->IsHidden()
			|| Key->IsInteractionEnabled() || Key->GetActorEnableCollision())
		{
			FailProbe(TEXT("key pickup did not release the gates and remove the keyring"));
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("UTILITY_KEYRING PASS visibility_trace=1 picked_up=1 hidden=1 collision_off=1 gates_unlocked=2"));
		// The realtor's message waits beside the keyring. Two reads: open, close.
		AIGReadableNote* AgentNote =
			PuzzleTwo ? PuzzleTwo->GetAgentMessageNote() : nullptr;
		if (!AgentNote || AgentNote->IsHidden()
			|| !AgentNote->IsInteractionEnabled())
		{
			FailProbe(TEXT("the realtor's message did not appear on night 3"));
			return;
		}
		Context.TargetActor = AgentNote;
		IIGInteractable::Execute_CompleteInteraction(AgentNote, Context);
		IIGInteractable::Execute_CompleteInteraction(AgentNote, Context);
		if (!Narrative->HasTruth(EIGMissingFloorTruth::WasStillAlive))
		{
			FailProbe(TEXT("T7 did not confirm after crossing both records"));
			return;
		}

		// The topology contract above owns the full walk. The probe jumps only
		// after proving both leaves and the 640 cm collision receipt, so content
		// interactions can remain deterministic and fast in headless CI.
		if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
		{
			PlayerCharacter->TeleportTo(
				FVector(-280.0f, 700.0f, 1292.0f),
				PlayerCharacter->GetActorRotation(),
				false,
				true);
		}
		AIGReadableNote* Notebook = NightThree->GetTunerNotebook();
		Context.TargetActor = Notebook;
		IIGInteractable::Execute_CompleteInteraction(Notebook, Context);
		IIGInteractable::Execute_CompleteInteraction(Notebook, Context);
		if (!Narrative->HasTruth(EIGMissingFloorTruth::TenantIdentity)
			|| !Narrative->HasTruth(EIGMissingFloorTruth::NoiseWasHomecoming))
		{
			FailProbe(TEXT("notebook did not cross T2/T3 with the day papers"));
			return;
		}
		AIGMissingFloorEvidence* Mark = NightThree->GetImpactMark();
		Context.TargetActor = Mark;
		IIGInteractable::Execute_CompleteInteraction(Mark, Context);
		if (!Narrative->HasTruth(EIGMissingFloorTruth::LandingStruggle))
		{
			FailProbe(TEXT("impact mark did not cross T4 with the final post"));
			return;
		}

		// P3, the patient route: silence first, then water behind one bay.
		AIGMissingFloorEvidence* CavityListen = NightThree->GetWallListen(1);
		if (!NightThree->TryPlayerListen(CavityListen, Player.Get()))
		{
			FailProbe(TEXT("dedicated listen verb rejected the cavity wall"));
			return;
		}
		if (Narrative->HasSource(
			EIGMissingFloorTruth::SomeoneInTheWall,
			EIGMissingFloorSource::PipeWaterComparison))
		{
			FailProbe(TEXT("a dry wall filed the water comparison"));
			return;
		}
		AIGMissingFloorEvidence* Valve = NightThree->GetRiserValve();
		Context.TargetActor = Valve;
		IIGInteractable::Execute_CompleteInteraction(Valve, Context);
		if (!NightThree->IsValveOpen())
		{
			FailProbe(TEXT("valve did not open"));
			return;
		}
		// P3는 견주는 퍼즐이다. 한 칸만 들어서는 「이 벽만」이 서지 않는다.
		// 찬 벽 하나를 먼저 듣고 공동 칸으로 돌아온다.
		if (!NightThree->TryPlayerListen(NightThree->GetWallListen(0), Player.Get()))
		{
			FailProbe(TEXT("dedicated listen verb rejected a solid wall"));
			return;
		}
		if (Narrative->HasSource(
			EIGMissingFloorTruth::SomeoneInTheWall,
			EIGMissingFloorSource::PipeWaterComparison))
		{
			FailProbe(TEXT("one wall filed the water comparison without a second"));
			return;
		}
		if (!NightThree->TryPlayerListen(CavityListen, Player.Get()))
		{
			FailProbe(TEXT("dedicated listen verb dropped after valve open"));
			return;
		}
		if (!Narrative->HasTruth(EIGMissingFloorTruth::SomeoneInTheWall))
		{
			FailProbe(TEXT("criterion plus water did not confirm T6"));
			return;
		}

		// The answer surface arms only now.
		AIGMissingFloorEvidence* Answer = NightThree->GetAnswerTarget();
		if (!Answer || Answer->IsHidden() || !Answer->IsInteractionEnabled())
		{
			FailProbe(TEXT("answer target did not arm after T6"));
			return;
		}
		if (!NightThree->TryPlayerKnock(Answer, Player.Get()))
		{
			FailProbe(TEXT("dedicated knock verb rejected the armed answer wall"));
			return;
		}
		ProbeStep = EProbeStep::AnswerPairTap;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::AnswerReachContract:
	{
		// §8 비트 3-7. P4 teaches 둘-쉬고-하나 on an authored wall; the corridor
		// asks her to use it with nothing under the cursor. Before this existed
		// the entity's Waiting state and NotifyAnswerKnock had no caller at all,
		// so the answer could not leave P4's surface.
		AIGPlayerCharacter* PlayerCharacter = Player.Get();
		if (!PlayerCharacter)
		{
			FailProbe(TEXT("no pawn to answer with"));
			return;
		}
		// Stand him next to her so the taps are within a knock's earshot, and
		// make sure he is awake and merely patrolling first. His dormancy is
		// remembered rather than assumed: this check runs right after the sleep
		// that begins night three, so "it is still day" is not true here.
		bAnswerReachWasDormant = Entity->IsDormant();
		Entity->SetDormant(false);
		if (Entity->GetListenerState() == EIGListenerState::Waiting)
		{
			FailProbe(TEXT("he was already waiting before she answered"));
			return;
		}
		// 바로 앞 단계가 밤2 귀환 추격이라 긴장이 0.85를 넘은 채 온다. 그 심박은
		// 3m 안의 그에게 소리로 들려 두 번이면 추격이 된다(규칙 5). 이 검사는
		// 박자의 뜻만 보므로 탭이 끝날 때까지 심장을 재운다.
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->SuppressHeartbeat(6.0f, false);
		}
		// 텔레포트만 하면 잠에서 깬 직후 들은 소리에 대한 반응이 남는다. 무언가를
		// 쫓는 중이면 P4 전의 박자는 귀를 세우지 못하고 그냥 소리가 된다.
		Entity->ParkForBeat(
			PlayerCharacter->GetActorLocation() + FVector(180.0f, 0.0f, 0.0f),
			Entity->GetActorRotation().Yaw);

		// Out of earshot the cadence must do nothing at all. Two floors up is
		// the case the guard exists for.
		const FVector FarAway =
			PlayerCharacter->GetActorLocation() + FVector(0.0f, 0.0f, 1800.0f);
		if (PlayerCharacter->OfferAnswerKnock(FarAway))
		{
			FailProbe(TEXT("an answer from two floors up reached him"));
			return;
		}

		// 둘 — 쉬고 — 하나, at the authored windows. The taps are offered
		// through the same entry point the knock verb uses.
		const FVector Here = PlayerCharacter->GetActorLocation();
		const bool bFirst = PlayerCharacter->OfferAnswerKnock(Here);
		AnswerReachTapTwoAt =
			GetWorld()->GetTimeSeconds() + AIGListenerEntity::AnswerPairMinSeconds;
		if (!bFirst)
		{
			FailProbe(TEXT("the first tap of the answer was not taken"));
			return;
		}
		ProbeStep = EProbeStep::AnswerReachCadence;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::AnswerReachCadence:
	{
		AIGPlayerCharacter* PlayerCharacter = Player.Get();
		if (!PlayerCharacter)
		{
			FailProbe(TEXT("no pawn to finish the answer with"));
			return;
		}
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now < AnswerReachTapTwoAt)
		{
			break;
		}
		const FVector Here = PlayerCharacter->GetActorLocation();
		if (AnswerReachTapsSent == 0)
		{
			PlayerCharacter->OfferAnswerKnock(Here);
			AnswerReachTapsSent = 1;
			// The rest: longer than the pair, inside the authored window.
			AnswerReachTapTwoAt = Now + AIGListenerEntity::AnswerRestMinSeconds
				+ 0.10;
			break;
		}
		PlayerCharacter->OfferAnswerKnock(Here);
		// 밤3 잠자리 직후라 P4 전(T9 전)이다. 이때 박자가 그를 얼리면 밤1 복도에서
		// P4의 해금과 3-7의 반전이 먼저 새어 나간다. 귀를 세우는 데서 그쳐야 한다.
		// 박자가 그를 세운다는 쪽은 T9 뒤의 NightThreePassContract가 본다.
		if (Entity->GetListenerState() == EIGListenerState::Waiting)
		{
			FailProbe(FString::Printf(
				TEXT("둘-쉬고-하나 froze him before P4: state=%d"),
				static_cast<int32>(Entity->GetListenerState())));
			return;
		}
		if (Entity->GetListenerState() != EIGListenerState::Listening)
		{
			FailProbe(FString::Printf(
				TEXT("둘-쉬고-하나 did not raise his ears before P4: state=%d"),
				static_cast<int32>(Entity->GetListenerState())));
			return;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_ANSWERREACH PASS: before P4 the cadence at nothing "
				"raised his ears without freezing him, and the same cadence from "
				"two floors up did not reach him"));

		// Put the day back exactly as it was: P4's own tap sequence runs later
		// and its pair interval is 0.65 s at the outside, so nothing of this
		// check may still be standing between his first and second knock.
		Entity->ResetToPatrolStart(/*bRaiseAggression=*/false);
		Entity->SetDormant(bAnswerReachWasDormant);
		AnswerReachTapsSent = 0;
		ProbeStep = EProbeStep::NightThreeContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::AnswerPairTap:
	{
		// Second beat at 0.42 s: inside the accepted 0.18..0.65 pair.
		if (StepDeadlineSeconds < 0.42f)
		{
			break;
		}
		AIGMissingFloorEvidence* Answer = NightThree
			? NightThree->GetAnswerTarget()
			: nullptr;
		if (!Answer || !Answer->IsInteractionEnabled())
		{
			FailProbe(TEXT("answer surface dropped before the second tap"));
			return;
		}
		if (!NightThree->TryPlayerKnock(Answer, Player.Get()))
		{
			FailProbe(TEXT("dedicated knock verb rejected the second tap"));
			return;
		}
		ProbeStep = EProbeStep::AnswerFinalTap;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::AnswerFinalTap:
	{
		// The 0.82 s rest is deliberately not the shortest accepted value, so
		// timer jitter cannot accidentally collapse the family rhythm.
		if (StepDeadlineSeconds < 0.82f)
		{
			break;
		}
		AIGMissingFloorEvidence* Answer = NightThree
			? NightThree->GetAnswerTarget()
			: nullptr;
		if (!Answer || !Answer->IsInteractionEnabled())
		{
			FailProbe(TEXT("answer surface dropped before the final tap"));
			return;
		}
		if (!NightThree->TryPlayerKnock(Answer, Player.Get()))
		{
			FailProbe(TEXT("dedicated knock verb rejected the final tap"));
			return;
		}
		ProbeStep = EProbeStep::AnswerContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::AnswerContract:
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (!Narrative || !NightPhase)
		{
			FailProbe(TEXT("stage lost while waiting on the wall"));
			return;
		}
		// Eight seconds of nothing, then the reply, T9, and dawn. The final
		// choice must stand unlocked afterwards: T6, T7 and T9 are all in.
		const bool bAnswered =
			Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer);
		if (bAnswered)
		{
			// §8 밤3 does not end at the wall either. T9 arms 비트 3-7 and the
			// hour has to still be running, with him standing in the corridor.
			if (!NightPhase->IsHourActive())
			{
				FailProbe(TEXT("the answer released her to dawn from the annex"));
				return;
			}
			if (!NightThree
				|| NightThree->GetReturnStage()
					!= EIGNightThreeReturnStage::Passing
				|| !NightThree->IsFigureInCorridor())
			{
				FailProbe(FString::Printf(
					TEXT("§8 비트 3-7 did not arm on T9: stage=%d corridor=%d"),
					NightThree
						? static_cast<int32>(NightThree->GetReturnStage())
						: -1,
					NightThree && NightThree->IsFigureInCorridor() ? 1 : 0));
				return;
			}
			if (!Narrative->IsFinalChoiceUnlocked())
			{
				FailProbe(TEXT("T6+T7+T9 did not unlock the final choice"));
				return;
			}
			if (!Narrative->IsPuzzleSolved(FName(TEXT("P3"))))
			{
				FailProbe(TEXT("P3 was not booked after the wall was identified"));
				return;
			}
			// Walk her to the west side of him and hand off; the pass itself is
			// the next step, because freezing him needs three taps in real time.
			if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
			{
				const FVector His = NightThree->GetReturnPassPoint();
				PlayerCharacter->TeleportTo(
					His - FVector(150.0f, 0.0f, 0.0f) + FVector(0.0f, 0.0f, 92.0f),
					PlayerCharacter->GetActorRotation(),
					false,
					true);
			}
			AnswerReachTapsSent = 0;
			AnswerReachTapTwoAt = 0.0;
			ProbeStep = EProbeStep::NightThreePassContract;
			StepDeadlineSeconds = 0.0f;
			break;
		}
		if (StepDeadlineSeconds > 12.0f)
		{
			FailProbe(TEXT("the wall never answered"));
			return;
		}
		break;
	}

	case EProbeStep::NightThreePassContract:
	{
		// §8 비트 3-7. 「그가 멈춰 기다리는 옆을 걸어 지나가는」 — the answer she
		// was taught minutes ago, used on a thing standing in her way, and then
		// the walk past it. Nothing forces this; slipping by unheard was always
		// allowed. It simply is not the beat.
		AIGPlayerCharacter* PlayerCharacter = Player.Get();
		if (!PlayerCharacter || !NightThree || !NightPhase)
		{
			FailProbe(TEXT("stage lost during the corridor pass"));
			return;
		}
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now < AnswerReachTapTwoAt)
		{
			break;
		}
		const FVector Here = PlayerCharacter->GetActorLocation();
		if (AnswerReachTapsSent < 3)
		{
			const bool bTaken = PlayerCharacter->OfferAnswerKnock(Here);
			UE_LOG(
				LogTemp,
				Display,
				TEXT("MISSINGFLOOR_N3PASS_TAP tap=%d taken=%d dist=%.0f state=%d"),
				AnswerReachTapsSent + 1,
				bTaken ? 1 : 0,
				FVector::Dist(Here, Entity->GetActorLocation()),
				static_cast<int32>(Entity->GetListenerState()));
			++AnswerReachTapsSent;
			// 둘 — 쉬고 — 하나, at the authored windows.
			AnswerReachTapTwoAt = Now
				+ (AnswerReachTapsSent == 1
					? AIGListenerEntity::AnswerPairMinSeconds
					: AIGListenerEntity::AnswerRestMinSeconds + 0.10);
			break;
		}
		if (Entity->GetListenerState() != EIGListenerState::Waiting)
		{
			FailProbe(FString::Printf(
				TEXT("the answer did not stop him in the corridor: state=%d"),
				static_cast<int32>(Entity->GetListenerState())));
			return;
		}

		// Past him, while he is still listening for the next knock.
		const FVector His = NightThree->GetReturnPassPoint();
		PlayerCharacter->TeleportTo(
			His + FVector(150.0f, 0.0f, 92.0f),
			PlayerCharacter->GetActorRotation(),
			false,
			true);
		ProbeStep = EProbeStep::NightThreeHomeContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::NightThreeHomeContract:
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		AIGPlayerCharacter* PlayerCharacter = Player.Get();
		if (!Narrative || !PlayerCharacter || !NightThree || !NightPhase)
		{
			FailProbe(TEXT("stage lost on the way home from the annex"));
			return;
		}
		if (!NightThree->HasPassedWhileWaiting())
		{
			if (StepDeadlineSeconds > 4.0f)
			{
				FailProbe(TEXT("walking past him while he waited did not book 3-7"));
				return;
			}
			break;
		}
		if (!Narrative->HasBeatPlayed(FName(TEXT("Night3.PassBy"))))
		{
			FailProbe(TEXT("비트 3-7 was not booked once"));
			return;
		}
		// Only her own floor ends the night.
		if (NightThree->GetReturnStage() != EIGNightThreeReturnStage::Home)
		{
			if (!NightPhase->IsHourActive())
			{
				FailProbe(TEXT("night 3 ended while she was still in the corridor"));
				return;
			}
			PlayerCharacter->TeleportTo(
				FVector(60.0f, -120.0f, 992.0f),
				PlayerCharacter->GetActorRotation(),
				false,
				true);
			if (StepDeadlineSeconds > 6.0f)
			{
				FailProbe(TEXT("403 did not close night 3"));
				return;
			}
			break;
		}
		if (NightPhase->IsHourActive() || NightThree->IsFigureInCorridor())
		{
			if (StepDeadlineSeconds > 6.0f)
			{
				FailProbe(FString::Printf(
					TEXT("§8 비트 3-7 did not close: hour=%d corridor=%d"),
					NightPhase->IsHourActive() ? 1 : 0,
					NightThree->IsFigureInCorridor() ? 1 : 0));
				return;
			}
			break;
		}
		if (!Narrative->WasFirstReportMade())
		{
			FailProbe(TEXT("night 3 ended without the 05:30 first report"));
			return;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_N3PASS PASS: T9 armed the walk home, the learned "
				"answer stopped him in the corridor, she passed him while he "
				"waited, and 403 ended the night"));

		if (!NightFour || !NightFour->ValidateFixtures())
		{
			FailProbe(TEXT("night-4 fixtures were not all placed"));
			return;
		}

		// Day three: T7 closed last night, so 황순금 hands the journal over now.
		AIGReadableNote* Journal = NightThree->GetJournalNote();
		if (!Journal || Journal->IsHidden())
		{
			FailProbe(TEXT("journal stayed hidden after T7 by day"));
			return;
		}
		FIGInteractionContext DayContext;
		DayContext.Interactor = Player.Get();
		DayContext.HoldProgress = 1.0f;
		DayContext.TargetActor = Journal;
		IIGInteractable::Execute_CompleteInteraction(Journal, DayContext);
		IIGInteractable::Execute_CompleteInteraction(Journal, DayContext);
		if (!Narrative->HasSource(
			EIGMissingFloorTruth::FiveNightsOfThirst,
			EIGMissingFloorSource::KnockTallyJournal))
		{
			FailProbe(TEXT("journal read did not file the tally record"));
			return;
		}

		// 요구서와 후속 신고를 빠뜨린 채 잠들 수 없어야 한다. 실제 침대의
		// 상호작용을 사용해 자동 문자가 없는 상태도 취침 허가가 아님을 확인한다.
		AIGMissingFloorEvidence* Eviction = NightFour->GetEvictionNotice();
		if (!Eviction || Eviction->IsHidden()
			|| !Eviction->IsInteractionEnabled() || !SleepTarget)
		{
			FailProbe(TEXT("the day-four eviction notice was not available"));
			return;
		}
		FIGInteractionContext Context;
		Context.Interactor = Player.Get();
		Context.HoldProgress = 1.0f;
		const auto IsSleepBlocked = [this, Narrative, &Context]()
		{
			Context.TargetActor = SleepTarget;
			IIGInteractable::Execute_CompleteInteraction(SleepTarget, Context);
			return !NightPhase->IsHourActive() && Narrative->GetNightIndex() == 3
				&& !GetWorldTimerManager().IsTimerActive(NightStartTimer)
				&& !Narrative->WasFifthDawnInterludeCompleted();
		};
		if (Narrative->HasSource(EIGMissingFloorTruth::StillCoveringIt,
			EIGMissingFloorSource::EvictionWarning)
			|| !NightPhase->GetObjectiveText().EqualTo(
				NSLOCTEXT("IGMissingFloor", "DayObjectiveNotice", "403호 문에 붙은 통보문 읽기"))
			|| IGListenerGreybox::NextFirstReportText(*Narrative, NightFour.Get())
				!= IGListenerGreybox::EFirstReportText::None
			|| !IsSleepBlocked())
		{
			FailProbe(TEXT("요구서를 읽지 않았는데 밤4 취침이 허용됨"));
			return;
		}
		Context.TargetActor = Eviction;
		IIGInteractable::Execute_CompleteInteraction(Eviction, Context);
		if (Narrative->HasTruth(EIGMissingFloorTruth::StillCoveringIt))
		{
			FailProbe(TEXT("eviction notice alone confirmed T10"));
			return;
		}
		if (!Narrative->HasSource(EIGMissingFloorTruth::StillCoveringIt,
			EIGMissingFloorSource::EvictionWarning)
			|| !NightPhase->GetObjectiveText().EqualTo(
				NSLOCTEXT("IGMissingFloor", "DayObjectiveReport", "신고 문자 확인하기"))
			|| Narrative->HasBeatPlayed(IGListenerGreybox::EvictionPhotoBeat)
			|| !IsSleepBlocked())
		{
			FailProbe(TEXT("요구서 사진을 보내기 전에 밤4 취침이 허용됨"));
			return;
		}
		// 대기 시간만 건너뛰고 실제 문자 처리로 사진과 마지막 독백을 보낸다.
		const auto AdvanceReportWithoutWaiting = [this]()
		{
			ReportLaneWaitSeconds = IGListenerGreybox::ReportLaneMaxWaitSeconds;
			ReportPendingGapSeconds = 0.0f;
			AdvanceFirstReportTexts();
			GetWorldTimerManager().ClearTimer(ReportTimer);
		};
		AdvanceReportWithoutWaiting();
		if (!Narrative->HasBeatPlayed(IGListenerGreybox::EvictionPhotoBeat)
			|| Narrative->HasBeatPlayed(IGListenerGreybox::EvictionDeadlineBeat)
			|| !IsSleepBlocked())
		{
			FailProbe(TEXT("공사 시한 독백 전에 밤4 취침이 허용됨"));
			return;
		}
		AdvanceReportWithoutWaiting();
		if (!IGListenerGreybox::HasCompletedFirstReportDay(*Narrative))
		{
			FailProbe(TEXT("요구서와 후속 신고를 마쳐도 밤4 취침 조건이 충족되지 않음"));
			return;
		}
		Context.TargetActor = SleepTarget;
		IIGInteractable::Execute_CompleteInteraction(SleepTarget, Context);
		if (!NightPhase->IsHourActive() || Narrative->GetNightIndex() != 4)
		{
			FailProbe(TEXT("sleeping did not begin night 4"));
			return;
		}
		UE_LOG(LogTemp, Display,
			TEXT("MISSINGFLOOR_N4SLEEP PASS: 요구서, 사진 전송, 공사 시한 독백을 마친 뒤 밤4 진입"));
		ProbeStep = EProbeStep::SealedHourUiContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::SealedHourUiContract:
	{
		// §24 즉시 차단 19. 봉쇄된 한 시간 동안 F9 즉시 로드도, 증거 기록
		// 화면도 열리지 않는다. 둘 다 구현돼 있었지만 아무것도 그것을 잠그지
		// 않았고, 잠금 없는 규칙은 다음 사람이 지우면 그만이다.
		//
		// 거부했다는 사실만 보지 않는다. 「아무 일도 일어나지 않았다」는 것은
		// 입력이 끊어졌을 때의 모습이기도 해서, 어떤 거부를 했는지까지 읽는다.
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		AIGPlayerCharacter* PlayerCharacter = Player.Get();
		AIGPlayerController* PlayerController = PlayerCharacter
			? Cast<AIGPlayerController>(PlayerCharacter->GetController())
			: nullptr;
		AIGHorrorHUD* HorrorHUD = PlayerController
			? Cast<AIGHorrorHUD>(PlayerController->GetHUD())
			: nullptr;
		if (!Narrative || !PlayerCharacter || !PlayerController || !HorrorHUD)
		{
			FailProbe(TEXT("sealed-hour UI probe is missing an actor"));
			return;
		}
		if (!Narrative->IsHourSealed())
		{
			FailProbe(TEXT("the hour is not sealed; the gate proves nothing"));
			return;
		}

		// §24 즉시 차단 17도 같은 한 시간에 걸린다. 목표 텍스트는 밤 표시가
		// 켜져 있는 동안 그려지지 않으며, 그 밤 표시는 봉쇄 상태가 그대로
		// 밀어 넣는다. 봉쇄인데 밤 표시가 꺼져 있으면 목표 줄이 다시 나온다.
		const bool bNightPresented = HorrorHUD->IsNightPresentation();

		PlayerController->OpenMissingFloorJournalForTesting();
		const bool bJournalStayedShut = !HorrorHUD->IsMissingFloorJournalVisible();
		// 열렸다면 SetPause(true)가 함께 걸린다. 화면과 시간 두 쪽을 본다.
		const bool bTimeKeptRunning = !UGameplayStatics::IsGamePaused(this);

		// 큐를 본다. 이 거부는 앞선 대사를 밀어내지 않고 뒤에 서므로, 화면에
		// 떠 있는 줄만 읽으면 방금 말한 것이 아니라 아까 말한 것을 읽는다.
		PlayerCharacter->LoadLatestAutosaveForTesting();
		const bool bRestoreRefused = HorrorHUD->HasDialogueLineForTesting(
			TEXT("지금은 되돌릴 수 없어."));
		const FString Refusal =
			HorrorHUD->GetActiveDialogueLineForTesting().ToString();

		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_SEALEDUI journal_shut=%d running=%d ")
			TEXT("restore_refused=%d night_presented=%d line=%s"),
			bJournalStayedShut ? 1 : 0,
			bTimeKeptRunning ? 1 : 0,
			bRestoreRefused ? 1 : 0,
			bNightPresented ? 1 : 0,
			*Refusal);
		if (!bJournalStayedShut || !bTimeKeptRunning || !bRestoreRefused
			|| !bNightPresented)
		{
			FailProbe(TEXT("MISSINGFLOOR_SEALEDUI FAIL"));
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_SEALEDUI PASS"));
		ProbeStep = EProbeStep::NightFourContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::NightFourContract:
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (!Narrative || !NightFour || !NoiseSubsystem)
		{
			FailProbe(TEXT("stage lost entering night 4"));
			return;
		}

		FIGInteractionContext Context;
		Context.Interactor = Player.Get();
		Context.HoldProgress = 1.0f;
		// The mechanically safe order must produce a continuous 0.40 mask and
		// no pressure-alarm branch. Each control remains a separately persisted
		// first activation rather than one three-stage scripted switch.
		for (AIGMissingFloorEvidence* Control : {
			NightFour->GetCleaningDrain(),
			NightFour->GetFloatBypass(),
			NightFour->GetTransferPump(),
		})
		{
			if (!Control || !Control->IsInteractionEnabled())
			{
				FailProbe(TEXT("a P5 cleaning-circuit control was unavailable"));
				return;
			}
			Context.TargetActor = Control;
			IIGInteractable::Execute_CompleteInteraction(Control, Context);
		}
		if (!Narrative->IsNightFourMaskRunning()
			|| !Narrative->IsPuzzleSolved(FName(TEXT("P5")))
			|| Narrative->GetNightFourControlOrder().Num() != 3)
		{
			FailProbe(TEXT("P5 controls did not settle into the running mask"));
			return;
		}
		if (NightFour->WasHydraulicAlarmTriggered())
		{
			FailProbe(TEXT("the safe P5 order triggered the pressure alarm"));
			return;
		}
		if (NightFour->GetPumpLampMask() != 3 || !NightFour->IsWaterMaskPlaying()
			|| NoiseSubsystem->GetMaskingAt(
				AIGMissingFloorNightFourDirector::GetWallBreakLocation())
				< 0.39f)
		{
			FailProbe(TEXT("P5 did not create its audible 0.40 wall mask"));
			return;
		}
		AIGMissingFloorEvidence* Wall = NightFour->GetWallBreakTarget();
		if (!Wall || Wall->IsHidden() || !Wall->IsInteractionEnabled())
		{
			FailProbe(TEXT("P5 completion did not arm the cavity wall"));
			return;
		}
		if (!bNightFourFailureRetryVerified)
		{
			// 엔딩 C는 일반 포획 리셋이나 새벽 완료가 아니라 밤 4 한정 재시도다.
			// 성공 공동 경로보다 먼저 한 번 실행하고, 다음 프로브 구간에서
			// 초기화된 조작부로 P5를 다시 구성한다.
			FailureRetryCaptureCountBefore = Narrative->GetCaptureCount();
			Narrative->SetAggressionTier(3);
			Entity->SetAggressionTier(3);
			if (!NightFour->ResolveFailureEnding()
				|| !NightFour->IsFailureEndingActive()
				|| Narrative->GetEndingChoice() != FName(TEXT("Ending.C"))
				|| !NightPhase || !NightPhase->IsHourActive()
				|| !NightPhase->IsFailureEndingSuspended()
				|| NightFour->IsWaterMaskPlaying()
				|| NoiseSubsystem->GetMaskingAt(
					AIGMissingFloorNightFourDirector::GetWallBreakLocation())
					> 0.01f)
			{
				FailProbe(TEXT("ending C did not suspend the hour and remove its mask"));
				return;
			}
			ProbeStep = EProbeStep::NightFourFailureRetryContract;
			StepDeadlineSeconds = 0.0f;
			return;
		}
		Context.TargetActor = Wall;
		IIGInteractable::Execute_CompleteInteraction(Wall, Context);
		IIGInteractable::Execute_CompleteInteraction(Wall, Context);
		if (Narrative->GetNightFourWallStrikeCount() != 2
			|| Narrative->HasTruth(EIGMissingFloorTruth::StillCoveringIt))
		{
			FailProbe(TEXT("T10 crossed before the third hammer strike"));
			return;
		}
		ProbeStep = EProbeStep::NightFourWallContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::NightFourFailureRetryContract:
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		AIGPrologueWorldScene* Scene = WorldScene.Get();
		if (!Narrative || !NightFour || !NightPhase || !Scene
			|| !NightFour->CompleteFailurePresentationForProbe()
			|| !NightFour->IsFailureRetryEnabled()
			|| !NightFour->RequestFailureRetry())
		{
			FailProbe(TEXT("ending C retry affordance did not complete"));
			return;
		}
		const bool bScopedRollbackPassed =
			!NightFour->IsFailureEndingActive()
			&& !NightFour->IsFailureRetryEnabled()
			&& NightPhase->IsHourActive()
			&& !NightPhase->IsFailureEndingSuspended()
			&& Narrative->GetNightIndex() == 4
			&& Narrative->GetAggressionTier() == 1
			&& Narrative->GetCaptureCount() == FailureRetryCaptureCountBefore + 1
			&& Narrative->GetEndingChoice().IsNone()
			&& Narrative->GetNightFourControlOrder().IsEmpty()
			&& Narrative->GetNightFourWallStrikeCount() == 0
			&& !Narrative->IsNightFourWallOpened()
			&& !Narrative->IsPuzzleSolved(FName(TEXT("P5")))
			&& Narrative->WasFirstReportMade()
			&& Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer)
			&& !Scene->IsMissingFloorCavityOpen();
		if (!bScopedRollbackPassed)
		{
			FailProbe(TEXT("ending C retry erased durable truth or kept night-four state"));
			return;
		}
		bNightFourFailureRetryVerified = true;
		ProbeStep = EProbeStep::NightFourContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::NightFourWallContract:
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		AIGMissingFloorEvidence* Wall = NightFour
			? NightFour->GetWallBreakTarget()
			: nullptr;
		if (!Narrative || !Wall)
		{
			FailProbe(TEXT("night-4 wall stage lost"));
			return;
		}
		FIGInteractionContext Context;
		Context.Interactor = Player.Get();
		Context.TargetActor = Wall;
		Context.HoldProgress = 1.0f;
		IIGInteractable::Execute_CompleteInteraction(Wall, Context);
		if (!Narrative->HasTruth(EIGMissingFloorTruth::StillCoveringIt)
			|| !Narrative->HasBeatPlayed(FName(TEXT("Night4.PowerCut"))))
		{
			FailProbe(TEXT("third strike did not cross T10 and cut power"));
			return;
		}
		IIGInteractable::Execute_CompleteInteraction(Wall, Context);
		IIGInteractable::Execute_CompleteInteraction(Wall, Context);
		AIGPrologueWorldScene* Scene = WorldScene.Get();
		if (!Narrative->IsNightFourWallOpened()
			|| !Scene || !Scene->IsMissingFloorCavityOpen())
		{
			FailProbe(TEXT("five strikes did not remove the real cavity panel"));
			return;
		}
		// The final choices are intentionally held behind the six-second
		// flashlight read, silence, Mok line and harmless entity pass. Waiting
		// here proves the authored sequence can finish without camera automation;
		// the 28-second ceiling also covers all three gaze fallbacks and the
		// complete silence, dialogue, entity-pass and blackout tail.
		if (!NightFour->IsFinalConfrontationComplete())
		{
			if (StepDeadlineSeconds > 28.0f)
			{
				FailProbe(TEXT("night-4 reveal/confrontation sequence stalled"));
			}
			return;
		}
		if (!NightFour->GetEndingATarget()
			|| NightFour->GetEndingATarget()->IsHidden()
			|| !NightFour->GetEndingATarget()->IsInteractionEnabled()
			|| !NightFour->GetEndingBTarget()
			|| NightFour->GetEndingBTarget()->IsHidden()
			|| !NightFour->GetEndingBTarget()->IsInteractionEnabled())
		{
			FailProbe(TEXT("wall discovery did not expose both mourning choices"));
			return;
		}
		if (bEndingCheckpointWrite)
		{
			WriteEndingCheckpointForProbe();
			return;
		}
		Context.TargetActor = NightFour->GetEndingATarget();
		IIGInteractable::Execute_CompleteInteraction(
			NightFour->GetEndingATarget(), Context);
		ProbeStep = EProbeStep::NightFourEndingContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::NightFourEndingContract:
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (!Narrative || Narrative->GetEndingChoice() != FName(TEXT("Ending.A")))
		{
			FailProbe(TEXT("ending A spatial target did not persist its choice"));
			return;
		}
		if (!Narrative->WasFirstReportMade()
			|| !Narrative->WasSecondReportMade()
			|| !Narrative->HasBeatPlayed(FName(TEXT("Night4.SecondReport"))))
		{
			FailProbe(TEXT("ending divergence changed the common report facts"));
			return;
		}
		if (NightPhase && NightPhase->IsHourActive())
		{
			FailProbe(TEXT("ending resolution did not release the building at dawn"));
			return;
		}
		// Exercise the exact v1 -> v2 normalization boundary in memory. This
		// catches both new night-four fields and the old upper-bound bug that
		// used to discard P4 voicemail/notebook/journal sources on restore.
		FIGMissingFloorNarrativeSnapshot RestoreReceipt = Narrative->GetSnapshot();
		RestoreReceipt.SchemaVersion = 1;
		Narrative->RestoreSnapshot(RestoreReceipt);
		const bool bVoicemailRestored = Narrative->HasSource(
			EIGMissingFloorTruth::WaitingForAnAnswer,
			EIGMissingFloorSource::AnswerRhythmVoicemail);
		const bool bNotebookRestored = Narrative->HasSource(
			EIGMissingFloorTruth::WaitingForAnAnswer,
			EIGMissingFloorSource::AnswerRhythmNotebook);
		const bool bRestoreContractPassed =
			Narrative->GetSnapshot().SchemaVersion
				== UIGMissingFloorNarrativeSubsystem::SnapshotSchemaVersion
			&& Narrative->GetNightFourControlOrder().Num() == 3
			&& Narrative->GetNightFourWallStrikeCount() == 5
			&& Narrative->IsNightFourWallOpened()
			&& Narrative->GetEndingChoice() == FName(TEXT("Ending.A"))
			&& Narrative->WasFirstReportMade()
			&& Narrative->WasSecondReportMade()
			&& bVoicemailRestored
			&& bNotebookRestored;
		if (!bRestoreContractPassed)
		{
			FailProbe(FString::Printf(
				TEXT("v2 restore mismatch: schema=%d controls=%d strikes=%d wall=%d ending=%s reports=%d/%d p4=%d/%d"),
				Narrative->GetSnapshot().SchemaVersion,
				Narrative->GetNightFourControlOrder().Num(),
				Narrative->GetNightFourWallStrikeCount(),
				Narrative->IsNightFourWallOpened() ? 1 : 0,
				*Narrative->GetEndingChoice().ToString(),
				Narrative->WasFirstReportMade() ? 1 : 0,
				Narrative->WasSecondReportMade() ? 1 : 0,
				bVoicemailRestored ? 1 : 0,
				bNotebookRestored ? 1 : 0));
			return;
		}
		// 시작 여부는 IsActive가 아니라 엔딩 이름으로 본다. 앞 단계들이
		// 느리게 흐르면 87초가 이미 지나 스스로 끝나 있을 수도 있고, 그건
		// 결함이 아니라 정상 종료다.
		if (!Epilogue || Epilogue->GetEndingId() != FName(TEXT("Ending.A")))
		{
			FailProbe(TEXT("epilogue did not start with the ending choice"));
			return;
		}
		ProbeStep = EProbeStep::EpilogueContract;
		StepDeadlineSeconds = 0.0f;
		break;
	}

	case EProbeStep::EpilogueContract:
	{
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (!Narrative || !Epilogue)
		{
			FailProbe(TEXT("epilogue contract lost its fixtures"));
			return;
		}
		// 87초를 기다리지 않고 남은 큐를 전부 흘린다. 끝나면 액터가 스스로
		// 비활성이 되고 HUD의 마지막 카드도 걷혀 있어야 한다.
		if (Epilogue->IsActive() && !Epilogue->CompleteImmediatelyForProbe())
		{
			FailProbe(TEXT("epilogue did not finish when its cues were flushed"));
			return;
		}
		if (Epilogue->IsActive())
		{
			FailProbe(TEXT("epilogue stayed active after its last cue"));
			return;
		}
		// 몽타주 · 공방 · 가을 · 보도 · 마지막 카드.
		if (Epilogue->GetPlayedSceneCount() != 5)
		{
			FailProbe(FString::Printf(
				TEXT("epilogue played %d scenes, expected 5"),
				Epilogue->GetPlayedSceneCount()));
			return;
		}
		if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
		{
			if (AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD()))
			{
				if (Hud->IsMissingFloorEpilogueVisible())
				{
					FailProbe(TEXT("epilogue left its last card on screen"));
					return;
				}
			}
		}

		// §22.3: 목격은 서로 독립이고, 어떤 교차에도 들어가지 않는다.
		const int32 TruthsBefore = Narrative->GetConfirmedTruthCount();
		const EIGMissingFloorWitness Witnesses[] = {
			EIGMissingFloorWitness::SeoSleepingPills,
			EIGMissingFloorWitness::HwangWaterBowl,
			EIGMissingFloorWitness::BoothSoundproofing,
			EIGMissingFloorWitness::RooftopCigarettePack,
			EIGMissingFloorWitness::BoothWallCalendar,
			EIGMissingFloorWitness::RecorderEmptyBay,
			EIGMissingFloorWitness::AnnexWorkGlove,
			EIGMissingFloorWitness::StoreNightRoster,
			EIGMissingFloorWitness::Unit401DoorRadio,
			EIGMissingFloorWitness::Unit402Silence,
			EIGMissingFloorWitness::RoofDoorWind,
			EIGMissingFloorWitness::BoothInnerRoomHum,
		};
		for (const EIGMissingFloorWitness Witness : Witnesses)
		{
			Narrative->RecordWitness(Witness);
			if (!Narrative->HasWitness(Witness))
			{
				FailProbe(TEXT("an optional sighting did not persist"));
				return;
			}
		}
		if (Narrative->GetWitnessCount() != UE_ARRAY_COUNT(Witnesses))
		{
			FailProbe(FString::Printf(
				TEXT("witness count %d, expected %d"),
				Narrative->GetWitnessCount(),
				static_cast<int32>(UE_ARRAY_COUNT(Witnesses))));
			return;
		}
		// 같은 것을 두 번 본다고 두 번 적히지 않는다.
		if (Narrative->RecordWitness(EIGMissingFloorWitness::HwangWaterBowl))
		{
			FailProbe(TEXT("a sighting was recorded twice"));
			return;
		}
		if (Narrative->GetConfirmedTruthCount() != TruthsBefore)
		{
			FailProbe(TEXT("optional sightings changed the confirmed truths"));
			return;
		}
		// 저장을 한 바퀴 돌려도 살아남고, 모르는 이름은 복원에서 버려진다.
		FIGMissingFloorNarrativeSnapshot WitnessReceipt = Narrative->GetSnapshot();
		WitnessReceipt.Night.Witnesses.Add(FName(TEXT("Seen.NotAThingThisBuildKnows")));
		Narrative->RestoreSnapshot(WitnessReceipt);
		if (Narrative->GetWitnessCount() != UE_ARRAY_COUNT(Witnesses)
			|| !Narrative->HasWitness(EIGMissingFloorWitness::BoothSoundproofing))
		{
			FailProbe(TEXT("witness restore dropped or kept the wrong names"));
			return;
		}
		PassProbe();
		break;
	}

	default:
		break;
	}
}

void AIGListenerGreyboxDirector::EmitProbeNoise()
{
	// 0.5 so the bait still carries (0.2 -> ~5 m) even when it lands inside
	// the entity's own knock-masking window; the probe must not depend on
	// the bang cycle's phase.
	NoiseSubsystem->ReportNoise(ProbeNoiseLocation, 0.5f, Player.Get());
}

void AIGListenerGreyboxDirector::FailProbe(const FString& Reason)
{
	GetWorldTimerManager().ClearTimer(ProbeTimer);
	ProbeStep = EProbeStep::Done;
	UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_GREYBOX FAIL: %s"), *Reason);
	RequestExit(true);
}

void AIGListenerGreyboxDirector::PassProbe()
{
	GetWorldTimerManager().ClearTimer(ProbeTimer);
	ProbeStep = EProbeStep::Done;
	UE_LOG(
		LogTemp,
		Display,
		TEXT("MISSINGFLOOR_M6_AUDIO PASS: six buses, score states, title window"));
	UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_GREYBOX PASS"));
	RequestExit(false);
}

void AIGListenerGreyboxDirector::RequestExit(const bool bFailed)
{
	FString ResultPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("IGMissingFloorResultPath="), ResultPath))
	{
		ResultPath.TrimQuotesInline();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ResultPath), true);
		const FString Result = FString::Printf(TEXT("MISSINGFLOOR_GREYBOX %s step=%d\n"),
			bFailed ? TEXT("FAIL") : TEXT("PASS"), static_cast<int32>(ProbeStep));
		if (!FFileHelper::SaveStringToFile(Result, *ResultPath))
		{
			FPlatformMisc::RequestExitWithStatus(false, 2);
			return;
		}
	}
	// 종료 코드를 분명히 남기고 메인 루프를 정상으로 빠져나가야 로그가 끝까지 적힌다.
	FPlatformMisc::RequestExitWithStatus(false, bFailed ? 1 : 0);
}

void AIGListenerGreyboxDirector::RunArrivalProbe()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	AIGPrologueWorldScene* Scene = WorldScene.Get();
	if (!Scene || !PuzzleTwo || !Scene->AuditPlayerClearance(Player.Get(), PuzzleTwo->GetBoothDoor()))
	{
		UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_ARRIVAL FAIL spatial_clearance"));
		RequestExit(true);
		return;
	}
	const UStaticMeshComponent* BoxComponent = ArrivalParcelBox
		? ArrivalParcelBox->GetPresentationMesh()
		: nullptr;
	if (!ArrivalContract) { FailProbe(TEXT("입주 계약서 없음")); return; }
	const UStaticMeshComponent* ContractMesh = ArrivalContract->GetPresentationMesh();
	const FVector ContractCenter = ContractMesh->Bounds.Origin;
	FHitResult ContractSupport;
	FCollisionQueryParams SupportQuery;
	SupportQuery.AddIgnoredActor(ArrivalContract);
	GetWorld()->LineTraceSingleByChannel(ContractSupport, ContractCenter + FVector(0,0,3),
		ContractCenter - FVector(0,0,12), ECC_Visibility, SupportQuery);
	const float PaperGap = ContractCenter.Z - ContractMesh->Bounds.BoxExtent.Z - ContractSupport.ImpactPoint.Z;
	FHitResult ContractRead;
	GetWorld()->LineTraceSingleByChannel(ContractRead, FVector(-85,-135,1040), ContractCenter, ECC_Visibility);
	if (!ContractSupport.bBlockingHit || PaperGap < -.05f || PaperGap > .15f || ContractRead.GetActor() != ArrivalContract)
	{
		UE_LOG(LogTemp, Error, TEXT("ENTRY_PLACEMENT FAIL paper_gap=%.3f support=%s read=%s"),
			PaperGap, *GetNameSafe(ContractSupport.GetComponent()), *GetNameSafe(ContractRead.GetActor()));
		RequestExit(true); return;
	}
	UE_LOG(LogTemp, Display, TEXT("ENTRY_PLACEMENT PASS paper_gap=%.3f readable=1"), PaperGap);
	AIGReadableNote* Labels = NightThree ? NightThree->GetLabelsNote() : nullptr;
	const UStaticMeshComponent* LabelsMesh = Labels ? Cast<UStaticMeshComponent>(Labels->GetRootComponent()) : nullptr;
	FHitResult LabelsRead;
	if (LabelsMesh)
	{
		GetWorld()->LineTraceSingleByChannel(LabelsRead, LabelsMesh->Bounds.Origin + FVector(0,0,30),
			LabelsMesh->Bounds.Origin - FVector(0,0,1), ECC_Visibility);
	}
	if (!LabelsMesh || LabelsRead.GetActor() != Labels
		|| LabelsMesh->Bounds.GetBox().ExpandBy(.5f).Intersect(ContractMesh->Bounds.GetBox()))
	{ FailProbe(TEXT("배송 라벨이 계약서를 덮거나 읽기 판정이 막힘")); return; }
	UE_LOG(LogTemp, Display, TEXT("ENTRY_LABELS PASS separated=1 readable=1"));
	FHitResult ArrivalNoteHit;
	GetWorld()->LineTraceSingleByChannel(ArrivalNoteHit, FVector(-30, -290, 1018), FVector(-30, -236, 1018), ECC_Visibility);
	FHitResult ArrivalListenHit;
	GetWorld()->LineTraceSingleByChannel(ArrivalListenHit, FVector(-30, -290, 1052), FVector(-30, -236, 1052), ECC_Visibility);
	if (!ArrivalUnit402Note || !Unit402Listen
		|| ArrivalNoteHit.GetActor() != ArrivalUnit402Note || ArrivalListenHit.GetActor() != Unit402Listen)
	{
		UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_ARRIVAL FAIL note_trace actor=%s component=%s at=%s listen=%s"),
			*GetNameSafe(ArrivalNoteHit.GetActor()), *GetNameSafe(ArrivalNoteHit.GetComponent()),
			*ArrivalNoteHit.ImpactPoint.ToString(), *GetNameSafe(ArrivalListenHit.GetActor()));
		RequestExit(true);
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_ARRIVAL note_trace=1 listen_trace=1"));
	// 401호 앞은 판정 둘이 한 문에 있다. 아래 호출들은 노크 함수를 직접
	// 부르므로 여기서 실제 눈높이로 겨눠 본다. 서서 정면은 라디오, 서서
	// 손잡이 쪽(피치 약 -31도)과 앉아서 정면은 노크여야 한다.
	{
		FCollisionQueryParams DoorQuery(SCENE_QUERY_STAT(IGArrivalUnit401Aim), false);
		DoorQuery.AddIgnoredActor(Player.Get());
		const FVector StandingEye(-150.0f, -330.0f, 1060.0f);
		const FVector CrouchedEye(-150.0f, -330.0f, 1012.0f);
		FHitResult EarHit;
		FHitResult HandleHit;
		FHitResult CrouchHit;
		GetWorld()->LineTraceSingleByChannel(EarHit, StandingEye,
			FVector(-150.0f, -220.0f, 1060.0f), ECC_Visibility, DoorQuery);
		GetWorld()->LineTraceSingleByChannel(HandleHit, StandingEye,
			FVector(-150.0f, -236.0f, 1004.0f), ECC_Visibility, DoorQuery);
		GetWorld()->LineTraceSingleByChannel(CrouchHit, CrouchedEye,
			FVector(-150.0f, -220.0f, 1012.0f), ECC_Visibility, DoorQuery);
		if (!Unit401Door || !Unit401Radio
			|| EarHit.GetActor() != Unit401Radio
			|| HandleHit.GetActor() != Unit401Door
			|| CrouchHit.GetActor() != Unit401Door)
		{
			UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_ARRIVAL FAIL unit401_aim ear=%s handle=%s crouch=%s"),
				*GetNameSafe(EarHit.GetActor()), *GetNameSafe(HandleHit.GetActor()),
				*GetNameSafe(CrouchHit.GetActor()));
			RequestExit(true);
			return;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_ARRIVAL unit401_aim knock=1 listen=1"));
	const UMaterialInterface* BoxMaterial = ArrivalParcelBox
		&& BoxComponent
		? BoxComponent->GetMaterial(0)
		: nullptr;
	const bool bCardboardPbr = BoxMaterial
		&& BoxMaterial->GetPathName().Contains(TEXT("M_MovingBoxCardboardUV"));
	const bool bInitialInteractionGate = ArrivalContract
		&& ArrivalContract->IsInteractionEnabled()
		&& ArrivalParcelBox && ArrivalParcelBox->IsInteractionEnabled()
		&& ArrivalNotebookBox && ArrivalNotebookBox->IsInteractionEnabled()
		&& ArrivalVoicemailBox && ArrivalVoicemailBox->IsInteractionEnabled()
		&& ArrivalStoreBell && ArrivalStoreBell->IsInteractionEnabled()
		&& Unit401Door && Unit401Door->IsInteractionEnabled()
		&& ArrivalUnit402Note && ArrivalUnit402Note->IsInteractionEnabled()
		&& ArrivalRoofLock && ArrivalRoofLock->IsInteractionEnabled()
		&& SleepTarget && !SleepTarget->IsInteractionEnabled();
	const bool bSafeEvening = bProductionMode
		&& Narrative
		&& Narrative->GetNightIndex() == 0
		&& Narrative->HasBeatPlayed(FName(TEXT("Arrival.Started")))
		&& !Narrative->IsHourSealed()
		&& NightPhase && !NightPhase->IsHourActive()
		&& Scene && !Scene->IsTheHourSealed()
		&& Entity && Entity->IsDormant();
	// 이사 날 먼저 복도를 둘러봐도 밤1의 세 구간은 다음 방문을 기다려야 한다.
	const FVector BeforeVisit = Player->GetActorLocation();
	const FRotator BeforeLook = Player->GetControlRotation();
	for (const FVector& Point : { FVector(-30, -300, 998), FVector(232, -300, 998), FVector(-300, -305, 998) })
	{
		CaptureTeleportPlayer(Point, 0, 0);
	}
	CaptureTeleportPlayer(BeforeVisit, BeforeLook.Yaw, BeforeLook.Pitch);
	int32 ArmedNightOneZones = 0;
	for (TActorIterator<AIGZoneTrigger> It(GetWorld()); It; ++It)
	{
		if (It->RequiredNightIndex == 1 && !It->WasTriggered())
		{
			++ArmedNightOneZones;
		}
	}
	const bool bNightOneStillArmed = ArmedNightOneZones == 3
		&& !Scene->IsCorridorExtinguisherDropped()
		&& !Narrative->HasBeatPlayed(FName(TEXT("Night1.Sighting")))
		&& !Narrative->HasBeatPlayed(FName(TEXT("Night1.Extinguisher")))
		&& !Narrative->HasBeatPlayed(FName(TEXT("Night1.Unit402Knock")));
	if (!bNightOneStillArmed)
	{
		UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_ARRIVAL FAIL early_night_beat armed=%d"), ArmedNightOneZones);
		RequestExit(true);
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_ARRIVAL night_beats_armed=3 early_beats=0"));
	const bool bBoxPlacement = BoxComponent
		&& BoxComponent->Bounds.Origin.Equals(FVector(20.0f, 70.0f, 924.0f), 2.0f)
		&& BoxComponent->Bounds.BoxExtent.Equals(FVector(24.0f, 19.0f, 24.0f), 2.0f);
	if (!bSafeEvening || !bInitialInteractionGate || !bCardboardPbr || !bBoxPlacement)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("MISSINGFLOOR_ARRIVAL FAIL: safe=%d gate=%d cardboard_pbr=%d placement=%d"),
			bSafeEvening ? 1 : 0,
			bInitialInteractionGate ? 1 : 0,
			bCardboardPbr ? 1 : 0,
			bBoxPlacement ? 1 : 0);
		RequestExit(true);
		return;
	}
	UE_LOG(
		LogTemp,
		Display,
		TEXT("MISSINGFLOOR_ARRIVAL box mesh=%s actor=%s bounds_origin=%s bounds_extent=%s scale=%s visible=%d registered=%d nanite_disabled=%d"),
		BoxComponent && BoxComponent->GetStaticMesh()
			? *BoxComponent->GetStaticMesh()->GetPathName()
			: TEXT("none"),
		ArrivalParcelBox ? *ArrivalParcelBox->GetActorLocation().ToCompactString() : TEXT("none"),
		BoxComponent ? *BoxComponent->Bounds.Origin.ToCompactString() : TEXT("none"),
		BoxComponent ? *BoxComponent->Bounds.BoxExtent.ToCompactString() : TEXT("none"),
		BoxComponent ? *BoxComponent->GetComponentScale().ToCompactString() : TEXT("none"),
		BoxComponent && BoxComponent->IsVisible() ? 1 : 0,
		BoxComponent && BoxComponent->IsRegistered() ? 1 : 0,
		BoxComponent && BoxComponent->bDisallowNanite ? 1 : 0);

	HandleArrivalEvidence(ArrivalRoofLock);
	const bool bRoofFirstSafe = !Narrative->HasBeatPlayed(FName(TEXT("Arrival.Complete"))) && !SleepTarget->IsInteractionEnabled();
	HandleArrivalEvidence(ArrivalStoreBell);
	HandleUnit401Knocked(Unit401Door);
	HandleArrivalEvidence(ArrivalUnit402Note);
	HandleArrivalEvidence(ArrivalVoicemailBox);
	// 먼저 읽은 단서를 다시 확인해도 취침이나 밤 진행이 열리지 않는다.
	FIGInteractionContext Recheck; Recheck.Interactor = Player.Get();
	ArrivalVoicemailBox->CompleteInteraction_Implementation(Recheck);
	if (!ArrivalVoicemailBox->IsInteractionEnabled() || SleepTarget->IsInteractionEnabled()
		|| Narrative->HasBeatPlayed(FName(TEXT("Arrival.Complete"))))
	{ FailProbe(TEXT("입주 단서 재확인 후 진행 단계가 바뀜")); return; }
	HandleArrivalEvidence(ArrivalNotebookBox);
	HandleArrivalEvidence(ArrivalParcelBox);
	const bool bLastClueRequired = !SleepTarget->IsInteractionEnabled();
	HandleArrivalEvidence(ArrivalContract);
	const bool bRereadable = ArrivalContract->IsInteractionEnabled() && ArrivalParcelBox->IsInteractionEnabled()
		&& ArrivalNotebookBox->IsInteractionEnabled() && ArrivalVoicemailBox->IsInteractionEnabled()
		&& ArrivalUnit402Note->IsInteractionEnabled();
	if (!bRereadable) { FailProbe(TEXT("입주 단서를 다시 읽을 수 없음")); return; }
	UE_LOG(LogTemp, Display, TEXT("ENTRY_NARRATIVE PASS legacy_dressing=0 rereadable=5 repeat_progress=0"));
	HandleNeighborhoodDeliveryRead(NeighborhoodDeliveryNote, true);
	const bool bDeliverySaved = Narrative->HasBeatPlayed(FName(TEXT("Neighborhood.Delivery")))
		&& GetNarinCounterLine().EqualTo(NSLOCTEXT("IGMissingFloor", "NarinDeliveryAnswer",
			"작년 여름에 몇 번 맡겼어요. 마지막 건 건물주 아저씨가 찾아가셨고요. 오빠분이 부탁했다고 하던데요."));
	if (!bRoofFirstSafe || !bLastClueRequired || !SleepTarget->IsInteractionEnabled() || !bDeliverySaved)
	{
		UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_ARRIVAL FAIL free_order=%d last_clue=%d sleep=%d delivery=%d"),
			bRoofFirstSafe, bLastClueRequired, SleepTarget->IsInteractionEnabled(), bDeliverySaved);
		RequestExit(true);
		return;
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("MISSINGFLOOR_ARRIVAL PASS props=7 night=0 hour_sealed=0 cardboard_pbr=1 free_order=1 delivery_branch=1"));
	if (FParse::Param(FCommandLine::Get(), TEXT("IGArrivalSaveWrite")))
	{
		// 마지막 단서까지 비동기 저장이 끝난 뒤 프로세스를 종료한다.
		const double StartedAt = FPlatformTime::Seconds();
		GetWorldTimerManager().SetTimer(SetupTimer, FTimerDelegate::CreateWeakLambda(this, [this, StartedAt]()
		{
			UIGSaveSubsystem* Save = GetGameInstance()->GetSubsystem<UIGSaveSubsystem>();
			if (!Save || FPlatformTime::Seconds() - StartedAt > 30.0)
			{
				GetWorldTimerManager().ClearTimer(SetupTimer);
				FailProbe(TEXT("입주 조사 결과의 자동 저장이 끝나지 않음"));
			}
			else if (!Save->IsBusy())
			{
				GetWorldTimerManager().ClearTimer(SetupTimer);
				RequestExit(!Save->HasCompatibleAutosave());
			}
		}), 0.1f, true);
		return;
	}
	RequestExit(false);
}

void AIGListenerGreyboxDirector::StartArrivalCapture()
{
	if (!bProductionMode || !Player.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_ARRIVAL_CAPTURE FAIL: production stage missing"));
		RequestExit(true);
		return;
	}
	Player->SetCameraMotionEnabled(false);
	if (GEngine)
	{
		GEngine->Exec(GetWorld(), TEXT("DisableAllScreenMessages"));
	}
	ArrivalCaptureStep = 0;
	if (FParse::Param(FCommandLine::Get(), TEXT("IGTrailerCapture")))
	{
		// 트레일러는 프레임마다 카메라를 옮기고 한 장씩 찍는다. -UseFixedTimeStep과 함께 돌린다.
		TWeakObjectPtr<AIGListenerGreyboxDirector> WeakThis(this);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
		{
			return WeakThis.IsValid() && WeakThis->AdvanceTrailerCapture();
		}));
		return;
	}
	// 근접 사진은 입주 자막과 시작 위치 보정이 끝난 뒤 찍는다.
	const bool bDetailScreens = (FParse::Param(FCommandLine::Get(), TEXT("IGReadingReview")) ||
		FParse::Param(FCommandLine::Get(), TEXT("IGReadmeCapture"))) && !bCaptureMetricsOnly;
	if (!bDetailScreens) AdvanceArrivalCapture();
	GetWorldTimerManager().SetTimer(
		ArrivalCaptureTimer,
		this,
		&AIGListenerGreyboxDirector::AdvanceArrivalCapture,
		1.4f,
		true,
		bDetailScreens ? 8.4f : -1.f);
}

bool AIGListenerGreyboxDirector::AdvanceTrailerCapture()
{
	// 장면 표. Night가 0이면 입주 날 저녁, 그 밖에는 그 밤의 04:30이다. bCamera가
	// 거짓이면 플레이어 시점이라 손전등과 손떨림이 같이 찍힌다. Entity는 0이면
	// 잠들고, 1이면 A에 세워 두고, 2면 A에서 B로 기어 온다.
	struct FTrailerShot
	{
		const TCHAR* Name;
		int32 Night;
		bool bCamera;
		FVector From;
		FVector To;
		FRotator RotFrom;
		FRotator RotTo;
		float Fov;
		float Seconds;
		bool bTorch;
		int32 EntityMode;
		FVector EntityA;
		FVector EntityB;
		float EntityYaw;
	};
	// 예고편은 짧게 끊지 않고 걷는 사람의 시선으로 길게 간다(2026-09-29 재편집).
	static const FTrailerShot Shots[] = {
		{TEXT("alley"), 0, false, {705,-505,98}, {795,-508,98}, {1,-2,0}, {2,1,0}, 90, 5.0f, false, 0, {}, {}, 0},
		{TEXT("bedroom-dusk"), 0, false, {80,-100,998}, {70,-92,998}, {-20,132,0}, {-15,146,0}, 90, 5.0f, false, 0, {}, {}, 0},
		// 엘리베이터 앞에서 서쪽으로 걸으며 왼쪽 현관문들을 흘끗 본다.
		{TEXT("corridor-day"), 0, false, {430,-310,998}, {170,-300,998}, {-4,180,0}, {-5,171,0}, 90, 7.0f, false, 0, {}, {}, 0},
		// 402호 문. 스티커에서 메모를 지나 떼지 않은 전단까지 훑어 내려간다.
		{TEXT("door-402"), 0, false, {-38,-302,998}, {-36,-297,998}, {-10,93,0}, {-40,89,0}, 90, 4.5f, false, 0, {}, {}, 0},
		{TEXT("bedroom-night"), 2, false, {80,-100,998}, {84,-104,998}, {-14,130,0}, {26,138,0}, 90, 6.0f, false, 0, {}, {}, 0},
		{TEXT("corridor-night"), 2, false, {380,-305,998}, {200,-305,998}, {-6,180,0}, {-4,176,0}, 90, 7.0f, true, 0, {}, {}, 0},
		{TEXT("listener-approach"), 2, false, {430,-305,998}, {450,-305,998}, {-9,180,0}, {-11,180,0}, 90, 7.0f, true, 2, {-280,-305,960}, {120,-305,960}, 0},
		// 순찰 끝이 플레이어에 닿아 실제로 잡힌다. 다가오는 몸과 포획, 끊기는 순간까지 한 번에 찍힌다.
		// 순찰이 짧아야 기다림 없이 닿는다. 순찰 끝(400)이 플레이어(430) 바로 앞이다.
		{TEXT("capture-front"), 2, false, {430,-305,998}, {436,-305,998}, {-9,180,0}, {-12,180,0}, 90, 7.0f, true, 2, {150,-305,960}, {400,-305,960}, 0},
		// 4층 참 난간에서 3.5층 참을 내려다본다. 그는 북쪽 벽에 귀를 대고 있다.
		{TEXT("stair-landing"), 2, false, {-522,-272,997}, {-522,-268,997}, {-38,90,0}, {-34,92,0}, 90, 4.0f, true, 3, {}, {}, 90},
		{TEXT("meter-cabinet"), 2, false, {505,-300,92}, {515,-296,92}, {-10,-62,0}, {-8,-58,0}, 90, 3.5f, true, 0, {}, {}, 0},
		{TEXT("booth-cctv"), 2, false, {165,-190,98}, {165,-178,96}, {-35,90,0}, {-31,90,0}, 90, 4.0f, true, 0, {}, {}, 0},
	};
	constexpr float FrameRate = 30.0f;

	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!Controller || !PlayerCharacter)
	{
		FailProbe(TEXT("트레일러 촬영 초기화 실패"));
		return false;
	}
	// -IGTrailerShot=a,b 처럼 이름을 주면 그 장면만 다시 찍는다.
	FString Only;
	FParse::Value(FCommandLine::Get(), TEXT("IGTrailerShot="), Only, false);
	TArray<FString> OnlyNames;
	Only.ParseIntoArray(OnlyNames, TEXT(","));
	while (TrailerShotIndex < UE_ARRAY_COUNT(Shots)
		&& OnlyNames.Num() > 0 && !OnlyNames.Contains(Shots[TrailerShotIndex].Name))
	{
		++TrailerShotIndex;
	}
	if (TrailerShotIndex >= UE_ARRAY_COUNT(Shots))
	{
		UE_LOG(LogTemp, Display, TEXT("TRAILER_CAPTURE PASS dropped=%d"), TrailerDroppedFrames);
		RequestExit(TrailerDroppedFrames > 0);
		return false;
	}
	const FTrailerShot& Shot = Shots[TrailerShotIndex];
	const int32 RecordFrames = FMath::RoundToInt(Shot.Seconds * FrameRate);
	// 첫 장면과 밤이 바뀐 장면은 입주 자막과 밤 제목 카드가 걷힐 때까지 8초 기다린다.
	const bool bLongWarmup = TrailerShotIndex == 0 || !Only.IsEmpty()
		|| Shots[TrailerShotIndex - 1].Night != Shot.Night;
	const int32 WarmupFrames = bLongWarmup ? 240 : 75;

	if (TrailerShotFrame == 0)
	{
		if (AHUD* Hud = Controller->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		if (Shot.Night != TrailerNight && Shot.Night > 0 && NightPhase)
		{
			NightPhase->BeginTheHour(Shot.Night);
			NightPhase->SetHourPaused(true);
			TrailerNight = Shot.Night;
		}
		if (PuzzleTwo && PuzzleTwo->GetBoothDoor())
		{
			PuzzleTwo->GetBoothDoor()->ForceOpenState(true);
		}
		if (Entity)
		{
			Entity->SetDormant(Shot.EntityMode == 0);
			if (Shot.EntityMode == 1 || Shot.EntityMode == 2)
			{
				CaptureParkEntity(Shot.EntityA, Shot.EntityYaw);
			}
			if (Shot.EntityMode == 2)
			{
				Entity->SetPatrolPoints({Shot.EntityA, Shot.EntityB});
			}
			if (Shot.EntityMode == 3)
			{
				Entity->TeleportTo(AIGNightOneBeatDirector::GetSightingStagePoint(),
					FRotator(0.0f, Shot.EntityYaw, 0.0f), false, true);
				Entity->SetPatrolPoints({AIGNightOneBeatDirector::GetSightingStagePoint(),
					AIGNightOneBeatDirector::GetSightingShufflePoint()});
			}
			Entity->SetActorTickEnabled(Shot.EntityMode != 0);
		}
		if (UIGFlashlightComponent* Torch = PlayerCharacter->GetFlashlight())
		{
			Torch->SetAvailable(true);
			Torch->SetOn(Shot.bTorch);
		}
		if (ACameraActor* OldCamera = TrailerCamera.Get())
		{
			OldCamera->Destroy();
		}
		TrailerCamera = nullptr;
		if (Shot.bCamera)
		{
			// 플레이어는 문 닫힌 방에 두어 그의 귀에 걸리지 않게 한다.
			CaptureTeleportPlayer(FVector(40, -70, 998), -90, 0);
			ACameraActor* Camera = World->SpawnActor<ACameraActor>(Shot.From, Shot.RotFrom);
			if (!Camera)
			{
				FailProbe(TEXT("트레일러 카메라 없음"));
				return false;
			}
			Camera->GetCameraComponent()->SetFieldOfView(Shot.Fov);
			Controller->SetViewTarget(Camera);
			TrailerCamera = Camera;
		}
		else
		{
			Controller->SetViewTarget(PlayerCharacter);
			CaptureTeleportPlayer(Shot.From, Shot.RotFrom.Yaw, Shot.RotFrom.Pitch);
		}
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		UE_LOG(LogTemp, Display, TEXT("TRAILER_SHOT %s frames=%d"), Shot.Name, RecordFrames);
	}

	// 준비 프레임 동안에는 시작 자세로 두고 빛과 노출이 가라앉기를 기다린다.
	const int32 RecordIndex = TrailerShotFrame - WarmupFrames;
	// 그는 소리를 들어야 움직인다. 노크와 듣기 사이에 멈춰 있는 동안 촬영이 지나가면
	// 장면마다 결과가 달라지므로, 녹화를 시작하는 순간 플레이어 자리에서 소리를 낸다.
	// 붙잡히는 장면은 크게(쫓아온다), 다가오는 장면은 작게(살피러 온다).
	if (RecordIndex == 0 && NoiseSubsystem && Shot.EntityMode == 2)
	{
		const bool bCaptureShot = FCString::Strcmp(Shot.Name, TEXT("capture-front")) == 0;
		NoiseSubsystem->ReportNoiseUnmasked(
			PlayerCharacter->GetActorLocation(), bCaptureShot ? 1.0f : 0.55f, PlayerCharacter);
	}
	const float Alpha = RecordIndex <= 0 ? 0.0f
		: FMath::Clamp(RecordIndex / FMath::Max(1.0f, RecordFrames - 1.0f), 0.0f, 1.0f);
	const float Eased = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
	const FVector Location = FMath::Lerp(Shot.From, Shot.To, Eased);
	FRotator Rotation = FMath::Lerp(Shot.RotFrom, Shot.RotTo, Eased);
	if (!Shot.bCamera)
	{
		// 손에 든 카메라처럼 아주 조금 흔들린다.
		const float Time = TrailerShotFrame / FrameRate;
		Rotation.Pitch += 0.22f * FMath::Sin(Time * 1.7f) + 0.08f * FMath::Sin(Time * 4.3f + 1.1f);
		Rotation.Yaw += 0.26f * FMath::Sin(Time * 1.1f + 0.4f) + 0.07f * FMath::Sin(Time * 3.7f);
	}
	if (ACameraActor* Camera = TrailerCamera.Get())
	{
		Camera->SetActorLocationAndRotation(Location, Rotation);
	}
	else
	{
		PlayerCharacter->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		Controller->SetControlRotation(Rotation);
	}

	if (RecordIndex >= 0 && RecordIndex < RecordFrames)
	{
		if (FScreenshotRequest::IsScreenshotRequested())
		{
			++TrailerDroppedFrames;
			UE_LOG(LogTemp, Warning, TEXT("TRAILER_DROP %s frame=%d"), Shot.Name, RecordIndex);
		}
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(), TEXT("Saved/Trailer"), Shot.Name,
			FString::Printf(TEXT("frame_%04d.png"), RecordIndex)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
	}
	++TrailerShotFrame;
	if (RecordIndex >= RecordFrames)
	{
		++TrailerShotIndex;
		TrailerShotFrame = 0;
	}
	return true;
}

void AIGListenerGreyboxDirector::AdvanceReadingReview()
{
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	AIGHorrorHUD* Hud = Controller ? Cast<AIGHorrorHUD>(Controller->GetHUD()) : nullptr;
	if (!Hud || !NightThree) { FailProbe(TEXT("문서 검사 초기화 실패")); return; }
	float TextScale = 1;
	FParse::Value(FCommandLine::Get(),TEXT("IGCaptionScale="),TextScale);
	const bool LargeText = TextScale > 1.15f;
	// 한국어는 인쇄된 원본 그림이 첫 장이고 큰 글씨면 본문부터 연다. 다른 언어는
	// 번역된 본문이 첫 장이고 원본 그림이 마지막 장이다.
	const int32 LabelFirstPage = AIGHorrorHUD::IsKoreanCulture() && LargeText ? 1 : 0;
	auto Check = [this](bool Ok, const TCHAR* Name)
	{
		ImmersionReviewFailures += !Ok;
		UE_LOG(LogTemp,Display,TEXT("READING_CHECK %s %s"),Name,Ok?TEXT("PASS"):TEXT("FAIL"));
	};
	auto Key = [Controller](FKey Value)
	{
		for (EInputEvent Event : {IE_Pressed,IE_Released})
		{
			const FInputKeyEventArgs Input(nullptr,FInputDeviceId::CreateFromInternalId(0),Value,
				Event,Event==IE_Pressed?1.f:0.f,false,FPlatformTime::Cycles64());
			Controller->InputKey(Input);
		}
	};
	auto Open = [this](AIGReadableNote* Note)
	{
		if (AIGReadableNote* Previous = AIGReadableNote::GetOpenNote()) Previous->Close();
		FIGInteractionContext Context; Context.Interactor = Player.Get();
		Note->CompleteInteraction_Implementation(Context);
	};
	switch (ArrivalCaptureStep++)
	{
	case 0:
		CaptureTeleportPlayer(FVector(40,-70,998),-85,0);
		Open(NightThree->GetLabelsNote());
		break;
	case 1:
		Check(NightThree->GetLabelsNote()->GetReadingArtwork()!=nullptr,TEXT("label_uses_installed_artwork"));
		Check(Hud->GetNotePageIndex()==LabelFirstPage,TEXT("first_page_respects_text_size"));
		Check(Hud->IsNoteTextWithinPaper(),TEXT("label_text_inside_paper"));
		CaptureShot(TEXT("reading-label"));
		break;
	case 2:
		Key(LabelFirstPage==1?EKeys::Left:EKeys::Right);
		break;
	case 3:
		Check(Hud->GetNotePageIndex()==1-LabelFirstPage,TEXT("keyboard_turns_page"));
		Check(Hud->IsNoteTextWithinPaper(),TEXT("label_second_page_inside_paper"));
		CaptureShot(TEXT("reading-label-alternate"));
		break;
	case 4:
		Open(NightThree->GetJournalNote());
		break;
	case 5:
		Check(Hud->GetNotePageIndex()==0,TEXT("another_document_starts_at_first_page"));
		Check(!LargeText || Hud->GetNotePageCount()>1,TEXT("large_type_paginates_long_clue"));
		Check(Hud->IsNoteTextWithinPaper(),TEXT("journal_first_page_inside_paper"));
		CaptureShot(TEXT("reading-journal"));
		break;
	case 6:
		Key(EKeys::Gamepad_DPad_Right);
		break;
	case 7:
		Check(Hud->GetNotePageIndex()==FMath::Min(1,Hud->GetNotePageCount()-1),TEXT("gamepad_turns_page"));
		Check(Hud->IsNoteTextWithinPaper(),TEXT("journal_page_inside_paper"));
		CaptureShot(*FString::Printf(TEXT("reading-journal-%d"),Hud->GetNotePageIndex()+1));
		break;
	case 8:
		if (Hud->GetNotePageIndex()+1<Hud->GetNotePageCount())
		{
			Key(EKeys::MouseScrollDown);
		}
		else ArrivalCaptureStep = 10;
		break;
	case 9:
		Check(Hud->IsNoteTextWithinPaper(),TEXT("wheel_page_inside_paper"));
		CaptureShot(*FString::Printf(TEXT("reading-journal-%d"),Hud->GetNotePageIndex()+1));
		ArrivalCaptureStep=8;
		break;
	case 10:
		Key(EKeys::Right);
		break;
	case 11:
		Check(Hud->GetNotePageIndex()==Hud->GetNotePageCount()-1,TEXT("last_page_does_not_skip_or_wrap"));
		Open(NightThree->GetJournalNote());
		break;
	case 12:
		Check(Hud->GetNotePageIndex()==0,TEXT("reopening_resets_page"));
		Open(NightThree->GetTunerNotebook());
		break;
	case 13:
		Check(Hud->IsNoteTextWithinPaper(),TEXT("tuning_clue_inside_paper"));
		CaptureShot(TEXT("reading-tuning"));
		break;
	case 14:
		// 탁자 위 휴대폰. 중고 거래 글이 폰 화면 폭 안에서 줄을 바꾸는지 본다.
		Check(UsedListingNote != nullptr,TEXT("phone_listing_exists"));
		if (UsedListingNote) Open(UsedListingNote.Get());
		else ArrivalCaptureStep = 16;
		break;
	case 15:
		Check(AIGReadableNote::GetOpenNote()==UsedListingNote.Get(),TEXT("phone_listing_opens"));
		CaptureShot(TEXT("reading-phone"));
		break;
	case 16:
		if (AIGReadableNote* Note=AIGReadableNote::GetOpenNote()) Note->Close();
		GetWorldTimerManager().ClearTimer(ArrivalCaptureTimer);
		UE_LOG(LogTemp,Display,TEXT("READING_REVIEW %s failures=%d"),
			ImmersionReviewFailures?TEXT("FAIL"):TEXT("PASS"),ImmersionReviewFailures);
		RequestExit(ImmersionReviewFailures!=0);
		break;
	}
}

void AIGListenerGreyboxDirector::AdvanceArrivalCapture()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("IGReadmeCapture")))
	{
		// 배포 소개에 쓰는 장면은 실제 입주 경로와 야간 조명으로 다시 찍는다.
		struct FReadmeView { const TCHAR* Name; FVector Position; float Yaw; float Pitch; };
		const FReadmeView Views[] = {
			{TEXT("game-bedroom"), {80,-100,998}, 132,-20},
			{TEXT("game-corridor-day"), {190,-305,998}, 180,-4},
			{TEXT("game-alley"), {700,-520,98}, 0,0},
			{TEXT("game-store"), {2760,-285,98}, 138,-4},
			{TEXT("game-corridor-night"), {190,-305,998}, 180,-4},
			{TEXT("game-booth"), {165,-190,98}, 90,-35},
			{TEXT("game-bedroom-dawn"), {80,-100,998}, 132,-20},
			{TEXT("game-alley-dawn"), {700,-520,98}, 0,0},
		};
		const int32 Step = ArrivalCaptureStep++;
		const int32 Index = Step / 6;
		if (Index < UE_ARRAY_COUNT(Views))
		{
			if (Step % 6 == 0)
			{
				if (Index == 4)
				{
					NightPhase->BeginTheHour(2);
					if (Entity) Entity->SetDormant(true);
				}
				if (Index == 5 && PuzzleTwo && PuzzleTwo->GetBoothDoor())
				{
					PuzzleTwo->GetBoothDoor()->ForceOpenState(true);
				}
				if (Index == 6)
				{
					// 실제 밤 종료 경로로 아침을 촬영한다. 하늘만 바꾸면 검증이 안 된다.
					NightPhase->SuppressNextMorningPresentation();
					NightPhase->CompleteNightGoal();
				}
				const FReadmeView& View = Views[Index];
				CaptureTeleportPlayer(View.Position, View.Yaw, View.Pitch);
				if (UIGFlashlightComponent* Torch = Player->GetFlashlight())
				{
					Torch->SetAvailable(true);
					Torch->SetOn(Index == 4 || Index == 5);
				}
			}
			if (Step % 6 == 5) CaptureShot(Views[Index].Name);
		}
		else
		{
			GetWorldTimerManager().ClearTimer(ArrivalCaptureTimer);
			UE_LOG(LogTemp, Display, TEXT("README_CAPTURE PASS shots=8 production=1"));
			RequestExit(false);
		}
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("IGReadingReview")))
	{
		AdvanceReadingReview();
		return;
	}
	// 같은 인쇄면을 문 안팎과 열린 상태에서 본다. 정면 사진만으로 놓쳤던
	// 부착 방향·두께·문짝 추종을 실제 컴포넌트에서도 확인한다.
	if (FParse::Param(FCommandLine::Get(), TEXT("IGDoorPrintAudit")))
	{
		AIGSwingDoor* EntryDoor = nullptr;
		UStaticMeshComponent* Magnet = nullptr;
		for (TActorIterator<AIGSwingDoor> It(GetWorld()); It; ++It)
		{
			TArray<UStaticMeshComponent*> Parts;
			It->GetComponents(Parts);
			for (UStaticMeshComponent* Part : Parts)
			{
				if (Part->ComponentHasTag(TEXT("Visual.DeliveryMagnet")))
				{
					EntryDoor = *It;
					Magnet = Part;
				}
			}
		}
		if (!EntryDoor || !Magnet || !Magnet->GetStaticMesh())
		{
			UE_LOG(LogTemp, Error, TEXT("DOOR_PRINT_CAPTURE FAIL missing_magnet"));
			RequestExit(true);
			return;
		}
		switch (ArrivalCaptureStep++ - 8)
		{
		case 2:
		{
			EntryDoor->ForceOpenState(false);
			Magnet->UpdateComponentToWorld();
			const FVector Size = Magnet->GetStaticMesh()->GetBoundingBox().GetSize();
			const FVector PrintNormal = Magnet->GetComponentTransform().TransformVectorNoScale(FVector(0, -1, 0));
			const bool bGeometryOK = FMath::IsNearlyEqual(Size.X, 9.f, .02f)
				&& FMath::IsNearlyEqual(Size.Y, .06f, .01f) && FMath::IsNearlyEqual(Size.Z, 6.5f, .02f);
			if (!bGeometryOK || PrintNormal.Y > -.99f || Magnet->GetRelativeLocation().X <= 2.5f
				|| Magnet->GetAttachParent() != EntryDoor->GetDoorPivot()
				|| Magnet->GetCollisionEnabled() != ECollisionEnabled::NoCollision || Magnet->CastShadow)
			{
				UE_LOG(LogTemp, Error, TEXT("DOOR_PRINT_CAPTURE FAIL size=%s normal=%s"), *Size.ToString(), *PrintNormal.ToString());
				RequestExit(true);
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("DOOR_PRINT_GEOMETRY PASS size_cm=%s outward=1 attached=1 collision=0 shadow=0"), *Size.ToString());
			CaptureTeleportPlayer(FVector(108, -301, 998), 90, -21);
			break;
		}
		case 5: CaptureShot(TEXT("door-print-outside")); break;
		case 6: CaptureTeleportPlayer(FVector(70, -100, 998), -64, -14); break;
		case 9: CaptureShot(TEXT("door-print-inside")); break;
		case 10:
			EntryDoor->ForceOpenState(true);
			CaptureTeleportPlayer(FVector(-15, -289, 998), 21, -18);
			break;
		case 13:
			Magnet->UpdateComponentToWorld();
			if (!FMath::IsNearlyEqual(EntryDoor->GetDoorPivot()->GetRelativeRotation().Yaw, -95.0f, .1f)
				|| !Magnet->GetComponentLocation().Equals(EntryDoor->GetDoorPivot()->GetComponentTransform().TransformPosition(Magnet->GetRelativeLocation()), .01f))
			{
				UE_LOG(LogTemp, Error, TEXT("DOOR_PRINT_CAPTURE FAIL open_door yaw=%.2f"), EntryDoor->GetDoorPivot()->GetRelativeRotation().Yaw);
				RequestExit(true);
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("DOOR_PRINT_SWING PASS yaw=%.2f magnet=%s"), EntryDoor->GetDoorPivot()->GetRelativeRotation().Yaw, *Magnet->GetComponentLocation().ToString());
			CaptureShot(TEXT("door-print-open"));
			break;
		case 14:
			EntryDoor->ForceOpenState(false);
			CaptureTeleportPlayer(FVector(145, -310, 998), 90, -65);
			break;
		case 17: CaptureShot(TEXT("door-print-floor")); break;
		case 18: CaptureTeleportPlayer(FVector(236, -280, 998), -90, -12); break;
		case 21: CaptureShot(TEXT("door-print-fire-corridor")); break;
		case 22: CaptureTeleportPlayer(FVector(254, -330, 98), 90, -12); break;
		case 25: CaptureShot(TEXT("door-print-fire-pilotis")); break;
		case 26:
		{
			// 이전 에셋을 별도 검수 위치에만 놓는다. 본편에는 새 발신기 세트만 있다.
			AActor* Sample = GetWorld()->SpawnActor<AActor>();
			UStaticMeshComponent* Legacy = NewObject<UStaticMeshComponent>(Sample);
			Sample->SetRootComponent(Legacy);
			Sample->AddInstanceComponent(Legacy);
			Legacy->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_FireExtinguisherBox.SM_FireExtinguisherBox")));
			Legacy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Legacy->RegisterComponent();
			Sample->SetActorLocationAndRotation(FVector(440, -371, 1005), FRotator(0, 180, 0));
			CaptureTeleportPlayer(FVector(440, -278, 998), -90, -20);
			break;
		}
		case 29: CaptureShot(TEXT("door-print-legacy-fire")); break;
		case 30: CaptureTeleportPlayer(FVector(-140, -115, 998), -90, -4); break;
		case 33: CaptureShot(TEXT("door-print-calendar")); break;
		case 34:
		{
			int32 AttachedNotices = 0;
			TArray<UStaticMeshComponent*> Parts;
			WorldScene->GetComponents(Parts);
			for (UStaticMeshComponent* Part : Parts)
			{
				if (!Part->ComponentHasTag(TEXT("Visual.RentalNotice"))) { continue; }
				const FVector At = Part->GetComponentLocation();
				FHitResult Wall;
				// 실제 벽 충돌을 짧게 쏜다. 빈 틈에 뜬 게시물은 여기서 걸린다.
				if (!GetWorld()->LineTraceSingleByChannel(Wall, At + FVector(0, -.10f, 0), At + FVector(0, .20f, 0), ECC_Visibility))
				{
					UE_LOG(LogTemp, Error, TEXT("DOOR_PRINT_CAPTURE FAIL unsupported_notice=%s"), *At.ToString());
					RequestExit(true);
					return;
				}
				++AttachedNotices;
			}
			if (AttachedNotices != 2)
			{
				UE_LOG(LogTemp, Error, TEXT("DOOR_PRINT_CAPTURE FAIL notice_count=%d"), AttachedNotices);
				RequestExit(true);
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("DOOR_PRINT_SUPPORT PASS wall_contacts=2"));
			CaptureTeleportPlayer(FVector(703, -480, 98), 90, 5);
			break;
		}
		case 37: CaptureShot(TEXT("door-print-rental-front")); break;
		case 38: CaptureTeleportPlayer(FVector(748, -431, 98), 141, 5); break;
		case 41: CaptureShot(TEXT("door-print-rental-side")); break;
		case 42:
		{
			// 글이 보이는 표면과 읽기 판정이 같은 물체에 붙어 있는지도 확인한다.
			FHitResult NoteHit;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(NoteHit,
				FVector(-30, -290, 1018), FVector(-30, -236, 1018), ECC_Visibility);
			if (!bHit || NoteHit.GetActor() != ArrivalUnit402Note)
			{
				UE_LOG(LogTemp, Error, TEXT("DOOR_PRINT_CAPTURE FAIL unreadable_neighbor_note"));
				RequestExit(true);
				return;
			}
			UE_LOG(LogTemp, Display, TEXT("DOOR_PRINT_NOTE PASS visibility_trace=1"));
			CaptureTeleportPlayer(FVector(-30, -310, 998), 90, -30);
			break;
		}
		case 45: CaptureShot(TEXT("door-print-neighbor-note")); break;
		case 46:
			GetWorldTimerManager().ClearTimer(ArrivalCaptureTimer);
			UE_LOG(LogTemp, Display, TEXT("DOOR_PRINT_CAPTURE PASS shots=11 production=1"));
			RequestExit(false);
			break;
		default: break;
		}
		return;
	}
	// 소방 설비·작은 병마개·타일 경계는 가까운 거리와 비스듬한 시점에서도 본다.
	if (FParse::Param(FCommandLine::Get(), TEXT("IGPropFinishAudit")))
	{
		switch (ArrivalCaptureStep++ - 8)
		{
		case 2: CaptureTeleportPlayer(FVector(236, -280, 998), -90, -12); break;
		case 5: CaptureShot(TEXT("finish-fire-panel")); break;
		case 6:
			Player->Crouch();
			CaptureTeleportPlayer(FVector(232, -280, 998), -90, -45);
			break;
		case 9: CaptureShot(TEXT("finish-extinguisher")); break;
		case 10:
			Player->UnCrouch();
			CaptureTeleportPlayer(FVector(3006, -470, 104), 0, 6);
			break;
		case 13: CaptureShot(TEXT("finish-bottle-near")); break;
		case 14: CaptureTeleportPlayer(FVector(2880, -470, 104), 0, 1); break;
		case 17: CaptureShot(TEXT("finish-bottle-far")); break;
		case 18: CaptureTeleportPlayer(FVector(2710, -790, 104), 270, -68); break;
		case 21: CaptureShot(TEXT("finish-store-grout")); break;
		case 22: CaptureTeleportPlayer(FVector(180, -290, 998), 180, -55); break;
		case 25: CaptureShot(TEXT("finish-corridor-grout")); break;
		case 26: CaptureTeleportPlayer(FVector(1950, -1300, 98), 180, -4); break;
		case 29: CaptureShot(TEXT("finish-alley-brick")); break;
		case 30: CaptureTeleportPlayer(FVector(-277.5f, 190, 1297), 270, -65); break;
		case 33: CaptureShot(TEXT("finish-stair-surface")); break;
		case 34:
			GetWorldTimerManager().ClearTimer(ArrivalCaptureTimer);
			UE_LOG(LogTemp, Display, TEXT("PROP_FINISH_CAPTURE PASS shots=8 production=1"));
			RequestExit(false);
			break;
		default: break;
		}
		return;
	}
	// 이전 순회에서 빠졌던 창문과 설비를 플레이어 눈높이에서 확인한다.
	if (FParse::Param(FCommandLine::Get(), TEXT("IGInteriorAudit")))
	{
		switch (ArrivalCaptureStep++ - 8)
		{
		case 0: break;
		case 4: CaptureTeleportPlayer(FVector(-100, 50, 998), 90, -4); break;
		case 5: CaptureShot(TEXT("interior-window-front")); break;
		case 6: CaptureTeleportPlayer(FVector(-175, 100, 998), 55, -18); break;
		case 9: CaptureShot(TEXT("interior-window-side")); break;
		case 10: CaptureTeleportPlayer(FVector(-100, 145, 998), 90, -30); break;
		case 13: CaptureShot(TEXT("interior-window-latch")); break;
		case 14: CaptureTeleportPlayer(FVector(45, 145, 998), -8, -40); break;
		case 17: CaptureShot(TEXT("interior-kitchen-sink")); break;
		case 18:
			PuzzleTwo->GetBoothDoor()->ForceOpenState(true);
			CaptureTeleportPlayer(FVector(185, -170, 98), 180, -15);
			break;
		case 21: CaptureShot(TEXT("interior-pump-panel")); break;
		case 22: CaptureTeleportPlayer(FVector(170, -228, 98), 150, -23); break;
		case 25: CaptureShot(TEXT("interior-pump-side")); break;
		case 26:
		{
			GetNarrative()->SetNightIndex(4);
			NightFour->SetHourActive(true);
			FIGInteractionContext Context;
			Context.Interactor = Player.Get(); Context.HoldProgress = 1.f;
			Context.TargetActor = NightFour->GetTransferPump();
			IIGInteractable::Execute_CompleteInteraction(NightFour->GetTransferPump(), Context);
			CaptureTeleportPlayer(FVector(185,-170,98),180,-15);
			if (NightFour->GetPumpLampMask() != 5) { FailProbe(TEXT("펌프 고장등이 켜지지 않음")); return; }
			break;
		}
		case 28: CaptureShot(TEXT("interior-pump-fault")); break;
		case 35:
		{
			if (NightFour->GetPumpLampMask() != 1) { FailProbe(TEXT("인터록 해제 뒤 고장등이 남음")); return; }
			FIGInteractionContext Context;
			Context.Interactor = Player.Get(); Context.HoldProgress = 1.f;
			for (AIGMissingFloorEvidence* Control : {NightFour->GetCleaningDrain(), NightFour->GetFloatBypass(), NightFour->GetTransferPump()})
			{
				Context.TargetActor = Control;
				IIGInteractable::Execute_CompleteInteraction(Control, Context);
			}
			if (NightFour->GetPumpLampMask() != 3 || !NightFour->IsWaterMaskPlaying()) { FailProbe(TEXT("펌프 운전 표시와 실제 소리가 다름")); return; }
			break;
		}
		case 38: CaptureShot(TEXT("interior-pump-running")); break;
		case 39:
			NightFour->SetHourActive(false);
			if (NightFour->GetPumpLampMask() != 1) { FailProbe(TEXT("시간 종료 뒤 운전등이 남음")); return; }
			CaptureTeleportPlayer(FVector(105, 182, 998), 0, -59);
			break;
		case 43: CaptureShot(TEXT("interior-sink-drain")); break;
		case 44:
			GetWorldTimerManager().ClearTimer(ArrivalCaptureTimer);
			UE_LOG(LogTemp, Display, TEXT("INTERIOR_CAPTURE PASS shots=9 production=1 pump_idle_fault_recovery_run_stop=1"));
			RequestExit(false);
			break;
		default: break;
		}
		return;
	}
	// 새 설비의 앞·옆면을 실제 플레이 화면에서 확인한다.
	if (FParse::Param(FCommandLine::Get(), TEXT("IGFixtureAudit")))
	{
		switch (ArrivalCaptureStep++)
		{
		case 0:
			PuzzleOne->SetHourActive(true);
			PuzzleTwo->SetHourActive(true);
			PuzzleTwo->GetBoothDoor()->ForceOpenState(true);
			CaptureTeleportPlayer(FVector(505, -285, 98), -90, -8);
			break;
		case 5: CaptureTeleportPlayer(FVector(75, -223, 98), 90, -70); break;
		case 7: CaptureShot(TEXT("utility-booth-valve")); break;
		case 8: CaptureTeleportPlayer(FVector(505, -285, 98), -90, -8); break;
		case 9: CaptureShot(TEXT("utility-meter-wide")); break;
		case 10: CaptureTeleportPlayer(FVector(542, -300, 98), -90, -8); break;
		case 13: CaptureShot(TEXT("utility-meter-close")); break;
		case 14: CaptureTeleportPlayer(FVector(150, -189, 98), 90, -34); break;
		case 17: CaptureShot(TEXT("utility-booth-front")); break;
		case 18: CaptureTeleportPlayer(FVector(190, -182, 98), 118, -35); break;
		case 21: CaptureShot(TEXT("utility-booth-side")); break;
		case 22: CaptureTeleportPlayer(FVector(5, 305, 1298), -90, -12); break;
		case 25: CaptureShot(TEXT("utility-tank-steel")); break;
		case 26: CaptureTeleportPlayer(FVector(52, 57, 997), 7, -34); break;
		case 29: CaptureShot(TEXT("utility-countertop")); break;
		case 30:
			CaptureTeleportPlayer(FVector(220, -222, 98), 160, -32);
			break;
		case 33: CaptureShot(TEXT("utility-booth-pump")); break;
		case 34:
			if (FParse::Param(FCommandLine::Get(), TEXT("IGBakeCctv"))) break;
			GetWorldTimerManager().ClearTimer(ArrivalCaptureTimer);
			UE_LOG(LogTemp, Display, TEXT("FIXTURE_CAPTURE PASS shots=8 production=1"));
			RequestExit(false);
			break;
		case 35:
			// 저작용 카메라 위치다. 중력으로 바닥에 떨어지지 않게 이 검사에서만 고정한다.
			Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
			Player->GetCharacterMovement()->StopMovementImmediately();
			Player->SetActorEnableCollision(false);
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{ if (AHUD* HUD = PC->GetHUD()) HUD->bShowHUD = false; }
			CaptureTeleportPlayer(FVector(490, -255, 165), -30, -25);
			break;
		case 38: CaptureShot(TEXT("cctv-source-entrance"), false); break;
		case 39: CaptureTeleportPlayer(FVector(2000, -460, 166), 180, -12); break;
		case 42: CaptureShot(TEXT("cctv-source-parking"), false); break;
		case 43: CaptureTeleportPlayer(FVector(30, -325, 1036), 180, -18); break;
		case 46: CaptureShot(TEXT("cctv-source-stair"), false); break;
		case 47: CaptureTeleportPlayer(FVector(680, -325, 1040), 180, -13); break;
		case 50: CaptureShot(TEXT("cctv-source-corridor"), false); break;
		case 51:
			GetWorldTimerManager().ClearTimer(ArrivalCaptureTimer);
			UE_LOG(LogTemp, Display, TEXT("FIXTURE_CAPTURE PASS shots=12 production=1 atlas_sources=4"));
			RequestExit(false);
			break;
		default: break;
		}
		return;
	}
	switch (ArrivalCaptureStep++)
	{
	case 0:
		CaptureTeleportPlayer(FVector(-120.0f, -5.0f, 997.0f), 34.0f, -24.0f);
		break;
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		// PSO와 셰이더 준비를 마치고 4.6초짜리 챕터 카드가 완전히 사라질 때까지 기다린다.
		break;
	case 8:
		CaptureShot(TEXT("readme/arrival-moving-boxes"));
		break;
	case 9:
		CaptureTeleportPlayer(FVector(-82.0f, -92.0f, 997.0f), -90.0f, -32.0f);
		break;
	case 10:
	case 11:
		break;
	case 12:
		CaptureShot(TEXT("readme/arrival-contract"));
		break;
	case 13:
	case 14:
		break;
	case 15:
	{
		GetWorldTimerManager().ClearTimer(ArrivalCaptureTimer);
		const FString BoxPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(), TEXT("Docs/Media/readme/arrival-moving-boxes.png")));
		const FString ContractPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(), TEXT("Docs/Media/readme/arrival-contract.png")));
		if (!IFileManager::Get().FileExists(*BoxPath)
			|| !IFileManager::Get().FileExists(*ContractPath))
		{
			UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_ARRIVAL_CAPTURE FAIL: screenshot write incomplete"));
			RequestExit(true);
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_ARRIVAL_CAPTURE PASS shots=2 d3d12=1"));
		RequestExit(false);
		break;
	}
	default:
		break;
	}
}

// -- README/night capture tour ---------------------------------------------
//
// README에서 설명하는 시스템을 실제로 찍는 18개 연출 구간이다.
// 기존 데모 캡처와 똑같이 정지 화면은 Docs/Media/<name>.png에,
// 연속 프레임은 Saved/NightCapture/<dir>/frame_%05d.png에 남겨
// ffmpeg GIF 조립 경로를 하나로 유지한다.

void AIGListenerGreyboxDirector::StartNightCapture()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		PlayerController->ConsoleCommand(TEXT("DisableAllScreenMessages"), true);
	}
	// 셰이더가 아직 컴파일 중인 재질은 엔진이 격자로 그린다. 프롤로그 캡처는
	// 이것을 이미 막고 있었지만 밤 캡처는 막지 않아서, 같은 커밋에서도 실행에
	// 따라 격자가 찍힌 프레임과 안 찍힌 프레임이 나왔다.
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	// The dying west fixture must not strobe the stair still.
	if (AIGPrologueWorldScene* SceneNow =
		const_cast<AIGPrologueWorldScene*>(WorldScene.Get()))
	{
		SceneNow->SuspendCorridorFlicker(true);
	}

	int32 StartStep = 0;
	FParse::Value(
		FCommandLine::Get(),
		TEXT("IGNightCaptureStartStep="),
		StartStep);
	EnterCaptureStep(FMath::Clamp(StartStep, 0, 18));
	if (StartStep > 0)
	{
		// A direct art-review stop still begins while the normal night card is
		// fading. Keep timed actions behind that card instead of photographing it.
		CaptureStepSeconds = StartStep == 18 ? -8.0f : -4.0f;
		if (APlayerController* PlayerController =
			World->GetFirstPlayerController())
		{
			if (AHUD* Hud = PlayerController->GetHUD())
			{
				// 포획 검수는 실제 HUD가 연출 중 스스로 숨는지까지 확인한다.
				// 트레일러 촬영(-IGTrailerNoHud)은 화면 글자 없이 찍는다.
				Hud->bShowHUD = StartStep == 18
					&& !FParse::Param(FCommandLine::Get(), TEXT("IGTrailerNoHud"));
			}
		}
	}
	GetWorldTimerManager().SetTimer(
		CaptureTimer,
		this,
		&AIGListenerGreyboxDirector::AdvanceNightCapture,
		0.04f,
		true);
}

namespace IGNightHistogram
{
	/** How the point stages itself before the frame is measured. */
	enum class ESetup : uint8
	{
		/** Torch off, entity parked far away. The corridor as authored. */
		DarkCorridor,
		/** Torch on, looking down the beam. V1's dust motes live here. */
		BeamDust,
		/** Torch off, entity close and lit only by its rim. */
		EntityRim,
		/** Torch on, against the fifth-floor cavity wall. */
		CavityWall,
		/** Torch on, over the settled dust and drag residue on 5F. */
		ResidueFifthFloor,
		/** Torch on, over the corridor runner and the meter box rust. */
		ResidueCorridor,
		/** Chase post-process at full pressure. */
		ChasePost,
		/** A loud noise report, so the ripple ring is on screen. */
		RippleRing,
		/**
		 * Torch on and nothing else. The residue setups also push a dust
		 * disturbance into the air, which is right when the point exists to
		 * show dust and wrong when it exists to read a floor material: the
		 * motes sit between the lens and the surface being judged.
		 */
		SurfaceReading
	};

	struct FPoint
	{
		const TCHAR* Name = TEXT("");
		ESetup Setup = ESetup::DarkCorridor;
		FVector PlayerLocation = FVector::ZeroVector;
		float PlayerYaw = 0.0f;
		float PlayerPitch = 0.0f;
		/** Fraction of pixels below 5% luminance. */
		float ShadowMinimum = 0.0f;
		float ShadowMaximum = 1.0f;
		/** Fraction of pixels above 98% luminance. */
		float HighlightMaximum = 1.0f;
		/**
		 * The HUD is off for every point but one. §11 V4 draws the noise ripple
		 * as a screen-edge arc, so it is a HUD element by design and measuring it
		 * with the HUD hidden would measure an empty corridor instead. Everywhere
		 * else a caption sitting in frame would count its own pixels into both
		 * tails, so it stays off.
		 */
		bool bShowHud = false;
	};

	// 9월 11일부터 복도 서쪽 등 하나만 남는다. 구형 기준은 꺼진 등 아래의
	// 존재와 빛이 없는 동쪽 벽을 측정했다. 남아 있는 등·손전등·출입구가 실제로
	// 보이는 시점을 쓰고, 화면과 측정값을 CIRCUIT_REVIEW_20260917에 함께 남긴다.
	// 9월 29일에 밤 비네트를 0.46에서 0.28로 줄였다. 0.46으로 되돌려 재면 11지점이
	// 9월 28일 값으로 돌아와서, 조명은 그대로이고 화면 가장자리만 밝아졌다는 것을
	// 확인했다(2026-09-30). 벗어난 지점은 그 차이만큼 밴드를 폭 그대로 내렸다.
	const FPoint Points[] =
	{
		{
			// 2026-09-30 실측 0.9825(비네트 0.46일 때 0.9893).
			TEXT("corridor_dark"), ESetup::DarkCorridor,
			FVector(-245.0f, -305.0f, 997.0f), 0.0f, -10.0f,
			0.973f, 0.990f, 0.010f
		},
		{
			// 2026-09-28 실측 0.8209. 9월 17일 기준(0.72) 때 복도 창은 남색 단색이었다.
			// 이제 건너편 빌라가 비치고, 04:30에는 그 집 창이 거의 다 꺼져 있다.
			// 비네트를 줄인 뒤 2026-09-30 실측 0.7705.
			TEXT("beam_dust"), ESetup::BeamDust,
			FVector(-60.0f, -305.0f, 997.0f), 0.0f, -6.0f,
			0.72f, 0.81f, 0.010f
		},
		{
			TEXT("entity_rim"), ESetup::EntityRim,
			// 남아 있는 서쪽 등 X=-180 아래에 존재를 둔다. 검수용 보조광은 없다.
			// 2026-09-30 실측 0.9504(비네트 0.46일 때 0.9579).
			FVector(80.0f, -305.0f, 997.0f), 180.0f, -18.0f,
			0.947f, 0.981f, 0.010f
		},
		{
			// 2026-09-28 실측 0.4207. 9월 17일 기준(0.38)보다 손전등 둘레가 조금
			// 어둡다. 그 사이 창밖 풍경·가로등·옥상 원경을 바꿨고, 층별 조명 구역을
			// 끄고 재도 같은 값이다. 비네트를 줄인 뒤 2026-09-30 실측 0.3562.
			TEXT("cavity_wall"), ESetup::CavityWall,
			FVector(120.0f, 700.0f, 1297.0f), 0.0f, -2.0f,
			0.32f, 0.40f, 0.010f
		},
		{
			// The first stance faced +X/−Y from (60, 760) and put both floor
			// residues *behind* the camera: the frame was the bay wall and the
			// slab's own grain, so raising the drag-trail material moved
			// 0.04/255 of it and the band was satisfied by pixels that had
			// nothing to do with the residue. A point named for a thing it does
			// not contain is the same defect as a brightness floor that black
			// satisfies. From here the camera is 161 cm over the slab
			// (pawn centre + 64) with a 78° lens, and this framing was checked by
			// projecting both residue masks through this exact transform before
			// it was committed: 100% of the drag trail and 99.9% of the dust
			// joint land inside the frame, the trail right of centre as the
			// subject and the joint running the north wall line behind it. X is
			// 208 rather than 215 so the 34 cm capsule clears the bay stud face
			// at 247: the teleport is bNoCheck, and a pawn that has to resolve
			// penetration moves before the shot, which would make this frame
			// unrepeatable for reasons that have nothing to do with lighting.
			// 밴드는 별관 바닥이 제대로 매핑된 뒤에 다시 쟀다. 이전 0.30..0.52는
			// M_ConcreteDark_X가 수평 슬래브에서 텍스처를 한 줄로 늘려 밝은
			// 띠를 만들던 시절의 값이고, 바닥이 실제 콘크리트 분포를 되찾자
			// 0.6387로 올라갔다. 프레임이 바닥으로 가득 찬 시점이라 암부 비율이
			// 높은 것이 정상이다.
			TEXT("residue_fifth_floor"), ESetup::ResidueFifthFloor,
			FVector(208.0f, 600.0f, 1297.0f), 125.0f, -38.0f,
			0.53f, 0.75f, 0.010f
		},
		{
			// 2026-09-28 실측 0.4215. 9월 17일(0.37) 뒤로 복도 창이 남색 단색에서
			// 불 꺼진 건너편 빌라로 바뀌었다. 04:30에는 401호 문 아래 빛줄이
			// 프레임 왼쪽에 들어온다. 비네트를 줄인 뒤 2026-09-30 실측 0.3714.
			TEXT("residue_corridor"), ESetup::ResidueCorridor,
			FVector(80.0f, -300.0f, 997.0f), 190.0f, -34.0f,
			0.33f, 0.41f, 0.010f
		},
		{
			TEXT("chase_post"), ESetup::ChasePost,
			FVector(200.0f, -305.0f, 997.0f), 180.0f, -3.0f,
			0.90f, 0.98f, 0.010f
		},
		{
			// Down the corridor, not across it: at yaw 90 the player is 70 cm
			// from the north wall and the frame is a close-up of plaster, which
			// measured 91% black and told us nothing about the ripple.
			// 2026-09-29: 겨눈 것이 없으면 화면 가운데 괄호를 그리지 않게 되면서
			// 밝은 픽셀이 조금 줄었다(0.9981). 링 자체는 그대로라 위쪽만 넓힌다.
			TEXT("ripple_ring"), ESetup::RippleRing,
			FVector(-40.0f, -305.0f, 997.0f), 0.0f, -3.0f,
			0.97f, 0.999f, 0.010f, /*bShowHud=*/true
		},
		{
			// §11 규칙 2는 「어느 바닥을 고르느냐」를 선택으로 만든다. 그
			// 선택은 표면이 눈으로 구분될 때에만 존재하므로, 소리가 갈리는
			// 두 바닥에도 프레임이 있어야 한다. 계단은 X=-277.5 수직통로를
			// +Y로 오르고, 카메라는 상단 착지에서 -Y로 내려다본다 — 디딤판
			// 윗면이 프레임을 채우는 유일한 각도다.
			// -42°는 계단통 전체를 담았지만 디딤판 하나가 화면에서 8px이라
			// 트레드 무늬를 판정할 수 없었다. -65°는 바로 아래 서너 단을
			// 크게 잡는다 — 밟기 전에 실제로 내려다보는 각도이기도 하다.
			// 실측 0.1130 / 0.1136. 계단통은 좁고 벽이 빛을 되돌려 주어서 밝은
			// 편이다. 0.00~1.00은 저작 전 자리표시였고 그 상태로는 아무것도
			// 걸러내지 못한다.
			TEXT("steel_stair"), ESetup::SurfaceReading,
			FVector(-277.5f, 190.0f, 1297.0f), 270.0f, -65.0f,
			0.02f, 0.21f, 0.010f
		},
		{
			// 옥상 route 슬래브의 상단면은 Z=1200이고 서쪽 구간이 가장 넓다.
			// 계단통과 달리 여기에는 빛을 되돌려 줄 벽이 없다. -38°에서는
			// 프레임의 87%가 5% 미만이었고 빔은 우하단 모서리에만 걸렸다 —
			// 밝기 문제가 아니라 시선과 빔이 만나지 않는 구도의 문제다.
			// 발 앞으로 내리면 빔 안쪽에서 도막을 읽을 수 있다.
			TEXT("rooftop_deck"), ESetup::SurfaceReading,
			// (0,-40)은 360cm 물탱크 받침대 내부라 기존 프레임에는 검은 안쪽 면과
			// 잘린 손전등 모서리만 보였다. 폭 160cm 서쪽 보행 슬래브에서 정비 통로를
			// 따라 바라보도록 옮긴다.
			// 옥상은 빛을 되돌려 줄 벽이 없어서 손전등이 닿는 자리 말고는 전부
			// 떨어진다. 암부 비율이 높은 것이 정상이다. 9월 17일에는 0.5935였는데,
			// 그때 오른쪽을 채우던 남색 판이 불 꺼진 동네 원경으로 바뀌어
			// 2026-09-28 실측 0.7573이다. 비네트를 줄인 뒤 2026-09-30 실측 0.6919.
			FVector(-410.0f, -40.0f, 1297.0f), 90.0f, -34.0f,
			0.66f, 0.86f, 0.010f
		},
		{
			// 옥상에서 동쪽을 본다. 아파트 단지 윤곽이 지평선의 도시 불빛과 해 뜨기
			// 전의 푸른 기를 등지고 검게 서 있어야 한다. 2026-09-28 실측으로 하늘빛
			// 띠가 없을 때 0.998, 띠 세기 0.15에서 0.963, 지금의 0.2에서 0.885였다.
			// 띠가 꺼지거나 원경이 사라지면 위로, 원경이 밝게 깔리면 아래로 벗어난다.
			// 비네트를 줄인 뒤 2026-09-30 실측 0.8576.
			TEXT("rooftop_skyline"), ESetup::DarkCorridor,
			FVector(150.0f, -200.0f, 1297.0f), 0.0f, 2.0f,
			0.81f, 0.90f, 0.010f
		}
	};

	constexpr int32 PointCount = UE_ARRAY_COUNT(Points);

	/** §11 V5 luminance thresholds. */
	constexpr float ShadowThreshold = 0.05f;
	constexpr float HighlightThreshold = 0.98f;

	/**
	 * Lumen and TSR both need frames to converge, and a temporal history that
	 * has not settled reads darker than the authored frame. Measuring early
	 * would quietly pass every shadow floor for the wrong reason.
	 */
	constexpr float SettleSeconds = 4.0f;
	constexpr float TickSeconds = 0.04f;

	/**
	 * How long a requested frame may take to arrive before the sweep gives up.
	 * Without this a run that cannot render at all (a -nullrhi invocation, say)
	 * hangs instead of saying why, and a hang is a worse answer than a failure.
	 */
	constexpr float ShotTimeoutSeconds = 20.0f;
}

void AIGListenerGreyboxDirector::StartHistogramSweep()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// 광량을 재는 동안 구역 연출이 등을 끄거나 포획이 시점을 방으로 돌리지 않게 한다.
	for (TActorIterator<AIGZoneTrigger> It(World); It; ++It) It->SetActorEnableCollision(false);
	if (NightOneBeats) GetWorldTimerManager().ClearAllTimersForObject(NightOneBeats.Get());

	if (APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		PlayerController->ConsoleCommand(TEXT("DisableAllScreenMessages"), true);
		if (AHUD* Hud = PlayerController->GetHUD())
		{
			// The HUD is not part of the lighting claim, and a caption sitting in
			// frame would count its own pixels into both tails.
			Hud->bShowHUD = false;
		}
	}
	// A strobing fixture would make every measurement a coin toss on which
	// frame it landed.
	if (AIGPrologueWorldScene* SceneNow =
		const_cast<AIGPrologueWorldScene*>(WorldScene.Get()))
	{
		SceneNow->SuspendCorridorFlicker(true);
	}

	HistogramFailures = 0;
	HistogramMeasured = 0;
	bHistogramShotPending = false;
	// The screenshot pipeline hands over the frame it actually captured, which
	// is the only frame that exists when no swap chain does.
	HistogramScreenshotHandle =
		UGameViewportClient::OnScreenshotCaptured().AddUObject(
			this,
			&AIGListenerGreyboxDirector::HandleHistogramScreenshot);
	EnterHistogramPoint(0);
	GetWorldTimerManager().SetTimer(
		HistogramTimer,
		this,
		&AIGListenerGreyboxDirector::AdvanceHistogramSweep,
		IGNightHistogram::TickSeconds,
		true);
}

void AIGListenerGreyboxDirector::EnterHistogramPoint(const int32 PointIndex)
{
	HistogramPointIndex = PointIndex;
	HistogramPointSeconds = 0.0f;
	if (!IGNightHistogram::Points || PointIndex < 0
		|| PointIndex >= IGNightHistogram::PointCount)
	{
		return;
	}

	const IGNightHistogram::FPoint& Point = IGNightHistogram::Points[PointIndex];
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	AIGListenerEntity* EntityActor = Entity.Get();
	UIGFlashlightComponent* Torch = PlayerCharacter
		? PlayerCharacter->GetFlashlight()
		: nullptr;
	UWorld* World = GetWorld();

	CaptureTeleportPlayer(Point.PlayerLocation, Point.PlayerYaw, Point.PlayerPitch);

	if (APlayerController* PlayerController = World
		? World->GetFirstPlayerController()
		: nullptr)
	{
		if (AHUD* Hud = PlayerController->GetHUD())
		{
			Hud->bShowHUD = Point.bShowHud;
		}
	}

	// Park the entity out of frame by default; only two points want it visible.
	if (EntityActor)
	{
		EntityActor->SetDormant(true);
		EntityActor->SetActorHiddenInGame(true);
		CaptureParkEntity(FVector(640.0f, -305.0f, 960.0f), 180.0f);
	}
	if (Torch)
	{
		Torch->SetAvailable(true);
		Torch->SetOn(false);
	}
	if (UIGMissingFloorAudioSubsystem* AudioDirector = World
		? World->GetSubsystem<UIGMissingFloorAudioSubsystem>()
		: nullptr)
	{
		AudioDirector->SetThreatState(EIGAudioThreatState::Calm);
	}
	if (UIGStressComponent* Stress = PlayerCharacter
		? PlayerCharacter->GetStress()
		: nullptr)
	{
		Stress->SetThreatPressure(0.0f);
	}

	switch (Point.Setup)
	{
	case IGNightHistogram::ESetup::DarkCorridor:
		break;

	case IGNightHistogram::ESetup::SurfaceReading:
		if (Torch)
		{
			Torch->SetOn(true);
		}
		break;

	case IGNightHistogram::ESetup::BeamDust:
	case IGNightHistogram::ESetup::CavityWall:
	case IGNightHistogram::ESetup::ResidueFifthFloor:
	case IGNightHistogram::ESetup::ResidueCorridor:
		if (Torch)
		{
			Torch->SetOn(true);
		}
		// The residue and beam points want his lane in the air, otherwise the
		// two dust systems are measured without the thing they exist to show.
		if (UIGDustSubsystem* Dust = World
			? World->GetSubsystem<UIGDustSubsystem>()
			: nullptr)
		{
			const FVector Ahead = Point.PlayerLocation
				+ FRotator(0.0f, Point.PlayerYaw, 0.0f).Vector() * 220.0f;
			Dust->ReportDisturbance(Ahead, 1.0f);
			// 실제 발자국은 플레이어 중심에서 88cm 아래에 찍힌다(IGPlayerCharacter).
			// 끌림은 존재의 캡슐 중심 58cm에서 18cm 아래다(IGListenerEntity).
			// 촬영 좌표는 높이 96cm인 플레이어 중심이므로 같은 바닥 높이로 맞춘다.
			constexpr float FootprintDrop = 88.0f;
			constexpr float DragDrop = 96.0f - (58.0f - 18.0f);
			Dust->ReportSettledPrint(
				Ahead - FVector(0.0f, 0.0f, DragDrop),
				Point.PlayerYaw,
				EIGDustPrintKind::Drag);
			Dust->ReportSettledPrint(
				Point.PlayerLocation
					+ FRotator(0.0f, Point.PlayerYaw, 0.0f).Vector() * 120.0f
					- FVector(0.0f, 0.0f, FootprintDrop),
				Point.PlayerYaw,
				EIGDustPrintKind::Footfall);
		}
		break;

	case IGNightHistogram::ESetup::EntityRim:
		// Close enough to fill frame, far enough not to trip the capture radius.
		CaptureParkEntity(
			Point.PlayerLocation
				+ FRotator(0.0f, Point.PlayerYaw, 0.0f).Vector() * 260.0f
				- FVector(0.0f, 0.0f, 39.0f),
			Point.PlayerYaw + 180.0f);
		if (EntityActor) EntityActor->SetActorHiddenInGame(false);
		break;

	case IGNightHistogram::ESetup::ChasePost:
		if (UIGStressComponent* Stress = PlayerCharacter
			? PlayerCharacter->GetStress()
			: nullptr)
		{
			Stress->SetThreatPressure(0.95f);
		}
		if (UIGMissingFloorAudioSubsystem* AudioDirector = World
			? World->GetSubsystem<UIGMissingFloorAudioSubsystem>()
			: nullptr)
		{
			AudioDirector->SetThreatState(EIGAudioThreatState::Chasing);
		}
		break;

	case IGNightHistogram::ESetup::RippleRing:
		// 파문은 짧게 사라진다. 노출이 안정된 뒤 촬영 직전에 소리를 낸다.
		break;

	default:
		break;
	}
}

void AIGListenerGreyboxDirector::AdvanceHistogramSweep()
{
	// While a shot is in flight the delegate owns the sweep. Requesting another
	// would measure one point against another point's frame.
	if (bHistogramShotPending)
	{
		HistogramShotWaitSeconds += IGNightHistogram::TickSeconds;
		if (HistogramShotWaitSeconds < IGNightHistogram::ShotTimeoutSeconds)
		{
			return;
		}
		// Nothing is rendering, so nothing can be measured. Say so instead of
		// waiting forever: a sweep that hangs looks like a slow machine, and a
		// sweep that fails looks like the missing RHI it actually is.
		UE_LOG(
			LogTemp,
			Error,
			TEXT("MISSINGFLOOR_V5 FAIL: no frame arrived for point %s within "
				"%.0f s. The sweep needs a real RHI — run it with "
				"-RenderOffScreen -d3d12 and never with -nullrhi."),
			IGNightHistogram::Points[
				FMath::Clamp(HistogramPointIndex, 0, IGNightHistogram::PointCount - 1)].Name,
			IGNightHistogram::ShotTimeoutSeconds);
		GetWorldTimerManager().ClearTimer(HistogramTimer);
		UGameViewportClient::OnScreenshotCaptured().Remove(
			HistogramScreenshotHandle);
		FPlatformMisc::RequestExit(false);
		return;
	}
	const float PreviousSeconds = HistogramPointSeconds;
	HistogramPointSeconds += IGNightHistogram::TickSeconds;
	const float RippleStartSeconds = IGNightHistogram::SettleSeconds - 0.2f;
	if (HistogramPointIndex >= 0 && HistogramPointIndex < IGNightHistogram::PointCount
		&& PreviousSeconds < RippleStartSeconds && HistogramPointSeconds >= RippleStartSeconds
		&& IGNightHistogram::Points[HistogramPointIndex].Setup == IGNightHistogram::ESetup::RippleRing
		&& NoiseSubsystem)
	{
		// 파문이 나타나는 0.1초를 기다리고, 사라지기 전에 찍는다.
		const IGNightHistogram::FPoint& Point = IGNightHistogram::Points[HistogramPointIndex];
		NoiseSubsystem->SetGlobalMasking(0.0f);
		NoiseSubsystem->ReportNoise(Point.PlayerLocation + FRotator(0.f, Point.PlayerYaw, 0.f).Vector()*90.f, 1.f, Player.Get());
	}
	if (HistogramPointSeconds < IGNightHistogram::SettleSeconds)
	{
		return;
	}
	if (HistogramPointIndex < 0
		|| HistogramPointIndex >= IGNightHistogram::PointCount)
	{
		GetWorldTimerManager().ClearTimer(HistogramTimer);
		UGameViewportClient::OnScreenshotCaptured().Remove(
			HistogramScreenshotHandle);
		return;
	}

	// A frame is kept for every point so the art review can happen later without
	// anyone having to run the engine again to look — and the same captured
	// frame is what gets measured, so the number and the picture always agree.
	//
	// The request carries no filename on purpose. UGameViewportClient writes the
	// PNG **only when nothing is bound** to OnScreenshotCaptured — the engine's
	// own comment is «If delegate subscribed, fire it instead of writing out a
	// file to disk». This sweep must be bound to measure the pixels, so the file
	// is ours to write, and WriteHistogramFrame does it from the very bitmap the
	// numbers came from. Passing a path here instead would silently do nothing.
	bHistogramShotPending = true;
	HistogramShotWaitSeconds = 0.0f;
	// bShowUI stays true: the authored bands were measured from UI-composited
	// frames, and ripple_ring exists precisely to capture a HUD element.
	FScreenshotRequest::RequestScreenshot(/*bInShowUI=*/true);
}

void AIGListenerGreyboxDirector::WriteHistogramFrame(
	const TCHAR* PointName,
	const int32 Width,
	const int32 Height,
	const TArray<FColor>& Colors) const
{
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(
			FPaths::ProjectDir(),
			FString::Printf(TEXT("Docs/Media/v5-%s.png"), PointName)));
	// The bitmap arrives as BGRA8 in sRGB, alpha already forced to 255 by the
	// viewport client before it broadcasts.
	const FImageView Frame(Colors.GetData(), Width, Height);
	if (!FImageUtils::SaveImageByExtension(*Path, Frame))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("MISSINGFLOOR_V5 FAIL: point %s measured but its frame could "
				"not be written: %s"),
			PointName,
			*Path);
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_V5 frame: %s"), *Path);
}

void AIGListenerGreyboxDirector::HandleHistogramScreenshot(
	const int32 Width,
	const int32 Height,
	const TArray<FColor>& Colors)
{
	if (!bHistogramShotPending
		|| HistogramPointIndex < 0
		|| HistogramPointIndex >= IGNightHistogram::PointCount)
	{
		return;
	}
	bHistogramShotPending = false;
	HistogramShotWaitSeconds = 0.0f;

	const IGNightHistogram::FPoint& Point =
		IGNightHistogram::Points[HistogramPointIndex];
	FVector CapturedEye;
	FRotator CapturedView;
	GetWorld()->GetFirstPlayerController()->GetPlayerViewPoint(CapturedEye, CapturedView);
	if (FVector::Dist(CapturedEye, Point.PlayerLocation + FVector(0,0,64)) > 25.f
		|| FMath::Abs(FMath::FindDeltaAngleDegrees(CapturedView.Yaw, Point.PlayerYaw)) > 5.f
		|| FMath::Abs(FMath::FindDeltaAngleDegrees(CapturedView.Pitch, Point.PlayerPitch)) > 5.f)
	{
		UE_LOG(LogTemp, Error, TEXT("MISSINGFLOOR_V5 FAIL point=%s 시점 이탈 eye=%s"), Point.Name, *CapturedEye.ToString());
		++HistogramFailures;
	}
	const int32 PixelCount = Colors.Num();
	if (PixelCount <= 0 || Width <= 0 || Height <= 0)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("MISSINGFLOOR_V5 FAIL: point %s captured no pixels; the sweep "
				"needs a real RHI (-RenderOffScreen -d3d12, never -nullrhi)"),
			Point.Name);
		GetWorldTimerManager().ClearTimer(HistogramTimer);
		UGameViewportClient::OnScreenshotCaptured().Remove(
			HistogramScreenshotHandle);
		HistogramFailures = IGNightHistogram::PointCount;
		return;
	}

	// Written before the verdict: a point that failed its band is exactly the
	// one somebody will want to look at.
	WriteHistogramFrame(Point.Name, Width, Height, Colors);

	int32 ShadowPixels = 0;
	int32 HighlightPixels = 0;
	for (const FColor& Pixel : Colors)
	{
		// Rec. 709 luma on the tonemapped sRGB values: the contract is about
		// perceived brightness on the player's monitor, not scene radiance
		// before the film curve, and not any single channel.
		const float Luma =
			(0.2126f * Pixel.R + 0.7152f * Pixel.G + 0.0722f * Pixel.B) / 255.0f;
		if (Luma < IGNightHistogram::ShadowThreshold)
		{
			++ShadowPixels;
		}
		else if (Luma > IGNightHistogram::HighlightThreshold)
		{
			++HighlightPixels;
		}
	}
	const float Shadow = static_cast<float>(ShadowPixels) / PixelCount;
	const float Highlight = static_cast<float>(HighlightPixels) / PixelCount;

	// A frame that is *entirely* black is not a dark frame, it is a broken read,
	// and it would satisfy every shadow floor in the table for the wrong reason.
	// Refusing it is what stopped this sweep from reporting a false pass.
	if (Shadow >= 0.9995f)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("MISSINGFLOOR_V5 FAIL point=%s frame is entirely black "
				"(%d pixels); nothing rendered, so nothing was measured"),
			Point.Name,
			PixelCount);
		++HistogramFailures;
	}
	else
	{
		const bool bShadowInBand =
			Shadow >= Point.ShadowMinimum && Shadow <= Point.ShadowMaximum;
		const bool bHighlightInBand = Highlight <= Point.HighlightMaximum;
		++HistogramMeasured;
		if (bShadowInBand && bHighlightInBand)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("MISSINGFLOOR_V5 point=%s shadow=%.4f (%.2f..%.2f) "
					"highlight=%.4f (max %.3f) pixels=%d OK"),
				Point.Name,
				Shadow,
				Point.ShadowMinimum,
				Point.ShadowMaximum,
				Highlight,
				Point.HighlightMaximum,
				PixelCount);
		}
		else if (bHistogramReportOnly)
		{
			// Authoring pass: say what it is, do not judge it.
			UE_LOG(
				LogTemp,
				Display,
				TEXT("MISSINGFLOOR_V5 point=%s shadow=%.4f (%.2f..%.2f) "
					"highlight=%.4f (max %.3f) pixels=%d OUT_OF_BAND"),
				Point.Name,
				Shadow,
				Point.ShadowMinimum,
				Point.ShadowMaximum,
				Highlight,
				Point.HighlightMaximum,
				PixelCount);
		}
		else
		{
			++HistogramFailures;
			UE_LOG(
				LogTemp,
				Error,
				TEXT("MISSINGFLOOR_V5 FAIL point=%s shadow=%.4f (%.2f..%.2f) "
					"highlight=%.4f (max %.3f) pixels=%d"),
				Point.Name,
				Shadow,
				Point.ShadowMinimum,
				Point.ShadowMaximum,
				Highlight,
				Point.HighlightMaximum,
				PixelCount);
		}
	}

	const int32 NextIndex = HistogramPointIndex + 1;
	if (NextIndex < IGNightHistogram::PointCount)
	{
		EnterHistogramPoint(NextIndex);
		return;
	}

	GetWorldTimerManager().ClearTimer(HistogramTimer);
	UGameViewportClient::OnScreenshotCaptured().Remove(HistogramScreenshotHandle);
	if (bHistogramReportOnly)
	{
		// An authoring pass judges nothing, so it must not claim a pass either.
		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_V5 REPORT points=%d — bands are authored from "
				"these numbers, never the other way round"),
			HistogramMeasured);
	}
	else if (HistogramFailures == 0)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("MISSINGFLOOR_V5 PASS points=%d shadow_threshold=%.2f "
				"highlight_threshold=%.2f"),
			HistogramMeasured,
			IGNightHistogram::ShadowThreshold,
			IGNightHistogram::HighlightThreshold);
	}
	else
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("MISSINGFLOOR_V5 FAIL points=%d failures=%d"),
			HistogramMeasured,
			HistogramFailures);
	}
	FPlatformMisc::RequestExit(false);
}

void AIGListenerGreyboxDirector::CaptureTeleportPlayer(
	const FVector& Location,
	const float Yaw,
	const float Pitch)
{
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!PlayerCharacter)
	{
		return;
	}
	PlayerCharacter->TeleportTo(
		Location, FRotator(0.0f, Yaw, 0.0f), false, true);
	if (APlayerController* Controller =
		Cast<APlayerController>(PlayerCharacter->GetController()))
	{
		Controller->SetControlRotation(FRotator(Pitch, Yaw, 0.0f));
	}
}

void AIGListenerGreyboxDirector::CaptureParkEntity(
	const FVector& Location,
	const float Yaw)
{
	if (!Entity)
	{
		return;
	}
	Entity->SetPatrolPoints({Location});
	Entity->ParkForBeat(Location, Yaw);
}

void AIGListenerGreyboxDirector::CaptureShot(const TCHAR* BaseName, bool bShowUI) const
{
	if (bCaptureMetricsOnly)
	{
		UE_LOG(LogIndieGame, Display, TEXT("NIGHT_PERF_POINT %s"), BaseName);
		return;
	}
	// 첫 프레임들이 렌더된 뒤에 재질·PSO 작업이 다시 쌓인다. 스틸마다 그
	// 두 번째 물결을 비우고 찍는다.
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FString ScreenshotPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(
			FPaths::ProjectDir(),
			FString::Printf(TEXT("Docs/Media/%s.png"), BaseName)));
	if (FParse::Param(FCommandLine::Get(), TEXT("IGFixtureAudit")))
	{
		FVector Eye; FRotator View;
		GetWorld()->GetFirstPlayerController()->GetPlayerViewPoint(Eye, View);
		UE_LOG(LogTemp, Display, TEXT("FIXTURE_VIEW %s eye=%s rotation=%s"), BaseName, *Eye.ToString(), *View.ToString());
	}
	FScreenshotRequest::RequestScreenshot(ScreenshotPath, bShowUI, false);
	UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_CAPTURE shot: %s"), *ScreenshotPath);
}

void AIGListenerGreyboxDirector::CaptureBeginBurst(
	const TCHAR* DirectoryName,
	const float Seconds)
{
	// 성능 검사에서는 같은 동선을 돌되 PNG 읽기·압축·저장 비용을 제외한다.
	if (bCaptureMetricsOnly) { return; }
	CaptureBurstDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Saved/NightCapture"), DirectoryName));
	UE_LOG(LogIndieGame, Display, TEXT("NIGHT_CAPTURE_BURST %s"), *CaptureBurstDirectory);
	IFileManager::Get().MakeDirectory(*CaptureBurstDirectory, true);
	CaptureBurstFrame = 0;
	CaptureBurstAccumulator = 0.0f;
	CaptureBurstEndsAt = CaptureStepSeconds + Seconds;
	bCaptureBurstActive = true;
}

void AIGListenerGreyboxDirector::EnterCaptureStep(const int32 StepIndex)
{
	// 한 장면만 다시 찍을 때는 다음 단계로 넘어가는 순간 끝낸다.
	int32 StopAfter = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("IGNightCaptureStopAfter="), StopAfter)
		&& StopAfter >= 0 && StepIndex > StopAfter)
	{
		UE_LOG(LogTemp, Display, TEXT("NIGHT_CAPTURE_STOP after=%d"), StopAfter);
		RequestExit(false);
		return;
	}
	CaptureStepIndex = StepIndex;
	if (UIGGameInstance* GameInstance = GetGameInstance<UIGGameInstance>())
	{
		GameInstance->SetRuntimeProfileStage(StepIndex);
	}
	CaptureStepSeconds = 0.0f;
	bCaptureActionADone = false;
	bCaptureActionBDone = false;
	bCaptureActionCDone = false;
	bCaptureBurstActive = false;

	switch (StepIndex)
	{
	case 0:
		// The night card over the 403 bedroom, seconds into the hour.
		CaptureParkEntity(FVector(540.0f, -305.0f, 960.0f), 180.0f);
		CaptureTeleportPlayer(AIGPrologueWorldScene::GetPlayerStartLocation(), -128.0f, -6.0f);
		break;
	case 1:
		// The one upstairs mid-knock, dead ahead down the corridor. The
		// player stands east of the fire-cabinet beat zone (X > 292): parking
		// inside it once fired the whole tutorial mid-photograph, and the
		// capture reset shot the next two stills from the bedroom.
		// 낮게 기는 몸과 얼굴이 평소 카메라 높이에서 함께 보이는 거리.
		CaptureParkEntity(FVector(150.0f, -305.0f, 960.0f), 0.0f);
		CaptureTeleportPlayer(FVector(420.0f, -305.0f, 997.0f), 180.0f, -24.0f);
		break;
	case 2:
		// Looking down the stair throat at the half-landing cameo.
		if (Entity)
		{
			Entity->TeleportTo(
				AIGNightOneBeatDirector::GetSightingStagePoint(),
				FRotator(0.0f, 180.0f, 0.0f),
				false,
				true);
			Entity->SetPatrolPoints({
				AIGNightOneBeatDirector::GetSightingStagePoint(),
				AIGNightOneBeatDirector::GetSightingShufflePoint(),
			});
		}
		// 4층 참 서쪽 난간에서 내려다본 3.5층 참. 그는 북쪽 벽에 귀를 댄 채
		// 1.5 m 아래, 3 m 앞에 있다.
		CaptureTeleportPlayer(FVector(-522.0f, -272.0f, 997.0f), 90.0f, -38.0f);
		break;
	case 3:
		// The noise ripple, moments after a deliberate sound.
		CaptureTeleportPlayer(FVector(60.0f, -305.0f, 997.0f), 0.0f, -4.0f);
		break;
	case 4:
		// The sealed common entrance and its refusal prompt.
		CaptureParkEntity(FVector(540.0f, -305.0f, 960.0f), 180.0f);
		CaptureTeleportPlayer(FVector(630.0f, -295.0f, 92.0f), -90.0f, -6.0f);
		break;
	case 5:
		// P1: the meter cabinet with the fifth, nameless dial.
		CaptureTeleportPlayer(FVector(505.0f, -300.0f, 92.0f), -62.0f, -10.0f);
		break;
	case 6:
		// P2: the booth desk — ledger, carbon pad, monitor.
		//
		// 이 컷의 주어는 「두 기록이 다르다」이므로 서류 두 장이 주인공이어야
		// 한다. 예전 자리는 책상에서 1.1m 떨어져 -25도라 모니터가 화면을
		// 차지하고 정서본과 먹지는 아래로 잘렸다. 책상 앞턱(Y=-137.5)에
		// 닿지 않는 선까지 다가가 두 장의 가운데(X=140)를 내려다본다.
		// 모니터는 위쪽에 남아 관리실이라는 것을 계속 말해 준다.
		CaptureTeleportPlayer(FVector(140.0f, -180.0f, 92.0f), 90.0f, -45.0f);
		break;
	case 7:
		// Burst: the extinguisher fall, with the entity resting so the
		// physics beat stays unphotobombed.
		if (Entity)
		{
			Entity->SetDormant(true);
		}
		CaptureTeleportPlayer(FVector(20.0f, -300.0f, 997.0f), -17.0f, -30.0f);
		break;
	case 8:
		// Burst: hear-investigate-chase-capture-reset, first person.
		if (Entity)
		{
			Entity->SetDormant(false);
			Entity->TeleportTo(
				// Start west of the extinguisher already shown in the previous
				// burst. The capture must exercise pursuit, not photograph the
				// capsule wedged against that settled physics prop.
				FVector(150.0f, -305.0f, 960.0f),
				FRotator(0.0f, 180.0f, 0.0f),
				false,
				true);
			Entity->SetPatrolPoints({FVector(150.0f, -305.0f, 960.0f)});
		}
		CaptureTeleportPlayer(FVector(-250.0f, -305.0f, 997.0f), 0.0f, -4.0f);
		break;
	case 9:
		// Dawn, then Hwang Sun-geum answering through her door.
		//
		// 401호 문 앞 47 cm에서 찍고 있었다. 화각이 78도이므로 그 거리에서
		// 보이는 폭은 76 cm인데 문짝만 84 cm다 — 문틀도, 상인방도, 호수판도
		// 프레임 밖이라 화면에는 무늬 없는 회색 판과 문구멍 하나만 남았다.
		// 「401호 문 너머로 대화하는 장면」이라고 걸어 둔 컷이 문으로 읽히지
		// 않았다. 91 cm까지 물러나면 폭 147 cm·높이 83 cm가 들어와 문틀 양쪽
		// (X -200..-192, -104..-96)과 상인방(Z 1100~1108)이 잡히고, 위로
		// 11도 들면 호수판(Z 1110~1118)까지 프레임에 들어온다. 복도가 130 cm
		// 깊이라 문 전체(208 cm)를 정면으로 담을 방법은 없으므로, 문이라는
		// 것과 401호라는 것을 말해 주는 위쪽을 택한다.
		if (NightPhase)
		{
			NightPhase->CompleteNightGoal();
		}
		CaptureTeleportPlayer(FVector(-150.0f, -328.0f, 997.0f), 90.0f, 11.0f);
		break;
	case 10:
		// 밤 3의 조명과 HUD로 옥상과 설비실을 검수한다.
		if (NightPhase) { NightPhase->BeginTheHour(3); NightPhase->SetHourPaused(true); }
		if (Entity)
		{
			Entity->SetDormant(true);
		}
		if (NightThree)
		{
			if (AIGSwingDoor* RoofGate = NightThree->GetStairGate())
			{
				RoofGate->ForceOpenState(true);
			}
			if (AIGSwingDoor* AnnexGate = NightThree->GetAnnexGate())
			{
				AnnexGate->ForceOpenState(true);
			}
		}
		CaptureTeleportPlayer(FVector(-277.5f, -175.0f, 1068.0f), 90.0f, 8.0f);
		break;
	case 11:
		// First 4.075 m leg, squeezed between tank base and guard rail. Start
		// beyond the stair cheek wall so the shot proves the walkable lane
		// instead of filling half the frame with the wall behind the door.
		CaptureTeleportPlayer(FVector(-150.0f, 220.0f, 1297.0f), 0.0f, -7.0f);
		break;
	case 12:
		// The 90-degree turn and second physical fire door into the annex.
		CaptureTeleportPlayer(FVector(130.0f, 350.0f, 1297.0f), 90.0f, -6.0f);
		break;
	case 13:
		// 밤 4의 전원 상태에서 물탱크 밸브와 최종 장면을 확인한다.
		if (NightPhase) { NightPhase->RestartTheHour(4); NightPhase->SetHourPaused(true); }
		CaptureTeleportPlayer(FVector(5.0f, 350.0f, 1297.0f), -90.0f, -4.0f);
		break;
	case 14:
		// Ground-floor motor, volute, pipes and selector inside the booth.
		CaptureTeleportPlayer(FVector(225.0f, -220.0f, 96.0f), 158.0f, -35.0f);
		break;
	case 15:
		// The real middle gypsum face before five strikes remove its collision.
		if (NightFour)
		{
			NightFour->SetFinaleCapturePreview(false, false);
		}
		CaptureTeleportPlayer(FVector(50.0f, 700.0f, 1297.0f), 0.0f, -22.0f);
		break;
	case 16:
		// Close enough to read the board grip and tired workwear as real 3D,
		// while keeping the player camera under normal first-person control.
		if (NightFour)
		{
			NightFour->SetFinaleCapturePreview(true, true);
		}
		CaptureTeleportPlayer(FVector(130.0f, 710.0f, 1297.0f), -90.0f, -4.0f);
		break;
	case 17:
		// M6.5 좌절 안전망은 5회 포획 메모를 실물로 보여 준다.
		// 평소 1인칭 시점에서 바닥을 보면 읽히지만 HUD는 절대 열지 않는다.
		CaptureParkEntity(FVector(540.0f, -305.0f, 960.0f), 180.0f);
		CaptureTeleportPlayer(FVector(-150.0f, -322.0f, 997.0f), 90.0f, -68.0f);
		if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
		{
			// 실제 접근성 설정 범위의 낮은 FOV를 써서 게임플레이 카메라를
			// 바꾸지 않고도 검수 스틸에서 글자가 충분히 읽히게 한다.
			if (UCameraComponent* Camera = PlayerCharacter->GetFirstPersonCamera())
			{
				Camera->SetFieldOfView(68.0f);
			}
		}
		break;
	case 18:
		if (NightPhase) { NightPhase->RestartTheHour(1); NightPhase->SetHourPaused(true); }
		if (Entity) { Entity->SetDormant(true); }
		CaptureTeleportPlayer(FVector(190.f, -305.f, 997.f), 180.f, -12.f);
		CaptureParkEntity(FVector(20.f, -305.f, 960.f), 0.f);
		break;
	default:
		break;
	}
}

void AIGListenerGreyboxDirector::AdvanceNightCapture()
{
	constexpr float TickSeconds = 0.04f;
	CaptureStepSeconds += TickSeconds;

	// Burst frames ride the same timer. Each 1080p PNG write stalls the next
	// request, so a fixed cadence would drop frames and punch holes in the
	// numbering — and ffmpeg's image sequence reader stops at the first gap.
	// Requesting only when the previous shot has been consumed keeps the
	// sequence continuous at whatever rate the disk actually sustains.
	if (bCaptureBurstActive)
	{
		if (!FScreenshotRequest::IsScreenshotRequested())
		{
			const FString FramePath = FPaths::Combine(
				CaptureBurstDirectory,
				FString::Printf(TEXT("frame_%05d.png"), CaptureBurstFrame++));
			FScreenshotRequest::RequestScreenshot(FramePath, true, false);
			if (FParse::Param(FCommandLine::Get(), TEXT("IGImmersionCapture")))
			{
				UE_LOG(LogTemp, Display, TEXT("IMMERSION_FRAME frame=%d time=%.4f"),
					CaptureBurstFrame - 1, GetWorld()->GetTimeSeconds());
			}
		}
		if (CaptureStepSeconds >= CaptureBurstEndsAt)
		{
			bCaptureBurstActive = false;
		}
	}

	const auto ActionA = [this](const float AtSeconds) -> bool
	{
		if (!bCaptureActionADone && CaptureStepSeconds >= AtSeconds)
		{
			bCaptureActionADone = true;
			return true;
		}
		return false;
	};
	const auto ActionB = [this](const float AtSeconds) -> bool
	{
		if (!bCaptureActionBDone && CaptureStepSeconds >= AtSeconds)
		{
			bCaptureActionBDone = true;
			return true;
		}
		return false;
	};
	const auto ActionC = [this](const float AtSeconds) -> bool
	{
		if (!bCaptureActionCDone && CaptureStepSeconds >= AtSeconds)
		{
			bCaptureActionCDone = true;
			return true;
		}
		return false;
	};
	const auto StepDone = [this](const float AfterSeconds)
	{
		return CaptureStepSeconds >= AfterSeconds;
	};

	AIGPrologueWorldScene* SceneNow =
		const_cast<AIGPrologueWorldScene*>(WorldScene.Get());
	switch (CaptureStepIndex)
	{
	case 0:
		if (ActionA(1.0f))
		{
			CaptureShot(TEXT("night1-card"));
		}
		// Hold here until the card scrim (4.2 s) and the wake-restore inner
		// voice have both drained, so every later still gets a clean HUD.
		if (StepDone(8.5f))
		{
			EnterCaptureStep(1);
		}
		break;
	case 1:
		if (ActionA(0.9f))
		{
			CaptureShot(TEXT("night1-listener-corridor"));
		}
		if (StepDone(1.4f))
		{
			EnterCaptureStep(2);
		}
		break;
	case 2:
		if (ActionA(0.9f))
		{
			CaptureShot(TEXT("night1-stair-sighting"));
		}
		if (StepDone(1.4f))
		{
			EnterCaptureStep(3);
		}
		break;
	case 3:
		if (ActionA(0.5f) && NoiseSubsystem)
		{
			if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
			{
				NoiseSubsystem->ReportNoise(
					PlayerCharacter->GetActorLocation()
						+ FVector(30.0f, 0.0f, 0.0f),
					0.5f,
					PlayerCharacter);
			}
		}
		if (ActionB(0.75f))
		{
			CaptureShot(TEXT("hud-noise-ripple"));
		}
		if (StepDone(1.3f))
		{
			EnterCaptureStep(4);
		}
		break;
	case 4:
		if (ActionA(0.9f))
		{
			CaptureShot(TEXT("night-sealed-entrance"));
		}
		if (StepDone(1.4f))
		{
			EnterCaptureStep(5);
		}
		break;
	case 5:
		if (ActionA(0.9f))
		{
			CaptureShot(TEXT("p1-meter-cabinet"));
		}
		if (StepDone(1.4f))
		{
			EnterCaptureStep(6);
		}
		break;
	case 6:
		if (ActionA(0.9f))
		{
			CaptureShot(TEXT("p2-booth-desk"));
		}
		if (StepDone(1.4f))
		{
			EnterCaptureStep(7);
		}
		break;
	case 7:
		if (ActionA(0.2f))
		{
			CaptureBeginBurst(TEXT("extinguisher"), 3.2f);
		}
		if (ActionB(0.4f) && SceneNow)
		{
			SceneNow->DropCorridorExtinguisher();
		}
		if (StepDone(3.8f))
		{
			EnterCaptureStep(8);
		}
		break;
	case 8:
		if (ActionA(0.3f))
		{
			CaptureBeginBurst(TEXT("chase"), 7.4f);
		}
		// Two sounds a second apart: the first turns its head, the second
		// starts the chase the GIF exists for. The capture and the wake in
		// bed both land inside the frame window on purpose.
		if (ActionB(0.5f) && NoiseSubsystem)
		{
			NoiseSubsystem->ReportNoise(
				FVector(100.0f, -305.0f, 960.0f), 0.45f, Player.Get());
		}
		if (ActionC(1.6f) && NoiseSubsystem)
		{
			NoiseSubsystem->ReportNoise(
				FVector(-220.0f, -305.0f, 960.0f), 0.45f, Player.Get());
		}
		if (StepDone(8.2f))
		{
			EnterCaptureStep(9);
		}
		break;
	case 9:
		if (ActionA(0.8f) && Unit401Door)
		{
			FIGInteractionContext KnockContext;
			KnockContext.Interactor = Player.Get();
			KnockContext.TargetActor = Unit401Door;
			KnockContext.HoldProgress = 1.0f;
			IIGInteractable::Execute_CompleteInteraction(
				Unit401Door, KnockContext);
		}
		if (ActionB(4.8f))
		{
			CaptureShot(TEXT("day-corridor-hwang"));
		}
		if (StepDone(5.6f))
		{
			EnterCaptureStep(10);
		}
		break;
	case 10:
		if (ActionA(4.8f))
		{
			CaptureShot(TEXT("night3-roof-stair"));
		}
		if (StepDone(5.4f))
		{
			EnterCaptureStep(11);
		}
		break;
	case 11:
		if (ActionA(0.9f))
		{
			CaptureShot(TEXT("night3-roof-passage"));
		}
		if (StepDone(1.5f))
		{
			EnterCaptureStep(12);
		}
		break;
	case 12:
		if (ActionA(0.9f))
		{
			CaptureShot(TEXT("night3-annex-doorway"));
		}
		if (StepDone(1.5f))
		{
			EnterCaptureStep(13);
		}
		break;
	case 13:
		if (ActionA(4.8f))
		{
			CaptureShot(TEXT("night4-p5-roof-controls"));
		}
		if (StepDone(5.4f))
		{
			EnterCaptureStep(14);
		}
		break;
	case 14:
		if (ActionA(0.9f))
		{
			CaptureShot(TEXT("night4-p5-transfer-pump"));
		}
		if (StepDone(1.5f))
		{
			EnterCaptureStep(15);
		}
		break;
	case 15:
		if (ActionA(0.9f))
		{
			CaptureShot(TEXT("night4-cavity-wall"));
		}
		if (ActionB(1.25f) && SceneNow)
		{
			SceneNow->OpenMissingFloorCavity();
			if (NightFour)
			{
				NightFour->SetFinaleCapturePreview(true, false);
			}
		}
		if (ActionC(1.75f))
		{
			CaptureShot(TEXT("night4-cavity-open"));
		}
		if (StepDone(2.35f))
		{
			EnterCaptureStep(16);
		}
		break;
	case 16:
		if (ActionA(0.9f))
		{
			CaptureShot(TEXT("night4-mok-confrontation"));
		}
		if (StepDone(1.5f))
		{
			EnterCaptureStep(18);
		}
		break;
	case 17:
		if (ActionA(0.2f))
		{
			CaptureBeginBurst(TEXT("mercy-note"), 2.35f);
			if (NightLoop)
			{
				NightLoop->PlayMercyNoteCapturePreview();
			}
		}
		// 고정 검수 카메라에서 종이가 어두운 타일을 지나면 TSR 히스토리가
		// 실제보다 길게 남는다. 연속 캡처 후의 문서용 스틸만 FXAA로
		// 바꾸어 멈춘 메모를 잔상 없이 남긴다. 게임 렌더러와 GIF는 기본 설정을 유지한다.
		if (!bCaptureMetricsOnly && ActionC(2.65f))
		{
			if (APlayerController* PlayerController =
				GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			{
				PlayerController->ConsoleCommand(
					TEXT("r.AntiAliasingMethod 1"), true);
			}
		}
		if (ActionB(3.1f))
		{
			CaptureShot(TEXT("m65-capture-mercy-note"));
		}
		if (StepDone(3.8f))
		{
			GetWorldTimerManager().ClearTimer(CaptureTimer);
			UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_CAPTURE DONE"));
			RequestExit(false);
		}
		break;
	case 18:
		if (ActionA(.8f) && Entity && Player.IsValid())
		{
			Entity->SetDifficultyForTesting(EIGNightDifficulty::Standard);
			Entity->SetDormant(false);
			CaptureTeleportPlayer(FVector(190.f, -305.f, 997.f), 180.f, -12.f);
			CaptureParkEntity(FVector(20.f, -305.f, 960.f), 0.f);
			CaptureBeginBurst(TEXT("physical-capture"), 8.6f);
		}
		if (ActionB(1.4f) && Entity)
		{
			CaptureParkEntity(FVector(100.f, -305.f, 960.f), 0.f);
			Entity->SetActorTickEnabled(true);
		}
		if (ActionC(2.2f) && Entity && Player.IsValid())
		{
			bool bTexturesReady = true;
			for (const TCHAR* TextureRole : {TEXT("D"), TEXT("N"), TEXT("ORM")})
			{
				if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr,
					*FString::Printf(TEXT("/Game/Prototype/Textures/T_ListenerCrawler_%s.T_ListenerCrawler_%s"), TextureRole, TextureRole)))
				{
					const auto& Streaming = Texture->GetStreamableResourceState();
					bTexturesReady &= Texture->IsFullyStreamedIn();
					UE_LOG(LogTemp, Display, TEXT("PHYSICAL_CAPTURE_TEXTURE %s resident=%d requested=%d max=%d forced=%d"),
						TextureRole, Streaming.NumResidentLODs, Streaming.NumRequestedLODs, Streaming.MaxNumLODs,
						Texture->ShouldMipLevelsBeForcedResident());
				}
			}
			UE_LOG(LogTemp, Display, TEXT("PHYSICAL_CAPTURE_CONTACT state=%d dormant=%d physical=%d player=%s face=%s eye=%s"),
				static_cast<int32>(Entity->GetListenerState()), Entity->IsDormant(), Player->HasPhysicalCaptureView(),
				*Player->GetActorLocation().ToString(), *Entity->GetCaptureFaceLocation().ToString(),
				*Player->GetPawnViewLocation().ToString());
			if (Entity->GetListenerState() != EIGListenerState::CaptureHold || !Player->HasPhysicalCaptureView() || !bTexturesReady)
			{
				UE_LOG(LogTemp, Error, TEXT("PHYSICAL_CAPTURE FAIL contact or texture streaming"));
				RequestExit(true);
			}
		}
		if (StepDone(9.7f))
		{
			GetWorldTimerManager().ClearTimer(CaptureTimer);
			const bool bCaptured = NightLoop && NightLoop->GetCaptureCount() > 0 &&
				!NightLoop->IsCaptureResetInFlight() && Player.IsValid() && Player->InputEnabled();
			UE_LOG(LogTemp, Display, TEXT("PHYSICAL_CAPTURE %s"), bCaptured ? TEXT("PASS") : TEXT("FAIL"));
			UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_CAPTURE DONE"));
			RequestExit(!bCaptured);
		}
		break;
	default:
		break;
	}
}
