#include "Interaction/IGFireDoorWedge.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Interaction/IGSwingDoor.h"
#include "Materials/MaterialInterface.h"
#include "Player/IGHorrorHUD.h"
#include "TimerManager.h"

namespace IGFireDoor
{
	// §4. 그냥 빼면 도어클로저가 문을 끌어당긴다. 빠르고 크다.
	constexpr float SlamLoudness = 0.42f;
	constexpr float SlamDurationScale = 0.45f;
	// 문을 잡고 빼면 천천히 닫힌다.
	constexpr float EasedLoudness = 0.08f;
	constexpr float EasedDurationScale = 1.9f;
	constexpr float HoldSeconds = 1.0f;
	// 닫힌 방화문 너머 계단실의 소리. 걷는 소리(0.06~0.18)는 삼키고 뛰는 소리는 줄인다.
	// 반지름은 계단실 반층 참만 덮는다. 문 앞 복도(X -320 동쪽)까지 닿으면 안 된다.
	constexpr float StairwellMuffleRadius = 100.0f;
	constexpr float StairwellMuffleMasking = 0.16f;
	// 문이 열렸는지 닫혔는지 보는 간격. 닫힘·열림에 맞춰 위의 삼킴을 켜고 끈다.
	constexpr float MuffleCheckSeconds = 0.5f;
}

AIGFireDoorWedge::AIGFireDoorWedge()
{
	WedgeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WedgeRoot"));
	SetRootComponent(WedgeRoot);

	// 고임목은 발밑의 손바닥만 한 나무다. 내려다보기만 해도 잡히게 상자를 키운다.
	FocusBox = CreateDefaultSubobject<UBoxComponent>(TEXT("FocusBox"));
	FocusBox->SetupAttachment(WedgeRoot);
	FocusBox->SetBoxExtent(FVector(14.0f, 14.0f, 10.0f));
	FocusBox->SetRelativeLocation(FVector(0.0f, 0.0f, 8.0f));
	FocusBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FocusBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	FocusBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	FocusBox->SetCanEverAffectNavigation(false);

	WedgeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WedgeMesh"));
	WedgeMesh->SetupAttachment(WedgeRoot);
	WedgeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	InteractionHoldDuration = IGFireDoor::HoldSeconds;
}

void AIGFireDoorWedge::Configure(
	AIGSwingDoor* InDoor,
	UStaticMesh* InWedgeMesh,
	UStaticMesh* CubeMesh,
	UMaterialInterface* WoodMaterial)
{
	Door = InDoor;
	if (InWedgeMesh)
	{
		// 각목을 비스듬히 잘라 만든 고임목. 원점이 바닥 중심이고 실치수다.
		WedgeMesh->SetStaticMesh(InWedgeMesh);
		WedgeMesh->SetCastShadow(false);
	}
	else if (CubeMesh)
	{
		// 18×7×4 cm 나무토막을 비스듬히 문 밑에 끼웠다.
		WedgeMesh->SetStaticMesh(CubeMesh);
		WedgeMesh->SetRelativeScale3D(FVector(0.18f, 0.07f, 0.04f));
		WedgeMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 2.0f));
		WedgeMesh->SetRelativeRotation(FRotator(-9.0f, 0.0f, 0.0f));
		if (WoodMaterial)
		{
			WedgeMesh->SetMaterial(0, WoodMaterial);
		}
	}
	if (InDoor)
	{
		// 괴어 있는 동안 문은 닫히지 않는다. 닫으려면 먼저 고임목을 뺀다.
		InDoor->ForceOpenState(true);
		InDoor->SetInteractionEnabled(false);
	}
	bWedged = true;
	SetInteractionEnabled(true);
}

bool AIGFireDoorWedge::CanInteract_Implementation(AActor* Interactor) const
{
	return Super::CanInteract_Implementation(Interactor) && bWedged && Door.IsValid();
}

FText AIGFireDoorWedge::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return NSLOCTEXT("IGMissingFloor", "FireDoorWedgePrompt", "고임목 빼기 (길게 누르면 문을 잡고 천천히)");
}

void AIGFireDoorWedge::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	Super::CompleteInteraction_Implementation(Context);
	PullWedge(true);
}

void AIGFireDoorWedge::EndInteraction_Implementation(
	const FIGInteractionContext& Context,
	const EIGInteractionEndReason EndReason)
{
	Super::EndInteraction_Implementation(Context, EndReason);
	// 일찍 손을 뗀 것만 「그냥 뺐다」로 친다. 시선이 벗어났거나 연출이 끊은 것은 아니다.
	if (EndReason == EIGInteractionEndReason::Released && bWedged)
	{
		PullWedge(false);
	}
}

void AIGFireDoorWedge::PullWedge(const bool bQuiet)
{
	AIGSwingDoor* FireDoor = Door.Get();
	if (!bWedged || !FireDoor)
	{
		return;
	}
	bWedged = false;
	SetInteractionEnabled(false);
	WedgeMesh->SetVisibility(false);
	FireDoor->SetInteractionEnabled(true);
	FireDoor->BeginScriptedSwingWithLoudness(
		false,
		bQuiet ? IGFireDoor::EasedLoudness : IGFireDoor::SlamLoudness,
		bQuiet ? IGFireDoor::EasedDurationScale : IGFireDoor::SlamDurationScale);
	if (!bQuiet)
	{
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "CaptionFireDoorSlam", "[방화문이 쾅 닫힌다]"),
			2.2f,
			FireDoor->GetLatchSoundLocation());
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			MuffleTimer,
			this,
			&AIGFireDoorWedge::RefreshStairwellMuffle,
			IGFireDoor::MuffleCheckSeconds,
			true);
	}
}

void AIGFireDoorWedge::RefreshStairwellMuffle()
{
	UWorld* World = GetWorld();
	UIGNoiseSubsystem* Noise = World ? World->GetSubsystem<UIGNoiseSubsystem>() : nullptr;
	const AIGSwingDoor* FireDoor = Door.Get();
	if (!Noise)
	{
		return;
	}
	const bool bShouldMuffle = FireDoor && FireDoor->IsFullyClosed();
	if (bShouldMuffle && MuffleHandle == INDEX_NONE)
	{
		MuffleHandle = Noise->RegisterHumSource(
			StairwellCenter,
			IGFireDoor::StairwellMuffleRadius,
			IGFireDoor::StairwellMuffleMasking);
	}
	else if (!bShouldMuffle && MuffleHandle != INDEX_NONE)
	{
		Noise->UnregisterHumSource(MuffleHandle);
		MuffleHandle = INDEX_NONE;
	}
}

void AIGFireDoorWedge::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MuffleTimer);
		if (MuffleHandle != INDEX_NONE)
		{
			if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
			{
				Noise->UnregisterHumSource(MuffleHandle);
			}
			MuffleHandle = INDEX_NONE;
		}
	}
	Super::EndPlay(EndPlayReason);
}
