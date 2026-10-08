#include "Entity/IGMissingFloorNightFourDirector.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "IndieGame.h"
#include "Interaction/IGReadableNote.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGMissingFloorFifthDawnDirector.h"
#include "Entity/IGMissingFloorMercyDirector.h"
#include "Entity/IGMissingFloorNightThreeDirector.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Entity/IGNightLoopDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "Entity/IGNightThreatDirector.h"
#include "Environment/IGDustSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Math/RotationMatrix.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGRecordingSubsystem.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Save/IGSaveSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"

namespace IGNightFour
{
	const FName CleaningDrainId(TEXT("P5.RoofCleaningDrain"));
	const FName FloatBypassId(TEXT("P5.RoofFloatBypass"));
	const FName TransferPumpId(TEXT("P5.TransferPump"));
	const FName PuzzleId(TEXT("P5"));
	const FName EndingAId(TEXT("Ending.A"));
	const FName EndingBId(TEXT("Ending.B"));
	const FName EndingCId(TEXT("Ending.C"));
	const FName PowerCutBeat(TEXT("Night4.PowerCut"));
	const FName FinalRevealBeat(TEXT("Night4.FinalReveal"));
	const FName FinalConfrontationBeat(TEXT("Night4.FinalConfrontation"));
	/** §22.4: 선택 직전 한 번. 양쪽을 다 보는 값이 여기서 정해진다. */
	const FName ChoiceOfferedBeat(TEXT("Night4.ChoiceOffered"));
	/** §22.3. 세 줄이 상한이다. 그 이상은 목격이 아니라 목록 낭독이 된다. */
	constexpr int32 ConfrontationReplyLimit = 3;
	/** 한 줄이 화면에 머무는 최소 시간과, 그만큼 뒤로 밀리는 통과. */
	constexpr float ConfrontationReplySeconds = 2.8f;
	constexpr float EntityPassBaseSeconds = 3.35f;
	constexpr float FailureCaptureSeconds = 2.15f;
	constexpr float FailureListingDelaySeconds = 2.2f;
	constexpr float FailureRetryDelaySeconds = 3.2f;

	// Posted beside the existing fourth-floor lift notice, not on top of the
	// 403 shipping labels. Its back face shares the established paper plane.
	const FVector EvictionNoticeLocation(640.0f, -232.0f, 1042.0f);
	/** 요구서가 붙은 낮. 경찰 문자 뒤에 한 번 붙고 저장에 남는다. */
	const FName EvictionPostedBeat(TEXT("Day.EvictionPosted"));
	/**
	 * 403호 안. 요구서 붙이는 소리는 여기서만 들려준다. 밤3 감독의
	 * Unit403Interior와 같은 상자다.
	 */
	const FBox EvictionEarshot(
		FVector(-190.0f, -235.0f, AIGPrologueWorldScene::FourthFloorZ - 20.0f),
		FVector(190.0f, 235.0f, AIGPrologueWorldScene::FourthFloorZ + 230.0f));
	// Both roof controls face the 1.2 m maintenance lane beside the tank.
	const FVector CleaningDrainLocation(-62.0f, 166.0f, 1340.0f);
	const FVector FloatBypassLocation(72.0f, 166.0f, 1340.0f);
	// Ground-floor transfer-pump selector, inside the management booth.
	// A wall-mounted selector above a floor-seated pump assembly in the booth.
	const FVector TransferPumpLocation(64.0f, -170.0f, 112.0f);
	// 선택반 옆에 붙은 절차서. 같은 벽면, 같은 높이.
	const FVector ProcedureSheetLocation(60.08f, -206.0f, 112.0f);
	/** 순서를 틀리면 인터록이 이만큼 선다(§7 P5). */
	constexpr float ControlLockoutSeconds = 10.0f;
	const FVector WallBreakLocation(246.0f, 700.0f, 1300.0f);
	/** 401호 안. 망치 소리에 그 노인이 아래에서 답하는 자리. */
	const FVector Unit401ReplyLocation(-146.0f, -239.0f, 960.0f);
	const FVector EndingALocation(223.0f, 665.0f, 1220.0f);
	const FVector EndingBLocation(165.0f, 765.0f, 1220.0f);
	const FVector CavityVisualOrigin(327.0f, 700.0f, 1200.0f);
	const FVector MokStartLocation(130.0f, 470.0f, 1195.0f);
	const FVector MokExitLocation(130.0f, 865.0f, 1195.0f);
	const FRotator MokRotation(0.0f, -90.0f, 0.0f);
	const FVector EndingHammerStart(218.0f, 660.0f, 1205.0f);
	const FVector EndingHammerRest(286.0f, 708.0f, 1268.0f);
	const FVector EndingPhoneRest(205.0f, 753.0f, 1204.0f);
	const FVector CavityDetailCenter(244.0f, 700.0f, 1290.0f);
	const FVector MokDetailOffset(0.0f, 16.0f, 113.0f);
	const FVector CavityDetailNormal(-1.0f, 0.0f, 0.0f);
	const FVector MokDetailNormal(0.0f, 1.0f, 0.0f);

	/** 5타에서 판이 실제로 내려앉기까지. 손전등이 죽어 있는 사이에 떨어진다. */
	constexpr float WallCollapseDelaySeconds = 0.16f;
	constexpr float WallBrownOutSeconds = 0.42f;
	/** 내려앉은 판에서 가루가 빔 안으로 떠오르기까지. */
	constexpr float WallDustDelaySeconds = 0.45f;
	/**
	 * §8 4-5b. 대치가 끝난 뒤 폰을 보기까지. 암전이 걷히고 앞의 독백(4.5초)이
	 * 다 읽힌 다음이다.
	 */
	constexpr float RecordingLiftDelaySeconds = 5.0f;

	/**
	 * 3타의 정전(§8 4-3). 망치 샘플 꼬리 한가운데이고, 다음 스윙을 모으는
	 * 0.8초보다 짧아서 4타보다 늘 먼저 온다. 차단기는 한 층 아래, 5층으로
	 * 오르는 계단 들머리 옆 벽에 있다. 딸깍은 계단실을 타고 올라온다.
	 */
	constexpr float PowerCutDelaySeconds = 0.7f;
	const FVector AnnexBreakerLocation(-236.0f, -200.0f, 1080.0f);
	/** 별채 천장 등. 전원이 끊기는 순간 안정기가 한 번 튄다. */
	const FVector AnnexCeilingLampLocation(0.0f, 700.0f, 1420.0f);
	/**
	 * 내린 사람이 내려가는 길. 5층 계단 첫 단, 4층 복도 끝, 계단탑 동쪽 계단의
	 * 첫 단과 그 아래 셋째 단. 철판·콘크리트·철판·철판이다.
	 */
	const FVector PowerCutStepLocations[] =
	{
		FVector(-277.5f, -200.0f, 942.0f),
		FVector(-305.0f, -268.0f, 906.0f),
		FVector(-407.5f, -222.0f, 886.0f),
		FVector(-407.5f, -160.0f, 836.0f),
	};
	constexpr float PowerCutStepVolumes[] = {0.9f, 0.72f, 0.58f, 0.44f};
	constexpr int32 PowerCutStepCount = 4;
	constexpr float PowerCutFirstStepSeconds = 0.8f;
	constexpr float PowerCutStepIntervalSeconds = 0.5f;

	/** 4타에 401호가 답하기까지. 망치의 1.1초 꼬리 뒤, 알아듣고 손을 드는 시간. */
	constexpr float Unit401ReplyDelaySeconds = 1.25f;
	/**
	 * 벽이 열리기 직전(§2.3 넷째 밤). 4타에서 401호의 대답이 걷힌 뒤 5층 철문 밖에서 부른다.
	 * 첫 말은 노크 2.4초 뒤이고, 벽 안의 둘, 쉬고, 하나는 그 말이 다 읽힐 무렵 온다. 그때까지
	 * 손이 저려 망치를 못 든다. 부름이 오지 못했으면 벽만 조금 뒤에 답한다.
	 */
	constexpr float HammerCallDelaySeconds = 3.6f;
	constexpr float WallAnswerAfterCallSeconds = 4.8f;
	constexpr float WallAnswerWithoutCallSeconds = 1.4f;

	/** 목한수의 보폭. 뒷걸음이라 좁다. 첫 발은 몸이 뒤로 빠지자마자 나온다. */
	constexpr float MokStepStride = 48.0f;
	constexpr float MokFirstStepHeadStart = 24.0f;

	/**
	 * 마지막 줄이 사라지고 그가 들어오기까지. 줄이 아직 화면에 있으면 이만큼씩
	 * 더 기다리되, 6초를 넘기지는 않는다.
	 */
	constexpr float EntityPassAfterLinesSeconds = 0.3f;
	constexpr float EntityPassWaitStepSeconds = 0.25f;
	constexpr int32 EntityPassMaximumWaits = 24;

	/** 인터록이 선 배관이 잦아드는 세 번. 6초 뒤로는 조용하다. */
	constexpr int32 ControlSettleKnockCount = 3;
	constexpr float ControlSettleKnockSeconds[] = {1.2f, 3.6f, 6.0f};
	constexpr float ControlSettleKnockVolumes[] = {0.65f, 0.45f, 0.28f};

	/** P5 물길. 펌프가 도는 만큼 관이 차오른다. */
	const FVector PumpDischargeFlowLocation(75.0f, -170.0f, 250.0f);
	constexpr float RiserFlowFadeInSeconds = 1.8f;

	/**
	 * 벽을 연 채 되풀이된 밤4에서 리빌을 다시 여는 거리. 공동 앞 방 안이다.
	 * 높이 허용은 5층 바닥 위의 몸만 받는다 — 4층 복도는 3미터 아래다.
	 */
	constexpr float RepeatRevealReachCentimeters = 320.0f;
	constexpr float RepeatRevealFloorToleranceCentimeters = 200.0f;

	/** 프로브와 야간 캡처는 상태를 한 프레임에 밟는다. 연출 지연을 건너뛴다. */
	static bool IsImmediateFinaleRun()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreyboxProbe"))
			|| FParse::Param(FCommandLine::Get(), TEXT("IGNightCapture"));
	}

	/** 새벽 디렉터. 캡처 투어처럼 시간이 없는 무대에는 없다. */
	static AIGNightPhaseDirector* FindNightPhase(const UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		TActorIterator<AIGNightPhaseDirector> It(World);
		return It ? *It : nullptr;
	}

	/**
	 * 엔딩 B의 기다림(§9 B). 그녀가 앉아서 두드리는 자리다. 열린 자리 바로
	 * 옆, 그가 마지막에 쓰러져 있던 높이.
	 */
	const FVector VigilKnockLocation(246.0f, 760.0f, 1235.0f);
	/**
	 * 복도 끝, 공동 너머. 막간에서 깨어난 뒤의 먼 노크와 엔딩 B의 대답이 같은
	 * 자리에서 온다. 밤 5 슬롯이 돌려주는 것도 이 대답이다.
	 */
	const FVector CorridorEndKnockLocation(360.0f, 930.0f, 1390.0f);
	constexpr float VigilReplyDelaySeconds = 2.4f;
	constexpr int32 VigilReplyLimit = 2;
	/** 대답을 한 번도 못 받아도 새벽은 온다. 두드리지 않고 곁에 있는 것도 이 선택이다. */
	constexpr float VigilUnansweredSeconds = 30.0f;
	/** 첫 대답 뒤에는 한 번 더 대답할 시간을, 마지막 대답 뒤에는 정적을 둔다. */
	constexpr float VigilAfterFirstReplySeconds = 9.0f;
	constexpr float VigilAfterLastReplySeconds = 5.5f;
	/** 05:30. 침묵이 걷히고 잠금이 풀리기까지, 풀린 뒤 전화가 걸리기까지. */
	constexpr float VigilLatchDelaySeconds = 0.8f;
	constexpr float VigilRingbackDelaySeconds = 1.1f;

	static FRotator DetailCardRotation(
		const FVector& ScreenRight,
		const FVector& SurfaceNormal)
	{
		// Engine Plane uses local X/Y as image axes and local Z as its normal.
		// Supplying screen-right as X makes the computed Y point downward, so
		// the source bitmap remains upright without a negative component scale.
		return FRotationMatrix::MakeFromXZ(
			ScreenRight,
			SurfaceNormal).Rotator();
	}

	static const FVector& RevealTarget(const int32 Stage)
	{
		static const FVector Targets[] = {
			CavityVisualOrigin + FVector(-20.0f, 0.0f, 130.0f),
			CavityVisualOrigin + FVector(-23.0f, 0.0f, 95.0f),
			CavityVisualOrigin + FVector(-43.0f, 22.0f, 15.0f),
		};
		return Targets[FMath::Clamp(Stage, 0, 2)];
	}

	static float RequiredAttentionSeconds(const int32 Stage)
	{
		static constexpr float Seconds[] = {1.8f, 2.0f, 2.2f};
		return Seconds[FMath::Clamp(Stage, 0, 2)];
	}

	static const TArray<FName>& SafeOrder()
	{
		static const TArray<FName> Order = {
			CleaningDrainId,
			FloatBypassId,
			TransferPumpId,
		};
		return Order;
	}
}

AIGMissingFloorNightFourDirector::AIGMissingFloorNightFourDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void AIGMissingFloorNightFourDirector::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bValveMotionActive)
	{
		UpdateRoofValveMotion(DeltaSeconds);
	}
	if (bFinalRevealActive)
	{
		UpdateCavityReveal(DeltaSeconds);
	}
	if (bMokRetreatActive)
	{
		UpdateMokRetreat(DeltaSeconds);
	}
	if (bEndingHammerMoving)
	{
		UpdateEndingHammer(DeltaSeconds);
	}
	UpdateFinaleDetailLayers();
	if (!bFinalRevealActive && !bMokRetreatActive && !bEndingHammerMoving && !bValveMotionActive)
	{
		SetActorTickEnabled(
			bCavityPresentationVisible || bMokPresentationVisible);
	}
}

FVector AIGMissingFloorNightFourDirector::GetWallBreakLocation()
{
	return IGNightFour::WallBreakLocation;
}

FVector AIGMissingFloorNightFourDirector::GetUnit401ReplyLocation()
{
	return IGNightFour::Unit401ReplyLocation;
}

bool AIGMissingFloorNightFourDirector::Configure(AIGPrologueWorldScene* InScene)
{
	UWorld* World = GetWorld();
	if (!World || !InScene)
	{
		return false;
	}
	Scene = InScene;
	for (TActorIterator<AIGListenerEntity> It(World); It; ++It)
	{
		Listener = *It;
		Listener->OnPlayerCaptured.AddUObject(
			this, &AIGMissingFloorNightFourDirector::HandleNightFourCapture);
		break;
	}

	UStaticMesh* CubeMesh =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* CylinderMesh =
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (!CubeMesh || !CylinderMesh)
	{
		return false;
	}

	UStaticMesh* LargeValveMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_P3ValveWheelLarge.SM_P3ValveWheelLarge"));
	UStaticMesh* SmallValveMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_P3ValveWheelSmall.SM_P3ValveWheelSmall"));
	UMaterialInterface* MetalMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Prototype/Materials/M_P3CabinetMetalUV.M_P3CabinetMetalUV"));
	UMaterialInterface* PaperMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_PaperClean.M_PaperClean"));
	UMaterialInterface* DarkMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_PlasticDark.M_PlasticDark"));
	UMaterialInterface* RedMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_SnackRed.M_SnackRed"));
	UMaterialInterface* ScreenMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_ScreenGlow.M_ScreenGlow"));

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	auto SpawnEvidence = [World, &SpawnParameters](
		const TCHAR* Name,
		const FVector& Location,
		const FRotator& Rotation) -> AIGMissingFloorEvidence*
	{
		SpawnParameters.Name = FName(Name);
		return World->SpawnActor<AIGMissingFloorEvidence>(
			AIGMissingFloorEvidence::StaticClass(),
			FTransform(Rotation, Location),
			SpawnParameters);
	};
	EquipmentVisuals.Reset();
	auto AddEquipment = [this, World](
		UStaticMesh* Mesh,
		UMaterialInterface* Material,
		const FName Name,
		const FVector& Location,
		const FVector& LocalSize,
		const FRotator& Rotation = FRotator::ZeroRotator) -> UStaticMeshComponent*
	{
		if (!Mesh)
		{
			return nullptr;
		}
		UStaticMeshComponent* Component =
			NewObject<UStaticMeshComponent>(this, Name);
		Component->SetStaticMesh(Mesh);
		if (Material)
		{
			Component->SetMaterial(0, Material);
		}
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetCastShadow(true);
		Component->SetWorldTransform(FTransform(
			Rotation,
			Location,
			LocalSize / 100.0f));
		AddInstanceComponent(Component);
		Component->RegisterComponentWithWorld(World);
		EquipmentVisuals.Add(Component);
		return Component;
	};

	EvictionNotice = SpawnEvidence(
		TEXT("MissingFloorEvictionNotice"),
		IGNightFour::EvictionNoticeLocation,
		FRotator::ZeroRotator);
	CleaningDrain = SpawnEvidence(
		TEXT("MissingFloorCleaningDrain"),
		IGNightFour::CleaningDrainLocation,
		FRotator(0.0f, 0.0f, 90.0f));
	FloatBypass = SpawnEvidence(
		TEXT("MissingFloorFloatBypass"),
		IGNightFour::FloatBypassLocation,
		FRotator(0.0f, 0.0f, 90.0f));
	TransferPump = SpawnEvidence(
		TEXT("MissingFloorTransferPump"),
		IGNightFour::TransferPumpLocation,
		FRotator(0, 90, 0));
	WallBreakTarget = SpawnEvidence(
		TEXT("MissingFloorWallBreakTarget"),
		IGNightFour::WallBreakLocation,
		FRotator::ZeroRotator);
	EndingATarget = SpawnEvidence(
		TEXT("MissingFloorEndingATarget"),
		IGNightFour::EndingALocation,
		FRotator::ZeroRotator);
	EndingBTarget = SpawnEvidence(
		TEXT("MissingFloorEndingBTarget"),
		IGNightFour::EndingBLocation,
		FRotator::ZeroRotator);
	if (!EvictionNotice || !CleaningDrain || !FloatBypass || !TransferPump
		|| !WallBreakTarget || !EndingATarget || !EndingBTarget)
	{
		return false;
	}

	EvictionNotice->Configure(
		CubeMesh,
		PaperMaterial,
		FVector(21.0f, 1.0f, 29.7f),
		NSLOCTEXT("IGMissingFloor", "EvictionNoticePrompt", "퇴거 통보문 읽기"),
		// 종이에서 읽는 것은 사유와 기한이다(§8 3-8). 아침 일곱 시 보수와 석고보드는
		// 이어서 담당 수사관에게 보내는 사진 문자가 말한다 — 그레이박스 감독의
		// 신고 문자 줄기가 이 종이를 읽은 뒤에 민다.
		NSLOCTEXT(
			"IGMissingFloor",
			"EvictionNoticeThought",
			"이번 주 안에 나가라고? 내가 밤에 올라간 걸 아는 거야."),
		EIGMissingFloorTruth::StillCoveringIt,
		EIGMissingFloorSource::EvictionWarning,
		0.0f,
		0.02f);
	EvictionNotice->OnExamined.AddUObject(
		this, &AIGMissingFloorNightFourDirector::HandleEvictionNotice);

	CleaningDrain->Configure(
		LargeValveMesh ? LargeValveMesh : CylinderMesh,
		MetalMaterial,
		LargeValveMesh ? FVector::ZeroVector : FVector(18.0f, 18.0f, 5.0f),
		NSLOCTEXT("IGMissingFloor", "CleaningDrainPrompt", "세척 배수 밸브 열기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		0.8f,
		0.18f);
	CleaningDrain->OnExamined.AddUObject(
		this, &AIGMissingFloorNightFourDirector::HandleCleaningDrain);

	FloatBypass->Configure(
		SmallValveMesh ? SmallValveMesh : CylinderMesh,
		MetalMaterial,
		SmallValveMesh ? FVector::ZeroVector : FVector(12.0f, 12.0f, 4.0f),
		NSLOCTEXT("IGMissingFloor", "FloatBypassPrompt", "우회 밸브 열기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		0.8f,
		0.18f);
	FloatBypass->OnExamined.AddUObject(
		this, &AIGMissingFloorNightFourDirector::HandleFloatBypass);
	int32 ValveIndex = 0;
	for (AIGMissingFloorEvidence* Valve : {CleaningDrain.Get(), FloatBypass.Get()})
	{
		ValveClosedTransforms[ValveIndex++] = Valve->GetActorTransform();
		Valve->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	}

	TransferPump->Configure(
		LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_PumpControlPanel.SM_PumpControlPanel")),
		nullptr,
		FVector::ZeroVector,
		NSLOCTEXT(
			"IGMissingFloor", "TransferPumpPrompt", "이송 펌프 수동으로 돌리기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		1.0f,
		0.22f);
	TransferPump->OnExamined.AddUObject(
		this, &AIGMissingFloorNightFourDirector::HandleTransferPump);

	// 설비의 고장 기록으로 관로 순서를 짐작하게 한다.
	SpawnParameters.Name = TEXT("MissingFloorPumpProcedureSheet");
	ProcedureSheet = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(),
		FTransform(FRotator(0, 90, 0), IGNightFour::ProcedureSheetLocation),
		SpawnParameters);
	if (!ProcedureSheet)
	{
		return false;
	}
	ProcedureSheet->ConfigurePrototypeVisuals(
		CubeMesh, LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Prototype/Materials/M_PumpProcedure.M_PumpProcedure")), FVector(21.0f, 0.08f, 29.7f));
	ProcedureSheet->SetInteractionPrompt(
		NSLOCTEXT("IGMissingFloor", "ProcedureSheetPrompt", "저수조 점검 메모"));
	ProcedureSheet->SetNoteText(
		NSLOCTEXT("IGMissingFloor", "ProcedureSheetTitle", "저수조 점검 메모 (2019.03)"),
		{
			NSLOCTEXT("IGMissingFloor", "ProcedureSheet1", "물이 넘친 날: 배수는 잠겨 있었고, 우회만 열려 있었음."),
			NSLOCTEXT("IGMissingFloor", "ProcedureSheet2", "펌프가 멎은 날: 배수만 열고 돌림. 우회관에 물이 안 찼음."),
			NSLOCTEXT("IGMissingFloor", "ProcedureSheet3", "세척할 때는 자동 수위 조절을 쓰지 말 것."),
			FText::GetEmpty(),
			NSLOCTEXT("IGMissingFloor", "ProcedureSheet4", "고장등이 켜지면 손대지 말고 10초 정도 기다리세요."),
			NSLOCTEXT("IGMissingFloor", "ProcedureSheet5", "배관이 조용해지고 등이 꺼진 뒤 다시 돌리면 됩니다."),
		});

	// 관로를 한 메시로 굽는다. 배수구·입수구가 탱크와 지면까지 이어진다.
	AddEquipment(LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_RoofCleaningPipework.SM_RoofCleaningPipework")),
		nullptr, TEXT("NightFourRoofPipework"), FVector(0, 0, 1200), FVector(100));
	for (const bool bDrain : {true, false})
	{
		UStaticMeshComponent* Plate = AddEquipment(LoadObject<UStaticMesh>(nullptr, bDrain
			? TEXT("/Game/Meshes/SM_RoofDrainPlate.SM_RoofDrainPlate")
			: TEXT("/Game/Meshes/SM_RoofBypassPlate.SM_RoofBypassPlate")),
			nullptr, bDrain ? TEXT("NightFourDrainLabel") : TEXT("NightFourBypassLabel"),
			FVector(bDrain ? -62.f : 72.f, 142.9f, 1318.f), FVector(100), FRotator(0, 180, 0));
		if (Plate) { Plate->SetCastShadow(false); Plate->SetCullDistance(1000); }
	}

	// 실물 사진에서 확인한 방열판·전장함·케이싱을 가진 소형 펌프.
	// 흡입관은 벽으로, 토출관은 천장으로 이어지며 공중에서 끝나지 않는다.
	AddEquipment(
		CubeMesh, DarkMaterial, TEXT("NightFourPumpBase"),
		FVector(87.5f, -154.0f, 4.0f), FVector(44.0f, 38.0f, 8.0f));
	AddEquipment(LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_BoothPump.SM_BoothPump")),
		nullptr, TEXT("NightFourPumpBody"), FVector(82.5f, -150.0f, 8.0f), FVector(100));
	// 조작반 아래에서 꺾어 올린다. 배관과 지지대는 한 메시로 묶었다.
	AddEquipment(LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_BoothPumpPipework.SM_BoothPumpPipework")),
		nullptr, TEXT("NightFourPumpPipework"), FVector(75.0f, -170.0f, 0.0f), FVector(100));
	const FVector PanelOrigin = TransferPump->GetActorLocation();
	PumpSelector = AddEquipment(LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_PumpSelector.SM_PumpSelector")),
		nullptr, TEXT("NightFourPumpSelector"), PanelOrigin + FVector(5.4f, 0, -7.7f), FVector(100), FRotator(0, 90, 0));
	if (PumpSelector) { PumpSelector->SetMobility(EComponentMobility::Movable); }
	UMaterialInterface* IndicatorMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Prototype/Materials/M_PumpIndicator.M_PumpIndicator"));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UStaticMeshComponent* Lens = AddEquipment(CylinderMesh, IndicatorMaterial,
			FName(*FString::Printf(TEXT("NightFourPumpLens%d"), Index)),
			PanelOrigin + FVector(5.3f, (1 - Index) * 8.2f, 7.1f), FVector(2.1f, 2.1f, .45f), FRotator(90, 0, 0));
		if (Lens)
		{
			UMaterialInstanceDynamic* Lamp = Lens->CreateAndSetMaterialInstanceDynamic(0);
			Lamp->SetVectorParameterValue(TEXT("Tint"), Index == 0 ? FLinearColor(.28f,.30f,.25f)
				: Index == 1 ? FLinearColor(.015f,.28f,.06f) : FLinearColor(.38f,.016f,.008f));
			PumpLamps.Add(Lamp);
		}
	}

	WallBreakTarget->Configure(
		CubeMesh,
		nullptr,
		FVector(3.0f, 92.0f, 168.0f),
		NSLOCTEXT("IGMissingFloor", "WallBreakPrompt", "벽을 망치로 두드리기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		// §18.4 망치 스윙 차징 0.8초. 뒤의 1.0은 §5.1 소음 크기다 — 같은
		// 자리에 붙어 있어서 한 번 헷갈리면 조용히 어긋난다.
		0.8f,
		1.0f);
	WallBreakTarget->OnExamined.AddUObject(
		this, &AIGMissingFloorNightFourDirector::HandleWallStrike);
	// The real gypsum panel remains the visible surface. This trace receiver is
	// invisible so it cannot z-fight or read as a second wall laid over it.
	WallBreakTarget->GetPresentationMesh()->SetVisibility(false, true);

	EndingATarget->Configure(
		CubeMesh,
		nullptr,
		FVector(18.0f, 24.0f, 5.0f),
		NSLOCTEXT(
			"IGMissingFloor", "EndingAPrompt", "튜닝 해머를 오빠 곁에 두고 물러나기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		1.2f,
		0.03f);
	EndingATarget->OnExamined.AddUObject(
		this, &AIGMissingFloorNightFourDirector::HandleEndingA);

	EndingBTarget->Configure(
		CubeMesh,
		nullptr,
		FVector(42.0f, 42.0f, 4.0f),
		NSLOCTEXT(
			"IGMissingFloor", "EndingBPrompt", "녹음 끄고 곁에 앉아 대답하기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		1.2f,
		0.03f);
	EndingBTarget->OnExamined.AddUObject(
		this, &AIGMissingFloorNightFourDirector::HandleEndingB);

	if (!BuildFinaleVisuals())
	{
		return false;
	}

	RefreshPresentation();
	return ValidateFixtures();
}

bool AIGMissingFloorNightFourDirector::BuildFinaleVisuals()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	auto LoadMesh = [](const TCHAR* Name) -> UStaticMesh*
	{
		return LoadObject<UStaticMesh>(
			nullptr,
			*FString::Printf(TEXT("/Game/Meshes/%s.%s"), Name, Name));
	};
	auto LoadMaterial = [](const TCHAR* Name) -> UMaterialInterface*
	{
		return LoadObject<UMaterialInterface>(
			nullptr,
			*FString::Printf(
				TEXT("/Game/Prototype/Materials/%s.%s"), Name, Name));
	};

	UStaticMesh* ClothingMesh = LoadMesh(TEXT("SM_FinalCavityClothingShell"));
	UStaticMesh* BoneMesh = LoadMesh(TEXT("SM_FinalCavityBoneInsert"));
	UStaticMesh* TarpMesh = LoadMesh(TEXT("SM_FinalCavityTarp"));
	UStaticMesh* CasterMesh = LoadMesh(TEXT("SM_FinalCavityBrokenCaster"));
	UStaticMesh* MokWorkwearMesh = LoadMesh(TEXT("SM_MokHansooWorkwear"));
	UStaticMesh* MokHeadHandsMesh = LoadMesh(TEXT("SM_MokHansooHeadHands"));
	UStaticMesh* MokBoardMesh = LoadMesh(TEXT("SM_MokHansooGypsumBoard"));
	// 유해와 목한수는 베이크된 3D 메시를 쓴다. 방수포와 카트 바퀴는 별도 소품이다.
	// 인물 앞에 정면 카드를 겹치지 않는다. 원점은 바닥 중심, 정면은 -Y다.
	UStaticMesh* CavityFigureMesh = LoadMesh(TEXT("SM_FinalCavityRemains"));
	UStaticMesh* MokFigureMesh = LoadMesh(TEXT("SM_MokHansooFigure"));
	UStaticMesh* TuningHammerMesh = LoadMesh(TEXT("SM_TuningHammer"));
	UStaticMesh* PhoneMesh = LoadMesh(TEXT("SM_CrackedPhone"));
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));

	UMaterialInterface* DryClothMaterial = LoadMaterial(TEXT("M_ConcreteDark"));
	UMaterialInterface* BoneMaterial = LoadMaterial(TEXT("M_PaperOld"));
	UMaterialInterface* TarpMaterial = LoadMaterial(TEXT("M_PlasticDark"));
	UMaterialInterface* MetalMaterial = LoadMaterial(TEXT("M_P3CabinetMetalUV"));
	UMaterialInterface* WorkwearMaterial = LoadMaterial(TEXT("M_PlasticDark"));
	UMaterialInterface* BoardMaterial = LoadMaterial(TEXT("M_MissingFloorPlaster_XY"));
	UMaterialInterface* CavityDetailMaterial = LoadMaterial(
		TEXT("M_SpriteFinalCavity"));
	UMaterialInterface* MokDetailMaterial = LoadMaterial(
		TEXT("M_SpriteMokFinalUpper"));

	if (!ClothingMesh || !BoneMesh || !TarpMesh || !CasterMesh
		|| !MokWorkwearMesh || !MokHeadHandsMesh || !MokBoardMesh
		|| !TuningHammerMesh || !PhoneMesh || !DryClothMaterial
		|| !BoneMaterial || !TarpMaterial || !MetalMaterial
		|| !WorkwearMaterial || !BoardMaterial || !PlaneMesh
		|| !CavityDetailMaterial || !MokDetailMaterial)
	{
		return false;
	}

	auto AddVisual = [this, World](
		const FName Name,
		UStaticMesh* Mesh,
		UMaterialInterface* Material,
		const FVector& Location,
		const FRotator& Rotation) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(this, Name);
		if (!Visual)
		{
			return nullptr;
		}
		Visual->SetStaticMesh(Mesh);
		if (Material)
		{
			Visual->SetMaterial(0, Material);
		}
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->SetGenerateOverlapEvents(false);
		Visual->SetCanEverAffectNavigation(false);
		Visual->SetCastShadow(true);
		Visual->SetWorldTransform(FTransform(Rotation, Location));
		Visual->SetVisibility(false, true);
		Visual->SetHiddenInGame(true, true);
		AddInstanceComponent(Visual);
		Visual->RegisterComponentWithWorld(World);
		return Visual;
	};

	bCavityFigureAuthored = CavityFigureMesh != nullptr;
	bMokFigureAuthored = MokFigureMesh != nullptr;

	CavityRevealVisuals.Reset();
	if (bCavityFigureAuthored)
	{
		// 절차 셸은 -X를 보고 앉아 있었다(디테일 법선 (-1,0,0)). 정면 -Y인
		// 생성 메시는 yaw -90으로 같은 쪽을 본다.
		CavityRevealVisuals.Add(AddVisual(
			TEXT("FinalCavityRemains"), CavityFigureMesh, nullptr,
			IGNightFour::CavityVisualOrigin, FRotator(0.0f, -90.0f, 0.0f)));
		// 접은 방수포는 발밑에 눕힌다. 두께는 2.7cm이며 공동의 뒷벽 안쪽에 맞춘다.
		UStaticMeshComponent* FoldedTarp = AddVisual(
			TEXT("FinalCavityTarp"), TarpMesh, TarpMaterial,
			FVector(390.0f, 690.0f, 1200.4f), FRotator(90.0f, 0.0f, 0.0f));
		if (FoldedTarp)
		{
			FoldedTarp->SetWorldScale3D(FVector(0.12f, 0.8f, 0.65f));
		}
		CavityRevealVisuals.Add(FoldedTarp);
		CavityRevealVisuals.Add(AddVisual(
			TEXT("FinalCavityCaster"), CasterMesh, MetalMaterial,
			IGNightFour::CavityVisualOrigin + FVector(0.0f, 0.0f, -4.35f), FRotator::ZeroRotator));
	}
	else
	{
		CavityRevealVisuals.Add(AddVisual(
			TEXT("FinalCavityClothing"), ClothingMesh, DryClothMaterial,
			IGNightFour::CavityVisualOrigin, FRotator::ZeroRotator));
		CavityRevealVisuals.Add(AddVisual(
			TEXT("FinalCavityBoneInsert"), BoneMesh, BoneMaterial,
			IGNightFour::CavityVisualOrigin, FRotator::ZeroRotator));
		CavityRevealVisuals.Add(AddVisual(
			TEXT("FinalCavityTarp"), TarpMesh, TarpMaterial,
			IGNightFour::CavityVisualOrigin, FRotator::ZeroRotator));
		CavityRevealVisuals.Add(AddVisual(
			TEXT("FinalCavityCaster"), CasterMesh, MetalMaterial,
			IGNightFour::CavityVisualOrigin, FRotator::ZeroRotator));
	}

	MokVisuals.Reset();
	if (bMokFigureAuthored)
	{
		// 절차 작업복은 -X가 정면이라 MokRotation(yaw -90)으로 +Y를 봤다.
		// 정면 -Y인 생성 메시는 yaw 180으로 같은 +Y를 본다.
		MokVisuals.Add(AddVisual(
			TEXT("MokHansooFigure"), MokFigureMesh, nullptr,
			IGNightFour::MokStartLocation, GetMokRotation()));
	}
	else
	{
		MokVisuals.Add(AddVisual(
			TEXT("MokHansooWorkwear"), MokWorkwearMesh, WorkwearMaterial,
			IGNightFour::MokStartLocation, IGNightFour::MokRotation));
		MokVisuals.Add(AddVisual(
			TEXT("MokHansooHeadHands"), MokHeadHandsMesh, BoneMaterial,
			IGNightFour::MokStartLocation, IGNightFour::MokRotation));
		MokVisuals.Add(AddVisual(
			TEXT("MokHansooGypsumBoard"), MokBoardMesh, BoardMaterial,
			IGNightFour::MokStartLocation, IGNightFour::MokRotation));
	}

	CavityDetailCard = nullptr;
	MokDetailCard = nullptr;
	if (!bCavityFigureAuthored)
	{
		CavityDetailCard = AddVisual(
			TEXT("FinalCavityDetailCard"), PlaneMesh, CavityDetailMaterial,
			IGNightFour::CavityDetailCenter,
			IGNightFour::DetailCardRotation(
				FVector(0.0f, 1.0f, 0.0f),
				IGNightFour::CavityDetailNormal));
		if (CavityDetailCard)
		{
			CavityDetailCard->SetWorldScale3D(FVector(1.19f, 1.78f, 1.0f));
			CavityDetailCard->SetCastShadow(false);
		}
	}
	if (!bMokFigureAuthored)
	{
		MokDetailCard = AddVisual(
			TEXT("MokHansooDetailCard"), PlaneMesh, MokDetailMaterial,
			IGNightFour::MokStartLocation + IGNightFour::MokDetailOffset,
			IGNightFour::DetailCardRotation(
				FVector(1.0f, 0.0f, 0.0f),
				IGNightFour::MokDetailNormal));
		if (MokDetailCard)
		{
			MokDetailCard->SetWorldScale3D(FVector(1.21f, 1.82f, 1.0f));
			MokDetailCard->SetCastShadow(false);
		}
	}

	EndingHammerVisual = AddVisual(
		TEXT("NightFourEndingHammer"), TuningHammerMesh, MetalMaterial,
		IGNightFour::EndingHammerStart, FRotator(0.0f, 18.0f, -76.0f));
	EndingPhoneVisual = AddVisual(
		TEXT("NightFourEndingPhone"), PhoneMesh, WorkwearMaterial,
		IGNightFour::EndingPhoneRest, FRotator(0.0f, -8.0f, 0.0f));

	return HasFinaleFigures()
		&& EndingHammerVisual
		&& EndingPhoneVisual;
}

bool AIGMissingFloorNightFourDirector::HasFinaleFigures() const
{
	// 유해·방수포·바퀴를 확인한다. 구형 인물의 경우에만 정면 보조 카드가 필요하다.
	const bool bCavityComplete = bCavityFigureAuthored
		? CavityRevealVisuals.Num() == 3
		: CavityRevealVisuals.Num() == 4 && CavityDetailCard != nullptr;
	const bool bMokComplete = bMokFigureAuthored
		? MokVisuals.Num() == 1
		: MokVisuals.Num() == 3 && MokDetailCard != nullptr;
	return bCavityComplete
		&& !CavityRevealVisuals.Contains(nullptr)
		&& bMokComplete
		&& !MokVisuals.Contains(nullptr);
}

void AIGMissingFloorNightFourDirector::SetCavityRevealVisible(
	const bool bVisible)
{
	bCavityPresentationVisible = bVisible;
	for (UStaticMeshComponent* Visual : CavityRevealVisuals)
	{
		if (Visual)
		{
			Visual->SetHiddenInGame(!bVisible, true);
			Visual->SetVisibility(bVisible, true);
		}
	}
	UpdateFinaleDetailLayers();
}

FRotator AIGMissingFloorNightFourDirector::GetMokRotation() const
{
	// 생성 메시의 정면은 -Y다. 등장·캡처·퇴장에도 같은 기준을 쓴다.
	return bMokFigureAuthored ? FRotator(0.0f, 180.0f, 0.0f) : IGNightFour::MokRotation;
}

void AIGMissingFloorNightFourDirector::SetMokVisible(const bool bVisible)
{
	bMokPresentationVisible = bVisible;
	for (UStaticMeshComponent* Visual : MokVisuals)
	{
		if (Visual)
		{
			Visual->SetHiddenInGame(!bVisible, true);
			Visual->SetVisibility(bVisible, true);
		}
	}
	UpdateFinaleDetailLayers();
}

void AIGMissingFloorNightFourDirector::UpdateFinaleDetailLayers()
{
	FVector ViewLocation = FVector::ZeroVector;
	bool bHasView = false;
	if (APlayerController* Controller = GetWorld()
		? GetWorld()->GetFirstPlayerController()
		: nullptr)
	{
		FRotator ViewRotation;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		bHasView = true;
	}

	auto ShouldUseLayer = [bHasView, &ViewLocation](
		const FVector& Origin,
		const FVector& Normal,
		const bool bWorldVisible) -> bool
	{
		if (!bWorldVisible || !bHasView)
		{
			return false;
		}
		const FVector ToView = ViewLocation - Origin;
		const float Distance = ToView.Size();
		return Distance > 105.0f
			&& Distance < 360.0f
			&& FVector::DotProduct(
				Normal, ToView.GetSafeNormal()) > 0.68f;
	};

	if (CavityDetailCard)
	{
		const bool bShowDetail = ShouldUseLayer(
			IGNightFour::CavityDetailCenter,
			IGNightFour::CavityDetailNormal,
			bCavityPresentationVisible);
		CavityDetailCard->SetHiddenInGame(!bShowDetail, true);
		CavityDetailCard->SetVisibility(bShowDetail, true);
		for (UStaticMeshComponent* Visual : CavityRevealVisuals)
		{
			if (Visual)
			{
				const bool bShowShell =
					bCavityPresentationVisible && !bShowDetail;
				Visual->SetHiddenInGame(!bShowShell, true);
				Visual->SetVisibility(bShowShell, true);
				Visual->SetCastHiddenShadow(
					bCavityPresentationVisible && bShowDetail);
			}
		}
	}
	if (MokDetailCard)
	{
		const FVector MokLocation = MokVisuals.Num() > 0 && MokVisuals[0]
			? MokVisuals[0]->GetComponentLocation()
			: IGNightFour::MokStartLocation;
		const FVector CardCenter = MokLocation + IGNightFour::MokDetailOffset;
		MokDetailCard->SetWorldLocation(CardCenter);
		const bool bShowDetail = ShouldUseLayer(
			CardCenter,
			IGNightFour::MokDetailNormal,
			bMokPresentationVisible);
		MokDetailCard->SetHiddenInGame(!bShowDetail, true);
		MokDetailCard->SetVisibility(bShowDetail, true);
	}

	if (bCavityPresentationVisible || bMokPresentationVisible)
	{
		SetActorTickEnabled(true);
	}
}

void AIGMissingFloorNightFourDirector::SetFinaleCapturePreview(
	const bool bShowCavity,
	const bool bShowMok)
{
	if (bShowCavity || bShowMok)
	{
		for (TActorIterator<AIGPlayerCharacter> It(GetWorld()); It; ++It)
		{
			if (UIGFlashlightComponent* Flashlight = It->GetFlashlight())
			{
				Flashlight->SetOn(true);
			}
			break;
		}
	}
	SetCavityRevealVisible(bShowCavity);
	for (UStaticMeshComponent* Visual : MokVisuals)
	{
		if (Visual)
		{
			Visual->SetWorldLocationAndRotation(
				IGNightFour::MokStartLocation,
				GetMokRotation());
		}
	}
	SetMokVisible(bShowMok);
	UpdateFinaleDetailLayers();
}

void AIGMissingFloorNightFourDirector::AbortFailureBlackout(const TCHAR* Reason)
{
	UWorld* World = GetWorld();
	AIGPlayerCharacter* Character = FailurePlayer.Get();
	APlayerController* Controller = Character
		? Cast<APlayerController>(Character->GetController())
		: nullptr;
	if (!Controller && World)
	{
		Controller = World->GetFirstPlayerController();
	}
	if (Controller && Controller->PlayerCameraManager)
	{
		Controller->PlayerCameraManager->StopCameraFade();
	}
	if (Character)
	{
		if (UCharacterMovementComponent* Movement =
			Character->GetCharacterMovement())
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
	UE_LOG(
		LogIndieGame,
		Warning,
		TEXT("IG_ENDING_C aborted blackout: %s (controller=%s)"),
		Reason,
		Controller ? TEXT("yes") : TEXT("none"));
}

void AIGMissingFloorNightFourDirector::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	// 엔딩 C는 이동을 잠그고 bHoldWhenFinished로 암전을 건 뒤 타이머로 푼다.
	// ResetFinaleTimers()가 바로 그 타이머를 지우므로, 시퀀스 도중에 이
	// 디렉터가 사라지면 푸는 쪽이 사라진다.
	if (bFailureEndingActive && EndPlayReason != EEndPlayReason::LevelTransition
		&& EndPlayReason != EEndPlayReason::EndPlayInEditor
		&& EndPlayReason != EEndPlayReason::Quit)
	{
		AbortFailureBlackout(
			TEXT("director destroyed during the failure ending"));
	}
	bFailureEndingActive = false;
	ResetFinaleTimers();
	GetWorldTimerManager().ClearTimer(WallOpenTimer);
	GetWorldTimerManager().ClearTimer(WallDustTimer);
	bWallOpeningPending = false;
	// 기다리는 도중에 사라지면 예약된 노크와 새벽 소리도 걷는다.
	EndEndingBVigil();
	if (AIGListenerEntity* ListenerActor = Listener.Get())
	{
		ListenerActor->OnPlayerCaptured.RemoveAll(this);
	}
	if (UWorld* World = GetWorld())
	{
		if (WaterMaskHumHandle != INDEX_NONE)
		{
			if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
			{
				Noise->UnregisterHumSource(WaterMaskHumHandle);
			}
			WaterMaskHumHandle = INDEX_NONE;
		}
	}
	if (WaterMaskBed)
	{
		WaterMaskBed->Stop();
	}
	SetRiserFlowPlaying(false);
	GetWorldTimerManager().ClearTimer(ControlSettleTimer);
	GetWorldTimerManager().ClearTimer(OverflowSloshFadeTimer);
	if (UAudioComponent* Slosh = OverflowSloshVoice.Get())
	{
		Slosh->Stop();
	}
	if (APlayerController* Controller = GetWorld()
		? GetWorld()->GetFirstPlayerController()
		: nullptr)
	{
		if (AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD()))
		{
			Hud->EndMissingFloorFailureEnding();
		}
	}
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->SetAuthoredSilence(false);
			// 붙든 대치의 드론을 풀 사람이 사라진다. 여기서 놓아야 다음 Calm이 걷는다.
			AudioDirector->HoldFinaleScore(false);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AIGMissingFloorNightFourDirector::SetHourActive(const bool bHourActive)
{
	bHourCurrentlyActive = bHourActive;
	if (!bHourCurrentlyActive)
	{
		// 이 디렉터가 검게 덮어 둔 화면인가. 대치의 암전은 BlackoutRestoreTimer가,
		// 엔딩 C의 암전은 카드 타이머가 푸는데 아래 ResetFinaleTimers가 둘 다
		// 지운다. 지우기 전에 봐 둔다.
		const bool bOwnBlackoutHeld = bFailureEndingActive
			|| GetWorldTimerManager().IsTimerActive(BlackoutRestoreTimer);
		if (UWorld* World = GetWorld())
		{
			if (UIGMissingFloorAudioSubsystem* AudioDirector =
				World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
			{
				AudioDirector->SetAuthoredSilence(false);
				// 고르지 않은 채 05:30이 오거나 B가 벽 곁에서 새벽을 맞으면 붙든 대치의
				// 드론이 여기서 빠진다(5초). A는 조율 걸음이 이미 걷었다.
				AudioDirector->HoldFinaleScore(false);
				if (AudioDirector->GetThreatState() == EIGAudioThreatState::Finale)
				{
					AudioDirector->SetThreatState(EIGAudioThreatState::Calm);
				}
			}
		}
		ResetFinaleTimers();
		bFinalRevealActive = false;
		bMokRetreatActive = false;
		SetMokVisible(false);
		// 엔딩 B의 기다림은 늦어도 새벽에 끝난다.
		EndEndingBVigil();
		if (AIGListenerEntity* ListenerActor = Listener.Get())
		{
			ListenerActor->SetDormant(true);
		}
		// 화면은 제 암전만 걷는다. 새벽은 눈을 감긴 검은 화면을 붙든 채 이 낮
		// 전환을 보내고, 엔딩 A·B의 새벽은 에필로그가 덮은 화면 아래서 온다.
		// 남의 암전을 걷으면 낮으로 바뀐 복도가 눈을 뜨기 전에 드러난다.
		const UIGMissingFloorNarrativeSubsystem* EndNarrative = GetNarrative();
		const FName Ending = EndNarrative ? EndNarrative->GetEndingChoice() : NAME_None;
		const bool bEpilogueOwnsScreen =
			Ending == IGNightFour::EndingAId || Ending == IGNightFour::EndingBId;
		const AIGNightPhaseDirector* DawnPhase =
			IGNightFour::FindNightPhase(GetWorld());
		const bool bDawnOwnsScreen =
			DawnPhase && DawnPhase->IsDawnTransitionInProgress();
		APlayerController* Controller = GetWorld()
			? GetWorld()->GetFirstPlayerController()
			: nullptr;
		if (bOwnBlackoutHeld && !bEpilogueOwnsScreen && !bDawnOwnsScreen
			&& Controller && Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StopCameraFade();
		}
	}
	RefreshPresentation();
	if (bHourCurrentlyActive)
	{
		if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
		{
			if (Narrative->GetNightIndex() == 4
				&& Narrative->IsNightFourWallOpened()
				&& !Narrative->HasBeatPlayed(
					IGNightFour::FinalConfrontationBeat))
			{
				// 벽을 열어 둔 채 넘어간 밤이 되풀이됐다. 눈은 403호 침대에서 뜬다.
				// 여기서 바로 리빌을 시작하면 4층에 누운 채 5층 공동 앞의 독백과
				// 목한수가 나온다. 공동 앞에 다시 설 때 이어 간다.
				if (IGNightFour::IsImmediateFinaleRun())
				{
					BeginCavityReveal();
				}
				else
				{
					GetWorldTimerManager().SetTimer(
						RepeatRevealPollTimer,
						this,
						&AIGMissingFloorNightFourDirector::PollRepeatedReveal,
						0.25f,
						true);
				}
			}
		}
	}
}

bool AIGMissingFloorNightFourDirector::ValidateFixtures() const
{
	const auto ValveFits = [](const AIGMissingFloorEvidence* Evidence, const float Diameter)
	{
		const UStaticMeshComponent* Mesh = Evidence ? Evidence->GetPresentationMesh() : nullptr;
		if (!Mesh || !Mesh->GetStaticMesh()) { return false; }
		const FVector Size = Mesh->GetStaticMesh()->GetBounds().BoxExtent * 2.0f * Mesh->GetComponentScale().GetAbs();
		const bool bFits = FMath::IsNearlyEqual(Size.GetMax(), Diameter, 1.0f) && Size.GetMin() < 6.0f;
		UE_LOG(LogIndieGame, Display, TEXT("SPATIAL_VALVE %s %s size=%s"), *Evidence->GetName(),
			bFits ? TEXT("PASS") : TEXT("FAIL"), *Size.ToCompactString());
		return bFits;
	};
	return Scene.IsValid()
		&& EvictionNotice
		&& CleaningDrain
		&& FloatBypass
		&& ValveFits(CleaningDrain, 18.0f)
		&& ValveFits(FloatBypass, 12.0f)
		&& TransferPump && PumpSelector && PumpLamps.Num() == 3
		&& WallBreakTarget
		&& EndingATarget
		&& EndingBTarget
		&& Listener.IsValid()
		&& HasFinaleFigures()
		&& EndingHammerVisual
		&& EndingPhoneVisual;
}

int32 AIGMissingFloorNightFourDirector::GetPumpLampMask() const
{
	int32 Mask = 0;
	for (int32 Index = 0; Index < PumpLamps.Num(); ++Index)
	{
		if (PumpLamps[Index] && PumpLamps[Index]->K2_GetScalarParameterValue(TEXT("Lit")) > 1.f)
			Mask |= 1 << Index;
	}
	return Mask;
}

bool AIGMissingFloorNightFourDirector::IsWaterMaskPlaying() const
{
	// This is the gameplay mix state, not the platform audio-device state.
	// Headless contracts run with -nosound, where UAudioComponent::IsPlaying
	// is false even though the authored mask and hum source are both active.
	return bWaterMaskActive;
}

void AIGMissingFloorNightFourDirector::ResetFinaleTimers()
{
	GetWorldTimerManager().ClearTimer(DistantReplyTimer);
	GetWorldTimerManager().ClearTimer(MokRevealTimer);
	GetWorldTimerManager().ClearTimer(EntityPassTimer);
	GetWorldTimerManager().ClearTimer(BlackoutTimer);
	GetWorldTimerManager().ClearTimer(BlackoutRestoreTimer);
	GetWorldTimerManager().ClearTimer(FailureListingTimer);
	GetWorldTimerManager().ClearTimer(FailureRetryTimer);
	// 벽이 열리면 두 번째 침묵이다. 아직 오지 않은 정전의 발소리와 401호의
	// 대답도 여기서 거둔다.
	GetWorldTimerManager().ClearTimer(PowerCutTimer);
	GetWorldTimerManager().ClearTimer(PowerCutStepTimer);
	GetWorldTimerManager().ClearTimer(Unit401ReplyTimer);
	GetWorldTimerManager().ClearTimer(HammerCallTimer);
	GetWorldTimerManager().ClearTimer(WallAnswerTimer);
	for (FTimerHandle& HitTimer : WallAnswerHitTimers)
	{
		GetWorldTimerManager().ClearTimer(HitTimer);
	}
	bHammerHeldForCall = false;
	GetWorldTimerManager().ClearTimer(RecordingLiftTimer);
	// 「오빠다.」 뒤 3초. 그 사이 05:30이 오면 막간은 낮에 시작하면 안 된다.
	GetWorldTimerManager().ClearTimer(InterludeTimer);
	GetWorldTimerManager().ClearTimer(RepeatRevealPollTimer);
}

void AIGMissingFloorNightFourDirector::PollRepeatedReveal()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || Narrative->GetNightIndex() != 4
		|| !Narrative->IsNightFourWallOpened()
		|| Narrative->HasBeatPlayed(IGNightFour::FinalConfrontationBeat))
	{
		GetWorldTimerManager().ClearTimer(RepeatRevealPollTimer);
		return;
	}
	// 눈이 감기는 동안은 기다린다. 새벽의 SetHourActive(false)가 이 타이머를 걷는다.
	if (!IsHourLive())
	{
		return;
	}
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		return;
	}
	const FVector Offset =
		PlayerPawn->GetActorLocation() - IGNightFour::WallBreakLocation;
	if (Offset.Size2D() > IGNightFour::RepeatRevealReachCentimeters
		|| FMath::Abs(Offset.Z) > IGNightFour::RepeatRevealFloorToleranceCentimeters)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(RepeatRevealPollTimer);
	BeginCavityReveal();
}

bool AIGMissingFloorNightFourDirector::IsHourLive() const
{
	if (!bHourCurrentlyActive)
	{
		return false;
	}
	const AIGNightPhaseDirector* NightPhase =
		IGNightFour::FindNightPhase(GetWorld());
	return !NightPhase || NightPhase->IsHourActive();
}

void AIGMissingFloorNightFourDirector::BeginCavityReveal()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !bHourCurrentlyActive || Narrative->GetNightIndex() != 4
		|| !Narrative->IsNightFourWallOpened())
	{
		return;
	}

	SetCavityRevealVisible(true);
	bFinalConfrontationComplete = Narrative->HasBeatPlayed(
		IGNightFour::FinalConfrontationBeat);
	if (bFinalConfrontationComplete || bFinalRevealActive
		|| bMokRetreatActive)
	{
		return;
	}

	if (Narrative->HasBeatPlayed(IGNightFour::FinalRevealBeat))
	{
		const bool bMokAlreadyVisible = MokVisuals.Num() > 0
			&& MokVisuals[0]
			&& MokVisuals[0]->IsVisible();
		if (!bMokAlreadyVisible
			&& !GetWorldTimerManager().IsTimerActive(MokRevealTimer)
			&& !GetWorldTimerManager().IsTimerActive(EntityPassTimer))
		{
			GetWorldTimerManager().SetTimer(
				MokRevealTimer,
				this,
				&AIGMissingFloorNightFourDirector::PresentMokHansoo,
				0.25f,
				false);
		}
		return;
	}

	ResetFinaleTimers();
	FinalRevealStage = 0;
	RevealAttentionSeconds = 0.0f;
	RevealStageElapsedSeconds = 0.0f;
	bFinalRevealActive = true;
	bRevealSilenceApplied = false;

	// The hydraulic bed has served its mechanical purpose. Discovery begins
	// with one real second where the player only hears their own room tone.
	if (WaterMaskBed)
	{
		WaterMaskBed->FadeOut(0.14f, 0.0f);
	}
	// 관 속 물길도 같이 멎는다. PUZZLE은 저작 침묵이 누르지 않는다.
	SetRiserFlowPlaying(false, 0.14f);
	// 안전망의 배관 울음도 PUZZLE이고, 같은 공용 입상관에서 운다. 울던 중이면
	// 같이 걷는다. 이 뒤로는 저작 침묵 동안 안전망이 울지 않는다.
	for (TActorIterator<AIGMissingFloorMercyDirector> It(GetWorld()); It; ++It)
	{
		It->FadeOutPipeCry(0.14f);
		break;
	}
	bWaterMaskActive = false;
	if (UWorld* World = GetWorld())
	{
		if (WaterMaskHumHandle != INDEX_NONE)
		{
			if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
			{
				Noise->UnregisterHumSource(WaterMaskHumHandle);
			}
			WaterMaskHumHandle = INDEX_NONE;
		}
	}
	if (AIGListenerEntity* ListenerActor = Listener.Get())
	{
		ListenerActor->SetDormant(true);
	}

	for (TActorIterator<AIGPlayerCharacter> It(GetWorld()); It; ++It)
	{
		if (UIGFlashlightComponent* Flashlight = It->GetFlashlight())
		{
			// 꺼 둔 채 왔으면 손이 켠다. 켜진 채였다면 스위치는 건드리지 않는다.
			const bool bTorchWasOn = Flashlight->IsOn();
			Flashlight->SetOn(true);
			if (!bTorchWasOn && Flashlight->IsOn())
			{
				IGAudio::SpawnOneShotAt(
					this,
					UIGToneSequenceSoundWave::CreateSwitchClick(this, true),
					It->GetActorLocation(),
					0.30f,
					1.0f,
					60.0f,
					320.0f,
					EIGAudioBus::Player);
			}
			Flashlight->TriggerBrownOut(0.08f);
		}
		break;
	}

	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"FinalRevealBegin",
			"위에서부터 천천히 비춰 본다."),
		4.2f);
	SetActorTickEnabled(true);
}

void AIGMissingFloorNightFourDirector::UpdateCavityReveal(
	const float DeltaSeconds)
{
	if (!bFinalRevealActive || FinalRevealStage < 0 || FinalRevealStage > 2)
	{
		return;
	}

	RevealStageElapsedSeconds += DeltaSeconds;
	// §10.2 두 번째 완전 침묵은 훑기와 함께 온다. 물이 빠지고 방 공기만 남은 1초 뒤
	// 세계가 내려가고, 손전등이 두개골부터 발까지 훑는 동안에는 심박과 석고 소리만
	// 남는다. 0단계는 최소 1.8초라 이 조건은 언제나 걸린다. 푸는 쪽은 그대로다(막간
	// 진입, 목한수 등장, 새벽).
	if (!bRevealSilenceApplied && FinalRevealStage == 0
		&& RevealStageElapsedSeconds >= 1.0f)
	{
		bRevealSilenceApplied = true;
		if (UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
			? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
			: nullptr)
		{
			AudioDirector->SetAuthoredSilence(true);
		}
	}
	APlayerController* Controller = GetWorld()
		? GetWorld()->GetFirstPlayerController()
		: nullptr;
	if (Controller)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const FVector ToTarget =
			(IGNightFour::RevealTarget(FinalRevealStage) - ViewLocation)
			.GetSafeNormal();
		const float Facing = FVector::DotProduct(
			ViewRotation.Vector(), ToTarget);
		if (Facing >= 0.72f)
		{
			RevealAttentionSeconds += DeltaSeconds;
		}
		else
		{
			RevealAttentionSeconds = FMath::Max(
				0.0f, RevealAttentionSeconds - DeltaSeconds * 0.20f);
		}
	}

	// The generous fallback prevents a lost flashlight or unusual FOV from
	// turning a presentation beat into a progression lock.
	if (RevealAttentionSeconds
			>= IGNightFour::RequiredAttentionSeconds(FinalRevealStage)
		|| RevealStageElapsedSeconds >= 5.5f)
	{
		AdvanceCavityReveal();
	}
}

void AIGMissingFloorNightFourDirector::AdvanceCavityReveal()
{
	// 눈이 감기는 동안에는 다음 단계로 넘기지 않는다. 리빌은 다음 밤에 처음부터다.
	if (!IsHourLive())
	{
		return;
	}
	if (FinalRevealStage == 0)
	{
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor", "FinalRevealSkull",
				"고개가 옷깃 쪽으로 푹 숙여져 있다."),
			3.0f);
		// 알아보는 순간 공동 안에서 석고가 갈라져 떨어진다. 글만 있던 단계에 소리.
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateSettlePlasterTick(this),
			IGNightFour::CavityDetailCenter,
			0.9f,
			0.9f,
			160.0f,
			1200.0f,
			EIGAudioBus::Puzzle);
		if (AIGPlayerCharacter* PlayerCharacter =
			Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
		{
			if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
			{
				Stress->ApplyScare(0.3f);
			}
			PlayerCharacter->PlayScareKick(0.8f);
		}
	}
	else if (FinalRevealStage == 1)
	{
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor", "FinalRevealRibs",
				"갈비뼈 사이에 석고 가루가 쌓여 있다. 오래됐어."),
			3.0f);
	}
	else
	{
		BeginSilenceBeat();
		return;
	}

	++FinalRevealStage;
	RevealAttentionSeconds = 0.0f;
	RevealStageElapsedSeconds = 0.0f;
}

void AIGMissingFloorNightFourDirector::BeginSilenceBeat()
{
	bFinalRevealActive = false;
	FinalRevealStage = 3;
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->MarkBeatPlayed(IGNightFour::FinalRevealBeat);
	}
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->SetAuthoredSilence(true);
		}
		if (APlayerController* Controller = World->GetFirstPlayerController())
		{
			if (AIGPlayerCharacter* Player =
				Cast<AIGPlayerCharacter>(Controller->GetPawn()))
			{
				if (UIGStressComponent* Stress = Player->GetStress())
				{
					Stress->SuppressHeartbeat(2.35f, true);
				}
			}
		}
	}

	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"FinalRevealShoes",
			"한쪽 발이 안으로 꺾여 있다. 방수포, 카트 바퀴… 오빠야."),
		5.0f);
	const UIGMissingFloorNarrativeSubsystem* NarrativeNow = GetNarrative();
	if (FifthDawn.IsValid() && NarrativeNow
		&& !NarrativeNow->WasFifthDawnInterludeCompleted())
	{
		// 오빠를 본 직후에 그 다섯 새벽을 산다. 독백 한 줄을 읽을 3초를 두고
		// 눈을 감긴다. 노크와 목한수는 눈을 뜬 뒤에 온다.
		GetWorldTimerManager().SetTimer(
			InterludeTimer,
			this,
			&AIGMissingFloorNightFourDirector::BeginInterludeInsideTheWall,
			3.0f,
			false);
		return;
	}
	GetWorldTimerManager().SetTimer(
		DistantReplyTimer,
		this,
		&AIGMissingFloorNightFourDirector::PlayDistantReply,
		1.05f,
		false);
	GetWorldTimerManager().SetTimer(
		MokRevealTimer,
		this,
		&AIGMissingFloorNightFourDirector::PresentMokHansoo,
		2.35f,
		false);
}

void AIGMissingFloorNightFourDirector::SetFifthDawn(
	AIGMissingFloorFifthDawnDirector* InFifthDawn)
{
	FifthDawn = InFifthDawn;
}

void AIGMissingFloorNightFourDirector::BeginInterludeInsideTheWall()
{
	// 「오빠다.」 뒤 3초 안에 05:30이 왔다. 막간을 낮에 열면 시계를 세우고
	// 봉인을 다시 건 채 다섯 새벽이 돌고, 끝나면 목한수가 낮의 벽 앞에 선다.
	if (!IsHourLive())
	{
		return;
	}
	AIGMissingFloorFifthDawnDirector* FifthDawnActor = FifthDawn.Get();
	AIGPlayerCharacter* PlayerCharacter =
		Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	UWorld* World = GetWorld();
	if (!FifthDawnActor || !PlayerCharacter || !World || bAwaitingInterlude)
	{
		ResumeAfterInterlude();
		return;
	}
	// 막간의 베드는 침묵 위에서 못 산다. 침묵은 막간이 걷고, 눈을 뜨면
	// 다시 건다. 시계와 안전망도 선다 — 벽 안의 2분 40초는 이 밤의 시간이
	// 아니다.
	if (UIGMissingFloorAudioSubsystem* AudioDirector =
		World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
	{
		AudioDirector->SetAuthoredSilence(false);
	}
	for (TActorIterator<AIGNightPhaseDirector> It(World); It; ++It)
	{
		It->SetHourPaused(true);
		break;
	}
	for (TActorIterator<AIGMissingFloorMercyDirector> It(World); It; ++It)
	{
		It->SetHourActive(false);
		break;
	}
	if (!FifthDawnActor->StartInterlude(PlayerCharacter))
	{
		for (TActorIterator<AIGNightPhaseDirector> It(World); It; ++It)
		{
			It->SetHourPaused(false);
			break;
		}
		for (TActorIterator<AIGMissingFloorMercyDirector> It(World); It; ++It)
		{
			It->SetHourActive(true);
			break;
		}
		ResumeAfterInterlude();
		return;
	}
	bAwaitingInterlude = true;
}

void AIGMissingFloorNightFourDirector::HandleInterludeCompleted()
{
	if (!bAwaitingInterlude)
	{
		return;
	}
	bAwaitingInterlude = false;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AIGNightPhaseDirector> It(World); It; ++It)
		{
			It->SetHourPaused(false);
			break;
		}
		for (TActorIterator<AIGMissingFloorMercyDirector> It(World); It; ++It)
		{
			It->SetHourActive(true);
			break;
		}
		// 눈을 뜬 자리의 정적. 목한수가 나타나며 걷는다.
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->SetAuthoredSilence(true);
		}
	}
	ResumeAfterInterlude();
}

void AIGMissingFloorNightFourDirector::ResumeAfterInterlude()
{
	// 공동 너머의 노크 둘, 그리고 목한수.
	GetWorldTimerManager().SetTimer(
		DistantReplyTimer,
		this,
		&AIGMissingFloorNightFourDirector::PlayDistantReply,
		1.05f,
		false);
	GetWorldTimerManager().SetTimer(
		MokRevealTimer,
		this,
		&AIGMissingFloorNightFourDirector::PresentMokHansoo,
		2.35f,
		false);
}

void AIGMissingFloorNightFourDirector::PlayDistantReply()
{
	if (!IsHourLive())
	{
		return;
	}
	const FVector KnockLocation = IGNightFour::CorridorEndKnockLocation;
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateWallKnockReply(this),
		KnockLocation,
		0.72f,
		0.84f,
		220.0f,
		2400.0f,
		EIGAudioBus::Entity);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "FinalRevealKnockCaption", "멀리서 두 번 두드리는 소리"),
		1.8f,
		KnockLocation);
}

void AIGMissingFloorNightFourDirector::PresentMokHansoo()
{
	// 새벽이 이 밤을 가져갔으면 그는 오지 않는다. 대치는 다음 밤에 다시 선다.
	if (bFinalConfrontationComplete || !IsHourLive())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->SetAuthoredSilence(false);
			AudioDirector->SetThreatState(EIGAudioThreatState::Finale);
		}
	}
	for (UStaticMeshComponent* Visual : MokVisuals)
	{
		if (Visual)
		{
			Visual->SetWorldLocationAndRotation(
				IGNightFour::MokStartLocation, GetMokRotation());
		}
	}
	SetMokVisible(true);
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateCardboardDrag(this),
		IGNightFour::MokStartLocation,
		0.52f,
		0.88f,
		160.0f,
		1200.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushFearDirection(
		this, IGNightFour::MokStartLocation, 1.0f);
	// 정사 4-5의 대사 그대로다. 밤4 전까지 그는 유담에게 말을 건 적이 없다 —
	// 낮의 그는 관리실 문 옆 쪽지뿐이다. 「처리한다고 했잖아요」처럼 없던 대화를
	// 가리키면 플레이어는 장면을 놓쳤나 의심한다. 벽이 열린 뒤라 이 말이 폰에
	// 담긴다(§9 보강 증거).
	const FText MokLine = NSLOCTEXT(
		"IGMissingFloor",
		"MokHansooFinalLine",
		"그만해요. 덮어야 돼. 이거… 말해 봤자 아무도 안 믿어요.");
	AIGHorrorHUD::PushDialogue(
		this,
		NSLOCTEXT("IGMissingFloor", "MokHansooName", "목한수"),
		MokLine,
		EIGDialogueChannel::Conversation,
		3.0f,
		EIGDialoguePriority::Critical);

	// §22.3. 그의 말은 언제나 같다 — 애원은 본 것과 무관하다. 달라지는 것은
	// 유담이 그 앞에서 무엇을 댈 수 있느냐다. 아무것도 못 본 회차는 침묵이
	// 대답이고, 그것도 이 장면에서 성립한다.
	TArray<FText> ReplyLines;
	BuildConfrontationReplyLines(ReplyLines);
	for (const FText& ReplyLine : ReplyLines)
	{
		AIGHorrorHUD::PushDialogue(
			this,
			NSLOCTEXT("IGMissingFloor", "YudamName", "백유담"),
			ReplyLine,
			EIGDialogueChannel::Conversation,
			IGNightFour::ConfrontationReplySeconds,
			EIGDialoguePriority::Critical);
	}
	// 그가 지나가기 전에 댄 줄이 다 끝나야 한다. 존재가 먼저 들어오면
	// 대치가 대화가 아니라 배경이 된다. 줄마다 실제로 화면에 머무는 시간을
	// 잰다 — 한 줄에 3.05초를 곱하던 값은 마흔 글자짜리 줄이 5초를 쓰는
	// 것을 몰랐고, 세 줄이면 그가 대화 중간에 들어왔다.
	float ReplySeconds = 0.0f;
	for (const FText& ReplyLine : ReplyLines)
	{
		ReplySeconds += AIGHorrorHUD::EstimateDialogueSeconds(
			ReplyLine, IGNightFour::ConfrontationReplySeconds);
	}
	// 목한수의 줄도 같이 잰다. 스물다섯 자라 기준값보다 길고, 자막을 오래 두는
	// 설정이면 모든 줄이 그 배율만큼 늘어난다. 기준값은 아무 말도 댈 수 없는
	// 회차의 하한으로 남는다. 프로브는 기본 배율로 잰다.
	float CaptionScale = 1.0f;
	if (!IGNightFour::IsImmediateFinaleRun())
	{
		const UGameInstance* GameInstance = GetGameInstance();
		const UIGAccessibilitySubsystem* Accessibility = GameInstance
			? GameInstance->GetSubsystem<UIGAccessibilitySubsystem>()
			: nullptr;
		if (Accessibility)
		{
			CaptionScale = Accessibility->GetCaptionDurationScale();
		}
	}
	const float SpokenSeconds = (AIGHorrorHUD::EstimateDialogueSeconds(MokLine, 3.0f)
		+ ReplySeconds) * CaptionScale;
	ReplySeconds = FMath::Max(
		0.0f,
		SpokenSeconds + IGNightFour::EntityPassAfterLinesSeconds
			- IGNightFour::EntityPassBaseSeconds);
	EntityPassWaits = 0;
	GetWorldTimerManager().SetTimer(
		EntityPassTimer,
		this,
		&AIGMissingFloorNightFourDirector::BeginEntityPass,
		IGNightFour::EntityPassBaseSeconds + ReplySeconds,
		false);
}

void AIGMissingFloorNightFourDirector::BeginEntityPass()
{
	// 눈을 감기는 새벽 디렉터가 그를 제자리에 세워 두었다. 여기서 길을 주면
	// 멈춘 몸이 다시 움직인다.
	if (!IsHourLive())
	{
		return;
	}
	// 댄 줄이 아직 화면에 있으면 조금 더 기다린다. 앞서 떠 있던 독백이 목한수의
	// 줄에 밀려 뒤로 다시 붙거나 긴 줄이 두 쪽으로 나뉘면, 잰 값보다 늦게
	// 끝난다. 대사 창을 못 그리는 프로브는 기다리지 않는다.
	if (!IGNightFour::IsImmediateFinaleRun()
		&& EntityPassWaits < IGNightFour::EntityPassMaximumWaits)
	{
		const APlayerController* Controller = GetWorld()
			? GetWorld()->GetFirstPlayerController()
			: nullptr;
		const AIGHorrorHUD* Hud = Controller
			? Cast<AIGHorrorHUD>(Controller->GetHUD())
			: nullptr;
		if (Hud && !Hud->IsDialogueLaneIdle())
		{
			++EntityPassWaits;
			GetWorldTimerManager().SetTimer(
				EntityPassTimer,
				this,
				&AIGMissingFloorNightFourDirector::BeginEntityPass,
				IGNightFour::EntityPassWaitStepSeconds,
				false);
			return;
		}
	}
	EntityPassWaits = 0;

	bMokRetreatActive = true;
	MokRetreatSeconds = 0.0f;
	MokLastStepLocation = IGNightFour::MokStartLocation;
	MokStepDistance = IGNightFour::MokFirstStepHeadStart;
	MokStepIndex = 0;
	SetActorTickEnabled(true);

	const FVector EntityStart(130.0f, 425.0f, 1253.0f);
	// 소리가 먼저 온다. 기는 걸음 둘이 들리는 동안 목한수가 먼저 물러서고, 그는
	// 0.7초 뒤 그 뒤에서 나와 목한수를 따라간다. 추격 속도의 82%로 같은 순간에
	// 출발시키던 때는 막 걸음을 떼는 목한수를 뚫고 앞질러 1.2초 만에 북벽 앞에서
	// 꺼졌다. 150 cm/s면 둘 사이가 끝까지 1 m 안팎으로 유지되고, 목한수가 벽에
	// 몰린 뒤 그가 다가오는 그림에서 불이 나간다.
	static constexpr float EntityEnterSeconds = 0.7f;
	static constexpr float EntitySecondStepSeconds = 0.4f;
	static constexpr float EntityFollowSpeed = 150.0f;
	const auto PlayApproachStep = [this, EntityStart](const uint32 StepHash)
	{
		IGAudio::SpawnOneShotAt(
			this,
			IGAudio::SampleVariantOr(
				TEXT("Entity_CrawlStep"), 3, StepHash,
				[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateEntityCrawlStep(this, false); }),
			EntityStart,
			0.8f,
			1.0f,
			160.0f,
			1400.0f,
			EIGAudioBus::Entity);
	};
	PlayApproachStep(0u);
	// 들어오는 시각은 EntityPassTimer 하나로 잇는다. 대치가 도중에 끊기면
	// ResetFinaleTimers가 그 핸들을 지우므로 그가 뒤늦게 나오지 않는다.
	GetWorldTimerManager().SetTimer(
		EntityPassTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, EntityStart, PlayApproachStep]()
		{
			if (!IsHourLive())
			{
				return;
			}
			PlayApproachStep(2654435761u);
			GetWorldTimerManager().SetTimer(
				EntityPassTimer,
				FTimerDelegate::CreateWeakLambda(this, [this, EntityStart]()
				{
					if (!IsHourLive())
					{
						return;
					}
					if (AIGListenerEntity* ListenerActor = Listener.Get())
					{
						ListenerActor->BeginFinalePass(
							EntityStart,
							{
								FVector(130.0f, 520.0f, 1253.0f),
								FVector(145.0f, 650.0f, 1253.0f),
								FVector(135.0f, 790.0f, 1253.0f),
								FVector(70.0f, 910.0f, 1253.0f),
							},
							EntityFollowSpeed);
					}
				}),
				EntityEnterSeconds - EntitySecondStepSeconds,
				false);
		}),
		EntitySecondStepSeconds,
		false);
	AIGHorrorHUD::PushFearDirection(this, EntityStart, 1.5f);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT(
			"IGMissingFloor", "FinaleDragCaption",
			"석고 가루 위로 무겁게 끌리는 소리"),
		2.3f,
		EntityStart);
	// 그가 늦게 나오는 만큼 불도 0.75초 늦게 나간다. 목한수가 북벽에 닿은 뒤
	// 그가 1 m 앞까지 다가온 자리다.
	GetWorldTimerManager().SetTimer(
		BlackoutTimer,
		this,
		&AIGMissingFloorNightFourDirector::TriggerBlackout,
		3.0f,
		false);
}

void AIGMissingFloorNightFourDirector::TriggerBlackout()
{
	// 새벽이 눈을 감기는 중이면 화면은 그쪽 것이다. 여기서 0부터 다시 감으면
	// 감기던 화면이 한 프레임 밝아진다.
	if (!IsHourLive())
	{
		return;
	}
	if (APlayerController* Controller = GetWorld()
		? GetWorld()->GetFirstPlayerController()
		: nullptr)
	{
		if (Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StartCameraFade(
				0.0f,
				1.0f,
				0.18f,
				FLinearColor::Black,
				/*bShouldFadeAudio=*/true,
				/*bHoldWhenFinished=*/true);
		}
	}
	GetWorldTimerManager().SetTimer(
		BlackoutRestoreTimer,
		this,
		&AIGMissingFloorNightFourDirector::CompleteConfrontation,
		0.55f,
		false);
}

void AIGMissingFloorNightFourDirector::CompleteConfrontation()
{
	// 암전 한가운데서 05:30이 왔다. 눈은 새벽이 뜨게 하고, 대치는 끝나지 않은
	// 것으로 둔다 — 여기서 걷으면 새벽의 검은 화면이 같이 걷힌다.
	if (!IsHourLive())
	{
		return;
	}
	bMokRetreatActive = false;
	SetMokVisible(false);
	// 대치의 드론은 최종 선택까지 남는다(§10.1 M-공동 「엔딩 분기 전」). 그가 잠들며
	// 보내는 Calm이 걷지 못하게 먼저 붙들고, 정적이 돌아오는 동안(§8 4-5b) 6초에
	// 걸쳐 절반으로 가라앉힌다. 엔딩 A의 조율 걸음이 이 드론을 걷고, B의 기다림과
	// 새벽이 놓는다. 그가 암전 전에 길 끝에 닿아 먼저 잠들었으면 드론은 빠지는
	// 중이다. Finale를 다시 보내 같은 드론을 되살린 뒤 붙든다.
	if (UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
		? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
		: nullptr)
	{
		AudioDirector->SetThreatState(EIGAudioThreatState::Finale);
		AudioDirector->HoldFinaleScore(true);
		AudioDirector->DuckFinaleScore(6.0f, 0.5f);
	}
	if (AIGListenerEntity* ListenerActor = Listener.Get())
	{
		ListenerActor->SetDormant(true);
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->MarkBeatPlayed(IGNightFour::FinalConfrontationBeat);
	}
	bFinalConfrontationComplete = true;

	if (APlayerController* Controller = GetWorld()
		? GetWorld()->GetFirstPlayerController()
		: nullptr)
	{
		if (Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StartCameraFade(
				1.0f,
				0.0f,
				0.45f,
				FLinearColor::Black,
				/*bShouldFadeAudio=*/true,
				/*bHoldWhenFinished=*/false);
		}
	}
	// §30.5. 방금 들은 것을 그대로 말한다. 그는 목한수의 발소리를 골랐다.
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor", "FinalConfrontationAfter",
			"나를 지나쳐서 관리인을 따라갔어."),
		4.5f);
	// §8 4-5b. 앞의 줄이 다 읽힌 뒤, 유담이 폰을 본다. 선택은 지금처럼 바로
	// 열린다 — 먼저 고른 사람은 이 줄을 건너뛴다.
	GetWorldTimerManager().SetTimer(
		RecordingLiftTimer,
		this,
		&AIGMissingFloorNightFourDirector::PlayRecordingLiftBeat,
		IGNightFour::RecordingLiftDelaySeconds,
		false);
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::UpdateMokRetreat(
	const float DeltaSeconds)
{
	MokRetreatSeconds += DeltaSeconds;
	const float Alpha = FMath::InterpEaseInOut(
		0.0f, 1.0f, FMath::Clamp(MokRetreatSeconds / 2.8f, 0.0f, 1.0f), 2.0f);
	const FVector Location = FMath::Lerp(
		IGNightFour::MokStartLocation, IGNightFour::MokExitLocation, Alpha);
	for (int32 Index = 0; Index < MokVisuals.Num(); ++Index)
	{
		if (UStaticMeshComponent* Visual = MokVisuals[Index])
		{
			Visual->SetWorldLocation(Location);
			Visual->SetWorldRotation(
				Index == 2
					? GetMokRotation() + FRotator(0.0f, 0.0f, Alpha * 9.0f)
					: GetMokRotation());
		}
	}

	// 목한수는 미끄러지지 않는다. 석고 가루를 밟으며 뒷걸음친다. 그가 따라가는
	// 것이 이 발소리다(§8 4-5). 움직인 거리로 세므로 처음과 끝은 성기고
	// 가운데는 촘촘하다.
	MokStepDistance += FVector::Dist2D(Location, MokLastStepLocation);
	MokLastStepLocation = Location;
	if (MokStepDistance < IGNightFour::MokStepStride)
	{
		return;
	}
	MokStepDistance -= IGNightFour::MokStepStride;
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleVariantOr(
			TEXT("Foot_Gypsum"), 5, static_cast<uint32>(MokStepIndex) << 12,
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSurfaceFootstep(this, EIGFootstepSurface::GypsumDebris, 0.88f, 0.9f); }),
		Location + FVector(0.0f, 0.0f, 8.0f),
		0.72f,
		0.9f,
		180.0f,
		1600.0f,
		EIGAudioBus::World);
	if (MokStepIndex == 0)
	{
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "MokRetreatStepsCaption", "뒷걸음치는 발소리"),
			2.0f,
			Location);
	}
	++MokStepIndex;
}

void AIGMissingFloorNightFourDirector::UpdateEndingHammer(
	const float DeltaSeconds)
{
	if (!EndingHammerVisual)
	{
		bEndingHammerMoving = false;
		return;
	}
	EndingHammerSeconds += DeltaSeconds;
	const float Alpha = FMath::InterpEaseInOut(
		0.0f, 1.0f, FMath::Clamp(EndingHammerSeconds / 0.85f, 0.0f, 1.0f), 2.0f);
	EndingHammerVisual->SetWorldLocation(FMath::Lerp(
		IGNightFour::EndingHammerStart, IGNightFour::EndingHammerRest, Alpha));
	EndingHammerVisual->SetWorldRotation(FQuat::Slerp(
		FRotator(0.0f, 18.0f, -76.0f).Quaternion(),
		FRotator(-12.0f, 72.0f, -88.0f).Quaternion(),
		Alpha));
	if (Alpha >= 1.0f)
	{
		bEndingHammerMoving = false;
	}
}

void AIGMissingFloorNightFourDirector::UpdateRoofValveMotion(const float DeltaSeconds)
{
	bValveMotionActive = false;
	AIGMissingFloorEvidence* Valves[] = {CleaningDrain.Get(), FloatBypass.Get()};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (!Valves[Index] || FMath::IsNearlyEqual(ValveAngles[Index], ValveTargetAngles[Index], .01f)) { continue; }
		ValveAngles[Index] = FMath::FInterpConstantTo(ValveAngles[Index], ValveTargetAngles[Index], DeltaSeconds, 300.f);
		const FTransform& Closed = ValveClosedTransforms[Index];
		const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(ValveAngles[Index]));
		Valves[Index]->SetActorLocationAndRotation(Closed.GetLocation() + FVector(0, -.65f * ValveAngles[Index] / 225.f, 0),
			Closed.GetRotation() * Turn);
		bValveMotionActive |= !FMath::IsNearlyEqual(ValveAngles[Index], ValveTargetAngles[Index], .01f);
	}
}

void AIGMissingFloorNightFourDirector::HandleEvictionNotice(
	AIGMissingFloorEvidence* Evidence)
{
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::PostEvictionNotice(const bool bAnnounce)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative
		|| !Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer)
		|| !Narrative->MarkBeatPlayed(IGNightFour::EvictionPostedBeat))
	{
		return;
	}
	RefreshPresentation();
	if (!bAnnounce || bHourCurrentlyActive || bEvictionAnnounced)
	{
		return;
	}
	// 붙이는 것은 못 봐도 그 자리가 달라졌다는 것은 들려야 한다. 403호 안에
	// 있을 때만 낸다. 다른 층에서 「4층 복도」 자막이 뜨면 거짓말이다.
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn || !CanHearEvictionPosting(PlayerPawn->GetActorLocation()))
	{
		return;
	}
	bEvictionAnnounced = true;
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreatePickupRustle(this),
		IGNightFour::EvictionNoticeLocation,
		0.5f,
		1.0f,
		120.0f,
		1100.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "EvictionPostedCaption", "4층 복도에서 종이 붙이는 소리"),
		2.0f,
		IGNightFour::EvictionNoticeLocation);
}

bool AIGMissingFloorNightFourDirector::IsEvictionNoticePosted() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Narrative && Narrative->HasBeatPlayed(IGNightFour::EvictionPostedBeat);
}

bool AIGMissingFloorNightFourDirector::CanHearEvictionPosting(
	const FVector& ListenerLocation)
{
	return IGNightFour::EvictionEarshot.IsInsideOrOn(ListenerLocation);
}

void AIGMissingFloorNightFourDirector::HandleCleaningDrain(
	AIGMissingFloorEvidence* Evidence)
{
	ActivateControl(IGNightFour::CleaningDrainId, Evidence);
}

void AIGMissingFloorNightFourDirector::HandleFloatBypass(
	AIGMissingFloorEvidence* Evidence)
{
	ActivateControl(IGNightFour::FloatBypassId, Evidence);
}

void AIGMissingFloorNightFourDirector::HandleTransferPump(
	AIGMissingFloorEvidence* Evidence)
{
	ActivateControl(IGNightFour::TransferPumpId, Evidence);
}

void AIGMissingFloorNightFourDirector::ActivateControl(
	const FName ControlId,
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !bHourCurrentlyActive || Narrative->GetNightIndex() != 4)
	{
		return;
	}

	const int32 OrderIndex = Narrative->GetNightFourControlOrder().Num();
	if (bControlLockoutActive || Narrative->HasNightFourControl(ControlId))
	{
		return;
	}
	// 틀린 손잡이는 기록에 남지 않는다. 여섯 순서가 전부 같은 곳에 닿으면
	// 순서는 퍼즐이 아니라 요금이다 — 틀리면 소리가 나고, 인터록이 서고,
	// 다시 해야 한다.
	const bool bSafeStep = IGNightFour::SafeOrder().IsValidIndex(OrderIndex)
		&& IGNightFour::SafeOrder()[OrderIndex] == ControlId;
	if (!bSafeStep)
	{
		HandleControlMisorder(ControlId, Evidence);
		return;
	}
	if (!Narrative->ActivateNightFourControl(ControlId))
	{
		return;
	}

	// Every control was silent until now, which made the water mask a number
	// the HUD knew about rather than something the player built. The large
	// cleaning drain, the small float bypass and the transfer pump motor are
	// each their own sound (§10.3 밸브 3종), so the roof is assembled by ear.
	const FVector ControlLocation =
		Evidence ? Evidence->GetActorLocation() : GetActorLocation();
	USoundBase* ControlCue = ControlId == IGNightFour::TransferPumpId
		? static_cast<USoundBase*>(
			UIGToneSequenceSoundWave::CreateVentDuctSpinUp(this))
		: static_cast<USoundBase*>(UIGToneSequenceSoundWave::CreateValveOpen(
			this,
			ControlId == IGNightFour::CleaningDrainId ? 1 : 2));
	IGAudio::SpawnOneShotAt(
		this,
		ControlCue,
		ControlLocation,
		0.78f,
		1.0f,
		170.0f,
		1500.0f,
		EIGAudioBus::Puzzle);

	if (Evidence)
	{
		Evidence->SetInteractionEnabled(false);
	}
	// 성공한 조작에만 결과를 말한다. 증거 액터가 판정 전에 대사를 띄우면
	// 역순으로 밸브를 돌려도 '물이 빠진다'와 경보가 동시에 나왔다.
	const FText Result = ControlId == IGNightFour::CleaningDrainId
		? NSLOCTEXT("IGMissingFloor", "CleaningDrainThought", "아래쪽 배관으로 물이 빠진다.")
		: ControlId == IGNightFour::FloatBypassId
			? NSLOCTEXT("IGMissingFloor", "FloatBypassThought", "급수관에서도 물소리가 난다.")
			: NSLOCTEXT("IGMissingFloor", "TransferPumpThought", "펌프가 돌기 시작했다.");
	AIGHorrorHUD::PushThought(this, Result, 3.0f);
	StartWaterMaskIfReady();
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::HandleControlMisorder(
	const FName ControlId,
	AIGMissingFloorEvidence* Evidence)
{
	bHydraulicAlarmTriggered = true;
	const FVector At = Evidence ? Evidence->GetActorLocation() : GetActorLocation();
	// 두 가지 잘못이 두 가지 소리를 낸다. 배수 전에 우회를 열면 탱크가
	// 넘치고, 관이 안 열린 채 펌프를 돌리면 역지변이 쾅 닫힌다. 둘 다
	// 0.70 — 그가 듣는다.
	const bool bOverflow = ControlId == IGNightFour::FloatBypassId;
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(At, 0.70f, this);
		}
	}
	if (bOverflow)
	{
		if (UAudioComponent* Previous = OverflowSloshVoice.Get())
		{
			Previous->FadeOut(0.3f, 0.0f);
		}
	}
	UAudioComponent* AlarmVoice = IGAudio::SpawnOneShotAt(
		this,
		bOverflow
			? static_cast<USoundBase*>(
				UIGToneSequenceSoundWave::CreateRooftopTankSlosh(this))
			: static_cast<USoundBase*>(UIGToneSequenceSoundWave::CreateDoorThud(this)),
		At,
		0.9f,
		1.0f,
		180.0f,
		1800.0f,
		EIGAudioBus::Puzzle);
	if (bOverflow)
	{
		// 역지변 소리는 한 번 치고 끝나지만 출렁임은 루프 파형이다. 경보 독백이
		// 끝날 무렵 걷는다. 놓아두면 옥상에서 밤새 넘친다.
		OverflowSloshVoice = AlarmVoice;
		GetWorldTimerManager().SetTimer(
			OverflowSloshFadeTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (UAudioComponent* Slosh = OverflowSloshVoice.Get())
				{
					Slosh->FadeOut(1.5f, 0.0f);
				}
			}),
			3.8f,
			false);
	}
	AIGHorrorHUD::PushThought(
		this,
		bOverflow
			? NSLOCTEXT(
				"IGMissingFloor",
				"P5OverflowAlarm",
				"물이 넘친다. 배수 밸브를 안 열었어.")
			: NSLOCTEXT(
				"IGMissingFloor",
				"P5PressureAlarm",
				"펌프가 멈췄다. 고장등 꺼질 때까지 기다리자."),
		3.8f);
	// 인터록이 선다. 그동안은 어느 손잡이도 안 돈다.
	bControlLockoutActive = true;
	for (AIGMissingFloorEvidence* Control :
		{CleaningDrain.Get(), FloatBypass.Get(), TransferPump.Get()})
	{
		if (Control)
		{
			Control->SetInteractionEnabled(false);
		}
	}
	RefreshPresentation();
	GetWorldTimerManager().SetTimer(
		ControlLockoutTimer,
		this,
		&AIGMissingFloorNightFourDirector::ReleaseControlLockout,
		IGNightFour::ControlLockoutSeconds,
		false);
	// 절차서의 「배관이 조용해지고」. 선 배관이 세 번 울며 잦아들고 6초 뒤로는
	// 조용하다. 소음은 따로 알리지 않는다 — 0.70은 이미 한 번 났다.
	ControlSettleLocation = At + FVector(0.0f, 0.0f, 40.0f);
	ControlSettleIndex = 0;
	GetWorldTimerManager().SetTimer(
		ControlSettleTimer,
		this,
		&AIGMissingFloorNightFourDirector::PlayControlSettleKnock,
		IGNightFour::ControlSettleKnockSeconds[0],
		false);
}

void AIGMissingFloorNightFourDirector::PlayControlSettleKnock()
{
	if (!bControlLockoutActive || !bHourCurrentlyActive || bFailureEndingActive
		|| ControlSettleIndex >= IGNightFour::ControlSettleKnockCount)
	{
		return;
	}
	const int32 Index = ControlSettleIndex++;
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateSettlePipeKnock(this),
		ControlSettleLocation,
		IGNightFour::ControlSettleKnockVolumes[Index],
		1.0f,
		150.0f,
		1500.0f,
		EIGAudioBus::Puzzle);
	if (ControlSettleIndex < IGNightFour::ControlSettleKnockCount)
	{
		GetWorldTimerManager().SetTimer(
			ControlSettleTimer,
			this,
			&AIGMissingFloorNightFourDirector::PlayControlSettleKnock,
			IGNightFour::ControlSettleKnockSeconds[ControlSettleIndex]
				- IGNightFour::ControlSettleKnockSeconds[Index],
			false);
	}
}

void AIGMissingFloorNightFourDirector::ReleaseControlLockout()
{
	bControlLockoutActive = false;
	GetWorldTimerManager().ClearTimer(ControlSettleTimer);
	// 절차서의 「등이 꺼진 뒤」. 고장등이 꺼지는 순간 선택반 계전기가 한 번
	// 떨어진다. 옥상에서 틀렸으면 그 자리에서는 들리지 않는다.
	if (bHourCurrentlyActive && !bFailureEndingActive)
	{
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateRelayClick(this),
			IGNightFour::TransferPumpLocation,
			0.8f,
			1.0f,
			120.0f,
			1200.0f,
			EIGAudioBus::Puzzle);
	}
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::StartWaterMaskIfReady()
{
	// 엔딩 C는 매물 화면 전에 기계음 마스킹을 제거한다. 시간이 봉인된 동안에도
	// RefreshPresentation이 호출될 수 있으므로 호출부와 이 함수에서 함께 막는다.
	if (bFailureEndingActive)
	{
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->IsNightFourMaskRunning())
	{
		return;
	}
	Narrative->MarkPuzzleSolved(IGNightFour::PuzzleId);
	bWaterMaskActive = bHourCurrentlyActive;
	if (bHourCurrentlyActive && WaterMaskHumHandle == INDEX_NONE)
	{
		if (UWorld* World = GetWorld())
		{
			if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
			{
				// The moving riser calls the listener to itself first. The local
				// 0.40 mask then turns each 1.0 hammer report into the authored
				// 0.60 effective loudness without deleting the sound.
				Noise->ReportNoise(
					AIGPrologueWorldScene::GetSharedRiserLocation(),
					0.55f,
					this);
				WaterMaskHumHandle = Noise->RegisterHumSource(
					IGNightFour::WallBreakLocation,
					720.0f,
					0.40f);
			}
		}
	}
	if (WaterMaskBed)
	{
		if (bHourCurrentlyActive && !WaterMaskBed->IsPlaying())
		{
			WaterMaskBed->Play();
		}
		if (bHourCurrentlyActive)
		{
			SetRiserFlowPlaying(true);
		}
		return;
	}

	WaterMaskBed = NewObject<UAudioComponent>(this, TEXT("NightFourWaterMask"));
	WaterMaskBed->RegisterComponent();
	WaterMaskBed->SetWorldLocation(FVector(310.0f, 746.0f, 1300.0f));
	WaterMaskBed->SetSound(
		UIGToneSequenceSoundWave::CreateFloodedCorridorWaterBed(this));
	WaterMaskBed->AttenuationSettings = IGAudio::MakeAttenuation(
		this,
		180.0f,
		1500.0f,
		EIGAudioBus::Puzzle);
	WaterMaskBed->bAllowSpatialization = true;
	WaterMaskBed->SetVolumeMultiplier(0.62f);
	UIGMissingFloorAudioSubsystem* AudioDirector = GetWorld()
		? GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>()
		: nullptr;
	if (AudioDirector)
	{
		AudioDirector->RegisterPersistentBed(
			WaterMaskBed,
			EIGAudioBus::Puzzle);
	}
	// §8 4-2 「물소리가 건물을 채운다」. 펌프를 돌린 관리실 토출관에서 물이
	// 계속 흐르고, 배관 샤프트가 4층에 닿는 설비 벽장에서도 들린다. 5층 벽 앞
	// 한 점에서만 흐르던 때는 펌프를 돌려 놓고도 관리실이 조용했다. 모터 험은
	// 쓰지 않는다. 60Hz는 이 건물에서 엄폐라는 뜻이다(§5.1). 설비 벽장은 이미
	// 험 자리라 거짓 엄폐도 만들지 않는다.
	struct FRiserFlowSpec
	{
		const TCHAR* Name;
		FVector Location;
		int32 DistanceStep;
		float Volume;
		float InnerRadius;
		float Falloff;
	};
	const FRiserFlowSpec RiserFlowSpecs[] =
	{
		{TEXT("NightFourPumpDischargeFlow"), IGNightFour::PumpDischargeFlowLocation,
			1, 0.38f, 100.0f, 650.0f},
		{TEXT("NightFourShaftFlow"),
			AIGPrologueWorldScene::GetBoilerCupboardLocation() + FVector(0.0f, 0.0f, 60.0f),
			2, 0.5f, 150.0f, 1500.0f},
	};
	RiserFlowBeds.Reset();
	for (const FRiserFlowSpec& Spec : RiserFlowSpecs)
	{
		UAudioComponent* Flow = NewObject<UAudioComponent>(this, Spec.Name);
		Flow->RegisterComponent();
		Flow->SetWorldLocation(Spec.Location);
		Flow->SetSound(
			UIGToneSequenceSoundWave::CreatePipeWaterFlow(this, Spec.DistanceStep));
		Flow->AttenuationSettings = IGAudio::MakeAttenuation(
			this,
			Spec.InnerRadius,
			Spec.Falloff,
			EIGAudioBus::Puzzle);
		Flow->bAllowSpatialization = true;
		Flow->SetVolumeMultiplier(Spec.Volume);
		if (AudioDirector)
		{
			AudioDirector->RegisterPersistentBed(Flow, EIGAudioBus::Puzzle);
		}
		RiserFlowBeds.Add(Flow);
	}
	if (bHourCurrentlyActive)
	{
		WaterMaskBed->Play();
		SetRiserFlowPlaying(true);
	}
	if (bHourCurrentlyActive)
	{
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"P5MaskReady",
				"물 도는 동안엔 벽 치는 소리가 묻히겠다."),
			4.0f);
		// §8 4-5b의 심기. 망치를 들기 전에 폰 녹음을 켠다. 그 시간의 소리는
		// 담기지 않는다는 것을 밤2에 배웠어도 증거를 만들 생각은 그대로다. 벽이
		// 열리는 순간 규칙이 풀리고, 대치가 끝난 뒤 그 녹음이 처음으로 무언가를
		// 담고 있다.
		StartWallRecording();
		AIGHorrorHUD::PushThought(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"NightFourRecordingArmed",
				"폰 녹음 켜 두자. 이번엔 뭐라도 남겨야 해."),
			3.6f);
	}
}

void AIGMissingFloorNightFourDirector::StartWallRecording()
{
	UWorld* World = GetWorld();
	UIGRecordingSubsystem* Recording = World
		? World->GetSubsystem<UIGRecordingSubsystem>()
		: nullptr;
	if (!Recording || Recording->IsRecording())
	{
		return;
	}
	// 폰은 메모 하나를 되쓴다. 밤2의 테이프는 여기서 지워진다.
	Recording->ClearTake();
	Recording->StartRecording();
}

void AIGMissingFloorNightFourDirector::PlayRecordingLiftBeat()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	UWorld* World = GetWorld();
	const UIGRecordingSubsystem* Recording = World
		? World->GetSubsystem<UIGRecordingSubsystem>()
		: nullptr;
	// 먼저 고른 사람은 이 줄을 건너뛴다. 녹음이 꺼져 있으면(저장에서 이어 붙인
	// 밤) 담겼다고 말하지 않는다.
	if (!IsHourLive() || bFailureEndingActive || !Narrative
		|| !Narrative->IsNightFourWallOpened()
		|| !Narrative->GetEndingChoice().IsNone()
		|| !Recording || !Recording->IsRecording() || !Recording->IsRuleLifted())
	{
		return;
	}
	// §8 4-5b. 정적이 돌아온 뒤 폰을 본다. 밤2의 아침에는 노크가 있던 자리가
	// 비어 있었다. 벽이 열린 뒤로는 방금의 목소리와 끌림이 파형에 남아 있다.
	// 소리는 붙이지 않는다 — 대치가 끝난 정적이 이 줄의 배경이다.
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor",
			"NightFourRecordingLifted",
			"폰을 보니 방금 그 소리가 다 찍혀 있다."),
		3.4f);
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT("IGMissingFloor", "P2PhoneKept", "녹음됐다. 이번엔 제대로 됐어."),
		4.0f);
}

void AIGMissingFloorNightFourDirector::SetRiserFlowPlaying(
	const bool bPlay,
	const float FadeSeconds)
{
	for (UAudioComponent* Flow : RiserFlowBeds)
	{
		if (!Flow)
		{
			continue;
		}
		const bool bFadingOut =
			Flow->GetPlayState() == EAudioComponentPlayState::FadingOut;
		if (bPlay)
		{
			// 펌프가 도는 만큼 관이 차오른다. 한 프레임에 켜지면 스위치로 들린다.
			if (!Flow->IsPlaying() || bFadingOut)
			{
				Flow->FadeIn(
					FMath::Max(FadeSeconds, IGNightFour::RiserFlowFadeInSeconds),
					1.0f);
			}
		}
		// 이미 잦아드는 중이면 그 페이드를 끝까지 둔다. 같은 프레임의 정지가
		// 페이드를 자르면 물이 스위치처럼 끊긴다.
		else if (Flow->IsPlaying() && !bFadingOut)
		{
			if (FadeSeconds > 0.0f)
			{
				Flow->FadeOut(FadeSeconds, 0.0f);
			}
			else
			{
				Flow->Stop();
			}
		}
	}
}

void AIGMissingFloorNightFourDirector::HandleWallStrike(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->IsNightFourMaskRunning()
		|| Narrative->IsNightFourWallOpened() || bWallOpeningPending)
	{
		return;
	}

	const int32 StrikeCount = Narrative->RecordNightFourWallStrike();
	if (StrikeCount == 1)
	{
		// 녹음은 첫 타격부터 돌아야 한다 — 벽이 열린 순간부터 레벨을 그리려면.
		// 물을 트는 자리에서 이미 켰으면 그대로 둔다. 엔딩 C의 재시도처럼 그
		// 자리를 다시 지나지 않는 밤은 여기서 켠다.
		StartWallRecording();
	}
	const FVector StrikeLocation = Evidence
		? Evidence->GetActorLocation()
		: IGNightFour::WallBreakLocation;
	// §21.3 망치 임팩트. The strike index escalates the §10.3 three-stage
	// fracture, so the wall audibly goes from bruised to broken through and the
	// player never needs the counter to know where they are.
	USoundBase* HitSound = IGAudio::SampleVariantOr(
		TEXT("Hammer_Hit"), 2, static_cast<uint32>(StrikeCount) * 2654435761u,
		[this, StrikeCount]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateHammerImpact(this, StrikeCount - 1); });
	IGAudio::SpawnOneShotAt(
		this,
		HitSound,
		StrikeLocation,
		1.0f,
		1.0f,
		160.0f,
		1400.0f,
		EIGAudioBus::Puzzle);
	// 녹음 두 벌은 망치 머리가 판에 닿는 0.2초만 갖고 있어서 다섯 번이 다
	// 같다. 몇 번째인지 — 멍, 찢기며 비는 울림, 뚫림 — 는 파쇄 층이 말한다.
	// 합성으로 내려간 판은 그 단계를 이미 품고 있으니 겹치지 않는다.
	if (HitSound && !Cast<UIGToneSequenceSoundWave>(HitSound))
	{
		constexpr float FractureVolumes[] = {0.62f, 0.72f, 0.82f, 0.92f, 1.0f};
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateHammerFractureLayer(this, StrikeCount - 1),
			StrikeLocation,
			FractureVolumes[FMath::Clamp(StrikeCount - 1, 0, 4)],
			1.0f,
			160.0f,
			1400.0f,
			EIGAudioBus::Puzzle);
	}
	// 반동은 손에 온다. 마지막 한 번이 가장 크다.
	AIGPlayerCharacter* Striker =
		Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (Striker)
	{
		Striker->PlayScareKick(StrikeCount >= 5 ? 0.9f : 0.3f + 0.1f * StrikeCount);
	}

	if (StrikeCount == 3)
	{
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::StillCoveringIt,
			EIGMissingFloorSource::BreakerCutIntervention);
		Narrative->MarkBeatPlayed(IGNightFour::PowerCutBeat);
		// 불은 망치가 떨어지고 잠깐 뒤에 나간다. 같은 프레임에 꺼지면 원인이
		// 내 망치로 읽힌다. 프로브는 3타를 친 프레임에 정전까지 본다.
		if (IGNightFour::IsImmediateFinaleRun())
		{
			CutAnnexPower();
		}
		else
		{
			GetWorldTimerManager().SetTimer(
				PowerCutTimer,
				this,
				&AIGMissingFloorNightFourDirector::CutAnnexPower,
				IGNightFour::PowerCutDelaySeconds,
				false);
		}
	}
	else if (StrikeCount < 5)
	{
		// 몇 번째인지는 파쇄 소리가 말한다. 글은 손과 벽만 본다.
		AIGHorrorHUD::PushThought(
			this,
			StrikeCount == 1
				? NSLOCTEXT("IGMissingFloor", "NightFourStrikeFirst", "석고에 금이 갔다.")
				: StrikeCount == 2
					? NSLOCTEXT("IGMissingFloor", "NightFourStrikeSecond", "안이 비었어. 소리가 달라.")
					: NSLOCTEXT("IGMissingFloor", "NightFourStrikeFourth", "손이 저리다. 한 번만 더."),
			2.2f);
		if (StrikeCount == 4)
		{
			// 401호가 듣고 있다. 망치 소리에 그 노인이 아래에서 답한다 — 둘,
			// 쉬고, 하나. 7월 29일에 한 번 했던 것을 다시. 이 밤에 사람 쪽에서
			// 오는 유일한 소리다. 망치와 같은 프레임에 내면 앞의 둘이 건물
			// 울림에 통째로 묻힌다. 꼬리가 걷힌 뒤에 온다.
			GetWorldTimerManager().SetTimer(
				Unit401ReplyTimer,
				FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					const UIGMissingFloorNarrativeSubsystem* ReplyNarrative = GetNarrative();
					if (!ReplyNarrative || !bHourCurrentlyActive || bFailureEndingActive
						|| bWallOpeningPending || ReplyNarrative->IsNightFourWallOpened())
					{
						return;
					}
					// 아래층에서 오는 둔한 쿵이다. 거리 감쇠는 걷고 층 가림만 남긴다.
					// 물 베드와 같은 PUZZLE이다. 대답 리듬은 퍼즐의 단서라서, WORLD의
					// 슬라이더와 듣기 더킹을 타면 이 밤에 이 소리만 사라진다.
					IGAudio::SpawnOneShotAt(
						this,
						UIGToneSequenceSoundWave::CreateAnswerKnockPattern(this, 0.72f),
						IGNightFour::Unit401ReplyLocation,
						1.0f,
						1.0f,
						1100.0f,
						2400.0f,
						EIGAudioBus::Puzzle);
					AIGHorrorHUD::PushAudioCaptionAt(
						this,
						NSLOCTEXT("IGMissingFloor", "Unit401ReplyCaption", "둘, 쉬고, 하나"),
						2.6f,
						IGNightFour::Unit401ReplyLocation);
				}),
				IGNightFour::Unit401ReplyDelaySeconds,
				false);
			// 벽이 열리기 직전, 5층 철문 밖에서 누가 부른다. 손이 저려 잠깐 못 친다. 그 사이
			// 부름이 오고 벽 안에서 둘, 쉬고, 하나가 오면 다시 든다. 대답할 쪽은 벽이다.
			// 프로브와 밤 캡처는 다섯 번을 곧장 친다.
			if (!IGNightFour::IsImmediateFinaleRun())
			{
				bHammerHeldForCall = true;
				GetWorldTimerManager().SetTimer(
					HammerCallTimer,
					this,
					&AIGMissingFloorNightFourDirector::BeginHammerCallAtDoor,
					IGNightFour::HammerCallDelaySeconds,
					false);
			}
		}
	}

	if (StrikeCount >= 5)
	{
		// 판은 손전등이 죽어 있는 사이에 내려앉는다. 치는 프레임에 지우면 무너진
		// 게 아니라 사라진 것이 된다. 그 틈에 한 번 더 치지 못하게 먼저 잠근다.
		bWallOpeningPending = true;
		if (Evidence)
		{
			Evidence->SetInteractionEnabled(false);
		}
		if (UIGFlashlightComponent* Flashlight = Striker ? Striker->GetFlashlight() : nullptr)
		{
			Flashlight->TriggerBrownOut(IGNightFour::WallBrownOutSeconds);
		}
		// 프로브는 5타를 친 프레임에 공동이 열렸는지 본다.
		if (FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreyboxProbe")))
		{
			OpenWallAfterFinalStrike();
		}
		else
		{
			GetWorldTimerManager().SetTimer(
				WallOpenTimer,
				this,
				&AIGMissingFloorNightFourDirector::OpenWallAfterFinalStrike,
				IGNightFour::WallCollapseDelaySeconds,
				false);
		}
	}
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::BeginHammerCallAtDoor()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !bHourCurrentlyActive || bFailureEndingActive || bWallOpeningPending
		|| Narrative->IsNightFourWallOpened())
	{
		bHammerHeldForCall = false;
		RefreshPresentation();
		return;
	}
	bool bCalled = false;
	for (TActorIterator<AIGNightThreatDirector> It(GetWorld()); It; ++It)
	{
		bCalled |= It->BeginHammerCall();
	}
	GetWorldTimerManager().SetTimer(
		WallAnswerTimer,
		this,
		&AIGMissingFloorNightFourDirector::PlayWallAnswerBetweenStrikes,
		bCalled ? IGNightFour::WallAnswerAfterCallSeconds : IGNightFour::WallAnswerWithoutCallSeconds,
		false);
}

void AIGMissingFloorNightFourDirector::PlayWallAnswerBetweenStrikes()
{
	bHammerHeldForCall = false;
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (Narrative && bHourCurrentlyActive && !bFailureEndingActive && !bWallOpeningPending
		&& !Narrative->IsNightFourWallOpened())
	{
		AIGMissingFloorNightThreeDirector::PlayWallAnswerKnocks(this, WallAnswerHitTimers);
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "NightFourWallAnswerCaption", "벽 안에서 둘, 쉬고, 하나"),
			2.6f,
			AIGMissingFloorNightThreeDirector::GetWallAnswerLocation());
		UE_LOG(LogTemp, Display, TEXT("NIGHT4_WALL_ANSWER between strikes"));
	}
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::CutAnnexPower()
{
	// 이 뒤로는 RefreshPresentation이 전원을 상태대로 맞춘다.
	GetWorldTimerManager().ClearTimer(PowerCutTimer);
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	// 그 사이에 새벽이나 포획이 밤을 가져갔으면 전원은 상태대로 둔다.
	if (!Narrative || !bHourCurrentlyActive || bFailureEndingActive
		|| Narrative->IsNightFourWallOpened())
	{
		RefreshPresentation();
		return;
	}
	if (AIGPrologueWorldScene* SceneActor = Scene.Get())
	{
		SceneActor->SetMissingFloorAnnexPower(false);
	}
	// 차단기는 내리는 소리가 있다. 한 층 아래 계단 들머리에서 딸깍 — 계단실을
	// 타고 올라와 둔하고 젖어 있다. 거리 감쇠는 걷고, 멀다는 것은 가림과 반향이
	// 말한다. 머리 위 등은 안정기가 한 번 튀고 죽는다. 밤1에 복도 등이 죽을 때와
	// 같은 소리다. 점멸은 없다. 차단기는 한 번에 끊고, 광과민 상한도 있다.
	// 딸깍은 밤1 로비에서 제 손으로 넘겨 본 그 차단기 소리다. 내리는 쪽이라 낮다.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateBreakerThrow(this),
		IGNightFour::AnnexBreakerLocation,
		1.0f,
		0.9f,
		1100.0f,
		1800.0f,
		EIGAudioBus::World);
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Ballast_Tick"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateFluorescentBallastSnap(this); }),
		IGNightFour::AnnexCeilingLampLocation,
		0.7f,
		1.0f,
		200.0f,
		1400.0f,
		EIGAudioBus::World);
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT(
			"IGMissingFloor", "NightFourPowerCut", "불이 나갔어. 밑에 누가 있어."),
		3.5f);
	AIGHorrorHUD::PushFearDirection(this, IGNightFour::AnnexBreakerLocation, 1.2f);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "BreakerCutCaption", "차단기 내리는 소리"),
		2.0f,
		IGNightFour::AnnexBreakerLocation);
	if (AIGPlayerCharacter* PlayerCharacter =
		Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		if (UIGStressComponent* Stress = PlayerCharacter->GetStress())
		{
			Stress->ApplyScare(0.2f);
		}
	}
	// 내린 사람은 계단으로 내려간다. 멀어지는 발소리 넷으로, 그가 왔다 갔다는
	// 것을 화면 없이 안다. 밤4에서 그의 유일한 개입이다.
	PowerCutStepIndex = 0;
	GetWorldTimerManager().SetTimer(
		PowerCutStepTimer,
		this,
		&AIGMissingFloorNightFourDirector::PlayPowerCutStep,
		IGNightFour::PowerCutStepIntervalSeconds,
		true,
		IGNightFour::PowerCutFirstStepSeconds);
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::PlayPowerCutStep()
{
	if (!bHourCurrentlyActive || bFailureEndingActive
		|| PowerCutStepIndex >= IGNightFour::PowerCutStepCount)
	{
		GetWorldTimerManager().ClearTimer(PowerCutStepTimer);
		return;
	}
	const int32 Step = PowerCutStepIndex++;
	// 둘째 발만 4층 복도 콘크리트다. 나머지는 철계단이라 계단실 울림이 길게
	// 남으며 멀어진다.
	USoundBase* StepSound = Step == 1
		? IGAudio::SampleVariantOr(
			TEXT("Foot_Concrete"), 5, static_cast<uint32>(Step + 1) << 12,
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSurfaceFootstep(this, EIGFootstepSurface::Concrete, 0.92f, 0.8f); })
		: IGAudio::SampleVariantOr(
			TEXT("Foot_MetalStair"), 5, static_cast<uint32>(Step + 1) << 12,
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateSurfaceFootstep(this, EIGFootstepSurface::MetalStair, 0.92f, 0.8f); });
	IGAudio::SpawnOneShotAt(
		this,
		StepSound,
		IGNightFour::PowerCutStepLocations[Step],
		IGNightFour::PowerCutStepVolumes[Step],
		1.0f,
		1000.0f,
		1800.0f,
		EIGAudioBus::World);
	if (PowerCutStepIndex >= IGNightFour::PowerCutStepCount)
	{
		GetWorldTimerManager().ClearTimer(PowerCutStepTimer);
	}
}

void AIGMissingFloorNightFourDirector::OpenWallAfterFinalStrike()
{
	if (!bWallOpeningPending)
	{
		return;
	}
	bWallOpeningPending = false;
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	AIGPrologueWorldScene* SceneActor = Scene.Get();
	// 그 사이에 새벽이나 포획이 밤을 가져갔으면 판은 그대로 둔다. 표적은
	// RefreshPresentation이 상태대로 다시 켠다.
	if (!Narrative || !bHourCurrentlyActive || bFailureEndingActive
		|| !SceneActor || !SceneActor->OpenMissingFloorCavity())
	{
		RefreshPresentation();
		return;
	}
	Narrative->SetNightFourWallOpened(true);
	if (WallBreakTarget)
	{
		WallBreakTarget->SetInteractionEnabled(false);
		WallBreakTarget->SetActorHiddenInGame(true);
	}
	BeginCavityReveal();
	GetWorldTimerManager().SetTimer(
		WallDustTimer,
		this,
		&AIGMissingFloorNightFourDirector::SettleWallDust,
		IGNightFour::WallDustDelaySeconds,
		false);
	RefreshPresentation();
}

void AIGMissingFloorNightFourDirector::SettleWallDust()
{
	UWorld* World = GetWorld();
	if (!World || !bHourCurrentlyActive)
	{
		return;
	}
	// 내려앉은 판에서 가루가 올라온다. 빔 안에서는 보이고, 귀에는 가장 가는
	// 소리로 온다. 물이 멎은 자리에 파편 소리와 함께 남는다.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreatePlasterDustFall(this),
		IGNightFour::CavityDetailCenter,
		0.5f,
		1.0f,
		120.0f,
		900.0f,
		EIGAudioBus::Puzzle);
	if (UIGDustSubsystem* Dust = World->GetSubsystem<UIGDustSubsystem>())
	{
		Dust->ReportDisturbance(
			IGNightFour::WallBreakLocation + FVector(-40.0f, -45.0f, -60.0f), 1.0f);
		Dust->ReportDisturbance(
			IGNightFour::WallBreakLocation + FVector(-40.0f, 0.0f, -20.0f), 1.0f);
		Dust->ReportDisturbance(
			IGNightFour::WallBreakLocation + FVector(-40.0f, 45.0f, -60.0f), 1.0f);
	}
}

void AIGMissingFloorNightFourDirector::BuildConfrontationReplyLines(
	TArray<FText>& OutLines) const
{
	OutLines.Reset();
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}

	// 두 묶음으로 나눈다. 앞 묶음은 그가 한 일을 가리키고, 뒤 묶음은 그 일이
	// 누구에게 일어났는지를 가리킨다. 순서는 각 묶음 안에서 그를 겨냥하는
	// 정도이자, 사람 쪽에서는 도하와 가까운 정도다.
	struct FReplyEntry
	{
		EIGMissingFloorWitness Witness;
		FText Line;
	};

	const FReplyEntry Documents[] =
	{
		{
			EIGMissingFloorWitness::BoothSoundproofing,
			NSLOCTEXT(
				"IGMissingFloor",
				"ConfrontationReplyFoam",
				"관리실 안쪽 방이요. 문틈까지 계란판 붙여 놓으셨더라고요."),
		},
		{
			EIGMissingFloorWitness::BoothWallCalendar,
			NSLOCTEXT(
				"IGMissingFloor",
				"ConfrontationReplyCalendar",
				"달력이요. 26일에 동그라미 치고, 그 뒤로 한 장도 안 넘기셨죠."),
		},
		{
			EIGMissingFloorWitness::AnnexWorkGlove,
			NSLOCTEXT(
				"IGMissingFloor",
				"ConfrontationReplyGlove",
				"5층 장갑에 석고가 굳어 있던데요. 아저씨 거예요?"),
		},
		{
			EIGMissingFloorWitness::RecorderEmptyBay,
			NSLOCTEXT(
				"IGMissingFloor",
				"ConfrontationReplyRecorder",
				"녹화기 하드는 언제 빼셨어요?"),
		},
		{
			EIGMissingFloorWitness::BoothInnerRoomHum,
			NSLOCTEXT(
				"IGMissingFloor",
				"ConfrontationReplyInnerRoom",
				"안쪽 방에 뭘 계속 돌려 놓으셨던데요. 그래서 못 들으셨어요?"),
		},
		{
			EIGMissingFloorWitness::RoofDoorWind,
			NSLOCTEXT(
				"IGMissingFloor",
				"ConfrontationReplyWind",
				"옥상에서 바람 들어 봤어요. 바람이 둘, 쉬고, 하나로 불어요?"),
		},
	};

	const FReplyEntry Humans[] =
	{
		{
			EIGMissingFloorWitness::RooftopCigarettePack,
			NSLOCTEXT(
				"IGMissingFloor",
				"ConfrontationReplyPack",
				"탱크 옆에 꽁초 여섯 개요. 거기 앉아서 쉬던 사람이 있었다고요."),
		},
		{
			EIGMissingFloorWitness::HwangWaterBowl,
			NSLOCTEXT(
				"IGMissingFloor",
				"ConfrontationReplyBowl",
				"401호 할머니는 아직도 물그릇을 갈아 놓으세요. 매일요."),
		},
		{
			EIGMissingFloorWitness::SeoSleepingPills,
			NSLOCTEXT(
				"IGMissingFloor",
				"ConfrontationReplyPills",
				"서일영 씨 아세요? 골목에 그분 약봉투가 떨어져 있던데요."),
		},
	};

	TArray<FText> HeldDocuments;
	for (const FReplyEntry& Entry : Documents)
	{
		if (Narrative->HasWitness(Entry.Witness))
		{
			HeldDocuments.Add(Entry.Line);
		}
	}
	TArray<FText> HeldHumans;
	for (const FReplyEntry& Entry : Humans)
	{
		if (Narrative->HasWitness(Entry.Witness))
		{
			HeldHumans.Add(Entry.Line);
		}
	}

	// 서류는 최대 둘까지만 댄다. 셋을 연달아 대면 사람이 아니라 조서가 된다.
	const int32 DocumentQuota = FMath::Min(HeldDocuments.Num(), 2);
	for (int32 Index = 0; Index < DocumentQuota; ++Index)
	{
		OutLines.Add(HeldDocuments[Index]);
	}
	if (HeldHumans.Num() > 0)
	{
		OutLines.Add(HeldHumans[0]);
	}
	// 셋을 봤으면 셋을 말한다. 남은 자리는 서류 먼저, 그다음 사람으로
	// 채운다 — 이 순서라야 마지막 줄이 계속 사람 쪽에 남는다.
	for (int32 Index = DocumentQuota;
		Index < HeldDocuments.Num()
			&& OutLines.Num() < IGNightFour::ConfrontationReplyLimit;
		++Index)
	{
		OutLines.Add(HeldDocuments[Index]);
	}
	for (int32 Index = 1;
		Index < HeldHumans.Num()
			&& OutLines.Num() < IGNightFour::ConfrontationReplyLimit;
		++Index)
	{
		OutLines.Add(HeldHumans[Index]);
	}
}

void AIGMissingFloorNightFourDirector::RequestEndingChoiceAutosave()
{
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GetWorld();
	UIGSaveSubsystem* SaveSubsystem = GameInstance
		? GameInstance->GetSubsystem<UIGSaveSubsystem>()
		: nullptr;
	if (!SaveSubsystem || !World)
	{
		return;
	}
	SaveSubsystem->RequestAutosave(
		FGameplayTag::RequestGameplayTag(FName(TEXT("Chapter.MissingFloor")), false),
		World->GetOutermost()->GetFName(),
		FGameplayTag::RequestGameplayTag(
			FName(TEXT("Checkpoint.MissingFloor.EndingChoice")), false));
}

void AIGMissingFloorNightFourDirector::HandleEndingA(
	AIGMissingFloorEvidence* Evidence)
{
	FinishEnding(IGNightFour::EndingAId);
}

void AIGMissingFloorNightFourDirector::HandleEndingB(
	AIGMissingFloorEvidence* Evidence)
{
	FinishEnding(IGNightFour::EndingBId);
}

void AIGMissingFloorNightFourDirector::HandleNightFourCapture(APawn* Player)
{
	FailurePlayer = Cast<AIGPlayerCharacter>(Player);
	ResolveFailureEnding();
}

void AIGMissingFloorNightFourDirector::FinishEnding(const FName EndingId)
{
	// 05:30에 눈이 감기기 시작했으면 늦었다. 세계는 다 감긴 뒤에야 낮으로 바뀌어
	// 그 0.48초 동안 선택지가 아직 떠 있다. 여기서 받으면 B의 기다림은 곧 올 낮
	// 전환에 지워져 결말이 영영 오지 않고, A는 아침 독백과 겹친다.
	if (!IsHourLive())
	{
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->WasFirstReportMade()
		|| !Narrative->IsNightFourWallOpened()
		|| !Narrative->HasBeatPlayed(IGNightFour::FinalConfrontationBeat)
		|| !Narrative->SelectEnding(EndingId))
	{
		return;
	}
	// 선택이 났다. 대치의 드론을 붙든 손을 놓는다. A는 조율 걸음이 드론을 걷고,
	// B는 기다림이 Calm과 침묵으로 놓는다.
	if (UIGMissingFloorAudioSubsystem* AudioDirector =
		GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>())
	{
		AudioDirector->HoldFinaleScore(false);
	}

	const bool bEndingA = EndingId == IGNightFour::EndingAId;
	if (bEndingA)
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			AudioDirector->PlayEndingATuningResolution();
		}
		bEndingHammerMoving = EndingHammerVisual != nullptr;
		EndingHammerSeconds = 0.0f;
		if (EndingHammerVisual)
		{
			EndingHammerVisual->SetHiddenInGame(false, true);
			EndingHammerVisual->SetVisibility(true, true);
		}
		SetActorTickEnabled(true);
	}
	else
	{
		if (EndingPhoneVisual)
		{
			EndingPhoneVisual->SetHiddenInGame(false, true);
			EndingPhoneVisual->SetVisibility(true, true);
		}
		// §8 4-6 ②. 폰 녹음을 끄고 곁에 앉는다. A는 녹음을 켠 채 물러난다.
		if (UIGRecordingSubsystem* Recording =
			GetWorld()->GetSubsystem<UIGRecordingSubsystem>())
		{
			Recording->StopRecording();
		}
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateRelayClick(this),
			IGNightFour::EndingPhoneRest,
			0.35f,
			0.82f,
			80.0f,
			520.0f,
			EIGAudioBus::Puzzle);
		// 딸깍 하나로는 무엇을 껐는지 모른다. 켠 채 물러나는 A와 갈리는 자리다.
		AIGHorrorHUD::PushAudioCaptionAt(
			this,
			NSLOCTEXT("IGMissingFloor", "EndingBRecordingOffCaption", "폰 녹음 끄는 소리"),
			1.8f,
			IGNightFour::EndingPhoneRest);
	}
	GetWorldTimerManager().ClearTimer(RecordingLiftTimer);
	AIGHorrorHUD::PushThought(
		this,
		bEndingA
			? NSLOCTEXT(
				"IGMissingFloor",
				"EndingAChoiceThought",
				"여기 내려놓자. 밖에 나가서 사람을 불러야 해.")
			: NSLOCTEXT(
				"IGMissingFloor",
				"EndingBChoiceThought",
				"조금만 더 여기 있을게."),
		5.0f);
	RefreshPresentation();
	// B는 곁에 앉아 있는 시간을 먼저 산다. 결말은 05:30의 연결음과 함께
	// 알린다. 기다림을 세우지 못하면(프로브 포함) 바로 알린다.
	if (!bEndingA && BeginEndingBVigil())
	{
		return;
	}
	if (!bResolvedBroadcast)
	{
		bResolvedBroadcast = true;
		OnResolved.Broadcast();
	}
}

bool AIGMissingFloorNightFourDirector::BeginEndingBVigil()
{
	UWorld* World = GetWorld();
	AIGPlayerCharacter* PlayerCharacter =
		Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	// 프로브는 선택한 프레임의 상태를 본다. 기다림은 사람의 시간이다.
	if (!World || !PlayerCharacter || bEndingBVigilActive
		|| FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreyboxProbe")))
	{
		return false;
	}
	bEndingBVigilActive = true;
	bVigilDawnReached = false;
	VigilReplyCount = 0;
	VigilTapTimes.Reset();
	VigilPlayer = PlayerCharacter;

	// 벽 앞의 한 시간은 이 밤의 시계로 재지 않는다. 막간과 같은 방식으로 세운다.
	for (TActorIterator<AIGNightPhaseDirector> It(World); It; ++It)
	{
		It->SetHourPaused(true);
		break;
	}
	for (TActorIterator<AIGMissingFloorMercyDirector> It(World); It; ++It)
	{
		It->SetHourActive(false);
		break;
	}
	// 대치의 드론을 놓고 건물을 걷어 낸다. 남는 것은 그녀의 노크와 먼 대답이다.
	if (UIGMissingFloorAudioSubsystem* AudioDirector =
		World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
	{
		AudioDirector->SetThreatState(EIGAudioThreatState::Calm);
		AudioDirector->SetAuthoredSilence(true);
	}
	// 기다리는 동안에도 걸을 수 있다. 앉을지, 벽을 두드릴지는 플레이어가 정한다.
	GetWorldTimerManager().SetTimer(
		VigilDawnTimer,
		this,
		&AIGMissingFloorNightFourDirector::BeginVigilDawn,
		IGNightFour::VigilUnansweredSeconds,
		false);
	return true;
}

bool AIGMissingFloorNightFourDirector::RegisterVigilKnock()
{
	UWorld* World = GetWorld();
	if (!bEndingBVigilActive || !World)
	{
		return false;
	}
	const AIGPlayerCharacter* Character = VigilPlayer.Get();
	// 다른 곳에서 친 노크가 멀리 떨어진 공동 벽을 두드리지는 않는다.
	if (!Character || FVector::DistSquared(Character->GetActorLocation(),
		IGNightFour::VigilKnockLocation) > FMath::Square(180.0f))
	{
		VigilTapTimes.Reset();
		return false;
	}
	const double Now = World->GetTimeSeconds();
	// 탭과 소리는 같은 프레임이다(§18.5). 판정은 소리를 낸 뒤에 한다. 곁의
	// 벽이라 무엇을 보고 있든 같은 자리를 친다. 녹음은 탭마다 돌린다 — 시각
	// 해시는 4초 남짓마다 한 번 바뀌어서 1.2초 안의 세 탭이 한 녹음이 된다.
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleVariantOr(
			TEXT("Knock_Plaster"),
			3,
			(VigilTapSerial++ % 3u) << 12,
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateWallKnockSingle(this, 0.0f); }),
		IGNightFour::VigilKnockLocation,
		0.8f,
		1.0f,
		120.0f,
		1400.0f,
		EIGAudioBus::Player);
	// 대답이 오는 중이거나, 두 번 받았거나, 새벽이 왔으면 벽의 평범한 울림뿐이다.
	if (bVigilDawnReached
		|| VigilReplyCount >= IGNightFour::VigilReplyLimit
		|| GetWorldTimerManager().IsTimerActive(VigilReplyTimer))
	{
		VigilTapTimes.Reset();
		return true;
	}

	const UIGAccessibilitySubsystem* Accessibility = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	const double WindowScale = Accessibility
		? FMath::Max(1.0, static_cast<double>(Accessibility->GetKnockWindowScale()))
		: 1.0;
	if (VigilTapTimes.Num() > 0
		&& Now - VigilTapTimes.Last()
			> AIGListenerEntity::AnswerSequenceResetSeconds * WindowScale)
	{
		VigilTapTimes.Reset();
	}
	VigilTapTimes.Add(Now);
	while (VigilTapTimes.Num() > 3)
	{
		VigilTapTimes.RemoveAt(0);
	}
	if (VigilTapTimes.Num() < 3)
	{
		return true;
	}

	// P4와 같은 창이다. 벽 앞에서 배운 박자가 여기서도 그대로 통한다.
	const double PairInterval = VigilTapTimes[1] - VigilTapTimes[0];
	const double RestInterval = VigilTapTimes[2] - VigilTapTimes[1];
	const bool bPairAccepted =
		PairInterval >= AIGListenerEntity::AnswerPairMinSeconds
		&& PairInterval <= AIGListenerEntity::AnswerPairMaxSeconds * WindowScale;
	const bool bRestAccepted =
		RestInterval >= AIGListenerEntity::AnswerRestMinSeconds
		&& RestInterval <= AIGListenerEntity::AnswerRestMaxSeconds * WindowScale;
	if (!bPairAccepted || !bRestAccepted)
	{
		// 틀린 박자에는 아무 말도 하지 않는다(§18.5). 뒤의 둘이 짝이 될 수
		// 있으면 남겨서 다음 한 번으로 이어지게 한다.
		if (RestInterval >= AIGListenerEntity::AnswerPairMinSeconds
			&& RestInterval <= AIGListenerEntity::AnswerPairMaxSeconds * WindowScale)
		{
			VigilTapTimes.RemoveAt(0);
		}
		else
		{
			VigilTapTimes.Reset();
			VigilTapTimes.Add(Now);
		}
		return true;
	}
	VigilTapTimes.Reset();
	// 대답이 오기 전에 새벽이 끼어들지 않게 한다. 대답이 새벽을 다시 건다.
	GetWorldTimerManager().ClearTimer(VigilDawnTimer);
	GetWorldTimerManager().SetTimer(
		VigilReplyTimer,
		this,
		&AIGMissingFloorNightFourDirector::PlayVigilReply,
		IGNightFour::VigilReplyDelaySeconds,
		false);
	return true;
}

void AIGMissingFloorNightFourDirector::PlayVigilReply()
{
	if (!bEndingBVigilActive || bVigilDawnReached)
	{
		return;
	}
	++VigilReplyCount;
	const bool bLastReply = VigilReplyCount >= IGNightFour::VigilReplyLimit;
	// 복도 끝에서 대답 둘(§9 B). 공동 너머에서 한 번 들었던 그 노크이고,
	// 타이틀의 밤 5가 돌려주는 것이 이 순간이다. 두 번째는 조금 더 멀다.
	// 진동은 없다 — 내 손이 아니다(§18.5).
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateWallKnockReply(this),
		IGNightFour::CorridorEndKnockLocation,
		bLastReply ? 0.58f : 0.72f,
		0.84f,
		220.0f,
		2400.0f,
		EIGAudioBus::Entity);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "EndingBAnswerCaption", "복도 끝에서 대답하듯 두 번 두드리는 소리"),
		3.0f,
		IGNightFour::CorridorEndKnockLocation);
	GetWorldTimerManager().SetTimer(
		VigilDawnTimer,
		this,
		&AIGMissingFloorNightFourDirector::BeginVigilDawn,
		bLastReply
			? IGNightFour::VigilAfterLastReplySeconds
			: IGNightFour::VigilAfterFirstReplySeconds,
		false);
}

void AIGMissingFloorNightFourDirector::BeginVigilDawn()
{
	UWorld* World = GetWorld();
	if (!bEndingBVigilActive || bVigilDawnReached || !World)
	{
		return;
	}
	// 대답이 오는 중이면 새벽이 그 뒤로 비킨다. PlayVigilReply가 다시 건다.
	if (GetWorldTimerManager().IsTimerActive(VigilReplyTimer))
	{
		return;
	}
	bVigilDawnReached = true;
	VigilTapTimes.Reset();
	// 05:30. 걷어 냈던 건물이 먼저 돌아오고, 그 위로 아래층 잠금이 풀린다.
	if (UIGMissingFloorAudioSubsystem* AudioDirector =
		World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
	{
		AudioDirector->SetAuthoredSilence(false);
	}
	GetWorldTimerManager().SetTimer(
		VigilDawnTimer,
		this,
		&AIGMissingFloorNightFourDirector::PlayVigilLatch,
		IGNightFour::VigilLatchDelaySeconds,
		false);
}

void AIGMissingFloorNightFourDirector::PlayVigilLatch()
{
	if (!bEndingBVigilActive)
	{
		return;
	}
	// 매일 새벽 듣던 금속음이다. 1층에서 풀리고 계단실을 타고 올라온다.
	// 새벽 디렉터의 소리를 그대로 쓴다 — 여기서 들은 것이 이 밤의 05:30이고,
	// 에필로그 아래의 진짜 새벽은 소리를 내지 않는다.
	for (TActorIterator<AIGNightPhaseDirector> It(GetWorld()); It; ++It)
	{
		It->PlayDawnLatchCue();
		break;
	}
	GetWorldTimerManager().SetTimer(
		VigilDawnTimer,
		this,
		&AIGMissingFloorNightFourDirector::PlayVigilRingback,
		IGNightFour::VigilRingbackDelaySeconds,
		false);
}

void AIGMissingFloorNightFourDirector::PlayVigilRingback()
{
	if (!bEndingBVigilActive)
	{
		return;
	}
	// 신호가 돌아오자마자 건다. 두 번째 신고의 연결음이고, 받는 딸깍 뒤의
	// 목소리는 싣지 않는다.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateCallRingback(this),
		IGNightFour::EndingPhoneRest,
		0.5f,
		1.0f,
		60.0f,
		500.0f,
		EIGAudioBus::Player);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "EndingBRingbackCaption", "112 연결음"),
		3.0f,
		IGNightFour::EndingPhoneRest);
	EndEndingBVigil();
	// 결말은 여기서 알린다. 그레이박스는 연결음이 이어지는 3.4초를 기다렸다가
	// 에필로그를 연다. 연결음이 울리는 동안에도 이동할 수 있다.
	if (!bResolvedBroadcast)
	{
		bResolvedBroadcast = true;
		OnResolved.Broadcast();
	}
}

void AIGMissingFloorNightFourDirector::EndEndingBVigil()
{
	if (!bEndingBVigilActive)
	{
		return;
	}
	bEndingBVigilActive = false;
	VigilTapTimes.Reset();
	GetWorldTimerManager().ClearTimer(VigilReplyTimer);
	GetWorldTimerManager().ClearTimer(VigilDawnTimer);
}

bool AIGMissingFloorNightFourDirector::ResolveFailureEnding()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (bFailureEndingActive || !Narrative || !bHourCurrentlyActive
		|| Narrative->GetNightIndex() != 4
		|| Narrative->GetAggressionTier() < 3
		|| !Narrative->IsNightFourMaskRunning()
		|| !Narrative->SelectEnding(IGNightFour::EndingCId))
	{
		return false;
	}
	return CommitFailureEnding(/*bRecordCapture=*/true);
}

bool AIGMissingFloorNightFourDirector::ResolveDawnFailureEnding()
{
	// §20.4: 듣기만 하는 밤에는 포획이 없으므로 티어가 3에 닿지 않고, 위 경로로는
	// 엔딩 C에 영원히 도달할 수 없다. 그래서 조건을 밤4의 05:30 벽 미개방으로
	// 대체한다. 실패의 문이 완전히 닫히면 성공의 무게도 사라진다.
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !WouldResolveDawnFailureEnding())
	{
		return false;
	}
	if (!Narrative->SelectEnding(IGNightFour::EndingCId))
	{
		return false;
	}
	// 새벽 경로에는 포획 콜백이 없다. 암전·이동 잠금·매물 카드·기상 지점
	// 복원이 모두 이 폰으로 걸린다. 비어 있으면 화면이 복도에 선 채 멈추고
	// E는 전부 먹힌다.
	FailurePlayer = Cast<AIGPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	return CommitFailureEnding(/*bRecordCapture=*/false);
}

bool AIGMissingFloorNightFourDirector::WouldResolveDawnFailureEnding() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return !bFailureEndingActive
		&& Narrative
		&& Narrative->GetNightIndex() == 4
		&& !Narrative->IsNightFourWallOpened()
		&& Narrative->GetEndingChoice().IsNone();
}

bool AIGMissingFloorNightFourDirector::CommitFailureEnding(
	const bool bRecordCapture)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return false;
	}
	bFailureEndingActive = true;
	bFailureRetryEnabled = false;
	// The dawn route is not a catch. Recording one there would raise the tier
	// and inflate the capture count in a mode that has no captures at all.
	if (bRecordCapture)
	{
		Narrative->RecordCapture();
	}
	if (UWorld* World = GetWorld())
	{
		if (UIGMissingFloorAudioSubsystem* AudioDirector =
			World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
		{
			// 최종 리빌의 강제 무음은 공동 대치까지만 유효하다. 엔딩 C에서는
			// 가까운 롤러 소리를 내기 전에 WORLD 버스를 Calm 상태로 복구한다.
			AudioDirector->SetAuthoredSilence(false);
			AudioDirector->HoldFinaleScore(false);
			AudioDirector->SetThreatState(EIGAudioThreatState::Calm);
		}
	}

	for (TActorIterator<AIGNightPhaseDirector> It(GetWorld()); It; ++It)
	{
		It->SuspendForFailureEnding();
		break;
	}
	if (WaterMaskBed)
	{
		WaterMaskBed->FadeOut(0.16f, 0.0f);
	}
	SetRiserFlowPlaying(false, 0.16f);
	bWaterMaskActive = false;
	if (WaterMaskHumHandle != INDEX_NONE)
	{
		if (UIGNoiseSubsystem* Noise = GetWorld()->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->UnregisterHumSource(WaterMaskHumHandle);
		}
		WaterMaskHumHandle = INDEX_NONE;
	}
	if (AIGListenerEntity* ListenerActor = Listener.Get())
	{
		ListenerActor->SetDormant(true);
	}

	if (bRecordCapture)
	{
		if (AIGListenerEntity* ListenerActor = Listener.Get())
		{
			// 추적만 멈추고 이미 닿은 몸과 덮치는 동작은 암전까지 남긴다.
			ListenerActor->KeepCaptureVisible();
		}
	}
	if (AIGPlayerCharacter* Character = FailurePlayer.Get())
	{
		// 포획의 몸짓(끌어안기, 시선 회전)은 잡힌 사람에게만 준다. 새벽 경로에
		// 틀면 없었던 포획을 화면이 지어낸다. 이동 잠금과 암전은 두 길 모두다.
		if (bRecordCapture)
		{
			Character->PlayCaptureFeedback(IGNightFour::FailureCaptureSeconds);
		}
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		if (APlayerController* Controller =
			Cast<APlayerController>(Character->GetController()))
		{
			if (Controller->PlayerCameraManager)
			{
				Controller->PlayerCameraManager->StartCameraFade(
					0.0f,
					1.0f,
					IGNightFour::FailureCaptureSeconds,
					FLinearColor::Black,
					false,
					true);
			}
		}
	}
	// 가까운 두 번의 노크는 잡힌 길의 소리다. 새벽 길에는 노크가 없고, 그
	// 자리의 소리는 새벽 디렉터가 내는 공동현관 잠금이다 — 벽은 닫힌 채로
	// 건물만 열린다.
	if (bRecordCapture)
	{
		AIGHorrorHUD::PushAudioCaption(
			this,
			NSLOCTEXT("IGMissingFloor", "EndingCCaption", "아주 가까이서, 똑같이 두 번 두드리는 소리"),
			2.0f);
	}
	GetWorldTimerManager().SetTimer(
		FailureListingTimer,
		this,
		&AIGMissingFloorNightFourDirector::BeginFailureListing,
		IGNightFour::FailureListingDelaySeconds,
		false);
	GetWorldTimerManager().SetTimer(
		FailureRetryTimer,
		this,
		&AIGMissingFloorNightFourDirector::EnableFailureRetry,
		IGNightFour::FailureRetryDelaySeconds,
		false);
	RefreshPresentation();
	return true;
}

void AIGMissingFloorNightFourDirector::BeginFailureListing()
{
	if (!bFailureEndingActive)
	{
		return;
	}
	if (AIGListenerEntity* ListenerActor = Listener.Get())
	{
		ListenerActor->SetDormant(true);
		// 새벽 경로에서 새벽 디렉터가 잠든 채 세워 둔 몸도 여기서 거둔다. 이미
		// 잠들어 있어 SetDormant가 조기 반환하므로 숨김을 직접 건다.
		ListenerActor->SetActorHiddenInGame(true);
	}
	AIGPlayerCharacter* Character = FailurePlayer.Get();
	APlayerController* Controller = Character
		? Cast<APlayerController>(Character->GetController())
		: nullptr;
	if (AIGHorrorHUD* Hud = Controller
		? Cast<AIGHorrorHUD>(Controller->GetHUD())
		: nullptr)
	{
		Hud->BeginMissingFloorFailureEnding();
	}
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateWallpaperSeamRoller(this),
		Character ? Character->GetActorLocation() : GetActorLocation(),
		0.78f,
		0.96f,
		120.0f,
		520.0f,
		EIGAudioBus::World);
}

void AIGMissingFloorNightFourDirector::EnableFailureRetry()
{
	if (!bFailureEndingActive)
	{
		return;
	}
	bFailureRetryEnabled = true;
	AIGPlayerCharacter* Character = FailurePlayer.Get();
	APlayerController* Controller = Character
		? Cast<APlayerController>(Character->GetController())
		: nullptr;
	if (AIGHorrorHUD* Hud = Controller
		? Cast<AIGHorrorHUD>(Controller->GetHUD())
		: nullptr)
	{
		Hud->SetMissingFloorFailureRetryEnabled(true);
	}
}

bool AIGMissingFloorNightFourDirector::CompleteFailurePresentationForProbe()
{
	if (!bFailureEndingActive)
	{
		return false;
	}
	GetWorldTimerManager().ClearTimer(FailureListingTimer);
	GetWorldTimerManager().ClearTimer(FailureRetryTimer);
	BeginFailureListing();
	EnableFailureRetry();
	return bFailureRetryEnabled;
}

bool AIGMissingFloorNightFourDirector::RequestFailureRetry()
{
	if (!bFailureEndingActive)
	{
		return false;
	}
	if (bFailureRetryEnabled)
	{
		ResetAfterFailureEnding();
	}
	// 카드가 펼쳐지는 동안 상호작용을 소비해 전면 연출 뒤의 월드 대상까지
	// 입력이 새어 나가지 않게 한다.
	return true;
}

void AIGMissingFloorNightFourDirector::ResetAfterFailureEnding()
{
	if (!bFailureEndingActive || !bFailureRetryEnabled)
	{
		return;
	}
	ResetFinaleTimers();
	bFailureEndingActive = false;
	bFailureRetryEnabled = false;
	bResolvedBroadcast = false;
	bHydraulicAlarmTriggered = false;
	bFinalRevealActive = false;
	bFinalConfrontationComplete = false;
	bMokRetreatActive = false;
	bEndingHammerMoving = false;
	FinalRevealStage = INDEX_NONE;
	RevealAttentionSeconds = 0.0f;
	RevealStageElapsedSeconds = 0.0f;
	MokRetreatSeconds = 0.0f;
	EndingHammerSeconds = 0.0f;
	SetCavityRevealVisible(false);
	SetMokVisible(false);

	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (Narrative)
	{
		Narrative->ResetNightFourForRetry();
	}
	// 시간이 04:30으로 돌아가면 그 사이의 테이프도 없던 것이다. 다시 치는 첫
	// 망치가 새로 켠다.
	if (UIGRecordingSubsystem* Recording = GetWorld()
		? GetWorld()->GetSubsystem<UIGRecordingSubsystem>()
		: nullptr)
	{
		Recording->StopRecording();
		Recording->ClearTake();
	}
	if (AIGPrologueWorldScene* SceneActor = Scene.Get())
	{
		SceneActor->ResetMissingFloorCavity();
		SceneActor->SetMissingFloorAnnexPower(true);
	}
	if (WaterMaskBed)
	{
		WaterMaskBed->Stop();
	}
	SetRiserFlowPlaying(false);
	bWaterMaskActive = false;

	AIGPlayerCharacter* Character = FailurePlayer.Get();
	APlayerController* Controller = Character
		? Cast<APlayerController>(Character->GetController())
		: nullptr;
	if (AIGHorrorHUD* Hud = Controller
		? Cast<AIGHorrorHUD>(Controller->GetHUD())
		: nullptr)
	{
		Hud->EndMissingFloorFailureEnding();
	}
	if (!Character)
	{
		// 복구가 폰에 묶여 있다. 폰이 사라진 채로 여기 오면 암전과 이동
		// 잠금이 그대로 남는다.
		AbortFailureBlackout(TEXT("failure ending finished without a pawn"));
	}
	if (Character)
	{
		for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It)
		{
			It->RestorePlayerAtWakePoint(Character);
			break;
		}
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->SetMovementMode(MOVE_Walking);
		}
		if (Controller && Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StartCameraFade(
				1.0f,
				0.0f,
				0.85f,
				FLinearColor::Black,
				false,
				false);
		}
	}
	if (AIGListenerEntity* ListenerActor = Listener.Get())
	{
		ListenerActor->SetAggressionTier(1);
		if (ListenerActor->IsDormant())
		{
			ListenerActor->SetDormant(false);
		}
		else
		{
			ListenerActor->ResetToPatrolStart(false);
		}
	}
	RefreshPresentation();
	for (TActorIterator<AIGNightPhaseDirector> It(GetWorld()); It; ++It)
	{
		It->RestartTheHour(4);
		break;
	}
	FailurePlayer.Reset();
}

void AIGMissingFloorNightFourDirector::RefreshPresentation()
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative)
	{
		return;
	}
	const bool bNightFour = bHourCurrentlyActive
		&& Narrative->GetNightIndex() == 4
		&& !bFailureEndingActive;
	const bool bHasEnding = !Narrative->GetEndingChoice().IsNone();
	const bool bWallOpened = Narrative->IsNightFourWallOpened();
	bFinalConfrontationComplete = Narrative->HasBeatPlayed(
		IGNightFour::FinalConfrontationBeat);
	SetCavityRevealVisible(bWallOpened);

	// 요구서는 경찰이 다녀간 낮에 붙는다. 새벽 암전 프레임에 붙이면 관리인이
	// 05:30 정각에 복도에 서 있었던 셈이 된다. 붙는 순간의 소리는
	// PostEvictionNotice가 한 번 낸다.
	const bool bShowEviction = !bHourCurrentlyActive
		&& Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer)
		&& Narrative->HasBeatPlayed(IGNightFour::EvictionPostedBeat);
	EvictionNotice->SetActorHiddenInGame(!bShowEviction);
	EvictionNotice->SetInteractionEnabled(
		bShowEviction
		&& !Narrative->HasSource(
			EIGMissingFloorTruth::StillCoveringIt,
			EIGMissingFloorSource::EvictionWarning));

	auto RefreshControl = [bNightFour, Narrative, bLockout = bControlLockoutActive](
		AIGMissingFloorEvidence* Control,
		const FName ControlId)
	{
		if (!Control)
		{
			return;
		}
		const bool bActivated = Narrative->HasNightFourControl(ControlId);
		Control->SetActorHiddenInGame(false);
		Control->SetInteractionEnabled(bNightFour && !bActivated && !bLockout);
	};
	RefreshControl(CleaningDrain, IGNightFour::CleaningDrainId);
	RefreshControl(FloatBypass, IGNightFour::FloatBypassId);
	RefreshControl(TransferPump, IGNightFour::TransferPumpId);
	ValveTargetAngles[0] = Narrative->HasNightFourControl(IGNightFour::CleaningDrainId) ? -225.f : 0.f;
	ValveTargetAngles[1] = Narrative->HasNightFourControl(IGNightFour::FloatBypassId) ? -225.f : 0.f;
	bValveMotionActive = !FMath::IsNearlyEqual(ValveAngles[0], ValveTargetAngles[0], .01f)
		|| !FMath::IsNearlyEqual(ValveAngles[1], ValveTargetAngles[1], .01f);
	if (bValveMotionActive) { SetActorTickEnabled(true); }
	const bool bPumpRunning = bNightFour && Narrative->IsNightFourMaskRunning() && !bControlLockoutActive;
	if (PumpSelector)
	{
		PumpSelector->SetWorldRotation(FRotator(bPumpRunning ? -45.f : 0.f, 90.f, 0.f));
	}
	for (int32 Index = 0; Index < PumpLamps.Num(); ++Index)
	{
		const bool bLit = Index == 0 || (Index == 1 && bPumpRunning) || (Index == 2 && bControlLockoutActive);
		PumpLamps[Index]->SetScalarParameterValue(TEXT("Lit"), bLit ? 1.8f : 0.f);
	}


	// 3타의 정전은 CutAnnexPower가 낸다. 그 전에 여기서 끄면 망치가 떨어진
	// 프레임에 불이 나간다.
	if (Narrative->GetNightFourWallStrikeCount() >= 3
		&& !GetWorldTimerManager().IsTimerActive(PowerCutTimer))
	{
		if (AIGPrologueWorldScene* SceneActor = Scene.Get())
		{
			SceneActor->SetMissingFloorAnnexPower(!bNightFour);
		}
	}
	if (bWallOpened)
	{
		if (AIGPrologueWorldScene* SceneActor = Scene.Get())
		{
			SceneActor->OpenMissingFloorCavity();
		}
	}

	const bool bCanBreak = bNightFour
		&& Narrative->IsFinalChoiceUnlocked()
		&& Narrative->IsNightFourMaskRunning()
		&& !Narrative->IsNightFourWallOpened()
		&& !bWallOpeningPending
		&& !bHammerHeldForCall;
	WallBreakTarget->SetActorHiddenInGame(!bCanBreak);
	WallBreakTarget->SetInteractionEnabled(bCanBreak);

	const bool bCanChoose = bNightFour
		&& bWallOpened
		&& bFinalConfrontationComplete
		&& Narrative->WasFirstReportMade()
		&& !bHasEnding;
	EndingATarget->SetActorHiddenInGame(!bCanChoose);
	EndingATarget->SetInteractionEnabled(bCanChoose);
	EndingBTarget->SetActorHiddenInGame(!bCanChoose);
	EndingBTarget->SetInteractionEnabled(bCanChoose);
	if (bCanChoose && Narrative->MarkBeatPlayed(IGNightFour::ChoiceOfferedBeat))
	{
		// §22.4 리플레이 유인 2. 두 결말은 애도의 방식만 다르고 사실은
		// 같다(§9). 그 차이를 보려고 밤 4를 통째로 다시 하게 만들면
		// 「다시 오는 이유는 더 잘하기 위해서」라는 전제가 무너진다.
		RequestEndingChoiceAutosave();
	}
	if (EndingHammerVisual)
	{
		const bool bShowHammer = bCanChoose
			|| Narrative->GetEndingChoice() == IGNightFour::EndingAId;
		EndingHammerVisual->SetHiddenInGame(!bShowHammer, true);
		EndingHammerVisual->SetVisibility(bShowHammer, true);
		if (bCanChoose)
		{
			EndingHammerVisual->SetWorldLocationAndRotation(
				IGNightFour::EndingHammerStart,
				FRotator(0.0f, 18.0f, -76.0f));
		}
	}
	if (EndingPhoneVisual)
	{
		const bool bShowPhone =
			Narrative->GetEndingChoice() == IGNightFour::EndingBId;
		EndingPhoneVisual->SetHiddenInGame(!bShowPhone, true);
		EndingPhoneVisual->SetVisibility(bShowPhone, true);
	}

	if (bNightFour && !bWallOpened)
	{
		StartWaterMaskIfReady();
	}
	if (WaterMaskBed)
	{
		if (bNightFour && Narrative->IsNightFourMaskRunning() && !bWallOpened)
		{
			bWaterMaskActive = true;
			if (!WaterMaskBed->IsPlaying())
			{
				WaterMaskBed->Play();
			}
			SetRiserFlowPlaying(true);
		}
		else
		{
			bWaterMaskActive = false;
			WaterMaskBed->Stop();
			SetRiserFlowPlaying(false);
		}
	}
}

UIGMissingFloorNarrativeSubsystem*
AIGMissingFloorNightFourDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
}
