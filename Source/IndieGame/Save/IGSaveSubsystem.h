#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IGSaveSubsystem.generated.h"

class UIGSaveGame;
class USaveGame;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FIGSaveCompletedSignature,
	bool, bSuccess,
	FString, SlotName);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FIGLoadCompletedSignature,
	bool, bSuccess,
	FString, SlotName,
	UIGSaveGame*, SaveGame);

/** Serializes checkpoint snapshots without blocking the game thread. */
UCLASS()
class INDIEGAME_API UIGSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool RequestSave(
		const FString& SlotName,
		FGameplayTag ChapterId,
		FName MapPackageName,
		FGameplayTag CheckpointTag);

	/** Alternates between two autosave files so one previous checkpoint survives. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool RequestAutosave(
		FGameplayTag ChapterId,
		FName MapPackageName,
		FGameplayTag CheckpointTag);

	UFUNCTION(BlueprintCallable, Category = "Save")
	bool RequestLoad(const FString& SlotName);

	/** Selects the newest compatible rotating autosave, then loads it. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool RequestLoadLatestAutosave();

	/** True only when at least one rotating autosave can be loaded by this build. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool HasCompatibleAutosave() const;

	/**
	 * 검사용: 가장 최근에 쓴 자동 저장을 이어하기와 같은 검사로 읽어, 「들은 말」이 몇 줄 든 채 받아들여지는지.
	 * 받아들여지지 않으면 -1이다. 「들은 말」이 든 저장이 조용히 걸러지던 일을 배포본에서 다시 보기 위해 둔다.
	 */
	int32 GetLastAutosaveHeardLinesForTesting() const;

	/**
	 * §9 「밤 5」: 엔딩 B를 본 세이브가 있는지.
	 *
	 * 슬롯이 조용히 늘어나는 근거는 플레이어가 아니라 **세이브**다 — 그 세이브가
	 * 그 밤을 기억하고 있기 때문에 한 줄이 늘어난다. 그래서 설정 파일에 별도
	 * 플래그를 심지 않고 최신 호환 자동 저장을 그대로 읽는다. 슬롯을 위한 실제
	 * 세이브 파일은 만들지 않는다(§14).
	 */
	bool HasEndingBAutosave() const;

	/** Invalidates both rotating autosaves after an explicit restart/menu reset. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool ClearRotatingAutosaves();

	UFUNCTION(BlueprintCallable, Category = "Save")
	bool ApplyLoadedProgress();

	UFUNCTION(BlueprintPure, Category = "Save")
	bool IsBusy() const { return bSaveInProgress || bLoadInProgress; }

	UFUNCTION(BlueprintPure, Category = "Save")
	bool IsApplyingLoadedProgress() const { return bApplyingLoadedProgress; }

	UFUNCTION(BlueprintPure, Category = "Save")
	UIGSaveGame* GetLastLoadedSave() const { return LastLoadedSave; }

	UPROPERTY(BlueprintAssignable, Category = "Save")
	FIGSaveCompletedSignature OnSaveCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Save")
	FIGLoadCompletedSignature OnLoadCompleted;

private:
	bool BeginSave(
		const FString& SlotName,
		FGameplayTag ChapterId,
		FName MapPackageName,
		FGameplayTag CheckpointTag,
		bool bIsAutosave,
		int32 AutosaveIndex,
		UIGSaveGame* PrebuiltSnapshot = nullptr);
	UIGSaveGame* CreateSaveSnapshot(
		FGameplayTag ChapterId,
		FName MapPackageName,
		FGameplayTag CheckpointTag) const;
	void ProcessQueuedAutosave();
	bool ClearRotatingAutosavesNow();
	void HandleSaveComplete(const FString& SlotName, int32 UserIndex, bool bSuccess);
	void HandleLoadComplete(const FString& SlotName, int32 UserIndex, USaveGame* LoadedObject);
	bool ApplyLoadedProgressInternal(bool bBroadcastStoryChanges);
	bool FindNewestCompatibleAutosave(FString& OutSlotName) const;
	bool IsAutosaveLoadable(const UIGSaveGame* SaveGame) const;
	bool IsSaveCompatible(const UIGSaveGame* SaveGame) const;

	UPROPERTY(Transient)
	TObjectPtr<UIGSaveGame> PendingSave;

	UPROPERTY(Transient)
	TObjectPtr<UIGSaveGame> LastLoadedSave;

	UPROPERTY(Transient)
	TObjectPtr<UIGSaveGame> QueuedAutosave;

	bool bSaveInProgress = false;
	bool bLoadInProgress = false;
	bool bApplyingLoadedProgress = false;
	bool bLastLoadedProgressApplied = false;
	bool bActiveSaveIsAutosave = false;
	bool bClearAutosavesAfterActiveSave = false;
	int32 ActiveAutosaveIndex = INDEX_NONE;
	int32 NextAutosaveIndex = 0;
	int32 LocalUserIndex = 0;
};
