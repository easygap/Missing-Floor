#include "Sequence/IGLeftoverProbe.h"

#include "AudioMixerBlueprintLibrary.h"
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
#include "Narrative/IGStoryHelpers.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGInteractionComponent.h"
#include "Player/IGPlayerCharacter.h"
#include "InputCoreTypes.h"
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
	bDawnBraceVariant = Variant == TEXT("dawnbrace");
	bNightTwoOnly = Variant == TEXT("night2");
	bWallOnly = Variant == TEXT("wall");
	bNight3Av = Variant == TEXT("night3av");
	bInputVariant = Variant == TEXT("input") || bNight3Av;
	bRecordAV = bNight3Av && FParse::Param(FCommandLine::Get(), TEXT("IGLeftoverRecord"));
	bLinesVariant = Variant == TEXT("lines");
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
	if (bRecordingAV)
	{
		const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Night3AV")));
		IFileManager::Get().MakeDirectory(*Directory, true);
		UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, TEXT("mix"), Directory);
		AvEvent(TEXT("record_stop"));
		bRecordingAV = false;
	}
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

bool AIGLeftoverProbe::HasLinesIn(const FName Context) const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	return Hud && Hud->HasDialogueInContextForTesting(Context);
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
	// 고정 60fps 실행에서 두 틱에 한 장, 30fps다. 25초가 넘으면 스스로 멈춘다.
	if (FrameTag.IsEmpty() || (FrameTick++ % 2) != 0)
	{
		return;
	}
	if (FrameIndex >= 750)
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
	// 녹화 실행(night3av -Record)도 오프스크린으로 그리므로 어두운 장면과 덧댄 것을 몇 장 남긴다.
	if (!bShots && !bRecordAV)
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
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->MyHUD)
	{
		if (bCleanShots)
		{
			PC->MyHUD->bShowHUD = false;
		}
		if (AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(PC->MyHUD))
		{
			Hud->ClearChapterCardForTesting();
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
		if (bInputVariant)
		{
			// 입력 길은 밤3 302호부터다. 그 앞 장면은 전체 검사가 본다.
			Next(30);
			return;
		}
		if (bLinesVariant)
		{
			Next(400);
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
		if (Lining)
		{
			// 틈을 누른 채 짚은 다리가 마감판·상자·바닥을 뚫지 않는다.
			bool bClear = true;
			for (int32 Leg = 0; Leg < AIGLiningEntity::LegCount; ++Leg)
			{
				const FString Hit = Lining->FindLegObstruction(Leg);
				UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_LEG n3 leg=%d pad=%s knee=%s hit=%s"), Leg,
					*Lining->GetPadLocation(Leg).ToCompactString(), *Lining->GetKneeLocation(Leg).ToCompactString(),
					Hit.IsEmpty() ? TEXT("none") : *Hit);
				if (Leg == 0)
				{
					UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_LEG n3 body=%s up=%s"), *Lining->GetBodyCenter().ToCompactString(),
						*Lining->GetBodyUp().ToCompactString());
					if (const UStaticMeshComponent* Board = Director->GetPropForTesting(FName(TEXT("Storage.Board"))))
					{
						UE_LOG(LogIndieGame, Display, TEXT("LEFTOVER_LEG n3 board loc=%s rot=%s box=%s collision=%d"),
							*Board->GetComponentLocation().ToCompactString(), *Board->GetComponentRotation().ToCompactString(),
							*Board->Bounds.GetBox().ToString(), static_cast<int32>(Board->GetCollisionEnabled()));
					}
				}
				bClear &= Hit.IsEmpty();
			}
			Check(bClear, TEXT("n3_lining_legs_clear_of_props"));
		}
		Check(Mok->IsAvoidingStorage(), TEXT("n3_manager_skips_third_floor"));
		Check(HintIs(TEXT("Hint.Night3.Storage"), TEXT("n3_hint")), TEXT("n3_hint_points_to_storage"));
		CheckStorageContact(false, TEXT("n3_trapped"));
		// 문 안쪽에서 넘어진 판과 그 너머 구석을 본다.
		Stand(FVector(-40.0f, -190.0f, 600.0f), FVector(80.0f, 40.0f, 690.0f));
		Shoot(TEXT("n3_trapped"));
		Step = 0;
		StepSeconds = 0.0f;
		Next(310);
		return;
	case 310:
		// 북쪽 벽을 따라 서쪽에서, 그리고 가까이에서 틈을 누른 몸과 판을 같이 본다(다리가 판을 뚫는지).
		if (!bShots)
		{
			Next(311);
			return;
		}
		StepSeconds += GetWorld()->GetDeltaSeconds();
		if (Step == 0 && StepSeconds >= 0.2f)
		{
			Stand(FVector(-125.0f, 30.0f, 600.0f), FVector(40.0f, 60.0f, 690.0f));
			Shoot(TEXT("n3_trapped_west"));
			Step = 1;
			StepSeconds = 0.0f;
		}
		else if (Step == 1 && StepSeconds >= 0.2f)
		{
			Stand(FVector(-10.0f, -70.0f, 600.0f), FVector(30.0f, 60.0f, 700.0f));
			Shoot(TEXT("n3_trapped_close"));
			Step = 2;
			StepSeconds = 0.0f;
		}
		else if (Step == 2 && StepSeconds >= 0.2f)
		{
			Step = 0;
			Next(311);
		}
		return;
	case 311:
		if (bDawnVariant)
		{
			Next(60);
			return;
		}
		if (bInputVariant)
		{
			// 여기서부터는 순간 이동하지 않는다. 302호 문 밖 복도에서 시작한다.
			Stand(FVector(-30.0f, -330.0f, 600.0f), FVector(-30.0f, -150.0f, 660.0f));
			if (bNight3Av)
			{
				// 준비(상태): 관리실에서 열쇠 꾸러미를 가져왔고, 입주 날 음성 메시지(「둘, 하나」)를 들었다.
				// 관리인은 세워 둔다(이 길은 302호와 5층을 본다. 관리인 순찰은 열쇠 찾기 검사가 본다).
				IGStory::AddState(this, FGameplayTag::RequestGameplayTag(FName(TEXT("State.MissingFloor.HasStairKey")), false));
				Narrative->RegisterTruthSource(EIGMissingFloorTruth::WaitingForAnAnswer, EIGMissingFloorSource::AnswerRhythmVoicemail);
				Mok->SetActorTickEnabled(false);
				AvEvent(TEXT("setup teleport=3f_corridor_outside_302 state=HasStairKey,AnswerRhythmVoicemail asleep=manager,guest,darkness,listener"));
				if (bRecordAV)
				{
					UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 900.0f);
					bRecordingAV = true;
					AvEvent(TEXT("record_start"));
				}
			}
			BuildStorageInputRoute();
			Next(200);
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
			// 꺼낸 뒤에는 갇혔을 때의 부름과 반응이 떠 있거나 밀려 있지 않다.
			Check(!HasLinesIn(IGLeftoverDialogue::Trapped), TEXT("n3_trapped_lines_gone_after_rescue"));
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
		// 덧댄 것이 큰 점검구에 자리 잡을 때까지 그 길 밖에서 기다린다. 정우는 「판이 풀렸어요」를 다
		// 한 뒤에 나오므로, 덧댄 것이 아직 지나가는 중일 수 있다. 그 길에 서면 붙들린다.
		if (Step == 0 && Narrative->GetLeftover().LiningHeldGap != FName(TEXT("Storage.Large")))
		{
			if (PhaseSeconds > 20.0f)
			{
				Check(false, TEXT("n3_lining_settles_before_freed_shot"));
				PhaseSeconds = 3.0f;
				Step = 1;
			}
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
		// 처음 받칠 때까지만 기다린다. 그 뒤에는 정우가 판을 놓고 문으로 가는 것까지 단계마다 따로 본다.
		if (Step == 0 && (Director->GetStorageStage() != FName(TEXT("Bracing")) || (Director->GetJeongwoo() && Director->GetJeongwoo()->IsWalking())))
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
			// 붙들려 화면이 어두워진 사이에 찍으면 팔을 볼 수 없다.
			bool bCaptureRunning = false;
			for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It)
			{
				bCaptureRunning |= It->IsCaptureResetInFlight();
			}
			Check(!bCaptureRunning, TEXT("n3_brace_seen_without_capture"));
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
		if (bDawnBraceVariant)
		{
			// 받치는 한가운데 새벽이 온다. 그녀가 꺼낸 정우는 같이 나간 것이고, 못 뺀 상자는 정우가 챙긴다.
			if (NightPhase.IsValid())
			{
				NightPhase->CompleteNightGoal();
				NightPhase->ConcludeNight();
			}
			Next(62);
			return;
		}
		if (Step == 2)
		{
			// 당기지 않고 기다린다. 받치는 장면이 밤 내내 남지 않고 판을 놓아야 한다.
			Stand(FVector(-20.0f, -115.0f, 600.0f), FVector(75.0f, -20.0f, 690.0f));
			Step = 3;
			StepSeconds = 0.0f;
			return;
		}
		if (Step == 3)
		{
			if (Director->GetStorageStage() == FName(TEXT("Leaving")))
			{
				Check(StepSeconds > 30.0f, TEXT("n3_brace_waits_before_release"));
				Check(true, TEXT("n3_brace_released_when_idle"));
				Stand(FVector(25.6f, -40.8f, 600.0f), FVector(56.8f, -10.6f, 617.0f));
				Check(Use(Target(TEXT("Storage.DohaPull"))), TEXT("n3_ask_again_from_door"));
				Step = 4;
				StepSeconds = 0.0f;
				return;
			}
			if (StepSeconds > 60.0f)
			{
				Check(false, TEXT("n3_brace_released_when_idle"));
				Step = 4;
				StepSeconds = 0.0f;
			}
			return;
		}
		if (Step == 4)
		{
			if (Director->GetStorageStage() != FName(TEXT("Bracing")) || (Director->GetJeongwoo() && Director->GetJeongwoo()->IsWalking()))
			{
				if (StepSeconds > 20.0f)
				{
					Check(false, TEXT("n3_jeongwoo_returns_to_brace"));
					Step = 5;
				}
				return;
			}
			Check(true, TEXT("n3_jeongwoo_returns_to_brace"));
			Step = 5;
			StepSeconds = 0.0f;
			return;
		}
		Stand(FVector(25.6f, -40.8f, 600.0f), FVector(56.8f, -10.6f, 617.0f));
		Use(Target(TEXT("Storage.DohaPull")));
		Check(!Taken(IGLeftoverProbe::DohaBox), TEXT("n3_box_catches_once"));
		Check(HasLinesIn(IGLeftoverDialogue::BoxStuck), TEXT("n3_box_stuck_line_queued"));
		Use(Target(TEXT("Storage.DohaPull")));
		Check(Taken(IGLeftoverProbe::DohaBox) && Taken(IGLeftoverProbe::ToolPouch), TEXT("n3_box_and_pouch_out"));
		// 상자가 빠졌다. 걸렸다는 말과 받치는 동안의 말은 떠 있거나 밀려 있지 않다.
		Check(!HasLinesIn(IGLeftoverDialogue::BoxStuck) && !HasLinesIn(IGLeftoverDialogue::Brace),
			TEXT("n3_box_stuck_line_cancelled"));
		Next(36);
		return;
	case 36:
		if (Step == 1)
		{
			StepSeconds += GetWorld()->GetDeltaSeconds();
			if (StepSeconds >= 0.2f)
			{
				Stand(FVector(-30.0f, -300.0f, 600.0f), FVector(-100.0f, -262.0f, 700.0f));
				Next(37);
			}
			return;
		}
		if (Director->GetStorageStage() == FName(TEXT("Leaving")) && Director->GetJeongwoo() && !Director->GetJeongwoo()->IsWalking())
		{
			// 방 안 동쪽에서 문간 너머로, 문설주를 잡고 선 정우를 본다. 문간은 비어 있어야 한다.
			Stand(FVector(40.0f, -170.0f, 600.0f), FVector(-100.0f, -262.0f, 700.0f));
			{
				const AIGJeongwooFigure* Figure = Director->GetJeongwoo();
				const FVector Feet = Figure ? Figure->GetActorLocation() : FVector::ZeroVector;
				// 문간 가운데(X -30)를 지나는 플레이어 캡슐(30)과 정우 캡슐(26)이 닿지 않는다.
				Check(Figure && FMath::Abs(Feet.X - (-30.0f)) > 26.0f + 30.0f + 4.0f && Feet.Y < -240.0f,
					TEXT("n3_door_holder_leaves_doorway_clear"));
			}
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
			// 소개 문서용 넓은 컷을 먼저 제 프레임에 찍는다. 몸판과 천장·벽 아래로 뻗은 다리가 한 화면에
			// 들어오게 물러선다(2026-10-15에 다시 지은 몸). 한 프레임에 자리를 두 번 바꾸면 마지막 자리만 찍힌다.
			if (bCleanShots && !bSpreadWideShot)
			{
				bSpreadWideShot = true;
				Stand(FVector(30.0f, 640.0f, 1200.0f), FVector(245.0f, 500.0f, 1320.0f));
				Shoot(TEXT("n4_spread_wide"));
				PhaseSeconds = 0.0f;
				return;
			}
			if (bCleanShots && PhaseSeconds < 0.4f)
			{
				return;
			}
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
			// 이음매로 가는 길은 처음 자리(Y 640)와 이음매(Y 700) 쪽을, 몸을 펴는 길은 점검구 위 벽을 본다.
			if (WallShotStage == 7)
			{
				Stand(FVector(40.0f, 700.0f, 1200.0f), FVector(250.0f, 670.0f, 1360.0f));
			}
			else
			{
				Stand(FVector(20.0f, 650.0f, 1200.0f), FVector(215.0f, 560.0f, 1330.0f));
			}
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
		// 점검구로 옮겨 누르고 몸을 펴기까지 실제 판(밤4 검사)에서 20초 남짓 걸린다.
		if (Step == 2 && (bSettled || PhaseSeconds > 28.0f))
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

	// -- 실제 입력 길(=input) ------------------------------------------------------------
	case 200:
		if (bNight3Av)
		{
			WatchAV();
		}
		if (TickInputRoute(DeltaSeconds))
		{
			UE_LOG(LogIndieGame, Display, TEXT("INPUT_ROUTE done seconds=%.1f"), PhaseSeconds);
			Finish();
		}
		return;

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
		{
			// 혼자 빠져나간 회차는 상자도 수거차와 같이 갔다. 403호 문 앞에 아무것도 없다.
			AIGLeftoverTarget* Box = Target(TEXT("Home.DeliveredBoxTake"));
			Check(!Box || !Box->IsInteractionEnabled(), TEXT("dawn_no_box_at_door"));
		}
		Next(70);
		return;

	case 62:
		if (PhaseSeconds < 6.0f)
		{
			return;
		}
		if (Step == 0)
		{
			Check(Narrative->GetLeftover().JeongwooState == FName(TEXT("LeftTogether")), TEXT("dawnbrace_jeongwoo_rescued"));
			Check(!Taken(IGLeftoverProbe::DohaBox), TEXT("dawnbrace_box_left_behind"));
			Check(!Director->IsSceneActive(), TEXT("dawnbrace_scene_closed"));
			// 403호 문 앞 복도에서 문 왼쪽 벽을 본다.
			Stand(FVector(60.0f, -330.0f, 900.0f), FVector(2.0f, -255.0f, 920.0f));
			Step = 1;
			PhaseSeconds = 6.0f;
			return;
		}
		if (PhaseSeconds < 6.6f)
		{
			return;
		}
		{
			AIGLeftoverTarget* Box = Target(TEXT("Home.DeliveredBoxTake"));
			Check(Box && Box->IsInteractionEnabled(), TEXT("day3_box_at_403_door"));
			Shoot(TEXT("day3_box_at_door"));
			Check(Box && Use(Box), TEXT("day3_box_taken"));
			Check(Taken(IGLeftoverProbe::DohaBox) && Taken(IGLeftoverProbe::ToolPouch), TEXT("day3_box_and_pouch"));
		}
		Next(70);
		return;

	case 400:
	{
		// 새 문장 진열. 큐를 비우고 한 줄만 띄운 뒤 찍는다(검사가 빨라 앞 줄이 밀려 있으면 새 줄이 안 보인다).
		struct FLineShot
		{
			TCHAR Kind;
			FText Speaker;
			FText Line;
			const TCHAR* Name;
		};
		static const FLineShot Lines[] = {
			{TEXT('D'), NSLOCTEXT("IGMissingFloor", "JeongwooSpeaker", "정우"),
				NSLOCTEXT("IGMissingFloor", "LeftoverLeaveLine", "문 잡고 있을게요. 더 볼 거 있으면 보고 나와요."), TEXT("LeftoverLeaveLine")},
			{TEXT('D'), NSLOCTEXT("IGMissingFloor", "JeongwooSpeaker", "정우"),
				NSLOCTEXT("IGMissingFloor", "LeftoverLeaveBoxLine", "문 잡고 있을게요. 상자 뺄 거면 말해요, 판 잡아 줄게요."), TEXT("LeftoverLeaveBoxLine")},
			{TEXT('D'), NSLOCTEXT("IGMissingFloor", "JeongwooSpeaker", "정우"),
				NSLOCTEXT("IGMissingFloor", "LeftoverBraceRelease", "판 놓을게요. 상자는 이따 다시 해요."), TEXT("LeftoverBraceRelease")},
			{TEXT('D'), NSLOCTEXT("IGMissingFloor", "JeongwooSpeaker", "정우"),
				NSLOCTEXT("IGMissingFloor", "LeftoverBraceAgain", "상자요? 잠깐만요, 판 잡을게요."), TEXT("LeftoverBraceAgain")},
			{TEXT('D'), NSLOCTEXT("IGMissingFloor", "JeongwooSpeaker", "정우"),
				NSLOCTEXT("IGMissingFloor", "LeftoverBraceReady", "잡았어요. 당겨요."), TEXT("LeftoverBraceReady")},
			{TEXT('D'), NSLOCTEXT("IGMissingFloor", "JeongwooSpeaker", "정우"),
				NSLOCTEXT("IGMissingFloor", "LeftoverGoAheadLine", "저 먼저 내려갈게요. 문은 열어 둘게요. 오래 있지 마요."), TEXT("LeftoverGoAheadLine")},
			{TEXT('P'), NSLOCTEXT("IGMissingFloor", "JeongwooSpeaker", "정우"),
				NSLOCTEXT("IGMissingFloor", "LeftoverJeongwooBoxAtDoor", "새벽엔 진짜 고마웠어요. 표시해 둔 상자는 아침에 빼서 403호 문 앞에 뒀어요. 그 방엔 이제 혼자 들어가지 마세요."), TEXT("LeftoverJeongwooBoxAtDoor")},
			{TEXT('D'), NSLOCTEXT("IGMissingFloor", "YudamCounterSpeaker", "백유담"),
				NSLOCTEXT("IGMissingFloor", "NarinReceiptQuestion", "그 마지막 택배요. 누가 언제 찾아갔는지 남아 있어요?"), TEXT("NarinReceiptQuestion")},
			{TEXT('D'), NSLOCTEXT("IGMissingFloor", "NarinSpeaker", "한나린"),
				NSLOCTEXT("IGMissingFloor", "NarinReceiptAnswer", "포스에 남아 있을 거예요. 잠깐만요, 찾아볼게요."), TEXT("NarinReceiptAnswer")},
			{TEXT('D'), NSLOCTEXT("IGMissingFloor", "NarinSpeaker", "한나린"),
				NSLOCTEXT("IGMissingFloor", "NarinReceiptFound", "작년 8월 2일이네요. 건물주 아저씨가 대신 받아 가셨어요. 한 장 뽑아 드릴게요."), TEXT("NarinReceiptFound")},
			{TEXT('D'), NSLOCTEXT("IGMissingFloor", "NarinSpeaker", "한나린"),
				NSLOCTEXT("IGMissingFloor", "NarinReceiptHere", "여기 있어요."), TEXT("NarinReceiptHere")},
			{TEXT('D'), NSLOCTEXT("IGMissingFloor", "NarinSpeaker", "한나린"),
				NSLOCTEXT("IGMissingFloor", "NarinLineLastDayReceipt", "그 영수증 잘 챙겨 둬요. 필요하면 저도 본 대로 말할게요. 무슨 일 있으면 바로 이쪽으로 오세요."), TEXT("NarinLineLastDayReceipt")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverFitWrongSlot", "잘린 면이 바깥으로 나와."), TEXT("LeftoverFitWrongSlot")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverFitFlipped", "고정쇠가 방수막에 미끄러져."), TEXT("LeftoverFitFlipped")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverFitRotated", "자리는 맞는데 구멍 위치가 어긋나."), TEXT("LeftoverFitRotated")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverCoverTakeThought", "이만한 덧판을 어디서 봤는데."), TEXT("LeftoverCoverTakeThought")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverFirstPickThought", "조각 셋에 틀 하나."), TEXT("LeftoverFirstPickThought")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverSackThought", "남은 조각 하나랑 금속 틀이야. 관리실에서 떼어 온 거랑 같은 막이 붙어 있어."), TEXT("LeftoverSackThought")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverSackAllPieces", "점검구 덮개 조각 셋이랑 금속 틀이야."), TEXT("LeftoverSackAllPieces")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverWedgePicked", "문 고이는 나무 쐐기야."), TEXT("LeftoverWedgePicked")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverWedgeSlip1", "장판에서 미끄러져 빠졌어."), TEXT("LeftoverWedgeSlip1")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverWedgeSlip2", "또 빠졌어. 장판 위라 걸릴 데가 없어."), TEXT("LeftoverWedgeSlip2")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverLedgerThought", "오빠 이름까지 적어 놓고 주인이 없대."), TEXT("LeftoverLedgerThought")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverMovedToLarge", "저리로 옮겨 갔어."), TEXT("LeftoverMovedToLarge")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverPieceBThought", "이것도 방수막째 잘린 조각이야."), TEXT("LeftoverPieceBThought")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverDeliveredBoxThought", "정우 씨가 표시해 둔 그 상자야. 안에 오빠 공구 주머니가 있어."), TEXT("LeftoverDeliveredBoxThought")},
			{TEXT('T'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "StoreReceiptThought", "오빠가 부탁했다고?"), TEXT("StoreReceiptThought")},
			{TEXT('C'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "CaptionLiningCreak", "[철골 마디가 버티며 삐걱이는 소리]"), TEXT("CaptionLiningCreak")},
			{TEXT('C'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "CaptionLiningPress", "[두꺼운 비닐이 벽에 쩍 들러붙는 소리]"), TEXT("CaptionLiningPress")},
			{TEXT('C'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "LeftoverPushedCaption", "[휜 철골이 빠지고, 벽을 긁으며 물러나는 소리]"), TEXT("LeftoverPushedCaption")},
			{TEXT('C'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "StoreTerminalCaption", "[계산대 단말기를 삑삑 누르는 소리]"), TEXT("StoreTerminalCaption")},
			{TEXT('C'), FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "StorePrinterCaption", "[영수증이 뽑혀 나오다 딸깍 잘리는 소리]"), TEXT("StorePrinterCaption")},
		};
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
		if (!Hud)
		{
			Check(false, TEXT("lines_hud"));
			Finish();
			return;
		}
		const int32 Count = UE_ARRAY_COUNT(Lines) + 1;
		if (LineIndex >= Count)
		{
			Check(true, TEXT("lines_shown"));
			Finish();
			return;
		}
		if (Step == 0)
		{
			if (LineIndex == 0)
			{
				// 4층 복도 남쪽 벽을 본다. 겨눌 것이 없어야 아래 줄의 안내가 가려지지 않는다.
				Stand(FVector(190.0f, -300.0f, 900.0f), FVector(190.0f, -372.0f, 1000.0f));
			}
			Hud->ClearDialogueForTesting();
			Hud->ClearChapterCardForTesting();
			if (LineIndex < UE_ARRAY_COUNT(Lines))
			{
				const FLineShot& Shot = Lines[LineIndex];
				if (Shot.Kind == TEXT('T'))
				{
					AIGHorrorHUD::PushThought(this, Shot.Line, 4.0f);
				}
				else if (Shot.Kind == TEXT('C'))
				{
					AIGHorrorHUD::PushAudioCaption(this, Shot.Line, 4.0f);
				}
				else
				{
					AIGHorrorHUD::PushDialogue(this, Shot.Speaker, Shot.Line,
						Shot.Kind == TEXT('P') ? EIGDialogueChannel::Device : EIGDialogueChannel::Conversation,
						4.0f, EIGDialoguePriority::Story);
				}
			}
			else
			{
				// 힌트 마지막 단계 앞의 안내(「풀이 보기 · 한 번 더 누르면 풀이가 나온다」).
				Hud->ShowHintAnswerTip();
			}
			Step = 1;
			StepSeconds = 0.0f;
			return;
		}
		StepSeconds += GetWorld()->GetDeltaSeconds();
		if (StepSeconds < 0.9f)
		{
			return;
		}
		Shoot(LineIndex < UE_ARRAY_COUNT(Lines) ? *FString::Printf(TEXT("line_%s"), Lines[LineIndex].Name) : TEXT("line_TipHintAnswer"));
		Step = 0;
		++LineIndex;
		return;
	}

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
		if (!bDawnVariant && !bDawnBraceVariant)
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

// -- 실제 입력 길 -------------------------------------------------------------------------
// 키와 마우스 축을 플레이어 컨트롤러의 입력으로 그대로 넣는다(사람이 누르는 것과 같은 길). 순간 이동과
// 상호작용 직접 호출은 하지 않는다. 마우스 한 칸이 몇 도인지는 처음에 한 번 재서 쓴다.

void AIGLeftoverProbe::SendKey(const FKey& Key, const bool bPressed)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}
	const FInputKeyEventArgs Event(nullptr, FInputDeviceId::CreateFromInternalId(0), Key,
		bPressed ? IE_Pressed : IE_Released, bPressed ? 1.0f : 0.0f, false, FPlatformTime::Cycles64());
	PC->InputKey(Event);
}

void AIGLeftoverProbe::SendMouse(const float X, const float Y)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}
	if (!FMath::IsNearlyZero(X))
	{
		PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::MouseX, X, 1.0f / 60.0f, 1, FPlatformTime::Cycles64()));
	}
	if (!FMath::IsNearlyZero(Y))
	{
		PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::MouseY, Y, 1.0f / 60.0f, 1, FPlatformTime::Cycles64()));
	}
}

AActor* AIGLeftoverProbe::FocusedActor() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	const UIGInteractionComponent* Interaction = Character ? Character->FindComponentByClass<UIGInteractionComponent>() : nullptr;
	return Interaction ? Interaction->GetFocusedActor() : nullptr;
}

FVector AIGLeftoverProbe::CameraLocation() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	const APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	return PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
}

FRotator AIGLeftoverProbe::CameraRotation() const
{
	const AIGPlayerCharacter* Character = Player.Get();
	const APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	return PC ? PC->GetControlRotation() : FRotator::ZeroRotator;
}

bool AIGLeftoverProbe::CalibrateMouse()
{
	// 마우스 X·Y를 한 번씩 4만큼 넣고 시점이 몇 도 도는지 잰다. 부호(반전 설정)까지 같이 잡힌다.
	switch (CalibrateStage)
	{
	case 0:
		CalibrateYaw = CameraRotation().Yaw;
		CalibratePitch = CameraRotation().Pitch;
		SendMouse(4.0f, 0.0f);
		CalibrateStage = 1;
		return false;
	case 1:
		MouseYawScale = FMath::FindDeltaAngleDegrees(CalibrateYaw, CameraRotation().Yaw) / 4.0f;
		CalibratePitch = CameraRotation().Pitch;
		SendMouse(0.0f, 4.0f);
		CalibrateStage = 2;
		return false;
	case 2:
		MousePitchScale = FMath::FindDeltaAngleDegrees(CalibratePitch, CameraRotation().Pitch) / 4.0f;
		CalibrateStage = 3;
		UE_LOG(LogIndieGame, Display, TEXT("INPUT_MOUSE yaw_per_unit=%.4f pitch_per_unit=%.4f"), MouseYawScale, MousePitchScale);
		Check(FMath::Abs(MouseYawScale) > 0.001f && FMath::Abs(MousePitchScale) > 0.001f, TEXT("input_mouse_turns_view"));
		return true;
	default:
		return true;
	}
}

bool AIGLeftoverProbe::InputAim(const FVector& Point, const float DeltaSeconds)
{
	const FRotator Want = (Point - CameraLocation()).Rotation();
	const FRotator Now = CameraRotation();
	const float YawErr = FMath::FindDeltaAngleDegrees(Now.Yaw, Want.Yaw);
	const float PitchErr = FMath::FindDeltaAngleDegrees(Now.Pitch, Want.Pitch);
	if (FMath::Abs(YawErr) < 1.2f && FMath::Abs(PitchErr) < 1.2f)
	{
		return true;
	}
	// 한 틱에 오차의 절반만 돌린다(사람 손처럼 넘치지 않게). 마우스 단위로 바꿔 넣는다.
	const float X = FMath::IsNearlyZero(MouseYawScale) ? 0.0f : FMath::Clamp(YawErr * 0.5f / MouseYawScale, -40.0f, 40.0f);
	const float Y = FMath::IsNearlyZero(MousePitchScale) ? 0.0f : FMath::Clamp(PitchErr * 0.5f / MousePitchScale, -40.0f, 40.0f);
	SendMouse(X, Y);
	return false;
}

bool AIGLeftoverProbe::InputWalk(const FVector& Goal, const float Tolerance, const float DeltaSeconds)
{
	AIGPlayerCharacter* Character = Player.Get();
	if (!Character)
	{
		return true;
	}
	FVector To = Goal - Character->GetActorLocation();
	To.Z = 0.0f;
	if (To.Size() < Tolerance)
	{
		if (bForwardDown)
		{
			SendKey(EKeys::W, false);
			bForwardDown = false;
		}
		StallSeconds = 0.0f;
		return true;
	}
	const FRotator Now = CameraRotation();
	const float YawErr = FMath::FindDeltaAngleDegrees(Now.Yaw, To.Rotation().Yaw);
	// 걸으면서 앞을 본다(약간 아래). 방향만 마우스로 맞춘다.
	const float PitchErr = FMath::FindDeltaAngleDegrees(Now.Pitch, -8.0f);
	SendMouse(FMath::IsNearlyZero(MouseYawScale) ? 0.0f : FMath::Clamp(YawErr * 0.5f / MouseYawScale, -40.0f, 40.0f),
		FMath::IsNearlyZero(MousePitchScale) ? 0.0f : FMath::Clamp(PitchErr * 0.3f / MousePitchScale, -20.0f, 20.0f));
	const bool bFacing = FMath::Abs(YawErr) < 25.0f;
	if (bFacing != bForwardDown)
	{
		SendKey(EKeys::W, bFacing);
		bForwardDown = bFacing;
	}
	// 앞으로 누르는데 몸이 안 나가면 막힌 것이다. 앞에 문이 잡혀 있으면 연다.
	if (bForwardDown && Character->GetVelocity().Size2D() < 5.0f)
	{
		StallSeconds += DeltaSeconds;
		if (StallSeconds > 1.0f && Cast<AIGSwingDoor>(FocusedActor()))
		{
			SendKey(EKeys::E, true);
			SendKey(EKeys::E, false);
			StallSeconds = 0.0f;
		}
		else if (StallSeconds > 1.0f && !bBlockLogged)
		{
			bBlockLogged = true;
			const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
			const FVector Start = Character->GetActorLocation();
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(LeftoverInputBlock), false, Character);
			if (Capsule && GetWorld()->SweepSingleByChannel(Hit, Start, Start + To.GetSafeNormal() * 30.0f, FQuat::Identity,
					Capsule->GetCollisionObjectType(),
					FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params))
			{
				UE_LOG(LogIndieGame, Display, TEXT("INPUT_BLOCK at=%s actor=%s comp=%s impact=%s normal=%s"),
					*Start.ToCompactString(), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("none"),
					Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("none"),
					*Hit.ImpactPoint.ToCompactString(), *Hit.ImpactNormal.ToCompactString());
				// 무엇을 밟고 서 있는지도 적는다. 발밑이 들리면 머리가 문틀에 걸린다.
				FHitResult Floor;
				if (GetWorld()->SweepSingleByChannel(Floor, Start, Start - FVector(0.0f, 0.0f, 200.0f), FQuat::Identity,
						Capsule->GetCollisionObjectType(),
						FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params))
				{
					UE_LOG(LogIndieGame, Display, TEXT("INPUT_FLOOR actor=%s comp=%s impact=%s"),
						Floor.GetActor() ? *Floor.GetActor()->GetName() : TEXT("none"),
						Floor.GetComponent() ? *Floor.GetComponent()->GetName() : TEXT("none"), *Floor.ImpactPoint.ToCompactString());
				}
				// 덧댄 것의 다리에 막혔으면 다리 캡슐이 어디를 가로지르는지 다 적는다(바닥 높이에서 길을 고를 때 쓴다).
				if (const AIGLiningEntity* Body = Cast<AIGLiningEntity>(Hit.GetActor()))
				{
					TInlineComponentArray<UCapsuleComponent*> Legs(Body);
					for (const UCapsuleComponent* Leg : Legs)
					{
						const FVector Axis = Leg->GetUpVector() * Leg->GetScaledCapsuleHalfHeight();
						UE_LOG(LogIndieGame, Display, TEXT("INPUT_LINING_LEG %s a=%s b=%s r=%.0f"), *Leg->GetName(),
							*(Leg->GetComponentLocation() - Axis).ToCompactString(), *(Leg->GetComponentLocation() + Axis).ToCompactString(),
							Leg->GetScaledCapsuleRadius());
					}
					UE_LOG(LogIndieGame, Display, TEXT("INPUT_LINING_BODY center=%s"), *Body->GetBodyCenter().ToCompactString());
				}
			}
			else
			{
				UE_LOG(LogIndieGame, Display, TEXT("INPUT_BLOCK at=%s nothing_ahead"), *Start.ToCompactString());
			}
		}
	}
	else
	{
		StallSeconds = 0.0f;
		bBlockLogged = false;
	}
	return false;
}

bool AIGLeftoverProbe::TickInputRoute(const float DeltaSeconds)
{
	if (!CalibrateMouse())
	{
		return false;
	}
	if (!InputSteps.IsValidIndex(InputIndex))
	{
		if (!InputSegment.IsEmpty())
		{
			UE_LOG(LogIndieGame, Display, TEXT("INPUT_SEGMENT %s seconds=%.1f"), *InputSegment, PhaseSeconds - InputSegmentStart);
			InputSegment.Reset();
		}
		return true;
	}
	FInputStep& Step_ = InputSteps[InputIndex];
	if (InputStepSeconds == 0.0f && Step_.Segment)
	{
		if (!InputSegment.IsEmpty())
		{
			UE_LOG(LogIndieGame, Display, TEXT("INPUT_SEGMENT %s seconds=%.1f"), *InputSegment, PhaseSeconds - InputSegmentStart);
		}
		InputSegment = Step_.Segment;
		InputSegmentStart = PhaseSeconds;
	}
	InputStepSeconds += DeltaSeconds;
	bool bDone = false;
	switch (Step_.Kind)
	{
	case EInputStep::Walk:
		bDone = InputWalk(Step_.Point, Step_.Value, DeltaSeconds);
		if (!bDone && InputStepSeconds > 25.0f)
		{
			Check(false, Step_.Name);
			UE_LOG(LogIndieGame, Display, TEXT("INPUT_STUCK %s at=%s goal=%s"), Step_.Name,
				Player.IsValid() ? *Player->GetActorLocation().ToCompactString() : TEXT("none"), *Step_.Point.ToCompactString());
			Shoot(TEXT("99_input_stuck"));
			if (Player.IsValid())
			{
				for (TActorIterator<AIGSwingDoor> It(GetWorld()); It; ++It)
				{
					if (FVector::Dist(It->GetActorLocation(), Player->GetActorLocation()) < 250.0f)
					{
						UE_LOG(LogIndieGame, Display, TEXT("INPUT_DOOR %s at=%s open=%d closed=%d swinging=%d"), *It->GetName(),
							*It->GetActorLocation().ToCompactString(), It->IsOpen() ? 1 : 0, It->IsFullyClosed() ? 1 : 0,
							It->IsSwinging() ? 1 : 0);
					}
				}
			}
			if (bForwardDown)
			{
				SendKey(EKeys::W, false);
				bForwardDown = false;
			}
			return true;
		}
		break;
	case EInputStep::Aim:
		bDone = InputAim(Step_.Point, DeltaSeconds);
		if (!bDone && InputStepSeconds > 6.0f)
		{
			Check(false, Step_.Name);
			bDone = true;
		}
		break;
	case EInputStep::Press:
		if (!bInputKeyDown)
		{
			// 초점은 10~15 Hz 타이머로 고쳐진다. 누르는 순간 게임이 하듯 한 번 다시 잡고 본다.
			if (AIGPlayerCharacter* Character = Player.Get())
			{
				if (UIGInteractionComponent* Interaction = Character->FindComponentByClass<UIGInteractionComponent>())
				{
					Interaction->RefreshFocus();
				}
			}
			AActor* Focus = FocusedActor();
			if (Step_.Expect.IsValid())
			{
				const bool bOnTarget = Focus == Step_.Expect.Get();
				Check(bOnTarget, *FString::Printf(TEXT("%s_focus"), Step_.Name));
				if (!bOnTarget)
				{
					// 시선이 처음 맞은 것. 보이지 않는 충돌이 표적을 가리는지 여기서 바로 보인다.
					FString FirstHit(TEXT("none"));
					if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
					{
						FHitResult Hit;
						FCollisionQueryParams Params(SCENE_QUERY_STAT(IGLeftoverFocusProbe), false, Controller->GetPawn());
						const FVector Start = CameraLocation();
						const FVector End = Start + Controller->GetControlRotation().Vector() * 220.0f;
						if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
						{
							FirstHit = FString::Printf(TEXT("%s/%s@%s"), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("?"),
								Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("?"), *Hit.ImpactPoint.ToCompactString());
						}
					}
					UE_LOG(LogIndieGame, Display, TEXT("INPUT_FOCUS %s want=%s got=%s cam=%s first_hit=%s target_enabled=%d"), Step_.Name,
						*Step_.Expect->GetName(), Focus ? *Focus->GetName() : TEXT("none"), *CameraLocation().ToCompactString(), *FirstHit,
						Step_.Expect->GetClass()->ImplementsInterface(UIGInteractable::StaticClass())
							? (IIGInteractable::Execute_CanInteract(Step_.Expect.Get(), Player.Get()) ? 1 : 0) : -1);
				}
			}
			SendKey(Step_.Key, true);
			bInputKeyDown = true;
			InputStepSeconds = 0.0f;
		}
		else if (InputStepSeconds >= Step_.Value)
		{
			SendKey(Step_.Key, false);
			bInputKeyDown = false;
			bDone = true;
		}
		break;
	case EInputStep::Wait:
		bDone = InputStepSeconds >= Step_.Value;
		break;
	case EInputStep::Until:
		bDone = Step_.Done && Step_.Done();
		if (bDone)
		{
			Check(true, Step_.Name);
		}
		else if (InputStepSeconds > Step_.Value)
		{
			Check(false, Step_.Name);
			bDone = true;
		}
		break;
	}
	if (bDone)
	{
		// 막혔을 때 어디서부터 꼬였는지 보려고 단계마다 선 자리를 남긴다.
		UE_LOG(LogIndieGame, Display, TEXT("INPUT_STEP %s at=%s"), Step_.Name,
			Player.IsValid() ? *Player->GetActorLocation().ToCompactString() : TEXT("none"));
		++InputIndex;
		InputStepSeconds = 0.0f;
	}
	return false;
}

void AIGLeftoverProbe::AvEvent(const FString& What)
{
	const UWorld* World = GetWorld();
	UE_LOG(LogIndieGame, Display, TEXT("N3AV t=%.3f %s"), World ? World->GetTimeSeconds() : 0.0, *What);
}

void AIGLeftoverProbe::WatchAV()
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	if (Hud)
	{
		const FString Line = Hud->GetActiveDialogueLineForTesting().ToString();
		if (Line != LastAvDialogue)
		{
			AvEvent(Line.IsEmpty() ? FString(TEXT("dialogue_end"))
				: FString::Printf(TEXT("dialogue speaker=\"%s\" line=\"%s\""), *Hud->GetActiveDialogueSpeakerForTesting().ToString(), *Line));
			LastAvDialogue = Line;
		}
		const FString Caption = Hud->GetActiveAudioCaptionForTesting().ToString();
		if (Caption != LastAvCaption)
		{
			AvEvent(Caption.IsEmpty() ? FString(TEXT("caption_end")) : FString::Printf(TEXT("caption \"%s\""), *Caption));
			LastAvCaption = Caption;
		}
	}
	bool bCapture = false;
	if (World)
	{
		for (TActorIterator<AIGNightLoopDirector> It(const_cast<UWorld*>(World)); It; ++It)
		{
			bCapture |= It->IsCaptureResetInFlight();
		}
	}
	if (bCapture != bLastAvCapture)
	{
		bLastAvCapture = bCapture;
		if (bCapture)
		{
			++AvCaptures;
		}
		AvEvent(FString::Printf(TEXT("%s count=%d segment=%s"), bCapture ? TEXT("capture_start") : TEXT("capture_end"),
			AvCaptures, *InputSegment));
	}
	if (World && Player.IsValid() && World->GetTimeSeconds() - LastAvPositionAt >= 0.5)
	{
		LastAvPositionAt = World->GetTimeSeconds();
		const FVector At = Player->GetActorLocation();
		const FRotator View = PC ? PC->GetControlRotation() : FRotator::ZeroRotator;
		AvEvent(FString::Printf(TEXT("player at=%.1f,%.1f,%.1f yaw=%.1f pitch=%.1f segment=%s"), At.X, At.Y, At.Z, View.Yaw,
			View.Pitch, *InputSegment));
	}
}

void AIGLeftoverProbe::BuildNightThreeContinuation()
{
	// 302호에서 나와 계단으로 4층, 옥상 계단, 옥상, 5층. 좌표는 관리인 열쇠 찾기 길(IGManagerPatrol)과
	// 밤3 감독(IGMissingFloorNightThreeDirector)의 것을 그대로 쓴다. 문은 실제로 E로 연다.
	const auto Walk = [this](const FVector& Goal, const float Tolerance, const TCHAR* Name, const TCHAR* Segment = nullptr)
	{
		FInputStep S; S.Kind = EInputStep::Walk; S.Point = Goal; S.Value = Tolerance; S.Name = Name; S.Segment = Segment;
		InputSteps.Add(MoveTemp(S));
	};
	const auto AimPoint = [this](const FVector& Point, const TCHAR* Name)
	{
		FInputStep S; S.Kind = EInputStep::Aim; S.Point = Point; S.Name = Name;
		InputSteps.Add(MoveTemp(S));
	};
	const auto Press = [this](AActor* Expect, const float Seconds, const TCHAR* Name, const FKey& Key = EKeys::E)
	{
		FInputStep S; S.Kind = EInputStep::Press; S.Value = Seconds; S.Name = Name; S.Expect = Expect; S.Key = Key;
		InputSteps.Add(MoveTemp(S));
	};
	const auto Wait = [this](const float Seconds)
	{
		FInputStep S; S.Kind = EInputStep::Wait; S.Value = Seconds; S.Name = TEXT("wait");
		InputSteps.Add(MoveTemp(S));
	};
	const auto Until = [this](TFunction<bool()> Done, const float Limit, const TCHAR* Name)
	{
		FInputStep S; S.Kind = EInputStep::Until; S.Value = Limit; S.Name = Name; S.Done = MoveTemp(Done);
		InputSteps.Add(MoveTemp(S));
	};
	AIGMissingFloorNightThreeDirector* Three = nullptr;
	for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
	{
		Three = *It;
	}
	if (!Three)
	{
		Check(false, TEXT("n3av_night_three_director"));
		return;
	}
	TWeakObjectPtr<AIGMissingFloorNightThreeDirector> WeakThree(Three);
	const float ThirdZ = AIGPrologueWorldScene::GetStoreyFloorZ(2);
	const float FourthZ = AIGPrologueWorldScene::GetStoreyFloorZ(3);
	constexpr float RoofZ = AIGPrologueWorldScene::RoofZ;

	// 3층 복도에서 서쪽 계단탑으로, 디딤판 점을 밟아 4층으로.
	Walk(FVector(-150.0f, -305.0f, ThirdZ), 30.0f, TEXT("n3av_third_corridor_west"), TEXT("to_roof"));
	Walk(AIGPrologueWorldScene::GetStairDoorwayFeet(2), 28.0f, TEXT("n3av_third_stair_door"));
	TArray<FVector> Climb;
	AIGPrologueWorldScene::GetStairClimbFeet(2, Climb);
	for (int32 Index = 1; Index + 1 < Climb.Num(); ++Index)
	{
		Walk(Climb[Index], 26.0f, TEXT("n3av_climb"));
	}
	Walk(AIGPrologueWorldScene::GetStairDoorwayFeet(3), 28.0f, TEXT("n3av_fourth_stair_door"));
	// 4층 서쪽 끝 옥상 계단.
	Walk(FVector(-262.0f, -292.0f, FourthZ), 25.0f, TEXT("n3av_roof_stair_foot"));
	Walk(FVector(-277.5f, -238.0f, FourthZ), 22.0f, TEXT("n3av_roof_lower_flight"));
	Walk(FVector(-277.5f, -150.0f, FourthZ + 72.0f), 22.0f, TEXT("n3av_roof_half_landing"));
	Walk(FVector(-277.5f, -125.0f, FourthZ + 72.0f), 22.0f, TEXT("n3av_roof_half_landing_end"));
	Walk(FVector(-277.5f, -100.0f, 980.0f), 22.0f, TEXT("n3av_roof_upper_flight"));
	Walk(FVector(-277.5f, 110.0f, 1142.3f), 22.0f, TEXT("n3av_below_roof_gate"));
	if (AIGSwingDoor* Gate = Three->GetStairGate())
	{
		AimPoint(Gate->GetLatchSoundLocation(), TEXT("n3av_aim_roof_gate"));
		Press(Gate, 0.3f, TEXT("n3av_open_roof_gate"));
		TWeakObjectPtr<AIGSwingDoor> WeakGate(Gate);
		Until([WeakGate]() { return WeakGate.IsValid() && WeakGate->IsOpen() && !WeakGate->IsSwinging(); }, 6.0f, TEXT("n3av_roof_gate_open"));
	}
	Walk(FVector(-277.5f, 208.5f, RoofZ), 22.0f, TEXT("n3av_roof_top_tread"));
	Walk(FVector(-277.5f, 245.0f, RoofZ), 22.0f, TEXT("n3av_roof_outside"), TEXT("roof"));
	// 문틀 동쪽 기둥 끝(-222.5, 226)을 30 cm 넘게 비켜 북쪽으로 돌아 통로에 든다.
	Walk(FVector(-255.0f, 262.0f, RoofZ), 22.0f, TEXT("n3av_roof_step_north"));
	Walk(FVector(-180.0f, 262.0f, RoofZ), 22.0f, TEXT("n3av_passage_west"));
	Walk(FVector(-150.0f, 240.0f, RoofZ), 25.0f, TEXT("n3av_passage_mouth"));
	Walk(FVector(130.0f, 220.0f, RoofZ), 25.0f, TEXT("n3av_passage_east"));
	Walk(FVector(130.0f, 390.0f, RoofZ), 22.0f, TEXT("n3av_annex_front"));
	if (AIGSwingDoor* Gate = Three->GetAnnexGate())
	{
		AimPoint(Gate->GetLatchSoundLocation(), TEXT("n3av_aim_annex_gate"));
		Press(Gate, 0.3f, TEXT("n3av_open_annex_gate"));
		TWeakObjectPtr<AIGSwingDoor> WeakGate(Gate);
		Until([WeakGate]() { return WeakGate.IsValid() && WeakGate->IsOpen() && !WeakGate->IsSwinging(); }, 6.0f, TEXT("n3av_annex_gate_open"));
	}
	// 5층. 밸브를 열고 두 칸에 귀를 대 견준다.
	Walk(FVector(130.0f, 520.0f, RoofZ), 25.0f, TEXT("n3av_annex_inside"), TEXT("annex"));
	Walk(FVector(185.0f, 631.0f, RoofZ), 20.0f, TEXT("n3av_walk_to_valve"));
	if (AIGMissingFloorEvidence* Valve = Three->GetRiserValve())
	{
		AimPoint(Valve->GetActorLocation(), TEXT("n3av_aim_valve"));
		Until([this]() { Shoot(TEXT("n3av_valve_in_gap")); return true; }, 0.1f, TEXT("av_shot_valve"));
		Wait(0.4f);
		Press(Valve, 1.2f, TEXT("n3av_open_valve"));
	}
	Until([WeakThree]() { return WeakThree.IsValid() && WeakThree->IsValveOpen(); }, 3.0f, TEXT("n3av_valve_open"));
	Wait(2.5f);
	Walk(FVector(200.0f, 534.0f, RoofZ), 18.0f, TEXT("n3av_walk_to_bay0"));
	AimPoint(FVector(247.0f, 534.0f, 1290.0f), TEXT("n3av_aim_listen_bay0"));
	Press(Three->GetWallListen(0), 1.1f, TEXT("n3av_listen_bay0"));
	Wait(3.5f);
	Walk(FVector(200.0f, 674.0f, RoofZ), 18.0f, TEXT("n3av_walk_to_bay1"));
	AimPoint(FVector(247.0f, 674.0f, 1290.0f), TEXT("n3av_aim_listen_bay1"));
	Press(Three->GetWallListen(1), 1.1f, TEXT("n3av_listen_bay1"));
	Wait(4.0f);
	// 수첩을 읽는다(펴고, 읽고, 덮는다). 덮은 뒤에 벽의 정체가 선다.
	Walk(FVector(-90.0f, 715.0f, RoofZ), 18.0f, TEXT("n3av_walk_to_notebook"), TEXT("notebook"));
	if (AIGReadableNote* Notebook = Three->GetTunerNotebook())
	{
		AimPoint(Notebook->GetActorLocation(), TEXT("n3av_aim_notebook"));
		Press(Notebook, 0.2f, TEXT("n3av_open_notebook"));
		Wait(6.0f);
		Press(nullptr, 0.15f, TEXT("n3av_close_notebook"));
	}
	Until([this]() { return GetNarrative() && GetNarrative()->HasTruth(EIGMissingFloorTruth::SomeoneInTheWall); }, 10.0f,
		TEXT("n3av_wall_identified"));
	Wait(5.0f);
	// 대답하는 벽 앞에서 박자대로 Q를 세 번(둘, 쉬고, 하나). 첫째→둘째 0.4초, 둘째→셋째 0.8초.
	Walk(FVector(200.0f, 700.0f, RoofZ), 18.0f, TEXT("n3av_walk_to_answer_wall"), TEXT("answer"));
	AimPoint(FVector(247.0f, 700.0f, 1266.0f), TEXT("n3av_aim_answer_wall"));
	Until([WeakThree]() { return WeakThree.IsValid() && WeakThree->GetAnswerTarget() && WeakThree->GetAnswerTarget()->IsInteractionEnabled(); },
		8.0f, TEXT("n3av_answer_wall_ready"));
	// 준비(상태): 재워 두었던 위층 사람을 대답 직전에 깨운다. 게임에서 그는 이 밤 내내 깨어 있다. 대답과
	// 같은 프레임에 밤3 감독이 그를 4층 복도 벽 앞에 세우고 붙든다(깨우기가 붙들림을 지우므로 먼저 깨운다).
	Until([this]()
	{
		for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
		{
			It->SetDormant(false);
		}
		AvEvent(TEXT("setup listener_awake=1"));
		return true;
	}, 1.0f, TEXT("n3av_wake_listener"));
	Press(Three->GetAnswerTarget(), 0.08f, TEXT("n3av_rhythm_tap1"), EKeys::Q);
	Wait(0.32f);
	Press(Three->GetAnswerTarget(), 0.08f, TEXT("n3av_rhythm_tap2"), EKeys::Q);
	Wait(0.72f);
	Press(Three->GetAnswerTarget(), 0.08f, TEXT("n3av_rhythm_tap3"), EKeys::Q);
	Until([this]() { return GetNarrative() && GetNarrative()->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer); }, 20.0f,
		TEXT("n3av_wall_answers"));
	Wait(9.0f);
	// 귀환. 옥상을 지나 4층으로 내려온다.
	Walk(FVector(130.0f, 390.0f, RoofZ), 22.0f, TEXT("n3av_back_annex_front"), TEXT("return"));
	Walk(FVector(130.0f, 220.0f, RoofZ), 25.0f, TEXT("n3av_back_passage_east"));
	Walk(FVector(-150.0f, 240.0f, RoofZ), 25.0f, TEXT("n3av_back_passage_mouth"));
	Walk(FVector(-180.0f, 262.0f, RoofZ), 22.0f, TEXT("n3av_back_passage_west"));
	Walk(FVector(-255.0f, 262.0f, RoofZ), 22.0f, TEXT("n3av_back_roof_step_north"));
	Walk(FVector(-277.5f, 245.0f, RoofZ), 22.0f, TEXT("n3av_back_roof_outside"));
	Walk(FVector(-277.5f, 208.5f, RoofZ), 22.0f, TEXT("n3av_back_top_tread"));
	Walk(FVector(-277.5f, 110.0f, 1142.3f), 22.0f, TEXT("n3av_back_below_gate"));
	Walk(FVector(-277.5f, -100.0f, 980.0f), 22.0f, TEXT("n3av_back_upper_flight"));
	Walk(FVector(-277.5f, -125.0f, FourthZ + 72.0f), 22.0f, TEXT("n3av_back_half_landing_end"));
	Walk(FVector(-277.5f, -150.0f, FourthZ + 72.0f), 22.0f, TEXT("n3av_back_half_landing"));
	Walk(FVector(-277.5f, -238.0f, FourthZ), 22.0f, TEXT("n3av_back_lower_flight"));
	Walk(FVector(-262.0f, -292.0f, FourthZ), 25.0f, TEXT("n3av_back_roof_stair_foot"));
	// 복도의 그(남쪽 벽에 엎드려 옆머리를 대고 서쪽을 본다)를 계단 밑에서 보자마자 박자로 대답해 세운 뒤,
	// 북쪽으로 어깨를 스치며 옆을 지나 403호로 간다. 그를 보면 붙들림이 풀려 걸음 소리를 듣기 시작한다.
	AimPoint(FVector(-135.0f, -345.0f, FourthZ + 30.0f), TEXT("n3av_look_at_figure"));
	Until([this]() { Shoot(TEXT("n3av_figure_at_wall")); return true; }, 0.1f, TEXT("av_shot_figure"));
	Press(nullptr, 0.08f, TEXT("n3av_answer_tap1"), EKeys::Q);
	Wait(0.32f);
	Press(nullptr, 0.08f, TEXT("n3av_answer_tap2"), EKeys::Q);
	Wait(0.72f);
	Press(nullptr, 0.08f, TEXT("n3av_answer_tap3"), EKeys::Q);
	Until([this]()
	{
		for (TActorIterator<AIGListenerEntity> It(GetWorld()); It; ++It)
		{
			if (It->GetListenerState() == EIGListenerState::Waiting)
			{
				return true;
			}
		}
		return false;
	}, 6.0f, TEXT("n3av_figure_waits"));
	Walk(FVector(-200.0f, -270.0f, FourthZ), 22.0f, TEXT("n3av_step_into_corridor"), TEXT("pass"));
	Walk(FVector(-150.0f, -268.0f, FourthZ), 20.0f, TEXT("n3av_pass_north_side"));
	Walk(FVector(-110.0f, -268.0f, FourthZ), 20.0f, TEXT("n3av_pass_shoulder"));
	Until([this]() { Shoot(TEXT("n3av_passing_beside")); return true; }, 0.1f, TEXT("av_shot_pass"));
	Walk(FVector(-40.0f, -268.0f, FourthZ), 20.0f, TEXT("n3av_pass_beside"));
	// 지나간 뒤 돌아서서 발 쪽에서 다시 본다. 벽 관통은 정면보다 뒤와 옆에서 드러난다.
	AimPoint(FVector(-110.0f, -345.0f, FourthZ + 25.0f), TEXT("n3av_look_back_at_figure"));
	Until([this]() { Shoot(TEXT("n3av_figure_from_behind")); return true; }, 0.1f, TEXT("av_shot_behind"));
	Walk(FVector(150.0f, -300.0f, FourthZ), 25.0f, TEXT("n3av_passed_east"));
	Until([this]() { return GetNarrative() && GetNarrative()->HasBeatPlayed(FName(TEXT("Night3.PassBy"))); }, 3.0f,
		TEXT("n3av_passed_while_waiting"));
	Walk(FVector(AIGPrologueWorldScene::HomeDoorX, -290.0f, FourthZ), 22.0f, TEXT("n3av_home_door"), TEXT("home"));
	Walk(FVector(AIGPrologueWorldScene::HomeDoorX, -160.0f, FourthZ), 25.0f, TEXT("n3av_inside_403"));
	Wait(4.0f);
	Until([this]() { return GetNarrative() && GetNarrative()->HasBeatPlayed(AIGNightPhaseDirector::GoalBeatId(3)); }, 6.0f,
		TEXT("n3av_night_goal"));
	// 조심해서 지난 길이다. 계단 밑이나 집 안에서 잡혔으면 귀환길 연출이 무너진 것이다.
	Wait(3.0f);
	Until([this]() { return AvCaptures == 0 && !bLastAvCapture; }, 0.1f, TEXT("n3av_no_capture"));
}

void AIGLeftoverProbe::BuildStorageInputRoute()
{
	AIGMissingFloorLeftoverDirector* Director = Leftover.Get();
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Director || !Narrative)
	{
		return;
	}
	InputSteps.Reset();
	InputIndex = 0;
	InputStepSeconds = 0.0f;
	const auto Walk = [this](const FVector& Goal, const float Tolerance, const TCHAR* Name, const TCHAR* Segment = nullptr)
	{
		FInputStep S; S.Kind = EInputStep::Walk; S.Point = Goal; S.Value = Tolerance; S.Name = Name; S.Segment = Segment;
		InputSteps.Add(MoveTemp(S));
	};
	const auto AimAt = [this](AActor* Actor, const FVector& Offset, const TCHAR* Name)
	{
		FInputStep S; S.Kind = EInputStep::Aim; S.Point = (Actor ? Actor->GetActorLocation() : FVector::ZeroVector) + Offset; S.Name = Name;
		InputSteps.Add(MoveTemp(S));
	};
	const auto AimPoint = [this](const FVector& Point, const TCHAR* Name)
	{
		FInputStep S; S.Kind = EInputStep::Aim; S.Point = Point; S.Name = Name;
		InputSteps.Add(MoveTemp(S));
	};
	const auto Press = [this](AActor* Expect, const float Seconds, const TCHAR* Name)
	{
		FInputStep S; S.Kind = EInputStep::Press; S.Value = Seconds; S.Name = Name; S.Expect = Expect;
		InputSteps.Add(MoveTemp(S));
	};
	const auto Wait = [this](const float Seconds)
	{
		FInputStep S; S.Kind = EInputStep::Wait; S.Value = Seconds; S.Name = TEXT("wait");
		InputSteps.Add(MoveTemp(S));
	};
	const auto Until = [this](TFunction<bool()> Done, const float Limit, const TCHAR* Name)
	{
		FInputStep S; S.Kind = EInputStep::Until; S.Value = Limit; S.Name = Name; S.Done = MoveTemp(Done);
		InputSteps.Add(MoveTemp(S));
	};
	TWeakObjectPtr<AIGMissingFloorLeftoverDirector> WeakDirector(Director);
	TWeakObjectPtr<UIGMissingFloorNarrativeSubsystem> WeakNarrative(Narrative);
	const auto Live = [this](const TCHAR* Key)
	{
		return [this, Key]() { AIGLeftoverTarget* T = Target(Key); return T && T->IsInteractionEnabled(); };
	};

	// 복도에서 302호로 걸어 들어간다.
	Walk(FVector(-30.0f, -236.0f, 690.0f), 22.0f, TEXT("input_walk_to_302_door"), TEXT("enter_302"));
	Walk(FVector(-32.0f, -125.0f, 690.0f), 25.0f, TEXT("input_walk_into_302"));
	if (bRecordAV)
	{
		AimPoint(FVector(60.0f, 40.0f, 700.0f), TEXT("av_look_board"));
		Until([this]() { Shoot(TEXT("n3av_302_board_and_lining")); return true; }, 0.1f, TEXT("av_shot_302"));
		Wait(0.4f);
	}
	// 문 옆 쐐기를 줍는다.
	AimAt(Target(TEXT("Storage.WedgePick")), FVector::ZeroVector, TEXT("input_aim_wedge"));
	Press(Target(TEXT("Storage.WedgePick")), 0.12f, TEXT("input_pick_wedge"));
	Until(Live(TEXT("Storage.Socket0")), 3.0f, TEXT("input_wedge_in_hand"));
	// 판 밑동이 바닥 틀에 걸리는 자리에 끼운다.
	// 덧댄 것의 동쪽 위 다리가 판 밑동 앞으로 무릎을 내밀고 있어, 그 다리 서쪽에 서서 밑동을 겨눈다.
	Walk(FVector(-20.0f, 5.0f, 690.0f), 18.0f, TEXT("input_walk_to_board_base"), TEXT("wedge"));
	AimAt(Target(TEXT("Storage.Socket0")), FVector::ZeroVector, TEXT("input_aim_socket0"));
	Press(Target(TEXT("Storage.Socket0")), 1.15f, TEXT("input_place_wedge"));
	Until([WeakNarrative]() { return WeakNarrative.IsValid() && WeakNarrative->GetLeftover().WedgeSocket == FName(TEXT("Brace")); },
		3.0f, TEXT("input_wedge_braces"));
	// 장롱을 민다. 덧댄 것이 북쪽 작업 틈을 누르며 다리를 방 가운데로 뻗고 있어 남쪽으로 돌아간다.
	Walk(FVector(-15.0f, -70.0f, 690.0f), 20.0f, TEXT("input_walk_around_legs"), TEXT("wardrobe"));
	Walk(FVector(-80.0f, -45.0f, 690.0f), 18.0f, TEXT("input_walk_to_wardrobe"));
	if (bRecordAV)
	{
		// 장롱 앞면 가운데는 숨는 자리, 북쪽 모서리는 미는 손잡이다.
		AimPoint(FVector(-114.0f, -10.0f, 700.0f), TEXT("av_look_wardrobe_center"));
		// 조사 안내는 겨누고 0.18초 뒤에 맺힌다(§18.4). 다 맺힌 뒤에 찍는다.
		Wait(0.3f);
		Until([this]() { Shoot(TEXT("n3av_wardrobe_hide_prompt")); return true; }, 0.1f, TEXT("av_shot_wardrobe"));
		Wait(0.4f);
	}
	AimAt(Target(TEXT("Storage.Wardrobe")), FVector::ZeroVector, TEXT("input_aim_wardrobe"));
	if (bRecordAV)
	{
		Wait(0.3f);
		Until([this]() { Shoot(TEXT("n3av_wardrobe_push_prompt")); return true; }, 0.1f, TEXT("av_shot_wardrobe_push"));
		Wait(0.4f);
	}
	Press(Target(TEXT("Storage.Wardrobe")), 1.9f, TEXT("input_push_wardrobe"));
	Until([WeakNarrative]() { return WeakNarrative.IsValid() && WeakNarrative->GetLeftover().bWardrobeMoved; }, 3.0f, TEXT("input_wardrobe_moved"));
	Wait(2.5f);
	// 장롱 뒤 큰 점검구를 연다.
	AIGAccessHatch* Large = Director->GetHatch(FName(TEXT("Storage.Large")));
	Walk(FVector(-80.0f, -55.0f, 690.0f), 18.0f, TEXT("input_walk_to_hatch"), TEXT("hatch"));
	AimAt(Large, FVector::ZeroVector, TEXT("input_aim_hatch"));
	Press(Large, 0.3f, TEXT("input_open_hatch"));
	// 덧댄 것이 지나가는 길 밖으로 비켜서 기다린다.
	Walk(FVector(40.0f, -110.0f, 690.0f), 20.0f, TEXT("input_step_aside"), TEXT("lining_moves"));
	if (bRecordAV)
	{
		// 덧댄 것이 벽에서 떨어져 큰 점검구로 옮겨 가는 동안. 실제 조명에서 몸과 다리를 본다.
		AimPoint(FVector(-90.0f, 20.0f, 720.0f), TEXT("av_look_lining"));
		Until([this]() { Shoot(TEXT("n3av_lining_moving_a")); return true; }, 0.1f, TEXT("av_shot_lining_a"));
		Wait(1.2f);
		Until([this]() { Shoot(TEXT("n3av_lining_moving_b")); return true; }, 0.1f, TEXT("av_shot_lining_b"));
		Wait(1.2f);
		Until([this]() { Shoot(TEXT("n3av_lining_moving_c")); return true; }, 0.1f, TEXT("av_shot_lining_c"));
	}
	Until([WeakDirector]() { return WeakDirector.IsValid() && WeakDirector->GetStorageStage() == FName(TEXT("Freed")); },
		60.0f, TEXT("input_jeongwoo_freed"));
	// 꺼내자마자 다른 걸 해도 판에 끼어 있던 때의 말은 다시 나오지 않는다.
	Until([this]() { return !HasLinesIn(IGLeftoverDialogue::Trapped); }, 0.5f, TEXT("input_no_trapped_line_after_rescue"));
	Wait(3.0f);
	// 판 밑 상자를 정우와 같이 뺀다.
	Walk(FVector(25.6f, -40.8f, 690.0f), 14.0f, TEXT("input_walk_to_box"), TEXT("box"));
	AimAt(Target(TEXT("Storage.DohaPull")), FVector::ZeroVector, TEXT("input_aim_box"));
	Press(Target(TEXT("Storage.DohaPull")), 1.9f, TEXT("input_ask_brace"));
	Until([WeakDirector]() { return WeakDirector.IsValid() && WeakDirector->GetStorageStage() == FName(TEXT("Bracing"))
		&& WeakDirector->GetJeongwoo() && !WeakDirector->GetJeongwoo()->IsWalking(); }, 25.0f, TEXT("input_jeongwoo_braces"));
	// 받치는 동안 302호를 잠깐 나갔다 온다. 정우는 판을 놓고 문을 잡으러 가고, 돌아와 다시 부탁하면 다시 받친다.
	// 받치는 정우가 등 뒤(동쪽)에 있다. 서쪽으로 돌아 나간다.
	Walk(FVector(25.0f, -58.0f, 690.0f), 18.0f, TEXT("input_back_off_from_box"));
	Walk(FVector(-15.0f, -70.0f, 690.0f), 20.0f, TEXT("input_round_the_legs_out"));
	Walk(FVector(-32.0f, -125.0f, 690.0f), 25.0f, TEXT("input_head_for_door"));
	Walk(FVector(-30.0f, -246.0f, 690.0f), 22.0f, TEXT("input_step_out_while_bracing"));
	Until([WeakDirector]() { return WeakDirector.IsValid() && WeakDirector->GetStorageStage() == FName(TEXT("Leaving")); },
		3.0f, TEXT("input_leaving_releases_board"));
	Until([this]() { return !HasLinesIn(IGLeftoverDialogue::Brace); }, 0.5f, TEXT("input_no_brace_line_after_release"));
	Walk(FVector(-32.0f, -125.0f, 690.0f), 25.0f, TEXT("input_come_back_in"));
	Until([WeakDirector]() { return WeakDirector.IsValid() && WeakDirector->GetStorageStage() == FName(TEXT("Leaving"))
		&& WeakDirector->GetJeongwoo() && !WeakDirector->GetJeongwoo()->IsWalking(); }, 20.0f, TEXT("input_jeongwoo_waits_at_door"));
	Walk(FVector(40.0f, -110.0f, 690.0f), 20.0f, TEXT("input_return_past_legs"));
	Walk(FVector(25.6f, -40.8f, 690.0f), 14.0f, TEXT("input_walk_back_to_box"));
	AimAt(Target(TEXT("Storage.DohaPull")), FVector::ZeroVector, TEXT("input_aim_box_after_return"));
	Press(Target(TEXT("Storage.DohaPull")), 1.9f, TEXT("input_ask_brace_again"));
	Until([WeakDirector]() { return WeakDirector.IsValid() && WeakDirector->GetStorageStage() == FName(TEXT("Bracing"))
		&& WeakDirector->GetJeongwoo() && !WeakDirector->GetJeongwoo()->IsWalking(); }, 25.0f, TEXT("input_jeongwoo_braces_again"));
	AimAt(Target(TEXT("Storage.DohaPull")), FVector::ZeroVector, TEXT("input_aim_box_again"));
	Press(Target(TEXT("Storage.DohaPull")), 1.9f, TEXT("input_pull_box_1"));
	Wait(0.6f);
	Press(Target(TEXT("Storage.DohaPull")), 1.9f, TEXT("input_pull_box_2"));
	Until([this]() { return GetNarrative() != nullptr && GetNarrative()->HasLeftover(EIGLeftoverList::Taken, FName(TEXT("DohaBox"))); },
		3.0f, TEXT("input_box_out"));
	// 연달아 당겨 빼도 「상자가 걸렸어요」가 뒤늦게 뜨지 않는다.
	Until([this]() { return !HasLinesIn(IGLeftoverDialogue::BoxStuck); }, 0.5f, TEXT("input_no_stale_box_line"));
	// 정우가 문을 잡고 기다리는 동안 상자 자리를 다시 본다. 지난 말(걸렸다, 받치겠다)이 되살아나지 않는다.
	Until([WeakDirector]() { return WeakDirector.IsValid() && WeakDirector->GetStorageStage() == FName(TEXT("Leaving"))
		&& WeakDirector->GetJeongwoo() && !WeakDirector->GetJeongwoo()->IsWalking(); }, 20.0f, TEXT("input_jeongwoo_holds_door"));
	AimAt(Target(TEXT("Storage.DohaPull")), FVector::ZeroVector, TEXT("input_look_at_box_spot"));
	Press(nullptr, 0.3f, TEXT("input_press_at_box_spot"));
	Wait(1.0f);
	Until([this, WeakDirector]() { return !HasLinesIn(IGLeftoverDialogue::BoxStuck) && !HasLinesIn(IGLeftoverDialogue::Brace)
		&& WeakDirector.IsValid() && WeakDirector->GetStorageStage() == FName(TEXT("Leaving")); },
		0.5f, TEXT("input_box_spot_again_no_stale_line"));
	// 마대에서 남은 조각과 틀을 꺼낸다.
	Walk(FVector(52.0f, -100.0f, 690.0f), 16.0f, TEXT("input_walk_to_sack"), TEXT("sack"));
	AimAt(Target(TEXT("Storage.Sack")), FVector::ZeroVector, TEXT("input_aim_sack"));
	Press(Target(TEXT("Storage.Sack")), 1.0f, TEXT("input_open_sack"));
	Until([this]() { return GetNarrative() != nullptr && GetNarrative()->HasLeftover(EIGLeftoverList::Taken, FName(TEXT("Frame"))); },
		3.0f, TEXT("input_sack_parts"));
	// 덮개 맞추기(P6). 조립대 북쪽에 서서 바닥 조각과 틀을 본다.
	Walk(FVector(-2.0f, -128.0f, 690.0f), 14.0f, TEXT("input_walk_to_table"), TEXT("p6"));
	// B: 처음엔 90도 돌고 방수막이 위다. 세 번 돌려 바로 세우고, 막 쪽이 위인 채로 한 번 대 본다(실패 원인).
	AimAt(Target(TEXT("Assembly.Pick1")), FVector::ZeroVector, TEXT("input_aim_pick_b"));
	Press(Target(TEXT("Assembly.Pick1")), 0.12f, TEXT("input_pick_b"));
	Wait(0.3f);
	for (int32 Tap = 0; Tap < 3; ++Tap)
	{
		AimAt(Target(TEXT("Assembly.Held")), FVector::ZeroVector, TEXT("input_aim_held"));
		Press(Target(TEXT("Assembly.Held")), 0.12f, TEXT("input_turn_b"));
		Wait(0.25f);
	}
	AimAt(Target(TEXT("Assembly.Slot1")), FVector::ZeroVector, TEXT("input_aim_slot_b"));
	Press(Target(TEXT("Assembly.Slot1")), 0.12f, TEXT("input_try_b_membrane_up"));
	Until([this]() { return GetNarrative() != nullptr && !GetNarrative()->HasBeatPlayed(FName(TEXT("Leftover.Assembly.B"))); },
		0.5f, TEXT("input_b_membrane_up_rejected"));
	Wait(1.7f);
	AimAt(Target(TEXT("Assembly.Held")), FVector::ZeroVector, TEXT("input_aim_held_flip"));
	Press(Target(TEXT("Assembly.Held")), 0.8f, TEXT("input_flip_b"));
	Wait(0.3f);
	AimAt(Target(TEXT("Assembly.Slot1")), FVector::ZeroVector, TEXT("input_aim_slot_b_again"));
	Press(Target(TEXT("Assembly.Slot1")), 0.12f, TEXT("input_seat_b"));
	Until([this]() { return GetNarrative() != nullptr && GetNarrative()->HasBeatPlayed(FName(TEXT("Leftover.Assembly.B"))); },
		1.0f, TEXT("input_b_seated"));
	// A: 180도. 두 번 돌리고 뒤집는다.
	AimAt(Target(TEXT("Assembly.Pick0")), FVector::ZeroVector, TEXT("input_aim_pick_a"));
	Press(Target(TEXT("Assembly.Pick0")), 0.12f, TEXT("input_pick_a"));
	Wait(0.3f);
	for (int32 Tap = 0; Tap < 2; ++Tap)
	{
		AimAt(Target(TEXT("Assembly.Held")), FVector::ZeroVector, TEXT("input_aim_held"));
		Press(Target(TEXT("Assembly.Held")), 0.12f, TEXT("input_turn_a"));
		Wait(0.25f);
	}
	AimAt(Target(TEXT("Assembly.Held")), FVector::ZeroVector, TEXT("input_aim_held_flip"));
	Press(Target(TEXT("Assembly.Held")), 0.8f, TEXT("input_flip_a"));
	Wait(0.3f);
	AimAt(Target(TEXT("Assembly.Slot0")), FVector::ZeroVector, TEXT("input_aim_slot_a"));
	Press(Target(TEXT("Assembly.Slot0")), 0.12f, TEXT("input_seat_a"));
	Until([this]() { return GetNarrative() != nullptr && GetNarrative()->HasBeatPlayed(FName(TEXT("Leftover.Assembly.A"))); },
		1.0f, TEXT("input_a_seated"));
	// C: 270도. 한 번 돌리고 뒤집는다.
	AimAt(Target(TEXT("Assembly.Pick2")), FVector::ZeroVector, TEXT("input_aim_pick_c"));
	Press(Target(TEXT("Assembly.Pick2")), 0.12f, TEXT("input_pick_c"));
	Wait(0.3f);
	AimAt(Target(TEXT("Assembly.Held")), FVector::ZeroVector, TEXT("input_aim_held"));
	Press(Target(TEXT("Assembly.Held")), 0.12f, TEXT("input_turn_c"));
	Wait(0.25f);
	AimAt(Target(TEXT("Assembly.Held")), FVector::ZeroVector, TEXT("input_aim_held_flip"));
	Press(Target(TEXT("Assembly.Held")), 0.8f, TEXT("input_flip_c"));
	Wait(0.3f);
	AimAt(Target(TEXT("Assembly.Slot2")), FVector::ZeroVector, TEXT("input_aim_slot_c"));
	Press(Target(TEXT("Assembly.Slot2")), 0.12f, TEXT("input_seat_c"));
	Until([this]() { return GetNarrative() != nullptr && GetNarrative()->GetLeftover().AssemblyStage == 1; }, 1.5f, TEXT("input_assembled"));
	// 뒤집어 앞면을 보고, 방수막을 들추고, 챙긴다.
	AimAt(Target(TEXT("Assembly.Flip")), FVector::ZeroVector, TEXT("input_aim_flip"));
	Press(Target(TEXT("Assembly.Flip")), 0.8f, TEXT("input_flip_cover"));
	Wait(1.0f);
	AimAt(Target(TEXT("Assembly.Membrane")), FVector::ZeroVector, TEXT("input_aim_membrane"));
	Press(Target(TEXT("Assembly.Membrane")), 1.1f, TEXT("input_lift_membrane"));
	Until([this]() { return GetNarrative() != nullptr && GetNarrative()->GetLeftover().AssemblyStage == 2; }, 2.0f, TEXT("input_vent_seen"));
	Wait(1.5f);
	AimAt(Target(TEXT("Assembly.Take")), FVector::ZeroVector, TEXT("input_aim_take"));
	Press(Target(TEXT("Assembly.Take")), 0.12f, TEXT("input_take_cover"));
	Until([this]() { return GetNarrative() != nullptr && GetNarrative()->HasLeftover(EIGLeftoverList::Taken, FName(TEXT("Cover"))); },
		2.0f, TEXT("input_cover_taken"));
	// 걸어 나간다. 정우가 문을 잡고 있었으면 같이 나간 것이고, 오래 남아 있었으면 먼저 내려갔다.
	Walk(FVector(-30.0f, -205.0f, 690.0f), 22.0f, TEXT("input_walk_to_door_out"), TEXT("leave"));
	Walk(FVector(-30.0f, -320.0f, 690.0f), 25.0f, TEXT("input_walk_out"));
	Until([this]() { return GetNarrative() != nullptr && GetNarrative()->GetLeftover().JeongwooState == FName(TEXT("LeftTogether")); },
		12.0f, TEXT("input_jeongwoo_out"));
	if (bNight3Av)
	{
		BuildNightThreeContinuation();
	}
}
