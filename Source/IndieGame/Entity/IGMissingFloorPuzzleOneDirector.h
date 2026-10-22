#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Narrative/IGMissingFloorNarrativeTypes.h"
#include "IGMissingFloorPuzzleOneDirector.generated.h"

class AIGMissingFloorEvidence;
class AIGPrologueWorldScene;
class AIGReadableNote;
class UAudioComponent;
class UIGMissingFloorNarrativeSubsystem;

DECLARE_MULTICAST_DELEGATE(FIGPuzzleOneSolvedSignature);

/**
 * P1 「다섯 번째 바늘」. 공용 조명을 끄고 무명 회로의 전원을 바꾸며
 * 계량기의 멈춤과 회전을 비교한다. 검침표까지 대조하면 숨은 회로를
 * 확인한다. 전원을 올린 상태로 조사를 끝내야 위층의 안정기 소리가 이어진다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGMissingFloorPuzzleOneDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGMissingFloorPuzzleOneDirector();
	AIGMissingFloorEvidence* GetMeterAction() const { return MeterDialEvidence; }
	AIGMissingFloorEvidence* GetBreakerAction() const { return BreakerAction; }
	AIGMissingFloorEvidence* GetCommonLightAction() const { return CommonLightAction; }
	/** 검사용. 이름표 없는 회로가 올라가 있고 안정기 소리가 나는 중인가. */
	bool IsBreakerThrownForTesting() const { return bBreakerThrown; }
	bool IsBallastHumAudibleForTesting() const { return bBallastHumAudible; }

	/** Spawns the puzzle's interactables against an already-built lobby. */
	bool Configure(AIGPrologueWorldScene* InScene);

	/**
	 * 낮에는 계전기가 곧장 되돌려서 아무 일도 없다(§7 P1). 위가 켜지는
	 * 것은 그 시간에만이다.
	 */
	void SetHourActive(bool bActive);

	/** 계량기·공용 스위치·무명 회로·검침표가 모두 배치됐는지 확인한다. */
	bool ValidateFixtures() const;

	/**
	 * §8 1-6. 밤1이 맞물린 뒤 천장 너머에서 안정기가 켜지는 순간. 딸깍 둘과
	 * 짧은 웅이 로비까지 내려온다 — 4층 천장 위의 험은 반경이 좁아 로비에서
	 * 한 번도 들리지 않았다. 회로가 올라가 있을 때만 난다.
	 */
	void PlayBallastFromAbove();

	/**
	 * 회로가 올라가고 T1까지 맞물렸을 때 한 번. 불만 켜고 계량기를 안 본
	 * 밤은 끝나지 않는다 — 퍼즐은 두 기록의 교차지 버튼이 아니다(§7).
	 */
	FIGPuzzleOneSolvedSignature OnSolved;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void UpdateMeterMotion();
	void AccumulateMeterMotion();
	void AdvanceMeterDisc();
	FTimerHandle MeterRotationTimer;
	FTimerHandle DaytimeTripTimer;
	double LastMeterUpdateTime = -1.0;
	float MeterUpdateInterval = 0.f;
	void ResetDaytimeBreaker();
	void HandleMeterExamined(AIGMissingFloorEvidence* Evidence);
	void HandleBreakerThrown(AIGMissingFloorEvidence* Evidence);
	void HandleCommonLighting(AIGMissingFloorEvidence* Evidence);

	/** Dynamic delegate target, so it has to be reflected. */
	UFUNCTION()
	void HandleSheetRead(AIGReadableNote* Note, bool bOpened);
	void CreateBallastHum();
	/** 안정기가 무는 딸깍 한 번. 켜지는 순간이라 로비까지 들린다. */
	void PlayBallastTick(float Volume, float Pitch);
	void HandleTruthConfirmed(EIGMissingFloorTruth Truth);
	void AnnounceSolvedIfReady();
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<AIGPrologueWorldScene> Scene;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> MeterDialEvidence;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> BreakerAction;
	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> CommonLightAction;

	UPROPERTY(Transient)
	TObjectPtr<AIGReadableNote> ReadingSheet;

	/** The hum from above the ceiling. Created silent, started on the throw. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BallastHum;

	/** 켜지는 순간의 두 번째 딸깍과, 로비까지 내려오는 짧은 웅. 새벽에 걷힌다. */
	FTimerHandle BallastCueTimer;
	TWeakObjectPtr<UAudioComponent> BallastSwell;

	bool bBreakerThrown = false;
	bool bCommonLightsEnabled = true;
	bool bBallastHumAudible = false;
	bool bHourActive = false;
	bool bSolvedAnnounced = false;
	FDelegateHandle TruthHandle;
};
