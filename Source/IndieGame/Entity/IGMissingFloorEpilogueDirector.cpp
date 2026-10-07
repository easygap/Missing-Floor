#include "Entity/IGMissingFloorEpilogueDirector.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGReplaySkip.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IndieGame.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "TimerManager.h"

namespace IGEpilogue
{
	const FName EndingAId(TEXT("Ending.A"));
	const FName EndingBId(TEXT("Ending.B"));

	/**
	 * 두 엔딩의 시각표. 앞 네 큐는 §9 「공통 사실」의 몽타주라 같은 값이고,
	 * 그 뒤부터 갈린다. 마지막 항목은 끝 시각이며 장면이 아니다.
	 *
	 * 인덱스가 곧 FireCue의 case이므로 순서를 바꾸면 장면이 바뀐다. 시간만
	 * 만지고 싶으면 값만 고쳐라 — ValidateTimelines가 단조 증가를 지킨다.
	 */
	constexpr float EndingATimes[] =
	{
		0.0f,    // 0 몽타주 시작 (검은 화면)
		1.40f,   // 1 폴리스라인 테이프
		5.20f,   // 2 들것 바퀴
		9.60f,   // 3 카메라 셔터 셋
		13.40f,  // 4 석고 조각을 쓸어 담는 빗자루
		17.60f,  // 5 에필로그 1 — 도하의 공방
		21.20f,  // 6 유담이 조율 렌치를 잡는다 (정음까지 약 21초)
		45.00f,  // 7 에필로그 2 — 가을의 달빛빌라
		61.00f,  // 8 뉴스 자막
		75.00f,  // 9 마지막 카드
		87.00f,  // 10 끝
	};

	constexpr float EndingBTimes[] =
	{
		0.0f,    // 0 몽타주 시작
		1.40f,   // 1 폴리스라인 테이프
		5.20f,   // 2 들것 바퀴
		9.60f,   // 3 카메라 셔터 셋
		13.40f,  // 4 빗자루
		17.60f,  // 5 마지막 신 — 비어 있는 서비스 베이
		23.40f,  // 6 열쇠를 반납함에 넣는다
		29.60f,  // 7 5층 벽에서 둘, 쉬고, 하나
		36.20f,  // 8 난간 두 번
		45.00f,  // 9 뉴스 자막
		59.00f,  // 10 마지막 카드
		71.00f,  // 11 끝
	};

	/** 몽타주 네 소리는 방 안이 아니라 기억 속이라 거리를 두지 않는다. */
	constexpr float MontageInnerRadius = 4000.0f;
	constexpr float MontageFalloff = 6000.0f;

	/** §34.2의 손동작. 값은 IGReplaySkip이 든다. */
	constexpr float ReplaySkipDurationSeconds = IGReplaySkip::HoldSeconds;
	constexpr float ReplaySkipRewindMultiplier = IGReplaySkip::RewindMultiplier;
	constexpr const TCHAR* ProfileSection = TEXT("IndieGame.MissingFloorProfile");
	constexpr const TCHAR* ExperiencedKey = TEXT("EpilogueExperienced");
}

AIGMissingFloorEpilogueDirector::AIGMissingFloorEpilogueDirector()
{
	// 홀드 진행률을 그리는 동안에만 깨운다. 시각표 자체는 타이머가 민다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void AIGMissingFloorEpilogueDirector::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bActive || !bReplaySkipAvailable)
	{
		SetActorTickEnabled(false);
		return;
	}

	const float RequiredSeconds = GetReplaySkipDurationSeconds();
	const float SafeDelta = FMath::Max(DeltaSeconds, 0.0f);
	if (bReplaySkipInputActive)
	{
		ReplaySkipProgress = FMath::Min(
			ReplaySkipProgress + SafeDelta / RequiredSeconds,
			1.0f);
	}
	else if (bReplaySkipRewinding)
	{
		ReplaySkipProgress = FMath::Max(
			ReplaySkipProgress
				- SafeDelta * IGEpilogue::ReplaySkipRewindMultiplier / RequiredSeconds,
			0.0f);
		bReplaySkipRewinding = ReplaySkipProgress > 0.0f;
	}

	UpdateSkipHud();
	if (ReplaySkipProgress >= 1.0f)
	{
		SkipToFinalCard();
		return;
	}
	if (!bReplaySkipInputActive && !bReplaySkipRewinding)
	{
		SetActorTickEnabled(false);
	}
}

bool AIGMissingFloorEpilogueDirector::BeginReplaySkipInput()
{
	if (!bActive || !bReplaySkipAvailable)
	{
		return false;
	}
	if (UsesToggleSkipInput())
	{
		// 토글 모드에서는 누르는 순간 확정한다. 홀드가 힘든 사람에게
		// 「계속 누르고 있기」를 요구하지 않는 것이 §19.8의 계약이다.
		ReplaySkipProgress = 1.0f;
		bReplaySkipInputActive = false;
		bReplaySkipRewinding = false;
		SkipToFinalCard();
		return true;
	}
	bReplaySkipInputActive = true;
	bReplaySkipRewinding = false;
	SetActorTickEnabled(true);
	UpdateSkipHud();
	return true;
}

bool AIGMissingFloorEpilogueDirector::EndReplaySkipInput()
{
	if (!bActive || !bReplaySkipAvailable || !bReplaySkipInputActive)
	{
		return false;
	}
	bReplaySkipInputActive = false;
	bReplaySkipRewinding = ReplaySkipProgress > 0.0f;
	SetActorTickEnabled(bReplaySkipRewinding);
	UpdateSkipHud();
	return true;
}

void AIGMissingFloorEpilogueDirector::SkipToFinalCard()
{
	UWorld* World = GetWorld();
	if (!bActive || !World)
	{
		return;
	}
	const int32 CueCount = bEndingA
		? static_cast<int32>(UE_ARRAY_COUNT(IGEpilogue::EndingATimes))
		: static_cast<int32>(UE_ARRAY_COUNT(IGEpilogue::EndingBTimes));
	const int32 CardIndex = CueCount - 2;
	const int32 EndIndex = CueCount - 1;
	// 마지막 카드에서 다시 누르면 마친다. 이미 지나온 큐는 다시 울리지 않는다.
	if ((FiredCueMask & (1u << CardIndex)) != 0)
	{
		FinishEpilogue();
		return;
	}

	GetWorldTimerManager().ClearTimer(CueTimerHandle);
	bReplaySkipInputActive = false;
	bReplaySkipRewinding = false;
	ReplaySkipProgress = 0.0f;
	SetActorTickEnabled(false);
	UpdateSkipHud();

	// 지나친 큐는 재생하지 않고 소비만 한다. 몰아서 발화시키면 몽타주
	// 네 소리가 한 프레임에 겹쳐 터진다.
	for (int32 Index = 0; Index < CardIndex; ++Index)
	{
		FiredCueMask |= 1u << Index;
	}
	StopBeds();

	// 건너뛴 회차도 마지막 카드는 본다. 그 한 문장이 이 장면의 결론이라
	// 카드까지 지우면 엔딩을 안 본 것이 된다. 체류도 원래 길이 그대로다.
	FireCue(CardIndex);
	NextCueIndex = EndIndex;
	const float CardHoldSeconds = bEndingA
		? IGEpilogue::EndingATimes[EndIndex] - IGEpilogue::EndingATimes[CardIndex]
		: IGEpilogue::EndingBTimes[EndIndex] - IGEpilogue::EndingBTimes[CardIndex];
	World->GetTimerManager().SetTimer(
		CueTimerHandle,
		this,
		&AIGMissingFloorEpilogueDirector::HandleNextCue,
		FMath::Max(CardHoldSeconds, 1.0f),
		false);
}

void AIGMissingFloorEpilogueDirector::UpdateSkipHud() const
{
	const AIGPlayerCharacter* PlayerCharacter = Player.Get();
	const APlayerController* Controller = PlayerCharacter
		? Cast<APlayerController>(PlayerCharacter->GetController())
		: nullptr;
	if (AIGHorrorHUD* Hud = Controller
		? Cast<AIGHorrorHUD>(Controller->GetHUD())
		: nullptr)
	{
		Hud->SetSensoryInterludeSkipState(
			bActive && bReplaySkipAvailable,
			bActive ? ReplaySkipProgress : 0.0f,
			bActive && bReplaySkipInputActive,
			GetReplaySkipDurationSeconds(),
			UsesToggleSkipInput());
	}
}

float AIGMissingFloorEpilogueDirector::GetReplaySkipDurationSeconds() const
{
	const UIGAccessibilitySubsystem* Accessibility = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	const float DurationScale = Accessibility
		? Accessibility->GetHoldDurationScale()
		: 1.0f;
	return FMath::Max(
		IGEpilogue::ReplaySkipDurationSeconds * DurationScale,
		0.25f);
}

bool AIGMissingFloorEpilogueDirector::UsesToggleSkipInput() const
{
	const UIGAccessibilitySubsystem* Accessibility = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>()
		: nullptr;
	return Accessibility && Accessibility->UsesToggleHoldInteractions();
}

bool AIGMissingFloorEpilogueDirector::HasExperiencedEpilogueProfile() const
{
	bool bExperienced = false;
	if (GConfig)
	{
		GConfig->GetBool(
			IGEpilogue::ProfileSection,
			IGEpilogue::ExperiencedKey,
			bExperienced,
			GGameUserSettingsIni);
	}
	return bExperienced;
}

void AIGMissingFloorEpilogueDirector::PersistEpilogueExperience() const
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetBool(
		IGEpilogue::ProfileSection,
		IGEpilogue::ExperiencedKey,
		true,
		GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

bool AIGMissingFloorEpilogueDirector::StartEpilogue(
	AIGPlayerCharacter* InPlayer,
	const FName EndingId)
{
	if (bActive || !IsValid(InPlayer) || !GetWorld())
	{
		return false;
	}
	if (EndingId != IGEpilogue::EndingAId && EndingId != IGEpilogue::EndingBId)
	{
		// 엔딩 C는 매물 화면과 밤4 재도전을 스스로 소유한다. 여기로 들어오면
		// 실패가 애도로 이어지는 그림이 되어 §9의 세 결말이 섞인다.
		return false;
	}

	Player = InPlayer;
	// 숨어 있었으면 먼저 꺼낸다. 에필로그가 이동을 끈 뒤에 숨는 자리가 되살리지 않게.
	InPlayer->LeaveHidingSpotImmediately();
	ActiveEndingId = EndingId;
	bEndingA = EndingId == IGEpilogue::EndingAId;
	FiredCueMask = 0;
	NextCueIndex = 1;
	PlayedSceneCount = 0;
	ReplaySkipProgress = 0.0f;
	bReplaySkipInputActive = false;
	bReplaySkipRewinding = false;
	bReplayForcedForSession =
		FParse::Param(FCommandLine::Get(), TEXT("IGEpilogueReplay"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGListenerGreyboxProbe"));
	bReplaySkipAvailable =
		HasExperiencedEpilogueProfile() || bReplayForcedForSession;
	bActive = true;
	StartWorldSeconds = GetWorld()->GetTimeSeconds();
	if (!bReplaySkipAvailable)
	{
		// §6. 처음 보는 결말도 10초가 지나면 E를 길게 눌러 넘길 수 있다. 결말을
		// 끝까지 보게 하는 것은 연출의 힘이어야지 잠긴 입력이어서는 안 된다.
		GetWorldTimerManager().SetTimer(
			FirstViewSkipTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (!bActive)
				{
					return;
				}
				bReplaySkipAvailable = true;
				UpdateSkipHud();
			}),
			IGReplaySkip::FirstViewEpilogueDelaySeconds,
			false);
	}

	// 최종 리빌의 강제 무음은 공동 대치까지다. 에필로그는 다른 시간이고
	// 다른 장소이므로 밤의 믹스를 그대로 끌고 가지 않는다.
	if (UIGMissingFloorAudioSubsystem* AudioDirector =
		GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>())
	{
		AudioDirector->SetAuthoredSilence(false);
		// Calm으로 넘기면 SwitchScore가 밤의 스코어를 먼저 놓는다. 여기에
		// 남는 스코어는 없고, 다음 장면이 자기 것을 새로 건다.
		AudioDirector->SetThreatState(EIGAudioThreatState::Calm);
		// 선택하는 순간 이미 Calm이라(A는 조율 걸음이, B는 기다림이 대치의 드론을
		// 놓는다) 위 줄은 대개 아무것도 걷지 않는다. 벽 앞에서 시작한 조율 걸음을
		// 암전(1.10초)과 함께 여기서 감는다. 정음은 공방에서 처음 닿는다(§10.1).
		AudioDirector->ReleaseScore(1.10f);
	}

	InPlayer->GetCharacterMovement()->DisableMovement();
	if (APlayerController* Controller =
		Cast<APlayerController>(InPlayer->GetController()))
	{
		if (Controller->PlayerCameraManager)
		{
			// 장면이 바뀌는 것이지 게임이 멈추는 것이 아니다. 페이드는
			// 유지되며, 돌아오지 않는다 — 마지막 카드 뒤가 타이틀이다.
			Controller->PlayerCameraManager->StartCameraFade(
				0.0f,
				1.0f,
				1.10f,
				FLinearColor::Black,
				false,
				true);
		}
	}

	FireCue(0);
	ScheduleNextCue();
	UpdateSkipHud();
	// 안내만 떠 있을 때는 정적이다. 홀드하거나 되감을 때만 Tick을 켠다.
	SetActorTickEnabled(false);
	return true;
}

void AIGMissingFloorEpilogueDirector::ScheduleNextCue()
{
	UWorld* World = GetWorld();
	const int32 CueCount = bEndingA
		? static_cast<int32>(UE_ARRAY_COUNT(IGEpilogue::EndingATimes))
		: static_cast<int32>(UE_ARRAY_COUNT(IGEpilogue::EndingBTimes));
	if (!bActive || !World || NextCueIndex >= CueCount)
	{
		return;
	}

	// 막간과 같은 규칙이다. 각 큐를 앞 큐가 아니라 시작 시각에 맞춰 예약해야
	// 한 프레임의 지연이 87초 동안 쌓이지 않는다.
	const float CueTime = bEndingA
		? IGEpilogue::EndingATimes[NextCueIndex]
		: IGEpilogue::EndingBTimes[NextCueIndex];
	const double WorldElapsed = World->GetTimeSeconds() - StartWorldSeconds;
	const float Delay = FMath::Max(
		static_cast<float>(CueTime - WorldElapsed),
		KINDA_SMALL_NUMBER);
	World->GetTimerManager().SetTimer(
		CueTimerHandle,
		this,
		&AIGMissingFloorEpilogueDirector::HandleNextCue,
		Delay,
		false);
}

void AIGMissingFloorEpilogueDirector::HandleNextCue()
{
	const int32 CueCount = bEndingA
		? static_cast<int32>(UE_ARRAY_COUNT(IGEpilogue::EndingATimes))
		: static_cast<int32>(UE_ARRAY_COUNT(IGEpilogue::EndingBTimes));
	if (!bActive || NextCueIndex >= CueCount)
	{
		return;
	}
	FireCue(NextCueIndex);
	++NextCueIndex;
	ScheduleNextCue();
}

void AIGMissingFloorEpilogueDirector::PlayMontageCue(const int32 MontageIndex)
{
	const FVector Origin = Player.IsValid()
		? Player->GetActorLocation()
		: GetActorLocation();

	USoundBase* Sound = nullptr;
	FText Caption;
	float Volume = 0.7f;
	switch (MontageIndex)
	{
	case 0:
		Sound = UIGToneSequenceSoundWave::CreatePoliceLineTapePull(this);
		Volume = 0.62f;
		Caption = NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueMontageTape",
			"[테이프 뜯는 소리]");
		break;
	case 1:
		Sound = UIGToneSequenceSoundWave::CreateGurneyWheels(this);
		Volume = 0.58f;
		Caption = NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueMontageGurney",
			"[바퀴 소리가 복도를 지나 멀어진다]");
		break;
	case 2:
		Sound = UIGToneSequenceSoundWave::CreateCameraShutterTriple(this);
		Volume = 0.55f;
		Caption = NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueMontageShutter",
			"[카메라 셔터 소리, 세 번]");
		break;
	case 3:
		Sound = UIGToneSequenceSoundWave::CreateDebrisSweep(this);
		Volume = 0.60f;
		Caption = NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueMontageSweep",
			"[석고 조각 쓸어 담는 소리]");
		break;
	default:
		return;
	}

	IGAudio::SpawnOneShotAt(
		this,
		Sound,
		Origin,
		Volume,
		1.0f,
		IGEpilogue::MontageInnerRadius,
		IGEpilogue::MontageFalloff,
		EIGAudioBus::World);
	// 화면이 검은 구간이므로 자막이 유일한 시각 정보다. 청각 접근성을 켠
	// 플레이어에게는 이 네 줄이 몽타주 그 자체다(§19.8).
	AIGHorrorHUD::PushAudioCaption(this, Caption, 2.4f);
}

void AIGMissingFloorEpilogueDirector::FireCue(const int32 CueIndex)
{
	if (CueIndex < 0 || CueIndex >= 32
		|| (FiredCueMask & (1u << CueIndex)) != 0)
	{
		return;
	}
	FiredCueMask |= 1u << CueIndex;

	// 큐가 흐르는 동안 월드가 사라질 수 있다. 아래 두 스코어가 오디오
	// 서브시스템을 월드에서 찾으므로 여기서 한 번만 확인한다.
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	APlayerController* Controller = PlayerCharacter
		? Cast<APlayerController>(PlayerCharacter->GetController())
		: nullptr;
	AIGHorrorHUD* Hud = Controller
		? Cast<AIGHorrorHUD>(Controller->GetHUD())
		: nullptr;
	const FVector Origin = PlayerCharacter
		? PlayerCharacter->GetActorLocation()
		: GetActorLocation();

	auto PresentScene = [this, Hud](
		const EIGMissingFloorEpilogueScene Scene,
		const FText& Heading,
		const TArray<FText>& Lines,
		const FText& Footnote)
	{
		++PlayedSceneCount;
		if (Hud)
		{
			Hud->BeginMissingFloorEpilogueScene(Scene, Heading, Lines, Footnote);
		}
	};

	// 몽타주 네 소리는 두 엔딩이 같은 인덱스에 둔다.
	if (CueIndex == 0)
	{
		PresentScene(
			EIGMissingFloorEpilogueScene::Montage,
			FText::GetEmpty(),
			TArray<FText>(),
			FText::GetEmpty());
		return;
	}
	if (CueIndex >= 1 && CueIndex <= 4)
	{
		PlayMontageCue(CueIndex - 1);
		return;
	}

	if (bEndingA)
	{
		switch (CueIndex)
		{
		case 5:
			PresentScene(
				EIGMissingFloorEpilogueScene::Workshop,
				NSLOCTEXT(
					"IGMissingFloor",
					"EpilogueWorkshopHeading",
					"장례를 치르고, 공방"),
				BuildWorkshopLines(),
				NSLOCTEXT(
					"IGMissingFloor",
					"EpilogueWorkshopFootnote",
					"공방 벽에 업라이트 피아노 한 대가 그대로 서 있다"));
			return;

		case 6:
			// §10.1의 계약이 여기서 닫힌다. 게임 내내 -30센트에 머물던
			// 모티프가 이 한 번만 정음에 닿는다.
			if (ScoreBed)
			{
				ScoreBed->Stop();
			}
			ScoreBed = NewObject<UAudioComponent>(this, TEXT("EpilogueWorkshopScore"));
			if (ScoreBed)
			{
				ScoreBed->RegisterComponent();
				ScoreBed->SetSound(
					UIGToneSequenceSoundWave::CreateEpilogueWorkshopScore(this));
				ScoreBed->bAllowSpatialization = false;
				ScoreBed->bAutoDestroy = false;
				ScoreBed->SetVolumeMultiplier(0.86f);
				if (UIGMissingFloorAudioSubsystem* AudioDirector =
					World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
				{
					// 게임에서 한 번뿐인 정음이다. 원샷에 밀리지 않게 베드로 건다.
					AudioDirector->RegisterPersistentBed(ScoreBed, EIGAudioBus::Score);
				}
				ScoreBed->Play();
			}
			AIGHorrorHUD::PushAudioCaption(
				this,
				NSLOCTEXT(
					"IGMissingFloor",
					"EpilogueTuningCaption",
					"[같은 건반을 다시 누른다. 이번엔 음이 맞는다]"),
				4.0f);
			return;

		case 7:
			if (ScoreBed)
			{
				ScoreBed->Stop();
			}
			ScoreBed = NewObject<UAudioComponent>(this, TEXT("EpilogueAutumnBed"));
			if (ScoreBed)
			{
				ScoreBed->RegisterComponent();
				ScoreBed->SetSound(
					UIGToneSequenceSoundWave::CreateEpilogueAutumnBed(this));
				ScoreBed->bAllowSpatialization = false;
				ScoreBed->bAutoDestroy = false;
				ScoreBed->SetVolumeMultiplier(0.62f);
				if (UIGMissingFloorAudioSubsystem* AudioDirector =
					World->GetSubsystem<UIGMissingFloorAudioSubsystem>())
				{
					AudioDirector->RegisterPersistentBed(ScoreBed, EIGAudioBus::World);
				}
				ScoreBed->Play();
			}
			PresentScene(
				EIGMissingFloorEpilogueScene::Autumn,
				NSLOCTEXT(
					"IGMissingFloor",
					"EpilogueAutumnHeading",
					"가을, 무영로"),
				BuildAutumnLines(),
				NSLOCTEXT(
					"IGMissingFloor",
					"EpilogueAutumnFootnote",
					"네 시 반 알람은 지웠다."));
			return;

		case 8:
			PresentScene(
				EIGMissingFloorEpilogueScene::News,
				NSLOCTEXT("IGMissingFloor", "EpilogueNewsHeading", "뉴스"),
				BuildNewsLines(),
				FText::GetEmpty());
			return;

		case 9:
			StopBeds();
			PresentScene(
				EIGMissingFloorEpilogueScene::Card,
				FText::GetEmpty(),
				{NSLOCTEXT(
					"IGMissingFloor",
					"EpilogueCardA",
					"조율이 끝난 방")},
				FText::GetEmpty());
			return;

		case 10:
			FinishEpilogue();
			return;

		default:
			return;
		}
	}

	switch (CueIndex)
	{
	case 5:
		PresentScene(
			EIGMissingFloorEpilogueScene::ServiceBay,
			NSLOCTEXT(
				"IGMissingFloor",
				"EpilogueServiceBayHeading",
				"오빠를 꺼낸 날, 5층"),
			BuildServiceBayLines(),
			NSLOCTEXT(
				"IGMissingFloor",
				"EpilogueServiceBayFootnote",
				"소리 일지는 401호 할머니께 돌려드렸다"));
		return;

	case 6:
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateKeyDropMetalBox(this),
			Origin,
			0.72f,
			1.0f,
			IGEpilogue::MontageInnerRadius,
			IGEpilogue::MontageFalloff,
			EIGAudioBus::Puzzle);
		AIGHorrorHUD::PushAudioCaption(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"EpilogueKeyDropCaption",
				"[열쇠가 반납함 바닥에 떨어지는 소리]"),
			2.4f);
		return;

	case 7:
		// 아무도 없는 5층에서 온다. 이 게임에서 마지막으로 울리는 벽이고, 그 집의
		// 신호인 둘-쉬고-하나다. 네 밤 동안 그가 찾아다니며 치던 고른 세 번을 여기
		// 두면 자막과 반대로 들린다. 6.5m 위에 두어 거리만큼 리버브가 젖게 하고,
		// 멀어진 만큼은 볼륨으로 메운다.
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateAnswerKnockPattern(this, 0.55f),
			Origin + FVector(0.0f, 0.0f, 650.0f),
			0.80f,
			1.0f,
			200.0f,
			3200.0f,
			EIGAudioBus::Entity);
		AIGHorrorHUD::PushAudioCaption(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"EpilogueFinalKnockCaption",
				"[위] 둘, 쉬고, 하나"),
			3.0f);
		return;

	case 8:
		// 돌아가지 않는다. 대답만 하고 내려간다.
		IGAudio::SpawnOneShotAt(
			this,
			UIGToneSequenceSoundWave::CreateRailingKnockTwo(this),
			Origin,
			0.78f,
			1.0f,
			IGEpilogue::MontageInnerRadius,
			IGEpilogue::MontageFalloff,
			EIGAudioBus::Player);
		AIGHorrorHUD::PushAudioCaption(
			this,
			NSLOCTEXT(
				"IGMissingFloor",
				"EpilogueRailingCaption",
				"[난간을 두 번 두드리는 소리]"),
			2.6f);
		return;

	case 9:
		PresentScene(
			EIGMissingFloorEpilogueScene::News,
			NSLOCTEXT("IGMissingFloor", "EpilogueNewsHeadingB", "뉴스"),
			BuildNewsLines(),
			FText::GetEmpty());
		return;

	case 10:
		StopBeds();
		PresentScene(
			EIGMissingFloorEpilogueScene::Card,
			FText::GetEmpty(),
			{NSLOCTEXT(
				"IGMissingFloor",
				"EpilogueCardB",
				"같이 있던 밤")},
			FText::GetEmpty());
		return;

	case 11:
		FinishEpilogue();
		return;

	default:
		return;
	}
}

TArray<FText> AIGMissingFloorEpilogueDirector::BuildWorkshopLines() const
{
	TArray<FText> Lines;
	Lines.Add(NSLOCTEXT(
		"IGMissingFloor",
		"EpilogueWorkshop1",
		"오빠가 쓰던 작업표가 아직 여기 있네."));
	Lines.Add(NSLOCTEXT(
		"IGMissingFloor",
		"EpilogueWorkshop2",
		"업라이트 1대  ·  받는 분 백유담  ·  집들이 전에 전달"));
	Lines.Add(FText::GetEmpty());
	Lines.Add(NSLOCTEXT(
		"IGMissingFloor",
		"EpilogueWorkshop3",
		"튜닝 해머를 쥐어 본다. 손잡이 한쪽이 닳아 있다."));
	return Lines;
}

TArray<FText> AIGMissingFloorEpilogueDirector::BuildAutumnLines() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	TArray<FText> Lines;
	Lines.Add(NSLOCTEXT(
		"IGMissingFloor",
		"EpilogueAutumn1",
		"5층을 뜯어내고 있다. 크레인이 하루 종일 골목을 막고 서 있다."));
	// 402호 문 너머의 없던 소리를 들어 본 회차만, 그 집이 다시 찬 것을
	// 알아본다. 못 들었으면 그냥 창문 하나가 더 밝을 뿐이다.
	if (Narrative
		&& Narrative->HasWitness(EIGMissingFloorWitness::Unit402Silence))
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueAutumn1Vacancy",
			"402호에 불이 켜져 있다. 냉장고 소리가 복도까지 들린다."));
	}
	if (Narrative
		&& Narrative->HasWitness(EIGMissingFloorWitness::HwangWaterBowl))
	{
		// 물그릇을 본 회차에만 그 자리가 무엇이었는지 말할 수 있다.
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueAutumn2Seen",
			"물그릇이 있던 401호 앞을 지난다. 창턱에 작은 라디오가 놓여 있다."));
	}
	else
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueAutumn2",
			"401호 창턱에 작은 라디오가 창밖을 향해 놓여 있다."));
	}
	// 문 너머로 그 소리를 들어 본 적이 있으면 알아본다. 없으면 그냥
	// 창밖을 향해 놓인 라디오이고, 그것도 같은 그림이다.
	if (Narrative
		&& Narrative->HasWitness(EIGMissingFloorWitness::Unit401DoorRadio))
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueAutumn3Radio",
			"문 너머로 듣던 그 불경 소리다. 창밖으로 들리게 틀어 놓으셨네."));
	}
	else
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueAutumn3Seen",
			"라디오를 창밖 쪽으로 돌려놓으셨다. 골목까지 들리겠다."));
	}
	return Lines;
}

TArray<FText> AIGMissingFloorEpilogueDirector::BuildServiceBayLines() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	TArray<FText> Lines;
	Lines.Add(NSLOCTEXT(
		"IGMissingFloor",
		"EpilogueServiceBay1",
		"방수포도 카트도 치워졌다. 콘크리트에 자국만 남았다."));
	// B의 마지막 그림. 벽 앞에 앉아 있던 한 시간 사이에 그는 나갔다.
	Lines.Add(NSLOCTEXT(
		"IGMissingFloor",
		"EpilogueServiceBay1b",
		"관리실 창은 그날 밤 뒤로 불이 한 번도 안 켜졌다."));
	// §9 B. 현장은 보존 중이라 곁에는 못 간다. 수습 전 두 번의 새벽은 황순금과
	// 벽 밖에서 대답했다. 각주의 소리 일지가 이 줄에서 받쳐진다.
	Lines.Add(NSLOCTEXT(
		"IGMissingFloor",
		"EpilogueServiceBay2",
		"그 뒤로 새벽 두 번을 401호 할머니와 벽 앞에서 같이 대답했다."));
	if (Narrative
		&& Narrative->HasWitness(EIGMissingFloorWitness::RooftopCigarettePack))
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueServiceBay3Seen",
			"옥상 탱크 옆, 오빠가 쉬던 자리는 아직 그대로다."));
	}
	Lines.Add(FText::GetEmpty());
	Lines.Add(NSLOCTEXT(
		"IGMissingFloor",
		"EpilogueServiceBay4",
		"방을 빼고 403호 열쇠를 돌려줬다."));
	return Lines;
}

TArray<FText> AIGMissingFloorEpilogueDirector::BuildNewsLines() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	TArray<FText> Lines;

	// 두 엔딩의 공통 사실이다(§9). 목격 여부와 무관하게 같은 사건이
	// 같은 순서로 보도된다 — 달라지는 것은 근거의 선명도뿐이다.
	Lines.Add(NSLOCTEXT(
		"IGMissingFloor",
		"EpilogueNews1",
		"무영로 다세대주택 벽체에서 남성 유해 발견 … 실종 1년 만"));
	// 같은 사실, 다른 마지막 그림(§9). 05:30에 바로 신고한 A는 그를 방음한
	// 방에서 만나고, 한 시간을 벽 앞에 앉아 있던 B는 빈 방을 만난다. 그의
	// 처분은 여기서도 확정하지 않는다.
	Lines.Add(
		ActiveEndingId == FName(TEXT("Ending.B"))
			? NSLOCTEXT(
				"IGMissingFloor",
				"EpilogueNewsMokB",
				"건물주 목씨 소재 파악 중… 관리실 비운 채 연락 끊겨")
			: NSLOCTEXT(
				"IGMissingFloor",
				"EpilogueNewsMokA",
				"경찰, 건물주 목씨 상대로 사건 경위 조사"));

	// §13의 회수다. 나린의 제보는 어느 회차에도 빠지지 않는다.
	if (Narrative
		&& Narrative->HasWitness(EIGMissingFloorWitness::BoothSoundproofing))
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueNews3Seen",
			"관리실 안쪽 방은 방음 시공돼 있었다"));
	}
	else
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueNews3",
			"민원 대장 두 벌과 이틀치 자재 영수증이 확보됐다"));
	}

	if (Narrative
		&& Narrative->HasWitness(EIGMissingFloorWitness::SeoSleepingPills))
	{
		// 출석 의사는 공통 사실이라 약봉투를 본 회차에도 남는다. 본 회차에는
		// 그 사람의 말이 더해질 뿐이다.
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueNews4Seen",
			"전 세입자 서모씨는 “그 뒤로 새벽마다 깼다”며 출석 의사를 밝혔다"));
	}
	else
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueNews4",
			"전 세입자 서모씨는 변호인을 통해 출석 의사를 밝혔다"));
	}

	// 채널 5가 어디에도 남지 않은 이유는 초자연이 아니었다. 본 회차에만
	// 보도가 그 사실을 댄다 — 못 본 회차에도 사건은 같다(§9).
	if (Narrative
		&& Narrative->HasWitness(EIGMissingFloorWitness::RecorderEmptyBay))
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueNews6Recorder",
			"건물 녹화 장치는 저장 디스크가 빠진 채로 발견됐다"));
	}
	else if (Narrative
		&& Narrative->HasWitness(EIGMissingFloorWitness::BoothWallCalendar))
	{
		// 밤2에 본 것은 관리실 벽 달력이다. 그 주의 민원 대장은 비어 있지 않다.
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueNews6Calendar",
			"관리실 벽 달력은 지난해 7월 26일에 표시된 채 멈춰 있었다"));
	}

	// §13의 회수는 목격과 무관하게 남는다. 근무표를 본 회차에만 그 제보가
	// 무엇으로 뒷받침됐는지까지 말한다. 근무표를 못 봤어도 뒤편 안내문을 보고
	// 나린에게 택배를 물었다면, 그때 들은 말이 그대로 진술이 된다.
	if (Narrative
		&& Narrative->HasWitness(EIGMissingFloorWitness::StoreNightRoster))
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueNews5Roster",
			"인근 편의점 직원은 사건 당일 야간 근무 중 들은 소리를 진술했다"));
	}
	else if (Narrative
		&& Narrative->HasBeatPlayed(FName(TEXT("Neighborhood.DeliveryDiscussed"))))
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueNews5Parcel",
			"인근 편의점 직원은 실종자의 택배를 건물주가 찾아갔다고 진술했다"));
	}
	else
	{
		Lines.Add(NSLOCTEXT(
			"IGMissingFloor",
			"EpilogueNews5",
			"인근 편의점 직원의 목격 진술을 토대로 당시 행적을 확인 중이다"));
	}
	return Lines;
}

void AIGMissingFloorEpilogueDirector::FinishEpilogue()
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	bReplaySkipInputActive = false;
	bReplaySkipRewinding = false;
	SetActorTickEnabled(false);
	GetWorldTimerManager().ClearTimer(CueTimerHandle);
	GetWorldTimerManager().ClearTimer(FirstViewSkipTimer);
	StopBeds();
	UpdateSkipHud();
	if (!bReplayForcedForSession)
	{
		PersistEpilogueExperience();
	}

	if (AIGPlayerCharacter* PlayerCharacter = Player.Get())
	{
		if (APlayerController* Controller =
			Cast<APlayerController>(PlayerCharacter->GetController()))
		{
			if (AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD()))
			{
				Hud->EndMissingFloorEpilogue();
			}
		}
	}
	else if (UWorld* World = GetWorld())
	{
		// 폰이 사라진 채로 끝났다. 카드가 화면에 남으면 타이틀 위에 겹친다.
		if (APlayerController* Controller = World->GetFirstPlayerController())
		{
			if (AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD()))
			{
				Hud->EndMissingFloorEpilogue();
			}
		}
	}

	// 이동 잠금과 암전은 걷지 않는다. 여기서 화면이 돌아오면 플레이어는
	// 유해가 있던 공동 앞에 다시 서게 된다 — 에필로그 다음은 타이틀이다.
	OnCompleted.Broadcast();
}

void AIGMissingFloorEpilogueDirector::StopBeds()
{
	if (ScoreBed)
	{
		ScoreBed->Stop();
	}
}

UIGMissingFloorNarrativeSubsystem*
AIGMissingFloorEpilogueDirector::GetNarrative() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
}

bool AIGMissingFloorEpilogueDirector::ValidateTimelines()
{
	auto IsMonotonic = [](const float* Times, const int32 Count) -> bool
	{
		if (Count < 2 || !FMath::IsNearlyZero(Times[0]))
		{
			return false;
		}
		for (int32 Index = 1; Index < Count; ++Index)
		{
			if (Times[Index] <= Times[Index - 1])
			{
				return false;
			}
		}
		return true;
	};

	const int32 ACount =
		static_cast<int32>(UE_ARRAY_COUNT(IGEpilogue::EndingATimes));
	const int32 BCount =
		static_cast<int32>(UE_ARRAY_COUNT(IGEpilogue::EndingBTimes));
	if (!IsMonotonic(IGEpilogue::EndingATimes, ACount)
		|| !IsMonotonic(IGEpilogue::EndingBTimes, BCount))
	{
		return false;
	}

	// 몽타주는 §9의 공통 사실이라 두 엔딩에서 같은 시각에 같은 순서로 난다.
	for (int32 Index = 0; Index <= 4; ++Index)
	{
		if (!FMath::IsNearlyEqual(
			IGEpilogue::EndingATimes[Index],
			IGEpilogue::EndingBTimes[Index]))
		{
			return false;
		}
	}

	// 마지막 카드는 끝나기 전에 충분히 읽혀야 한다. 8초는 200% 자막에서
	// 두 줄로 접히는 문장을 읽고 남는 길이다.
	const float ACardHold =
		IGEpilogue::EndingATimes[ACount - 1] - IGEpilogue::EndingATimes[ACount - 2];
	const float BCardHold =
		IGEpilogue::EndingBTimes[BCount - 1] - IGEpilogue::EndingBTimes[BCount - 2];
	return ACardHold >= 8.0f && BCardHold >= 8.0f;
}

bool AIGMissingFloorEpilogueDirector::CompleteImmediatelyForProbe()
{
	if (!bActive)
	{
		return false;
	}
	GetWorldTimerManager().ClearTimer(CueTimerHandle);
	const int32 CueCount = bEndingA
		? static_cast<int32>(UE_ARRAY_COUNT(IGEpilogue::EndingATimes))
		: static_cast<int32>(UE_ARRAY_COUNT(IGEpilogue::EndingBTimes));
	for (int32 Index = NextCueIndex; Index < CueCount && bActive; ++Index)
	{
		FireCue(Index);
	}
	NextCueIndex = CueCount;
	return !bActive;
}

void AIGMissingFloorEpilogueDirector::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(CueTimerHandle);
	GetWorldTimerManager().ClearTimer(FirstViewSkipTimer);
	StopBeds();
	// 장면 도중 불러오기나 액터 교체로 끊겨도 이동과 화면을 붙들어 두지 않는다.
	if (bActive && EndPlayReason != EEndPlayReason::LevelTransition
		&& EndPlayReason != EEndPlayReason::EndPlayInEditor
		&& EndPlayReason != EEndPlayReason::Quit)
	{
		if (AIGPlayerCharacter* Character = Player.Get())
		{
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			if (APlayerController* Controller = Cast<APlayerController>(Character->GetController()))
			{
				if (Controller->PlayerCameraManager)
				{
					Controller->PlayerCameraManager->StopCameraFade();
				}
				if (AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(Controller->GetHUD()))
				{
					Hud->EndMissingFloorEpilogue();
				}
			}
		}
	}
	bActive = false;
	bReplaySkipInputActive = false;
	bReplaySkipRewinding = false;
	UpdateSkipHud();
	Super::EndPlay(EndPlayReason);
}
