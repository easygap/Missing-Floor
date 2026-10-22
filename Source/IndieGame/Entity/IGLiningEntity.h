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
 * 가서, 네 다리로 둘레를 짚고, 몸으로 눌러 막는다. 다 막은 뒤에야 다음 틈을 고른다. 가는
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
	/** 몸 살·물갈퀴·패드 막 뼈가 있는 몸인가(예전 SK_Lining에는 없다). */
	bool HasSoftBody() const { return bHasSoftBones; }
	/** 검사용: 몸 사분면(다리와 같은 번호) 살이 쉬는 자리에서 처진 거리(월드 cm). */
	FVector GetMassOffset(int32 Quadrant) const;
	/** 검사용: 이 다리의 패드 막 끝이 짚었던 면에 남아 늘어나 있는가, 늘어난 길이(cm, 붙어 있을 때만). */
	bool IsSkinStuck(int32 Leg) const;
	float GetSkinStretch(int32 Leg) const;
	/** 검사용: 몸 둘레 막(치마)이 면을 따라 바깥으로 퍼진 거리와 아래로 처진 거리(cm). 0 앞, 1 뒤, 2 왼쪽, 3 오른쪽. */
	float GetSkirtSpread(int32 Side) const;
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
		/** 마지막으로 푼 관절 자리(검사·충돌용). */
		FVector Hip = FVector::ZeroVector;
		FVector Knee = FVector::ZeroVector;
		FVector Ankle = FVector::ZeroVector;
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

	void SetState(EIGLiningState NewState);
	void StartTicking();
	bool ChooseTarget();
	void BeginTravelTo(FName GapId);
	void BuildPath(const FVector& Start, const FVector& StartUp, const FVector& Goal, const FVector& GoalNormal);
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
	bool FindContact(int32 Leg, const FVector& FutureBody, const FQuat& FutureRotation, const FVector& Travel, FVector& OutPad, FVector& OutNormal) const;
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
	void ResetKneeLean();
	void UpdateColliders();
	/** 몸판·다리·패드 충돌을 한꺼번에 켜거나 끈다. */
	void SetBlockersEnabled(bool bEnabled);
	void CheckGrip();
	void SetObstructions(FName GapId, bool bObstructed);
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
	int32 SwingGroup = 0;
	int32 PeelLeg = 0;

	// 누르기·펴기·물러나기의 출발과 도착.
	FVector MoveFromCenter = FVector::ZeroVector;
	FVector MoveToCenter = FVector::ZeroVector;
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
