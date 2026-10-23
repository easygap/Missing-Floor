#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGNightFourHammerProbe.generated.h"

class AIGDoorLatch;
class AIGLiningEntity;
class AIGListenerEntity;
class AIGMissingFloorEvidence;
class AIGMissingFloorNightFourDirector;
class AIGNightLoopDirector;
class AIGNightThreatDirector;
class AIGPlayerCharacter;
class AIGPrologueWorldScene;
class AIGSwingDoor;

/**
 * -IGNightFourHammerProbe 전용. 밤4 망치질 사이를 겪는다(EXPANSION_PLAN §2.3 넷째 밤).
 *
 * 밤4로 놓고 진실 셋(벽 속 사람, 살아 있었다, 대답을 기다린다)과 물소리 가면을 세운 뒤 5층
 * 벽을 친다. 덧댄 것이 벽을 따라 이음매와 점검구로 옮겨 가는 동안 망치 든 사람은 밀리지도
 * 눌리지도 않아야 한다. 셋째 타 뒤 정전에 건물 안 등이 다 죽어야 한다. 넷째 타 뒤에는 손이
 * 저려 잠깐 못 치고, 그 사이 5층 철문 밖에서 「유담아. 오빠야. 문 좀 열어 줘.」가 오고, 벽 안에서
 * 둘, 쉬고, 하나가 오면 다시 칠 수 있어야 한다. 빗장을 걸어 두었으므로 손님은 열쇠를 꽂다
 * 빗장에 걸려 간다. 그 뒤 정전 속에 어둑시니가 빚은 몸(SK_Eoduksini)으로 서야 하고, 다섯째 타에
 * 벽이 열리면 어둑시니와 손님이 다 그쳐야 한다.
 *
 * -IGNightFourHammerOpen을 주면 빗장을 걸지 않는다. 손님이 열고 들어와 잡으면 결말 C가 아니라
 * 그 철문 안쪽에서 깨야 하고(공격성 3 아래), 빗장은 휘어 있고, 정전이라 403호 전등도 죽어 있어야
 * 한다.
 *
 * -IGNightFourHammerShots를 같이 주면 렌더가 있는 실행에서 몇 장면을 찍는다.
 *
 * -IGNightFourWallDiag는 망치 자리 화면이 왜 알아보기 어려운지 원인별로 갈라 찍는다. 덧댄 것이 벽을
 * 따라오는 몇 순간에 세계를 멈추고 같은 카메라에서 게임 화면, HUD 끔, 덧댄 것 숨김, 언릿, 월드 노멀,
 * 그림자 끔, 안티앨리어싱 끔, 와이어프레임을 차례로 찍는다. 그다음 실제 이동 입력으로 물러섰다
 * 다가가고 옆으로 걷고 고개를 돌리며 프레임마다 찍는다. 카메라에서 덧댄 것의 몸·무릎·발까지 가장
 * 가까운 거리와 실행 중 그래픽 설정을 기록에 남긴다. 이 실행은 진단용이라 망치 자리 밀림은 보지 않는다.
 */
UCLASS()
class INDIEGAME_API AIGNightFourHammerProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGNightFourHammerProbe();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(AIGPrologueWorldScene* InScene, AIGPlayerCharacter* InPlayer, AIGListenerEntity* InListener);

private:
	void Check(bool bCondition, const TCHAR* Name);
	void Finish();
	void Next(int32 NewPhase);
	void Stand(const FVector& Feet, const FVector& LookAt);
	void Shoot(const TCHAR* Name);
	/** 진단 촬영이 진행 중이면 true. 그동안 검사 단계는 멈춘다. */
	bool TickWallDiagnostics(float DeltaSeconds);
	void BeginWallDiagnosticMoment(const TCHAR* Name);
	/** 진단 화면 하나를 고른다(0 게임 화면 … 7 와이어프레임). 끝나면 원래대로 돌린다. */
	void ApplyWallDiagnosticView(int32 View, bool bOn);
	/** 카메라에서 덧댄 것의 몸 가운데·무릎·발까지 가장 가까운 거리(cm)와 그 부위. */
	float MeasureLiningDistance(FString& OutPart) const;
	bool SaidLine(const FText& Line) const;
	/** 벽을 한 번 친다. 칠 수 없으면 거짓이다. */
	/** 다음 단계까지 손댄다(살펴보기·틀 세우기처럼 망치가 아닌 것도). 단계가 올랐으면 true. */
	bool Strike();
	bool IsCapturedSince(double Seconds) const;

	TWeakObjectPtr<AIGPrologueWorldScene> Scene;
	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerEntity> Listener;
	TWeakObjectPtr<AIGMissingFloorNightFourDirector> NightFour;
	TWeakObjectPtr<AIGNightThreatDirector> Threats;
	TWeakObjectPtr<AIGNightLoopDirector> NightLoop;
	TWeakObjectPtr<AIGSwingDoor> AnnexDoor;
	TWeakObjectPtr<AIGDoorLatch> AnnexBolt;

	int32 Phase = 0;
	int32 Failures = 0;
	float PhaseSeconds = 0.0f;
	float TotalSeconds = 0.0f;
	bool bShots = false;
	bool bDiag = false;
	/** 진단: 찍을 순간 이름, 그 순간의 화면 번호, 다음 동작까지 남은 프레임, 기다리는 캡처 파일. */
	FString DiagMoment;
	int32 DiagView = -1;
	int32 DiagWaitFrames = 0;
	FString DiagPendingShot;
	int32 DiagMomentsTaken = 0;
	/** 진단: 움직이며 찍는 프레임 번호(음수면 아직 안 함). */
	int32 DiagMotionFrame = -1;
	float DiagNextMomentSeconds = 0.0f;
	TWeakObjectPtr<AIGLiningEntity> Lining;
	bool bOpenDoor = false;
	bool bActed = false;
	int32 Struck = 0;
	float LastActionSeconds = -10.0f;
	float ThirdStageSeconds = -1.0f;
	/** 첫째~셋째 단계 동안 망치 든 자리에서 가장 멀리 밀려난 거리(cm). */
	float MaxDrift = 0.0f;
	bool bSawHeld = false;
	bool bSawCall = false;
	bool bSawCaughtOnBolt = false;
	double CaptureBaseline = 0.0;
};
