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
#include "Entity/IGMissingFloorEpilogueDirector.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorFifthDawnDirector.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
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
	/** 그가 막은 길을 물러났다 다시 가 보는 횟수(처음 포함). */
	constexpr int32 MaxPassAttempts = 6;
	/** 넷째 밤 벽을 뜯는 차례(쓸 데가 없어 1초씩 기다리는 차례 포함)의 상한. */
	constexpr int32 MaxWallRounds = 180;
	/** 중간 점은 이만큼 가까이 가면 다음 점으로 간다. */
	constexpr float PassTolerance = 22.0f;
	/** 물건 앞에 서는 거리들. 사람이 손을 뻗는 거리부터 본다. */
	// 뒤의 둘은 물건 바로 앞이 막혔을 때 한 걸음 물러서서 뻗는 자리다(손 닿는 거리 안일 때만 쓴다).
	constexpr float StandRadii[] = {85.0f, 105.0f, 125.0f, 65.0f, 145.0f, 165.0f, 185.0f};
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
	// 에필로그 뒤 타이틀은 게임을 멈춘다. 멈춘 동안에도 틱을 받아야 마무리 줄을 남긴다(Tick 참고).
	PrimaryActorTick.bTickEvenWhenPaused = true;
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
	FString Ending;
	if (FParse::Value(FCommandLine::Get(), TEXT("IGFullPlaythroughEnding="), Ending) && Ending.Equals(TEXT("B"), ESearchCase::IgnoreCase))
	{
		EndingChoice = TEXT("B");
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

void AIGFullPlaythroughProbe::Crouch(const FString& Name)
{
	AddLeg(TEXT("crouch:") + Name, [this, Name]()
	{
		const AIGPlayerCharacter* Character = Player.Get();
		if (!Character || Character->bIsCrouched)
		{
			return;
		}
		Route.Press(0.1f, TEXT("key:") + Name, nullptr, EKeys::C);
		TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
		Route.Until([WeakThis]()
		{
			const AIGPlayerCharacter* Body = WeakThis.IsValid() ? WeakThis->Player.Get() : nullptr;
			return Body && Body->bIsCrouched;
		}, 3.0f, TEXT("listen:") + Name);
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

void AIGFullPlaythroughProbe::SetDoor(const FString& Name, const FString& DoorName, const FVector& Stand, const bool bOpen)
{
	AddLeg(TEXT("door:") + Name, MakeSetDoor(Name, DoorName, Stand, bOpen, 0));
}

TFunction<void()> AIGFullPlaythroughProbe::MakeSetDoor(FString Name, FString DoorName, FVector Stand, bool bOpen, int32 Attempt)
{
	return [this, Name, DoorName, Stand, bOpen, Attempt]()
	{
		AIGSwingDoor* Door = Cast<AIGSwingDoor>(FindNamed(DoorName));
		if (!Door)
		{
			AbortReason = TEXT("target:") + Name;
			return;
		}
		if (Door->IsOpen() == bOpen)
		{
			return;
		}
		// 눌렀는데 문이 그대로면 사람처럼 한 번 더 누른다. 5층 철문을 안에서 닫을 때 초점이 막 잡힌 순간의 누름이
		// 문에 가지 않은 적이 있다.
		if (Attempt + 1 < IGFullPlaythrough::MaxAttempts)
		{
			Retry = MakeSetDoor(Name, DoorName, Stand, bOpen, Attempt + 1);
		}
		Route.Walk(Stand, 15.0f, TEXT("walk:") + Name);
		Route.Aim(Door->GetLatchSoundLocation(), TEXT("aim:") + Name, 0.6f);
		// 겨눈 뒤 손잡이에 초점이 자리 잡을 틈을 둔다.
		Route.Wait(0.2f, TEXT("settle:") + Name);
		Route.Press(0.3f, TEXT("press:") + Name, Door);
		TWeakObjectPtr<AIGSwingDoor> WeakDoor(Door);
		Route.Until([WeakDoor, bOpen]() { return WeakDoor.IsValid() && WeakDoor->IsOpen() == bOpen && !WeakDoor->IsSwinging(); },
			6.0f, TEXT("gate:") + Name);
	};
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
		// 다시 할 때는 다음 점까지 곧게 가지 않고 길을 찾아 간다. 곧은 길을 막은 몸을 비켜 가고, 점 자체가 막혀 있으면
		// 그 점을 건너 다음 점으로 간다. 첫째 밤 3.5층 참에 엎드린 위층 사람이 참 가운데로 기어 나오면 참의 꺾는 점이 그의
		// 몸 안에 들어가는데, 참 남쪽 가장자리로 아래 계단 첫 단까지는 갈 수 있다. 길이 없으면 곧게 간다.
		if (Attempt > 0)
		{
			const FVector Feet = Player->GetActorLocation() - FVector(0.0f, 0.0f, BodyHalfHeight);
			const int32 Start = FVector::Dist(Feet, Points[First]) < 40.0f ? First + 1 : First;
			for (int32 Ahead = Start; Ahead < Points.Num() && Ahead <= Start + 1; ++Ahead)
			{
				TArray<FVector> Detour;
				FString Why;
				// 길 찾기는 막힌 목표를 가까운 설 수 있는 칸으로 옮겨 잇는다. 그 칸이 점에서 멀면 점은 막힌 것이다.
				if (!PlanPath(Points[Ahead], Detour, Why)
					|| (Detour.Num() > 0 && FVector::Dist2D(Detour.Last(), Points[Ahead]) > 25.0f))
				{
					UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_SKIP %s point=%d why=%s"), *Name, Ahead,
						Why.IsEmpty() ? TEXT("snapped") : *Why);
					continue;
				}
				for (int32 Step = 0; Step + 1 < Detour.Num(); ++Step)
				{
					Route.Walk(Detour[Step], IGFullPlaythrough::PassTolerance, TEXT("walk:") + Name);
				}
				First = Ahead;
				break;
			}
		}
		for (int32 Index = First; Index < Points.Num(); ++Index)
		{
			Route.Walk(Points[Index], Tolerance, TEXT("walk:") + Name);
		}
	};
}

void AIGFullPlaythroughProbe::FollowPast(const TArray<FVector>& Points, const float Tolerance, const FString& Name,
	TFunction<bool()> Clear)
{
	AddLeg(TEXT("pass:") + Name, MakeFollowPast(Points, Tolerance, Name, MoveTemp(Clear), 0));
}

TFunction<void()> AIGFullPlaythroughProbe::MakeFollowPast(TArray<FVector> Points, float Tolerance, FString Name,
	TFunction<bool()> Clear, int32 Attempt)
{
	return [this, Points, Tolerance, Name, Clear, Attempt]()
	{
		if (Attempt + 1 < IGFullPlaythrough::MaxPassAttempts)
		{
			Retry = MakeFollowPast(Points, Tolerance, Name, Clear, Attempt + 1);
		}
		// 막혔으면 지나온 점을 거슬러 첫 점까지 물러나서 그가 떠나기를 기다린다. 그 곁에 서 있으면 내 숨과 심장
		// 소리에 그도 그 자리를 뜨지 않는다.
		if (Attempt > 0)
		{
			const FVector Feet = Player->GetActorLocation() - FVector(0.0f, 0.0f, BodyHalfHeight);
			int32 Nearest = 0;
			float Best = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const float Distance = static_cast<float>(FVector::Dist(Feet, Points[Index]));
				if (Distance < Best)
				{
					Best = Distance;
					Nearest = Index;
				}
			}
			UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_BACK_OFF %s attempt=%d from=%d"), *Name, Attempt, Nearest);
			// 가장 가까운 점은 막힌 앞쪽일 수 있다(계단 밑에서 막히면 그 점에 그가 서 있다). 한 점 뒤부터 거스른다.
			for (int32 Index = FMath::Max(Nearest - 1, 0); Index >= 0; --Index)
			{
				Route.Walk(Points[Index], Tolerance, TEXT("walk:") + Name + TEXT("_back"));
			}
		}
		if (Clear)
		{
			Route.Until(Clear, 180.0f, TEXT("gate:") + Name + TEXT("_clear"));
		}
		for (int32 Index = 0; Index < Points.Num(); ++Index)
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
	// 씬의 꺾는 점은 반 층 참 안쪽 55 cm(Y 20)에 있다. 사람은 모퉁이를 바짝 돌아 참 남쪽으로 꺾는다. 3.5층 참에 엎드린
	// 그(-528, 52)의 몸이 Y 20 점을 덮고 있었다.
	for (FVector& Point : Points)
	{
		if (FMath::IsNearlyEqual(Point.Y, 20.0f, 1.0f) && AIGPrologueWorldScene::IsInsideStairCore(Point))
		{
			Point.Y = -15.0f;
		}
	}
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
			// 정해 준 자리라도 가는 길은 찾는다. 곧게 걸으면 403호 현관에서 문틀에 걸렸다(셋째 낮 요구서 앞).
			FString Why;
			if (!PlanPath(Stand.GetValue(), Path, Why))
			{
				Path.Reset();
				Path.Add(Stand.GetValue());
			}
		}
		else
		{
			TArray<FVector> Spots;
			bVerbosePlan = Waits == 0 || Waits + 1 >= IGFullPlaythrough::MaxBlockedWaits;
			FindStandSpots(Target, Aim, Spots);
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

FString AIGFullPlaythroughProbe::DescribeBodyBlock(const FVector2D& At, const float FloorZ, const float ExtraRadius) const
{
	// IsBodyClear와 같은 몸을 세워 보고 걸린 것을 이름과 자리로 적는다(처음 셋).
	const float Half = BodyHalfHeight - IGFullPlaythrough::StepLift * 0.5f;
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByChannel(Overlaps, FVector(At, FloorZ + IGFullPlaythrough::StepLift + Half), FQuat::Identity,
		BodyChannel, FCollisionShape::MakeCapsule(BodyRadius + ExtraRadius, Half), BodyQuery, BodyResponse);
	FString Text = FString::Printf(TEXT("spot=(%.0f,%.0f,%.0f)"), At.X, At.Y, FloorZ);
	int32 Count = 0;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Overlap.bBlockingHit && Count++ < 3)
		{
			const UPrimitiveComponent* Component = Overlap.GetComponent();
			Text += FString::Printf(TEXT(" %s/%s@%s~%s"), *GetNameSafe(Overlap.GetActor()), *GetNameSafe(Component),
				Component ? *Component->Bounds.Origin.ToCompactString() : TEXT("none"),
				Component ? *Component->Bounds.BoxExtent.ToCompactString() : TEXT("none"));
		}
	}
	return Text;
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
	// 설 자리가 하나도 없으면 왜 없는지 적는다. 바닥, 몸, 손 닿는 거리, 눈길 중 어디서 걸렸는지와 눈길을 처음
	// 막은 것이다.
	int32 NoFloor = 0;
	int32 NoBody = 0;
	int32 TooFar = 0;
	int32 NoSight = 0;
	FString FirstSightBlock;
	FString FirstBodyBlock;
	float NearestBodyBlock = TNumericLimits<float>::Max();
	TMap<FString, int32> BodyBlockers;
	for (const float Radius : StandRadii)
	{
		for (int32 Direction = 0; Direction < StandDirections; ++Direction)
		{
			const float Angle = 2.0f * UE_PI * Direction / StandDirections;
			const FVector2D At = FVector2D(Aim) + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
			float Floor = 0.0f;
			if (!ProbeFloor(At, FromFloor, Floor) || FMath::Abs(Floor - FromFloor) > StandFloorRange)
			{
				++NoFloor;
				continue;
			}
			if (!IsBodyClear(At, Floor, BodySkin + 4.0f))
			{
				// 지금 선 자리에서 가장 가까운(물건 앞쪽) 막힌 자리와, 막힌 자리마다 걸린 것을 센다.
				++NoBody;
				const float Near = static_cast<float>(FVector2D::Distance(FVector2D(From), At));
				if (Near < NearestBodyBlock)
				{
					NearestBodyBlock = Near;
					FirstBodyBlock = DescribeBodyBlock(At, Floor, BodySkin + 4.0f);
				}
				if (bVerbosePlan)
				{
					const float Half = BodyHalfHeight - StepLift * 0.5f;
					TArray<FOverlapResult> Overlaps;
					GetWorld()->OverlapMultiByChannel(Overlaps, FVector(At, Floor + StepLift + Half), FQuat::Identity, BodyChannel,
						FCollisionShape::MakeCapsule(BodyRadius + BodySkin + 4.0f, Half), BodyQuery, BodyResponse);
					for (const FOverlapResult& Overlap : Overlaps)
					{
						if (Overlap.bBlockingHit)
						{
							++BodyBlockers.FindOrAdd(GetNameSafe(Overlap.GetActor()) + TEXT("/") + GetNameSafe(Overlap.GetComponent()));
						}
					}
				}
				continue;
			}
			// 서서 겨눈 눈길이 다른 데 걸리지 않고 그 물건에 먼저 닿아야 한다. 손이 닿는 거리 안이어야 한다.
			const FVector Eye(At, Floor + BodyHalfHeight + EyeOffset);
			if (FVector::Dist(Eye, Aim) > ReachDistance - 30.0f)
			{
				++TooFar;
				continue;
			}
			FHitResult Hit;
			const FVector End = Aim + (Aim - Eye).GetSafeNormal() * 6.0f;
			// 게임의 초점처럼 숨겨 두고 꺼 둔 물건(다 쓴 벽 표적 같은 것)은 건너뛰고 본다.
			FCollisionQueryParams Look = Sight;
			bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Look);
			for (int32 Skip = 0; bHit && Hit.GetActor() != Target && Skip < UIGInteractionComponent::MaxDormantSkips
				&& UIGInteractionComponent::IsDormantInteractable(Hit.GetActor(), Player.Get()); ++Skip)
			{
				Look.AddIgnoredActor(Hit.GetActor());
				bHit = GetWorld()->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Look);
			}
			if (!bHit || Hit.GetActor() != Target)
			{
				if (++NoSight == 1)
				{
					FirstSightBlock = FString::Printf(TEXT("%s/%s at=%s from=%s"), *GetNameSafe(Hit.GetActor()),
						*GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToCompactString(), *Eye.ToCompactString());
				}
				continue;
			}
			Spots.Add(FSpot{FVector(At, Floor), static_cast<float>(FVector2D::Distance(FVector2D(From), At))
				+ FMath::Abs(Radius - 100.0f) * 0.5f});
		}
	}
	if (Spots.Num() == 0 && bVerbosePlan)
	{
		BodyBlockers.ValueSort([](const int32 A, const int32 B) { return A > B; });
		FString Blockers;
		int32 Listed = 0;
		for (const TPair<FString, int32>& Pair : BodyBlockers)
		{
			if (Listed++ < 5)
			{
				Blockers += FString::Printf(TEXT(" %s=%d"), *Pair.Key, Pair.Value);
			}
		}
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_SPOTS %s none floor=%d body=%d far=%d sight=%d first_sight=%s first_body=%s blockers=%s"),
			*Target->GetName(), NoFloor, NoBody, TooFar, NoSight, FirstSightBlock.IsEmpty() ? TEXT("none") : *FirstSightBlock,
			FirstBodyBlock.IsEmpty() ? TEXT("none") : *FirstBodyBlock, Blockers.IsEmpty() ? TEXT(" none") : *Blockers);
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
	// 3.5층 참에 그가 엎드려 있다(첫 목격). 소리를 내지 않으면 그는 모른다는 것이 이 장면이 가르치는 규칙이라, 그
	// 곁은 쪼그리고 내려간다. 걸어 내려가면 발소리에 기어 나와 참 가운데를 막았다.
	Crouch(TEXT("night1_sneak"));
	TakeStairs(3, 2);
	StandUp(TEXT("night1_sneak"));
	TakeStairs(2, 0);
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
	SetDoor(TEXT("night3_roof_gate"), TEXT("MissingFloorStairGate"), FVector(-277.5f, 110.0f, FourthZ + 242.0f), true);
	// 문틀 동쪽 기둥 끝(-222.5, 226)을 30 cm 넘게 비켜 북쪽으로 돌아 통로에 든다.
	Follow({FVector(-277.5f, 208.5f, RoofZ), FVector(-277.5f, 245.0f, RoofZ), FVector(-255.0f, 262.0f, RoofZ),
		FVector(-180.0f, 262.0f, RoofZ), FVector(-150.0f, 240.0f, RoofZ), FVector(130.0f, 220.0f, RoofZ)}, 22.0f,
		TEXT("night3_roof_passage"));
	SetDoor(TEXT("night3_annex_gate"), TEXT("MissingFloorAnnexGate"), FVector(130.0f, 390.0f, RoofZ), true);
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
	// 5층 철문 소리 같은 큰 소리에 위층 사람이 자리를 떠나면 계단 쪽으로 왔다가, 닿고 물러나 남쪽 벽 제자리로
	// 돌아간다. 돌아가는 길에 대답하면 복도 가운데서 기다려 북쪽 틈이 25 cm만 남았고, 제자리에 돌아간 뒤에는 대답을
	// 안 받은 채라 몸이 길을 막았다(11-03). 사람이 하듯 제자리에 돌아가 벽에 귀를 댈 때까지 계단 밑에서 기다렸다 대답한다.
	WaitUntil([WeakThis, FourthZ]()
	{
		const AIGListenerEntity* Entity = WeakThis.IsValid() ? WeakThis->FindFirst<AIGListenerEntity>() : nullptr;
		return !Entity || Entity->IsDormant() || FMath::Abs(Entity->GetActorLocation().Z - FourthZ) > 150.0f
			|| FVector::Dist2D(Entity->GetActorLocation(), FVector(-95.0f, -332.0f, FourthZ)) < 60.0f;
	}, 90.0f, TEXT("night3_figure_settled"));
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
}

void AIGFullPlaythroughProbe::BuildDayThree()
{
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	const auto Beat = [WeakThis](const TCHAR* Name) -> TFunction<bool()>
	{
		const FString Id(Name);
		return [WeakThis, Id]() { return WeakThis.IsValid() && WeakThis->HasBeat(*Id); };
	};
	const float FourthZ = AIGPrologueWorldScene::GetStoreyFloorZ(3);
	constexpr float RoofZ = AIGPrologueWorldScene::RoofZ;
	const FVector HomeEntry(131.0f, -160.0f, FourthZ);

	// 셋째 낮. 신고와 접수 문자가 오고, 경찰이 현장을 보러 온다는 문자에 옥상으로 올라가 5층에서 경찰관이
	// 벽을 두드리는 것을 본다. 403호로 돌아오면 문밖에 요구서가 붙는다. 엘리베이터 옆 안내문을 읽고, 마감
	// 문자를 받고, 401호 할머니께 간밤 일을 알린 뒤 잔다.
	Stage(TEXT("day3_morning"));
	Listen(TEXT("day3_morning"), 30.0f);
	WaitUntil(Beat(TEXT("Night3.SiteCheckDispatched")), 240.0f, TEXT("day3_police_dispatched"));
	Listen(TEXT("day3_dispatch_text"), 20.0f);
	Stage(TEXT("day3_police"));
	GoTo(FVector(-262.0f, -292.0f, FourthZ), 25.0f, TEXT("day3_roof_stair_foot"), HomeEntry);
	Follow(IGFullPlaythrough::RoofStairUp(), 22.0f, TEXT("day3_roof_stair_up"));
	SetDoor(TEXT("day3_roof_gate"), TEXT("MissingFloorStairGate"), FVector(-277.5f, 110.0f, FourthZ + 242.0f), true);
	Follow({FVector(-277.5f, 208.5f, RoofZ), FVector(-277.5f, 245.0f, RoofZ), FVector(-255.0f, 262.0f, RoofZ),
		FVector(-180.0f, 262.0f, RoofZ), FVector(-150.0f, 240.0f, RoofZ), FVector(130.0f, 220.0f, RoofZ)}, 22.0f,
		TEXT("day3_roof_passage"));
	SetDoor(TEXT("day3_annex_gate"), TEXT("MissingFloorAnnexGate"), FVector(130.0f, 390.0f, RoofZ), true);
	// 별관 안, 관리인(198, 540)과 경찰관(207, 696)이 서는 자리와 경찰관이 걸어오는 길을 비켜 그 뒤에 선다.
	// 서쪽(X 60 밑)은 가구가 막는다.
	Follow({FVector(130.0f, 520.0f, RoofZ), FVector(110.0f, 690.0f, RoofZ)}, 22.0f, TEXT("day3_annex_inside"));
	WaitUntil(Beat(TEXT("Night3.SiteCheck")), 240.0f, TEXT("day3_site_check"));
	Listen(TEXT("day3_officer"), 40.0f);
	Stage(TEXT("day3_home"));
	Follow({FVector(130.0f, 520.0f, RoofZ), FVector(130.0f, 390.0f, RoofZ), FVector(130.0f, 220.0f, RoofZ),
		FVector(-150.0f, 240.0f, RoofZ), FVector(-180.0f, 262.0f, RoofZ), FVector(-255.0f, 262.0f, RoofZ),
		FVector(-277.5f, 245.0f, RoofZ), FVector(-277.5f, 208.5f, RoofZ)}, 22.0f, TEXT("day3_back_to_roof_door"));
	{
		TArray<FVector> Down = IGFullPlaythrough::RoofStairUp();
		Algo::Reverse(Down);
		Follow(Down, 22.0f, TEXT("day3_roof_stair_down"));
	}
	GoTo(HomeEntry, 30.0f, TEXT("day3_home"));
	WaitUntil(Beat(TEXT("Day.EvictionPosted")), 180.0f, TEXT("day3_eviction_posted"));
	Listen(TEXT("day3_eviction_sound"), 20.0f);
	// 사람은 종이 앞에 서서 읽는다. 걸어온 쪽에서 가장 가까운 자리를 고르면 1.8 m 비스듬히 읽어서 인쇄면을 못 본다.
	UseNamed(TEXT("day3_eviction_notice"), TEXT("MissingFloorEvictionNotice"), FVector::ZeroVector, [WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return Narrative && Narrative->HasSource(EIGMissingFloorTruth::StillCoveringIt, EIGMissingFloorSource::EvictionWarning);
	}, 5.0f, FVector(640.0f, -300.0f, FourthZ));
	// 읽은 자리에서 한 장 찍는다. 벽에 붙은 종이와 인쇄면이 보이는지 본다.
	Stage(TEXT("day3_notice_read"));
	Listen(TEXT("day3_eviction_read"), 30.0f);
	WaitUntil(Beat(TEXT("Day.EvictionDeadline")), 180.0f, TEXT("day3_deadline_text"));
	Listen(TEXT("day3_deadline"), 30.0f);
	const FName TalkBeat = AIGNightPhaseDirector::DayConversationBeatId(3);
	UseNamed(TEXT("day3_knock_401"), TEXT("MissingFloorUnit401Door"), FVector::ZeroVector, [WeakThis, TalkBeat]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return Narrative && (Narrative->HasBeatPlayed(TalkBeat) || Narrative->HasBeatPlayed(FName(TEXT("Day.Hwang.Permission"))));
	}, 8.0f);
	Listen(TEXT("day3_hwang"), 60.0f);
	Stage(TEXT("day3_bed"));
	GoTo(HomeEntry, 30.0f, TEXT("day3_back_home"));
	UseNamed(TEXT("day3_sleep"), TEXT("MissingFloorSleepTarget"), FVector::ZeroVector, [WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		const AIGNightPhaseDirector* Phase = WeakThis.IsValid() ? WeakThis->NightPhase.Get() : nullptr;
		return Narrative && Phase && Narrative->GetNightIndex() == 4 && Phase->IsHourActive();
	}, 12.0f);
	WaitSeconds(5.0f, TEXT("night4_fade"));
}

void AIGFullPlaythroughProbe::BuildNightFour()
{
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	const auto Beat = [WeakThis](const TCHAR* Name) -> TFunction<bool()>
	{
		const FString Id(Name);
		return [WeakThis, Id]() { return WeakThis.IsValid() && WeakThis->HasBeat(*Id); };
	};
	const auto Control = [WeakThis](const TCHAR* Id) -> TFunction<bool()>
	{
		const FName ControlId(Id);
		return [WeakThis, ControlId]()
		{
			const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
			return Narrative && Narrative->HasNightFourControl(ControlId);
		};
	};
	const float FourthZ = AIGPrologueWorldScene::GetStoreyFloorZ(3);
	constexpr float RoofZ = AIGPrologueWorldScene::RoofZ;
	const FVector HomeEntry(131.0f, -160.0f, FourthZ);
	const TArray<FVector> RoofOut = {FVector(-277.5f, 208.5f, RoofZ), FVector(-277.5f, 245.0f, RoofZ),
		FVector(-255.0f, 262.0f, RoofZ), FVector(-180.0f, 262.0f, RoofZ)};
	TArray<FVector> RoofIn = RoofOut;
	Algo::Reverse(RoofIn);
	TArray<FVector> RoofStairDown = IGFullPlaythrough::RoofStairUp();
	Algo::Reverse(RoofStairDown);
	// 옥상 문에서 5층 철문 앞까지.
	const TArray<FVector> RoofToAnnex = {FVector(-277.5f, 208.5f, RoofZ), FVector(-277.5f, 245.0f, RoofZ),
		FVector(-255.0f, 262.0f, RoofZ), FVector(-180.0f, 262.0f, RoofZ), FVector(-150.0f, 240.0f, RoofZ),
		FVector(130.0f, 220.0f, RoofZ)};

	// 선택 직전 저장(벽 앞)에서 이어 하면 403호 침대에서 깬다. 벽은 열려 있고 대치도 끝났다. 5층으로 올라가
	// 결말만 고른다. 이어 하기로 다른 결말을 보러 오는 사람이 걷는 길이다.
	if (const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		Narrative && Narrative->IsNightFourWallOpened() && Narrative->GetEndingChoice().IsNone())
	{
		Stage(TEXT("night4_resumed_choice"));
		WaitSeconds(6.0f, TEXT("night4_resumed_opening"));
		GoTo(FVector(-262.0f, -292.0f, FourthZ), 25.0f, TEXT("resume_roof_stair_foot"), HomeEntry);
		Follow(IGFullPlaythrough::RoofStairUp(), 22.0f, TEXT("resume_roof_stair_up"));
		SetDoor(TEXT("resume_roof_gate"), TEXT("MissingFloorStairGate"), FVector(-277.5f, 110.0f, FourthZ + 242.0f), true);
		Follow(RoofToAnnex, 22.0f, TEXT("resume_roof_passage"));
		SetDoor(TEXT("resume_annex_gate"), TEXT("MissingFloorAnnexGate"), FVector(130.0f, 390.0f, RoofZ), true);
		Follow({FVector(130.0f, 520.0f, RoofZ)}, 25.0f, TEXT("resume_annex_inside"));
		BuildEnding();
		return;
	}

	// 넷째 밤. 물탱크 청소 회로를 옥상 청소 배수, 부구 우회, 1층 관리실 이송 펌프 차례로 돌려 물소리를 깐다.
	// 5층에 들어가 철문을 닫고 빗장을 건 뒤, 벽을 게임이 내놓는 차례대로 뜯는다. 벽이 열리면 안을 보고 막간과
	// 대치를 지나 결말을 고른다. 에필로그가 끝나고 타이틀이 뜨면 마친다.
	Stage(TEXT("night4_awake"));
	WaitSeconds(6.0f, TEXT("night4_opening"));
	Listen(TEXT("night4_opening"), 30.0f);
	Stage(TEXT("night4_roof_valves"));
	GoTo(FVector(-262.0f, -292.0f, FourthZ), 25.0f, TEXT("night4_roof_stair_foot"), HomeEntry);
	Follow(IGFullPlaythrough::RoofStairUp(), 22.0f, TEXT("night4_roof_stair_up"));
	SetDoor(TEXT("night4_roof_gate"), TEXT("MissingFloorStairGate"), FVector(-277.5f, 110.0f, FourthZ + 242.0f), true);
	Follow(RoofOut, 22.0f, TEXT("night4_roof_out"));
	UseNamed(TEXT("night4_cleaning_drain"), TEXT("MissingFloorCleaningDrain"), FVector::ZeroVector,
		Control(TEXT("P5.RoofCleaningDrain")), 6.0f);
	UseNamed(TEXT("night4_float_bypass"), TEXT("MissingFloorFloatBypass"), FVector::ZeroVector,
		Control(TEXT("P5.RoofFloatBypass")), 6.0f);
	Listen(TEXT("night4_valves"), 15.0f);

	Stage(TEXT("night4_pump"));
	// 옥상 계단은 4층 복도 서쪽 끝으로 내려간다. 그는 옥상으로 올라오지 않아서, 위에서 소리가 나면 계단 밑까지
	// 와 두드리고 듣기를 되풀이한다. 그가 계단 밑에 있으면 그 곁을 지나갈 수 없다. 내려오는 발소리를 듣고 온
	// 그와 계단에서 마주 서서 둘 다 움직이지 못했다. 사람이 하듯 옥상에서 그가 복도 안쪽으로 떠나기를
	// 기다렸다가 내려가고, 내려가다 마주치면 옥상으로 물러났다가 다시 간다.
	const auto StairFootClear = [WeakThis, FourthZ]()
	{
		const AIGListenerEntity* Entity = WeakThis.IsValid() ? WeakThis->FindFirst<AIGListenerEntity>() : nullptr;
		if (!Entity || Entity->IsDormant())
		{
			return true;
		}
		const FVector At = Entity->GetActorLocation();
		// 그는 넷째 밤 내내 4층 복도 서쪽 절반을 돈다. 계단 밑만 비어 있으면 된다.
		return FMath::Abs(At.Z - FourthZ) > 150.0f || FVector::Dist2D(At, FVector(-262.0f, -292.0f, FourthZ)) > 250.0f;
	};
	TArray<FVector> RoofDown = RoofIn;
	RoofDown.Append(RoofStairDown);
	FollowPast(RoofDown, 22.0f, TEXT("night4_roof_stair_down"), StairFootClear);
	GoTo(AIGPrologueWorldScene::GetStairDoorwayFeet(3), 28.0f, TEXT("night4_to_stairs"), HomeEntry);
	TakeStairs(3, 0);
	GoTo(FVector(-45.0f, -305.0f, 0.0f), 30.0f, TEXT("night4_ground_connector"));
	GoTo(FVector(165.0f, -300.0f, 0.0f), 30.0f, TEXT("night4_booth_front"));
	UseNamed(TEXT("night4_transfer_pump"), TEXT("MissingFloorTransferPump"), FVector::ZeroVector,
		Control(TEXT("P5.TransferPump")), 6.0f);
	WaitUntil([WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return Narrative && Narrative->IsPuzzleSolved(FName(TEXT("P5")));
	}, 15.0f, TEXT("night4_water_mask"));
	Listen(TEXT("night4_pump"), 15.0f);

	Stage(TEXT("night4_annex"));
	GoTo(FVector(165.0f, -300.0f, 0.0f), 30.0f, TEXT("night4_booth_out"));
	GoTo(FVector(-45.0f, -305.0f, 0.0f), 30.0f, TEXT("night4_back_to_stairs"));
	GoTo(AIGPrologueWorldScene::GetStairDoorwayFeet(0), 28.0f, TEXT("night4_ground_stair_door"));
	TakeStairs(0, 3);
	GoTo(FVector(-262.0f, -292.0f, FourthZ), 25.0f, TEXT("night4_back_to_roof_stair"), HomeEntry);
	Follow(IGFullPlaythrough::RoofStairUp(), 22.0f, TEXT("night4_roof_stair_up_again"));
	SetDoor(TEXT("night4_roof_gate_again"), TEXT("MissingFloorStairGate"), FVector(-277.5f, 110.0f, FourthZ + 242.0f), true);
	Follow(RoofToAnnex, 22.0f, TEXT("night4_roof_passage"));
	SetDoor(TEXT("night4_annex_gate"), TEXT("MissingFloorAnnexGate"), FVector(130.0f, 390.0f, RoofZ), true);
	Follow({FVector(130.0f, 520.0f, RoofZ)}, 25.0f, TEXT("night4_annex_inside"));
	// 안에서 철문을 닫고 빗장을 건다. 망치질 사이에 문밖에서 부르는 소리가 와도 문이 안 열린다. 문짝은 안으로
	// 열려 있어 경첩(85, 452.5)에서 88 cm 안쪽은 닫히는 문짝이 지나간다. 그 밖에 서서 닫는다.
	SetDoor(TEXT("night4_close_annex_gate"), TEXT("MissingFloorAnnexGate"), FVector(160.0f, 575.0f, RoofZ), false);
	UseNamed(TEXT("night4_bolt"), TEXT("MissingFloorAnnexBolt"), FVector::ZeroVector, [WeakThis]()
	{
		const AIGSwingDoor* Gate = WeakThis.IsValid() ? Cast<AIGSwingDoor>(WeakThis->FindNamed(TEXT("MissingFloorAnnexGate"))) : nullptr;
		return Gate && Gate->IsLatched();
	}, 4.0f);

	Stage(TEXT("night4_wall"));
	// 벽 앞에 서서 이음매 쪽을 한 장 찍는다. 덧댄 것이 벽에 어떻게 붙어 있는지 본다.
	AddLeg(TEXT("look:night4_seam"), [this]()
	{
		Route.Aim(FVector(243.0f, 700.0f, 1383.0f), TEXT("aim:night4_seam"));
	});
	Stage(TEXT("night4_seam_view"));
	AddLeg(TEXT("wall:0"), MakeWallStep(0));
	WaitUntil([WeakThis]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return Narrative && Narrative->IsNightFourWallOpened();
	}, 30.0f, TEXT("night4_wall_open"));
	BuildReveal();
	WaitUntil(Beat(TEXT("Night4.FinalConfrontation")), 240.0f, TEXT("night4_confrontation"));
	Listen(TEXT("night4_after_confrontation"), 20.0f);
	BuildEnding();
}

void AIGFullPlaythroughProbe::BuildReveal()
{
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	const auto Four = [WeakThis]() -> const AIGMissingFloorNightFourDirector*
	{
		return WeakThis.IsValid() ? WeakThis->FindFirst<AIGMissingFloorNightFourDirector>() : nullptr;
	};
	const auto Interlude = [WeakThis]()
	{
		const AIGMissingFloorFifthDawnDirector* Dawn = WeakThis.IsValid() ? WeakThis->FindFirst<AIGMissingFloorFifthDawnDirector>() : nullptr;
		return Dawn && Dawn->IsActive();
	};

	// 벽이 열리면 손전등이 두개골, 몸, 발 차례로 훑는다. 사람이 하듯 그 자리를 차례로 본다. 예전 검사는 서 있기만
	// 해서 단계마다 5.5초 폴백으로 넘어갔고, 그동안 천장만 찍혔다.
	const TCHAR* const Parts[] = {TEXT("skull"), TEXT("body"), TEXT("feet")};
	for (int32 Part = 0; Part < UE_ARRAY_COUNT(Parts); ++Part)
	{
		const FString Name = FString::Printf(TEXT("night4_reveal_%s"), Parts[Part]);
		AddLeg(TEXT("look:") + Name, [this, Four, Part, Name]()
		{
			const AIGMissingFloorNightFourDirector* Director4 = Four();
			if (!Director4 || !Director4->IsFinalRevealPlaying() || Director4->GetFinalRevealStage() != Part)
			{
				UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_NOTE %s passed_before_look stage=%d t=%.1f"), *Name,
					Director4 ? Director4->GetFinalRevealStage() : INDEX_NONE, GetWorld()->GetTimeSeconds());
				return;
			}
			Route.Aim(AIGMissingFloorNightFourDirector::GetRevealTarget(Part), TEXT("aim:") + Name);
			Route.Wait(0.6f, TEXT("wait:") + Name);
		});
		Stage(Name);
		WaitUntil([Four, Part]()
		{
			const AIGMissingFloorNightFourDirector* Director4 = Four();
			return !Director4 || !Director4->IsFinalRevealPlaying() || Director4->GetFinalRevealStage() > Part;
		}, 8.0f, Name + TEXT("_seen"));
	}

	// 오빠를 본 직후 눈이 감기고 「다섯 번째 새벽」이 2분 40초 돈다. 화면은 검고 소리와 자막만 남는다. 24초에
	// 머리 위 배관이 덜컹거리고, 58초에 벽 안에서 둘·쉬고·하나가 온다. 그 박자에 한 번 대답해 본다. 막간을
	// 이미 본 판이면 눈을 감지 않고 바로 목한수가 온다.
	WaitUntil([WeakThis, Interlude]()
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
		return !Narrative || Interlude() || Narrative->WasFifthDawnInterludeCompleted()
			|| Narrative->HasBeatPlayed(FName(TEXT("Night4.FinalConfrontation")));
	}, 12.0f, TEXT("night4_interlude"));
	const auto WaitInInterlude = [this, Interlude](const float Seconds, const FString& Name)
	{
		AddLeg(TEXT("wait:") + Name, [this, Interlude, Seconds, Name]()
		{
			if (Interlude())
			{
				Route.Wait(Seconds, TEXT("wait:") + Name);
			}
		});
	};
	WaitInInterlude(25.0f, TEXT("night4_interlude_pipe"));
	Stage(TEXT("night4_interlude"));
	WaitInInterlude(33.5f, TEXT("night4_interlude_call"));
	Stage(TEXT("night4_interlude_call"));
	KnockAnswer(TEXT("night4_interlude_answer"));
}

void AIGFullPlaythroughProbe::KnockAnswer(const FString& Name)
{
	AddLeg(TEXT("knock:") + Name, [this, Name]()
	{
		Route.Press(0.08f, TEXT("key:") + Name + TEXT("_tap1"), nullptr, EKeys::Q);
		Route.Wait(0.32f, TEXT("wait:") + Name + TEXT("_gap1"));
		Route.Press(0.08f, TEXT("key:") + Name + TEXT("_tap2"), nullptr, EKeys::Q);
		Route.Wait(0.72f, TEXT("wait:") + Name + TEXT("_gap2"));
		Route.Press(0.08f, TEXT("key:") + Name + TEXT("_tap3"), nullptr, EKeys::Q);
	});
}

void AIGFullPlaythroughProbe::BuildEnding()
{
	TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
	const bool bEndingB = EndingChoice == TEXT("B");
	const FName Expected(bEndingB ? TEXT("Ending.B") : TEXT("Ending.A"));
	Stage(TEXT("ending_choice"));
	UseNamed(bEndingB ? TEXT("ending_b") : TEXT("ending_a"),
		bEndingB ? TEXT("MissingFloorEndingBTarget") : TEXT("MissingFloorEndingATarget"), FVector::ZeroVector,
		[WeakThis, Expected]()
		{
			const UIGMissingFloorNarrativeSubsystem* Narrative = WeakThis.IsValid() ? WeakThis->GetNarrative() : nullptr;
			return Narrative && Narrative->GetEndingChoice() == Expected;
		}, 8.0f);
	if (bEndingB)
	{
		// 녹음을 끄고 곁에 앉는다. 곁의 벽에 대고 오빠의 박자(둘, 쉬고, 하나)로 대답하면 복도 끝에서 두 번
		// 돌아온다. 두 번째 대답 뒤에 05:30의 잠금 소리와 112 연결음이 오고 에필로그가 열린다.
		GoTo(FVector(150.0f, 735.0f, AIGPrologueWorldScene::RoofZ), 25.0f, TEXT("ending_b_beside"));
		Crouch(TEXT("ending_b_sit"));
		// 앉아서 오빠 쪽을 본다. 고른 자리에서 그대로 앉으면 북쪽 석고보드 더미만 보였다.
		AddLeg(TEXT("look:ending_b_beside"), [this]()
		{
			Route.Aim(AIGMissingFloorNightFourDirector::GetRevealTarget(1), TEXT("aim:ending_b_beside"));
		});
		Stage(TEXT("ending_b_vigil"));
		for (int32 Reply = 1; Reply <= 2; ++Reply)
		{
			const FString Name = FString::Printf(TEXT("ending_b_answer%d"), Reply);
			AddLeg(TEXT("reach:") + Name, [this, Name]()
			{
				const AIGPlayerCharacter* Character = Player.Get();
				const float Reach = Character
					? FVector::Dist(Character->GetActorLocation(), AIGMissingFloorNightFourDirector::GetVigilKnockLocation())
					: -1.0f;
				UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_VIGIL %s reach=%.0f"), *Name, Reach);
			});
			KnockAnswer(Name);
			WaitUntil([WeakThis, Reply]()
			{
				const AIGMissingFloorNightFourDirector* Four = WeakThis.IsValid() ? WeakThis->FindFirst<AIGMissingFloorNightFourDirector>() : nullptr;
				return Four && Four->GetVigilReplyCount() >= Reply;
			}, 8.0f, Name + TEXT("_reply"));
			Listen(Name, 6.0f);
		}
		Stage(TEXT("ending_b_answered"));
		WaitUntil([WeakThis]()
		{
			const AIGMissingFloorNightFourDirector* Four = WeakThis.IsValid() ? WeakThis->FindFirst<AIGMissingFloorNightFourDirector>() : nullptr;
			return Four && !Four->IsEndingBVigilActive();
		}, 30.0f, TEXT("ending_b_dawn"));
	}
	Stage(TEXT("ending"));
	WaitUntil([WeakThis]() { return WeakThis.IsValid() && WeakThis->bEpilogueDone; }, 600.0f, TEXT("epilogue_done"));
	Stage(TEXT("ending_done"));
	// 에필로그가 끝나면 타이틀이 뜬다. 메뉴가 다 그려진 화면을 남긴다.
	WaitSeconds(2.5f, TEXT("title_settle"));
	Stage(TEXT("ending_title"));
}

TFunction<void()> AIGFullPlaythroughProbe::MakeWallStep(const int32 Round)
{
	return [this, Round]()
	{
		// 벽은 게임이 다음에 손댈 자리를 내놓는다(살펴보기, 이음매, 덧판, 버팀, 나사, 고정쇠). 덧댄 것이 움직이거나
		// 부름을 기다리는 동안에는 쓸 데가 없어 잠깐 기다린다. 열리면 끝낸다.
		const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
		if (!Narrative || Narrative->IsNightFourWallOpened() || Round >= IGFullPlaythrough::MaxWallRounds)
		{
			return;
		}
		Legs.Insert(FLeg{FString::Printf(TEXT("wall:%d"), Round + 1), MakeWallStep(Round + 1)}, LegIndex + 1);
		AIGMissingFloorNightFourDirector* Four = FindFirst<AIGMissingFloorNightFourDirector>();
		AIGMissingFloorEvidence* Next = Four ? Four->GetNextWallActionForTesting() : nullptr;
		if (!Next)
		{
			Route.Wait(1.0f, TEXT("wait:wall_busy"));
			return;
		}
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_WALL round=%d target=%s progress=%d"), Round, *Next->GetName(),
			Narrative->GetNightFourWallProgress());
		TWeakObjectPtr<AIGMissingFloorEvidence> WeakNext(Next);
		TWeakObjectPtr<AIGMissingFloorNightFourDirector> WeakFour(Four);
		Legs.Insert(FLeg{TEXT("use:night4_wall"), MakeUse(TEXT("night4_wall"), [WeakNext]() -> AActor* { return WeakNext.Get(); },
			FVector::ZeroVector, [WeakFour, WeakNext]()
			{
				const AIGMissingFloorNightFourDirector* Director4 = WeakFour.Get();
				return !Director4 || Director4->GetNextWallActionForTesting() != WeakNext.Get();
			}, 8.0f, TOptional<FVector>(), 1, 0)}, LegIndex + 1);
	};
}

void AIGFullPlaythroughProbe::BuildStages()
{
	// 이어 하기로 불러온 판은 그 자리의 단계부터 짓는다. 밤 번호와 밤이 진행 중인지로 고른다.
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const int32 Night = Narrative ? Narrative->GetNightIndex() : 0;
	const bool bHour = NightPhase.IsValid() && NightPhase->IsHourActive();
	const int32 First = Night <= 0 ? 0 : (bHour ? Night * 2 - 1 : Night * 2);
	void (AIGFullPlaythroughProbe::*const Builders[])() = {
		&AIGFullPlaythroughProbe::BuildArrival,
		&AIGFullPlaythroughProbe::BuildNightOne,
		&AIGFullPlaythroughProbe::BuildDayOne,
		&AIGFullPlaythroughProbe::BuildNightTwo,
		&AIGFullPlaythroughProbe::BuildDayTwo,
		&AIGFullPlaythroughProbe::BuildNightThree,
		&AIGFullPlaythroughProbe::BuildDayThree,
		&AIGFullPlaythroughProbe::BuildNightFour,
	};
	UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_FROM night=%d hour=%d first=%d"), Night, bHour ? 1 : 0, First);
	// 이어 한 판이면 불러온 이야기가 어디까지인지 적는다. 저장에서 무엇이 돌아왔는지 볼 수 있다.
	if (Narrative)
	{
		FString Truths;
		const UEnum* TruthEnum = StaticEnum<EIGMissingFloorTruth>();
		for (int32 Index = 0; TruthEnum && Index < TruthEnum->NumEnums() - 1; ++Index)
		{
			const EIGMissingFloorTruth Truth = static_cast<EIGMissingFloorTruth>(TruthEnum->GetValueByIndex(Index));
			if (Truth != EIGMissingFloorTruth::None && Narrative->HasTruth(Truth))
			{
				Truths += (Truths.IsEmpty() ? TEXT("") : TEXT(",")) + TruthEnum->GetNameStringByIndex(Index);
			}
		}
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_STATE truths=%s first_report=%d site_check=%d eviction=%d deadline=%d"),
			*Truths, Narrative->WasFirstReportMade() ? 1 : 0, HasBeat(TEXT("Night3.SiteCheck")) ? 1 : 0,
			HasBeat(TEXT("Day.EvictionPosted")) ? 1 : 0, HasBeat(TEXT("Day.EvictionDeadline")) ? 1 : 0);
	}
	for (int32 Index = FMath::Clamp(First, 0, UE_ARRAY_COUNT(Builders)); Index < UE_ARRAY_COUNT(Builders); ++Index)
	{
		(this->*Builders[Index])();
	}
}

void AIGFullPlaythroughProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFinished)
	{
		return;
	}
	// 멈춘 게임에서는 아무것도 누르지 않는다. 에필로그가 끝나 타이틀이 게임을 멈춘 뒤에만 남은 마무리를 한다.
	if (GetWorld()->IsPaused() && !bEpilogueDone)
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
		// 에필로그가 끝나는 것은 방송으로만 안다.
		if (AIGMissingFloorEpilogueDirector* Epilogue = FindFirst<AIGMissingFloorEpilogueDirector>())
		{
			TWeakObjectPtr<AIGFullPlaythroughProbe> WeakThis(this);
			Epilogue->OnCompleted.AddWeakLambda(this, [WeakThis]()
			{
				if (WeakThis.IsValid())
				{
					WeakThis->bEpilogueDone = true;
				}
			});
		}
		BuildStages();
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
			// 곁에 있으면 그의 손이 닿는지(몸 가운데끼리의 눈길)도 적는다. 닿으면 듣기만 하는 밤에는 물러난다.
			const float Apart = static_cast<float>(FVector::Dist(Entity->GetActorLocation(), Character->GetActorLocation()));
			if (Apart <= 200.0f)
			{
				FCollisionQueryParams ReachQuery(SCENE_QUERY_STAT(FullPlaythroughReach), false, Entity);
				ReachQuery.AddIgnoredActor(Character);
				FHitResult ReachHit;
				Upstairs += GetWorld()->LineTraceSingleByChannel(ReachHit, Entity->GetActorLocation(),
						Character->GetActorLocation(), ECC_Visibility, ReachQuery)
					? FString::Printf(TEXT(" apart=%.0f reach_block=%s/%s at=%s"), Apart, *GetNameSafe(ReachHit.GetActor()),
						*GetNameSafe(ReachHit.GetComponent()), *ReachHit.ImpactPoint.ToCompactString())
					: FString::Printf(TEXT(" apart=%.0f reach=clear"), Apart);
			}
		}
		UE_LOG(LogIndieGame, Display, TEXT("PLAYTHROUGH_AT t=%.1f at=%s leg=%s step=%s upstairs=%s"), TotalSeconds,
			*Character->GetActorLocation().ToCompactString(), *CurrentLeg, *Route.GetCurrentName(), *Upstairs);
	}
}
