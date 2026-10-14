// 「302호에 남은 짐」의 밤4 5층. 벽을 뜯는 단계(밤4 감독의 IGMissingFloorNightFourWall.cpp)가
// 덧댄 것을 부른다. 감독 본체는 IGMissingFloorLeftoverDirector.cpp다.
//
// 시신 벽 칸(Y 640..760)의 덧판 이음매가 들뜨면 덧댄 것이 그 틈을 덮고, 남쪽 배관 점검구가
// 열리면 그쪽으로 옮겨 가 벽과 천장 사이로 몸을 편다. 철골 틀이 무릎 마디를 걸면 더는 못 접는다.
// 마지막 판을 열기 전에 틀과 함께 남동쪽 천장 구석으로 밀려나고, 벽이 열린 뒤로는 돌아오지 않는다.

#include "Entity/IGMissingFloorLeftoverDirector.h"

#include "Audio/IGToneSequenceSoundWave.h"
#include "Core/IGPrologueWorldScene.h"
#include "Entity/IGLiningEntity.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "TimerManager.h"

namespace IGLeftoverAnnex
{
	/** 이 감독이 덧댄 것을 둔 공간 이름. 유니티 빌드에서 다른 파일의 인자 이름과 겹치지 않게 접두어를 붙였다. */
	const FName AnnexSpace(TEXT("Annex"));
	const FName GapSeam(TEXT("Annex.Seam"));
	const FName GapInspection(TEXT("Annex.Inspection"));

	/** 덧판 이음매(덧판 아래 변, 높이 1383). 가로로 긴 틈이다. */
	const FVector Seam(250.0f, 700.0f, 1385.0f);
	const FVector2D SeamSize(100.0f, 14.0f);
	/** 남쪽 배관 점검구(Y 462..522, 높이 1230..1300). */
	const FVector Inspection(250.0f, 492.0f, 1265.0f);
	const FVector2D InspectionSize(60.0f, 70.0f);
	/** 처음 접혀 붙어 있는 자리. 남쪽 칸과 시신 칸 사이 기둥 위쪽, 덧댄 층의 일부처럼 보인다. */
	const FVector Rest(250.0f, 640.0f, 1360.0f);
	/**
	 * 다 편 몸. 몸판은 점검구 북쪽 위 벽(Y 485..605, Z 1290..1370)에 납작하게 붙고, 다리는 몸 바깥으로
	 * 뻗는다. 위 두 다리(0 남, 1 북)는 천장을, 아래 두 다리(2 남, 3 북)는 벽 아랫부분을 짚는다. 다리마다
	 * 제 엉덩이 쪽 패드를 짚어서 몸판을 가로지르는 다리가 없다. 몸판이 벽에 붙어 있어 점검구 앞(X 200)에
	 * 세우는 철골 틀과 8 cm 떨어진다. 남쪽 벽은 Y 455다.
	 */
	const FVector SpreadCenter(229.0f, 545.0f, 1330.0f);
	const FVector SpreadPads[4] = {
		FVector(116.0f, 466.0f, 1438.0f),
		FVector(175.0f, 690.0f, 1438.0f),
		FVector(250.0f, 472.0f, 1212.0f),
		FVector(250.0f, 625.0f, 1212.0f),
	};
	const FVector SpreadNormals[4] = {
		FVector(0.0f, 0.0f, -1.0f),
		FVector(0.0f, 0.0f, -1.0f),
		FVector(-1.0f, 0.0f, 0.0f),
		FVector(-1.0f, 0.0f, 0.0f),
	};
	/** 철골 틀 위 런너에 걸리는 무릎. 위 남쪽 다리(0)의 무릎이고 엉덩이에서 첫 마디(80 cm)만큼 떨어졌다. */
	constexpr int32 PinnedLeg = 0;
	const FVector KneePin(200.0f, 470.0f, 1430.0f);
	/** 밀려나 접혀 붙는 자리. 남동쪽 천장 구석, 작업할 칸에서 한 칸 넘게 떨어졌다. */
	const FVector PushedRest(195.0f, 525.0f, 1440.0f);
	constexpr float SpreadSeconds = 2.2f;
	constexpr float PushOutSeconds = 3.0f;

	FIGLiningGap MakeGap(const FName Id, const FVector& Center, const FVector2D& Size)
	{
		FIGLiningGap Gap;
		Gap.Id = Id;
		Gap.Center = Center;
		Gap.Normal = FVector(-1.0f, 0.0f, 0.0f);
		Gap.Up = FVector::UpVector;
		Gap.Size = Size;
		Gap.bAlwaysOpen = true;
		return Gap;
	}
}

using namespace IGLeftoverAnnex;

FVector AIGMissingFloorLeftoverDirector::GetAnnexKneePin()
{
	return KneePin;
}

bool AIGMissingFloorLeftoverDirector::IsInspectionOverlayOn() const
{
	return Scene && !Scene->IsMissingFloorInspectionOverlayRemoved();
}

bool AIGMissingFloorLeftoverDirector::IsAnnexLiningBusy() const
{
	return Lining && CurrentSpace == AnnexSpace && !Lining->IsHidden() && Lining->IsBusy();
}

bool AIGMissingFloorLeftoverDirector::IsAnnexSpread() const
{
	return Lining && CurrentSpace == AnnexSpace && Lining->GetState() == EIGLiningState::Spread && !Lining->IsMoving();
}

void AIGMissingFloorLeftoverDirector::EnsureAnnexLining()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Lining || !Narrative || CurrentSpace == AnnexSpace || IsLegacyRun() || Narrative->IsNightFourWallOpened())
	{
		return;
	}
	PrepareAnnexLining(Narrative->GetLeftover().WallStage, Narrative->GetLeftover().bFramePlaced);
}

void AIGMissingFloorLeftoverDirector::SpreadAnnexLining(const float Seconds)
{
	if (!Lining)
	{
		return;
	}
	TArray<FVector> Pads;
	TArray<FVector> Normals;
	for (int32 Leg = 0; Leg < AIGLiningEntity::LegCount; ++Leg)
	{
		Pads.Add(SpreadPads[Leg]);
		Normals.Add(SpreadNormals[Leg]);
	}
	Lining->SpreadTo(SpreadCenter, FVector(-1.0f, 0.0f, 0.0f), FVector::UpVector, Pads, Normals, Seconds);
}

void AIGMissingFloorLeftoverDirector::PrepareAnnexLining(const int32 WallStage, const bool bBracePlaced)
{
	if (!Lining || !Lining->LoadBody())
	{
		return;
	}
	CurrentSpace = AnnexSpace;
	LoadGapsFor(CurrentSpace);
	if (Scene)
	{
		Scene->SetMissingFloorInspectionOverlayRemoved(WallStage >= 2);
	}
	Lining->ReleasePin();
	if (WallStage <= 0)
	{
		Lining->RestOnSurface(Rest, FVector(-1.0f, 0.0f, 0.0f), FVector::UpVector);
		return;
	}
	if (WallStage == 1)
	{
		Lining->SetGap(MakeGap(GapSeam, Seam, SeamSize));
		Lining->PlaceHolding(GapSeam);
		return;
	}
	if (WallStage <= 3)
	{
		// 이음매는 눌러 막혔고, 점검구 위에서 천장까지 편 채다. 틀을 댄 판이면 무릎이 걸려 있다.
		FIGLiningGap SealedSeam = MakeGap(GapSeam, Seam, SeamSize);
		SealedSeam.bSealed = true;
		Lining->SetGap(SealedSeam);
		Lining->SetGap(MakeGap(GapInspection, Inspection, InspectionSize));
		Lining->PlaceHolding(GapInspection);
		SpreadAnnexLining(0.5f);
		if (bBracePlaced)
		{
			Lining->PinKnee(PinnedLeg, KneePin);
		}
		return;
	}
	// 넷째 단계 뒤: 남동쪽 천장 구석에 접혀 붙어 있다. 고를 틈이 없다.
	Lining->ClearGaps();
	Lining->RestOnSurface(PushedRest, FVector(0.0f, 0.0f, -1.0f), FVector::ForwardVector);
}

void AIGMissingFloorLeftoverDirector::OpenAnnexSeam()
{
	EnsureAnnexLining();
	if (!Lining)
	{
		return;
	}
	// 이음 테이프가 뜯겨 덧판 아래 변이 들떴다. 벽 속 공간과 이어진 가로로 긴 틈이다.
	Lining->SetGap(MakeGap(GapSeam, Seam, SeamSize));
	Lining->NotifyGapsChanged();
}

void AIGMissingFloorLeftoverDirector::OpenAnnexInspection()
{
	EnsureAnnexLining();
	if (!Lining || !Scene)
	{
		return;
	}
	Scene->SetMissingFloorInspectionOverlayRemoved(true);
	PlaySound(UIGToneSequenceSoundWave::CreateBoardStackFall(this), Inspection, 0.8f, 1.1f, 200.0f, 2000.0f);
	Lining->SetGap(MakeGap(GapInspection, Inspection, InspectionSize));
	Lining->NotifyGapsChanged();
	// 점검구를 다 눌러 막으면 벽과 천장 사이로 몸을 편다. 무릎은 철골 틀을 대야 걸린다.
	Lining->OnGapSealed.RemoveAll(this);
	Lining->OnGapSealed.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleLiningSealed);
	Lining->OnGapSealed.AddWeakLambda(this, [this](AIGLiningEntity* Entity, const FName GapId)
	{
		if (GapId == GapInspection && Entity && CurrentSpace == AnnexSpace)
		{
			SpreadAnnexLining(SpreadSeconds);
		}
	});
}

void AIGMissingFloorLeftoverDirector::PinAnnexKnee()
{
	if (!Lining || CurrentSpace != AnnexSpace)
	{
		return;
	}
	Lining->PinKnee(PinnedLeg, KneePin);
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->MutableLeftover().bFramePlaced = true;
	}
	Caption(NSLOCTEXT("IGMissingFloor", "LeftoverFrameCatchCaption", "[철골 마디가 틀에 걸려 끼는 소리]"), KneePin, 2.6f);
}

void AIGMissingFloorLeftoverDirector::StrainAnnexLining()
{
	if (!Lining || CurrentSpace != AnnexSpace || Lining->IsHidden())
	{
		return;
	}
	// 바깥층이 떨어져 벽에 새 틈이 났다. 걸린 무릎 때문에 못 가고 틀을 민다. 몸짓은 틀이 맡는다.
	PlaySound(UIGToneSequenceSoundWave::CreateSettleTimberCreak(this), KneePin, 0.75f, 0.78f, 150.0f, 1600.0f);
}

void AIGMissingFloorLeftoverDirector::PushOutAnnexLining(const bool bInstant)
{
	if (!Lining || CurrentSpace != AnnexSpace)
	{
		return;
	}
	// 마지막 판을 열기 전. 걸린 틀이 휘며 빠지고, 몸이 작업할 자리에서 남동쪽 천장 구석으로 밀려난다.
	Lining->ReleasePin();
	Lining->ClearGaps();
	if (bInstant)
	{
		Lining->RestOnSurface(PushedRest, FVector(0.0f, 0.0f, -1.0f), FVector::ForwardVector);
		return;
	}
	Lining->RetreatTo(PushedRest, FVector(0.0f, 0.0f, -1.0f), FVector::ForwardVector, PushOutSeconds);
	Caption(NSLOCTEXT("IGMissingFloor", "LeftoverPushedCaption", "[휜 철골이 빠지며 무언가 벽을 긁고 물러나는 소리]"),
		Lining->GetBodyCenter(), 2.8f);
}

void AIGMissingFloorLeftoverDirector::HideAnnexLining()
{
	if (!Lining || CurrentSpace != AnnexSpace)
	{
		return;
	}
	Lining->ClearGaps();
	Lining->Hide();
	CurrentSpace = NAME_None;
}
