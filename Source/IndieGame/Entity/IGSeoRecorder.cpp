#include "Entity/IGSeoRecorder.h"

#include "Accessibility/IGAccessibilitySubsystem.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGMissingFloorEvidence.h"
#include "Entity/IGNightLoopDirector.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "TimerManager.h"

namespace IGSeoRecorder
{
	/**
	 * 5층 첫 판재 더미(가운데 (0, 590), 120 x 80, 받침목 높이 12 cm) 남쪽 끝. 받침목 사이
	 * X -38..-5 빈칸에 몸통이 들어가 있고, 끝 2.5 cm와 손목끈만 판재 밖으로 나왔다.
	 * 철문(Y 452)에서 들어서며 내려다보면 손전등에 주황 끈이 걸린다. 자재 모서리 얼룩
	 * (-10, 585)의 바로 아래다.
	 */
	const FVector RestingLocation(-21.0f, 553.0f, 1200.02f);
	/** 몸통 길이 방향(+X, 액정 쪽)이 판재 안쪽(+Y)을 본다. 조금 비스듬히 미끄러져 들어갔다. */
	const FRotator RestingRotation(0.0f, 102.0f, 0.0f);
	/** 줍기 판정은 판재 밖으로 나온 끝과 끈을 덮는다. 몸통만 정확히 겨눠야 집히는 일이 없게 한다. */
	const FVector PickupCenter(-19.5f, 546.0f, 1203.0f);

	constexpr float ClimbStepSeconds = 0.42f;
	constexpr int32 ClimbSteps = 7;
	constexpr float RunStepSeconds = 0.24f;
	constexpr int32 RunSteps = 8;
	constexpr float RecordedLineSlackSeconds = 2.0f;
	/** 서일영 목소리 피치. 남자 목소리 높이로 조금 내린다. */
	constexpr float SeoVoicePitch = 0.9f;
	/** 다른 남자 목소리 피치. 녹음기에서 떨어져 있어서 더 낮고 작게 녹음됐다. */
	constexpr float OtherVoicePitch = 0.8f;
}

AIGSeoRecorder::AIGSeoRecorder()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

FVector AIGSeoRecorder::GetRestingLocation()
{
	return IGSeoRecorder::RestingLocation;
}

bool AIGSeoRecorder::Configure()
{
	UWorld* World = GetWorld();
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Meshes/SM_SeoVoiceRecorder.SM_SeoVoiceRecorder"));
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!World || !Mesh || !CubeMesh)
	{
		return false;
	}
	SetActorLocation(IGSeoRecorder::RestingLocation);

	RecorderMesh = NewObject<UStaticMeshComponent>(this, TEXT("SeoRecorderMesh"));
	RecorderMesh->SetupAttachment(RootComponent);
	RecorderMesh->SetStaticMesh(Mesh);
	RecorderMesh->SetWorldLocationAndRotation(
		IGSeoRecorder::RestingLocation, IGSeoRecorder::RestingRotation);
	RecorderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RecorderMesh->SetCanEverAffectNavigation(false);
	// 판재 밑 그늘에 있는 작은 물건이라 그림자와 디스턴스 필드 조명에서 빼고 가까이에서만 렌더링한다.
	RecorderMesh->SetCastShadow(false);
	RecorderMesh->SetAffectDistanceFieldLighting(false);
	RecorderMesh->SetAffectDynamicIndirectLighting(false);
	RecorderMesh->SetCullDistance(1200.0f);
	RecorderMesh->RegisterComponent();
	AddInstanceComponent(RecorderMesh);

	FActorSpawnParameters Parameters;
	Parameters.Name = TEXT("MissingFloorSeoRecorderPickup");
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Pickup = World->SpawnActor<AIGMissingFloorEvidence>(
		AIGMissingFloorEvidence::StaticClass(),
		FTransform(FRotator::ZeroRotator, IGSeoRecorder::PickupCenter),
		Parameters);
	if (!Pickup)
	{
		return false;
	}
	// 보이는 건 위의 메시고, 이 액터는 판재 밖으로 나온 끝을 줍는 판정만 맡는다. 크기는 리터럴로
	// 둔다. 소품 배치 감사(audit_director_props.py)가 상수 이름은 해석하지 못한다.
	Pickup->Configure(
		CubeMesh,
		nullptr,
		FVector(16.0f, 16.0f, 6.0f),
		NSLOCTEXT("IGMissingFloor", "SeoRecorderPrompt", "판재 밑 녹음기"),
		FText::GetEmpty(),
		EIGMissingFloorTruth::None,
		EIGMissingFloorSource::None,
		0.6f,
		0.04f,
		/*bPresentationVisible=*/false);
	Pickup->OnExamined.AddUObject(this, &AIGSeoRecorder::HandleTaken);
	RefreshAvailability();
	return true;
}

void AIGSeoRecorder::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(CueTimer);
	for (const TWeakObjectPtr<UAudioComponent>& Voice : Voices)
	{
		if (UAudioComponent* Component = Voice.Get())
		{
			Component->Stop();
		}
	}
	Voices.Reset();
	Super::EndPlay(EndPlayReason);
}

UIGMissingFloorNarrativeSubsystem* AIGSeoRecorder::GetNarrative() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
}

bool AIGSeoRecorder::IsAvailable() const
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	return Pickup
		&& bHourActive
		&& !bPlaying
		&& Narrative
		&& !Narrative->HasWitness(EIGMissingFloorWitness::SeoVoiceRecorder)
		&& !Narrative->IsNightFourWallOpened();
}

void AIGSeoRecorder::RefreshAvailability()
{
	const UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative();
	// 끝까지 들었으면 유담이 가지고 있다. 벽이 열린 뒤에는 판재 밑을 볼 일이 없다.
	const bool bLying = Narrative
		&& !bPlaying
		&& !Narrative->HasWitness(EIGMissingFloorWitness::SeoVoiceRecorder)
		&& !Narrative->IsNightFourWallOpened();
	if (RecorderMesh)
	{
		RecorderMesh->SetVisibility(bLying);
	}
	if (Pickup)
	{
		const bool bAvailable = IsAvailable();
		Pickup->SetActorEnableCollision(bAvailable);
		Pickup->SetInteractionEnabled(bAvailable);
	}
}

void AIGSeoRecorder::SetHourActive(const bool bInHourActive)
{
	const bool bChanged = bHourActive != bInHourActive;
	bHourActive = bInHourActive;
	// 새벽이 오거나 밤이 시작됐다. 틀던 녹음은 거기서 멈춘다.
	if (bChanged && bPlaying)
	{
		AbortPlayback();
	}
	RefreshAvailability();
}

void AIGSeoRecorder::StartPlaybackForTesting()
{
	if (!bPlaying)
	{
		BeginPlayback();
	}
}

void AIGSeoRecorder::HandleTaken(AIGMissingFloorEvidence* Evidence)
{
	if (!bPlaying && IsAvailable())
	{
		BeginPlayback();
	}
}

void AIGSeoRecorder::BeginPlayback()
{
	bPlaying = true;
	CueIndex = 0;
	StepIndex = 0;
	LineDeadlineAt = 0.0;
	GetWorldTimerManager().SetTimer(
		CaptureWatchTimer, this, &AIGSeoRecorder::WatchForCapture, 0.2f, true);
	IGAudio::SpawnOneShotAt(
		this,
		UIGToneSequenceSoundWave::CreatePickupRustle(this),
		IGSeoRecorder::RestingLocation + FVector(0.0f, 0.0f, 4.0f),
		0.38f, 1.08f, 60.0f, 700.0f, EIGAudioBus::Player);
	RefreshAvailability();
	UE_LOG(LogTemp, Display, TEXT("SEORECORDER playback begins"));
	Wait(0.7f);
}

void AIGSeoRecorder::AbortPlayback()
{
	GetWorldTimerManager().ClearTimer(CueTimer);
	GetWorldTimerManager().ClearTimer(CaptureWatchTimer);
	// 끝까지 듣고 목격까지 남긴 뒤 생각이 뜨기 전에 멈췄다. 들은 것은 들은 것이니 생각은 띄운다.
	if (CueIndex == 14)
	{
		PushRecorderThought();
	}
	for (const TWeakObjectPtr<UAudioComponent>& Voice : Voices)
	{
		if (UAudioComponent* Component = Voice.Get())
		{
			Component->Stop();
		}
	}
	Voices.Reset();
	bPlaying = false;
	CueIndex = 0;
	StepIndex = 0;
	UE_LOG(LogTemp, Display, TEXT("SEORECORDER playback stopped before the end"));
	RefreshAvailability();
}

void AIGSeoRecorder::WatchForCapture()
{
	if (!bPlaying)
	{
		GetWorldTimerManager().ClearTimer(CaptureWatchTimer);
		return;
	}
	for (TActorIterator<AIGNightLoopDirector> It(GetWorld()); It; ++It)
	{
		if (It->IsCaptureResetInFlight())
		{
			AbortPlayback();
			return;
		}
	}
}

void AIGSeoRecorder::PushRecorderThought()
{
	AIGHorrorHUD::PushThought(
		this,
		NSLOCTEXT("IGMissingFloor", "SeoRecorderThought", "오빠를 두고 그냥 내려간 거야?"),
		4.0f);
}

void AIGSeoRecorder::Wait(const float Seconds)
{
	GetWorldTimerManager().SetTimer(
		CueTimer, this, &AIGSeoRecorder::RunCue, FMath::Max(Seconds, 0.05f), false);
}

bool AIGSeoRecorder::IsDialogueLaneIdle() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AIGHorrorHUD* Hud = PC ? Cast<AIGHorrorHUD>(PC->GetHUD()) : nullptr;
	return !Hud || Hud->IsDialogueLaneIdle();
}

FVector AIGSeoRecorder::GetSpeakerLocation() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return IGSeoRecorder::RestingLocation;
	}
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	// 가슴께에 든 녹음기다.
	return Eye + View.Vector() * 28.0f - FVector(0.0f, 0.0f, 22.0f);
}

void AIGSeoRecorder::PlayFromSpeaker(USoundBase* Sound, const float Volume, const float Pitch)
{
	UAudioComponent* Voice = IGAudio::SpawnOneShotAt(
		this, Sound, GetSpeakerLocation(), Volume, Pitch, 40.0f, 520.0f, EIGAudioBus::Player);
	if (!Voice)
	{
		return;
	}
	// 손바닥만 한 스피커다. 저역과 고역을 잘라서 녹음기 소리처럼 들리게 한다.
	Voice->SetHighPassFilterEnabled(true);
	Voice->SetHighPassFilterFrequency(380.0f);
	Voice->SetLowPassFilterEnabled(true);
	Voice->SetLowPassFilterFrequency(5200.0f);
	Voices.Add(Voice);
}

float AIGSeoRecorder::PushRecordedLine(const FText& Line)
{
	AIGHorrorHUD::PushDialogue(
		this,
		NSLOCTEXT("IGMissingFloor", "SeoRecorderSpeaker", "녹음기"),
		Line,
		EIGDialogueChannel::Device,
		0.0f,
		EIGDialoguePriority::Story);
	float CaptionScale = 1.0f;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UIGAccessibilitySubsystem* Accessibility = GameInstance->GetSubsystem<UIGAccessibilitySubsystem>())
		{
			CaptionScale = Accessibility->GetCaptionDurationScale();
		}
	}
	const float Seconds = AIGHorrorHUD::EstimateDialogueSeconds(Line) * CaptionScale;
	LineDeadlineAt = GetWorld()->GetTimeSeconds() + Seconds + IGSeoRecorder::RecordedLineSlackSeconds;
	return Seconds;
}

void AIGSeoRecorder::PushCaption(const FText& Caption, const float Seconds)
{
	AIGHorrorHUD::PushAudioCaption(this, Caption, Seconds);
}

void AIGSeoRecorder::RunCue()
{
	using namespace IGSeoRecorder;
	UWorld* World = GetWorld();
	if (!bPlaying || !World)
	{
		return;
	}
	// 듣다가 붙잡혔다. 녹음기는 손에서 떨어져 판재 밑으로 돌아가고, 처음부터 다시 들어야 한다.
	for (TActorIterator<AIGNightLoopDirector> It(World); It; ++It)
	{
		if (It->IsCaptureResetInFlight())
		{
			AbortPlayback();
			return;
		}
	}
	const double Now = World->GetTimeSeconds();
	// 대사는 앞 대사가 끝난 뒤에 넣는다. 다른 대사가 끼어서 안 끝나면 넉넉히 기다린다.
	const bool bLineCue = CueIndex == 1 || CueIndex == 4 || CueIndex == 6 || CueIndex == 8 || CueIndex == 11;
	if (bLineCue && !IsDialogueLaneIdle() && Now < LineDeadlineAt)
	{
		Wait(0.1f);
		return;
	}
	const auto Murmur = [this](const float Volume, const float Pitch)
	{
		PlayFromSpeaker(UIGToneSequenceSoundWave::CreateDoorMurmur(this), Volume, Pitch);
	};
	switch (CueIndex)
	{
	case 0:
		PlayFromSpeaker(UIGToneSequenceSoundWave::CreateSwitchClick(this, true), 0.45f, 1.18f);
		++CueIndex;
		Wait(0.6f);
		return;

	case 1:
	{
		const float Seconds = PushRecordedLine(NSLOCTEXT(
			"IGMissingFloor",
			"SeoRecorderLine1",
			"7월 26일 새벽 4시 25분. 403호 서일영입니다. 지금 위에 올라가 봅니다."));
		Murmur(0.34f, SeoVoicePitch);
		++CueIndex;
		Wait(Seconds);
		return;
	}

	case 2:
		// 철계단을 오른다. 녹음기를 쥔 손이 같이 흔들려 걸음마다 조금씩 다르다.
		PlayFromSpeaker(
			UIGToneSequenceSoundWave::CreateSurfaceFootstep(
				this, EIGFootstepSurface::MetalStair, 0.94f + 0.02f * (StepIndex % 3), 0.9f),
			0.42f, 1.0f);
		if (StepIndex == 0)
		{
			PushCaption(NSLOCTEXT("IGMissingFloor", "SeoRecorderStepsCaption", "철계단을 오르는 발소리"), 2.6f);
		}
		if (++StepIndex < ClimbSteps)
		{
			Wait(ClimbStepSeconds);
			return;
		}
		StepIndex = 0;
		++CueIndex;
		Wait(0.5f);
		return;

	case 3:
		// 입주하던 저녁 천장 너머에서 구르던 그 바퀴다. 금 간 캐스터가 한 바퀴에 한 번 턱을 친다.
		PlayFromSpeaker(UIGToneSequenceSoundWave::CreateToolCartRoll(this), 0.5f, 1.0f);
		PushCaption(NSLOCTEXT("IGMissingFloor", "SeoRecorderCartCaption", "바퀴가 구르다 멈추는 소리"), 2.4f);
		++CueIndex;
		Wait(2.2f);
		return;

	case 4:
	{
		const float Seconds = PushRecordedLine(NSLOCTEXT(
			"IGMissingFloor", "SeoRecorderLine2", "저기요. 거기 누구 있어요?"));
		Murmur(0.38f, SeoVoicePitch);
		++CueIndex;
		Wait(Seconds);
		return;
	}

	case 5:
		Murmur(0.2f, OtherVoicePitch);
		PushCaption(NSLOCTEXT("IGMissingFloor", "SeoRecorderOtherCaption", "다른 남자의 낮은 목소리"), 2.2f);
		++CueIndex;
		Wait(2.0f);
		return;

	case 6:
	{
		const float Seconds = PushRecordedLine(NSLOCTEXT(
			"IGMissingFloor",
			"SeoRecorderLine3",
			"미안하다는 말 말고요. 매일 이 시간이잖아요. 몇 달째인지 알아요?"));
		Murmur(0.42f, SeoVoicePitch);
		++CueIndex;
		Wait(Seconds);
		return;
	}

	case 7:
		Murmur(0.18f, OtherVoicePitch * 0.97f);
		PushCaption(NSLOCTEXT("IGMissingFloor", "SeoRecorderOtherCaption", "다른 남자의 낮은 목소리"), 1.8f);
		++CueIndex;
		Wait(1.5f);
		return;

	case 8:
	{
		const float Seconds = PushRecordedLine(NSLOCTEXT(
			"IGMissingFloor",
			"SeoRecorderLine4",
			"관리실은 여기 아무도 안 산대요. 그럼 당신은 뭔데요?"));
		Murmur(0.5f, SeoVoicePitch * 1.03f);
		++CueIndex;
		// 대사가 끝나기 전에 몸싸움이 난다.
		Wait(Seconds * 0.8f);
		return;
	}

	case 9:
		PlayFromSpeaker(UIGToneSequenceSoundWave::CreateClothSettle(this), 0.6f, 1.25f);
		PlayFromSpeaker(UIGToneSequenceSoundWave::CreateSurfaceFootstep(
			this, EIGFootstepSurface::Concrete, 1.1f, 1.0f), 0.55f, 1.0f);
		++CueIndex;
		Wait(0.42f);
		return;

	case 10:
		// 자재 더미 위로 넘어지고 주철 부품이 쏟아진다(설정집 §2 04:30).
		PlayFromSpeaker(UIGToneSequenceSoundWave::CreateBoardStackFall(this), 0.62f, 0.86f);
		PlayFromSpeaker(UIGToneSequenceSoundWave::CreateTuningWrenchClink(this), 0.42f, 0.74f);
		PlayFromSpeaker(UIGToneSequenceSoundWave::CreateKeyDropMetalBox(this), 0.36f, 0.82f);
		PushCaption(NSLOCTEXT("IGMissingFloor", "SeoRecorderCrashCaption", "쇳덩이가 쏟아지는 소리"), 2.6f);
		++CueIndex;
		// 그다음은 아무 소리도 없다.
		Wait(3.4f);
		return;

	case 11:
	{
		const float Seconds = PushRecordedLine(NSLOCTEXT(
			"IGMissingFloor", "SeoRecorderLine5", "…저기요. 괜찮아요?"));
		Murmur(0.24f, SeoVoicePitch * 1.06f);
		++CueIndex;
		Wait(Seconds + 1.0f);
		return;
	}

	case 12:
		// 대답이 없자 뛰어 내려간다. 녹음기는 판재 밑에 남았다.
		PlayFromSpeaker(
			UIGToneSequenceSoundWave::CreateSurfaceFootstep(
				this, EIGFootstepSurface::MetalStair, 1.02f + 0.03f * (StepIndex % 2), 1.0f),
			FMath::Lerp(0.5f, 0.12f, static_cast<float>(StepIndex) / RunSteps), 1.0f);
		if (StepIndex == 0)
		{
			PushCaption(NSLOCTEXT("IGMissingFloor", "SeoRecorderRunCaption", "계단을 뛰어 내려가는 발소리"), 2.4f);
		}
		if (++StepIndex < RunSteps)
		{
			Wait(RunStepSeconds);
			return;
		}
		StepIndex = 0;
		++CueIndex;
		Wait(1.8f);
		return;

	case 13:
		PlayFromSpeaker(UIGToneSequenceSoundWave::CreateSwitchClick(this, false), 0.45f, 1.18f);
		if (UIGMissingFloorNarrativeSubsystem* Narrative = GetNarrative())
		{
			Narrative->RecordWitness(EIGMissingFloorWitness::SeoVoiceRecorder);
		}
		UE_LOG(LogTemp, Display, TEXT("SEORECORDER heard to the end"));
		++CueIndex;
		Wait(1.2f);
		return;

	case 14:
		PushRecorderThought();
		bPlaying = false;
		Voices.Reset();
		RefreshAvailability();
		return;

	default:
		bPlaying = false;
		RefreshAvailability();
		return;
	}
}
