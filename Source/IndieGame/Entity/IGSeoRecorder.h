#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGSeoRecorder.generated.h"

class AIGMissingFloorEvidence;
class UAudioComponent;
class UIGMissingFloorNarrativeSubsystem;
class USoundBase;
class UStaticMeshComponent;

/**
 * 5층 판재 더미 밑에 1년 동안 있던 서일영 녹음기(설정집 §3.4 「남기고 간 녹음 메모」).
 *
 * 그날 새벽 서일영은 증거를 남기려고 녹음을 켜고 올라왔다. 몸싸움하다 떨어진 녹음기가
 * 판재 밑으로 미끄러져 들어가서, 다음 날 현장을 치운 목한수도 못 봤다. 손목끈
 * 끝만 판재 밖으로 나와 있다.
 *
 * 꺼내면 녹음이 재생된다. 서일영이 날짜와 시각을 말하고 계단을 오르면 카트 바퀴가
 * 구르다 멈추고, 누군가와 말이 오가다가 쇳덩이가 쏟아진다. 괜찮냐고 묻고 나서 계단을
 * 뛰어 내려간다. 다른 남자 목소리는 웅얼거림으로만 들린다. 무슨 말을 했는지는
 * 서일영이 받아친 말(「미안하다는 말 말고요」)로만 짐작할 수 있다.
 *
 * 끝까지 들으면 목격으로 남는다. 재생 중에 그 시간이 끝나면 멈추고 녹음기를 원래
 * 자리에 둔다. 소리는 유담 손에 든 녹음기 스피커에서 나고, 위층 사람은 이 소리를 못 듣는다.
 * 틱 없이 타이머로만 돈다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGSeoRecorder : public AActor
{
	GENERATED_BODY()

public:
	AIGSeoRecorder();

	/** 판재 밑 자리에 녹음기와 집는 판정을 세운다. 메시가 없으면 아무것도 세우지 않는다. */
	bool Configure();

	/**
	 * 그 시간과 저장 상태를 보고 녹음기를 두거나 치운다. 끝까지 들었으면 없고, 밤4 벽이
	 * 열린 뒤에도 없다. 낮에는 경찰과 관리인이 이 자리에 서므로 집지 않는다.
	 */
	void SetHourActive(bool bHourActive);

	bool IsPlaying() const { return bPlaying; }
	/** 집을 수 있는 상태인가. 검사와 디렉터가 본다. */
	bool IsAvailable() const;

	/** 검사용. 상호작용 없이 꺼내서 튼다. */
	void StartPlaybackForTesting();
	int32 GetCueIndexForTesting() const { return CueIndex; }
	AIGMissingFloorEvidence* GetPickup() const { return Pickup; }

	/** 녹음기가 놓인 자리(바닥 위). 검사에서 유담을 세울 때 쓴다. */
	static FVector GetRestingLocation();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleTaken(AIGMissingFloorEvidence* Evidence);
	void BeginPlayback();
	void AbortPlayback();
	void RunCue();
	void Wait(float Seconds);
	/** 녹음 속 서일영의 말. 화면에 머무는 시간을 돌려준다. */
	float PushRecordedLine(const FText& Line);
	void PushCaption(const FText& Caption, float Seconds);
	void PlayFromSpeaker(USoundBase* Sound, float Volume, float Pitch);
	bool IsDialogueLaneIdle() const;
	FVector GetSpeakerLocation() const;
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;
	void RefreshAvailability();

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RecorderMesh;

	UPROPERTY(Transient)
	TObjectPtr<AIGMissingFloorEvidence> Pickup;

	/** 녹음에서 나는 소리. 멈출 때 같이 끊는다. */
	TArray<TWeakObjectPtr<UAudioComponent>> Voices;

	FTimerHandle CueTimer;
	int32 CueIndex = 0;
	int32 StepIndex = 0;
	/** 앞 대사가 끝나기를 기다리다 그냥 넘어가는 시각. 다른 대사가 끼면 넉넉히 기다린다. */
	double LineDeadlineAt = 0.0;
	bool bPlaying = false;
	bool bHourActive = true;
};
