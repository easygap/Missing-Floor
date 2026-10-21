#include "Sequence/IGPlacementProbe.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "IndieGame.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace IGPlacementProbe
{
	/** 구역 개관. 가운데를 네 방향에서 찍는다. 벽에 막히면 벽 앞까지 다가온다. */
	struct FStation
	{
		const TCHAR* Name;
		FVector Target;
		float Distance;
	};

	const FStation Stations[] = {
		{ TEXT("403-kitchen"), FVector(150.0f, -40.0f, 960.0f), 260.0f },
		{ TEXT("403-room"), FVector(-60.0f, 60.0f, 960.0f), 300.0f },
		{ TEXT("302-room"), FVector(-20.0f, -65.0f, 660.0f), 260.0f },
		{ TEXT("roof-route"), FVector(130.0f, 300.0f, AIGPrologueWorldScene::RoofZ + 90.0f), 420.0f },
	};
	/** 한 상태에서 걸린 물건을 찍는 최대 개수. 관통을 먼저, 깊은 것부터 찍는다. */
	constexpr int32 MaximumFlagShots = 48;

	/** 고친 자리를 다시 보는 고정 촬영. 그 상태에서 그 메시가 있는 부품 가운데 Near에서 가장 가까운 것을 찍는다. */
	struct FFocus
	{
		const TCHAR* Label;
		const TCHAR* Mesh;
		FVector Near;
	};

	const FFocus FocusShots[] = {
		// 옥상 물탱크 받침 귀퉁이의 꽁초, 편의점 직원문 철물, 5층 공구 카트, 골목 상자, 403호 도착 상자, 공동현관 밀대.
		{ TEXT("day"), TEXT("SM_WitnessCigaretteButts"), FVector(70.0f, -14.0f, 1221.0f) },
		{ TEXT("day"), TEXT("SM_UnitDoorHardware"), FVector(2952.0f, -60.0f, 119.0f) },
		{ TEXT("day"), TEXT("SM_TunerToolCart"), FVector(-280.0f, 865.0f, 1237.0f) },
		{ TEXT("day"), TEXT("cardboard_box_01_1k"), FVector(1586.0f, -1444.0f, 9.0f) },
		{ TEXT("day"), TEXT("cardboard_box_01_1k"), FVector(108.0f, 62.0f, 913.0f) },
		{ TEXT("day"), TEXT("Cube"), FVector(646.0f, -378.5f, 95.9f) },
		// 1층 게시판 옆 수거 안내. 찾는 기준점은 종이 앞 복도 쪽이다(가장 가까운 인쇄물을 고른다).
		{ TEXT("n1"), TEXT("SM_CollectionNoticePrint"), FVector(568.0f, -160.0f, 140.0f) },
		// 302호 짐 더미(둘째 밤): 남서 구석 더미와 그 북쪽 오빠 상자, 문짝 옆 마대.
		{ TEXT("n2"), TEXT("SM_StorageBox"), FVector(-139.8f, -189.0f, 617.0f) },
		{ TEXT("n2"), TEXT("SM_StorageBox"), FVector(-139.4f, -137.5f, 617.0f) },
		{ TEXT("n2"), TEXT("SM_WasteSack"), FVector(-102.0f, -189.0f, 638.0f) },
		// 셋째 밤: 1층 필로티 짐 줄, 302호 마대와 조립대 상자.
		{ TEXT("n3"), TEXT("SM_WasteSack"), FVector(-142.0f, -268.0f, 38.0f) },
		{ TEXT("n3"), TEXT("SM_StorageBox"), FVector(-20.0f, -268.0f, 17.0f) },
		{ TEXT("n3"), TEXT("SM_WasteSack"), FVector(100.0f, -100.0f, 638.0f) },
		{ TEXT("n3"), TEXT("SM_StorageBox"), FVector(53.0f, -185.0f, 617.0f) },
	};
}

AIGPlacementProbe::AIGPlacementProbe()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AIGPlacementProbe::Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer)
{
	Scene = InScene;
	Player = InPlayer;
	bShots = FParse::Param(FCommandLine::Get(), TEXT("IGPlacementShots"));
	for (TActorIterator<AIGNightPhaseDirector> It(GetWorld()); It; ++It)
	{
		NightPhase = *It;
	}
	UE_LOG(LogIndieGame, Display, TEXT("PLACEMENT_PROBE start shots=%d nightPhase=%d"), bShots ? 1 : 0,
		NightPhase.IsValid() ? 1 : 0);
}

void AIGPlacementProbe::Silence()
{
	UWorld* World = GetWorld();
	// 손님·어둑시니·위층 사람은 재운다. 소품이 놓인 모습만 본다.
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

bool AIGPlacementProbe::BeginNight(const int32 Night)
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
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr)
	{
		Narrative->SetNightIndex(Night - 1);
	}
	NightPhase->BeginTheHour(Night);
	Silence();
	return NightPhase->IsHourActive();
}

void AIGPlacementProbe::Audit(const TCHAR* Label)
{
	IGPlacementAudit::FOptions Options;
	Options.Label = Label;
	if (Player.IsValid())
	{
		Options.Ignore.Add(Player.Get());
	}
	// 촬영용 카메라와 그 작업등은 무대가 아니다.
	if (Camera)
	{
		Options.Ignore.Add(Camera.Get());
	}
	const IGPlacementAudit::FResult Result = IGPlacementAudit::Run(GetWorld(), Options);
	++Audits;
	bMeasured &= Result.bMeasured;
	Floating += Result.Floating;
	Penetrations += Result.Penetrations;
	LightFlags += Result.LightsWithoutFixture + Result.LightsBuried;
	// 낮에는 구역 개관을 찍는다. 걸린 물건은 처음 걸린 상태에서 한 번만 찍는다(밤에 새로 놓인 것은 밤에).
	if (bShots)
	{
		const bool bDay = FCString::Strcmp(Label, TEXT("day")) == 0;
		const int32 First = Shots.Num();
		if (bDay)
		{
			QueueStationShots(Label);
		}
		else if (FCString::Strcmp(Label, TEXT("n3")) == 0)
		{
			// 셋째 밤의 302호는 낮과 소품이 다르다.
			QueueStationShots(Label, TEXT("302-room"));
		}
		QueueFlagShots(Label, Result);
		// 개관은 그 상태의 조명 그대로, 걸린 물건과 고정 촬영은 낮이든 밤이든 작업등을 켜고 찍는다(입주 저녁도 어둡다).
		for (int32 Index = First; Index < Shots.Num(); ++Index)
		{
			Shots[Index].bFill = !bDay || !Shots[Index].Name.Contains(TEXT("_station_"));
		}
	}
}

float AIGPlacementProbe::ViewReach(const FVector& Target, const float Yaw, const float Pitch, const float Distance,
	const float StartOffset, FString* OutBlocker) const
{
	const FVector Back = -FRotator(Pitch, Yaw, 0.0f).Vector();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PlacementProbeView), false);
	if (Player.IsValid())
	{
		Query.AddIgnoredActor(Player.Get());
	}
	// 목표는 물건 속(관통이면 두 메시 속)이다. 물건 바깥에서 출발해야 제 몸에 막히지 않는다.
	const FVector Start = Target + Back * StartOffset;
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, Target + Back * Distance, ECC_Visibility, Query))
		{
			return Distance;
		}
		// 보이지 않는 조준 상자와 충돌 부피는 화면을 가리지 않는다.
		const UPrimitiveComponent* Component = Hit.GetComponent();
		if (Component && (!Component->IsVisible() || Component->bHiddenInGame) && !Hit.bStartPenetrating)
		{
			Query.AddIgnoredComponent(Component);
			continue;
		}
		if (OutBlocker)
		{
			*OutBlocker = FString::Printf(TEXT("%s/%s@%.1f%s"), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("none"),
				Component ? *Component->GetName() : TEXT("none"), StartOffset + Hit.Distance,
				Hit.bStartPenetrating ? TEXT("(start)") : TEXT(""));
		}
		return Hit.bStartPenetrating ? -1.0f : StartOffset + Hit.Distance - 15.0f;
	}
	return -1.0f;
}

bool AIGPlacementProbe::QueueView(const FString& Name, const FVector& Target, const float Yaw, const float Pitch,
	const float Distance, const float StartOffset)
{
	const float Reach = ViewReach(Target, Yaw, Pitch, Distance, StartOffset);
	if (Reach < FMath::Max(30.0f, StartOffset + 10.0f))
	{
		return false;
	}
	const FRotator Rotation(Pitch, Yaw, 0.0f);
	FShot& Shot = Shots.AddDefaulted_GetRef();
	Shot.Location = Target - Rotation.Vector() * Reach;
	Shot.Rotation = Rotation;
	Shot.Name = Name;
	return true;
}

void AIGPlacementProbe::QueueStationShots(const FString& Label, const TCHAR* Only)
{
	for (const IGPlacementProbe::FStation& Station : IGPlacementProbe::Stations)
	{
		if (Only && FCString::Strcmp(Only, Station.Name) != 0)
		{
			continue;
		}
		for (const float Yaw : { 45.0f, 135.0f, 225.0f, 315.0f })
		{
			QueueView(FString::Printf(TEXT("%s_station_%s_%03d"), *Label, Station.Name, FMath::RoundToInt(Yaw)),
				Station.Target, Yaw, -18.0f, Station.Distance);
		}
	}
}

void AIGPlacementProbe::QueueFlagShots(const FString& Label, const IGPlacementAudit::FFlag& Flag, const int32 Index)
{
	const float Size = static_cast<float>(Flag.Box.GetSize().GetMax());
	const float Distance = FMath::Clamp(Size * 1.6f + 60.0f, 90.0f, 240.0f);
	const float StartOffset = FMath::Min(60.0f, static_cast<float>(Flag.Box.GetExtent().Size()) + 3.0f);
	// 여덟 방향과 두 높이에서 가장 멀리 물러설 수 있는 두 시점을 고른다. 벽에 붙은 물건은 벽 쪽 시점이 막힌다.
	struct FCandidate
	{
		float Yaw;
		float Pitch;
		float Reach;
	};
	TArray<FCandidate> Candidates;
	for (int32 Step = 0; Step < 8; ++Step)
	{
		for (const float Pitch : { -25.0f, -55.0f })
		{
			const float Yaw = 35.0f + 45.0f * Step;
			Candidates.Add({ Yaw, Pitch, ViewReach(Flag.Box.GetCenter(), Yaw, Pitch, Distance, StartOffset) });
		}
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Reach > B.Reach; });
	FString Short = Flag.Name;
	Short.ReplaceInline(TEXT("/"), TEXT("-"));
	TArray<float, TInlineAllocator<2>> Taken;
	for (const FCandidate& Candidate : Candidates)
	{
		if (Taken.Num() >= 2 || Candidate.Reach < FMath::Max(30.0f, StartOffset + 10.0f))
		{
			break;
		}
		// 두 번째 시점은 첫 시점과 90도 넘게 돌아간 쪽에서.
		if (!Taken.IsEmpty() && FMath::Abs(FMath::FindDeltaAngleDegrees(Taken[0], Candidate.Yaw)) < 90.0f)
		{
			continue;
		}
		if (QueueView(FString::Printf(TEXT("%s_%s_%02d_%s_%03d"), *Label, *Flag.Kind.ToString(), Index, *Short,
			FMath::RoundToInt(Candidate.Yaw)), Flag.Box.GetCenter(), Candidate.Yaw, Candidate.Pitch, Distance, StartOffset))
		{
			Taken.Add(Candidate.Yaw);
		}
	}
	// 사방이 막힌 좁은 자리면 가장 트인 쪽에서 바짝 붙어 한 장만 찍는다.
	if (Taken.IsEmpty() && Candidates[0].Reach >= StartOffset + 5.0f)
	{
		const float Close = Candidates[0].Reach;
		const FRotator Rotation(Candidates[0].Pitch, Candidates[0].Yaw, 0.0f);
		FShot& Shot = Shots.AddDefaulted_GetRef();
		Shot.Location = Flag.Box.GetCenter() - Rotation.Vector() * Close;
		Shot.Rotation = Rotation;
		Shot.Name = FString::Printf(TEXT("%s_%s_%02d_%s_%03d_close"), *Label, *Flag.Kind.ToString(), Index, *Short,
			FMath::RoundToInt(Candidates[0].Yaw));
		Taken.Add(Candidates[0].Yaw);
	}
	if (Taken.IsEmpty())
	{
		FString Blocker;
		ViewReach(Flag.Box.GetCenter(), Candidates[0].Yaw, Candidates[0].Pitch, Distance, StartOffset, &Blocker);
		UE_LOG(LogIndieGame, Display, TEXT("PLACEMENT_SHOT_SKIPPED name=%s reason=no_clear_view best=%.1f blocker=%s"),
			*Flag.Name, Candidates[0].Reach, *Blocker);
	}
}

void AIGPlacementProbe::QueueFlagShots(const FString& Label, const IGPlacementAudit::FResult& Result)
{
	TArray<const IGPlacementAudit::FFlag*> Picked;
	for (const IGPlacementAudit::FFlag& Flag : Result.Flags)
	{
		if (Flag.Kind == FName(TEXT("penetration")) || Flag.Kind == FName(TEXT("floating")))
		{
			Picked.Add(&Flag);
		}
	}
	Picked.Sort([](const IGPlacementAudit::FFlag& A, const IGPlacementAudit::FFlag& B)
	{
		const bool bAPen = A.Kind == FName(TEXT("penetration"));
		const bool bBPen = B.Kind == FName(TEXT("penetration"));
		return bAPen != bBPen ? bAPen : FMath::Abs(A.Cm) > FMath::Abs(B.Cm);
	});
	int32 Count = 0;
	for (const IGPlacementAudit::FFlag* Flag : Picked)
	{
		if (Count >= IGPlacementProbe::MaximumFlagShots)
		{
			break;
		}
		// 한 물건은 한 번만 찍는다. 앞선 상태(낮)에 찍었으면 밤에 다시 찍지 않는다.
		if (ShotKeys.Contains(Flag->Name))
		{
			continue;
		}
		ShotKeys.Add(Flag->Name);
		QueueFlagShots(Label, *Flag, Count++);
	}
	// 고친 자리는 걸리지 않아도 다시 찍는다. 메시 이름이 같은 부품 가운데 지정한 자리에서 가장 가까운 것.
	int32 FocusIndex = 0;
	for (const IGPlacementProbe::FFocus& Focus : IGPlacementProbe::FocusShots)
	{
		if (FCString::Strcmp(Focus.Label, *Label) != 0)
		{
			continue;
		}
		const UStaticMeshComponent* Best = nullptr;
		double BestDistance = 80.0;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components(*It);
			for (const UStaticMeshComponent* Component : Components)
			{
				if (!Component || !Component->IsVisible() || !Component->GetStaticMesh()
					|| Component->GetStaticMesh()->GetName() != Focus.Mesh)
				{
					continue;
				}
				const double Distance = FVector::Dist(Component->Bounds.Origin, Focus.Near);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = Component;
				}
			}
		}
		if (!Best)
		{
			UE_LOG(LogIndieGame, Display, TEXT("PLACEMENT_FOCUS_MISSING label=%s mesh=%s near=(%.0f,%.0f,%.0f)"),
				*Label, Focus.Mesh, Focus.Near.X, Focus.Near.Y, Focus.Near.Z);
			continue;
		}
		IGPlacementAudit::FFlag Flag;
		Flag.Kind = FName(TEXT("focus"));
		Flag.Name = FString::Printf(TEXT("%s/%s"), *Best->GetOwner()->GetName(), *Best->GetName());
		Flag.Box = Best->Bounds.GetBox();
		QueueFlagShots(Label, Flag, FocusIndex++);
	}
}

bool AIGPlacementProbe::TickShots(const float DeltaSeconds)
{
	if (ShotIndex >= Shots.Num())
	{
		return true;
	}
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return true;
	}
	if (!Camera)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Camera = GetWorld()->SpawnActor<ACameraActor>(Parameters);
		Camera->GetCameraComponent()->SetFieldOfView(70.0f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		// 밤 장면은 불이 꺼져 있다. 카메라에 붙인 작업등으로 소품만 보이게 한다(그림자는 카메라 뒤로 숨는다).
		Fill = NewObject<UPointLightComponent>(Camera);
		Fill->SetupAttachment(Camera->GetCameraComponent());
		Fill->SetIntensityUnits(ELightUnits::Candelas);
		Fill->SetIntensity(60.0f);
		Fill->SetAttenuationRadius(900.0f);
		Fill->SetCastShadows(false);
		Fill->SetRelativeLocation(FVector(-10.0f, 0.0f, 25.0f));
		Fill->RegisterComponent();
	}
	if (ShotWait < 0.0f)
	{
		Camera->SetActorLocationAndRotation(Shots[ShotIndex].Location, Shots[ShotIndex].Rotation);
		Fill->SetVisibility(Shots[ShotIndex].bFill);
		PC->SetViewTarget(Camera);
		if (PC->MyHUD)
		{
			PC->MyHUD->bShowHUD = false;
		}
		ShotWait = 0.45f;
		bShotRequested = false;
		return false;
	}
	ShotWait -= DeltaSeconds;
	if (ShotWait > 0.0f)
	{
		return false;
	}
	if (!bShotRequested)
	{
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		const FString Directory = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlacementReview")));
		IFileManager::Get().MakeDirectory(*Directory, true);
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory,
			FString::Printf(TEXT("%03d_%s.png"), ShotFileIndex++, *Shots[ShotIndex].Name)), false, false);
		UE_LOG(LogIndieGame, Display, TEXT("PLACEMENT_SHOT name=%s at=(%.1f,%.1f,%.1f) rot=(%.1f,%.1f)"),
			*Shots[ShotIndex].Name, Shots[ShotIndex].Location.X, Shots[ShotIndex].Location.Y,
			Shots[ShotIndex].Location.Z, Shots[ShotIndex].Rotation.Pitch, Shots[ShotIndex].Rotation.Yaw);
		bShotRequested = true;
		ShotWait = 0.25f;
		return false;
	}
	++ShotIndex;
	ShotWait = -1.0f;
	if (ShotIndex >= Shots.Num() && Player.IsValid())
	{
		PC->SetViewTarget(Player.Get());
		if (PC->MyHUD)
		{
			PC->MyHUD->bShowHUD = true;
		}
	}
	return ShotIndex >= Shots.Num();
}

void AIGPlacementProbe::Next(const int32 NewPhase)
{
	Phase = NewPhase;
	PhaseSeconds = 0.0f;
}

void AIGPlacementProbe::Finish()
{
	if (Phase < 0)
	{
		return;
	}
	Phase = -1;
	UE_LOG(LogIndieGame, Display,
		TEXT("PLACEMENT_PROBE %s audits=%d measured=%d floating=%d penetrations=%d lightFlags=%d shots=%d"),
		bMeasured && Audits == 5 ? TEXT("DONE") : TEXT("FAIL"), Audits, bMeasured ? 1 : 0, Floating, Penetrations,
		LightFlags, ShotFileIndex);
	// 찾은 것의 판정은 사람이 화면과 수치를 보고 한다. 끝까지 쟀으면 0으로 끝낸다.
	FPlatformMisc::RequestExitWithStatus(false, bMeasured && Audits == 5 ? 0 : 1);
}

void AIGPlacementProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PhaseSeconds += DeltaSeconds;
	switch (Phase)
	{
	case 0:
		// 무대가 다 지어지고 셰이더가 붙을 때까지 기다린다.
		if (PhaseSeconds > 5.0f)
		{
			Silence();
			Audit(TEXT("day"));
			Next(1);
		}
		break;
	case 1:
		if (TickShots(DeltaSeconds))
		{
			Next(2);
		}
		break;
	case 2:
		if (!BeginNight(1))
		{
			UE_LOG(LogIndieGame, Error, TEXT("PLACEMENT_PROBE night1 did not begin"));
		}
		Next(3);
		break;
	case 3:
		if (PhaseSeconds > 3.0f)
		{
			Audit(TEXT("n1"));
			Next(31);
		}
		break;
	case 31:
		if (TickShots(DeltaSeconds))
		{
			BeginNight(2);
			Next(32);
		}
		break;
	case 32:
		// 둘째 밤: 302호에 낮1에 옮겨 온 짐 더미와 오빠 상자가 함께 있는 밤 상태.
		if (PhaseSeconds > 3.0f)
		{
			Audit(TEXT("n2"));
			Next(33);
		}
		break;
	case 33:
		if (TickShots(DeltaSeconds))
		{
			BeginNight(3);
			Next(4);
		}
		break;
	case 4:
		if (PhaseSeconds > 3.0f)
		{
			Audit(TEXT("n3"));
			Next(41);
		}
		break;
	case 41:
		if (TickShots(DeltaSeconds))
		{
			BeginNight(4);
			Next(5);
		}
		break;
	case 5:
		if (PhaseSeconds > 3.0f)
		{
			Audit(TEXT("n4"));
			Next(51);
		}
		break;
	case 51:
		if (TickShots(DeltaSeconds))
		{
			Finish();
		}
		break;
	default:
		break;
	}
}
