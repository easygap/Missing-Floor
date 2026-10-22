#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGLiningProbe.generated.h"

class AIGAccessHatch;
class AIGLiningEntity;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;

/**
 * -IGLiningProbe 전용. 덧댄 것의 규칙을 302호 벽을 빌려 한 실행에서 겪는다.
 *
 * 북쪽 벽에 접혀 붙은 몸 앞에서 작은 점검구를 연다. 몸이 처음 떨어져 작은 틈으로 가서 누르고,
 * 누르는 동안 덮개를 닫으면 다리에 걸려 튕기는지 본다. 큰 점검구를 열면 작은 쪽을 막은 채
 * 큰 쪽으로 옮겨 가야 한다. 옮기는 도중에 작은 쪽을 다시 뜯어도 방향을 꺾지 않고 큰 쪽을 마저
 * 막아야 하고, 막은 뒤에도 그보다 좁은 작은 쪽으로는 돌아가지 않아야 한다. 작은 쪽을 큰 쪽보다 넓게
 * 떼어 내면 그때 그리로 간다. 소리와 손전등에는 반응하지 않는다. 문간에 서 있는
 * 사람은 눌리지 않는다. 움직이는 동안 몸 살이 처지고(한도 안에서), 패드가 떨어질 때 막 끝이 짚었던
 * 면에 남아 늘어났다가 놓이는지도 잰다. 몸 둘레 막은 들려 있을 때 처지고 틈을 누를 때 면을 따라 퍼져야
 * 하고, 걷는 동안 늘 두 발은 면을 누르고 있어야 하며, 짚은 발은 자리에서 미끄러지지 않아야 한다.
 *
 * -IGLiningShots를 같이 주면 렌더가 있는 실행에서 걸음마다 화면을 찍는다(Saved/LiningReview). -IGLiningFrames까지
 * 주면 큰 점검구로 옮겨 가는 동안을 문간에서 30fps로 이어 찍는다(Saved/LiningFrames, 처짐과 막을 보는 재료).
 */
UCLASS()
class INDIEGAME_API AIGLiningProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGLiningProbe();
	virtual void Tick(float DeltaSeconds) override;
	void Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Stand(const FVector& Feet, const FVector& LookAt);
	void Shoot(const TCHAR* Name);
	void Next(int32 NewPhase);

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGLiningEntity> Lining;
	TWeakObjectPtr<AIGAccessHatch> SmallHatch;
	TWeakObjectPtr<AIGAccessHatch> LargeHatch;
	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	float ShotClock = 0.0f;
	int32 ShotIndex = 0;
	bool bShots = false;
	bool bFrames = false;
	int32 FrameIndex = 0;
	int32 FrameTick = 0;
	bool bFirstMove = false;
	bool bBlockedBounce = false;
	bool bGripped = false;
	/** 움직이는 동안 잰 몸 살의 가장 큰 처짐과 패드 막의 가장 긴 늘어남(cm), 늘어났다가 놓인 적이 있는가. */
	float MaxSag = 0.0f;
	float MaxSkinStretch = 0.0f;
	bool bSkinWasStuck[4] = {};
	bool bSkinLetGo = false;
	/** 틈을 누르는 동안 몸 둘레 막이 퍼진 가장 큰 거리, 걷는 동안 처진 가장 큰 거리(cm), 걷는 동안 면을 누른 발의 가장 적은 수. */
	float MaxSkirtSpread = 0.0f;
	float MaxSkirtDroop = 0.0f;
	int32 MinPadsHolding = 4;
	TArray<FName> Sealed;
	TArray<FName> Targets;
};
