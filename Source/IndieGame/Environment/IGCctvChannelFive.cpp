#include "Environment/IGCctvChannelFive.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"

namespace IGCctvFive
{
	/**
	 * CIF — 352×288. This is not a stand-in for a better number; it is the
	 * resolution the analog channel this monitor was built for actually carries,
	 * and it is why the low shape stays a shape. It also keeps the whole feature
	 * inside §14's render budget: about a tenth of the pixels of a 1080p capture.
	 */
	constexpr int32 FeedWidth = 352;
	constexpr int32 FeedHeight = 288;

	/**
	 * Twelve captures a second. Analog multiplexers ran 4–12 fps per channel, so
	 * this is both the authentic cadence and a fifth of the cost of rendering the
	 * capture every frame. Anything faster would buy nothing the player can see
	 * through 288 lines of snow.
	 */
	constexpr float CaptureIntervalSeconds = 1.0f / 12.0f;

	/** Snow while the tube finds sync, and snow while it loses it for good. */
	constexpr float AcquireSeconds = 0.32f;
	constexpr float CollapseSeconds = 0.86f;
	/** How long the collapse takes to reach full snow — a tear, not a fade. */
	constexpr float CollapseTearSeconds = 0.30f;

	/** Snow floor while the picture is up: a direct analog feed is never clean. */
	constexpr float LiveStaticFloor = 0.06f;
	constexpr float AcquireStaticFloor = 0.18f;

	/**
	 * Two dropouts during the live window, at seconds 1.45 and 3.90. They are
	 * authored rather than random so the low shape's crossing is never hidden
	 * behind one, and so the beat plays the same for every player.
	 */
	constexpr float GlitchTimes[] = {1.45f, 3.90f};
	constexpr float GlitchSeconds = 0.11f;
	constexpr float GlitchStatic = 0.62f;

	/** The tube's brightness. Emissive, so this is also how much it lights the desk. */
	constexpr float ScreenGain = 1.15f;

	/**
	 * Forced adaptation target for the capture. Smaller is brighter, because both
	 * bounds clamp the same value. Measured against Docs/Media/cctv5-feed.png —
	 * the first authored guess of 0.62 produced a frame that passed every
	 * brightness floor and showed nothing but the bulb.
	 */
	constexpr float PinnedExposure = 0.045f;
	constexpr float ExposureBias = 0.85f;

	/** 녹화기 위 모니터의 액정 개구부. 씬과 같은 중심 높이를 쓴다. */
	const FVector ScreenCenter(150.0f, -105.45f, AIGPrologueWorldScene::CctvScreenCenterZ);
	// 화면을 세운 것은 씬이다. 렌더 면은 그 치수를 받아 쓴다.
	constexpr float ScreenWidth = AIGPrologueWorldScene::CctvScreenWidth;
	constexpr float ScreenHeight = AIGPrologueWorldScene::CctvScreenHeight;

	/**
	 * §5.5's label, taped across the rear third of the monitor's top case, over
	 * the input panel. That is where it goes in a real booth — the back of a
	 * monitor pushed up against a wall is not somewhere anyone can read — and it
	 * is legible from the front when she leans over the desk.
	 */
	const FVector LabelCenter(150.0f, -97.6f, 121.5f);
	const FVector LabelSize(12.8f, 4.8f, 0.5f);

	/**
	 * 화면에는 아무것도 지나가지 않는다. 대신 화면이 살아 있는 동안 건물이
	 * 소리를 낸다 — 위에서, 기는 걸음 하나. 빈 복도를 보면서 그 복도가 비어
	 * 있지 않다는 것을 귀로 안다. 형체 세 상자를 지우고 나서야 이 컷이 §4.6
	 * (그를 보여 주지 않는다)과 §5.5(그 시간은 기계에 담기지 않는다)와 같은
	 * 말을 하게 됐다. 밤3에 같은 화각에 직접 서는 회수는 복도와 전구로 남는다.
	 */
	constexpr float LiveSoundProgress = 0.46f;
	/** 부속동 복도, 카메라가 보는 그 자리. */
	const FVector LiveSoundLocation(-190.0f, 680.0f, 1213.0f);
	constexpr float LiveSoundInnerRadius = 500.0f;
	constexpr float LiveSoundFalloff = 4200.0f;
	/**
	 * 걸음 하나로는 14미터와 슬래브를 못 넘었다. CRT 베드와 60Hz 험 옆에서
	 * 0.3초짜리 합성 한 걸음은 스피커로 사실상 안 들렸다. 밤1과 그의 실제
	 * 걸음과 같은 녹음으로 세 걸음을 친다. 셋째 걸음이 두 번째 끊김(3.90초)
	 * 안에 떨어져 지나가는 순간 화면이 찢긴다. 4.6초짜리 _2는 쓰지 않는다.
	 */
	constexpr int32 LiveStepCount = 3;
	constexpr float LiveStepSpacingSeconds = 0.67f;
	const TCHAR* const LiveStepSamples[LiveStepCount] = {
		TEXT("Entity_CrawlStep_0"), TEXT("Entity_CrawlStep_1"), TEXT("Entity_CrawlStep_0")};
	constexpr float LiveStepVolumes[LiveStepCount] = {0.80f, 0.85f, 0.75f};
	constexpr float LiveStepPitches[LiveStepCount] = {0.97f, 1.02f, 0.93f};
	/** 복도를 따라 옮겨 가는 자리. Y와 높이는 그대로다. */
	constexpr float LiveStepX[LiveStepCount] = {-190.0f, -120.0f, -50.0f};
	/**
	 * 【S】0.65(§8 2-2). 둘째 걸음에 건다 — 0.4를 넘어 들숨이 따라오고, 첫
	 * 걸음은 숨에 가리지 않고 들린다.
	 */
	constexpr int32 LiveScareStep = 1;
	constexpr float LiveScareAmount = 0.65f;

	/** The monitor is at her elbow, so it is close and quiet on the world bus. */
	constexpr float MonitorInnerRadius = 90.0f;
	constexpr float MonitorFalloff = 720.0f;
	constexpr float SwitchVolume = 0.52f;
	constexpr float BedVolume = 0.34f;
}

AIGCctvChannelFive::AIGCctvChannelFive()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	USceneComponent* Root =
		CreateDefaultSubobject<USceneComponent>(TEXT("CctvChannelFiveRoot"));
	SetRootComponent(Root);
}

bool AIGCctvChannelFive::Configure(AIGPrologueWorldScene* InScene)
{
	UWorld* World = GetWorld();
	if (!World || !InScene)
	{
		return false;
	}
	Scene = InScene;

	PlaneMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	CubeMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!PlaneMesh || !CubeMesh)
	{
		return false;
	}

	// Loaded here, at night start, and never on the press. A synchronous load in
	// the middle of a beat that plays exactly once would be a hitch nobody can
	// replay to check.
	ScreenMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Prototype/Materials/M_CctvChannelFive.M_CctvChannelFive"));
	if (!ScreenMaterial)
	{
		return false;
	}

	// The channel-5 face, hidden under the four-way split until it is pressed.
	// Roll 90 makes texture V vertical and leaves the plane normal on -Y, facing
	// whoever is standing at the desk — the same orientation the listener's card
	// uses, for the same reason.
	ScreenFace = NewObject<UStaticMeshComponent>(this, TEXT("CctvScreenFace"));
	if (!ScreenFace)
	{
		return false;
	}
	ScreenFace->SetupAttachment(GetRootComponent());
	ScreenFace->RegisterComponent();
	ScreenFace->SetStaticMesh(PlaneMesh);
	ScreenFace->SetAbsolute(true, true, true);
	ScreenFace->SetWorldLocationAndRotation(
		IGCctvFive::ScreenCenter,
		FRotator(0.0f, 180.0f, 90.0f));
	// 화면의 앞면이 책상 앞을 향해야 단면 재질도 보인다.
	if (FVector::DotProduct(ScreenFace->GetUpVector(), FVector(0, -1, 0)) < 0.99f)
	{
		UE_LOG(LogTemp, Error, TEXT("CCTV screen faces away from the desk"));
		return false;
	}
	ScreenFace->SetWorldScale3D(
		FVector(
			IGCctvFive::ScreenWidth / 100.0f,
			IGCctvFive::ScreenHeight / 100.0f,
			1.0f));
	ScreenFace->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ScreenFace->SetCanEverAffectNavigation(false);
	ScreenFace->SetCastShadow(false);
	ScreenFace->SetHiddenInGame(true);

	ScreenInstance =
		UMaterialInstanceDynamic::Create(ScreenMaterial, this);
	if (!ScreenInstance)
	{
		return false;
	}
	ScreenFace->SetMaterial(0, ScreenInstance);
	StaticMix = 1.0f;
	ApplyMaterialParameters();

	// §5.5 물리적 확인. The label is permanent: it must be readable before the
	// press, so a player who finds it first understands what channel 5 is before
	// they ever see it, and readable afterwards, when it is the only proof left.
	if (UMaterialInterface* LabelMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/Prototype/Materials/M_SignAux5MonitorOnly."
				 "M_SignAux5MonitorOnly")))
	{
		if (UStaticMeshComponent* Label =
			NewObject<UStaticMeshComponent>(this, TEXT("CctvAuxLabel")))
		{
			Label->SetupAttachment(GetRootComponent());
			Label->RegisterComponent();
			Label->SetStaticMesh(CubeMesh);
			Label->SetMaterial(0, LabelMaterial);
			Label->SetAbsolute(true, true, true);
			// 라벨은 모니터 위에 눕는다. 회전을 주지 않으면 윗면의 글자가
			// 평면 안에서 180도 돌아 앉아, 앞에 선 사람에게 「AUX 5 MONITOR
			// ONLY」가 거꾸로 읽힌다.
			Label->SetWorldLocationAndRotation(
				IGCctvFive::LabelCenter,
				FRotator(0.0f, 180.0f, 0.0f));
			Label->SetWorldScale3D(IGCctvFive::LabelSize / 100.0f);
			Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Label->SetCanEverAffectNavigation(false);
			Label->SetCastShadow(false);
		}
	}

	return true;
}

bool AIGCctvChannelFive::IsOnScreen() const
{
	return State == EIGCctvChannelState::Acquiring
		|| State == EIGCctvChannelState::Live
		|| State == EIGCctvChannelState::Collapsing;
}

FIntPoint AIGCctvChannelFive::GetExpectedFeedResolution() const
{
	// 저작용 배율. 출하 값은 CIF 그대로이고, 진단 실행에서만 같은 화각을 크게
	// 내보내 프레임 안의 무엇이 무엇인지 눈으로 가린다 — 352×288 썸네일을 보고
	// 「어느 밝은 모양이 무엇인가」를 추측하는 것은 이미 두 번 틀렸다. 할당과
	// 계약 검사가 같은 함수를 읽으므로 둘이 어긋날 수 없다.
	int32 FeedScale = 1;
	FParse::Value(FCommandLine::Get(), TEXT("IGCctvFeedScale="), FeedScale);
	FeedScale = FMath::Clamp(FeedScale, 1, 6);
	return FIntPoint(
		IGCctvFive::FeedWidth * FeedScale,
		IGCctvFive::FeedHeight * FeedScale);
}

FIntPoint AIGCctvChannelFive::GetFeedResolution() const
{
	return Feed ? FIntPoint(Feed->SizeX, Feed->SizeY) : FIntPoint::ZeroValue;
}

bool AIGCctvChannelFive::Play()
{
	UWorld* World = GetWorld();
	AIGPrologueWorldScene* SceneActor = Scene.Get();
	if (!World || !SceneActor || !ScreenFace || !ScreenInstance)
	{
		return false;
	}
	// 1회 한정. Enforced here as well as by the narrative beat flag, so no future
	// caller can accidentally give the player a second look.
	if (bUsed)
	{
		return false;
	}
	bUsed = true;
	bLiveSoundPlayed = false;
	LiveStepsPlayed = 0;

	// §14 상시 렌더 금지 — everything the channel costs is allocated on this line
	// and released again when it dies.
	Feed = NewObject<UTextureRenderTarget2D>(this, TEXT("CctvChannelFiveFeed"));
	if (!Feed)
	{
		return false;
	}
	// RTF_RGBA8_SRGB pairs with the material's Color sampler: SCS_FinalColorLDR
	// writes gamma-encoded pixels, and the screen has to emit light proportional
	// to the linear value, not to the encoding.
	Feed->RenderTargetFormat = RTF_RGBA8_SRGB;
	Feed->ClearColor = FLinearColor::Black;
	Feed->bAutoGenerateMips = false;
	Feed->AddressX = TA_Clamp;
	Feed->AddressY = TA_Clamp;
	const FIntPoint FeedSize = GetExpectedFeedResolution();
	Feed->InitAutoFormat(FeedSize.X, FeedSize.Y);

	Capture = NewObject<USceneCaptureComponent2D>(
		this, TEXT("CctvChannelFiveCapture"));
	if (!Capture)
	{
		ReleaseChannel();
		return false;
	}
	Capture->SetupAttachment(GetRootComponent());
	Capture->RegisterComponent();
	// 관리실은 1층이라 층별 조명 구역이 5층 등을 꺼 두었다. 화면이 사는 동안 켠다.
	SceneActor->SetRemoteViewActive(true);
	bHoldsRemoteView = true;
	Capture->SetAbsolute(true, true, true);
	Capture->SetWorldLocationAndRotation(
		SceneActor->GetMissingFloorCctvCameraLocation(),
		SceneActor->GetMissingFloorCctvCameraRotation());
	Capture->FOVAngle = SceneActor->GetMissingFloorCctvFieldOfView();
	Capture->TextureTarget = Feed;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	// Driven by CaptureScene() on a cadence instead. Persisting the rendering
	// state keeps virtual shadow map caching from thrashing on a view that
	// appears and disappears; with temporal AA off it costs nothing in ghosting.
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->ShowFlags.SetTemporalAA(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetBloom(false);
	// The annex has one bare bulb and the camera's own illuminator. Pinning the
	// exposure to a single value is what makes the far end of the corridor stay
	// black instead of being lifted into readable grey by eye adaptation — the
	// darkness at the end of that room is the reason the shape is frightening.
	//
	// Min and Max both clamp the adaptation target, so a *smaller* value is a
	// brighter picture. This one was measured against the exported frame, not
	// reasoned about: -IGCctvExposure= sweeps it without a rebuild.
	float Exposure = IGCctvFive::PinnedExposure;
	FParse::Value(FCommandLine::Get(), TEXT("IGCctvExposure="), Exposure);
	FPostProcessSettings& Post = Capture->PostProcessSettings;
	Post.bOverride_AutoExposureMinBrightness = true;
	Post.AutoExposureMinBrightness = Exposure;
	Post.bOverride_AutoExposureMaxBrightness = true;
	Post.AutoExposureMaxBrightness = Exposure;
	// A cheap sensor runs its gain up in a dark room, and the noise that comes
	// with it is the monitor material's snow.
	Post.bOverride_AutoExposureBias = true;
	Post.AutoExposureBias = IGCctvFive::ExposureBias;
	Post.bOverride_BloomIntensity = true;
	Post.BloomIntensity = 0.0f;
	Post.bOverride_MotionBlurAmount = true;
	Post.MotionBlurAmount = 0.0f;
	Post.bOverride_SceneFringeIntensity = true;
	Post.SceneFringeIntensity = 0.0f;
	// The monitor material owns both of these; doing them twice would read as a
	// filter over a filter rather than as a screen.
	Post.bOverride_VignetteIntensity = true;
	Post.VignetteIntensity = 0.0f;
	Post.bOverride_FilmGrainIntensity = true;
	Post.FilmGrainIntensity = 0.0f;
	// 형체는 없다. 복도, 자재, 비닐, 전구 하나 — 카메라가 보는 것은 그게 다다.
	// 지나가는 것은 소리로만 온다(UpdateLiveSound).

	ScreenFace->SetHiddenInGame(false);
	// The bed spans the picture and the tear, so the tube's dim ends underneath
	// the noise that kills it rather than in silence.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateCrtChannelBed(
			this,
			IGCctvFive::AcquireSeconds + LiveSeconds + IGCctvFive::CollapseSeconds),
		IGCctvFive::ScreenCenter,
		IGCctvFive::BedVolume,
		1.0f,
		IGCctvFive::MonitorInnerRadius,
		IGCctvFive::MonitorFalloff);
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateCrtChannelSwitch(this, false),
		IGCctvFive::ScreenCenter,
		IGCctvFive::SwitchVolume,
		1.0f,
		IGCctvFive::MonitorInnerRadius,
		IGCctvFive::MonitorFalloff);

	EnterState(EIGCctvChannelState::Acquiring);
	SetActorTickEnabled(true);
	return true;
}

void AIGCctvChannelFive::EnterState(const EIGCctvChannelState NextState)
{
	State = NextState;
	StateSeconds = 0.0f;
	switch (State)
	{
	case EIGCctvChannelState::Acquiring:
		StaticMix = 1.0f;
		// One capture immediately, so the first frame the snow clears onto is the
		// corridor and not a black target.
		SinceCaptureSeconds = IGCctvFive::CaptureIntervalSeconds;
		break;

	case EIGCctvChannelState::Live:
		StaticMix = IGCctvFive::LiveStaticFloor;
		break;

	case EIGCctvChannelState::Collapsing:
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateCrtChannelSwitch(this, true),
			IGCctvFive::ScreenCenter,
			IGCctvFive::SwitchVolume,
			1.0f,
			IGCctvFive::MonitorInnerRadius,
			IGCctvFive::MonitorFalloff);
		// The picture is gone the moment the tear starts. Tick stops issuing
		// captures in this state, so no frame is rendered that nobody will see.
		break;

	case EIGCctvChannelState::Spent:
		StaticMix = 1.0f;
		if (ScreenFace)
		{
			ScreenFace->SetHiddenInGame(true);
		}
		ReleaseChannel();
		SetActorTickEnabled(false);
		break;

	default:
		break;
	}
	ApplyMaterialParameters();
}

void AIGCctvChannelFive::ReleaseChannel()
{
	if (bHoldsRemoteView)
	{
		bHoldsRemoteView = false;
		if (AIGPrologueWorldScene* SceneActor = Scene.Get())
		{
			SceneActor->SetRemoteViewActive(false);
		}
	}
	if (Capture)
	{
		Capture->TextureTarget = nullptr;
		Capture->DestroyComponent();
		Capture = nullptr;
	}
	if (Feed)
	{
		// Hand the target back before the material can sample a released
		// resource; the parameter falls back to the graph's engine black.
		if (ScreenInstance)
		{
			ScreenInstance->SetTextureParameterValue(TEXT("Feed"), nullptr);
		}
		Feed->ReleaseResource();
		Feed = nullptr;
	}
}

void AIGCctvChannelFive::ApplyMaterialParameters()
{
	if (!ScreenInstance)
	{
		return;
	}
	ScreenInstance->SetScalarParameterValue(TEXT("Static"), StaticMix);
	ScreenInstance->SetScalarParameterValue(
		TEXT("Gain"), IGCctvFive::ScreenGain);
	ScreenInstance->SetTextureParameterValue(TEXT("Feed"), Feed);
}

void AIGCctvChannelFive::UpdateLiveSound(const float LiveProgress01)
{
	if (LiveStepsPlayed >= IGCctvFive::LiveStepCount
		|| LiveProgress01 < IGCctvFive::LiveSoundProgress)
	{
		return;
	}
	const float LiveSecondsNow = LiveProgress01 * LiveSeconds;
	const float FirstStepSeconds = IGCctvFive::LiveSoundProgress * LiveSeconds;
	while (LiveStepsPlayed < IGCctvFive::LiveStepCount
		&& LiveSecondsNow >= FirstStepSeconds
			+ IGCctvFive::LiveStepSpacingSeconds * LiveStepsPlayed)
	{
		const int32 Step = LiveStepsPlayed++;
		// 화면은 비어 있고 소리는 그 복도에서 난다. 관리실은 1층이라 거리가
		// 멀고, 멀어서 맞다 — 슬래브의 먹먹함이 「위에서」를 말한다.
		const FVector StepLocation(
			IGCctvFive::LiveStepX[Step],
			IGCctvFive::LiveSoundLocation.Y,
			IGCctvFive::LiveSoundLocation.Z);
		IGAudio::SpawnOneShotAt(
			this,
			IGAudio::SampleOr(
				IGCctvFive::LiveStepSamples[Step],
				[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateEntityCrawlStep(this, false); }),
			StepLocation,
			IGCctvFive::LiveStepVolumes[Step],
			IGCctvFive::LiveStepPitches[Step],
			IGCctvFive::LiveSoundInnerRadius,
			IGCctvFive::LiveSoundFalloff,
			EIGAudioBus::Entity);
		if (Step == 0)
		{
			bLiveSoundPlayed = true;
			// 자막은 한 번. 방위를 붙인다 — 위.
			AIGHorrorHUD::PushAudioCaptionAt(
				this,
				NSLOCTEXT("IGMissingFloor", "CctvLiveSoundCaption", "무언가 기어가는 소리"),
				2.4f,
				StepLocation);
		}
		else if (Step == IGCctvFive::LiveScareStep)
		{
			// 빈 화면과 들리는 소리가 어긋나는 것이 이 컷의 공포다. 몸이 먼저
			// 알아듣는다.
			if (const AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(
				UGameplayStatics::GetPlayerPawn(this, 0)))
			{
				if (UIGStressComponent* Stress = Character->GetStress())
				{
					Stress->ApplyScare(IGCctvFive::LiveScareAmount);
				}
			}
		}
	}
}

void AIGCctvChannelFive::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StateSeconds += DeltaSeconds;

	switch (State)
	{
	case EIGCctvChannelState::Acquiring:
	{
		const float Progress = FMath::Clamp(
			StateSeconds / IGCctvFive::AcquireSeconds, 0.0f, 1.0f);
		StaticMix = FMath::Lerp(1.0f, IGCctvFive::AcquireStaticFloor, Progress);
		if (StateSeconds >= IGCctvFive::AcquireSeconds)
		{
			EnterState(EIGCctvChannelState::Live);
			return;
		}
		break;
	}

	case EIGCctvChannelState::Live:
	{
		StaticMix = IGCctvFive::LiveStaticFloor;
		for (const float GlitchTime : IGCctvFive::GlitchTimes)
		{
			if (StateSeconds >= GlitchTime
				&& StateSeconds < GlitchTime + IGCctvFive::GlitchSeconds)
			{
				StaticMix = IGCctvFive::GlitchStatic;
			}
		}
		UpdateLiveSound(FMath::Clamp(StateSeconds / LiveSeconds, 0.0f, 1.0f));
		if (StateSeconds >= LiveSeconds)
		{
			EnterState(EIGCctvChannelState::Collapsing);
			return;
		}
		break;
	}

	case EIGCctvChannelState::Collapsing:
	{
		const float Tear = FMath::Clamp(
			StateSeconds / IGCctvFive::CollapseTearSeconds, 0.0f, 1.0f);
		StaticMix = FMath::Lerp(IGCctvFive::AcquireStaticFloor, 1.0f, Tear);
		if (StateSeconds >= IGCctvFive::CollapseSeconds)
		{
			EnterState(EIGCctvChannelState::Spent);
			return;
		}
		break;
	}

	default:
		SetActorTickEnabled(false);
		return;
	}

	// The capture runs only while there is a picture to take.
	if (Capture && State != EIGCctvChannelState::Collapsing)
	{
		SinceCaptureSeconds += DeltaSeconds;
		if (SinceCaptureSeconds >= IGCctvFive::CaptureIntervalSeconds)
		{
			SinceCaptureSeconds = 0.0f;
			Capture->CaptureScene();
			++CaptureCount;
		}
	}
	ApplyMaterialParameters();
}

void AIGCctvChannelFive::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseChannel();
	Super::EndPlay(EndPlayReason);
}
