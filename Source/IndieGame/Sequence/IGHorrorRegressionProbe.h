#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGHorrorRegressionProbe.generated.h"

class AIGBathroomRefuge;
class AIGDoorLatch;
class AIGListenerEntity;
class AIGMissingFloorNightThreeDirector;
class AIGNightLoopDirector;
class AIGNightPhaseDirector;
class AIGNightThreatDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGSeoRecorder;
class AIGSwingDoor;

/**
 * -IGHorrorRegressionProbe 전용. 2026-10-13에 고친 겹침과 구멍을 한 실행에서 다시 겪는다.
 *
 * 1. 벽의 대답을 기다리는 동안에는 다른 괴이가 끼어들지 않고, 그때 부른 자동 저장은 끝난 뒤에 된다.
 * 2. 단서를 다 모으기 전 5층에서 둘-쉬고-하나를 치면 벽은 대답하지 않고 한 번 알아챈다.
 * 3. 위층 사람은 한 번씩 치는 노크에 45초 동안 두 번까지만 멈춰 선다.
 * 4. 첫 노크와 거의 같이 문을 열어 미룬 방문은 그 시간이 다시 열리면 지워진다.
 * 5. 현관을 열어 둔 채 있어도 둘째 밤 손님이 문간에 선다. 닫고 걸쇠를 걸면 걸려서 간다.
 * 6. 셋째 밤 옆집을 두드리는 사이 문을 열었다가 곧바로 닫기 시작하면 몸이 들어오지 못한다.
 * 7. 손님에게 잡히면 걸쇠가 뜯기고 그 밤에 한 번 더 온다. 다시 올 때 잠근 욕실에 있으면 못 잡는다.
 * 8. 잡힌 뒤 40초는 어둠이 쌓이지 않는다. 첫째 밤에는 3층 참 센서등 장면 전에 쌓이지 않는다.
 * 9. 녹음기는 잡히면 멈추고 목격을 남기지 않는다. 다시 끝까지 들으면 목격과 생각이 남는다.
 */
UCLASS()
class INDIEGAME_API AIGHorrorRegressionProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGHorrorRegressionProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer, AIGListenerEntity* InListener);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Next(int32 NewPhase);
	void Stand(const FVector& Feet, const FVector& LookAt);
	void NoteLines();
	bool Saw(const FText& Line) const;
	void RequestQuietAutosave();
	void SetTorch(bool bOn);
	void LockBathroom();

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGNightThreatDirector> Threat;
	TWeakObjectPtr<AIGMissingFloorNightThreeDirector> NightThree;
	TWeakObjectPtr<AIGNightLoopDirector> Loop;
	TWeakObjectPtr<AIGNightPhaseDirector> Phase;
	TWeakObjectPtr<AIGSeoRecorder> Recorder;
	TWeakObjectPtr<AIGBathroomRefuge> Refuge;
	TWeakObjectPtr<AIGSwingDoor> HomeDoor;
	TWeakObjectPtr<AIGDoorLatch> HomeLatch;

	int32 Step = 0;
	int32 Failures = 0;
	float StepSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	int32 Taps = 0;
	int32 CapturesAtStart = 0;
	bool bActed = false;
	bool bSawInside = false;
	int32 LastGuestStage = -1;
	/** 어둠 시험에 쓸 자리. 후보를 차례로 서 보고 가장 어두운 곳을 고른다. */
	int32 SpotIndex = 0;
	FVector DarkSpot = FVector::ZeroVector;
	float BestDarkness = -1.0f;
	/** 화면에 한 번이라도 떴던 말. HUD는 지나간 말을 기억하지 않는다. */
	TSet<FString> SeenLines;
};
