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
 * -IGLeftoverProbe=input이면 밤3 302호를 순간 이동 없이 실제 입력(W, 마우스 축, E 누르기·길게 누르기)으로
 * 지난다. 복도에서 걸어 들어가 쐐기·장롱·점검구·상자 당기기·덮개 맞추기를 하고 걸어 나온다. 시작 상태(밤3,
 * 4층 관리인·위층 사람 재우기)만 검사가 만든다. 걸린 시간을 구간별로 남긴다.
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
	/** 그 사건 이름의 대사가 지금 떠 있거나 밀려 있는가. */
	bool HasLinesIn(FName Context) const;
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
	/** 정우의 뼈가 302호 북벽·동벽에서 몸 두께만큼 떨어져 있는지 본다. */
	void CheckJeongwooInsideRoom(const TCHAR* Prefix);
	/** 바닥에 엎어 둔 폰 손전등의 가운데를 정우 몸이 덮지 않는지 본다. */
	void CheckPhoneTorchClear(const TCHAR* Prefix);
	void Next(int32 NewPhase);
	/** 지금 쓸 수 있는가(살아 있는 표적이고 상호작용을 받는가). */
	bool CanUse(AActor* Actor) const;
	bool Use(AActor* Target, bool bTap = false);
	AIGLeftoverTarget* Target(const TCHAR* Key) const;
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;
	void Silence();
	/** 지난 밤이 아직 열려 있으면 먼저 새벽을 맞게 하고 false다. 다음 틱에 다시 부른다. */
	bool BeginNight(int32 Night);
	/** 열린 밤이 있으면 끝낸다. 닫혀 있으면 true. */
	bool EndNight();
	/**
	 * -IGLeftoverProbe=hints. 셋째 밤 302호 구출에서 정우 소개 여부, 302호 곁과 멀리, 쐐기·장롱·판 낙하 순서,
	 * 저장하고 이어 한 밤마다 H 힌트 세 단계를 그대로 남긴다(-IGHintScenario=named|unnamed|talk|fallen|remote|save).
	 * 저장 시나리오 뒤에 -IGHintResume으로 다시 켜면 자동 저장을 불러온 밤에서 같은 것을 본다.
	 */
	void TickHints(class AIGMissingFloorLeftoverDirector* Director, UIGMissingFloorNarrativeSubsystem* Narrative);
	void DumpHint(const TCHAR* Where);
	/** 302호 안 소품의 받침·관통을 실제 메시로 잰다(에디터 실행에서만). 판·장롱·쐐기를 움직인 상태마다 부른다. */
	void AuditStorage(const TCHAR* Label);
	/** -IGLeftoverShots면 302호를 네 자리에서 손전등으로 비춰 찍는다. 다 찍었으면 true. */
	bool StorageViews(const TCHAR* Label);
	int32 StorageViewIndex = 0;
	int32 StorageViewStep = 0;
	float StorageViewClock = 0.0f;
	bool bHintsVariant = false;
	FString HintScenario;

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
	/** dawnbrace: 정우를 꺼내 판을 받치게 한 채 새벽을 맞는다. 낮3 403호 문 앞에 상자가 와 있어야 한다. */
	bool bDawnBraceVariant = false;
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
	/** 처음 당기기 전 오빠 상자 자리. 한 뼘 끌려 나오다 걸리는지 본다. */
	FVector BoxBeforePull = FVector::ZeroVector;
	/** 판을 밀어 낸 정우가 쪼그려 숨을 고르는 모습을 한 번 찍었다. */
	bool bBreathShot = false;

	// -- 실제 입력 길(input) -------------------------------------------------------------
	enum class EInputStep : uint8 { Walk, Aim, Press, Wait, Until };
	struct FInputStep
	{
		EInputStep Kind = EInputStep::Wait;
		FVector Point = FVector::ZeroVector;
		/** Walk 허용 거리, Press 누르는 시간, Wait 시간, Until 제한 시간. */
		float Value = 0.0f;
		const TCHAR* Name = TEXT("");
		/** Press 전에 조준이 잡혀 있어야 하는 표적(비우면 보지 않는다). */
		TWeakObjectPtr<AActor> Expect;
		TFunction<bool()> Done;
		/** 구간 이름이 있으면 여기서 시간 구간이 바뀐다. */
		const TCHAR* Segment = nullptr;
		/** Press가 누르는 키. 박자 두드리기는 Q다. */
		FKey Key = EKeys::E;
	};
	bool bInputVariant = false;
	/**
	 * night3av: 302호 입력 길을 마치고 그대로 계단과 옥상으로 5층에 올라가 밸브·벽 듣기·수첩·박자 두드리기로
	 * 벽의 대답을 듣고, 복도에서 기다리는 그의 옆을 지나 403호로 돌아온다. 준비로 주는 것(열쇠 꾸러미,
	 * 입주 때 들은 음성 메시지, 관리인과 손님·어둑시니 재우기)은 N3AV setup 줄에 적는다.
	 */
	bool bNight3Av = false;
	/** -IGLeftoverRecord: 마스터 믹스를 녹음한다(-DeterministicAudio와 같이 쓴다). */
	bool bRecordAV = false;
	/** -Clean 촬영에서 넷째 밤 편 몸의 넓은 컷을 이미 찍었다. */
	bool bSpreadWideShot = false;
	bool bRecordingAV = false;
	FString LastAvDialogue;
	FString LastAvCaption;
	/** 포획 암전이 도는 중이었는가. 바뀔 때 capture_start·capture_end를 남긴다(검사가 잡힌 것을 놓치지 않게). */
	bool bLastAvCapture = false;
	int32 AvCaptures = 0;
	double LastAvPositionAt = -10.0;
	void WatchAV();
	void AvEvent(const FString& What);
	void BuildNightThreeContinuation();
	/** lines: 새 문장을 한 줄씩 화면에 띄워 찍는다(번역 길이·글자·줄바꿈을 언어마다 본다). -Shots와 같이 쓴다. */
	bool bLinesVariant = false;
	int32 LineIndex = 0;
	TArray<FInputStep> InputSteps;
	int32 InputIndex = 0;
	float InputStepSeconds = 0.0f;
	bool bInputKeyDown = false;
	bool bForwardDown = false;
	float MouseYawScale = 0.0f;
	float MousePitchScale = 0.0f;
	int32 CalibrateStage = 0;
	float CalibrateYaw = 0.0f;
	float CalibratePitch = 0.0f;
	float StallSeconds = 0.0f;
	/** 막힌 걸음마다 무엇에 막혔는지 한 번만 적는다. */
	bool bBlockLogged = false;
	FString InputSegment;
	float InputSegmentStart = 0.0f;
	void BuildStorageInputRoute();
	/** 이번 틱의 입력 한 단계를 진행한다. 다 끝났으면 true. */
	bool TickInputRoute(float DeltaSeconds);
	bool CalibrateMouse();
	bool InputWalk(const FVector& Goal, float Tolerance, float DeltaSeconds);
	bool InputAim(const FVector& Point, float DeltaSeconds);
	void SendKey(const FKey& Key, bool bPressed);
	void SendMouse(float X, float Y);
	AActor* FocusedActor() const;
	FVector CameraLocation() const;
	FRotator CameraRotation() const;
};
