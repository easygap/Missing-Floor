// 밤4 벽 해체(0.10.0). 망치를 휘두른 수와 벽을 실제로 뜯은 단계 다섯을 따로 센다.
// 감독 본체는 IGMissingFloorNightFourDirector.cpp다.
//
// 1 덧판 이음매의 테이프를 쳐서 뜯는다. 덧판 나사(바깥 고정점)가 드러나고 들뜬 이음매로 덧댄 것이 온다.
// 2 남쪽 덧판을 떼어 배관 점검구를 연다. 덧댄 것이 그리로 옮겨 가 벽과 천장 사이로 몸을 편다.
//   구석의 철골 틀을 세워 무릎 마디를 걸면 더는 접지 못한다(망치가 아니라 손으로 한다).
// 3 드러난 덧판 나사를 쳐 낸다. 덧판과 각재가 떨어지고 목한수가 차단기를 내린다. 걸린 몸이 버티며 틀을 민다.
// 4 밀린 틀을 다시 세우고 안쪽 판 나사를 친다. 401호의 대답, 철문 밖의 부름, 벽 안의 대답이 차례로 온다.
//   대답이 다 들린 뒤 틀과 덧댄 것이 작업할 자리에서 밀려난다.
// 5 남은 ㄱ자 철물을 쳐 낸다. 판이 내려앉고 공동과 대치로 이어진다.
//
// 고정되지 않은 데를 치면 판이 들썩이거나 겉만 깨진다. 단계는 그대로고, 같은 단계에서 세 번째로
// 빗나가면 H로 물어볼 수 있다는 안내만 뜬다. 확장 전 저장에서 이미 망치질을 시작한 판은 예전 다섯 번 순서다.

#include "Entity/IGMissingFloorNightFourDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/IGDustSubsystem.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorLeftoverDirector.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RotationMatrix.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "TimerManager.h"

namespace IGNightFourWall
{
	using EWallTarget = AIGMissingFloorNightFourDirector::EWallTarget;

	const FName LayerSurvey(TEXT("Survey"));
	const FName LayerBraceSlip(TEXT("BraceSlip"));
	const FName LayerRebrace(TEXT("Rebrace"));
	const FName LayerPushedOut(TEXT("PushedOut"));
	const FName SeenMoveBeat(TEXT("Leftover.SeenMove"));

	/** 시신 벽 칸. 석고 면 X 250, 각재 바깥 Y 642..758, 덧판 이음매 높이 1383. 덧댄 층은 yaw 180으로 붙인다. */
	constexpr float WallFaceX = 250.0f;
	constexpr float OverlayFaceX = 248.75f;
	constexpr float BayMinY = 642.0f;
	constexpr float BayMaxY = 758.0f;
	constexpr float OverlayMinY = 647.0f;
	constexpr float OverlayMaxY = 753.0f;
	constexpr float JointZ = 1383.0f;
	const FRotator BayFacing(0.0f, 180.0f, 0.0f);
	const FVector BayCenter(250.0f, 700.0f, 1320.0f);
	const FVector OverlaySpot(250.0f, 700.0f, JointZ);
	const FVector BattenSpots[2] = {FVector(250.0f, 644.5f, 1320.0f), FVector(250.0f, 755.5f, 1320.0f)};
	const FVector ClipSpot(250.0f, 700.0f, 1440.0f);
	/** 떨어진 덧판은 칸 밑동에 비스듬히 기대고, 각재 둘은 그 앞 바닥에 엇갈려 눕는다. */
	const FVector OverlayFallenBase(226.0f, 700.0f, 1200.6f);
	constexpr float OverlayFallenTilt = 27.0f;
	const FVector BattenFallen[2] = {FVector(204.0f, 668.0f, 1200.2f), FVector(216.0f, 738.0f, 1200.2f)};
	const float BattenFallenYaw[2] = {8.0f, -14.0f};
	constexpr float OuterFallDuration = 0.45f;

	/** 철골 틀(원점 바닥 가운데, 틀 면 YZ). 남쪽 벽에 기대 두었다가 점검구 앞에 세운다. */
	const FVector BraceLeanBase(218.0f, 478.0f, 1200.0f);
	const FRotator BraceLeanRotation(3.2f, 90.0f, 0.0f);
	constexpr float BraceUprightY = 492.0f;
	constexpr float BraceHeight = 238.0f;
	constexpr float BraceSlipDegrees = 7.0f;
	const FVector BracePushedBase(220.0f, 486.0f, 1200.0f);
	const FRotator BracePushedRotation(5.0f, 72.0f, 0.0f);
	enum EBracePose : uint8
	{
		BraceLean,
		BraceUpright,
		BraceSlipped,
		BracePushed
	};

	constexpr int32 DentCount = 6;

	UStaticMesh* Mesh(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(
			nullptr, *FString::Printf(TEXT("/Game/Meshes/%s.%s"), Name, Name), nullptr, LOAD_NoWarn);
	}

	FVector BraceUprightBase()
	{
		return FVector(AIGMissingFloorLeftoverDirector::GetAnnexKneePin().X, BraceUprightY, 1200.0f);
	}

	FTransform BraceTransform(const uint8 Pose)
	{
		switch (Pose)
		{
		case BraceLean:
			return FTransform(BraceLeanRotation, BraceLeanBase);
		case BraceSlipped:
		{
			// 위 런너는 무릎에 걸린 채, 밑동만 방 쪽(-X)으로 밀려났다.
			const FVector Top = BraceUprightBase() + FVector(0.0f, 0.0f, BraceHeight);
			FRotator Tilt(BraceSlipDegrees, 0.0f, 0.0f);
			FVector Down = Tilt.RotateVector(FVector(0.0f, 0.0f, -BraceHeight));
			if (Down.X > 0.0f)
			{
				Tilt.Pitch = -Tilt.Pitch;
				Down = Tilt.RotateVector(FVector(0.0f, 0.0f, -BraceHeight));
			}
			return FTransform(Tilt, Top + Down);
		}
		case BracePushed:
			return FTransform(BracePushedRotation, BracePushedBase);
		default:
			return FTransform(FRotator::ZeroRotator, BraceUprightBase());
		}
	}

	FTransform OverlayOnWall()
	{
		return FTransform(BayFacing, OverlaySpot);
	}

	FTransform OverlayFallen()
	{
		// yaw 180 뒤 피치가 위 변을 벽(+X) 쪽으로 눕힌다.
		return FTransform(FRotator(OverlayFallenTilt, 180.0f, 0.0f), OverlayFallenBase);
	}

	FTransform BattenOnWall(const int32 Index)
	{
		return FTransform(BayFacing, BattenSpots[Index]);
	}

	FTransform BattenOnFloor(const int32 Index)
	{
		// 앞면(+X)이 위를 보고 길이(Z)가 바닥을 따라 눕는다.
		const float Yaw = FMath::DegreesToRadians(BattenFallenYaw[Index]);
		const FVector Along(FMath::Sin(Yaw), FMath::Cos(Yaw), 0.0f);
		return FTransform(FRotationMatrix::MakeFromXZ(FVector::UpVector, Along).Rotator(), BattenFallen[Index]);
	}

	/** 방금 친 자리나 버팀에 대한 짧은 반응. 밀려서 늦게 뜨면 지난 일이라 버린다. */
	void Think(UObject* Context, const FText& Line, const float Seconds = 2.8f)
	{
		FIGDialogueScope Scope;
		Scope.Context = FName(TEXT("Night4.WallReaction"));
		Scope.Owner = Context;
		Scope.Lifetime = EIGDialogueLifetime::Reaction;
		AIGHorrorHUD::PushThoughtScoped(Context, Line, Seconds, MoveTemp(Scope));
	}

	/**
	 * 벽을 뜯으며 알게 된 것(어디를 고정했는지, 무엇이 나왔는지, 덧댄 것이 어떻게 됐는지). 밸브 반응이나
	 * 물소리 속말과 겹쳐 줄이 차도 버리지 않는다. 화면 렌더 검사에서 「테이프 밑에 나사가 박혀 있어」가
	 * 줄이 찼다고 버려지고, 「덧판 밑이 뚫려 있었어」는 15초 밀려 버려졌다.
	 */
	void ThinkClue(UObject* Context, const FText& Line, const float Seconds = 2.8f)
	{
		FIGDialogueScope Scope;
		Scope.Context = FName(TEXT("Night4.Wall"));
		Scope.Owner = Context;
		AIGHorrorHUD::PushThoughtScoped(Context, Line, Seconds, MoveTemp(Scope));
	}

	void ShowProp(UStaticMeshComponent* Component, const bool bShow)
	{
		if (Component)
		{
			Component->SetVisibility(bShow, true);
			Component->SetHiddenInGame(!bShow, true);
		}
	}

	// -- 글 -------------------------------------------------------------------
	FText MissLifted() { return NSLOCTEXT("IGMissingFloor", "WallMissLifted", "판만 들썩였어."); }
	FText MissSurface() { return NSLOCTEXT("IGMissingFloor", "WallMissSurface", "겉만 깨졌어."); }
	FText BlockedHeld() { return NSLOCTEXT("IGMissingFloor", "WallBlockedHeld", "그게 누르고 있어서 판이 안 떨어져."); }
	FText BlockedNoBrace() { return NSLOCTEXT("IGMissingFloor", "WallBlockedNoBrace", "칠 때마다 다시 접혀서 벽으로 와."); }
	FText BlockedSlipping() { return NSLOCTEXT("IGMissingFloor", "WallBlockedSlipping", "틀이 밀렸어."); }
	FText BlockedInnerFirst() { return NSLOCTEXT("IGMissingFloor", "WallBlockedInnerFirst", "안쪽 판이 아직 기둥에 박혀 있어."); }
	FText BlockedMoving() { return NSLOCTEXT("IGMissingFloor", "WallBlockedMoving", "아직 움직여."); }
}

using namespace IGNightFourWall;

// -- 조회 ------------------------------------------------------------------------

AIGMissingFloorLeftoverDirector* AIGMissingFloorNightFourDirector::GetLeftover() const
{
	if (!CachedLeftover.IsValid())
	{
		for (TActorIterator<AIGMissingFloorLeftoverDirector> It(GetWorld()); It; ++It)
		{
			CachedLeftover = *It;
			break;
		}
	}
	return CachedLeftover.Get();
}

bool AIGMissingFloorNightFourDirector::IsExpansionWall() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative && !Narrative->GetLeftover().bLegacyWallSequence;
}

AIGMissingFloorEvidence* AIGMissingFloorNightFourDirector::GetWallTarget(const EWallTarget Target) const
{
	const int32 Index = static_cast<int32>(Target);
	return WallTargets.IsValidIndex(Index) ? WallTargets[Index].Get() : nullptr;
}

bool AIGMissingFloorNightFourDirector::HasWallLayer(const FName Layer) const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative && Narrative->HasLeftover(EIGLeftoverList::WallLayers, Layer);
}

void AIGMissingFloorNightFourDirector::AddWallLayer(const FName Layer)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->AddLeftover(EIGLeftoverList::WallLayers, Layer);
	}
}

AIGMissingFloorEvidence* AIGMissingFloorNightFourDirector::GetNextWallActionForTesting() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || Narrative->IsNightFourWallOpened() || bWallOpeningPending)
	{
		return nullptr;
	}
	if (!IsExpansionWall())
	{
		return WallBreakTarget && WallBreakTarget->IsInteractionEnabled() ? WallBreakTarget.Get() : nullptr;
	}
	const FIGLeftoverState& State = Narrative->GetLeftover();
	const AIGMissingFloorLeftoverDirector* Leftover = GetLeftover();
	AIGMissingFloorEvidence* Next = nullptr;
	switch (State.WallStage)
	{
	case 0:
		Next = GetWallTarget(HasWallLayer(LayerSurvey) ? EWallTarget::Joint : EWallTarget::Survey);
		break;
	case 1:
		Next = GetWallTarget(EWallTarget::Overlay);
		break;
	case 2:
		if (State.bFramePlaced)
		{
			Next = GetWallTarget(EWallTarget::OuterScrews);
		}
		else if (IsImmediateWallRun() || !Leftover || Leftover->IsAnnexSpread())
		{
			Next = GetWallTarget(EWallTarget::Brace);
		}
		break;
	case 3:
		if (HasWallLayer(LayerRebrace))
		{
			Next = GetWallTarget(EWallTarget::InnerScrewsSouth);
		}
		else if (HasWallLayer(LayerBraceSlip))
		{
			Next = GetWallTarget(EWallTarget::Rebrace);
		}
		break;
	case 4:
		if (HasWallLayer(LayerPushedOut))
		{
			Next = GetWallTarget(EWallTarget::LastClip);
		}
		break;
	default:
		break;
	}
	return Next && Next->IsInteractionEnabled() ? Next : nullptr;
}

void AIGMissingFloorNightFourDirector::RestoreWallForTesting()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (AIGMissingFloorLeftoverDirector* Leftover = Narrative ? GetLeftover() : nullptr)
	{
		Leftover->PrepareAnnexLining(Narrative->GetLeftover().WallStage, Narrative->GetLeftover().bFramePlaced);
	}
	OuterFallSeconds = -1.0f;
	BraceDuration = 0.0f;
	RefreshPresentation();
}

// -- 세우기 ----------------------------------------------------------------------

void AIGMissingFloorNightFourDirector::BuildWallStage(UStaticMesh* CubeMesh)
{
	UWorld* World = GetWorld();
	if (!World || !CubeMesh)
	{
		return;
	}
	// 손대는 자리. 보이는 것은 위의 소품이 맡고 이 상자들은 조준만 받는다. 벽 전체 표적(X 246)보다
	// 앞(X 243 남짓)에 세워, 그 자리를 겨누면 이쪽이 먼저 걸린다.
	struct FTargetSpec
	{
		const TCHAR* Name;
		FVector Location;
		FVector Size;
		FText Prompt;
		float HoldSeconds;
		float Noise;
	};
	const FTargetSpec Specs[] = {
		{TEXT("NightFourWallSurvey"), FVector(242.5f, 700.0f, 1320.0f), FVector(2.0f, 116.0f, 236.0f),
			NSLOCTEXT("IGMissingFloor", "WallSurveyPrompt", "살펴보기"), 0.0f, 0.0f},
		{TEXT("NightFourWallJoint"), FVector(243.5f, 700.0f, JointZ), FVector(3.0f, 106.0f, 16.0f),
			NSLOCTEXT("IGMissingFloor", "WallJointPrompt", "이음매 내려치기"), 0.8f, 1.0f},
		{TEXT("NightFourWallOverlay"), FVector(246.0f, 492.0f, 1265.0f), FVector(3.0f, 64.0f, 90.0f),
			NSLOCTEXT("IGMissingFloor", "WallOverlayPrompt", "떼어 내기"), 0.8f, 1.0f},
		{TEXT("NightFourWallBrace"), FVector(218.0f, 472.0f, 1310.0f), FVector(66.0f, 14.0f, 200.0f),
			NSLOCTEXT("IGMissingFloor", "WallBracePrompt", "틀 세우기"), 0.6f, 0.35f},
		{TEXT("NightFourWallOuterScrews"), FVector(243.5f, 700.0f, JointZ + 2.0f), FVector(3.0f, 106.0f, 12.0f),
			NSLOCTEXT("IGMissingFloor", "WallOuterScrewsPrompt", "나사 쳐 내기"), 0.8f, 1.0f},
		{TEXT("NightFourWallRebrace"), FVector(190.0f, BraceUprightY, 1255.0f), FVector(20.0f, 64.0f, 110.0f),
			NSLOCTEXT("IGMissingFloor", "WallRebracePrompt", "다시 세우기"), 0.6f, 0.3f},
		{TEXT("NightFourWallInnerSouth"), FVector(243.5f, 644.5f, 1300.0f), FVector(3.0f, 14.0f, 210.0f),
			NSLOCTEXT("IGMissingFloor", "WallInnerScrewsPrompt", "나사 쳐 내기"), 0.8f, 1.0f},
		{TEXT("NightFourWallInnerNorth"), FVector(243.5f, 755.5f, 1300.0f), FVector(3.0f, 14.0f, 210.0f),
			NSLOCTEXT("IGMissingFloor", "WallInnerScrewsPrompt", "나사 쳐 내기"), 0.8f, 1.0f},
		{TEXT("NightFourWallLastClip"), FVector(244.0f, 700.0f, 1430.0f), FVector(4.0f, 24.0f, 20.0f),
			NSLOCTEXT("IGMissingFloor", "WallLastClipPrompt", "철물 쳐 내기"), 0.8f, 1.0f},
	};
	static_assert(UE_ARRAY_COUNT(Specs) == static_cast<int32>(EWallTarget::Count), "벽 표적 표와 EWallTarget 순서가 같아야 한다");
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	WallTargets.Reset();
	for (const FTargetSpec& Spec : Specs)
	{
		Parameters.Name = FName(Spec.Name);
		AIGMissingFloorEvidence* Target = World->SpawnActor<AIGMissingFloorEvidence>(
			AIGMissingFloorEvidence::StaticClass(), FTransform(Spec.Location), Parameters);
		if (!Target)
		{
			WallTargets.Add(nullptr);
			continue;
		}
		Target->Configure(CubeMesh, nullptr, Spec.Size, Spec.Prompt, FText::GetEmpty(),
			EIGMissingFloorTruth::None, EIGMissingFloorSource::None, Spec.HoldSeconds, Spec.Noise, false);
		Target->OnExamined.AddUObject(this, &AIGMissingFloorNightFourDirector::HandleWallTarget);
		Target->SetInteractionEnabled(false);
		Target->SetActorHiddenInGame(true);
		WallTargets.Add(Target);
	}

	// 보이는 층. 이동하는 것이라 Movable이고 충돌은 없다.
	const auto AddProp = [this, World](const TCHAR* MeshName, const FName Name, const FTransform& Transform) -> UStaticMeshComponent*
	{
		UStaticMesh* PropMesh = IGNightFourWall::Mesh(MeshName);
		if (!PropMesh)
		{
			return nullptr;
		}
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, Name);
		Component->SetStaticMesh(PropMesh);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetWorldTransform(Transform);
		AddInstanceComponent(Component);
		Component->RegisterComponentWithWorld(World);
		return Component;
	};
	WallOverlayTop = AddProp(TEXT("SM_AnnexOverlayTop"), TEXT("NightFourWallOverlayTop"), OverlayOnWall());
	WallJointTape = AddProp(TEXT("SM_AnnexJointTape"), TEXT("NightFourWallJointTape"), FTransform(BayFacing, OverlaySpot));
	WallJointTorn = AddProp(TEXT("SM_AnnexJointTorn"), TEXT("NightFourWallJointTorn"), FTransform(BayFacing, OverlaySpot));
	WallBattens.Reset();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		WallBattens.Add(AddProp(TEXT("SM_AnnexBatten"), FName(*FString::Printf(TEXT("NightFourWallBatten%d"), Index)),
			BattenOnWall(Index)));
	}
	WallInnerFixings = AddProp(TEXT("SM_AnnexInnerFixings"), TEXT("NightFourWallInnerFixings"), FTransform(BayFacing, BayCenter));
	WallAngleClip = AddProp(TEXT("SM_AnnexAngleClip"), TEXT("NightFourWallAngleClip"), FTransform(BayFacing, ClipSpot));
	BraceFrame = AddProp(TEXT("SM_AnnexBraceFrame"), TEXT("NightFourBraceFrame"), BraceTransform(BraceLean));
	BracePose = BraceLean;
	HammerDents.Reset();
	for (int32 Index = 0; Index < DentCount; ++Index)
	{
		if (UStaticMeshComponent* Dent = AddProp(TEXT("SM_HammerDent"), FName(*FString::Printf(TEXT("NightFourHammerDent%d"), Index)),
			FTransform(BayFacing, BayCenter)))
		{
			Dent->SetCastShadow(false);
			ShowProp(Dent, false);
			HammerDents.Add(Dent);
		}
	}
}

// -- 손댄 자리 ---------------------------------------------------------------------

void AIGMissingFloorNightFourDirector::HandleWallTarget(AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const int32 Index = WallTargets.IndexOfByKey(Evidence);
	if (!Narrative || Index == INDEX_NONE || !IsExpansionWall() || !Narrative->IsNightFourMaskRunning()
		|| Narrative->IsNightFourWallOpened() || bWallOpeningPending || bFailureEndingActive)
	{
		return;
	}
	AIGMissingFloorLeftoverDirector* Leftover = GetLeftover();
	FIGLeftoverState& State = Narrative->MutableLeftover();
	const int32 Stage = State.WallStage;
	const FVector Point = Evidence->GetLastInteractionPoint();
	switch (static_cast<EWallTarget>(Index))
	{
	case EWallTarget::Survey:
		if (Stage == 0 && !HasWallLayer(LayerSurvey))
		{
			AddWallLayer(LayerSurvey);
			// P6에서 덮개를 제자리에 대 봤으면 남쪽 덧판 밑을 이미 안다. 몰라도 여기서 보이는 것만으로 풀린다.
			ThinkClue(this, State.bSiteMatched
				? NSLOCTEXT("IGMissingFloor", "WallSurveyKnown", "가운데 칸에만 판을 한 겹 더 댔어. 구석 쪽 작은 덧판 밑은 점검구였고.")
				: NSLOCTEXT("IGMissingFloor", "WallSurvey", "가운데 칸에만 판을 한 겹 더 댔어. 이음매에 테이프를 발랐네."),
				3.6f);
			GetWorldTimerManager().SetTimer(WallThoughtTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ThinkClue(this, NSLOCTEXT("IGMissingFloor", "WallSurveyFixings", "덧판을 각재에 대고 박아 놨어."), 3.4f);
			}), 3.8f, false);
		}
		break;
	case EWallTarget::Joint:
		if (Stage == 0)
		{
			AdvanceWallStage(1, Point);
		}
		break;
	case EWallTarget::Overlay:
		if (Stage == 1)
		{
			AdvanceWallStage(2, Point);
		}
		else
		{
			HandleWallMiss(Point, FText::GetEmpty());
		}
		break;
	case EWallTarget::Brace:
		if (Stage != 2 || State.bFramePlaced || GetWorldTimerManager().IsTimerActive(BracePinTimer))
		{
			break;
		}
		if (!IsImmediateWallRun() && Leftover && !Leftover->IsAnnexSpread())
		{
			Think(this, NSLOCTEXT("IGMissingFloor", "WallBraceNotYet", "아직 움직여."));
			break;
		}
		PlaceBraceFrame(BraceUpright, IsImmediateWallRun() ? 0.0f : 0.7f);
		{
			const auto Pin = [this]()
			{
				if (AIGMissingFloorLeftoverDirector* PinLeftover = GetLeftover())
				{
					PinLeftover->PinAnnexKnee();
				}
				else if (UIGMissingFloorNarrativeSubsystem* PinNarrative = GetNarrative())
				{
					PinNarrative->MutableLeftover().bFramePlaced = true;
				}
				IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateKickedPropKnock(this, true),
					AIGMissingFloorLeftoverDirector::GetAnnexKneePin(), 0.8f, 0.8f, 150.0f, 1600.0f, EIGAudioBus::World);
				Think(this, NSLOCTEXT("IGMissingFloor", "WallBraced", "걸렸어. 이제 못 접어."));
				RefreshPresentation();
			};
			if (IsImmediateWallRun())
			{
				Pin();
			}
			else
			{
				GetWorldTimerManager().SetTimer(BracePinTimer, FTimerDelegate::CreateWeakLambda(this, Pin), 0.65f, false);
			}
		}
		break;
	case EWallTarget::OuterScrews:
		if (Stage == 2 && State.bFramePlaced)
		{
			AdvanceWallStage(3, Point);
		}
		else
		{
			HandleWallMiss(Point, Stage <= 1 ? BlockedHeld() : BlockedNoBrace());
		}
		break;
	case EWallTarget::Rebrace:
		if (Stage == 3 && HasWallLayer(LayerBraceSlip) && !HasWallLayer(LayerRebrace))
		{
			AddWallLayer(LayerRebrace);
			PlaceBraceFrame(BraceUpright, IsImmediateWallRun() ? 0.0f : 0.5f);
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateKickedPropKnock(this, true),
				BraceUprightBase(), 0.7f, 0.85f, 150.0f, 1600.0f, EIGAudioBus::World);
			Think(this, NSLOCTEXT("IGMissingFloor", "WallRebraced", "다시 물렸어."));
		}
		break;
	case EWallTarget::InnerScrewsSouth:
	case EWallTarget::InnerScrewsNorth:
		if (Stage == 3 && HasWallLayer(LayerRebrace))
		{
			AdvanceWallStage(4, Point);
		}
		else
		{
			HandleWallMiss(Point, Stage == 3 ? BlockedSlipping() : FText::GetEmpty());
		}
		break;
	case EWallTarget::LastClip:
		if (Stage == 4 && HasWallLayer(LayerPushedOut))
		{
			AdvanceWallStage(5, Point);
		}
		else
		{
			HandleWallMiss(Point, Stage <= 3 ? BlockedInnerFirst() : BlockedMoving());
		}
		break;
	default:
		break;
	}
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::HandleWallMiss(const FVector& Point, const FText& Reason)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	if (Narrative->RecordNightFourWallStrike() == 1)
	{
		StartWallRecording();
	}
	const int32 Stage = Narrative->GetLeftover().WallStage;
	if (WallMissStage != Stage)
	{
		WallMissStage = Stage;
		WallMissCount = 0;
	}
	++WallMissCount;
	// 박히지 않은 판은 망치를 받고 들썩인다. 둔한 소리와 판 떠는 소리, 손에 오는 반동은 작다.
	PlayWallStrikeSound(0, Point, 0.8f);
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateKickedPropKnock(this, false),
		Point, 0.5f, 0.9f, 140.0f, 1200.0f, EIGAudioBus::World);
	if (AIGPlayerCharacter* Striker = Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		Striker->PlayScareKick(0.22f);
	}
	PlaceHammerDent(Point);
	FText Line = Reason;
	if (Line.IsEmpty())
	{
		// 덧댄 층(각재, 덧판)을 치면 판이 들썩이고, 석고 면을 치면 겉만 깨진다.
		const bool bOnLayer = Stage < 3
			&& (Point.Y < OverlayMinY || Point.Y > OverlayMaxY || Point.Z >= JointZ);
		// 엉뚱한 데를 친 결과만 말한다. 세 번째부터는 H로 물을 수 있다는 것만 알린다.
		Line = bOnLayer ? MissLifted() : MissSurface();
		if (WallMissCount >= 3)
		{
			if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
			{
				if (AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD()))
				{
					Hud->OfferHintTip();
				}
			}
		}
	}
	Think(this, Line, 3.0f);
	UE_LOG(LogTemp, Display, TEXT("NIGHT4_WALL_MISS stage=%d misses=%d strikes=%d"),
		Stage, WallMissCount, Narrative->GetNightFourWallStrikeCount());
}

void AIGMissingFloorNightFourDirector::AdvanceWallStage(const int32 Stage, const FVector& StrikeLocation)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	AIGMissingFloorLeftoverDirector* Leftover = GetLeftover();
	FIGLeftoverState& State = Narrative->MutableLeftover();
	const int32 Strikes = Narrative->RecordNightFourWallStrike();
	// 녹음은 첫 망치부터 돈다. 이미 돌고 있으면 그대로 둔다.
	StartWallRecording();
	State.WallStage = Stage;
	WallMissStage = INDEX_NONE;
	WallMissCount = 0;
	const bool bImmediate = IsImmediateWallRun();
	PlayWallStrikeSound(Stage, StrikeLocation, 1.0f);
	if (AIGPlayerCharacter* Striker = Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		Striker->PlayScareKick(Stage >= 5 ? 0.9f : 0.3f + 0.1f * Stage);
	}
	UE_LOG(LogTemp, Display, TEXT("NIGHT4_WALL_STAGE %d strikes=%d"), Stage, Strikes);

	switch (Stage)
	{
	case 1:
	{
		// 테이프가 찢기고 덧판 나사가 드러난다. 들뜬 이음매로 바람이 빨려 들고, 거기 붙어 있던 게 온다.
		IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreatePackingTapeRip(this), OverlaySpot,
			0.7f, 0.85f, 140.0f, 1400.0f, EIGAudioBus::World);
		if (Leftover)
		{
			if (bImmediate)
			{
				Leftover->PrepareAnnexLining(1, false);
			}
			else
			{
				Leftover->OpenAnnexSeam();
			}
		}
		ThinkClue(this, NSLOCTEXT("IGMissingFloor", "WallStageJoint", "테이프 밑에 나사가 박혀 있어."));
		const bool bSeenBefore = Narrative->HasBeatPlayed(SeenMoveBeat)
			|| Narrative->HasBeatPlayed(FName(TEXT("Leftover.N2.FirstMove")));
		GetWorldTimerManager().SetTimer(WallThoughtTimer, FTimerDelegate::CreateWeakLambda(this, [this, bSeenBefore]()
		{
			ThinkClue(this, bSeenBefore
				? NSLOCTEXT("IGMissingFloor", "WallStageJointSeen", "여기도 그게 붙어 있었어.")
				: NSLOCTEXT("IGMissingFloor", "LeftoverFirstMove", "벽에 붙어 있던 게… 움직였어."), 3.0f);
		}), 2.6f, false);
		break;
	}
	case 2:
		if (Leftover)
		{
			if (bImmediate)
			{
				Leftover->PrepareAnnexLining(2, false);
			}
			else
			{
				Leftover->OpenAnnexInspection();
			}
		}
		else if (AIGPrologueWorldScene* SceneActor = Scene.Get())
		{
			SceneActor->SetMissingFloorInspectionOverlayRemoved(true);
		}
		BeginNeighbor303Call();
		ThinkClue(this, State.bSiteMatched
			? NSLOCTEXT("IGMissingFloor", "WallStageOverlayKnown", "점검구가 나왔어.")
			: NSLOCTEXT("IGMissingFloor", "WallStageOverlay", "덧판 밑이 뚫려 있었어."));
		bSpreadNoticed = false;
		if (!bImmediate)
		{
			GetWorldTimerManager().SetTimer(AnnexSpreadPollTimer, this,
				&AIGMissingFloorNightFourDirector::PollAnnexSpread, 0.25f, true, 1.0f);
		}
		break;
	case 3:
		// 덧판과 각재가 떨어진다. 목한수가 차단기를 내리고, 걸린 몸이 새 틈 쪽으로 버티며 틀을 민다.
		BeginPowerCutStage();
		OuterFallSeconds = bImmediate ? OuterFallDuration : 0.0f;
		bWallMotionActive = true;
		SetActorTickEnabled(true);
		for (UStaticMeshComponent* Dent : HammerDents)
		{
			ShowProp(Dent, false);
		}
		AIGHorrorHUD::PushAudioCaptionAt(this,
			NSLOCTEXT("IGMissingFloor", "WallOuterFallCaption", "[덧판과 각재가 떨어지는 소리]"), 2.2f, OverlaySpot);
		if (bImmediate)
		{
			BeginBraceSlip();
		}
		else
		{
			GetWorldTimerManager().SetTimer(BraceSlipTimer, this, &AIGMissingFloorNightFourDirector::BeginBraceSlip, 1.2f, false);
		}
		break;
	case 4:
		Think(this, NSLOCTEXT("IGMissingFloor", "NightFourStrikeFourth", "손이 저리다. 한 번만 더."), 2.2f);
		BeginHammerCallStage();
		if (bImmediate)
		{
			if (Leftover)
			{
				Leftover->PushOutAnnexLining(true);
			}
			AddWallLayer(LayerPushedOut);
			PlaceBraceFrame(BracePushed, 0.0f);
		}
		break;
	case 5:
		BeginWallOpening(GetWallTarget(EWallTarget::LastClip));
		break;
	default:
		break;
	}
}

void AIGMissingFloorNightFourDirector::PlaceHammerDent(const FVector& Point)
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (HammerDents.Num() == 0 || !Narrative || Point.Y < BayMinY || Point.Y > BayMaxY
		|| Point.Z < 1205.0f || Point.Z > 1436.0f)
	{
		return;
	}
	float FaceX = WallFaceX;
	if (Narrative->GetLeftover().WallStage < 3)
	{
		// 각재는 들썩일 뿐 자국이 안 남는다. 이음매 위는 덧판 앞면이다.
		if (Point.Y < OverlayMinY || Point.Y > OverlayMaxY)
		{
			return;
		}
		if (Point.Z >= JointZ)
		{
			FaceX = OverlayFaceX;
		}
	}
	UStaticMeshComponent* Dent = HammerDents[NextHammerDent++ % HammerDents.Num()];
	// 자국 메시는 오목한 쪽이 벽 속이다. 면보다 5 mm 앞에 둬야 가장 깊은 자리가 면에 닿는다.
	Dent->SetWorldTransform(FTransform(
		FRotator(0.0f, 180.0f, FMath::FRandRange(0.0f, 360.0f)),
		FVector(FaceX - 0.5f, Point.Y, Point.Z)));
	ShowProp(Dent, true);
}

// -- 틀과 바깥층의 움직임 -------------------------------------------------------------

void AIGMissingFloorNightFourDirector::PlaceBraceFrame(const uint8 Pose, const float Seconds)
{
	BracePose = Pose;
	if (!BraceFrame)
	{
		return;
	}
	const FTransform Target = BraceTransform(Pose);
	if (Seconds <= 0.0f)
	{
		BraceFrame->SetWorldTransform(Target);
		BraceDuration = 0.0f;
		return;
	}
	BraceFrom = BraceFrame->GetComponentTransform();
	BraceTo = Target;
	BraceSeconds = 0.0f;
	BraceDuration = Seconds;
	bWallMotionActive = true;
	SetActorTickEnabled(true);
	// 철골이 바닥을 긁고 끌리는 소리. 위층에서도 들릴 만큼은 아니다.
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateSettleTimberCreak(this),
		BraceFrom.GetLocation() + FVector(0.0f, 0.0f, 20.0f), 0.55f, 1.25f, 120.0f, 1200.0f, EIGAudioBus::World);
}

void AIGMissingFloorNightFourDirector::UpdateWallStageMotion(const float DeltaSeconds)
{
	bool bMoving = false;
	if (BraceDuration > 0.0f && BraceFrame)
	{
		BraceSeconds += DeltaSeconds;
		const float Alpha = FMath::Clamp(BraceSeconds / BraceDuration, 0.0f, 1.0f);
		FTransform Blend;
		Blend.Blend(BraceFrom, BraceTo, FMath::SmoothStep(0.0f, 1.0f, Alpha));
		BraceFrame->SetWorldTransform(Blend);
		if (Alpha >= 1.0f)
		{
			BraceDuration = 0.0f;
		}
		else
		{
			bMoving = true;
		}
	}
	if (OuterFallSeconds >= 0.0f)
	{
		OuterFallSeconds += DeltaSeconds;
		const float Alpha = FMath::Clamp(OuterFallSeconds / OuterFallDuration, 0.0f, 1.0f);
		// 떨어지는 것은 갈수록 빠르다.
		const float T = Alpha * Alpha;
		if (WallOverlayTop)
		{
			FTransform Blend;
			Blend.Blend(OverlayOnWall(), OverlayFallen(), T);
			WallOverlayTop->SetWorldTransform(Blend);
		}
		for (int32 Index = 0; Index < WallBattens.Num(); ++Index)
		{
			if (WallBattens[Index])
			{
				FTransform Blend;
				Blend.Blend(BattenOnWall(Index), BattenOnFloor(Index), FMath::Clamp(T * (1.0f + 0.15f * Index), 0.0f, 1.0f));
				WallBattens[Index]->SetWorldTransform(Blend);
			}
		}
		if (Alpha >= 1.0f)
		{
			OuterFallSeconds = -1.0f;
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateBoardStackFall(this),
				OverlayFallenBase, 0.75f, 1.05f, 160.0f, 1800.0f, EIGAudioBus::World);
			if (UIGDustSubsystem* Dust = GetWorld() ? GetWorld()->GetSubsystem<UIGDustSubsystem>() : nullptr)
			{
				Dust->ReportDisturbance(OverlayFallenBase + FVector(0.0f, 0.0f, 10.0f), 0.8f);
			}
			RefreshWallStage();
		}
		else
		{
			bMoving = true;
		}
	}
	bWallMotionActive = bMoving;
}

void AIGMissingFloorNightFourDirector::BeginBraceSlip()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !IsExpansionWall() || Narrative->GetLeftover().WallStage != 3 || HasWallLayer(LayerBraceSlip))
	{
		return;
	}
	AddWallLayer(LayerBraceSlip);
	PlaceBraceFrame(BraceSlipped, IsImmediateWallRun() ? 0.0f : 1.2f);
	if (AIGMissingFloorLeftoverDirector* Leftover = GetLeftover())
	{
		Leftover->StrainAnnexLining();
	}
	AIGHorrorHUD::PushAudioCaptionAt(this,
		NSLOCTEXT("IGMissingFloor", "WallBraceSlipCaption", "[철골 틀이 밀려 바닥을 긁는 소리]"), 2.4f, BraceUprightBase());
	GetWorldTimerManager().SetTimer(WallThoughtTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		Think(this, NSLOCTEXT("IGMissingFloor", "WallBraceSlip", "틀이 밀려."), 2.4f);
	}), 0.8f, false);
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::BeginWallPushOut()
{
	if (!IsExpansionWall() || HasWallLayer(LayerPushedOut) || bWallPushOutRunning)
	{
		return;
	}
	bWallPushOutRunning = true;
	if (AIGMissingFloorLeftoverDirector* Leftover = GetLeftover())
	{
		Leftover->PushOutAnnexLining(false);
	}
	PlaceBraceFrame(BracePushed, 0.9f);
	GetWorldTimerManager().SetTimer(WallPushOutPollTimer, this,
		&AIGMissingFloorNightFourDirector::PollWallPushOut, 0.2f, true, 0.4f);
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::PollWallPushOut()
{
	const AIGMissingFloorLeftoverDirector* Leftover = GetLeftover();
	if (Leftover && Leftover->IsAnnexLiningBusy())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(WallPushOutPollTimer);
	bWallPushOutRunning = false;
	AddWallLayer(LayerPushedOut);
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::PollAnnexSpread()
{
	const AIGMissingFloorLeftoverDirector* Leftover = GetLeftover();
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Leftover || !Narrative || bSpreadNoticed || Narrative->GetLeftover().WallStage != 2)
	{
		GetWorldTimerManager().ClearTimer(AnnexSpreadPollTimer);
		return;
	}
	if (!Leftover->IsAnnexSpread())
	{
		return;
	}
	bSpreadNoticed = true;
	GetWorldTimerManager().ClearTimer(AnnexSpreadPollTimer);
	// 몸을 펴는 장면은 움직임과 소리로 보인다. 해결 방법은 요청한 힌트에서만 다룬다.
	RefreshPresentation();
}

// -- 상태 맞추기 --------------------------------------------------------------------

void AIGMissingFloorNightFourDirector::RefreshWallStage()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	const bool bExpansion = IsExpansionWall();
	const FIGLeftoverState& State = Narrative->GetLeftover();
	const int32 Stage = bExpansion ? State.WallStage : 0;
	const bool bOpened = Narrative->IsNightFourWallOpened();
	const bool bNightFour = bHourCurrentlyActive && Narrative->GetNightIndex() == 4 && !bFailureEndingActive;
	AIGMissingFloorLeftoverDirector* Leftover = GetLeftover();

	// 지난 단계의 몸과 틀을 되살린다(불러오기, 재도전). 진행 중인 장면이면 건드리지 않는다.
	if (bExpansion && bNightFour && !bOpened)
	{
		if (Leftover)
		{
			Leftover->EnsureAnnexLining();
		}
		if (Stage == 3 && !HasWallLayer(LayerBraceSlip) && !GetWorldTimerManager().IsTimerActive(BraceSlipTimer))
		{
			BeginBraceSlip();
			return;
		}
		if (Stage >= 4 && !HasWallLayer(LayerPushedOut) && !bWallPushOutRunning && !bHammerHeldForCall
			&& !GetWorldTimerManager().IsTimerActive(WallPushOutTimer))
		{
			AddWallLayer(LayerPushedOut);
			if (Leftover)
			{
				Leftover->PushOutAnnexLining(true);
			}
		}
	}

	// 보이는 층. 확장 전 망치질을 이어 가는 판은 예전 벽 그대로다.
	ShowProp(WallOverlayTop, bExpansion);
	for (UStaticMeshComponent* Batten : WallBattens)
	{
		ShowProp(Batten, bExpansion);
	}
	if (OuterFallSeconds < 0.0f)
	{
		if (WallOverlayTop)
		{
			WallOverlayTop->SetWorldTransform(Stage >= 3 ? OverlayFallen() : OverlayOnWall());
		}
		for (int32 Index = 0; Index < WallBattens.Num(); ++Index)
		{
			if (WallBattens[Index])
			{
				WallBattens[Index]->SetWorldTransform(Stage >= 3 ? BattenOnFloor(Index) : BattenOnWall(Index));
			}
		}
	}
	ShowProp(WallJointTape, bExpansion && Stage == 0);
	ShowProp(WallJointTorn, bExpansion && Stage >= 1 && Stage <= 2);
	ShowProp(WallInnerFixings, bExpansion && Stage >= 3 && !bOpened);
	ShowProp(WallAngleClip, bExpansion && Stage >= 3 && !bOpened);
	ShowProp(BraceFrame, bExpansion);
	if (bOpened || !bExpansion)
	{
		for (UStaticMeshComponent* Dent : HammerDents)
		{
			ShowProp(Dent, false);
		}
	}
	if (BraceFrame && bExpansion && BraceDuration <= 0.0f && !GetWorldTimerManager().IsTimerActive(BracePinTimer)
		&& !bWallPushOutRunning)
	{
		uint8 Desired = State.bFramePlaced ? BraceUpright : BraceLean;
		if (Stage == 3 && HasWallLayer(LayerBraceSlip) && !HasWallLayer(LayerRebrace))
		{
			Desired = BraceSlipped;
		}
		if (Stage >= 5 || (Stage == 4 && HasWallLayer(LayerPushedOut)))
		{
			Desired = BracePushed;
		}
		if (Desired != BracePose || !BraceFrame->GetComponentLocation().Equals(BraceTransform(Desired).GetLocation(), 0.5f))
		{
			PlaceBraceFrame(Desired, 0.0f);
		}
	}

	// 손대는 자리. 물길이 돌고 마지막 선택이 열린 밤4, 망치를 들 수 있을 때만이다.
	const bool bCanWork = bExpansion && bNightFour
		&& Narrative->IsFinalChoiceUnlocked()
		&& Narrative->IsNightFourMaskRunning()
		&& !bOpened
		&& !bWallOpeningPending
		&& !bHammerHeldForCall;
	const auto SetLive = [this](const EWallTarget Target, const bool bLive)
	{
		if (AIGMissingFloorEvidence* Evidence = GetWallTarget(Target))
		{
			Evidence->SetActorHiddenInGame(!bLive);
			Evidence->SetInteractionEnabled(bLive);
		}
	};
	SetLive(EWallTarget::Survey, bCanWork && Stage == 0 && !HasWallLayer(LayerSurvey));
	SetLive(EWallTarget::Joint, bCanWork && Stage == 0 && HasWallLayer(LayerSurvey));
	SetLive(EWallTarget::Overlay, bCanWork && Stage == 1 && (!Leftover || Leftover->IsInspectionOverlayOn()));
	SetLive(EWallTarget::Brace, bCanWork && Stage == 2 && !State.bFramePlaced);
	SetLive(EWallTarget::OuterScrews, bCanWork && (Stage == 1 || Stage == 2));
	SetLive(EWallTarget::Rebrace, bCanWork && Stage == 3 && HasWallLayer(LayerBraceSlip) && !HasWallLayer(LayerRebrace));
	SetLive(EWallTarget::InnerScrewsSouth, bCanWork && Stage == 3);
	SetLive(EWallTarget::InnerScrewsNorth, bCanWork && Stage == 3);
	SetLive(EWallTarget::LastClip, bCanWork && (Stage == 3 || Stage == 4));
}
