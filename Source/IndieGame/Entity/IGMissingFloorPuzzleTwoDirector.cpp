#include "Entity/IGMissingFloorPuzzleTwoDirector.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorNightFourDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Environment/IGCctvChannelFive.h"
#include "Interaction/IGReadableNote.h"
#include "Interaction/IGSwingDoor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGRecordingSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"

namespace IGPuzzleTwo
{
	// Booth-interior coordinates, ground floor (Z 0). Matched to the booth
	// BuildLobby erects behind the connector's north wall; duplicated by the
	// same rule every night director follows, because the scene's coordinate
	// namespace is .cpp-local.
	const FVector BoothDoorHinge(112.0f, -235.0f, 0.0f);
	// Desk top is Z=76.  The 2.8 cm ledger rests at Z=77.5, never at the old
	// upright-paper centre that made it intersect and appear to float.
	const FVector FairCopyLocation(120.0f, -103.0f, 77.5f);
	// 접수철의 얇은 받침이 상판(Z=76)에 닿는다.
	const FVector CarbonLocation(179.0f, -103.0f, 76.35f);
	// 책상에 놓인 종이다. 세워 놓고 중심을 Z=80에 두면 24cm 높이의 절반이
	// 상판(Z=76) 아래로 들어가 책상에 박힌다 — 위 두 장에서 이미 한 번 고친
	// 실수다. 눕히고 상판 위에 올린다.
	const FVector AgentNoteLocation(205.0f, -103.0f, 76.08f);
	// 별도 상자 대신 모니터 하단 INPUT 버튼을 누른다.
	const FVector CctvLocation(134.8f, -105.6f, 91.3f);
	const FVector FoamGapLocation(272.0f, -90.0f, 105.0f);
	/**
	 * 자재 반입 영수증 두 장. 관리실 상판(X 105..215, Y -137.5..-82.5, Z=76)
	 * 앞쪽 오른편의 빈자리다 — 대리인 쪽지(X 196..214, Y -115..-91)와는
	 * Y로 갈라진다. 접수철은 X 168..190, Y -118..-88에 놓인다.
	 * 눕혀서 얹는다. 세우면 절반이 상판 아래로 들어간다.
	 */
	const FVector BoardReceiptsLocation(196.0f, -126.0f, 76.08f);
	/**
	 * §22.3 관리실 달력. 북벽 안쪽 면이 Y=-85이다. 동쪽은 벽이 아니라
	 * 안쪽 방 문짝(X 205..275)이라 처음에 거기 걸었다가 문을 뚫었다.
	 * 책상(X 105..215) 서쪽, 서벽(X 45..60)과도 떨어진 빈 벽에 건다.
	 */
	const FVector WallCalendarLocation(82.0f, -85.12f, 150.0f);
	// 녹화기 앞면의 표시등을 확인하는 범위다. 모델과 같은 위치를 쓴다.
	const FVector RecorderBayLocation(150.0f, -111.5f, 78.5f);
	/**
	 * 1층 배관 밸브. 펌프 토출관(X 75, Y -170) 앞, 책상 서쪽. 열면 관에 물이
	 * 흐르고 그 소리가 책상을 덮는다 — 밸브라는 동사를 여기서 처음 배운다.
	 * 밤3의 5층 밸브와 밤4의 세 손잡이가 같은 손이다(§7 P2, 2026-09-10).
	 */
	const FVector BoothValveLocation(75.0f, -184.2f, 52.0f);
	constexpr float BoothValveHoldSeconds = 0.8f;
	constexpr float BoothValveLoudness = 0.55f;
	/** 책상 둘레의 마스킹. 문지르기 0.25가 이 안에서 삼켜진다. */
	const FVector BoothMaskCenter(160.0f, -108.0f, 80.0f);
	constexpr float BoothMaskRadius = 420.0f;
	constexpr float BoothMaskAmount = 0.30f;
	/**
	 * §22.3 안쪽 방 청음. 문짝은 X 205..275, Y -87..-82이고 문틈 판정은
	 * X 270..274다. 그 문 앞에 서는 자리로, 문짝에서 1 cm 떨어뜨린다.
	 */
	const FVector InnerRoomListenLocation(248.0f, -93.0f, 140.0f);
	/**
	 * 기계 험 뒤에 사람이 머무는 흔적이 온다. 간이침대 나무가 한 번 삐걱이고,
	 * 몸을 뒤척이는 천 소리가 난다. 목소리도 숨도 없다. 독백은 다 들은 뒤다.
	 */
	constexpr float InnerRoomCreakSeconds = 1.30f;
	constexpr float InnerRoomClothSeconds = 2.60f;
	constexpr float InnerRoomThoughtSeconds = 3.0f;

	/**
	 * §13 「물탱크 바람 소리예요」의 쪽지. 관리실 남벽의 연결통로 쪽 면(Y -245)
	 * 이고, 문 개구부(X 110..220) 서쪽이다. 동쪽 벽에는 발신기가 붙어 있다.
	 * 계단에서 내려오는 사람이 문보다 먼저 본다.
	 */
	const FVector BoothNoticeLocation(62.0f, -245.12f, 150.0f);

	/** §5.1: frottage is a sustained 0.25 — three times, on purpose. */
	constexpr float FrottageLoudness = 0.25f;
	constexpr float FrottageHoldSeconds = 1.2f;

	/**
	 * §5.5. 비트 2-1에서 유담이 폰 녹음을 켜고 403호 현관문에 대어 둔다. 문
	 * 안쪽 바닥이므로 복도의 그가 아니라 그녀가 종일 지나는 자리다.
	 *
	 * 4층 세계 좌표다. 관리실 프롭들은 1층이라 Z가 작지만 이 폰만은 403호
	 * 안이므로 슬래브 높이를 더해야 한다 — 처음 넣을 때 그 900을 빠뜨려서
	 * 폰이 관리실 바닥에 놓여 있었다. 현관문은 (101, -225)에 있고 문 안쪽은
	 * Y가 0에 가까운 쪽이다.
	 */
	constexpr float FourthFloorZ = AIGPrologueWorldScene::FourthFloorZ;
	const FVector PhoneAtDoorLocation(150.0f, -196.0f, FourthFloorZ + 4.0f);
	/** 폰을 놓는 것은 소리를 내는 행동이다. 발소리보다 조용하지만 0은 아니다. */
	constexpr float PhonePlacementLoudness = 0.08f;
	/** 재생의 결론은 공백이 끝나고 숨 한 번 뒤에 온다. */
	constexpr float PhoneThoughtAfterGapSeconds = 1.2f;
	/** 비트 2-6 【S】0.4 — 한기. 0.4부터는 들숨이 따라와 놀람이 되므로 그 아래. */
	constexpr float PhoneSilenceScare = 0.35f;
	/** 다 듣고 폰을 탁자에 내려놓기까지. */
	constexpr float PhoneReturnAfterPlaybackSeconds = 1.5f;
	/**
	 * 새벽 뒤 폰이 떠는 시각. 눈을 뜨고 「문이 열린다. 아침이다.」가 지나간
	 * 다음이다. 같은 층, 9미터 안에서만 들린다 — 멀리서 울리는 폰은 부름이
	 * 아니라 소음이다.
	 */
	constexpr float PhoneMorningBuzzSeconds = 6.0f;
	constexpr float PhoneMorningBuzzReach = 900.0f;
	constexpr float PhoneMorningBuzzFloorSpan = 200.0f;
	/** 12초짜리 진동 녹음을 한 번 떨림으로 자른다. */
	constexpr float PhoneMorningBuzzCutSeconds = 0.9f;

	/**
	 * §13 밤2 회수. 자재 반입 영수증 옆, 접수철 모서리 앞에 놓인 현금 메모.
	 * 낮에 본 매물(8/12, 30,000원, 닉네임 달빛)이 관리인 책상에 돈으로 적혀 있다.
	 */
	const FVector CashMemoLocation = BoardReceiptsLocation + FVector(-24.0f, 2.0f, 0.0f);
	const FName CashMemoBeatId(TEXT("Night2.CashMemo"));

	const FName CctvBeatId(TEXT("Night2.CCTV"));
	const FName FoamBeatId(TEXT("Night2.Foam"));
	const FName PuzzleId(TEXT("P2"));
}

AIGMissingFloorPuzzleTwoDirector::AIGMissingFloorPuzzleTwoDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool AIGMissingFloorPuzzleTwoDirector::Configure(AIGPrologueWorldScene* InScene)
{
	UWorld* World = GetWorld();
	if (!World || !InScene)
	{
		return false;
	}
	Scene = InScene;

	UStaticMesh* CubeMesh =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		return false;
	}
	UStaticMesh* ComplaintLedgerMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_ComplaintLedger.SM_ComplaintLedger"));
	UStaticMesh* ImpressionPadMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_ComplaintImpressionPad.SM_ComplaintImpressionPad"));
	UMaterialInterface* LedgerMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_PaperOld.M_PaperOld"));
	// 대리인 문자 사본은 어제 뽑은 출력물이다. 낡은 장부 종이가 아니라
	// 멀쩡한 종이를 쓴다. 재질을 비워 두면 엔진 기본 격자가 나온다.
	UMaterialInterface* SheetMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_PaperClean.M_PaperClean"));
	// 관리실 문짝과 채널 선택기가 쓰는 무광 검은 플라스틱. 씬이 안쪽 방
	// 문짝에 이미 같은 재질을 쓴다. 둘 다 재질이 비어 있어서 엔진 기본
	// 격자로 그려지고 있었다 — 선택기는 모니터 뒤에 숨어 아무도 못 봤고,
	// 문은 플레이어가 지나다니는 6 x 80 x 204cm짜리였다.
	UMaterialInterface* DarkPlasticMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_PlasticDark.M_PlasticDark"));
	UMaterialInterface* MetalMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_MetalFrame.M_MetalFrame"));
	// Her own phone, the cracked one CH03 already models.
	UStaticMesh* PhoneMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_CrackedPhone.SM_CrackedPhone"));

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// The booth door: same hinge grammar as the unit doors, panel spanning
	// the doorway BuildLobby left in the north wall. Starts day-locked;
	// SetHourActive is the only key it ever has.
	SpawnParameters.Name = TEXT("MissingFloorBoothDoor");
	BoothDoor = World->SpawnActor<AIGSwingDoor>(
		AIGSwingDoor::StaticClass(),
		FTransform(FRotator(0.0f, -90.0f, 0.0f), IGPuzzleTwo::BoothDoorHinge),
		SpawnParameters);
	if (!BoothDoor)
	{
		return false;
	}
	UStaticMesh* BoothLeaf = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Meshes/SM_UnitDoorLeafWideL.SM_UnitDoorLeafWideL"));
	if (BoothLeaf)
	{
		BoothDoor->ConfigureAuthoredLeaf(BoothLeaf,
			LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_UnitDoorHardwareWideL.SM_UnitDoorHardwareWideL")),
			FVector(5, AIGPrologueWorldScene::WideDoorLeafWidth, 200));
	}
	else
	{
		BoothDoor->ConfigurePrototypeVisuals(CubeMesh, DarkPlasticMaterial, MetalMaterial,
			FVector(6, AIGPrologueWorldScene::WideDoorLeafWidth, 204));
	}
	BoothDoor->SetOpenYaw(-95.0f);
	SetHourActive(false);

	// The fair copy: what the building admits happened.
	SpawnParameters.Name = TEXT("MissingFloorFairCopyLedger");
	FairCopyLedger = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleTwo::FairCopyLocation),
		SpawnParameters);
	if (!FairCopyLedger)
	{
		return false;
	}
	FairCopyLedger->ConfigurePrototypeVisuals(
		ComplaintLedgerMesh ? ComplaintLedgerMesh : CubeMesh,
		ComplaintLedgerMesh ? nullptr : LedgerMaterial,
		ComplaintLedgerMesh
			? FVector(100.0f, 100.0f, 100.0f)
			: FVector(21.0f, 1.2f, 29.7f),
		ComplaintLedgerMesh != nullptr);
	FairCopyLedger->SetInteractionPrompt(
		NSLOCTEXT("IGMissingFloor", "P2FairCopyPrompt", "민원 대장"));
	FairCopyLedger->SetNoteText(
		NSLOCTEXT("IGMissingFloor", "P2FairCopyTitle", "달빛빌라 민원 처리 대장"),
		{
			NSLOCTEXT("IGMissingFloor", "P2FairCopy1", "7/27  401호  물탱크 배관 소음.  조치 완료"),
			NSLOCTEXT("IGMissingFloor", "P2FairCopy2", "7/28  401호  배관 소음 재발.   조치 완료"),
			NSLOCTEXT("IGMissingFloor", "P2FairCopy3", "7/29  401호  옥상 바람 소리.   확인"),
			NSLOCTEXT("IGMissingFloor", "P2FairCopy4", "7/31  401호  소음 없음.       종결"),
			FText::GetEmpty(),
			NSLOCTEXT(
				"IGMissingFloor",
				"P2FairCopyNote",
				"담당  목한수  /  확인란 별도"),
		});

	// 뜯어낸 접수지 아래 남은 눌린 글씨. 저장 파일의 기존 Carbon 식별자는 유지한다.
	SpawnParameters.Name = TEXT("MissingFloorCarbonLedger");
	CarbonLedger = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleTwo::CarbonLocation),
		SpawnParameters);
	if (!CarbonLedger)
	{
		return false;
	}
	CarbonLedger->Configure(
		ImpressionPadMesh ? ImpressionPadMesh : CubeMesh,
		ImpressionPadMesh ? nullptr : SheetMaterial,
		ImpressionPadMesh
			? FVector::ZeroVector
			: FVector(21.0f, 29.7f, .6f),
		NSLOCTEXT("IGMissingFloor", "P2CarbonPrompt", "접수철 밑장 연필로 문지르기"),
		NSLOCTEXT(
			"IGMissingFloor",
			"P2CarbonRestored",
			"31일에도 들린다고 했는데, 장부엔 ‘소음 없음. 종결’이라고 적어 놨네."),
		EIGMissingFloorTruth::WasStillAlive,
		EIGMissingFloorSource::CarbonLedgerOriginal,
		IGPuzzleTwo::FrottageHoldSeconds,
		IGPuzzleTwo::FrottageLoudness);
	// §21.3 프로타주: the only sustained interaction in the game gets the only
	// sustained cue. Until now the player rubbed for 1.2 s in silence while the
	// noise bus reported 0.25 — the cost was real and inaudible.
	CarbonLedger->SetSustainedRubCue(true);
	CarbonLedger->SetProgressiveStages({
		NSLOCTEXT(
			"IGMissingFloor",
			"P2CarbonStage1",
			"7월 27일, 401호. ‘벽에서 쿵쿵. 사람 소리 같음.’ 다음 날에도 전화했네."),
		NSLOCTEXT(
			"IGMissingFloor",
			"P2CarbonStage2",
			"29일 새벽에도 들렸고, 30일엔 직접 와 달라고 했네."),
	});
	CarbonLedger->ConfigureProgressReveal(
		LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/M_ComplaintImpression.M_ComplaintImpression")),
		FVector(0, 0, .56f), FVector2D(20.9f, 29.6f),
		NSLOCTEXT("IGMissingFloor", "P2CarbonReadPrompt", "밑장 민원 다시 읽기"));
	if (UStaticMesh* PencilMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_GraphitePencil.SM_GraphitePencil")))
	{
		UStaticMeshComponent* Pencil = NewObject<UStaticMeshComponent>(CarbonLedger, TEXT("Pencil"));
		Pencil->SetupAttachment(CarbonLedger->GetRootComponent());
		Pencil->SetStaticMesh(PencilMesh);
		Pencil->SetRelativeLocation(FVector(-9.4f, -1.f, .92f));
		Pencil->SetRelativeRotation(FRotator(0, 0, 90));
		Pencil->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Pencil->SetAffectDistanceFieldLighting(false);
		Pencil->SetCanEverAffectNavigation(false);
		Pencil->RegisterComponent();
	}
	CarbonLedger->OnExamined.AddUObject(
		this, &AIGMissingFloorPuzzleTwoDirector::HandleCarbonRestored);

	// The agent's message: when the building says he left.
	SpawnParameters.Name = TEXT("MissingFloorAgentNote");
	AgentMessageNote = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(),
		FTransform(FRotator(0, 180, 0), IGPuzzleTwo::AgentNoteLocation),
		SpawnParameters);
	if (!AgentMessageNote)
	{
		return false;
	}
	AgentMessageNote->ConfigurePrototypeVisuals(
		CubeMesh, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/M_BoothAgentNote.M_BoothAgentNote")), FVector(18.0f, 24.0f, 0.08f));
	AgentMessageNote->SetInteractionPrompt(
		NSLOCTEXT("IGMissingFloor", "P2AgentPrompt", "문자 출력본"));
	AgentMessageNote->SetNoteText(
		NSLOCTEXT("IGMissingFloor", "P2AgentTitle", "무영부동산 문자 사본"),
		{
			NSLOCTEXT("IGMissingFloor", "P2Agent1", "7/26 14:02  부동산: 사장님, 옥탑 짐은 다 뺐습니다."),
			NSLOCTEXT("IGMissingFloor", "P2Agent2", "7/26 14:05  목한수: 네. 이제 창고로 쓸 거예요."),
			FText::GetEmpty(),
			NSLOCTEXT(
				"IGMissingFloor",
				"P2Agent3",
				"7/26 14:11  부동산: 열쇠는 우편함에 넣고 갑니다."),
		});
	AgentMessageNote->OnReadStateChanged.AddDynamic(
		this, &AIGMissingFloorPuzzleTwoDirector::HandleAgentNoteRead);

	// The monitor's channel selector: a four-way split and a fifth button.
	SpawnParameters.Name = TEXT("MissingFloorCctvSelector");
	CctvSelector = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleTwo::CctvLocation),
		SpawnParameters);
	if (!CctvSelector)
	{
		return false;
	}
	CctvSelector->Configure(
		CubeMesh,
		DarkPlasticMaterial,
		FVector(5.0f, 2.0f, 4.0f),
		NSLOCTEXT("IGMissingFloor", "P2CctvPrompt", "모니터 외부 입력 보기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		0.0f,
		0.06f, false);
	CctvSelector->OnExamined.AddUObject(
		this, &AIGMissingFloorPuzzleTwoDirector::HandleCctvExamined);

	// §14 CCTV 채널 5. Spawned with the booth so the monitor material and the AUX
	// label are resident before the press; the capture and its render target are
	// not created until the button is actually pushed (상시 렌더 금지).
	SpawnParameters.Name = TEXT("MissingFloorCctvChannelFive");
	CctvChannelFive = World->SpawnActor<AIGCctvChannelFive>(
		AIGCctvChannelFive::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!CctvChannelFive || !CctvChannelFive->Configure(InScene))
	{
		// The beat degrades to its narration rather than failing the whole booth:
		// a missing baked material must not cost the player P2 and T7.
		if (CctvChannelFive)
		{
			CctvChannelFive->Destroy();
			CctvChannelFive = nullptr;
		}
	}

	// The inner room's door gap: the foam reveal.
	SpawnParameters.Name = TEXT("MissingFloorFoamGap");
	FoamGap = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleTwo::FoamGapLocation),
		SpawnParameters);
	if (!FoamGap)
	{
		return false;
	}
	FoamGap->Configure(
		CubeMesh,
		nullptr,
		FVector(4.0f, 4.0f, 60.0f),
		NSLOCTEXT("IGMissingFloor", "P2FoamPrompt", "문틈 보기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		0.0f,
		0.05f,
		/*bPresentationVisible=*/false);
	FoamGap->OnExamined.AddUObject(
		this, &AIGMissingFloorPuzzleTwoDirector::HandleFoamExamined);

	// 1층 배관 밸브. 밸브라는 동사의 첫 수업이다 — 물이 흐르면 책상의 소리가
	// 묻힌다. 밤3의 5층 밸브, 밤4의 세 손잡이가 같은 손으로 이어진다.
	SpawnParameters.Name = TEXT("MissingFloorBoothRiserValve");
	BoothRiserValve = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator(0, 0, 90), IGPuzzleTwo::BoothValveLocation),
		SpawnParameters);
	if (!BoothRiserValve)
	{
		return false;
	}
	UStaticMesh* BoothValveMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_P3ValveWheelSmall.SM_P3ValveWheelSmall"));
	BoothRiserValve->Configure(
		BoothValveMesh,
		MetalMaterial,
		FVector::ZeroVector,
		NSLOCTEXT("IGMissingFloor", "BoothValvePrompt", "배관 밸브 열기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		IGPuzzleTwo::BoothValveHoldSeconds,
		IGPuzzleTwo::BoothValveLoudness);
	BoothRiserValve->GetPresentationMesh()->SetMobility(EComponentMobility::Movable);
	BoothRiserValve->OnExamined.AddUObject(
		this, &AIGMissingFloorPuzzleTwoDirector::HandleBoothValveOpened);

	// T5의 첫 번째 출처(§12). 목한수의 이중 서류 습관은 밤2 관리실에서
	// 심고 밤4·엔딩에서 그를 잡는 증거가 된다(§13).
	SpawnParameters.Name = TEXT("MissingFloorBoardReceipts");
	BoardReceipts = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(),
		FTransform(
			FRotator(0.0f, 173.0f, 0.0f),
			IGPuzzleTwo::BoardReceiptsLocation),
		SpawnParameters);
	if (!BoardReceipts)
	{
		return false;
	}
	BoardReceipts->ConfigurePrototypeVisuals(
		CubeMesh, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/M_BoothReceipts.M_BoothReceipts")), FVector(16.0f, 9.0f, 0.08f));
	BoardReceipts->SetInteractionPrompt(
		NSLOCTEXT("IGMissingFloor", "P2ReceiptsPrompt", "자재 반입 영수증"));
	BoardReceipts->SetNoteText(
		NSLOCTEXT("IGMissingFloor", "P2ReceiptsTitle", "자재 반입 영수증 (2장)"),
		{
			NSLOCTEXT("IGMissingFloor", "P2Receipt1", "무영건재  ·  무영로 27-3 달빛빌라"),
			FText::GetEmpty(),
			NSLOCTEXT("IGMissingFloor", "P2Receipt2", "7/26   석고보드 12.5T  12장     현금"),
			NSLOCTEXT("IGMissingFloor", "P2Receipt3", "       경량스터드 3.6m  8본     현금"),
			FText::GetEmpty(),
			NSLOCTEXT("IGMissingFloor", "P2Receipt4", "7/27   석고보드 12.5T  12장     현금"),
			NSLOCTEXT("IGMissingFloor", "P2Receipt5", "       미장몰탈 20kg    4포     현금"),
			FText::GetEmpty(),
			NSLOCTEXT(
				"IGMissingFloor",
				"P2Receipt6",
				"기재일 7/26  ·  옥상 보수비"),
		});
	BoardReceipts->OnReadStateChanged.AddDynamic(
		this, &AIGMissingFloorPuzzleTwoDirector::HandleBoardReceiptsRead);

	// §13 13행의 밤2 쪽. 낮에 폰으로 본 중고 매물이 관리인 책상에서는 현금으로
	// 적혀 있다. 진실 표에 들어가지 않는다 — 매물을 본 회차에만 한 줄이 붙고,
	// 못 본 회차에는 누가 무엇을 팔았다는 종이 한 장이다.
	SpawnParameters.Name = TEXT("MissingFloorBoothCashMemo");
	CashMemo = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(),
		FTransform(
			FRotator(0.0f, 166.0f, 0.0f),
			IGPuzzleTwo::CashMemoLocation),
		SpawnParameters);
	if (CashMemo)
	{
		CashMemo->ConfigurePrototypeVisuals(
			CubeMesh,
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/M_PaperOld.M_PaperOld")),
			FVector(9.0f, 6.5f, 0.06f));
		CashMemo->SetInteractionPrompt(
			NSLOCTEXT("IGMissingFloor", "P2CashMemoPrompt", "책상 메모"));
		CashMemo->SetNoteText(
			NSLOCTEXT("IGMissingFloor", "P2CashMemoTitle", "현금 메모"),
			{
				NSLOCTEXT("IGMissingFloor", "P2CashMemo1", "8/12  옥탑 잔짐  공구 일괄   30,000"),
				NSLOCTEXT("IGMissingFloor", "P2CashMemo2", "      직거래  ·  닉네임 달빛"),
			});
		CashMemo->OnReadStateChanged.AddDynamic(
			this, &AIGMissingFloorPuzzleTwoDirector::HandleCashMemoRead);
	}

	// §22.3. 둘 다 진실 표에 들어가지 않는다 — 밤2를 통과하는 데는 필요
	// 없고, 본 회차에만 목한수 앞에서 댈 것이 늘어난다.
	SpawnParameters.Name = TEXT("MissingFloorWallCalendar");
	WallCalendar = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleTwo::WallCalendarLocation),
		SpawnParameters);
	if (WallCalendar)
	{
		WallCalendar->Configure(
			CubeMesh,
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/M_BoothCalendar.M_BoothCalendar")),
			FVector(30.0f, 0.16f, 42.0f),
			NSLOCTEXT("IGMissingFloor", "P2CalendarPrompt", "벽 달력"),
			NSLOCTEXT(
				"IGMissingFloor",
				"P2CalendarThought",
				"작년 7월 달력이네. 26일에만 동그라미가 쳐져 있다."),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			0.9f,
			0.04f);
		WallCalendar->OnExamined.AddUObject(
			this, &AIGMissingFloorPuzzleTwoDirector::HandleWallCalendarExamined);
	}

	SpawnParameters.Name = TEXT("MissingFloorRecorderBay");
	RecorderBay = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleTwo::RecorderBayLocation),
		SpawnParameters);
	if (RecorderBay)
	{
		RecorderBay->Configure(
			CubeMesh,
			DarkPlasticMaterial,
			FVector(26.0f, 3.0f, 4.5f),
			NSLOCTEXT("IGMissingFloor", "P2RecorderPrompt", "녹화기"),
			NSLOCTEXT(
				"IGMissingFloor",
				"P2RecorderThought",
				"화면에 ‘HDD 없음’이라고 떠 있네. 녹화 표시등도 꺼져 있고."),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			0.9f,
			0.06f, false);
		RecorderBay->OnExamined.AddUObject(
			this, &AIGMissingFloorPuzzleTwoDirector::HandleRecorderBayExamined);
	}

	// 같은 문에서 눈과 귀가 다른 것을 준다. 문틈은 계란판을 보여 주고,
	// 여기서는 그 너머에서 뭔가 돌고 있다는 것만 들린다.
	SpawnParameters.Name = TEXT("MissingFloorInnerRoomListen");
	InnerRoomListen = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleTwo::InnerRoomListenLocation),
		SpawnParameters);
	if (InnerRoomListen)
	{
		InnerRoomListen->Configure(
			CubeMesh,
			nullptr,
			FVector(40.0f, 10.0f, 60.0f),
			NSLOCTEXT(
				"IGMissingFloor",
				"P2InnerRoomListenPrompt",
				"안쪽 방 문에 귀 대기"),
			FText::GetEmpty(),
			EIGMissingFloorTruth::None,
			EIGMissingFloorSource::None,
			1.0f,
			0.03f,
			/*bPresentationVisible=*/false);
		InnerRoomListen->Tags.AddUnique(FName(TEXT("MissingFloor.Verb.Listen")));
		InnerRoomListen->OnExamined.AddUObject(
			this, &AIGMissingFloorPuzzleTwoDirector::HandleInnerRoomListenExamined);
	}

	// §5.5 비트 2-1과 2-6. 같은 물건이 밤에는 녹음을 켜고 아침에는 그것을
	// 재생한다. 두 번째 상호작용이 이 게임에서 가장 조용한 절망이다.
	SpawnParameters.Name = TEXT("MissingFloorPhoneRecorder");
	PhoneRecorder = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator(0.0f, 12.0f, 0.0f), IGPuzzleTwo::PhoneAtDoorLocation),
		SpawnParameters);
	if (!PhoneRecorder)
	{
		return false;
	}
	// 저작된 깨진 폰은 이미 실제 크기다. (100.0f)를 넘기면 1m 정육면체가
	// 되고, 재질을 비우면 엔진 기본 격자가 나온다. 밤4가 같은 메시에 쓰는
	// 무광 검은 플라스틱을 맞춰 준다.
	PhoneRecorder->Configure(
		PhoneMesh ? PhoneMesh : CubeMesh,
		DarkPlasticMaterial,
		PhoneMesh ? FVector::ZeroVector : FVector(7.0f, 14.5f, 1.6f),
		NSLOCTEXT("IGMissingFloor", "P2PhoneArmPrompt", "폰으로 녹음하기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		0.0f,
		IGPuzzleTwo::PhonePlacementLoudness);
	PhoneRecorder->OnExamined.AddUObject(
		this, &AIGMissingFloorPuzzleTwoDirector::HandlePhoneRecorder);
	RefreshPhonePrompt();

	// §13 3행. 목한수는 낮에 문을 열어 주지 않는다. 대신 문 옆에 붙여 둔 종이가
	// 그가 유담에게 하는 첫 말이다. 글은 합쇼체이고, 밤4에 입으로 하는 말은
	// 해요체다. 여기 적힌 바람이 대치의 「바람이 둘, 쉬고, 하나로 불어요?」가
	// 되받는 말이고, 옥탑은 배송 라벨에 적힌 오빠의 주소다.
	SpawnParameters.Name = TEXT("MissingFloorBoothNotice");
	BoothNotice = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleTwo::BoothNoticeLocation),
		SpawnParameters);
	if (BoothNotice)
	{
		BoothNotice->ConfigurePrototypeVisuals(
			CubeMesh, SheetMaterial, FVector(21.0f, 0.08f, 14.8f));
		BoothNotice->SetInteractionPrompt(
			NSLOCTEXT("IGMissingFloor", "BoothNoticePrompt", "관리실 쪽지"));
		BoothNotice->SetNoteText(
			NSLOCTEXT("IGMissingFloor", "BoothNoticeTitle", "입주민 여러분께"),
			{
				NSLOCTEXT("IGMissingFloor", "BoothNotice1", "옥탑은 창고로 쓰고 있습니다. 거주자는 없습니다."),
				NSLOCTEXT("IGMissingFloor", "BoothNotice2", "새벽에 들리는 소리는 물탱크 배관 소음입니다."),
				NSLOCTEXT("IGMissingFloor", "BoothNotice3", "불편한 점은 호수와 연락처를 적어 관리실 문 아래로 넣어 주세요."),
				FText::GetEmpty(),
				NSLOCTEXT("IGMissingFloor", "BoothNotice4", "관리인"),
			});
	}
	RefreshBoothNotice();

	// 밤2부터 문자와 접수철을 어느 쪽 순서로도 조사할 수 있다.
	RefreshAgentNoteAvailability();
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		TruthHandle = Narrative->OnTruthConfirmed.AddUObject(
			this, &AIGMissingFloorPuzzleTwoDirector::HandleTruthConfirmed);
	}
	return true;
}

void AIGMissingFloorPuzzleTwoDirector::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->OnTruthConfirmed.Remove(TruthHandle);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CctvThoughtTimer);
		World->GetTimerManager().ClearTimer(PhoneThoughtTimer);
		World->GetTimerManager().ClearTimer(PhoneReturnTimer);
		World->GetTimerManager().ClearTimer(PhoneBuzzTimer);
		World->GetTimerManager().ClearTimer(PhoneBuzzCutTimer);
		World->GetTimerManager().ClearTimer(InnerRoomCreakTimer);
		World->GetTimerManager().ClearTimer(InnerRoomClothTimer);
		World->GetTimerManager().ClearTimer(InnerRoomThoughtTimer);
	}
	CloseBoothValve();
	Super::EndPlay(EndPlayReason);
}

void AIGMissingFloorPuzzleTwoDirector::SetHourActive(const bool bHourActive)
{
	bHourActiveCached = bHourActive;
	RefreshAgentNoteAvailability();
	RefreshBoothNotice();
	if (bHourActive)
	{
		// 새 밤이 오면 손에 들고 있던 폰도 이미 내려놓았다.
		bPhoneInHand = false;
		GetWorldTimerManager().ClearTimer(PhoneReturnTimer);
		GetWorldTimerManager().ClearTimer(PhoneBuzzTimer);
	}
	// §5.5: dawn closes the take. She left it running all night and now there is
	// something to play — which is the only reason beat 2-6 can exist.
	if (UWorld* World = GetWorld())
	{
		if (UIGRecordingSubsystem* Recording =
			World->GetSubsystem<UIGRecordingSubsystem>())
		{
			if (!bHourActive && Recording->IsRecording())
			{
				Recording->StopRecording();
				bPhonePlayedBack = false;
				// 신호가 돌아오면 바닥의 폰이 한 번 떤다. 아침 프롬프트만 바뀌고
				// 아무도 그리로 부르지 않으면 2-6은 지나가 버린다.
				const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
				if (Narrative && Narrative->GetNightIndex() == 2 && Recording->HasTake())
				{
					GetWorldTimerManager().SetTimer(
						PhoneBuzzTimer,
						this,
						&AIGMissingFloorPuzzleTwoDirector::PlayMorningPhoneBuzz,
						IGPuzzleTwo::PhoneMorningBuzzSeconds,
						false);
				}
			}
		}
	}
	RefreshPhonePrompt();
	if (!bHourActive)
	{
		// 낮에 그가 도로 잠근다.
		CloseBoothValve();
		// 새벽 암전 아래로 문 너머의 뒤척임이 새지 않게.
		GetWorldTimerManager().ClearTimer(InnerRoomCreakTimer);
		GetWorldTimerManager().ClearTimer(InnerRoomClothTimer);
		GetWorldTimerManager().ClearTimer(InnerRoomThoughtTimer);
	}

	if (!BoothDoor)
	{
		return;
	}
	if (bHourActive)
	{
		// He is hiding in the soundproofed room; the booth stands open.
		TArray<FIGDoorRequirement> NoRequirements;
		BoothDoor->SetRequirements(MoveTemp(NoRequirements));
	}
	else
	{
		// Daytime post. The requirement tag is a registered sentinel that is
		// never granted — the door opens by clearing requirements, never by
		// a tag, so no lock state can leak into a save.
		BoothDoor->ForceOpenState(false);
		TArray<FIGDoorRequirement> DayLock;
		FIGDoorRequirement& Lock = DayLock.AddDefaulted_GetRef();
		Lock.RequiredState = FGameplayTag::RequestGameplayTag(
			FName(TEXT("State.MissingFloor.Office.Vacant")), false);
		Lock.LockedPrompt =
			NSLOCTEXT("IGMissingFloor", "BoothDayPrompt", "관리실 문 두드리기");
		Lock.LockedThought = NSLOCTEXT(
			"IGMissingFloor",
			"BoothDayThought",
			"안에 사람 있는데. 낮에는 문을 안 열어 주네.");
		BoothDoor->SetRequirements(MoveTemp(DayLock));
	}
}

void AIGMissingFloorPuzzleTwoDirector::HandleCarbonRestored(
	AIGMissingFloorEvidence* Evidence)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		if (!Narrative->HasTruth(EIGMissingFloorTruth::WasStillAlive)) { return; }
		Narrative->MarkPuzzleSolved(IGPuzzleTwo::PuzzleId);
	}
	// 원문을 읽는 데서 끝내지 않고, 퇴실 문자와 접수 날짜를 대조한다.
	if (!bSolvedAnnounced)
	{
		bSolvedAnnounced = true;
		OnSolved.Broadcast();
	}
}

void AIGMissingFloorPuzzleTwoDirector::HandleBoothValveOpened(
	AIGMissingFloorEvidence* Evidence)
{
	UWorld* World = GetWorld();
	if (bBoothValveOpen || !World)
	{
		return;
	}
	bBoothValveOpen = true;
	BoothRiserValve->GetPresentationMesh()->AddLocalRotation(FRotator(0, 90, 0));
	if (Evidence)
	{
		Evidence->SetInteractionPrompt(
			NSLOCTEXT("IGMissingFloor", "BoothValveOpenPrompt", "열린 배관 밸브"));
		Evidence->SetInteractionEnabled(false);
	}
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateValveOpen(this, 0),
		IGPuzzleTwo::BoothValveLocation,
		0.7f,
		1.0f,
		160.0f,
		1400.0f,
		EIGAudioBus::Puzzle);

	// 관에 물이 흐른다. 흡입관 옆에서는 가까이, 책상에서는 한 단 멀리.
	BoothRiserFlow = NewObject<UAudioComponent>(this, TEXT("BoothRiserFlowBed"));
	if (BoothRiserFlow)
	{
		BoothRiserFlow->RegisterComponent();
		BoothRiserFlow->SetWorldLocation(
			IGPuzzleTwo::BoothValveLocation + FVector(0.0f, -15.0f, -20.0f));
		BoothRiserFlow->SetSound(
			UIGToneSequenceSoundWave::CreatePipeWaterFlow(this, 1));
		BoothRiserFlow->AttenuationSettings = IGAudio::MakeAttenuation(
			this,
			120.0f,
			900.0f,
			EIGAudioBus::Puzzle);
		BoothRiserFlow->bAllowSpatialization = true;
		BoothRiserFlow->SetVolumeMultiplier(0.5f);
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			// 새벽까지 흐르는 물길이다. 한 번 울고 마는 소리로 걸면 PUZZLE 상한에서
			// 가장 오래된 것으로 밀려 페이드아웃되고, 책상을 덮던 물소리가 마스킹은
			// 그대로인 채 조용해진다 — 들리지 않는 엄폐가 된다. 밸브를 잠글 때 이
			// 컴포넌트를 부수므로 자리를 영영 쥐지도 않는다.
			AudioDirector->RegisterPersistentBed(BoothRiserFlow, EIGAudioBus::Puzzle);
		}
		BoothRiserFlow->Play();
	}
	// 책상 둘레가 물소리 아래로 들어간다. 문지르기 0.25는 여기서 삼켜지고,
	// 소음 파문도 안 뜬다 — 그게 수업이다.
	if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
	{
		BoothValveHumHandle = Noise->RegisterHumSource(
			IGPuzzleTwo::BoothMaskCenter,
			IGPuzzleTwo::BoothMaskRadius,
			IGPuzzleTwo::BoothMaskAmount);
	}
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"BoothValveThought",
			"배관에서 물소리가 난다. 책상까지 울리네."),
		3.6f);
}

void AIGMissingFloorPuzzleTwoDirector::CloseBoothValve()
{
	if (!bBoothValveOpen)
	{
		return;
	}
	bBoothValveOpen = false;
	if (BoothRiserValve) { BoothRiserValve->GetPresentationMesh()->AddLocalRotation(FRotator(0, -90, 0)); }
	if (BoothRiserFlow)
	{
		BoothRiserFlow->Stop();
		BoothRiserFlow->DestroyComponent();
		BoothRiserFlow = nullptr;
	}
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			if (BoothValveHumHandle != INDEX_NONE)
			{
				Noise->UnregisterHumSource(BoothValveHumHandle);
			}
		}
	}
	BoothValveHumHandle = INDEX_NONE;
	if (BoothRiserValve)
	{
		BoothRiserValve->SetInteractionPrompt(
			NSLOCTEXT("IGMissingFloor", "BoothValvePrompt", "배관 밸브 열기"));
		BoothRiserValve->SetInteractionEnabled(true);
	}
}

void AIGMissingFloorPuzzleTwoDirector::RefreshAgentNoteAvailability()
{
	if (!AgentMessageNote)
	{
		return;
	}
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const bool bPresent = Narrative != nullptr;
	AgentMessageNote->SetActorHiddenInGame(!bPresent);
	AgentMessageNote->SetActorEnableCollision(bPresent);
	AgentMessageNote->SetInteractionEnabled(bPresent);
}

void AIGMissingFloorPuzzleTwoDirector::RefreshBoothNotice()
{
	if (!BoothNotice)
	{
		return;
	}
	// 입주 날에는 아직 없다. 「내일 관리인부터 만나 봐야겠다」고 잔 다음부터
	// 닫힌 문 옆에 이 종이가 있다. 경찰이 다녀가고 요구서를 붙인 날 떼어 냈다.
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const bool bPosted = Narrative
		&& Narrative->GetNightIndex() >= 1
		&& !IsEvictionNoticeUp();
	BoothNotice->SetActorHiddenInGame(!bPosted);
	BoothNotice->SetActorEnableCollision(bPosted);
	BoothNotice->SetInteractionEnabled(bPosted);
}

bool AIGMissingFloorPuzzleTwoDirector::IsEvictionNoticeUp() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AIGMissingFloorNightFourDirector> It(World); It; ++It)
	{
		return It->IsEvictionNoticePosted();
	}
	return false;
}

void AIGMissingFloorPuzzleTwoDirector::HandleAgentNoteRead(
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
			EIGMissingFloorTruth::WasStillAlive,
			EIGMissingFloorSource::AgentMoveOutMessage);
	}
}

void AIGMissingFloorPuzzleTwoDirector::RefreshPhonePrompt()
{
	if (!PhoneRecorder)
	{
		return;
	}
	RefreshPhoneOffer();
	const UWorld* World = GetWorld();
	const UIGRecordingSubsystem* Recording = World
		? World->GetSubsystem<UIGRecordingSubsystem>()
		: nullptr;
	if (!Recording)
	{
		return;
	}
	if (Recording->IsRecording())
	{
		PhoneRecorder->SetInteractionPrompt(
			NSLOCTEXT("IGMissingFloor", "P2PhoneRecording", "폰 녹음 중"));
		return;
	}
	if (Recording->HasTake() && !bPhonePlayedBack)
	{
		PhoneRecorder->SetInteractionPrompt(
			NSLOCTEXT("IGMissingFloor", "P2PhonePlayPrompt", "녹음 들어 보기"));
		return;
	}
	PhoneRecorder->SetInteractionPrompt(
		NSLOCTEXT("IGMissingFloor", "P2PhoneArmPrompt", "폰으로 녹음하기"));
}

void AIGMissingFloorPuzzleTwoDirector::RefreshPhoneOffer()
{
	if (!PhoneRecorder)
	{
		return;
	}
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	const UWorld* World = GetWorld();
	const UIGRecordingSubsystem* Recording = World
		? World->GetSubsystem<UIGRecordingSubsystem>()
		: nullptr;
	// 밤2 동안, 그리고 밤새 켜 둔 테이프를 아직 틀지 않은 아침에만 바닥에 있다.
	// NightIndex는 시간이 시작될 때 오르므로 밤2 다음 낮에도 2다.
	const bool bOnFloor = Narrative && Recording
		&& Narrative->GetNightIndex() == 2
		&& (bHourActiveCached
			|| Recording->IsRecording()
			|| (Recording->HasTake() && !bPhonePlayedBack));
	PhoneRecorder->SetActorHiddenInGame(!bOnFloor);
	PhoneRecorder->SetActorEnableCollision(bOnFloor);
	PhoneRecorder->SetInteractionEnabled(bOnFloor);

	const bool bInUse = bOnFloor || bPhoneInHand;
	if (bInUse != bPhoneInUse)
	{
		bPhoneInUse = bInUse;
		OnPhoneInUseChanged.Broadcast();
	}
}

void AIGMissingFloorPuzzleTwoDirector::PlayMorningPhoneBuzz()
{
	if (!PhoneRecorder || PhoneRecorder->IsHidden() || bPhonePlayedBack)
	{
		return;
	}
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return;
	}
	const FVector PhoneAt = PhoneRecorder->GetActorLocation();
	const FVector ToPlayer = Pawn->GetActorLocation() - PhoneAt;
	if (ToPlayer.Size() > IGPuzzleTwo::PhoneMorningBuzzReach
		|| FMath::Abs(ToPlayer.Z) > IGPuzzleTwo::PhoneMorningBuzzFloorSpan)
	{
		return;
	}
	// 바닥에 놓인 물건의 소리라 WORLD다. 그녀의 몸에서 나는 소리가 아니다.
	UAudioComponent* Buzz = IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Phone_Vibrate"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreatePhoneVibrationUnfinished(this); }),
		PhoneAt,
		0.45f,
		1.0f,
		60.0f,
		500.0f,
		EIGAudioBus::World);
	if (Buzz)
	{
		// 녹음은 12초다. 한 번 떨고 멎게 자른다.
		TWeakObjectPtr<UAudioComponent> WeakBuzz(Buzz);
		GetWorldTimerManager().SetTimer(
			PhoneBuzzCutTimer,
			FTimerDelegate::CreateWeakLambda(this, [WeakBuzz]()
			{
				if (UAudioComponent* Component = WeakBuzz.Get())
				{
					Component->FadeOut(0.25f, 0.0f);
				}
			}),
			IGPuzzleTwo::PhoneMorningBuzzCutSeconds,
			false);
	}
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "P2PhoneMorningBuzz", "폰 진동"),
		1.8f,
		PhoneAt);
}

FVector AIGMissingFloorPuzzleTwoDirector::GetPhoneInHandLocation(
	const AIGMissingFloorEvidence* Evidence) const
{
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		// 눈앞 조금 아래, 귀에 대기 직전의 손.
		return Pawn->GetPawnViewLocation()
			+ Pawn->GetControlRotation().Vector() * 22.0f
			+ FVector(0.0f, 0.0f, -12.0f);
	}
	return Evidence ? Evidence->GetActorLocation() : IGPuzzleTwo::PhoneAtDoorLocation;
}

void AIGMissingFloorPuzzleTwoDirector::HandleCashMemoRead(
	AIGReadableNote* Note,
	const bool bOpened)
{
	if (!bOpened)
	{
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->MarkBeatPlayed(IGPuzzleTwo::CashMemoBeatId))
	{
		return;
	}
	// 매물을 본 회차에만. 못 본 사람에게는 누가 무엇을 팔았다는 종이 한 장이고,
	// 그것도 맞는 말이다.
	if (Narrative->HasBeatPlayed(FName(TEXT("Day.UsedListing"))))
	{
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT("IGMissingFloor", "P2CashMemoThought", "8월 12일, 3만 원. 그 중고 매물이네."),
			3.8f);
	}
}

void AIGMissingFloorPuzzleTwoDirector::HandlePhoneRecorder(
	AIGMissingFloorEvidence* Evidence)
{
	UWorld* World = GetWorld();
	UIGRecordingSubsystem* Recording = World
		? World->GetSubsystem<UIGRecordingSubsystem>()
		: nullptr;
	if (!Recording)
	{
		return;
	}

	// 비트 2-6. The take exists, so this press is the morning one.
	if (!Recording->IsRecording() && Recording->HasTake() && !bPhonePlayedBack)
	{
		// 폰을 집어 들고 듣는다. 바닥에 둔 채 1.5m 떨어진 작은 스피커는 낮
		// 베드에 깔려서, 정확한 길이의 무음이 아무 일 없는 16초로 들렸다.
		const FVector At = GetPhoneInHandLocation(Evidence);
		if (!Recording->PlayBack(At))
		{
			return;
		}
		bPhonePlayedBack = true;
		bPhoneInHand = true;
		RefreshPhonePrompt();
		// 거부된 자리가 있으면 그 공백이 대사다. 거부된 게 없는 테이프는 둘로
		// 갈린다 — 규칙이 풀린 뒤라면 담긴 것이고, 아니면 그가 다녀간 뒤에
		// 켠 것이다. 후자에 「담겼다」를 주면 규칙이 거짓말이 된다.
		const bool bAnythingRefused = Recording->GetSuppressedCount() > 0;
		const bool bRuleLifted = Recording->IsRuleLifted();
		const FText Thought = bAnythingRefused
			? NSLOCTEXT(
				"IGMissingFloor",
				"P2PhoneSilence",
				"내 발소리는 들리는데, 문에서 난 소리는 하나도 안 담겼어.")
			: bRuleLifted
				? NSLOCTEXT(
					"IGMissingFloor",
					"P2PhoneKept",
					"녹음됐다. 이번엔 제대로 됐어.")
				: NSLOCTEXT(
					"IGMissingFloor",
					"P2PhoneNothing",
					"내 발소리랑 숨소리만 녹음됐네.");
		// 결론은 증거 뒤에 온다. 재생과 같은 틱에 뜨면 공백이 오기도 전에 글이
		// 먼저 말해 버린다. 발췌가 둘러싼 순간(3연이 비운 자리)이 끝나고 숨
		// 한 번 뒤에 띄운다.
		const float FocusEnd = Recording->GetLastExcerptFocusEndSeconds();
		if ((bAnythingRefused || bRuleLifted) && FocusEnd >= 0.0f)
		{
			GetWorldTimerManager().SetTimer(
				PhoneThoughtTimer,
				FTimerDelegate::CreateWeakLambda(this, [this, Thought, bAnythingRefused]()
				{
					AIGHorrorHUD::PushThought(this, Thought, 5.0f);
					if (!bAnythingRefused)
					{
						return;
					}
					if (const AIGPlayerCharacter* Character = Cast<AIGPlayerCharacter>(
						UGameplayStatics::GetPlayerPawn(this, 0)))
					{
						if (UIGStressComponent* Stress = Character->GetStress())
						{
							Stress->ApplyScare(IGPuzzleTwo::PhoneSilenceScare);
						}
					}
				}),
				FocusEnd + IGPuzzleTwo::PhoneThoughtAfterGapSeconds,
				false);
		}
		else
		{
			AIGHorrorHUD::PushThought(this, Thought, 5.0f);
		}
		// 다 듣고 나면 폰은 탁자로 돌아간다. 탁자의 폰(중고 거래 앱 화면)이 다시 보인다.
		GetWorldTimerManager().SetTimer(
			PhoneReturnTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				bPhoneInHand = false;
				RefreshPhoneOffer();
			}),
			Recording->GetLastPlaybackSeconds()
				+ IGPuzzleTwo::PhoneReturnAfterPlaybackSeconds,
			false);
		return;
	}

	// 비트 2-1. She sets it against the door and lets it run.
	if (!Recording->IsRecording())
	{
		Recording->StartRecording();
		// 폰은 현관 바닥에서 제 둘레만 듣는다. 내려놓는 소리가 테이프의 첫
		// 소리다 — 그 소음 보고는 녹음이 켜지기 한 틱 전에 나가서 테이프에 없었다.
		const FVector PhoneAt = Evidence
			? Evidence->GetActorLocation()
			: IGPuzzleTwo::PhoneAtDoorLocation;
		Recording->SetMicrophoneLocation(PhoneAt);
		Recording->RecordPlayerBody(IGPuzzleTwo::PhonePlacementLoudness, /*bBreath=*/false);
		bPhonePlayedBack = false;
		RefreshPhonePrompt();
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"P2PhoneArmed",
				"여기 두면 들리겠지."),
			3.4f);
	}
}

void AIGMissingFloorPuzzleTwoDirector::HandleCctvExamined(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->MarkBeatPlayed(IGPuzzleTwo::CctvBeatId))
	{
		return;
	}
	// One press, one look, and the channel dies back to the four-way split.
	// The fifth floor exists on a monitor before it exists underfoot — and
	// the low shape crossing the frame is why the player climbs in night 3.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateRelayClick(this),
		Evidence ? Evidence->GetActorLocation() : GetActorLocation(),
		0.7f);
	// The picture comes up on the monitor itself. When the channel cannot be
	// built the thought still lands, because losing the reveal must never leave
	// the player without the reason to climb in 밤3.
	const bool bChannelLive = CctvChannelFive && CctvChannelFive->Play();
	AIGHorrorHUD::PushAudioCaption(
		this,
		NSLOCTEXT("IGMissingFloor", "P2CctvCaption", "화면 지지직거리는 소리"),
		1.6f);
	if (!bChannelLive)
	{
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"P2CctvThought1",
				"어디지? 이 건물에 이런 복도가 있었나?"),
			4.4f);
		return;
	}
	// 화면이 살아 있는 동안은 글이 없다. 보는 동안 읽게 하면 둘 다 놓친다.
	// 스노우 0.32초, 화면 5.60초, 찢어짐 0.86초가 지나고 숨 한 번 뒤에
	// 남은 것만 말한다 — 낮게 지나간 것은 본 사람의 몫으로 남긴다.
	GetWorldTimerManager().SetTimer(
		CctvThoughtTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT(
					"IGMissingFloor",
					"P2CctvThought1",
					"어디지? 이 건물에 이런 복도가 있었나?"),
				4.4f);
			// §5.5. The picture is on screen and already unrecoverable: nothing
			// about channel 5 reaches the recorder, and the label on the case
			// says so. This is the line that keeps 위험 8 — 화면과 규칙의 모순 —
			// from being possible.
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT(
					"IGMissingFloor",
					"P2CctvThought2",
					"녹화는 안 되네. 어디로 이어지는지 봐 두자."),
				4.2f);
		}),
		0.32f + AIGCctvChannelFive::LiveSeconds + 0.86f + 0.25f,
		false);
}

void AIGMissingFloorPuzzleTwoDirector::HandleBoardReceiptsRead(
	AIGReadableNote* Note,
	const bool bOpened)
{
	// 펼친 순간에 적는다. 두 날짜가 같은 종이에 있으므로 볼 것은 다 봤다.
	if (!bOpened)
	{
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	Narrative->RegisterTruthSource(
		EIGMissingFloorTruth::WallSealedThatDay,
		EIGMissingFloorSource::BoardDeliveryReceipt);
	if (Narrative->MarkBeatPlayed(FName(TEXT("Night2.BoardReceipts"))))
	{
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"P2ReceiptsThought",
				"26일에도 사고, 27일에도 샀네. 장부엔 26일 것만 적혀 있는데."),
			4.6f);
	}
}

void AIGMissingFloorPuzzleTwoDirector::HandleWallCalendarExamined(
	AIGMissingFloorEvidence* Evidence)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RecordWitness(EIGMissingFloorWitness::BoothWallCalendar);
	}
}

void AIGMissingFloorPuzzleTwoDirector::HandleInnerRoomListenExamined(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	Narrative->RecordWitness(EIGMissingFloorWitness::BoothInnerRoomHum);
	const FVector Where = Evidence
		? Evidence->GetActorLocation()
		: IGPuzzleTwo::InnerRoomListenLocation;
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateFoamedRoomHum(this),
		Where,
		0.60f,
		1.0f,
		80.0f,
		360.0f,
		EIGAudioBus::World);
	// 자막은 한 줄씩 줄을 선다. 험 줄을 짧게 두어야 뒤의 소리와 박자가 맞는다.
	AIGHorrorHUD::PushAudioCaption(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"P2InnerRoomCaption",
			"[문 너머] 뭔가 낮게 웅웅 도는 소리"),
		IGPuzzleTwo::InnerRoomCreakSeconds);
	// 기계만 도는 방이 아니다. 간이침대의 나무가 한 번 삐걱이고, 몸을 뒤척이는
	// 천 소리가 난다. 목소리도 기침도 숨도 넣지 않는다 — 산 사람의 목소리는
	// 글로만 온다(§4.6).
	GetWorldTimerManager().SetTimer(
		InnerRoomCreakTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, Where]()
		{
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateSettleTimberCreak(this),
				Where,
				0.20f,
				0.78f,
				60.0f,
				300.0f,
				EIGAudioBus::World);
			AIGHorrorHUD::PushAudioCaption(
				this,
				NSLOCTEXT("IGMissingFloor", "P2InnerRoomCreakCaption", "[문 너머] 나무 한 번 삐걱거리는 소리"),
				IGPuzzleTwo::InnerRoomClothSeconds - IGPuzzleTwo::InnerRoomCreakSeconds);
		}),
		IGPuzzleTwo::InnerRoomCreakSeconds,
		false);
	GetWorldTimerManager().SetTimer(
		InnerRoomClothTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, Where]()
		{
			IGAudio::SpawnOneShotAt(
				this,
				UIGToneSequenceSoundWave::CreateClothSettle(this),
				Where,
				0.32f,
				0.85f,
				60.0f,
				300.0f,
				EIGAudioBus::World);
			AIGHorrorHUD::PushAudioCaption(
				this,
				NSLOCTEXT("IGMissingFloor", "P2InnerRoomClothCaption", "[문 너머] 천 스치는 소리"),
				1.8f);
		}),
		IGPuzzleTwo::InnerRoomClothSeconds,
		false);
	// 들은 뒤에 말한다. 소리와 같은 프레임에 결론이 뜨면 증거보다 말이 먼저다.
	GetWorldTimerManager().SetTimer(
		InnerRoomThoughtTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			AIGHorrorHUD::PushThought(
				this,
				NSLOCTEXT(
					"IGMissingFloor",
					"P2InnerRoomThought",
					"안에서 누가 뒤척였어. 지금 문 열면 들킨다."),
				4.4f);
		}),
		IGPuzzleTwo::InnerRoomThoughtSeconds,
		false);
}

void AIGMissingFloorPuzzleTwoDirector::HandleRecorderBayExamined(
	AIGMissingFloorEvidence* Evidence)
{
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RecordWitness(EIGMissingFloorWitness::RecorderEmptyBay);
	}
}

void AIGMissingFloorPuzzleTwoDirector::HandleFoamExamined(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->MarkBeatPlayed(IGPuzzleTwo::FoamBeatId))
	{
		return;
	}
	// §22.3. 이 문틈은 진실을 열지 않는다 — 열어야 할 진실이 없다. 대신
	// 본 사람의 엔딩 자막에서 「듣지 않은 사람」이 이름을 얻는다.
	Narrative->RecordWitness(EIGMissingFloorWitness::BoothSoundproofing);
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"P2FoamThought",
			"문틈까지 죄다 막아 놨네. 소리 때문에 그런가?"),
		4.6f);
}

void AIGMissingFloorPuzzleTwoDirector::HandleTruthConfirmed(
	const EIGMissingFloorTruth Truth)
{
	if (Truth != EIGMissingFloorTruth::WasStillAlive)
	{
		return;
	}
	// 문자부터 읽었든 먹지부터 복원했든, 둘을 모으면 같은 지점에 도착한다.
	HandleCarbonRestored(CarbonLedger);
}

bool AIGMissingFloorPuzzleTwoDirector::ValidateFixtures() const
{
	return BoothDoor != nullptr
		&& FairCopyLedger != nullptr
		&& CarbonLedger != nullptr
		&& AgentMessageNote != nullptr
		&& CctvSelector != nullptr
		&& BoardReceipts != nullptr
		&& FoamGap != nullptr;
}

UIGMissingFloorNarrativeSubsystem*
AIGMissingFloorPuzzleTwoDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
}
