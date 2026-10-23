#include "Interaction/IGFridge.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGAmbienceSoundWave.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Narrative/IGStoryHelpers.h"
#include "Player/IGHorrorHUD.h"
#include "TimerManager.h"

namespace IGFridge
{
	// Exterior footprint in centimeters; the shell is assembled from cube panels.
	constexpr float BodyWidth = 66.0f;   // X: door-to-back depth
	constexpr float BodyDepth = 72.0f;   // Y: side-to-side width
	constexpr float BodyHeight = 158.0f; // Z
	constexpr float PanelThickness = 6.0f;

	// §5.1 서랍·캐비닛 행. 이 게임에서 그 행을 쓰는 건 냉장고 문 하나다.
	// 닫는 쪽이 조용한 건 고무 패킹이 소리를 먹기 때문이고, 그래서
	// 열어 두고 도망치는 것과 닫고 도망치는 것의 값이 다르다.
	constexpr float OpenLoudness = 0.2f;
	constexpr float CloseLoudness = 0.15f;
}

AIGFridge::AIGFridge()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	FridgeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FridgeRoot"));
	SetRootComponent(FridgeRoot);

	// The pivot sits on the front-left vertical edge so the door swings like a hinge.
	DoorPivot = CreateDefaultSubobject<USceneComponent>(TEXT("DoorPivot"));
	DoorPivot->SetupAttachment(FridgeRoot);
	DoorPivot->SetRelativeLocation(FVector(
		-IGFridge::BodyWidth * 0.5f,
		-IGFridge::BodyDepth * 0.5f,
		IGFridge::BodyHeight * 0.5f));

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(DoorPivot);
	DoorMesh->SetRelativeLocation(FVector(
		-IGFridge::PanelThickness * 0.5f,
		IGFridge::BodyDepth * 0.5f,
		0.0f));
	DoorMesh->SetRelativeScale3D(FVector(
		IGFridge::PanelThickness,
		IGFridge::BodyDepth,
		IGFridge::BodyHeight) / 100.0f);
	DoorMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	DoorMesh->SetGenerateOverlapEvents(false);
	DoorMesh->SetCanEverAffectNavigation(false);
	DoorMesh->SetMobility(EComponentMobility::Movable);

	HandleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandleMesh"));
	HandleMesh->SetupAttachment(DoorPivot);
	HandleMesh->SetRelativeLocation(FVector(
		-IGFridge::PanelThickness - 6.0f,
		IGFridge::BodyDepth - 10.0f,
		6.0f));
	HandleMesh->SetRelativeScale3D(FVector(0.025f, 0.032f, 0.58f));
	HandleMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	HandleMesh->SetGenerateOverlapEvents(false);
	HandleMesh->SetCanEverAffectNavigation(false);
	HandleMesh->SetMobility(EComponentMobility::Movable);

	InteriorLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("InteriorLight"));
	InteriorLight->SetupAttachment(FridgeRoot);
	InteriorLight->SetRelativeLocation(FVector(-8.0f, 0.0f, IGFridge::BodyHeight * 0.62f));
	InteriorLight->SetMobility(EComponentMobility::Movable);
	InteriorLight->SetIntensity(0.0f);
	InteriorLight->SetAttenuationRadius(220.0f);
	InteriorLight->SetLightColor(FLinearColor(1.0f, 0.96f, 0.88f));
	// The open door carves a hard shadow wedge across the dark room.
	InteriorLight->SetCastShadows(true);
	InteriorLight->SetSourceRadius(5.0f);
	InteriorLight->ContactShadowLength = 0.05f;

	HumAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("HumAudio"));
	HumAudioComponent->SetupAttachment(FridgeRoot);
	// 뒤판 아래 압축기 자리. 원점은 바닥이라 거기서 울리면 바닥 면에 걸려 먹먹해진다.
	HumAudioComponent->SetRelativeLocation(FVector(24.0f, 0.0f, 28.0f));
	HumAudioComponent->bAutoActivate = false;

	InteractionPrompt = NSLOCTEXT("IGFridge", "OpenPrompt", "열기");
	InspectionThought = NSLOCTEXT("IGFridge", "NoWater", "텅 비었네. 물이라도 좀 사다 놔야겠다.");
	InteriorShelfLocalCenter = FVector(4.0f, 0.0f, IGFridge::BodyHeight * 0.52f);
}

void AIGFridge::ConfigurePrototypeVisuals(
	UStaticMesh* CubeMesh,
	UStaticMesh* CylinderMesh,
	UMaterialInterface* BodyMaterial,
	UMaterialInterface* InteriorMaterial,
	UMaterialInterface* HandleMaterial,
	UMaterialInterface* GlassMaterial,
	UMaterialInterface* MetalMaterial,
	UMaterialInterface* AccentRedMaterial,
	UMaterialInterface* AccentYellowMaterial,
	UMaterialInterface* BottleGreenMaterial)
{
	if (bVisualsConfigured || !CubeMesh)
	{
		return;
	}
	bVisualsConfigured = true;

	using namespace IGFridge;

	// Blender 냉장고(Scripts/blender/build_fridge.py)가 있으면 본체와 문짝은
	// 그 메시다. 껍데기 다섯 판, 문 상세, 선반·서랍·냉기 패널이 메시 안에
	// 있고 재질도 메시가 들고 온다. 내용물(김치통·우유·소주·반찬통)과 실내등,
	// 문 열림 로직은 그대로다. 문짝 원점이 힌지 축의 피벗 높이라 DoorPivot에
	// 상대 변환 없이 붙는다.
	UStaticMesh* AuthoredBody = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_FridgeBody.SM_FridgeBody"));
	UStaticMesh* AuthoredDoor = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_FridgeDoor.SM_FridgeDoor"));
	const bool bAuthored = AuthoredBody != nullptr && AuthoredDoor != nullptr;

	if (bAuthored)
	{
		DoorMesh->SetStaticMesh(AuthoredDoor);
		DoorMesh->SetRelativeLocation(FVector::ZeroVector);
		DoorMesh->SetRelativeScale3D(FVector::OneVector);
		HandleMesh->SetStaticMesh(nullptr);
		HandleMesh->SetVisibility(false, true);

		UStaticMeshComponent* BodyMesh = NewObject<UStaticMeshComponent>(
			this, TEXT("FridgeBodyAuthored"));
		BodyMesh->SetupAttachment(FridgeRoot);
		BodyMesh->SetStaticMesh(AuthoredBody);
		BodyMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		BodyMesh->SetGenerateOverlapEvents(false);
		BodyMesh->SetCanEverAffectNavigation(false);
		BodyMesh->SetMobility(EComponentMobility::Movable);
		BodyMesh->RegisterComponent();
		ShellMeshes.Add(BodyMesh);
	}
	else
	{
		DoorMesh->SetStaticMesh(CubeMesh);
		DoorMesh->SetMaterial(0, BodyMaterial);
		HandleMesh->SetStaticMesh(CubeMesh);
		HandleMesh->SetMaterial(0, HandleMaterial);
	}

	struct FPanelSpec
	{
		FVector Center;
		FVector Size;
		UMaterialInterface* Material;
	};

	const float InteriorHalfHeight = BodyHeight * 0.5f;
	const FPanelSpec Panels[] = {
		// Back, left, right, top, bottom shell panels (hollow interior).
		{{BodyWidth * 0.5f - PanelThickness * 0.5f, 0.0f, InteriorHalfHeight},
			{PanelThickness, BodyDepth, BodyHeight}, BodyMaterial},
		{{0.0f, -BodyDepth * 0.5f + PanelThickness * 0.5f, InteriorHalfHeight},
			{BodyWidth, PanelThickness, BodyHeight}, BodyMaterial},
		{{0.0f, BodyDepth * 0.5f - PanelThickness * 0.5f, InteriorHalfHeight},
			{BodyWidth, PanelThickness, BodyHeight}, BodyMaterial},
		{{0.0f, 0.0f, BodyHeight - PanelThickness * 0.5f},
			{BodyWidth, BodyDepth, PanelThickness}, BodyMaterial},
		{{0.0f, 0.0f, PanelThickness * 0.5f},
			{BodyWidth, BodyDepth, PanelThickness}, BodyMaterial},
	};

	int32 PanelIndex = 0;
	for (const FPanelSpec& Panel : Panels)
	{
		if (bAuthored)
		{
			break;
		}
		UStaticMeshComponent* PanelMesh = NewObject<UStaticMeshComponent>(
			this,
			*FString::Printf(TEXT("FridgePanel%d"), PanelIndex++));
		PanelMesh->SetupAttachment(FridgeRoot);
		PanelMesh->SetStaticMesh(CubeMesh);
		PanelMesh->SetMaterial(0, Panel.Material);
		PanelMesh->SetRelativeLocation(Panel.Center);
		PanelMesh->SetRelativeScale3D(Panel.Size / 100.0f);
		PanelMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		PanelMesh->SetGenerateOverlapEvents(false);
		PanelMesh->SetCanEverAffectNavigation(false);
		PanelMesh->SetMobility(EComponentMobility::Movable);
		PanelMesh->RegisterComponent();
		ShellMeshes.Add(PanelMesh);
	}

	// Non-colliding appliance detailing: what makes it read as a fridge.
	auto AddDetail = [this, CubeMesh, GlassMaterial](
		USceneComponent* Parent, UMaterialInterface* Material,
		const FVector& Center, const FVector& Size)
	{
		UStaticMeshComponent* Detail = NewObject<UStaticMeshComponent>(
			this,
			*FString::Printf(TEXT("FridgeDetail%d"), ShellMeshes.Num() + 100));
		Detail->SetupAttachment(Parent);
		Detail->SetStaticMesh(CubeMesh);
		Detail->SetMaterial(0, Material);
		Detail->SetRelativeLocation(Center);
		Detail->SetRelativeScale3D(Size / 100.0f);
		Detail->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Detail->SetGenerateOverlapEvents(false);
		Detail->SetCanEverAffectNavigation(false);
		Detail->SetMobility(EComponentMobility::Movable);
		if (Material == GlassMaterial)
		{
			Detail->SetCastShadow(false);
		}
		Detail->RegisterComponent();
		ShellMeshes.Add(Detail);
	};

	// --- interior: what an actual studio fridge looks like inside ---------
	// Glass shelves with steel front trim; the middle shelf anchors the
	// rolling empty bottle. Bottom crisper drawer with a clear front.
	// 저작 본체는 이 모든 것을 메시로 들고 있다.
	for (const float ShelfZ : {BodyHeight * 0.50f, BodyHeight * 0.26f})
	{
		if (bAuthored)
		{
			break;
		}
		AddDetail(FridgeRoot, GlassMaterial,
			FVector(2.0f, 0.0f, ShelfZ),
			FVector(BodyWidth - PanelThickness * 2.5f, BodyDepth - PanelThickness * 2.0f, 1.6f));
		AddDetail(FridgeRoot, MetalMaterial,
			FVector(-BodyWidth * 0.5f + PanelThickness + 2.0f, 0.0f, ShelfZ),
			FVector(2.0f, BodyDepth - PanelThickness * 2.0f, 2.4f));
	}
	if (!bAuthored)
	{
		AddDetail(FridgeRoot, InteriorMaterial,
			FVector(4.0f, 0.0f, 18.0f), FVector(BodyWidth - 18.0f, BodyDepth - 16.0f, 24.0f));
		AddDetail(FridgeRoot, GlassMaterial,
			FVector(-BodyWidth * 0.5f + PanelThickness + 3.0f, 0.0f, 19.0f),
			FVector(1.6f, BodyDepth - 18.0f, 22.0f));
		AddDetail(FridgeRoot, MetalMaterial,
			FVector(-BodyWidth * 0.5f + PanelThickness + 3.0f, 0.0f, 31.0f),
			FVector(2.0f, BodyDepth - 22.0f, 2.0f));

		// Back cooling panel with vent slits, and the interior lamp housing.
		AddDetail(FridgeRoot, InteriorMaterial,
			FVector(BodyWidth * 0.5f - PanelThickness - 2.0f, 0.0f, BodyHeight * 0.62f),
			FVector(3.5f, BodyDepth - 26.0f, 52.0f));
		for (int32 SlitIndex = 0; SlitIndex < 3; ++SlitIndex)
		{
			AddDetail(FridgeRoot, HandleMaterial,
				FVector(BodyWidth * 0.5f - PanelThickness - 4.0f, 0.0f,
					BodyHeight * 0.55f + SlitIndex * 7.0f),
				FVector(0.8f, BodyDepth - 34.0f, 1.4f));
		}
		AddDetail(FridgeRoot, InteriorMaterial,
			FVector(2.0f, 0.0f, BodyHeight - PanelThickness - 3.0f),
			FVector(18.0f, 12.0f, 3.5f));

		// Door pockets on the inside face of the door.
		for (const float PocketZ : {-42.0f, 8.0f})
		{
			AddDetail(DoorPivot, InteriorMaterial,
				FVector(2.4f, BodyDepth * 0.5f, PocketZ),
				FVector(9.0f, BodyDepth - 18.0f, 2.0f));
			AddDetail(DoorPivot, InteriorMaterial,
				FVector(6.8f, BodyDepth * 0.5f, PocketZ + 6.0f),
				FVector(1.6f, BodyDepth - 18.0f, 9.0f));
		}
	}

	// Leftovers with no water anywhere. The tub, carton and soju are authored
	// meshes (lathed / gable-extruded); the small side dishes stay simple.
	auto AddContentMesh = [this](
		UStaticMesh* Mesh, UMaterialInterface* Material,
		const FVector& BaseLocation, float YawDegrees) -> bool
	{
		if (!Mesh)
		{
			return false;
		}
		UStaticMeshComponent* Content = NewObject<UStaticMeshComponent>(
			this,
			*FString::Printf(TEXT("FridgeContent%d"), ShellMeshes.Num() + 400));
		Content->SetupAttachment(FridgeRoot);
		Content->SetStaticMesh(Mesh);
		Content->SetMaterial(0, Material);
		Content->SetRelativeLocation(BaseLocation);
		Content->SetRelativeRotation(FRotator(0.0f, YawDegrees, 0.0f));
		Content->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Content->SetGenerateOverlapEvents(false);
		Content->SetCanEverAffectNavigation(false);
		Content->SetMobility(EComponentMobility::Movable);
		Content->RegisterComponent();
		ShellMeshes.Add(Content);
		return true;
	};

	const float UpperShelfTop = BodyHeight * 0.50f + 0.8f;
	const float LowerShelfTop = BodyHeight * 0.26f + 0.8f;

	if (!AddContentMesh(ContentMeshes.KimchiTub, InteriorMaterial,
		FVector(6.0f, -14.0f, LowerShelfTop), 22.0f))
	{
		AddDetail(FridgeRoot, InteriorMaterial,
			FVector(4.0f, -14.0f, LowerShelfTop + 6.0f), FVector(16.0f, 16.0f, 12.0f));
	}
	if (!AddContentMesh(ContentMeshes.MilkCarton, InteriorMaterial,
		FVector(4.0f, 12.0f, LowerShelfTop), -14.0f))
	{
		AddDetail(FridgeRoot, InteriorMaterial,
			FVector(6.0f, 8.0f, LowerShelfTop + 8.0f), FVector(7.5f, 7.5f, 17.0f));
	}
	if (!AddContentMesh(ContentMeshes.SojuBottle, BottleGreenMaterial,
		FVector(2.0f, 16.0f, UpperShelfTop), 40.0f))
	{
		AddDetail(FridgeRoot, BottleGreenMaterial,
			FVector(2.0f, 16.0f, UpperShelfTop + 10.0f), FVector(7.0f, 7.0f, 20.0f));
	}
	// Two stacked side-dish containers with their lids.
	AddDetail(FridgeRoot, InteriorMaterial,
		FVector(8.0f, -12.0f, UpperShelfTop + 5.0f), FVector(18.0f, 15.0f, 9.0f));
	AddDetail(FridgeRoot, AccentYellowMaterial,
		FVector(8.0f, -12.0f, UpperShelfTop + 10.0f), FVector(19.0f, 16.0f, 1.8f));
	AddDetail(FridgeRoot, InteriorMaterial,
		FVector(9.0f, 6.0f, UpperShelfTop + 3.5f), FVector(13.0f, 13.0f, 6.5f));
	AddDetail(FridgeRoot, AccentRedMaterial,
		FVector(9.0f, 6.0f, UpperShelfTop + 7.4f), FVector(14.0f, 14.0f, 1.4f));
	// Gochujang tube standing in the door pocket.
	AddDetail(DoorPivot, AccentRedMaterial,
		FVector(4.0f, BodyDepth * 0.5f - 12.0f, 14.0f), FVector(4.5f, 4.5f, 13.0f));

	if (bAuthored)
	{
		// 문 상세·힌지·통풍 슬릿은 저작 메시가 들고 있다.
		return;
	}

	const float DoorFrontX = -PanelThickness - 0.6f;
	// Inset face plate with a shadow seam, and the rubber gasket outline.
	AddDetail(DoorPivot, BodyMaterial,
		FVector(DoorFrontX - 0.6f, BodyDepth * 0.5f, 4.0f),
		FVector(1.6f, BodyDepth - 12.0f, BodyHeight - 20.0f));
	AddDetail(DoorPivot, HandleMaterial,
		FVector(DoorFrontX, BodyDepth * 0.5f, BodyHeight * 0.5f - 1.5f),
		FVector(0.8f, BodyDepth - 4.0f, 2.0f));
	AddDetail(DoorPivot, HandleMaterial,
		FVector(DoorFrontX, BodyDepth * 0.5f, -BodyHeight * 0.5f + 4.5f),
		FVector(0.8f, BodyDepth - 4.0f, 2.0f));
	AddDetail(DoorPivot, HandleMaterial,
		FVector(DoorFrontX, 2.0f, 1.5f),
		FVector(0.8f, 2.0f, BodyHeight - 10.0f));
	AddDetail(DoorPivot, HandleMaterial,
		FVector(DoorFrontX, BodyDepth - 2.0f, 1.5f),
		FVector(0.8f, 2.0f, BodyHeight - 10.0f));
	// Handle stand-offs connecting the bar to the door skin.
	AddDetail(DoorPivot, HandleMaterial,
		FVector(-PanelThickness - 3.0f, BodyDepth - 10.0f, 28.0f),
		FVector(4.0f, 2.6f, 4.0f));
	AddDetail(DoorPivot, HandleMaterial,
		FVector(-PanelThickness - 3.0f, BodyDepth - 10.0f, -18.0f),
		FVector(4.0f, 2.6f, 4.0f));
	// Hinge knuckles on the pivot edge.
	AddDetail(FridgeRoot, HandleMaterial,
		FVector(-BodyWidth * 0.5f - 1.0f, -BodyDepth * 0.5f - 1.5f, BodyHeight - 6.0f),
		FVector(5.0f, 5.0f, 8.0f));
	AddDetail(FridgeRoot, HandleMaterial,
		FVector(-BodyWidth * 0.5f - 1.0f, -BodyDepth * 0.5f - 1.5f, 10.0f),
		FVector(5.0f, 5.0f, 8.0f));
	// Compressor vent slats low on the side panel.
	for (int32 SlatIndex = 0; SlatIndex < 3; ++SlatIndex)
	{
		AddDetail(FridgeRoot, HandleMaterial,
			FVector(6.0f, BodyDepth * 0.5f + 0.4f, 7.0f + SlatIndex * 4.5f),
			FVector(BodyWidth - 28.0f, 0.8f, 1.6f));
	}
}

void AIGFridge::BeginPlay()
{
	Super::BeginPlay();

	if (!InspectedStateTag.IsValid())
	{
		InspectedStateTag = FGameplayTag::RequestGameplayTag(
			FName(TEXT("State.CH01.Morning.FridgeChecked")),
			false);
	}

	if (!InteractionTag.IsValid())
	{
		InteractionTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interaction.Fridge")), false);
	}

	// The compressor hum runs for as long as the fridge has power.
	UIGAmbienceSoundWave* HumWave = NewObject<UIGAmbienceSoundWave>(this, TEXT("FridgeHumWave"));
	HumWave->Configure(EIGAmbienceMode::RoomTone, 0x0F51D6E1u);
	HumAudioComponent->SetSound(HumWave);
	// 다른 베드와 같은 감쇠를 쓴다. 벽 너머에서는 먹먹해지고 멀수록 젖는다.
	// 냉장고 몸통은 주인 액터라 오클루전이 알아서 뺀다.
	HumAudioComponent->AttenuationSettings = IGAudio::MakeAttenuation(
		this, 90.0f, 620.0f, EIGAudioBus::World);
	HumAudioComponent->bAllowSpatialization = true;
	// WORLD 버스의 믹스·더킹·환경음 슬라이더를 받는다. 늘 우는 소리라 상한에는
	// 세지 않는다. 버스 밖에 있으면 그가 귀를 기울여도, 침묵을 걸어도 혼자 운다.
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->RegisterPersistentBed(HumAudioComponent, EIGAudioBus::World);
		}
	}
	HumAudioComponent->SetVolumeMultiplier(0.85f);
	HumAudioComponent->Play();

	if (IGStory::HasState(this, InspectedStateTag))
	{
		bInspectionDone = true;
	}
}

void AIGFridge::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const bool bFinished = DoorAnimation.Advance(DeltaSeconds);
	DoorPivot->SetRelativeRotation(FRotator(0.0f, DoorAnimation.CurrentValue, 0.0f));

	if (bFinished)
	{
		SetActorTickEnabled(false);

		if (bDoorOpen && !bInspectionDone)
		{
			GetWorldTimerManager().SetTimer(
				InspectionTimerHandle,
				this,
				&ThisClass::HandleInspectionTimer,
				FMath::Max(InspectionDelay, 0.01f),
				false);
		}

		if (!bDoorOpen)
		{
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateDoorThud(this),
				DoorPivot->GetComponentLocation(),
				0.65f,
				1.25f);
		}
	}
}

bool AIGFridge::CanInteract_Implementation(AActor* Interactor) const
{
	// 문이 도는 동안도 초점은 남긴다. BeginDoorSwing이 움직이는 중 입력을 흘린다.
	return Super::CanInteract_Implementation(Interactor);
}

FText AIGFridge::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return bDoorOpen
		? NSLOCTEXT("IGFridge", "ClosePrompt", "닫기")
		: NSLOCTEXT("IGFridge", "OpenPrompt", "열기");
}

void AIGFridge::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	Super::CompleteInteraction_Implementation(Context);
	BeginDoorSwing(!bDoorOpen);
}

void AIGFridge::BeginDoorSwing(const bool bOpen)
{
	if (DoorAnimation.bActive || bDoorOpen == bOpen)
	{
		return;
	}

	if (!bOpen)
	{
		// 회전하는 자식 컴포넌트는 스윕을 못 한다. 문짝이 지나갈 자리에 사람이
		// 서 있으면 닫지 않는다 — 118도를 0.85초에 도는 판이 캡슐을 밀어낸다.
		const APlayerController* PlayerController =
			GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		FVector ClosestPoint = FVector::ZeroVector;
		const float DistanceToLeaf = Pawn
			? DoorMesh->GetDistanceToCollision(Pawn->GetActorLocation(), ClosestPoint)
			: -1.0f;
		if (DistanceToLeaf >= 0.0f && DistanceToLeaf < 55.0f)
		{
			return;
		}
	}
	bDoorOpen = bOpen;
	// 열린 문짝은 지나다닐 수 있고, 닫힌 문짝만 막는다. 여닫이문과 같은 규칙.
	DoorMesh->SetCollisionProfileName(
		bOpen
			? UCollisionProfile::NoCollision_ProfileName
			: UCollisionProfile::BlockAll_ProfileName);
	GetWorldTimerManager().ClearTimer(InspectionTimerHandle);
	DoorAnimation.Begin(
		DoorPivot->GetRelativeRotation().Yaw,
		bOpen ? OpenYaw : 0.0f,
		DoorAnimDuration);
	SetActorTickEnabled(true);
	UpdateHumIntensity();

	if (bOpen)
	{
		// Suction seal pop as the door releases.
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateDoorThud(this),
			DoorPivot->GetComponentLocation(),
			0.4f,
			1.6f);
	}

	// One report per committed swing (§5.1, drawer/cabinet band). The seal pop
	// is the louder half, so opening carries a little further than closing.
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(
				DoorPivot->GetComponentLocation(),
				bOpen ? IGFridge::OpenLoudness : IGFridge::CloseLoudness,
				this);
		}
	}
}

void AIGFridge::HandleInspectionTimer()
{
	if (!bDoorOpen || bInspectionDone)
	{
		return;
	}

	bInspectionDone = true;
	IGStory::AddState(this, InspectedStateTag);
	AIGHorrorHUD::PushThought(this, InspectionThought, 4.2f);
	OnFridgeInspected.Broadcast(this);
}

void AIGFridge::UpdateHumIntensity()
{
	InteriorLight->SetIntensity(bDoorOpen ? 420.0f : 0.0f);
	HumAudioComponent->SetVolumeMultiplier(bDoorOpen ? 1.25f : 0.85f);
}

FTransform AIGFridge::GetInteriorShelfTransform() const
{
	return FTransform(
		GetActorRotation(),
		GetActorTransform().TransformPosition(InteriorShelfLocalCenter));
}

void AIGFridge::ApplyRestoredInspectedState()
{
	bInspectionDone = true;
}

void AIGFridge::ConfigureChapterState(
	const FGameplayTag InInspectedStateTag,
	const FText& InInspectionThought)
{
	if (InInspectedStateTag.IsValid())
	{
		InspectedStateTag = InInspectedStateTag;
	}
	InspectionThought = InInspectionThought;
}

