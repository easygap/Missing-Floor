#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/** 건물 안을 기어 다니는 길의 한 점. 발이 닿는 높이에 있다. */
struct INDIEGAME_API FIGBuildingNavNode
{
	FVector Feet = FVector::ZeroVector;
	/** 0이 1층, 3이 4층. 반 층 참은 아래층으로 친다. */
	int32 Floor = 0;
	/** 계단 띠나 반 층 참 위. 여기서는 바닥을 쓸며 기지 않고 디딤판을 따라 오르내린다. */
	bool bOnStair = false;
	/** 문 앞과 방 안, 복도 끝. 놓친 소리를 찾을 때 들러서 귀를 대 볼 만한 자리다. */
	bool bLookout = false;
	TArray<int32> Links;
};

/**
 * 위층 사람이 층을 오가는 길. 1~4층 복도의 점과 문 앞, 1층 연결통로와 관리실,
 * 그리고 서쪽 계단탑의 디딤판 위 점을 잇는다. 계단 치수는 씬이 한 곳에서 들고
 * (GetStairClimbFeet), 여기서는 그 점을 받아 잇기만 한다.
 *
 * 옥상과 5층은 없다. 그는 자기가 갇혔던 층에는 올라가지 않는다(설정집 §8 3-3).
 */
class INDIEGAME_API FIGBuildingNav
{
public:
	void Build();
	bool IsBuilt() const { return Nodes.Num() > 0; }
	const TArray<FIGBuildingNavNode>& GetNodes() const { return Nodes; }

	/** 발 높이로 층을 정한다. 0~3은 1~4층이고, 옥상·5층은 4다. */
	static int32 FloorOfFeet(float FeetZ);
	/** 계단탑 안(층 참, 반 층 참, 계단 띠)인가. */
	static bool IsInStairCore(const FVector& Feet);

	/**
	 * Feet과 같은 층에서 벽에 막히지 않고 닿는 가장 가까운 점. 계단탑 안이면 계단
	 * 점도 후보다. 찾지 못하면 INDEX_NONE.
	 */
	int32 FindNearest(const UWorld* World, const FVector& Feet, const AActor* Ignore) const;
	/** 가장 짧은 길. From과 To가 같으면 그 점 하나다. */
	bool FindPath(int32 From, int32 To, TArray<int32>& OutPath) const;
	/** From에서 길을 따라 MaxDistance 안에 드는 점과 그 거리. */
	void GatherWithin(int32 From, float MaxDistance, TArray<TPair<int32, float>>& OutNodes) const;

private:
	int32 Add(const FVector& Feet, bool bOnStair, bool bLookout = false);
	void Link(int32 A, int32 B);
	/** 한 층의 복도 점을 서쪽에서 동쪽으로 잇는다. 문 앞 점은 복도 줄에 끼워 넣는다. */
	void LinkChain(const TArray<int32>& Chain);

	TArray<FIGBuildingNavNode> Nodes;
};
