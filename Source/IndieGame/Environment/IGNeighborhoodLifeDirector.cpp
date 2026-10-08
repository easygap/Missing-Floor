#include "Environment/IGNeighborhoodLifeDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"
#include "TimerManager.h"

namespace IGNeighborhoodLife
{
	constexpr int32 ScooterPoolSize = 2;
	constexpr int32 LeafPoolSize = 24;
	constexpr float SoundSpeedCentimetersPerSecond = 34300.0f;

	constexpr uint32 NormalSeedSalt = 0x23A16E41u;

	// 125cc 배달 스쿠터와 라이더. 차체 중심에서 앞뒤 끝까지, 옆면까지(cm).
	constexpr float ScooterHalfLength = 95.0f;
	constexpr float ScooterHalfWidth = 36.0f;
	// 몸과 차체 사이에 남기는 여유. 이보다 가까우면 지나갈 수 없다고 본다.
	constexpr float PassMargin = 10.0f;
	// 첫 외출의 스침. 몸 중심에서 차체 중심까지 92 cm면 어깨에서 손 한 뼘이다.
	constexpr float NearMissCenterDistance = 92.0f;
	constexpr float CruiseSpeed = 950.0f;
	constexpr float Acceleration = 520.0f;
	constexpr float BrakeDeceleration = 950.0f;
	// 원호에서 낼 수 있는 옆 가속. 배달 라이더가 몸을 눕혀 도는 정도다.
	constexpr float CorneringAcceleration = 900.0f;
	constexpr float MaxLateralSpeed = 240.0f;
	constexpr float LateralAcceleration = 900.0f;
	// 급정거 때 코와 몸 사이에 남는 거리.
	constexpr float StopNoseGap = 45.0f;
	constexpr float LookAheadDistance = 1400.0f;
	constexpr float BlockingSpeed = 160.0f;

	/** 골목의 걸리는 것들. 회피가 차체를 이 원 안으로 보내지 않는다(cm). */
	struct FAlleyObstacle
	{
		float X;
		float Y;
		float Radius;
	};
	constexpr FAlleyObstacle AlleyObstacles[] = {
		{500.0f, -422.0f, 12.0f},   // 전신주
		{1600.0f, -422.0f, 12.0f},  // 전신주
		{1180.0f, -430.0f, 16.0f},  // 라바콘
		{1100.0f, -411.0f, 30.0f},  // 실외기
		{150.0f, -640.0f, 17.0f},   // 보안등 기초
		{1000.0f, -640.0f, 17.0f},
		{1850.0f, -640.0f, 17.0f},
		{330.0f, -645.0f, 17.0f},   // 병 상자
		{362.0f, -643.0f, 17.0f},
		{1445.0f, -644.0f, 30.0f},  // 과자 판매대
	};
	// 연석 면. 차체 중심은 여기서 차체 반폭만큼 안쪽에만 선다.
	constexpr float AlleyNorthCurbFace = -418.0f;
	constexpr float AlleySouthCurbFace = -657.0f;
	// 두 샛길과 필로티 기둥 사이.
	constexpr float WestPassageMinX = 1210.0f;
	constexpr float WestPassageMaxX = 1390.0f;
	constexpr float EastPassageMinX = 1640.0f;
	constexpr float EastPassageMaxX = 1840.0f;
	// 뒤편 배송 골목. 상가 뒷벽(Y -1240)과 뒷담(Y -1480) 사이다.
	constexpr float BackAlleyNorthFace = -1240.0f;
	constexpr float BackAlleySouthFace = -1480.0f;

	constexpr float NearMissLaneY = -545.0f;
	// 오토바이를 세워 두는 자리. 공동현관 서쪽, 전신주 앞 연석 옆이다.
	constexpr float ParkingX = 390.0f;
	constexpr float ParkingY = -472.0f;
	constexpr const TCHAR* NearMissBeat = TEXT("Neighborhood.ScooterNearMiss");
	// 첫 외출의 오토바이를 기다리는 한도. 골목으로 안 나오면 접고 평소대로 다니게 한다.
	constexpr double NearMissArmedTimeoutSeconds = 60.0;
}

AIGNeighborhoodLifeDirector::AIGNeighborhoodLifeDirector()
{
	// Per-frame work exists only while a pooled pass, gust, leaf or cat trace
	// is active. Vehicles therefore stay smooth without an always-on tick.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("NeighborhoodLifeRoot"));
	SetRootComponent(SceneRoot);
}

void AIGNeighborhoodLifeDirector::BeginPlay()
{
	Super::BeginPlay();
	InitializePools();
	RestartDeterministicSchedule();
}

void AIGNeighborhoodLifeDirector::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateRuntime(FMath::Clamp(DeltaSeconds, 0.0f, 0.1f));
}

void AIGNeighborhoodLifeDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	StopAllRuntimeEvents();
	Super::EndPlay(EndPlayReason);
}

void AIGNeighborhoodLifeDirector::ConfigureNeighborhood(
	const FVector InRoadStart,
	const FVector InRoadEnd,
	const int32 InDeterministicSeed)
{
	if (!InRoadStart.Equals(InRoadEnd, 10.0f))
	{
		RoadStart = InRoadStart;
		RoadEnd = InRoadEnd;
	}
	DeterministicSeed = InDeterministicSeed != 0 ? InDeterministicSeed : 4040444;

	if (HasActorBegunPlay())
	{
		InitializePools();
		RestartDeterministicSchedule();
	}
}

void AIGNeighborhoodLifeDirector::PrimeOutdoorSequence()
{
	// The street-level trigger may overlap again while the pawn is still
	// crossing its boundary.  Re-arming here would restart the cat/wind/
	// scooter sequence and turn ordinary neighbourhood sound into a loop.
	if (!GetWorld() || bOutdoorSequencePrimed)
	{
		return;
	}

	bOutdoorSequencePrimed = true;
	GetWorldTimerManager().ClearTimer(CatScheduleHandle);
	GetWorldTimerManager().ClearTimer(GustScheduleHandle);
	GetWorldTimerManager().SetTimer(
		CatScheduleHandle,
		this,
		&ThisClass::LaunchCatTrace,
		0.8f,
		false);
	GetWorldTimerManager().SetTimer(
		GustScheduleHandle,
		this,
		&ThisClass::LaunchWindGust,
		3.4f,
		false);

	if (HasNearMissPlayed())
	{
		return;
	}
	// 오토바이는 시간으로 띄우지 않는다. 그녀가 골목을 어디까지 걸어왔는지를 보고
	// 띄워야 샛길에서 튀어나오는 순간이 늘 몇 걸음 앞이다.
	bNearMissArmed = true;
	NearMissArmedSeconds = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().ClearTimer(ScooterScheduleHandle);
	GetWorldTimerManager().SetTimer(
		NearMissPollHandle,
		this,
		&ThisClass::PollNearMissTrigger,
		0.1f,
		true);
}

void AIGNeighborhoodLifeDirector::InitializePools()
{
	if (bPoolsInitialized || !GetWorld())
	{
		return;
	}

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	AlleyCatMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_AlleyCatRun.SM_AlleyCatRun"));
	// 생성 메시 두 장(DeliveryScooter_20261002·DeliveryScooterParked_20261007을
	// TRELLIS.2로 뽑아 Blender에서 다듬었다). 기사와 배달통이 메시에 들어 있다.
	ScooterMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_DeliveryScooter.SM_DeliveryScooter"));
	RiddenScooterMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_DeliveryScooterRidden.SM_DeliveryScooterRidden"));
	DarkMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_PlasticDark.M_PlasticDark"));
	LeafMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_Cardboard.M_Cardboard"));
	AlleyCatMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Prototype/Materials/M_AlleyCatTabbyUV.M_AlleyCatTabbyUV"));

	if (!CubeMesh || !SphereMesh)
	{
		return;
	}

	auto ConfigureVisual = [this](
		UStaticMeshComponent* Component,
		USceneComponent* Parent,
		UStaticMesh* Mesh,
		UMaterialInterface* Material)
	{
		Component->SetupAttachment(Parent ? Parent : SceneRoot.Get());
		Component->SetStaticMesh(Mesh);
		if (Material)
		{
			Component->SetMaterial(0, Material);
		}
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(true);
		Component->SetHiddenInGame(true);
		Component->RegisterComponent();
	};

	for (int32 SlotIndex = 0; SlotIndex < IGNeighborhoodLife::ScooterPoolSize; ++SlotIndex)
	{
		USceneComponent* Root = NewObject<USceneComponent>(
			this, *FString::Printf(TEXT("ScooterRoot_%d"), SlotIndex));
		Root->SetupAttachment(SceneRoot);
		Root->SetMobility(EComponentMobility::Movable);
		Root->RegisterComponent();
		ScooterRoots.Add(Root);

		// 뿌리는 바퀴가 닿는 바닥 한가운데, 앞이 +X다. 두 메시도 같은 원점이다.
		// 달리는 동안은 기사가 탄 메시, 내린 뒤에는 세워 둔 메시로 바꿔 끼운다(SetScooterVisible).
		UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(
			this, *FString::Printf(TEXT("ScooterBody_%d"), SlotIndex));
		ConfigureVisual(Body, Root, RiddenScooterMesh, nullptr);
		ScooterBodies.Add(Body);

		// 전조등. 샛길에서 나오기 전에 빛이 먼저 맞은편 벽을 쓸고 지나간다.
		USpotLightComponent* Headlight = NewObject<USpotLightComponent>(
			this, *FString::Printf(TEXT("ScooterHeadlight_%d"), SlotIndex));
		Headlight->SetupAttachment(Root);
		Headlight->SetMobility(EComponentMobility::Movable);
		Headlight->SetRelativeLocation(FVector(82.0f, 0.0f, 96.0f));
		Headlight->SetRelativeRotation(FRotator(-5.0f, 0.0f, 0.0f));
		Headlight->SetIntensity(2600.0f);
		Headlight->SetAttenuationRadius(2400.0f);
		Headlight->SetInnerConeAngle(12.0f);
		Headlight->SetOuterConeAngle(28.0f);
		Headlight->SetLightColor(FLinearColor(1.0f, 0.97f, 0.90f));
		// 한 화면에 그림자 광원을 늘리지 않는다. 빛줄기만으로 충분히 읽힌다.
		Headlight->SetCastShadows(false);
		Headlight->SetVolumetricScatteringIntensity(0.8f);
		Headlight->SetMaxDrawDistance(3200.0f);
		Headlight->SetVisibility(false);
		Headlight->RegisterComponent();
		ScooterHeadlights.Add(Headlight);

		UPointLightComponent* TailLight = NewObject<UPointLightComponent>(
			this, *FString::Printf(TEXT("ScooterTailLight_%d"), SlotIndex));
		TailLight->SetupAttachment(Root);
		TailLight->SetMobility(EComponentMobility::Movable);
		TailLight->SetRelativeLocation(FVector(-80.0f, 0.0f, 74.0f));
		TailLight->SetIntensity(40.0f);
		TailLight->SetAttenuationRadius(220.0f);
		TailLight->SetLightColor(FLinearColor(1.0f, 0.08f, 0.04f));
		TailLight->SetCastShadows(false);
		TailLight->SetVisibility(false);
		TailLight->RegisterComponent();
		ScooterTailLights.Add(TailLight);

		// 서 있는 오토바이는 몸으로 밀고 지나갈 수 없다. 달리는 동안에는 끄고, 회피와
		// ResolvePlayerClearance가 몸과 차체를 떼어 놓는다. 움직이는 막힘 상자가
		// 캡슐을 뚫고 들어가면 캐릭터 이동이 엉뚱한 쪽으로 튕겨 내기 때문이다.
		UBoxComponent* Blocker = NewObject<UBoxComponent>(
			this, *FString::Printf(TEXT("ScooterBlocker_%d"), SlotIndex));
		Blocker->SetupAttachment(Root);
		Blocker->SetMobility(EComponentMobility::Movable);
		Blocker->SetBoxExtent(FVector(
			IGNeighborhoodLife::ScooterHalfLength,
			IGNeighborhoodLife::ScooterHalfWidth,
			58.0f));
		Blocker->SetRelativeLocation(FVector(0.0f, 0.0f, 58.0f));
		Blocker->SetCollisionObjectType(ECC_WorldDynamic);
		Blocker->SetCollisionResponseToAllChannels(ECR_Ignore);
		Blocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Blocker->SetGenerateOverlapEvents(false);
		Blocker->SetCanEverAffectNavigation(false);
		Blocker->RegisterComponent();
		ScooterBlockers.Add(Blocker);

		UAudioComponent* Audio = CreateSpatialAudioComponent(
			Root,
			*FString::Printf(TEXT("ScooterAudio_%d"), SlotIndex),
			160.0f,
			2600.0f);
		ScooterAudio.Add(Audio);
		ScooterRuntime.AddDefaulted();
	}

	for (int32 LeafIndex = 0; LeafIndex < IGNeighborhoodLife::LeafPoolSize; ++LeafIndex)
	{
		UStaticMeshComponent* Leaf = NewObject<UStaticMeshComponent>(
			this, *FString::Printf(TEXT("WindLeaf_%d"), LeafIndex));
		// A paper-thin cube renders from both sides; Engine/BasicShapes/Plane
		// disappears whenever a one-sided material flips away from the camera.
		ConfigureVisual(Leaf, SceneRoot, CubeMesh, LeafMaterial);
		Leaf->SetCastShadow(false);
		Leaf->SetRelativeScale3D(FVector(0.055f, 0.025f, 0.003f));
		LeafMeshes.Add(Leaf);
		LeafRuntime.AddDefaulted();
	}

	CatTraceRoot = NewObject<USceneComponent>(this, TEXT("FleeingCatTraceRoot"));
	CatTraceRoot->SetupAttachment(SceneRoot);
	CatTraceRoot->SetMobility(EComponentMobility::Movable);
	CatTraceRoot->RegisterComponent();

	auto AddCatPart = [this, &ConfigureVisual](
		const TCHAR* Name,
		UStaticMesh* Mesh,
		UMaterialInterface* Material,
		const FVector& Location,
		const FVector& Scale,
		const FRotator& Rotation)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, Name);
		// TRELLIS.2에서 다듬은 고양이는 기준 시트의 줄무늬를 구운 재질을 슬롯에
		// 들고 온다. 타일 털 재질을 덮어씌우면 스마트 UV 섬마다 무늬가 끊긴다.
		const bool bKeepsOwnMaterial = Mesh == AlleyCatMesh;
		ConfigureVisual(
			Part, CatTraceRoot, Mesh,
			bKeepsOwnMaterial ? nullptr : (Material ? Material : DarkMaterial.Get()));
		Part->SetRelativeLocation(Location);
		Part->SetRelativeRotation(Rotation);
		Part->SetRelativeScale3D(Scale);
		Part->SetCastShadow(false);
		CatSilhouetteParts.Add(Part);
	};

	// One coherent static pose is enough for the brief peripheral pass. It
	// carries the story bible's mackerel-tabby identity without adding a
	// skeletal animation pipeline. Engine primitives remain a playable fallback
	// until the generated mesh has been baked in UE.
	if (AlleyCatMesh)
	{
		AddCatPart(
			TEXT("AlleyCatRun"),
			AlleyCatMesh,
			AlleyCatMaterial,
			FVector::ZeroVector,
			FVector::OneVector,
			FRotator::ZeroRotator);
		bPoolsInitialized = true;
		return;
	}

	AddCatPart(
		TEXT("CatBody"), SphereMesh, DarkMaterial, FVector(0, 0, 18),
		FVector(0.42f, 0.14f, 0.14f), FRotator::ZeroRotator);
	AddCatPart(
		TEXT("CatHead"), SphereMesh, DarkMaterial, FVector(26, 0, 22),
		FVector(0.15f, 0.12f, 0.13f), FRotator::ZeroRotator);
	AddCatPart(
		TEXT("CatTail"), CubeMesh, DarkMaterial, FVector(-31, 0, 23), FVector(0.34f, 0.028f, 0.028f),
		FRotator(0, -18, 18));
	AddCatPart(
		TEXT("CatForeLeg"), CubeMesh, DarkMaterial, FVector(14, -5, 7), FVector(0.10f, 0.028f, 0.11f),
		FRotator(0, 0, -18));
	AddCatPart(
		TEXT("CatHindLeg"), CubeMesh, DarkMaterial, FVector(-14, 5, 7), FVector(0.11f, 0.028f, 0.10f),
		FRotator(0, 0, 24));
	AddCatPart(
		TEXT("CatFarLeg"), CubeMesh, DarkMaterial, FVector(-6, -5, 6), FVector(0.09f, 0.025f, 0.09f),
		FRotator(0, 0, -28));

	bPoolsInitialized = true;
}

UAudioComponent* AIGNeighborhoodLifeDirector::CreateSpatialAudioComponent(
	USceneComponent* Parent,
	const FName ComponentName,
	const float InnerRadius,
	const float FalloffDistance)
{
	UAudioComponent* Audio = NewObject<UAudioComponent>(this, ComponentName);
	Audio->SetupAttachment(Parent ? Parent : SceneRoot.Get());
	Audio->bAutoActivate = false;
	Audio->bAutoDestroy = false;
	Audio->bOverrideAttenuation = true;
	Audio->AttenuationOverrides.bAttenuate = true;
	Audio->AttenuationOverrides.bSpatialize = true;
	Audio->AttenuationOverrides.AttenuationShapeExtents =
		FVector(FMath::Max(1.0f, InnerRadius), 0.0f, 0.0f);
	Audio->AttenuationOverrides.FalloffDistance = FMath::Max(1.0f, FalloffDistance);
	Audio->AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Audio->AttenuationOverrides.dBAttenuationAtMax = -60.0f;
	Audio->RegisterComponent();
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->RegisterComponent(Audio, EIGAudioBus::World);
		}
	}
	return Audio;
}

UAudioComponent* AIGNeighborhoodLifeDirector::SpawnTransientOneShot(
	UIGToneSequenceSoundWave* Sound,
	const FVector& Location,
	const float Volume,
	const float InnerRadius,
	const float FalloffDistance)
{
	if (!Sound)
	{
		return nullptr;
	}
	TransientAudio.RemoveAllSwap(
		[](const TObjectPtr<UAudioComponent>& Component)
		{
			return !IsValid(Component.Get());
		},
		EAllowShrinking::No);
	UAudioComponent* Audio = CreateSpatialAudioComponent(
		SceneRoot,
		MakeUniqueObjectName(this, UAudioComponent::StaticClass(), TEXT("ScooterOneShot")),
		InnerRadius,
		FalloffDistance);
	Audio->bAutoDestroy = true;
	TransientAudio.Add(Audio);
	Audio->SetWorldLocation(Location);
	Audio->SetSound(Sound);
	Audio->SetVolumeMultiplier(Volume);
	Audio->Play();
	return Audio;
}

UIGToneSequenceSoundWave* AIGNeighborhoodLifeDirector::CreateScooterLoop(UObject* Outer) const
{
	UIGToneSequenceSoundWave* Wave = NewObject<UIGToneSequenceSoundWave>(Outer);
	TArray<FIGToneNote> Notes;
	// 125cc 단기통 4행정. 배기 박동은 FM 목소리로 거칠게, 몸통은 저역 사각파,
	// 흡기와 체인 소리는 대역 잡음 둘이다. 빠르기는 실행 중 음높이로 올린다.
	Notes.Add({0.00f, 0.96f, 52.0f, 0.070f, 0.02f, 0.30f, EIGToneWaveform::Growl, 0.50f});
	Notes.Add({0.00f, 0.96f, 104.0f, 0.045f, 0.02f, 0.30f, EIGToneWaveform::SoftSquare});
	Notes.Add({0.00f, 0.96f, 420.0f, 0.040f, 0.04f, 0.40f, EIGToneWaveform::BandNoise, 0.25f});
	Notes.Add({0.00f, 0.96f, 1800.0f, 0.018f, 0.04f, 0.40f, EIGToneWaveform::BandNoise, 0.20f});
	Wave->ConfigureNotes(MoveTemp(Notes), true, 0.96f);
	// 손목이 스로틀을 미세하게 놓았다 당기는 떨림.
	Wave->ConfigurePitchWow(0.015f, 4.0f);
	return Wave;
}

UIGToneSequenceSoundWave* AIGNeighborhoodLifeDirector::CreateGustSound(
	UObject* Outer,
	const float Duration) const
{
	UIGToneSequenceSoundWave* Wave = NewObject<UIGToneSequenceSoundWave>(Outer);
	TArray<FIGToneNote> Notes;
	Notes.Add({0.0f, Duration, 420.0f, 0.075f, 0.24f, 1.8f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.2f, FMath::Max(0.2f, Duration - 0.4f), 91.0f, 0.028f, 0.32f, 2.1f,
		EIGToneWaveform::ValueNoise});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* AIGNeighborhoodLifeDirector::CreateCatCall(UObject* Outer) const
{
	UIGToneSequenceSoundWave* Wave = NewObject<UIGToneSequenceSoundWave>(Outer);
	TArray<FIGToneNote> Notes;

	const float Amplitude = 0.105f;
	// Stepped glide is softened by overlapping sine partials. It reads as a
	// distant cat call without presenting an animal directly.
	const float Frequencies[] = {690.0f, 760.0f, 825.0f, 790.0f, 710.0f, 625.0f};
	for (int32 NoteIndex = 0; NoteIndex < static_cast<int32>(UE_ARRAY_COUNT(Frequencies)); ++NoteIndex)
	{
		const float Start = NoteIndex * 0.085f;
		Notes.Add({Start, 0.18f, Frequencies[NoteIndex], Amplitude,
			0.18f, 1.15f, EIGToneWaveform::Sine});
		Notes.Add({Start, 0.16f, Frequencies[NoteIndex] * 2.01f,
			Amplitude * 0.18f, 0.20f, 1.35f, EIGToneWaveform::Sine});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

void AIGNeighborhoodLifeDirector::RestartDeterministicSchedule()
{
	if (!GetWorld())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(ScooterScheduleHandle);
	GetWorldTimerManager().ClearTimer(NearMissPollHandle);
	GetWorldTimerManager().ClearTimer(GustScheduleHandle);
	GetWorldTimerManager().ClearTimer(CatScheduleHandle);
	StopAllRuntimeEvents();

	const uint32 CombinedSeed =
		static_cast<uint32>(DeterministicSeed) ^ IGNeighborhoodLife::NormalSeedSalt;
	Random.Initialize(static_cast<int32>(CombinedSeed));
	bOutdoorSequencePrimed = false;
	bNearMissArmed = false;

	ScheduleNextGust();
	ScheduleNextScooter();
	ScheduleNextCatTrace();

	// 정우는 낮에 자고 새벽에 일한다. 오토바이가 연석에 서 있는지 가끔 다시 본다.
	GetWorldTimerManager().SetTimer(
		ParkedPresenceHandle,
		this,
		&ThisClass::RefreshParkedScooterPresence,
		2.0f,
		true,
		0.2f);
}

void AIGNeighborhoodLifeDirector::StopAllRuntimeEvents()
{
	for (int32 SlotIndex = 0; SlotIndex < ScooterRuntime.Num(); ++SlotIndex)
	{
		DeactivateScooter(SlotIndex);
	}
	for (int32 LeafIndex = 0; LeafIndex < LeafRuntime.Num(); ++LeafIndex)
	{
		DeactivateLeaf(LeafIndex);
	}
	DeactivateCatTrace();
	for (UAudioComponent* Audio : TransientAudio)
	{
		if (IsValid(Audio))
		{
			Audio->Stop();
			Audio->DestroyComponent();
		}
	}
	TransientAudio.Reset();

	GustElapsed = -1.0f;
	CurrentWindSignal = FVector::ZeroVector;
	SetActorTickEnabled(false);
}

void AIGNeighborhoodLifeDirector::ScheduleNextScooter()
{
	if (!GetWorld())
	{
		return;
	}

	// 동네 오토바이는 반 분에 한 대꼴이다. 이어지는 소음이 아니라 하나씩의 사건이다.
	const float Delay = Random.FRandRange(30.0f, 55.0f);
	GetWorldTimerManager().SetTimer(
		ScooterScheduleHandle,
		this,
		&ThisClass::LaunchScooterThroughPass,
		Delay,
		false);
}

void AIGNeighborhoodLifeDirector::ScheduleNextGust()
{
	if (!GetWorld())
	{
		return;
	}

	const float Delay = Random.FRandRange(5.0f, 13.0f);
	GetWorldTimerManager().SetTimer(
		GustScheduleHandle,
		this,
		&ThisClass::LaunchWindGust,
		Delay,
		false);
}

void AIGNeighborhoodLifeDirector::ScheduleNextCatTrace()
{
	if (!GetWorld())
	{
		return;
	}

	const float Delay = Random.FRandRange(17.0f, 31.0f);
	GetWorldTimerManager().SetTimer(
		CatScheduleHandle,
		this,
		&ThisClass::LaunchCatTrace,
		Delay,
		false);
}

void AIGNeighborhoodLifeDirector::ComputeLaneLimits(
	const FVector& Location,
	const FVector& Forward,
	float& OutMinOffset,
	float& OutMaxOffset) const
{
	using namespace IGNeighborhoodLife;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal2D();
	const float Clearance = ScooterHalfWidth + 4.0f;
	FVector LowWorld = Location;
	FVector HighWorld = Location;

	if (FMath::Abs(Forward.X) >= FMath::Abs(Forward.Y))
	{
		// 동서로 달린다. 기본은 길 양옆 40 cm, 골목과 뒤편 길에서는 그 길의 폭이다.
		float NorthLimit = Location.Y + 40.0f;
		float SouthLimit = Location.Y - 40.0f;
		if (Location.Y > -700.0f && Location.Y < -380.0f)
		{
			// 앞 골목. 양쪽 연석, 그리고 차체 길이 안에 걸리는 장애물이 폭을 줄인다.
			NorthLimit = AlleyNorthCurbFace - Clearance;
			SouthLimit = AlleySouthCurbFace + Clearance;
			for (const FAlleyObstacle& Obstacle : AlleyObstacles)
			{
				if (FMath::Abs(Obstacle.X - Location.X) > Obstacle.Radius + ScooterHalfLength)
				{
					continue;
				}
				if (Obstacle.Y > Location.Y)
				{
					NorthLimit = FMath::Min(NorthLimit, Obstacle.Y - Obstacle.Radius - Clearance);
				}
				else
				{
					SouthLimit = FMath::Max(SouthLimit, Obstacle.Y + Obstacle.Radius + Clearance);
				}
			}
		}
		else if (Location.Y < BackAlleyNorthFace && Location.Y > BackAlleySouthFace)
		{
			NorthLimit = BackAlleyNorthFace - Clearance;
			SouthLimit = BackAlleySouthFace + Clearance;
		}
		if (NorthLimit < SouthLimit)
		{
			const float Middle = (NorthLimit + SouthLimit) * 0.5f;
			NorthLimit = Middle;
			SouthLimit = Middle;
		}
		LowWorld.Y = SouthLimit;
		HighWorld.Y = NorthLimit;
	}
	else
	{
		// 샛길을 남북으로 달린다. 양쪽 벽 사이다.
		float MinX = Location.X - 30.0f;
		float MaxX = Location.X + 30.0f;
		if (Location.X > WestPassageMinX && Location.X < WestPassageMaxX)
		{
			MinX = WestPassageMinX + Clearance;
			MaxX = WestPassageMaxX - Clearance;
		}
		else if (Location.X > EastPassageMinX && Location.X < EastPassageMaxX)
		{
			MinX = EastPassageMinX + Clearance;
			MaxX = EastPassageMaxX - Clearance;
		}
		if (MaxX < MinX)
		{
			const float Middle = (MinX + MaxX) * 0.5f;
			MinX = Middle;
			MaxX = Middle;
		}
		LowWorld.X = MinX;
		HighWorld.X = MaxX;
	}

	const float A = FVector::DotProduct(LowWorld - Location, Right);
	const float B = FVector::DotProduct(HighWorld - Location, Right);
	OutMinOffset = FMath::Min(A, B);
	OutMaxOffset = FMath::Max(A, B);
}

void AIGNeighborhoodLifeDirector::BuildScooterPath(
	const TArray<FVector>& Corners,
	const TArray<float>& CornerRadii,
	TArray<FScooterPathPoint>& OutPath) const
{
	OutPath.Reset();
	if (Corners.Num() < 2)
	{
		return;
	}

	TArray<FVector> Points;
	Points.Add(Corners[0]);
	for (int32 Index = 1; Index < Corners.Num() - 1; ++Index)
	{
		const FVector Corner = Corners[Index];
		const FVector Incoming = (Corner - Corners[Index - 1]).GetSafeNormal2D();
		const FVector Outgoing = (Corners[Index + 1] - Corner).GetSafeNormal2D();
		const float Cosine = FMath::Clamp(FVector::DotProduct(Incoming, Outgoing), -1.0f, 1.0f);
		const float LegIn = FVector::Dist2D(Corner, Corners[Index - 1]);
		const float LegOut = FVector::Dist2D(Corners[Index + 1], Corner);
		// 겹친 모서리, 곧은 길, 되돌아 꺾는 길(tan이 끝없이 커진다)은 깎지 않는다.
		if ((CornerRadii.IsValidIndex(Index) ? CornerRadii[Index] : 0.0f) <= 1.0f
			|| LegIn < 1.0f || LegOut < 1.0f
			|| Cosine > 0.999f || Cosine < -0.95f)
		{
			Points.Add(Corner);
			continue;
		}
		// 모서리를 반지름 R의 원호로 깎는다. 원호는 들어오는 직선과 나가는 직선
		// 모두에 접하고, 접점은 모서리에서 R·tan(θ/2)만큼 떨어져 있다. 15도마다
		// 한 점을 두면 차체 기울기가 계단처럼 튀지 않는다. 다리가 짧으면 원호가
		// 다리 밖으로 나가지 않게 반지름을 줄인다.
		const float Turn = FMath::Acos(Cosine);
		const float HalfTan = FMath::Tan(Turn * 0.5f);
		const float Tangent = FMath::Min(CornerRadii[Index] * HalfTan, 0.45f * FMath::Min(LegIn, LegOut));
		const float Radius = Tangent / FMath::Max(HalfTan, KINDA_SMALL_NUMBER);
		const FVector Inside = (Outgoing - Incoming * Cosine).GetSafeNormal2D();
		const FVector Entry = Corner - Incoming * Tangent;
		const FVector Center = Entry + Inside * Radius;
		const int32 ArcSteps = FMath::Max(2, FMath::CeilToInt(FMath::RadiansToDegrees(Turn) / 15.0f));
		for (int32 Step = 0; Step <= ArcSteps; ++Step)
		{
			const float Angle = Turn * static_cast<float>(Step) / ArcSteps;
			Points.Add(
				Center
				- Inside * (Radius * FMath::Cos(Angle))
				+ Incoming * (Radius * FMath::Sin(Angle)));
		}
	}
	Points.Add(Corners.Last());

	float Distance = 0.0f;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (Index > 0)
		{
			Distance += FVector::Dist2D(Points[Index - 1], Points[Index]);
		}
		const FVector Forward = Index < Points.Num() - 1
			? (Points[Index + 1] - Points[Index]).GetSafeNormal2D()
			: (Points[Index] - Points[Index - 1]).GetSafeNormal2D();
		FScooterPathPoint& Point = OutPath.AddDefaulted_GetRef();
		Point.Location = Points[Index];
		Point.Distance = Distance;
		ComputeLaneLimits(Point.Location, Forward, Point.MinOffset, Point.MaxOffset);
	}
}

void AIGNeighborhoodLifeDirector::SamplePath(
	const FScooterRuntime& Runtime,
	const float Distance,
	FVector& OutLocation,
	FVector& OutForward,
	float& OutMinOffset,
	float& OutMaxOffset) const
{
	const TArray<FScooterPathPoint>& Path = Runtime.Path;
	if (Path.Num() < 2)
	{
		OutLocation = Path.Num() ? Path[0].Location : FVector::ZeroVector;
		OutForward = FVector::ForwardVector;
		OutMinOffset = 0.0f;
		OutMaxOffset = 0.0f;
		return;
	}
	const float Clamped = FMath::Clamp(Distance, 0.0f, Path.Last().Distance);
	int32 Segment = 0;
	while (Segment < Path.Num() - 2 && Path[Segment + 1].Distance < Clamped)
	{
		++Segment;
	}
	const FScooterPathPoint& A = Path[Segment];
	const FScooterPathPoint& B = Path[Segment + 1];
	const float Length = FMath::Max(B.Distance - A.Distance, 0.01f);
	const float Alpha = FMath::Clamp((Clamped - A.Distance) / Length, 0.0f, 1.0f);
	OutLocation = FMath::Lerp(A.Location, B.Location, Alpha);
	OutForward = (B.Location - A.Location).GetSafeNormal2D();
	// 원호 위의 점은 직선의 넓은 폭을 그대로 믿지 않는다. 두 점 중 좁은 쪽을 쓴다.
	OutMinOffset = FMath::Max(A.MinOffset, B.MinOffset);
	OutMaxOffset = FMath::Min(A.MaxOffset, B.MaxOffset);
	if (OutMaxOffset < OutMinOffset)
	{
		const float Middle = (OutMinOffset + OutMaxOffset) * 0.5f;
		OutMinOffset = Middle;
		OutMaxOffset = Middle;
	}
}

bool AIGNeighborhoodLifeDirector::ProjectOntoPath(
	const FScooterRuntime& Runtime,
	const FVector& Point,
	const float SearchFrom,
	const float SearchTo,
	float& OutAlong,
	float& OutSide) const
{
	const TArray<FScooterPathPoint>& Path = Runtime.Path;
	bool bFound = false;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Path.Num() - 1; ++Index)
	{
		const FScooterPathPoint& A = Path[Index];
		const FScooterPathPoint& B = Path[Index + 1];
		if (B.Distance < SearchFrom || A.Distance > SearchTo)
		{
			continue;
		}
		const FVector A2(A.Location.X, A.Location.Y, 0.0f);
		const FVector B2(B.Location.X, B.Location.Y, 0.0f);
		const FVector P2(Point.X, Point.Y, 0.0f);
		const FVector Closest = FMath::ClosestPointOnSegment(P2, A2, B2);
		const float DistanceSquared = FVector::DistSquared(P2, Closest);
		if (DistanceSquared >= BestDistanceSquared)
		{
			continue;
		}
		BestDistanceSquared = DistanceSquared;
		const FVector Forward = (B2 - A2).GetSafeNormal();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		OutAlong = A.Distance + FVector::Dist(A2, Closest);
		OutSide = FVector::DotProduct(P2 - Closest, Right);
		bFound = true;
	}
	return bFound;
}

int32 AIGNeighborhoodLifeDirector::ActivateScooter(
	TArray<FScooterPathPoint>&& Path,
	const float InCruiseSpeed,
	const bool bNearMissPass,
	const bool bParkAtEnd)
{
	if (!bPoolsInitialized || Path.Num() < 2)
	{
		return INDEX_NONE;
	}
	const int32 SlotIndex = ScooterRuntime.IndexOfByPredicate(
		[](const FScooterRuntime& Runtime) { return !Runtime.bActive && !Runtime.bParked; });
	if (SlotIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	// 출발점이 그녀 코앞이면 띄우지 않는다. 오토바이가 몸 안에서 생겨나는 일은 없어야 한다.
	FVector Player;
	if (TryGetListenerLocation(Player)
		&& FVector::Dist2D(Player, Path[0].Location) < 650.0f
		&& FMath::Abs(Player.Z - Path[0].Location.Z) < 300.0f)
	{
		return INDEX_NONE;
	}

	FScooterRuntime& Runtime = ScooterRuntime[SlotIndex];
	Runtime = FScooterRuntime();
	Runtime.bActive = true;
	Runtime.bNearMissPass = bNearMissPass;
	Runtime.bParkAtEnd = bParkAtEnd;
	Runtime.Path = MoveTemp(Path);
	Runtime.CruiseSpeed = InCruiseSpeed;
	// 첫 외출의 오토바이는 이미 달리던 채로 샛길에 들어선다.
	Runtime.Speed = InCruiseSpeed * (bNearMissPass ? 0.7f : 0.55f);
	Runtime.BasePitch = 1.0f;
	Runtime.BaseVolume = bNearMissPass ? 0.62f : 0.40f;
	float MinOffset = 0.0f;
	float MaxOffset = 0.0f;
	SamplePath(Runtime, 0.0f, Runtime.Location, Runtime.Forward, MinOffset, MaxOffset);
	Runtime.PreviousYaw = Runtime.Forward.Rotation().Yaw;

	SetScooterVisible(SlotIndex, true, true);
	SetScooterBlocking(SlotIndex, false);
	ApplyScooterTransform(SlotIndex, 0.0f);

	UAudioComponent* Audio = ScooterAudio[SlotIndex];
	Audio->Stop();
	Audio->SetSound(CreateScooterLoop(Audio));
	Audio->SetVolumeMultiplier(Runtime.BaseVolume);
	Audio->SetPitchMultiplier(Runtime.BasePitch);
	Audio->Play();

	RefreshRuntimeUpdates();
	return SlotIndex;
}

void AIGNeighborhoodLifeDirector::LaunchScooterThroughPass()
{
	ScheduleNextScooter();
	if (!bPoolsInitialized || bNearMissArmed || !IsPlayerNearRoad(EventActivationDistance))
	{
		return;
	}

	// 뒤편 배송 골목에서 한 샛길로 나와 골목을 가로질러 다른 샛길로 빠진다.
	// 골목 양 끝은 막혀 있으니 이 길 말고는 들어올 데가 없다.
	const bool bWestToEast = Random.FRand() < 0.5f;
	// 남쪽 과자 판매대(Y -644) 앞을 피한다.
	const float LaneY = Random.FRandRange(-560.0f, -520.0f);
	const float FromX = bWestToEast ? 1300.0f : 1740.0f;
	const float ToX = bWestToEast ? 1740.0f : 1300.0f;
	TArray<FVector> Corners = {
		FVector(FromX, -1230.0f, RoadStart.Z),
		FVector(FromX, LaneY, RoadStart.Z),
		FVector(ToX, LaneY, RoadStart.Z),
		FVector(ToX, -1230.0f, RoadStart.Z)};
	TArray<float> Radii = {0.0f, 110.0f, 110.0f, 0.0f};
	TArray<FScooterPathPoint> Path;
	BuildScooterPath(Corners, Radii, Path);
	ActivateScooter(MoveTemp(Path), Random.FRandRange(700.0f, 880.0f), false, false);
}

void AIGNeighborhoodLifeDirector::PollNearMissTrigger()
{
	if (!bNearMissArmed)
	{
		GetWorldTimerManager().ClearTimer(NearMissPollHandle);
		return;
	}
	// 골목으로 나오지 않고 집으로 돌아갔거나 서쪽으로 갔다. 1분이 지나면 접고
	// 평소처럼 지나다니게 한다. 기다리는 동안은 오토바이가 한 대도 다니지 않는다.
	if (GetWorld()->GetTimeSeconds() - NearMissArmedSeconds > IGNeighborhoodLife::NearMissArmedTimeoutSeconds)
	{
		bNearMissArmed = false;
		GetWorldTimerManager().ClearTimer(NearMissPollHandle);
		ScheduleNextScooter();
		return;
	}
	FVector Player;
	if (!TryGetListenerLocation(Player))
	{
		return;
	}
	// 공동현관(X 643)을 나와 동쪽으로 몇 걸음. 동쪽 샛길 입구(X 1640)까지 아직
	// 8 m 남은 자리에서 띄우면, 오토바이가 샛길을 빠져나올 때 그녀는 그 3~4 m 앞이다.
	const bool bInAlley = Player.Y < -400.0f && Player.Y > -680.0f && Player.Z < 260.0f;
	if (!bInAlley || Player.X < 820.0f)
	{
		return;
	}
	if (Player.X > 1500.0f)
	{
		// 이미 샛길 앞을 지났다. 등 뒤에서 튀어나오는 오토바이는 보이지 않으니 접는다.
		bNearMissArmed = false;
		GetWorldTimerManager().ClearTimer(NearMissPollHandle);
		ScheduleNextScooter();
		return;
	}
	if (LaunchAlleyNearMiss())
	{
		bNearMissArmed = false;
		bNearMissPlayed = true;
		GetWorldTimerManager().ClearTimer(NearMissPollHandle);
		// 서사 기록에 남기면 저장이 「없는 층」 저장으로 분류된다. 이미 밤이 시작된
		// 회차에서만 남기고, 그 전에는 이번 실행 안에서만 기억한다.
		if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
			Narrative && Narrative->GetNightIndex() > 0)
		{
			Narrative->MarkBeatPlayed(FName(IGNeighborhoodLife::NearMissBeat));
		}
		ScheduleNextScooter();
	}
}

bool AIGNeighborhoodLifeDirector::HasNearMissPlayed() const
{
	if (bNearMissPlayed)
	{
		return true;
	}
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative && Narrative->HasBeatPlayed(FName(IGNeighborhoodLife::NearMissBeat));
}

bool AIGNeighborhoodLifeDirector::LaunchAlleyNearMiss()
{
	using namespace IGNeighborhoodLife;
	const float Z = RoadStart.Z;
	// 동쪽 샛길 깊은 곳에서 출발한다. 상가 벽에 가려 골목에서는 보이지 않는 자리다.
	// 샛길을 북쪽으로 달려 나와 골목에서 서쪽으로 꺾고, 그녀 옆을 지난 뒤 공동현관
	// 서쪽 연석에 붙여 선다. 필로티 기둥 사이는 61 cm라 스쿠터가 들어가지 못한다.
	// 모서리 반지름 170 cm는 샛길 서쪽 벽 모서리(1640, -700)가 회전 원 안쪽에 남는 값이다.
	TArray<FVector> Corners = {
		FVector(1740.0f, -1150.0f, Z),
		FVector(1740.0f, NearMissLaneY, Z),
		FVector(760.0f, NearMissLaneY, Z),
		FVector(ParkingX, ParkingY, Z)};
	TArray<float> Radii = {0.0f, 170.0f, 400.0f, 0.0f};
	TArray<FScooterPathPoint> Path;
	BuildScooterPath(Corners, Radii, Path);
	return ActivateScooter(MoveTemp(Path), CruiseSpeed, true, true) != INDEX_NONE;
}

void AIGNeighborhoodLifeDirector::LaunchWindGust()
{
	ScheduleNextGust();
	if (!IsPlayerNearRoad(EventActivationDistance))
	{
		return;
	}

	GustElapsed = 0.0f;
	GustDuration = Random.FRandRange(2.2f, 4.2f);
	GustPeakStrength = Random.FRandRange(0.35f, 0.70f);
	const FVector RoadDirection = (RoadEnd - RoadStart).GetSafeNormal();
	const float DirectionSign = Random.FRand() < 0.5f ? -1.0f : 1.0f;
	GustDirection = (
		RoadDirection * DirectionSign +
		FVector(0.0f, Random.FRandRange(-0.22f, 0.22f), 0.04f)).GetSafeNormal();
	// 값은 쓰지 않는다. 이 뽑기를 빼면 뒤에 오는 소리 자리와 잎이 전부 달라진다.
	Random.FRandRange(0.0f, 2.0f * UE_PI);

	UIGToneSequenceSoundWave* GustSound = CreateGustSound(this, GustDuration);
	TransientAudio.RemoveAllSwap(
		[](const TObjectPtr<UAudioComponent>& Component)
		{
			return !IsValid(Component.Get());
		},
		EAllowShrinking::No);
	UAudioComponent* GustAudio = CreateSpatialAudioComponent(
		SceneRoot, MakeUniqueObjectName(this, UAudioComponent::StaticClass(), TEXT("WindGustAudio")),
		400.0f, 2200.0f);
	GustAudio->bAutoDestroy = true;
	TransientAudio.Add(GustAudio);
	GustAudio->SetWorldLocation(FMath::Lerp(RoadStart, RoadEnd, Random.FRand()) + FVector(0, 0, 160));
	GustAudio->SetSound(GustSound);
	GustAudio->SetVolumeMultiplier(0.34f);
	GustAudio->Play();

	const FVector LeafOrigin =
		FMath::Lerp(RoadStart, RoadEnd, Random.FRandRange(0.12f, 0.88f)) +
		FVector(0.0f, Random.FRandRange(-85.0f, 85.0f), 3.0f);
	const int32 LeafCount = Random.RandRange(7, 13);
	ActivateLeaves(LeafOrigin, LeafCount, GustPeakStrength);
	RefreshRuntimeUpdates();
}

void AIGNeighborhoodLifeDirector::LaunchCatTrace()
{
	ScheduleNextCatTrace();
	if (!bPoolsInitialized || !IsPlayerNearRoad(900.0f))
	{
		return;
	}

	const FVector SideAlleyMouth(1220.0f, -405.0f, 4.0f);
	const FVector CallLocation = SideAlleyMouth + FVector(0.0f, 35.0f, 38.0f);

	UIGToneSequenceSoundWave* CatCall = CreateCatCall(this);
	TransientAudio.RemoveAllSwap(
		[](const TObjectPtr<UAudioComponent>& Component)
		{
			return !IsValid(Component.Get());
		},
		EAllowShrinking::No);
	UAudioComponent* CatAudio = CreateSpatialAudioComponent(
		SceneRoot, MakeUniqueObjectName(this, UAudioComponent::StaticClass(), TEXT("CatCallAudio")),
		90.0f, 1700.0f);
	CatAudio->bAutoDestroy = true;
	TransientAudio.Add(CatAudio);
	CatAudio->SetWorldLocation(CallLocation);
	CatAudio->SetSound(CatCall);
	CatAudio->SetVolumeMultiplier(0.55f);
	CatAudio->Play();

	const bool bReverse = Random.FRand() < 0.5f;
	CatRuntime.bActive = true;
	CatRuntime.Start = SideAlleyMouth + FVector(0.0f, bReverse ? -250.0f : 85.0f, 0.0f);
	CatRuntime.End = SideAlleyMouth + FVector(0.0f, bReverse ? 85.0f : -250.0f, 0.0f);
	CatRuntime.Elapsed = 0.0f;
	CatRuntime.Duration = Random.FRandRange(0.9f, 1.35f);
	CatTraceRoot->SetWorldLocation(CatRuntime.Start);
	CatTraceRoot->SetWorldRotation((CatRuntime.End - CatRuntime.Start).Rotation());
	CatTraceRoot->SetWorldScale3D(FVector::OneVector);
	for (UStaticMeshComponent* Part : CatSilhouetteParts)
	{
		if (Part)
		{
			Part->SetVisibility(true);
			Part->SetHiddenInGame(false);
		}
	}
	ActivateLeaves(CatRuntime.End, Random.RandRange(3, 6), 0.55f);
	RefreshRuntimeUpdates();
}

void AIGNeighborhoodLifeDirector::ActivateLeaves(
	const FVector& Origin,
	const int32 Count,
	const float ImpulseScale)
{
	int32 Remaining = FMath::Max(0, Count);
	for (int32 LeafIndex = 0; LeafIndex < LeafRuntime.Num() && Remaining > 0; ++LeafIndex)
	{
		FLeafRuntime& Runtime = LeafRuntime[LeafIndex];
		if (Runtime.bActive)
		{
			continue;
		}

		Runtime.bActive = true;
		Runtime.Age = 0.0f;
		Runtime.Lifetime = Random.FRandRange(2.0f, 4.8f);
		Runtime.Phase = Random.FRandRange(0.0f, 2.0f * UE_PI);
		Runtime.Position = Origin + FVector(
			Random.FRandRange(-90.0f, 90.0f),
			Random.FRandRange(-60.0f, 60.0f),
			Random.FRandRange(1.0f, 15.0f));

		Runtime.Velocity =
			GustDirection * Random.FRandRange(55.0f, 145.0f) * FMath::Max(0.25f, ImpulseScale) +
			FVector(
				Random.FRandRange(-22.0f, 22.0f),
				Random.FRandRange(-18.0f, 18.0f),
				Random.FRandRange(20.0f, 72.0f));

		UStaticMeshComponent* Leaf = LeafMeshes[LeafIndex];
		Leaf->SetWorldLocation(Runtime.Position);
		Leaf->SetWorldRotation(FRotator(
			Random.FRandRange(-50.0f, 50.0f),
			Random.FRandRange(-180.0f, 180.0f),
			Random.FRandRange(-70.0f, 70.0f)));
		Leaf->SetVisibility(true);
		Leaf->SetHiddenInGame(false);
		--Remaining;
	}
}

void AIGNeighborhoodLifeDirector::UpdateRuntime(const float DeltaSeconds)
{
	if (!GetWorld())
	{
		return;
	}

	FVector ListenerLocation = FVector::ZeroVector;
	TryGetListenerLocation(ListenerLocation);
	UpdateGust(DeltaSeconds);
	UpdateScooters(DeltaSeconds, ListenerLocation);
	UpdateLeaves(DeltaSeconds, ListenerLocation);
	UpdateCatTrace(DeltaSeconds);
	RefreshRuntimeUpdates();
}

void AIGNeighborhoodLifeDirector::UpdateScooters(
	const float DeltaSeconds,
	const FVector& ListenerLocation)
{
	for (int32 SlotIndex = 0; SlotIndex < ScooterRuntime.Num(); ++SlotIndex)
	{
		FScooterRuntime& Runtime = ScooterRuntime[SlotIndex];
		if (Runtime.bParked && !Runtime.bEngineCut)
		{
			// 시동을 끄고 내린다. 라이더는 그녀가 오토바이를 보지 않는 사이에 사라진다.
			Runtime.ParkedSeconds += DeltaSeconds;
			if (Runtime.ParkedSeconds > 0.9f)
			{
				Runtime.bEngineCut = true;
				PlayScooterEngineOff(SlotIndex);
			}
			continue;
		}
		if (Runtime.bParked)
		{
			Runtime.ParkedSeconds += DeltaSeconds;
			const UStaticMeshComponent* Body = ScooterBodies.IsValidIndex(SlotIndex)
				? ScooterBodies[SlotIndex].Get() : nullptr;
			if (Body && Body->IsVisible() && Body->GetStaticMesh() == RiddenScooterMesh && !bParkedRiderPresent
				&& (Runtime.ParkedSeconds > 6.0f
					|| (Runtime.ParkedSeconds > 2.0f && !Body->WasRecentlyRendered(0.2f))))
			{
				SetScooterVisible(SlotIndex, true, false);
			}
			continue;
		}
		if (!Runtime.bActive)
		{
			continue;
		}
		AdvanceScooter(SlotIndex, DeltaSeconds, ListenerLocation);
	}
}

void AIGNeighborhoodLifeDirector::AdvanceScooter(
	const int32 SlotIndex,
	const float DeltaSeconds,
	const FVector& ListenerLocation)
{
	using namespace IGNeighborhoodLife;
	FScooterRuntime& Runtime = ScooterRuntime[SlotIndex];
	const float PathLength = Runtime.Path.Last().Distance;
	APawn* Player = GetPlayerPawn();
	float PlayerRadius = 34.0f;
	if (const ACharacter* Character = Cast<ACharacter>(Player))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			PlayerRadius = Capsule->GetScaledCapsuleRadius();
		}
	}
	// 오래 막혀 있으면 라이더가 여유 없이 비집고 지나가려 한다. 그때는 기어서 간다.
	const bool bSqueezing = Runtime.bSqueezePass;
	const float MinClearance = ScooterHalfWidth + PlayerRadius + (bSqueezing ? 1.0f : PassMargin);

	// 1. 그녀가 길 위 어디에 있나. 같은 바닥 높이에 있을 때만 본다.
	bool bPlayerOnPath = false;
	float PlayerAlong = 0.0f;
	float PlayerSide = 0.0f;
	if (Player && FMath::Abs(Player->GetActorLocation().Z - Runtime.Location.Z) < 220.0f)
	{
		bPlayerOnPath = ProjectOntoPath(
			Runtime,
			Player->GetActorLocation(),
			Runtime.Distance - ScooterHalfLength - PlayerRadius - 60.0f,
			Runtime.Distance + LookAheadDistance,
			PlayerAlong,
			PlayerSide)
			&& FMath::Abs(PlayerSide) < 320.0f;
	}
	const float Gap = PlayerAlong - Runtime.Distance;
	const bool bPlayerAhead = bPlayerOnPath && Gap > -(ScooterHalfLength + PlayerRadius);
	if (!bPlayerAhead)
	{
		Runtime.bSqueezePass = false;
	}

	// 2. 옆으로 어디를 지나갈지. 몸에서 더 멀리 지날 수 있는 쪽을 고른다.
	FVector PathLocation;
	FVector PathForward;
	float CurrentMin = 0.0f;
	float CurrentMax = 0.0f;
	SamplePath(Runtime, Runtime.Distance, PathLocation, PathForward, CurrentMin, CurrentMax);
	float TargetOffset = FMath::Clamp(0.0f, CurrentMin, CurrentMax);
	bool bBlocked = false;
	if (bPlayerAhead && Gap < LookAheadDistance)
	{
		FVector PlayerPathLocation;
		FVector PlayerPathForward;
		float LimitMin = 0.0f;
		float LimitMax = 0.0f;
		SamplePath(Runtime, PlayerAlong, PlayerPathLocation, PlayerPathForward, LimitMin, LimitMax);
		const float Desired = Runtime.bNearMissPass && !Runtime.bPassedPlayer
			? FMath::Max(NearMissCenterDistance, MinClearance)
			: MinClearance + 18.0f;
		const float LeftCandidate = FMath::Clamp(PlayerSide - Desired, LimitMin, LimitMax);
		const float RightCandidate = FMath::Clamp(PlayerSide + Desired, LimitMin, LimitMax);
		const float LeftClearance = FMath::Abs(LeftCandidate - PlayerSide);
		const float RightClearance = FMath::Abs(RightCandidate - PlayerSide);
		float Best = LeftCandidate;
		if (RightClearance > LeftClearance + 4.0f)
		{
			Best = RightCandidate;
		}
		else if (FMath::Abs(RightClearance - LeftClearance) <= 4.0f
			&& FMath::Abs(RightCandidate - Runtime.LateralOffset)
				< FMath::Abs(LeftCandidate - Runtime.LateralOffset))
		{
			Best = RightCandidate;
		}
		TargetOffset = Best;
		bBlocked = Gap > 0.0f && FMath::Abs(Best - PlayerSide) < MinClearance;
	}

	// 3. 빠르기. 원호 앞에서는 미리 줄이고, 길 끝에서 멈추며, 몸이 막고 있으면 선다.
	float TargetSpeed = Runtime.CruiseSpeed;
	{
		// 앞으로 제동 거리만큼의 길에서 가장 급한 원호에 맞춘다.
		const float BrakeWindow = Runtime.Speed * Runtime.Speed / (2.0f * BrakeDeceleration) + 150.0f;
		for (int32 Index = 1; Index < Runtime.Path.Num() - 1; ++Index)
		{
			const FScooterPathPoint& Point = Runtime.Path[Index];
			const float Ahead = Point.Distance - Runtime.Distance;
			if (Ahead < -40.0f || Ahead > BrakeWindow)
			{
				continue;
			}
			const FVector In = (Point.Location - Runtime.Path[Index - 1].Location).GetSafeNormal2D();
			const FVector Out = (Runtime.Path[Index + 1].Location - Point.Location).GetSafeNormal2D();
			const float Turn = FMath::Acos(FMath::Clamp(FVector::DotProduct(In, Out), -1.0f, 1.0f));
			// 원호의 반지름은 그 점 양옆의 짧은 현으로 잰다. 직선과 원호가 만나는
			// 점에서 긴 직선을 쓰면 반지름이 터무니없이 커진다.
			const float Step = FMath::Max(FMath::Min(
				FVector::Dist2D(Point.Location, Runtime.Path[Index - 1].Location),
				FVector::Dist2D(Runtime.Path[Index + 1].Location, Point.Location)), 1.0f);
			if (Turn < 0.02f)
			{
				continue;
			}
			const float Radius = Step / Turn;
			const float CornerSpeed = FMath::Sqrt(CorneringAcceleration * Radius);
			const float Allowed = FMath::Sqrt(
				CornerSpeed * CornerSpeed + 2.0f * BrakeDeceleration * FMath::Max(0.0f, Ahead));
			TargetSpeed = FMath::Min(TargetSpeed, Allowed);
		}
		if (Runtime.bParkAtEnd)
		{
			const float ToEnd = FMath::Max(0.0f, PathLength - Runtime.Distance - 20.0f);
			// 주차는 브레이크를 다 쓰지 않는다. 마지막 몇 미터를 천천히 굴러 들어간다.
			TargetSpeed = FMath::Min(TargetSpeed, FMath::Sqrt(2.0f * 450.0f * ToEnd));
		}
	}
	// 옆으로 비키는 빠르기는 앞으로 가는 빠르기에 묶인다. 너무 빨리 비키면 차체가
	// 길에서 크게 틀어져 앞뒤 끝이 몸 쪽으로 휘두른다(17도 안팎에서 막는다).
	const float LateralCap = FMath::Min(MaxLateralSpeed, Runtime.Speed * 0.3f);
	if (bBlocked)
	{
		// 원호에서는 길을 따라 잰 거리보다 차체 앞끝이 몸에 더 가깝다. 곧은 거리와
		// 길 거리 중 짧은 쪽으로 멈출 자리를 잡는다.
		const float StraightGap = Player
			? FVector::Dist2D(Runtime.Location, Player->GetActorLocation())
			: Gap;
		const float StopDistance = FMath::Min(Gap, StraightGap)
			- (ScooterHalfLength + PlayerRadius + StopNoseGap);
		TargetSpeed = StopDistance > 2.0f
			? FMath::Min(TargetSpeed, FMath::Sqrt(2.0f * BrakeDeceleration * StopDistance))
			: 0.0f;
	}
	else if (bPlayerAhead && Gap > 0.0f
		&& FMath::Abs(Runtime.LateralOffset - PlayerSide) < MinClearance)
	{
		// 아직 비키지 못했다. 비키기 전에 몸에 닿을 빠르기면 줄인다.
		const float LateralNeed = FMath::Abs(TargetOffset - Runtime.LateralOffset);
		const float TimeNeeded = LateralNeed / FMath::Max(60.0f, LateralCap) + 0.15f;
		const float Room = FMath::Max(1.0f, Gap - ScooterHalfLength - PlayerRadius);
		TargetSpeed = FMath::Min(TargetSpeed, FMath::Max(150.0f, Room / TimeNeeded));
	}

	if (bSqueezing && bPlayerAhead && Gap > 0.0f && Gap < 500.0f)
	{
		TargetSpeed = FMath::Min(TargetSpeed, 90.0f);
	}
	const bool bHardBraking = TargetSpeed < Runtime.Speed - 320.0f && Runtime.Speed > 380.0f;
	const float Rate = TargetSpeed > Runtime.Speed ? Acceleration : BrakeDeceleration;
	Runtime.Speed = FMath::FInterpConstantTo(Runtime.Speed, TargetSpeed, DeltaSeconds, Rate);

	// 4. 옆 움직임. 서 있는 오토바이는 옆으로 미끄러지지 않는다.
	const float LateralCapNow = FMath::Min(MaxLateralSpeed, Runtime.Speed * 0.3f);
	const float DesiredLateral = FMath::Clamp(
		(TargetOffset - Runtime.LateralOffset) * 3.2f,
		-LateralCapNow,
		LateralCapNow);
	Runtime.LateralVelocity = FMath::FInterpConstantTo(
		Runtime.LateralVelocity, DesiredLateral, DeltaSeconds, LateralAcceleration);
	Runtime.LateralOffset += Runtime.LateralVelocity * DeltaSeconds;

	// 5. 길을 따라 나아간다.
	Runtime.Distance = FMath::Min(Runtime.Distance + Runtime.Speed * DeltaSeconds, PathLength);
	SamplePath(Runtime, Runtime.Distance, PathLocation, PathForward, CurrentMin, CurrentMax);
	const float ClampedOffset = FMath::Clamp(Runtime.LateralOffset, CurrentMin, CurrentMax);
	if (!FMath::IsNearlyEqual(ClampedOffset, Runtime.LateralOffset))
	{
		// 장애물 옆이라 더는 비켜 갈 수 없다. 옆으로 미는 속도를 남겨 두면 차체만
		// 벽 쪽으로 틀어진 채 곧게 달린다.
		Runtime.LateralOffset = ClampedOffset;
		Runtime.LateralVelocity = 0.0f;
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, PathForward);
	Runtime.Location = PathLocation + Right * Runtime.LateralOffset;
	Runtime.Forward = (PathForward * FMath::Max(Runtime.Speed, 60.0f) + Right * Runtime.LateralVelocity)
		.GetSafeNormal2D();
	if (Runtime.Forward.IsNearlyZero())
	{
		Runtime.Forward = PathForward;
	}

	// 6. 차체가 몸을 통과하는 프레임은 없다.
	ResolvePlayerClearance(SlotIndex, Player);

	// 7. 소리와 반응.
	const FVector PlayerLocation = Player ? Player->GetActorLocation() : ListenerLocation;
	const float DistanceToPlayer = FVector::Dist2D(Runtime.Location, PlayerLocation);
	if (bBlocked && bHardBraking && !Runtime.bScreechPlayed)
	{
		Runtime.bScreechPlayed = true;
		PlayScooterScreech(SlotIndex);
		PlayScooterHorn(SlotIndex, true);
		if (AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(Player))
		{
			if (DistanceToPlayer < 700.0f)
			{
				Character->PlayScareKick(2.0f);
				if (UIGStressComponent* Stress = Character->GetStress())
				{
					Stress->ApplyScare(0.45f);
				}
			}
		}
		if (!Runtime.bBlockLinePlayed)
		{
			Runtime.bBlockLinePlayed = true;
			PlayRiderLine(NSLOCTEXT("IGMissingFloor", "ScooterRiderStartled", "아, 깜짝이야…"));
		}
	}
	if (bBlocked && Runtime.Speed < 12.0f && Runtime.bParkAtEnd
		&& PathLength - Runtime.Distance < 350.0f && Runtime.BlockedSeconds > 1.0f)
	{
		// 세울 자리에 그녀가 서 있다. 라이더는 그 자리에 그냥 세운다.
		ParkScooter(SlotIndex);
		return;
	}
	if (bBlocked && Runtime.Speed < 12.0f && !Runtime.bParkAtEnd && Runtime.BlockedSeconds > 12.0f
		&& ScooterBodies.IsValidIndex(SlotIndex)
		&& !ScooterBodies[SlotIndex]->WasRecentlyRendered(0.3f))
	{
		// 끝내 비켜 주지 않는다. 라이더는 돌아서 다른 길로 갔다(보지 않는 사이에 거둔다).
		DeactivateScooter(SlotIndex);
		return;
	}
	if (bBlocked && Runtime.Speed < 12.0f)
	{
		Runtime.BlockedSeconds += DeltaSeconds;
		if (Runtime.BlockedSeconds > 3.5f)
		{
			Runtime.bSqueezePass = true;
		}
		if (Runtime.BlockedSeconds > 0.8f && !Runtime.bBlockHornPlayed)
		{
			Runtime.bBlockHornPlayed = true;
			PlayScooterHorn(SlotIndex, false);
		}
		if (Runtime.BlockedSeconds > 2.6f && Runtime.BlockedSeconds - DeltaSeconds <= 2.6f)
		{
			PlayRiderLine(NSLOCTEXT("IGMissingFloor", "ScooterRiderPassing", "저기요, 좀 지나갈게요."));
		}
	}
	else
	{
		Runtime.BlockedSeconds = 0.0f;
	}

	// 샛길을 빠져나온 직후, 몇 걸음 앞에 사람이 있으면 미리 경적을 울린다.
	if (!Runtime.bWarningHornPlayed && bPlayerAhead && !bBlocked
		&& Gap > 220.0f && Gap < 750.0f && Runtime.Speed > 420.0f)
	{
		Runtime.bWarningHornPlayed = true;
		PlayScooterHorn(SlotIndex, Runtime.bNearMissPass);
	}

	// 그녀 옆을 지나간 순간. 바람이 얼굴을 치고 숨이 걸린다.
	const bool bAheadNow = bPlayerOnPath && Gap > 0.0f;
	if (Runtime.bWasAheadOfPlayer && !bAheadNow && !Runtime.bPassedPlayer
		&& DistanceToPlayer < 220.0f)
	{
		Runtime.bPassedPlayer = true;
		PlayScooterWhoosh(SlotIndex, Runtime.Location);
		if (AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(Player))
		{
			Character->PlayScareKick(Runtime.bNearMissPass ? 2.6f : 1.2f);
			if (UIGStressComponent* Stress = Character->GetStress())
			{
				Stress->ApplyScare(Runtime.bNearMissPass ? 0.5f : 0.22f);
			}
		}
		if (Runtime.bNearMissPass)
		{
			PlayRiderLine(NSLOCTEXT("IGMissingFloor", "ScooterRiderSorry", "아, 죄송해요!"));
			if (!bNearMissThoughtPending)
			{
				bNearMissThoughtPending = true;
				GetWorldTimerManager().SetTimer(
					NearMissThoughtHandle,
					FTimerDelegate::CreateWeakLambda(this, [this]()
					{
						AIGHorrorHUD::PushThought(
							this,
							NSLOCTEXT("IGMissingFloor", "YudamScooterThought", "…심장 떨어지는 줄 알았네."),
							3.2f);
					}),
					1.4f,
					false);
			}
		}
	}
	Runtime.bWasAheadOfPlayer = bAheadNow;

	// 8. 모습과 소리.
	ApplyScooterTransform(SlotIndex, DeltaSeconds);
	SetScooterBlocking(SlotIndex, Runtime.Speed < BlockingSpeed);

	UAudioComponent* Audio = ScooterAudio[SlotIndex];
	const FVector ListenerToSource = (Runtime.Location - ListenerLocation).GetSafeNormal();
	const FVector Velocity = Runtime.Forward * Runtime.Speed;
	const float RadialVelocity = FVector::DotProduct(Velocity, ListenerToSource);
	// 오토바이는 가까이 지나가므로 도플러가 뚜렷해야 「슝」으로 들린다.
	const float PhysicalDoppler = FMath::Clamp(
		1.0f - RadialVelocity / SoundSpeedCentimetersPerSecond * 3.0f,
		0.84f,
		1.18f);
	const float RevPitch = 0.72f + 0.5f * FMath::Clamp(Runtime.Speed / CruiseSpeed, 0.0f, 1.2f);
	Audio->SetPitchMultiplier(Runtime.BasePitch * RevPitch * PhysicalDoppler);
	const float Alpha = PathLength > 1.0f ? Runtime.Distance / PathLength : 1.0f;
	const float EdgeFade = Runtime.bParkAtEnd
		? FMath::Clamp(Alpha / 0.08f, 0.0f, 1.0f)
		: FMath::Min(
			FMath::Clamp(Alpha / 0.08f, 0.0f, 1.0f),
			FMath::Clamp((1.0f - Alpha) / 0.08f, 0.0f, 1.0f));
	const float Throttle = 0.65f + 0.35f * FMath::Clamp(Runtime.Speed / CruiseSpeed, 0.0f, 1.0f);
	Audio->SetVolumeMultiplier(Runtime.BaseVolume * EdgeFade * Throttle);

	// 서는 오토바이는 끝 20 cm 앞에서 속도가 0이 되도록 제동한다(위 ToEnd). 지나가는
	// 오토바이만 경로 끝까지 간다.
	const bool bArrived = Runtime.bParkAtEnd
		? (PathLength - Runtime.Distance <= 20.5f && Runtime.Speed < 5.0f)
		: Runtime.Distance >= PathLength - 0.5f;
	if (bArrived)
	{
		if (Runtime.bParkAtEnd)
		{
			ParkScooter(SlotIndex);
		}
		else
		{
			DeactivateScooter(SlotIndex);
		}
	}
}

void AIGNeighborhoodLifeDirector::ResolvePlayerClearance(const int32 SlotIndex, APawn* Player)
{
	using namespace IGNeighborhoodLife;
	if (!Player || !ScooterRuntime.IsValidIndex(SlotIndex))
	{
		return;
	}
	FScooterRuntime& Runtime = ScooterRuntime[SlotIndex];
	const FVector PlayerLocation = Player->GetActorLocation();
	if (FMath::Abs(PlayerLocation.Z - Runtime.Location.Z) > 220.0f)
	{
		return;
	}
	float PlayerRadius = 34.0f;
	ACharacter* Character = Cast<ACharacter>(Player);
	if (Character && Character->GetCapsuleComponent())
	{
		PlayerRadius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	}
	// 차체를 반폭 둥근 막대로 본다. 막대 끝에 반폭이 더해지므로 막대 길이는 차체
	// 길이에서 반폭만큼 덜어 내야 코와 꼬리가 실제 차체 끝에서 끝난다.
	const float CoreHalf = ScooterHalfLength - ScooterHalfWidth;
	const FVector Front = Runtime.Location + Runtime.Forward * CoreHalf;
	const FVector Rear = Runtime.Location - Runtime.Forward * CoreHalf;
	const FVector P2(PlayerLocation.X, PlayerLocation.Y, 0.0f);
	const FVector Closest = FMath::ClosestPointOnSegment(
		P2,
		FVector(Front.X, Front.Y, 0.0f),
		FVector(Rear.X, Rear.Y, 0.0f));
	const float Required = ScooterHalfWidth + PlayerRadius;
	const float Distance = FVector::Dist(P2, Closest);
	if (Distance >= Required)
	{
		return;
	}

	const FVector Right = FVector::CrossProduct(FVector::UpVector, Runtime.Forward).GetSafeNormal2D();
	FVector Away = (P2 - Closest).GetSafeNormal();
	if (Away.IsNearlyZero())
	{
		const float Side = FVector::DotProduct(P2 - FVector(Runtime.Location.X, Runtime.Location.Y, 0.0f), Right);
		Away = Side >= 0.0f ? Right : -Right;
	}

	// 몸을 차체 밖으로 옮긴다. 벽을 뚫지 않게 쓸어 가며 옮긴다.
	const float Push = Required - Distance + 2.0f;
	Player->AddActorWorldOffset(Away * Push, true);
	// 차체도 반대쪽으로 비튼다. 둘 다 조금씩 물러나야 다음 프레임에 다시 겹치지 않는다.
	const float AwaySide = FVector::DotProduct(Away, Right);
	Runtime.LateralOffset -= FMath::Sign(AwaySide) * FMath::Min(Push, 12.0f);

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	if (Now - Runtime.LastBrushTime < 0.6f)
	{
		return;
	}
	Runtime.LastBrushTime = Now;
	if (Character && Runtime.Speed > 120.0f)
	{
		// 어깨를 스쳐 몸이 옆으로 밀리고 휘청인다. 쓰러지지는 않는다.
		const float Strength = FMath::Lerp(160.0f, 430.0f, FMath::Clamp(Runtime.Speed / CruiseSpeed, 0.0f, 1.0f));
		Character->LaunchCharacter(Away * Strength + FVector(0.0f, 0.0f, 60.0f), true, false);
	}
	if (AIGPlayerCharacter* PlayerCharacter = Cast<AIGPlayerCharacter>(Player))
	{
		PlayerCharacter->PlayScareKick(3.2f);
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->ApplyScare(0.6f);
		}
	}
	if (!Runtime.bBrushed)
	{
		Runtime.bBrushed = true;
		PlayScooterHorn(SlotIndex, false);
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateClothSettle(this),
			PlayerLocation,
			0.9f,
			1.15f,
			60.0f,
			700.0f);
		PlayRiderLine(NSLOCTEXT("IGMissingFloor", "ScooterRiderBrushed", "괜찮으세요? 진짜 죄송합니다!"));
	}
}

void AIGNeighborhoodLifeDirector::ApplyScooterTransform(const int32 SlotIndex, const float DeltaSeconds)
{
	using namespace IGNeighborhoodLife;
	if (!ScooterRuntime.IsValidIndex(SlotIndex) || !ScooterRoots.IsValidIndex(SlotIndex))
	{
		return;
	}
	FScooterRuntime& Runtime = ScooterRuntime[SlotIndex];
	const float Yaw = Runtime.Forward.Rotation().Yaw;
	if (DeltaSeconds > 0.0f)
	{
		// 오른쪽으로 돌면 오른쪽으로 눕는다(UE의 + 롤은 오른쪽이 내려간다).
		const float YawRate = FMath::DegreesToRadians(
			FMath::FindDeltaAngleDegrees(Runtime.PreviousYaw, Yaw)) / DeltaSeconds;
		const float TurnAcceleration = Runtime.Speed * YawRate;
		const float TargetLean = FMath::Clamp(
			FMath::RadiansToDegrees(FMath::Atan(TurnAcceleration / 980.0f)),
			-34.0f,
			34.0f);
		Runtime.Lean = FMath::FInterpTo(Runtime.Lean, TargetLean, DeltaSeconds, 7.0f);
	}
	Runtime.PreviousYaw = Yaw;

	USceneComponent* Root = ScooterRoots[SlotIndex];
	Root->SetWorldLocationAndRotation(
		FVector(Runtime.Location.X, Runtime.Location.Y, RoadStart.Z + 5.0f),
		FRotator(0.0f, Yaw, Runtime.Lean));
}

void AIGNeighborhoodLifeDirector::ParkScooter(const int32 SlotIndex)
{
	if (!ScooterRuntime.IsValidIndex(SlotIndex))
	{
		return;
	}
	FScooterRuntime& Runtime = ScooterRuntime[SlotIndex];
	Runtime.bActive = false;
	Runtime.bParked = true;
	Runtime.Speed = 0.0f;
	Runtime.ParkedSeconds = 0.0f;
	Runtime.bEngineCut = false;
	Runtime.Lean = 0.0f;
	ApplyScooterTransform(SlotIndex, 0.0f);
	SetScooterBlocking(SlotIndex, true);
}

void AIGNeighborhoodLifeDirector::RefreshParkedScooterPresence()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !bPoolsInitialized)
	{
		return;
	}
	const bool bShouldBeParked = HasNearMissPlayed() && !Narrative->IsHourSealed();
	int32 ParkedSlot = ScooterRuntime.IndexOfByPredicate(
		[](const FScooterRuntime& Runtime) { return Runtime.bParked; });
	const bool bNearMissRunning = ScooterRuntime.ContainsByPredicate(
		[](const FScooterRuntime& Runtime) { return Runtime.bActive && Runtime.bNearMissPass; });
	if (bShouldBeParked && ParkedSlot == INDEX_NONE && !bNearMissRunning)
	{
		// 이어하기로 들어온 저녁. 정우의 오토바이는 이미 공동현관 옆 연석에 서 있다.
		ParkedSlot = ScooterRuntime.IndexOfByPredicate(
			[](const FScooterRuntime& Runtime) { return !Runtime.bActive; });
		if (ParkedSlot != INDEX_NONE)
		{
			// 콜을 기다리며 앉아 있는 날은 공동현관 쪽(동쪽)을 보고 세운다. 현관을 나서는 그녀와
			// 얼굴이 마주친다. 다른 날은 첫 저녁에 골목에서 들어와 선 그대로 서쪽을 본다.
			const float ApproachX = bParkedRiderPresent ? -90.0f : 90.0f;
			TArray<FVector> Corners = {
				FVector(IGNeighborhoodLife::ParkingX + ApproachX, IGNeighborhoodLife::ParkingY, RoadStart.Z),
				FVector(IGNeighborhoodLife::ParkingX, IGNeighborhoodLife::ParkingY, RoadStart.Z)};
			TArray<float> Radii = {0.0f, 0.0f};
			FScooterRuntime& Runtime = ScooterRuntime[ParkedSlot];
			Runtime = FScooterRuntime();
			BuildScooterPath(Corners, Radii, Runtime.Path);
			Runtime.Distance = Runtime.Path.Last().Distance;
			float MinOffset = 0.0f;
			float MaxOffset = 0.0f;
			SamplePath(Runtime, Runtime.Distance, Runtime.Location, Runtime.Forward, MinOffset, MaxOffset);
			Runtime.PreviousYaw = Runtime.Forward.Rotation().Yaw;
			Runtime.bParked = true;
			Runtime.bEngineCut = true;
			Runtime.ParkedSeconds = 10.0f;
			SetScooterVisible(ParkedSlot, true, false);
			RefreshParkedRider(ParkedSlot, true);
			ApplyScooterTransform(ParkedSlot, 0.0f);
			SetScooterBlocking(ParkedSlot, true);
		}
	}
	else if (!bShouldBeParked && ParkedSlot != INDEX_NONE)
	{
		// 그 시간에는 정우가 배달을 나가 있다. 연석은 비어 있다.
		DeactivateScooter(ParkedSlot);
	}
	else if (bShouldBeParked && ParkedSlot != INDEX_NONE && ScooterRuntime[ParkedSlot].bEngineCut)
	{
		RefreshParkedRider(ParkedSlot, false);
	}
}

void AIGNeighborhoodLifeDirector::RefreshParkedRider(const int32 SlotIndex, const bool bForce)
{
	if (!ScooterRuntime.IsValidIndex(SlotIndex) || !ScooterRuntime[SlotIndex].bParked
		|| !ScooterBodies.IsValidIndex(SlotIndex))
	{
		return;
	}
	const UStaticMeshComponent* Body = ScooterBodies[SlotIndex];
	const bool bSeated = Body && Body->GetStaticMesh() == RiddenScooterMesh;
	if (!Body || !RiddenScooterMesh || bSeated == bParkedRiderPresent
		|| (!bForce && Body->WasRecentlyRendered(0.3f)))
	{
		return;
	}
	SetScooterVisible(SlotIndex, true, bParkedRiderPresent);
	// 시동은 꺼 둔 채 앉아 있다. 기사가 탄 메시라도 불은 켜지 않는다.
	if (ScooterHeadlights.IsValidIndex(SlotIndex))
	{
		ScooterHeadlights[SlotIndex]->SetVisibility(false);
	}
	if (ScooterTailLights.IsValidIndex(SlotIndex))
	{
		ScooterTailLights[SlotIndex]->SetVisibility(false);
	}
}

void AIGNeighborhoodLifeDirector::SetParkedRiderPresent(const bool bPresent)
{
	if (bParkedRiderPresent == bPresent)
	{
		return;
	}
	bParkedRiderPresent = bPresent;
	const int32 ParkedSlot = ScooterRuntime.IndexOfByPredicate(
		[](const FScooterRuntime& Runtime) { return Runtime.bParked && Runtime.bEngineCut; });
	if (ParkedSlot == INDEX_NONE)
	{
		return;
	}
	// 보이지 않으면 다시 세워 방향까지 맞춘다. 보이는 동안에는 메시만 나중에 바꾼다.
	const UStaticMeshComponent* Body = ScooterBodies.IsValidIndex(ParkedSlot) ? ScooterBodies[ParkedSlot].Get() : nullptr;
	if (Body && !Body->WasRecentlyRendered(0.3f))
	{
		DeactivateScooter(ParkedSlot);
		RefreshParkedScooterPresence();
		return;
	}
	RefreshParkedRider(ParkedSlot, false);
}

bool AIGNeighborhoodLifeDirector::GetSeatedRiderLocation(FVector& OutLocation) const
{
	const int32 ParkedSlot = ScooterRuntime.IndexOfByPredicate(
		[](const FScooterRuntime& Runtime) { return Runtime.bParked && Runtime.bEngineCut; });
	if (ParkedSlot == INDEX_NONE || !ScooterBodies.IsValidIndex(ParkedSlot))
	{
		return false;
	}
	const UStaticMeshComponent* Body = ScooterBodies[ParkedSlot];
	if (!Body || !Body->IsVisible() || Body->GetStaticMesh() != RiddenScooterMesh)
	{
		return false;
	}
	OutLocation = ScooterRuntime[ParkedSlot].Location;
	return true;
}

void AIGNeighborhoodLifeDirector::SetScooterVisible(
	const int32 SlotIndex,
	const bool bVisible,
	const bool bWithRider)
{
	auto Show = [bVisible](UStaticMeshComponent* Component, const bool bAlso)
	{
		if (Component)
		{
			const bool bOn = bVisible && bAlso;
			Component->SetVisibility(bOn);
			Component->SetHiddenInGame(!bOn);
		}
	};
	if (ScooterBodies.IsValidIndex(SlotIndex))
	{
		UStaticMeshComponent* Body = ScooterBodies[SlotIndex];
		UStaticMesh* Wanted = bWithRider ? RiddenScooterMesh.Get() : ScooterMesh.Get();
		if (Body && Body->GetStaticMesh() != Wanted)
		{
			Body->SetStaticMesh(Wanted);
		}
		Show(Body, Wanted != nullptr);
	}
	// 불은 달리는 동안만 켠다. 세워 둔 오토바이는 시동이 꺼져 있다.
	const bool bLights = bVisible && bWithRider;
	if (ScooterHeadlights.IsValidIndex(SlotIndex))
	{
		ScooterHeadlights[SlotIndex]->SetVisibility(bLights);
	}
	if (ScooterTailLights.IsValidIndex(SlotIndex))
	{
		ScooterTailLights[SlotIndex]->SetVisibility(bLights);
	}
}

void AIGNeighborhoodLifeDirector::SetScooterBlocking(const int32 SlotIndex, const bool bBlocking)
{
	if (!ScooterBlockers.IsValidIndex(SlotIndex))
	{
		return;
	}
	UBoxComponent* Blocker = ScooterBlockers[SlotIndex];
	const ECollisionEnabled::Type Wanted =
		bBlocking ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
	if (Blocker->GetCollisionEnabled() != Wanted)
	{
		Blocker->SetCollisionEnabled(Wanted);
	}
}

void AIGNeighborhoodLifeDirector::PlayScooterHorn(const int32 SlotIndex, const bool bDouble)
{
	if (!ScooterRuntime.IsValidIndex(SlotIndex))
	{
		return;
	}
	UIGToneSequenceSoundWave* Wave = NewObject<UIGToneSequenceSoundWave>(this);
	TArray<FIGToneNote> Notes;
	// 스쿠터 전자 경적. 「빵」 하나에 기본음과 두 배음, 짧게 둘이면 「빵빵」이다.
	// 음을 반복문으로 만들면 생성 단계 깎임 감사가 읽지 못하므로 하나씩 적는다.
	if (bDouble)
	{
		Notes.Add({0.00f, 0.22f, 415.0f, 0.100f, 0.03f, 0.5f, EIGToneWaveform::SoftSquare});
		Notes.Add({0.00f, 0.22f, 830.0f, 0.035f, 0.03f, 0.5f, EIGToneWaveform::SoftSquare});
		Notes.Add({0.00f, 0.22f, 1245.0f, 0.015f, 0.03f, 0.5f, EIGToneWaveform::Triangle});
		Notes.Add({0.34f, 0.22f, 415.0f, 0.100f, 0.03f, 0.5f, EIGToneWaveform::SoftSquare});
		Notes.Add({0.34f, 0.22f, 830.0f, 0.035f, 0.03f, 0.5f, EIGToneWaveform::SoftSquare});
		Notes.Add({0.34f, 0.22f, 1245.0f, 0.015f, 0.03f, 0.5f, EIGToneWaveform::Triangle});
	}
	else
	{
		Notes.Add({0.00f, 0.42f, 415.0f, 0.100f, 0.03f, 0.5f, EIGToneWaveform::SoftSquare});
		Notes.Add({0.00f, 0.42f, 830.0f, 0.035f, 0.03f, 0.5f, EIGToneWaveform::SoftSquare});
		Notes.Add({0.00f, 0.42f, 1245.0f, 0.015f, 0.03f, 0.5f, EIGToneWaveform::Triangle});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	const FVector At = ScooterRuntime[SlotIndex].Location + FVector(0.0f, 0.0f, 90.0f);
	SpawnTransientOneShot(Wave, At, 0.85f, 260.0f, 3200.0f);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "CaptionScooterHorn", "[오토바이 경적]"),
		1.6f,
		At);
}

void AIGNeighborhoodLifeDirector::PlayScooterScreech(const int32 SlotIndex)
{
	if (!ScooterRuntime.IsValidIndex(SlotIndex))
	{
		return;
	}
	UIGToneSequenceSoundWave* Wave = NewObject<UIGToneSequenceSoundWave>(this);
	TArray<FIGToneNote> Notes;
	// 뒷바퀴가 미끄러지며 내는 끼익. 대역 잡음 셋을 겹쳐 높이를 조금씩 떨어뜨린다.
	Notes.Add({0.00f, 0.30f, 2700.0f, 0.070f, 0.05f, 0.6f, EIGToneWaveform::BandNoise, 0.65f});
	Notes.Add({0.22f, 0.30f, 2450.0f, 0.070f, 0.10f, 0.8f, EIGToneWaveform::BandNoise, 0.65f});
	Notes.Add({0.45f, 0.35f, 2200.0f, 0.060f, 0.10f, 1.4f, EIGToneWaveform::BandNoise, 0.60f});
	Notes.Add({0.00f, 0.80f, 1300.0f, 0.040f, 0.05f, 1.2f, EIGToneWaveform::BandNoise, 0.35f});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	const FVector At = ScooterRuntime[SlotIndex].Location + FVector(0.0f, 0.0f, 20.0f);
	SpawnTransientOneShot(Wave, At, 0.9f, 220.0f, 2800.0f);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "CaptionScooterScreech", "[타이어 끌리는 소리]"),
		1.6f,
		At);
}

void AIGNeighborhoodLifeDirector::PlayScooterWhoosh(const int32 SlotIndex, const FVector& At)
{
	UIGToneSequenceSoundWave* Wave = NewObject<UIGToneSequenceSoundWave>(this);
	TArray<FIGToneNote> Notes;
	// 차체가 밀어낸 공기. 귀 옆에서 부풀었다가 뒤로 빠진다.
	Notes.Add({0.00f, 0.50f, 640.0f, 0.110f, 0.40f, 1.6f, EIGToneWaveform::BandNoise, 0.15f});
	Notes.Add({0.05f, 0.40f, 1500.0f, 0.045f, 0.35f, 1.8f, EIGToneWaveform::BandNoise, 0.20f});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	SpawnTransientOneShot(Wave, At + FVector(0.0f, 0.0f, 120.0f), 0.9f, 140.0f, 900.0f);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "CaptionScooterWhoosh", "[오토바이가 스쳐 지나간다]"),
		1.8f,
		At);
	(void)SlotIndex;
}

void AIGNeighborhoodLifeDirector::PlayScooterEngineOff(const int32 SlotIndex)
{
	if (!ScooterRuntime.IsValidIndex(SlotIndex))
	{
		return;
	}
	if (ScooterAudio.IsValidIndex(SlotIndex))
	{
		ScooterAudio[SlotIndex]->Stop();
	}
	SetScooterVisible(SlotIndex, true, true);
	if (ScooterHeadlights.IsValidIndex(SlotIndex))
	{
		ScooterHeadlights[SlotIndex]->SetVisibility(false);
	}
	if (ScooterTailLights.IsValidIndex(SlotIndex))
	{
		ScooterTailLights[SlotIndex]->SetVisibility(false);
	}
	UIGToneSequenceSoundWave* Wave = NewObject<UIGToneSequenceSoundWave>(this);
	TArray<FIGToneNote> Notes;
	// 시동이 꺼지며 몇 번 털털거리고, 1초 뒤 사이드 스탠드가 내려간다.
	Notes.Add({0.00f, 0.10f, 60.0f, 0.080f, 0.05f, 1.2f, EIGToneWaveform::SoftSquare});
	Notes.Add({0.12f, 0.10f, 56.0f, 0.060f, 0.05f, 1.2f, EIGToneWaveform::SoftSquare});
	Notes.Add({0.26f, 0.12f, 52.0f, 0.040f, 0.05f, 1.4f, EIGToneWaveform::SoftSquare});
	Notes.Add({0.42f, 0.14f, 48.0f, 0.025f, 0.05f, 1.6f, EIGToneWaveform::SoftSquare});
	Notes.Add({1.10f, 0.20f, 1900.0f, 0.060f, 0.01f, 2.0f, EIGToneWaveform::Pluck, 0.30f});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	SpawnTransientOneShot(
		Wave,
		ScooterRuntime[SlotIndex].Location + FVector(0.0f, 0.0f, 40.0f),
		0.8f,
		120.0f,
		1600.0f);
}

void AIGNeighborhoodLifeDirector::PlayRiderLine(const FText& Line)
{
	AIGHorrorHUD::PushDialogue(
		this,
		NSLOCTEXT("IGMissingFloor", "ScooterRiderSpeaker", "배달 기사"),
		Line,
		EIGDialogueChannel::Conversation,
		0.0f,
		EIGDialoguePriority::Ambient);
}

void AIGNeighborhoodLifeDirector::UpdateGust(const float DeltaSeconds)
{
	if (GustElapsed < 0.0f)
	{
		return;
	}

	GustElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(GustElapsed / FMath::Max(0.01f, GustDuration), 0.0f, 1.0f);
	const float Envelope = FMath::Sin(Alpha * UE_PI);
	CurrentWindSignal = GustDirection * GustPeakStrength * Envelope;
	if (Alpha >= 1.0f)
	{
		GustElapsed = -1.0f;
		CurrentWindSignal = FVector::ZeroVector;
	}
}

void AIGNeighborhoodLifeDirector::UpdateLeaves(
	const float DeltaSeconds,
	const FVector& ListenerLocation)
{
	const float MaxDistanceSquared = FMath::Square(LeafSimulationDistance);
	for (int32 LeafIndex = 0; LeafIndex < LeafRuntime.Num(); ++LeafIndex)
	{
		FLeafRuntime& Runtime = LeafRuntime[LeafIndex];
		if (!Runtime.bActive)
		{
			continue;
		}

		if (FVector::DistSquared2D(Runtime.Position, ListenerLocation) > MaxDistanceSquared)
		{
			DeactivateLeaf(LeafIndex);
			continue;
		}

		Runtime.Age += DeltaSeconds;
		if (Runtime.Age >= Runtime.Lifetime)
		{
			DeactivateLeaf(LeafIndex);
			continue;
		}

		const float Flutter = FMath::Sin(Runtime.Phase + Runtime.Age * 8.0f);
		Runtime.Velocity += CurrentWindSignal * (80.0f * DeltaSeconds);
		Runtime.Velocity.Z += (-35.0f + Flutter * 24.0f) * DeltaSeconds;
		Runtime.Velocity *= FMath::Pow(0.82f, DeltaSeconds);
		Runtime.Position += Runtime.Velocity * DeltaSeconds;
		if (Runtime.Position.Z < 2.0f)
		{
			Runtime.Position.Z = 2.0f;
			Runtime.Velocity.Z = FMath::Abs(Runtime.Velocity.Z) * 0.18f;
			Runtime.Velocity.X *= 0.72f;
			Runtime.Velocity.Y *= 0.72f;
		}

		UStaticMeshComponent* Leaf = LeafMeshes[LeafIndex];
		Leaf->SetWorldLocation(Runtime.Position);
		Leaf->AddWorldRotation(FRotator(
			Flutter * 95.0f * DeltaSeconds,
			135.0f * DeltaSeconds,
			(90.0f + Flutter * 80.0f) * DeltaSeconds));
	}
}

void AIGNeighborhoodLifeDirector::UpdateCatTrace(const float DeltaSeconds)
{
	if (!CatRuntime.bActive)
	{
		return;
	}

	CatRuntime.Elapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(
		CatRuntime.Elapsed / FMath::Max(0.01f, CatRuntime.Duration), 0.0f, 1.0f);
	// Ease in/out keeps the trace peripheral; a linear grey blob reads like
	// an unfinished enemy pawn.
	const float SmoothedAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);
	CatTraceRoot->SetWorldLocation(FMath::Lerp(CatRuntime.Start, CatRuntime.End, SmoothedAlpha));
	const float FleetingScale = FMath::Sin(Alpha * UE_PI);
	CatTraceRoot->SetWorldScale3D(FVector(FMath::Max(0.04f, FleetingScale)));
	if (Alpha >= 1.0f)
	{
		DeactivateCatTrace();
	}
}

void AIGNeighborhoodLifeDirector::DeactivateScooter(const int32 SlotIndex)
{
	if (!ScooterRuntime.IsValidIndex(SlotIndex))
	{
		return;
	}

	ScooterRuntime[SlotIndex].bActive = false;
	ScooterRuntime[SlotIndex].bParked = false;
	SetScooterVisible(SlotIndex, false, false);
	SetScooterBlocking(SlotIndex, false);
	if (ScooterAudio.IsValidIndex(SlotIndex))
	{
		ScooterAudio[SlotIndex]->Stop();
	}
}

void AIGNeighborhoodLifeDirector::DeactivateLeaf(const int32 LeafIndex)
{
	if (!LeafRuntime.IsValidIndex(LeafIndex))
	{
		return;
	}
	LeafRuntime[LeafIndex].bActive = false;
	if (LeafMeshes.IsValidIndex(LeafIndex))
	{
		LeafMeshes[LeafIndex]->SetVisibility(false);
		LeafMeshes[LeafIndex]->SetHiddenInGame(true);
	}
}

void AIGNeighborhoodLifeDirector::DeactivateCatTrace()
{
	CatRuntime.bActive = false;
	if (CatTraceRoot)
	{
		CatTraceRoot->SetWorldScale3D(FVector::OneVector);
	}
	for (UStaticMeshComponent* Part : CatSilhouetteParts)
	{
		if (Part)
		{
			Part->SetVisibility(false);
			Part->SetHiddenInGame(true);
		}
	}
}

APawn* AIGNeighborhoodLifeDirector::GetPlayerPawn() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* Controller = World->GetFirstPlayerController())
		{
			return Controller->GetPawn();
		}
	}
	return nullptr;
}

bool AIGNeighborhoodLifeDirector::TryGetListenerLocation(FVector& OutLocation) const
{
	if (const APawn* Pawn = GetPlayerPawn())
	{
		OutLocation = Pawn->GetActorLocation();
		return true;
	}
	return false;
}

bool AIGNeighborhoodLifeDirector::IsPlayerNearRoad(const float MaxDistance) const
{
	FVector ListenerLocation;
	if (!TryGetListenerLocation(ListenerLocation))
	{
		return false;
	}
	const FVector Closest = FMath::ClosestPointOnSegment(ListenerLocation, RoadStart, RoadEnd);
	return FVector::DistSquared(ListenerLocation, Closest) <= FMath::Square(MaxDistance);
}

UIGMissingFloorNarrativeSubsystem* AIGNeighborhoodLifeDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
}

void AIGNeighborhoodLifeDirector::RefreshRuntimeUpdates()
{
	bool bHasActiveWork = GustElapsed >= 0.0f || CatRuntime.bActive;
	bHasActiveWork |= ScooterRuntime.ContainsByPredicate(
		[](const FScooterRuntime& Runtime)
		{
			return Runtime.bActive || (Runtime.bParked && Runtime.ParkedSeconds < 6.5f);
		});
	bHasActiveWork |= LeafRuntime.ContainsByPredicate(
		[](const FLeafRuntime& Runtime) { return Runtime.bActive; });

	if (!bHasActiveWork)
	{
		SetActorTickEnabled(false);
		return;
	}

	SetActorTickEnabled(true);
}
