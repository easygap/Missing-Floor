#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IGPlayRecord.generated.h"

/**
 * 플레이테스트 기록. -IGPlayRecord로 켰을 때만 동작하고, 기록은 이 PC의
 * Saved/PlayRecords에만 남는다. 개인 정보는 넣지 않는다.
 *
 * 출시 기준(Docs/MISSING_FLOOR_ACCEPTANCE.md)에서 사람이 확인해야 하는 항목을
 * 숫자로 남긴다. 밤별 소요 시간, 붙잡힌 횟수, 진실을 새로 찾지 못한 채 머문 자리와
 * 그때 사건이 벌어지고 있었는지·숨어 있었는지, 기록 화면과 힌트를 연 횟수,
 * 앉기·문 조용히 열기 같은 동작을 처음 쓴 때,
 * 밤별 프레임 시간과 PC 사양이다. 판정은 Scripts/summarize_playtest_records.py가 한다.
 */
UCLASS()
class INDIEGAME_API UIGPlayRecordSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 플레이어가 한 동작 하나. 기록을 켜지 않은 실행에서는 아무것도 하지 않는다. */
	static void Note(const UObject* WorldContext, FName Action);

private:
	struct FEvent
	{
		double Seconds = 0.0;
		FName Type;
		int32 Night = 0;
		FVector Location = FVector::ZeroVector;
		FString Detail;
	};
	struct FTrailPoint
	{
		double Seconds = 0.0;
		FVector Location = FVector::ZeroVector;
		int32 Night = 0;
		bool bHourActive = false;
		int32 Truths = 0;
		/** 사건(손님, 어둑시니, 위층 사람의 조사·추격 등)이 벌어지는 중이었다. 밤에 쉬기를 막는 판정과 같다. */
		bool bThreatActive = false;
		/** 숨는 자리나 잠근 욕실 안에 있었다. */
		bool bHiding = false;
		/** 공포 수치 0~1. 어둠 속에 있기만 해도 오르니 참고로만 본다. */
		float Stress = 0.0f;
	};
	struct FNightRecord
	{
		int32 Night = 0;
		double StartedAt = 0.0;
		double EndedAt = -1.0;
		int32 CapturesAtStart = 0;
		int32 CapturesAtEnd = -1;
		/** 1 ms 칸의 프레임 시간 분포. 마지막 칸은 250 ms 이상이다. */
		TArray<uint32> FrameHistogram;
	};

	void RecordAction(FName Action);
	void RecordFrame();
	bool Sample(float FrameDeltaSeconds);
	void AddEvent(FName Type, const FString& Detail = FString());
	FVector GetPlayerLocation() const;
	void Save() const;
	double Now() const;

	bool bEnabled = false;
	double StartSeconds = 0.0;
	double LastFrameSeconds = 0.0;
	double LastSaveSeconds = 0.0;
	double LastTrailSeconds = -1000.0;
	double LastSampleSeconds = 0.0;
	FString StartedUtc;
	FString OutputPath;
	FTSTicker::FDelegateHandle SampleHandle;
	FDelegateHandle FrameHandle;

	TArray<FEvent> Events;
	TArray<FTrailPoint> Trail;
	TArray<FNightRecord> Nights;
	TMap<FName, int32> ActionCounts;
	TMap<FName, double> FirstUse;
	double KeyboardSeconds = 0.0;
	double GamepadSeconds = 0.0;

	int32 LastNightIndex = -1;
	bool bLastHourActive = false;
	int32 LastCaptures = 0;
	int32 LastTruths = 0;
	FName LastEnding;
	FString LastDifficulty;
};
