#include "Entity/IGElevatorOverloadDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGManagerPatrol.h"
#include "Entity/IGShadowFigure.h"
#include "GameFramework/PlayerController.h"
#include "IndieGame.h"
#include "Interaction/IGReadableNote.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Save/IGSaveSubsystem.h"

const FName AIGElevatorOverloadDirector::BeatId(TEXT("Day.ElevatorOverload"));
const FName AIGElevatorOverloadDirector::FirstRideBeatId(TEXT("Day.ElevatorFirstRide"));

namespace IGElevatorOverload
{
	/** 부저가 우는 동안 버티면 칸이 데려간다. 내릴 시간은 충분히 준다. */
	constexpr float HoldOnSeconds = 10.0f;
	/** 칸 문턱에서 이만큼 복도 쪽으로 나서면 내린 것으로 본다(칸 바닥 가운데부터, cm). */
	constexpr float LeftCabDistance = 110.0f;
	constexpr int32 MirrorFigureCount = 5;
	/** 거울에 눈을 둔 동안은 사람이 느리게 는다. 눈을 돌리면 금방 는다. */
	constexpr float FigureSecondsWatched = 2.6f;
	constexpr float FigureSecondsAway = 1.0f;
	/** 거울에 서는 자리(칸 로컬 XY). 문 쪽이 -Y, 거울이 +Y다. */
	const FVector2D FigureSlots[] = {
		FVector2D(-52.0f, -38.0f), FVector2D(50.0f, -36.0f), FVector2D(-14.0f, -50.0f),
		FVector2D(18.0f, -50.0f), FVector2D(-56.0f, 16.0f), FVector2D(56.0f, 20.0f),
		FVector2D(0.0f, -24.0f)};
}

AIGElevatorOverloadDirector::AIGElevatorOverloadDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// 칸이 움직인 뒤에 본다. 승강기는 물리 전에 움직인다.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGElevatorOverloadDirector::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UIGSaveSubsystem* Save = GameInstance->GetSubsystem<UIGSaveSubsystem>())
		{
			Save->OnLoadCompleted.AddUniqueDynamic(this, &ThisClass::HandleLoadCompleted);
		}
	}
}

void AIGElevatorOverloadDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UIGSaveSubsystem* Save = GameInstance->GetSubsystem<UIGSaveSubsystem>())
		{
			Save->OnLoadCompleted.RemoveDynamic(this, &ThisClass::HandleLoadCompleted);
		}
	}
	if (AIGElevator* Lift = Elevator.Get())
	{
		Lift->OnButtonPressed.RemoveAll(this);
		Lift->OnArrived.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AIGElevatorOverloadDirector::Configure(AIGElevator* InElevator)
{
	Elevator = InElevator;
	if (InElevator)
	{
		InElevator->OnButtonPressed.AddUObject(this, &ThisClass::HandleButtonPressed);
		InElevator->OnArrived.AddUObject(this, &ThisClass::HandleArrived);
	}
}

UIGMissingFloorNarrativeSubsystem* AIGElevatorOverloadDirector::GetNarrative() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
}

AIGPlayerCharacter* AIGElevatorOverloadDirector::GetPlayer() const
{
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return Controller ? Cast<AIGPlayerCharacter>(Controller->GetPawn()) : nullptr;
}

FVector AIGElevatorOverloadDirector::CabPoint(const FVector& CabLocal) const
{
	const AIGElevator* Lift = Elevator.Get();
	return Lift && Lift->GetCabRoot()
		? Lift->GetCabRoot()->GetComponentTransform().TransformPosition(CabLocal)
		: FVector::ZeroVector;
}

void AIGElevatorOverloadDirector::HandleArrived(const int32 Landing)
{
	// 처음 한 번은 그냥 승강기다. 두 번째부터 이 일이 일어날 수 있다.
	const AIGElevator* Lift = Elevator.Get();
	const AIGPlayerCharacter* Player = GetPlayer();
	if (Stage == EStage::Idle && Lift && Player && Lift->IsPawnInsideCab(Player))
	{
		if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
		{
			Narrative->MarkBeatPlayed(FirstRideBeatId);
		}
	}
}

bool AIGElevatorOverloadDirector::IsEligible(const int32 Floor) const
{
	const AIGElevator* Lift = Elevator.Get();
	const AIGPlayerCharacter* Player = GetPlayer();
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Lift || !Player || !Narrative || Lift->IsMoving() || Lift->IsScripted() || Lift->IsHourDead()
		|| !Lift->IsPawnInsideCab(Player))
	{
		return false;
	}
	const int32 Landing = Lift->GetStoppedLanding();
	if (Landing == INDEX_NONE || Floor < 0 || Floor >= AIGElevator::LandingCount || Floor == Landing)
	{
		return false;
	}
	if (Narrative->IsHourSealed() || Narrative->GetNightIndex() < 1
		|| !Narrative->HasBeatPlayed(FirstRideBeatId) || Narrative->HasBeatPlayed(BeatId)
		|| AIGReadableNote::GetOpenNote())
	{
		return false;
	}
	// 다른 무엇이 깨어 있으면 하지 않는다. 한 번에 하나만 무섭다.
	for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
	{
		if (!It->IsDormant())
		{
			return false;
		}
	}
	for (TActorIterator<AIGManagerPatrol> It(GetWorld()); It; ++It)
	{
		if (It->IsOnDuty())
		{
			return false;
		}
	}
	for (TActorIterator<AIGShadowFigure> It(GetWorld()); It; ++It)
	{
		if (It->IsManifested())
		{
			return false;
		}
	}
	return true;
}

void AIGElevatorOverloadDirector::HandleButtonPressed(const EIGElevatorButtonKind Kind, const int32 Floor)
{
	if (Stage != EStage::Idle || Kind != EIGElevatorButtonKind::Floor)
	{
		return;
	}
	if (bForceArmed ? Floor >= 0 && Floor < AIGElevator::LandingCount : IsEligible(Floor))
	{
		Begin(Floor);
	}
}

void AIGElevatorOverloadDirector::Begin(const int32 InTargetFloor)
{
	AIGElevator* Lift = Elevator.Get();
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Lift)
	{
		return;
	}
	if (Narrative)
	{
		Narrative->MarkBeatPlayed(BeatId);
	}
	bForceArmed = false;
	TargetFloor = InTargetFloor;
	StartLanding = FMath::Max(Lift->GetStoppedLanding(), 0);
	FiguresShown = 0;
	FigureClock = 0.0f;
	bSawEmptyMirror = false;
	bAbsentUntilExit = false;
	// 이 판(SetScriptedControl을 켠 뒤)부터 버튼은 눌린 소리만 낸다. 누른 층 등만 켜 둔다.
	Lift->SetScriptedControl(true);
	Lift->ScriptSetButtonLit(TargetFloor, true);
	Lift->ScriptSetDoors(false, 1.6f);
	EnterStage(EStage::Closing);
	SetActorTickEnabled(true);
	UE_LOG(LogIndieGame, Display, TEXT("ELEVATOR_OVERLOAD begin landing=%d target=%d"), StartLanding, TargetFloor);
}

void AIGElevatorOverloadDirector::EnterStage(const EStage NewStage)
{
	Stage = NewStage;
	StageSeconds = 0.0f;
	Cue = 0;
	AIGElevator* Lift = Elevator.Get();
	if (!Lift)
	{
		return;
	}
	switch (NewStage)
	{
	case EStage::Overload:
	{
		// 닫히던 문이 멈칫하고 되열린다. 혼자인데 정원 초과다.
		Lift->ScriptSetDoors(true, 0.7f);
		Lift->PlayOverloadBuzzer(true);
		Lift->ScriptSetFullLamp(true);
		Lift->ScriptSetSag(2.5f);
		Lift->PlayCableCreak(0.55f);
		AIGHorrorHUD::PushAudioCaptionAt(this,
			NSLOCTEXT("IGElevatorOverload", "BuzzerCaption", "정원 초과 경보음"), 3.0f, CabPoint(FVector(70.0f, -40.0f, 110.0f)));
		break;
	}
	case EStage::LeftBehind:
		// 내렸다. 무게가 빠진 것처럼 부저가 멎는다.
		Lift->PlayOverloadBuzzer(false);
		Lift->ScriptSetFullLamp(false);
		Lift->ScriptSetSag(0.0f);
		Lift->ClearReflectionFigures();
		Lift->SetReflection(EIGElevatorReflection::Normal);
		StopDrone(1.2f);
		break;
	case EStage::CarriedUp:
		// 버텼다. 부저가 뚝 끊기고 거울의 사람들이 사라지는데, 칸은 더 가라앉는다.
		Lift->PlayOverloadBuzzer(false);
		Lift->ScriptSetFullLamp(false);
		Lift->ClearReflectionFigures();
		Lift->ScriptSetSag(4.5f);
		Lift->PlayCableCreak(0.8f);
		StopDrone(0.05f);
		break;
	case EStage::AtFive:
		Lift->ScriptSetCabLight(0.55f, true);
		IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateFluorescentBallastSnap(this),
			CabPoint(FVector(0.0f, 0.0f, AIGElevator::CabHeight - 10.0f)), 0.7f, 1.0f, 60.0f, 600.0f);
		break;
	default:
		break;
	}
}

void AIGElevatorOverloadDirector::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StageSeconds += DeltaSeconds;
	AIGElevator* Lift = Elevator.Get();
	AIGPlayerCharacter* Player = GetPlayer();
	if (!Lift || !Player)
	{
		Finish(true);
		return;
	}
	if ((Stage == EStage::AtFive || Stage == EStage::Returning) && IsLookingAtMirror())
	{
		bSawEmptyMirror = true;
	}
	switch (Stage)
	{
	case EStage::Closing:
		if (StageSeconds >= 0.85f)
		{
			EnterStage(EStage::Overload);
		}
		break;
	case EStage::Overload:
		TickOverload(DeltaSeconds);
		break;
	case EStage::LeftBehind:
		TickLeftBehind();
		break;
	case EStage::CarriedUp:
		TickCarriedUp();
		break;
	case EStage::AtFive:
		TickAtFive();
		break;
	case EStage::Returning:
		TickReturning();
		break;
	case EStage::Done:
		// 거울은 그 칸에서 내릴 때까지 비어 있다.
		if (!bAbsentUntilExit || !Lift->IsPawnInsideCab(Player))
		{
			Lift->SetReflection(EIGElevatorReflection::Normal);
			bAbsentUntilExit = false;
			SetActorTickEnabled(false);
		}
		break;
	default:
		SetActorTickEnabled(false);
		break;
	}
}

void AIGElevatorOverloadDirector::TickOverload(const float DeltaSeconds)
{
	using namespace IGElevatorOverload;
	AIGElevator* Lift = Elevator.Get();
	AIGPlayerCharacter* Player = GetPlayer();
	if (Cue == 0 && StageSeconds >= 1.4f)
	{
		// 거울 속 유담은 마주 서 있는데 등을 보인다.
		Lift->SetReflection(EIGElevatorReflection::Back);
		Cue = 1;
	}
	if (Cue == 1 && StageSeconds >= 2.2f)
	{
		AIGHorrorHUD::PushThought(this, NSLOCTEXT("IGElevatorOverload", "AloneThought", "…나 혼자 탔는데."), 2.4f);
		StartDrone();
		Cue = 2;
	}
	if (Cue == 2 && StageSeconds >= 4.6f)
	{
		// 바로 뒤에서 누가 숨을 쉰다. 돌아보면 거울이다.
		if (const APlayerController* Controller = Cast<APlayerController>(Player->GetController()))
		{
			FVector Eye;
			FRotator View;
			Controller->GetPlayerViewPoint(Eye, View);
			const FVector Behind = Eye - View.Vector().GetSafeNormal2D() * 45.0f + FVector(0.0f, 0.0f, 6.0f);
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateDarknessInhale(this), Behind, 0.45f, 1.15f, 40.0f, 400.0f);
			AIGHorrorHUD::PushAudioCaptionAt(this,
				NSLOCTEXT("IGElevatorOverload", "BehindCaption", "가까이서 숨 쉬는 소리"), 2.4f, Behind);
		}
		Cue = 3;
	}
	if (StageSeconds >= 1.6f && FiguresShown < MirrorFigureCount)
	{
		FigureClock += DeltaSeconds;
		if (FigureClock >= (IsLookingAtMirror() ? FigureSecondsWatched : FigureSecondsAway))
		{
			FigureClock = 0.0f;
			AddMirrorFigure();
		}
	}
	const bool bInside = Lift->IsPawnInsideCab(Player);
	if (!bInside && FVector::Dist2D(Player->GetActorLocation(), Lift->GetCabFloorWorldLocation()) > LeftCabDistance)
	{
		UE_LOG(LogIndieGame, Display, TEXT("ELEVATOR_OVERLOAD left figures=%d"), FiguresShown);
		EnterStage(EStage::LeftBehind);
		return;
	}
	if (bInside && StageSeconds >= HoldOnSeconds)
	{
		UE_LOG(LogIndieGame, Display, TEXT("ELEVATOR_OVERLOAD stayed figures=%d"), FiguresShown);
		EnterStage(EStage::CarriedUp);
	}
}

void AIGElevatorOverloadDirector::TickLeftBehind()
{
	AIGElevator* Lift = Elevator.Get();
	AIGPlayerCharacter* Player = GetPlayer();
	switch (Cue)
	{
	case 0:
		if (StageSeconds >= 0.8f)
		{
			// 아무도 없는 칸이 문을 닫는다.
			Lift->ScriptSetDoors(false, 1.2f);
			Cue = 1;
		}
		break;
	case 1:
		if (StageSeconds >= 2.3f && Lift->AreDoorsClosed())
		{
			Lift->ScriptSetButtonLit(TargetFloor, false);
			Lift->ScriptSetButtonLit(AIGElevator::LandingCount, true);
			Lift->ScriptTravelTo(AIGElevator::OverrunZ, 105.0f, 80.0f);
			Cue = 2;
		}
		break;
	case 2:
		if (!Lift->IsMoving() && FMath::IsNearlyEqual(Lift->GetCabZ(), AIGElevator::OverrunZ, 1.0f))
		{
			// 4층 위에서 칸이 선다. 층 표시는 5다.
			const FVector Top = CabPoint(FVector(0.0f, 0.0f, AIGElevator::CabHeight + 20.0f));
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateDoorThud(this), Top, 0.8f, 0.6f, 200.0f, 2600.0f);
			AIGHorrorHUD::PushAudioCaptionAt(this,
				NSLOCTEXT("IGElevatorOverload", "StopAboveCaption", "엘리베이터가 서는 소리"), 2.6f, Top);
			if (FVector::Dist(Player->GetActorLocation(), Lift->GetLandingFrontWorldLocation(StartLanding)) < 800.0f)
			{
				AIGHorrorHUD::PushThought(this, NSLOCTEXT("IGElevatorOverload", "NoFifthThought", "5층은 없잖아."), 2.6f);
			}
			Cue = 3;
			StageSeconds = 0.0f;
		}
		break;
	case 3:
		if (StageSeconds >= 7.0f)
		{
			Lift->ScriptSetButtonLit(AIGElevator::LandingCount, false);
			Lift->ScriptTravelTo(AIGElevator::GetLandingZ(StartLanding), 105.0f, 80.0f);
			Cue = 4;
		}
		break;
	case 4:
		if (Lift->GetStoppedLanding() == StartLanding)
		{
			// 빈 칸이 돌아와 문을 연다. 거울에는 아무도 없다.
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateDoorChime(this),
				Lift->GetLandingFrontWorldLocation(StartLanding) + FVector(80.0f, 0.0f, 226.0f), 0.5f, 0.92f, 150.0f, 1400.0f);
			Lift->ScriptSetDoors(true, 1.6f);
			Cue = 5;
			StageSeconds = 0.0f;
		}
		break;
	case 5:
		if (StageSeconds >= 1.7f)
		{
			Finish(false);
		}
		break;
	default:
		break;
	}
}

void AIGElevatorOverloadDirector::TickCarriedUp()
{
	AIGElevator* Lift = Elevator.Get();
	AIGPlayerCharacter* Player = GetPlayer();
	const bool bInside = Lift->IsPawnInsideCab(Player);
	switch (Cue)
	{
	case 0:
		if (!bInside)
		{
			EnterStage(EStage::LeftBehind);
			return;
		}
		if (StageSeconds >= 1.0f)
		{
			// 아무도 누르지 않았는데 문이 천천히 닫힌다.
			Lift->ScriptSetDoors(false, 2.4f);
			Cue = 1;
		}
		break;
	case 1:
		if (!bInside && !Lift->AreDoorsClosed())
		{
			EnterStage(EStage::LeftBehind);
			return;
		}
		if (StageSeconds >= 3.6f && Lift->AreDoorsClosed())
		{
			Lift->ScriptTravelTo(AIGElevator::OverrunZ, 95.0f, 70.0f);
			Cue = 2;
		}
		break;
	case 2:
		if (Lift->GetCabZ() > AIGElevator::GetLandingZ(AIGElevator::LandingCount - 1) + 20.0f)
		{
			// 4층을 지나친다. 누른 적 없는 5에 불이 들어온다.
			Lift->ScriptSetButtonLit(TargetFloor, false);
			Lift->ScriptSetButtonLit(AIGElevator::LandingCount, true);
			AIGHorrorHUD::PushThought(this, NSLOCTEXT("IGElevatorOverload", "WhereThought", "…어디로 가는 거야."), 2.4f);
			Cue = 3;
		}
		break;
	case 3:
		if (!Lift->IsMoving() && FMath::IsNearlyEqual(Lift->GetCabZ(), AIGElevator::OverrunZ, 1.0f))
		{
			EnterStage(EStage::AtFive);
		}
		break;
	default:
		break;
	}
}

void AIGElevatorOverloadDirector::TickAtFive()
{
	AIGElevator* Lift = Elevator.Get();
	switch (Cue)
	{
	case 0:
		if (StageSeconds >= 1.8f)
		{
			// 문 너머는 승강장이 아니라 손바닥 자국이 찍힌 벽이다. 거울에는 아무도 없다.
			Lift->ScriptSetDoors(true, 2.2f);
			Lift->SetReflection(EIGElevatorReflection::Absent);
			Cue = 1;
		}
		break;
	case 1:
		if (StageSeconds >= 3.4f)
		{
			const FVector Wall = CabPoint(FVector(0.0f, -95.0f, 140.0f));
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateDarknessInhale(this), Wall, 0.75f, 0.85f, 120.0f, 900.0f);
			AIGHorrorHUD::PushAudioCaptionAt(this,
				NSLOCTEXT("IGElevatorOverload", "InhaleCaption", "벽 너머에서 숨을 들이쉬는 소리"), 2.8f, Wall);
			Cue = 2;
		}
		break;
	case 2:
		if (StageSeconds >= 6.2f)
		{
			// 칸 지붕을 두드린다. 둘, 쉬고, 하나.
			const FVector Roof = CabPoint(FVector(10.0f, 10.0f, AIGElevator::CabHeight + 12.0f));
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateAnswerKnockPattern(this, 0.25f), Roof, 0.9f, 1.0f, 120.0f, 1200.0f);
			AIGHorrorHUD::PushAudioCaptionAt(this,
				NSLOCTEXT("IGElevatorOverload", "RoofKnockCaption", "천장을 두드리는 소리"), 2.8f, Roof);
			Cue = 3;
		}
		break;
	case 3:
		if (StageSeconds >= 8.8f)
		{
			Lift->ScriptSetDoors(false, 0.9f);
			Cue = 4;
		}
		break;
	case 4:
		if (StageSeconds >= 10.0f && Lift->AreDoorsClosed())
		{
			// 줄이 미끄러진다. 45 cm 떨어졌다가 제동이 잡는다.
			Lift->ScriptSetCabLight(0.2f, true);
			Lift->ScriptDrop(45.0f);
			AIGHorrorHUD::PushAudioCaptionAt(this,
				NSLOCTEXT("IGElevatorOverload", "DropCaption", "줄이 미끄러지는 소리"), 2.2f,
				CabPoint(FVector(0.0f, 0.0f, AIGElevator::CabHeight + 30.0f)));
			Cue = 5;
			StageSeconds = 0.0f;
		}
		break;
	case 5:
		if (StageSeconds >= 1.6f && !Lift->IsMoving())
		{
			Lift->ScriptSetCabLight(1.0f, false);
			Lift->ScriptSetButtonLit(AIGElevator::LandingCount, false);
			Lift->ScriptSetButtonLit(TargetFloor, true);
			Lift->ScriptTravelTo(AIGElevator::GetLandingZ(TargetFloor), 105.0f, 80.0f);
			EnterStage(EStage::Returning);
		}
		break;
	default:
		break;
	}
}

void AIGElevatorOverloadDirector::TickReturning()
{
	AIGElevator* Lift = Elevator.Get();
	if (Cue == 0 && Lift->GetStoppedLanding() == TargetFloor)
	{
		IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateDoorChime(this),
			Lift->GetLandingFrontWorldLocation(TargetFloor) + FVector(80.0f, 0.0f, 226.0f), 0.5f, 1.0f, 150.0f, 1400.0f);
		Lift->ScriptSetDoors(true, 1.6f);
		Lift->ScriptSetButtonLit(TargetFloor, false);
		Cue = 1;
		StageSeconds = 0.0f;
	}
	else if (Cue == 1 && StageSeconds >= 1.7f)
	{
		AIGHorrorHUD::PushThought(this, bSawEmptyMirror
			? NSLOCTEXT("IGElevatorOverload", "EmptyMirrorThought", "거울에 아무도 없었어.")
			: NSLOCTEXT("IGElevatorOverload", "NoFifthAfterThought", "…5층은 없는데."), 2.8f);
		bAbsentUntilExit = true;
		Finish(false);
	}
}

void AIGElevatorOverloadDirector::Finish(const bool bAborted)
{
	StopDrone(bAborted ? 0.05f : 0.6f);
	if (AIGElevator* Lift = Elevator.Get())
	{
		Lift->PlayOverloadBuzzer(false);
		Lift->ScriptSetFullLamp(false);
		Lift->ScriptSetSag(0.0f);
		Lift->ClearReflectionFigures();
		Lift->ScriptSetCabLight(1.0f, false);
		Lift->SetScriptedControl(false);
		if (bAborted)
		{
			bAbsentUntilExit = false;
			Lift->SetReflection(EIGElevatorReflection::Normal);
			Lift->ResetForNewRide();
		}
		else if (!bAbsentUntilExit)
		{
			Lift->SetReflection(EIGElevatorReflection::Normal);
		}
	}
	if (!bAborted)
	{
		Autosave();
	}
	UE_LOG(LogIndieGame, Display, TEXT("ELEVATOR_OVERLOAD finish aborted=%d empty_mirror_seen=%d"),
		bAborted ? 1 : 0, bSawEmptyMirror ? 1 : 0);
	Stage = EStage::Done;
	SetActorTickEnabled(bAbsentUntilExit);
}

bool AIGElevatorOverloadDirector::IsLookingAtMirror() const
{
	const AIGElevator* Lift = Elevator.Get();
	const AIGPlayerCharacter* Player = GetPlayer();
	const APlayerController* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
	if (!Lift || !Controller || !Lift->IsPawnInsideCab(Player))
	{
		return false;
	}
	FVector Eye;
	FRotator View;
	Controller->GetPlayerViewPoint(Eye, View);
	const FVector Mirror = CabPoint(FVector(0.0f, 59.0f, 120.0f));
	return FVector::DotProduct(View.Vector(), (Mirror - Eye).GetSafeNormal()) > 0.82f;
}

void AIGElevatorOverloadDirector::AddMirrorFigure()
{
	using namespace IGElevatorOverload;
	AIGElevator* Lift = Elevator.Get();
	const AIGPlayerCharacter* Player = GetPlayer();
	if (!Lift || !Player || !Lift->GetCabRoot())
	{
		return;
	}
	// 그녀가 선 자리와 이미 선 사람의 자리는 비켜 선다.
	const FVector PlayerLocal = Lift->GetCabRoot()->GetComponentTransform().InverseTransformPosition(Player->GetActorLocation());
	for (const FVector2D& Slot : FigureSlots)
	{
		if (FVector2D::Distance(Slot, FVector2D(PlayerLocal.X, PlayerLocal.Y)) < 50.0f)
		{
			continue;
		}
		bool bTaken = false;
		for (const USkeletalMeshComponent* Figure : Lift->GetReflectionFigures())
		{
			if (Figure && FVector2D::Distance(Slot, FVector2D(Figure->GetRelativeLocation())) < 10.0f)
			{
				bTaken = true;
				break;
			}
		}
		if (bTaken)
		{
			continue;
		}
		// 다들 거울 쪽을 보고 선다. 거울 안에서는 그녀를 마주 본다.
		Lift->AddReflectionFigure(FVector(Slot.X, Slot.Y, 0.0f), 90.0f + FMath::FRandRange(-7.0f, 7.0f));
		++FiguresShown;
		if (FiguresShown == 1 || FiguresShown == MirrorFigureCount)
		{
			Lift->PlayCableCreak(0.35f + 0.08f * FiguresShown);
			Lift->ScriptSetSag(2.5f + 0.4f * FiguresShown);
		}
		return;
	}
}

void AIGElevatorOverloadDirector::StartDrone()
{
	AIGElevator* Lift = Elevator.Get();
	if (!Lift || !Lift->GetCabRoot())
	{
		return;
	}
	if (!Drone)
	{
		Drone = NewObject<UAudioComponent>(this, TEXT("ElevatorOverloadDrone"));
		Drone->SetupAttachment(Lift->GetCabRoot());
		Drone->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
		Drone->bAutoActivate = false;
		Drone->bAutoDestroy = false;
		Drone->bOverrideAttenuation = true;
		Drone->AttenuationOverrides.bAttenuate = true;
		Drone->AttenuationOverrides.bSpatialize = true;
		Drone->AttenuationOverrides.AttenuationShapeExtents = FVector(150.0f, 0.0f, 0.0f);
		Drone->AttenuationOverrides.FalloffDistance = 900.0f;
		Drone->SetSound(UIGToneSequenceSoundWave::CreatePresenceLayer(this));
		Drone->RegisterComponent();
		if (UIGMissingFloorAudioSubsystem* Audio = GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			Audio->RegisterComponent(Drone, EIGAudioBus::Entity);
		}
	}
	Drone->SetVolumeMultiplier(1.0f);
	Drone->FadeIn(3.0f, 0.9f);
}

void AIGElevatorOverloadDirector::StopDrone(const float FadeSeconds)
{
	if (Drone && Drone->IsPlaying())
	{
		if (FadeSeconds <= 0.06f)
		{
			Drone->Stop();
		}
		else
		{
			Drone->FadeOut(FadeSeconds, 0.0f);
		}
	}
}

void AIGElevatorOverloadDirector::Autosave() const
{
	UGameInstance* GameInstance = GetGameInstance();
	UIGSaveSubsystem* Save = GameInstance ? GameInstance->GetSubsystem<UIGSaveSubsystem>() : nullptr;
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Save || !Narrative)
	{
		return;
	}
	Save->RequestAutosave(
		FGameplayTag::RequestGameplayTag(TEXT("Chapter.MissingFloor"), false),
		GetWorld()->GetOutermost()->GetFName(),
		FGameplayTag::RequestGameplayTag(TEXT("Checkpoint.MissingFloor.Day"), false));
}

void AIGElevatorOverloadDirector::HandleLoadCompleted(bool bSuccess, FString /*SlotName*/, UIGSaveGame* /*SaveGame*/)
{
	if (bSuccess && IsEventActive())
	{
		Finish(true);
	}
}
