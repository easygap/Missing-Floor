#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"

class AActor;
class AIGSwingDoor;
class UWorld;
enum class EIGFootstepSurface : uint8;

/**
 * 점을 이어 만든 이동 경로. 정해진 경로를 걷는 NPC(셋째 낮 303호, 셋째 아침 경찰관과
 * 관리인)가 같이 쓴다. 이동한 거리를 넣으면 경로 위 위치와 방향, 그 구간이 계단인지를 돌려준다.
 */
struct INDIEGAME_API FIGWalkerRoute
{
	TArray<FVector> Points;
	/** 첫 점에서 각 점까지 누적 거리(cm). */
	TArray<float> Distances;
	/** 수평 1 cm에 이보다 많이 오르내리면 계단 구간이다. 0이면 StairRise로 판단한다. */
	float StairSlope = 0.0f;
	/** StairSlope가 0일 때, 높이가 이보다 많이 바뀌는 구간이 계단이다. */
	float StairRise = 30.0f;

	void Set(const TArray<FVector>& InPoints);
	float Length() const { return Distances.IsEmpty() ? 0.0f : Distances.Last(); }
	bool IsEmpty() const { return Points.IsEmpty(); }
	/** Distance만큼 이동한 위치. 경로가 비었으면 Fallback을 돌려준다. */
	FVector At(float Distance, const FVector& Fallback, FVector* OutDirection = nullptr, bool* bOutOnStair = nullptr) const;
};

/** 경로를 걷는 NPC가 같이 쓰는 것. 애니메이션 전환, 회전, 걸음 박자, 발소리, 계단실 방화문 판정. */
namespace IGWalker
{
	/** 애니메이션을 바꾼다. 같은 애니메이션이면 재생 속도만 바꾼다. Active에는 지금 재생 중인 애니메이션을 기억해 둔다. */
	template <typename TActivePtr>
	void PlayBody(USkeletalMeshComponent* Body, TActivePtr& Active, UAnimSequence* Sequence, const float Rate, const bool bLoop = true)
	{
		if (!Body || !Sequence)
		{
			return;
		}
		if (Active != Sequence)
		{
			Body->PlayAnimation(Sequence, bLoop);
			Active = Sequence;
		}
		Body->SetPlayRate(Rate);
	}

	/** 현재 yaw에서 목표 yaw 쪽으로 한 프레임만큼 돈다. Speed가 클수록 빨리 따라간다. */
	INDIEGAME_API float TurnYaw(float CurrentYaw, float TargetYaw, float DeltaSeconds, float Speed);

	/** 걸음 위상을 진행시킨다. 반 주기를 넘겨 새 발을 디뎠으면 true다. */
	INDIEGAME_API bool AdvanceStepPhase(float& Phase, float DeltaSeconds, float Rate, float HalfCycleSeconds);

	/** 발밑 바닥 재질. 계단 철판, 옥상 방수층, 5층 석고 가루를 구분하고 나머지는 콘크리트로 본다. */
	INDIEGAME_API EIGFootstepSurface FindSurface(const UWorld* World, const FVector& Feet, const AActor* Ignore);

	/**
	 * 바닥에 맞는 발소리를 낸다. 녹음 샘플이 있으면 그중에서 고르고 없으면 합성한다. 피치는
	 * PitchBase에서 걸음마다 조금씩 달라진다. Seed는 걸음마다 바뀐다.
	 */
	INDIEGAME_API void PlayFootstep(
		UObject* Context,
		EIGFootstepSurface Surface,
		uint32& Seed,
		const FVector& Feet,
		float Volume,
		float PitchBase,
		float SynthPitch,
		float SynthAmplitude);

	/**
	 * From에서 To로 가는 걸음이 계단실 방화문을 지나는가. 문짝은 경첩에서 +Y로 120 cm이고
	 * 닫히면 경첩과 같은 X 평면이다. 다 열려 멈춘 문은 길을 막지 않고, 움직이는 중인 문은 문 앞
	 * 한 걸음 안에서도 막는다. 위층 사람·관리인·303호가 같은 판정을 쓴다.
	 */
	INDIEGAME_API bool IsStairDoorAhead(const AIGSwingDoor& Door, const FVector& From, const FVector& To);
}
