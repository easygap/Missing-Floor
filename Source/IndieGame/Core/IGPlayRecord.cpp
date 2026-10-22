#include "Core/IGPlayRecord.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGListenerEntity.h"
#include "Entity/IGNightThreatDirector.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGPlayerController.h"
#include "Player/IGStressComponent.h"
#include "RHIGlobals.h"
#include "Scalability.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace IGPlayRecord
{
	constexpr int32 FrameBuckets = 251;
	/** 위치는 5초마다 남긴다. 90분 플레이가 천 줄 남짓이다. */
	constexpr double TrailInterval = 5.0;
	constexpr double SaveInterval = 30.0;
}

void UIGPlayRecordSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bEnabled = FParse::Param(FCommandLine::Get(), TEXT("IGPlayRecord"));
	if (!bEnabled)
	{
		return;
	}
	StartSeconds = FPlatformTime::Seconds();
	LastFrameSeconds = StartSeconds;
	const FDateTime Started = FDateTime::UtcNow();
	StartedUtc = Started.ToIso8601();
	OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlayRecords"),
		FString::Printf(TEXT("playrecord-%s.json"), *Started.ToString(TEXT("%Y%m%d-%H%M%S"))));
	SampleHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UIGPlayRecordSubsystem::Sample), 1.0f);
	FrameHandle = FCoreDelegates::OnEndFrame.AddUObject(this, &UIGPlayRecordSubsystem::RecordFrame);
	AddEvent(TEXT("session_start"));
	UE_LOG(LogTemp, Display, TEXT("MISSINGFLOOR_PLAYRECORD %s"), *OutputPath);
}

void UIGPlayRecordSubsystem::Deinitialize()
{
	if (bEnabled)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SampleHandle);
		FCoreDelegates::OnEndFrame.Remove(FrameHandle);
		AddEvent(TEXT("session_end"));
		Save();
	}
	Super::Deinitialize();
}

void UIGPlayRecordSubsystem::Note(const UObject* WorldContext, const FName Action)
{
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (UIGPlayRecordSubsystem* Record = GameInstance ? GameInstance->GetSubsystem<UIGPlayRecordSubsystem>() : nullptr)
	{
		Record->RecordAction(Action);
	}
}

double UIGPlayRecordSubsystem::Now() const
{
	return FPlatformTime::Seconds() - StartSeconds;
}

void UIGPlayRecordSubsystem::RecordAction(const FName Action)
{
	if (!bEnabled)
	{
		return;
	}
	++ActionCounts.FindOrAdd(Action);
	if (!FirstUse.Contains(Action))
	{
		// 처음 쓴 때만 사건으로 남긴다. 횟수는 따로 센다.
		FirstUse.Add(Action, Now());
		AddEvent(FName(*FString::Printf(TEXT("first_%s"), *Action.ToString())));
	}
	else if (Action == TEXT("journal") || Action == TEXT("hint"))
	{
		AddEvent(Action);
	}
}

void UIGPlayRecordSubsystem::RecordFrame()
{
	const double Seconds = FPlatformTime::Seconds();
	const double FrameMs = (Seconds - LastFrameSeconds) * 1000.0;
	LastFrameSeconds = Seconds;
	if (!bLastHourActive || Nights.IsEmpty())
	{
		return;
	}
	// 밤 동안의 프레임만 센다. 메뉴와 낮은 사양 판정과 상관이 적다.
	TArray<uint32>& Histogram = Nights.Last().FrameHistogram;
	if (Histogram.Num() != IGPlayRecord::FrameBuckets)
	{
		Histogram.Init(0, IGPlayRecord::FrameBuckets);
	}
	++Histogram[FMath::Clamp(FMath::FloorToInt32(FrameMs), 0, IGPlayRecord::FrameBuckets - 1)];
}

FVector UIGPlayRecordSubsystem::GetPlayerLocation() const
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	return Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
}

void UIGPlayRecordSubsystem::AddEvent(const FName Type, const FString& Detail)
{
	FEvent& Event = Events.AddDefaulted_GetRef();
	Event.Seconds = Now();
	Event.Type = Type;
	Event.Night = LastNightIndex;
	Event.Location = GetPlayerLocation();
	Event.Detail = Detail;
}

bool UIGPlayRecordSubsystem::Sample(float /*FrameDeltaSeconds*/)
{
	// 티커가 넘기는 값은 프레임 간격이다. 표본 사이의 실제 시간을 따로 잰다.
	const double SampleDelta = Now() - LastSampleSeconds;
	LastSampleSeconds = Now();
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World)
	{
		return true;
	}
	const UIGMissingFloorNarrativeSubsystem* Narrative = GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>();
	if (Narrative)
	{
		const int32 NightIndex = Narrative->GetNightIndex();
		const bool bHourActive = Narrative->IsHourSealed();
		const int32 Captures = Narrative->GetCaptureCount();
		const int32 Truths = Narrative->GetConfirmedTruthCount();
		if (NightIndex != LastNightIndex)
		{
			LastNightIndex = NightIndex;
			AddEvent(TEXT("night_index"), FString::FromInt(NightIndex));
		}
		if (bHourActive && !bLastHourActive)
		{
			FNightRecord& Night = Nights.AddDefaulted_GetRef();
			Night.Night = NightIndex;
			Night.StartedAt = Now();
			Night.CapturesAtStart = Captures;
			AddEvent(TEXT("night_start"));
		}
		else if (!bHourActive && bLastHourActive && !Nights.IsEmpty())
		{
			Nights.Last().EndedAt = Now();
			Nights.Last().CapturesAtEnd = Captures;
			AddEvent(TEXT("night_end"));
		}
		bLastHourActive = bHourActive;
		if (Captures > LastCaptures)
		{
			AddEvent(TEXT("capture"), FString::FromInt(Captures));
		}
		LastCaptures = Captures;
		if (Truths != LastTruths)
		{
			AddEvent(TEXT("truth"), FString::FromInt(Truths));
			LastTruths = Truths;
		}
		const FName Ending = Narrative->GetEndingChoice();
		if (Ending != LastEnding)
		{
			LastEnding = Ending;
			if (!Ending.IsNone())
			{
				AddEvent(TEXT("ending"), Ending.ToString());
			}
		}
	}
	for (TActorIterator<AIGListenerEntity> It(World); It; ++It)
	{
		const FString Difficulty = StaticEnum<EIGNightDifficulty>()->GetNameStringByValue(static_cast<int64>(It->GetDifficulty()));
		if (Difficulty != LastDifficulty)
		{
			LastDifficulty = Difficulty;
			AddEvent(TEXT("difficulty"), Difficulty);
		}
		break;
	}
	if (const AIGPlayerController* Controller = Cast<AIGPlayerController>(World->GetFirstPlayerController()))
	{
		(Controller->IsUsingGamepadForHud() ? GamepadSeconds : KeyboardSeconds) += SampleDelta;
	}
	const double Seconds = Now();
	if (Seconds - LastTrailSeconds >= IGPlayRecord::TrailInterval)
	{
		LastTrailSeconds = Seconds;
		FTrailPoint& Point = Trail.AddDefaulted_GetRef();
		Point.Seconds = Seconds;
		Point.Location = GetPlayerLocation();
		Point.Night = LastNightIndex;
		Point.bHourActive = bLastHourActive;
		Point.Truths = LastTruths;
		// 오래 머문 자리가 무서워서 기다린 것인지, 뭘 할지 몰라 헤맨 것인지 가르는 데 쓴다.
		for (TActorIterator<AIGNightThreatDirector> It(World); It; ++It)
		{
			if (It->IsBusyForNightRest())
			{
				Point.bThreatActive = true;
				break;
			}
		}
		const APlayerController* PlayerController = World->GetFirstPlayerController();
		if (const AIGPlayerCharacter* Player = Cast<AIGPlayerCharacter>(PlayerController ? PlayerController->GetPawn() : nullptr))
		{
			Point.bHiding = Player->IsInHidingSpot() || Player->IsConcealedInHidingSpot();
			if (const UIGStressComponent* Stress = Player->GetStress())
			{
				Point.Stress = Stress->GetStress();
			}
		}
	}
	if (Seconds - LastSaveSeconds >= IGPlayRecord::SaveInterval)
	{
		LastSaveSeconds = Seconds;
		// 게임이 강제로 꺼져도 30초 전까지는 남는다.
		Save();
	}
	return true;
}

void UIGPlayRecordSubsystem::Save() const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), 2);
	FString ProjectVersion;
	GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), ProjectVersion, GGameIni);
	Root->SetStringField(TEXT("game_version"), ProjectVersion);
	Root->SetStringField(TEXT("started_utc"), StartedUtc);
	Root->SetNumberField(TEXT("duration_seconds"), Now());

	const TSharedRef<FJsonObject> System = MakeShared<FJsonObject>();
	System->SetStringField(TEXT("os"), FPlatformMisc::GetOSVersion());
	System->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand().TrimStartAndEnd());
	System->SetStringField(TEXT("gpu"), GRHIAdapterName);
	System->SetStringField(TEXT("gpu_driver"), GRHIAdapterUserDriverVersion);
	System->SetNumberField(TEXT("memory_gb"), FPlatformMemory::GetConstants().TotalPhysicalGB);
	Root->SetObjectField(TEXT("system"), System);

	const TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
	Settings->SetStringField(TEXT("culture"), FInternationalization::Get().GetCurrentCulture()->GetName());
	Settings->SetNumberField(TEXT("quality"), Scalability::GetQualityLevels().GetSingleQualityLevel());
	if (const UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		const FIntPoint Resolution = UserSettings->GetScreenResolution();
		Settings->SetStringField(TEXT("resolution"), FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y));
		Settings->SetBoolField(TEXT("vsync"), UserSettings->IsVSyncEnabled());
	}
	Settings->SetStringField(TEXT("difficulty"), LastDifficulty);
	Settings->SetNumberField(TEXT("keyboard_seconds"), KeyboardSeconds);
	Settings->SetNumberField(TEXT("gamepad_seconds"), GamepadSeconds);
	Root->SetObjectField(TEXT("settings"), Settings);

	const TSharedRef<FJsonObject> Actions = MakeShared<FJsonObject>();
	for (const TPair<FName, int32>& Pair : ActionCounts)
	{
		const TSharedRef<FJsonObject> Action = MakeShared<FJsonObject>();
		Action->SetNumberField(TEXT("count"), Pair.Value);
		Action->SetNumberField(TEXT("first_seconds"), FirstUse.FindRef(Pair.Key));
		Actions->SetObjectField(Pair.Key.ToString(), Action);
	}
	Root->SetObjectField(TEXT("actions"), Actions);

	TArray<TSharedPtr<FJsonValue>> NightValues;
	for (const FNightRecord& Night : Nights)
	{
		const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
		Value->SetNumberField(TEXT("night"), Night.Night);
		Value->SetNumberField(TEXT("started_seconds"), Night.StartedAt);
		Value->SetNumberField(TEXT("ended_seconds"), Night.EndedAt);
		Value->SetNumberField(TEXT("captures"), (Night.CapturesAtEnd >= 0 ? Night.CapturesAtEnd : LastCaptures) - Night.CapturesAtStart);
		TArray<TSharedPtr<FJsonValue>> Histogram;
		for (const uint32 Count : Night.FrameHistogram)
		{
			Histogram.Add(MakeShared<FJsonValueNumber>(Count));
		}
		Value->SetArrayField(TEXT("frame_ms_histogram"), Histogram);
		NightValues.Add(MakeShared<FJsonValueObject>(Value));
	}
	Root->SetArrayField(TEXT("nights"), NightValues);

	TArray<TSharedPtr<FJsonValue>> EventValues;
	for (const FEvent& Event : Events)
	{
		const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
		Value->SetNumberField(TEXT("t"), FMath::RoundToDouble(Event.Seconds * 10.0) / 10.0);
		Value->SetStringField(TEXT("type"), Event.Type.ToString());
		Value->SetNumberField(TEXT("night"), Event.Night);
		Value->SetStringField(TEXT("at"), Event.Location.ToCompactString());
		if (!Event.Detail.IsEmpty())
		{
			Value->SetStringField(TEXT("detail"), Event.Detail);
		}
		EventValues.Add(MakeShared<FJsonValueObject>(Value));
	}
	Root->SetArrayField(TEXT("events"), EventValues);

	// [초, X, Y, Z, 밤, 밤 진행 중, 확인한 진실 수, 사건 중, 숨음, 공포 수치]. 오래 막힌 자리는 이 줄로 찾고,
	// 뒤의 셋으로 그 자리에서 무서워서 기다렸는지 헤맸는지 가른다. 스키마 1 기록에는 뒤의 셋이 없다.
	TArray<TSharedPtr<FJsonValue>> TrailValues;
	for (const FTrailPoint& Point : Trail)
	{
		TArray<TSharedPtr<FJsonValue>> Row;
		Row.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Point.Seconds)));
		Row.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Point.Location.X)));
		Row.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Point.Location.Y)));
		Row.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Point.Location.Z)));
		Row.Add(MakeShared<FJsonValueNumber>(Point.Night));
		Row.Add(MakeShared<FJsonValueBoolean>(Point.bHourActive));
		Row.Add(MakeShared<FJsonValueNumber>(Point.Truths));
		Row.Add(MakeShared<FJsonValueBoolean>(Point.bThreatActive));
		Row.Add(MakeShared<FJsonValueBoolean>(Point.bHiding));
		Row.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Point.Stress * 100.0f) / 100.0));
		TrailValues.Add(MakeShared<FJsonValueArray>(Row));
	}
	Root->SetArrayField(TEXT("trail"), TrailValues);

	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Root, Writer);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	FFileHelper::SaveStringToFile(Text, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
