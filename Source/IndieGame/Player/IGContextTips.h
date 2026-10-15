#pragma once

#include "CoreMinimal.h"
#include "Player/IGHudGuidance.h"
#include "Player/IGOnboardingMemory.h"

/**
 * 조작은 쓸 일이 생긴 순간에 한 줄만 알려 준다. 입주하자마자 조작표를 통째로
 * 보여 주면 대부분은 필요할 때 이미 잊었다. 한 번에 하나만 띄우고, 끝까지
 * 보여 준 안내는 프로필에 기억해 다시 띄우지 않는다.
 */
enum class EIGContextTip : uint8
{
	None,
	/** 첫 밤이 조금 지나서. 발소리를 줄이는 법. */
	Crouch,
	/** 그가 가까이서 듣고 있을 때. */
	HoldBreath,
	/** 처음 쫓길 때. */
	Sprint,
	/** 처음으로 단서가 기록됐을 때(낮). */
	Journal,
	/** 한동안 새로 알아낸 것이 없을 때. */
	Hint,
	/** H 힌트가 풀이를 말하기 직전. 매번 띄운다. */
	HintAnswer
};

struct FIGContextTips
{
	static constexpr float ShowSeconds = 5.5f;
	/** 목표 줄과 같은 속도로 오르내린다. */
	static constexpr float FadeSeconds = FIGHudGuidance::FadeSeconds;
	/** 자막에 가려 이만큼 기다려도 못 띄우면 이번에는 접는다. 상황이 이미 지나갔다. */
	static constexpr float StaleSeconds = 8.0f;

	static const TCHAR* Name(const EIGContextTip Tip)
	{
		switch (Tip)
		{
		case EIGContextTip::Crouch: return TEXT("Crouch");
		case EIGContextTip::HoldBreath: return TEXT("HoldBreath");
		case EIGContextTip::Sprint: return TEXT("Sprint");
		case EIGContextTip::Journal: return TEXT("Journal");
		case EIGContextTip::Hint: return TEXT("Hint");
		case EIGContextTip::HintAnswer: return TEXT("HintAnswer");
		default: return TEXT("None");
		}
	}

	/** 기억하지 않는 안내. 떠 있던 다른 안내를 밀어내고 바로 띄운다. */
	void Show(const EIGContextTip Tip)
	{
		Current = Tip;
		Remaining = ShowSeconds;
		Waited = 0.0f;
	}

	/** 이미 띄운 적이 있거나 다른 안내가 떠 있으면 아무것도 하지 않는다. */
	void Offer(const EIGContextTip Tip)
	{
		if (Tip == EIGContextTip::None || Current != EIGContextTip::None
			|| IGOnboardingMemory::WasTipShown(Name(Tip)))
		{
			return;
		}
		Current = Tip;
		Remaining = ShowSeconds;
		Waited = 0.0f;
	}

	/** bLaneFree: 아래쪽 줄이 비어 있어 실제로 그릴 수 있는가. 가려진 시간은 세지 않는다. */
	void Update(const float DeltaSeconds, const bool bLaneFree)
	{
		if (Current == EIGContextTip::None)
		{
			return;
		}
		const float Delta = FMath::Max(0.0f, DeltaSeconds);
		if (!bLaneFree)
		{
			Waited += Delta;
			if (Waited >= StaleSeconds && Remaining >= ShowSeconds)
			{
				Current = EIGContextTip::None;
			}
			return;
		}
		Remaining -= Delta;
		if (Remaining <= 0.0f)
		{
			if (Current != EIGContextTip::HintAnswer)
			{
				IGOnboardingMemory::MarkTipShown(Name(Current));
			}
			Current = EIGContextTip::None;
		}
	}

	/** 안내한 동작을 직접 해 봤다. 더 볼 필요가 없으니 곧바로 내리고 기억한다. */
	void Acknowledge(const EIGContextTip Tip)
	{
		IGOnboardingMemory::MarkTipShown(Name(Tip));
		if (Current == Tip)
		{
			Remaining = FMath::Min(Remaining, FadeSeconds);
		}
	}

	/** 메뉴·암전·연출이 화면을 가져가면 떠 있던 안내는 기억하지 않고 접는다. */
	void Interrupt()
	{
		if (Current != EIGContextTip::None && Remaining >= ShowSeconds)
		{
			return;
		}
		Current = EIGContextTip::None;
	}

	EIGContextTip GetCurrent() const { return Current; }
	float Alpha() const
	{
		if (Current == EIGContextTip::None)
		{
			return 0.0f;
		}
		const float FadeIn = FMath::SmoothStep(0.0f, FadeSeconds, ShowSeconds - Remaining);
		const float FadeOut = FMath::SmoothStep(0.0f, FadeSeconds, Remaining);
		return FMath::Min(FadeIn, FadeOut);
	}

private:
	EIGContextTip Current = EIGContextTip::None;
	float Remaining = 0.0f;
	float Waited = 0.0f;
};
