#pragma once

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace IGOnScreen
{
	/**
	 * 몸이 화면에 실제로 그려졌는가. WasRecentlyRendered는 그림자 패스에서도 참이라
	 * 벽 뒤에 있는 사람도 해나 복도 등의 그림자를 드리우는 동안 「보이는」 것으로
	 * 잡혔다. 그래서 아무도 안 보는 계단 끝에 사람이 계속 서 있었다. 플레이어가 보는지를
	 * 따지는 판단(안 볼 때 치우기, 몸이 안 보일 때 자막 달기)은 이걸로 한다.
	 */
	inline bool WasRecentlySeen(const UPrimitiveComponent* Component, const float Tolerance = 0.2f)
	{
		const UWorld* World = Component ? Component->GetWorld() : nullptr;
		if (!World)
		{
			return false;
		}
		const float Threshold = FMath::Max(Tolerance, World->DeltaTimeSeconds + UE_KINDA_SMALL_NUMBER);
		return World->TimeSince(Component->GetLastRenderTimeOnScreen()) <= Threshold;
	}

	/** 액터에 프리미티브가 여럿이면 그중 하나라도 화면에 그려졌는가. */
	inline bool WasRecentlySeen(const AActor* Actor, const float Tolerance = 0.2f)
	{
		bool bSeen = false;
		if (Actor)
		{
			Actor->ForEachComponent<UPrimitiveComponent>(false, [&bSeen, Tolerance](const UPrimitiveComponent* Component)
			{
				bSeen = bSeen || WasRecentlySeen(Component, Tolerance);
			});
		}
		return bSeen;
	}
}
