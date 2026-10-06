#include "Core/IGPrologueWorldScene.h"
#include "Environment/IGStoreClerk.h"
#include "Accessibility/IGAccessibilitySubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Audio/IGAmbienceSoundWave.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Environment/IGSettledDustComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Texture2D.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/IGNeighborhoodLifeDirector.h"
#include "Entity/IGListenerEntity.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "IndieGame.h"
#include "Kismet/GameplayStatics.h"
#include "Interaction/IGBatteryPickup.h"
#include "Interaction/IGCheckoutCounter.h"
#include "Interaction/IGDoorLatch.h"
#include "Interaction/IGElevator.h"
#include "Interaction/IGFireDoorWedge.h"
#include "Interaction/IGFridge.h"
#include "Interaction/IGHidingSpot.h"
#include "Interaction/IGInspectable.h"
#include "Interaction/IGInteractable.h"
#include "Interaction/IGInteractableActor.h"
#include "Interaction/IGPickupItem.h"
#include "Interaction/IGReadableNote.h"
#include "Interaction/IGSlidingDoor.h"
#include "Interaction/IGSwingDoor.h"
#include "Interaction/IGZoneTrigger.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGStoryHelpers.h"
#include "Narrative/IGStoryStateSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace IGPrologueWorld
{
	constexpr int32 MaxPlayerPositionAttempts = 30;

	// The player wakes beside the bed facing the nightstand, so the ringing
	// alarm is the first thing in view and the first prompt is reachable.
	// Keep the 72 cm player capsule clear of the mattress edge. The old X=-85
	// start penetrated the bed by roughly 30 cm and produced a violent
	// depenetration/camera shove on both mornings.
	const FVector PlayerLocation(-48.0f, 60.0f, 997.0f);
	const FRotator PlayerActorRotation(0.0f, -128.0f, 0.0f);
	const FRotator PlayerViewRotation(-14.0f, -128.0f, 0.0f);

	// 403호가 앉은 층. 값은 클래스가 든다.
	constexpr float FourthFloorZ = AIGPrologueWorldScene::FourthFloorZ;
	constexpr float MissingFloorRoofZ = 1200.0f;
	constexpr int32 MissingFloorUpperStepCount = 14;
	constexpr float MissingFloorRouteLengthCentimeters = 640.0f;
	const FVector MissingFloorRouteStart(-277.5f, 220.0f, MissingFloorRoofZ);
	const FVector MissingFloorRouteCorner(130.0f, 220.0f, MissingFloorRoofZ);
	const FVector MissingFloorRouteEnd(130.0f, 452.5f, MissingFloorRoofZ);

	/**
	 * 서쪽 계단탑. 1층 필로티 옆에서 4층 복도 끝까지 한 줄로 이어진 꺾인
	 * 계단이고, 층마다 남쪽 참이 층 바닥 높이에서 동쪽 복도로 열린다. 북쪽 참은
	 * 반 층이다. 서쪽 띠는 층에서 북쪽으로 반 층 참까지 오르고, 동쪽 띠는 그
	 * 참에서 남쪽으로 올라 다음 층에 닿는다. 순찰하는 관리인도 이 숫자로 걷는다.
	 */
	constexpr float StairCoreWestX = -590.0f;
	constexpr float StairCoreEastX = -340.0f;
	constexpr float StairCoreSouthY = -395.0f;
	constexpr float StairCoreNorthY = 120.0f;
	constexpr float StairInnerWestX = -575.0f;
	constexpr float StairInnerEastX = -355.0f;
	constexpr float StairInnerSouthY = -375.0f;
	constexpr float StairInnerNorthY = 105.0f;
	// 두 띠 사이는 10 cm 벽이다. 서쪽 띠는 -575..-470, 동쪽 띠는 -460..-355.
	constexpr float StairWestBandEastX = -470.0f;
	constexpr float StairEastBandWestX = -460.0f;
	// 남쪽 참은 Y -375..-235, 계단은 -235..-35, 북쪽 참은 -35..105.
	constexpr float StairFlightSouthY = -235.0f;
	constexpr float StairFlightNorthY = -35.0f;
	constexpr float StairStoreyHeight = 300.0f;
	// 한 층에 열여덟 단. 단높이 16.7 cm, 단너비 25 cm다.
	constexpr float StairRise = StairStoreyHeight / 18.0f;
	constexpr float StairGoing = (StairFlightNorthY - StairFlightSouthY) / 8.0f;
	constexpr float StairTreadThickness = 25.0f;

	/**
	 * §14 CCTV 채널 5's vantage — corner-mounted high in the annex, looking
	 * diagonally across the stalled material toward bay B. 92° is a 2.8 mm lens
	 * on a third-inch sensor, which is what actually gets installed in a corridor
	 * this narrow, and it is wide enough that the low shape can cross an edge
	 * instead of walking through the middle of the picture.
	 *
	 * §17 requires the player to recognise this framing by eye in 밤3, so the
	 * camera prop, the scene capture and the night-3 observation point all read
	 * these three values. There is exactly one shot and this is it.
	 */
	const FVector CctvCameraLocation(-355.0f, 486.0f, 1404.0f);
	const FRotator CctvCameraRotation(-25.0f, 29.0f, 0.0f);
	/**
	 * 78° is a 4 mm lens, not the 2.8 mm the first pass used. The wider glass put
	 * a third of the frame on the ceiling 36 cm above the housing, which measured
	 * as a perfectly bright picture and showed nothing. Judged from the exported
	 * frame at Docs/Media/cctv5-feed.png, not from the focal length.
	 */
	constexpr float CctvCameraFieldOfView = 78.0f;

	constexpr float BedsideTableTopZ = 60.0f;
	constexpr float DeskWorkSurfaceHeight = 74.0f;
	const FVector FridgeLocation(155.0f, -20.0f, FourthFloorZ);
	// 설비 벽장 앞면. 분전반 문짝(-229.2)보다 조금 더 복도로 나온다 —
	// 보일러는 분전반보다 두껍다.
	constexpr float BoilerCupboardX = 580.0f;
	constexpr float BoilerCupboardZ = 150.0f;
	constexpr float BoilerCupboardFaceY = -240.0f;
	const FVector HomeDoorLocation(
		AIGPrologueWorldScene::HomeDoorX,
		AIGPrologueWorldScene::HomeDoorY,
		FourthFloorZ);
	// Far end of the hallway, so leaving 403 is a walk rather than a step.
	const FVector ElevatorLocation(790.0f, -305.0f, FourthFloorZ);
	// 자동문 짝은 열리면 고정 유리(X 2401..2409) 옆으로 물러난다. 문을
	// 유리와 같은 X 2405에 두면 짝이 유리 **안으로** 들어가 그 구간만
	// 유리가 두 겹이 되고 짝의 알루미늄 틀이 유리 속에 박혀 보인다.
	// 8 cm 안쪽 레일로 물려 유리 뒤를 지나가게 한다.
	const FVector StoreDoorLocation(2413.0f, -457.0f, 6.0f);
	const FVector CheckoutLocation(2650.0f, -205.0f, 96.0f);

	const FName PurchaseBagProxyTag(TEXT("Store.PurchaseBagProxy"));
	const FName FootstepVinylTag(TEXT("Footstep.Vinyl"));
	const FName FootstepConcreteTag(TEXT("Footstep.Concrete"));
	const FName FootstepMetalStairTag(TEXT("Footstep.MetalStair"));
	const FName FootstepRooftopTag(TEXT("Footstep.Rooftop"));
	const FName FootstepGypsumTag(TEXT("Footstep.GypsumDebris"));

	void TagFootstepSurface(UStaticMeshComponent* Component, const FName Tag)
	{
		if (Component && !Component->ComponentHasTag(Tag))
		{
			Component->ComponentTags.Add(Tag);
		}
	}

	// These products are never interacted with; the evidence bottles spawned
	// later remain individual actors. At 16 m a 5-20 cm package is already a
	// handful of pixels, so it can fade before the store itself disappears.
	constexpr int32 StoreStockCullStartCentimeters = 1600;
	constexpr int32 StoreStockCullEndCentimeters = 2200;
	// 기존 상품·가격표 1,301개에 냉장고 가격표 17개를 더한다.
	// 병의 몸체·뚜껑·라벨도 각각의 렌더 인스턴스로 센다.
	constexpr int32 ExpectedStoreStockInstances = 1318;
	constexpr int32 MaximumStoreStockBatches = 28;

	struct FPurchaseProfileSpec
	{
		FText ProductName;
		int32 Quantity = 1;
		int32 UnitPrice = 0;
		FVector BagSize = FVector(12.0f, 18.0f, 20.0f);
		float BagCenterY = 0.0f;
		float BagHandleHeight = 8.0f;
	};

	FPurchaseProfileSpec GetPurchaseProfileSpec(
		const EIGPurchaseProfile PurchaseProfile)
	{
		FPurchaseProfileSpec Spec;
		switch (PurchaseProfile)
		{
		case EIGPurchaseProfile::ProfileB1LX1:
			Spec.ProductName =
				NSLOCTEXT("IGReceipt", "ProfileBProduct", "한강수 1L");
			Spec.Quantity = 1;
			Spec.UnitPrice = 1500;
			Spec.BagSize = FVector(13.0f, 12.0f, 28.0f);
			Spec.BagCenterY = 0.0f;
			Spec.BagHandleHeight = 10.0f;
			break;
		case EIGPurchaseProfile::ProfileC2LX2:
			Spec.ProductName =
				NSLOCTEXT("IGReceipt", "ProfileCProduct", "맑은산 2L");
			Spec.Quantity = 2;
			Spec.UnitPrice = 2000;
			Spec.BagSize = FVector(16.0f, 25.0f, 31.0f);
			Spec.BagCenterY = 5.5f;
			Spec.BagHandleHeight = 15.0f;
			break;
		case EIGPurchaseProfile::ProfileA500MlX2:
		case EIGPurchaseProfile::Unset:
		default:
			Spec.ProductName =
				NSLOCTEXT("IGReceipt", "ProfileAProduct", "새벽샘물 500mL");
			Spec.Quantity = 2;
			Spec.UnitPrice = 1000;
			Spec.BagSize = FVector(12.0f, 18.0f, 20.0f);
			Spec.BagCenterY = 4.0f;
			Spec.BagHandleHeight = 8.0f;
			break;
		}
		return Spec;
	}

	FText FormatWon(const int32 Amount)
	{
		return FText::Format(
			NSLOCTEXT("IGReceipt", "WonFormat", "{0}원"),
			FText::AsNumber(Amount));
	}

	FText BuildCheckoutPrompt(const EIGPurchaseProfile PurchaseProfile)
	{
		const FPurchaseProfileSpec Spec =
			GetPurchaseProfileSpec(PurchaseProfile);
		return FText::Format(
			NSLOCTEXT(
				"IGReceipt",
				"CheckoutPrompt",
				"{0} {1}병 계산하기 ({2})"),
			Spec.ProductName,
			FText::AsNumber(Spec.Quantity),
			FormatWon(Spec.Quantity * Spec.UnitPrice));
	}
}

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------

AIGPrologueWorldScene::AIGPrologueWorldScene()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	SetRootComponent(SceneRoot);
	DeskSurfaceWorldZ = IGPrologueWorld::FourthFloorZ
		+ IGPrologueWorld::DeskWorkSurfaceHeight;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMeshFinder(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMeshFinder(
		TEXT("/Engine/BasicShapes/Cone.Cone"));
	CubeMesh = CubeMeshFinder.Object;
	CylinderMesh = CylinderMeshFinder.Object;
	PlaneMesh = PlaneMeshFinder.Object;
	SphereMesh = SphereMeshFinder.Object;
	ConeMesh = ConeMeshFinder.Object;

	auto FindMaterial = [](const TCHAR* Path) -> UMaterialInterface*
	{
		ConstructorHelpers::FObjectFinder<UMaterialInterface> Finder(Path);
		return Finder.Object;
	};

	WallMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_RoomWall.M_RoomWall"));
	FloorMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_RoomFloor.M_RoomFloor"));
	WoodMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_DarkWood.M_DarkWood"));
	BeddingMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_Bedding.M_Bedding"));
	DoorMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_Door.M_Door"));
	AlarmMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_Alarm.M_Alarm"));
	AsphaltMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_Asphalt.M_Asphalt"));
	ConcreteMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_Concrete.M_Concrete"));
	ConcreteDarkMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_ConcreteDark.M_ConcreteDark"));
	StoreFloorMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_StoreFloor.M_StoreFloor"));
	LightPanelMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_LightPanel.M_LightPanel"));
	SignMintMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_SignMint.M_SignMint"));
	SignWhiteMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_SignWhite.M_SignWhite"));
	GlassMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_Glass.M_Glass"));
	MetalFrameMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_MetalFrame.M_MetalFrame"));
	FridgeBodyMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_FridgeBody.M_FridgeBody"));
	FridgeInteriorMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_FridgeInterior.M_FridgeInterior"));
	PlasticDarkMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_PlasticDark.M_PlasticDark"));
	TrashBagMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_TrashBag.M_TrashBag"));
	CardboardMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_Cardboard.M_Cardboard"));
	WaterBlueMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_WaterBlue.M_WaterBlue"));
	BottleGreenMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_BottleGreen.M_BottleGreen"));
	BottleBrownMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_BottleBrown.M_BottleBrown"));
	SnackRedMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_SnackRed.M_SnackRed"));
	SnackYellowMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_SnackYellow.M_SnackYellow"));
	SnackBlueMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_SnackBlue.M_SnackBlue"));
	WindowGlowMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_WindowGlow.M_WindowGlow"));
	WindowDarkMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_WindowDark.M_WindowDark"));
	NightSkyMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_NightSky.M_NightSky"));
	StreetLampGlowMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_StreetLampGlow.M_StreetLampGlow"));
	CoolerBodyMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_CoolerBody.M_CoolerBody"));
	CounterTopMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_CounterTop.M_CounterTop"));
	ScreenGlowMaterial = FindMaterial(TEXT("/Game/Prototype/Materials/M_ScreenGlow.M_ScreenGlow"));

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(SceneRoot);
	PostProcess->bUnbound = true;
	// Adaptive exposure like an eye/camera: dark alleys stay dark but
	// readable, the fluorescent store genuinely blooms after the walk.
	PostProcess->Settings.bOverride_AutoExposureMethod = true;
	PostProcess->Settings.AutoExposureMethod = AEM_Histogram;
	PostProcess->Settings.bOverride_AutoExposureMinBrightness = true;
	PostProcess->Settings.bOverride_AutoExposureMaxBrightness = true;
	// These values are EV100 because extended luminance range is enabled.
	// The old -1.2..1.1 clamp was below the actual fluorescent/store range,
	// so the camera could not stop down and white fixtures clipped. Retain a
	// bounded adaptation range, but let bright practicals reach a sane EV.
	PostProcess->Settings.AutoExposureMinBrightness = -0.5f;
	PostProcess->Settings.AutoExposureMaxBrightness = 5.0f;
	PostProcess->Settings.bOverride_AutoExposureBias = true;
	PostProcess->Settings.AutoExposureBias = -0.35f;
	PostProcess->Settings.bOverride_AutoExposureSpeedUp = true;
	PostProcess->Settings.AutoExposureSpeedUp = 2.0f;
	PostProcess->Settings.bOverride_AutoExposureSpeedDown = true;
	PostProcess->Settings.AutoExposureSpeedDown = 0.75f;
	// The apartment deliberately contains a hot tungsten pool and an almost
	// unlit wardrobe in the same frame. Bilateral local exposure preserves the
	// wallpaper emboss and furniture silhouette without raising the global
	// exposure until the night scene looks like daylight.
	PostProcess->Settings.bOverride_LocalExposureMethod = true;
	PostProcess->Settings.LocalExposureMethod = ELocalExposureMethod::Bilateral;
	PostProcess->Settings.bOverride_LocalExposureHighlightContrastScale = true;
	PostProcess->Settings.LocalExposureHighlightContrastScale = 0.84f;
	PostProcess->Settings.bOverride_LocalExposureShadowContrastScale = true;
	// Preserve readable floor and door silhouettes in the unlit corridor while
	// local detail enhancement carries the plaster response inside the beam.
	PostProcess->Settings.LocalExposureShadowContrastScale = 0.76f;
	PostProcess->Settings.bOverride_LocalExposureDetailStrength = true;
	PostProcess->Settings.LocalExposureDetailStrength = 1.12f;
	PostProcess->Settings.bOverride_LocalExposureBlurredLuminanceBlend = true;
	PostProcess->Settings.LocalExposureBlurredLuminanceBlend = 0.52f;
	PostProcess->Settings.bOverride_LocalExposureBlurredLuminanceKernelSizePercent = true;
	PostProcess->Settings.LocalExposureBlurredLuminanceKernelSizePercent = 48.0f;
	PostProcess->Settings.bOverride_LocalExposureMiddleGreyBias = true;
	PostProcess->Settings.LocalExposureMiddleGreyBias = -0.18f;
	// Grade stays close to neutral; the atmosphere supplies the palette.
	PostProcess->Settings.bOverride_VignetteIntensity = true;
	PostProcess->Settings.VignetteIntensity = 0.17f;
	PostProcess->Settings.bOverride_FilmGrainIntensity = true;
	// 조명과 표면의 명암을 그대로 남기고 화면에 입자를 덧씌우지 않는다.
	PostProcess->Settings.FilmGrainIntensity = 0.0f;
	PostProcess->Settings.bOverride_ColorSaturation = true;
	PostProcess->Settings.ColorSaturation = FVector4(0.93f, 0.95f, 1.0f, 1.0f);
	// A restrained film curve gives PBR roughness and normal changes somewhere
	// to read.  The toe is kept below the engine default so the unlit corridor
	// retains material information instead of crushing into a single black,
	// while the shoulder rolls practicals off before their fixture detail clips.
	PostProcess->Settings.bOverride_ColorContrast = true;
	PostProcess->Settings.ColorContrast = FVector4(1.025f, 1.025f, 1.025f, 1.0f);
	PostProcess->Settings.bOverride_FilmSlope = true;
	PostProcess->Settings.FilmSlope = 0.90f;
	PostProcess->Settings.bOverride_FilmToe = true;
	PostProcess->Settings.FilmToe = 0.53f;
	PostProcess->Settings.bOverride_FilmShoulder = true;
	PostProcess->Settings.FilmShoulder = 0.24f;
	PostProcess->Settings.bOverride_FilmBlackClip = true;
	PostProcess->Settings.FilmBlackClip = 0.0f;
	PostProcess->Settings.bOverride_FilmWhiteClip = true;
	PostProcess->Settings.FilmWhiteClip = 0.035f;
	// Restrained bloom keeps emissive signage readable instead of hazy.
	PostProcess->Settings.bOverride_BloomIntensity = true;
	PostProcess->Settings.BloomIntensity = 0.18f;
	// Low motion blur: walking stays smooth but frames remain readable.
	PostProcess->Settings.bOverride_MotionBlurAmount = true;
	PostProcess->Settings.MotionBlurAmount = 0.05f;
	// Ambient occlusion seats furniture and shelf stock into their corners.
	PostProcess->Settings.bOverride_AmbientOcclusionIntensity = true;
	PostProcess->Settings.AmbientOcclusionIntensity = 0.48f;
	PostProcess->Settings.bOverride_AmbientOcclusionRadius = true;
	PostProcess->Settings.AmbientOcclusionRadius = 48.0f;
	PostProcess->Settings.bOverride_LumenAmbientOcclusionIntensity = true;
	PostProcess->Settings.LumenAmbientOcclusionIntensity = 0.55f;
}

void AIGPrologueWorldScene::BeginPlay()
{
	Super::BeginPlay();
	InitializePrologue();
}

bool AIGPrologueWorldScene::AuditPlayerClearance(APawn* Pawn, AIGSwingDoor* BoothDoor)
{
	const UCapsuleComponent* Capsule = Pawn ? Pawn->FindComponentByClass<UCapsuleComponent>() : nullptr;
	if (!Capsule || !HomeDoor || !BuildingDoor || !BoothDoor) { return false; }
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SpatialClearance), false, Pawn);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	int32 Failures = 0;
	int32 Checks = 0;
	const auto Sweep = [&](const TCHAR* Name, const FVector& From, const FVector& To, const bool bExpectedClear = true)
	{
		FHitResult Hit;
		const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn, Shape, Query);
		const bool bPass = bBlocked != bExpectedClear;
		++Checks;
		Failures += bPass ? 0 : 1;
		UE_LOG(LogIndieGame, Display, TEXT("SPATIAL_CHECK %s %s hit=%s"), Name,
			bPass ? TEXT("PASS") : TEXT("FAIL"), *GetNameSafe(Hit.GetComponent()));
		if (bBlocked && !bPass && Hit.GetComponent())
		{
			UE_LOG(LogIndieGame, Display, TEXT("SPATIAL_OBSTACLE owner=%s center=%s extent=%s at=%s"),
				*GetNameSafe(Hit.GetActor()), *Hit.GetComponent()->Bounds.Origin.ToCompactString(),
				*Hit.GetComponent()->Bounds.BoxExtent.ToCompactString(), *Hit.ImpactPoint.ToCompactString());
		}
	};
	const bool bHomeWasOpen = HomeDoor->IsOpen();
	const bool bBuildingWasOpen = BuildingDoor->IsOpen();
	const bool bBoothWasOpen = BoothDoor->IsOpen();
	const float DoorCenter = HomeDoorX + WideDoorLeafWidth * .5f;
	HomeDoor->ForceOpenState(false);
	Sweep(TEXT("home_closed_blocks"), FVector(DoorCenter, -300, FourthFloorZ + 106),
		FVector(DoorCenter, -165, FourthFloorZ + 106), false);
	HomeDoor->ForceOpenState(true);
	for (const float Offset : {-18.0f, 0.0f, 18.0f})
	{
		const FVector Outer(DoorCenter + Offset, -300, FourthFloorZ + 106);
		const FVector Inner(DoorCenter + Offset, -165, FourthFloorZ + 106);
		Sweep(TEXT("home_enter_offset"), Outer, Inner);
		Sweep(TEXT("home_leave_offset"), Inner, Outer);
	}
	BoothDoor->ForceOpenState(false);
	Sweep(TEXT("booth_closed_blocks"), FVector(165, -310, 106), FVector(165, -181, 106), false);
	BoothDoor->ForceOpenState(true);
	for (const float Offset : {-18.0f, 0.0f, 18.0f})
	{
		Sweep(TEXT("booth_entry_offset"), FVector(165 + Offset, -310, 106), FVector(165 + Offset, -181, 106));
	}
	BuildingDoor->ForceOpenState(true);
	Sweep(TEXT("building_entrance"), FVector(646, -470, 106), FVector(646, -310, 106));
	Sweep(TEXT("lobby_connector"), FVector(-45, -305, 98), FVector(410, -305, 98));
	for (const float LobbyY : {-320.0f, -270.0f, -210.0f})
	{
		Sweep(TEXT("lobby_waiting_lane"), FVector(495, LobbyY, 98), FVector(650, LobbyY, 98));
	}
	Sweep(TEXT("lobby_turn"), FVector(630, -320, 98), FVector(630, -190, 98));
	Sweep(TEXT("upper_lift_step_aside"), FVector(635, -305, FourthFloorZ + 98), FVector(635, -415, FourthFloorZ + 98));
	Sweep(TEXT("upper_lift_waiting_edge"), FVector(620, -415, FourthFloorZ + 98), FVector(660, -415, FourthFloorZ + 98));
	Sweep(TEXT("west_alley"), FVector(1300, -560, 98), FVector(1300, -1150, 98));
	Sweep(TEXT("rear_alley"), FVector(1330, -1380, 98), FVector(2310, -1380, 98));
	// 새 위치의 소품도 플레이어와 같은 단순 가시성 트레이스에 잡혀야 한다.
	const auto TraceFixture = [&](const TCHAR* Name, const FVector& Eye, const FVector& Target, const UClass* ExpectedClass)
	{
		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Eye, Target, ECC_Visibility, Query);
		const bool bPass = bHit && Hit.GetActor() && Hit.GetActor()->IsA(ExpectedClass);
		++Checks;
		Failures += bPass ? 0 : 1;
		UE_LOG(LogIndieGame, Display, TEXT("SPATIAL_CHECK %s %s hit=%s"), Name,
			bPass ? TEXT("PASS") : TEXT("FAIL"), *GetNameSafe(Hit.GetActor()));
	};
	TraceFixture(TEXT("lower_call_plate_target"), FVector(610, -227, 162), FVector(702, -227, 120), AIGElevator::StaticClass());
	TraceFixture(TEXT("upper_call_plate_target"), FVector(610, -390, 1062), FVector(702, -390, 1020), AIGElevator::StaticClass());
	TraceFixture(TEXT("meter_sheet_target"), FVector(401, -287, 162), FVector(401, -363, 150), AIGReadableNote::StaticClass());
	HomeDoor->ForceOpenState(bHomeWasOpen);
	BuildingDoor->ForceOpenState(bBuildingWasOpen);
	BoothDoor->ForceOpenState(bBoothWasOpen);
	UE_LOG(LogIndieGame, Display, TEXT("SPATIAL_CLEARANCE %s checks=%d failures=%d capsule=%.1fx%.1f"),
		Failures ? TEXT("FAIL") : TEXT("PASS"), Checks, Failures,
		Capsule->GetScaledCapsuleRadius() * 2, Capsule->GetScaledCapsuleHalfHeight() * 2);
	return Failures == 0;
}

// ---------------------------------------------------------------------------
// Assembly helpers
// ---------------------------------------------------------------------------

UStaticMeshComponent* AIGPrologueWorldScene::CreateBlock(
	const FVector& Center,
	const FVector& SizeCentimeters,
	UMaterialInterface* Material,
	const bool bEnableCollision,
	UStaticMesh* MeshOverride,
	const FRotator& Rotation,
	USceneComponent* Parent)
{
	UStaticMeshComponent* Block = NewObject<UStaticMeshComponent>(
		this,
		*FString::Printf(TEXT("Block_%d"), BlockCounter++));
	USceneComponent* ResolvedParent =
		Parent ? Parent : (ActiveParent ? ActiveParent.Get() : SceneRoot.Get());
	Block->SetupAttachment(ResolvedParent);
	Block->SetStaticMesh(MeshOverride ? MeshOverride : CubeMesh.Get());
	// Blender에서 구운 메시는 재질 인스턴스를 슬롯에 들고 온다. 그 메시에
	// nullptr를 주면 「메시 것을 써라」다. 회색 기본 재질로 바꾸지 않는다.
	if (Material)
	{
		Block->SetMaterial(0, Material);
	}
	Block->SetRelativeLocation(Center);
	Block->SetRelativeRotation(Rotation);
	Block->SetRelativeScale3D(SizeCentimeters / 100.0f);
	Block->SetMobility(EComponentMobility::Static);
	Block->SetGenerateOverlapEvents(false);
	Block->SetCanEverAffectNavigation(false);
	Block->SetCollisionProfileName(
		bEnableCollision
			? UCollisionProfile::BlockAll_ProfileName
			: UCollisionProfile::NoCollision_ProfileName);
	// Paper-thin dressing (posters, price rails, seams, panel grooves) sits
	// flush against its host surface; letting it cast shadows only produces
	// self-shadow acne and doubled contact lines.
	const FVector MeshSize = Block->GetStaticMesh()->GetBounds().BoxExtent * 2.0f
		* Block->GetRelativeScale3D().GetAbs();
	if (MeshSize.GetMin() < 3.0f || Material == GlassMaterial)
	{
		Block->SetCastShadow(false);
	}
	// 표면에 붙인 얇은 표찰·띠는 뒤의 벽과 같은 공간을 점유한다. 이런 장식까지
	// 거리장에 넣으면 작은 형상이 부풀고 Lumen 갱신 대상만 늘어난다.
	// 구조물과 충돌 있는 소품은 보존하며, 크기는 원본 메시의 실제 바운드로 잰다.
	if (!bEnableCollision && MeshSize.GetMin() < 3.0f)
	{
		Block->SetAffectDistanceFieldLighting(false);
		Block->SetAffectDynamicIndirectLighting(false);
	}
	Block->RegisterComponent();
	GeometryComponents.Add(Block);
	return Block;
}

UStaticMeshComponent* AIGPrologueWorldScene::CreatePrintedBlock(
	const FVector& Center,
	const FVector& SizeCentimeters,
	UMaterialInterface* BodyMaterial,
	UMaterialInterface* PrintMaterial,
	const FVector& PrintFacing,
	const bool bEnableCollision,
	const bool bPrintBothFaces)
{
	UStaticMeshComponent* Body =
		CreateBlock(Center, SizeCentimeters, BodyMaterial, bEnableCollision);
	if (!Body || !PrintMaterial)
	{
		return Body;
	}

	// 인쇄판은 4 mm다. 이 씬이 게시물·명판·가격표에 이미 쓰는 두께이고,
	// 그만한 마구리는 어느 각도에서도 두 번째 인쇄로 읽히지 않는다.
	constexpr float PlateThickness = 0.4f;
	const FVector Facing = PrintFacing.GetSafeNormal();
	int32 Axis = 0;
	for (int32 Index = 1; Index < 3; ++Index)
	{
		if (FMath::Abs(Facing[Index]) > FMath::Abs(Facing[Axis]))
		{
			Axis = Index;
		}
	}
	if (FMath::Abs(Facing[Axis]) < 0.99f)
	{
		// 축에 붙지 않은 방향은 이 방식으로 덮을 수 없다. 예전처럼 몸통에
		// 인쇄를 주고, 호출부가 알아채도록 이름을 남긴다.
		Body->SetMaterial(0, PrintMaterial);
		UE_LOG(
			LogIndieGame,
			Warning,
			TEXT("IG_PRINTED_BLOCK facing is not axis aligned: %s"),
			*Facing.ToString());
		return Body;
	}

	FVector PlateSize = SizeCentimeters;
	PlateSize[Axis] = PlateThickness;
	FVector Step = FVector::ZeroVector;
	Step[Axis] = (SizeCentimeters[Axis] + PlateThickness) * 0.5f
		* FMath::Sign(Facing[Axis]);

	CreateBlock(Center + Step, PlateSize, PrintMaterial, false);
	if (bPrintBothFaces)
	{
		// 큐브 여섯 면의 UV 손잡이는 모두 같다(Scripts/probe_cube_face_uvs.py로
		// 실측). 반대쪽 판을 돌릴 필요 없이 같은 자세로 한 장 더 붙이면
		// 양면 간판이 양쪽에서 똑같이 읽힌다.
		CreateBlock(Center - Step, PlateSize, PrintMaterial, false);
	}
	return Body;
}

UStaticMeshComponent* AIGPrologueWorldScene::CreatePhysicsProp(
	UStaticMesh* Mesh,
	UMaterialInterface* Material,
	const FVector& Scale,
	const FVector& Location,
	const FRotator& Rotation,
	const float MassKg)
{
	UStaticMeshComponent* Prop = NewObject<UStaticMeshComponent>(
		this,
		*FString::Printf(TEXT("PhysProp_%d"), BlockCounter++));
	Prop->SetupAttachment(ActiveParent ? ActiveParent.Get() : SceneRoot.Get());
	Prop->SetStaticMesh(Mesh);
	if (Material)
	{
		Prop->SetMaterial(0, Material);
	}
	Prop->SetRelativeLocation(Location);
	Prop->SetRelativeRotation(Rotation);
	Prop->SetRelativeScale3D(Scale);
	Prop->SetMobility(EComponentMobility::Movable);
	Prop->SetGenerateOverlapEvents(false);
	Prop->SetCanEverAffectNavigation(false);
	Prop->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Prop->RegisterComponent();
	Prop->SetSimulatePhysics(true);
	// 걷어차면 굴러가는 물건은 부딪는 소리도 낸다. 생긴 직후 자리 잡는 충돌은
	// 소리가 아니라서 2초 동안은 듣지 않는다.
	Prop->SetNotifyRigidBodyCollision(true);
	Prop->OnComponentHit.AddDynamic(this, &AIGPrologueWorldScene::HandlePhysicsPropHit);
	if (const UWorld* PropWorld = GetWorld())
	{
		PropImpactArmedAtSeconds = FMath::Max(
			PropImpactArmedAtSeconds, PropWorld->GetTimeSeconds() + 2.0);
	}
	Prop->SetMassOverrideInKg(NAME_None, FMath::Max(0.05f, MassKg));
	// Small household props should settle instead of skating and spinning for
	// seconds after a light capsule contact.
	Prop->SetUseCCD(true);
	Prop->SetPhysicsMaxAngularVelocityInDegrees(720.0f);
	Prop->SetAngularDamping(2.4f);
	Prop->SetLinearDamping(1.1f);
	GeometryComponents.Add(Prop);
	return Prop;
}

void AIGPrologueWorldScene::HandlePhysicsPropHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	const FVector NormalImpulse,
	const FHitResult& Hit)
{
	// 소화기는 밤1의 낙하 연출이 제 녹음과 소음 보고를 낸다. 여기서 또 내면 두 번 운다.
	UWorld* World = GetWorld();
	if (!World || !HitComponent || HitComponent == CorridorExtinguisher)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now < PropImpactArmedAtSeconds || Now - PropImpactLastAnySeconds < 0.05)
	{
		return;
	}
	// 충격량을 질량으로 나누면 부딪는 순간 바뀐 속도다. 가만히 놓였거나 구르는
	// 동안의 접촉은 한 프레임에 16cm/s 남짓이라 여기서 걸러진다.
	const float MassKg = FMath::Max(HitComponent->GetMass(), 0.05f);
	const float DeltaSpeed = NormalImpulse.Size() / MassKg;
	if (DeltaSpeed < 50.0f)
	{
		return;
	}
	double& LastSeconds = PropImpactLastSeconds.FindOrAdd(
		TWeakObjectPtr<UPrimitiveComponent>(HitComponent), -1.0);
	if (LastSeconds >= 0.0 && Now - LastSeconds < 0.3)
	{
		return;
	}
	LastSeconds = Now;
	PropImpactLastAnySeconds = Now;
	const float KnockVolume = FMath::GetMappedRangeValueClamped(
		FVector2D(50.0f, 400.0f), FVector2D(0.18f, 0.50f), DeltaSpeed);
	// 발소리가 아니라 물건 소리다. 그에게 보고하지 않는다 — §5.1의 낙하물 값은
	// 복도 소화기 하나에 묶여 있고, 실내화를 건드릴 때마다 그 값이 나가면 403호가
	// 밤마다 시끄러운 방이 된다. 소품과 바닥이 모두 이 씬의 몸이라, 씬을 오클루전
	// 에서 빼야 닿는 자리의 소리가 제 바닥과 제 몸에 가려지지 않는다. 걷어찬
	// 사람은 늘 그 옆에 있다. 냉장고 안의 병은 닫힌 냉장고가 가린다.
	IGAudio::SpawnOneShotFromActorAt(
		this,
		UIGToneSequenceSoundWave::CreateKickedPropKnock(this, MassKg < 0.18f),
		Hit.ImpactPoint,
		KnockVolume,
		FMath::FRandRange(0.94f, 1.08f),
		60.0f,
		700.0f,
		EIGAudioBus::World);
}

UPointLightComponent* AIGPrologueWorldScene::CreateLight(
	const FVector& Location,
	const float Intensity,
	const float Radius,
	const FLinearColor& Color,
	const bool bCastShadows,
	const float SourceRadius,
	USceneComponent* Parent,
	const bool bDownlight)
{
	const FName LightName(*FString::Printf(TEXT("Light_%d"), BlockCounter++));
	UPointLightComponent* Light = bDownlight
		? NewObject<USpotLightComponent>(this, LightName)
		: NewObject<UPointLightComponent>(this, LightName);
	ZoneLights[static_cast<int32>(BuildingLightZone)].Add(Light);
	USceneComponent* ResolvedParent =
		Parent ? Parent : (ActiveParent ? ActiveParent.Get() : SceneRoot.Get());
	Light->SetupAttachment(ResolvedParent);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetRelativeLocation(Location);
	if (USpotLightComponent* Downlight = Cast<USpotLightComponent>(Light))
	{
		// 천장 조명은 아래쪽만 비춘다. 위층과 외벽까지 6면 그림자를 만들지 않는다.
		Downlight->SetRelativeRotation(FRotator(-90, 0, 0));
		Downlight->SetInnerConeAngle(56.f);
		Downlight->SetOuterConeAngle(75.f);
	}
	Light->SetIntensity(Intensity);
	Light->SetAttenuationRadius(Radius);
	// 광원 영향권 밖에서는 먼 층과 골목의 그림자까지 계속 계산할 필요가 없다.
	Light->SetMaxDrawDistance(FMath::Max(Radius * 2.5f, 1600.0f));
	Light->SetMaxDistanceFadeRange(FMath::Max(Radius * 0.5f, 250.0f));
	Light->SetLightColor(Color);
	Light->SetCastShadows(bCastShadows);
	// 광원의 면적으로 그림자 가장자리를 부드럽게 한다.
	const float EffectiveSourceRadius = FMath::Max(SourceRadius, 3.0f);
	Light->SetSourceRadius(EffectiveSourceRadius);
	Light->SetSoftSourceRadius(EffectiveSourceRadius * 1.6f);
	// VSM이 소품 접촉부까지 처리한다. 화면 공간 접촉 그림자를 중복 계산하지 않는다.
	Light->ContactShadowLength = 0.0f;
	Light->ContactShadowLengthInWS = false;
	Light->ShadowSharpen = 0.0f;
	// Do not mute the BRDF at the light.  Material roughness and specular now
	// own highlight width/energy, so brushed steel, plastic film and plaster no
	// longer receive the same flattened response.
	Light->SetSpecularScale(1.0f);
	Light->RegisterComponent();
	Lights.Add(Light);
	return Light;
}

void AIGPrologueWorldScene::AdvanceUtilityMeters(float Degrees, bool bUnnamedPowered, bool bCommonPowered)
{
	const APawn* Observer = UGameplayStatics::GetPlayerPawn(this, 0);
	const bool bNearby = Observer && FVector::DistSquared(Observer->GetActorLocation(), FVector(506, -345, 150)) <= FMath::Square(650.0f);
	// 멀리서는 각도만 누적한다. 돌아왔을 때 정지한 원판이나 다른 속도를 보이지 않는다.
	for (int32 Index = 0; Index < UtilityMeterDiscs.Num(); ++Index)
	{
		if ((Index != 4 || bUnnamedPowered) && (Index != 3 || bCommonPowered))
		{
			UtilityMeterPhases[Index] = FMath::Fmod(UtilityMeterPhases[Index] + Degrees * (1.f + .11f * Index), 360.f);
		}
		if (bNearby)
		{
			UtilityMeterDiscs[Index]->SetRelativeRotation(FRotator(0, UtilityMeterPhases[Index], 0));
		}
	}
}

float AIGPrologueWorldScene::GetUtilityMeterUpdateInterval() const
{
	const APawn* Observer = UGameplayStatics::GetPlayerPawn(this, 0);
	const float DistanceSquared = Observer ? FVector::DistSquared(Observer->GetActorLocation(), FVector(506,-345,150)) : MAX_flt;
	return DistanceSquared <= FMath::Square(250.f) ? 1.f/60.f : DistanceSquared <= FMath::Square(650.f) ? .05f : .25f;
}

void AIGPrologueWorldScene::SetCommonInspectionLightsEnabled(const bool bEnabled)
{
	bCommonInspectionLightsEnabled = bEnabled;
	for (int32 Index = 0; Index < LobbyLights.Num(); ++Index) SetFixtureLive(Index, true, false);
	for (int32 Index = 0; Index < CorridorLights.Num(); ++Index) SetFixtureLive(Index, true, true);
	ApplyServiceLights();
}

bool AIGPrologueWorldScene::AreCommonInspectionLightsOff() const
{
	for (const UPointLightComponent* Light : LobbyLights) if (Light && Light->Intensity > 0.f) return false;
	for (const UPointLightComponent* Light : CorridorLights) if (Light && Light->Intensity > 0.f) return false;
	for (const UPointLightComponent* Light : StairCoreLights) if (Light && Light->Intensity > 0.f) return false;
	for (const UPointLightComponent* Light : LowerFloorLights) if (Light && Light->Intensity > 0.f) return false;
	for (const UStaticMeshComponent* Disc : LobbyLightDiscs) if (Disc && Disc->GetMaterial(0) == LightPanelMaterial) return false;
	for (const UStaticMeshComponent* Disc : CorridorLightDiscs) if (Disc && Disc->GetMaterial(0) == LightPanelMaterial) return false;
	return !LobbyLights.IsEmpty() && !CorridorLights.IsEmpty();
}

void AIGPrologueWorldScene::ApplyServiceLights()
{
	// 계단탑과 2·3층의 등. 낮에는 켜져 있고 그 시간에는 비상구 등만 남는다.
	// 1층 공용 차단기가 내려가면 복도 등과 함께 꺼진다.
	const bool bLit = bCommonInspectionLightsEnabled && !bTheHourSealed;
	const auto Apply = [this, bLit](
		const TArray<TObjectPtr<UPointLightComponent>>& Lights,
		const TArray<float>& Intensities,
		const TArray<TObjectPtr<UStaticMeshComponent>>& Discs)
	{
		for (int32 Index = 0; Index < Lights.Num(); ++Index)
		{
			if (UPointLightComponent* Light = Lights[Index])
			{
				Light->SetIntensity(
					bLit && Intensities.IsValidIndex(Index) ? Intensities[Index] : 0.0f);
			}
		}
		for (UStaticMeshComponent* Disc : Discs)
		{
			if (Disc)
			{
				Disc->SetMaterial(0, bLit ? LightPanelMaterial : PlasticDarkMaterial);
			}
		}
	};
	Apply(StairCoreLights, StairCoreLightIntensities, StairCoreLightDiscs);
	Apply(LowerFloorLights, LowerFloorLightIntensities, LowerFloorLightDiscs);
}

UStaticMeshComponent* AIGPrologueWorldScene::CreateDecoOnComponent(
	USceneComponent* Parent,
	UStaticMesh* Mesh,
	UMaterialInterface* Material,
	const FVector& RelativeLocation,
	const FRotator& RelativeRotation,
	const FVector& RelativeScale)
{
	if (!Parent || !Mesh)
	{
		return nullptr;
	}

	AActor* OwnerActor = Parent->GetOwner();
	UStaticMeshComponent* Deco = NewObject<UStaticMeshComponent>(
		OwnerActor ? static_cast<UObject*>(OwnerActor) : static_cast<UObject*>(this),
		*FString::Printf(TEXT("Deco_%d"), BlockCounter++));
	Deco->SetupAttachment(Parent);
	Deco->SetStaticMesh(Mesh);
	// 물병 띠는 공용 페이지 높이의 1/8만 쓴다. 가까운 글자의 밉이 먼저 내려가지 않게 한다.
	if (Mesh->GetFName() == FName(TEXT("SM_LabelSleeve")))
	{
		Deco->StreamingDistanceMultiplier = 8.0f;
	}
	Deco->SetMaterial(0, Material);
	Deco->SetRelativeLocation(RelativeLocation);
	Deco->SetRelativeRotation(RelativeRotation);
	Deco->SetRelativeScale3D(RelativeScale);
	Deco->SetMobility(EComponentMobility::Movable);
	Deco->SetGenerateOverlapEvents(false);
	Deco->SetCanEverAffectNavigation(false);
	Deco->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Deco->RegisterComponent();
	return Deco;
}

void AIGPrologueWorldScene::AddStaticPurchaseBagProxy(
	AIGPickupItem* WaterBottle,
	const EIGPurchaseProfile PurchaseProfile)
{
	if (!WaterBottle || !CubeMesh)
	{
		return;
	}

	UStaticMeshComponent* BottleRoot = WaterBottle->GetMeshComponent();
	if (!BottleRoot)
	{
		return;
	}

	const IGPrologueWorld::FPurchaseProfileSpec Spec =
		IGPrologueWorld::GetPurchaseProfileSpec(PurchaseProfile);
	const FVector RootScale = BottleRoot->GetRelativeScale3D().GetAbs();
	const FVector SafeRootScale(
		FMath::Max(RootScale.X, KINDA_SMALL_NUMBER),
		FMath::Max(RootScale.Y, KINDA_SMALL_NUMBER),
		FMath::Max(RootScale.Z, KINDA_SMALL_NUMBER));
	const auto DivideByRootScale = [SafeRootScale](const FVector& Value)
	{
		return FVector(
			Value.X / SafeRootScale.X,
			Value.Y / SafeRootScale.Y,
			Value.Z / SafeRootScale.Z);
	};
	const auto AddProxyPart =
		[this, BottleRoot, &DivideByRootScale](
			UMaterialInterface* Material,
			const FVector& ActorSpaceLocation,
			const FVector& ActorSpaceSize)
		{
			UStaticMeshComponent* Part = CreateDecoOnComponent(
				BottleRoot,
				CubeMesh,
				Material,
				DivideByRootScale(ActorSpaceLocation),
				FRotator::ZeroRotator,
				DivideByRootScale(ActorSpaceSize / 100.0f));
			if (Part)
			{
				Part->ComponentTags.AddUnique(
					IGPrologueWorld::PurchaseBagProxyTag);
				Part->SetVisibility(false, true);
				Part->SetCastShadow(false);
			}
			return Part;
		};

	const float Depth = Spec.BagSize.X;
	const float Width = Spec.BagSize.Y;
	const float Height = Spec.BagSize.Z;
	const float CenterY = Spec.BagCenterY;
	const float PanelThickness = 0.35f;
	UMaterialInterface* BagFilmMaterial =
		TexMat(TEXT("M_CarrierBagFilm"), GlassMaterial);

	// Five translucent sheets preserve the selected bottles while giving the
	// carried actor a readable convenience-store-bag silhouette.
	AddProxyPart(
		BagFilmMaterial,
		FVector(Depth * 0.5f, CenterY, Height * 0.5f),
		FVector(PanelThickness, Width, Height));
	AddProxyPart(
		BagFilmMaterial,
		FVector(-Depth * 0.5f, CenterY, Height * 0.5f),
		FVector(PanelThickness, Width, Height));
	AddProxyPart(
		BagFilmMaterial,
		FVector(0.0f, CenterY + Width * 0.5f, Height * 0.5f),
		FVector(Depth, PanelThickness, Height));
	AddProxyPart(
		BagFilmMaterial,
		FVector(0.0f, CenterY - Width * 0.5f, Height * 0.5f),
		FVector(Depth, PanelThickness, Height));
	AddProxyPart(
		BagFilmMaterial,
		FVector(0.0f, CenterY, PanelThickness * 0.5f),
		FVector(Depth, Width, PanelThickness));

	const float HandleHalfWidth = Width * 0.29f;
	const float HandleCenterZ = Height + Spec.BagHandleHeight * 0.5f;
	for (const float HandleY :
		{CenterY - HandleHalfWidth, CenterY + HandleHalfWidth})
	{
		AddProxyPart(
			SignWhiteMaterial,
			FVector(0.0f, HandleY, HandleCenterZ),
			FVector(0.9f, 0.8f, Spec.BagHandleHeight));
	}
	AddProxyPart(
		SignWhiteMaterial,
		FVector(0.0f, CenterY, Height + Spec.BagHandleHeight),
		FVector(0.9f, HandleHalfWidth * 2.0f, 0.8f));
}

void AIGPrologueWorldScene::RefreshPurchaseProfilePresentation()
{
	EIGPurchaseProfile PurchaseProfile = EIGPurchaseProfile::ProfileA500MlX2;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UIGMissingFloorNarrativeSubsystem* Narrative =
			GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
		{
			const EIGPurchaseProfile SavedProfile =
				Narrative->GetStorePurchaseProfile();
			if (SavedProfile != EIGPurchaseProfile::Unset)
			{
				PurchaseProfile = SavedProfile;
			}
		}
	}

	if (Checkout)
	{
		Checkout->SetInteractionPrompt(
			IGPrologueWorld::BuildCheckoutPrompt(PurchaseProfile));
	}

	const FGameplayTag PurchaseTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("State.CH01.Morning.WaterPurchased")),
		false);
	const bool bPurchaseCommitted = IGStory::HasState(this, PurchaseTag);
	for (AIGPickupItem* WaterBottle : WaterBottles)
	{
		if (!WaterBottle)
		{
			continue;
		}
		const bool bShowBag =
			bPurchaseCommitted
			&& WaterBottle->PurchaseProfileOnPickup == PurchaseProfile;
		TArray<UStaticMeshComponent*> Components;
		WaterBottle->GetComponents<UStaticMeshComponent>(Components);
		for (UStaticMeshComponent* Component : Components)
		{
			if (Component
				&& Component->ComponentTags.Contains(
					IGPrologueWorld::PurchaseBagProxyTag))
			{
				Component->SetVisibility(bShowBag, true);
			}
		}
	}
}

void AIGPrologueWorldScene::LoadTexturedMaterials()
{
	const TCHAR* MaterialNames[] = {
		TEXT("M_Jangpan"), TEXT("M_Wallpaper_X"), TEXT("M_Wallpaper_Y"),
		TEXT("M_WallpaperCeil"), TEXT("M_ApartmentWallPatina"),
		TEXT("M_WoodFurnitureUV"), TEXT("M_BeddingUV"),
		TEXT("M_AsphaltWorld"),
		TEXT("M_VillaStucco_X"), TEXT("M_VillaStucco_Y"),
		TEXT("M_Concrete_XY"), TEXT("M_Concrete_X"),
		TEXT("M_ConcreteDark_X"),
		TEXT("M_ConcreteDark_XY"),
		TEXT("M_StoreTileWorld"), TEXT("M_StoreCeilWorld"),
		TEXT("M_StoreWall_X"), TEXT("M_StoreWall_Y"),
		TEXT("M_MetalUV"), TEXT("M_ShelfSteelUV"),
		TEXT("M_PosterSale"),
		TEXT("M_NoteFridge"),
		TEXT("M_SignMainLit"), TEXT("M_SignBladeLit"),
		TEXT("M_SignVilla"), TEXT("M_Plate401"), TEXT("M_Plate402"),
		TEXT("M_Plate403"),
		TEXT("M_LobbyWaterNotice"), TEXT("M_LobbyContactNotice"),
		TEXT("M_LobbyMeterSheet"), TEXT("M_LobbyForumPrint"),
		TEXT("M_Shutter_X"), TEXT("M_SignLaundry"), TEXT("M_SignHair"),
		TEXT("M_SignHof"), TEXT("M_SignSuper"), TEXT("M_Calendar"),
		TEXT("M_TobaccoNotice"), TEXT("M_ConeOrange"),
		TEXT("M_SignPC"), TEXT("M_SignKaraoke"),
		TEXT("M_LabelWater"), TEXT("M_LabelGreenTea"), TEXT("M_LabelBarley"),
		TEXT("M_LabelSoda"),
		TEXT("M_CarrierBagFilm"),
		TEXT("M_ConstructionFilm"),
		// Villa surfaces and fittings from the reference photos.
		TEXT("M_Stucco_X"), TEXT("M_Stucco_Y"), TEXT("M_StuccoCeil"),
		TEXT("M_StuccoDado_X"),
		TEXT("M_GraniteTile_XY"),
		TEXT("M_MarbleFloor_XY"), TEXT("M_StainlessUV"),
		TEXT("M_SteelDoorUV"), TEXT("M_UnitDoorPaintedSteel"),
		TEXT("M_KitchenGlossUV"), TEXT("M_CounterStoneUV"),
		TEXT("M_DoorLock"),
		TEXT("M_Intercom"),
		TEXT("M_LiftCOP"), TEXT("M_LiftHall"), TEXT("M_SwitchPlate"),
		// 「없는 층」 dry-plaster architecture and authored residue layers.
		TEXT("M_MissingFloorPlaster_X"), TEXT("M_MissingFloorPlaster_Y"),
		TEXT("M_MissingFloorPlaster_XY"), TEXT("M_MissingFloorHandprints"),
		TEXT("M_AnnexPressure"),
		TEXT("M_MissingFloorDragTrails"), TEXT("M_MissingFloorDustJoint"),
		TEXT("M_CorridorCasterScuff"),
		TEXT("M_MissingFloorCavityScratches"),
		TEXT("M_DecalDampWallpaper"),
		// §11 규칙 2가 고르게 만드는 발소리 표면들. 이름이 여기 없으면
		// 소리만 다르고 그림은 복도 콘크리트 그대로다.
		TEXT("M_MissingFloorSteelStair"), TEXT("M_RooftopWaterproofing_XY"),
		TEXT("M_UtilityTankSteel"), TEXT("M_UtilityFoundation"), TEXT("M_UtilityGraniteCladding"), TEXT("M_UtilityConcreteDark"), TEXT("M_UtilityVillaBrick"), TEXT("M_UtilityStreetBrick"), TEXT("M_CctvStandby"), TEXT("M_UtilityMeterCounter"), TEXT("M_UtilityMeterLabel"),
		TEXT("M_ApartmentNightGlass"), TEXT("M_NightSkyline"), TEXT("M_NightSkyGlow"), TEXT("M_AnnexPanel"), TEXT("M_RoomInterior"), TEXT("M_SpriteSeo"), TEXT("M_SpriteMok"),
		// Aged paper stock for readable notes.
		TEXT("M_PaperClean"), TEXT("M_PaperOld"),
		TEXT("M_MovingBoxCardboardUV"), TEXT("M_RetailPET"), TEXT("M_LabelWater1L"), TEXT("M_LabelWater2L"),
		TEXT("M_RetailPricePotato"), TEXT("M_RetailPriceShrimp"), TEXT("M_RetailPriceCorn"),
		TEXT("M_RetailPriceCupBeef"), TEXT("M_RetailPriceCupKimchi"), TEXT("M_RetailPriceBiscuit"),
		TEXT("M_RetailPriceWater"), TEXT("M_RetailPriceSoda"), TEXT("M_RetailPriceBarley"), TEXT("M_RetailPriceGreenTea"),
		TEXT("M_RetailTobaccoAd"),
	};

	int32 LoadedCount = 0;
	for (const TCHAR* MaterialName : MaterialNames)
	{
		const FString AssetPath = FString::Printf(
			TEXT("/Game/Prototype/Materials/%s.%s"), MaterialName, MaterialName);
		if (UMaterialInterface* Material =
			LoadObject<UMaterialInterface>(nullptr, *AssetPath))
		{
			TexturedMaterials.Add(FName(MaterialName), Material);
			++LoadedCount;
		}
	}

	UE_LOG(
		LogIndieGame,
		Display,
		TEXT("Textured materials loaded: %d/%d (missing entries fall back to flat colors)"),
		LoadedCount,
		static_cast<int32>(UE_ARRAY_COUNT(MaterialNames)));
}

UMaterialInterface* AIGPrologueWorldScene::TexMat(
	const FName MaterialName,
	UMaterialInterface* Fallback) const
{
	const TObjectPtr<UMaterialInterface>* Found = TexturedMaterials.Find(MaterialName);
	return Found && *Found ? Found->Get() : Fallback;
}

UStaticMesh* AIGPrologueWorldScene::PropMesh(
	const TCHAR* MeshName,
	UStaticMesh* Fallback) const
{
	const FName Key(MeshName);
	if (const TObjectPtr<UStaticMesh>* Cached = PropMeshes.Find(Key))
	{
		return *Cached ? Cached->Get() : Fallback;
	}

	const FString AssetPath = FString::Printf(
		TEXT("/Game/Meshes/%s.%s"), MeshName, MeshName);
	UStaticMesh* Loaded = LoadObject<UStaticMesh>(nullptr, *AssetPath);
	PropMeshes.Add(Key, Loaded);
	if (!Loaded)
	{
		UE_LOG(
			LogIndieGame,
			Warning,
			TEXT("Prop mesh missing (run Scripts/generate_meshes.py): %s"),
			MeshName);
	}
	return Loaded ? Loaded : Fallback;
}

UStaticMeshComponent* AIGPrologueWorldScene::CreateProp(
	const TCHAR* MeshName,
	const FVector& BaseLocation,
	UMaterialInterface* Material,
	const float YawDegrees,
	const float UniformScale,
	const bool bEnableCollision)
{
	UStaticMesh* Mesh = PropMesh(MeshName);
	if (!Mesh)
	{
		return nullptr;
	}

	UStaticMeshComponent* Prop = NewObject<UStaticMeshComponent>(
		this,
		*FString::Printf(TEXT("Prop_%d"), BlockCounter++));
	Prop->SetupAttachment(ActiveParent ? ActiveParent.Get() : SceneRoot.Get());
	Prop->SetStaticMesh(Mesh);
	// nullptr는 메시 재질 그대로. CreateBlock과 같은 규칙이다.
	if (Material)
	{
		Prop->SetMaterial(0, Material);
	}
	Prop->SetRelativeLocation(BaseLocation);
	Prop->SetRelativeRotation(FRotator(0.0f, YawDegrees, 0.0f));
	Prop->SetRelativeScale3D(FVector(UniformScale));
	Prop->SetMobility(EComponentMobility::Static);
	Prop->SetGenerateOverlapEvents(false);
	Prop->SetCanEverAffectNavigation(false);
	Prop->SetCollisionProfileName(
		bEnableCollision
			? UCollisionProfile::BlockAll_ProfileName
			: UCollisionProfile::NoCollision_ProfileName);
	Prop->RegisterComponent();
	GeometryComponents.Add(Prop);
	return Prop;
}

bool AIGPrologueWorldScene::AddStoreStockInstance(
	UStaticMesh* Mesh,
	UMaterialInterface* Material,
	const FTransform& RelativeTransform,
	const bool bCastShadow)
{
	if (!Mesh)
	{
		return false;
	}

	USceneComponent* ResolvedParent =
		ActiveParent ? ActiveParent.Get() : SceneRoot.Get();
	const FString BatchKey = FString::Printf(
		TEXT("%s|%s|%s|shadow=%d"),
		*Mesh->GetPathName(),
		*GetPathNameSafe(Material),
		*GetPathNameSafe(ResolvedParent),
		bCastShadow ? 1 : 0);

	UInstancedStaticMeshComponent* Batch = nullptr;
	if (const TObjectPtr<UInstancedStaticMeshComponent>* Existing =
			StoreStockBatches.Find(BatchKey))
	{
		Batch = Existing->Get();
	}
	else
	{
		Batch = NewObject<UInstancedStaticMeshComponent>(
			this,
			*FString::Printf(
				TEXT("StoreStockBatch_%02d"),
				StoreStockBatches.Num()));
		Batch->SetupAttachment(ResolvedParent);
		Batch->SetStaticMesh(Mesh);
		// 물병 띠는 공용 페이지 높이의 1/8만 쓴다. 가까운 글자의 밉이 먼저 내려가지 않게 한다.
		if (Mesh->GetFName() == FName(TEXT("SM_LabelSleeve")))
		{
			Batch->StreamingDistanceMultiplier = 8.0f;
		}
		// nullptr는 「메시가 가진 재질 그대로」다. 구운 상품(과자 상자·삼각김밥)은
		// 자기 인스턴스를 들고 오므로 덮어쓰면 기본 회색이 된다.
		if (Material)
		{
			Batch->SetMaterial(0, Material);
		}
		Batch->SetMobility(EComponentMobility::Static);
		Batch->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Batch->SetGenerateOverlapEvents(false);
		Batch->SetCanEverAffectNavigation(false);
		Batch->SetReceivesDecals(false);
		Batch->SetCastShadow(bCastShadow);
		// Tiny packages do not justify entries in the Lumen distance-field scene.
		// They still receive direct light, material response and screen traces.
		Batch->SetAffectDistanceFieldLighting(false);
		// Start/end are kept as one authoring contract so a stock material may
		// consume PerInstanceFadeAmount later. Opaque materials that do not use
		// that node are still GPU-culled at the end distance; do not describe
		// this interval as a visual blend until the material has been verified.
		Batch->SetCullDistances(
			IGPrologueWorld::StoreStockCullStartCentimeters,
			IGPrologueWorld::StoreStockCullEndCentimeters);
		StoreStockBatches.Add(BatchKey, Batch);
	}

	return Batch && Batch->AddInstance(RelativeTransform) != INDEX_NONE;
}

bool AIGPrologueWorldScene::AddStoreStockProp(
	const TCHAR* MeshName,
	const FVector& BaseLocation,
	UMaterialInterface* Material,
	const float YawDegrees,
	const float UniformScale,
	const bool bCastShadow)
{
	return AddStoreStockInstance(
		PropMesh(MeshName),
		Material,
		FTransform(
			FRotator(0.0f, YawDegrees, 0.0f),
			BaseLocation,
			FVector(UniformScale)),
		bCastShadow);
}

void AIGPrologueWorldScene::AddStoreStockBlock(
	const FVector& Center,
	const FVector& SizeCentimeters,
	UMaterialInterface* Material,
	const bool bCastShadow,
	const FRotator& Rotation)
{
	const bool bAdded = AddStoreStockInstance(
		CubeMesh,
		Material,
		FTransform(Rotation, Center, SizeCentimeters / 100.0f),
		bCastShadow);
	ensureMsgf(bAdded, TEXT("The store-stock cube fallback must always be available."));
}

void AIGPrologueWorldScene::AddStoreStockBottleLabel(
	const FVector& BottleBase,
	const float Radius,
	const float BandBottomZ,
	const float BandHeight,
	const TCHAR* LabelMaterialName,
	const float YawDegrees)
{
	UMaterialInterface* LabelMaterial =
		TexMat(LabelMaterialName, FridgeInteriorMaterial);
	const bool bAddedSleeve = AddStoreStockInstance(
			PropMesh(TEXT("SM_LabelSleeve")),
			LabelMaterial,
			FTransform(
				FRotator(0.0f, YawDegrees, 0.0f),
				BottleBase + FVector(0.0f, 0.0f, BandBottomZ),
				FVector(Radius, Radius, BandHeight)),
			false);
	ensureMsgf(
		bAddedSleeve,
		TEXT("SM_LabelSleeve is release-required; a flat panel cannot replace wrap film."));
}

void AIGPrologueWorldScene::FinalizeStoreStockBatches()
{
	// Register once per batch. Registering before every AddInstance rebuilds
	// render state repeatedly during BeginPlay and produces an avoidable hitch.
	for (const TPair<FString, TObjectPtr<UInstancedStaticMeshComponent>>& Pair :
		StoreStockBatches)
	{
		UInstancedStaticMeshComponent* Batch = Pair.Value.Get();
		if (!Batch || Batch->IsRegistered())
		{
			continue;
		}
		Batch->RegisterComponent();
		GeometryComponents.Add(Batch);
	}
}

bool AIGPrologueWorldScene::ValidateStoreStockBatches(
	int32& OutBatchCount,
	int32& OutInstanceCount) const
{
	OutBatchCount = StoreStockBatches.Num();
	OutInstanceCount = 0;
	bool bConfigurationValid = true;

	for (const TPair<FString, TObjectPtr<UInstancedStaticMeshComponent>>& Pair :
		StoreStockBatches)
	{
		const UInstancedStaticMeshComponent* Batch = Pair.Value.Get();
		if (!Batch)
		{
			bConfigurationValid = false;
			continue;
		}

		OutInstanceCount += Batch->GetInstanceCount();
		int32 CullStart = 0;
		int32 CullEnd = 0;
		Batch->GetCullDistances(CullStart, CullEnd);
		bConfigurationValid = bConfigurationValid
			&& Batch->IsRegistered()
			&& Batch->GetMobility() == EComponentMobility::Static
			&& Batch->GetCollisionEnabled() == ECollisionEnabled::NoCollision
			&& CullStart == IGPrologueWorld::StoreStockCullStartCentimeters
			&& CullEnd == IGPrologueWorld::StoreStockCullEndCentimeters;
	}

	return bConfigurationValid
		&& OutInstanceCount == IGPrologueWorld::ExpectedStoreStockInstances
		&& OutBatchCount > 0
		&& OutBatchCount <= IGPrologueWorld::MaximumStoreStockBatches;
}

UStaticMesh* AIGPrologueWorldScene::FindPhotoPropMesh(const TCHAR* AssetId) const
{
	// Interchange can create several meshes in one source folder. Registry
	// iteration order is not stable, and choosing its first entry previously
	// turned a cash register into a loose drawer and an outdoor set into one
	// chair. Explicitly select the authored primary mesh.
	static const TMap<FName, FName> PreferredAssetNames = {
		{TEXT("metal_office_desk"), TEXT("metal_office_desk")},
	};
	const FName AssetIdName(AssetId);
	const FName PreferredName = PreferredAssetNames.Contains(AssetIdName)
		? PreferredAssetNames[AssetIdName]
		: FName(FString::Printf(TEXT("%s_1k"), AssetId));

	// Required photo props must be available while the world is constructed,
	// including a first run with an empty Asset Registry discovery cache. The
	// import pipeline owns this stable package convention, so load the authored
	// primary object directly before falling back to a registry search for
	// future import variants.
	const FString PreferredObjectPath = FString::Printf(
		TEXT("/Game/Photo/Props/%s/%s_1k/StaticMeshes/%s.%s"),
		AssetId,
		AssetId,
		*PreferredName.ToString(),
		*PreferredName.ToString());
	if (UStaticMesh* DirectMesh = LoadObject<UStaticMesh>(
		nullptr,
		*PreferredObjectPath,
		nullptr,
		LOAD_NoWarn))
	{
		return DirectMesh;
	}

	const FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	RegistryModule.Get().GetAssetsByPath(
		*FString::Printf(TEXT("/Game/Photo/Props/%s"), AssetId), Assets, true);

	TArray<FAssetData> StaticMeshes;
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName())
		{
			StaticMeshes.Add(Asset);
			if (Asset.AssetName == PreferredName)
			{
				return Cast<UStaticMesh>(Asset.GetAsset());
			}
		}
	}

	// A newly imported source may use a different suffix. Keep that fallback
	// deterministic so two machines still build the same world.
	StaticMeshes.Sort([](const FAssetData& Left, const FAssetData& Right)
	{
		return Left.AssetName.LexicalLess(Right.AssetName);
	});
	if (!StaticMeshes.IsEmpty())
	{
		UE_LOG(
			LogIndieGame,
			Warning,
			TEXT("Photo prop '%s' missing preferred mesh '%s'; using '%s'."),
			AssetId,
			*PreferredName.ToString(),
			*StaticMeshes[0].AssetName.ToString());
		return Cast<UStaticMesh>(StaticMeshes[0].GetAsset());
	}
	return nullptr;
}

UStaticMeshComponent* AIGPrologueWorldScene::PlacePhotoProp(
	const TCHAR* AssetId,
	const FVector& FloorCenter,
	const FVector& TargetSize,
	const float YawDegrees,
	const bool bEnableCollision)

{
	return PlacePhotoPropInternal(
		AssetId, FloorCenter, TargetSize, YawDegrees, bEnableCollision, true);
}

UStaticMeshComponent* AIGPrologueWorldScene::PlacePhotoPropExactSize(
	const TCHAR* AssetId,
	const FVector& FloorCenter,
	const FVector& TargetSize,
	const float YawDegrees,
	const bool bEnableCollision)
{
	return PlacePhotoPropInternal(
		AssetId, FloorCenter, TargetSize, YawDegrees, bEnableCollision, false);
}

UStaticMeshComponent* AIGPrologueWorldScene::PlacePhotoPropInternal(
	const TCHAR* AssetId,
	const FVector& FloorCenter,
	const FVector& TargetSize,
	const float YawDegrees,
	const bool bEnableCollision,
	const bool bPreserveAspectRatio)
{
	UStaticMesh* Mesh = FindPhotoPropMesh(AssetId);
	if (!Mesh)
	{
		return nullptr;
	}

	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const FVector MeshSize = Bounds.BoxExtent * 2.0f;
	if (MeshSize.GetMin() <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	FVector Scale = TargetSize / MeshSize;
	if (bPreserveAspectRatio)
	{
		const float UniformScale = Scale.GetMin();
		Scale = FVector(UniformScale);
	}

	UStaticMeshComponent* Prop = NewObject<UStaticMeshComponent>(
		this,
		*FString::Printf(TEXT("PhotoProp_%d"), BlockCounter++));
	Prop->SetupAttachment(ActiveParent ? ActiveParent.Get() : SceneRoot.Get());
	Prop->SetStaticMesh(Mesh);
	// Imported photo props already carry the project's audited fallback LODs.
	// Force that path instead of asking every third-party material (including
	// translucent glass slots) for a Nanite shader permutation it cannot use.
	// Without this, a cooked build may replace the scan with the grey default
	// material even though it looked correct after an editor-side recompile.
	Prop->bDisallowNanite = true;
	Prop->SetMobility(EComponentMobility::Static);
	Prop->SetGenerateOverlapEvents(false);
	Prop->SetCanEverAffectNavigation(false);
	Prop->SetCollisionProfileName(
		bEnableCollision
			? UCollisionProfile::BlockAll_ProfileName
			: UCollisionProfile::NoCollision_ProfileName);

	const FRotator Rotation(0.0f, YawDegrees, 0.0f);
	const FVector RotatedOriginOffset =
		Rotation.RotateVector(FVector(
			Bounds.Origin.X * Scale.X,
			Bounds.Origin.Y * Scale.Y,
			0.0f));
	const FVector Location(
		FloorCenter.X - RotatedOriginOffset.X,
		FloorCenter.Y - RotatedOriginOffset.Y,
		FloorCenter.Z - (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale.Z);
	Prop->SetRelativeLocation(Location);
	Prop->SetRelativeRotation(Rotation);
	Prop->SetRelativeScale3D(Scale);
	Prop->RegisterComponent();
	GeometryComponents.Add(Prop);
	return Prop;
}

UAudioComponent* AIGPrologueWorldScene::CreateAmbientBed(
	USoundBase* Sound,
	const FVector& Location,
	const float Volume,
	const float InnerRadius,
	const float FalloffDistance)
{
	UAudioComponent* Bed = NewObject<UAudioComponent>(
		this,
		*FString::Printf(TEXT("AmbientBed_%d"), BlockCounter++));
	Bed->SetupAttachment(SceneRoot);
	Bed->SetRelativeLocation(Location);
	Bed->SetSound(Sound);
	Bed->SetVolumeMultiplier(Volume);
	Bed->bAutoActivate = false;
	// 손으로 쓴 AttenuationOverrides는 헤드폰/스피커 전환이 훑는 목록에 안 들고
	// 리버브 센드도 없었다. 다른 소리와 같은 감쇠를 쓴다. 늘 우는 베드는 보이스
	// 상한에서 밀려나면 그 밤 내내 안 돌아오므로 상시 베드로 건다.
	Bed->AttenuationSettings = IGAudio::MakeAttenuation(
		this, InnerRadius, FalloffDistance, EIGAudioBus::World);
	Bed->bAllowSpatialization = true;
	Bed->RegisterComponent();
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->PrepareSound(Sound, EIGAudioBus::World);
			AudioDirector->RegisterPersistentBed(Bed, EIGAudioBus::World);
		}
	}
	Bed->Play();
	AmbientBeds.Add(Bed);
	return Bed;
}

// ---------------------------------------------------------------------------
// Construction stages
// ---------------------------------------------------------------------------

void AIGPrologueWorldScene::InitializePrologue()
{
	if (bPrologueInitialized)
	{
		return;
	}

	if (!PositionPlayer())
	{
		if (++PlayerPositionAttempts < IGPrologueWorld::MaxPlayerPositionAttempts)
		{
			GetWorldTimerManager().SetTimerForNextTick(
				this,
				&ThisClass::InitializePrologue);
		}
		return;
	}

	bPrologueInitialized = true;

	// Interchange photo props are discovered asynchronously in editor-game
	// launches.  Building the world before the registry finished meant every
	// lookup missed and silently fell back to cubes on a cold run.
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
		.Get()
		.WaitForCompletion();
	LoadTexturedMaterials();
	CardboardMaterial = TexMat(
		TEXT("M_MovingBoxCardboardUV"),
		CardboardMaterial);

	// Unit 403 and its corridor live on the 4th floor, three slabs up.
	// Static mobility is required so the static wall blocks can attach.
	UpperFloorRoot = NewObject<USceneComponent>(this, TEXT("UpperFloorRoot"));
	UpperFloorRoot->SetupAttachment(SceneRoot);
	UpperFloorRoot->SetMobility(EComponentMobility::Static);
	UpperFloorRoot->SetRelativeLocation(FVector(0, 0, 900));
	UpperFloorRoot->RegisterComponent();

	// 빌더가 만드는 광원은 그 빌더의 공간에 속한다. 보이지 않는 층의 광원을
	// 끄는 UpdateLightZones가 이 표를 쓴다.
	BuildingLightZone = EIGLightZone::HomeInterior;
	BuildApartment();
	BuildingLightZone = EIGLightZone::FourthFloor;
	BuildCorridor();
	BuildingLightZone = EIGLightZone::StairCore;
	BuildStairCore();
	BuildLowerFloors();
	BuildingLightZone = EIGLightZone::Lobby;
	BuildLobby();
	BuildingLightZone = EIGLightZone::Annex;
	BuildFifthFloorAnnex();
	BuildingLightZone = EIGLightZone::Alley;
	BuildAlley();
	BuildingLightZone = EIGLightZone::Store;
	BuildStore();
	BuildingLightZone = EIGLightZone::Always;
	BuildSkyAndFog();
	BuildDistantSkyline();
	SpawnInteractables();
	UpdateLightZones();
	GetWorldTimerManager().SetTimer(
		LightZoneTimer, this, &ThisClass::UpdateLightZones, 0.1f, true);
	UpdateNeighborhoodAwake();
	GetWorldTimerManager().SetTimer(
		NeighborhoodAwakeTimer, this, &ThisClass::UpdateNeighborhoodAwake, 4.0f, true);

	RefreshPurchaseProfilePresentation();
	CreateAmbience();

	// 평범한 골목 생활은 이야기가 기대는 기준선이다. 샛길로 드나드는 배달 오토바이,
	// 바람에 날리는 잎, 시야 끝을 스치는 길고양이가 그 몫을 한다.
	FActorSpawnParameters NeighborhoodParameters;
	NeighborhoodParameters.Owner = this;
	NeighborhoodParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	NeighborhoodLifeDirector = GetWorld()->SpawnActor<AIGNeighborhoodLifeDirector>(
		AIGNeighborhoodLifeDirector::StaticClass(),
		FTransform::Identity,
		NeighborhoodParameters);
	if (NeighborhoodLifeDirector)
	{
		// 골목은 양끝이 막혀 있다 — 서쪽은 X -340..-320 막다른 벽, 동쪽은
		// X 2398 편의점 정면이다. 예전 구간(-360..2390)은 차체 길이를 세지
		// 않아서, 승용차가 벽에 반쯤 걸친 채 생겨나 벽을 뚫고 나왔고
		// 반대편에서는 코가 X 2597까지 들어가 과자 매대 사이에 섰다.
		// 스폰 자리를 벽 뒤로 완전히 물리고, 끝은 정면 앞에서 끊는다.
		NeighborhoodLifeDirector->ConfigureNeighborhood(
			GetActorTransform().TransformPosition(FVector(-540.0f, -555.0f, -5.0f)),
			GetActorTransform().TransformPosition(FVector(2190.0f, -555.0f, -5.0f)),
			4040444);
	}

	// A tired ballast shimmer runs for the whole session.
	GetWorldTimerManager().SetTimer(
		CorridorFlickerHandle,
		this,
		&ThisClass::HandleCorridorFlicker,
		0.09f,
		true);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UIGStoryStateSubsystem* StoryState =
			GameInstance->GetSubsystem<UIGStoryStateSubsystem>())
		{
			StoryState->OnStoryStateTagChanged.AddUniqueDynamic(
				this,
				&ThisClass::HandleStoryStateChanged);
		}
	}

	UE_LOG(LogIndieGame, Display, TEXT("Prologue world ready: apartment, alley and store assembled."));
}

bool AIGPrologueWorldScene::PositionPlayer()
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!PlayerController || !PlayerPawn)
	{
		return false;
	}

	PlayerPawn->SetActorLocationAndRotation(
		IGPrologueWorld::PlayerLocation,
		IGPrologueWorld::PlayerActorRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	PlayerController->SetControlRotation(IGPrologueWorld::PlayerViewRotation);
	return true;
}

void AIGPrologueWorldScene::BuildApartment()
{
	// Everything here is 4th-floor local; the root lifts it into place.
	ActiveParent = UpperFloorRoot;

	UMaterialInterface* Jangpan = TexMat(TEXT("M_Jangpan"), FloorMaterial);
	UMaterialInterface* WallX = TexMat(TEXT("M_Wallpaper_X"), WallMaterial);
	UMaterialInterface* WallY = TexMat(TEXT("M_Wallpaper_Y"), WallMaterial);
	UMaterialInterface* CeilHome = TexMat(TEXT("M_WallpaperCeil"), WallMaterial);
	UMaterialInterface* WallPatina = TexMat(TEXT("M_ApartmentWallPatina"), nullptr);
	UMaterialInterface* Furniture = TexMat(TEXT("M_WoodFurnitureUV"), WoodMaterial);
	UMaterialInterface* Bedding = TexMat(TEXT("M_BeddingUV"), BeddingMaterial);
	UMaterialInterface* Metal = TexMat(TEXT("M_MetalUV"), MetalFrameMaterial);

	// Shell: interior 380 x 430 cm (about 5 pyeong), 230 cm ceiling.
	IGPrologueWorld::TagFootstepSurface(
		CreateBlock(FVector(0, 10, -10), FVector(440, 470, 20), Jangpan),
		IGPrologueWorld::FootstepVinylTag);
	CreateBlock(FVector(0, 0, 240), FVector(440, 490, 20), CeilHome);
	CreateBlock(FVector(-200, 0, 115), FVector(20, 490, 230), WallY);
	CreateBlock(FVector(200, 0, 115), FVector(20, 490, 230), WallY);
	// The east wall projects ten centimetres past the south wall into the
	// corridor. Its exposed end is a Y-facing surface, so the wall's X-facing
	// material stretched into dense horizontal bands at the authored corridor
	// camera. Cap that return with the correctly oriented wallpaper material.
	CreateBlock(FVector(200, -245.6f, 115), FVector(20, 0.8f, 230), WallX, false);
	CreateBlock(FVector(0, 225, 115), FVector(440, 20, 230), WallX);
	// 문틀 바깥 개구부 X 76..186. 동쪽 내벽은 유지하고 서쪽으로 넓힌다.
	CreateBlock(FVector(-72, -225, 115), FVector(296, 20, 230), WallX);
	CreateBlock(FVector(203, -225, 115), FVector(34, 20, 230), WallX);
	// A 210 cm clear opening leaves headroom above the 8 cm Korean shoe step.
	// The former 200 cm soffit exactly touched the 192 cm player capsule once
	// the pawn stood on the step, making the doorway look open but impassable.
	CreateBlock(FVector(131, -225, 220), FVector(110, 20, 20), WallX);
	// Entrance shoe step.
	CreateBlock(FVector(131, -195, 4), FVector(106, 40, 8), TexMat(TEXT("M_GraniteTile_XY"), ConcreteDarkMaterial));

	// 얼룩 아래로 벽지의 꽃무늬와 요철이 이어진다. 투영 깊이는 벽에만 닿게 제한한다.
	if (WallPatina)
	{
		const auto AddDamp = [this, WallPatina](const TCHAR* Name,
			const FVector& Center, const FVector& Extent, float Yaw)
		{
			UDecalComponent* Damp = NewObject<UDecalComponent>(this, FName(Name));
			Damp->SetupAttachment(UpperFloorRoot);
			Damp->SetRelativeLocation(Center);
			Damp->SetRelativeRotation(FRotator(0, Yaw, 0));
			Damp->DecalSize = Extent;
			Damp->SetDecalMaterial(WallPatina);
			Damp->SetFadeScreenSize(0.012f);
			Damp->RegisterComponent();
		};
		AddDamp(TEXT("ApartmentSouthDamp"), FVector(-82,-215,47), FVector(1.2f,74,44), -90);
		AddDamp(TEXT("ApartmentWestDamp"), FVector(-190,58,47), FVector(1.2f,63,44), 180);
	}

	// §11 V2: 403호는 프롤로그(깨끗) → 밤4(천장 모서리 균열 진행)로 3단계
	// 노화한다. Every plane is built now and hidden; SetUnit403AgeStage reveals
	// them. Building them later would make the room pop the first time the story
	// state changed, and the whole point is that the damage was always coming.
	//
	// The cracks start in the two ceiling corners over the wall he is behind. A
	// player who never looks up never learns it, and one who does gets the only
	// warning the apartment ever gives: this is spreading toward you.
	Unit403AgeStageOne.Reset();
	Unit403AgeStageTwo.Reset();
	if (PlaneMesh)
	{
		UMaterialInterface* Cracks =
			TexMat(TEXT("M_MissingFloorCavityScratches"), nullptr);
		UMaterialInterface* DampLift =
			TexMat(TEXT("M_DecalDampWallpaper"), nullptr);
		const auto AddAging = [this](
			TArray<TObjectPtr<UStaticMeshComponent>>& Stage,
			const FVector& Center,
			const FVector& Size,
			UMaterialInterface* Material,
			const FRotator& Rotation)
		{
			if (!Material)
			{
				return;
			}
			if (UStaticMeshComponent* Plane = CreateBlock(
				Center,
				Size,
				Material,
				false,
				PlaneMesh,
				Rotation))
			{
				// V2 contract: 0.15 cm off the surface, no shadow, no collision,
				// no distance field. The trace only wets what is already there.
				Plane->SetCastShadow(false);
				Plane->SetCanEverAffectNavigation(false);
				Plane->SetAffectDistanceFieldLighting(false);
				Plane->SetCullDistance(950.0f);
				Plane->SetHiddenInGame(true);
				Stage.Add(Plane);
			}
		};

		// Stage one: hairline cracks in the north-east ceiling corner, and the
		// first lift of wallpaper where the damp behind it has started to work.
		AddAging(
			Unit403AgeStageOne,
			FVector(150.0f, 200.0f, 229.85f),
			FVector(72.0f, 78.0f, 1.0f),
			Cracks,
			FRotator::ZeroRotator);
		AddAging(
			Unit403AgeStageOne,
			FVector(189.85f, 196.0f, 196.0f),
			FVector(58.0f, 46.0f, 1.0f),
			DampLift,
			FRotator(90.0f, 0.0f, 0.0f));

		// Stage two: it has crossed the ceiling and come down the corner. 곰팡이
		// 모서리 is the same damp overlay taken further down the wall, because
		// mould follows the water and the water has had four more nights.
		AddAging(
			Unit403AgeStageTwo,
			FVector(96.0f, 200.0f, 229.85f),
			FVector(96.0f, 84.0f, 1.0f),
			Cracks,
			FRotator(0.0f, 12.0f, 0.0f));
		AddAging(
			Unit403AgeStageTwo,
			FVector(189.85f, 214.0f, 122.0f),
			FVector(128.0f, 62.0f, 1.0f),
			DampLift,
			FRotator(90.0f, 0.0f, 0.0f));
		AddAging(
			Unit403AgeStageTwo,
			FVector(178.0f, 213.85f, 34.0f),
			FVector(74.0f, 58.0f, 1.0f),
			DampLift,
			FRotator(0.0f, 0.0f, 90.0f));
	}
	ApplyUnit403AgeStage();

	// 창틀 안쪽 면은 Y=206.5 부근까지 나온다. 침대를 창에서 12cm 떼고
	// 프레임·침구·눕기 판정을 함께 옮긴다. 발치 협탁과도 간격을 남긴다.
	if (UStaticMeshComponent* Frame = PlacePhotoProp(TEXT("old_bed_frame"), FVector(-140, 98, 0), FVector(108, 208, 130), 180.0f))
	{
		// 프레임의 단순 충돌은 빈 공간까지 감싼다. 조사 판정은 침구의 눕기 대상에 맡긴다.
		Frame->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}
	else
	{
		CreateBlock(FVector(-140, 98, 20), FVector(100, 200, 40), Furniture);
		CreateBlock(FVector(-140, 199, 55), FVector(100, 8, 110), Furniture);
	}
	// 천 계산은 제작할 때만 한다. 게임에서는 한 재질과 단순 매트리스 충돌을 쓴다.
	if (UStaticMesh* BeddingMesh = PropMesh(TEXT("SM_ApartmentBedding")))
	{
		UStaticMeshComponent* Bed = CreateBlock(FVector(-140,98,38), FVector(100),
			nullptr, true, BeddingMesh);
		Bed->ComponentTags.Add(TEXT("Apartment.Bedding"));
		// 눈에 보이는 침구를 가리킬 때는 눕기 대상이 시선 판정을 받는다.
		Bed->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}

	// Bedside table with an articulated lamp, desk with chair, wardrobe. Scanned
	// props keep their aspect ratio, so "fit in 58x58x60" does not guarantee a
	// 60 cm result. Measure the registered component and use its real upper
	// bound for every object that rests on it.
	UStaticMeshComponent* BedsideTable = PlacePhotoProp(
		TEXT("side_table_01"),
		FVector(-160, -35, 0),
		FVector(58, 58, 60),
		0.0f);
	if (!BedsideTable)
	{
		BedsideTable = CreateBlock(
			FVector(-160, -35, IGPrologueWorld::BedsideTableTopZ * 0.5f),
			FVector(55, 55, IGPrologueWorld::BedsideTableTopZ),
			Furniture);
	}
	float BedsideSurfaceLocalZ = IGPrologueWorld::BedsideTableTopZ;
	if (BedsideTable)
	{
		const FBoxSphereBounds TableBounds = BedsideTable->CalcBounds(
			BedsideTable->GetComponentTransform());
		const float BedsideSurfaceWorldZ =
			TableBounds.Origin.Z + TableBounds.BoxExtent.Z;
		const float UpperFloorWorldZ = UpperFloorRoot
			? UpperFloorRoot->GetComponentLocation().Z
			: IGPrologueWorld::FourthFloorZ;
		BedsideSurfaceLocalZ = BedsideSurfaceWorldZ - UpperFloorWorldZ;
	}
	UStaticMeshComponent* DeskLampFixture = PlacePhotoProp(
		TEXT("desk_lamp_arm_01"),
		FVector(-172, -52, BedsideSurfaceLocalZ),
		FVector(30, 30, 48),
		0.0f,
		false);
	FVector BedsideEmitterLocal(-164, -45, 101);
	if (DeskLampFixture && BedsideTable)
	{
		const FBoxSphereBounds TableBounds = BedsideTable->CalcBounds(BedsideTable->GetComponentTransform());
		const FVector TableFrontWorld(TableBounds.Origin.X, TableBounds.Origin.Y + TableBounds.BoxExtent.Y,
			TableBounds.Origin.Z + TableBounds.BoxExtent.Z);
		const FVector TableFrontLocal = DeskLampFixture->GetAttachParent()->GetComponentTransform()
			.InverseTransformPosition(TableFrontWorld);
		// 스캔 원본의 집게 입구(0, 0, -1cm)를 협탁 앞 모서리에 물린다.
		const FVector ClampBefore = DeskLampFixture->GetRelativeTransform().TransformPosition(FVector(0, 0, -1));
		const FVector ClampTarget(-172, TableFrontLocal.Y - .6f, BedsideSurfaceLocalZ);
		// 고정 소품은 등록된 채로 옮길 수 없다. 집게 위치를 맞춘 뒤 다시 등록한다.
		DeskLampFixture->UnregisterComponent();
		DeskLampFixture->SetRelativeLocation(DeskLampFixture->GetRelativeLocation() + ClampTarget - ClampBefore);
		DeskLampFixture->RegisterComponent();
		// 원본 갓 안쪽에서 잰 전구 중심. 회전·크기가 바뀌어도 갓과 같이 이동한다.
		BedsideEmitterLocal = DeskLampFixture->GetRelativeTransform().TransformPosition(FVector(-.15f, -17.1f, 68.8f));
	}
	// Furniture is dimensioned around a seated adult: 74 cm work surface and a
	// roughly 43 cm chair seat. The source desk is two metres wide, so the usual
	// aspect-preserving fit would shrink its height to about 41 cm.
	UStaticMeshComponent* Desk = PlacePhotoPropExactSize(
		TEXT("metal_office_desk"),
		FVector(-100, -179, 0),
		FVector(145, 68, IGPrologueWorld::DeskWorkSurfaceHeight),
		0.0f);
	if (!Desk)
	{
		Desk = CreateBlock(
			FVector(-100, -179, 72.5f), FVector(145, 68, 3), Furniture);
		for (const float LegX : {-164.0f, -36.0f})
		{
			for (const float LegY : {-207.0f, -151.0f})
			{
				CreateBlock(FVector(LegX, LegY, 36), FVector(4, 4, 72), Metal);
			}
		}
	}
	float DeskSurfaceLocalZ = IGPrologueWorld::DeskWorkSurfaceHeight;
	if (Desk)
	{
		const FBoxSphereBounds DeskBounds = Desk->CalcBounds(Desk->GetComponentTransform());
		DeskSurfaceWorldZ = DeskBounds.Origin.Z + DeskBounds.BoxExtent.Z;
		const float UpperFloorWorldZ = UpperFloorRoot
			? UpperFloorRoot->GetComponentLocation().Z
			: IGPrologueWorld::FourthFloorZ;
		DeskSurfaceLocalZ = DeskSurfaceWorldZ - UpperFloorWorldZ;
	}
	if (!PlacePhotoPropExactSize(
			TEXT("painted_wooden_chair_01"),
			FVector(-100, -118, 0),
			FVector(48, 50, 88),
			180.0f))
	{
		CreateBlock(FVector(-100, -118, 44), FVector(46, 46, 4), Furniture);
		CreateBlock(FVector(-100, -139, 66), FVector(46, 4, 48), Furniture);
		for (const float LegX : {-120.0f, -80.0f})
		{
			for (const float LegY : {-137.0f, -99.0f})
			{
				CreateBlock(FVector(LegX, LegY, 21), FVector(3, 3, 42), Furniture);
			}
		}
	}
	// 옷장. `FindPhotoPropMesh`가 이 에셋만 일부러 nullptr을 돌려준다 —
	// 원본이 몸통을 스켈레탈로만 들여와서 정적 항목이 문짝 둘뿐이기
	// 때문이다. 그래서 실제로 사는 것은 언제나 아래 상자다. 언젠가 몸통이
	// 제대로 들어오면 같은 자리 같은 크기로 대체되도록 맞춤 상자와 요각을
	// 상자에 맞춰 둔다.
	//
	// 자리를 옮겼다. 원래 (-172, -70)은 Y -110..-30을 먹어서 협탁
	// (Y -62..-8)과 32 cm 겹쳐 있었고, 그 겹친 자리에 스탠드 메시와 방의
	// 유일한 온색 광원 (-164, -45, 101)이 같이 들어가 있었다. 사진 소품이
	// 없는 체크아웃에서는 침실의 하나뿐인 등이 상자 안에서 켜진다.
	// 서쪽 벽에서 비어 있는 구간은 책상 끝(Y -145)과 협탁 앞(Y -62) 사이
	// 83 cm뿐이다. Y -104에 폭 80으로 놓으면 Y -144..-64로 양쪽에 1~2 cm를
	// 남기고 들어간다.
	// 옷장은 Blender 메시(Scripts/blender/build_apartment_props.py)가 정식이다.
	// 같은 40 x 80 x 180 봉투에 문 둘·손잡이·플린스가 들어 있고 앞면이 +X다.
	UStaticMesh* WardrobeMesh = PropMesh(TEXT("SM_Wardrobe"));
	if (WardrobeMesh)
	{
		CreateBlock(
			FVector(-170, -104, 0), FVector(100, 100, 100),
			nullptr, true, WardrobeMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(-170, -104, 90), FVector(40, 80, 180), Furniture);
	}

	// Eye-level dressing gives the room a personal history without blocking a
	// route or becoming false evidence. Each tiny mesh has a short draw range;
	// at corridor distance it is sub-pixel and should not cost a draw call.
	auto AddApartmentDressing = [this](
		const FVector& Center,
		const FVector& Size,
		UMaterialInterface* Material,
		UStaticMesh* Mesh,
		const FRotator& Rotation)
	{
		UStaticMeshComponent* Component = CreateBlock(
			Center, Size, Material, false, Mesh, Rotation);
		if (Component)
		{
			Component->SetCullDistance(850.0f);
			Component->SetAffectDistanceFieldLighting(false);
		}
		return Component;
	};
	if (UStaticMesh* PencilCup = PropMesh(TEXT("SM_DeskPencilCup")))
	{
		AddApartmentDressing(FVector(-152,-187,DeskSurfaceLocalZ), FVector(100),
			nullptr, PencilCup, FRotator::ZeroRotator);
	}
	// 없는 층의 계약서는 이 자리에 눕는다. 보관 장면의 공책과 겹치지 않는다.
	if (!GetWorld()->URL.HasOption(TEXT("IGMissingFloor"))
		&& !GetWorld()->URL.HasOption(TEXT("IGListenerGreybox"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("IGMissingFloor"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreybox")))
	{
		AddApartmentDressing(
			FVector(-75.0f, -183.0f, DeskSurfaceLocalZ + 1.0f), FVector(25.0f, 18.0f, 2.2f),
			Bedding, nullptr, FRotator(0.0f, -4.0f, 0.0f));
		AddApartmentDressing(
			FVector(-75.0f, -183.0f, DeskSurfaceLocalZ + 3.05f), FVector(22.0f, 16.0f, 2.0f),
			TexMat(TEXT("M_PaperClean"), Bedding), nullptr, FRotator(0.0f, 3.0f, 0.0f));
	}

	// --- Built-in kitchen line along the east wall ---------------------------
	// A real 원룸 is fitted, not furnished: one continuous run of white gloss
	// carcasses with finger-pull grooves instead of handles, a stone counter,
	// an inset stainless sink, an induction hob, a range hood, and the drum
	// washer that always lives under the same worktop.
	UMaterialInterface* Gloss = TexMat(TEXT("M_KitchenGlossUV"), SignWhiteMaterial);
	UMaterialInterface* CounterStone = TexMat(TEXT("M_CounterStoneUV"), StoreFloorMaterial);
	UMaterialInterface* Stainless = TexMat(TEXT("M_StainlessUV"), MetalFrameMaterial);

	// 주방 일곱 점은 Blender 메시(Scripts/blender/build_kitchen.py)가 정식이다.
	// 상자 좌표를 그대로 원점으로 받도록 저작했다: 앞면 -X, 원점은 각 자리의
	// 바닥 중심. 메시가 하나라도 없으면 그 자리만 예전 상자로 내려간다.
	UStaticMesh* KitchenRunMesh = PropMesh(TEXT("SM_KitchenBaseRun"));
	UStaticMesh* WasherMesh = PropMesh(TEXT("SM_DrumWasher"));
	UStaticMesh* SinkMesh = PropMesh(TEXT("SM_KitchenSink"));
	UStaticMesh* HobMesh = PropMesh(TEXT("SM_InductionHob"));
	UStaticMesh* WallUnitsMesh = PropMesh(TEXT("SM_KitchenWallUnits"));
	UStaticMesh* RangeHoodMesh = PropMesh(TEXT("SM_RangeHood"));
	UStaticMesh* MicrowaveMesh = PropMesh(TEXT("SM_Microwave"));

	if (KitchenRunMesh)
	{
		// 캐비닛·걸레받이·상판(싱크 자리 뚫림)·백스플래시·문 둘이 한 메시다.
		CreateBlock(
			FVector(162, 128, 0), FVector(100, 100, 100),
			nullptr, true, KitchenRunMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		// Carcass, recessed toe kick and the splashback upstand.
		CreateBlock(FVector(162, 128, 46), FVector(56, 176, 72), Gloss);
		CreateBlock(FVector(166, 128, 5), FVector(48, 176, 10), PlasticDarkMaterial);
		CreateBlock(FVector(187, 128, 96), FVector(6, 176, 20), CounterStone, false);
		// Worktop, split around the sink cut-out at Y 160..205.
		CreateBlock(FVector(161, 99, 84.5f), FVector(60, 122, 5), CounterStone);
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(161, 211, 84.5f), FVector(60, 14, 5), CounterStone);
		for (const float RailY : {160.0f, 205.0f})
		{
			CreateBlock(FVector(161, RailY, 84.5f), FVector(60, 4, 5), CounterStone, false);
		}
		// Cabinet fronts under the hob and the sink; the groove is the handle.
		for (const float DoorY : {129.0f, 186.0f})
		{
			// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
			CreateBlock(FVector(133.2f, DoorY, 46), FVector(1.8f, 54, 68), Gloss, false);
			CreateBlock(FVector(132.0f, DoorY, 78), FVector(1.4f, 50, 1.6f), PlasticDarkMaterial, false);
		}
	}

	// Drum washer built into the south end of the run.
	if (WasherMesh)
	{
		// 앞판·포트홀·드럼·조작 패널. 드럼은 캐비닛 속으로 들어간다.
		CreateBlock(
			FVector(133.4f, 70, 0), FVector(100, 100, 100),
			nullptr, false, WasherMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(162, 70, 46), FVector(56, 58, 72), Gloss);
		CreateBlock(FVector(133.4f, 70, 76), FVector(1.8f, 54, 11), PlasticDarkMaterial, false);
		CreateBlock(
			FVector(133.2f, 70, 44), FVector(42, 42, 4),
			Stainless, false, CylinderMesh, FRotator(90, 0, 0));
		CreateBlock(
			FVector(132.0f, 70, 44), FVector(33, 33, 3),
			GlassMaterial, false, CylinderMesh, FRotator(90, 0, 0));
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(133.6f, 46, 44), FVector(2, 4, 14), Stainless, false);
	}

	// Inset stainless sink: a real basin with walls, a drain and a rim flange.
	if (SinkMesh)
	{
		// 원점은 림 윗면 중심. 볼은 상판 구멍으로 26 cm 내려가고 수전이 같이 온다.
		CreateBlock(
			FVector(160, 182, 87.4f), FVector(100, 100, 100),
			nullptr, false, SinkMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(160, 182, 63), FVector(46, 47, 2), Stainless);
		for (const float BasinY : {159.5f, 204.5f})
		{
			CreateBlock(FVector(160, BasinY, 75), FVector(46, 2, 26), Stainless, false);
		}
		CreateBlock(FVector(137.5f, 182, 75), FVector(2, 47, 26), Stainless, false);
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(182.5f, 182, 75), FVector(2, 47, 26), Stainless, false);
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(160, 182, 87.4f), FVector(54, 55, 1.4f), Stainless, false);
		CreateBlock(
			FVector(160, 182, 64.4f), FVector(9, 9, 1),
			PlasticDarkMaterial, false, CylinderMesh);
		// Gooseneck mixer tap: column, arc and spout, with the lever on the side.
		CreateBlock(FVector(178, 182, 100), FVector(4.4f, 4.4f, 26), Stainless, false, CylinderMesh);
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(
			FVector(169, 182, 113), FVector(4.4f, 4.4f, 20),
			Stainless, false, CylinderMesh, FRotator(90, 0, 0));
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(160, 182, 107), FVector(3.2f, 3.2f, 14), Stainless, false, CylinderMesh);
		CreateBlock(FVector(181, 182, 112), FVector(3, 9, 3), Stainless, false);
	}

	// Induction hob: black glass, two element rings and the touch strip.
	if (HobMesh)
	{
		CreateBlock(
			FVector(160, 128, 87), FVector(100, 100, 100),
			nullptr, false, HobMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(160, 128, 87.8f), FVector(46, 50, 1.4f), PlasticDarkMaterial, false);
		for (const FVector2D& Ring : {FVector2D(150, 116), FVector2D(170, 140)})
		{
			CreateBlock(
				FVector(Ring.X, Ring.Y, 88.6f), FVector(19, 19, 0.4f),
				ConcreteDarkMaterial, false, CylinderMesh);
		}
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(141, 128, 88.6f), FVector(6, 30, 0.4f), ConcreteDarkMaterial, false);
	}

	// --- Wall units: carcass, gloss doors, valance with a live LED strip ----
	if (WallUnitsMesh)
	{
		// LED 띠는 구운 발광 텍스처다.
		CreateBlock(
			FVector(174, 128, 0), FVector(100, 100, 100),
			nullptr, true, WallUnitsMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(174, 128, 178), FVector(32, 176, 68), Gloss);
		for (const float DoorY : {70.0f, 195.0f})
		{
			CreateBlock(FVector(157.4f, DoorY, 178), FVector(1.8f, 54, 64), Gloss, false);
			CreateBlock(FVector(156.2f, DoorY, 148), FVector(1.4f, 50, 1.6f), PlasticDarkMaterial, false);
		}
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(158, 128, 143), FVector(5, 176, 5), Gloss, false);
		CreateBlock(FVector(156.6f, 128, 142), FVector(2, 168, 1.6f), LightPanelMaterial, false);
	}
	// Range hood between the wall units, over the hob.
	if (RangeHoodMesh)
	{
		CreateBlock(
			FVector(172, 128, 0), FVector(100, 100, 100),
			nullptr, false, RangeHoodMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(172, 128, 168), FVector(36, 52, 26), Stainless, false);
		CreateBlock(FVector(166, 128, 151), FVector(24, 52, 9), Stainless, false);
		CreateBlock(FVector(154.5f, 128, 151), FVector(1.5f, 46, 4), PlasticDarkMaterial, false);
		CreateBlock(FVector(178, 128, 200), FVector(18, 26, 24), Stainless, false);
	}

	// 전자레인지의 바닥 전체와 고무발이 상판 안에 들어오도록 배치한다.
	if (MicrowaveMesh)
	{
		// 상판 윗면(Z 87)에 발이 닿는다.
		CreateBlock(
			FVector(157, 68, 87), FVector(100, 100, 100),
			nullptr, false, MicrowaveMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(157, 68, 100), FVector(34, 44, 26), Stainless, false);
		CreateBlock(FVector(139.6f, 72, 100), FVector(1.4f, 32, 20), GlassMaterial, false);
		CreateBlock(FVector(139.6f, 50, 100), FVector(1.4f, 7, 20), PlasticDarkMaterial, false);
	}

	// Lived-in unit 403: wall AC unit, outlets, a July calendar, range hood.
	// 에어컨은 Blender 메시. 같은 82 x 19 x 27 봉투, 앞면 +Y, 원점 바닥 중심.
	UStaticMesh* WallAcMesh = PropMesh(TEXT("SM_WallAirConditioner"));
	if (WallAcMesh)
	{
		CreateBlock(
			FVector(40, -206, 182.5f), FVector(100, 100, 100),
			nullptr, false, WallAcMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(40, -206, 196), FVector(82, 19, 27), FridgeBodyMaterial, false);
		CreateBlock(FVector(40, -196.2f, 188), FVector(70, 1.5f, 3), PlasticDarkMaterial, false);
	}
	CreateBlock(FVector(-70, -212.5f, 32), FVector(7, 2, 11), FridgeInteriorMaterial, false);
	// 7월 말이면 어느 집에나 꽂혀 있는 액체 전자모기향. 침대 머리맡 협탁 위 콘센트에
	// 꽂혀 있고, 불 끈 방에서 주황 점 하나가 밤새 켜져 있다(§11 V1 ④ 기기 LED).
	// 본체는 콘센트 판에 붙고 약병이 그 아래로 매달린다. 스탠드 갓이 Y -15~-25에
	// 걸려 있어 그보다 책상 쪽에 둔다.
	CreateBlock(FVector(-189, -48, 75), FVector(2, 7, 11), FridgeInteriorMaterial, false);
	CreateBlock(FVector(-185.5f, -48, 77), FVector(5, 5.5f, 7.5f), FridgeInteriorMaterial, false);
	CreateBlock(FVector(-186, -48, 70.25f), FVector(3.2f, 3.2f, 6), GlassMaterial, false, CylinderMesh);
	UMaterialInterface* RepellentLedMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_PumpIndicator.M_PumpIndicator"));
	if (RepellentLedMaterial)
	{
		UStaticMeshComponent* RepellentLed = CreateBlock(
			FVector(-182.95f, -48, 79), FVector(0.1f, 0.4f, 0.4f), RepellentLedMaterial, false);
		UMaterialInstanceDynamic* Led = RepellentLed->CreateAndSetMaterialInstanceDynamic(0);
		Led->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.9f, 0.28f, 0.04f));
		Led->SetScalarParameterValue(TEXT("Lit"), 3.0f);
	}
	CreateBlock(FVector(186.5f, 40, 32), FVector(2, 7, 11), FridgeInteriorMaterial, false);
	// 현재 입주는 2025년 7월이다. 2024년 사건 자료와 방의 생활 달력을 구분한다.
	const bool bCurrentStory = GetWorld()->URL.HasOption(TEXT("IGMissingFloor"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGMissingFloor"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreybox"));
	if (bCurrentStory)
	{
		if (UStaticMesh* Calendar = PropMesh(TEXT("SM_ApartmentCalendar2025")))
		{
			CreateBlock(FVector(-140, -214.81f, 154), FVector(100), nullptr, false, Calendar, FRotator(0, 180, 0));
		}
	}
	else
	{
		CreateBlock(FVector(-140, -214.86f, 154), FVector(0.18f, 31, 42),
			TexMat(TEXT("M_Calendar"), SignWhiteMaterial), false, nullptr, FRotator(0, 90, 0));
	}
	CreateBlock(FVector(170, 90, 182), FVector(46, 40, 22), Metal, false);
	CreateBlock(FVector(170, 90, 212), FVector(13, 13, 38), Metal, false, CylinderMesh);

	// Ceiling: the flush LED slab every 원룸 has, plus the perimeter molding
	// that finishes wallpaper to ceiling. Both are cold at this hour — the
	// room is lit by the lamp and the window, not by the fixture.
	// 꺼진 등이라 빛나지 않는 흰 플라스틱이다. 간판용 흰색은 스스로 빛나서
	// 새벽에 천장을 올려다보면 불 꺼진 방에 판만 하얗게 떠 있었다.
	CreateBlock(FVector(-30, 0, 227), FVector(96, 62, 6), FridgeInteriorMaterial, false);
	CreateBlock(FVector(-30, 0, 230.5f), FVector(104, 70, 3), Furniture, false);
	for (const float MoldY : {-213.0f, 213.0f})
	{
		CreateBlock(FVector(0, MoldY, 224), FVector(378, 5, 7), Furniture, false);
	}
	for (const float MoldX : {-187.0f, 187.0f})
	{
		CreateBlock(FVector(MoldX, 0, 224), FVector(5, 428, 7), Furniture, false);
	}

	// Apartment lighting: no ceiling light at this hour — only the warm
	// bedside lamp, the strip left on under the wall units, and the cool
	// spill through the window; the sky light carries the rest physically.
	UPointLightComponent* BedsideLamp = CreateLight(
		BedsideEmitterLocal, 255.0f, 340.0f,
		FLinearColor(1.0f, 0.53f, 0.25f), true, 3.0f);
	BedsideLamp->SetVolumetricScatteringIntensity(0.28f);
	CreateLight(
		FVector(-100, 190, 160), 132.0f, 460.0f,
		FLinearColor(0.42f, 0.58f, 0.90f), true, 32.0f);
	// A weak, shadowless cool bounce approximates the window contribution that
	// would otherwise disappear behind the wardrobe at this small scale. It is
	// intentionally too dim to flatten the lamp shadow or reveal the whole room.
	UPointLightComponent* WindowBounce = CreateLight(
		FVector(-55, -98, 132), 22.0f, 270.0f,
		FLinearColor(0.34f, 0.46f, 0.68f), false, 46.0f);
	WindowBounce->SetSpecularScale(0.12f);
	WindowBounce->SetVolumetricScatteringIntensity(0.0f);
	UPointLightComponent* UnderCabinet = CreateLight(
		FVector(150, 128, 138), 115.0f, 280.0f,
		FLinearColor(0.92f, 0.96f, 1.0f), true, 10.0f);
	UnderCabinet->SetVolumetricScatteringIntensity(0.2f);

	// Window frame and cross bars turn the glow plane into a real window.
	// 창틀·블라인드·인터폰·스위치·신발장은 Blender 메시
	// (Scripts/blender/build_apartment_fixtures.py)다. 창틀은 미닫이 두 짝과
	// 만나는 살까지 기하이고 유리는 없다 — 달빛 발광판이 그 뒤에 그대로 선다.
	// 원점은 창 개구부 아래 중심이고 틀 뒷면이 북쪽 벽면(Y 215)에 닿는다.
	UStaticMesh* WindowMesh = PropMesh(TEXT("SM_ApartmentWindow"));
	if (WindowMesh)
	{
		CreateBlock(
			FVector(-100, 212.5f, 100), FVector(100, 100, 100),
			nullptr, false, WindowMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(-100, 212, 196), FVector(130, 5, 7), PlasticDarkMaterial, false);
		CreateBlock(FVector(-100, 212, 104), FVector(130, 5, 7), PlasticDarkMaterial, false);
		CreateBlock(FVector(-163, 212, 150), FVector(7, 5, 99), PlasticDarkMaterial, false);
		CreateBlock(FVector(-37, 212, 150), FVector(7, 5, 99), PlasticDarkMaterial, false);
		CreateBlock(FVector(-100, 212, 150), FVector(124, 4, 5), PlasticDarkMaterial, false);
		CreateBlock(FVector(-100, 212, 150), FVector(5, 4, 92), PlasticDarkMaterial, false);
	}
	// Venetian blind, drawn up into its stack: the headrail, the pulled-up
	// slat bundle and the cord. Leaving it up keeps the moonlight beam.
	// 메시 원점은 헤드레일 윗면 중심이고 줄은 아래로 84 cm 내려온다.
	UStaticMesh* BlindMesh = PropMesh(TEXT("SM_VenetianBlind"));
	if (BlindMesh)
	{
		CreateBlock(
			FVector(-100, 207, 206), FVector(100, 100, 100),
			nullptr, false, BlindMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(-100, 207, 202), FVector(134, 7, 8), SignWhiteMaterial, false);
		for (const float SlatZ : {188.0f, 191.5f, 195.0f})
		{
			CreateBlock(FVector(-100, 206.5f, SlatZ), FVector(130, 6, 2), SignWhiteMaterial, false);
		}
		CreateBlock(FVector(-36, 205, 160), FVector(1.2f, 1.2f, 76), SignWhiteMaterial, false);
	}

	// Entrance wall: video intercom, the switch bank beside it, and the shoe
	// cabinet that stands against every Korean entryway.
	// 인터폰과 스위치는 남쪽 벽면(Y -215)에 붙는 메시다. 원점이 벽면 바닥 중심,
	// 앞면 +Y. 실물 크기의 외함 안에 LCD 하나만 들어가며 발광하지 않는다.
	UStaticMesh* IntercomMesh = PropMesh(TEXT("SM_VideoIntercom"));
	if (IntercomMesh)
	{
		CreateBlock(
			FVector(62, -214.8f, 136), FVector(100, 100, 100),
			nullptr, false, IntercomMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreatePrintedBlock(
			FVector(62, -213.4f, 146.15f), FVector(14, 2.6f, 20.3f),
			FridgeInteriorMaterial,
			TexMat(TEXT("M_Intercom"), SignWhiteMaterial),
			FVector(0, 1, 0));
	}
	// 이름은 SwitchPlate가 아니다 — M_SwitchPlate가 읽는 간판 텍스처 T_SwitchPlate_D와 겹친다.
	UStaticMesh* SwitchMesh = PropMesh(TEXT("SM_WallSwitch"));
	if (SwitchMesh)
	{
		CreateBlock(
			FVector(30, -215, 123), FVector(100, 100, 100),
			nullptr, false, SwitchMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(
			FVector(30, -213.4f, 128), FVector(10, 2, 10),
			TexMat(TEXT("M_SwitchPlate"), SignWhiteMaterial), false);
	}
	// 신발장은 유광 흰 문 넷에 손가락 홈, 상판이 벽면까지 닿는 메시다. 원점
	// 바닥 중심, 문이 +Y(방 쪽).
	UStaticMesh* ShoeCabinetMesh = PropMesh(TEXT("SM_ShoeCabinet"));
	if (ShoeCabinetMesh)
	{
		CreateBlock(
			FVector(24, -198, 0), FVector(100, 100, 100),
			nullptr, true, ShoeCabinetMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(24, -198, 55), FVector(80, 32, 110), Gloss);
		for (const float ShelfZ : {28.0f, 82.0f})
		{
			CreateBlock(FVector(24, -181.6f, ShelfZ), FVector(76, 1.6f, 52), Gloss, false);
			CreateBlock(FVector(24, -180.6f, ShelfZ + 26), FVector(70, 1.2f, 1.6f), PlasticDarkMaterial, false);
		}
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(24, -198, 111.5f), FVector(84, 34, 3), Furniture, false);
	}

	// Baseboard trim along the interior walls.
	CreateBlock(FVector(0, 213, 5), FVector(378, 4, 10), Furniture, false);
	CreateBlock(FVector(-188, 0, 5), FVector(4, 428, 10), Furniture, false);
	CreateBlock(FVector(188, 0, 5), FVector(4, 428, 10), Furniture, false);
	CreateBlock(FVector(-58, -213, 5), FVector(262, 4, 10), Furniture, false);

	// 벗어 둔 실내화는 침대 옆에 둔다. 현관 단차 위에 물리 소품을 놓으면
	// 문을 지나던 발에 걸리고, 바닥과 겹친 채 생성되면 밖으로 튀어나간다.
	if (UStaticMesh* SlipperMesh = PropMesh(TEXT("SM_HouseSlipper")))
	{
		CreatePhysicsProp(SlipperMesh, nullptr, FVector::OneVector,
			FVector(-62, 80, 1), FRotator(0, 10, 0), 0.2f);
		CreatePhysicsProp(SlipperMesh, nullptr, FVector::OneVector,
			FVector(-42, 81, 1), FRotator(0, -8, 0), 0.2f);
	}

	ActiveParent = nullptr;
}

void AIGPrologueWorldScene::BuildCorridor()
{
	// 4F hallway of the villa: unit doors on the north side, windows to the
	// alley on the south, the elevator at the east end and stairs going down
	// into darkness at the west end. All coordinates are 4th-floor local.
	ActiveParent = UpperFloorRoot;

	// Surfaces are the ones every Korean walk-up landing actually has:
	// troweled stucco on the walls, 600 mm speckled granite tile underfoot,
	// a dark granite skirting band, and a painted stucco soffit overhead.
	UMaterialInterface* CorridorFloor = TexMat(TEXT("M_GraniteTile_XY"), ConcreteMaterial);
	// 철제 계단은 복도 화강석 타일과 다른 물건이다. §11 규칙 2가 발밑을
	// 선택으로 만드는데, 지금까지 이 계단은 소리만 금속이고 그림은 복도
	// 바닥과 같았다 — 들리는 거리가 다른 두 표면을 눈으로 구분할 수 없으면
	// 「어느 바닥을 고르느냐」는 선택이 아니라 우연이 된다.
	UMaterialInterface* StairSteel =
		TexMat(TEXT("M_MissingFloorSteelStair"), CorridorFloor);
	UMaterialInterface* CorridorCeil = TexMat(TEXT("M_StuccoCeil"), ConcreteMaterial);
	UMaterialInterface* CorridorWallX = TexMat(TEXT("M_Stucco_X"), ConcreteMaterial);
	UMaterialInterface* CorridorWallY = TexMat(TEXT("M_Stucco_Y"), ConcreteMaterial);
	UMaterialInterface* Skirting = TexMat(TEXT("M_UtilityGraniteCladding"), ConcreteDarkMaterial);
	UMaterialInterface* SteelDoor = TexMat(TEXT("M_SteelDoorUV"), DoorMaterial);
	UMaterialInterface* Stainless = TexMat(TEXT("M_StainlessUV"), MetalFrameMaterial);
	UMaterialInterface* Metal = TexMat(TEXT("M_MetalUV"), MetalFrameMaterial);
	// Thin cube UVs smear into horizontal bands on a tall jamb. The flat
	// powder-coated metal fallback reads like a Korean steel-door casing.
	UMaterialInterface* DoorTrim = PlasticDarkMaterial;
	// Directional stone/metal UVs smear across the 1–2 cm portal strips. A
	// matte charcoal powder coat is common on renovated Korean villa lifts and
	// stays visually stable on every thin reveal face.

	// Floor and ceiling. The hallway runs well past our door so that leaving
	// 404 means actually walking the building, not stepping into the lift.
	// Interior X -320..700, Y -375..-235, height 240.
	IGPrologueWorld::TagFootstepSurface(
		CreateBlock(FVector(190, -305, -10), FVector(1040, 160, 20), CorridorFloor),
		IGPrologueWorld::FootstepConcreteTag);
	CreateBlock(FVector(190, -305, 250), FVector(1040, 160, 20), CorridorCeil);

	// §11 V2 끌린 자국 (복도 러너). 계단에서 403호로 수레를 돌린 지점에만
	// 바퀴 자국을 남긴다. 복도 전체에 같은 큰 무늬를 반복하지 않는다.
	if (PlaneMesh)
	{
		UMaterialInterface* Runner =
			TexMat(TEXT("M_CorridorCasterScuff"), nullptr);
		const auto AddCorridorResidue = [this](
			const FVector& Center,
			const FVector& Size,
			UMaterialInterface* Material,
			const FRotator& Rotation)
		{
			if (!Material)
			{
				return;
			}
			if (UStaticMeshComponent* Plane = CreateBlock(
				Center,
				Size,
				Material,
				false,
				PlaneMesh,
				Rotation))
			{
				// V2 contract: 0.15 cm clear of the surface, no shadow, no
				// collision, out of the distance field.
				Plane->SetCastShadow(false);
				Plane->SetCanEverAffectNavigation(false);
				Plane->SetAffectDistanceFieldLighting(false);
				Plane->SetCullDistance(1600.0f);
			}
		};
		AddCorridorResidue(
			FVector(120.0f, -298.0f, 0.15f),
			FVector(75.0f, 75.0f, 1.0f),
			Runner,
			FRotator(0.0f, -35.0f, 0.0f));
	}

	// South wall with hopper windows onto the alley. The openings are cut out
	// of the masonry rather than drawn on it: a 20 cm slab with a glass plane
	// and an aluminium frame buried inside it left the frame standing half a
	// centimetre proud of solid concrete, which is the "blue rectangle stuck
	// on the wall" this dressing exists to avoid. Piers, sill course and head
	// course are separate blocks, so the reveal a player leans into is real
	// depth and the pane sits in the outer half of it like a real sash.
	{
		const float WindowXs[] = {-120.0f, 60.0f, 300.0f, 520.0f};
		constexpr float OpeningHalfWidth = 44.0f;
		constexpr float OpeningBottomZ = 114.0f;
		constexpr float OpeningTopZ = 186.0f;
		constexpr float WallWestX = -330.0f;
		constexpr float WallEastX = 584.0f;

		// Piers: masonry between the openings, plus the two end returns.
		const int32 WindowCount = static_cast<int32>(UE_ARRAY_COUNT(WindowXs));
		for (int32 PierIndex = 0; PierIndex < WindowCount + 1; ++PierIndex)
		{
			const float PierStartX = (PierIndex == 0)
				? WallWestX
				: WindowXs[PierIndex - 1] + OpeningHalfWidth;
			const float PierEndX = (PierIndex == WindowCount)
				? WallEastX
				: WindowXs[PierIndex] - OpeningHalfWidth;
			CreateBlock(
				FVector((PierStartX + PierEndX) * 0.5f, -385, 120),
				FVector(PierEndX - PierStartX, 20, 240),
				CorridorWallX);
		}

		for (const float WindowX : WindowXs)
		{
			// Sill course under the opening and head course over it.
			CreateBlock(
				FVector(WindowX, -385, OpeningBottomZ * 0.5f),
				FVector(OpeningHalfWidth * 2.0f, 20, OpeningBottomZ),
				CorridorWallX);
			CreateBlock(
				FVector(WindowX, -385, (OpeningTopZ + 240.0f) * 0.5f),
				FVector(OpeningHalfWidth * 2.0f, 20, 240.0f - OpeningTopZ),
				CorridorWallX);
			// The sash sits in the outer half of the reveal, lapping 1 cm into
			// the masonry on every edge so it is held by the opening. It keeps
			// collision: these windows never open, and the envelope has to
			// stay sealed now that the opening is a real hole in the wall.
			// 창밖은 침실 창과 같은 건너편 빌라의 밤 풍경이다. 까만 판을 두면
			// 복도 창이 벽에 붙인 남색 스티커처럼 읽혔다.
			NightViewSurfaces.Add(CreateBlock(
				FVector(WindowX, -390, 150),
				FVector(90, 3, 74),
				TexMat(TEXT("M_ApartmentNightGlass"), WindowDarkMaterial)));
			// Aluminium trim ring on the corridor face of the reveal.
			CreateBlock(FVector(WindowX, -377.5f, 184), FVector(88, 6, 5), PlasticDarkMaterial, false);
			CreateBlock(FVector(WindowX, -377.5f, 116), FVector(88, 6, 5), PlasticDarkMaterial, false);
			CreateBlock(FVector(WindowX - 41, -377.5f, 150), FVector(5, 6, 66), PlasticDarkMaterial, false);
			CreateBlock(FVector(WindowX + 41, -377.5f, 150), FVector(5, 6, 66), PlasticDarkMaterial, false);
			CreateBlock(FVector(WindowX, -377.5f, 150), FVector(78, 5, 3.5f), PlasticDarkMaterial, false);
			CreateBlock(FVector(WindowX, -379, 111), FVector(94, 10, 5), Skirting, false);
		}
	}

	// East end: elevator door opening (Y -360..-250); west end: dark stairwell.
	CreateBlock(FVector(710, -242.5f, 120), FVector(20, 15, 240), CorridorWallY);
	CreateBlock(FVector(710, -407.5f, 120), FVector(20, 95, 240), CorridorWallY);
	CreateBlock(FVector(710, -305, 225), FVector(20, 110, 30), CorridorWallY);
	// 문 옆으로 비켜 서서 복도를 볼 수 있는 작은 대기 공간.
	CreateBlock(FVector(647, -425, -10), FVector(146, 80, 20), CorridorFloor);
	CreateBlock(FVector(647, -425, 250), FVector(146, 80, 20), CorridorCeil);
	CreateBlock(FVector(574, -430, 120), FVector(20, 70, 240), CorridorWallY);
	CreateBlock(FVector(647, -465, 120), FVector(146, 20, 240), CorridorWallX);
	CreateBlock(FVector(642, -453.25f, 6), FVector(116, 3.5f, 12), Skirting, false);
	CreateBlock(FVector(585.75f, -415, 6), FVector(3.5f, 76, 12), Skirting, false);
	CreateBlock(FVector(698.25f, -407.5f, 6), FVector(3.5f, 95, 12), Skirting, false);
	// Open stair throat. The former full-height wall made the five visible
	// treads a dead-end decoration while route tests claimed a stair choice.
	CreateBlock(FVector(-330, -237.5f, 120), FVector(20, 15, 240), PlasticDarkMaterial);
	CreateBlock(FVector(-330, -372.5f, 120), FVector(20, 15, 240), PlasticDarkMaterial);
	CreateBlock(FVector(-330, -305, 225), FVector(20, 120, 30), PlasticDarkMaterial);

	// 집 구간 밖으로 이어지는 북쪽 벽과, 집 벽(230 cm) 위를 복도 높이(240 cm)까지
	// 채우는 띠. 동쪽은 옛 문 자리를 벽 마개로 메워 한 장의 벽으로 읽히고, 서쪽은
	// 4층에서 5층으로 오르는 계단 입구를 비워 둔다.
	CreateBlock(FVector(-332.5f, -225, 120), FVector(15, 20, 240), CorridorWallX);
	CreateBlock(FVector(-225.0f, -225, 120), FVector(10, 20, 240), CorridorWallX);
	CreateBlock(FVector(-277.5f, -225, 225), FVector(95, 20, 30), CorridorWallX);
	CreateBlock(FVector(302.5f, -225, 120), FVector(165, 20, 240), CorridorWallX);
	CreateBlock(FVector(582.5f, -225, 120), FVector(215, 20, 240), CorridorWallX);
	CreateBlock(FVector(430, -225, 225), FVector(90, 20, 30), CorridorWallX);
	CreateBlock(FVector(430, -225, 105), FVector(100, 20, 210), CorridorWallX);
	CreateBlock(FVector(0, -225, 235), FVector(440, 20, 10), CorridorWallX);

	// 허리 아래의 진한 페인트. 오래된 빌라 복도는 바닥에서 1 m까지 유성 페인트를
	// 따로 칠하고 그 위를 몰딩 한 줄로 끊는다. 걸레질 자국이 남는 자리이고,
	// 손전등이 벽을 훑을 때 위아래가 다른 반사로 읽힌다. 판은 벽면에서 4 mm.
	{
		UMaterialInterface* DadoX = TexMat(TEXT("M_StuccoDado_X"), Skirting);
		constexpr float DadoTop = 100.0f;
		constexpr float DadoThickness = 0.8f;
		// 남쪽 벽은 창이 114 cm부터라 허리 아래가 통째로 이어진다.
		CreateBlock(
			FVector(127, -375.0f + DadoThickness * 0.5f, DadoTop * 0.5f),
			FVector(914, DadoThickness, DadoTop), DadoX, false);
		CreateBlock(
			FVector(127, -375.0f + 1.4f, DadoTop + 1.5f),
			FVector(914, 2.8f, 3.0f), PlasticDarkMaterial, false);
		// 북쪽 벽은 세 문(401·402·403)의 문선 사이만. 구간을 쌍으로 적으면
		// 지오메트리 감사가 못 읽으니 람다로 하나씩 부른다.
		const auto AddNorthDado = [this, DadoX](const float X0, const float X1)
		{
			const float SpanCenter = (X0 + X1) * 0.5f;
			const float SpanWidth = X1 - X0;
			CreateBlock(
				FVector(SpanCenter, -235.0f - DadoThickness * 0.5f, DadoTop * 0.5f),
				FVector(SpanWidth, DadoThickness, DadoTop), DadoX, false);
			CreateBlock(
				FVector(SpanCenter, -235.0f - 1.4f, DadoTop + 1.5f),
				FVector(SpanWidth, 2.8f, 3.0f), PlasticDarkMaterial, false);
		};
		// 넓힌 대기 공간까지 같은 도장선과 몰딩을 이어 준다.
		CreateBlock(FVector(584.4f, -415, 50), FVector(0.8f, 80, 100), DadoX, false);
		CreateBlock(FVector(642, -454.6f, 50), FVector(116, 0.8f, 100), DadoX, false);
		CreateBlock(FVector(699.6f, -407.5f, 50), FVector(0.8f, 95, 100), DadoX, false);
		CreateBlock(FVector(585.4f, -415, 101.5f), FVector(2.8f, 80, 3), PlasticDarkMaterial, false);
		CreateBlock(FVector(642, -453.6f, 101.5f), FVector(116, 2.8f, 3), PlasticDarkMaterial, false);
		CreateBlock(FVector(698.6f, -407.5f, 101.5f), FVector(2.8f, 95, 3), PlasticDarkMaterial, false);
		AddNorthDado(-320.0f, -200.0f);
		AddNorthDado(-100.0f, -78.0f);
		AddNorthDado(18.0f, 68.0f);
		AddNorthDado(194.0f, 700.0f);

		// 천장 밑 전선관. 관리인이 나중에 단 인터폰과 등의 배선은 벽 속이 아니라
		// 벽 위를 지난다. 강관 하나가 북쪽 벽 꼭대기를 따라 달리고, 등 자리마다
		// 정션박스가 하나씩 붙는다.
		CreateBlock(
			FVector(190, -238.0f, 229), FVector(3, 3, 1040),
			Metal, false, CylinderMesh, FRotator(90, 0, 0));
		for (const float BoxX : {-180.0f, 60.0f, 300.0f, 540.0f})
		{
			CreateBlock(FVector(BoxX, -239.5f, 229), FVector(10, 7, 10), PlasticDarkMaterial, false);
			// 박스에서 등으로 올라가는 짧은 관.
			CreateBlock(
				FVector(BoxX, -272.0f, 238.0f), FVector(2.2f, 2.2f, 66),
				Metal, false, CylinderMesh, FRotator(0, 0, 90));
		}
	}

	// Neighbouring unit doors. The reference landing is a charcoal steel slab
	// with one brushed vertical band inset from the handle edge, small dark
	// squares punched down that band, a lever, a keypad lock and a peephole —
	// so that is exactly what gets built here, once per leaf.
	// 문짝은 브러시드 스테인리스가 아니라 무광 도장 강판이다. 밴드·인레이·
	// 레버·도어록·도어스코프는 이미 실제 기하이므로 표면만 바꾼다.
	UMaterialInterface* UnitDoorLeaf =
		TexMat(TEXT("M_UnitDoorPaintedSteel"), SteelDoor);
	// 문짝의 정식 경로는 Blender에서 만든 SM_UnitDoorLeaf다
	// (Scripts/blender/build_unit_door.py). 띠·인레이·도어스코프·힌지·스위프가
	// 한 메시에 실제 기하로 들어 있고, 원점은 바닥 중심, 앞면은 -Y다. 레버와
	// 도어락은 원점이 같은 SM_UnitDoorHardware다. 문짝 앞으로 7 cm 넘게 나오는
	// 철물을 문짝 바운드에서 떼어 놓아야 문에 붙인 종이(밤3 일지)가 감사에서
	// 문짝을 뚫지 않는다. 메시가 없을 때만 예전 상자 조립으로 내려간다.
	UStaticMesh* UnitDoorLeafMesh = PropMesh(TEXT("SM_UnitDoorLeaf"));
	UStaticMesh* UnitDoorHardwareMesh = PropMesh(TEXT("SM_UnitDoorHardware"));
	UStaticMesh* UnitDoorFrameMesh = PropMesh(TEXT("SM_UnitDoorFrame"));
	// 두 세대의 카메라를 문 사이 좁은 벽에 몰지 않고 각 문 바깥쪽으로 나눈다.
	auto DressUnitDoor = [this, UnitDoorLeaf, UnitDoorLeafMesh, UnitDoorHardwareMesh,
			Stainless, Metal](
		const float DoorX, const float FaceY, const float IntercomSide)
	{
		if (UnitDoorLeafMesh)
		{
			// 크기 100은 배율 1이다. 상자가 아니라 실제 치수의 메시다.
			CreateBlock(
				FVector(DoorX, FaceY, 0), FVector(100, 100, 100),
				nullptr, true, UnitDoorLeafMesh, FRotator::ZeroRotator);
			// 레버·도어락은 같은 자리에 놓는 충돌 없는 메시다. 레버는 손이 닿는
			// 프롬프트용이라 충돌이 없어도 된다.
			if (UnitDoorHardwareMesh)
			{
				CreateBlock(
					FVector(DoorX, FaceY, 0), FVector(100, 100, 100),
					nullptr, false, UnitDoorHardwareMesh, FRotator::ZeroRotator);
			}
			else
			{
				// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
				CreateBlock(FVector(DoorX + 32, FaceY - 3.9f, 95), FVector(4, 2, 12), Metal, false);
				CreateBlock(FVector(DoorX + 32, FaceY - 6.4f, 95), FVector(3, 9, 3), Metal, false);
			}
		}
		else
		{
			// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
			CreateBlock(FVector(DoorX, FaceY, 100), FVector(84, 5, 200), UnitDoorLeaf);
			const float PlateY = FaceY - 2.9f;
			// Brushed band down the leaf, with the punched square inlays.
			CreateBlock(FVector(DoorX + 14, PlateY, 100), FVector(13, 0.8f, 188), Stainless, false);
			for (const float InlayZ : {36.0f, 68.0f, 100.0f, 132.0f, 164.0f})
			{
				// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
				CreateBlock(
					FVector(DoorX + 14, PlateY - 0.6f, InlayZ), FVector(5, 0.6f, 5),
					PlasticDarkMaterial, false);
			}
			// Lever handle on a rose, digital lock above it, peephole at eye level.
			// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
			CreateBlock(FVector(DoorX + 32, PlateY - 1.0f, 95), FVector(4, 2, 12), Metal, false);
			CreateBlock(FVector(DoorX + 32, PlateY - 3.5f, 95), FVector(3, 9, 3), Metal, false);
			CreatePrintedBlock(
				FVector(DoorX + 32, PlateY - 1.4f, 122), FVector(9, 3.2f, 24),
				PlasticDarkMaterial,
				TexMat(TEXT("M_DoorLock"), PlasticDarkMaterial),
				FVector(0, -1, 0));
			// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
			CreateBlock(
				FVector(DoorX, PlateY - 0.4f, 155), FVector(3, 1.2f, 3),
				Metal, false, CylinderMesh, FRotator(90, 0, 0));
		}
		// 실외 카메라 한 대에 호출 버튼이 달린다. 상자 옆면에 실내 모니터를 반복하지 않는다.
		CreateProp(TEXT("SM_EntranceCamera"),
			FVector(DoorX + IntercomSide * 56.0f, -235, 130), nullptr, 0, 1, false);
	};

	// West to east the landing reads 401, 402, 403. Keeping the ordinary
	// sequence matters: 403 is Yudam's home and the wall past it stays bare,
	// so no door number turns into a horror sign.
	const float NeighborDoorXs[] = {-30.0f, -150.0f};
	const TCHAR* NeighborPlates[] = {TEXT("M_Plate402"), TEXT("M_Plate401")};
	for (int32 NeighborIndex = 0; NeighborIndex < 2; ++NeighborIndex)
	{
		const float DoorX = NeighborDoorXs[NeighborIndex];
		DressUnitDoor(DoorX, -234.5f, NeighborIndex == 0 ? 1.0f : -1.0f);
		if (UnitDoorFrameMesh)
		{
			// 문선·머리·스톱 립이 한 메시다. 원점은 개구부 바닥 중심.
			CreateBlock(
				FVector(DoorX, -233, 0), FVector(100, 100, 100),
				nullptr, false, UnitDoorFrameMesh, FRotator::ZeroRotator);
		}
		else
		{
			// Powder-coated casing stays readable at this thin aspect ratio; the
			// former stucco UV stretched into conspicuous horizontal stripes.
			// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
			CreateBlock(FVector(DoorX - 46, -233, 102), FVector(8, 7, 208), DoorTrim, false);
			CreateBlock(FVector(DoorX + 46, -233, 102), FVector(8, 7, 208), DoorTrim, false);
			CreateBlock(FVector(DoorX, -233, 204), FVector(100, 7, 8), DoorTrim, false);
		}
		// Unit number, on the landing face of the wall above the head trim.
		// At Y -231.5 the plate was two centimetres deep inside the wall.
		CreateBlock(
			FVector(DoorX, -236, 214), FVector(16, 2, 8),
			TexMat(NeighborPlates[NeighborIndex], FridgeInteriorMaterial), false);
	}

	// 현관문에 붙은 것들. 사는 사람이 붙인 것보다 남이 붙이고 간 것이 많다.
	// 봄에 비어 버린 402호는 전단지를 떼는 사람이 없어 문에 그대로 남아 있다.
	// 문마다 스티커·전단을 한 메시(텍스처 한 장)로 묶어 그리기 호출이 하나다
	// (Scripts/blender/build_door_prints.py). 원점은 문짝 앞면(Y -237.35) 가운데 바닥.
	if (UStaticMeshComponent* Prints = CreateProp(
		TEXT("SM_DoorPrints401"), FVector(-150.0f, -237.35f, 0.0f), nullptr, 0, 1, false))
	{
		Prints->SetCastShadow(false);
		Prints->bAffectDistanceFieldLighting = false;
		Prints->SetCullDistance(900.0f);
	}
	if (UStaticMeshComponent* Prints = CreateProp(
		TEXT("SM_DoorPrints402"), FVector(-30.0f, -237.35f, 0.0f), nullptr, 0, 1, false))
	{
		Prints->SetCastShadow(false);
		Prints->bAffectDistanceFieldLighting = false;
		Prints->SetCullDistance(900.0f);
	}

	// 401호 문 아래로 새는 불빛(§11 V1 ③). 문짝 앞면은 Y -237.35이고 바닥까지
	// 내려온다. 그 끝에 0.9 cm 빛줄을 여덟 칸으로 나눠 붙인다 — 칸을 하나씩
	// 끌 수 있어야 안쪽에 선 발이 줄을 끊는 것처럼 보인다. 바닥으로 번지는 빛은
	// 문틈 높이의 면광원이 맡는다. 그림자를 드리우므로 문 앞을 지나는 몸이
	// 번진 빛을 실제로 가린다. 낮에는 둘 다 꺼 둔다.
	Unit401GapSegments.Reset();
	for (int32 Segment = 0; Segment < 8; ++Segment)
	{
		UStaticMeshComponent* GapSegment = CreateBlock(
			FVector(-185.0f + Segment * 10.0f, -237.45f, 0.6f), FVector(10.0f, 0.1f, 0.9f),
			StreetLampGlowMaterial, false);
		GapSegment->SetCastShadow(false);
		GapSegment->SetVisibility(false);
		Unit401GapSegments.Add(GapSegment);
	}
	Unit401GapLight = NewObject<URectLightComponent>(this, TEXT("Unit401GapLight"));
	Unit401GapLight->SetupAttachment(ActiveParent ? ActiveParent.Get() : SceneRoot.Get());
	Unit401GapLight->SetMobility(EComponentMobility::Movable);
	Unit401GapLight->SetRelativeLocation(FVector(-150.0f, -237.8f, 1.5f));
	// 면광원은 +X를 비춘다. 복도 쪽(-Y)으로 돌리고 40도 숙인다. 바닥과 나란히
	// 비추면 빛이 타일에 스치기만 해서 번진 자리가 안 생기고 줄만 LED처럼 남았다.
	// 실제 문틈 빛은 방 바닥과 천장에서 되튄 빛이 틈을 비스듬히 빠져나와 문 앞
	// 한 뼘에 부채꼴로 고인다.
	Unit401GapLight->SetRelativeRotation(FRotator(-40.0f, -90.0f, 0.0f));
	Unit401GapLight->SetSourceWidth(80.0f);
	Unit401GapLight->SetSourceHeight(1.0f);
	Unit401GapLight->SetAttenuationRadius(170.0f);
	Unit401GapLight->SetMaxDrawDistance(1600.0f);
	// 전구색. 복도에 남은 형광등(청백)과 온도가 달라야 문 아래가 따로 읽힌다.
	Unit401GapLight->SetLightColor(FLinearColor(1.0f, 0.70f, 0.40f));
	Unit401GapLight->SetCastShadows(true);
	Unit401GapLight->SetVolumetricScatteringIntensity(0.0f);
	Unit401GapLight->SetIntensity(0.0f);
	Unit401GapLight->RegisterComponent();
	ZoneLights[static_cast<int32>(EIGLightZone::FourthFloor)].Add(Unit401GapLight);
	// 단위 없는 값 625가 1 cd다. 160은 0.26 cd로, 밤에 남는 서쪽 등(0.52 cd)의
	// 절반이다. 그래야 등 아래에서도 문 앞 한 뼘만 따뜻하게 읽힌다.
	Unit401GapLightIntensity = 160.0f;

	// Our 403 door casing and plate around the real swing door; the leaf
	// itself is the AIGSwingDoor actor, which dresses its own face.
	if (UStaticMesh* WideFrame = PropMesh(TEXT("SM_UnitDoorFrameWide")))
	{
		CreateBlock(
			FVector(131, -233, 0), FVector(100, 100, 100),
			nullptr, false, WideFrame, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(73, -233, 102), FVector(8, 7, 208), DoorTrim, false);
		CreateBlock(FVector(190, -233, 102), FVector(8, 7, 208), DoorTrim, false);
		CreateBlock(FVector(131, -233, 206), FVector(124, 7, 8), DoorTrim, false);
	}
	// The actual 404 opening also needs its three inside returns capped. The
	// south-wall material is authored for the broad wall face and streaks when
	// seen edge-on through this 20 cm reveal.
	// Offset these caps a few millimetres into the opening. Making their outer
	// faces exactly coplanar with the wall return caused a striped z-fighting
	// pattern in the corridor capture.
	CreateBlock(FVector(76.7f, -225, 105), FVector(1.4f, 22, 210), DoorTrim, false);
	CreateBlock(FVector(184.4f, -225, 105), FVector(2.8f, 22, 210), DoorTrim, false);
	CreateBlock(FVector(131, -225, 209.7f), FVector(110, 22, 0.4f), DoorTrim, false);
	// Both read from the landing, so both have to clear the wall face at
	// Y -235; at Y -233.5 the plate and the intercom were inside the wall.
	CreateBlock(
		FVector(131, -236, 214), FVector(16, 2, 8),
		TexMat(TEXT("M_Plate403"), FridgeInteriorMaterial), false);
	CreateProp(TEXT("SM_EntranceCamera"), FVector(205, -235, 130), nullptr, 0, 1, false);

	// Granite skirting, the way real landings finish the stucco to the tile.
	// It stands on the landing side of each wall: the north face is at
	// Y -235 and the south face at Y -375. Authored at -233.4 and -376.6 the
	// whole 3.5 cm course was inside the wall it was supposed to finish, with
	// a millimetre and a half showing.
	// 북쪽 굽도리는 벽이 있는 구간에만 붙는다. X -325..255 한 줄로 깔던
	// 예전 배치는 계단 개구부(X -325..-230)를 95 cm, 404호 현관(X 98..186)을
	// 88 cm 가로질러 문지방 위로 3.5 cm 턱이 지나갔고, 열리는 문짝이 그것을
	// 쓸고 나갔다. X 220..385 줄과도 35 cm 겹쳐 있었다.
	CreateBlock(FVector(-66, -236.75f, 6), FVector(328, 3.5f, 12), Skirting, false);
	CreateBlock(FVector(285.5f, -236.75f, 6), FVector(199, 3.5f, 12), Skirting, false);
	CreateBlock(FVector(-35, -373.25f, 6), FVector(580, 3.5f, 12), Skirting, false);
	CreateBlock(FVector(582.5f, -236.75f, 6), FVector(215, 3.5f, 12), Skirting, false);
	CreateBlock(FVector(430, -236.75f, 6), FVector(100, 3.5f, 12), Skirting, false);
	CreateBlock(FVector(398, -373.25f, 6), FVector(356, 3.5f, 12), Skirting, false);

	// 401·402 사이 22cm 벽에는 34cm 함이 들어가지 않는다. 402·403 사이 50cm 벽으로 옮긴다.
	// 복도 쪽 표면은 Y=-235다. 외함만 매입하고 문짝은 벽에서 8mm 내민다.
	CreateProp(TEXT("SM_CorridorCircuitCabinet"), FVector(44,-232.4f,180), nullptr, 0, 1, false);
	if (UStaticMeshComponent* Print = CreateProp(TEXT("SM_CorridorCircuitPrint"), FVector(44,-232.4f,180), nullptr, 0, 1, false))
	{
		Print->SetCastShadow(false);
		Print->bAffectDistanceFieldLighting = false;
		Print->SetCullDistance(900.f);
	}

	// 동쪽 끝 설비 벽장. §5.1의 세 번째 험이 여기서 난다 — 복도의 엄폐가
	// 서쪽 분전반 하나뿐이라 동쪽 절반이 통째로 비어 있었다. 문도 창도 없는
	// X 475~690 구간이라 402호 문선과도 엘리베이터 문틀과도 안 겹친다.
	//
	// 방이 아니라 벽장인 것은 §6에 4층 보일러실이라는 공간이 없기 때문이다.
	// 배관 샤프트 설명에 이름이 한 번 나올 뿐이고, 없는 방을 새로 지어
	// 붙이면 §1이 지키는 동선 축이 흔들린다.
	// 값은 이 파일의 이름공간에 있다. 기하 감사가 호출부의 리터럴과 이
	// 파일의 상수만 풀기 때문이다 — 헤더의 클래스 상수로 두었더니 세 상자가
	// 감사 밖으로 빠져 사각지대가 56에서 59로 늘었고 검증기가 잡았다.
	const float CupboardX = IGPrologueWorld::BoilerCupboardX;
	const float CupboardZ = IGPrologueWorld::BoilerCupboardZ;
	CreateBlock(
		FVector(CupboardX, -234.0f, CupboardZ),
		FVector(62, 12, 92), Metal, false);
	CreateBlock(
		FVector(CupboardX, -239.4f, CupboardZ),
		FVector(60, 1.2f, 90), Metal, false);
	// 손잡이는 문짝 오른쪽. 분전반과 같은 높이에 두어 복도의 금속이 한 줄로
	// 읽힌다.
	CreateBlock(
		FVector(CupboardX + 24.0f, -240.4f, 180),
		FVector(3, 1.5f, 6), PlasticDarkMaterial, false);
	// 소형 호스함처럼 보이던 상자를 국내 25×65cm 발신기 세트로 바꿨다.
	// 버튼 중심 127cm, 상단 표시등 162cm. 남쪽 벽에 등을 붙이고 복도를 향한다.
	UStaticMesh* FireAlarmMesh = PropMesh(TEXT("SM_FireAlarmPanel"));
	if (FireAlarmMesh)
	{
		CreateBlock(
			FVector(236, -370.25f, 108), FVector(100, 100, 100),
			nullptr, false, FireAlarmMesh, FRotator(0, 180, 0));
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(FVector(236, -370.25f, 140.5f), FVector(25, 9.5f, 65), Metal, false);
	}
	// The extinguisher is the one corridor prop authored to fall (밤1 beat
	// 1-5). A physics body from birth, but kinematic until the scripted drop:
	// visually identical to the old static block and free at rest.
	// 소화기도 같은 빌더의 메시다. 본체·헤드 두 볼록 껍데기를 충돌로 들고
	// 오므로 떨어질 때 구르는 것도 실제 형상대로다.
	UStaticMesh* FireExtinguisherMesh = PropMesh(TEXT("SM_FireExtinguisher"));
	if (FireExtinguisherMesh)
	{
		CorridorExtinguisher = CreatePhysicsProp(
			FireExtinguisherMesh,
			nullptr,
			FVector(1.0f, 1.0f, 1.0f),
			FVector(232, -364, 0.5f),
			FRotator(0, 180, 0),
			5.2f);
		if (CorridorExtinguisher)
		{
			CorridorExtinguisher->SetSimulatePhysics(false);
		}
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CorridorExtinguisher = CreatePhysicsProp(
			CylinderMesh,
			SnackRedMaterial,
			FVector(0.15f, 0.15f, 0.48f),
			FVector(232, -364, 26),
			FRotator::ZeroRotator,
			6.0f);
	}
	if (CorridorExtinguisher && !FireExtinguisherMesh)
	{
		CorridorExtinguisher->SetSimulatePhysics(false);
		// Valve stub rides the body so the silhouette survives the fall.
		UStaticMeshComponent* Valve = NewObject<UStaticMeshComponent>(
			this, TEXT("CorridorExtinguisherValve"));
		Valve->SetupAttachment(CorridorExtinguisher);
		Valve->SetStaticMesh(CylinderMesh);
		Valve->SetMaterial(0, PlasticDarkMaterial);
		// Child scale compounds with the parent's (0.15, 0.15, 0.48).
		Valve->SetRelativeScale3D(FVector(0.33f, 0.33f, 0.17f));
		Valve->SetRelativeLocation(FVector(0.0f, 0.0f, 54.0f));
		Valve->SetMobility(EComponentMobility::Movable);
		Valve->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Valve->SetGenerateOverlapEvents(false);
		Valve->SetCanEverAffectNavigation(false);
		Valve->RegisterComponent();
		GeometryComponents.Add(Valve);
	}

	// Permanent first flight toward 5F. It turns north from the west landing,
	// so it never overlaps the parallel flight descending toward 3F. The
	// memory cut happens on the first tread, but the visible architecture is
	// always here and therefore cannot pop in when the story state changes.
	for (int32 UpperStepIndex = 0; UpperStepIndex < 4; ++UpperStepIndex)
	{
		const float StepY = -216.0f + UpperStepIndex * 22.0f;
		const float StepTop = 18.0f + UpperStepIndex * 18.0f;
		IGPrologueWorld::TagFootstepSurface(CreateBlock(
			FVector(-277.5f, StepY, StepTop * 0.5f),
			FVector(85, 22, StepTop),
			SteelDoor), IGPrologueWorld::FootstepMetalStairTag);
	}
	// 계단은 바닥에서 자란 한 덩어리라 마지막 단의 북쪽 면이 통째로 드러난다.
	// 체커플레이트는 XY로 읽으므로 그 세로 면에서는 무늬가 전혀 변하지 않는다
	// — 85 x 54 cm가 한 줄로 늘어난 민무늬였다. 실제 철제 계단에서 그 자리는
	// 디딤판이 아니라 도장 강판 마구리이므로 UV 재질로 덮는다.
	CreateBlock(
		FVector(-277.5f, -138.7f, 27.0f),
		FVector(85, 0.6f, 54),
		TexMat(TEXT("M_SteelDoorUV"), MetalFrameMaterial),
		false);
	// Threshold slab flush with the fourth tread. The former north closure and
	// hidden portal are gone: BuildFifthFloorAnnex continues this exact shaft
	// with fourteen physical treads to the roof.
	IGPrologueWorld::TagFootstepSurface(CreateBlock(
		FVector(-277.5f, -125.0f, 63.0f),
		FVector(85, 28, 18),
		StairSteel), IGPrologueWorld::FootstepMetalStairTag);
	CreateBlock(
		FVector(-332.5f, -160.0f, 120),
		FVector(15, 110, 240),
		ConcreteDarkMaterial);
	CreateBlock(
		FVector(-222.5f, -160.0f, 120),
		FVector(15, 110, 240),
		ConcreteDarkMaterial);
	CreateBlock(
		FVector(-277.5f, -165.0f, 250),
		FVector(125, 120, 20),
		CorridorCeil);

	// 서쪽 끝 목 너머는 계단탑이다(BuildStairCore). 1층까지 실제 계단이 이어지고,
	// 4층에서는 동쪽 띠가 북쪽으로 내려가며 서쪽 띠 위는 난간 너머로 뚫려 있다.
	// A tired green exit lamp glows at the stair throat, screwed to the head
	// of the stair opening rather than hanging a centimetre clear of it.
	CreateBlock(FVector(-313, -305, 220), FVector(14, 8, 10),
		ScreenGlowMaterial, false);
	// 비상구 등은 실제 빛이다. 밤에 복도 등이 죽으면 계단 입구를 가리키는
	// 유일한 표지가 되고, 그 초록이 서쪽 벽에 남는다.
	UPointLightComponent* ExitLamp = CreateLight(
		FVector(-313, -305, 213), 22.0f, 340.0f,
		FLinearColor(0.30f, 1.0f, 0.42f), false, 5.0f);
	ExitLamp->SetVolumetricScatteringIntensity(0.4f);

	// Ceiling fixtures down the whole hallway: flush round downlights, the way
	// the reference landing is lit. The far one has a dying ballast and never
	// stops shimmering.
	// 등은 Blender 메시 둘(Scripts/blender/build_ceiling_light.py)이다. 테와
	// 확산 돔이 따로라, 죽어 가는 등이 돔의 재질을 바꾸는 예전 방식이 그대로
	// 통한다. 둘 다 원점이 천장 접촉면이라 천장 아랫면 Z 240에 놓는다.
	UStaticMesh* CeilingLightRingMesh = PropMesh(TEXT("SM_CeilingLightRing"));
	UStaticMesh* CeilingLightDomeMesh = PropMesh(TEXT("SM_CeilingLightDome"));
	for (const float FixtureX : {-180.0f, 60.0f, 300.0f, 540.0f})
	{
		if (CeilingLightRingMesh && CeilingLightDomeMesh)
		{
			CreateBlock(
				FVector(FixtureX, -305, 240), FVector(100, 100, 100),
				nullptr, false, CeilingLightRingMesh, FRotator::ZeroRotator);
			CorridorLightDiscs.Add(CreateBlock(
				FVector(FixtureX, -305, 240), FVector(100, 100, 100),
				LightPanelMaterial, false, CeilingLightDomeMesh, FRotator::ZeroRotator));
		}
		else
		{
			// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
			CreateBlock(
				FVector(FixtureX, -305, 237), FVector(26, 26, 4),
				Stainless, false, CylinderMesh);
			// Keep the emissive disc: putting a fixture out means darkening this
			// too, or a lit ring hangs on a black ceiling.
			CorridorLightDiscs.Add(CreateBlock(
				FVector(FixtureX, -305, 234.5f), FVector(21, 21, 2),
				LightPanelMaterial, false, CylinderMesh));
		}
		UPointLightComponent* CorridorLight = CreateLight(
			FVector(FixtureX, -305, 226), 1020.0f, 410.0f,
			FLinearColor(0.86f, 0.97f, 1.0f), true, 16.0f);
		CorridorLight->SetVolumetricScatteringIntensity(0.10f);
		CorridorLights.Add(CorridorLight);
		CorridorSensorLastSeen.Add(-100.0);
		// 실제로 교체한 전구처럼 등마다 색이 조금 다르다.
		if (FixtureX == 300.0f)
		{
			CorridorLight->SetLightColor(FLinearColor(1.0f, 0.91f, 0.75f));
		}
		if (FixtureX < 0.0f)
		{
			DegradedCorridorLight = CorridorLight;
			DegradedLightBaseIntensity = CorridorLight->Intensity;
		}
	}

	ActiveParent = nullptr;
}

float AIGPrologueWorldScene::GetStoreyFloorZ(const int32 FloorIndex)
{
	return IGPrologueWorld::StairStoreyHeight * FMath::Clamp(FloorIndex, 0, 3);
}

FVector AIGPrologueWorldScene::GetStairDoorwayFeet(const int32 FloorIndex)
{
	// 1층은 주차장 쪽 출입구(Y -350..-235) 밖, 위층은 복도 목 기둥 안쪽이다.
	return FloorIndex <= 0
		? FVector(IGPrologueWorld::StairCoreEastX + 20.0f, -292.0f, 0.0f)
		: FVector(IGPrologueWorld::StairCoreEastX + 35.0f, -305.0f, GetStoreyFloorZ(FloorIndex));
}

bool AIGPrologueWorldScene::IsInsideStairCore(const FVector& Location)
{
	return Location.X > IGPrologueWorld::StairInnerWestX
		&& Location.X < IGPrologueWorld::StairInnerEastX
		&& Location.Y > IGPrologueWorld::StairInnerSouthY
		&& Location.Y < IGPrologueWorld::StairInnerNorthY;
}

bool AIGPrologueWorldScene::IsOnStairFlight(const FVector& Location)
{
	return Location.X > IGPrologueWorld::StairInnerWestX
		&& Location.X < IGPrologueWorld::StairInnerEastX
		&& Location.Y > IGPrologueWorld::StairFlightSouthY
		&& Location.Y < IGPrologueWorld::StairFlightNorthY;
}

void AIGPrologueWorldScene::GetStairClimbFeet(const int32 FromFloor, TArray<FVector>& OutFeet)
{
	OutFeet.Reset();
	const int32 Floor = FMath::Clamp(FromFloor, 0, 2);
	const float FloorZ = GetStoreyFloorZ(Floor);
	const float HalfZ = FloorZ + IGPrologueWorld::StairStoreyHeight * 0.5f;
	const float UpperZ = FloorZ + IGPrologueWorld::StairStoreyHeight;
	const float HalfRise = IGPrologueWorld::StairRise * 0.5f;
	const float WestX =
		(IGPrologueWorld::StairInnerWestX + IGPrologueWorld::StairWestBandEastX) * 0.5f;
	const float EastX =
		(IGPrologueWorld::StairEastBandWestX + IGPrologueWorld::StairInnerEastX) * 0.5f;
	const float SouthY = IGPrologueWorld::StairFlightSouthY;
	const float NorthY = IGPrologueWorld::StairFlightNorthY;
	// 반 층 참에서는 띠 폭의 반쯤 들어가서 꺾는다.
	const float TurnY = NorthY + 55.0f;
	OutFeet.Add(GetStairDoorwayFeet(Floor));
	OutFeet.Add(FVector(WestX, SouthY - 30.0f, FloorZ));
	OutFeet.Add(FVector(WestX, SouthY, FloorZ + HalfRise));
	OutFeet.Add(FVector(WestX, NorthY, HalfZ - HalfRise));
	OutFeet.Add(FVector(WestX, TurnY, HalfZ));
	OutFeet.Add(FVector(EastX, TurnY, HalfZ));
	OutFeet.Add(FVector(EastX, NorthY, HalfZ + HalfRise));
	OutFeet.Add(FVector(EastX, SouthY, UpperZ - HalfRise));
	OutFeet.Add(FVector(EastX, SouthY - 30.0f, UpperZ));
	OutFeet.Add(GetStairDoorwayFeet(Floor + 1));
}

void AIGPrologueWorldScene::BuildStairCore()
{
	// 서쪽 계단탑. 1층 필로티 옆에서 4층 복도 끝까지 실제로 걸어 오르내린다.
	// 전에는 1층 발치 다섯 단과 4층 목 네 단만 있고, 그 사이를 0.12초 암전으로
	// 건너뛰었다. 2·3층이 생기면서 그 사이가 진짜 계단이 됐다.
	//
	// 서쪽 띠는 층에서 북쪽으로 반 층 참까지 오르고, 동쪽 띠는 그 참에서 남쪽으로
	// 올라 다음 층에 닿는다. 두 띠 사이는 벽이라, 아래에서 오는 발소리는 들려도
	// 몸은 꺾이는 참에 가서야 보인다.
	//
	// 좌표는 월드 cm 그대로다. 기하 감사가 숫자를 읽을 수 있게 프레임을 바꾸지 않는다.
	ActiveParent = nullptr;

	constexpr float OuterWestX = IGPrologueWorld::StairCoreWestX;
	constexpr float OuterEastX = IGPrologueWorld::StairCoreEastX;
	constexpr float OuterSouthY = IGPrologueWorld::StairCoreSouthY;
	constexpr float OuterNorthY = IGPrologueWorld::StairCoreNorthY;
	constexpr float WestX = IGPrologueWorld::StairInnerWestX;
	constexpr float EastX = IGPrologueWorld::StairInnerEastX;
	constexpr float SouthY = IGPrologueWorld::StairInnerSouthY;
	constexpr float NorthY = IGPrologueWorld::StairInnerNorthY;
	constexpr float WestBandEastX = IGPrologueWorld::StairWestBandEastX;
	constexpr float EastBandWestX = IGPrologueWorld::StairEastBandWestX;
	constexpr float FlightSouthY = IGPrologueWorld::StairFlightSouthY;
	constexpr float FlightNorthY = IGPrologueWorld::StairFlightNorthY;
	constexpr float Storey = IGPrologueWorld::StairStoreyHeight;
	constexpr float Rise = IGPrologueWorld::StairRise;
	constexpr float Going = IGPrologueWorld::StairGoing;
	constexpr float TreadThickness = IGPrologueWorld::StairTreadThickness;
	// 4층 참의 천장 아랫면. 복도 천장과 같은 높이다.
	constexpr float CeilingZ = 1140.0f;
	// 벽돌 난간 윗면. 정면 벽돌과 같은 높이에서 끝난다.
	constexpr float ParapetZ = 1240.0f;
	const float CoreCenterX = (WestX + EastX) * 0.5f;
	const float CoreWidth = EastX - WestX;
	const float WestBandX = (WestX + WestBandEastX) * 0.5f;
	const float EastBandX = (EastBandWestX + EastX) * 0.5f;
	const float BandWidth = WestBandEastX - WestX;
	const float HalfLandingY = (FlightNorthY + NorthY) * 0.5f;
	const float HalfLandingDepth = NorthY - FlightNorthY;

	UMaterialInterface* LandingFloor = TexMat(TEXT("M_GraniteTile_XY"), ConcreteMaterial);
	UMaterialInterface* StairSteel = TexMat(TEXT("M_MissingFloorSteelStair"), LandingFloor);
	UMaterialInterface* StairMass = TexMat(TEXT("M_UtilityConcreteDark"), ConcreteDarkMaterial);
	UMaterialInterface* CoreWallX = TexMat(TEXT("M_Stucco_X"), ConcreteMaterial);
	UMaterialInterface* CoreWallY = TexMat(TEXT("M_Stucco_Y"), ConcreteMaterial);
	UMaterialInterface* CoreCeil = TexMat(TEXT("M_StuccoCeil"), ConcreteMaterial);
	UMaterialInterface* GranitePanelX = TexMat(TEXT("M_UtilityGraniteCladding"), ConcreteMaterial);
	UMaterialInterface* VillaBrickX = TexMat(TEXT("M_UtilityVillaBrick"), GranitePanelX);
	UMaterialInterface* BandConcrete = TexMat(TEXT("M_Concrete_X"), ConcreteMaterial);
	UMaterialInterface* RoofDeck = TexMat(
		TEXT("M_RooftopWaterproofing_XY"), TexMat(TEXT("M_Concrete_XY"), ConcreteMaterial));

	// --- 바깥벽 -----------------------------------------------------------
	// 바깥은 빌라 정면과 같은 붉은 벽돌이고, 안에는 복도와 같은 미장을 한 겹 댄다.
	// 남쪽 벽은 정면 벽돌과 같은 선(Y -395)에 서서 빌라가 서쪽으로 한 칸 늘어난 것으로 읽힌다.
	CreateBlock(
		FVector((OuterWestX + OuterEastX) * 0.5f, OuterSouthY + 5.0f, ParapetZ * 0.5f),
		FVector(OuterEastX - OuterWestX, 10, ParapetZ), VillaBrickX);
	CreateBlock(
		FVector(OuterWestX + 5.0f, (OuterSouthY + 10.0f + OuterNorthY) * 0.5f, ParapetZ * 0.5f),
		FVector(10, OuterNorthY - OuterSouthY - 10.0f, ParapetZ), VillaBrickX);
	// 북쪽 벽은 옥상 슬래브 밑에서 끝난다. 그 위는 옥상이 북쪽으로 이어진다.
	CreateBlock(
		FVector((OuterWestX + 10.0f + OuterEastX) * 0.5f, OuterNorthY - 5.0f, 590.0f),
		FVector(OuterEastX - OuterWestX - 10.0f, 10, 1180.0f), VillaBrickX);
	CreateBlock(
		FVector(-540.0f, OuterNorthY - 5.0f, 1210.0f), FVector(80, 10, 60), VillaBrickX);
	// 안쪽 미장. 남쪽과 북쪽은 띠 폭만큼, 서쪽은 남북 벽 사이를 채운다.
	CreateBlock(
		FVector(CoreCenterX, SouthY - 5.0f, CeilingZ * 0.5f),
		FVector(CoreWidth, 10, CeilingZ), CoreWallX);
	CreateBlock(
		FVector(WestX - 2.5f, (OuterSouthY + 10.0f + OuterNorthY - 10.0f) * 0.5f, CeilingZ * 0.5f),
		FVector(5, OuterNorthY - OuterSouthY - 20.0f, CeilingZ), CoreWallY);
	CreateBlock(
		FVector(CoreCenterX, NorthY + 2.5f, CeilingZ * 0.5f),
		FVector(CoreWidth, 5, CeilingZ), CoreWallX);
	// 동쪽 벽. 계단 옆은 꼭대기까지 막혀 있고, 남쪽 참 쪽은 층마다 복도로 열린다.
	CreateBlock(
		FVector((EastX + OuterEastX) * 0.5f, (FlightSouthY + OuterNorthY - 10.0f) * 0.5f, 580.0f),
		FVector(OuterEastX - EastX, OuterNorthY - 10.0f - FlightSouthY, 1160.0f), CoreWallY);
	// 1층은 필로티 주차장으로 나간다. 밤 셔터(Y -354..-350)보다 남쪽은 벽으로 둔다.
	CreateBlock(
		FVector((EastX + OuterEastX) * 0.5f, -362.5f, 140.0f),
		FVector(OuterEastX - EastX, 25, 280), CoreWallY);
	CreateBlock(
		FVector((EastX + OuterEastX) * 0.5f, -292.5f, 259.0f),
		FVector(OuterEastX - EastX, 115, 42), CoreWallY);
	// 2~4층 출입구 위. 복도 천장(층+240)에서 다음 층 참 밑까지다.
	for (const float FloorZ : {300.0f, 600.0f})
	{
		CreateBlock(
			FVector((EastX + OuterEastX) * 0.5f, (SouthY + FlightSouthY) * 0.5f, FloorZ + 260.0f),
			FVector(OuterEastX - EastX, FlightSouthY - SouthY, 40), CoreWallY);
	}
	CreateBlock(
		FVector((EastX + OuterEastX) * 0.5f, (SouthY + FlightSouthY) * 0.5f, 1150.0f),
		FVector(OuterEastX - EastX, FlightSouthY - SouthY, 20), CoreWallY);
	// 두 띠 사이의 벽.
	CreateBlock(
		FVector((WestBandEastX + EastBandWestX) * 0.5f, (FlightSouthY + FlightNorthY) * 0.5f, CeilingZ * 0.5f),
		FVector(EastBandWestX - WestBandEastX, FlightNorthY - FlightSouthY, CeilingZ), CoreWallY);
	// 4층 위 천장과 지붕. 옥상 슬래브(X -500..-340, Y -300..220)가 덮지 않는 자리만 채워
	// 옥상 바닥이 한 높이로 이어진다.
	CreateBlock(
		FVector(CoreCenterX, (SouthY + NorthY) * 0.5f, CeilingZ + 10.0f),
		FVector(CoreWidth, NorthY - SouthY, 20), CoreCeil);
	CreateBlock(FVector(-540.0f, -137.5f, 1190.0f), FVector(80, 495, 20), RoofDeck);
	CreateBlock(FVector(-420.0f, -342.5f, 1190.0f), FVector(160, 85, 20), RoofDeck);

	// 바깥 마감. 층 사이 콘크리트 띠와 난간 갓돌이 정면에서 그대로 이어진다.
	for (const float BandZ : {540.0f, 840.0f})
	{
		CreateBlock(
			FVector((OuterWestX + OuterEastX) * 0.5f, -396.5f, BandZ),
			FVector(OuterEastX - OuterWestX, 3.0f, 14.0f), BandConcrete, false);
	}
	CreateBlock(
		FVector((OuterWestX - 4.0f + OuterEastX - 4.0f) * 0.5f, -397, 1244),
		FVector(OuterEastX - OuterWestX, 12, 10), BandConcrete, false);
	// 계단참마다 창 하나. 정면의 창과 같은 메시다.
	if (UStaticMesh* StairWindowMesh = PropMesh(TEXT("SM_VillaWindow")))
	{
		for (const float WindowZ : {390.0f, 690.0f, 990.0f})
		{
			CreateBlock(FVector(-465.0f, -396, WindowZ), FVector(96, 4, 116), WindowDarkMaterial, false);
			CreateBlock(
				FVector(-465.0f, -395, WindowZ - 60), FVector(100, 100, 100),
				nullptr, false, StairWindowMesh, FRotator::ZeroRotator);
		}
	}

	// --- 층 참 ------------------------------------------------------------
	// 층의 남쪽 참은 복도와 같은 화강석이다. 철판은 계단부터라, 발소리가 바뀌는 자리가
	// 눈에도 보인다(§11 규칙 2). 1층 참은 주차장 바닥(X -340부터)과 맞닿고, 위층 참은
	// 복도 바닥(X -330부터)과 맞닿는다.
	IGPrologueWorld::TagFootstepSurface(CreateBlock(
		FVector((WestX + OuterEastX) * 0.5f, (SouthY + FlightSouthY) * 0.5f, -10.0f),
		FVector(OuterEastX - WestX, FlightSouthY - SouthY, 20), LandingFloor),
		IGPrologueWorld::FootstepConcreteTag);
	for (const float FloorZ : {300.0f, 600.0f, 900.0f})
	{
		IGPrologueWorld::TagFootstepSurface(CreateBlock(
			FVector((WestX - 330.0f) * 0.5f, (SouthY + FlightSouthY) * 0.5f, FloorZ - 10.0f),
			FVector(-330.0f - WestX, FlightSouthY - SouthY, 20), LandingFloor),
			IGPrologueWorld::FootstepConcreteTag);
	}

	// --- 1층에서 2층까지 ---------------------------------------------------
	// 맨 아래 두 단은 땅에서 자란 덩어리다. 디딤판 밑을 어두운 콘크리트로 채워
	// 밑이 빈 계단이 아니라는 것이 옆에서도 보인다.
	for (int32 Step = 1; Step < 9; ++Step)
	{
		const float Top = Rise * Step;
		const float CenterY = FlightSouthY + Going * (Step - 0.5f);
		const float TreadBottom = Top - TreadThickness < 0.0f ? 0.0f : Top - TreadThickness;
		IGPrologueWorld::TagFootstepSurface(CreateBlock(
			FVector(WestBandX, CenterY, (TreadBottom + Top) * 0.5f),
			FVector(BandWidth, Going, Top - TreadBottom), StairSteel),
			IGPrologueWorld::FootstepMetalStairTag);
		if (Step > 1)
		{
			CreateBlock(
				FVector(WestBandX, CenterY, TreadBottom * 0.5f),
				FVector(BandWidth, Going, TreadBottom), StairMass);
		}
	}
	CreateBlock(
		FVector(CoreCenterX, HalfLandingY, (Storey * 0.5f - 20.0f) * 0.5f),
		FVector(CoreWidth, HalfLandingDepth, Storey * 0.5f - 20.0f), StairMass);
	IGPrologueWorld::TagFootstepSurface(CreateBlock(
		FVector(CoreCenterX, HalfLandingY, Storey * 0.5f - 10.0f),
		FVector(CoreWidth, HalfLandingDepth, 20), StairSteel),
		IGPrologueWorld::FootstepMetalStairTag);
	for (int32 Step = 1; Step < 9; ++Step)
	{
		const float Top = Storey * 0.5f + Rise * Step;
		const float CenterY = FlightNorthY - Going * (Step - 0.5f);
		IGPrologueWorld::TagFootstepSurface(CreateBlock(
			FVector(EastBandX, CenterY, Top - TreadThickness * 0.5f),
			FVector(BandWidth, Going, TreadThickness), StairSteel),
			IGPrologueWorld::FootstepMetalStairTag);
		CreateBlock(
			FVector(EastBandX, CenterY, (Top - TreadThickness) * 0.5f),
			FVector(BandWidth, Going, Top - TreadThickness), StairMass);
	}

	// --- 2층부터 4층까지 ---------------------------------------------------
	// 디딤판 상자의 밑면이 아랫단 윗면보다 8 cm 낮아 단과 단 사이가 비지 않는다.
	// 같은 띠의 바로 아래 계단과는 2.75 m가 떨어져 머리가 닿지 않는다.
	for (int32 Level = 1; Level < 3; ++Level)
	{
		const float FloorZ = Storey * Level;
		const float HalfZ = FloorZ + Storey * 0.5f;
		for (int32 Step = 1; Step < 9; ++Step)
		{
			IGPrologueWorld::TagFootstepSurface(CreateBlock(
				FVector(WestBandX, FlightSouthY + Going * (Step - 0.5f), FloorZ + Rise * Step - TreadThickness * 0.5f),
				FVector(BandWidth, Going, TreadThickness), StairSteel),
				IGPrologueWorld::FootstepMetalStairTag);
			IGPrologueWorld::TagFootstepSurface(CreateBlock(
				FVector(EastBandX, FlightNorthY - Going * (Step - 0.5f), HalfZ + Rise * Step - TreadThickness * 0.5f),
				FVector(BandWidth, Going, TreadThickness), StairSteel),
				IGPrologueWorld::FootstepMetalStairTag);
		}
		IGPrologueWorld::TagFootstepSurface(CreateBlock(
			FVector(CoreCenterX, HalfLandingY, HalfZ - 10.0f),
			FVector(CoreWidth, HalfLandingDepth, 20), StairSteel),
			IGPrologueWorld::FootstepMetalStairTag);
	}

	// 4층 참 북쪽 가장자리. 서쪽 띠 위로는 계단이 없어 3층까지 뚫려 있다. 한국
	// 빌라 계단의 검은 철제 난간이다. 살 사이 9 cm라 몸이 빠지지 않는다.
	CreateBlock(
		FVector(WestBandX, FlightSouthY - 2.5f, 995.0f),
		FVector(BandWidth, 4, 4), PlasticDarkMaterial);
	for (int32 Post = 0; Post < 9; ++Post)
	{
		CreateBlock(
			FVector(WestX + 6.0f + Post * 11.625f, FlightSouthY - 2.5f, 946.5f),
			FVector(2.5f, 2.5f, 93), PlasticDarkMaterial);
	}

	// --- 표지와 등 ----------------------------------------------------------
	// 층 참 서쪽 벽에 칠한 층수. 축광 도료라 밤에도 희미하게 남는다.
	const auto PaintFloorNumber = [this](const float FloorZ, const TCHAR* Label)
	{
		UTextRenderComponent* Number = NewObject<UTextRenderComponent>(
			this, *FString::Printf(TEXT("StairFloorNumber_%d"), BlockCounter++));
		Number->SetupAttachment(SceneRoot);
		Number->SetMobility(EComponentMobility::Static);
		Number->SetRelativeLocation(FVector(-574.6f, -305.0f, FloorZ + 150.0f));
		Number->SetHorizontalAlignment(EHTA_Center);
		Number->SetVerticalAlignment(EVRTA_TextCenter);
		Number->SetWorldSize(46.0f);
		Number->SetTextRenderColor(FColor(96, 128, 100));
		Number->SetText(FText::AsCultureInvariant(FString(Label)));
		Number->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Number->SetCastShadow(false);
		Number->RegisterComponent();
	};
	PaintFloorNumber(0.0f, TEXT("1F"));
	PaintFloorNumber(300.0f, TEXT("2F"));
	PaintFloorNumber(600.0f, TEXT("3F"));
	PaintFloorNumber(900.0f, TEXT("4F"));

	// 층 참마다 천장 등 하나. 그림자를 드리워 벽 너머 골목에 새지 않는다. 4층 참은
	// 켜지 않는다 — 복도 끝 죽어 가는 등 너머로 계단이 어둠 속에 내려가야 한다.
	UStaticMesh* RingMesh = PropMesh(TEXT("SM_CeilingLightRing"));
	UStaticMesh* DomeMesh = PropMesh(TEXT("SM_CeilingLightDome"));
	StairCoreLights.Reset();
	StairCoreLightDiscs.Reset();
	StairCoreLightIntensities.Reset();
	for (const float FloorZ : {0.0f, 300.0f, 600.0f})
	{
		const float CeilingUnderside = FloorZ + 280.0f;
		if (RingMesh && DomeMesh)
		{
			CreateBlock(
				FVector(-465.0f, -305.0f, CeilingUnderside), FVector(100, 100, 100),
				nullptr, false, RingMesh, FRotator::ZeroRotator);
			StairCoreLightDiscs.Add(CreateBlock(
				FVector(-465.0f, -305.0f, CeilingUnderside), FVector(100, 100, 100),
				LightPanelMaterial, false, DomeMesh, FRotator::ZeroRotator));
		}
		UPointLightComponent* LandingLight = CreateLight(
			FVector(-465.0f, -305.0f, CeilingUnderside - 14.0f), 760.0f, 470.0f,
			FLinearColor(0.92f, 0.95f, 1.0f), true, 14.0f);
		LandingLight->SetVolumetricScatteringIntensity(0.10f);
		StairCoreLights.Add(LandingLight);
		StairCoreLightIntensities.Add(LandingLight->Intensity);
	}

	ActiveParent = nullptr;
}

void AIGPrologueWorldScene::BuildLowerFloors()
{
	// 2층과 3층. 4층과 같은 폭의 복도가 계단탑에서 동쪽으로 뻗고, 북쪽에 세대 문이
	// 셋씩 선다. 들어갈 수 있는 집은 층마다 하나다 — 201호는 관리인이 석고보드를
	// 쌓아 둔 창고, 302호는 할머니가 장롱만 두고 나간 빈집이다. 나머지 문은 잠겨 있다.
	// 월드 좌표 그대로다.
	ActiveParent = nullptr;

	UMaterialInterface* CorridorFloor = TexMat(TEXT("M_GraniteTile_XY"), ConcreteMaterial);
	UMaterialInterface* CorridorCeil = TexMat(TEXT("M_StuccoCeil"), ConcreteMaterial);
	UMaterialInterface* CorridorWallX = TexMat(TEXT("M_Stucco_X"), ConcreteMaterial);
	UMaterialInterface* CorridorWallY = TexMat(TEXT("M_Stucco_Y"), ConcreteMaterial);
	UMaterialInterface* DadoX = TexMat(TEXT("M_StuccoDado_X"), ConcreteDarkMaterial);
	UMaterialInterface* Skirting = TexMat(TEXT("M_UtilityGraniteCladding"), ConcreteDarkMaterial);
	UStaticMesh* DoorLeafMesh = PropMesh(TEXT("SM_UnitDoorLeaf"));
	UStaticMesh* DoorHardwareMesh = PropMesh(TEXT("SM_UnitDoorHardware"));
	UStaticMesh* DoorFrameMesh = PropMesh(TEXT("SM_UnitDoorFrame"));
	UStaticMesh* RingMesh = PropMesh(TEXT("SM_CeilingLightRing"));
	UStaticMesh* DomeMesh = PropMesh(TEXT("SM_CeilingLightDome"));
	LowerFloorLights.Reset();
	LowerFloorLightDiscs.Reset();
	LowerFloorLightIntensities.Reset();

	// 문 위 표찰의 호수. 표찰 판은 흰 상자이고 숫자는 그 앞면에 붙인 글자다.
	const auto AddDoorNumber = [this](const float DoorX, const float FloorZ, const TCHAR* Number)
	{
		UTextRenderComponent* Label = NewObject<UTextRenderComponent>(
			this, *FString::Printf(TEXT("UnitNumber_%d"), BlockCounter++));
		Label->SetupAttachment(SceneRoot);
		Label->SetMobility(EComponentMobility::Static);
		Label->SetRelativeLocation(FVector(DoorX, -237.1f, FloorZ + 214.0f));
		Label->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetVerticalAlignment(EVRTA_TextCenter);
		Label->SetWorldSize(6.5f);
		Label->SetTextRenderColor(FColor(34, 36, 40));
		Label->SetText(FText::AsCultureInvariant(FString(Number)));
		Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Label->SetCastShadow(false);
		Label->RegisterComponent();
	};

	// 잠긴 세대 문. 4층 이웃 문과 같은 문짝·철물·문선이다.
	const auto BuildLockedDoor = [this, DoorLeafMesh, DoorHardwareMesh, DoorFrameMesh](
		const float DoorX, const float FloorZ)
	{
		if (DoorLeafMesh)
		{
			CreateBlock(
				FVector(DoorX, -234.5f, FloorZ), FVector(100, 100, 100),
				nullptr, true, DoorLeafMesh, FRotator::ZeroRotator);
		}
		if (DoorHardwareMesh)
		{
			CreateBlock(
				FVector(DoorX, -234.5f, FloorZ), FVector(100, 100, 100),
				nullptr, false, DoorHardwareMesh, FRotator::ZeroRotator);
		}
		if (DoorFrameMesh)
		{
			CreateBlock(
				FVector(DoorX, -233, FloorZ), FVector(100, 100, 100),
				nullptr, false, DoorFrameMesh, FRotator::ZeroRotator);
		}
		CreateBlock(FVector(DoorX, -236, FloorZ + 214.0f), FVector(16, 2, 8), SignWhiteMaterial, false);
	};

	// 한 층의 복도. 열린 문 하나와 잠긴 문 둘이 북쪽 벽에 선다.
	const auto BuildStorey = [this, CorridorFloor, CorridorCeil, CorridorWallX, DadoX, Skirting,
			DoorFrameMesh, RingMesh, DomeMesh, BuildLockedDoor](
		const float FloorZ, const float OpenDoorX, const float LockedWestX, const float LockedEastX)
	{
		// 바닥과 천장. 4층처럼 벽 밑으로 10 cm씩 들어간다.
		IGPrologueWorld::TagFootstepSurface(CreateBlock(
			FVector(50, -305, FloorZ - 10.0f), FVector(760, 160, 20), CorridorFloor),
			IGPrologueWorld::FootstepConcreteTag);
		CreateBlock(FVector(50, -305, FloorZ + 250.0f), FVector(760, 160, 20), CorridorCeil);
		// 남쪽은 정면 벽돌의 안쪽 면이다. 미장을 한 겹 대고 허리 아래를 진하게 칠한다.
		CreateBlock(FVector(55, -373.5f, FloorZ + 120.0f), FVector(750, 3, 240), CorridorWallX);
		CreateBlock(FVector(55, -371.6f, FloorZ + 50.0f), FVector(750, 0.8f, 100), DadoX, false);
		CreateBlock(FVector(55, -370.25f, FloorZ + 6.0f), FVector(750, 2.7f, 12), Skirting, false);
		// 북쪽 벽. 계단탑 목 기둥 뒤를 메우고, 열린 문 자리만 비운다.
		CreateBlock(FVector(-330, -222.5f, FloorZ + 120.0f), FVector(20, 15, 240), CorridorWallX);
		CreateBlock(
			FVector((-320.0f + OpenDoorX - 45.0f) * 0.5f, -225, FloorZ + 120.0f),
			FVector(OpenDoorX - 45.0f + 320.0f, 20, 240), CorridorWallX);
		CreateBlock(
			FVector((OpenDoorX + 45.0f + 430.0f) * 0.5f, -225, FloorZ + 120.0f),
			FVector(430.0f - OpenDoorX - 45.0f, 20, 240), CorridorWallX);
		CreateBlock(FVector(OpenDoorX, -225, FloorZ + 225.0f), FVector(90, 20, 30), CorridorWallX);
		// 문턱. 복도 바닥(Y -225까지)과 집 바닥(Y -215부터) 사이 벽 두께를 메운다.
		IGPrologueWorld::TagFootstepSurface(CreateBlock(
			FVector(OpenDoorX, -220, FloorZ - 10.0f), FVector(90, 10, 20),
			TexMat(TEXT("M_UtilityGraniteCladding"), ConcreteDarkMaterial)),
			IGPrologueWorld::FootstepConcreteTag);
		CreateBlock(
			FVector((-320.0f + OpenDoorX - 51.0f) * 0.5f, -236.75f, FloorZ + 6.0f),
			FVector(OpenDoorX - 51.0f + 320.0f, 3.5f, 12), Skirting, false);
		CreateBlock(
			FVector((OpenDoorX + 51.0f + 430.0f) * 0.5f, -236.75f, FloorZ + 6.0f),
			FVector(430.0f - OpenDoorX - 51.0f, 3.5f, 12), Skirting, false);
		if (DoorFrameMesh)
		{
			CreateBlock(
				FVector(OpenDoorX, -233, FloorZ), FVector(100, 100, 100),
				nullptr, false, DoorFrameMesh, FRotator::ZeroRotator);
		}
		CreateBlock(FVector(OpenDoorX, -236, FloorZ + 214.0f), FVector(16, 2, 8), SignWhiteMaterial, false);
		BuildLockedDoor(LockedWestX, FloorZ);
		BuildLockedDoor(LockedEastX, FloorZ);
		// 계단탑으로 나가는 목. 4층과 같은 검은 기둥 둘과 머리, 그 위의 비상구 등이다.
		CreateBlock(FVector(-330, -237.5f, FloorZ + 120.0f), FVector(20, 15, 240), PlasticDarkMaterial);
		CreateBlock(FVector(-330, -372.5f, FloorZ + 120.0f), FVector(20, 15, 240), PlasticDarkMaterial);
		CreateBlock(FVector(-330, -305, FloorZ + 225.0f), FVector(20, 120, 30), PlasticDarkMaterial);
		CreateBlock(FVector(-313, -305, FloorZ + 220.0f), FVector(14, 8, 10), ScreenGlowMaterial, false);
		UPointLightComponent* ExitLamp = CreateLight(
			FVector(-313, -305, FloorZ + 213.0f), 22.0f, 340.0f,
			FLinearColor(0.30f, 1.0f, 0.42f), false, 5.0f);
		ExitLamp->SetVolumetricScatteringIntensity(0.4f);
		// 천장 등 둘. 낮에는 켜져 있고 그 시간에는 죽어 있다.
		for (const float FixtureX : {-170.0f, 200.0f})
		{
			if (RingMesh && DomeMesh)
			{
				CreateBlock(
					FVector(FixtureX, -305, FloorZ + 240.0f), FVector(100, 100, 100),
					nullptr, false, RingMesh, FRotator::ZeroRotator);
				LowerFloorLightDiscs.Add(CreateBlock(
					FVector(FixtureX, -305, FloorZ + 240.0f), FVector(100, 100, 100),
					LightPanelMaterial, false, DomeMesh, FRotator::ZeroRotator));
			}
			UPointLightComponent* HallLight = CreateLight(
				FVector(FixtureX, -305, FloorZ + 226.0f), 900.0f, 410.0f,
				FLinearColor(0.88f, 0.96f, 1.0f), true, 16.0f);
			HallLight->SetVolumetricScatteringIntensity(0.10f);
			LowerFloorLights.Add(HallLight);
			LowerFloorLightIntensities.Add(HallLight->Intensity);
		}
	};

	// --- 2층 -------------------------------------------------------------
	BuildingLightZone = EIGLightZone::SecondFloor;
	BuildStorey(300.0f, -150.0f, -30.0f, 78.0f);
	AddDoorNumber(-150.0f, 300.0f, TEXT("201"));
	AddDoorNumber(-30.0f, 300.0f, TEXT("202"));
	AddDoorNumber(78.0f, 300.0f, TEXT("203"));
	{
		// 201호. 세입자가 나간 뒤 관리인이 자재 창고로 쓴다. 장판을 걷어 낸 콘크리트
		// 바닥에 석고보드가 누워 쌓이고, 북동쪽 구석은 세운 보드와 걸어 둔 비닐 사이로
		// 사람 하나가 설 자리가 남는다(숨는 자리, SpawnInteractables).
		UMaterialInterface* PlasterX = TexMat(TEXT("M_MissingFloorPlaster_X"), ConcreteMaterial);
		UMaterialInterface* PlasterY = TexMat(TEXT("M_MissingFloorPlaster_Y"), ConcreteMaterial);
		UMaterialInterface* PlasterXY = TexMat(TEXT("M_MissingFloorPlaster_XY"), ConcreteMaterial);
		UMaterialInterface* BareFloor = TexMat(TEXT("M_Concrete_XY"), ConcreteMaterial);
		UMaterialInterface* Batten = TexMat(TEXT("M_WoodFurnitureUV"), WoodMaterial);
		IGPrologueWorld::TagFootstepSurface(CreateBlock(
			FVector(-160, -65, 290), FVector(280, 300, 20), BareFloor),
			IGPrologueWorld::FootstepConcreteTag);
		CreateBlock(FVector(-160, -65, 550), FVector(280, 300, 20), PlasterXY);
		CreateBlock(FVector(-307.5f, -57.5f, 420), FVector(15, 315, 240), PlasterY);
		CreateBlock(FVector(-12.5f, -57.5f, 420), FVector(15, 315, 240), PlasterY);
		CreateBlock(FVector(-160, 92.5f, 420), FVector(280, 15, 240), PlasterX);
		// 서쪽 벽을 따라 누운 보드 더미. 120 x 240 판을 쌓은 높이 60 cm다.
		CreateBlock(FVector(-238, -32.5f, 330), FVector(120, 231, 60), PlasterXY);
		CreateBlock(FVector(-226, -40, 375), FVector(96, 180, 30), PlasterXY);
		// 숨는 구석. 서쪽은 세워 둔 보드, 남쪽은 각목에 건 비닐이다.
		CreateBlock(FVector(-117.5f, 51.5f, 410), FVector(15, 63, 220), PlasterY);
		CreateBlock(FVector(-65, 25.5f, 532), FVector(90, 5, 4), Batten);
		if (UStaticMesh* SheetMesh = PropMesh(TEXT("SM_ConstructionSheetFloor")))
		{
			CreateBlock(
				FVector(-66, 25, 430), FVector(81, 232.6f, 100),
				nullptr, false, SheetMesh, FRotator(0, 0, 90));
		}
		CreateProp(TEXT("SM_WorkPaintCan"), FVector(-44, -118, 300), nullptr, 25.0f, 1.0f, true);
		CreateProp(TEXT("SM_WorkPaintCan"), FVector(-40, -144, 300), nullptr, -40.0f, 1.0f, true);
		CreateProp(TEXT("SM_CottonWorkGlove"), FVector(-232, -60, 390), nullptr, 70.0f, 1.0f, false);
		CreateProp(TEXT("SM_TrafficCone"), FVector(-278, -190, 300), nullptr, 0.0f, 1.0f, true);
		// 창고 한가운데 천장 등. 그 시간에는 죽어 있다.
		if (RingMesh && DomeMesh)
		{
			CreateBlock(
				FVector(-160, -60, 540), FVector(100, 100, 100),
				nullptr, false, RingMesh, FRotator::ZeroRotator);
			LowerFloorLightDiscs.Add(CreateBlock(
				FVector(-160, -60, 540), FVector(100, 100, 100),
				LightPanelMaterial, false, DomeMesh, FRotator::ZeroRotator));
		}
		UPointLightComponent* StoreLight = CreateLight(
			FVector(-160, -60, 526), 520.0f, 360.0f,
			FLinearColor(1.0f, 0.93f, 0.82f), true, 12.0f);
		LowerFloorLights.Add(StoreLight);
		LowerFloorLightIntensities.Add(StoreLight->Intensity);
	}

	// --- 3층 -------------------------------------------------------------
	BuildingLightZone = EIGLightZone::ThirdFloor;
	BuildStorey(600.0f, -30.0f, -150.0f, 78.0f);
	AddDoorNumber(-150.0f, 600.0f, TEXT("301"));
	AddDoorNumber(-30.0f, 600.0f, TEXT("302"));
	AddDoorNumber(78.0f, 600.0f, TEXT("303"));
	// 3층 복도 동쪽 끝. 2층은 승강기 홀의 벽이 같은 자리를 막는다.
	CreateBlock(FVector(440, -300, 720), FVector(20, 170, 240), CorridorWallY);
	{
		// 302호. 할머니가 요양원으로 가며 장롱만 두고 나갔다. 장판과 벽지가 남은 빈방이고,
		// 장롱 문틈이 숨는 자리다(SpawnInteractables).
		UMaterialInterface* Jangpan = TexMat(TEXT("M_Jangpan"), FloorMaterial);
		UMaterialInterface* PaperX = TexMat(TEXT("M_Wallpaper_X"), WallMaterial);
		UMaterialInterface* PaperY = TexMat(TEXT("M_Wallpaper_Y"), WallMaterial);
		UMaterialInterface* PaperCeil = TexMat(TEXT("M_WallpaperCeil"), WallMaterial);
		IGPrologueWorld::TagFootstepSurface(CreateBlock(
			FVector(-20, -65, 590), FVector(280, 300, 20), Jangpan),
			IGPrologueWorld::FootstepVinylTag);
		CreateBlock(FVector(-20, -65, 850), FVector(280, 300, 20), PaperCeil);
		CreateBlock(FVector(-167.5f, -57.5f, 720), FVector(15, 315, 240), PaperY);
		CreateBlock(FVector(127.5f, -57.5f, 720), FVector(15, 315, 240), PaperY);
		CreateBlock(FVector(-20, 92.5f, 720), FVector(280, 15, 240), PaperX);
		if (UStaticMesh* WardrobeMesh = PropMesh(TEXT("SM_Wardrobe")))
		{
			CreateBlock(
				FVector(-138, 0, 600), FVector(100, 100, 100),
				nullptr, true, WardrobeMesh, FRotator::ZeroRotator);
		}
		// 열려 있는 현관문. 안쪽 벽에 붙여 젖혀 두었다.
		if (DoorLeafMesh)
		{
			CreateBlock(
				FVector(-80, -170, 600), FVector(100, 100, 100),
				nullptr, true, DoorLeafMesh, FRotator(0, 90, 0));
		}
		CreateProp(TEXT("SM_KimchiTub"), FVector(90, 60, 600), nullptr, 15.0f, 1.0f, true);
		CreateProp(TEXT("SM_Basket"), FVector(84, 20, 600), nullptr, -20.0f, 1.0f, true);
		CreateProp(TEXT("SM_HouseSlipper"), FVector(-12, -190, 600), nullptr, 8.0f, 1.0f, false);
		CreateProp(TEXT("SM_HouseSlipper"), FVector(2, -192, 600), nullptr, -4.0f, 1.0f, false);
		if (RingMesh && DomeMesh)
		{
			CreateBlock(
				FVector(-20, -60, 840), FVector(100, 100, 100),
				nullptr, false, RingMesh, FRotator::ZeroRotator);
			LowerFloorLightDiscs.Add(CreateBlock(
				FVector(-20, -60, 840), FVector(100, 100, 100),
				LightPanelMaterial, false, DomeMesh, FRotator::ZeroRotator));
		}
		UPointLightComponent* RoomLight = CreateLight(
			FVector(-20, -60, 826), 420.0f, 360.0f,
			FLinearColor(1.0f, 0.90f, 0.76f), true, 12.0f);
		LowerFloorLights.Add(RoomLight);
		LowerFloorLightIntensities.Add(RoomLight->Intensity);
	}

	ActiveParent = nullptr;
}

void AIGPrologueWorldScene::SuspendCorridorFlicker(const bool bSuspend)
{
	bCorridorFlickerSuspended = bSuspend;
}

void AIGPrologueWorldScene::SetFixtureLive(
	const int32 Index,
	const bool bLive,
	const bool bCorridor)
{
	TArray<TObjectPtr<UPointLightComponent>>& FixtureLights =
		bCorridor ? CorridorLights : LobbyLights;
	TArray<TObjectPtr<UStaticMeshComponent>>& FixtureDiscs =
		bCorridor ? CorridorLightDiscs : LobbyLightDiscs;

	if (!FixtureLights.IsValidIndex(Index) || !FixtureDiscs.IsValidIndex(Index))
	{
		return;
	}

	// A fixture is the light AND the disc: kill both or the ceiling keeps a
	// glowing ring where the lamp used to be.
	float Scale = NightFixtureScale(Index, bCorridor);
	if (bCorridor && !bTheHourSealed)
	{
		// 계단 쪽 낡은 등 하나는 상시등, 나머지는 사람이 지나갈 때 켜진다.
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const bool bSensorOn = Index == 0 || (CorridorSensorLastSeen.IsValidIndex(Index)
			&& Now - CorridorSensorLastSeen[Index] < 18.0);
		Scale *= bSensorOn ? 0.72f : 0.0f;
	}
	const bool bShines = bLive && bCommonInspectionLightsEnabled && Scale > 0.0f;
	if (UPointLightComponent* Light = FixtureLights[Index])
	{
		Light->SetIntensity(bShines ? (bCorridor ? 1020.0f : 920.0f) * Scale : 0.0f);
	}
	if (UStaticMeshComponent* Disc = FixtureDiscs[Index])
	{
		Disc->SetMaterial(0, bShines ? LightPanelMaterial : PlasticDarkMaterial);
	}
}

float AIGPrologueWorldScene::NightFixtureScale(
	const int32 Index,
	const bool bCorridor) const
{
	if (!bTheHourSealed)
	{
		return 1.0f;
	}
	// 서쪽(계단 입구)의 죽어 가는 등이 0번이다. 그것만 남고, 그것도 3분의 1이다.
	if (bCorridor)
	{
		return Index == 0 ? 0.32f : 0.0f;
	}
	// 로비는 우편함 위 하나. 계량기함과 관리실은 손전등으로 읽는다.
	return Index == 0 ? 0.45f : 0.0f;
}

void AIGPrologueWorldScene::ApplyNightAtmosphere(const bool bSealed)
{
	if (StoreClerk) { StoreClerk->SetActorHiddenInGame(bSealed); }
	// 등. SetFixtureLive가 밤 배율을 곱한다. 새벽에는 전부 낮의 밝기로 돌아온다.
	for (int32 Index = 0; Index < CorridorLights.Num(); ++Index)
	{
		SetFixtureLive(Index, true, true);
	}
	for (int32 Index = 0; Index < LobbyLights.Num(); ++Index)
	{
		SetFixtureLive(Index, true, false);
	}
	ApplyServiceLights();
	if (HeightFog)
	{
		// 실내 공기. 손전등 원뿔이 서고 복도 끝이 흐려진다. 높이 감쇠를 거의
		// 없애 4층과 1층이 같은 공기를 갖게 한다. 새벽에는 골목 안개로 되돌린다.
		HeightFog->SetFogDensity(bSealed ? 0.08f : 0.012f);
		HeightFog->SetFogHeightFalloff(bSealed ? 0.02f : 0.4f);
		HeightFog->SetFogInscatteringColor(
			bSealed
				? FLinearColor(0.010f, 0.012f, 0.018f)
				: FLinearColor(0.030f, 0.042f, 0.085f));
		HeightFog->SetVolumetricFogScatteringDistribution(bSealed ? 0.35f : 0.55f);
		HeightFog->SetVolumetricFogExtinctionScale(bSealed ? 1.8f : 1.0f);
	}
	SetUnit401GapLit(bSealed);
	HourSealedAtSeconds = bSealed && GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	ApplyExteriorTimeOfDay();
	UpdateNeighborhoodAwake();
	if (PostProcess)
	{
		// 카메라 룩. 밤은 어둠을 들어 올리지 않고, 가장자리가 흐려지고 색이 빠진다.
		// 그림자는 차갑고 손전등의 하이라이트는 따뜻하다.
		FPostProcessSettings& Look = PostProcess->Settings;
		Look.bOverride_SceneFringeIntensity = true;
		Look.SceneFringeIntensity = 0.0f;
		Look.VignetteIntensity = bSealed ? 0.28f : 0.12f;
		Look.ColorSaturation = bSealed
			? FVector4(0.80f, 0.86f, 1.0f, 1.0f)
			: FVector4(0.93f, 0.95f, 1.0f, 1.0f);
		Look.BloomIntensity = bSealed ? 0.34f : 0.18f;
		Look.bOverride_ColorGainShadows = true;
		Look.ColorGainShadows = bSealed
			? FVector4(0.86f, 0.96f, 1.12f, 1.0f)
			: FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		Look.bOverride_ColorGainHighlights = true;
		Look.ColorGainHighlights = bSealed
			? FVector4(1.06f, 1.0f, 0.92f, 1.0f)
			: FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		Look.bOverride_ColorCorrectionShadowsMax = true;
		Look.ColorCorrectionShadowsMax = 0.09f;
		Look.LocalExposureShadowContrastScale = bSealed ? 0.92f : 0.76f;
		Look.LocalExposureHighlightContrastScale = bSealed ? 0.90f : 0.84f;
	}
	ApplyStreetNightLevel(bSealed);
}

void AIGPrologueWorldScene::ApplyStreetNightLevel(const bool bNight)
{
	// 그 시간에는 창이 열리지 않고 도로가 멀다. 새벽에는 잠금이 풀리는 소리와
	// 함께 8초에 걸쳐 차오른다 — 밤 베드가 가라앉는 사이 낮이 밤보다 조용하지
	// 않게. 새벽마다 같은 녹음이라 새소리 같은 반복이 드러나는 층은 얹지 않는다.
	if (StreetBedComponent && StreetBedComponent->IsPlaying())
	{
		StreetBedComponent->AdjustVolume(bNight ? 2.0f : 8.0f, bNight ? 0.4f : 1.0f);
	}
	// 작아지기만 하면 먼 도로일 뿐이다. 건물이 잠기면 바깥은 유리 한 겹 뒤로
	// 물러나 고역부터 빠진다.
	if (StreetBedComponent)
	{
		StreetBedComponent->SetLowPassFilterFrequency(bNight ? 1200.0f : 20000.0f);
		StreetBedComponent->SetLowPassFilterEnabled(bNight);
	}
}

void AIGPrologueWorldScene::UpdateLightZones()
{
	const APlayerController* Controller =
		GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Controller || !Controller->PlayerCameraManager)
	{
		return;
	}
	// 폰이 아니라 카메라 자리로 판단한다. 캡처와 연출이 카메라만 옮길 때도 맞는다.
	const FVector Eye = GetActorTransform().InverseTransformPosition(
		Controller->PlayerCameraManager->GetCameraLocation());
	UpdateCorridorSensors(Eye);
	// 구역 번호: 0 1층(로비·골목·편의점), 4 2층, 5 3층, 1 4층, 2 옥상·별관.
	// 층 경계는 그 층 슬래브 윗면 20 cm 아래다. 아래층에서 뛰어도 넘지 않고,
	// 위층 침대 밑에 엎드려도 내려가지 않는다. 경계에서 40 cm는 앞의 판단을
	// 유지해 계단을 오르내릴 때 등이 번갈아 켜지지 않게 한다.
	constexpr float LevelFloors[] = {280.0f, 580.0f, 880.0f, 1170.0f};
	constexpr int32 LevelBands[] = {0, 4, 5, 1, 2};
	constexpr float Hysteresis = 40.0f;
	int32 Level = 0;
	while (Level < 4 && Eye.Z >= LevelFloors[Level])
	{
		++Level;
	}
	int32 ActiveLevel = INDEX_NONE;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		if (LevelBands[Index] == ActiveLightBand)
		{
			ActiveLevel = Index;
		}
	}
	if (ActiveLevel != INDEX_NONE && FMath::Abs(Level - ActiveLevel) == 1)
	{
		const float Boundary = LevelFloors[FMath::Min(Level, ActiveLevel)];
		if (FMath::Abs(Eye.Z - Boundary) < Hysteresis)
		{
			Level = ActiveLevel;
		}
	}
	int32 Band = LevelBands[Level];
	// 3: 다른 층을 비추는 화면(관리실 CCTV 5번)이 살아 있다. 모든 층을 켠다.
	if (RemoteViewCount > 0)
	{
		Band = 3;
	}
	// 403호 문이 완전히 닫혀 있으면 방 안과 복도의 등은 서로 닿지 않는다. 등의 구는
	// 벽을 지나 반대편까지 걸쳐서, 켜 두면 가려질 그림자를 매 픽셀 다시 계산한다.
	// 문이 움직이기 시작하면 bOpen이 먼저 바뀌어 양쪽이 바로 다시 켜진다.
	int32 HomeView = 0;
	if (Band == 1 && HomeDoor && HomeDoor->IsFullyClosed())
	{
		const bool bInsideHome = FMath::Abs(Eye.X) < 200.0f
			&& Eye.Y > HomeDoorY && Eye.Y < 225.0f
			&& Eye.Z > FourthFloorZ - 10.0f && Eye.Z < FourthFloorZ + 250.0f;
		HomeView = bInsideHome ? 1 : 2;
	}
	if (Band == ActiveLightBand && HomeView == ActiveHomeView)
	{
		return;
	}
	ActiveLightBand = Band;
	ActiveHomeView = HomeView;

	// 층마다 보일 수 있는 공간. 2~4층 창은 전부 불투명한 원경이고, 로비의 유리
	// 현관은 골목과 편의점 쪽을 보여 준다. 옥상 가장자리에서는 골목이 내려다보인다.
	const auto ZoneVisible = [Band, HomeView](const EIGLightZone Zone)
	{
		if (Band == 3)
		{
			return true;
		}
		const bool bFourth = Band == 1;
		const bool bRoof = Band == 2;
		switch (Zone)
		{
		case EIGLightZone::HomeInterior:
			return bFourth && HomeView != 2;
		case EIGLightZone::FourthFloorRooms:
			return bFourth && HomeView != 1;
		case EIGLightZone::FourthFloor:
		case EIGLightZone::UpperStair:
			return (bFourth || bRoof) && HomeView != 1;
		// 별관은 옥상에서만 보인다. 4층에서 보이는 윗계단 등은 UpperStair에 따로 있다.
		// 켜 두면 별관 등의 구가 바닥판을 지나 403호까지 닿아, 가려진 그림자를 매
		// 픽셀 다시 계산하고 그림자 없는 등은 천장을 뚫고 비춘다.
		case EIGLightZone::Annex:
			return bRoof;
		case EIGLightZone::Lobby:
			return Band == 0;
		case EIGLightZone::Alley:
		case EIGLightZone::Store:
			return Band == 0 || bRoof;
		case EIGLightZone::SecondFloor:
			return Band == 4;
		case EIGLightZone::ThirdFloor:
			return Band == 5;
		// 계단을 오르내리는 동안 위아래 참이 보인다. 건물 안이면 층을 가리지 않는다.
		case EIGLightZone::StairCore:
			return !bRoof && HomeView != 1;
		default:
			return true;
		}
	};
	for (int32 Zone = 0; Zone < static_cast<int32>(EIGLightZone::Count); ++Zone)
	{
		const bool bZoneVisible = ZoneVisible(static_cast<EIGLightZone>(Zone));
		for (const TWeakObjectPtr<ULightComponent>& Light : ZoneLights[Zone])
		{
			if (ULightComponent* Resolved = Light.Get())
			{
				// 4일 차 밤에 차단기가 내려간 별관 등은 층을 오르내려도 다시 켜지지 않는다.
				const bool bVisible = bZoneVisible
					&& (bMissingFloorAnnexPowered
						|| !MissingFloorAnnexLights.Contains(Cast<UPointLightComponent>(Resolved)));
				if (Resolved->IsVisible() != bVisible)
				{
					Resolved->SetVisibility(bVisible);
				}
			}
		}
	}
}

void AIGPrologueWorldScene::UpdateCorridorSensors(const FVector& LocalEye)
{
	if (bTheHourSealed || !GetWorld())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bInCorridor = LocalEye.Z > 910.0f && LocalEye.Z < 1160.0f
		&& LocalEye.Y > -395.0f && LocalEye.Y < -218.0f;
	for (int32 Index = 1; Index < CorridorLights.Num(); ++Index)
	{
		if (!CorridorLights[Index] || !CorridorSensorLastSeen.IsValidIndex(Index))
		{
			continue;
		}
		const float FixtureX = GetActorTransform().InverseTransformPosition(
			CorridorLights[Index]->GetComponentLocation()).X;
		if (bInCorridor && FMath::Abs(LocalEye.X - FixtureX) < 185.0f)
		{
			CorridorSensorLastSeen[Index] = Now;
		}
		SetFixtureLive(Index, true, true);
	}
}

void AIGPrologueWorldScene::ApplyExteriorTimeOfDay()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
	const bool bMorning = !bTheHourSealed && Narrative && Narrative->GetNightIndex() > 0;
	if (PreDawnSun)
	{
		PreDawnSun->SetWorldRotation(FRotator(bMorning ? -2.0f : 14.0f, 180.0f, 0.0f));
		PreDawnSun->SetIntensity(bMorning ? 900.0f : 120000.0f);
		PreDawnSun->SetLightColor(bMorning
			? FLinearColor(1.0f, 0.82f, 0.64f) : FLinearColor(1.0f, 0.86f, 0.72f));
		PreDawnSun->ForwardShadingPriority = bMorning ? 1 : 0;
		// 2026-10-01 측정: 밤에 그림자를 꺼도 §11 V5 11지점 값이 소수 셋째 자리까지 같았다.
		PreDawnSun->SetCastShadows(bMorning);
	}
	if (MoonLight)
	{
		MoonLight->SetIntensity(bMorning ? 0.0f : 0.25f);
		MoonLight->ForwardShadingPriority = bMorning ? 0 : 1;
	}
	if (SkyAmbient) { SkyAmbient->SetIntensity(bMorning ? 0.85f : 0.62f); }
	if (bMorning && HeightFog)
	{
		HeightFog->SetFogDensity(0.004f);
		HeightFog->SetFogInscatteringColor(FLinearColor(0.12f, 0.15f, 0.18f));
	}
	if (bMorning && PostProcess)
	{
		PostProcess->Settings.AutoExposureMinBrightness = 0.0f;
		PostProcess->Settings.AutoExposureMaxBrightness = 10.0f;
	}
	for (UPrimitiveComponent* Surface : NightViewSurfaces)
	{
		if (Surface) { Surface->SetCustomPrimitiveDataFloat(2, bMorning ? 1.0f : 0.0f); }
	}
	UE_LOG(LogIndieGame, Display, TEXT("EXTERIOR_TIME mode=%s night=%d"),
		bTheHourSealed ? TEXT("night") : (bMorning ? TEXT("morning") : TEXT("arrival")),
		Narrative ? Narrative->GetNightIndex() : 0);
}

void AIGPrologueWorldScene::SetRemoteViewActive(const bool bActive)
{
	RemoteViewCount = FMath::Max(0, RemoteViewCount + (bActive ? 1 : -1));
	// 구역을 바로 다시 계산한다. 다음 타이머를 기다리면 CCTV 첫 장면이 꺼진 층을 찍는다.
	ActiveLightBand = -1;
	UpdateLightZones();
}

void AIGPrologueWorldScene::SetUnit401GapLit(const bool bLit)
{
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.ClearTimer(Unit401GapBeatTimer);
	Timers.ClearTimer(Unit401GapPollTimer);
	Unit401GapBeatStep = 0;
	SetUnit401GapShadow(-1, 0);
	for (UStaticMeshComponent* Segment : Unit401GapSegments)
	{
		if (Segment)
		{
			Segment->SetVisibility(bLit);
		}
	}
	if (Unit401GapLight)
	{
		Unit401GapLight->SetIntensity(bLit ? Unit401GapLightIntensity : 0.0f);
	}
	if (!bLit)
	{
		return;
	}
	// 그 시간이 다시 시작하면(포획 뒤 04:30 포함) 한 번 더 볼 수 있다.
	bUnit401GapBeatPlayed = false;
	Timers.SetTimer(
		Unit401GapPollTimer, this, &ThisClass::PollUnit401GapVisitor, 0.25f, true);
}

void AIGPrologueWorldScene::PollUnit401GapVisitor()
{
	// 캡처·광량 검사·밤1 연출이 복도 등을 쥐고 있을 때는 같은 이유로 비킨다.
	// 측정 중에 문 아래 빛이 꺼지면 같은 지점이 매번 다른 값을 낸다.
	if (bUnit401GapBeatPlayed || bCorridorFlickerSuspended || !Unit401GapLight)
	{
		return;
	}
	const APlayerController* Controller =
		GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	const FVector Offset =
		Pawn->GetActorLocation() - Unit401GapLight->GetComponentLocation();
	// 문 앞 2.2 m, 같은 층. 문을 두드리지 않고 지나가기만 해도 안에서는 안다.
	if (FVector2D(Offset.X, Offset.Y).Size() > 220.0f || FMath::Abs(Offset.Z) > 150.0f)
	{
		return;
	}
	bUnit401GapBeatPlayed = true;
	Unit401GapBeatStep = 0;
	AdvanceUnit401GapBeat();
}

void AIGPrologueWorldScene::AdvanceUnit401GapBeat()
{
	if (!Unit401GapLight)
	{
		return;
	}
	// 발 둘이 문 앞에 멈추고, 한 번 무게를 옮기고, 불이 꺼진다. 한참 뒤 다시
	// 켜질 때는 아무도 서 있지 않다. 소리는 내지 않는다 — 문 너머에서 숨죽인
	// 사람이 낼 소리가 없다.
	float NextSeconds = 0.0f;
	switch (Unit401GapBeatStep++)
	{
	case 0:
		SetUnit401GapShadow(3, 2);
		Unit401GapLight->SetIntensity(Unit401GapLightIntensity * 0.72f);
		NextSeconds = 1.6f;
		break;
	case 1:
		SetUnit401GapShadow(2, 2);
		Unit401GapLight->SetIntensity(Unit401GapLightIntensity * 0.70f);
		NextSeconds = 1.3f;
		break;
	case 2:
		for (UStaticMeshComponent* Segment : Unit401GapSegments)
		{
			if (Segment)
			{
				Segment->SetVisibility(false);
			}
		}
		Unit401GapLight->SetIntensity(0.0f);
		NextSeconds = 7.5f;
		break;
	case 3:
		SetUnit401GapShadow(-1, 0);
		Unit401GapLight->SetIntensity(Unit401GapLightIntensity);
		return;
	default:
		return;
	}
	GetWorldTimerManager().SetTimer(
		Unit401GapBeatTimer, this, &ThisClass::AdvanceUnit401GapBeat, NextSeconds, false);
}

void AIGPrologueWorldScene::SetUnit401GapShadow(
	const int32 FirstSegment,
	const int32 SegmentCount)
{
	for (int32 Index = 0; Index < Unit401GapSegments.Num(); ++Index)
	{
		if (UStaticMeshComponent* Segment = Unit401GapSegments[Index])
		{
			const bool bBlocked = FirstSegment >= 0
				&& Index >= FirstSegment
				&& Index < FirstSegment + SegmentCount;
			Segment->SetVisibility(!bBlocked);
		}
	}
}

FVector AIGPrologueWorldScene::GetCorridorFixtureLocation(const int32 Index) const
{
	return CorridorLights.IsValidIndex(Index) && CorridorLights[Index]
		? CorridorLights[Index]->GetComponentLocation()
		: FVector::ZeroVector;
}

void AIGPrologueWorldScene::BuildFifthFloorAnnex()
{
	// Release topology, all in one world: the existing 4F stair continues to
	// the roof slab, a 1.2 m maintenance lane passes the real tank, and the
	// second fire door enters the illegal annex. No portal or camera cut is
	// allowed to stand in for this walk.
	ActiveParent = nullptr;

	// 수평 슬래브에는 XY 매핑을 쓴다. _X는 (X, Z) 마스킹이라 Z가 일정한
	// 바닥에서 텍스처가 한 줄로 잘려 늘어난다 — 별관 바닥이 그 상태였다.
	UMaterialInterface* AnnexWallX = TexMat(TEXT("M_MissingFloorPlaster_X"), ConcreteMaterial);
	UMaterialInterface* AnnexWallY = TexMat(TEXT("M_MissingFloorPlaster_Y"), ConcreteMaterial);
	UMaterialInterface* AnnexCeiling = TexMat(TEXT("M_MissingFloorPlaster_XY"), ConcreteMaterial);
	// 노출된 경량 스터드. 여기에 M_MeterBox를 쓰고 있었는데 그 재질은
	// 분전반 문짝 도장면이라 「분전반」·「취급주의」 활자가 인쇄돼 있다.
	// 폭 4 cm 스터드 여섯 개에 그 글자가 잘려 실려서, 손전등이 스치면
	// 흰색·노란색 획이 세로로 흩어진 노이즈처럼 보였다.
	UMaterialInterface* Stud = TexMat(TEXT("M_MetalUV"), MetalFrameMaterial);
	// 계단과 옥상 바닥은 서로 다른 발소리 표면인데 한 변수를 공유하고
	// 있었다. 옥상 방수층은 조용하고 철제 계단은 길게 울린다 — 같은
	// 콘크리트로 그리면 그 차이를 볼 방법이 없다.
	UMaterialInterface* RoofFloor = TexMat(TEXT("M_Concrete_XY"), ConcreteMaterial);
	UMaterialInterface* StairSteel =
		TexMat(TEXT("M_MissingFloorSteelStair"), RoofFloor);
	UMaterialInterface* RoofDeck =
		TexMat(TEXT("M_RooftopWaterproofing_XY"), RoofFloor);
	UMaterialInterface* RoofMetal = TexMat(TEXT("M_UtilityTankSteel"), MetalFrameMaterial);
	UMaterialInterface* RailMetal = TexMat(TEXT("M_StainlessUV"), MetalFrameMaterial);
	UMaterialInterface* StairRiser = TexMat(TEXT("M_SteelDoorUV"), MetalFrameMaterial);

	MissingFloorUpperStairSteps.Reset();
	MissingFloorRooftopRouteFloors.Reset();
	MissingFloorAnnexLights.Reset();
	MissingFloorCavityWallPanel = nullptr;
	MissingFloorCavityWallResidue = nullptr;
	bMissingFloorCavityOpen = false;

	// Fourteen 17 cm risers continue directly from the authored 4F threshold.
	// Each box grows from the same 963 cm base, producing a solid, sweep-safe
	// stair rather than independent floating treads.
	constexpr float StairBaseZ = 963.0f;
	constexpr float RiserHeight = 17.0f;
	constexpr float TreadDepth = 22.0f;
	for (int32 StepIndex = 0;
		StepIndex < IGPrologueWorld::MissingFloorUpperStepCount;
		++StepIndex)
	{
		const float Height = RiserHeight * (StepIndex + 1);
		UStaticMeshComponent* Step = CreateBlock(
			FVector(
				-277.5f,
				-100.0f + TreadDepth * StepIndex,
				StairBaseZ + Height * 0.5f),
			FVector(85.0f, TreadDepth, Height),
			StairRiser);
		IGPrologueWorld::TagFootstepSurface(
			Step,
			IGPrologueWorld::FootstepMetalStairTag);
		MissingFloorUpperStairSteps.Add(Step);
	}
	// 단의 충돌은 기존 상자가 맡는다. 얇은 마감과 노멀맵만 20개 인스턴스로 그린다.
	if (UStaticMesh* TreadMesh = PropMesh(TEXT("SM_RoofStairTread")))
	{
		UInstancedStaticMeshComponent* Treads = NewObject<UInstancedStaticMeshComponent>(this, TEXT("RoofStairFinishes"));
		Treads->SetupAttachment(SceneRoot);
		Treads->SetStaticMesh(TreadMesh);
		Treads->SetMobility(EComponentMobility::Static);
		Treads->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Treads->SetGenerateOverlapEvents(false);
		Treads->SetCanEverAffectNavigation(false);
		Treads->SetCastShadow(false);
		Treads->SetAffectDistanceFieldLighting(false);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Treads->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-277.5f, -216.f + Index * 22.f, 918.f + Index * 18.f), FVector(1, 1, 18.f / 17.f)));
		}
		for (int32 Index = 0; Index < IGPrologueWorld::MissingFloorUpperStepCount; ++Index)
		{
			Treads->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-277.5f, -100.f + Index * 22.f, 980.f + Index * 17.f)));
		}
		Treads->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-277.5f, -125.f, 972.f), FVector(1, 28.f / 22.f, 1)));
		Treads->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-277.5f, 208.5f, 1200.f), FVector(1, 23.f / 22.f, 1)));
		AddInstanceComponent(Treads);
		Treads->RegisterComponent();
		GeometryComponents.Add(Treads);
	}
	CreateBlock(FVector(-317, -220, 918), FVector(100), nullptr, false, PropMesh(TEXT("SM_RoofStairHandrail")));
	// 4층 계단과 같은 이유로 마지막 단의 북쪽 마구리를 덮는다. 이쪽은
	// 2.2 m가 통째로 드러나 있어 계단참에서 올려다보면 바로 보인다.
	// 열네 번째 단은 Y=186에서 끝나므로 그 북쪽 면은 Y=197, 위로는 계단참
	// 밑면 Z=1182까지다.
	CreateBlock(
		FVector(-277.5f, 197.3f, 1072.5f),
		FVector(85.0f, 0.6f, 219.0f),
		TexMat(TEXT("M_SteelDoorUV"), MetalFrameMaterial),
		false);
	IGPrologueWorld::TagFootstepSurface(CreateBlock(
		FVector(-277.5f, 208.5f, 1191.0f),
		FVector(85.0f, 23.0f, 18.0f),
		StairSteel), IGPrologueWorld::FootstepMetalStairTag);

	// Enclose the upper flight. The side walls overlap the existing stub by
	// 10 cm, so there is no blue-sky seam when looking up from 4F.
	CreateBlock(
		FVector(-332.5f, 52.5f, 1200.0f),
		FVector(15.0f, 325.0f, 480.0f),
		AnnexWallY);
	CreateBlock(
		FVector(-222.5f, 52.5f, 1200.0f),
		FVector(15.0f, 325.0f, 480.0f),
		AnnexWallY);

	// The reinforced roof slab is split around the stair opening. All three
	// pieces collide; their top plane and the annex floor are exactly Z=1200.
	MissingFloorRooftopRouteFloors.Add(CreateBlock(
		FVector(-420.0f, -40.0f, 1190.0f),
		FVector(160.0f, 520.0f, 20.0f),
		RoofDeck));
	MissingFloorRooftopRouteFloors.Add(CreateBlock(
		FVector(142.5f, -40.0f, 1190.0f),
		FVector(715.0f, 520.0f, 20.0f),
		RoofDeck));
	MissingFloorRooftopRouteFloors.Add(CreateBlock(
		FVector(0.0f, 335.0f, 1190.0f),
		FVector(1000.0f, 230.0f, 20.0f),
		RoofDeck));
	for (UStaticMeshComponent* RouteFloor : MissingFloorRooftopRouteFloors)
	{
		IGPrologueWorld::TagFootstepSurface(
			RouteFloor,
			IGPrologueWorld::FootstepRooftopTag);
	}

	// 정상 수위 150cm, 지름 130cm면 약 2,000L다. 종전의 306cm 원통과
	// 1m짜리 기단은 용량 표기와 맞지 않았고 주변 통로까지 좁혔다.
	const FVector TankCenter(0.0f, 60.0f, 0.0f);
	CreateBlock(
		TankCenter + FVector(0.0f, 0.0f, 1210.0f),
		FVector(170.0f, 170.0f, 20.0f),
		TexMat(TEXT("M_UtilityFoundation"), ConcreteMaterial));
	if (UStaticMesh* TankShell = PropMesh(TEXT("SM_RoofTank2000L")))
	{
		CreateBlock(
			TankCenter + FVector(0.0f, 0.0f, 1220.0f),
			FVector(100.0f),
			nullptr,
			false,
			TankShell);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(
			TankCenter + FVector(0.0f, 0.0f, 1300.0f),
			FVector(130.0f, 130.0f, 160.0f),
			RoofMetal,
			false,
			CylinderMesh);
	}
	if (UStaticMeshComponent* TankCollision = CreateBlock(
			TankCenter + FVector(0.0f, 0.0f, 1300.0f),
			FVector(130.0f, 130.0f, 160.0f),
			RoofMetal,
			true,
			CylinderMesh))
	{
		TankCollision->SetVisibility(false, true);
		TankCollision->SetHiddenInGame(true, true);
		TankCollision->SetCastShadow(false);
	}
	if (UStaticMeshComponent* Plate = CreateBlock(FVector(0, 125.7f, 1340), FVector(100), nullptr, false,
		PropMesh(TEXT("SM_RoofTankPlate")), FRotator(0, 180, 0)))
	{
		Plate->ComponentTags.Add(TEXT("Visual.RoofTankPlate"));
		Plate->SetCastShadow(false);
		Plate->SetCullDistance(1200);
	}

	// The route centerline is exactly 407.5 + 232.5 = 640 cm. Rail placement
	// leaves a 120 cm clear lane and makes the corner physical, not a waypoint.
	auto AddRail = [this, RailMetal](
		const FVector& Start,
		const FVector& End)
	{
		const FVector Delta = End - Start;
		const float Length = Delta.Size2D();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		const FVector Midpoint = (Start + End) * 0.5f;
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		for (const float Z : {1248.0f, 1294.0f})
		{
			CreateBlock(
				FVector(Midpoint.X, Midpoint.Y, Z),
				FVector(Length, 4.0f, 4.0f),
				RailMetal,
				true,
				nullptr,
				FRotator(0.0f, Yaw, 0.0f));
		}
		// Roughly one-metre post bays match ordinary Korean rooftop guardrails.
		// The two continuous horizontal bars own collision between the posts, so
		// denser greybox pickets only add visual noise without improving safety.
		const int32 PostCount = FMath::Max(2, FMath::CeilToInt(Length / 100.0f) + 1);
		for (int32 PostIndex = 0; PostIndex < PostCount; ++PostIndex)
		{
			const float Alpha = PostCount > 1
				? static_cast<float>(PostIndex) / static_cast<float>(PostCount - 1)
				: 0.0f;
			const FVector Point = FMath::Lerp(Start, End, Alpha);
			CreateBlock(
				FVector(Point.X, Point.Y, 1247.0f),
				FVector(4.0f, 4.0f, 94.0f),
				RailMetal);
		}
	};
	// 남쪽 난간은 계단탑 동쪽 벽면(X=-215)에서 시작해 물탱크 받침대 서쪽
	// 모서리(X=-180)까지 35 cm를 메운다. X=-277.5는 계단 한복판이었다 —
	// 그 자리에서 시작하면 가로대 둘이 동쪽 벽을 뚫고 들어가 계단 위를
	// 가로지르고, 기둥 하나가 열두 번째 디딤판 16 cm 위에 떠서 통로 85 cm
	// 중 42.5 cm를 막았다. 캡슐 지름이 68 cm이므로 옥상으로 올라갈 수 없었다.
	AddRail(FVector(-215.0f, 160.0f, 0.0f), FVector(-180.0f, 160.0f, 0.0f));
	AddRail(FVector(-277.5f, 280.0f, 0.0f), FVector(70.0f, 280.0f, 0.0f));
	// 북쪽 끝은 부속동 남벽의 바깥면(Y=445)에서 멈춘다. Y 452.5는 그 벽의
	// 중심선이라, 마지막 기둥이 두께 15 cm 벽 한가운데에 통째로 파묻혀
	// 보이지도 않는 물건이 되고 가로대는 벽으로 7.5 cm 들어갔다.
	AddRail(FVector(190.0f, 160.0f, 0.0f), FVector(190.0f, 445.0f, 0.0f));
	AddRail(FVector(70.0f, 280.0f, 0.0f), FVector(70.0f, 445.0f, 0.0f));

	// Door frames make both openings legible even before their interactive
	// leaves are spawned by the night-three director.
	for (const float DoorJambX : {-327.5f, -227.5f})
	{
		CreateBlock(
			FVector(DoorJambX, 220.0f, 1305.0f),
			FVector(10.0f, 12.0f, 210.0f),
			RailMetal);
	}
	CreateBlock(
		FVector(-277.5f, 220.0f, 1412.5f),
		FVector(110.0f, 12.0f, 15.0f),
		RailMetal);

	// 바탕은 사진에서 가져온 콘크리트다. 큰 파편을 바닥 그림에 반복해서
	// 찍지 않고 자재 주변에 얇은 메시로 놓는다. 쌓인 분진의 발소리는 유지한다.
	IGPrologueWorld::TagFootstepSurface(
		CreateBlock(
			FVector(0, 700, 1195), FVector(800, 500, 10),
			TexMat(TEXT("M_ConcreteDark_XY"), ConcreteDarkMaterial)),
		IGPrologueWorld::FootstepGypsumTag);
	// §11 V2 분진 퇴적: this is the one floor in the building deep enough in
	// plaster dust to hold a print. The field covers the slab exactly, so a
	// footfall in the stairwell or the corridor is reported and simply not drawn.
	if (!SettledDust)
	{
		SettledDust = NewObject<UIGSettledDustComponent>(
			this,
			TEXT("MissingFloorSettledDust"));
		if (SettledDust)
		{
			SettledDust->SetupAttachment(GetRootComponent());
			SettledDust->RegisterComponent();
		}
	}
	if (SettledDust)
	{
		constexpr float AnnexFloorTopZ = 1200.0f;
		SettledDust->ConfigureField(
			FBox(
				FVector(-400.0f, 450.0f, AnnexFloorTopZ),
				FVector(400.0f, 950.0f, AnnexFloorTopZ)),
			AnnexFloorTopZ);
	}
	CreateBlock(FVector(0, 700, 1445), FVector(800, 500, 10), AnnexCeiling);
	CreateBlock(FVector(0, 947.5f, 1320), FVector(800, 15, 240), AnnexWallX);
	// South wall split around the second 90 cm fire door (X=85..175).
	CreateBlock(FVector(-157.5f, 452.5f, 1320), FVector(485, 15, 240), AnnexWallX);
	CreateBlock(FVector(287.5f, 452.5f, 1320), FVector(225, 15, 240), AnnexWallX);
	CreateBlock(FVector(130.0f, 452.5f, 1422.5f), FVector(90, 15, 35), AnnexWallX);
	// 문 개구부의 절단면에는 그 면에 맞는 방향으로 석고 결을 붙인다.
	CreateBlock(FVector(85.05f, 452.5f, 1320), FVector(.1f, 15, 240), AnnexWallY, false);
	CreateBlock(FVector(174.95f, 452.5f, 1320), FVector(.1f, 15, 240), AnnexWallY, false);
	CreateBlock(FVector(130, 452.5f, 1404.95f), FVector(90, 15, .1f), AnnexCeiling, false);
	// Steel casings lapping the opening on the annex side. In the plane of the
	// wall they were thinner than it and disappeared into it completely.
	for (const float DoorJambX : {80.0f, 180.0f})
	{
		CreateBlock(
			FVector(DoorJambX, 466.0f, 1305.0f),
			FVector(10.0f, 12.0f, 210.0f),
			RailMetal);
	}
	CreateBlock(FVector(-397.5f, 700, 1320), FVector(15, 480, 240), AnnexWallY);
	CreateBlock(FVector(397.5f, 700, 1320), FVector(15, 480, 240), AnnexWallY);

	// 바깥에서 본 5층은 석고 상자가 아니라 경량 철골에 샌드위치 패널을 두른 옥탑
	// 증축이다(§1). 벽 블록은 안팎이 한 재질이라 안쪽 석고는 두고, 바깥면에서
	// 6 mm 떨어진 1 cm 외피를 붙인다. 패널 집이면 꼭 있는 모서리 캡과 처마
	// 후레싱, 문 위 물끊기도 두른다. 철문이 바깥으로 열려도 걸리지 않게 문설주
	// 후레싱은 두지 않는다. 조각끼리는 면으로만 맞닿고 겹치지 않는다.
	// 재질이 없으면 예전 석고 그대로 둔다.
	UMaterialInterface* AnnexPanel = TexMat(TEXT("M_AnnexPanel"), nullptr);
	if (AnnexPanel)
	{
		// 남쪽 외벽은 철문 개구부(X 85~175)를 비켜 세 장이다.
		CreateBlock(FVector(-160.0f, 444.4f, 1320.0f), FVector(490.0f, 1.0f, 240.0f), AnnexPanel, false);
		CreateBlock(FVector(290.0f, 444.4f, 1320.0f), FVector(230.0f, 1.0f, 240.0f), AnnexPanel, false);
		CreateBlock(FVector(130.0f, 444.4f, 1422.5f), FVector(90.0f, 1.0f, 35.0f), AnnexPanel, false);
		CreateBlock(FVector(-405.6f, 700.0f, 1320.0f), FVector(1.0f, 510.0f, 240.0f), AnnexPanel, false);
		CreateBlock(FVector(405.6f, 700.0f, 1320.0f), FVector(1.0f, 510.0f, 240.0f), AnnexPanel, false);
		CreateBlock(FVector(0.0f, 955.6f, 1320.0f), FVector(812.2f, 1.0f, 240.0f), AnnexPanel, false);
		// 옥상 쪽 두 모서리. 남쪽 외피 끝과 옆 외피 끝이 만나는 자리를 채운다.
		CreateBlock(FVector(-406.05f, 443.95f, 1320.0f), FVector(2.1f, 2.1f, 240.0f), AnnexPanel, false);
		CreateBlock(FVector(406.05f, 443.95f, 1320.0f), FVector(2.1f, 2.1f, 240.0f), AnnexPanel, false);
		// 처마 후레싱. 천장 슬래브 끝이 외벽보다 5 cm 들어가 있어 그 턱을 덮는다.
		CreateBlock(FVector(0.0f, 446.45f, 1446.0f), FVector(814.2f, 7.1f, 12.0f), AnnexPanel, false);
		CreateBlock(FVector(0.0f, 953.55f, 1446.0f), FVector(814.2f, 7.1f, 12.0f), AnnexPanel, false);
		CreateBlock(FVector(-403.55f, 700.0f, 1446.0f), FVector(7.1f, 500.0f, 12.0f), AnnexPanel, false);
		CreateBlock(FVector(403.55f, 700.0f, 1446.0f), FVector(7.1f, 500.0f, 12.0f), AnnexPanel, false);
		// 철문 위 물끊기.
		CreateBlock(FVector(130.0f, 442.9f, 1407.0f), FVector(102.0f, 2.0f, 4.0f), AnnexPanel, false);
	}

	// Three finished bays in a row: only the middle one hides a cavity, and
	// only sound can tell them apart. Gypsum faces with exposed stud edges.
	for (const float BayY : {560.0f, 700.0f, 840.0f})
	{
		UStaticMeshComponent* BayPanel = CreateBlock(
			FVector(257.5f, BayY, 1320),
			FVector(15, 120, 240),
			AnnexWallY);
		if (FMath::IsNearlyEqual(BayY, 700.0f))
		{
			MissingFloorCavityWallPanel = BayPanel;
		}
		CreateBlock(FVector(249.0f, BayY - 62.0f, 1320), FVector(4, 6, 240), Stud, false);
		CreateBlock(FVector(249.0f, BayY + 62.0f, 1320), FVector(4, 6, 240), Stud, false);
	}
	// The riser: two tonnes of water passing behind bay B on its way down.
	CreateBlock(
		FVector(310, 750, 1320), FVector(24, 24, 240),
		TexMat(TEXT("M_StainlessUV"), MetalFrameMaterial), false,
		CylinderMesh);

	// 12.5T 판재 사이에 0.3mm 틈을 둔다. 잘린 코어와 종이 겉지는 메시 재질을 쓴다.
	UInstancedStaticMeshComponent* BoardStack = NewObject<UInstancedStaticMeshComponent>(this, TEXT("AnnexGypsumStack"));
	BoardStack->SetupAttachment(ActiveParent.Get() ? ActiveParent.Get() : GetRootComponent());
	BoardStack->SetStaticMesh(PropMesh(TEXT("SM_GypsumCutBoard")));
	BoardStack->SetMobility(EComponentMobility::Static);
	BoardStack->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoardStack->SetCastShadow(false);
	BoardStack->SetAffectDistanceFieldLighting(false);
	BoardStack->SetAffectDynamicIndirectLighting(false);
	BoardStack->SetGenerateOverlapEvents(false);
	BoardStack->SetCanEverAffectNavigation(false);
	BoardStack->SetCullDistances(1400, 2000);
	BoardStack->SetComponentTickEnabled(false);
	const auto StackBoards = [this, BoardStack](FVector Base, FVector Footprint, int32 Count)
	{
		for (const float Side : {-.36f, 0.f, .36f})
		{
			CreateBlock(Base + FVector(Footprint.X * Side, 0, 6), FVector(10, Footprint.Y * .94f, 12),
				TexMat(TEXT("M_WoodFurnitureUV"), WoodMaterial));
		}
		// 판 사이 0.3mm 틈은 외형에만 남긴다. 충돌·그림자·거리장은 더미당 하나다.
		const float Height = Count * 1.28f - .03f;
		UStaticMeshComponent* Proxy = CreateBlock(Base + FVector(0, 0, 12.f + Height * .5f),
			FVector(Footprint.X, Footprint.Y, Height), SignWhiteMaterial, true);
		Proxy->SetRenderInMainPass(false);
		Proxy->SetRenderInDepthPass(false);
		Proxy->SetReceivesDecals(false);
		for (int32 Layer = 0; Layer < Count; ++Layer)
		{
			BoardStack->AddInstance(FTransform(FRotator::ZeroRotator,
				Base + FVector(0, 0, 12.625f + Layer * 1.28f), FVector(Footprint.X / 120.f, Footprint.Y / 80.f, 1.f)));
		}
	};
	StackBoards(FVector(0, 590, 1200), FVector(120, 80, 0), 40);
	StackBoards(FVector(-80, 780, 1200), FVector(140, 60, 0), 15);
	StackBoards(FVector(120, 880, 1200), FVector(90, 50, 0), 30);
	AddInstanceComponent(BoardStack);
	BoardStack->RegisterComponent();
	// 작은 파편은 한 번에 묶어 그린다. 플레이어와 발밑 추적을 막지 않는다.
	UInstancedStaticMeshComponent* Chips = NewObject<UInstancedStaticMeshComponent>(this, TEXT("AnnexGypsumChips"));
	Chips->SetupAttachment(ActiveParent.Get() ? ActiveParent.Get() : GetRootComponent());
	Chips->SetStaticMesh(PropMesh(TEXT("SM_GypsumChipCluster")));
	Chips->SetMobility(EComponentMobility::Static);
	Chips->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Chips->SetGenerateOverlapEvents(false);
	Chips->SetCanEverAffectNavigation(false);
	Chips->SetCastShadow(false);
	Chips->SetAffectDistanceFieldLighting(false);
	Chips->SetAffectDynamicIndirectLighting(false);
	Chips->SetCullDistances(900, 1400);
	Chips->SetComponentTickEnabled(false);
	const FVector ChipPlacements[] = {
		{-80, 538, 12}, {58, 646, 70}, {-178, 786, 134}, {-40, 837, 29},
		{184, 855, 201}, {218, 762, 83}, {207, 649, 245}, {-40, 496, 308},
		{-155, 575, 26}, {-244, 701, 170}, {80, 736, 113}, {2, 910, 257}
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ChipPlacements); ++Index)
	{
		const FVector& P = ChipPlacements[Index];
		const float Scale = .65f + (Index % 4) * .14f;
		Chips->AddInstance(FTransform(FRotator(0, P.Z, 0), FVector(P.X, P.Y, 1200.05f), FVector(Scale, Scale, 1)));
	}
	AddInstanceComponent(Chips);
	Chips->RegisterComponent();
	CreateBlock(FVector(0, 700, 1436), FVector(10, 10, 8), PlasticDarkMaterial, false);

	// 접힌 비닐 한 겹만 그린다. 상자 여섯 면의 반투명 중첩과 뜬 모서리를 없앤다.
	UMaterialInterface* Sheeting = TexMat(TEXT("M_ConstructionFilm"), GlassMaterial);
	auto AddSheeting = [this, Sheeting](
		const TCHAR* MeshName,
		const FVector& Center,
		const FVector& Size,
		const FRotator& Rotation)
	{
		if (UStaticMeshComponent* Film = CreateBlock(
				Center, Size, Sheeting, false, PropMesh(MeshName), Rotation))
		{
			Film->SetCastShadow(false);
			Film->SetCanEverAffectNavigation(false);
		}
	};
	// physics-audit: intentional ISM 판재 40장의 상단 Z=1263.17에 얹힌 보호 비닐.
	AddSheeting(
		TEXT("SM_ConstructionSheetDrape"), FVector(0, 590, 1263.17f), FVector(100), FRotator::ZeroRotator);
	AddSheeting(
		TEXT("SM_ConstructionSheetFloor"), FVector(-296, 842, 1200.05f), FVector(100), FRotator(0, 24, 0));
	AddSheeting(
		TEXT("SM_ConstructionSheetFloor"), FVector(-244, 878, 1200.12f), FVector(75, 72, 100), FRotator(0, -14, 0));
	// 스터드 앞에 걸친 비닐. XY 원본을 YZ 방향으로 세운다.
	AddSheeting(
		TEXT("SM_ConstructionSheetFloor"), FVector(246, 552, 1318), FVector(142.31f, 269.77f, 100), FRotator(0, 90, 90));

	// §14 CCTV 채널 5's camera, and it is a permanent fixture rather than part of
	// the beat: §17's payoff table requires the player to stand at this exact
	// vantage in 밤3 and recognise the framing they were shown in 밤2. If the
	// camera only existed while the channel was live, that recognition would have
	// nothing to land on. AIGCctvChannelFive reads MissingFloorCctvCamera's world
	// transform, so this transform *is* the shot — moving it moves both.
	// Corner mount flat to the ceiling, then a short arm out to the housing.
	const FVector CctvMount(-372.0f, 478.0f, 1438.0f);
	if (UStaticMeshComponent* MountPlate = CreateBlock(
			CctvMount + FVector(0.0f, 0.0f, 1.0f),
			FVector(16.0f, 16.0f, 2.0f),
			RailMetal,
			false))
	{
		MountPlate->SetCanEverAffectNavigation(false);
	}
	const FVector CctvArm = IGPrologueWorld::CctvCameraLocation - CctvMount;
	if (UStaticMeshComponent* Bracket = CreateBlock(
			CctvMount + CctvArm * 0.5f,
			FVector(CctvArm.Size(), 2.6f, 2.6f),
			RailMetal,
			false,
			nullptr,
			CctvArm.Rotation()))
	{
		Bracket->SetCanEverAffectNavigation(false);
	}
	MissingFloorCctvCamera = CreateBlock(
		IGPrologueWorld::CctvCameraLocation,
		FVector(15.0f, 9.0f, 8.5f),
		PlasticDarkMaterial,
		false,
		nullptr,
		IGPrologueWorld::CctvCameraRotation);
	if (MissingFloorCctvCamera)
	{
		MissingFloorCctvCamera->SetCanEverAffectNavigation(false);
		// The camera's own illuminator, and it is permanent for the same reason
		// the housing is: a light that only exists while the channel is live would
		// make 밤3's vantage a different place than the one 밤2 showed. Every
		// corridor camera of this class has an IR ring, and it is what gives the
		// shot its shape — near material readable, the far end of the room gone.
		// Deliberately not in MissingFloorAnnexLights: the night-four breaker cuts
		// the ceiling bulb, and this runs off the camera's own supply.
		CreateLight(
			IGPrologueWorld::CctvCameraLocation
				+ IGPrologueWorld::CctvCameraRotation.Vector() * 12.0f,
			2400.0f,
			640.0f,
			FLinearColor(0.62f, 0.66f, 0.72f),
			false);
		// The lens housing, so the thing reads as pointed rather than as a box.
		CreateBlock(
			IGPrologueWorld::CctvCameraLocation
				+ IGPrologueWorld::CctvCameraRotation.Vector() * 9.0f,
			FVector(5.0f, 5.5f, 5.5f),
			TexMat(TEXT("M_StainlessUV"), MetalFrameMaterial),
			false,
			CylinderMesh,
			IGPrologueWorld::CctvCameraRotation + FRotator(90.0f, 0.0f, 0.0f))
			->SetCanEverAffectNavigation(false);
	}
	// §5.5 물리적 확인의 나머지 절반. One coax leaves this camera and never
	// reaches the NVR: it runs the ceiling to the stair core and drops straight
	// to the spare BNC input on the booth monitor. The channel is visible and
	// unrecorded because of this cable, and the cable is here to be followed.
	const FVector CoaxRun[] = {
		CctvMount + FVector(0.0f, 0.0f, 0.2f),
		FVector(-372.0f, 466.0f, 1438.2f),
		FVector(-300.0f, 464.0f, 1438.2f),
		FVector(-277.5f, 461.0f, 1438.2f),
	};
	for (int32 CoaxIndex = 0; CoaxIndex + 1 < UE_ARRAY_COUNT(CoaxRun); ++CoaxIndex)
	{
		const FVector Start = CoaxRun[CoaxIndex];
		const FVector End = CoaxRun[CoaxIndex + 1];
		const FVector Delta = End - Start;
		const float Length = Delta.Size();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			continue;
		}
		if (UStaticMeshComponent* Coax = CreateBlock(
				(Start + End) * 0.5f,
				FVector(Length, 1.1f, 1.1f),
				PlasticDarkMaterial,
				false,
				nullptr,
				Delta.Rotation()))
		{
			Coax->SetCastShadow(false);
			Coax->SetCanEverAffectNavigation(false);
		}
	}

	// ImageGen residue arrives as value masks, never as baked lighting. Thin
	// masked receivers sit on the real wall/floor planes so the flashlight,
	// contact angle and PBR surface underneath stay coherent. None collides.
	auto AddResidue = [this](
		const FVector& Center,
		const FVector& Size,
		const FName MaterialName,
		const FRotator& Rotation) -> UStaticMeshComponent*
	{
		if (UStaticMeshComponent* Residue = CreateBlock(
				Center,
				Size,
				TexMat(MaterialName, SignWhiteMaterial),
				false,
				nullptr,
				Rotation))
		{
			Residue->SetCastShadow(false);
			Residue->SetCanEverAffectNavigation(false);
			return Residue;
		}
		return nullptr;
	};
	// 손바닥의 압흔은 실제 손 크기로 붙인다. 상자의 옆면에 인쇄가 늘어나던
	// 테두리는 없애고, 벽이 열릴 때 함께 숨길 수 있는 평면 하나만 둔다.
	MissingFloorCavityWallResidue = CreateBlock(
		FVector(249.65f, 700.0f, 1332.0f), FVector(48, 44, 100),
		TexMat(TEXT("M_AnnexPressure"), SignWhiteMaterial), false,
		PlaneMesh, FRotator(0, 90, -90));
	AddResidue(
		FVector(20.0f, 718.0f, 1200.25f),
		FVector(250.0f, 112.0f, 0.30f),
		TEXT("M_MissingFloorDragTrails"),
		FRotator(0.0f, 13.0f, 0.0f));
	// 분진 이음은 열린 바닥 가운데가 아니라 벽과 바닥이 만나는 선에 쌓인다.
	// 북쪽 벽(Y=940) 아래로 붙여 길고 얇게 눕히면 이음으로 읽히고, 끌림 자국과
	// 나란해진다 — 앞서는 이 둘이 열린 바닥에서 서로 교차해, 카메라에서 보면
	// 칠해 놓은 X 한 개로 합쳐졌다. 마스크의 대각선이 가로세로 비에 따라 눕는
	// 각도가 달라지므로, 같은 계열의 잔흔을 다른 요각으로 겹쳐 두면 안 된다.
	AddResidue(
		FVector(150.0f, 906.0f, 1200.26f),
		FVector(268.0f, 42.0f, 0.32f),
		TEXT("M_MissingFloorDustJoint"),
		FRotator(0.0f, 0.0f, 0.0f));
	// 자국은 공동의 뒷벽에 붙인다. 앞쪽 석고판이 떨어져도 빈 공간에 인쇄가 남지 않는다.
	AddResidue(
		FVector(389.65f, 700.0f, 1322.0f),
		FVector(0.28f, 104.0f, 150.0f),
		TEXT("M_MissingFloorCavityScratches"),
		FRotator::ZeroRotator);
	MissingFloorAnnexLights.Add(CreateLight(
		FVector(0, 700, 1420), 650.0f, 900.0f,
		FLinearColor(1.0f, 0.93f, 0.82f), false));
	// The upper flight is deliberately dim, but it must still read as fourteen
	// grounded treads rather than a black transition volume. A cold bulkhead
	// spill also silhouettes the first real roof door from the fourth floor.
	// 4층에서 보이는 등이라 별관과 따로 센다.
	BuildingLightZone = EIGLightZone::UpperStair;
	MissingFloorAnnexLights.Add(CreateLight(
		FVector(-277.5f, 92.0f, 1358.0f), 820.0f, 470.0f,
		FLinearColor(0.50f, 0.61f, 0.76f), true, 8.0f));
	BuildingLightZone = EIGLightZone::Annex;
	MissingFloorAnnexLights.Add(CreateLight(
		FVector(-35.0f, 224.0f, 1450.0f), 760.0f, 760.0f,
		FLinearColor(0.48f, 0.58f, 0.72f), true, 12.0f));
	// A separate battery emergency practical is deliberately not inserted into
	// MissingFloorAnnexLights. When Mok cuts the annex breaker, this weak red
	// pool survives and blends with the player's warm flashlight instead of
	// reducing the finale to featureless black.
	CreateBlock(
		FVector(-389.0f, 700.0f, 1370.0f),
		FVector(2.0f, 18.0f, 34.0f),
		SnackRedMaterial,
		false);
	if (UPointLightComponent* EmergencyPractical = CreateLight(
		FVector(-356.0f, 700.0f, 1355.0f),
		165.0f,
		430.0f,
		FLinearColor(1.0f, 0.055f, 0.018f),
		false))
	{
		EmergencyPractical->SetSourceRadius(5.0f);
		EmergencyPractical->SetVolumetricScatteringIntensity(0.14f);
	}

	ActiveParent = nullptr;
}

void AIGPrologueWorldScene::SetUnit403AgeStage(const int32 Stage)
{
	const int32 Clamped = FMath::Clamp(Stage, 0, 2);
	if (Unit403AgeStage == Clamped)
	{
		return;
	}
	Unit403AgeStage = Clamped;
	ApplyUnit403AgeStage();
}

void AIGPrologueWorldScene::ApplyUnit403AgeStage()
{
	// Cumulative, not exclusive: stage two keeps stage one's cracks and adds to
	// them. Damage does not move house.
	for (const TObjectPtr<UStaticMeshComponent>& Plane : Unit403AgeStageOne)
	{
		if (Plane)
		{
			Plane->SetHiddenInGame(Unit403AgeStage < 1);
		}
	}
	for (const TObjectPtr<UStaticMeshComponent>& Plane : Unit403AgeStageTwo)
	{
		if (Plane)
		{
			Plane->SetHiddenInGame(Unit403AgeStage < 2);
		}
	}
}

int32 AIGPrologueWorldScene::GetUnit403AgingPlaneCount() const
{
	int32 Count = 0;
	if (Unit403AgeStage >= 1)
	{
		Count += Unit403AgeStageOne.Num();
	}
	if (Unit403AgeStage >= 2)
	{
		Count += Unit403AgeStageTwo.Num();
	}
	return Count;
}

void AIGPrologueWorldScene::SetTheHourSealed(const bool bSealed)
{
	bTheHourSealed = bSealed;
	if (PostProcess)
	{
		// 밤에도 먼지 자국과 문 아래 빛이 노이즈에 묻히지 않게 한다.
		PostProcess->Settings.FilmGrainIntensity = 0.0f;
		PostProcess->Settings.AutoExposureMaxBrightness = bSealed ? 1.30f : 5.0f;
		// §11 V1: 자동노출 하한 잠금. Capping the ceiling alone still let the
		// histogram adapt *down* into an unlit corridor and quietly hand the
		// player night vision — which is exactly the currency the torch is
		// supposed to be. Pinning the floor to the ceiling freezes exposure for
		// the whole hour, so the dark stays as dark as it was authored and the
		// beam is the only thing that reveals anything.
		PostProcess->Settings.AutoExposureMinBrightness = bSealed ? 1.30f : -0.5f;
	}
	ApplyNightAtmosphere(bSealed);

	// The 공동현관. Shut the leaf first: a swing door only consults its
	// requirements while closed, so sealing an open door is a no-op and the
	// player strolls out. ForceOpenState is instant and tick-free, which also
	// dodges the proximity auto-reopen a scripted swing would expose.
	if (BuildingDoor)
	{
		if (bSealed)
		{
			BuildingDoor->ForceOpenState(false);
			TArray<FIGDoorRequirement> SealRequirements;
			FIGDoorRequirement& Seal = SealRequirements.AddDefaulted_GetRef();
			// Deliberately a state that is never granted during the hour. The
			// release below drops the requirement instead of granting the tag,
			// so no 'sealed' fact can ever be written into a save.
			Seal.RequiredState = FGameplayTag::RequestGameplayTag(
				FName(TEXT("State.MissingFloor.Night.MorningCame")),
				false);
			Seal.LockedPrompt = NSLOCTEXT(
				"IGMissingFloor", "EntranceSealedPrompt", "공동현관 열어 보기");
			// '그 시간'의 규칙 1(§1)은 게임 안에서 여기서 배운다. 문은 잠긴 게 아니라
			// 붙들려 있다 — 걸쇠 소리 없이 밀리다 선다. 폰은 그 뒤에 따로 운다.
			// 그레이박스 디렉터가 첫 시도에 통화 실패음을 내고 「폰도 안 터진다.」를
			// 잇는다. 밤에 폰이 죽어 있어야 05:30의 신고가 그날 처음 잡힌 신호로 읽힌다.
			Seal.LockedThought = NSLOCTEXT(
				"IGMissingFloor",
				"EntranceSealedThought",
				"…안 열리네. 잠긴 것도 아닌데.");
			Seal.bHeldShut = true;
			BuildingDoor->SetRequirements(MoveTemp(SealRequirements));
		}
		else
		{
			TArray<FIGDoorRequirement> NoRequirements;
			BuildingDoor->SetRequirements(MoveTemp(NoRequirements));
		}
	}

	// 그 시간의 승강기는 죽어 있다. 프롬프트까지 지우면 고장인지 규칙인지 알 수
	// 없어서, 버튼은 눌리되 불이 안 들어오고 층 표시도 꺼진다(SetHourDead).
	if (Elevator)
	{
		Elevator->SetInteractionEnabled(true);
		Elevator->SetHourDead(bSealed);
		if (!bSealed)
		{
			Elevator->ResetForNewRide();
		}
	}

	// The second seal, without which the stairs deliver the player into the
	// open car park and out to the alley.
	if (StairCoreNightGate)
	{
		StairCoreNightGate->SetHiddenInGame(!bSealed);
		StairCoreNightGate->SetCollisionEnabled(
			bSealed
				? ECollisionEnabled::QueryAndPhysics
				: ECollisionEnabled::NoCollision);
		StairCoreNightGate->SetCollisionProfileName(
			bSealed
				? UCollisionProfile::BlockAll_ProfileName
				: UCollisionProfile::NoCollision_ProfileName);
	}
}

bool AIGPrologueWorldScene::ValidateMissingFloorRooftopRoute(
	float& OutCenterlineLengthCentimeters,
	int32& OutUpperStepCount) const
{
	OutCenterlineLengthCentimeters =
		FVector::Dist2D(
			IGPrologueWorld::MissingFloorRouteStart,
			IGPrologueWorld::MissingFloorRouteCorner)
		+ FVector::Dist2D(
			IGPrologueWorld::MissingFloorRouteCorner,
			IGPrologueWorld::MissingFloorRouteEnd);
	OutUpperStepCount = MissingFloorUpperStairSteps.Num();

	if (!FMath::IsNearlyEqual(
			OutCenterlineLengthCentimeters,
			IGPrologueWorld::MissingFloorRouteLengthCentimeters,
			0.1f)
		|| OutUpperStepCount != IGPrologueWorld::MissingFloorUpperStepCount
		|| MissingFloorRooftopRouteFloors.Num() != 3)
	{
		return false;
	}

	auto HasWalkableCollision = [](const UStaticMeshComponent* Component)
	{
		return Component
			&& Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision
			&& Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
	};
	for (const UStaticMeshComponent* Step : MissingFloorUpperStairSteps)
	{
		if (!HasWalkableCollision(Step))
		{
			return false;
		}
	}
	for (const UStaticMeshComponent* Floor : MissingFloorRooftopRouteFloors)
	{
		if (!HasWalkableCollision(Floor))
		{
			return false;
		}
	}
	return true;
}

FVector AIGPrologueWorldScene::GetMissingFloorCctvCameraLocation() const
{
	return IGPrologueWorld::CctvCameraLocation;
}

FRotator AIGPrologueWorldScene::GetMissingFloorCctvCameraRotation() const
{
	return IGPrologueWorld::CctvCameraRotation;
}

float AIGPrologueWorldScene::GetMissingFloorCctvFieldOfView() const
{
	return IGPrologueWorld::CctvCameraFieldOfView;
}

void AIGPrologueWorldScene::SetMissingFloorAnnexPower(const bool bPowered)
{
	bMissingFloorAnnexPowered = bPowered;
	// 끄는 것은 바로 한다. 켜는 것은 조명 구역이 정한다 — 별관과 윗계단 등은
	// 아래층에 있는 동안 구역이 꺼 두었으니, 전원이 돌아와도 그 층에서는 켜지 않는다.
	if (!bPowered)
	{
		for (UPointLightComponent* Light : MissingFloorAnnexLights)
		{
			if (Light)
			{
				Light->SetVisibility(false, true);
			}
		}
	}
	ActiveLightBand = -1;
	UpdateLightZones();
}

bool AIGPrologueWorldScene::OpenMissingFloorCavity()
{
	if (!MissingFloorCavityWallPanel)
	{
		return false;
	}
	MissingFloorCavityWallPanel->SetVisibility(false, true);
	MissingFloorCavityWallPanel->SetHiddenInGame(true, true);
	MissingFloorCavityWallPanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (MissingFloorCavityWallResidue)
	{
		// The receiver belongs to the removable face. Leaving it visible after
		// the gypsum disappears would create a pair of handprints floating over
		// the cavity — exactly the kind of physical break this reveal cannot bear.
		MissingFloorCavityWallResidue->SetVisibility(false, true);
		MissingFloorCavityWallResidue->SetHiddenInGame(true, true);
	}
	bMissingFloorCavityOpen = true;
	return true;
}

bool AIGPrologueWorldScene::ResetMissingFloorCavity()
{
	if (!MissingFloorCavityWallPanel)
	{
		return false;
	}
	MissingFloorCavityWallPanel->SetVisibility(true, true);
	MissingFloorCavityWallPanel->SetHiddenInGame(false, true);
	MissingFloorCavityWallPanel->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (MissingFloorCavityWallResidue)
	{
		MissingFloorCavityWallResidue->SetVisibility(true, true);
		MissingFloorCavityWallResidue->SetHiddenInGame(false, true);
	}
	bMissingFloorCavityOpen = false;
	return true;
}

bool AIGPrologueWorldScene::DropCorridorExtinguisher()
{
	if (!CorridorExtinguisher || bCorridorExtinguisherDropped)
	{
		return false;
	}
	bCorridorExtinguisherDropped = true;

	CorridorExtinguisher->SetSimulatePhysics(true);
	// A shove at the neck, toward the walkway, so the cylinder tips into the
	// player's path instead of rolling into the wall.
	const FVector Neck =
		CorridorExtinguisher->GetComponentLocation() + FVector(0.0f, 0.0f, 20.0f);
	CorridorExtinguisher->AddImpulseAtLocation(
		FVector(0.0f, 260.0f, 40.0f) * CorridorExtinguisher->GetMass(),
		Neck);

	// Physics only runs for the fall itself: once the clatter has settled the
	// body freezes where it lies and costs nothing again.
	GetWorldTimerManager().SetTimer(
		ExtinguisherSettleTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (CorridorExtinguisher)
			{
				CorridorExtinguisher->SetSimulatePhysics(false);
			}
		}),
		4.0f,
		false);
	return true;
}

FVector AIGPrologueWorldScene::GetCorridorExtinguisherLocation() const
{
	return CorridorExtinguisher
		? CorridorExtinguisher->GetComponentLocation()
		: GetActorTransform().TransformPosition(FVector(232.0f, -364.0f, 926.0f));
}

FVector AIGPrologueWorldScene::GetBoilerCupboardLocation()
{
	// 벽장 앞면에서 복도 쪽으로 조금 나온 자리. 험은 기계에서 나오지
	// 벽 속에서 나오지 않는다.
	return FVector(
		IGPrologueWorld::BoilerCupboardX,
		IGPrologueWorld::BoilerCupboardFaceY,
		IGPrologueWorld::FourthFloorZ + IGPrologueWorld::BoilerCupboardZ);
}

FVector AIGPrologueWorldScene::GetPlayerStartLocation()
{
	return IGPrologueWorld::PlayerLocation;
}

FVector AIGPrologueWorldScene::GetFridgeLocation() const
{
	// 프롭이 세워지기 전에 물어보는 자리가 있어 작성 좌표를 함께 둔다.
	// 스폰도 이 값을 쓰므로 둘은 갈라질 수 없다.
	return Fridge
		? Fridge->GetActorLocation()
		: IGPrologueWorld::FridgeLocation;
}

float AIGPrologueWorldScene::ComputeListenerNearness()
{
	if (!bTheHourSealed || !DegradedCorridorLight || !GetWorld())
	{
		return 0.0f;
	}
	if (!CachedListener.IsValid() && GetWorld()->GetTimeSeconds() >= NextListenerSearchSeconds)
	{
		NextListenerSearchSeconds = GetWorld()->GetTimeSeconds() + 1.0;
		for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
		{
			CachedListener = *It;
			break;
		}
	}
	const AIGListenerEntity* Listener = CachedListener.Get();
	if (!Listener || Listener->IsDormant() || Listener->IsHidden())
	{
		return 0.0f;
	}
	constexpr float Reach = 650.0f;
	const float Distance = FVector::Dist(
		Listener->GetActorLocation(), DegradedCorridorLight->GetComponentLocation());
	float Near = FMath::Clamp(1.0f - Distance / Reach, 0.0f, 1.0f);
	if (Distance < Reach * 1.3f)
	{
		const EIGListenerState State = Listener->GetListenerState();
		if (State == EIGListenerState::Banging)
		{
			Near = FMath::Min(1.0f, Near + 0.35f);
		}
		else if (State == EIGListenerState::Chasing)
		{
			Near = FMath::Min(1.0f, Near + 0.5f);
		}
	}
	return Near;
}

void AIGPrologueWorldScene::HandleCorridorFlicker()
{
	// A chapter that owns the corridor lighting suspends this. The timer runs
	// for the whole session at 10 Hz and writes the intensity unconditionally,
	// so without the gate anything else done to the west fixture is undone
	// within a tenth of a second.
	if (bCorridorFlickerSuspended || !bCommonInspectionLightsEnabled || !DegradedCorridorLight)
	{
		return;
	}

	// 떨림 자체(0.86~1.06)는 어두운 쪽이 충분히 밝아 점멸로 치지 않는다.
	// 5% 확률로 0.12까지 떨어지는 드롭아웃은 점멸이다. 점멸 감소를 켠 사람은
	// 낡은 형광등 아래를 걷되 그 떨어짐은 겪지 않아야 한다.
	const bool bReducedFlicker = GetGameInstance()
		&& GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>()
		&& GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>()
			->IsReducedFlickerEnabled();
	const float NightScale = NightFixtureScale(0, true);
	if (bReducedFlicker)
	{
		DegradedCorridorLight->SetIntensity(
			DegradedLightBaseIntensity * 0.94f * NightScale);
		return;
	}

	uint32 Hash = ++FlickerHashCounter * 2654435761u;
	Hash ^= Hash >> 15;
	const float Uniform = (Hash & 0xFFFF) / 65535.0f;
	const float DropoutRoll = ((Hash >> 16) & 0xFFFF) / 65535.0f;
	// 밤에는 낙하가 잦고 더 깊다. 안정기가 다 된 등은 어둠 속에서 더 자주 죽는다.
	// 그가 등 가까이 오면 더 자주, 더 깊이 죽는다. 두드리거나 쫓는 동안은 더하다.
	// 같은 규칙이 매번 같게 작동해야 플레이어가 등을 보고 거리를 읽는다.
	const float Near = ComputeListenerNearness();
	const float DropoutChance = (bTheHourSealed ? 0.09f : 0.05f) + 0.30f * Near;
	const float DropoutFloor = (bTheHourSealed ? 0.06f : 0.12f) * (1.0f - 0.8f * Near);
	const float Multiplier =
		DropoutRoll < DropoutChance ? DropoutFloor : (0.86f + 0.20f * Uniform);
	DegradedCorridorLight->SetIntensity(
		DegradedLightBaseIntensity * Multiplier * NightScale);
}

void AIGPrologueWorldScene::BuildLobby()
{
	// Ground-floor lobby: elevator on the east wall, mailboxes on the north,
	// and the framed-glass common entrance opening south onto the porch.
	UMaterialInterface* LobbyFloor = TexMat(TEXT("M_GraniteTile_XY"), StoreFloorMaterial);
	UMaterialInterface* LobbyWallX = TexMat(TEXT("M_Stucco_X"), ConcreteMaterial);
	UMaterialInterface* LobbyWallY = TexMat(TEXT("M_Stucco_Y"), ConcreteMaterial);
	UMaterialInterface* LobbyCeil = TexMat(TEXT("M_StuccoCeil"), ConcreteMaterial);
	UMaterialInterface* Skirting = TexMat(TEXT("M_UtilityGraniteCladding"), ConcreteDarkMaterial);
	UMaterialInterface* Stainless = TexMat(TEXT("M_StainlessUV"), MetalFrameMaterial);
	UMaterialInterface* Metal = TexMat(TEXT("M_MetalUV"), MetalFrameMaterial);
	UMaterialInterface* ShelfSteel = TexMat(TEXT("M_ShelfSteelUV"), CoolerBodyMaterial);

	// 승강기 앞은 대기·회전 공간으로 비운다. 안쪽 폭 230cm, 천장 높이 240cm.
	CreateBlock(FVector(580, -260, -10), FVector(300, 270, 20), LobbyFloor);
	CreateBlock(FVector(580, -257.5f, 250), FVector(300, 265, 20), LobbyCeil);
	CreateBlock(FVector(580, -135, 120), FVector(300, 20, 240), LobbyWallX);
	// Split the west wall around a real stair-core doorway. The old solid
	// slab forced a player standing inside the lobby to walk back outdoors
	// and around the pilotis before the lower stair mouth became reachable.
	CreateBlock(FVector(440, -370, 120), FVector(20, 30, 240), LobbyWallY);
	CreateBlock(FVector(440, -200, 120), FVector(20, 110, 240), LobbyWallY);
	CreateBlock(FVector(440, -305, 225), FVector(20, 100, 30), LobbyWallY);
	// Enclosed ground-floor connector from the lobby doorway to the stair
	// core. The core is vertically aligned with the west end of the 4F hall;
	// leaving this span open made the new doorway lead into a five-metre void.
	CreateBlock(FVector(170.5f, -305, -10), FVector(519, 120, 20), LobbyFloor);
	CreateBlock(FVector(170.5f, -305, 250), FVector(519, 120, 20), LobbyCeil);
	CreateBlock(FVector(170.5f, -375, 120), FVector(519, 20, 240), LobbyWallX);
	// 관리실은 한 손에 물건을 든 채 드나드는 곳이다. 진입 폭 110cm를 확보한다.
	CreateBlock(FVector(10.5f, -235, 120), FVector(199, 20, 240), LobbyWallX);
	CreateBlock(FVector(325, -235, 120), FVector(210, 20, 240), LobbyWallX);
	CreateBlock(FVector(165, -235, 225), FVector(110, 20, 30), LobbyWallX);
	if (UStaticMesh* BoothFrame = PropMesh(TEXT("SM_UnitDoorFrameWide")))
	{
		CreateBlock(FVector(165, -243, 0), FVector(100, 100, 100), nullptr, false, BoothFrame);
	}

	// 없는 층: the management booth, tucked behind the connector's north wall
	// where Korean villas actually put it — beside the way in. Mok Hansu's
	// daytime post and the night's P2 stage: the complaint ledger, the carbon
	// ledger underneath it, the CCTV monitor with one channel too many, and
	// the inner room whose door edge shows the egg-crate foam
	// (STORY_BIBLE_MISSING_FLOOR.md §8 밤2).
	CreateBlock(FVector(170, -160, -10), FVector(240, 170, 20), LobbyFloor);
	CreateBlock(FVector(170, -155, 250), FVector(240, 160, 20), LobbyCeil);
	CreateBlock(FVector(170, -77.5f, 120), FVector(240, 15, 240), LobbyWallX);
	CreateBlock(FVector(52.5f, -155, 120), FVector(15, 140, 240), LobbyWallY);
	CreateBlock(FVector(287.5f, -155, 120), FVector(15, 140, 240), LobbyWallY);
	// 자재 보관 벽의 작업조끼. 아래 판재와 떨어뜨리고 서쪽 배관·조작반은 비운다.
	if (UStaticMesh* Vest = PropMesh(TEXT("SM_HangingWorkVest")))
	{
		UStaticMeshComponent* Visual = CreateBlock(FVector(279.8f,-155,130), FVector(100), nullptr,
			false, Vest, FRotator(0,90,0));
		if (Visual) Visual->ComponentTags.Add(TEXT("Visual.WorkVest"));
	}
	// 상판 아래가 빈 사무용 책상. 받침대와 녹화기, 장부의 자리를 따로 둔다.
	CreateBlock(FVector(160, -110, 74.5f), FVector(110, 55, 3), ShelfSteel);
	for (float LegX : {110.0f, 210.0f})
	{
		for (float LegY : {-132.5f, -87.5f})
			CreateBlock(FVector(LegX, LegY, 36.5f), FVector(3.5f, 3.5f, 73), ShelfSteel);
	}
	CreateBlock(FVector(160, -87.5f, 24), FVector(96.5f, 2, 4), ShelfSteel, false);
	// 배관 밸브가 토출관과 맞물리는 몸체와 축이다.
	CreateBlock(FVector(75, -170, 52), FVector(9, 9, 8), ShelfSteel, false, CylinderMesh);
	CreateBlock(FVector(75, -177, 52), FVector(2, 2, 12), ShelfSteel, false, CylinderMesh, FRotator(0, 0, 90));
	CreateProp(TEXT("SM_BoothRecorder"), FVector(150, -100, 76), nullptr, 0, 1, false);
	CreateProp(TEXT("SM_BoothMonitor"), FVector(150, -100, 80.5f), nullptr, 0, 1, false);
	// 일반 네 채널은 실제 맵에서 구운 화면을 쓴다. 외부 입력 5번만 순간 렌더한다.
	CreateBlock(FVector(150, -105.2f, CctvScreenCenterZ), FVector(CctvScreenWidth, CctvScreenHeight, 100),
		TexMat(TEXT("M_CctvStandby"), ScreenGlowMaterial), false,
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")), FRotator(0, 180, 90));
	// The inner room's door leaf, always shut: a dark slab with a hairline
	// gap the foam reveal peers through. It never opens — that is the point.
	CreateBlock(FVector(240, -84.5f, 105), FVector(70, 5, 210), PlasticDarkMaterial, false);
	CreateBlock(FVector(272, -84, 105), FVector(2.4f, 3, 200), ConcreteDarkMaterial, false);
	// Booth ceiling lamp block so the room is not a cave in the day section.
	CreateBlock(FVector(170, -150, 237), FVector(24, 24, 4), PlasticDarkMaterial, false);
	CreateLight(FVector(170, -150, 226), 900.0f, 420.0f,
		FLinearColor(1.0f, 0.95f, 0.85f), false);
	// 벽에 기댄 절단재. 로컬 XY 판재를 세우고 바닥에 닿는 높이를 맞춘다.
	CreateBlock(
		FVector(267, -164, 59.74f), FVector(75, 150, 100), nullptr,
		true, PropMesh(TEXT("SM_GypsumCutBoard")), FRotator(0, 90, 96));
	CreateBlock(
		FVector(254, -166, 39.61f), FVector(50, 100, 100), nullptr,
		true, PropMesh(TEXT("SM_GypsumCutBoard")), FRotator(0, 90, 99));
	CreateProp(TEXT("SM_WorkPaintCan"), FVector(230, -198, 0), nullptr, -60, 1, true);

	// Granite skirting round the lobby, matching the landings upstairs.
	CreateBlock(FVector(575, -146.75f, 6), FVector(250, 3.5f, 12), Skirting, false);
	CreateBlock(FVector(451.75f, -200, 6), FVector(3.5f, 110, 12), Skirting, false);

	// Street wall of the lobby, with the common-entrance opening at X 600..686.
	// West of X 450 the ground floor is the open pilotis car park, which
	// BuildAlley puts in — Korean villas give the whole ground level to
	// parking and recess the entrance into it.
	CreateBlock(FVector(525, -385, 120), FVector(150, 20, 240), LobbyWallX);
	CreateBlock(FVector(703, -385, 120), FVector(34, 20, 240), LobbyWallX);
	CreateBlock(FVector(643, -385, 225), FVector(86, 20, 30), LobbyWallX);

	// East wall around the elevator opening (Y -360..-250).
	CreateBlock(FVector(710, -197.5f, 120), FVector(20, 105, 240), LobbyWallY);
	CreateBlock(FVector(710, -367.5f, 120), FVector(20, 15, 240), LobbyWallY);
	CreateBlock(FVector(710, -305, 225), FVector(20, 110, 30), LobbyWallY);
	// 우편을 꺼내는 자리는 통과 동선에서 한 걸음 물러난 뒤쪽 벽이다.
	CreateProp(TEXT("SM_MailboxUnit"), FVector(508, -145, 106), nullptr, 0, 1, false);

	// 검침함의 열린 창 안에 실제 계기를 넣는다. 계수기 아래의 원판은 수평이다.
	CreateProp(TEXT("SM_MeterCabinetFive"), FVector(506, -365, 150), nullptr, 180, 1, false);
	UtilityMeterDiscs.Reset();
	UtilityMeterPhases.Init(0.f, 5);
	for (int32 MeterIndex = 0; MeterIndex < 5; ++MeterIndex)
	{
		const float MeterX = 470.0f + MeterIndex * 18.0f;
		CreateProp(TEXT("SM_InductionMeter"), FVector(MeterX, -373.3f, 140.5f), nullptr, 180, 1, false);
		UStaticMeshComponent* Disc = CreateProp(TEXT("SM_MeterRotor"), FVector(MeterX, -369.1f, 150.1f), nullptr, 0, 1, false);
		if (Disc)
		{
			Disc->SetMobility(EComponentMobility::Movable);
			Disc->SetCastShadow(false);
			UtilityMeterDiscs.Add(Disc);
			if (MeterIndex == 4) { FifthMeterDisc = Disc; }
		}
		UMaterialInstanceDynamic* CounterPrint = UMaterialInstanceDynamic::Create(
			TexMat(TEXT("M_UtilityMeterCounter"), SignWhiteMaterial), this);
		UMaterialInstanceDynamic* LabelPrint = UMaterialInstanceDynamic::Create(
			TexMat(TEXT("M_UtilityMeterLabel"), SignWhiteMaterial), this);
		CounterPrint->SetScalarParameterValue(TEXT("MeterIndex"), MeterIndex);
		LabelPrint->SetScalarParameterValue(TEXT("MeterIndex"), MeterIndex);
		CreateBlock(FVector(MeterX, -367.0f, 158.3f), FVector(7.25f, .01f, 2.25f), CounterPrint, false);
		CreateBlock(FVector(MeterX, -364.75f, 133), FVector(15, .04f, 6), LabelPrint, false);
	}

	// 배선을 가린 함체와 회전 손잡이는 분리한다. 인쇄된 그림 위에 큐브를 얹지 않는다.
	if (UStaticMeshComponent* Panel = CreateProp(TEXT("SM_LobbyCircuitPanel"), FVector(576,-372,152), nullptr, 180, 1, false))
	{
		Panel->SetCullDistance(1800.f);
	}
	if (UStaticMeshComponent* Print = CreateProp(TEXT("SM_CircuitPanelPrints"), FVector(576,-372,152), nullptr, 180, 1, false))
	{
		Print->SetCastShadow(false);
		Print->bAffectDistanceFieldLighting = false;
		Print->SetCullDistance(900.f);
	}
	for (const float BreakerX : {569.4f, 582.7f})
	{
		for (const float BreakerZ : {166.f, 151.f, 136.f})
		{
			if (BreakerX == 582.7f && BreakerZ == 136.f) continue;
			UStaticMeshComponent* Toggle = CreateProp(TEXT("SM_CircuitToggle"), FVector(BreakerX,-367.2f,BreakerZ), nullptr, 180, 1, false);
			if (!Toggle) continue;
			const bool bUnnamed = BreakerX == 569.4f && BreakerZ == 136.f;
			const bool bCommon = BreakerX == 582.7f && BreakerZ == 151.f;
			// 등록된 정적 컴포넌트는 회전을 거부한다. 초기 자세를 먼저 정하고 고정한다.
			Toggle->SetMobility(EComponentMobility::Movable);
			Toggle->SetRelativeRotation(FRotator(0,180,bUnnamed ? -32.f : 32.f));
			if (bUnnamed || bCommon)
			{
				if (bUnnamed) UnnamedBreakerToggle = Toggle;
				else CommonBreakerToggle = Toggle;
			}
			else Toggle->SetMobility(EComponentMobility::Static);
			Toggle->SetCastShadow(false);
			Toggle->bAffectDistanceFieldLighting = false;
			Toggle->SetCullDistance(650.f);
		}
	}

	// 기록지와 계량기를 같은 벽에서 비교한다. 승강기 버튼 주변에는 종이를 두지 않는다.
	CreateBlock(FVector(401, -363, 150), FVector(23, 3, 32), Metal, false);
	CreateBlock(FVector(401, -361.3f, 164.8f), FVector(9, .4f, 1.8f), Stainless, false);

	// 공동현관 인터폰은 바깥쪽 손잡이 옆에 붙는다.
	CreateProp(TEXT("SM_EntranceCamera"), FVector(703, -395, 137), nullptr, 0, 1, false);
	// 게시판 두 장은 실제 A4 비율로 걸며, 우편 투입구를 가리지 않는다.
	CreateBlock(FVector(633, -146.5f, 153), FVector(82, 3, 76), Metal, false);
	CreateBlock(FVector(633, -148.2f, 153), FVector(78, .4f, 72), FridgeInteriorMaterial, false);
	// 옛 이야기의 단수 공지는 없는 층의 게시판에 붙이지 않는다. 4층 승강기 옆에
	// 걸던 공지와 같은 문구라, 관리인이 추락 시각에 옥상을 막아 둔 것처럼 읽힌다.
	if (!GetWorld()->URL.HasOption(TEXT("IGMissingFloor"))
		&& !GetWorld()->URL.HasOption(TEXT("IGListenerGreybox"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("IGMissingFloor"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreybox")))
	{
		CreateBlock(FVector(610, -148.65f, 166), FVector(21, .08f, 29.7f),
			TexMat(TEXT("M_LobbyWaterNotice"), SignWhiteMaterial), false);
	}
	CreateBlock(FVector(610, -148.65f, 134), FVector(21, 0.08f, 29.7f),
		TexMat(TEXT("M_LobbyContactNotice"), SignWhiteMaterial), false);

	// Lobby fluorescents: one over the mailboxes, one at the lift doors.
	// 복도와 같은 Blender 등 메시 둘. 돔 슬롯은 예전처럼 씬이 재질을 건다.
	UStaticMesh* LobbyLightRingMesh = PropMesh(TEXT("SM_CeilingLightRing"));
	UStaticMesh* LobbyLightDomeMesh = PropMesh(TEXT("SM_CeilingLightDome"));
	for (const float FixtureX : {510.0f, 660.0f})
	{
		if (LobbyLightRingMesh && LobbyLightDomeMesh)
		{
			CreateBlock(
				FVector(FixtureX, -305, 240), FVector(100, 100, 100),
				nullptr, false, LobbyLightRingMesh, FRotator::ZeroRotator);
			LobbyLightDiscs.Add(CreateBlock(
				FVector(FixtureX, -305, 240), FVector(100, 100, 100),
				LightPanelMaterial, false, LobbyLightDomeMesh, FRotator::ZeroRotator));
		}
		else
		{
			// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
			CreateBlock(
				FVector(FixtureX, -305, 237), FVector(28, 28, 4),
				Stainless, false, CylinderMesh);
			LobbyLightDiscs.Add(CreateBlock(
				FVector(FixtureX, -305, 234.5f), FVector(23, 23, 2),
				LightPanelMaterial, false, CylinderMesh));
		}
		UPointLightComponent* LobbyLight = CreateLight(
			FVector(FixtureX, -305, 226), 920.0f, 400.0f,
			FLinearColor(0.87f, 0.98f, 1.0f), true, 16.0f);
		LobbyLight->SetVolumetricScatteringIntensity(0.10f);
		LobbyLights.Add(LobbyLight);
	}

	// A physically separate 2F landing for the interrupted CH02 ride. The
	// previous prototype opened the 1F lobby doors and called that view "2F",
	// which broke the building's vertical logic. This enclosed landing shares
	// the 300 cm floor spacing of the rest of the villa and is visible only
	// through the authored 12 cm lift-door gap.
	constexpr float SecondFloorZ = 300.0f;
	CreateBlock(
		FVector(580, -303, SecondFloorZ - 10),
		FVector(300, 176, 20),
		TexMat(TEXT("M_Concrete_XY"), ConcreteDarkMaterial));
	CreateBlock(
		FVector(580, -303, SecondFloorZ + 250),
		FVector(300, 176, 20),
		LobbyCeil);
	CreateBlock(
		FVector(580, -225, SecondFloorZ + 120),
		FVector(300, 20, 240),
		LobbyWallX);
	CreateBlock(
		FVector(440, -310, SecondFloorZ + 120),
		FVector(20, 150, 240),
		LobbyWallY);

	// East wall and reveals around the actual 2F lift opening.
	CreateBlock(
		FVector(710, -242.5f, SecondFloorZ + 120),
		FVector(20, 15, 240),
		LobbyWallY);
	CreateBlock(
		FVector(710, -367.5f, SecondFloorZ + 120),
		FVector(20, 15, 240),
		LobbyWallY);
	CreateBlock(
		FVector(710, -305, SecondFloorZ + 225),
		FVector(20, 110, 30),
		LobbyWallY);
	// The closed service door and a dim floor plaque give the slit a readable
	// destination without turning the landing into an explorable branch.
	CreateBlock(
		FVector(451.0f, -305, SecondFloorZ + 100),
		FVector(4, 86, 200),
		TexMat(TEXT("M_SteelDoorUV"), DoorMaterial),
		false);
	CreateBlock(
		FVector(454.0f, -305, SecondFloorZ + 158),
		FVector(1.2f, 18, 24),
		ScreenGlowMaterial,
		false);
	// 2층에 있는 등이라 2층 구역에서 켠다. 로비 구역에 두면 2층 복도에 선 동안
	// 꺼지고, 1층에 있는 동안 쓸데없이 켜진다.
	BuildingLightZone = EIGLightZone::SecondFloor;
	UPointLightComponent* SecondFloorEmergencyLight = CreateLight(
		FVector(520, -305, SecondFloorZ + 205),
		18.0f,
		185.0f,
		FLinearColor(0.42f, 0.55f, 0.46f),
		false,
		4.0f);
	SecondFloorEmergencyLight->SetVolumetricScatteringIntensity(0.08f);
	BuildingLightZone = EIGLightZone::Lobby;
}

void AIGPrologueWorldScene::BuildAlley()
{
	UMaterialInterface* AsphaltWorld = TexMat(TEXT("M_AsphaltWorld"), AsphaltMaterial);
	UMaterialInterface* BrickX = TexMat(TEXT("M_UtilityStreetBrick"), ConcreteMaterial);
	UMaterialInterface* VillaStuccoX =
		TexMat(TEXT("M_VillaStucco_X"), ConcreteMaterial);
	UMaterialInterface* VillaStuccoY =
		TexMat(TEXT("M_VillaStucco_Y"), ConcreteMaterial);
	UMaterialInterface* DarkX = TexMat(TEXT("M_UtilityConcreteDark"), ConcreteDarkMaterial);
	UMaterialInterface* DarkY = DarkX;
	// 기둥과 수평 테두리는 면 방향을 고르는 같은 재질을 쓴다.
	UMaterialInterface* DarkXY = DarkX;
	UMaterialInterface* Metal = TexMat(TEXT("M_MetalUV"), MetalFrameMaterial);

	// Asphalt strip from the west dead end to the store front.
	CreateBlock(FVector(1040, -542.5f, -10), FVector(2720, 295, 20), AsphaltWorld);

	// The shop is deeper than the alley is wide, so the ground in front of its
	// northern half was missing entirely — from inside, looking out through
	// the glass showed a hole. Pave that corner and close it with a wall.
	CreateBlock(FVector(2300, -365, -10), FVector(220, 650, 20), AsphaltWorld);
	// The corner belongs to a patched cement-render annex, not to the opposing
	// brick shop row. Its two axes use separate world projections so neither
	// face collapses into the vertical colour stripe seen in the old capture.
	CreateBlock(FVector(2190, -215, 230), FVector(20, 350, 460), VillaStuccoY);
	CreateBlock(FVector(2300, -30, 230), FVector(240, 20, 460), VillaStuccoX);

	// Curb stones seat the facades onto the road. 남쪽 연석은 샛길 입구 두 곳(X 1210..1390,
	// 1640..1800)을 건너뛴다. 한 줄로 두면 샛길로 들어서는 발밑에 12 cm 턱이 걸린다.
	// 공동현관 앞은 연석을 끊는다. 문턱 앞에 별도의 10cm 턱을 만들지 않는다.
	CreateBlock(FVector(138, -410, 4), FVector(916, 16, 12), TexMat(TEXT("M_Concrete_X"), ConcreteMaterial));
	CreateBlock(FVector(1546, -410, 4), FVector(1708, 16, 12), TexMat(TEXT("M_Concrete_X"), ConcreteMaterial));
	CreateBlock(FVector(445, -665, 4), FVector(1530, 16, 12), TexMat(TEXT("M_Concrete_X"), ConcreteMaterial));
	CreateBlock(FVector(1515, -665, 4), FVector(250, 16, 12), TexMat(TEXT("M_Concrete_X"), ConcreteMaterial));
	CreateBlock(FVector(2000, -665, 4), FVector(320, 16, 12), TexMat(TEXT("M_Concrete_X"), ConcreteMaterial));

	// Manhole covers and the drainage channel running down the alley center.
	// Keep the first cover clear of the villa threshold and CH02 offering.
	// The old 72 cm cover sat less than a metre from the bowl and dominated
	// the approach at ordinary first-person eye height.
	CreateBlock(FVector(430, -525, 1.5f), FVector(64, 64, 3), Metal, true, CylinderMesh);
	CreateBlock(FVector(1520, -560, 1.5f), FVector(72, 72, 3), Metal, true, CylinderMesh);
	CreateBlock(FVector(1040, -537, 0.8f), FVector(2720, 26, 2), Metal, false);

	// North side: the villa rises four storeys in red brick — README와 타이틀이
	// 말하는 「붉은 벽돌 빌라」다. 화강석 판은 1층 필로티 기둥과 승강기 벽에만
	// 남는다. Band between the pilotis (0..240) and the 4F corridor wall
	// (900..1140), then a parapet above. 벽돌은 줄눈이 텍스처에 있으므로 판
	// 이음 홈을 따로 세우지 않는다.
	UMaterialInterface* GranitePanelX = TexMat(TEXT("M_UtilityGraniteCladding"), ConcreteMaterial);
	UMaterialInterface* VillaBrickX = TexMat(TEXT("M_UtilityVillaBrick"), GranitePanelX);
	// 대기 공간 아래·위의 벽돌은 바닥과 천장 슬래브의 안쪽까지 뚫고 나오지 않는다.
	CreateBlock(FVector(117, -385, 570), FVector(914, 20, 660), VillaBrickX);
	CreateBlock(FVector(647, -385, 560), FVector(146, 20, 640), VillaBrickX);
	CreateBlock(FVector(117, -385, 1190), FVector(914, 20, 100), VillaBrickX);
	CreateBlock(FVector(647, -385, 1200), FVector(146, 20, 80), VillaBrickX);
	// 층 사이 콘크리트 띠. 벽돌 빌라는 슬래브 선이 밖으로 드러난다.
	for (const float BandZ : {540.0f, 840.0f})
	{
		CreateBlock(
			FVector(190, -396.5f, BandZ), FVector(1060, 3.0f, 14.0f),
			TexMat(TEXT("M_Concrete_X"), ConcreteMaterial), false);
	}
	// Coping band at the parapet and a black roof railing above it.
	CreateBlock(
		FVector(190, -397, 1244), FVector(1068, 12, 10),
		TexMat(TEXT("M_Concrete_X"), ConcreteMaterial), false);
	CreateBlock(FVector(190, -392, 1296), FVector(1060, 4, 4), PlasticDarkMaterial, false);
	CreateBlock(FVector(190, -392, 1272), FVector(1060, 3, 3), PlasticDarkMaterial, false);
	for (float PostX = -320.0f; PostX <= 700.0f; PostX += 34.0f)
	{
		CreateBlock(FVector(PostX, -392, 1274), FVector(2.4f, 2.4f, 56), PlasticDarkMaterial, false);
	}

	// --- Ground-floor pilotis car park (X -340..440) ------------------------
	// The whole ground level is open parking on columns; the lobby entrance
	// sits recessed at its east end. This is what makes the building read as
	// a Korean villa from the alley instead of a wall with a door in it.
	{
		UMaterialInterface* ParkFloor = TexMat(TEXT("M_Concrete_XY"), ConcreteMaterial);
		// 로비 연결통로와 로비 바닥이 같은 높이로 이 슬래브 위에 한 장 더
		// 깔려 있었다. 같은 평면에 두 면을 겹치면 깊이 버퍼가 둘을 갈라내지
		// 못해 걷는 내내 두 재질이 번갈아 이긴다. 실내 바닥이 덮는 자리를
		// 비우고 주차면만 남긴다.
		CreateBlock(FVector(-214.5f, -310, -10), FVector(251, 170, 20), ParkFloor);
		CreateBlock(FVector(170.5f, -380, -10), FVector(519, 30, 20), ParkFloor);
		CreateBlock(FVector(-19.5f, -235, -10), FVector(139, 20, 20), ParkFloor);
		CreateBlock(FVector(360, -235, -10), FVector(140, 20, 20), ParkFloor);
		// 7.8 x 1.7 m 필로티 천장. 바닥과 마주 보는 면이므로 바닥과 같은
		// 축으로 읽어야 한다 — XZ로 읽는 동안 이 면 전체가 콘크리트 한 줄을
		// 1.7 m 늘여 놓은 민무늬였다.
		CreateBlock(FVector(50, -308, 244), FVector(780, 166, 12), DarkXY, false);
		// 주차장 뒤벽은 주차면까지만. 연결 복도의 벽은 BuildLobby가 개구부와
		// 함께 만든다. 여기서 통째로 덮으면 관리실 문이 다시 막힌다. 서쪽은
		// 계단탑의 동쪽 벽이다(BuildStairCore).
		CreateBlock(FVector(-215, -232, 120), FVector(250, 16, 240), DarkX);
		// Columns on the street line, each with a concrete capital.
		for (const float ColumnX : {-300.0f, -140.0f, 20.0f, 180.0f, 340.0f})
		{
			CreateBlock(FVector(ColumnX, -378, 118), FVector(38, 38, 236), GranitePanelX);
			CreateBlock(FVector(ColumnX, -378, 232), FVector(46, 46, 12), DarkX, false);
			CreateBlock(FVector(ColumnX, -378, 8), FVector(46, 46, 16), DarkX, false);
		}
		// 주차면 선과 기둥 사이 체인. 동쪽 기둥들은 연결통로 벽에 묻혀 있어 그
		// 앞의 선과 체인이 통로 바닥과 벽을 뚫고 지나갔다. 열린 칸에만 둔다.
		// 체인은 X로 눕혀 두 기둥 면 사이에 건다 — Roll 90은 원통을 Y로 세워
		// 골목과 주차면 쪽으로 75 cm씩 찌르고 있었다.
		CreateBlock(FVector(-220, -310, 0.6f), FVector(6, 160, 1.4f), SignWhiteMaterial, false);
		CreateBlock(
			FVector(-220, -378, 62), FVector(2.6f, 2.6f, 122),
			PlasticDarkMaterial, false, CylinderMesh, FRotator(90, 0, 0));
		// 그 시간의 두 번째 봉쇄. 공동현관만 잠그면 계단탑에서 주차장을 지나 골목으로
		// 나갈 수 있다. 기둥 안쪽에 내려오는 셔터가 주차장을 건물 안으로 접는다.
		// 계단탑 1층 출입구(Y -350..-235)와 연결통로 입구는 셔터 북쪽이다. 낮에는
		// 감춰 두고 SetTheHourSealed가 내린다. 셔터 통은 늘 보인다.
		StairCoreNightGate = CreateBlock(
			FVector(-214.5f, -352, 119), FVector(251, 4, 238), Metal, false);
		if (StairCoreNightGate)
		{
			StairCoreNightGate->SetHiddenInGame(true);
		}
		CreateBlock(FVector(-214.5f, -349, 230), FVector(251, 10, 16), Metal, false);
		// 계단실 문 옆 발신기도 복도와 같은 제품을 쓴다.
		CreateBlock(FVector(-120, -241, 100), FVector(88, 6, 200), DarkX, false);
		CreateBlock(FVector(-84, -244.5f, 96), FVector(4, 2, 14), Metal, false);
		if (UStaticMesh* FireAlarm = PropMesh(TEXT("SM_FireAlarmPanel")))
		{
			CreateBlock(FVector(254, -248.75f, 108), FVector(100, 100, 100),
				nullptr, false, FireAlarm);
		}
		// A single sodium bulkhead keeps the bay from being a black hole.
		CreateBlock(FVector(-30, -244, 214), FVector(22, 14, 12), Metal, false);
		UPointLightComponent* PilotisLamp = CreateLight(
			FVector(-30, -252, 208), 300.0f, 480.0f,
			FLinearColor(0.98f, 0.78f, 0.48f), true, 12.0f);
		PilotisLamp->SetVolumetricScatteringIntensity(0.65f);
		// 내놓은 쓰레기봉투와 상자. 주차장은 이제 계단탑과 연결통로를 잇는 길이라
		// 길을 막지 않게 빌라 앞 연석 밖에 둔다.
		PlacePhotoProp(TEXT("trashbag"), FVector(-236, -455, 0), FVector(58, 58, 56), 20.0f);
		PlacePhotoProp(TEXT("cardboard_box_01"), FVector(-178, -452, 0), FVector(46, 38, 32), -35.0f);
	}
	// The north facade is split by a service gap, so the alley reads as a
	// junction rather than one long tube. The neighbouring block starts where
	// the villa ends.
	// The lift projects 160 cm beyond the old villa edge. Give that shaft its
	// own thin granite enclosure, then start the neighbouring brick block after
	// it. The previous continuous brick facade intersected the lobby cab and
	// appeared literally inside its left wall.
	// 승강기 벽도 같은 벽돌이다. 화강석 판의 알갱이는 24 cm 반복에서 이 높이로
	// 서면 손바닥만 한 조각으로 읽혔다.
	CreateBlock(FVector(800, -394, 620), FVector(160, 6, 1240), VillaBrickX);
	CreateBlock(FVector(1015, -385, 230), FVector(270, 20, 460), VillaStuccoX);
	CreateBlock(FVector(1845, -385, 230), FVector(1110, 20, 460), VillaStuccoX);

	// Side alley running north between the two buildings: unlit, dead-ended,
	// and just wide enough to notice on the way past.
	{
		const float GapCenterX = 1220.0f;
		CreateBlock(FVector(GapCenterX, -250, -10), FVector(140, 290, 20), AsphaltWorld);
		// 골목 벽은 파사드 뒤(Y -375)에서 시작한다. 예전에는 거리면까지
		// 나와 파사드 끝 20 cm를 그대로 물었고, 두 벽의 거리면과 골목면이
		// 같은 평면이라 어귀 전체가 서로 깜빡였다. 어귀 리빌은 파사드의
		// 마구리가 맡는다.
		CreateBlock(FVector(GapCenterX - 78, -240, 230), FVector(16, 270, 460), DarkY);
		CreateBlock(FVector(GapCenterX + 78, -240, 230), FVector(16, 270, 460), DarkY);
		CreateBlock(FVector(GapCenterX, -110, 230), FVector(140, 20, 460), DarkX);
		// Drain channel, gas meter cluster and a dead wall lamp.
		CreateBlock(FVector(GapCenterX, -250, 0.8f), FVector(24, 280, 2), Metal, false);
		for (const float MeterZ : {96.0f, 140.0f})
		{
			CreateBlock(FVector(GapCenterX - 62, -200, MeterZ), FVector(26, 30, 34),
				FridgeBodyMaterial, false);
			CreateBlock(FVector(GapCenterX - 62, -200, MeterZ + 20), FVector(8, 8, 12),
				Metal, false, CylinderMesh);
		}
		CreateBlock(FVector(GapCenterX + 62, -168, 214), FVector(20, 26, 12),
			PlasticDarkMaterial, false);
		// Rubbish nobody has collected.
		PlacePhotoProp(TEXT("trashbag"), FVector(GapCenterX - 34, -152, 0),
			FVector(58, 58, 56), 40.0f);
		PlacePhotoProp(TEXT("cardboard_box_01"), FVector(GapCenterX + 30, -172, 0),
			FVector(46, 38, 32), -25.0f);
		// 콘은 Blender 메시(Scripts/blender/build_alley_props.py). 원점이 바닥이라
		// 엔진 원뿔의 중심 Z 24 대신 Z 0에 놓는다.
		if (UStaticMesh* SideConeMesh = PropMesh(TEXT("SM_TrafficCone")))
		{
			CreateBlock(
				FVector(GapCenterX + 40, -300, 0), FVector(100, 100, 100),
				nullptr, true, SideConeMesh, FRotator::ZeroRotator);
		}
		else
		{
			// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
			CreateBlock(FVector(GapCenterX + 40, -300, 24), FVector(30, 30, 48),
				TexMat(TEXT("M_ConeOrange"), SnackRedMaterial), true, ConeMesh);
		}
		// One failing lamp deep inside: barely enough to show it dead-ends.
		UPointLightComponent* SideLamp = CreateLight(
			FVector(GapCenterX + 50, -172, 206), 150.0f, 300.0f,
			FLinearColor(0.95f, 0.72f, 0.45f), true, 9.0f);
		SideLamp->SetVolumetricScatteringIntensity(0.70f);
	}

	// Windows for floors two to four. Korean villa windows are tall sashes in
	// dark aluminium with a black railing across the lower half and a stone
	// sill under them — that combination is most of what makes the facade
	// read. One flat on the third floor is faintly awake.
	// 창틀·창턱·난간은 Blender 메시(SM_VillaWindow, build_villa_window.py) 하나를
	// 열다섯 자리에 놓는다. 미닫이 두 짝의 프로파일과 물끊기까지 기하다. 유리
	// 판은 그대로 상자다 — 깨어 있는 집 하나의 발광이 그 판이다. 원점은 벽면
	// (Y -395)의 창 개구부 바닥 중심.
	UStaticMesh* VillaWindowMesh = PropMesh(TEXT("SM_VillaWindow"));
	for (const float WindowZ : {390.0f, 690.0f, 990.0f})
	{
		for (const float WindowX : {-260.0f, -100.0f, 60.0f, 220.0f, 380.0f})
		{
			UMaterialInterface* Pane =
				(FMath::IsNearlyEqual(WindowX, 60.0f) && FMath::IsNearlyEqual(WindowZ, 690.0f))
					? WindowGlowMaterial
					: WindowDarkMaterial;
			CreateBlock(FVector(WindowX, -396, WindowZ), FVector(96, 4, 116), Pane, false);
			if (VillaWindowMesh)
			{
				CreateBlock(
					FVector(WindowX, -395, WindowZ - 60), FVector(100, 100, 100),
					nullptr, false, VillaWindowMesh, FRotator::ZeroRotator);
				continue;
			}
			// Aluminium frame: head, cill, two jambs and the sliding mullion.
			// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
			CreateBlock(FVector(WindowX, -397.5f, WindowZ + 60), FVector(104, 5, 6), PlasticDarkMaterial, false);
			CreateBlock(FVector(WindowX, -397.5f, WindowZ - 60), FVector(104, 5, 6), PlasticDarkMaterial, false);
			CreateBlock(FVector(WindowX - 50, -397.5f, WindowZ), FVector(6, 5, 116), PlasticDarkMaterial, false);
			CreateBlock(FVector(WindowX + 50, -397.5f, WindowZ), FVector(6, 5, 116), PlasticDarkMaterial, false);
			CreateBlock(FVector(WindowX, -397.5f, WindowZ), FVector(4, 5, 112), PlasticDarkMaterial, false);
			// Stone sill with a drip edge.
			// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
			CreateBlock(
				FVector(WindowX, -400, WindowZ - 66), FVector(112, 12, 7),
				TexMat(TEXT("M_Concrete_X"), ConcreteMaterial), false);
			// Black railing across the lower half.
			CreateBlock(FVector(WindowX, -401, WindowZ - 6), FVector(108, 3.5f, 3.5f), PlasticDarkMaterial, false);
			CreateBlock(FVector(WindowX, -401, WindowZ - 34), FVector(108, 3, 3), PlasticDarkMaterial, false);
			for (int32 BarIndex = -4; BarIndex <= 4; ++BarIndex)
			{
				// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
				CreateBlock(
					FVector(WindowX + BarIndex * 12.0f, -401, WindowZ - 34),
					FVector(2.2f, 2.2f, 60), PlasticDarkMaterial, false);
			}
		}
	}

	// 실외기 여섯 대는 같은 고정 메시를 공유한다. 받침과 배관도 모델에 들어 있다.
	UInstancedStaticMeshComponent* Condensers = nullptr;
	if (UStaticMesh* FacadeAcMesh = PropMesh(TEXT("SM_AcOutdoorUnit")))
	{
		Condensers = NewObject<UInstancedStaticMeshComponent>(this, TEXT("OutdoorCondensers"));
		Condensers->SetupAttachment(SceneRoot);
		Condensers->SetMobility(EComponentMobility::Static);
		Condensers->SetStaticMesh(FacadeAcMesh);
		Condensers->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Condensers->SetGenerateOverlapEvents(false);
		Condensers->SetCanEverAffectNavigation(false);
		Condensers->SetCullDistances(0, 5500);
		for (const FVector& Spot : {
			FVector(-100.0f, -395.0f, 268.0f), FVector(220.0f, -395.0f, 568.0f),
			FVector(380.0f, -395.0f, 268.0f), FVector(-260.0f, -395.0f, 868.0f)})
		{
			// 창턱 아래로 외함을 내리고, 벽면 원점에 앵커를 맞춘다.
			Condensers->AddInstance(FTransform(FRotator::ZeroRotator, Spot));
		}
	}
	// 도시가스 배관. 노란 강관이 1층에서 옥상까지 오르고 층마다 세대로 꺾인다.
	{
		UMaterialInterface* GasYellow = SnackYellowMaterial;
		CreateBlock(
			FVector(470, -399.0f, 640), FVector(4.5f, 4.5f, 1200),
			GasYellow, false, CylinderMesh);
		for (const float BranchZ : {330.0f, 630.0f, 930.0f})
		{
			CreateBlock(
				FVector(425, -399.0f, BranchZ), FVector(3.2f, 3.2f, 90),
				GasYellow, false, CylinderMesh, FRotator(90, 0, 0));
			// 세대 가스 계량기. 골목 샛길과 같은 메시.
			if (UStaticMesh* FacadeMeter = PropMesh(TEXT("SM_GasMeterBox")))
			{
				CreateBlock(
					FVector(380, -397.0f, BranchZ - 26.0f), FVector(100, 100, 100),
					nullptr, false, FacadeMeter, FRotator::ZeroRotator);
			}
		}
		// 벽에 고정하는 U볼트 밴드.
		for (float ClampZ = 120.0f; ClampZ < 1200.0f; ClampZ += 180.0f)
		{
			CreateBlock(FVector(470, -397.5f, ClampZ), FVector(8, 6, 3), Metal, false);
		}
	}

	// 전주에서 파사드로 건너오는 전선. 한국 골목의 하늘은 전선이 반이다.
	{
		const auto AddWire = [this](const FVector& From, const FVector& To, const float Thickness)
		{
			const FVector Delta = To - From;
			const float Length = Delta.Size();
			// 원기둥의 Z축을 두 점 사이에 눕힌다. FRotator(90, 0, 0)이 X축을 따라
			// 눕히는 것과 같은 규약이라 피치는 수평 거리와 높이차의 atan2, 요는
			// 뒤집은 방향이다. MakeFromZ와 같은 자세지만 지오메트리 감사가 읽는다.
			const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Delta.Size2D(), Delta.Z));
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Delta.Y, -Delta.X));
			CreateBlock(
				(From + To) * 0.5f, FVector(Thickness, Thickness, Length),
				PlasticDarkMaterial, false, CylinderMesh, FRotator(Pitch, Yaw, 0.0f));
		};
		// 전주 완철(Z 약 430)에서 3층 벽 앵커로, 그리고 두 전주 사이.
		AddWire(FVector(500, -432, 432), FVector(300, -400, 704), 1.6f);
		AddWire(FVector(500, -432, 424), FVector(640, -400, 760), 1.4f);
		AddWire(FVector(500, -434, 440), FVector(1600, -434, 452), 1.6f);
		AddWire(FVector(500, -430, 418), FVector(1600, -430, 428), 1.2f);
		// 벽 앵커: 애자 하나씩.
		CreateBlock(FVector(300, -398, 704), FVector(6, 6, 6), PlasticDarkMaterial, false);
		CreateBlock(FVector(640, -398, 760), FVector(6, 6, 6), PlasticDarkMaterial, false);
	}

	// Rain downspouts pin the facade to the ground.
	for (const float PipeX : {-320.0f, 255.0f})
	{
		CreateBlock(
			FVector(PipeX, -400, 620), FVector(11, 11, 1240),
			DarkY, false, CylinderMesh);
	}

	// Common entrance dressing: canopy, name plate, keypad, threshold.
	// 캐노피도 눕힌 판이라 수평 축으로 읽는다.
	CreateBlock(FVector(643, -405, 240), FVector(104, 44, 6), DarkXY, false);
	CreatePrintedBlock(
		FVector(643, -396.5f, 258), FVector(80, 3, 24),
		PlasticDarkMaterial,
		TexMat(TEXT("M_SignVilla"), SignWhiteMaterial),
		FVector(0, -1, 0));
	CreateBlock(FVector(692, -394, 115), FVector(10, 4, 16), PlasticDarkMaterial, false);
	CreateBlock(FVector(695, -395.4f, 118), FVector(3, 1.2f, 3),
		ScreenGlowMaterial, false);
	CreateBlock(FVector(643, -402, 4), FVector(94, 30, 8), TexMat(TEXT("M_Concrete_XY"), ConcreteMaterial));

	// Wall-mounted AC condenser units.
	for (const FVector& UnitCenter : {FVector(310, -403, 320), FVector(1100, -403, 142)})
	{
		CreateBlock(UnitCenter, FVector(56, 26, 52), FridgeBodyMaterial, false);
		CreateBlock(
			UnitCenter + FVector(0, -14.5f, 0), FVector(34, 3, 34),
			PlasticDarkMaterial, false, CylinderMesh, FRotator(90, 0, 0));
		for (int32 GrillIndex = -1; GrillIndex <= 1; ++GrillIndex)
		{
			CreateBlock(
				UnitCenter + FVector(0, -15.5f, GrillIndex * 10.0f),
				FVector(44, 1.5f, 2), FridgeBodyMaterial, false);
		}
	}

	// 첫 안내문은 공동현관 오른쪽 34cm 벽체에 붙인다. 예전 X 800은 벽이
	// 없는 틈이었다. 두꺼운 판·공중에 뜬 현수막 대신 A4 두 장만 남긴다.
	if (UStaticMesh* RentalNotice = PropMesh(TEXT("SM_RentalNoticeA4")))
	{
		for (const FVector& NoticeAt : {FVector(703, -395.04f, 170), FVector(1905, -395.04f, 168)})
		{
			if (UStaticMeshComponent* Notice = CreateBlock(NoticeAt, FVector(100), nullptr, false, RentalNotice))
			{
				Notice->ComponentTags.Add(TEXT("Visual.RentalNotice"));
				Notice->SetCullDistance(1400.0f);
				Notice->SetEvaluateWorldPositionOffset(false);
			}
		}
	}


	// A red church cross on a far rooftop keeps watch over the district.
	// X 1750은 동쪽 샛길 입구 위 빈 하늘이라 동쪽 토막 지붕선(X 2000)으로 옮겼다.
	CreateBlock(FVector(2000, -700, 545), FVector(10, 8, 96), AlarmMaterial, false);
	CreateBlock(FVector(2000, -700, 566), FVector(58, 8, 10), AlarmMaterial, false);

	// Parking cones and the AC drain pipes running to the street.
	UStaticMesh* ParkingConeMesh = PropMesh(TEXT("SM_TrafficCone"));
	if (ParkingConeMesh)
	{
		// 둥근 콘이라 돌릴 이유가 없고, 돌리면 감사 상자가 커져 옆의 물리
		// 상자(X 2240)를 1 cm 문다.
		CreateBlock(
			FVector(2280, -640, 0), FVector(100, 100, 100),
			nullptr, true, ParkingConeMesh, FRotator::ZeroRotator);
		CreateBlock(
			FVector(1180, -430, 0), FVector(100, 100, 100),
			nullptr, true, ParkingConeMesh, FRotator::ZeroRotator);
	}
	else
	{
		// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
		CreateBlock(
			FVector(2280, -640, 24), FVector(30, 30, 48),
			TexMat(TEXT("M_ConeOrange"), SnackRedMaterial), true, ConeMesh);
		CreateBlock(
			FVector(1180, -430, 24), FVector(30, 30, 48),
			TexMat(TEXT("M_ConeOrange"), SnackRedMaterial), true, ConeMesh);
	}
	for (const float PipeX : {350.0f, 1100.0f})
	{
		CreateBlock(
			FVector(PipeX + 34, -399, 70), FVector(6, 6, 140),
			DarkY, false, CylinderMesh);
	}

	// Dark upstairs windows on the neighbouring block; nobody is awake at this
	// hour. These belong to the street facade at Y -395, not to the pilotis
	// back wall at Y -232: authored on the back wall's plane, every one of
	// them sat 1.6 m inside the building, above a wall that stops at Z 240 and
	// behind granite that hides it. Each pane now beds 1 cm into the stucco it
	// is glazed into, and the bays that fell on the villa's own frontage are
	// gone — that facade already carries its full sash-frame-sill-railing grid
	// for floors two to four, so a bare pane there was a second window drawn
	// over the first.
	// 유리 뒤에는 방이 있다. M_RoomInterior가 창마다 방 사진 한 장을 원근에 맞춰 펴고,
	// 그 집이 깨어 있으면 불을 켠다(UpdateNeighborhoodAwake). 재질이 없으면 예전
	// 어두운 유리다.
	UMaterialInterface* RoomWindow = TexMat(TEXT("M_RoomInterior"), WindowDarkMaterial);
	const float NorthWindowXs[] = {1000, 1700, 2100};
	for (int32 WindowIndex = 0; WindowIndex < static_cast<int32>(UE_ARRAY_COUNT(NorthWindowXs)); ++WindowIndex)
	{
		const float OffsetZ = (WindowIndex % 2 == 0) ? 300.0f : 310.0f;
		NightViewSurfaces.Add(CreateBlock(
			FVector(NorthWindowXs[WindowIndex], -396, OffsetZ),
			FVector(90, 4, 110),
			RoomWindow,
			false));
	}

	// South side: opposing building lined with shuttered shops — the
	// text-dense storefront wall that makes it read as a Korean back street.
	// Masonry, not flat render: brick above a dark painted plinth.
	// 27 m를 한 판으로 두면 골목이 아니라 복도다. 두 자리(X 1210..1390, 1640..1800)를
	// 샛길로 터서 상가를 세 토막으로 나눈다. 샛길 안쪽은 아래에서 짓는다.
	const float FacadeX0[] = {-320.0f, 1390.0f, 1840.0f};
	const float FacadeX1[] = {1210.0f, 1640.0f, 2160.0f};
	for (int32 SegmentIndex = 0; SegmentIndex < 3; ++SegmentIndex)
	{
		const float CenterX = (FacadeX0[SegmentIndex] + FacadeX1[SegmentIndex]) * 0.5f;
		const float Length = FacadeX1[SegmentIndex] - FacadeX0[SegmentIndex];
		CreateBlock(FVector(CenterX, -690, 310), FVector(Length, 20, 380), BrickX);
		CreateBlock(FVector(CenterX, -688, 60), FVector(Length, 22, 120), DarkX);
		CreateBlock(FVector(CenterX, -687, 122), FVector(Length, 24, 8),
			TexMat(TEXT("M_Concrete_X"), ConcreteMaterial), false);
	}
	{
		// Four individualized storefronts: varied widths, awnings, blade
		// signs, a display window, an A-frame board, gas bottles, planters —
		// and second-storey PC-bang/noraebang signs, one of them still lit.
		UMaterialInterface* Shutter = TexMat(TEXT("M_Shutter_X"), PlasticDarkMaterial);
		struct FShopSpec
		{
			float X;
			float Width;
			const TCHAR* Sign;
			UMaterialInterface* Awning;
		};
		const FShopSpec Shops[] = {
			{400.0f, 150.0f, TEXT("M_SignLaundry"), SnackBlueMaterial},
			{900.0f, 128.0f, TEXT("M_SignHair"), SnackRedMaterial},
			{1500.0f, 172.0f, TEXT("M_SignHof"), SnackRedMaterial},
			{2050.0f, 150.0f, TEXT("M_SignSuper"), BottleGreenMaterial},
		};
		for (int32 ShopIndex = 0; ShopIndex < 4; ++ShopIndex)
		{
			const FShopSpec& Shop = Shops[ShopIndex];
			const float HalfWidth = Shop.Width * 0.5f;
			// Frame jambs and the fascia sign with its phone-number strip.
			CreateBlock(FVector(Shop.X - HalfWidth - 6, -677, 100), FVector(12, 10, 200), DarkX, false);
			CreateBlock(FVector(Shop.X + HalfWidth + 6, -677, 100), FVector(12, 10, 200), DarkX, false);
			// 간판 텍스처를 16 cm 몸통에 통째로 주면 엔진 큐브의 여섯 면이
			// 같은 UV를 나눠 쓰는 탓에 옆면·밑면·뒷면에도 상호가 한 번 더
			// 눌려 찍힌다. 편의점 파사드에서 이미 고친 것과 같은 자리다.
			// 함체는 민무늬로 두고 인쇄는 골목을 보는 앞면에만 붙인다.
			CreatePrintedBlock(
				FVector(Shop.X, -672, 222), FVector(Shop.Width + 24, 16, 42),
				PlasticDarkMaterial,
				TexMat(Shop.Sign, PlasticDarkMaterial),
				FVector(0, 1, 0),
				false);
			CreateBlock(FVector(Shop.X, -670, 197), FVector(Shop.Width - 30, 2, 9),
				FridgeInteriorMaterial, false);
			// Striped awning over the entrance.
			CreateBlock(
				FVector(Shop.X, -662, 206), FVector(Shop.Width + 16, 42, 3.5f),
				Shop.Awning, false, nullptr, FRotator(-21, 0, 0));
			// Blade sign hung off the west jamb.
			CreateBlock(
				FVector(Shop.X - HalfWidth - 10, -664, 258), FVector(10, 24, 78),
				Shop.Awning, false);

			if (ShopIndex == 1)
			{
				// The hair salon keeps a dark display window instead of a shutter.
				CreateBlock(FVector(Shop.X, -676, 96), FVector(Shop.Width, 5, 192), GlassMaterial);
				CreateBlock(FVector(Shop.X, -683, 96), FVector(Shop.Width, 4, 192), PlasticDarkMaterial, false);
				CreateBlock(FVector(Shop.X, -679, 60), FVector(Shop.Width - 24, 6, 5), DarkX, false);
			}
			else
			{
				CreateBlock(FVector(Shop.X, -676, 96), FVector(Shop.Width, 8, 192), Shutter);
			}
		}
		// A-frame board in front of the hof, gas bottles and foam boxes for
		// the super, planters by the laundry.
		CreateBlock(FVector(1445, -648, 38), FVector(42, 3, 74), SnackYellowMaterial, false,
			nullptr, FRotator(-12, 0, 0));
		CreateBlock(FVector(1445, -640, 38), FVector(42, 3, 74), SnackYellowMaterial, false,
			nullptr, FRotator(12, 0, 0));
		CreateBlock(FVector(2110, -655, 40), FVector(26, 26, 80), Metal, true, CylinderMesh);
		CreateBlock(FVector(2136, -652, 40), FVector(26, 26, 80), Metal, true, CylinderMesh);
		// Shopfront clutter stands on the road at Z 0 and beside the kerb,
		// whose face is at Y -657. Authored one or two centimetres up and
		// seven to ten centimetres back, each of these hovered over the
		// asphalt with a corner inside the kerb stone.
		CreateBlock(FVector(1985, -642, 10), FVector(40, 30, 20), FridgeInteriorMaterial);
		CreateBlock(FVector(1985, -642, 29), FVector(38, 28, 18), FridgeInteriorMaterial);
		CreateBlock(FVector(330, -645, 13), FVector(24, 24, 26), CoolerBodyMaterial, true, CylinderMesh);
		CreateBlock(FVector(330, -645, 36), FVector(34, 34, 30), BottleGreenMaterial, false, SphereMesh);
		CreateBlock(FVector(362, -643, 13), FVector(24, 24, 26), CoolerBodyMaterial, true, CylinderMesh);
		CreateBlock(FVector(362, -643, 36), FVector(34, 34, 30), BottleGreenMaterial, false, SphereMesh);
		// Upstairs: PC-bang sign dark, noraebang sign still glowing pink. Both
		// boxes bolt back onto the brick at Y -680; at Y -672 they hung a
		// centimetre off it with nothing carrying the load.
		// 벽에 볼트로 붙은 광고판이라 인쇄는 골목 쪽 한 면뿐이다. 14 cm
		// 몸통에 직접 주면 위아래 마구리에도 같은 상호가 눌려 찍힌다.
		CreatePrintedBlock(
			FVector(900, -673, 330), FVector(170, 14, 40),
			PlasticDarkMaterial,
			TexMat(TEXT("M_SignPC"), PlasticDarkMaterial),
			FVector(0, 1, 0));
		CreatePrintedBlock(
			FVector(1500, -673, 330), FVector(190, 14, 40),
			PlasticDarkMaterial,
			TexMat(TEXT("M_SignKaraoke"), PlasticDarkMaterial),
			FVector(0, 1, 0));
	}
	// 1750은 동쪽 샛길 자리라 1900으로 옮겼다. 1850 전신주와 겹치지 않는다.
	const float SouthWindowXs[] = {250, 650, 1150, 1900, 2150};
	for (int32 WindowIndex = 0; WindowIndex < static_cast<int32>(UE_ARRAY_COUNT(SouthWindowXs)); ++WindowIndex)
	{
		const float OffsetZ = (WindowIndex % 2 == 0) ? 320.0f : 330.0f;
		// Beds 1 cm into the brick face at Y -680 instead of hanging 2 cm
		// clear of it.
		NightViewSurfaces.Add(CreateBlock(
			FVector(SouthWindowXs[WindowIndex], -679, OffsetZ),
			FVector(80, 4, 90),
			RoomWindow,
			false));
	}

	// West dead end.
	CreateBlock(FVector(-330, -542.5f, 250), FVector(20, 295, 500), DarkY);

	// Streetlights; the middle one is wired to fail as the player passes.
	// 골목 보안등은 아연도금 강관 기둥에 팔을 하나 내고 납작한 LED 등기구를 단
	// 한국 주택가의 그 모양이다. 예전 자리에 서 있던 유럽식 주물 가로등(스캔 소품)은
	// 골목을 외국 관광지처럼 보이게 해서 뺐다. 기둥 바깥 지름 11 cm, 높이 4 m,
	// 팔은 골목 가운데 쪽으로 80 cm.
	UMaterialInterface* GalvanizedSteel = TexMat(TEXT("M_MetalUV"), PlasticDarkMaterial);
	const float StreetlightXs[] = {150, 1000, 1850};
	for (const float PoleX : StreetlightXs)
	{
		// Concrete anchor plinth under every mast.
		CreateBlock(
			FVector(PoleX, -640, 14), FVector(34, 34, 28),
			TexMat(TEXT("M_Concrete_XY"), ConcreteMaterial), true, CylinderMesh);
		CreateBlock(
			FVector(PoleX, -640, 214), FVector(11, 11, 372),
			GalvanizedSteel, true, CylinderMesh);
		CreateBlock(
			FVector(PoleX, -596, 392), FVector(6, 81, 6),
			GalvanizedSteel, false);
		// 등기구 몸체와 아래로 향한 발광면. 발광면은 몸체 밑에 붙은 얇은 판이다.
		CreateBlock(
			FVector(PoleX, -566, 385.5f), FVector(24, 34, 7),
			PlasticDarkMaterial, false);
		CreateBlock(
			FVector(PoleX, -566, 381.5f), FVector(18, 28, 1),
			StreetLampGlowMaterial, false);
		UPointLightComponent* LampLight = CreateLight(
			FVector(PoleX, -566, 368),
			2400.0f,
			760.0f,
			FLinearColor(0.93f, 0.95f, 1.0f),
			true,
			18.0f,
			nullptr,
			true);
		// 아래로 향한 등기구에 맞추되 가로등 사이 바닥까지 고르게 비춘다.
		CastChecked<USpotLightComponent>(LampLight)->SetInnerConeAngle(65.0f);
		LampLight->SetVolumetricScatteringIntensity(0.55f);
	}

	// 상가 사이 두 샛길이 뒤편 배송 골목으로 이어진다. 편의점 옆길까지 한 바퀴 돌 수 있다.
	UMaterialInterface* BrickY = TexMat(TEXT("M_UtilityStreetBrick"), ConcreteMaterial);
	const auto Passage = [this, AsphaltWorld, BrickY](float X0, float X1)
	{
		CreateBlock(FVector((X0 + X1) * .5f, -960, -10), FVector(X1 - X0, 560, 20), AsphaltWorld);
		CreateBlock(FVector(X0 - 10, -965, 230), FVector(20, 530, 460), BrickY);
		CreateBlock(FVector(X1 + 10, -965, 230), FVector(20, 530, 460), BrickY);
	};
	Passage(1210, 1390);
	Passage(1640, 1840);
	// 후면 길과 편의점 옆길. 경계는 눈에 보이는 건물 벽이며 보행 바닥에 턱을 두지 않는다.
	// 끝벽 안쪽까지 바닥을 넣어 모서리에서 하늘이 비치는 틈을 막는다.
	CreateBlock(FVector(1830, -1360, -10), FVector(1280, 240, 20), AsphaltWorld);
	// 샛길 서쪽 벽과 끝벽이 만나는 곳에 남는 10×10cm 바닥도 잇는다.
	CreateBlock(FVector(1205, -1235, -10), FVector(10, 10, 20), AsphaltWorld);
	CreateBlock(FVector(2310, -965, -10), FVector(260, 550, 20), AsphaltWorld);
	CreateBlock(FVector(1190, -1350, 230), FVector(20, 260, 460), BrickY);
	CreateBlock(FVector(1830, -1490, 230), FVector(1300, 20, 460), BrickX);
	// 편의점 옆 외벽의 뒤쪽 끝을 뒷담 안까지 이어 세로로 열린 10cm 틈을 닫는다.
	CreateBlock(FVector(2450, -1170, 230), FVector(20, 640, 460), VillaStuccoY);
	CreateBlock(FVector(2170, -960, 230), FVector(20, 560, 460), BrickY);
	CreateBlock(FVector(1515, -1240, 230), FVector(250, 20, 460), BrickX);
	CreateBlock(FVector(2000, -1240, 230), FVector(320, 20, 460), BrickX);
	// 뒷벽의 20cm 측면도 벽돌 줄눈이 이어지도록 해당 면의 축으로 마감한다.
	for (const float CapX : {1389.9f, 1640.1f, 1839.9f, 2160.1f})
	{
		CreateBlock(FVector(CapX, -1240, 230), FVector(.2f, 20, 460), BrickY, false);
	}
	// 실외기와 계량기는 벽에 붙이고 발밑의 이동 폭을 비워 둔다.
	for (const FVector& P : {FVector(1209, -830, 135), FVector(1639, -880, 135), FVector(2169, -1050, 135)})
	{
		if (UStaticMesh* Meter = PropMesh(TEXT("SM_GasMeterBox")))
			CreateBlock(P, FVector(100), nullptr, false, Meter, FRotator(0, -90, 0));
	}
	for (const FVector& P : {FVector(1460, -1480, 230), FVector(2040, -1480, 230)})
	{
		if (Condensers) Condensers->AddInstance(FTransform(FRotator(0, 180, 0), P));
	}
	if (Condensers)
	{
		Condensers->RegisterComponent();
		AddInstanceComponent(Condensers);
		GeometryComponents.Add(Condensers);
	}
	for (const FVector& P : {FVector(1226, -1060, 300), FVector(1824, -1100, 300), FVector(2196, -1060, 300)})
	{
		CreateBlock(P, FVector(16, 28, 12), PlasticDarkMaterial, false);
		CreateBlock(P - FVector(0, 0, 7), FVector(12, 22, 2), StreetLampGlowMaterial, false);
		CreateLight(P - FVector(0, 0, 15), 850, 540, FLinearColor(1, .85f, .65f), false, 16);
	}
	CreateLight(FVector(1800, -1390, 340), 1400, 800, FLinearColor(.93f, .94f, 1), true, 24);
	// 세탁소 후문과 상가 공용 게시판. 인물의 말을 확인하러 다시 찾을 수 있는 장소다.
	CreateBlock(FVector(1510, -1477, 105), FVector(92, 5, 210), TexMat(TEXT("M_Shutter_X"), Metal), true);
	CreateBlock(FVector(2045, -1475, 151), FVector(116, 8, 86), PlasticDarkMaterial, false);
	CreateBlock(FVector(2045, -1470, 151), FVector(108, 2, 78), TexMat(TEXT("M_PaperClean"), SignWhiteMaterial), false);
	PlacePhotoProp(TEXT("plastic_crate_01"), FVector(1560, -1447, 0), FVector(42, 32, 26), 0);
	PlacePhotoProp(TEXT("cardboard_box_01"), FVector(1580, -1430, 27), FVector(28, 26, 21), 0);
	PlacePhotoProp(TEXT("trashbag"), FVector(2390, -1435, 0), FVector(42, 42, 45), 0);
	if (UStaticMesh* Mirror = PropMesh(TEXT("SM_ConvexMirror")))
		CreateBlock(FVector(1196, -680.5f, 240), FVector(100), nullptr, false, Mirror, FRotator(0, 145, 0));

	// Utility poles with junction boxes for the Korean-alley silhouette.
	// X 600은 공동현관(힌지 X 604, Y -385) 문짝이 그리는 호 안이었다. 밖으로
	// 95도 열리면 문짝이 이 4.5 m 전신주를 6 cm 물고 지나간다. 힌지에서 37 cm
	// 밖에 안 되는 자리다. 호 밖(110 cm)이면서 현관 디딤판(X 596..690)도
	// 비켜나는 X 500으로 옮긴다.
	// 전주는 Blender 메시(SM_UtilityPole, build_alley_props.py)다. 위로 가늘어지는
	// 콘크리트 전주에 완철 둘과 애자, 번호판. 분전함은 그대로 스캔 소품이 붙는다.
	UStaticMesh* PoleMesh = PropMesh(TEXT("SM_UtilityPole"));
	for (const float PoleX : {500.0f, 1600.0f})
	{
		if (PoleMesh)
		{
			CreateBlock(
				FVector(PoleX, -422, 0), FVector(100, 100, 100),
				nullptr, true, PoleMesh, FRotator::ZeroRotator);
		}
		else
		{
			// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. 위 if와 배타적이라 화면에 함께 없다.
			CreateBlock(
				FVector(PoleX, -422, 225), FVector(14, 14, 450),
				DarkY, true, CylinderMesh);
		}
		if (!PlacePhotoProp(
			TEXT("utility_box_01"), FVector(PoleX, -428, 330), FVector(52, 42, 66), 90.0f, false))
		{
			CreateBlock(FVector(PoleX, -422, 390), FVector(45, 35, 60), DarkX, false);
		}
	}

	// (Moonlight now comes from the physically based directional moon.)

	// Trash and boxes: scanned bags/cartons where imported, plus a couple of
	// physics cubes that can still be kicked down the alley.
	if (!PlacePhotoProp(TEXT("trashbag"), FVector(-260, -520, 0), FVector(60, 60, 58), 20.0f))
	{
		CreatePhysicsProp(
			SphereMesh, TrashBagMaterial,
			FVector(0.42f, 0.42f, 0.33f), FVector(-260, -520, 17), FRotator::ZeroRotator, 1.2f);
	}
	PlacePhotoProp(TEXT("trashbag"), FVector(-232, -556, 0), FVector(54, 54, 52), 140.0f);
	PlacePhotoProp(TEXT("trashbag"), FVector(2282, -612, 0), FVector(62, 62, 60), 260.0f);
	if (!PlacePhotoProp(
		TEXT("cardboard_box_01"), FVector(2320, -585, 0), FVector(48, 40, 34), 20.0f))
	{
		CreatePhysicsProp(
			CubeMesh, CardboardMaterial,
			FVector(0.40f, 0.32f, 0.26f), FVector(2320, -585, 14), FRotator(0, 20, 0), 0.8f);
	}
	// 이 상자도 -35도라 Y로 45 cm를 차지한다. Y -645에서는 뒤쪽 모서리가
	// 연석(Y -673..-657, 높이 12 cm) 안으로 10 cm 들어가 연석에 잠겨 있었다.
	// 아래 물리 상자에 이미 같은 계산을 적어 둔 자리다.
	PlacePhotoProp(TEXT("cardboard_box_01"), FVector(1180, -630, 0), FVector(44, 36, 30), -35.0f);
	// Beside the kerb and clear of the parking cone, not through either.
	// Turned 65 degrees the box reaches 21.2 cm in X and 22.7 cm in Y, so at
	// (2255, -640) it had a corner 5.7 cm inside the kerb stone and 11.2 cm
	// inside the cone -- both of which the solver ejects on the first tick.
	CreatePhysicsProp(
		CubeMesh, CardboardMaterial,
		FVector(0.36f, 0.30f, 0.24f), FVector(2240, -633, 13), FRotator(0, 65, 0), 0.7f);
}

void AIGPrologueWorldScene::BuildStore()
{
	UMaterialInterface* Tile = TexMat(TEXT("M_StoreTileWorld"), StoreFloorMaterial);
	UMaterialInterface* Wall = TexMat(TEXT("M_StoreWall_X"), ConcreteMaterial);
	UMaterialInterface* Ceiling = TexMat(TEXT("M_StoreCeilWorld"), ConcreteMaterial);
	UMaterialInterface* Metal = TexMat(TEXT("M_MetalUV"), MetalFrameMaterial);
	UMaterialInterface* Pet = TexMat(TEXT("M_RetailPET"), GlassMaterial);
	// 실내 700×770cm. 계산대 앞 110cm, 진열대 사이 162cm를 비운다.
	CreateBlock(FVector(2760, -435, 2), FVector(720, 790, 8), Tile);
	CreateBlock(FVector(2750, -40, 130), FVector(720, 20, 260), Wall);
	CreateBlock(FVector(2750, -830, 130), FVector(720, 20, 260), Wall);
	CreateBlock(FVector(3120, -435, 130), FVector(20, 810, 260), TexMat(TEXT("M_StoreWall_Y"), ConcreteMaterial));
	CreateBlock(FVector(2760, -435, 270), FVector(720, 810, 20), Ceiling);
	// 간판 함체와 천장 슬래브를 겹치지 않는다. 유리 출입구는 기존 자동문과 맞춘다.
	for (const float Y : {-52.0f, -824.0f})
	{
		CreateBlock(FVector(2405, Y, 130), FVector(10, 20, 260), Wall);
	}
	CreateBlock(FVector(2405, -227, 108), FVector(8, 328, 204), GlassMaterial);
	CreateBlock(FVector(2405, -665, 108), FVector(8, 288, 204), GlassMaterial);
	for (const float Y : {-65.0f, -230.0f, -395.0f, -517.0f, -660.0f, -810.0f})
	{
		CreateBlock(FVector(2405, Y, 110), FVector(12, 8, 208), Metal);
	}
	CreateBlock(FVector(2405, -227, 12), FVector(12, 328, 12), Metal);
	CreateBlock(FVector(2405, -665, 12), FVector(12, 288, 12), Metal);
	CreateBlock(FVector(2405, -435, 227), FVector(12, 770, 34), TexMat(TEXT("M_StoreWall_Y"), ConcreteMaterial));
	CreatePrintedBlock(FVector(2392, -435, 276), FVector(12, 770, 96),
		PlasticDarkMaterial, TexMat(TEXT("M_SignMainLit"), SignMintMaterial), FVector(-1, 0, 0));
	CreatePrintedBlock(FVector(2358, -108, 322), FVector(48, 8, 96),
		PlasticDarkMaterial, TexMat(TEXT("M_SignBladeLit"), SignWhiteMaterial), FVector(0, 1, 0), false, true);
	CreateBlock(FVector(2384, -108, 344), FVector(20, 3, 3), Metal, false);
	CreatePrintedBlock(FVector(2399, -585, 134), FVector(1, 50, 69),
		SignWhiteMaterial, TexMat(TEXT("M_PosterSale"), SignWhiteMaterial), FVector(-1, 0, 0), false);
	CreateBlock(FVector(2438, -457, 7), FVector(64, 94, 1.4f), PlasticDarkMaterial, false);
	CreateBlock(FVector(2426, -398, 246), FVector(15, 10, 10), FridgeInteriorMaterial, false);
	CreateBlock(FVector(2434, -404, 241), FVector(7, 7, 10), PlasticDarkMaterial, false, CylinderMesh, FRotator(48, 35, 0));
	// 실내 LED 네 줄. 밝은 매장은 정상적인 생활 공간의 기준점이다.
	for (const float X : {2540.0f, 2920.0f})
	{
		for (const float Y : {-250.0f, -650.0f})
		{
			CreateBlock(FVector(X, Y, 257), FVector(180, 7, 4), LightPanelMaterial, false);
			UPointLightComponent* Light = CreateLight(FVector(X, Y, 239), 3650, 460,
				FLinearColor(1, .985f, .955f), true, 12, nullptr, true);
			Light->SetVolumetricScatteringIntensity(0.02f);
		}
	}
	CreateLight(FVector(2340, -457, 225), 1150, 650, FLinearColor(1, .96f, .87f), true, 28);
	// 집기는 자체 재질을 유지한다. 인쇄 좌표는 FBX UV0에 있다.
	const auto Fixture = [this](const TCHAR* Name, const FVector& Position, float Yaw = 0, bool Collision = true) -> UStaticMeshComponent*
	{
		if (UStaticMesh* Mesh = PropMesh(Name))
		{
			return CreateBlock(Position, FVector(100), nullptr, Collision, Mesh, FRotator(0, Yaw, 0));
		}
		return nullptr;
	};
	StoreClerk = GetWorld()->SpawnActor<AIGStoreClerk>(AIGStoreClerk::StaticClass(), FVector(2590, -128, 6), FRotator::ZeroRotator);
	Fixture(TEXT("SM_StoreCounter"), FVector(2590, -200, 6));
	Fixture(TEXT("SM_RetailPOS"), FVector(2608, -190, 99), 0, false);
	Fixture(TEXT("SM_CardTerminal"), FVector(2665, -216, 99), 0, false);
	Fixture(TEXT("SM_HotSnackWarmer"), FVector(2510, -198, 99));
	Fixture(TEXT("SM_TobaccoCabinet"), FVector(2590, -50, 80));
	for (const float X : {2535.0f, 2645.0f})
	{
		CreatePrintedBlock(FVector(X, -67.4f, 203), FVector(99, .15f, 25.5f),
			PlasticDarkMaterial, TexMat(TEXT("M_RetailTobaccoAd"), SignWhiteMaterial), FVector(0, -1, 0), false);
	}
	CreatePrintedBlock(FVector(2590, -51, 228), FVector(140, 1, 12),
		SignWhiteMaterial, TexMat(TEXT("M_TobaccoNotice"), SignWhiteMaterial), FVector(0, -1, 0), false);
	// 계산대 옆 직원 출입문. 호출을 듣고 나오는 방향을 공간으로 보여 준다.
	Fixture(TEXT("SM_UnitDoorLeaf"), FVector(2925, -53, 6), 0, false);
	Fixture(TEXT("SM_UnitDoorHardware"), FVector(2925, -56, 6), 0, false);
	// 품목별 구획과 앞줄 맞춤. 같은 상품은 3~4개 폭으로 묶고 뒤에 재고를 둔다.
	const TCHAR* Bags[] = {TEXT("SM_RetailPotato"), TEXT("SM_RetailShrimp"), TEXT("SM_RetailCorn")};
	const TCHAR* BagPrices[] = {TEXT("M_RetailPricePotato"), TEXT("M_RetailPriceShrimp"), TEXT("M_RetailPriceCorn")};
	for (int32 Run = 0; Run < 2; ++Run)
	{
		const float Y = Run == 0 ? -380.0f : -620.0f;
		Fixture(TEXT("SM_StoreGondola"), FVector(2710, Y, 6));
		for (const float Side : {-1.0f, 1.0f})
		{
			for (int32 Tier = 0; Tier < 5; ++Tier)
			{
				const float Z = 31.5f + Tier * 30.0f;
				const bool Cups = Run == 0 && (Tier == 2 || Tier == 3);
				const bool Boxes = Run == 1 && (Tier == 0 || Tier == 2);
				const int32 Count = Cups ? 14 : (Boxes ? 11 : 12);
				const float Pitch = Cups ? 16.0f : (Boxes ? 21.0f : 19.0f);
				for (int32 Column = 0; Column < Count; ++Column)
				{
					const float X = 2710.0f + (Column - (Count - 1) * .5f) * Pitch;
					const TCHAR* SKU = Cups ? (Column < 7 ? TEXT("SM_RetailCupBeef") : TEXT("SM_RetailCupKimchi"))
						: Boxes ? TEXT("SM_RetailBiscuit") : Bags[(Column / 4 + Tier + Run) % 3];
					const bool bPricePosition = Cups ? (Column == 3 || Column == 10)
						: Boxes ? (Column == 2 || Column == 8) : (Column % 4 == 1);
					if (bPricePosition)
					{
						const TCHAR* Price = Cups ? (Column < 7 ? TEXT("M_RetailPriceCupBeef") : TEXT("M_RetailPriceCupKimchi"))
							: Boxes ? TEXT("M_RetailPriceBiscuit") : BagPrices[(Column / 4 + Tier + Run) % 3];
						AddStoreStockBlock(FVector(X, Y + Side * 38.8f, Z - 1.5f), FVector(20, .2f, 5),
							TexMat(Price, SignWhiteMaterial), false, FRotator::ZeroRotator);
					}
					for (int32 Depth = 0; Depth < 2; ++Depth)
					{
						// 한두 개 빠진 자리는 앞줄에 남긴다. 무작위 회전으로 진열을 흐트러뜨리지 않는다.
						if (Depth == 0 && Column == 9 && Tier == 1 && Run == 1 && Side < 0) continue;
						// 돌려놓은 한 봉지만 뒷면이 보인다. 나머지는 품목별 앞줄을 유지한다.
						const bool bReturnedBag = Run == 1 && Tier == 1 && Column == 8 && Depth == 0 && Side < 0;
						const float StockYaw = (Side > 0 ? 180.0f : 0.0f) + (bReturnedBag ? 180.0f : 0.0f);
						AddStoreStockProp(SKU, FVector(X, Y + Side * (Depth == 0 ? 28.0f : 11.5f), Z),
							nullptr, StockYaw, 1, true);
					}
				}
			}
		}
	}
	Fixture(TEXT("SM_StoreCoolerBank"), FVector(3085, -430, 6));
	Fixture(TEXT("SM_StoreCoolerDoor"), FVector(3060, -592, 6), 120);
	const float BayCenters[] = {-630, -552, -474, -396, -318, -240};
	for (int32 Bay = 0; Bay < 6; ++Bay)
	{
		for (int32 Tier = 0; Tier < 3; ++Tier)
		{
			if (Bay == 1 && Tier == 1) continue;
			const float Z = 61.5f + Tier * 45.0f;
			UMaterialInterface* DrinkPrice = Bay <= 2 ? TexMat(TEXT("M_RetailPriceWater"), SignWhiteMaterial)
				: Bay == 3 ? TexMat(TEXT("M_RetailPriceSoda"), SignWhiteMaterial)
				: Bay == 4 ? TexMat(TEXT("M_RetailPriceBarley"), SignWhiteMaterial)
				: TexMat(TEXT("M_RetailPriceGreenTea"), SignWhiteMaterial);
			AddStoreStockBlock(FVector(3060.4f, BayCenters[Bay], Z + 1.5f), FVector(.2f, 16, 4),
				DrinkPrice, false, FRotator::ZeroRotator);
			for (int32 Column = 0; Column < 7; ++Column)
			{
				for (int32 Depth = 0; Depth < 2; ++Depth)
				{
					const FVector P(3081.0f + Depth * 12.0f, BayCenters[Bay] - 27.0f + Column * 9.0f, Z);
					// 냉장고도 한 칸에 같은 종류를 모은다. 우유·소주·생수를 한 줄씩 섞지 않는다.
					if (Bay <= 2)
					{
						AddStoreStockProp(TEXT("SM_WaterBottle"), P, Pet, 0, 1, true);
						AddStoreStockProp(TEXT("SM_BottleCap"), P + FVector(0, 0, 20.1f), FridgeInteriorMaterial, 0, 1, false);
						AddStoreStockBottleLabel(P, 3.32f, 7.2f, 5.5f, TEXT("M_LabelWater"), 0);
					}
					else if (Bay == 3)
					{
						AddStoreStockProp(TEXT("SM_WaterBottle"), P, Pet, 0, 1, true);
						AddStoreStockProp(TEXT("SM_BottleCap"), P + FVector(0, 0, 20.1f), FridgeInteriorMaterial, 0, 1, false);
						AddStoreStockBottleLabel(P, 3.32f, 7.2f, 5.5f, TEXT("M_LabelSoda"), 0);
					}
					else
					{
						AddStoreStockProp(TEXT("SM_WaterBottle"), P, BottleBrownMaterial, 0, 1, true);
						AddStoreStockProp(TEXT("SM_BottleCap"), P + FVector(0, 0, 20.1f), FridgeInteriorMaterial, 0, 1, false);
						AddStoreStockBottleLabel(P, 3.32f, 7.2f, 5.5f, Bay == 4 ? TEXT("M_LabelBarley") : TEXT("M_LabelGreenTea"), 0);
					}
				}
			}
		}
	}
	CreateLight(FVector(3050, -430, 195), 380, 490, FLinearColor(.91f, .97f, 1), false, 20);
	Fixture(TEXT("SM_OpenShowcase"), FVector(2820, -800, 6));
	for (int32 Tier = 0; Tier < 3; ++Tier)
	{
		const TCHAR* Kimbap[] = {TEXT("SM_TriangleKimbapA"), TEXT("SM_TriangleKimbapB"), TEXT("SM_TriangleKimbapC"), TEXT("SM_TriangleKimbapD")};
		for (int32 Column = 0; Column < 16; ++Column)
		{
			AddStoreStockProp(Kimbap[(Column / 4 + Tier) % 4], FVector(2722 + Column * 13, -795, 71.5f + Tier * 35),
				nullptr, 180, 1, true);
		}
	}
	Fixture(TEXT("SM_ChestFreezer"), FVector(2445, -545, 6));
	Fixture(TEXT("SM_WindowBar"), FVector(2433, -730, 6));
	Fixture(TEXT("SM_HotWaterDispenser"), FVector(2433, -710, 111), 90);
	CreateProp(TEXT("SM_Stool"), FVector(2468, -751, 6), PlasticDarkMaterial, 0, 1, true);
	// 온수기 앞은 서서 물을 받는 자리다. 의자를 놓으면 꼭지와 통로를 함께 막는다.
	Fixture(TEXT("SM_TrashBin"), FVector(3030, -799, 6), 180);
	for (int32 Basket = 0; Basket < 5; ++Basket)
	{
		CreateProp(TEXT("SM_Basket"), FVector(2442, -320, 6 + Basket * 3.0f), SnackBlueMaterial, 0, 1, Basket == 0);
	}
	FinalizeStoreStockBatches();
	int32 RetailBatches = 0;
	int32 RetailInstances = 0;
	const bool bRetailValid = ValidateStoreStockBatches(RetailBatches, RetailInstances);
	UE_LOG(LogTemp, Display, TEXT("RETAIL_STOCK %s instances=%d batches=%d"),
		bRetailValid ? TEXT("PASS") : TEXT("FAIL"), RetailInstances, RetailBatches);
}


void AIGPrologueWorldScene::BuildSkyAndFog()
{
	// Physically based pre-dawn: real atmospheric scattering with the sun
	// still 8 degrees below the eastern horizon, a genuine directional moon,
	// and a real-time sky light so every surface receives twilight ambience.
	SkyAtmosphere = NewObject<USkyAtmosphereComponent>(this, TEXT("SkyAtmosphere"));
	SkyAtmosphere->SetupAttachment(SceneRoot);
	SkyAtmosphere->RegisterComponent();

	PreDawnSun = NewObject<UDirectionalLightComponent>(this, TEXT("PreDawnSun"));
	PreDawnSun->SetupAttachment(SceneRoot);
	PreDawnSun->SetMobility(EComponentMobility::Movable);
	// The prologue's east is +X: nautical twilight, sun 14 degrees under.
	PreDawnSun->SetWorldRotation(FRotator(14.0f, 180.0f, 0.0f));
	PreDawnSun->SetIntensity(120000.0f); // physical sun illuminance in lux
	PreDawnSun->SetLightColor(FLinearColor(1.0f, 0.86f, 0.72f));
	// 지평선 아래에 있는 동안은 대기 투과율이 지면에 닿는 빛을 이미 0으로 만든다.
	// 그림자를 켜 두면 아무것도 비추지 않는 해의 클립맵만 매 프레임 그린다.
	// 해가 뜨는 아침에 ApplyExteriorTimeOfDay가 그림자를 켠다.
	PreDawnSun->SetCastShadows(false);
	PreDawnSun->SetAtmosphereSunLight(true);
	PreDawnSun->SetAtmosphereSunLightIndex(0);
	PreDawnSun->ForwardShadingPriority = 0;
	PreDawnSun->RegisterComponent();

	MoonLight = NewObject<UDirectionalLightComponent>(this, TEXT("MoonLight"));
	MoonLight->SetupAttachment(SceneRoot);
	MoonLight->SetMobility(EComponentMobility::Movable);
	MoonLight->SetWorldRotation(FRotator(-38.0f, 35.0f, 0.0f));
	MoonLight->SetIntensity(0.25f); // full-moon-ish illuminance in lux
	MoonLight->SetLightColor(FLinearColor(0.62f, 0.72f, 0.92f));
	MoonLight->SetCastShadows(true);
	MoonLight->ContactShadowLength = 0.0f;
	MoonLight->SetAtmosphereSunLight(true);
	MoonLight->SetAtmosphereSunLightIndex(1);
	MoonLight->SetVolumetricScatteringIntensity(0.15f);
	// The moon owns single-light effects (volumetric fog) at this hour.
	MoonLight->ForwardShadingPriority = 1;
	MoonLight->RegisterComponent();

	SkyAmbient = NewObject<USkyLightComponent>(this, TEXT("SkyAmbient"));
	SkyAmbient->SetupAttachment(SceneRoot);
	SkyAmbient->SetMobility(EComponentMobility::Movable);
	SkyAmbient->bRealTimeCapture = true;
	SkyAmbient->SetIntensity(0.62f);
	SkyAmbient->bLowerHemisphereIsBlack = true;
	SkyAmbient->RegisterComponent();

	// Thin pre-dawn haze; volumetric so the streetlights carve visible cones.
	HeightFog = NewObject<UExponentialHeightFogComponent>(this, TEXT("HeightFog"));
	HeightFog->SetupAttachment(SceneRoot);
	HeightFog->SetRelativeLocation(FVector(1000, -400, 0));
	HeightFog->SetFogDensity(0.012f);
	HeightFog->SetFogHeightFalloff(0.4f);
	HeightFog->SetFogInscatteringColor(FLinearColor(0.030f, 0.042f, 0.085f));
	HeightFog->SetVolumetricFog(true);
	HeightFog->SetVolumetricFogScatteringDistribution(0.55f);
	HeightFog->SetVolumetricFogExtinctionScale(1.0f);
	HeightFog->RegisterComponent();
}

void AIGPrologueWorldScene::BuildDistantSkyline()
{
	// 옥상에 올라가면 사방이 새까만 허공이었다. 서울 빌라촌의 새벽 네 시 반은
	// 동쪽 아파트 단지, 북쪽 산비탈 빌라촌, 남쪽 큰길, 서쪽 교회와 능선이 몇 집만
	// 불을 켠 채 둘러선 풍경이다. 방향마다 그림 한 장씩이고, 하늘은 비워 두어 게임의
	// 하늘과 달이 그대로 보인다. 그림 속 창은 동네가 깨어 있는 만큼만 켜진다
	// (UpdateNeighborhoodAwake).
	//
	// 판 네 장을 34 m 앞에 따로 세웠을 때는 모서리마다 하늘이 뚫렸고, 옥상을 걸으면
	// 무대 배경처럼 같이 밀렸다. 이제 판은 지도를 닫는 상자일 뿐이고 그림은 재질이
	// 시선 방향으로 찾는다. 원경은 무한히 먼 곳에 붙어 움직이지 않고, 가까운 난간과
	// 이웃 건물만 그 앞을 지나간다. 두께 2 cm라 그림자와 거리장에 들어가지 않는다.
	// 상자는 편의점(X 3120)보다 바깥에 두르고, 땅 밑까지 내려 어느 높이에서
	// 내려다봐도 틈이 없다.
	UMaterialInterface* Skyline = TexMat(TEXT("M_NightSkyline"), WindowDarkMaterial);
	UMaterialInterface* Glow = TexMat(TEXT("M_NightSkyGlow"), nullptr);
	// 좌표와 재질을 호출마다 적어야 정적 배치 감사가 자리와 재질을 읽는다.
	auto NightWall = [this](const FVector& Center, const FVector& Size, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Wall = CreateBlock(Center, Size, Material, false);
		NightViewSurfaces.Add(Wall);
		return Wall;
	};
	// 상자는 X -3200..4400, Y -3800..3400, Z -4000..3200이다.
	NightWall(FVector(600.0f, 3400.0f, -400.0f), FVector(7600.0f, 2.0f, 7200.0f), Skyline);
	NightWall(FVector(600.0f, -3800.0f, -400.0f), FVector(7600.0f, 2.0f, 7200.0f), Skyline);
	NightWall(FVector(4400.0f, -200.0f, -400.0f), FVector(2.0f, 7200.0f, 7200.0f), Skyline);
	NightWall(FVector(-3200.0f, -200.0f, -400.0f), FVector(2.0f, 7200.0f, 7200.0f), Skyline);

	// 지평선의 도시 불빛과 동쪽 끝의 새벽 기운은 4 m 바깥 상자에 더해서 그린다.
	// 원경 그림의 건물이 앞을 가린 곳은 깊이에 막혀 들어가지 않으니 하늘 빈 곳만
	// 밝아지고, 건물은 그 앞에서 검은 윤곽으로 남는다. 세기 0.2는 옥상 동쪽 측정
	// 지점(rooftop_skyline)에서 윤곽이 읽히는 가장 낮은 값이다. 재질이 없으면 회색
	// 기본 재질 상자가 하늘을 덮으므로 아예 세우지 않는다.
	if (!Glow)
	{
		return;
	}
	constexpr float GlowStrength = 0.2f;
	NightWall(FVector(600.0f, 3800.0f, 1650.0f), FVector(8400.0f, 2.0f, 6300.0f), Glow)
		->SetCustomPrimitiveDataFloat(1, GlowStrength);
	NightWall(FVector(600.0f, -4200.0f, 1650.0f), FVector(8400.0f, 2.0f, 6300.0f), Glow)
		->SetCustomPrimitiveDataFloat(1, GlowStrength);
	NightWall(FVector(4800.0f, -200.0f, 1650.0f), FVector(2.0f, 8000.0f, 6300.0f), Glow)
		->SetCustomPrimitiveDataFloat(1, GlowStrength);
	NightWall(FVector(-3600.0f, -200.0f, 1650.0f), FVector(2.0f, 8000.0f, 6300.0f), Glow)
		->SetCustomPrimitiveDataFloat(1, GlowStrength);
}

void AIGPrologueWorldScene::UpdateNeighborhoodAwake()
{
	if (!GetWorld())
	{
		return;
	}
	float Awake = 0.45f;
	if (bTheHourSealed)
	{
		// 04:30. 거의 다 자고 있다. 15분에 걸쳐 새벽 출근하는 집부터 하나둘 켜진다.
		const double Elapsed = HourSealedAtSeconds >= 0.0
			? GetWorld()->GetTimeSeconds() - HourSealedAtSeconds
			: 0.0;
		Awake = 0.10f + 0.22f * static_cast<float>(FMath::Clamp(Elapsed / 900.0, 0.0, 1.0));
	}
	else
	{
		// 입주한 저녁은 거의 다 켜져 있다. 밤을 넘긴 뒤의 낮은 일어난 집이 반쯤이다.
		const UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
			: nullptr;
		Awake = (!Narrative || Narrative->GetNightIndex() <= 0) ? 0.82f : 0.45f;
	}
	for (UPrimitiveComponent* Surface : NightViewSurfaces)
	{
		if (Surface)
		{
			Surface->SetCustomPrimitiveDataFloat(0, Awake);
		}
	}
}

void AIGPrologueWorldScene::SpawnInteractables()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Fridge with its empty interior; the reveal drives the whole morning.
	Fridge = World->SpawnActor<AIGFridge>(
		AIGFridge::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPrologueWorld::FridgeLocation),
		SpawnParameters);
	if (Fridge)
	{
		AIGFridge::FIGFridgeContentMeshes FridgeContents;
		FridgeContents.KimchiTub = PropMesh(TEXT("SM_KimchiTub"));
		FridgeContents.MilkCarton = PropMesh(TEXT("SM_MilkCarton"));
		FridgeContents.SojuBottle = PropMesh(TEXT("SM_SojuBottle"));
		Fridge->SetContentMeshes(FridgeContents);

		Fridge->ConfigurePrototypeVisuals(
			CubeMesh, CylinderMesh,
			FridgeBodyMaterial, FridgeInteriorMaterial, PlasticDarkMaterial,
			GlassMaterial, TexMat(TEXT("M_MetalUV"), MetalFrameMaterial),
			SnackRedMaterial, SnackYellowMaterial, BottleGreenMaterial);

		// A single empty bottle lies on its side: the "no water" beat.
		CreatePhysicsProp(
			PropMesh(TEXT("SM_WaterBottle"), CylinderMesh), GlassMaterial,
			FVector::OneVector, FVector(150, -32, 986), FRotator(0, 0, 90), 0.15f);

		// A standard 76 x 76 mm adhesive memo. Its top strip stays flush and the
		// lower edge releases by only 1.8 mm, so the contact shadow reads as paper.
		UStaticMesh* StickyNoteMesh = PropMesh(TEXT("SM_StickyNote76mm"));
		const bool bHasAuthoredStickyNote = StickyNoteMesh != nullptr;
		UStaticMeshComponent* StickyNote = CreateDecoOnComponent(
			Fridge->GetDoorPivot(),
			bHasAuthoredStickyNote ? StickyNoteMesh : PlaneMesh.Get(),
			TexMat(TEXT("M_NoteFridge"), SignWhiteMaterial),
			FVector(-7.43f, 36.0f, 18.0f),
			bHasAuthoredStickyNote ? FRotator::ZeroRotator : FRotator(-90.0f, 0.0f, 0.0f),
			bHasAuthoredStickyNote ? FVector::OneVector : FVector(0.076f));
		if (StickyNote)
		{
			StickyNote->SetCullDistance(650.0f);
			StickyNote->SetAffectDistanceFieldLighting(false);
		}
	}

	// Front door: always available; room clues remain optional.
	HomeDoor = World->SpawnActor<AIGSwingDoor>(
		AIGSwingDoor::StaticClass(),
		FTransform(FRotator(0, -90, 0), IGPrologueWorld::HomeDoorLocation),
		SpawnParameters);
	if (HomeDoor)
	{
		// 힌지가 액터 원점이고 문짝은 +Y로 뻗는다. 바깥면은 +X다.
		// 액터가 -90도 돌아 있으므로 월드에서는 복도 쪽인 -Y를 향한다.
		UStaticMesh* HomeDoorLeafMesh = PropMesh(TEXT("SM_UnitDoorLeafWideL"));
		if (HomeDoorLeafMesh)
		{
			HomeDoor->ConfigureAuthoredLeaf(
				HomeDoorLeafMesh, PropMesh(TEXT("SM_UnitDoorHardwareWideL")), FVector(5, WideDoorLeafWidth, 200));
		}
		else
		{
			HomeDoor->ConfigurePrototypeVisuals(
				CubeMesh,
				TexMat(TEXT("M_UnitDoorPaintedSteel"), DoorMaterial),
				// The authored stainless UV is useful on broad lift panels but
				// compresses into horizontal bands on this 13 cm vertical inlay.
				MetalFrameMaterial,
				FVector(7, WideDoorLeafWidth, 204));
			HomeDoor->SetLeverMesh(
				PropMesh(TEXT("SM_LeverHandle")),
				MetalFrameMaterial,
				FVector(7, WideDoorLeafWidth, 204));
		}
		// Korean entrance doors open outward — and it keeps the hallway clear.
		HomeDoor->SetOpenYaw(-95.0f);

		// Fridge and wallet are optional investigations. Leaving immediately
		// is the canonical PocketCard route, so objectives may suggest them but
		// the physical door must never enforce either state.
		TArray<FIGDoorRequirement> NoDoorRequirements;
		HomeDoor->SetRequirements(MoveTemp(NoDoorRequirements));

		// 광고는 배달원이 붙일 수 있는 바깥 철판에만 둔다. 인쇄면과 고무
		// 뒷면을 나눈 0.6mm 자석이며, 문과 같은 피벗을 따라 움직인다.
		if (UStaticMeshComponent* DeliveryMagnet = CreateDecoOnComponent(
			HomeDoor->GetDoorPivot(), PropMesh(TEXT("SM_DoorDeliveryMagnet")), nullptr,
			FVector(HomeDoorLeafMesh ? 2.56f : 3.56f, 30.0f, 132.0f),
			FRotator(1.5f, 90.0f, 0.0f), FVector::OneVector))
		{
			DeliveryMagnet->EmptyOverrideMaterials();
			DeliveryMagnet->ComponentTags.Add(TEXT("Visual.DeliveryMagnet"));
			DeliveryMagnet->SetCastShadow(false);
			DeliveryMagnet->SetAffectDistanceFieldLighting(false);
			DeliveryMagnet->SetCullDistance(650.0f);
		}
	}

	// Common entrance of the villa: a properly framed glass door off the lobby.
	BuildingDoor = World->SpawnActor<AIGSwingDoor>(
		AIGSwingDoor::StaticClass(),
		FTransform(FRotator(0, -90, 0), FVector(604, -385, 0)),
		SpawnParameters);
	if (BuildingDoor)
	{
		BuildingDoor->ConfigureFramedGlassVisuals(
			CubeMesh, GlassMaterial, TexMat(TEXT("M_MetalUV"), MetalFrameMaterial),
			FVector(6, 84, 204));
		BuildingDoor->SetOpenYaw(-95.0f);
		BuildingDoor->SetInteractionPrompt(
			NSLOCTEXT("IGPrologue", "BuildingDoorPrompt", "공동현관 열기"));
	}

	// 현관 신발장 위의 손전등. 집을 수는 없는 소품이다.
	Flashlight = World->SpawnActor<AIGPickupItem>(
		AIGPickupItem::StaticClass(),
		FTransform(
			FRotator(0, 24, 0),
			FVector(30.0f, -190.0f, IGPrologueWorld::FourthFloorZ + 117.0f)),
		SpawnParameters);
	if (Flashlight)
	{
		Flashlight->ConfigurePrototypeVisuals(
			CylinderMesh, PlasticDarkMaterial, FVector(0.05f, 0.05f, 0.17f), false);
		Flashlight->GetMeshComponent()->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
		Flashlight->SetInteractionEnabled(false);
	}

	// The villa elevator: call it on 4F, ride down to the lobby.
	Elevator = World->SpawnActor<AIGElevator>(
		AIGElevator::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPrologueWorld::ElevatorLocation),
		SpawnParameters);
	if (Elevator)
	{
		AIGElevator::FIGElevatorVisuals CabVisuals;
		CabVisuals.CubeMesh = CubeMesh;
		CabVisuals.CylinderMesh = CylinderMesh;
		CabVisuals.CallPlateMesh = PropMesh(TEXT("SM_LiftCallPlate"));
		// Brushed, mid-roughness stainless retains panel direction and contact
		// shading without becoming a mirror of the exterior Lumen scene.
		CabVisuals.StainlessMaterial =
			TexMat(TEXT("M_StainlessUV"), FridgeBodyMaterial);
		// The apartment-door navy read as an open patch of sky at the end of the
		// corridor. Neutral lift enamel plus the physical 1.4 cm centre seam keeps
		// both landing leaves unmistakably closed before the call completes.
		CabVisuals.DoorMaterial =
			TexMat(TEXT("M_SteelDoorUV"), FridgeBodyMaterial);
		// The story specifies no readable mirror in the safe CH01 car. A
		// brushed rear panel also prevents the outdoor facade/sky reflection
		// from appearing inside a closed elevator during the hidden transfer.
		CabVisuals.MirrorMaterial =
			TexMat(TEXT("M_StainlessUV"), FridgeBodyMaterial);
		CabVisuals.FloorMaterial = TexMat(TEXT("M_MarbleFloor_XY"), StoreFloorMaterial);
		CabVisuals.InlayMaterial = PlasticDarkMaterial;
		CabVisuals.CopMaterial = TexMat(TEXT("M_LiftCOP"), ScreenGlowMaterial);
		CabVisuals.HallMaterial = TexMat(TEXT("M_LiftHall"), ScreenGlowMaterial);
		// The point light below the fixture is the physical emitter. Reusing the
		// store's emissive panel here double-lit the small cab and clipped both
		// diffusers to featureless white after exposure adapted to the dark hall.
		// A matte white lens keeps the panel shape readable without baked light.
		CabVisuals.DiffuserMaterial = SignWhiteMaterial;
		Elevator->ConfigurePrototypeVisuals(CabVisuals, 900.0f);
		// 칸은 층마다 하나다. 그 층에 있을 때만 칸 등이 켜지도록 구역에 넣는다.
		for (UPointLightComponent* CabLight : Elevator->GetCabLights())
		{
			if (!CabLight) { continue; }
			const float LocalZ = GetActorTransform().InverseTransformPosition(CabLight->GetComponentLocation()).Z;
			const EIGLightZone CabZone = LocalZ >= 880.0f
				? EIGLightZone::FourthFloorRooms
				: (LocalZ >= 280.0f ? EIGLightZone::SecondFloor : EIGLightZone::Lobby);
			ZoneLights[static_cast<int32>(CabZone)].Add(CabLight);
		}

		// BuildCabInterior owns the sole rider COP. Adding a camera-facing copy
		// here used to bury buttons inside the opposite handrail.
	}

	// 각 창짝의 가스켓 안에 유리를 끼운다. 발광판이 창틀을 덮지 않는다.
	if (AIGInspectable* Window = World->SpawnActor<AIGInspectable>(
		AIGInspectable::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(-130, 209.4f, 1050)), SpawnParameters))
	{
		Window->ConfigurePrototypeVisuals(CubeMesh, TexMat(TEXT("M_ApartmentNightGlass"), WindowGlowMaterial), FVector(0.534f, 0.0025f, 0.764f));
		if (UStaticMeshComponent* Glass = Window->FindComponentByClass<UStaticMeshComponent>())
		{
			NightViewSurfaces.Add(Glass);
		}
		Window->SetInteractionPrompt(NSLOCTEXT("IGPrologue", "WindowPrompt", "창문"));
		Window->ThoughtText = NSLOCTEXT("IGPrologue", "WindowThought", "건너편 불빛이 방까지 들어오네.");
	}
	if (AIGInspectable* WindowRight = World->SpawnActor<AIGInspectable>(
		AIGInspectable::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(-70, 213.3f, 1050)), SpawnParameters))
	{
		WindowRight->ConfigurePrototypeVisuals(CubeMesh, TexMat(TEXT("M_ApartmentNightGlass"), WindowGlowMaterial), FVector(0.534f, 0.0025f, 0.764f));
		if (UStaticMeshComponent* Glass = WindowRight->FindComponentByClass<UStaticMeshComponent>())
		{
			NightViewSurfaces.Add(Glass);
		}
		WindowRight->SetInteractionPrompt(NSLOCTEXT("IGPrologue", "WindowPrompt", "창문"));
		WindowRight->ThoughtText = NSLOCTEXT("IGPrologue", "WindowThought", "건너편 불빛이 방까지 들어오네.");
	}

	// Store sliding door.
	StoreDoor = World->SpawnActor<AIGSlidingDoor>(
		AIGSlidingDoor::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPrologueWorld::StoreDoorLocation),
		SpawnParameters);
	if (StoreDoor)
	{
		StoreDoor->ConfigurePrototypeVisuals(
			CubeMesh, GlassMaterial, MetalFrameMaterial, FVector(6, 60, 200));
	}

	// 음료 냉장고 한 칸에 물 세 가지가 놓인다. 하나를 집으면 HasWater가
	// 퍼지기 전에 어느 물을 골랐는지부터 적힌다.
	const float WaterYs[] = {-576.0f, -552.0f, -528.0f};
	const EIGPurchaseProfile WaterProfiles[] = {
		EIGPurchaseProfile::ProfileA500MlX2,
		EIGPurchaseProfile::ProfileB1LX1,
		EIGPurchaseProfile::ProfileC2LX2};
	for (int32 WaterIndex = 0; WaterIndex < UE_ARRAY_COUNT(WaterYs); ++WaterIndex)
	{
		const float WaterY = WaterYs[WaterIndex];
		const EIGPurchaseProfile PurchaseProfile = WaterProfiles[WaterIndex];
		AIGPickupItem* WaterBottle = World->SpawnActor<AIGPickupItem>(
			AIGPickupItem::StaticClass(),
			FTransform(FRotator::ZeroRotator, FVector(3085, WaterY, 106.5f)),
			SpawnParameters);
		if (WaterBottle)
		{
			WaterBottle->PickupMode = EIGPickupMode::CarryInHand;
			WaterBottle->PurchaseProfileOnPickup = PurchaseProfile;
			WaterBottle->StateTagOnPickup = FGameplayTag::RequestGameplayTag(
				FName(TEXT("State.CH01.Morning.HasWater")), false);
			FVector ProfileScale = FVector::OneVector;
			bool bHasSecondBottle = false;
			switch (PurchaseProfile)
			{
			case EIGPurchaseProfile::ProfileA500MlX2:
				WaterBottle->SetInteractionPrompt(NSLOCTEXT(
					"IGPrologue", "WaterProfileA", "새벽샘물 500mL 2병 고르기"));
				WaterBottle->ThoughtOnPickup = NSLOCTEXT(
					"IGPrologue", "WaterProfileAThought", "두 병이면 충분하겠지.");
				bHasSecondBottle = true;
				break;
			case EIGPurchaseProfile::ProfileB1LX1:
				WaterBottle->SetInteractionPrompt(NSLOCTEXT(
					"IGPrologue", "WaterProfileB", "한강수 1L 1병 고르기"));
				WaterBottle->ThoughtOnPickup = NSLOCTEXT(
					"IGPrologue", "WaterProfileBThought", "이거 하나면 되겠다.");
				ProfileScale = FVector(1.30f, 1.30f, 1.20f);
				WaterBottle->CarryOffset = FVector(43.0f, 17.0f, -40.0f);
				break;
			case EIGPurchaseProfile::ProfileC2LX2:
				WaterBottle->SetInteractionPrompt(NSLOCTEXT(
					"IGPrologue", "WaterProfileC", "맑은산 2L 2병 고르기"));
				WaterBottle->ThoughtOnPickup = NSLOCTEXT(
					"IGPrologue", "WaterProfileCThought", "무겁지만 한 번에 가져가자.");
				ProfileScale = FVector(1.60f, 1.60f, 1.57f);
				WaterBottle->CarryOffset = FVector(49.0f, 20.0f, -51.0f);
				bHasSecondBottle = true;
				break;
			default:
				break;
			}
			// Held low and to the side so the lathed bottle reads without
			// filling the view; the mesh pivot is at the bottle's base.
			if (PurchaseProfile == EIGPurchaseProfile::ProfileA500MlX2)
			{
				WaterBottle->CarryOffset = FVector(40.0f, 16.0f, -35.0f);
			}
			WaterBottle->CarryRotation = FRotator(-8.0f, -14.0f, 0.0f);
			// Not simulated on the shelf: a lathed bottle standing on a wire
			// shelf topples the instant physics settles, and this one is a
			// pickup target rather than a kickable prop.
			WaterBottle->ConfigurePrototypeVisuals(
				PropMesh(TEXT("SM_WaterBottle"), CylinderMesh),
				TexMat(TEXT("M_RetailPET"), GlassMaterial), ProfileScale, false);
			CreateDecoOnComponent(
				WaterBottle->GetMeshComponent(),
				PropMesh(TEXT("SM_BottleCap"), CylinderMesh), SnackBlueMaterial,
				FVector(0, 0, 20.1f), FRotator::ZeroRotator, FVector::OneVector);
			// A 55 mm commercial wrap band sits on the bottle's straight waist.
			// Keeping it clear of the lower grip ribs prevents floating film and
			// the stretched, torn-looking print of the old 86 mm sleeve.
			CreateDecoOnComponent(
				WaterBottle->GetMeshComponent(),
				PropMesh(TEXT("SM_LabelSleeve"), CylinderMesh),
				TexMat(PurchaseProfile == EIGPurchaseProfile::ProfileC2LX2 ? TEXT("M_LabelWater2L") : PurchaseProfile == EIGPurchaseProfile::ProfileB1LX1 ? TEXT("M_LabelWater1L") : TEXT("M_LabelWater"), WaterBlueMaterial),
				FVector(0, 0, 7.2f), FRotator::ZeroRotator,
				FVector(3.30f, 3.30f, 5.5f));
			if (bHasSecondBottle)
			{
				UStaticMeshComponent* SecondBottle = CreateDecoOnComponent(
					WaterBottle->GetMeshComponent(),
					PropMesh(TEXT("SM_WaterBottle"), CylinderMesh),
					TexMat(TEXT("M_RetailPET"), GlassMaterial),
					FVector(0, 8.0f, 0),
					FRotator(0, 7.0f, 0),
					FVector::OneVector);
				CreateDecoOnComponent(
					SecondBottle,
					PropMesh(TEXT("SM_BottleCap"), CylinderMesh),
					SnackBlueMaterial,
					FVector(0, 0, 20.1f),
					FRotator::ZeroRotator,
					FVector::OneVector);
				CreateDecoOnComponent(
					SecondBottle,
					PropMesh(TEXT("SM_LabelSleeve"), CylinderMesh),
					TexMat(PurchaseProfile == EIGPurchaseProfile::ProfileC2LX2 ? TEXT("M_LabelWater2L") : PurchaseProfile == EIGPurchaseProfile::ProfileB1LX1 ? TEXT("M_LabelWater1L") : TEXT("M_LabelWater"), WaterBlueMaterial),
					FVector(0, 0, 7.2f),
					FRotator::ZeroRotator,
					FVector(3.30f, 3.30f, 5.5f));
			}
			AddStaticPurchaseBagProxy(WaterBottle, PurchaseProfile);
			WaterBottle->OnPickedUp.AddUniqueDynamic(
				this,
				&ThisClass::HandlePurchaseSelectionChanged);
			WaterBottles.Add(WaterBottle);
		}
	}
	// Self-service checkout on the counter.
	Checkout = World->SpawnActor<AIGCheckoutCounter>(
		AIGCheckoutCounter::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPrologueWorld::CheckoutLocation),
		SpawnParameters);
	if (Checkout)
	{
		Checkout->ConfigurePrototypeVisuals(CubeMesh, PlasticDarkMaterial, ScreenGlowMaterial);

		// 터치 POS는 BuildStore에서 상판 위에 배치한다.
		Checkout->SetVisualsHidden(true);
	}

	// Progress volumes: stepping outside and entering the store.
	auto SpawnZone = [&](const FVector& Location, const FVector& Extent,
		const TCHAR* StateTagName, const FText& Thought) -> AIGZoneTrigger*
	{
		const FTransform ZoneTransform(FRotator::ZeroRotator, Location);
		AIGZoneTrigger* Zone = World->SpawnActorDeferred<AIGZoneTrigger>(
			AIGZoneTrigger::StaticClass(),
			ZoneTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Zone)
		{
			Zone->SetZoneExtent(Extent);
			if (StateTagName)
			{
				Zone->StateTagOnEnter = FGameplayTag::RequestGameplayTag(FName(StateTagName), false);
			}
			Zone->ThoughtOnEnter = Thought;
			Zone->FinishSpawning(ZoneTransform);
		}
		return Zone;
	};

	// 403호 문턱을 넘는 순간과 공동현관을 나서는 순간을 따로 남긴다.
	ChapterOneApartmentExitZone = SpawnZone(
		FVector(180, -305, 1010), FVector(72, 62, 110),
		TEXT("State.CH01.Morning.LeftApartment"),
		FText::GetEmpty());
	LeftHomeZone = SpawnZone(
		FVector(643, -435, 110), FVector(120, 55, 110),
		TEXT("State.CH01.Morning.LeftHome"),
		NSLOCTEXT("IGPrologue", "LeftHomeThought", "밖은 후텁지근하네."));
	StoreEntryZone = SpawnZone(
		FVector(2450, -457, 116), FVector(35, 95, 110),
		TEXT("State.CH01.Morning.EnteredStore"),
		FText::GetEmpty());

	SpawnShelterProps(SpawnParameters);
}

void AIGPrologueWorldScene::SpawnShelterProps(const FActorSpawnParameters& SpawnParameters)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 숨는 자리 다섯. 좌표는 각 가구의 앞면 가운데 바닥이고, 자리의 +X가 밖을 본다.
	auto SpawnHidingSpot = [&](const FVector& FrontCenter, const float Yaw) -> AIGHidingSpot*
	{
		return World->SpawnActor<AIGHidingSpot>(
			AIGHidingSpot::StaticClass(),
			FTransform(FRotator(0.0f, Yaw, 0.0f), FrontCenter),
			SpawnParameters);
	};
	// 403호 장롱(SM_Wardrobe, 앞면 X -146.4). 눈은 문짝 14 cm 뒤, 서서 보는 높이.
	// 앞은 의자와 협탁 사이가 좁아서 나오는 자리를 의자 동쪽으로 뺀다.
	if (AIGHidingSpot* Wardrobe = SpawnHidingSpot(FVector(-146.4f, -104.0f, 900.0f), 0.0f))
	{
		Wardrobe->Configure(
			EIGHidingView::DoorGap,
			FVector(-14.0f, 0.0f, 158.0f),
			FVector(106.4f, 24.0f, 0.0f),
			FVector(2.5f, 0.0f, 90.0f),
			FVector(1.5f, 38.0f, 86.0f),
			35.0f,
			15.0f,
			false);
	}
	// 403호 침대 밑. 침대 틀과 이불은 시선 추적을 통과시키므로 상자는 침대 동쪽 옆면
	// 아래쪽 30 cm에만 둔다. 위쪽을 보면 「침대에 눕기」가 잡힌다.
	if (AIGHidingSpot* UnderBed = SpawnHidingSpot(FVector(-93.2f, 98.0f, 900.0f), 0.0f))
	{
		UnderBed->Configure(
			EIGHidingView::UnderBed,
			FVector(-40.0f, 0.0f, 16.0f),
			FVector(48.0f, 30.0f, 0.0f),
			FVector(2.5f, 0.0f, 17.0f),
			FVector(1.5f, 88.0f, 16.0f),
			50.0f,
			12.0f,
			true);
	}
	// 1층 관리실 책상 밑. 상판 아래 73 cm가 비어 있다. 책상 앞면은 남쪽(-Y)을 본다.
	if (AIGHidingSpot* BoothDesk = SpawnHidingSpot(FVector(160.0f, -137.5f, 0.0f), -90.0f))
	{
		BoothDesk->Configure(
			EIGHidingView::UnderBed,
			FVector(-24.0f, 0.0f, 50.0f),
			FVector(62.0f, 0.0f, 0.0f),
			FVector(2.5f, 0.0f, 36.0f),
			FVector(1.5f, 50.0f, 34.0f),
			45.0f,
			15.0f,
			true);
		BoothDesk->SetInteractionPrompt(
			NSLOCTEXT("IGMissingFloor", "HideUnderDeskPrompt", "책상 밑에 숨기"));
	}
	// 201호 북동쪽 구석. 세워 둔 석고보드와 각목에 건 비닐 사이에 사람 하나가
	// 선다. 비닐 선(Y 25)이 앞면이고 남쪽을 본다. 비닐 끝과 보드 사이로 방이 보인다.
	if (AIGHidingSpot* BoardNook = SpawnHidingSpot(FVector(-66.0f, 25.0f, 300.0f), -90.0f))
	{
		BoardNook->Configure(
			EIGHidingView::DoorGap,
			FVector(-24.0f, 0.0f, 150.0f),
			FVector(60.0f, 0.0f, 0.0f),
			FVector(2.5f, 0.0f, 100.0f),
			FVector(1.5f, 40.0f, 92.0f),
			30.0f,
			15.0f,
			false);
		BoardNook->SetInteractionPrompt(
			NSLOCTEXT("IGMissingFloor", "HideBehindSheetPrompt", "비닐 뒤에 숨기"));
	}
	// 302호 장롱. 403호 장롱과 같은 메시라 같은 자리에서 본다(앞면 X -114.4).
	if (AIGHidingSpot* Wardrobe302 = SpawnHidingSpot(FVector(-114.4f, 0.0f, 600.0f), 0.0f))
	{
		Wardrobe302->Configure(
			EIGHidingView::DoorGap,
			FVector(-14.0f, 0.0f, 158.0f),
			FVector(106.4f, 24.0f, 0.0f),
			FVector(2.5f, 0.0f, 90.0f),
			FVector(1.5f, 38.0f, 86.0f),
			35.0f,
			15.0f,
			false);
	}

	// 손전등 건전지 셋. 9×6×1.6 cm 낱개 포장이고, 손전등 빛에 잘 보이게 노란 카드다.
	// 403호는 현관 신발장 위 손전등 옆(신발장 윗면 Z 1013).
	AIGBatteryPickup* HomeBattery = World->SpawnActor<AIGBatteryPickup>(
		AIGBatteryPickup::StaticClass(),
		FTransform(FRotator(0.0f, 12.0f, 0.0f), FVector(2.0f, -197.0f, 1013.8f)),
		SpawnParameters);
	if (HomeBattery)
	{
		HomeBattery->ConfigurePrototypeVisuals(
			CubeMesh, SnackYellowMaterial, FVector(0.09f, 0.06f, 0.016f), false);
	}
	// 관리실 책상 상판(Z 76)의 빈 자리.
	AIGBatteryPickup* BoothBattery = World->SpawnActor<AIGBatteryPickup>(
		AIGBatteryPickup::StaticClass(),
		FTransform(FRotator(0.0f, -21.0f, 0.0f), FVector(145.0f, -127.0f, 76.8f)),
		SpawnParameters);
	if (BoothBattery)
	{
		BoothBattery->ConfigurePrototypeVisuals(
			CubeMesh, SnackYellowMaterial, FVector(0.09f, 0.06f, 0.016f), false);
	}
	// 편의점 계산대(윗면 Z 99), 온장고와 포스 사이. 낮에 산다.
	AIGBatteryPickup* StoreBattery = World->SpawnActor<AIGBatteryPickup>(
		AIGBatteryPickup::StaticClass(),
		FTransform(FRotator(0.0f, 4.0f, 0.0f), FVector(2556.0f, -200.0f, 99.8f)),
		SpawnParameters);
	if (StoreBattery)
	{
		StoreBattery->ConfigurePrototypeVisuals(
			CubeMesh, SnackYellowMaterial, FVector(0.09f, 0.06f, 0.016f), false);
		StoreBattery->SetInteractionPrompt(
			NSLOCTEXT("IGMissingFloor", "BatteryBuyPrompt", "건전지 한 팩 사기"));
	}

	// 4층 서쪽 계단실 입구의 방화문. 늘 고임목으로 괴어 열려 있다. 경첩은 남쪽
	// 문설주 모서리, 문짝은 북쪽(+Y)으로 120 cm. 열면 복도 쪽으로 돌아 남쪽 벽에 붙는다.
	AIGSwingDoor* FireDoor = World->SpawnActor<AIGSwingDoor>(
		AIGSwingDoor::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(-320.0f, -365.0f, 900.0f)),
		SpawnParameters);
	if (FireDoor)
	{
		FireDoor->ConfigurePrototypeVisuals(
			CubeMesh,
			TexMat(TEXT("M_UnitDoorPaintedSteel"), DoorMaterial),
			TexMat(TEXT("M_MetalUV"), MetalFrameMaterial),
			FVector(6.0f, 120.0f, 210.0f));
		FireDoor->SetOpenYaw(-92.0f);
		if (AIGFireDoorWedge* Wedge = World->SpawnActor<AIGFireDoorWedge>(
				AIGFireDoorWedge::StaticClass(),
				FTransform(FRotator(0.0f, -2.0f, 0.0f), FVector(-212.0f, -366.0f, 900.0f)),
				SpawnParameters))
		{
			Wedge->Configure(FireDoor, CubeMesh, WoodMaterial);
			// 계단실 반층 참. 닫힌 문 너머 걷는 소리는 여기서 삼킨다. 복도까지는 닿지 않는다.
			Wedge->SetStairwellCenter(FVector(-420.0f, -305.0f, 870.0f));
		}
	}

	// 403호 현관문 안쪽 걸쇠. 손잡이 위, 문틀 쪽 끝에서 8 cm.
	if (HomeDoor)
	{
		if (AIGDoorLatch* Latch = World->SpawnActor<AIGDoorLatch>(
				AIGDoorLatch::StaticClass(),
				FTransform(FRotator::ZeroRotator, FVector(176.0f, -221.5f, 1045.0f)),
				SpawnParameters))
		{
			Latch->Configure(HomeDoor, CubeMesh, TexMat(TEXT("M_MetalUV"), MetalFrameMaterial));
		}
	}
}

void AIGPrologueWorldScene::HandlePurchaseSelectionChanged(AIGPickupItem* Item)
{
	if (Item)
	{
		RefreshPurchaseProfilePresentation();
	}
}

void AIGPrologueWorldScene::CreateAmbience()
{
	// 방의 공기, 창밖 도로, 편의점 냉각기의 녹음을 각 공간에 둔다.
	// 샘플을 못 읽는 소스 작업 환경에서도 기존 합성 베드는 살아 있다.
	auto RecordedBed = [this](const TCHAR* Name, const EIGAmbienceMode Mode,
		const uint32 Seed, const FVector& Location, const float RecordedVolume,
		const float FallbackVolume, const float Inner, const float Falloff)
	{
		USoundBase* Sound = IGAudio::Sample(Name);
		const bool bRecorded = Sound != nullptr;
		if (!Sound)
		{
			UIGAmbienceSoundWave* Fallback = NewObject<UIGAmbienceSoundWave>(this);
			Fallback->Configure(Mode, Seed);
			Sound = Fallback;
		}
		return CreateAmbientBed(Sound, Location,
			bRecorded ? RecordedVolume : FallbackVolume, Inner, Falloff);
	};
	RecordedBed(TEXT("Bed_Corridor"), EIGAmbienceMode::RoomTone, 0xA13F92C7u,
		FVector(0, 0, 1020), 0.055f, 0.6f, 320.0f, 850.0f);
	StreetBedComponent = RecordedBed(TEXT("Bed_City_Night"), EIGAmbienceMode::StreetWind, 0x5D2E77B1u,
		FVector(1040, -457, 240), 0.16f, 0.85f, 900.0f, 2600.0f);
	RecordedBed(TEXT("Hum_Machine"), EIGAmbienceMode::StoreBuzz, 0x3C91D4E5u,
		FVector(3070, -430, 220), 0.08f, 0.4f, 220.0f, 650.0f);

	// 천장 스피커. 계산대 대화와 냉장고 소리가 묻히지 않게 작게 튼다.
	JingleComponent = CreateAmbientBed(
		UIGToneSequenceSoundWave::CreateStoreJingle(this),
		FVector(2740, -240, 249),
		0.12f,
		170.0f,
		650.0f);
}

void AIGPrologueWorldScene::HandleStoryStateChanged(
	const FGameplayTag StateTag,
	const bool bAdded)
{
	if (!bAdded)
	{
		return;
	}

	const FGameplayTag ChapterOnePurchase = FGameplayTag::RequestGameplayTag(
		FName(TEXT("State.CH01.Morning.WaterPurchased")), false);
	const FGameplayTag ChapterOneHasWater = FGameplayTag::RequestGameplayTag(
		FName(TEXT("State.CH01.Morning.HasWater")), false);
	const FGameplayTag ChapterOneLeftHome = FGameplayTag::RequestGameplayTag(
		FName(TEXT("State.CH01.Morning.LeftHome")), false);
	if (StateTag.MatchesTagExact(ChapterOneLeftHome) && NeighborhoodLifeDirector)
	{
		NeighborhoodLifeDirector->PrimeOutdoorSequence();
	}
	if (StateTag.MatchesTagExact(ChapterOneHasWater)
		|| StateTag.MatchesTagExact(ChapterOnePurchase))
	{
		RefreshPurchaseProfilePresentation();
	}
}
