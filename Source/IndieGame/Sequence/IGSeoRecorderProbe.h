#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGSeoRecorderProbe.generated.h"

class AIGListenerGreyboxDirector;
class AIGMissingFloorEpilogueDirector;
class AIGMissingFloorNightFourDirector;
class AIGMissingFloorNightThreeDirector;
class AIGPlayerCharacter;
class AIGSeoRecorder;

/**
 * -IGSeoRecorderProbe 전용. 서일영의 녹음기와 골목의 약봉투를 겪는다(설정집 §3.4).
 *
 * 그 시간이 켜진 새 게임에서 시작한다. 녹음기는 5층 판재 밑에 있고 집을 수 있어야 한다.
 * 한 번 틀다가 그 시간이 바뀌면 멈추고 녹음기가 제자리로 돌아와야 한다. 다시 틀어 끝까지
 * 들으면 다섯 줄과 독백이 나오고, 목격이 남고, 녹음기는 더는 없어야 한다.
 *
 * 약봉투는 골목 건너 남자가 한 번 나타났다 가기 전에는 없다. 그 남자가 떨어뜨리면 생기고, 주우면
 * 녹음기를 들은 회차의 독백이 나와야 한다. 마지막으로 밤4 대치와 보도가 녹음기 줄을 고르고
 * 약봉투 줄은 빼는지 본다.
 *
 * -IGSeoRecorderShots를 같이 주면 렌더가 있는 실행에서 판재 밑 녹음기와 골목 약봉투를 찍는다.
 */
UCLASS()
class INDIEGAME_API AIGSeoRecorderProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGSeoRecorderProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(AIGPlayerCharacter* InPlayer, AIGListenerGreyboxDirector* InDirector);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Next(int32 NewPhase);
	void NoteLines();
	bool SaidLine(const FText& Line) const;
	void Stand(const FVector& Feet, const FVector& LookAt);
	void Shoot(const TCHAR* Name);

	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerGreyboxDirector> Director;
	TWeakObjectPtr<AIGMissingFloorNightThreeDirector> NightThree;
	TWeakObjectPtr<AIGMissingFloorNightFourDirector> NightFour;
	TWeakObjectPtr<AIGMissingFloorEpilogueDirector> Epilogue;
	TWeakObjectPtr<AIGSeoRecorder> Recorder;

	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	bool bShots = false;
	/** 화면에 한 번이라도 떴던 말. HUD는 지나간 말을 기억하지 않는다. */
	TSet<FString> SeenLines;
};
