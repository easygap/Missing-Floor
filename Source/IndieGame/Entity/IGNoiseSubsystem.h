#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IGNoiseSubsystem.generated.h"

/**
 * One reported sound, after masking. Radius is how far the sound carries;
 * whether something reacts is the listener's business, not the reporter's.
 */
USTRUCT(BlueprintType)
struct FIGNoiseEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Noise")
	FVector Location = FVector::ZeroVector;

	/** Post-masking loudness, 0..1. Zero-loudness events are never broadcast. */
	UPROPERTY(BlueprintReadOnly, Category = "Noise")
	float Loudness = 0.0f;

	/** Carry distance in centimeters, derived from post-masking loudness. */
	UPROPERTY(BlueprintReadOnly, Category = "Noise")
	float Radius = 0.0f;

	/** Who made the sound. Weak: reporters outlive nothing on its account. */
	UPROPERTY()
	TWeakObjectPtr<AActor> Instigator;

	/** 소리가 난 게임 시각(일시정지 중에는 멈춘다). 최근성 판정에 쓴다. */
	UPROPERTY(BlueprintReadOnly, Category = "Noise")
	double TimeSeconds = 0.0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FIGNoiseReportedSignature, const FIGNoiseEvent&);

/**
 * The building's ear. Every deliberate or accidental player sound — footsteps,
 * doors, drawers, dropped props, valves, hammer blows, an audible heartbeat —
 * is reported here as a loudness in [0..1]; the subsystem applies masking and
 * broadcasts the surviving event to whatever is listening (the one upstairs).
 *
 * Masking has two layers:
 *  - hum sources: registered machine beds (fridge, boiler, breaker panel)
 *    that swallow quiet sounds made close to them;
 *  - a global window the entity raises while its own knocking fills the
 *    building — the player's safe beat to move on.
 *
 * The subsystem never decides who heard what. It only says what sounded.
 */
UCLASS()
class INDIEGAME_API UIGNoiseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Reports one sound. Loudness is the pre-masking value from the design
	 * table (walk 0.15, door 0.35, hammer 1.0 ...). Returns the event that
	 * was broadcast, or a zero-loudness event when masking swallowed it.
	 */
	FIGNoiseEvent ReportNoise(
		const FVector& Location,
		float Loudness,
		AActor* Instigator = nullptr);

	/**
	 * 대본 사건이 낸 소리를 마스킹 없이 알린다. 험이 삼키는 것은 조용한 소리이고,
	 * 무너지는 자재처럼 반드시 그에게 닿아야 하는 비트가 밸브를 열어 둔 자리에서
	 * 조용히 사라지면 안 된다. 그녀의 습관이 아니므로 §5.6 열지도도 데우지 않는다.
	 */
	FIGNoiseEvent ReportNoiseUnmasked(
		const FVector& Location,
		float Loudness,
		AActor* Instigator = nullptr);

	/**
	 * Registers a machine hum that masks nearby sounds. Returns a handle for
	 * unregistration; the source is a fixed point (machines do not walk).
	 */
	int32 RegisterHumSource(const FVector& Location, float Radius, float Masking);
	void UnregisterHumSource(int32 Handle);

	/**
	 * Building-wide masking while the entity's own knocks (or the finale's
	 * open taps) cover everything. Owned by whoever is making the loud bed.
	 */
	void SetGlobalMasking(float Masking);
	float GetGlobalMasking() const { return GlobalMasking; }

	/** Total masking applied to a sound made at Location, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Noise")
	float GetMaskingAt(const FVector& Location) const;

	/**
	 * 장롱이나 침대 밑처럼 소리가 덜 새는 자리에 들어간 사람. 그가 낸 소리는
	 * Scale배로 줄어 나간다(숨는 자리 0.55, §4). 1 이상이거나 nullptr이면 푼다.
	 * 한 번에 한 사람만 숨으므로 한 자리만 기억한다.
	 */
	void SetInstigatorMuffle(const AActor* Instigator, float Scale);
	float GetInstigatorMuffle(const AActor* Instigator) const;

	/** Code listeners (the entity, telemetry, the ripple HUD). */
	FIGNoiseReportedSignature OnNoiseReported;

	/**
	 * §19.8 대체 채널만 듣는 신호. 존재가 낸 사건성 소리(노크, 대답, 추격 진입,
	 * 들숨, 가까이 기는 걸음)를 노크 진동과 노크 파문에 알린다. 마스킹도 히트맵도
	 * 거치지 않고, 존재 자신과 녹음은 이 신호를 듣지 않는다. 세기는 보내는 쪽이
	 * 플레이어 자리에 닿는 만큼으로 줄여서 준다.
	 */
	FIGNoiseReportedSignature OnPresentationCue;
	void BroadcastPresentationCue(
		const FVector& Location,
		float Loudness,
		AActor* Instigator);

	/** How far a full-loudness (1.0) sound carries, in centimeters. */
	static constexpr float CarryPerLoudness = 2600.0f;

	// -- §5.6 적응 청각 -----------------------------------------------------
	//
	// 존재는 소음의 누적 히트맵을 가진다. 플레이어가 자주 소리 낸 구역일수록
	// 순찰 경유 확률이 오르고, 티어3에서는 가장 뜨거운 구역에 미리 가서
	// 두드리지 않고 기다린다. 구현은 생성형이 아니라 순수 통계다 — 구역별
	// 카운트와 감쇠뿐이므로 재현 가능하고 검증 가능하다.

	/** 이 지점의 누적 열, 0~1로 정규화된 값. */
	UFUNCTION(BlueprintPure, Category = "Noise")
	float GetHeatAt(const FVector& Location) const;

	/**
	 * 가장 뜨거운 구역의 중심을 돌려준다. 동률이면 먼저 뜨거워진 구역이
	 * 이기므로 같은 플레이는 같은 매복 지점을 만든다.
	 */
	bool GetHottestZone(FVector& OutCenter, float& OutHeat) const;

	/** 밤이 끝나면 절반으로. 어제의 습관이 오늘 완전히 사라지지는 않는다. */
	void DecayHeatmapForNewNight();

	/** 회차 시작과 포획 리셋이 쓰는 완전 초기화. */
	void ResetHeatmap();

	int32 GetHeatZoneCount() const { return HeatZones.Num(); }

	/** 구역 한 변의 길이. 복도 한 구간에 대응하는 굵기다. */
	static constexpr float HeatZoneSize = 400.0f;

	/** 한 구역이 포화되는 누적 소음량. 이보다 뜨거워지지는 않는다. */
	static constexpr float HeatSaturation = 6.0f;

	/** 밤 단위 감쇠 계수. */
	static constexpr float HeatNightDecay = 0.5f;

private:
	/** 좌표를 구역 키로 접는다. 결정적이며 해시 순서에 의존하지 않는다. */
	static FIntVector ToHeatZoneKey(const FVector& Location);
	static FVector FromHeatZoneKey(const FIntVector& Key);
	void AccumulateHeat(const FVector& Location, float Loudness);

	struct FIGHumSource
	{
		int32 Handle = 0;
		FVector Location = FVector::ZeroVector;
		float Radius = 0.0f;
		float Masking = 0.0f;
	};

	struct FIGHeatZone
	{
		FIntVector Key = FIntVector::ZeroValue;
		float Heat = 0.0f;
		/** 먼저 뜨거워진 구역이 동률에서 이기도록 하는 순번. */
		int32 Serial = 0;
	};

	TArray<FIGHumSource> HumSources;
	/** 배열이라 순회 순서가 고정된다. TMap 해시 순서는 결정적이지 않다. */
	TArray<FIGHeatZone> HeatZones;
	int32 NextHumHandle = 1;
	int32 NextHeatSerial = 1;
	float GlobalMasking = 0.0f;
	TWeakObjectPtr<const AActor> MuffledInstigator;
	float InstigatorMuffleScale = 1.0f;
};
