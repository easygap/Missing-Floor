#include "Entity/IGMissingFloorLeftoverDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGJeongwooFigure.h"
#include "Entity/IGLiningEntity.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGManagerPatrol.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorNightTwoBeatDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "Interaction/IGAccessHatch.h"
#include "Interaction/IGLeftoverTarget.h"
#include "Interaction/IGReadableNote.h"
#include "Interaction/IGSwingDoor.h"
#include "Materials/MaterialInterface.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "TimerManager.h"

namespace IGLeftover
{
	// 공간 이름. 덧댄 것의 틈 Id와 점검구 Id의 앞머리다.
	const FName SpaceInner(TEXT("Inner"));
	const FName SpaceStorage(TEXT("Storage"));
	const FName SpaceAnnex(TEXT("Annex"));

	// 틈.
	const FName GapInnerSmall(TEXT("Inner.Small"));
	const FName GapInnerLarge(TEXT("Inner.Large"));
	const FName GapStorageNarrow(TEXT("Storage.Narrow"));
	const FName GapStorageLarge(TEXT("Storage.Large"));
	const FName GapStorageAlt(TEXT("Storage.Alt"));

	// 물건.
	const FName PieceA(TEXT("PieceA"));
	const FName PieceB(TEXT("PieceB"));
	const FName PieceC(TEXT("PieceC"));
	const FName Frame(TEXT("Frame"));
	const FName Gloves(TEXT("JeongwooGloves"));
	const FName GlovesGiven(TEXT("JeongwooGlovesGiven"));
	const FName ToolPouch(TEXT("ToolPouch"));
	const FName DohaBox(TEXT("DohaBox"));
	const FName Cover(TEXT("Cover"));
	const FName Wedge(TEXT("Wedge"));

	// 비트.
	const FName BeatDayOneBoxes(TEXT("Leftover.Day1.Boxes"));
	const FName BeatNoticeRead(TEXT("Leftover.NoticeRead"));
	const FName BeatNightTwoErrand(TEXT("Leftover.N2.Errand"));
	const FName BeatNightTwoMissed(TEXT("Leftover.N2.Missed"));
	const FName BeatFirstMove(TEXT("Leftover.N2.FirstMove"));
	const FName BeatSecondMove(TEXT("Leftover.N2.SecondMove"));
	const FName BeatDayTwoTalk(TEXT("Leftover.Day2.Talk"));
	const FName BeatDayThreeEmptied(TEXT("Leftover.Day3.Emptied"));
	const FName BeatDayThreeText(TEXT("Leftover.Day3.JeongwooText"));
	const FName BeatSeenMoveHint(TEXT("Leftover.SeenMove"));
	const FName JeongwooIntroBeat(TEXT("Day.Jeongwoo.Intro"));

	// 정우 상태.
	const FName JeongwooLeftTogether(TEXT("LeftTogether"));
	const FName JeongwooSelfEscaped(TEXT("SelfEscaped"));

	// -- 관리실 안쪽 방(1층) ------------------------------------------------------
	/** 문짝 경첩 축. 문틀 개구부 X 202..278, 문 닫힘 턱이 관리실 쪽(Y -82)에 선다. */
	const FVector InnerDoorHinge(205.0f, -80.2f, 0.0f);
	constexpr float InnerDoorOpenYaw = 95.0f;
	/** 점검구 둘. 액터 +X가 방 쪽 법선. */
	const FVector InnerSmallHatch(-100.0f, 85.0f, 140.0f);
	const FVector InnerLargeHatch(190.0f, 170.0f, 60.0f);
	/** 덧댄 것이 북벽에 접혀 있는 자리(벽 면). */
	const FVector InnerLiningRest(50.0f, 170.0f, 112.0f);
	/** 눌렸다 깨어나는 넓은 자리. 문 앞 빈 바닥. */
	const FVector InnerRecovery(150.0f, 15.0f, 100.0f);
	const FVector CartHome(-205.0f, -285.0f, 0.0f);
	constexpr float CartHomeYaw = 270.0f;
	const FVector CartPark(300.0f, -275.0f, 0.0f);
	constexpr float CartParkYaw = 180.0f;
	/** 사슬을 푸는 시간. 깨는 장면(약 7초)과 오가는 길(약 20초)을 더하면 안쪽 방에 3분 남짓이다. */
	constexpr float ErrandFetchSeconds = 125.0f;
	constexpr float WakeToDoorSeconds = 7.5f;

	// -- 302호(3층 바닥 600) -------------------------------------------------------
	const FVector StorageNarrowGap(5.0f, 85.0f, 675.0f);
	const FVector StorageLargeHatch(-160.0f, 0.0f, 660.0f);
	const FVector StorageAltHatch(120.0f, -150.0f, 645.0f);
	const FVector StorageRecovery(-40.0f, -120.0f, 700.0f);

	// -- 5층 -------------------------------------------------------------------
	const FVector AnnexRecovery(60.0f, 640.0f, 1300.0f);

	// -- 낮 ---------------------------------------------------------------------
	/** 1층 게시판 옆, 수거 안내 자리(로비 북벽 안쪽 면 Y -148.65). */
	const FVector CollectionNoticeSpot(568.0f, -148.6f, 150.0f);
	/** 필로티 적치 자리(뒷벽 앞). 낮2와 밤3 동안 마대와 상자가 쌓인다. */
	const FVector LoadingSpot(-150.0f, -262.0f, 0.0f);

	constexpr double DayOneDoorWaitSeconds = 26.0;

	UStaticMesh* Mesh(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(
			nullptr, *FString::Printf(TEXT("/Game/Meshes/%s.%s"), Name, Name), nullptr, LOAD_NoWarn);
	}

	const FText& JeongwooName()
	{
		static const FText Name = NSLOCTEXT("IGMissingFloor", "JeongwooSpeaker", "정우");
		return Name;
	}
	const FText& RiderName()
	{
		static const FText Name = NSLOCTEXT("IGMissingFloor", "ScooterRiderSpeaker", "배달 기사");
		return Name;
	}
	const FText& YudamName()
	{
		static const FText Name = NSLOCTEXT("IGMissingFloor", "YudamCounterSpeaker", "백유담");
		return Name;
	}
}

AIGMissingFloorLeftoverDirector::AIGMissingFloorLeftoverDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	PropRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PropRoot"));
	SetRootComponent(PropRoot);
	PropRoot->SetMobility(EComponentMobility::Movable);
}

bool AIGMissingFloorLeftoverDirector::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGListenerEntity* InListener,
	AIGNightLoopDirector* InNightLoop,
	AIGNightPhaseDirector* InNightPhase,
	AIGMissingFloorNightTwoBeatDirector* InNightTwoBeats,
	AIGManagerPatrol* InManager)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
	NightLoop = InNightLoop;
	NightPhase = InNightPhase;
	NightTwoBeats = InNightTwoBeats;
	Manager = InManager;
	UWorld* World = GetWorld();
	if (!World || !Scene || !Player)
	{
		return false;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.Name = TEXT("MissingFloorLiningEntity");
	Lining = World->SpawnActor<AIGLiningEntity>(AIGLiningEntity::StaticClass(), FTransform::Identity, Parameters);
	if (Lining)
	{
		Lining->OnGrippedPlayer.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleGripped);
		Lining->OnGapSealed.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleLiningSealed);
		Lining->OnTargetChosen.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleLiningTarget);
		Lining->OnFirstMove.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleLiningFirstMove);
		Lining->Hide();
	}
	Parameters.Name = TEXT("MissingFloorJeongwooFigure");
	Jeongwoo = World->SpawnActor<AIGJeongwooFigure>(AIGJeongwooFigure::StaticClass(), FTransform::Identity, Parameters);
	if (Jeongwoo)
	{
		Jeongwoo->OnArrived.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleJeongwooArrived);
	}
	for (TActorIterator<AIGSwingDoor> It(World); It; ++It)
	{
		if (It->GetFName() == FName(TEXT("MissingFloorBoothDoor")))
		{
			BoothDoor = *It;
			break;
		}
	}
	if (NightLoop)
	{
		CaptureSettledHandle = NightLoop->OnCaptureSettled.AddUObject(
			this, &AIGMissingFloorLeftoverDirector::HandleCaptureSettled);
	}
	if (Manager)
	{
		Manager->OnErrandOutside.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleErrandOutside);
		Manager->OnErrandReturning.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleErrandReturning);
		Manager->OnErrandFinished.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleErrandFinished);
	}
	BuildInnerRoom();
	BuildDayProps();
	BuildStorage();
	BuildAssembly();
	bConfigured = true;
	RefreshAll();
	World->GetTimerManager().SetTimer(
		PollTimer, this, &AIGMissingFloorLeftoverDirector::PollPlayer, 0.25f, true);
	return true;
}

void AIGMissingFloorLeftoverDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	if (NightLoop)
	{
		NightLoop->OnCaptureSettled.Remove(CaptureSettledHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AIGMissingFloorLeftoverDirector::Destroyed()
{
	// 스테이지를 다시 세울 때 같은 이름으로 스폰한다. 남겨 두면 그 이름에 막힌다.
	TArray<AActor*> Owned = {Lining.Get(), Jeongwoo.Get(), InnerDoor.Get(), Cart.Get(),
		CollectionNotice.Get(), CabinetLedger.Get()};
	for (const TPair<FName, TObjectPtr<AIGAccessHatch>>& Pair : Hatches)
	{
		Owned.Add(Pair.Value.Get());
	}
	for (const TPair<FName, TObjectPtr<AIGLeftoverTarget>>& Pair : Targets)
	{
		Owned.Add(Pair.Value.Get());
	}
	for (AActor* Actor : Owned)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	Hatches.Reset();
	Targets.Reset();
	Super::Destroyed();
}

// -- 공통 ----------------------------------------------------------------------

UIGMissingFloorNarrativeSubsystem* AIGMissingFloorLeftoverDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
}

int32 AIGMissingFloorLeftoverDirector::GetNightIndex() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative ? Narrative->GetNightIndex() : 0;
}

bool AIGMissingFloorLeftoverDirector::IsHourActive() const
{
	return bHourActive;
}

bool AIGMissingFloorLeftoverDirector::IsLegacyRun() const
{
	// 확장 전 저장도 이어 하는 그 자리부터의 장면은 다 겪는다(못 본 장면을 본 것으로 적지 않는다).
	// 밤4에 이미 망치질을 시작한 판만 예전 다섯 번 순서로 끝낸다. 그 밤에는 이 감독이 나서지 않는다.
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return !Narrative || (Narrative->GetNightIndex() >= 4 && Narrative->GetLeftover().bLegacyWallSequence);
}

bool AIGMissingFloorLeftoverDirector::HasTaken(const FName Item) const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative && Narrative->HasLeftover(EIGLeftoverList::Taken, Item);
}

void AIGMissingFloorLeftoverDirector::Take(const FName Item)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->AddLeftover(EIGLeftoverList::Taken, Item);
		Narrative->AddLeftover(EIGLeftoverList::Examined, Item);
	}
	UE_LOG(LogTemp, Display, TEXT("LEFTOVER_TAKEN %s"), *Item.ToString());
}

bool AIGMissingFloorLeftoverDirector::HasExamined(const FName Item) const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative && Narrative->HasLeftover(EIGLeftoverList::Examined, Item);
}

void AIGMissingFloorLeftoverDirector::MarkExamined(const FName Item)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->AddLeftover(EIGLeftoverList::Examined, Item);
	}
}

bool AIGMissingFloorLeftoverDirector::PlayBeat(const FName Beat)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative && Narrative->MarkBeatPlayed(Beat);
}

bool AIGMissingFloorLeftoverDirector::HasBeat(const FName Beat) const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative && Narrative->HasBeatPlayed(Beat);
}

void AIGMissingFloorLeftoverDirector::Say(const FText& Speaker, const FText& Line, const float MinSeconds)
{
	AIGHorrorHUD::PushDialogue(this, Speaker, Line, EIGDialogueChannel::Conversation, MinSeconds, EIGDialoguePriority::Story);
}

void AIGMissingFloorLeftoverDirector::Think(const FText& Line, const float Seconds)
{
	AIGHorrorHUD::PushThought(this, Line, Seconds);
}

void AIGMissingFloorLeftoverDirector::Caption(const FText& Line, const FVector& Where, const float Seconds)
{
	AIGHorrorHUD::PushAudioCaptionAt(this, Line, Seconds, Where);
}

void AIGMissingFloorLeftoverDirector::SayIn(const FName Context, const FText& Speaker, const FText& Line, const bool bStory)
{
	FIGDialogueScope Scope;
	Scope.Context = Context;
	Scope.Owner = this;
	Scope.Lifetime = bStory ? EIGDialogueLifetime::Story : EIGDialogueLifetime::Reaction;
	AIGHorrorHUD::PushDialogueScoped(this, Speaker, Line, EIGDialogueChannel::Conversation, 0.0f,
		EIGDialoguePriority::Story, MoveTemp(Scope));
}

void AIGMissingFloorLeftoverDirector::ThinkIn(const FName Context, const FText& Line, const float Seconds, const bool bStory)
{
	FIGDialogueScope Scope;
	Scope.Context = Context;
	Scope.Owner = this;
	Scope.Lifetime = bStory ? EIGDialogueLifetime::Story : EIGDialogueLifetime::Reaction;
	AIGHorrorHUD::PushThoughtScoped(this, Line, Seconds, MoveTemp(Scope));
}

void AIGMissingFloorLeftoverDirector::CaptionIn(const FName Context, const FText& Line, const FVector& Where, const float Seconds)
{
	AIGHorrorHUD::PushAudioCaptionScoped(this, Line, Seconds, Where, Context);
}

void AIGMissingFloorLeftoverDirector::SayThen(
	const FName Context, const FText& Speaker, const FText& Line, const float FallbackSeconds, TFunction<void()> Then)
{
	// 말이 끝나는 쪽과 대비 타이머 가운데 먼저 오는 쪽이 한 번만 부른다.
	TSharedRef<bool> bDone = MakeShared<bool>(false);
	TSharedRef<TFunction<void()>> Next = MakeShared<TFunction<void()>>(MoveTemp(Then));
	TWeakObjectPtr<AIGMissingFloorLeftoverDirector> WeakThis(this);
	const auto Fire = [WeakThis, bDone, Next]()
	{
		if (*bDone || !WeakThis.IsValid())
		{
			return;
		}
		*bDone = true;
		if (*Next)
		{
			(*Next)();
		}
	};
	FIGDialogueScope Scope;
	Scope.Context = Context;
	Scope.Owner = this;
	Scope.Lifetime = EIGDialogueLifetime::Story;
	Scope.OnEnded = [Fire](EIGDialogueEndReason) { Fire(); };
	AIGHorrorHUD::PushDialogueScoped(this, Speaker, Line, EIGDialogueChannel::Conversation, 0.0f,
		EIGDialoguePriority::Story, MoveTemp(Scope));
	FTimerHandle Fallback;
	GetWorldTimerManager().SetTimer(Fallback, FTimerDelegate::CreateWeakLambda(this, [Fire]() { Fire(); }),
		FMath::Max(0.1f, FallbackSeconds), false);
}

void AIGMissingFloorLeftoverDirector::Hush(const FName Context)
{
	AIGHorrorHUD::CancelDialogue(this, Context);
}

void AIGMissingFloorLeftoverDirector::PlaySound(
	USoundBase* Sound, const FVector& Where, const float Volume, const float Pitch, const float Inner, const float Outer)
{
	if (Sound)
	{
		IGAudio::SpawnOneShotAt(this, Sound, Where, Volume, Pitch, Inner, Outer, EIGAudioBus::World);
	}
}

void AIGMissingFloorLeftoverDirector::PhoneBuzz()
{
	if (Player)
	{
		IGAudio::SpawnOneShotAt(
			this, UIGToneSequenceSoundWave::CreatePhoneVibrationUnfinished(this),
			Player->GetActorLocation() + FVector(0.0f, 0.0f, 20.0f), 0.55f, 1.0f, 60.0f, 400.0f, EIGAudioBus::World);
	}
}

FVector AIGMissingFloorLeftoverDirector::GetPlayerFeet() const
{
	if (!Player)
	{
		return FVector(0.0f, 0.0f, -100000.0f);
	}
	// 앉으면 캡슐 반높이가 줄어든다. 서 있을 때 값(92)으로 빼면 발이 바닥 밑으로 내려가 방 밖으로 친다.
	const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	return Player->GetActorLocation() - FVector(0.0f, 0.0f, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 92.0f);
}

bool AIGMissingFloorLeftoverDirector::IsPlayerNear(const FVector& Where, const float Radius, const float MaxHeight) const
{
	const FVector Feet = GetPlayerFeet();
	return FVector::Dist2D(Feet, Where) <= Radius && FMath::Abs(Feet.Z - Where.Z) <= MaxHeight;
}

AIGLeftoverTarget* AIGMissingFloorLeftoverDirector::SpawnTarget(
	const FName Key,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& Extent,
	const FText& Prompt,
	const float HoldSeconds,
	const float Noise)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.Name = FName(*FString::Printf(TEXT("MissingFloorLeftover_%s"), *Key.ToString().Replace(TEXT("."), TEXT("_"))));
	AIGLeftoverTarget* Target = World->SpawnActor<AIGLeftoverTarget>(
		AIGLeftoverTarget::StaticClass(), FTransform(Rotation, Location), Parameters);
	if (!Target)
	{
		return nullptr;
	}
	Target->Key = Key;
	Target->Configure(Extent, Prompt, HoldSeconds, Noise);
	Targets.Add(Key, Target);
	return Target;
}

UStaticMeshComponent* AIGMissingFloorLeftoverDirector::SpawnProp(
	const FName Key, UStaticMesh* PropMesh, const FTransform& Transform, const bool bCollision)
{
	if (!PropMesh)
	{
		return nullptr;
	}
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, FName(*FString::Printf(TEXT("Prop_%s"), *Key.ToString().Replace(TEXT("."), TEXT("_")))));
	Component->SetupAttachment(PropRoot);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(PropMesh);
	Component->SetWorldTransform(Transform);
	Component->SetCanEverAffectNavigation(false);
	Component->SetGenerateOverlapEvents(false);
	Component->bDisallowNanite = true;
	if (bCollision)
	{
		// 몸과 덧댄 것의 짚기 검사는 막고, 상호작용 시선(Visibility)은 지나간다. 마대 열기·상자 당기기·조립대
		// 조각 같은 대상이 소품 안이나 겉면에 걸쳐 있어서 시선을 막으면 겨눌 수 없다.
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		// 상자·탁자·마대는 밟고 올라서는 물건이 아니다. 문간 옆 낮은 탁자 모서리를 밟고 올라서면
		// 머리가 문틀에 걸려 302호를 나갈 수 없었다.
		Component->CanCharacterStepUpOn = ECB_No;
		BlockingProps.Add(Key);
	}
	else
	{
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Component->RegisterComponent();
	Component->SetWorldTransform(Transform);
	Props.Add(Key, Component);
	return Component;
}

void AIGMissingFloorLeftoverDirector::ShowProp(const FName Key, const bool bShow)
{
	if (const TObjectPtr<UStaticMeshComponent>* Found = Props.Find(Key); Found && *Found)
	{
		(*Found)->SetVisibility(bShow, true);
		(*Found)->SetHiddenInGame(!bShow, true);
		// 프로필 이름으로 가르면 한 번 숨긴 뒤에는 Custom이 되어 다시 보여도 충돌이 안 돌아온다.
		(*Found)->SetCollisionEnabled(bShow && BlockingProps.Contains(Key) ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}

void AIGMissingFloorLeftoverDirector::SetTargetLive(AIGLeftoverTarget* Target, const bool bLive)
{
	if (!Target)
	{
		return;
	}
	Target->SetInteractionEnabled(bLive);
	Target->SetActorEnableCollision(bLive);
	if (UStaticMeshComponent* Visual = Target->GetVisual())
	{
		Visual->SetVisibility(bLive, true);
	}
}

AIGLeftoverTarget* AIGMissingFloorLeftoverDirector::FindTarget(const FName Key) const
{
	const TObjectPtr<AIGLeftoverTarget>* Found = Targets.Find(Key);
	return Found ? Found->Get() : nullptr;
}

AIGAccessHatch* AIGMissingFloorLeftoverDirector::GetHatch(const FName Id) const
{
	const TObjectPtr<AIGAccessHatch>* Found = Hatches.Find(Id);
	return Found ? Found->Get() : nullptr;
}

FName AIGMissingFloorLeftoverDirector::HatchId(const AIGAccessHatch* Hatch) const
{
	for (const TPair<FName, TObjectPtr<AIGAccessHatch>>& Pair : Hatches)
	{
		if (Pair.Value == Hatch)
		{
			return Pair.Key;
		}
	}
	return NAME_None;
}

bool AIGMissingFloorLeftoverDirector::IsSceneActive() const
{
	// 판이 풀려 정우가 밀고 나오는 1~2초(Released)도 장면이다. 그 사이 새벽이 오면 구출이 혼자
	// 빠져나간 것으로 적혔다.
	return NightTwoStage == ENightTwo::Waking
		|| (DayOneStage != EDayOne::Idle && DayOneStage != EDayOne::Done)
		|| StorageStage == EStorage::Released
		|| StorageStage == EStorage::Bracing
		|| StorageStage == EStorage::Leaving;
}

bool AIGMissingFloorLeftoverDirector::IsAnySceneActive(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AIGMissingFloorLeftoverDirector> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->IsSceneActive())
		{
			return true;
		}
	}
	return false;
}

const FName AIGMissingFloorLeftoverDirector::SeenAnnexInspectionBeat(TEXT("Leftover.Seen.AnnexInspection"));
const FName AIGMissingFloorLeftoverDirector::SeenBraceFrameBeat(TEXT("Leftover.Seen.BraceFrame"));

bool AIGMissingFloorLeftoverDirector::IsHoldingDawn(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AIGMissingFloorLeftoverDirector> It(const_cast<UWorld*>(World)); It; ++It)
	{
		// 조각을 들고 자리를 뜨면 내려놓는다(PollPlayer). 들고 조립대 앞에 있을 때만 기다린다.
		if (It->IsSceneActive() || (It->HeldPiece != INDEX_NONE && It->IsPlayerAtAssemblyTable()))
		{
			return true;
		}
	}
	return false;
}

bool AIGMissingFloorLeftoverDirector::HasCoverEvidence() const
{
	return HasTaken(IGLeftover::PieceA) || HasTaken(IGLeftover::PieceB) || HasTaken(IGLeftover::PieceC)
		|| HasTaken(IGLeftover::Cover);
}

bool AIGMissingFloorLeftoverDirector::WasJeongwooRescuedTogether() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative && Narrative->GetLeftover().JeongwooState == IGLeftover::JeongwooLeftTogether;
}

bool AIGMissingFloorLeftoverDirector::IsInnerRoomErrandRunning() const
{
	return NightTwoStage == ENightTwo::Waking || NightTwoStage == ENightTwo::Errand;
}

void AIGMissingFloorLeftoverDirector::SetHourActive(const bool bActive)
{
	const bool bWasActive = bHourActive;
	bHourActive = bActive;
	// 밤3이 끝났는데 정우가 아직 판 뒤에 있다. 압이 풀린 새벽에 혼자 판을 밀고 나간다.
	if (bWasActive && !bActive)
	{
		PutDownHeldPiece();
		SelfEscapeAtDawn();
		if (NightTwoStage != ENightTwo::Done && NightTwoStage != ENightTwo::Idle)
		{
			NightTwoStage = ENightTwo::Done;
		}
	}
	if (!bActive)
	{
		NightTwoStage = ENightTwo::Idle;
		bReturnArmed = false;
	}
	RefreshAll();
}

void AIGMissingFloorLeftoverDirector::RefreshHomePouch()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Scene || IsLegacyRun())
	{
		return;
	}
	const bool bHome = Narrative->HasLeftover(EIGLeftoverList::Stored, IGLeftover::ToolPouch);
	const FName PropKey(TEXT("Home.Pouch"));
	const FName TargetKey(TEXT("Home.PouchLook"));
	if (bHome && !Props.Contains(PropKey))
	{
		// 계약서 옆, 책상 위. 지퍼 쪽이 그녀를 본다.
		const FVector Spot(-58.0f, -172.0f, Scene->GetDeskSurfaceWorldZ() + 0.2f);
		SpawnProp(PropKey, IGLeftover::Mesh(TEXT("SM_ToolPouch")), FTransform(FRotator(0.0f, 200.0f, 0.0f), Spot), true);
		if (AIGLeftoverTarget* Look = SpawnTarget(TargetKey, Spot + FVector(0.0f, 0.0f, 5.0f), FRotator(0.0f, 200.0f, 0.0f),
				FVector(16.0f, 10.0f, 6.0f), NSLOCTEXT("IGMissingFloor", "LeftoverHomePouchPrompt", "공구 주머니 살펴보기"), 0.0f, 0.04f))
		{
			Look->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleHomePouch);
		}
	}
	ShowProp(PropKey, bHome);
	SetTargetLive(FindTarget(TargetKey), bHome);
}

void AIGMissingFloorLeftoverDirector::HandleHomePouch(AIGLeftoverTarget* Target)
{
	// 생활 도구였던 주머니가 여기서는 흔적이 된다. 5층에서 튜닝 해머를 챙겼으면 빈 고리가 그 자리라는 것을
	// 안다. 못 챙겼으면 비어 있다는 것만 본다.
	const bool bHammer = HasBeat(FName(TEXT("Night3.WrenchTaken")));
	PlaySound(UIGToneSequenceSoundWave::CreateToolRollClink(this), Target ? Target->GetActorLocation() : FVector::ZeroVector, 0.35f);
	Think(bHammer
		? NSLOCTEXT("IGMissingFloor", "LeftoverHomePouchHammer", "고리 하나가 비어 있어. 5층에서 가져온 튜닝 해머가 꽂히던 자리야.")
		: NSLOCTEXT("IGMissingFloor", "LeftoverHomePouchEmpty", "고리 하나가 비어 있어. 손잡이 긴 공구를 꽂던 자리야."), 3.8f);
	PlayBeat(FName(TEXT("Leftover.PouchLoopSeen")));
}

void AIGMissingFloorLeftoverDirector::RefreshAll()
{
	if (!bConfigured)
	{
		return;
	}
	RefreshInnerRoom();
	RefreshDay();
	RefreshStorage();
	RefreshAssembly();
	PlaceLining();
}

void AIGMissingFloorLeftoverDirector::PollPlayer()
{
	if (!bConfigured)
	{
		return;
	}
	PollNightTwo();
	PollDayOneBoxes();
	PollStorage();
	UpdateJeongwooArrival();
	PollSightings();
	RefreshHomePouch();
	RefreshDeliveredBox();
	// 조각을 든 채 조립대를 떠나면 내려놓는다. 들고 다니는 물건이 아니다.
	if (HeldPiece != INDEX_NONE && !IsPlayerAtAssemblyTable())
	{
		PutDownHeldPiece();
	}
	// 들고 다니던 것은 403호에 들어서면 내려놓는다(Stored).
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		const FVector Feet = GetPlayerFeet();
		const bool bInHome = Feet.Z > AIGPrologueWorldScene::FourthFloorZ - 30.0f && Feet.Z < AIGPrologueWorldScene::FourthFloorZ + 60.0f
			&& Feet.X > -190.0f && Feet.X < 410.0f && Feet.Y > AIGPrologueWorldScene::HomeDoorY + 10.0f && Feet.Y < 215.0f;
		if (bInHome)
		{
			for (const FName Item : {IGLeftover::ToolPouch, IGLeftover::DohaBox, IGLeftover::Cover, IGLeftover::PieceA,
					 IGLeftover::PieceB, IGLeftover::PieceC, IGLeftover::Frame})
			{
				if (Narrative->HasLeftover(EIGLeftoverList::Taken, Item))
				{
					Narrative->AddLeftover(EIGLeftoverList::Stored, Item);
				}
			}
		}
	}
	// 덧댄 것이 움직이는 것을 한 번 봤으면 그 뒤로 힌트가 그 말을 쓴다.
	if (Lining && Lining->IsMoving() && Player && !HasBeat(IGLeftover::BeatSeenMoveHint))
	{
		const FVector Eye = Player->GetPawnViewLocation();
		if (FVector::Dist(Eye, Lining->GetBodyCenter()) < 900.0f
			&& FVector::DotProduct(Player->GetControlRotation().Vector(), (Lining->GetBodyCenter() - Eye).GetSafeNormal()) > 0.6f)
		{
			PlayBeat(IGLeftover::BeatSeenMoveHint);
		}
	}
}

void AIGMissingFloorLeftoverDirector::LoadGapsFor(const FName Space)
{
	if (!Lining)
	{
		return;
	}
	Lining->ClearGaps();
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const auto Sealed = [Narrative](const FName Id)
	{
		return Narrative && Narrative->HasLeftover(EIGLeftoverList::GapsSealed, Id);
	};
	const auto AddHatchGap = [this, &Sealed](const FName Id, const FVector& Up)
	{
		AIGAccessHatch* Hatch = GetHatch(Id);
		if (!Hatch)
		{
			return;
		}
		FIGLiningGap Gap;
		Gap.Id = Id;
		Gap.Center = Hatch->GetOpeningCenter();
		Gap.Normal = Hatch->GetOpeningNormal();
		Gap.Up = Up;
		Gap.Size = Hatch->GetOpeningSize();
		Gap.Hatch = Hatch;
		Gap.bSealed = Sealed(Id);
		Lining->SetGap(Gap);
	};
	if (Space == IGLeftover::SpaceInner)
	{
		AddHatchGap(IGLeftover::GapInnerSmall, FVector::UpVector);
		AddHatchGap(IGLeftover::GapInnerLarge, FVector::UpVector);
		Lining->SetGripRecoveryTransform(FTransform(FRotator(0.0f, 180.0f, 0.0f), IGLeftover::InnerRecovery));
	}
	else if (Space == IGLeftover::SpaceStorage)
	{
		FIGLiningGap Narrow;
		Narrow.Id = IGLeftover::GapStorageNarrow;
		Narrow.Center = IGLeftover::StorageNarrowGap;
		Narrow.Normal = FVector(0.0f, -1.0f, 0.0f);
		Narrow.Up = FVector::UpVector;
		Narrow.Size = FVector2D(30.0f, 110.0f);
		// 틈 동쪽 바로 옆에 마감판(서쪽 끝 X 25..48)이 기울어 서 있다. 120 cm 몸이 틈 가운데에 오면 동쪽
		// 3분의 1이 판을 뚫어서, 서쪽으로 36 cm 비켜 판 서쪽 끝에 몸을 붙인다(몸 X -96..24, 틈 X -10..20을
		// 다 덮는다). 틈 밑이 바닥에서 20 cm라 아래 다리는 접는다.
		Narrow.BodyOffset = FVector(-36.0f, 0.0f, 0.0f);
		Narrow.bTuckLowerLegs = true;
		Narrow.bAlwaysOpen = true;
		Narrow.bSealed = Sealed(IGLeftover::GapStorageNarrow);
		Lining->SetGap(Narrow);
		AddHatchGap(IGLeftover::GapStorageLarge, FVector::UpVector);
		AddHatchGap(IGLeftover::GapStorageAlt, FVector::UpVector);
		Lining->SetGripRecoveryTransform(FTransform(FRotator(0.0f, -90.0f, 0.0f), IGLeftover::StorageRecovery));
	}
	else if (Space == IGLeftover::SpaceAnnex)
	{
		Lining->SetGripRecoveryTransform(FTransform(FRotator(0.0f, 0.0f, 0.0f), IGLeftover::AnnexRecovery));
	}
}

void AIGMissingFloorLeftoverDirector::PlaceLining()
{
	if (!Lining || !Lining->LoadBody())
	{
		return;
	}
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const int32 Night = GetNightIndex();
	FName Space = NAME_None;
	if (bHourActive && !IsLegacyRun() && Narrative)
	{
		if (Night == 2)
		{
			Space = IGLeftover::SpaceInner;
		}
		else if (Night == 3)
		{
			Space = IGLeftover::SpaceStorage;
		}
		else if (Night == 4 && !Narrative->IsNightFourWallOpened() && !Narrative->GetLeftover().bLegacyWallSequence)
		{
			Space = IGLeftover::SpaceAnnex;
		}
	}
	if (Space == CurrentSpace && Space != NAME_None && !Lining->IsHidden())
	{
		return;
	}
	CurrentSpace = Space;
	if (Space == NAME_None)
	{
		Lining->ClearGaps();
		Lining->Hide();
		return;
	}
	LoadGapsFor(Space);
	const FName Held = Narrative ? Narrative->GetLeftover().LiningHeldGap : NAME_None;
	if (Space == IGLeftover::SpaceInner)
	{
		if (Held.ToString().StartsWith(TEXT("Inner.")) && Lining->FindGap(Held))
		{
			Lining->PlaceHolding(Held);
			PlayBeat(IGLeftover::BeatFirstMove);
		}
		else
		{
			Lining->RestOnSurface(IGLeftover::InnerLiningRest, FVector(0.0f, -1.0f, 0.0f), FVector::UpVector);
		}
		PlaceCoverPieceOnWall();
	}
	else if (Space == IGLeftover::SpaceStorage)
	{
		// 셋째 밤 04:30, 정우가 상자를 빼던 작업 틈을 눌러 막고 있다. 큰 점검구로 옮겨 갔던 저장이면 거기다.
		const FName Hold = Held.ToString().StartsWith(TEXT("Storage.")) && Lining->FindGap(Held)
			? Held : IGLeftover::GapStorageNarrow;
		Lining->PlaceHolding(Hold);
		if (UIGMissingFloorNarrativeSubsystem* Mutable = GetNarrative())
		{
			Mutable->AddLeftover(EIGLeftoverList::GapsSealed, Hold);
			Mutable->MutableLeftover().LiningHeldGap = Hold;
		}
	}
	else if (Space == IGLeftover::SpaceAnnex)
	{
		PrepareAnnexLining(Narrative ? Narrative->GetLeftover().WallStage : 0,
			Narrative && Narrative->GetLeftover().bFramePlaced);
	}
}

void AIGMissingFloorLeftoverDirector::HandleGripped(AIGLiningEntity* Entity)
{
	if (!NightLoop || !Player || !Entity)
	{
		return;
	}
	// 판과 몸 사이에 눌렸다. 몸이 물러난 자리 옆 넓은 데서 깬다. 진행은 그대로다.
	bLiningGripPending = true;
	FTransform Wake = FTransform(FRotator(0.0f, 180.0f, 0.0f), IGLeftover::InnerRecovery);
	if (CurrentSpace == IGLeftover::SpaceStorage)
	{
		Wake = FTransform(FRotator(0.0f, 90.0f, 0.0f), IGLeftover::StorageRecovery);
	}
	else if (CurrentSpace == IGLeftover::SpaceAnnex)
	{
		Wake = FTransform(FRotator::ZeroRotator, IGLeftover::AnnexRecovery);
	}
	Player->SetCaptureLookTarget(Entity, FVector::ZeroVector);
	NightLoop->RequestExternalCapture(Player, EIGCaptureKind::Lining, &Wake);
	if (!NightLoop->IsCaptureResetInFlight())
	{
		// 다른 포획이 이미 돌고 있었다. 누르기만 풀고 하던 일을 한다.
		bLiningGripPending = false;
		Entity->ReleaseGrip();
	}
}

void AIGMissingFloorLeftoverDirector::HandleCaptureSettled(const EIGCaptureKind Kind, const FVector& Spot)
{
	if (Kind == EIGCaptureKind::Lining || bLiningGripPending)
	{
		bLiningGripPending = false;
		if (Lining)
		{
			Lining->ReleaseGrip();
		}
	}
	// 밤2 심부름 중 관리인에게 잡혔으면 그가 집에 데려다 놓았다(HandleErrandFinished가 맡는다).
}

void AIGMissingFloorLeftoverDirector::HandleLiningSealed(AIGLiningEntity* Entity, const FName GapId)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->AddLeftover(EIGLeftoverList::GapsSealed, GapId);
		Narrative->MutableLeftover().LiningHeldGap = GapId;
	}
	if (GapId == IGLeftover::GapInnerLarge && HasBeat(IGLeftover::BeatFirstMove)
		&& PlayBeat(IGLeftover::BeatSecondMove))
	{
		Think(NSLOCTEXT("IGMissingFloor", "LeftoverMovedToLarge", "저리로 옮겨 갔어."), 3.0f);
	}
	if (CurrentSpace == IGLeftover::SpaceStorage)
	{
		RefreshStorage();
	}
}

void AIGMissingFloorLeftoverDirector::HandleLiningTarget(AIGLiningEntity* Entity, const FName GapId)
{
	UE_LOG(LogTemp, Display, TEXT("LEFTOVER_LINING_TARGET %s"), *GapId.ToString());
	// 302호 작업 틈을 놓고 다른 틈으로 떠나면 판이 풀린다.
	if (CurrentSpace == IGLeftover::SpaceStorage && GapId != IGLeftover::GapStorageNarrow
		&& StorageStage == EStorage::Trapped)
	{
		ReleaseBoard();
	}
}

void AIGMissingFloorLeftoverDirector::HandleLiningFirstMove(AIGLiningEntity* Entity)
{
	if (CurrentSpace == IGLeftover::SpaceInner && PlayBeat(IGLeftover::BeatFirstMove))
	{
		// 첫 조우. 벽에 붙어 있던 보강재 더미에서 패드 하나가 떨어지고 버팀다리가 드러난다.
		if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
		{
			Narrative->RecordWitness(EIGMissingFloorWitness::InnerRoomCoverPieces);
		}
		GetWorldTimerManager().SetTimer(WakeTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			Think(NSLOCTEXT("IGMissingFloor", "LeftoverFirstMove", "벽에 붙어 있던 게… 움직였어."), 3.2f);
		}), 2.4f, false);
	}
	PlaceCoverPieceOnWall();
}

void AIGMissingFloorLeftoverDirector::HandleHatchOpened(AIGAccessHatch* Hatch)
{
	const FName Id = HatchId(Hatch);
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative(); Narrative && !Id.IsNone())
	{
		Narrative->AddLeftover(EIGLeftoverList::HatchesOpen, Id);
	}
	if (Lining)
	{
		Lining->NotifyGapsChanged();
	}
	// 처음 연 점검구에서 바람이 빨려 든다. 정체는 말하지 않는다.
	if (Id == IGLeftover::GapInnerSmall || Id == IGLeftover::GapInnerLarge)
	{
		if (PlayBeat(FName(TEXT("Leftover.N2.HatchDraft"))))
		{
			Think(NSLOCTEXT("IGMissingFloor", "LeftoverHatchDraft", "바람이 안으로 빨려 들어가."), 2.6f);
		}
	}
}

void AIGMissingFloorLeftoverDirector::HandleHatchClosed(AIGAccessHatch* Hatch)
{
	const FName Id = HatchId(Hatch);
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative(); Narrative && !Id.IsNone())
	{
		Narrative->RemoveLeftover(EIGLeftoverList::HatchesOpen, Id);
	}
	if (Lining)
	{
		Lining->NotifyGapsChanged();
	}
}

void AIGMissingFloorLeftoverDirector::HandleHatchReopened(AIGAccessHatch* Hatch)
{
	const FName Id = HatchId(Hatch);
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative(); Narrative && !Id.IsNone())
	{
		Narrative->RemoveLeftover(EIGLeftoverList::GapsSealed, Id);
	}
	if (Lining && !Id.IsNone())
	{
		Lining->UnsealGap(Id);
	}
}

// -- 안쪽 방 ---------------------------------------------------------------------

void AIGMissingFloorLeftoverDirector::BuildInnerRoom()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 안쪽 방 문. 403호 욕실과 같은 ABS 문짝이고, 방 쪽 면에 방음재 판 둘을 대 붙였다.
	Parameters.Name = TEXT("MissingFloorInnerRoomDoor");
	InnerDoor = World->SpawnActor<AIGSwingDoor>(
		AIGSwingDoor::StaticClass(), FTransform(FRotator(0.0f, -90.0f, 0.0f), IGLeftover::InnerDoorHinge), Parameters);
	if (InnerDoor)
	{
		if (UStaticMesh* Leaf = IGLeftover::Mesh(TEXT("SM_BathroomDoorLeaf")))
		{
			InnerDoor->ConfigureAuthoredLeaf(Leaf, nullptr, FVector(3.6f, 70.0f, 199.0f));
		}
		InnerDoor->SetOpenYaw(IGLeftover::InnerDoorOpenYaw);
		InnerDoor->SetHollowLeaf(true);
		if (UStaticMesh* Foam = IGLeftover::Mesh(TEXT("SM_LinedWallPanelA")))
		{
			for (const float Bottom : {4.0f, 98.0f})
			{
				UStaticMeshComponent* Panel = NewObject<UStaticMeshComponent>(InnerDoor);
				Panel->SetupAttachment(InnerDoor->GetDoorPivot());
				Panel->SetStaticMesh(Foam);
				Panel->SetMobility(EComponentMobility::Movable);
				Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Panel->SetRelativeLocation(FVector(-1.8f, 35.0f, Bottom));
				Panel->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
				Panel->SetRelativeScale3D(FVector(0.68f, 1.0f, 0.95f));
				Panel->RegisterComponent();
			}
		}
	}

	// 점검구 둘. 서벽 가슴 높이 작은 것, 북벽 동쪽 무릎 높이 큰 것.
	const auto SpawnHatch = [this, World, &Parameters](const FName Id, const TCHAR* ActorName, const FVector& Where,
		const float Yaw, const FVector2D& Size, const bool bHingeLeft, const TCHAR* Frame, const TCHAR* Door)
	{
		Parameters.Name = ActorName;
		AIGAccessHatch* Hatch = World->SpawnActor<AIGAccessHatch>(
			AIGAccessHatch::StaticClass(), FTransform(FRotator(0.0f, Yaw, 0.0f), Where), Parameters);
		if (!Hatch)
		{
			return;
		}
		// 구운 틀과 문짝이 제 칠을 갖고 있다. 칠 재질은 메시가 없을 때 짓는 상자에만 쓴다.
		UStaticMesh* FrameMesh = IGLeftover::Mesh(Frame);
		Hatch->Configure(
			Size, bHingeLeft, FrameMesh, IGLeftover::Mesh(Door), FrameMesh ? FrameMesh->GetMaterial(0) : nullptr,
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/MI_HatchSealPlug.MI_HatchSealPlug"), nullptr, LOAD_NoWarn),
			NSLOCTEXT("IGMissingFloor", "HatchOpenPrompt", "점검구 열기"),
			NSLOCTEXT("IGMissingFloor", "HatchClosePrompt", "점검구 닫기"));
		Hatch->OnOpened.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleHatchOpened);
		Hatch->OnClosed.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleHatchClosed);
		Hatch->OnReopened.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleHatchReopened);
		Hatches.Add(Id, Hatch);
	};
	SpawnHatch(IGLeftover::GapInnerSmall, TEXT("MissingFloorInnerHatchSmall"), IGLeftover::InnerSmallHatch, 0.0f,
		FVector2D(45.0f, 45.0f), true, TEXT("SM_AccessHatchFrameS"), TEXT("SM_AccessHatchDoorS"));
	SpawnHatch(IGLeftover::GapInnerLarge, TEXT("MissingFloorInnerHatchLarge"), IGLeftover::InnerLargeHatch, -90.0f,
		FVector2D(80.0f, 100.0f), true, TEXT("SM_AccessHatchFrameL"), TEXT("SM_AccessHatchDoorL"));
	SpawnHatch(IGLeftover::GapStorageLarge, TEXT("MissingFloorStorageHatchLarge"), IGLeftover::StorageLargeHatch, 0.0f,
		FVector2D(80.0f, 100.0f), false, TEXT("SM_AccessHatchFrameL"), TEXT("SM_AccessHatchDoorL"));
	SpawnHatch(IGLeftover::GapStorageAlt, TEXT("MissingFloorStorageHatchAlt"), IGLeftover::StorageAltHatch, 180.0f,
		FVector2D(50.0f, 70.0f), true, TEXT("SM_AccessHatchFrameM"), TEXT("SM_AccessHatchDoorM"));

	// 작업대의 덮개 조각(A). 방수막 쪽이 위로 누워 있다.
	if (AIGLeftoverTarget* Piece = SpawnTarget(IGLeftover::PieceA, FVector(-62.0f, 62.0f, 84.0f), FRotator::ZeroRotator,
			FVector(18.0f, 36.0f, 3.0f), NSLOCTEXT("IGMissingFloor", "LeftoverPieceAPrompt", "막에 붙은 조각 떼어 내기"), 1.2f, 0.12f))
	{
		Piece->SetVisual(IGLeftover::Mesh(TEXT("SM_CoverPieceA")),
			FTransform(FRotator(90.0f, 90.0f, 0.0f), FVector(-14.5f, 0.0f, -2.0f)));
		Piece->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleInnerTarget);
	}
	// 벽 방수막에 붙은 조각(B). 자리는 덧댄 것의 아래 패드 밑이다(PlaceCoverPieceOnWall).
	if (AIGLeftoverTarget* Piece = SpawnTarget(IGLeftover::PieceB, FVector(20.0f, 166.0f, 60.0f), FRotator(0.0f, -90.0f, 0.0f),
			FVector(3.0f, 18.0f, 18.0f), NSLOCTEXT("IGMissingFloor", "LeftoverPieceBPrompt", "막에 붙은 조각 떼어 내기"), 1.2f, 0.12f))
	{
		Piece->SetVisual(IGLeftover::Mesh(TEXT("SM_CoverPieceB")),
			FTransform(FRotator::ZeroRotator, FVector(1.0f, -14.0f, -17.5f)));
		Piece->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleInnerTarget);
	}
	// 작업대 밑 상자. 「202 정우 겨울 장비」에 「주인 없음」 딱지.
	SpawnProp(FName(TEXT("Inner.GloveBox")), IGLeftover::Mesh(TEXT("SM_StorageBox")),
		FTransform(FRotator(0.0f, 95.0f, 0.0f), FVector(-62.0f, 128.0f, 0.0f)), true);
	SpawnProp(FName(TEXT("Inner.GloveBoxLabel")), IGLeftover::Mesh(TEXT("SM_LeftoverLabelJeongwoo")),
		FTransform(FRotator(0.0f, 95.0f, 0.0f), FVector(-62.0f, 128.0f, 0.0f)), false);
	if (AIGLeftoverTarget* Box = SpawnTarget(FName(TEXT("Inner.GloveBoxOpen")), FVector(-62.0f, 128.0f, 20.0f), FRotator::ZeroRotator,
			FVector(22.0f, 27.0f, 18.0f), NSLOCTEXT("IGMissingFloor", "LeftoverGloveBoxPrompt", "상자 열어 보기"), 0.8f, 0.10f))
	{
		Box->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleInnerTarget);
	}
	if (AIGLeftoverTarget* Pouch = SpawnTarget(IGLeftover::Gloves, FVector(-60.0f, 126.0f, 38.0f), FRotator(0.0f, 20.0f, 0.0f),
			FVector(14.0f, 9.0f, 7.0f), NSLOCTEXT("IGMissingFloor", "LeftoverGlovesPrompt", "장갑 주머니 챙기기"), 0.0f, 0.04f))
	{
		Pouch->SetVisual(IGLeftover::Mesh(TEXT("SM_GlovePouch")), FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -4.5f)));
		Pouch->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleInnerTarget);
	}
	// 보관함 안의 맡긴 짐 대장. 펼쳐 읽는다.
	Parameters.Name = TEXT("MissingFloorInnerCabinetLedger");
	CabinetLedger = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(), FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(231.0f, 122.0f, 118.0f)), Parameters);
	if (CabinetLedger)
	{
		// 보이는 것은 보관함 메시다. 이 판은 보관함 문 앞에서 조준만 받는다.
		// 오빠 짐은 호수 대신 옥탑으로 적혀 있다. 부동산이 옥탑을 비운 7월 26일 날짜다.
		UStaticMesh* CabinetMesh = IGLeftover::Mesh(TEXT("SM_InnerRoomCabinet"));
		CabinetLedger->ConfigurePrototypeVisuals(
			LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")),
			CabinetMesh ? CabinetMesh->GetMaterial(0) : nullptr, FVector(1.0f, 40.0f, 60.0f));
		CabinetLedger->HidePresentation();
		CabinetLedger->SetInteractionPrompt(NSLOCTEXT("IGMissingFloor", "LeftoverLedgerPrompt", "보관함 열어 보기"));
		CabinetLedger->SetNoteText(
			NSLOCTEXT("IGMissingFloor", "LeftoverLedgerTitle", "맡긴 짐 대장"),
			{
				NSLOCTEXT("IGMissingFloor", "LeftoverLedger1", "202  정우     겨울 장비 1상자          24.11.3"),
				NSLOCTEXT("IGMissingFloor", "LeftoverLedger2", "301  (공실)   선반·전기장판            24.12.20"),
				NSLOCTEXT("IGMissingFloor", "LeftoverLedger3", "옥탑 백도하   공구·상자 2              24.7.26"),
				FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverLedger4", "위 짐 일체 주인 없음. 수거일에 같이 처리."),
			});
		CabinetLedger->OnReadStateChanged.AddDynamic(this, &AIGMissingFloorLeftoverDirector::HandleLedgerRead);
	}
	// 둘러볼 것. 간이침대, 떼어 낸 판재, 벽에 접힌 더미.
	if (AIGLeftoverTarget* Cot = SpawnTarget(FName(TEXT("Inner.Cot")), FVector(10.0f, -30.0f, 48.0f), FRotator::ZeroRotator,
			FVector(80.0f, 30.0f, 10.0f), NSLOCTEXT("IGMissingFloor", "LeftoverCotPrompt", "간이침대 보기"), 0.0f, 0.03f))
	{
		Cot->SetThought(NSLOCTEXT("IGMissingFloor", "LeftoverCotThought", "여기서 먹고 자나 봐. 베개 밑에 귀마개가 있어."));
		Cot->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleInnerTarget);
	}
	if (AIGLeftoverTarget* Boards = SpawnTarget(FName(TEXT("Inner.Boards")), FVector(258.0f, 30.0f, 85.0f), FRotator::ZeroRotator,
			FVector(14.0f, 40.0f, 60.0f), NSLOCTEXT("IGMissingFloor", "LeftoverBoardsPrompt", "떼어 낸 판재 보기"), 0.0f, 0.06f))
	{
		Boards->SetThought(NSLOCTEXT("IGMissingFloor", "LeftoverBoardsThought", "벽에서 뜯어낸 판이야. 뒷면마다 방수 시트가 붙어 있어."));
		Boards->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleInnerTarget);
	}
	if (AIGLeftoverTarget* Heap = SpawnTarget(FName(TEXT("Inner.Heap")), FVector(50.0f, 150.0f, 112.0f), FRotator::ZeroRotator,
			FVector(14.0f, 58.0f, 42.0f), NSLOCTEXT("IGMissingFloor", "LeftoverHeapPrompt", "벽에 붙은 더미 보기"), 0.0f, 0.03f))
	{
		Heap->SetThought(NSLOCTEXT("IGMissingFloor", "LeftoverHeapThought", "방음재랑 방수 시트를 겹겹이 덧대 놨어. 철 골조까지 같이 붙어 있네."));
		Heap->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleInnerTarget);
	}

	// 필로티 뒷벽에 사슬로 묶어 둔 평대차. 밤2에 그가 가지러 나간다.
	Parameters.Name = TEXT("MissingFloorHandCart");
	Cart = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FRotator(0.0f, IGLeftover::CartHomeYaw, 0.0f), IGLeftover::CartHome), Parameters);
	if (Cart)
	{
		UStaticMeshComponent* CartMesh = NewObject<UStaticMeshComponent>(Cart, TEXT("CartMesh"));
		CartMesh->SetMobility(EComponentMobility::Movable);
		CartMesh->SetStaticMesh(IGLeftover::Mesh(TEXT("SM_HandCart")));
		CartMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		CartMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		CartMesh->SetCanEverAffectNavigation(false);
		Cart->SetRootComponent(CartMesh);
		CartMesh->RegisterComponent();
		Cart->SetActorTransform(FTransform(FRotator(0.0f, IGLeftover::CartHomeYaw, 0.0f), IGLeftover::CartHome));
	}
}

void AIGMissingFloorLeftoverDirector::HandleLedgerRead(AIGReadableNote* Note, const bool bOpened)
{
	if (!bOpened)
	{
		return;
	}
	MarkExamined(FName(TEXT("Inner.Ledger")));
	if (PlayBeat(FName(TEXT("Leftover.LedgerRead"))))
	{
		AIGNightThreatDirector::HoldQuietAfterDiscovery(this, 60.0f);
		GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			Think(NSLOCTEXT("IGMissingFloor", "LeftoverLedgerThought", "오빠 이름까지 적어 놓고 주인이 없대."), 4.0f);
		}), 0.6f, false);
	}
}

void AIGMissingFloorLeftoverDirector::LockInnerDoor(const bool bLocked)
{
	if (!InnerDoor)
	{
		return;
	}
	TArray<FIGDoorRequirement> Requirements;
	if (bLocked)
	{
		FIGDoorRequirement& Requirement = Requirements.AddDefaulted_GetRef();
		Requirement.RequiredState = FGameplayTag::RequestGameplayTag(FName(TEXT("State.MissingFloor.InnerRoomOpen")), false);
		Requirement.LockedPrompt = NSLOCTEXT("IGMissingFloor", "LeftoverInnerLockedPrompt", "안쪽 방 문 열기");
		Requirement.LockedThought = NSLOCTEXT("IGMissingFloor", "LeftoverInnerLockedThought", "안에서 잠겼어.");
	}
	InnerDoor->SetRequirements(MoveTemp(Requirements));
}

void AIGMissingFloorLeftoverDirector::SetBoothHooksEnabled(const bool bEnabled)
{
	// 문틈 보기와 귀 대기는 닫힌 문 앞의 동사다. 문이 열려 있는 동안에는 길을 막지 않는다.
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AIGMissingFloorEvidence> It(World); It; ++It)
		{
			const FName Name = It->GetFName();
			if (Name == FName(TEXT("MissingFloorFoamGap")) || Name == FName(TEXT("MissingFloorInnerRoomListen")))
			{
				It->SetInteractionEnabled(bEnabled);
				It->SetActorEnableCollision(bEnabled);
			}
		}
	}
}

void AIGMissingFloorLeftoverDirector::PlaceCoverPieceOnWall()
{
	AIGLeftoverTarget* Piece = FindTarget(IGLeftover::PieceB);
	if (!Piece || !Lining || CurrentSpace != IGLeftover::SpaceInner)
	{
		return;
	}
	// 접힌 몸의 가장 낮은 패드 바로 밑 방수막. 몸이 떠나면 그 자리에 그대로 남는다.
	if (Lining->GetState() == EIGLiningState::Folded && !Lining->HasMovedOnce())
	{
		float LowestZ = TNumericLimits<float>::Max();
		FVector Lowest = FVector::ZeroVector;
		for (int32 Leg = 0; Leg < AIGLiningEntity::LegCount; ++Leg)
		{
			const FVector Pad = Lining->GetPadLocation(Leg);
			if (Pad.Z < LowestZ)
			{
				LowestZ = Pad.Z;
				Lowest = Pad;
			}
		}
		if (LowestZ < TNumericLimits<float>::Max())
		{
			Piece->SetActorLocation(FVector(FMath::Clamp(Lowest.X, -60.0f, 120.0f), 166.0f, FMath::Clamp(Lowest.Z - 6.0f, 30.0f, 120.0f)));
		}
	}
}

void AIGMissingFloorLeftoverDirector::RefreshInnerRoom()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const int32 Night = GetNightIndex();
	const bool bLegacy = IsLegacyRun();
	// 문은 밤2 심부름 동안과 밤4(그는 옥상에 있다)에만 열린다.
	const bool bErrandOpen = NightTwoStage == ENightTwo::Errand || NightTwoStage == ENightTwo::Waking;
	const bool bNightFourOpen = bHourActive && Night == 4 && !bLegacy;
	const bool bOpenable = (bErrandOpen && NightTwoStage == ENightTwo::Errand) || bNightFourOpen;
	LockInnerDoor(!bOpenable);
	if (InnerDoor && !bOpenable && InnerDoor->IsOpen() && !InnerDoor->IsSwinging() && NightTwoStage != ENightTwo::Errand)
	{
		InnerDoor->ForceOpenState(false);
	}
	SetBoothHooksEnabled(!(InnerDoor && InnerDoor->IsOpen()));
	if (Scene && NightTwoStage == ENightTwo::Idle)
	{
		Scene->SetInnerRoomBulbOn(false);
	}
	// 조각과 주머니. 이미 챙긴 것은 없다. 확장 전 판에는 이 방의 물건이 다 나와 있어도 된다.
	const bool bRoomLive = bHourActive && (Night == 2 || Night == 4);
	SetTargetLive(FindTarget(IGLeftover::PieceA), bRoomLive && !HasTaken(IGLeftover::PieceA));
	SetTargetLive(FindTarget(IGLeftover::PieceB), bRoomLive && !HasTaken(IGLeftover::PieceB));
	const bool bBoxOpened = HasExamined(FName(TEXT("Inner.GloveBoxOpen")));
	SetTargetLive(FindTarget(FName(TEXT("Inner.GloveBoxOpen"))), bRoomLive && !bBoxOpened);
	SetTargetLive(FindTarget(IGLeftover::Gloves), bRoomLive && bBoxOpened && !HasTaken(IGLeftover::Gloves));
	if (AIGLeftoverTarget* Gloves = FindTarget(IGLeftover::Gloves); Gloves && Gloves->GetVisual())
	{
		Gloves->GetVisual()->SetVisibility(bBoxOpened && !HasTaken(IGLeftover::Gloves), true);
	}
	for (const TCHAR* Key : {TEXT("Inner.Cot"), TEXT("Inner.Boards"), TEXT("Inner.Heap")})
	{
		SetTargetLive(FindTarget(FName(Key)), bRoomLive);
	}
	if (AIGLeftoverTarget* Heap = FindTarget(FName(TEXT("Inner.Heap"))))
	{
		const bool bFolded = Lining && CurrentSpace == IGLeftover::SpaceInner
			&& Lining->GetState() == EIGLiningState::Folded && !Lining->HasMovedOnce();
		SetTargetLive(Heap, bRoomLive && bFolded);
	}
	if (CabinetLedger)
	{
		CabinetLedger->SetInteractionEnabled(bRoomLive);
		CabinetLedger->SetActorEnableCollision(bRoomLive);
	}
	for (const FName Id : {IGLeftover::GapInnerSmall, IGLeftover::GapInnerLarge})
	{
		if (AIGAccessHatch* Hatch = GetHatch(Id))
		{
			Hatch->SetInteractionEnabled(bRoomLive);
			const bool bOpen = Narrative && Narrative->HasLeftover(EIGLeftoverList::HatchesOpen, Id);
			if (Hatch->IsOpen() != bOpen && !Hatch->IsObstructed())
			{
				Hatch->ForceOpenState(bOpen);
			}
			Hatch->SetSealed(Narrative && Narrative->HasLeftover(EIGLeftoverList::GapsSealed, Id));
		}
	}
	// 손수레: 밤2 심부름이 끝난 뒤로는 관리실 앞에 세워 둔 채다.
	if (Cart && !(Manager && Manager->IsOnErrand()))
	{
		const bool bParked = HasBeat(IGLeftover::BeatNightTwoErrand);
		Cart->SetActorTransform(bParked
			? FTransform(FRotator(0.0f, IGLeftover::CartParkYaw, 0.0f), IGLeftover::CartPark)
			: FTransform(FRotator(0.0f, IGLeftover::CartHomeYaw, 0.0f), IGLeftover::CartHome));
	}
}

void AIGMissingFloorLeftoverDirector::HandleInnerTarget(AIGLeftoverTarget* Target)
{
	if (!Target)
	{
		return;
	}
	const FName Key = Target->Key;
	MarkExamined(Key);
	if (Key == IGLeftover::PieceA)
	{
		Take(IGLeftover::PieceA);
		Think(NSLOCTEXT("IGMissingFloor", "LeftoverPieceAThought", "점검구 덮개 조각이야. 방수막째로 잘려 나왔어."), 3.6f);
	}
	else if (Key == IGLeftover::PieceB)
	{
		// 접힌 몸의 패드가 누르고 있으면 떨어지지 않는다.
		if (Lining && CurrentSpace == IGLeftover::SpaceInner && Lining->GetState() == EIGLiningState::Folded
			&& !Lining->HasMovedOnce())
		{
			Think(NSLOCTEXT("IGMissingFloor", "LeftoverPieceBStuck", "뭔가에 눌려서 안 떨어져."), 2.6f);
			return;
		}
		Take(IGLeftover::PieceB);
		Think(NSLOCTEXT("IGMissingFloor", "LeftoverPieceBThought", "이것도 방수막째 잘린 조각이야."), 3.4f);
	}
	else if (Key == FName(TEXT("Inner.GloveBoxOpen")))
	{
		Think(NSLOCTEXT("IGMissingFloor", "LeftoverGloveBoxThought", "202호 정우 씨 짐이네. 주인 없음 딱지가 붙어 있어."), 3.6f);
		PlaySound(UIGToneSequenceSoundWave::CreateCardboardDrag(this), Target->GetActorLocation(), 0.4f);
	}
	else if (Key == IGLeftover::Gloves)
	{
		Take(IGLeftover::Gloves);
		Think(NSLOCTEXT("IGMissingFloor", "LeftoverGlovesThought", "낮에 정우 씨한테 보여 주자."), 3.0f);
	}
	RefreshInnerRoom();
}

bool AIGMissingFloorLeftoverDirector::BeginNightTwoInnerRoom()
{
	if (!bConfigured || GetNightIndex() != 2 || !bHourActive || IsLegacyRun() || !Manager || !InnerDoor
		|| HasBeat(IGLeftover::BeatNightTwoErrand) || HasBeat(IGLeftover::BeatNightTwoMissed)
		|| NightTwoStage != ENightTwo::Idle)
	{
		return false;
	}
	NightTwoStage = ENightTwo::Waking;
	bReturnArmed = false;
	// 장부를 맞춘 결론은 예전 그대로 먼저 말한다. 「403호까지」는 안쪽 방이 끝난 뒤다.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		Think(NSLOCTEXT("IGMissingFloor", "P2ContradictionThought",
			"26일에 짐을 다 뺐다는데, 벽 두드리는 소리는 31일까지 났어. 그때도 위에 누가 있었던 거야."), 5.0f);
	}));
	GetWorldTimerManager().SetTimer(WakeTimer, this, &AIGMissingFloorLeftoverDirector::PlayInnerRoomWake, 2.2f, false);
	UE_LOG(LogTemp, Display, TEXT("LEFTOVER_N2 waking"));
	return true;
}

void AIGMissingFloorLeftoverDirector::PlayInnerRoomWake()
{
	// 간이침대가 삐걱이고, 기침, 스위치, 불빛이 문 밑으로 샌다. 슬리퍼, 열쇠, 문고리.
	const FVector Cot(10.0f, -20.0f, 60.0f);
	PlaySound(UIGToneSequenceSoundWave::CreateSettleTimberCreak(this), Cot, 0.55f, 0.95f);
	Caption(NSLOCTEXT("IGMissingFloor", "LeftoverWakeCreak", "[간이침대가 삐걱거리는 소리]"), Cot, 2.2f);
	GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this, Cot]()
	{
		PlaySound(UIGToneSequenceSoundWave::CreateSwitchClick(this, true), FVector(220.0f, -60.0f, 130.0f), 0.6f);
		if (Scene)
		{
			Scene->SetInnerRoomBulbOn(true);
		}
		Caption(NSLOCTEXT("IGMissingFloor", "LeftoverWakeSwitch", "[스위치 켜는 소리]"), FVector(220.0f, -60.0f, 130.0f), 1.8f);
		Think(NSLOCTEXT("IGMissingFloor", "LeftoverWakeThought", "아저씨가 깼어. 숨어야 해."), 2.8f);
		GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			PlaySound(UIGToneSequenceSoundWave::CreateSlipperScuff(this), FVector(200.0f, 0.0f, 10.0f), 0.5f);
			PlaySound(UIGToneSequenceSoundWave::CreateKeyRingJingle(this, true), FVector(220.0f, -30.0f, 100.0f), 0.45f);
			GetWorldTimerManager().SetTimer(SceneTimer, this, &AIGMissingFloorLeftoverDirector::StartManagerErrand,
				IGLeftover::WakeToDoorSeconds - 4.4f, false);
		}), 2.4f, false);
	}), 2.0f, false);
}

bool AIGMissingFloorLeftoverDirector::IsManagerAsleepInInnerRoom() const
{
	return bHourActive && GetNightIndex() == 2 && NightTwoStage == ENightTwo::Idle;
}

void AIGMissingFloorLeftoverDirector::StartManagerErrand()
{
	if (NightTwoStage != ENightTwo::Waking || !Manager || !InnerDoor)
	{
		return;
	}
	NightTwoStage = ENightTwo::Errand;
	LockInnerDoor(false);
	InnerDoor->BeginScriptedSwingWithLoudness(true, 0.3f, 1.0f);
	SetBoothHooksEnabled(false);
	Manager->StartCartErrand(InnerDoor, BoothDoor, Cart,
		FTransform(FRotator(0.0f, IGLeftover::CartParkYaw, 0.0f), IGLeftover::CartPark), IGLeftover::ErrandFetchSeconds);
	RefreshInnerRoom();
	UE_LOG(LogTemp, Display, TEXT("LEFTOVER_N2 errand start"));
}

void AIGMissingFloorLeftoverDirector::HandleErrandOutside()
{
	if (NightTwoStage != ENightTwo::Errand)
	{
		return;
	}
	GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		Think(NSLOCTEXT("IGMissingFloor", "LeftoverErrandOut", "필로티 쪽으로 나갔어. 안쪽 방 문이 열려 있어."), 3.6f);
	}), 1.6f, false);
}

void AIGMissingFloorLeftoverDirector::HandleErrandReturning()
{
	if (NightTwoStage != ENightTwo::Errand)
	{
		return;
	}
	Think(NSLOCTEXT("IGMissingFloor", "LeftoverErrandBack", "손수레 소리가 이쪽으로 와. 나가야 해."), 3.4f);
}

void AIGMissingFloorLeftoverDirector::HandleErrandFinished(const bool bCaught)
{
	if (NightTwoStage != ENightTwo::Errand && NightTwoStage != ENightTwo::Waking)
	{
		return;
	}
	NightTwoStage = ENightTwo::Done;
	PlayBeat(IGLeftover::BeatNightTwoErrand);
	if (Scene)
	{
		// 눕고 나서 불을 끈다. 문 밑의 빛이 사라진다.
		GetWorldTimerManager().SetTimer(WakeTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (Scene)
			{
				Scene->SetInnerRoomBulbOn(false);
			}
			PlaySound(UIGToneSequenceSoundWave::CreateSwitchClick(this, false), FVector(220.0f, -60.0f, 130.0f), 0.5f);
			PlaySound(UIGToneSequenceSoundWave::CreateSettleTimberCreak(this), FVector(10.0f, -20.0f, 60.0f), 0.45f, 0.9f);
		}), bCaught ? 0.2f : 2.6f, false);
	}
	LockInnerDoor(true);
	RefreshInnerRoom();
	UE_LOG(LogTemp, Display, TEXT("LEFTOVER_N2 errand done caught=%d"), bCaught ? 1 : 0);
	if (bCaught)
	{
		// 그가 집 문 앞까지 데려다 놓았다. 그 밤은 집에 돌아온 것으로 끝난다.
		PlayBeat(IGLeftover::BeatNightTwoMissed);
		if (NightTwoBeats)
		{
			NightTwoBeats->CompleteReturnAfterEscort();
		}
		return;
	}
	ArmNightTwoReturn(true);
}

bool AIGMissingFloorLeftoverDirector::SkipNightTwoInnerRoomForTesting()
{
	if (NightTwoStage != ENightTwo::Waking && NightTwoStage != ENightTwo::Errand)
	{
		return false;
	}
	GetWorldTimerManager().ClearTimer(WakeTimer);
	GetWorldTimerManager().ClearTimer(SceneTimer);
	NightTwoStage = ENightTwo::Done;
	LockInnerDoor(true);
	if (Scene)
	{
		Scene->SetInnerRoomBulbOn(false);
	}
	RefreshInnerRoom();
	ArmNightTwoReturn(false);
	return true;
}

void AIGMissingFloorLeftoverDirector::ArmNightTwoReturn(const bool bWithThought)
{
	if (bReturnArmed || !NightTwoBeats)
	{
		return;
	}
	bReturnArmed = true;
	NightTwoBeats->ArmReturnChase(/*bWithConclusion=*/false);
	if (bWithThought)
	{
		Think(NSLOCTEXT("IGMissingFloor", "N2ReturnThought", "이제 403호까지 올라가야 해."), 4.0f);
	}
	UE_LOG(LogTemp, Display, TEXT("LEFTOVER_N2 return armed"));
}

void AIGMissingFloorLeftoverDirector::PollNightTwo()
{
	if (NightTwoStage != ENightTwo::Errand && NightTwoStage != ENightTwo::Waking)
	{
		return;
	}
	// 그가 나가 있는 동안 1층을 떠났다(계단이나 엘리베이터로 올라갔다). 귀가 길(귀환 추격)은
	// 그때부터다. 1층 연결통로나 로비에 숨어 그가 지나가기를 기다리는 것은 아직 나선 게 아니다.
	if (GetPlayerFeet().Z > 150.0f && !bReturnArmed)
	{
		ArmNightTwoReturn(false);
	}
	SetBoothHooksEnabled(!(InnerDoor && InnerDoor->IsOpen()));
}

// -- 낮 ----------------------------------------------------------------------

void AIGMissingFloorLeftoverDirector::BuildDayProps()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 1층 게시판 옆의 수거 안내. 인쇄 날짜를 줄 긋고 손글씨로 당겼다.
	Parameters.Name = TEXT("MissingFloorCollectionNotice");
	CollectionNotice = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(), FTransform(FRotator::ZeroRotator, IGLeftover::CollectionNoticeSpot), Parameters);
	if (CollectionNotice)
	{
		// 보이는 것은 구운 인쇄물(SM_CollectionNoticePrint)이다. 이 판은 그 앞에서 조준만 받는다.
		UStaticMesh* Print = IGLeftover::Mesh(TEXT("SM_CollectionNoticePrint"));
		CollectionNotice->ConfigurePrototypeVisuals(
			LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")),
			Print ? Print->GetMaterial(0) : nullptr, FVector(21.0f, 0.2f, 29.7f));
		CollectionNotice->HidePresentation();
		if (Print)
		{
			SpawnProp(FName(TEXT("Day.NoticePrint")), Print,
				FTransform(FRotator(0.0f, 0.0f, 0.0f), IGLeftover::CollectionNoticeSpot - FVector(0.0f, 0.0f, 14.85f)), false);
		}
		CollectionNotice->SetInteractionPrompt(NSLOCTEXT("IGMissingFloor", "LeftoverNoticePrompt", "수거 안내 읽기"));
		CollectionNotice->SetNoteText(
			NSLOCTEXT("IGMissingFloor", "LeftoverNoticeTitle", "대형 폐기물 수거 안내"),
			{
				NSLOCTEXT("IGMissingFloor", "LeftoverNotice1", "건물 안에 주인 없이 남은 짐을 한꺼번에 처리합니다."),
				// 인쇄물(LeftoverCollectionNotice.png)과 같다. 인쇄된 날짜는 볼펜으로 두 줄 그었고 그 밑에 손글씨다.
				NSLOCTEXT("IGMissingFloor", "LeftoverNotice2", "수거일: 다음 주 월요일 오전 [두 줄 그어 지움]"),
				NSLOCTEXT("IGMissingFloor", "LeftoverNotice3", "이번 주 목요일 아침 7시 [볼펜 손글씨]"),
				FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverNotice4", "버릴 짐은 전날 밤까지 필로티 적치 자리에 내놓아 주세요."),
				NSLOCTEXT("IGMissingFloor", "LeftoverNotice5", "찾아갈 짐이 있으면 관리실에 말씀해 주세요.  -관리인"),
			});
		CollectionNotice->OnReadStateChanged.AddDynamic(this, &AIGMissingFloorLeftoverDirector::HandleNoticeRead);
	}
	// 필로티 적치 자리의 마대와 상자. 낮2부터 밤3까지 있다가 수거차가 가져간다.
	SpawnProp(FName(TEXT("Day.Load1")), IGLeftover::Mesh(TEXT("SM_WasteSack")),
		FTransform(FRotator(0.0f, 10.0f, 0.0f), IGLeftover::LoadingSpot + FVector(-40.0f, 0.0f, 0.0f)), true);
	SpawnProp(FName(TEXT("Day.Load2")), IGLeftover::Mesh(TEXT("SM_WasteSack")),
		FTransform(FRotator(0.0f, -25.0f, 0.0f), IGLeftover::LoadingSpot + FVector(10.0f, 4.0f, 0.0f)), true);
	SpawnProp(FName(TEXT("Day.Load3")), IGLeftover::Mesh(TEXT("SM_StorageBox")),
		FTransform(FRotator(0.0f, 4.0f, 0.0f), IGLeftover::LoadingSpot + FVector(60.0f, 6.0f, 0.0f)), true);
	SpawnProp(FName(TEXT("Day.Load4")), IGLeftover::Mesh(TEXT("SM_StorageBox")),
		FTransform(FRotator(0.0f, -6.0f, 0.0f), IGLeftover::LoadingSpot + FVector(58.0f, 4.0f, 34.2f)), true);
	SpawnProp(FName(TEXT("Day.Load5")), IGLeftover::Mesh(TEXT("SM_RemovedBoards")),
		FTransform(FRotator(0.0f, 0.0f, 0.0f), IGLeftover::LoadingSpot + FVector(-100.0f, 8.0f, 0.0f)), true);

	// 낮1 3층 방화문 앞의 「문 잡아 주기」. 문짝보다 앞에 얇게 둔다.
	if (AIGLeftoverTarget* Hold = SpawnTarget(FName(TEXT("Day.HoldDoor")),
			AIGPrologueWorldScene::GetStairDoorwayFeet(2) + FVector(-6.0f, 0.0f, 110.0f), FRotator::ZeroRotator,
			FVector(4.0f, 50.0f, 40.0f), NSLOCTEXT("IGMissingFloor", "LeftoverHoldDoorPrompt", "문 잡아 주기"), 0.0f, 0.08f))
	{
		Hold->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleDayTarget);
		SetTargetLive(Hold, false);
	}
}

void AIGMissingFloorLeftoverDirector::HandleNoticeRead(AIGReadableNote* Note, const bool bOpened)
{
	if (!bOpened)
	{
		return;
	}
	if (PlayBeat(IGLeftover::BeatNoticeRead))
	{
		GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			Think(NSLOCTEXT("IGMissingFloor", "LeftoverNoticeThought", "월요일이던 걸 목요일로 당겼어. 주인 없는 짐이라니, 누구 짐인데."), 4.0f);
		}), 0.6f, false);
	}
}

void AIGMissingFloorLeftoverDirector::RefreshDay()
{
	const int32 Night = GetNightIndex();
	const bool bLegacy = IsLegacyRun();
	if (CollectionNotice)
	{
		// 공지는 낮1부터 수거일(낮3) 전까지 붙어 있다.
		const bool bPosted = !bLegacy && Night >= 1 && Night <= 3 && !HasBeat(IGLeftover::BeatDayThreeEmptied);
		CollectionNotice->SetActorHiddenInGame(!bPosted);
		CollectionNotice->SetActorEnableCollision(bPosted);
		CollectionNotice->SetInteractionEnabled(bPosted && !bHourActive);
		ShowProp(FName(TEXT("Day.NoticePrint")), bPosted);
	}
	RefreshLoadingArea();
	if (!bHourActive && Night == 1 && !bLegacy && !HasBeat(IGLeftover::BeatDayOneBoxes) && DayOneStage == EDayOne::Idle)
	{
		// 낮1 정우는 그녀가 계단에 들어서면 나온다(PollDayOneBoxes).
	}
	if (bHourActive && DayOneStage != EDayOne::Idle && DayOneStage != EDayOne::Done)
	{
		FinishDayOneBoxes();
	}
	// 낮3 아침, 수거차가 다녀갔다. 처음 필로티에 내려가면 골목 끝에서 엔진 소리가 멀어진다.
	if (!bHourActive && Night == 3 && !bLegacy)
	{
		if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
		{
			// 정우가 혼자 빠져나갔으면 낮에 문자 한 통이 온다. 같이 나왔으면 고맙다는 문자다.
			// 번호를 아는 사이(이름을 들었거나 낮에 302호 얘기를 나눈 회차)일 때만 온다.
			const FName State = Narrative->GetLeftover().JeongwooState;
			const bool bHasNumber = HasBeat(IGLeftover::JeongwooIntroBeat) || HasBeat(IGLeftover::BeatDayTwoTalk);
			if ((State == IGLeftover::JeongwooSelfEscaped || State == IGLeftover::JeongwooLeftTogether)
				&& bHasNumber && Narrative->MarkBeatPlayed(IGLeftover::BeatDayThreeText))
			{
				TFunction<bool()> SameScene = AIGHorrorHUD::MakeSceneGuard(this);
				GetWorldTimerManager().SetTimer(MorningTextTimer, FTimerDelegate::CreateWeakLambda(this, [this, State, SameScene]()
				{
					if (!SameScene())
					{
						return;
					}
					// 같이 나왔는데 오빠 상자는 그 밤에 못 뺐다. 정우가 아침에 짐을 빼면서 그 상자를 403호
					// 문 앞에 두었다. 문자가 오기 전에 그녀가 벌써 들고 들어왔으면 상자 얘기는 하지 않는다.
					const bool bBoxAtDoor = State == IGLeftover::JeongwooLeftTogether && !HasTaken(IGLeftover::DohaBox);
					PhoneBuzz();
					const bool bKnown = HasBeat(IGLeftover::JeongwooIntroBeat);
					AIGHorrorHUD::PushDialogue(this,
						bKnown ? IGLeftover::JeongwooName() : NSLOCTEXT("IGMissingFloor", "LeftoverUnknownSender", "202호"),
						State != IGLeftover::JeongwooLeftTogether
							? NSLOCTEXT("IGMissingFloor", "LeftoverJeongwooAlone", "새벽에 판이 갑자기 헐거워져서 겨우 밀고 나왔어요. 상자는 못 챙겼어요. 302호 혼자 가지 마세요.")
							: bBoxAtDoor
							? NSLOCTEXT("IGMissingFloor", "LeftoverJeongwooBoxAtDoor", "새벽엔 진짜 고마웠어요. 표시해 둔 상자는 아침에 빼서 403호 문 앞에 뒀어요. 그 방엔 이제 혼자 들어가지 마세요.")
							: NSLOCTEXT("IGMissingFloor", "LeftoverJeongwooThanks", "새벽엔 진짜 고마웠어요. 짐은 아침에 제가 마저 뺐어요. 그 방엔 이제 혼자 들어가지 마세요."),
						EIGDialogueChannel::Device, 0.0f, EIGDialoguePriority::Story);
				}), 14.0f, false);
			}
		}
	}
}

void AIGMissingFloorLeftoverDirector::RefreshLoadingArea()
{
	const int32 Night = GetNightIndex();
	// 낮2(밤2 다음)부터 셋째 밤까지 필로티에 쌓여 있다. 셋째 밤이 끝난 새벽에 수거차가 가져간다.
	const bool bStacked = !IsLegacyRun() && (Night == 2 || (Night == 3 && bHourActive));
	for (const TCHAR* Key : {TEXT("Day.Load1"), TEXT("Day.Load2"), TEXT("Day.Load3"), TEXT("Day.Load4"), TEXT("Day.Load5")})
	{
		ShowProp(FName(Key), bStacked);
	}
	if (!bHourActive && Night == 3 && !IsLegacyRun() && IsPlayerNear(IGLeftover::LoadingSpot, 520.0f, 150.0f)
		&& PlayBeat(IGLeftover::BeatDayThreeEmptied))
	{
		PlaySound(UIGToneSequenceSoundWave::CreateVentDuctSpinUp(this), IGLeftover::LoadingSpot + FVector(0.0f, -900.0f, 120.0f), 0.35f, 0.6f, 300.0f, 2600.0f);
		Caption(NSLOCTEXT("IGMissingFloor", "LeftoverTruckCaption", "[골목 끝에서 트럭 엔진 소리가 멀어진다]"),
			IGLeftover::LoadingSpot + FVector(0.0f, -900.0f, 120.0f), 3.0f);
		GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			Think(NSLOCTEXT("IGMissingFloor", "LeftoverEmptiedThought", "다 실어 갔어. 끈이랑 찢긴 딱지만 남았네."), 3.6f);
		}), 2.0f, false);
	}
}

void AIGMissingFloorLeftoverDirector::PollDayOneBoxes()
{
	if (bHourActive || IsLegacyRun() || GetNightIndex() != 1 || HasBeat(IGLeftover::BeatDayOneBoxes) || !Jeongwoo)
	{
		return;
	}
	if (DayOneStage == EDayOne::Idle)
	{
		// 4층에서 계단으로 내려서면 정우가 2층에서 올라오기 시작한다.
		const FVector Feet = GetPlayerFeet();
		if (AIGPrologueWorldScene::IsInsideStairCore(Feet) && Feet.Z > AIGPrologueWorldScene::GetStoreyFloorZ(2) + 60.0f
			&& Feet.Z < AIGPrologueWorldScene::GetStoreyFloorZ(3) + 120.0f)
		{
			BeginDayOneBoxes();
		}
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (DayOneStage == EDayOne::AtDoor && Now - DayOneStageAt > IGLeftover::DayOneDoorWaitSeconds)
	{
		// 아무도 안 잡아 주면 상자를 내려놓고 문을 연 뒤 다시 든다.
		bDayOneDoorHeld = false;
		HandleDayTarget(FindTarget(FName(TEXT("Day.HoldDoor"))));
	}
	if (DayOneStage == EDayOne::Talking && Now - DayOneStageAt > 75.0)
	{
		FinishDayOneBoxes();
	}
}

void AIGMissingFloorLeftoverDirector::BeginDayOneBoxes()
{
	if (!Jeongwoo || !Jeongwoo->LoadBody())
	{
		PlayBeat(IGLeftover::BeatDayOneBoxes);
		return;
	}
	DayOneStage = EDayOne::Climbing;
	DayOneStageAt = GetWorld()->GetTimeSeconds();
	TArray<FVector> Climb;
	AIGPrologueWorldScene::GetStairClimbFeet(1, Climb);
	if (Climb.Num() < 2)
	{
		PlayBeat(IGLeftover::BeatDayOneBoxes);
		DayOneStage = EDayOne::Done;
		return;
	}
	// 2층 참에서 3층 참까지 상자 둘을 안고 오른다. 3층 방화문 바로 앞에 선다.
	const FVector DoorFront = AIGPrologueWorldScene::GetStairDoorwayFeet(2) + FVector(-55.0f, 0.0f, 0.0f);
	Climb.Last() = DoorFront;
	Jeongwoo->SetCarriedBoxes(2);
	Jeongwoo->Appear(Climb[0], 90.0f, AIGJeongwooFigure::EPose::CarryIdle);
	Jeongwoo->WalkRoute(Climb, true, AIGJeongwooFigure::EPose::CarryIdle, 70.0f);
	UE_LOG(LogTemp, Display, TEXT("LEFTOVER_DAY1 boxes begin"));
}

void AIGMissingFloorLeftoverDirector::HandleJeongwooArrived()
{
	if (!Jeongwoo)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	switch (DayOneStage)
	{
	case EDayOne::Climbing:
	{
		DayOneStage = EDayOne::AtDoor;
		DayOneStageAt = Now;
		Jeongwoo->FaceYaw(0.0f);
		SetTargetLive(FindTarget(FName(TEXT("Day.HoldDoor"))), true);
		const bool bKnown = HasBeat(IGLeftover::JeongwooIntroBeat);
		if (!bKnown)
		{
			Say(IGLeftover::RiderName(), NSLOCTEXT("IGMissingFloor", "LeftoverBoxesIntro",
				"어, 어제 골목에서 뵌… 진짜 죄송했어요. 저 202호 정우예요."));
			PlayBeat(IGLeftover::JeongwooIntroBeat);
		}
		Say(IGLeftover::JeongwooName(), NSLOCTEXT("IGMissingFloor", "LeftoverBoxesAskDoor", "저기, 죄송한데 문 좀 잡아 주실래요?"));
		return;
	}
	case EDayOne::Walking:
	{
		// 302호 안. 상자를 내려놓고 오빠 상자 이야기로 넘어간다.
		DayOneStage = EDayOne::Unloading;
		DayOneStageAt = Now;
		Jeongwoo->SetCarriedBoxes(0);
		Jeongwoo->SetPose(AIGJeongwooFigure::EPose::Idle);
		Jeongwoo->FaceTowards(GetPlayerFeet());
		PlaySound(UIGToneSequenceSoundWave::CreateCardboardDrag(this), Jeongwoo->GetFeet() + FVector(40.0f, 0.0f, 10.0f), 0.5f);
		Say(IGLeftover::JeongwooName(), NSLOCTEXT("IGMissingFloor", "LeftoverBoxesWhy",
			"수거 날짜를 당겼대요. 내일 아침까지 여기다 모아 달라고요."));
		DayOneStage = EDayOne::Talking;
		RefreshStorage();
		return;
	}
	default:
		break;
	}
	// 302호 장면의 걸음(갇힘에서 나오기, 판 받치기, 문 잡기).
	switch (StorageStage)
	{
	case EStorage::Freed:
		Jeongwoo->FaceTowards(GetPlayerFeet());
		return;
	case EStorage::Bracing:
		// 밀려난 판(25도 돈 셋째 자세)의 방 쪽 면을 본다. 두 손이 그 면에 닿는다.
		Jeongwoo->SetPose(AIGJeongwooFigure::EPose::Brace);
		Jeongwoo->FaceYaw(70.0f);
		SayIn(IGLeftoverDialogue::Brace, IGLeftover::JeongwooName(), NSLOCTEXT("IGMissingFloor", "LeftoverBraceLine", "제가 받칠게요. 상자부터 당겨요."));
		RefreshStorage();
		return;
	case EStorage::Leaving:
		// 복도에서 서쪽 문설주를 잡는다. 문간은 비어 있다.
		Jeongwoo->SetPose(AIGJeongwooFigure::EPose::HoldDoor);
		Jeongwoo->FaceTowards(FVector(-72.0f, -236.0f, Jeongwoo->GetFeet().Z));
		return;
	case EStorage::Gone:
		Jeongwoo->Vanish();
		return;
	default:
		return;
	}
}

void AIGMissingFloorLeftoverDirector::HandleDayTarget(AIGLeftoverTarget* Target)
{
	if (!Target || !Jeongwoo || DayOneStage != EDayOne::AtDoor)
	{
		return;
	}
	if (Target->Key == FName(TEXT("Day.HoldDoor")))
	{
		bDayOneDoorHeld = true;
		SetTargetLive(Target, false);
		// 3층 방화문을 연다(고임목이 문을 잡아 주는 자리까지).
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AIGSwingDoor> It(World); It; ++It)
			{
				const FVector Hinge = It->GetActorLocation();
				if (FMath::Abs(Hinge.Z - AIGPrologueWorldScene::GetStoreyFloorZ(2)) < 20.0f
					&& AIGPrologueWorldScene::IsInsideStairCore(Hinge + FVector(-20.0f, 0.0f, 0.0f))
					&& FVector::Dist2D(Hinge, AIGPrologueWorldScene::GetStairDoorwayFeet(2)) < 200.0f && !It->IsOpen())
				{
					It->BeginScriptedSwingWithLoudness(true, 0.15f, 1.0f);
				}
			}
		}
		if (bDayOneDoorHeld)
		{
			Say(IGLeftover::JeongwooName(), NSLOCTEXT("IGMissingFloor", "LeftoverBoxesThanks", "고마워요. 302호까지만 가면 돼요."));
		}
		DayOneStage = EDayOne::Walking;
		DayOneStageAt = GetWorld()->GetTimeSeconds();
		const float ThirdZ = AIGPrologueWorldScene::GetStoreyFloorZ(2);
		TArray<FVector> Route = {
			Jeongwoo->GetFeet(),
			AIGPrologueWorldScene::GetStairDoorwayFeet(2) + FVector(40.0f, 0.0f, 0.0f),
			FVector(-30.0f, -300.0f, ThirdZ),
			FVector(-30.0f, -222.0f, ThirdZ),
			FVector(-60.0f, -150.0f, ThirdZ),
			FVector(-110.0f, -170.0f, ThirdZ),
		};
		Jeongwoo->WalkRoute(Route, true, AIGJeongwooFigure::EPose::CarryIdle, 90.0f);
	}
}

void AIGMissingFloorLeftoverDirector::FinishDayOneBoxes()
{
	if (DayOneStage == EDayOne::Idle || DayOneStage == EDayOne::Done)
	{
		return;
	}
	DayOneStage = EDayOne::Done;
	PlayBeat(IGLeftover::BeatDayOneBoxes);
	SetTargetLive(FindTarget(FName(TEXT("Day.HoldDoor"))), false);
	if (Jeongwoo)
	{
		Jeongwoo->Vanish();
	}
	RefreshStorage();
}

bool AIGMissingFloorLeftoverDirector::TryJeongwooTalk(const bool bKnown, const bool bGuestTalkDone,
	const TFunction<void(const FText&, const FText&)>& SayLine)
{
	const auto Talk = [this, &SayLine](const FText& Speaker, const FText& Line)
	{
		if (SayLine)
		{
			SayLine(Speaker, Line);
		}
		else
		{
			Say(Speaker, Line);
		}
	};
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || IsLegacyRun() || Narrative->GetNightIndex() != 2 || !bGuestTalkDone
		|| Narrative->HasBeatPlayed(IGLeftover::BeatDayTwoTalk))
	{
		return false;
	}
	const FText& Rider = bKnown ? IGLeftover::JeongwooName() : IGLeftover::RiderName();
	Narrative->MarkBeatPlayed(IGLeftover::BeatDayTwoTalk);
	if (HasTaken(IGLeftover::Gloves))
	{
		// 밤에 안쪽 방에서 챙긴 장갑 주머니를 건넨다.
		Narrative->AddLeftover(EIGLeftoverList::Taken, IGLeftover::GlovesGiven);
		Talk(Rider, NSLOCTEXT("IGMissingFloor", "LeftoverGloves1", "이거 제 건데요. 버리라고 한 적 없어요."));
		Talk(IGLeftover::YudamName(), NSLOCTEXT("IGMissingFloor", "LeftoverGloves2", "302호에 더 있다면서요. 그것도 한번 봐요."));
		Talk(Rider, NSLOCTEXT("IGMissingFloor", "LeftoverGloves3", "배달 끝나면 네 시쯤 돼요. 들어오는 길에 빼놓을게요."));
	}
	else
	{
		Talk(Rider, NSLOCTEXT("IGMissingFloor", "LeftoverNoGloves1", "302호로 옮긴 짐에 제 겨울 장비도 섞여 있더라고요."));
		Talk(IGLeftover::YudamName(), NSLOCTEXT("IGMissingFloor", "LeftoverNoGloves2", "302호에 더 있으면 같이 봐요."));
		Talk(Rider, NSLOCTEXT("IGMissingFloor", "LeftoverNoGloves3", "배달 끝나면 네 시쯤이에요. 들어오는 길에 빼 놓을게요."));
	}
	return true;
}
