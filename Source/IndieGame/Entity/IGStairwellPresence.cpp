#include "Entity/IGStairwellPresence.h"

#include "Animation/AnimSequence.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorNightTwoBeatDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "GameFramework/PlayerController.h"
#include "IndieGame.h"
#include "Interaction/IGReadableNote.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Save/IGSaveSubsystem.h"
#include "TimerManager.h"

const FName AIGStairwellPresence::StartedBeat(TEXT("Night2.StairFollowerStarted"));
const FName AIGStairwellPresence::HeardBeat(TEXT("Witness.StairExtraStep"));
const FName AIGStairwellPresence::SeenBeat(TEXT("Witness.StairFoldedFigure"));
const FName AIGStairwellPresence::DayPrintsBeat(TEXT("Day.StairWetPrints"));

namespace IGStairwell
{
	/** 그녀보다 이만큼 뒤에서 걷는다. 한 띠와 꺾이는 참 하나다(cm, 길을 따라). */
	constexpr float FollowLag = 330.0f;
	/** 그녀가 이만큼 걸을 때마다 한 걸음. 철판 계단의 보폭이다. */
	constexpr float StrideLength = 75.0f;
	constexpr float StepLatency = 0.38f;
	/** 멈춘 뒤 이만큼 지나 한 발이 더 난다. */
	constexpr float ExtraStepLatency = 0.55f;
	constexpr float QuietToExtra = 0.45f;
	constexpr int32 MaxExtraSteps = 3;
	/** 보이지 않는 동안 따라붙는 빠르기. 걷는 사람보다 조금 느리다(cm/s). */
	constexpr float FollowSpeed = 210.0f;
	constexpr float SceneLimitSeconds = 24.0f;
	constexpr float TwitchAfterSeconds = 1.0f;
	constexpr float LiftAfterSeconds = 2.4f;
	/** 발이 이 높이 안에 있는 띠만 같은 자리로 본다. 위아래 띠는 같은 XY에 겹친다. */
	constexpr float RouteHeightTolerance = 45.0f;
	constexpr float RouteWidthTolerance = 85.0f;
}

AIGStairwellPresence::AIGStairwellPresence()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Figure = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FoldedFigure"));
	Figure->SetupAttachment(GetRootComponent());
	Figure->SetUsingAbsoluteLocation(true);
	Figure->SetUsingAbsoluteRotation(true);
	Figure->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Figure->SetCanEverAffectNavigation(false);
	Figure->SetHiddenInGame(true);
	Figure->SetCastShadow(true);
	Figure->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
}

UIGMissingFloorNarrativeSubsystem* AIGStairwellPresence::GetNarrative() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
}

AIGPlayerCharacter* AIGStairwellPresence::GetPlayer() const
{
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return Controller ? Cast<AIGPlayerCharacter>(Controller->GetPawn()) : nullptr;
}

void AIGStairwellPresence::BuildDescentRoute(TArray<FVector>& OutRoute)
{
	OutRoute.Reset();
	OutRoute.Add(AIGPrologueWorldScene::GetStairDoorwayFeet(3));
	for (int32 Floor = 2; Floor >= 0; --Floor)
	{
		// 한 층의 길은 [아래층 출입구, …, 위층 출입구]다. 양 끝 출입구를 빼고 거꾸로 잇는다.
		TArray<FVector> Climb;
		AIGPrologueWorldScene::GetStairClimbFeet(Floor, Climb);
		for (int32 Index = Climb.Num() - 2; Index >= 1; --Index)
		{
			OutRoute.Add(Climb[Index]);
		}
	}
	OutRoute.Add(AIGPrologueWorldScene::GetStairDoorwayFeet(0));
}

void AIGStairwellPresence::Configure(const TArray<FVector>& InRoute)
{
	if (InRoute.Num() < 2)
	{
		return;
	}
	Route = InRoute;
	RouteDistance.Reset();
	float Total = 0.0f;
	RouteDistance.Add(0.0f);
	for (int32 Index = 1; Index < Route.Num(); ++Index)
	{
		Total += FVector::Dist(Route[Index - 1], Route[Index]);
		RouteDistance.Add(Total);
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Meshes/SK_StairFoldedFigure.SK_StairFoldedFigure"));
	IdleAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Meshes/A_StairFoldedFigure_Idle.A_StairFoldedFigure_Idle"));
	TwitchAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Meshes/A_StairFoldedFigure_Twitch.A_StairFoldedFigure_Twitch"));
	LiftAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Meshes/A_StairFoldedFigure_Lift.A_StairFoldedFigure_Lift"));
	if (!Mesh)
	{
		return;
	}
	Figure->SetSkeletalMeshAsset(Mesh);
	if (UMaterialInterface* PrintBase = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Prototype/Materials/M_StairWetBootprints.M_StairWetBootprints")))
	{
		PrintMaterial = UMaterialInstanceDynamic::Create(PrintBase, this);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UDecalComponent* Decal = NewObject<UDecalComponent>(this, *FString::Printf(TEXT("WetBootprints_%d"), Index));
			Decal->SetupAttachment(GetRootComponent());
			Decal->SetUsingAbsoluteLocation(true);
			Decal->SetUsingAbsoluteRotation(true);
			Decal->SetDecalMaterial(PrintMaterial);
			// 한 켤레가 40 cm 네모에 들어간다. 디딤판 윗면 위아래 3 cm만 투영한다.
			Decal->DecalSize = FVector(3.0f, 20.0f, 20.0f);
			Decal->SetFadeScreenSize(0.002f);
			Decal->SetVisibility(false);
			Decal->RegisterComponent();
			Prints.Add(Decal);
		}
	}
	bConfigured = true;
}

void AIGStairwellPresence::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UIGSaveSubsystem* Save = GameInstance->GetSubsystem<UIGSaveSubsystem>())
		{
			Save->OnLoadCompleted.AddUniqueDynamic(this, &ThisClass::HandleLoadCompleted);
		}
	}
	GetWorldTimerManager().SetTimer(PollTimer, this, &ThisClass::Poll, 0.2f, true);
}

void AIGStairwellPresence::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PollTimer);
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UIGSaveSubsystem* Save = GameInstance->GetSubsystem<UIGSaveSubsystem>())
		{
			Save->OnLoadCompleted.RemoveDynamic(this, &ThisClass::HandleLoadCompleted);
		}
	}
	Super::EndPlay(EndPlayReason);
}

float AIGStairwellPresence::ProjectOntoRoute(const FVector& Feet) const
{
	using namespace IGStairwell;
	float Best = -1.0f;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 Index = 1; Index < Route.Num(); ++Index)
	{
		const FVector Closest = FMath::ClosestPointOnSegment(Feet, Route[Index - 1], Route[Index]);
		if (FMath::Abs(Closest.Z - Feet.Z) > RouteHeightTolerance
			|| FVector::DistSquared2D(Closest, Feet) > FMath::Square(RouteWidthTolerance))
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared(Closest, Feet);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = RouteDistance[Index - 1] + FVector::Dist(Route[Index - 1], Closest);
		}
	}
	return Best;
}

FVector AIGStairwellPresence::RoutePoint(const float Distance, FVector* OutForward) const
{
	if (Route.Num() < 2)
	{
		return FVector::ZeroVector;
	}
	const float Clamped = FMath::Clamp(Distance, 0.0f, RouteDistance.Last());
	for (int32 Index = 1; Index < Route.Num(); ++Index)
	{
		if (Clamped <= RouteDistance[Index] || Index == Route.Num() - 1)
		{
			const float Span = FMath::Max(RouteDistance[Index] - RouteDistance[Index - 1], 1.0f);
			const float Alpha = FMath::Clamp((Clamped - RouteDistance[Index - 1]) / Span, 0.0f, 1.0f);
			if (OutForward)
			{
				*OutForward = (Route[Index] - Route[Index - 1]).GetSafeNormal2D();
			}
			return FMath::Lerp(Route[Index - 1], Route[Index], Alpha);
		}
	}
	return Route.Last();
}

FVector AIGStairwellPresence::GetFigureFeet() const
{
	return Figure ? Figure->GetComponentLocation() : FVector::ZeroVector;
}

bool AIGStairwellPresence::IsFigureShown() const
{
	return Figure && !Figure->bHiddenInGame;
}

bool AIGStairwellPresence::ArePrintsShown() const
{
	return Prints.Num() > 0 && Prints[0] && Prints[0]->IsVisible();
}

bool AIGStairwellPresence::IsPointOnScreen(const FVector& Point, const float MinDot) const
{
	const AIGPlayerCharacter* Player = GetPlayer();
	const APlayerController* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
	if (!Controller)
	{
		return false;
	}
	FVector Eye;
	FRotator View;
	Controller->GetPlayerViewPoint(Eye, View);
	if (FVector::DotProduct(View.Vector(), (Point - Eye).GetSafeNormal()) < MinDot)
	{
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IGStairwellSight), false, this);
	Query.AddIgnoredActor(Player);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Query);
}

bool AIGStairwellPresence::IsFigureVisible() const
{
	const AIGPlayerCharacter* Player = GetPlayer();
	const APlayerController* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
	if (!Controller || !IsFigureShown())
	{
		return false;
	}
	// 화면 가장자리까지 본다. 시야각의 반에 5도를 더한 원뿔이다.
	const float Fov = Controller->PlayerCameraManager ? Controller->PlayerCameraManager->GetFOVAngle() : 90.0f;
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(FMath::Min(Fov * 0.5f + 5.0f, 85.0f)));
	const FVector Feet = GetFigureFeet();
	for (const float Height : {30.0f, 80.0f, 115.0f})
	{
		if (IsPointOnScreen(Feet + FVector(0.0f, 0.0f, Height), MinDot))
		{
			return true;
		}
	}
	return false;
}

bool AIGStairwellPresence::IsListenerClear(const bool bStarting) const
{
	const AIGPlayerCharacter* Player = GetPlayer();
	if (!Player)
	{
		return false;
	}
	for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
	{
		const AIGListenerEntity* Listener = *It;
		if (Listener->IsDormant())
		{
			continue;
		}
		const EIGListenerState State = Listener->GetListenerState();
		const bool bPassive = State == EIGListenerState::Patrolling || State == EIGListenerState::Listening
			|| State == EIGListenerState::Banging;
		// 이미 시작했다면 먼 곳의 조사까지는 견딘다. 다가오거나 쫓으면 물러난다.
		const bool bDistantSearch = !bStarting
			&& (State == EIGListenerState::Investigating || State == EIGListenerState::Holding);
		const float Distance = FVector::Dist(Listener->GetActorLocation(), Player->GetActorLocation());
		if (!(bPassive || bDistantSearch) || Distance < (bStarting ? 900.0f : 650.0f))
		{
			return false;
		}
	}
	return true;
}

bool AIGStairwellPresence::CanBegin() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const AIGPlayerCharacter* Player = GetPlayer();
	if (Stage != EStage::Idle || !Narrative || !Player || Route.Num() < 2)
	{
		return false;
	}
	if (!bForceArmed)
	{
		if (Narrative->GetNightIndex() != 2 || !Narrative->IsHourSealed()
			|| Narrative->GetNightElapsedSeconds() < 20.0f || Narrative->HasBeatPlayed(StartedBeat)
			|| ClearSeconds < 2.4f || Player->bIsCrouched || AIGReadableNote::GetOpenNote())
		{
			return false;
		}
		for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It)
		{
			if (It->IsCaptureResetInFlight())
			{
				return false;
			}
		}
		for (TActorIterator<AIGMissingFloorNightTwoBeatDirector> It(GetWorld()); It; ++It)
		{
			if (It->GetReturnStage() != EIGNightTwoReturnStage::Idle)
			{
				return false;
			}
		}
		if (const APlayerController* Controller = Cast<APlayerController>(Player->GetController()))
		{
			if (Controller->IsMoveInputIgnored())
			{
				return false;
			}
			if (const AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD()))
			{
				if (!Hud->IsDialogueLaneIdle() || Hud->IsMissingFloorJournalVisible())
				{
					return false;
				}
			}
		}
	}
	// 계단 위에서 걷고 있어야 한다. 서 있는 사람 뒤에서 발소리가 시작되면 그건 그냥 소리다.
	const FVector Feet = Player->GetActorLocation()
		- FVector(0.0f, 0.0f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	return AIGPrologueWorldScene::IsInsideStairCore(Player->GetActorLocation())
		&& ProjectOntoRoute(Feet) >= 0.0f
		&& Player->GetVelocity().Size2D() > 60.0f;
}

void AIGStairwellPresence::Poll()
{
	if (!bConfigured)
	{
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	AIGPlayerCharacter* Player = GetPlayer();
	if (!Narrative || !Player)
	{
		return;
	}
	ClearSeconds = IsListenerClear(true) ? ClearSeconds + 0.2f : 0.0f;

	// 남은 발자국. 셋째 밤이 오면 말라서 없다.
	if (PrintsAge >= 0.0f)
	{
		PrintsAge += 0.2f;
		if (Narrative->GetNightIndex() >= 3 && Narrative->IsHourSealed())
		{
			for (UDecalComponent* Decal : Prints)
			{
				Decal->SetVisibility(false);
			}
			PrintsAge = -1.0f;
		}
		else if (!Narrative->IsHourSealed() && Prints.Num() > 0 && Prints[0]->IsVisible()
			&& FVector::Dist(Player->GetPawnViewLocation(), Prints[0]->GetComponentLocation()) < 260.0f
			&& IsPointOnScreen(Prints[0]->GetComponentLocation() + FVector(0.0f, 0.0f, 4.0f), 0.85f))
		{
			DayLookSeconds += 0.2f;
			if (DayLookSeconds > 0.8f && Narrative->MarkBeatPlayed(DayPrintsBeat))
			{
				AIGHorrorHUD::PushThought(this,
					NSLOCTEXT("IGStairwell", "DayPrintsThought", "젖은 발자국… 아직 안 말랐네."), 2.6f);
			}
		}
		else
		{
			DayLookSeconds = 0.0f;
		}
	}

	if (Stage == EStage::Idle && CanBegin())
	{
		Begin();
	}
}

void AIGStairwellPresence::Begin()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	AIGPlayerCharacter* Player = GetPlayer();
	if (!Narrative || !Player)
	{
		return;
	}
	Narrative->MarkBeatPlayed(StartedBeat);
	bForcedRun = bForceArmed;
	bForceArmed = false;
	const FVector Feet = Player->GetActorLocation()
		- FVector(0.0f, 0.0f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	PlayerRouteDistance = LastRouteDistance = ProjectOntoRoute(Feet);
	// 걷는 방향은 몸이 향한 쪽을 길의 방향과 견줘 정한다.
	FVector Forward = FVector::ForwardVector;
	RoutePoint(PlayerRouteDistance, &Forward);
	FollowSign = FVector::DotProduct(Player->GetVelocity().GetSafeNormal2D(), Forward) >= 0.0f ? 1.0f : -1.0f;
	FollowerDistance = FMath::Clamp(PlayerRouteDistance - FollowSign * IGStairwell::FollowLag, 0.0f, RouteDistance.Last());
	Stage = EStage::Following;
	SceneSeconds = QuietSeconds = WatchedSeconds = TravelSinceStep = 0.0f;
	StepDelay = -1.0f;
	StepsPlayed = ExtraSteps = 0;
	bExtraPending = bTwitched = bLifted = false;
	CaptureCountAtStart = Narrative->GetCaptureCount();
	PlaceFigure();
	Figure->SetHiddenInGame(false);
	if (IdleAnim)
	{
		Figure->PlayAnimation(IdleAnim, true);
	}
	SetActorTickEnabled(true);
	UE_LOG(LogIndieGame, Display, TEXT("STAIRWELL_PRESENCE begin route=%.0f follower=%.0f sign=%.0f"),
		PlayerRouteDistance, FollowerDistance, FollowSign);
}

bool AIGStairwellPresence::FindTreadTop(const FVector& Near, FVector& OutTop) const
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IGStairwellTread), false, this);
	if (const AIGPlayerCharacter* Player = GetPlayer())
	{
		Query.AddIgnoredActor(Player);
	}
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Near + FVector(0.0f, 0.0f, 40.0f), Near - FVector(0.0f, 0.0f, 60.0f),
		ECC_Visibility, Query) && Hit.ImpactNormal.Z > 0.7f)
	{
		OutTop = Hit.ImpactPoint;
		return true;
	}
	return false;
}

void AIGStairwellPresence::PlaceFigure()
{
	FVector Forward = FVector::ForwardVector;
	FVector Feet = RoutePoint(FollowerDistance, &Forward);
	FVector Top;
	if (FindTreadTop(Feet, Top))
	{
		Feet.Z = Top.Z;
	}
	// 그녀가 간 쪽을 보고 선다. 저작 메시는 앞이 -Y라 90도를 더한다.
	const FVector Facing = Forward * FollowSign;
	Figure->SetWorldLocationAndRotation(Feet, FRotator(0.0f, Facing.Rotation().Yaw + 90.0f, 0.0f));
}

void AIGStairwellPresence::PlayStep(const bool bExtra)
{
	const FVector At = GetFigureFeet() + FVector(0.0f, 0.0f, 4.0f);
	const uint32 Hash = static_cast<uint32>(StepsPlayed * 13 + 7);
	IGAudio::SpawnOneShotAt(this,
		IGAudio::SampleVariantOr(TEXT("Foot_MetalStair"), 5, Hash,
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSurfaceFootstep(this, EIGFootstepSurface::MetalStair, 0.82f, 0.6f); }),
		At, bExtra ? 0.95f : 0.8f, 0.94f + 0.02f * (StepsPlayed % 3), 120.0f, 1400.0f, EIGAudioBus::Entity);
	if (StepsPlayed++ == 0)
	{
		AIGHorrorHUD::PushAudioCaptionAt(this,
			NSLOCTEXT("IGStairwell", "FollowingCaption", "따라오는 발소리"), 2.6f, At);
	}
	if (bExtra)
	{
		++ExtraSteps;
		UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (Narrative && Narrative->MarkBeatPlayed(HeardBeat))
		{
			AIGHorrorHUD::PushThought(this, NSLOCTEXT("IGStairwell", "ExtraStepThought", "…한 발 더 들렸어."), 2.2f);
		}
	}
}

void AIGStairwellPresence::Withdraw(const TCHAR* Reason)
{
	if (Stage == EStage::Withdrawing || Stage == EStage::Spent)
	{
		return;
	}
	UE_LOG(LogIndieGame, Display, TEXT("STAIRWELL_PRESENCE withdraw reason=%s steps=%d extra=%d"), Reason, StepsPlayed, ExtraSteps);
	Stage = EStage::Withdrawing;
	StepDelay = -1.0f;
	bExtraPending = false;
}

void AIGStairwellPresence::LeavePrints()
{
	if (Prints.Num() < 2 || !PrintMaterial)
	{
		return;
	}
	const FVector Feet = GetFigureFeet();
	const float Yaw = Figure->GetComponentRotation().Yaw - 90.0f;
	const FVector Side = FRotator(0.0f, Yaw + 90.0f, 0.0f).Vector();
	// 선 자리에 한 켤레, 반 걸음 뒤에 한 켤레. 디딤판 윗면에 얇게 투영해 챌판에 번지지 않는다.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FVector Near = Feet + FRotator(0.0f, Yaw, 0.0f).Vector() * (Index == 0 ? 0.0f : -38.0f)
			+ Side * (Index == 0 ? 0.0f : 6.0f);
		FVector Top;
		if (!FindTreadTop(Near, Top))
		{
			Prints[Index]->SetVisibility(false);
			continue;
		}
		Prints[Index]->SetWorldLocationAndRotation(Top, FRotator(-90.0f, Yaw, 0.0f));
		Prints[Index]->SetVisibility(true);
	}
	PrintMaterial->SetScalarParameterValue(TEXT("Wetness"), 0.72f);
	PrintsAge = 0.0f;
}

void AIGStairwellPresence::Finish()
{
	if (Stage == EStage::Spent)
	{
		return;
	}
	const bool bWasActive = IsEncounterActive();
	Stage = EStage::Spent;
	StepDelay = -1.0f;
	bExtraPending = false;
	if (bWasActive && !Figure->bHiddenInGame)
	{
		LeavePrints();
	}
	Figure->SetHiddenInGame(true);
	SetActorTickEnabled(false);
	if (bWasActive)
	{
		SaveEncounter();
		UE_LOG(LogIndieGame, Display, TEXT("STAIRWELL_PRESENCE finish steps=%d extra=%d seen=%.1f lifted=%d"),
			StepsPlayed, ExtraSteps, WatchedSeconds, bLifted ? 1 : 0);
	}
}

void AIGStairwellPresence::Tick(const float DeltaSeconds)
{
	using namespace IGStairwell;
	Super::Tick(DeltaSeconds);
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	AIGPlayerCharacter* Player = GetPlayer();
	if (!IsEncounterActive() || !Narrative || !Player)
	{
		SetActorTickEnabled(false);
		return;
	}
	if (Narrative->GetCaptureCount() != CaptureCountAtStart)
	{
		// 붙잡혔다. 그 일과 함께 이 일도 없던 것이 된다.
		Figure->SetHiddenInGame(true);
		Stage = EStage::Spent;
		SetActorTickEnabled(false);
		return;
	}
	SceneSeconds += DeltaSeconds;
	const FVector Feet = Player->GetActorLocation()
		- FVector(0.0f, 0.0f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const float OnRoute = ProjectOntoRoute(Feet);
	const bool bQuiet = Player->bIsCrouched || Player->GetVelocity().Size2D() < 12.0f;
	QuietSeconds = bQuiet ? QuietSeconds + DeltaSeconds : 0.0f;
	if (OnRoute >= 0.0f)
	{
		const float Delta = OnRoute - LastRouteDistance;
		if (FMath::Abs(Delta) > 1.5f && FMath::Abs(Delta) < 60.0f && !Player->bIsCrouched)
		{
			TravelSinceStep += FMath::Abs(Delta);
		}
		LastRouteDistance = PlayerRouteDistance = OnRoute;
	}
	const bool bVisible = IsFigureVisible();
	WatchedSeconds = bVisible ? WatchedSeconds + DeltaSeconds : (Stage == EStage::Watched ? WatchedSeconds : 0.0f);

	// 물러나는 까닭들. 붙잡지 않고, 위층 사람과 겹치지 않는다.
	if (Stage != EStage::Withdrawing)
	{
		if (!bForcedRun && (!Narrative->IsHourSealed() || Narrative->GetNightIndex() != 2))
		{
			Withdraw(TEXT("dawn"));
		}
		else if (!AIGPrologueWorldScene::IsInsideStairCore(Player->GetActorLocation()) && SceneSeconds > 1.0f)
		{
			Withdraw(TEXT("left_core"));
		}
		else if (!IsListenerClear(false))
		{
			Withdraw(TEXT("listener"));
		}
		else if (SceneSeconds > SceneLimitSeconds)
		{
			Withdraw(TEXT("time"));
		}
	}

	switch (Stage)
	{
	case EStage::Following:
	{
		if (bVisible && WatchedSeconds > 0.3f)
		{
			Stage = EStage::Watched;
			StepDelay = -1.0f;
			break;
		}
		// 보이지 않는 동안 한 띠 뒤로 따라붙는다. 그녀가 돌아서 오면 그만큼 물러선다.
		const float Target = FMath::Clamp(PlayerRouteDistance - FollowSign * FollowLag, 0.0f, RouteDistance.Last());
		if (!bVisible)
		{
			FollowerDistance = FMath::FInterpConstantTo(FollowerDistance, Target, DeltaSeconds, FollowSpeed);
			PlaceFigure();
		}
		if (TravelSinceStep >= StrideLength && StepDelay < 0.0f)
		{
			TravelSinceStep = 0.0f;
			StepDelay = StepLatency;
			bExtraPending = false;
		}
		if (StepDelay >= 0.0f)
		{
			StepDelay -= DeltaSeconds;
			if (StepDelay < 0.0f)
			{
				PlayStep(bExtraPending);
				bExtraPending = false;
			}
		}
		// 멈추면 한 발이 더 난다. 그 뒤로는 조용하다.
		if (QuietSeconds >= QuietToExtra && StepsPlayed > 0 && StepDelay < 0.0f
			&& !bExtraPending && ExtraSteps < MaxExtraSteps && QuietSeconds < QuietToExtra + DeltaSeconds * 1.5f)
		{
			bExtraPending = true;
			StepDelay = ExtraStepLatency;
		}
		break;
	}
	case EStage::Watched:
	{
		UIGMissingFloorNarrativeSubsystem* Story = GetNarrative();
		if (WatchedSeconds > 0.6f && Story)
		{
			Story->MarkBeatPlayed(SeenBeat);
		}
		if (WatchedSeconds > TwitchAfterSeconds && !bTwitched)
		{
			bTwitched = true;
			if (TwitchAnim)
			{
				Figure->PlayAnimation(TwitchAnim, false);
			}
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateClothSettle(this),
				GetFigureFeet() + FVector(0.0f, 0.0f, 90.0f), 0.6f, 0.8f, 80.0f, 700.0f, EIGAudioBus::Entity);
		}
		if (WatchedSeconds > LiftAfterSeconds && !bLifted)
		{
			// 고개를 든다. 얼굴이 있어야 할 자리가 비어 있다.
			bLifted = true;
			if (LiftAnim)
			{
				Figure->PlayAnimation(LiftAnim, false);
			}
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateCloseCallStinger(this),
				GetFigureFeet() + FVector(0.0f, 0.0f, 120.0f), 0.85f, 0.92f, 200.0f, 1600.0f, EIGAudioBus::Entity);
		}
		if (bVisible && !bLifted
			&& FVector::Dist(Player->GetActorLocation(), GetFigureFeet() + FVector(0.0f, 0.0f, 90.0f)) < 170.0f)
		{
			// 보면서 다가왔다. 거기서 고개를 든다.
			WatchedSeconds = FMath::Max(WatchedSeconds, LiftAfterSeconds);
		}
		if (!bVisible)
		{
			// 눈을 돌렸다. 고개를 든 뒤라면 그걸로 끝이고, 아니면 다시 따라온다.
			if (bLifted)
			{
				Withdraw(TEXT("seen"));
			}
			else
			{
				Stage = EStage::Following;
				WatchedSeconds = 0.0f;
				if (IdleAnim)
				{
					Figure->PlayAnimation(IdleAnim, true);
				}
			}
		}
		break;
	}
	case EStage::Withdrawing:
		// 보고 있는 동안은 그 자리에 있다. 눈을 돌리면 없다.
		if (!bVisible)
		{
			Finish();
		}
		break;
	default:
		break;
	}
}

void AIGStairwellPresence::SaveEncounter() const
{
	UGameInstance* GameInstance = GetGameInstance();
	UIGSaveSubsystem* Save = GameInstance ? GameInstance->GetSubsystem<UIGSaveSubsystem>() : nullptr;
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Save || !Narrative)
	{
		return;
	}
	const TCHAR* Checkpoint = Narrative->IsHourSealed() ? TEXT("Checkpoint.MissingFloor.Night")
		: TEXT("Checkpoint.MissingFloor.Day");
	Save->RequestAutosave(
		FGameplayTag::RequestGameplayTag(TEXT("Chapter.MissingFloor"), false),
		GetWorld()->GetOutermost()->GetFName(),
		FGameplayTag::RequestGameplayTag(Checkpoint, false));
}

void AIGStairwellPresence::HandleLoadCompleted(bool bSuccess, FString /*SlotName*/, UIGSaveGame* /*SaveGame*/)
{
	if (!bSuccess)
	{
		return;
	}
	// 불러온 판에서는 지금 겪는 일도, 남은 발자국도 없다.
	Figure->SetHiddenInGame(true);
	SetActorTickEnabled(false);
	Stage = EStage::Idle;
	for (UDecalComponent* Decal : Prints)
	{
		Decal->SetVisibility(false);
	}
	PrintsAge = -1.0f;
}
