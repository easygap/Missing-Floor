#include "Interaction/IGBathroomRefuge.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGNoiseSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/IGSwingDoor.h"
#include "Kismet/GameplayStatics.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "TimerManager.h"

namespace IGBathroom
{
	/** 숨음을 다시 재는 간격. */
	constexpr float ConcealCheckSeconds = 0.2f;
	/** 숨은 동안 그녀가 낸 소리가 나가는 비율. 장롱·침대 밑과 같다(§4). */
	constexpr float RoomNoiseScale = 0.55f;
}

AIGBathroomRefuge::AIGBathroomRefuge()
{
	FocusBox = CreateDefaultSubobject<UBoxComponent>(TEXT("FocusBox"));
	SetRootComponent(FocusBox);
	// 로제트 가운데 단추. 손바닥만 하게 잡아 레버를 보면서도 누를 수 있게 한다.
	FocusBox->SetBoxExtent(FVector(3.0f, 6.0f, 6.0f));
	FocusBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FocusBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	FocusBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	FocusBox->SetGenerateOverlapEvents(false);
	FocusBox->SetCanEverAffectNavigation(false);
}

void AIGBathroomRefuge::Configure(
	AIGSwingDoor* InDoor,
	const FBox& InRoom,
	const FVector& InOutsideApproach,
	const FVector& InInsideApproach)
{
	Door = InDoor;
	Room = InRoom;
	OutsideApproach = InOutsideApproach;
	InsideApproach = InInsideApproach;
}

void AIGBathroomRefuge::BeginPlay()
{
	Super::BeginPlay();
	GetWorldTimerManager().SetTimer(
		ConcealTimer, this, &AIGBathroomRefuge::RefreshConcealment, IGBathroom::ConcealCheckSeconds, true);
}

void AIGBathroomRefuge::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ConcealTimer);
	if (bConcealed)
	{
		if (AIGPlayerCharacter* Player = Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
		{
			Player->SetLockedRoomConcealed(false);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool AIGBathroomRefuge::IsSealed() const
{
	const AIGSwingDoor* RoomDoor = Door.Get();
	return RoomDoor && RoomDoor->IsFullyClosed() && RoomDoor->IsLatched();
}

AIGBathroomRefuge* AIGBathroomRefuge::FindRoomAt(const UWorld* World, const FVector& Location)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AIGBathroomRefuge> It(World); It; ++It)
	{
		if (It->IsInside(Location))
		{
			return *It;
		}
	}
	return nullptr;
}

bool AIGBathroomRefuge::CanInteract_Implementation(AActor* Interactor) const
{
	const AIGSwingDoor* RoomDoor = Door.Get();
	return Super::CanInteract_Implementation(Interactor)
		&& RoomDoor
		&& RoomDoor->IsFullyClosed()
		&& Interactor
		&& IsInside(Interactor->GetActorLocation());
}

FText AIGBathroomRefuge::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	const AIGSwingDoor* RoomDoor = Door.Get();
	return RoomDoor && RoomDoor->IsLatched()
		? NSLOCTEXT("IGMissingFloor", "BathroomUnlockPrompt", "잠금 풀기")
		: NSLOCTEXT("IGMissingFloor", "BathroomLockPrompt", "문 잠그기");
}

void AIGBathroomRefuge::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	Super::CompleteInteraction_Implementation(Context);
	AIGSwingDoor* RoomDoor = Door.Get();
	if (!RoomDoor || !RoomDoor->IsFullyClosed())
	{
		return;
	}
	const bool bLock = !RoomDoor->IsLatched();
	RoomDoor->SetLatched(bLock, true);
	if (bLock)
	{
		// 처음 잠글 때 한 번. 잠갔지만 얇은 문 한 장이다.
		const UGameInstance* GameInstance = GetGameInstance();
		UIGMissingFloorNarrativeSubsystem* Narrative = GameInstance
			? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
			: nullptr;
		if (Narrative && Narrative->MarkBeatPlayed(FName(TEXT("Home.BathroomLocked"))))
		{
			AIGHorrorHUD::PushThought(
				this, NSLOCTEXT("IGMissingFloor", "YudamBathroomLocked", "잠그긴 했는데 문이 얇아."), 2.6f);
		}
	}
	RefreshConcealment();
}

void AIGBathroomRefuge::RefreshConcealment()
{
	AIGPlayerCharacter* Player = Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Player)
	{
		return;
	}
	const bool bNow = IsSealed() && IsInside(Player->GetActorLocation());
	if (bNow == bConcealed)
	{
		return;
	}
	bConcealed = bNow;
	Player->SetLockedRoomConcealed(bNow);
	if (UIGNoiseSubsystem* Noise = GetWorld() ? GetWorld()->GetSubsystem<UIGNoiseSubsystem>() : nullptr)
	{
		// 숨는 가구가 쓰는 자리와 같다. 욕실 안에서는 가구에 들어갈 수 없으니 겹치지 않는다.
		Noise->SetInstigatorMuffle(Player, bNow ? IGBathroom::RoomNoiseScale : 1.0f);
	}
}
