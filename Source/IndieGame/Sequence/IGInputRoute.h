#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class AActor;
class AIGPlayerCharacter;

/**
 * 실제 키·마우스 입력으로 정해 둔 길을 따라가는 검사 도우미. 플레이어 컨트롤러에 사람이 누르는 것과
 * 같은 입력을 넣는다. 순간 이동과 상호작용 직접 호출은 하지 않는다. 마우스 한 칸이 몇 도인지는
 * 처음에 한 번 재서 쓴다(반전 설정까지 같이 잡힌다). 남은 짐 검사(IGLeftoverProbe)의 입력 길과 같은
 * 방식이다.
 */
class FIGInputRoute
{
public:
	enum class EStep : uint8
	{
		/** 그 자리까지 걷는다(Value: 허용 거리). */
		Walk,
		/** 그 점을 겨눈다. */
		Aim,
		/** 키를 Value초 누른다. Expect가 있으면 누르기 전 조준이 그 액터를 잡고 있어야 한다. */
		Press,
		/** Value초 기다린다. */
		Wait,
		/** Done이 참이 될 때까지 기다린다(Value: 제한 시간). */
		Until
	};

	struct FStep
	{
		EStep Kind = EStep::Wait;
		FVector Point = FVector::ZeroVector;
		float Value = 0.0f;
		FString Name;
		TWeakObjectPtr<AActor> Expect;
		TFunction<bool()> Done;
		FKey Key = EKeys::E;
	};

	/** 길을 비우고 다시 시작한다. Check는 단계마다 통과·실패를 받는다. */
	void Reset(AIGPlayerCharacter* InPlayer, TFunction<void(bool, const FString&)> InCheck);
	void Walk(const FVector& Point, float Tolerance, const FString& Name);
	void Aim(const FVector& Point, const FString& Name);
	void Press(float Seconds, const FString& Name, AActor* Expect = nullptr, const FKey& Key = EKeys::E);
	void Wait(float Seconds, const FString& Name);
	void Until(TFunction<bool()> Done, float TimeoutSeconds, const FString& Name);
	/** 한 틱 진행한다. 다 끝났으면 true. */
	bool Tick(float DeltaSeconds);
	bool IsDone() const { return !Steps.IsValidIndex(Index); }
	/** 지금 하는 단계의 이름(다 끝났으면 빈 문자열). */
	FString GetCurrentName() const { return Steps.IsValidIndex(Index) ? Steps[Index].Name : FString(); }
	/** 걷던 키를 놓는다(검사를 끝내거나 길을 바꿀 때). */
	void ReleaseKeys();
	AActor* GetFocusedActor() const;

private:
	void SendKey(const FKey& Key, bool bPressed);
	void SendMouse(float X, float Y);
	bool Calibrate();
	bool StepWalk(const FStep& Step, float DeltaSeconds);
	bool StepAim(const FStep& Step);
	FVector CameraLocation() const;
	FRotator CameraRotation() const;

	TWeakObjectPtr<AIGPlayerCharacter> Player;
	TFunction<void(bool, const FString&)> Check;
	TArray<FStep> Steps;
	int32 Index = 0;
	float StepSeconds = 0.0f;
	bool bKeyDown = false;
	bool bForwardDown = false;
	float MouseYawScale = 0.0f;
	float MousePitchScale = 0.0f;
	int32 CalibrateStage = 0;
	float CalibrateYaw = 0.0f;
	float CalibratePitch = 0.0f;
	float StallSeconds = 0.0f;
	bool bBlockLogged = false;
};
