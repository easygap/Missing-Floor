#include "Environment/IGStairSensorLights.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGManagerPatrol.h"
#include "Entity/IGStairNeighbor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

namespace IGStairSensor
{
	// 감지기를 보는 간격. 켜지는 데 걸리는 시간이 이만큼 늦는다.
	constexpr float SensorPollSeconds = 0.1f;
	// 움직이는 몸 목록은 자주 바뀌지 않는다. 위층 사람과 관리인은 밤마다 다시 선다.
	constexpr double RefreshSeconds = 1.0;
	// 켜진 등이 닿는 곳. 그 참과 위아래 계단, 반 층 참의 가장자리까지다.
	constexpr float LitRadius = 430.0f;
	constexpr float LitBelow = 200.0f;
	constexpr float LitAbove = 260.0f;
}

AIGStairSensorLights::AIGStairSensorLights()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AIGStairSensorLights::Configure(
	AIGPrologueWorldScene* InScene,
	const TArray<UPointLightComponent*>& InLights,
	const TArray<UStaticMeshComponent*>& InLamps,
	const TArray<float>& InFloorZ)
{
	Scene = InScene;
	Lights.Reset();
	Lamps.Reset();
	LampMaterials.Reset();
	FloorZ.Reset();
	const int32 Count = FMath::Min(InLights.Num(), InFloorZ.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Lights.Add(InLights[Index]);
		FloorZ.Add(InFloorZ[Index]);
		UStaticMeshComponent* Lamp = InLamps.IsValidIndex(Index) ? InLamps[Index] : nullptr;
		Lamps.Add(Lamp);
		// 갓은 M_IGBakedProp 인스턴스다. 발광 맵은 돔에만 구워 두었으니 세기만 올리고 내린다.
		LampMaterials.Add(Lamp ? Lamp->CreateDynamicMaterialInstance(0) : nullptr);
	}
	LastMotionSeconds.Init(-1000.0, Count);
	LampOn.Init(0, Count);
	SwitchOnCounts.Init(0, Count);
	EarlyCutoffSeconds.Init(0.0f, Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		SetLampOn(Index, false, true);
	}
	RefreshMovers();
	GetWorldTimerManager().SetTimer(
		UpdateTimer, this, &AIGStairSensorLights::Update, IGStairSensor::SensorPollSeconds, true);
}

void AIGStairSensorLights::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(UpdateTimer);
	Super::EndPlay(EndPlayReason);
}

bool AIGStairSensorLights::IsPowered() const
{
	// 센서등은 공용 회로에 물려 있다. 1층 공용 차단기를 내리면 같이 죽고, 넷째 밤에
	// 관리인이 차단기를 내리면 건물의 등이 전부 죽는다. 비상구 등만 축전지로 남는다.
	const AIGPrologueWorldScene* Building = Scene.Get();
	return Building && Building->AreCommonInspectionLightsEnabled() && Building->IsMissingFloorAnnexPowered();
}

void AIGStairSensorLights::ApplyPower()
{
	if (IsPowered())
	{
		return;
	}
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		if (LampOn[Index])
		{
			SetLampOn(Index, false, true);
		}
		EarlyCutoffSeconds[Index] = 0.0f;
	}
}

bool AIGStairSensorLights::IsLampOn(const int32 Index) const
{
	return LampOn.IsValidIndex(Index) && LampOn[Index] != 0;
}

bool AIGStairSensorLights::IsAnyLampOn() const
{
	for (const uint8 bLit : LampOn)
	{
		if (bLit)
		{
			return true;
		}
	}
	return false;
}

FVector AIGStairSensorLights::GetLampLocation(const int32 Index) const
{
	const UPointLightComponent* Light = Lights.IsValidIndex(Index) ? Lights[Index].Get() : nullptr;
	return Light ? Light->GetComponentLocation() : FVector::ZeroVector;
}

float AIGStairSensorLights::GetLampFloorZ(const int32 Index) const
{
	return FloorZ.IsValidIndex(Index) ? FloorZ[Index] : 0.0f;
}

int32 AIGStairSensorLights::GetSwitchOnCount(const int32 Index) const
{
	return SwitchOnCounts.IsValidIndex(Index) ? SwitchOnCounts[Index] : 0;
}

float AIGStairSensorLights::GetLampIntensity(const int32 Index) const
{
	const UPointLightComponent* Light = Lights.IsValidIndex(Index) ? Lights[Index].Get() : nullptr;
	return Light ? Light->Intensity : 0.0f;
}

void AIGStairSensorLights::SetEarlyCutoff(const int32 Index, const float Seconds)
{
	if (EarlyCutoffSeconds.IsValidIndex(Index))
	{
		EarlyCutoffSeconds[Index] = FMath::Max(0.0f, Seconds);
	}
}

int32 AIGStairSensorLights::FindLampForFeet(const FVector& Feet) const
{
	// 감지기는 벽 너머를 못 본다. 계단탑 안에 들어선 몸만 본다. 복도에서 출입구를
	// 들여다보는 동안 계단은 어둠 속에 내려가 있다.
	if (!AIGPrologueWorldScene::IsInsideStairCore(Feet))
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		const float Rise = Feet.Z - FloorZ[Index];
		if (Rise < -DetectBelow || Rise > DetectAbove)
		{
			continue;
		}
		if (FVector::Dist2D(Feet, GetLampLocation(Index)) <= DetectRadius)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool AIGStairSensorLights::IsLitAt(const FVector& Location) const
{
	// 불빛은 출입구로 조금 새어 나갈 뿐이다. 복도에 선 것은 어둠 속에 있다.
	if (!AIGPrologueWorldScene::IsInsideStairCore(Location))
	{
		return false;
	}
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		if (!LampOn[Index])
		{
			continue;
		}
		const float Rise = Location.Z - FloorZ[Index];
		if (Rise < -IGStairSensor::LitBelow || Rise > IGStairSensor::LitAbove)
		{
			continue;
		}
		if (FVector::Dist2D(Location, GetLampLocation(Index)) <= IGStairSensor::LitRadius)
		{
			return true;
		}
	}
	return false;
}

bool AIGStairSensorLights::IsMoverActive(const AActor* Actor)
{
	if (!Actor || Actor->IsHidden())
	{
		return false;
	}
	if (const AIGListenerEntity* Listener = Cast<AIGListenerEntity>(Actor))
	{
		return !Listener->IsDormant();
	}
	if (const AIGManagerPatrol* Patrol = Cast<AIGManagerPatrol>(Actor))
	{
		return Patrol->IsOnDuty();
	}
	return true;
}

FVector AIGStairSensorLights::GetFeet(const AActor* Actor)
{
	// 플레이어와 위층 사람은 캡슐 가운데가 몸 위치다. 관리인은 루트가 발이다.
	const FVector Location = Actor->GetActorLocation();
	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Actor->GetRootComponent()))
	{
		return Location - FVector(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight());
	}
	return Location;
}

void AIGStairSensorLights::RefreshMovers()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	LastRefreshSeconds = World->GetTimeSeconds();
	TArray<AActor*> Wanted;
	if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Wanted.Add(Player);
	}
	for (TActorIterator<AIGListenerEntity> It(World); It; ++It)
	{
		Wanted.Add(*It);
	}
	for (TActorIterator<AIGManagerPatrol> It(World); It; ++It)
	{
		Wanted.Add(*It);
	}
	// 셋째 낮에 출근하며 계단을 오르내리는 303호. 숨어 있는 동안은 IsMoverActive가 뺀다.
	for (TActorIterator<AIGStairNeighbor> It(World); It; ++It)
	{
		Wanted.Add(*It);
	}
	// 이미 보던 몸은 마지막 발자리를 그대로 둔다. 새로 들어온 몸만 처음부터 본다.
	TArray<FMover> Next;
	for (AActor* Actor : Wanted)
	{
		const FMover* Known = Movers.FindByPredicate([Actor](const FMover& Mover)
		{
			return Mover.Actor.Get() == Actor;
		});
		if (Known)
		{
			Next.Add(*Known);
		}
		else
		{
			FMover Fresh;
			Fresh.Actor = Actor;
			Next.Add(Fresh);
		}
	}
	Movers = MoveTemp(Next);
}

void AIGStairSensorLights::Update()
{
	UWorld* World = GetWorld();
	if (!World || Lights.IsEmpty())
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const float DeltaSeconds = LastUpdateSeconds < 0.0
		? IGStairSensor::SensorPollSeconds
		: static_cast<float>(Now - LastUpdateSeconds);
	LastUpdateSeconds = Now;
	if (Now - LastRefreshSeconds >= IGStairSensor::RefreshSeconds)
	{
		RefreshMovers();
	}

	const bool bPowered = IsPowered();
	for (FMover& Mover : Movers)
	{
		const AActor* Actor = Mover.Actor.Get();
		if (!IsMoverActive(Actor))
		{
			Mover.bHasLast = false;
			continue;
		}
		const FVector Feet = GetFeet(Actor);
		// 순간이동(리셋, 침대로 옮기기)은 움직임이 아니다. 한 번에 2 m 넘게 뛴 걸음은 버린다.
		const float Travel = Mover.bHasLast ? FVector::Dist(Feet, Mover.LastFeet) : 0.0f;
		const bool bMoved = Mover.bHasLast
			&& DeltaSeconds > KINDA_SMALL_NUMBER
			&& Travel < 200.0f
			&& Travel / DeltaSeconds > MotionSpeed;
		Mover.LastFeet = Feet;
		Mover.bHasLast = true;
		if (!bMoved || !bPowered)
		{
			continue;
		}
		const int32 Lamp = FindLampForFeet(Feet);
		if (Lamp == INDEX_NONE)
		{
			continue;
		}
		LastMotionSeconds[Lamp] = Now;
		if (!LampOn[Lamp])
		{
			SetLampOn(Lamp, true, false);
		}
	}

	if (!bPowered)
	{
		ApplyPower();
		return;
	}
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		if (!LampOn[Index])
		{
			continue;
		}
		const float Hold = EarlyCutoffSeconds[Index] > 0.0f ? EarlyCutoffSeconds[Index] : HoldSeconds;
		if (Now - LastMotionSeconds[Index] >= Hold)
		{
			EarlyCutoffSeconds[Index] = 0.0f;
			SetLampOn(Index, false, false);
		}
	}
}

void AIGStairSensorLights::SetLampOn(const int32 Index, const bool bOn, const bool bSilent)
{
	if (!Lights.IsValidIndex(Index))
	{
		return;
	}
	const bool bWasOn = LampOn[Index] != 0;
	LampOn[Index] = bOn ? 1 : 0;
	if (UPointLightComponent* Light = Lights[Index])
	{
		Light->SetIntensity(bOn ? OnIntensity : 0.0f);
	}
	if (UMaterialInstanceDynamic* Material = LampMaterials[Index])
	{
		Material->SetScalarParameterValue(TEXT("EmissiveStrength"), bOn ? OnEmissive : 0.0f);
	}
	if (bWasOn == bOn)
	{
		return;
	}
	if (bOn)
	{
		++SwitchOnCounts[Index];
	}
	if (!bSilent)
	{
		// 천장 속 계전기가 붙고 떨어지는 소리. 켜질 때가 조금 더 또렷하다. 위층 사람은
		// 이 소리를 듣지 않는다 — 그를 부르는 것은 등을 켜려고 움직인 발소리다.
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateRelayClick(this),
			GetLampLocation(Index),
			bOn ? 0.55f : 0.42f,
			bOn ? 1.0f : 0.86f,
			80.0f,
			900.0f,
			EIGAudioBus::World);
	}
	OnLampSwitched.Broadcast(Index, bOn);
}
