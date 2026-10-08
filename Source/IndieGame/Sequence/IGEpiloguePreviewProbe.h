#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGEpiloguePreviewProbe.generated.h"

class AIGMissingFloorEpilogueDirector;
class AIGPlayerCharacter;

/**
 * -IGEpiloguePreview=A|B 전용. 결말 에필로그를 밤4를 거치지 않고 바로 실제 시간으로 흘리며
 * 장면이 바뀔 때마다 한 장씩 찍는다. 정지 화면이 제 비례로 서는지, 장면 글이 번역에서 넘치지
 * 않는지 볼 때 쓴다(-IGCulture와 같이). 그림은 -UserDir 아래 Saved/EpiloguePreview에 남는다.
 */
UCLASS()
class INDIEGAME_API AIGEpiloguePreviewProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGEpiloguePreviewProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(AIGPlayerCharacter* InPlayer, FName InEndingId);

private:
	/** 에필로그가 끝난 프레임. 그다음 프레임에는 타이틀로 넘어가며 이 액터도 사라진다. */
	void HandleCompleted();
	void Finish(bool bPassed);
	void Shoot(int32 SceneNumber);

	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGMissingFloorEpilogueDirector> Epilogue;
	FName EndingId;
	float TotalSeconds = 0.0f;
	float SceneSeconds = 0.0f;
	int32 SeenScenes = 0;
	bool bStarted = false;
	bool bSceneShot = true;
	bool bFinished = false;
};
