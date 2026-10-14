#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGLeftoverProbe.generated.h"

class AIGAccessHatch;
class AIGLeftoverTarget;
class AIGManagerPatrol;
class AIGMissingFloorEvidence;
class AIGMissingFloorLeftoverDirector;
class AIGMissingFloorNightFourDirector;
class AIGNightPhaseDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class UIGMissingFloorNarrativeSubsystem;

/**
 * -IGLeftoverProbe 전용. 「302호에 남은 짐」을 실제 감독과 실제 괴이로 한 실행에서 겪는다.
 *
 * 낮1 짐 장면(정우가 3층 방화문 앞에 서고, 문을 잡아 주면 302호까지 간다), 밤2 장부를 맞춘 뒤
 * 안쪽 방에서 목한수가 깨어 손수레를 가지러 나가는 동안 안쪽 방을 뒤지고(덮개 조각 둘, 장갑
 * 주머니, 두 점검구와 덧댄 것), 손수레가 돌아와 그가 눕고 나서야 귀환 추격이 걸리는지, 낮2
 * 정우와의 대화, 밤3 302호(쐐기를 잘못 끼우면 미끄러지고, 장롱을 밀어 큰 점검구를 열면 덧댄
 * 것이 옮겨 가 판이 풀리고, 정우가 판을 받치는 동안 상자를 빼고 같이 나간다), 덮개 조립(크기가
 * 안 맞는 자리, 앞은 맞는데 뒤가 안 맞는 자리, 맞는 자리, 뒤집어 방수막 들추기), 5층 원래 자리에
 * 대 보기, 밤4 벽 다섯 단계(빗나간 망치는 단계를 올리지 않는다, 이음매 → 배관 점검구 → 철골
 * 틀로 무릎을 건다 → 바깥층과 정전 → 밀린 틀을 다시 세우고 안쪽 나사 → 부름과 대답 뒤 밀려남 →
 * 마지막 철물과 개방), 저장 왕복과 확장 전 저장의 처리까지다.
 *
 * -IGLeftoverProbe=dawn이면 밤3에 정우를 꺼내지 않고 새벽을 맞아 혼자 빠져나가는 길을 본다.
 * -IGLeftoverProbe=wall이면 밤4 벽의 단계마다 저장 상태만 바꿔 되살린 모습을 보고 찍는다.
 * -IGLeftoverShots를 같이 주면 렌더가 있는 실행에서 걸음마다 화면을 찍는다(Saved/LeftoverReview).
 */
UCLASS()
class INDIEGAME_API AIGLeftoverProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGLeftoverProbe();
	virtual void Tick(float DeltaSeconds) override;
	void Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Stand(const FVector& Feet, const FVector& LookAt);
	void Shoot(const TCHAR* Name);
	/** 정우가 판을 받치는 자리: 판·상자·손발 뼈를 판 좌표로 남기고 둘레 벽까지 잰다. */
	void LogBraceGeometry();
	/**
	 * 판과 상자, 정우의 몸이 서로 뚫지 않는지 본다. bBracing이면 두 손바닥이 판 방 쪽 면에 닿고 몸과
	 * 발은 그 앞 바닥에 있어야 한다. 판은 상자 위 모서리에 얹혀 있고 판과 상자는 동벽을 넘지 않는다.
	 */
	void CheckStorageContact(bool bBracing, const TCHAR* Prefix);
	/** 5층 벽 칸 둘레의 벽·천장까지 거리와 편 몸의 패드 자리를 남긴다(다리를 놓을 자리를 고를 때). */
	void LogAnnexGeometry();
	void Next(int32 NewPhase);
	bool Use(AActor* Target, bool bTap = false);
	AIGLeftoverTarget* Target(const TCHAR* Key) const;
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;
	void Silence();
	/** 지난 밤이 아직 열려 있으면 먼저 새벽을 맞게 하고 false다. 다음 틱에 다시 부른다. */
	bool BeginNight(int32 Night);
	/** 열린 밤이 있으면 끝낸다. 닫혀 있으면 true. */
	bool EndNight();

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGMissingFloorLeftoverDirector> Leftover;
	TWeakObjectPtr<AIGManagerPatrol> Manager;
	TWeakObjectPtr<AIGNightPhaseDirector> NightPhase;
	TWeakObjectPtr<AIGMissingFloorNightFourDirector> NightFour;
	/** 밤4 벽 표적 하나를 손에 쥔 망치로 친다(조준 정보 없이). */
	bool Strike(AIGMissingFloorEvidence* Target);
	int32 StrikesBefore = 0;
	/** 밤4 덧댄 것이 옮겨 가는 길에 일부러 서 있다가 눌렸다. */
	bool bLiningGripped = false;
	/** 벽 단계 검사에서 편 몸 전체 사진을 한 번 찍었다. */
	bool bWallFullShotTaken = false;
	/** -IGLeftoverFrames: 덧댄 것이 움직이는 구간을 30fps로 이어 찍는다. */
	bool bFrames = false;
	FString FrameTag;
	int32 FrameIndex = 0;
	int32 FrameTick = 0;
	void BeginFrames(const TCHAR* Tag);
	void EndFrames();
	void TickFrames();
	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	int32 ShotIndex = 0;
	bool bShots = false;
	/** 소개 문서용으로 HUD 없이 찍는다(-IGLeftoverCleanShots). */
	bool bCleanShots = false;
	/** 힌트나 읽기 화면을 띄운 뒤 글이 다 나타나면 찍는다. 음수면 찍을 것이 없다. */
	float HintShotCountdown = -1.0f;
	FString HintShotName;
	/** 찍은 뒤 닫을 읽기 화면. */
	TWeakObjectPtr<class AIGReadableNote> PendingNote;
	/** 쪽지·대장을 펼쳐 그 언어로 읽히는지 찍는다(찍는 실행만). */
	void ReadAndShoot(class AIGReadableNote* Note, const TCHAR* ShotName);
	/** H를 누르면 지금 이 힌트가 나오는가. 찍는 실행이면 가장 구체적인 단계를 띄우고 1.2초 뒤에 찍는다. */
	bool HintIs(const TCHAR* GoalId, const TCHAR* ShotName);
	bool bDawnVariant = false;
	/** night2: 낮1을 건너뛰고 밤2 안쪽 방만 본다. */
	bool bNightTwoOnly = false;
	/** wall: 밤4 벽 단계마다 저장 상태로 되살려 보고 찍는다(불러오기 길). */
	bool bWallOnly = false;
	int32 WallShotStage = 0;
	bool bErrandFinished = false;
	bool bErrandCaught = false;
	bool bErrandReturning = false;
	int32 Step = 0;
	/** 한 단계 안에서 찍은 뒤 다음 행동까지 기다린 시간(PhaseSeconds를 쓰는 조건과 따로 센다). */
	float StepSeconds = 0.0f;
};
