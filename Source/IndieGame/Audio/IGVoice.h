#pragma once

#include "CoreMinimal.h"
#include "Player/IGHorrorHUD.h"

class UAudioComponent;
class USoundBase;

/**
 * 인물의 목소리. 미리 녹음해 검수한 한국어 대사를 /Game/Audio/Voice/ko/VO_<줄 키>에서 찾는다.
 * 줄 키는 그 대사의 NSLOCTEXT 키다. 녹음이 없는 줄은 지금까지처럼 글로 된 대화 줄이 되고,
 * 게임을 하는 중에 목소리를 만들지는 않는다. 자막은 어느 언어로 켜도 한국어 목소리 위에 뜬다.
 */
namespace IGVoice
{
	/** 목소리가 나는 자리. 거리·가림·울림은 엔진이 맡고, 녹음 원본에는 효과를 굽지 않는다. */
	enum class ESource : uint8
	{
		/** 같은 공간에서 마주 보고 하는 말. */
		Person,
		/** 문 너머에서 들리는 말. 문짝이 고역을 먹는다. 존재의 소리라 ENTITY 버스에 건다. */
		ThroughDoor,
		/** 녹음기·휴대폰 스피커. 좁은 대역으로 장치 자리에서 난다. */
		Device,
		/** 유담 자신의 말. 내 머리에서 나므로 정위하지 않는다. */
		Self,
		/**
		 * 문이나 판 너머의 사람(401호 문 안의 황순금, 판 뒤의 정우, 잠긴 문 앞의 목한수). 문짝이
		 * 고역을 먹는 것은 ThroughDoor와 같고, 사람의 말이라 SPEECH 버스에 건다.
		 */
		PersonThroughDoor,
		/** 다른 층에서 계단을 타고 오는 사람의 말. 거리로는 줄지 않고 층 가림과 고역 손실만 남는다. */
		PersonAcrossFloors
	};

	/** 그 대사의 줄 키(NSLOCTEXT 키). 조립한 문장처럼 키가 없으면 None. */
	INDIEGAME_API FName LineIdOf(const FText& Line);

	/** 화자 이름이 유담인가. 상대와 유담의 말을 한 함수로 번갈아 내는 대화가 Self를 고를 때 쓴다. */
	INDIEGAME_API bool IsYudam(const FText& Speaker);

	/** 그 줄의 녹음. 없으면 nullptr. 찾은 녹음은 약하게 들고 있어서 다 쓰면 메모리에서 내려간다. */
	INDIEGAME_API USoundBase* Find(FName LineId);

	/**
	 * 대사 한 줄. 녹음이 있으면 그 줄이 화면에 오를 때 Where가 가리키는 자리에서 목소리를
	 * 내고, 자막은 음성 대사 자막 설정을 따른다. 녹음이 없으면 글로 된 줄을 그 칸(대화·장치)에
	 * 낸다. Where는 소리를 낼 때 부르므로 움직이는 사람의 자리를 그때 읽는다.
	 */
	INDIEGAME_API void Say(
		const UObject* WorldContext,
		const FText& Speaker,
		const FText& Line,
		FIGDialogueScope Scope,
		TFunction<FVector()> Where,
		ESource Source = ESource::Person,
		EIGDialogueChannel Channel = EIGDialogueChannel::Conversation,
		float MinimumDurationSeconds = 0.0f,
		EIGDialoguePriority Priority = EIGDialoguePriority::Story);

	/** 그 줄의 녹음 길이(초). 녹음이 없으면 0. 다음 말을 소리 길이에 맞춰 이을 때 쓴다. */
	INDIEGAME_API float GetSeconds(const FText& Line);

	/** 지금 그 자리에서 목소리를 낸다. Say가 줄 차례에 부르고, 숨처럼 줄 밖의 소리에도 쓴다. */
	INDIEGAME_API UAudioComponent* Play(const UObject* WorldContext, USoundBase* Sound, const FVector& Where, ESource Source);

	/** 검사용. 그 줄 키에 녹음 대신 쓸 소리를 건다(nullptr면 푼다). */
	INDIEGAME_API void SetSoundForTesting(FName LineId, USoundBase* Sound);
}
