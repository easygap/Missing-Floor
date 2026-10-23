#include "Sequence/IGNightFourHammerProbe.h"

#include "Components/CapsuleComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameViewportClient.h"
#include "Entity/IGLiningEntity.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "Entity/IGShadowFigure.h"
#include "EngineUtils.h"
#include "Environment/IGStairSensorLights.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGDoorLatch.h"
#include "Interaction/IGSwingDoor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGNightFourHammerProbe
{
	/**
	 * 5층 동쪽 벽 앞, 망치를 드는 자리와 벽의 판 가운데. 월드 좌표다. 0.10.0부터 덧댄 것이 벽을
	 * 따라 이음매로 왔다가 점검구로 옮겨 간다. 몸판은 벽에서 36 cm 안으로 붙어 가고 발도 벽 가까이만
	 * 짚지만, 무릎은 벽에서 1 m 남짓까지 나온다. 그래서 벽에서 110 cm 떨어져 선다(망치는 220 cm까지
	 * 닿는다). 바짝 붙어 있으면 지나가는 몸판에 눌린다.
	 */
	const FVector WallFeet(140.0f, 700.0f, 1200.0f);
	const FVector WallFace(246.0f, 700.0f, 1290.0f);
	/** 첫째~셋째 단계 동안 망치 든 자리에서 밀려나도 되는 거리(cm). 덧댄 것의 발에 걸려 끌려가면 넘는다. */
	constexpr float MaxHammerDrift = 5.0f;
	/** 넷째 타 뒤 벽 안의 대답까지(밤4 디렉터의 3.6 + 4.4초). 이보다 일찍 풀리면 부름을 건너뛴 것이다. */
	constexpr float ExpectedReleaseSeconds = 8.0f;
}

AIGNightFourHammerProbe::AIGNightFourHammerProbe()
{
	// 검사기는 매 프레임 망치·손님·어둑시니의 상태를 잰다. -IGNightFourHammerProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGNightFourHammerProbe::Configure(
	AIGPrologueWorldScene* InScene,
	AIGPlayerCharacter* InPlayer,
	AIGListenerEntity* InListener)
{
	Scene = InScene;
	Player = InPlayer;
	Listener = InListener;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGNightFourHammerShots"));
	bDiag = FParse::Param(FCommandLine::Get(), TEXT("IGNightFourWallDiag"));
	bShots |= bDiag;
	bOpenDoor = FParse::Param(FCommandLine::Get(), TEXT("IGNightFourHammerOpen"));
	if (bDiag)
	{
		// 진단 화면은 세계를 멈춘 채 찍는다. 멈춘 동안에도 이 검사기는 돈다.
		SetTickableWhenPaused(true);
	}
}

namespace IGNightFourWallDiag
{
	const TCHAR* ViewNames[] = {
		TEXT("a_game"), TEXT("b_nohud"), TEXT("c_nolining"), TEXT("d_unlit"),
		TEXT("e_normal"), TEXT("f_noshadow"), TEXT("g_noaa"), TEXT("h_wire")};
	constexpr int32 ViewCount = UE_ARRAY_COUNT(ViewNames);
	/** 한 화면을 고른 뒤 찍기까지 기다리는 프레임. 시간 누적 안티앨리어싱이 멈춘 화면에서 자리 잡는다. */
	constexpr int32 SettleFrames = 8;
	/** 움직이며 찍는 프레임 수(30fps): 물러서기 45, 다가가기 45, 왼쪽 30, 오른쪽 30, 왼쪽 보기 20, 오른쪽 보기 20. */
	constexpr int32 MotionFrames = 190;
}

float AIGNightFourHammerProbe::MeasureLiningDistance(FString& OutPart) const
{
	const AIGLiningEntity* Body = Lining.Get();
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Body || !PC || !PC->PlayerCameraManager)
	{
		OutPart = TEXT("none");
		return -1.0f;
	}
	const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
	float Best = FVector::Dist(Eye, Body->GetBodyCenter());
	OutPart = TEXT("body");
	for (int32 Leg = 0; Leg < AIGLiningEntity::LegCount; ++Leg)
	{
		const float Knee = FVector::Dist(Eye, Body->GetKneeLocation(Leg));
		if (Knee < Best)
		{
			Best = Knee;
			OutPart = FString::Printf(TEXT("knee%d"), Leg);
		}
		const float Pad = FVector::Dist(Eye, Body->GetPadLocation(Leg));
		if (Pad < Best)
		{
			Best = Pad;
			OutPart = FString::Printf(TEXT("pad%d"), Leg);
		}
	}
	return Best;
}

void AIGNightFourHammerProbe::ApplyWallDiagnosticView(const int32 View, const bool bOn)
{
	UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Viewport || !PC)
	{
		return;
	}
	if (AHUD* Hud = PC->GetHUD())
	{
		Hud->bShowHUD = !bOn || View == 0;
	}
	if (AIGLiningEntity* Body = Lining.Get())
	{
		Body->SetActorHiddenInGame(bOn && View == 2);
	}
	Viewport->ViewModeIndex = !bOn ? VMI_Lit
		: View == 3 ? VMI_Unlit
		: View == 4 ? VMI_VisualizeBuffer
		: View == 7 ? VMI_Wireframe
		: VMI_Lit;
	if (IConsoleVariable* Target = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BufferVisualizationTarget")))
	{
		Target->Set(bOn && View == 4 ? TEXT("WorldNormal") : TEXT(""), ECVF_SetByCode);
	}
	Viewport->EngineShowFlags.SetDynamicShadows(!(bOn && View == 5));
	static int32 OriginalAntiAliasing = -1;
	if (IConsoleVariable* AntiAliasing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod")))
	{
		if (OriginalAntiAliasing < 0)
		{
			OriginalAntiAliasing = AntiAliasing->GetInt();
		}
		AntiAliasing->Set(bOn && View == 6 ? 0 : OriginalAntiAliasing, ECVF_SetByCode);
	}
}

void AIGNightFourHammerProbe::BeginWallDiagnosticMoment(const TCHAR* Name)
{
	DiagMoment = Name;
	DiagView = 0;
	DiagWaitFrames = IGNightFourWallDiag::SettleFrames;
	DiagPendingShot.Reset();
	UGameplayStatics::SetGamePaused(this, true);
	ApplyWallDiagnosticView(0, true);
	FString Part;
	const float Distance = MeasureLiningDistance(Part);
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const FVector Eye = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const AIGLiningEntity* Body = Lining.Get();
	UE_LOG(LogIndieGame, Display,
		TEXT("NIGHT4DIAG_MOMENT %s t=%.1f eye=%s lining_state=%d nearest=%.1f part=%s body=%s near_clip=%.1f"),
		Name, TotalSeconds, *Eye.ToCompactString(), Body ? static_cast<int32>(Body->GetState()) : -1, Distance, *Part,
		Body ? *Body->GetBodyCenter().ToCompactString() : TEXT("none"), GNearClippingPlane);
}

bool AIGNightFourHammerProbe::TickWallDiagnostics(const float DeltaSeconds)
{
	using namespace IGNightFourWallDiag;
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NightFourWallDiag")));
	// 화면 하나를 찍는 중이다. 파일이 나올 때까지 기다린 뒤 다음 화면으로 간다.
	if (DiagView >= 0)
	{
		if (!DiagPendingShot.IsEmpty())
		{
			if (!FPaths::FileExists(DiagPendingShot))
			{
				return true;
			}
			DiagPendingShot.Reset();
			ApplyWallDiagnosticView(DiagView, false);
			++DiagView;
			if (DiagView >= ViewCount)
			{
				DiagView = -1;
				++DiagMomentsTaken;
				UGameplayStatics::SetGamePaused(this, false);
				// 처음으로 덧댄 것이 가까이 온 순간 뒤에 움직이며 찍는다.
				FString Part;
				if (DiagMotionFrame < 0 && MeasureLiningDistance(Part) >= 0.0f && MeasureLiningDistance(Part) < 120.0f)
				{
					DiagMotionFrame = 0;
				}
				return DiagMotionFrame >= 0;
			}
			ApplyWallDiagnosticView(DiagView, true);
			DiagWaitFrames = SettleFrames;
			return true;
		}
		if (--DiagWaitFrames > 0)
		{
			return true;
		}
		IFileManager::Get().MakeDirectory(*Directory, true);
		DiagPendingShot = FPaths::Combine(Directory, FString::Printf(TEXT("%s_%s.png"), *DiagMoment, ViewNames[DiagView]));
		FScreenshotRequest::RequestScreenshot(DiagPendingShot, false, false);
		return true;
	}
	// 움직이며 찍기. 실제 이동 입력을 넣고 프레임마다 남긴다. 순간 이동하지 않는다.
	if (DiagMotionFrame >= 0 && DiagMotionFrame < MotionFrames)
	{
		AIGPlayerCharacter* Character = Player.Get();
		APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
		if (!Character || !PC)
		{
			DiagMotionFrame = MotionFrames;
			return false;
		}
		const FRotator Yaw(0.0f, PC->GetControlRotation().Yaw, 0.0f);
		const FVector Forward = Yaw.Vector();
		const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
		const int32 F = DiagMotionFrame;
		const TCHAR* Motion = F < 45 ? TEXT("back") : F < 90 ? TEXT("forward") : F < 120 ? TEXT("left")
			: F < 150 ? TEXT("right") : F < 170 ? TEXT("turn_left") : TEXT("turn_right");
		if (F < 45) { Character->AddMovementInput(-Forward, 1.0f); }
		else if (F < 90) { Character->AddMovementInput(Forward, 1.0f); }
		else if (F < 120) { Character->AddMovementInput(-Right, 1.0f); }
		else if (F < 150) { Character->AddMovementInput(Right, 1.0f); }
		else if (F < 170) { PC->AddYawInput(-1.2f); }
		else { PC->AddYawInput(1.2f); }
		FString Part;
		const float Distance = MeasureLiningDistance(Part);
		UE_LOG(LogIndieGame, Display, TEXT("NIGHT4DIAG_FRAME %d %s nearest=%.1f part=%s"), F, Motion, Distance, *Part);
		IFileManager::Get().MakeDirectory(*(Directory / TEXT("motion")), true);
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(Directory, TEXT("motion"), FString::Printf(TEXT("frame_%04d_%s.png"), F, Motion)), false, false);
		++DiagMotionFrame;
		if (DiagMotionFrame == MotionFrames)
		{
			// 원래 망치 자리로 돌아간다. 이 뒤의 단계는 평소 검사와 같다.
			Stand(IGNightFourHammerProbe::WallFeet, IGNightFourHammerProbe::WallFace);
		}
		return true;
	}
	return false;
}

void AIGNightFourHammerProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGNightFourHammerProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER_PROBE %s failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGNightFourHammerProbe::Next(const int32 NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
	bActed = false;
}

void AIGNightFourHammerProbe::Stand(const FVector& Feet, const FVector& LookAt)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !PC)
	{
		return;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(Feet + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr,
		ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	const FVector Eye = Character->GetActorLocation() + Character->GetEyeOffsetFromActor();
	PC->SetControlRotation((LookAt - Eye).Rotation());
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
	}
}

void AIGNightFourHammerProbe::Shoot(const TCHAR* Name)
{
	if (!bShots)
	{
		return;
	}
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NightFourHammerReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER_SHOT %s"), *Path);
}

bool AIGNightFourHammerProbe::SaidLine(const FText& Line) const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	return Hud && Hud->HasDialogueLineForTesting(Line.ToString());
}

bool AIGNightFourHammerProbe::Strike()
{
	AIGMissingFloorNightFourDirector* Director = NightFour.Get();
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	if (!Director || !Narrative)
	{
		return false;
	}
	// 0.10.0 벽은 단계마다 칠 자리가 따로다. 살펴보기·틀 세우기 같은 손일은 단계를 올리지 않으니
	// 단계가 오를 때까지 다음 자리로 넘어간다. 덧댄 것이 움직이는 중이면 쓸 자리가 없다.
	const int32 Before = Narrative->GetNightFourWallProgress();
	for (int32 Guard = 0; Guard < 3; ++Guard)
	{
		AIGMissingFloorEvidence* Next = Director->GetNextWallActionForTesting();
		if (!Next)
		{
			return false;
		}
		Next->OnExamined.Broadcast(Next);
		if (Narrative->GetNightFourWallProgress() > Before)
		{
			++Struck;
			UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER stage=%d t=%.1f"), Narrative->GetNightFourWallProgress(), TotalSeconds);
			return true;
		}
	}
	return false;
}

bool AIGNightFourHammerProbe::IsCapturedSince(const double Seconds) const
{
	const AIGNightLoopDirector* Loop = NightLoop.Get();
	return Loop && (Loop->IsCaptureResetInFlight() || Loop->GetLastCaptureSeconds() > Seconds);
}

void AIGNightFourHammerProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	if (bDiag && TickWallDiagnostics(DeltaSeconds))
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > 300.0f)
	{
		Check(false, TEXT("probe_timeout"));
		Finish();
		return;
	}
	using namespace IGNightFourHammerProbe;
	UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	AIGPrologueWorldScene* Building = Scene.Get();
	AIGMissingFloorNightFourDirector* Director = NightFour.Get();
	AIGNightThreatDirector* Threat = Threats.Get();
	AIGMissingFloorEvidence* Wall = Director ? Director->GetWallBreakTarget() : nullptr;
	const FText HammerCall = NSLOCTEXT("IGMissingFloor", "GuestN4HammerCall", "유담아. 오빠야. 문 좀 열어 줘.");

	if (Phase > 0 && (!Narrative || !Building || !Director || !Threat || !Wall))
	{
		Check(false, TEXT("night4_actors_still_exist"));
		Finish();
		return;
	}

	switch (Phase)
	{
	case 0:
	{
		if (PhaseSeconds < (bShots ? 12.0f : 1.0f))
		{
			return;
		}
		for (TActorIterator<AIGMissingFloorNightFourDirector> It(GetWorld()); It; ++It)
		{
			NightFour = *It;
		}
		for (TActorIterator<AIGNightThreatDirector> It(GetWorld()); It; ++It)
		{
			Threats = *It;
		}
		for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It)
		{
			NightLoop = *It;
		}
		for (TActorIterator<AIGMissingFloorNightThreeDirector> It(GetWorld()); It; ++It)
		{
			AnnexDoor = It->GetAnnexGate();
			AnnexBolt = It->GetAnnexBolt();
		}
		Director = NightFour.Get();
		Threat = Threats.Get();
		Check(Narrative && Building && Director && Threat && NightLoop.IsValid() && AnnexDoor.IsValid()
			&& AnnexBolt.IsValid() && Player.IsValid(), TEXT("night4_actors_resolved"));
		if (!Narrative || !Building || !Director || !Threat || !NightLoop.IsValid() || !AnnexDoor.IsValid()
			|| !AnnexBolt.IsValid() || !Player.IsValid())
		{
			Finish();
			return;
		}
		if (Listener.IsValid())
		{
			Listener->SetDormant(true);
		}
		if (UIGFlashlightComponent* Torch = Player->GetFlashlight())
		{
			Torch->SetAvailable(true);
			Torch->SetOn(true);
		}
		// 넷째 밤. 벽을 칠 수 있게 진실 셋을 세운다.
		Narrative->SetNightIndex(4);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::SomeoneInTheWall, EIGMissingFloorSource::PipeAuditionCriterion);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::SomeoneInTheWall, EIGMissingFloorSource::PipeWaterComparison);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::CarbonLedgerOriginal);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WasStillAlive, EIGMissingFloorSource::AgentMoveOutMessage);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WaitingForAnAnswer, EIGMissingFloorSource::AnswerRhythmMaterials);
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::WaitingForAnAnswer, EIGMissingFloorSource::AnswerReturned);
		Check(Narrative->IsFinalChoiceUnlocked(), TEXT("final_choice_unlocked"));
		// 철문은 닫고, 빗장은 장면대로.
		AnnexDoor->ForceOpenState(false);
		AnnexDoor->SetLatched(!bOpenDoor, false);
		// 물소리 가면. 옥상 세척 배수 → 물탱크 우회 → 1층 이송 펌프.
		for (AIGMissingFloorEvidence* Control : {Director->GetCleaningDrain(), Director->GetFloatBypass(), Director->GetTransferPump()})
		{
			if (Control)
			{
				Control->OnExamined.Broadcast(Control);
			}
		}
		Check(Narrative->IsNightFourMaskRunning(), TEXT("water_mask_running"));
		Stand(WallFeet, WallFace);
		if (bDiag)
		{
			for (TActorIterator<AIGLiningEntity> It(GetWorld()); It; ++It)
			{
				Lining = *It;
			}
			const auto CVarText = [](const TCHAR* Name)
			{
				const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
				return Variable ? Variable->GetString() : FString(TEXT("missing"));
			};
			UE_LOG(LogIndieGame, Display,
				TEXT("NIGHT4DIAG_SETTINGS aa=%s screen_percentage=%s gi=%s reflections=%s vsm=%s shadow_quality=%s ")
				TEXT("post_quality=%s view_distance=%s motion_blur=%s near_clip=%.1f lining=%d"),
				*CVarText(TEXT("r.AntiAliasingMethod")), *CVarText(TEXT("r.ScreenPercentage")),
				*CVarText(TEXT("r.DynamicGlobalIlluminationMethod")), *CVarText(TEXT("r.ReflectionMethod")),
				*CVarText(TEXT("r.Shadow.Virtual.Enable")), *CVarText(TEXT("sg.ShadowQuality")),
				*CVarText(TEXT("sg.PostProcessQuality")), *CVarText(TEXT("sg.ViewDistanceQuality")),
				*CVarText(TEXT("r.MotionBlurQuality")), GNearClippingPlane, Lining.IsValid() ? 1 : 0);
		}
		CaptureBaseline = NightLoop->GetLastCaptureSeconds();
		Next(1);
		break;
	}

	case 1:
		// 첫째에서 셋째 단계. 덧댄 것이 이음매로 왔다가 점검구로 옮겨 가 몸을 펴고, 틀에 걸린다.
		// 셋째 뒤 0.7초에 차단기가 내려간다. 그동안 망치 든 사람은 제자리에 있어야 한다.
		if (bDiag && DiagMomentsTaken < 5 && PhaseSeconds >= DiagNextMomentSeconds)
		{
			// 맨 처음(덧댄 것이 오기 전)과 그 뒤 3초마다 네 순간을 찍는다.
			DiagNextMomentSeconds = PhaseSeconds + (DiagMomentsTaken == 0 ? 2.0f : 3.0f);
			BeginWallDiagnosticMoment(*FString::Printf(TEXT("m%d_stage%d"), DiagMomentsTaken + 1,
				Narrative->GetNightFourWallProgress()));
			return;
		}
		if (Player.IsValid() && !bDiag)
		{
			MaxDrift = FMath::Max(MaxDrift, FVector::Dist2D(Player->GetActorLocation(), WallFeet));
		}
		if (!bActed && PhaseSeconds >= 0.5f)
		{
			bActed = true;
			Check(Wall->IsInteractionEnabled(), TEXT("wall_can_be_struck"));
		}
		if (bActed && Narrative->GetNightFourWallProgress() < 3 && PhaseSeconds - LastActionSeconds >= 0.6f)
		{
			if (Strike())
			{
				LastActionSeconds = PhaseSeconds;
			}
		}
		if (ThirdStageSeconds < 0.0f && Narrative->GetNightFourWallProgress() >= 3)
		{
			ThirdStageSeconds = PhaseSeconds;
		}
		if (ThirdStageSeconds >= 0.0f && PhaseSeconds >= ThirdStageSeconds + 1.6f)
		{
			Check(Struck == 3 && !Building->IsMissingFloorAnnexPowered(), TEXT("third_strike_cuts_the_power"));
			// 계단 센서등은 복도등과 회로가 따로라 공용 등 판정에 들지 않는다. 건물 차단기에는 같이 죽는다.
			const AIGStairSensorLights* Sensors = Building->GetStairSensorLights();
			Check(Building->AreCommonInspectionLightsOff() && Sensors && !Sensors->IsAnyLampOn(),
				TEXT("corridor_lobby_and_sensor_lights_die"));
			UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER drift=%.1f"), MaxDrift);
			Check(MaxDrift < MaxHammerDrift && !IsCapturedSince(CaptureBaseline), TEXT("lining_passes_the_hammer_spot"));
			Next(2);
		}
		else if (PhaseSeconds >= 60.0f)
		{
			Check(false, TEXT("third_strike_cuts_the_power"));
			Finish();
		}
		break;

	case 2:
		// 넷째 단계. 밀린 틀을 다시 세우고 안쪽 판 나사를 친다. 손이 저려 잠깐 못 치고, 그 사이
		// 철문 밖에서 부른다. 벽 안의 대답 뒤 다시 든다.
		if (!bActed && PhaseSeconds >= 0.3f)
		{
			bActed = true;
			Check(Strike() && Narrative->GetNightFourWallProgress() == 4, TEXT("fourth_strike"));
		}
		if (bActed && PhaseSeconds >= 1.0f && PhaseSeconds < 7.0f)
		{
			bSawHeld |= !Wall->IsInteractionEnabled();
		}
		bSawCall |= Threat->IsHammerCallActive() && Threat->GetGuestDoor() == EIGGuestDoor::Annex;
		if (PhaseSeconds >= 6.8f && PhaseSeconds - DeltaSeconds < 6.8f)
		{
			Check(bSawHeld, TEXT("hands_numb_after_fourth_strike"));
			Check(bSawCall, TEXT("someone_calls_at_the_annex_door"));
			Check(SaidLine(HammerCall), TEXT("brother_voice_asks_to_open"));
		}
		if (PhaseSeconds >= 8.7f && PhaseSeconds - DeltaSeconds < 8.7f)
		{
			Shoot(TEXT("hammer-wall-answer"));
		}
		if (bActed && PhaseSeconds >= 1.0f && Wall->IsInteractionEnabled())
		{
			Check(PhaseSeconds >= ExpectedReleaseSeconds - 0.6f, TEXT("hammer_back_after_wall_answers"));
			Next(3);
		}
		else if (PhaseSeconds >= 12.0f)
		{
			Check(false, TEXT("hammer_back_after_wall_answers"));
			Finish();
		}
		break;

	case 3:
		if (!bOpenDoor)
		{
			// 빗장. 열쇠를 꽂다 걸려 한마디 남기고 간다.
			bSawCaughtOnBolt |= Threat->GetGuestStage() == EIGGuestStage::CaughtOnLatch;
			if (Threat->GetGuestStage() == EIGGuestStage::Spent)
			{
				UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER_BOLT caught=%d closed=%d captured=%d seconds=%.1f"),
					bSawCaughtOnBolt ? 1 : 0, AnnexDoor->IsFullyClosed() ? 1 : 0, IsCapturedSince(CaptureBaseline) ? 1 : 0, PhaseSeconds);
				Check(bSawCaughtOnBolt && AnnexDoor->IsFullyClosed() && !IsCapturedSince(CaptureBaseline),
					TEXT("bolt_holds_the_hammer_call"));
				Next(4);
			}
			else if (PhaseSeconds >= 35.0f)
			{
				Check(false, TEXT("bolt_holds_the_hammer_call"));
				Finish();
			}
			break;
		}
		// 빗장 없음. 열고 들어와 잡는다. 공격성 3 아래라 결말 C가 아니라 그 철문 안쪽에서 깬다.
		if (IsCapturedSince(CaptureBaseline))
		{
			Check(NightLoop->IsCaptureResetInFlight() && Narrative->GetEndingChoice().IsNone(),
				TEXT("hammer_phase_capture_is_not_ending_c"));
			Next(6);
		}
		else if (PhaseSeconds >= 45.0f)
		{
			Check(false, TEXT("hammer_phase_capture_is_not_ending_c"));
			Finish();
		}
		break;

	case 4:
		// 정전 속에 어둑시니가 선다. 쳐다보면 커진다.
		if (!bActed && FMath::FloorToInt(PhaseSeconds / 2.0f) != FMath::FloorToInt((PhaseSeconds - DeltaSeconds) / 2.0f))
		{
			UE_LOG(LogIndieGame, Display, TEXT("NIGHT4HAMMER dark=%.2f room=%.2f dark_seconds=%.1f"),
				Player->GetDarkness(), Player->GetRoomDarkness(), Threat->GetDarknessSeconds());
		}
		if (!bActed && Threat->IsEoduksiniManifested())
		{
			bActed = true;
			PhaseSeconds = 0.0f;
			Check(true, TEXT("eoduksini_after_blackout"));
			// 등 뒤에 선 것은 빚은 몸이어야 한다. 몸을 못 읽으면 도형 윤곽으로 조용히 돌아가서
			// 메시가 빠져도 화면 말고는 티가 나지 않는다.
			bool bSculpted = false;
			for (TActorIterator<AIGShadowFigure> It(GetWorld()); It; ++It)
			{
				if (It->IsManifested() && !It->IsPaper())
				{
					bSculpted = It->IsSculpted();
				}
			}
			Check(bSculpted, TEXT("eoduksini_wears_sculpted_body"));
			Stand(WallFeet, Threat->GetEoduksiniLocation() + FVector(0.0f, 0.0f, 110.0f));
		}
		if (bActed && PhaseSeconds >= 1.2f && PhaseSeconds - DeltaSeconds < 1.2f)
		{
			Shoot(TEXT("hammer-eoduksini"));
		}
		if (bActed && PhaseSeconds >= 1.6f)
		{
			Stand(WallFeet, WallFace);
			Next(5);
		}
		else if (!bActed && PhaseSeconds >= 30.0f)
		{
			Check(false, TEXT("eoduksini_after_blackout"));
			Next(5);
		}
		break;

	case 5:
		// 다섯째 단계. 덧댄 것과 틀이 밀려난 뒤 남은 철물을 친다. 벽이 열리면 어둠도 손님도 그친다.
		if (!bActed && PhaseSeconds >= 0.3f && Director->GetNextWallActionForTesting())
		{
			bActed = true;
			PhaseSeconds = 0.3f;
			Check(Strike(), TEXT("fifth_strike"));
		}
		if (!bActed && PhaseSeconds >= 30.0f)
		{
			Check(false, TEXT("fifth_strike"));
			Finish();
		}
		if (!bActed)
		{
			break;
		}
		if (PhaseSeconds >= 2.0f)
		{
			Check(Narrative->IsNightFourWallOpened(), TEXT("wall_opens_on_fifth"));
			Check(!Threat->IsEoduksiniManifested()
				&& (Threat->GetGuestStage() == EIGGuestStage::Spent || Threat->GetGuestStage() == EIGGuestStage::Idle),
				TEXT("threats_stop_when_wall_opens"));
			Finish();
		}
		break;

	case 6:
		// 철문 안쪽 바닥에서 깼다. 빗장은 휘어 걸리지 않고, 정전은 그대로라 403호 전등도
		// 죽어 있어야 한다.
		if (NightLoop->IsCaptureResetInFlight() && PhaseSeconds < 20.0f)
		{
			return;
		}
		if (!bActed)
		{
			bActed = true;
			PhaseSeconds = 0.0f;
			const float FeetZ = Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			// 5층 바닥은 1200(손님 문 검사와 같은 바닥).
			Check(FMath::Abs(FeetZ - 1200.0f) < 60.0f
				&& FVector::Dist2D(Player->GetActorLocation(), AnnexDoor->GetActorLocation()) < 200.0f,
				TEXT("woke_inside_annex_door"));
			const AIGDoorLatch* Bolt = nullptr;
			for (TActorIterator<AIGDoorLatch> It(GetWorld()); It; ++It)
			{
				if (It->GetDoor() == AnnexDoor.Get()) { Bolt = *It; }
			}
			Check(Bolt && Bolt->IsBroken(), TEXT("guest_bent_the_bolt"));
			Check(!Building->IsMissingFloorAnnexPowered() && !Building->IsAnyUnitLightVisibleForTesting(),
				TEXT("home_lights_dead_in_blackout"));
		}
		if (PhaseSeconds >= 1.0f && PhaseSeconds - DeltaSeconds < 1.0f)
		{
			Shoot(TEXT("blackout-annex-wake"));
		}
		if (PhaseSeconds >= 1.5f)
		{
			Finish();
		}
		break;

	default:
		break;
	}
}
