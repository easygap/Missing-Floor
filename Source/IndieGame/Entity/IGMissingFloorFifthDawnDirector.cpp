#include "Entity/IGMissingFloorFifthDawnDirector.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
#include "Entity/IGReplaySkip.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IndieGame.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "TimerManager.h"

namespace IGFifthDawn
{
	constexpr float DurationSeconds = 160.0f;
	constexpr float ReplaySkipDurationSeconds = IGReplaySkip::HoldSeconds;
	constexpr float ReplaySkipRewindMultiplier = IGReplaySkip::RewindMultiplier;
	constexpr const TCHAR* ProfileSection = TEXT("IndieGame.MissingFloorProfile");
	constexpr const TCHAR* ExperiencedKey = TEXT("FifthDawnExperienced");
	// 7/27 start, water shift, 7/28, 7/29 call/reply, 7/30, 7/31,
	// final two knocks, all beds cut, wake.
	constexpr float CueTimes[] =
	{
		0.0f, 24.0f, 24.0f, 52.0f, 58.0f, 74.0f,
		80.0f, 115.0f, 118.0f, 148.0f, 159.2f, DurationSeconds
	};

	/**
	 * 새벽마다 예불이 한 걸음씩 멀어진다. 7월 27일부터 31일까지, 볼륨이 줄고
	 * 고역이 먼저 죽는다 — 날마다 벽을 하나씩 더 사이에 둔 것처럼.
	 */
	constexpr float PrayerDawnVolumes[] = {0.12f, 0.10f, 0.08f, 0.065f, 0.05f};
	constexpr float PrayerDawnLowPassHz[] = {6000.0f, 3600.0f, 2400.0f, 1600.0f, 1100.0f};
	/**
	 * 새벽이 바뀌면 예불은 끊겼다가 다시 시작한다. 마지막 새벽은 더 오래
	 * 끊겨서 118초의 두 번이 그 빈틈에 떨어진다.
	 */
	constexpr float PrayerDipSeconds = 1.5f;
	constexpr float PrayerReturnSeconds = 2.5f;
	constexpr float PrayerFinalReturnSeconds = 4.5f;
	constexpr float PrayerReturnFadeSeconds = 3.0f;
	/** 숨을 죽이면 바깥이 커진다. */
	constexpr float PrayerListenGain = 1.8f;
	constexpr float WaterListenGain = 1.5f;
}

AIGMissingFloorFifthDawnDirector::AIGMissingFloorFifthDawnDirector()
{
	// 재관람 스킵의 연속 진행률을 그리는 동안에만 Tick을 깨운다.
	// 입력이 끝나고 되감기까지 완료되면 즉시 다시 비활성화한다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void AIGMissingFloorFifthDawnDirector::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bActive || !bReplaySkipAvailable)
	{
		SetActorTickEnabled(false);
		return;
	}

	const float RequiredSeconds = GetReplaySkipDurationSeconds();
	const float SafeDelta = FMath::Max(DeltaSeconds, 0.0f);
	if (bReplaySkipInputActive)
	{
		ReplaySkipProgress = FMath::Min(
			ReplaySkipProgress + SafeDelta / RequiredSeconds,
			1.0f);
	}
	else if (bReplaySkipRewinding)
	{
		ReplaySkipProgress = FMath::Max(
			ReplaySkipProgress
				- SafeDelta * IGFifthDawn::ReplaySkipRewindMultiplier / RequiredSeconds,
			0.0f);
		bReplaySkipRewinding = ReplaySkipProgress > 0.0f;
	}

	UpdateSensoryHudSkip();
	if (ReplaySkipProgress >= 1.0f)
	{
		FinishInterlude();
		return;
	}
	if (!bReplaySkipInputActive && !bReplaySkipRewinding)
	{
		SetActorTickEnabled(false);
	}
}

bool AIGMissingFloorFifthDawnDirector::StartInterlude(
	AIGPlayerCharacter* InPlayer)
{
	if (bActive || !IsValid(InPlayer) || !GetWorld())
	{
		return false;
	}

	Player = InPlayer;
	ElapsedSeconds = 0.0f;
	FiredCueMask = 0;
	PlayerKnockCount = 0;
	NextCueIndex = 1;
	bPlayerListening = false;
	ReplaySkipProgress = 0.0f;
	bReplaySkipInputActive = false;
	bReplaySkipRewinding = false;
	bReplayAvailabilityForcedForSession =
		FParse::Param(FCommandLine::Get(), TEXT("IGFifthDawnReplay"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreyboxProbe"));
	bReplaySkipAvailable = HasExperiencedInterludeProfile()
		|| bReplayAvailabilityForcedForSession;
	bActive = true;
	StartWorldSeconds = GetWorld()->GetTimeSeconds();
	if (!bReplaySkipAvailable)
	{
		// §6. 처음 보는 사람도 붙잡아 두지 않는다. 15초면 이 막간이 무엇인지는
		// 알게 된다. 그 뒤로는 E를 길게 눌러 넘어간다.
		GetWorldTimerManager().SetTimer(
			FirstViewSkipTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (!bActive)
				{
					return;
				}
				bReplaySkipAvailable = true;
				UpdateSensoryHudSkip();
			}),
			IGReplaySkip::FirstViewDelaySeconds,
			false);
	}
	// 역사층의 소리는 눈을 감은 자리와 그때 보던 방향에 놓는다. 화면은 검어도
	// 그 자리의 공간이라 고개를 돌리면 소리도 돈다.
	InterludeOrigin = InPlayer->GetActorLocation();
	const FRotator InterludeYaw(0.0f, InPlayer->GetControlRotation().Yaw, 0.0f);
	InterludeForward = InterludeYaw.Vector();
	InterludeRight = InterludeYaw.Quaternion().GetRightVector();
	WaterBaseVolume = 0.28f;
	PrayerDawnIndex = 0;
	PrayerBaseVolume = IGFifthDawn::PrayerDawnVolumes[0];

	// 이름은 겹치지 않게 짓는다. 끝날 때 지운 베드가 아직 수거되지 않았을 수 있다.
	WaterBed = NewObject<UAudioComponent>(this, MakeUniqueObjectName(
		this, UAudioComponent::StaticClass(), TEXT("FifthDawnWaterBed")));
	PrayerBed = NewObject<UAudioComponent>(this, MakeUniqueObjectName(
		this, UAudioComponent::StaticClass(), TEXT("FifthDawnPrayerBed")));
	BreathBed = NewObject<UAudioComponent>(this, MakeUniqueObjectName(
		this, UAudioComponent::StaticClass(), TEXT("FifthDawnBreathBed")));
	if (!WaterBed || !PrayerBed || !BreathBed)
	{
		bActive = false;
		return false;
	}
	UIGMissingFloorAudioSubsystem* AudioDirector =
		GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>();
	// 세 베드는 상주 자리로 건다. 비상주로 두면 노크가 몇 번 겹칠 때 가장
	// 오래된 소리인 이 베드들이 먼저 밀려 꺼진다.
	WaterBed->RegisterComponent();
	WaterBed->SetSound(
		UIGToneSequenceSoundWave::CreateFloodedCorridorWaterBed(this));
	// [오른쪽] 물이 흐르는 소리. 벽 너머 배관이라 오른쪽에서 난다.
	WaterBed->bAllowSpatialization = true;
	WaterBed->AttenuationSettings = IGAudio::MakeAttenuation(
		this, 420.0f, 1600.0f, EIGAudioBus::World);
	WaterBed->SetWorldLocation(
		InterludeOrigin + InterludeRight * 280.0f + FVector(0.0f, 0.0f, 30.0f));
	WaterBed->bAutoDestroy = false;
	WaterBed->SetVolumeMultiplier(WaterBaseVolume);
	if (AudioDirector)
	{
		AudioDirector->RegisterPersistentBed(WaterBed, EIGAudioBus::World);
	}
	WaterBed->Play();

	PrayerBed->RegisterComponent();
	PrayerBed->SetSound(
		UIGToneSequenceSoundWave::CreateMuffledPrayerRadio(this));
	PrayerBed->bAllowSpatialization = false;
	PrayerBed->bAutoDestroy = false;
	PrayerBed->SetLowPassFilterEnabled(true);
	PrayerBed->SetLowPassFilterFrequency(IGFifthDawn::PrayerDawnLowPassHz[0]);
	PrayerBed->SetVolumeMultiplier(PrayerBaseVolume);
	if (AudioDirector)
	{
		AudioDirector->RegisterPersistentBed(PrayerBed, EIGAudioBus::World);
	}
	PrayerBed->Play();

	BreathBed->RegisterComponent();
	BreathBed->SetSound(
		UIGToneSequenceSoundWave::CreateTrappedBreathBed(this));
	BreathBed->bAllowSpatialization = false;
	BreathBed->bAutoDestroy = false;
	BreathBed->SetVolumeMultiplier(0.18f);
	if (AudioDirector)
	{
		AudioDirector->RegisterPersistentBed(BreathBed, EIGAudioBus::Player);
	}
	BreathBed->Play();

	InPlayer->GetCharacterMovement()->DisableMovement();
	if (APlayerController* Controller =
		Cast<APlayerController>(InPlayer->GetController()))
	{
		if (Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StartCameraFade(
				0.0f,
				1.0f,
				0.85f,
				FLinearColor::Black,
				false,
				true);
		}
	}
	SetSensoryHud(true);
	// 안내만 보일 때는 정적이다. 홀드하거나 진행률을 되감을 때만 Tick을 켠다.
	SetActorTickEnabled(false);
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UIGMissingFloorNarrativeSubsystem* Narrative =
			GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
		{
			// Also blocks H/F9 while the scene is dark; the physical day seal is
			// still owned by AIGNightPhaseDirector.
			Narrative->SetHourSealed(true);
		}
	}

	FireCue(0);
	ScheduleNextCue();
	return true;
}

void AIGMissingFloorFifthDawnDirector::ScheduleNextCue()
{
	UWorld* World = GetWorld();
	if (!bActive || !World
		|| NextCueIndex >= static_cast<int32>(UE_ARRAY_COUNT(IGFifthDawn::CueTimes)))
	{
		return;
	}

	// 각 큐를 이전 큐가 아니라 시작 시각에 맞춰 예약한다. 한 프레임이
	// 늦어져도 160초 타임라인 전체에 오차가 누적되지 않는다.
	const double WorldElapsed = World->GetTimeSeconds() - StartWorldSeconds;
	const float Delay = FMath::Max(
		static_cast<float>(IGFifthDawn::CueTimes[NextCueIndex] - WorldElapsed),
		KINDA_SMALL_NUMBER);
	World->GetTimerManager().SetTimer(
		CueTimerHandle,
		this,
		&AIGMissingFloorFifthDawnDirector::HandleNextCue,
		Delay,
		false);
}

void AIGMissingFloorFifthDawnDirector::HandleNextCue()
{
	if (!bActive
		|| NextCueIndex >= static_cast<int32>(UE_ARRAY_COUNT(IGFifthDawn::CueTimes)))
	{
		return;
	}

	// 24초 경계처럼 같은 시각에 놓인 큐는 인덱스 순서로 한 번에 처리한다.
	const float CueTime = IGFifthDawn::CueTimes[NextCueIndex];
	ElapsedSeconds = CueTime;
	do
	{
		FireCue(NextCueIndex);
		++NextCueIndex;
	}
	while (bActive
		&& NextCueIndex < static_cast<int32>(UE_ARRAY_COUNT(IGFifthDawn::CueTimes))
		&& FMath::IsNearlyEqual(IGFifthDawn::CueTimes[NextCueIndex], CueTime));

	ScheduleNextCue();
}

void AIGMissingFloorFifthDawnDirector::FireCue(const int32 CueIndex)
{
	if (CueIndex < 0 || CueIndex >= 32
		|| (FiredCueMask & (1u << CueIndex)) != 0)
	{
		return;
	}
	FiredCueMask |= 1u << CueIndex;

	// 그가 두드리던 벽. 눈을 감을 때 보던 쪽 바로 앞이다.
	const FVector HandOnWall =
		InterludeOrigin + InterludeForward * 45.0f + FVector(0.0f, 0.0f, 40.0f);
	switch (CueIndex)
	{
	case 0:
		PushDirectionCaption(
			NSLOCTEXT(
				"IGMissingFloor",
				"FifthDawnCaptionStart",
				"[7월 27일 · 가까이] 얕은 숨소리  ·  [오른쪽] 물 흐르는 소리"),
			4.0f);
		// 7월 27일. 아직 힘이 있는 두 번. 뒤의 새벽들은 여기서부터 약해진다.
		ScheduleInterludeSound(8.0f, FTimerDelegate::CreateWeakLambda(this, [this, HandOnWall]()
		{
			if (!bActive)
			{
				return;
			}
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.05f),
				HandOnWall,
				0.85f,
				1.0f,
				120.0f,
				900.0f,
				EIGAudioBus::Player);
		}));
		ScheduleInterludeSound(8.45f, FTimerDelegate::CreateWeakLambda(this, [this, HandOnWall]()
		{
			if (!bActive)
			{
				return;
			}
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.05f),
				HandOnWall,
				0.85f,
				1.0f,
				120.0f,
				900.0f,
				EIGAudioBus::Player);
		}));
		break;
	case 1:
	{
		// 24초. 검은 화면이 로딩으로 읽히기 전에 소리의 자리를 옮긴다. 배관이
		// 머리 위에서 튀고, 오른쪽에 있던 물도 그리로 올라간다.
		const FVector Overhead = InterludeOrigin + FVector(0.0f, 0.0f, 135.0f);
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateSettlePipeKnock(this),
			Overhead,
			1.0f,
			1.0f,
			200.0f,
			1400.0f,
			EIGAudioBus::World);
		if (WaterBed)
		{
			WaterBed->SetWorldLocation(Overhead + InterludeRight * 60.0f);
		}
		PushDirectionCaption(
			NSLOCTEXT(
				"IGMissingFloor",
				"FifthDawnCaptionWaterShift",
				"[머리 위] 배관 크게 덜컹거리는 소리"),
			2.6f);
		break;
	}
	case 2:
		DipPrayerForDawn(1, IGFifthDawn::PrayerReturnSeconds);
		if (PrayerBed)
		{
			PrayerBed->SetPitchMultiplier(0.97f);
		}
		if (BreathBed)
		{
			BreathBed->SetVolumeMultiplier(bPlayerListening ? 0.025f : 0.16f);
		}
		PushDirectionCaption(
			NSLOCTEXT(
				"IGMissingFloor",
				"FifthDawnCaptionSecondDawn",
				"[7월 28일] 발소리와 불경 소리가 점점 멀어진다"),
			2.8f);
		// 배관이 가라앉은 뒤 계단을 내려가는 발소리 셋. 아무도 올라오지 않는다.
		ScheduleInterludeSound(3.0f, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bActive)
			{
				return;
			}
			IGAudio::SpawnOneShotAt(
				this,
				IGAudio::SampleVariantOr(
					TEXT("Foot_Concrete"), 5, 7u,
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSurfaceFootstep(this, EIGFootstepSurface::Concrete, 0.95f, 0.8f); }),
				InterludeOrigin + FVector(-260.0f, -120.0f, -180.0f),
				0.42f,
				1.0f,
				150.0f,
				1600.0f,
				EIGAudioBus::World);
		}));
		ScheduleInterludeSound(3.8f, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bActive)
			{
				return;
			}
			IGAudio::SpawnOneShotAt(
				this,
				IGAudio::SampleVariantOr(
					TEXT("Foot_Concrete"), 5, 11u,
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSurfaceFootstep(this, EIGFootstepSurface::Concrete, 0.95f, 0.8f); }),
				InterludeOrigin + FVector(-380.0f, -160.0f, -300.0f),
				0.28f,
				1.0f,
				150.0f,
				1600.0f,
				EIGAudioBus::World);
		}));
		ScheduleInterludeSound(4.7f, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bActive)
			{
				return;
			}
			IGAudio::SpawnOneShotAt(
				this,
				IGAudio::SampleVariantOr(
					TEXT("Foot_Concrete"), 5, 13u,
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSurfaceFootstep(this, EIGFootstepSurface::Concrete, 0.95f, 0.8f); }),
				InterludeOrigin + FVector(-500.0f, -200.0f, -420.0f),
				0.16f,
				1.0f,
				150.0f,
				1600.0f,
				EIGAudioBus::World);
		}));
		break;
	case 3:
		DipPrayerForDawn(2, IGFifthDawn::PrayerReturnSeconds);
		if (BreathBed)
		{
			BreathBed->SetVolumeMultiplier(bPlayerListening ? 0.025f : 0.14f);
		}
		PushDirectionCaption(
			NSLOCTEXT(
				"IGMissingFloor",
				"FifthDawnCaptionThirdDawn",
				"[7월 29일] 벽을 긁는 소리"),
			2.8f);
		// 손끝이 안벽을 끌고 내려간다. 두 번째는 더 짧고 힘이 없다.
		ScheduleInterludeSound(1.5f, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bActive)
			{
				return;
			}
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateCardboardDrag(this),
				InterludeOrigin + InterludeForward * 35.0f + FVector(0.0f, 0.0f, 20.0f),
				0.32f,
				0.72f,
				100.0f,
				700.0f,
				EIGAudioBus::Player);
		}));
		ScheduleInterludeSound(3.2f, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bActive)
			{
				return;
			}
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateCardboardDrag(this),
				InterludeOrigin + InterludeForward * 35.0f + FVector(0.0f, 0.0f, 20.0f),
				0.24f,
				0.68f,
				100.0f,
				700.0f,
				EIGAudioBus::Player);
		}));
		break;
	case 4:
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateAnswerKnockPattern(this, 0.84f),
			HandOnWall,
			0.52f,
			1.0f,
			140.0f,
			1000.0f,
			EIGAudioBus::Player);
		// 먼저 두드린 쪽이 그다. 자막으로만 따라오는 사람에게도 부름이 있어야
		// 74초의 대답이 대답으로 읽힌다(§2 7월 29일).
		PushDirectionCaption(
			NSLOCTEXT(
				"IGMissingFloor",
				"FifthDawnCaptionDohaCall",
				"[벽 안] 둘, 쉬고, 하나"),
			2.8f);
		break;
	case 5:
		// 황순금의 대답은 401호 벽 안에서 온다. 밤4에 망치 소리에 답하던 그
		// 자리다. 거리만큼 젖고 층을 지나며 먹먹해져서 [아주 멀리]가 소리로 선다.
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateAnswerKnockPattern(this, 0.93f),
			AIGMissingFloorNightFourDirector::GetUnit401ReplyLocation(),
			0.9f,
			1.0f,
			300.0f,
			3000.0f,
			EIGAudioBus::Entity);
		PushDirectionCaption(
			NSLOCTEXT(
				"IGMissingFloor",
				"FifthDawnCaptionHwangReply",
				"[아주 멀리] 같은 박자로 대답하는 소리"),
			3.2f);
		break;
	case 6:
		DipPrayerForDawn(3, IGFifthDawn::PrayerReturnSeconds);
		WaterBaseVolume = 0.22f;
		if (WaterBed)
		{
			WaterBed->SetVolumeMultiplier(bPlayerListening
				? WaterBaseVolume * IGFifthDawn::WaterListenGain
				: WaterBaseVolume);
		}
		if (BreathBed)
		{
			BreathBed->SetVolumeMultiplier(bPlayerListening ? 0.025f : 0.10f);
		}
		PushDirectionCaption(
			NSLOCTEXT(
				"IGMissingFloor",
				"FifthDawnCaptionFourthDawn",
				"[7월 30일] 느리고 약한 숨소리"),
			2.8f);
		break;
	case 7:
		DipPrayerForDawn(4, IGFifthDawn::PrayerFinalReturnSeconds);
		if (BreathBed)
		{
			BreathBed->SetVolumeMultiplier(bPlayerListening ? 0.025f : 0.07f);
		}
		PushDirectionCaption(
			NSLOCTEXT(
				"IGMissingFloor",
				"FifthDawnCaptionFinalDawn",
				"[7월 31일] 다섯 번째 새벽"),
			2.4f);
		break;
	case 8:
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateWallKnockReply(this),
			HandOnWall,
			0.30f,
			1.0f,
			120.0f,
			900.0f,
			EIGAudioBus::Player);
		PushDirectionCaption(
			NSLOCTEXT(
				"IGMissingFloor",
				"FifthDawnCaptionNoReply",
				"[벽 안] 두 번 두드리는 소리  ·  대답 없음"),
			3.0f);
		break;
	case 9:
		GetWorldTimerManager().ClearTimer(PrayerReturnTimer);
		if (WaterBed)
		{
			WaterBed->Stop();
		}
		if (PrayerBed)
		{
			PrayerBed->Stop();
		}
		PushDirectionCaption(
			NSLOCTEXT(
				"IGMissingFloor",
				"FifthDawnCaptionBreathOnly",
				"[가까이] 희미한 숨소리"),
			3.2f);
		break;
	case 10:
		if (BreathBed)
		{
			BreathBed->Stop();
		}
		break;
	case 11:
		FinishInterlude();
		break;
	default:
		break;
	}
}

bool AIGMissingFloorFifthDawnDirector::RegisterPlayerKnock()
{
	if (!bActive)
	{
		return false;
	}
	if (ElapsedSeconds >= IGFifthDawn::CueTimes[8])
	{
		// 7월 31일 뒤. 손은 움직이는데 소리가 안 난다 — 그가 마지막에 겪은
		// 것을 손가락으로 겪는다. 입력은 먹고, 벽은 답하지 않는다.
		++PlayerKnockCount;
		return true;
	}
	const float Muffle = FMath::Clamp(0.18f + PlayerKnockCount * 0.075f, 0.18f, 0.90f);
	++PlayerKnockCount;
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateWallKnockSingle(this, Muffle),
		Player.IsValid() ? Player->GetActorLocation() : GetActorLocation(),
		FMath::Max(0.24f, 0.72f - PlayerKnockCount * 0.035f),
		1.0f,
		120.0f,
		900.0f,
		EIGAudioBus::Player);
	return true;
}

bool AIGMissingFloorFifthDawnDirector::SetPlayerListening(
	const bool bListening)
{
	if (!bActive)
	{
		return false;
	}
	bPlayerListening = bListening;
	// 바깥은 그 새벽의 크기에서 커진다. 날이 갈수록 멀어진 예불은 숨을
	// 죽여도 첫 새벽만큼 가까워지지 않는다.
	if (WaterBed && WaterBed->IsPlaying())
	{
		WaterBed->SetVolumeMultiplier(bListening
			? WaterBaseVolume * IGFifthDawn::WaterListenGain
			: WaterBaseVolume);
	}
	if (PrayerBed && PrayerBed->IsPlaying())
	{
		PrayerBed->SetVolumeMultiplier(bListening
			? PrayerBaseVolume * IGFifthDawn::PrayerListenGain
			: PrayerBaseVolume);
	}
	if (BreathBed && BreathBed->IsPlaying())
	{
		const float BreathBase = ElapsedSeconds < 52.0f
			? 0.18f
			: ElapsedSeconds < 80.0f
				? 0.14f
				: ElapsedSeconds < 115.0f
					? 0.10f
					: 0.07f;
		BreathBed->SetVolumeMultiplier(bListening ? 0.025f : BreathBase);
	}
	return true;
}

bool AIGMissingFloorFifthDawnDirector::BeginReplaySkipInput()
{
	if (!bActive || !bReplaySkipAvailable)
	{
		return false;
	}

	if (UsesToggleSkipInput() && bReplaySkipInputActive)
	{
		bReplaySkipInputActive = false;
		bReplaySkipRewinding = ReplaySkipProgress > 0.0f;
	}
	else
	{
		bReplaySkipInputActive = true;
		bReplaySkipRewinding = false;
	}
	SetActorTickEnabled(true);
	UpdateSensoryHudSkip();
	return true;
}

bool AIGMissingFloorFifthDawnDirector::EndReplaySkipInput()
{
	if (!bActive || !bReplaySkipAvailable)
	{
		return false;
	}
	if (!UsesToggleSkipInput())
	{
		bReplaySkipInputActive = false;
		bReplaySkipRewinding = ReplaySkipProgress > 0.0f;
		SetActorTickEnabled(true);
		UpdateSensoryHudSkip();
	}
	return true;
}

bool AIGMissingFloorFifthDawnDirector::ValidateTimeline() const
{
	return UE_ARRAY_COUNT(IGFifthDawn::CueTimes) == 12
		&& FMath::IsNearlyEqual(IGFifthDawn::CueTimes[0], 0.0f)
		&& FMath::IsNearlyEqual(IGFifthDawn::CueTimes[1], 24.0f)
		&& FMath::IsNearlyEqual(IGFifthDawn::CueTimes[3], 52.0f)
		&& FMath::IsNearlyEqual(IGFifthDawn::CueTimes[4], 58.0f)
		&& FMath::IsNearlyEqual(IGFifthDawn::CueTimes[5], 74.0f)
		&& FMath::IsNearlyEqual(IGFifthDawn::CueTimes[7], 115.0f)
		&& FMath::IsNearlyEqual(IGFifthDawn::CueTimes[9], 148.0f)
		&& FMath::IsNearlyEqual(IGFifthDawn::CueTimes[10], 159.2f)
		&& FMath::IsNearlyEqual(IGFifthDawn::CueTimes[11], 160.0f);
}

bool AIGMissingFloorFifthDawnDirector::CompleteImmediatelyForProbe()
{
	if (!bActive)
	{
		return false;
	}
	// 자동 검증이 개발 PC의 관람 이력을 재관람 상태로 바꾸면 안 된다.
	FinishInterlude(/*bPersistExperience=*/false);
	return true;
}

void AIGMissingFloorFifthDawnDirector::FinishInterlude(
	const bool bPersistExperience)
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	bReplaySkipInputActive = false;
	bReplaySkipRewinding = false;
	SetActorTickEnabled(false);
	GetWorldTimerManager().ClearTimer(CueTimerHandle);
	GetWorldTimerManager().ClearTimer(FirstViewSkipTimer);
	ReleaseInterludeAudio();
	SetSensoryHud(false);
	if (!Player.Get())
	{
		// 폰이 사라진 채로 끝났다. 복구가 폰에 묶여 있으면 암전과 이동
		// 잠금이 그대로 남는다.
		AbortSlotBlackout(TEXT("slot finished without a pawn"));
	}
	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		PlayerCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		if (APlayerController* Controller =
			Cast<APlayerController>(PlayerCharacter->GetController()))
		{
			if (Controller->PlayerCameraManager)
			{
				Controller->PlayerCameraManager->StartCameraFade(
					1.0f,
					0.0f,
					0.8f,
					FLinearColor::Black,
					false,
					false);
			}
		}
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UIGMissingFloorNarrativeSubsystem* Narrative =
			GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
		{
			Narrative->SetFifthDawnInterludeCompleted(true);
		}
	}
	if (bPersistExperience && !bReplayAvailabilityForcedForSession)
	{
		PersistInterludeExperience();
	}
	OnCompleted.Broadcast();
}

void AIGMissingFloorFifthDawnDirector::SetSensoryHud(
	const bool bEnabled) const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	const APlayerController* Controller = PlayerCharacter
		? Cast<APlayerController>(PlayerCharacter->GetController())
		: nullptr;
	if (AIGHorrorHUD* Hud = Controller
		? Cast<AIGHorrorHUD>(Controller->GetHUD())
		: nullptr)
	{
		Hud->SetSensoryInterludePresentation(bEnabled);
		Hud->SetSensoryInterludeSkipState(
			bEnabled && bReplaySkipAvailable,
			bEnabled ? ReplaySkipProgress : 0.0f,
			bEnabled && bReplaySkipInputActive,
			GetReplaySkipDurationSeconds(),
			UsesToggleSkipInput());
	}
}

void AIGMissingFloorFifthDawnDirector::UpdateSensoryHudSkip() const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	const APlayerController* Controller = PlayerCharacter
		? Cast<APlayerController>(PlayerCharacter->GetController())
		: nullptr;
	if (AIGHorrorHUD* Hud = Controller
		? Cast<AIGHorrorHUD>(Controller->GetHUD())
		: nullptr)
	{
		Hud->SetSensoryInterludeSkipState(
			bReplaySkipAvailable,
			ReplaySkipProgress,
			bReplaySkipInputActive,
			GetReplaySkipDurationSeconds(),
			UsesToggleSkipInput());
	}
}

float AIGMissingFloorFifthDawnDirector::GetReplaySkipDurationSeconds() const
{
	const UIGAccessibilitySubsystem* Accessibility = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	const float DurationScale = Accessibility
		? Accessibility->GetHoldDurationScale()
		: 1.0f;
	return FMath::Max(
		IGFifthDawn::ReplaySkipDurationSeconds * DurationScale,
		0.25f);
}

bool AIGMissingFloorFifthDawnDirector::UsesToggleSkipInput() const
{
	const UIGAccessibilitySubsystem* Accessibility = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	return Accessibility && Accessibility->UsesToggleHoldInteractions();
}

bool AIGMissingFloorFifthDawnDirector::HasExperiencedInterludeProfile() const
{
	bool bExperienced = false;
	if (GConfig)
	{
		GConfig->GetBool(
			IGFifthDawn::ProfileSection,
			IGFifthDawn::ExperiencedKey,
			bExperienced,
			GGameUserSettingsIni);
	}
	return bExperienced;
}

void AIGMissingFloorFifthDawnDirector::PersistInterludeExperience() const
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetBool(
		IGFifthDawn::ProfileSection,
		IGFifthDawn::ExperiencedKey,
		true,
		GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void AIGMissingFloorFifthDawnDirector::PushDirectionCaption(
	const FText& Caption,
	const float Seconds) const
{
	AIGHorrorHUD::PushAudioCaption(this, Caption, Seconds);
}

void AIGMissingFloorFifthDawnDirector::AbortSlotBlackout(const TCHAR* Reason)
{
	UWorld* World = GetWorld();
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* Controller = Character
		? Cast<APlayerController>(Character->GetController())
		: nullptr;
	// 폰이 사라져도 화면을 가진 컨트롤러는 남는다. 암전은 그쪽 카메라
	// 매니저가 들고 있으므로 거기서 걷는다.
	if (!Controller && World)
	{
		Controller = World->GetFirstPlayerController();
	}
	if (Controller && Controller->PlayerCameraManager)
	{
		Controller->PlayerCameraManager->StopCameraFade();
	}
	if (Character)
	{
		if (UCharacterMovementComponent* Movement =
			Character->GetCharacterMovement())
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
	UE_LOG(
		LogIndieGame,
		Warning,
		TEXT("IG_NIGHT5_SLOT aborted blackout: %s (controller=%s)"),
		Reason,
		Controller ? TEXT("yes") : TEXT("none"));
}

void AIGMissingFloorFifthDawnDirector::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	// 슬롯은 이동을 잠그고 bHoldWhenFinished로 암전을 걸어 둔 채 30초를 돈다.
	// 그 사이에 디렉터가 사라지면 푸는 쪽이 아무도 없다 — 이벤트도 프롬프트도
	// 없이 검은 화면에 조작만 죽은 상태가 남는다.
	if (bActive && EndPlayReason != EEndPlayReason::LevelTransition
		&& EndPlayReason != EEndPlayReason::EndPlayInEditor
		&& EndPlayReason != EEndPlayReason::Quit)
	{
		AbortSlotBlackout(TEXT("director destroyed while the slot was running"));
	}
	bActive = false;
	GetWorldTimerManager().ClearTimer(CueTimerHandle);
	GetWorldTimerManager().ClearTimer(FirstViewSkipTimer);
	ReleaseInterludeAudio();
	SetSensoryHud(false);
	SetActorTickEnabled(false);
	Super::EndPlay(EndPlayReason);
}

void AIGMissingFloorFifthDawnDirector::DipPrayerForDawn(
	const int32 DawnIndex,
	const float ReturnDelaySeconds)
{
	PrayerDawnIndex = FMath::Clamp(
		DawnIndex,
		0,
		static_cast<int32>(UE_ARRAY_COUNT(IGFifthDawn::PrayerDawnVolumes)) - 1);
	if (!PrayerBed)
	{
		return;
	}
	// 0으로 페이드하면 엔진이 소리를 멈춘다. 되살릴 때는 FadeIn으로 새로 건다 —
	// 예불도 새벽마다 처음부터 다시 시작한다.
	PrayerBed->FadeOut(IGFifthDawn::PrayerDipSeconds, 0.0f);
	GetWorldTimerManager().SetTimer(
		PrayerReturnTimer,
		this,
		&AIGMissingFloorFifthDawnDirector::ReturnPrayer,
		FMath::Max(ReturnDelaySeconds, IGFifthDawn::PrayerDipSeconds),
		false);
}

void AIGMissingFloorFifthDawnDirector::ReturnPrayer()
{
	if (!bActive || !PrayerBed)
	{
		return;
	}
	PrayerBaseVolume = IGFifthDawn::PrayerDawnVolumes[PrayerDawnIndex];
	PrayerBed->SetLowPassFilterFrequency(
		IGFifthDawn::PrayerDawnLowPassHz[PrayerDawnIndex]);
	PrayerBed->SetVolumeMultiplier(bPlayerListening
		? PrayerBaseVolume * IGFifthDawn::PrayerListenGain
		: PrayerBaseVolume);
	PrayerBed->FadeIn(IGFifthDawn::PrayerReturnFadeSeconds, 1.0f);
}

void AIGMissingFloorFifthDawnDirector::ScheduleInterludeSound(
	const float DelaySeconds,
	const FTimerDelegate& Sound)
{
	FTimerHandle& Handle = InterludeSoundTimers.AddDefaulted_GetRef();
	GetWorldTimerManager().SetTimer(Handle, Sound, DelaySeconds, false);
}

void AIGMissingFloorFifthDawnDirector::ReleaseInterludeAudio()
{
	GetWorldTimerManager().ClearTimer(PrayerReturnTimer);
	for (FTimerHandle& Handle : InterludeSoundTimers)
	{
		GetWorldTimerManager().ClearTimer(Handle);
	}
	InterludeSoundTimers.Reset();
	// 상주 자리로 건 베드다. 멈추기만 하면 막간이 끝난 뒤에도 버스 자리를
	// 쥐고 있으므로 지운다.
	if (WaterBed)
	{
		WaterBed->Stop();
		WaterBed->DestroyComponent();
		WaterBed = nullptr;
	}
	if (PrayerBed)
	{
		PrayerBed->Stop();
		PrayerBed->DestroyComponent();
		PrayerBed = nullptr;
	}
	if (BreathBed)
	{
		BreathBed->Stop();
		BreathBed->DestroyComponent();
		BreathBed = nullptr;
	}
}
