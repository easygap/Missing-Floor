#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGAimReachProbe.generated.h"

class AIGNightPhaseDirector;
class AIGPlayerCharacter;

/**
 * -IGAimReachProbe. 밤과 낮을 차례로 열고, 그때 켜져 있는 상호작용 물체마다 사람이 설 수 있는
 * 자리에서 시선이 실제로 닿는지 본다.
 *
 * 계산대 벨(단순 충돌 없음), 5층 배관 밸브(벽 뒤), 셋째 밤 빈 벽 칸(숨긴 넷째 밤 표적이 가림)은 모두
 * 상호작용 함수를 바로 부르는 검사만 있어서 몇 주씩 남았다. 이 검사는 물체 둘레의 바닥 높이에
 * 몸통이 들어가는 자리를 찾아, 눈높이(서기·앉기)에서 상호작용 거리 안으로 시선을 보낸다. 게임의
 * 초점처럼 숨긴 채 꺼 둔 표적은 건너뛴다. 걸어서 그 자리에 갈 수 있는지는 보지 않는다(실제 입력
 * 길 검사가 본다).
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGAimReachProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGAimReachProbe();
	void Configure(AIGPlayerCharacter* InPlayer);
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FAimSpot
	{
		FVector Eye = FVector::ZeroVector;
		FVector Feet = FVector::ZeroVector;
	};

	void Audit(const TCHAR* Label);
	bool FindSpot(AActor* Target, const FBox& Box, FAimSpot& OutSpot, int32& OutTried, TMap<FString, int32>& OutBlockers) const;
	bool IsFloorHeight(float Z) const;
	bool BeginNight(int32 Night);
	bool ConcludeNight();
	void Silence();
	void Finish();

	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	int32 Phase = 0;
	float PhaseSeconds = 0.0f;
	int32 Failures = 0;
	int32 Audited = 0;
	/** 같은 물체를 여러 때에 거듭 세지 않는다. 처음 실패한 때만 적는다. */
	TSet<FString> Reported;
};
