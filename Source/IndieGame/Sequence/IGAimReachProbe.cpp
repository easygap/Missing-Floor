#include "Sequence/IGAimReachProbe.h"

#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGManagerPatrol.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "IndieGame.h"
#include "Interaction/IGInteractable.h"
#include "Misc/CommandLine.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGInteractionComponent.h"
#include "Player/IGPlayerCharacter.h"
#include "TimerManager.h"

namespace IGAimReach
{
	// 건물의 층 바닥 높이. 탁자·선반 윗면에 선 자리를 세지 않으려고 바닥은 이 높이 근처만 받는다.
	// 1층(골목·로비·편의점·필로티)은 0, 2·3·4층 복도와 집은 300·600·900, 옥상과 5층은 1200이다.
	constexpr float FloorLevels[] = {0.0f, 300.0f, 600.0f, 900.0f, 1200.0f};
	constexpr float FloorTolerance = 14.0f;
	// 둘레를 도는 각도와 물체에서 떨어진 거리. 상호작용 거리(220 cm) 안쪽만 본다.
	constexpr int32 YawSteps = 24;
	constexpr float Distances[] = {45.0f, 70.0f, 100.0f, 140.0f, 180.0f};
	// 물체 하나에 이만큼 자리를 찾으면 그만 본다.
	constexpr int32 EnoughSpots = 1;
	// 밤이나 낮이 열리고 감독이 물건을 세울 때까지 기다리는 시간.
	constexpr float SettleSeconds = 2.5f;
}

AIGAimReachProbe::AIGAimReachProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AIGAimReachProbe::Configure(AIGPlayerCharacter* InPlayer)
{
	Player = InPlayer;
	for (TActorIterator<AIGNightPhaseDirector> It(GetWorld()); It; ++It)
	{
		NightPhase = *It;
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr)
	{
		Narrative->ResetNarrative();
	}
	UE_LOG(LogIndieGame, Display, TEXT("AIM_REACH stage player=%d night_phase=%d"), Player.IsValid() ? 1 : 0, NightPhase.IsValid() ? 1 : 0);
	if (!Player.IsValid() || !NightPhase.IsValid())
	{
		++Failures;
		Finish();
	}
}

void AIGAimReachProbe::Silence()
{
	UWorld* World = GetWorld();
	// 위협은 재운다. 이 검사는 서 있는 물건의 자리만 본다.
	for (TActorIterator<AIGNightThreatDirector> It(World); It; ++It)
	{
		It->SetActorTickEnabled(false);
		World->GetTimerManager().ClearAllTimersForObject(*It);
	}
	for (TActorIterator<AIGListenerEntity> It(World); It; ++It)
	{
		It->SetDormant(true);
	}
	for (TActorIterator<AIGManagerPatrol> It(World); It; ++It)
	{
		It->SetActorTickEnabled(false);
	}
}

bool AIGAimReachProbe::ConcludeNight()
{
	if (!NightPhase.IsValid())
	{
		return false;
	}
	if (NightPhase->IsHourActive())
	{
		NightPhase->CompleteNightGoal();
		NightPhase->ConcludeNight();
	}
	return !NightPhase->IsHourActive();
}

bool AIGAimReachProbe::BeginNight(const int32 Night)
{
	if (!ConcludeNight())
	{
		return false;
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr)
	{
		Narrative->SetNightIndex(Night - 1);
	}
	NightPhase->BeginTheHour(Night);
	Silence();
	return NightPhase->IsHourActive();
}

bool AIGAimReachProbe::IsFloorHeight(const float Z) const
{
	for (const float Level : IGAimReach::FloorLevels)
	{
		if (FMath::Abs(Z - Level) <= IGAimReach::FloorTolerance)
		{
			return true;
		}
	}
	return false;
}

bool AIGAimReachProbe::FindSpot(AActor* Target, const FBox& Box, FAimSpot& OutSpot, int32& OutTried,
	TMap<FString, int32>& OutBlockers) const
{
	UWorld* World = GetWorld();
	AIGPlayerCharacter* Pawn = Player.Get();
	if (!World || !Pawn)
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent();
	const float Radius = Capsule->GetUnscaledCapsuleRadius();
	const float HalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	const float StandEye = Pawn->GetEyeOffsetFromActor().Z;
	const UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
	const float CrouchHalf = Movement ? Movement->GetCrouchedHalfHeight() : HalfHeight * 0.5f;
	const float CrouchEye = Pawn->CrouchedEyeHeight > 0.0f ? Pawn->CrouchedEyeHeight : 32.0f;
	const UIGInteractionComponent* Interaction = Pawn->FindComponentByClass<UIGInteractionComponent>();
	const float Reach = Interaction ? Interaction->GetTraceDistance() : 220.0f;

	const FVector Center = Box.GetCenter();
	const FVector Extent = Box.GetExtent();
	// 상자 가운데와 네 귀퉁이 쪽. 큰 표적(벽 한 칸)은 한 점만 보면 모서리 하나에 가린 것을 놓친다.
	const FVector AimPoints[] = {
		Center,
		Center + FVector(0.0f, Extent.Y * 0.4f, Extent.Z * 0.4f),
		Center + FVector(0.0f, -Extent.Y * 0.4f, Extent.Z * 0.4f),
		Center + FVector(Extent.X * 0.4f, 0.0f, -Extent.Z * 0.4f),
		Center + FVector(-Extent.X * 0.4f, 0.0f, -Extent.Z * 0.4f),
	};

	FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(IGAimReachFloor), false, Pawn);
	FloorParams.AddIgnoredActor(Target);
	const FCollisionShape StandShape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	const FCollisionShape CrouchShape = FCollisionShape::MakeCapsule(Radius, CrouchHalf);

	OutTried = 0;
	for (const float Distance : IGAimReach::Distances)
	{
		for (int32 Step = 0; Step < IGAimReach::YawSteps; ++Step)
		{
			const float Yaw = 360.0f * Step / IGAimReach::YawSteps;
			const FVector Direction = FRotator(0.0f, Yaw, 0.0f).Vector();
			const FVector Column = FVector(Center.X, Center.Y, 0.0f) + Direction * (Distance + FMath::Max(Extent.X, Extent.Y) * 0.5f);
			// 물체보다 조금 위에서 아래로 바닥을 찾는다. 가장 먼저 닿는 면이 바닥 높이가 아니면 그 자리는 버린다.
			// 천장 바로 밑 표적에서 시작점이 천장 위로 올라가지 않게 표적 높이의 절반 남짓만 올린다.
			FHitResult Floor;
			const FVector FloorStart(Column.X, Column.Y, Center.Z + FMath::Min(40.0f, Extent.Z + 5.0f));
			const FVector FloorEnd(Column.X, Column.Y, Center.Z - 260.0f);
			if (!World->LineTraceSingleByChannel(Floor, FloorStart, FloorEnd, ECC_Visibility, FloorParams)
				|| Floor.ImpactNormal.Z < 0.7f || !IsFloorHeight(Floor.ImpactPoint.Z))
			{
				continue;
			}
			for (int32 Pose = 0; Pose < 2; ++Pose)
			{
				const bool bCrouch = Pose == 1;
				const float Half = bCrouch ? CrouchHalf : HalfHeight;
				const FVector Body = Floor.ImpactPoint + FVector(0.0f, 0.0f, Half + 2.0f);
				if (World->OverlapBlockingTestByProfile(Body, FQuat::Identity, TEXT("Pawn"),
					bCrouch ? CrouchShape : StandShape, FloorParams))
				{
					continue;
				}
				const FVector Eye = Body + FVector(0.0f, 0.0f, bCrouch ? CrouchEye : StandEye);
				for (const FVector& Aim : AimPoints)
				{
					++OutTried;
					if (FVector::Dist(Eye, Aim) > Reach - 5.0f)
					{
						continue;
					}
					FCollisionQueryParams SightParams(SCENE_QUERY_STAT(IGAimReachSight), false, Pawn);
					const FVector End = Eye + (Aim - Eye).GetSafeNormal() * Reach;
					FHitResult Sight;
					bool bHit = World->LineTraceSingleByChannel(Sight, Eye, End, ECC_Visibility, SightParams);
					for (int32 Skip = 0; Skip < UIGInteractionComponent::MaxDormantSkips && bHit && Sight.GetActor() != Target
						&& UIGInteractionComponent::IsDormantInteractable(Sight.GetActor(), Pawn); ++Skip)
					{
						SightParams.AddIgnoredActor(Sight.GetActor());
						bHit = World->LineTraceSingleByChannel(Sight, Eye, End, ECC_Visibility, SightParams);
					}
					if (bHit && Sight.GetActor() == Target)
					{
						OutSpot.Eye = Eye;
						OutSpot.Feet = Floor.ImpactPoint;
						return true;
					}
					// 무엇이 가렸는지 센다. 실패한 물체의 보고에 가장 많이 가린 것을 붙인다.
					const FString Blocker = bHit
						? FString::Printf(TEXT("%s/%s"), Sight.GetActor() ? *Sight.GetActor()->GetName() : TEXT("?"),
							Sight.GetComponent() ? *Sight.GetComponent()->GetName() : TEXT("?"))
						: FString(TEXT("nothing_within_reach"));
					OutBlockers.FindOrAdd(Blocker) += 1;
				}
			}
		}
	}
	return false;
}

void AIGAimReachProbe::Audit(const TCHAR* Label)
{
	UWorld* World = GetWorld();
	AIGPlayerCharacter* Pawn = Player.Get();
	if (!World || !Pawn)
	{
		return;
	}
	int32 Checked = 0;
	int32 Missed = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor == Pawn || Actor->IsHidden()
			|| Actor->GetAttachParentActor() == Pawn
			|| !Actor->GetClass()->ImplementsInterface(UIGInteractable::StaticClass())
			|| !IIGInteractable::Execute_CanInteract(Actor, Pawn))
		{
			continue;
		}
		// 시선 트레이스를 막는 몸만 겨눌 수 있다. 그런 몸이 하나도 없으면 그 자체가 결함이다.
		FBox Box(ForceInit);
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
		for (const UPrimitiveComponent* Primitive : Primitives)
		{
			if (Primitive && Primitive->IsRegistered() && Primitive->IsQueryCollisionEnabled()
				&& Primitive->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block)
			{
				Box += Primitive->Bounds.GetBox();
			}
		}
		++Checked;
		const FString Name = Actor->GetName();
		if (!Box.IsValid)
		{
			++Missed;
			if (!Reported.Contains(Name))
			{
				Reported.Add(Name);
				++Failures;
				UE_LOG(LogIndieGame, Display, TEXT("AIM_REACH %s %s FAIL reason=no_trace_body at=%s"),
					Label, *Name, *Actor->GetActorLocation().ToCompactString());
			}
			continue;
		}
		FAimSpot Spot;
		int32 Tried = 0;
		TMap<FString, int32> Blockers;
		if (FindSpot(Actor, Box, Spot, Tried, Blockers))
		{
			UE_LOG(LogIndieGame, Verbose, TEXT("AIM_REACH %s %s PASS feet=%s"), Label, *Name, *Spot.Feet.ToCompactString());
			continue;
		}
		++Missed;
		FString TopBlocker(TEXT("none"));
		int32 TopCount = 0;
		for (const TPair<FString, int32>& Pair : Blockers)
		{
			if (Pair.Value > TopCount)
			{
				TopCount = Pair.Value;
				TopBlocker = Pair.Key;
			}
		}
		if (!Reported.Contains(Name))
		{
			Reported.Add(Name);
			++Failures;
			UE_LOG(LogIndieGame, Display, TEXT("AIM_REACH %s %s FAIL reason=no_spot tried=%d box_center=%s extent=%s blocker=%s x%d"),
				Label, *Name, Tried, *Box.GetCenter().ToCompactString(), *Box.GetExtent().ToCompactString(), *TopBlocker, TopCount);
		}
		else
		{
			UE_LOG(LogIndieGame, Display, TEXT("AIM_REACH %s %s FAIL again"), Label, *Name);
		}
	}
	++Audited;
	UE_LOG(LogIndieGame, Display, TEXT("AIM_REACH_STAGE %s checked=%d missed=%d"), Label, Checked, Missed);
}

void AIGAimReachProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	if (PhaseSeconds < IGAimReach::SettleSeconds)
	{
		return;
	}
	// 짝수 단계에서 밤이나 낮을 열고, 홀수 단계에서 자리를 잡은 뒤 감사한다.
	switch (Phase)
	{
	case 0:
		Silence();
		Audit(TEXT("start"));
		break;
	case 1:
		if (!BeginNight(1)) { ++Failures; Finish(); return; }
		break;
	case 2:
		Audit(TEXT("night1"));
		break;
	case 3:
		if (NightPhase.IsValid()) { NightPhase->CompleteNightGoal(); }
		break;
	case 4:
		Audit(TEXT("night1_goal"));
		break;
	case 5:
		if (!ConcludeNight()) { ++Failures; Finish(); return; }
		break;
	case 6:
		Silence();
		Audit(TEXT("day2"));
		break;
	case 7:
		if (!BeginNight(2)) { ++Failures; Finish(); return; }
		break;
	case 8:
		Audit(TEXT("night2"));
		break;
	case 9:
		if (!ConcludeNight()) { ++Failures; Finish(); return; }
		break;
	case 10:
		Silence();
		Audit(TEXT("day3"));
		break;
	case 11:
		if (!BeginNight(3)) { ++Failures; Finish(); return; }
		break;
	case 12:
		Audit(TEXT("night3"));
		break;
	case 13:
		if (!ConcludeNight()) { ++Failures; Finish(); return; }
		break;
	case 14:
		Silence();
		Audit(TEXT("day4"));
		break;
	case 15:
		if (!BeginNight(4)) { ++Failures; Finish(); return; }
		break;
	case 16:
		Audit(TEXT("night4"));
		break;
	default:
		Finish();
		return;
	}
	++Phase;
	PhaseSeconds = 0.0f;
}

void AIGAimReachProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("AIM_REACH_PROBE %s failures=%d stages=%d"),
		Failures ? TEXT("FAIL") : TEXT("PASS"), Failures, Audited);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}
