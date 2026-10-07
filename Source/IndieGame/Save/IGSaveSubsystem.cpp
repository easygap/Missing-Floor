#include "Save/IGSaveSubsystem.h"

#include "IndieGame.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGStoryStateSubsystem.h"
#include "Save/IGSaveGame.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/PropertyTypeName.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace IGSave
{
	constexpr int32 AutosaveSlotCount = 2;
	const TCHAR* AutosaveSlotPrefix = TEXT("AutoSave");
	// 이 빌드에서 진행을 복원하는 맵은 하나다. 다른 에셋이나 예전 맵을
	// 체크포인트로 받으면 불러오기에 성공한 뒤 맵 이동에서 멎는다.
	const FName PlayableMap(TEXT("/Game/Maps/Prologue_Morning"));

	// 정상 저장은 수 KiB다. 손상된 길이/개수를 믿고 메모리를 잡지 않도록
	// 파일, 문자열, 중첩 깊이와 전체 항목 수를 각각 제한한다.
	constexpr int64 MaxSaveBytes = 8 * 1024 * 1024;
	constexpr int32 MaxStringCharacters = 1024;
	constexpr int32 MaxContainerElements = 1024;
	constexpr int32 MaxTotalElements = 4096;
	constexpr int32 MaxPropertyTags = 4096;
	constexpr int32 MaxNestingDepth = 16;

	class FCheckedSaveReader final : public FMemoryReader
	{
	public:
		explicit FCheckedSaveReader(const TArray<uint8>& InBytes)
			: FMemoryReader(InBytes, true), Bytes(InBytes)
		{
			ArMaxSerializeSize = MaxStringCharacters;
		}

		bool HasBytes(const int64 Count, const int64 End)
		{
			return !IsError() && Count >= 0 && Tell() >= 0
				&& End <= TotalSize() && Tell() <= End && Count <= End - Tell();
		}

		template <typename T>
		bool Read(T& Value, const int64 End)
		{
			if (!HasBytes(sizeof(T), End))
			{
				return false;
			}
			*this << Value;
			return !IsError();
		}

		bool Skip(const int64 Count, const int64 End)
		{
			if (!HasBytes(Count, End))
			{
				return false;
			}
			Seek(Tell() + Count);
			return true;
		}

		bool ReadString(FString& Value, const int64 End)
		{
			const int64 Start = Tell();
			int32 Length = 0;
			if (!Read(Length, End) || Length == MIN_int32)
			{
				return false;
			}
			const int64 Characters = FMath::Abs(static_cast<int64>(Length));
			const int32 CharacterBytes = Length < 0 ? 2 : 1;
			if (Characters > MaxStringCharacters || !HasBytes(Characters * CharacterBytes, End))
			{
				return false;
			}
			// 엔진이 문자열을 보정하기 전에 종결 문자와 중간 NUL도 검사한다.
			for (int64 Index = 0; Index < Characters; ++Index)
			{
				const int32 ByteOffset = static_cast<int32>(Tell() + Index * CharacterBytes);
				const uint16 Character = Bytes[ByteOffset]
					| (CharacterBytes == 2 ? static_cast<uint16>(Bytes[ByteOffset + 1]) << 8 : 0);
				if ((Character == 0) != (Index == Characters - 1) || Character == 0xffff)
				{
					return false;
				}
			}
			Seek(Start);
			*this << Value;
			return !IsError();
		}

		bool ReadHeader()
		{
			// GameplayStatics.cpp의 GVAS v3 헤더. 오래된 무표식 형식은 읽지 않는다.
			const int64 End = TotalSize();
			uint32 Magic = 0;
			int32 Format = 0;
			FPackageFileVersion PackageVersion;
			if (!Read(Magic, End) || Magic != 0x53415647
				|| !Read(Format, End) || Format != 3
				|| !Read(PackageVersion.FileVersionUE4, End)
				|| !Read(PackageVersion.FileVersionUE5, End)
				|| PackageVersion.FileVersionUE4 != GPackageFileUEVersion.FileVersionUE4
				|| PackageVersion < EUnrealEngineObjectUE5Version::PROPERTY_TAG_COMPLETE_TYPE_NAME
				|| !GPackageFileUEVersion.IsCompatible(PackageVersion))
			{
				return false;
			}
			uint16 Major = 0, Minor = 0, Patch = 0;
			uint32 Changelist = 0;
			FString Branch;
			if (!Read(Major, End) || !Read(Minor, End) || !Read(Patch, End)
				|| !Read(Changelist, End) || !ReadString(Branch, End)
				|| Major != FEngineVersion::Current().GetMajor()
				|| Minor > FEngineVersion::Current().GetMinor())
			{
				return false;
			}
			SetUEVer(PackageVersion);
			SetEngineVer(FEngineVersion(Major, Minor, Patch, Changelist, Branch));

			int32 CustomFormat = 0, CustomCount = 0;
			if (!Read(CustomFormat, End)
				|| CustomFormat != static_cast<int32>(ECustomVersionSerializationFormat::Optimized)
				|| !Read(CustomCount, End) || CustomCount < 0 || CustomCount > 512
				|| !HasBytes(static_cast<int64>(CustomCount) * 20, End))
			{
				return false;
			}
			const FCustomVersionContainer CurrentVersions = FCurrentCustomVersions::GetAll();
			FCustomVersionContainer SavedVersions;
			for (int32 Index = 0; Index < CustomCount; ++Index)
			{
				FGuid Key;
				int32 Version = 0;
				*this << Key;
				if (!Read(Version, End))
				{
					return false;
				}
				const FCustomVersion* KnownVersion = CurrentVersions.GetVersion(Key);
				if (!KnownVersion || Version < 0 || Version > KnownVersion->Version
					|| SavedVersions.GetVersion(Key))
				{
					return false;
				}
				SavedVersions.SetVersion(Key, Version, NAME_None);
			}
			SetCustomVersions(SavedVersions);
			FString ClassName;
			return ReadString(ClassName, End)
				&& ClassName == UIGSaveGame::StaticClass()->GetPathName();
		}

		bool ValidateObject()
		{
			const int64 End = TotalSize();
			if (UEVer() >= EUnrealEngineObjectUE5Version::PROPERTY_TAG_EXTENSION_AND_OVERRIDABLE_SERIALIZATION)
			{
				uint8 Extensions = 0;
				if (!Read(Extensions, End) || Extensions != 0)
				{
					return false;
				}
			}
			// IGSaveGame에는 객체 GUID가 없다. 임의 GUID 등록도 허용하지 않는다.
			uint32 HasGuid = 0;
			return ValidateProperties(UIGSaveGame::StaticClass(), End, 0)
				&& Read(HasGuid, End) && HasGuid == 0 && Tell() == End;
		}

	private:
		bool ValidateType(const UE::FPropertyTypeName* Expected, const int64 End,
			const int32 Depth, int32& Nodes)
		{
			FString Name;
			int32 Children = 0;
			if (Depth > MaxNestingDepth || ++Nodes > 32
				|| !ReadString(Name, End) || Name.IsEmpty()
				|| !Read(Children, End) || Children < 0 || Children > 8
				|| (Expected && (!Name.Equals(Expected->GetName().ToString(), ESearchCase::IgnoreCase)
					|| Children != Expected->GetParameterCount())))
			{
				return false;
			}
			for (int32 Index = 0; Index < Children; ++Index)
			{
				const UE::FPropertyTypeName Child = Expected ? Expected->GetParameter(Index) : UE::FPropertyTypeName();
				if (!ValidateType(Expected ? &Child : nullptr, End, Depth + 1, Nodes))
				{
					return false;
				}
			}
			return true;
		}

		bool ReadCount(int32& Count, const int64 End)
		{
			if (!Read(Count, End) || Count < 0 || Count > MaxContainerElements
				|| Count > End - Tell() || Count > MaxTotalElements - TotalElements)
			{
				return false;
			}
			TotalElements += Count;
			return true;
		}

		bool ValidateValue(const FProperty* Property, const int64 End, const int32 Depth)
		{
			if (Depth > MaxNestingDepth)
			{
				return false;
			}
			if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				int32 Count = 0;
				if (!ReadCount(Count, End))
				{
					return false;
				}
				for (int32 Index = 0; Index < Count; ++Index)
				{
					if (!ValidateValue(Array->Inner, End, Depth + 1))
					{
						return false;
					}
				}
				return true;
			}
			if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
			{
				if (Struct->Struct == TBaseStructure<FDateTime>::Get())
				{
					return Skip(sizeof(int64), End);
				}
				if (Struct->Struct == FGameplayTagContainer::StaticStruct())
				{
					int32 Count = 0;
					if (!ReadCount(Count, End))
					{
						return false;
					}
					FString Tag;
					for (int32 Index = 0; Index < Count; ++Index)
					{
						if (!ReadString(Tag, End))
						{
							return false;
						}
					}
					return true;
				}
				return !Struct->UseBinaryOrNativeSerialization(*this)
					&& ValidateProperties(Struct->Struct, End, Depth + 1);
			}
			const FByteProperty* Byte = CastField<FByteProperty>(Property);
			if (CastField<FNameProperty>(Property) || CastField<FStrProperty>(Property)
				|| CastField<FEnumProperty>(Property) || (Byte && Byte->Enum))
			{
				FString Value;
				return ReadString(Value, End);
			}
			if (CastField<FNumericProperty>(Property))
			{
				return Skip(Property->GetElementSize(), End);
			}
			// 현재 저장에는 Map, Set, 객체 참조가 없다. 새 필드는 읽기 검사를
			// 함께 추가해야 하며, 알 수 없는 할당 경로는 여기서 멈춘다.
			return false;
		}

		bool ValidateProperties(const UStruct* Struct, const int64 End, const int32 Depth)
		{
			if (Depth > MaxNestingDepth)
			{
				return false;
			}
			while (++PropertyTags <= MaxPropertyTags)
			{
				FString Name;
				if (!ReadString(Name, End) || Name.IsEmpty())
				{
					return false;
				}
				if (Name.Equals(TEXT("None"), ESearchCase::IgnoreCase))
				{
					return true;
				}
				const FProperty* Property = FindFProperty<FProperty>(Struct, *Name);
				if (!Property)
				{
					// 에디터의 속성 이름 리다이렉트도 실제로 읽힐 타입으로 검사한다.
					for (const UStruct* Owner = Struct; Owner; Owner = Owner->GetSuperStruct())
					{
						const FName Redirect = FProperty::FindRedirectedPropertyName(Owner, FName(*Name));
						if (!Redirect.IsNone())
						{
							Property = FindFProperty<FProperty>(Struct, Redirect);
							break;
						}
					}
				}
				const UE::FPropertyTypeName Type = Property ? UE::FPropertyTypeName(Property) : UE::FPropertyTypeName();
				int32 Nodes = 0, Size = 0, ArrayIndex = 0;
				uint8 Flags = 0;
				if (!ValidateType(Property ? &Type : nullptr, End, 0, Nodes)
					|| !Read(Size, End) || Size < 0 || !Read(Flags, End)
					|| (Flags & ~0x1b) != 0
					|| ((Flags & 0x01) && (!Read(ArrayIndex, End) || ArrayIndex != 0))
					|| ((Flags & 0x02) && !Skip(16, End)) || !HasBytes(Size, End))
				{
					return false;
				}
				const int64 PropertyEnd = Tell() + Size;
				if (Property)
				{
					const bool bBool = CastField<FBoolProperty>(Property) != nullptr;
					if (Property->ArrayDim != 1
						|| ((Flags & 0x08) != 0) != Property->UseBinaryOrNativeSerialization(*this)
						|| ((Flags & 0x10) && !bBool)
						|| (bBool ? Size != 0 : !ValidateValue(Property, PropertyEnd, Depth + 1))
						|| Tell() != PropertyEnd)
					{
						return false;
					}
				}
				// 예전 저장에서 없어진 필드는 엔진처럼 크기만큼 건너뛴다.
				Seek(PropertyEnd);
			}
			return false;
		}

		const TArray<uint8>& Bytes;
		int32 PropertyTags = 0;
		int32 TotalElements = 0;
	};

	static UIGSaveGame* LoadCheckedSave(const FString& SlotName)
	{
		if (SlotName.IsEmpty() || SlotName.Contains(TEXT(".."))
			|| SlotName.Contains(TEXT("/")) || SlotName.Contains(TEXT("\\"))
			|| SlotName.Contains(TEXT(":")))
		{
			return nullptr;
		}
		// Windows의 GenericSaveGameSystem과 같은 경로다. 같은 파일 핸들에서
		// 크기를 확인하고 읽어, 검사 직후 파일이 커져도 대량 할당하지 않는다.
		const FString Filename = FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".sav"));
		TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Filename));
		if (!File || File->TotalSize() < 32 || File->TotalSize() > MaxSaveBytes)
		{
			return nullptr;
		}
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(static_cast<int32>(File->TotalSize()));
		File->Serialize(Bytes.GetData(), Bytes.Num());
		if (File->IsError() || File->TotalSize() != Bytes.Num())
		{
			return nullptr;
		}
		File.Reset();
		FCheckedSaveReader Reader(Bytes);
		if (!Reader.ReadHeader())
		{
			return nullptr;
		}
		const int64 ObjectStart = Reader.Tell();
		if (!Reader.ValidateObject())
		{
			return nullptr;
		}
		Reader.Seek(ObjectStart);
		UIGSaveGame* Save = NewObject<UIGSaveGame>(GetTransientPackage());
		FObjectAndNameAsStringProxyArchive Archive(Reader, false);
		Archive.ArMaxSerializeSize = MaxStringCharacters;
		Save->Serialize(Archive);
		return !Reader.IsError() && !Archive.IsError() && Reader.Tell() == Reader.TotalSize()
			? Save : nullptr;
	}

	/**
	 * 없는 층에서 쓴 저장인지. 없는 층의 자동 저장은 모두 이 챕터 태그를
	 * 달지만, 서사가 한 발이라도 나아간 저장도 같이 쳐 준다.
	 */
	static bool IsMissingFloorSave(const UIGSaveGame& SaveGame)
	{
		const FGameplayTag MissingFloor = FGameplayTag::RequestGameplayTag(
			FName(TEXT("Chapter.MissingFloor")),
			false);
		const FIGMissingFloorNarrativeSnapshot& MissingFloorSnapshot =
			SaveGame.Progress.MissingFloorNarrative;
		return SaveGame.Progress.ChapterId.MatchesTagExact(MissingFloor)
			|| MissingFloorSnapshot.Night.NightIndex > 0
			|| MissingFloorSnapshot.Night.CompletedBeats.Num() > 0
			|| MissingFloorSnapshot.Truths.Num() > 0;
	}
}

bool UIGSaveSubsystem::RequestSave(
	const FString& SlotName,
	const FGameplayTag ChapterId,
	const FName MapPackageName,
	const FGameplayTag CheckpointTag)
{
	return BeginSave(
		SlotName,
		ChapterId,
		MapPackageName,
		CheckpointTag,
		false,
		INDEX_NONE);
}

bool UIGSaveSubsystem::ClearRotatingAutosaves()
{
	// An explicit R/M reset supersedes any coalesced checkpoint that belonged
	// to the old run. If an async write is already in flight, erase it from
	// its completion callback so it cannot win the race and resurrect an end.
	QueuedAutosave = nullptr;
	LastLoadedSave = nullptr;
	bLastLoadedProgressApplied = false;
	if (bLoadInProgress)
	{
		return false;
	}
	if (bSaveInProgress)
	{
		bClearAutosavesAfterActiveSave = true;
		return true;
	}
	return ClearRotatingAutosavesNow();
}

bool UIGSaveSubsystem::ClearRotatingAutosavesNow()
{
	bool bAllSlotsCleared = true;
	for (int32 SlotIndex = 0; SlotIndex < IGSave::AutosaveSlotCount; ++SlotIndex)
	{
		const FString SlotName = FString::Printf(
			TEXT("%s_%d"),
			IGSave::AutosaveSlotPrefix,
			SlotIndex);
		if (UGameplayStatics::DoesSaveGameExist(SlotName, LocalUserIndex))
		{
			if (!UGameplayStatics::DeleteGameInSlot(SlotName, LocalUserIndex))
			{
				bAllSlotsCleared = false;
				UE_LOG(
					LogIndieGame,
					Error,
					TEXT("Failed to delete autosave slot '%s' for local user %d."),
					*SlotName,
					LocalUserIndex);
			}
		}
	}
	if (bAllSlotsCleared)
	{
		NextAutosaveIndex = 0;
	}
	bClearAutosavesAfterActiveSave = false;
	return bAllSlotsCleared;
}

bool UIGSaveSubsystem::BeginSave(
	const FString& SlotName,
	const FGameplayTag ChapterId,
	const FName MapPackageName,
	const FGameplayTag CheckpointTag,
	const bool bIsAutosave,
	const int32 AutosaveIndex,
	UIGSaveGame* PrebuiltSnapshot)
{
	if (IsBusy() || SlotName.IsEmpty())
	{
		return false;
	}

	PendingSave = PrebuiltSnapshot
		? PrebuiltSnapshot
		: CreateSaveSnapshot(ChapterId, MapPackageName, CheckpointTag);
	if (!PendingSave)
	{
		return false;
	}

	bActiveSaveIsAutosave = bIsAutosave;
	ActiveAutosaveIndex = bIsAutosave ? AutosaveIndex : INDEX_NONE;
	bSaveInProgress = true;
	FAsyncSaveGameToSlotDelegate CompletionDelegate;
	CompletionDelegate.BindUObject(this, &ThisClass::HandleSaveComplete);
	UGameplayStatics::AsyncSaveGameToSlot(PendingSave, SlotName, LocalUserIndex, CompletionDelegate);
	return true;
}

UIGSaveGame* UIGSaveSubsystem::CreateSaveSnapshot(
	const FGameplayTag ChapterId,
	const FName MapPackageName,
	const FGameplayTag CheckpointTag) const
{
	UIGSaveGame* Snapshot = Cast<UIGSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UIGSaveGame::StaticClass()));
	if (!Snapshot)
	{
		return nullptr;
	}

	Snapshot->Progress.SchemaVersion = UIGSaveGame::CurrentSchemaVersion;
	Snapshot->Progress.SavedAtUtc = FDateTime::UtcNow();
	Snapshot->Progress.ChapterId = ChapterId;
	Snapshot->Progress.MapPackageName = MapPackageName;
	Snapshot->Progress.CheckpointTag = CheckpointTag;

	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UIGStoryStateSubsystem* StoryState =
			GameInstance->GetSubsystem<UIGStoryStateSubsystem>())
		{
			Snapshot->Progress.StoryStateTags = StoryState->GetStateSnapshot();
		}
		if (const UIGMissingFloorNarrativeSubsystem* MissingFloorState =
			GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
		{
			Snapshot->Progress.MissingFloorNarrative =
				MissingFloorState->GetSnapshot();
		}
	}

	return Snapshot;
}

bool UIGSaveSubsystem::RequestAutosave(
	const FGameplayTag ChapterId,
	const FName MapPackageName,
	const FGameplayTag CheckpointTag)
{
	if (!ChapterId.IsValid()
		|| MapPackageName.IsNone()
		|| !CheckpointTag.IsValid())
	{
		// The front end only offers navigable checkpoints. Reject malformed
		// writes here as well so a bad caller cannot poison both rotating slots.
		UE_LOG(
			LogIndieGame,
			Error,
			TEXT(
				"Rejected malformed autosave: chapter_valid=%d map='%s' "
				"checkpoint_valid=%d."),
			ChapterId.IsValid() ? 1 : 0,
			*MapPackageName.ToString(),
			CheckpointTag.IsValid() ? 1 : 0);
		return false;
	}
	if (bLoadInProgress || bApplyingLoadedProgress)
	{
		// Never mix a pre-load checkpoint with state that is being replaced by
		// a load, including synchronous tag callbacks during snapshot restore.
		return false;
	}

	if (bSaveInProgress || QueuedAutosave)
	{
		// Coalesce to a complete immutable snapshot captured at request time.
		QueuedAutosave = CreateSaveSnapshot(ChapterId, MapPackageName, CheckpointTag);
		if (!QueuedAutosave)
		{
			return false;
		}

		if (!bSaveInProgress)
		{
			ProcessQueuedAutosave();
		}

		return true;
	}

	const int32 AutosaveIndex = NextAutosaveIndex;
	const FString SlotName = FString::Printf(
		TEXT("%s_%d"),
		IGSave::AutosaveSlotPrefix,
		AutosaveIndex);

	return BeginSave(
		SlotName,
		ChapterId,
		MapPackageName,
		CheckpointTag,
		true,
		AutosaveIndex);
}

bool UIGSaveSubsystem::RequestLoad(const FString& SlotName)
{
	if (IsBusy() || SlotName.IsEmpty())
	{
		return false;
	}

	bLoadInProgress = true;
	LastLoadedSave = nullptr;
	bLastLoadedProgressApplied = false;
	// 불러오기 전에 잡아 둔 스냅샷이 복원한 진행을 다시 덮어쓰지 않게 한다.
	QueuedAutosave = nullptr;

	// 메뉴는 월드를 일시정지하므로 월드 타이머 대신 CoreTicker를 쓴다.
	// 검증한 객체의 강한 참조를 다음 tick까지 보관하고 다시 읽지 않는다.
	TStrongObjectPtr<UIGSaveGame> LoadedSave(IGSave::LoadCheckedSave(SlotName));
	FTSTicker::GetCoreTicker().AddTicker(TEXT("IGSaveLoadCompletion"), 0.0f,
		[WeakThis = TWeakObjectPtr<UIGSaveSubsystem>(this), SlotName, LoadedSave](float)
		{
			if (UIGSaveSubsystem* SaveSubsystem = WeakThis.Get())
			{
				SaveSubsystem->HandleLoadComplete(SlotName, SaveSubsystem->LocalUserIndex, LoadedSave.Get());
			}
			return false;
		});
	return true;
}

bool UIGSaveSubsystem::RequestLoadLatestAutosave()
{
	if (IsBusy())
	{
		return false;
	}

	FString NewestSlot;
	return FindNewestCompatibleAutosave(NewestSlot)
		&& RequestLoad(NewestSlot);
}

bool UIGSaveSubsystem::HasCompatibleAutosave() const
{
	FString NewestSlot;
	return FindNewestCompatibleAutosave(NewestSlot);
}

bool UIGSaveSubsystem::HasEndingBAutosave() const
{
	FString NewestSlot;
	if (!FindNewestCompatibleAutosave(NewestSlot))
	{
		return false;
	}
	const UIGSaveGame* Newest = IGSave::LoadCheckedSave(NewestSlot);
	return Newest
		&& Newest->Progress.MissingFloorNarrative.Night.EndingChoice
			== FName(TEXT("Ending.B"));
}

bool UIGSaveSubsystem::FindNewestCompatibleAutosave(
	FString& OutSlotName) const
{
	OutSlotName.Reset();
	FDateTime NewestTimestamp = FDateTime::MinValue();
	for (int32 AutosaveIndex = 0;
		AutosaveIndex < IGSave::AutosaveSlotCount;
		++AutosaveIndex)
	{
		const FString SlotName = FString::Printf(
			TEXT("%s_%d"),
			IGSave::AutosaveSlotPrefix,
			AutosaveIndex);
		if (!UGameplayStatics::DoesSaveGameExist(SlotName, LocalUserIndex))
		{
			continue;
		}

		const UIGSaveGame* Candidate = IGSave::LoadCheckedSave(SlotName);
		if (IsAutosaveLoadable(Candidate)
			&& (OutSlotName.IsEmpty()
				|| Candidate->Progress.SavedAtUtc > NewestTimestamp))
		{
			OutSlotName = SlotName;
			NewestTimestamp = Candidate->Progress.SavedAtUtc;
		}
	}

	return !OutSlotName.IsEmpty();
}

bool UIGSaveSubsystem::ApplyLoadedProgress()
{
	return ApplyLoadedProgressInternal(true);
}

bool UIGSaveSubsystem::ApplyLoadedProgressInternal(
	const bool bBroadcastStoryChanges)
{
	if (bLastLoadedProgressApplied)
	{
		return true;
	}
	if (!IsAutosaveLoadable(LastLoadedSave))
	{
		return false;
	}

	TGuardValue<bool> ApplyingGuard(bApplyingLoadedProgress, true);
	UIGSaveGame::MigrateFlashlightOwnership(LastLoadedSave->Progress);
	bool bApplied = false;
	// 서사 스냅샷을 스토리 태그보다 먼저 되돌린다. RestoreStateSnapshot은 태그
	// 변화를 그 자리에서 알리고, 밤 디렉터는 그 콜백 안에서 게이트를, 월드
	// 씬은 고른 물의 봉투를 다시 맞춘다. 순서를 뒤집으면 불러오기 전 상태로
	// 한 번 맞춰 버린다.
	if (UIGMissingFloorNarrativeSubsystem* MissingFloorState =
		GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>())
	{
		MissingFloorState->RestoreSnapshot(
			LastLoadedSave->Progress.MissingFloorNarrative);
		bApplied = true;
	}
	if (UIGStoryStateSubsystem* StoryState =
		GetGameInstance()->GetSubsystem<UIGStoryStateSubsystem>())
	{
		StoryState->RestoreStateSnapshot(
			LastLoadedSave->Progress.StoryStateTags,
			bBroadcastStoryChanges);
		bApplied = true;
	}

	bLastLoadedProgressApplied = bApplied;
	return bApplied;
}

void UIGSaveSubsystem::HandleSaveComplete(
	const FString& SlotName,
	const int32 UserIndex,
	const bool bSuccess)
{
	bSaveInProgress = false;
	PendingSave = nullptr;
	if (!bSuccess)
	{
		UE_LOG(
			LogIndieGame,
			Error,
			TEXT("Save operation failed for slot '%s' (user %d)."),
			*SlotName,
			UserIndex);
	}
	if (bSuccess && bActiveSaveIsAutosave && ActiveAutosaveIndex != INDEX_NONE)
	{
		NextAutosaveIndex = (ActiveAutosaveIndex + 1) % IGSave::AutosaveSlotCount;
	}

	bActiveSaveIsAutosave = false;
	ActiveAutosaveIndex = INDEX_NONE;
	if (bClearAutosavesAfterActiveSave)
	{
		if (!ClearRotatingAutosavesNow())
		{
			UE_LOG(
				LogIndieGame,
				Error,
				TEXT("Deferred rotating autosave cleanup failed."));
		}
	}
	ProcessQueuedAutosave();
	OnSaveCompleted.Broadcast(bSuccess, SlotName);
}

void UIGSaveSubsystem::HandleLoadComplete(
	const FString& SlotName,
	const int32 UserIndex,
	USaveGame* LoadedObject)
{
	bLoadInProgress = false;
	UIGSaveGame* TypedSave = Cast<UIGSaveGame>(LoadedObject);
	// 슬롯 이름을 직접 지정한 불러오기도 자동 선택과 같은 기준을 적용한다.
	const bool bLoaded = IsAutosaveLoadable(TypedSave);
	LastLoadedSave = bLoaded ? TypedSave : nullptr;
	bLastLoadedProgressApplied = false;

	const FName SavedMap = bLoaded
		? LastLoadedSave->Progress.MapPackageName
		: NAME_None;
	const bool bWillTravel = !SavedMap.IsNone();
	const bool bApplied = bLoaded
		&& ApplyLoadedProgressInternal(!bWillTravel);
	const bool bSuccess = bLoaded && bApplied;
	if (!bSuccess)
	{
		UE_LOG(
			LogIndieGame,
			Error,
			TEXT(
				"Load operation failed for slot '%s' (user %d, compatible=%d, "
				"applied=%d)."),
			*SlotName,
			UserIndex,
			bLoaded ? 1 : 0,
			bApplied ? 1 : 0);
	}
	OnLoadCompleted.Broadcast(bSuccess, SlotName, LastLoadedSave);

	if (bSuccess && bWillTravel)
	{
		// 없는 층이 아닌 저장은 이어하기 목록에 오르지 않는다. 슬롯을 이름으로
		// 직접 불러온 경우에만 뒤쪽 갈래를 타고, 그때는 맵만 다시 연다.
		const bool bIsMissingFloorSave =
			IGSave::IsMissingFloorSave(*LastLoadedSave);
		const FString TravelOptions =
			bIsMissingFloorSave
				? TEXT("IGMissingFloor=1?IGResumeSave=1")
				: TEXT("IGResumeSave=1");
		UGameplayStatics::OpenLevel(
			this,
			SavedMap,
			true,
			TravelOptions);
	}
	ProcessQueuedAutosave();
}

void UIGSaveSubsystem::ProcessQueuedAutosave()
{
	if (!QueuedAutosave || IsBusy())
	{
		return;
	}

	UIGSaveGame* Snapshot = QueuedAutosave;
	QueuedAutosave = nullptr;
	const int32 AutosaveIndex = NextAutosaveIndex;
	const FString SlotName = FString::Printf(
		TEXT("%s_%d"),
		IGSave::AutosaveSlotPrefix,
		AutosaveIndex);

	BeginSave(
		SlotName,
		Snapshot->Progress.ChapterId,
		Snapshot->Progress.MapPackageName,
		Snapshot->Progress.CheckpointTag,
		true,
		AutosaveIndex,
		Snapshot);
}

bool UIGSaveSubsystem::IsSaveCompatible(const UIGSaveGame* SaveGame) const
{
	return SaveGame
		&& SaveGame->Progress.SchemaVersion > 0
		&& SaveGame->Progress.SchemaVersion <= UIGSaveGame::CurrentSchemaVersion;
}

bool UIGSaveSubsystem::IsAutosaveLoadable(const UIGSaveGame* SaveGame) const
{
	// 예전 이야기(CH01~CH03)의 자동 저장은 돌아갈 장면이 없다. 목록에서 조용히
	// 빼서 타이틀이 이어하기를 권하지 않게 한다.
	return IsSaveCompatible(SaveGame)
		&& SaveGame->Progress.ChapterId.IsValid()
		&& !SaveGame->Progress.MapPackageName.IsNone()
		&& SaveGame->Progress.MapPackageName == IGSave::PlayableMap
		&& FPackageName::DoesPackageExist(SaveGame->Progress.MapPackageName.ToString())
		&& SaveGame->Progress.CheckpointTag.IsValid()
		&& IGSave::IsMissingFloorSave(*SaveGame);
}
