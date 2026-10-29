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
	 * 계단탑 층 참에서 출입구와 서쪽 계단 사이를 오가는 길이 갈라지는 자리. 참 가운데보다 출입구 쪽이다. 참
	 * 가운데(X -465)에서 서쪽을 보고 서면 손끝이 서쪽 벽 앞 7 cm라 거기서는 돌 수가 없다.
	 */
	constexpr float LandingJunctionX = -400.0f;
	constexpr float LandingJunctionY = -305.0f;
	/** 시야 검사를 하는 높이. 문턱과 걸레받이를 넘고, 낮은 가구는 막는다. */
	constexpr float SightHeight = 40.0f;
	/** 두 계단 사이 벽의 가운데. 반 층 참에서 도는 길을 여기를 축으로 반 바퀴 돌리면 층 참에서 도는 길이다. */
	constexpr float StairCoreMidX = -465.0f;
	constexpr float StairCoreMidY = -135.0f;

	// 계단참에서 도는 자세(X, Y, Yaw). 기는 몸은 손끝에서 발끝까지 2 m(앞 103 cm, 뒤 97 cm)이고 왼팔이 옆으로 39 cm
	// 나온다(-IGClipAuditTrace로 잰 둘레). 깊이 140 cm 참에서 몸이 가는 쪽만 보며 원호로 돌면 바깥쪽 팔과 손끝이
	// 벽에 10 cm 넘게 묻혔다. 그래서 참마다 몸 가운데 자리와 방향을 2.5도, 2.5 cm 간격으로 다 훑어 벽과 계단 사이
	// 벽 끝에서 가장 멀어지는 길을 고르고, 10도마다 남겼다. 반 층 참에서는 비스듬히 참으로 나와서 참 가운데에서
	// 제자리로 40~50도 돌고, 다시 비스듬히 다음 계단으로 든다. 둘레에서 벽까지는 3 cm 넘게 남는다. 몸이 계단과
	// 45도 넘게 틀어져 있을 때 옆으로 나온 팔과 옆구리는 오르는 계단 첫 단 위로 5 cm 넘게 나가지 않는다. 경사대로
	// 눕혀도 그 자리는 디딤판 속이라서다. 오르는 계단으로 들 때는 30 cm 앞에서 계단을 보고 서서 몸이 미리 눕는다.

	/** 반 층 참. 동쪽 계단 끝 단(-407.5, -35)에서 북쪽을 보고 올라와 서쪽 계단 끝 단(-522.5, -35)으로 내려간다. */
	const FVector HalfLandingDown[] = {
		FVector(-412.5f, -27.0f, 110.0f), FVector(-418.0f, -16.5f, 120.0f), FVector(-434.5f, 9.0f, 130.0f),
		FVector(-447.0f, 23.0f, 140.0f), FVector(-452.5f, 20.0f, 150.0f), FVector(-457.0f, 18.0f, 160.0f),
		FVector(-461.0f, 16.5f, 170.0f), FVector(-463.5f, 15.5f, 210.0f), FVector(-476.0f, 9.5f, 220.0f),
		FVector(-487.5f, 3.5f, 230.0f), FVector(-496.0f, -4.5f, 240.0f), FVector(-503.5f, -11.5f, 250.0f),
		FVector(-511.5f, -20.5f, 260.0f), FVector(-522.5f, -35.0f, 270.0f)};
	/** 반 층 참. 서쪽 계단 끝 단에서 북쪽을 보고 올라와 동쪽 계단 끝 단 30 cm 앞(-407.5, -5)에서 남쪽을 본다. */
	const FVector HalfLandingUp[] = {
		FVector(-507.5f, -17.0f, 80.0f), FVector(-496.0f, -5.0f, 70.0f), FVector(-486.5f, 2.0f, 60.0f),
		FVector(-477.0f, 9.5f, 50.0f), FVector(-472.0f, 13.0f, 40.0f), FVector(-466.0f, 16.5f, -10.0f),
		FVector(-459.5f, 21.0f, -20.0f), FVector(-453.0f, 26.5f, -30.0f), FVector(-442.5f, 34.0f, -40.0f),
		FVector(-438.5f, 23.5f, -50.0f), FVector(-426.5f, 8.0f, -60.0f), FVector(-413.5f, -3.5f, -70.0f),
		FVector(-406.5f, -4.0f, -80.0f), FVector(-407.5f, -5.0f, -90.0f)};
	/** 층 참. 갈림 자리(-400, -305)에서 서쪽을 보고 와서 서쪽 계단 첫 단 30 cm 앞(-522.5, -265)에서 북쪽을 본다. */
	const FVector JunctionToWestFlight[] = {
		FVector(-400.0f, -305.0f, 180.0f), FVector(-412.5f, -303.5f, 170.0f), FVector(-425.5f, -303.0f, 160.0f),
		FVector(-438.5f, -303.5f, 150.0f), FVector(-452.5f, -304.0f, 140.0f), FVector(-472.0f, -294.0f, 130.0f),
		FVector(-486.5f, -285.5f, 120.0f), FVector(-498.0f, -279.0f, 110.0f), FVector(-509.0f, -272.5f, 100.0f),
		FVector(-522.5f, -265.0f, 90.0f)};
	/** 층 참. 서쪽 계단 첫 단(-522.5, -235)에서 남쪽을 보고 내려와 갈림 자리로 동쪽을 보고 나간다. */
	const FVector WestFlightToJunction[] = {
		FVector(-519.5f, -251.0f, 280.0f), FVector(-513.0f, -263.0f, 290.0f), FVector(-504.5f, -272.0f, 300.0f),
		FVector(-483.5f, -280.5f, 310.0f), FVector(-463.5f, -291.5f, 320.0f), FVector(-439.0f, -296.0f, 330.0f),
		FVector(-416.0f, -301.5f, 340.0f), FVector(-407.0f, -303.5f, 350.0f), FVector(-400.0f, -305.0f, 360.0f)};
	/** 층 참. 출입구에서 서쪽을 보고 들어와 동쪽 계단 머리(-407.5, -235)로 북쪽을 보고 내려간다. */
	const FVector DoorwayToEastFlight[] = {
		FVector(-337.5f, -305.0f, 180.0f), FVector(-350.5f, -297.0f, 150.0f), FVector(-364.0f, -288.5f, 140.0f),
		FVector(-377.0f, -280.5f, 130.0f), FVector(-388.5f, -271.5f, 120.0f), FVector(-397.5f, -260.5f, 110.0f),
		FVector(-403.0f, -248.0f, 100.0f), FVector(-407.5f, -235.0f, 90.0f)};
	/** 층 참. 동쪽 계단 머리에서 남쪽을 보고 올라와 출입구로 동쪽을 보고 나간다. */
	const FVector EastFlightToDoorway[] = {
		FVector(-406.5f, -247.0f, 280.0f), FVector(-402.0f, -249.5f, 290.0f), FVector(-398.5f, -251.0f, 300.0f),
		FVector(-394.5f, -253.0f, 310.0f), FVector(-384.5f, -267.0f, 320.0f), FVector(-375.0f, -282.5f, 330.0f),
		FVector(-365.0f, -291.5f, 340.0f), FVector(-349.0f, -304.0f, 350.0f), FVector(-337.5f, -305.0f, 360.0f)};

	// 층 참에서 되돌아서는 자세. 출입구(-305, -305)에서 복도를 보던 몸이 계단 쪽으로 돌아서려면 깊이 140 cm 참에서
	// 2 m 몸을 반 바퀴 돌려야 한다. 제자리에서 돌면 손끝과 발끝이 목 기둥(Y -245, -365)과 남벽, 열린 방화문을 쓸었다.
	// 위의 자세들처럼 자리와 방향을 다 훑되, 목 기둥과 남벽 홈, 복도 벽, 열린 문짝까지 넣고 한 칸에 10 cm까지 옮길 수
	// 있게 해서 골랐다. 서쪽으로 물러나며 30~40도 틀고, 북쪽 계단 위로 머리를 내밀며 마저 돈다. 둘레에서 벽까지
	// 1 cm 넘게 남는다.
	const FVector DoorToJunctionTurn[] = {
		FVector(-305.0f, -305.0f, 0.0f), FVector(-422.5f, -305.0f, 30.0f), FVector(-430.0f, -305.0f, 32.5f),
		FVector(-435.0f, -305.0f, 42.5f), FVector(-417.5f, -280.0f, 67.5f), FVector(-410.0f, -275.0f, 82.5f),
		FVector(-387.5f, -280.0f, 127.5f), FVector(-370.0f, -292.5f, 145.0f), FVector(-400.0f, -305.0f, 180.0f)};
	const FVector DoorToEastFlightTurn[] = {
		FVector(-305.0f, -305.0f, 0.0f), FVector(-422.5f, -305.0f, 30.0f), FVector(-430.0f, -305.0f, 32.5f),
		FVector(-435.0f, -305.0f, 42.5f), FVector(-407.5f, -235.0f, 90.0f)};
	const FVector JunctionToDoorTurn[] = {
		FVector(-400.0f, -305.0f, 180.0f), FVector(-370.0f, -290.0f, 145.0f), FVector(-395.0f, -277.5f, 120.0f),
		FVector(-410.0f, -275.0f, 77.5f), FVector(-422.5f, -285.0f, 60.0f), FVector(-435.0f, -305.0f, 42.5f),
		FVector(-430.0f, -305.0f, 32.5f), FVector(-305.0f, -305.0f, 0.0f)};
	const FVector JunctionToEastFlightTurn[] = {
		FVector(-400.0f, -305.0f, 180.0f), FVector(-375.0f, -295.0f, 147.5f), FVector(-370.0f, -287.5f, 145.0f),
		FVector(-407.5f, -235.0f, 90.0f)};
}

TConstArrayView<FVector> FIGBuildingNav::GetLandingUTurn(const ELandingUTurn Turn)
{
	using namespace IGBuildingNav;
	switch (Turn)
	{
	case ELandingUTurn::DoorToJunction: return MakeArrayView(DoorToJunctionTurn);
	case ELandingUTurn::DoorToEastFlight: return MakeArrayView(DoorToEastFlightTurn);
	case ELandingUTurn::JunctionToDoor: return MakeArrayView(JunctionToDoorTurn);
	case ELandingUTurn::JunctionToEastFlight: return MakeArrayView(JunctionToEastFlightTurn);
	default: return TConstArrayView<FVector>();
	}
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

void FIGBuildingNav::AddTurn(const int32 From, const int32 To, const TConstArrayView<FVector> Poses, const float Z,
	const bool bHalfTurn)
{
	int32 Previous = From;
	for (const FVector& Pose : Poses)
	{
		const FVector Feet = bHalfTurn
			? FVector(2.0f * IGBuildingNav::StairCoreMidX - Pose.X, 2.0f * IGBuildingNav::StairCoreMidY - Pose.Y, Z)
			: FVector(Pose.X, Pose.Y, Z);
		const int32 Point = Add(Feet, false);
		Nodes[Point].bPose = true;
		Nodes[Point].Yaw = FRotator::NormalizeAxis(bHalfTurn ? Pose.Z + 180.0f : Pose.Z);
		if (Nodes.IsValidIndex(Previous))
		{
			Nodes[Previous].Links.AddUnique(Point);
		}
		Previous = Point;
	}
	if (Nodes.IsValidIndex(Previous) && Nodes.IsValidIndex(To))
	{
		Nodes[Previous].Links.AddUnique(To);
	}
}

void FIGBuildingNav::Build()
{
	using namespace IGBuildingNav;
	Nodes.Reset();

	// 계단탑. 층마다 출입구와 층 참 갈림 자리를 둔다.
	int32 Doorways[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	int32 Junctions[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	for (int32 Floor = 0; Floor < 4; ++Floor)
	{
		const float Z = AIGPrologueWorldScene::GetStoreyFloorZ(Floor);
		Doorways[Floor] = Add(AIGPrologueWorldScene::GetStairDoorwayFeet(Floor), false);
		Junctions[Floor] = Add(FVector(LandingJunctionX, LandingJunctionY, Z), false);
		Link(Doorways[Floor], Junctions[Floor]);
	}
	// 층마다 서쪽(올라가는) 계단 첫 단과 동쪽(내려가는) 계단 머리. 계단참에서 도는 길은 이 점들 사이에 단다.
	int32 WestFeet[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	int32 EastHeads[4] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	for (int32 Floor = 0; Floor < 3; ++Floor)
	{
		TArray<FVector> Climb;
		AIGPrologueWorldScene::GetStairClimbFeet(Floor, Climb);
		check(Climb.Num() == 10);
		// 0과 9는 출입구, 1과 8은 층 참 위 계단 앞, 2·3은 서쪽 계단 첫 단과 끝 단, 6·7은 동쪽 계단 끝 단과 머리다.
		// 4와 5는 반 층 참에서 꺾는 자리라 쓰지 않는다. 반 층 참은 자세 점으로 돈다. 1과 8(계단 앞 층 참)도 두지
		// 않는다. 계단 끝 단에서 곁가지로 달면 거기까지 갔다가 돌아서느라 제자리에서 반 바퀴 돈다.
		const int32 WestFoot = Add(Climb[2], true);
		const int32 WestTop = Add(Climb[3], true);
		const int32 EastTop = Add(Climb[6], true);
		const int32 EastHead = Add(Climb[7], true);
		Link(WestFoot, WestTop);
		Link(EastTop, EastHead);
		AddTurn(EastTop, WestTop, MakeArrayView(HalfLandingDown), Climb[4].Z, false);
		AddTurn(WestTop, EastTop, MakeArrayView(HalfLandingUp), Climb[4].Z, false);
		WestFeet[Floor] = WestFoot;
		EastHeads[Floor + 1] = EastHead;
		// 4층 참은 위로 가는 계단이 없다. 계단을 다 올라온 자리를 거쳐 갈림 자리에 잇는다.
		if (Floor + 1 == 3)
		{
			const int32 EastEnd = Add(Climb[8], false);
			LinkChain({EastHead, EastEnd, Junctions[3]});
		}
	}
	// 층 참. 서쪽 계단은 갈림 자리와, 동쪽 계단은 출입구와 잇는다. 2·3층에서는 내려온 계단과 내려갈 계단을 반 층
	// 참과 같은 길을 반 바퀴 돌려 잇는다.
	for (int32 Floor = 0; Floor < 4; ++Floor)
	{
		const float Z = AIGPrologueWorldScene::GetStoreyFloorZ(Floor);
		const bool bWest = Nodes.IsValidIndex(WestFeet[Floor]);
		const bool bEast = Nodes.IsValidIndex(EastHeads[Floor]);
		if (bWest)
		{
			AddTurn(Junctions[Floor], WestFeet[Floor], MakeArrayView(JunctionToWestFlight), Z, false);
			AddTurn(WestFeet[Floor], Junctions[Floor], MakeArrayView(WestFlightToJunction), Z, false);
		}
		if (bEast)
		{
			AddTurn(Doorways[Floor], EastHeads[Floor], MakeArrayView(DoorwayToEastFlight), Z, false);
			AddTurn(EastHeads[Floor], Doorways[Floor], MakeArrayView(EastFlightToDoorway), Z, false);
		}
		if (bWest && bEast)
		{
			AddTurn(WestFeet[Floor], EastHeads[Floor], MakeArrayView(HalfLandingDown), Z, true);
			AddTurn(EastHeads[Floor], WestFeet[Floor], MakeArrayView(HalfLandingUp), Z, true);
		}
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
		// 도는 자세 점은 앞 점에서만 들어간다. 길 가운데에서 시작하면 반대로 돌게 된다.
		if (Node.bPose)
		{
			continue;
		}
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
