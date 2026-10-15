#include "Sequence/IGEpiloguePreviewProbe.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Entity/IGMissingFloorEpilogueDirector.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "IndieGame.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

namespace IGEpiloguePreview
{
	/** 무대가 서고 노출이 가라앉을 때까지 기다렸다가 에필로그를 연다. */
	constexpr float StartDelaySeconds = 3.0f;
	/** 장면이 바뀐 뒤 그림과 글이 다 들어온 때(HUD 입장 1.15초 뒤)에 찍는다. */
	constexpr float ShotDelaySeconds = 2.6f;
	/** 엔딩 A가 99초다. 그보다 넉넉히 기다리고 그래도 안 끝나면 실패로 본다. */
	constexpr float TimeoutSeconds = 150.0f;
}

AIGEpiloguePreviewProbe::AIGEpiloguePreviewProbe()
{
	// 매 프레임 장면 수가 바뀌었는지 본다. -IGEpiloguePreview로만 선다.
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGEpiloguePreviewProbe::Configure(AIGPlayerCharacter* InPlayer, const FName InEndingId)
{
	Player = InPlayer;
	EndingId = InEndingId;
}

void AIGEpiloguePreviewProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFinished)
	{
		return;
	}
	TotalSeconds += DeltaSeconds;
	if (TotalSeconds > IGEpiloguePreview::TimeoutSeconds)
	{
		UE_LOG(LogIndieGame, Display, TEXT("EPILOGUE_PREVIEW timeout scenes=%d"), SeenScenes);
		Finish(false);
		return;
	}
	if (!bStarted)
	{
		if (TotalSeconds < IGEpiloguePreview::StartDelaySeconds)
		{
			return;
		}
		for (TActorIterator<AIGMissingFloorEpilogueDirector> It(GetWorld()); It; ++It)
		{
			Epilogue = *It;
		}
		bStarted = true;
		// -IGEpiloguePreviewAllSeen: 목격과 이웃 비트를 다 채워 장면마다 가장 긴 글을 본다.
		// 번역에서 글이 넘치는지는 가장 긴 판으로 재야 한다.
		if (FParse::Param(FCommandLine::Get(), TEXT("IGEpiloguePreviewAllSeen")))
		{
			UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
			if (UIGMissingFloorNarrativeSubsystem* Narrative = GameInstance
				? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
				: nullptr)
			{
				for (uint8 Raw = 1; Raw <= static_cast<uint8>(EIGMissingFloorWitness::SeoVoiceRecorder); ++Raw)
				{
					Narrative->RecordWitness(static_cast<EIGMissingFloorWitness>(Raw));
				}
				Narrative->MarkBeatPlayed(FName(TEXT("Day.Jeongwoo.Intro")));
				Narrative->MarkBeatPlayed(FName(TEXT("Day.Jeongwoo.Guest")));
				Narrative->MarkBeatPlayed(FName(TEXT("Day2.NarinMilk")));
				// 0.10.0 남은 짐 줄기: 공구 주머니를 가져오고 정우와 오빠 상자를 같이 꺼낸 회차.
				Narrative->AddLeftover(EIGLeftoverList::Taken, FName(TEXT("ToolPouch")));
				Narrative->AddLeftover(EIGLeftoverList::Taken, FName(TEXT("DohaBox")));
				Narrative->MutableLeftover().JeongwooState = FName(TEXT("LeftTogether"));
				// 나린과 찾은 수령 영수증을 챙긴 회차. 보도의 편의점 줄이 가장 길어진다.
				Narrative->MarkBeatPlayed(FName(TEXT("Neighborhood.DeliveryDiscussed")));
				Narrative->MarkBeatPlayed(FName(TEXT("Neighborhood.ParcelLedger")));
				Narrative->MarkBeatPlayed(FName(TEXT("Neighborhood.ParcelReceipt")));
				UE_LOG(LogIndieGame, Display, TEXT("EPILOGUE_PREVIEW all witnesses seen"));
			}
		}
		if (!Epilogue.IsValid() || !Epilogue->StartEpilogue(Player.Get(), EndingId))
		{
			UE_LOG(LogIndieGame, Display, TEXT("EPILOGUE_PREVIEW could not start %s"), *EndingId.ToString());
			Finish(false);
			return;
		}
		Epilogue->OnCompleted.AddUObject(this, &AIGEpiloguePreviewProbe::HandleCompleted);
		return;
	}
	AIGMissingFloorEpilogueDirector* Director = Epilogue.Get();
	if (!Director)
	{
		Finish(false);
		return;
	}
	if (Director->GetPlayedSceneCount() > SeenScenes)
	{
		SeenScenes = Director->GetPlayedSceneCount();
		SceneSeconds = 0.0f;
		bSceneShot = false;
	}
	SceneSeconds += DeltaSeconds;
	if (!bSceneShot && SceneSeconds >= IGEpiloguePreview::ShotDelaySeconds)
	{
		bSceneShot = true;
		Shoot(SeenScenes);
	}
	if (!Director->IsActive())
	{
		HandleCompleted();
	}
}

void AIGEpiloguePreviewProbe::HandleCompleted()
{
	// 마지막 카드까지 찍고 끝난다. A는 여섯 장면, B는 다섯 장면이다.
	const int32 Played = Epilogue.IsValid() ? Epilogue->GetPlayedSceneCount() : SeenScenes;
	const int32 Expected = EndingId == FName(TEXT("Ending.B")) ? 5 : 6;
	UE_LOG(LogIndieGame, Display, TEXT("EPILOGUE_PREVIEW scenes=%d expected=%d seconds=%.1f"),
		Played, Expected, TotalSeconds - IGEpiloguePreview::StartDelaySeconds);
	Finish(Played == Expected);
}

void AIGEpiloguePreviewProbe::Shoot(const int32 SceneNumber)
{
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EpiloguePreview")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(
		TEXT("epilogue-%s-%d.png"), EndingId == FName(TEXT("Ending.B")) ? TEXT("b") : TEXT("a"), SceneNumber));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogIndieGame, Display, TEXT("EPILOGUE_PREVIEW_SHOT %s"), *Path);
}

void AIGEpiloguePreviewProbe::Finish(const bool bPassed)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	UE_LOG(LogIndieGame, Display, TEXT("EPILOGUE_PREVIEW %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"));
	FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
}
