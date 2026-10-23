#include "Audio/IGAudioHelpers.h"

#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace IGAudio
{
	namespace
	{
		/** 한 번 찾은 샘플은 붙들어 둔다. 없던 것도 기억해 매번 디스크를 안 본다. */
		TMap<FName, TStrongObjectPtr<USoundBase>>& SampleCache()
		{
			static TMap<FName, TStrongObjectPtr<USoundBase>> Cache;
			return Cache;
		}
		TSet<FName>& MissingSamples()
		{
			static TSet<FName> Missing;
			return Missing;
		}
	}

	USoundBase* Sample(const TCHAR* Name)
	{
		const FName Key(Name);
		if (const TStrongObjectPtr<USoundBase>* Cached = SampleCache().Find(Key))
		{
			return Cached->Get();
		}
		if (MissingSamples().Contains(Key))
		{
			return nullptr;
		}
		const FString Path = FString::Printf(TEXT("/Game/Audio/S_%s.S_%s"), Name, Name);
		USoundBase* Loaded = LoadObject<USoundBase>(nullptr, *Path);
		if (!Loaded)
		{
			MissingSamples().Add(Key);
			return nullptr;
		}
		SampleCache().Add(Key, TStrongObjectPtr<USoundBase>(Loaded));
		return Loaded;
	}

	USoundBase* SampleVariant(const TCHAR* Prefix, const int32 Count, const uint32 Hash)
	{
		if (Count <= 0)
		{
			return nullptr;
		}
		const int32 Index = static_cast<int32>((Hash >> 12) % static_cast<uint32>(Count));
		return Sample(*FString::Printf(TEXT("%s_%d"), Prefix, Index));
	}

	namespace
	{
		// 단일 플레이어 게임이라 전역 하나로 충분하다. 새로 만드는 감쇠는
		// 즉시 이 값을 읽고, 이미 울고 있는 소리는 오디오 감독이 훑어서
		// 다시 걸어 준다.
		EIGOutputMode ActiveOutputMode = EIGOutputMode::Headphones;

		// 기계 몸통 안에서는 세기가 그대로고, 거기서부터 마스킹이 0이 되는
		// 자리까지 떨어진다.
		constexpr float HumBodyRadius = 25.0f;

		// 험은 배경이다. 이 위로 올리면 발소리를 가리는 게 아니라 덮는다.
		constexpr float HumVolumeMultiplier = 0.34f;

		// 최근에 낸 원샷 몇 개를 자리·시각과 함께 기억한다. 소리 자막이 그 소리를 찾아 묶어, 소리가 거둬지면
		// 자막도 같이 내린다. 오래된 칸은 덮어쓴다.
		struct FRecentSound
		{
			TWeakObjectPtr<UAudioComponent> Component;
			TWeakObjectPtr<UWorld> World;
			FVector Location = FVector::ZeroVector;
			double StartedAt = -1.0;
		};
		FRecentSound RecentSounds[32];
		int32 NextRecentSound = 0;

		UAudioComponent* SpawnOneShotInternal(
			const UObject* WorldContext,
			USoundBase* Sound,
			const FVector& Location,
			const float VolumeMultiplier,
			const float PitchMultiplier,
			const float InnerRadius,
			const float FalloffDistance,
			const EIGAudioBus Bus,
			const bool bPlayWhenPaused,
			const bool bForceDry,
			const bool bExpendable = false,
			const AActor* OcclusionOwner = nullptr,
			const bool bPersistent = false)
		{
			if (!Sound || !WorldContext)
			{
				return nullptr;
			}

			UWorld* World = GEngine
				? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
				: nullptr;
			if (!World)
			{
				return nullptr;
			}

			UIGMissingFloorAudioSubsystem* AudioDirector =
				World->GetSubsystem<UIGMissingFloorAudioSubsystem>();
			if (AudioDirector)
			{
				AudioDirector->PrepareSound(Sound, Bus);
			}
			USoundAttenuation* Attenuation = MakeAttenuation(
				Sound,
				InnerRadius,
				FalloffDistance,
				Bus,
				bForceDry);
			// 엔진은 소리에 주인 액터가 있을 때만 그 액터를 오클루전 트레이스에서
			// 뺀다. 월드를 넘기면 주인이 없다. 주인을 거는 것은 부른 쪽이 고른 때뿐이다.
			const UObject* SpawnContext = OcclusionOwner
				? static_cast<const UObject*>(OcclusionOwner)
				: static_cast<const UObject*>(World);
			UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(
				SpawnContext,
				Sound,
				Location,
				FRotator::ZeroRotator,
				VolumeMultiplier,
				PitchMultiplier,
				0.0f,
				Attenuation);
			if (Component)
			{
				Component->SetUISound(bPlayWhenPaused);
				FRecentSound& Slot = RecentSounds[NextRecentSound];
				NextRecentSound = (NextRecentSound + 1) % UE_ARRAY_COUNT(RecentSounds);
				Slot.Component = Component;
				Slot.World = World;
				Slot.Location = Location;
				Slot.StartedAt = World->GetTimeSeconds();
			}
			// 상시 소리는 처음부터 상시로 건다. 일반 소리로 먼저 들어오면 상한을
			// 세는 그 순간에 대본 소리 하나를 밀어낸다.
			if (AudioDirector && bPersistent)
			{
				AudioDirector->RegisterPersistentBed(Component, Bus);
			}
			else if (AudioDirector && bExpendable)
			{
				AudioDirector->RegisterExpendable(Component, Bus);
			}
			else if (AudioDirector)
			{
				AudioDirector->RegisterComponent(Component, Bus);
			}
			return Component;
		}
	}

	UAudioComponent* FindRecentSoundNear(
		const UObject* WorldContext,
		const FVector& Location,
		const float Radius,
		const float MaxAgeSeconds,
		float* OutAgeSeconds)
	{
		const UWorld* World = GEngine && WorldContext
			? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
			: nullptr;
		if (!World)
		{
			return nullptr;
		}
		const double Now = World->GetTimeSeconds();
		UAudioComponent* Best = nullptr;
		double BestStartedAt = -1.0;
		for (const FRecentSound& Slot : RecentSounds)
		{
			UAudioComponent* Component = Slot.Component.Get();
			if (!Component || Slot.World.Get() != World || !Component->IsPlaying()
				|| Now - Slot.StartedAt > MaxAgeSeconds || Slot.StartedAt <= BestStartedAt
				|| FVector::DistSquared(Slot.Location, Location) > FMath::Square(Radius))
			{
				continue;
			}
			Best = Component;
			BestStartedAt = Slot.StartedAt;
		}
		if (OutAgeSeconds)
		{
			*OutAgeSeconds = Best ? static_cast<float>(Now - BestStartedAt) : 0.0f;
		}
		return Best;
	}

	void SetOutputMode(const EIGOutputMode Mode)
	{
		ActiveOutputMode = Mode;
	}

	EIGOutputMode GetOutputMode()
	{
		return ActiveOutputMode;
	}

	ESoundSpatializationAlgorithm GetSpatializationAlgorithm()
	{
		return ActiveOutputMode == EIGOutputMode::Headphones
			? SPATIALIZATION_HRTF
			: SPATIALIZATION_Default;
	}

	UAudioComponent* SpawnHumLoopAt(
		AActor* Owner,
		const FName ComponentName,
		const FVector& Location,
		const float MaskingRadius)
	{
		UWorld* World = Owner ? Owner->GetWorld() : nullptr;
		if (!World || MaskingRadius <= HumBodyRadius)
		{
			return nullptr;
		}
		UAudioComponent* Component =
			NewObject<UAudioComponent>(Owner, ComponentName);
		Component->RegisterComponent();
		Component->SetWorldLocation(Location);
		Component->SetSound(
			UIGToneSequenceSoundWave::CreateMachineHumLoop(Owner));
		Component->AttenuationSettings = MakeAttenuation(
			Owner,
			HumBodyRadius,
			MaskingRadius - HumBodyRadius,
			EIGAudioBus::World);
		Component->bAllowSpatialization = true;
		Component->SetVolumeMultiplier(HumVolumeMultiplier);
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->RegisterPersistentBed(Component, EIGAudioBus::World);
		}
		Component->Play();
		return Component;
	}

	USoundAttenuation* MakeAttenuation(
		UObject* Outer,
		const float InnerRadius,
		const float FalloffDistance,
		const EIGAudioBus Bus,
		const bool bForceDry)
	{
		UObject* SafeOuter = Outer ? Outer : GetTransientPackage();
		USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(SafeOuter);
		FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
		Settings.bAttenuate = true;
		Settings.bSpatialize = true;
		// Resonance Audio is selected project-wide. Plugin spatialization gives
		// the knock/pipe/entity contract its required binaural elevation cues;
		// platforms without the plugin still fall back to UE's normal panning.
		// 스피커를 고르면 평범한 패닝으로 내린다 — 바이노럴을 스피커로 틀면
		// 좌우가 서로 새어 위아래가 오히려 뭉개진다.
		Settings.SpatializationAlgorithm = GetSpatializationAlgorithm();
		Settings.AttenuationShapeExtents = FVector(FMath::Max(1.0f, InnerRadius), 0.0f, 0.0f);
		Settings.FalloffDistance = FMath::Max(1.0f, FalloffDistance);
		Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Settings.dBAttenuationAtMax = -60.0f;
		// 벽 너머의 소리는 먹먹하고 작다. 엔진 오클루전은 청자와 음원 사이에
		// 트레이스 하나를 긋는다. 예전엔 두 층 위 콘크리트 너머의 노크가 같은 방의
		// 노크와 같은 스펙트럼으로 왔다 — 거리는 볼륨과 리버브가 말했지만 벽은
		// 아무도 말하지 않았다.
		Settings.bEnableOcclusion = true;
		Settings.OcclusionTraceChannel = ECC_Visibility;
		Settings.OcclusionLowPassFilterFrequency = 900.0f;
		Settings.OcclusionVolumeAttenuation = 0.55f;
		Settings.OcclusionInterpolationTime = 0.18f;
		// 공기가 고역을 먹는다. 멀수록 둔해진다.
		Settings.bAttenuateWithLPF = true;
		Settings.LPFRadiusMin = FMath::Max(1.0f, InnerRadius);
		Settings.LPFRadiusMax = Settings.FalloffDistance;
		Settings.LPFFrequencyAtMin = 20000.0f;
		Settings.LPFFrequencyAtMax = 2600.0f;
		// §10.4: volume tells the player a sound is far. Reverb tells them it is
		// far *through the building* — and the moment it goes dry, it is in the
		// room with them.
		if (bForceDry)
		{
			Settings.bEnableReverbSend = false;
		}
		else
		{
			UIGMissingFloorAudioSubsystem::ConfigureReverbSend(
				Settings,
				Bus,
				InnerRadius,
				Settings.FalloffDistance);
		}
		return Attenuation;
	}

	UAudioComponent* SpawnOneShotAt(
		const UObject* WorldContext,
		USoundBase* Sound,
		const FVector& Location,
		const float VolumeMultiplier,
		const float PitchMultiplier,
		const float InnerRadius,
		const float FalloffDistance,
		const EIGAudioBus Bus,
		const bool bPlayWhenPaused)
	{
		return SpawnOneShotInternal(
			WorldContext,
			Sound,
			Location,
			VolumeMultiplier,
			PitchMultiplier,
			InnerRadius,
			FalloffDistance,
			Bus,
			bPlayWhenPaused,
			/*bForceDry=*/false);
	}

	UAudioComponent* SpawnOneShotFromActorAt(
		const AActor* Source,
		USoundBase* Sound,
		const FVector& Location,
		const float VolumeMultiplier,
		const float PitchMultiplier,
		const float InnerRadius,
		const float FalloffDistance,
		const EIGAudioBus Bus)
	{
		return SpawnOneShotInternal(
			Source,
			Sound,
			Location,
			VolumeMultiplier,
			PitchMultiplier,
			InnerRadius,
			FalloffDistance,
			Bus,
			/*bPlayWhenPaused=*/false,
			/*bForceDry=*/false,
			/*bExpendable=*/false,
			/*OcclusionOwner=*/Source);
	}

	UAudioComponent* SpawnExpendableOneShotAt(
		const UObject* WorldContext,
		USoundBase* Sound,
		const FVector& Location,
		const float VolumeMultiplier,
		const float PitchMultiplier,
		const float InnerRadius,
		const float FalloffDistance,
		const EIGAudioBus Bus)
	{
		return SpawnOneShotInternal(
			WorldContext,
			Sound,
			Location,
			VolumeMultiplier,
			PitchMultiplier,
			InnerRadius,
			FalloffDistance,
			Bus,
			/*bPlayWhenPaused=*/false,
			/*bForceDry=*/false,
			/*bExpendable=*/true);
	}

	UAudioComponent* SpawnPersistentOneShotAt(
		const UObject* WorldContext,
		USoundBase* Sound,
		const FVector& Location,
		const float VolumeMultiplier,
		const float PitchMultiplier,
		const float InnerRadius,
		const float FalloffDistance,
		const EIGAudioBus Bus)
	{
		return SpawnOneShotInternal(
			WorldContext,
			Sound,
			Location,
			VolumeMultiplier,
			PitchMultiplier,
			InnerRadius,
			FalloffDistance,
			Bus,
			/*bPlayWhenPaused=*/false,
			/*bForceDry=*/false,
			/*bExpendable=*/false,
			/*OcclusionOwner=*/nullptr,
			/*bPersistent=*/true);
	}

	void FadeOutAfter(
		UAudioComponent* Component,
		const float DelaySeconds,
		const float FadeSeconds)
	{
		UWorld* World = Component ? Component->GetWorld() : nullptr;
		if (!World)
		{
			return;
		}
		// 원샷은 끝나면 스스로 지워진다. 그 컴포넌트에 묶은 약한 람다라서 먼저
		// 끝났으면 부르지 않고, 핸들을 붙들고 정리할 주인도 필요 없다.
		const TWeakObjectPtr<UAudioComponent> WeakComponent(Component);
		const float Fade = FMath::Max(0.01f, FadeSeconds);
		FTimerHandle TrimHandle;
		World->GetTimerManager().SetTimer(
			TrimHandle,
			FTimerDelegate::CreateWeakLambda(Component, [WeakComponent, Fade]()
			{
				UAudioComponent* Trimmed = WeakComponent.Get();
				if (Trimmed && Trimmed->IsPlaying())
				{
					Trimmed->FadeOut(Fade, 0.0f);
				}
			}),
			FMath::Max(0.01f, DelaySeconds),
			false);
	}

	UAudioComponent* SpawnDryOneShotAt(
		const UObject* WorldContext,
		USoundBase* Sound,
		const FVector& Location,
		const float VolumeMultiplier,
		const float PitchMultiplier,
		const float InnerRadius,
		const float FalloffDistance,
		const EIGAudioBus Bus)
	{
		return SpawnOneShotInternal(
			WorldContext,
			Sound,
			Location,
			VolumeMultiplier,
			PitchMultiplier,
			InnerRadius,
			FalloffDistance,
			Bus,
			/*bPlayWhenPaused=*/false,
			/*bForceDry=*/true);
	}
}
