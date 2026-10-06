#include "Entity/IGNightThreatDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/CollisionProfile.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorEpilogueDirector.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorFifthDawnDirector.h"
#include "Entity/IGMissingFloorNightTwoBeatDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Entity/IGShadowFigure.h"
#include "Interaction/IGHidingSpot.h"
#include "Interaction/IGSwingDoor.h"
#include "Materials/MaterialInterface.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"
#include "TimerManager.h"

namespace IGNightThreat
{
	constexpr float UpdateSeconds = 0.1f;
	// §7. 잡혔다 깬 뒤 이만큼은 아무것도 오지 않는다.
	constexpr double CaptureGraceSeconds = 40.0;

	// --- 어둑시니 ---
	// 손전등을 끈 이만큼 어두운 자리에서 시간이 쌓인다(0~1, 1이 칠흑).
	constexpr float DarkThreshold = 0.7f;
	// 방의 불빛(손전등 제외)이 이보다 밝으면 견디지 못하고 사라진다.
	constexpr float LitThreshold = 0.4f;
	// 밤마다 나타나기까지 쌓여야 하는 어둠(초). 밤이 깊을수록 짧다.
	constexpr float DarknessSecondsByNight[] = {30.0f, 30.0f, 24.0f, 18.0f, 14.0f};
	// 처음 서는 크기. 1.1 m 남짓, 웅크린 어둠이다.
	constexpr float StartGrowth = 0.15f;
	// 쳐다보는 동안 1초에 크는 양. 손전등을 비추면 두 배.
	constexpr float GrowthPerSecond = 0.10f;
	// 눈을 돌리면 1초에 줄어드는 양.
	constexpr float ShrinkPerSecond = 0.06f;
	// 이만큼 눈을 돌리고 있으면 사라진다.
	constexpr float UnseenVanishSeconds = 3.0f;
	// 시선이 닿았다고 보는 반각. 화면 가운데에서 이 안이면 「쳐다본다」.
	constexpr float LookHalfAngle = 28.0f;
	// 손전등 원뿔의 반각(바깥 원뿔 34도의 절반).
	constexpr float TorchHalfAngle = 17.0f;
	// 다가오는 빠르기(cm/s). 클수록 빨라지고, 손전등을 비추면 1.5배.
	constexpr float ApproachBaseSpeed = 30.0f;
	constexpr float ApproachGrowthSpeed = 90.0f;
	// 몸이 멈추는 거리. 이보다 가까이는 오지 않고, 다 자라면 덮친다.
	constexpr float ApproachStopDistance = 120.0f;
	constexpr float CaptureGrowth = 0.95f;
	constexpr float CaptureDistance = 220.0f;
	// 사라진 뒤 다시 어둠이 쌓이기 시작하기까지.
	constexpr double ManifestCooldownSeconds = 25.0;

	// --- 손님 ---
	// 403호 안에 이만큼 머물러야 온다.
	constexpr float RequiredHomeDwellSeconds = 25.0f;
	// 그 시간이 시작되고 이만큼 지난 뒤, 이만큼 전까지만 온다(05:30 마감 앞은 비운다).
	constexpr float GuestEarliestHourSeconds = 150.0f;
	constexpr float GuestLatestHourSeconds = 960.0f;
	// 대본 시각(초). 노크 → 말 → 노크 → 말 → 도어락 → 풀림 → 문.
	constexpr float KnockOneAt = 0.0f;
	constexpr float LineOneAt = 2.4f;
	constexpr float KnockTwoAt = 6.0f;
	constexpr float LineTwoAt = 8.2f;
	// 처음 누른 번호는 틀린다. 경고음 뒤 다시 누를 때까지가 걸쇠를 걸 마지막 틈이다.
	constexpr float KeypadAt = 12.0f;
	constexpr float WrongCodeAt = 13.7f;
	constexpr float KeypadAgainAt = 16.0f;
	constexpr float UnlockedAt = 17.9f;
	constexpr float DoorAt = 18.4f;
	// 걸쇠에 걸린 뒤.
	constexpr float LatchLineDelay = 1.2f;
	constexpr float LatchLeaveDelay = 4.6f;
	// 들어온 몸이 걷는 빠르기(cm/s). 숨은 사람을 찾을 때는 느리게, 보이면 빠르게.
	constexpr float SearchSpeed = 60.0f;
	constexpr float PursueSpeed = 150.0f;
	constexpr float GuestReach = 140.0f;
	// 숨은 사람의 소리를 듣는 거리.
	constexpr float GuestHearing = 320.0f;
	// 들어와 찾는 시간. 그 뒤로는 문으로 돌아가 나간다.
	constexpr float SearchHoldSeconds = 5.0f;
	// 보이는 사람을 쫓다 이보다 멀어지면 포기한다.
	constexpr float GiveUpDistance = 900.0f;
	// 발 높이가 이만큼 넘게 다르면 다른 층이다. 계단으로 달아났으면 손이 닿지 않고
	// 소리도 듣지 못한다. 문 너머 층까지 따라가지 않는다.
	constexpr float SameFloorTolerance = 150.0f;
	// 손님의 몸 크기(0~1). 2.1 m, 문틀만 하다. 고개를 숙이고 들어온다.
	constexpr float GuestGrowth = 0.55f;
	// 철문 너머 말소리의 높낮이. 밤2 나린(젊은 여자), 밤3 배달 기사, 밤4 오빠.
	constexpr float MurmurPitchByNight[] = {1.0f, 1.0f, 1.32f, 0.92f, 0.86f};
	// 들어와 움직이는 동안 종이가 스치는 간격.
	constexpr float RustleSeconds = 0.6f;
	// 문 밑으로 들어오는 종이. 두 번째 말 뒤, 도어락을 누르기 전에 민다.
	constexpr float PaperSlideAt = 9.6f;
	// 세 번에 나눠 민다. 민 시각(초)과 그때 앞 가장자리가 문짝 안쪽 면을 넘어 들어온
	// 거리(cm). 처음에는 문짝 밖에 숨어 있다. 마지막에는 현관 바닥에 한 뼘쯤 나온다.
	constexpr float PaperShoveTimes[] = {0.0f, 0.55f, 1.3f};
	constexpr float PaperShoveReach[] = {2.0f, 13.0f, 27.0f};
	constexpr float PaperStartReach = -4.0f;
	constexpr float PaperShoveSeconds = 0.2f;
	// A4를 세로로 민다. 문 가운데에서 조금 비켜서, 조금 비뚤게. 전단 메시의 크기와 같다.
	constexpr float PaperLengthCm = 29.7f;
	constexpr float PaperWidthCm = 21.0f;
	constexpr float PaperOffsetX = -7.0f;
	constexpr float PaperYawDegrees = 5.0f;
	// 문짝 두께의 절반. 문짝 안쪽 면이 HomeDoorY에서 이만큼 안쪽(+Y)이다.
	constexpr float DoorHalfThickness = 2.5f;
	// 들어와 돌아다니는 동안 떨어지는 종이. 스치는 소리 세 번에 한 장, 한 번 오면 넷까지.
	constexpr int32 RustlesPerFallenPaper = 3;
	constexpr int32 MaxFallenPapers = 4;
	constexpr float FallenPaperSpread = 28.0f;
}

AIGNightThreatDirector::AIGNightThreatDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AIGNightThreatDirector::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGListenerEntity* InListener,
	AIGNightLoopDirector* InNightLoop,
	AIGNightPhaseDirector* InNightPhase)
{
	Scene = InScene;
	PlayerPawn = InPlayer;
	Listener = InListener;
	NightLoop = InNightLoop;
	NightPhase = InNightPhase;
	HomeDoor = InScene ? InScene->GetHomeDoor() : nullptr;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FActorSpawnParameters FigureParameters;
	FigureParameters.Owner = this;
	FigureParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!Eoduksini)
	{
		Eoduksini = World->SpawnActor<AIGShadowFigure>(
			AIGShadowFigure::StaticClass(), FTransform::Identity, FigureParameters);
	}
	if (!Guest)
	{
		Guest = World->SpawnActor<AIGShadowFigure>(
			AIGShadowFigure::StaticClass(), FTransform::Identity, FigureParameters);
		if (Guest)
		{
			Guest->DressAsPaper();
		}
	}
	if (Eoduksini && !EoduksiniBreath)
	{
		// 어둑시니의 숨. 몸을 따라다니고, 크기에 맞춰 볼륨과 높이가 오른다.
		EoduksiniBreath = NewObject<UAudioComponent>(this);
		EoduksiniBreath->bAutoActivate = false;
		EoduksiniBreath->RegisterComponent();
		EoduksiniBreath->AttachToComponent(
			Eoduksini->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		EoduksiniBreath->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
		EoduksiniBreath->SetSound(UIGToneSequenceSoundWave::CreateDarknessBreathLoop(this));
		EoduksiniBreath->AttenuationSettings = IGAudio::MakeAttenuation(
			this, 140.0f, 1300.0f, EIGAudioBus::Entity);
		EoduksiniBreath->bAllowSpatialization = true;
		if (UIGMissingFloorAudioSubsystem* AudioDirector = World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->PrepareSound(EoduksiniBreath->Sound, EIGAudioBus::Entity);
		}
	}
	if (!PaperUnderDoor)
	{
		PaperUnderDoor = NewObject<UStaticMeshComponent>(this, TEXT("GuestPaperUnderDoor"));
		PaperUnderDoor->SetMobility(EComponentMobility::Movable);
		// 공동현관 옆에 붙어 있던 「원룸 있습니다」 전단과 같은 종이다. 메시가 없으면 흰 종이.
		if (UStaticMesh* Notice = LoadObject<UStaticMesh>(
				nullptr, TEXT("/Game/Meshes/SM_RentalNoticeA4.SM_RentalNoticeA4"), nullptr, LOAD_NoWarn))
		{
			PaperUnderDoor->SetStaticMesh(Notice);
		}
		else
		{
			bPaperUnderDoorIsCube = true;
			PaperUnderDoor->SetStaticMesh(
				LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"), nullptr, LOAD_NoWarn));
			if (UMaterialInterface* Blank = LoadObject<UMaterialInterface>(
					nullptr, TEXT("/Game/Prototype/Materials/M_PaperClean.M_PaperClean"), nullptr, LOAD_NoWarn))
			{
				PaperUnderDoor->SetMaterial(0, Blank);
			}
			// 큐브는 100 cm다. 두께는 1 mm로 둔다. 더 얇으면 멀리서 깜박인다.
			PaperUnderDoor->SetRelativeScale3D(FVector(
				IGNightThreat::PaperWidthCm / 100.0f, IGNightThreat::PaperLengthCm / 100.0f, 0.001f));
		}
		PaperUnderDoor->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		PaperUnderDoor->SetGenerateOverlapEvents(false);
		PaperUnderDoor->SetCanEverAffectNavigation(false);
		PaperUnderDoor->SetCastShadow(false);
		PaperUnderDoor->SetVisibility(false);
		PaperUnderDoor->RegisterComponent();
	}
	if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
	{
		Noise->OnNoiseReported.Remove(NoiseHandle);
		NoiseHandle = Noise->OnNoiseReported.AddUObject(this, &AIGNightThreatDirector::HandleNoise);
	}
	GetWorldTimerManager().SetTimer(
		UpdateTimer, this, &AIGNightThreatDirector::Update, IGNightThreat::UpdateSeconds, true);
}

void AIGNightThreatDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(UpdateTimer);
	// 빌려 쓴 문구멍과 화면 가장자리는 돌려놓고 간다.
	SetPeepholeOffered(false);
	if (AIGPlayerCharacter* Player = PlayerPawn.Get())
	{
		Player->SetThreatVignetteTarget(0.0f);
	}
	// 두 몸은 이 디렉터가 세웠다. 스테이지를 다시 세울 때 남아 있으면 안 된다.
	for (AIGShadowFigure* Figure : {Eoduksini.Get(), Guest.Get()})
	{
		if (IsValid(Figure))
		{
			Figure->Destroy();
		}
	}
	Eoduksini = nullptr;
	Guest = nullptr;
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->OnNoiseReported.Remove(NoiseHandle);
		}
	}
	Super::EndPlay(EndPlayReason);
}

UIGMissingFloorNarrativeSubsystem* AIGNightThreatDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
}

void AIGNightThreatDirector::ThinkOnce(const TCHAR* BeatId, const FText& Thought, const float DelaySeconds)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (Narrative && Narrative->HasBeatPlayed(FName(BeatId)))
	{
		return;
	}
	if (Narrative)
	{
		Narrative->MarkBeatPlayed(FName(BeatId));
	}
	if (DelaySeconds <= 0.0f)
	{
		AIGHorrorHUD::PushThought(this, Thought, 3.2f);
		return;
	}
	FTimerHandle ThoughtTimer;
	GetWorldTimerManager().SetTimer(
		ThoughtTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, Thought]()
		{
			AIGHorrorHUD::PushThought(this, Thought, 3.2f);
		}),
		DelaySeconds,
		false);
}

void AIGNightThreatDirector::Update()
{
	const UWorld* World = GetWorld();
	AIGPlayerCharacter* Player = PlayerPawn.Get();
	if (!World || !Player)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const float DeltaSeconds = LastUpdateSeconds < 0.0
		? IGNightThreat::UpdateSeconds
		: FMath::Clamp(static_cast<float>(Now - LastUpdateSeconds), 0.0f, 0.5f);
	LastUpdateSeconds = Now;

	const AIGNightPhaseDirector* Phase = NightPhase.Get();
	if (!Phase || !Phase->IsHourActive())
	{
		// 낮이나 막간. 남아 있던 것은 거둔다.
		if (IsEoduksiniManifested())
		{
			DismissEoduksini(true);
		}
		if (GuestStage != EIGGuestStage::Idle && GuestStage != EIGGuestStage::Spent)
		{
			EndGuest(true);
		}
		DarknessSeconds = 0.0f;
		HomeDwellSeconds = 0.0f;
		HideGuestPapers();
		return;
	}

	UpdateGuest(DeltaSeconds);
	UpdatePaperUnderDoor(DeltaSeconds);
	UpdateEoduksini(DeltaSeconds);
}

bool AIGNightThreatDirector::IsCaptureAllowed() const
{
	// 난이도는 위층 사람의 튜닝이 들고 있다. 그가 잡지 않는 밤이면 아무도 잡지 않는다.
	const AIGListenerEntity* Upstairs = Listener.Get();
	return !Upstairs || Upstairs->GetTuning().bCaptureEnabled;
}

bool AIGNightThreatDirector::IsQuietWindow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// §7. 위층 사람이 쫓거나 찾는 동안에는 다른 것이 끼어들지 않는다.
	if (const AIGListenerEntity* Upstairs = Listener.Get())
	{
		const EIGListenerState State = Upstairs->GetListenerState();
		if (State == EIGListenerState::Chasing
			|| State == EIGListenerState::Searching
			|| State == EIGListenerState::Investigating
			|| State == EIGListenerState::CaptureHold)
		{
			return false;
		}
	}
	if (const AIGNightLoopDirector* Loop = NightLoop.Get())
	{
		if (Loop->IsCaptureResetInFlight()
			|| World->GetTimeSeconds() - Loop->GetLastCaptureSeconds() < IGNightThreat::CaptureGraceSeconds)
		{
			return false;
		}
	}
	// 밤4 마지막 대치(가면)는 결말 C가 혼자 맡는다. 거기에 다른 괴이의 침대 리셋이
	// 끼어들면 타임라인 둘이 부딪친다.
	if (const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		Narrative && Narrative->GetNightIndex() == 4 && Narrative->IsNightFourMaskRunning())
	{
		return false;
	}
	for (TActorIterator<AIGMissingFloorFifthDawnDirector> It(World); It; ++It)
	{
		if (It->IsActive())
		{
			return false;
		}
	}
	for (TActorIterator<AIGMissingFloorEpilogueDirector> It(World); It; ++It)
	{
		if (It->IsActive())
		{
			return false;
		}
	}
	return true;
}

FVector AIGNightThreatDirector::GetPlayerEye() const
{
	const AIGPlayerCharacter* Player = PlayerPawn.Get();
	return Player ? Player->GetActorLocation() + Player->GetEyeOffsetFromActor() : FVector::ZeroVector;
}

FVector AIGNightThreatDirector::GetPlayerSightOrigin() const
{
	// 숨어 있으면 눈이 가구 안에 있다. 문짝과 침대 틀이 시선 추적을 막으므로
	// 무엇이 보이는지는 가구 앞면 바로 바깥에서 잰다.
	const AIGPlayerCharacter* Player = PlayerPawn.Get();
	if (Player && Player->IsConcealedInHidingSpot())
	{
		if (const AIGHidingSpot* Spot = Player->GetHidingSpot())
		{
			return Spot->GetPeekLocation();
		}
	}
	return GetPlayerEye();
}

float AIGNightThreatDirector::GetPlayerFeetZ() const
{
	const AIGPlayerCharacter* Player = PlayerPawn.Get();
	if (!Player)
	{
		return 0.0f;
	}
	const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	return Player->GetActorLocation().Z - (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.0f);
}

bool AIGNightThreatDirector::HasLineOfSight(const FVector& From, const FVector& To, const AActor* Ignored) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGNightThreatSight), false, PlayerPawn.Get());
	if (Ignored)
	{
		Params.AddIgnoredActor(Ignored);
	}
	// 숨은 자리의 상호작용 상자도 시선 추적을 막는다. 숨은 사람 자신의 자리는 뺀다.
	if (const AIGPlayerCharacter* Player = PlayerPawn.Get())
	{
		if (const AIGHidingSpot* Spot = Player->GetHidingSpot())
		{
			Params.AddIgnoredActor(Spot);
		}
	}
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
}

bool AIGNightThreatDirector::IsPlayerLookingAt(
	const FVector& Target, const float HalfAngleDegrees, const AActor* Ignored) const
{
	const AIGPlayerCharacter* Player = PlayerPawn.Get();
	if (!Player || !Player->GetController())
	{
		return false;
	}
	const FVector Eye = GetPlayerEye();
	const FVector View = Player->GetControlRotation().Vector();
	const FVector ToTarget = (Target - Eye).GetSafeNormal();
	if (FVector::DotProduct(View, ToTarget) < FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees)))
	{
		return false;
	}
	return HasLineOfSight(GetPlayerSightOrigin(), Target, Ignored);
}

bool AIGNightThreatDirector::IsTorchOn(const FVector& Target) const
{
	const AIGPlayerCharacter* Player = PlayerPawn.Get();
	const UIGFlashlightComponent* Torch = Player ? Player->GetFlashlight() : nullptr;
	if (!Torch || !Torch->IsProvidingLight())
	{
		return false;
	}
	const FVector Eye = GetPlayerEye();
	const FVector ToTarget = (Target - Eye).GetSafeNormal();
	return FVector::DotProduct(Player->GetControlRotation().Vector(), ToTarget)
		>= FMath::Cos(FMath::DegreesToRadians(IGNightThreat::TorchHalfAngle));
}

// ---------------------------------------------------------------------------
// 어둑시니
// ---------------------------------------------------------------------------

bool AIGNightThreatDirector::IsEoduksiniManifested() const
{
	return Eoduksini && Eoduksini->IsManifested();
}

float AIGNightThreatDirector::GetDarknessThresholdSeconds() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const int32 Night = FMath::Clamp(Narrative ? Narrative->GetNightIndex() : 1, 0, 4);
	return IGNightThreat::DarknessSecondsByNight[Night];
}

void AIGNightThreatDirector::UpdateEoduksini(const float DeltaSeconds)
{
	AIGPlayerCharacter* Player = PlayerPawn.Get();
	AIGShadowFigure* Figure = Eoduksini.Get();
	const UWorld* World = GetWorld();
	if (!Player || !Figure || !World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	if (EoduksiniCaptureSeconds >= 0.0)
	{
		// 삼키는 중. 화면이 끊기고 침대로 옮겨질 때까지 그 자리에 둔다. 잡지 않는
		// 난이도에서는 리셋이 없으니 덮쳐 오는 1.4초를 다 보여 주고 흩어진다.
		const AIGNightLoopDirector* Loop = NightLoop.Get();
		const bool bResetSettled = !bEoduksiniHarmlessLunge && Loop && !Loop->IsCaptureResetInFlight();
		if (Now - EoduksiniCaptureSeconds > 1.4 || bResetSettled)
		{
			const bool bWasHarmless = bEoduksiniHarmlessLunge;
			EoduksiniCaptureSeconds = -1.0;
			bEoduksiniHarmlessLunge = false;
			DismissEoduksini(!bWasHarmless);
		}
		return;
	}

	const bool bGuestBusy = GuestStage != EIGGuestStage::Idle && GuestStage != EIGGuestStage::Spent;
	if (!Figure->IsManifested())
	{
		const bool bDark = Player->GetDarkness() >= IGNightThreat::DarkThreshold;
		if (bDark && !bGuestBusy && Now >= EoduksiniCooldownUntil && IsQuietWindow())
		{
			DarknessSeconds += DeltaSeconds;
		}
		else
		{
			DarknessSeconds = FMath::Max(0.0f, DarknessSeconds - DeltaSeconds * 2.0f);
		}
		// 처음 만나기 전에 한 번, 어둠이 길어진다는 것을 몸으로 먼저 느낀다.
		if (!bEoduksiniWarned && DarknessSeconds >= GetDarknessThresholdSeconds() * 0.6f)
		{
			bEoduksiniWarned = true;
			ThinkOnce(
				TEXT("Threat.Eoduksini.Warning"),
				NSLOCTEXT("IGMissingFloor", "YudamDarknessWarning", "너무 어두워. 어둠이 짙어지는 것 같아."));
		}
		if (DarknessSeconds >= GetDarknessThresholdSeconds())
		{
			if (TryManifestEoduksini())
			{
				DarknessSeconds = 0.0f;
			}
			else
			{
				// 설 자리가 없었다. 조금 뒤에 다시 본다.
				DarknessSeconds = GetDarknessThresholdSeconds() * 0.85f;
			}
		}
		return;
	}

	if (!IsQuietWindow() || bGuestBusy)
	{
		DismissEoduksini(false);
		return;
	}
	// 불이 켜진 곳에서는 견디지 못한다. 손전등은 불빛으로 치지 않는다.
	if (Player->GetRoomDarkness() < IGNightThreat::LitThreshold)
	{
		DismissEoduksini(false);
		ThinkOnce(
			TEXT("Threat.Eoduksini.Light"),
			NSLOCTEXT("IGMissingFloor", "YudamEoduksiniLight", "불빛 속으로는 못 들어오나 봐."),
			0.6f);
		return;
	}

	const FVector Chest = Figure->GetChestLocation();
	const bool bLooked = IsPlayerLookingAt(Chest, IGNightThreat::LookHalfAngle, Figure);
	const bool bLit = bLooked && IsTorchOn(Chest);
	if (bLooked)
	{
		EoduksiniUnseenSeconds = 0.0f;
		const float PreviousGrowth = EoduksiniGrowth;
		EoduksiniGrowth = FMath::Min(
			1.0f, EoduksiniGrowth + DeltaSeconds * IGNightThreat::GrowthPerSecond * (bLit ? 2.0f : 1.0f));
		// 쳐다보는 동안 다가온다. 클수록 빠르고, 손전등을 비추면 더 빠르다.
		const FVector Here = Figure->GetActorLocation();
		const FVector PlayerFloor(Player->GetActorLocation().X, Player->GetActorLocation().Y, Here.Z);
		const FVector Toward = (PlayerFloor - Here).GetSafeNormal2D();
		const FVector Stop = PlayerFloor - Toward * IGNightThreat::ApproachStopDistance;
		const float Speed = (IGNightThreat::ApproachBaseSpeed + IGNightThreat::ApproachGrowthSpeed * EoduksiniGrowth)
			* (bLit ? 1.5f : 1.0f);
		Figure->SetTargetLocation(Stop, Speed);
		Figure->SetTrembling(true);
		if (UIGStressComponent* Stress = Player->GetStress())
		{
			if (PreviousGrowth < 0.5f && EoduksiniGrowth >= 0.5f)
			{
				Stress->ApplyScare(0.35f);
			}
			else if (PreviousGrowth < 0.8f && EoduksiniGrowth >= 0.8f)
			{
				Stress->ApplyScare(0.5f);
				Player->PlayScareKick(1.2f);
			}
		}
		if (EoduksiniGrowth >= 0.45f)
		{
			ThinkOnce(
				TEXT("Threat.Eoduksini.Growth"),
				NSLOCTEXT("IGMissingFloor", "YudamEoduksiniGrowth", "쳐다볼수록 커져. 보면 안 돼."));
		}
	}
	else
	{
		EoduksiniUnseenSeconds += DeltaSeconds;
		EoduksiniGrowth = FMath::Max(0.0f, EoduksiniGrowth - DeltaSeconds * IGNightThreat::ShrinkPerSecond);
		Figure->SetTargetLocation(Figure->GetActorLocation(), 0.0f);
		Figure->SetTrembling(false);
		if (EoduksiniUnseenSeconds >= IGNightThreat::UnseenVanishSeconds)
		{
			DismissEoduksini(false);
			ThinkOnce(
				TEXT("Threat.Eoduksini.LookAway"),
				NSLOCTEXT("IGMissingFloor", "YudamEoduksiniLookAway", "눈을 돌렸더니… 없어졌어."),
				0.5f);
			return;
		}
	}
	Figure->SetTargetGrowth(EoduksiniGrowth);
	// 커지는 만큼 화면 가장자리가 먹힌다. 눈을 돌리면 절반으로 풀린다.
	Player->SetThreatVignetteTarget(EoduksiniGrowth * (bLooked ? 1.0f : 0.5f));
	const FVector ToPlayer = Player->GetActorLocation() - Figure->GetActorLocation();
	Figure->SetTargetYaw(ToPlayer.Rotation().Yaw);
	if (EoduksiniBreath)
	{
		EoduksiniBreath->SetVolumeMultiplier(0.12f + 0.75f * EoduksiniGrowth);
		EoduksiniBreath->SetPitchMultiplier(0.85f + 0.25f * EoduksiniGrowth);
	}

	if (EoduksiniGrowth >= IGNightThreat::CaptureGrowth
		&& FVector::Dist2D(Player->GetActorLocation(), Figure->GetActorLocation()) <= IGNightThreat::CaptureDistance)
	{
		// 다 자란 어둠이 덮는다. 몸이 그녀 쪽으로 쏟아지고 화면이 끊긴다.
		EoduksiniCaptureSeconds = Now;
		Figure->SetTargetGrowth(1.0f);
		Figure->SetTargetLocation(
			FVector(Player->GetActorLocation().X, Player->GetActorLocation().Y, Figure->GetActorLocation().Z),
			400.0f);
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateDarknessInhale(this),
			Player->GetActorLocation(),
			1.0f,
			0.8f,
			200.0f,
			1200.0f,
			EIGAudioBus::Entity);
		if (!IsCaptureAllowed())
		{
			// 추격 없음. 덮쳐 오다 흩어진다. 놀람만 남고 침대로 가지 않는다.
			bEoduksiniHarmlessLunge = true;
			if (UIGStressComponent* Stress = Player->GetStress())
			{
				Stress->ApplyScare(0.6f);
			}
			Player->PlayScareKick(1.3f);
		}
		else if (AIGNightLoopDirector* Loop = NightLoop.Get())
		{
			Loop->RequestExternalCapture(Player);
		}
	}
}

bool AIGNightThreatDirector::IsValidManifestSpot(const FVector& Candidate, FVector& OutFloor) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGEoduksiniSpot), false, PlayerPawn.Get());
	if (Eoduksini)
	{
		Params.AddIgnoredActor(Eoduksini);
	}
	// 플레이어가 선 바닥과 같은 층이어야 한다. 발 높이에서 위아래 60 cm 안. 숨어서
	// 눈이 바닥 가까이 있어도 몸이 선 바닥으로 잰다.
	const float FeetZ = GetPlayerFeetZ();
	FHitResult Floor;
	const FVector Top(Candidate.X, Candidate.Y, FeetZ + 180.0f);
	if (!World->LineTraceSingleByChannel(Floor, Top, Top - FVector(0.0f, 0.0f, 420.0f), ECC_Visibility, Params))
	{
		return false;
	}
	if (FMath::Abs(Floor.ImpactPoint.Z - FeetZ) > 60.0f)
	{
		return false;
	}
	OutFloor = Floor.ImpactPoint;
	// 벽이나 가구 속이면 안 된다. 사람 하나 설 자리.
	if (World->OverlapBlockingTestByChannel(
			OutFloor + FVector(0.0f, 0.0f, 100.0f),
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeCapsule(28.0f, 80.0f),
			Params))
	{
		return false;
	}
	// 그 자리가 눈에 보여야 한다. 고개를 돌리면 거기 있다.
	return HasLineOfSight(GetPlayerSightOrigin(), OutFloor + FVector(0.0f, 0.0f, 120.0f), Eoduksini);
}

bool AIGNightThreatDirector::FindManifestSpot(FVector& OutLocation) const
{
	const AIGPlayerCharacter* Player = PlayerPawn.Get();
	if (!Player)
	{
		return false;
	}
	const FVector Eye = GetPlayerEye();
	FVector Floor;
	// 숨어 있으면 틈 앞. 어둠은 숨어 있어도 쌓인다.
	if (const AIGHidingSpot* Spot = Player->IsConcealedInHidingSpot() ? Player->GetHidingSpot() : nullptr)
	{
		const FVector Forward = Spot->GetActorForwardVector().GetSafeNormal2D();
		for (const float Distance : {300.0f, 240.0f, 380.0f})
		{
			if (IsValidManifestSpot(Spot->GetActorLocation() + Forward * Distance, Floor))
			{
				OutLocation = Floor;
				return true;
			}
		}
		return false;
	}
	// 등 뒤와 옆이 먼저다. 숨소리가 들리고, 돌아보면 거기 있다.
	const float ViewYaw = Player->GetControlRotation().Yaw;
	for (const float Distance : {650.0f, 500.0f, 380.0f})
	{
		for (const float Angle : {150.0f, -150.0f, 180.0f, 120.0f, -120.0f, 90.0f, -90.0f, 35.0f, -35.0f})
		{
			const FVector Direction = FRotator(0.0f, ViewYaw + Angle, 0.0f).Vector();
			if (IsValidManifestSpot(Eye + Direction * Distance, Floor))
			{
				OutLocation = Floor;
				return true;
			}
		}
	}
	return false;
}

bool AIGNightThreatDirector::TryManifestEoduksini()
{
	AIGPlayerCharacter* Player = PlayerPawn.Get();
	AIGShadowFigure* Figure = Eoduksini.Get();
	if (!Player || !Figure)
	{
		return false;
	}
	FVector Spot;
	if (!FindManifestSpot(Spot))
	{
		return false;
	}
	EoduksiniGrowth = IGNightThreat::StartGrowth;
	EoduksiniUnseenSeconds = 0.0f;
	const float Yaw = (Player->GetActorLocation() - Spot).Rotation().Yaw;
	Figure->Manifest(Spot, Yaw, EoduksiniGrowth);
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateDarknessInhale(this),
		Spot + FVector(0.0f, 0.0f, 130.0f),
		0.8f,
		1.0f,
		140.0f,
		1600.0f,
		EIGAudioBus::Entity);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "CaptionDarknessInhale", "[어둠 속에서 무언가 숨을 들이쉰다]"),
		2.8f,
		Spot + FVector(0.0f, 0.0f, 130.0f));
	if (EoduksiniBreath)
	{
		EoduksiniBreath->SetVolumeMultiplier(0.12f);
		EoduksiniBreath->Play();
	}
	if (UIGStressComponent* Stress = Player->GetStress())
	{
		Stress->ApplyScare(0.2f);
	}
	ThinkOnce(
		TEXT("Threat.Eoduksini.First"),
		NSLOCTEXT("IGMissingFloor", "YudamEoduksiniFirst", "…저기, 어둠이 뭉쳐 있어."),
		1.6f);
	return true;
}

void AIGNightThreatDirector::DismissEoduksini(const bool bSilently)
{
	if (AIGShadowFigure* Figure = Eoduksini.Get(); Figure && Figure->IsManifested())
	{
		if (!bSilently)
		{
			// 사라질 때 공기가 한 번 빠진다. 숨을 내쉬는 것처럼.
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateClothSettle(this),
				Figure->GetChestLocation(),
				0.35f,
				0.62f,
				120.0f,
				1000.0f,
				EIGAudioBus::Entity);
		}
		Figure->Vanish();
	}
	if (EoduksiniBreath)
	{
		EoduksiniBreath->Stop();
	}
	if (AIGPlayerCharacter* Player = PlayerPawn.Get())
	{
		Player->SetThreatVignetteTarget(0.0f);
	}
	EoduksiniGrowth = 0.0f;
	EoduksiniUnseenSeconds = 0.0f;
	// 덮치던 중에 밤이 끝나도 다음 밤에 그 상태가 남지 않게 한다.
	EoduksiniCaptureSeconds = -1.0;
	bEoduksiniHarmlessLunge = false;
	if (const UWorld* World = GetWorld())
	{
		EoduksiniCooldownUntil = World->GetTimeSeconds() + IGNightThreat::ManifestCooldownSeconds;
	}
}

// ---------------------------------------------------------------------------
// 손님
// ---------------------------------------------------------------------------

bool AIGNightThreatDirector::IsPlayerInHome() const
{
	const AIGPlayerCharacter* Player = PlayerPawn.Get();
	if (!Player)
	{
		return false;
	}
	// 403호 안. 문면 안쪽부터 북쪽 벽까지.
	const FBox Home(
		FVector(-190.0f, AIGPrologueWorldScene::HomeDoorY + 10.0f, AIGPrologueWorldScene::FourthFloorZ - 20.0f),
		FVector(190.0f, 235.0f, AIGPrologueWorldScene::FourthFloorZ + 230.0f));
	return Home.IsInsideOrOn(Player->GetActorLocation());
}

FVector AIGNightThreatDirector::GetDoorOutside() const
{
	// 403호 문짝 가운데의 복도 쪽, 노크하는 손 높이.
	return FVector(
		AIGPrologueWorldScene::HomeDoorX + AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f,
		AIGPrologueWorldScene::HomeDoorY - 8.0f,
		AIGPrologueWorldScene::FourthFloorZ + 140.0f);
}

bool AIGNightThreatDirector::CanStartGuest() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const AIGNightPhaseDirector* Phase = NightPhase.Get();
	const AIGSwingDoor* Door = HomeDoor.Get();
	if (!Narrative || !Phase || !Door)
	{
		return false;
	}
	const int32 Night = Narrative->GetNightIndex();
	if (Night < 2 || Night > 4
		|| Narrative->HasBeatPlayed(FName(*FString::Printf(TEXT("Threat.Guest.N%d"), Night))))
	{
		return false;
	}
	if (Phase->GetHourElapsedSeconds() < IGNightThreat::GuestEarliestHourSeconds
		|| Phase->GetHourElapsedSeconds() > IGNightThreat::GuestLatestHourSeconds)
	{
		return false;
	}
	if (HomeDwellSeconds < IGNightThreat::RequiredHomeDwellSeconds || !Door->IsFullyClosed())
	{
		return false;
	}
	if (const AIGListenerEntity* Upstairs = Listener.Get(); Upstairs && Upstairs->IsAtHomeDoor())
	{
		return false;
	}
	if (Night == 4 && Narrative->IsNightFourMaskRunning())
	{
		return false;
	}
	if (Night == 2)
	{
		// 밤2의 첫 대면(2-1)이 같은 문을 쓴다. 그 비트가 끝난 뒤에만 온다.
		for (TActorIterator<AIGMissingFloorNightTwoBeatDirector> It(GetWorld()); It; ++It)
		{
			if (!It->HasPlayed())
			{
				return false;
			}
		}
	}
	return !IsEoduksiniManifested() && IsQuietWindow();
}

void AIGNightThreatDirector::StartGuest()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	GuestNight = Narrative ? Narrative->GetNightIndex() : 2;
	if (Narrative)
	{
		Narrative->MarkBeatPlayed(FName(*FString::Printf(TEXT("Threat.Guest.N%d"), GuestNight)));
	}
	GuestStage = EIGGuestStage::Knocking;
	GuestElapsed = 0.0f;
	GuestStep = 0;
	GuestRustleSeconds = 0.0f;
	bPaperUnderDoorPlayed = false;
	GuestRustleCount = 0;
	HideGuestPapers();
	SetPeepholeOffered(true);
	PlayerDoorKnockTimes.Reset();
	bGuestAnswered = false;
	bGuestHeardPlayer = false;
	bGuestCapturing = false;
	GuestCaptureSeconds = -1.0;
}

void AIGNightThreatDirector::GuestKnock(const int32 Count, const float Volume)
{
	// 강철 현관문을 손등으로 친다. 위층 사람의 세 번(0.62초)보다 빠르고 고르다.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FTimerHandle KnockTimer;
		GetWorldTimerManager().SetTimer(
			KnockTimer,
			FTimerDelegate::CreateWeakLambda(this, [this, Index, Volume]()
			{
				IGAudio::SpawnOneShotAt(
					this,
					IGAudio::SampleVariantOr(
						TEXT("Knock_Steel"), 3, static_cast<uint32>(Index + 31) * 2654435761u,
						[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.0f); }),
					GetDoorOutside(),
					Volume,
					1.0f,
					160.0f,
					1400.0f,
					EIGAudioBus::World);
			}),
			0.05f + 0.3f * Index,
			false);
	}
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "CaptionGuestKnock", "[현관문을 두드리는 소리]"),
		2.0f,
		GetDoorOutside());
}

void AIGNightThreatDirector::GuestSpeak(const int32 LineIndex)
{
	const FText Speaker = NSLOCTEXT("IGMissingFloor", "GuestSpeaker", "문 밖");
	FText Line;
	switch (GuestNight)
	{
	case 2:
		// 편의점의 나린. 새벽 근무 중이어야 할 사람이다.
		Line = LineIndex == 0
			? NSLOCTEXT("IGMissingFloor", "GuestN2Line1", "언니, 저 편의점 나린이에요. 아까 지갑 두고 가셨어요.")
			: LineIndex == 1
				? NSLOCTEXT("IGMissingFloor", "GuestN2Line2", "언니. 문 좀 열어 주세요. 지갑… 두고 가셨어요.")
				: LineIndex == 2
					? NSLOCTEXT("IGMissingFloor", "GuestN2Line3", "…열어 줘요.")
					: NSLOCTEXT("IGMissingFloor", "GuestN2Line4", "언니?");
		break;
	case 3:
		// 새벽배송은 서명을 받지 않는다. 문 앞에 두고 사진을 찍고 간다.
		Line = LineIndex == 0
			? NSLOCTEXT("IGMissingFloor", "GuestN3Line1", "새벽배송입니다. 서명을 받아야 해서요, 잠깐만 열어 주세요.")
			: LineIndex == 1
				? NSLOCTEXT("IGMissingFloor", "GuestN3Line2", "고객님. 문 열어 주세요. 서명만 받으면 돼요.")
				: LineIndex == 2
					? NSLOCTEXT("IGMissingFloor", "GuestN3Line3", "…열어.")
					: NSLOCTEXT("IGMissingFloor", "GuestN3Line4", "고객님?");
		break;
	default:
		// 오빠의 목소리. 오빠는 늘 둘, 하나로 두드렸다.
		Line = LineIndex == 0
			? NSLOCTEXT("IGMissingFloor", "GuestN4Line1", "유담아, 나야. 문 좀 열어 봐.")
			: LineIndex == 1
				? NSLOCTEXT("IGMissingFloor", "GuestN4Line2", "유담아. 오빠 왔어. 춥다, 문 열어.")
				: LineIndex == 2
					? NSLOCTEXT("IGMissingFloor", "GuestN4Line3", "…유담아.")
					: NSLOCTEXT("IGMissingFloor", "GuestN4Line4", "유담아, 어디 있어.");
		break;
	}
	AIGHorrorHUD::PushDialogue(this, Speaker, Line, EIGDialogueChannel::Conversation, 3.2f, EIGDialoguePriority::Story);
	// 말은 자막이 맡고, 귀에는 철문 너머의 웅얼거림만 온다. 들어온 뒤에는 방 안에서.
	const bool bInside = GuestStage == EIGGuestStage::Inside && Guest;
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateDoorMurmur(this),
		bInside ? Guest->GetChestLocation() + FVector(0.0f, 0.0f, 40.0f) : GetDoorOutside() + FVector(0.0f, 0.0f, 20.0f),
		bInside ? 0.45f : 0.6f,
		IGNightThreat::MurmurPitchByNight[FMath::Clamp(GuestNight, 0, 4)],
		140.0f,
		1100.0f,
		EIGAudioBus::World);
}

void AIGNightThreatDirector::SetPeepholeOffered(const bool bOffered)
{
	// 밤2 첫 대면의 문구멍을 빌려 쓴다. 그 비트의 문구멍 장면이 이미 지났을 때만이다.
	// 아니면 그쪽 처리기가 밤2 장면을 다시 튼다.
	if (!bOffered)
	{
		if (AIGMissingFloorEvidence* Hole = Peephole.Get())
		{
			Hole->OnExamined.Remove(PeepholeHandle);
			Hole->SetInteractionEnabled(false);
		}
		Peephole.Reset();
		PeepholeHandle.Reset();
		return;
	}
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->HasBeatPlayed(FName(TEXT("Night2.Peephole"))))
	{
		return;
	}
	for (TActorIterator<AIGMissingFloorNightTwoBeatDirector> It(GetWorld()); It; ++It)
	{
		if (AIGMissingFloorEvidence* Hole = It->GetPeephole())
		{
			Peephole = Hole;
			PeepholeHandle = Hole->OnExamined.AddUObject(this, &AIGNightThreatDirector::HandlePeepholeExamined);
			Hole->SetInteractionEnabled(true);
		}
		break;
	}
}

void AIGNightThreatDirector::HandlePeepholeExamined(AIGMissingFloorEvidence* Evidence)
{
	if (GuestStage != EIGGuestStage::Knocking && GuestStage != EIGGuestStage::Keypad)
	{
		return;
	}
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT("IGMissingFloor", "YudamGuestPeephole", "복도에 아무도 없어. …목소리는 문 바로 앞에서 났는데."),
		3.4f);
	if (AIGPlayerCharacter* Player = PlayerPawn.Get())
	{
		if (UIGStressComponent* Stress = Player->GetStress())
		{
			Stress->ApplyScare(0.3f);
		}
	}
}

namespace IGNightThreat
{
	/** 종이가 지나가는 문 아래 자리. X는 문 가운데에서 조금 비켜 있다. */
	static FVector GetPaperDoorBottom()
	{
		return FVector(
			AIGPrologueWorldScene::HomeDoorX + AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f + PaperOffsetX,
			AIGPrologueWorldScene::HomeDoorY + DoorHalfThickness,
			AIGPrologueWorldScene::FourthFloorZ);
	}
}

void AIGNightThreatDirector::BeginPaperUnderDoor()
{
	bPaperUnderDoorPlayed = true;
	UWorld* World = GetWorld();
	if (!PaperUnderDoor || !World)
	{
		return;
	}
	using namespace IGNightThreat;
	const FVector DoorBottom = GetPaperDoorBottom();
	// 현관 바닥을 직접 잰다. 문턱이나 바닥 타일이 바뀌어도 종이가 뜨거나 묻히지 않는다.
	PaperUnderDoorFloorZ = DoorBottom.Z;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGGuestPaperFloor), false, PlayerPawn.Get());
	if (Guest)
	{
		Params.AddIgnoredActor(Guest);
	}
	const FVector Probe(
		DoorBottom.X,
		DoorBottom.Y + PaperShoveReach[UE_ARRAY_COUNT(PaperShoveReach) - 1] - PaperLengthCm * 0.5f,
		DoorBottom.Z + 40.0f);
	FHitResult Floor;
	if (World->LineTraceSingleByChannel(Floor, Probe, Probe - FVector(0.0f, 0.0f, 80.0f), ECC_Visibility, Params))
	{
		PaperUnderDoorFloorZ = Floor.ImpactPoint.Z;
	}
	PaperUnderDoorElapsed = 0.0f;
	PaperUnderDoorShoves = 0;
	// 전단 메시는 인쇄면이 -Y, 글자 위쪽이 +Z다. 옆으로 90도 눕혀 인쇄면을 위로 하고,
	// 반 바퀴 돌려 글자 위쪽이 문을 향하게 한다. 방 안에서 내려다보면 바로 읽힌다.
	PaperUnderDoor->SetWorldRotation(bPaperUnderDoorIsCube
		? FRotator(0.0f, PaperYawDegrees, 0.0f)
		: FRotator(0.0f, 180.0f + PaperYawDegrees, 90.0f));
	PaperUnderDoor->SetVisibility(true);
	UpdatePaperUnderDoor(0.0f);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "CaptionGuestPaper", "[문 밑으로 종이를 밀어 넣는 소리]"),
		2.2f,
		DoorBottom + FVector(0.0f, 0.0f, 10.0f));
}

void AIGNightThreatDirector::UpdatePaperUnderDoor(const float DeltaSeconds)
{
	if (!PaperUnderDoor || PaperUnderDoorElapsed < 0.0f)
	{
		return;
	}
	using namespace IGNightThreat;
	PaperUnderDoorElapsed += DeltaSeconds;
	const FVector DoorBottom = GetPaperDoorBottom();
	// 한 번 밀 때마다 0.2초 들어오고 멎는다. 손가락으로 밀어 넣는 박자다.
	float Reach = PaperStartReach;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(PaperShoveTimes); ++Index)
	{
		const float Since = PaperUnderDoorElapsed - PaperShoveTimes[Index];
		if (Since < 0.0f)
		{
			break;
		}
		if (Index >= PaperUnderDoorShoves)
		{
			PaperUnderDoorShoves = Index + 1;
			// 미는 손마다 종이가 문턱에 쓸린다.
			IGAudio::SpawnOneShotAt(
				this,
				IGAudio::SampleVariantOr(
					TEXT("Paper_Turn"), 2, static_cast<uint32>(GuestNight * 31 + Index),
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreatePickupRustle(this); }),
				DoorBottom + FVector(0.0f, 0.0f, 4.0f),
				0.5f,
				FMath::FRandRange(0.9f, 1.08f),
				80.0f,
				800.0f,
				EIGAudioBus::World);
		}
		const float From = Index == 0 ? PaperStartReach : PaperShoveReach[Index - 1];
		Reach = FMath::Lerp(From, PaperShoveReach[Index], FMath::Clamp(Since / PaperShoveSeconds, 0.0f, 1.0f));
	}
	// 종이 가운데는 앞 가장자리에서 길이의 절반 뒤다. 바닥에서 두께의 절반보다 조금 띄운다.
	PaperUnderDoor->SetWorldLocation(FVector(
		DoorBottom.X,
		DoorBottom.Y + Reach - PaperLengthCm * 0.5f,
		PaperUnderDoorFloorZ + (bPaperUnderDoorIsCube ? 0.12f : 0.06f)));
	const int32 LastShove = static_cast<int32>(UE_ARRAY_COUNT(PaperShoveTimes)) - 1;
	if (PaperUnderDoorElapsed >= PaperShoveTimes[LastShove] + PaperShoveSeconds)
	{
		PaperUnderDoorElapsed = -1.0f;
	}
}

void AIGNightThreatDirector::DropGuestPaper(const FVector& BodyLocation)
{
	UWorld* World = GetWorld();
	if (!World || !PaperUnderDoor || FallenPaperCount >= IGNightThreat::MaxFallenPapers)
	{
		return;
	}
	UStaticMeshComponent* Sheet = FallenPapers.IsValidIndex(FallenPaperCount)
		? FallenPapers[FallenPaperCount].Get()
		: nullptr;
	if (!Sheet)
	{
		// 문 밑으로 밀어 넣은 것과 같은 종이를 쓴다.
		Sheet = NewObject<UStaticMeshComponent>(
			this, *FString::Printf(TEXT("GuestFallenPaper_%d"), FallenPaperCount));
		Sheet->SetMobility(EComponentMobility::Movable);
		Sheet->SetStaticMesh(PaperUnderDoor->GetStaticMesh());
		if (bPaperUnderDoorIsCube)
		{
			Sheet->SetMaterial(0, PaperUnderDoor->GetMaterial(0));
			Sheet->SetRelativeScale3D(PaperUnderDoor->GetRelativeScale3D());
		}
		Sheet->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Sheet->SetGenerateOverlapEvents(false);
		Sheet->SetCanEverAffectNavigation(false);
		Sheet->SetCastShadow(false);
		Sheet->RegisterComponent();
		if (FallenPapers.IsValidIndex(FallenPaperCount))
		{
			FallenPapers[FallenPaperCount] = Sheet;
		}
		else
		{
			FallenPapers.Add(Sheet);
		}
	}
	++FallenPaperCount;
	// 몸 언저리에 아무렇게나 떨어진다. 침대나 상 위에 걸리면 거기 얹힌다.
	const FVector Around = BodyLocation + FVector(
		FMath::FRandRange(-IGNightThreat::FallenPaperSpread, IGNightThreat::FallenPaperSpread),
		FMath::FRandRange(-IGNightThreat::FallenPaperSpread, IGNightThreat::FallenPaperSpread),
		0.0f);
	float RestZ = BodyLocation.Z;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGGuestFallenPaper), false, PlayerPawn.Get());
	if (Guest)
	{
		Params.AddIgnoredActor(Guest);
	}
	FHitResult Rest;
	const FVector Top = Around + FVector(0.0f, 0.0f, 90.0f);
	if (World->LineTraceSingleByChannel(Rest, Top, Top - FVector(0.0f, 0.0f, 140.0f), ECC_Visibility, Params))
	{
		RestZ = Rest.ImpactPoint.Z;
	}
	const float Yaw = FMath::FRandRange(0.0f, 360.0f);
	Sheet->SetWorldLocationAndRotation(
		FVector(Around.X, Around.Y, RestZ + (bPaperUnderDoorIsCube ? 0.12f : 0.06f)),
		bPaperUnderDoorIsCube ? FRotator(0.0f, Yaw, 0.0f) : FRotator(0.0f, Yaw, 90.0f));
	Sheet->SetVisibility(true);
}

void AIGNightThreatDirector::HideGuestPapers()
{
	PaperUnderDoorElapsed = -1.0f;
	if (PaperUnderDoor && PaperUnderDoor->IsVisible())
	{
		PaperUnderDoor->SetVisibility(false);
	}
	if (FallenPaperCount > 0)
	{
		for (UStaticMeshComponent* Sheet : FallenPapers)
		{
			if (Sheet)
			{
				Sheet->SetVisibility(false);
			}
		}
		FallenPaperCount = 0;
	}
}

void AIGNightThreatDirector::RegisterPlayerDoorKnock(const AActor* KnockedActor)
{
	const UWorld* World = GetWorld();
	if (!World || GuestStage != EIGGuestStage::Knocking || KnockedActor != HomeDoor.Get())
	{
		return;
	}
	// 오빠의 노크: 둘(0.18~0.65초 간격), 쉬고(0.68~1.8초), 하나. 위층 사람이 알아듣는
	// 박자와 같다.
	const double Now = World->GetTimeSeconds();
	PlayerDoorKnockTimes.Add(Now);
	while (PlayerDoorKnockTimes.Num() > 3)
	{
		PlayerDoorKnockTimes.RemoveAt(0);
	}
	if (PlayerDoorKnockTimes.Num() < 3)
	{
		return;
	}
	const double Pair = PlayerDoorKnockTimes[1] - PlayerDoorKnockTimes[0];
	const double Rest = PlayerDoorKnockTimes[2] - PlayerDoorKnockTimes[1];
	if (Pair >= AIGListenerEntity::AnswerPairMinSeconds && Pair <= AIGListenerEntity::AnswerPairMaxSeconds
		&& Rest >= AIGListenerEntity::AnswerRestMinSeconds && Rest <= AIGListenerEntity::AnswerRestMaxSeconds)
	{
		bGuestAnswered = true;
	}
}

void AIGNightThreatDirector::OpenDoorForGuest()
{
	AIGSwingDoor* Door = HomeDoor.Get();
	AIGPlayerCharacter* Player = PlayerPawn.Get();
	if (!Door || !Guest || !Player)
	{
		EndGuest(false);
		return;
	}
	// 소리 없이 연다. 문 소리가 버스에 나가면 위층 사람이 그리로 온다.
	Door->BeginScriptedSwingWithLoudness(true, 0.0f, 1.3f);
	Guest->Manifest(
		FVector(
			AIGPrologueWorldScene::HomeDoorX + AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f,
			AIGPrologueWorldScene::HomeDoorY - 30.0f,
			AIGPrologueWorldScene::FourthFloorZ),
		90.0f,
		IGNightThreat::GuestGrowth);
	GuestStage = EIGGuestStage::Inside;
	GuestElapsed = 0.0f;
	bPlayerHiddenWhenOpened = Player->IsConcealedInHidingSpot();
	if (UIGStressComponent* Stress = Player->GetStress())
	{
		Stress->ApplyScare(0.6f);
	}
}

void AIGNightThreatDirector::BeginGuestCapture()
{
	AIGPlayerCharacter* Player = PlayerPawn.Get();
	const UWorld* World = GetWorld();
	if (bGuestCapturing || !Player || !World)
	{
		return;
	}
	bGuestCapturing = true;
	GuestCaptureSeconds = World->GetTimeSeconds();
	if (!IsCaptureAllowed())
	{
		// 추격 없음. 코앞까지 와서 1.4초 서 있다가 흩어진다. 침대로 가지 않는다.
		if (UIGStressComponent* Stress = Player->GetStress())
		{
			Stress->ApplyScare(0.6f);
		}
		Player->PlayScareKick(1.3f);
		return;
	}
	if (AIGNightLoopDirector* Loop = NightLoop.Get())
	{
		Loop->RequestExternalCapture(Player);
	}
}

void AIGNightThreatDirector::EndGuest(const bool bCloseDoor)
{
	if (Guest && Guest->IsManifested())
	{
		Guest->Vanish();
	}
	if (AIGSwingDoor* Door = HomeDoor.Get(); bCloseDoor && Door && Door->IsOpen())
	{
		Door->BeginScriptedSwingWithLoudness(false, 0.0f, 1.3f);
	}
	GuestStage = EIGGuestStage::Spent;
	bGuestCapturing = false;
	GuestCaptureSeconds = -1.0;
	SetPeepholeOffered(false);
}

void AIGNightThreatDirector::UpdateGuest(const float DeltaSeconds)
{
	AIGPlayerCharacter* Player = PlayerPawn.Get();
	AIGSwingDoor* Door = HomeDoor.Get();
	const UWorld* World = GetWorld();
	if (!Player || !Door || !World)
	{
		return;
	}
	HomeDwellSeconds = IsPlayerInHome() ? HomeDwellSeconds + DeltaSeconds : 0.0f;

	if (GuestStage == EIGGuestStage::Idle || GuestStage == EIGGuestStage::Spent)
	{
		// 밤이 바뀌면 다시 올 수 있다. 한 밤에 한 번은 서사 기록이 막는다.
		if (GuestStage == EIGGuestStage::Spent)
		{
			const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
			if (Narrative && Narrative->GetNightIndex() != GuestNight)
			{
				GuestStage = EIGGuestStage::Idle;
			}
		}
		if (GuestStage == EIGGuestStage::Idle && CanStartGuest())
		{
			StartGuest();
		}
		return;
	}

	if (bGuestCapturing)
	{
		if (World->GetTimeSeconds() - GuestCaptureSeconds > 1.4)
		{
			EndGuest(true);
		}
		return;
	}
	// 위층 사람이 끼어들었거나 그녀가 잡혀 침대로 갔다. 물러간다.
	if (!IsQuietWindow())
	{
		EndGuest(true);
		return;
	}

	GuestElapsed += DeltaSeconds;
	// 대답하지 않으면 두 번째 말 뒤에 문 밑으로 종이가 들어온다.
	if (GuestStage == EIGGuestStage::Knocking && !bGuestAnswered && !bPaperUnderDoorPlayed
		&& GuestElapsed >= IGNightThreat::PaperSlideAt)
	{
		BeginPaperUnderDoor();
	}
	using namespace IGNightThreat;
	switch (GuestStage)
	{
	case EIGGuestStage::Knocking:
	case EIGGuestStage::Keypad:
	{
		// 두드리는 동안 문을 열면 바로 거기 서 있다.
		if (Door->IsOpen() && Guest)
		{
			Guest->Manifest(
				FVector(AIGPrologueWorldScene::HomeDoorX + AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f,
					AIGPrologueWorldScene::HomeDoorY - 30.0f,
					AIGPrologueWorldScene::FourthFloorZ),
				90.0f,
				GuestGrowth);
			IGAudio::SpawnOneShotAt(
				this,
				IGAudio::SampleOr(TEXT("Stinger_CloseCall"),
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateCloseCallStinger(this); }),
				GetDoorOutside(),
				0.9f,
				1.0f,
				200.0f,
				2000.0f,
				EIGAudioBus::Entity);
			BeginGuestCapture();
			return;
		}
		if (GuestStage == EIGGuestStage::Knocking && bGuestAnswered)
		{
			// 오빠의 노크로 대답했다. 두 번만 따라 치고 조용해진다. 마지막 하나는 모른다.
			GuestKnock(2, 0.7f);
			ThinkOnce(
				*FString::Printf(TEXT("Threat.Guest.Answered.N%d"), GuestNight),
				NSLOCTEXT("IGMissingFloor", "YudamGuestAnswered", "두 번만 두드리고… 조용해졌어."),
				2.4f);
			EndGuest(false);
			return;
		}
		if (GuestStep == 0 && GuestElapsed >= KnockOneAt)
		{
			++GuestStep;
			GuestKnock(3, 0.6f);
		}
		else if (GuestStep == 1 && GuestElapsed >= LineOneAt)
		{
			++GuestStep;
			GuestSpeak(0);
			// 진짜 사람은 아니라는 표시. 밤4에는 오빠의 노크로, 그 밖에는 문 밑의 빛으로 안다.
			// 밤에는 계단 쪽 등 하나만 살아 있고 그 빛이 403호 문 앞까지 닿는다. 사람이 서
			// 있으면 그 빛이 발에 끊긴다.
			if (GuestNight == 4)
			{
				ThinkOnce(
					TEXT("Threat.Guest.Tell.N4"),
					NSLOCTEXT("IGMissingFloor", "YudamGuestTellBrother", "…오빠는 늘 둘, 하나로 두드렸는데."),
					3.4f);
			}
			else if (FVector::Dist2D(Player->GetActorLocation(), GetDoorOutside()) < 260.0f)
			{
				AIGHorrorHUD::PushThought(
					this,
					NSLOCTEXT("IGMissingFloor", "YudamGuestTellFeet", "문 밑으로 새는 불빛에 발 그림자가 없어."),
					3.2f);
			}
		}
		else if (GuestStep == 2 && GuestElapsed >= KnockTwoAt)
		{
			++GuestStep;
			GuestKnock(3, 0.85f);
		}
		else if (GuestStep == 3 && GuestElapsed >= LineTwoAt)
		{
			++GuestStep;
			GuestSpeak(1);
		}
		else if (GuestStep == 4 && GuestElapsed >= KeypadAt)
		{
			++GuestStep;
			GuestStage = EIGGuestStage::Keypad;
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateDoorlockCode(this, false),
				GetDoorOutside() + FVector(0.0f, 0.0f, -20.0f),
				0.75f,
				1.0f,
				140.0f,
				1300.0f,
				EIGAudioBus::World);
			AIGHorrorHUD::PushAudioCaptionAt(
				this,
				NSLOCTEXT("IGMissingFloor", "CaptionGuestKeypad", "[밖에서 도어락 번호를 누른다]"),
				2.0f,
				GetDoorOutside());
			if (UIGStressComponent* Stress = Player->GetStress())
			{
				Stress->ApplyScare(0.4f);
			}
		}
		else if (GuestStep == 5 && GuestElapsed >= WrongCodeAt)
		{
			++GuestStep;
			AIGHorrorHUD::PushAudioCaptionAt(
				this,
				NSLOCTEXT("IGMissingFloor", "CaptionGuestWrongCode", "[번호가 틀렸다는 경고음]"),
				1.8f,
				GetDoorOutside());
		}
		else if (GuestStep == 6 && GuestElapsed >= KeypadAgainAt)
		{
			++GuestStep;
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateDoorlockCode(this, true),
				GetDoorOutside() + FVector(0.0f, 0.0f, -20.0f),
				0.75f,
				1.0f,
				140.0f,
				1300.0f,
				EIGAudioBus::World);
			AIGHorrorHUD::PushAudioCaptionAt(
				this,
				NSLOCTEXT("IGMissingFloor", "CaptionGuestKeypadAgain", "[도어락 번호를 다시 누른다]"),
				1.8f,
				GetDoorOutside());
		}
		else if (GuestStep == 7 && GuestElapsed >= UnlockedAt)
		{
			++GuestStep;
			AIGHorrorHUD::PushAudioCaptionAt(
				this,
				NSLOCTEXT("IGMissingFloor", "CaptionGuestUnlocked", "[도어락이 풀린다]"),
				1.6f,
				GetDoorOutside());
		}
		else if (GuestStep == 8 && GuestElapsed >= DoorAt)
		{
			++GuestStep;
			if (Door->IsLatched())
			{
				// 걸쇠가 문을 한 뼘에서 잡는다. 두 번 덜컥거린다.
				GuestStage = EIGGuestStage::CaughtOnLatch;
				GuestElapsed = 0.0f;
				for (int32 Rattle = 0; Rattle < 2; ++Rattle)
				{
					FTimerHandle RattleTimer;
					GetWorldTimerManager().SetTimer(
						RattleTimer,
						FTimerDelegate::CreateWeakLambda(this, [this]()
						{
							IGAudio::SpawnOneShotAt(
								this,
								IGAudio::SampleOr(TEXT("Lock_Rattle"),
									[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateLockedRattle(this); }),
								GetDoorOutside(),
								0.9f,
								FMath::FRandRange(0.94f, 1.04f),
								160.0f,
								1400.0f,
								EIGAudioBus::World);
						}),
						0.05f + 0.55f * Rattle,
						false);
				}
				AIGHorrorHUD::PushAudioCaptionAt(
					this,
					NSLOCTEXT("IGMissingFloor", "CaptionGuestLatch", "[걸쇠에 문이 걸려 덜컥거린다]"),
					2.2f,
					GetDoorOutside());
				if (UIGStressComponent* Stress = Player->GetStress())
				{
					Stress->ApplyScare(0.45f);
				}
				Player->PlayScareKick(1.6f);
			}
			else
			{
				OpenDoorForGuest();
			}
		}
		break;
	}

	case EIGGuestStage::CaughtOnLatch:
		if (GuestStep == 9 && GuestElapsed >= LatchLineDelay)
		{
			++GuestStep;
			GuestSpeak(2);
		}
		else if (GuestStep == 10 && GuestElapsed >= LatchLeaveDelay)
		{
			ThinkOnce(
				*FString::Printf(TEXT("Threat.Guest.Left.N%d"), GuestNight),
				NSLOCTEXT("IGMissingFloor", "YudamGuestNoSteps", "…가는 발소리가 없었어."),
				1.4f);
			EndGuest(false);
		}
		break;

	case EIGGuestStage::Inside:
	{
		AIGShadowFigure* Body = Guest.Get();
		if (!Body)
		{
			EndGuest(true);
			return;
		}
		const FVector BodyLocation = Body->GetActorLocation();
		// 움직이는 동안 몸의 종이가 스친다. 발소리는 없다.
		GuestRustleSeconds += DeltaSeconds;
		if (GuestRustleSeconds >= RustleSeconds
			&& FVector::DistSquared2D(BodyLocation, LastGuestLocation) > FMath::Square(4.0f))
		{
			GuestRustleSeconds = 0.0f;
			IGAudio::SpawnOneShotAt(
				this,
				IGAudio::SampleVariantOr(
					TEXT("Paper_Turn"), 2, static_cast<uint32>(GuestElapsed * 977.0f),
					[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreatePickupRustle(this); }),
				BodyLocation + FVector(0.0f, 0.0f, 110.0f),
				0.35f,
				FMath::FRandRange(0.82f, 0.96f),
				120.0f,
				900.0f,
				EIGAudioBus::World);
			// 세 번 스칠 때마다 한 장이 떨어진다. 지나간 길에 종이가 남는다.
			if (++GuestRustleCount % IGNightThreat::RustlesPerFallenPaper == 0)
			{
				DropGuestPaper(BodyLocation);
			}
		}
		LastGuestLocation = BodyLocation;
		// 계단으로 달아나 다른 층에 있으면 거기까지 오지 않는다. 몸이 바닥을 뚫고
		// 따라오거나 위층에서 손을 뻗지 않게 한다.
		if (FMath::Abs(GetPlayerFeetZ() - BodyLocation.Z) > SameFloorTolerance)
		{
			EndGuest(true);
			return;
		}
		const FVector PlayerFloor(Player->GetActorLocation().X, Player->GetActorLocation().Y, BodyLocation.Z);
		const float Distance = FVector::Dist2D(PlayerFloor, BodyLocation);
		Body->SetTargetYaw((PlayerFloor - BodyLocation).Rotation().Yaw);
		const bool bHidden = Player->IsConcealedInHidingSpot();
		// 숨는 것을 봤으면 그 자리로 간다. 못 봤으면 소리로만 찾는다.
		const bool bSawHide = bHidden && !bPlayerHiddenWhenOpened && Distance < 400.0f;
		if (!bHidden || bSawHide || bGuestHeardPlayer)
		{
			Body->SetTargetLocation(PlayerFloor, PursueSpeed);
			if (Distance <= GuestReach)
			{
				BeginGuestCapture();
				return;
			}
			if (!bHidden && Distance > GiveUpDistance)
			{
				EndGuest(true);
			}
			break;
		}
		// 숨은 사람을 못 봤다. 방 가운데로 천천히 들어와 서서 듣다가 돌아간다.
		const FVector RoomCenter(-10.0f, 20.0f, BodyLocation.Z);
		const FVector Doorway(
			AIGPrologueWorldScene::HomeDoorX + AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f,
			AIGPrologueWorldScene::HomeDoorY - 30.0f,
			BodyLocation.Z);
		const float WalkIn = FVector::Dist2D(Doorway, RoomCenter) / SearchSpeed;
		if (GuestElapsed < WalkIn)
		{
			Body->SetTargetLocation(RoomCenter, SearchSpeed);
		}
		else if (GuestElapsed < WalkIn + SearchHoldSeconds)
		{
			if (GuestStep == 9)
			{
				++GuestStep;
				GuestSpeak(3);
			}
		}
		else if (GuestElapsed < WalkIn * 2.0f + SearchHoldSeconds)
		{
			Body->SetTargetLocation(Doorway, SearchSpeed);
		}
		else
		{
			ThinkOnce(
				*FString::Printf(TEXT("Threat.Guest.Searched.N%d"), GuestNight),
				NSLOCTEXT("IGMissingFloor", "YudamGuestGone", "나갔어. 문이… 저절로 닫혔어."),
				1.0f);
			EndGuest(true);
		}
		break;
	}

	default:
		break;
	}
}

void AIGNightThreatDirector::HandleNoise(const FIGNoiseEvent& Event)
{
	// 들어와 찾는 동안, 숨은 자리 근처의 소리는 들린다.
	if (GuestStage != EIGGuestStage::Inside || !Guest || Event.Instigator.Get() != PlayerPawn.Get())
	{
		return;
	}
	// 소리가 실제로 닿는 거리 안에서만 듣는다. 숨은 자리 안의 소리는 이미 줄어든
	// 반경으로 온다. 다른 층의 소리는 바닥에 막힌다(소리는 대개 바닥 90 cm 위에서 난다).
	const FVector GuestAt = Guest->GetActorLocation();
	const bool bSameFloor = FMath::Abs(Event.Location.Z - 90.0f - GuestAt.Z) <= IGNightThreat::SameFloorTolerance;
	if (bSameFloor
		&& FVector::Dist2D(Event.Location, GuestAt) <= FMath::Min(IGNightThreat::GuestHearing, Event.Radius))
	{
		bGuestHeardPlayer = true;
	}
}
