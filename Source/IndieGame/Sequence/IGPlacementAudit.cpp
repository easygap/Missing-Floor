#include "Sequence/IGPlacementAudit.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "IndieGame.h"
#if WITH_EDITOR
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#endif

#include <algorithm>

namespace IGPlacementAuditPrivate
{
	/** 한 메시의 원본 삼각형(로컬 좌표). */
	struct FMeshData
	{
		TArray<FVector> Positions;
		TArray<FIntVector> Triangles;
		/** 한 면에만 붙은 모서리가 2% 이하면 닫힌 메시로 본다. 열린 메시는 안팎을 가를 수 없다. */
		bool bClosed = false;
	};

	struct FNode
	{
		FBox Box = FBox(ForceInit);
		int32 Left = INDEX_NONE;
		int32 Right = INDEX_NONE;
		int32 First = 0;
		int32 Count = 0;
	};

	/** 월드에 놓인 부품 하나. 삼각형은 처음 물어볼 때 월드 좌표로 옮기고 BVH를 짓는다. */
	struct FItem
	{
		UStaticMeshComponent* Component = nullptr;
		/** 부품의 월드 변환. 인스턴스 메시는 인스턴스마다 하나씩 부품을 만든다. */
		FTransform Transform;
		FString Name;
		FString MeshName;
		const AActor* Owner = nullptr;
		FBox Box = FBox(ForceInit);
		TSharedPtr<FMeshData> Mesh;
		TArray<FVector> World;
		TArray<int32> Order;
		TArray<FNode> Nodes;
		bool bStructure = false;
		bool bBuilt = false;
	};

	TSharedPtr<FMeshData> Load(const UStaticMesh* Mesh)
	{
		TSharedPtr<FMeshData> Out = MakeShared<FMeshData>();
#if WITH_EDITOR
		const FMeshDescription* Description = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
		if (!Description)
		{
			return Out;
		}
		FStaticMeshConstAttributes Attributes(*Description);
		const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TMap<FVertexID, int32> Remap;
		Remap.Reserve(Description->Vertices().Num());
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			Remap.Add(Vertex, Out->Positions.Add(FVector(Positions[Vertex])));
		}
		TMap<uint64, int32> Edges;
		Out->Triangles.Reserve(Description->Triangles().Num());
		for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexID> Corners = Description->GetTriangleVertices(Triangle);
			const FIntVector Tri(Remap[Corners[0]], Remap[Corners[1]], Remap[Corners[2]]);
			Out->Triangles.Add(Tri);
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				const uint64 A = static_cast<uint32>(Tri[Edge]);
				const uint64 B = static_cast<uint32>(Tri[(Edge + 1) % 3]);
				++Edges.FindOrAdd((FMath::Min(A, B) << 32) | FMath::Max(A, B));
			}
		}
		int32 Boundary = 0;
		for (const TPair<uint64, int32>& Edge : Edges)
		{
			Boundary += Edge.Value == 1 ? 1 : 0;
		}
		Out->bClosed = Edges.Num() > 0 && Boundary * 50 <= Edges.Num();
		// 빌드 설정의 축척 따위로 원본과 렌더 메시가 다르면 잴 수 없다고 본다.
		FBox Source(ForceInit);
		for (const FVector& Position : Out->Positions)
		{
			Source += Position;
		}
		const FBox Render = Mesh->GetBoundingBox();
		if (!Source.IsValid || (Source.GetSize() - Render.GetSize()).GetAbsMax() > FMath::Max(1.0, Render.GetSize().GetMax() * 0.05))
		{
			Out->Positions.Reset();
			Out->Triangles.Reset();
		}
#endif
		return Out;
	}

	void Build(FItem& Item)
	{
		if (Item.bBuilt)
		{
			return;
		}
		Item.bBuilt = true;
		const FTransform& Transform = Item.Transform;
		const FMeshData& Mesh = *Item.Mesh;
		Item.World.SetNumUninitialized(Mesh.Positions.Num());
		for (int32 Index = 0; Index < Mesh.Positions.Num(); ++Index)
		{
			Item.World[Index] = Transform.TransformPosition(Mesh.Positions[Index]);
		}
		const int32 TriangleCount = Mesh.Triangles.Num();
		TArray<FVector> Centers;
		Centers.SetNumUninitialized(TriangleCount);
		Item.Order.SetNumUninitialized(TriangleCount);
		for (int32 Index = 0; Index < TriangleCount; ++Index)
		{
			const FIntVector& Tri = Mesh.Triangles[Index];
			Item.Order[Index] = Index;
			Centers[Index] = (Item.World[Tri.X] + Item.World[Tri.Y] + Item.World[Tri.Z]) / 3.0;
		}
		FNode Root;
		Root.Count = TriangleCount;
		Item.Nodes.Add(Root);
		TArray<int32> Stack;
		Stack.Add(0);
		while (!Stack.IsEmpty())
		{
			const int32 NodeIndex = Stack.Pop(EAllowShrinking::No);
			const int32 First = Item.Nodes[NodeIndex].First;
			const int32 Count = Item.Nodes[NodeIndex].Count;
			FBox Box(ForceInit);
			FBox CenterBox(ForceInit);
			for (int32 Slot = First; Slot < First + Count; ++Slot)
			{
				const FIntVector& Tri = Mesh.Triangles[Item.Order[Slot]];
				Box += Item.World[Tri.X];
				Box += Item.World[Tri.Y];
				Box += Item.World[Tri.Z];
				CenterBox += Centers[Item.Order[Slot]];
			}
			Item.Nodes[NodeIndex].Box = Box.ExpandBy(0.01);
			if (Count <= 8)
			{
				continue;
			}
			const FVector Size = CenterBox.GetSize();
			const int32 Axis = Size.X >= Size.Y && Size.X >= Size.Z ? 0 : (Size.Y >= Size.Z ? 1 : 2);
			const int32 Middle = First + Count / 2;
			int32* Begin = Item.Order.GetData();
			std::nth_element(Begin + First, Begin + Middle, Begin + First + Count,
				[&Centers, Axis](const int32 A, const int32 B) { return Centers[A][Axis] < Centers[B][Axis]; });
			FNode Left;
			Left.First = First;
			Left.Count = Middle - First;
			FNode Right;
			Right.First = Middle;
			Right.Count = First + Count - Middle;
			const int32 LeftIndex = Item.Nodes.Add(Left);
			const int32 RightIndex = Item.Nodes.Add(Right);
			Item.Nodes[NodeIndex].Left = LeftIndex;
			Item.Nodes[NodeIndex].Right = RightIndex;
			Stack.Add(LeftIndex);
			Stack.Add(RightIndex);
		}
	}

	bool RayHitsBox(const FBox& Box, const FVector& Origin, const FVector& Direction, const double MaxT)
	{
		double Near = 0.0;
		double Far = MaxT;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::IsNearlyZero(Direction[Axis]))
			{
				if (Origin[Axis] < Box.Min[Axis] || Origin[Axis] > Box.Max[Axis])
				{
					return false;
				}
				continue;
			}
			double T0 = (Box.Min[Axis] - Origin[Axis]) / Direction[Axis];
			double T1 = (Box.Max[Axis] - Origin[Axis]) / Direction[Axis];
			if (T0 > T1)
			{
				Swap(T0, T1);
			}
			Near = FMath::Max(Near, T0);
			Far = FMath::Min(Far, T1);
			if (Near > Far)
			{
				return false;
			}
		}
		return true;
	}

	bool RayHitsTriangle(const FVector& Origin, const FVector& Direction,
		const FVector& A, const FVector& B, const FVector& C, double& OutT)
	{
		const FVector Edge1 = B - A;
		const FVector Edge2 = C - A;
		const FVector P = FVector::CrossProduct(Direction, Edge2);
		const double Determinant = FVector::DotProduct(Edge1, P);
		if (FMath::Abs(Determinant) < 1e-12)
		{
			return false;
		}
		const double Inverse = 1.0 / Determinant;
		const FVector S = Origin - A;
		const double U = FVector::DotProduct(S, P) * Inverse;
		if (U < 0.0 || U > 1.0)
		{
			return false;
		}
		const FVector Q = FVector::CrossProduct(S, Edge1);
		const double V = FVector::DotProduct(Direction, Q) * Inverse;
		if (V < 0.0 || U + V > 1.0)
		{
			return false;
		}
		OutT = FVector::DotProduct(Edge2, Q) * Inverse;
		return true;
	}

	template <typename FVisit>
	void Trace(const FItem& Item, const FVector& Origin, const FVector& Direction, const double MaxT, FVisit&& Visit)
	{
		if (Item.Nodes.IsEmpty())
		{
			return;
		}
		TArray<int32, TInlineAllocator<64>> Stack;
		Stack.Add(0);
		while (!Stack.IsEmpty())
		{
			const FNode& Node = Item.Nodes[Stack.Pop(EAllowShrinking::No)];
			if (!RayHitsBox(Node.Box, Origin, Direction, MaxT))
			{
				continue;
			}
			if (Node.Left != INDEX_NONE)
			{
				Stack.Add(Node.Left);
				Stack.Add(Node.Right);
				continue;
			}
			for (int32 Slot = Node.First; Slot < Node.First + Node.Count; ++Slot)
			{
				const FIntVector& Tri = Item.Mesh->Triangles[Item.Order[Slot]];
				double T = 0.0;
				if (RayHitsTriangle(Origin, Direction, Item.World[Tri.X], Item.World[Tri.Y], Item.World[Tri.Z], T)
					&& T >= 0.0 && T <= MaxT)
				{
					Visit(T);
				}
			}
		}
	}

	/** 닫힌 메시 안의 점인가. 세 방향 중 둘 이상에서 면을 홀수 번 지나면 안이다. */
	bool IsInside(const FItem& Item, const FVector& Point)
	{
		static const FVector Directions[3] = {
			FVector(1.0, 0.0131, 0.0213).GetSafeNormal(),
			FVector(0.0173, 1.0, 0.0117).GetSafeNormal(),
			FVector(0.0093, 0.0191, 1.0).GetSafeNormal() };
		const double MaxT = Item.Box.GetSize().Size() * 2.0 + 10.0;
		int32 Odd = 0;
		for (const FVector& Direction : Directions)
		{
			int32 Hits = 0;
			Trace(Item, Point, Direction, MaxT, [&Hits](const double T) { Hits += T > 1e-4 ? 1 : 0; });
			Odd += Hits & 1;
		}
		return Odd >= 2;
	}

	double DistanceToSurface(const FItem& Item, const FVector& Point)
	{
		double Best = TNumericLimits<double>::Max();
		TArray<int32, TInlineAllocator<64>> Stack;
		Stack.Add(0);
		while (!Stack.IsEmpty() && !Item.Nodes.IsEmpty())
		{
			const FNode& Node = Item.Nodes[Stack.Pop(EAllowShrinking::No)];
			if (Node.Box.ComputeSquaredDistanceToPoint(Point) >= Best)
			{
				continue;
			}
			if (Node.Left != INDEX_NONE)
			{
				Stack.Add(Node.Left);
				Stack.Add(Node.Right);
				continue;
			}
			for (int32 Slot = Node.First; Slot < Node.First + Node.Count; ++Slot)
			{
				const FIntVector& Tri = Item.Mesh->Triangles[Item.Order[Slot]];
				const FVector Closest = FMath::ClosestPointOnTriangleToPoint(
					Point, Item.World[Tri.X], Item.World[Tri.Y], Item.World[Tri.Z]);
				Best = FMath::Min(Best, FVector::DistSquared(Point, Closest));
			}
		}
		return Best == TNumericLimits<double>::Max() ? -1.0 : FMath::Sqrt(Best);
	}

	/**
	 * 두 부품이 실제로 맞닿았는가. 한쪽 꼭짓점이 다른 쪽 면에서 Tolerance 안이거나, 모서리가 다른 쪽 면을 지나면 붙었다.
	 * 찾은 가장 가까운 거리를 돌려주고, 모서리가 면을 지나면 0이다. 둘 다 Build를 마친 뒤에 부른다.
	 */
	double ContactGap(const FItem& Item, const FItem& Other, const double Tolerance)
	{
		double Best = TNumericLimits<double>::Max();
		const auto Near = [&Best, Tolerance](const FItem& From, const FItem& To)
		{
			const FBox Reach = To.Box.ExpandBy(Tolerance);
			const int32 Stride = FMath::Max(1, From.World.Num() / 512);
			for (int32 Vertex = 0; Vertex < From.World.Num() && Best > Tolerance; Vertex += Stride)
			{
				const FVector& Point = From.World[Vertex];
				if (!Reach.IsInside(Point))
				{
					continue;
				}
				const double Distance = DistanceToSurface(To, Point);
				if (Distance >= 0.0)
				{
					Best = FMath::Min(Best, Distance);
				}
			}
		};
		Near(Item, Other);
		Near(Other, Item);
		if (Best <= Tolerance)
		{
			return Best;
		}
		// 양 끝에만 꼭짓점이 있는 긴 배관은 꼭짓점이 벽면에서 멀어도 벽을 꿰고 들어간다.
		const FBox Reach = Other.Box.ExpandBy(Tolerance);
		const TArray<FIntVector>& Triangles = Item.Mesh->Triangles;
		const int32 Stride = FMath::Max(1, Triangles.Num() / 1024);
		for (int32 Index = 0; Index < Triangles.Num(); Index += Stride)
		{
			const FIntVector& Tri = Triangles[Index];
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				const FVector& A = Item.World[Tri[Edge]];
				const FVector Delta = Item.World[Tri[(Edge + 1) % 3]] - A;
				const double Length = Delta.Size();
				if (Length < 1e-3 || !RayHitsBox(Reach, A, Delta / Length, Length))
				{
					continue;
				}
				bool bCrossed = false;
				Trace(Other, A, Delta / Length, Length, [&bCrossed](const double) { bCrossed = true; });
				if (bCrossed)
				{
					return 0.0;
				}
			}
		}
		return Best;
	}

	/** 벽·문틀·배관처럼 벽에 묻어 짓는 부품. 구조물에 들어간 깊이는 결함이 아니라 시공으로 따로 적는다. */
	bool IsBuiltIn(const FString& MeshName)
	{
		static const TCHAR* Words[] = {
			TEXT("Frame"), TEXT("Door"), TEXT("Window"), TEXT("Pipe"), TEXT("Rail"), TEXT("Trim"),
			TEXT("Baseboard"), TEXT("Switch"), TEXT("Socket"), TEXT("Vent"), TEXT("Grille"), TEXT("Sill"),
			TEXT("Molding"), TEXT("Moulding"), TEXT("Cable"), TEXT("Conduit"), TEXT("Hatch"), TEXT("Meter"),
			TEXT("Breaker"), TEXT("Intercom"), TEXT("Plate"), TEXT("Riser"), TEXT("Duct"), TEXT("Stair") };
		for (const TCHAR* Word : Words)
		{
			if (MeshName.Contains(Word))
			{
				return true;
			}
		}
		return false;
	}
}

IGPlacementAudit::FResult IGPlacementAudit::Run(UWorld* World, const FOptions& Options)
{
	using namespace IGPlacementAuditPrivate;
	FResult Result;
#if WITH_EDITOR
	if (!World)
	{
		return Result;
	}
	Result.bMeasured = true;
	const TCHAR* Label = Options.Label.IsEmpty() ? TEXT("all") : *Options.Label;
	const bool bWholeWorld = !Options.Area.IsValid;
	TSet<const AActor*> Ignored;
	for (const TWeakObjectPtr<const AActor>& Actor : Options.Ignore)
	{
		if (Actor.IsValid())
		{
			Ignored.Add(Actor.Get());
		}
	}

	TMap<const UStaticMesh*, TSharedPtr<FMeshData>> Meshes;
	TArray<FItem> Items;
	int32 Instanced = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		if (!Actor || Actor->IsHidden() || Ignored.Contains(Actor) || Actor->IsA<APawn>())
		{
			continue;
		}
		// 인물과 괴이의 몸은 손·발 접점을 따로 재는 검사가 본다. 일부러 벽을 누르는 연출도 거기서 가른다.
		const FString ClassName = Actor->GetClass()->GetName();
		if (ClassName.Contains(TEXT("Entity")) || ClassName.Contains(TEXT("Figure")))
		{
			continue;
		}
		TInlineComponentArray<UStaticMeshComponent*> Components(const_cast<AActor*>(Actor));
		for (UStaticMeshComponent* Component : Components)
		{
			if (!Component || !Component->IsRegistered() || !Component->IsVisible() || Component->bHiddenInGame
				|| !Component->GetStaticMesh())
			{
				continue;
			}
			// 점검구 앞에 떠도는 먼지 알갱이(AirMote)는 원래 공중에 있다.
			if (Component->GetName().StartsWith(TEXT("AirMote")))
			{
				continue;
			}
			UStaticMesh* Mesh = Component->GetStaticMesh();
			// 인스턴스 메시(계단 디딤판, 난간 살 따위)는 인스턴스 하나를 부품 하나로 본다. 그 위에 놓인 물건의 받침이다.
			TArray<FTransform, TInlineAllocator<1>> Transforms;
			if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Component))
			{
				for (int32 Instance = 0; Instance < Instances->GetInstanceCount(); ++Instance)
				{
					FTransform Transform;
					if (Instances->GetInstanceTransform(Instance, Transform, true))
					{
						Transforms.Add(Transform);
					}
				}
				Instanced += Transforms.Num();
			}
			else
			{
				Transforms.Add(Component->GetComponentTransform());
			}
			for (int32 Slot = 0; Slot < Transforms.Num(); ++Slot)
			{
				const bool bInstance = Component->IsA<UInstancedStaticMeshComponent>();
				const FBox Box = bInstance ? Mesh->GetBoundingBox().TransformBy(Transforms[Slot]) : Component->Bounds.GetBox();
				if (!bWholeWorld && !Box.Intersect(Options.Area))
				{
					continue;
				}
				TSharedPtr<FMeshData>& Data = Meshes.FindOrAdd(Mesh);
				if (!Data)
				{
					Data = Load(Mesh);
				}
				if (Data->Triangles.IsEmpty())
				{
					++Result.Unmeasured;
					UE_LOG(LogIndieGame, Display, TEXT("IG_PLACEMENT label=%s kind=unmeasured item=%s/%s mesh=%s"),
						Label, *Actor->GetName(), *Component->GetName(), *Mesh->GetName());
					break;
				}
				FItem& Item = Items.AddDefaulted_GetRef();
				Item.Component = Component;
				Item.Transform = Transforms[Slot];
				Item.Owner = Actor;
				Item.Box = Box;
				Item.Mesh = Data;
				Item.MeshName = Mesh->GetName();
				Item.Name = bInstance
					? FString::Printf(TEXT("%s/%s#%d"), *Actor->GetName(), *Component->GetName(), Slot)
					: FString::Printf(TEXT("%s/%s"), *Actor->GetName(), *Component->GetName());
				Item.bStructure = Box.GetSize().GetMax() > 320.0;
				if (Item.bStructure)
				{
					UE_LOG(LogIndieGame, Display,
						TEXT("IG_PLACEMENT label=%s kind=structure item=%s mesh=%s closed=%d center=(%.1f,%.1f,%.1f) size=(%.1f,%.1f,%.1f)"),
						Label, *Item.Name, *Item.MeshName, Data->bClosed ? 1 : 0,
						Box.GetCenter().X, Box.GetCenter().Y, Box.GetCenter().Z,
						Box.GetSize().X, Box.GetSize().Y, Box.GetSize().Z);
				}
			}
		}
	}

	// 아래·옆·위로 쏜 선이 처음 닿는 다른 부품.
	const auto Nearest = [&Items](const FVector& Origin, const FVector& Direction, const double Length,
		const int32 Self, int32& OutIndex)
	{
		double Best = TNumericLimits<double>::Max();
		OutIndex = INDEX_NONE;
		FBox RayBounds(ForceInit);
		RayBounds += Origin;
		RayBounds += Origin + Direction * Length;
		RayBounds = RayBounds.ExpandBy(0.5);
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			if (Index == Self || !Items[Index].Box.Intersect(RayBounds))
			{
				continue;
			}
			Build(Items[Index]);
			Trace(Items[Index], Origin, Direction, Length, [&Best, &OutIndex, Index](const double T)
			{
				if (T < Best)
				{
					Best = T;
					OutIndex = Index;
				}
			});
		}
		return Best;
	};

	const double SupportTolerance = Options.SupportToleranceCm;
	const double PenetrationTolerance = Options.PenetrationToleranceCm;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		FItem& Item = Items[Index];
		if (Item.bStructure)
		{
			continue;
		}
		++Result.Props;
		Build(Item);
		// 실제 가장 낮은 꼭짓점들에서 잰다. 기울거나 누운 물건도 닿는 자리는 이 점들이다.
		double MinZ = TNumericLimits<double>::Max();
		for (const FVector& Point : Item.World)
		{
			MinZ = FMath::Min(MinZ, Point.Z);
		}
		FVector Centroid = FVector::ZeroVector;
		int32 LowCount = 0;
		FVector Extremes[4] = { FVector(TNumericLimits<double>::Max()), FVector(-TNumericLimits<double>::Max()),
			FVector(TNumericLimits<double>::Max()), FVector(-TNumericLimits<double>::Max()) };
		for (const FVector& Point : Item.World)
		{
			if (Point.Z > MinZ + 0.5)
			{
				continue;
			}
			Centroid += Point;
			++LowCount;
			if (Point.X < Extremes[0].X) { Extremes[0] = Point; }
			if (Point.X > Extremes[1].X) { Extremes[1] = Point; }
			if (Point.Y < Extremes[2].Y) { Extremes[2] = Point; }
			if (Point.Y > Extremes[3].Y) { Extremes[3] = Point; }
		}
		TArray<FVector, TInlineAllocator<5>> Samples;
		Samples.Add(Centroid / FMath::Max(1, LowCount));
		for (const FVector& Extreme : Extremes)
		{
			Samples.AddUnique(Extreme);
		}
		// 밑면보다 3cm 위에서 쏜다. 받침 면에 살짝 묻힌 물건도 받침의 윗면에서 잡힌다.
		double BestGap = TNumericLimits<double>::Max();
		int32 SupportIndex = INDEX_NONE;
		for (const FVector& Sample : Samples)
		{
			int32 Hit = INDEX_NONE;
			const double T = Nearest(Sample + FVector(0, 0, 3.0), FVector(0, 0, -1), 63.0, Index, Hit);
			if (Hit != INDEX_NONE)
			{
				const double Gap = T - 3.0;
				if (FMath::Abs(Gap) < FMath::Abs(BestGap))
				{
					BestGap = Gap;
					SupportIndex = Hit;
				}
			}
		}
		const TCHAR* State = nullptr;
		if (SupportIndex != INDEX_NONE && BestGap <= SupportTolerance)
		{
			State = BestGap < -PenetrationTolerance ? TEXT("sunk") : TEXT("rests");
		}
		else
		{
			// 밑에 없으면 벽걸이·천장 등일 수 있다. 바운드 여섯 면 가운데에서 바깥으로 잰다.
			static const FVector Axes[6] = { FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0),
				FVector(0, -1, 0), FVector(0, 0, 1), FVector(0, 0, -1) };
			for (const FVector& Axis : Axes)
			{
				const FVector Face = Item.Box.GetCenter() + Axis * (Item.Box.GetExtent() * Axis.GetAbs()).Size();
				int32 Hit = INDEX_NONE;
				const double T = Nearest(Face - Axis * 2.0, Axis, 5.0, Index, Hit);
				if (Hit != INDEX_NONE && T - 2.0 <= SupportTolerance)
				{
					State = Axis.Z > 0.5 ? TEXT("ceiling") : TEXT("wall");
					SupportIndex = Hit;
					BestGap = T - 2.0;
					break;
				}
			}
		}
		if (!State)
		{
			// 그래도 없으면 실제 면끼리 붙었는지 본다. 같은 액터의 부품(문짝과 손잡이)도 받침이 될 수 있다.
			const double ContactTolerance = Options.ContactToleranceCm;
			const FBox Reach = Item.Box.ExpandBy(ContactTolerance);
			double BestContact = TNumericLimits<double>::Max();
			int32 ContactIndex = INDEX_NONE;
			for (int32 Other = 0; Other < Items.Num() && BestContact > ContactTolerance; ++Other)
			{
				if (Other == Index || !Items[Other].Box.Intersect(Reach))
				{
					continue;
				}
				Build(Items[Other]);
				const double Gap = ContactGap(Item, Items[Other], ContactTolerance);
				if (Gap < BestContact)
				{
					BestContact = Gap;
					ContactIndex = Other;
				}
			}
			if (ContactIndex != INDEX_NONE && BestContact <= ContactTolerance)
			{
				State = TEXT("attached");
				SupportIndex = ContactIndex;
				BestGap = BestContact;
				++Result.Attached;
			}
		}
		if (!State)
		{
			++Result.Floating;
			IGPlacementAudit::FFlag& Flag = Result.Flags.AddDefaulted_GetRef();
			Flag.Kind = TEXT("floating");
			Flag.Name = Item.Name;
			Flag.Other = SupportIndex != INDEX_NONE ? Items[SupportIndex].Name : TEXT("none");
			Flag.Box = Item.Box;
			Flag.Cm = SupportIndex != INDEX_NONE ? BestGap : -1.0;
		}
		else
		{
			++Result.Supported;
		}
		UE_LOG(LogIndieGame, Display,
			TEXT("IG_PLACEMENT label=%s kind=support state=%s item=%s mesh=%s gapCm=%.2f support=%s supportMesh=%s center=(%.1f,%.1f,%.1f) size=(%.1f,%.1f,%.1f)"),
			Label, State ? State : TEXT("floating"), *Item.Name, *Item.MeshName,
			SupportIndex != INDEX_NONE ? BestGap : -1.0,
			SupportIndex != INDEX_NONE ? *Items[SupportIndex].Name : TEXT("none"),
			SupportIndex != INDEX_NONE ? *Items[SupportIndex].MeshName : TEXT("none"),
			Item.Box.GetCenter().X, Item.Box.GetCenter().Y, Item.Box.GetCenter().Z,
			Item.Box.GetSize().X, Item.Box.GetSize().Y, Item.Box.GetSize().Z);
	}

	// 관통: 바운드가 겹친 쌍만 고르고, 한쪽 꼭짓점이 다른 쪽 닫힌 메시 안에 있는지 실제 면으로 판정한다.
	int32 SameActorPairs = 0;
	for (int32 A = 0; A < Items.Num(); ++A)
	{
		if (Items[A].bStructure)
		{
			continue;
		}
		for (int32 B = 0; B < Items.Num(); ++B)
		{
			if (A == B)
			{
				continue;
			}
			const FBox Overlap = Items[A].Box.Overlap(Items[B].Box);
			if (!Overlap.IsValid || Overlap.GetSize().GetMin() < PenetrationTolerance)
			{
				continue;
			}
			if (Items[A].Owner == Items[B].Owner)
			{
				// 한 액터 안의 부품(문짝과 경첩, 등갓과 전구)은 함께 지은 것이다.
				++SameActorPairs;
				continue;
			}
			if (!Items[B].Mesh->bClosed)
			{
				++Result.OpenMeshPairs;
				continue;
			}
			Build(Items[A]);
			Build(Items[B]);
			++Result.PairsChecked;
			TArray<int32> Candidates;
			for (int32 Vertex = 0; Vertex < Items[A].World.Num(); ++Vertex)
			{
				if (Overlap.IsInside(Items[A].World[Vertex]))
				{
					Candidates.Add(Vertex);
				}
			}
			if (Candidates.IsEmpty())
			{
				continue;
			}
			const int32 Stride = FMath::Max(1, Candidates.Num() / 96);
			int32 Sampled = 0;
			int32 InsideCount = 0;
			double Depth = 0.0;
			for (int32 Slot = 0; Slot < Candidates.Num(); Slot += Stride)
			{
				const FVector& Point = Items[A].World[Candidates[Slot]];
				++Sampled;
				if (!IsInside(Items[B], Point))
				{
					continue;
				}
				++InsideCount;
				if (InsideCount <= 32)
				{
					Depth = FMath::Max(Depth, DistanceToSurface(Items[B], Point));
				}
			}
			if (InsideCount < 2 || Depth <= PenetrationTolerance)
			{
				continue;
			}
			const bool bBuiltIn = Items[B].bStructure && IsBuiltIn(Items[A].MeshName);
			if (!bBuiltIn)
			{
				++Result.Penetrations;
				IGPlacementAudit::FFlag& Flag = Result.Flags.AddDefaulted_GetRef();
				Flag.Kind = TEXT("penetration");
				Flag.Name = Items[A].Name;
				Flag.Other = Items[B].Name;
				Flag.Box = Overlap;
				Flag.Cm = Depth;
			}
			UE_LOG(LogIndieGame, Display,
				TEXT("IG_PLACEMENT label=%s kind=%s item=%s mesh=%s into=%s intoMesh=%s depthCm=%.2f insideVertices=%d sampled=%d overlapCenter=(%.1f,%.1f,%.1f)"),
				Label, bBuiltIn ? TEXT("embed") : TEXT("penetration"), *Items[A].Name, *Items[A].MeshName,
				*Items[B].Name, *Items[B].MeshName, Depth, InsideCount, Sampled,
				Overlap.GetCenter().X, Overlap.GetCenter().Y, Overlap.GetCenter().Z);
		}
	}

	// 광원: 등 기구 곁에 있는지, 벽·바닥 속에 묻혔는지, 그림자를 지는지.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		if (!Actor || Actor->IsHidden() || Ignored.Contains(Actor))
		{
			continue;
		}
		TInlineComponentArray<ULocalLightComponent*> Lights(const_cast<AActor*>(Actor));
		for (const ULocalLightComponent* Light : Lights)
		{
			if (!Light || !Light->IsRegistered() || !Light->IsVisible() || Light->Intensity <= 0.0f)
			{
				continue;
			}
			const FVector Position = Light->GetComponentLocation();
			if (!bWholeWorld && !Options.Area.IsInside(Position))
			{
				continue;
			}
			++Result.Lights;
			int32 Fixture = INDEX_NONE;
			double FixtureDistance = 12.0;
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				if (Items[Index].bStructure)
				{
					continue;
				}
				const double Distance = FMath::Sqrt(Items[Index].Box.ComputeSquaredDistanceToPoint(Position));
				if (Distance <= FixtureDistance)
				{
					FixtureDistance = Distance;
					Fixture = Index;
				}
			}
			int32 Buried = INDEX_NONE;
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				if (Index == Fixture || !Items[Index].bStructure || !Items[Index].Mesh->bClosed
					|| !Items[Index].Box.IsInside(Position))
				{
					continue;
				}
				Build(Items[Index]);
				if (IsInside(Items[Index], Position))
				{
					Buried = Index;
					break;
				}
			}
			const FString LightName = FString::Printf(TEXT("%s/%s"), *Actor->GetName(), *Light->GetName());
			if (Fixture == INDEX_NONE)
			{
				++Result.LightsWithoutFixture;
				IGPlacementAudit::FFlag& Flag = Result.Flags.AddDefaulted_GetRef();
				Flag.Kind = TEXT("light_no_fixture");
				Flag.Name = LightName;
				Flag.Box = FBox(Position - FVector(10.0), Position + FVector(10.0));
			}
			if (Buried != INDEX_NONE)
			{
				++Result.LightsBuried;
				IGPlacementAudit::FFlag& Flag = Result.Flags.AddDefaulted_GetRef();
				Flag.Kind = TEXT("light_buried");
				Flag.Name = LightName;
				Flag.Other = Items[Buried].Name;
				Flag.Box = FBox(Position - FVector(10.0), Position + FVector(10.0));
			}
			UE_LOG(LogIndieGame, Display,
				TEXT("IG_PLACEMENT label=%s kind=light item=%s class=%s at=(%.1f,%.1f,%.1f) intensity=%.1f radius=%.0f shadows=%d fixture=%s fixtureMesh=%s fixtureCm=%.1f buriedIn=%s"),
				Label, *LightName, *Light->GetClass()->GetName(), Position.X, Position.Y, Position.Z,
				Light->Intensity, Light->AttenuationRadius, Light->CastShadows ? 1 : 0,
				Fixture != INDEX_NONE ? *Items[Fixture].Name : TEXT("none"),
				Fixture != INDEX_NONE ? *Items[Fixture].MeshName : TEXT("none"),
				Fixture != INDEX_NONE ? FixtureDistance : -1.0,
				Buried != INDEX_NONE ? *Items[Buried].Name : TEXT("none"));
		}
	}

	UE_LOG(LogIndieGame, Display,
		TEXT("IG_PLACEMENT_SUMMARY label=%s props=%d supported=%d attached=%d floating=%d pairs=%d penetrations=%d openMeshPairs=%d sameActorPairs=%d lights=%d lightsWithoutFixture=%d lightsBuried=%d unmeasured=%d instances=%d"),
		Label, Result.Props, Result.Supported, Result.Attached, Result.Floating, Result.PairsChecked, Result.Penetrations,
		Result.OpenMeshPairs, SameActorPairs, Result.Lights, Result.LightsWithoutFixture, Result.LightsBuried,
		Result.Unmeasured, Instanced);
#endif
	return Result;
}
