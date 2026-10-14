#include "Interaction/IGAccessHatch.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "IndieGame.h"
#include "Materials/MaterialInterface.h"
#include "Entity/IGNoiseSubsystem.h"

namespace IGAccessHatch
{
	// 다 연 문짝 각도. 벽에 거의 붙을 만큼 젖혀진다.
	constexpr float OpenYawDegrees = 104.0f;
	// 틀 띠 폭과 두께, 문짝 두께(cm).
	constexpr float FrameBand = 3.0f;
	constexpr float FrameDepth = 1.6f;
	constexpr float LeafThickness = 1.2f;
	// 걸려서 튕기는 시간(초)과 튕겨 나가는 각도.
	constexpr float BounceDuration = 0.42f;
	constexpr float BounceBackDegrees = 9.0f;
	// 막힌 자리를 뜯어내는 데 드는 시간(초).
	constexpr float PeelHoldSeconds = 1.3f;
	// 여닫는 소리 크기(위층 사람이 듣는 소음).
	constexpr float SwingNoiseLoudness = 0.16f;
	constexpr float BlockedNoiseLoudness = 0.32f;
	constexpr int32 MoteCount = 9;
}

AIGAccessHatch::AIGAccessHatch()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	HatchRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HatchRoot"));
	SetRootComponent(HatchRoot);
	FocusBox = CreateDefaultSubobject<UBoxComponent>(TEXT("FocusBox"));
	FocusBox->SetupAttachment(HatchRoot);
	FocusBox->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	FocusBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	FocusBox->SetCanEverAffectNavigation(false);
	DoorPivot = CreateDefaultSubobject<USceneComponent>(TEXT("DoorPivot"));
	DoorPivot->SetupAttachment(HatchRoot);
	InteractionHoldDuration = 0.0f;
}

void AIGAccessHatch::Configure(
	const FVector2D& OpeningSizeCm,
	const bool bInHingeOnLeft,
	UStaticMesh* FrameMesh,
	UStaticMesh* DoorMesh,
	UMaterialInterface* PaintMaterial,
	UMaterialInterface* SealMaterial,
	const FText& InOpenPrompt,
	const FText& InClosePrompt)
{
	OpeningSize = OpeningSizeCm;
	bHingeOnLeft = bInHingeOnLeft;
	OpenPrompt = InOpenPrompt;
	ClosePrompt = InClosePrompt;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	const float W = OpeningSize.X;
	const float H = OpeningSize.Y;
	auto MakePart = [this](const TCHAR* Name, USceneComponent* Parent) {
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, Name);
		Part->SetupAttachment(Parent);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Part->SetCanEverAffectNavigation(false);
		Part->RegisterComponent();
		return Part;
	};
	if (FrameMesh)
	{
		UStaticMeshComponent* Frame = MakePart(TEXT("Frame"), HatchRoot);
		Frame->SetStaticMesh(FrameMesh);
		FrameParts.Add(Frame);
	}
	else if (Cube)
	{
		// 틀 네 변. 벽 면에서 0.8 cm 솟는다.
		const float B = IGAccessHatch::FrameBand;
		const FVector Depth(IGAccessHatch::FrameDepth, 0.0f, 0.0f);
		const struct { FVector Center; FVector Size; } Bands[] = {
			{FVector(0.0f, 0.0f, H * 0.5f + B * 0.5f), FVector(IGAccessHatch::FrameDepth, W + B * 2.0f, B)},
			{FVector(0.0f, 0.0f, -H * 0.5f - B * 0.5f), FVector(IGAccessHatch::FrameDepth, W + B * 2.0f, B)},
			{FVector(0.0f, -W * 0.5f - B * 0.5f, 0.0f), FVector(IGAccessHatch::FrameDepth, B, H)},
			{FVector(0.0f, W * 0.5f + B * 0.5f, 0.0f), FVector(IGAccessHatch::FrameDepth, B, H)},
		};
		int32 Index = 0;
		for (const auto& Band : Bands)
		{
			UStaticMeshComponent* Part = MakePart(*FString::Printf(TEXT("FrameBand%d"), Index++), HatchRoot);
			Part->SetStaticMesh(Cube);
			Part->SetRelativeLocation(Band.Center + Depth * 0.0f);
			Part->SetRelativeScale3D(Band.Size / 100.0f);
			if (PaintMaterial)
			{
				Part->SetMaterial(0, PaintMaterial);
			}
			FrameParts.Add(Part);
		}
	}
	// 경첩 축은 개구부 세로 변에 있고, 문짝은 축에서 반대쪽 변까지 뻗는다.
	const float HingeY = bHingeOnLeft ? -W * 0.5f : W * 0.5f;
	DoorPivot->SetRelativeLocation(FVector(IGAccessHatch::FrameDepth * 0.5f, HingeY, 0.0f));
	DoorPanel = MakePart(TEXT("DoorPanel"), DoorPivot);
	if (DoorMesh)
	{
		DoorPanel->SetStaticMesh(DoorMesh);
		if (!bHingeOnLeft)
		{
			DoorPanel->SetRelativeScale3D(FVector(1.0f, -1.0f, 1.0f));
		}
	}
	else if (Cube)
	{
		DoorPanel->SetStaticMesh(Cube);
		const float Dir = bHingeOnLeft ? 1.0f : -1.0f;
		DoorPanel->SetRelativeLocation(FVector(IGAccessHatch::LeafThickness * 0.5f, Dir * (W - 1.0f) * 0.5f, 0.0f));
		DoorPanel->SetRelativeScale3D(FVector(IGAccessHatch::LeafThickness, W - 1.0f, H - 1.0f) / 100.0f);
		if (PaintMaterial)
		{
			DoorPanel->SetMaterial(0, PaintMaterial);
		}
	}
	// 문짝은 닫히면 조준을 받고(열기), 열리면 개구부 상자가 받는다.
	DoorPanel->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	DoorPanel->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	// 눌러 막은 자리. 뭉개진 방음재와 비어져 나온 검은 접착층을 구운 1 m 메시를 개구부 크기로 줄인다.
	UStaticMesh* PlugMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_HatchSealPlug.SM_HatchSealPlug"), nullptr, LOAD_NoWarn);
	if (PlugMesh || Cube)
	{
		SealPlug = MakePart(TEXT("SealPlug"), HatchRoot);
		SealPlug->SetStaticMesh(PlugMesh ? PlugMesh : Cube);
		SealPlug->SetRelativeLocation(FVector(-2.5f, 0.0f, 0.0f));
		SealPlug->SetRelativeScale3D(FVector(4.0f, W - 0.5f, H - 0.5f) / 100.0f);
		if (!PlugMesh && SealMaterial)
		{
			SealPlug->SetMaterial(0, SealMaterial);
		}
		SealPlug->SetVisibility(false);
	}
	// 조준 상자는 개구부 앞에 얇게 둔다.
	FocusBox->SetBoxExtent(FVector(2.0f, W * 0.5f + 2.0f, H * 0.5f + 2.0f));
	FocusBox->SetRelativeLocation(FVector(1.5f, 0.0f, 0.0f));
	// 틈으로 빨려 드는 것은 뜯긴 방음재 부스러기다. 벽판과 같은 재질을 작게 쓴다.
	if (UMaterialInterface* DustMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Prototype/Materials/MI_LinedWallPanelA.MI_LinedWallPanelA"), nullptr, LOAD_NoWarn);
		DustMaterial && Cube)
	{
		for (int32 Mote = 0; Mote < IGAccessHatch::MoteCount; ++Mote)
		{
			UStaticMeshComponent* Part = MakePart(*FString::Printf(TEXT("AirMote%d"), Mote), HatchRoot);
			Part->SetStaticMesh(Cube);
			Part->SetMaterial(0, DustMaterial);
			Part->SetCastShadow(false);
			Part->SetRelativeScale3D(FVector(0.006f));
			Part->SetVisibility(false);
			AirMotes.Add(Part);
		}
	}
	ApplyDoorYaw(0.0f);
	RefreshSealVisual();
}

void AIGAccessHatch::SetSealed(const bool bInSealed)
{
	if (bSealed == bInSealed)
	{
		return;
	}
	bSealed = bInSealed;
	RefreshSealVisual();
	SetAirSound(bAirSound);
}

void AIGAccessHatch::SetObstructed(const bool bInObstructed, const float BlockedYawDegrees)
{
	bObstructed = bInObstructed;
	BlockedYaw = FMath::Clamp(BlockedYawDegrees, 8.0f, IGAccessHatch::OpenYawDegrees - 10.0f);
}

void AIGAccessHatch::ForceOpenState(const bool bInOpen)
{
	bOpen = bInOpen;
	TargetYaw = bOpen ? IGAccessHatch::OpenYawDegrees : 0.0f;
	CurrentYaw = TargetYaw;
	BounceSeconds = 0.0f;
	ApplyDoorYaw(CurrentYaw);
	SetAirSound(bAirSound);
	SetActorTickEnabled(IsGapOpen());
}

void AIGAccessHatch::OpenByScript()
{
	if (!bOpen)
	{
		BeginSwing(true, true);
	}
}

void AIGAccessHatch::SetAirSound(const bool bEnabled)
{
	bAirSound = bEnabled;
	const bool bWant = bAirSound && IsGapOpen();
	if (!bWant)
	{
		if (AirLoop)
		{
			AirLoop->FadeOut(0.6f, 0.0f);
		}
		return;
	}
	if (!AirLoop)
	{
		if (USoundBase* Sound = IGAudio::Sample(TEXT("Lining_AirLoop")))
		{
			AirLoop = NewObject<UAudioComponent>(this, TEXT("AirLoop"));
			AirLoop->SetupAttachment(HatchRoot);
			AirLoop->SetRelativeLocation(FVector(-6.0f, 0.0f, 0.0f));
			AirLoop->bAutoActivate = false;
			AirLoop->bAutoDestroy = false;
			AirLoop->SetSound(Sound);
			AirLoop->AttenuationSettings = IGAudio::MakeAttenuation(this, 60.0f, 700.0f, EIGAudioBus::World);
			AirLoop->bOverrideAttenuation = false;
			AirLoop->RegisterComponent();
		}
	}
	if (AirLoop && !AirLoop->IsPlaying())
	{
		AirLoop->FadeIn(0.8f, 0.55f);
	}
}

void AIGAccessHatch::BeginSwing(const bool bInOpen, const bool bQuiet)
{
	bOpen = bInOpen;
	if (!bOpen && bObstructed)
	{
		// 다리에 걸린다. 걸린 각도까지 닫히다 부딪혀 튕긴다. 문짝은 열린 쪽에 남는다.
		TargetYaw = BlockedYaw;
		BounceSeconds = IGAccessHatch::BounceDuration;
		bOpen = true;
		PlayHatchSound(TEXT("Lining_HatchBlocked"), 0.9f, 1.0f);
		if (UIGNoiseSubsystem* Noise = GetWorld() ? GetWorld()->GetSubsystem<UIGNoiseSubsystem>() : nullptr)
		{
			Noise->ReportNoiseUnmasked(GetActorLocation(), IGAccessHatch::BlockedNoiseLoudness, this);
		}
		OnCloseBlocked.Broadcast(this);
	}
	else
	{
		TargetYaw = bOpen ? IGAccessHatch::OpenYawDegrees : 0.0f;
		BounceSeconds = 0.0f;
		PlayHatchSound(bOpen ? TEXT("Lining_HatchOpen") : TEXT("Lining_HatchClose"), bQuiet ? 0.55f : 0.9f, 1.0f);
		if (UIGNoiseSubsystem* Noise = GetWorld() ? GetWorld()->GetSubsystem<UIGNoiseSubsystem>() : nullptr)
		{
			Noise->ReportNoise(GetActorLocation(), IGAccessHatch::SwingNoiseLoudness, this);
		}
	}
	SetActorTickEnabled(true);
	SetAirSound(bAirSound);
	if (bOpen)
	{
		OnOpened.Broadcast(this);
	}
	else
	{
		OnClosed.Broadcast(this);
	}
}

void AIGAccessHatch::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (BounceSeconds > 0.0f)
	{
		BounceSeconds -= DeltaSeconds;
		if (FMath::IsNearlyEqual(CurrentYaw, TargetYaw, 1.0f))
		{
			// 다리를 친 자리에서 몇 도 튕겨 나갔다가 다시 기대 선다.
			const float Phase = 1.0f - FMath::Max(BounceSeconds, 0.0f) / IGAccessHatch::BounceDuration;
			const float Kick = FMath::Sin(Phase * PI) * IGAccessHatch::BounceBackDegrees;
			ApplyDoorYaw(BlockedYaw + Kick);
			if (BounceSeconds <= 0.0f)
			{
				CurrentYaw = BlockedYaw;
				ApplyDoorYaw(CurrentYaw);
			}
			RefreshAirMotes(DeltaSeconds);
			return;
		}
	}
	CurrentYaw = FMath::FInterpConstantTo(CurrentYaw, TargetYaw, DeltaSeconds, SwingSpeed);
	ApplyDoorYaw(CurrentYaw);
	RefreshAirMotes(DeltaSeconds);
	if (FMath::IsNearlyEqual(CurrentYaw, TargetYaw, 0.1f) && BounceSeconds <= 0.0f && !IsGapOpen())
	{
		SetActorTickEnabled(false);
	}
}

void AIGAccessHatch::ApplyDoorYaw(const float Yaw)
{
	// 왼쪽 경첩은 +Y 변이 방 쪽(+X)으로 나오도록 음의 yaw로 돈다.
	DoorPivot->SetRelativeRotation(FRotator(0.0f, bHingeOnLeft ? -Yaw : Yaw, 0.0f));
}

void AIGAccessHatch::RefreshSealVisual()
{
	if (SealPlug)
	{
		SealPlug->SetVisibility(bSealed);
	}
}

void AIGAccessHatch::RefreshAirMotes(const float DeltaSeconds)
{
	if (AirMotes.IsEmpty())
	{
		return;
	}
	const bool bShow = IsGapOpen() && CurrentYaw > 30.0f;
	MoteClock += DeltaSeconds;
	const float W = OpeningSize.X;
	const float H = OpeningSize.Y;
	for (int32 Index = 0; Index < AirMotes.Num(); ++Index)
	{
		UStaticMeshComponent* Mote = AirMotes[Index];
		if (!Mote)
		{
			continue;
		}
		Mote->SetVisibility(bShow);
		if (!bShow)
		{
			continue;
		}
		// 방 쪽 50 cm 앞에서 개구부 안쪽으로 빨려 들어간다. 먼지마다 출발 자리와 빠르기가 다르다.
		const float Seed = Index * 0.6180339f;
		const float Period = 1.6f + FMath::Frac(Seed * 3.1f) * 1.2f;
		const float T = FMath::Frac(MoteClock / Period + Seed);
		const float Y = (FMath::Frac(Seed * 7.3f) - 0.5f) * W * (1.0f - T * 0.8f);
		const float Z = (FMath::Frac(Seed * 5.7f) - 0.5f) * H * (1.0f - T * 0.8f);
		const float X = FMath::Lerp(48.0f, -6.0f, T * T);
		Mote->SetRelativeLocation(FVector(X, Y, Z));
	}
}

void AIGAccessHatch::PlayHatchSound(const TCHAR* SampleName, const float Volume, const float Pitch) const
{
	USoundBase* Sound = IGAudio::SampleOr(SampleName, [this, SampleName]() -> USoundBase* {
		UObject* Outer = const_cast<AIGAccessHatch*>(this);
		if (FCString::Strcmp(SampleName, TEXT("Lining_HatchBlocked")) == 0)
		{
			return UIGToneSequenceSoundWave::CreateLockedRattle(Outer);
		}
		if (FCString::Strcmp(SampleName, TEXT("Lining_HatchClose")) == 0)
		{
			return UIGToneSequenceSoundWave::CreateDoorThud(Outer);
		}
		return UIGToneSequenceSoundWave::CreateDoorCreak(Outer, false);
	});
	IGAudio::SpawnOneShotFromActorAt(this, Sound, GetHingeWorld(), Volume, Pitch, 80.0f, 900.0f);
}

FVector AIGAccessHatch::GetHingeWorld() const
{
	return DoorPivot ? DoorPivot->GetComponentLocation() : GetActorLocation();
}

bool AIGAccessHatch::CanInteract_Implementation(AActor* Interactor) const
{
	if (!Super::CanInteract_Implementation(Interactor))
	{
		return false;
	}
	if (bSealed && bOpen && !bObstructed)
	{
		return bReopenAllowed;
	}
	return true;
}

FText AIGAccessHatch::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	// 몸이 누르고 있으면 닫아 보는 것만 된다(다리에 걸려 튕긴다).
	if (bSealed && bOpen && !bObstructed)
	{
		return NSLOCTEXT("IGMissingFloor", "HatchPeelSealPrompt", "막힌 자리 뜯어내기");
	}
	if (bOpen)
	{
		return ClosePrompt.IsEmpty() ? NSLOCTEXT("IGMissingFloor", "HatchClosePrompt", "점검구 닫기") : ClosePrompt;
	}
	return OpenPrompt.IsEmpty() ? NSLOCTEXT("IGMissingFloor", "HatchOpenPrompt", "점검구 열기") : OpenPrompt;
}

float AIGAccessHatch::GetInteractionHoldDuration_Implementation(AActor* Interactor) const
{
	return bSealed && bOpen && !bObstructed ? IGAccessHatch::PeelHoldSeconds : 0.0f;
}

void AIGAccessHatch::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	Super::CompleteInteraction_Implementation(Context);
	if (bSealed && bOpen && !bObstructed)
	{
		SetSealed(false);
		PlayHatchSound(TEXT("Lining_Peel"), 0.8f, 1.08f);
		if (UIGNoiseSubsystem* Noise = GetWorld() ? GetWorld()->GetSubsystem<UIGNoiseSubsystem>() : nullptr)
		{
			Noise->ReportNoise(GetActorLocation(), IGAccessHatch::SwingNoiseLoudness, this);
		}
		SetActorTickEnabled(true);
		OnReopened.Broadcast(this);
		return;
	}
	BeginSwing(!bOpen, false);
}

void AIGAccessHatch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AirLoop)
	{
		AirLoop->Stop();
	}
	Super::EndPlay(EndPlayReason);
}
