#include "Sequence/IGLeftoverProbe.h"

#include "Camera/PlayerCameraManager.h"
#include "Core/IGGameInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGJeongwooFigure.h"
#include "Entity/IGLiningEntity.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGManagerPatrol.h"
#include "Entity/IGMissingFloorEpilogueDirector.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorLeftoverDirector.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGMissingFloorNightTwoBeatDirector.h"
#include "Entity/IGMissingFloorPuzzleTwoDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "IndieGame.h"
#include "Interaction/IGAccessHatch.h"
#include "Interaction/IGHidingSpot.h"
#include "Interaction/IGInteractable.h"
#include "Interaction/IGLeftoverTarget.h"
#include "Interaction/IGReadableNote.h"
#include "Interaction/IGSwingDoor.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorHints.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGLeftoverProbe
{
	const FName PieceA(TEXT("PieceA"));
	const FName PieceB(TEXT("PieceB"));
	const FName PieceC(TEXT("PieceC"));
	const FName Frame(TEXT("Frame"));
	const FName Gloves(TEXT("JeongwooGloves"));
	const FName ToolPouch(TEXT("ToolPouch"));
	const FName DohaBox(TEXT("DohaBox"));
	const FName Cover(TEXT("Cover"));
}

AIGLeftoverProbe::AIGLeftoverProbe()
{
	PrimaryActorTick.bCanEverTick = true;
}

UIGMissingFloorNarrativeSubsystem* AIGLeftoverProbe::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
}

void AIGLeftoverProbe::Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer)
{
	Scene = InScene;
	Player = InPlayer;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGLeftoverShots"));
	bCleanShots = bShots && FParse::Param(FCommandLine::Get(), TEXT("IGLeftoverCleanShots"));
	bFrames = bShots && FParse::Param(FCommandLine::Get(), TEXT("IGLeftoverFrames"));
	FString Variant;
	FParse::Value(FCommandLine::Get(), TEXT("IGLeftoverProbe="), Variant);
	bDawnVariant = Variant == TEXT("dawn");
	bNightTwoOnly = Variant == TEXT("night2");
	bWallOnly = Variant == TEXT("wall");
	if (UIGGameInstance* GameInstance = Cast<UIGGameInstance>(GetGameInstance()))
	{
		GameInstance->SetRuntimeProfileStage(0);
	}
	UWorld* World = GetWorld();
	for (TActorIterator<AIGMissingFloorLeftoverDirector> It(World); It; ++It)
	{
		Leftover = *It;
	}
	for (TActorIterator<AIGManagerPatrol> It(World); It; ++It)
	{
		Manager = *It;
	}
	for (TActorIterator<AIGNightPhaseDirector> It(World); It; ++It)
	{
		NightPhase = *It;
	}
	if (Manager.IsValid())
	{
		Manager->OnErrandFinished.AddWeakLambda(this, [this](const bool bCaught)
		{
			bErrandFinished = true;
			bErrandCaught = bCaught;
		});
		Manager->OnErrandReturning.AddWeakLambda(this, [this]() { bErrandReturning = true; });
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->ResetNarrative();
	}
	Check(Leftover.IsValid() && Manager.IsValid() && NightPhase.IsValid(), TEXT("stage_has_directors"));
	if (!Leftover.IsValid() || !Manager.IsValid() || !NightPhase.IsValid())
	{
		Finish();
	}
}

void AIGLeftoverProbe::Silence()
{
	UWorld* World = GetWorld();
	// 손님·어둑시니·위층 사람은 재운다. 이 검사는 관리인·덧댄 것·정우만 본다.
	for (TActorIterator<AIGNightThreatDirector> It(World); It; ++It)
	{
		It->SetActorTickEnabled(false);
		World->GetTimerManager().ClearAllTimersForObject(*It);
	}
	for (TActorIterator<AIGListenerEntity> It(World); It; ++It)
	{
		It->SetDormant(true);
	}
}

bool AIGLeftoverProbe::EndNight()
{
	if (!NightPhase.IsValid() || !NightPhase->IsHourActive())
	{
		return true;
	}
	NightPhase->CompleteNightGoal();
	NightPhase->ConcludeNight();
	return !NightPhase->IsHourActive();
}

bool AIGLeftoverProbe::BeginNight(const int32 Night)
{
	if (!EndNight())
	{
		return false;
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->SetNightIndex(Night - 1);
	}
	if (NightPhase.IsValid())
	{
		NightPhase->BeginTheHour(Night);
	}
	Silence();
	return NightPhase.IsValid() && NightPhase->IsHourActive();
}

void AIGLeftoverProbe::Check(const bool bCondition, const TCHAR* Name)
{
	if (!bCondition)
	{
		++Failures;
	}
	UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGLeftoverProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_PROBE %s failures=%d seconds=%.1f"),
		Failures ? TEXT("FAIL") : TEXT("PASS"), Failures, TotalSeconds);
	// 배포본(Shipping)은 로그를 남기지 않는다. 성능 검사가 경로를 끝까지 돌았는지 알 수 있게 결과를 파일로도 쓴다.
	FString ResultPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("IGMissingFloorResultPath="), ResultPath))
	{
		ResultPath.TrimQuotesInline();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ResultPath), true);
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("MISSINGFLOOR_LEFTOVER %s failures=%d seconds=%.1f\n"),
			Failures ? TEXT("FAIL") : TEXT("PASS"), Failures, TotalSeconds), *ResultPath);
	}
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGLeftoverProbe::Stand(const FVector& Feet, const FVector& LookAt)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !PC)
	{
		return;
	}
	if (Character->IsConcealedInHidingSpot())
	{
		Character->LeaveHidingSpotImmediately();
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(Feet + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	const FVector Eye = Character->GetActorLocation() + Character->GetEyeOffsetFromActor();
	PC->SetControlRotation((LookAt - Eye).Rotation());
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
		// 순간 이동한 프레임을 찍으면 모션 블러가 화면 전체를 끈다.
		PC->PlayerCameraManager->SetGameCameraCutThisFrame();
	}
}

void AIGLeftoverProbe::LogBraceGeometry()
{
	AIGMissingFloorLeftoverDirector* Director = Leftover.Get();
	AIGJeongwooFigure* Jeongwoo = Director ? Director->GetJeongwoo() : nullptr;
	UStaticMeshComponent* Board = Director ? Director->GetPropForTesting(FName(TEXT("Storage.Board"))) : nullptr;
	if (!Jeongwoo || !Board || !Jeongwoo->GetBody())
	{
		UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_BRACE missing actors"));
		return;
	}
	const FTransform BoardTransform = Board->GetComponentTransform();
	UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_BRACE jeongwoo feet=%s yaw=%.1f pose=%d"),
		*Jeongwoo->GetActorLocation().ToCompactString(), Jeongwoo->GetActorRotation().Yaw, static_cast<int32>(Jeongwoo->GetPose()));
	for (const TCHAR* Key : {TEXT("Storage.Board"), TEXT("Storage.DohaBox"), TEXT("Storage.Table1"), TEXT("Storage.Table2"),
			TEXT("Storage.PartsSack"), TEXT("Storage.Track")})
	{
		if (UStaticMeshComponent* Prop = Director->GetPropForTesting(FName(Key)))
		{
			const FBox Local = Prop->GetStaticMesh() ? Prop->GetStaticMesh()->GetBoundingBox() : FBox(ForceInit);
			UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_BRACE prop %s visible=%d loc=%s rot=%s local=[%s .. %s] world=[%s .. %s]"),
				Key, Prop->IsVisible() ? 1 : 0, *Prop->GetComponentLocation().ToCompactString(),
				*Prop->GetComponentRotation().ToCompactString(), *Local.Min.ToCompactString(), *Local.Max.ToCompactString(),
				*Prop->Bounds.GetBox().Min.ToCompactString(), *Prop->Bounds.GetBox().Max.ToCompactString());
		}
	}
	for (const TCHAR* Bone : {TEXT("hand_l"), TEXT("hand_r"), TEXT("lowerarm_l"), TEXT("lowerarm_r"), TEXT("foot_l"),
			TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r"), TEXT("pelvis"), TEXT("spine_03"), TEXT("head")})
	{
		const FVector World = Jeongwoo->GetBody()->GetSocketLocation(FName(Bone));
		const FVector OnBoard = BoardTransform.InverseTransformPosition(World);
		const FVector OnBody = Jeongwoo->GetActorTransform().InverseTransformPosition(World);
		UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_BRACE bone %s world=%s board=%s actor=%s"),
			Bone, *World.ToCompactString(), *OnBoard.ToCompactString(), *OnBody.ToCompactString());
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LeftoverBraceGeometry), false);
	Params.AddIgnoredActor(Jeongwoo);
	if (Player.IsValid())
	{
		Params.AddIgnoredActor(Player.Get());
	}
	const FVector Origins[] = {FVector(100.0f, 40.0f, 700.0f), FVector(40.0f, -30.0f, 700.0f), FVector(70.0f, -30.0f, 640.0f)};
	for (const FVector& Origin : Origins)
	{
		for (int32 Ray = 0; Ray < 8; ++Ray)
		{
			const FVector Direction = FRotator(0.0f, 45.0f * Ray, 0.0f).Vector();
			FHitResult Hit;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * 500.0f, ECC_Visibility, Params);
			UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_BRACE ray from=%s yaw=%d hit=%d dist=%.1f at=%s what=%s"),
				*Origin.ToCompactString(), 45 * Ray, bHit ? 1 : 0, bHit ? Hit.Distance : -1.0f,
				*Hit.ImpactPoint.ToCompactString(), Hit.GetComponent() ? *Hit.GetComponent()->GetReadableName() : TEXT("-"));
		}
	}
}

void AIGLeftoverProbe::CheckStorageContact(const bool bBracing, const TCHAR* Prefix)
{
	AIGMissingFloorLeftoverDirector* Director = Leftover.Get();
	AIGJeongwooFigure* Jeongwoo = Director ? Director->GetJeongwoo() : nullptr;
	UStaticMeshComponent* Board = Director ? Director->GetPropForTesting(FName(TEXT("Storage.Board"))) : nullptr;
	UStaticMeshComponent* Box = Director ? Director->GetPropForTesting(FName(TEXT("Storage.DohaBox"))) : nullptr;
	const auto Name = [Prefix](const TCHAR* Suffix) { return FString::Printf(TEXT("%s_%s"), Prefix, Suffix); };
	if (!Jeongwoo || !Jeongwoo->GetBody() || !Board || !Box || !Board->GetStaticMesh() || !Box->GetStaticMesh())
	{
		Check(false, *Name(TEXT("actors")));
		return;
	}
	const FTransform BoardTransform = Board->GetComponentTransform();
	const FBox BoardLocal = Board->GetStaticMesh()->GetBoundingBox();
	// 판 +Y가 구석을, -Y가 방을 본다. 방 쪽 면은 판 좌표 Y 최솟값이다.
	const float RoomFace = BoardLocal.Min.Y;
	const float CornerFace = BoardLocal.Max.Y;
	const auto OnBoard = [&BoardTransform](const FVector& World) { return BoardTransform.InverseTransformPosition(World); };
	const auto Bone = [Jeongwoo](const TCHAR* BoneName) { return Jeongwoo->GetBody()->GetSocketLocation(FName(BoneName)); };

	// 판은 상자 위 모서리(상자 +Y 면 위 변)에 얹혀 있다. 들뜨지도 파고들지도 않는다.
	const FTransform BoxTransform = Box->GetComponentTransform();
	const FBox BoxLocal = Box->GetStaticMesh()->GetBoundingBox();
	float WorstRest = 0.0f;
	for (const float Along : {-0.9f, 0.0f, 0.9f})
	{
		const FVector Edge = BoxTransform.TransformPosition(FVector(BoxLocal.Max.X * Along, BoxLocal.Max.Y, BoxLocal.Max.Z));
		const float Gap = RoomFace - OnBoard(Edge).Y;
		WorstRest = FMath::Abs(Gap) > FMath::Abs(WorstRest) ? Gap : WorstRest;
	}
	UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_CONTACT %s board_on_box_gap=%.2f board_max_x=%.1f box_max_x=%.1f"),
		Prefix, WorstRest, Board->Bounds.GetBox().Max.X, Box->Bounds.GetBox().Max.X);
	Check(WorstRest > -0.6f && WorstRest < 1.5f, *Name(TEXT("board_rests_on_box")));
	Check(Board->Bounds.GetBox().Max.X <= 120.5f && Box->Bounds.GetBox().Max.X <= 120.5f, *Name(TEXT("board_and_box_inside_room")));

	if (!bBracing)
	{
		// 갇힌 정우는 판 뒤 구석에 있다. 몸 어디도 판 구석 쪽 면을 넘지 않는다.
		float Nearest = TNumericLimits<float>::Max();
		for (const TCHAR* Part : {TEXT("pelvis"), TEXT("spine_03"), TEXT("head"), TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r")})
		{
			Nearest = FMath::Min(Nearest, OnBoard(Bone(Part)).Y - CornerFace);
		}
		UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_CONTACT %s jeongwoo_behind_board=%.1f"), Prefix, Nearest);
		Check(Nearest > 5.0f, *Name(TEXT("jeongwoo_behind_board")));
		return;
	}
	// 두 손바닥이 판 방 쪽 면에 닿는다(손 뼈가 면에서 1..8 cm, 판 안쪽 자리).
	bool bHands = true;
	for (const TCHAR* Hand : {TEXT("hand_l"), TEXT("hand_r")})
	{
		const FVector Local = OnBoard(Bone(Hand));
		const float Gap = RoomFace - Local.Y;
		UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_CONTACT %s %s face_gap=%.1f along=%.1f height=%.1f"), Prefix, Hand, Gap, Local.X, Local.Z);
		bHands &= Gap > 1.0f && Gap < 8.0f && Local.X > BoardLocal.Min.X + 3.0f && Local.X < BoardLocal.Max.X - 3.0f
			&& Local.Z > 20.0f && Local.Z < BoardLocal.Max.Z - 5.0f;
	}
	Check(bHands, *Name(TEXT("hands_on_board")));
	// 몸은 판 앞에 선다. 팔꿈치까지는 면에 가깝고 몸통·머리·발은 멀다.
	bool bBody = true;
	for (const TCHAR* Part : {TEXT("pelvis"), TEXT("spine_03"), TEXT("head"), TEXT("foot_l"), TEXT("foot_r"), TEXT("lowerarm_l"), TEXT("lowerarm_r")})
	{
		const float Gap = RoomFace - OnBoard(Bone(Part)).Y;
		const bool bArm = FCString::Strstr(Part, TEXT("arm")) != nullptr;
		bBody &= Gap > (bArm ? 10.0f : 25.0f);
	}
	Check(bBody, *Name(TEXT("body_in_front_of_board")));
	// 발은 바닥(발목 높이)이고 상자·조립대에 들지 않는다.
	bool bFeet = true;
	for (const TCHAR* Foot : {TEXT("foot_l"), TEXT("foot_r")})
	{
		const FVector World = Bone(Foot);
		bFeet &= World.Z > 600.0f + 3.0f && World.Z < 600.0f + 16.0f;
		for (const TCHAR* Key : {TEXT("Storage.DohaBox"), TEXT("Storage.Table1"), TEXT("Storage.Table2"), TEXT("Storage.PartsSack")})
		{
			UStaticMeshComponent* Prop = Director->GetPropForTesting(FName(Key));
			if (Prop && Prop->IsVisible() && Prop->GetStaticMesh())
			{
				const FVector Local = Prop->GetComponentTransform().InverseTransformPosition(World);
				bFeet &= !Prop->GetStaticMesh()->GetBoundingBox().ExpandBy(FVector(12.0f, 12.0f, 0.0f)).IsInsideXY(Local);
			}
		}
	}
	Check(bFeet, *Name(TEXT("feet_planted_clear")));
}

void AIGLeftoverProbe::LogAnnexGeometry()
{
	AIGMissingFloorLeftoverDirector* Director = Leftover.Get();
	AIGLiningEntity* Lining = Director ? Director->GetLining() : nullptr;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LeftoverAnnexGeometry), false);
	if (Lining)
	{
		Params.AddIgnoredActor(Lining);
		UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_ANNEX body=%s"), *Lining->GetBodyCenter().ToCompactString());
		for (int32 Leg = 0; Leg < AIGLiningEntity::LegCount; ++Leg)
		{
			UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_ANNEX pad%d=%s"), Leg, *Lining->GetPadLocation(Leg).ToCompactString());
		}
	}
	if (Player.IsValid())
	{
		Params.AddIgnoredActor(Player.Get());
	}
	const FVector Origins[] = {
		FVector(214.0f, 492.0f, 1330.0f), FVector(240.0f, 492.0f, 1212.0f), FVector(240.0f, 492.0f, 1300.0f),
		FVector(185.0f, 492.0f, 1425.0f), FVector(150.0f, 560.0f, 1300.0f), FVector(100.0f, 600.0f, 1300.0f)};
	const FVector Directions[] = {FVector(0, 1, 0), FVector(0, -1, 0), FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 0, 1), FVector(0, 0, -1)};
	for (const FVector& Origin : Origins)
	{
		for (const FVector& Direction : Directions)
		{
			FHitResult Hit;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * 600.0f, ECC_Visibility, Params);
			UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_ANNEX ray from=%s dir=%s hit=%d dist=%.1f what=%s"),
				*Origin.ToCompactString(), *Direction.ToCompactString(), bHit ? 1 : 0, bHit ? Hit.Distance : -1.0f,
				Hit.GetComponent() ? *Hit.GetComponent()->GetReadableName() : TEXT("-"));
		}
	}
}

void AIGLeftoverProbe::BeginFrames(const TCHAR* Tag)
{
	if (!bFrames)
	{
		return;
	}
	FrameTag = Tag;
	FrameIndex = 0;
	FrameTick = 0;
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_FRAMES begin %s"), Tag);
}

void AIGLeftoverProbe::EndFrames()
{
	if (!FrameTag.IsEmpty())
	{
		UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_FRAMES end %s frames=%d"), *FrameTag, FrameIndex);
	}
	FrameTag.Reset();
}

void AIGLeftoverProbe::TickFrames()
{
	// 고정 60fps 실행에서 두 틱에 한 장, 30fps다. 10초가 넘으면 스스로 멈춘다.
	if (FrameTag.IsEmpty() || (FrameTick++ % 2) != 0)
	{
		return;
	}
	if (FrameIndex >= 300)
	{
		EndFrames();
		return;
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LeftoverFrames"), FrameTag));
	IFileManager::Get().MakeDirectory(*Directory, true);
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, FString::Printf(TEXT("f%04d.png"), FrameIndex++)), false, false);
}

void AIGLeftoverProbe::Shoot(const TCHAR* Name)
{
	if (!bShots)
	{
		return;
	}
	// 밤의 장면은 손전등 하나로 보인다. 회색 상자 실행은 손전등을 줍는 장면을 건너뛰어서
	// 가진 것으로 쳐 주고 켠다.
	if (Player.IsValid() && Player->GetFlashlight() && NightPhase.IsValid() && NightPhase->IsHourActive()
		&& Phase >= 30)
	{
		Player->GetFlashlight()->SetAvailable(true);
		Player->GetFlashlight()->SetOn(true);
	}
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	if (bCleanShots)
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->MyHUD)
		{
			PC->MyHUD->bShowHUD = false;
		}
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LeftoverReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	FScreenshotRequest::RequestScreenshot(
		FPaths::Combine(Directory, FString::Printf(TEXT("%02d_%s.png"), ShotIndex++, Name)), false, false);
}

bool AIGLeftoverProbe::HintIs(const TCHAR* GoalId, const TCHAR* ShotName)
{
	const FIGHintStep Hint = IGMissingFloorHints::Resolve(this);
	const bool bMatch = Hint.IsValid() && Hint.GoalId == FName(GoalId);
	UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_HINT expected=%s got=%s"), GoalId, *Hint.GoalId.ToString());
	if (bMatch && bShots && ShotName)
	{
		AIGHorrorHUD::PushThought(this, Hint.Tiers.Last(), 3.2f);
		HintShotCountdown = 1.2f;
		HintShotName = ShotName;
	}
	return bMatch;
}

void AIGLeftoverProbe::ReadAndShoot(AIGReadableNote* Note, const TCHAR* ShotName)
{
	if (!bShots || bCleanShots || !Note || HintShotCountdown >= 0.0f)
	{
		return;
	}
	Use(Note);
	PendingNote = Note;
	HintShotCountdown = 1.2f;
	HintShotName = ShotName;
}

void AIGLeftoverProbe::Next(const int32 NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
	Step = 0;
	// -IGRuntimeProfile로 프레임을 기록하는 실행이면 단계 번호를 장면 번호로 남긴다(낮1 0번대, 밤2 10번대…).
	if (UIGGameInstance* GameInstance = Cast<UIGGameInstance>(GetGameInstance()))
	{
		GameInstance->SetRuntimeProfileStage(NewPhase);
	}
}

bool AIGLeftoverProbe::Strike(AIGMissingFloorEvidence* Target)
{
	AIGPlayerCharacter* Character = Player.Get();
	if (!Target || !Character || !Target->IsInteractionEnabled())
	{
		return false;
	}
	FIGInteractionContext Context;
	Context.Interactor = Character;
	Context.TargetActor = Target;
	Context.HeldDuration = IIGInteractable::Execute_GetInteractionHoldDuration(Target, Character);
	Context.HoldProgress = 1.0f;
	IIGInteractable::Execute_BeginInteraction(Target, Context);
	IIGInteractable::Execute_CompleteInteraction(Target, Context);
	IIGInteractable::Execute_EndInteraction(Target, Context, EIGInteractionEndReason::Completed);
	return true;
}

AIGLeftoverTarget* AIGLeftoverProbe::Target(const TCHAR* Key) const
{
	return Leftover.IsValid() ? Leftover->FindTarget(FName(Key)) : nullptr;
}

bool AIGLeftoverProbe::Use(AActor* Actor, const bool bTap)
{
	AIGPlayerCharacter* Character = Player.Get();
	if (!Actor || !Character || !Actor->GetClass()->ImplementsInterface(UIGInteractable::StaticClass()))
	{
		return false;
	}
	if (!IIGInteractable::Execute_CanInteract(Actor, Character))
	{
		UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_USE %s refused"), *Actor->GetName());
		return false;
	}
	FIGInteractionContext Context;
	Context.Interactor = Character;
	Context.TargetActor = Actor;
	Context.HeldDuration = bTap ? 0.1f : IIGInteractable::Execute_GetInteractionHoldDuration(Actor, Character);
	Context.HoldProgress = bTap ? 0.1f : 1.0f;
	IIGInteractable::Execute_BeginInteraction(Actor, Context);
	if (bTap)
	{
		IIGInteractable::Execute_EndInteraction(Actor, Context, EIGInteractionEndReason::Released);
		return true;
	}
	IIGInteractable::Execute_CompleteInteraction(Actor, Context);
	IIGInteractable::Execute_EndInteraction(Actor, Context, EIGInteractionEndReason::Completed);
	return true;
}

void AIGLeftoverProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	TickFrames();
	if (HintShotCountdown >= 0.0f)
	{
		HintShotCountdown -= DeltaSeconds;
		if (HintShotCountdown < 0.0f)
		{
			Shoot(*HintShotName);
			if (AIGReadableNote* Note = PendingNote.Get())
			{
				Note->Close();
			}
			PendingNote.Reset();
		}
	}
	if (TotalSeconds > 900.0f)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	AIGMissingFloorLeftoverDirector* Director = Leftover.Get();
	AIGManagerPatrol* Mok = Manager.Get();
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Director || !Mok || !Narrative)
	{
		return;
	}
	const auto Taken = [Narrative](const FName Item) { return Narrative->HasLeftover(EIGLeftoverList::Taken, Item); };
	AIGLiningEntity* Lining = Director->GetLining();
	switch (Phase)
	{
	// -- 낮1: 짐 장면 ------------------------------------------------------------
	case 0:
		// 회색 상자 실행은 첫째 밤이 열린 채 시작한다. 새벽을 맞혀 낮1로 간다.
		if (PhaseSeconds < 3.0f || !EndNight())
		{
			return;
		}
		if (bNightTwoOnly)
		{
			Next(10);
			return;
		}
		if (bWallOnly)
		{
			Next(80);
			return;
		}
		Narrative->SetNightIndex(1);
		Director->SetHourActive(false);
		Silence();
		// 4층에서 계단으로 내려선다. 정우가 2층에서 상자를 안고 올라온다.
		Stand(AIGPrologueWorldScene::GetStairDoorwayFeet(2) + FVector(-90.0f, 40.0f, 140.0f),
			AIGPrologueWorldScene::GetStairDoorwayFeet(2));
		Director->StartDayOneBoxesForTesting();
		Next(1);
		return;
	case 1:
		if (PhaseSeconds > 1.0f && Step == 0)
		{
			Step = 1;
			Check(Director->GetJeongwoo() && Director->GetJeongwoo()->IsShown(), TEXT("day1_jeongwoo_appears_with_boxes"));
			Check(Director->GetJeongwoo() && Director->GetJeongwoo()->GetCarriedBoxes() == 2, TEXT("day1_jeongwoo_carries_two_boxes"));
		}
		if (Step == 1 && Target(TEXT("Day.HoldDoor")) && Target(TEXT("Day.HoldDoor"))->IsInteractionEnabled())
		{
			Step = 2;
			if (Director->GetJeongwoo())
			{
				const FVector At = Director->GetJeongwoo()->GetFeet();
				Stand(At + FVector(-30.0f, 150.0f, 0.0f), At + FVector(0.0f, 0.0f, 120.0f));
			}
			Shoot(TEXT("day1_at_fire_door"));
			Check(Narrative->HasBeatPlayed(FName(TEXT("Day.Jeongwoo.Intro"))), TEXT("day1_name_heard_at_door"));
			Check(Use(Target(TEXT("Day.HoldDoor"))), TEXT("day1_hold_door"));
		}
		if (Step == 2 && Director->GetJeongwoo() && !Director->GetJeongwoo()->IsWalking() && PhaseSeconds > 4.0f
			&& Target(TEXT("Storage.DohaName")) && Target(TEXT("Storage.DohaName"))->IsInteractionEnabled())
		{
			Step = 3;
			Stand(FVector(-40.0f, -110.0f, 600.0f), FVector(-115.0f, -185.0f, 640.0f));
			Shoot(TEXT("day1_boxes_in_302"));
			Check(Use(Target(TEXT("Storage.DohaName"))), TEXT("day1_see_brother_name"));
			// 같은 더미 맨 위 상자의 「주인 없음」 딱지를 들춘다. 밑에 303호의 이름표가 있다.
			Check(Use(Target(TEXT("Storage.Label303"))), TEXT("day1_peel_303_label"));
			Check(Narrative->HasBeatPlayed(FName(TEXT("Leftover.Label303Peeled"))), TEXT("day1_303_label_seen"));
			Check(Target(TEXT("Storage.Label303")) && !Target(TEXT("Storage.Label303"))->IsInteractionEnabled(), TEXT("day1_303_peel_once"));
		}
		if (Step == 3 && Narrative->HasBeatPlayed(FName(TEXT("Leftover.Day1.Marked"))))
		{
			Check(true, TEXT("day1_box_marked"));
			ReadAndShoot(Director->GetCollectionNotice(), TEXT("day1_notice"));
			Step = 4;
		}
		if (Step == 4 && Narrative->HasBeatPlayed(FName(TEXT("Leftover.Day1.Boxes"))))
		{
			Check(!Director->IsSceneActive(), TEXT("day1_scene_closed"));
			Next(10);
			return;
		}
		if (PhaseSeconds > 140.0f)
		{
			Check(false, TEXT("day1_boxes_timeout"));
			Next(10);
		}
		return;

	// -- 밤2: 안쪽 방 -------------------------------------------------------------
	case 10:
		if (!BeginNight(2))
		{
			return;
		}
		// 장부 책상 앞. 그가 깨면 책상 밑에 숨는다(관리실은 좁아서 그냥 서 있으면 지나가다 안다).
		Stand(FVector(160.0f, -175.0f, 0.0f), FVector(160.0f, -110.0f, 80.0f));
		if (Player.IsValid() && Player->GetFlashlight())
		{
			Player->GetFlashlight()->SetOn(false);
		}
		Next(11);
		return;
	case 11:
		if (PhaseSeconds < 4.0f)
		{
			return;
		}
		Check(Director->GetInnerDoor() && !Director->GetInnerDoor()->IsOpen(), TEXT("n2_inner_door_shut_before_p2"));
		Check(Lining && !Lining->IsHidden() && Lining->GetState() == EIGLiningState::Folded, TEXT("n2_lining_folded_on_wall"));
		// 게임에서 P2가 풀리는 길 그대로 두 출처를 적는다. 진실이 서면 P2 감독이 풀린 것으로 표시하고
		// 한 번만 알린다(힌트가 이 표시를 본다). 여기서 따로 알리면 귀환 추격이 같이 걸린다.
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::CarbonLedgerOriginal);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::AgentMoveOutMessage);
		Check(Narrative->IsPuzzleSolved(FName(TEXT("P2"))), TEXT("n2_p2_marked_solved"));
		Check(Director->IsInnerRoomErrandRunning(), TEXT("n2_p2_wakes_inner_room"));
		for (TActorIterator<AIGHidingSpot> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), FVector(160.0f, -137.5f, 0.0f)) < 20.0f)
			{
				Check(Use(*It), TEXT("n2_hide_under_desk"));
			}
		}
		for (TActorIterator<AIGMissingFloorNightTwoBeatDirector> It(GetWorld()); It; ++It)
		{
			Check(It->GetReturnStage() == EIGNightTwoReturnStage::Idle, TEXT("n2_return_not_armed_yet"));
		}
		Next(12);
		return;
	case 12:
		if (PhaseSeconds > 13.0f && Step == 0)
		{
			Step = 1;
			Check(Mok->IsOnErrand(), TEXT("n2_manager_leaves_for_cart"));
			Check(Director->GetInnerDoor() && Director->GetInnerDoor()->IsOpen(), TEXT("n2_inner_door_left_open"));
			Check(Scene.IsValid() && Scene->IsInnerRoomBulbOn(), TEXT("n2_bulb_on"));
			Check(HintIs(TEXT("Hint.Night2.InnerRoom"), TEXT("n2_hint")), TEXT("n2_hint_points_to_inner_room"));
		}
		if (Step == 1 && PhaseSeconds > 34.0f)
		{
			// 필로티에서 사슬을 푸는 중이다. 안쪽 방으로 들어간다.
			Check(Mok->IsOnErrand() && !Mok->IsErrandReturning(), TEXT("n2_manager_busy_at_cart"));
			Stand(FVector(120.0f, 40.0f, 0.0f), FVector(50.0f, 150.0f, 100.0f));
			Shoot(TEXT("n2_inner_room_heap"));
			Use(Target(TEXT("PieceB")));
			Check(!Taken(IGLeftoverProbe::PieceB), TEXT("n2_piece_b_pinned_by_pad"));
			Step = 2;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 2 && PhaseSeconds > 1.0f)
		{
			Stand(FVector(-30.0f, 20.0f, 0.0f), FVector(-100.0f, 85.0f, 140.0f));
			Check(Use(Director->GetHatch(FName(TEXT("Inner.Small")))), TEXT("n2_open_small_hatch"));
			Stand(FVector(150.0f, -20.0f, 0.0f), FVector(-60.0f, 100.0f, 120.0f));
			Step = 3;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 3 && PhaseSeconds > 1.2f && PhaseSeconds < 1.3f)
		{
			Shoot(TEXT("n2_first_peel"));
		}
		if (Step == 3 && PhaseSeconds > 4.0f && PhaseSeconds < 4.1f)
		{
			Shoot(TEXT("n2_travel_small"));
		}
		if (Step == 3 && Narrative->HasLeftover(EIGLeftoverList::GapsSealed, FName(TEXT("Inner.Small"))))
		{
			Check(Narrative->HasBeatPlayed(FName(TEXT("Leftover.N2.FirstMove"))), TEXT("n2_first_move_seen"));
			Shoot(TEXT("n2_pressed_small"));
			Stand(FVector(40.0f, 120.0f, 0.0f), FVector(40.0f, 166.0f, 60.0f));
			Use(Target(TEXT("PieceB")));
			Check(Taken(IGLeftoverProbe::PieceB), TEXT("n2_piece_b_free_after_move"));
			Stand(FVector(110.0f, -30.0f, 0.0f), FVector(190.0f, 170.0f, 60.0f));
			Check(Use(Director->GetHatch(FName(TEXT("Inner.Large")))), TEXT("n2_open_large_hatch"));
			// 몸이 서벽에서 북벽 동쪽으로 건너가는 길 밖. 문간에서 지켜본다.
			Stand(FVector(245.0f, -45.0f, 0.0f), FVector(40.0f, 110.0f, 110.0f));
			Step = 4;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 3 && PhaseSeconds > 25.0f)
		{
			Check(false, TEXT("n2_small_hatch_not_pressed"));
			Step = 4;
		}
		if (Step == 4 && PhaseSeconds > 5.0f && PhaseSeconds < 5.1f)
		{
			Shoot(TEXT("n2_crossing_room"));
		}
		if (Step == 4 && Narrative->HasLeftover(EIGLeftoverList::GapsSealed, FName(TEXT("Inner.Large"))))
		{
			Check(Narrative->HasLeftover(EIGLeftoverList::GapsSealed, FName(TEXT("Inner.Small"))), TEXT("n2_small_stays_sealed"));
			Check(Narrative->GetLeftover().LiningHeldGap == FName(TEXT("Inner.Large")), TEXT("n2_holds_large"));
			Shoot(TEXT("n2_pressed_large"));
			Stand(FVector(-30.0f, 60.0f, 0.0f), FVector(-62.0f, 62.0f, 84.0f));
			Use(Target(TEXT("PieceA")));
			Check(Taken(IGLeftoverProbe::PieceA), TEXT("n2_piece_a_taken"));
			Use(Target(TEXT("Inner.GloveBoxOpen")));
			Use(Target(TEXT("JeongwooGloves")));
			Check(Taken(IGLeftoverProbe::Gloves), TEXT("n2_gloves_taken"));
			ReadAndShoot(Director->GetCabinetLedger(), TEXT("n2_ledger"));
			// 손수레를 빨리 돌아오게 한다.
			Mok->SetErrandFetchSecondsForTesting(1.0f);
			Step = 5;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 4 && PhaseSeconds > 30.0f)
		{
			Check(false, TEXT("n2_large_hatch_not_pressed"));
			Step = 5;
			Mok->SetErrandFetchSecondsForTesting(1.0f);
		}
		if (Step == 5 && bErrandReturning)
		{
			// 손수레 소리가 다가온다. 책상 밑으로 다시 숨는다.
			Stand(FVector(160.0f, -175.0f, 0.0f), FVector(160.0f, -110.0f, 80.0f));
			for (TActorIterator<AIGHidingSpot> It(GetWorld()); It; ++It)
			{
				if (FVector::Dist2D(It->GetActorLocation(), FVector(160.0f, -137.5f, 0.0f)) < 20.0f)
				{
					Check(Use(*It), TEXT("n2_hide_again"));
				}
			}
			Step = 6;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 6 && bErrandFinished)
		{
			Check(!bErrandCaught, TEXT("n2_manager_back_without_catch"));
			Check(Director->GetInnerDoor() && (!Director->GetInnerDoor()->IsOpen() || Director->GetInnerDoor()->IsSwinging()),
				TEXT("n2_inner_door_closed_behind_him"));
			Check(Narrative->HasBeatPlayed(FName(TEXT("Leftover.N2.Errand"))), TEXT("n2_errand_beat"));
			for (TActorIterator<AIGMissingFloorNightTwoBeatDirector> It(GetWorld()); It; ++It)
			{
				Check(It->GetReturnStage() == EIGNightTwoReturnStage::AwaitingExit, TEXT("n2_return_armed_after_errand"));
			}
			Next(13);
			return;
		}
		if (PhaseSeconds > 120.0f)
		{
			Check(false, TEXT("n2_errand_timeout"));
			Next(13);
		}
		return;
	case 13:
		if (PhaseSeconds < 3.0f)
		{
			return;
		}
		if (bNightTwoOnly && PhaseSeconds > 5.0f)
		{
			for (TActorIterator<AIGMissingFloorNightTwoBeatDirector> It(GetWorld()); It; ++It)
			{
				Check(It->GetReturnStage() == EIGNightTwoReturnStage::Chased, TEXT("n2_chase_on_leaving_office"));
			}
			Finish();
			return;
		}
		// 관리실을 나선다. 자재가 무너지고 귀환 추격이 시작된다.
		Stand(FVector(165.0f, -300.0f, 0.0f), FVector(-40.0f, -300.0f, 100.0f));
		if (PhaseSeconds > 4.0f)
		{
			for (TActorIterator<AIGMissingFloorNightTwoBeatDirector> It(GetWorld()); It; ++It)
			{
				Check(It->GetReturnStage() == EIGNightTwoReturnStage::Chased, TEXT("n2_chase_on_leaving_office"));
			}
			// 낮2로 건너간다(밤2 다음 낮).
			if (NightPhase.IsValid())
			{
				NightPhase->CompleteNightGoal();
				NightPhase->ConcludeNight();
			}
			Next(20);
		}
		return;

	// -- 낮2: 정우 -----------------------------------------------------------------
	case 20:
		if (PhaseSeconds < 4.0f)
		{
			return;
		}
		Narrative->SetNightIndex(2);
		Director->SetHourActive(false);
		Check(Director->TryJeongwooTalk(true, true), TEXT("day2_gloves_talk"));
		Check(Narrative->HasLeftover(EIGLeftoverList::Taken, FName(TEXT("JeongwooGlovesGiven"))), TEXT("day2_gloves_returned"));
		Check(!Director->TryJeongwooTalk(true, true), TEXT("day2_talk_once"));
		Next(30);
		return;

	// -- 밤3: 302호 -----------------------------------------------------------------
	case 30:
		if (!BeginNight(3))
		{
			return;
		}
		Stand(FVector(-30.0f, -300.0f, 600.0f), FVector(-30.0f, -150.0f, 680.0f));
		Next(31);
		return;
	case 31:
		if (PhaseSeconds < 3.0f)
		{
			return;
		}
		Check(Narrative->GetLeftover().JeongwooState == FName(TEXT("Trapped")), TEXT("n3_jeongwoo_trapped"));
		Check(Director->GetJeongwoo() && Director->GetJeongwoo()->IsShown(), TEXT("n3_jeongwoo_behind_board"));
		Check(Lining && Lining->GetHeldGap() == FName(TEXT("Storage.Narrow")), TEXT("n3_lining_presses_work_gap"));
		Check(Mok->IsAvoidingStorage(), TEXT("n3_manager_skips_third_floor"));
		Check(HintIs(TEXT("Hint.Night3.Storage"), TEXT("n3_hint")), TEXT("n3_hint_points_to_storage"));
		CheckStorageContact(false, TEXT("n3_trapped"));
		// 문 안쪽에서 넘어진 판과 그 너머 구석을 본다.
		Stand(FVector(-40.0f, -190.0f, 600.0f), FVector(80.0f, 40.0f, 690.0f));
		Shoot(TEXT("n3_trapped"));
		if (bDawnVariant)
		{
			Next(60);
			return;
		}
		Next(38);
		return;
	case 38:
		if (PhaseSeconds < 0.2f)
		{
			return;
		}
		Check(Use(Target(TEXT("Storage.WedgePick"))), TEXT("n3_pick_wedge"));
		Check(Use(Target(TEXT("Storage.Socket1"))), TEXT("n3_wedge_wrong_socket"));
		Check(Narrative->GetLeftover().WedgeSocket == FName(TEXT("Slipped")), TEXT("n3_wrong_wedge_slips"));
		Check(Use(Target(TEXT("Storage.WedgePick"))), TEXT("n3_pick_wedge_again"));
		Check(Use(Target(TEXT("Storage.Socket0"))), TEXT("n3_wedge_right_socket"));
		Check(Narrative->GetLeftover().WedgeSocket == FName(TEXT("Brace")), TEXT("n3_wedge_braces_board"));
		Stand(FVector(-70.0f, 0.0f, 600.0f), FVector(-140.0f, 0.0f, 680.0f));
		Check(Use(Target(TEXT("Storage.Wardrobe"))), TEXT("n3_push_wardrobe"));
		Next(32);
		return;
	case 32:
		if (PhaseSeconds < 2.5f)
		{
			return;
		}
		if (Step == 0)
		{
			Check(Narrative->GetLeftover().bWardrobeMoved, TEXT("n3_wardrobe_moved_once"));
			// 밀어낸 장롱 뒤로 드러난 큰 점검구.
			Stand(FVector(-40.0f, -70.0f, 600.0f), FVector(-160.0f, 0.0f, 650.0f));
			Shoot(TEXT("n3_hatch_revealed"));
			Step = 1;
			PhaseSeconds = 2.5f;
			return;
		}
		if (PhaseSeconds < 2.7f)
		{
			return;
		}
		Stand(FVector(-80.0f, -60.0f, 600.0f), FVector(-160.0f, 0.0f, 660.0f));
		Check(Use(Director->GetHatch(FName(TEXT("Storage.Large")))), TEXT("n3_open_large_hatch"));
		// 덧댄 것이 지나가는 길(북서쪽) 밖, 방 남쪽 가운데에서 지켜본다.
		Stand(FVector(40.0f, -110.0f, 600.0f), FVector(-60.0f, 30.0f, 680.0f));
		BeginFrames(TEXT("n3_move"));
		Next(33);
		return;
	case 33:
		if (Step == 0 && PhaseSeconds > 3.0f)
		{
			Step = 1;
			Shoot(TEXT("n3_lining_moves"));
		}
		if (Director->GetStorageStage() == FName(TEXT("Freed")))
		{
			EndFrames();
			Check(Narrative->GetLeftover().JeongwooState == FName(TEXT("Freed")), TEXT("n3_jeongwoo_freed_after_move"));
			Next(34);
			return;
		}
		if (PhaseSeconds > 40.0f)
		{
			Check(false, TEXT("n3_freed_timeout"));
			Next(34);
		}
		return;
	case 34:
		if (PhaseSeconds < 3.0f)
		{
			return;
		}
		if (Step == 0)
		{
			// 판에서 빠져나온 정우를 방 가운데에서 본다.
			Stand(FVector(-60.0f, -150.0f, 600.0f), FVector(80.0f, 40.0f, 650.0f));
			Shoot(TEXT("n3_freed"));
			// 낮에 못 만난 회차(night2)는 판에서 풀려나며 이름을 댄다. 그래야 결말 B에도 나온다.
			Check(Narrative->HasBeatPlayed(FName(TEXT("Day.Jeongwoo.Intro"))), TEXT("n3_jeongwoo_named_once_freed"));
			Step = 1;
			PhaseSeconds = 3.0f;
			return;
		}
		if (PhaseSeconds < 3.2f)
		{
			return;
		}
		Stand(FVector(50.0f, -105.0f, 600.0f), FVector(105.0f, -100.0f, 640.0f));
		Check(Use(Target(TEXT("Storage.Sack"))), TEXT("n3_open_sack"));
		Check(Taken(IGLeftoverProbe::PieceC) && Taken(IGLeftoverProbe::Frame), TEXT("n3_piece_c_and_frame"));
		// 밀려난 판 밑 상자를 서쪽 끝에서 본다(판 가운데 아래).
		Stand(FVector(25.6f, -40.8f, 600.0f), FVector(56.8f, -10.6f, 617.0f));
		Check(Use(Target(TEXT("Storage.DohaPull"))), TEXT("n3_ask_jeongwoo_to_brace"));
		Next(35);
		return;
	case 35:
		if (Director->GetStorageStage() != FName(TEXT("Bracing")) || (Director->GetJeongwoo() && Director->GetJeongwoo()->IsWalking()))
		{
			if (PhaseSeconds > 15.0f)
			{
				Check(false, TEXT("n3_brace_timeout"));
				Next(36);
			}
			return;
		}
		if (Step == 0)
		{
			// 판을 받친 정우와 그 밑의 오빠 상자를 조금 떨어져서 본다. 손이 면에 닿는지는 옆에서 본다.
			LogBraceGeometry();
			CheckStorageContact(true, TEXT("n3_brace"));
			Stand(FVector(-20.0f, -115.0f, 600.0f), FVector(75.0f, -20.0f, 690.0f));
			Shoot(TEXT("n3_jeongwoo_braces"));
			Step = 1;
			StepSeconds = 0.0f;
			return;
		}
		if (Step == 1)
		{
			StepSeconds += GetWorld()->GetDeltaSeconds();
			if (StepSeconds < 0.2f)
			{
				return;
			}
			Stand(FVector(-76.5f, -29.0f, 600.0f), FVector(71.0f, -61.5f, 700.0f));
			Shoot(TEXT("n3_jeongwoo_braces_side"));
			Step = 2;
			StepSeconds = 0.0f;
			return;
		}
		StepSeconds += GetWorld()->GetDeltaSeconds();
		if (StepSeconds < 0.2f)
		{
			return;
		}
		Stand(FVector(25.6f, -40.8f, 600.0f), FVector(56.8f, -10.6f, 617.0f));
		Use(Target(TEXT("Storage.DohaPull")));
		Check(!Taken(IGLeftoverProbe::DohaBox), TEXT("n3_box_catches_once"));
		Use(Target(TEXT("Storage.DohaPull")));
		Check(Taken(IGLeftoverProbe::DohaBox) && Taken(IGLeftoverProbe::ToolPouch), TEXT("n3_box_and_pouch_out"));
		Next(36);
		return;
	case 36:
		if (Step == 1)
		{
			StepSeconds += GetWorld()->GetDeltaSeconds();
			if (StepSeconds >= 0.2f)
			{
				Stand(FVector(-30.0f, -300.0f, 600.0f), FVector(-30.0f, -268.0f, 700.0f));
				Next(37);
			}
			return;
		}
		if (Director->GetStorageStage() == FName(TEXT("Leaving")) && Director->GetJeongwoo() && !Director->GetJeongwoo()->IsWalking())
		{
			// 방 안에서 문을 잡고 선 정우를 본다.
			Stand(FVector(-40.0f, -140.0f, 600.0f), FVector(-30.0f, -268.0f, 690.0f));
			Shoot(TEXT("n3_jeongwoo_holds_door"));
			Step = 1;
			StepSeconds = 0.0f;
			return;
		}
		if (FMath::FloorToInt(PhaseSeconds) != FMath::FloorToInt(PhaseSeconds - GetWorld()->GetDeltaSeconds()))
		{
			const AIGPlayerCharacter* Character = Player.Get();
			const AIGJeongwooFigure* Figure = Director->GetJeongwoo();
			const AIGLiningEntity* Body = Director->GetLining();
			UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_WAIT t=%.0f stage=%s walking=%d jeongwoo=%s player=%s state=%s lining=%d body=%s"),
				PhaseSeconds, *Director->GetStorageStage().ToString(), Figure && Figure->IsWalking() ? 1 : 0,
				Figure ? *Figure->GetActorLocation().ToCompactString() : TEXT("none"),
				Character ? *Character->GetActorLocation().ToCompactString() : TEXT("none"),
				*Narrative->GetLeftover().JeongwooState.ToString(),
				Body ? static_cast<int32>(Body->GetState()) : -1,
				Body ? *Body->GetBodyCenter().ToCompactString() : TEXT("none"));
		}
		if (PhaseSeconds > 30.0f)
		{
			Check(false, TEXT("n3_leave_timeout"));
			Next(37);
		}
		return;
	case 37:
		if (Narrative->GetLeftover().JeongwooState == FName(TEXT("LeftTogether")))
		{
			Check(true, TEXT("n3_left_together"));
			Next(40);
			return;
		}
		if (PhaseSeconds > 10.0f)
		{
			Check(false, TEXT("n3_left_together"));
			Next(40);
		}
		return;

	// -- 덮개 조립 -------------------------------------------------------------------
	case 40:
	{
		if (PhaseSeconds < 1.0f)
		{
			return;
		}
		Stand(FVector(-50.0f, -185.0f, 600.0f), FVector(34.0f, -185.0f, 634.0f));
		Shoot(TEXT("p6_table"));
		// B를 집어 든다(처음엔 앞면이 위, 90도 돌아간 채).
		Check(Use(Target(TEXT("Assembly.Pick1"))), TEXT("p6_pick_b"));
		Use(Target(TEXT("Assembly.Slot2")));
		Check(Narrative->GetLeftover().AssemblyStage == 0, TEXT("p6_rotated_piece_does_not_fit"));
		Use(Target(TEXT("Assembly.Held")), true);
		Use(Target(TEXT("Assembly.Held")), true);
		Use(Target(TEXT("Assembly.Held")), true);
		Shoot(TEXT("p6_holding_b_front"));
		Next(41);
		return;
	}
	case 41:
		if (Step == 0)
		{
			// 앞면이 위인 채로 B 자리에 대 본다. 얼룩은 맞는데 고정쇠가 들뜬다.
			Use(Target(TEXT("Assembly.Slot1")));
			Shoot(TEXT("p6_clamp_lifts"));
			Step = 1;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 1 && PhaseSeconds > 1.8f)
		{
			Check(!Narrative->HasBeatPlayed(FName(TEXT("Leftover.Assembly.B"))), TEXT("p6_front_up_rejected"));
			Use(Target(TEXT("Assembly.Held")));
			Use(Target(TEXT("Assembly.Slot2")));
			Step = 2;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 2 && PhaseSeconds > 1.8f)
		{
			Check(!Narrative->HasBeatPlayed(FName(TEXT("Leftover.Assembly.B"))), TEXT("p6_wrong_slot_rejected"));
			Use(Target(TEXT("Assembly.Slot1")));
			Check(Narrative->HasBeatPlayed(FName(TEXT("Leftover.Assembly.B"))), TEXT("p6_b_seated"));
			// A: 처음 자세가 180도 돌아간 앞면.
			Use(Target(TEXT("Assembly.Pick0")));
			Use(Target(TEXT("Assembly.Held")), true);
			Use(Target(TEXT("Assembly.Held")), true);
			Use(Target(TEXT("Assembly.Held")));
			Use(Target(TEXT("Assembly.Slot0")));
			Check(Narrative->HasBeatPlayed(FName(TEXT("Leftover.Assembly.A"))), TEXT("p6_a_seated"));
			// C: 270도.
			Use(Target(TEXT("Assembly.Pick2")));
			Use(Target(TEXT("Assembly.Held")), true);
			Use(Target(TEXT("Assembly.Held")));
			Use(Target(TEXT("Assembly.Slot2")));
			Check(Narrative->HasBeatPlayed(FName(TEXT("Leftover.Assembly.C"))), TEXT("p6_c_seated"));
			Check(Narrative->GetLeftover().AssemblyStage == 1, TEXT("p6_assembled"));
			Shoot(TEXT("p6_all_seated_back"));
			Check(Use(Target(TEXT("Assembly.Flip"))), TEXT("p6_flip_assembled"));
			Step = 3;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 3 && PhaseSeconds > 1.0f)
		{
			Shoot(TEXT("p6_front_continuous_stain"));
			Check(Use(Target(TEXT("Assembly.Membrane"))), TEXT("p6_lift_membrane"));
			Step = 4;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 4 && PhaseSeconds > 1.5f)
		{
			Check(Narrative->GetLeftover().AssemblyStage == 2, TEXT("p6_blocked_vent_seen"));
			Shoot(TEXT("p6_blocked_vent"));
			Check(Use(Target(TEXT("Assembly.Take"))), TEXT("p6_take_cover"));
			Check(Taken(IGLeftoverProbe::Cover), TEXT("p6_cover_taken"));
			Next(50);
		}
		return;

	// -- 5층 대 보기와 밤4 덧댄 것 ---------------------------------------------------
	case 50:
		if (PhaseSeconds < 1.0f)
		{
			return;
		}
		Stand(FVector(170.0f, 492.0f, 1200.0f), FVector(250.0f, 492.0f, 1265.0f));
		Check(Use(Target(TEXT("Annex.SiteMatch"))), TEXT("site_match_use"));
		Check(Narrative->GetLeftover().bSiteMatched, TEXT("site_matched"));
		Check(Narrative->HasWitness(EIGMissingFloorWitness::InspectionCoverMatched), TEXT("site_match_witness"));
		Shoot(TEXT("site_match"));
		Next(51);
		return;
	case 51:
	{
		if (PhaseSeconds < 2.0f)
		{
			return;
		}
		if (!BeginNight(4))
		{
			return;
		}
		for (TActorIterator<AIGMissingFloorNightFourDirector> It(GetWorld()); It; ++It)
		{
			NightFour = *It;
		}
		AIGMissingFloorNightFourDirector* Wall = NightFour.Get();
		Check(Wall != nullptr, TEXT("n4_director_found"));
		if (!Wall)
		{
			Next(70);
			return;
		}
		// 위층 사람은 재우고 5층 철문은 빗장을 건다. 이 검사는 벽과 덧댄 것만 본다.
		for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
		{
			It->SetDormant(true);
		}
		for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
		{
			if (AIGSwingDoor* Gate = It->GetAnnexGate())
			{
				Gate->ForceOpenState(false);
				Gate->SetLatched(true, false);
			}
		}
		// 정전 속에서도 손전등으로 본다.
		if (Player.IsValid() && Player->GetFlashlight())
		{
			Player->GetFlashlight()->SetAvailable(true);
			Player->GetFlashlight()->SetOn(true);
		}
		// 마지막 선택이 열리는 진실 셋과 P5 물길.
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::SomeoneInTheWall, EIGMissingFloorSource::PipeAuditionCriterion);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::SomeoneInTheWall, EIGMissingFloorSource::PipeWaterComparison);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::CarbonLedgerOriginal);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::AgentMoveOutMessage);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WaitingForAnAnswer, EIGMissingFloorSource::AnswerRhythmMaterials);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WaitingForAnAnswer, EIGMissingFloorSource::AnswerReturned);
		for (AIGMissingFloorEvidence* Control : {Wall->GetCleaningDrain(), Wall->GetFloatBypass(), Wall->GetTransferPump()})
		{
			if (Control)
			{
				Control->OnExamined.Broadcast(Control);
			}
		}
		Check(Narrative->IsFinalChoiceUnlocked() && Narrative->IsNightFourMaskRunning(), TEXT("n4_mask_running_and_choice_open"));
		if (Lining)
		{
			Lining->OnGrippedPlayer.AddWeakLambda(this, [this](AIGLiningEntity*) { bLiningGripped = true; });
		}
		Stand(FVector(165.0f, 700.0f, 1200.0f), FVector(250.0f, 700.0f, 1340.0f));
		Next(52);
		return;
	}
	case 52:
	{
		AIGMissingFloorNightFourDirector* Wall = NightFour.Get();
		if (!Wall)
		{
			Next(70);
			return;
		}
		using EWall = AIGMissingFloorNightFourDirector::EWallTarget;
		const FIGLeftoverState& State = Narrative->GetLeftover();
		bool bCaptureInFlight = false;
		for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It)
		{
			bCaptureInFlight |= It->IsCaptureResetInFlight();
		}
		const auto Layer = [Narrative](const TCHAR* Name)
		{
			return Narrative->HasLeftover(EIGLeftoverList::WallLayers, FName(Name));
		};
		const bool bPowerCut = Scene.IsValid() && !Scene->IsMissingFloorAnnexPowered();
		if (Step == 0 && PhaseSeconds > 2.0f)
		{
			Check(Lining && !Lining->IsHidden() && Lining->GetState() == EIGLiningState::Folded, TEXT("n4_lining_folded_on_annex_wall"));
			Check(HintIs(TEXT("Hint.Night4.Wall0"), nullptr), TEXT("n4_hint_stage0"));
			Shoot(TEXT("n4_wall_layers"));
			// 박히지 않은 데를 두 번 친다. 단계도 정전도 오지 않는다.
			StrikesBefore = Narrative->GetNightFourWallStrikeCount();
			Check(Strike(Wall->GetWallBreakTarget()), TEXT("n4_wrong_hit_1"));
			Check(Strike(Wall->GetWallBreakTarget()), TEXT("n4_wrong_hit_2"));
			Check(State.WallStage == 0 && Narrative->GetNightFourWallStrikeCount() == StrikesBefore + 2,
				TEXT("n4_wrong_hits_counted_without_stage"));
			Check(!bPowerCut && !Narrative->HasTruth(EIGMissingFloorTruth::StillCoveringIt), TEXT("n4_wrong_hits_do_not_cut_power"));
			Check(Strike(Wall->GetWallTarget(EWall::Survey)) && Layer(TEXT("Survey")), TEXT("n4_survey_wall"));
			Check(Strike(Wall->GetWallTarget(EWall::Joint)) && State.WallStage == 1, TEXT("n4_stage1_joint"));
			Check(HintIs(TEXT("Hint.Night4.Wall1"), TEXT("n4_hint_seam")), TEXT("n4_hint_stage1"));
			Step = 1;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 1 && Lining && Lining->GetHeldGap() == FName(TEXT("Annex.Seam")) && !Lining->IsMoving())
		{
			Check(true, TEXT("n4_lining_presses_seam"));
			Stand(FVector(150.0f, 640.0f, 1200.0f), FVector(250.0f, 680.0f, 1370.0f));
			Shoot(TEXT("n4_seam_pressed"));
			Stand(FVector(165.0f, 640.0f, 1200.0f), FVector(250.0f, 640.0f, 1300.0f));
			const int32 Before = Narrative->GetNightFourWallStrikeCount();
			Check(Strike(Wall->GetWallTarget(EWall::OuterScrews)) && State.WallStage == 1
				&& Narrative->GetNightFourWallStrikeCount() == Before + 1, TEXT("n4_screws_blocked_while_held"));
			Check(Strike(Wall->GetWallTarget(EWall::Overlay)) && State.WallStage == 2, TEXT("n4_stage2_overlay"));
			Check(!Leftover->IsInspectionOverlayOn(), TEXT("n4_inspection_open"));
			// 옮겨 가는 길(벽에 붙은 남쪽 칸 앞)에 일부러 선다. 지나가는 몸판에 눌리고, 뜯은 단계는 남는다.
			bLiningGripped = false;
			Stand(FVector(226.0f, 600.0f, 1200.0f), FVector(250.0f, 600.0f, 1300.0f));
			Step = 2;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 1 && PhaseSeconds > 25.0f)
		{
			Check(false, TEXT("n4_lining_presses_seam"));
			Next(70);
			return;
		}
		if (Step == 2 && Leftover->IsAnnexSpread() && !bCaptureInFlight)
		{
			Check(bLiningGripped, TEXT("n4_pressed_when_standing_in_its_path"));
			Check(State.WallStage == 2 && !Leftover->IsInspectionOverlayOn(), TEXT("n4_press_keeps_wall_stage"));
			Check(Lining->GetHeldGap() == FName(TEXT("Annex.Inspection")), TEXT("n4_moved_to_inspection_and_spread"));
			Stand(FVector(120.0f, 600.0f, 1200.0f), FVector(220.0f, 492.0f, 1320.0f));
			Shoot(TEXT("n4_spread"));
			Check(Strike(Wall->GetWallTarget(EWall::OuterScrews)) && State.WallStage == 2, TEXT("n4_screws_blocked_without_brace"));
			Check(HintIs(TEXT("Hint.Night4.Wall2"), nullptr), TEXT("n4_hint_stage2"));
			Check(Strike(Wall->GetWallTarget(EWall::Brace)), TEXT("n4_brace_used"));
			Step = 3;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 2 && PhaseSeconds > 30.0f)
		{
			Check(false, TEXT("n4_moved_to_inspection_and_spread"));
			Next(70);
			return;
		}
		if (Step == 3 && PhaseSeconds > 1.2f)
		{
			Check(State.bFramePlaced && Lining->IsKneePinned(), TEXT("n4_knee_caught_on_brace"));
			Check(HintIs(TEXT("Hint.Night4.Wall2Braced"), nullptr), TEXT("n4_hint_stage2_braced"));
			Shoot(TEXT("n4_braced"));
			Check(Strike(Wall->GetWallTarget(EWall::OuterScrews)) && State.WallStage == 3, TEXT("n4_stage3_outer_screws"));
			Check(Narrative->HasSource(EIGMissingFloorTruth::StillCoveringIt, EIGMissingFloorSource::BreakerCutIntervention),
				TEXT("n4_stage3_files_breaker_cut"));
			Step = 4;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 4 && PhaseSeconds > 2.4f)
		{
			Check(bPowerCut, TEXT("n4_power_cut_after_stage3"));
			Check(Layer(TEXT("BraceSlip")), TEXT("n4_brace_slips"));
			Stand(FVector(165.0f, 700.0f, 1200.0f), FVector(240.0f, 690.0f, 1300.0f));
			Shoot(TEXT("n4_outer_layer_off"));
			Stand(FVector(120.0f, 600.0f, 1200.0f), FVector(200.0f, 492.0f, 1290.0f));
			Shoot(TEXT("n4_brace_slipped"));
			Check(Strike(Wall->GetWallTarget(EWall::InnerScrewsSouth)) && State.WallStage == 3, TEXT("n4_inner_blocked_until_rebrace"));
			Check(HintIs(TEXT("Hint.Night4.Wall3"), nullptr), TEXT("n4_hint_stage3"));
			Check(Strike(Wall->GetWallTarget(EWall::Rebrace)) && Layer(TEXT("Rebrace")), TEXT("n4_rebrace"));
			Check(HintIs(TEXT("Hint.Night4.Wall3Inner"), TEXT("n4_hint_inner")), TEXT("n4_hint_stage3_inner"));
			Step = 5;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 5 && PhaseSeconds > 0.8f)
		{
			Check(Strike(Wall->GetWallTarget(EWall::InnerScrewsNorth)) && State.WallStage == 4, TEXT("n4_stage4_inner_screws"));
			Check(!Wall->GetWallTarget(EWall::LastClip)->IsInteractionEnabled(), TEXT("n4_hands_numb_during_call"));
			Step = 6;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 6 && Layer(TEXT("PushedOut")))
		{
			Check(PhaseSeconds > 6.0f, TEXT("n4_pushed_out_after_wall_answer"));
			Check(Lining && !Lining->IsKneePinned() && !Lining->IsMoving() && Lining->GetState() == EIGLiningState::Folded,
				TEXT("n4_pushed_out_and_settled"));
			Check(HintIs(TEXT("Hint.Night4.Wall4"), nullptr), TEXT("n4_hint_stage4"));
			Stand(FVector(120.0f, 620.0f, 1200.0f), FVector(205.0f, 520.0f, 1400.0f));
			Shoot(TEXT("n4_pushed_out"));
			Stand(FVector(165.0f, 700.0f, 1200.0f), FVector(250.0f, 700.0f, 1420.0f));
			Shoot(TEXT("n4_last_clip"));
			Check(Strike(Wall->GetWallTarget(EWall::LastClip)) && State.WallStage == 5, TEXT("n4_stage5_last_clip"));
			Step = 7;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 6 && PhaseSeconds > 40.0f)
		{
			Check(false, TEXT("n4_pushed_out_after_wall_answer"));
			Next(70);
			return;
		}
		if (Step == 7 && Narrative->IsNightFourWallOpened())
		{
			Check(Lining && Lining->IsHidden(), TEXT("n4_lining_gone_when_wall_opens"));
			// 결말 쪽에 남는 것. 대치에서 덮개를 대고, A는 공구 주머니를 공방에 두고, B는 정우가
			// 같이 꺼낸 상자부터 옮기고 문을 닫는다(이름을 들은 회차).
			TArray<FText> Replies;
			Wall->BuildConfrontationReplyLinesForTesting(Replies);
			Check(Replies.ContainsByPredicate([](const FText& Line)
			{
				return Line.EqualTo(NSLOCTEXT("IGMissingFloor", "ConfrontationReplyCover", "이 판, 위에서 떼어 왔죠?"));
			}), TEXT("e07_confrontation_names_the_cover"));
			for (TActorIterator<AIGMissingFloorEpilogueDirector> It(GetWorld()); It; ++It)
			{
				const TArray<FText> Workshop = It->BuildWorkshopLinesForTesting();
				Check(Workshop.Num() > 0 && Workshop[0].EqualTo(NSLOCTEXT("IGMissingFloor", "EpilogueWorkshopPouch",
					"가져온 오빠 공구 주머니를 입구 보관 상자에 넣는다.")), TEXT("e07_ending_a_pouch_into_workshop_box"));
				const TArray<FText> Bay = It->BuildServiceBayLinesForTesting();
				Check(Bay.ContainsByPredicate([](const FText& Line)
				{
					return Line.EqualTo(NSLOCTEXT("IGMissingFloor", "EpilogueServiceBayJeongwooDoor", "“먼저 나가세요. 문은 제가 닫을게요.”"));
				}), TEXT("e07_ending_b_jeongwoo_holds_the_door"));
				break;
			}
			Check(Narrative->GetNightFourWallStrikeCount() >= StrikesBefore + 9 && Narrative->GetNightFourWallProgress() == 5,
				TEXT("n4_strikes_and_stages_counted_apart"));
			Stand(FVector(165.0f, 700.0f, 1200.0f), FVector(300.0f, 700.0f, 1290.0f));
			Shoot(TEXT("n4_wall_open"));
			Next(70);
			return;
		}
		if (Step == 7 && PhaseSeconds > 10.0f)
		{
			Check(false, TEXT("n4_lining_gone_when_wall_opens"));
			Next(70);
		}
		return;
	}

	// -- 벽 단계마다 되살려 보기(=wall) --------------------------------------------------
	case 85:
	{
		// 5층 영상: 0 처음 자리에서 이음매로, 1 이음매에서 점검구로 옮겨 몸을 편다. 칸 가운데에서 본다.
		AIGMissingFloorNightFourDirector* Wall = NightFour.Get();
		AIGLiningEntity* Body = Director ? Director->GetLining() : nullptr;
		FIGLeftoverState& State = Narrative->MutableLeftover();
		if (!Wall || !Body)
		{
			Check(false, TEXT("frames_wall_director"));
			Finish();
			return;
		}
		if (Step == 0)
		{
			State.WallStage = WallShotStage == 7 ? 0 : 1;
			State.bFramePlaced = false;
			for (const TCHAR* Layer : {TEXT("BraceSlip"), TEXT("Rebrace"), TEXT("PushedOut")})
			{
				Narrative->RemoveLeftover(EIGLeftoverList::WallLayers, FName(Layer));
			}
			Wall->RestoreWallForTesting();
			Stand(FVector(20.0f, 650.0f, 1200.0f), FVector(215.0f, 560.0f, 1330.0f));
			Step = 1;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 1 && PhaseSeconds > 2.0f)
		{
			if (WallShotStage == 7)
			{
				BeginFrames(TEXT("n4_seam"));
				Director->OpenAnnexSeam();
			}
			else
			{
				BeginFrames(TEXT("n4_spread"));
				Director->OpenAnnexInspection();
			}
			Step = 2;
			PhaseSeconds = 0.0f;
			return;
		}
		const bool bSettled = WallShotStage == 7
			? Body->GetHeldGap() == FName(TEXT("Annex.Seam")) && !Body->IsMoving()
			: Director->IsAnnexSpread();
		if (Step == 2 && (bSettled || PhaseSeconds > 12.0f))
		{
			Check(bSettled, *FString::Printf(TEXT("frames_%s_settled"), WallShotStage == 7 ? TEXT("seam") : TEXT("spread")));
			Step = 3;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 3 && PhaseSeconds > 1.2f)
		{
			EndFrames();
			if (++WallShotStage >= 9)
			{
				Finish();
				return;
			}
			Step = 0;
		}
		return;
	}
	case 80:
	{
		if (PhaseSeconds < 1.0f || !BeginNight(4))
		{
			return;
		}
		for (TActorIterator<AIGMissingFloorNightFourDirector> It(GetWorld()); It; ++It)
		{
			NightFour = *It;
		}
		for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
		{
			It->SetDormant(true);
		}
		if (Player.IsValid() && Player->GetFlashlight())
		{
			Player->GetFlashlight()->SetAvailable(true);
			Player->GetFlashlight()->SetOn(true);
		}
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::SomeoneInTheWall, EIGMissingFloorSource::PipeAuditionCriterion);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::SomeoneInTheWall, EIGMissingFloorSource::PipeWaterComparison);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::CarbonLedgerOriginal);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::AgentMoveOutMessage);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WaitingForAnAnswer, EIGMissingFloorSource::AnswerRhythmMaterials);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WaitingForAnAnswer, EIGMissingFloorSource::AnswerReturned);
		if (AIGMissingFloorNightFourDirector* Wall = NightFour.Get())
		{
			for (AIGMissingFloorEvidence* Control : {Wall->GetCleaningDrain(), Wall->GetFloatBypass(), Wall->GetTransferPump()})
			{
				if (Control)
				{
					Control->OnExamined.Broadcast(Control);
				}
			}
		}
		WallShotStage = 0;
		Next(81);
		return;
	}
	case 81:
	{
		// 단계 하나를 저장 상태로 세우고 덧댄 것이 자리를 잡을 때까지 둔다.
		AIGMissingFloorNightFourDirector* Wall = NightFour.Get();
		if (!Wall)
		{
			Check(false, TEXT("wall_director_found"));
			Finish();
			return;
		}
		FIGLeftoverState& State = Narrative->MutableLeftover();
		const auto SetLayer = [Narrative](const TCHAR* Name, const bool bOn)
		{
			if (bOn)
			{
				Narrative->AddLeftover(EIGLeftoverList::WallLayers, FName(Name));
			}
			else
			{
				Narrative->RemoveLeftover(EIGLeftoverList::WallLayers, FName(Name));
			}
		};
		if (Step == 0)
		{
			// 0 처음, 1 이음매, 2 점검구에서 편 몸(틀 전), 3 틀에 걸림, 4 바깥층 떨어지고 틀 밀림,
			// 5 다시 세움, 6 밀려남. 벽이 열린 모습은 흐름 검사가 본다.
			const int32 Stages[] = {0, 1, 2, 2, 3, 3, 4};
			State.WallStage = Stages[WallShotStage];
			State.bFramePlaced = WallShotStage >= 3;
			SetLayer(TEXT("Survey"), WallShotStage >= 1);
			SetLayer(TEXT("BraceSlip"), WallShotStage >= 4);
			SetLayer(TEXT("Rebrace"), WallShotStage >= 5);
			SetLayer(TEXT("PushedOut"), WallShotStage >= 6);
			Wall->RestoreWallForTesting();
			Step = 1;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 1 && PhaseSeconds > 2.0f)
		{
			Stand(FVector(150.0f, 700.0f, 1200.0f), FVector(250.0f, 700.0f, 1330.0f));
			Step = 2;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 2 && PhaseSeconds > 0.3f)
		{
			Shoot(*FString::Printf(TEXT("wall%d_bay"), WallShotStage));
			Step = 3;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 3 && PhaseSeconds > 0.2f)
		{
			Stand(FVector(110.0f, 640.0f, 1200.0f), FVector(220.0f, 500.0f, 1330.0f));
			Step = 4;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 4 && PhaseSeconds > 0.3f)
		{
			if (WallShotStage == 2)
			{
				LogAnnexGeometry();
			}
			Shoot(*FString::Printf(TEXT("wall%d_corner"), WallShotStage));
			const bool bPushed = WallShotStage >= 6;
			const bool bSpread = WallShotStage >= 2 && !bPushed;
			Check(Lining && !Lining->IsHidden() && Leftover->IsAnnexSpread() == bSpread,
				*FString::Printf(TEXT("wall%d_restored_lining_pose"), WallShotStage));
			Check(Lining && Lining->IsKneePinned() == (WallShotStage >= 3 && !bPushed),
				*FString::Printf(TEXT("wall%d_restored_knee_pin"), WallShotStage));
			Check(Leftover->IsInspectionOverlayOn() == (WallShotStage < 2),
				*FString::Printf(TEXT("wall%d_restored_overlay"), WallShotStage));
			Step = 5;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 5 && WallShotStage == 2 && !bWallFullShotTaken && PhaseSeconds > 0.2f)
		{
			// 편 몸 전체를 칸 가운데쯤에서 본다(소개 문서 사진 후보). 틀은 아직 남쪽 벽에 기대 있다.
			Stand(FVector(20.0f, 650.0f, 1200.0f), FVector(215.0f, 545.0f, 1325.0f));
			Step = 50;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 50 && PhaseSeconds > 0.3f)
		{
			Shoot(TEXT("wall2_full"));
			bWallFullShotTaken = true;
			Step = 5;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 5 && PhaseSeconds > 0.2f)
		{
			// 천장 구석. 무릎이 틀 위 런너에 걸린 자리와 밀려난 몸.
			Stand(FVector(115.0f, 590.0f, 1200.0f), FVector(205.0f, 490.0f, 1415.0f));
			Step = 6;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 6 && PhaseSeconds > 0.3f)
		{
			Shoot(*FString::Printf(TEXT("wall%d_knee"), WallShotStage));
			Step = 7;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 7 && PhaseSeconds > 0.2f)
		{
			// 칸 위쪽. 덧판과 이음 테이프, 드러난 나사, 바깥층을 뗀 뒤의 철물.
			Stand(FVector(160.0f, 700.0f, 1200.0f), FVector(250.0f, 700.0f, 1395.0f));
			Step = 8;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 8 && PhaseSeconds > 0.3f)
		{
			Shoot(*FString::Printf(TEXT("wall%d_top"), WallShotStage));
			Step = 9;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 9 && PhaseSeconds > 0.2f)
		{
			if (++WallShotStage >= 7)
			{
				if (bFrames)
				{
					Next(85);
					return;
				}
				Finish();
				return;
			}
			Step = 0;
		}
		return;
	}

	// -- 새벽에 혼자 빠져나가는 길(=dawn) ------------------------------------------------
	case 60:
		if (PhaseSeconds < 2.0f)
		{
			return;
		}
		if (NightPhase.IsValid())
		{
			NightPhase->CompleteNightGoal();
			NightPhase->ConcludeNight();
		}
		Next(61);
		return;
	case 61:
		if (PhaseSeconds < 6.0f)
		{
			return;
		}
		Check(Narrative->GetLeftover().JeongwooState == FName(TEXT("SelfEscaped")), TEXT("dawn_jeongwoo_self_escaped"));
		Check(!Director->GetJeongwoo() || !Director->GetJeongwoo()->IsShown(), TEXT("dawn_jeongwoo_gone"));
		Next(70);
		return;

	// -- 저장 왕복과 확장 전 저장 ---------------------------------------------------------
	case 70:
	{
		// 오빠 공구 주머니를 가져온 회차: 403호에 들어서면 책상에 놓이고, 살펴보면 빈 고리가 보인다.
		if (!bDawnVariant && !bNightTwoOnly && !bWallOnly && Step == 0)
		{
			Stand(FVector(-20.0f, -120.0f, 900.0f), FVector(-58.0f, -172.0f, 975.0f));
			Step = 1;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 1)
		{
			if (PhaseSeconds < 0.8f)
			{
				return;
			}
			AIGLeftoverTarget* Look = Target(TEXT("Home.PouchLook"));
			Check(Look && Look->IsInteractionEnabled(), TEXT("home_pouch_on_desk"));
			Check(Look && Use(Look), TEXT("home_pouch_examined"));
			Check(Narrative->HasBeatPlayed(FName(TEXT("Leftover.PouchLoopSeen"))), TEXT("home_pouch_loop_seen"));
			Shoot(TEXT("home_pouch"));
			Step = 2;
			PhaseSeconds = 0.0f;
			return;
		}
		if (Step == 2 && PhaseSeconds < 0.2f)
		{
			return;
		}
		const FIGMissingFloorNarrativeSnapshot Saved = Narrative->GetSnapshot();
		Narrative->ResetNarrative();
		Narrative->RestoreSnapshot(Saved);
		const FIGLeftoverState& After = Narrative->GetLeftover();
		Check(After.Version == 1, TEXT("save_keeps_version"));
		if (!bDawnVariant)
		{
			Check(After.AssemblyStage == 2 && After.bSiteMatched && After.bWardrobeMoved, TEXT("save_keeps_assembly_and_room"));
			Check(After.JeongwooState == FName(TEXT("LeftTogether")), TEXT("save_keeps_jeongwoo"));
			Check(Narrative->HasLeftover(EIGLeftoverList::Taken, IGLeftoverProbe::ToolPouch), TEXT("save_keeps_taken"));
		}
		// 확장 전 저장: 밤4, 망치질 둘, 벽은 닫힘 → 예전 다섯 번 순서로 끝낸다.
		FIGMissingFloorNarrativeSnapshot Old = Saved;
		Old.SchemaVersion = 3;
		Old.Leftover = FIGLeftoverState();
		Old.Night.NightIndex = 4;
		Old.Night.NightFourWallStrikeCount = 2;
		Old.Night.bNightFourWallOpened = false;
		Narrative->RestoreSnapshot(Old);
		Check(Narrative->GetLeftover().bLegacyWallSequence, TEXT("old_save_mid_hammer_keeps_old_sequence"));
		Check(Narrative->GetLeftover().Taken.IsEmpty(), TEXT("old_save_has_no_leftover_items"));
		Old.Night.NightFourWallStrikeCount = 0;
		Narrative->RestoreSnapshot(Old);
		Check(!Narrative->GetLeftover().bLegacyWallSequence, TEXT("old_save_before_hammer_uses_new_rules"));
		// 확장 전 저장에는 Leftover.* 비트가 있을 수 없다. 둘째 밤 저장을 이어 하면 안쪽 방 장면은 아직이다.
		Old.Night.NightIndex = 2;
		Old.Night.CompletedBeats.RemoveAll([](const FName& Beat) { return Beat.ToString().StartsWith(TEXT("Leftover.")); });
		Narrative->RestoreSnapshot(Old);
		Check(!Narrative->HasBeatPlayed(FName(TEXT("Leftover.N2.Errand"))), TEXT("old_save_unseen_scene_not_marked"));
		Check(Narrative->GetLeftover().JeongwooState.IsNone() && Narrative->GetLeftover().AssemblyStage == 0,
			TEXT("old_save_starts_leftover_fresh"));
		Finish();
		return;
	}
	default:
		return;
	}
}
