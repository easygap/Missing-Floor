#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGLiningEntity.generated.h"

class AIGAccessHatch;
class AIGLiningEntity;
class UAudioComponent;
class UBoxComponent;
class UCapsuleComponent;
class UPoseableMeshComponent;

/**
 * 덧댄 것이 노리는 틈 하나. 같은 벽 속 공간으로 이어진 시공 틈만 넣는다. 세대문·창문은
 * 틈이 아니다. 덮개(Hatch)가 있으면 덮개가 열려 있을 때만 열린 틈이고, 덮개 없이 판을 떼어
 * 낸 자리는 bAlwaysOpen이다. 몸이 눌러 막으면 bSealed가 되고 다시 뜯기 전에는 고르지 않는다.
 */
struct FIGLiningGap
{
	FName Id;
	/** 틈 가운데. 벽 면 위다. */
	FVector Center = FVector::ZeroVector;
	/** 벽에서 방 쪽으로 나온 법선. */
	FVector Normal = FVector::ForwardVector;
	/** 몸을 붙일 때 몸 앞쪽(+X)이 향할 벽 위의 방향. 보통 위. */
	FVector Up = FVector::UpVector;
	/** 가로·세로(cm). 넓이로 고른다. */
	FVector2D Size = FVector2D(45.0f, 45.0f);
	/**
	 * 몸 가운데를 틈 가운데에서 벽 면을 따라 비키는 거리. 틈 바로 옆에 판 같은 소품이 서 있으면 몸과 다리가
	 * 소품을 뚫지 않게 반대쪽으로 비켜 누른다. 몸이 틈을 다 덮는 만큼만 비킨다.
	 */
	FVector BodyOffset = FVector::ZeroVector;
	/**
	 * 틈이 바닥에 붙어 있어 아래 다리 둘이 짚을 벽이 없다. 아래 다리는 몸 밑에 접고 위 다리 둘로만 짚는다
	 * (펴면 바닥 밑이나 옆 소품을 짚고, 다리가 길어 무릎이 방 가운데로 나온다).
	 */
	bool bTuckLowerLegs = false;
	/** 몸과 다리가 자리를 잡는 기준점(틈 가운데 + 비키는 거리). */
	FVector BodyAnchor() const { return Center + BodyOffset; }
	TWeakObjectPtr<AIGAccessHatch> Hatch;
	bool bAlwaysOpen = false;
	bool bSealed = false;

	float Area() const { return Size.X * Size.Y; }
};

UENUM()
enum class EIGLiningState : uint8
{
	/** 없다(다른 밤, 다른 공간). */
	Hidden,
	/** 벽이나 바닥에 접혀 붙어 있다. 보강재 더미처럼 보인다. */
	Folded,
	/** 처음 움직인다. 패드 하나가 벽에서 떨어지고 다리가 드러난다. */
	Peeling,
	/** 두 다리로 짚고 몸을 당기고, 남은 두 다리를 옮긴다. */
	Stepping,
	/** 틈 둘레를 네 다리로 짚는다. */
	Bracing,
	/** 몸으로 틈을 눌러 막는다. */
	Pressing,
	/** 막은 틈 위에 그대로 있다. 다른 열린 틈을 본다. */
	Holding,
	/** 5층에서 천장과 벽을 짚고 몸을 다 폈다. */
	Spread,
	/** 정한 자리로 물러나 접힌다. */
	Retreating,
	/** 사람을 판과 몸 사이에 눌렀다(포획). */
	Gripping
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FIGLiningGapEvent, AIGLiningEntity*, FName /*GapId*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FIGLiningEvent, AIGLiningEntity*);

/**
 * 셋째 괴이, 덧댄 것(개발용 이름). 은폐 공사에 쓰고 남은 방음재·방수막·실리콘·ㄷ자 철골로 된
 * 낮고 넓은 몸에 넓적한 버팀다리 넷이 있다. 눈·입·머리·손발은 없다(SK_Lining, rig_lining.py).
 *
 * 하는 일은 하나다. 같은 벽 속 공간으로 이어진 열린 틈 가운데 가장 넓은 것을 골라 거기로
 * 가서, 네 다리로 둘레를 짚고, 몸으로 눌러 막는다. 다 막은 뒤에야 다음 틈을 고르고, 그때는
 * 지금 누르는 틈보다 넓은 틈으로만 옮겨 간다. 좁은 틈은 열려 있어도 그대로 누른다. 가는
 * 도중에 새 틈이 열려도 방향을 바꾸지 않는다. 막은 자리는 접착층이 눌려 바람이 새지 않고,
 * 다시 뜯어내야 대상이 된다. 소리·시선·손전등·사람에게는 반응하지 않는다. 걷는 길에 서 있거나
 * 누르는 자리와 판 사이에 끼면 눌린다(포획).
 *
 * 움직임은 굽지 않은 뼈 자세다. 두 다리가 벽이나 천장을 짚고 몸을 당긴 뒤 나머지 두 다리가
 * 지지점을 옮긴다. 다리는 두 마디 IK(엉덩이→무릎→발목)에 패드가 짚은 면을 따라 돈다.
 * 접혔을 때는 접자처럼 몸 옆구리에 포개진다.
 *
 * 이야기는 모른다. 공간을 맡은 디렉터가 틈과 쉬는 자리를 정하고 이벤트를 받는다.
 * 틱은 움직이는 동안에만 켠다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGLiningEntity : public AActor
{
	GENERATED_BODY()

public:
	static constexpr int32 LegCount = 4;

	AIGLiningEntity();

	/** SK_Lining을 불러 뼈를 찾는다. 없으면 false이고 이 괴이는 나오지 않는다. */
	bool LoadBody();
	bool HasBody() const { return Body != nullptr; }

	/** 벽이나 바닥에 접혀 붙는다. Normal은 그 면에서 방 쪽, Forward는 몸 앞쪽이 향할 곳. */
	void RestOnSurface(const FVector& Point, const FVector& Normal, const FVector& Forward);
	/**
	 * 그 틈을 이미 눌러 막고 있는 자세로 바로 놓는다(밤이 열릴 때, 불러오기). 소리와 이벤트가
	 * 없다. 틈은 막힌 것으로 적고 덮개는 걸린 채 둔다. 틈이 없으면 아무것도 하지 않는다.
	 */
	void PlaceHolding(FName GapId);

	/** 틈을 넣거나 같은 Id를 고친다. */
	void SetGap(const FIGLiningGap& Gap);
	void ClearGaps();
	const FIGLiningGap* FindGap(FName Id) const;
	/** 덮개가 열리거나 닫혔다. 쉬는 중이면 곧 고르고, 움직이는 중이면 지금 동작을 마친 뒤에 본다. */
	void NotifyGapsChanged();
	/** 막힌 틈을 다시 뜯었다(덮개 쪽이 부른다). */
	void UnsealGap(FName Id);
	bool IsGapOpen(FName Id) const;

	EIGLiningState GetState() const { return State; }
	/** 떨어지기·걷기·짚기·누르기·펴기·물러나기 중이다. */
	bool IsMoving() const;
	/** 움직이거나 사람을 누르는 중이다. 위협 감독이 다른 괴이를 막는다. */
	bool IsBusy() const { return IsMoving() || State == EIGLiningState::Gripping; }
	/** 월드에 움직이거나 누르는 덧댄 것이 있는가. 그동안 다른 괴이는 끼어들지 않는다. */
	static bool IsAnyBusy(const UWorld* World);
	FName GetTargetGap() const { return TargetGap; }
	FName GetHeldGap() const { return HeldGap; }
	bool HasMovedOnce() const { return bHasMoved; }
	FVector GetBodyCenter() const { return BodyCenter; }
	FVector GetBodyUp() const { return BodyRotation.GetUpVector(); }
	/** 다리 패드 자리(검사·연출용). */
	FVector GetPadLocation(int32 Leg) const;
	FVector GetKneeLocation(int32 Leg) const;
	/** 검사용. 다리가 엉덩이→무릎→패드 사이에서 벽이나 소품을 뚫으면 뚫은 것과 자리를, 아니면 빈 문자열. */
	FString FindLegObstruction(int32 Leg) const;
	/** FindLegObstruction과 같되 어느 마디(0 엉덩이→무릎, 1 무릎→발목, 2 발목→패드)인지와 부딪힌 것의 이름만 준다. */
	bool FindLegObstructionPart(int32 Leg, int32& OutSegment, FString& OutComponent, FVector* OutImpact = nullptr,
		FVector* OutNormal = nullptr, float* OutInside = nullptr) const;
	/**
	 * 무릎을 그 자리에 두면 엉덩이→무릎·무릎→발목 마디가 벽·바닥·천장·소품을 지나지 않는가. bThighOnly면 엉덩이→무릎만 본다.
	 * 발목 끝 4 cm는 짚은 면 바로 위라 덜 본다. StandNormal(짚은 면의 법선)을 주면 그 면에 닿는 것만 덜 보고, 옆 소품은 끝까지 본다.
	 */
	bool IsKneeClear(const FVector& Hip, const FVector& Knee, const FVector& Ankle, bool bThighOnly = false,
		const FVector& StandNormal = FVector::ZeroVector) const;
	/**
	 * 넓적다리·정강이가 굵기(반지름 Radius)만큼 비었는가. 엉덩이 쪽 끝은 몸판에 묻혀 있어 반지름만큼 떨어진 데부터, 발목 쪽
	 * 끝은 짚은 면 가까이라 8 cm 덜 본다. StandNormal을 주면 정강이 끝 25 cm 안에서 그 면에 닿는 것은 덜 본다.
	 */
	bool IsLegThickClear(const FVector& Hip, const FVector& Knee, const FVector& Ankle, float Radius,
		const FVector& StandNormal = FVector::ZeroVector) const;
	/** 몸 살·물갈퀴·패드 막 뼈가 있는 몸인가(예전 SK_Lining에는 없다). */
	bool HasSoftBody() const { return bHasSoftBones; }
	/** 검사용: 몸 사분면(다리와 같은 번호) 살이 쉬는 자리에서 처진 거리(월드 cm). */
	FVector GetMassOffset(int32 Quadrant) const;
	/** 검사용: 이 다리의 패드 막 끝이 짚었던 면에 남아 늘어나 있는가, 늘어난 길이(cm, 붙어 있을 때만). */
	bool IsSkinStuck(int32 Leg) const;
	float GetSkinStretch(int32 Leg) const;
	/** 검사용: 몸 둘레 막(치마)이 면을 따라 바깥으로 퍼진 거리와 아래로 처진 거리(cm). 0 앞, 1 뒤, 2 왼쪽, 3 오른쪽. */
	float GetSkirtSpread(int32 Side) const;
	/** 검사용: 몸판이 벽·바닥·소품이나 열린 점검구 문짝을 파고드는가(조금 줄인 몸판 상자로 본다). OutComponent에 걸린 것. */
	bool IsBodyObstructed(FString* OutComponent = nullptr) const;
	float GetSkirtDroop(int32 Side) const;
	/** 검사용: 이 발이 면을 누르고 있는가(옮기는 중이 아니거나, 옮길 차례를 기다리고 있다). */
	bool IsPadHolding(int32 Leg) const;
	/** 검사용: 짚고 있는 발이 짚은 자리에서 벗어난 가장 큰 거리(cm). 다리가 닿지 않아 발이 끌려가면 커진다. */
	float GetMaxPadSlip() const { return MaxPadSlip; }

	/** 5층: 정한 자리를 짚고 몸을 천장까지 편다. */
	void SpreadTo(
		const FVector& InBodyCenter,
		const FVector& InBodyUp,
		const FVector& InBodyForward,
		const TArray<FVector>& Pads,
		const TArray<FVector>& PadNormals,
		float Seconds);
	/**
	 * 편 몸에서 이 다리 무릎을 둘 쪽(월드). 나중에 그 자리에서 걸릴 무릎(5층 철골 틀 런너)이 미리 그쪽으로 굽어 있어,
	 * 틀을 댈 때 무릎이 한 프레임에 건너뛰지 않는다. 펴기가 끝나면(물러나기·쉬기·숨기) 지운다.
	 */
	void SetKneeHint(int32 Leg, const FVector& KneeWorld);
	/** 프레임이 다리(Leg)의 무릎에 걸렸다. 무릎이 그 자리에 묶이고 몸을 접지 못한다. */
	void PinKnee(int32 Leg, const FVector& KneeWorld);
	void ReleasePin();
	bool IsKneePinned() const { return PinnedLeg != INDEX_NONE; }
	int32 GetPinnedLeg() const { return PinnedLeg; }
	/** 정한 면으로 물러나 접힌다. */
	void RetreatTo(const FVector& Point, const FVector& Normal, const FVector& Forward, float Seconds);
	void Hide();

	/** 사람을 누를 수 있는지. 연출 중(5층 대치 직전 등)에는 끈다. */
	void SetGripEnabled(bool bEnabled) { bGripEnabled = bEnabled; }
	/** 눌린 사람이 깨어날 넓은 자리. 공간 디렉터가 정한다. */
	void SetGripRecoveryTransform(const FTransform& InTransform) { GripRecovery = InTransform; }
	/** 포획 연출이 끝났다. 하던 움직임을 이어 간다. */
	void ReleaseGrip();
	/** 검사·장면용: 걸음 빠르기 배율. */
	void SetPaceScale(float Scale) { PaceScale = FMath::Clamp(Scale, 0.25f, 4.0f); }
	/** 소리를 낸다(검사에서 끌 수 있다). */
	void SetAudible(bool bInAudible) { bAudible = bInAudible; RefreshGapDrafts(); }
	/** 검사용: 뼈 자리를 로그로 남긴다. */
	void LogPose(const TCHAR* Tag) const;

	/** 새 틈으로 가기 시작했다. */
	FIGLiningGapEvent OnTargetChosen;
	/** 틈을 다 눌러 막았다. */
	FIGLiningGapEvent OnGapSealed;
	/** 처음으로 벽에서 떨어졌다. */
	FIGLiningEvent OnFirstMove;
	/** 사람을 판과 몸 사이에 눌렀다. */
	FIGLiningEvent OnGrippedPlayer;
	/** 지금 동작을 마치고 멈췄다(막기·펴기·물러나기 끝). */
	FIGLiningEvent OnSettled;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FLeg
	{
		/** 지금 짚은 자리(패드 가운데)와 그 면의 바깥 법선. */
		FVector Pad = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		/** 옮기는 중이면 출발과 도착. */
		FVector FromPad = FVector::ZeroVector;
		FVector FromNormal = FVector::UpVector;
		FVector ToPad = FVector::ZeroVector;
		FVector ToNormal = FVector::UpVector;
		float SwingAlpha = 1.0f;
		bool bSwinging = false;
		/** 1이면 접자처럼 접은 자세, 0이면 짚은 자세. 처음 옮길 때 펴진다. */
		float Fold = 1.0f;
		float FoldFrom = 1.0f;
		float FoldTo = 1.0f;
		/** 짚은 직후 패드가 면을 한 번 더 눌렀다 자리 잡는 정도(1에서 0으로 준다). */
		float PlantKick = 0.0f;
		/** 옮길 때 패드를 드는 높이 배율. 틈 둘레를 더듬을 때는 낮게 든다. */
		float LiftScale = 1.0f;
		/**
		 * 걷다가 옮기는 발이 제 엉덩이 밑을 지나가게 되면 몸 면을 따라 그만큼 비켜 돈다(가장 많이 비킬 때의 방향과
		 * 거리). 비키는 몫은 스윙 비율 SwingDetourPeak에서 가장 크고 처음과 끝에서는 0이다.
		 */
		FVector SwingDetour = FVector::ZeroVector;
		float SwingDetourPeak = 0.5f;
		/**
		 * 이번에 옮기는 동안 무릎 극이 따라갈 길(월드 방향, 스윙 비율 0~1을 고르게 나눈 자리마다). bSwingPlanned가
		 * 꺼져 있으면 몸 기준 극을 쓴다. 옮기기 시작할 때 정하고(PlanSwingPole) 디디면 끈다. 디딘 뒤에도 쓰면 몸이 당겨
		 * 오는 동안 그 극이 벽 쪽을 가리켜 무릎이 벽을 지났다.
		 */
		FVector SwingPoles[8];
		bool bSwingPlanned = false;
		/**
		 * 짚은 다리의 무릎이 이번 당기기 동안 따라갈 길(SwingPoles를 같이 쓴다). 당기기를 시작할 때 정하고(PlanPullPole)
		 * 당기기가 끝나거나 다리를 들면 끈다. PullPlanFrom은 길을 정한 때 몸이 길 위에서 간 거리다.
		 */
		bool bPullPlanned = false;
		float PullPlanFrom = 0.0f;
		/** 이번에는 몸 쪽 가장자리부터 들지 않고 패드를 평평하게 뗀다(엉덩이 바로 밑에서 출발하는 짚기). */
		bool bFlatPeel = false;
		/** 지난 프레임에 쓴 몸 기준 극(눕히기까지 넣은 것). PlanSwingPole이 기본값으로 쓴다. */
		FVector LastPole = FVector::ZeroVector;
		/** 마지막으로 푼 관절 자리(검사·충돌용). */
		FVector Hip = FVector::ZeroVector;
		FVector Knee = FVector::ZeroVector;
		FVector Ankle = FVector::ZeroVector;
		/**
		 * 무릎이 지난 프레임에 있던 쪽(엉덩이→발목 축에 수직인 단위 방향). 극이 그 축과 나란해져도 무릎이 원을
		 * 가로질러 반대로 튀지 않게 다음 프레임이 이어받는다. 0이면 이을 것이 없다(처음 놓을 때, 순간 이동).
		 */
		FVector KneeBend = FVector::ZeroVector;
		/** 무릎이 무릎 빠르기에 막혀 IK 자리를 아직 못 따라잡았다. 디딘 뒤에도 따라잡을 때까지 같은 한도로 옮긴다. */
		bool bKneeCatchUp = false;
		/**
		 * 가까운 쪽이 벽 속이라 반대쪽으로 멀리 돌기 시작한 방향(+1·-1, 0이면 없음). 정한 쪽에 닿을 때까지 지킨다.
		 * 프레임마다 다시 고르면 한 걸음 물러난 자리에서 가까운 쪽이 다시 비어 보여 무릎이 두 자리를 오갔다.
		 */
		float KneeTurnSign = 0.0f;
		/** 무릎을 기다리며 디딜 자리 위에 머문 시간(초). */
		float LandWaitSeconds = 0.0f;
		/** 마지막으로 놓은 패드 가운데(짚는 면)와 그 틀(X 바깥쪽, Z 짚은 면의 법선). */
		FVector PadCenter = FVector::ZeroVector;
		FQuat PadFrame = FQuat::Identity;
	};

	/** 무른 데 한 곳. 쉬는 자리에서 비킨 거리(월드 cm)와 그 속도. */
	struct FSoftPoint
	{
		FVector Offset = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
	};

	/** 옮기는 길 하나에서 무릎 극이 따라갈 길과 그 길의 점수(MeasureKneePath). */
	struct FKneePath
	{
		/**
		 * 자리마다 무릎이 굵기만큼 비면 2, 가운데 선만 비면 1, 막히면 -10을 더하고 많이 돌수록 조금 깎은 점수. 정한 길과
		 * 몸 기준 극을 그대로 쓸 때의 점수다.
		 */
		float Score = 0.0f;
		float DefaultScore = 0.0f;
		/** 몸 기준 극이 내내 굵기만큼 비어 있어 나머지 방향은 재지 않았다. */
		bool bDefaultClear = false;
		/** 정한 길과 몸 기준 극에서 무릎이 막히는 자리 수. */
		int32 Blocked = 0;
		int32 DefaultBlocked = 0;
		FVector Poles[8];
	};

	/** 지금 짚은 자리에서 고른 짚을 자리로 옮겨 갈 때의 비켜 돌기와 무릎 길(ChooseBracePad). */
	struct FBraceSwing
	{
		FVector Detour = FVector::ZeroVector;
		float DetourPeak = 0.5f;
		FKneePath Path;
	};

	/** 틈을 짚고 누를 몸 자리. 벽 면 위 몸 가운데와 몸 방향(앞 +X가 벽을 따라 향하는 쪽, 위 +Z가 틈 법선). */
	struct FGapPose
	{
		FVector Anchor = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		/** 몸 앞쪽과 옆쪽(벽 면 위 단위 방향)과 그 쪽으로 잰 틈 반치수(cm). */
		FVector Forward = FVector::UpVector;
		FVector Side = FVector::RightVector;
		FVector2D GapHalf = FVector2D::ZeroVector;
		/** 몸을 벽 면 위에서 90도 돌려 짧은 쪽을 가로로 댔다(감독이 정한 아래 다리 접기는 돌리지 않은 몸 기준이다). */
		bool bTurned = false;
		bool bValid = false;
	};

	void SetState(EIGLiningState NewState);
	void StartTicking();
	bool ChooseTarget();
	void BeginTravelTo(FName GapId);
	void BuildPath(const FVector& Start, const FVector& StartUp, const FVector& Goal, const FVector& GoalNormal);
	/**
	 * 다른 면으로 건너가는 길 하나를 몸판이 지나는 자리로 매긴다. 벽·소품을 지나는 점은 1, 목표 틈의 열린 문짝이나
	 * 서 있는 사람을 지나는 점은 10이다. PathStartRotation·PathGoalRotation이 정해진 뒤에 부른다.
	 */
	int32 CountPathObstruction(const TArray<FVector>& Points, const AIGAccessHatch* Hatch) const;
	/** 마지막으로 매긴 길의 내역(문짝·사람·벽·엉덩이). 진단 로그용. */
	mutable FString LastPathCost;
	FVector PathPoint(float Distance) const;
	float PathLength() const { return PathDistances.IsEmpty() ? 0.0f : PathDistances.Last(); }
	FQuat BodyRotationAt(float Alpha) const;
	void UpdatePeeling(float DeltaSeconds);
	void UpdateStepping(float DeltaSeconds);
	void UpdateBracing(float DeltaSeconds);
	void UpdatePressing(float DeltaSeconds);
	void UpdateHolding(float DeltaSeconds);
	void UpdateSpread(float DeltaSeconds);
	void UpdateRetreating(float DeltaSeconds);
	void UpdateSwings(float DeltaSeconds);
	/** 막고 있는 동안 다리 하나로 틈 둘레를 낮게 더듬는다(몇 초에 한 번). */
	void UpdateProbing(float DeltaSeconds);
	/** 지금 상태에서 몸이 눌리고 늘어나는 정도(몸 기준 X 앞, Y 옆, Z 면 법선). */
	FVector ComputeBodySquash() const;
	/**
	 * 무른 데의 움직임. 몸 사분면 살은 중력과 끌려가는 가속도에 처지고 늦게 따라오며, 짚을 때마다 한 번
	 * 더 처진다. 물갈퀴는 다리를 접을수록 느슨하게 처지고 패드가 떨어지기 직전에 먼저 벌어진다. 패드 막
	 * 끝은 짚었던 면에 붙어 늘어났다가 놓이면 튕겨 돌아온다. 뼈가 없는 몸이면 아무것도 하지 않는다.
	 */
	void UpdateSoftParts(float DeltaSeconds);
	/** 자리를 옮겨 놓았다(쉬기·막은 채로 놓기·숨기기). 무른 데를 그 자리에서 다 처진 채로 맞춘다. */
	void ResetSoftParts();
	void PoseSoftParts(const FTransform& BodyTransform);
	bool AreSoftPartsSettled() const;
	/** 몸이 받는 중력. 면에 붙어 있으면 그 면이 받치는 성분을 뺀다. */
	FVector SoftGravity() const;
	/** 다리 하나가 짚었다. 무게가 옮겨 가며 살이 한 번 처진다. */
	void KickSoftParts(int32 Leg, float Strength);
	/** 패드 막 끝 뼈가 패드를 그대로 따를 때의 자리(월드). */
	FTransform DefaultSkinWorld(int32 Leg) const;
	/** 발이 떨어질 때 제자리에서 몸 쪽 가장자리만 드는 몫(스윙 비율). 처음 벽에서 떨어질 때는 더 길다. */
	float PeelHoldFraction() const;
	/** 짚고 있는 발이 이 몸 자리에서도 닿는가(당기다 발이 끌려가지 않게). */
	bool PlantedLegsReach(const FVector& Center, const FQuat& Rotation) const;
	/** 이 다리가 지금 몸에서 얼마나 뻗었는가(1이면 다 뻗음). */
	float LegReach(int32 Leg, const FVector& Center, const FQuat& Rotation) const;
	/** 이 다리로 Pad(면 법선 Normal)를 짚으려면 얼마나 뻗어야 하는가(1이면 다 뻗음). SolveLeg와 같은 수다. */
	float ReachTo(int32 Leg, const FVector& Pad, const FVector& Normal, const FVector& Center, const FQuat& Rotation) const;
	/** 엉덩이가 Hip일 때 이 다리로 Pad(면 법선 Normal)를 짚는 발목 자리. SolveLeg와 같은 수다. */
	FVector AnkleFor(int32 Leg, const FVector& Pad, const FVector& Normal, const FVector& Hip, const FQuat& Rotation) const;
	void StartSwing(int32 Leg, const FVector& Pad, const FVector& Normal, float FoldTarget);
	/** 옮기는 패드가 스윙 비율 T(부드럽게 한 값)에 있을 자리. Detour만큼 비켜 돌고 Peak에서 가장 많이 비킨다. */
	FVector SwingPadAt(const FLeg& Leg, float T, const FVector& Detour, float Peak) const;
	/**
	 * 걷는 동안 옮기는 발이 제 엉덩이 밑을 지나가 무릎이 SwingKneeSpeed보다 빨리 튀게 되면 몸 면을 따라 비켜 돌 쪽과
	 * 거리를 정한다. 그 빠르기 안에 드는 길 가운데 가장 덜 비켜 도는 것을 고르고, 벽·천장·바닥을 뚫는 길은 버린다.
	 * 맞는 길이 없으면 그대로 옮긴다.
	 */
	void PlanSwingDetour(int32 Leg);
	/** PlanSwingDetour가 Leg의 스윙에 고를 비켜 돌기. 비켜 돌 일이 없거나 맞는 길이 없으면 false다. 다리 상태는 바꾸지 않는다. */
	bool FindSwingDetour(int32 Index, const FLeg& Leg, FVector& OutDetour, float& OutPeak) const;
	bool FindContact(int32 Leg, const FVector& FutureBody, const FQuat& FutureRotation, const FVector& Travel, FVector& OutPad, FVector& OutNormal) const;
	/**
	 * 틈을 덮을 몸 자리를 정한다. 틈 가운데에 몸판(80×120)을 대면 옆 소품·이웃 벽·열린 점검구 문짝을 파고드는 자리가
	 * 있다(302호 큰 점검구 옆 밀어 둔 장롱과 북쪽 문짝, 안쪽 방 큰 점검구 옆 보관함과 서쪽 문짝, 5층 배관 점검 자리
	 * 남벽). 그러면 몸판이 틈을 덮는 만큼 안에서 벽 면을 따라 가장 덜 비키고, 그래도 막히면 몸을 벽 면 위에서 90도
	 * 돌려 짧은 쪽을 가로로 대 본다. 빈 자리가 없으면 틈 가운데 그대로 둔다.
	 */
	FGapPose FitGapPose(const FIGLiningGap& Gap) const;
	/** 접은 다리의 패드가 이 몸 자리에서 놓일 곳(월드). */
	FVector FoldedPadWorld(int32 Leg, const FVector& Center, const FQuat& Rotation) const;
	/** 몸판(조금 줄인 상자)을 벽에서 띄운 그 자리에 두면 벽·바닥·소품·이 틈의 열린 문짝과 겹치지 않는가. */
	bool IsBodySpaceFree(const FIGLiningGap& Gap, const FVector& Anchor, const FQuat& Rotation) const;
	/** 그 자리 몸판이 겹치는 것들의 깊이 합(cm). 빈 자리가 없을 때 가장 덜 파고드는 자리를 고른다. OutNames에 무엇=깊이를 붙인다. */
	float BodySpacePenetration(const FIGLiningGap& Gap, const FVector& Anchor, const FQuat& Rotation, FString* OutNames) const;
	/**
	 * 틈을 짚고 버틸 때 이 다리가 짚을 자리. 틈 둘레(BraceMargin 바깥)의 벽을 먼저 보고, 그 자리가 바닥 밑·벽 속·옆 소품
	 * 속이거나 엉덩이에서 막혀 있으면 엉덩이에서 둘레 쪽으로 부채꼴로 쏴서 닿는 면(옆 소품의 옆면, 바닥, 이웃 벽, 천장)
	 * 가운데 둘레에 가장 가까운 곳을 고른다. 없으면 false이고 다리를 몸 밑에 접는다. OutSwing을 주면 다리가 지금 몸에서
	 * 지금 짚은 자리를 떼어 그리로 옮겨 가는 길의 무릎 길까지 재서 고르고, 고른 자리의 비켜 돌기와 무릎 길을 돌려준다.
	 */
	bool ChooseBracePad(int32 Leg, const FIGLiningGap& Gap, const FGapPose& Pose, FVector& OutPad, FVector& OutNormal,
		FBraceSwing* OutSwing = nullptr) const;
	/**
	 * 틈을 짚으러 지금 드는 다리의 짚을 자리를 옮겨 가는 길까지 보고 다시 고르고, 그 길의 비켜 돌기와 무릎 길을 그대로
	 * 쓴다(짚기를 시작할 때는 짚을 자리만 보고 정해 둔다). 다시 고를 수 없으면 false다.
	 */
	bool RechooseBracePad(int32 Index);
	/**
	 * 엉덩이 Hip에서 이 다리로 Pad를 짚을 때 패드 덩어리가 옆 벽·바닥·소품이나 Hatch의 열린 문짝을 파고들지 않고, 무릎을
	 * 둘 빈 쪽이 엉덩이→발목 원 위에 한 군데는 있는가(SolveLeg가 거기로 비킨다). 틈을 짚을 때와 걸을 때 다 쓴다.
	 */
	bool IsPadPlaceable(int32 Leg, const FVector& Hip, const FQuat& Rotation, const FVector& Pad, const FVector& Normal,
		const AIGAccessHatch* Hatch) const;
	/** 그 자리가 어느 틈의 벽 면 뒤(열린 점검구 속)인가. 거기를 짚으면 다리가 개구부 옆 벽을 뚫고 들어간다. */
	bool IsInsideGapCavity(const FVector& Pad) const;
	/** 엉덩이에서 그 자리까지 곧게 그은 선이 열린 덮개 문짝(다 연 자리)을 지나는가. */
	bool CrossesOpenHatchDoor(const FVector& Hip, const FVector& Pad) const;
	/** 이 다리가 버티는 자리(짚을 때와 누를 때 몸)에서 그 자리를 짚을 수 있는가. 마디가 막히거나 패드가 옆 면을 파고들면 안 된다. */
	bool IsBracePadUsable(int32 Leg, const FIGLiningGap& Gap, const FGapPose& Pose, const FVector& Pad, const FVector& Normal) const;
	/** 틈을 막고 있을 때 플레이어가 덮개를 닫으면 문짝이 몸판이나 다리에 걸리는 각(도). */
	float FindBlockedHatchYaw(const FIGLiningGap& Gap, const FGapPose& Pose) const;
	FVector HipWorld(int32 Leg, const FVector& Center, const FQuat& Rotation) const;
	void PoseBody();
	void SolveLeg(int32 Leg);
	/**
	 * 같은 벽을 따라 걷는 동안 이 다리의 무릎을 엉덩이→발목 축으로 돌릴 각(라디안). 무릎이 벽 면에서
	 * AlongWallKneeHeight보다 나오면 그만큼만 벽 면 쪽으로 눕힌다. 눕힌 무릎이 천장·바닥·옆 벽에 묻히거나
	 * 몸판 위에 얹히거나 다른 무릎과 맞닿으면 그 앞까지만 눕히고, 막힌 쪽 대신 반대쪽이 비어 있으면 넘긴다.
	 * 걷는 동안이 아니면 0으로 돌아간다.
	 */
	float LeanKnee(int32 Leg, const FVector& Hip, const FVector& Ankle, const FVector& Pole);
	/** 무릎을 그 자리에 둘 때 빈 정도. 2는 굵기만큼 빔, 1은 가운데 선만 빔, 0은 막힘. */
	int32 LegRoom(const FVector& Hip, const FVector& Knee, const FVector& Ankle, const FVector& StandNormal) const;
	/**
	 * 엉덩이 Hip에서 Pad를 짚을 때 무릎 둘 자리가 모자란 만큼의 벌점(짚을 자리 점수와 같은 cm 단위). 발목이 엉덩이 곁이거나,
	 * 무릎 원에서 굵기만큼 빈 쪽이 세 방향(45도) 이상 이어지지 않으면 붙는다.
	 */
	float KneeRoomPenalty(int32 Leg, const FVector& Hip, const FQuat& Rotation, const FVector& Pad, const FVector& Normal) const;
	/**
	 * 엉덩이→발목 축을 도는 무릎 원(가운데 Center, 축 Axis, 극 쪽 PoleSide, 반지름 Radius) 위에서 이번 프레임 무릎 쪽을
	 * 고른다. 둘레 24쪽의 빈 정도를 재서 가장 잘 빈 쪽 가운데 극과 지난 쪽에 가까운 곳을 목표로 삼고, 지난 쪽에서 목표까지
	 * 빈 호로만 한 프레임에 MaxTurn(라디안)씩 돈다. 두 호가 다 막히면 지난 쪽이 비어 있을 때 그 자리를 지킨다.
	 */
	FVector ChooseKneeDirection(int32 Index, const FVector& Hip, const FVector& Ankle, const FVector& Center, const FVector& Axis,
		const FVector& PoleSide, float Radius, bool bHasPrevious, const FVector& Previous, const FVector& StandNormal, float MaxTurn);
	/** 옮기기를 시작하는 다리의 무릎 극이 따라갈 길을 패드가 지날 길 전체를 보고 정한다(FLeg::SwingPoles). */
	void PlanSwingPole(int32 Index);
	/** 이 다리의 이번 스윙에 무릎 길을 정할 수 있는가(틀에 걸리거나 접고 펴는 다리, 극을 모르는 다리는 안 된다). */
	bool CanPlanKneePath(int32 Index) const;
	/** 몸을 당기는 동안 짚은 다리의 무릎이 따라갈 길을 정한다(FLeg::bPullPlanned). */
	void PlanPullPole(int32 Index);
	/** 당기기를 시작한다. 짚은 다리의 무릎 길은 다음 프레임부터 한 다리씩 정한다. */
	void BeginPullPlans();
	/**
	 * Swing의 출발·도착·비켜 돌기대로 패드가 옮겨 가는 동안 엉덩이가 HipFrom에서 HipTo로 갈 때, 스윙을 여덟 자리로 나눠
	 * 자리마다 무릎 원 24방향의 빈 정도를 재고 무릎 빠르기 안에서 빈 방향만 이어 가는 길을 찾는다. 처음 자리의 무릎은
	 * Swing.KneeBend, 몸 기준 극은 Swing.LastPole이다.
	 */
	void MeasureKneePath(int32 Index, const FLeg& Swing, const FVector& HipFrom, const FQuat& RotationFrom, const FVector& HipTo,
		const FQuat& RotationTo, FKneePath& Out) const;
	void ResetKneeLean();
	void UpdateColliders();
	/** 밀려나는 동안 Alpha(0~1)의 몸 가운데와 회전. Away는 처음에 면에서 떨어지는 거리, Before는 구석 면 앞에서 다 돌 자리다. */
	FVector RetreatPoseAt(float Alpha, float Away, const FVector& Before, FQuat& OutRotation) const;
	/** 열리는 덮개 문짝이 몸판에 닿으면 그 각에서 멈추게 하고, 몸이 비키면 푼다. 다 열린 문짝은 밀지 않는다. */
	void UpdateHatchOpenLimits();
	/** 몸판·다리·패드 충돌을 한꺼번에 켜거나 끈다. */
	void SetBlockersEnabled(bool bEnabled);
	void CheckGrip();
	/** 틈 덮개에 몸이 걸렸다고 알린다. Pose를 주면 닫히던 문짝이 몸판·다리에 걸리는 각을 그 자리로 잰다. */
	void SetObstructions(FName GapId, bool bObstructed, const FGapPose* Pose = nullptr);
	/** 지금 짚거나 막고 있는 틈의 열린 문짝. 다리와 몸판이 비켜야 한다. */
	UStaticMeshComponent* ActiveHatchDoor() const;
	void SealGap(FName GapId);
	void PlayCue(const TCHAR* Sample, int32 Variants, const FVector& Where, float Volume, float Pitch);
	/**
	 * 열린 틈마다 벽 속으로 빨려 드는 바람 소리를 건다. 틈이 막히거나 닫히면 그 소리가 먹히듯
	 * 줄며 끊긴다. 플레이어는 어디가 아직 열려 있는지를 귀로도 안다.
	 */
	void RefreshGapDrafts();
	void Caption(const FText& Text, const FVector& Where, float Seconds, double& LastAt, float MinInterval);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> Body;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> BodyBlocker;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCapsuleComponent>> LegBlockers;

	/** 패드 덩어리. 짚은 면에 붙은 납작한 상자다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> PadBlockers;

	/** 열린 틈의 바람 소리(틈 Id별). */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UAudioComponent>> GapDrafts;

	// 뼈 이름과 쉬는 자세(컴포넌트 공간).
	FName BodyBone;
	FName SegmentBones[LegCount][3];
	FTransform RestBody;
	FTransform RestSegment[LegCount][3];
	FVector RestPad[LegCount];
	/** 쉬는 자세의 엉덩이 자리(몸 기준, cm). */
	FVector HipLocal[LegCount];
	float Length1[LegCount] = {};
	float Length2[LegCount] = {};
	/** 쉬는 자세에서 다리가 뻗은 수평 방향(몸 기준). */
	FVector RestOutward[LegCount];

	// 무른 뼈(rig_lining.py의 mass_*·web_*·skin_*)와 쉬는 자세에서 부모 뼈에 대한 자리.
	FName MassBones[LegCount];
	FName WebBones[LegCount];
	FName SkinBones[LegCount];
	FTransform MassRest[LegCount];
	FTransform WebRestBody[LegCount];
	FTransform WebRestUpper[LegCount];
	FTransform SkinRest[LegCount];
	/** 몸 둘레로 늘어진 큰 막(앞·뒤·왼쪽·오른쪽). 들려 있으면 처지고, 틈을 누르면 면을 따라 퍼진다. */
	static constexpr int32 SkirtCount = 4;
	FName SkirtBones[SkirtCount];
	FTransform SkirtRest[SkirtCount];
	bool bHasSoftBones = false;
	/** 마지막으로 놓은 다리 마디(월드). 물갈퀴와 패드 막 뼈를 여기에 맞춘다. */
	FTransform SegmentWorld[LegCount][3];
	bool bSegmentsPosed = false;
	FSoftPoint Mass[LegCount];
	FSoftPoint Web[LegCount];
	FSoftPoint Skin[LegCount];
	FSoftPoint Skirt[SkirtCount];
	bool bSkinStuck[LegCount] = {};
	float MaxPadSlip = 0.0f;
	/**
	 * 검사 실행에서만 센다(-IGLiningProbe·-IGLeftoverProbe·-IGNightFourHammerProbe). 다리마다 무릎이 한 프레임에
	 * 움직인 가장 큰 거리와 15 cm를 넘은 프레임, 마디가 벽·바닥·천장을 지난 프레임. 끝날 때 LINING_LEGS로 남긴다.
	 */
	bool bMeasureLegs = false;
	/** 무릎 방향을 프레임 사이에 잇는다. 비교 검사에서 -IGLiningKneeBaseline으로 예전처럼 끌 수 있다. */
	bool bKneeContinuity = true;
	float MaxKneeStep[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	int32 KneeJumpFrames[4] = {0, 0, 0, 0};
	int32 ObstructedFrames[4] = {0, 0, 0, 0};
	/** 넓적다리·정강이 겉(반지름 6 cm)이 벽·소품·문짝에 묻힌 프레임. 가운데 선은 비어도 겉이 보이게 묻히는 것을 센다. */
	int32 ThickObstructedFrames[4] = {0, 0, 0, 0};
	/**
	 * 다리 풀이에 든 시간(검사 로그 LINING_SOLVE_TIME). 틈 둘레를 짚을 자리 고르기, 옮기기 전 무릎 길 정하기, 네 다리 풀기를
	 * 프레임마다 더한다.
	 */
	double SolveFrameSeconds = 0.0;
	double SolveTotalSeconds = 0.0;
	double SolveMaxSeconds = 0.0;
	double PlanTotalSeconds = 0.0;
	double PlanMaxSeconds = 0.0;
	int32 PlanCount = 0;
	double BraceChooseMaxSeconds = 0.0;
	int32 MeasuredFrames = 0;
	FString WorstKneeStep;
	/** SetKneeHint로 무릎 쪽을 정한 다리와 그 자리. 없으면 INDEX_NONE. */
	int32 HintLeg = INDEX_NONE;
	FVector KneeHint = FVector::ZeroVector;
	/** 무릎이 걸린 자리로 옮겨 가는 몫(0~1). 걸리는 순간 0에서 짧게 차오른다. */
	float PinBlend = 1.0f;
	/** 관통 프레임을 다리·상태·마디·부딪힌 것으로 나눈 수. 어디를 고칠지 정할 때 본다. */
	TMap<FString, int32> ObstructionBreakdown;
	/** 같은 묶음에서 마디가 물체 속을 지난 가장 긴 길이(cm). 얕게 스친 것과 깊이 뚫은 것을 가른다. */
	TMap<FString, float> ObstructionDepth;
	/** 몸판이 벽·소품·문짝을 파고든 프레임(상태별). */
	int32 BodyObstructedFrames = 0;
	TMap<int32, int32> BodyObstructionByState;
	/** 끌려간 거리가 이만큼을 넘으면 어디서 그랬는지 로그로 남기고 두 배로 올린다. */
	float NextSlipLog = 3.0f;
	/** 걷거나 틈 둘레를 짚는 동안 면을 누르고 있던 발의 가장 적은 수(끝날 때 로그로 남긴다). */
	int32 MinPadsHoldingWhileWalking = LegCount;
	/** 다리가 닿지 않아 당기기를 일찍 끝낸 횟수(잇달아). 길이 막혔으면 몇 번 뒤에는 그냥 당긴다. */
	int32 BlockedPulls = 0;
	FTransform SkinStuckWorld[LegCount];
	FVector SoftLastCenter = FVector::ZeroVector;
	FVector SoftLastVelocity = FVector::ZeroVector;
	FVector SoftAcceleration = FVector::ZeroVector;
	/** 몸이 면에 붙어 있는 정도(0 다리로 들고 있음, 1 면에 붙음). 상태가 바뀌면 따라간다. */
	float SoftContact = 1.0f;
	bool bSoftFresh = true;

	TArray<FIGLiningGap> Gaps;
	FLeg Legs[LegCount];
	EIGLiningState State = EIGLiningState::Hidden;
	EIGLiningState StateBeforeGrip = EIGLiningState::Hidden;
	FName TargetGap;
	FName HeldGap;
	/** 가고 있는 틈에서 짚고 누를 몸 자리(BeginTravelTo가 정한다)와 막고 있는 틈의 몸 자리. */
	FGapPose TargetPose;
	FGapPose HeldPose;
	FVector BodyCenter = FVector::ZeroVector;
	FQuat BodyRotation = FQuat::Identity;
	float StateSeconds = 0.0f;
	bool bHasMoved = false;
	bool bGapsDirty = false;
	bool bGripEnabled = true;
	bool bAudible = true;
	float PaceScale = 1.0f;
	FTransform GripRecovery = FTransform::Identity;

	// 걷는 길.
	TArray<FVector> PathPoints;
	TArray<float> PathDistances;
	float PathTravelled = 0.0f;
	FQuat PathStartRotation = FQuat::Identity;
	FQuat PathGoalRotation = FQuat::Identity;
	/** 같은 벽 위의 틈으로 가는 길이면 몸도 발도 그 벽(면 위의 점과 방 쪽 법선)을 따라간다. */
	bool bPathAlongWall = false;
	FVector PathWallPoint = FVector::ZeroVector;
	FVector PathWallNormal = FVector::ForwardVector;
	/** 무릎을 벽 면 쪽으로 눕히는 정도(0~1). 같은 벽을 따라 걷는 동안 1로 차오르고, 다 오면 0으로 빠진다. */
	float KneeLean = 0.0f;
	/** 다리마다 지금 눕힌 각(라디안)과 눕히는 쪽(+1, -1, 0은 아직 안 정함). */
	float KneeLeanAngle[LegCount] = {};
	int32 KneeLeanSide[LegCount] = {};
	/** 눕히는 쪽을 넘긴 뒤 남은 시간(초). 그동안은 다시 넘기지 않는다. */
	float KneeLeanHold[LegCount] = {};
	/** 이번 자세를 놓는 틱의 시간(초). 순간이동으로 놓을 때는 0이다. */
	float PoseDeltaSeconds = 0.0f;
	/** 당기기 단계인가(아니면 두 다리를 옮기는 단계). */
	bool bPulling = true;
	float PhaseSeconds = 0.0f;
	float PullFrom = 0.0f;
	float PullTo = 0.0f;
	/** 이번 당기기에서 무릎 길을 정할 다음 다리(LegCount면 다 정했다). 한 프레임에 한 다리씩 정한다. */
	int32 PullPlanNext = LegCount;
	int32 SwingGroup = 0;
	int32 PeelLeg = 0;

	// 누르기·펴기·물러나기의 출발과 도착.
	FVector MoveFromCenter = FVector::ZeroVector;
	FVector MoveToCenter = FVector::ZeroVector;
	/** 밀려날 때 붙을 구석 면 앞에서 다 돌 자리. RetreatTo가 몸판이 덜 지나는 자리로 고른다. */
	FVector RetreatBefore = FVector::ZeroVector;
	/** 밀려날 때 처음에 짚고 있던 면에서 곧장 떨어지는 거리(cm). */
	float RetreatAway = 40.0f;
	FQuat MoveFromRotation = FQuat::Identity;
	FQuat MoveToRotation = FQuat::Identity;
	float MoveSeconds = 1.0f;
	TArray<FVector> SpreadPads;
	TArray<FVector> SpreadNormals;

	int32 PinnedLeg = INDEX_NONE;
	FVector PinnedKnee = FVector::ZeroVector;

	/** 몸 뼈에 거는 눌림·늘어남. 상태가 바뀌면 부드럽게 따라간다. */
	FVector BodySquash = FVector::OneVector;
	float ProbeSeconds = 0.0f;
	int32 ProbeCount = 0;
	double LastCreakCaptionAt = -100.0;
	double LastPressCaptionAt = -100.0;
	double LastCreakAt = -100.0;
	uint32 CueSeed = 23u;
	float HoldingCheckSeconds = 0.0f;
};
