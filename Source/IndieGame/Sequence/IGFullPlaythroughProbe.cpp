#include "Sequence/IGFullPlaythroughProbe.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Algo/Reverse.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGListenerGreyboxDirector.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGMissingFloorNightTwoBeatDirector.h"
#include "Entity/IGMissingFloorPuzzleOneDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Entity/IGNightThreatDirector.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "IndieGame.h"
#include "Interaction/IGFlashlightPickup.h"
#include "Interaction/IGHidingSpot.h"
#include "Interaction/IGInteractable.h"
#include "Interaction/IGReadableNote.h"
#include "Interaction/IGSlidingDoor.h"
#include "Interaction/IGSwingDoor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGStoryStateSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGInteractionComponent.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGFullPlaythrough
{
	/** 길 찾기 격자 한 칸(cm). 몸 반지름의 반이 안 되게 잡아 108 cm 문간을 놓치지 않는다. */
	constexpr float PathCell = 12.0f;
	/** 출발점과 목표를 감싼 상자 둘레로 이만큼 더 본다. 돌아가는 길이 이 안에 있어야 찾는다. */
	constexpr float PathMargin = 300.0f;
	constexpr int32 MaxPathSide = 400;
	constexpr int32 MaxPathExpansions = 160000;
	/** 이 높이까지의 턱과 단은 넘는다. 캐릭터의 단 오르기보다 낮게 잡는다. */
	constexpr float StepLift = 22.0f;
	/** 이웃 칸 바닥 높이 차가 이보다 크면 건너지 못한다. 계단 한 단은 넘는다. */
	constexpr float StepRise = 20.0f;
	/** 몸 반지름에 더하는 여유. 이만큼은 비어야 선다. */
	constexpr float BodySkin = 4.0f;
	/** 이만큼 더 비어 있지 않은 칸은 벽이나 가구에 붙은 칸이라 비용을 더 매긴다. 길이 복도 가운데로 난다. */
	constexpr float RoomyGap = 18.0f;
	constexpr float TightCost = 2.5f;
	/** 칸 바닥을 찾는 높이. 이웃 칸 바닥 위아래로 본다. */
	constexpr float FloorSearch = 70.0f;
	/** 출발과 목표 바닥 높이에서 이만큼 벗어난 바닥(가구 위, 다른 층 계단)은 쓰지 않는다. */
	constexpr float FloorBand = 45.0f;
	/** 걷다 막히거나 기다리던 일이 오지 않으면 다시 해 보는 횟수(처음 포함). */
	constexpr int32 MaxAttempts = 3;
	/** 길이 아예 없을 때 기다렸다 다시 찾는 간격과 횟수. 사람이 비켜 주기를 1분 반까지 기다린다. */
	constexpr float BlockedWaitSeconds = 3.0f;
	constexpr int32 MaxBlockedWaits = 30;
	/** 피할 자리로 물러나 기다릴 때는 순찰이 한 바퀴 돌 만큼 더 기다린다. */
	constexpr int32 MaxRefugeWaits = 80;
	/** 중간 점은 이만큼 가까이 가면 다음 점으로 간다. */
	constexpr float PassTolerance = 22.0f;
	/** 물건 앞에 서는 거리들. 사람이 손을 뻗는 거리부터 본다. */
	constexpr float StandRadii[] = {85.0f, 105.0f, 125.0f, 65.0f, 145.0f};
	constexpr int32 StandDirections = 24;
	/** 설 자리는 지금 서 있는 바닥에서 이만큼 안이어야 한다. 그보다 높은 데는 Stand로 준다. */
	constexpr float StandFloorRange = 40.0f;
	/** 이만큼 말이 없으면 다 들은 것으로 본다. */
	constexpr float QuietSeconds = 1.5f;
	constexpr float PositionLogSeconds = 5.0f;

	/** 4층 옥상 계단. 4층 복도 서쪽 끝에서 반 층 참을 지나 철문 아래까지(발 높이). */
	TArray<FVector> RoofStairUp()
	{
		const float FourthZ = AIGPrologueWorldScene::GetStoreyFloorZ(3);
		return {
			FVector(-262.0f, -292.0f, FourthZ),
			FVector(-277.5f, -238.0f, FourthZ),
			FVector(-277.5f, -150.0f, FourthZ + 72.0f),
			FVector(-277.5f, -125.0f, FourthZ + 72.0f),
			FVector(-277.5f, -100.0f, FourthZ + 80.0f),
			FVector(-277.5f, 110.0f, FourthZ + 242.0f),
		};
	}
}

AIGFullPlaythroughProbe::AIGFullPlaythroughProbe()
{
	// -IGFullPlaythrough로만 선다. 입력을 넣고 걷는 길을 매 프레임 본다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGFullPlaythroughProbe::Configure(AIGPlayerCharacter* InPlayer, AIGListenerGreyboxDirector* InDirector)
{
	Player = InPlayer;
	Director = InDirector;
	float Minutes = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("IGFullPlaythroughMinutes="), Minutes) && Minutes > 0.0f)
	{
		LimitSeconds = Minutes * 60.0f;
	}
	WallStartSeconds = FPlatformTime::Seconds();
}

UIGMissingFloorNarrativeSubsystem* AIGFullPlaythroughProbe::GetNarrative() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
}

bool AIGFullPlaythroughProbe::HasBeat(const TCHAR* Beat) const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative && Narrative->HasBeatPlayed(FName(Beat));
}

AActor* AIGFullPlaythroughProbe::FindNamed(const FString& Name) const
{
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetName() == Name)
		{
			return *It;
		}
	}
	return nullptr;
}

template <typename T>
T* AIGFullPlaythroughProbe::FindFirst() const
{
	for (TActorIterator<T> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool AIGFullPlaythroughProbe::IsTalkBusy() const
{
	return AIGHorrorHUD::IsPresentingDialogue(this);
}

bool AIGFullPlaythroughProbe::IsRestBlocked() const
{
	const AIGNightThreatDirector* Threats = FindFirst<AIGNightThreatDirector>();
	if (Threats && Threats->IsBusyForNightRest())
	{
		return true;
	}
	const AIGListenerEntity* Entity = FindFirst<AIGListenerEntity>();
	if (!Entity || Entity->IsDormant())
	{
		return false;
	}
	const EIGListenerState State = Entity->GetListenerState();
	return State == EIGListenerState::Chasing || State == EIGListenerState::CaptureHold || Entity->IsAtHomeDoor();
}

FString AIGFullPlaythroughProbe::GetObjective() const
{
	// 입주 저녁에는 감독이, 그 뒤로는 밤 진행이 목표 줄을 든다(밤 동안에는 둘 다 비운다).
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	FText Objective;
	if (Narrative && Narrative->GetNightIndex() == 0)
	{
		if (const AIGListenerGreyboxDirector* Greybox = Director.Get())
		{
			Objective = Greybox->GetObjectiveText();
		}
	}
	else if (NightPhase.IsValid())
	{
		Objective = NightPhase->GetObjectiveText();
	}
	return Objective.ToString();
}

void AIGFullPlaythroughProbe::Check(const bool bPass, const FString& Name)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (bPass)
	{
		if (Name.StartsWith(TEXT("gate:")))
		{
			UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_CHECK %s PASS t=%.1f"), *Name.RightChop(5), Now);
		}
		return;
	}
	// 말을 끝까지 못 기다린 것과 피할 자리로 다 못 물러난 것은 흐름이 막힌 게 아니다. 적어만 둔다.
	if (Name.StartsWith(TEXT("listen:")) || Name.StartsWith(TEXT("refuge:")))
	{
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_NOTE %s still_talking t=%.1f"), *Name.RightChop(7), Now);
		return;
	}
	if (Name.StartsWith(TEXT("walk:")) || Name.StartsWith(TEXT("gate:")))
	{
		if (Retry && !PendingRetry)
		{
			++Retries;
			PendingRetry = Retry;
			UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_RETRY %s leg=%s t=%.1f at=%s"), *Name, *CurrentLeg, Now,
				Player.IsValid() ? *Player->GetActorLocation().ToCompactString() : TEXT("none"));
			return;
		}
		if (!PendingRetry && AbortReason.IsEmpty())
		{
			AbortReason = Name;
		}
		return;
	}
	++Failures;
	UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_CHECK %s FAIL leg=%s t=%.1f"), *Name, *CurrentLeg, Now);
}

void AIGFullPlaythroughProbe::Shoot(const FString& Name)
{
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FullPlaythrough")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	FScreenshotRequest::RequestScreenshot(
		FPaths::Combine(Directory, FString::Printf(TEXT("%02d_%s.png"), ++ShotIndex, *Name)), true, false);
}

void AIGFullPlaythroughProbe::Finish()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	Route.ReleaseKeys();
	const bool bPass = AbortReason.IsEmpty() && Failures == 0 && LegIndex >= Legs.Num();
	UE_LOG(LogIndieGame, Display,
		TEXT("PLAYTHROUGH %s stage=%s leg=%s blocked=%s failures=%d retries=%d game_seconds=%.1f wall_seconds=%.1f at=%s"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *CurrentStage, *CurrentLeg,
		AbortReason.IsEmpty() ? TEXT("none") : *AbortReason, Failures, Retries, TotalSeconds,
		FPlatformTime::Seconds() - WallStartSeconds,
		Player.IsValid() ? *Player->GetActorLocation().ToCompactString() : TEXT("none"));
	FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1);
}

void AIGFullPlaythroughProbe::AddLeg(const FString& Name, TFunction<void()> Build)
{
	Legs.Add(FLeg{Name, MoveTemp(Build)});
}

void AIGFullPlaythroughProbe::StartNextLeg()
{
	// 아무 입력도 넣지 않는 일(단계 표시, 이미 된 일)은 같은 프레임에 넘긴다.
	while (AbortReason.IsEmpty() && !bFinished)
	{
		++LegIndex;
		if (!Legs.IsValidIndex(LegIndex))
		{
			Finish();
			return;
		}
		TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
		Route.Reset(Player.Get(), [WeakThis](const bool bPass, const FString& Name)
		{
			if (WeakThis.IsValid())
			{
				WeakThis->Check(bPass, Name);
			}
		});
		Retry = nullptr;
		CurrentLeg = Legs[LegIndex].Name;
		// 일을 짓다가 다른 일을 끼워 넣으면 배열이 옮겨진다. 복사해서 부른다.
		const TFunction<void()> Build = Legs[LegIndex].Build;
		Build();
		if (!Route.IsDone())
		{
			return;
		}
	}
}

void AIGFullPlaythroughProbe::Stage(const FString& Name)
{
	AddLeg(TEXT("stage:") + Name, [this, Name]()
	{
		CurrentStage = Name;
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_STAGE %s t=%.1f wall=%.1f at=%s objective=\"%s\""), *Name,
			GetWorld()->GetTimeSeconds(), FPlatformTime::Seconds() - WallStartSeconds,
			Player.IsValid() ? *Player->GetActorLocation().ToCompactString() : TEXT("none"), *GetObjective());
		Shoot(Name);
	});
}

void AIGFullPlaythroughProbe::WaitSeconds(const float Seconds, const FString& Name)
{
	AddLeg(TEXT("wait:") + Name, [this, Seconds, Name]()
	{
		Route.Wait(Seconds, TEXT("wait:") + Name);
	});
}

void AIGFullPlaythroughProbe::WaitUntil(TFunction<bool()> Done, const float Timeout, const FString& Name)
{
	AddLeg(TEXT("until:") + Name, [this, Done, Timeout, Name]()
	{
		Route.Until(Done, Timeout, TEXT("gate:") + Name);
	});
}

void AIGFullPlaythroughProbe::Listen(const FString& Name, const float Timeout)
{
	AddLeg(TEXT("listen:") + Name, [this, Name, Timeout]()
	{
		const TSharedRef<double> QuietSince = MakeShared<double>(-1.0);
		TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
		Route.Until([WeakThis, QuietSince]()
		{
			if (!WeakThis.IsValid())
			{
				return true;
			}
			const double Now = WeakThis->GetWorld()->GetTimeSeconds();
			if (WeakThis->IsTalkBusy())
			{
				*QuietSince = -1.0;
				return false;
			}
			if (*QuietSince < 0.0)
			{
				*QuietSince = Now;
			}
			return Now - *QuietSince >= IGFullPlaythrough::QuietSeconds;
		}, Timeout, TEXT("listen:") + Name);
	});
}

void AIGFullPlaythroughProbe::PressKey(const FString& Name, const FKey& Key, const float Seconds)
{
	AddLeg(TEXT("key:") + Name, [this, Name, Key, Seconds]()
	{
		Route.Press(Seconds, TEXT("key:") + Name, nullptr, Key);
	});
}

void AIGFullPlaythroughProbe::StandUp(const FString& Name)
{
	AddLeg(TEXT("stand:") + Name, [this, Name]()
	{
		const AIGPlayerCharacter* Character = Player.Get();
		if (!Character || !Character->bIsCrouched)
		{
			return;
		}
		Route.Press(0.1f, TEXT("key:") + Name, nullptr, EKeys::C);
		TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
		Route.Until([WeakThis]()
		{
			const AIGPlayerCharacter* Body = WeakThis.IsValid() ? WeakThis->Player.Get() : nullptr;
			return Body && !Body->bIsCrouched;
		}, 3.0f, TEXT("listen:") + Name);
	});
}

bool AIGFullPlaythroughProbe::HasStoryState(const TCHAR* Tag) const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UIGStoryStateSubsystem* Story = GameInstance ? GameInstance->GetSubsystem<UIGStoryStateSubsystem>() : nullptr;
	return Story && Story->HasState(FGameplayTag::RequestGameplayTag(FName(Tag), false));
}

void AIGFullPlaythroughProbe::OpenDoor(const FString& Name, const FString& DoorName, const FVector& Stand)
{
	AddLeg(TEXT("door:") + Name, [this, Name, DoorName, Stand]()
	{
		AIGSwingDoor* Door = Cast<AIGSwingDoor>(FindNamed(DoorName));
		if (!Door)
		{
			AbortReason = TEXT("target:") + Name;
			return;
		}
		if (Door->IsOpen())
		{
			return;
		}
		Route.Walk(Stand, 15.0f, TEXT("walk:") + Name);
		Route.Aim(Door->GetLatchSoundLocation(), TEXT("aim:") + Name, 0.6f);
		Route.Press(0.3f, TEXT("press:") + Name, Door);
		TWeakObjectPtr<AIGSwingDoor> WeakDoor(Door);
		Route.Until([WeakDoor]() { return WeakDoor.IsValid() && WeakDoor->IsOpen() && !WeakDoor->IsSwinging(); }, 6.0f,
			TEXT("gate:") + Name);
	});
}

void AIGFullPlaythroughProbe::GoTo(const FVector& Goal, const float Tolerance, const FString& Name, TOptional<FVector> Refuge)
{
	AddLeg(TEXT("go:") + Name, MakeGo(Goal, Tolerance, Name, 0, 0, Refuge));
}

bool AIGFullPlaythroughProbe::WaitAndRetry(const FString& Name, const int32 Waits, TFunction<void()> Again, const FString& Why,
	TOptional<FVector> Refuge)
{
	if (Waits >= (Refuge.IsSet() ? IGFullPlaythrough::MaxRefugeWaits : IGFullPlaythrough::MaxBlockedWaits))
	{
		return false;
	}
	// 같은 까닭으로 막혀 있는 동안은 처음과 열 번째마다만 적는다.
	if (Waits % 10 == 0)
	{
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_WAIT %s waits=%d why=%s"), *Name, Waits, *Why);
	}
	// 길을 막은 사람의 길목에 서 있지 않는다. 피할 자리로 물러나서 지나가기를 기다린다.
	const AIGPlayerCharacter* Character = Player.Get();
	if (Refuge.IsSet() && Character && FVector::Dist2D(Character->GetActorLocation(), Refuge.GetValue()) > 80.0f)
	{
		const bool bVerbose = bVerbosePlan;
		bVerbosePlan = false;
		TArray<FVector> Back;
		FString BackWhy;
		if (PlanPath(Refuge.GetValue(), Back, BackWhy))
		{
			for (const FVector& Point : Back)
			{
				Route.Walk(Point, IGFullPlaythrough::PassTolerance, TEXT("refuge:") + Name);
			}
		}
		bVerbosePlan = bVerbose;
	}
	Route.Wait(IGFullPlaythrough::BlockedWaitSeconds, TEXT("blocked:") + Name);
	Legs.Insert(FLeg{CurrentLeg, MoveTemp(Again)}, LegIndex + 1);
	return true;
}

TFunction<void()> AIGFullPlaythroughProbe::MakeGo(FVector Goal, float Tolerance, FString Name, int32 Attempt, int32 Waits,
	TOptional<FVector> Refuge)
{
	return [this, Goal, Tolerance, Name, Attempt, Waits, Refuge]()
	{
		TArray<FVector> Points;
		FString Why;
		const int32 MaxWaits = Refuge.IsSet() ? IGFullPlaythrough::MaxRefugeWaits : IGFullPlaythrough::MaxBlockedWaits;
		bVerbosePlan = Waits == 0 || Waits + 1 >= MaxWaits;
		if (!PlanPath(Goal, Points, Why))
		{
			if (WaitAndRetry(Name, Waits, MakeGo(Goal, Tolerance, Name, Attempt, Waits + 1, Refuge), Why, Refuge))
			{
				return;
			}
			UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_PATH %s none why=%s from=%s goal=%s"), *Name, *Why,
				*Player->GetActorLocation().ToCompactString(), *Goal.ToCompactString());
			AbortReason = TEXT("path:") + Name;
			return;
		}
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_PATH %s points=%d attempt=%d"), *Name, Points.Num(), Attempt);
		if (Attempt + 1 < IGFullPlaythrough::MaxAttempts)
		{
			Retry = MakeGo(Goal, Tolerance, Name, Attempt + 1, 0, Refuge);
		}
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const bool bLast = Index + 1 == Points.Num();
			Route.Walk(Points[Index], bLast ? Tolerance : IGFullPlaythrough::PassTolerance, TEXT("walk:") + Name);
		}
	};
}

void AIGFullPlaythroughProbe::Follow(const TArray<FVector>& Points, const float Tolerance, const FString& Name)
{
	AddLeg(TEXT("follow:") + Name, MakeFollow(Points, Tolerance, Name, 0));
}

TFunction<void()> AIGFullPlaythroughProbe::MakeFollow(TArray<FVector> Points, float Tolerance, FString Name, int32 Attempt)
{
	return [this, Points, Tolerance, Name, Attempt]()
	{
		// 다시 할 때는 지금 발에서 가장 가까운 점부터 잇는다. 계단은 층마다 같은 자리가 겹치니 높이까지 본다.
		int32 First = 0;
		if (Attempt > 0)
		{
			const FVector Feet = Player->GetActorLocation() - FVector(0.0f, 0.0f, BodyHalfHeight);
			float Best = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const float Distance = static_cast<float>(FVector::Dist(Feet, Points[Index]));
				if (Distance < Best)
				{
					Best = Distance;
					First = Index;
				}
			}
		}
		if (Attempt + 1 < IGFullPlaythrough::MaxAttempts)
		{
			Retry = MakeFollow(Points, Tolerance, Name, Attempt + 1);
		}
		for (int32 Index = First; Index < Points.Num(); ++Index)
		{
			Route.Walk(Points[Index], Tolerance, TEXT("walk:") + Name);
		}
	};
}

void AIGFullPlaythroughProbe::TakeStairs(const int32 FromFloor, const int32 ToFloor)
{
	// 층 참마다 출입구로 나가지 않고 다음 계단으로 바로 꺾는다. 씬의 점은 출입구에서 시작해 다음 층 출입구에서
	// 끝나므로 가운데 층의 출입구 점만 뺀다.
	TArray<FVector> Points;
	Points.Add(AIGPrologueWorldScene::GetStairDoorwayFeet(FromFloor));
	const int32 Direction = ToFloor > FromFloor ? 1 : -1;
	for (int32 Floor = FromFloor; Floor != ToFloor; Floor += Direction)
	{
		TArray<FVector> Climb;
		AIGPrologueWorldScene::GetStairClimbFeet(Direction > 0 ? Floor : Floor - 1, Climb);
		if (Direction < 0)
		{
			Algo::Reverse(Climb);
		}
		for (int32 Index = 1; Index + 1 < Climb.Num(); ++Index)
		{
			Points.Add(Climb[Index]);
		}
	}
	Points.Add(AIGPrologueWorldScene::GetStairDoorwayFeet(ToFloor));
	Follow(Points, 26.0f, FString::Printf(TEXT("stairs_%d_to_%d"), FromFloor + 1, ToFloor + 1));
}

void AIGFullPlaythroughProbe::Use(const FString& Name, TFunction<AActor*()> FindTarget, const FVector& AimOffset,
	TFunction<bool()> Done, const float Timeout, TOptional<FVector> Stand, const int32 Presses)
{
	AddLeg(TEXT("use:") + Name, MakeUse(Name, MoveTemp(FindTarget), AimOffset, MoveTemp(Done), Timeout, Stand, Presses, 0));
}

void AIGFullPlaythroughProbe::UseNamed(const FString& Name, const FString& ActorName, const FVector& AimOffset,
	TFunction<bool()> Done, const float Timeout, TOptional<FVector> Stand, const int32 Presses)
{
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	Use(Name, [WeakThis, ActorName]() -> AActor*
	{
		return WeakThis.IsValid() ? WeakThis->FindNamed(ActorName) : nullptr;
	}, AimOffset, MoveTemp(Done), Timeout, Stand, Presses);
}

float AIGFullPlaythroughProbe::GetHoldSeconds(AActor* Target) const
{
	if (!Target || !Target->GetClass()->ImplementsInterface(UIGInteractable::StaticClass()))
	{
		return 0.0f;
	}
	float Hold = FMath::Max(0.0f, IIGInteractable::Execute_GetInteractionHoldDuration(Target, Player.Get()));
	if (Hold > KINDA_SMALL_NUMBER)
	{
		const UGameInstance* GameInstance = GetGameInstance();
		if (const UIGAccessibilitySubsystem* Accessibility =
			GameInstance ? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>() : nullptr)
		{
			Hold *= Accessibility->GetHoldDurationScale();
		}
	}
	return Hold;
}

TFunction<void()> AIGFullPlaythroughProbe::MakeUse(FString Name, TFunction<AActor*()> FindTarget, FVector AimOffset,
	TFunction<bool()> Done, float Timeout, TOptional<FVector> Stand, int32 Presses, int32 Attempt, int32 Waits)
{
	return [this, Name, FindTarget, AimOffset, Done, Timeout, Stand, Presses, Attempt, Waits]()
	{
		// 이미 된 일이면 넘긴다(앞 일을 하다 같이 됐거나, 다시 해 보는 사이에 됐다).
		if (Done && Done())
		{
			UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_CHECK %s PASS already"), *Name);
			return;
		}
		AActor* Target = FindTarget ? FindTarget() : nullptr;
		if (!Target)
		{
			AbortReason = TEXT("target:") + Name;
			return;
		}
		// 사람은 물건 몸통을 겨눈다. 원점은 바닥면이나 맞닿은 면에 있는 일이 많아 조준선이 조금만 흔들려도
		// 받침에 먼저 맞는다.
		FVector Origin = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		Target->GetActorBounds(false, Origin, Extent);
		const FVector Aim = (Extent.IsNearlyZero() ? Target->GetActorLocation() : Origin) + AimOffset;
		TArray<FVector> Path;
		if (Stand.IsSet())
		{
			Path.Add(Stand.GetValue());
		}
		else
		{
			TArray<FVector> Spots;
			FindStandSpots(Target, Aim, Spots);
			bVerbosePlan = Waits == 0 || Waits + 1 >= IGFullPlaythrough::MaxBlockedWaits;
			// 다시 할 때는 앞서 막힌 자리를 건너뛴다.
			for (int32 Index = FMath::Min(Attempt, FMath::Max(0, Spots.Num() - 1)); Index < Spots.Num() && Path.Num() == 0; ++Index)
			{
				FString Why;
				if (!PlanPath(Spots[Index], Path, Why))
				{
					Path.Reset();
				}
			}
			if (Path.Num() == 0)
			{
				const FString Why = FString::Printf(TEXT("spots=%d"), Spots.Num());
				if (WaitAndRetry(Name, Waits,
						MakeUse(Name, FindTarget, AimOffset, Done, Timeout, Stand, Presses, Attempt, Waits + 1), Why))
				{
					return;
				}
				UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_STAND %s none spots=%d target=%s aim=%s"), *Name, Spots.Num(),
					*Target->GetName(), *Aim.ToCompactString());
				AbortReason = TEXT("stand:") + Name;
				return;
			}
		}
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_STAND %s at=%s points=%d attempt=%d"), *Name,
			*Path.Last().ToCompactString(), Path.Num(), Attempt);
		if (Attempt + 1 < IGFullPlaythrough::MaxAttempts)
		{
			Retry = MakeUse(Name, FindTarget, AimOffset, Done, Timeout, Stand, Presses, Attempt + 1);
		}
		for (int32 Index = 0; Index < Path.Num(); ++Index)
		{
			const bool bLast = Index + 1 == Path.Num();
			Route.Walk(Path[Index], bLast ? 15.0f : IGFullPlaythrough::PassTolerance, TEXT("walk:") + Name);
		}
		QueuePress(Name, Target, Aim, GetHoldSeconds(Target), Done, Timeout, FMath::Max(1, Presses));
	};
}

void AIGFullPlaythroughProbe::QueuePress(const FString& Name, AActor* Target, const FVector& Aim, const float Hold,
	TFunction<bool()> Done, const float Timeout, const int32 Remaining)
{
	// 작은 물건(손전등 굵기 3 cm)은 1.2도만 비껴도 조준선이 뒤로 빠진다. 사람처럼 몸통 한가운데에 올 때까지
	// 맞춘다. 물건의 가장 얇은 쪽이 손 닿는 거리에서 차지하는 각도의 반쯤이다.
	float Tolerance = 1.2f;
	if (Target)
	{
		FVector Origin = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		Target->GetActorBounds(false, Origin, Extent);
		const float Thin = static_cast<float>(FMath::Max(1.0, Extent.GetMin()));
		Tolerance = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(Thin * 0.6f / 120.0f)), 0.3f, 1.2f);
	}
	// 홀드는 물건이 요구하는 시간보다 조금 더 누른다. 짧게 누르는 것은 한 번 탁 친다.
	Route.Aim(Aim, TEXT("aim:") + Name, Tolerance);
	Route.Press(Hold > 0.0f ? Hold + 0.25f : 0.15f, TEXT("press:") + Name, Target);
	if (Remaining <= 1)
	{
		// 확인할 일이 없는 누르기(벽에 귀 대기)는 누르고 끝낸다.
		if (Done)
		{
			Route.Until(Done, Timeout, TEXT("gate:") + Name);
		}
		return;
	}
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	TWeakObjectPtr<AActor> WeakTarget(Target);
	const TFunction<void()> RetryUse = Retry;
	Route.Wait(0.6f, TEXT("pause:") + Name);
	Route.Until([WeakThis, WeakTarget, Name, Aim, Hold, Done, Timeout, Remaining, RetryUse]()
	{
		AIGFullPlaythroughProbe* Self = WeakThis.Get();
		if (!Self || (Done && Done()))
		{
			return true;
		}
		Self->Legs.Insert(FLeg{TEXT("use:") + Name, [WeakThis, WeakTarget, Name, Aim, Hold, Done, Timeout, Remaining, RetryUse]()
		{
			if (AIGFullPlaythroughProbe* Probe = WeakThis.Get())
			{
				Probe->Retry = RetryUse;
				Probe->QueuePress(Name, WeakTarget.Get(), Aim, Hold, Done, Timeout, Remaining - 1);
			}
		}}, Self->LegIndex + 1);
		return true;
	}, 1.0f, TEXT("check:") + Name);
}

void AIGFullPlaythroughProbe::PrepareQuery()
{
	AIGPlayerCharacter* Character = Player.Get();
	BodyQuery = FCollisionQueryParams(SCENE_QUERY_STAT(FullPlaythroughBody), false, Character);
	// 잠기지 않은 문은 앞에서 E를 누르면 열린다. 열린 문짝은 몸과 부딪히지 않는다. 길을 찾을 때는 없는 셈 친다.
	for (TActorIterator<AIGSwingDoor> It(GetWorld()); It; ++It)
	{
		if (!It->IsLocked())
		{
			BodyQuery.AddIgnoredActor(*It);
		}
	}
	for (TActorIterator<AIGSlidingDoor> It(GetWorld()); It; ++It)
	{
		BodyQuery.AddIgnoredActor(*It);
	}
	if (const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr)
	{
		BodyChannel = Capsule->GetCollisionObjectType();
		BodyResponse = FCollisionResponseParams(Capsule->GetCollisionResponseToChannels());
	}
}

bool AIGFullPlaythroughProbe::ProbeFloor(const FVector2D& At, const float AroundZ, float& OutFloor) const
{
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, FVector(At, AroundZ + IGFullPlaythrough::FloorSearch),
			FVector(At, AroundZ - IGFullPlaythrough::FloorSearch), BodyChannel, BodyQuery, BodyResponse)
		|| Hit.ImpactNormal.Z < 0.7f)
	{
		return false;
	}
	OutFloor = static_cast<float>(Hit.ImpactPoint.Z);
	return true;
}

bool AIGFullPlaythroughProbe::IsBodyClear(const FVector2D& At, const float FloorZ, const float ExtraRadius) const
{
	// 발목 높이 아래는 넘을 수 있는 턱이라 보지 않는다. 머리 꼭대기는 서 있는 몸과 같다.
	const float Half = BodyHalfHeight - IGFullPlaythrough::StepLift * 0.5f;
	return !GetWorld()->OverlapBlockingTestByChannel(FVector(At, FloorZ + IGFullPlaythrough::StepLift + Half),
		FQuat::Identity, BodyChannel, FCollisionShape::MakeCapsule(BodyRadius + ExtraRadius, Half), BodyQuery,
		BodyResponse);
}

int32 AIGFullPlaythroughProbe::CellOf(const FVector2D& At) const
{
	const int32 X = FMath::FloorToInt((At.X - Grid.Origin.X) / IGFullPlaythrough::PathCell);
	const int32 Y = FMath::FloorToInt((At.Y - Grid.Origin.Y) / IGFullPlaythrough::PathCell);
	return X >= 0 && Y >= 0 && X < Grid.Width && Y < Grid.Height ? Y * Grid.Width + X : INDEX_NONE;
}

FVector2D AIGFullPlaythroughProbe::CellCenter(const int32 Index) const
{
	return Grid.Origin + FVector2D(Index % Grid.Width + 0.5f, Index / Grid.Width + 0.5f) * IGFullPlaythrough::PathCell;
}

uint8 AIGFullPlaythroughProbe::Evaluate(const int32 Index, const float AroundZ)
{
	if (!Grid.State.IsValidIndex(Index))
	{
		return 1;
	}
	if (Grid.State[Index] != 0)
	{
		return Grid.State[Index];
	}
	const FVector2D At = CellCenter(Index);
	float Floor = 0.0f;
	uint8 State = 1;
	if (ProbeFloor(At, AroundZ, Floor) && Floor >= Grid.BandMin && Floor <= Grid.BandMax
		&& IsBodyClear(At, Floor, IGFullPlaythrough::BodySkin))
	{
		State = 2;
		Grid.Floor[Index] = Floor;
		Grid.Tight[Index] = IsBodyClear(At, Floor, IGFullPlaythrough::RoomyGap) ? 0 : 1;
	}
	Grid.State[Index] = State;
	return State;
}

bool AIGFullPlaythroughProbe::IsStraightClear(const int32 From, const int32 To)
{
	// 두 칸 사이를 반 칸씩 짚어 본다. 가운데 칸은 벽에서 넉넉히 떨어져 있어야 곧게 걷는다.
	const FVector2D Start = CellCenter(From);
	const FVector2D End = CellCenter(To);
	const int32 Samples = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(Start, End) / (IGFullPlaythrough::PathCell * 0.5f)));
	float Previous = Grid.Floor[From];
	for (int32 Sample = 1; Sample < Samples; ++Sample)
	{
		const int32 Cell = CellOf(FMath::Lerp(Start, End, static_cast<float>(Sample) / Samples));
		if (Cell == INDEX_NONE || Evaluate(Cell, Previous) != 2 || Grid.Tight[Cell]
			|| FMath::Abs(Grid.Floor[Cell] - Previous) > IGFullPlaythrough::StepRise)
		{
			return false;
		}
		Previous = Grid.Floor[Cell];
	}
	return FMath::Abs(Grid.Floor[To] - Previous) <= IGFullPlaythrough::StepRise;
}

FString AIGFullPlaythroughProbe::DescribeBlockers(const TArray<uint8>& Closed, const FVector2D& Goal)
{
	// 목표에 가장 가까운 가장자리 칸들은 따로 하나씩 적는다. 거기가 막힌 앞이다.
	if (bVerbosePlan)
	{
		struct FEdge
		{
			int32 Cell;
			int32 Parent;
			float Distance;
		};
		TArray<FEdge> Edges;
		for (int32 Index = 0; Index < Closed.Num(); ++Index)
		{
			if (!Closed[Index])
			{
				continue;
			}
			const int32 X = Index % Grid.Width;
			const int32 Y = Index / Grid.Width;
			for (const FIntPoint Side : {FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)})
			{
				const int32 NX = X + Side.X;
				const int32 NY = Y + Side.Y;
				if (NX >= 0 && NY >= 0 && NX < Grid.Width && NY < Grid.Height && Grid.State[NY * Grid.Width + NX] == 1)
				{
					const int32 Cell = NY * Grid.Width + NX;
					Edges.Add(FEdge{Cell, Index, static_cast<float>(FVector2D::Distance(CellCenter(Cell), Goal))});
				}
			}
		}
		Edges.Sort([](const FEdge& A, const FEdge& B) { return A.Distance < B.Distance; });
		const float Half = BodyHalfHeight - IGFullPlaythrough::StepLift * 0.5f;
		for (int32 Index = 0; Index < FMath::Min(5, Edges.Num()); ++Index)
		{
			const FVector2D At = CellCenter(Edges[Index].Cell);
			float Floor = 0.0f;
			FString What;
			if (!ProbeFloor(At, Grid.Floor[Edges[Index].Parent], Floor))
			{
				What = TEXT("no_floor");
			}
			else
			{
				What = FString::Printf(TEXT("floor=%.1f parent=%.1f"), Floor, Grid.Floor[Edges[Index].Parent]);
				TArray<FOverlapResult> Overlaps;
				GetWorld()->OverlapMultiByChannel(Overlaps, FVector(At, Floor + IGFullPlaythrough::StepLift + Half),
					FQuat::Identity, BodyChannel, FCollisionShape::MakeCapsule(BodyRadius + IGFullPlaythrough::BodySkin, Half),
					BodyQuery, BodyResponse);
				for (const FOverlapResult& Overlap : Overlaps)
				{
					if (Overlap.bBlockingHit && Overlap.GetComponent())
					{
						What += FString::Printf(TEXT(" %s.%s(%s)"), *GetNameSafe(Overlap.GetActor()), *Overlap.GetComponent()->GetName(),
							*Overlap.GetComponent()->Bounds.Origin.ToCompactString());
					}
				}
			}
			UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_EDGE (%.0f,%.0f) %s"), At.X, At.Y, *What);
		}
	}
	TMap<FString, int32> Counts;
	TMap<FString, FVector2D> FirstAt;
	int32 Sampled = 0;
	for (int32 Index = 0; Index < Closed.Num() && Sampled < 160; ++Index)
	{
		if (!Closed[Index])
		{
			continue;
		}
		const int32 X = Index % Grid.Width;
		const int32 Y = Index / Grid.Width;
		const int32 Sides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
		for (const int32 (&Side)[2] : Sides)
		{
			const int32 NX = X + Side[0];
			const int32 NY = Y + Side[1];
			if (NX < 0 || NY < 0 || NX >= Grid.Width || NY >= Grid.Height || Grid.State[NY * Grid.Width + NX] != 1)
			{
				continue;
			}
			++Sampled;
			const FVector2D At = CellCenter(NY * Grid.Width + NX);
			float Floor = 0.0f;
			if (!ProbeFloor(At, Grid.Floor[Index], Floor))
			{
				Counts.FindOrAdd(TEXT("no_floor"))++;
				continue;
			}
			if (Floor < Grid.BandMin || Floor > Grid.BandMax)
			{
				Counts.FindOrAdd(FString::Printf(TEXT("floor_%.0f"), Floor))++;
				continue;
			}
			const float Half = BodyHalfHeight - IGFullPlaythrough::StepLift * 0.5f;
			TArray<FOverlapResult> Overlaps;
			GetWorld()->OverlapMultiByChannel(Overlaps, FVector(At, Floor + IGFullPlaythrough::StepLift + Half), FQuat::Identity,
				BodyChannel, FCollisionShape::MakeCapsule(BodyRadius + IGFullPlaythrough::BodySkin, Half), BodyQuery, BodyResponse);
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (Overlap.bBlockingHit)
				{
					const FString Key = GetNameSafe(Overlap.GetActor()) + TEXT(".") + GetNameSafe(Overlap.GetComponent());
					Counts.FindOrAdd(Key)++;
					FirstAt.FindOrAdd(Key, At);
				}
			}
		}
	}
	// 닿은 칸 둘레를 글자 지도로 남긴다. o 닿은 칸, . 설 수 있지만 못 닿은 칸, # 막힘, 빈칸은 안 본 칸.
	int32 MinX = Grid.Width, MinY = Grid.Height, MaxX = -1, MaxY = -1;
	for (int32 Index = 0; Index < Closed.Num(); ++Index)
	{
		if (Closed[Index])
		{
			MinX = FMath::Min(MinX, Index % Grid.Width);
			MaxX = FMath::Max(MaxX, Index % Grid.Width);
			MinY = FMath::Min(MinY, Index / Grid.Width);
			MaxY = FMath::Max(MaxY, Index / Grid.Width);
		}
	}
	if (MaxX >= 0 && bVerbosePlan)
	{
		MinX = FMath::Max(0, MinX - 8);
		MinY = FMath::Max(0, MinY - 8);
		MaxX = FMath::Min(Grid.Width - 1, MaxX + 8);
		MaxY = FMath::Min(Grid.Height - 1, MaxY + 8);
		const FVector2D Corner = CellCenter(MinY * Grid.Width + MinX);
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_MAP origin=(%.0f,%.0f) cell=%.0f rows=+Y"), Corner.X, Corner.Y,
			IGFullPlaythrough::PathCell);
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			FString Row;
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const int32 Index = Y * Grid.Width + X;
				Row.AppendChar(Closed[Index] ? TEXT('o') : Grid.State[Index] == 2 ? TEXT('.') : Grid.State[Index] == 1 ? TEXT('#') : TEXT(' '));
			}
			UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_MAP %4.0f %s"), CellCenter(Y * Grid.Width + MinX).Y, *Row);
		}
	}
	Counts.ValueSort([](const int32 A, const int32 B) { return A > B; });
	FString Out;
	int32 Listed = 0;
	for (const TPair<FString, int32>& Pair : Counts)
	{
		if (Listed++ == 8)
		{
			break;
		}
		const FVector2D* At = FirstAt.Find(Pair.Key);
		Out += FString::Printf(TEXT("%s%s:%d"), Out.IsEmpty() ? TEXT("") : TEXT(","), *Pair.Key, Pair.Value);
		if (At)
		{
			Out += FString::Printf(TEXT("@(%.0f,%.0f)"), At->X, At->Y);
		}
	}
	return Out;
}

bool AIGFullPlaythroughProbe::PlanPath(const FVector& Goal, TArray<FVector>& OutPoints, FString& OutWhy)
{
	using namespace IGFullPlaythrough;
	OutPoints.Reset();
	AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		OutWhy = TEXT("no_player");
		return false;
	}
	PrepareQuery();
	const FVector From = Character->GetActorLocation();
	const float FromFloor = static_cast<float>(From.Z) - BodyHalfHeight;
	const FVector2D Min(FMath::Min(From.X, Goal.X) - PathMargin, FMath::Min(From.Y, Goal.Y) - PathMargin);
	const FVector2D Max(FMath::Max(From.X, Goal.X) + PathMargin, FMath::Max(From.Y, Goal.Y) + PathMargin);
	Grid.Origin = Min;
	Grid.Width = FMath::CeilToInt((Max.X - Min.X) / PathCell);
	Grid.Height = FMath::CeilToInt((Max.Y - Min.Y) / PathCell);
	if (Grid.Width > MaxPathSide || Grid.Height > MaxPathSide)
	{
		OutWhy = TEXT("too_far");
		return false;
	}
	Grid.BandMin = FMath::Min(FromFloor, Goal.Z) - FloorBand;
	Grid.BandMax = FMath::Max(FromFloor, Goal.Z) + FloorBand;
	const int32 Count = Grid.Width * Grid.Height;
	Grid.State.Init(0, Count);
	Grid.Tight.Init(0, Count);
	Grid.Floor.Init(0.0f, Count);

	// 벽에 바짝 서 있거나 목표가 가구에 붙어 있으면 가까운 설 수 있는 칸에서 잇는다.
	const auto Snap = [this](const FVector2D& At, const float AroundZ, const int32 Reach) -> int32
	{
		const int32 Exact = CellOf(At);
		if (Exact != INDEX_NONE && Evaluate(Exact, AroundZ) == 2)
		{
			return Exact;
		}
		int32 Best = INDEX_NONE;
		float BestDistance = TNumericLimits<float>::Max();
		for (int32 DY = -Reach; DY <= Reach; ++DY)
		{
			for (int32 DX = -Reach; DX <= Reach; ++DX)
			{
				const int32 Cell = CellOf(At + FVector2D(DX, DY) * PathCell);
				if (Cell != INDEX_NONE && Evaluate(Cell, AroundZ) == 2)
				{
					const float Distance = static_cast<float>(FVector2D::Distance(At, CellCenter(Cell)));
					if (Distance < BestDistance)
					{
						BestDistance = Distance;
						Best = Cell;
					}
				}
			}
		}
		return Best;
	};
	const int32 Start = Snap(FVector2D(From), FromFloor, 4);
	const int32 GoalCell = Snap(FVector2D(Goal), Goal.Z, 6);
	if (Start == INDEX_NONE || GoalCell == INDEX_NONE)
	{
		OutWhy = Start == INDEX_NONE ? TEXT("start_blocked") : TEXT("goal_blocked");
		return false;
	}

	struct FOpen
	{
		float Score;
		int32 Index;
	};
	const auto Less = [](const FOpen& A, const FOpen& B) { return A.Score < B.Score; };
	const FVector2D GoalAt = CellCenter(GoalCell);
	TArray<float> Cost;
	Cost.Init(TNumericLimits<float>::Max(), Count);
	TArray<int32> Parent;
	Parent.Init(INDEX_NONE, Count);
	TArray<uint8> Closed;
	Closed.Init(0, Count);
	TArray<FOpen> Open;
	Cost[Start] = 0.0f;
	Open.HeapPush(FOpen{static_cast<float>(FVector2D::Distance(CellCenter(Start), GoalAt)), Start}, Less);
	int32 Expanded = 0;
	bool bFound = false;
	while (Open.Num() > 0 && Expanded < MaxPathExpansions)
	{
		FOpen Top;
		Open.HeapPop(Top, Less);
		if (Closed[Top.Index])
		{
			continue;
		}
		Closed[Top.Index] = 1;
		++Expanded;
		if (Top.Index == GoalCell)
		{
			bFound = true;
			break;
		}
		const int32 X = Top.Index % Grid.Width;
		const int32 Y = Top.Index / Grid.Width;
		const float HereFloor = Grid.Floor[Top.Index];
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const int32 NX = X + DX;
				const int32 NY = Y + DY;
				if ((DX == 0 && DY == 0) || NX < 0 || NY < 0 || NX >= Grid.Width || NY >= Grid.Height)
				{
					continue;
				}
				const int32 Next = NY * Grid.Width + NX;
				if (Closed[Next] || Evaluate(Next, HereFloor) != 2
					|| FMath::Abs(Grid.Floor[Next] - HereFloor) > StepRise)
				{
					continue;
				}
				// 대각선은 양옆 칸이 다 비어 있을 때만 간다. 모서리를 깎으며 지나가지 않는다.
				if (DX != 0 && DY != 0
					&& (Evaluate(Y * Grid.Width + NX, HereFloor) != 2 || Evaluate(NY * Grid.Width + X, HereFloor) != 2))
				{
					continue;
				}
				const float Step = (DX != 0 && DY != 0 ? UE_SQRT_2 : 1.0f) * PathCell * (Grid.Tight[Next] ? TightCost : 1.0f);
				const float NewCost = Cost[Top.Index] + Step;
				if (NewCost < Cost[Next])
				{
					Cost[Next] = NewCost;
					Parent[Next] = Top.Index;
					Open.HeapPush(FOpen{NewCost + static_cast<float>(FVector2D::Distance(CellCenter(Next), GoalAt)), Next}, Less);
				}
			}
		}
	}
	if (!bFound)
	{
		OutWhy = FString::Printf(TEXT("no_route expanded=%d blockers=%s"), Expanded, *DescribeBlockers(Closed, FVector2D(Goal)));
		return false;
	}
	TArray<int32> Cells;
	for (int32 At = GoalCell; At != INDEX_NONE; At = Parent[At])
	{
		Cells.Add(At);
	}
	Algo::Reverse(Cells);

	// 곧게 갈 수 있는 데까지 한 번에 간다. 좁은 문간은 격자 길을 그대로 밟는다.
	const auto Feet = [this](const int32 Cell)
	{
		return FVector(CellCenter(Cell), Grid.Floor[Cell]);
	};
	int32 Here = 0;
	while (Here + 1 < Cells.Num())
	{
		int32 Next = Here + 1;
		for (int32 Far = FMath::Min(Cells.Num() - 1, Here + 120); Far > Here + 1; --Far)
		{
			if (IsStraightClear(Cells[Here], Cells[Far]))
			{
				Next = Far;
				break;
			}
		}
		const FVector Point = Feet(Cells[Next]);
		if (OutPoints.Num() > 0 && Next + 1 < Cells.Num() && FVector::Dist2D(OutPoints.Last(), Point) < 30.0f)
		{
			Here = Next;
			continue;
		}
		OutPoints.Add(Point);
		Here = Next;
	}
	if (OutPoints.Num() == 0)
	{
		OutPoints.Add(Feet(GoalCell));
	}
	// 목표 칸이 그대로 설 수 있는 칸이면 칸 가운데가 아니라 목표에 선다.
	if (GoalCell == CellOf(FVector2D(Goal)))
	{
		OutPoints.Last() = FVector(Goal.X, Goal.Y, OutPoints.Last().Z);
	}
	return true;
}

void AIGFullPlaythroughProbe::FindStandSpots(const AActor* Target, const FVector& Aim, TArray<FVector>& OutSpots)
{
	using namespace IGFullPlaythrough;
	OutSpots.Reset();
	const AIGPlayerCharacter* Character = Player.Get();
	if (!Character || !Target)
	{
		return;
	}
	PrepareQuery();
	const FVector From = Character->GetActorLocation();
	const float FromFloor = static_cast<float>(From.Z) - BodyHalfHeight;
	FCollisionQueryParams Sight(SCENE_QUERY_STAT(FullPlaythroughSight), false, Character);
	struct FSpot
	{
		FVector Feet;
		float Score;
	};
	TArray<FSpot> Spots;
	for (const float Radius : StandRadii)
	{
		for (int32 Direction = 0; Direction < StandDirections; ++Direction)
		{
			const float Angle = 2.0f * UE_PI * Direction / StandDirections;
			const FVector2D At = FVector2D(Aim) + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
			float Floor = 0.0f;
			if (!ProbeFloor(At, FromFloor, Floor) || FMath::Abs(Floor - FromFloor) > StandFloorRange
				|| !IsBodyClear(At, Floor, BodySkin + 4.0f))
			{
				continue;
			}
			// 서서 겨눈 눈길이 다른 데 걸리지 않고 그 물건에 먼저 닿아야 한다. 손이 닿는 거리 안이어야 한다.
			const FVector Eye(At, Floor + BodyHalfHeight + EyeOffset);
			if (FVector::Dist(Eye, Aim) > ReachDistance - 30.0f)
			{
				continue;
			}
			FHitResult Hit;
			const FVector End = Aim + (Aim - Eye).GetSafeNormal() * 6.0f;
			if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Sight) || Hit.GetActor() != Target)
			{
				continue;
			}
			Spots.Add(FSpot{FVector(At, Floor), static_cast<float>(FVector2D::Distance(FVector2D(From), At))
				+ FMath::Abs(Radius - 100.0f) * 0.5f});
		}
	}
	Spots.Sort([](const FSpot& A, const FSpot& B) { return A.Score < B.Score; });
	for (const FSpot& Spot : Spots)
	{
		OutSpots.Add(Spot.Feet);
	}
}

void AIGFullPlaythroughProbe::BuildArrival()
{
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	const auto Beat = [WeakThis](const TCHAR* Name) -> TFunction<bool()>
	{
		const FString Id(Name);
		return [WeakThis, Id]() { return WeakThis.IsValid() && WeakThis->HasBeat(*Id); };
	};
	const float FourthZ = AIGPrologueWorldScene::GetStoreyFloorZ(3);

	// 입주 저녁. 게임이 띄우는 차례대로 한다: 계약서, 짐 상자 셋, 편의점, 401·402호, 옥상 문, 침대.
	Stage(TEXT("arrival_room"));
	WaitSeconds(3.0f, TEXT("opening"));
	UseNamed(TEXT("contract"), TEXT("MissingFloorArrivalContract"), FVector::ZeroVector,
		Beat(TEXT("Arrival.Contract")), 4.0f);
	UseNamed(TEXT("parcel_box"), TEXT("MissingFloorArrivalParcelBox"), FVector::ZeroVector,
		Beat(TEXT("Arrival.Box.Parcel")), 4.0f);
	UseNamed(TEXT("voicemail_box"), TEXT("MissingFloorArrivalVoicemailBox"), FVector::ZeroVector,
		Beat(TEXT("Arrival.Box.Voicemail")), 4.0f);
	UseNamed(TEXT("notebook_box"), TEXT("MissingFloorArrivalNotebookBox"), FVector::ZeroVector,
		Beat(TEXT("Arrival.Box.Notebook")), 4.0f);
	Listen(TEXT("room_thoughts"), 15.0f);
	// 신발장 위 손전등. 진행 조건은 아니지만 사람은 나가기 전에 챙긴다.
	Use(TEXT("flashlight"), [WeakThis]() -> AActor*
	{
		return WeakThis.IsValid() ? WeakThis->FindFirst<AIGFlashlightPickup>() : nullptr;
	}, FVector::ZeroVector, [WeakThis]()
	{
		const AIGPlayerCharacter* Character = WeakThis.IsValid() ? WeakThis->Player.Get() : nullptr;
		return Character && Character->GetFlashlight() && Character->GetFlashlight()->IsAvailable();
	}, 3.0f);

	Stage(TEXT("arrival_store"));
	GoTo(FVector(131.0f, -305.0f, FourthZ), 30.0f, TEXT("home_door_out"));
	GoTo(AIGPrologueWorldScene::GetStairDoorwayFeet(3), 28.0f, TEXT("fourth_stair_door"));
	TakeStairs(3, 0);
	GoTo(FVector(-45.0f, -305.0f, 0.0f), 30.0f, TEXT("ground_connector"));
	GoTo(FVector(560.0f, -305.0f, 0.0f), 30.0f, TEXT("lobby"));
	GoTo(FVector(646.0f, -470.0f, 0.0f), 30.0f, TEXT("building_door_out"));
	GoTo(FVector(2250.0f, -457.0f, 0.0f), 35.0f, TEXT("alley_to_store"));
	GoTo(FVector(2480.0f, -457.0f, 0.0f), 30.0f, TEXT("store_door_in"));
	UseNamed(TEXT("store_bell"), TEXT("MissingFloorArrivalStoreBell"), FVector::ZeroVector,
		Beat(TEXT("Arrival.Store")), 4.0f);
	Listen(TEXT("narin_answer"), 40.0f);

	Stage(TEXT("arrival_neighbors"));
	GoTo(FVector(2480.0f, -457.0f, 0.0f), 30.0f, TEXT("store_door_out"));
	GoTo(FVector(2250.0f, -457.0f, 0.0f), 35.0f, TEXT("alley_west"));
	GoTo(FVector(646.0f, -470.0f, 0.0f), 30.0f, TEXT("porch"));
	GoTo(FVector(646.0f, -320.0f, 0.0f), 30.0f, TEXT("building_door_in"));
	GoTo(FVector(-45.0f, -305.0f, 0.0f), 30.0f, TEXT("connector_west"));
	GoTo(AIGPrologueWorldScene::GetStairDoorwayFeet(0), 28.0f, TEXT("ground_stair_door"));
	TakeStairs(0, 3);
	UseNamed(TEXT("unit402_note"), TEXT("MissingFloorArrivalUnit402Note"), FVector::ZeroVector,
		Beat(TEXT("Arrival.Unit402")), 4.0f);
	UseNamed(TEXT("unit401_knock"), TEXT("MissingFloorUnit401Door"), FVector::ZeroVector,
		Beat(TEXT("Arrival.Unit401")), 4.0f);
	Listen(TEXT("hwang_through_door"), 40.0f);

	Stage(TEXT("arrival_roof"));
	Follow(IGFullPlaythrough::RoofStairUp(), 22.0f, TEXT("roof_stair_up"));
	// 철문 아래 디딤판. 자물쇠(-242.5, 215.5, 1290)가 눈앞이다.
	UseNamed(TEXT("roof_lock"), TEXT("MissingFloorArrivalRoofLock"), FVector::ZeroVector,
		Beat(TEXT("Arrival.RoofDoor")), 4.0f, FVector(-265.0f, 160.0f, FourthZ + 265.0f));
	WaitUntil(Beat(TEXT("Arrival.Complete")), 5.0f, TEXT("arrival_complete"));
	Listen(TEXT("roof_thoughts"), 20.0f);

	Stage(TEXT("arrival_bed"));
	{
		TArray<FVector> Down = IGFullPlaythrough::RoofStairUp();
		Algo::Reverse(Down);
		Follow(Down, 22.0f, TEXT("roof_stair_down"));
	}
	GoTo(FVector(131.0f, -305.0f, FourthZ), 30.0f, TEXT("home_door_front"));
	UseNamed(TEXT("sleep"), TEXT("MissingFloorSleepTarget"), FVector::ZeroVector, [WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		const AIGNightPhaseDirector* Phase = WeakThis.IsValid() ? WeakThis->NightPhase.Get() : nullptr;
		return Narrative && Phase && Narrative->GetNightIndex() == 1 && Phase->IsHourActive();
	}, 12.0f);
}

void AIGFullPlaythroughProbe::BuildNightOne()
{
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	const auto Beat = [WeakThis](const TCHAR* Name) -> TFunction<bool()>
	{
		const FString Id(Name);
		return [WeakThis, Id]() { return WeakThis.IsValid() && WeakThis->HasBeat(*Id); };
	};
	const float FourthZ = AIGPrologueWorldScene::GetStoreyFloorZ(3);

	// 첫째 밤. 알람과 천장 노크, 손전등. 1층 계량기함에서 공용 조명을 내리고 이름 없는 회로를 올렸다
	// 내렸다 하며 계량기를 견주고, 검침표를 읽는다. 그다음 4층 천장에 귀를 대 보고, 조용해지면 쉰다.
	Stage(TEXT("night1_awake"));
	WaitSeconds(8.0f, TEXT("night1_opening"));
	Listen(TEXT("night1_opening"), 25.0f);
	Stage(TEXT("night1_lobby"));
	// 복도에 위층 사람이 있으면 현관 안에서 지나가기를 기다렸다가 나선다. 밤에는 엘리베이터가 서 있다.
	const FVector HomeEntry(131.0f, -160.0f, FourthZ);
	GoTo(AIGPrologueWorldScene::GetStairDoorwayFeet(3), 28.0f, TEXT("night1_to_stairs"), HomeEntry);
	TakeStairs(3, 0);
	GoTo(FVector(-45.0f, -305.0f, 0.0f), 30.0f, TEXT("night1_ground_connector"));
	GoTo(FVector(470.0f, -300.0f, 0.0f), 30.0f, TEXT("night1_meter_wall"));
	UseNamed(TEXT("common_lights_off"), TEXT("MissingFloorCommonLighting"), FVector::ZeroVector, [WeakThis]()
	{
		const AIGPrologueWorldScene* Scene = WeakThis.IsValid() ? WeakThis->FindFirst<AIGPrologueWorldScene>() : nullptr;
		return Scene && !Scene->AreCommonInspectionLightsEnabled();
	}, 4.0f);
	UseNamed(TEXT("meter_unpowered"), TEXT("MissingFloorMeterDial"), FVector::ZeroVector,
		Beat(TEXT("P1.Isolation.Unpowered")), 5.0f);
	UseNamed(TEXT("breaker_up"), TEXT("MissingFloorBreaker"), FVector::ZeroVector, [WeakThis]()
	{
		const AIGMissingFloorPuzzleOneDirector* Puzzle =
			WeakThis.IsValid() ? WeakThis->FindFirst<AIGMissingFloorPuzzleOneDirector>() : nullptr;
		return Puzzle && Puzzle->IsBreakerThrownForTesting();
	}, 4.0f);
	UseNamed(TEXT("meter_powered"), TEXT("MissingFloorMeterDial"), FVector::ZeroVector,
		Beat(TEXT("P1.Isolation.Powered")), 5.0f);
	UseNamed(TEXT("reading_sheet"), TEXT("MissingFloorReadingSheet"), FVector::ZeroVector, [WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return Narrative && Narrative->HasSource(EIGMissingFloorTruth::LivedUpstairs, EIGMissingFloorSource::MeterReadingSheet);
	}, 4.0f);
	WaitSeconds(4.0f, TEXT("reading_sheet"));
	PressKey(TEXT("close_sheet"), EKeys::E, 0.15f);
	WaitUntil([WeakThis]()
	{
		return WeakThis.IsValid() && WeakThis->NightPhase.IsValid() && WeakThis->NightPhase->IsGoalComplete();
	}, 10.0f, TEXT("night1_goal"));
	Listen(TEXT("night1_realization"), 25.0f);

	Stage(TEXT("night1_ceiling"));
	GoTo(FVector(-45.0f, -305.0f, 0.0f), 30.0f, TEXT("night1_back_to_stairs"));
	GoTo(AIGPrologueWorldScene::GetStairDoorwayFeet(0), 28.0f, TEXT("night1_ground_stair_door"));
	TakeStairs(0, 3);
	UseNamed(TEXT("ceiling_listen"), TEXT("MissingFloorNightOneCeilingListen"), FVector::ZeroVector,
		Beat(TEXT("Night1.AboveConfirmed")), 4.0f);
	Listen(TEXT("night1_above"), 15.0f);

	// 위에서 내려온 사람이 복도를 뒤지는 동안은 눕지 못한다. 방에 들어가 조용해지기를 기다린다.
	Stage(TEXT("night1_rest"));
	GoTo(FVector(131.0f, -160.0f, FourthZ), 30.0f, TEXT("night1_home"), FVector(-200.0f, -305.0f, FourthZ));
	WaitUntil([WeakThis]() { return WeakThis.IsValid() && !WeakThis->IsRestBlocked(); }, 120.0f, TEXT("night1_quiet"));
	UseNamed(TEXT("night1_bed"), TEXT("MissingFloorSleepTarget"), FVector::ZeroVector, [WeakThis]()
	{
		const AIGNightPhaseDirector* Phase = WeakThis.IsValid() ? WeakThis->NightPhase.Get() : nullptr;
		return Phase && (!Phase->IsHourActive() || Phase->IsDawnTransitionInProgress());
	}, 8.0f);
	WaitUntil([WeakThis]()
	{
		const AIGNightPhaseDirector* Phase = WeakThis.IsValid() ? WeakThis->NightPhase.Get() : nullptr;
		return Phase && !Phase->IsHourActive() && !Phase->IsDawnTransitionInProgress();
	}, 60.0f, TEXT("night1_dawn"));
	WaitSeconds(4.0f, TEXT("day1_wake"));
}

void AIGFullPlaythroughProbe::BuildDayOne()
{
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	const float FourthZ = AIGPrologueWorldScene::GetStoreyFloorZ(3);

	// 첫째 낮. 목표 줄대로 401호 할머니를 찾아가 문을 두드리고, 이야기를 다 듣고 403호로 돌아가 잔다.
	Stage(TEXT("day1_morning"));
	Listen(TEXT("day1_morning"), 30.0f);
	GoTo(FVector(131.0f, -305.0f, FourthZ), 30.0f, TEXT("day1_home_door_out"));
	const FName TalkBeat = AIGNightPhaseDirector::DayConversationBeatId(1);
	UseNamed(TEXT("day1_knock_401"), TEXT("MissingFloorUnit401Door"), FVector::ZeroVector, [WeakThis, TalkBeat]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return Narrative && Narrative->HasBeatPlayed(TalkBeat);
	}, 6.0f);
	Listen(TEXT("day1_hwang"), 60.0f);
	Stage(TEXT("day1_bed"));
	GoTo(FVector(131.0f, -160.0f, FourthZ), 30.0f, TEXT("day1_home"));
	UseNamed(TEXT("day1_sleep"), TEXT("MissingFloorSleepTarget"), FVector::ZeroVector, [WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		const AIGNightPhaseDirector* Phase = WeakThis.IsValid() ? WeakThis->NightPhase.Get() : nullptr;
		return Narrative && Phase && Narrative->GetNightIndex() == 2 && Phase->IsHourActive();
	}, 12.0f);
	WaitSeconds(5.0f, TEXT("night2_fade"));
}

void AIGFullPlaythroughProbe::BuildNightTwo()
{
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	const auto Beat = [WeakThis](const TCHAR* Name) -> TFunction<bool()>
	{
		const FString Id(Name);
		return [WeakThis, Id]() { return WeakThis.IsValid() && WeakThis->HasBeat(*Id); };
	};
	const auto DoorStage = [WeakThis](const EIGNightTwoBeatStage AtLeast) -> TFunction<bool()>
	{
		return [WeakThis, AtLeast]()
		{
			const AIGMissingFloorNightTwoBeatDirector* Beats =
				WeakThis.IsValid() ? WeakThis->FindFirst<AIGMissingFloorNightTwoBeatDirector>() : nullptr;
			return Beats && static_cast<uint8>(Beats->GetStage()) >= static_cast<uint8>(AtLeast);
		};
	};
	const float FourthZ = AIGPrologueWorldScene::GetStoreyFloorZ(3);

	// 둘째 밤. 현관문 노크에 문구멍으로 내다보고 바닥의 폰으로 녹음을 건다. 그가 물러가면 1층 관리실에서
	// 먹지를 문질러 원래 접수 기록을 살리고 부동산 문자 사본을 읽는다. 안쪽 방에서 관리인이 깨면 책상 밑에
	// 숨었다가, 심부름에서 돌아와 다시 누우면 나와서 403호로 돌아간다.
	Stage(TEXT("night2_awake"));
	WaitUntil(DoorStage(EIGNightTwoBeatStage::AwaitingPeephole), 20.0f, TEXT("night2_first_knock"));
	UseNamed(TEXT("night2_peephole"), TEXT("MissingFloorNightTwoPeephole"), FVector::ZeroVector,
		DoorStage(EIGNightTwoBeatStage::AwaitingPhone), 6.0f);
	Listen(TEXT("night2_peephole"), 15.0f);
	UseNamed(TEXT("night2_phone"), TEXT("MissingFloorPhoneRecorder"), FVector::ZeroVector,
		DoorStage(EIGNightTwoBeatStage::Answering), 6.0f);
	WaitUntil(DoorStage(EIGNightTwoBeatStage::Spent), 40.0f, TEXT("night2_door_answered"));
	Listen(TEXT("night2_after_door"), 20.0f);

	Stage(TEXT("night2_booth"));
	GoTo(AIGPrologueWorldScene::GetStairDoorwayFeet(3), 28.0f, TEXT("night2_to_stairs"), FVector(131.0f, -160.0f, FourthZ));
	TakeStairs(3, 0);
	GoTo(FVector(-45.0f, -305.0f, 0.0f), 30.0f, TEXT("night2_ground_connector"));
	GoTo(FVector(165.0f, -300.0f, 0.0f), 30.0f, TEXT("night2_booth_front"));
	UseNamed(TEXT("night2_carbon"), TEXT("MissingFloorCarbonLedger"), FVector::ZeroVector, [WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return Narrative && Narrative->HasSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::CarbonLedgerOriginal);
	}, 5.0f, TOptional<FVector>(), 4);
	Listen(TEXT("night2_carbon"), 20.0f);
	UseNamed(TEXT("night2_agent_note"), TEXT("MissingFloorAgentNote"), FVector::ZeroVector, [WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return Narrative && Narrative->HasSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::AgentMoveOutMessage);
	}, 4.0f);
	WaitSeconds(4.0f, TEXT("night2_reading_note"));
	PressKey(TEXT("night2_close_note"), EKeys::E, 0.15f);
	WaitUntil([WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return Narrative && Narrative->IsPuzzleSolved(FName(TEXT("P2")));
	}, 10.0f, TEXT("night2_p2"));
	// 「아저씨가 깼어. 숨어야 해.」 책상 밑으로.
	Use(TEXT("night2_hide"), [WeakThis]() -> AActor*
	{
		if (!WeakThis.IsValid())
		{
			return nullptr;
		}
		for (TActorIterator<AIGHidingSpot> It(WeakThis->GetWorld()); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), FVector(133.0f, -137.5f, 0.0f)) < 40.0f)
			{
				return *It;
			}
		}
		return nullptr;
	}, FVector::ZeroVector, [WeakThis]()
	{
		const AIGPlayerCharacter* Character = WeakThis.IsValid() ? WeakThis->Player.Get() : nullptr;
		return Character && Character->IsInHidingSpot();
	}, 6.0f);
	WaitUntil(Beat(TEXT("Leftover.N2.Errand")), 150.0f, TEXT("night2_errand_over"));
	WaitSeconds(3.0f, TEXT("night2_settle"));
	PressKey(TEXT("night2_leave_hiding"), EKeys::E, 0.15f);
	WaitUntil([WeakThis]()
	{
		const AIGPlayerCharacter* Character = WeakThis.IsValid() ? WeakThis->Player.Get() : nullptr;
		return Character && !Character->IsInHidingSpot();
	}, 6.0f, TEXT("night2_out_of_hiding"));
	StandUp(TEXT("night2_stand"));

	Stage(TEXT("night2_return"));
	GoTo(FVector(165.0f, -300.0f, 0.0f), 30.0f, TEXT("night2_booth_out"));
	GoTo(FVector(-45.0f, -305.0f, 0.0f), 30.0f, TEXT("night2_back_to_stairs"));
	GoTo(AIGPrologueWorldScene::GetStairDoorwayFeet(0), 28.0f, TEXT("night2_ground_stair_door"));
	TakeStairs(0, 3);
	// 위층 사람이 집 문 앞에 있으면 복도 서쪽으로 물러나 기다렸다가 들어간다. 곁에 서 있으면 심장 소리가 그를 붙든다.
	GoTo(FVector(131.0f, -160.0f, FourthZ), 30.0f, TEXT("night2_home"), FVector(-200.0f, -305.0f, FourthZ));
	WaitUntil([WeakThis]()
	{
		return WeakThis.IsValid() && WeakThis->NightPhase.IsValid() && WeakThis->NightPhase->IsGoalComplete();
	}, 20.0f, TEXT("night2_goal"));
	WaitUntil([WeakThis]() { return WeakThis.IsValid() && !WeakThis->IsRestBlocked(); }, 120.0f, TEXT("night2_quiet"));
	UseNamed(TEXT("night2_bed"), TEXT("MissingFloorSleepTarget"), FVector::ZeroVector, [WeakThis]()
	{
		const AIGNightPhaseDirector* Phase = WeakThis.IsValid() ? WeakThis->NightPhase.Get() : nullptr;
		return Phase && (!Phase->IsHourActive() || Phase->IsDawnTransitionInProgress());
	}, 8.0f);
	WaitUntil([WeakThis]()
	{
		const AIGNightPhaseDirector* Phase = WeakThis.IsValid() ? WeakThis->NightPhase.Get() : nullptr;
		return Phase && !Phase->IsHourActive() && !Phase->IsDawnTransitionInProgress();
	}, 60.0f, TEXT("night2_dawn"));
	WaitSeconds(4.0f, TEXT("day2_wake"));
}

void AIGFullPlaythroughProbe::BuildDayTwo()
{
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	const auto Beat = [WeakThis](const TCHAR* Name) -> TFunction<bool()>
	{
		const FString Id(Name);
		return [WeakThis, Id]() { return WeakThis.IsValid() && WeakThis->HasBeat(*Id); };
	};
	const float FourthZ = AIGPrologueWorldScene::GetStoreyFloorZ(3);

	// 둘째 낮. 아침 독백 뒤 할머니가 401호 문에 일지를 걸어 둔다(문을 보고 있으면 걸지 않는다). 방에서
	// 기다렸다가 나가서 일지를 읽고, 문을 두드려 다시 여쭤본 뒤 돌아와 잔다.
	Stage(TEXT("day2_morning"));
	Listen(TEXT("day2_morning"), 30.0f);
	WaitUntil(Beat(TEXT("Day.JournalHandedOver")), 120.0f, TEXT("day2_journal_hung"));
	GoTo(FVector(131.0f, -305.0f, FourthZ), 30.0f, TEXT("day2_home_door_out"));
	UseNamed(TEXT("day2_journal"), TEXT("MissingFloorJournalNote"), FVector::ZeroVector, [WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return Narrative && Narrative->HasSource(EIGMissingFloorTruth::FiveNightsOfThirst, EIGMissingFloorSource::KnockTallyJournal);
	}, 4.0f);
	WaitSeconds(5.0f, TEXT("day2_reading_journal"));
	PressKey(TEXT("day2_close_journal"), EKeys::E, 0.15f);
	Listen(TEXT("day2_journal"), 20.0f);
	const FName TalkBeat = AIGNightPhaseDirector::DayConversationBeatId(2);
	UseNamed(TEXT("day2_knock_401"), TEXT("MissingFloorUnit401Door"), FVector::ZeroVector, [WeakThis, TalkBeat]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return Narrative && (Narrative->HasBeatPlayed(TalkBeat) || Narrative->HasBeatPlayed(FName(TEXT("Day.Hwang.Journal"))));
	}, 6.0f);
	Listen(TEXT("day2_hwang"), 60.0f);
	Stage(TEXT("day2_bed"));
	GoTo(FVector(131.0f, -160.0f, FourthZ), 30.0f, TEXT("day2_home"));
	UseNamed(TEXT("day2_sleep"), TEXT("MissingFloorSleepTarget"), FVector::ZeroVector, [WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		const AIGNightPhaseDirector* Phase = WeakThis.IsValid() ? WeakThis->NightPhase.Get() : nullptr;
		return Narrative && Phase && Narrative->GetNightIndex() == 3 && Phase->IsHourActive();
	}, 12.0f);
	WaitSeconds(5.0f, TEXT("night3_fade"));
}

void AIGFullPlaythroughProbe::BuildNightThree()
{
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	const auto Truth = [WeakThis](const EIGMissingFloorTruth Wanted) -> TFunction<bool()>
	{
		return [WeakThis, Wanted]()
		{
			const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
			return Narrative && Narrative->HasTruth(Wanted);
		};
	};
	const auto Three = [WeakThis]() -> AIGMissingFloorNightThreeDirector*
	{
		return WeakThis.IsValid() ? WeakThis->FindFirst<AIGMissingFloorNightThreeDirector>() : nullptr;
	};
	const float FourthZ = AIGPrologueWorldScene::GetStoreyFloorZ(3);
	constexpr float RoofZ = AIGPrologueWorldScene::RoofZ;
	const FVector HomeEntry(131.0f, -160.0f, FourthZ);

	// 셋째 밤. 관리실 책상에서 옥상·별관 열쇠를 챙긴다. 관리인이 2·3층을 돈다(듣기만 하는 밤에는 비추기만 한다).
	// 옥상 철문과 별관 문을 열고 5층에서 밸브를 연 뒤 두 칸에 귀를 대 견주고, 수첩을 읽고, 대답하는 벽에
	// 박자대로 두드린다(둘, 쉬고, 하나). 그다음 403호로 돌아와 쉰다.
	Stage(TEXT("night3_awake"));
	WaitSeconds(6.0f, TEXT("night3_opening"));
	Listen(TEXT("night3_opening"), 25.0f);
	Stage(TEXT("night3_keys"));
	GoTo(AIGPrologueWorldScene::GetStairDoorwayFeet(3), 28.0f, TEXT("night3_to_stairs"), HomeEntry);
	TakeStairs(3, 0);
	GoTo(FVector(-45.0f, -305.0f, 0.0f), 30.0f, TEXT("night3_ground_connector"));
	GoTo(FVector(165.0f, -300.0f, 0.0f), 30.0f, TEXT("night3_booth_front"));
	UseNamed(TEXT("night3_keyring"), TEXT("MissingFloorKeyring"), FVector::ZeroVector, [WeakThis]()
	{
		return WeakThis.IsValid() && WeakThis->HasStoryState(TEXT("State.MissingFloor.HasStairKey"));
	}, 4.0f);
	Listen(TEXT("night3_keyring"), 15.0f);
	GoTo(FVector(165.0f, -300.0f, 0.0f), 30.0f, TEXT("night3_booth_out"));
	GoTo(FVector(-45.0f, -305.0f, 0.0f), 30.0f, TEXT("night3_back_to_stairs"));
	GoTo(AIGPrologueWorldScene::GetStairDoorwayFeet(0), 28.0f, TEXT("night3_ground_stair_door"));
	TakeStairs(0, 3);

	Stage(TEXT("night3_roof"));
	Follow(IGFullPlaythrough::RoofStairUp(), 22.0f, TEXT("night3_roof_stair_up"));
	OpenDoor(TEXT("night3_roof_gate"), TEXT("MissingFloorStairGate"), FVector(-277.5f, 110.0f, FourthZ + 242.0f));
	// 문틀 동쪽 기둥 끝(-222.5, 226)을 30 cm 넘게 비켜 북쪽으로 돌아 통로에 든다.
	Follow({FVector(-277.5f, 208.5f, RoofZ), FVector(-277.5f, 245.0f, RoofZ), FVector(-255.0f, 262.0f, RoofZ),
		FVector(-180.0f, 262.0f, RoofZ), FVector(-150.0f, 240.0f, RoofZ), FVector(130.0f, 220.0f, RoofZ)}, 22.0f,
		TEXT("night3_roof_passage"));
	OpenDoor(TEXT("night3_annex_gate"), TEXT("MissingFloorAnnexGate"), FVector(130.0f, 390.0f, RoofZ));
	Stage(TEXT("night3_annex"));
	Follow({FVector(130.0f, 520.0f, RoofZ)}, 25.0f, TEXT("night3_annex_inside"));
	Use(TEXT("night3_valve"), [Three]() -> AActor*
	{
		AIGMissingFloorNightThreeDirector* Director3 = Three();
		return Director3 ? Director3->GetRiserValve() : nullptr;
	}, FVector::ZeroVector, [Three]()
	{
		const AIGMissingFloorNightThreeDirector* Director3 = Three();
		return Director3 && Director3->IsValveOpen();
	}, 4.0f, FVector(185.0f, 631.0f, RoofZ));
	WaitSeconds(2.5f, TEXT("night3_valve_settle"));
	for (int32 Bay = 0; Bay < 2; ++Bay)
	{
		const float BayY = Bay == 0 ? 534.0f : 674.0f;
		Use(FString::Printf(TEXT("night3_listen_bay%d"), Bay), [Three, Bay]() -> AActor*
		{
			AIGMissingFloorNightThreeDirector* Director3 = Three();
			return Director3 ? Director3->GetWallListen(Bay) : nullptr;
		}, FVector::ZeroVector, nullptr, 0.0f, FVector(200.0f, BayY, RoofZ));
		WaitSeconds(3.5f, FString::Printf(TEXT("night3_listening_bay%d"), Bay));
	}
	UseNamed(TEXT("night3_notebook"), TEXT("MissingFloorTunerNotebook"), FVector::ZeroVector, []()
	{
		return AIGReadableNote::GetOpenNote() != nullptr;
	}, 4.0f, FVector(-90.0f, 715.0f, RoofZ));
	WaitSeconds(6.0f, TEXT("night3_reading_notebook"));
	PressKey(TEXT("night3_close_notebook"), EKeys::E, 0.15f);
	WaitUntil(Truth(EIGMissingFloorTruth::SomeoneInTheWall), 10.0f, TEXT("night3_wall_identified"));
	Listen(TEXT("night3_notebook"), 20.0f);
	// 대답하는 벽 앞에서 박자대로 Q(둘, 쉬고, 하나). 첫째→둘째 0.4초, 둘째→셋째 0.8초. E는 누르지 않는다.
	Follow({FVector(200.0f, 700.0f, RoofZ)}, 18.0f, TEXT("night3_walk_to_answer_wall"));
	WaitUntil([Three]()
	{
		const AIGMissingFloorNightThreeDirector* Director3 = Three();
		return Director3 && Director3->GetAnswerTarget() && Director3->GetAnswerTarget()->IsInteractionEnabled();
	}, 10.0f, TEXT("night3_answer_wall_ready"));
	AddLeg(TEXT("knock:night3_answer"), [this, Three]()
	{
		AIGMissingFloorNightThreeDirector* Director3 = Three();
		AActor* Target = Director3 ? Director3->GetAnswerTarget() : nullptr;
		if (Target)
		{
			FVector Origin = FVector::ZeroVector;
			FVector Extent = FVector::ZeroVector;
			Target->GetActorBounds(false, Origin, Extent);
			Route.Aim(Origin, TEXT("aim:night3_answer_wall"));
		}
		Route.Press(0.08f, TEXT("press:night3_answer_tap1"), Target, EKeys::Q);
		Route.Wait(0.32f, TEXT("wait:night3_answer_gap1"));
		Route.Press(0.08f, TEXT("press:night3_answer_tap2"), Target, EKeys::Q);
		Route.Wait(0.72f, TEXT("wait:night3_answer_gap2"));
		Route.Press(0.08f, TEXT("press:night3_answer_tap3"), Target, EKeys::Q);
	});
	WaitUntil(Truth(EIGMissingFloorTruth::WaitingForAnAnswer), 25.0f, TEXT("night3_wall_answers"));
	Listen(TEXT("night3_answer"), 30.0f);

	Stage(TEXT("night3_return"));
	Follow({FVector(130.0f, 520.0f, RoofZ), FVector(130.0f, 390.0f, RoofZ), FVector(130.0f, 220.0f, RoofZ),
		FVector(-150.0f, 240.0f, RoofZ), FVector(-180.0f, 262.0f, RoofZ), FVector(-255.0f, 262.0f, RoofZ),
		FVector(-277.5f, 245.0f, RoofZ), FVector(-277.5f, 208.5f, RoofZ)}, 22.0f, TEXT("night3_back_to_roof_door"));
	{
		TArray<FVector> Down = IGFullPlaythrough::RoofStairUp();
		Algo::Reverse(Down);
		Follow(Down, 22.0f, TEXT("night3_roof_stair_down"));
	}
	// 귀환길(3-7). 복도의 그(남쪽 벽에 엎드려 서쪽을 본다)를 계단 밑에서 보자마자 방금 배운 박자로 대답해
	// 세우고, 북쪽 벽을 따라 옆을 지나 403호로 간다. 세워지지 않으면 옥상 계단 반 층 참으로 물러나 기다린다.
	AddLeg(TEXT("knock:night3_return_answer"), [this, FourthZ]()
	{
		Route.Aim(FVector(-135.0f, -345.0f, FourthZ + 30.0f), TEXT("aim:night3_look_at_figure"));
		Route.Press(0.08f, TEXT("key:night3_return_tap1"), nullptr, EKeys::Q);
		Route.Wait(0.32f, TEXT("wait:night3_return_gap1"));
		Route.Press(0.08f, TEXT("key:night3_return_tap2"), nullptr, EKeys::Q);
		Route.Wait(0.72f, TEXT("wait:night3_return_gap2"));
		Route.Press(0.08f, TEXT("key:night3_return_tap3"), nullptr, EKeys::Q);
		TWeakObjectPtr<AIGFullPlaythroughProbe> WeakSelf(this);
		Route.Until([WeakSelf]()
		{
			const AIGListenerEntity* Entity = WeakSelf.IsValid() ? WeakSelf->FindFirst<AIGListenerEntity>() : nullptr;
			return Entity && Entity->GetListenerState() == EIGListenerState::Waiting;
		}, 6.0f, TEXT("listen:night3_figure_waits"));
	});
	Follow({FVector(-200.0f, -270.0f, FourthZ), FVector(-150.0f, -268.0f, FourthZ), FVector(-110.0f, -268.0f, FourthZ),
		FVector(-40.0f, -268.0f, FourthZ)}, 20.0f, TEXT("night3_pass_north_side"));
	GoTo(HomeEntry, 30.0f, TEXT("night3_home"), FVector(-277.5f, -150.0f, FourthZ + 72.0f));
	WaitUntil([WeakThis]()
	{
		return WeakThis.IsValid() && WeakThis->NightPhase.IsValid() && WeakThis->NightPhase->IsGoalComplete();
	}, 30.0f, TEXT("night3_goal"));
	WaitUntil([WeakThis]() { return WeakThis.IsValid() && !WeakThis->IsRestBlocked(); }, 120.0f, TEXT("night3_quiet"));
	UseNamed(TEXT("night3_bed"), TEXT("MissingFloorSleepTarget"), FVector::ZeroVector, [WeakThis]()
	{
		const AIGNightPhaseDirector* Phase = WeakThis.IsValid() ? WeakThis->NightPhase.Get() : nullptr;
		return Phase && (!Phase->IsHourActive() || Phase->IsDawnTransitionInProgress());
	}, 8.0f);
	WaitUntil([WeakThis]()
	{
		const AIGNightPhaseDirector* Phase = WeakThis.IsValid() ? WeakThis->NightPhase.Get() : nullptr;
		return Phase && !Phase->IsHourActive() && !Phase->IsDawnTransitionInProgress();
	}, 60.0f, TEXT("night3_dawn"));
	WaitSeconds(4.0f, TEXT("day3_wake"));
	Stage(TEXT("day3_morning"));
}

void AIGFullPlaythroughProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFinished)
	{
		return;
	}
	TotalSeconds += DeltaSeconds;
	AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		AbortReason = TEXT("player_missing");
		Finish();
		return;
	}
	if (TotalSeconds > LimitSeconds)
	{
		AbortReason = TEXT("time_limit");
		Finish();
		return;
	}
	if (!bStarted)
	{
		// 셰이더가 서고 첫 화면이 자리 잡을 때까지 기다린다.
		if (TotalSeconds < 1.5f)
		{
			return;
		}
		bStarted = true;
		NightPhase = FindFirst<AIGNightPhaseDirector>();
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			BodyRadius = Capsule->GetScaledCapsuleRadius();
			BodyHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
		const APlayerController* Controller = Cast<APlayerController>(Character->GetController());
		const float Measured = Controller && Controller->PlayerCameraManager
			? static_cast<float>(Controller->PlayerCameraManager->GetCameraLocation().Z - Character->GetActorLocation().Z)
			: Character->BaseEyeHeight;
		EyeOffset = Measured > 20.0f && Measured < 120.0f ? Measured : Character->BaseEyeHeight;
		if (const UIGInteractionComponent* Interaction = Character->FindComponentByClass<UIGInteractionComponent>())
		{
			ReachDistance = Interaction->GetTraceDistance();
		}
		// 위층 사람이 무엇을 듣고 움직이는지 볼 수 있게 소리를 적는다. 걷는 발소리는 빼고, 서 있을 때 몸에서
		// 난 소리(심장, 손전등 딸깍)와 다른 데서 난 소리만 적는다.
		if (UIGNoiseSubsystem* Noise = GetWorld()->GetSubsystem<UIGNoiseSubsystem>())
		{
			TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
			Noise->OnNoiseReported.AddWeakLambda(this, [WeakThis](const FIGNoiseEvent& Event)
			{
				const AIGFullPlaythroughProbe* Self = WeakThis.Get();
				const AIGPlayerCharacter* Character = Self ? Self->Player.Get() : nullptr;
				if (!Self || !Character
					|| (Event.Instigator.Get() == Character && Character->GetVelocity().Size2D() > 10.0f))
				{
					return;
				}
				UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_NOISE t=%.2f loud=%.3f at=%s by=%s"),
					Self->GetWorld()->GetTimeSeconds(), Event.Loudness, *Event.Location.ToCompactString(),
					*GetNameSafe(Event.Instigator.Get()));
			});
		}
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_BEGIN radius=%.1f half=%.1f eye=%.1f reach=%.0f limit=%.0f"),
			BodyRadius, BodyHalfHeight, EyeOffset, ReachDistance, LimitSeconds);
		BuildArrival();
		BuildNightOne();
		BuildDayOne();
		BuildNightTwo();
		BuildDayTwo();
		BuildNightThree();
		StartNextLeg();
		return;
	}

	Route.Tick(DeltaSeconds);
	if (PendingRetry)
	{
		// 막힌 자리에서 길이나 설 자리를 다시 찾는다. 남은 입력은 버리고 같은 일을 새로 짓는다.
		TFunction<void()> Again = MoveTemp(PendingRetry);
		PendingRetry = nullptr;
		Route.ReleaseKeys();
		Legs.Insert(FLeg{CurrentLeg, MoveTemp(Again)}, LegIndex + 1);
		StartNextLeg();
	}
	else if (AbortReason.IsEmpty() && Route.IsDone())
	{
		StartNextLeg();
	}
	if (!AbortReason.IsEmpty() && !bFinished)
	{
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_BLOCKED %s leg=%s step=%s objective=\"%s\""), *AbortReason,
			*CurrentLeg, *Route.GetCurrentName(), *GetObjective());
		Shoot(TEXT("blocked"));
		Finish();
		return;
	}
	const FString Step = Route.GetCurrentName();
	if (Step.StartsWith(TEXT("press:")))
	{
		AActor* Focus = Route.GetFocusedActor();
		if (Focus != LastFocus.Get())
		{
			UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_FOCUS %s from=%s to=%s t=%.2f"), *Step, *GetNameSafe(LastFocus.Get()),
				*GetNameSafe(Focus), GetWorld()->GetTimeSeconds());
			LastFocus = Focus;
		}
	}
	else
	{
		LastFocus = nullptr;
	}
	if (TotalSeconds >= NextPositionLog)
	{
		NextPositionLog = TotalSeconds + IGFullPlaythrough::PositionLogSeconds;
		// 위층 사람이 어디서 무엇을 하는지 같이 적는다. 길을 막거나 쉬기를 막는 일이 많다.
		FString Upstairs = TEXT("none");
		if (const AIGListenerEntity* Entity = FindFirst<AIGListenerEntity>())
		{
			Upstairs = FString::Printf(TEXT("%s state=%d home_door=%d dormant=%d"),
				*Entity->GetActorLocation().ToCompactString(), static_cast<int32>(Entity->GetListenerState()),
				Entity->IsAtHomeDoor() ? 1 : 0, Entity->IsDormant() ? 1 : 0);
		}
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_AT t=%.1f at=%s leg=%s step=%s upstairs=%s"), TotalSeconds,
			*Character->GetActorLocation().ToCompactString(), *CurrentLeg, *Route.GetCurrentName(), *Upstairs);
	}
}
