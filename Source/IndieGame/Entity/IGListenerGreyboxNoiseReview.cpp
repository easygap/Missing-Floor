#include "Entity/IGListenerGreyboxDirector.h"

#include "AssetCompilingManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGMissingFloorPuzzleTwoDirector.h"
#include "Entity/IGNightPhaseDirector.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "IndieGame.h"
#include "Interaction/IGSwingDoor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/IGFlashlightComponent.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

// 화면 잡티를 원인별로 나눠 보는 촬영(-IGScreenNoiseReview). 같은 자리, 같은 카메라에서 설정을
// 하나씩만 바꿔 정지 화면 두 장(이어진 두 프레임)과 천천히 도는 화면 여덟 장을 남긴다. 어떤 원인이
// 점을 만드는지는 Scripts/analyze_screen_noise.py가 같은 영역을 비교해 판단한다.
namespace IGScreenNoise
{
	struct FNoiseView
	{
		const TCHAR* Name;
		/** 0이면 입주 날 저녁, 그 밖에는 그 밤의 04:30이다. */
		int32 Night;
		FVector Eye;
		float Yaw;
		float Pitch;
		bool bTorch;
	};
	// 넓은 화면(README와 같은 자리)과 벽에 바짝 붙은 화면을 함께 본다. 점무늬는 벽 가까이서,
	// 끓는 반짝임은 손전등 빛 가장자리와 넓은 어둠에서 잘 드러난다.
	const FNoiseView Views[] = {
		{TEXT("corridor-day"), 0, {190, -305, 998}, 180, -4, false},
		{TEXT("corridor-day-wall"), 0, {150, -318, 998}, 270, -6, false},
		{TEXT("store"), 0, {2760, -285, 98}, 138, -4, false},
		{TEXT("alley"), 0, {700, -520, 98}, 0, 0, false},
		{TEXT("bedroom-night"), 2, {80, -100, 998}, 132, -20, false},
		{TEXT("corridor-night"), 2, {190, -305, 998}, 180, -4, true},
		{TEXT("corridor-night-wall"), 2, {150, -318, 998}, 270, -6, true},
		{TEXT("booth"), 2, {165, -190, 98}, 90, -35, true},
		// 302호 벽지와 장롱, 복도 바닥 인조석을 손전등으로 가까이 본다. 정우가 판을 받치던 장면에서
		// 벽 전체에 고운 점이 앉아 보였다.
		{TEXT("storage-wall-torch"), 2, {40, -60, 698}, 0, -5, true},
		{TEXT("storage-wardrobe-torch"), 2, {-30, -10, 698}, 180, -12, true},
		{TEXT("corridor-floor-torch"), 2, {190, -305, 998}, 180, -45, true},
	};

	struct FNoiseVariant
	{
		const TCHAR* Name;
		const TCHAR* Apply;
		const TCHAR* Undo;
		/** 움직일 때 끓는지도 본다. 정지 화면만으로 갈리는 설정은 돌리지 않는다. */
		bool bPan;
	};
	// 한 번에 하나만 바꾼다. 둘을 같이 바꾸면 어느 쪽이 점을 지웠는지 알 수 없다.
	const FNoiseVariant Variants[] = {
		{TEXT("base"), TEXT(""), TEXT(""), true},
		{TEXT("grain-quantization-off"), TEXT("r.Tonemapper.GrainQuantization 0"), TEXT("r.Tonemapper.GrainQuantization 1"), false},
		{TEXT("texture-mip-bias"), TEXT("r.MipMapLODBias 1.5"), TEXT("r.MipMapLODBias 0"), true},
		{TEXT("specular-off"), TEXT("ShowFlag.Specular 0"), TEXT("ShowFlag.Specular 2"), false},
		{TEXT("lumen-gi-off"), TEXT("r.Lumen.DiffuseIndirect.Allow 0"), TEXT("r.Lumen.DiffuseIndirect.Allow 1"), true},
		{TEXT("lumen-reflections-off"), TEXT("r.Lumen.Reflections.Allow 0"), TEXT("r.Lumen.Reflections.Allow 1"), false},
		{TEXT("aa-fxaa"), TEXT("r.AntiAliasingMethod 1"), TEXT("r.AntiAliasingMethod 2"), false},
		{TEXT("aa-tsr"), TEXT("r.AntiAliasingMethod 4"), TEXT("r.AntiAliasingMethod 2"), true},
		{TEXT("aa-off"), TEXT("r.AntiAliasingMethod 0"), TEXT("r.AntiAliasingMethod 2"), false},
		// 마지막에 기본 설정을 한 번 더 찍는다. 처음 것과 밝기·점 세기가 다르면 그 장면은 노출이
		// 덜 맞은 채 찍힌 것이라 비율을 믿을 수 없다.
		{TEXT("base-again"), TEXT(""), TEXT(""), true},
	};

	constexpr int32 SettleFrames = 45;
	/** 자리를 옮긴 직후. 노출 적응과 루멘 누적이 따라오는 시간이다. 45프레임으로는 302호 벽이 덜 밝았다. */
	constexpr int32 ViewSettleFrames = 150;
	constexpr int32 FirstViewSettleFrames = 240;
	constexpr int32 PanFrames = 8;
	constexpr float PanDegreesPerFrame = 0.4f;
	/** 대사 상자와 소리 설명 상자를 띄우고 찍는 프레임. 첫 화면에서 한 번만 한다. */
	constexpr int32 CaptionSettleFrames = 20;

	int32 ReadInt(const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetInt() : -1;
	}

	float ReadFloat(const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetFloat() : -1.0f;
	}
}

bool AIGListenerGreyboxDirector::AdvanceScreenNoiseReview()
{
	using namespace IGScreenNoise;
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	AIGPlayerCharacter* PlayerCharacter = Player.Get();
	if (!Controller || !PlayerCharacter || !GEngine)
	{
		FailProbe(TEXT("화면 잡티 촬영 초기화 실패"));
		return false;
	}
	// -IGScreenNoiseViews=a,b로 몇 장면만 다시 찍는다.
	FString Only;
	FParse::Value(FCommandLine::Get(), TEXT("IGScreenNoiseViews="), Only, false);
	TArray<FString> OnlyNames;
	Only.ParseIntoArray(OnlyNames, TEXT(","));
	while (ScreenNoiseView < UE_ARRAY_COUNT(Views) && ScreenNoiseFrame == 0 && ScreenNoiseVariant == 0
		&& OnlyNames.Num() > 0 && !OnlyNames.Contains(Views[ScreenNoiseView].Name))
	{
		++ScreenNoiseView;
	}
	if (ScreenNoiseView >= UE_ARRAY_COUNT(Views))
	{
		UE_LOG(LogIndieGame, Display, TEXT("SCREEN_NOISE PASS views=%d variants=%d dropped=%d"),
			static_cast<int32>(UE_ARRAY_COUNT(Views)), static_cast<int32>(UE_ARRAY_COUNT(Variants)), ScreenNoiseDropped);
		RequestExit(ScreenNoiseDropped > 0);
		return false;
	}
	const FNoiseView& View = Views[ScreenNoiseView];
	const FNoiseVariant& Variant = Variants[ScreenNoiseVariant];
	const bool bFirstView = ScreenNoiseVariant == 0 && !bScreenNoiseStarted;
	const bool bNightChanges = ScreenNoiseVariant == 0 && View.Night != ScreenNoiseNight;
	const int32 Settle = (bFirstView || bNightChanges) ? FirstViewSettleFrames
		: ScreenNoiseVariant == 0 ? ViewSettleFrames : SettleFrames;

	if (ScreenNoiseFrame == 0)
	{
		if (AHUD* Hud = Controller->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		if (ScreenNoiseVariant == 0)
		{
			if (View.Night != ScreenNoiseNight && View.Night > 0 && NightPhase)
			{
				NightPhase->BeginTheHour(View.Night);
				NightPhase->SetHourPaused(true);
				ScreenNoiseNight = View.Night;
			}
			if (PuzzleTwo && PuzzleTwo->GetBoothDoor())
			{
				PuzzleTwo->GetBoothDoor()->ForceOpenState(true);
			}
			if (Entity)
			{
				Entity->SetDormant(true);
			}
			PlayerCharacter->SetCameraMotionEnabled(false);
			if (UIGFlashlightComponent* Torch = PlayerCharacter->GetFlashlight())
			{
				Torch->SetAvailable(true);
				Torch->SetOn(View.bTorch);
			}
			Controller->SetViewTarget(PlayerCharacter);
			CaptureTeleportPlayer(View.Eye, View.Yaw, View.Pitch);
			FAssetCompilingManager::Get().FinishAllCompilation();
			if (GShaderCompilingManager)
			{
				GShaderCompilingManager->FinishAllCompilation();
			}
			// 실제로 돌고 있는 설정을 남긴다. 기본 AA가 무엇인지, 루멘이 켜져 있는지를 짐작하지 않는다.
			UE_LOG(LogIndieGame, Display,
				TEXT("SCREEN_NOISE_VIEW %s night=%d aa=%d gi=%d reflections=%d grain_quantization=%d screen_percentage=%.1f view_distance=%d shadow=%d gi_quality=%d reflection_quality=%d post=%d texture=%d"),
				View.Name, View.Night, ReadInt(TEXT("r.AntiAliasingMethod")), ReadInt(TEXT("r.DynamicGlobalIlluminationMethod")),
				ReadInt(TEXT("r.ReflectionMethod")), ReadInt(TEXT("r.Tonemapper.GrainQuantization")), ReadFloat(TEXT("r.ScreenPercentage")),
				ReadInt(TEXT("sg.ViewDistanceQuality")), ReadInt(TEXT("sg.ShadowQuality")), ReadInt(TEXT("sg.GlobalIlluminationQuality")),
				ReadInt(TEXT("sg.ReflectionQuality")), ReadInt(TEXT("sg.PostProcessQuality")), ReadInt(TEXT("sg.TextureQuality")));
		}
		Controller->SetControlRotation(FRotator(View.Pitch, View.Yaw, 0.0f));
		if (*Variant.Apply)
		{
			GEngine->Exec(World, Variant.Apply);
		}
	}

	const FString Folder = FPaths::Combine(ScreenNoiseOutputRoot, View.Name);
	const auto Shoot = [this, &Folder](const FString& FileName, const bool bShowUI)
	{
		if (FScreenshotRequest::IsScreenshotRequested())
		{
			++ScreenNoiseDropped;
			UE_LOG(LogIndieGame, Warning, TEXT("SCREEN_NOISE_DROP %s"), *FileName);
		}
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(Folder, FileName), bShowUI, false);
	};
	const int32 Local = ScreenNoiseFrame - Settle;
	const int32 PanEnd = 2 + (Variant.bPan ? PanFrames : 0);
	// 첫 화면의 기본 설정에서만 대사 상자와 소리 설명 상자를 띄워 바탕이 깨끗한지 본다.
	const bool bCaptionShot = ScreenNoiseView == 0 && ScreenNoiseVariant == 0 && OnlyNames.Num() == 0;
	if (Local == 0 || Local == 1)
	{
		Shoot(FString::Printf(TEXT("%s_still%d.png"), Variant.Name, Local), false);
	}
	else if (Local >= 2 && Local < PanEnd)
	{
		// 이번 프레임에 돌린 시점이 이번 프레임에 그려진다.
		Controller->SetControlRotation(FRotator(View.Pitch, View.Yaw + PanDegreesPerFrame * (Local - 1), 0.0f));
		Shoot(FString::Printf(TEXT("%s_pan%02d.png"), Variant.Name, Local - 2), false);
	}
	else if (bCaptionShot && Local == PanEnd)
	{
		Controller->SetControlRotation(FRotator(View.Pitch, View.Yaw, 0.0f));
		if (AHUD* Hud = Controller->GetHUD())
		{
			Hud->bShowHUD = true;
		}
		AIGHorrorHUD::PushDialogue(this, FText::FromString(TEXT("나린")),
			FText::FromString(TEXT("자막 바탕을 보는 문장이에요. 글자 뒤에 점이 끓으면 안 돼요.")),
			EIGDialogueChannel::Conversation, 6.0f, EIGDialoguePriority::Story);
		AIGHorrorHUD::PushAudioCaption(this, FText::FromString(TEXT("[소리 설명 바탕 검사]")), 6.0f);
	}
	else if (bCaptionShot && Local == PanEnd + CaptionSettleFrames)
	{
		Shoot(TEXT("base_captions.png"), true);
	}

	const int32 LastLocal = bCaptionShot ? PanEnd + CaptionSettleFrames : PanEnd - 1;
	if (Local >= LastLocal)
	{
		if (*Variant.Undo)
		{
			GEngine->Exec(World, Variant.Undo);
		}
		if (bCaptionShot)
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (AIGHorrorHUD* Hud = Cast<AIGHorrorHUD>(PC->GetHUD()))
				{
					Hud->ClearDialogueForTesting();
					Hud->bShowHUD = false;
				}
			}
		}
		ScreenNoiseFrame = 0;
		bScreenNoiseStarted = true;
		if (++ScreenNoiseVariant >= UE_ARRAY_COUNT(Variants))
		{
			ScreenNoiseVariant = 0;
			++ScreenNoiseView;
		}
		return true;
	}
	++ScreenNoiseFrame;
	return true;
}
