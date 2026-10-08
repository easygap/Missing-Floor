#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGNightThreatDirector.generated.h"

class AIGBathroomRefuge;
class AIGListenerEntity;
class AIGMissingFloorEvidence;
class AIGNightLoopDirector;
class AIGNightPhaseDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGShadowFigure;
class AIGStairSensorLights;
class AIGSwingDoor;
class UAudioComponent;
class UStaticMeshComponent;
class UIGMissingFloorNarrativeSubsystem;
struct FIGNoiseEvent;

/** 문 밖의 손님이 어디까지 왔는가. */
UENUM()
enum class EIGGuestStage : uint8
{
	Idle,
	/** 밤3. 403호에 오기 전에 401호와 402호 문을 차례로 두드린다. */
	Canvassing,
	/** 두드리고 말을 건다. 둘-쉬고-하나로 대답하면 물러간다. */
	Knocking,
	/** 밖에서 도어락 번호를 누르거나(403호) 열쇠를 꽂는다(5층). 걸쇠나 빗장을 걸 마지막 틈이다. */
	Keypad,
	/** 밤4. 5층 철문이 열려 있어 문간에 섰다. 닫지 않으면 들어온다. */
	AtOpenDoor,
	/** 문이 걸쇠나 빗장에 걸렸다. 한 번 더 말하고 발소리 없이 간다. */
	CaughtOnLatch,
	/** 문이 열렸다. 몸이 들어와 찾는다. */
	Inside,
	/** 이번 밤에는 끝났다. */
	Spent
};

/** 손님이 찾아온 문. */
UENUM()
enum class EIGGuestDoor : uint8
{
	/** 403호 현관. 도어락과 걸쇠가 있고, 밤2 비트를 겪었으면 문구멍으로 내다본다. */
	Home,
	/** 옥상에서 5층으로 들어가는 철문. 열쇠로 열고 안쪽에 빗장이 있다. 밤4에만 온다. */
	Annex
};

/**
 * 위층 사람 말고 밤에 오는 것 둘(EXPANSION_PLAN §3).
 *
 * 어둑시니 — 손전등을 끈 채 어둠에 오래 있으면 등 뒤나 옆의 어둠이 뭉쳐 선다.
 * 쳐다볼수록 커지고 다가오며, 손전등을 비추면 두 배로 큰다. 3초 동안 눈을
 * 돌리거나 불이 켜진 곳에 서면 사라진다. 다 자란 채 2.2 m 안에 들면 삼킨다.
 *
 * 손님 — 밤2부터 한 밤에 한 번, 403호에 오래 있으면 현관을 두드리고 아는
 * 사람의 목소리로 문을 열어 달라고 한다. 대답하지 않으면 도어락을 누른다.
 * 걸쇠를 걸었으면 문이 한 뼘에서 걸리고, 아니면 들어와 찾는다. 오빠의 노크
 * (둘-쉬고-하나)로 대답하면 두 번만 두드리고 간다. 밤3에는 403호에 오기 전에
 * 401호와 402호 문을 먼저 두드린다. 그사이 403호 문을 열면 그리로 온다. 밤4에는
 * 그녀가 5층에 오래 있으면 5층 철문으로 와서 도어락 대신 열쇠를 꽂는다. 안쪽
 * 빗장을 걸었으면 걸리고, 문이 열려 있으면 문간에 서서 들어가도 되느냐고 묻는다.
 *
 * 긴장 예산(§7): 위층 사람이 쫓는 동안, 잡혔다 깬 뒤 40초 동안은 둘 다 오지
 * 않는다. 한 번에 하나만 나온다. 틱 없이 0.1초 타이머로 돈다.
 */
UCLASS()
class INDIEGAME_API AIGNightThreatDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGNightThreatDirector();

	void Configure(
		AIGPrologueWorldScene* InScene,
		AIGPlayerCharacter* InPlayer,
		AIGListenerEntity* InListener,
		AIGNightLoopDirector* InNightLoop,
		AIGNightPhaseDirector* InNightPhase);

	/** 플레이어가 문을 두드렸다. 손님이 찾아온 문이면 대답한 것인지 본다. */
	void RegisterPlayerDoorKnock(const AActor* KnockedActor);

	/** 밤4에 손님이 찾아갈 5층 철문. 감독이 밤3 디렉터의 문을 넘긴다. */
	void SetAnnexDoor(AIGSwingDoor* InDoor);
	/**
	 * 밤4 망치질 사이(EXPANSION_PLAN §2.3 넷째 밤). 넷째 타 뒤에 밤4 디렉터가 부른다. 5층
	 * 철문 밖에서 오빠 목소리가 문을 열어 달라고 한다. 그 뒤는 여느 손님과 같아서, 빗장을
	 * 안 걸었으면 열쇠로 열고 들어온다. 벽이 열리면 그 자리에서 그친다. 한 번의 밤에
	 * 한 번이고, 다른 손님이나 어둑시니가 나와 있으면 거둔다. 시작했으면 참이다.
	 */
	bool BeginHammerCall();
	/** 망치질 사이의 부름이 문 앞에 와 있다. */
	bool IsHammerCallActive() const { return bHammerCall; }
	EIGGuestDoor GetGuestDoor() const { return GuestDoor; }
	/** 손님이 지금 두드리는 문 앞, 손 높이. 손님이 없으면 영벡터다. */
	FVector GetGuestKnockLocation() const;
	/** 손님의 몸이 서 있으면 그 자리(바닥). 검사가 읽는다. */
	bool GetGuestBodyLocation(FVector& OutLocation) const;

	bool IsEoduksiniManifested() const;
	/** 첫째 밤 3층 참에서 처음 선 어둑시니는 제자리에서 커지기만 한다. */
	bool IsEoduksiniPinned() const { return bEoduksiniPinned; }
	FVector GetEoduksiniLocation() const;
	EIGGuestStage GetGuestStage() const { return GuestStage; }
	float GetDarknessSeconds() const { return DarknessSeconds; }

	/** 첫째 밤 3층 참의 센서등이 꺼지며 어둑시니를 처음 보여 준 비트. 한 판에 한 번. */
	static const FName SensorIntroBeat;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Update();
	bool IsQuietWindow() const;
	/**
	 * 밤4, 물소리로 망치를 덮은 채 차단기가 내려간 뒤 벽이 열리기 전. 다른 때의 가면 구간은
	 * 결말 C가 혼자 맡아 아무것도 오지 않지만, 정전 뒤에는 어둑시니가 상시로 서고 철문 밖의
	 * 부름이 온다(§7 밤별 긴장 예산).
	 */
	bool IsHammerPhase() const;
	/** 추격 없음(듣기만 하는 밤)이면 거짓이다. 그때는 아무도 잡지 않고 놀래기만 한다. */
	bool IsCaptureAllowed() const;
	FVector GetPlayerEye() const;
	/** 무엇이 보이는지 재는 자리. 숨어 있으면 가구 앞면 바로 바깥이다. */
	FVector GetPlayerSightOrigin() const;
	/** 몸이 선 바닥 높이. 숨어서 눈이 낮아져도 그대로다. */
	float GetPlayerFeetZ() const;
	bool IsPlayerLookingAt(const FVector& Target, float HalfAngleDegrees, const AActor* Ignored) const;
	bool IsTorchOn(const FVector& Target) const;
	bool HasLineOfSight(const FVector& From, const FVector& To, const AActor* Ignored) const;
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;
	/** 한 판에 한 번 하는 속말. 서사 기록에 남겨 이어하기에도 되풀이하지 않는다. */
	void ThinkOnce(const TCHAR* BeatId, const FText& Thought, float DelaySeconds = 0.0f);

	// 어둑시니
	void UpdateEoduksini(float DeltaSeconds);
	bool TryManifestEoduksini();
	bool FindManifestSpot(FVector& OutLocation) const;
	bool IsValidManifestSpot(const FVector& Candidate, FVector& OutFloor) const;
	void DismissEoduksini(bool bSilently);
	float GetDarknessThresholdSeconds() const;
	/** 자리를 받아 어둑시니를 세운다. 들숨과 자막, 처음 볼 때의 속말이 같이 온다. */
	void ManifestEoduksiniAt(const FVector& Spot);

	// 계단 센서등
	/** 켜진 등의 불빛 안에 선 어둑시니는 사라진다. 첫째 밤 3층 참의 등이 꺼지면 처음 선다. */
	void HandleSensorLamp(int32 LampIndex, bool bOn);
	/** 첫째 밤, 그녀가 3층 참에 서 있으면 그 등이 멈춘 지 몇 초 만에 꺼지게 걸어 둔다. */
	void UpdateSensorIntro();
	bool CanRunSensorIntro() const;
	/** 3층 참의 등이 꺼진 뒤, 그녀가 계단 아래를 보지 않는 틈에 아래 어둠에 세운다. */
	bool TryManifestSensorIntro();
	bool IsNightOneSightingRunning() const;

	// 손님
	void UpdateGuest(float DeltaSeconds);
	/** 찾아갈 문이 있으면 참이다. 403호가 먼저고, 밤4에는 5층 철문도 본다. */
	bool CanStartGuest(EIGGuestDoor& OutDoor) const;
	void StartGuest(EIGGuestDoor Door);
	/** 밤3, 401호와 402호를 두드리는 동안. 403호 문이 열리면 몸이 그리로 온다. */
	void UpdateCanvass(AIGPlayerCharacter& Player);
	/** 열린 5층 철문간에 서 있는 동안. 닫으면 밖에서 두드리기로 넘어간다. */
	void UpdateAtOpenDoor(AIGPlayerCharacter& Player);
	/** 지금 두드리는 문 앞에서 Count번 친다. 자막은 문에 따라 다르다. */
	void GuestKnock(int32 Count, float Volume);
	void GuestSpeak(int32 LineIndex);
	/** 밤3, 401호 문 앞에서 하는 말. 403호 사람 목소리다. */
	void GuestSpeakAtNeighbor();
	/** 도어락(403호)이나 열쇠(5층) 소리 한 번. Step 0 누름·꽂음, 1 틀림, 2 다시, 3 풀림. */
	void PlayGuestLockStep(int32 Step);
	void OpenDoorForGuest();
	void BeginGuestCapture();
	void EndGuest(bool bCloseDoor);
	/**
	 * 그녀가 욕실 안이다. 손님은 욕실 문을 거쳐서만 간다. 잠그지 않은 문은 열고, 잠긴 문은
	 * 손잡이만 돌려 보고 돌아간다. 이번 틱을 맡았으면 참이다.
	 */
	bool UpdateGuestAtRoomDoor(const AIGBathroomRefuge& Room, AIGShadowFigure& Body, const FVector& BodyLocation);
	/** 문구멍을 손님에게 빌려 쓴다(밤2 비트가 끝난 뒤에만). */
	void SetPeepholeOffered(bool bOffered);
	void HandlePeepholeExamined(AIGMissingFloorEvidence* Evidence);
	/** 두드리는 동안 문 밑으로 종이 한 장을 밀어 넣는다. 세 번에 나눠 민다. */
	void BeginPaperUnderDoor();
	void UpdatePaperUnderDoor(float DeltaSeconds);
	/** 들어와 돌아다니는 동안 몸에서 종이가 한 장씩 떨어진다. */
	void DropGuestPaper(const FVector& BodyLocation);
	/** 문 밑 전단과 떨어진 종이를 거둔다. 새벽과 새 방문 때 부른다. */
	void HideGuestPapers();
	bool IsPlayerInHome() const;
	/** 그녀가 5층 증축부 안에 있다. */
	bool IsPlayerInAnnex() const;
	/** 밤4, 5층 철문으로 갈 수 있다. 벽을 치기 전이고 그녀가 거기 오래 있었다. */
	bool CanVisitAnnex() const;
	/** 지금 손님이 찾아온 문. */
	AIGSwingDoor* GetGuestDoorActor() const;
	/** 손님이 찾아온 문 바깥쪽, 노크하는 손 높이. */
	FVector GetDoorOutside() const;
	/** 문이 열리면 몸이 서는 문간 바깥 자리(바닥). */
	FVector GetDoorThreshold() const;
	/** 들어와 둘러보는 방 가운데(바닥). */
	FVector GetGuestRoomCenter() const;
	/** 401호(0)와 402호(1) 문 앞, 노크하는 손 높이. */
	static FVector GetNeighborDoorOutside(int32 Stop);
	void HandleNoise(const FIGNoiseEvent& Event);

	UPROPERTY(Transient)
	TObjectPtr<AIGShadowFigure> Eoduksini;

	UPROPERTY(Transient)
	TObjectPtr<AIGShadowFigure> Guest;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> EoduksiniBreath;

	/** 문 밑으로 밀려 들어온 임대 안내문. 손님이 가도 새벽까지 바닥에 남는다. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PaperUnderDoor;

	/** 손님이 지나간 자리에 떨어진 종이. 밤마다 다시 쓴다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FallenPapers;

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> PlayerPawn;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGNightLoopDirector> NightLoop;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	TWeakObjectPtr<AIGSwingDoor> HomeDoor;
	TWeakObjectPtr<AIGSwingDoor> AnnexDoor;
	TWeakObjectPtr<AIGStairSensorLights> SensorLights;
	FDelegateHandle SensorLampHandle;
	TWeakObjectPtr<AIGMissingFloorEvidence> Peephole;
	FDelegateHandle PeepholeHandle;

	FTimerHandle UpdateTimer;
	FDelegateHandle NoiseHandle;
	double LastUpdateSeconds = -1.0;

	// 어둑시니
	float DarknessSeconds = 0.0f;
	float EoduksiniGrowth = 0.0f;
	float EoduksiniUnseenSeconds = 0.0f;
	double EoduksiniCooldownUntil = 0.0;
	double EoduksiniCaptureSeconds = -1.0;
	/** 덮쳐 왔지만 잡지 않는 중이다(추격 없음). 리셋을 기다리지 않고 1.4초 뒤에 흩어진다. */
	bool bEoduksiniHarmlessLunge = false;
	bool bEoduksiniWarned = false;
	/** 계단 아래에 선 어둑시니는 다가오지 않는다. 다가오면 수평으로 움직여 단을 뚫는다. */
	bool bEoduksiniPinned = false;
	/** 어둑시니가 선 시각. 방 밝기는 늦게 따라오므로 막 선 몸은 그것으로 지우지 않는다. */
	double EoduksiniManifestedAt = -1.0;
	/** 3층 참의 등이 꺼진 뒤 설 자리를 찾는 마감. 0보다 작으면 기다리지 않는다. */
	double SensorIntroDeadline = -1.0;

	// 손님
	EIGGuestStage GuestStage = EIGGuestStage::Idle;
	EIGGuestDoor GuestDoor = EIGGuestDoor::Home;
	int32 GuestNight = 0;
	float GuestElapsed = 0.0f;
	float HomeDwellSeconds = 0.0f;
	float AnnexDwellSeconds = 0.0f;
	/** 밤3에 두드리는 이웃 문. 0이 401호, 1이 402호다. */
	int32 CanvassStop = 0;
	/** 401·402호를 두드리는 사이 403호 문이 열렸다. 몸이 그 문으로 오는 중이다. */
	bool bCanvassLured = false;
	int32 GuestStep = 0;
	TArray<double> PlayerDoorKnockTimes;
	bool bGuestAnswered = false;
	bool bPlayerHiddenWhenOpened = false;
	bool bGuestHeardPlayer = false;
	/** 잠긴 욕실 손잡이를 돌려 봤다. 그 뒤로 몇 초 서 있다가 간다. */
	bool bGuestRattledRoom = false;
	float GuestRoomWaitStart = 0.0f;
	bool bGuestCapturing = false;
	double GuestCaptureSeconds = -1.0;
	/** 지금 손님이 망치질 사이의 부름이다. 첫 말이 계획서의 그 말이다. */
	bool bHammerCall = false;
	/** 이번 밤에 부름을 썼다. 침대로 돌아가도 다시 오지 않고, 그 시간이 다시 시작되면 푼다. */
	bool bHammerCallUsed = false;
	float GuestRustleSeconds = 0.0f;
	FVector LastGuestLocation = FVector::ZeroVector;
	/** 밀기 시작한 뒤 흐른 시간. 음수면 움직이지 않는다. */
	float PaperUnderDoorElapsed = -1.0f;
	int32 PaperUnderDoorShoves = 0;
	float PaperUnderDoorFloorZ = 0.0f;
	bool bPaperUnderDoorPlayed = false;
	int32 FallenPaperCount = 0;
	int32 GuestRustleCount = 0;
	/** 전단 메시를 못 읽어 큐브로 대신했다. 눕히는 방향과 두께가 다르다. */
	bool bPaperUnderDoorIsCube = false;
};
