#include "Interaction/IGElevator.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Animation/AnimSequence.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Entity/IGNoiseSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/IGElevatorButton.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "TimerManager.h"

namespace IGElevator
{
	// §5.1: 호출 버튼을 누르면 승강로 전체가 한 번 운다. 문 여닫기보다는
	// 작고 소품 집는 것보다는 크다.
	constexpr float CallLoudness = 0.18f;
	/** 비상 버튼. 배터리로 우는 벨이라 정전에도 난다. */
	constexpr float AlarmLoudness = 0.55f;

	// 칸 안쪽(저작 메시와 같은 치수, cm). 원점은 칸 바닥 가운데다.
	constexpr float HalfWidth = 75.0f;
	constexpr float HalfDepth = 60.0f;
	/** 문짝 한 장의 폭. 문은 가운데에서 양쪽으로 열린다(문 폭 75). */
	constexpr float DoorLeaf = 37.5f;
	/** 칸 문짝 가운데(칸 로컬 Y). 앞벽(Y -60..-66) 바로 바깥이다. */
	constexpr float CarDoorY = -67.5f;
	/** 승강장 문짝 가운데(액터 로컬 Y). 벽 면(-93)에서 13 cm 안쪽이다. */
	constexpr float LandingDoorY = -79.5f;
	/** 승강장 벽 면(액터 로컬 Y). 세계 X 700이다. */
	constexpr float LandingWallY = -93.0f;
	constexpr float DoorSeconds = 1.6f;
	constexpr float DoorHoldSeconds = 5.0f;
	constexpr float DoorHoldAfterCloseSeconds = 0.6f;
	/** 1990년대 빌라 승강기. 분당 60 m 남짓이라 1층에서 4층까지 10초쯤 걸린다. */
	constexpr float CruiseSpeed = 105.0f;
	constexpr float Accel = 80.0f;

	// 조작반: 칸 오른쪽 벽(+X)의 앞쪽에 붙는다. 그림에서 잰 버튼 높이(판 바닥 기준).
	constexpr float CopBottomZ = 62.0f;
	constexpr float CopY = -40.0f;
	constexpr float CopX = HalfWidth - 0.6f;
	constexpr float FloorButtonZ[6] = {50.02f, 56.31f, 62.60f, 68.83f, 75.06f, 43.73f};
	constexpr float OpenButtonZ = 34.69f;
	constexpr float CloseButtonZ = 28.47f;
	constexpr float AlarmButtonZ = 21.61f;
	constexpr float CopDisplayZ = 83.78f;

	// 층 승강장. 호출판 자리(액터 로컬 X)는 층마다 벽이 남는 쪽이다.
	constexpr float HallPlateX[4] = {-78.0f, -62.5f, -62.5f, 85.0f};
	constexpr float HallPlateZ = 105.0f;
	constexpr float HallDisplayZ = 226.0f;

	// 거울 판(칸 로컬). 뒷벽 가운데 90 x 205 cm.
	constexpr float MirrorY = HalfDepth - 0.1f;
	constexpr float MirrorHalfWidth = 45.0f;
	constexpr float MirrorBottomZ = 10.0f;
	constexpr float MirrorTopZ = 215.0f;
	constexpr int32 MirrorWidthPixels = 256;
	constexpr int32 MirrorHeightPixels = 584;
	/** 반사 몸의 눈높이. 1인칭 카메라 높이에서 이만큼 아래가 발이다. */
	constexpr float ReflectionEyeToFeet = 152.0f;

	const FColor DisplayColor(214, 74, 52);
}

AIGElevator::AIGElevator()
{
	PrimaryActorTick.bCanEverTick = true;
	// 서 있는 동안은 잔다. 버튼·대본·안에 탄 사람이 깨운다(Wake).
	PrimaryActorTick.bStartWithTickEnabled = false;
	// 탄 사람의 이동은 칸 바닥을 기반으로 따라간다. 이동 컴포넌트가 기반 액터의 틱
	// 뒤에 돌도록 엔진이 순서를 잡으므로, 칸은 물리 전에 움직인다.
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	ElevatorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ElevatorRoot"));
	ElevatorRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(ElevatorRoot);

	CabRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CabRoot"));
	CabRoot->SetupAttachment(ElevatorRoot);
	CabRoot->SetMobility(EComponentMobility::Movable);
}

UStaticMeshComponent* AIGElevator::MakeMesh(
	USceneComponent* Parent, UStaticMesh* Mesh, const FName Name,
	const FVector& Location, const FRotator& Rotation, const bool bCollide)
{
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this,
		*FString::Printf(TEXT("%s_%d"), *Name.ToString(), ComponentCounter++));
	Component->SetupAttachment(Parent);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(Mesh);
	Component->SetRelativeLocationAndRotation(Location, Rotation);
	Component->SetCollisionProfileName(bCollide
		? UCollisionProfile::BlockAll_ProfileName
		: UCollisionProfile::NoCollision_ProfileName);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->RegisterComponent();
	return Component;
}

UTextRenderComponent* AIGElevator::MakeDisplay(
	USceneComponent* Parent, const FName Name, const FVector& Location,
	const FRotator& Rotation, const float WorldSize)
{
	UTextRenderComponent* Display = NewObject<UTextRenderComponent>(this,
		*FString::Printf(TEXT("%s_%d"), *Name.ToString(), ComponentCounter++));
	Display->SetupAttachment(Parent);
	Display->SetMobility(EComponentMobility::Movable);
	Display->SetRelativeLocationAndRotation(Location, Rotation);
	Display->SetHorizontalAlignment(EHTA_Center);
	Display->SetVerticalAlignment(EVRTA_TextCenter);
	Display->SetWorldSize(WorldSize);
	Display->SetTextRenderColor(IGElevator::DisplayColor);
	Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Display->SetCastShadow(false);
	Display->RegisterComponent();
	return Display;
}

AIGElevatorButton* AIGElevator::SpawnButton(
	USceneComponent* Parent, const EIGElevatorButtonKind Kind, const int32 Floor,
	const FVector& Location, const FRotator& Rotation, const FVector& Extent)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AIGElevatorButton* Button = World->SpawnActor<AIGElevatorButton>(
		AIGElevatorButton::StaticClass(), Parent->GetComponentTransform(), Params);
	if (!Button)
	{
		return nullptr;
	}
	Button->Configure(this, Kind, Floor, Extent);
	Button->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
	Button->SetActorRelativeLocation(Location);
	Button->SetActorRelativeRotation(Rotation);
	Buttons.Add(Button);
	return Button;
}

void AIGElevator::Configure(const FIGElevatorVisuals& Visuals)
{
	using namespace IGElevator;
	if (bVisualsConfigured)
	{
		return;
	}
	bVisualsConfigured = true;

	// --- 칸 -------------------------------------------------------------
	MakeMesh(CabRoot, Visuals.CabSides, TEXT("CabSides"), FVector::ZeroVector, FRotator::ZeroRotator, true);
	MakeMesh(CabRoot, Visuals.CabFront, TEXT("CabFront"), FVector::ZeroVector, FRotator::ZeroRotator, true);
	MakeMesh(CabRoot, Visuals.CabBack, TEXT("CabBack"), FVector::ZeroVector, FRotator::ZeroRotator, true);
	MakeMesh(CabRoot, Visuals.CabFloor, TEXT("CabFloor"), FVector::ZeroVector, FRotator::ZeroRotator, true);
	CeilingMesh = MakeMesh(CabRoot, Visuals.CabCeiling, TEXT("CabCeiling"), FVector::ZeroVector, FRotator::ZeroRotator, true);
	if (CeilingMesh && CeilingMesh->GetStaticMesh())
	{
		CeilingMaterial = CeilingMesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	BackPanelMesh = MakeMesh(CabRoot, Visuals.BackPanel, TEXT("CabMirror"), FVector::ZeroVector, FRotator::ZeroRotator, true);
	BackPanelSteel = Visuals.BackPanelMaterial;
	if (BackPanelSteel)
	{
		BackPanelMesh->SetMaterial(0, BackPanelSteel);
	}
	// 조작반은 오른쪽 벽에서 칸 안쪽(-X)을 본다. 저작 앞면 -Y를 yaw -90으로 돌린다.
	MakeMesh(CabRoot, Visuals.Cop, TEXT("CabCop"),
		FVector(CopX, CopY, CopBottomZ), FRotator(0.0f, -90.0f, 0.0f), false);
	if (Visuals.Cctv)
	{
		UStaticMeshComponent* Cctv = MakeMesh(CabRoot, Visuals.Cctv, TEXT("CabCctv"),
			FVector(-HalfWidth + 9.0f, HalfDepth - 9.0f, CabHeight), FRotator(0.0f, -135.0f, 0.0f), false);
		Cctv->SetCastShadow(false);
	}
	for (int32 Leaf = 0; Leaf < 2; ++Leaf)
	{
		UStaticMeshComponent* Door = MakeMesh(CabRoot, Visuals.DoorPanel, TEXT("CarDoor"),
			FVector(Leaf == 0 ? -DoorLeaf * 0.5f : DoorLeaf * 0.5f, CarDoorY, 0.0f), FRotator::ZeroRotator, true);
		CarDoors.Add(Door);
	}

	// 칸 등. 확산판 아래 점광원 하나와 발치를 받치는 약한 채움빛.
	CabLight = NewObject<UPointLightComponent>(this, TEXT("ElevatorCabLight"));
	CabLight->SetupAttachment(CabRoot);
	CabLight->SetMobility(EComponentMobility::Movable);
	CabLight->SetRelativeLocation(FVector(0.0f, 0.0f, CabHeight - 26.0f));
	// 작은 칸을 노출이 어두운 복도에 맞춰 하얗게 날리지 않게 한다.
	CabLight->SetIntensity(520.0f);
	CabLight->SetAttenuationRadius(300.0f);
	CabLight->SetLightColor(FLinearColor(0.84f, 0.91f, 1.0f));
	CabLight->SetSourceRadius(46.0f);
	CabLight->SetSoftSourceRadius(70.0f);
	CabLight->SetCastShadows(true);
	// 거울에 둥근 하이라이트가 따로 뜨지 않게 점광원 자체의 반사는 줄인다.
	CabLight->SetSpecularScale(0.18f);
	CabLight->SetVolumetricScatteringIntensity(0.12f);
	CabLight->RegisterComponent();
	FloorFill = NewObject<UPointLightComponent>(this, TEXT("ElevatorCabFill"));
	FloorFill->SetupAttachment(CabRoot);
	FloorFill->SetMobility(EComponentMobility::Movable);
	FloorFill->SetRelativeLocation(FVector(10.0f, 0.0f, 55.0f));
	FloorFill->SetIntensity(36.0f);
	FloorFill->SetAttenuationRadius(220.0f);
	FloorFill->SetLightColor(FLinearColor(0.90f, 0.94f, 1.0f));
	FloorFill->SetSourceRadius(60.0f);
	FloorFill->SetCastShadows(false);
	FloorFill->SetSpecularScale(0.0f);
	FloorFill->RegisterComponent();

	// 층 표시: 문 위 안쪽과 조작반 꼭대기 창.
	CabDisplays.Add(MakeDisplay(CabRoot, TEXT("CabDisplayDoor"),
		FVector(0.0f, -HalfDepth + 0.8f, 217.5f), FRotator(0.0f, 90.0f, 0.0f), 6.0f));
	CabDisplays.Add(MakeDisplay(CabRoot, TEXT("CabDisplayCop"),
		FVector(CopX - 1.0f, CopY, CopBottomZ + CopDisplayZ), FRotator(0.0f, 180.0f, 0.0f), 4.6f));

	// 조작반 버튼. 상자는 버튼 원판(반지름 2.6 cm)보다 조금 작게 잡아 이웃 버튼을 덮지 않는다.
	const FVector ButtonExtent(1.2f, 2.4f, 2.4f);
	const FRotator ButtonRotation(0.0f, 180.0f, 0.0f);
	const int32 FloorValues[6] = {0, 1, 2, 3, 4, -1};
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const float Z = CopBottomZ + FloorButtonZ[Index];
		SpawnButton(CabRoot, EIGElevatorButtonKind::Floor, FloorValues[Index],
			FVector(CopX - 1.4f, CopY, Z), ButtonRotation, ButtonExtent);
		// 눌린 층에는 버튼 오른쪽 작은 등이 들어온다.
		if (Visuals.IndicatorMesh)
		{
			UStaticMeshComponent* Lamp = MakeMesh(CabRoot, Visuals.IndicatorMesh, TEXT("CopLamp"),
				FVector(CopX - 0.9f, CopY + 4.1f, Z), FRotator(0.0f, -90.0f, 0.0f), false);
			Lamp->SetCastShadow(false);
			Lamp->CreateAndSetMaterialInstanceDynamic(0);
			ButtonLamps.Add(Lamp);
		}
	}
	SpawnButton(CabRoot, EIGElevatorButtonKind::Open, 0,
		FVector(CopX - 1.4f, CopY, CopBottomZ + OpenButtonZ), ButtonRotation, ButtonExtent);
	SpawnButton(CabRoot, EIGElevatorButtonKind::Close, 0,
		FVector(CopX - 1.4f, CopY, CopBottomZ + CloseButtonZ), ButtonRotation, ButtonExtent);
	SpawnButton(CabRoot, EIGElevatorButtonKind::Alarm, 0,
		FVector(CopX - 1.4f, CopY, CopBottomZ + AlarmButtonZ), ButtonRotation, ButtonExtent);
	// 「만원」 등. 문 위 안쪽, 층 표시 옆에 따로 붙어 있다. 저작 앞면 -Y를 칸 안(+Y)으로 돌린다.
	if (Visuals.FullLamp)
	{
		FullLamp = MakeMesh(CabRoot, Visuals.FullLamp, TEXT("FullLamp"),
			FVector(-18.0f, -HalfDepth + 0.1f, 214.5f), FRotator(0.0f, 180.0f, 0.0f), false);
		FullLamp->SetCastShadow(false);
		FullLampMaterial = FullLamp->CreateAndSetMaterialInstanceDynamic(0);
		ScriptSetFullLamp(false);
	}

	// --- 층 승강장 ---------------------------------------------------------
	for (int32 Landing = 0; Landing < LandingCount; ++Landing)
	{
		const float Z = GetLandingZ(Landing);
		MakeMesh(ElevatorRoot, Visuals.LandingFrame, TEXT("LandingFrame"),
			FVector(0.0f, LandingWallY, Z), FRotator::ZeroRotator, false);
		for (int32 Leaf = 0; Leaf < 2; ++Leaf)
		{
			LandingDoors.Add(MakeMesh(ElevatorRoot, Visuals.DoorPanel, TEXT("LandingDoor"),
				FVector(Leaf == 0 ? -DoorLeaf * 0.5f : DoorLeaf * 0.5f, LandingDoorY, Z), FRotator::ZeroRotator, true));
		}
		HallDisplays.Add(MakeDisplay(ElevatorRoot, TEXT("HallDisplay"),
			FVector(0.0f, LandingWallY - 2.6f, Z + HallDisplayZ), FRotator(0.0f, -90.0f, 0.0f), 6.0f));
		// 호출판. 원점이 판 앞면 아래 가운데라 판 두께만큼 벽 쪽으로 들인다.
		if (Visuals.CallPlate)
		{
			UStaticMeshComponent* Plate = MakeMesh(ElevatorRoot, Visuals.CallPlate, TEXT("CallPlate"),
				FVector(HallPlateX[Landing], LandingWallY - 1.4f, Z + HallPlateZ), FRotator::ZeroRotator, false);
			Plate->SetCastShadow(false);
		}
		SpawnButton(ElevatorRoot, EIGElevatorButtonKind::HallCall, Landing,
			FVector(HallPlateX[Landing], LandingWallY - 2.4f, Z + HallPlateZ + 12.0f),
			FRotator(0.0f, -90.0f, 0.0f), FVector(1.5f, 5.0f, 12.0f));
	}

	ReflectionMesh = Visuals.ReflectionMesh;
	ReflectionIdle = Visuals.ReflectionIdle;
	ReflectionWalk = Visuals.ReflectionWalk;
	ReflectionLookBack = Visuals.ReflectionLookBack;
	BuildMirror(Visuals);

	// 모터 웅웅거림과 정원 초과 부저는 칸에 붙어 같이 움직인다.
	auto MakeLoop = [this](const FName Name, const float Inner, const float Falloff, USoundBase* Sound)
	{
		UAudioComponent* Audio = NewObject<UAudioComponent>(this, Name);
		Audio->SetupAttachment(CabRoot);
		Audio->SetRelativeLocation(FVector(0.0f, 0.0f, CabHeight + 20.0f));
		Audio->bAutoActivate = false;
		Audio->bAutoDestroy = false;
		Audio->bOverrideAttenuation = true;
		Audio->AttenuationOverrides.bAttenuate = true;
		Audio->AttenuationOverrides.bSpatialize = true;
		Audio->AttenuationOverrides.AttenuationShapeExtents = FVector(Inner, 0.0f, 0.0f);
		Audio->AttenuationOverrides.FalloffDistance = Falloff;
		Audio->AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Audio->AttenuationOverrides.dBAttenuationAtMax = -60.0f;
		Audio->SetSound(Sound);
		Audio->RegisterComponent();
		if (UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
			? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>() : nullptr)
		{
			AudioDirector->RegisterComponent(Audio, EIGAudioBus::World);
		}
		return Audio;
	};
	{
		// 권상기 웅웅거림: 낮은 사인 둘과 줄이 도르래를 타는 가는 잡음.
		TArray<FIGToneNote> Hum;
		Hum.Add({0.0f, 4.0f, 41.0f, 0.12f, 0.0f, 1.0f, EIGToneWaveform::Sine});
		Hum.Add({0.0f, 4.0f, 82.0f, 0.045f, 0.0f, 1.0f, EIGToneWaveform::Sine});
		Hum.Add({0.0f, 4.0f, 260.0f, 0.016f, 0.0f, 1.0f, EIGToneWaveform::ValueNoise});
		UIGToneSequenceSoundWave* HumWave = NewObject<UIGToneSequenceSoundWave>(this);
		HumWave->ConfigureNotes(MoveTemp(Hum), true, 4.0f);
		HumWave->ConfigurePitchWow(0.006f, 0.21f);
		MotorHum = MakeLoop(TEXT("ElevatorMotorHum"), 120.0f, 1100.0f, HumWave);
	}
	{
		// 정원 초과 부저. 0.3초 울고 0.25초 쉬는 각진 소리.
		TArray<FIGToneNote> Buzz;
		for (int32 Beat = 0; Beat < 4; ++Beat)
		{
			const float Start = Beat * 0.55f;
			Buzz.Add({Start, 0.30f, 1180.0f, 0.10f, 0.01f, 0.2f, EIGToneWaveform::SoftSquare});
			Buzz.Add({Start, 0.30f, 2360.0f, 0.025f, 0.01f, 0.2f, EIGToneWaveform::SoftSquare});
		}
		UIGToneSequenceSoundWave* BuzzWave = NewObject<UIGToneSequenceSoundWave>(this);
		BuzzWave->ConfigureNotes(MoveTemp(Buzz), true, 2.2f);
		Buzzer = MakeLoop(TEXT("ElevatorOverloadBuzzer"), 60.0f, 700.0f, BuzzWave);
		Buzzer->SetRelativeLocation(FVector(CopX - 2.0f, CopY, CopBottomZ + 40.0f));
	}

	ApplyCabTransform();
	ApplyDoors();
	UpdateDisplays();
	RefreshButtonLights();
	SetCabLightLevel(1.0f);
}

void AIGElevator::BuildMirror(const FIGElevatorVisuals& Visuals)
{
	using namespace IGElevator;
	// 반사 몸. 메인 화면에는 그리지 않고 거울 캡처에만 나온다.
	if (ReflectionMesh)
	{
		ReflectionBody = NewObject<USkeletalMeshComponent>(this, TEXT("ReflectionBody"));
		ReflectionBody->SetupAttachment(CabRoot);
		ReflectionBody->SetMobility(EComponentMobility::Movable);
		ReflectionBody->SetSkeletalMeshAsset(ReflectionMesh);
		ReflectionBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ReflectionBody->SetCastShadow(false);
		ReflectionBody->bVisibleInSceneCaptureOnly = true;
		ReflectionBody->SetComponentTickEnabled(true);
		ReflectionBody->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		ReflectionBody->RegisterComponent();
		ReflectionBody->SetVisibility(false);
		if (ReflectionIdle)
		{
			ReflectionBody->PlayAnimation(ReflectionIdle, true);
		}
	}
	if (!Visuals.MirrorMaterial || !BackPanelMesh)
	{
		// 거울 재질이 없으면 판은 반입된 스테인리스 그대로다.
		return;
	}
	MirrorTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("ElevatorMirrorTarget"));
	// 장면 색을 선형 HDR 그대로 받는다. 메인 화면의 노출이 한 번만 걸려야 반사가
	// 칸 안 밝기와 맞는다. 낮은 해상도가 그대로 스테인리스의 흐림이 된다.
	MirrorTarget->RenderTargetFormat = RTF_RGBA16f;
	MirrorTarget->ClearColor = FLinearColor::Black;
	MirrorTarget->bAutoGenerateMips = false;
	MirrorTarget->AddressX = TA_Clamp;
	MirrorTarget->AddressY = TA_Clamp;
	MirrorTarget->InitAutoFormat(MirrorWidthPixels, MirrorHeightPixels);

	MirrorCapture = NewObject<USceneCaptureComponent2D>(this, TEXT("ElevatorMirrorCapture"));
	MirrorCapture->SetupAttachment(ElevatorRoot);
	MirrorCapture->SetMobility(EComponentMobility::Movable);
	MirrorCapture->RegisterComponent();
	MirrorCapture->SetAbsolute(true, true, true);
	MirrorCapture->TextureTarget = MirrorTarget;
	MirrorCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	MirrorCapture->bCaptureEveryFrame = false;
	MirrorCapture->bCaptureOnMovement = false;
	MirrorCapture->bAlwaysPersistRenderingState = true;
	MirrorCapture->bUseCustomProjectionMatrix = true;
	MirrorCapture->ShowFlags.SetTemporalAA(false);
	MirrorCapture->ShowFlags.SetMotionBlur(false);
	MirrorCapture->ShowFlags.SetBloom(false);
	MirrorCapture->ShowFlags.SetFog(false);
	MirrorCapture->ShowFlags.SetVolumetricFog(false);
	MirrorCapture->ShowFlags.SetAmbientOcclusion(false);
	MirrorCapture->ShowFlags.SetLumenGlobalIllumination(false);
	MirrorCapture->ShowFlags.SetLumenReflections(false);
	MirrorCapture->ShowFlags.SetScreenSpaceReflections(false);
	MirrorCapture->ShowFlags.SetDynamicShadows(false);
	FPostProcessSettings& Post = MirrorCapture->PostProcessSettings;
	Post.bOverride_AutoExposureMethod = true;
	Post.AutoExposureMethod = AEM_Manual;
	Post.bOverride_AutoExposureBias = true;
	Post.AutoExposureBias = 0.0f;
	Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Post.AutoExposureApplyPhysicalCameraExposure = false;

	MirrorMaterial = UMaterialInstanceDynamic::Create(Visuals.MirrorMaterial, this);
	if (MirrorMaterial)
	{
		MirrorMaterial->SetTextureParameterValue(TEXT("Feed"), MirrorTarget);
	}
}

void AIGElevator::BeginPlay()
{
	Super::BeginPlay();
	CabZ = GetLandingZ(CurrentLanding);
	ApplyCabTransform();
	// 칸 등을 끄고 켜는 판단은 틱이 자는 동안에도 해야 한다. 층을 오가는 걸음이면 충분하다.
	GetWorldTimerManager().SetTimer(LightCullTimer, this, &AIGElevator::UpdateLightCulling, 0.4f, true);
}

void AIGElevator::Wake()
{
	if (!IsActorTickEnabled())
	{
		SetActorTickEnabled(true);
	}
}

bool AIGElevator::CanSleep() const
{
	if (bScripted || bMoving || bDropping || Phase != EIGElevatorPhase::Idle || PendingCalls != 0
		|| DoorOpenAlpha > 0.001f || DoorTargetAlpha > 0.001f
		|| JoltZ != 0.0f || JoltVelocity != 0.0f || bCabLightFlicker || bMirrorLive
		|| ReflectionLookBackSeconds > 0.0f || (Buzzer && Buzzer->IsPlaying()))
	{
		return false;
	}
	const APawn* Pawn = GetPlayerPawn();
	return !(Pawn && IsPawnInsideCab(Pawn));
}

void AIGElevator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(AlarmReplyTimer);
	GetWorldTimerManager().ClearTimer(LightCullTimer);
	for (AIGElevatorButton* Button : Buttons)
	{
		if (IsValid(Button))
		{
			Button->Destroy();
		}
	}
	Buttons.Reset();
	Super::EndPlay(EndPlayReason);
}

APawn* AIGElevator::GetPlayerPawn() const
{
	return UGameplayStatics::GetPlayerPawn(this, 0);
}

int32 AIGElevator::GetStoppedLanding() const
{
	if (bMoving)
	{
		return INDEX_NONE;
	}
	for (int32 Landing = 0; Landing < LandingCount; ++Landing)
	{
		if (FMath::Abs(CabZ - GetLandingZ(Landing)) < 1.0f)
		{
			return Landing;
		}
	}
	return INDEX_NONE;
}

bool AIGElevator::IsPawnInsideCab(const APawn* Pawn) const
{
	if (!Pawn || !CabRoot)
	{
		return false;
	}
	const FVector Local = CabRoot->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
	// 캡슐 가운데가 문턱을 넘어 칸 안쪽에 있어야 탄 것이다.
	return FMath::Abs(Local.X) < IGElevator::HalfWidth
		&& Local.Y > -IGElevator::HalfDepth + 6.0f && Local.Y < IGElevator::HalfDepth
		&& Local.Z > -20.0f && Local.Z < AIGElevator::CabHeight;
}

FVector AIGElevator::GetCabFloorWorldLocation() const
{
	return CabRoot ? CabRoot->GetComponentLocation() : GetActorLocation();
}

FVector AIGElevator::GetLandingFrontWorldLocation(const int32 Landing) const
{
	return GetActorTransform().TransformPosition(
		FVector(0.0f, IGElevator::LandingWallY - 100.0f, GetLandingZ(FMath::Clamp(Landing, 0, LandingCount - 1))));
}

FVector AIGElevator::GetHallCallWorldLocation(const int32 Landing) const
{
	const int32 Index = FMath::Clamp(Landing, 0, LandingCount - 1);
	return GetActorTransform().TransformPosition(FVector(
		IGElevator::HallPlateX[Index], IGElevator::LandingWallY - 2.0f,
		GetLandingZ(Index) + IGElevator::HallPlateZ + 12.0f));
}

void AIGElevator::ApplyCabTransform()
{
	if (CabRoot)
	{
		CabRoot->SetRelativeLocation(FVector(0.0f, 0.0f, CabZ + CabOffsetZ));
	}
}

void AIGElevator::ApplyDoors()
{
	using namespace IGElevator;
	const float Ease = DoorOpenAlpha * DoorOpenAlpha * (3.0f - 2.0f * DoorOpenAlpha);
	const float Shift = Ease * DoorLeaf;
	for (int32 Leaf = 0; Leaf < CarDoors.Num(); ++Leaf)
	{
		const float Side = Leaf == 0 ? -1.0f : 1.0f;
		CarDoors[Leaf]->SetRelativeLocation(FVector(Side * (DoorLeaf * 0.5f + Shift), CarDoorY, 0.0f));
	}
	// 승강장 문은 칸이 선 층만 칸 문을 따라 열린다. 다른 층은 닫힌 채다.
	const int32 OpenLanding = bMoving ? INDEX_NONE : CurrentLanding;
	for (int32 Landing = 0; Landing < LandingCount; ++Landing)
	{
		const bool bThisLanding = Landing == OpenLanding && FMath::Abs(CabZ - GetLandingZ(Landing)) < 2.0f;
		for (int32 Leaf = 0; Leaf < 2; ++Leaf)
		{
			const int32 Index = Landing * 2 + Leaf;
			if (!LandingDoors.IsValidIndex(Index))
			{
				continue;
			}
			const float Side = Leaf == 0 ? -1.0f : 1.0f;
			LandingDoors[Index]->SetRelativeLocation(FVector(
				Side * (DoorLeaf * 0.5f + (bThisLanding ? Shift : 0.0f)), LandingDoorY, GetLandingZ(Landing)));
		}
	}
}

void AIGElevator::SetPhase(const EIGElevatorPhase NewPhase)
{
	Phase = NewPhase;
	switch (Phase)
	{
	case EIGElevatorPhase::DoorsOpening:
		DoorTargetAlpha = 1.0f;
		DoorSpeed = 1.0f / IGElevator::DoorSeconds;
		PlayDoorMotor(true);
		break;
	case EIGElevatorPhase::DoorsOpen:
		DoorHoldSeconds = IGElevator::DoorHoldSeconds;
		break;
	case EIGElevatorPhase::DoorsClosing:
		DoorTargetAlpha = 0.0f;
		DoorSpeed = 1.0f / IGElevator::DoorSeconds;
		PlayDoorMotor(false);
		break;
	default:
		break;
	}
}

bool AIGElevator::HasPendingCall() const
{
	return PendingCalls != 0;
}

void AIGElevator::ClearCall(const int32 Landing)
{
	PendingCalls &= ~(1u << Landing);
	RefreshButtonLights();
}

int32 AIGElevator::PickNextCall() const
{
	// 가장 가까운 층부터. 칸이 하나뿐인 4층 건물이라 방향 우선이 필요 없다.
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Landing = 0; Landing < LandingCount; ++Landing)
	{
		if ((PendingCalls & (1u << Landing)) == 0)
		{
			continue;
		}
		const float Distance = FMath::Abs(GetLandingZ(Landing) - CabZ);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Landing;
		}
	}
	return Best;
}

void AIGElevator::ServeNextCall()
{
	if (bScripted || bHourDead || bMoving)
	{
		return;
	}
	const int32 Next = PickNextCall();
	if (Next == INDEX_NONE)
	{
		return;
	}
	if (Next == CurrentLanding && FMath::Abs(CabZ - GetLandingZ(Next)) < 1.0f)
	{
		ClearCall(Next);
		SetPhase(EIGElevatorPhase::DoorsOpening);
		return;
	}
	BeginTravel(GetLandingZ(Next), IGElevator::CruiseSpeed, IGElevator::Accel);
}

void AIGElevator::BeginTravel(const float TargetZ, const float MaxSpeed, const float InAccel)
{
	TravelStartZ = CabZ;
	TravelTargetZ = TargetZ;
	TravelMaxSpeed = FMath::Max(MaxSpeed, 5.0f);
	TravelAccel = FMath::Max(InAccel, 5.0f);
	TravelSpeed = 0.0f;
	bMoving = true;
	bDropping = false;
	Phase = EIGElevatorPhase::Travelling;
	StartMotorHum();
	// 출발하는 순간 줄이 한 번 당겨진다.
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateRelayClick(this),
		CabRoot->GetComponentLocation() + FVector(0.0f, 0.0f, AIGElevator::CabHeight + 30.0f), 0.45f, 0.55f, 120.0f, 900.0f);
}

void AIGElevator::AdvanceTravel(const float DeltaSeconds)
{
	const float Remaining = TravelTargetZ - CabZ;
	const float Direction = FMath::Sign(Remaining);
	const float Distance = FMath::Abs(Remaining);
	// 남은 거리로 멈출 수 있는 속도까지만 낸다(감속 구간).
	const float StoppingSpeed = FMath::Sqrt(2.0f * TravelAccel * Distance);
	const float Desired = FMath::Min(TravelMaxSpeed, StoppingSpeed);
	TravelSpeed = TravelSpeed < Desired
		? FMath::Min(Desired, TravelSpeed + TravelAccel * DeltaSeconds)
		: FMath::Max(Desired, TravelSpeed - TravelAccel * 1.5f * DeltaSeconds);
	const float Step = TravelSpeed * DeltaSeconds;
	if (Step >= Distance || Distance < 0.05f)
	{
		CabZ = TravelTargetZ;
		Arrive();
		return;
	}
	CabZ += Direction * Step;
	if (MotorHum)
	{
		MotorHum->SetPitchMultiplier(0.86f + 0.14f * (TravelSpeed / FMath::Max(TravelMaxSpeed, 1.0f)));
	}
}

void AIGElevator::Arrive()
{
	bMoving = false;
	TravelSpeed = 0.0f;
	StopMotorHum();
	// 멈출 때 칸이 한 번 내려앉았다 선다.
	JoltVelocity = -14.0f;
	int32 Landing = INDEX_NONE;
	for (int32 Index = 0; Index < LandingCount; ++Index)
	{
		if (FMath::Abs(CabZ - GetLandingZ(Index)) < 1.0f)
		{
			Landing = Index;
		}
	}
	UpdateDisplays();
	if (Landing == INDEX_NONE)
	{
		// 대본이 층 사이에 세웠다. 문은 대본이 연다.
		Phase = EIGElevatorPhase::Idle;
		return;
	}
	CurrentLanding = Landing;
	OnArrived.Broadcast(Landing);
	if (bScripted)
	{
		Phase = EIGElevatorPhase::Idle;
		return;
	}
	PlayChime(Landing);
	ClearCall(Landing);
	SetPhase(EIGElevatorPhase::DoorsOpening);
}

bool AIGElevator::IsDoorwayObstructed() const
{
	const APawn* Pawn = GetPlayerPawn();
	if (!Pawn || !CabRoot)
	{
		return false;
	}
	const FVector Local = CabRoot->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
	const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>();
	const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 30.0f;
	// 문이 지나가는 띠(승강장 벽 면부터 칸 문 안쪽 면까지)에 몸이 걸쳐 있으면 닫지 않는다.
	// 칸 안에서 앞벽 가까이 선 것만으로는 문을 붙잡지 않는다.
	const float CarDoorInnerY = IGElevator::CarDoorY + 1.5f;
	return FMath::Abs(Local.X) < IGElevator::DoorLeaf + Radius
		&& Local.Y - Radius < CarDoorInnerY
		&& Local.Y + Radius > IGElevator::LandingWallY
		&& Local.Z > -60.0f && Local.Z < AIGElevator::CabHeight;
}

void AIGElevator::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bVisualsConfigured)
	{
		SetActorTickEnabled(false);
		return;
	}

	// --- 칸 움직임 ----------------------------------------------------------
	if (bDropping)
	{
		// 줄이 미끄러진다. 중력처럼 붙다가 제동이 잡는다.
		TravelSpeed = FMath::Min(TravelSpeed + 980.0f * DeltaSeconds, 420.0f);
		const float Step = FMath::Min(TravelSpeed * DeltaSeconds, DropRemaining);
		CabZ -= Step;
		DropRemaining -= Step;
		if (DropRemaining <= 0.01f)
		{
			bDropping = false;
			bMoving = false;
			TravelSpeed = 0.0f;
			StopMotorHum();
			JoltVelocity = -60.0f;
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateBreakerThrow(this),
				CabRoot->GetComponentLocation() + FVector(0.0f, 0.0f, AIGElevator::CabHeight + 40.0f), 1.0f, 0.45f, 200.0f, 2400.0f);
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateDoorThud(this),
				CabRoot->GetComponentLocation(), 0.9f, 0.6f, 200.0f, 2400.0f);
			if (AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(GetPlayerPawn()))
			{
				if (IsPawnInsideCab(Character))
				{
					Character->PlayScareKick(1.6f);
				}
			}
		}
	}
	else if (bMoving)
	{
		AdvanceTravel(DeltaSeconds);
	}
	// 출렁임: 스프링으로 되돌아온다.
	if (FMath::Abs(JoltVelocity) > 0.01f || FMath::Abs(JoltZ) > 0.01f)
	{
		const float Spring = -60.0f * JoltZ - 9.0f * JoltVelocity;
		JoltVelocity += Spring * DeltaSeconds;
		JoltZ += JoltVelocity * DeltaSeconds;
		if (FMath::Abs(JoltVelocity) < 0.05f && FMath::Abs(JoltZ) < 0.02f)
		{
			JoltVelocity = 0.0f;
			JoltZ = 0.0f;
		}
	}
	CabOffsetZ = JoltZ - SagZ;
	ApplyCabTransform();

	// --- 문 ---------------------------------------------------------------
	if (!FMath::IsNearlyEqual(DoorOpenAlpha, DoorTargetAlpha))
	{
		if (DoorTargetAlpha < DoorOpenAlpha && IsDoorwayObstructed() && !bScripted)
		{
			// 닫히다가 사람이 걸렸다. 다시 연다.
			SetPhase(EIGElevatorPhase::DoorsOpening);
		}
		DoorOpenAlpha = FMath::FInterpConstantTo(DoorOpenAlpha, DoorTargetAlpha, DeltaSeconds, DoorSpeed);
		ApplyDoors();
		if (FMath::IsNearlyEqual(DoorOpenAlpha, DoorTargetAlpha))
		{
			if (Phase == EIGElevatorPhase::DoorsOpening)
			{
				SetPhase(EIGElevatorPhase::DoorsOpen);
			}
			else if (Phase == EIGElevatorPhase::DoorsClosing)
			{
				Phase = EIGElevatorPhase::Idle;
			}
		}
	}
	else if (bMoving || bDropping)
	{
		ApplyDoors();
	}
	if (Phase == EIGElevatorPhase::DoorsOpen && !bScripted)
	{
		DoorHoldSeconds -= DeltaSeconds;
		if (DoorHoldSeconds <= 0.0f)
		{
			if (IsDoorwayObstructed())
			{
				DoorHoldSeconds = IGElevator::DoorHoldAfterCloseSeconds;
			}
			else
			{
				SetPhase(EIGElevatorPhase::DoorsClosing);
			}
		}
	}
	if (Phase == EIGElevatorPhase::Idle && !bScripted && !bHourDead && HasPendingCall())
	{
		ServeNextCall();
	}

	UpdateDisplays();
	UpdateCabLightFlicker(DeltaSeconds);
	UpdateMirror(DeltaSeconds);
	if (CanSleep())
	{
		SetActorTickEnabled(false);
	}
}

// --- 버튼 ---------------------------------------------------------------

bool AIGElevator::CanPress(const EIGElevatorButtonKind Kind, const int32 Floor, const AActor* Presser) const
{
	const APawn* Pawn = Cast<APawn>(Presser);
	if (!Pawn)
	{
		return false;
	}
	if (Kind == EIGElevatorButtonKind::HallCall)
	{
		// 칸 안에서 밖의 호출판을 누르지 않는다.
		return !IsPawnInsideCab(Pawn);
	}
	// 조작반은 칸 안에서만, 또는 문이 열려 있을 때 문턱에서 손을 뻗어서.
	return IsPawnInsideCab(Pawn) || !AreDoorsClosed();
}

FText AIGElevator::GetButtonPrompt(const EIGElevatorButtonKind Kind, const int32 Floor) const
{
	switch (Kind)
	{
	case EIGElevatorButtonKind::HallCall:
		return NSLOCTEXT("IGElevator", "CallPrompt", "엘리베이터 부르기");
	case EIGElevatorButtonKind::Open:
		return NSLOCTEXT("IGElevator", "OpenPrompt", "열림 버튼 누르기");
	case EIGElevatorButtonKind::Close:
		return NSLOCTEXT("IGElevator", "ClosePrompt", "닫힘 버튼 누르기");
	case EIGElevatorButtonKind::Alarm:
		return NSLOCTEXT("IGElevator", "AlarmPrompt", "비상 버튼 누르기");
	default:
		break;
	}
	if (Floor == -1)
	{
		return NSLOCTEXT("IGElevator", "BasementPrompt", "B1 누르기");
	}
	return FText::Format(NSLOCTEXT("IGElevator", "FloorPrompt", "{0}층 누르기"), FText::AsNumber(Floor + 1));
}

void AIGElevator::ReportPressNoise(const FVector& Location, AActor* Presser)
{
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(Location, IGElevator::CallLoudness, Presser);
		}
	}
}

void AIGElevator::PlayButtonClick(const FVector& Location, const bool bAccepted)
{
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateSwitchClick(this, bAccepted),
		Location, 0.32f, bAccepted ? 1.0f : 0.8f, 60.0f, 500.0f);
	if (bAccepted)
	{
		IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateScannerBeep(this),
			Location, 0.22f, 0.7f, 60.0f, 500.0f);
	}
}

void AIGElevator::Press(const EIGElevatorButtonKind Kind, const int32 Floor, AActor* Presser)
{
	Wake();
	using namespace IGElevator;
	const FVector Location = Kind == EIGElevatorButtonKind::HallCall
		? GetHallCallWorldLocation(Floor)
		: CabRoot->GetComponentTransform().TransformPosition(FVector(CopX - 1.0f, CopY, CopBottomZ + 50.0f));
	// 버튼 소리는 그 버튼판에서 난다. 그 시간에도 눌린 소리는 났다.
	ReportPressNoise(Location, Presser);
	OnButtonPressed.Broadcast(Kind, Floor);

	if (Kind == EIGElevatorButtonKind::Alarm)
	{
		// 비상 벨은 배터리로 운다. 그 시간에도 대본 중에도 운다.
		IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateAlarmFirstNote(this),
			Location, 0.9f, 1.0f, 150.0f, 2600.0f);
		if (UWorld* World = GetWorld())
		{
			if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
			{
				Noise->ReportNoise(Location, AlarmLoudness, Presser);
			}
		}
		if (!bScripted && !bHourDead && !bAlarmAnswered)
		{
			// 낮에는 관리실이 받는다. 한 번만, 짧게. 목소리는 합성하지 않는다.
			bAlarmAnswered = true;
			GetWorldTimerManager().SetTimer(AlarmReplyTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				AIGHorrorHUD::PushDialogue(this,
					NSLOCTEXT("IGElevator", "IntercomSpeaker", "인터폰"),
					NSLOCTEXT("IGElevator", "IntercomReply", "관리실이에요. …무슨 일이에요? 고장 아니면 누르지 마세요."),
					EIGDialogueChannel::Device, 4.0f);
			}), 2.2f, false);
		}
		return;
	}

	if (bHourDead)
	{
		PlayButtonClick(Location, false);
		if (!bHourDeadThoughtShown)
		{
			bHourDeadThoughtShown = true;
			AIGHorrorHUD::PushThought(this,
				NSLOCTEXT("IGElevator", "HourDeadThought", "버튼에 불이 안 들어오네."), 2.6f);
		}
		return;
	}
	if (bScripted)
	{
		// 대본 중의 버튼은 눌린 소리만 난다. 무엇이 일어날지는 대본이 정한다.
		PlayButtonClick(Location, false);
		return;
	}

	switch (Kind)
	{
	case EIGElevatorButtonKind::HallCall:
	{
		PlayButtonClick(Location, true);
		if (!bMoving && CurrentLanding == Floor && FMath::Abs(CabZ - GetLandingZ(Floor)) < 1.0f)
		{
			if (Phase == EIGElevatorPhase::DoorsOpen)
			{
				DoorHoldSeconds = IGElevator::DoorHoldSeconds;
			}
			else if (Phase != EIGElevatorPhase::DoorsOpening)
			{
				SetPhase(EIGElevatorPhase::DoorsOpening);
			}
			return;
		}
		PendingCalls |= (1u << Floor);
		RefreshButtonLights();
		return;
	}
	case EIGElevatorButtonKind::Floor:
	{
		if (Floor < 0 || Floor >= LandingCount)
		{
			// B1과 5. 눌리기는 하는데 불이 잠깐 들어왔다 꺼진다.
			PlayButtonClick(Location, false);
			const int32 LampIndex = Floor < 0 ? 5 : 4;
			if (ButtonLamps.IsValidIndex(LampIndex))
			{
				ScriptLitButtons |= (1u << LampIndex);
				RefreshButtonLights();
				FTimerHandle Blink;
				GetWorldTimerManager().SetTimer(Blink, FTimerDelegate::CreateWeakLambda(this, [this, LampIndex]()
				{
					ScriptLitButtons &= ~(1u << LampIndex);
					RefreshButtonLights();
				}), 0.35f, false);
			}
			if (Floor >= LandingCount && !bFiveThoughtShown)
			{
				bFiveThoughtShown = true;
				AIGHorrorHUD::PushThought(this, NSLOCTEXT("IGElevator", "FiveThought", "5가 왜 있지."), 2.6f);
			}
			return;
		}
		PlayButtonClick(Location, true);
		if (!bMoving && CurrentLanding == Floor && FMath::Abs(CabZ - GetLandingZ(Floor)) < 1.0f)
		{
			if (Phase != EIGElevatorPhase::DoorsOpening && Phase != EIGElevatorPhase::DoorsOpen)
			{
				SetPhase(EIGElevatorPhase::DoorsOpening);
			}
			return;
		}
		PendingCalls |= (1u << Floor);
		RefreshButtonLights();
		// 문이 열려 있으면 잠깐 뒤 닫고 떠난다.
		if (Phase == EIGElevatorPhase::DoorsOpen)
		{
			DoorHoldSeconds = FMath::Min(DoorHoldSeconds, 1.6f);
		}
		return;
	}
	case EIGElevatorButtonKind::Open:
		PlayButtonClick(Location, true);
		if (!bMoving && GetStoppedLanding() != INDEX_NONE)
		{
			if (Phase == EIGElevatorPhase::DoorsOpen)
			{
				DoorHoldSeconds = IGElevator::DoorHoldSeconds;
			}
			else
			{
				SetPhase(EIGElevatorPhase::DoorsOpening);
			}
		}
		return;
	case EIGElevatorButtonKind::Close:
		PlayButtonClick(Location, true);
		if (Phase == EIGElevatorPhase::DoorsOpen)
		{
			DoorHoldSeconds = FMath::Min(DoorHoldSeconds, 0.2f);
		}
		return;
	default:
		return;
	}
}

void AIGElevator::RefreshButtonLights()
{
	for (int32 Index = 0; Index < ButtonLamps.Num(); ++Index)
	{
		UStaticMeshComponent* Lamp = ButtonLamps[Index];
		if (!Lamp)
		{
			continue;
		}
		const bool bCall = Index < LandingCount && (PendingCalls & (1u << Index)) != 0;
		const bool bLit = !bHourDead && (bCall || (ScriptLitButtons & (1u << Index)) != 0);
		if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Lamp->GetMaterial(0)))
		{
			Material->SetScalarParameterValue(TEXT("EmissiveStrength"), bLit ? 6.0f : 0.0f);
		}
	}
}

void AIGElevator::SetHourDead(const bool bDead)
{
	Wake();
	bHourDead = bDead;
	if (bDead)
	{
		// 그 시간에는 선 자리에서 문을 닫고 멈춘다. 부르던 층은 잊는다.
		PendingCalls = 0;
		if (!bScripted)
		{
			DoorTargetAlpha = 0.0f;
			DoorSpeed = 1.0f / IGElevator::DoorSeconds;
			Phase = EIGElevatorPhase::DoorsClosing;
		}
		StopMotorHum();
	}
	SetCabLightLevel(bDead ? 0.0f : 1.0f);
	RefreshButtonLights();
	UpdateDisplays();
}

void AIGElevator::ResetForNewRide()
{
	// 탄 채로 장이 바뀌면 칸을 1층으로 끌어내리지 않는다. 가까운 층에 세우고 문을 연다.
	const APawn* Pawn = GetPlayerPawn();
	const bool bRiderInside = Pawn && IsPawnInsideCab(Pawn);
	SetScriptedControl(false);
	PendingCalls = 0;
	bMoving = false;
	bDropping = false;
	TravelSpeed = 0.0f;
	JoltZ = 0.0f;
	JoltVelocity = 0.0f;
	SagZ = 0.0f;
	CurrentLanding = bRiderInside
		? FMath::Clamp(FMath::RoundToInt(CabZ / StoreyHeight), 0, LandingCount - 1)
		: 0;
	CabZ = GetLandingZ(CurrentLanding);
	DoorOpenAlpha = 0.0f;
	DoorTargetAlpha = 0.0f;
	Phase = EIGElevatorPhase::Idle;
	if (bRiderInside && !bHourDead)
	{
		SetPhase(EIGElevatorPhase::DoorsOpening);
	}
	StopMotorHum();
	PlayOverloadBuzzer(false);
	ScriptSetFullLamp(false);
	ClearReflectionFigures();
	SetReflection(EIGElevatorReflection::Normal);
	ApplyCabTransform();
	ApplyDoors();
	RefreshButtonLights();
	UpdateDisplays();
	Wake();
}

// --- 표시와 등 ---------------------------------------------------------

void AIGElevator::SetDisplayText(const FText& Text)
{
	for (UTextRenderComponent* Display : CabDisplays)
	{
		if (Display)
		{
			Display->SetText(Text);
		}
	}
	for (UTextRenderComponent* Display : HallDisplays)
	{
		if (Display)
		{
			Display->SetText(Text);
		}
	}
}

void AIGElevator::UpdateDisplays()
{
	if (bDisplayOverride)
	{
		return;
	}
	if (bHourDead)
	{
		SetDisplayText(FText::GetEmpty());
		LastShownFloor = -1;
		return;
	}
	const int32 Floor = FMath::Clamp(FMath::RoundToInt(CabZ / StoreyHeight), 0, LandingCount) + 1;
	if (Floor != LastShownFloor)
	{
		LastShownFloor = Floor;
		SetDisplayText(FText::AsNumber(Floor));
	}
}

void AIGElevator::SetCabLightLevel(const float Level)
{
	CabLightLevel = FMath::Clamp(Level, 0.0f, 1.0f);
	if (CabLight)
	{
		CabLight->SetIntensity(520.0f * CabLightLevel);
	}
	if (FloorFill)
	{
		FloorFill->SetIntensity(36.0f * CabLightLevel);
	}
	if (CeilingMaterial)
	{
		CeilingMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"), 3.0f * CabLightLevel);
	}
}

void AIGElevator::UpdateCabLightFlicker(const float DeltaSeconds)
{
	if (!bCabLightFlicker)
	{
		return;
	}
	FlickerClock += DeltaSeconds;
	// 안정기가 죽어 가는 형광등. 대부분 켜져 있다가 가끔 뚝 끊긴다. 점멸 감소를 켠
	// 사람에게는 끊김 없이 한 단 어두운 등으로만 보인다.
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGAccessibilitySubsystem* Accessibility = GameInstance
		? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>() : nullptr;
	const bool bReducedFlicker = Accessibility && Accessibility->IsReducedFlickerEnabled();
	const float Wave = FMath::Sin(FlickerClock * 37.0f) * FMath::Sin(FlickerClock * 5.3f + 1.1f);
	const float Level = bReducedFlicker ? 0.7f : (Wave > 0.82f ? 0.08f : (Wave > 0.6f ? 0.55f : 1.0f));
	const float Base = CabLightLevel;
	if (CabLight)
	{
		CabLight->SetIntensity(520.0f * Base * Level);
	}
	if (CeilingMaterial)
	{
		CeilingMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"), 3.0f * Base * Level);
	}
}

void AIGElevator::UpdateLightCulling()
{
	// 칸 등은 그림자를 드리우는 이동 광원이다. 두 층 넘게 떨어진 곳에서는 문틈으로도
	// 안 보이니 끈다. 바로 위아래 층은 둔다. 승강로 너머로 소리와 함께 빛이 새는 자리다.
	const APawn* Pawn = GetPlayerPawn();
	const bool bNear = !Pawn || !CabRoot
		|| FMath::Abs(Pawn->GetActorLocation().Z - CabRoot->GetComponentLocation().Z) < 420.0f;
	if (CabLight && CabLight->IsVisible() != bNear)
	{
		CabLight->SetVisibility(bNear);
	}
	if (FloorFill && FloorFill->IsVisible() != bNear)
	{
		FloorFill->SetVisibility(bNear);
	}
}

// --- 소리 ------------------------------------------------------------------

void AIGElevator::StartMotorHum()
{
	if (MotorHum && !MotorHum->IsPlaying())
	{
		MotorHum->SetVolumeMultiplier(1.0f);
		MotorHum->FadeIn(0.4f, 0.75f);
	}
}

void AIGElevator::StopMotorHum()
{
	if (MotorHum && MotorHum->IsPlaying())
	{
		MotorHum->FadeOut(0.5f, 0.0f);
	}
}

void AIGElevator::PlayDoorMotor(const bool bOpening)
{
	if (!CabRoot)
	{
		return;
	}
	const FVector Location = CabRoot->GetComponentTransform().TransformPosition(FVector(0.0f, IGElevator::CarDoorY, 200.0f));
	// 문 모터의 낮은 휘이와 끝에 닿는 고무 소리.
	TArray<FIGToneNote> Notes;
	Notes.Add({0.0f, IGElevator::DoorSeconds, bOpening ? 180.0f : 160.0f, 0.06f, 0.15f, 0.8f, EIGToneWaveform::SoftSquare});
	Notes.Add({0.0f, IGElevator::DoorSeconds, 1400.0f, 0.012f, 0.2f, 0.8f, EIGToneWaveform::ValueNoise});
	Notes.Add({IGElevator::DoorSeconds - 0.06f, 0.12f, 95.0f, 0.12f, 0.01f, 3.0f, EIGToneWaveform::Sine});
	UIGToneSequenceSoundWave* Motor = NewObject<UIGToneSequenceSoundWave>(this);
	Motor->ConfigureNotes(MoveTemp(Notes), false);
	IGAudio::SpawnOneShotAt(this, Motor, Location, 0.5f, 1.0f, 120.0f, 1000.0f);
}

void AIGElevator::PlayChime(const int32 Landing)
{
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateDoorChime(this),
		GetActorTransform().TransformPosition(FVector(0.0f, IGElevator::LandingWallY, GetLandingZ(Landing) + 226.0f)),
		0.5f, 1.0f, 150.0f, 1400.0f);
}

void AIGElevator::PlayOverloadBuzzer(const bool bOn)
{
	Wake();
	if (!Buzzer)
	{
		return;
	}
	if (bOn && !Buzzer->IsPlaying())
	{
		Buzzer->SetVolumeMultiplier(1.0f);
		Buzzer->Play();
	}
	else if (!bOn && Buzzer->IsPlaying())
	{
		Buzzer->Stop();
	}
}

void AIGElevator::PlayCableCreak(const float Volume)
{
	if (!CabRoot)
	{
		return;
	}
	IGAudio::SpawnOneShotAt(this,
		IGAudio::SampleVariantOr(TEXT("Settle_Creak"), 2, static_cast<uint32>(FMath::Rand()),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSettleTimberCreak(this); }),
		CabRoot->GetComponentLocation() + FVector(0.0f, 0.0f, AIGElevator::CabHeight + 30.0f),
		Volume, 0.55f, 120.0f, 1400.0f);
}

// --- 대본 제어 -------------------------------------------------------------

void AIGElevator::SetScriptedControl(const bool bInScripted)
{
	Wake();
	bScripted = bInScripted;
	if (!bScripted)
	{
		bDisplayOverride = false;
		bCabLightFlicker = false;
		ScriptLitButtons = 0;
		SetCabLightLevel(bHourDead ? 0.0f : 1.0f);
		RefreshButtonLights();
		UpdateDisplays();
	}
}

void AIGElevator::ScriptTravelTo(const float TargetZ, const float MaxSpeed, const float InAccel)
{
	Wake();
	if (FMath::IsNearlyEqual(TargetZ, CabZ, 0.5f))
	{
		return;
	}
	BeginTravel(TargetZ, MaxSpeed, InAccel);
}

void AIGElevator::ScriptHalt(const float JoltCm)
{
	Wake();
	if (bMoving)
	{
		bMoving = false;
		TravelSpeed = 0.0f;
		StopMotorHum();
	}
	Phase = EIGElevatorPhase::Idle;
	JoltVelocity = -FMath::Abs(JoltCm) * 6.0f;
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateRelayClick(this),
		CabRoot->GetComponentLocation() + FVector(0.0f, 0.0f, AIGElevator::CabHeight + 30.0f), 0.8f, 0.5f, 120.0f, 1400.0f);
}

void AIGElevator::ScriptDrop(const float DropCm)
{
	Wake();
	bMoving = true;
	bDropping = true;
	DropRemaining = FMath::Max(DropCm, 1.0f);
	TravelSpeed = 0.0f;
	Phase = EIGElevatorPhase::Travelling;
}

void AIGElevator::ScriptSetDoors(const bool bOpen, const float Seconds)
{
	Wake();
	DoorTargetAlpha = bOpen ? 1.0f : 0.0f;
	DoorSpeed = 1.0f / FMath::Max(Seconds, 0.1f);
	Phase = bOpen ? EIGElevatorPhase::DoorsOpening : EIGElevatorPhase::DoorsClosing;
	PlayDoorMotor(bOpen);
}

void AIGElevator::ScriptSetDisplay(const FText& Text)
{
	bDisplayOverride = true;
	SetDisplayText(Text);
}

void AIGElevator::ScriptClearDisplay()
{
	bDisplayOverride = false;
	LastShownFloor = -1;
	UpdateDisplays();
}

void AIGElevator::ScriptSetFullLamp(const bool bLit)
{
	bFullLampLit = bLit;
	if (FullLampMaterial)
	{
		FullLampMaterial->SetScalarParameterValue(TEXT("EmissiveStrength"), bLit ? 8.0f : 0.0f);
	}
}

void AIGElevator::ScriptSetCabLight(const float Level, const bool bFlicker)
{
	Wake();
	bCabLightFlicker = bFlicker;
	SetCabLightLevel(Level);
}

void AIGElevator::ScriptSetButtonLit(const int32 Floor, const bool bLit)
{
	const int32 Index = Floor < 0 ? 5 : FMath::Clamp(Floor, 0, 4);
	if (bLit)
	{
		ScriptLitButtons |= (1u << Index);
	}
	else
	{
		ScriptLitButtons &= ~(1u << Index);
	}
	RefreshButtonLights();
}

void AIGElevator::ScriptSetSag(const float SagCm)
{
	Wake();
	SagZ = FMath::Max(0.0f, SagCm);
}

// --- 거울 ------------------------------------------------------------------

void AIGElevator::SetReflection(const EIGElevatorReflection Mode)
{
	Wake();
	Reflection = Mode;
}

void AIGElevator::PlayReflectionLookBack()
{
	Wake();
	if (ReflectionBody && ReflectionLookBack)
	{
		ReflectionBody->PlayAnimation(ReflectionLookBack, false);
		ReflectionLookBackSeconds = ReflectionLookBack->GetPlayLength();
		bReflectionWalking = false;
	}
}

USkeletalMeshComponent* AIGElevator::AddReflectionFigure(const FVector& CabLocal, const float Yaw)
{
	if (!ReflectionMesh)
	{
		return nullptr;
	}
	USkeletalMeshComponent* Figure = NewObject<USkeletalMeshComponent>(this,
		*FString::Printf(TEXT("ReflectionFigure_%d"), ComponentCounter++));
	Figure->SetupAttachment(CabRoot);
	Figure->SetMobility(EComponentMobility::Movable);
	Figure->SetSkeletalMeshAsset(ReflectionMesh);
	Figure->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Figure->SetCastShadow(false);
	Figure->bVisibleInSceneCaptureOnly = true;
	Figure->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Figure->SetRelativeLocationAndRotation(CabLocal, FRotator(0.0f, Yaw, 0.0f));
	Figure->RegisterComponent();
	if (ReflectionIdle)
	{
		Figure->PlayAnimation(ReflectionIdle, true);
		// 같은 동작이 한 박자에 맞춰 숨 쉬면 인형처럼 보인다. 위상을 흩는다.
		Figure->SetPosition(FMath::FRandRange(0.0f, ReflectionIdle->GetPlayLength()), false);
	}
	ReflectionFigures.Add(Figure);
	return Figure;
}

void AIGElevator::ClearReflectionFigures()
{
	for (USkeletalMeshComponent* Figure : ReflectionFigures)
	{
		if (Figure)
		{
			Figure->DestroyComponent();
		}
	}
	ReflectionFigures.Reset();
}

void AIGElevator::UpdateReflectionBody(const APawn* Pawn, const FVector& EyeLocation, const float DeltaSeconds)
{
	if (!ReflectionBody)
	{
		return;
	}
	const bool bShow = Pawn && Reflection != EIGElevatorReflection::Absent;
	ReflectionBody->SetVisibility(bShow);
	if (!bShow)
	{
		return;
	}
	// 몸은 시선 방향을 본다. 뒷모습 모드는 반대로 선다.
	const FRotator View = Pawn->GetControlRotation();
	const float Yaw = View.Yaw + (Reflection == EIGElevatorReflection::Back ? 180.0f : 0.0f);
	const FVector Feet = EyeLocation - FVector(0.0f, 0.0f, IGElevator::ReflectionEyeToFeet);
	ReflectionBody->SetWorldLocationAndRotation(Feet, FRotator(0.0f, Yaw, 0.0f));
	if (ReflectionLookBackSeconds > 0.0f)
	{
		ReflectionLookBackSeconds -= DeltaSeconds;
		return;
	}
	const bool bWalking = Pawn->GetVelocity().Size2D() > 25.0f;
	if (bWalking != bReflectionWalking)
	{
		bReflectionWalking = bWalking;
		UAnimSequence* Anim = bWalking ? ReflectionWalk.Get() : ReflectionIdle.Get();
		if (Anim)
		{
			ReflectionBody->PlayAnimation(Anim, true);
		}
	}
}

void AIGElevator::UpdateMirror(const float DeltaSeconds)
{
	using namespace IGElevator;
	const APawn* Pawn = GetPlayerPawn();
	const bool bInside = Pawn && IsPawnInsideCab(Pawn);
	const bool bShouldRun = MirrorCapture && MirrorMaterial && bInside && CabLightLevel > 0.01f;
	if (bShouldRun != bMirrorLive)
	{
		bMirrorLive = bShouldRun;
		// 캡처가 도는 동안만 거울 재질을 씌운다. 밖에서 보면 그냥 광택 스테인리스다.
		if (BackPanelMesh)
		{
			UMaterialInterface* Steel = BackPanelSteel.Get();
			BackPanelMesh->SetMaterial(0, bMirrorLive ? MirrorMaterial.Get() : Steel);
		}
	}
	FVector EyeLocation = Pawn ? Pawn->GetPawnViewLocation() : FVector::ZeroVector;
	float Fov = 78.0f;
	if (const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
	{
		if (Controller->PlayerCameraManager)
		{
			EyeLocation = Controller->PlayerCameraManager->GetCameraLocation();
			Fov = Controller->PlayerCameraManager->GetFOVAngle();
		}
	}
	UpdateReflectionBody(bInside ? Pawn : nullptr, EyeLocation, DeltaSeconds);
	if (!bMirrorLive)
	{
		return;
	}
	// 거울 면(칸 로컬)을 세계로. 면의 법선은 칸 안(-Y)을 본다.
	const FTransform Cab = CabRoot->GetComponentTransform();
	const FVector PlanePoint = Cab.TransformPosition(FVector(0.0f, MirrorY, (MirrorBottomZ + MirrorTopZ) * 0.5f));
	const FVector Normal = Cab.TransformVectorNoScale(FVector(0.0f, -1.0f, 0.0f));
	const float EyeDistance = FVector::DotProduct(EyeLocation - PlanePoint, Normal);
	if (EyeDistance < 2.0f)
	{
		return;
	}
	// 눈을 거울 면 너머로 접은 자리에서, 거울 면을 창으로 삼아 칸 쪽을 본다.
	const FVector MirroredEye = EyeLocation - 2.0f * EyeDistance * Normal;
	const FVector Forward = Normal;
	const FVector Up = Cab.TransformVectorNoScale(FVector::UpVector);
	const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
	MirrorCapture->SetWorldLocationAndRotation(MirroredEye, FRotationMatrix::MakeFromXZ(Forward, Up).Rotator());

	// 거울 네 귀퉁이를 카메라 공간(오른쪽 X, 위 Y, 앞 Z)에 놓고 비대칭 원뿔을 짠다.
	// 가까운 면을 거울 면에 두면 거울 뒤(승강로 벽, 칸 뒷벽)는 저절로 잘린다.
	float Left = TNumericLimits<float>::Max();
	float RightEdge = -TNumericLimits<float>::Max();
	float Bottom = TNumericLimits<float>::Max();
	float Top = -TNumericLimits<float>::Max();
	const float Near = EyeDistance;
	for (const FVector& Corner : {
		FVector(-MirrorHalfWidth, MirrorY, MirrorBottomZ), FVector(MirrorHalfWidth, MirrorY, MirrorBottomZ),
		FVector(-MirrorHalfWidth, MirrorY, MirrorTopZ), FVector(MirrorHalfWidth, MirrorY, MirrorTopZ)})
	{
		const FVector Offset = Cab.TransformPosition(Corner) - MirroredEye;
		const float Depth = FVector::DotProduct(Offset, Forward);
		const float X = FVector::DotProduct(Offset, Right) * Near / FMath::Max(Depth, 1.0f);
		const float Y = FVector::DotProduct(Offset, Up) * Near / FMath::Max(Depth, 1.0f);
		Left = FMath::Min(Left, X);
		RightEdge = FMath::Max(RightEdge, X);
		Bottom = FMath::Min(Bottom, Y);
		Top = FMath::Max(Top, Y);
	}
	const float Width = FMath::Max(RightEdge - Left, 0.01f);
	const float Height = FMath::Max(Top - Bottom, 0.01f);
	MirrorCapture->CustomProjectionMatrix = FMatrix(
		FPlane(2.0f * Near / Width, 0.0f, 0.0f, 0.0f),
		FPlane(0.0f, 2.0f * Near / Height, 0.0f, 0.0f),
		FPlane(-(RightEdge + Left) / Width, -(Top + Bottom) / Height, 0.0f, 1.0f),
		FPlane(0.0f, 0.0f, Near, 0.0f));
	MirrorCapture->FOVAngle = Fov;
	// 손전등 빛은 장면 조명이라 캡처에도 그대로 들어온다. 30번에 한 번이 아니라 매 프레임이어야
	// 고개를 돌릴 때 반사가 끌리지 않는다. 해상도가 낮아 값이 싸다.
	MirrorCapture->CaptureScene();
}
