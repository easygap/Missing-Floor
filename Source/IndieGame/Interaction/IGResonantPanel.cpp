#include "Interaction/IGResonantPanel.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AIGResonantPanel::AIGResonantPanel()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	InteractionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("PanelInteraction"));
	SetRootComponent(InteractionBounds);
	InteractionBounds->SetBoxExtent(FVector(25, 1.4f, 25));
	InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBounds->SetGenerateOverlapEvents(false);
	InteractionBounds->SetCanEverAffectNavigation(false);
	PanelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PressurePanel"));
	PanelMesh->SetupAttachment(InteractionBounds);
	PanelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PanelMesh->SetCastShadow(false);
	PanelMesh->SetCullDistance(1800);
	PanelMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(
		TEXT("/Game/Meshes/SM_ServicePressurePanel.SM_ServicePressurePanel"));
	if (Mesh.Succeeded()) { PanelMesh->SetStaticMesh(Mesh.Object); }
	InteractionHoldDuration = 0.65f;
	InteractionPrompt = NSLOCTEXT("IGPanel", "Press", "들뜬 벽판 눌러 보기");
}

bool AIGResonantPanel::CanInteract_Implementation(AActor* Interactor) const
{
	return Super::CanInteract_Implementation(Interactor) && GetWorld()
		&& GetWorld()->GetTimeSeconds() >= NextInteractionAt
		&& !GetWorld()->GetTimerManager().IsTimerActive(ReleaseTimer);
}

void AIGResonantPanel::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	if (!CanInteract_Implementation(Context.Interactor)) { return; }
	Super::CompleteInteraction_Implementation(Context);
	LastInteractor = Context.Interactor;
	NextInteractionAt = GetWorld()->GetTimeSeconds() + 12.0;
	const UGameInstance* Instance = GetGameInstance();
	UIGMissingFloorNarrativeSubsystem* Narrative = Instance
		? Instance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
	bPressedDuringHour = Narrative && Narrative->IsHourSealed();
	CaptureCountAtPress = Narrative ? Narrative->GetCaptureCount() : 0;
	// 0.6cm의 실제 움직임만 준다. 시점이나 손전등을 빼앗지 않는다.
	PanelMesh->SetRelativeLocation(FVector(0, 0.6f, 0));
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.4f),
		GetActorLocation(), 0.32f, 1, 70, 600, EIGAudioBus::Player);
	if (UIGNoiseSubsystem* Noise = GetWorld()->GetSubsystem<UIGNoiseSubsystem>())
	{
		Noise->ReportNoise(GetActorLocation(), 0.12f, Context.Interactor);
	}
	if (Narrative && Narrative->MarkBeatPlayed(FName(TEXT("Discovery.LoosePanel"))))
	{
		AIGHorrorHUD::PushThought(this,
			NSLOCTEXT("IGPanel", "Loose", "여기만 덧댔네. 손으로 누르면 들어간다."), 3.0f);
	}
	GetWorld()->GetTimerManager().SetTimer(ReleaseTimer, this,
		&AIGResonantPanel::ReleasePanel, 1.6f, false);
}

void AIGResonantPanel::ReleasePanel()
{
	MovementSeconds = 0;
	SetActorTickEnabled(true);
	const UGameInstance* Instance = GetGameInstance();
	UIGMissingFloorNarrativeSubsystem* Narrative = Instance
		? Instance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
	// 붙잡히거나 밤이 끝난 뒤에 예약된 소리가 새 장면으로 넘어가지 않게 한다.
	if (!LastInteractor.IsValid() || !Narrative ||
		(Narrative->IsHourSealed() != bPressedDuringHour) ||
		Narrative->GetCaptureCount() != CaptureCountAtPress ||
		FVector::DistSquared(LastInteractor->GetActorLocation(), GetActorLocation()) > FMath::Square(1200.f))
	{
		return;
	}
	const bool bNight = bPressedDuringHour && Narrative->IsHourSealed();
	// 판이 제자리로 튀어나오며 덜컥한다. 노크 소리를 쓰지 않는다 — 이 건물에서
	// 두드리는 소리는 대답을 바라는 쪽의 말이다.
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateKickedPropKnock(this, true),
		GetActorLocation(), bNight ? 0.62f : 0.28f, 0.82f, 90, 1000, EIGAudioBus::World);
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateSettlePlasterTick(this),
		GetActorLocation(), bNight ? 0.30f : 0.14f, 1, 70, 600, EIGAudioBus::World);
	AIGHorrorHUD::PushAudioCaptionAt(this,
		NSLOCTEXT("IGPanel", "Rattle", "벽판이 덜컥 튀어나오는 소리"), 2.0f, GetActorLocation());
	// 늦게 튀어나온 판이 소리를 낸다. 소리를 낸 것은 판이다. 그녀를 시작점으로 알리면
	// 숨어 있어도 그녀 자리로 찾아오고, 그녀의 발자취(위층 사람의 예측)에도 섞인다.
	if (UIGNoiseSubsystem* Noise = GetWorld()->GetSubsystem<UIGNoiseSubsystem>())
	{
		Noise->ReportNoise(GetActorLocation(), bNight ? 0.48f : 0.18f, this);
	}
	if (bNight && Narrative->MarkBeatPlayed(FName(TEXT("Discovery.PanelReply"))))
	{
		AIGHorrorHUD::PushThought(this,
			NSLOCTEXT("IGPanel", "Reply", "손을 떼고 한참 있다가 튀어나오네."), 3.0f);
	}
}

void AIGResonantPanel::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	MovementSeconds += DeltaSeconds;
	const float Remaining = FMath::Max(0.f, 1.f - MovementSeconds / 0.8f);
	PanelMesh->SetRelativeLocation(FVector(0, 0.6f * Remaining * FMath::Cos(MovementSeconds * 22.f), 0));
	if (Remaining <= 0)
	{
		PanelMesh->SetRelativeLocation(FVector::ZeroVector);
		SetActorTickEnabled(false);
	}
}

void AIGResonantPanel::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(ReleaseTimer);
	Super::EndPlay(EndPlayReason);
}
