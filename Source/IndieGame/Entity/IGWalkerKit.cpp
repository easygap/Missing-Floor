#include "Entity/IGWalkerKit.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Interaction/IGSwingDoor.h"

void FIGWalkerRoute::Set(const TArray<FVector>& InPoints)
{
	Points = InPoints;
	Distances.Reset();
	Distances.Add(0.0f);
	float Total = 0.0f;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		Total += FVector::Dist(Points[Index - 1], Points[Index]);
		Distances.Add(Total);
	}
}

FVector FIGWalkerRoute::At(
	const float Distance, const FVector& Fallback, FVector* OutDirection, bool* bOutOnStair) const
{
	if (Points.IsEmpty())
	{
		return Fallback;
	}
	if (Points.Num() == 1)
	{
		return Points[0];
	}
	int32 Segment = Points.Num() - 2;
	for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
	{
		if (Distance <= Distances[Index + 1])
		{
			Segment = Index;
			break;
		}
	}
	const FVector& From = Points[Segment];
	const FVector& To = Points[Segment + 1];
	const float SegmentLength = Distances[Segment + 1] - Distances[Segment];
	const float Alpha = SegmentLength > KINDA_SMALL_NUMBER
		? FMath::Clamp((Distance - Distances[Segment]) / SegmentLength, 0.0f, 1.0f)
		: 1.0f;
	if (OutDirection)
	{
		*OutDirection = (To - From).GetSafeNormal();
	}
	if (bOutOnStair)
	{
		const float Rise = FMath::Abs(To.Z - From.Z);
		*bOutOnStair = StairSlope > 0.0f
			? Rise > StairSlope * FVector::Dist2D(From, To)
			: Rise > StairRise;
	}
	return FMath::Lerp(From, To, Alpha);
}

float IGWalker::TurnYaw(const float CurrentYaw, const float TargetYaw, const float DeltaSeconds, const float Speed)
{
	const float Delta = FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw);
	return CurrentYaw + Delta * FMath::Clamp(DeltaSeconds * Speed, 0.0f, 1.0f);
}

bool IGWalker::AdvanceStepPhase(float& Phase, const float DeltaSeconds, const float Rate, const float HalfCycleSeconds)
{
	const float Before = Phase;
	Phase += DeltaSeconds * Rate / FMath::Max(HalfCycleSeconds, KINDA_SMALL_NUMBER);
	return FMath::FloorToInt(Phase) != FMath::FloorToInt(Before);
}

EIGFootstepSurface IGWalker::FindSurface(const UWorld* World, const FVector& Feet, const AActor* Ignore)
{
	if (!World)
	{
		return EIGFootstepSurface::Concrete;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IGWalkerStep), false, Ignore);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(
			Hit, Feet + FVector(0.0f, 0.0f, 30.0f), Feet - FVector(0.0f, 0.0f, 40.0f), ECC_Visibility, Query)
		|| !Hit.GetComponent())
	{
		return EIGFootstepSurface::Concrete;
	}
	const UPrimitiveComponent* Floor = Hit.GetComponent();
	if (Floor->ComponentHasTag(FName(TEXT("Footstep.MetalStair"))))
	{
		return EIGFootstepSurface::MetalStair;
	}
	if (Floor->ComponentHasTag(FName(TEXT("Footstep.Rooftop"))))
	{
		return EIGFootstepSurface::Rooftop;
	}
	if (Floor->ComponentHasTag(FName(TEXT("Footstep.GypsumDebris"))))
	{
		return EIGFootstepSurface::GypsumDebris;
	}
	return EIGFootstepSurface::Concrete;
}

void IGWalker::PlayFootstep(
	UObject* Context,
	const EIGFootstepSurface Surface,
	uint32& Seed,
	const FVector& Feet,
	const float Volume,
	const float PitchBase,
	const float SynthPitch,
	const float SynthAmplitude)
{
	const TCHAR* Prefix = TEXT("Foot_Concrete");
	int32 Variants = 5;
	switch (Surface)
	{
	case EIGFootstepSurface::MetalStair: Prefix = TEXT("Foot_MetalStair"); break;
	case EIGFootstepSurface::Rooftop: Prefix = TEXT("Foot_Rooftop"); Variants = 3; break;
	case EIGFootstepSurface::GypsumDebris: Prefix = TEXT("Foot_Gypsum"); break;
	default: break;
	}
	Seed = Seed * 1664525u + 1013904223u;
	USoundBase* Sound = IGAudio::SampleVariantOr(Prefix, Variants, Seed,
		[Context, Surface, SynthPitch, SynthAmplitude]() -> USoundBase*
		{
			return UIGToneSequenceSoundWave::CreateSurfaceFootstep(Context, Surface, SynthPitch, SynthAmplitude);
		});
	IGAudio::SpawnExpendableOneShotAt(
		Context, Sound, Feet + FVector(0.0f, 0.0f, 6.0f), Volume,
		PitchBase + static_cast<float>(Seed % 9u) * 0.01f, 150.0f, 1500.0f, EIGAudioBus::World);
}

bool IGWalker::IsStairDoorAhead(const AIGSwingDoor& Door, const FVector& From, const FVector& To)
{
	if (Door.IsOpen() && !Door.IsSwinging())
	{
		return false;
	}
	const FVector Hinge = Door.GetActorLocation();
	if (FMath::Abs(From.Z - Hinge.Z) > 150.0f && FMath::Abs(To.Z - Hinge.Z) > 150.0f)
	{
		return false;
	}
	const float FromSide = From.X - Hinge.X;
	const float ToSide = To.X - Hinge.X;
	if (Door.IsSwinging() && FMath::Abs(FromSide) < 60.0f
		&& From.Y > Hinge.Y - 20.0f && From.Y < Hinge.Y + 140.0f)
	{
		return true;
	}
	if (FromSide * ToSide >= 0.0f)
	{
		return false;
	}
	const float CrossY = FMath::Lerp(From.Y, To.Y, FromSide / (FromSide - ToSide));
	return CrossY > Hinge.Y - 20.0f && CrossY < Hinge.Y + 140.0f;
}
