#include "Interaction/IGDoorLatch.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Interaction/IGSwingDoor.h"
#include "Materials/MaterialInterface.h"

AIGDoorLatch::AIGDoorLatch()
{
	LatchRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LatchRoot"));
	SetRootComponent(LatchRoot);

	// 걸쇠는 작다. 시선 추적이 잡기 쉽게 보이는 것보다 조금 큰 상자를 둔다.
	FocusBox = CreateDefaultSubobject<UBoxComponent>(TEXT("FocusBox"));
	FocusBox->SetupAttachment(LatchRoot);
	FocusBox->SetBoxExtent(FVector(9.0f, 3.0f, 6.0f));
	FocusBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FocusBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	FocusBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	FocusBox->SetCanEverAffectNavigation(false);

	Plate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plate"));
	Plate->SetupAttachment(LatchRoot);
	Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plate->SetCastShadow(false);

	Bolt = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bolt"));
	Bolt->SetupAttachment(LatchRoot);
	Bolt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bolt->SetCastShadow(false);
}

void AIGDoorLatch::BindDoor(AIGSwingDoor* InDoor)
{
	if (AIGSwingDoor* Previous = Door.Get())
	{
		Previous->OnLatchChanged.RemoveAll(this);
	}
	Door = InDoor;
	if (InDoor)
	{
		// 문짝에 붙어 문과 같이 돈다. 안에서 문을 열어 걸쇠가 풀리면 막대도 돌아온다.
		AttachToComponent(InDoor->GetDoorPivot(), FAttachmentTransformRules::KeepWorldTransform);
		InDoor->OnLatchChanged.AddUObject(this, &AIGDoorLatch::RefreshBoltPose);
	}
}

void AIGDoorLatch::Configure(AIGSwingDoor* InDoor, UStaticMesh* CubeMesh, UMaterialInterface* Material)
{
	BindDoor(InDoor);
	if (CubeMesh)
	{
		// 받침판 12×1×5 cm, 쇠막대 9×1.4×1.4 cm. 기본 큐브는 100 cm다.
		Plate->SetStaticMesh(CubeMesh);
		Plate->SetRelativeScale3D(FVector(0.12f, 0.01f, 0.05f));
		Bolt->SetStaticMesh(CubeMesh);
		Bolt->SetRelativeScale3D(FVector(0.09f, 0.014f, 0.014f));
		if (Material)
		{
			Plate->SetMaterial(0, Material);
			Bolt->SetMaterial(0, Material);
		}
	}
	RefreshBoltPose();
}

void AIGDoorLatch::ConfigureAuthored(
	AIGSwingDoor* InDoor,
	UStaticMesh* HousingMesh,
	UStaticMesh* PinMesh,
	const float InTravelCm,
	const FText& InLockPrompt,
	const FText& InUnlockPrompt)
{
	BindDoor(InDoor);
	BoltTravelCm = InTravelCm;
	BoltRestOffset = FVector::ZeroVector;
	LockPrompt = InLockPrompt;
	UnlockPrompt = InUnlockPrompt;
	// 몸통 15 x 4.4 cm가 문 면에서 2 cm 솟는다. 초점 상자도 그만하게 둔다.
	FocusBox->SetBoxExtent(FVector(8.0f, 1.6f, 3.0f));
	FocusBox->SetRelativeLocation(FVector(-1.5f, 1.4f, 0.0f));
	for (UStaticMeshComponent* Part : {Plate.Get(), Bolt.Get()})
	{
		Part->EmptyOverrideMaterials();
		Part->SetRelativeScale3D(FVector::OneVector);
	}
	Plate->SetStaticMesh(HousingMesh);
	Bolt->SetStaticMesh(PinMesh);
	RefreshBoltPose();
}

bool AIGDoorLatch::CanInteract_Implementation(AActor* Interactor) const
{
	const AIGSwingDoor* LatchedDoor = Door.Get();
	return Super::CanInteract_Implementation(Interactor)
		&& LatchedDoor
		&& LatchedDoor->IsFullyClosed();
}

FText AIGDoorLatch::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	const AIGSwingDoor* LatchedDoor = Door.Get();
	if (LatchedDoor && LatchedDoor->IsLatched())
	{
		return UnlockPrompt.IsEmpty() ? NSLOCTEXT("IGMissingFloor", "DoorLatchOpenPrompt", "걸쇠 풀기") : UnlockPrompt;
	}
	return LockPrompt.IsEmpty() ? NSLOCTEXT("IGMissingFloor", "DoorLatchClosePrompt", "걸쇠 걸기") : LockPrompt;
}

void AIGDoorLatch::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	Super::CompleteInteraction_Implementation(Context);
	AIGSwingDoor* LatchedDoor = Door.Get();
	if (!LatchedDoor || !LatchedDoor->IsFullyClosed())
	{
		return;
	}
	LatchedDoor->SetLatched(!LatchedDoor->IsLatched(), true);
}

void AIGDoorLatch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AIGSwingDoor* LatchedDoor = Door.Get())
	{
		LatchedDoor->OnLatchChanged.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AIGDoorLatch::RefreshBoltPose()
{
	// 액터의 +X가 문틀 쪽이다. 걸면 막대가 받침 밖으로 밀려 나간다.
	const AIGSwingDoor* LatchedDoor = Door.Get();
	const bool bLatched = LatchedDoor && LatchedDoor->IsLatched();
	Bolt->SetRelativeLocation(BoltRestOffset + FVector(bLatched ? BoltTravelCm : 0.0f, 0.0f, 0.0f));
}
