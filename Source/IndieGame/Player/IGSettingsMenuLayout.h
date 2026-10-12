#pragma once

#include "CoreMinimal.h"

/**
 * Shared geometry contract for drawing and hit-testing settings screens.
 *
 * Keeping these numbers outside the HUD prevents rendered rows and pointer
 * targets from drifting apart as resolution or UI scale changes. This file
 * stores geometry only; it does not own presentation or gameplay state.
 */
namespace IGSettingsMenuLayout
{
	/** 다섯 설정 + 언어 + 접근성 + 소리·밝기 + 조작 + 적용/돌아가기 두 줄. */
	constexpr int32 DisplayRowCount = 11;

	/**
	 * 화면 설정 행 번호. 접근성 쪽과 같은 이유로 이름을 준다 — 조작 행을
	 * 하나 끼웠더니 확인 창이 「이 설정 유지」 대신 그 행에 커서를 올려
	 * 두고 있었다.
	 */
	enum EDisplayRow : int32
	{
		WindowMode = 0,
		Resolution,
		Quality,
		VSync,
		FrameLimit,
		Language,
		AccessibilityPanel,
		AudioCalibrationPanel,
		KeyBindingsPanel,
		ApplyOrKeep,
		BackOrRevert
	};
	static_assert(
		BackOrRevert + 1 == DisplayRowCount,
		"화면 설정 행 이름과 행 수가 어긋났다");
	constexpr int32 AccessibilityRowCount = 26;

	/**
	 * 접근성 행 번호. 화면과 컨트롤러와 묶음 범위 셋이 같은 숫자를 봐야 한다.
	 * 번호로만 세면 한 줄 끼울 때 「밝기」가 「듣는 방식」이 된다.
	 */
	enum EAccessibilityRow : int32
	{
		NightDifficulty = 0,
		Hints,
		ReducedCameraMotion,
		ReducedFlicker,
		FieldOfView,
		ComfortVignette,
		CameraTexture,
		CenterDot,
		DirectionalFearCues,
		KnockRippleSubstitute,
		KnockHapticSubstitute,
		HeartbeatWarning,
		CognitiveAssist,
		SoundCaptions,
		CaptionSize,
		CaptionBackground,
		CaptionSafeArea,
		CaptionDuration,
		ToggleCrouch,
		ToggleHold,
		HoldDuration,
		PromptKeys,
		Haptics,
		MicrophoneNoise,
		ResetDefaults,
		CloseMenu
	};
	static_assert(
		CloseMenu + 1 == AccessibilityRowCount,
		"접근성 행 이름과 행 수가 어긋났다");
	constexpr int32 DisplayCategoryCount = 4;
	constexpr int32 AccessibilityCategoryCount = 6;

	struct FCategoryRange
	{
		int32 FirstRow = 0;
		int32 RowCount = 0;
	};

	struct FPanelMetrics
	{
		float Scale = 1.0f;
		FVector2D PanelPosition = FVector2D::ZeroVector;
		FVector2D PanelSize = FVector2D::ZeroVector;
		float CornerRadius = 14.0f;
		float HeaderBottom = 0.0f;
		float FooterTop = 0.0f;
		float RailLeft = 0.0f;
		float RailRight = 0.0f;
		float ContentLeft = 0.0f;
		float ContentRight = 0.0f;
		float CategoryStartY = 0.0f;
		float CategoryRowHeight = 48.0f;
		float OptionStartY = 0.0f;
		float OptionRowHeight = 56.0f;
	};

	inline FPanelMetrics MakePanelMetrics(
		const float ViewportWidth,
		const float ViewportHeight)
	{
		FPanelMetrics Result;
		Result.Scale = FMath::Clamp(
			FMath::Min(ViewportWidth / 1920.0f, ViewportHeight / 1080.0f),
			0.85f,
			2.0f);

		const float HorizontalMargin = FMath::Max(24.0f, 28.0f * Result.Scale);
		const float VerticalMargin = FMath::Max(20.0f, 28.0f * Result.Scale);
		Result.PanelSize = FVector2D(
			FMath::Min(
				FMath::Max(320.0f, ViewportWidth - HorizontalMargin * 2.0f),
				1320.0f * Result.Scale),
			FMath::Min(
				FMath::Max(420.0f, ViewportHeight - VerticalMargin * 2.0f),
				800.0f * Result.Scale));
		Result.PanelPosition = FVector2D(
			(ViewportWidth - Result.PanelSize.X) * 0.5f,
			(ViewportHeight - Result.PanelSize.Y) * 0.5f);
		Result.CornerRadius = 15.0f * Result.Scale;

		const float HeaderHeight = 108.0f * Result.Scale;
		const float FooterHeight = 64.0f * Result.Scale;
		const float RailWidth = FMath::Min(
			248.0f * Result.Scale,
			Result.PanelSize.X * 0.27f);
		Result.HeaderBottom = Result.PanelPosition.Y + HeaderHeight;
		Result.FooterTop = Result.PanelPosition.Y + Result.PanelSize.Y - FooterHeight;
		Result.RailLeft = Result.PanelPosition.X + 18.0f * Result.Scale;
		Result.RailRight = Result.PanelPosition.X + RailWidth;
		Result.ContentLeft = Result.RailRight + 38.0f * Result.Scale;
		Result.ContentRight =
			Result.PanelPosition.X + Result.PanelSize.X - 34.0f * Result.Scale;
		Result.CategoryStartY = Result.HeaderBottom + 48.0f * Result.Scale;
		Result.CategoryRowHeight = FMath::Max(48.0f, 56.0f * Result.Scale);
		Result.OptionStartY = Result.HeaderBottom + 56.0f * Result.Scale;
		Result.OptionRowHeight = FMath::Max(52.0f, 62.0f * Result.Scale);
		return Result;
	}

	/**
	 * 소리와 밝기 화면. 그리는 쪽(HUD)과 마우스로 줄을 고르는 쪽(컨트롤러)이
	 * 같은 숫자를 봐야 한다. 배경 음악·환경음 두 줄을 넣을 때 HUD만 두 줄
	 * 올리고 마우스 판정은 그대로 둬서, 「환경음」을 가리키면 「전체 소리」가
	 * 골라지고 있었다.
	 */
	constexpr int32 AudioCalibrationRowCount = 7;

	struct FAudioCalibrationMetrics
	{
		float Scale = 1.0f;
		FVector2D PanelPosition = FVector2D::ZeroVector;
		FVector2D PanelSize = FVector2D::ZeroVector;
		/** 위쪽 두 칸(노크·어두운 화면)을 가르는 세로선. 메뉴 줄 위에서 멈춘다. */
		float DividerTop = 0.0f;
		float DividerBottom = 0.0f;
		/** 첫 메뉴 줄 글자의 윗변. */
		float RowTop = 0.0f;
		float RowSpacing = 42.0f;
		/** 글자 윗변에서 줄 가운데까지. 마우스 판정은 가운데를 기준으로 잡는다. */
		float RowCenterOffset = 12.0f;
		/** 고른 줄의 설명. 두 줄로 접혀도 마지막 메뉴 줄과 조작 안내 사이에 선다. */
		float NoteTop = 0.0f;
		float FooterTop = 0.0f;
	};

	inline FAudioCalibrationMetrics MakeAudioCalibrationMetrics(
		const float ViewportWidth,
		const float ViewportHeight)
	{
		FAudioCalibrationMetrics Result;
		Result.Scale = FMath::Clamp(
			FMath::Min(ViewportHeight / 1080.0f, ViewportWidth / 1920.0f),
			0.67f,
			2.0f);
		const float Scale = Result.Scale;
		Result.PanelSize = FVector2D(
			FMath::Min(ViewportWidth * 0.76f, 980.0f * Scale),
			FMath::Min(ViewportHeight * 0.76f, 740.0f * Scale));
		Result.PanelPosition = FVector2D(
			(ViewportWidth - Result.PanelSize.X) * 0.5f,
			(ViewportHeight - Result.PanelSize.Y) * 0.5f + 12.0f * Scale);
		// 아래에서부터 쌓는다. 조작 안내, 설명 두 줄, 메뉴 일곱 줄 순서다.
		// 설명이 마지막 메뉴 줄과 같은 높이에 그려져 「저장하고 돌아가기」를
		// 덮던 자리를 이렇게 떼어 놓았다.
		const float PanelBottom = Result.PanelPosition.Y + Result.PanelSize.Y;
		Result.FooterTop = PanelBottom - 34.0f * Scale;
		Result.NoteTop = PanelBottom - 98.0f * Scale;
		Result.RowSpacing = 42.0f * Scale;
		Result.RowCenterOffset = 12.0f * Scale;
		Result.RowTop = PanelBottom - 386.0f * Scale;
		Result.DividerTop = Result.PanelPosition.Y + 122.0f * Scale;
		Result.DividerBottom = FMath::Min(
			Result.PanelPosition.Y + 324.0f * Scale,
			Result.RowTop - 12.0f * Scale);
		return Result;
	}

	inline FCategoryRange GetDisplayCategory(const int32 Category)
	{
		switch (Category)
		{
		case 0: return {0, 2}; // Display mode and resolution.
		case 1: return {2, 3}; // Quality, sync, and frame rate.
		case 2: return {Language, 4}; // 언어, 접근성, 소리와 밝기, 조작 설정.
		case 3: return {ApplyOrKeep, 2}; // 적용과 돌아가기.
		default: return {0, 0};
		}
	}

	inline FCategoryRange GetAccessibilityCategory(const int32 Category)
	{
		switch (Category)
		{
		case 0: return {NightDifficulty, 2};       // 난이도와 힌트.
		case 1: return {ReducedCameraMotion, 6};   // 화면 효과, 렌즈 왜곡, 가운데 점.
		case 2: return {DirectionalFearCues, 5};   // 소리 안내와 노크 도움.
		case 3: return {SoundCaptions, 5};         // 자막. 음성 대사가 없어 대사 자막 줄은 뺐다.
		case 4: return {ToggleCrouch, 6};          // 입력과 조작 안내.
		case 5: return {ResetDefaults, 2};         // General actions.
		default: return {0, 0};
		}
	}

	/** 자막 묶음에는 200% 미리 보기를 위한 높이를 남긴다. 클릭 판정에도 같은 간격을 쓴다. */
	inline FPanelMetrics MakeAccessibilityPanelMetrics(
		const float ViewportWidth,
		const float ViewportHeight,
		const int32 SelectedRow)
	{
		FPanelMetrics Result = MakePanelMetrics(ViewportWidth, ViewportHeight);
		if (SelectedRow >= SoundCaptions && SelectedRow <= CaptionDuration)
		{
			Result.OptionRowHeight = FMath::Max(44.0f, 52.0f * Result.Scale);
			Result.OptionStartY = Result.HeaderBottom + 44.0f * Result.Scale;
		}
		return Result;
	}

	template <typename RangeGetter>
	inline int32 FindCategoryForRow(
		const int32 Row,
		const int32 CategoryCount,
		RangeGetter&& GetRange)
	{
		for (int32 Category = 0; Category < CategoryCount; ++Category)
		{
			const FCategoryRange Range = GetRange(Category);
			if (Row >= Range.FirstRow && Row < Range.FirstRow + Range.RowCount)
			{
				return Category;
			}
		}
		return 0;
	}

	inline bool ContainsPoint(
		const FVector2D& Point,
		const FVector2D& Position,
		const FVector2D& Size)
	{
		return Point.X >= Position.X
			&& Point.Y >= Position.Y
			&& Point.X <= Position.X + Size.X
			&& Point.Y <= Position.Y + Size.Y;
	}

	inline bool ValidatePanelMetrics(
		const float ViewportWidth,
		const float ViewportHeight)
	{
		const FPanelMetrics Metrics = MakePanelMetrics(
			ViewportWidth,
			ViewportHeight);
		const bool bPanelInside = Metrics.PanelPosition.X >= 0.0f
			&& Metrics.PanelPosition.Y >= 0.0f
			&& Metrics.PanelPosition.X + Metrics.PanelSize.X <= ViewportWidth
			&& Metrics.PanelPosition.Y + Metrics.PanelSize.Y <= ViewportHeight;
		const bool bHorizontalOrder = Metrics.RailLeft < Metrics.RailRight
			&& Metrics.RailRight < Metrics.ContentLeft
			&& Metrics.ContentLeft < Metrics.ContentRight;
		const bool bVerticalOrder = Metrics.HeaderBottom < Metrics.OptionStartY
			&& Metrics.OptionStartY
				+ 5.0f * Metrics.OptionRowHeight < Metrics.FooterTop;
		return bPanelInside && bHorizontalOrder && bVerticalOrder;
	}

	/**
	 * Hit-tests both the category rail and the visible option rows. Category
	 * hits are reported separately because they move focus without activating
	 * the first option.
	 */
	template <typename RangeGetter>
	inline bool HitTestSettingsRow(
		const FPanelMetrics& Metrics,
		const FVector2D& Pointer,
		const int32 SelectedRow,
		const int32 CategoryCount,
		RangeGetter&& GetRange,
		int32& OutRow,
		bool& bOutCategoryHit)
	{
		OutRow = INDEX_NONE;
		bOutCategoryHit = false;
		const float Scale = Metrics.Scale;

		for (int32 Category = 0; Category < CategoryCount; ++Category)
		{
			const FVector2D Position(
				Metrics.RailLeft,
				Metrics.CategoryStartY + Category * Metrics.CategoryRowHeight);
			const FVector2D Size(
				Metrics.RailRight - Metrics.RailLeft - 14.0f * Scale,
				Metrics.CategoryRowHeight - 6.0f * Scale);
			if (ContainsPoint(Pointer, Position, Size))
			{
				OutRow = GetRange(Category).FirstRow;
				bOutCategoryHit = true;
				return true;
			}
		}

		const int32 ActiveCategory = FindCategoryForRow(
			SelectedRow,
			CategoryCount,
			GetRange);
		const FCategoryRange ActiveRange = GetRange(ActiveCategory);
		for (int32 LocalRow = 0; LocalRow < ActiveRange.RowCount; ++LocalRow)
		{
			const FVector2D Position(
				Metrics.ContentLeft,
				Metrics.OptionStartY + LocalRow * Metrics.OptionRowHeight);
			const FVector2D Size(
				Metrics.ContentRight - Metrics.ContentLeft,
				Metrics.OptionRowHeight - 7.0f * Scale);
			if (ContainsPoint(Pointer, Position, Size))
			{
				OutRow = ActiveRange.FirstRow + LocalRow;
				return true;
			}
		}
		return false;
	}
}
