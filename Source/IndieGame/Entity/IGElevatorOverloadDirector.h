#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/IGElevator.h"
#include "IGElevatorOverloadDirector.generated.h"

class AIGPlayerCharacter;
class UAudioComponent;
class UIGMissingFloorNarrativeSubsystem;
class UIGSaveGame;

/**
 * 만원. 한 판에 한 번, 낮에 혼자 탄 승강기에서 정원 초과 경보가 운다.
 *
 * 첫 밤을 지난 뒤의 낮, 처음이 아닌 승강기에서 층 버튼을 누르면 닫히던 문이 다시
 * 열리고 부저가 울린다. 「만원」 등이 켜지고 칸이 무게에 눌려 가라앉는다. 거울 속
 * 유담은 등을 돌리고 서 있고, 눈을 돌릴 때마다 거울에만 있는 사람이 하나씩 는다.
 * 다섯이 된다(밤의 포획 다섯 번과 같은 수다).
 *
 * 내리면 부저가 멎고 문이 닫혀 빈 칸이 「5」로 올라갔다 돌아온다. 버티면 부저가
 * 뚝 끊기고 칸이 더 가라앉은 채 문이 저절로 닫힌다. 칸은 누른 층을 지나쳐 4층
 * 위 기계실 높이(「5」)까지 오르고, 문이 열리면 손바닥 자국 다섯이 찍힌 콘크리트
 * 벽이다. 거울에는 아무도 없다. 칸 지붕을 「둘, 쉬고, 하나」로 두드리는 소리가 난
 * 뒤 문이 닫히고, 줄이 미끄러져 45 cm 떨어졌다 잡힌다. 칸은 그제야 누른 층으로
 * 간다.
 *
 * 붙잡거나 다치게 하지 않는다. 낮은 안전하다는 약속(§1 규칙 4)은 지킨다.
 */
UCLASS(NotBlueprintable)
class INDIEGAME_API AIGElevatorOverloadDirector : public AActor
{
	GENERATED_BODY()

public:
	AIGElevatorOverloadDirector();
	virtual void Tick(float DeltaSeconds) override;

	void Configure(AIGElevator* InElevator);
	bool IsEventActive() const { return Stage != EStage::Idle && Stage != EStage::Done; }
	/** 검사용. 다음 층 버튼에서 조건을 보지 않고 시작한다. */
	void ForceArm() { bForceArmed = true; }

	static const FName BeatId;
	static const FName FirstRideBeatId;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EStage : uint8
	{
		Idle,
		Closing,
		Overload,
		LeftBehind,
		CarriedUp,
		AtFive,
		Returning,
		Done
	};

	void HandleButtonPressed(EIGElevatorButtonKind Kind, int32 Floor);
	void HandleArrived(int32 Landing);
	bool IsEligible(int32 Floor) const;
	void Begin(int32 TargetFloor);
	void EnterStage(EStage NewStage);
	void TickOverload(float DeltaSeconds);
	void TickLeftBehind();
	void TickCarriedUp();
	void TickAtFive();
	void TickReturning();
	void Finish(bool bAborted);
	void AddMirrorFigure();
	bool IsLookingAtMirror() const;
	FVector CabPoint(const FVector& CabLocal) const;
	void StartDrone();
	void StopDrone(float FadeSeconds);
	void Autosave() const;
	AIGPlayerCharacter* GetPlayer() const;
	UIGMissingFloorNarrativeSubsystem* GetNarrative() const;

	UFUNCTION()
	void HandleLoadCompleted(bool bSuccess, FString SlotName, UIGSaveGame* SaveGame);

	TWeakObjectPtr<AIGElevator> Elevator;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Drone;

	EStage Stage = EStage::Idle;
	float StageSeconds = 0.0f;
	float FigureClock = 0.0f;
	int32 FiguresShown = 0;
	int32 TargetFloor = 0;
	int32 StartLanding = 0;
	int32 Cue = 0;
	bool bForceArmed = false;
	bool bSawEmptyMirror = false;
	bool bAbsentUntilExit = false;
};
