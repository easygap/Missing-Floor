#pragma once

#include "CoreMinimal.h"

/**
 * §34.2 재관람 스킵의 손동작.
 *
 * 에필로그와 다섯째 새벽이 같은 조작을 쓴다. 두 디렉터가 각자 적어 두면
 * 한쪽만 고쳐져도 컴파일은 되고, 플레이어는 같은 화면 문법이 자리마다
 * 다르게 반응하는 것을 만난다. 그게 값이 틀린 것보다 나쁘다.
 */
namespace IGReplaySkip
{
	/** 눌러서 넘어가기까지의 시간. */
	constexpr float HoldSeconds = 2.0f;
	/** 손을 떼면 이 배로 되감긴다. 잘못 눌린 것이 빨리 풀린다. */
	constexpr float RewindMultiplier = 2.4f;
	/**
	 * 처음 볼 때 건너뛰기가 열리기까지(§6). 다섯째 새벽은 이 값, 에필로그는
	 * 10초다. 재관람은 처음부터 열려 있다.
	 */
	constexpr float FirstViewDelaySeconds = 15.0f;
	constexpr float FirstViewEpilogueDelaySeconds = 10.0f;
}
