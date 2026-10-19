// 「302호에 남은 짐」의 302호(밤3, P7), 덮개 조립(P6), 5층 대 보기. 밤4 5층은 IGMissingFloorLeftoverAnnex.cpp다.
// 감독 본체는 IGMissingFloorLeftoverDirector.cpp다. 좌표와 이름표는 거기 IGLeftover에 있고,
// 여기서 쓰는 것만 다시 적는다(.cpp마다 이름공간이 따로다).

#include "Entity/IGMissingFloorLeftoverDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Entity/IGJeongwooFigure.h"
#include "Entity/IGLiningEntity.h"
#include "Entity/IGManagerPatrol.h"
#include "Entity/IGNightThreatDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Interaction/IGAccessHatch.h"
#include "Interaction/IGHidingSpot.h"
#include "Interaction/IGLeftoverTarget.h"
#include "Materials/MaterialInterface.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "TimerManager.h"

namespace IGLeftoverStorage
{
	constexpr float StorageZ = 600.0f;
	const FName GapNarrow(TEXT("Storage.Narrow"));
	const FName GapLarge(TEXT("Storage.Large"));
	const FName GapAlt(TEXT("Storage.Alt"));
	const FName PieceA(TEXT("PieceA"));
	const FName PieceB(TEXT("PieceB"));
	const FName PieceC(TEXT("PieceC"));
	const FName StorageFrame(TEXT("Frame"));
	const FName ToolPouch(TEXT("ToolPouch"));
	const FName DohaBox(TEXT("DohaBox"));
	const FName Cover(TEXT("Cover"));
	const FName StorageWedge(TEXT("Wedge"));
	const FName Trapped(TEXT("Trapped"));
	const FName Freed(TEXT("Freed"));
	const FName LeftTogether(TEXT("LeftTogether"));
	const FName SelfEscaped(TEXT("SelfEscaped"));
	const FName WedgeBrace(TEXT("Brace"));
	const FName WedgeSlipped(TEXT("Slipped"));
	const FName BeatText(TEXT("Leftover.N3.Text"));
	const FName BeatVoice(TEXT("Leftover.N3.Voice"));
	const FName BeatOut(TEXT("Leftover.N3.Out"));
	const FName BeatDayOneMarked(TEXT("Leftover.Day1.Marked"));
	const FName BeatDayOneBoxes(TEXT("Leftover.Day1.Boxes"));
	const FName BeatSeenMove(TEXT("Leftover.SeenMove"));
	const FName JeongwooIntroBeat(TEXT("Day.Jeongwoo.Intro"));
	const FName BeatDayTwoTalk(TEXT("Leftover.Day2.Talk"));
	const FName BeatLabel303Peeled(TEXT("Leftover.Label303Peeled"));

	/** 판 뒤 구석(벽에서 20 cm). 판에서 풀려나면 판 끝이 다 돈 뒤 서쪽 끝을 돌아 나온다. */
	const FVector JeongwooCorner(100.0f, 66.0f, StorageZ);
	const FVector JeongwooOutSpot(-5.0f, 0.0f, StorageZ);
	/**
	 * 셋째 자세의 판을 상자 바로 위에서 받치는 자리. A_Jeongwoo_Brace는 두 손을 발보다 60.5 cm 앞,
	 * 136.7 cm 높이에 둔다. 판이 방 쪽으로 10도 기울어 그 높이의 면은 밑동보다 25 cm 방 쪽이고,
	 * 손바닥이 면에 닿게 손 뼈를 면에서 3.5 cm 뗐다. 두 손은 판 길이 58..92 cm에 온다.
	 */
	const FVector JeongwooBraceSpot(64.5f, -80.3f, StorageZ);
	/**
	 * 문을 잡고 기다리는 자리. 302호 문간(X -75..15) 바로 앞에 서면 그녀가 방을 못 나와서, 복도의 서쪽
	 * 문설주 옆에 서서 문틀을 잡는다. 문간 가운데와 74 cm 떨어져 둘의 캡슐(26 + 30)이 닿지 않는다.
	 */
	const FVector JeongwooDoorSpot(-104.0f, -262.0f, StorageZ);
	/** 판을 받친 채 기다리는 시간. 그사이 상자를 당기지 않으면 판을 놓고 문으로 간다. */
	constexpr double BraceWaitSeconds = 40.0;
	/** 문을 잡고 기다리는 시간. 그 뒤로는 먼저 내려간다(그녀는 302호를 더 볼 수 있다). */
	constexpr double DoorWaitSeconds = 150.0;
	/** 같이 못 뺀 오빠 상자를 정우가 아침에 두고 간 자리. 403호 문 왼쪽 벽 앞, 문 열리는 길 밖이다. */
	const FTransform DeliveredBoxSpot(FRotator(0.0f, 8.0f, 0.0f), FVector(2.0f, -255.0f, 900.0f));
	/** 판: 북동 구석을 대각선으로 막는 마감판. 밑동은 x+y=113 줄이고 동쪽 끝이 동벽(X 120)에 닿는다. */
	const FVector BoardBase(83.0f, 30.0f, StorageZ);
	constexpr float BoardYaw = -45.0f;
	/** 판 동쪽 끝(밑동). 정우가 서쪽 끝을 밀어 낼 때 이 점을 축으로 돈다. */
	const FVector BoardEastEnd(118.36f, -5.36f, StorageZ);
	constexpr float BoardPushYaw = 25.0f;
	/** 쐐기 자리. 0은 판 밑동이 바닥 틀에 걸리는 곳이고 1·2는 바닥 틀이 없는 장판이다. */
	const FVector WedgeSockets[3] = {
		FVector(51.0f, 60.0f, StorageZ + 2.0f),
		FVector(61.2f, 47.9f, StorageZ + 2.0f),
		FVector(71.1f, 38.0f, StorageZ + 2.0f),
	};
	const FVector WedgePickup(-40.0f, -165.0f, StorageZ);
	/** 덮개 조각과 금속 틀을 넣은 마대. 동벽 앞, 덧댄 것이 지나가는 북서쪽 길과 판·상자를 비켰다. */
	const FVector PartsSack(105.0f, -100.0f, StorageZ);
	/**
	 * 오빠 상자: 방 쪽으로 기운 판 밑, 판 동쪽 절반. 판 방 쪽 면이 상자 위 모서리에 얹혔다(틈 2.5 mm).
	 * 이름표 면이 방을 본다.
	 */
	const FTransform DohaBoxUnderBoard(FRotator(0.0f, BoardYaw, 0.0f), FVector(82.3f, -6.0f, StorageZ));
	/** 조립대. 상자 둘(X -4..72, Y -210..-160)이 남벽 앞에 붙었고 그 위에 금속 틀이 뒷면을 위로 누웠다. */
	const FVector AssemblyCenter(34.0f, -185.0f, StorageZ + 34.6f);
	const FVector AssemblyHover(-24.0f, -185.0f, StorageZ + 72.0f);

	/** 판 동쪽 끝을 축으로 수평으로 돌린 자리. 셋째 자세의 판과 상자가 같이 쓴다. */
	FTransform PushedAside(const FTransform& Resting)
	{
		const FQuat Turn(FVector::ZAxisVector, FMath::DegreesToRadians(BoardPushYaw));
		return FTransform(Turn * Resting.GetRotation(), BoardEastEnd + Turn.RotateVector(Resting.GetLocation() - BoardEastEnd));
	}
	/** 조각 셋이 처음 놓인 바닥(조립대 서쪽). */
	const FVector PieceFloor[3] = {
		FVector(-30.0f, -110.0f, StorageZ + 0.8f),
		FVector(-28.0f, -60.0f, StorageZ + 0.8f),
		FVector(-62.0f, -82.0f, StorageZ + 0.8f),
	};
	/** 덮개 좌표(cm)에서 조각마다의 무게 중심. 앞면 방수막이 X 0, 판이 -1.2. */
	const FVector PieceCentroid[3] = {
		FVector(-0.6f, -14.5f, 0.0f),
		FVector(-0.6f, 14.0f, 17.5f),
		FVector(-0.6f, 14.0f, -17.5f),
	};
	/** 고정쇠 넷: 0 A 위, 1 A 아래, 2 B 위, 3 C 아래. 기둥은 모서리 안쪽, 구멍은 조각 뒷면. */
	const FVector2D ClampPost[4] = {FVector2D(-29.6f, 34.6f), FVector2D(-29.6f, -34.6f), FVector2D(29.6f, 34.6f), FVector2D(29.6f, -34.6f)};
	const FVector2D ClampHole[4] = {FVector2D(-27.0f, 32.0f), FVector2D(-27.0f, -32.0f), FVector2D(27.0f, 32.0f), FVector2D(27.0f, -32.0f)};
	const int32 ClampPiece[4] = {0, 0, 1, 2};
	/** 5층 배관 점검 자리(덧판 앞)와 대 본 뒤 기대 두는 자리. */
	const FVector SiteMatchSpot(244.0f, 492.0f, 1265.0f);
	/** 5층 남쪽 벽에 기대 둔 철골 틀의 가운데(밤4 벽 감독이 세운다). */
	const FVector BraceLeanCenter(218.0f, 478.0f, 1310.0f);
	const FVector SiteLeanSpot(236.0f, 585.0f, 1200.0f);

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
	const FText& YudamName()
	{
		static const FText Name = NSLOCTEXT("IGMissingFloor", "YudamCounterSpeaker", "백유담");
		return Name;
	}
	const FText& UnknownVoiceName()
	{
		static const FText Name = NSLOCTEXT("IGMissingFloor", "LeftoverUnknownVoice", "302호 안");
		return Name;
	}

	FQuat CoverStateQuat(const int32 Rotation, const bool bFlipped)
	{
		// 덮개 좌표에서: 법선(X)을 축으로 90도씩 돌리고, 뒤집기는 세로축(Z)을 축으로 반 바퀴.
		const FQuat Turn(FVector::XAxisVector, FMath::DegreesToRadians(90.0f * static_cast<float>(Rotation & 3)));
		const FQuat Flip = bFlipped ? FQuat(FVector::ZAxisVector, PI) : FQuat::Identity;
		return Turn * Flip;
	}

	/** 누운 틀의 자세: 덮개 앞면(+X)이 바닥(-Z)을, 덮개 세로(Z)가 +X를 본다. */
	FQuat FrameLyingQuat()
	{
		return FRotator(-90.0f, 0.0f, 0.0f).Quaternion();
	}
}

using namespace IGLeftoverStorage;

// -- 302호 ---------------------------------------------------------------------

const FText& AIGMissingFloorLeftoverDirector::StorageVoice() const
{
	return HasBeat(JeongwooIntroBeat) ? JeongwooName() : UnknownVoiceName();
}

void AIGMissingFloorLeftoverDirector::BuildStorage()
{
	// 판: 북동 구석을 대각선으로 막는 벽지 마감판. 몸이 서쪽 끝을 누르고 있다.
	SpawnProp(FName(TEXT("Storage.Board")), Mesh(TEXT("SM_FinishBoard")),
		FTransform(FRotator(0.0f, BoardYaw, -10.0f), BoardBase), true);
	// 판 밑동이 걸리는 바닥 틀(떼어 내다 둔 경량 철골 한 토막). 쐐기가 여기에 물린다.
	SpawnProp(FName(TEXT("Storage.Track")), LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")),
		FTransform(FRotator(0.0f, -45.0f, 0.0f), FVector(45.0f, 68.0f, StorageZ + 2.0f), FVector(0.46f, 0.06f, 0.04f)), true);
	if (UStaticMeshComponent* Track = Props.FindRef(FName(TEXT("Storage.Track"))))
	{
		Track->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/MI_HandCart.MI_HandCart"), nullptr, LOAD_NoWarn));
	}
	// 오빠 상자(정우가 표시해 둔 것). 기운 판 밑에 끼었다. 이름표는 앞면.
	SpawnProp(FName(TEXT("Storage.DohaBox")), Mesh(TEXT("SM_StorageBox")), DohaBoxUnderBoard, true);
	SpawnProp(FName(TEXT("Storage.DohaLabel")), Mesh(TEXT("SM_LeftoverLabelDoha")), DohaBoxUnderBoard, false);
	SpawnProp(FName(TEXT("Storage.DohaMark")), Mesh(TEXT("SM_LeftoverLabelMarked")), DohaBoxUnderBoard, false);
	// 남은 덮개 조각과 금속 틀을 넣은 마대(동벽 앞).
	SpawnProp(FName(TEXT("Storage.PartsSack")), Mesh(TEXT("SM_WasteSack")),
		FTransform(FRotator(0.0f, 30.0f, 0.0f), PartsSack), true);
	// 낮1에 옮겨 온 「주인 없는 짐」. 남쪽 벽 서쪽에 쌓였다.
	SpawnProp(FName(TEXT("Storage.Pile1")), Mesh(TEXT("SM_StorageBox")), FTransform(FRotator(0.0f, 8.0f, 0.0f), FVector(-138.0f, -192.0f, StorageZ)), true);
	// 맨 위 상자는 303호 김치통이다. 이름표가 방 쪽을 보도록 돌려 얹었고, 관리인이 이름 위에 「주인 없음」을 붙였다.
	const FTransform Pile2(FRotator(0.0f, 175.0f, 0.0f), FVector(-136.0f, -191.0f, StorageZ + 34.2f));
	SpawnProp(FName(TEXT("Storage.Pile2")), Mesh(TEXT("SM_StorageBox")), Pile2, true);
	SpawnProp(FName(TEXT("Storage.Pile2Label")), Mesh(TEXT("SM_LeftoverLabel303")), Pile2, false);
	SpawnProp(FName(TEXT("Storage.Pile2Sticker")), Mesh(TEXT("SM_LeftoverStickerUnclaimed")), Pile2, false);
	SpawnProp(FName(TEXT("Storage.Pile3")), Mesh(TEXT("SM_StorageBox")), FTransform(FRotator(0.0f, 92.0f, 0.0f), FVector(-108.0f, -192.0f, StorageZ)), true);
	SpawnProp(FName(TEXT("Storage.Pile4")), Mesh(TEXT("SM_WasteSack")), FTransform(FRotator(0.0f, 40.0f, 0.0f), FVector(-118.0f, -163.0f, StorageZ)), true);
	// 조립대 상자 둘. 문 길(X -30)과 동벽 점검구 앞을 비켰다.
	SpawnProp(FName(TEXT("Storage.Table1")), Mesh(TEXT("SM_StorageBox")), FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(15.0f, -185.0f, StorageZ)), true);
	SpawnProp(FName(TEXT("Storage.Table2")), Mesh(TEXT("SM_StorageBox")), FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(53.0f, -185.0f, StorageZ)), true);
	// 쐐기. 처음에는 문 옆 공사 찌꺼기 사이에 있다.
	SpawnProp(FName(TEXT("Storage.Wedge")), Mesh(TEXT("SM_TimberWedge")), FTransform(FRotator(0.0f, 70.0f, 0.0f), WedgePickup), false);

	const auto Bind = [this](AIGLeftoverTarget* Target, const bool bTap = false)
	{
		if (!Target)
		{
			return;
		}
		Target->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleStorageTarget);
		if (bTap)
		{
			Target->SetTapAction(true);
			Target->OnTapped.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleStorageTap);
		}
	};
	Bind(SpawnTarget(FName(TEXT("Storage.WedgePick")), WedgePickup + FVector(0.0f, 0.0f, 4.0f), FRotator::ZeroRotator,
		FVector(14.0f, 14.0f, 6.0f), NSLOCTEXT("IGMissingFloor", "LeftoverWedgePickPrompt", "나무 쐐기 줍기"), 0.0f, 0.03f));
	for (int32 Socket = 0; Socket < 3; ++Socket)
	{
		if (AIGLeftoverTarget* Target = SpawnTarget(FName(*FString::Printf(TEXT("Storage.Socket%d"), Socket)),
				WedgeSockets[Socket] + FVector(0.0f, 0.0f, 3.0f), FRotator(0.0f, -45.0f, 0.0f), FVector(10.0f, 14.0f, 6.0f),
				NSLOCTEXT("IGMissingFloor", "LeftoverWedgePlacePrompt", "판 밑에 쐐기 끼우기"), 0.9f, 0.12f))
		{
			Target->Index = Socket;
			Bind(Target);
		}
	}
	// 장롱은 남쪽(-Y)으로 밀리니 손은 앞면 북쪽 모서리(Y 20..41)를 잡는다. 예전에는 이 표적이 앞면 전체를 덮어
	// 셋째 밤에 장롱에 숨으려고 겨누면 늘 「장롱 밀기」가 잡혔다(숨으려다 시끄럽게 밀었다). 북쪽 옆면은 그때
	// 덧댄 것이 북벽 틈을 누르고 있어 설 자리가 없다. 가운데와 남쪽을 겨누면 숨는 자리가 잡힌다.
	Bind(SpawnTarget(FName(TEXT("Storage.Wardrobe")), FVector(-112.0f, 30.5f, StorageZ + 100.0f), FRotator::ZeroRotator,
		FVector(4.0f, 10.5f, 60.0f), NSLOCTEXT("IGMissingFloor", "LeftoverWardrobePrompt", "장롱 밀기"), 1.6f, 0.45f));
	Bind(SpawnTarget(FName(TEXT("Storage.Lift")), BoardBase + FVector(-18.0f, -18.0f, 95.0f), FRotator(0.0f, -45.0f, 0.0f),
		FVector(6.0f, 46.0f, 40.0f), NSLOCTEXT("IGMissingFloor", "LeftoverBoardLiftPrompt", "판 세워 받치기"), 1.2f, 0.3f));
	Bind(SpawnTarget(FName(TEXT("Storage.Sack")), PartsSack + FVector(0.0f, 0.0f, 40.0f), FRotator::ZeroRotator,
		FVector(24.0f, 20.0f, 38.0f), NSLOCTEXT("IGMissingFloor", "LeftoverSackPrompt", "마대 열어 보기"), 0.8f, 0.10f));
	{
		const FTransform PulledBox = PushedAside(DohaBoxUnderBoard);
		Bind(SpawnTarget(FName(TEXT("Storage.DohaPull")), PulledBox.TransformPosition(FVector(-31.0f, 0.0f, 17.0f)),
			PulledBox.Rotator(), FVector(8.0f, 20.0f, 14.0f), NSLOCTEXT("IGMissingFloor", "LeftoverDohaPullPrompt", "끼인 상자 당기기"),
			1.6f, 0.25f));
	}
	Bind(SpawnTarget(FName(TEXT("Storage.Jeongwoo")), JeongwooCorner + FVector(0.0f, 0.0f, 80.0f), FRotator::ZeroRotator,
		FVector(30.0f, 30.0f, 50.0f), NSLOCTEXT("IGMissingFloor", "LeftoverJeongwooPrompt", "정우에게 말 걸기"), 0.0f, 0.04f));
	// 303호 상자의 딱지. 이름표 앞면(상자 원점에서 X 12, Y -19.3, Z 18.7 cm를 상자 방향으로 돌린 자리).
	Bind(SpawnTarget(FName(TEXT("Storage.Label303")), FVector(-146.3f, -170.7f, StorageZ + 52.9f), FRotator(0.0f, 175.0f, 0.0f),
		FVector(10.0f, 4.0f, 7.0f), NSLOCTEXT("IGMissingFloor", "LeftoverLabel303Prompt", "딱지 들춰 보기"), 0.0f, 0.03f));
	// 낮1 302호의 오빠 상자(이름을 보는 자리).
	Bind(SpawnTarget(FName(TEXT("Storage.DohaName")), FVector(-108.0f, -192.0f, StorageZ + 20.0f), FRotator::ZeroRotator,
		FVector(28.0f, 22.0f, 20.0f), NSLOCTEXT("IGMissingFloor", "LeftoverDohaNamePrompt", "상자 이름표 보기"), 0.0f, 0.03f));
}

FName AIGMissingFloorLeftoverDirector::GetStorageStage() const
{
	switch (StorageStage)
	{
	case EStorage::Idle: return FName(TEXT("Idle"));
	case EStorage::Trapped: return FName(TEXT("Trapped"));
	case EStorage::Released: return FName(TEXT("Released"));
	case EStorage::Fallen: return FName(TEXT("Fallen"));
	case EStorage::Freed: return FName(TEXT("Freed"));
	case EStorage::Bracing: return FName(TEXT("Bracing"));
	case EStorage::Leaving: return FName(TEXT("Leaving"));
	case EStorage::Gone: return FName(TEXT("Gone"));
	default: return NAME_None;
	}
}

void AIGMissingFloorLeftoverDirector::SetBoardPose(const int32 Pose, const bool bAnimate)
{
	UStaticMeshComponent* Board = Props.FindRef(FName(TEXT("Storage.Board")));
	if (!Board)
	{
		return;
	}
	BoardPose = Pose;
	const FTransform Resting(FRotator(0.0f, BoardYaw, -10.0f), BoardBase);
	FTransform Target;
	switch (Pose)
	{
	case 2:
		// 몸이 떠나며 밑동이 구석 쪽으로 9 cm 미끄러지고 판이 더 누웠다. 상자 위 모서리에 얹혀 멈췄고
		// 구석이 좁아졌다. 동쪽 끝이 동벽을 안 뚫게 판 길이 방향으로 8 cm 물러났다.
		Target = FTransform(FRotator(0.0f, BoardYaw, -24.0f), BoardBase + FVector(0.67f, 11.99f, 0.0f));
		break;
	case 3:
		// 정우가 서쪽 끝을 밀어 냈다. 동쪽 끝을 축으로 25도 돌았고 밑동에 걸린 상자가 같이 끌려왔다.
		Target = PushedAside(Resting);
		break;
	default:
		Target = Resting;
		break;
	}
	const FTransform BoxTarget = Pose == 3 ? PushedAside(DohaBoxUnderBoard) : DohaBoxUnderBoard;
	UStaticMeshComponent* Box = Props.FindRef(FName(TEXT("Storage.DohaBox")));
	if (!bAnimate)
	{
		Board->SetWorldTransform(Target);
		for (const TCHAR* Key : {TEXT("Storage.DohaBox"), TEXT("Storage.DohaLabel"), TEXT("Storage.DohaMark")})
		{
			if (UStaticMeshComponent* Part = Props.FindRef(FName(Key)))
			{
				Part->SetWorldTransform(BoxTarget);
			}
		}
		return;
	}
	BoardFrom = Board->GetComponentTransform();
	BoardTo = Target;
	BoxFrom = Box ? Box->GetComponentTransform() : BoxTarget;
	BoxTo = BoxTarget;
	BoardMoveStart = GetWorld()->GetTimeSeconds();
	BoardMoveSeconds = Pose == 2 ? 0.45f : 1.2f;
	GetWorldTimerManager().SetTimer(BoardTimer, this, &AIGMissingFloorLeftoverDirector::TickBoardMove, 1.0f / 60.0f, true);
}

void AIGMissingFloorLeftoverDirector::TickBoardMove()
{
	const float Alpha = FMath::Clamp(
		static_cast<float>((GetWorld()->GetTimeSeconds() - BoardMoveStart) / FMath::Max(BoardMoveSeconds, 0.05f)), 0.0f, 1.0f);
	const float Ease = BoardPose == 2 ? Alpha * Alpha : Alpha * Alpha * (3.0f - 2.0f * Alpha);
	if (UStaticMeshComponent* Moving = Props.FindRef(FName(TEXT("Storage.Board"))))
	{
		FTransform Blend;
		Blend.Blend(BoardFrom, BoardTo, Ease);
		Moving->SetWorldTransform(Blend);
	}
	FTransform BoxBlend;
	BoxBlend.Blend(BoxFrom, BoxTo, Ease);
	for (const TCHAR* Key : {TEXT("Storage.DohaBox"), TEXT("Storage.DohaLabel"), TEXT("Storage.DohaMark")})
	{
		if (UStaticMeshComponent* Part = Props.FindRef(FName(Key)))
		{
			Part->SetWorldTransform(BoxBlend);
		}
	}
	if (Alpha >= 1.0f)
	{
		GetWorldTimerManager().ClearTimer(BoardTimer);
	}
}

void AIGMissingFloorLeftoverDirector::RefreshStorage()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const int32 Night = GetNightIndex();
	const bool bLegacy = IsLegacyRun();
	const FIGLeftoverState* State = Narrative ? &Narrative->GetLeftover() : nullptr;
	// 짐 더미는 낮1에 옮겨 온 뒤부터 셋째 밤까지 302호에 있다. 수거차가 간 뒤에는 없다.
	const bool bPile = !bLegacy && (Night == 1 ? HasBeat(BeatDayOneBoxes) || DayOneStage >= EDayOne::Unloading : Night >= 2 && Night <= 3);
	for (const TCHAR* Key : {TEXT("Storage.Pile1"), TEXT("Storage.Pile2"), TEXT("Storage.Pile4"), TEXT("Storage.Pile2Label")})
	{
		ShowProp(FName(Key), bPile);
	}
	const bool bLabel303Peeled = HasBeat(BeatLabel303Peeled);
	ShowProp(FName(TEXT("Storage.Pile2Sticker")), bPile && !bLabel303Peeled);
	SetTargetLive(FindTarget(FName(TEXT("Storage.Label303"))), bPile && !bLabel303Peeled);
	// 오빠 상자: 낮1에는 짐 더미 맨 아래(Pile3 자리), 표시한 뒤에는 북동 구석. 셋째 밤 뒤에는
	// 빼냈으면 403호에, 못 뺐으면 수거차와 함께 갔다.
	const bool bMarked = HasBeat(BeatDayOneMarked);
	const bool bBoxInCorner = !bLegacy && Night == 3 && !HasTaken(DohaBox);
	ShowProp(FName(TEXT("Storage.Pile3")), bPile && !bBoxInCorner && Night <= 2);
	ShowProp(FName(TEXT("Storage.DohaBox")), bBoxInCorner);
	ShowProp(FName(TEXT("Storage.DohaLabel")), bBoxInCorner);
	ShowProp(FName(TEXT("Storage.DohaMark")), bBoxInCorner && bMarked);
	SetTargetLive(FindTarget(FName(TEXT("Storage.DohaName"))), !bHourActive && bPile && Night <= 2 && !bMarked);
	// 셋째 밤의 302호. 판, 바닥 틀, 마대, 조립대.
	const bool bNightThree = !bLegacy && Night == 3 && bHourActive;
	const bool bScene = bNightThree && State && (State->JeongwooState == Trapped || State->JeongwooState == Freed
		|| State->JeongwooState.IsNone());
	ShowProp(FName(TEXT("Storage.Board")), !bLegacy && Night == 3);
	ShowProp(FName(TEXT("Storage.Track")), !bLegacy && Night >= 2);
	const bool bPartsTaken = HasTaken(PieceC) && HasTaken(StorageFrame);
	ShowProp(FName(TEXT("Storage.PartsSack")), !bLegacy && Night == 3 && !bPartsTaken);
	ShowProp(FName(TEXT("Storage.Table1")), !bLegacy && Night >= 3);
	ShowProp(FName(TEXT("Storage.Table2")), !bLegacy && Night >= 3);
	const bool bWedgePlaced = State && State->WedgeSocket == WedgeBrace;
	ShowProp(FName(TEXT("Storage.Wedge")), bNightThree);
	if (UStaticMeshComponent* WedgeMesh = Props.FindRef(FName(TEXT("Storage.Wedge"))))
	{
		if (bWedgePlaced)
		{
			WedgeMesh->SetWorldTransform(FTransform(FRotator(0.0f, 45.0f, 0.0f), WedgeSockets[0] - FVector(0.0f, 0.0f, 2.0f)));
		}
		else if (bWedgeInHand)
		{
			WedgeMesh->SetVisibility(false, true);
		}
		else if (!(State && State->WedgeSocket == WedgeSlipped))
		{
			WedgeMesh->SetWorldTransform(FTransform(FRotator(0.0f, 70.0f, 0.0f), WedgePickup));
		}
	}
	SetTargetLive(FindTarget(FName(TEXT("Storage.WedgePick"))), bNightThree && !bWedgeInHand && !bWedgePlaced);
	const bool bBoardHeld = StorageStage == EStorage::Trapped || StorageStage == EStorage::Fallen;
	for (int32 Socket = 0; Socket < 3; ++Socket)
	{
		SetTargetLive(FindTarget(FName(*FString::Printf(TEXT("Storage.Socket%d"), Socket))), bNightThree && bWedgeInHand && bBoardHeld);
	}
	SetTargetLive(FindTarget(FName(TEXT("Storage.Wardrobe"))), bNightThree && State && !State->bWardrobeMoved);
	SetTargetLive(FindTarget(FName(TEXT("Storage.Lift"))), bNightThree && StorageStage == EStorage::Fallen && bWedgePlaced);
	SetTargetLive(FindTarget(FName(TEXT("Storage.Sack"))), !bLegacy && Night == 3 && !bPartsTaken
		&& (StorageStage == EStorage::Freed || StorageStage == EStorage::Bracing || StorageStage == EStorage::Leaving
			|| StorageStage == EStorage::Gone || (State && State->JeongwooState == SelfEscaped) || !bHourActive));
	SetTargetLive(FindTarget(FName(TEXT("Storage.DohaPull"))), bNightThree && bBoxInCorner
		&& (StorageStage == EStorage::Freed || StorageStage == EStorage::Bracing || StorageStage == EStorage::Leaving));
	SetTargetLive(FindTarget(FName(TEXT("Storage.Jeongwoo"))), bNightThree && (StorageStage == EStorage::Trapped || StorageStage == EStorage::Fallen));
	// 장롱: 한 번 밀면 그 자리다. 숨는 자리도 같이 옮긴다.
	if (Scene)
	{
		if (UStaticMeshComponent* Wardrobe = Scene->GetWardrobe302())
		{
			const bool bMoved = State && State->bWardrobeMoved;
			if (WardrobeMoveStart < 0.0)
			{
				Wardrobe->SetWorldLocation(FVector(-138.0f, bMoved ? -85.0f : 0.0f, StorageZ));
			}
			if (AIGHidingSpot* Hide = Scene->GetWardrobe302HidingSpot())
			{
				Hide->SetActorLocation(FVector(-114.4f, bMoved ? -85.0f : 0.0f, StorageZ));
			}
		}
	}
	for (const FName Id : {GapLarge, GapAlt})
	{
		if (AIGAccessHatch* Hatch = GetHatch(Id))
		{
			const bool bReachable = Id == GapAlt || (State && State->bWardrobeMoved);
			Hatch->SetInteractionEnabled(!bLegacy && Night >= 3 && bReachable);
			const bool bOpen = Narrative && Narrative->HasLeftover(EIGLeftoverList::HatchesOpen, Id);
			if (Hatch->IsOpen() != bOpen && !Hatch->IsObstructed())
			{
				Hatch->ForceOpenState(bOpen);
			}
			Hatch->SetSealed(Narrative && Narrative->HasLeftover(EIGLeftoverList::GapsSealed, Id));
		}
	}
	if (!bHourActive || Night != 3)
	{
		if (BoardPose != 0 && Night != 3)
		{
			SetBoardPose(0, false);
		}
	}
}

void AIGMissingFloorLeftoverDirector::StageStorageTrapForTesting()
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->MarkBeatPlayed(BeatText);
	}
	BeginStorageTrap(false);
}

void AIGMissingFloorLeftoverDirector::BeginStorageTrap(const bool bAnnounce)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Jeongwoo)
	{
		return;
	}
	FIGLeftoverState& State = Narrative->MutableLeftover();
	if (State.JeongwooState.IsNone())
	{
		State.JeongwooState = Trapped;
	}
	if (State.JeongwooState != Trapped)
	{
		return;
	}
	StorageStage = State.WedgeSocket == WedgeBrace && !State.LiningHeldGap.IsNone() && State.LiningHeldGap != GapNarrow
		? EStorage::Released : EStorage::Trapped;
	StorageStageAt = GetWorld()->GetTimeSeconds();
	SetBoardPose(0, false);
	if (Jeongwoo->LoadBody())
	{
		Jeongwoo->SetCarriedBoxes(0);
		Jeongwoo->SetBlocksPlayer(true);
		Jeongwoo->Appear(JeongwooCorner, -135.0f, AIGJeongwooFigure::EPose::Crouch);
	}
	if (StorageStage == EStorage::Released)
	{
		// 받쳐 둔 채 덧댄 것이 이미 옮겨 간 저장이다. 곧 밀고 나온다.
		GetWorldTimerManager().SetTimer(SceneTimer, this, &AIGMissingFloorLeftoverDirector::FreeJeongwoo, 1.2f, false);
	}
	// 갇힌 동안 관리인은 3층을 돌지 않는다. 302호 문간에서 정우를 보는 일이 없게 한다.
	if (Manager)
	{
		Manager->SetAvoidingStorage(true);
	}
	RefreshStorage();
	UE_LOG(LogTemp, Display, TEXT("LEFTOVER_N3 trapped"));
}

void AIGMissingFloorLeftoverDirector::PollStorage()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || IsLegacyRun() || !bHourActive || GetNightIndex() != 3)
	{
		return;
	}
	FIGLeftoverState& State = Narrative->MutableLeftover();
	// 깨면 폰에 정우의 문자가 와 있다(04:12, 신호가 끊기기 전).
	if (Narrative->MarkBeatPlayed(BeatText))
	{
		// 낮에 이름을 들었거나 302호를 같이 보자고 말을 나눈 회차만 번호가 있다.
		const bool bKnown = Narrative->HasBeatPlayed(JeongwooIntroBeat);
		if (bKnown || Narrative->HasBeatPlayed(BeatDayTwoTalk))
		{
			GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this, bKnown]()
			{
				PhoneBuzz();
				AIGHorrorHUD::PushDialogue(this,
					bKnown ? JeongwooName() : NSLOCTEXT("IGMissingFloor", "LeftoverUnknownSender", "202호"),
					NSLOCTEXT("IGMissingFloor", "LeftoverJeongwooText",
						"[04:12] 302호 짐 빼러 들어왔는데 상자가 판에 끼어서 안 빠지네요. 조금만 더 해 보고 갈게요."),
					EIGDialogueChannel::Device, 0.0f, EIGDialoguePriority::Story);
			}), 9.0f, false);
		}
		BeginStorageTrap(false);
		return;
	}
	if (State.JeongwooState == Trapped && StorageStage == EStorage::Idle)
	{
		// 불러온 밤. 갇힌 채 저장됐다.
		BeginStorageTrap(false);
	}
	// 3층에 내려서면 판 너머에서 정우가 부른다.
	if (State.JeongwooState == Trapped && StorageStage == EStorage::Trapped
		&& IsPlayerNear(FVector(-30.0f, -250.0f, StorageZ), 520.0f, 160.0f) && Narrative->MarkBeatPlayed(BeatVoice))
	{
		const FVector Voice = JeongwooCorner + FVector(0.0f, 0.0f, 110.0f);
		PlaySound(UIGToneSequenceSoundWave::CreateKickedPropKnock(this, true), Voice, 0.5f, 0.9f, 120.0f, 1400.0f);
		CaptionIn(IGLeftoverDialogue::Trapped, NSLOCTEXT("IGMissingFloor", "LeftoverBoardKnockCaption", "[302호 안에서 판을 두드리는 소리]"), Voice, 2.6f);
		SayIn(IGLeftoverDialogue::Trapped, StorageVoice(), NSLOCTEXT("IGMissingFloor", "LeftoverTrappedCall", "거기 누구 있어요? 판에 끼었어요."));
	}
}

void AIGMissingFloorLeftoverDirector::HandleStorageTap(AIGLeftoverTarget* Target)
{
	HandleAssemblyTap(Target);
}

void AIGMissingFloorLeftoverDirector::HandleStorageTarget(AIGLeftoverTarget* Target)
{
	if (!Target)
	{
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	FIGLeftoverState& State = Narrative->MutableLeftover();
	const FName Key = Target->Key;
	MarkExamined(Key);
	if (Key == FName(TEXT("Storage.DohaName")))
	{
		// 낮1, 정우가 옮겨 온 짐 더미의 맨 아래 상자.
		Think(NSLOCTEXT("IGMissingFloor", "LeftoverDohaNameThought", "백도하. 오빠 글씨야."), 2.6f);
		if (DayOneStage == EDayOne::Talking && Jeongwoo && Jeongwoo->IsShown())
		{
			Say(YudamName(), NSLOCTEXT("IGMissingFloor", "LeftoverKeepBox", "잠깐만요. 이 상자는 두세요. 오빠 이름이 있어요."));
			Say(JeongwooName(), NSLOCTEXT("IGMissingFloor", "LeftoverMarkBox", "그럼 이건 따로 빼 둘게요. 버리지 말라고 표시해 놓을게요."));
			PlayBeat(BeatDayOneMarked);
			PlaySound(UIGToneSequenceSoundWave::CreateCardboardDrag(this), FVector(-108.0f, -192.0f, StorageZ + 10.0f), 0.5f);
			GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				FinishDayOneBoxes();
			}), 9.0f, false);
		}
		RefreshStorage();
		return;
	}
	if (Key == FName(TEXT("Storage.Jeongwoo")))
	{
		SayIn(IGLeftoverDialogue::Trapped, StorageVoice(), StorageStage == EStorage::Fallen
			? NSLOCTEXT("IGMissingFloor", "LeftoverTrappedFallen", "판이 더 내려왔어요. 밑동이 자꾸 밀려요.")
			: NSLOCTEXT("IGMissingFloor", "LeftoverTrappedTalk", "상자 빼다가 판이 넘어왔어요. 벽에 붙은 게 밀어요."));
		return;
	}
	if (Key == FName(TEXT("Storage.WedgePick")))
	{
		bWedgeInHand = true;
		State.WedgeSocket = NAME_None;
		Take(StorageWedge);
		// 줍는 그 순간의 반응이다. 바로 끼워 넣었으면 밀려서 늦게 뜨지 않는다.
		ThinkIn(IGLeftoverDialogue::Wedge, NSLOCTEXT("IGMissingFloor", "LeftoverWedgePicked", "문 고이는 나무 쐐기야."), 2.4f);
		RefreshStorage();
		return;
	}
	if (Key.ToString().StartsWith(TEXT("Storage.Socket")))
	{
		TryPlaceWedge(Target->Index);
		return;
	}
	if (Key == FName(TEXT("Storage.Wardrobe")))
	{
		PushWardrobe();
		return;
	}
	if (Key == FName(TEXT("Storage.Lift")))
	{
		// 쐐기를 받친 채 판을 다시 세운다. 정우가 안에서 같이 민다.
		SetBoardPose(1, true);
		StorageStage = EStorage::Released;
		PlaySound(UIGToneSequenceSoundWave::CreateBoardStackFall(this), BoardBase + FVector(0.0f, 0.0f, 60.0f), 0.35f, 1.2f);
		GetWorldTimerManager().SetTimer(SceneTimer, this, &AIGMissingFloorLeftoverDirector::FreeJeongwoo, 1.4f, false);
		RefreshStorage();
		return;
	}
	if (Key == FName(TEXT("Storage.Label303")))
	{
		// 지금 사는 집의 짐이다. 같은 말을 해설로 덧붙이지 않는다.
		if (PlayBeat(BeatLabel303Peeled))
		{
			PlaySound(UIGToneSequenceSoundWave::CreatePackingTapeRip(this), FVector(-146.3f, -170.7f, StorageZ + 52.9f), 0.3f, 1.3f);
			Think(NSLOCTEXT("IGMissingFloor", "LeftoverLabel303Thought", "딱지 밑에 303호가 써 붙인 게 있어. 겨울까지만 맡긴다고."), 3.6f);
		}
		RefreshStorage();
		return;
	}
	if (Key == FName(TEXT("Storage.Sack")))
	{
		// 관리실 안쪽 방을 못 봤어도 덮개는 맞출 수 있어야 한다. 버릴 짐을 302호로 옮기면서
		// 떼어 낸 조각도 이 마대에 같이 쓸어 담았다.
		const bool bHadOfficePieces = HasTaken(PieceA) && HasTaken(PieceB);
		Take(PieceA);
		Take(PieceB);
		Take(PieceC);
		Take(StorageFrame);
		PlaySound(UIGToneSequenceSoundWave::CreatePlasticBagSetDown(this), PartsSack + FVector(0.0f, 0.0f, 30.0f), 0.5f);
		Think(bHadOfficePieces
			? NSLOCTEXT("IGMissingFloor", "LeftoverSackThought", "남은 조각 하나랑 금속 틀이야. 관리실에서 떼어 온 거랑 같은 막이 붙어 있어.")
			: NSLOCTEXT("IGMissingFloor", "LeftoverSackAllPieces", "점검구 덮개 조각 셋이랑 금속 틀이야."), 3.6f);
		RefreshStorage();
		RefreshAssembly();
		return;
	}
	if (Key == FName(TEXT("Storage.DohaPull")))
	{
		PullDohaBox();
		return;
	}
	HandleAssemblyTarget(Target);
}

void AIGMissingFloorLeftoverDirector::TryPlaceWedge(const int32 Socket)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !bWedgeInHand)
	{
		return;
	}
	FIGLeftoverState& State = Narrative->MutableLeftover();
	bWedgeInHand = false;
	UStaticMeshComponent* WedgeMesh = Props.FindRef(FName(TEXT("Storage.Wedge")));
	if (Socket == 0)
	{
		// 판 밑동과 바닥 틀이 맞닿는 자리. 쐐기가 물리고 판이 더는 밀리지 않는다.
		State.WedgeSocket = WedgeBrace;
		if (WedgeMesh)
		{
			WedgeMesh->SetVisibility(true, true);
			WedgeMesh->SetWorldTransform(FTransform(FRotator(0.0f, 45.0f, 0.0f), WedgeSockets[0] - FVector(0.0f, 0.0f, 2.0f)));
		}
		PlaySound(UIGToneSequenceSoundWave::CreateKickedPropKnock(this, false), WedgeSockets[0], 0.45f, 1.2f);
		Hush(IGLeftoverDialogue::Wedge);
		ThinkIn(IGLeftoverDialogue::Wedge, NSLOCTEXT("IGMissingFloor", "LeftoverWedgeHeld", "물렸어. 이제 판이 안 밀려."), 2.6f);
	}
	else
	{
		// 바닥이 장판이라 쐐기가 판 밑에서 미끄러져 나온다. 소리 없이도 보인다.
		State.WedgeSocket = WedgeSlipped;
		if (WedgeMesh)
		{
			WedgeMesh->SetVisibility(true, true);
			const FVector Out = WedgeSockets[Socket] + FVector(-16.0f, -16.0f, -2.0f);
			WedgeMesh->SetWorldTransform(FTransform(FRotator(0.0f, 45.0f + 35.0f * Socket, 0.0f), Out));
		}
		PlaySound(UIGToneSequenceSoundWave::CreateKickedPropKnock(this, true), WedgeSockets[Socket], 0.35f, 1.4f);
		ThinkIn(IGLeftoverDialogue::Wedge, WedgeSlips++ == 0
			? NSLOCTEXT("IGMissingFloor", "LeftoverWedgeSlip1", "장판에서 미끄러져 빠졌어.")
			: NSLOCTEXT("IGMissingFloor", "LeftoverWedgeSlip2", "또 빠졌어. 장판 위라 걸릴 데가 없어."), 3.0f);
		// 빠진 쐐기는 그 자리에서 다시 줍는다.
		if (AIGLeftoverTarget* Pick = FindTarget(FName(TEXT("Storage.WedgePick"))))
		{
			Pick->SetActorLocation(WedgeSockets[Socket] + FVector(-16.0f, -16.0f, 2.0f));
		}
	}
	RefreshStorage();
}

void AIGMissingFloorLeftoverDirector::PushWardrobe()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Scene || Narrative->GetLeftover().bWardrobeMoved)
	{
		return;
	}
	Narrative->MutableLeftover().bWardrobeMoved = true;
	UStaticMeshComponent* Wardrobe = Scene->GetWardrobe302();
	if (Wardrobe)
	{
		WardrobeFrom = Wardrobe->GetComponentLocation();
		WardrobeMoveStart = GetWorld()->GetTimeSeconds();
		PlaySound(UIGToneSequenceSoundWave::CreateCardboardDrag(this), WardrobeFrom + FVector(0.0f, 0.0f, 20.0f), 0.85f, 0.6f, 200.0f, 1800.0f);
		Caption(NSLOCTEXT("IGMissingFloor", "LeftoverWardrobeCaption", "[장롱 다리가 장판을 긁으며 밀리는 소리]"), WardrobeFrom, 2.6f);
		GetWorldTimerManager().SetTimer(WardrobeTimer, this, &AIGMissingFloorLeftoverDirector::TickWardrobeMove, 1.0f / 60.0f, true);
	}
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoiseUnmasked(WardrobeFrom, 0.45f, this);
		}
	}
	RefreshStorage();
}

void AIGMissingFloorLeftoverDirector::TickWardrobeMove()
{
	// 끝나면 자기 타이머를 지우고 멤버를 계속 쓴다. 람다로 걸면 지우는 순간 람다 본문이 해제돼서
	// 그 뒤 줄이 해제된 메모리를 읽는다(배포본에서 크래시). 그래서 멤버 함수로 건다.
	UStaticMeshComponent* Moving = Scene ? Scene->GetWardrobe302() : nullptr;
	if (!Moving)
	{
		GetWorldTimerManager().ClearTimer(WardrobeTimer);
		WardrobeMoveStart = -1.0;
		return;
	}
	const float Alpha = FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - WardrobeMoveStart) / 1.8), 0.0f, 1.0f);
	Moving->SetWorldLocation(FMath::Lerp(WardrobeFrom, FVector(-138.0f, -85.0f, StorageZ), Alpha * Alpha * (3.0f - 2.0f * Alpha)));
	if (Alpha >= 1.0f)
	{
		GetWorldTimerManager().ClearTimer(WardrobeTimer);
		WardrobeMoveStart = -1.0;
		// 정우가 아직 판 뒤에 있을 때의 발견이다. 그를 꺼낸 뒤에는 이 말이 뒤늦게 뜨지 않는다.
		ThinkIn(IGLeftoverDialogue::Trapped, NSLOCTEXT("IGMissingFloor", "LeftoverWardrobeHatch", "장롱 뒤에 점검구가 있었어. 저쪽 틈보다 훨씬 커."), 3.4f);
		RefreshStorage();
	}
}

void AIGMissingFloorLeftoverDirector::ReleaseBoard()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || StorageStage != EStorage::Trapped)
	{
		return;
	}
	const bool bBraced = Narrative->GetLeftover().WedgeSocket == WedgeBrace;
	StorageStageAt = GetWorld()->GetTimeSeconds();
	if (bBraced)
	{
		// 쐐기가 밑동을 잡고 있다. 판은 그대로 서 있고 정우가 밀고 나온다.
		StorageStage = EStorage::Released;
		SayIn(IGLeftoverDialogue::Trapped, StorageVoice(), NSLOCTEXT("IGMissingFloor", "LeftoverReleasedBraced", "어, 판이 풀렸어요. 밀어 볼게요."));
		GetWorldTimerManager().SetTimer(SceneTimer, this, &AIGMissingFloorLeftoverDirector::FreeJeongwoo, 2.2f, false);
	}
	else
	{
		// 받친 게 없다. 누르던 몸이 떠나자 밑동이 미끄러지며 판이 더 눕는다. 소리가 크다.
		StorageStage = EStorage::Fallen;
		SetBoardPose(2, true);
		PlaySound(UIGToneSequenceSoundWave::CreateBoardStackFall(this), BoardBase + FVector(0.0f, 0.0f, 50.0f), 0.95f, 0.95f, 200.0f, 2200.0f);
		Caption(NSLOCTEXT("IGMissingFloor", "LeftoverBoardFallCaption", "[큰 판이 미끄러지며 쾅 하고 눕는 소리]"), BoardBase, 2.8f);
		if (UWorld* World = GetWorld())
		{
			if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
			{
				Noise->ReportNoiseUnmasked(BoardBase, 0.85f, this);
			}
		}
		SayIn(IGLeftoverDialogue::Trapped, StorageVoice(), NSLOCTEXT("IGMissingFloor", "LeftoverFallenLine", "괜찮아요! 근데 판이 더 내려왔어요."));
		GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			ThinkIn(IGLeftoverDialogue::Trapped, NSLOCTEXT("IGMissingFloor", "LeftoverFallenThought", "밑동이 쭉 미끄러졌어. 판이 더 누웠어."), 3.4f);
		}), 2.6f, false);
	}
	RefreshStorage();
}

void AIGMissingFloorLeftoverDirector::FreeJeongwoo()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Jeongwoo || (StorageStage != EStorage::Released))
	{
		return;
	}
	StorageStage = EStorage::Freed;
	StorageStageAt = GetWorld()->GetTimeSeconds();
	Narrative->MutableLeftover().JeongwooState = Freed;
	Hush(IGLeftoverDialogue::Trapped);
	SetBoardPose(3, true);
	PlaySound(UIGToneSequenceSoundWave::CreateCardboardDrag(this), DohaBoxUnderBoard.GetLocation() + FVector(0.0f, 0.0f, 20.0f), 0.6f, 0.8f);
	Jeongwoo->SetBlocksPlayer(false);
	Jeongwoo->Appear(JeongwooCorner, -135.0f, AIGJeongwooFigure::EPose::Idle);
	GetWorldTimerManager().SetTimer(JeongwooWalkTimer, this, &AIGMissingFloorLeftoverDirector::WalkJeongwooOut, 1.25f, false);
	GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (StorageStage != EStorage::Freed)
		{
			// 그 사이 상자를 당겨 받치러 갔다. 인사는 걸어가며 짧게만 한다.
			SayIn(IGLeftoverDialogue::Freed, JeongwooName(), NSLOCTEXT("IGMissingFloor", "LeftoverFreedLine", "살았다… 고마워요."), true);
			return;
		}
		const bool bBoxLeft = !HasTaken(DohaBox);
		if (PlayBeat(JeongwooIntroBeat))
		{
			// 낮에 한 번도 못 만났다. 판에서 나와서야 이름을 댄다.
			SayIn(IGLeftoverDialogue::Freed, JeongwooName(), NSLOCTEXT("IGMissingFloor", "LeftoverFreedIntro", "살았다… 고마워요. 저 202호 정우예요."), true);
		}
		else
		{
			SayIn(IGLeftoverDialogue::Freed, JeongwooName(), NSLOCTEXT("IGMissingFloor", "LeftoverFreedLine", "살았다… 고마워요."), true);
		}
		if (Jeongwoo)
		{
			Jeongwoo->SetBlocksPlayer(true);
		}
		// 말이 다 끝나면 문으로 간다. 그 사이 상자를 당기면 판부터 같이 받친다(JeongwooLeave가 받치는 중이면 하지 않는다).
		const auto GoToDoor = [this]()
		{
			if (StorageStage == EStorage::Freed)
			{
				GetWorldTimerManager().SetTimer(SceneTimer, this, &AIGMissingFloorLeftoverDirector::JeongwooLeave, 0.6f, false);
			}
		};
		if (bBoxLeft)
		{
			SayThen(IGLeftoverDialogue::Freed, JeongwooName(),
				NSLOCTEXT("IGMissingFloor", "LeftoverFreedBoxLine", "그 상자… 아직 판 밑에 걸려 있어요."), 8.0f, GoToDoor);
		}
		else
		{
			FTimerHandle Wait;
			GetWorldTimerManager().SetTimer(Wait, FTimerDelegate::CreateWeakLambda(this, GoToDoor), 3.2f, false);
		}
	}), 2.4f, false);
	RefreshStorage();
	UE_LOG(LogTemp, Display, TEXT("LEFTOVER_N3 freed"));
}

void AIGMissingFloorLeftoverDirector::WalkJeongwooOut()
{
	if (!Jeongwoo || StorageStage != EStorage::Freed)
	{
		return;
	}
	// 바닥 틀 끝(61, 52)과 밀려난 판 서쪽 끝(24, 29) 사이를 지난다. 둘 다 20 cm 넘게 비킨다.
	Jeongwoo->WalkRoute({JeongwooCorner, FVector(70.0f, 46.0f, StorageZ), FVector(35.0f, 47.0f, StorageZ), FVector(5.0f, 50.0f, StorageZ), JeongwooOutSpot},
		false, AIGJeongwooFigure::EPose::Idle, 80.0f);
}

void AIGMissingFloorLeftoverDirector::PullDohaBox()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Jeongwoo)
	{
		return;
	}
	if (StorageStage == EStorage::Freed || StorageStage == EStorage::Leaving)
	{
		// 판을 받쳐 줄 사람이 있다. 정우가 판 앞으로 간다(문을 잡고 있었으면 돌아온다).
		GetWorldTimerManager().ClearTimer(SceneTimer);
		TArray<FVector> Route{Jeongwoo->GetFeet()};
		// 문을 잡고 기다린다던 말은 이제 맞지 않는다.
		Hush(IGLeftoverDialogue::DoorWait);
		if (StorageStage == EStorage::Leaving)
		{
			SayIn(IGLeftoverDialogue::Brace, JeongwooName(), NSLOCTEXT("IGMissingFloor", "LeftoverBraceAgain", "상자요? 잠깐만요, 판 잡을게요."));
			// 복도의 문설주 옆에서 돌아온다. 문간 가운데로 들어와야 벽 모서리를 안 뚫는다.
			Route.Append({FVector(-30.0f, -252.0f, StorageZ), FVector(-30.0f, -200.0f, StorageZ)});
		}
		StorageStage = EStorage::Bracing;
		StorageStageAt = GetWorld()->GetTimeSeconds();
		// 마지막 15 cm는 판을 보고 걸어 들어온다(도착하면 판 쪽 70도로 선다).
		Route.Append({JeongwooBraceSpot - FVector(5.1f, 14.1f, 0.0f), JeongwooBraceSpot});
		Jeongwoo->WalkRoute(Route, false, AIGJeongwooFigure::EPose::Brace, 90.0f);
		RefreshStorage();
		return;
	}
	if (StorageStage != EStorage::Bracing)
	{
		return;
	}
	++DohaBoxPulls;
	// 당길 때마다 받친 시간을 다시 센다.
	StorageStageAt = GetWorld()->GetTimeSeconds();
	if (DohaBoxPulls == 1)
	{
		PlaySound(UIGToneSequenceSoundWave::CreateCardboardDrag(this), PushedAside(DohaBoxUnderBoard).GetLocation() + FVector(0.0f, 0.0f, 15.0f), 0.55f, 0.9f);
		SayIn(IGLeftoverDialogue::BoxStuck, YudamName(), NSLOCTEXT("IGMissingFloor", "LeftoverBoxStuck", "잠깐만요. 상자가 걸렸어요."));
		SayIn(IGLeftoverDialogue::BoxStuck, JeongwooName(), NSLOCTEXT("IGMissingFloor", "LeftoverBoxStuckReply", "모서리가 판에 걸렸어요. 조금만 더요."));
		return;
	}
	// 빠졌다. 걸렸다는 말과 받치는 동안의 말은 이제 지난 일이다. 안에 오빠의 공구 주머니가 있다.
	Hush(IGLeftoverDialogue::BoxStuck);
	Hush(IGLeftoverDialogue::Brace);
	Take(DohaBox);
	Take(ToolPouch);
	AIGNightThreatDirector::HoldQuietAfterDiscovery(this, 60.0f);
	PlaySound(UIGToneSequenceSoundWave::CreateToolRollClink(this), PushedAside(DohaBoxUnderBoard).TransformPosition(FVector(-40.0f, 0.0f, 20.0f)), 0.55f);
	Think(NSLOCTEXT("IGMissingFloor", "LeftoverPouchThought", "오빠 공구 주머니야. 집에 가져가자."), 3.2f);
	StorageStage = EStorage::Freed;
	Jeongwoo->WalkRoute({Jeongwoo->GetFeet(), JeongwooOutSpot}, false, AIGJeongwooFigure::EPose::Idle, 90.0f);
	GetWorldTimerManager().SetTimer(SceneTimer, this, &AIGMissingFloorLeftoverDirector::JeongwooLeave, 3.0f, false);
	RefreshStorage();
}

void AIGMissingFloorLeftoverDirector::JeongwooLeave()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Jeongwoo || (StorageStage != EStorage::Freed && StorageStage != EStorage::Bracing))
	{
		return;
	}
	// 정우가 먼저 302호 문으로 가서 문을 잡고 기다린다. 그녀가 문을 지나 나오면 같이 나간 것이고,
	// 남아서 더 보면 기다리다가 먼저 내려간다. 상자를 당기면 돌아와 판을 받친다.
	StorageStage = EStorage::Leaving;
	StorageStageAt = GetWorld()->GetTimeSeconds();
	Hush(IGLeftoverDialogue::Brace);
	const bool bBoxLeft = !HasTaken(DohaBox);
	SayIn(IGLeftoverDialogue::DoorWait, JeongwooName(), bBoxLeft
		? NSLOCTEXT("IGMissingFloor", "LeftoverLeaveBoxLine", "문 잡고 있을게요. 상자 뺄 거면 말해요, 판 잡아 줄게요.")
		: NSLOCTEXT("IGMissingFloor", "LeftoverLeaveLine", "문 잡고 있을게요. 더 볼 거 있으면 보고 나와요."), true);
	Jeongwoo->WalkRoute({Jeongwoo->GetFeet(), FVector(-30.0f, -120.0f, StorageZ), FVector(-30.0f, -222.0f, StorageZ),
		FVector(-30.0f, -252.0f, StorageZ), JeongwooDoorSpot}, false, AIGJeongwooFigure::EPose::HoldDoor, 100.0f);
	RefreshStorage();
}

void AIGMissingFloorLeftoverDirector::JeongwooStopBracing()
{
	if (StorageStage != EStorage::Bracing || !Jeongwoo)
	{
		return;
	}
	Hush(IGLeftoverDialogue::Brace);
	SayIn(IGLeftoverDialogue::DoorWait, JeongwooName(), NSLOCTEXT("IGMissingFloor", "LeftoverBraceRelease", "판 놓을게요. 상자는 이따 다시 해요."));
	JeongwooLeave();
}

void AIGMissingFloorLeftoverDirector::JeongwooGoDownstairs(const bool bTogether)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Jeongwoo)
	{
		return;
	}
	if (Narrative)
	{
		// 같이 나왔든 먼저 내려갔든 그녀가 꺼내 준 것이다. 낮의 문자와 결말이 이 상태를 본다.
		Narrative->MutableLeftover().JeongwooState = LeftTogether;
		if (bTogether)
		{
			Narrative->MarkBeatPlayed(BeatOut);
		}
	}
	StorageStage = EStorage::Gone;
	// 문을 잡고 있겠다는 말과 받치는 말은 이제 지난 일이다.
	Hush(IGLeftoverDialogue::DoorWait);
	Hush(IGLeftoverDialogue::Brace);
	Say(JeongwooName(), bTogether
		? NSLOCTEXT("IGMissingFloor", "LeftoverByeLine", "짐은 낮에 마저 뺄게요. 들어가세요.")
		: NSLOCTEXT("IGMissingFloor", "LeftoverGoAheadLine", "저 먼저 내려갈게요. 문은 열어 둘게요. 오래 있지 마요."));
	Jeongwoo->WalkRoute({Jeongwoo->GetFeet(), FVector(-120.0f, -300.0f, StorageZ), AIGPrologueWorldScene::GetStairDoorwayFeet(2)},
		false, AIGJeongwooFigure::EPose::Idle, 110.0f);
	if (Manager)
	{
		Manager->SetAvoidingStorage(false);
	}
	RefreshStorage();
}

void AIGMissingFloorLeftoverDirector::UpdateJeongwooArrival()
{
	if (!Jeongwoo || !bHourActive)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// 구출 뒤 말이 끝났는데 문으로 가는 타이머가 없다(불러온 밤 등). 곧장 문으로 간다.
	if (StorageStage == EStorage::Freed && !Jeongwoo->IsWalking() && Now - StorageStageAt > 12.0
		&& !GetWorldTimerManager().IsTimerActive(SceneTimer))
	{
		JeongwooLeave();
		return;
	}
	if (StorageStage == EStorage::Bracing && !Jeongwoo->IsWalking())
	{
		// 판을 받친 채 그녀가 방을 나갔거나 오래 안 당겼다. 받치는 장면이 밤 내내 남지 않게 판을 놓는다.
		if (!StorageRoomBoxContainsPlayer() || Now - StorageStageAt > BraceWaitSeconds)
		{
			JeongwooStopBracing();
		}
		return;
	}
	if (StorageStage != EStorage::Leaving || Jeongwoo->IsWalking())
	{
		return;
	}
	// 그녀가 302호를 나와 복도에 서면 같이 나온 것이다. 남아서 더 보면 기다리다가 먼저 내려간다.
	if (!StorageRoomBoxContainsPlayer())
	{
		JeongwooGoDownstairs(true);
	}
	else if (Now - StorageStageAt > DoorWaitSeconds)
	{
		JeongwooGoDownstairs(false);
	}
}

bool AIGMissingFloorLeftoverDirector::StorageRoomBoxContainsPlayer() const
{
	const FVector Feet = GetPlayerFeet();
	return Feet.X > -165.0f && Feet.X < 125.0f && Feet.Y > -218.0f && Feet.Y < 90.0f
		&& Feet.Z > StorageZ - 30.0f && Feet.Z < StorageZ + 120.0f;
}

void AIGMissingFloorLeftoverDirector::SelfEscapeAtDawn()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || GetNightIndex() != 3)
	{
		return;
	}
	FIGLeftoverState& State = Narrative->MutableLeftover();
	if (State.JeongwooState == Trapped || State.JeongwooState == Freed)
	{
		// 새벽이 오며 누르던 것이 풀렸다. 판이 이미 풀려 밀고 나오던 참(Released)이면 그녀가 꺼낸 것이고,
		// 판 뒤에 갇혀 있었으면 정우는 혼자 판을 밀고 나갔다.
		const bool bRescued = State.JeongwooState == Freed || StorageStage == EStorage::Released;
		State.JeongwooState = bRescued ? LeftTogether : SelfEscaped;
	}
	StorageStage = EStorage::Idle;
	if (Jeongwoo)
	{
		Jeongwoo->Vanish();
	}
	if (Manager)
	{
		Manager->SetAvoidingStorage(false);
	}
}

// -- 덮개 조립(P6) ------------------------------------------------------------------

void AIGMissingFloorLeftoverDirector::BuildAssembly()
{
	FrameRoot = NewObject<USceneComponent>(this, TEXT("CoverFrameRoot"));
	FrameRoot->SetupAttachment(PropRoot);
	FrameRoot->SetMobility(EComponentMobility::Movable);
	FrameRoot->RegisterComponent();
	FrameRoot->SetWorldLocationAndRotation(AssemblyCenter, FrameLyingQuat());
	const auto MakeMesh = [this](const TCHAR* Name, const TCHAR* MeshName, USceneComponent* Parent)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, Name);
		Component->SetupAttachment(Parent);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh(MeshName));
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		Component->bDisallowNanite = true;
		Component->RegisterComponent();
		return Component;
	};
	MakeMesh(TEXT("CoverFrameMesh"), TEXT("SM_CoverFrame"), FrameRoot);
	for (int32 Clamp = 0; Clamp < 4; ++Clamp)
	{
		UStaticMeshComponent* Component = MakeMesh(*FString::Printf(TEXT("CoverClamp%d"), Clamp), TEXT("SM_CoverClamp"), FrameRoot);
		Clamps.Add(Component);
	}
	const TCHAR* PieceNames[3] = {TEXT("SM_CoverPieceA"), TEXT("SM_CoverPieceB"), TEXT("SM_CoverPieceC")};
	for (int32 Piece = 0; Piece < 3; ++Piece)
	{
		UStaticMeshComponent* Component = MakeMesh(*FString::Printf(TEXT("CoverPiece%d"), Piece), PieceNames[Piece], PropRoot);
		PieceMeshes.Add(Component);
		if (Piece < 2)
		{
			UStaticMeshComponent* Flap = MakeMesh(*FString::Printf(TEXT("CoverFlap%d"), Piece),
				Piece == 0 ? TEXT("SM_CoverFlapA") : TEXT("SM_CoverFlapB"), Component);
			Flap->SetRelativeLocation(FVector(0.0f, 0.0f, 35.0f));
			FlapMeshes.Add(Flap);
		}
	}
	for (int32 Clamp = 0; Clamp < 4; ++Clamp)
	{
		SetClamp(Clamp, 0);
	}
	const auto Bind = [this](AIGLeftoverTarget* Target, const bool bTap)
	{
		if (!Target)
		{
			return;
		}
		Target->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleStorageTarget);
		if (bTap)
		{
			Target->SetTapAction(true);
			Target->OnTapped.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleStorageTap);
		}
	};
	for (int32 Piece = 0; Piece < 3; ++Piece)
	{
		if (AIGLeftoverTarget* Target = SpawnTarget(FName(*FString::Printf(TEXT("Assembly.Pick%d"), Piece)), PieceFloor[Piece] + FVector(0.0f, 0.0f, 3.0f),
				FRotator::ZeroRotator, FVector(20.0f, 22.0f, 4.0f), NSLOCTEXT("IGMissingFloor", "LeftoverPiecePickPrompt", "조각 집어 들기"), 0.0f, 0.03f))
		{
			Target->Index = Piece;
			Bind(Target, false);
		}
	}
	if (AIGLeftoverTarget* Held = SpawnTarget(FName(TEXT("Assembly.Held")), AssemblyHover, FRotator::ZeroRotator,
			FVector(22.0f, 22.0f, 14.0f), NSLOCTEXT("IGMissingFloor", "LeftoverHeldPrompt", "돌려 보기 · 길게 눌러 뒤집기"), 0.55f, 0.02f))
	{
		Bind(Held, true);
	}
	const FVector2D SlotCenter[3] = {FVector2D(-14.5f, 0.0f), FVector2D(14.0f, 17.5f), FVector2D(14.0f, -17.5f)};
	const FVector2D SlotHalf[3] = {FVector2D(15.0f, 34.0f), FVector2D(15.0f, 16.0f), FVector2D(15.0f, 16.0f)};
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		// 누운 틀에서 덮개 Y는 월드 Y, 덮개 Z는 월드 X다.
		const FVector World = AssemblyCenter + FVector(SlotCenter[Slot].Y, SlotCenter[Slot].X, 3.0f);
		if (AIGLeftoverTarget* Target = SpawnTarget(FName(*FString::Printf(TEXT("Assembly.Slot%d"), Slot)), World,
				FRotator::ZeroRotator, FVector(SlotHalf[Slot].Y, SlotHalf[Slot].X, 2.0f),
				NSLOCTEXT("IGMissingFloor", "LeftoverSlotPrompt", "테두리에 대 보기"), 0.0f, 0.04f))
		{
			Target->Index = Slot;
			Bind(Target, false);
		}
	}
	Bind(SpawnTarget(FName(TEXT("Assembly.Flip")), AssemblyCenter + FVector(0.0f, 0.0f, 4.0f), FRotator::ZeroRotator,
		FVector(36.0f, 32.0f, 3.0f), NSLOCTEXT("IGMissingFloor", "LeftoverAssemblyFlipPrompt", "맞춘 덮개 뒤집어 보기"), 0.6f, 0.06f), false);
	Bind(SpawnTarget(FName(TEXT("Assembly.Membrane")), AssemblyCenter + FVector(29.0f, 0.0f, 4.0f), FRotator::ZeroRotator,
		FVector(8.0f, 30.0f, 3.0f), NSLOCTEXT("IGMissingFloor", "LeftoverMembranePrompt", "방수막 들춰 보기"), 0.9f, 0.05f), false);
	Bind(SpawnTarget(FName(TEXT("Assembly.Take")), AssemblyCenter + FVector(0.0f, 0.0f, 5.0f), FRotator::ZeroRotator,
		FVector(36.0f, 32.0f, 4.0f), NSLOCTEXT("IGMissingFloor", "LeftoverCoverTakePrompt", "덮개 챙기기"), 0.0f, 0.06f), false);
	// 5층 원래 자리. 덧판 앞에서 대 본다.
	if (AIGLeftoverTarget* Site = SpawnTarget(FName(TEXT("Annex.SiteMatch")), SiteMatchSpot, FRotator::ZeroRotator,
			FVector(4.0f, 32.0f, 45.0f), NSLOCTEXT("IGMissingFloor", "LeftoverSitePrompt", "떼어 낸 덮개 대 보기"), 1.0f, 0.06f))
	{
		Site->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleSiteMatch);
	}
	// 대 본 뒤 벽 밑에 기대 둔 덮개. 틀과 조각 셋이 한 장으로 맞춰져 있다.
	const FTransform Lean(FRotator(-12.0f, 180.0f, 0.0f), SiteLeanSpot + FVector(0.0f, 0.0f, 37.0f));
	SpawnProp(FName(TEXT("Annex.CoverLean")), Mesh(TEXT("SM_CoverFrame")), Lean, false);
	SpawnProp(FName(TEXT("Annex.CoverLeanA")), Mesh(TEXT("SM_CoverPieceA")), Lean, false);
	SpawnProp(FName(TEXT("Annex.CoverLeanB")), Mesh(TEXT("SM_CoverPieceB")), Lean, false);
	SpawnProp(FName(TEXT("Annex.CoverLeanC")), Mesh(TEXT("SM_CoverPieceC")), Lean, false);
}

FTransform AIGMissingFloorLeftoverDirector::PieceSlotTransform(const int32 Piece) const
{
	return FrameRoot ? FrameRoot->GetComponentTransform() : FTransform::Identity;
}

FTransform AIGMissingFloorLeftoverDirector::PieceHoverTransform(const int32 Piece) const
{
	const FQuat Base = FrameLyingQuat();
	const FQuat StateQ = CoverStateQuat(HeldRotation, bHeldFlipped);
	const FQuat World = Base * StateQ;
	const FVector Offset = World.RotateVector(-PieceCentroid[FMath::Clamp(Piece, 0, 2)]);
	return FTransform(World, AssemblyHover + Offset);
}

void AIGMissingFloorLeftoverDirector::SetClamp(const int32 Clamp, const int32 Pose)
{
	if (!Clamps.IsValidIndex(Clamp) || !Clamps[Clamp])
	{
		return;
	}
	// 기둥에서 구멍 쪽(안쪽 대각선)이 맞물린 자세, 그 반대가 비켜 둔 자세. 들뜬 것은 끝이 들린다.
	const FVector2D Post = ClampPost[Clamp];
	const FVector2D Hole = ClampHole[Clamp];
	const FVector2D Dir = (Hole - Post).GetSafeNormal();
	float Angle = FMath::Atan2(Dir.Y, Dir.X);
	if (Pose == 0)
	{
		Angle += PI;
	}
	FQuat Q = FQuat(FVector::XAxisVector, Angle);
	if (Pose == 2)
	{
		Q = Q * FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(24.0f));
	}
	Clamps[Clamp]->SetRelativeLocationAndRotation(FVector(-2.2f, Post.X, Post.Y), Q);
}

void AIGMissingFloorLeftoverDirector::RefreshAssembly()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const int32 Night = GetNightIndex();
	const bool bLegacy = IsLegacyRun();
	const FIGLeftoverState* State = Narrative ? &Narrative->GetLeftover() : nullptr;
	const bool bTable = !bLegacy && Night >= 3 && State;
	const bool bHaveFrame = HasTaken(StorageFrame);
	const int32 Stage = State ? State->AssemblyStage : 0;
	const bool bTaken = HasTaken(Cover);
	const bool bPlaced[3] = {
		HasBeat(FName(TEXT("Leftover.Assembly.A"))), HasBeat(FName(TEXT("Leftover.Assembly.B"))), HasBeat(FName(TEXT("Leftover.Assembly.C")))};
	const FName PieceIds[3] = {PieceA, PieceB, PieceC};
	if (FrameRoot)
	{
		const bool bShowFrame = bTable && bHaveFrame && !bTaken;
		FrameRoot->SetVisibility(bShowFrame, true);
		FrameRoot->SetHiddenInGame(!bShowFrame, true);
		if (Stage >= 2 || (Stage >= 1 && HasBeat(FName(TEXT("Leftover.Assembly.Flipped")))))
		{
			// 뒤집어 앞면이 위다.
			FrameRoot->SetWorldLocationAndRotation(AssemblyCenter + FVector(0.0f, 0.0f, 1.4f),
				FrameLyingQuat() * FQuat(FVector::ZAxisVector, PI));
		}
		else if (FitFeedbackUntil <= 0.0)
		{
			FrameRoot->SetWorldLocationAndRotation(AssemblyCenter, FrameLyingQuat());
		}
	}
	for (int32 Piece = 0; Piece < 3; ++Piece)
	{
		UStaticMeshComponent* PieceMesh = PieceMeshes.IsValidIndex(Piece) ? PieceMeshes[Piece].Get() : nullptr;
		if (!PieceMesh)
		{
			continue;
		}
		const bool bHave = HasTaken(PieceIds[Piece]);
		const bool bShow = bTable && bHave && !bTaken;
		PieceMesh->SetVisibility(bShow, true);
		PieceMesh->SetHiddenInGame(!bShow, true);
		if (!bShow)
		{
			continue;
		}
		if (bPlaced[Piece] && FrameRoot)
		{
			if (PieceMesh->GetAttachParent() != FrameRoot)
			{
				PieceMesh->AttachToComponent(FrameRoot, FAttachmentTransformRules::KeepWorldTransform);
			}
			PieceMesh->SetRelativeTransform(FTransform::Identity);
		}
		else if (Piece == HeldPiece)
		{
			if (PieceMesh->GetAttachParent() != PropRoot)
			{
				PieceMesh->AttachToComponent(PropRoot, FAttachmentTransformRules::KeepWorldTransform);
			}
			if (FitFeedbackPiece != Piece)
			{
				PieceMesh->SetWorldTransform(PieceHoverTransform(Piece));
			}
		}
		else
		{
			if (PieceMesh->GetAttachParent() != PropRoot)
			{
				PieceMesh->AttachToComponent(PropRoot, FAttachmentTransformRules::KeepWorldTransform);
			}
			// 바닥에 누운 조각. 처음 놓인 자세(앞면이 위, 제각각 돌아간 채)다.
			const FQuat Lying = FrameLyingQuat() * CoverStateQuat(PieceRotation[Piece], bPieceFlipped[Piece]);
			const FVector Floor = PieceFloor[Piece] + Lying.RotateVector(-PieceCentroid[Piece]) + FVector(0.0f, 0.0f, 0.6f);
			PieceMesh->SetWorldTransform(FTransform(Lying, Floor));
		}
	}
	for (int32 Clamp = 0; Clamp < 4; ++Clamp)
	{
		SetClamp(Clamp, bPlaced[ClampPiece[Clamp]] ? 1 : 0);
	}
	if (FlapMeshes.Num() >= 2)
	{
		const bool bLifted = Stage >= 2 && !bTaken;
		for (UStaticMeshComponent* Flap : FlapMeshes)
		{
			if (Flap && MembraneLiftStart < 0.0)
			{
				Flap->SetRelativeRotation(bLifted ? FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-120.0f)) : FQuat::Identity);
			}
		}
	}
	const bool bAllPlaced = bPlaced[0] && bPlaced[1] && bPlaced[2];
	const bool bAtTable = bTable && bHaveFrame && !bTaken;
	for (int32 Piece = 0; Piece < 3; ++Piece)
	{
		SetTargetLive(FindTarget(FName(*FString::Printf(TEXT("Assembly.Pick%d"), Piece))),
			bAtTable && HasTaken(PieceIds[Piece]) && !bPlaced[Piece] && Piece != HeldPiece);
	}
	SetTargetLive(FindTarget(FName(TEXT("Assembly.Held"))), bAtTable && HeldPiece != INDEX_NONE && FitFeedbackPiece == INDEX_NONE);
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		SetTargetLive(FindTarget(FName(*FString::Printf(TEXT("Assembly.Slot%d"), Slot))),
			bAtTable && HeldPiece != INDEX_NONE && !bPlaced[Slot] && FitFeedbackPiece == INDEX_NONE);
	}
	const bool bFlipped = HasBeat(FName(TEXT("Leftover.Assembly.Flipped")));
	SetTargetLive(FindTarget(FName(TEXT("Assembly.Flip"))), bAtTable && bAllPlaced && !bFlipped);
	SetTargetLive(FindTarget(FName(TEXT("Assembly.Membrane"))), bAtTable && bAllPlaced && bFlipped && Stage < 2);
	SetTargetLive(FindTarget(FName(TEXT("Assembly.Take"))), bAtTable && Stage >= 2);
	// 5층 대 보기와 기대 둔 덮개.
	const bool bMatched = State && State->bSiteMatched;
	SetTargetLive(FindTarget(FName(TEXT("Annex.SiteMatch"))), !bLegacy && bTaken && !bMatched && IsInspectionOverlayOn());
	for (const TCHAR* Key : {TEXT("Annex.CoverLean"), TEXT("Annex.CoverLeanA"), TEXT("Annex.CoverLeanB"), TEXT("Annex.CoverLeanC")})
	{
		ShowProp(FName(Key), !bLegacy && bMatched);
	}
}

void AIGMissingFloorLeftoverDirector::HandleAssemblyTap(AIGLeftoverTarget* Target)
{
	if (!Target || Target->Key != FName(TEXT("Assembly.Held")) || HeldPiece == INDEX_NONE)
	{
		return;
	}
	// 짧게 누르면 90도 돌린다.
	HeldRotation = (HeldRotation + 1) & 3;
	PieceRotation[HeldPiece] = HeldRotation;
	PlaySound(UIGToneSequenceSoundWave::CreatePickupRustle(this), AssemblyHover, 0.3f, 1.2f);
	RefreshAssembly();
}

void AIGMissingFloorLeftoverDirector::HoldPiece(const int32 Piece)
{
	if (Piece < 0 || Piece > 2)
	{
		return;
	}
	if (HeldPiece != INDEX_NONE)
	{
		PieceRotation[HeldPiece] = HeldRotation;
		bPieceFlipped[HeldPiece] = bHeldFlipped;
	}
	HeldPiece = Piece;
	HeldRotation = PieceRotation[Piece];
	bHeldFlipped = bPieceFlipped[Piece];
	PlaySound(UIGToneSequenceSoundWave::CreatePickupRustle(this), AssemblyHover, 0.35f, 1.0f);
	RefreshAssembly();
}

void AIGMissingFloorLeftoverDirector::HandleAssemblyTarget(AIGLeftoverTarget* Target)
{
	if (!Target)
	{
		return;
	}
	const FName Key = Target->Key;
	const FString KeyString = Key.ToString();
	if (KeyString.StartsWith(TEXT("Assembly.Pick")))
	{
		HoldPiece(Target->Index);
		if (PlayBeat(FName(TEXT("Leftover.Assembly.FirstPick"))))
		{
			Think(NSLOCTEXT("IGMissingFloor", "LeftoverFirstPickThought", "조각 셋에 틀 하나."), 2.6f);
		}
		return;
	}
	if (Key == FName(TEXT("Assembly.Held")))
	{
		// 길게 누르면 뒤집는다(앞면 방수막 ↔ 뒷면 나사 구멍).
		bHeldFlipped = !bHeldFlipped;
		bPieceFlipped[HeldPiece] = bHeldFlipped;
		PlaySound(UIGToneSequenceSoundWave::CreatePickupRustle(this), AssemblyHover, 0.4f, 0.85f);
		RefreshAssembly();
		return;
	}
	if (KeyString.StartsWith(TEXT("Assembly.Slot")))
	{
		TryFit(Target->Index);
		return;
	}
	if (Key == FName(TEXT("Assembly.Flip")))
	{
		FinishAssemblyFlip();
		return;
	}
	if (Key == FName(TEXT("Assembly.Membrane")))
	{
		LiftMembrane();
		return;
	}
	if (Key == FName(TEXT("Assembly.Take")))
	{
		TakeAssembledCover();
		return;
	}
}

void AIGMissingFloorLeftoverDirector::TryFit(const int32 Slot)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || HeldPiece == INDEX_NONE || Slot < 0 || Slot > 2 || FitFeedbackPiece != INDEX_NONE)
	{
		return;
	}
	const int32 Piece = HeldPiece;
	// 크기가 안 맞으면 테두리에 들어가지도 않는다. A는 길쭉하고 B·C는 거의 정사각형이다.
	const bool bTall = (HeldRotation & 1) == 0;
	const bool bSizeFits = bTall && ((Slot == 0) == (Piece == 0));
	if (!bSizeFits)
	{
		PlaySound(UIGToneSequenceSoundWave::CreateKickedPropKnock(this, false), AssemblyCenter, 0.3f, 1.3f);
		ThinkIn(IGLeftoverDialogue::Fit, NSLOCTEXT("IGMissingFloor", "LeftoverFitNoRoom", "이 자리엔 안 들어가."), 2.2f);
		return;
	}
	const bool bCorrect = Piece == Slot && HeldRotation == 0 && !bHeldFlipped;
	UStaticMeshComponent* PieceMesh = PieceMeshes.IsValidIndex(Piece) ? PieceMeshes[Piece].Get() : nullptr;
	if (bCorrect)
	{
		SeatPiece(Piece);
		return;
	}
	// 크기는 맞아 칸에 들어갔는데 뒷면이 안 맞는다. 고정쇠가 구멍에 안 물리고 들뜬다.
	++WrongFits;
	FitFeedbackPiece = Piece;
	FitFeedbackUntil = GetWorld()->GetTimeSeconds() + 1.4;
	if (PieceMesh && FrameRoot)
	{
		const FQuat Base = FrameRoot->GetComponentQuat();
		const FQuat StateQ = CoverStateQuat(HeldRotation, bHeldFlipped);
		const FVector SlotCentroid = PieceCentroid[Slot];
		const FVector World = FrameRoot->GetComponentLocation()
			+ Base.RotateVector(SlotCentroid + StateQ.RotateVector(-PieceCentroid[Piece]))
			+ FVector(0.0f, 0.0f, 1.2f);
		PieceMesh->SetWorldTransform(FTransform(Base * StateQ, World));
	}
	for (int32 Clamp = 0; Clamp < 4; ++Clamp)
	{
		if (ClampPiece[Clamp] == Slot)
		{
			SetClamp(Clamp, 2);
		}
	}
	PlaySound(UIGToneSequenceSoundWave::CreateKickedPropKnock(this, false), AssemblyCenter + FVector(0.0f, 0.0f, 4.0f), 0.4f, 1.6f);
	Caption(NSLOCTEXT("IGMissingFloor", "LeftoverClampLiftCaption", "[고정쇠가 덜컥 들뜨는 소리]"), AssemblyCenter, 1.8f);
	// 어디가 어긋났는지 보이는 대로만 말한다. 틀은 뒷면이 위라 얼룩은 안 보인다. 자리가 틀리면
	// 잘린 면이 바깥으로 나오고, 막 쪽이 위면 고정쇠가 막에서 미끄러지고, 돌아가 있으면 구멍이
	// 어긋난다. 몇 번 틀렸다고 답을 대신 말하지 않는다(H로 물으면 단계별로 떠올린다).
	const FText Hint = Piece != Slot
		? NSLOCTEXT("IGMissingFloor", "LeftoverFitWrongSlot", "잘린 면이 바깥으로 나와.")
		: bHeldFlipped
			? NSLOCTEXT("IGMissingFloor", "LeftoverFitFlipped", "고정쇠가 방수막에 미끄러져.")
			: NSLOCTEXT("IGMissingFloor", "LeftoverFitRotated", "자리는 맞는데 구멍 위치가 어긋나.");
	ThinkIn(IGLeftoverDialogue::Fit, Hint, 3.0f);
	if (WrongFits >= 3)
	{
		if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
		{
			if (AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD()))
			{
				Hud->OfferHintTip();
			}
		}
	}
	SetTargetLive(FindTarget(FName(TEXT("Assembly.Held"))), false);
	GetWorldTimerManager().SetTimer(FitFeedbackTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		FitFeedbackPiece = INDEX_NONE;
		FitFeedbackUntil = 0.0;
		RefreshAssembly();
	}), 1.4f, false);
}

void AIGMissingFloorLeftoverDirector::SeatPiece(const int32 Piece)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	const TCHAR* Beats[3] = {TEXT("Leftover.Assembly.A"), TEXT("Leftover.Assembly.B"), TEXT("Leftover.Assembly.C")};
	Narrative->MarkBeatPlayed(FName(Beats[Piece]));
	HeldPiece = INDEX_NONE;
	PlaySound(UIGToneSequenceSoundWave::CreateKickedPropKnock(this, false), AssemblyCenter + FVector(0.0f, 0.0f, 4.0f), 0.45f, 1.9f);
	Hush(IGLeftoverDialogue::Fit);
	Caption(NSLOCTEXT("IGMissingFloor", "LeftoverClampSeatCaption", "[고정쇠가 딸깍 물리는 소리]"), AssemblyCenter, 1.6f);
	const bool bAll = Narrative->HasBeatPlayed(FName(Beats[0])) && Narrative->HasBeatPlayed(FName(Beats[1]))
		&& Narrative->HasBeatPlayed(FName(Beats[2]));
	if (bAll)
	{
		Narrative->MutableLeftover().AssemblyStage = FMath::Max(Narrative->GetLeftover().AssemblyStage, 1);
		Think(NSLOCTEXT("IGMissingFloor", "LeftoverAssembledThought", "다 맞았어. 한 장짜리 덮개였네."), 3.0f);
	}
	RefreshAssembly();
}

void AIGMissingFloorLeftoverDirector::FinishAssemblyFlip()
{
	if (!PlayBeat(FName(TEXT("Leftover.Assembly.Flipped"))))
	{
		return;
	}
	PlaySound(UIGToneSequenceSoundWave::CreatePickupRustle(this), AssemblyCenter, 0.45f, 0.8f);
	Think(NSLOCTEXT("IGMissingFloor", "LeftoverFlipThought", "얼룩이 대각선으로 이어져. 물이 위에서 새어 내려온 자국이야."), 3.6f);
	RefreshAssembly();
}

void AIGMissingFloorLeftoverDirector::LiftMembrane()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || Narrative->GetLeftover().AssemblyStage >= 2)
	{
		return;
	}
	Narrative->MutableLeftover().AssemblyStage = 2;
	MembraneLiftStart = GetWorld()->GetTimeSeconds();
	PlaySound(UIGToneSequenceSoundWave::CreatePackingTapeRip(this), AssemblyCenter + FVector(30.0f, 0.0f, 4.0f), 0.45f, 0.8f);
	GetWorldTimerManager().SetTimer(MembraneTimer, this, &AIGMissingFloorLeftoverDirector::TickMembraneLift, 1.0f / 60.0f, true);
	GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		Think(NSLOCTEXT("IGMissingFloor", "LeftoverVentThought", "통풍구를 방음재로 쑤셔 막고 그 위에 방수막을 덮었어."), 4.0f);
	}), 1.2f, false);
	RefreshAssembly();
}

void AIGMissingFloorLeftoverDirector::TickMembraneLift()
{
	// TickWardrobeMove와 같은 이유로 멤버 함수 타이머다.
	const float Alpha = FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - MembraneLiftStart) / 0.9), 0.0f, 1.0f);
	for (UStaticMeshComponent* Flap : FlapMeshes)
	{
		if (Flap)
		{
			Flap->SetRelativeRotation(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-120.0f * Alpha)));
		}
	}
	if (Alpha >= 1.0f)
	{
		GetWorldTimerManager().ClearTimer(MembraneTimer);
		MembraneLiftStart = -1.0;
		RefreshAssembly();
	}
}

void AIGMissingFloorLeftoverDirector::PollSightings()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Player || IsLegacyRun())
	{
		return;
	}
	const FIGLeftoverState& State = Narrative->GetLeftover();
	// 그 자리를 이미 다룬 저장이면 본 것이다(이전 판 저장 포함).
	if (State.bSiteMatched || State.WallStage >= 2)
	{
		Narrative->MarkBeatPlayed(SeenAnnexInspectionBeat);
	}
	if (State.bFramePlaced)
	{
		Narrative->MarkBeatPlayed(SeenBraceFrameBeat);
	}
	const bool bInspectionSeen = Narrative->HasBeatPlayed(SeenAnnexInspectionBeat);
	const bool bFrameSeen = Narrative->HasBeatPlayed(SeenBraceFrameBeat);
	if (bInspectionSeen && bFrameSeen)
	{
		return;
	}
	const APlayerController* Controller = Cast<APlayerController>(Player->GetController());
	if (!Controller || !Controller->PlayerCameraManager || !GetWorld())
	{
		return;
	}
	// 5층은 손전등이 비춰야 보인다.
	const UIGFlashlightComponent* Torch = Player->GetFlashlight();
	if (!Torch || !Torch->IsOn())
	{
		return;
	}
	const FVector Eye = Controller->PlayerCameraManager->GetCameraLocation();
	const FVector Look = Controller->PlayerCameraManager->GetCameraRotation().Vector();
	auto Sees = [this, &Eye, &Look](const FVector& Spot)
	{
		const FVector To = Spot - Eye;
		const float Distance = To.Size();
		if (Distance < 1.0f || Distance > 450.0f || FVector::DotProduct(To / Distance, Look) < 0.86f)
		{
			return false;
		}
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LeftoverSighting), false, Player);
		return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Spot, ECC_Visibility, Params)
			|| Hit.Distance > Distance - 40.0f;
	};
	if (!bInspectionSeen && Sees(SiteMatchSpot))
	{
		Narrative->MarkBeatPlayed(SeenAnnexInspectionBeat);
	}
	if (!bFrameSeen && Sees(BraceLeanCenter))
	{
		Narrative->MarkBeatPlayed(SeenBraceFrameBeat);
	}
}

void AIGMissingFloorLeftoverDirector::TakeAssembledCover()
{
	Take(Cover);
	PlaySound(UIGToneSequenceSoundWave::CreatePickupRustle(this), AssemblyCenter, 0.5f, 0.9f);
	// 5층 구석을 본 적이 있을 때만 그 자리를 떠올린다. 못 봤으면 손에 든 것만 말한다.
	Think(HasBeat(SeenAnnexInspectionBeat)
		? NSLOCTEXT("IGMissingFloor", "LeftoverCoverTakeThought", "이만한 덧판을 어디서 봤는데.")
		: NSLOCTEXT("IGMissingFloor", "LeftoverCoverTakeUnseen", "네 귀퉁이에 나사 구멍이 있어. 어딘가에 박혀 있던 거야."), 3.6f);
	RefreshAssembly();
}

void AIGMissingFloorLeftoverDirector::HandleSiteMatch(AIGLeftoverTarget* Target)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !HasTaken(Cover) || Narrative->GetLeftover().bSiteMatched)
	{
		return;
	}
	Narrative->MutableLeftover().bSiteMatched = true;
	AIGNightThreatDirector::HoldQuietAfterDiscovery(this, 60.0f);
	Narrative->RecordWitness(EIGMissingFloorWitness::InspectionCoverMatched);
	MarkExamined(FName(TEXT("Annex.SiteMatch")));
	PlaySound(UIGToneSequenceSoundWave::CreateKickedPropKnock(this, true), SiteMatchSpot, 0.4f, 0.9f);
	Think(NSLOCTEXT("IGMissingFloor", "LeftoverSiteMatch1", "나사 자리도 잘린 면도 맞아. 원래 여기 달려 있던 덮개야."), 4.0f);
	GetWorldTimerManager().SetTimer(SceneTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		Think(NSLOCTEXT("IGMissingFloor", "LeftoverSiteMatch2", "덮개 나사 자리 위로 덧판 나사가 박혔어. 덮개를 떼어 간 다음에 막은 거야."), 4.0f);
	}), 4.2f, false);
	RefreshAssembly();
}

void AIGMissingFloorLeftoverDirector::StartDayOneBoxesForTesting()
{
	BeginDayOneBoxes();
}

// -- 힌트와 새벽이 쓰는 자리 판정 -------------------------------------------------

bool AIGMissingFloorLeftoverDirector::IsPlayerAtAssemblyTable() const
{
	return IsPlayerNear(AssemblyCenter, 260.0f, 160.0f);
}

bool AIGMissingFloorLeftoverDirector::IsPlayerNearStorage() const
{
	// 302호 안이거나 문 앞 복도(문 길 X -30, 문 Y -230 둘레).
	return StorageRoomBoxContainsPlayer() || IsPlayerNear(FVector(-30.0f, -300.0f, StorageZ), 450.0f, 160.0f);
}

int32 AIGMissingFloorLeftoverDirector::GetAssemblyHintStage() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || IsLegacyRun() || GetNightIndex() < 3 || !HasTaken(StorageFrame) || HasTaken(Cover))
	{
		return -1;
	}
	const bool bAll = HasBeat(FName(TEXT("Leftover.Assembly.A"))) && HasBeat(FName(TEXT("Leftover.Assembly.B")))
		&& HasBeat(FName(TEXT("Leftover.Assembly.C")));
	if (!bAll)
	{
		return 0;
	}
	if (!HasBeat(FName(TEXT("Leftover.Assembly.Flipped"))))
	{
		return 1;
	}
	return Narrative->GetLeftover().AssemblyStage >= 2 ? 3 : 2;
}

void AIGMissingFloorLeftoverDirector::PutDownHeldPiece()
{
	if (HeldPiece == INDEX_NONE)
	{
		return;
	}
	// 든 자세 그대로 바닥 자리에 둔다. 다시 집으면 그 자세다.
	PieceRotation[HeldPiece] = HeldRotation;
	bPieceFlipped[HeldPiece] = bHeldFlipped;
	HeldPiece = INDEX_NONE;
	GetWorldTimerManager().ClearTimer(FitFeedbackTimer);
	FitFeedbackPiece = INDEX_NONE;
	FitFeedbackUntil = 0.0;
	RefreshAssembly();
}

// -- 정우가 문 앞에 둔 오빠 상자 ---------------------------------------------------

void AIGMissingFloorLeftoverDirector::RefreshDeliveredBox()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || IsLegacyRun())
	{
		return;
	}
	// 셋째 밤에 정우를 꺼냈는데 상자는 같이 못 뺐다. 정우가 아침에 302호 짐을 빼며 표시해 둔 그 상자를
	// 403호 문 앞에 두었다(낮3 문자). 판 뒤에 혼자 남았던 회차는 상자도 수거차와 같이 갔다.
	const int32 Night = GetNightIndex();
	const bool bAfterNightThree = Night > 3 || (Night == 3 && !bHourActive);
	const bool bShow = bAfterNightThree && Narrative->GetLeftover().JeongwooState == LeftTogether && !HasTaken(DohaBox);
	const FName BoxKey(TEXT("Home.DeliveredBox"));
	const FName TargetKey(TEXT("Home.DeliveredBoxTake"));
	if (bShow && !Props.Contains(BoxKey))
	{
		SpawnProp(BoxKey, Mesh(TEXT("SM_StorageBox")), DeliveredBoxSpot, false);
		SpawnProp(FName(TEXT("Home.DeliveredBoxLabel")), Mesh(TEXT("SM_LeftoverLabelDoha")), DeliveredBoxSpot, false);
		SpawnProp(FName(TEXT("Home.DeliveredBoxMark")), Mesh(TEXT("SM_LeftoverLabelMarked")), DeliveredBoxSpot, false);
		if (AIGLeftoverTarget* Take = SpawnTarget(TargetKey, DeliveredBoxSpot.GetLocation() + FVector(0.0f, 0.0f, 22.0f),
				DeliveredBoxSpot.Rotator(), FVector(26.0f, 22.0f, 20.0f),
				NSLOCTEXT("IGMissingFloor", "LeftoverDeliveredBoxPrompt", "상자 들여놓기"), 0.6f, 0.04f))
		{
			Take->OnCompleted.AddUObject(this, &AIGMissingFloorLeftoverDirector::HandleDeliveredBox);
		}
	}
	for (const TCHAR* Key : {TEXT("Home.DeliveredBox"), TEXT("Home.DeliveredBoxLabel"), TEXT("Home.DeliveredBoxMark")})
	{
		ShowProp(FName(Key), bShow);
	}
	SetTargetLive(FindTarget(TargetKey), bShow);
}

void AIGMissingFloorLeftoverDirector::HandleDeliveredBox(AIGLeftoverTarget* Target)
{
	if (HasTaken(DohaBox))
	{
		return;
	}
	Take(DohaBox);
	Take(ToolPouch);
	AIGNightThreatDirector::HoldQuietAfterDiscovery(this, 60.0f);
	PlaySound(UIGToneSequenceSoundWave::CreateToolRollClink(this), DeliveredBoxSpot.GetLocation() + FVector(0.0f, 0.0f, 20.0f), 0.5f);
	Think(NSLOCTEXT("IGMissingFloor", "LeftoverDeliveredBoxThought", "정우 씨가 표시해 둔 그 상자야. 안에 오빠 공구 주머니가 있어."), 3.8f);
	RefreshDeliveredBox();
}
