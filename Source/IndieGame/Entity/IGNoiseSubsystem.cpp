#include "Entity/IGNoiseSubsystem.h"

#include "Engine/World.h"

FIGNoiseEvent UIGNoiseSubsystem::ReportNoise(
	const FVector& Location,
	const float Loudness,
	AActor* Instigator)
{
	FIGNoiseEvent Event;
	Event.Location = Location;
	Event.Instigator = Instigator;

	const UWorld* World = GetWorld();
	// 게임 시간이다. 실시간으로 찍으면 일시정지 메뉴를 8초 열었다 닫는 것만으로
	// 추격이 끝나고 10초 반응 기억이 지워졌다 — 그의 Tick은 멈춰 있는데 시계만 흘렀다.
	Event.TimeSeconds = World ? World->GetTimeSeconds() : 0.0;

	// 숨은 자리 안에서 낸 소리는 문짝과 이불에 먹혀 덜 나간다.
	const float Effective =
		FMath::Clamp(Loudness * GetInstigatorMuffle(Instigator), 0.0f, 1.0f)
		- GetMaskingAt(Location);
	if (Effective <= 0.0f)
	{
		// Swallowed by a hum or the entity's own knocking. Nothing sounded,
		// as far as the building is concerned.
		return Event;
	}

	Event.Loudness = Effective;
	Event.Radius = Effective * CarryPerLoudness;
	// §5.6: what got through is what he can remember. A sound a hum swallowed
	// never happened, so it never warms the zone either — which is why hiding
	// beside the fridge stays genuinely safe over a whole night, not just once.
	AccumulateHeat(Location, Effective);
	OnNoiseReported.Broadcast(Event);
	return Event;
}

void UIGNoiseSubsystem::SetInstigatorMuffle(const AActor* Instigator, const float Scale)
{
	if (!Instigator || Scale >= 1.0f)
	{
		if (!Instigator || MuffledInstigator.Get() == Instigator)
		{
			MuffledInstigator.Reset();
			InstigatorMuffleScale = 1.0f;
		}
		return;
	}
	MuffledInstigator = Instigator;
	InstigatorMuffleScale = FMath::Clamp(Scale, 0.0f, 1.0f);
}

float UIGNoiseSubsystem::GetInstigatorMuffle(const AActor* Instigator) const
{
	return Instigator && MuffledInstigator.Get() == Instigator
		? InstigatorMuffleScale
		: 1.0f;
}

FIGNoiseEvent UIGNoiseSubsystem::ReportNoiseUnmasked(
	const FVector& Location,
	const float Loudness,
	AActor* Instigator)
{
	FIGNoiseEvent Event;
	Event.Location = Location;
	Event.Instigator = Instigator;
	const UWorld* World = GetWorld();
	Event.TimeSeconds = World ? World->GetTimeSeconds() : 0.0;

	const float Clamped = FMath::Clamp(Loudness, 0.0f, 1.0f);
	if (Clamped <= 0.0f)
	{
		return Event;
	}
	Event.Loudness = Clamped;
	Event.Radius = Clamped * CarryPerLoudness;
	// 열지도는 건드리지 않는다. 건물이 무너뜨린 것은 그녀가 자주 소리 내는
	// 자리가 아니다.
	OnNoiseReported.Broadcast(Event);
	return Event;
}

void UIGNoiseSubsystem::BroadcastPresentationCue(
	const FVector& Location,
	const float Loudness,
	AActor* Instigator)
{
	const float Clamped = FMath::Clamp(Loudness, 0.0f, 1.0f);
	if (Clamped <= 0.0f)
	{
		return;
	}
	// 소음 버스와 같은 모양이라 파문과 진동이 받는 쪽 코드를 그대로 쓴다. 다만
	// 그의 귀, 녹음, 히트맵으로 가는 길은 없다.
	FIGNoiseEvent Event;
	Event.Location = Location;
	Event.Loudness = Clamped;
	Event.Radius = Clamped * CarryPerLoudness;
	Event.Instigator = Instigator;
	const UWorld* World = GetWorld();
	Event.TimeSeconds = World ? World->GetTimeSeconds() : 0.0;
	OnPresentationCue.Broadcast(Event);
}

FIntVector UIGNoiseSubsystem::ToHeatZoneKey(const FVector& Location)
{
	return FIntVector(
		FMath::FloorToInt(Location.X / HeatZoneSize),
		FMath::FloorToInt(Location.Y / HeatZoneSize),
		FMath::FloorToInt(Location.Z / HeatZoneSize));
}

FVector UIGNoiseSubsystem::FromHeatZoneKey(const FIntVector& Key)
{
	return FVector(
		(static_cast<float>(Key.X) + 0.5f) * HeatZoneSize,
		(static_cast<float>(Key.Y) + 0.5f) * HeatZoneSize,
		(static_cast<float>(Key.Z) + 0.5f) * HeatZoneSize);
}

void UIGNoiseSubsystem::AccumulateHeat(
	const FVector& Location,
	const float Loudness)
{
	if (Loudness <= 0.0f)
	{
		return;
	}
	const FIntVector Key = ToHeatZoneKey(Location);
	for (FIGHeatZone& Zone : HeatZones)
	{
		if (Zone.Key == Key)
		{
			Zone.Heat = FMath::Min(Zone.Heat + Loudness, HeatSaturation);
			return;
		}
	}
	FIGHeatZone& Added = HeatZones.AddDefaulted_GetRef();
	Added.Key = Key;
	Added.Heat = FMath::Min(Loudness, HeatSaturation);
	Added.Serial = NextHeatSerial++;
}

float UIGNoiseSubsystem::GetHeatAt(const FVector& Location) const
{
	const FIntVector Key = ToHeatZoneKey(Location);
	for (const FIGHeatZone& Zone : HeatZones)
	{
		if (Zone.Key == Key)
		{
			return FMath::Clamp(Zone.Heat / HeatSaturation, 0.0f, 1.0f);
		}
	}
	return 0.0f;
}

bool UIGNoiseSubsystem::GetHottestZone(
	FVector& OutCenter,
	float& OutHeat) const
{
	const FIGHeatZone* Best = nullptr;
	for (const FIGHeatZone& Zone : HeatZones)
	{
		if (Zone.Heat <= 0.0f)
		{
			continue;
		}
		// Strictly greater, so the zone that warmed first keeps a tie. Two
		// identical playthroughs must produce the same ambush.
		if (!Best || Zone.Heat > Best->Heat)
		{
			Best = &Zone;
		}
	}
	if (!Best)
	{
		return false;
	}
	OutCenter = FromHeatZoneKey(Best->Key);
	OutHeat = FMath::Clamp(Best->Heat / HeatSaturation, 0.0f, 1.0f);
	return true;
}

void UIGNoiseSubsystem::DecayHeatmapForNewNight()
{
	for (FIGHeatZone& Zone : HeatZones)
	{
		Zone.Heat *= HeatNightDecay;
	}
	// Anything below a tenth of a walk step is noise in the statistics, not a
	// habit. Dropping it keeps the array from growing over four nights.
	HeatZones.RemoveAll([](const FIGHeatZone& Zone)
	{
		return Zone.Heat < 0.015f;
	});
}

void UIGNoiseSubsystem::ResetHeatmap()
{
	HeatZones.Reset();
	NextHeatSerial = 1;
}

int32 UIGNoiseSubsystem::RegisterHumSource(
	const FVector& Location,
	const float Radius,
	const float Masking)
{
	FIGHumSource Source;
	Source.Handle = NextHumHandle++;
	Source.Location = Location;
	Source.Radius = FMath::Max(Radius, 0.0f);
	Source.Masking = FMath::Clamp(Masking, 0.0f, 1.0f);
	HumSources.Add(Source);
	return Source.Handle;
}

void UIGNoiseSubsystem::UnregisterHumSource(const int32 Handle)
{
	HumSources.RemoveAll([Handle](const FIGHumSource& Source)
	{
		return Source.Handle == Handle;
	});
}

void UIGNoiseSubsystem::SetGlobalMasking(const float Masking)
{
	GlobalMasking = FMath::Clamp(Masking, 0.0f, 1.0f);
}

float UIGNoiseSubsystem::GetMaskingAt(const FVector& Location) const
{
	// Hums do not stack: standing between two machines is as covered as the
	// stronger one alone. The global window stacks on top — moving on the
	// entity's beat while beside the fridge really is the safest thing.
	float BestHum = 0.0f;
	for (const FIGHumSource& Source : HumSources)
	{
		if (Source.Radius <= 0.0f)
		{
			continue;
		}
		const float Distance = FVector::Dist(Location, Source.Location);
		if (Distance >= Source.Radius)
		{
			continue;
		}
		// Full masking at the machine, fading linearly to zero at the edge.
		const float Falloff = 1.0f - Distance / Source.Radius;
		BestHum = FMath::Max(BestHum, Source.Masking * Falloff);
	}
	return FMath::Clamp(BestHum + GlobalMasking, 0.0f, 1.0f);
}
