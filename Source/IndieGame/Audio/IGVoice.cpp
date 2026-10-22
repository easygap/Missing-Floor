#include "Audio/IGVoice.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	const TCHAR* const VoiceFolder = TEXT("/Game/Audio/Voice/ko");

	/** 사람 목소리가 또렷한 거리와 사라지는 거리. 복도 한 칸 너머까지는 말이 들린다. */
	constexpr float PersonInnerRadius = 120.0f;
	constexpr float PersonFalloffDistance = 1800.0f;
	/** 문 너머. 문짝 바로 앞에서야 말이 되고, 문짝이 고역을 먹는다. */
	constexpr float DoorInnerRadius = 60.0f;
	constexpr float DoorFalloffDistance = 900.0f;
	constexpr float DoorLowPassHz = 2200.0f;
	/**
	 * 다른 층. 계단으로 올라오는 말이라 거리로 줄이지 않는다. 크기와 먹먹함은 층을 가로지르는
	 * 오클루전이 맡는다(같은 자리의 웅얼거림이 쓰던 거리다).
	 */
	constexpr float FloorsInnerRadius = 1100.0f;
	constexpr float FloorsFalloffDistance = 2600.0f;
	/** 녹음기·휴대폰. 작은 스피커라 저역과 고역이 같이 빠진다. */
	constexpr float DeviceInnerRadius = 40.0f;
	constexpr float DeviceFalloffDistance = 700.0f;
	constexpr float DeviceLowPassHz = 4200.0f;
	constexpr float DeviceHighPassHz = 320.0f;

	TMap<FName, TWeakObjectPtr<USoundBase>>& FoundVoices()
	{
		static TMap<FName, TWeakObjectPtr<USoundBase>> Voices;
		return Voices;
	}

	TSet<FName>& MissingVoices()
	{
		static TSet<FName> Missing;
		return Missing;
	}

	TMap<FName, TStrongObjectPtr<USoundBase>>& TestVoices()
	{
		static TMap<FName, TStrongObjectPtr<USoundBase>> Voices;
		return Voices;
	}

	FString VoiceAssetName(const FName LineId)
	{
		return FString::Printf(TEXT("VO_%s"), *LineId.ToString());
	}
}

FName IGVoice::LineIdOf(const FText& Line)
{
	const TOptional<FString> Key = FTextInspector::GetKey(Line);
	return Key.IsSet() && !Key->IsEmpty() ? FName(*Key.GetValue()) : NAME_None;
}

bool IGVoice::IsYudam(const FText& Speaker)
{
	// 화자 이름표의 키는 어느 언어로 켜도 같다. 유담의 이름표는 이 둘이다.
	const FName Key = LineIdOf(Speaker);
	return Key == FName(TEXT("YudamCounterSpeaker")) || Key == FName(TEXT("YudamName"));
}

USoundBase* IGVoice::Find(const FName LineId)
{
	if (LineId.IsNone())
	{
		return nullptr;
	}
	if (const TStrongObjectPtr<USoundBase>* Test = TestVoices().Find(LineId))
	{
		return Test->Get();
	}
	if (const TWeakObjectPtr<USoundBase>* Found = FoundVoices().Find(LineId))
	{
		if (USoundBase* Sound = Found->Get())
		{
			return Sound;
		}
	}
	if (MissingVoices().Contains(LineId))
	{
		return nullptr;
	}
	const FString Name = VoiceAssetName(LineId);
	const FString PackagePath = FString::Printf(TEXT("%s/%s"), VoiceFolder, *Name);
	// 녹음이 없는 줄이 대부분이다. 패키지가 있는지 먼저 보아 없는 자산을 찾는 경고를 남기지 않는다.
	if (!FPackageName::DoesPackageExist(PackagePath))
	{
		MissingVoices().Add(LineId);
		return nullptr;
	}
	USoundBase* Loaded = LoadObject<USoundBase>(
		nullptr, *FString::Printf(TEXT("%s.%s"), *PackagePath, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Loaded)
	{
		MissingVoices().Add(LineId);
		return nullptr;
	}
	FoundVoices().Add(LineId, Loaded);
	return Loaded;
}

UAudioComponent* IGVoice::Play(
	const UObject* WorldContext,
	USoundBase* Sound,
	const FVector& Where,
	const ESource Source)
{
	if (!WorldContext || !Sound)
	{
		return nullptr;
	}
	if (Source == ESource::Self)
	{
		UWorld* World = GEngine
			? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
			: nullptr;
		UIGMissingFloorAudioSubsystem* AudioDirector = World
			? World->GetSubsystem<UIGMissingFloorAudioSubsystem>()
			: nullptr;
		if (AudioDirector)
		{
			AudioDirector->PrepareSound(Sound, EIGAudioBus::Speech);
		}
		UAudioComponent* Component = UGameplayStatics::SpawnSound2D(
			World, Sound, 1.0f, 1.0f, 0.0f, nullptr, false, true);
		if (Component && AudioDirector)
		{
			AudioDirector->RegisterPersistentBed(Component, EIGAudioBus::Speech);
		}
		return Component;
	}

	float InnerRadius = PersonInnerRadius;
	float FalloffDistance = PersonFalloffDistance;
	EIGAudioBus Bus = EIGAudioBus::Speech;
	float LowPassHz = 0.0f;
	float HighPassHz = 0.0f;
	switch (Source)
	{
	case ESource::ThroughDoor:
		InnerRadius = DoorInnerRadius;
		FalloffDistance = DoorFalloffDistance;
		Bus = EIGAudioBus::Entity;
		LowPassHz = DoorLowPassHz;
		break;
	case ESource::PersonThroughDoor:
		// 문짝은 고역만 먹는다. 사람의 말은 문에 귀를 붙이지 않아도 들린다.
		LowPassHz = DoorLowPassHz;
		break;
	case ESource::PersonAcrossFloors:
		InnerRadius = FloorsInnerRadius;
		FalloffDistance = FloorsFalloffDistance;
		LowPassHz = DoorLowPassHz;
		break;
	case ESource::Device:
		InnerRadius = DeviceInnerRadius;
		FalloffDistance = DeviceFalloffDistance;
		LowPassHz = DeviceLowPassHz;
		HighPassHz = DeviceHighPassHz;
		break;
	default:
		break;
	}
	UAudioComponent* Component = IGAudio::SpawnPersistentOneShotAt(
		WorldContext, Sound, Where, 1.0f, 1.0f, InnerRadius, FalloffDistance, Bus);
	if (!Component)
	{
		return nullptr;
	}
	if (LowPassHz > 0.0f)
	{
		Component->SetLowPassFilterEnabled(true);
		Component->SetLowPassFilterFrequency(LowPassHz);
	}
	if (HighPassHz > 0.0f)
	{
		Component->SetHighPassFilterEnabled(true);
		Component->SetHighPassFilterFrequency(HighPassHz);
	}
	return Component;
}

float IGVoice::GetSeconds(const FText& Line)
{
	const USoundBase* Sound = Find(LineIdOf(Line));
	const float Seconds = Sound ? Sound->GetDuration() : 0.0f;
	return Seconds > 0.0f && Seconds < INDEFINITELY_LOOPING_DURATION ? Seconds : 0.0f;
}

void IGVoice::Say(
	const UObject* WorldContext,
	const FText& Speaker,
	const FText& Line,
	FIGDialogueScope Scope,
	TFunction<FVector()> Where,
	const ESource Source,
	const EIGDialogueChannel Channel,
	const float MinimumDurationSeconds,
	const EIGDialoguePriority Priority)
{
	USoundBase* Sound = Find(LineIdOf(Line));
	if (!Sound)
	{
		AIGHorrorHUD::PushDialogueScoped(
			WorldContext, Speaker, Line, Channel, MinimumDurationSeconds, Priority, MoveTemp(Scope));
		return;
	}
	if (!Where)
	{
		// 자리를 주지 않았으면 말을 낸 액터에서 난다.
		const TWeakObjectPtr<const AActor> WeakActor(Cast<AActor>(WorldContext));
		Where = [WeakActor]()
		{
			return WeakActor.IsValid() ? WeakActor->GetActorLocation() : FVector::ZeroVector;
		};
	}
	const TWeakObjectPtr<const UObject> WeakContext(WorldContext);
	const TStrongObjectPtr<USoundBase> Held(Sound);
	Scope.StartVoice = [WeakContext, Held, Where = MoveTemp(Where), Source]() -> UAudioComponent*
	{
		const UObject* Context = WeakContext.Get();
		return Context ? Play(Context, Held.Get(), Where(), Source) : nullptr;
	};
	AIGHorrorHUD::PushDialogueScoped(
		WorldContext, Speaker, Line, Channel, MinimumDurationSeconds, Priority, MoveTemp(Scope));
}

void IGVoice::SetSoundForTesting(const FName LineId, USoundBase* Sound)
{
	if (LineId.IsNone())
	{
		return;
	}
	if (Sound)
	{
		TestVoices().Add(LineId, TStrongObjectPtr<USoundBase>(Sound));
	}
	else
	{
		TestVoices().Remove(LineId);
	}
}
