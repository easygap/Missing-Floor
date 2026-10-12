#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IGStressComponent.generated.h"

class UAudioComponent;
class UCameraComponent;
class UPostProcessComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FIGStressChangedSignature,
	float, Stress);

/**
 * The player's fear state, in one number from 0 (calm) to 1 (panicking).
 *
 * Stress rises from three sources — standing in the dark, being near
 * something that is hunting you, and scripted scares — and bleeds off slowly
 * when none of those apply. It is deliberately slow to fall: the point is
 * that the corridor stays frightening for a while after the light comes back.
 *
 * What it drives:
 *  - heartbeat rate and volume (procedural, no audio assets)
 *  - breathing rate, which the character's camera sway reads
 *  - the sound of that breathing: a looped scared breath that rises with
 *    fear and with exertion, a gasp on a scare, a shaky exhale when a chase
 *    lets go — §21.1 puts 「호흡」 on the player bus and this is where it is
 *  - a post-process ramp: vignette closes in, colour drains, the lens
 *    aberrates slightly at the edges
 *  - a fine camera tremor at high stress
 *
 * Nothing here kills the player. Fear is a lens, not a health bar.
 */
UCLASS(ClassGroup = (IndieGame), meta = (BlueprintSpawnableComponent))
class INDIEGAME_API UIGStressComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIGStressComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaSeconds,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Current fear, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Stress")
	float GetStress() const { return Stress; }

	/** An instant jolt: a slam, a figure appearing, a light dying. */
	UFUNCTION(BlueprintCallable, Category = "Stress")
	void ApplyScare(float Amount);

	/**
	 * Sustained pressure this frame, 0..1, from whatever is hunting the
	 * player. The chase director sets this every tick; it decays on its own
	 * if nothing does, so a dead pursuer cannot leave the player pinned.
	 */
	UFUNCTION(BlueprintCallable, Category = "Stress")
	void SetThreatPressure(float Pressure);

	/** Told by the player pawn how dark it is where they stand, 0..1. */
	UFUNCTION(BlueprintCallable, Category = "Stress")
	void SetDarkness(float InDarkness);

	/**
	 * Silences the bodily pulse for an authored reveal window. Any pulse already
	 * playing is stopped; optionally one pulse resolves the silence at its end.
	 */
	void SuppressHeartbeat(float DurationSeconds, bool bPlayOneBeatOnRelease);

	/**
	 * §5.2 숨 참기. 참는 동안 심박은 소리도 소음도 아니다. 놓으면 1.5배 반동이
	 * 몇 초 온다 — 스트레스 0.85 위에서는 그 반동이 그대로 새는 소음이다.
	 */
	void BeginBreathHold(float MaximumSeconds);
	void EndBreathHold(bool bRebound);
	bool IsBreathHeld() const { return bBreathHeld; }

	/** Current breaths per minute, for the pawn's breath sway. */
	UFUNCTION(BlueprintPure, Category = "Stress")
	float GetBreathsPerMinute() const;

	/**
	 * 숨이 찬 정도 0~1. 폰이 달리는 동안 매 프레임 넣고, 넣는 값이 0으로
	 * 떨어져도 여기서 6초에 걸쳐 스스로 가라앉는다 — 멈춰 섰다고 바로 숨이
	 * 고르지는 않는다. 스트레스와 따로 숨소리를 올린다.
	 */
	void SetExertion(float Exertion01);

	/**
	 * 놀라서 들이켜는 숨 한 번. 0.4 넘는 놀람이 스스로 부르고, 참았던 숨을
	 * 놓을 때도 온다. 7초에 한 번 — 놀람마다 헐떡이면 몸이 아니라 효과음이다.
	 */
	void PlayGasp(bool bIgnoreCooldown = false);

	/** 추격이 끝났거나 아침이 왔을 때 떨리며 내쉬는 숨. 10초에 한 번. 겁먹은 몸만 쉰다. */
	void PlayReliefExhale();

	/**
	 * 공포가 무너지는 자리(§20.1 밤3 P4의 0.90 → 0.30). 스트레스를 Ceiling까지
	 * Seconds에 걸쳐 내리고, 쌓여 있던 놀람과 위협 압박을 비운다. 한 번에
	 * 떨어뜨리면 숨 루프와 비네트가 튄다. 이미 Ceiling 아래면 아무것도 안 한다.
	 */
	void ApplyRelief(float Ceiling, float Seconds);

	UPROPERTY(BlueprintAssignable, Category = "Stress|Events")
	FIGStressChangedSignature OnStressChanged;

protected:
	/**
	 * Stress gained per second while standing in full darkness.
	 *
	 * This is per SECOND, which is easy to misread. At the original 0.055 the
	 * meter reached half in nine seconds and pegged in eighteen — fine for a
	 * corridor you cross once, useless for a chapter that spends minutes in
	 * the dark, because a saturated meter stops meaning anything. At 0.012
	 * full darkness takes ~42 s to reach half and ~83 s to saturate, so being
	 * in the dark sets a floor and the scares still have somewhere to go.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "0.0"))
	float DarknessRate = 0.012f;

	/** Stress gained per second at full threat pressure. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "0.0"))
	float ThreatRate = 0.42f;

	/** Stress lost per second when nothing is applying pressure. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "0.0"))
	float RecoveryRate = 0.075f;

	/** Resting and panicked heart rates, in beats per minute. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "30.0"))
	float RestingBPM = 62.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stress", meta = (ClampMin = "60.0"))
	float PanicBPM = 148.0f;

private:
	friend class AIGAudioPresentationProbe;
	friend class AIGGameplayRealismProbe;
	bool IsBreathPresentationSuppressed() const;
	/** Enables frame updates only while fear state is changing or audible. */
	void RefreshTickState();
	void UpdateStress(float DeltaSeconds);
	void UpdateHeartbeat(float DeltaSeconds);
	void PlayHeartbeat(float EffectiveStress);
	/** 숨의 루프. 스트레스·숨참·반동으로 볼륨, 숨을 참는 동안 0. */
	void UpdateBreathLayer(float DeltaSeconds);
	/** 2D 숨 한 번. CutSeconds가 양수면 그 뒤로 잘라 낸다 — 녹음이 길 때. */
	void PlayBreathOneShot(USoundBase* Sound, float Volume, float CutSeconds);
	/** §19.8. 심박 경고가 켜져 있을 때 비네트가 부푸는 배율. */
	float GetHeartbeatWarningScale() const;

	/** §18.3. 접근성의 멀미 완화 비네트 세기. 없으면 0이다. */
	float GetComfortVignetteStrength() const;
	void UpdatePostProcess();

	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> FearPostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> HeartbeatComponent;

	/** 그녀의 숨. 2D, PLAYER 버스, 상시 베드. 배수는 1이고 페이더가 볼륨이다. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BreathComponent;
	/** 들이켜거나 내쉬는 숨은 한 번에 하나만 낸다. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BreathOneShotComponent;
	/** 폰이 넣은 숨찬 정도와, 거기서 스스로 가라앉는 값. */
	float ExertionReported = 0.0f;
	float Exertion = 0.0f;
	float BreathLevelTarget = 0.0f;
	float BreathPitchTarget = 1.0f;
	double LastGaspSeconds = -1000.0;
	double LastReliefSeconds = -1000.0;

	float Stress = 0.0f;
	float Darkness = 0.0f;
	float ThreatPressure = 0.0f;
	/** Scare jolts decay separately so a jolt reads as a spike, not a plateau. */
	float ScareCharge = 0.0f;
	float BeatPhase = 0.0f;
	float HeartbeatSuppressionRemaining = 0.0f;
	/** ApplyRelief가 건 하강. 남은 시간 동안 초당 ReliefRate씩 ReliefCeiling까지. */
	float ReliefCeiling = 1.0f;
	float ReliefRate = 0.0f;
	float ReliefRemaining = 0.0f;
	bool bPlayHeartbeatOnSuppressionRelease = false;
	bool bBreathHeld = false;
	/** 숨을 놓은 뒤 반동이 남은 시간. 심박이 커지고 빨라진다. */
	float HeartbeatReboundRemaining = 0.0f;
	/** 지금 만드는 박동에 걸 반동 배율. UpdateHeartbeat이 정하고 PlayHeartbeat이 읽는다. */
	float HeartbeatReboundScaleNow = 1.0f;
};
