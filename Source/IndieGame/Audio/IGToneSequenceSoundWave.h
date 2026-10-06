#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "IGToneSequenceSoundWave.generated.h"

/** Oscillator shape for one scheduled note. */
enum class EIGToneWaveform : uint8
{
	Sine,
	/** Odd-harmonic blend that reads as a soft electronic square. */
	SoftSquare,
	Triangle,
	/** Band-limited value noise; Frequency acts as the noise bandwidth in Hz. */
	ValueNoise,
	/** 매 샘플 독립 잡음, 전대역. 긁힘·바람·거친 표면의 재료. Frequency는 안 쓴다. */
	WhiteNoise,
	/**
	 * 공진 대역통과를 지난 백색 잡음. Frequency가 중심(≤7kHz), Resonance 0~1이
	 * Q 2~40. 발소리 몸통·천 스침·숨·바람 휘파람은 전부 이것으로 만든다.
	 */
	BandNoise,
	/** 부드럽게 포화된 사인. 저역 타격·심박·드론의 몸통. 사인보다 배음이 조금 있다. */
	Sub,
	/**
	 * 성긴 알갱이. Frequency가 초당 밀도, 알갱이 하나는 짧게 꺼지는 잡음이다.
	 * 석고 부스러기·모래·불티·전기 잡음.
	 */
	Crackle,
	/**
	 * 카플러스-스트롱 공명체. Frequency가 음높이(≥40Hz), Resonance 0~1이 울림
	 * 길이. 노크의 나무·석고 몸통, 금속 난간 울림, 튕긴 줄.
	 */
	Pluck,
	/**
	 * FM 목소리. Frequency가 기본음, Resonance가 변조 깊이(0~1 → 지수 0~7).
	 * 으르렁·신음·숨 섞인 목소리. 사람 소리라기보다 몸에서 나는 소리.
	 */
	Growl
};

/** Physical floor families used by the §21.2 stealth/noise matrix. */
UENUM(BlueprintType)
enum class EIGFootstepSurface : uint8
{
	Vinyl,
	Concrete,
	MetalStair,
	Rooftop,
	GypsumDebris,
	Water
};

/**
 * One note in a tone sequence. All fields are immutable once playback starts.
 * The amplitude envelope is a smooth attack over AttackFraction of the note,
 * followed by a (1 - t)^ReleasePower decay to zero.
 */
struct FIGToneNote
{
	float StartSeconds = 0.0f;
	float DurationSeconds = 0.1f;
	float FrequencyHz = 440.0f;
	float Amplitude = 0.1f;
	float AttackFraction = 0.02f;
	float ReleasePower = 1.0f;
	EIGToneWaveform Waveform = EIGToneWaveform::Sine;
	/**
	 * 파형별 두 번째 손잡이 0~1. BandNoise는 Q, Pluck은 울림 길이, Growl은 변조
	 * 깊이. 나머지는 안 읽는다. 여덟째 자리라 일곱 자리 초기화도 그대로 된다.
	 */
	float Resonance = 0.0f;
};

/**
 * Allocation-stable procedural PCM source that mixes a fixed schedule of notes.
 * It backs every melodic or percussive cue in the prologue: the store jingle,
 * the entrance chime, scanner/register cues, door creaks and footsteps.
 *
 * Build the note list on the game thread via the static factories (or
 * ConfigureNotes) before handing the wave to an audio component; afterwards
 * the sample cursor is owned exclusively by the audio render thread.
 */
UCLASS()
class INDIEGAME_API UIGToneSequenceSoundWave final : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	explicit UIGToneSequenceSoundWave(const FObjectInitializer& ObjectInitializer);

	/**
	 * Installs the note schedule. When bInLooping is true the pattern repeats
	 * every LoopSeconds; otherwise the wave renders silence after the last
	 * note and stops at its finite Duration.
	 */
	void ConfigureNotes(TArray<FIGToneNote>&& InNotes, bool bInLooping, float LoopSeconds = 0.0f);
	/** Applies a small integrated pitch drift without changing sequence timing. */
	void ConfigurePitchWow(float DepthRatio, float RateHz);
	/**
	 * 방 울림. 음의 합에 피드백 지연 하나를 건다 — 짧은 지연(8~40ms)에 되먹임
	 * 0.3~0.6이면 노크와 타격이 벽 사이에서 되울리는 것처럼 들린다. 되먹임 고리
	 * 안의 저역 필터(Damping 0~1, 클수록 밝음)가 고역부터 죽인다. 최종 합은
	 * 음의 합 × (1 + Mix ÷ (1 − Feedback))을 넘지 않는다. audit_tone_headroom.py가
	 * 이 곱을 같이 센다. 유한 파형은 꼬리가 60dB 죽을 때까지 길어진다.
	 */
	void ConfigureRoomTail(float DelaySeconds, float Feedback, float Damping, float Mix);
	/** Finite authored length, including the protected release tail. */
	float GetConfiguredDurationSeconds() const { return Duration; }
	/** True when ConfigureNotes installed an indefinitely repeating pattern. */
	bool IsConfiguredLooping() const { return bLooping; }

	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;

	/** "Ding-dong" two-tone convenience-store entrance chime. */
	static UIGToneSequenceSoundWave* CreateDoorChime(UObject* Outer);

	/** Short descending squeak for a swinging hinge. 닫힐 때는 오르는 삐걱. */
	static UIGToneSequenceSoundWave* CreateDoorCreak(UObject* Outer, bool bClosing = false);

	/** 물건을 손에 드는 소리: 천·비닐 스침 두 번과 가벼운 툭. */
	static UIGToneSequenceSoundWave* CreatePickupRustle(UObject* Outer);

	/** 이삿짐 상자의 박스 테이프를 한 번에 뜯는다. 끝에서 날개가 툭 선다. */
	static UIGToneSequenceSoundWave* CreatePackingTapeRip(UObject* Outer);

	/** 조율 공구 두루마리 안에서 작은 쇠붙이 둘이 맞닿는다. */
	static UIGToneSequenceSoundWave* CreateToolRollClink(UObject* Outer);

	/** 손전등 슬라이드 스위치. 켤 때는 위로 딸깍, 끌 때는 아래로 둔탁하게. */
	static UIGToneSequenceSoundWave* CreateSwitchClick(UObject* Outer, bool bOn);

	/**
	 * 발에 걸린 가벼운 소품이 바닥이나 벽에 부딪는다. bHollow는 속이 빈 페트병의
	 * 통 울림, 아니면 실내화·골판지의 둔한 톡이다. 발소리 합성(62Hz 뒤꿈치)을
	 * 빌리면 150g짜리 병이 사람 걸음처럼 무겁게 난다.
	 */
	static UIGToneSequenceSoundWave* CreateKickedPropKnock(UObject* Outer, bool bHollow);

	/** Low thud used when a door settles shut. */
	static UIGToneSequenceSoundWave* CreateDoorThud(UObject* Outer);

	/** Brief handle rattle for a locked door. */
	static UIGToneSequenceSoundWave* CreateLockedRattle(UObject* Outer);

	/**
	 * 그 시간의 공동현관을 민다. 푸시바는 끝까지 들어가는데 문짝이 보이지 않는
	 * 무언가에 둔하게 막히고, 한 번 더 밀어도 같다. 걸쇠 소리가 없는 것이 요점이다 —
	 * 잠긴 게 아니다.
	 */
	static UIGToneSequenceSoundWave* CreateHeldDoorPush(UObject* Outer);

	/** Single barcode-scanner beep. */
	static UIGToneSequenceSoundWave* CreateScannerBeep(UObject* Outer);

	/**
	 * 밖에서 누르는 디지털 도어락. 사람 손 박자로 넷, 확인 하나, 그리고 풀리는
	 * 「띠리리릭」과 모터·걸림쇠. 우리나라 현관에서 가장 익숙한 소리다.
	 */
	static UIGToneSequenceSoundWave* CreateDoorlockCode(UObject* Outer, bool bAccepted = true);

	/**
	 * 철문 너머로 들리는 말소리. 단어는 들리지 않고 높낮이와 박자만 온다.
	 * 남녀와 나이는 부르는 쪽이 피치로 정한다. 자막이 말을 맡는다.
	 */
	static UIGToneSequenceSoundWave* CreateDoorMurmur(UObject* Outer);

	/**
	 * 허리춤 열쇠 꾸러미가 걸음마다 부딪는 소리. 짧고 높은 쇳소리 몇 개가 0.3초 안에
	 * 흩어진다. 두 벌을 번갈아 써서 같은 소리가 되풀이되지 않게 한다.
	 */
	static UIGToneSequenceSoundWave* CreateKeyRingJingle(UObject* Outer, bool bSecondVariant);

	/** 관리실 슬리퍼를 끄는 걸음. 발뒤꿈치가 닿고 밑창이 바닥을 쓸며 끌린다. */
	static UIGToneSequenceSoundWave* CreateSlipperScuff(UObject* Outer);

	/** Register confirmation beeps with a cash-drawer clunk. */
	static UIGToneSequenceSoundWave* CreateRegisterSound(UObject* Outer);

	/**
	 * A dry relay crack followed by the short 120 Hz ring of a failing
	 * fluorescent ballast. Kept separate from footsteps so a light going out
	 * reads as an electrical event even when it happens behind the player.
	 */
	static UIGToneSequenceSoundWave* CreateFluorescentBallastSnap(UObject* Outer);

	/**
	 * Looping, unintelligible low-band radio cadence heard through unit 401's
	 * closed door. It suggests an early-morning prayer broadcast without
	 * synthesizing words or a recognizable human voice.
	 */
	static UIGToneSequenceSoundWave* CreateMuffledPrayerRadio(UObject* Outer);

	/** One rough cardboard scrape and a small box-settle thump. */
	static UIGToneSequenceSoundWave* CreateCardboardDrag(UObject* Outer);

	/**
	 * 공구 카트 바퀴 하나가 짧게 굴러 멎는다(§8 0-1). 금 간 캐스터가 한 바퀴 돌
	 * 때마다 딸깍이 나고, 카트가 느려지는 만큼 간격이 벌어진다. 입주 저녁 천장
	 * 너머에서 한 번, 밤3 5층에서 그 카트를 밀 때 한 번 더 난다. 슬래브 차폐
	 * (900Hz)를 지나도 딸깍이 남게 몸통을 저역에 둔다.
	 */
	static UIGToneSequenceSoundWave* CreateToolCartRoll(UObject* Outer);

	/**
	 * 조율 렌치가 콘크리트에 내려앉는다(§8 0-4). 25cm 강철 자루의 굽힘 모드
	 * 780·2150·4210Hz와 나무 손잡이의 둔한 톡. 입주 날 옥상 철문 너머에서는
	 * 차폐가 780Hz 밑만 남기고, 밤3에 렌치를 집을 때는 같은 쇠가 밝게 난다.
	 */
	static UIGToneSequenceSoundWave* CreateTuningWrenchClink(UObject* Outer);

	/** Dry paper lift and fingertip brush used by the daylight evidence journal. */
	static UIGToneSequenceSoundWave* CreateJournalPageTurn(UObject* Outer);

	/** 문틈을 통과해 복도 타일에 안착하는 얇은 종이 소리. */
	static UIGToneSequenceSoundWave* CreatePaperDoorSlide(UObject* Outer);

	/**
	 * Looping cheerful music-box store jingle.
	 * PitchSemitones and TimeScale author the degraded CH02 version without
	 * changing global audio-component pitch (which would couple both values).
	 */
	static UIGToneSequenceSoundWave* CreateStoreJingle(
		UObject* Outer,
		float PitchSemitones = 0.0f,
		float TimeScale = 1.0f);

	/** Looping CH03 flooded-corridor bed with low water mass and sparse drops. */
	static UIGToneSequenceSoundWave* CreateFloodedCorridorWaterBed(UObject* Outer);

	/**
	 * Soft shoe-on-floor step. PitchScale shifts the surface character
	 * (lower = duller wood, higher = harder tile) and Amplitude scales loudness.
	 */
	static UIGToneSequenceSoundWave* CreateFootstep(UObject* Outer, float PitchScale, float Amplitude);

	/** Surface-authored footfall; gameplay loudness is applied by the caller. */
	static UIGToneSequenceSoundWave* CreateSurfaceFootstep(
		UObject* Outer,
		EIGFootstepSurface Surface,
		float VariationPitch,
		float Amplitude = 1.0f);

	/** M-조율: 220 Hz triangle, -30 cents, rising two cents per strike. */
	static UIGToneSequenceSoundWave* CreateTuningMotif(
		UObject* Outer,
		bool bResolvedEndingA);

	/** One M-조율 strike for a newly crossed truth; index rises by two cents. */
	static UIGToneSequenceSoundWave* CreateTuningStrike(
		UObject* Outer,
		int32 ConfirmationIndex);

	/**
	 * M-공동: 44 Hz cavity mass, 180 Hz value noise and sparse water ticks.
	 * 밤3부터 벽 안의 숨과 물 틱이 촘촘해지고, 밤4에는 가는 긴장음이 한 줄 더
	 * 붙는다. 박은 끝까지 없다 — 박이 있는 음악은 추격 하나뿐이다. bFinale은 밤4
	 * 대치의 드론이다. 루프마다 열 번째 진실의 음(220 Hz −12센트)을 한 번 치고
	 * 끝내 맞추지 않는다. 그 음은 엔딩 A의 조율 걸음이 푼다.
	 */
	static UIGToneSequenceSoundWave* CreateCavityDrone(
		UObject* Outer,
		int32 NightIndex = 1,
		bool bFinale = false);

	/**
	 * 118 BPM pursuit loop: 52 Hz pulse plus a 220/223/227 Hz cluster.
	 * 밤2가 기준이다. 밤1은 튕긴 불협 없이 펄스와 클러스터만 가고, 밤3부터 앞박
	 * 쇳소리와 높은 불협이, 밤4에는 둘째 마디의 반박 펄스와 넷째 불협 줄이 붙는다.
	 */
	static UIGToneSequenceSoundWave* CreateChaseScore(
		UObject* Outer,
		int32 NightIndex = 2);

	/**
	 * 추격이 끝나는 자리에 남는 스탭 하나(§10.2). 클러스터 220/223/227을 한 번
	 * 치고 4초 동안 울린다. 펄스는 없다 — 그건 음악 감독이 먼저 걷는다.
	 */
	static UIGToneSequenceSoundWave* CreateChaseTail(UObject* Outer);

	/** 첫 밤에 잠을 깨우는 짧은 알람음. */
	static UIGToneSequenceSoundWave* CreateAlarmFirstNote(UObject* Outer);

	/** Two flat descending handset beeps that end a failed call attempt. */
	static UIGToneSequenceSoundWave* CreateCallFailTone(UObject* Outer);

	/**
	 * 국내 통화 연결음. 440+480Hz를 1초 울리고 2초 쉰 뒤, 두 번째 신호가
	 * 울리다 받는 딸깍에 끊긴다. 뒤에는 열린 회선의 숨만 남고 목소리는 없다.
	 * 엔딩 B의 05:30, 두 번째 신고.
	 */
	static UIGToneSequenceSoundWave* CreateCallRingback(UObject* Outer);

	/** 계산대 호출벨. 손바닥으로 치는 종 하나의 짧은 타격과 느리게 맥놀이하는 울림. */
	static UIGToneSequenceSoundWave* CreateServiceBellDing(UObject* Outer);

	/** Tiny dry relay click for a small appliance switching off. */
	static UIGToneSequenceSoundWave* CreateRelayClick(UObject* Outer);

	/**
	 * 두꺼비집 차단기를 손으로 넘기는 소리. 접점이 붙는 딸깍 뒤로 레버가 멈추는
	 * 몸통과 철제 함의 짧은 울림이 온다. 소음 0.55짜리 행동이 22ms 딸깍이면 위층까지
	 * 들린다는 걸 귀로 못 배운다. 새벽 잠금·CCTV·폰은 계속 CreateRelayClick이다.
	 */
	static UIGToneSequenceSoundWave* CreateBreakerThrow(UObject* Outer);

	/** Loaded carrier bag settling onto concrete: crinkle, then bottle knock. */
	static UIGToneSequenceSoundWave* CreatePlasticBagSetDown(UObject* Outer);

	/** The same bag gathered and lifted: stretch creak and a light clink. */
	static UIGToneSequenceSoundWave* CreatePlasticBagLift(UObject* Outer);

	/** Duvet weight settling once: cloth friction with no breath rhythm. */
	static UIGToneSequenceSoundWave* CreateClothSettle(UObject* Outer);

	/** Flexible duct unfolding, then a ventilation fan holding low RPM. */
	static UIGToneSequenceSoundWave* CreateVentDuctSpinUp(UObject* Outer);

	/**
	 * A phone vibrating far away on a desk. The pattern dies before its third
	 * bar completes; the screen never lights, so the sound is all there is.
	 */
	static UIGToneSequenceSoundWave* CreatePhoneVibrationUnfinished(UObject* Outer);

	// --- The Missing Floor: the one upstairs ------------------------------

	/** Close, exhausted inhale/exhale loop for the five-dawn black interlude. */
	static UIGToneSequenceSoundWave* CreateTrappedBreathBed(UObject* Outer);

	/**
	 * 기는 한 걸음: 손바닥이 바닥을 치고 무릎이 끌린다. 끌림 루프 위에 박자마다
	 * 얹는 유한 소리. 장판은 찍찍거리고 타일은 모래가 갈린다. 변주는 피치 배수로.
	 */
	static UIGToneSequenceSoundWave* CreateEntityCrawlStep(UObject* Outer, bool bVinyl);

	/** 그의 숨. 3.2초 루프, 들이쉬고 목이 울리고 내쉰다. 가까울수록 커진다. */
	static UIGToneSequenceSoundWave* CreateEntityBreathLoop(UObject* Outer);

	/** 어둠이 숨을 들이쉰다. 어둑시니가 나타나는 소리. 목소리가 없는 바람이다. */
	static UIGToneSequenceSoundWave* CreateDarknessInhale(UObject* Outer);

	/** 어둑시니의 숨. 3.6초 루프. 쳐다볼수록 커지게 볼륨은 부르는 쪽이 올린다. */
	static UIGToneSequenceSoundWave* CreateDarknessBreathLoop(UObject* Outer);

	/** 무엇을 들었을 때: 날카롭게 들이쉬고 낮게 으르렁. 조사가 시작되는 소리. */
	static UIGToneSequenceSoundWave* CreateEntityAlertVocal(UObject* Outer);

	/**
	 * 추격 진입(녹음 Stinger_ChaseStart가 없을 때): 손바닥 두 번, 미장 갈라짐,
	 * 끌림 밀물, 저역 타격, 불협 세 줄. 목소리는 없다. 1.5초.
	 */
	static UIGToneSequenceSoundWave* CreateEntityChaseScream(UObject* Outer);

	/** 코앞에서 마주쳤을 때의 스팅어. 타격·저역 낙하·불협 세 줄·잡음 밀물. */
	static UIGToneSequenceSoundWave* CreateCloseCallStinger(UObject* Outer);

	/** 포획 직전의 덮침. 저역과 천 스침, 목소리. 뒤에 드라이 노크 둘이 온다. */
	static UIGToneSequenceSoundWave* CreateCaptureLunge(UObject* Outer);
	static UIGToneSequenceSoundWave* CreateCaptureStruggle(UObject* Outer);
	/** 포획 화면이 끊기는 순간. 바닥에 닿는 둔한 타격과 귀에 남는 높은 울림. */
	static UIGToneSequenceSoundWave* CreateCaptureCut(UObject* Outer);

	/**
	 * 압박 층. 36Hz 저역과 2.4kHz 가는 휘파람, 느린 목울림. 6초 루프. 소리 자체는
	 * 작고 오디오 감독이 존재 거리로 볼륨을 올린다 — 14m 밖에서 0, 3m 안에서 1.
	 */
	static UIGToneSequenceSoundWave* CreatePresenceLayer(UObject* Outer);

	/** 건물 소리 넷. 밤 사이 위쪽에서 무작위로 한 번씩 난다. */
	static UIGToneSequenceSoundWave* CreateSettlePipeKnock(UObject* Outer);
	static UIGToneSequenceSoundWave* CreateSettleTimberCreak(UObject* Outer);
	static UIGToneSequenceSoundWave* CreateSettleFarDoorSlam(UObject* Outer);
	static UIGToneSequenceSoundWave* CreateSettlePlasterTick(UObject* Outer);
	/**
	 * 옥상의 전동 드릴. 다섯 번 돌다 멈춘다, 11초. 밤1에 위에서 누가 일한다는
	 * 것을 처음 듣는다 — 목한수의 첫 흔적이고, 밤4의 한 마디를 두 밤의
	 * 노동으로 번다.
	 */
	static UIGToneSequenceSoundWave* CreateCordlessDriverRun(UObject* Outer);

	/**
	 * Three deliberate knuckle knocks on a stud wall, evenly spaced. The
	 * entity's idle cycle: it knocks, then listens. Muffle01 rolls off the
	 * contact click for playback through a closed wall (1 = fully entombed).
	 */
	static UIGToneSequenceSoundWave* CreateWallKnockTriple(UObject* Outer, float Muffle01 = 0.0f);

	/** One player-timed knuckle tap; P4 assembles three calls into its rhythm. */
	static UIGToneSequenceSoundWave* CreateWallKnockSingle(
		UObject* Outer,
		float Muffle01 = 0.0f);

	/**
	 * Two soft knocks, close together: the calmed reply it gives when an
	 * answer reaches it, and the last thing a captured player hears.
	 */
	static UIGToneSequenceSoundWave* CreateWallKnockReply(UObject* Outer);

	/**
	 * The family signal: two, a rest, one — "문 열어, 나야." The player's
	 * P4 answer and, muffled, the reply that comes back through the studs.
	 */
	static UIGToneSequenceSoundWave* CreateAnswerKnockPattern(
		UObject* Outer,
		float Muffle01 = 0.0f);

	/**
	 * Looping crawl bed for the entity: palm plant, a long dry drag of
	 * cloth-and-weight over concrete, and a plaster grit tail. Volume is
	 * driven by movement speed so silence means it is holding still.
	 *
	 * 끌림 2종 (§10.3): concrete is gritty and carries low. 장판 is thin vinyl
	 * over screed, so it loses the rumble, hisses higher, and squeaks where the
	 * cloth sticks and slips. Which floor he is on is a fact about *where* he
	 * is, and a player who has learned both hears him come inside.
	 */
	static UIGToneSequenceSoundWave* CreateEntityDragLoop(
		UObject* Outer,
		bool bVinyl = false);

	/**
	 * Hardened plaster shell settling: two or three dry hairline cracks.
	 * Played when the entity stops moving to listen.
	 */
	static UIGToneSequenceSoundWave* CreatePlasterSettle(UObject* Outer);

	/**
	 * 분진 낙하 (§10.3, parameters fixed by §21.3) — the fine powder his drag
	 * scrapes off a joint, sifting down onto tile over 0.90 s. Deliberately the
	 * thinnest cue in the game: broadband hiss high-passed at 6 kHz with an
	 * exponential decay, and **no low frequency at all**. Powder has no mass, so
	 * anything lower would turn it into the wall itself moving — that is
	 * CreatePlasterSettle — or into falling debris, which is 미장 갈라짐.
	 *
	 * Being pure high frequency is also why it works: the §10.2 listening window
	 * drops the low end by 6 dB, so this survives the duck that swallows
	 * everything else, and it is the one cue that says "he passed here" rather
	 * than "he is here". It rides BUS_ENTITY, so §10.4 reverb tells the player
	 * whether the sift is two floors up or in this corridor.
	 */
	static UIGToneSequenceSoundWave* CreatePlasterDustFall(UObject* Outer);

	/**
	 * 석고 판재 더미가 넘어져 바닥을 친다(비트 2-5). 판면이 바닥을 때리는 넓은
	 * 찰싹, 무게가 실린 저역, 판이 우는 대역, 부서진 모서리의 부스러기. 문짝도
	 * 걸쇠도 없다 — 예전 자리의 문 쾅과 손잡이 덜컹은 「누가 문을 닫는다」로
	 * 들렸다. 첫 장과 나머지는 피치로 가른다.
	 */
	static UIGToneSequenceSoundWave* CreateBoardStackFall(UObject* Outer);

	/**
	 * 채널 전환 지직임 (§8 비트 2-2) — an analog tube losing and finding sync.
	 *
	 * bCollapse false is the 0.35 s acquire: hiss decaying as the picture locks.
	 * bCollapse true is the 0.90 s death: hiss swelling, two sync tears, then
	 * nothing. The channel dies once and cannot be pressed again, so the collapse
	 * has to sound terminal rather than like a dropout that might come back.
	 *
	 * The hum is 60 Hz because the building is on Korean mains, and the thin
	 * 15.734 kHz line whine is the NTSC horizontal rate this monitor was built
	 * for. Both are there for the players who can hear them.
	 */
	static UIGToneSequenceSoundWave* CreateCrtChannelSwitch(
		UObject* Outer,
		bool bCollapse);

	/**
	 * 모니터 험 (§8 비트 2-2) — what a live tube sounds like when nobody is
	 * speaking: 60 Hz mains and its harmonics, a breath of hiss, the line whine.
	 * Very quiet. Playing only while channel 5 is up is deliberate — the 4분할
	 * monitor has been silent all night, so the hum arriving *with* the picture is
	 * the ear's confirmation that this input was never on before.
	 *
	 * Sized to the beat rather than looped, because a note envelope in this synth
	 * always returns to zero at the loop point and a hum that pulses once a second
	 * is worse than no hum. TotalSeconds should span the live window *and* the
	 * collapse, so the tube's slow dim ends underneath the tearing noise.
	 */
	static UIGToneSequenceSoundWave* CreateCrtChannelBed(
		UObject* Outer,
		float TotalSeconds);

	/**
	 * 배관 수류, 원근 4단 (§21.3) — the riser running behind the finished wall.
	 *
	 * DistanceStep 0..3 is how much building the water had to come through:
	 * bandwidths 5000 / 2400 / 1100 / 480 Hz. Structure is a low-pass filter, so
	 * the step *is* the distance, and the player reads it without being told.
	 * Close water still has audible ticks; far water is only a hum. Looping.
	 */
	static UIGToneSequenceSoundWave* CreatePipeWaterFlow(
		UObject* Outer,
		int32 DistanceStep);

	/**
	 * 옥상 물탱크의 출렁임 — 강판 안에서 2톤이 아주 느리게 오간다.
	 *
	 * 배관 수류와 다른 소리다. 관 속의 물은 계속 흐르지만 탱크의 물은
	 * 밀렸다가 돌아오며, 돌아오는 끝에서 벽을 한 번 친다. 주기가 4초를
	 * 넘는 것이 요점이다 — 이 느림이 「가득 차 있다」는 뜻이고, 그것이
	 * §13의 「물 2톤 옆의 갈증」을 만든다. Looping.
	 */
	static UIGToneSequenceSoundWave* CreateRooftopTankSlosh(UObject* Outer);

	/**
	 * The answer P3 is actually asking for: 속이 찬 벽은 짧게 죽고, 빈 벽은
	 * 길게 운다.
	 *
	 * A cavity wall is two leaves with an air spring between them, and that
	 * mass-air-mass system resonates — measured near 100–110 Hz in real stud
	 * walls. Driven by the riser it rings on. A solid wall has no air spring, so
	 * the same excitation dies almost immediately. This one difference is the
	 * whole puzzle, and it has to be audible: the thought bubble must confirm
	 * what the player already heard, never replace it.
	 */
	static UIGToneSequenceSoundWave* CreateWallCavityResponse(
		UObject* Outer,
		bool bHollow);

	/**
	 * 밸브 개방 (§21.3) — 1.8 kHz metal ringing plus a 1.2 s ramp of water
	 * starting to move, over 2.00 s. ValveIndex 0..2 picks one of the three
	 * authored wheels (§10.3); larger wheels ring lower and fill slower.
	 */
	static UIGToneSequenceSoundWave* CreateValveOpen(
		UObject* Outer,
		int32 ValveIndex);

	/**
	 * 망치 임팩트 (§21.3) — a 90 Hz impulse, gypsum fracture, and 1.4 s of the
	 * building answering, over 1.60 s. StrikeIndex escalates the fracture
	 * through the §10.3 three stages: the first blows bruise the board, the
	 * later ones break through it, and the sound has to say which.
	 */
	static UIGToneSequenceSoundWave* CreateHammerImpact(
		UObject* Outer,
		int32 StrikeIndex);

	/**
	 * 망치 녹음에 얹는 석고 파쇄 층. 녹음은 머리가 판에 닿는 0.2초뿐이라
	 * 다섯 번이 다 같다. 그 위에 단계만 싣는다 — 1타는 멍, 2~4타는 찢기며
	 * 속이 빈 105Hz 울림, 5타는 판이 통째로 내려앉는 저역과 파편, 가루.
	 * 머리의 저역과 접촉음은 녹음에 있으므로 넣지 않는다. 독립 큐가 아니라
	 * 망치 임팩트의 한 층이다(§21.3.1).
	 */
	static UIGToneSequenceSoundWave* CreateHammerFractureLayer(
		UObject* Outer,
		int32 StrikeIndex);

	/**
	 * 풀이 묻은 벽지를 누르는 고무 이음 롤러. 긴 상하 왕복 두 번과 젖은
	 * 종이 표면, 방향 전환 때의 작은 축 소리를 합성하며 엔딩 C에서만 쓴다.
	 */
	static UIGToneSequenceSoundWave* CreateWallpaperSeamRoller(UObject* Outer);

	/**
	 * 프로타주 문지름 (§21.3) — graphite laid flat and dragged over the carbon
	 * ledger until the pressed letters come up. Band noise 900~4200 Hz, looping
	 * for as long as the hold lasts.
	 *
	 * §5.1 rates this a sustained 0.25, three times a footstep, and until now it
	 * made no sound at all: the player rubbed for 1.2 s in silence while the
	 * noise bus told the one upstairs exactly where they were. A cost the player
	 * cannot hear is not a cost they can choose.
	 *
	 * The design says 입력 속도 연동. The shipped interaction is a hold rather
	 * than a rubbing gesture, so the hold's own progress drives the intensity —
	 * the stroke gets more insistent as the date surfaces.
	 */
	static UIGToneSequenceSoundWave* CreateFrottageRub(UObject* Outer);

	/**
	 * 심박 소음화 (§21.3, §5.2) — 자기 몸이 배신하는 소리.
	 *
	 * Past stress 0.85 the pulse stops being something the player hears in their
	 * head and becomes a sound in the room, audible to him within three meters
	 * (§4.3-5). That transition already reported to the noise bus, but it sounded
	 * identical, so the most dangerous state in the game had no tell.
	 *
	 * This is the ordinary lub-dub on the same beat, 6 dB down with the crisp
	 * upper partial gone — the 220 Hz low pass of §21.3, realised by dropping the
	 * partial rather than filtering, because this synth is additive. Played
	 * spatially so §10.4 lets the corridor answer it.
	 */
	static UIGToneSequenceSoundWave* CreateAudibleHeartbeat(
		UObject* Outer,
		float Loudness);

	// --- 없는 층: 유담의 몸 (§21.1 BUS_PLAYER의 「호흡」) --------------------
	//
	// 심박은 있었는데 숨이 없었다. §21.4가 침묵 구간에 「플레이어 호흡만 남긴다」고
	// 적어 둔 그 호흡이다. 녹음(Player_Breath_Scared)이 있으면 그쪽이 먼저고
	// 아래는 폴백이다. 소리를 내는 자리는 UIGStressComponent 하나다.

	/**
	 * 겁먹은 숨의 루프. 4.2초에 숨 둘 — 뒤의 것이 앞의 것보다 급하다. 볼륨은
	 * 스트레스와 숨찬 정도로 오르고, 숨을 참는 동안은 0이다.
	 */
	static UIGToneSequenceSoundWave* CreatePlayerBreathLoop(UObject* Outer);
	/** 놀라서 들이켜는 숨. 참았던 숨을 놓을 때도 이것이다. 0.45초. */
	static UIGToneSequenceSoundWave* CreatePlayerGasp(UObject* Outer);
	/** 추격이 끝났을 때, 아침이 왔을 때 떨리며 내쉬는 숨. 1초. 안도이지 회복은 아니다. */
	static UIGToneSequenceSoundWave* CreatePlayerExhale(UObject* Outer);
	/**
	 * 메뉴 칸을 옮기고 고르는 소리. 나무를 손톱으로 톡 — 고를 때는 둘. 밤의
	 * 세계와 같은 재질이라 화면이 게임 밖으로 튀지 않는다. UI 버스.
	 */
	static UIGToneSequenceSoundWave* CreateMenuTick(UObject* Outer, bool bConfirm);

	// --- 없는 층: 소리로 된 선택적 목격 (§22.3) ---------------------------
	//
	// 이 게임에서 목격은 집어 드는 물건일 필요가 없다. 아래 셋은 귀로만
	// 확인되며, 어느 것도 진실을 열지 않는다.

	/**
	 * 402호 문 너머 — 비어 있는 세대의 공기.
	 *
	 * **없는 것이 내용이다.** 403호의 룸톤에는 늘 냉장고 메인즈 험이
	 * 깔려 있는데 여기에는 그 60·120Hz가 아예 없다. 게임 내내 그 험을
	 * 듣고 산 플레이어만 이 구멍을 알아챈다 — 전기가 끊긴 집이라는 뜻이고,
	 * 두 세대가 정말로 나갔다는 뜻이다.
	 */
	static UIGToneSequenceSoundWave* CreateVacantUnitTone(UObject* Outer);

	/**
	 * 옥상 철문 안쪽 — 실제 바람.
	 *
	 * 목한수는 새벽 소음을 「물탱크 바람 소리」라고 했다(§13). 이 큐의
	 * 쓸모는 반증이다: 바람은 느리게 부풀고 느리게 죽으며, 박자가 없다.
	 * 둘-쉬고-하나를 들어 온 귀에는 같은 소리로 들릴 수가 없다.
	 */
	static UIGToneSequenceSoundWave* CreateRoofDoorGust(UObject* Outer);

	/**
	 * 관리실 안쪽 방 — 방음재 너머로 새는 기계 험.
	 *
	 * 고역이 계란판에 다 먹혀 92Hz와 그 배음만 남는다. 사람이 안 쓰는
	 * 창고에서는 아무것도 돌지 않는다 — §9에서 그가 발견되는 방이다.
	 */
	static UIGToneSequenceSoundWave* CreateFoamedRoomHum(UObject* Outer);

	/**
	 * 험 존이 서 있는 자리에서 도는 기계 소리.
	 *
	 * 냉장고·배전반·보일러가 같은 소리로 운다. 셋이 반경과 마스킹을 같이
	 * 쓰는 것과 같은 이유다 — 기계마다 다르게 울면 규칙 하나를 배우는 데
	 * 소리 셋을 외워야 한다.
	 */
	static UIGToneSequenceSoundWave* CreateMachineHumLoop(UObject* Outer);

	// --- 없는 층: 엔딩 에필로그 (§9) ---------------------------------------

	/**
	 * 폴리스라인 테이프가 롤에서 당겨져 풀리는 소리. 몽타주의 첫 소리이며,
	 * 이 장면에서 처음으로 유담이 아닌 사람들이 건물에 들어온다.
	 */
	static UIGToneSequenceSoundWave* CreatePoliceLineTapePull(UObject* Outer);

	/** 들것 바퀴가 복도 타일 이음매를 넘어간다. 네 번, 점점 멀어진다. */
	static UIGToneSequenceSoundWave* CreateGurneyWheels(UObject* Outer);

	/** 현장 사진 셔터 세 번. 미러 슬랩과 얇은 모터 감김. */
	static UIGToneSequenceSoundWave* CreateCameraShutterTriple(UObject* Outer);

	/** 빗자루가 석고 조각을 쓸어 모은다. 마른 알갱이가 앞으로 밀리는 소리. */
	static UIGToneSequenceSoundWave* CreateDebrisSweep(UObject* Outer);

	/**
	 * 에필로그 1의 스코어. M-조율이 이 작품에서 유일하게 끝까지 간다.
	 *
	 * -30센트에서 시작해 여덟 타건에 걸쳐 220 Hz 정음으로 올라오고,
	 * 마지막에 열린 5도(A-E)를 한 번 누른 뒤 놓는다. 게임 내내 닿지
	 * 못하던 음이 여기서만 닿는 것이 §10.1의 계약이다.
	 */
	static UIGToneSequenceSoundWave* CreateEpilogueWorkshopScore(UObject* Outer);

	/**
	 * 에필로그 2의 베드. 크레인 유압의 아주 먼 저역과, 401호 창턱
	 * 라디오에서 새어 나오는 대역 제한 신호. 말은 만들지 않는다.
	 */
	static UIGToneSequenceSoundWave* CreateEpilogueAutumnBed(UObject* Outer);

	/** 열쇠 두 개가 중개사 반납함 철판 바닥에 떨어진다. */
	static UIGToneSequenceSoundWave* CreateKeyDropMetalBox(UObject* Outer);

	/**
	 * 계단 난간을 두 번. 응답 노크와 같은 손이지만 벽이 아니라 강관이라
	 * 저역 대신 금속 배음이 남는다. 이 게임의 마지막 입력의 소리다.
	 */
	static UIGToneSequenceSoundWave* CreateRailingKnockTwo(UObject* Outer);

	/**
	 * §5.5 기록되지 않는 시간 — a phone take played back through its own speaker.
	 *
	 * Built from the recording log rather than captured audio (§14), which is
	 * what makes the silences exact: a suppressed event contributes no notes at
	 * all and its duration simply passes. **노크가 있던 자리에 정확히 그 길이만큼의
	 * 무음.** Nothing is faded or crossfaded over the gap; an edit would be a
	 * different and much weaker idea than an absence.
	 *
	 * Everything sits above 400 Hz. A phone speaker has no low end, so the take
	 * is audibly a recording — and the knock, which lives at 58~80 Hz, could not
	 * have survived it even if the rule had let it through.
	 */
	static UIGToneSequenceSoundWave* CreateRecordingPlayback(
		UObject* Outer,
		const TArray<struct FIGRecordedSound>& Sounds);

private:
	friend class AIGAudioPresentationProbe;
	/** 상태가 있는 파형(BandNoise·Pluck)의 음별 작업 기억. 렌더 스레드만 만진다. */
	struct FNoteRenderState
	{
		float FilterLow = 0.0f;
		float FilterBand = 0.0f;
		TArray<float> Delay;
		int32 DelayIndex = 0;
		double LastNoteTime = -1.0;
		uint32 NoiseState = 0u;
	};

	static float EvaluateWaveform(EIGToneWaveform Waveform, float FrequencyHz, double NoteTimeSeconds);
	float EvaluateStatefulWaveform(
		const FIGToneNote& Note, FNoteRenderState& State, double NoteTimeSeconds, int32 NoteIndex);
	static float EvaluateEnvelope(const FIGToneNote& Note, float NoteProgress01);

	// Immutable after ConfigureNotes; read from the audio render thread.
	TArray<FIGToneNote> Notes;
	int64 LoopSampleCount = 0;
	int64 TotalSampleCount = 0;
	float PitchWowDepthRatio = 0.0f;
	float PitchWowRateHz = 0.0f;
	int32 TailDelaySamples = 0;
	float TailFeedback = 0.0f;
	float TailDamping = 1.0f;
	float TailMix = 0.0f;

	// Render-thread-owned sample cursor and working memory.
	uint64 GeneratedSampleCount = 0;
	TArray<FNoteRenderState> NoteStates;
	TArray<float> TailBuffer;
	int32 TailIndex = 0;
	float TailLow = 0.0f;
};
