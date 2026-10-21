#include "Player/IGInputBindingSubsystem.h"

#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerInput.h"
#include "Misc/ConfigCacheIni.h"

namespace IGInputBinding
{
	const TCHAR* ConfigSection = TEXT("IndieGame.InputBindings");

	/**
	 * §18.1의 입력 지도. 기본값은 `Config/DefaultInput.ini`와 같은 값이어야
	 * 하며, 정적 계약이 두 자리를 대조한다 — 여기만 고치면 처음 실행한
	 * 사람은 표에 없는 키를 쓰게 된다.
	 */
	static const TArray<FIGBindableActionInfo>& ActionTable()
	{
		static const TArray<FIGBindableActionInfo> Table = {
			{
				TEXT("Sprint"),
				NSLOCTEXT("IGInput", "ActionSprint", "달리기"),
				NSLOCTEXT("IGInput", "DescSprint", "빠르게 달립니다. 발소리와 숨소리가 커집니다."),
				EKeys::LeftShift,
				EKeys::Gamepad_LeftThumbstick,
			},
			{
				TEXT("Crouch"),
				NSLOCTEXT("IGInput", "ActionCrouch", "앉기"),
				NSLOCTEXT("IGInput", "DescCrouch", "몸을 낮춰 조용히 움직입니다."),
				EKeys::C,
				EKeys::Gamepad_RightThumbstick,
			},
			{
				TEXT("Interact"),
				NSLOCTEXT("IGInput", "ActionInteract", "조사 · 상호작용"),
				NSLOCTEXT("IGInput", "DescInteract", "물건을 조사하거나 문을 엽니다. 문은 길게 누르면 조용히 열 수 있습니다."),
				EKeys::E,
				EKeys::Gamepad_FaceButton_Bottom,
			},
			{
				TEXT("Knock"),
				NSLOCTEXT("IGInput", "ActionKnock", "두드리기"),
				NSLOCTEXT("IGInput", "DescKnock", "문이나 벽을 두드립니다. 돌아오는 소리를 들어 보세요."),
				EKeys::Q,
				EKeys::Gamepad_FaceButton_Right,
			},
			{
				TEXT("Listen"),
				NSLOCTEXT("IGInput", "ActionListen", "귀 대기"),
				NSLOCTEXT("IGInput", "DescListen", "벽에 귀를 대고 듣습니다. 키보드에서는 벽을 보며 조사 키를 길게 누르세요."),
				EKeys::Invalid,
				EKeys::Gamepad_RightTrigger,
			},
			{
				TEXT("HoldBreath"),
				NSLOCTEXT("IGInput", "ActionHoldBreath", "숨 참기"),
				NSLOCTEXT("IGInput", "DescHoldBreath", "잠깐 숨을 참습니다. 너무 오래 참으면 숨소리가 거칠어집니다."),
				EKeys::LeftControl,
				EKeys::Gamepad_LeftTrigger,
			},
			{
				TEXT("Flashlight"),
				NSLOCTEXT("IGInput", "ActionFlashlight", "손전등"),
				NSLOCTEXT("IGInput", "DescFlashlight", "손전등을 켜거나 끕니다. 딸깍 소리도 위층 사람이 들을 수 있습니다."),
				EKeys::F,
				EKeys::Gamepad_FaceButton_Left,
			},
			{
				TEXT("Jump"),
				NSLOCTEXT("IGInput", "ActionJump", "점프"),
				NSLOCTEXT("IGInput", "DescJump", "낮은 장애물을 뛰어넘습니다. 착지하면 소리가 납니다."),
				EKeys::SpaceBar,
				EKeys::Gamepad_LeftShoulder,
			},
			{
				TEXT("Journal"),
				NSLOCTEXT("IGInput", "ActionJournal", "기록 보기"),
				NSLOCTEXT("IGInput", "DescJournal", "낮에 그동안 찾은 단서를 다시 볼 수 있습니다."),
				EKeys::Tab,
				EKeys::Gamepad_FaceButton_Top,
			},
			{
				TEXT("RequestHint"),
				NSLOCTEXT("IGInput", "ActionHint", "힌트"),
				NSLOCTEXT("IGInput", "DescHint", "진행이 막혔을 때 힌트를 봅니다. 여러 번 누르면 더 자세히 알려 줍니다."),
				EKeys::H,
				EKeys::Gamepad_RightShoulder,
			},
			{
				TEXT("LoadAutosave"),
				NSLOCTEXT("IGInput", "ActionLoadAutosave", "자동 저장 불러오기"),
				NSLOCTEXT("IGInput", "DescLoadAutosave", "최근 자동 저장부터 다시 시작합니다. 밤이 진행되는 동안에는 불러올 수 없습니다."),
				EKeys::F9,
				// 패드 기본값이 없다. Y는 기록 보기에 쓰고 있고, 한 장치에서
				// 두 동사가 같은 버튼을 쓰는 것을 이 화면이 금지한다.
				EKeys::Invalid,
			},
			{
				TEXT("GameplayGuide"),
				NSLOCTEXT("IGInput", "ActionGuide", "목표 · 조작 안내"),
				NSLOCTEXT("IGInput", "DescGuide", "목표와 조작법을 10초 동안 표시합니다. 한 번 더 누르면 닫힙니다."),
				EKeys::F1,
				EKeys::Gamepad_DPad_Up,
			},
		};
		return Table;
	}
}

void UIGInputBindingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadOverrides();
	LoadLookSettings();
	ApplyAllBindings();
}

int32 UIGInputBindingSubsystem::GetActionCount()
{
	return IGInputBinding::ActionTable().Num();
}

const FIGBindableActionInfo& UIGInputBindingSubsystem::GetActionInfo(
	const int32 ActionIndex)
{
	const TArray<FIGBindableActionInfo>& Table = IGInputBinding::ActionTable();
	return Table[FMath::Clamp(ActionIndex, 0, Table.Num() - 1)];
}

bool UIGInputBindingSubsystem::IsReservedKey(const FKey& Key)
{
	// 나가는 문은 잠글 수 없다. 일시정지와 접근성 패널을 잃으면 그 뒤의
	// 어떤 설정도 손댈 수 없고, 재설정 화면 자체로 돌아올 수도 없다.
	return Key == EKeys::Escape
		|| Key == EKeys::F10
		|| Key == EKeys::Gamepad_Special_Right
		|| Key == EKeys::Gamepad_Special_Left
		|| Key == EKeys::Enter
		|| Key == EKeys::LeftMouseButton;
}

FString UIGInputBindingSubsystem::MakeOverrideKeyName(
	const int32 ActionIndex,
	const bool bGamepad)
{
	return FString::Printf(
		TEXT("%s.%s"),
		*GetActionInfo(ActionIndex).ActionName.ToString(),
		bGamepad ? TEXT("Gamepad") : TEXT("Keyboard"));
}

FKey UIGInputBindingSubsystem::GetBoundKey(
	const int32 ActionIndex,
	const bool bGamepad) const
{
	if (const FKey* Override = Overrides.Find(
		MakeOverrideKeyName(ActionIndex, bGamepad)))
	{
		return *Override;
	}
	const FIGBindableActionInfo& Info = GetActionInfo(ActionIndex);
	return bGamepad ? Info.DefaultGamepad : Info.DefaultKeyboard;
}

bool UIGInputBindingSubsystem::IsDefaultBinding(
	const int32 ActionIndex,
	const bool bGamepad) const
{
	return !Overrides.Contains(MakeOverrideKeyName(ActionIndex, bGamepad));
}

bool UIGInputBindingSubsystem::TryRebind(
	const int32 ActionIndex,
	const bool bGamepad,
	const FKey& NewKey,
	FText& OutFailureReason)
{
	if (!NewKey.IsValid())
	{
		OutFailureReason = NSLOCTEXT(
			"IGInput", "RebindInvalid", "이 입력은 지정할 수 없습니다.");
		return false;
	}
	if (IsReservedKey(NewKey))
	{
		OutFailureReason = NSLOCTEXT(
			"IGInput",
			"RebindReserved",
			"일시정지와 접근성 설정에 쓰는 키라 다른 동작에 지정할 수 없습니다.");
		return false;
	}
	// 칸과 장치가 어긋나면 그 칸에서 영영 눌리지 않는 키가 된다.
	if (bGamepad != NewKey.IsGamepadKey())
	{
		OutFailureReason = bGamepad
			? NSLOCTEXT(
				"IGInput", "RebindNeedsPad", "게임패드 버튼을 눌러 주세요.")
			: NSLOCTEXT(
				"IGInput", "RebindNeedsKeyboard", "키보드나 마우스 버튼을 눌러 주세요.");
		return false;
	}
	// §24 즉시 차단 16. 한 장치 안에서 두 동사가 같은 키를 쓰면 두드리기가
	// 조사의 별칭이 되는 상태를 재설정으로 만들 수 있게 된다.
	for (int32 Other = 0; Other < GetActionCount(); ++Other)
	{
		if (Other == ActionIndex)
		{
			continue;
		}
		if (GetBoundKey(Other, bGamepad) == NewKey)
		{
			OutFailureReason = FText::Format(
				NSLOCTEXT(
					"IGInput",
					"RebindConflict",
					"‘{0}’에 이미 지정된 키입니다. 그쪽을 먼저 바꿔 주세요."),
				GetActionInfo(Other).Label);
			return false;
		}
	}

	const FIGBindableActionInfo& Info = GetActionInfo(ActionIndex);
	const FKey DefaultKey = bGamepad ? Info.DefaultGamepad : Info.DefaultKeyboard;
	const FString OverrideName = MakeOverrideKeyName(ActionIndex, bGamepad);
	if (NewKey == DefaultKey)
	{
		Overrides.Remove(OverrideName);
	}
	else
	{
		Overrides.Add(OverrideName, NewKey);
	}
	SaveOverrides();
	ApplyAllBindings();
	return true;
}

void UIGInputBindingSubsystem::ResetToDefaults()
{
	Overrides.Reset();
	MouseSensitivity = 1.0f;
	GamepadSensitivity = 1.0f;
	VerticalLookScale = 1.0f;
	bInvertLookY = false;
	SaveOverrides();
	SaveLookSettings();
	ApplyAllBindings();
}

void UIGInputBindingSubsystem::AdjustMouseSensitivity(const int32 Direction)
{
	MouseSensitivity = FMath::Clamp(
		FMath::GridSnap(
			MouseSensitivity + Direction * LookSensitivityStep,
			LookSensitivityStep),
		MinimumLookSensitivity,
		MaximumLookSensitivity);
	SaveLookSettings();
}

void UIGInputBindingSubsystem::AdjustGamepadSensitivity(const int32 Direction)
{
	GamepadSensitivity = FMath::Clamp(
		FMath::GridSnap(
			GamepadSensitivity + Direction * LookSensitivityStep,
			LookSensitivityStep),
		MinimumLookSensitivity,
		MaximumLookSensitivity);
	SaveLookSettings();
}

void UIGInputBindingSubsystem::AdjustVerticalLookScale(const int32 Direction)
{
	VerticalLookScale = FMath::Clamp(
		FMath::GridSnap(
			VerticalLookScale + Direction * LookSensitivityStep,
			LookSensitivityStep),
		MinimumVerticalLookScale,
		MaximumVerticalLookScale);
	SaveLookSettings();
}

void UIGInputBindingSubsystem::ToggleInvertLookY()
{
	bInvertLookY = !bInvertLookY;
	SaveLookSettings();
}

void UIGInputBindingSubsystem::LoadLookSettings()
{
	MouseSensitivity = 1.0f;
	GamepadSensitivity = 1.0f;
	VerticalLookScale = 1.0f;
	bInvertLookY = false;
	if (!GConfig)
	{
		return;
	}
	float Stored = 1.0f;
	if (GConfig->GetFloat(
		IGInputBinding::ConfigSection,
		TEXT("MouseSensitivity"),
		Stored,
		GGameUserSettingsIni))
	{
		MouseSensitivity = FMath::Clamp(
			Stored, MinimumLookSensitivity, MaximumLookSensitivity);
	}
	if (GConfig->GetFloat(
		IGInputBinding::ConfigSection,
		TEXT("GamepadSensitivity"),
		Stored,
		GGameUserSettingsIni))
	{
		GamepadSensitivity = FMath::Clamp(
			Stored, MinimumLookSensitivity, MaximumLookSensitivity);
	}
	if (GConfig->GetFloat(
		IGInputBinding::ConfigSection,
		TEXT("VerticalLookScale"),
		Stored,
		GGameUserSettingsIni))
	{
		VerticalLookScale = FMath::Clamp(
			Stored, MinimumVerticalLookScale, MaximumVerticalLookScale);
	}
	GConfig->GetBool(
		IGInputBinding::ConfigSection,
		TEXT("InvertLookY"),
		bInvertLookY,
		GGameUserSettingsIni);
}

void UIGInputBindingSubsystem::SaveLookSettings() const
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetFloat(
		IGInputBinding::ConfigSection,
		TEXT("MouseSensitivity"),
		MouseSensitivity,
		GGameUserSettingsIni);
	GConfig->SetFloat(
		IGInputBinding::ConfigSection,
		TEXT("GamepadSensitivity"),
		GamepadSensitivity,
		GGameUserSettingsIni);
	GConfig->SetFloat(
		IGInputBinding::ConfigSection,
		TEXT("VerticalLookScale"),
		VerticalLookScale,
		GGameUserSettingsIni);
	GConfig->SetBool(
		IGInputBinding::ConfigSection,
		TEXT("InvertLookY"),
		bInvertLookY,
		GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UIGInputBindingSubsystem::LoadOverrides()
{
	Overrides.Reset();
	if (!GConfig)
	{
		return;
	}
	for (int32 Index = 0; Index < GetActionCount(); ++Index)
	{
		for (const bool bGamepad : {false, true})
		{
			const FString OverrideName = MakeOverrideKeyName(Index, bGamepad);
			FString Stored;
			if (!GConfig->GetString(
				IGInputBinding::ConfigSection,
				*OverrideName,
				Stored,
				GGameUserSettingsIni)
				|| Stored.IsEmpty())
			{
				continue;
			}
			const FKey StoredKey(*Stored);
			if (!StoredKey.IsValid()
				|| IsReservedKey(StoredKey)
				|| bGamepad != StoredKey.IsGamepadKey())
			{
				// 이 빌드가 모르는 키나 규칙을 어기는 저장은 버린다. 남겨 두면
				// 영영 눌리지 않는 키를 화면이 「지금 이 키」라고 보여 준다.
				continue;
			}
			Overrides.Add(OverrideName, StoredKey);
		}
	}
}

void UIGInputBindingSubsystem::SaveOverrides() const
{
	if (!GConfig)
	{
		return;
	}
	for (int32 Index = 0; Index < GetActionCount(); ++Index)
	{
		for (const bool bGamepad : {false, true})
		{
			const FString OverrideName = MakeOverrideKeyName(Index, bGamepad);
			if (const FKey* Override = Overrides.Find(OverrideName))
			{
				GConfig->SetString(
					IGInputBinding::ConfigSection,
					*OverrideName,
					*Override->GetFName().ToString(),
					GGameUserSettingsIni);
			}
			else
			{
				GConfig->RemoveKey(
					IGInputBinding::ConfigSection,
					*OverrideName,
					GGameUserSettingsIni);
			}
		}
	}
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UIGInputBindingSubsystem::ApplyAllBindings() const
{
	UInputSettings* Settings = UInputSettings::GetInputSettings();
	if (!Settings)
	{
		return;
	}
	for (int32 Index = 0; Index < GetActionCount(); ++Index)
	{
		const FIGBindableActionInfo& Info = GetActionInfo(Index);
		for (const bool bGamepad : {false, true})
		{
			const FKey DefaultKey =
				bGamepad ? Info.DefaultGamepad : Info.DefaultKeyboard;
			const FKey Desired = GetBoundKey(Index, bGamepad);
			if (!DefaultKey.IsValid() && !Desired.IsValid())
			{
				continue;
			}
			if (DefaultKey == Desired)
			{
				continue;
			}
			// 기본 매핑을 걷어내고 원하는 것을 건다. SaveKeyMappings는 부르지
			// 않는다 — 그건 DefaultInput.ini를 쓰는데, 패키지에서는 읽기
			// 전용이고 다음 사람의 기본값까지 바꿔 버린다.
			if (DefaultKey.IsValid())
			{
				Settings->RemoveActionMapping(
					FInputActionKeyMapping(Info.ActionName, DefaultKey), false);
			}
			if (Desired.IsValid())
			{
				Settings->AddActionMapping(
					FInputActionKeyMapping(Info.ActionName, Desired), false);
			}
		}
	}
	Settings->ForceRebuildKeymaps();
}
