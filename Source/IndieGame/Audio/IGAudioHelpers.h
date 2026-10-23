#pragma once

#include "CoreMinimal.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Sound/SoundAttenuation.h"

class AActor;
class UAudioComponent;
class USoundAttenuation;
class USoundBase;

/** Small runtime audio utilities shared by prologue actors. */
namespace IGAudio
{
	/**
	 * 녹음 샘플 뱅크. /Game/Audio/S_<Name>. 없으면 nullptr를 돌려주고 부르는
	 * 쪽이 합성기로 내려간다 — 그래서 샘플이 하나도 없어도 게임은 예전 소리로
	 * 돈다. 원본은 Content/SourceArt/Audio(CC0), 반입은 Import-AudioSamples.ps1.
	 */
	INDIEGAME_API USoundBase* Sample(const TCHAR* Name);
	/** S_<Prefix>_<0..Count-1> 가운데 해시로 하나. 같은 걸음이 같은 소리를 내지 않게. */
	INDIEGAME_API USoundBase* SampleVariant(const TCHAR* Prefix, int32 Count, uint32 Hash);
	/** 샘플이 있으면 샘플, 없으면 합성기. 호출부의 한 줄이 그대로 남는다. */
	template <typename FactoryType>
	USoundBase* SampleOr(const TCHAR* Name, FactoryType&& Factory)
	{
		if (USoundBase* Found = Sample(Name))
		{
			return Found;
		}
		return Factory();
	}
	template <typename FactoryType>
	USoundBase* SampleVariantOr(const TCHAR* Prefix, int32 Count, uint32 Hash, FactoryType&& Factory)
	{
		if (USoundBase* Found = SampleVariant(Prefix, Count, Hash))
		{
			return Found;
		}
		return Factory();
	}

	/**
	 * §10.5. 이 게임의 훅은 「위에서 나는 소리」라서 기본이 바이노럴이다.
	 * 그런데 바이노럴을 스피커로 들으면 좌우가 서로 새어 상이 무너진다.
	 * 스피커도 막지 않기로 한 이상, 스피커로 듣는다고 말할 자리가 있어야
	 * 한다.
	 */
	enum class EIGOutputMode : uint8
	{
		Headphones,
		Speakers
	};

	INDIEGAME_API void SetOutputMode(EIGOutputMode Mode);
	INDIEGAME_API EIGOutputMode GetOutputMode();

	/** 지금 출력 방식에 맞는 정위 알고리즘. */
	INDIEGAME_API ESoundSpatializationAlgorithm GetSpatializationAlgorithm();

	/**
	 * Creates a transient natural-falloff attenuation object.
	 *
	 * The bus decides the §10.4 reverb-send curve: how wet this cue gets as it
	 * moves away from the listener. Every spatial cue in the building goes
	 * through here, so distance stays a single readable axis.
	 */
	INDIEGAME_API USoundAttenuation* MakeAttenuation(
		UObject* Outer,
		float InnerRadius,
		float FalloffDistance,
		EIGAudioBus Bus = EIGAudioBus::World,
		bool bForceDry = false);

	/**
	 * 험 존이 내는 소리를 그 자리에 건다.
	 *
	 * 마스킹 반경을 그대로 받아서 들리는 끝과 가려지는 끝을 한 값으로 맞춘다.
	 * 둘이 어긋나면 소리는 나는데 안 가려지는 띠가 생기고, 그 띠를 밟은
	 * 플레이어는 §5.1이 가르치려는 「기계 옆이 안전지대다」를 틀린 규칙으로
	 * 배운다.
	 */
	INDIEGAME_API UAudioComponent* SpawnHumLoopAt(
		AActor* Owner,
		FName ComponentName,
		const FVector& Location,
		float MaskingRadius);

	/**
	 * Fire-and-forget spatialized one-shot. Returns the auto-destroying
	 * component, or nullptr when the world or sound is unavailable.
	 */
	INDIEGAME_API UAudioComponent* SpawnOneShotAt(
		const UObject* WorldContext,
		USoundBase* Sound,
		const FVector& Location,
		float VolumeMultiplier = 1.0f,
		float PitchMultiplier = 1.0f,
		float InnerRadius = 160.0f,
		float FalloffDistance = 1400.0f,
		EIGAudioBus Bus = EIGAudioBus::World,
		bool bPlayWhenPaused = false);

	/**
	 * 그 자리 가까이에서 방금 낸 소리(이 도우미로 낸 원샷). 소리 자막이 설명하는 실제 소리를 찾아 묶을 때
	 * HUD가 쓴다. 지금 울고 있는 것 가운데 가장 최근 것을 돌려주고, 없으면 nullptr.
	 */
	INDIEGAME_API UAudioComponent* FindRecentSoundNear(
		const UObject* WorldContext,
		const FVector& Location,
		float Radius,
		float MaxAgeSeconds,
		float* OutAgeSeconds = nullptr);

	/**
	 * SpawnOneShotAt과 같되 소리를 낸 액터를 오클루전 트레이스에서 뺀다. 문의
	 * 걸쇠·경첩·닫힘은 문짝 두께 안에서 나서, 그냥 내면 제 문짝에 가려 먹먹해진다.
	 * 다른 벽과 바닥은 그대로 가린다. 엘리베이터처럼 제 몸이 가려야 맞는 소리에는
	 * 쓰지 않는다.
	 */
	INDIEGAME_API UAudioComponent* SpawnOneShotFromActorAt(
		const AActor* Source,
		USoundBase* Sound,
		const FVector& Location,
		float VolumeMultiplier = 1.0f,
		float PitchMultiplier = 1.0f,
		float InnerRadius = 160.0f,
		float FalloffDistance = 1400.0f,
		EIGAudioBus Bus = EIGAudioBus::World);

	/**
	 * SpawnOneShotAt과 같되 소모음으로 건다. 박자마다 쏟아지는 소리(기는 걸음)
	 * 전용이다. 상한에 닿으면 가장 먼저 밀리고, 스팅어·덮침·들숨 같은 대본 소리를
	 * 밀어내지 못한다 — 그의 소리가 그의 걸음에 눌리면 안 된다(§21.1).
	 */
	INDIEGAME_API UAudioComponent* SpawnExpendableOneShotAt(
		const UObject* WorldContext,
		USoundBase* Sound,
		const FVector& Location,
		float VolumeMultiplier = 1.0f,
		float PitchMultiplier = 1.0f,
		float InnerRadius = 160.0f,
		float FalloffDistance = 1400.0f,
		EIGAudioBus Bus = EIGAudioBus::World);

	/**
	 * SpawnOneShotAt과 같되 처음부터 상시 소리로 건다. 발음 상한을 세지 않으니
	 * 누구에게도 밀리지 않고 누구도 밀지 않는다. 스팅어처럼 한 번 울고 끝나지만
	 * 대본 소리와 자리를 다투면 안 되는 소리 전용이다. 끝나면 스스로 지워진다.
	 */
	INDIEGAME_API UAudioComponent* SpawnPersistentOneShotAt(
		const UObject* WorldContext,
		USoundBase* Sound,
		const FVector& Location,
		float VolumeMultiplier = 1.0f,
		float PitchMultiplier = 1.0f,
		float InnerRadius = 160.0f,
		float FalloffDistance = 1400.0f,
		EIGAudioBus Bus = EIGAudioBus::World);

	/**
	 * 녹음의 앞부분만 쓰고 뒤를 걷는다. 지정한 시간에 아직 울고 있으면 자르지
	 * 않고 페이드아웃한다(0으로 가는 페이드는 엔진에서 정지다). 원샷이 먼저
	 * 끝나 지워졌으면 아무 일도 하지 않는다.
	 */
	INDIEGAME_API void FadeOutAfter(
		UAudioComponent* Component,
		float DelaySeconds,
		float FadeSeconds);

	/**
	 * SpawnOneShotAt with the building reverb send switched off entirely.
	 *
	 * §21.3 reserves this for one cue: the two knocks at the moment of capture.
	 * The whole §10.4 grammar spends the game teaching the player to hear
	 * distance as wetness, so the single cue that arrives perfectly dry says
	 * **same space** more plainly than any volume could — he is holding you.
	 * Do not reach for this to make something merely clearer.
	 */
	INDIEGAME_API UAudioComponent* SpawnDryOneShotAt(
		const UObject* WorldContext,
		USoundBase* Sound,
		const FVector& Location,
		float VolumeMultiplier = 1.0f,
		float PitchMultiplier = 1.0f,
		float InnerRadius = 160.0f,
		float FalloffDistance = 1400.0f,
		EIGAudioBus Bus = EIGAudioBus::Entity);
}
