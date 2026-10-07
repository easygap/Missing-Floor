#include "Entity/IGBuildingNav.h"

#include "Core/IGPrologueWorldScene.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace IGBuildingNav
{
	/** 복도 가운데 줄. 4층 복도는 Y -375..-235이고 2·3층도 같은 폭이다. */
	constexpr float CorridorY = -305.0f;
	/**
	 * 문짝 앞면(Y -235)에서 한 걸음 떨어진 자리. 기는 몸(반지름 34 cm)이 문짝과
	 * 문틀에 걸리지 않고 귀를 대는 거리다.
	 */
	constexpr float DoorFrontY = -280.0f;
	/** 세대 문 자리. 4층 401·402와 2·3층의 같은 줄. */
	constexpr float WestUnitDoorX = -150.0f;
	constexpr float MiddleUnitDoorX = -30.0f;
	/**
	 * 계단탑 층 참의 가운데. 두 계단 사이 벽(X -470..-460)이 층 참 바로 위(Y -235)에서
	 * 끝나서, 계단 끝 점끼리 곧게 오가면 몸이 벽 끝에 걸린다. 계단과 출입구 사이는 이
	 * 점을 지난다.
	 */
	constexpr float LandingCenterX = -465.0f;
	constexpr float LandingCenterY = -305.0f;
	/** 시야 검사를 하는 높이. 문턱과 걸레받이를 넘고, 낮은 가구는 막는다. */
	constexpr float SightHeight = 40.0f;
}

int32 FIGBuildingNav::Add(const FVector& Feet, const bool bOnStair, const bool bLookout)
{
	FIGBuildingNavNode& Node = Nodes.AddDefaulted_GetRef();
	Node.Feet = Feet;
	Node.Floor = FloorOfFeet(Feet.Z);
	Node.bOnStair = bOnStair;
	Node.bLookout = bLookout;
	return Nodes.Num() - 1;
}

void FIGBuildingNav::Link(const int32 A, const int32 B)
{
	if (Nodes.IsValidIndex(A) && Nodes.IsValidIndex(B) && A != B)
	{
		Nodes[A].Links.AddUnique(B);
		Nodes[B].Links.AddUnique(A);
	}
}

void FIGBuildingNav::LinkChain(const TArray<int32>& Chain)
{
	for (int32 Index = 1; Index < Chain.Num(); ++Index)
	{
		Link(Chain[Index - 1], Chain[Index]);
	}
}

void FIGBuildingNav::Build()
{
	using namespace IGBuildingNav;
	Nodes.Reset();

	// 계단탑. 층마다 출입구와 층 참 가운데 점을 두고, 계단 끝 점은 그 가운데 점에 잇는다.
	int32 Doorways[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	int32 Landings[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	for (int32 Floor = 0; Floor < 4; ++Floor)
	{
		const float Z = AIGPrologueWorldScene::GetStoreyFloorZ(Floor);
		Doorways[Floor] = Add(AIGPrologueWorldScene::GetStairDoorwayFeet(Floor), false);
		Landings[Floor] = Add(FVector(LandingCenterX, LandingCenterY, Z), false);
		Link(Doorways[Floor], Landings[Floor]);
	}
	for (int32 Floor = 0; Floor < 3; ++Floor)
	{
		TArray<FVector> Climb;
		AIGPrologueWorldScene::GetStairClimbFeet(Floor, Climb);
		// 0과 마지막은 출입구, 1과 끝에서 둘째는 층 참 위 계단 끝이다. 그 사이가 계단과 반 층 참이다.
		int32 Previous = Landings[Floor];
		for (int32 Index = 1; Index + 1 < Climb.Num(); ++Index)
		{
			const bool bLanding = Index == 1 || Index + 2 == Climb.Num();
			const int32 Step = Add(Climb[Index], !bLanding);
			Link(Previous, Step);
			Previous = Step;
		}
		Link(Previous, Landings[Floor + 1]);
	}

	// 문 앞 점은 복도 줄에 끼우지 않고 곁가지로 단다. 줄에 끼우면 복도를 지나갈
	// 때마다 문 앞마다 들렀다 간다.
	const auto Spur = [this](const int32 Corridor, const FVector& Feet, const bool bLookout)
	{
		const int32 Node = Add(Feet, false, bLookout);
		Link(Corridor, Node);
		return Node;
	};

	// 4층. 목에서 승강기 앞까지 복도 한 줄.
	{
		const float Z = AIGPrologueWorldScene::FourthFloorZ;
		const float HomeFrontX = AIGPrologueWorldScene::HomeDoorX
			+ AIGPrologueWorldScene::WideDoorLeafWidth * 0.5f;
		const int32 West = Add(FVector(-180.0f, CorridorY, Z), false);
		const int32 Middle = Add(FVector(-30.0f, CorridorY, Z), false);
		const int32 Home = Add(FVector(HomeFrontX, CorridorY, Z), false);
		const int32 East = Add(FVector(300.0f, CorridorY, Z), false);
		const int32 Cupboard = Add(FVector(540.0f, CorridorY, Z), false);
		const int32 LiftFront = Add(FVector(680.0f, CorridorY, Z), false, true);
		LinkChain({Doorways[3], West, Middle, Home, East, Cupboard, LiftFront});
		Spur(West, FVector(WestUnitDoorX, DoorFrontY, Z), true);
		Spur(Middle, FVector(MiddleUnitDoorX, DoorFrontY, Z), true);
		Spur(Home, FVector(HomeFrontX, DoorFrontY, Z), true);
		Spur(Cupboard, FVector(580.0f, DoorFrontY, Z), true);
	}

	// 2층과 3층. 들어갈 수 있는 집은 층마다 하나다(201호 창고, 302호 빈집).
	for (int32 Floor = 1; Floor <= 2; ++Floor)
	{
		const float Z = AIGPrologueWorldScene::GetStoreyFloorZ(Floor);
		const int32 West = Add(FVector(-150.0f, CorridorY, Z), false);
		const int32 Middle = Add(FVector(-30.0f, CorridorY, Z), false);
		const int32 East = Add(FVector(100.0f, CorridorY, Z), false);
		const int32 EastMid = Add(FVector(375.0f, CorridorY, Z), false);
		const int32 LiftFront = Add(FVector(680.0f, CorridorY, Z), false, true);
		LinkChain({Doorways[Floor], West, Middle, East, EastMid, LiftFront});
		const int32 WestDoor = Spur(West, FVector(WestUnitDoorX, DoorFrontY, Z), Floor == 2);
		const int32 MiddleDoor = Spur(Middle, FVector(MiddleUnitDoorX, DoorFrontY, Z), Floor == 1);
		if (Floor == 1)
		{
			// 201호. 문 없는 문틀 너머에 석고보드와 비닐이 쌓여 있다.
			const int32 Threshold = Add(FVector(WestUnitDoorX, -222.0f, Z), false);
			const int32 Inside = Add(FVector(-118.0f, -110.0f, Z), false, true);
			LinkChain({WestDoor, Threshold, Inside});
		}
		else
		{
			// 302호. 장롱만 남은 빈집.
			const int32 Threshold = Add(FVector(MiddleUnitDoorX, -222.0f, Z), false);
			const int32 Inside = Add(FVector(10.0f, -70.0f, Z), false, true);
			LinkChain({MiddleDoor, Threshold, Inside});
		}
	}

	// 1층. 계단탑 출입구에서 필로티를 지나 연결통로, 관리실, 로비와 승강기 앞까지.
	{
		const int32 Bay = Add(FVector(-230.0f, -300.0f, 0.0f), false);
		const int32 ConnectorWest = Add(FVector(-40.0f, -305.0f, 0.0f), false);
		const int32 Booth = Add(FVector(165.0f, -305.0f, 0.0f), false);
		const int32 ConnectorEast = Add(FVector(330.0f, -305.0f, 0.0f), false);
		const int32 LobbyDoor = Add(FVector(440.0f, -305.0f, 0.0f), false);
		const int32 Lobby = Add(FVector(560.0f, -290.0f, 0.0f), false, true);
		const int32 LiftFront = Add(FVector(680.0f, -305.0f, 0.0f), false, true);
		LinkChain({Doorways[0], Bay, ConnectorWest, Booth, ConnectorEast, LobbyDoor, Lobby, LiftFront});
		// 관리실. 문턱을 넘어 책상 앞까지. 안쪽 방 문은 열리지 않는다.
		const int32 BoothThreshold = Add(FVector(165.0f, -232.0f, 0.0f), false);
		const int32 BoothInside = Add(FVector(165.0f, -185.0f, 0.0f), false, true);
		LinkChain({Booth, BoothThreshold, BoothInside});
	}
}

int32 FIGBuildingNav::FloorOfFeet(const float FeetZ)
{
	return FMath::Clamp(FMath::FloorToInt((FeetZ + 100.0f) / 300.0f), 0, 4);
}

bool FIGBuildingNav::IsInStairCore(const FVector& Feet)
{
	return AIGPrologueWorldScene::IsInsideStairCore(Feet);
}

int32 FIGBuildingNav::FindNearest(const UWorld* World, const FVector& Feet, const AActor* Ignore) const
{
	const int32 Floor = FloorOfFeet(Feet.Z);
	const bool bInCore = IsInStairCore(Feet);
	int32 BestSeen = INDEX_NONE;
	float BestSeenDistance = TNumericLimits<float>::Max();
	int32 BestAny = INDEX_NONE;
	float BestAnyDistance = TNumericLimits<float>::Max();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IGBuildingNavSight), false, Ignore);
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const FIGBuildingNavNode& Node = Nodes[Index];
		// 계단탑 밖에서는 같은 층의 점만, 안에서는 계단 점도 본다. 높이가 1 m 넘게
		// 다른 계단 점은 위아래 계단이라 벽 너머다.
		if (bInCore)
		{
			if (FMath::Abs(Node.Feet.Z - Feet.Z) > 100.0f)
			{
				continue;
			}
		}
		else if (Node.Floor != Floor || Node.bOnStair)
		{
			continue;
		}
		const float Distance = FVector::Dist(Node.Feet, Feet);
		if (Distance < BestAnyDistance)
		{
			BestAnyDistance = Distance;
			BestAny = Index;
		}
		if (Distance >= BestSeenDistance || !World)
		{
			continue;
		}
		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(
			Hit,
			Feet + FVector(0.0f, 0.0f, IGBuildingNav::SightHeight),
			Node.Feet + FVector(0.0f, 0.0f, IGBuildingNav::SightHeight),
			ECC_Visibility,
			Params)
			&& !Cast<APawn>(Hit.GetActor());
		if (!bBlocked)
		{
			BestSeenDistance = Distance;
			BestSeen = Index;
		}
	}
	return BestSeen != INDEX_NONE ? BestSeen : BestAny;
}

bool FIGBuildingNav::FindPath(const int32 From, const int32 To, TArray<int32>& OutPath) const
{
	OutPath.Reset();
	if (!Nodes.IsValidIndex(From) || !Nodes.IsValidIndex(To))
	{
		return false;
	}
	TArray<float> Cost;
	TArray<int32> Previous;
	TArray<bool> Done;
	Cost.Init(TNumericLimits<float>::Max(), Nodes.Num());
	Previous.Init(INDEX_NONE, Nodes.Num());
	Done.Init(false, Nodes.Num());
	Cost[From] = 0.0f;
	for (int32 Pass = 0; Pass < Nodes.Num(); ++Pass)
	{
		int32 Best = INDEX_NONE;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (!Done[Index] && Cost[Index] < TNumericLimits<float>::Max()
				&& (Best == INDEX_NONE || Cost[Index] < Cost[Best]))
			{
				Best = Index;
			}
		}
		if (Best == INDEX_NONE || Best == To)
		{
			break;
		}
		Done[Best] = true;
		for (const int32 Next : Nodes[Best].Links)
		{
			const float Candidate = Cost[Best] + FVector::Dist(Nodes[Best].Feet, Nodes[Next].Feet);
			if (Candidate < Cost[Next])
			{
				Cost[Next] = Candidate;
				Previous[Next] = Best;
			}
		}
	}
	if (Cost[To] == TNumericLimits<float>::Max())
	{
		return false;
	}
	for (int32 At = To; At != INDEX_NONE; At = Previous[At])
	{
		OutPath.Insert(At, 0);
	}
	return OutPath.Num() > 0 && OutPath[0] == From;
}

void FIGBuildingNav::GatherWithin(
	const int32 From,
	const float MaxDistance,
	TArray<TPair<int32, float>>& OutNodes) const
{
	OutNodes.Reset();
	if (!Nodes.IsValidIndex(From))
	{
		return;
	}
	TArray<float> Cost;
	Cost.Init(TNumericLimits<float>::Max(), Nodes.Num());
	TArray<int32> Open;
	Cost[From] = 0.0f;
	Open.Add(From);
	while (Open.Num() > 0)
	{
		int32 BestSlot = 0;
		for (int32 Slot = 1; Slot < Open.Num(); ++Slot)
		{
			if (Cost[Open[Slot]] < Cost[Open[BestSlot]])
			{
				BestSlot = Slot;
			}
		}
		const int32 Current = Open[BestSlot];
		Open.RemoveAtSwap(BestSlot);
		OutNodes.Emplace(Current, Cost[Current]);
		for (const int32 Next : Nodes[Current].Links)
		{
			const float Candidate = Cost[Current] + FVector::Dist(Nodes[Current].Feet, Nodes[Next].Feet);
			if (Candidate <= MaxDistance && Candidate < Cost[Next])
			{
				const bool bWasOpen = Cost[Next] < TNumericLimits<float>::Max();
				Cost[Next] = Candidate;
				if (!bWasOpen)
				{
					Open.Add(Next);
				}
			}
		}
	}
}
