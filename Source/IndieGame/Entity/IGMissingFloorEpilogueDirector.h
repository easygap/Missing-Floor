#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "IGMissingFloorEpilogueDirector.generated.h"

class AIGPlayerCharacter;
class UAudioComponent;
class UIGMissingFloorNarrativeSubsystem;

DECLARE_MULTICAST_DELEGATE(FIGEpilogueCompletedSignature);

/**
 * §9 엔딩 A·B의 에필로그.
 *
 * 밤4의 선택은 지금까지 속마음 한 줄로 끝나 있었다. 문서가 약속한 것은
 * 그 뒤였다 — 소리로만 지나가는 몽타주, 도하의 공방, 가을의 달빛빌라,
 * 비어 있는 서비스 베이, 그리고 마지막 카드 한 장.
 *
 * 「다섯 번째 새벽」 막간과 같은 방식으로 짓는다. 화면은 카메라 페이드가
 * 검게 만들고, HUD는 장면 하나씩 그리고, 이 액터는 시각표만 들고 있다.
 * 로딩 화면이 아니라는 것이 요점이라 이동만 잠그고 시점은 남긴다(§23).
 *
 * 뉴스 자막은 §22.3의 선택적 목격을 읽어 문장이 구체화된다. 못 본 회차도
 * 같은 사실을 같은 순서로 듣는다 — 달라지는 것은 이름과 근거의 선명도뿐,
 * 무엇을 놓쳤는지는 끝까지 말하지 않는다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGMissingFloorEpilogueDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGMissingFloorEpilogueDirector();
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 선택된 엔딩의 시각표를 시작한다. Ending.A와 Ending.B만 받는다 —
	 * C는 매물 화면과 재도전을 스스로 소유하므로 여기로 오지 않는다.
	 */
	bool StartEpilogue(AIGPlayerCharacter* InPlayer, FName EndingId);

	bool IsActive() const { return bActive; }
	FName GetEndingId() const { return ActiveEndingId; }

	/**
	 * §34.2와 같은 규칙의 재관람 전용 우회. 초회차는 87초를 다 본다 — 이
	 * 장면이 게임의 대가이고, 건너뛸 수 있게 만드는 순간 대가가 아니게 된다.
	 *
	 * 두 번째부터 여는 이유는 §22.4다. 선택 직전 자동 저장으로 반대쪽
	 * 엔딩을 보러 오는 사람에게, 이미 본 몽타주를 다시 앉혀 두면 그 저장
	 * 지점이 하는 일이 없다.
	 */
	bool BeginReplaySkipInput();
	bool EndReplaySkipInput();
	bool IsReplaySkipAvailable() const { return bReplaySkipAvailable; }

	/** 지금까지 재생된 장면 수. 계약 스크립트와 프로브가 읽는다. */
	int32 GetPlayedSceneCount() const { return PlayedSceneCount; }

	/**
	 * 두 엔딩의 시각표가 단조 증가하고, 마지막 큐가 끝 시각보다 앞서는지
	 * 본다. 실화면 없이 도는 출시 영수증이다.
	 */
	static bool ValidateTimelines();

	/** 160초를 기다리지 않고 전 구간을 한 번에 통과시킨다(CI 전용). */
	bool CompleteImmediatelyForProbe();

	FIGEpilogueCompletedSignature OnCompleted;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ScheduleNextCue();
	void HandleNextCue();
	void FireCue(int32 CueIndex);
	void FinishEpilogue();

	/** 몽타주 네 소리. 두 엔딩이 같은 발견을 공유한다(§9 공통 사실). */
	void PlayMontageCue(int32 MontageIndex);

	/** §22.3을 읽어 뉴스 자막 줄을 만든다. 못 본 것은 말하지 않는다. */
	TArray<FText> BuildNewsLines() const;
	/** 공방 작업지. 적금 칸을 본 회차에만 한 줄이 더 붙는다. */
	TArray<FText> BuildWorkshopLines() const;
	TArray<FText> BuildAutumnLines() const;
	TArray<FText> BuildServiceBayLines() const;

	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;
	void StopBeds();
	/** 우회가 끝나면 마지막 카드로 건너뛴다. 카드는 원래 길이대로 머문다. */
	void SkipToFinalCard();
	void UpdateSkipHud() const;
	float GetReplaySkipDurationSeconds() const;
	bool UsesToggleSkipInput() const;
	bool HasExperiencedEpilogueProfile() const;
	void PersistEpilogueExperience() const;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ScoreBed;

	TWeakObjectPtr<AIGPlayerCharacter> Player;
	FTimerHandle CueTimerHandle;
	/** 처음 보는 사람에게도 건너뛰기를 여는 타이머(§6). */
	FTimerHandle FirstViewSkipTimer;
	FName ActiveEndingId;
	double StartWorldSeconds = 0.0;
	uint32 FiredCueMask = 0;
	int32 NextCueIndex = 1;
	int32 PlayedSceneCount = 0;
	float ReplaySkipProgress = 0.0f;
	bool bActive = false;
	bool bEndingA = false;
	bool bReplaySkipAvailable = false;
	bool bReplaySkipInputActive = false;
	bool bReplaySkipRewinding = false;
	bool bReplayForcedForSession = false;
};
