#include "Entity/IGNightPhaseDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGStoryStateSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"
#include "Save/IGSaveSubsystem.h"
#include "TimerManager.h"

namespace IGNightPhase
{
	/** How often the hour is sampled. Coarse on purpose: nothing needs frames. */
	constexpr float TickIntervalSeconds = 0.25f;
	/**
	 * 새벽은 한 번 눈을 감는다. 스무 분의 밤이 독백 한 줄로 끝나면 밤과 낮이
	 * 같은 화면이라 끝났다는 감각이 몸에 안 온다.
	 */
	constexpr float DawnFadeOutSeconds = 0.45f;
	/** 눈이 다 감긴 다음 프레임쯤. 페이드 마지막 프레임에 등이 켜지지 않게. */
	constexpr float DawnWorldDelaySeconds = DawnFadeOutSeconds + 0.03f;
	/**
	 * 다 감긴 뒤의 검은 시간. 등이 켜지고 노출 상한이 풀린 눈이 여기서 자리를
	 * 잡는다. 잠금 해제음은 이 시간 한가운데 온다 — 소리를 듣고 눈을 뜬다.
	 */
	constexpr float DawnBlackSeconds = 0.75f;
	constexpr float DawnLatchDelaySeconds = DawnFadeOutSeconds + 0.25f;
	constexpr float DawnFadeInSeconds = 0.9f;
	/** 공동현관 유리문의 전자 잠금. 문 자체는 (604, -385)에 서 있다. */
	const FVector EntranceLatchLocation(604.0f, -385.0f, 100.0f);
	/**
	 * 계단실 샤프트. 밤마다 계단실 베드가 울던 자리다. 1층의 딸깍은 4·5층에서
	 * 슬래브에 걸러져 톡 하나로 남으므로, 샤프트를 타고 올라온 걸쇠 소리를 그녀가
	 * 있는 층의 반 층 아래에서 한 번 더 낸다.
	 */
	constexpr float StairShaftX = -445.0f;
	constexpr float StairShaftY = -305.0f;
	constexpr float StairLatchDrop = 150.0f;
	/** 1층 가까이에 있으면 현관의 딸깍이 직접 들린다. */
	constexpr float StairLatchMinimumHeight = 300.0f;
}

AIGNightPhaseDirector::AIGNightPhaseDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AIGNightPhaseDirector::BeginPlay()
{
	Super::BeginPlay();
	// 무인 검증과 캡처 투어는 새벽 직후의 세계를 같은 프레임에 확인한다.
	// 눈을 감는 시각표는 거기서 즉시 경로로 접힌다.
	bImmediateDawn =
		FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreyboxProbe"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGNightCapture"));
}

void AIGNightPhaseDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(HourTimer);
	GetWorldTimerManager().ClearTimer(DawnLatchTimer);
	GetWorldTimerManager().ClearTimer(DawnTimer);
	const bool bDawnPending = GetWorldTimerManager().IsTimerActive(DawnWorldTimer);
	GetWorldTimerManager().ClearTimer(DawnWorldTimer);
	bDawnTransitionInProgress = false;
	// Never leave a torn-down director holding the building shut. 눈을 감는
	// 도중이면 시간은 이미 끝났지만 건물은 아직 봉쇄돼 있다.
	if (bHourActive || bDawnPending)
	{
		if (AIGPrologueWorldScene* WorldScene = Scene.Get())
		{
			WorldScene->SetTheHourSealed(false);
		}
		ApplySealedPresentation(false);
	}
	ReleaseHeldListeners();
	Super::EndPlay(EndPlayReason);
}

void AIGNightPhaseDirector::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer)
{
	Scene = InScene;
	Player = InPlayer;
}

void AIGNightPhaseDirector::BeginTheHour(const int32 NightIndex)
{
	if (bHourActive)
	{
		return;
	}
	// 눈을 감는 사이에 다음 밤이 먼저 오면 미뤄 둔 새벽부터 적용한다. 그냥
	// 지우면 그는 한 번도 잠들지 않은 채 밤을 맞고, 아래 브로드캐스트의
	// SetDormant(false)는 멈춰 둔 몸을 되살리지 못한다.
	if (GetWorldTimerManager().IsTimerActive(DawnWorldTimer))
	{
		GetWorldTimerManager().ClearTimer(DawnWorldTimer);
		FlipWorldUnderBlack();
	}
	GetWorldTimerManager().ClearTimer(DawnLatchTimer);
	bHourActive = true;
	bGoalComplete = false;
	bFailureEndingSuspended = false;
	bHourPaused = false;
	HourElapsedSeconds = 0.0f;
	HourDurationSeconds = GetHourDurationSeconds(NightIndex);

	// 같은 번호의 밤이 다시 오면 카드가 그것을 안다. 못 채운 밤의 되풀이와
	// 엔딩 C의 재시도가 여기 걸린다. 저장에서 이어 붙이는 것은 되풀이가 아니다.
	bool bRepeatedNight = false;
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		bRepeatedNight = !bRestoringHour
			&& NightIndex >= 1
			&& Narrative->GetNightIndex() == NightIndex;
		Narrative->SetNightIndex(NightIndex);
		Narrative->SetHourSealed(true);
		Narrative->SetNightElapsedSeconds(0.0f);
	}

	if (AIGPrologueWorldScene* WorldScene = Scene.Get())
	{
		WorldScene->SetTheHourSealed(true);
		// §11 V2 403호 3단계 노화. The prologue and the first night are a flat
		// she lives in; nights two and three crack the ceiling corner; night
		// four has the damp down the wall. Driven off the night rather than any
		// story flag, because the building is not reacting to her — it is just
		// getting worse, and that is the point.
		WorldScene->SetUnit403AgeStage(
			NightIndex >= 4 ? 2 : (NightIndex >= 2 ? 1 : 0));
	}
	ApplySealedPresentation(true);

	// 잠자리에 들며 방 불은 껐다. 밤에 다시 켜는 것은 그녀가 고른다.
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (UIGStoryStateSubsystem* Story = Instance->GetSubsystem<UIGStoryStateSubsystem>())
		{
			Story->RemoveState(FGameplayTag::RequestGameplayTag(TEXT("State.MissingFloor.HomeLightOn"), false));
		}
	}

	// 현관 신발장 위에서 챙긴 손전등만 쓴다. 놓고 잤으면 밤이 대신 쥐여 주지 않는다.
	// 집 안은 창 불빛으로 신발장까지 걸어갈 만큼은 보이고, F를 누르면 속말이 알려 준다.
	// 밤을 여는 모든 길(잠·되풀이·저장 복원·캡처)이 여기를 지나므로 이 한 자리에서 읽는다.
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (UIGFlashlightComponent* Torch = PlayerCharacter->GetFlashlight())
		{
			Torch->RefreshOwnership();
			Torch->SetOn(true);
		}
	}

	// The one allowed piece of framing UI: the night card, mirroring the
	// legacy chapter cards. Everything after it is world and sound.
	FText NightTitle;
	switch (NightIndex)
	{
	case 1:
		NightTitle = NSLOCTEXT("IGMissingFloor", "Night1Title", "첫째 밤 · 소리");
		break;
	case 2:
		NightTitle = NSLOCTEXT("IGMissingFloor", "Night2Title", "둘째 밤 · 기록");
		break;
	case 3:
		NightTitle = NSLOCTEXT("IGMissingFloor", "Night3Title", "셋째 밤 · 조율");
		break;
	case 4:
		NightTitle = NSLOCTEXT("IGMissingFloor", "Night4Title", "넷째 밤 · 대답");
		break;
	default:
		break;
	}
	if (!NightTitle.IsEmpty())
	{
		AIGHorrorHUD::ShowChapterCard(
			this,
			bRepeatedNight
				? NSLOCTEXT("IGMissingFloor", "NightCardEyebrowAgain", "또 새벽 네 시 반")
				: NSLOCTEXT("IGMissingFloor", "NightCardEyebrow", "새벽 네 시 반"),
			NightTitle,
			FText::GetEmpty(),
			4.2f);
	}

	GetWorldTimerManager().SetTimer(
		HourTimer,
		this,
		&AIGNightPhaseDirector::TickHour,
		IGNightPhase::TickIntervalSeconds,
		true);

	OnHourActiveChanged.Broadcast(true);
	if (!bRestoringHour)
	{
		RequestMissingFloorAutosave(true);
	}
}

void AIGNightPhaseDirector::ResumeTheHour(
	const int32 NightIndex,
	const float ElapsedSeconds)
{
	if (bHourActive)
	{
		return;
	}
	bRestoringHour = true;
	BeginTheHour(NightIndex);
	bRestoringHour = false;
	HourElapsedSeconds = FMath::Clamp(
		ElapsedSeconds,
		0.0f,
		HourDurationSeconds - IGNightPhase::TickIntervalSeconds);
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->SetNightElapsedSeconds(HourElapsedSeconds);
	}
}

void AIGNightPhaseDirector::SuspendForFailureEnding()
{
	if (!bHourActive || bFailureEndingSuspended)
	{
		return;
	}
	bFailureEndingSuspended = true;
	GetWorldTimerManager().ClearTimer(HourTimer);
}

void AIGNightPhaseDirector::RestartTheHour(const int32 NightIndex)
{
	GetWorldTimerManager().ClearTimer(HourTimer);
	bHourActive = false;
	bGoalComplete = false;
	bFailureEndingSuspended = false;
	BeginTheHour(NightIndex);
}

void AIGNightPhaseDirector::CompleteNightGoal()
{
	if (!bHourActive || bGoalComplete)
	{
		return;
	}
	bGoalComplete = true;
	// 채운 밤은 기록에 남는다. 못 채운 밤은 다음 저녁에 다시 온다(§5.4).
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->MarkBeatPlayed(GoalBeatId(Narrative->GetNightIndex()));
	}
	// The goal and the timeout share one exit so morning is always the same
	// world state, whichever way the player got there.
	ReleaseAtDawn();
}

FName AIGNightPhaseDirector::GoalBeatId(const int32 NightIndex)
{
	return FName(*FString::Printf(TEXT("Night%d.Goal"), NightIndex));
}

FName AIGNightPhaseDirector::DayConversationBeatId(const int32 NightIndex)
{
	return FName(*FString::Printf(TEXT("Day.Hwang.%d"), NightIndex));
}

void AIGNightPhaseDirector::TickHour()
{
	if (!bHourActive || bFailureEndingSuspended)
	{
		return;
	}
	if (bHourPaused)
	{
		return;
	}
	HourElapsedSeconds += IGNightPhase::TickIntervalSeconds;
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->SetNightElapsedSeconds(HourElapsedSeconds);
	}
	if (HourElapsedSeconds >= HourDurationSeconds)
	{
		// 05:30이 포획 암전 한가운데 떨어지면 그 암전과 새벽의 눈 감기가 한
		// 화면을 두고 다툰다. 침대에서 눈을 뜨고 나서 새벽이 온다.
		if (!bImmediateDawn && IsCaptureResetInFlight())
		{
			return;
		}
		ReleaseAtDawn();
	}
}

void AIGNightPhaseDirector::ReleaseAtDawn()
{
	GetWorldTimerManager().ClearTimer(HourTimer);
	bHourActive = false;

	// §20.4: dawn on night four with the wall still closed is the substitute
	// route to ending C. It is the only way there for 듣기만 하는 밤, which has
	// no captures to raise the tier, and it is harmless in the other modes —
	// a player who reached 05:30 without opening the wall has failed either way.
	AIGMissingFloorNightFourDirector* FailureRoute = nullptr;
	for (TActorIterator<AIGMissingFloorNightFourDirector> It(GetWorld()); It; ++It)
	{
		if (It->WouldResolveDawnFailureEnding())
		{
			FailureRoute = *It;
		}
		break;
	}
	if (FailureRoute)
	{
		// 실패의 새벽은 눈을 감지 않는다. 매물 화면이 제 암전을 갖고 온다.
		// 낮 전환을 먼저 보내고 엔딩을 그 뒤에 건다 — 순서가 반대면 밤4의 낮
		// 전환(SetHourActive(false))이 엔딩 C의 카드 타이머와 암전을 지운다.
		HoldListenersForDawn();
		const TArray<TWeakObjectPtr<AIGListenerEntity>> InView = DawnHeldListeners;
		ApplyDawnWorld();
		if (FailureRoute->ResolveDawnFailureEnding())
		{
			// 05:30에 그는 잠든다. 그래도 눈앞에서 지워지지는 않는다(§4.6).
			// 멈춘 몸은 매물 화면의 2.15초 암전이 다 덮은 뒤 거둔다
			// (BeginFailureListing). 새벽 전에 이미 잠들어 있던 몸은 건드리지 않는다.
			for (const TWeakObjectPtr<AIGListenerEntity>& Held : InView)
			{
				if (AIGListenerEntity* Listener = Held.Get())
				{
					Listener->SetActorHiddenInGame(false);
				}
			}
			// 건물은 열리고 벽은 닫힌 채다. 목표 줄도 아침 독백도 없다 —
			// 그 아침은 오지 않았다.
			PlayEntranceLatch();
			return;
		}
		// 판정과 확정이 어긋나면 평범한 아침으로 둔다.
		ApplySealedPresentation(false);
		RequestMissingFloorAutosave(false);
		PlayEntranceLatch();
		PlayMorningLine();
		return;
	}

	APlayerController* Controller = GetDawnController();
	APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager : nullptr;
	const bool bCloseEyes = Camera
		&& !bMorningPresentationSuppressed
		&& !bImmediateDawn
		&& !IsCaptureResetInFlight();
	if (!bCloseEyes)
	{
		// 즉시 경로. 에필로그가 화면을 가진 엔딩 길, 무인 검증과 캡처, 포획
		// 암전이 화면을 쥔 동안. 세계와 목표 줄이 같은 프레임에 바뀐다.
		ApplyDawnWorld();
		ApplySealedPresentation(false);
		RequestMissingFloorAutosave(false);
		if (bMorningPresentationSuppressed)
		{
			bMorningPresentationSuppressed = false;
			return;
		}
		PlayEntranceLatch();
		PlayMorningLine();
		return;
	}

	// 그가 먼저 그 자리에 선다. 사라지는 것은 검은 화면 아래서다(§4.6).
	// 예전에는 세계가 먼저 바뀌고 눈이 그 뒤에 감겨서, 페이드 첫 프레임에
	// 복도등이 낮 밝기로 켜지고 바로 뒤의 그가 한 프레임 만에 지워졌다.
	HoldListenersForDawn();
	// 밤의 음악도 눈과 같이 감긴다. 그가 잠드는 것은 눈이 다 감긴 뒤라, 여기서 먼저
	// 걷지 않으면 추격의 꼬리가 「문이 열린다. 아침이다.」와 날숨 위로 운다.
	if (UIGMissingFloorAudioSubsystem* AudioDirector =
		GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>())
	{
		AudioDirector->ReleaseScoreForDawn(IGNightPhase::DawnFadeOutSeconds);
	}
	// 여기서부터 눈을 다시 뜰 때까지 화면은 새벽의 것이다.
	bDawnTransitionInProgress = true;
	// 다른 연출이 화면을 반쯤 덮고 있었다면 거기서부터 감는다. 0에서 다시
	// 시작하면 한 프레임 밝아졌다가 감긴다.
	Camera->StartCameraFade(
		Camera->bEnableFading ? FMath::Clamp(Camera->FadeAmount, 0.0f, 1.0f) : 0.0f,
		1.0f,
		IGNightPhase::DawnFadeOutSeconds,
		FLinearColor::Black,
		/*bShouldFadeAudio=*/false,
		/*bHoldWhenFinished=*/true);
	GetWorldTimerManager().SetTimer(
		DawnWorldTimer,
		this,
		&AIGNightPhaseDirector::FlipWorldUnderBlack,
		IGNightPhase::DawnWorldDelaySeconds,
		false);
	GetWorldTimerManager().SetTimer(
		DawnLatchTimer,
		this,
		&AIGNightPhaseDirector::PlayEntranceLatch,
		IGNightPhase::DawnLatchDelaySeconds,
		false);
	GetWorldTimerManager().SetTimer(
		DawnTimer,
		this,
		&AIGNightPhaseDirector::FinishDawnPresentation,
		IGNightPhase::DawnFadeOutSeconds + IGNightPhase::DawnBlackSeconds,
		false);
}

void AIGNightPhaseDirector::ApplyDawnWorld()
{
	if (AIGPrologueWorldScene* WorldScene = Scene.Get())
	{
		// 등·안개·노출, 그리고 창 너머 도로가 낮으로 돌아온다(ApplyNightAtmosphere).
		WorldScene->SetTheHourSealed(false);
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->SetHourSealed(false);
	}
	// 그가 잠드는 것(SetDormant)도, 밤의 베드가 가라앉는 것도 이 한 줄이다.
	OnHourActiveChanged.Broadcast(false);
	ReleaseHeldListeners();
}

void AIGNightPhaseDirector::FlipWorldUnderBlack()
{
	ApplyDawnWorld();
	// 낮 전환을 받는 쪽 하나라도 페이드를 걷으면, 눈을 뜨기 전에 낮으로 바뀐
	// 복도가 보이고 눈을 뜰 때 한 번 더 검게 튄다. 세계를 바꾼 직후 검정을 다시
	// 붙든다. 걷는 것은 FinishDawnPresentation 하나다.
	if (bDawnTransitionInProgress)
	{
		if (APlayerController* Controller = GetDawnController())
		{
			if (Controller->PlayerCameraManager)
			{
				Controller->PlayerCameraManager->SetManualCameraFade(
					1.0f,
					FLinearColor::Black,
					/*bInFadeAudio=*/false);
			}
		}
	}
	RequestMissingFloorAutosave(false);
}

void AIGNightPhaseDirector::HoldListenersForDawn()
{
	DawnHeldListeners.Reset();
	for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
	{
		AIGListenerEntity* Listener = *It;
		if (!Listener || Listener->IsDormant())
		{
			continue;
		}
		// Tick이 그의 전부다 — 상태 기계, 기는 걸음, 포획 판정. 멈추면 눈을
		// 감는 반 초 안에 잡히는 일이 없다. 재생도 세운다. 기던 동작이 제자리에서
		// 계속 돌면 멈춘 것이 아니라 헛도는 것이다. 깨어나면 그의 Tick이 속도를
		// 다시 넣는다.
		Listener->SetActorTickEnabled(false);
		if (USkeletalMeshComponent* Body =
			Listener->FindComponentByClass<USkeletalMeshComponent>())
		{
			Body->SetPlayRate(0.0f);
		}
		DawnHeldListeners.Add(Listener);
	}
}

void AIGNightPhaseDirector::ReleaseHeldListeners()
{
	// 새벽 전환이 재우지 않은 몸은 여기서 되살린다. 멈춘 채로 두면 다음 밤의
	// SetDormant(false)가 조기 반환해서 그는 밤새 그 자리에 서 있다.
	for (const TWeakObjectPtr<AIGListenerEntity>& Held : DawnHeldListeners)
	{
		AIGListenerEntity* Listener = Held.Get();
		if (Listener && !Listener->IsDormant())
		{
			Listener->SetActorTickEnabled(true);
		}
	}
	DawnHeldListeners.Reset();
}

bool AIGNightPhaseDirector::IsCaptureResetInFlight() const
{
	for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It)
	{
		if (It->IsCaptureResetInFlight())
		{
			return true;
		}
	}
	return false;
}

APlayerController* AIGNightPhaseDirector::GetDawnController() const
{
	if (const AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (APlayerController* Controller =
			Cast<APlayerController>(PlayerCharacter->GetController()))
		{
			return Controller;
		}
	}
	const UWorld* World = GetWorld();
	return World ? World->GetFirstPlayerController() : nullptr;
}

void AIGNightPhaseDirector::PlayEntranceLatch()
{
	// 1층 공동현관의 전자 잠금. 여기서 나는 것이 사실이다.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateRelayClick(this),
		IGNightPhase::EntranceLatchLocation,
		0.9f,
		0.7f,
		300.0f,
		3200.0f,
		EIGAudioBus::World);
	// 그런데 4·5층에서는 세 층의 슬래브가 이것을 210Hz 톡 하나로 거른다.
	// 계단실을 타고 올라온 걸쇠 소리를 그녀가 있는 층 반 층 아래에서 한 번
	// 더 낸다 — 가깝고, 아래에서 올라온다. 문마다 걸쇠가 풀릴 때 나던 그
	// 소리라 몸이 먼저 알아듣는다.
	if (const AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		const float PlayerZ = PlayerCharacter->GetActorLocation().Z;
		if (PlayerZ - IGNightPhase::EntranceLatchLocation.Z
			> IGNightPhase::StairLatchMinimumHeight)
		{
			IGAudio::SpawnOneShotAt(
				this,
				IGAudio::SampleOr(
					TEXT("Door_Steel_Open"),
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateRelayClick(this); }),
				FVector(
					IGNightPhase::StairShaftX,
					IGNightPhase::StairShaftY,
					PlayerZ - IGNightPhase::StairLatchDrop),
				0.42f,
				0.82f,
				260.0f,
				1500.0f,
				EIGAudioBus::World);
		}
	}
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "DawnLatchCaption", "공동현관 잠금 풀리는 소리"),
		2.2f,
		IGNightPhase::EntranceLatchLocation);
}

void AIGNightPhaseDirector::FinishDawnPresentation()
{
	bDawnTransitionInProgress = false;
	if (APlayerController* Controller = GetDawnController())
	{
		if (APlayerCameraManager* Camera = Controller->PlayerCameraManager)
		{
			// 검은 화면이 그 사이 다른 연출(계단 전환 따위)에 걷혔으면 다시 검게
			// 튀지 않고 지금 밝기에서 뜬다.
			Camera->StartCameraFade(
				Camera->bEnableFading ? FMath::Clamp(Camera->FadeAmount, 0.0f, 1.0f) : 0.0f,
				0.0f,
				IGNightPhase::DawnFadeInSeconds,
				FLinearColor::Black,
				/*bShouldFadeAudio=*/false,
				/*bHoldWhenFinished=*/false);
		}
	}
	// 눈을 감은 사이에 다음 밤이 먼저 왔으면(저장 복원·시험 경로) 아침을 말하지
	// 않는다. 눈만 뜬다.
	if (bHourActive)
	{
		return;
	}
	// 목표 줄은 눈을 뜰 때 독백과 함께 온다. HUD는 카메라 암전 위에 그려지므로
	// 이보다 먼저 낮 표시로 바꾸면 검은 화면 위에 글자가 먼저 뜬다.
	ApplySealedPresentation(false);
	PlayMorningLine();
}

void AIGNightPhaseDirector::PlayMorningLine()
{
	// The release is announced by the world, not by a banner: the entrance
	// simply opens again. One inner-voice line is allowed (§7 forbids
	// confirmation UI, not thought).
	// 못 채운 밤의 아침은 구원이 아니다. 다음 저녁 카드에서가 아니라 눈을 뜨는
	// 이 자리에서 안다(§5.4). 밤1은 답을 찾은 순간에 목표를 먼저 적어 두므로
	// 기록을 같이 본다 — 기다리는 사이 05:30이 먼저 와도 채운 밤이다.
	// 밤4도 같다. 벽이 닫힌 새벽은 엔딩 C가, 고른 새벽은 에필로그가 가져가서
	// 여기까지 오는 밤4는 벽을 열고도 고르지 못한 밤뿐이고, 다음 저녁에 되풀이된다.
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const int32 NightIndex = Narrative ? Narrative->GetNightIndex() : 0;
	const bool bMissedNight = !bGoalComplete
		&& Narrative
		&& NightIndex >= 1
		&& NightIndex <= 4
		&& !Narrative->HasBeatPlayed(GoalBeatId(NightIndex));
	AIGHorrorHUD::PushThought(
		this,
		bMissedNight
			? NSLOCTEXT("IGMissingFloor", "MorningCameMissed", "벌써 다섯 시 반이야. 아직 못 끝냈는데.")
			: NSLOCTEXT("IGMissingFloor", "MorningCame", "아침이다. 현관도 열렸겠지."),
		3.4f);
	if (bMissedNight)
	{
		// 딸깍과 자막은 같다. 내쉴 숨이 없을 뿐이다.
		return;
	}
	// 잠금이 풀리는 소리에 숨을 내쉰다. 밤을 무섭게 보낸 몸만 — 스트레스가
	// 낮으면 그냥 아침이고, 그건 소리 낼 일이 아니다.
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->PlayReliefExhale();
		}
	}
}

void AIGNightPhaseDirector::RequestMissingFloorAutosave(const bool bAtNight)
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GetWorld();
	UIGSaveSubsystem* SaveSubsystem = GameInstance
		? GameInstance->GetSubsystem<UIGSaveSubsystem>()
		: nullptr;
	if (!SaveSubsystem || !World)
	{
		return;
	}
	// 결말 A나 B를 고른 뒤의 새벽은 저장하지 않는다. 에필로그가 끝나면 게임도
	// 끝났다. 그 아침을 저장하면 이어하기가 할 일 없는 낮으로 들어가고, 거기서
	// 자면 넷째 밤이 되풀이됐다. 가장 새 저장은 선택 직전(벽 앞)으로 남아서
	// 이어하기로 다른 결말을 고를 수 있다.
	if (const UIGMissingFloorNarrativeSubsystem* Narrative =
		GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
	{
		const FName Ending = Narrative->GetEndingChoice();
		if (Ending == FName(TEXT("Ending.A")) || Ending == FName(TEXT("Ending.B")))
		{
			return;
		}
	}
	SaveSubsystem->RequestAutosave(
		FGameplayTag::RequestGameplayTag(FName(TEXT("Chapter.MissingFloor")), false),
		World->GetOutermost()->GetFName(),
		FGameplayTag::RequestGameplayTag(
			FName(bAtNight
				? TEXT("Checkpoint.MissingFloor.Night")
				: TEXT("Checkpoint.MissingFloor.Day")),
			false));
}

void AIGNightPhaseDirector::ApplySealedPresentation(const bool bSealed)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return;
	}
	AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(PlayerController->GetHUD());
	if (!Hud)
	{
		return;
	}

	// Night austerity: no objective line while the hour holds. The objective
	// provider is still bound so the day sections have something to say.
	Hud->SetNightPresentation(bSealed);
	Hud->SetObjectiveProvider(this);
}

int32 AIGNightPhaseDirector::GetStoryMinutesRemaining() const
{
	const float Remaining =
		FMath::Max(HourDurationSeconds - HourElapsedSeconds, 0.0f);
	const float Ratio = Remaining / HourDurationSeconds;
	return FMath::CeilToInt(Ratio * (StoryEndMinutes - StoryStartMinutes));
}

FText AIGNightPhaseDirector::GetObjectiveText() const
{
	// During the hour the HUD suppresses the objective entirely, so this only
	// ever reads in the day sections between nights.
	if (bHourActive)
	{
		return FText::GetEmpty();
	}
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const int32 NightIndex = Narrative ? Narrative->GetNightIndex() : 0;
	if (Narrative)
	{
		// 밤4의 이유가 되는 요구서와 신고 문자는 대화가 끝났어도 남을 수 있다.
		if (NightIndex == 3 && Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer))
		{
			if (Narrative->HasBeatPlayed(TEXT("Day.EvictionPosted"))
				&& !Narrative->HasSource(EIGMissingFloorTruth::StillCoveringIt,
					EIGMissingFloorSource::EvictionWarning))
			{
				return NSLOCTEXT("IGMissingFloor", "DayObjectiveNotice", "403호 문에 붙은 통보문 읽기");
			}
			if (!Narrative->HasBeatPlayed(TEXT("Night3.SiteCheck")))
			{
				return NSLOCTEXT("IGMissingFloor", "DayObjectiveReport", "신고 문자 확인하기");
			}
			if (!Narrative->HasBeatPlayed(TEXT("Day.EvictionPosted")))
			{
				return NSLOCTEXT("IGMissingFloor", "DayObjectiveRoom", "403호로 돌아가기");
			}
			if (!Narrative->HasBeatPlayed(TEXT("Day.EvictionDeadline")))
			{
				return NSLOCTEXT("IGMissingFloor", "DayObjectiveReport", "신고 문자 확인하기");
			}
		}
		if (NightIndex >= 2 && Narrative->HasTruth(EIGMissingFloorTruth::WasStillAlive)
			&& !Narrative->HasSource(EIGMissingFloorTruth::FiveNightsOfThirst,
				EIGMissingFloorSource::KnockTallyJournal))
		{
			return NSLOCTEXT("IGMissingFloor", "DayObjectiveJournal", "401호 문고리에 걸린 일지 읽기");
		}
		// 이미 끝낸 대화로 되돌려 보내지 않는다. 이전 저장의 특별 대화 기록도 읽는다.
		const bool bTalked = Narrative->HasBeatPlayed(DayConversationBeatId(NightIndex))
			|| (NightIndex == 2 && Narrative->HasBeatPlayed(TEXT("Day.Hwang.Journal")))
			|| (NightIndex == 3 && Narrative->HasBeatPlayed(TEXT("Day.Hwang.Permission")));
		if (bTalked)
		{
			return NSLOCTEXT("IGMissingFloor", "ArrivalObjectiveSleep", "403호로 돌아가서 자기");
		}
	}
	switch (NightIndex)
	{
	case 2:
		return NSLOCTEXT("IGMissingFloor", "DayObjectiveTwo", "401호 할머니께 다시 여쭤보기");
	case 3:
		return NSLOCTEXT("IGMissingFloor", "DayObjectiveThree", "401호 할머니께 간밤 일 알리기");
	default:
		return NSLOCTEXT("IGMissingFloor", "DayObjectiveOne", "401호 할머니 찾아가기");
	}
}

float AIGNightPhaseDirector::GetObjectiveProgress() const
{
	if (!bHourActive)
	{
		return 0.0f;
	}
	return FMath::Clamp(HourElapsedSeconds / HourDurationSeconds, 0.0f, 1.0f);
}

UIGMissingFloorNarrativeSubsystem* AIGNightPhaseDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
}
