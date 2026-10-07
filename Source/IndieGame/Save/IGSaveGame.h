#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTagContainer.h"
#include "Narrative/IGMissingFloorNarrativeTypes.h"
#include "IGSaveGame.generated.h"

USTRUCT(BlueprintType)
struct INDIEGAME_API FIGProgressSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Save")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Save")
	FDateTime SavedAtUtc;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Save")
	FGameplayTag ChapterId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Save")
	FName MapPackageName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Save")
	FGameplayTag CheckpointTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Save")
	FGameplayTagContainer StoryStateTags;

	/**
	 * 없는 층의 서사 상태. 태그 직렬화라 이 칸이 생기기 전 저장에서는
	 * 기본값으로 읽히고, 그래서 스키마 번호를 올리지 않았다.
	 *
	 * 예전 이야기의 REBIRTH 스냅샷 칸은 2026-09-28에 지웠다. 옛 저장에 남은
	 * 그 칸은 이름이 맞는 속성이 없어 불러올 때 통째로 건너뛴다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Save")
	FIGMissingFloorNarrativeSnapshot MissingFloorNarrative;
};

/** Versioned story progress. User settings belong in a separate save object. */
UCLASS()
class INDIEGAME_API UIGSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentSchemaVersion = 4;
	/** 손전등을 밤마다 받던 시절의 밤 저장은 그 손전등을 가진 채로 이어 간다. */
	static void MigrateFlashlightOwnership(FIGProgressSnapshot& Snapshot);

	UIGSaveGame();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Save")
	FIGProgressSnapshot Progress;
};
