#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IGMissingFloorAudioSubsystem.generated.h"

class UAudioComponent;
class UReverbEffect;
class USoundBase;
class USoundClass;
class USoundMix;
struct FSoundAttenuationSettings;

enum class EIGFootstepSurface : uint8;

/** Six logical mix lanes locked by STORY_BIBLE_MISSING_FLOOR.md §21.1. */
UENUM(BlueprintType)
enum class EIGAudioBus : uint8
{
	Entity,
	Player,
	Puzzle,
	World,
	UI,
	Score,
	Count UMETA(Hidden)
};

/** Listener-driven score states. These are presentation states, not AI logic. */
UENUM(BlueprintType)
enum class EIGAudioThreatState : uint8
{
	Calm,
	Banging,
	Listening,
	Investigating,
	Chasing,
	Finale,
	/** 붙잡힌 접촉음이 들리도록 음악을 걷는다. */
	Captured
};

/**
 * 듣고 있는 건물 공간. STORY_BIBLE_MISSING_FLOOR.md §10.4의 거리 리버브
 * 문법은 프리셋을 두 개로 고정한다 — 흡음이 있는 복도와, 흡음이 없는
 * 노출 콘크리트 계단실. 세 번째 값은 프리셋이 아니라 프리셋의 부재다:
 * 옥상은 건물 밖이므로 어떤 반향도 걸지 않는다.
 */
UENUM(BlueprintType)
enum class EIGAcousticSpace : uint8
{
	/** 4·5층 복도와 세대 안. 석고 벽과 문이 고역을 먼저 먹는다. */
	Corridor,
	/** 계단실. 평행한 콘크리트 참이 두 배 넘게 길게 울린다. */
	Stairwell,
	/** 옥상 등 건물 밖. 반향 없음. */
	Open,
	Count UMETA(Hidden)
};

/** 존재가 낼 수 있는 놀람 셋. 전부 그의 버스를 타고 그의 자리에서 난다. */
enum class EIGStinger : uint8
{
	/** 코앞에서 마주쳤다. 3.2m 안, 시야 안, 25초 쿨다운. */
	CloseCall,
	/** 추격이 시작됐다. 몸이 튀어 나가는 타격과 땅울림이고, 목소리는 없다(§4.6). */
	ChaseStart,
	/** 덮쳤다. 드라이 노크 둘 바로 앞. */
	Capture
};

/**
 * Runtime mix and score director for The Missing Floor.
 *
 * The subsystem owns six transient SoundClasses, their bus-level ducking,
 * oldest-first voice limiting and the three procedural score motifs. Spatial
 * one-shots opt in through IGAudio::SpawnOneShotAt; persistent components call
 * RegisterComponent before Play. Nothing here changes puzzle or AI truth.
 */
UCLASS()
class INDIEGAME_API UIGMissingFloorAudioSubsystem final
	: public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

	/** Assigns the bus SoundClass before a transient sound begins playback. */
	void PrepareSound(USoundBase* Sound, EIGAudioBus Bus) const;

	/**
	 * 한 번 울고 마는 소리를 건다. 버스 상한에 닿으면 소모음부터, 그다음 가장
	 * 오래된 것부터 자르지 않고 페이드아웃한다.
	 */
	void RegisterComponent(UAudioComponent* Component, EIGAudioBus Bus);

	/**
	 * 밤 내내 우는 소리를 건다. 상한에 세지 않고, 밀려나지도 않는다.
	 *
	 * 상한은 한 번 울고 마는 소리가 쌓이는 걸 막으려고 있다. 험이나 물소리는
	 * 그 대상이 아니다 — 오래됐다는 이유로 밀리면, 복도가 시끄러워진 순간에
	 * 엄폐가 조용해지고 퇴출은 페이드아웃이라 그 밤 내내 안 돌아온다. 자리를
	 * 세던 때는 베드 열 개가 WORLD 열두 자리를 쥐고 있어서, 첫 개방의 자물쇠
	 * 소리가 같은 프레임의 문소리에 밀렸다.
	 */
	void RegisterPersistentBed(UAudioComponent* Component, EIGAudioBus Bus);

	/**
	 * 소모음을 건다. 기는 걸음처럼 박자마다 쏟아지는 소리 전용이다. 상한에 닿으면
	 * 가장 먼저 밀리고, 스팅어·덮침·들숨 같은 대본 소리는 밀지 못한다. 밀 수
	 * 있는 소모음이 없으면 추적 없이 울고 끝난다.
	 */
	void RegisterExpendable(UAudioComponent* Component, EIGAudioBus Bus);

	/**
	 * §10.5. 헤드폰과 스피커를 오간다. 새로 나는 소리만 바꾸면 이미 돌고
	 * 있는 환경음 루프가 옛 방식으로 남아 설정이 반만 듣는 것처럼 된다.
	 * 그래서 살아 있는 목소리를 훑어 다시 건다.
	 */
	void SetHeadphoneOutput(bool bHeadphones);
	bool IsHeadphoneOutput() const { return bHeadphoneOutput; }

	void SetThreatState(EIGAudioThreatState NewState);

	/**
	 * §10.4 거리 리버브 문법: 지금 듣고 있는 공간의 반향을 교체한다.
	 * 매 0.2초 폴링이 플레이어가 밟은 표면으로 같은 값을 계산하므로 보통은
	 * 부를 필요가 없다. 연출이 공간을 강제해야 할 때만 직접 호출한다.
	 */
	void SetAcousticSpace(EIGAcousticSpace NewSpace);

	UFUNCTION(BlueprintPure, Category = "Audio|Missing Floor")
	EIGAcousticSpace GetAcousticSpace() const { return AcousticSpace; }

	UReverbEffect* GetAcousticPreset(EIGAcousticSpace Space) const;

	/**
	 * 밟고 있는 표면 하나로 공간을 판정한다. 계단 디딤판만 계단실이고
	 * 옥상 슬래브만 건물 밖이며, 나머지 전부는 복도로 취급한다. 세계
	 * 지오메트리를 오디오가 따로 알 필요가 없고, 표면 태그는 이미
	 * §21.2 발소리 매트릭스가 저자 데이터로 들고 있다.
	 */
	static EIGAcousticSpace ClassifyAcousticSpace(EIGFootstepSurface Surface);

	/**
	 * 버스별 거리→젖음 곡선을 감쇠 설정에 붙인다. 서브시스템 인스턴스가
	 * 아직 없는 시점에도 쓸 수 있도록 정적이다.
	 */
	static void ConfigureReverbSend(
		FSoundAttenuationSettings& Settings,
		EIGAudioBus Bus,
		float InnerRadius,
		float FalloffDistance);

	void SetPlayerListening(bool bListening);
	/**
	 * 그가 듣고 있다. 두드린 뒤의 청취(LISTENING)뿐 아니라 조사 끝의 제자리
	 * 청취도 같은 창이라, 음악 상태와 따로 WORLD −6dB만 건다. SetThreatState는
	 * 상태가 바뀔 때 이 값을 LISTENING 여부로 다시 쓴다.
	 */
	void SetEntityListening(bool bListening);
	/**
	 * 지정 침묵. 걷을 때 공기가 돌아오는 시간을 연출이 고를 수 있다. 침묵 안에서
	 * 확정된 진실의 타건은 버리지 않고, 침묵이 걷힌 뒤 음악 버스가 돌아올 틈을
	 * 두고 친다.
	 */
	void SetAuthoredSilence(bool bSilent, float ReleaseFadeSeconds = 0.45f);
	void SetEntityDistance(float DistanceCentimeters);
	/** 놀람 하나. 존재 버스, 그의 자리에서. */
	bool PlayStinger(EIGStinger Kind, const FVector& Location);
	/**
	 * 방금 낸 스팅어. 포획은 덮침 녹음의 숨 꼬리를 잘라 암전 속 노크 둘이 묻히지
	 * 않게 한다(AIGListenerEntity::BeginCapture).
	 */
	const TObjectPtr<UAudioComponent>& GetStingerComponent() const { return StingerComponent; }
	/** 압박 층의 지금 볼륨 0~1. 계약과 프로브가 읽는다. */
	float GetPresenceAlpha() const { return PresenceAlpha; }
	void SetTitleMode(bool bEnabled);
	/**
	 * 타이틀 위에 다른 소리를 올리는 동안(밤 5) 타이틀의 노크와 조율 모티프를
	 * 멈춰 둔다. 풀면 타이틀 소리 풍경이 처음 들어왔을 때처럼 다시 든다.
	 */
	void HoldTitleSoundscape(bool bHold);
	/**
	 * 엔딩 카드 바로 뒤의 타이틀이라고 알린다. 다음에 타이틀이 켜질 때 한 번만,
	 * 몇 초를 비운 뒤 모티프를 천천히 올리고 첫 노크를 늦춘다. 타이틀이 이미
	 * 켜져 있으면 아무것도 하지 않는다.
	 */
	void ArmPostEndingTitle();
	/**
	 * 위협 상태와 상관없이 지금 음악을 놓는다. 장면이 통째로 바뀌는 자리(에필로그)
	 * 용이다. 타이틀의 음악은 타이틀이 가지고 있으므로 건드리지 않는다.
	 */
	void ReleaseScore(float FadeSeconds);
	/**
	 * 새벽에 눈이 감기는 동안 밤의 음악과 압박 층, 추격의 꼬리를 같이 감는다.
	 * 눈을 뜨면 잠금 해제음과 아침 독백, 날숨만 남는다.
	 */
	void ReleaseScoreForDawn(float FadeSeconds);
	/**
	 * 밤4 대치가 끝나도 피날레 드론을 붙들어 둔다. 붙든 동안에는 그가 잠들며 보내는
	 * Calm이 음악을 걷지 않고, 다른 상태가 오면 붙듦이 풀린다. 엔딩을 고르거나 밤이
	 * 끝나면 푼다. 피날레가 울고 있을 때만 붙든다.
	 */
	void HoldFinaleScore(bool bHold);
	/** 붙든 피날레 드론을 Level(0.05 이상)까지 Seconds에 걸쳐 가라앉힌다. */
	void DuckFinaleScore(float Seconds, float Level);
	/** 보정한 사용자 이득을 여섯 버스에 같은 비율로 적용한다. */
	void SetUserMasterVolume(float Volume01, float FadeSeconds = 0.08f);
	float GetUserMasterVolume() const { return UserMasterVolume; }

	/**
	 * §21.1 대원칙: 존재의 소리는 절대 눌리지 않는다. 그래서 사용자 손이
	 * 닿는 버스는 SCORE와 WORLD 둘뿐이다. ENTITY·PLAYER·PUZZLE을 줄일 수
	 * 있게 두면 놓친 노크가 믹스의 여유처럼 보이지만, 그건 게임의 실패다.
	 *
	 * 음악은 0까지 내려간다 — 작가가 얹은 것이라 없어도 사건은 남는다.
	 * 환경음은 바닥이 있다 — 건물이 내는 소리 자체가 단서다.
	 */
	static constexpr float MinimumAmbienceVolume = 0.40f;

	void SetScoreUserVolume(float Volume01, float FadeSeconds = 0.08f);
	void SetAmbienceUserVolume(float Volume01, float FadeSeconds = 0.08f);
	float GetScoreUserVolume() const { return ScoreUserVolume; }
	float GetAmbienceUserVolume() const { return AmbienceUserVolume; }

	/** 버스별 사용자 배율. 손댈 수 없는 버스는 언제나 1이다. */
	float GetBusUserScale(EIGAudioBus Bus) const;
	/** 보정용 위층 노크를 ENTITY 버스와 HRTF 경로로 재생한다. */
	void PlayCalibrationKnock();
	int32 GetCalibrationKnockPlayCount() const
	{
		return CalibrationKnockPlayCount;
	}
	/**
	 * 소리 맞추기의 음악·환경음 칸. 값을 바꿀 때마다 그 버스의 소리(타이틀 모티프,
	 * 밤 복도의 공기)를 2.6초 들려준다. 메뉴가 월드를 멈춰 두므로 UI 소리로 낸다.
	 * 같은 칸을 연달아 바꾸면 처음부터 다시 틀지 않고 끝만 미룬다.
	 */
	void PlayCalibrationPreview(bool bMusic);
	void StopCalibrationPreview(float FadeSeconds = 0.15f);
	/**
	 * 진실 하나가 맞물릴 때의 조율 한 타. 바로 치지 않고 대기열에 올린다. 확정
	 * 순간의 효과음(망치, 차단기 딸깍) 어택을 비켜 조금 늦게 치고, 여럿이 한꺼번에
	 * 확정되면 모티프의 타건 간격으로 한 음씩 친다. 지정 침묵 동안은 기다린다.
	 */
	void PlayTruthConfirmation(int32 ConfirmationIndex);
	/**
	 * 벽 앞에서 피날레 드론을 걷고 조율 걸음을 시작한다. 에필로그가 시작되면
	 * ReleaseScore가 이걸 암전과 함께 감고, 정음은 공방 스코어가 처음 친다.
	 */
	void PlayEndingATuningResolution();

	UFUNCTION(BlueprintPure, Category = "Audio|Missing Floor")
	EIGAudioThreatState GetThreatState() const { return ThreatState; }

	UFUNCTION(BlueprintPure, Category = "Audio|Missing Floor")
	bool IsPlayerListening() const { return bPlayerListening; }

	UFUNCTION(BlueprintPure, Category = "Audio|Missing Floor")
	bool IsAuthoredSilence() const { return bAuthoredSilence; }

	UFUNCTION(BlueprintPure, Category = "Audio|Missing Floor")
	bool IsEntityNearPlayer() const { return bEntityNearPlayer; }

	UFUNCTION(BlueprintPure, Category = "Audio|Missing Floor")
	bool IsTitleModeActive() const { return bTitleMode; }

	/** Effective dB including state ducking; silence reports -96 dB. */
	/**
	 * §21.1 더킹. 이 함수가 그 표의 유일한 구현이다.
	 *
	 * 예전에는 같은 규칙이 RefreshMix와 GetEffectiveBusDecibels 두 곳에
	 * 적혀 있었다. 한쪽만 조율하면 들리는 믹스와 계약이 보고하는 믹스가
	 * 갈라지고, 그 차이는 귀로만 발견된다.
	 */
	float GetBusDuckingDecibels(EIGAudioBus Bus) const;

	float GetEffectiveBusDecibels(EIGAudioBus Bus) const;
	int32 GetVoiceCap(EIGAudioBus Bus) const;
	USoundClass* GetBusSoundClass(EIGAudioBus Bus) const;

	/** Headless/runtime receipt for the complete six-bus contract. */
	bool ValidateContract(FString& OutFailure) const;

	/** Inclusive local-time easter-egg window, 04:30 through 05:30. */
	static bool IsTitleReplyTime(const FDateTime& LocalTime);

private:
	friend class AIGAudioPresentationProbe;
	struct FTrackedVoice
	{
		TWeakObjectPtr<UAudioComponent> Component;
		uint64 Serial = 0;
		/** 늘 우는 소리. 상한에 세지 않고 밀리지도 않는다. */
		bool bPersistent = false;
		/** 박자마다 쏟아지는 소리(기는 걸음). 상한에 닿으면 가장 먼저 밀린다. */
		bool bExpendable = false;
	};

	static constexpr int32 BusCount = static_cast<int32>(EIGAudioBus::Count);

	void RegisterVoice(
		UAudioComponent* Component,
		EIGAudioBus Bus,
		bool bPersistent,
		bool bExpendable = false);
	/** 대기열에서 꺼낸 타건 하나를 실제로 친다. */
	void PlayTruthStrikeNow(int32 ConfirmationIndex);
	void BuildBusGraph();
	void BuildAcousticPresets();
	void ApplyAcousticSpace(float FadeSeconds);
	void PollAcousticSpace();
	void RefreshMix(float FadeSeconds = 0.12f);
	void PruneVoices();
	void SwitchScore(EIGAudioThreatState NewState, bool bForce = false);
	/**
	 * bChaseRelease면 추격 루프를 끄지 않고 0.4초 안에 거의 들리지 않게 내린 뒤
	 * FadeSeconds가 지나면 멈춘다. 그 사이 추격이 돌아오면 같은 박자로 되살고,
	 * 빠진 자리에는 클러스터 스탭 하나가 울린다.
	 */
	void StopScore(float FadeSeconds, bool bChaseRelease = false);
	/** 추격 끝의 스탭을 걷는다. */
	void FadeChaseTail(float Seconds);
	/**
	 * 지금이 몇째 밤이고 그가 얼마나 사나운지(§20.2 밤, §4.3-7 공격성 티어). 새
	 * 음악을 고를 때만 읽는다. 티어는 그가 쓰는 실효값이다(성급한 밤은 최소 1).
	 */
	void ResolveScoreEscalation(int32& OutNightIndex, int32& OutTier) const;
	/** 압박 층이 0에서 오르기 시작하는 거리. 그가 듣는 거리의 3m 바깥이다. */
	float GetPresenceReachCentimeters() const;
	void StartTitleSoundscape();
	void StopTitleSoundscape();
	void PlayTitleKnockCycle();
	void PlayTitleReply();
	FVector ResolveTitleCueLocation(bool bReply) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundClass>> BusSoundClasses;

	UPROPERTY(Transient)
	TObjectPtr<USoundMix> RuntimeMix;

	/** 복도·계단실 두 프리셋. Open은 프리셋 없이 태그를 내린다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UReverbEffect>> AcousticPresets;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ScoreComponent;
	/** 추격이 곧 재개되면 빠지던 음악을 같은 박자에서 되살린다. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ReleasingScoreComponent;
	EIGAudioThreatState ReleasingScoreState = EIGAudioThreatState::Calm;
	/** 추격이 끝난 자리에 남는 스탭(§10.2 「스탭만 남기고 4초 테일」). */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ChaseTailComponent;
	/** 내려 둔 추격 루프를 실제로 멈출 월드 시각. 음수면 기다리는 것이 없다. */
	double ChaseReleaseStopAtSeconds = -1.0;
	/** 밤4 대치 뒤 최종 선택까지 피날레 드론을 붙들고 있다. */
	bool bFinaleScoreHeld = false;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> StingerComponent;
	double LastChaseStingerSeconds = -1000.0;
	/**
	 * 존재 거리로 켜지는 압박 층. 그가 듣는 거리의 3m 바깥에서 0(밤1 12m, 밤2·3
	 * 14m, 밤4 16m), 3m 안에서 1. 순찰 중에는 음악이 없다는 §10.2의 원칙은
	 * 그대로다 — 이건 음악이 아니라 그가 가까이 있다는 몸의 감각이다. 2D, 스코어
	 * 버스, 상시 베드.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> PresenceComponent;
	float PresenceAlpha = 0.0f;
	void UpdatePresenceLayer(float DistanceCentimeters);
	void FadePresenceLayer(float Target, float Seconds);

	static constexpr int32 AcousticSpaceCount =
		static_cast<int32>(EIGAcousticSpace::Count);

	TArray<FTrackedVoice> ActiveVoices[BusCount];

	bool bHeadphoneOutput = true;
	uint64 NextVoiceSerial = 1;
	float VoicePruneAccumulator = 0.0f;
	float AcousticPollAccumulator = 0.0f;
	double LastEntityDistanceUpdateSeconds = -1000.0;
	EIGAcousticSpace AcousticSpace = EIGAcousticSpace::Corridor;
	bool bAcousticSpaceResolved = false;
	bool bAcousticSpaceApplied = false;
	EIGAudioThreatState ThreatState = EIGAudioThreatState::Calm;
	EIGAudioThreatState ActiveScoreState = EIGAudioThreatState::Calm;
	bool bPlayerListening = false;
	bool bEntityListening = false;
	bool bAuthoredSilence = false;
	bool bEntityNearPlayer = false;
	bool bTitleMode = false;
	bool bMixPushed = false;
	float UserMasterVolume = 1.0f;
	float ScoreUserVolume = 1.0f;
	float AmbienceUserVolume = 1.0f;
	int32 CalibrationKnockPlayCount = 0;
	double NextTitleKnockRealTime = -1.0;
	double PendingTitleReplyRealTime = -1.0;
	/** 밤 5가 타이틀 위에 올라와 있는 동안 참. 타이틀 노크를 치지 않는다. */
	bool bTitleSoundscapeHeld = false;
	/** ArmPostEndingTitle이 걸었다. 다음 타이틀 진입이 한 번 쓰고 지운다. */
	bool bPostEndingTitleArmed = false;
	/** SwitchScore가 타이틀 모티프를 엔딩 직후의 긴 페이드로 올리는 동안만 참. */
	bool bPostEndingScoreFade = false;
	/** 엔딩 직후 비워 둔 타이틀에서 모티프를 올릴 실시각. 음수면 기다리는 것이 없다. */
	double PendingTitleScoreRealTime = -1.0;
	/** 소리 맞추기의 미리 듣기. 한 번에 하나만 운다. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> CalibrationPreviewComponent;
	bool bCalibrationPreviewIsMusic = false;
	double CalibrationPreviewStopRealTime = -1.0;
	/** 아직 치지 않은 진실 확정 타건. 월드 시간으로 재므로 일시정지 중에는 기다린다. */
	TArray<int32> PendingTruthStrikes;
	double NextTruthStrikeWorldSeconds = 0.0;
};
