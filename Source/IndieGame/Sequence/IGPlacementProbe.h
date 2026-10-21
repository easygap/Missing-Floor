#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sequence/IGPlacementAudit.h"
#include "IGPlacementProbe.generated.h"

class ACameraActor;
class UPointLightComponent;
class AIGNightPhaseDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;

/**
 * -IGPlacementProbe 전용. 낮과 밤1·밤2·밤3·밤4의 첫 상태에서 월드 전체 소품의 지지면·관통·광원을 잰다.
 * -IGPlacementShots면 걸린 물건마다 두 방향에서 찍고(밤에는 카메라 작업등을 켠다), 구역마다 네 방향 개관도 찍는다.
 * 창과 소리 없이 돌리는 실행에서만 쓴다(Scripts/Run-PlacementAudit.ps1).
 */
UCLASS()
class INDIEGAME_API AIGPlacementProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGPlacementProbe();
	void Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer);
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FShot
	{
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FString Name;
		/** 밤 장면이라 카메라 작업등을 켜고 찍는다. */
		bool bFill = false;
	};

	void Audit(const TCHAR* Label);
	void QueueFlagShots(const FString& Label, const IGPlacementAudit::FResult& Result);
	/** 물건 하나(걸린 것이거나 고정 촬영)를 가장 트인 두 방향에서 찍게 줄 세운다. */
	void QueueFlagShots(const FString& Label, const IGPlacementAudit::FFlag& Flag, int32 Index);
	void QueueStationShots(const FString& Label, const TCHAR* Only = nullptr);
	bool QueueView(const FString& Name, const FVector& Target, float Yaw, float Pitch, float Distance, float StartOffset = 0.0f);
	/** 목표에서 시점 쪽으로 물러설 수 있는 거리(앞을 막는 면에서 15 cm 앞). StartOffset만큼 떨어진 데서 재기 시작한다. */
	float ViewReach(const FVector& Target, float Yaw, float Pitch, float Distance, float StartOffset = 0.0f,
		FString* OutBlocker = nullptr) const;
	bool TickShots(float DeltaSeconds);
	bool BeginNight(int32 Night);
	void Silence();
	void Next(int32 NewPhase);
	void Finish();

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Fill;
	TArray<FShot> Shots;
	TSet<FString> ShotKeys;
	int32 ShotIndex = 0;
	int32 ShotFileIndex = 0;
	float ShotWait = -1.0f;
	bool bShotRequested = false;
	bool bShots = false;
	int32 Phase = 0;
	float PhaseSeconds = 0.0f;
	int32 Floating = 0;
	int32 Penetrations = 0;
	int32 LightFlags = 0;
	int32 Audits = 0;
	bool bMeasured = true;
};
