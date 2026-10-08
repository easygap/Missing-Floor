#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGStairSensorLights.generated.h"

class AIGPrologueWorldScene;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;

/** 센서등 하나가 켜지거나 꺼졌다. 번호는 층 참 순서(1층이 0)다. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FIGStairSensorSwitched, int32 /*LampIndex*/, bool /*bOn*/);

/**
 * 계단탑 층 참의 센서등(EXPANSION_PLAN §2.3 첫째 밤, §3.5).
 *
 * 관리실은 공용 전기료를 아끼려고 밤 12시부터 새벽 5시까지 복도 등을 끈다. 계단은
 * 사람이 지나갈 때만 켜지는 센서등이라 그대로 둔다. 그래서 새벽 네 시 반의 건물에서
 * 저절로 켜지는 등은 이것뿐이다.
 *
 * 1~4층 참에 하나씩 있다. 감지기는 그 참과, 참에 붙은 두 계단의 절반쯤까지를 본다.
 * 반 층 참은 보지 않는다. 계단탑에서 가장 어두운 자리가 거기다. 움직이는 몸이 범위에
 * 들면 딸깍 켜지고, 마지막 움직임 뒤 HoldSeconds가 지나면 딸깍 꺼진다. 감지기는 소리가
 * 아니라 몸을 본다. 위층 사람이 기어 지나가도, 관리인이나 303호가 걸어 지나가도 켜진다.
 *
 * 1층 공용 차단기가 내려가 있거나 넷째 밤에 관리인이 차단기를 내렸으면 켜지지 않는다.
 * 켜고 끌 때는 세기와 갓의 발광만 바꾼다. 표시 여부는 층별 조명 구역이 쥔다.
 * 틱 없이 0.1초 타이머로 돈다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGStairSensorLights : public AActor
{
	GENERATED_BODY()

public:
	AIGStairSensorLights();

	/**
	 * 씬이 계단탑을 지으며 만든 등과 갓을 받는다. 셋은 1층부터 같은 순서이고,
	 * InFloorZ는 그 참의 바닥 높이다. 갓 메시가 없으면 그 자리는 nullptr이다.
	 */
	void Configure(
		AIGPrologueWorldScene* InScene,
		const TArray<UPointLightComponent*>& InLights,
		const TArray<UStaticMeshComponent*>& InLamps,
		const TArray<float>& InFloorZ);

	/** 공용 차단기나 밤4 차단기가 바뀌었다. 끊겼으면 그 자리에서 다 끈다. */
	void ApplyPower();

	int32 GetLampCount() const { return Lights.Num(); }
	bool IsLampOn(int32 Index) const;
	bool IsAnyLampOn() const;
	/** 발이 어느 참 감지기의 범위 안인가. 범위 밖이면 INDEX_NONE. */
	int32 FindLampForFeet(const FVector& Feet) const;
	/** 이 자리가 켜진 센서등 불빛 안인가. 어둑시니는 여기서 견디지 못한다. */
	bool IsLitAt(const FVector& Location) const;
	FVector GetLampLocation(int32 Index) const;
	float GetLampFloorZ(int32 Index) const;
	/** 검사용. 이 등이 지금까지 켜진 횟수. */
	int32 GetSwitchOnCount(int32 Index) const;
	/** 검사용. 지금 등의 세기. */
	float GetLampIntensity(int32 Index) const;
	/**
	 * 이 등이 다음 한 번은 움직임이 멎고 Seconds 만에 꺼진다. 오래된 빌라 센서등은
	 * 원래 그렇다. 첫째 밤 3층 참이 이것으로 어둑시니를 처음 보여 준다.
	 */
	void SetEarlyCutoff(int32 Index, float Seconds);

	FIGStairSensorSwitched OnLampSwitched;

	/** 마지막 움직임 뒤 켜져 있는 시간. */
	static constexpr float HoldSeconds = 9.0f;
	/** 이보다 빨리 움직여야 감지한다(cm/s). 발소리가 나기 시작하는 빠르기와 같다. */
	static constexpr float MotionSpeed = 20.0f;
	/** 감지 범위. 참 가운데에서 수평 거리와, 참 바닥을 기준으로 한 발 높이. */
	static constexpr float DetectRadius = 260.0f;
	static constexpr float DetectBelow = 120.0f;
	static constexpr float DetectAbove = 110.0f;
	/**
	 * 켜진 등의 세기와 갓의 발광. 밤 노출은 손전등에 맞춰 잠겨 있다. 그 노출에서 참
	 * 하나가 또렷하게 밝아야 불이 들어오는 순간이 사건이 된다. 복도 등(1020)보다 밝은
	 * LED다. 반지름 520 밖인 반 층 참은 거의 그대로 어둡다.
	 */
	static constexpr float OnIntensity = 2600.0f;
	static constexpr float OnEmissive = 4.5f;

protected:
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

private:
	void Update();
	void RefreshMovers();
	bool IsPowered() const;
	void SetLampOn(int32 Index, bool bOn, bool bSilent);
	static bool IsMoverActive(const AActor* Actor);
	static FVector GetFeet(const AActor* Actor);

	struct FMover
	{
		TWeakObjectPtr<AActor> Actor;
		FVector LastFeet = FVector::ZeroVector;
		bool bHasLast = false;
	};

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> Lights;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Lamps;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> LampMaterials;

	TArray<float> FloorZ;
	TArray<double> LastMotionSeconds;
	TArray<uint8> LampOn;
	TArray<int32> SwitchOnCounts;
	TArray<float> EarlyCutoffSeconds;
	TArray<FMover> Movers;
	double LastUpdateSeconds = -1.0;
	double LastRefreshSeconds = -1.0;
	FTimerHandle UpdateTimer;
};
