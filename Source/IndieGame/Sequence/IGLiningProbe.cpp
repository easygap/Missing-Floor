#include "Sequence/IGLiningProbe.h"

#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGNightThreatDirector.h"
#include "EngineUtils.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/World.h"
#include "Entity/IGLiningEntity.h"
#include "Entity/IGNoiseSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Interaction/IGAccessHatch.h"
#include "Interaction/IGInteractionTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGLiningProbe
{
	// 302호(3층 바닥 600). 북쪽 벽 안쪽 면 Y 85, 동쪽 벽 X 120, 서쪽 벽 X -160, 문은 남쪽.
	const FVector RestPoint(30.0f, 85.0f, 700.0f);
	const FVector SmallCenter(120.0f, -20.0f, 728.0f);
	const FVector LargeCenter(-160.0f, -120.0f, 668.0f);
	const FVector DoorFeet(-30.0f, -192.0f, 600.0f);
	const FVector LookAt(-10.0f, 20.0f, 700.0f);
}

AIGLiningProbe::AIGLiningProbe()
{
	// 검사기는 매 프레임 몸의 상태와 덮개를 잰다. -IGLiningProbe로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGLiningProbe::Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer)
{
	Scene = InScene;
	Player = InPlayer;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGLiningShots"));
	UWorld* World = GetWorld();
	// 다른 괴이는 재운다. 어둑시니와 손님은 타이머로 돌아서 틱만 끄면 계속 나온다.
	for (TActorIterator<AIGNightThreatDirector> It(World); It; ++It)
	{
		It->SetActorTickEnabled(false);
		World->GetTimerManager().ClearAllTimersForObject(*It);
	}
	for (TActorIterator<AIGListenerEntity> It(World); It; ++It)
	{
		It->SetDormant(true);
	}
	if (bShots)
	{
		// 밤이라 302호 등이 꺼져 있다. 몸의 생김새를 보려고 검사 등을 켠다.
		UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this, TEXT("ProbeLamp"));
		Lamp->SetMobility(EComponentMobility::Movable);
		Lamp->SetIntensity(5200.0f);
		Lamp->SetAttenuationRadius(520.0f);
		Lamp->SetCastShadows(true);
		Lamp->RegisterComponent();
		Lamp->SetWorldLocation(FVector(-20.0f, -60.0f, 820.0f));
		UPointLightComponent* Fill = NewObject<UPointLightComponent>(this, TEXT("ProbeFill"));
		Fill->SetMobility(EComponentMobility::Movable);
		Fill->SetIntensity(1400.0f);
		Fill->SetAttenuationRadius(400.0f);
		Fill->SetCastShadows(false);
		Fill->RegisterComponent();
		Fill->SetWorldLocation(FVector(-30.0f, -170.0f, 760.0f));
		// 진단: 같은 메시를 자세 없이 바닥에 하나 더 세운다.
		if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Meshes/SK_Lining.SK_Lining")))
		{
			USkeletalMeshComponent* Plain = NewObject<USkeletalMeshComponent>(this, TEXT("PlainLining"));
			Plain->SetSkeletalMesh(Mesh);
			Plain->SetMobility(EComponentMobility::Movable);
			Plain->RegisterComponent();
			Plain->SetWorldLocation(FVector(-40.0f, -40.0f, 601.0f));
			UE_LOG(LogIndieGame, Display, TEXT("LINING_PLAIN bounds=%s/%s mats=%d mat0=%s"),
				*Plain->Bounds.Origin.ToCompactString(), *Plain->Bounds.BoxExtent.ToCompactString(),
				Plain->GetNumMaterials(), Plain->GetMaterial(0) ? *Plain->GetMaterial(0)->GetPathName() : TEXT("none"));
		}
	}
	AIGLiningEntity* Body = World->SpawnActor<AIGLiningEntity>();
	AIGAccessHatch* Small = World->SpawnActor<AIGAccessHatch>(AIGAccessHatch::StaticClass(),
		FTransform(FRotator(0.0f, 180.0f, 0.0f), IGLiningProbe::SmallCenter));
	AIGAccessHatch* Large = World->SpawnActor<AIGAccessHatch>(AIGAccessHatch::StaticClass(),
		FTransform(FRotator(0.0f, 0.0f, 0.0f), IGLiningProbe::LargeCenter));
	UMaterialInterface* Paint = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/MI_UnitDoorFrame.MI_UnitDoorFrame"), nullptr, LOAD_NoWarn);
	UMaterialInterface* Seal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/MI_Lining.MI_Lining"), nullptr, LOAD_NoWarn);
	if (Small)
	{
		Small->Configure(FVector2D(45.0f, 45.0f), true, nullptr, nullptr, Paint, Seal, FText::GetEmpty(), FText::GetEmpty());
	}
	if (Large)
	{
		Large->Configure(FVector2D(80.0f, 100.0f), false, nullptr, nullptr, Paint, Seal, FText::GetEmpty(), FText::GetEmpty());
	}
	SmallHatch = Small;
	LargeHatch = Large;
	Lining = Body;
	if (!Body || !Small || !Large || !Body->LoadBody())
	{
		Check(false, TEXT("spawned"));
		Finish();
		return;
	}
	Body->RestOnSurface(IGLiningProbe::RestPoint, FVector(0.0f, -1.0f, 0.0f), FVector::UpVector);
	FIGLiningGap SmallGap;
	SmallGap.Id = TEXT("Small");
	SmallGap.Center = IGLiningProbe::SmallCenter;
	SmallGap.Normal = FVector(-1.0f, 0.0f, 0.0f);
	SmallGap.Size = FVector2D(45.0f, 45.0f);
	SmallGap.Hatch = Small;
	Body->SetGap(SmallGap);
	FIGLiningGap LargeGap;
	LargeGap.Id = TEXT("Large");
	LargeGap.Center = IGLiningProbe::LargeCenter;
	LargeGap.Normal = FVector(1.0f, 0.0f, 0.0f);
	LargeGap.Size = FVector2D(80.0f, 100.0f);
	LargeGap.Hatch = Large;
	Body->SetGap(LargeGap);
	for (AIGAccessHatch* Hatch : {Small, Large})
	{
		Hatch->OnOpened.AddWeakLambda(this, [this](AIGAccessHatch*) { if (Lining.IsValid()) { Lining->NotifyGapsChanged(); } });
		Hatch->OnClosed.AddWeakLambda(this, [this](AIGAccessHatch*) { if (Lining.IsValid()) { Lining->NotifyGapsChanged(); } });
	}
	Small->OnCloseBlocked.AddWeakLambda(this, [this](AIGAccessHatch*) { bBlockedBounce = true; });
	Small->OnReopened.AddWeakLambda(this, [this](AIGAccessHatch*) { if (Lining.IsValid()) { Lining->UnsealGap(TEXT("Small")); } });
	Large->OnReopened.AddWeakLambda(this, [this](AIGAccessHatch*) { if (Lining.IsValid()) { Lining->UnsealGap(TEXT("Large")); } });
	Body->OnFirstMove.AddWeakLambda(this, [this](AIGLiningEntity*) { bFirstMove = true; });
	Body->OnTargetChosen.AddWeakLambda(this, [this](AIGLiningEntity*, const FName Gap) { Targets.Add(Gap); });
	Body->OnGapSealed.AddWeakLambda(this, [this](AIGLiningEntity*, const FName Gap) { Sealed.Add(Gap); });
	Body->OnGrippedPlayer.AddWeakLambda(this, [this](AIGLiningEntity*) { bGripped = true; });
	Stand(IGLiningProbe::DoorFeet, IGLiningProbe::LookAt);
}

void AIGLiningProbe::Check(const bool bCondition, const TCHAR* Name)
{
	if (!bCondition)
	{
		++Failures;
	}
	UE_LOG(LogIndieGame, Display, TEXT("LINING_CHECK %s %s"), Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
}

void AIGLiningProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display, TEXT("LINING_PROBE %s failures=%d seconds=%.1f"), Failures ? TEXT("FAIL") : TEXT("PASS"), Failures, TotalSeconds);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void AIGLiningProbe::Stand(const FVector& Feet, const FVector& LookAt)
{
	AIGPlayerCharacter* Character = Player.Get();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !PC)
	{
		return;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(Feet + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();
	const FVector Eye = Character->GetActorLocation() + Character->GetEyeOffsetFromActor();
	PC->SetControlRotation((LookAt - Eye).Rotation());
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
	}
}

void AIGLiningProbe::Shoot(const TCHAR* Name)
{
	if (!bShots)
	{
		return;
	}
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LiningReview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("%s.png"), Name));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
}

void AIGLiningProbe::Next(const int32 NewPhase)
{
	UE_LOG(LogIndieGame, Display, TEXT("LINING_PHASE %d -> %d at %.1f"), Phase, NewPhase, TotalSeconds);
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
}

void AIGLiningProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0)
	{
		return;
	}
	PhaseSeconds += DeltaSeconds;
	TotalSeconds += DeltaSeconds;
	AIGLiningEntity* Body = Lining.Get();
	AIGAccessHatch* Small = SmallHatch.Get();
	AIGAccessHatch* Large = LargeHatch.Get();
	if (!Body || !Small || !Large || TotalSeconds > 150.0f)
	{
		Check(false, TEXT("finished_in_time"));
		Finish();
		return;
	}
	if (bShots && Phase >= 1 && Phase <= 7)
	{
		ShotClock += DeltaSeconds;
		if (ShotClock >= 0.5f)
		{
			ShotClock = 0.0f;
			Shoot(*FString::Printf(TEXT("lining_%03d_p%d_s%d"), ShotIndex++, Phase, static_cast<int32>(Body->GetState())));
		}
	}
	switch (Phase)
	{
	case 0:
		if (PhaseSeconds >= 0.5f && PhaseSeconds - DeltaSeconds < 0.5f)
		{
			Body->LogPose(TEXT("rest"));
			Shoot(TEXT("lining_rest"));
			// 소리와 손전등에는 반응하지 않는다.
			if (UIGNoiseSubsystem* Noise = GetWorld()->GetSubsystem<UIGNoiseSubsystem>())
			{
				Noise->ReportNoiseUnmasked(Body->GetBodyCenter(), 1.0f, nullptr);
			}
		}
		if (PhaseSeconds >= 2.0f)
		{
			Check(Body->GetState() == EIGLiningState::Folded, TEXT("ignores_noise_while_folded"));
			Small->OpenByScript();
			Next(1);
		}
		break;
	case 1:
		if (Targets.Contains(TEXT("Small")) && bFirstMove)
		{
			Check(true, TEXT("small_gap_draws_it_out"));
			Next(2);
		}
		else if (PhaseSeconds > 3.0f)
		{
			Check(false, TEXT("small_gap_draws_it_out"));
			Next(2);
		}
		break;
	case 2:
		if (Sealed.Contains(TEXT("Small")))
		{
			Check(Small->IsSealed() && Body->GetHeldGap() == TEXT("Small"), TEXT("small_gap_pressed_and_sealed"));
			Check(Small->IsObstructed(), TEXT("hatch_caught_on_body"));
			// 누르는 중에 덮개를 닫아 본다.
			FIGInteractionContext Context;
			Context.TargetActor = Small;
			IIGInteractable::Execute_CompleteInteraction(Small, Context);
			Next(3);
		}
		else if (PhaseSeconds > 40.0f)
		{
			Check(false, TEXT("small_gap_pressed_and_sealed"));
			Finish();
		}
		break;
	case 3:
		if (PhaseSeconds >= 1.0f)
		{
			Check(bBlockedBounce && Small->IsOpen(), TEXT("hatch_bounces_off_leg"));
			Large->OpenByScript();
			Next(4);
		}
		break;
	case 4:
		if (Targets.Num() >= 2 && Targets.Last() == TEXT("Large"))
		{
			Check(true, TEXT("larger_gap_wins"));
			Next(5);
		}
		else if (PhaseSeconds > 3.0f)
		{
			Check(false, TEXT("larger_gap_wins"));
			Next(5);
		}
		break;
	case 5:
		// 큰 쪽으로 가는 도중에 작은 쪽을 다시 뜯는다. 방향을 꺾지 말아야 한다.
		if (PhaseSeconds >= 2.0f && PhaseSeconds - DeltaSeconds < 2.0f)
		{
			Check(Body->GetState() == EIGLiningState::Stepping || Body->GetState() == EIGLiningState::Bracing, TEXT("travelling_to_large"));
			Body->UnsealGap(TEXT("Small"));
			Check(Small->IsGapOpen(), TEXT("small_gap_reopened"));
		}
		if (Sealed.Contains(TEXT("Large")))
		{
			Check(Targets.Num() == 2, TEXT("no_redirect_mid_travel"));
			Check(Large->IsSealed(), TEXT("large_gap_sealed"));
			Next(6);
		}
		else if (PhaseSeconds > 45.0f)
		{
			Check(false, TEXT("large_gap_sealed"));
			Finish();
		}
		break;
	case 6:
		// 큰 쪽을 다 막은 뒤에야 다시 연 작은 쪽으로 간다.
		if (Targets.Num() >= 3 && Targets.Last() == TEXT("Small"))
		{
			Check(true, TEXT("returns_to_reopened_gap_after_sealing"));
			Next(7);
		}
		else if (PhaseSeconds > 4.0f)
		{
			Check(false, TEXT("returns_to_reopened_gap_after_sealing"));
			Next(7);
		}
		break;
	case 7:
		if (Sealed.Num() >= 3)
		{
			// 떠난 큰 쪽 덮개는 이제 닫힌다(막힌 채).
			FIGInteractionContext Context;
			Context.TargetActor = Large;
			Check(!Large->IsObstructed(), TEXT("left_hatch_unobstructed"));
			Check(Large->IsSealed(), TEXT("left_gap_stays_sealed"));
			Next(8);
		}
		else if (PhaseSeconds > 45.0f)
		{
			Check(false, TEXT("second_small_seal"));
			Finish();
		}
		break;
	case 8:
		if (PhaseSeconds >= 1.0f)
		{
			Check(!bGripped, TEXT("bystander_at_door_not_gripped"));
			Shoot(TEXT("lining_end"));
			Next(9);
		}
		break;
	case 9:
		if (PhaseSeconds >= 0.5f)
		{
			Finish();
		}
		break;
	default:
		break;
	}
}
