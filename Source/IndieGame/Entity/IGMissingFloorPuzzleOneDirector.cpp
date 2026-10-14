#include "Entity/IGMissingFloorPuzzleOneDirector.h"

#include "Audio/IGAmbienceSoundWave.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IGPrologueWorldScene.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interaction/IGReadableNote.h"
#include "Materials/MaterialInterface.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Player/IGHorrorHUD.h"
#include "TimerManager.h"

namespace IGPuzzleOne
{
	/**
	 * Lobby coordinates, matching the fixtures BuildLobby placed on the street
	 * wall. Duplicated here with intent: the scene's coordinate namespace is
	 * .cpp-local, so a director either duplicates the numbers or the scene
	 * grows an accessor per prop.
	 */
	const FVector FifthMeterFace(542.0f, -364.6f, 152.0f);
	// 왼쪽 아래 회로는 이름표가 비어 있다. 손잡이 축은 전원 상태와 무관하게 고정된다.
	const FVector BreakerFace(569.4f, -365.6f, 136.0f);
	const FVector CommonBreakerFace(582.7f, -365.6f, 151.0f);
	const FName UnpoweredObservation(TEXT("P1.Isolation.Unpowered"));
	const FName PoweredObservation(TEXT("P1.Isolation.Powered"));
	const FVector ReadingSheetLocation(401.0f, -361.2f, 150.0f);

	/**
	 * Eight centimeters above the 4F ceiling slab (world Z 1160), directly over
	 * the third corridor fixture, on the corridor centreline. A tight radius
	 * keeps it a corridor cue — "somewhere above the ceiling" — rather than a
	 * building-wide bed.
	 */
	const FVector BallastHumLocation(300.0f, -305.0f, 1168.0f);
	constexpr float BallastHumVolume = 0.28f;
	constexpr float BallastHumInnerRadius = 90.0f;
	constexpr float BallastHumFalloff = 620.0f;
	/**
	 * 켜지는 순간만 멀리 간다. 로비(Z 152)에서 이 자리까지 약 10.5 m라 슬래브
	 * 몇 장을 지나 먹먹하게, 위에서 내려온다. 웅은 새벽까지 남는다.
	 */
	constexpr float BallastCueInnerRadius = 300.0f;
	constexpr float BallastCueFalloff = 2600.0f;
	constexpr float BallastCueSecondTickSeconds = 0.55f;
	constexpr float BallastSwellVolume = 0.5f;

	/** Throwing a breaker is a deliberate, loud act. See §5.1. */
	constexpr float BreakerHoldSeconds = 0.6f;
	constexpr float BreakerNoiseLoudness = 0.55f;
	/** Leaning in to read a dial barely sounds at all. */
	constexpr float DialNoiseLoudness = 0.05f;

	const FName PuzzleId(TEXT("P1"));
	void SetToggle(UStaticMeshComponent* Toggle, bool bOn)
	{
		if (Toggle) Toggle->SetRelativeRotation(FRotator(0,180,bOn ? 32.f : -32.f));
	}
}

AIGMissingFloorPuzzleOneDirector::AIGMissingFloorPuzzleOneDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AIGMissingFloorPuzzleOneDirector::UpdateMeterMotion()
{
	if (!Scene.IsValid()) { return; }
	const float Interval = Scene->GetUtilityMeterUpdateInterval();
	if (!GetWorldTimerManager().IsTimerActive(MeterRotationTimer) || !FMath::IsNearlyEqual(Interval, MeterUpdateInterval))
	{
		MeterUpdateInterval = Interval;
		FTimerManagerTimerParameters Parameters;
		Parameters.bLoop = true;
		Parameters.bMaxOncePerFrame = true;
		GetWorldTimerManager().SetTimer(MeterRotationTimer, this,
			&AIGMissingFloorPuzzleOneDirector::AdvanceMeterDisc, Interval, Parameters);
	}
}

void AIGMissingFloorPuzzleOneDirector::AccumulateMeterMotion()
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Scene.IsValid())
	{
		const float Elapsed = LastMeterUpdateTime < 0.0 ? 0.f : static_cast<float>(Now-LastMeterUpdateTime);
		Scene->AdvanceUtilityMeters(42.f*Elapsed, bHourActive && bBreakerThrown, bCommonLightsEnabled);
	}
	LastMeterUpdateTime = Now;
}

void AIGMissingFloorPuzzleOneDirector::AdvanceMeterDisc()
{
	AccumulateMeterMotion();
	UpdateMeterMotion();
}

bool AIGMissingFloorPuzzleOneDirector::Configure(AIGPrologueWorldScene* InScene)
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
	// 검침 기록지는 금속 클립보드에 끼운 종이다. 사무실에서 계속 쓰는
	// 장부이므로 젖은 공지가 아니라 멀쩡한 종이를 쓴다. 예전에는 다섯째
	// 계량기 원판의 재질을 그대로 넘겨서, A4 한 장이 검은 플라스틱 판으로
	// 서 있었다.
	UMaterialInterface* SheetMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Prototype/Materials/M_LobbyMeterSheet.M_LobbyMeterSheet"));
	// Reuse the material the lobby already put on this fixture so the
	// interaction surface disappears into the prop it belongs to.
	UMaterialInterface* ToggleMaterial = nullptr;
	if (const UStaticMeshComponent* Toggle = InScene->GetUnnamedBreakerToggle())
	{
		ToggleMaterial = Toggle->GetMaterial(0);
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// The fifth dial. The thought does the work of the missing nameplate: the
	// player is told what they are looking at, never what it means.
	//
	// 이 액터는 그림을 갖지 않는다. 씬이 다섯 자리 모두에 문자판·유리·회전
	// 원판을 세워 두는데, 여기에 15cm 판을 하나 더 띄우면 그 셋을 통째로
	// 덮는다. 덮이는 것이 하필 P1이 보라고 하는 바로 그 계량기다 — 문자판도
	// 안 보이고, 「다섯째만 안 돈다」를 읽을 원판도 안 보인다. 상호작용만
	// 맡기고 그림은 씬에 맡긴다.
	SpawnParameters.Name = TEXT("MissingFloorMeterDial");
	MeterDialEvidence = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleOne::FifthMeterFace),
		SpawnParameters);
	if (!MeterDialEvidence)
	{
		return false;
	}
	MeterDialEvidence->Configure(
		CubeMesh,
		nullptr,
		FVector(15.0f, 3.0f, 15.0f),
		NSLOCTEXT("IGMissingFloor", "P1MeterPrompt", "계량기 원판 보기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		1.2f,
		IGPuzzleOne::DialNoiseLoudness,
		/*bPresentationVisible=*/false);
	MeterDialEvidence->OnExamined.AddUObject(
		this, &AIGMissingFloorPuzzleOneDirector::HandleMeterExamined);

	// The unnamed breaker. A hold, so throwing it is a decision rather than a
	// stray click — and the noise it makes is the price of checking.
	SpawnParameters.Name = TEXT("MissingFloorBreaker");
	BreakerAction = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleOne::BreakerFace),
		SpawnParameters);
	if (!BreakerAction)
	{
		return false;
	}
	BreakerAction->Configure(
		CubeMesh,
		ToggleMaterial,
		FVector(7.0f, 3.0f, 8.0f),
		NSLOCTEXT("IGMissingFloor", "P1BreakerPrompt", "이름표 없는 차단기"),
		FText::GetEmpty(),
		// Files nothing on its own: what it produces is a sound, and the sound
		// is what the player reasons from.
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		IGPuzzleOne::BreakerHoldSeconds,
		IGPuzzleOne::BreakerNoiseLoudness,
		// 씬이 세워 둔 미명칭 토글 위에 겹치는 대리물이다. 그림을 켜 두면
		// 진짜 토글을 가려, 내려간 것이 올라가는 것도 보이지 않는다.
		/*bPresentationVisible=*/false);
	BreakerAction->OnExamined.AddUObject(
		this, &AIGMissingFloorPuzzleOneDirector::HandleBreakerThrown);
	SpawnParameters.Name = TEXT("MissingFloorCommonLighting");
	CommonLightAction = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleOne::CommonBreakerFace), SpawnParameters);
	if (!CommonLightAction) { return false; }
	CommonLightAction->Configure(CubeMesh, nullptr, FVector(7, 3, 7),
		NSLOCTEXT("IGMissingFloor", "P1CommonPrompt", "복도등 끄기"),
		FText::GetEmpty(), EIGMissingFloorTruth::None, EIGMissingFloorSource::None,
		IGPuzzleOne::BreakerHoldSeconds, IGPuzzleOne::BreakerNoiseLoudness, false);
	CommonLightAction->OnExamined.AddUObject(this, &AIGMissingFloorPuzzleOneDirector::HandleCommonLighting);

	// The meter-reading sheet. Five columns; the fifth stops in July 2024.
	// Authored as padded rows for now — a real column presentation is a HUD
	// change and is tracked separately.
	SpawnParameters.Name = TEXT("MissingFloorReadingSheet");
	ReadingSheet = World->SpawnActor<AIGReadableNote>(
		AIGReadableNote::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGPuzzleOne::ReadingSheetLocation),
		SpawnParameters);
	if (!ReadingSheet)
	{
		return false;
	}
	ReadingSheet->ConfigurePrototypeVisuals(
		CubeMesh,
		SheetMaterial,
		FVector(21.0f, 0.08f, 29.7f));
	ReadingSheet->SetInteractionPrompt(
		NSLOCTEXT("IGMissingFloor", "P1SheetPrompt", "검침 기록지"));
	ReadingSheet->SetNoteText(
		NSLOCTEXT("IGMissingFloor", "P1SheetTitle", "달빛빌라 검침 기록"),
		{
			NSLOCTEXT("IGMissingFloor", "P1SheetHeader", "월 사용량(kWh) / 4월  5월  6월  7월"),
			NSLOCTEXT("IGMissingFloor", "P1Sheet401", "401    182   174   169   201"),
			NSLOCTEXT("IGMissingFloor", "P1Sheet402", "402    240   233   251   266"),
			NSLOCTEXT("IGMissingFloor", "P1Sheet403", "403    118   121   115   130"),
			NSLOCTEXT("IGMissingFloor", "P1Sheet404", "공용   97    102   99    104"),
			FText::GetEmpty(),
			NSLOCTEXT("IGMissingFloor", "P1SheetFifth", "(공란)  63    58    61    0"),
			FText::GetEmpty(),
			NSLOCTEXT(
				"IGMissingFloor",
				"P1SheetNote",
				"(공란)  24.07~ 0  검침 생략\n공용은 복도등 계량. 이름 없는 것은 확인 후 기입."),
		});
	ReadingSheet->OnReadStateChanged.AddDynamic(
		this, &AIGMissingFloorPuzzleOneDirector::HandleSheetRead);

	// 밤1의 목표는 회로가 아니라 T1이다. 회로는 확인이고, 확인은 두 기록이
	// 맞물린 뒤에야 뜻이 생긴다.
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		TruthHandle = Narrative->OnTruthConfirmed.AddUObject(
			this, &AIGMissingFloorPuzzleOneDirector::HandleTruthConfirmed);
	}

	CreateBallastHum();
	LastMeterUpdateTime = World->GetTimeSeconds();
	UpdateMeterMotion();
	return true;
}

void AIGMissingFloorPuzzleOneDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(MeterRotationTimer);
	GetWorldTimerManager().ClearTimer(DaytimeTripTimer);
	GetWorldTimerManager().ClearTimer(BallastCueTimer);
	if (UAudioComponent* Swell = BallastSwell.Get())
	{
		Swell->Stop();
	}
	BallastSwell.Reset();
	if (Scene.IsValid())
	{
		Scene->SetCommonInspectionLightsEnabled(true);
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->OnTruthConfirmed.Remove(TruthHandle);
	}
	if (BallastHum)
	{
		BallastHum->Stop();
	}
	Super::EndPlay(EndPlayReason);
}

void AIGMissingFloorPuzzleOneDirector::CreateBallastHum()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UIGAmbienceSoundWave* Wave =
		NewObject<UIGAmbienceSoundWave>(this, TEXT("FifthFloorBallastWave"));
	// The store's ballast buzz is literally the right sound: a fluorescent
	// fitting with a thin high whine. Reused rather than re-synthesized.
	Wave->Configure(EIGAmbienceMode::StoreBuzz, 0x5A17C0DEu);

	BallastHum = NewObject<UAudioComponent>(this, TEXT("FifthFloorBallastHum"));
	BallastHum->RegisterComponent();
	BallastHum->AttachToComponent(
		GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	BallastHum->SetWorldLocation(IGPuzzleOne::BallastHumLocation);
	BallastHum->SetSound(Wave);
	BallastHum->AttenuationSettings = IGAudio::MakeAttenuation(
		this,
		IGPuzzleOne::BallastHumInnerRadius,
		IGPuzzleOne::BallastHumFalloff,
		EIGAudioBus::World);
	BallastHum->bAllowSpatialization = true;
	BallastHum->bAutoActivate = false;
	BallastHum->SetVolumeMultiplier(IGPuzzleOne::BallastHumVolume);
	if (UIGMissingFloorAudioSubsystem* AudioDirector =
		World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
	{
		AudioDirector->RegisterComponent(BallastHum, EIGAudioBus::World);
	}
	// Deliberately not played: there is nothing above the ceiling until the
	// player gives that circuit power.
}

void AIGMissingFloorPuzzleOneDirector::HandleMeterExamined(
	AIGMissingFloorEvidence* Evidence)
{
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative) { return; }
	if (!bHourActive || bCommonLightsEnabled)
	{
		AIGHorrorHUD::PushThought(this,
			NSLOCTEXT("IGMissingFloor", "P1DialStill", "복도등 계량기는 따로 있는데. 이건 어디랑 연결된 거지?"), 3.8f);
		return;
	}
	Narrative->MarkBeatPlayed(bBreakerThrown ? IGPuzzleOne::PoweredObservation : IGPuzzleOne::UnpoweredObservation);
	AIGHorrorHUD::PushThought(this, bBreakerThrown
		? NSLOCTEXT("IGMissingFloor", "P1IsolatedRunning", "복도등은 껐는데 이건 돌아가네.")
		: NSLOCTEXT("IGMissingFloor", "P1IsolatedStopped", "이건 멈춰 있네."), 3.8f);
	if (Narrative->HasBeatPlayed(IGPuzzleOne::PoweredObservation)
		&& Narrative->HasBeatPlayed(IGPuzzleOne::UnpoweredObservation))
	{
		Narrative->RegisterTruthSource(EIGMissingFloorTruth::LivedUpstairs, EIGMissingFloorSource::MeterFifthDial);
	}
	AnnounceSolvedIfReady();
}

void AIGMissingFloorPuzzleOneDirector::HandleCommonLighting(AIGMissingFloorEvidence* Evidence)
{
	AccumulateMeterMotion();
	bCommonLightsEnabled = !bCommonLightsEnabled;
	if (AIGPrologueWorldScene* WorldScene = Scene.Get())
	{
		WorldScene->SetCommonInspectionLightsEnabled(bCommonLightsEnabled);
		IGPuzzleOne::SetToggle(WorldScene->GetCommonBreakerToggle(), bCommonLightsEnabled);
	}
	CommonLightAction->SetInteractionPrompt(bCommonLightsEnabled
		? NSLOCTEXT("IGMissingFloor", "P1CommonPrompt", "복도등 끄기")
		: NSLOCTEXT("IGMissingFloor", "P1CommonRestore", "복도등 켜기"));
	// 공용 조명 차단기도 0.55짜리 행동이다. 이름 없는 쪽보다 작은 손잡이라 조금 높다.
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateBreakerThrow(this),
		IGPuzzleOne::CommonBreakerFace, .9f, bCommonLightsEnabled ? 1.12f : 1.04f,
		120.f, 1400.f, EIGAudioBus::Puzzle);
	// 소리를 못 듣는 사람도 어느 차단기를 건드렸는지, 그게 얼마나 컸는지 알아야 비교가 된다.
	AIGHorrorHUD::PushAudioCaptionAt(this, bCommonLightsEnabled
		? NSLOCTEXT("IGMissingFloor", "P1CommonUpCaption", "[복도등 차단기를 올리는 소리]")
		: NSLOCTEXT("IGMissingFloor", "P1CommonDownCaption", "[복도등 차단기를 내리는 소리]"),
		2.2f, IGPuzzleOne::CommonBreakerFace);
}

void AIGMissingFloorPuzzleOneDirector::HandleBreakerThrown(
	AIGMissingFloorEvidence* Evidence)
{
	if (!bHourActive)
	{
		if (GetWorldTimerManager().IsTimerActive(DaytimeTripTimer)) { return; }
		if (Scene.IsValid()) IGPuzzleOne::SetToggle(Scene->GetUnnamedBreakerToggle(), true);
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateBreakerThrow(this),
			IGPuzzleOne::BreakerFace,
			1.0f,
			1.0f,
			120.0f,
			1400.0f,
			EIGAudioBus::Puzzle);
		GetWorldTimerManager().SetTimer(DaytimeTripTimer, this,
			&AIGMissingFloorPuzzleOneDirector::ResetDaytimeBreaker, .24f, false);
		return;
	}
	AccumulateMeterMotion();
	bBreakerThrown = !bBreakerThrown;
	UpdateMeterMotion();
	BreakerAction->SetInteractionPrompt(bBreakerThrown
		? NSLOCTEXT("IGMissingFloor", "P1BreakerLower", "이름표 없는 차단기 내리기")
		: NSLOCTEXT("IGMissingFloor", "P1BreakerRaise", "이름표 없는 차단기 올리기"));

	if (AIGPrologueWorldScene* WorldScene = Scene.Get())
	{
		IGPuzzleOne::SetToggle(WorldScene->GetUnnamedBreakerToggle(), bBreakerThrown);
	}

	// 이 밤에서 가장 시끄러운 손짓이다(0.55, §7 「차단기 소리는 위층까지 들린다」).
	// 올릴 때는 탁, 내릴 때는 조금 낮고 둔하게.
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreateBreakerThrow(this),
		IGPuzzleOne::BreakerFace,
		1.0f,
		bBreakerThrown ? 1.0f : 0.9f,
		120.0f,
		1400.0f,
		EIGAudioBus::Puzzle);
	AIGHorrorHUD::PushAudioCaptionAt(this, bBreakerThrown
		? NSLOCTEXT("IGMissingFloor", "P1BreakerUpCaption", "[이름표 없는 차단기를 탁 올리는 소리]")
		: NSLOCTEXT("IGMissingFloor", "P1BreakerDownCaption", "[이름표 없는 차단기를 내리는 소리]"),
		2.4f, IGPuzzleOne::BreakerFace);

	if (BallastHum)
	{
		if (bBreakerThrown) { BallastHum->Play(); }
		else { BallastHum->Stop(); }
		bBallastHumAudible = bBreakerThrown;
	}
	if (!bBreakerThrown)
	{
		return;
	}

	// 안정기 소리는 실제로 위층에 올라가야 들린다. 로비에서 원인을 먼저 설명하지 않는다.
	AnnounceSolvedIfReady();
}

void AIGMissingFloorPuzzleOneDirector::ResetDaytimeBreaker()
{
	if (Scene.IsValid()) IGPuzzleOne::SetToggle(Scene->GetUnnamedBreakerToggle(), false);
	// 떨어지는 쪽은 낮고 둔하다. 올린 손에 「탁 — 턱」으로 온다.
	IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateBreakerThrow(this),
		IGPuzzleOne::BreakerFace, .8f, .86f, 120.f, 1400.f, EIGAudioBus::Puzzle);
	AIGHorrorHUD::PushThought(this, NSLOCTEXT("IGMissingFloor", "P1BreakerDaytime", "올려도 다시 내려가네."), 3.4f);
}

void AIGMissingFloorPuzzleOneDirector::SetHourActive(const bool bActive)
{
	AccumulateMeterMotion();
	GetWorldTimerManager().ClearTimer(DaytimeTripTimer);
	// 맞물린 밤에 잠가 둔 차단기를 경계마다 돌려준다. 낮의 「올려도 다시
	// 떨어진다」도 이 손잡이다.
	if (BreakerAction)
	{
		BreakerAction->SetInteractionEnabled(true);
	}
	if (!bActive)
	{
		GetWorldTimerManager().ClearTimer(BallastCueTimer);
		if (UAudioComponent* Swell = BallastSwell.Get())
		{
			Swell->FadeOut(1.2f, 0.0f);
		}
		BallastSwell.Reset();
	}
	bHourActive = bActive;
	if (!bActive) bBreakerThrown = false;
	if (Scene.IsValid()) IGPuzzleOne::SetToggle(Scene->GetUnnamedBreakerToggle(), bBreakerThrown);
	UpdateMeterMotion();
	if (BallastHum)
	{
		bBallastHumAudible = bActive && bBreakerThrown;
		if (bBallastHumAudible) { BallastHum->Play(); }
		else { BallastHum->Stop(); }
	}
	if (!bActive && Scene.IsValid())
	{
		Scene->SetCommonInspectionLightsEnabled(true);
		bCommonLightsEnabled = true;
		IGPuzzleOne::SetToggle(Scene->GetCommonBreakerToggle(), true);
		CommonLightAction->SetInteractionPrompt(NSLOCTEXT("IGMissingFloor", "P1CommonPrompt", "복도등 끄기"));
		BreakerAction->SetInteractionPrompt(NSLOCTEXT("IGMissingFloor", "P1BreakerRaise", "이름표 없는 차단기 올리기"));
	}
}

void AIGMissingFloorPuzzleOneDirector::HandleTruthConfirmed(
	const EIGMissingFloorTruth Truth)
{
	if (Truth == EIGMissingFloorTruth::LivedUpstairs)
	{
		AnnounceSolvedIfReady();
	}
}

void AIGMissingFloorPuzzleOneDirector::AnnounceSolvedIfReady()
{
	// 불을 켰어도 그 위에 뭐가 있는지 모르면 밤은 안 끝난다. 계량기 다섯과
	// 검침표 다섯 칸이 맞물려야 「위층」이 된다.
	if (bSolvedAnnounced || !bBreakerThrown)
	{
		return;
	}
	UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	if (!Narrative || !Narrative->HasTruth(EIGMissingFloorTruth::LivedUpstairs))
	{
		return;
	}
	bSolvedAnnounced = true;
	// 저장에서 이어 붙인 세션에는 알린 기억이 없다. 이미 푼 퍼즐이면 밤2~4에
	// 차단기를 올려도 그냥 차단기다 — 여기서 잠그면 새벽까지 못 내리고 안정기
	// 웅만 남는다.
	if (Narrative->IsPuzzleSolved(IGPuzzleOne::PuzzleId))
	{
		return;
	}
	Narrative->MarkPuzzleSolved(IGPuzzleOne::PuzzleId);
	// 회로를 켜 둔 채로 새벽까지 간다(§7). 결론 뒤에 차단기를 도로 내리면
	// 방금 위에서 켜진 불이 이유 없이 꺼진다.
	if (BreakerAction)
	{
		BreakerAction->SetInteractionEnabled(false);
	}
	OnSolved.Broadcast();
}

void AIGMissingFloorPuzzleOneDirector::PlayBallastFromAbove()
{
	if (!bHourActive || !bBreakerThrown)
	{
		return;
	}
	PlayBallastTick(0.6f, 1.0f);
	AIGHorrorHUD::PushAudioCaptionAt(
		this,
		NSLOCTEXT("IGMissingFloor", "P1BallastFromAboveCaption", "천장 너머로 형광등 안정기 웅웅거리는 소리"),
		3.0f,
		IGPuzzleOne::BallastHumLocation);
	// 두 번째 딸깍에 안정기가 물고, 그 뒤로 웅이 남는다. 형광등이 켜지는 순서다.
	GetWorldTimerManager().SetTimer(
		BallastCueTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bHourActive || !bBreakerThrown)
			{
				return;
			}
			PlayBallastTick(0.42f, 0.96f);
			if (BallastSwell.IsValid())
			{
				return;
			}
			UIGAmbienceSoundWave* Wave = NewObject<UIGAmbienceSoundWave>(this);
			Wave->Configure(EIGAmbienceMode::StoreBuzz, 0x5A17C0DFu);
			// 루프 파형이다. SetHourActive(false)와 EndPlay가 끝낸다.
			BallastSwell = IGAudio::SpawnOneShotAt(
				this,
				Wave,
				IGPuzzleOne::BallastHumLocation,
				IGPuzzleOne::BallastSwellVolume,
				1.0f,
				IGPuzzleOne::BallastCueInnerRadius,
				IGPuzzleOne::BallastCueFalloff,
				EIGAudioBus::World);
		}),
		IGPuzzleOne::BallastCueSecondTickSeconds,
		false);
}

void AIGMissingFloorPuzzleOneDirector::PlayBallastTick(
	const float Volume,
	const float Pitch)
{
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(
			TEXT("Ballast_Tick"),
			[this]() -> USoundBase* { return UIGToneSequenceSoundWave::CreateFluorescentBallastSnap(this); }),
		IGPuzzleOne::BallastHumLocation,
		Volume,
		Pitch,
		IGPuzzleOne::BallastCueInnerRadius,
		IGPuzzleOne::BallastCueFalloff,
		EIGAudioBus::World);
}

void AIGMissingFloorPuzzleOneDirector::HandleSheetRead(
	AIGReadableNote* Note,
	const bool bOpened)
{
	// File on open, not on close: the player has seen the fifth column.
	if (!bOpened)
	{
		return;
	}
	if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
	{
		Narrative->RegisterTruthSource(
			EIGMissingFloorTruth::LivedUpstairs,
			EIGMissingFloorSource::MeterReadingSheet);
	}
}

bool AIGMissingFloorPuzzleOneDirector::ValidateFixtures() const
{
	const AIGPrologueWorldScene* WorldScene = Scene.Get();
	return WorldScene != nullptr
		&& WorldScene->GetFifthMeterDisc() != nullptr
		&& WorldScene->GetUnnamedBreakerToggle() != nullptr
		&& WorldScene->GetCommonBreakerToggle() != nullptr
		&& CommonLightAction != nullptr
		&& MeterDialEvidence != nullptr
		&& BreakerAction != nullptr
		&& ReadingSheet != nullptr
		&& BallastHum != nullptr;
}

UIGMissingFloorNarrativeSubsystem*
AIGMissingFloorPuzzleOneDirector::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
}
