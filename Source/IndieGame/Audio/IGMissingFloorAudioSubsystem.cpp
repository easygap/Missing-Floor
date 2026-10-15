#include "Audio/IGMissingFloorAudioSubsystem.h"

#include "Audio/IGAmbienceSoundWave.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "AudioDevice.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Sound/ReverbEffect.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "HAL/PlatformTime.h"

namespace IGMissingFloorMix
{
	constexpr float SilentDecibels = -96.0f;
	constexpr float EntityNearDistance = 600.0f;
	constexpr float EntityFarHysteresis = 640.0f;
	constexpr float EntityDistanceLeaseSeconds = 0.55f;
	constexpr float TitleKnockDelaySeconds = 4.0f;
	constexpr float TitleReplyDelaySeconds = 1.15f;
	constexpr float TitleCycleSeconds = 12.0f;
	/**
	 * 엔딩 카드가 닫힌 뒤의 첫 타이틀. 무음 카드 뒤에 3초를 더 비우고, 모티프는
	 * 6초에 걸쳐 오르며, 그의 첫 노크는 24초에 온다. 한 번뿐이다.
	 */
	constexpr float PostEndingTitleSilenceSeconds = 3.0f;
	constexpr float PostEndingTitleFadeSeconds = 6.0f;
	constexpr float PostEndingFirstKnockSeconds = 24.0f;
	/** 소리 맞추기에서 음악·환경음 칸을 바꿨을 때 들려주는 길이. */
	constexpr double CalibrationPreviewSeconds = 2.6;

	/**
	 * 진실 확정 타건. 확정은 대개 망치 타격이나 차단기 딸깍과 같은 프레임에 온다.
	 * 그 어택을 비켜 조금 늦게 치고, 여럿이 한꺼번에 확정되면 모티프의 타건
	 * 간격으로 한 음씩 친다. 지정 침묵이 걷힌 뒤에는 음악 버스가 돌아올 틈을 준다.
	 */
	constexpr double TruthStrikeGraceSeconds = 0.9;
	constexpr double TruthStrikeSpacingSeconds = 1.70;
	constexpr float TruthStrikeAfterSilenceSeconds = 0.6f;

	/**
	 * §10.4 거리 리버브 문법. 두 프리셋은 같은 태그를 공유하므로 뒤에
	 * 올린 쪽이 앞의 것을 밀어내며, 엔진이 FadeTime으로 섞어 준다.
	 * 계단을 한 칸 오르는 시간보다 짧아야 공간 전환이 발소리와 맞는다.
	 */
	const FName AcousticSpaceReverbTag(TEXT("MissingFloor.AcousticSpace"));
	constexpr float AcousticSpaceReverbPriority = 1.0f;
	constexpr float AcousticCrossfadeSeconds = 0.90f;
	constexpr float AcousticPollIntervalSeconds = 0.20f;

	/**
	 * 지상에서 빌라 밖. 골목·옆 샛길·편의점 바닥에는 발밑 표면 태그가 없어서
	 * 표면으로 가르면 복도가 된다. 빌라 남쪽 외벽 바깥면(Y −395)보다 남쪽이
	 * 골목이고, 승강기 샤프트(X 880까지) 동쪽이 옆 샛길과 편의점이다. 편의점은
	 * 좁고 물건이 들어차 복도 잔향이 맞지 않으니 같이 마른 소리로 둔다. 로비와
	 * 필로티 주차장은 이 선 안쪽이라 그대로 복도다.
	 */
	constexpr float OutdoorGroundMaxZ = 300.0f;
	constexpr float VillaSouthFaceY = -395.0f;
	constexpr float VillaEastEdgeX = 900.0f;

	/**
	 * 복도 — 1.2m 폭, 2.4m 천장, 석고 마감에 세대문 세 짝. 맨 콘크리트
	 * 복도의 실측 잔향은 2초에 가깝지만, 이 게임의 시그니처 입력은
	 * 「둘, 쉬고, 하나」 리듬이다. 1.25초로 당겨야 세 번의 타격이 서로
	 * 뭉개지지 않고 플레이어가 되받아 칠 수 있다. 현실보다 서사가 먼저다.
	 */
	constexpr float CorridorDecayTime = 1.25f;
	constexpr float CorridorDecayHFRatio = 0.72f;

	/**
	 * 계단실 — 마감 없는 콘크리트 수직 통로. 흡음이 없어 고역이 거의
	 * 그대로 남고, 평행한 계단참 사이에서 플러터 에코가 생겨 후미가
	 * 거칠어진다(Diffusion 낮음). 복도의 두 배 넘게 울리는 이 차이가
	 * 「내 발소리가 계단에서 훨씬 오래 남는다」는 §5.1의 학습을 만든다.
	 */
	constexpr float StairwellDecayTime = 2.70f;
	constexpr float StairwellDecayHFRatio = 0.95f;

	/** 존재의 소리는 완전히 마르지 않는다. 마르면 같은 방이라는 뜻이다. */
	constexpr float EntityReverbFloor = 0.22f;
	constexpr float EntityReverbCeiling = 0.95f;
	constexpr float PuzzleReverbFloor = 0.15f;
	constexpr float PuzzleReverbCeiling = 0.80f;
	constexpr float WorldReverbFloor = 0.10f;
	constexpr float WorldReverbCeiling = 0.55f;
	/** 내 몸의 소리는 거리가 항상 0이므로 고정 센드로 공간을 태운다. */
	constexpr float PlayerManualReverbSend = 0.30f;

	constexpr float BaseDecibels[] =
	{
		0.0f,   // ENTITY
		-3.0f,  // PLAYER
		-1.0f,  // PUZZLE
		-8.0f,  // WORLD
		-10.0f, // UI
		-6.0f,  // SCORE
		-2.0f   // SPEECH
	};

	// 상한은 한 번 울고 마는 소리만 센다. 늘 우는 베드(험·물소리·끌림·숨·음악)는
	// 자리를 차지하지 않고 밀리지도 않는다.
	constexpr int32 VoiceCaps[] =
	{
		4,  // ENTITY
		6,  // PLAYER
		6,  // PUZZLE
		12, // WORLD
		8,  // UI: navigation remains responsive under caption churn
		// SCORE: 거리 압박, 현재 음악, 빠지는 꼬리는 베드라 세지 않는다. 남은 자리는
		// 확인음처럼 한 번 울고 마는 음악 몫이다.
		4,
		// SPEECH: 목소리는 상시 소리로 걸어 밀리지 않는다. 대사는 한 번에 한 줄이다.
		2
	};

	/**
	 * 어떤 버스가 건물 반향을 타는가. UI와 스코어는 방 안에서 나는 소리가
	 * 아니므로 SoundClass 단계에서 끈다. FSoundClassProperties의 bReverb
	 * 기본값이 true라서 반드시 명시해야 한다.
	 */
	constexpr bool BusUsesBuildingReverb[] =
	{
		true,  // ENTITY
		true,  // PLAYER
		true,  // PUZZLE
		true,  // WORLD
		false, // UI
		false, // SCORE
		true   // SPEECH: 방 안에서 나는 말이다
	};

	const TCHAR* BusNames[] =
	{
		TEXT("BUS_ENTITY"),
		TEXT("BUS_PLAYER"),
		TEXT("BUS_PUZZLE"),
		TEXT("BUS_WORLD"),
		TEXT("BUS_UI"),
		TEXT("BUS_SCORE"),
		TEXT("BUS_SPEECH")
	};

	float DecibelsToLinear(const float Decibels)
	{
		return Decibels <= SilentDecibels
			? 0.0f
			: FMath::Pow(10.0f, Decibels / 20.0f);
	}

	int32 ToIndex(const EIGAudioBus Bus)
	{
		return FMath::Clamp(
			static_cast<int32>(Bus),
			0,
			static_cast<int32>(EIGAudioBus::Count) - 1);
	}
}

void UIGMissingFloorAudioSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BuildBusGraph();
	BuildAcousticPresets();
}

void UIGMissingFloorAudioSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (bMixPushed && RuntimeMix)
		{
			UGameplayStatics::PopSoundMixModifier(World, RuntimeMix);
		}
		if (bAcousticSpaceApplied)
		{
			UGameplayStatics::DeactivateReverbEffect(
				World,
				IGMissingFloorMix::AcousticSpaceReverbTag);
		}
		if (FAudioDevice* Device = World->GetAudioDeviceRaw())
		{
			for (USoundClass* Bus : BusSoundClasses)
			{
				Device->UnregisterSoundClass(Bus);
			}
		}
	}
	StopScore(0.0f);
	if (IsValid(ChaseTailComponent))
	{
		ChaseTailComponent->Stop();
	}
	ChaseTailComponent = nullptr;
	ChaseReleaseStopAtSeconds = -1.0;
	if (IsValid(PresenceComponent))
	{
		PresenceComponent->Stop();
		PresenceComponent->DestroyComponent();
	}
	PresenceComponent = nullptr;
	if (IsValid(StingerComponent))
	{
		StingerComponent->Stop();
	}
	StingerComponent = nullptr;
	if (IsValid(CalibrationPreviewComponent))
	{
		CalibrationPreviewComponent->Stop();
	}
	CalibrationPreviewComponent = nullptr;
	CalibrationPreviewStopRealTime = -1.0;
	bMixPushed = false;
	bAcousticSpaceApplied = false;
	bAcousticSpaceResolved = false;
	RuntimeMix = nullptr;
	AcousticPresets.Reset();
	BusSoundClasses.Reset();
	for (TArray<FTrackedVoice>& Voices : ActiveVoices)
	{
		Voices.Reset();
	}
	Super::Deinitialize();
}

void UIGMissingFloorAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// NewObject로 만든 SoundClass는 에셋의 PostLoad를 거치지 않는다.
	// 실제 장치에 등록하지 않으면 버스·더킹·음량 설정이 모두 적용되지 않는다.
	if (FAudioDevice* Device = InWorld.GetAudioDeviceRaw())
	{
		for (USoundClass* Bus : BusSoundClasses)
		{
			Device->RegisterSoundClass(Bus);
		}
	}
	if (RuntimeMix && !bMixPushed)
	{
		UGameplayStatics::PushSoundMixModifier(&InWorld, RuntimeMix);
		bMixPushed = true;
	}
	RefreshMix(0.0f);
	// 첫 프레임부터 복도가 울려 있어야 한다. 아직 한 걸음도 걷지 않은
	// 플레이어는 밟은 표면이 없으므로 폴링이 값을 줄 수 없다.
	SetAcousticSpace(AcousticSpace);
}

void UIGMissingFloorAudioSubsystem::Tick(const float DeltaTime)
{
	if (bTitleMode && !bTitleSoundscapeHeld)
	{
		const double Now = FPlatformTime::Seconds();
		// 엔딩 직후의 타이틀. 비워 둔 몇 초가 지나면 모티프가 천천히 오른다.
		if (PendingTitleScoreRealTime > 0.0 && Now >= PendingTitleScoreRealTime)
		{
			PendingTitleScoreRealTime = -1.0;
			bPostEndingScoreFade = true;
			SwitchScore(EIGAudioThreatState::Calm, true);
			bPostEndingScoreFade = false;
		}
		if (PendingTitleReplyRealTime > 0.0
			&& Now >= PendingTitleReplyRealTime)
		{
			PendingTitleReplyRealTime = -1.0;
			PlayTitleReply();
		}
		if (NextTitleKnockRealTime > 0.0 && Now >= NextTitleKnockRealTime)
		{
			PlayTitleKnockCycle();
		}
	}
	// 소리 맞추기의 미리 듣기는 메뉴가 월드를 멈춰 둔 동안 운다. 실시간으로 끝낸다.
	if (CalibrationPreviewStopRealTime > 0.0
		&& FPlatformTime::Seconds() >= CalibrationPreviewStopRealTime)
	{
		StopCalibrationPreview(0.4f);
	}
	VoicePruneAccumulator += DeltaTime;
	if (VoicePruneAccumulator >= 0.25f)
	{
		VoicePruneAccumulator = 0.0f;
		PruneVoices();
		const bool bSpeech = ActiveVoices[IGMissingFloorMix::ToIndex(EIGAudioBus::Speech)].ContainsByPredicate(
			[](const FTrackedVoice& Voice) { return Voice.Component.IsValid() && Voice.Component->IsPlaying(); });
		if (bSpeech != bSpeechPlaying)
		{
			bSpeechPlaying = bSpeech;
			RefreshMix(0.25f);
		}
	}

	AcousticPollAccumulator += DeltaTime;
	if (AcousticPollAccumulator >= IGMissingFloorMix::AcousticPollIntervalSeconds)
	{
		AcousticPollAccumulator = 0.0f;
		PollAcousticSpace();
	}

	const UWorld* World = GetWorld();
	// 추격이 끝나고 내려 둔 루프는 꼬리 시간이 지나도 추격이 돌아오지 않으면 멈춘다.
	if (ChaseReleaseStopAtSeconds > 0.0 && World
		&& World->GetTimeSeconds() >= ChaseReleaseStopAtSeconds)
	{
		ChaseReleaseStopAtSeconds = -1.0;
		if (IsValid(ReleasingScoreComponent)
			&& ReleasingScoreState == EIGAudioThreatState::Chasing)
		{
			ReleasingScoreComponent->Stop();
			ReleasingScoreComponent = nullptr;
		}
	}
	// 진실 확정 타건은 한 번에 하나씩. 침묵 안에서는 기다리고 타이틀에서는 치지 않는다.
	if (PendingTruthStrikes.Num() > 0 && !bTitleMode && !bAuthoredSilence && World
		&& World->GetTimeSeconds() >= NextTruthStrikeWorldSeconds)
	{
		const int32 StrikeIndex = PendingTruthStrikes[0];
		PendingTruthStrikes.RemoveAt(0);
		PlayTruthStrikeNow(StrikeIndex);
		NextTruthStrikeWorldSeconds =
			World->GetTimeSeconds() + IGMissingFloorMix::TruthStrikeSpacingSeconds;
	}
	if (bEntityNearPlayer && World
		&& World->GetTimeSeconds() - LastEntityDistanceUpdateSeconds
			> IGMissingFloorMix::EntityDistanceLeaseSeconds)
	{
		bEntityNearPlayer = false;
		RefreshMix();
	}
	// 거리 보고가 끊기면(잠들었거나 사라졌거나) 압박 층도 내려간다.
	if (PresenceAlpha > 0.0f && World
		&& World->GetTimeSeconds() - LastEntityDistanceUpdateSeconds > 1.5f)
	{
		FadePresenceLayer(0.0f, 1.2f);
	}
}

TStatId UIGMissingFloorAudioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(
		UIGMissingFloorAudioSubsystem,
		STATGROUP_Tickables);
}

void UIGMissingFloorAudioSubsystem::PrepareSound(
	USoundBase* Sound,
	const EIGAudioBus Bus) const
{
	if (!Sound)
	{
		return;
	}
	if (USoundClass* SoundClass = GetBusSoundClass(Bus))
	{
		Sound->SoundClassObject = SoundClass;
	}
}

void UIGMissingFloorAudioSubsystem::SetHeadphoneOutput(const bool bHeadphones)
{
	bHeadphoneOutput = bHeadphones;
	IGAudio::SetOutputMode(
		bHeadphones
			? IGAudio::EIGOutputMode::Headphones
			: IGAudio::EIGOutputMode::Speakers);

	const ESoundSpatializationAlgorithm Algorithm =
		IGAudio::GetSpatializationAlgorithm();
	for (int32 BusIndex = 0; BusIndex < BusCount; ++BusIndex)
	{
		for (FTrackedVoice& Voice : ActiveVoices[BusIndex])
		{
			UAudioComponent* Component = Voice.Component.Get();
			if (!Component)
			{
				continue;
			}
			// 감쇠 개체는 소리마다 새로 만든 것이라 여기서 고쳐도 남의
			// 소리에 번지지 않는다.
			USoundAttenuation* Attenuation = Component->AttenuationSettings;
			if (!Attenuation)
			{
				continue;
			}
			Attenuation->Attenuation.SpatializationAlgorithm = Algorithm;
			Component->AdjustAttenuation(Attenuation->Attenuation);
		}
	}
}

void UIGMissingFloorAudioSubsystem::RegisterComponent(
	UAudioComponent* Component,
	const EIGAudioBus Bus)
{
	RegisterVoice(Component, Bus, /*bPersistent=*/false);
}

void UIGMissingFloorAudioSubsystem::RegisterVoice(
	UAudioComponent* Component,
	const EIGAudioBus Bus,
	const bool bPersistent,
	const bool bExpendable)
{
	if (!Component)
	{
		return;
	}

	const int32 BusIndex = IGMissingFloorMix::ToIndex(Bus);
	if (BusSoundClasses.IsValidIndex(BusIndex))
	{
		Component->SoundClassOverride = BusSoundClasses[BusIndex];
		PrepareSound(Component->GetSound(), Bus);
	}

	// 상시 베드는 소모음이 될 수 없다. 둘 다 켜져 오면 베드로 본다.
	const bool bNewExpendable = bExpendable && !bPersistent;
	TArray<FTrackedVoice>& Voices = ActiveVoices[BusIndex];
	Voices.RemoveAll([](const FTrackedVoice& Voice)
	{
		return !Voice.Component.IsValid();
	});
	// 같은 루프를 다시 보호할 때 자리를 두 번 차지하지 않게 한다.
	for (FTrackedVoice& Voice : Voices)
	{
		if (Voice.Component == Component)
		{
			Voice.bPersistent = bPersistent;
			Voice.bExpendable = bNewExpendable;
			return;
		}
	}

	// 상한은 한 번 울고 마는 소리만 센다. 베드는 자리를 차지하지도 누구를 밀지도
	// 않는다. 예전에는 베드까지 세면서 밀기는 비상시 소리만 밀어서, 끌림과 숨이
	// ENTITY 네 자리 중 둘을 쥐고 있었고 추격 진입 스팅어가 걸음 두 번에 밀렸다.
	if (!bPersistent)
	{
		const int32 VoiceCap = GetVoiceCap(Bus);
		int32 TransientCount = 0;
		for (const FTrackedVoice& Voice : Voices)
		{
			TransientCount += Voice.bPersistent ? 0 : 1;
		}
		while (TransientCount >= VoiceCap && TransientCount > 0)
		{
			// 소모음(기는 걸음)이 있으면 그중 가장 오래된 것, 없으면 가장 오래된 소리.
			int32 OldestIndex = INDEX_NONE;
			for (int32 Index = 0; Index < Voices.Num(); ++Index)
			{
				if (Voices[Index].bPersistent)
				{
					continue;
				}
				if (OldestIndex != INDEX_NONE)
				{
					const FTrackedVoice& Candidate = Voices[Index];
					const FTrackedVoice& Current = Voices[OldestIndex];
					const bool bCheaperTier =
						Candidate.bExpendable && !Current.bExpendable;
					const bool bOlderSameTier =
						Candidate.bExpendable == Current.bExpendable
						&& Candidate.Serial < Current.Serial;
					if (!bCheaperTier && !bOlderSameTier)
					{
						continue;
					}
				}
				OldestIndex = Index;
			}
			if (OldestIndex == INDEX_NONE)
			{
				// 셀 수 있는 소리가 없다. 여기서 계속 돌면 멈추지 않는다.
				return;
			}
			if (bNewExpendable && !Voices[OldestIndex].bExpendable)
			{
				// 대본 소리는 보존하고 추가 걸음을 짧게 걷는다. 추적만 생략하면
				// 걸음이 계속 재생되어 버스 상한과 출력 방식 변경을 모두 우회한다.
				Component->FadeOut(0.04f, 0.0f);
				return;
			}
			if (UAudioComponent* Oldest = Voices[OldestIndex].Component.Get())
			{
				Oldest->FadeOut(0.12f, 0.0f);
			}
			Voices.RemoveAt(OldestIndex);
			--TransientCount;
		}
	}

	Voices.Add({Component, NextVoiceSerial++, bPersistent, bNewExpendable});
}

void UIGMissingFloorAudioSubsystem::RegisterPersistentBed(
	UAudioComponent* Component,
	const EIGAudioBus Bus)
{
	RegisterVoice(Component, Bus, /*bPersistent=*/true);
}

void UIGMissingFloorAudioSubsystem::RegisterExpendable(
	UAudioComponent* Component,
	const EIGAudioBus Bus)
{
	RegisterVoice(Component, Bus, /*bPersistent=*/false, /*bExpendable=*/true);
}

void UIGMissingFloorAudioSubsystem::SetThreatState(
	const EIGAudioThreatState NewState)
{
	if (ThreatState == NewState)
	{
		return;
	}
	// 대치 뒤 그가 잠들며 보내는 Calm은 붙든 피날레 드론을 걷지 않는다. 상태는
	// Finale로 남겨 두어야 나중에 붙듦을 풀고 Calm을 보낼 때 드론이 실제로 빠진다.
	// 다른 상태가 오면 장면이 이미 넘어간 것이므로 붙듦을 푼다.
	if (bFinaleScoreHeld)
	{
		if (NewState == EIGAudioThreatState::Calm)
		{
			return;
		}
		bFinaleScoreHeld = false;
	}
	ThreatState = NewState;
	bEntityListening = NewState == EIGAudioThreatState::Listening;
	if (NewState == EIGAudioThreatState::Captured)
	{
		FadePresenceLayer(0.0f, 0.12f);
	}
	RefreshMix();
	if (!bTitleMode)
	{
		SwitchScore(NewState);
	}
}

void UIGMissingFloorAudioSubsystem::SetAcousticSpace(
	const EIGAcousticSpace NewSpace)
{
	const int32 SpaceIndex = FMath::Clamp(
		static_cast<int32>(NewSpace),
		0,
		AcousticSpaceCount - 1);
	const EIGAcousticSpace Clamped = static_cast<EIGAcousticSpace>(SpaceIndex);
	if (bAcousticSpaceResolved && AcousticSpace == Clamped)
	{
		return;
	}
	const bool bFirstResolve = !bAcousticSpaceResolved;
	AcousticSpace = Clamped;
	bAcousticSpaceResolved = true;
	// 첫 판정은 즉시 걸어야 시작 프레임이 드라이하게 새지 않는다. 이후
	// 공간 변화는 계단 한 칸을 오르는 시간 안에 섞인다.
	ApplyAcousticSpace(
		bFirstResolve ? 0.0f : IGMissingFloorMix::AcousticCrossfadeSeconds);
}

EIGAcousticSpace UIGMissingFloorAudioSubsystem::ClassifyAcousticSpace(
	const EIGFootstepSurface Surface)
{
	switch (Surface)
	{
	case EIGFootstepSurface::MetalStair:
		return EIGAcousticSpace::Stairwell;
	case EIGFootstepSurface::Rooftop:
		// 옥상은 반사면이 바닥 하나뿐이다. 건물 반향을 태우면 실내가 된다.
		return EIGAcousticSpace::Open;
	default:
		return EIGAcousticSpace::Corridor;
	}
}

UReverbEffect* UIGMissingFloorAudioSubsystem::GetAcousticPreset(
	const EIGAcousticSpace Space) const
{
	const int32 SpaceIndex = static_cast<int32>(Space);
	return AcousticPresets.IsValidIndex(SpaceIndex)
		? AcousticPresets[SpaceIndex]
		: nullptr;
}

void UIGMissingFloorAudioSubsystem::ConfigureReverbSend(
	FSoundAttenuationSettings& Settings,
	const EIGAudioBus Bus,
	const float InnerRadius,
	const float FalloffDistance)
{
	if (!IGMissingFloorMix::BusUsesBuildingReverb[IGMissingFloorMix::ToIndex(Bus)])
	{
		Settings.bEnableReverbSend = false;
		return;
	}

	Settings.bEnableReverbSend = true;

	if (Bus == EIGAudioBus::Player)
	{
		// 내 발소리와 호흡은 청취자와 같은 자리에서 난다. 거리로 젖음을
		// 구하면 언제나 최솟값이 되어 콘크리트 계단에서도 마른 소리가
		// 난다. 그래서 센드는 고정하고, 대신 프리셋이 길이를 바꾼다 —
		// 계단에서 내 소리가 오래 남는다는 사실이 §5.1의 압박이 된다.
		Settings.ReverbSendMethod = EReverbSendMethod::Manual;
		Settings.ManualReverbSendLevel =
			IGMissingFloorMix::PlayerManualReverbSend;
		return;
	}

	// 가까울수록 마르고 멀수록 젖는다. 플레이어는 이 한 축으로 거리를 읽고,
	// 드라이하게 들리는 순간을 「같은 방」으로 배운다.
	Settings.ReverbSendMethod = EReverbSendMethod::Linear;
	Settings.ReverbDistanceMin = FMath::Clamp(InnerRadius, 120.0f, 400.0f);
	Settings.ReverbDistanceMax = FMath::Max(
		Settings.ReverbDistanceMin + 200.0f,
		FMath::Max(1.0f, FalloffDistance) * 0.85f);
	switch (Bus)
	{
	case EIGAudioBus::Entity:
		Settings.ReverbWetLevelMin = IGMissingFloorMix::EntityReverbFloor;
		Settings.ReverbWetLevelMax = IGMissingFloorMix::EntityReverbCeiling;
		break;
	case EIGAudioBus::Puzzle:
		// 배관과 물은 구조체를 타고 온다. 소품보다 항상 더 젖어 있다.
		Settings.ReverbWetLevelMin = IGMissingFloorMix::PuzzleReverbFloor;
		Settings.ReverbWetLevelMax = IGMissingFloorMix::PuzzleReverbCeiling;
		break;
	case EIGAudioBus::World:
	default:
		Settings.ReverbWetLevelMin = IGMissingFloorMix::WorldReverbFloor;
		Settings.ReverbWetLevelMax = IGMissingFloorMix::WorldReverbCeiling;
		break;
	}
}

void UIGMissingFloorAudioSubsystem::SetPlayerListening(const bool bListening)
{
	if (bPlayerListening == bListening)
	{
		return;
	}
	bPlayerListening = bListening;
	RefreshMix();
}

void UIGMissingFloorAudioSubsystem::SetEntityListening(const bool bListening)
{
	// 조사 끝의 제자리 청취(Holding)는 음악이 조사 드론 그대로라 SetThreatState가
	// 알 수 없다. 청취 창의 WORLD −6dB만 여기서 따로 건다.
	if (bEntityListening == bListening)
	{
		return;
	}
	bEntityListening = bListening;
	RefreshMix();
}

void UIGMissingFloorAudioSubsystem::SetAuthoredSilence(
	const bool bSilent,
	const float ReleaseFadeSeconds)
{
	if (bAuthoredSilence == bSilent)
	{
		return;
	}
	bAuthoredSilence = bSilent;
	if (bSilent && IsValid(StingerComponent))
	{
		StingerComponent->FadeOut(0.08f, 0.0f);
	}
	const float ReleaseSeconds = FMath::Max(0.0f, ReleaseFadeSeconds);
	if (!bSilent)
	{
		// 침묵 안에서 확정된 진실은 침묵이 걷힌 뒤에 친다. 음악 버스가 −∞에서
		// 돌아올 틈을 먼저 주고, 복귀가 길면 그 중간쯤에서 쳐 공기와 같이 오르게 한다.
		if (const UWorld* World = GetWorld())
		{
			NextTruthStrikeWorldSeconds = FMath::Max(
				NextTruthStrikeWorldSeconds,
				World->GetTimeSeconds() + FMath::Max(
					IGMissingFloorMix::TruthStrikeAfterSilenceSeconds,
					ReleaseSeconds * 0.6f));
		}
	}
	RefreshMix(bSilent ? 0.08f : ReleaseSeconds);
}

void UIGMissingFloorAudioSubsystem::SetEntityDistance(
	const float DistanceCentimeters)
{
	if (const UWorld* World = GetWorld())
	{
		LastEntityDistanceUpdateSeconds = World->GetTimeSeconds();
	}
	const bool bWasNear = bEntityNearPlayer;
	if (bEntityNearPlayer)
	{
		bEntityNearPlayer = DistanceCentimeters
			<= IGMissingFloorMix::EntityFarHysteresis;
	}
	else
	{
		bEntityNearPlayer = DistanceCentimeters
			<= IGMissingFloorMix::EntityNearDistance;
	}
	if (bWasNear != bEntityNearPlayer)
	{
		RefreshMix();
	}
	UpdatePresenceLayer(DistanceCentimeters);
}

void UIGMissingFloorAudioSubsystem::UpdatePresenceLayer(const float DistanceCentimeters)
{
	UWorld* World = GetWorld();
	if (!World || bTitleMode)
	{
		return;
	}
	if (ThreatState == EIGAudioThreatState::Captured)
	{
		FadePresenceLayer(0.0f, 0.12f);
		return;
	}
	// 그가 듣는 거리의 3m 바깥에서 0, 3m 안에서 1. 밤이 깊을수록 그가 멀리서
	// 들으므로 공기도 멀리서부터 무거워진다(밤1 12m, 밤2·3 14m, 밤4 16m). 1.6제곱이라
	// 멀리서는 거의 없고 가까워질수록 급하게 차오른다. 추격 중에는 거리와 무관하게
	// 절반은 깔린다.
	const float Reach = GetPresenceReachCentimeters();
	float Target = FMath::Pow(
		FMath::Clamp((Reach - DistanceCentimeters) / (Reach - 300.0f), 0.0f, 1.0f), 1.6f);
	// 그가 듣는 창에는 세계가 숨을 참는다(§10.2). WORLD가 −6dB 내려가도 코앞에서는
	// 이 층의 저역이 그 신호를 덮으므로, 같은 −6dB(진폭 절반)를 여기에도 건다.
	if (bEntityListening)
	{
		Target *= 0.5f;
	}
	if (ThreatState == EIGAudioThreatState::Chasing)
	{
		Target = FMath::Max(Target, 0.5f);
	}
	if (Target <= 0.001f && !PresenceComponent)
	{
		return;
	}
	if (!PresenceComponent)
	{
		UIGToneSequenceSoundWave* Layer = UIGToneSequenceSoundWave::CreatePresenceLayer(this);
		PrepareSound(Layer, EIGAudioBus::Score);
		// 볼륨 배수는 1이다. 예전엔 0으로 만들고 AdjustVolume으로 올렸는데,
		// 엔진은 배수 × 페이더로 소리를 내므로 페이더가 어디로 가든 곱은 0이었다
		// — 이 층은 2026-09-09에 넣은 뒤로 한 번도 들린 적이 없다. 재생은
		// 아래 FadePresenceLayer가 페이더 0에서 올리며 시작한다. 자동 파괴를
		// 끄는 것은 0으로 내리는 페이드가 엔진에서 정지이기 때문이다 — 지워지면
		// 다음에 올릴 때 새 컴포넌트를 또 만들어야 한다.
		PresenceComponent = UGameplayStatics::CreateSound2D(
			World, Layer, 1.0f, 1.0f, 0.0f, nullptr, false, false);
		if (!PresenceComponent)
		{
			return;
		}
		PresenceComponent->SetUISound(false);
		RegisterPersistentBed(PresenceComponent, EIGAudioBus::Score);
	}
	FadePresenceLayer(Target, 0.35f);
}

void UIGMissingFloorAudioSubsystem::FadePresenceLayer(const float Target, const float Seconds)
{
	const float Clamped = FMath::Clamp(Target, 0.0f, 1.0f);
	// 작은 음량 변화는 묶되, 완전히 멎는 전환은 빠뜨리지 않는다.
	// 거의 멀어진 상태에서 0으로 내려갈 때도 루프를 정리해야 한다.
	const bool bStopping = Clamped == 0.0f && PresenceAlpha > 0.0f;
	if (!bStopping && FMath::IsNearlyEqual(PresenceAlpha, Clamped, 0.01f))
	{
		return;
	}
	PresenceAlpha = Clamped;
	if (!PresenceComponent)
	{
		return;
	}
	// 0으로 가는 페이드는 엔진이 끝에서 정지로 처리한다. 그래서 다시 올릴 때는
	// 멈춘 컴포넌트를 FadeIn으로 되살리고, 도는 중이면 페이더만 옮긴다 — 내려가는
	// 도중에 AdjustVolume을 받으면 엔진이 정지 예약을 풀고 다시 올라간다.
	const float Level = Clamped * 0.9f;
	if (Level <= 0.005f)
	{
		if (PresenceComponent->IsPlaying())
		{
			PresenceComponent->FadeOut(Seconds, 0.0f);
		}
		return;
	}
	if (!PresenceComponent->IsPlaying())
	{
		PresenceComponent->FadeIn(Seconds, Level);
		return;
	}
	PresenceComponent->AdjustVolume(Seconds, Level);
}

bool UIGMissingFloorAudioSubsystem::PlayStinger(const EIGStinger Kind, const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!World || bTitleMode || bAuthoredSilence)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	// 재탐색과 추격이 짧게 오가도 추격 진입 타격을 연달아 내지 않는다.
	if (Kind == EIGStinger::ChaseStart && Now - LastChaseStingerSeconds < 6.0)
	{
		return false;
	}
	if (Kind == EIGStinger::CloseCall
		&& (ThreatState == EIGAudioThreatState::Chasing
			|| ThreatState == EIGAudioThreatState::Captured
			|| Now - LastChaseStingerSeconds < 6.0
			|| (IsValid(StingerComponent) && StingerComponent->IsPlaying())))
	{
		return false;
	}
	USoundBase* Wave = nullptr;
	float Volume = 1.0f;
	switch (Kind)
	{
	case EIGStinger::CloseCall:
		Wave = IGAudio::SampleOr(
			TEXT("Stinger_CloseCall"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateCloseCallStinger(this); });
		Volume = 0.95f;
		break;
	case EIGStinger::ChaseStart:
		// 굳은 몸이 튀어 나가는 소리다. 금속 타격과 땅울림, 목소리는 없다(§4.6
		// 포효·괴성 금지 — 사람 비명 녹음 Entity_Scream은 쓰지 않는다). 녹음이
		// 없으면 손바닥·미장·끌림으로 짠 합성음으로 내려간다.
		Wave = IGAudio::SampleOr(
			TEXT("Stinger_ChaseStart"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateEntityChaseScream(this); });
		Volume = 1.0f;
		break;
	case EIGStinger::Capture:
		Wave = IGAudio::SampleOr(
			TEXT("Entity_Grab"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateCaptureLunge(this); });
		Volume = 1.0f;
		break;
	}
	if (!Wave)
	{
		return false;
	}
	if (IsValid(StingerComponent) && StingerComponent->IsPlaying())
	{
		StingerComponent->FadeOut(0.08f, 0.0f);
	}
	// 놀람은 그의 걸음이나 다른 원샷에 밀리지 않고, 제가 대본 소리를 밀지도 않는다.
	// 처음부터 상시 소리로 건다. 일반 소리로 먼저 들어오면 ENTITY 네 자리가 차
	// 있을 때 문 노크의 철판 타격 같은 대본 소리 하나를 밀어낸 뒤에야 상시가
	// 됐다. 끝나면 스스로 지워지고 PruneVoices가 목록에서 걷는다.
	StingerComponent = IGAudio::SpawnPersistentOneShotAt(
		this, Wave, Location, Volume, 1.0f, 240.0f, 2600.0f, EIGAudioBus::Entity);
	if (StingerComponent && Kind == EIGStinger::ChaseStart)
	{
		LastChaseStingerSeconds = Now;
	}
	return StingerComponent != nullptr;
}

void UIGMissingFloorAudioSubsystem::SetTitleMode(const bool bEnabled)
{
	if (bTitleMode == bEnabled)
	{
		return;
	}
	bTitleMode = bEnabled;
	if (bTitleMode)
	{
		// 지난 밤에 치지 못한 확정음이 타이틀이나 다음 게임으로 새지 않는다.
		PendingTruthStrikes.Reset();
		// 소리 맞추기에서 틀던 미리 듣기가 타이틀 음악 위에 남지 않는다.
		StopCalibrationPreview();
		StartTitleSoundscape();
	}
	else
	{
		StopTitleSoundscape();
	}
}

void UIGMissingFloorAudioSubsystem::HoldTitleSoundscape(const bool bHold)
{
	if (!bTitleMode || bTitleSoundscapeHeld == bHold)
	{
		return;
	}
	bTitleSoundscapeHeld = bHold;
	if (!bHold)
	{
		// 처음 타이틀에 들어온 것과 같다. 모티프가 1.2초에 걸쳐 오르고 노크는 4초 뒤.
		StartTitleSoundscape();
		return;
	}
	// 밤 5의 30초는 대부분 침묵이다. 타이틀의 조율과 사냥 노크가 그 자리를 채우면
	// 슬롯이 다른 장면이 된다.
	NextTitleKnockRealTime = -1.0;
	PendingTitleReplyRealTime = -1.0;
	StopScore(1.2f);
}

void UIGMissingFloorAudioSubsystem::ReleaseScore(const float FadeSeconds)
{
	if (bTitleMode)
	{
		return;
	}
	StopScore(FMath::Max(0.0f, FadeSeconds));
	// 걷은 음악을 지금의 위협 상태가 다시 부르지 않게 맞춰 둔다.
	ActiveScoreState = ThreatState;
}

void UIGMissingFloorAudioSubsystem::ReleaseScoreForDawn(const float FadeSeconds)
{
	if (bTitleMode)
	{
		return;
	}
	// 그가 잠드는 것은 눈이 다 감긴 뒤다. 그때 Calm이 와서 추격 꼬리를 새로 치면
	// 스탭이 「문이 열린다. 아침이다.」 위로 4초를 운다. 눈을 감는 동안 먼저 걷고,
	// 뒤따르는 Calm에는 걷을 음악이 남지 않게 한다.
	const float Seconds = FMath::Max(0.05f, FadeSeconds);
	FadeChaseTail(Seconds);
	ChaseReleaseStopAtSeconds = -1.0;
	ReleaseScore(Seconds);
	FadePresenceLayer(0.0f, Seconds);
}

void UIGMissingFloorAudioSubsystem::HoldFinaleScore(const bool bHold)
{
	bFinaleScoreHeld = bHold && !bTitleMode
		&& ThreatState == EIGAudioThreatState::Finale
		&& ActiveScoreState == EIGAudioThreatState::Finale;
}

void UIGMissingFloorAudioSubsystem::DuckFinaleScore(const float Seconds, const float Level)
{
	if (ActiveScoreState != EIGAudioThreatState::Finale || !IsValid(ScoreComponent))
	{
		return;
	}
	// 0으로 가는 페이드는 엔진에서 정지다. 가라앉히기만 하고 끄지 않는다.
	ScoreComponent->AdjustVolume(FMath::Max(0.0f, Seconds), FMath::Max(Level, 0.05f));
}

void UIGMissingFloorAudioSubsystem::SetUserMasterVolume(
	const float Volume01,
	const float FadeSeconds)
{
	const float Clamped = FMath::Clamp(Volume01, 0.25f, 1.0f);
	if (FMath::IsNearlyEqual(UserMasterVolume, Clamped, 0.001f))
	{
		return;
	}
	UserMasterVolume = Clamped;
	RefreshMix(FadeSeconds);
}

float UIGMissingFloorAudioSubsystem::GetBusUserScale(const EIGAudioBus Bus) const
{
	// 이 switch에 ENTITY 분기가 없다는 사실이 §21.1 대원칙의 구현이다.
	switch (Bus)
	{
	case EIGAudioBus::Score:
		return ScoreUserVolume;
	case EIGAudioBus::World:
		return AmbienceUserVolume;
	default:
		return 1.0f;
	}
}

void UIGMissingFloorAudioSubsystem::SetScoreUserVolume(
	const float Volume01,
	const float FadeSeconds)
{
	const float Clamped = FMath::Clamp(Volume01, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(ScoreUserVolume, Clamped, 0.001f))
	{
		return;
	}
	ScoreUserVolume = Clamped;
	RefreshMix(FadeSeconds);
}

void UIGMissingFloorAudioSubsystem::SetAmbienceUserVolume(
	const float Volume01,
	const float FadeSeconds)
{
	const float Clamped = FMath::Clamp(Volume01, MinimumAmbienceVolume, 1.0f);
	if (FMath::IsNearlyEqual(AmbienceUserVolume, Clamped, 0.001f))
	{
		return;
	}
	AmbienceUserVolume = Clamped;
	RefreshMix(FadeSeconds);
}

void UIGMissingFloorAudioSubsystem::PlayCalibrationKnock()
{
	if (!GetWorld())
	{
		return;
	}
	// 보정은 노크 하나로 판단한다. 음악·환경음 미리 듣기가 그 위에 남지 않는다.
	StopCalibrationPreview();
	++CalibrationKnockPlayCount;
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	const APlayerCameraManager* Camera = Controller
		? Controller->PlayerCameraManager
		: nullptr;
	const FVector Location = Camera
		? Camera->GetCameraLocation()
			+ Camera->GetActorForwardVector() * 145.0f
			+ Camera->GetActorRightVector() * 55.0f
			+ FVector::UpVector * 285.0f
		: FVector(145.0f, 55.0f, 285.0f);
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Entity_KnockTriple_Muffled"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockTriple(this, 0.78f); }),
		Location,
		0.62f,
		1.0f,
		110.0f,
		1650.0f,
		EIGAudioBus::Entity,
		true);
}

void UIGMissingFloorAudioSubsystem::PlayTruthConfirmation(
	const int32 ConfirmationIndex)
{
	const UWorld* World = GetWorld();
	if (!World || ConfirmationIndex <= 0)
	{
		return;
	}
	// 확정은 망치 타격이나 수첩을 여는 소리와 같은 프레임에 온다. 그 어택 뒤로
	// 비켜서 치고, 앞선 타건이 남아 있으면 그 뒤에 줄을 선다. 침묵 중이면 Tick이
	// 침묵이 걷힐 때까지 기다린다(P4의 대답은 정적 한가운데서 확정된다).
	PendingTruthStrikes.Add(ConfirmationIndex);
	NextTruthStrikeWorldSeconds = FMath::Max(
		NextTruthStrikeWorldSeconds,
		World->GetTimeSeconds() + IGMissingFloorMix::TruthStrikeGraceSeconds);
}

void UIGMissingFloorAudioSubsystem::PlayTruthStrikeNow(
	const int32 ConfirmationIndex)
{
	UWorld* World = GetWorld();
	if (!World || ConfirmationIndex <= 0)
	{
		return;
	}
	UIGToneSequenceSoundWave* Strike =
		UIGToneSequenceSoundWave::CreateTuningStrike(
			this,
			ConfirmationIndex);
	PrepareSound(Strike, EIGAudioBus::Score);
	UAudioComponent* Component = UGameplayStatics::CreateSound2D(
		World,
		Strike,
		0.82f,
		1.0f,
		0.0f,
		nullptr,
		false,
		true);
	if (Component)
	{
		Component->SetUISound(false);
		RegisterComponent(Component, EIGAudioBus::Score);
		Component->Play();
	}
}

void UIGMissingFloorAudioSubsystem::PlayEndingATuningResolution()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// 조율 걸음은 혼자 울린다. 대기 중인 확정음이 그 위에 얹히지 않는다.
	PendingTruthStrikes.Reset();
	bAuthoredSilence = false;
	RefreshMix(0.45f);
	StopScore(0.45f);
	ActiveScoreState = EIGAudioThreatState::Finale;
	// 붙들어 둔 대치의 드론은 방금 걷었다. 위협 상태를 먼저 Calm으로 두어야 뒤따르는
	// 새벽 전환과 에필로그의 Calm이 조율 걸음을 끊지 않는다. 걸음은 에필로그의
	// ReleaseScore가 암전과 함께 감는다.
	bFinaleScoreHeld = false;
	ThreatState = EIGAudioThreatState::Calm;
	UIGToneSequenceSoundWave* Resolution =
		UIGToneSequenceSoundWave::CreateTuningMotif(this, true);
	PrepareSound(Resolution, EIGAudioBus::Score);
	ScoreComponent = UGameplayStatics::CreateSound2D(
		World,
		Resolution,
		0.78f,
		1.0f,
		0.0f,
		nullptr,
		false,
		true);
	if (ScoreComponent)
	{
		ScoreComponent->SetUISound(false);
		RegisterComponent(ScoreComponent, EIGAudioBus::Score);
		ScoreComponent->Play();
	}
}

float UIGMissingFloorAudioSubsystem::GetBusDuckingDecibels(
	const EIGAudioBus Bus) const
{
	// §21.1 더킹 열의 전부다. ENTITY는 여기에 분기가 없고, 그것이
	// 「존재의 소리는 절대 눌리지 않는다」의 구현이다(§24 즉시 차단 20).
	if (Bus == EIGAudioBus::Score && bAuthoredSilence)
	{
		return IGMissingFloorMix::SilentDecibels;
	}
	if (Bus == EIGAudioBus::World && bAuthoredSilence)
	{
		// WORLD starts at -8 dB; another -16 lands on the authored
		// -24 dB silence floor while player breath remains untouched.
		return -16.0f;
	}
	// 사람의 목소리가 나오는 동안에도 내 숨을 같은 만큼 내린다. 문밖의 말이 숨에 묻히지 않게.
	if (Bus == EIGAudioBus::Player && (bEntityNearPlayer || bSpeechPlaying))
	{
		return -4.0f;
	}
	if (Bus == EIGAudioBus::World && (bPlayerListening || bEntityListening))
	{
		return -6.0f;
	}
	if (Bus == EIGAudioBus::Score && bSpeechPlaying)
	{
		return -6.0f;
	}
	return 0.0f;
}

float UIGMissingFloorAudioSubsystem::GetEffectiveBusDecibels(
	const EIGAudioBus Bus) const
{
	const int32 BusIndex = IGMissingFloorMix::ToIndex(Bus);
	const float Ducking = GetBusDuckingDecibels(Bus);
	if (Ducking <= IGMissingFloorMix::SilentDecibels)
	{
		return IGMissingFloorMix::SilentDecibels;
	}
	return IGMissingFloorMix::BaseDecibels[BusIndex] + Ducking;
}

int32 UIGMissingFloorAudioSubsystem::GetVoiceCap(const EIGAudioBus Bus) const
{
	return IGMissingFloorMix::VoiceCaps[IGMissingFloorMix::ToIndex(Bus)];
}

USoundClass* UIGMissingFloorAudioSubsystem::GetBusSoundClass(
	const EIGAudioBus Bus) const
{
	const int32 BusIndex = IGMissingFloorMix::ToIndex(Bus);
	return BusSoundClasses.IsValidIndex(BusIndex)
		? BusSoundClasses[BusIndex]
		: nullptr;
}

bool UIGMissingFloorAudioSubsystem::ValidateContract(
	FString& OutFailure) const
{
	if (BusSoundClasses.Num() != BusCount || !RuntimeMix)
	{
		OutFailure = TEXT("bus graph was not constructed");
		return false;
	}
	for (int32 Index = 0; Index < BusCount; ++Index)
	{
		const USoundClass* SoundClass = BusSoundClasses[Index];
		if (!SoundClass)
		{
			OutFailure = FString::Printf(TEXT("bus %d has no SoundClass"), Index);
			return false;
		}
		const float Expected = IGMissingFloorMix::DecibelsToLinear(
			IGMissingFloorMix::BaseDecibels[Index]);
		if (!FMath::IsNearlyEqual(SoundClass->Properties.Volume, Expected, 0.001f))
		{
			OutFailure = FString::Printf(
				TEXT("bus %d base gain drifted"), Index);
			return false;
		}
		if (IGMissingFloorMix::VoiceCaps[Index] <= 0)
		{
			OutFailure = FString::Printf(TEXT("bus %d has no voice cap"), Index);
			return false;
		}
		if (SoundClass->Properties.bReverb
			!= IGMissingFloorMix::BusUsesBuildingReverb[Index])
		{
			OutFailure = FString::Printf(
				TEXT("bus %d building-reverb routing drifted"), Index);
			return false;
		}
	}

	// §10.4 거리 리버브 문법: 두 프리셋이 존재하고, 계단실이 복도보다
	// 두 배 이상 길게 울리며 고역을 더 오래 붙잡아야 공간이 구분된다.
	const UReverbEffect* Corridor =
		GetAcousticPreset(EIGAcousticSpace::Corridor);
	const UReverbEffect* Stairwell =
		GetAcousticPreset(EIGAcousticSpace::Stairwell);
	if (!Corridor || !Stairwell)
	{
		OutFailure = TEXT("acoustic space presets were not constructed");
		return false;
	}
	if (GetAcousticPreset(EIGAcousticSpace::Open))
	{
		OutFailure = TEXT("open air must not carry a building reverb preset");
		return false;
	}
	if (Stairwell->DecayTime < Corridor->DecayTime * 2.0f)
	{
		OutFailure = TEXT("stairwell does not ring twice the corridor");
		return false;
	}
	if (Stairwell->DecayHFRatio <= Corridor->DecayHFRatio)
	{
		OutFailure = TEXT("bare concrete must hold high frequencies longer");
		return false;
	}
	if (Stairwell->Diffusion >= Corridor->Diffusion)
	{
		OutFailure = TEXT("stairwell flutter echo must stay grainier");
		return false;
	}

	// 존재의 소리는 항상 건물을 태운다. 마르는 순간은 같은 방을 뜻하므로
	// 하한이 0이면 이 문법이 성립하지 않는다.
	if (IGMissingFloorMix::EntityReverbFloor <= 0.0f
		|| IGMissingFloorMix::EntityReverbCeiling
			<= IGMissingFloorMix::WorldReverbCeiling)
	{
		OutFailure = TEXT("entity cues do not ride the building reverb");
		return false;
	}
	if (IGMissingFloorMix::PlayerManualReverbSend <= 0.0f)
	{
		OutFailure = TEXT("player body cues must ring the space they stand in");
		return false;
	}
	if (!FMath::IsNearlyEqual(
		GetEffectiveBusDecibels(EIGAudioBus::Entity), 0.0f, 0.01f))
	{
		OutFailure = TEXT("entity bus is not invariant at 0 dB");
		return false;
	}
	if (UserMasterVolume < 0.25f || UserMasterVolume > 1.0f)
	{
		OutFailure = TEXT("user master volume escaped the calibrated range");
		return false;
	}
	OutFailure.Reset();
	return true;
}

bool UIGMissingFloorAudioSubsystem::IsTitleReplyTime(
	const FDateTime& LocalTime)
{
	const int32 MinuteOfDay = LocalTime.GetHour() * 60 + LocalTime.GetMinute();
	return MinuteOfDay >= 4 * 60 + 30 && MinuteOfDay <= 5 * 60 + 30;
}

void UIGMissingFloorAudioSubsystem::BuildBusGraph()
{
	BusSoundClasses.SetNum(BusCount);
	for (int32 Index = 0; Index < BusCount; ++Index)
	{
		USoundClass* SoundClass = NewObject<USoundClass>(
			this,
			IGMissingFloorMix::BusNames[Index]);
		SoundClass->Properties.Volume = IGMissingFloorMix::DecibelsToLinear(
			IGMissingFloorMix::BaseDecibels[Index]);
		SoundClass->Properties.Pitch = 1.0f;
		SoundClass->Properties.bIsUISound = Index == static_cast<int32>(EIGAudioBus::UI);
		SoundClass->Properties.bReverb =
			IGMissingFloorMix::BusUsesBuildingReverb[Index];
		// 2D 소리의 기본 센드는 0으로 남긴다. 젖음은 거리를 아는 감쇠
		// 설정에서만 결정되고, 화면에 붙은 소리는 방을 갖지 않는다.
		SoundClass->Properties.Default2DReverbSendAmount = 0.0f;
		BusSoundClasses[Index] = SoundClass;
	}
	RuntimeMix = NewObject<USoundMix>(this, TEXT("MIX_MISSING_FLOOR_RUNTIME"));
}

void UIGMissingFloorAudioSubsystem::BuildAcousticPresets()
{
	AcousticPresets.SetNum(AcousticSpaceCount);

	// 복도: 석고 마감과 세대문이 고역을 먼저 먹는다. 첫 반사가 9ms면
	// 1.5m 안쪽 벽까지의 왕복과 맞고, 확산이 높아 후미가 매끄럽다.
	UReverbEffect* Corridor = NewObject<UReverbEffect>(
		this,
		TEXT("REVERB_MISSING_FLOOR_CORRIDOR"));
	Corridor->bBypassEarlyReflections = false;
	Corridor->bBypassLateReflections = false;
	Corridor->ReflectionsDelay = 0.009f;
	Corridor->ReflectionsGain = 0.28f;
	Corridor->GainHF = 0.62f;
	Corridor->LateDelay = 0.015f;
	Corridor->LateGain = 1.05f;
	Corridor->DecayTime = IGMissingFloorMix::CorridorDecayTime;
	Corridor->DecayHFRatio = IGMissingFloorMix::CorridorDecayHFRatio;
	Corridor->Density = 0.86f;
	Corridor->Diffusion = 0.80f;
	Corridor->AirAbsorptionGainHF = 0.986f;
	Corridor->Gain = 0.32f;
	AcousticPresets[static_cast<int32>(EIGAcousticSpace::Corridor)] = Corridor;

	// 계단실: 마감이 없다. 고역이 거의 그대로 남고(DecayHFRatio 0.95),
	// 평행한 참 사이의 플러터 에코 때문에 확산이 낮아 후미가 거칠다.
	// 첫 반사가 16ms로 늦은 것이 「높은 통로」의 단서다.
	UReverbEffect* Stairwell = NewObject<UReverbEffect>(
		this,
		TEXT("REVERB_MISSING_FLOOR_STAIRWELL"));
	Stairwell->bBypassEarlyReflections = false;
	Stairwell->bBypassLateReflections = false;
	Stairwell->ReflectionsDelay = 0.016f;
	Stairwell->ReflectionsGain = 0.45f;
	Stairwell->GainHF = 0.80f;
	Stairwell->LateDelay = 0.024f;
	Stairwell->LateGain = 1.32f;
	Stairwell->DecayTime = IGMissingFloorMix::StairwellDecayTime;
	Stairwell->DecayHFRatio = IGMissingFloorMix::StairwellDecayHFRatio;
	Stairwell->Density = 0.95f;
	Stairwell->Diffusion = 0.58f;
	Stairwell->AirAbsorptionGainHF = 0.991f;
	Stairwell->Gain = 0.40f;
	AcousticPresets[static_cast<int32>(EIGAcousticSpace::Stairwell)] = Stairwell;

	// Open은 의도적으로 비어 있다. 건물 밖은 프리셋이 아니라 프리셋의
	// 부재로 표현한다.
	AcousticPresets[static_cast<int32>(EIGAcousticSpace::Open)] = nullptr;
}

void UIGMissingFloorAudioSubsystem::ApplyAcousticSpace(const float FadeSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UReverbEffect* Preset = GetAcousticPreset(AcousticSpace);
	if (!Preset)
	{
		if (bAcousticSpaceApplied)
		{
			UGameplayStatics::DeactivateReverbEffect(
				World,
				IGMissingFloorMix::AcousticSpaceReverbTag);
			bAcousticSpaceApplied = false;
		}
		return;
	}

	UGameplayStatics::ActivateReverbEffect(
		World,
		Preset,
		IGMissingFloorMix::AcousticSpaceReverbTag,
		IGMissingFloorMix::AcousticSpaceReverbPriority,
		1.0f,
		FMath::Max(0.0f, FadeSeconds));
	bAcousticSpaceApplied = true;
}

void UIGMissingFloorAudioSubsystem::PollAcousticSpace()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const APlayerController* Controller = World->GetFirstPlayerController();
	const AIGPlayerCharacter* Character = Controller
		? Cast<AIGPlayerCharacter>(Controller->GetPawn())
		: nullptr;
	if (!Character)
	{
		return;
	}
	// 지상의 빌라 밖(골목·옆 샛길·편의점)은 위치로 먼저 가른다. 그 바닥에는
	// 표면 태그가 없다. 밤에는 건물이 잠겨 여기 올 수 없다.
	const FVector At = Character->GetActorLocation();
	if (At.Z < IGMissingFloorMix::OutdoorGroundMaxZ
		&& (At.Y < IGMissingFloorMix::VillaSouthFaceY
			|| At.X > IGMissingFloorMix::VillaEastEdgeX))
	{
		SetAcousticSpace(EIGAcousticSpace::Open);
		return;
	}
	// 마지막으로 밟은 표면은 끈적하게 유지된다. 계단참에 멈춰 서 있어도
	// 계단실 반향이 풀리지 않는 것이 옳다 — 공간은 걷지 않아도 그대로다.
	SetAcousticSpace(
		ClassifyAcousticSpace(Character->GetLastFootstepSurface()));
}

void UIGMissingFloorAudioSubsystem::RefreshMix(const float FadeSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !RuntimeMix)
	{
		return;
	}

	for (int32 Index = 0; Index < BusCount; ++Index)
	{
		const EIGAudioBus Bus = static_cast<EIGAudioBus>(Index);
		const float AdditionalDecibels = GetBusDuckingDecibels(Bus);
		UGameplayStatics::SetSoundMixClassOverride(
			World,
			RuntimeMix,
			BusSoundClasses[Index],
			IGMissingFloorMix::DecibelsToLinear(AdditionalDecibels)
				* UserMasterVolume
				* GetBusUserScale(Bus),
			1.0f,
			FMath::Max(0.0f, FadeSeconds),
			false);
	}
}

void UIGMissingFloorAudioSubsystem::PruneVoices()
{
	for (TArray<FTrackedVoice>& Voices : ActiveVoices)
	{
		Voices.RemoveAll([](const FTrackedVoice& Voice)
		{
			return !Voice.Component.IsValid()
				|| (!Voice.Component->IsPlaying()
					&& Voice.Component->bAutoDestroy);
		});
	}
}

void UIGMissingFloorAudioSubsystem::SwitchScore(
	const EIGAudioThreatState NewState,
	const bool bForce)
{
	if (!bForce && ActiveScoreState == NewState)
	{
		return;
	}

	UAudioComponent* Resume = !bTitleMode && IsValid(ReleasingScoreComponent)
		&& ReleasingScoreState == NewState && ReleasingScoreComponent->IsPlaying()
		? ReleasingScoreComponent.Get() : nullptr;
	if (Resume)
	{
		ReleasingScoreComponent = nullptr;
	}
	// 대치의 드론은 최종 선택 아래서 천천히 빠진다. 0.3초에 걷히면 벽 앞이 갑자기
	// 비어 버린다.
	const float ReleaseSeconds = ActiveScoreState == EIGAudioThreatState::Chasing
		? 4.0f
		: (ActiveScoreState == EIGAudioThreatState::Finale ? 5.0f : 0.30f);
	// 추격이 다른 상태로 풀리면 펄스부터 빼고 스탭 하나를 남긴다. 포획은 제 연출이
	// 있고 타이틀은 제 음악이 있으니 거기서는 예전처럼 통째로 줄인다.
	const bool bChaseRelease = !bTitleMode
		&& ActiveScoreState == EIGAudioThreatState::Chasing
		&& NewState != EIGAudioThreatState::Captured;
	// 붙잡히는 암전과 다시 시작되는 추격 위에는 지난 스탭이 남지 않는다.
	if (NewState == EIGAudioThreatState::Captured)
	{
		FadeChaseTail(0.12f);
	}
	else if (NewState == EIGAudioThreatState::Chasing)
	{
		FadeChaseTail(0.25f);
	}
	StopScore(
		NewState == EIGAudioThreatState::Captured ? 0.12f : ReleaseSeconds,
		bChaseRelease);
	ActiveScoreState = NewState;
	if (Resume)
	{
		// 내려 두었던 그 루프다. 멈출 예약을 거두고 같은 박자에서 다시 올린다.
		ChaseReleaseStopAtSeconds = -1.0;
		ScoreComponent = Resume;
		RegisterPersistentBed(ScoreComponent, EIGAudioBus::Score);
		ScoreComponent->AdjustVolume(0.25f, 1.0f);
		return;
	}

	UIGToneSequenceSoundWave* Score = nullptr;
	float Volume = 1.0f;
	float ScorePitch = 1.0f;
	// §10.2: 조사 드론은 「페이드인」이다. 갑자기 켜지는 스코어는 드론이 아니라
	// 스팅이고, 스팅은 그의 것이지 음악의 것이 아니다. 추격만 빠르게 든다 —
	// 추격 진입 타격이 먼저 왔으니 음악이 그 뒤를 바로 받아야 한다.
	float FadeInSeconds = 0.30f;
	if (bTitleMode)
	{
		Score = UIGToneSequenceSoundWave::CreateTuningMotif(this, false);
		Volume = 0.78f;
		// 엔딩 직후에는 더 천천히 오른다. 배수는 그대로, 페이더만 늦게 올린다.
		FadeInSeconds = bPostEndingScoreFade
			? IGMissingFloorMix::PostEndingTitleFadeSeconds
			: 1.2f;
	}
	else
	{
		// 밤과 티어가 오를수록 같은 음악이 급해진다. 음이 촘촘해지는 것은 밤마다
		// 파형이 맡고, 빨라지는 것은 재생 배율이 맡는다(박자와 음높이가 같이 오른다).
		// 밤2가 118BPM 그대로이고, 밤4 티어3이 약 132BPM이다. 순찰에는 여전히 음악이
		// 없고, 드론은 여전히 그가 조사할 때만 운다.
		int32 NightIndex = 2;
		int32 Tier = 0;
		if (NewState == EIGAudioThreatState::Investigating
			|| NewState == EIGAudioThreatState::Chasing)
		{
			ResolveScoreEscalation(NightIndex, Tier);
		}
		static constexpr float NightChasePitch[] = {1.00f, 1.00f, 1.04f, 1.08f};
		const float ChasePitch = FMath::Min(
			1.12f,
			NightChasePitch[FMath::Clamp(NightIndex, 1, 4) - 1] + 0.015f * Tier);
		switch (NewState)
		{
		case EIGAudioThreatState::Investigating:
			Score = UIGToneSequenceSoundWave::CreateCavityDrone(this, NightIndex);
			// 밤마다 한 걸음씩 앞에 선다. 대치의 드론보다는 늘 뒤다.
			Volume = FMath::Clamp(0.82f + 0.04f * (NightIndex - 2), 0.78f, 0.90f);
			ScorePitch = 1.0f + (ChasePitch - 1.0f) * 0.5f;
			FadeInSeconds = 1.8f;
			break;
		case EIGAudioThreatState::Chasing:
			Score = UIGToneSequenceSoundWave::CreateChaseScore(this, NightIndex);
			Volume = NightIndex <= 1 ? 0.85f : 1.0f;
			ScorePitch = ChasePitch;
			FadeInSeconds = 0.30f;
			break;
		case EIGAudioThreatState::Finale:
			// 대치의 드론은 조사 드론보다 크고, 열 번째 진실의 음을 품는다. 그 음이
			// 제 높이에 있어야 엔딩 A의 조율 걸음이 그것을 푼다. 재생 배율은 1이다.
			Score = UIGToneSequenceSoundWave::CreateCavityDrone(this, 4, true);
			Volume = 0.95f;
			FadeInSeconds = 1.6f;
			break;
		case EIGAudioThreatState::Calm:
		case EIGAudioThreatState::Banging:
		case EIGAudioThreatState::Listening:
		case EIGAudioThreatState::Captured:
		default:
			break;
		}
	}

	if (!Score || !GetWorld())
	{
		return;
	}
	PrepareSound(Score, EIGAudioBus::Score);
	ScoreComponent = UGameplayStatics::CreateSound2D(
		GetWorld(),
		Score,
		Volume,
		ScorePitch,
		0.0f,
		nullptr,
		false,
		true);
	if (ScoreComponent)
	{
		ScoreComponent->SetUISound(bTitleMode);
	}
	RegisterPersistentBed(ScoreComponent, EIGAudioBus::Score);
	if (ScoreComponent)
	{
		ScoreComponent->FadeIn(FadeInSeconds, 1.0f);
	}
}

void UIGMissingFloorAudioSubsystem::StopScore(
	const float FadeSeconds,
	const bool bChaseRelease)
{
	if (IsValid(ReleasingScoreComponent))
	{
		// 이미 교체 중인 음악은 짧게 마무리하고 다음 전환에 자리를 내준다.
		RegisterComponent(ReleasingScoreComponent, EIGAudioBus::Score);
		if (FadeSeconds <= KINDA_SMALL_NUMBER)
		{
			ReleasingScoreComponent->Stop();
		}
		else
		{
			ReleasingScoreComponent->FadeOut(0.12f, 0.0f);
		}
	}
	ReleasingScoreComponent = nullptr;
	if (!IsValid(ScoreComponent))
	{
		ScoreComponent = nullptr;
		return;
	}
	UWorld* World = GetWorld();
	// 방금 빠지기 시작한 꼬리는 끝까지 보호한다. 단서 확인음이 끊을 수 없다.
	if (FadeSeconds <= KINDA_SMALL_NUMBER)
	{
		ScoreComponent->Stop();
	}
	else if (bChaseRelease && World)
	{
		// §10.2 「스탭만 남기고 4초 테일」. 루프 전체를 4초에 걸쳐 줄이면 52Hz
		// 펄스가 대답 노크 둘(58Hz) 위로, 새벽의 날숨 위로 계속 뛴다. 루프는 0.4초
		// 안에 들리지 않을 만큼 내린다(0까지 내리면 엔진이 멈춘다). 꼬리 시간 동안은
		// 살려 두어 그 안에 추격이 돌아오면 같은 박자에서 되살린다.
		ScoreComponent->AdjustVolume(0.4f, 0.02f);
		ReleasingScoreComponent = ScoreComponent;
		ReleasingScoreState = ActiveScoreState;
		ChaseReleaseStopAtSeconds = World->GetTimeSeconds() + FadeSeconds;
		// 펄스가 빠진 자리의 스탭. 방금 들은 루프와 같은 높이에서 친다.
		FadeChaseTail(0.12f);
		UIGToneSequenceSoundWave* Tail = UIGToneSequenceSoundWave::CreateChaseTail(this);
		PrepareSound(Tail, EIGAudioBus::Score);
		ChaseTailComponent = UGameplayStatics::CreateSound2D(
			World,
			Tail,
			ScoreComponent->VolumeMultiplier,
			ScoreComponent->PitchMultiplier,
			0.0f,
			nullptr,
			false,
			true);
		if (ChaseTailComponent)
		{
			ChaseTailComponent->SetUISound(false);
			// 재생 전에 등록하면 상한에 걸려도 FadeOut이 작동하지 않는다.
			// 먼저 재생해야 확인음이 몰렸을 때 이 꼬리를 짧게 걷을 수 있다.
			ChaseTailComponent->Play();
			RegisterExpendable(ChaseTailComponent, EIGAudioBus::Score);
		}
	}
	else
	{
		ScoreComponent->FadeOut(FadeSeconds, 0.0f);
		ReleasingScoreComponent = ScoreComponent;
		ReleasingScoreState = ActiveScoreState;
	}
	ScoreComponent = nullptr;
}

void UIGMissingFloorAudioSubsystem::FadeChaseTail(const float Seconds)
{
	if (IsValid(ChaseTailComponent) && ChaseTailComponent->IsPlaying())
	{
		if (Seconds <= KINDA_SMALL_NUMBER)
		{
			ChaseTailComponent->Stop();
		}
		else
		{
			ChaseTailComponent->FadeOut(Seconds, 0.0f);
		}
	}
	ChaseTailComponent = nullptr;
}

void UIGMissingFloorAudioSubsystem::ResolveScoreEscalation(
	int32& OutNightIndex,
	int32& OutTier) const
{
	OutNightIndex = 2;
	OutTier = 0;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (const UGameInstance* GameInstance = World->GetGameInstance())
	{
		if (const UIGMissingFloorNarrativeSubsystem* Narrative =
			GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
		{
			OutNightIndex = Narrative->GetNightIndex();
		}
	}
	OutNightIndex = FMath::Clamp(OutNightIndex, 1, 4);
	// 그가 실제로 쓰는 티어다. 성급한 밤은 1에서 시작한다
	// (AIGListenerEntity::RefreshNightTuning과 같은 규칙).
	for (TActorIterator<AIGListenerEntity> It(World); It; ++It)
	{
		const AIGListenerEntity* Listener = *It;
		if (!Listener)
		{
			continue;
		}
		const int32 Tier = Listener->GetDifficulty() == EIGNightDifficulty::Hasty
			? FMath::Max(Listener->GetAggressionTier(), 1)
			: Listener->GetAggressionTier();
		OutTier = FMath::Clamp(Tier, 0, 3);
		break;
	}
}

float UIGMissingFloorAudioSubsystem::GetPresenceReachCentimeters() const
{
	// §20.2 기본 청취 반경 9·11·11·13m에 3m를 더한 자리. 낮과 프롤로그처럼 밤이
	// 아닌 때는 밤2 값이다.
	static constexpr float NightReach[] = {1200.0f, 1400.0f, 1400.0f, 1600.0f};
	int32 NightIndex = 2;
	if (const UWorld* World = GetWorld())
	{
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (const UIGMissingFloorNarrativeSubsystem* Narrative =
				GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
			{
				NightIndex = Narrative->GetNightIndex();
			}
		}
	}
	return NightIndex >= 1 && NightIndex <= 4 ? NightReach[NightIndex - 1] : 1400.0f;
}

void UIGMissingFloorAudioSubsystem::StartTitleSoundscape()
{
	FadePresenceLayer(0.0f, 0.12f);
	// 밤의 것은 타이틀로 넘어오지 않는다. 추격 끝의 스탭도, 붙들어 둔 대치의 드론도.
	FadeChaseTail(0.12f);
	ChaseReleaseStopAtSeconds = -1.0;
	bFinaleScoreHeld = false;
	bAuthoredSilence = false;
	RefreshMix(0.25f);
	PendingTitleReplyRealTime = -1.0;
	if (bPostEndingTitleArmed)
	{
		// 엔딩 카드 바로 뒤의 타이틀. 방금 처음 정음에 닿은 귀를 곧바로 미해결
		// 모티프와 그의 노크로 되돌리지 않는다. 몇 초 비운 뒤 모티프가 천천히 오르고
		// 첫 노크는 한참 뒤에 온다. 그다음부터는 평소 주기와 네시 반 대답 그대로다.
		bPostEndingTitleArmed = false;
		StopScore(0.3f);
		const double Now = FPlatformTime::Seconds();
		PendingTitleScoreRealTime = Now + IGMissingFloorMix::PostEndingTitleSilenceSeconds;
		NextTitleKnockRealTime = Now + IGMissingFloorMix::PostEndingFirstKnockSeconds;
		return;
	}
	PendingTitleScoreRealTime = -1.0;
	SwitchScore(EIGAudioThreatState::Calm, true);
	NextTitleKnockRealTime =
		FPlatformTime::Seconds() + IGMissingFloorMix::TitleKnockDelaySeconds;
}

void UIGMissingFloorAudioSubsystem::StopTitleSoundscape()
{
	bTitleSoundscapeHeld = false;
	NextTitleKnockRealTime = -1.0;
	PendingTitleReplyRealTime = -1.0;
	// 엔딩 직후 몇 초 안에 새 게임이나 소리 맞추기로 나가도 모티프가 늦게 켜지지 않는다.
	PendingTitleScoreRealTime = -1.0;
	SwitchScore(ThreatState, true);
}

void UIGMissingFloorAudioSubsystem::ArmPostEndingTitle()
{
	// 이미 타이틀이면 늦었다. 남겨 두면 밤 5를 마치고 돌아올 때 엉뚱하게 발동한다.
	bPostEndingTitleArmed = !bTitleMode;
}

void UIGMissingFloorAudioSubsystem::PlayCalibrationPreview(const bool bMusic)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (IsValid(CalibrationPreviewComponent) && CalibrationPreviewComponent->IsPlaying())
	{
		if (bCalibrationPreviewIsMusic == bMusic)
		{
			// 칸을 연달아 넘기는 동안은 처음부터 다시 틀지 않고 끝만 미룬다.
			CalibrationPreviewStopRealTime =
				Now + IGMissingFloorMix::CalibrationPreviewSeconds;
			return;
		}
		CalibrationPreviewComponent->FadeOut(0.15f, 0.0f);
	}
	CalibrationPreviewComponent = nullptr;
	CalibrationPreviewStopRealTime = -1.0;
	// 지정 침묵 중에 멈췄다면 음악 버스가 −96dB라 들려줄 것이 없다.
	if (bMusic && bAuthoredSilence)
	{
		return;
	}

	const EIGAudioBus Bus = bMusic ? EIGAudioBus::Score : EIGAudioBus::World;
	USoundBase* Sound = nullptr;
	float Volume = 1.0f;
	if (bMusic)
	{
		// 타이틀과 같은 모티프, 같은 크기. 첫 타가 바로 나온다.
		Sound = UIGToneSequenceSoundWave::CreateTuningMotif(this, false);
		Volume = 0.78f;
	}
	else
	{
		// 밤 복도의 공기. 한 겹만 틀므로 게임 안에서 여러 베드가 겹친 크기에 맞춰
		// 조금 올려 둔다.
		Sound = IGAudio::Sample(TEXT("Bed_Corridor"));
		Volume = 0.18f;
		if (!Sound)
		{
			UIGAmbienceSoundWave* Fallback = NewObject<UIGAmbienceSoundWave>(this);
			Fallback->Configure(EIGAmbienceMode::CorridorNight, 0x7A11C0DEu);
			Sound = Fallback;
			Volume = 0.75f;
		}
	}
	PrepareSound(Sound, Bus);
	UAudioComponent* Preview = UGameplayStatics::CreateSound2D(
		World, Sound, Volume, 1.0f, 0.0f, nullptr, false, true);
	if (!Preview)
	{
		return;
	}
	// 메뉴가 월드를 멈춰 두었다. UI 소리여야 엔진이 튼다.
	Preview->SetUISound(true);
	RegisterComponent(Preview, Bus);
	Preview->FadeIn(0.25f, 1.0f);
	CalibrationPreviewComponent = Preview;
	bCalibrationPreviewIsMusic = bMusic;
	CalibrationPreviewStopRealTime = Now + IGMissingFloorMix::CalibrationPreviewSeconds;
}

void UIGMissingFloorAudioSubsystem::StopCalibrationPreview(const float FadeSeconds)
{
	if (IsValid(CalibrationPreviewComponent) && CalibrationPreviewComponent->IsPlaying())
	{
		CalibrationPreviewComponent->FadeOut(FMath::Max(0.05f, FadeSeconds), 0.0f);
	}
	CalibrationPreviewComponent = nullptr;
	CalibrationPreviewStopRealTime = -1.0;
}

void UIGMissingFloorAudioSubsystem::PlayTitleKnockCycle()
{
	if (!bTitleMode || !GetWorld())
	{
		return;
	}
	// 매번 똑같이 치면 시계가 된다. 셋은 늘 치되 세기·높이·간격만 조금씩 흔든다.
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Entity_KnockTriple_Muffled"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockTriple(this, 0.72f); }),
		ResolveTitleCueLocation(false),
		FMath::FRandRange(0.64f, 0.76f),
		FMath::FRandRange(0.97f, 1.02f),
		120.0f,
		1800.0f,
		EIGAudioBus::Entity,
		true);

	const double Now = FPlatformTime::Seconds();
	if (IsTitleReplyTime(FDateTime::Now()))
	{
		PendingTitleReplyRealTime =
			Now + IGMissingFloorMix::TitleReplyDelaySeconds;
	}
	// 평균은 12초(10~14초).
	NextTitleKnockRealTime = Now + IGMissingFloorMix::TitleCycleSeconds
		+ FMath::FRandRange(-2.0f, 2.0f);
}

void UIGMissingFloorAudioSubsystem::PlayTitleReply()
{
	if (!bTitleMode)
	{
		return;
	}
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateWallKnockReply(this),
		ResolveTitleCueLocation(true),
		0.62f,
		0.94f,
		100.0f,
		1700.0f,
		EIGAudioBus::Entity,
		true);
}

FVector UIGMissingFloorAudioSubsystem::ResolveTitleCueLocation(
	const bool bReply) const
{
	const APlayerController* Controller = GetWorld()
		? GetWorld()->GetFirstPlayerController()
		: nullptr;
	const APlayerCameraManager* Camera = Controller
		? Controller->PlayerCameraManager
		: nullptr;
	if (!Camera)
	{
		return FVector(0.0f, 0.0f, bReply ? 420.0f : 280.0f);
	}
	const FVector Forward = Camera->GetActorForwardVector();
	const FVector Right = Camera->GetActorRightVector();
	return Camera->GetCameraLocation()
		+ Forward * (bReply ? -110.0f : 180.0f)
		+ Right * (bReply ? -90.0f : 70.0f)
		+ FVector::UpVector * (bReply ? 430.0f : 260.0f);
}
