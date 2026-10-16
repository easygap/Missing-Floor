#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IGAccessibilitySubsystem.generated.h"

/** Settings that alter presentation and input without changing story truth. */
USTRUCT(BlueprintType)
struct INDIEGAME_API FIGAccessibilitySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bReducedCameraMotion = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bReducedFlicker = false;

	/**
	 * 1인칭 시야각. 좁은 화면에서 멀미가 나는 사람이 있고, 반대로 넓히면
	 * 복도가 실제보다 멀어 보여 압박이 준다. 어느 쪽이든 손에 맡긴다.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Accessibility",
		meta = (ClampMin = "68.0", ClampMax = "100.0"))
	float FieldOfViewDegrees = 78.0f;

	/**
	 * §18.3 멀미 완화 비네트. 시야각과 둘 중 하나를 고르는 것이 아니라
	 * 함께 쓴다 — 화각을 넓히면 주변부가 빨라져서 오히려 힘든 손도 있다.
	 * 기본은 0이다. 의도한 화면은 비네트가 없는 쪽이다.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Accessibility",
		meta = (ClampMin = "0.00", ClampMax = "1.00"))
	float ComfortVignetteStrength = 0.0f;

	/**
	 * 렌즈 왜곡. 화면 가장자리의 휘어짐과 색 번짐을 조절한다.
	 * 기본 1이 의도한 화면이고, 눈이 피로한 사람은 0까지 내릴 수 있다.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Accessibility",
		meta = (ClampMin = "0.00", ClampMax = "1.00"))
	float CameraTextureStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bDirectionalFearCues = false;

	/**
	 * §19.8 노크 진동 대체. 존재가 내는 소리를 패드 진동으로 옮긴다.
	 *
	 * §18.5는 벽에서 돌아오는 응답 노크에 진동을 붙이지 말라고 했고 그건
	 * 기본값으로 지켜진다. 이 설정은 그 규칙을 뒤집는 것이 아니라, 소리를
	 * 못 듣는 손에게 같은 정보를 다른 통로로 주는 것이다. 켠 사람만 받는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bKnockHapticSubstitute = false;

	/**
	 * §19.8 노크 시각 대체. 파문 링을 존재의 소리에도 그린다.
	 * 내 소리와는 **두께로** 구분한다 — 색만으로 나누면 색각에서 사라진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bKnockRippleSubstitute = false;

	/** §19.8 심박 경고. 스트레스가 임계에 닿으면 비네트가 맥동한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bHeartbeatWarning = false;

	/** §19.8 인지 지원. 노크 판정창을 넓힌다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bCognitiveAssist = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bSubtitlesEnabled = true;

	/** Important non-speech sounds can be configured separately from dialogue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bSoundCaptionsEnabled = true;

	/**
	 * 소리 자막을 켰을 때 건물과 동네의 평범한 소리까지 글로 띄운다. 꺼 두면(기본)
	 * 위협·대답·사건처럼 진행에 쓰이는 소리만 띄운다. 설정의 「소리 자막」 칸에서
	 * 끔 → 중요한 소리 → 모든 소리 순으로 고른다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bBackgroundSoundCaptions = false;

	/** Shared dialogue/caption scale; 2.0 preserves the 200% accessibility path. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Accessibility",
		meta = (ClampMin = "0.85", ClampMax = "2.00"))
	float CaptionSizeScale = 1.0f;

	/** Opacity of the high-contrast lower-third backing surface. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Accessibility",
		meta = (ClampMin = "0.00", ClampMax = "1.00"))
	float CaptionBackgroundOpacity = 0.82f;

	/**
	 * 자막이 화면에 머무는 시간의 배율. 읽는 속도는 사람마다 다르고,
	 * 이 게임은 소리를 글로 옮긴 줄이 정보의 전부인 구간이 있다.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Accessibility",
		meta = (ClampMin = "0.75", ClampMax = "2.00"))
	float CaptionDurationScale = 1.0f;

	/** Width/height fraction reserved as the caption-safe display rectangle. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Accessibility",
		meta = (ClampMin = "0.80", ClampMax = "1.00"))
	float CaptionSafeAreaScale = 0.90f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bToggleHoldInteractions = false;

	/** Crouch is a tap toggle by default; disable for press-to-crouch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bToggleCrouch = true;

	/** Master gate for controller vibration, including audio replacement cues. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bHapticsEnabled = true;

	/**
	 * Optional real microphone-to-noise bridge. OFF is the balance baseline;
	 * enabling it never records, stores or transmits captured samples.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bMicrophoneNoiseEnabled = false;

	/**
	 * 화면 가운데 점. 꺼 두면(기본) 조사할 대상을 겨눴을 때만 뜬다. 멀미가 나는
	 * 사람에게는 늘 떠 있는 점 하나가 눈의 기준이 되어 준다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bAlwaysShowCenterDot = false;

	/**
	 * 조사 안내에 키 이름을 늘 붙인다. 꺼 두면(기본) 같은 동작을 세 번 해 본
	 * 뒤로는 「[ E ]  문 열기」가 「문 열기」로 줄어든다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bAlwaysShowPromptKeys = false;

	/**
	 * 힌트 키(H). 켜 두면 누를 때마다 지금 볼 곳을 한 단계씩 더 구체적으로
	 * 떠올린다. 끄면 키도, 막혔을 때 한 번 뜨는 힌트 키 안내도 없다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bHintsEnabled = true;

	/** Multiplier applied only to non-zero hold interactions. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Accessibility",
		meta = (ClampMin = "0.25", ClampMax = "1.0"))
	float HoldDurationScale = 1.0f;
};

/**
 * Process-wide accessibility state backed by GameUserSettings.ini.
 *
 * Command-line overrides are deliberately effective-session only so QA runs
 * cannot silently change the player's persisted preferences.
 */
UCLASS()
class INDIEGAME_API UIGAccessibilitySubsystem final
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintPure, Category = "Accessibility")
	FIGAccessibilitySettings GetSettings() const { return EffectiveSettings; }

	UFUNCTION(BlueprintCallable, Category = "Accessibility")
	void ApplySettings(const FIGAccessibilitySettings& NewSettings);

	UFUNCTION(BlueprintCallable, Category = "Accessibility")
	void ResetToDefaults();

	UFUNCTION(BlueprintPure, Category = "Accessibility|Motion")
	bool IsReducedCameraMotionEnabled() const
	{
		return EffectiveSettings.bReducedCameraMotion;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Motion")
	bool IsReducedFlickerEnabled() const
	{
		return EffectiveSettings.bReducedFlicker;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Audio")
	bool UsesDirectionalFearCues() const
	{
		return EffectiveSettings.bDirectionalFearCues;
	}

	/** 검사용. 저장하지 않고 지금 판의 음성 대사 자막만 켜고 끈다. */
	void OverrideSubtitlesForTesting(const bool bEnabled) { EffectiveSettings.bSubtitlesEnabled = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "Accessibility|Subtitles")
	bool AreSubtitlesEnabled() const
	{
		return EffectiveSettings.bSubtitlesEnabled;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Subtitles")
	bool AreSoundCaptionsEnabled() const
	{
		return EffectiveSettings.bSoundCaptionsEnabled;
	}

	/** 소리 자막이 켜져 있고 「모든 소리」까지 고른 경우. */
	UFUNCTION(BlueprintPure, Category = "Accessibility|Subtitles")
	bool AreBackgroundSoundCaptionsEnabled() const
	{
		return EffectiveSettings.bSoundCaptionsEnabled && EffectiveSettings.bBackgroundSoundCaptions;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Subtitles")
	float GetCaptionSizeScale() const
	{
		return EffectiveSettings.CaptionSizeScale;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Captions")
	float GetCaptionDurationScale() const
	{
		return EffectiveSettings.CaptionDurationScale;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Audio")
	bool UsesKnockHapticSubstitute() const
	{
		return EffectiveSettings.bKnockHapticSubstitute;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Audio")
	bool UsesKnockRippleSubstitute() const
	{
		return EffectiveSettings.bKnockRippleSubstitute;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Motion")
	bool UsesHeartbeatWarning() const
	{
		return EffectiveSettings.bHeartbeatWarning;
	}

	/** §19.8. 인지 지원에서 노크 판정창이 넓어지는 배율. */
	UFUNCTION(BlueprintPure, Category = "Accessibility|Guidance")
	float GetKnockWindowScale() const;

	UFUNCTION(BlueprintPure, Category = "Accessibility|Motion")
	float GetComfortVignetteStrength() const
	{
		return EffectiveSettings.ComfortVignetteStrength;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Motion")
	float GetCameraTextureStrength() const
	{
		return EffectiveSettings.CameraTextureStrength;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Motion")
	float GetFieldOfViewDegrees() const
	{
		return EffectiveSettings.FieldOfViewDegrees;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Input")
	bool UsesToggleHoldInteractions() const
	{
		return EffectiveSettings.bToggleHoldInteractions;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Input")
	bool UsesToggleCrouch() const
	{
		return EffectiveSettings.bToggleCrouch;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Input")
	bool AreHapticsEnabled() const
	{
		return EffectiveSettings.bHapticsEnabled;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Audio")
	bool IsMicrophoneNoiseEnabled() const
	{
		return EffectiveSettings.bMicrophoneNoiseEnabled;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Guidance")
	bool AreHintsEnabled() const
	{
		return EffectiveSettings.bHintsEnabled;
	}

	UFUNCTION(BlueprintPure, Category = "Accessibility|Input")
	float GetHoldDurationScale() const
	{
		return EffectiveSettings.HoldDurationScale;
	}

private:
	static FIGAccessibilitySettings Sanitize(
		const FIGAccessibilitySettings& Candidate);
	void LoadPersistedSettings();
	void SavePersistedSettings() const;
	void RebuildEffectiveSettings();
	void ApplyCommandLineOverrides(FIGAccessibilitySettings& Settings) const;

	FIGAccessibilitySettings PersistedSettings;
	FIGAccessibilitySettings EffectiveSettings;
};
