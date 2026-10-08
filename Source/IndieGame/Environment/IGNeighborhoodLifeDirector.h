#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGNeighborhoodLifeDirector.generated.h"

class UAudioComponent;
class UBoxComponent;
class UMaterialInterface;
class UPointLightComponent;
class USceneComponent;
class USpotLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class APawn;

/**
 * Allocation-bounded life for a small Korean residential alley.
 *
 * 골목은 폭이 2.6 m이고 양 끝이 막혀 있다(서쪽 막다른 벽, 동쪽 편의점 정면).
 * 그래서 여기 들어오는 탈것은 뒤편 배송 골목에서 샛길로 빠져나오는 배달
 * 오토바이뿐이다. 오토바이는 사람을 통과하지 않는다. 비켜 갈 폭이 있으면
 * 비켜 가고, 없으면 급정거해 경적을 울리고 기다리며, 마지막 순간에 뛰어든
 * 몸은 어깨로 스쳐 밀어낸다.
 *
 * 첫 외출에는 동쪽 샛길에서 오토바이가 튀어나와 코앞을 스친 뒤 공동현관 옆
 * 연석에 선다(루이턴 버스). 202호 라이더 정우의 오토바이다. 그 뒤로는 낮과
 * 저녁 동안 거기 서 있고, 밤의 그 시간에는 없다.
 *
 * Runtime work is event driven: the actor ticks only for an active pass,
 * gust, leaf or cat trace, and scheduled callbacks skip when the player is
 * too far away.
 */
UCLASS(BlueprintType, NotBlueprintable, Transient)
class INDIEGAME_API AIGNeighborhoodLifeDirector final : public AActor
{
	GENERATED_BODY()

public:
	AIGNeighborhoodLifeDirector();

	/**
	 * Defines the road centre line and restarts the deterministic schedule.
	 * Call immediately after spawning. Coordinates are in world space.
	 */
	UFUNCTION(BlueprintCallable, Category = "Indie Game|Neighborhood")
	void ConfigureNeighborhood(
		FVector InRoadStart,
		FVector InRoadEnd,
		int32 InDeterministicSeed = 4040444);

	/**
	 * 첫 외출. 공동현관을 나선 그녀가 골목 동쪽으로 걸어가면 동쪽 샛길에서
	 * 오토바이가 튀어나온다. 한 판에 한 번이다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Indie Game|Neighborhood")
	void PrimeOutdoorSequence();

	/**
	 * 첫째·둘째 낮, 정우가 연석에 세운 오토바이에 앉아 콜을 기다린다. 그녀가 보지 않을 때
	 * 앉고 일어난다(기사가 탄 메시와 빈 메시를 바꿔 끼운다). 시동은 꺼져 있어 불은 없다.
	 */
	void SetParkedRiderPresent(bool bPresent);
	/** 정우가 오토바이에 앉아 있으면 그 오토바이 차체 중심(바닥)을 준다. */
	bool GetSeatedRiderLocation(FVector& OutLocation) const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 길 위의 한 점. 차체 중심이 이 점에서 옆으로 갈 수 있는 범위를 함께 든다. */
	struct FScooterPathPoint
	{
		FVector Location = FVector::ZeroVector;
		/** 진행 방향 오른쪽이 +. 차체 중심이 갈 수 있는 최소·최대 옆 거리(cm). */
		float MinOffset = -30.0f;
		float MaxOffset = 30.0f;
		/** 이 점까지 길을 따라 잰 거리(cm). */
		float Distance = 0.0f;
	};

	struct FScooterRuntime
	{
		bool bActive = false;
		/** 첫 외출의 루이턴 버스. 동쪽 샛길에서 나와 공동현관 옆 연석에 선다. */
		bool bNearMissPass = false;
		bool bParkAtEnd = false;
		bool bParked = false;
		TArray<FScooterPathPoint> Path;
		float Distance = 0.0f;
		float Speed = 0.0f;
		float CruiseSpeed = 900.0f;
		float LateralOffset = 0.0f;
		float LateralVelocity = 0.0f;
		float Lean = 0.0f;
		float PreviousYaw = 0.0f;
		float BlockedSeconds = 0.0f;
		float ParkedSeconds = 0.0f;
		float LastBrushTime = -100.0f;
		bool bScreechPlayed = false;
		bool bBlockHornPlayed = false;
		bool bBlockLinePlayed = false;
		bool bWarningHornPlayed = false;
		bool bPassedPlayer = false;
		bool bWasAheadOfPlayer = false;
		bool bBrushed = false;
		bool bEngineCut = false;
		/** 오래 막혀 있다가 여유 없이 비집고 지나가는 중. 그녀를 지나칠 때까지 유지한다. */
		bool bSqueezePass = false;
		float BasePitch = 1.0f;
		float BaseVolume = 1.0f;
		FVector Location = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
	};

	struct FLeafRuntime
	{
		bool bActive = false;
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Age = 0.0f;
		float Lifetime = 1.0f;
		float Phase = 0.0f;
	};

	struct FCatTraceRuntime
	{
		bool bActive = false;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		float Elapsed = 0.0f;
		float Duration = 1.0f;
	};

	void InitializePools();
	void RestartDeterministicSchedule();
	void StopAllRuntimeEvents();
	void UpdateRuntime(float DeltaSeconds);
	void RefreshRuntimeUpdates();

	void ScheduleNextScooter();
	void ScheduleNextGust();
	void ScheduleNextCatTrace();

	/** 샛길 하나로 들어와 골목을 지나 다른 샛길로 빠지는 평범한 배달. */
	void LaunchScooterThroughPass();
	/** 첫 외출의 루이턴 버스를 걸어 두고, 그녀가 골목 동쪽으로 들어서면 띄운다. */
	void PollNearMissTrigger();
	bool LaunchAlleyNearMiss();
	/** 첫 외출의 오토바이가 이미 지나갔는가. 이번 실행의 기억과 서사 기록을 함께 본다. */
	bool HasNearMissPlayed() const;
	void LaunchWindGust();
	void LaunchCatTrace();

	/**
	 * 꺾이는 점에 반지름을 주면 그 모서리를 원호로 깎아 길을 만든다. 모든 점에는
	 * 그 자리의 골목 폭과 장애물로 잰 옆 범위가 붙는다.
	 */
	void BuildScooterPath(
		const TArray<FVector>& Corners,
		const TArray<float>& CornerRadii,
		TArray<FScooterPathPoint>& OutPath) const;
	/** 이 자리에서 차체 중심이 들어갈 수 있는 세계 좌표 범위를 옆 거리로 바꾼다. */
	void ComputeLaneLimits(
		const FVector& Location,
		const FVector& Forward,
		float& OutMinOffset,
		float& OutMaxOffset) const;
	void SamplePath(
		const FScooterRuntime& Runtime,
		float Distance,
		FVector& OutLocation,
		FVector& OutForward,
		float& OutMinOffset,
		float& OutMaxOffset) const;
	/** 점을 길 좌표로 접는다. 앞뒤 거리와 진행 방향 오른쪽 옆 거리. */
	bool ProjectOntoPath(
		const FScooterRuntime& Runtime,
		const FVector& Point,
		float SearchFrom,
		float SearchTo,
		float& OutAlong,
		float& OutSide) const;
	int32 ActivateScooter(
		TArray<FScooterPathPoint>&& Path,
		float CruiseSpeed,
		bool bNearMissPass,
		bool bParkAtEnd);

	void UpdateScooters(float DeltaSeconds, const FVector& ListenerLocation);
	void AdvanceScooter(int32 SlotIndex, float DeltaSeconds, const FVector& ListenerLocation);
	/** 몸이 차체에 닿았으면 밀어낸다. 차체가 몸을 통과하는 프레임은 없다. */
	void ResolvePlayerClearance(int32 SlotIndex, APawn* Player);
	void ApplyScooterTransform(int32 SlotIndex, float DeltaSeconds);
	void ParkScooter(int32 SlotIndex);
	void RefreshParkedScooterPresence();
	/** 세워 둔 오토바이에 정우를 앉히거나 내린다. 보이는 동안에는 바꾸지 않는다(bForce 제외). */
	void RefreshParkedRider(int32 SlotIndex, bool bForce);
	void SetScooterVisible(int32 SlotIndex, bool bVisible, bool bWithRider);
	void SetScooterBlocking(int32 SlotIndex, bool bBlocking);

	void PlayScooterHorn(int32 SlotIndex, bool bDouble);
	void PlayScooterScreech(int32 SlotIndex);
	void PlayScooterWhoosh(int32 SlotIndex, const FVector& At);
	void PlayScooterEngineOff(int32 SlotIndex);
	void PlayRiderLine(const FText& Line);

	void UpdateGust(float DeltaSeconds);
	void UpdateLeaves(float DeltaSeconds, const FVector& ListenerLocation);
	void UpdateCatTrace(float DeltaSeconds);

	void ActivateLeaves(const FVector& Origin, int32 Count, float ImpulseScale);
	void DeactivateScooter(int32 SlotIndex);
	void DeactivateLeaf(int32 LeafIndex);
	void DeactivateCatTrace();

	UAudioComponent* CreateSpatialAudioComponent(
		USceneComponent* Parent,
		FName ComponentName,
		float InnerRadius,
		float FalloffDistance);
	UAudioComponent* SpawnTransientOneShot(
		class UIGToneSequenceSoundWave* Sound,
		const FVector& Location,
		float Volume,
		float InnerRadius,
		float FalloffDistance);
	class UIGToneSequenceSoundWave* CreateScooterLoop(UObject* Outer) const;
	class UIGToneSequenceSoundWave* CreateGustSound(UObject* Outer, float Duration) const;
	class UIGToneSequenceSoundWave* CreateCatCall(UObject* Outer) const;

	bool TryGetListenerLocation(FVector& OutLocation) const;
	APawn* GetPlayerPawn() const;
	bool IsPlayerNearRoad(float MaxDistance) const;
	class UIGMissingFloorNarrativeSubsystem* GetNarrative() const;

	UPROPERTY(VisibleAnywhere, Category = "Indie Game|Neighborhood")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(EditAnywhere, Category = "Indie Game|Neighborhood|Scheduling", meta = (ClampMin = "1"))
	int32 DeterministicSeed = 4040444;

	UPROPERTY(EditAnywhere, Category = "Indie Game|Neighborhood|Performance", meta = (ClampMin = "500.0"))
	float EventActivationDistance = 900.0f;

	UPROPERTY(EditAnywhere, Category = "Indie Game|Neighborhood|Performance", meta = (ClampMin = "500.0"))
	float LeafSimulationDistance = 1900.0f;

	/** 슬롯마다 차체 뿌리. 이 컴포넌트가 길을 따라 움직이고 기울어진다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> ScooterRoots;

	/**
	 * 슬롯마다 메시 한 장. 기사가 타고 있는 동안은 SM_DeliveryScooterRidden이고,
	 * 기사가 내린 뒤(세워 둔 오토바이)는 SM_DeliveryScooter로 바꿔 끼운다.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ScooterBodies;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> ScooterHeadlights;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> ScooterTailLights;

	/** 서 있거나 기어갈 때만 몸을 막는 상자. 달리는 동안에는 회피 로직이 막는다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> ScooterBlockers;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> ScooterAudio;

	/** Bounded one-shots that must be stopped when chapters change. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> TransientAudio;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> LeafMeshes;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> CatTraceRoot;

	/** One authored cat mesh, or the six-part release-safe fallback. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CatSilhouetteParts;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> AlleyCatMesh;

	/** 세워 둔 오토바이(기사 없음, 받침대 내림). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ScooterMesh;

	/** 기사가 탄 채 달리는 오토바이. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> RiddenScooterMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> DarkMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LeafMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> AlleyCatMaterial;

	TArray<FScooterRuntime> ScooterRuntime;
	TArray<FLeafRuntime> LeafRuntime;
	FCatTraceRuntime CatRuntime;
	FRandomStream Random;

	FVector RoadStart = FVector(-540.0f, -555.0f, -5.0f);
	FVector RoadEnd = FVector(2190.0f, -555.0f, -5.0f);
	FVector CurrentWindSignal = FVector::ZeroVector;
	FVector GustDirection = FVector::ForwardVector;
	float GustElapsed = -1.0f;
	float GustDuration = 0.0f;
	float GustPeakStrength = 0.0f;
	bool bPoolsInitialized = false;
	bool bOutdoorSequencePrimed = false;
	bool bParkedRiderPresent = false;
	bool bNearMissArmed = false;
	/** 이번 실행에서 첫 외출의 오토바이가 지나갔다. 밤 전에는 서사에 남기지 않는다. */
	bool bNearMissPlayed = false;
	double NearMissArmedSeconds = 0.0;
	bool bNearMissThoughtPending = false;

	FTimerHandle ScooterScheduleHandle;
	FTimerHandle NearMissPollHandle;
	FTimerHandle ParkedPresenceHandle;
	FTimerHandle NearMissThoughtHandle;
	FTimerHandle GustScheduleHandle;
	FTimerHandle CatScheduleHandle;
};
