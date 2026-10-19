#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sequence/IGInputRoute.h"
#include "IGStoreCounterProbe.generated.h"

class AIGListenerGreyboxDirector;
class AIGMissingFloorEvidence;
class AIGPlayerCharacter;
class AIGReadableNote;
class AIGStoreClerk;

/**
 * -IGStoreCounterProbe 전용. 편의점 계산대 장면을 실제 키·마우스 입력으로 겪는다.
 *
 * 준비만 상태로 한다(입주 저녁, 나린과 인사함, 뒤편 택배 안내문을 읽음, 가게 앞 골목에 선다). 그다음은
 * 걸어 들어가 벨을 누르고, 택배 얘기를 듣고, 다시 벨을 눌러 누가 찾아갔는지 묻고, 나린이 단말기를
 * 누르고 영수증을 뽑아 놓는 것을 지켜본 뒤 읽고, 가게를 나갔다가 다시 들어와 벨을 누른다.
 *
 * 변형(-IGStoreCounterProbe=…): stay(기본), leave(나린이 단말기를 누르기 시작하면 가게를 나간다),
 * spam(나린이 일하는 동안 벨을 거듭 누른다).
 *
 * -IGStoreCounterRecord를 주면 마스터 믹스를 녹음하고(-DeterministicAudio와 같이 쓴다) 몇 장면을
 * 찍는다. 대사·소리 자막·나린의 소리·영수증의 시각을 STORE_AV 줄로 남겨 녹음과 맞춰 본다.
 */
UCLASS()
class INDIEGAME_API AIGStoreCounterProbe : public AActor
{
	GENERATED_BODY()

public:
	AIGStoreCounterProbe();
	virtual void Tick(float DeltaSeconds) override;
	void Configure(AIGPlayerCharacter* InPlayer, AIGListenerGreyboxDirector* InDirector);

private:
	void Check(bool bCondition, const FString& Name);
	void Finish();
	void Event(const FString& What);
	void Shoot(const FString& Name);
	void WatchState();
	bool IsReceiptShown() const;
	bool IsStoreTalkBusy() const;
	void BuildRoute();

	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TWeakObjectPtr<AIGListenerGreyboxDirector> Director;
	TWeakObjectPtr<AIGStoreClerk> Clerk;
	TWeakObjectPtr<class AIGNightPhaseDirector> NightPhase;
	TWeakObjectPtr<AIGMissingFloorEvidence> Bell;
	TWeakObjectPtr<AIGReadableNote> Receipt;
	FIGInputRoute Route;
	FString Variant;
	bool bRecord = false;
	bool bRecording = false;
	int32 Phase = 0;
	int32 Failures = 0;
	float TotalSeconds = 0.0f;
	float PhaseSeconds = 0.0f;
	double FirstBellAt = -1.0;
	/** 지켜보는 상태. 바뀔 때만 STORE_AV 줄을 남긴다. */
	FString LastDialogue;
	FString LastCaption;
	bool bLastReceipt = false;
	bool bLastClerkBusy = false;
	double LastPositionLogAt = -10.0;
	/** 유담이 나린에게 물은 줄 수. 벨을 거듭 눌러도 질문이 늘지 않아야 한다. */
	int32 YudamQuestions = 0;
	int32 YudamQuestionsAtSpam = -1;
	double ClerkFirstBusyAt = -1.0;
	double ReceiptShownAt = -1.0;
	double PrintStartedAt = -1.0;
	bool bShotTap = false;
	bool bShotPrint = false;
	bool bShotPrinting = false;
	bool bShotPlaced = false;
};
