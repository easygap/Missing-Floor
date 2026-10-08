#include "Entity/IGMissingFloorNightThreeDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorMercyDirector.h"
#include "Entity/IGMissingFloorPuzzleTwoDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/IGDoorLatch.h"
#include "Interaction/IGReadableNote.h"
#include "Interaction/IGSwingDoor.h"
#include "Interaction/IGZoneTrigger.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGStoryHelpers.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"
#include "TimerManager.h"

namespace IGNightThree
{
	// Two real leaves on the same roof slab. The route between their threshold
	// centers is 407.5 + 232.5 = 640 cm and is built by the world scene.
	const FVector StairGateHinge(-320.0f, 220.0f, 1200.0f);
	const FVector AnnexGateHinge(85.0f, 452.5f, 1200.0f);
	// 두 문짝의 크기(두께, 폭, 높이). 5층 문짝은 개구부보다 2 cm 좁아 손잡이 쪽에 빗장
	// 받이쇠가 들어갈 틈이 남는다.
	const FVector RoofLeafSize(4.5f, 85.0f, 203.5f);
	const FVector AnnexLeafSize(4.5f, 88.0f, 203.5f);
	// 5층 철문 안쪽 빗장. 몸통 끝이 문짝 끝에서 3 cm, 문 면에서 0.5 mm, 바닥에서 1.32 m.
	// 막대가 4.5 cm 나가면 문설주 옆면(X 175)의 받이쇠(씬)에 들어간다.
	const FVector AnnexBoltLocation(164.0f, 454.8f, 1332.0f);
	constexpr float AnnexBoltTravelCm = 4.5f;

	// 12.5T 판재 15장의 윗면은 Z=1231.17이다. 사라진 받침대 높이에
	// 남아 있던 수첩과 렌치도 이 면에 내려놓는다. 좌우로 나눠 서로 겹치지 않는다.
	const FVector NotebookLocation(-90.0f, 770.0f, 1231.87f);
	const FVector TuningHammerLocation(-40.0f, 770.0f, 1232.67f);
	/** 렌치를 챙긴 회차. 저장되는 사실이라 불러온 밤에 자재 위로 돌아오지 않는다. */
	const FName WrenchTakenBeatId(TEXT("Night3.WrenchTaken"));
	const FVector WorkGloveLocation(-135.0f, 774.0f, 1231.97f);
	const FVector TunerToolCartLocation(-280.0f, 865.0f, 1201.0f);
	const FVector ValveLocation(296.0f, 610.0f, 1266.0f);
	const FVector ImpactMarkLocation(-10.0f, 585.0f, 1264.0f);
	/**
	 * T5 「새 벽 미장 시기」의 높이. 귀·주먹 판정이 Z 1277..1303을 쓰므로
	 * 바닥 쪽 이음선으로 내린다. 칸 좌표는 WallBayYs를 그대로 쓴다.
	 */
	constexpr float PlasterDatingZ = 1220.0f;
	/**
	 * T8 「탱크 물소리 청음」. 탱크 몸통은 Y -5..125이고 점검 통로는 그
	 * 북쪽이다. 씬에 붙인 24×14cm 표찰과 같은 위치에 읽기 판정을 둔다.
	 */
	const FVector TankAuditionLocation(0.0f, 125.7f, 1340.0f);
	/**
	 * §13 12행. 채널 5는 「도면에 없는 복도, 천장 전구 하나, 바닥을 지나가는
	 * 낮은 형체」를 보여 준다. 그 화각에 실제로 서는 자리는 별관 철문을
	 * 지나 복도가 시작되는 지점이다. 부피만 있고 그리는 것이 없다.
	 */
	const FVector AnnexRecognitionCenter(120.0f, 520.0f, 1290.0f);
	const FVector AnnexRecognitionExtent(150.0f, 60.0f, 100.0f);
	const float WallBayYs[3] = {560.0f, 700.0f, 840.0f};
	constexpr int32 CavityBayIndex = 1;

	// The booth keyring, on the desk's west end.
	// 관리실 책상 위. (118, -96, 80)은 P2 민원 대장 정서본(X 108.6~130.8,
	// Y -118.3~-87.7) 한가운데였고 상판(Z=76)에도 1cm 박혀 있었다. 서로
	// 다른 디렉터가 같은 책상에 놓으면서 부딪혔다. 대장 앞쪽 빈자리로 뺀다.
	const FVector KeyringLocation(118.0f, -126.0f, 76.16f);

	// Day papers: the mover's labels in 403, the forum printout by the
	// mailboxes, the tally journal at 401's threshold once it is earned.
	// 계약서 왼쪽 빈자리. 높이는 생성할 때 실제 상판에서 잰다.
	const FVector LabelsLocation(-125.0f, -165.0f, 0.0f);
	// 관리인에게 남긴 인쇄본은 게시판에 꽂는다. 우편 투입구는 비워 둔다.
	const FVector ForumLocation(643.0f, -148.65f, 166.0f);
	const FVector JournalLocation(-172.0f, -237.5f, 985.0f);
	/**
	 * 일지는 401호 문에 거는 달력 뒷장이다. 그녀가 문을 열고, 걸고, 닫는다.
	 * 소리는 문짝 가운데 손잡이 높이에서 난다.
	 */
	const FVector Unit401DoorSoundLocation(-145.0f, -245.0f, 1000.0f);
	/**
	 * 일지를 거는 때. 새벽의 잠금 소리와 눈을 뜨며 드는 아침 독백, 밤새 녹음하던
	 * 폰의 진동(새벽 뒤 6초)이 다 지나간 뒤다. 그 뒤로는 대사가 비고 그녀가 그
	 * 문을 보고 있지 않을 때까지 기다린다.
	 */
	constexpr float JournalHandoverDelaySeconds = 8.5f;
	constexpr float JournalHandoverPollSeconds = 0.5f;
	/** 저장되는 사실이다. 불러온 낮에 문이 한 번 더 열리면 안 된다. */
	const FName JournalHandedOverBeatId(TEXT("Day.JournalHandedOver"));

	// -- 낮의 서일영 (§3.4, §8 밤3 낮) ------------------------------------
	/**
	 * 골목 건너 서쪽 샛길(X 1210..1390) 입구. 아스팔트 윗면이 Z 0이고, 승용차
	 * 차체 바깥면(Y -677)보다 23 cm 바깥이라 지나가는 차가 그를 뚫고 가지 않는다.
	 * 편의점 자동문에서 12 m 남짓이고, 거기서 본 시선이 동쪽 상가 모서리 앞을
	 * 아슬하게 비켜 간다. 예전 자리(-40, -835)는 남쪽 상가 벽 뒤라 어디서도
	 * 보이지 않았다.
	 */
	const FVector DistantSeoLocation(1240.0f, -700.0f, 90.0f);
	/**
	 * 다가오는 그녀 앞에서 비켜서는 자리. 골목에서 오면 샛길 안쪽, 동쪽 상가
	 * 모서리 뒤로 든다. 샛길 쪽(뒤편 골목)에서 오면 골목으로 나서며 샛길 서쪽
	 * 벽 뒤로 빠진다. 어느 쪽이든 그녀에게서 멀어지는 걸음이다.
	 */
	const FVector DistantSeoRetreatLocation(1270.0f, -800.0f, 90.0f);
	const FVector DistantSeoStreetRetreatLocation(1130.0f, -660.0f, 90.0f);
	/** 이보다 남쪽이면 그녀는 상가 벽 뒤, 샛길이나 뒤편 골목에 있다. */
	constexpr float SeoFacadeLineY = -700.0f;
	constexpr float SeoPollSeconds = 0.1f;
	constexpr float SeoRetreatPollSeconds = 0.05f;
	/** 걸음 빠르기로 비켜선다. */
	constexpr float SeoRetreatSpeed = 130.0f;
	/** 계산대 쪽을 보는 사이에 선다. 유리 너머로 생겨나는 순간을 보이면 안 된다. */
	constexpr float SeoArmFacingDot = 0.2f;
	/** 화면 가운데 가까이, 가려지지 않고 이만큼 머물러야 알아본 것이다. */
	constexpr float SeoSeenFacingDot = 0.9f;
	constexpr float SeoRecognizeSeconds = 0.5f;
	/** 이보다 바깥이면 화면 밖이다. 거기서만 사라진다. */
	constexpr float SeoOffscreenDot = 0.6f;
	constexpr float SeoGoneAwaySeconds = 1.0f;
	/** 눈을 돌린 채 이만큼 다가오면 이미 없다. */
	constexpr float SeoHideRange = 1100.0f;
	/** 보면서 이만큼 다가오면 샛길 안으로 비켜선다. 판이 판으로 보이기 전이다. */
	constexpr float SeoRetreatRange = 800.0f;
	const FName DistantSeoBeatId(TEXT("Day3.DistantSeo"));

	// 주먹이 석고보드를 때리는 값은 플레이어 쪽이 든다 —
	// AIGPlayerCharacter::KnockLoudness. 귀를 대는 건 소리를 안 낸다.
	constexpr float ListenLoudness = 0.05f;
	constexpr float ValveLoudness = 0.55f;
	/**
	 * 주먹으로 벽을 읽는 값. 밸브보다 조용하면 급한 길이 조용한 길이 된다
	 * (§7 신중한 자에게는 물이, 급한 자에게는 추격이). 대답 노크는 손가락
	 * 마디 값 그대로다.
	 */
	constexpr float WallEchoKnockLoudness = 0.48f;

	/**
	 * The three authored wheels ring differently (§10.3 밸브 3종): 0 is the 5F
	 * shaft inspection valve here, 1 the large rooftop cleaning drain, 2 the
	 * small float bypass. A player who opened this one in night 3 recognises it
	 * is *not* what they are turning on the roof in night 4.
	 */
	constexpr int32 ShaftValveIndex = 0;
	/**
	 * §21.3 원근 4단. Out in the annex the riser comes through finished wall and
	 * arrives muffled; against the cavity wall there is only air between the
	 * player's ear and the pipe; against a solid wall the water took the long
	 * way through mass. The step is the whole distance cue.
	 */
	constexpr int32 RiserBedDistanceStep = 2;
	constexpr int32 CavityWallDistanceStep = 0;
	constexpr int32 SolidWallDistanceStep = 3;
	/** Ear against board: close enough that the wall itself dominates. */
	constexpr float WallListenInnerRadius = 90.0f;
	constexpr float WallListenFalloff = 620.0f;

	/** Eight seconds of nothing before the wall comes back. */
	constexpr float AnswerDelaySeconds = 8.0f;
	// P4의 벽과 복도의 맨손 노크는 같은 박자를 받아야 한다. 정의는 존재가
	// 가지고 있고 여기서는 참조만 한다 — 두 벌로 두면 언젠가 어긋난다.
	constexpr double AnswerPairMinSeconds =
		AIGListenerEntity::AnswerPairMinSeconds;
	constexpr double AnswerPairMaxSeconds =
		AIGListenerEntity::AnswerPairMaxSeconds;
	constexpr double AnswerRestMinSeconds =
		AIGListenerEntity::AnswerRestMinSeconds;
	constexpr double AnswerRestMaxSeconds =
		AIGListenerEntity::AnswerRestMaxSeconds;
	/**
	 * 대답 뒤의 시각표. 노크는 0 / 0.42 / 1.15초에 치고, 1.65초에 정적이
	 * 걷히기 시작해 공기가 이만큼에 걸쳐 돌아온다. 숨과 「…대답이다.」는
	 * 걷히기 시작한 직후에 온다. 소리가 먼저 대답하고 글은 뒤따른다.
	 */
	constexpr float AnswerAirReturnSeconds = 2.4f;
	constexpr float AnswerAftermathSeconds = 1.7f;
	/** 「내려가자.」는 여운과 조율음이 다 지나간 뒤에 온다. */
	constexpr float ReturnThoughtDelaySeconds = 7.0f;
	/** 대답 동안 걷어 둔 라이저 물소리를 공기가 다 돌아온 뒤에 되살린다. */
	constexpr float RiserResumeDelaySeconds = 6.0f;
	constexpr float RiserResumeFadeInSeconds = 4.0f;
	/**
	 * 벽 안에서 돌아오는 손은 사흘 밤 복도를 두드리던 그 손이다. 그의 노크 셋
	 * (Entity_KnockTriple)과 석고 노크(Knock_Plaster_0..2)는 같은 나무 타격
	 * 세 개에서 나왔다. 같은 순서로 치고, 그의 노크(0.70배)와 같은 높이로
	 * 내리고, 스터드 너머로 먹인다. 박자는 CreateAnswerKnockPattern과 같다.
	 */
	constexpr float AnswerHitStartSeconds[3] = {0.0f, 0.42f, 1.15f};
	constexpr float AnswerHitVolumes[3] = {0.76f, 0.76f, 0.6f};
	constexpr float AnswerHitPitch = 0.97f;
	constexpr float AnswerHitLowPassHz = 760.0f;

	const FName PuzzleThreeId(TEXT("P3"));
	const FName PuzzleFourId(TEXT("P4"));

	// -- 비트 3-7 「귀환길」 ------------------------------------------------
	constexpr float FourthFloorZ = AIGPrologueWorldScene::FourthFloorZ;
	/**
	 * 4층 복도, 계단코어와 403호 문 사이. 복도는 Y -385..-225로 깊이가 160cm뿐이라
	 * 사람 하나를 지나치는 일이 실제로 좁다 — 그것이 이 비트의 전부다.
	 * 그녀는 서쪽(계단코어, X=-277.5)에서 내려와 동쪽 403호 문(X=101)으로 간다.
	 */
	const FVector ReturnPassPoint(-95.0f, -300.0f, FourthFloorZ);
	/** 서 있는 것이 아니라 기다리는 것으로 읽히도록 복도를 가로질러 조금 움직인다. */
	const FVector ReturnShufflePoint(-95.0f, -334.0f, FourthFloorZ);
	/** 그가 향한 쪽. 남쪽 벽에 귀를 대고 있다 — 1-4의 자세 그대로. */
	constexpr float ReturnPassYaw = -90.0f;
	/** 지나쳤다고 인정하는 여유. 몸 하나 폭보다 넉넉하게. */
	constexpr float PassClearanceCentimeters = 70.0f;
	/** 403호 실내는 4층 X -190..190, Y -235..235. */
	const FBox Unit403Interior(
		FVector(-190.0f, -235.0f, FourthFloorZ - 20.0f),
		FVector(190.0f, 235.0f, FourthFloorZ + 230.0f));
	/**
	 * 도착은 문턱이 아니라 방 안이다. 문면(Y -225)에서 몸 하나만큼 더 들어온
	 * 선부터 세고, 거기서 잠시 머물러야 밤이 닫힌다. 들어서자마자 암전되면
	 * 그를 돌아볼 틈이 없다. 나갔는지는 위의 실내 상자로 본다.
	 */
	const FBox Unit403Arrival(
		FVector(-190.0f, -195.0f, FourthFloorZ - 20.0f),
		FVector(190.0f, 235.0f, FourthFloorZ + 230.0f));
	constexpr float HomeArrivalDwellSeconds = 2.5f;
	/** 옆을 지나고 나서 이만큼 멀어지면 참았던 숨이 나간다. */
	constexpr float PassReliefCentimeters = 150.0f;
	/** 계단을 내려와 복도에 들어선 자리에서 그가 보이는 거리. */
	constexpr float ReturnHintRangeCentimeters = 600.0f;
	constexpr float ReturnPollSeconds = 0.25f;

	const FName PassByBeatId(TEXT("Night3.PassBy"));

	/**
	 * 프로브와 캡처는 상태를 동기적으로 밟는다. 여기서 새로 늦춘 상태 전이는
	 * 그 둘에서 즉시 넘어간다.
	 */
	inline bool IsSynchronousHarness()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreyboxProbe"))
			|| FParse::Param(FCommandLine::Get(), TEXT("IGNightCapture"));
	}

	/**
	 * 05:30에 눈이 감기기 시작했는가. 이 디렉터가 낮을 받는 것은 다 감긴 뒤
	 * (0.48초)라서 그 사이에는 새벽 디렉터를 직접 본다.
	 */
	inline bool IsDawnUnderway(const UWorld* World)
	{
		if (!World)
		{
			return false;
		}
		TActorIterator<AIGNightPhaseDirector> It(World);
		return It && !It->IsHourActive();
	}
}

AIGMissingFloorNightThreeDirector::AIGMissingFloorNightThreeDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool AIGMissingFloorNightThreeDirector::Configure(
	AIGPrologueWorldScene* InScene,
	AIGListenerEntity* InEntity,
	AIGPlayerCharacter* InPlayer,
	const TArray<FVector>& InCorridorPatrolPoints)
{
	UWorld* World = GetWorld();
	if (!World || !InScene)
	{
		return false;
	}
	Scene = InScene;
	Entity = InEntity;
	Player = InPlayer;
	CorridorPatrolPoints = InCorridorPatrolPoints;

	UStaticMesh* CubeMesh =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* CylinderMesh =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (!CubeMesh || !CylinderMesh)
	{
		return false;
	}

	// 서일영은 밤2를 넘긴 낮, 편의점을 나서는 길에 골목 건너 샛길 입구에 한 번
	// 선다(§3.4). 다가가기 전에 비켜서는 원경이라 마스크 스프라이트로 충분하고,
	// 가까운 사람과 만지는 증거는 전부 3D다. 판은 그녀 쪽으로 돌려 세운다.
	// Feet sit exactly on Z=0 and the masked card keeps a real cast shadow.
	if (UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		if (UMaterialInterface* SeoMaterial = LoadObject<UMaterialInterface>(
				nullptr,
				TEXT("/Game/Prototype/Materials/M_SpriteSeo.M_SpriteSeo")))
		{
			DistantSeo = NewObject<UStaticMeshComponent>(this, TEXT("DistantSeo"));
			DistantSeo->RegisterComponent();
			DistantSeo->SetStaticMesh(PlaneMesh);
			DistantSeo->SetMaterial(0, SeoMaterial);
			DistantSeo->SetWorldLocation(IGNightThree::DistantSeoLocation);
			DistantSeo->SetWorldRotation(FRotator(0.0f, 0.0f, 90.0f));
			DistantSeo->SetWorldScale3D(FVector(0.86f, 1.80f, 1.0f));
			DistantSeo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			DistantSeo->SetCanEverAffectNavigation(false);
			DistantSeo->SetCastShadow(true);
			DistantSeo->SetVisibility(false, true);
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UMaterialInterface* DoorMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Prototype/Materials/M_SteelDoorUV.M_SteelDoorUV"));
	UMaterialInterface* HandleMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Prototype/Materials/M_StainlessUV.M_StainlessUV"));

	// The roof and annex leaves are separate physical locks. The state is the
	// persisted *keyring* possession; the prop and text make its two labelled
	// keys explicit instead of implying one magical master key.
	SpawnParameters.Name = TEXT("MissingFloorStairGate");
	StairGate = World->SpawnActor<AIGSwingDoor>(
		AIGSwingDoor::StaticClass(),
		FTransform(FRotator(0.0f, -90.0f, 0.0f), IGNightThree::StairGateHinge),
		SpawnParameters);
	if (!StairGate)
	{
		return false;
	}
	// 두 철문은 시안을 보고 Blender에서 만든 문짝이다. 메시가 없으면 예전 상자 문으로 둔다.
	if (UStaticMesh* RoofLeaf = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Meshes/SM_RooftopDoorLeaf.SM_RooftopDoorLeaf")))
	{
		StairGate->ConfigureAuthoredLeaf(RoofLeaf, nullptr, IGNightThree::RoofLeafSize);
		StairGate->AddLeafDressing(LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Meshes/SM_RooftopDoorSign.SM_RooftopDoorSign")));
	}
	else
	{
		StairGate->ConfigurePrototypeVisuals(
			CubeMesh, DoorMaterial, HandleMaterial, FVector(6.0f, 85.0f, 205.0f));
	}
	StairGate->SetOpenYaw(-95.0f);
	{
		TArray<FIGDoorRequirement> GateRequirements;
		FIGDoorRequirement& KeyLock = GateRequirements.AddDefaulted_GetRef();
		KeyLock.RequiredState = FGameplayTag::RequestGameplayTag(
			FName(TEXT("State.MissingFloor.HasStairKey")), false);
		KeyLock.LockedPrompt =
			NSLOCTEXT("IGMissingFloor", "StairGatePrompt", "잠긴 옥상 철문");
		KeyLock.LockedThought = NSLOCTEXT(
			"IGMissingFloor",
			"StairGateThought",
			"옥상 열쇠가 있어야 열 수 있겠다.");
		StairGate->SetRequirements(MoveTemp(GateRequirements));
	}

	SpawnParameters.Name = TEXT("MissingFloorAnnexGate");
	AnnexGate = World->SpawnActor<AIGSwingDoor>(
		AIGSwingDoor::StaticClass(),
		FTransform(FRotator(0.0f, -90.0f, 0.0f), IGNightThree::AnnexGateHinge),
		SpawnParameters);
	if (!AnnexGate)
	{
		return false;
	}
	if (UStaticMesh* AnnexLeaf = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Meshes/SM_AnnexDoorLeaf.SM_AnnexDoorLeaf")))
	{
		AnnexGate->ConfigureAuthoredLeaf(AnnexLeaf, nullptr, IGNightThree::AnnexLeafSize);
	}
	else
	{
		AnnexGate->ConfigurePrototypeVisuals(
			CubeMesh, DoorMaterial, HandleMaterial, FVector(6.0f, 90.0f, 205.0f));
	}
	AnnexGate->SetOpenYaw(95.0f);
	{
		TArray<FIGDoorRequirement> GateRequirements;
		FIGDoorRequirement& KeyLock = GateRequirements.AddDefaulted_GetRef();
		KeyLock.RequiredState = FGameplayTag::RequestGameplayTag(
			FName(TEXT("State.MissingFloor.HasStairKey")), false);
		KeyLock.LockedPrompt = NSLOCTEXT(
			"IGMissingFloor", "AnnexGatePrompt", "잠긴 5층 철문");
		KeyLock.LockedThought = NSLOCTEXT(
			"IGMissingFloor", "AnnexGateThought", "창고 열쇠가 있어야 열 수 있겠다.");
		AnnexGate->SetRequirements(MoveTemp(GateRequirements));
	}
	// 안쪽 빗장. 밤4에 손님이 오빠 열쇠로 자물쇠를 돌려도 이게 걸려 있으면 문이 걸린다.
	if (UStaticMesh* BoltHousing = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Meshes/SM_DoorBarrelBolt.SM_DoorBarrelBolt")))
	{
		SpawnParameters.Name = TEXT("MissingFloorAnnexBolt");
		AnnexBolt = World->SpawnActor<AIGDoorLatch>(
			AIGDoorLatch::StaticClass(),
			FTransform(FRotator::ZeroRotator, IGNightThree::AnnexBoltLocation),
			SpawnParameters);
		if (AnnexBolt)
		{
			AnnexBolt->ConfigureAuthored(
				AnnexGate,
				BoltHousing,
				LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_DoorBarrelBoltPin.SM_DoorBarrelBoltPin")),
				IGNightThree::AnnexBoltTravelCm,
				NSLOCTEXT("IGMissingFloor", "AnnexBoltClosePrompt", "빗장 걸기"),
				NSLOCTEXT("IGMissingFloor", "AnnexBoltOpenPrompt", "빗장 풀기"));
		}
	}
	float RouteLengthCentimeters = 0.0f;
	int32 UpperStepCount = 0;
	if (!Scene->ValidateMissingFloorRooftopRoute(
			RouteLengthCentimeters,
			UpperStepCount))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT(
				"Missing-floor roof route invalid: length=%.1f steps=%d "
				"(expected 640.0/14)."),
			RouteLengthCentimeters,
			UpperStepCount);
		return false;
	}

	// 관리실 상판에 내려 둔 금속 열쇠 두 개. 이름표까지 같은 메시로 반입한다.
	SpawnParameters.Name = TEXT("MissingFloorKeyring");
	Keyring = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGNightThree::KeyringLocation),
		SpawnParameters);
	if (!Keyring)
	{
		return false;
	}
	UStaticMesh* KeyringMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_BoothKeyring.SM_BoothKeyring"));
	Keyring->Configure(
		KeyringMesh,
		LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/MI_BoothKeyring.MI_BoothKeyring")),
		FVector::ZeroVector,
		NSLOCTEXT("IGMissingFloor", "KeyringPrompt", "옥상·창고 열쇠 꾸러미"),
		NSLOCTEXT(
			"IGMissingFloor",
			"KeyringThought",
			"옥상 열쇠랑 창고 열쇠. 챙겨 두자."),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		0.0f,
		0.1f);
	Keyring->OnExamined.AddUObject(
		this, &AIGMissingFloorNightThreeDirector::HandleKeyringTaken);
	RefreshKeyringAvailability();

	// -- annex contents ----------------------------------------------------

	SpawnParameters.Name = TEXT("MissingFloorTunerNotebook");
	TunerNotebook = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGNightThree::NotebookLocation),
		SpawnParameters);
	if (!TunerNotebook)
	{
		return false;
	}
	// 5층 별관에 놓이는 종이와 금속. 비워 두면 엔진 기본 격자가 그대로 보인다.
	UMaterialInterface* AgedPaperMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_PaperOld.M_PaperOld"));
	UMaterialInterface* FreshPaperMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_PaperClean.M_PaperClean"));
	UMaterialInterface* AnnexMetalMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_MetalFrame.M_MetalFrame"));

	UStaticMesh* NotebookMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_TunerNotebook.SM_TunerNotebook"));
	// 이 함수는 크기를 100으로 나눈다. 저작 메시에는 스케일 1을 전달한다.
	TunerNotebook->ConfigurePrototypeVisuals(
		NotebookMesh ? NotebookMesh : CubeMesh,
		NotebookMesh ? nullptr : AgedPaperMaterial,
		NotebookMesh ? FVector(100) : FVector(16, 22, 1.4f), true);
	TunerNotebook->SetInteractionPrompt(
		NSLOCTEXT("IGMissingFloor", "NotebookPrompt", "조율 수첩"));
	TunerNotebook->SetNoteText(
		NSLOCTEXT("IGMissingFloor", "NotebookTitle", "백도하 조율 수첩"),
		{
			NSLOCTEXT("IGMissingFloor", "Notebook1", "월  서초 공연장  ·  공연 끝나고 조율 02:30"),
			NSLOCTEXT("IGMissingFloor", "Notebook2", "수  대치동 학원  ·  업라이트 네 대  ·  22시 이후 출입"),
			FText::GetEmpty(),
			NSLOCTEXT("IGMissingFloor", "Notebook3", "옥탑 벽 확인: 빈 곳은 낮게 울리고 소리가 오래 감."),
			NSLOCTEXT("IGMissingFloor", "Notebook4", "기둥 있는 쪽은 짧고 둔함. 배관 멈추면 다시 확인."),
			FText::GetEmpty(),
			NSLOCTEXT("IGMissingFloor", "Notebook5", "●●  —  ●"),
			NSLOCTEXT("IGMissingFloor", "Notebook6", "토요일 11시  ·  유담 연습실 예약"),
			FText::GetEmpty(),
			NSLOCTEXT("IGMissingFloor", "Notebook7", "유담 피아노 적금  180만 / 200만"),
		});
	TunerNotebook->OnReadStateChanged.AddDynamic(
		this, &AIGMissingFloorNightThreeDirector::HandleNotebookRead);

	SpawnParameters.Name = TEXT("MissingFloorTuningHammer");
	TuningHammer = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(
			FRotator(0.0f, 0.0f, 90.0f),
			IGNightThree::TuningHammerLocation),
		SpawnParameters);
	if (!TuningHammer)
	{
		return false;
	}
	UStaticMesh* TuningHammerMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_TuningHammer.SM_TuningHammer"));
	UMaterialInterface* TuningHammerMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_MetalFrame.M_MetalFrame"));
	TuningHammer->Configure(
		TuningHammerMesh ? TuningHammerMesh : CylinderMesh,
		TuningHammerMesh ? nullptr : TuningHammerMaterial,
		// 목재·금속 구운 재질과 실제 크기를 쓴다. 손잡이는 지름 3cm다.
		TuningHammerMesh
			? FVector::ZeroVector
			: FVector(3.0f, 3.0f, 26.0f),
		NSLOCTEXT("IGMissingFloor", "TuningHammerPrompt", "튜닝 해머"),
		// 입주한 날 연 공구 상자에는 이것만 없었다. 여기서 챙겨 가서 밤4에
		// 유해 곁에 돌려놓는다(§7 소지품 인과 장부).
		NSLOCTEXT(
			"IGMissingFloor",
			"TuningHammerThought",
			"공구 상자에서 빠져 있던 오빠 튜닝 해머다. 챙겨 두자."),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		0.0f,
		0.06f);
	TuningHammer->OnExamined.AddUObject(
		this, &AIGMissingFloorNightThreeDirector::HandleTuningHammerExamined);
	RefreshTuningHammerAvailability();

	// §22.3. 도하의 손은 이 크기가 아니다. 진실을 열지는 않는다 — 누구
	// 것인지는 이미 T5가 말하고, 이건 그 사람이 여기 있었다는 감각이다.
	SpawnParameters.Name = TEXT("MissingFloorWorkGlove");
	WorkGlove = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(
			FRotator(0.0f, 24.0f, 0.0f),
			IGNightThree::WorkGloveLocation),
		SpawnParameters);
	if (WorkGlove)
	{
		UStaticMesh* GloveMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Meshes/SM_CottonWorkGlove.SM_CottonWorkGlove"));
		WorkGlove->Configure(
			GloveMesh ? GloveMesh : CubeMesh,
			GloveMesh ? nullptr : AgedPaperMaterial,
			GloveMesh ? FVector::ZeroVector : FVector(24.0f, 11.0f, 2.4f),
			NSLOCTEXT("IGMissingFloor", "WorkGlovePrompt", "작업 장갑 한 짝"),
			NSLOCTEXT(
				"IGMissingFloor",
				"WorkGloveThought",
				"장갑에 석고가 굳어 있다. 오빠 장갑은 이것보다 작았는데."),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			0.9f,
			0.05f);
		WorkGlove->OnExamined.AddUObject(
			this, &AIGMissingFloorNightThreeDirector::HandleWorkGloveExamined);
	}

	// A close prop must preserve parallax, contact shadow and the gap beneath
	// its shelves.  The ImageGen sheet is only the shape reference; the runtime
	// object is a real 45 x 34 x 78 cm mesh resting on the annex slab.
	if (UStaticMesh* CartMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Game/Meshes/SM_TunerToolCart.SM_TunerToolCart")))
	{
		TunerToolCart = NewObject<UStaticMeshComponent>(
			this, TEXT("TunerToolCart"));
		TunerToolCart->RegisterComponent();
		TunerToolCart->SetStaticMesh(CartMesh);
		TunerToolCart->SetMaterial(0, TuningHammerMaterial);
		TunerToolCart->SetWorldLocation(IGNightThree::TunerToolCartLocation);
		TunerToolCart->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		TunerToolCart->SetCollisionResponseToAllChannels(ECR_Block);
		TunerToolCart->SetCanEverAffectNavigation(false);
		TunerToolCart->SetCastShadow(true);

		// 카트는 밀린다. 입주 저녁 천장 너머에서 구르던 그 바퀴다(§8 0-1).
		// 그림은 카트 메시가 맡고 이 부피는 프롬프트와 홀드만 받는다. 이름을
		// 붙이지 않는다 — 디렉터가 다시 서도 같은 이름에 막히지 않게.
		SpawnParameters.Name = NAME_None;
		TunerCartPush = World->SpawnActor<AIGMissingFloorEvidence>(
			AIGMissingFloorEvidence::StaticClass(),
			FTransform(
				FRotator::ZeroRotator,
				IGNightThree::TunerToolCartLocation + FVector(0.0f, 0.0f, 39.0f)),
			SpawnParameters);
		if (TunerCartPush)
		{
			TunerCartPush->Configure(
				CubeMesh,
				nullptr,
				FVector(45.0f, 34.0f, 78.0f),
				NSLOCTEXT("IGMissingFloor", "TunerCartPrompt", "공구 카트 밀어 보기"),
				FText::GetEmpty(),
				EIGMissingFloorTruth::None,
				EIGMissingFloorSource::None,
				0.6f,
				0.25f,
				/*bPresentationVisible=*/false);
			TunerCartPush->OnExamined.AddUObject(
				this, &AIGMissingFloorNightThreeDirector::HandleTunerCartPushed);
		}
	}

	SpawnParameters.Name = TEXT("MissingFloorRiserValve");
	RiserValve = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator(90.0f, 0.0f, 0.0f), IGNightThree::ValveLocation),
		SpawnParameters);
	if (!RiserValve)
	{
		return false;
	}
	RiserValve->Configure(
		CylinderMesh,
		AnnexMetalMaterial,
		FVector(16.0f, 16.0f, 5.0f),
		NSLOCTEXT("IGMissingFloor", "ValvePrompt", "배관 밸브 열기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		0.8f,
		IGNightThree::ValveLoudness);
	RiserValve->OnExamined.AddUObject(
		this, &AIGMissingFloorNightThreeDirector::HandleValveOpened);

	SpawnParameters.Name = TEXT("MissingFloorImpactMark");
	ImpactMark = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGNightThree::ImpactMarkLocation),
		SpawnParameters);
	if (!ImpactMark)
	{
		return false;
	}
	ImpactMark->Configure(
		CubeMesh,
		nullptr,
		FVector(22.0f, 10.0f, 6.0f),
		NSLOCTEXT("IGMissingFloor", "ImpactPrompt", "자재 모서리 얼룩 보기"),
		NSLOCTEXT(
			"IGMissingFloor",
			"ImpactThought",
			"모서리에 검은 얼룩이 배어 있다. 바닥에도 똑같은 얼룩이 있네."),
		EIGMissingFloorTruth::LandingStruggle,
		EIGMissingFloorSource::LandingImpactMark,
		0.0f,
		0.05f,
		/*bPresentationVisible=*/false);
	ImpactMark->OnExamined.AddUObject(
		this, &AIGMissingFloorNightThreeDirector::HandleImpactMarkExamined);

	// Per-bay verbs: an ear (quiet, needs the water) and a fist (loud,
	// needs nothing but nerve). Same answer, two prices — §7's promise.
	WallListens.SetNum(3);
	WallKnocks.SetNum(3);
	for (int32 BayIndex = 0; BayIndex < 3; ++BayIndex)
	{
		const float BayY = IGNightThree::WallBayYs[BayIndex];

		SpawnParameters.Name = *FString::Printf(
			TEXT("MissingFloorWallListen%d"), BayIndex);
		AIGMissingFloorEvidence* Listen =
			World->SpawnActor<AIGMissingFloorEvidence>(
				AIGMissingFloorEvidence::StaticClass(),
				FTransform(
					FRotator::ZeroRotator,
					FVector(247.0f, BayY - 26.0f, 1290.0f)),
				SpawnParameters);
		if (!Listen)
		{
			return false;
		}
		Listen->Configure(
			CubeMesh,
			nullptr,
			FVector(3.0f, 26.0f, 26.0f),
			NSLOCTEXT("IGMissingFloor", "WallListenPrompt", "벽에 귀 대기"),
			FText::GetEmpty(),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			0.8f,
			IGNightThree::ListenLoudness,
			/*bPresentationVisible=*/false);
		Listen->Tags.AddUnique(FName(TEXT("MissingFloor.Verb.Listen")));
		Listen->OnExamined.AddWeakLambda(
			this,
			[this, BayIndex](AIGMissingFloorEvidence*)
			{
				HandleWallListened(BayIndex);
			});
		WallListens[BayIndex] = Listen;

		SpawnParameters.Name = *FString::Printf(
			TEXT("MissingFloorWallKnock%d"), BayIndex);
		AIGMissingFloorEvidence* Knock =
			World->SpawnActor<AIGMissingFloorEvidence>(
				AIGMissingFloorEvidence::StaticClass(),
				FTransform(
					FRotator::ZeroRotator,
					FVector(247.0f, BayY + 26.0f, 1290.0f)),
				SpawnParameters);
		if (!Knock)
		{
			return false;
		}
		Knock->Configure(
			CubeMesh,
			nullptr,
			FVector(3.0f, 26.0f, 26.0f),
			NSLOCTEXT("IGMissingFloor", "WallKnockPrompt", "벽 두드리기"),
			FText::GetEmpty(),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			0.0f,
			IGNightThree::WallEchoKnockLoudness,
			/*bPresentationVisible=*/false);
		Knock->Tags.AddUnique(FName(TEXT("MissingFloor.Verb.Knock")));
		WallKnocks[BayIndex] = Knock;
	}

	// P4's surface on the cavity bay: hidden until the wall is certain and two
	// independent rhythm clues are known.  The prompt exposes only the verb;
	// three player-timed taps, not a hold that auto-solves the pattern, are the
	// actual answer.
	SpawnParameters.Name = TEXT("MissingFloorAnswerTarget");
	AnswerTarget = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(
			FRotator::ZeroRotator,
			FVector(247.0f, IGNightThree::WallBayYs[IGNightThree::CavityBayIndex], 1266.0f)),
		SpawnParameters);
	if (!AnswerTarget)
	{
		return false;
	}
	AnswerTarget->Configure(
		CubeMesh,
		nullptr,
		FVector(3.0f, 40.0f, 20.0f),
		NSLOCTEXT(
			"IGMissingFloor",
			"AnswerPrompt",
			"벽에 대고 박자 맞춰 두드리기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		0.0f,
		AIGPlayerCharacter::KnockLoudness,
		/*bPresentationVisible=*/false);
	AnswerTarget->Tags.AddUnique(FName(TEXT("MissingFloor.Verb.Knock")));

	// T5의 두 번째 출처. 영수증이 「언제 실어 왔는가」라면 이쪽은 「언제
	// 발랐는가」다. 둘이 같은 주를 가리켜야 은폐가 확정된다(§12).
	PlasterDatings.SetNum(3);
	for (int32 BayIndex = 0; BayIndex < 3; ++BayIndex)
	{
		SpawnParameters.Name = *FString::Printf(
			TEXT("MissingFloorPlasterDating%d"), BayIndex);
		AIGMissingFloorEvidence* Dating =
			World->SpawnActor<AIGMissingFloorEvidence>(
				AIGMissingFloorEvidence::StaticClass(),
				FTransform(
					FRotator::ZeroRotator,
					FVector(
						247.0f,
						IGNightThree::WallBayYs[BayIndex],
						IGNightThree::PlasterDatingZ)),
				SpawnParameters);
		if (!Dating)
		{
			return false;
		}
		Dating->Configure(
			CubeMesh,
			nullptr,
			FVector(3.0f, 20.0f, 26.0f),
			NSLOCTEXT("IGMissingFloor", "PlasterDatingPrompt", "새로 바른 벽 이음매 보기"),
			FText::GetEmpty(),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			1.0f,
			IGNightThree::ListenLoudness,
			/*bPresentationVisible=*/false);
		Dating->OnExamined.AddUObject(
			this, &AIGMissingFloorNightThreeDirector::HandlePlasterDatingExamined);
		PlasterDatings[BayIndex] = Dating;
	}

	// §13 12행의 회수. 밤2에 모니터로 본 화각에 직접 서는 순간이다.
	SpawnParameters.Name = TEXT("MissingFloorAnnexRecognitionZone");
	AnnexRecognitionZone = World->SpawnActor<AIGZoneTrigger>(
		AIGZoneTrigger::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGNightThree::AnnexRecognitionCenter),
		SpawnParameters);
	if (!AnnexRecognitionZone)
	{
		return false;
	}
	AnnexRecognitionZone->SetZoneExtent(IGNightThree::AnnexRecognitionExtent);
	AnnexRecognitionZone->OnZoneTriggered.AddDynamic(
		this, &AIGMissingFloorNightThreeDirector::HandleAnnexRecognitionZone);

	// T8의 두 번째 출처. 일지가 두드린 횟수를 세었다면 이쪽은 그 옆에
	// 무엇이 있었는지를 말한다.
	SpawnParameters.Name = TEXT("MissingFloorTankAudition");
	TankAudition = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGNightThree::TankAuditionLocation),
		SpawnParameters);
	if (!TankAudition)
	{
		return false;
	}
	TankAudition->Configure(
		CubeMesh,
		nullptr,
		FVector(24.0f, 1.0f, 14.0f),
		NSLOCTEXT("IGMissingFloor", "TankAuditionPrompt", "저수조 살펴보기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		1.0f,
		IGNightThree::ListenLoudness,
		/*bPresentationVisible=*/false);
	TankAudition->Tags.AddUnique(FName(TEXT("MissingFloor.Verb.Listen")));
	TankAudition->OnExamined.AddUObject(
		this, &AIGMissingFloorNightThreeDirector::HandleTankAuditionExamined);
	AnswerTarget->SetInteractionEnabled(false);
	AnswerTarget->SetActorHiddenInGame(true);

	// -- day papers --------------------------------------------------------

	SpawnParameters.Name = TEXT("MissingFloorLabelsNote");
	LabelsNote = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(),
		FTransform(FRotator(0,180,0), IGNightThree::LabelsLocation
			+ FVector(0,0,Scene->GetDeskSurfaceWorldZ() + .12f)),
		SpawnParameters);
	if (!LabelsNote)
	{
		return false;
	}
	if (UStaticMesh* LabelsMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Meshes/SM_ShippingLabels.SM_ShippingLabels")))
	{
		LabelsNote->ConfigurePrototypeVisuals(LabelsMesh, LabelsMesh->GetMaterial(0), FVector(100));
	}
	else
	{
		LabelsNote->ConfigurePrototypeVisuals(CubeMesh, FreshPaperMaterial, FVector(18,13,.12f));
	}
	LabelsNote->SetReadingArtwork(LoadObject<UTexture2D>(nullptr,
		TEXT("/Game/UI/Reading/T_ShippingLabelRead_D.T_ShippingLabelRead_D")));
	LabelsNote->SetInteractionPrompt(
		NSLOCTEXT("IGMissingFloor", "LabelsPrompt", "배송 라벨 뭉치"));
	LabelsNote->SetNoteText(
		NSLOCTEXT("IGMissingFloor", "LabelsTitle", "공방 창고에서 온 상자"),
		{
			NSLOCTEXT("IGMissingFloor", "Labels1", "받는 분: 백도하"),
			NSLOCTEXT("IGMissingFloor", "Labels2", "무영로 27-3  달빛빌라 옥탑"),
			NSLOCTEXT("IGMissingFloor", "LabelsSender", "보내는 분: 공방 창고"),
			NSLOCTEXT("IGMissingFloor", "LabelsContents", "품목: 조율 공구"),
			FText::GetEmpty(),
			NSLOCTEXT(
				"IGMissingFloor",
				"Labels3",
				"부재 시 앞쪽 편의점 보관"),
		});
	LabelsNote->OnReadStateChanged.AddDynamic(
		this, &AIGMissingFloorNightThreeDirector::HandleLabelsRead);

	SpawnParameters.Name = TEXT("MissingFloorForumNote");
	ForumNote = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGNightThree::ForumLocation),
		SpawnParameters);
	if (!ForumNote)
	{
		return false;
	}
	ForumNote->ConfigurePrototypeVisuals(
		CubeMesh, LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Prototype/Materials/M_LobbyForumPrint.M_LobbyForumPrint")), FVector(21.0f, 0.08f, 29.7f));
	ForumNote->SetInteractionPrompt(
		NSLOCTEXT("IGMissingFloor", "ForumPrompt", "층간소음 게시글 인쇄본"));
	ForumNote->SetNoteText(
		NSLOCTEXT("IGMissingFloor", "ForumTitle", "관리인께 드립니다"),
		{
			NSLOCTEXT("IGMissingFloor", "Forum1", "6/30  새벽 네 시만 되면 위에서 뭘 질질 끕니다. 자다가 매번 깨요."),
			NSLOCTEXT("IGMissingFloor", "Forum2", "7/12  관리인은 창고라 사람이 없대요. 그럼 이 소리는 어디서 나는 건가요?"),
			FText::GetEmpty(),
			NSLOCTEXT("IGMissingFloor", "Forum3", "7/26  03:12  어차피 네 시면 또 시작이에요. 오늘은 올라가 봅니다. 사람인지 뭔지 얼굴이나 보죠."),
			NSLOCTEXT("IGMissingFloor", "Forum4", "댓글 12  ·  참지 마세요 / 잘 생각하셨어요 / 가서 확실하게 말하세요"),
		});
	ForumNote->OnReadStateChanged.AddDynamic(
		this, &AIGMissingFloorNightThreeDirector::HandleForumRead);

	SpawnParameters.Name = TEXT("MissingFloorJournalNote");
	JournalNote = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGNightThree::JournalLocation),
		SpawnParameters);
	if (!JournalNote)
	{
		return false;
	}
	UStaticMesh* CalendarJournalMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/Meshes/SM_CalendarJournal.SM_CalendarJournal"));
	UMaterialInterface* JournalMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_PaperOld.M_PaperOld"));
	JournalNote->ConfigurePrototypeVisuals(
		CalendarJournalMesh ? CalendarJournalMesh : CubeMesh,
		CalendarJournalMesh ? JournalMaterial : nullptr,
		CalendarJournalMesh
			? FVector(100.0f, 100.0f, 100.0f)
			: FVector(17.0f, 1.4f, 24.0f),
		CalendarJournalMesh != nullptr);
	JournalNote->SetInteractionPrompt(
		NSLOCTEXT("IGMissingFloor", "JournalPrompt", "달력 뒷장 소리 일지"));
	JournalNote->SetNoteText(
		NSLOCTEXT("IGMissingFloor", "JournalTitle", "401호 황순금 소리 일지"),
		{
			NSLOCTEXT("IGMissingFloor", "Journal1", "7/27  네 시 반쯤 또 깸. 위에서 다섯 번. 관리실 전화 안 받음."),
			NSLOCTEXT("IGMissingFloor", "Journal2", "7/28  또 다섯 번. 어제보다 힘이 없음."),
			NSLOCTEXT("IGMissingFloor", "Journal3", "7/29  벽이 하도 울어서 나도 두드려 줬다. 저쪽이 하던 대로 둘, 쉬고, 하나. 그랬더니 조용하데. 사람인가."),
			NSLOCTEXT("IGMissingFloor", "Journal4", "7/30  네 번 들음. 수도 틀자 안 들림."),
			NSLOCTEXT("IGMissingFloor", "Journal5", "7/31  오늘은 세 번. 귀가 먹은 건지."),
			FText::GetEmpty(),
			NSLOCTEXT("IGMissingFloor", "Journal6", "8/1"),
		});
	JournalNote->OnReadStateChanged.AddDynamic(
		this, &AIGMissingFloorNightThreeDirector::HandleJournalRead);
	// Earned, not found: she hands it out only after the truth of the
	// knocking is known, and only by daylight.
	RefreshJournalAvailability(/*bHourActive=*/true);

	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		// The opening phone transcript is guaranteed starting knowledge. It is
		// still recorded as provenance so P4 needs one more independent clue,
		// instead of treating the protagonist's memory as invisible permission.
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::WaitingForAnAnswer,
			EIGMissingFloorSource::AnswerRhythmVoicemail);
		TruthHandle = Narrative->OnTruthConfirmed.AddUObject(
			this, &AIGMissingFloorNightThreeDirector::HandleTruthConfirmed);
	}
	return true;
}

bool AIGMissingFloorNightThreeDirector::IsPlayerKnockTarget(
	const AActor* FocusedActor) const
{
	if (!IsValid(FocusedActor))
	{
		return false;
	}
	if (FocusedActor == AnswerTarget
		&& AnswerTarget
		&& AnswerTarget->IsInteractionEnabled()
		&& !AnswerTarget->IsHidden())
	{
		return true;
	}
	for (const AIGMissingFloorEvidence* KnockTarget : WallKnocks)
	{
		if (FocusedActor == KnockTarget
			&& IsValid(KnockTarget)
			&& KnockTarget->IsInteractionEnabled()
			&& !KnockTarget->IsHidden())
		{
			return true;
		}
	}
	return false;
}

bool AIGMissingFloorNightThreeDirector::IsPlayerListenTarget(
	const AActor* FocusedActor) const
{
	if (!IsValid(FocusedActor))
	{
		return false;
	}
	for (const AIGMissingFloorEvidence* ListenTarget : WallListens)
	{
		if (FocusedActor == ListenTarget
			&& IsValid(ListenTarget)
			&& ListenTarget->IsInteractionEnabled()
			&& !ListenTarget->IsHidden())
		{
			return true;
		}
	}
	return false;
}

bool AIGMissingFloorNightThreeDirector::TryPlayerKnock(
	AActor* FocusedActor,
	AActor* NoiseInstigator)
{
	if (!IsPlayerKnockTarget(FocusedActor))
	{
		return false;
	}

	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			// 대답 노크는 손가락 마디, 벽을 읽는 주먹은 그보다 크다.
			Noise->ReportNoise(
				FocusedActor->GetActorLocation(),
				FocusedActor == AnswerTarget
					? AIGPlayerCharacter::KnockLoudness
					: IGNightThree::WallEchoKnockLoudness,
				NoiseInstigator);
		}
	}

	if (FocusedActor == AnswerTarget)
	{
		HandleAnswerKnock(AnswerTarget);
		return true;
	}
	for (int32 BayIndex = 0; BayIndex < WallKnocks.Num(); ++BayIndex)
	{
		if (FocusedActor == WallKnocks[BayIndex])
		{
			HandleWallKnocked(BayIndex);
			return true;
		}
	}
	return false;
}

bool AIGMissingFloorNightThreeDirector::TryPlayerListen(
	AActor* FocusedActor,
	AActor* NoiseInstigator)
{
	if (!IsPlayerListenTarget(FocusedActor))
	{
		return false;
	}

	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(
				FocusedActor->GetActorLocation(),
				IGNightThree::ListenLoudness,
				NoiseInstigator);
		}
	}
	for (int32 BayIndex = 0; BayIndex < WallListens.Num(); ++BayIndex)
	{
		if (FocusedActor == WallListens[BayIndex])
		{
			HandleWallListened(BayIndex);
			return true;
		}
	}
	return false;
}

void AIGMissingFloorNightThreeDirector::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(AnswerTimer);
	GetWorldTimerManager().ClearTimer(AnswerSilenceReleaseTimer);
	GetWorldTimerManager().ClearTimer(AnswerAftermathTimer);
	GetWorldTimerManager().ClearTimer(RiserResumeTimer);
	GetWorldTimerManager().ClearTimer(ReturnTimer);
	GetWorldTimerManager().ClearTimer(ReturnThoughtTimer);
	GetWorldTimerManager().ClearTimer(RepeatPassTimer);
	GetWorldTimerManager().ClearTimer(WallWaterFadeTimer);
	GetWorldTimerManager().ClearTimer(TankSloshFadeTimer);
	GetWorldTimerManager().ClearTimer(TunerCartRollTimer);
	GetWorldTimerManager().ClearTimer(TunerCartThoughtTimer);
	for (FTimerHandle& HitTimer : AnswerHitTimers)
	{
		GetWorldTimerManager().ClearTimer(HitTimer);
	}
	GetWorldTimerManager().ClearTimer(JournalHandoverTimer);
	GetWorldTimerManager().ClearTimer(JournalHandoverSoundTimer);
	GetWorldTimerManager().ClearTimer(SeoWatchTimer);
	ReleaseReturnFigure();
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->SetAuthoredSilence(false);
		}
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->OnTruthConfirmed.Remove(TruthHandle);
	}
	if (RiserFlow)
	{
		RiserFlow->Stop();
	}
	// 끊어 줄 타이머를 방금 지웠으니 물소리 루프는 여기서 멈춘다.
	for (UAudioComponent* Loop : {WallWaterVoice.Get(), TankSloshVoice.Get()})
	{
		if (Loop)
		{
			Loop->Stop();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AIGMissingFloorNightThreeDirector::SetHourActive(const bool bHourActive)
{
	RefreshKeyringAvailability();
	RefreshTuningHammerAvailability();
	bHourCurrentlyActive = bHourActive;
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	// 대답은 한 번 받으면 끝이다. 저장에서 이어 붙인 세션은 이 플래그가 비어
	// 있어서, 수첩을 다시 읽으면 벽이 다시 열리고 P4가 헛돈다.
	if (Narrative
		&& Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer)
		&& !bAnswerDelivered
		&& !bAnswerPending)
	{
		bAnswerDelivered = true;
		bSolvedAnnounced = true;
		if (AnswerTarget)
		{
			AnswerTarget->SetActorHiddenInGame(true);
			AnswerTarget->SetInteractionEnabled(false);
		}
	}
	if (!bHourActive)
	{
		GetWorldTimerManager().ClearTimer(AnswerSilenceReleaseTimer);
		EndAnswerSilence();
		// 8초 정적 안에서 05:30이 오면 대답은 오지 않는다. 낮에 대답이 돌아오면
		// 귀환길 없이 T9만 남으니, 벽은 다음 밤에 다시 두드릴 수 있게 둔다.
		if (bAnswerPending)
		{
			GetWorldTimerManager().ClearTimer(AnswerTimer);
			bAnswerPending = false;
			AnswerTapTimes.Reset();
			// 대답을 기다리며 걷어 둔 물길은 대답이 와야 되살아났다. 끊긴 대답은
			// 그 자리를 못 지나가므로 여기서 되살린다. 밸브는 여전히 열려 있다.
			ResumeRiserFlow(IGNightThree::RiserResumeFadeInSeconds);
		}
		GetWorldTimerManager().ClearTimer(AnswerAftermathTimer);
		GetWorldTimerManager().ClearTimer(ReturnThoughtTimer);
		GetWorldTimerManager().ClearTimer(RepeatPassTimer);
		bRepeatedPass = false;
		// 새벽은 어떻게 왔든 복도를 비운다. 시간 초과로 왔다면 그를 낮의
		// 복도에 세워 둔 채로 남기면 안 된다.
		GetWorldTimerManager().ClearTimer(ReturnTimer);
		ReleaseReturnFigure();
		if (ReturnStage != EIGNightThreeReturnStage::Home)
		{
			ReturnStage = EIGNightThreeReturnStage::Idle;
		}
	}
	// 대답하는 벽은 밤에만 선다. 경계마다 다시 잰다 — 새벽에는 거두고, 밤이
	// 오면 세운다. 불러온 되풀이 밤3에서는 수첩을 다시 펼치기 전까지 벽을 켜
	// 줄 곳이 여기뿐이다.
	RefreshAnswerTargetAvailability();
	RefreshJournalAvailability(bHourActive);
	RefreshDistantSeoVisibility();
	// 대답을 듣고도 05:30을 넘긴 밤3은 다음 저녁에 다시 온다. T9는 이미 있어서
	// OnSolved가 다시 오지 않으니 귀환길은 여기서 다시 세운다. 그러지 않으면
	// 403호로 돌아와도 밤이 끝나지 않고, 밤3이 끝없이 되풀이된다.
	if (bHourActive
		&& Narrative
		&& Narrative->GetNightIndex() == 3
		&& Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer)
		&& !Narrative->HasBeatPlayed(AIGNightPhaseDirector::GoalBeatId(3))
		&& ReturnStage != EIGNightThreeReturnStage::Passing)
	{
		ReturnStage = EIGNightThreeReturnStage::Idle;
		RearmReturnPassForRepeatedNight();
	}
}

AIGMissingFloorEvidence* AIGMissingFloorNightThreeDirector::GetWallListen(
	const int32 BayIndex) const
{
	return WallListens.IsValidIndex(BayIndex) ? WallListens[BayIndex] : nullptr;
}

bool AIGMissingFloorNightThreeDirector::GetCavityWallObservationPoint(
	FVector& OutLocation) const
{
	const AIGMissingFloorEvidence* CavityListen =
		GetWallListen(IGNightThree::CavityBayIndex);
	if (!CavityListen)
	{
		return false;
	}
	if (const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		if (Narrative->IsPuzzleSolved(IGNightThree::PuzzleThreeId))
		{
			return false;
		}
	}
	// A little out from the face, so he ends up beside the wall rather than
	// inside it, and the player sees a body against a surface.
	OutLocation = CavityListen->GetActorLocation() - FVector(70.0f, 0.0f, 0.0f);
	return true;
}

void AIGMissingFloorNightThreeDirector::RefreshKeyringAvailability()
{
	if (!Keyring) { return; }
	const bool bTaken = IGStory::HasState(this, FGameplayTag::RequestGameplayTag(
		FName(TEXT("State.MissingFloor.HasStairKey")), false));
	Keyring->SetActorHiddenInGame(bTaken);
	Keyring->SetActorEnableCollision(!bTaken);
	Keyring->SetInteractionEnabled(!bTaken);
}

void AIGMissingFloorNightThreeDirector::RefreshTuningHammerAvailability()
{
	if (!TuningHammer)
	{
		return;
	}
	// 챙겼으면 유담이 들고 있다. 못 챙긴 회차도 벽이 열린 뒤에는 자재 위에
	// 두지 않는다. 밤4 선택 지점에 렌치가 한 자루 놓이는데, 여기 한 자루가
	// 더 있으면 돌려놓을 물건이 둘이 된다. 막간의 검은 화면이 끝날 때 치운다.
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const bool bGone = Narrative
		&& (Narrative->HasBeatPlayed(IGNightThree::WrenchTakenBeatId)
			|| Narrative->IsNightFourWallOpened()
			|| Narrative->WasFifthDawnInterludeCompleted());
	TuningHammer->SetActorHiddenInGame(bGone);
	TuningHammer->SetActorEnableCollision(!bGone);
	TuningHammer->SetInteractionEnabled(!bGone);
}

void AIGMissingFloorNightThreeDirector::HandleKeyringTaken(
	AIGMissingFloorEvidence* Evidence)
{
	// A key is a persistent fact: the tag is registered, granted once, and
	// saved — the gate's requirement reads it from then on.
	IGStory::AddState(
		this,
		FGameplayTag::RequestGameplayTag(
			FName(TEXT("State.MissingFloor.HasStairKey")), false));
	RefreshKeyringAvailability();
	// 아직 읽지 않았다면 옆에 놓인 문자 사본을 짚어 준다. 밤2부터 있는 종이다.
	if (const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		if (!Narrative->HasSource(
			EIGMissingFloorTruth::WasStillAlive,
			EIGMissingFloorSource::AgentMoveOutMessage))
		{
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT(
					"IGMissingFloor",
					"KeyringPaperThought",
					"열쇠 옆에 부동산 문자 뽑아 둔 게 있네."),
				3.8f);
		}
	}
}

void AIGMissingFloorNightThreeDirector::HandleValveOpened(
	AIGMissingFloorEvidence* Evidence)
{
	if (bValveOpen)
	{
		return;
	}
	bValveOpen = true;

	if (Evidence)
	{
		Evidence->SetInteractionPrompt(
			NSLOCTEXT("IGMissingFloor", "ValveOpenPrompt", "열린 배관 밸브"));
	}
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateValveOpen(
			this,
			IGNightThree::ShaftValveIndex),
		IGNightThree::ValveLocation,
		0.7f,
		1.0f,
		160.0f,
		1400.0f,
		EIGAudioBus::Puzzle);

	// Water starts moving behind exactly one of three identical walls.
	if (UWorld* World = GetWorld())
	{
		RiserFlow = NewObject<UAudioComponent>(this, TEXT("RiserFlowBed"));
		RiserFlow->RegisterComponent();
		RiserFlow->SetWorldLocation(FVector(
			310.0f,
			IGNightThree::WallBayYs[IGNightThree::CavityBayIndex],
			1300.0f));
		// From out in the annex the riser is heard through finished wall, so it
		// arrives at the third band — present, but not yet locatable. Standing
		// at a wall and listening is what opens the band up (§21.3 원근 4단).
		RiserFlow->SetSound(
			UIGToneSequenceSoundWave::CreatePipeWaterFlow(
				this,
				IGNightThree::RiserBedDistanceStep));
		RiserFlow->AttenuationSettings = IGAudio::MakeAttenuation(
			this,
			120.0f,
			900.0f,
			EIGAudioBus::Puzzle);
		RiserFlow->bAllowSpatialization = true;
		RiserFlow->SetVolumeMultiplier(0.5f);
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			// 밤 내내 흐르는 물길이다. 벽을 여러 번 듣다 PUZZLE 상한에 닿아도
			// 이것이 먼저 밀려나면 안 된다.
			AudioDirector->RegisterPersistentBed(RiserFlow, EIGAudioBus::Puzzle);
		}
		RiserFlow->Play();
	}

	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"ValveThought",
			"물 내려가는 소리가 난다. 이제 벽에 귀를 대 보자."),
		3.8f);
}

void AIGMissingFloorNightThreeDirector::PlayWallListenResponse(
	const int32 BayIndex,
	const bool bHollow)
{
	const FVector WallLocation =
		WallListens.IsValidIndex(BayIndex) && WallListens[BayIndex]
			? WallListens[BayIndex]->GetActorLocation()
			: GetActorLocation();

	// The wall's own answer. This is the puzzle: a cavity rings on its
	// mass-air-mass note, a solid wall dies in a fifth of a second. Doha's memo
	// says 속이 찬 벽은 짧게 죽고 빈 벽은 길게 운다, and now that is literally
	// what the two walls do.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateWallCavityResponse(this, bHollow),
		WallLocation,
		0.92f,
		1.0f,
		IGNightThree::WallListenInnerRadius,
		IGNightThree::WallListenFalloff,
		EIGAudioBus::Puzzle);

	// And the water, at the band the structure left it in.
	if (bValveOpen)
	{
		// 물소리는 루프 파형이라 스스로 멎지 않는다. 한 번에 한 벽의 물만 흐르게
		// 앞 벽의 것을 걷고, 이 벽의 것도 독백이 끝날 무렵 걷는다. 놓아두면
		// 들은 벽마다 밤새 흘러 세 벽을 견줄 수 없다.
		if (UAudioComponent* Previous = WallWaterVoice.Get())
		{
			Previous->FadeOut(0.25f, 0.0f);
		}
		WallWaterVoice = IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreatePipeWaterFlow(
				this,
				bHollow
					? IGNightThree::CavityWallDistanceStep
					: IGNightThree::SolidWallDistanceStep),
			WallLocation,
			bHollow ? 0.80f : 0.58f,
			1.0f,
			IGNightThree::WallListenInnerRadius,
			IGNightThree::WallListenFalloff,
			EIGAudioBus::Puzzle);
		GetWorldTimerManager().SetTimer(
			WallWaterFadeTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (UAudioComponent* Water = WallWaterVoice.Get())
				{
					Water->FadeOut(0.9f, 0.0f);
				}
			}),
			// ListenCavity 4.0초, ListenFar 3.2초와 같이 끝난다.
			bHollow ? 4.0f : 3.2f,
			false);
	}
}

void AIGMissingFloorNightThreeDirector::HandleWorkGloveExamined(
	AIGMissingFloorEvidence* Evidence)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RecordWitness(EIGMissingFloorWitness::AnnexWorkGlove);
	}
}

void AIGMissingFloorNightThreeDirector::HandleTuningHammerExamined(
	AIGMissingFloorEvidence* Evidence)
{
	// 집어 드는 손에서 입주 날 옥상 철문 너머의 그 쇠가 운다(§8 0-4). 그날은
	// 문이 780Hz 밑만 남겼고, 여기서는 같은 쇠가 밝게 난다. 중고 거래 글을 못
	// 본 회차에도 난다.
	if (Evidence)
	{
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateTuningWrenchClink(this),
			Evidence->GetActorLocation(),
			0.42f,
			1.0f,
			60.0f,
			700.0f,
			EIGAudioBus::Player);
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	// 집어 든다. 밤4 엔딩 A의 「돌려놓고」가 이 손에서 나간다.
	if (Narrative)
	{
		Narrative->MarkBeatPlayed(IGNightThree::WrenchTakenBeatId);
	}
	RefreshTuningHammerAvailability();
	// 소리가 먼저 나고, 이 줄은 렌치 독백 다음에 선다. 옥상 문 앞에서 그 소리를
	// 들은 회차만.
	if (Narrative
		&& Narrative->HasBeatPlayed(FName(TEXT("Arrival.RoofDoor")))
		&& Narrative->MarkBeatPlayed(FName(TEXT("Night3.HammerArrivalEcho"))))
	{
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"TuningHammerArrivalEcho",
				"이사 온 날 옥상 문 너머에서 들린 그 소리다."),
			3.6f);
	}
	// §13 13행의 회수. 중고 거래 글을 본 회차에만 이 한 줄이 붙는다 —
	// 못 본 사람에게는 그냥 오빠가 떨어뜨린 렌치이고, 그것도 맞는 말이다.
	if (!Narrative
		|| !Narrative->HasBeatPlayed(FName(TEXT("Day.UsedListing")))
		|| !Narrative->MarkBeatPlayed(FName(TEXT("Night3.HammerListing"))))
	{
		return;
	}
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"TuningHammerListingThought",
			"목록에 있던 공구다. 이것까지 팔지는 못했네."),
		5.0f);
}

void AIGMissingFloorNightThreeDirector::HandleTunerCartPushed(
	AIGMissingFloorEvidence* Evidence)
{
	if (Evidence)
	{
		Evidence->SetInteractionEnabled(false);
	}
	UWorld* World = GetWorld();
	if (!World || !TunerToolCart)
	{
		return;
	}
	const FVector CartAt = TunerToolCart->GetComponentLocation();
	// 입주 저녁 천장 너머에서 구르던 바퀴다. 금 간 캐스터가 같은 박자로
	// 딸깍이다 선다. 알아보는 한 줄은 바퀴가 멎은 뒤에 온다.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateToolCartRoll(this),
		CartAt + FVector(0.0f, 0.0f, 10.0f),
		0.55f,
		1.0f,
		90.0f,
		1400.0f,
		EIGAudioBus::World);
	GetWorldTimerManager().SetTimer(
		TunerCartThoughtTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT(
					"IGMissingFloor",
					"TunerCartThought",
					"바퀴 하나가 깨졌네. 천장에서 들리던 소리랑 비슷하다."),
				4.2f);
		}),
		1.6f,
		false);

	// 미는 쪽으로 한 뼘 구른다. 벽이나 자재에 막히면 막힌 데까지만 간다.
	FVector Push = FVector::ForwardVector;
	if (const AIGPlayerCharacter* Pusher = Player.Get())
	{
		const FVector Away = (CartAt - Pusher->GetActorLocation()).GetSafeNormal2D();
		if (!Away.IsNearlyZero())
		{
			Push = Away;
		}
	}
	float Distance = 12.0f;
	const FBoxSphereBounds Bounds = TunerToolCart->Bounds;
	const FVector Extent(
		FMath::Max(1.0f, Bounds.BoxExtent.X - 2.0f),
		FMath::Max(1.0f, Bounds.BoxExtent.Y - 2.0f),
		FMath::Max(1.0f, Bounds.BoxExtent.Z - 4.0f));
	// 바닥에 닿지 않게 조금 띄워서 쓴다.
	const FVector SweepStart = Bounds.Origin + FVector(0.0f, 0.0f, 4.0f);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IGTunerCartPush), false, this);
	if (Evidence)
	{
		Query.AddIgnoredActor(Evidence);
	}
	if (AIGPlayerCharacter* Pusher = Player.Get())
	{
		Query.AddIgnoredActor(Pusher);
	}
	FCollisionObjectQueryParams Blockers;
	Blockers.AddObjectTypesToQuery(ECC_WorldStatic);
	Blockers.AddObjectTypesToQuery(ECC_WorldDynamic);
	FHitResult Hit;
	if (World->SweepSingleByObjectType(
			Hit,
			SweepStart,
			SweepStart + Push * (Distance + 2.0f),
			FQuat::Identity,
			Blockers,
			FCollisionShape::MakeBox(Extent),
			Query))
	{
		Distance = FMath::Max(0.0f, Hit.Distance - 2.0f);
	}
	if (Distance < 1.0f)
	{
		return;
	}
	TunerCartRollFrom = CartAt;
	TunerCartRollTo = CartAt + Push * Distance;
	TunerCartRollStartSeconds = World->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(
		TunerCartRollTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const UWorld* RollWorld = GetWorld();
			if (!RollWorld || !TunerToolCart)
			{
				GetWorldTimerManager().ClearTimer(TunerCartRollTimer);
				return;
			}
			const float Alpha = FMath::Clamp(
				static_cast<float>((RollWorld->GetTimeSeconds() - TunerCartRollStartSeconds) / 1.3),
				0.0f,
				1.0f);
			// 민 힘이 빠지는 만큼 느려진다. 소리의 딸깍 간격이 벌어지는 것과 같다.
			const float Eased = 1.0f - FMath::Square(1.0f - Alpha);
			TunerToolCart->SetWorldLocation(
				FMath::Lerp(TunerCartRollFrom, TunerCartRollTo, Eased));
			if (Alpha >= 1.0f)
			{
				GetWorldTimerManager().ClearTimer(TunerCartRollTimer);
			}
		}),
		1.0f / 30.0f,
		true);
}

void AIGMissingFloorNightThreeDirector::HandleAnnexRecognitionZone(
	AIGZoneTrigger* Zone)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	// 채널 5를 안 눌렀으면 회수할 것이 없다. 못 본 사람에게 「그때 그
	// 화면」이라고 말하면 있지도 않은 기억을 지어내는 셈이다.
	if (!Narrative->HasBeatPlayed(FName(TEXT("Night2.CCTV"))))
	{
		return;
	}
	if (!Narrative->MarkBeatPlayed(FName(TEXT("Night3.AnnexRecognition"))))
	{
		return;
	}
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"AnnexRecognitionThought",
			"CCTV에서 본 곳이다. 저 전구, 왼쪽으로 꺾이는 복도."),
		4.8f);
}

void AIGMissingFloorNightThreeDirector::HandlePlasterDatingExamined(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	Narrative->RegisterTruthSource(
		EIGMissingFloorTruth::WallSealedThatDay,
		EIGMissingFloorSource::FreshPlasterDating);
	// 기록 화면의 「덧댄 벽」 카드가 이 독백의 앞 문장을 그대로 옮긴다.
	// 한쪽을 고치면 다른 쪽도 같이 고친다.
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"PlasterDatingThought",
			"안쪽은 바싹 말랐는데, 바깥 실리콘은 덜 굳었어. 얼마 전에 또 막았나?"),
		4.6f);
}

void AIGMissingFloorNightThreeDirector::HandleTankAuditionExamined(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	UWorld* World = GetWorld();
	if (!Narrative || !World)
	{
		return;
	}
	Narrative->RegisterTruthSource(
		EIGMissingFloorTruth::FiveNightsOfThirst,
		EIGMissingFloorSource::TankWaterAudition);

	// 수위는 눈으로, 물의 움직임은 소리로 확인한다. 출렁임은 8.8초 루프라
	// 물이 한 번 밀려갔다 돌아와 강판을 치는 만큼만 듣고 걷는다. 다시 살피면
	// 앞의 것부터 걷는다 — 살필 때마다 탱크 소리가 한 겹씩 쌓이면 안 된다.
	if (UAudioComponent* Previous = TankSloshVoice.Get())
	{
		Previous->FadeOut(0.3f, 0.0f);
	}
	TankSloshVoice = IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateRooftopTankSlosh(this),
		IGNightThree::TankAuditionLocation,
		0.62f,
		1.0f,
		120.0f,
		900.0f,
		EIGAudioBus::Puzzle);
	GetWorldTimerManager().SetTimer(
		TankSloshFadeTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (UAudioComponent* Slosh = TankSloshVoice.Get())
			{
				Slosh->FadeOut(1.2f, 0.0f);
			}
		}),
		4.6f,
		false);
	AIGHorrorHUD::PushAudioCaption(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"TankAuditionCaption",
			"[안쪽] 물이 아주 천천히 흐르는 소리"),
		3.0f);

	// 일지를 이미 읽었다면 두 사실이 여기서 맞물린다. 안 읽었으면 그냥
	// 가득 찬 탱크다 — 순서를 강제하지 않는 것이 §7의 자유 경로다.
	const bool bKnowsTally = Narrative->HasSource(
		EIGMissingFloorTruth::FiveNightsOfThirst,
		EIGMissingFloorSource::KnockTallyJournal);
	// 용량 표찰과 수위계는 같은 탱크에 붙어 있다. 기록에도 관찰한 수위를 남긴다.
	AIGHorrorHUD::PushThought(
		this,
		bKnowsTally
			? NSLOCTEXT(
				"IGMissingFloor",
				"TankAuditionThoughtCrossed",
				"탱크에 물이 가득 찼다. 벽 바로 뒤로 배관이 지나가네.")
			: NSLOCTEXT(
				"IGMissingFloor",
				"TankAuditionThought",
				"이 밑으로 급수관이 내려가네."),
		bKnowsTally ? 5.0f : 4.6f);
}

void AIGMissingFloorNightThreeDirector::HandleWallListened(const int32 BayIndex)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!bValveOpen)
	{
		// Before the valve there is nothing driving the wall, so all three
		// answer alike — and they answer, rather than being described as
		// answering. The dead board is why the player goes looking for water.
		// 「세 벽 다」는 세 칸을 다 들어 본 뒤에야 할 수 있는 말이다.
		DryListenedBays.Add(BayIndex);
		PlayWallListenResponse(BayIndex, /*bHollow=*/false);
		AIGHorrorHUD::PushThought(
			this,
			DryListenedBays.Num() >= 3
				? NSLOCTEXT(
					"IGMissingFloor",
					"ListenSilent",
					"조용하네. 세 벽 다 똑같아.")
				: DryListenedBays.Num() == 2
					? NSLOCTEXT(
						"IGMissingFloor",
						"ListenSilentAgain",
						"조용하네. 여기도 똑같아.")
					: NSLOCTEXT(
						"IGMissingFloor",
						"ListenSilentFirst",
						"조용하네."),
			3.2f);
		return;
	}
	// 밸브 뒤로는 칸마다 소리가 다르다. 그래도 한 칸만 듣고 「이 벽만」이라고
	// 할 수는 없다. 다른 칸 하나와 견준 뒤에야 독백이 들은 것을 확인한다(§7).
	ComparedBays.Add(BayIndex);
	const bool bCompared = HasComparedWalls();
	// 수첩을 안 읽었으면 소리가 다르다는 것까지만 안다. 그게 무슨 뜻인지는
	// 오빠가 적어 놓은 줄이 말해 준다 — 빠진 조각이 있다는 것은 알려야 한다.
	const bool bKnowsCriterion = Narrative
		&& Narrative->HasSource(
			EIGMissingFloorTruth::SomeoneInTheWall,
			EIGMissingFloorSource::PipeAuditionCriterion);
	if (BayIndex == IGNightThree::CavityBayIndex)
	{
		PlayWallListenResponse(BayIndex, /*bHollow=*/true);
		AIGHorrorHUD::PushThought(
			this,
			!bCompared
				? NSLOCTEXT(
					"IGMissingFloor",
					"ListenCavityFirst",
					"바로 뒤로 물이 흐른다. 소리가 길게 울리네.")
				: bKnowsCriterion
					? NSLOCTEXT(
						"IGMissingFloor",
						"ListenCavity",
						"바로 뒤로 물이 흐른다. 이 벽만 속이 빈 것 같아.")
					: NSLOCTEXT(
						"IGMissingFloor",
						"ListenCavityUnread",
						"바로 뒤로 물이 흐른다. 이 벽만 소리가 달라. 왜 이러지?"),
			4.0f);
	}
	else
	{
		PlayWallListenResponse(BayIndex, /*bHollow=*/false);
		// 비교 상대는 귀로 들었을 수도, 주먹으로 쳤을 수도 있다. 어느 쪽이든
		// 공동 칸은 길게 울었으니 그것을 말한다.
		AIGHorrorHUD::PushThought(
			this,
			!bCompared
				? NSLOCTEXT(
					"IGMissingFloor",
					"ListenFar",
					"물소리가 멀리서 웅웅거리네.")
				: bKnowsCriterion
					? NSLOCTEXT(
						"IGMissingFloor",
						"ListenFarCompared",
						"여기선 물소리가 멀어. 아까 그 벽만 속이 비어 있었어.")
					: NSLOCTEXT(
						"IGMissingFloor",
						"ListenFarComparedUnread",
						"여기선 물소리가 멀어. 아까 그 벽만 소리가 길게 울렸는데."),
			bCompared ? 3.8f : 3.2f);
	}
	if (bCompared && Narrative)
	{
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::SomeoneInTheWall,
			EIGMissingFloorSource::PipeWaterComparison);
		if (Narrative->HasTruth(EIGMissingFloorTruth::SomeoneInTheWall))
		{
			Narrative->MarkPuzzleSolved(IGNightThree::PuzzleThreeId);
		}
	}
}

void AIGMissingFloorNightThreeDirector::HandleWallKnocked(const int32 BayIndex)
{
	// The reckless route: the fist reads the wall instantly, and the sound
	// it makes is real — reported by the evidence actor itself.
	// 주먹은 한 번이다. 셋을 치면 그의 사냥 노크(0.62초 간격 셋)를 흉내 낸다.
	// 치는 소리는 맨손 노크와 같은 석고 녹음 하나로 칸마다 같게 두고, 칸의
	// 차이는 벽이 대답하는 꼬리(공동 1.45초, 찬 벽 0.22초)에서만 난다. 치는
	// 소리까지 다르면 벽이 아니라 손을 비교하게 된다.
	const bool bHollow = BayIndex == IGNightThree::CavityBayIndex;
	const FVector KnockLocation =
		WallKnocks.IsValidIndex(BayIndex) && WallKnocks[BayIndex]
			? WallKnocks[BayIndex]->GetActorLocation()
			: GetActorLocation();
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Knock_Plaster_0"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.0f); }),
		KnockLocation,
		0.8f,
		1.0f,
		160.0f,
		1400.0f,
		EIGAudioBus::Player);
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateWallCavityResponse(this, bHollow),
		KnockLocation,
		1.0f,
		1.0f,
		IGNightThree::WallListenInnerRadius,
		IGNightThree::WallListenFalloff,
		EIGAudioBus::Puzzle);

	ComparedBays.Add(BayIndex);
	const bool bCompared = HasComparedWalls();
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const bool bKnowsCriterion = Narrative
		&& Narrative->HasSource(
			EIGMissingFloorTruth::SomeoneInTheWall,
			EIGMissingFloorSource::PipeAuditionCriterion);
	if (bHollow)
	{
		AIGHorrorHUD::PushThought(
			this,
			!bCompared
				? NSLOCTEXT(
					"IGMissingFloor",
					"KnockCavityFirst",
					"소리가 오래 울리네.")
				: bKnowsCriterion
					? NSLOCTEXT(
						"IGMissingFloor",
						"KnockCavity",
						"소리가 오래 울린다. 안이 비어 있어.")
					: NSLOCTEXT(
						"IGMissingFloor",
						"KnockCavityUnread",
						"소리가 오래 울린다. 이 벽만 달라. 오빠였으면 왜 그런지 알았을 텐데."),
			3.8f);
	}
	else
	{
		AIGHorrorHUD::PushThought(
			this,
			!bCompared
				? NSLOCTEXT(
					"IGMissingFloor",
					"KnockSolid",
					"소리가 금방 끊긴다.")
				: bKnowsCriterion
					? NSLOCTEXT(
						"IGMissingFloor",
						"KnockSolidCompared",
						"소리가 금방 끊기네. 아까 그 벽만 속이 비어 있었어.")
					: NSLOCTEXT(
						"IGMissingFloor",
						"KnockSolidComparedUnread",
						"소리가 금방 끊기네. 아까 그 벽만 길게 울렸는데."),
			bCompared ? 3.6f : 3.0f);
	}
	if (bCompared && Narrative)
	{
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::SomeoneInTheWall,
			EIGMissingFloorSource::WallEchoByHand);
		if (Narrative->HasTruth(EIGMissingFloorTruth::SomeoneInTheWall))
		{
			Narrative->MarkPuzzleSolved(IGNightThree::PuzzleThreeId);
		}
	}
}

bool AIGMissingFloorNightThreeDirector::HasComparedWalls() const
{
	return ComparedBays.Contains(IGNightThree::CavityBayIndex)
		&& ComparedBays.Num() >= 2;
}

void AIGMissingFloorNightThreeDirector::HandleImpactMarkExamined(
	AIGMissingFloorEvidence* Evidence)
{
	// The evidence actor already filed LandingImpactMark; nothing extra.
}

void AIGMissingFloorNightThreeDirector::HandleAnswerKnock(
	AIGMissingFloorEvidence* Evidence)
{
	// 벽은 밤에만 대답한다. 낮에는 표적이 서지 않지만, 서 있던 표적을 낮에
	// 두드리는 길이 하나라도 남으면 지정 침묵과 T9가 낮에 온다.
	if (!bHourCurrentlyActive || bAnswerPending || bAnswerDelivered)
	{
		return;
	}

	// Every interaction is one physical tap. The player owns the silence
	// between taps; no progress bar or prompt leaks the accepted cadence.
	// 그녀의 손은 어느 벽에서나 같은 석고 녹음이다(맨손 노크). 탭마다 녹음을
	// 돌려 친다 — 시각 해시는 4초 남짓마다 한 번 바뀌어 세 탭이 한 녹음이 된다.
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleVariantOr(
			TEXT("Knock_Plaster"),
			3,
			(AnswerTapSerial++ % 3u) << 12,
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.0f); }),
		Evidence ? Evidence->GetActorLocation() : GetActorLocation(),
		0.82f,
		1.0f,
		160.0f,
		1400.0f,
		EIGAudioBus::Player);
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	// 박자 맞추기 도움은 여기서도 같은 배율로 창을 넓힌다. 벽 앞에서 익힌
	// 박자가 복도에서 안 통하면 도움이 반쪽이다.
	const double WindowScale = AIGListenerEntity::GetAnswerWindowScale(this);
	if (AnswerTapTimes.Num() > 0
		&& Now - AnswerTapTimes.Last() > AIGListenerEntity::AnswerSequenceResetSeconds * WindowScale)
	{
		AnswerTapTimes.Reset();
	}
	AnswerTapTimes.Add(Now);
	while (AnswerTapTimes.Num() > 3)
	{
		AnswerTapTimes.RemoveAt(0);
	}
	if (AnswerTapTimes.Num() < 3)
	{
		return;
	}

	const double PairInterval = AnswerTapTimes[1] - AnswerTapTimes[0];
	const double RestInterval = AnswerTapTimes[2] - AnswerTapTimes[1];
	if (!AIGListenerEntity::MatchesAnswerCadence(PairInterval, RestInterval, WindowScale))
	{
		// Keep a plausible new pair, otherwise make this tap the next attempt's
		// first beat. Failure feedback is only the ordinary wall resonance.
		if (RestInterval >= IGNightThree::AnswerPairMinSeconds
			&& RestInterval <= IGNightThree::AnswerPairMaxSeconds * WindowScale)
		{
			AnswerTapTimes.RemoveAt(0);
		}
		else
		{
			AnswerTapTimes.Reset();
			AnswerTapTimes.Add(Now);
		}
		return;
	}
	// 박자는 맞았지만 05:30에 눈이 감기기 시작했다. 탭은 들렸고, 대답은 이
	// 밤에 오지 않는다. 받아 두면 다 감긴 뒤의 낮 전환이 곧바로 끊는다.
	if (IGNightThree::IsDawnUnderway(World))
	{
		AnswerTapTimes.Reset();
		return;
	}

	bAnswerPending = true;
	if (UIGMissingFloorAudioSubsystem* AudioDirector =
		GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>())
	{
		AudioDirector->SetAuthoredSilence(true);
	}
	// 지정 침묵은 SCORE와 WORLD만 누른다. 라이저와 벽 앞의 물은 PUZZLE이라
	// 여기서 직접 걷어야 8초가 정말로 빈다.
	GetWorldTimerManager().ClearTimer(RiserResumeTimer);
	if (RiserFlow && RiserFlow->IsPlaying())
	{
		RiserFlow->FadeOut(1.2f, 0.0f);
	}
	for (UAudioComponent* Loop : {WallWaterVoice.Get(), TankSloshVoice.Get()})
	{
		if (Loop)
		{
			Loop->FadeOut(0.6f, 0.0f);
		}
	}
	// 안전망의 배관 울음도 이 벽 바로 뒤 공용 입상관에서 운다(PUZZLE). 울던
	// 중이면 같이 걷는다. 정적 동안에는 안전망이 새로 울지 않는다.
	for (TActorIterator<AIGMissingFloorMercyDirector> It(GetWorld()); It; ++It)
	{
		It->FadeOutPipeCry(0.6f);
		break;
	}
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		if (AIGPlayerCharacter* Pawn = Cast<AIGPlayerCharacter>(Controller->GetPawn()))
		{
			if (UIGStressComponent* Stress = Pawn->GetStress())
			{
				Stress->SuppressHeartbeat(
					IGNightThree::AnswerDelaySeconds + 1.65f,
					true);
			}
		}
	}
	if (Evidence)
	{
		// No repeat while the silence holds — the wait is the scene.
		Evidence->SetInteractionEnabled(false);
	}

	GetWorldTimerManager().SetTimer(
		AnswerTimer,
		this,
		&AIGMissingFloorNightThreeDirector::DeliverWallAnswer,
		IGNightThree::AnswerDelaySeconds,
		false);
}

FVector AIGMissingFloorNightThreeDirector::GetWallAnswerLocation()
{
	return FVector(278.0f, IGNightThree::WallBayYs[IGNightThree::CavityBayIndex], 1290.0f);
}

void AIGMissingFloorNightThreeDirector::PlayWallAnswerKnocks(AActor* Owner, FTimerHandle* OutTimers)
{
	if (!Owner || !Owner->GetWorld())
	{
		return;
	}
	// From inside the studs: the same rhythm, muffled by gypsum.
	const FVector InsideWall = GetWallAnswerLocation();
	if (IGAudio::Sample(TEXT("Knock_Plaster_0")))
	{
		// 사흘 밤 복도에서 들은 그 손이다(AnswerHitStartSeconds 주석). 셋째는
		// 내려놓는 한 번이라 조금 여리다.
		TWeakObjectPtr<AActor> WeakOwner(Owner);
		const auto Hit = [WeakOwner, InsideWall](const int32 HitIndex)
		{
			AActor* Source = WeakOwner.Get();
			if (!Source)
			{
				return;
			}
			if (UAudioComponent* Knock = IGAudio::SpawnOneShotAt(
				Source,
				IGAudio::SampleVariant(
					TEXT("Knock_Plaster"),
					3,
					static_cast<uint32>(HitIndex) << 12),
				InsideWall,
				IGNightThree::AnswerHitVolumes[HitIndex],
				IGNightThree::AnswerHitPitch,
				140.0f,
				1200.0f,
				EIGAudioBus::Entity))
			{
				// 석고 두 겹과 스터드 너머다. 가림 판정이 벽에 걸리든 말든
				// 그의 먹먹한 노크(700 Hz)만큼은 먹는다.
				Knock->SetLowPassFilterEnabled(true);
				Knock->SetLowPassFilterFrequency(IGNightThree::AnswerHitLowPassHz);
			}
		};
		Hit(0);
		for (int32 HitIndex = 1; HitIndex < 3; ++HitIndex)
		{
			FTimerHandle LocalTimer;
			Owner->GetWorldTimerManager().SetTimer(
				OutTimers ? OutTimers[HitIndex - 1] : LocalTimer,
				FTimerDelegate::CreateWeakLambda(Owner, [Hit, HitIndex]()
				{
					Hit(HitIndex);
				}),
				IGNightThree::AnswerHitStartSeconds[HitIndex],
				false);
		}
	}
	else
	{
		IGAudio::SpawnOneShotAt(
			Owner,
			UIGToneSequenceSoundWave::CreateAnswerKnockPattern(Owner, 1.0f),
			InsideWall,
			0.85f,
			0.92f,
			140.0f,
			1200.0f,
			EIGAudioBus::Entity);
	}
}

void AIGMissingFloorNightThreeDirector::DeliverWallAnswer()
{
	bAnswerPending = false;
	bAnswerDelivered = true;
	PlayWallAnswerKnocks(this, AnswerHitTimers);

	// T9와 P4는 첫 노크와 함께 확정한다. 여기서 미루면 그 사이에 05:30이 왔을
	// 때 대답은 들었는데 T9가 없는 밤이 된다. 늦추는 것은 들리는 것과 글뿐이다.
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::WaitingForAnAnswer,
			EIGMissingFloorSource::AnswerReturned);
		Narrative->MarkPuzzleSolved(IGNightThree::PuzzleFourId);
	}
	GetWorldTimerManager().SetTimer(
		AnswerSilenceReleaseTimer,
		this,
		&AIGMissingFloorNightThreeDirector::EndAnswerSilence,
		1.65f,
		false);
	GetWorldTimerManager().SetTimer(
		AnswerAftermathTimer,
		this,
		&AIGMissingFloorNightThreeDirector::AnswerAftermath,
		IGNightThree::AnswerAftermathSeconds,
		false);
	GetWorldTimerManager().SetTimer(
		RiserResumeTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			ResumeRiserFlow(IGNightThree::RiserResumeFadeInSeconds);
		}),
		IGNightThree::RiserResumeDelaySeconds,
		false);
}

void AIGMissingFloorNightThreeDirector::ResumeRiserFlow(const float FadeInSeconds)
{
	if (!RiserFlow || !bValveOpen || bAnswerPending)
	{
		return;
	}
	// 대답 직전에 걸어 둔 1.2초 페이드가 아직 도는 중이면 끝나는 순간 멎는다.
	// 그것도 멎은 것으로 본다.
	const bool bFadingOut =
		RiserFlow->GetPlayState() == EAudioComponentPlayState::FadingOut;
	if (RiserFlow->IsPlaying() && !bFadingOut)
	{
		return;
	}
	if (bFadingOut)
	{
		RiserFlow->Stop();
	}
	// 한 번 멎은 절차 파형은 새로 만들어 건다. 배수 0.5는 그대로 두고
	// 페이더만 1까지 올린다 — 페이더를 0.5로 두면 반의반이 된다.
	RiserFlow->SetSound(
		UIGToneSequenceSoundWave::CreatePipeWaterFlow(
			this,
			IGNightThree::RiserBedDistanceStep));
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->RegisterPersistentBed(RiserFlow, EIGAudioBus::Puzzle);
		}
	}
	RiserFlow->FadeIn(FadeInSeconds, 1.0f);
}

void AIGMissingFloorNightThreeDirector::EndAnswerSilence()
{
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			// 대답 뒤에는 공기가 천천히 돌아온다. 새벽이 거두는 길은 예전처럼 짧다
			// (SetHourActive가 이 플래그를 먼저 내린다).
			AudioDirector->SetAuthoredSilence(
				false,
				bHourCurrentlyActive ? IGNightThree::AnswerAirReturnSeconds : 0.45f);
		}
	}
}

void AIGMissingFloorNightThreeDirector::AnswerAftermath()
{
	// 3-6의 끝. 공기가 돌아오기 시작한 직후에 숨이 먼저 나가고 한 줄이 따라온다.
	// 조율음은 오디오 쪽이 정적이 걷힌 뒤로 미뤄 둔다.
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT("IGMissingFloor", "AnswerThought", "…대답했어."),
		3.2f);
	const UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	AIGPlayerCharacter* Pawn = Controller
		? Cast<AIGPlayerCharacter>(Controller->GetPawn())
		: nullptr;
	if (UIGStressComponent* Stress = Pawn ? Pawn->GetStress() : nullptr)
	{
		// 날숨은 스트레스가 높을 때만 난다. 내리기 전에 먼저 쉰다.
		Stress->PlayReliefExhale();
		// §20.1 밤3의 0.90 → 0.30. 공포가 무너지는 자리가 몸에 와야 한다.
		Stress->ApplyRelief(0.30f, 4.0f);
	}
}

void AIGMissingFloorNightThreeDirector::HandleNotebookRead(
	AIGReadableNote* Note,
	const bool bOpened)
{
	if (!bOpened)
	{
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	// 3-5. 여백의 ●● — ●를 처음 본 순간 유담이 알아본다. 출처를 적기 전에
	// 띄워야 뒤따르는 확정(T6, 벽에 생긴 새 동사)보다 이 줄이 먼저 온다. 수첩이
	// 펼쳐져 있는 동안 HUD는 대사를 붙들어 두었다가 덮으면 차례로 내보낸다.
	if (!Narrative->HasSource(
		EIGMissingFloorTruth::WaitingForAnAnswer,
		EIGMissingFloorSource::AnswerRhythmNotebook))
	{
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"NotebookRhythmRecognized",
				"아빠가 문 두드리던 박자잖아. 오빠가 내 방문에 하던 거."),
			4.4f);
	}
	// The mother lode: the cover names him, the schedule explains the dawn
	// noise, the margin teaches the criterion, and the doodle carries the
	// rhythm she grew up with.
	Narrative->RegisterTruthSource(
		EIGMissingFloorTruth::TenantIdentity,
		EIGMissingFloorSource::TunerNotebookName);
	Narrative->RegisterTruthSource(
		EIGMissingFloorTruth::NoiseWasHomecoming,
		EIGMissingFloorSource::TunerWorkSchedule);
	Narrative->RegisterTruthSource(
		EIGMissingFloorTruth::SomeoneInTheWall,
		EIGMissingFloorSource::PipeAuditionCriterion);
	Narrative->RegisterTruthSource(
		EIGMissingFloorTruth::WaitingForAnAnswer,
		EIGMissingFloorSource::AnswerRhythmNotebook);
	RefreshAnswerTargetAvailability();
}

void AIGMissingFloorNightThreeDirector::HandleLabelsRead(
	AIGReadableNote* Note,
	const bool bOpened)
{
	if (!bOpened)
	{
		return;
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::TenantIdentity,
			EIGMissingFloorSource::ShippingLabels);
		// 반송 소포의 송장과 라벨이 같은 방을 다르게 부른다. 어느 쪽이 맞는지는
		// 말하지 않고 둘을 나란히 놓기만 한다.
		if (Narrative->HasBeatPlayed(FName(TEXT("Arrival.Box.Parcel")))
			&& Narrative->MarkBeatPlayed(FName(TEXT("Day.LabelsAddress"))))
		{
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT(
					"IGMissingFloor",
					"LabelsAddressThought",
					"송장엔 501호, 라벨엔 옥탑이라고 돼 있네."),
				3.6f);
		}
	}
}

void AIGMissingFloorNightThreeDirector::HandleForumRead(
	AIGReadableNote* Note,
	const bool bOpened)
{
	if (!bOpened)
	{
		return;
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::NoiseWasHomecoming,
			EIGMissingFloorSource::NoiseForumPosts);
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::LandingStruggle,
			EIGMissingFloorSource::ForumFinalPost);
	}
}

void AIGMissingFloorNightThreeDirector::HandleJournalRead(
	AIGReadableNote* Note,
	const bool bOpened)
{
	if (!bOpened)
	{
		return;
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::FiveNightsOfThirst,
			EIGMissingFloorSource::KnockTallyJournal);
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::WaitingForAnAnswer,
			EIGMissingFloorSource::AnswerRhythmJournal);
		RefreshAnswerTargetAvailability();
	}
}

void AIGMissingFloorNightThreeDirector::HandleTruthConfirmed(
	const EIGMissingFloorTruth Truth)
{
	if (Truth == EIGMissingFloorTruth::SomeoneInTheWall)
	{
		// 벽을 먼저 견주고 수첩을 나중에 읽어도 P3는 풀린 것이다. 순서를 강제하지
		// 않는 것이 §7의 자유 경로다.
		if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
		{
			Narrative->MarkPuzzleSolved(IGNightThree::PuzzleThreeId);
		}
		RefreshAnswerTargetAvailability();
	}
	if (Truth == EIGMissingFloorTruth::WaitingForAnAnswer && !bSolvedAnnounced)
	{
		bSolvedAnnounced = true;
		OnSolved.Broadcast();
	}
}

// -- 비트 3-7 「귀환길」 ---------------------------------------------------

FVector AIGMissingFloorNightThreeDirector::GetReturnPassPoint() const
{
	return IGNightThree::ReturnPassPoint;
}

void AIGMissingFloorNightThreeDirector::ArmReturnPass()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	// 낮에는 세우지 않는다. 낮에 걸린 귀환길은 집에 닿아도 밤의 목표를
	// 채우지 못하고, 되풀이 밤의 재무장까지 막는다.
	if (!Narrative || Narrative->GetNightIndex() != 3 || !bHourCurrentlyActive)
	{
		return;
	}
	if (ReturnStage != EIGNightThreeReturnStage::Idle)
	{
		return;
	}
	ReturnStage = EIGNightThreeReturnStage::Passing;
	// 침대에서 무장되면(되풀이 밤, 이어 붙인 세션) 한 번 나갔다 와야 한다.
	// 누운 자리를 도착으로 세면 밤이 시작하자마자 끝난다.
	bMustLeaveHomeAgain = IsPlayerInsideUnit403();
	bWasWestOfHim = false;
	bPassedWhileWaiting = false;
	bReturnHintShown = false;
	bReliefPending = false;
	HomeDwellSeconds = 0.0f;
	StageReturnFigure();
	if (!bRepeatedPass)
	{
		// 벽의 대답과 같은 프레임이다. 그를 지금 복도에 세우되 그 자리의 첫
		// 노크는 삼킨다. 대답 셋에 아래층 노크 셋이 섞이면 여섯 번 두드린 것이
		// 된다. 다음 노크는 여운이 다 지나간 뒤에 아래에서 올라온다.
		if (AIGListenerEntity* Listener = Entity.Get())
		{
			Listener->SilenceNextStopKnock();
		}
	}
	// 목표가 바뀐 것을 한 줄로만 말한다. 무엇을 해야 하는지는 방금 배웠다.
	// 「…대답이다.」와 조율음을 밀어내지 않게 늦게 온다.
	const bool bRepeated = bRepeatedPass;
	GetWorldTimerManager().SetTimer(
		ReturnThoughtTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, bRepeated]()
		{
			if (ReturnStage != EIGNightThreeReturnStage::Passing)
			{
				return;
			}
			if (bRepeated)
			{
				AIGHorrorHUD::PushThought(
					this,
					NSLOCTEXT(
						"IGMissingFloor",
						"N3ReturnRepeatThought",
						"복도에서 또 두드리네. 이번엔 옆으로 지나서 돌아가자."),
					4.2f);
				return;
			}
			// 아직 위층에 있을 때만 맞는 말이다. 그새 잡혀서 침대로 돌아갔다면
			// 하지 않는다.
			const AIGPlayerCharacter* PlayerCharacter = Player.Get();
			if (PlayerCharacter
				&& PlayerCharacter->GetActorLocation().Z
					> IGNightThree::FourthFloorZ + 250.0f)
			{
				AIGHorrorHUD::PushThought(
					this,
					NSLOCTEXT("IGMissingFloor", "N3ReturnThought", "내려가자."),
					2.6f);
			}
		}),
		bRepeated ? 5.0f : IGNightThree::ReturnThoughtDelaySeconds,
		false);
	GetWorldTimerManager().SetTimer(
		ReturnTimer,
		this,
		&AIGMissingFloorNightThreeDirector::AdvanceReturn,
		IGNightThree::ReturnPollSeconds,
		true);
}

void AIGMissingFloorNightThreeDirector::RearmReturnPassForRepeatedNight()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!bHourCurrentlyActive
		|| !Narrative
		|| Narrative->GetNightIndex() != 3
		|| ReturnStage != EIGNightThreeReturnStage::Idle)
	{
		return;
	}
	// 그가 복도에 세워지는 순간을 그녀가 보면 안 된다. 403호 안에서만 세운다.
	// 밤은 침대에서 시작하므로 보통은 깨는 그 프레임에 세워지고, 문밖 복도에서
	// 노크가 난다. 이어 붙인 세션처럼 밖에 있으면 돌아올 때까지 기다린다.
	if (!IsPlayerInsideUnit403())
	{
		GetWorldTimerManager().SetTimer(
			RepeatPassTimer,
			this,
			&AIGMissingFloorNightThreeDirector::RearmReturnPassForRepeatedNight,
			1.0f,
			false);
		return;
	}
	bRepeatedPass = true;
	ArmReturnPass();
}

void AIGMissingFloorNightThreeDirector::StageReturnFigure()
{
	AIGListenerEntity* Listener = Entity.Get();
	if (!Listener || bFigureStaged)
	{
		return;
	}
	bFigureStaged = true;
	// 1-4의 계단 카메오와 같은 장치다. 두 점을 주어 복도를 가로질러 조금씩
	// 움직이게 하면, 세워 둔 프롭이 아니라 자리를 지키는 사람으로 읽힌다.
	Listener->SetPatrolPoints({
		IGNightThree::ReturnPassPoint,
		IGNightThree::ReturnShufflePoint,
	});
	// ParkForBeat, not TeleportTo: P4's own three taps just left him
	// investigating a spot up in the annex, and a teleported-but-still-reacting
	// pursuer crawls out of the corridor before she ever gets down the stairs.
	Listener->ParkForBeat(
		IGNightThree::ReturnPassPoint,
		IGNightThree::ReturnPassYaw);
}

void AIGMissingFloorNightThreeDirector::ReleaseReturnFigure()
{
	if (!bFigureStaged)
	{
		return;
	}
	bFigureStaged = false;
	if (AIGListenerEntity* Listener = Entity.Get())
	{
		Listener->SetPatrolPoints(CorridorPatrolPoints);
		// 연출이었지 실패가 아니다. 공격 티어는 건드리지 않는다.
		Listener->ResetToPatrolStart(/*bRaiseAggression=*/false);
	}
}

bool AIGMissingFloorNightThreeDirector::IsPlayerInsideUnit403() const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	return PlayerCharacter
		&& IGNightThree::Unit403Interior.IsInsideOrOn(
			PlayerCharacter->GetActorLocation());
}

void AIGMissingFloorNightThreeDirector::AdvanceReturn()
{
	if (ReturnStage != EIGNightThreeReturnStage::Passing)
	{
		GetWorldTimerManager().ClearTimer(ReturnTimer);
		return;
	}
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	const AIGListenerEntity* Listener = Entity.Get();
	if (!PlayerCharacter || !Listener)
	{
		return;
	}

	// 복도에서 그를 처음 마주친 순간의 한 줄. 대답으로 그를 세울 수 있다는
	// 것은 방금 벽에서 확인한 사실(T9)이지만, 폭 160cm 복도에서 그것이 떠오르지
	// 않으면 거듭 잡히다 새벽을 맞는다. 잡힌 뒤에는 더 짧게 짚는다.
	if (!bReturnHintShown
		&& !bPassedWhileWaiting
		&& Listener->GetListenerState() != EIGListenerState::Waiting
		&& IsReturnFigureInSight())
	{
		bReturnHintShown = true;
		AIGHorrorHUD::PushThought(
			this,
			bCaughtDuringReturn
				? NSLOCTEXT(
					"IGMissingFloor",
					"N3ReturnHintAgain",
					"벽에 했던 대로 해 보자.")
				: NSLOCTEXT(
					"IGMissingFloor",
					"N3ReturnHint",
					"저기 있다. 대답을 기다리는 건가?"),
			3.4f);
	}

	// 「그가 멈춰 기다리는 옆을 걸어 지나가는」. 서쪽에 있었다가 그가 기다리는
	// 동안 동쪽으로 넘어가면 그것이 이 비트다. 대답하지 않고 몰래 지나가는
	// 것도 정당한 해법이고 언제나 그랬다 — 다만 이 비트는 아니다.
	const float PlayerX = PlayerCharacter->GetActorLocation().X;
	const float HisX = Listener->GetActorLocation().X;
	if (PlayerX < HisX - IGNightThree::PassClearanceCentimeters)
	{
		bWasWestOfHim = true;
	}
	if (!bPassedWhileWaiting
		&& bWasWestOfHim
		&& PlayerX > HisX + IGNightThree::PassClearanceCentimeters
		&& Listener->GetListenerState() == EIGListenerState::Waiting)
	{
		bPassedWhileWaiting = true;
		if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
		{
			Narrative->MarkBeatPlayed(IGNightThree::PassByBeatId);
		}
		// 스치는 순간. 그는 들킨 것이 아니라 기다리는 중이라 경계 소리도,
		// 헐떡임도, 화면 흔들림도 없다. 굳은 몸이 옆에서 한 번 삐걱이고 그녀의
		// 심장이 한 박 멎는다. 사흘 동안 「들켰다」를 뜻하던 신호를 여기서 내면
		// 플레이어는 도망친다.
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreatePlasterSettle(this),
			Listener->GetActorLocation(),
			0.3f,
			0.9f,
			120.0f,
			900.0f,
			EIGAudioBus::Entity);
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->SuppressHeartbeat(2.5f, true);
		}
		bReliefPending = true;
		// 회피 대상이 애도 대상으로. 이 게임에서 가장 조용한 한 줄이어야 하므로
		// 설명하지 않는다 — 그가 무엇을 하고 있는지만 말한다.
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"N3PassByThought",
				"안 움직인다. 기다리는 건가."),
			4.0f);
	}
	// 몇 걸음 더 멀어지고서야 참았던 숨이 나간다.
	if (bReliefPending
		&& PlayerX > HisX + IGNightThree::PassReliefCentimeters)
	{
		bReliefPending = false;
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->PlayReliefExhale();
		}
	}

	if (bMustLeaveHomeAgain)
	{
		if (!IsPlayerInsideUnit403())
		{
			bMustLeaveHomeAgain = false;
		}
		HomeDwellSeconds = 0.0f;
		return;
	}
	// 문턱을 밟는 순간 끝내지 않는다. 방 안으로 들어와 잠시 머물러야 밤이
	// 닫힌다 — 문 너머의 그를 돌아볼 틈이 있어야 한다.
	if (IGNightThree::Unit403Arrival.IsInsideOrOn(PlayerCharacter->GetActorLocation()))
	{
		HomeDwellSeconds += IGNightThree::ReturnPollSeconds;
	}
	else
	{
		HomeDwellSeconds = 0.0f;
	}
	if (HomeDwellSeconds > 0.0f
		&& (HomeDwellSeconds >= IGNightThree::HomeArrivalDwellSeconds
			|| IGNightThree::IsSynchronousHarness()))
	{
		ReturnStage = EIGNightThreeReturnStage::Home;
		GetWorldTimerManager().ClearTimer(ReturnTimer);
		// 그를 복도에서 거두는 것은 새벽이다(SetHourActive). 여기서 거두면 문
		// 너머로 돌아보던 그녀 눈앞에서 그가 사라진다.
		OnReturnedHome.Broadcast();
	}
}

bool AIGMissingFloorNightThreeDirector::IsReturnFigureInSight() const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	const AIGListenerEntity* Listener = Entity.Get();
	const UWorld* World = GetWorld();
	if (!PlayerCharacter || !Listener || !World)
	{
		return false;
	}
	const FVector Eye = PlayerCharacter->GetPawnViewLocation();
	const FVector Chest = Listener->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f);
	if (FVector::DistSquared(Eye, Chest)
		> FMath::Square(IGNightThree::ReturnHintRangeCentimeters))
	{
		return false;
	}
	// 등 뒤나 시야 가장자리에 있는 그를 두고 「저기 있다」고 하지 않는다.
	if (FVector::DotProduct(
		PlayerCharacter->GetViewRotation().Vector(),
		(Chest - Eye).GetSafeNormal()) < 0.5f)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGNightThreeReturnHint), false, this);
	Params.AddIgnoredActor(PlayerCharacter);
	Params.AddIgnoredActor(Listener);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, Eye, Chest, ECC_Visibility, Params);
}

void AIGMissingFloorNightThreeDirector::NotifyCaptureReset()
{
	if (ReturnStage == EIGNightThreeReturnStage::Passing)
	{
		// 리셋은 침대로 되돌린다. 그것을 도착으로 세면 잡히는 것이 목표 달성이
		// 되므로, 한 번 밖으로 나갔다 와야 한다. 형체는 다시 복도에 세운다.
		bMustLeaveHomeAgain = true;
		HomeDwellSeconds = 0.0f;
		bReliefPending = false;
		// 잡힌 뒤에 그를 다시 보면 한 번 더, 더 짧게 짚는다.
		bCaughtDuringReturn = true;
		bReturnHintShown = false;
		bFigureStaged = false;
		StageReturnFigure();
	}
}

void AIGMissingFloorNightThreeDirector::RefreshAnswerTargetAvailability()
{
	if (!AnswerTarget || bAnswerDelivered)
	{
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	int32 RhythmClueCount = 0;
	for (const EIGMissingFloorSource Clue : {
		EIGMissingFloorSource::AnswerRhythmVoicemail,
		EIGMissingFloorSource::AnswerRhythmNotebook,
		EIGMissingFloorSource::AnswerRhythmJournal})
	{
		RhythmClueCount += Narrative
			&& Narrative->HasSource(EIGMissingFloorTruth::WaitingForAnAnswer, Clue)
				? 1
				: 0;
	}
	if (Narrative && RhythmClueCount >= 2)
	{
		// Store the derived compatibility record used by the existing T9 rule.
		// Individual clue records remain in the save for fairness audits.
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::WaitingForAnAnswer,
			EIGMissingFloorSource::AnswerRhythmMaterials);
	}
	// 낮에는 서지 않는다. 낮에 받은 대답은 T9만 남기고 돌아갈 귀환길이 없다.
	const bool bReady =
		bHourCurrentlyActive
		&& Narrative
		&& Narrative->HasTruth(EIGMissingFloorTruth::SomeoneInTheWall)
		&& RhythmClueCount >= 2;
	AnswerTarget->SetActorHiddenInGame(!bReady);
	AnswerTarget->SetInteractionEnabled(bReady);
	if (bReady && !bAnswerTargetAnnounced && bHourCurrentlyActive)
	{
		// 새 동사가 생겼다는 것은 알려야 한다. 같은 벽에 「두드린다」가 둘이면
		// 지금이 리듬인지 잔향 시험인지 알 길이 없다. 다만 박자와 할 일은 적지
		// 않는다(§7 P4). 수첩에서 이미 알아본 노크를 오빠의 음성메시지가 불러낸다.
		bAnswerTargetAnnounced = true;
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"AnswerTargetReady",
				"“문 두드리면 알지?”"),
			3.4f);
	}
}

void AIGMissingFloorNightThreeDirector::RefreshJournalAvailability(
	const bool bHourActive)
{
	if (!JournalNote)
	{
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const bool bEarned =
		!bHourActive
		&& Narrative
		&& Narrative->HasTruth(EIGMissingFloorTruth::WasStillAlive);
	if (!bEarned)
	{
		GetWorldTimerManager().ClearTimer(JournalHandoverTimer);
		GetWorldTimerManager().ClearTimer(JournalHandoverSoundTimer);
		SetJournalShown(false);
		return;
	}
	// 그냥 나타나는 물건은 없다. 처음 받는 아침에는 황순금이 문을 열고 달력
	// 뒷장을 걸고 닫는다. 새벽 암전 아래서 걸면 아무도 듣지 못하고, 잠금 소리와
	// 아침 독백에 묻힌다. 한 번 받은 뒤로는 낮마다 그 자리에 걸려 있다.
	// 프로브와 캡처는 새벽 프레임에 바로 읽는다.
	if (Narrative->HasBeatPlayed(IGNightThree::JournalHandedOverBeatId)
		|| IGNightThree::IsSynchronousHarness())
	{
		GetWorldTimerManager().ClearTimer(JournalHandoverTimer);
		Narrative->MarkBeatPlayed(IGNightThree::JournalHandedOverBeatId);
		SetJournalShown(true);
		return;
	}
	SetJournalShown(false);
	if (!GetWorldTimerManager().IsTimerActive(JournalHandoverTimer))
	{
		GetWorldTimerManager().SetTimer(
			JournalHandoverTimer,
			this,
			&AIGMissingFloorNightThreeDirector::TickJournalHandover,
			IGNightThree::JournalHandoverDelaySeconds,
			false);
	}
}

void AIGMissingFloorNightThreeDirector::SetJournalShown(const bool bShown)
{
	if (!JournalNote)
	{
		return;
	}
	JournalNote->SetActorHiddenInGame(!bShown);
	JournalNote->SetActorEnableCollision(bShown);
	JournalNote->SetInteractionEnabled(bShown);
}

void AIGMissingFloorNightThreeDirector::TickJournalHandover()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (bHourCurrentlyActive
		|| !JournalNote
		|| !Narrative
		|| !Narrative->HasTruth(EIGMissingFloorTruth::WasStillAlive))
	{
		return;
	}
	if (Narrative->HasBeatPlayed(IGNightThree::JournalHandedOverBeatId))
	{
		SetJournalShown(true);
		return;
	}
	// 앞선 대사(신고 문자, 녹음을 들은 결론)와 펼친 종이, 손에 든 녹음이 다
	// 비기를 기다린다. 테이프를 듣는 사이 옆집 문소리가 끼면 녹음 속 소리로
	// 들린다. 그녀가 그 문을 보고 있으면 종이가 생겨나는 것을 보게 되니 눈을
	// 돌릴 때까지 둔다.
	UWorld* World = GetWorld();
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = Controller ? Cast<AIGHorrorHUD>(Controller->GetHUD()) : nullptr;
	bool bTapePlaying = false;
	if (World)
	{
		for (TActorIterator<AIGMissingFloorPuzzleTwoDirector> It(World); It; ++It)
		{
			bTapePlaying = bTapePlaying || It->IsPhonePlayingBack();
		}
	}
	if ((Hud && !Hud->IsDialogueLaneIdle())
		|| bTapePlaying
		|| AIGReadableNote::GetOpenNote() != nullptr
		|| IsJournalDoorInView())
	{
		GetWorldTimerManager().SetTimer(
			JournalHandoverTimer,
			this,
			&AIGMissingFloorNightThreeDirector::TickJournalHandover,
			IGNightThree::JournalHandoverPollSeconds,
			false);
		return;
	}
	Narrative->MarkBeatPlayed(IGNightThree::JournalHandedOverBeatId);
	SetJournalShown(true);
	PlayJournalHandoverSound();
}

bool AIGMissingFloorNightThreeDirector::IsJournalDoorInView() const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!PlayerCharacter || IsPlayerInsideUnit403())
	{
		return false;
	}
	const FVector Eye = PlayerCharacter->GetPawnViewLocation();
	const FVector ToDoor = IGNightThree::JournalLocation - Eye;
	// 4층 복도에 서서 문 쪽을 보고 있을 때만이다. 다른 층이나 방 안에서는 안 보인다.
	if (FMath::Abs(ToDoor.Z) > 200.0f
		|| ToDoor.SizeSquared2D() > FMath::Square(1600.0f))
	{
		return false;
	}
	return FVector::DotProduct(
		PlayerCharacter->GetViewRotation().Vector(),
		ToDoor.GetSafeNormal()) > 0.4f;
}

void AIGMissingFloorNightThreeDirector::PlayJournalHandoverSound()
{
	// 다른 층에서는 들리지 않는다. 돌아와서 문에 걸린 것을 보게 된다.
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!PlayerCharacter
		|| FMath::Abs(
			PlayerCharacter->GetActorLocation().Z
			- IGNightThree::Unit401DoorSoundLocation.Z) > 250.0f)
	{
		return;
	}
	// 403호 안에서는 문과 벽 너머로 듣는다. 가림 판정이 알아서 먹먹하게 한다.
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Door_Steel_Open"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateDoorCreak(this); }),
		IGNightThree::Unit401DoorSoundLocation,
		0.42f,
		0.94f,
		90.0f,
		1200.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "JournalHandoverCaption", "401호 문 여닫는 소리"),
		2.8f,
		IGNightThree::Unit401DoorSoundLocation);
	// 문에 종이를 거는 손, 그리고 다시 닫히는 문.
	GetWorldTimerManager().SetTimer(
		JournalHandoverSoundTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreatePickupRustle(this),
				IGNightThree::JournalLocation,
				0.34f,
				1.0f,
				60.0f,
				900.0f,
				EIGAudioBus::World);
			GetWorldTimerManager().SetTimer(
				JournalHandoverSoundTimer,
				FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					IGAudio::SpawnOneShotAt(
						this,
						IGAudio::SampleOr(
							TEXT("Door_Steel_Close"),
							[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateDoorThud(this); }),
						IGNightThree::Unit401DoorSoundLocation,
						0.4f,
						0.96f,
						90.0f,
						1200.0f,
						EIGAudioBus::World);
				}),
				0.8f,
				false);
		}),
		0.75f,
		false);
}

void AIGMissingFloorNightThreeDirector::RefreshDistantSeoVisibility()
{
	if (!DistantSeo)
	{
		return;
	}
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	// 밤2를 넘긴 낮, 대답을 듣기 전까지. 한 번 알아본 뒤로는 다시 서지 않는다.
	// 새벽 암전 속 403호에서 울리던 발소리는 없다. 그가 서는 것도, 발소리도
	// 그녀가 편의점에 들른 뒤의 일이다(§8 「편의점을 나서는 길」).
	const bool bEligible =
		!bHourCurrentlyActive
		&& Narrative
		&& Narrative->IsPuzzleSolved(FName(TEXT("P2")))
		&& !Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer)
		&& !Narrative->HasBeatPlayed(IGNightThree::DistantSeoBeatId)
		&& !IGNightThree::IsSynchronousHarness();
	if (!bEligible)
	{
		GetWorldTimerManager().ClearTimer(SeoWatchTimer);
		SeoStage = EIGDistantSeoStage::Idle;
		DistantSeo->SetVisibility(false, true);
		DistantSeo->SetWorldLocation(IGNightThree::DistantSeoLocation);
		return;
	}
	if (SeoStage == EIGDistantSeoStage::Idle)
	{
		HideDistantSeo(/*bSeenOnce=*/false);
	}
}

void AIGMissingFloorNightThreeDirector::TickDistantSeo()
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	UWorld* World = GetWorld();
	if (!DistantSeo || !PlayerCharacter || !World || bHourCurrentlyActive)
	{
		return;
	}
	const FVector Eye = PlayerCharacter->GetPawnViewLocation();
	const FVector Feet = DistantSeo->GetComponentLocation();
	const FVector Chest = Feet + FVector(0.0f, 0.0f, 30.0f);
	const float Facing = FVector::DotProduct(
		PlayerCharacter->GetViewRotation().Vector(),
		(Chest - Eye).GetSafeNormal());
	const float Distance = FVector::Dist2D(Eye, Feet);
	// 편의점 안은 X 2405..3120, Y -830..-40이다. 골목 북쪽 벽 너머(빌라)나
	// 1층보다 높은 곳에서는 골목이 보이지 않는다.
	const bool bInStore = Eye.X > 2410.0f
		&& Eye.Y > -830.0f
		&& Eye.Y < -40.0f
		&& Eye.Z < 300.0f;
	const bool bIndoors = !bInStore && (Eye.Z > 300.0f || Eye.Y > -385.0f);
	// 판은 늘 그녀 쪽을 본다. 옆에서 보면 선 하나가 된다. 롤 90이면 판의
	// 앞면(평면의 +Z)이 +Y로 서므로 그녀 쪽 방위각에서 90도를 뺀다.
	const auto FaceViewer = [this, &Eye]()
	{
		const FVector From = DistantSeo->GetComponentLocation();
		const float Yaw = FMath::RadiansToDegrees(
			FMath::Atan2(Eye.Y - From.Y, Eye.X - From.X)) - 90.0f;
		DistantSeo->SetWorldRotation(FRotator(0.0f, Yaw, 90.0f));
	};
	const auto StepAt = [this](const FVector& Where, const float Volume)
	{
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateSurfaceFootstep(
				this, EIGFootstepSurface::Concrete, 0.9f, 0.7f),
			Where,
			Volume,
			1.0f,
			300.0f,
			2600.0f,
			EIGAudioBus::World);
	};

	if (SeoStage == EIGDistantSeoStage::Armed)
	{
		// 계산대 쪽을 보는 사이에 선다. 유리 너머로 생겨나는 순간을 보이면 안 된다.
		if (bInStore && Facing < IGNightThree::SeoArmFacingDot)
		{
			SeoStage = EIGDistantSeoStage::Shown;
			bSeoAnnounced = false;
			DistantSeo->SetWorldLocation(IGNightThree::DistantSeoLocation);
			FaceViewer();
			DistantSeo->SetVisibility(true, true);
		}
		return;
	}
	FaceViewer();

	if (SeoStage == EIGDistantSeoStage::Retreating)
	{
		const float Step = IGNightThree::SeoRetreatPollSeconds;
		SeoRetreatSeconds += Step;
		const FVector Next = FMath::VInterpConstantTo(
			Feet,
			SeoRetreatTarget,
			Step,
			IGNightThree::SeoRetreatSpeed);
		DistantSeo->SetWorldLocation(Next);
		// 비켜서는 두 걸음째. 첫 걸음은 돌아서는 순간에 났다.
		if (SeoRetreatSeconds - Step < 0.45f && SeoRetreatSeconds >= 0.45f)
		{
			StepAt(Next, 0.3f);
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(IGNightThreeDistantSeo), false, this);
		Params.AddIgnoredActor(PlayerCharacter);
		FHitResult Hit;
		const bool bHiddenByWall = World->LineTraceSingleByChannel(
			Hit,
			Eye,
			Next + FVector(0.0f, 0.0f, 30.0f),
			ECC_Visibility,
			Params);
		if (Next.Equals(SeoRetreatTarget, 1.0f)
			|| (SeoRetreatSeconds > 0.3f && bHiddenByWall))
		{
			HideDistantSeo(/*bSeenOnce=*/true);
		}
		return;
	}

	if (bIndoors)
	{
		HideDistantSeo(SeoStage == EIGDistantSeoStage::Seen);
		return;
	}
	// 편의점 문을 나서면 발소리 하나가 눈을 끈다.
	if (!bSeoAnnounced && Eye.X < 2400.0f)
	{
		bSeoAnnounced = true;
		StepAt(Feet, 0.55f);
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "DistantSeoCaption", "골목 건너편 발소리"),
			2.0f,
			Feet);
	}
	const bool bOnScreen = Facing >= IGNightThree::SeoOffscreenDot;
	// 보면서 다가오면 판이 판으로 보이기 전에 그녀에게서 멀어지는 쪽으로 비켜선다.
	if (bOnScreen && Distance < IGNightThree::SeoRetreatRange)
	{
		SeoStage = EIGDistantSeoStage::Retreating;
		SeoRetreatSeconds = 0.0f;
		SeoRetreatTarget = Eye.Y < IGNightThree::SeoFacadeLineY
			? IGNightThree::DistantSeoStreetRetreatLocation
			: IGNightThree::DistantSeoRetreatLocation;
		StepAt(Feet, 0.34f);
		GetWorldTimerManager().SetTimer(
			SeoWatchTimer,
			this,
			&AIGMissingFloorNightThreeDirector::TickDistantSeo,
			IGNightThree::SeoRetreatPollSeconds,
			true);
		return;
	}
	// 눈을 돌린 채 다가오면 이미 없다.
	if (!bOnScreen && Distance < IGNightThree::SeoHideRange)
	{
		HideDistantSeo(SeoStage == EIGDistantSeoStage::Seen);
		return;
	}
	if (SeoStage == EIGDistantSeoStage::Shown)
	{
		if (Facing >= IGNightThree::SeoSeenFacingDot)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(IGNightThreeDistantSeo), false, this);
			Params.AddIgnoredActor(PlayerCharacter);
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Eye, Chest, ECC_Visibility, Params))
			{
				SeoSeenSeconds += IGNightThree::SeoPollSeconds;
			}
		}
		if (SeoSeenSeconds >= IGNightThree::SeoRecognizeSeconds)
		{
			SeoStage = EIGDistantSeoStage::Seen;
			SeoAwaySeconds = 0.0f;
		}
		return;
	}
	// 한 번 똑바로 봤다. 시선을 돌렸다 다시 보면 없다.
	SeoAwaySeconds = bOnScreen ? 0.0f : SeoAwaySeconds + IGNightThree::SeoPollSeconds;
	if (SeoAwaySeconds >= IGNightThree::SeoGoneAwaySeconds)
	{
		HideDistantSeo(/*bSeenOnce=*/true);
	}
}

void AIGMissingFloorNightThreeDirector::HideDistantSeo(const bool bSeenOnce)
{
	if (!DistantSeo)
	{
		return;
	}
	// 사라질 때는 소리도 자막도 독백도 없다. 다시 볼 때 없을 뿐이다.
	DistantSeo->SetVisibility(false, true);
	DistantSeo->SetWorldLocation(IGNightThree::DistantSeoLocation);
	SeoSeenSeconds = 0.0f;
	SeoAwaySeconds = 0.0f;
	SeoRetreatSeconds = 0.0f;
	if (bSeenOnce)
	{
		GetWorldTimerManager().ClearTimer(SeoWatchTimer);
		SeoStage = EIGDistantSeoStage::Idle;
		if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
		{
			Narrative->MarkBeatPlayed(IGNightThree::DistantSeoBeatId);
		}
		return;
	}
	// 알아보지 못했으면 다음에 편의점에 들를 때 다시 선다.
	SeoStage = EIGDistantSeoStage::Armed;
	GetWorldTimerManager().SetTimer(
		SeoWatchTimer,
		this,
		&AIGMissingFloorNightThreeDirector::TickDistantSeo,
		IGNightThree::SeoPollSeconds,
		true);
}

UIGMissingFloorNarrativeSubsystem*
AIGMissingFloorNightThreeDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
}

bool AIGMissingFloorNightThreeDirector::ValidateFixtures() const
{
	float RouteLengthCentimeters = 0.0f;
	int32 UpperStepCount = 0;
	const bool bPhysicalRouteValid =
		Scene.IsValid()
		&& Scene->ValidateMissingFloorRooftopRoute(
			RouteLengthCentimeters,
			UpperStepCount);
	return StairGate != nullptr
		&& AnnexGate != nullptr
		&& bPhysicalRouteValid
		&& FMath::IsNearlyEqual(RouteLengthCentimeters, 640.0f, 0.1f)
		&& UpperStepCount == 14
		&& Keyring != nullptr
		&& TunerNotebook != nullptr
		&& TuningHammer != nullptr
		&& RiserValve != nullptr
		&& WallListens.Num() == 3
		&& WallKnocks.Num() == 3
		&& ImpactMark != nullptr
		&& AnswerTarget != nullptr
		&& PlasterDatings.Num() == 3
		&& TankAudition != nullptr
		&& LabelsNote != nullptr
		&& ForumNote != nullptr
		&& JournalNote != nullptr;
}
