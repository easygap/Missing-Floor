#include "Interaction/IGRoomLightSwitch.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Narrative/IGStoryStateSubsystem.h"

namespace
{
	FGameplayTag LightOnTag() { return FGameplayTag::RequestGameplayTag(TEXT("State.MissingFloor.HomeLightOn"), false); }
}

AIGRoomLightSwitch::AIGRoomLightSwitch()
{
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("SwitchInteraction"));
	SetRootComponent(InteractionBox);
	InteractionBox->SetBoxExtent(FVector(6, 2, 9));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBox->SetGenerateOverlapEvents(false);
	InteractionBox->SetCanEverAffectNavigation(false);
}

void AIGRoomLightSwitch::BeginPlay()
{
	Super::BeginPlay();
	if (UIGStoryStateSubsystem* Story = GetGameInstance()->GetSubsystem<UIGStoryStateSubsystem>())
	{
		Story->OnStoryStateTagChanged.AddUniqueDynamic(this, &ThisClass::HandleState);
	}
	RefreshState();
}

void AIGRoomLightSwitch::BindLight(UPointLightComponent* InLight)
{
	Light = InLight;
	RefreshState();
}

void AIGRoomLightSwitch::RefreshState()
{
	const UIGStoryStateSubsystem* Story = GetGameInstance()->GetSubsystem<UIGStoryStateSubsystem>();
	bLightOn = Story && Story->HasState(LightOnTag());
	// 층별 조명 구역이 Visibility를 쥔다. 스위치는 세기만 바꾼다.
	if (Light) Light->SetIntensity(bLightOn ? 950.f : 0.f);
}

void AIGRoomLightSwitch::HandleState(FGameplayTag Tag, bool bAdded)
{
	if (Tag == LightOnTag()) RefreshState();
}

FText AIGRoomLightSwitch::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return bLightOn ? NSLOCTEXT("IGRoomLight", "Off", "방 불 끄기")
		: NSLOCTEXT("IGRoomLight", "On", "방 불 켜기");
}

void AIGRoomLightSwitch::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	if (!CanInteract_Implementation(Context.Interactor.Get()) || !Light) return;
	UIGStoryStateSubsystem* Story = GetGameInstance()->GetSubsystem<UIGStoryStateSubsystem>();
	if (!Story) return;
	if (bLightOn) Story->RemoveState(LightOnTag());
	else Story->AddState(LightOnTag());
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateSwitchClick(this, bLightOn),
		GetActorLocation(), 0.3f, 1.f, 60.f, 400.f, EIGAudioBus::Player);
	if (UIGNoiseSubsystem* Noise = GetWorld()->GetSubsystem<UIGNoiseSubsystem>())
		Noise->ReportNoise(GetActorLocation(), 0.08f, Context.Interactor.Get());
}

void AIGRoomLightSwitch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* Instance = GetGameInstance())
		if (UIGStoryStateSubsystem* Story = Instance->GetSubsystem<UIGStoryStateSubsystem>())
			Story->OnStoryStateTagChanged.RemoveDynamic(this, &ThisClass::HandleState);
	Super::EndPlay(EndPlayReason);
}
