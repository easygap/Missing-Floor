#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IGInputBindingSubsystem.generated.h"

/**
 * 다시 묶을 수 있는 동사들.
 *
 * §18.1의 입력 지도가 그대로 여기 온다. 순서는 그 표의 순서이고, 화면도
 * 이 순서로 그린다 — 손이 먼저 배우는 다섯 동사가 위에 있어야 한다.
 *
 * 값은 직렬화하지 않는다. 저장에는 액션 이름이 들어가므로 중간에 하나를
 * 끼워 넣어도 옛 설정이 깨지지 않는다.
 */
UENUM()
enum class EIGBindableAction : uint8
{
	Sprint,
	Crouch,
	Interact,
	Knock,
	Listen,
	HoldBreath,
	Flashlight,
	Jump,
	Journal,
	RequestHint,
	LoadAutosave,
	GameplayGuide,
	Count UMETA(Hidden)
};

/** 한 동사의 이름, 화면에 보일 말, 그리고 두 장치의 기본값. */
struct FIGBindableActionInfo
{
	FName ActionName;
	FText Label;
	/** §18.1에서 이 동사가 무엇을 하는지. 설명 줄에 그대로 나간다. */
	FText Description;
	FKey DefaultKeyboard;
	FKey DefaultGamepad;
};

/**
 * 런타임 키 재설정 (§19.8).
 *
 * 2026 기준으로 「모든 입력은 다시 묶을 수 있어야 한다」가 접근성의 기본선이다.
 * 다만 이 게임에는 **묶으면 안 되는 자리**가 있다.
 *
 * - `Esc`와 `F10`은 고정이다. 일시정지와 접근성 패널로 돌아가는 길이 없어지면
 *   그 뒤의 어떤 설정도 손댈 수 없다. 나가는 문을 잠글 수 있게 두지 않는다
 * - 한 장치 안에서 두 동사가 같은 키를 쓸 수 없다. §24 즉시 차단 16이
 *   「두드리기가 Interact의 별칭으로 남으면 안 된다」를 출시 조건으로 걸어
 *   두었는데, 재설정으로 그 상태를 만들 수 있으면 계약이 무의미해진다
 * - 키보드 칸에는 키보드·마우스 키만, 패드 칸에는 패드 키만 들어간다
 *
 * 저장은 `GameUserSettings.ini`다. 진행 세이브에는 넣지 않는다 — 저장본이
 * 키 배열을 잠그면 다른 자리에서 시작한 사람이 자기 손을 못 쓴다.
 */
UCLASS()
class INDIEGAME_API UIGInputBindingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static int32 GetActionCount();
	static const FIGBindableActionInfo& GetActionInfo(int32 ActionIndex);

	/** 지금 묶여 있는 키. 저장된 것이 없으면 기본값을 돌려준다. */
	FKey GetBoundKey(int32 ActionIndex, bool bGamepad) const;
	bool IsDefaultBinding(int32 ActionIndex, bool bGamepad) const;

	/**
	 * 새 키를 받아 본다. 거절하면 false와 함께 **왜** 거절했는지를 돌려준다 —
	 * 「안 됩니다」만 말하는 재설정 화면은 고장과 구분되지 않는다.
	 */
	bool TryRebind(
		int32 ActionIndex,
		bool bGamepad,
		const FKey& NewKey,
		FText& OutFailureReason);

	void ResetToDefaults();

	// -- 시점 (§18.3) ------------------------------------------------------
	//
	// 1인칭인데 감도를 못 바꾸는 게임은 손이 맞지 않는 사람에게 그 자리에서
	// 끝난다. 마우스와 패드는 곡선이 달라 값을 따로 둔다 — 하나로 묶으면
	// 한쪽을 맞추는 순간 다른 쪽이 어긋난다.

	/**
	 * 조작 화면에서 동사 목록보다 위에 오는 시점 행의 수. 화면과 컨트롤러가
	 * 같은 값을 봐야 행 번호가 어긋나지 않는다.
	 */
	static constexpr int32 LookRowCount = 4;

	static constexpr float MinimumLookSensitivity = 0.40f;
	static constexpr float MaximumLookSensitivity = 2.00f;
	static constexpr float LookSensitivityStep = 0.10f;
	// §18.3. 좌우와 상하를 같은 감도로 두면 한쪽이 늘 어긋난다. 폭이 좁은
	// 것은 이게 취향이 아니라 보정이기 때문이다.
	static constexpr float MinimumVerticalLookScale = 0.70f;
	static constexpr float MaximumVerticalLookScale = 1.30f;

	float GetMouseSensitivity() const { return MouseSensitivity; }
	float GetGamepadSensitivity() const { return GamepadSensitivity; }
	bool IsLookInverted() const { return bInvertLookY; }
	float GetVerticalLookScale() const { return VerticalLookScale; }

	/** 단계로 움직인다. 화면이 값을 직접 쓰지 않고 이 함수만 부른다. */
	void AdjustMouseSensitivity(int32 Direction);
	void AdjustGamepadSensitivity(int32 Direction);
	void ToggleInvertLookY();
	void AdjustVerticalLookScale(int32 Direction);

	/** 이 키는 어떤 경우에도 다시 묶을 수 없다. */
	static bool IsReservedKey(const FKey& Key);

private:
	void LoadOverrides();
	void SaveOverrides() const;
	/** 저장된 배열을 엔진의 입력 설정에 실제로 반영한다. */
	void ApplyAllBindings() const;
	static FString MakeOverrideKeyName(int32 ActionIndex, bool bGamepad);

	void LoadLookSettings();
	void SaveLookSettings() const;

	/** 액션 이름 + 장치 → 덮어쓴 키. 비어 있으면 기본값이다. */
	TMap<FString, FKey> Overrides;

	float MouseSensitivity = 1.0f;
	float GamepadSensitivity = 1.0f;
	bool bInvertLookY = false;
	float VerticalLookScale = 1.0f;
};
