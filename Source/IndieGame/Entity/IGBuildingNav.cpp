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

	/**
	 * 기는 몸은 머리끝에서 발끝까지 190 cm다. 반 층 참(깊이 140 cm)에서 서쪽 계단 끝에 선 채 돌면 엉덩이와 발이
	 * 서쪽 벽에 묻히고, 층 참 가운데서 돌면 머리가 남쪽 벽이나 계단 사이 벽 끝에 박힌다. 그래서 계단탑 안에서 꺾는
	 * 자리는 몸이 걸어가면서 조금씩 돌도록 원호를 따라간다. 원호는 15도마다 점을 둔다. 점마다 가는 쪽이 꺾이는
	 * 만큼 몸이 늦게 돌아서, 더 성기면 머리가 벽에 닿는다.
	 */
	constexpr float ArcStepDegrees = 15.0f;
	/**
	 * 반 층 참은 두 계단 가운데선을 그대로 잇는 반원이다. 중심이 두 계단 사이 벽의 연장선(X -465)이라 동쪽을 볼
	 * 때 몸이 참 폭 가운데에 오고, 비스듬할 때 발끝은 서쪽 벽, 머리는 북쪽 벽 앞 몇 cm에서 멈춘다. 계단에서
	 * 원호로 옆걸음 없이 들어서야 앞으로 뻗은 손이 벽 쪽으로 쏠리지 않는다.
	 */
	constexpr float TurnArcCenterY = -5.0f;
	constexpr float TurnArcRadius = 57.5f;
	/**
	 * 2·3층 참에서 올라온 계단(동쪽)과 올라갈 계단(서쪽)을 잇는 반원. 반 층 참과 같은 반원을 남쪽으로 뒤집었다.
	 * 예전에는 층 참 가운데를 들러 거의 되돌아서, 머리가 동쪽 벽 모서리(X -355, Y -235)나 계단 사이 벽 끝에 박혔다.
	 */
	constexpr float LandingArcCenterY = -258.0f;
	/**
	 * 층 참 가운데에서 서쪽(올라가는) 계단으로 드는 사분원. 서쪽을 보고 가다 북쪽으로 꺾는다.
	 */
	constexpr float WestTurnRadius = 50.0f;
	/**
	 * 출입구에서 들어와 동쪽 계단(아래층으로 내려가는 계단) 머리로 바로 꺾는 사분원. 중심이 동쪽 벽 모서리
	 * (X -355, Y -235) 가까이에 있어, 도는 동안 엉덩이는 복도 쪽으로, 머리는 계단 쪽으로 빠진다.
	 */
	constexpr float DoorwayArcCenterX = -337.5f;
	constexpr float DoorwayArcCenterY = -235.0f;
	constexpr float DoorwayArcRadius = 70.0f;
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

int32 FIGBuildingNav::AddArc(const int32 From, const FVector& Center, const float Radius, const float StartDegrees,
	const float EndDegrees, const bool bOnStair)
{
	// 양 끝은 이을 점이 따로 있다. 그 사이만 15도마다 둔다.
	const int32 Count = FMath::Max(1, FMath::RoundToInt(FMath::Abs(EndDegrees - StartDegrees) / IGBuildingNav::ArcStepDegrees));
	int32 Previous = From;
	for (int32 Segment = 1; Segment < Count; ++Segment)
	{
		const float Angle = FMath::DegreesToRadians(FMath::Lerp(StartDegrees, EndDegrees, static_cast<float>(Segment) / Count));
		const int32 Point = Add(Center + FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.0f), bOnStair);
		Link(Previous, Point);
		Previous = Point;
	}
	return Previous;
}

void FIGBuildingNav::Build()
{
	using namespace IGBuildingNav;
	Nodes.Reset();

	// 계단탑. 층마다 출입구와 층 참 가운데 점을 둔다. 계단 끝 점은 아래의 원호들로 잇는다.
	int32 Doorways[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	int32 Landings[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	for (int32 Floor = 0; Floor < 4; ++Floor)
	{
		const float Z = AIGPrologueWorldScene::GetStoreyFloorZ(Floor);
		Doorways[Floor] = Add(AIGPrologueWorldScene::GetStairDoorwayFeet(Floor), false);
		Landings[Floor] = Add(FVector(LandingCenterX, LandingCenterY, Z), false);
		Link(Doorways[Floor], Landings[Floor]);
	}
	// 층마다 서쪽(올라가는) 계단이 시작하는 점과 동쪽(올라온) 계단이 끝나는 점.
	int32 WestStarts[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	int32 EastEnds[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	for (int32 Floor = 0; Floor < 3; ++Floor)
	{
		TArray<FVector> Climb;
		AIGPrologueWorldScene::GetStairClimbFeet(Floor, Climb);
		check(Climb.Num() == 10);
		// 0과 마지막은 출입구, 1과 끝에서 둘째는 층 참 위 계단 끝이다. 그 사이가 계단과 반 층 참이고, 4와 5가
		// 반 층 참에서 꺾는 두 점이다. 그 두 점 대신 반원을 잇는다.
		const float FloorZ = Climb[1].Z;
		const float HalfZ = Climb[4].Z;
		int32 Previous = INDEX_NONE;
		for (int32 Index = 1; Index + 1 < Climb.Num(); ++Index)
		{
			if (Index == 5)
			{
				continue;
			}
			if (Index == 4)
			{
				const FVector Center(LandingCenterX, TurnArcCenterY, HalfZ);
				const int32 West = Add(Center + FVector(-TurnArcRadius, 0.0f, 0.0f), true);
				Link(Previous, West);
				const int32 East = Add(Center + FVector(TurnArcRadius, 0.0f, 0.0f), true);
				Link(AddArc(West, Center, TurnArcRadius, 180.0f, 0.0f, true), East);
				Previous = East;
				continue;
			}
			const bool bLanding = Index == 1 || Index + 2 == Climb.Num();
			const int32 Step = Add(Climb[Index], !bLanding);
			if (Index == 1)
			{
				// 층 참 가운데에서 서쪽을 보고 가다 북쪽으로 꺾어 계단에 든다.
				WestStarts[Floor] = Step;
				const FVector Center(Climb[1].X + WestTurnRadius, Climb[1].Y, FloorZ);
				const int32 Entry = Add(Center + FVector(0.0f, -WestTurnRadius, 0.0f), false);
				Link(Landings[Floor], Entry);
				Link(AddArc(Entry, Center, WestTurnRadius, -90.0f, -180.0f, false), Step);
			}
			else
			{
				Link(Previous, Step);
			}
			Previous = Step;
			// 동쪽 계단 머리(위층 참 바로 앞 디딤판). 위층 출입구에서 사분원으로 바로 잇는다.
			if (Index == 7)
			{
				const FVector Center(DoorwayArcCenterX, DoorwayArcCenterY, Nodes[Doorways[Floor + 1]].Feet.Z);
				const int32 Entry = Add(Center + FVector(0.0f, -DoorwayArcRadius, 0.0f), false);
				Link(Doorways[Floor + 1], Entry);
				Link(AddArc(Entry, Center, DoorwayArcRadius, -90.0f, -180.0f, false), Step);
			}
		}
		EastEnds[Floor + 1] = Previous;
	}
	// 2·3층 참은 올라온 계단과 올라갈 계단을 남쪽 반원으로 잇는다. 4층 참은 위로 가는 계단이 없어 가운데 점에 잇는다.
	for (int32 Floor = 1; Floor < 4; ++Floor)
	{
		if (!Nodes.IsValidIndex(WestStarts[Floor]))
		{
			Link(EastEnds[Floor], Landings[Floor]);
			continue;
		}
		const FVector Center(LandingCenterX, LandingArcCenterY, Nodes[Landings[Floor]].Feet.Z);
		Link(AddArc(WestStarts[Floor], Center, TurnArcRadius, 180.0f, 360.0f, false), EastEnds[Floor]);
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
