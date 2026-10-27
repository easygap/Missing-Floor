#include "Entity/IGLiningEntity.h"

#include "AnimationRuntime.h"
#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "IndieGame.h"
#include "Interaction/IGAccessHatch.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/IGHorrorHUD.h"

namespace IGLining
{
	const TCHAR* const BodyPath = TEXT("/Game/Meshes/SK_Lining.SK_Lining");
	const TCHAR* const LegKeys[AIGLiningEntity::LegCount] = {TEXT("fl"), TEXT("fr"), TEXT("bl"), TEXT("br")};
	// 다리마다 앞뒤(sx)와 좌우(sy). 왼쪽이 -Y다(rig_lining.py LEGS).
	constexpr float LegSx[AIGLiningEntity::LegCount] = {1.0f, 1.0f, -1.0f, -1.0f};
	constexpr float LegSy[AIGLiningEntity::LegCount] = {-1.0f, 1.0f, -1.0f, 1.0f};
	// 걸음 묶음. 대각선 둘씩 짚고 당긴다.
	constexpr int32 GroupLegs[2][2] = {{0, 3}, {1, 2}};

	// 몸판 바닥에서 몸 뼈까지(cm). 누를 때 이만큼 벽에서 떨어진 자리가 몸 가운데다.
	constexpr float PressStandOff = 21.0f;
	// 짚고 버틸 때 몸 가운데와 벽 사이. 같은 벽 위의 다른 틈으로 옮겨 갈 때도 이 높이로 벽을 따라간다.
	constexpr float BraceStandOff = 36.0f;
	// 같은 벽을 따라갈 때 발이 짚는 자리는 그 벽 면에서 방 쪽으로 이만큼, 벽 뒤로 이만큼 안이다.
	constexpr float AlongWallPadReach = 60.0f;
	constexpr float AlongWallPadBehind = 8.0f;
	// 같은 벽을 따라갈 때 무릎이 벽 면에서 방 쪽으로 나오는 한도(cm). 무릎은 늘 방 쪽으로 굽어서, 발이 엉덩이
	// 가까이 짚으면 벽에서 1 m 넘게 나와 벽 앞에 선 사람 몸을 다리 충돌이 파고들었다. 5층 망치 자리는 벽에서
	// 110 cm라 사람 캡슐 반지름 30과 다리 반지름 12를 빼면 무릎이 68 cm 안에 있어야 닿지 않는다. 조금 여유를 뒀다.
	constexpr float AlongWallKneeHeight = 62.0f;
	// 무릎을 눕히기 시작하고 거두는 데 걸리는 시간(초), 눕힌 각이 한 틱에 뛰지 않게 도는 가장 빠른 각속도(라디안/초).
	constexpr float KneeLeanFadeSeconds = 0.45f;
	constexpr float KneeLeanRate = 12.0f;
	// 눕히는 쪽을 넘긴 뒤 다시 넘기지 않는 시간(초), 눕힐 쪽을 고를 때 몸 바깥쪽에 더 남아 있어야 하는 각(라디안).
	constexpr float KneeLeanSwitchHold = 0.5f;
	constexpr float KneeLeanSpare = 0.5f;
	// 틈 둘레를 짚는 자리. 틈 가장자리에서 이만큼 바깥이다.
	constexpr float BraceMarginAlong = 34.0f;
	constexpr float BraceMarginAcross = 40.0f;
	// 몸 자리를 맞출 때 보는 몸판 상자. 누를 때 몸판은 벽에서 3~39 cm, 짚을 때 15~57 cm에 있고, 눌려 휘는 겉 3 cm를
	// 빼면 짚을 때 바깥면이 52 cm까지 온다. 벽에 붙인 6 cm 안쪽(덧댄 방음 판 두께)과 몸판 둘레 1 cm는 눌려 휘는 막이라
	// 보지 않는다. 안쪽 방 큰 점검구는 열린 문짝과 보관함
	// 사이가 81.7 cm라 몸판(80 cm)이 1 cm 남짓 띄우고 들어간다.
	constexpr float FitNear = 6.0f;
	constexpr float FitFar = 54.0f;
	constexpr float FitEdgeSoft = 1.0f;
	// 몸판 상자는 처진 살을 따라 아래로 2~4 cm 내려앉는다(UpdateColliders). 몸 자리를 맞출 때 이만큼 내린 자리도 본다.
	// 안 보면 짚는 동안 관리실 작업대 윗면과 안쪽 방 바닥에 1~2 cm 걸렸다.
	constexpr float FitSag = 4.0f;
	// 틀에 걸린 무릎이 걸린 자리로 옮겨 가는 시간(초).
	constexpr float PinBlendSeconds = 0.35f;
	// 몸판 가장자리가 틈 안으로 이만큼 들어와도 둘레 막이 덮는다. 비킬 때는 2 cm씩 본다.
	constexpr float FitCoverSlack = 10.0f;
	constexpr float FitStep = 2.0f;
	// 패드 덩어리(66×60 cm)가 옆 면을 파고드는지 볼 때 빼는 가장자리와, 짚은 면에서 띄우는 높이.
	constexpr float PadFootprintInset = 6.0f;
	constexpr float PadFootprintLift = 6.0f;
	// 부채꼴로 짚을 데를 찾을 때 엉덩이에서 이만큼 안쪽은 짚지 않는다(다리가 다 접혀 무릎이 크게 돈다).
	constexpr float BraceMinDistance = 35.0f;

	// 걸음. 한 번 당길 때 몸이 나가는 거리와 시간, 두 다리를 옮기는 시간.
	constexpr float StepLength = 48.0f;
	constexpr float PullSeconds = 0.95f;
	constexpr float SwingSeconds = 0.78f;
	// 무릎이 아직 따라오는 발은 디딜 자리 바로 위에서 이만큼까지 기다렸다가 디딘다(초).
	constexpr float LandWaitSeconds = 0.5f;
	// 옮기기 전에 무릎 길을 정할 때 스윙을 나누는 자리 수(FLeg::SwingPoles 크기와 같다).
	constexpr int32 PoleSamples = 8;
	// 틈 둘레를 짚을 자리를 고를 때 옮겨 가는 무릎 길을 다 재 보는 후보 수와, 그 길에서 무릎을 둘 데가 없는 자리 하나의
	// 벌점(짚을 자리 점수와 같은 cm 단위).
	constexpr int32 BracePathChecks = 4;
	constexpr float BraceBlockedPenalty = 48.0f;
	// 무릎 자리를 고를 때 넓적다리·정강이를 이 반지름(cm)의 굵기로 본다. 다리 겉(12 cm)을 다 비키게 하면 좁은 구석에서
	// 고를 쪽이 없어서, 짚은 면 가까이 붙는 막은 조금 겹쳐도 되게 절반 남짓으로 잡았다.
	constexpr float KneeRoomRadius = 7.0f;
	// 다리 겉이 묻혔는지 재는 반지름(검사 로그 LINING_LEGS_THICK).
	constexpr float ThickMeasureRadius = 6.0f;
	constexpr float UnfoldSeconds = 1.05f;
	constexpr float PeelSeconds = 1.9f;
	constexpr float BraceSeconds = 0.95f;
	constexpr float PressSeconds = 1.45f;
	// 다리를 옮길 때 패드가 면에서 들리는 높이.
	constexpr float SwingLift = 26.0f;
	// 걷는 동안 짚을 자리는 발목이 제 엉덩이에서 이만큼(cm) 떨어져 있어야 한다. 엉덩이 바로 옆이나 밑을 짚으면 두 마디가
	// 거의 포개져 무릎이 반지름 78 cm 원 위에 있고, 발이 조금만 움직여도 무릎이 크게 돈다(5층 처음 자리에서 걷기 시작한
	// 왼쪽 앞다리가 엉덩이 바로 위 천장을 짚을 때 한 프레임 40 cm).
	constexpr float StepHipClear = 40.0f;
	// 걷다가 옮기는 발이 제 엉덩이 밑을 지나가 무릎이 이 빠르기(cm/초, 60fps 한 프레임에 15 cm)를 넘어 튀게 되면 몸 면을
	// 따라 비켜 돈다. 엉덩이→발목 축이 극을 거슬러 벽 쪽을 향하는 순간 무릎 방향이 뒤집혀 한 프레임에 47 cm 튀었다(5층
	// 이음매→점검구, 벽에서 바닥으로 옮기던 왼쪽 뒷다리). 펴지며 떨어지는 다리처럼 이보다 느린 스윙은 그대로 둔다.
	constexpr float SwingKneeSpeed = 900.0f;
	// 떨어지는 패드는 옮기기 전에 제자리에서 몸 쪽 가장자리부터 든다(스윙의 앞 PeelHold). 바깥 가장자리
	// (패드 가운데에서 PadOuterEdge)를 축으로 PeelTiltDegrees만큼 들리는 동안 물갈퀴와 패드 막이 먼저 벌어진다.
	constexpr float PeelHold = 0.16f;
	// 처음 벽에서 떨어질 때는 더 오래 붙어 있다가 떨어진다(자재 더미가 살아 있다는 걸 보이는 순간).
	constexpr float FirstPeelHold = 0.3f;
	constexpr float PeelTiltDegrees = 20.0f;
	constexpr float PadOuterEdge = 24.0f;
	// 패드 막 끝은 짚었던 면에 붙어 있다가 스윙이 이만큼 지나거나 이만큼(cm) 늘어나면 놓인다.
	constexpr float SkinRelease = 0.34f;
	constexpr float SkinStretchMax = 26.0f;
	// 몸 살이 처지는 깊이(cm). 면에 붙어 받칠 때, 다리로만 들고 있을 때, 넘지 않는 한도.
	constexpr float SagRest = 3.5f;
	constexpr float SagHung = 5.0f;
	constexpr float SagMax = 10.0f;
	// 물갈퀴가 느슨할 때 처지는 깊이, 패드가 떨어지기 직전에 벌어지는 거리, 한도(cm).
	constexpr float WebDroop = 7.0f;
	constexpr float WebOpen = 7.0f;
	constexpr float WebMax = 14.0f;
	// 무른 데의 용수철(고유 진동수 Hz, 감쇠비). 살은 무겁게 느리고 막은 가볍게 빠르다.
	constexpr float MassHz = 1.8f;
	constexpr float MassDamping = 0.38f;
	constexpr float WebHz = 2.4f;
	constexpr float WebDamping = 0.3f;
	constexpr float SkinHz = 4.5f;
	constexpr float SkinDamping = 0.28f;
	// 몸 둘레 막(치마): 틈을 누를 때 바깥으로 퍼지는 거리, 들려 있을 때 처지는 깊이, 한도(cm), 용수철.
	constexpr float SkirtSpread = 9.0f;
	constexpr float SkirtDroop = 8.0f;
	constexpr float SkirtMax = 16.0f;
	constexpr float SkirtHz = 2.2f;
	constexpr float SkirtDamping = 0.3f;
	// 누를 때 몸 살이 면을 따라 옆으로 밀려나는 거리(cm).
	constexpr float MassSpread = 3.5f;
	const TCHAR* const SkirtKeys[4] = {TEXT("f"), TEXT("b"), TEXT("l"), TEXT("r")};
	const FVector SkirtSides[4] = {FVector(1.0f, 0.0f, 0.0f), FVector(-1.0f, 0.0f, 0.0f), FVector(0.0f, -1.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f)};
	// 몸판 반치수(몸 기준, cm). 사람이 이 안에 들면 눌린다.
	const FVector BodyHalf(40.0f, 60.0f, 21.0f);
	// 다리 충돌 반지름. 마디를 감은 막 판과 지지대가 뼈 줄에서 떨어진 거리의 가운데(11.3 cm)와 4분의 3
	// (13.2 cm) 사이다(SK_Lining manifest의 collision_reference).
	constexpr float LegRadius = 12.0f;
	// 발 충돌 상자 반치수(X 바깥쪽, Y 옆, Z 짚은 면에서 위). 짚는 면에 눌려 퍼진 덩어리를 덮는다. 몸 쪽으로
	// 늘어진 막 끝은 얇아서 넣지 않는다.
	const FVector PadHalf(33.0f, 30.0f, 5.5f);
	constexpr float GripRadius = 30.0f;
	// 몸판 앞뒤(면의 법선 쪽)로는 몸에 실제로 닿을 만큼만 잡는다. 벽에서 한 걸음 물러선 사람까지
	// 덮치면 망치질하는 자리에서 늘 눌린다.
	constexpr float GripDepth = 12.0f;
	// 쉬는 동안 틈을 다시 보는 간격.
	constexpr float RestCheckSeconds = 0.25f;
	constexpr float HoldCheckSeconds = 0.5f;
	// 자막 간격.
	constexpr float CreakCaptionInterval = 4.5f;
	constexpr float CreakCueInterval = 1.6f;

	FQuat FrameRotation(const FVector& FromDir, const FVector& FromUp, const FVector& ToDir, const FVector& ToUp)
	{
		auto Basis = [](const FVector& Dir, const FVector& Up) {
			FVector X = Dir.GetSafeNormal();
			if (X.IsNearlyZero())
			{
				X = FVector::ForwardVector;
			}
			FVector Z = Up - X * FVector::DotProduct(Up, X);
			if (!Z.Normalize())
			{
				const FVector Alt = FMath::Abs(X.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
				Z = (Alt - X * FVector::DotProduct(Alt, X)).GetSafeNormal();
			}
			return FRotationMatrix::MakeFromXZ(X, Z).ToQuat();
		};
		return Basis(ToDir, ToUp) * Basis(FromDir, FromUp).Inverse();
	}

	/**
	 * 엉덩이와 발목이 정해지면 무릎은 엉덩이→발목 축을 도는 원 위에 있다. 극(Pole) 쪽이 각 0이고, 양의 각은
	 * Axis × Bend 쪽으로 돈다(극을 그 축으로 돌린 것과 같다).
	 */
	struct FKneeCircle
	{
		FVector Axis = FVector::UpVector;
		FVector Center = FVector::ZeroVector;
		FVector Bend = FVector::ForwardVector;
		FVector Side = FVector::RightVector;
		float Radius = 0.0f;

		FVector At(const float Angle) const
		{
			return Center + (Bend * FMath::Cos(Angle) + Side * FMath::Sin(Angle)) * Radius;
		}
	};

	FKneeCircle KneeCircle(const FVector& Hip, const FVector& Ankle, const float L1, const float L2, const FVector& Pole)
	{
		FKneeCircle Circle;
		const FVector To = Ankle - Hip;
		const float Raw = To.Size();
		Circle.Axis = Raw > KINDA_SMALL_NUMBER ? To / Raw : FVector::UpVector;
		const float Dist = FMath::Clamp(Raw, FMath::Abs(L1 - L2) + 0.5f, L1 + L2 - 0.5f);
		const float Along = (L1 * L1 - L2 * L2 + Dist * Dist) / (2.0f * Dist);
		Circle.Radius = FMath::Sqrt(FMath::Max(L1 * L1 - Along * Along, 0.0f));
		Circle.Center = Hip + Circle.Axis * Along;
		Circle.Bend = Pole - Circle.Axis * FVector::DotProduct(Pole, Circle.Axis);
		if (!Circle.Bend.Normalize())
		{
			Circle.Bend = (FVector::UpVector - Circle.Axis * Circle.Axis.Z).GetSafeNormal();
		}
		Circle.Side = FVector::CrossProduct(Circle.Axis, Circle.Bend);
		return Circle;
	}

	FVector TwoBoneKnee(const FVector& Hip, const FVector& Ankle, const float L1, const float L2, const FVector& Pole)
	{
		return KneeCircle(Hip, Ankle, L1, L2, Pole).At(0.0f);
	}

	float Smooth(const float X)
	{
		const float T = FMath::Clamp(X, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	/** 스윙 비율 T에서 옮기는 발이 비켜 도는 몫(0~1). Peak에서 1이고, 처음과 끝에서 0으로 천천히 들어간다. */
	float DetourBump(const float T, const float Peak)
	{
		return T <= Peak ? Smooth(T / Peak) : Smooth((1.0f - T) / (1.0f - Peak));
	}

	/** 접자를 닫은 다리(몸 기준, 엉덩이에서). rig_lining.py folded_joints와 같은 수다. */
	void FoldedJoints(const int32 Leg, const FVector& HipLocal, FVector& OutKnee, FVector& OutAnkle, FVector& OutPad)
	{
		const float Sy = LegSy[Leg];
		if (LegSx[Leg] > 0.0f)
		{
			OutKnee = HipLocal + FVector(-78.0f, 10.0f * Sy, 10.0f);
			OutAnkle = OutKnee + FVector(76.0f, 6.0f * Sy, -12.0f);
			OutPad = OutAnkle + FVector(7.0f, 11.0f * Sy, -14.0f);
		}
		else
		{
			OutKnee = HipLocal + FVector(78.0f, 12.0f * Sy, -10.0f);
			OutAnkle = OutKnee + FVector(-76.0f, 5.0f * Sy, 2.0f);
			OutPad = OutAnkle + FVector(-7.0f, 11.0f * Sy, -13.0f);
		}
	}

	/** 다 열린 점검구 문짝 판. 덧댄 것은 열리는 중인 문짝 대신 이것을 보고 자리와 다리를 정한다. */
	struct FDoorPlane
	{
		FVector Hinge = FVector::ZeroVector;
		FVector Along = FVector::ForwardVector;
		FVector Up = FVector::UpVector;
		float Width = 0.0f;
		float Height = 0.0f;
		bool bValid = false;
	};

	FDoorPlane OpenDoorPlane(const AIGAccessHatch* Hatch)
	{
		FDoorPlane Door;
		if (Hatch && Hatch->IsOpen())
		{
			Hatch->GetDoorPlane(AIGAccessHatch::GetOpenYaw(), Door.Hinge, Door.Along, Door.Up, Door.Width, Door.Height);
			Door.bValid = Door.Width > 0.0f && Door.Height > 0.0f;
		}
		return Door;
	}

	/** 선분이 문짝 판(두께와 여유 Margin을 더한)을 지나는가. */
	bool SegmentCrossesDoor(const FDoorPlane& Door, const FVector& A, const FVector& B, const float Margin)
	{
		if (!Door.bValid)
		{
			return false;
		}
		const FVector Normal = FVector::CrossProduct(Door.Along, Door.Up).GetSafeNormal();
		const float DA = FVector::DotProduct(A - Door.Hinge, Normal);
		const float DB = FVector::DotProduct(B - Door.Hinge, Normal);
		const auto Inside = [&Door, Margin](const FVector& P)
		{
			const float Along = FVector::DotProduct(P - Door.Hinge, Door.Along);
			const float Up = FVector::DotProduct(P - Door.Hinge, Door.Up);
			return Along >= -Margin && Along <= Door.Width + Margin && FMath::Abs(Up) <= Door.Height * 0.5f + Margin;
		};
		if ((DA > Margin && DB > Margin) || (DA < -Margin && DB < -Margin))
		{
			return false;
		}
		if (FMath::Abs(DA - DB) < KINDA_SMALL_NUMBER || (FMath::Abs(DA) <= Margin && FMath::Abs(DB) <= Margin))
		{
			// 판과 거의 나란히 판 두께 안을 지난다. 양 끝과 가운데를 본다.
			return Inside(A) || Inside(B) || Inside((A + B) * 0.5f);
		}
		const float T = FMath::Clamp(DA / (DA - DB), 0.0f, 1.0f);
		return Inside(A + (B - A) * T);
	}

	/** 문짝 판이 상자(가운데·회전·반치수)에 들어간 가장 깊은 거리(cm). 들어가지 않으면 0. */
	float DoorDepthInBox(const FDoorPlane& Door, const FVector& Center, const FQuat& Rotation, const FVector& Half)
	{
		if (!Door.bValid)
		{
			return 0.0f;
		}
		float Deepest = 0.0f;
		const int32 Across = FMath::Max(2, FMath::CeilToInt(Door.Width / 2.0f));
		const int32 Down = FMath::Max(2, FMath::CeilToInt(Door.Height / 8.0f));
		for (int32 I = 0; I <= Across; ++I)
		{
			for (int32 J = 0; J <= Down; ++J)
			{
				const FVector Point = Door.Hinge + Door.Along * (Door.Width * I / Across)
					+ Door.Up * (Door.Height * (static_cast<float>(J) / Down - 0.5f));
				const FVector Local = Rotation.UnrotateVector(Point - Center);
				const FVector Room = Half - Local.GetAbs();
				if (Room.X > 0.0f && Room.Y > 0.0f && Room.Z > 0.0f)
				{
					Deepest = FMath::Max(Deepest, FMath::Min3(Room.X, Room.Y, Room.Z));
				}
			}
		}
		return Deepest;
	}

	/** -IGLiningDrawCollision이면 충돌 모양을 그린다(검사 캡처에서 보이는 몸과 맞춰 본다). */
	bool DrawCollision()
	{
		static const bool bDraw = FParse::Param(FCommandLine::Get(), TEXT("IGLiningDrawCollision"));
		return bDraw;
	}

	/**
	 * 사람이 몸판·다리·발을 딛고 서지 못하게 한다. 옮기던 발 상자가 서 있는 사람 발밑 가장자리로 들어오면 캐릭터
	 * 무브먼트가 그 상자를 바닥으로 잡아 사람을 들어 올려 싣고 갔다(5층 망치 자리, 패드가 벽에서 바닥으로 옮겨 갈 때).
	 */
	void KeepPawnsOff(UPrimitiveComponent* Shape)
	{
		Shape->CanCharacterStepUpOn = ECB_No;
		Shape->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.0f));
	}
}

AIGLiningEntity::AIGLiningEntity()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// 검사 실행에서만 다리를 잰다(무릎 튐, 관통 프레임). 관통은 프레임마다 트레이스라 평소에는 끈다.
	bMeasureLegs = FParse::Param(FCommandLine::Get(), TEXT("IGLiningProbe"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGLeftoverProbe"))
		|| FParse::Param(FCommandLine::Get(), TEXT("IGNightFourHammerProbe"));
	bKneeContinuity = !FParse::Param(FCommandLine::Get(), TEXT("IGLiningKneeBaseline"));
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);
	BodyBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("BodyBlocker"));
	BodyBlocker->SetupAttachment(Root);
	BodyBlocker->SetBoxExtent(IGLining::BodyHalf);
	BodyBlocker->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	BodyBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyBlocker->SetCollisionObjectType(ECC_WorldDynamic);
	BodyBlocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	BodyBlocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	BodyBlocker->SetCanEverAffectNavigation(false);
	IGLining::KeepPawnsOff(BodyBlocker);
	BodyBlocker->SetUsingAbsoluteRotation(true);
	SetActorHiddenInGame(true);
}

bool AIGLiningEntity::LoadBody()
{
	if (Body)
	{
		return true;
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, IGLining::BodyPath, nullptr, LOAD_NoWarn);
	if (!Mesh)
	{
		UE_LOG(LogIndieGame, Warning, TEXT("%s missing; the lining does not appear"), IGLining::BodyPath);
		return false;
	}
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	BodyBone = TEXT("body");
	const int32 BodyIndex = Ref.FindBoneIndex(BodyBone);
	if (BodyIndex == INDEX_NONE)
	{
		UE_LOG(LogIndieGame, Warning, TEXT("SK_Lining lacks the body bone"));
		return false;
	}
	RestBody = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, BodyIndex);
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		for (int32 Segment = 0; Segment < 3; ++Segment)
		{
			SegmentBones[Leg][Segment] = FName(*FString::Printf(TEXT("leg_%s_%d"), IGLining::LegKeys[Leg], Segment + 1));
			const int32 Index = Ref.FindBoneIndex(SegmentBones[Leg][Segment]);
			if (Index == INDEX_NONE)
			{
				UE_LOG(LogIndieGame, Warning, TEXT("SK_Lining lacks %s"), *SegmentBones[Leg][Segment].ToString());
				return false;
			}
			RestSegment[Leg][Segment] = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Index);
		}
		const FVector Hip = RestSegment[Leg][0].GetLocation();
		const FVector Knee = RestSegment[Leg][1].GetLocation();
		const FVector Ankle = RestSegment[Leg][2].GetLocation();
		RestOutward[Leg] = (Ankle - Knee).GetSafeNormal2D();
		// 패드 가운데는 발목에서 바깥으로 10 cm, 아래로 16 cm(rig_lining.py SEG3_OUT·SEG3_DOWN).
		RestPad[Leg] = Ankle + RestOutward[Leg] * 10.0f + FVector(0.0f, 0.0f, -16.0f);
		HipLocal[Leg] = Hip - RestBody.GetLocation();
		Length1[Leg] = FVector::Distance(Hip, Knee);
		Length2[Leg] = FVector::Distance(Knee, Ankle);
	}
	// 무른 뼈(몸 살·물갈퀴·패드 막). 없는 몸이면 다리만 놓는다.
	bHasSoftBones = true;
	for (int32 Leg = 0; Leg < LegCount && bHasSoftBones; ++Leg)
	{
		MassBones[Leg] = FName(*FString::Printf(TEXT("mass_%s"), IGLining::LegKeys[Leg]));
		WebBones[Leg] = FName(*FString::Printf(TEXT("web_%s"), IGLining::LegKeys[Leg]));
		SkinBones[Leg] = FName(*FString::Printf(TEXT("skin_%s"), IGLining::LegKeys[Leg]));
		const int32 MassIndex = Ref.FindBoneIndex(MassBones[Leg]);
		const int32 WebIndex = Ref.FindBoneIndex(WebBones[Leg]);
		const int32 SkinIndex = Ref.FindBoneIndex(SkinBones[Leg]);
		if (MassIndex == INDEX_NONE || WebIndex == INDEX_NONE || SkinIndex == INDEX_NONE)
		{
			bHasSoftBones = false;
			break;
		}
		MassRest[Leg] = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, MassIndex).GetRelativeTransform(RestBody);
		const FTransform WebPose = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, WebIndex);
		WebRestBody[Leg] = WebPose.GetRelativeTransform(RestBody);
		WebRestUpper[Leg] = WebPose.GetRelativeTransform(RestSegment[Leg][0]);
		SkinRest[Leg] = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, SkinIndex).GetRelativeTransform(RestSegment[Leg][2]);
	}
	for (int32 Side = 0; Side < SkirtCount && bHasSoftBones; ++Side)
	{
		SkirtBones[Side] = FName(*FString::Printf(TEXT("skirt_%s"), IGLining::SkirtKeys[Side]));
		const int32 SkirtIndex = Ref.FindBoneIndex(SkirtBones[Side]);
		if (SkirtIndex == INDEX_NONE)
		{
			bHasSoftBones = false;
			break;
		}
		SkirtRest[Side] = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, SkirtIndex).GetRelativeTransform(RestBody);
	}
	// 다리를 천장과 벽까지 펴면 쉬는 자세의 경계를 크게 벗어난다. 경계를 넉넉히 늘려 잘리지 않게 한다.
	Mesh->SetPositiveBoundsExtension(FVector(140.0f, 140.0f, 280.0f));
	Mesh->SetNegativeBoundsExtension(FVector(140.0f, 140.0f, 280.0f));

	Body = NewObject<UPoseableMeshComponent>(this, TEXT("LiningBody"));
	Body->SetupAttachment(Root);
	Body->SetSkinnedAssetAndUpdate(Mesh);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);
	Body->SetCastShadow(true);
	Body->SetRelativeLocation(-RestBody.GetLocation());
	Body->RegisterComponent();
	Body->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);

	// 다리·패드 충돌은 사람(Pawn)만 막는다. 짚을 데를 찾는 트레이스(WorldStatic)에는 걸리지 않는다.
	const auto BlockPawn = [this](UPrimitiveComponent* Shape)
	{
		Shape->SetupAttachment(Root);
		Shape->SetUsingAbsoluteLocation(true);
		Shape->SetUsingAbsoluteRotation(true);
		Shape->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
		Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Shape->SetCollisionObjectType(ECC_WorldDynamic);
		Shape->SetCollisionResponseToAllChannels(ECR_Ignore);
		Shape->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Shape->SetCanEverAffectNavigation(false);
		IGLining::KeepPawnsOff(Shape);
		Shape->RegisterComponent();
	};
	for (int32 Index = 0; Index < LegCount * 2; ++Index)
	{
		UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(this, *FString::Printf(TEXT("LegBlocker%d"), Index));
		Capsule->SetCapsuleSize(IGLining::LegRadius, 30.0f);
		BlockPawn(Capsule);
		LegBlockers.Add(Capsule);
	}
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(this, *FString::Printf(TEXT("PadBlocker%d"), Index));
		Box->SetBoxExtent(IGLining::PadHalf);
		BlockPawn(Box);
		PadBlockers.Add(Box);
	}
	UE_LOG(LogIndieGame, Display, TEXT("LINING_BODY scale body=%s seg=%s rot body=%s seg=%s soft=%d"),
		*RestBody.GetScale3D().ToCompactString(), *RestSegment[0][0].GetScale3D().ToCompactString(),
		*RestBody.GetRotation().Rotator().ToCompactString(), *RestSegment[0][0].GetRotation().Rotator().ToCompactString(),
		bHasSoftBones ? 1 : 0);
	UE_LOG(LogIndieGame, Display, TEXT("LINING_BODY loaded rest body=%s hip_fl=%s knee_fl=%s ankle_fl=%s L1=%.1f L2=%.1f"),
		*RestBody.GetLocation().ToCompactString(),
		*RestSegment[0][0].GetLocation().ToCompactString(),
		*RestSegment[0][1].GetLocation().ToCompactString(),
		*RestSegment[0][2].GetLocation().ToCompactString(),
		Length1[0], Length2[0]);
	return true;
}

void AIGLiningEntity::RestOnSurface(const FVector& Point, const FVector& Normal, const FVector& Forward)
{
	if (!LoadBody())
	{
		return;
	}
	const FVector Up = Normal.GetSafeNormal();
	BodyRotation = IGLining::FrameRotation(FVector::ForwardVector, FVector::UpVector, Forward, Up);
	BodyCenter = Point + Up * IGLining::PressStandOff;
	HintLeg = INDEX_NONE;
	for (FLeg& Leg : Legs)
	{
		Leg.Fold = 1.0f;
		Leg.bSwinging = false;
	}
	ResetKneeLean();
	HeldGap = NAME_None;
	TargetGap = NAME_None;
	SetActorHiddenInGame(false);
	SetBlockersEnabled(true);
	SetState(EIGLiningState::Folded);
	ResetSoftParts();
	PoseBody();
	StartTicking();
	UE_LOG(LogIndieGame, Display, TEXT("LINING_REST center=%s bodybone=%s padfl=%s bounds=%s/%s visible=%d hidden=%d tick=%d mats=%d lod=%d"),
		*BodyCenter.ToCompactString(),
		*Body->GetBoneLocation(BodyBone).ToCompactString(),
		*Body->GetBoneLocation(SegmentBones[0][2]).ToCompactString(),
		*Body->Bounds.Origin.ToCompactString(), *Body->Bounds.BoxExtent.ToCompactString(),
		Body->IsVisible() ? 1 : 0, IsHidden() ? 1 : 0, Body->IsComponentTickEnabled() ? 1 : 0,
		Body->GetNumMaterials(), Body->GetPredictedLODLevel());
}

FVector AIGLiningEntity::FoldedPadWorld(const int32 Leg, const FVector& Center, const FQuat& Rotation) const
{
	FVector Knee, Ankle, Pad;
	IGLining::FoldedJoints(Leg, HipLocal[Leg], Knee, Ankle, Pad);
	return Center + Rotation.RotateVector(Pad);
}

UStaticMeshComponent* AIGLiningEntity::ActiveHatchDoor() const
{
	FName Id = NAME_None;
	switch (State)
	{
	case EIGLiningState::Peeling:
	case EIGLiningState::Stepping:
	case EIGLiningState::Bracing:
	case EIGLiningState::Pressing:
		Id = TargetGap;
		break;
	case EIGLiningState::Holding:
		Id = HeldGap;
		break;
	case EIGLiningState::Gripping:
		Id = TargetGap.IsNone() ? HeldGap : TargetGap;
		break;
	default:
		break;
	}
	const FIGLiningGap* Gap = Id.IsNone() ? nullptr : FindGap(Id);
	AIGAccessHatch* Hatch = Gap ? Gap->Hatch.Get() : nullptr;
	return Hatch && Hatch->IsOpen() ? Hatch->GetDoorPanel() : nullptr;
}

bool AIGLiningEntity::IsBodySpaceFree(const FIGLiningGap& Gap, const FVector& Anchor, const FQuat& Rotation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	const FVector N = Rotation.GetUpVector();
	const FVector Center = Anchor + N * ((IGLining::FitNear + IGLining::FitFar) * 0.5f);
	const FCollisionShape Shape = FCollisionShape::MakeBox(
		FVector(IGLining::BodyHalf.X - IGLining::FitEdgeSoft, IGLining::BodyHalf.Y - IGLining::FitEdgeSoft, (IGLining::FitFar - IGLining::FitNear) * 0.5f));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningFit), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	AIGAccessHatch* Hatch = Gap.Hatch.Get();
	if (Hatch)
	{
		Params.AddIgnoredActor(Hatch);
	}
	const FVector Sagged = Center - FVector(0.0f, 0.0f, IGLining::FitSag);
	if (World->OverlapAnyTestByObjectType(Center, Rotation, FCollisionObjectQueryParams(ECC_WorldStatic), Shape, Params)
		|| World->OverlapAnyTestByObjectType(Sagged, Rotation, FCollisionObjectQueryParams(ECC_WorldStatic), Shape, Params))
	{
		return false;
	}
	// 열린 문짝은 경첩 쪽 벽 앞에 104도로 선다(덮개를 막 열면 아직 도는 중이라 다 열린 자리로 본다). 몸판이 경첩을
	// 넘으면 문짝을 뚫는다.
	const IGLining::FDoorPlane Door = IGLining::OpenDoorPlane(Hatch);
	return IGLining::DoorDepthInBox(Door, Center, Rotation, Shape.GetExtent()) <= 0.0f
		&& IGLining::DoorDepthInBox(Door, Sagged, Rotation, Shape.GetExtent()) <= 0.0f;
}

float AIGLiningEntity::BodySpacePenetration(const FIGLiningGap& Gap, const FVector& Anchor, const FQuat& Rotation,
	FString* OutNames) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	const FVector N = Rotation.GetUpVector();
	const FVector Center = Anchor + N * ((IGLining::FitNear + IGLining::FitFar) * 0.5f);
	const FCollisionShape Shape = FCollisionShape::MakeBox(FVector(IGLining::BodyHalf.X - IGLining::FitEdgeSoft,
		IGLining::BodyHalf.Y - IGLining::FitEdgeSoft, (IGLining::FitFar - IGLining::FitNear) * 0.5f));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningFitDepth), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	AIGAccessHatch* Hatch = Gap.Hatch.Get();
	if (Hatch)
	{
		Params.AddIgnoredActor(Hatch);
	}
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, Rotation, FCollisionObjectQueryParams(ECC_WorldStatic), Shape, Params);
	const FVector Sagged = Center - FVector(0.0f, 0.0f, IGLining::FitSag);
	TArray<FOverlapResult> SaggedOverlaps;
	World->OverlapMultiByObjectType(SaggedOverlaps, Sagged, Rotation, FCollisionObjectQueryParams(ECC_WorldStatic), Shape, Params);
	Overlaps.Append(SaggedOverlaps);
	TArray<UPrimitiveComponent*> Components;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (UPrimitiveComponent* Component = Overlap.GetComponent())
		{
			Components.AddUnique(Component);
		}
	}
	float Depth = 0.0f;
	if (const float DoorDepth = IGLining::DoorDepthInBox(IGLining::OpenDoorPlane(Hatch), Center, Rotation, Shape.GetExtent()); DoorDepth > 0.0f)
	{
		Depth += FMath::Max(DoorDepth, 0.5f);
		if (OutNames)
		{
			*OutNames += FString::Printf(TEXT(" %s.Door=%.1f"), *Hatch->GetName(), DoorDepth);
		}
	}
	for (UPrimitiveComponent* Component : Components)
	{
		FMTDResult MTD;
		FMTDResult SaggedMTD;
		// 깊이를 못 재면(복잡한 충돌) 겹친 것만으로 10 cm로 친다. 내려앉은 자리와 둘 중 깊은 쪽이다.
		const bool bAt = Component->ComputePenetration(MTD, Shape, Center, Rotation);
		const bool bSagged = Component->ComputePenetration(SaggedMTD, Shape, Sagged, Rotation);
		const float Each = bAt || bSagged ? FMath::Max(bAt ? MTD.Distance : 0.0f, bSagged ? SaggedMTD.Distance : 0.0f) : 10.0f;
		Depth += FMath::Max(Each, 0.5f);
		if (OutNames)
		{
			*OutNames += FString::Printf(TEXT(" %s.%s=%.1f"), Component->GetOwner() ? *Component->GetOwner()->GetName() : TEXT("none"),
				*Component->GetName(), Each);
		}
	}
	return Depth;
}

AIGLiningEntity::FGapPose AIGLiningEntity::FitGapPose(const FIGLiningGap& Gap) const
{
	const FVector N = Gap.Normal.GetSafeNormal();
	FVector U = Gap.Up - N * FVector::DotProduct(Gap.Up, N);
	if (!U.Normalize())
	{
		const FVector Alt = FMath::Abs(N.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
		U = (Alt - N * FVector::DotProduct(Alt, N)).GetSafeNormal();
	}
	const FVector Manual = Gap.BodyAnchor() - Gap.Center;
	// 빈 자리가 없으면 가장 덜 파고드는 자리로 간다(겹친 깊이의 합).
	FGapPose Least;
	float LeastDepth = TNumericLimits<float>::Max();
	FVector2D LeastOffset = FVector2D::ZeroVector;
	for (int32 Turn = 0; Turn < 2; ++Turn)
	{
		FGapPose Pose;
		Pose.Forward = Turn == 0 ? U : FVector::CrossProduct(N, U);
		Pose.Side = FVector::CrossProduct(N, Pose.Forward);
		Pose.Rotation = IGLining::FrameRotation(FVector::ForwardVector, FVector::UpVector, Pose.Forward, N);
		Pose.bTurned = Turn == 1;
		Pose.bValid = true;
		// Size.X는 가로(Gap.Up에 수직), Size.Y는 세로(Gap.Up 쪽)다. 몸 앞쪽·옆쪽으로 잰 반치수로 바꾼다.
		Pose.GapHalf = Turn == 0 ? FVector2D(Gap.Size.Y, Gap.Size.X) * 0.5f : FVector2D(Gap.Size.X, Gap.Size.Y) * 0.5f;
		// 몸판이 틈보다 짧은 쪽(302호 좁은 틈 세로 110 cm)은 비키지 않고 가운데에 댄다. 둘레 막이 나머지를 덮는다.
		const float LimitForward = FMath::Max(0.0f, IGLining::BodyHalf.X - Pose.GapHalf.X + IGLining::FitCoverSlack);
		const float LimitSide = FMath::Max(0.0f, IGLining::BodyHalf.Y - Pose.GapHalf.Y + IGLining::FitCoverSlack);
		// 감독이 정한 비킴(Gap.BodyOffset)에서 가까운 자리부터 본다. 틈을 덮는 한도 밖으로는 비키지 않는다.
		const FVector2D Preferred(FVector::DotProduct(Manual, Pose.Forward), FVector::DotProduct(Manual, Pose.Side));
		TArray<FVector2D> Offsets;
		const int32 CountForward = FMath::FloorToInt(LimitForward / IGLining::FitStep);
		const int32 CountSide = FMath::FloorToInt(LimitSide / IGLining::FitStep);
		for (int32 F = -CountForward; F <= CountForward; ++F)
		{
			for (int32 S = -CountSide; S <= CountSide; ++S)
			{
				Offsets.Add(FVector2D(static_cast<float>(F), static_cast<float>(S)) * IGLining::FitStep);
			}
		}
		Offsets.Add(FVector2D(FMath::Clamp(Preferred.X, -LimitForward, LimitForward), FMath::Clamp(Preferred.Y, -LimitSide, LimitSide)));
		Offsets.Sort([&Preferred](const FVector2D& A, const FVector2D& B)
		{
			return FVector2D::DistSquared(A, Preferred) < FVector2D::DistSquared(B, Preferred);
		});
		for (const FVector2D& Offset : Offsets)
		{
			const FVector Anchor = Gap.Center + Pose.Forward * Offset.X + Pose.Side * Offset.Y;
			if (IsBodySpaceFree(Gap, Anchor, Pose.Rotation))
			{
				Pose.Anchor = Anchor;
				UE_LOG(LogIndieGame, Display, TEXT("LINING_FIT %s turned=%d offset=%.0f,%.0f"), *Gap.Id.ToString(), Turn,
					Offset.X, Offset.Y);
				return Pose;
			}
		}
		// 빈 자리가 없을 때만 깊이를 잰다. 깊이는 무거워서 6 cm마다 본다. 돌리지 않은 몸(감독이 정한 모습)을 조금 더 쳐준다.
		for (const FVector2D& Offset : Offsets)
		{
			if (FMath::Fmod(FMath::Abs(Offset.X), 6.0f) > 0.5f || FMath::Fmod(FMath::Abs(Offset.Y), 6.0f) > 0.5f)
			{
				continue;
			}
			const FVector Anchor = Gap.Center + Pose.Forward * Offset.X + Pose.Side * Offset.Y;
			const float Depth = BodySpacePenetration(Gap, Anchor, Pose.Rotation, nullptr) + (Turn == 0 ? 0.0f : 1.0f);
			if (Depth < LeastDepth)
			{
				LeastDepth = Depth;
				Least = Pose;
				Least.Anchor = Anchor;
				LeastOffset = Offset;
			}
		}
	}
	FString Names;
	BodySpacePenetration(Gap, Least.Anchor, Least.Rotation, &Names);
	UE_LOG(LogIndieGame, Warning, TEXT("LINING_FIT_LEAST %s turned=%d offset=%.0f,%.0f depth=%.1f hits:%s"), *Gap.Id.ToString(),
		Least.bTurned ? 1 : 0, LeastOffset.X, LeastOffset.Y, LeastDepth, *Names);
	return Least;
}

bool AIGLiningEntity::IsBracePadUsable(const int32 Leg, const FIGLiningGap& Gap, const FGapPose& Pose, const FVector& Pad,
	const FVector& Normal) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningBracePad), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	AIGAccessHatch* Hatch = Gap.Hatch.Get();
	if (Hatch)
	{
		Params.AddIgnoredActor(Hatch);
	}
	// 덮개를 막 열면 문짝은 아직 도는 중이다. 다 열린 문짝 판을 보고 정한다. 문짝 두께와 다리 굵기 절반을 더해 비킨다.
	const IGLining::FDoorPlane Door = IGLining::OpenDoorPlane(Hatch);
	constexpr float DoorMargin = 2.0f + IGLining::LegRadius * 0.5f;
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	const FVector N = Pose.Rotation.GetUpVector();
	FHitResult Block;
	// 짚을 때(벽에서 36 cm)와 누를 때(21 cm) 두 몸 자리에서 다 닿고, 엉덩이에서 패드까지 막히지 않아야 한다.
	for (const float StandOff : {IGLining::BraceStandOff, IGLining::PressStandOff})
	{
		const FVector Center = Pose.Anchor + N * StandOff;
		if (ReachTo(Leg, Pad, Normal, Center, Pose.Rotation) > 0.96f)
		{
			return false;
		}
		const FVector Hip = HipWorld(Leg, Center, Pose.Rotation);
		const FVector ToPad = Pad - Hip;
		const float Distance = ToPad.Size();
		if (Distance > 10.0f)
		{
			const FVector End = Pad - ToPad / Distance * 6.0f;
			if (World->LineTraceSingleByObjectType(Block, Hip, End, Objects, Params)
				|| IGLining::SegmentCrossesDoor(Door, Hip, End, DoorMargin))
			{
				return false;
			}
		}
	}
	return IsPadPlaceable(Leg, HipWorld(Leg, Pose.Anchor + N * IGLining::PressStandOff, Pose.Rotation), Pose.Rotation, Pad, Normal,
		Hatch);
}

bool AIGLiningEntity::IsPadPlaceable(const int32 Leg, const FVector& Hip, const FQuat& Rotation, const FVector& Pad,
	const FVector& Normal, const AIGAccessHatch* Hatch) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	if (IsInsideGapCavity(Pad))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningPadPlace), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	if (Hatch)
	{
		Params.AddIgnoredActor(Hatch);
	}
	const IGLining::FDoorPlane Door = IGLining::OpenDoorPlane(Hatch);
	constexpr float DoorMargin = 2.0f + IGLining::LegRadius * 0.5f;
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	// 패드 덩어리가 옆 벽·바닥·소품이나 열린 문짝을 파고들지 않는다. 가장자리는 눌려 휘는 막이라 조금 덜 본다.
	FVector PadForward = Pad - Hip;
	PadForward -= Normal * FVector::DotProduct(PadForward, Normal);
	if (!PadForward.Normalize())
	{
		PadForward = Rotation.RotateVector(RestOutward[Leg]);
	}
	const FQuat PadQuat = FRotationMatrix::MakeFromXZ(PadForward, Normal).ToQuat();
	const FVector FootCenter = Pad + Normal * (IGLining::PadFootprintLift + 1.0f);
	const FCollisionShape Foot = FCollisionShape::MakeBox(
		FVector(IGLining::PadHalf.X - IGLining::PadFootprintInset, IGLining::PadHalf.Y - IGLining::PadFootprintInset, 3.0f));
	if (World->OverlapAnyTestByObjectType(FootCenter, PadQuat, Objects, Foot, Params)
		|| IGLining::DoorDepthInBox(Door, FootCenter, PadQuat, Foot.GetExtent()) > 0.0f)
	{
		return false;
	}
	// 무릎을 둘 빈 쪽이 엉덩이→발목 원 위에 한 군데는 있어야 한다(SolveLeg가 거기로 비킨다).
	FHitResult Block;
	const FVector Ankle = AnkleFor(Leg, Pad, Normal, Hip, Rotation);
	const FVector Pole = (Rotation.RotateVector(RestOutward[Leg]) * 0.35f + Rotation.GetUpVector() * 0.9f).GetSafeNormal();
	const IGLining::FKneeCircle Circle = IGLining::KneeCircle(Hip, Ankle, Length1[Leg], Length2[Leg], Pole);
	for (int32 Step = 0; Step < 16; ++Step)
	{
		const FVector Knee = Circle.At(TWO_PI * Step / 16.0f);
		if (!World->LineTraceSingleByObjectType(Block, Hip, Knee, Objects, Params)
			&& !World->LineTraceSingleByObjectType(Block, Knee, Ankle, Objects, Params)
			&& !IGLining::SegmentCrossesDoor(Door, Hip, Knee, DoorMargin)
			&& !IGLining::SegmentCrossesDoor(Door, Knee, Ankle, DoorMargin))
		{
			return true;
		}
	}
	return false;
}

bool AIGLiningEntity::IsInsideGapCavity(const FVector& Pad) const
{
	for (const FIGLiningGap& Gap : Gaps)
	{
		// 벽 면에서 3 cm 넘게 뒤이고 개구부 둘레 40 cm 안이면 점검구 속이다.
		const float Behind = FVector::DotProduct(Gap.Center - Pad, Gap.Normal);
		if (Behind <= 3.0f || Behind >= 150.0f)
		{
			continue;
		}
		const FVector Across = FVector::CrossProduct(Gap.Normal, Gap.Up).GetSafeNormal();
		const float Reach = FMath::Max(Gap.Size.X, Gap.Size.Y) * 0.5f + 40.0f;
		if (FMath::Abs(FVector::DotProduct(Pad - Gap.Center, Across)) <= Reach
			&& FMath::Abs(FVector::DotProduct(Pad - Gap.Center, Gap.Up)) <= Reach)
		{
			return true;
		}
	}
	return false;
}

bool AIGLiningEntity::CrossesOpenHatchDoor(const FVector& Hip, const FVector& Pad) const
{
	constexpr float DoorMargin = 2.0f + IGLining::LegRadius * 0.5f;
	for (const FIGLiningGap& Gap : Gaps)
	{
		if (IGLining::SegmentCrossesDoor(IGLining::OpenDoorPlane(Gap.Hatch.Get()), Hip, Pad, DoorMargin))
		{
			return true;
		}
	}
	return false;
}

void AIGLiningEntity::UpdateHatchOpenLimits()
{
	const bool bActive = State != EIGLiningState::Hidden && BodyBlocker;
	const FVector Extent = bActive ? BodyBlocker->GetScaledBoxExtent() - FVector(3.0f) : FVector::ZeroVector;
	const FVector Center = bActive ? BodyBlocker->GetComponentLocation() : FVector::ZeroVector;
	const FQuat Rotation = bActive ? BodyBlocker->GetComponentQuat() : FQuat::Identity;
	const float Open = AIGAccessHatch::GetOpenYaw();
	for (const FIGLiningGap& Gap : Gaps)
	{
		AIGAccessHatch* Hatch = Gap.Hatch.Get();
		if (!Hatch)
		{
			continue;
		}
		if (!bActive || !Hatch->IsOpen() || Extent.GetMin() <= 0.0f)
		{
			Hatch->ClearOpenLimit();
			continue;
		}
		const auto Touches = [Hatch, &Center, &Rotation, &Extent](const float Yaw)
		{
			IGLining::FDoorPlane Door;
			Hatch->GetDoorPlane(Yaw, Door.Hinge, Door.Along, Door.Up, Door.Width, Door.Height);
			Door.bValid = Door.Width > 0.0f && Door.Height > 0.0f;
			return IGLining::DoorDepthInBox(Door, Center, Rotation, Extent) > 0.0f;
		};
		if (!Touches(Open))
		{
			Hatch->ClearOpenLimit();
			continue;
		}
		// 이미 다 열린 문짝은 밀지 않는다. 여는 중이거나 몸판에 걸려 멈춘 문짝만 닿는 각 바로 앞에 세운다.
		if (!Hatch->HasOpenLimit() && Hatch->GetDoorYaw() >= Open - 0.5f)
		{
			continue;
		}
		float Limit = Open;
		while (Limit > 0.0f && Touches(Limit))
		{
			Limit -= 2.0f;
		}
		Hatch->SetOpenLimit(FMath::Max(Limit - 1.0f, 0.0f));
	}
}

bool AIGLiningEntity::ChooseBracePad(const int32 Leg, const FIGLiningGap& Gap, const FGapPose& Pose, FVector& OutPad,
	FVector& OutNormal, FBraceSwing* OutSwing) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningBrace), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	if (AIGAccessHatch* Hatch = Gap.Hatch.Get())
	{
		Params.AddIgnoredActor(Hatch);
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	const FVector N = Pose.Rotation.GetUpVector();
	// 틈 둘레, 틈 가장자리에서 BraceMargin만큼 바깥의 벽.
	const FVector Spot = Pose.Anchor
		+ Pose.Forward * IGLining::LegSx[Leg] * (Pose.GapHalf.X + IGLining::BraceMarginAlong)
		+ Pose.Side * IGLining::LegSy[Leg] * (Pose.GapHalf.Y + IGLining::BraceMarginAcross);
	// 쓸 수 있는 자리를 다 모은 뒤, 기본 점수 순으로 무릎 둘 자리 벌점(KneeRoomPenalty)을 더해 고른다. 예전에는 둘레 자리가
	// 쓸 만하면 바로 짚고 아니면 기본 점수만 보았다. 302호 큰 점검구 아래 다리는 엉덩이에서 24 cm 자리를 짚어 무릎 원 전체가
	// 바닥 속이었다(넓적다리 50 cm).
	struct FBraceCandidate
	{
		FVector Pad;
		FVector Normal;
		float Score;
	};
	TArray<FBraceCandidate, TInlineAllocator<64>> Candidates;
	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit, Spot + N * 60.0f, Spot - N * 30.0f, Objects, Params) && !Hit.bStartPenetrating)
	{
		const FVector Pad = Hit.ImpactPoint + Hit.ImpactNormal * 2.0f;
		if (IsBracePadUsable(Leg, Gap, Pose, Pad, Hit.ImpactNormal))
		{
			Candidates.Add({Pad, Hit.ImpactNormal, 0.0f});
		}
	}
	// 둘레가 바닥 밑·옆 소품 속이거나 그리로 가는 길이 막혔다. 짚을 때 엉덩이에서 둘레 쪽으로 부채꼴로 쏴서 닿는 면
	// 가운데 둘레에 가깝고 다리를 덜 바꾸는 곳을 고른다. 벽 면을 따라 돌리고(Yaw), 방 쪽(+)이나 벽 쪽(-)으로 기울인다.
	const FVector Hip = HipWorld(Leg, Pose.Anchor + N * IGLining::BraceStandOff, Pose.Rotation);
	const FVector Toward = (Spot - Hip).GetSafeNormal();
	const float Desired = FVector::Distance(Hip, Spot);
	const float Reach = (Length1[Leg] + Length2[Leg]) * 0.95f;
	for (const float Yaw : {0.0f, 25.0f, -25.0f, 50.0f, -50.0f, 80.0f, -80.0f, 115.0f, -115.0f})
	{
		const FVector Turned = FQuat(N, FMath::DegreesToRadians(Yaw)).RotateVector(Toward);
		const FVector TiltAxis = FVector::CrossProduct(Turned, N).GetSafeNormal();
		for (const float Pitch : {0.0f, 20.0f, 40.0f, 60.0f, -15.0f})
		{
			const FVector Dir = TiltAxis.IsZero() ? Turned : FQuat(TiltAxis, FMath::DegreesToRadians(Pitch)).RotateVector(Turned);
			if (!World->LineTraceSingleByObjectType(Hit, Hip, Hip + Dir * Reach, Objects, Params) || Hit.bStartPenetrating
				|| Hit.Distance < IGLining::BraceMinDistance)
			{
				continue;
			}
			const FVector Pad = Hit.ImpactPoint + Hit.ImpactNormal * 2.0f;
			// 둘레에서 먼 자리일수록, 짚을 거리가 둘레와 다를수록, 틈이 난 벽이 아닌 면일수록 덜 고른다.
			const float Score = FVector::Distance(Pad, Spot) + 0.5f * FMath::Abs(Hit.Distance - Desired)
				+ (FVector::DotProduct(Hit.ImpactNormal, N) > 0.7f ? 0.0f : 12.0f);
			if (IsBracePadUsable(Leg, Gap, Pose, Pad, Hit.ImpactNormal))
			{
				Candidates.Add({Pad, Hit.ImpactNormal, Score});
			}
		}
	}
	// 몸을 돌려 대면 아래 두 다리의 엉덩이가 바닥에서 한 뼘 위다(안쪽 방·302호 큰 점검구). 둘레는 바닥 밑이고 접으면
	// 접힌 발이 바닥을 뚫는다. 벽 앞 방 쪽 바닥을 개구리 다리처럼 짚어 본다.
	FVector Flat = N - FVector::UpVector * FVector::DotProduct(N, FVector::UpVector);
	const FVector Along = FVector::CrossProduct(FVector::UpVector, Flat).GetSafeNormal();
	if (Flat.Normalize() && !Along.IsZero())
	{
		const float Lateral = FVector::DotProduct(Spot - Hip, Along) >= 0.0f ? 1.0f : -1.0f;
		for (const float Out : {45.0f, 65.0f, 85.0f})
		{
			for (const float Side : {0.0f, 25.0f, 50.0f})
			{
				const FVector Above = Hip + Flat * Out + Along * (Lateral * Side);
				if (!World->LineTraceSingleByObjectType(Hit, Above + FVector(0.0f, 0.0f, 40.0f), Above - FVector(0.0f, 0.0f, 200.0f),
						Objects, Params) || Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.7f)
				{
					continue;
				}
				const FVector Pad = Hit.ImpactPoint + Hit.ImpactNormal * 2.0f;
				if (IsBracePadUsable(Leg, Gap, Pose, Pad, Hit.ImpactNormal))
				{
					Candidates.Add({Pad, Hit.ImpactNormal, static_cast<float>(FVector::Distance(Pad, Spot)) + 12.0f});
				}
			}
		}
	}
	Candidates.Sort([](const FBraceCandidate& A, const FBraceCandidate& B) { return A.Score < B.Score; });
	const FVector PressHip = HipWorld(Leg, Pose.Anchor + N * IGLining::PressStandOff, Pose.Rotation);
	// 옮겨 가는 길도 본다. 짚을 자리만 보고 고르면 안쪽 방 큰 점검구 아래 1번 다리가 옮기는 길 가운데 세 자리에서 무릎 원
	// 24방향이 다 막혀, 넓적다리가 바닥을 28 cm 지났다(엉덩이가 바닥 12 cm 위다). 다리는 지금 짚은 자리를 떼어, 짚을 때 몸
	// 자리로 가는 몸을 따라 옮긴다. 비켜 돌기와 무릎 길은 옮길 때와 같은 수로 잰다.
	const FLeg& Current = Legs[Leg];
	const bool bMeasurePath = OutSwing && CanPlanKneePath(Leg);
	const FVector LiftHip = HipWorld(Leg, BodyCenter, BodyRotation);
	const FVector BraceHip = HipWorld(Leg, Pose.Anchor + N * IGLining::BraceStandOff, Pose.Rotation);
	int32 FullChecks = 0;
	float BestScore = TNumericLimits<float>::Max();
	bool bFound = false;
	for (const FBraceCandidate& Candidate : Candidates)
	{
		if (Candidate.Score >= BestScore)
		{
			break;
		}
		float Score = Candidate.Score + KneeRoomPenalty(Leg, PressHip, Pose.Rotation, Candidate.Pad, Candidate.Normal);
		if (Score >= BestScore)
		{
			continue;
		}
		FBraceSwing Swing;
		if (bMeasurePath)
		{
			// 무릎 길을 다 재는 자리(몸 기준 극이 내내 비지 않는 자리)는 점수 순으로 많아야 BracePathChecks개다. 하나에
			// 트레이스가 수백 번이다.
			if (FullChecks >= IGLining::BracePathChecks)
			{
				break;
			}
			FLeg Trial = Current;
			Trial.FromPad = Current.Pad;
			Trial.FromNormal = Current.Normal;
			Trial.ToPad = Candidate.Pad;
			Trial.ToNormal = Candidate.Normal.GetSafeNormal();
			Trial.FoldFrom = Current.Fold;
			Trial.FoldTo = 0.0f;
			Trial.LiftScale = 1.0f;
			Trial.SwingDetour = FVector::ZeroVector;
			Trial.SwingDetourPeak = 0.5f;
			if (FindSwingDetour(Leg, Trial, Swing.Detour, Swing.DetourPeak))
			{
				Trial.SwingDetour = Swing.Detour;
				Trial.SwingDetourPeak = Swing.DetourPeak;
			}
			MeasureKneePath(Leg, Trial, LiftHip, BodyRotation, BraceHip, Pose.Rotation, Swing.Path);
			FullChecks += Swing.Path.bDefaultClear ? 0 : 1;
			// 옮기는 동안 무릎을 둘 데가 아예 없는 자리만 센다. 길을 정하지 않으면 몸 기준 극을 쓴다. 가운데 선만 빈 자리나
			// 도는 각까지 점수로 치면 5층 배관 점검구 아래 다리가 길 점수 1점 남짓 차이로 둘레에서 먼 자리를 짚어, 짚는 동안
			// 다리 겉이 묻힌 프레임이 3에서 14로 늘었다.
			const bool bPlanTaken = !Swing.Path.bDefaultClear && Swing.Path.Score > Swing.Path.DefaultScore + 1.0f;
			const int32 Blocked = Swing.Path.bDefaultClear ? 0 : (bPlanTaken ? Swing.Path.Blocked : Swing.Path.DefaultBlocked);
			Score += Blocked * IGLining::BraceBlockedPenalty;
		}
		if (Score < BestScore)
		{
			BestScore = Score;
			OutPad = Candidate.Pad;
			OutNormal = Candidate.Normal;
			bFound = true;
			if (bMeasurePath)
			{
				*OutSwing = Swing;
			}
		}
	}
	return bFound;
}

bool AIGLiningEntity::RechooseBracePad(const int32 Index)
{
	FLeg& Leg = Legs[Index];
	const FIGLiningGap* Gap = FindGap(TargetGap);
	FVector Pad = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	FBraceSwing Swing;
	if (!Gap || !TargetPose.bValid || !CanPlanKneePath(Index) || !ChooseBracePad(Index, *Gap, TargetPose, Pad, Normal, &Swing))
	{
		return false;
	}
	Leg.ToPad = Pad;
	Leg.ToNormal = Normal.GetSafeNormal();
	Leg.SwingDetour = Swing.Detour;
	Leg.SwingDetourPeak = Swing.DetourPeak;
	// 고를 때 잰 무릎 길을 그대로 쓴다(PlanSwingPole과 같은 기준).
	Leg.bFlatPeel = LegReach(Index, BodyCenter, BodyRotation) < 0.3f;
	Leg.bSwingPlanned = !Swing.Path.bDefaultClear && Swing.Path.Score > Swing.Path.DefaultScore + 1.0f;
	for (int32 Sample = 0; Sample < IGLining::PoleSamples; ++Sample)
	{
		Leg.SwingPoles[Sample] = Swing.Path.Poles[Sample];
	}
	return true;
}

float AIGLiningEntity::KneeRoomPenalty(const int32 Leg, const FVector& Hip, const FQuat& Rotation, const FVector& Pad,
	const FVector& Normal) const
{
	// 발목이 엉덩이 곁이면 두 마디가 포개져 무릎 원이 커지고(반지름 80 cm 가까이) 구석에서는 둘 쪽이 없다. 무릎 원 24쪽 가운데
	// 굵기만큼 빈 쪽이 세 방향(45도) 이상 이어지지 않으면 무릎이 벽·바닥에 붙어 지낸다.
	const FVector Ankle = AnkleFor(Leg, Pad, Normal, Hip, Rotation);
	const float Reach = FVector::Distance(Hip, Ankle) / FMath::Max(Length1[Leg] + Length2[Leg], 1.0f);
	float Penalty = Reach < 0.35f ? (0.35f - Reach) * 150.0f : 0.0f;
	const FVector Pole = (Rotation.RotateVector(RestOutward[Leg]) * 0.35f + Rotation.GetUpVector() * 0.9f).GetSafeNormal();
	const IGLining::FKneeCircle Circle = IGLining::KneeCircle(Hip, Ankle, Length1[Leg], Length2[Leg], Pole);
	constexpr int32 Count = 24;
	int32 Rooms[Count];
	bool bAnyRoom = false;
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		Rooms[Slot] = LegRoom(Hip, Circle.At(TWO_PI * Slot / Count), Ankle, Normal);
		bAnyRoom |= Rooms[Slot] > 0;
	}
	int32 Run = 0;
	int32 BestRun = 0;
	for (int32 Slot = 0; Slot < Count * 2 && BestRun < Count; ++Slot)
	{
		Run = Rooms[Slot % Count] == 2 ? Run + 1 : 0;
		BestRun = FMath::Max(BestRun, Run);
	}
	Penalty += BestRun >= 3 ? 0.0f : (bAnyRoom ? 25.0f : 60.0f);
	return Penalty;
}

float AIGLiningEntity::FindBlockedHatchYaw(const FIGLiningGap& Gap, const FGapPose& Pose) const
{
	const AIGAccessHatch* Hatch = Gap.Hatch.Get();
	if (!Hatch)
	{
		return 38.0f;
	}
	// 플레이어가 닫으면 문짝은 다 연 자리에서 닫히는 쪽으로 돈다. 처음 몸판이나 짚은 다리에 닿는 각에서 멈춘다.
	// 몸판은 누른 몸(벽에서 0~40 cm), 다리는 마디마다 굵기(LegRadius)만큼 본다.
	TArray<FVector> Points;
	for (float Yaw = AIGAccessHatch::GetOpenYaw() - 2.0f; Yaw >= 8.0f; Yaw -= 2.0f)
	{
		Hatch->GetDoorSamplePoints(Yaw, Points);
		for (const FVector& Point : Points)
		{
			const FVector Local = Pose.Rotation.UnrotateVector(Point - Pose.Anchor);
			bool bTouch = FMath::Abs(Local.X) <= IGLining::BodyHalf.X + 2.0f && FMath::Abs(Local.Y) <= IGLining::BodyHalf.Y + 2.0f
				&& Local.Z >= 0.0f && Local.Z <= IGLining::PressStandOff * 2.0f;
			for (int32 Index = 0; Index < LegCount && !bTouch; ++Index)
			{
				const FLeg& L = Legs[Index];
				if (L.Fold > 0.5f)
				{
					continue;
				}
				const float Reach = IGLining::LegRadius + 2.0f;
				bTouch = FMath::PointDistToSegment(Point, L.Hip, L.Knee) < Reach
					|| FMath::PointDistToSegment(Point, L.Knee, L.Ankle) < Reach
					|| FMath::PointDistToSegment(Point, L.Ankle, L.Pad) < Reach;
			}
			if (bTouch)
			{
				return Yaw + 2.0f;
			}
		}
	}
	return 8.0f;
}

void AIGLiningEntity::PlaceHolding(const FName GapId)
{
	const FIGLiningGap* Gap = FindGap(GapId);
	if (!Gap || !LoadBody())
	{
		return;
	}
	// 덮개는 열린 채 몸에 걸려 있다. 문짝이 선 자리를 보고 몸을 맞추므로 먼저 연다.
	if (AIGAccessHatch* Hatch = Gap->Hatch.Get())
	{
		Hatch->ForceOpenState(true);
	}
	const FGapPose Pose = FitGapPose(*Gap);
	const FVector N = Pose.Rotation.GetUpVector();
	BodyRotation = Pose.Rotation;
	BodyCenter = Pose.Anchor + N * IGLining::PressStandOff;
	// 다리 넷은 틈 둘레를 짚고 있다(누를 때의 자리와 같다). 둘레가 바닥 밑이나 옆 소품 속이면 닿는 면을 다시 고르고,
	// 짚을 데가 없는 다리는 몸 밑에 접는다.
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		FVector Pad = FVector::ZeroVector;
		FVector Normal = N;
		const bool bTuck = Gap->bTuckLowerLegs && !Pose.bTurned && IGLining::LegSx[Leg] < 0.0f;
		const bool bPlanted = !bTuck && ChooseBracePad(Leg, *Gap, Pose, Pad, Normal);
		if (!bPlanted)
		{
			Pad = FoldedPadWorld(Leg, BodyCenter, BodyRotation);
			Normal = N;
		}
		FLeg& L = Legs[Leg];
		const float Fold = bPlanted ? 0.0f : 1.0f;
		L.Pad = Pad;
		L.Normal = Normal;
		L.FromPad = Pad;
		L.ToPad = Pad;
		L.FromNormal = Normal;
		L.ToNormal = Normal;
		L.bSwinging = false;
		L.SwingAlpha = 1.0f;
		L.Fold = Fold;
		L.FoldFrom = Fold;
		L.FoldTo = Fold;
	}
	bHasMoved = true;
	TargetGap = NAME_None;
	HeldGap = GapId;
	HeldPose = Pose;
	ResetKneeLean();
	for (FIGLiningGap& Existing : Gaps)
	{
		if (Existing.Id == GapId)
		{
			Existing.bSealed = true;
			if (AIGAccessHatch* Hatch = Existing.Hatch.Get())
			{
				Hatch->SetSealed(true);
			}
		}
	}
	SetActorHiddenInGame(false);
	SetBlockersEnabled(true);
	SetState(EIGLiningState::Holding);
	HoldingCheckSeconds = 0.0f;
	bGapsDirty = false;
	ResetSoftParts();
	PoseBody();
	// 문짝이 걸리는 각은 다리를 놓은 뒤에 잰다.
	SetObstructions(GapId, true, &HeldPose);
	StartTicking();
	UE_LOG(LogIndieGame, Display, TEXT("LINING_PLACED %s center=%s"), *GapId.ToString(), *BodyCenter.ToCompactString());
}

void AIGLiningEntity::SetGap(const FIGLiningGap& Gap)
{
	for (FIGLiningGap& Existing : Gaps)
	{
		if (Existing.Id == Gap.Id)
		{
			const bool bSealed = Existing.bSealed;
			Existing = Gap;
			Existing.bSealed = Existing.bSealed || bSealed;
			bGapsDirty = true;
			return;
		}
	}
	Gaps.Add(Gap);
	bGapsDirty = true;
	RefreshGapDrafts();
}

void AIGLiningEntity::ClearGaps()
{
	Gaps.Reset();
	TargetGap = NAME_None;
	HeldGap = NAME_None;
	RefreshGapDrafts();
}

const FIGLiningGap* AIGLiningEntity::FindGap(const FName Id) const
{
	return Gaps.FindByPredicate([Id](const FIGLiningGap& Gap) { return Gap.Id == Id; });
}

void AIGLiningEntity::NotifyGapsChanged()
{
	bGapsDirty = true;
	RefreshGapDrafts();
	if (State == EIGLiningState::Folded || State == EIGLiningState::Holding)
	{
		StartTicking();
	}
}

void AIGLiningEntity::UnsealGap(const FName Id)
{
	for (FIGLiningGap& Gap : Gaps)
	{
		if (Gap.Id == Id)
		{
			Gap.bSealed = false;
			if (AIGAccessHatch* Hatch = Gap.Hatch.Get())
			{
				Hatch->SetSealed(false);
			}
		}
	}
	NotifyGapsChanged();
}

bool AIGLiningEntity::IsGapOpen(const FName Id) const
{
	const FIGLiningGap* Gap = FindGap(Id);
	if (!Gap || Gap->bSealed)
	{
		return false;
	}
	if (Gap->bAlwaysOpen)
	{
		return true;
	}
	const AIGAccessHatch* Hatch = Gap->Hatch.Get();
	return Hatch && Hatch->IsGapOpen();
}

bool AIGLiningEntity::IsAnyBusy(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AIGLiningEntity> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->IsBusy())
		{
			return true;
		}
	}
	return false;
}

bool AIGLiningEntity::IsMoving() const
{
	switch (State)
	{
	case EIGLiningState::Peeling:
	case EIGLiningState::Stepping:
	case EIGLiningState::Bracing:
	case EIGLiningState::Pressing:
	case EIGLiningState::Retreating:
		return true;
	case EIGLiningState::Spread:
		return StateSeconds < MoveSeconds;
	default:
		return false;
	}
}

FVector AIGLiningEntity::GetPadLocation(const int32 Leg) const
{
	return Leg >= 0 && Leg < LegCount ? Legs[Leg].Pad : FVector::ZeroVector;
}

FVector AIGLiningEntity::GetKneeLocation(const int32 Leg) const
{
	return Leg >= 0 && Leg < LegCount ? Legs[Leg].Knee : FVector::ZeroVector;
}

FVector AIGLiningEntity::GetMassOffset(const int32 Quadrant) const
{
	return Quadrant >= 0 && Quadrant < LegCount ? Mass[Quadrant].Offset : FVector::ZeroVector;
}

bool AIGLiningEntity::IsSkinStuck(const int32 Leg) const
{
	return Leg >= 0 && Leg < LegCount && bSkinStuck[Leg];
}

float AIGLiningEntity::GetSkinStretch(const int32 Leg) const
{
	return IsSkinStuck(Leg) ? FVector::Distance(SkinStuckWorld[Leg].GetLocation(), DefaultSkinWorld(Leg).GetLocation()) : 0.0f;
}

float AIGLiningEntity::GetSkirtSpread(const int32 Side) const
{
	return Side >= 0 && Side < SkirtCount
		? FVector::DotProduct(Skirt[Side].Offset, BodyRotation.RotateVector(IGLining::SkirtSides[Side])) : 0.0f;
}

float AIGLiningEntity::GetSkirtDroop(const int32 Side) const
{
	return Side >= 0 && Side < SkirtCount ? -Skirt[Side].Offset.Z : 0.0f;
}

bool AIGLiningEntity::IsPadHolding(const int32 Leg) const
{
	return Leg >= 0 && Leg < LegCount && (!Legs[Leg].bSwinging || Legs[Leg].SwingAlpha <= 0.0f);
}

void AIGLiningEntity::SetState(const EIGLiningState NewState)
{
	if (State == NewState)
	{
		return;
	}
	UE_LOG(LogIndieGame, Display, TEXT("LINING_STATE %d -> %d target=%s held=%s"),
		static_cast<int32>(State), static_cast<int32>(NewState), *TargetGap.ToString(), *HeldGap.ToString());
	State = NewState;
	StateSeconds = 0.0f;
	RefreshGapDrafts();
	if (State == EIGLiningState::Hidden)
	{
		// 숨으면 틱이 멈추니 몸판에 걸려 멈춰 있던 문짝을 여기서 놓는다.
		UpdateHatchOpenLimits();
	}
}

void AIGLiningEntity::StartTicking()
{
	if (State != EIGLiningState::Hidden)
	{
		SetActorTickEnabled(true);
	}
}

bool AIGLiningEntity::ChooseTarget()
{
	// 누르고 있는 틈이 있으면 그보다 넓은 틈만 본다. 예전에는 누르는 틈만 빼고 골라서, 둘째 밤 안쪽 방에서
	// 큰 점검구를 먼저 열고 작은 것을 열면 작은 쪽으로 옮겨 갔다. 힌트와 302호 풀이는 틈의 크기를 말한다.
	const FIGLiningGap* Held = HeldGap.IsNone() ? nullptr : FindGap(HeldGap);
	const FIGLiningGap* Best = nullptr;
	float BestDistance = 0.0f;
	for (const FIGLiningGap& Gap : Gaps)
	{
		if (Gap.Id == HeldGap || !IsGapOpen(Gap.Id) || (Held && Gap.Area() <= Held->Area() + 1.0f))
		{
			continue;
		}
		const float Distance = FVector::Distance(Gap.Center, BodyCenter);
		// 넓은 틈이 먼저다. 넓이가 같으면 가까운 쪽.
		if (!Best || Gap.Area() > Best->Area() + 1.0f
			|| (FMath::IsNearlyEqual(Gap.Area(), Best->Area(), 1.0f) && Distance < BestDistance))
		{
			Best = &Gap;
			BestDistance = Distance;
		}
	}
	if (!Best)
	{
		return false;
	}
	BeginTravelTo(Best->Id);
	return true;
}

void AIGLiningEntity::BeginTravelTo(const FName GapId)
{
	const FIGLiningGap* Gap = FindGap(GapId);
	if (!Gap)
	{
		return;
	}
	if (!HeldGap.IsNone())
	{
		// 막아 둔 자리는 접착층이 눌린 채 남는다. 덮개는 이제 닫힌다.
		SetObstructions(HeldGap, false);
	}
	TargetGap = GapId;
	HintLeg = INDEX_NONE;
	TargetPose = FitGapPose(*Gap);
	const FVector GoalNormal = TargetPose.Rotation.GetUpVector();
	const FVector Goal = TargetPose.Anchor + GoalNormal * IGLining::BraceStandOff;
	PathStartRotation = BodyRotation;
	PathGoalRotation = TargetPose.Rotation;
	BuildPath(BodyCenter, BodyRotation.GetUpVector(), Goal, GoalNormal);
	PathTravelled = 0.0f;
	PhaseSeconds = 0.0f;
	BlockedPulls = 0;
	OnTargetChosen.Broadcast(this, GapId);
	UE_LOG(LogIndieGame, Display, TEXT("LINING_TARGET %s area=%.0f path=%.0f"), *GapId.ToString(), Gap->Area(), PathLength());
	if (!bHasMoved && State == EIGLiningState::Folded)
	{
		// 처음 움직인다. 목표 쪽 패드 하나가 벽에서 떨어진다.
		bHasMoved = true;
		const FVector Toward = (Goal - BodyCenter).GetSafeNormal();
		float BestDot = -2.0f;
		for (int32 Leg = 0; Leg < LegCount; ++Leg)
		{
			const FVector Out = BodyRotation.RotateVector(RestOutward[Leg]);
			const float Dot = FVector::DotProduct(Out, Toward);
			if (Dot > BestDot)
			{
				BestDot = Dot;
				PeelLeg = Leg;
			}
		}
		SetState(EIGLiningState::Peeling);
		MoveFromCenter = BodyCenter;
		PlayCue(TEXT("Lining_Peel"), 1, Legs[PeelLeg].Pad, 0.85f, 1.0f);
		double Dummy = -100.0;
		Caption(NSLOCTEXT("IGMissingFloor", "CaptionLiningPeel", "[무언가 벽에서 쩍 떨어지는 소리]"), Legs[PeelLeg].Pad, 2.6f, Dummy, 0.0f);
		OnFirstMove.Broadcast(this);
	}
	else
	{
		bHasMoved = true;
		SetState(EIGLiningState::Stepping);
		// 접혀 있던 다리는 먼저 편다. 짚고 있던 다리는 그대로 두고 당기기부터 한다.
		bool bAnyFolded = false;
		for (const FLeg& Leg : Legs)
		{
			bAnyFolded |= Leg.Fold > 0.5f;
		}
		bPulling = !bAnyFolded;
		PullFrom = 0.0f;
		PullTo = FMath::Min(IGLining::StepLength, PathLength());
		if (bAnyFolded)
		{
			for (int32 Leg = 0; Leg < LegCount; ++Leg)
			{
				FVector Pad, Normal;
				if (Legs[Leg].Fold > 0.5f && FindContact(Leg, BodyCenter, BodyRotation, (Goal - BodyCenter).GetSafeNormal(), Pad, Normal))
				{
					StartSwing(Leg, Pad, Normal, 0.0f);
					// 대각선 둘씩 편다. 먼저 편 둘이 면을 짚은 뒤에 나머지 둘이 펴진다.
					Legs[Leg].SwingAlpha = (Leg == 1 || Leg == 2) ? -1.0f : 0.0f;
				}
			}
		}
	}
	HeldGap = NAME_None;
	StartTicking();
}

void AIGLiningEntity::BuildPath(const FVector& Start, const FVector& StartUp, const FVector& Goal, const FVector& GoalNormal)
{
	const FVector N = GoalNormal.GetSafeNormal();
	// 다른 면으로 건너갈 때는 벽에서 몸을 떼어 방 쪽으로 나왔다가, 목표 틈 앞에서 벽 쪽으로 들어간다.
	const FVector P0 = Start;
	FVector P1 = Start + StartUp.GetSafeNormal() * 55.0f;
	FVector P2 = Goal + N * 70.0f;
	const FVector P3 = Goal;
	// 목표 틈이 난 벽 면에서 출발점까지의 높이. 같은 벽에 붙어 있었으면 누를 때(21)나 버틸 때(36) 높이다.
	const float StartHeight = FVector::DotProduct(Start - Goal, N) + IGLining::BraceStandOff;
	bPathAlongWall = FVector::DotProduct(StartUp.GetSafeNormal(), N) > 0.97f
		&& FMath::Abs(StartHeight - IGLining::BraceStandOff) < 30.0f;
	PathWallPoint = Goal - N * IGLining::BraceStandOff;
	PathWallNormal = N;
	if (bPathAlongWall)
	{
		// 같은 벽 위의 틈이면 짚고 버틸 만큼만 떼고 벽 면을 따라 기어간다. 방 쪽으로 부풀어 가면 몸판이
		// 벽에서 70 cm 넘게 떠서 벽 앞에 서서 일하는 사람을 덮친다(5층 이음매→점검구 277 cm).
		P1 = Start + N * (IGLining::BraceStandOff - StartHeight);
		P2 = Goal;
	}
	constexpr int32 Samples = 24;
	const auto Sample = [](const FVector& A, const FVector& B, const FVector& C, const FVector& D, TArray<FVector>& Out)
	{
		Out.Reset();
		for (int32 Index = 0; Index <= Samples; ++Index)
		{
			const float T = static_cast<float>(Index) / Samples;
			const float U = 1.0f - T;
			Out.Add(A * (U * U * U) + B * (3.0f * U * U * T) + C * (3.0f * U * T * T) + D * (T * T * T));
		}
	};
	TArray<FVector> Best;
	Sample(P0, P1, P2, P3, Best);
	if (!bPathAlongWall)
	{
		// 다른 면으로 건너가는 길은 몸판이 지나는 자리를 보고 고른다. 그대로 그리면 관리실 작은 점검구 앞에서 작업대를
		// 지나갔다. 같은 길을 위로 띄운 것과, 목표 틈 앞에서 방 쪽으로 더 나왔다가 곧게 들어가는 것을 견줘 확실히 덜
		// 지날 때만(두 점 이상) 바꾼다. 안쪽 방 큰 점검구는 서쪽에서 벽을 따라 오면 104도로 열린 문짝(벽에서 78 cm)을
		// 몸판이 85 cm 동안 지나서, 문짝 끝을 돌아 들어가야 했다. 방 쪽으로 나온 길은 문간에 선 사람을 덮칠 수 있어
		// 사람을 지나는 점도 문짝처럼 무겁게 센다(덧댄 것 검사의 문간 사람).
		const FIGLiningGap* Gap = TargetGap.IsNone() ? nullptr : FindGap(TargetGap);
		const AIGAccessHatch* Hatch = Gap ? Gap->Hatch.Get() : nullptr;
		int32 BestHits = CountPathObstruction(Best, Hatch);
		const FVector StartOut = StartUp.GetSafeNormal();
		const FVector Variants[][2] = {
			{Start + StartOut * 55.0f + FVector(0.0f, 0.0f, 40.0f), Goal + N * 70.0f + FVector(0.0f, 0.0f, 40.0f)},
			{Start + StartOut * 55.0f + FVector(0.0f, 0.0f, 80.0f), Goal + N * 70.0f + FVector(0.0f, 0.0f, 80.0f)},
			{Start + StartOut * 55.0f, Goal + N * 130.0f},
			{Start + StartOut * 55.0f, Goal + N * 190.0f},
			{Start + StartOut * 55.0f + FVector(0.0f, 0.0f, 40.0f), Goal + N * 130.0f + FVector(0.0f, 0.0f, 40.0f)},
			{Start + StartOut * 55.0f + FVector(0.0f, 0.0f, 80.0f), Goal + N * 130.0f + FVector(0.0f, 0.0f, 80.0f)},
			{Start + StartOut * 55.0f + FVector(0.0f, 0.0f, 40.0f), Goal + N * 190.0f + FVector(0.0f, 0.0f, 40.0f)},
			{Start + StartOut * 55.0f + FVector(0.0f, 0.0f, 80.0f), Goal + N * 190.0f + FVector(0.0f, 0.0f, 80.0f)},
		};
		TArray<FVector> Points;
		FString Scores = FString::Printf(TEXT("base=%d(%s)"), BestHits, *LastPathCost);
		int32 Chosen = -1;
		int32 VariantIndex = 0;
		for (const auto& Controls : Variants)
		{
			if (BestHits == 0)
			{
				break;
			}
			Sample(P0, Controls[0], Controls[1], P3, Points);
			const int32 Hits = CountPathObstruction(Points, Hatch);
			Scores += FString::Printf(TEXT(" v%d=%d(%s)"), VariantIndex, Hits, *LastPathCost);
			if (Hits + 1 < BestHits)
			{
				BestHits = Hits;
				Best = Points;
				Chosen = VariantIndex;
			}
			++VariantIndex;
		}
		UE_LOG(LogIndieGame, Display, TEXT("LINING_PATH target=%s chosen=%d %s"), *TargetGap.ToString(), Chosen, *Scores);
	}
	PathPoints.Reset();
	PathDistances.Reset();
	float Total = 0.0f;
	for (const FVector& Point : Best)
	{
		if (!PathPoints.IsEmpty())
		{
			Total += FVector::Distance(PathPoints.Last(), Point);
		}
		PathPoints.Add(Point);
		PathDistances.Add(Total);
	}
}

int32 AIGLiningEntity::CountPathObstruction(const TArray<FVector>& Points, const AIGAccessHatch* Hatch) const
{
	UWorld* World = GetWorld();
	if (!World || Points.Num() < 2)
	{
		return 0;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningPathCheck), false, this);
	const UPrimitiveComponent* PlayerBody = nullptr;
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
		PlayerBody = Cast<UPrimitiveComponent>(PlayerPawn->GetRootComponent());
	}
	for (const FIGLiningGap& Gap : Gaps)
	{
		if (AIGAccessHatch* Each = Gap.Hatch.Get())
		{
			Params.AddIgnoredActor(Each);
		}
	}
	const IGLining::FDoorPlane Door = IGLining::OpenDoorPlane(Hatch);
	// 몸판 상자(가장자리와 벽에 붙는 바닥면 3 cm는 뺀다). 마지막 몇 점은 짚고 누를 자리라 FitGapPose가 이미 봤다.
	const FVector Half = IGLining::BodyHalf - FVector(3.0f);
	const FCollisionShape Shape = FCollisionShape::MakeBox(Half);
	// 사람은 15 cm 띄워 본다. 다리가 몸판 밖으로 나오고, 사람은 그사이에 조금 움직인다.
	const FCollisionShape Near = FCollisionShape::MakeBox(Half + FVector(15.0f));
	int32 DoorHits = 0;
	int32 PlayerHits = 0;
	int32 WorldHits = 0;
	int32 HipHits = 0;
	const FCollisionShape HipShape = FCollisionShape::MakeSphere(IGLining::LegRadius);
	// 몸은 길을 지나온 거리 비율만큼 돈다(UpdateStepping). 예전에는 곡선 매개변수로 돌려 봐서, 처음과 끝이 촘촘한 곡선에서는
	// 점마다 실제보다 덜 돌거나 더 돈 몸을 쟀다. 옆으로 돌아가는 길이 점수로는 낫고 실제로는 벽을 더 지나간 것도 이 때문이다.
	TArray<float, TInlineAllocator<32>> Travelled;
	Travelled.Add(0.0f);
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		Travelled.Add(Travelled.Last() + FVector::Distance(Points[Index - 1], Points[Index]));
	}
	const float Length = FMath::Max(Travelled.Last(), KINDA_SMALL_NUMBER);
	for (int32 Index = 1; Index < Points.Num() - 1; ++Index)
	{
		const FQuat Rotation = BodyRotationAt(Travelled[Index] / Length);
		// 문짝과 사람은 끝 바로 앞 점까지 본다. 문짝은 틈 바로 옆에 서서, 마지막에 곧게 들어가는 몫에서 걸린다.
		DoorHits += IGLining::DoorDepthInBox(Door, Points[Index], Rotation, Half) > 0.0f ? 1 : 0;
		PlayerHits += PlayerBody && PlayerBody->OverlapComponent(Points[Index], Rotation, Near) ? 1 : 0;
		if (Index < Points.Num() - 2)
		{
			WorldHits += World->OverlapAnyTestByObjectType(Points[Index], Rotation, FCollisionObjectQueryParams(ECC_WorldStatic), Shape, Params)
				? 1 : 0;
		}
		// 몸판 상자는 가장자리 3 cm를 빼고 보니, 모서리 엉덩이가 벽 속을 지나도 몰랐다(덧댄 것 검사 302호). 몸판이 문짝을
		// 비켜 가도 엉덩이 바로 옆에 선 문짝을 넓적다리가 지났다. 엉덩이와, 쉬는 자세의 무릎 쪽으로 뻗은 넓적다리 선을
		// 벽·소품과 문짝에 대 본다.
		const FVector Up = Rotation.GetUpVector();
		for (int32 Leg = 0; Leg < LegCount; ++Leg)
		{
			const FVector Hip = HipWorld(Leg, Points[Index], Rotation);
			const FVector Thigh = Hip + Rotation.RotateVector(RestOutward[Leg]) * 45.0f + Up * 25.0f;
			FHitResult Block;
			if (Index < Points.Num() - 2
				&& (World->OverlapAnyTestByObjectType(Hip, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), HipShape, Params)
					|| World->LineTraceSingleByObjectType(Block, Hip, Thigh, FCollisionObjectQueryParams(ECC_WorldStatic), Params)))
			{
				++HipHits;
			}
			if (IGLining::DoorDepthInBox(Door, Hip, Rotation, FVector(IGLining::LegRadius)) > 0.0f
				|| IGLining::SegmentCrossesDoor(Door, Hip, Thigh, IGLining::LegRadius))
			{
				++DoorHits;
			}
		}
	}
	LastPathCost = FString::Printf(TEXT("d%d p%d w%d h%d"), DoorHits, PlayerHits, WorldHits, HipHits);
	return (DoorHits + PlayerHits) * 10 + WorldHits + HipHits;
}

FVector AIGLiningEntity::PathPoint(const float Distance) const
{
	if (PathPoints.IsEmpty())
	{
		return BodyCenter;
	}
	if (Distance <= 0.0f)
	{
		return PathPoints[0];
	}
	for (int32 Index = 1; Index < PathPoints.Num(); ++Index)
	{
		if (Distance <= PathDistances[Index])
		{
			const float Span = FMath::Max(PathDistances[Index] - PathDistances[Index - 1], KINDA_SMALL_NUMBER);
			return FMath::Lerp(PathPoints[Index - 1], PathPoints[Index], (Distance - PathDistances[Index - 1]) / Span);
		}
	}
	return PathPoints.Last();
}

FQuat AIGLiningEntity::BodyRotationAt(const float Alpha) const
{
	return FQuat::Slerp(PathStartRotation, PathGoalRotation, IGLining::Smooth(Alpha));
}

FVector AIGLiningEntity::HipWorld(const int32 Leg, const FVector& Center, const FQuat& Rotation) const
{
	return Center + Rotation.RotateVector(HipLocal[Leg]);
}

bool AIGLiningEntity::FindContact(
	const int32 Leg,
	const FVector& FutureBody,
	const FQuat& FutureRotation,
	const FVector& Travel,
	FVector& OutPad,
	FVector& OutNormal) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FVector Hip = HipWorld(Leg, FutureBody, FutureRotation);
	const float Reach = Length1[Leg] + Length2[Leg] + 18.0f;
	const float Ideal = Reach * 0.72f;
	// 가는 틈의 덮개 문짝도 비킨다(다 열린 자리로 본다).
	const FIGLiningGap* TargetGapInfo = TargetGap.IsNone() ? nullptr : FindGap(TargetGap);
	const AIGAccessHatch* TargetHatch = TargetGapInfo ? TargetGapInfo->Hatch.Get() : nullptr;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningContact), false, this);
	if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(Player);
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	const FVector Side = FutureRotation.RotateVector(FVector(IGLining::LegSx[Leg] * 0.55f, IGLining::LegSy[Leg] * 0.85f, 0.0f)).GetSafeNormal();
	const FVector Up = FutureRotation.GetUpVector();
	const FVector Ahead = Travel.GetSafeNormal() * (IGLining::LegSx[Leg] > 0.0f ? 0.65f : 0.15f);
	// 옆 벽 → 천장 쪽 → 바닥 쪽 순으로 짚을 데를 찾는다.
	const FVector Candidates[] = {
		(Side + Ahead + Up * 0.25f).GetSafeNormal(),
		(Side + Ahead + Up * 0.9f).GetSafeNormal(),
		(Side * 0.6f + Ahead + Up * 1.4f).GetSafeNormal(),
		(Side + Ahead - Up * 0.5f).GetSafeNormal(),
		(Side * 0.4f + Ahead - Up * 1.2f).GetSafeNormal(),
		(Side * 0.6f - Ahead * 0.4f - FVector::UpVector).GetSafeNormal(),
	};
	// 같은 벽을 따라가는 동안은 그 벽 가까이만 짚는다. 벽에 붙은 몸에서 위쪽 후보는 방 쪽이라 그대로 두면
	// 1 m 넘게 떨어진 천장이나 바닥을 짚고, 열린 점검구 안으로 벽 속을 짚기도 한다.
	const bool bWalking = State == EIGLiningState::Peeling || State == EIGLiningState::Stepping;
	const bool bHugWall = bPathAlongWall && bWalking;
	const auto OffWall = [this, bHugWall](const FVector& Pad)
	{
		const float Height = FVector::DotProduct(Pad - PathWallPoint, PathWallNormal);
		return bHugWall && (Height > IGLining::AlongWallPadReach || Height < -IGLining::AlongWallPadBehind);
	};
	// 앞으로 갈 몸 자리를 보고 고르되, 지금 몸에서도 닿아야 한다. 안 그러면 발이 면에 닿지 못하고 몸이 따라올 때까지
	// 허공에 떠 있다. 걷는 동안에는 발목이 지금 엉덩이 바로 옆이나 밑에 오는 자리도 짚지 않는다.
	const float MinReach = bWalking ? IGLining::StepHipClear / FMath::Max(Length1[Leg] + Length2[Leg], 1.0f) : 0.0f;
	const auto ReachableNow = [this, Leg, MinReach](const FVector& Pad, const FVector& Normal)
	{
		const float Reach = ReachTo(Leg, Pad, Normal, BodyCenter, BodyRotation);
		return Reach <= 0.96f && Reach >= MinReach;
	};
	// 걷는 동안 아래로 내려다보고 찾은 자리는 엉덩이에서 막힘 없이 보여야 한다. 천장 가까이 벽에 붙어 있으면 30 cm 위에서
	// 내려다보는 자리가 천장 판 속이라 판 윗면을 짚었다(5층 처음 자리, 왼쪽 앞다리).
	const auto InSight = [World, &Hip, &Objects, &Params](const FVector& Pad)
	{
		const FVector Toward = Pad - Hip;
		FHitResult Block;
		return Toward.Size() <= 6.0f
			|| !World->LineTraceSingleByObjectType(Block, Hip, Pad - Toward.GetSafeNormal() * 6.0f, Objects, Params);
	};
	// 열린 점검구 속(벽 면 뒤)과, 엉덩이에서 열린 문짝 너머인 자리는 짚지 않는다. 밤2 안쪽 방 큰 점검구로 가는 동안
	// 한 다리는 점검구 속 뒷벽을 짚어 정강이가 개구부 옆 벽을 뚫었고(69프레임), 다른 다리는 경첩 쪽 문짝 밑 바닥을
	// 짚어 정강이가 문짝을 지났다(45프레임). 지금 몸과 앞으로 갈 몸 두 엉덩이에서 다 본다.
	const FVector HipNow = HipWorld(Leg, BodyCenter, BodyRotation);
	const auto ClearOfHatches = [this, &Hip, &HipNow](const FVector& Pad)
	{
		return !IsInsideGapCavity(Pad) && !CrossesOpenHatchDoor(Hip, Pad) && !CrossesOpenHatchDoor(HipNow, Pad);
	};
	bool bFound = false;
	float BestScore = TNumericLimits<float>::Max();
	// 패드가 옆 소품이나 바닥 턱을 파고들지 않고 무릎을 둘 빈 쪽이 있는 자리(관리실 장갑 상자·간이침대·보관함 곁을 피함)를
	// 먼저 짚는다. 그런 데가 없거나 원래 고르던 자리보다 한참 나쁘면 원래 자리를 짚는다. 아무 데도 안 짚으면 몸을 당기다
	// 짚고 있던 발이 끌려간다(302호 북벽→서벽 모서리에서 27 cm).
	bool bRoughFound = false;
	float RoughScore = TNumericLimits<float>::Max();
	FVector RoughPad = FVector::ZeroVector;
	FVector RoughNormal = FVector::UpVector;
	for (const FVector& Direction : Candidates)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByObjectType(Hit, Hip, Hip + Direction * (Reach + 40.0f), Objects, Params))
		{
			continue;
		}
		const float Distance = FVector::Distance(Hip, Hit.ImpactPoint);
		if (Distance < 45.0f || Distance > Reach * 0.95f)
		{
			continue;
		}
		const FVector Pad = Hit.ImpactPoint + Hit.ImpactNormal * 2.0f;
		if (OffWall(Pad) || !ReachableNow(Pad, Hit.ImpactNormal) || !ClearOfHatches(Pad))
		{
			continue;
		}
		const float Score = FMath::Abs(Distance - Ideal);
		if (Score < RoughScore)
		{
			RoughScore = Score;
			RoughPad = Pad;
			RoughNormal = Hit.ImpactNormal;
			bRoughFound = true;
		}
		if (Score < BestScore && IsPadPlaceable(Leg, Hip, FutureRotation, Pad, Hit.ImpactNormal, TargetHatch))
		{
			BestScore = Score;
			OutPad = Pad;
			OutNormal = Hit.ImpactNormal;
			bFound = true;
		}
	}
	if (!bFound)
	{
		// 짚을 데가 없으면 엉덩이 바깥 바닥을 짚는다. 거기도 닿지 않으면 이번에는 옮기지 않는다.
		FHitResult Hit;
		const FVector Out = Hip + Side * 70.0f;
		if (World->LineTraceSingleByObjectType(Hit, Out + FVector(0, 0, 30.0f), Out - FVector(0, 0, 400.0f), Objects, Params)
			&& !OffWall(Hit.ImpactPoint + Hit.ImpactNormal * 2.0f)
			&& ClearOfHatches(Hit.ImpactPoint + Hit.ImpactNormal * 2.0f)
			&& ReachableNow(Hit.ImpactPoint + Hit.ImpactNormal * 2.0f, Hit.ImpactNormal)
			&& (!bWalking || InSight(Hit.ImpactPoint + Hit.ImpactNormal * 2.0f))
			&& (!bRoughFound
				|| IsPadPlaceable(Leg, Hip, FutureRotation, Hit.ImpactPoint + Hit.ImpactNormal * 2.0f, Hit.ImpactNormal, TargetHatch)))
		{
			OutPad = Hit.ImpactPoint + Hit.ImpactNormal * 2.0f;
			OutNormal = Hit.ImpactNormal;
			bFound = true;
		}
	}
	// 깨끗한 자리가 원래 고르던 자리보다 한참 나쁘면(20 cm 넘게) 원래 자리를 짚는다. 걸음새가 크게 바뀌어 늘 비켜
	// 서던 자리(302호 문간)까지 발이 뻗었다.
	if (bRoughFound && (!bFound || BestScore > RoughScore + 20.0f))
	{
		OutPad = RoughPad;
		OutNormal = RoughNormal;
		bFound = true;
	}
	return bFound;
}

void AIGLiningEntity::StartSwing(const int32 Leg, const FVector& Pad, const FVector& Normal, const float FoldTarget)
{
	FLeg& State_ = Legs[Leg];
	// 접힌 다리는 접힌 패드 자리에서 출발한다(SolveLeg가 접힌 동안 그 자리를 적어 둔다).
	State_.FromPad = State_.Pad;
	State_.FromNormal = State_.Normal;
	State_.ToPad = Pad;
	State_.ToNormal = Normal.GetSafeNormal();
	State_.FoldFrom = State_.Fold;
	State_.FoldTo = FoldTarget;
	State_.SwingAlpha = 0.0f;
	State_.bSwinging = true;
	State_.LandWaitSeconds = 0.0f;
	State_.LiftScale = 1.0f;
	State_.SwingDetour = FVector::ZeroVector;
	State_.SwingDetourPeak = 0.5f;
	State_.bSwingPlanned = false;
	State_.bFlatPeel = false;
	// 틈을 짚을 때도 옮기는 발이 제 엉덩이 곁을 지나면 무릎 원이 뒤집혀 한 프레임에 40~69 cm 튀었다(관리실 작은 점검구,
	// 302호 큰 점검구). 짚는 동안 몸은 길 끝 자리에 있으니 걸을 때처럼 비켜 돌 길을 미리 잡는다.
	if (State == EIGLiningState::Peeling || State == EIGLiningState::Stepping || State == EIGLiningState::Bracing)
	{
		PlanSwingDetour(Leg);
	}
	// 패드 막 끝은 짚었던 면에 붙어 남는다. 패드가 들리고 나서야 놓인다(UpdateSoftParts).
	if (bHasSoftBones && bSegmentsPosed)
	{
		SkinStuckWorld[Leg] = DefaultSkinWorld(Leg);
		bSkinStuck[Leg] = true;
		Skin[Leg] = FSoftPoint();
	}
}

FVector AIGLiningEntity::SwingPadAt(const FLeg& Leg, const float T, const FVector& Detour, const float Peak) const
{
	const FVector Lift = (Leg.FromNormal + Leg.ToNormal).GetSafeNormal() * IGLining::SwingLift * Leg.LiftScale * FMath::Sin(T * PI);
	return FMath::Lerp(Leg.FromPad, Leg.ToPad, T) + Lift + Detour * IGLining::DetourBump(T, Peak);
}

void AIGLiningEntity::PlanSwingDetour(const int32 Index)
{
	FLeg& Leg = Legs[Index];
	FVector Detour = FVector::ZeroVector;
	float Peak = 0.5f;
	if (FindSwingDetour(Index, Leg, Detour, Peak))
	{
		Leg.SwingDetour = Detour;
		Leg.SwingDetourPeak = Peak;
	}
}

bool AIGLiningEntity::FindSwingDetour(const int32 Index, const FLeg& Leg, FVector& OutDetour, float& OutPeak) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// 걷는 동안 스윙하는 사이에는 몸이 서 있으므로 엉덩이도 그 자리다. 무릎은 SolveLeg처럼 극으로 풀고 접힌 자세와
	// 섞는다(눕히기는 빼고 잰다).
	const FVector Hip = HipWorld(Index, BodyCenter, BodyRotation);
	const FVector Up = BodyRotation.GetUpVector();
	const FVector Pole = (BodyRotation.RotateVector(RestOutward[Index]) * 0.35f + Up * 0.9f).GetSafeNormal();
	FVector FoldKnee;
	FVector FoldAnkle;
	FVector FoldPad;
	IGLining::FoldedJoints(Index, HipLocal[Index], FoldKnee, FoldAnkle, FoldPad);
	FoldKnee = BodyCenter + BodyRotation.RotateVector(FoldKnee);
	constexpr int32 Samples = 20;
	const auto AnkleAt = [this, Index, &Leg, &Hip](const float T, const FVector& Detour, const float Peak)
	{
		return AnkleFor(Index, SwingPadAt(Leg, T, Detour, Peak), FMath::Lerp(Leg.FromNormal, Leg.ToNormal, T).GetSafeNormal(), Hip,
			BodyRotation);
	};
	// 옮기는 동안을 같은 시간 간격으로 나눠 무릎을 풀고, 한 간격에 무릎이 가장 많이 움직인 거리와 그 간격을 돌려준다.
	const auto KneeStep = [&](const FVector& Detour, const float Peak, int32& OutStep)
	{
		float Worst = 0.0f;
		FVector Previous = FVector::ZeroVector;
		for (int32 Sample = 0; Sample <= Samples; ++Sample)
		{
			const float T = IGLining::Smooth(static_cast<float>(Sample) / Samples);
			FVector Knee = IGLining::TwoBoneKnee(Hip, AnkleAt(T, Detour, Peak), Length1[Index], Length2[Index], Pole);
			const float Fold = FMath::Lerp(Leg.FoldFrom, Leg.FoldTo, T);
			if (Fold > 0.001f)
			{
				Knee = FMath::Lerp(Knee, FoldKnee, IGLining::Smooth(Fold));
			}
			Knee = Hip + (Knee - Hip).GetSafeNormal() * Length1[Index];
			if (Sample > 0 && FVector::Distance(Previous, Knee) > Worst)
			{
				Worst = FVector::Distance(Previous, Knee);
				OutStep = Sample - 1;
			}
			Previous = Knee;
		}
		return Worst;
	};
	const float Duration = (State == EIGLiningState::Peeling ? IGLining::PeelSeconds * 0.55f : IGLining::SwingSeconds) / PaceScale;
	const float Budget = IGLining::SwingKneeSpeed * Duration * (1.0f - PeelHoldFraction()) / Samples;
	int32 WorstStep = 0;
	const float Before = KneeStep(FVector::ZeroVector, 0.5f, WorstStep);
	if (Before <= Budget)
	{
		return false;
	}
	// 몸 면을 따라, 옮기는 방향에 수직으로 비킨다. 무릎이 가장 크게 튀는 때 발목이 엉덩이에서 벗어나 있는 쪽부터 보고,
	// 엉덩이 바로 밑을 지나면 다리 바깥쪽부터 본다. 벽에서 떨어지는 쪽(사람이 서 있는 방 쪽)으로는 비키지 않는다.
	const auto Planar = [&Up](const FVector& V) { return V - Up * FVector::DotProduct(V, Up); };
	FVector Along = Planar(Leg.ToPad - Leg.FromPad);
	const bool bHasAlong = Along.Normalize();
	const auto Across = [&Planar, &Along, bHasAlong](const FVector& V)
	{
		const FVector Flat = Planar(V);
		return bHasAlong ? Flat - Along * FVector::DotProduct(Flat, Along) : Flat;
	};
	FVector Away = Across(AnkleAt(IGLining::Smooth((WorstStep + 0.5f) / Samples), FVector::ZeroVector, 0.5f) - Hip);
	if (Away.Size() < 3.0f)
	{
		Away = Across(BodyRotation.RotateVector(RestOutward[Index]));
	}
	if (!Away.Normalize())
	{
		return false;
	}
	struct FDetour
	{
		FVector Offset;
		float Peak;
		float Push;
		float Step;
		/** 쪽과 가장 많이 비키는 비율의 묶음(0~9). 한 묶음에서 어떤 거리가 막히면 그보다 크게 비켜도 막힌다고 본다. */
		int32 Group;
	};
	constexpr int32 PeakCount = 5;
	constexpr float Peaks[PeakCount] = {0.25f, 0.35f, 0.45f, 0.55f, 0.65f};
	TArray<FDetour, TInlineAllocator<160>> Options;
	for (int32 SignIndex = 0; SignIndex < 2; ++SignIndex)
	{
		for (int32 PeakIndex = 0; PeakIndex < PeakCount; ++PeakIndex)
		{
			for (float Push = 5.0f; Push <= 80.0f; Push += 5.0f)
			{
				int32 Unused = 0;
				const FVector Offset = Away * ((SignIndex == 0 ? 1.0f : -1.0f) * Push);
				Options.Add({Offset, Peaks[PeakIndex], Push, KneeStep(Offset, Peaks[PeakIndex], Unused),
					SignIndex * PeakCount + PeakIndex});
			}
		}
	}
	// 무릎이 그 빠르기 안에 드는 길 가운데 가장 덜 비켜 도는 것부터, 그런 길이 없으면 가장 덜 튀는 것부터 쓴다.
	Options.Sort([Budget](const FDetour& A, const FDetour& B)
	{
		const bool bA = A.Step <= Budget;
		const bool bB = B.Step <= Budget;
		if (bA != bB)
		{
			return bA;
		}
		return bA ? (A.Push != B.Push ? A.Push < B.Push : A.Step < B.Step) : (A.Step != B.Step ? A.Step < B.Step : A.Push < B.Push);
	});
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningSwingDetour), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	// 패드가 지나는 길에서 벽·천장·바닥이나 소품에 막힌 구간 수.
	const auto BlockedSpans = [&](const FVector& Offset, const float Peak)
	{
		int32 Blocked = 0;
		FVector Previous = SwingPadAt(Leg, 1.0f / Samples, Offset, Peak);
		for (int32 Sample = 2; Sample < Samples; ++Sample)
		{
			const FVector Next = SwingPadAt(Leg, static_cast<float>(Sample) / Samples, Offset, Peak);
			FHitResult Hit;
			Blocked += World->LineTraceSingleByObjectType(Hit, Previous, Next, Objects, Params) ? 1 : 0;
			Previous = Next;
		}
		return Blocked;
	};
	// 비켜 돈 길이 원래 길보다 더 많이 막히면 다음 길을 본다. 원래 길이 벌써 구석을 스치면(5층 점검구 옆 남쪽 벽) 그만큼은
	// 둔다. 그러지 않으면 구석에서는 어떤 길도 못 써서 엉덩이 밑을 그대로 지나갔다.
	const int32 BlockedBefore = BlockedSpans(FVector::ZeroVector, 0.5f);
	float BlockedPush[2 * PeakCount];
	for (float& Push : BlockedPush)
	{
		Push = BIG_NUMBER;
	}
	// 막힘을 재는 길은 많아야 48개다(한 번에 트레이스 18번).
	int32 Tried = 0;
	for (const FDetour& Option : Options)
	{
		if (Option.Step >= Before - 0.5f || Tried >= 48)
		{
			break;
		}
		if (Option.Push >= BlockedPush[Option.Group])
		{
			continue;
		}
		++Tried;
		if (BlockedSpans(Option.Offset, Option.Peak) > BlockedBefore)
		{
			BlockedPush[Option.Group] = Option.Push;
		}
		else
		{
			OutDetour = Option.Offset;
			OutPeak = Option.Peak;
			return true;
		}
	}
	return false;
}

void AIGLiningEntity::UpdateSwings(const float DeltaSeconds)
{
	const float Duration = (State == EIGLiningState::Peeling ? IGLining::PeelSeconds * 0.55f
		: State == EIGLiningState::Spread ? FMath::Max(MoveSeconds * 0.5f, 0.6f)
		: IGLining::SwingSeconds) / PaceScale;
	bool bRechosePad = false;
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		FLeg& Leg = Legs[Index];
		if (!Leg.bSwinging)
		{
			continue;
		}
		// 걸을 때와 틈을 짚을 때는 늘 두 발이 짚고 있다. 먼저 간 발이 무릎을 기다리며 아직 공중에 있으면 다음 발은
		// 떼지 않고 기다린다(차례는 시간으로만 엇갈려 있어서 넷이 다 떠 있던 순간이 생겼다).
		const bool bLifting = Leg.SwingAlpha <= 0.0f && Leg.SwingAlpha + DeltaSeconds / Duration > 0.0f;
		if (bLifting && (State == EIGLiningState::Stepping || State == EIGLiningState::Bracing))
		{
			int32 Holding = 0;
			for (int32 Other = 0; Other < LegCount; ++Other)
			{
				Holding += Other != Index && IsPadHolding(Other) ? 1 : 0;
			}
			if (Holding < 2)
			{
				continue;
			}
		}
		if (bLifting
			&& (State == EIGLiningState::Peeling || State == EIGLiningState::Stepping || State == EIGLiningState::Bracing))
		{
			// 틈을 짚으러 드는 다리는 옮겨 가는 길까지 보고 짚을 자리를 다시 고른다. 후보마다 무릎 길을 재느라 오래 걸려서(네
			// 다리를 한 프레임에 고르면 18 ms) 한 프레임에 한 다리만 고르고 다른 다리는 다음 프레임에 든다.
			const bool bRechoose = State == EIGLiningState::Bracing && CanPlanKneePath(Index);
			if (bRechoose && bRechosePad)
			{
				continue;
			}
			const double PlanStarted = bMeasureLegs ? FPlatformTime::Seconds() : 0.0;
			bRechosePad |= bRechoose;
			if (!bRechoose || !RechooseBracePad(Index))
			{
				PlanSwingPole(Index);
			}
			if (bMeasureLegs)
			{
				const double Spent = FPlatformTime::Seconds() - PlanStarted;
				SolveFrameSeconds += Spent;
				PlanTotalSeconds += Spent;
				PlanMaxSeconds = FMath::Max(PlanMaxSeconds, Spent);
				++PlanCount;
				if (bRechoose)
				{
					BraceChooseMaxSeconds = FMath::Max(BraceChooseMaxSeconds, Spent);
				}
			}
		}
		Leg.SwingAlpha = FMath::Min(1.0f, Leg.SwingAlpha + DeltaSeconds / Duration);
		// 무릎이 무릎 빠르기에 막혀 아직 따라오는 중이면 디딜 자리 바로 위에서 기다린다. 늦은 채 디디면 짚은 패드가
		// 무릎을 따라 끌려갔다(19 cm). 오래 걸리면 그냥 디딘다.
		if (Leg.SwingAlpha >= 1.0f && Leg.bKneeCatchUp && Leg.LandWaitSeconds < IGLining::LandWaitSeconds)
		{
			Leg.LandWaitSeconds += DeltaSeconds;
			Leg.SwingAlpha = 0.999f;
		}
		// 밀려나며 접는 다리는 지금 몸의 접힌 자리로 간다. 도착할 몸의 접힌 자리로 뻗게 두면 0.8초 동안 3초 뒤의 천장 구석
		// 쪽으로 뻗었다가 몸에 접혀서, 그사이 정강이와 패드가 5층 철문 문설주를 지나갔다.
		if (State == EIGLiningState::Retreating && Leg.FoldTo > 0.999f)
		{
			Leg.ToPad = FoldedPadWorld(Index, BodyCenter, BodyRotation);
			Leg.ToNormal = BodyRotation.GetUpVector();
		}
		// 앞 몫 동안 패드는 제자리에서 몸 쪽 가장자리만 든다(SolveLeg). 막이 먼저 벌어진 뒤에 옮긴다.
		const float Hold = PeelHoldFraction();
		const float T = IGLining::Smooth((Leg.SwingAlpha - Hold) / (1.0f - Hold));
		Leg.Pad = SwingPadAt(Leg, T, Leg.SwingDetour, Leg.SwingDetourPeak);
		Leg.Normal = FMath::Lerp(Leg.FromNormal, Leg.ToNormal, T).GetSafeNormal();
		Leg.Fold = FMath::Lerp(Leg.FoldFrom, Leg.FoldTo, T);
		if (Leg.SwingAlpha >= 1.0f)
		{
			Leg.bSwinging = false;
			Leg.bSwingPlanned = false;
			Leg.bFlatPeel = false;
			Leg.Pad = Leg.ToPad;
			Leg.Normal = Leg.ToNormal;
			// 내려놓은 패드는 체중을 받아 한 번 더 눌렸다가 자리 잡는다. 더듬는 발은 가볍게 짚는다.
			Leg.PlantKick = Leg.LiftScale < 0.5f ? 0.45f : 1.0f;
			// 짚은 다리로 무게가 옮겨 가며 몸 살이 한 번 처졌다가 자리를 잡는다.
			KickSoftParts(Index, Leg.LiftScale < 0.5f ? 0.35f : 1.0f);
			PlayCue(TEXT("Lining_Plant"), 3, Leg.Pad, Leg.LiftScale < 0.5f ? 0.22f : 0.55f, FMath::FRandRange(0.92f, 1.06f));
		}
	}
}

void AIGLiningEntity::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StateSeconds += DeltaSeconds;
	switch (State)
	{
	case EIGLiningState::Hidden:
		SetActorTickEnabled(false);
		return;
	case EIGLiningState::Folded:
		HoldingCheckSeconds += DeltaSeconds;
		if (HoldingCheckSeconds >= IGLining::RestCheckSeconds || bGapsDirty)
		{
			HoldingCheckSeconds = 0.0f;
			bGapsDirty = false;
			ChooseTarget();
		}
		break;
	case EIGLiningState::Peeling:
		UpdatePeeling(DeltaSeconds);
		break;
	case EIGLiningState::Stepping:
		UpdateStepping(DeltaSeconds);
		break;
	case EIGLiningState::Bracing:
		UpdateBracing(DeltaSeconds);
		break;
	case EIGLiningState::Pressing:
		UpdatePressing(DeltaSeconds);
		break;
	case EIGLiningState::Holding:
		UpdateHolding(DeltaSeconds);
		break;
	case EIGLiningState::Spread:
		UpdateSpread(DeltaSeconds);
		break;
	case EIGLiningState::Retreating:
		UpdateRetreating(DeltaSeconds);
		break;
	case EIGLiningState::Gripping:
		break;
	}
	UpdateSwings(DeltaSeconds);
	if (State == EIGLiningState::Stepping || State == EIGLiningState::Bracing)
	{
		int32 Holding = 0;
		for (int32 Leg = 0; Leg < LegCount; ++Leg)
		{
			Holding += IsPadHolding(Leg) ? 1 : 0;
		}
		MinPadsHoldingWhileWalking = FMath::Min(MinPadsHoldingWhileWalking, Holding);
	}
	for (FLeg& Leg : Legs)
	{
		Leg.PlantKick = FMath::Max(0.0f, Leg.PlantKick - DeltaSeconds / 0.18f);
	}
	if (PinnedLeg != INDEX_NONE)
	{
		PinBlend = FMath::Min(1.0f, PinBlend + DeltaSeconds / IGLining::PinBlendSeconds);
	}
	// 몸의 눌림은 순간이동하지 않고 따라간다(누르기 끝에서 납작해지고, 당길 때 늘어난다).
	BodySquash = FMath::VInterpTo(BodySquash, ComputeBodySquash(), DeltaSeconds, 7.0f);
	// 같은 벽을 따라 걷는 동안에만 무릎을 벽 면 쪽으로 눕힌다. 걷기 시작하면 차오르고, 틈 둘레를 짚으면 빠진다.
	const auto IsWalking = [](const EIGLiningState InState)
	{
		return InState == EIGLiningState::Peeling || InState == EIGLiningState::Stepping;
	};
	const bool bLeanKnees = bPathAlongWall
		&& (IsWalking(State) || (State == EIGLiningState::Gripping && IsWalking(StateBeforeGrip)));
	KneeLean = FMath::FInterpConstantTo(KneeLean, bLeanKnees ? 1.0f : 0.0f, DeltaSeconds, 1.0f / IGLining::KneeLeanFadeSeconds);
	UpdateSoftParts(DeltaSeconds);
	PoseDeltaSeconds = DeltaSeconds;
	PoseBody();
	PoseDeltaSeconds = 0.0f;
	UpdateHatchOpenLimits();
	CheckGrip();
	// 쉬는 동안은 틈만 본다. 자세가 안 바뀌니 몇 틱에 한 번이면 된다. 처진 살이나 막이 아직 흔들리면 매 틱 본다.
	if ((State == EIGLiningState::Folded || State == EIGLiningState::Holding || State == EIGLiningState::Spread)
		&& !IsMoving())
	{
		bool bAnySwing = false;
		for (const FLeg& Leg : Legs)
		{
			bAnySwing |= Leg.bSwinging;
		}
		PrimaryActorTick.TickInterval = bAnySwing || !AreSoftPartsSettled() ? 0.0f : 0.1f;
	}
	else
	{
		PrimaryActorTick.TickInterval = 0.0f;
	}
}

void AIGLiningEntity::UpdatePeeling(const float DeltaSeconds)
{
	const float Seconds = IGLining::PeelSeconds / PaceScale;
	FLeg& Leg = Legs[PeelLeg];
	// 처음 0.35는 패드가 벽에 붙은 채 몸이 조금 떨어지고, 그다음 다리가 펴져 옆을 짚는다.
	const float Alpha = StateSeconds / Seconds;
	if (Alpha < 0.35f)
	{
		// 몸이 벽에서 한 뼘도 안 되게 떨어지고, 붙어 있던 패드가 들린다.
		const float Lift = IGLining::Smooth(Alpha / 0.35f);
		BodyCenter = MoveFromCenter + BodyRotation.GetUpVector() * 5.0f * Lift;
		Leg.Fold = FMath::Lerp(1.0f, 0.86f, Lift);
	}
	else if (!Leg.bSwinging && Leg.Fold > 0.5f)
	{
		const FIGLiningGap* Gap = FindGap(TargetGap);
		const FVector Toward = Gap ? (Gap->Center - BodyCenter).GetSafeNormal() : BodyRotation.GetForwardVector();
		FVector Pad, Normal;
		if (FindContact(PeelLeg, BodyCenter, BodyRotation, Toward, Pad, Normal))
		{
			StartSwing(PeelLeg, Pad, Normal, 0.0f);
		}
		else
		{
			Leg.Fold = 0.0f;
		}
	}
	if (Alpha >= 1.0f && !Leg.bSwinging)
	{
		// 나머지 다리를 펴며 걷기 시작한다.
		SetState(EIGLiningState::Stepping);
		const FIGLiningGap* Gap = FindGap(TargetGap);
		const FVector Toward = Gap ? (Gap->Center - BodyCenter).GetSafeNormal() : BodyRotation.GetForwardVector();
		for (int32 Index = 0; Index < LegCount; ++Index)
		{
			FVector Pad, Normal;
			if (Index != PeelLeg && Legs[Index].Fold > 0.5f && FindContact(Index, BodyCenter, BodyRotation, Toward, Pad, Normal))
			{
				StartSwing(Index, Pad, Normal, 0.0f);
				// 떨어진 발의 대각선 짝이 먼저 펴고, 나머지 둘은 그 두 발이 면을 짚은 뒤에 옮긴다.
				Legs[Index].SwingAlpha = Index == LegCount - 1 - PeelLeg ? 0.0f : -1.0f;
			}
		}
		bPulling = false;
		PhaseSeconds = 0.0f;
		PathTravelled = 0.0f;
	}
}

void AIGLiningEntity::UpdateStepping(const float DeltaSeconds)
{
	PhaseSeconds += DeltaSeconds * PaceScale;
	const float Length = PathLength();
	if (bPulling)
	{
		float Alpha = FMath::Clamp(PhaseSeconds / IGLining::PullSeconds, 0.0f, 1.0f);
		const float Next = FMath::Lerp(PullFrom, PullTo, IGLining::Smooth(Alpha));
		// 짚은 발이 닿지 않을 만큼은 당기지 않는다. 발이 면에서 끌려가는 대신 이번 당기기를 거기서 끝내고 뒤처진
		// 발부터 옮긴다. 몇 번 잇달아 막히면(닿을 데가 없는 길) 그냥 당긴다.
		const FVector NextCenter = PathPoint(Next);
		const FQuat NextRotation = BodyRotationAt(Length > 0.0f ? Next / Length : 1.0f);
		if (Next > PathTravelled && BlockedPulls < 3 && !PlantedLegsReach(NextCenter, NextRotation))
		{
			Alpha = 1.0f;
			PullTo = PathTravelled;
			++BlockedPulls;
		}
		else
		{
			if (Next > PathTravelled && BlockedPulls >= 3)
			{
				// 길이 막혀 그냥 당긴다. 닿지 않는 발은 면에서 끌려가지 않게 들어 몸에 접는다. 다음 걸음에서 짚을 데를
				// 찾으면 다시 편다(몸을 돌리며 모서리를 도는 302호 북벽→서벽에서 한 발이 37 cm 끌려갔다).
				for (int32 Leg = 0; Leg < LegCount; ++Leg)
				{
					if (!Legs[Leg].bSwinging && Legs[Leg].Fold < 0.5f && Leg != PinnedLeg && LegReach(Leg, NextCenter, NextRotation) > 0.97f)
					{
						StartSwing(Leg, FoldedPadWorld(Leg, NextCenter, NextRotation), NextRotation.GetUpVector(), 1.0f);
					}
				}
			}
			PathTravelled = Next;
			if (Alpha >= 1.0f)
			{
				BlockedPulls = 0;
			}
		}
		BodyCenter = PathPoint(PathTravelled);
		BodyRotation = BodyRotationAt(Length > 0.0f ? PathTravelled / Length : 1.0f);
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now - LastCreakAt > IGLining::CreakCueInterval)
		{
			LastCreakAt = Now;
			PlayCue(TEXT("Lining_Creak"), 3, BodyCenter, 0.7f, FMath::FRandRange(0.9f, 1.05f));
			Caption(NSLOCTEXT("IGMissingFloor", "CaptionLiningCreak", "[쇠붙이가 버티며 삐걱이는 소리]"), BodyCenter, 2.0f,
				LastCreakCaptionAt, IGLining::CreakCaptionInterval);
		}
		if (Alpha >= 1.0f)
		{
			if (PathTravelled >= Length - 1.0f)
			{
				// 다 왔다. 둘레를 짚는다.
				MoveFromCenter = BodyCenter;
				MoveFromRotation = BodyRotation;
				SetState(EIGLiningState::Bracing);
				const FIGLiningGap* Gap = FindGap(TargetGap);
				if (Gap)
				{
					// 몸 자리는 길을 정할 때 맞춰 두었다(옆 소품·이웃 벽·열린 문짝을 비킨 자리).
					const FVector N = TargetPose.Rotation.GetUpVector();
					MoveToCenter = TargetPose.Anchor + N * IGLining::BraceStandOff;
					MoveToRotation = TargetPose.Rotation;
					// 대각선 둘이 먼저 짚고, 그 둘이 면을 누른 뒤에 나머지 둘이 옮긴다(늘 두 발이 버틴다). 걸을 때 디딘 다리가
					// 옆 소품에 걸려 있으면 그 다리가 든 쪽이 먼저 옮긴다. 늘 1·2가 기다리면 관리실 작업대 옆 바닥을 디딘 1번
					// 다리의 정강이 끝이 기다리는 동안(약 1초) 작업대 아래 모서리를 지났다.
					int32 Caught[2] = {0, 0};
					for (int32 Group = 0; Group < 2; ++Group)
					{
						for (const int32 Leg : IGLining::GroupLegs[Group])
						{
							int32 Segment = INDEX_NONE;
							FString Component;
							Caught[Group] += FindLegObstructionPart(Leg, Segment, Component) ? 1 : 0;
						}
					}
					const int32 FirstGroup = Caught[1] > Caught[0] ? 1 : 0;
					for (int32 Leg = 0; Leg < LegCount; ++Leg)
					{
						// 둘레가 바닥 밑·옆 소품 속이면 닿는 면을 다시 고른다. 짚을 데가 없는 다리는 몸 밑에 접는다. 짚는 다리는
						// 들 때 옮겨 가는 길까지 보고 다시 고른다(RechooseBracePad).
						FVector Pad = FVector::ZeroVector;
						FVector Normal = N;
						const bool bTuck = Gap->bTuckLowerLegs && !TargetPose.bTurned && IGLining::LegSx[Leg] < 0.0f;
						if (!bTuck && ChooseBracePad(Leg, *Gap, TargetPose, Pad, Normal))
						{
							StartSwing(Leg, Pad, Normal, 0.0f);
						}
						else
						{
							StartSwing(Leg, FoldedPadWorld(Leg, MoveToCenter, MoveToRotation), N, 1.0f);
						}
						const bool bFirst = IGLining::GroupLegs[FirstGroup][0] == Leg || IGLining::GroupLegs[FirstGroup][1] == Leg;
						Legs[Leg].SwingAlpha = bFirst ? 0.0f : -1.0f;
					}
				}
				else
				{
					MoveToCenter = BodyCenter;
					MoveToRotation = BodyRotation;
				}
				return;
			}
			// 당기기를 마쳤다. 뒤에 처진 두 다리를 앞으로 옮긴다.
			bPulling = false;
			PhaseSeconds = 0.0f;
			const FVector Travel = (PathPoint(FMath::Min(PathTravelled + IGLining::StepLength, Length)) - BodyCenter).GetSafeNormal();
			float GroupLag[2] = {0.0f, 0.0f};
			for (int32 Group = 0; Group < 2; ++Group)
			{
				for (const int32 Leg : IGLining::GroupLegs[Group])
				{
					GroupLag[Group] += FVector::DotProduct(BodyCenter - Legs[Leg].Pad, Travel);
					// 다 뻗은 다리가 있는 짝을 먼저 옮긴다(다음 당기기에서 그 발이 끌려가지 않게).
					if (LegReach(Leg, BodyCenter, BodyRotation) > 0.9f)
					{
						GroupLag[Group] += 1000.0f;
					}
				}
			}
			SwingGroup = GroupLag[0] >= GroupLag[1] ? 0 : 1;
			const float FutureDistance = FMath::Min(PathTravelled + IGLining::StepLength * 1.6f, Length);
			const FVector FutureBody = PathPoint(FutureDistance);
			const FQuat FutureRotation = BodyRotationAt(Length > 0.0f ? FutureDistance / Length : 1.0f);
			for (const int32 Leg : IGLining::GroupLegs[SwingGroup])
			{
				FVector Pad, Normal;
				if (FindContact(Leg, FutureBody, FutureRotation, Travel, Pad, Normal))
				{
					StartSwing(Leg, Pad, Normal, 0.0f);
				}
			}
		}
		return;
	}
	// 두 다리를 옮기는 중. 다 옮기면 다시 당긴다.
	bool bAnySwing = false;
	for (const FLeg& Leg : Legs)
	{
		bAnySwing |= Leg.bSwinging;
	}
	if (!bAnySwing && PhaseSeconds >= 0.2f)
	{
		bPulling = true;
		PhaseSeconds = 0.0f;
		PullFrom = PathTravelled;
		PullTo = FMath::Min(Length, PathTravelled + IGLining::StepLength);
		PlayCue(TEXT("Lining_Drag"), 2, BodyCenter, 0.5f, FMath::FRandRange(0.9f, 1.05f));
	}
}

void AIGLiningEntity::UpdateBracing(const float DeltaSeconds)
{
	const float Alpha = FMath::Clamp(StateSeconds * PaceScale / IGLining::BraceSeconds, 0.0f, 1.0f);
	BodyCenter = FMath::Lerp(MoveFromCenter, MoveToCenter, IGLining::Smooth(Alpha));
	BodyRotation = FQuat::Slerp(MoveFromRotation, MoveToRotation, IGLining::Smooth(Alpha));
	bool bAnySwing = false;
	for (const FLeg& Leg : Legs)
	{
		bAnySwing |= Leg.bSwinging;
	}
	if (Alpha >= 1.0f && !bAnySwing)
	{
		if (!IsGapOpen(TargetGap))
		{
			// 오는 사이에 덮개가 닫혔다. 바람이 새지 않으니 누르지 않고 그 자리에 멈춘다.
			UE_LOG(LogIndieGame, Display, TEXT("LINING_ARRIVED_CLOSED %s"), *TargetGap.ToString());
			TargetGap = NAME_None;
			HeldGap = NAME_None;
			SetState(EIGLiningState::Holding);
			HoldingCheckSeconds = 0.0f;
			OnSettled.Broadcast(this);
			return;
		}
		SetState(EIGLiningState::Pressing);
		SetObstructions(TargetGap, true, &TargetPose);
		MoveFromCenter = BodyCenter;
		MoveToCenter = TargetPose.bValid ? TargetPose.Anchor + TargetPose.Rotation.GetUpVector() * IGLining::PressStandOff : BodyCenter;
		PlayCue(TEXT("Lining_Press"), 1, MoveToCenter, 0.85f, 0.94f);
		Caption(NSLOCTEXT("IGMissingFloor", "CaptionLiningPress", "[무언가 벽에 쩍 들러붙는 소리]"), MoveToCenter, 2.4f,
			LastPressCaptionAt, 3.0f);
	}
}

void AIGLiningEntity::UpdatePressing(const float DeltaSeconds)
{
	const float Alpha = FMath::Clamp(StateSeconds * PaceScale / IGLining::PressSeconds, 0.0f, 1.0f);
	// 한 번에 붙지 않고 두어 번 밀어붙인다.
	const float Shudder = FMath::Sin(Alpha * PI * 5.0f) * (1.0f - Alpha) * 2.5f;
	const FIGLiningGap* Gap = FindGap(TargetGap);
	const FVector N = Gap ? Gap->Normal.GetSafeNormal() : BodyRotation.GetUpVector();
	BodyCenter = FMath::Lerp(MoveFromCenter, MoveToCenter, IGLining::Smooth(Alpha)) + N * Shudder;
	if (const TObjectPtr<UAudioComponent>* Draft = GapDrafts.Find(TargetGap))
	{
		if (UAudioComponent* Audio = Draft->Get())
		{
			Audio->AdjustVolume(0.1f, FMath::Max(0.02f, 1.0f - IGLining::Smooth(Alpha)));
		}
	}
	if (Alpha >= 1.0f)
	{
		const FName Sealed = TargetGap;
		SealGap(Sealed);
		HeldGap = Sealed;
		HeldPose = TargetPose;
		TargetGap = NAME_None;
		SetState(EIGLiningState::Holding);
		HoldingCheckSeconds = 0.0f;
		OnGapSealed.Broadcast(this, Sealed);
		OnSettled.Broadcast(this);
	}
}

void AIGLiningEntity::UpdateHolding(const float DeltaSeconds)
{
	UpdateProbing(DeltaSeconds);
	HoldingCheckSeconds += DeltaSeconds;
	if (HoldingCheckSeconds >= IGLining::HoldCheckSeconds || bGapsDirty)
	{
		HoldingCheckSeconds = 0.0f;
		bGapsDirty = false;
		ChooseTarget();
	}
}

void AIGLiningEntity::UpdateProbing(const float DeltaSeconds)
{
	for (const FLeg& Leg : Legs)
	{
		if (Leg.bSwinging)
		{
			return;
		}
	}
	ProbeSeconds += DeltaSeconds * PaceScale;
	// 간격은 5~8초. 씨앗으로 정해 실행마다 같다.
	const float Interval = 5.0f + 3.0f * FMath::Frac(FMath::Sin(static_cast<float>(ProbeCount) * 12.9898f) * 43758.547f);
	if (ProbeSeconds < Interval)
	{
		return;
	}
	ProbeSeconds = 0.0f;
	const int32 Index = ProbeCount % LegCount;
	++ProbeCount;
	if (Index == PinnedLeg || Legs[Index].Fold > 0.5f)
	{
		return;
	}
	FLeg& Leg = Legs[Index];
	// 짚은 면을 따라 한 뼘 옆을 낮게 짚어 본다. 다음 번에는 제자리로 돌아온다.
	FVector Along = FVector::CrossProduct(Leg.Normal, BodyRotation.GetForwardVector());
	if (!Along.Normalize())
	{
		Along = FVector::CrossProduct(Leg.Normal, FVector::RightVector).GetSafeNormal();
	}
	const float Reach = (ProbeCount / LegCount) % 2 == 0 ? 9.0f : -9.0f;
	StartSwing(Index, Leg.Pad + Along * Reach, Leg.Normal, 0.0f);
	Leg.LiftScale = 0.28f;
	StartTicking();
}

FVector AIGLiningEntity::ComputeBodySquash() const
{
	switch (State)
	{
	// 몸을 크게 키워 펼침을 흉내 내지 않는다. 눌리면 두께만 줄고, 넓게 퍼지는 것은 몸 둘레 막과 살(무른 뼈)이 맡는다.
	case EIGLiningState::Stepping:
		if (bPulling)
		{
			// 당기기 첫머리에 몸을 웅크려 힘을 모은다.
			const float Alpha = FMath::Clamp(PhaseSeconds / IGLining::PullSeconds, 0.0f, 1.0f);
			const float Gather = 1.0f - FMath::Clamp(Alpha / 0.25f, 0.0f, 1.0f);
			return FVector(1.0f, 1.0f, 1.0f - 0.07f * Gather);
		}
		return FVector::OneVector;
	case EIGLiningState::Bracing:
		return FVector(1.0f, 1.0f, 0.96f);
	case EIGLiningState::Pressing:
	{
		const float Alpha = IGLining::Smooth(FMath::Clamp(StateSeconds * PaceScale / IGLining::PressSeconds, 0.0f, 1.0f));
		return FVector(1.0f, 1.0f, 1.0f - 0.16f * Alpha);
	}
	case EIGLiningState::Holding:
		if (!HeldGap.IsNone())
		{
			// 막은 채로 숨 쉬듯 조금씩 다시 눌러 붙인다.
			const UWorld* World = GetWorld();
			const float Time = World ? static_cast<float>(World->GetTimeSeconds()) : 0.0f;
			const float Breath = 0.02f * FMath::Sin(Time * 2.0f * PI * 0.28f);
			return FVector(1.0f, 1.0f, 0.85f + Breath);
		}
		return FVector::OneVector;
	case EIGLiningState::Spread:
		return FVector(1.0f, 1.0f, 0.93f);
	case EIGLiningState::Gripping:
		return FVector(1.0f, 1.0f, 0.88f);
	default:
		return FVector::OneVector;
	}
}

FVector AIGLiningEntity::SoftGravity() const
{
	const FVector Down(0.0f, 0.0f, -1.0f);
	const FVector Up = BodyRotation.GetUpVector();
	// 면에 붙어 있으면 면 쪽으로 누르는 성분은 그 면이 받는다. 면을 따라 흘러내리는 성분만 남는다.
	return Down - Up * FMath::Min(0.0f, FVector::DotProduct(Down, Up)) * SoftContact;
}

void AIGLiningEntity::UpdateSoftParts(const float DeltaSeconds)
{
	if (!bHasSoftBones || DeltaSeconds <= 0.0f)
	{
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	// 몸이 끌려가며 붙는 가속도. 살과 막은 그 반대쪽으로 뒤처졌다가 따라온다.
	const FVector Velocity = bSoftFresh ? FVector::ZeroVector : (BodyCenter - SoftLastCenter) / Dt;
	const FVector Accel = bSoftFresh ? FVector::ZeroVector : ((Velocity - SoftLastVelocity) / Dt).GetClampedToMaxSize(900.0f);
	SoftAcceleration = FMath::Lerp(SoftAcceleration, Accel, FMath::Min(1.0f, Dt * 20.0f));
	SoftLastCenter = BodyCenter;
	SoftLastVelocity = Velocity;
	bSoftFresh = false;
	const bool bOnSurface = State == EIGLiningState::Folded || State == EIGLiningState::Pressing || State == EIGLiningState::Holding
		|| State == EIGLiningState::Spread || State == EIGLiningState::Gripping;
	SoftContact = FMath::FInterpTo(SoftContact, bOnSurface ? 1.0f : 0.0f, Dt, 6.0f);
	const FVector Gravity = SoftGravity();
	const FVector Up = BodyRotation.GetUpVector();
	const float Sag = FMath::Lerp(IGLining::SagHung, IGLining::SagRest, SoftContact);
	// 틈을 누르는 정도(0~1). 누르는 동안 차오르고, 막은 채로는 다 눌려 있다. 5층에서 편 몸과 쉬는 더미는 덜 퍼진다.
	const float Press = State == EIGLiningState::Pressing
		? IGLining::Smooth(FMath::Clamp(StateSeconds * PaceScale / IGLining::PressSeconds, 0.0f, 1.0f))
		: State == EIGLiningState::Holding && !HeldGap.IsNone() ? 1.0f
		: State == EIGLiningState::Gripping ? 1.0f
		: State == EIGLiningState::Spread ? 0.7f
		: State == EIGLiningState::Folded ? 0.3f : 0.0f;
	const float Hold = PeelHoldFraction();
	// 0.1초 간격으로 틱할 때도 튀지 않게 잘게 나눠 적분한다.
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Dt * 120.0f), 1, 12);
	const float H = Dt / Steps;
	const auto Spring = [this, Steps, H](FSoftPoint& Point, const FVector& Target, const float Hz, const float Damping,
		const float Inertia, const float Limit)
	{
		const float Omega = 2.0f * PI * Hz;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Point.Velocity += ((Target - Point.Offset) * (Omega * Omega) - Point.Velocity * (2.0f * Damping * Omega)
				- SoftAcceleration * Inertia) * H;
			Point.Offset += Point.Velocity * H;
		}
		if (Point.Offset.SizeSquared() > Limit * Limit)
		{
			Point.Offset = Point.Offset.GetClampedToMaxSize(Limit);
			Point.Velocity *= 0.5f;
		}
	};
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		const FLeg& Leg = Legs[Index];
		// 받치던 다리가 들렸거나 접힌 사분면이 더 처진다.
		const bool bUnsupported = Leg.bSwinging || Leg.Fold > 0.5f;
		// 누르면 살이 면을 따라 그 사분면 쪽으로 밀려난다.
		const FVector Outward = BodyRotation.RotateVector(FVector(IGLining::LegSx[Index], IGLining::LegSy[Index], 0.0f).GetSafeNormal());
		Spring(Mass[Index], Gravity * Sag * (bUnsupported ? 1.3f : 0.8f) + Outward * IGLining::MassSpread * Press,
			IGLining::MassHz, IGLining::MassDamping, 0.9f, IGLining::SagMax);

		// 물갈퀴: 윗마디를 몸 쪽으로 접을수록 느슨해져 처지고, 패드가 떨어지기 직전에 몸에서 먼저 벌어진다.
		const FVector Upper = BodyRotation.UnrotateVector(Leg.Knee - Leg.Hip).GetSafeNormal();
		const float Slack = FMath::Clamp((0.85f - FVector::DotProduct(Upper, RestOutward[Index])) / 1.2f, 0.0f, 1.0f);
		const float Opening = Leg.bSwinging && Leg.SwingAlpha > 0.0f
			? FMath::Sin(PI * FMath::Clamp(Leg.SwingAlpha / (Hold + 0.12f), 0.0f, 1.0f)) * Leg.LiftScale
			: 0.0f;
		Spring(Web[Index], FVector(0.0f, 0.0f, -IGLining::WebDroop * (0.3f + 0.7f * Slack)) + Up * IGLining::WebOpen * Opening,
			IGLining::WebHz, IGLining::WebDamping, 1.0f, IGLining::WebMax);

		// 패드 막 끝: 붙어 있는 동안은 짚었던 면에 남는다. 놓이면 늘어났던 자리에서 패드 쪽으로 튕겨 돌아온다.
		if (bSkinStuck[Index])
		{
			const FVector Stretch = SkinStuckWorld[Index].GetLocation() - DefaultSkinWorld(Index).GetLocation();
			if (!Leg.bSwinging || Leg.SwingAlpha >= Hold + IGLining::SkinRelease - IGLining::PeelHold
				|| Stretch.Size() > IGLining::SkinStretchMax)
			{
				bSkinStuck[Index] = false;
				Skin[Index].Offset = Stretch.GetClampedToMaxSize(IGLining::SkinStretchMax);
				Skin[Index].Velocity = FVector::ZeroVector;
			}
		}
		else
		{
			Spring(Skin[Index], FVector::ZeroVector, IGLining::SkinHz, IGLining::SkinDamping, 0.6f, IGLining::SkinStretchMax);
		}
	}
	// 몸 둘레 막: 들려 있으면 무게로 처지고, 틈을 누를수록 면을 따라 바깥으로 퍼진다(몸을 키우지 않는다).
	for (int32 Side = 0; Side < SkirtCount; ++Side)
	{
		const FVector Outward = BodyRotation.RotateVector(IGLining::SkirtSides[Side]);
		const FVector Target = Outward * IGLining::SkirtSpread * Press + Gravity * IGLining::SkirtDroop * (1.0f - 0.7f * SoftContact);
		Spring(Skirt[Side], Target, IGLining::SkirtHz, IGLining::SkirtDamping, 1.0f, IGLining::SkirtMax);
	}
}

void AIGLiningEntity::ResetSoftParts()
{
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		Mass[Index] = FSoftPoint();
		Web[Index] = FSoftPoint();
		Skin[Index] = FSoftPoint();
		bSkinStuck[Index] = false;
	}
	for (FSoftPoint& Point : Skirt)
	{
		Point = FSoftPoint();
	}
	SoftAcceleration = FVector::ZeroVector;
	SoftLastVelocity = FVector::ZeroVector;
	bSoftFresh = true;
	// 옮겨 놓은 자리에서 이미 다 처진 채로 시작한다(첫 화면에서 살이 내려앉는 게 보이지 않게).
	for (int32 Step = 0; Step < 20; ++Step)
	{
		UpdateSoftParts(0.05f);
	}
}

void AIGLiningEntity::PoseSoftParts(const FTransform& BodyTransform)
{
	if (!bHasSoftBones)
	{
		return;
	}
	const FVector ToComponent = RestBody.GetLocation() - BodyCenter;
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		// 살은 몸 뼈(눌림 포함)를 따르다 처진 만큼 비킨다.
		FTransform MassPose = MassRest[Index] * BodyTransform;
		MassPose.AddToTranslation(Mass[Index].Offset);
		Body->SetBoneTransformByName(MassBones[Index], MassPose, EBoneSpaces::ComponentSpace);
		// 물갈퀴 가운데는 몸 옆구리와 윗마디 사이 가운데에 있다가 처지거나 벌어진다.
		FTransform Upper = SegmentWorld[Index][0];
		Upper.AddToTranslation(ToComponent);
		FTransform WebPose;
		WebPose.Blend(WebRestBody[Index] * BodyTransform, WebRestUpper[Index] * Upper, 0.5f);
		WebPose.AddToTranslation(Web[Index].Offset);
		Body->SetBoneTransformByName(WebBones[Index], WebPose, EBoneSpaces::ComponentSpace);
		// 패드 막 끝은 붙어 있는 동안 면에 남고, 놓인 뒤에는 늘어났던 만큼에서 패드로 돌아온다.
		FTransform SkinPose = bSkinStuck[Index] ? SkinStuckWorld[Index] : DefaultSkinWorld(Index);
		if (!bSkinStuck[Index])
		{
			SkinPose.AddToTranslation(Skin[Index].Offset);
			const float Back = FMath::Clamp(Skin[Index].Offset.Size() / IGLining::SkinStretchMax, 0.0f, 1.0f);
			SkinPose.SetRotation(FQuat::Slerp(SkinPose.GetRotation(), SkinStuckWorld[Index].GetRotation(), Back));
		}
		SkinPose.AddToTranslation(ToComponent);
		Body->SetBoneTransformByName(SkinBones[Index], SkinPose, EBoneSpaces::ComponentSpace);
	}
	for (int32 Side = 0; Side < SkirtCount; ++Side)
	{
		FTransform SkirtPose = SkirtRest[Side] * BodyTransform;
		SkirtPose.AddToTranslation(Skirt[Side].Offset);
		Body->SetBoneTransformByName(SkirtBones[Side], SkirtPose, EBoneSpaces::ComponentSpace);
	}
}

bool AIGLiningEntity::AreSoftPartsSettled() const
{
	if (!bHasSoftBones)
	{
		return true;
	}
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		if (bSkinStuck[Index] || Skin[Index].Offset.SizeSquared() > 0.25f || Skin[Index].Velocity.SizeSquared() > 0.25f
			|| Mass[Index].Velocity.SizeSquared() > 0.25f || Web[Index].Velocity.SizeSquared() > 0.25f)
		{
			return false;
		}
	}
	for (const FSoftPoint& Point : Skirt)
	{
		if (Point.Velocity.SizeSquared() > 0.25f)
		{
			return false;
		}
	}
	return true;
}

void AIGLiningEntity::KickSoftParts(const int32 Leg, const float Strength)
{
	const FVector Gravity = SoftGravity();
	if (!bHasSoftBones || Gravity.SizeSquared() < 0.04f)
	{
		return;
	}
	const FVector Down = Gravity.GetSafeNormal();
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		Mass[Index].Velocity += Down * (Index == Leg ? 32.0f : 18.0f) * Strength;
	}
	for (FSoftPoint& Point : Skirt)
	{
		Point.Velocity += Down * 22.0f * Strength;
	}
	Web[Leg].Velocity += Down * 24.0f * Strength;
}

float AIGLiningEntity::PeelHoldFraction() const
{
	return State == EIGLiningState::Peeling ? IGLining::FirstPeelHold : IGLining::PeelHold;
}

float AIGLiningEntity::LegReach(const int32 Leg, const FVector& Center, const FQuat& Rotation) const
{
	return ReachTo(Leg, Legs[Leg].Pad, Legs[Leg].Normal, Center, Rotation);
}

float AIGLiningEntity::ReachTo(const int32 Leg, const FVector& Pad, const FVector& Normal, const FVector& Center,
	const FQuat& Rotation) const
{
	const FVector Hip = HipWorld(Leg, Center, Rotation);
	return FVector::Distance(Hip, AnkleFor(Leg, Pad, Normal, Hip, Rotation)) / FMath::Max(Length1[Leg] + Length2[Leg], 1.0f);
}

FVector AIGLiningEntity::AnkleFor(const int32 Leg, const FVector& Pad, const FVector& Normal, const FVector& Hip,
	const FQuat& Rotation) const
{
	// SolveLeg와 같은 수로 짚을 패드에서 발목 자리를 거꾸로 구한다(엉덩이 바로 밑이면 몸 바깥쪽을 섞는 것까지).
	const FVector OutwardWorld = Rotation.RotateVector(RestOutward[Leg]);
	FVector OutwardOnSurface = OutwardWorld - Normal * FVector::DotProduct(OutwardWorld, Normal);
	if (!OutwardOnSurface.Normalize())
	{
		OutwardOnSurface = OutwardWorld;
	}
	FVector PadForward = Pad - Hip;
	PadForward -= Normal * FVector::DotProduct(PadForward, Normal);
	const float InPlane = PadForward.Size();
	const float Away = FMath::Clamp(InPlane / 30.0f, 0.0f, 1.0f);
	PadForward = (InPlane > KINDA_SMALL_NUMBER ? PadForward / InPlane : FVector::ZeroVector) * Away + OutwardOnSurface * (1.0f - Away);
	if (!PadForward.Normalize())
	{
		PadForward = OutwardOnSurface;
	}
	const FQuat PadQuat = IGLining::FrameRotation(RestOutward[Leg], FVector::UpVector, PadForward, Normal);
	return Pad - PadQuat.RotateVector(RestPad[Leg] - RestSegment[Leg][2].GetLocation());
}

bool AIGLiningEntity::PlantedLegsReach(const FVector& Center, const FQuat& Rotation) const
{
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		if (!Legs[Leg].bSwinging && Legs[Leg].Fold < 0.5f && Leg != PinnedLeg && LegReach(Leg, Center, Rotation) > 0.97f)
		{
			return false;
		}
	}
	return true;
}

FTransform AIGLiningEntity::DefaultSkinWorld(const int32 Leg) const
{
	return SkinRest[Leg] * SegmentWorld[Leg][2];
}

void AIGLiningEntity::SpreadTo(
	const FVector& InBodyCenter,
	const FVector& InBodyUp,
	const FVector& InBodyForward,
	const TArray<FVector>& Pads,
	const TArray<FVector>& PadNormals,
	const float Seconds)
{
	if (!LoadBody() || Pads.Num() != LegCount || PadNormals.Num() != LegCount)
	{
		return;
	}
	SetObstructions(HeldGap, false);
	MoveFromCenter = BodyCenter;
	MoveFromRotation = BodyRotation;
	MoveToCenter = InBodyCenter;
	MoveToRotation = IGLining::FrameRotation(FVector::ForwardVector, FVector::UpVector, InBodyForward, InBodyUp);
	MoveSeconds = FMath::Max(Seconds, 0.5f);
	SpreadPads = Pads;
	SpreadNormals = PadNormals;
	bHasMoved = true;
	SetState(EIGLiningState::Spread);
	// 대각선 둘이 먼저 옮겨 짚고, 그 둘이 버티는 동안 나머지 둘이 따른다. 몸이 가는 쪽으로 가장 멀리 짚고 있는 다리가
	// 든 쪽이 먼저 간다. 늘 0·3이 먼저 가면 5층 점검구에서 펼 때 몸판이 기다리는 1번 패드 바로 위를 지나, 엉덩이가
	// 발목에 3 cm까지 붙으며 무릎이 벽 속으로 크게 돌았다. 두 다리를 더해서 재면 반대쪽으로 짚은 짝과 지워졌다.
	const FVector Travel = InBodyCenter - BodyCenter;
	float Ahead[2] = {-BIG_NUMBER, -BIG_NUMBER};
	for (int32 Group = 0; Group < 2; ++Group)
	{
		for (const int32 Leg : IGLining::GroupLegs[Group])
		{
			Ahead[Group] = FMath::Max(Ahead[Group], FVector::DotProduct(Legs[Leg].Pad - BodyCenter, Travel));
		}
	}
	const int32 FirstGroup = Ahead[1] > Ahead[0] ? 1 : 0;
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		StartSwing(Leg, Pads[Leg], PadNormals[Leg], 0.0f);
		const bool bFirst = IGLining::GroupLegs[FirstGroup][0] == Leg || IGLining::GroupLegs[FirstGroup][1] == Leg;
		Legs[Leg].SwingAlpha = bFirst ? 0.0f : -1.0f;
	}
	PlayCue(TEXT("Lining_Creak"), 3, InBodyCenter, 0.95f, 0.82f);
	Caption(NSLOCTEXT("IGMissingFloor", "CaptionLiningSpread", "[쇠붙이 여러 개가 한꺼번에 긁히는 소리]"), InBodyCenter, 2.8f,
		LastCreakCaptionAt, 0.0f);
	StartTicking();
}

void AIGLiningEntity::UpdateSpread(const float DeltaSeconds)
{
	const float Alpha = FMath::Clamp(StateSeconds / MoveSeconds, 0.0f, 1.0f);
	BodyCenter = FMath::Lerp(MoveFromCenter, MoveToCenter, IGLining::Smooth(Alpha));
	BodyRotation = FQuat::Slerp(MoveFromRotation, MoveToRotation, IGLining::Smooth(Alpha));
	if (Alpha >= 1.0f && StateSeconds - DeltaSeconds < MoveSeconds)
	{
		OnSettled.Broadcast(this);
	}
}

void AIGLiningEntity::SetKneeHint(const int32 Leg, const FVector& KneeWorld)
{
	HintLeg = Leg >= 0 && Leg < LegCount ? Leg : INDEX_NONE;
	KneeHint = KneeWorld;
}

void AIGLiningEntity::PinKnee(const int32 Leg, const FVector& KneeWorld)
{
	if (Leg < 0 || Leg >= LegCount)
	{
		return;
	}
	// 같은 자리에 다시 거는 것(불러오기 뒤 펴기)이면 옮겨 가지 않는다.
	if (PinnedLeg != Leg || !PinnedKnee.Equals(KneeWorld, 1.0f))
	{
		PinBlend = 0.0f;
	}
	PinnedLeg = Leg;
	PinnedKnee = KneeWorld;
	PlayCue(TEXT("Lining_FrameCatch"), 1, KneeWorld, 0.9f, 1.0f);
	StartTicking();
}

void AIGLiningEntity::ReleasePin()
{
	PinnedLeg = INDEX_NONE;
}

void AIGLiningEntity::RetreatTo(const FVector& Point, const FVector& Normal, const FVector& Forward, const float Seconds)
{
	if (!LoadBody())
	{
		return;
	}
	SetObstructions(HeldGap, false);
	MoveFromCenter = BodyCenter;
	MoveFromRotation = BodyRotation;
	MoveToCenter = Point + Normal.GetSafeNormal() * IGLining::PressStandOff;
	MoveToRotation = IGLining::FrameRotation(FVector::ForwardVector, FVector::UpVector, Forward, Normal.GetSafeNormal());
	MoveSeconds = FMath::Max(Seconds, 0.5f);
	// 처음에 면에서 떨어지는 거리(40·55·70 cm)와 구석 면 앞에서 다 돌 자리(면에서 60·90 cm, 옆으로 0·±30 cm, 앞뒤로
	// 0·±30·-60 cm)를 그려 보고 몸판과 접힌 다리가 덜 지나는 쪽으로 간다. 한 가지로 정해 두면 5층 철문 옆 구석으로 밀려날 때
	// 몸판 모서리가 옆벽을 11 cm 파고들며 돌았다. 접힌 다리 무릎은 몸판 앞뒤 끝보다 5 cm 더 나와서, 몸판만 보면 도는
	// 동안 무릎이 동쪽 벽을 5 cm 파고들었다.
	RetreatAway = 40.0f;
	RetreatBefore = MoveToCenter + MoveToRotation.GetUpVector() * 60.0f;
	if (UWorld* World = GetWorld())
	{
		const FCollisionShape Shape = FCollisionShape::MakeBox(IGLining::BodyHalf - FVector(3.0f));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningRetreat), false, this);
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			Params.AddIgnoredActor(PlayerPawn);
		}
		const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
		// 다리는 처음 4분의 1쯤에 다 접힌다(스윙 0.78초). 그 뒤부터 접힌 다리를 몇 점 걸러 본다.
		const auto FoldedLegHits = [this, World, &Params, &Objects](const FVector& Center, const FQuat& Rotation)
		{
			int32 Hits = 0;
			for (int32 Leg = 0; Leg < LegCount; ++Leg)
			{
				FVector Knee, Ankle, Pad;
				IGLining::FoldedJoints(Leg, HipLocal[Leg], Knee, Ankle, Pad);
				const FVector Joints[4] = {Center + Rotation.RotateVector(HipLocal[Leg]), Center + Rotation.RotateVector(Knee),
					Center + Rotation.RotateVector(Ankle), Center + Rotation.RotateVector(Pad)};
				FHitResult Block;
				for (int32 Segment = 0; Segment < 3; ++Segment)
				{
					if (World->LineTraceSingleByObjectType(Block, Joints[Segment], Joints[Segment + 1], Objects, Params))
					{
						++Hits;
						break;
					}
				}
			}
			return Hits;
		};
		int32 BestHits = TNumericLimits<int32>::Max();
		for (const float AwayDistance : {40.0f, 55.0f, 70.0f})
		{
			for (const float Out : {60.0f, 90.0f})
			{
				for (const float Across : {0.0f, -30.0f, 30.0f})
				{
					for (const float Along : {0.0f, -30.0f, -60.0f, 30.0f})
					{
						const FVector Before = MoveToCenter + MoveToRotation.GetUpVector() * Out
							+ MoveToRotation.GetRightVector() * Across + MoveToRotation.GetForwardVector() * Along;
						int32 Hits = 0;
						for (int32 Step = 1; Step < 40; ++Step)
						{
							FQuat Rotation;
							const FVector Center = RetreatPoseAt(Step / 40.0f, AwayDistance, Before, Rotation);
							Hits += World->OverlapAnyTestByObjectType(Center, Rotation, Objects, Shape, Params) ? 1 : 0;
							if (Step >= 11 && Step % 2 == 1)
							{
								Hits += FoldedLegHits(Center, Rotation);
							}
						}
						if (Hits < BestHits)
						{
							BestHits = Hits;
							RetreatAway = AwayDistance;
							RetreatBefore = Before;
						}
					}
				}
			}
		}
		UE_LOG(LogIndieGame, Display, TEXT("LINING_RETREAT away=%.0f before=%s hits=%d"), RetreatAway,
			*(RetreatBefore - MoveToCenter).ToCompactString(), BestHits);
	}
	HeldGap = NAME_None;
	TargetGap = NAME_None;
	SetState(EIGLiningState::Retreating);
	// 다리는 짚었던 자리를 놓고 곧바로 들어 몸에 접는다. 짚은 채 몸만 밀려나면 다리가 늘어나며 무릎이 5층 철문 문틀
	// 덧판과 남벽을 휘저었다. 접는 다리는 지금 몸의 접힌 자리를 따른다(UpdateSwings).
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		if (Legs[Leg].Fold < 0.999f)
		{
			StartSwing(Leg, FoldedPadWorld(Leg, MoveToCenter, MoveToRotation), MoveToRotation.GetUpVector(), 1.0f);
		}
	}
	PlayCue(TEXT("Lining_Creak"), 3, BodyCenter, 0.8f, 0.86f);
	StartTicking();
}

FVector AIGLiningEntity::RetreatPoseAt(const float Alpha, const float AwayDistance, const FVector& Before, FQuat& OutRotation) const
{
	// 처음 4분의 1은 짚고 있던 면에서 곧장 떨어지기만 한다. 벽에 붙은 채 돌기 시작하면 몸 뒤로 접히는 발과 엉덩이가
	// 벽 속으로 돌아 들어갔다(5층 밀려나기). 그다음 바닥 쪽으로 무게를 빼며 미끄러지듯 돌아 구석으로 간다. 붙을 구석
	// 면 앞(Before)까지 오는 동안 다 돌고, 마지막은 그 면에 곧게 붙는다. 돌면서 구석으로 들어가면 몸판 모서리가 천장과
	// 옆벽 속을 쓸고 지나갔다(몸판 40프레임·다리 110프레임).
	const float Away = IGLining::Smooth(Alpha / 0.25f);
	const float T = IGLining::Smooth((Alpha - 0.25f) / 0.75f);
	const FVector Off = MoveFromCenter + MoveFromRotation.GetUpVector() * AwayDistance * Away;
	const FVector Mid = FMath::Lerp(Off, Before, 0.5f);
	const float U = 1.0f - T;
	const FVector Glide = Off * (U * U * U) + Mid * (3.0f * U * U * T) + Before * (3.0f * U * T * T) + MoveToCenter * (T * T * T);
	OutRotation = FQuat::Slerp(MoveFromRotation, MoveToRotation, IGLining::Smooth(FMath::Clamp(T / 0.65f, 0.0f, 1.0f)));
	return Glide - FVector::UpVector * FMath::Sin(T * PI) * 18.0f;
}

void AIGLiningEntity::UpdateRetreating(const float DeltaSeconds)
{
	const float Alpha = FMath::Clamp(StateSeconds / MoveSeconds, 0.0f, 1.0f);
	BodyCenter = RetreatPoseAt(Alpha, RetreatAway, RetreatBefore, BodyRotation);
	if (Alpha >= 1.0f)
	{
		ReleasePin();
		SetState(EIGLiningState::Folded);
		OnSettled.Broadcast(this);
	}
}

void AIGLiningEntity::Hide()
{
	SetObstructions(HeldGap, false);
	SetObstructions(TargetGap, false);
	SetState(EIGLiningState::Hidden);
	SetActorHiddenInGame(true);
	SetBlockersEnabled(false);
	ResetKneeLean();
	ResetSoftParts();
	SetActorTickEnabled(false);
}

void AIGLiningEntity::ReleaseGrip()
{
	if (State == EIGLiningState::Gripping)
	{
		const EIGLiningState Resume = StateBeforeGrip;
		State = Resume;
		StartTicking();
	}
}

void AIGLiningEntity::PoseBody()
{
	if (!Body)
	{
		return;
	}
	SetActorLocation(BodyCenter, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(FRotator::ZeroRotator);
	const FVector Offset = RestBody.GetLocation();
	// 반입한 뼈는 쉬는 자세의 배율이 100이다(미터 뼈대를 센티미터로 받은 뿌리 배율). 그 배율에 몸의 눌림을
	// 곱한다. 눌림은 몸 기준 축(X 앞, Y 옆, Z 면 법선)이라 뼈의 축으로 옮겨 건다.
	const FQuat RestRotation = RestBody.GetRotation();
	FVector LocalSquash;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const FVector Dir = RestRotation.RotateVector(FVector(Axis == 0 ? 1.0f : 0.0f, Axis == 1 ? 1.0f : 0.0f, Axis == 2 ? 1.0f : 0.0f)).GetAbs();
		LocalSquash[Axis] = BodySquash.X * Dir.X + BodySquash.Y * Dir.Y + BodySquash.Z * Dir.Z;
	}
	const FVector Settle = BodyRotation.GetUpVector() * (1.0f - BodySquash.Z) * IGLining::PressStandOff;
	const FTransform BodyTransform(BodyRotation * RestRotation, Offset - Settle, RestBody.GetScale3D() * LocalSquash);
	Body->SetBoneTransformByName(BodyBone, BodyTransform, EBoneSpaces::ComponentSpace);
	const double SolveStarted = bMeasureLegs ? FPlatformTime::Seconds() : 0.0;
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		SolveLeg(Leg);
	}
	if (bMeasureLegs)
	{
		SolveFrameSeconds += FPlatformTime::Seconds() - SolveStarted;
		if (State != EIGLiningState::Hidden && State != EIGLiningState::Folded)
		{
			SolveTotalSeconds += SolveFrameSeconds;
			SolveMaxSeconds = FMath::Max(SolveMaxSeconds, SolveFrameSeconds);
		}
		SolveFrameSeconds = 0.0;
	}
	if (bMeasureLegs && State != EIGLiningState::Hidden && State != EIGLiningState::Folded)
	{
		++MeasuredFrames;
		for (int32 Leg = 0; Leg < LegCount; ++Leg)
		{
			int32 Segment = INDEX_NONE;
			FString Component;
			FVector Impact = FVector::ZeroVector;
			FVector ImpactNormal = FVector::ZeroVector;
			float Inside = 0.0f;
			const FLeg& Measured = Legs[Leg];
			if (!IsLegThickClear(Measured.Hip, Measured.Knee, Measured.Ankle, IGLining::ThickMeasureRadius, Measured.Normal))
			{
				++ThickObstructedFrames[Leg];
			}
			if (FindLegObstructionPart(Leg, Segment, Component, &Impact, &ImpactNormal, &Inside))
			{
				++ObstructedFrames[Leg];
				const FString Key = FString::Printf(TEXT("leg%d/state%d/seg%d/%s"), Leg, static_cast<int32>(State), Segment, *Component);
				float& Deepest = ObstructionDepth.FindOrAdd(Key);
				Deepest = FMath::Max(Deepest, Inside);
				int32& Count = ObstructionBreakdown.FindOrAdd(Key);
				if (Count++ == 0)
				{
					const FLeg& L = Legs[Leg];
					UE_LOG(LogIndieGame, Display,
						TEXT("LINING_OBSTRUCTION_FIRST %s body=%s up=%s hip=%s knee=%s ankle=%s pad=%s impact=%s normal=%s lean=%.2f"),
						*Key, *BodyCenter.ToCompactString(), *BodyRotation.GetUpVector().ToCompactString(),
						*L.Hip.ToCompactString(), *L.Knee.ToCompactString(), *L.Ankle.ToCompactString(), *L.Pad.ToCompactString(),
						*Impact.ToCompactString(), *ImpactNormal.ToCompactString(), KneeLean);
				}
			}
		}
	}
	bSegmentsPosed = true;
	PoseSoftParts(BodyTransform);
	UpdateColliders();
	FString BodyHit;
	if (bMeasureLegs && State != EIGLiningState::Hidden && IsBodyObstructed(&BodyHit))
	{
		++BodyObstructedFrames;
		++ObstructionBreakdown.FindOrAdd(FString::Printf(TEXT("body/state%d/%s"), static_cast<int32>(State), *BodyHit));
		int32& Count = BodyObstructionByState.FindOrAdd(static_cast<int32>(State));
		if (Count++ == 0)
		{
			UE_LOG(LogIndieGame, Display, TEXT("LINING_BODY_OBSTRUCTION_FIRST state=%d center=%s up=%s target=%s held=%s"),
				static_cast<int32>(State), *BodyCenter.ToCompactString(), *BodyRotation.GetUpVector().ToCompactString(),
				*TargetGap.ToString(), *HeldGap.ToString());
		}
	}
}

void AIGLiningEntity::SolveLeg(const int32 Index)
{
	FLeg& Leg = Legs[Index];
	const FVector Up = BodyRotation.GetUpVector();
	const FVector Hip = HipWorld(Index, BodyCenter, BodyRotation);
	const FVector RestHip = RestSegment[Index][0].GetLocation();
	const FVector RestKnee = RestSegment[Index][1].GetLocation();
	const FVector RestAnkle = RestSegment[Index][2].GetLocation();
	const FVector OutwardWorld = BodyRotation.RotateVector(RestOutward[Index]);

	// 짚은 자세: 패드가 면을 따르고, 발목은 패드에서 거꾸로, 무릎은 두 마디 IK로 푼다.
	FVector PadForward = Leg.Pad - Hip;
	PadForward -= Leg.Normal * FVector::DotProduct(PadForward, Leg.Normal);
	// 패드가 엉덩이 바로 밑(면 위로 30 cm 안)이면 엉덩이가 조금만 움직여도 그 방향이 휙 돌아, 패드를 따라 도는 발목이
	// 한 프레임에 17 cm 옮겨 가고 짚은 다리의 무릎이 뒤집혔다(덧댄 것 검사 61 cm). 가까울수록 몸 바깥쪽을 섞는다.
	FVector OutwardOnSurface = OutwardWorld - Leg.Normal * FVector::DotProduct(OutwardWorld, Leg.Normal);
	if (!OutwardOnSurface.Normalize())
	{
		OutwardOnSurface = OutwardWorld;
	}
	const float InPlane = PadForward.Size();
	const float Away = FMath::Clamp(InPlane / 30.0f, 0.0f, 1.0f);
	PadForward = (InPlane > KINDA_SMALL_NUMBER ? PadForward / InPlane : FVector::ZeroVector) * Away + OutwardOnSurface * (1.0f - Away);
	if (!PadForward.Normalize())
	{
		PadForward = OutwardOnSurface;
	}
	FQuat PadQuat = IGLining::FrameRotation(RestOutward[Index], FVector::UpVector, PadForward, Leg.Normal);
	FVector PadPose = Leg.Pad - Leg.Normal * 1.8f * IGLining::Smooth(Leg.PlantKick);
	const float Hold = PeelHoldFraction();
	if (Leg.bSwinging && !Leg.bFlatPeel && Leg.SwingAlpha > 0.0f && Leg.SwingAlpha < Hold + 0.2f)
	{
		// 붙어 있던 패드는 바깥 가장자리를 짚은 채 몸 쪽 가장자리부터 들린다. 그 틈으로 막이 먼저 벌어진다. 엉덩이 바로
		// 밑에서 출발하는 다리(bFlatPeel)는 들리는 가장자리가 발목을 엉덩이 쪽으로 8 cm 더 끌어와, 무릎 원 24방향이 다
		// 막히고 무릎이 관리실 벽 뒤로 23 cm 갔다.
		const float Rise = Leg.SwingAlpha < Hold ? IGLining::Smooth(Leg.SwingAlpha / Hold)
			: 1.0f - IGLining::Smooth((Leg.SwingAlpha - Hold) / 0.2f);
		const FQuat Peel(FVector::CrossProduct(Leg.Normal, PadForward).GetSafeNormal(),
			FMath::DegreesToRadians(IGLining::PeelTiltDegrees) * Rise * Leg.LiftScale);
		const FVector Pivot = PadPose + PadForward * IGLining::PadOuterEdge;
		PadPose = Pivot + Peel.RotateVector(PadPose - Pivot);
		PadQuat = Peel * PadQuat;
	}
	FVector Ankle = PadPose - PadQuat.RotateVector(RestPad[Index] - RestAnkle);
	FVector Pole = (OutwardWorld * 0.35f + Up * 0.9f).GetSafeNormal();
	// 편 몸에서 나중에 틀에 걸릴 무릎은 미리 그 자리 쪽으로 굽힌다(무릎 이음이 그쪽으로 천천히 돌려 준다).
	if (HintLeg == Index && State == EIGLiningState::Spread)
	{
		const FVector ToHint = KneeHint - Hip;
		if (!ToHint.IsNearlyZero())
		{
			Pole = ToHint.GetSafeNormal();
		}
	}
	// 같은 벽을 따라 걷는 동안에는 무릎을 엉덩이→발목 축으로 돌려 벽 면 쪽으로 눕힌다. 극을 같이 돌려야 마디가
	// 꼬이지 않는다.
	const float Lean = LeanKnee(Index, Hip, Ankle, Pole);
	const FVector LegAxis = (Ankle - Hip).GetSafeNormal();
	if (Lean != 0.0f && !LegAxis.IsZero())
	{
		Pole = FQuat(LegAxis, Lean).RotateVector(Pole);
	}
	// 옮기는 길에서 몸 기준 극 쪽이 막히는 다리는 옮기기 전에 정한 극을 쓴다(PlanSwingPole).
	Leg.LastPole = Pole;
	if (Leg.bSwinging && Leg.bSwingPlanned && PinnedLeg != Index && Leg.Fold < 0.001f)
	{
		const float Along = IGLining::Smooth((Leg.SwingAlpha - Hold) / (1.0f - Hold)) * (IGLining::PoleSamples - 1);
		const int32 Before = FMath::Clamp(FMath::FloorToInt(Along), 0, IGLining::PoleSamples - 1);
		const int32 After = FMath::Min(Before + 1, IGLining::PoleSamples - 1);
		const FVector Blended = FMath::Lerp(Leg.SwingPoles[Before], Leg.SwingPoles[After], Along - Before);
		Pole = Blended.IsNearlyZero() ? Leg.SwingPoles[Before] : Blended.GetSafeNormal();
	}
	// 무릎은 극을 엉덩이→발목 축에 수직인 면에 내린 쪽(원 위)에 둔다.
	const IGLining::FKneeCircle Circle = IGLining::KneeCircle(Hip, Ankle, Length1[Index], Length2[Index], Pole);
	// 지난 프레임 무릎 쪽을 새 원에 내려 잇고, 정한 쪽으로는 스윙과 같은 무릎 빠르기 안에서만 돈다. 극이 엉덩이→발목 축과
	// 나란해지면 극의 그림자가 휙 바뀌어, 무릎이 한 프레임에 원을 가로질러 반대로 뒤집혔다(덧댄 것 검사 102 cm, 관리실 81 cm).
	FVector Previous = Leg.KneeBend - Circle.Axis * FVector::DotProduct(Leg.KneeBend, Circle.Axis);
	const bool bHasPrevious = bKneeContinuity && !Leg.KneeBend.IsZero() && Previous.Normalize();
	const float TurnSeconds = GetWorld() ? FMath::Max(GetWorld()->GetDeltaSeconds(), 1.0f / 240.0f) : 1.0f / 60.0f;
	const float MaxTurn = IGLining::SwingKneeSpeed * TurnSeconds / FMath::Max(Circle.Radius, 1.0f);
	FVector KneeDirection = Circle.Bend;
	if (bKneeContinuity && PinnedLeg != Index && Leg.Fold < 0.999f)
	{
		// 넓적다리·정강이 굵기까지 보고 둘레에서 둘 쪽과 도는 쪽을 같이 고른다(ChooseKneeDirection).
		KneeDirection = ChooseKneeDirection(Index, Hip, Ankle, Circle.Center, Circle.Axis, Circle.Bend, Circle.Radius, bHasPrevious,
			Previous, Leg.Normal, MaxTurn);
	}
	else
	{
		// 틀에 걸린 다리와 다 접히는 다리는 걸린 자리·접힌 자리를 따르니 극 쪽으로만 돈다. 반대편까지 돌아야 할 때는 몸
		// 위(방 쪽)로 돈다.
		Leg.KneeTurnSign = 0.0f;
		const float Gap = bHasPrevious ? FMath::Acos(FMath::Clamp(FVector::DotProduct(Previous, Circle.Bend), -1.0f, 1.0f)) : 0.0f;
		if (bHasPrevious && Gap > MaxTurn)
		{
			float Sign = FVector::DotProduct(FVector::CrossProduct(Previous, Circle.Bend), Circle.Axis) >= 0.0f ? 1.0f : -1.0f;
			if (Gap > PI * 0.75f)
			{
				const FVector ViaPlus = FQuat(Circle.Axis, HALF_PI).RotateVector(Previous);
				Sign = FVector::DotProduct(ViaPlus, Up) >= FVector::DotProduct(-ViaPlus, Up) ? 1.0f : -1.0f;
			}
			KneeDirection = FQuat(Circle.Axis, Sign * MaxTurn).RotateVector(Previous);
		}
	}
	const bool bKneeContinuous = !Leg.KneeBend.IsZero();
	Leg.KneeBend = KneeDirection;
	FVector Knee = Circle.Center + KneeDirection * Circle.Radius;
	FQuat Segment3 = PadQuat;

	const FVector IkKnee = Knee;
	// 접은 자세(몸 기준 정해진 자리)와 섞는다.
	if (Leg.Fold > 0.001f)
	{
		FVector FoldKnee, FoldAnkle, FoldPad;
		IGLining::FoldedJoints(Index, HipLocal[Index], FoldKnee, FoldAnkle, FoldPad);
		const FVector WorldKnee = BodyCenter + BodyRotation.RotateVector(FoldKnee);
		const FVector WorldAnkle = BodyCenter + BodyRotation.RotateVector(FoldAnkle);
		const float F = IGLining::Smooth(Leg.Fold);
		// 무릎은 엉덩이를 중심으로 돌려 섞는다. 곧게 섞으면 두 자리가 엉덩이를 사이에 둘 때 섞인 점이 엉덩이 곁을 지나
		// 마디 길이를 맞추는 순간 방향이 휙 뒤집혔다(펴며 옮기는 다리, 한 프레임 53 cm).
		const FVector FromDirection = (Knee - Hip).GetSafeNormal();
		const FVector ToDirection = (WorldKnee - Hip).GetSafeNormal();
		if (!FromDirection.IsZero() && !ToDirection.IsZero())
		{
			const FQuat Turn = FQuat::FindBetweenNormals(FromDirection, ToDirection);
			Knee = Hip + FQuat::Slerp(FQuat::Identity, Turn, F).RotateVector(FromDirection)
				* FMath::Lerp(FVector::Distance(Knee, Hip), FVector::Distance(WorldKnee, Hip), F);
		}
		else
		{
			Knee = FMath::Lerp(Knee, WorldKnee, F);
		}
		Ankle = FMath::Lerp(Ankle, WorldAnkle, F);
		Segment3 = FQuat::Slerp(PadQuat, BodyRotation, F);
		if (Leg.Fold > 0.999f)
		{
			Leg.Pad = BodyCenter + BodyRotation.RotateVector(FoldPad);
			Leg.Normal = Up;
		}
	}
	if (PinnedLeg == Index)
	{
		// 틀에 걸리는 순간 무릎이 한 프레임에 건너뛰지 않게 짧게 옮겨 간다.
		Knee = FMath::Lerp(Knee, PinnedKnee, IGLining::Smooth(PinBlend));
	}
	// 마디 길이를 지킨다(손이 닿지 않으면 다리를 곧게 뻗고 패드가 따라온다).
	const FVector AnkleGoal = Ankle;
	Knee = Hip + (Knee - Hip).GetSafeNormal() * Length1[Index];
	Ankle = Knee + (Ankle - Knee).GetSafeNormal() * Length2[Index];
	// 옮기는 다리의 무릎은 한 프레임에 무릎 빠르기만큼만 움직인다. 옮기기 처음이나 끝에 디딜 자리가 엉덩이 곁이면
	// 엉덩이→발목 축이 한 프레임에 크게 돌아 두 마디 원 전체가 지난 무릎에서 멀어졌다(덧댄 것 검사 91 cm, 남은 짐
	// 122 cm). 그때는 지난 무릎에서 그만큼만 다가간 자리에 무릎을 두고 발목은 거기서 디딜 자리 쪽으로 뻗는다. 공중의
	// 발이 몇 프레임 늦게 따라오고, 늦은 채 디뎠으면 따라잡을 때까지 같은 규칙을 이어 간다.
	FVector HeldBend = FVector::ZeroVector;
	if (bKneeContinuity && bKneeContinuous && bSegmentsPosed && PinnedLeg != Index && Leg.Fold < 0.999f
		&& (Leg.bSwinging || Leg.bKneeCatchUp))
	{
		const float DeltaSeconds = GetWorld() ? FMath::Max(GetWorld()->GetDeltaSeconds(), 1.0f / 240.0f) : 1.0f / 60.0f;
		const float MaxStep = IGLining::SwingKneeSpeed * DeltaSeconds;
		// 몸이 움직인 만큼은 지난 무릎도 같이 옮겨 놓고 잰다.
		const FVector LastKnee = Leg.Knee + (Hip - Leg.Hip);
		const FVector Wanted = Knee - LastKnee;
		Leg.bKneeCatchUp = Wanted.Size() > MaxStep;
		if (Leg.bKneeCatchUp)
		{
			Knee = Hip + (LastKnee + Wanted.GetSafeNormal() * MaxStep - Hip).GetSafeNormal() * Length1[Index];
			Ankle = Knee + (AnkleGoal - Knee).GetSafeNormal() * Length2[Index];
			// 따라잡는 곧은 길이 막힌 데로 들어가면 지난 무릎에 둔다(위와 같은 까닭). 디딜 자리 위에서 기다리는 다리와 디딘
			// 다리는 그러지 않는다. 머문 무릎은 끝내 따라잡지 못해 기다림이 다 지난 뒤 디뎠고, 디딘 다리 무릎이 머무는 동안은
			// 발목이 패드에 닿지 못해 짚은 패드가 내내 끌려 있었다(45 cm).
			const FVector Stay = Hip + (LastKnee - Hip).GetSafeNormal() * Length1[Index];
			const FVector StayAnkle = Stay + (AnkleGoal - Stay).GetSafeNormal() * Length2[Index];
			if (Leg.bSwinging && Leg.LandWaitSeconds <= 0.0f && !IsKneeClear(Hip, Knee, Ankle, false, Leg.Normal)
				&& IsKneeClear(Hip, Stay, StayAnkle, false, Leg.Normal))
			{
				Knee = Stay;
				Ankle = StayAnkle;
			}
			const FVector Axis = (Ankle - Hip).GetSafeNormal();
			HeldBend = Knee - Hip;
			HeldBend -= Axis * FVector::DotProduct(HeldBend, Axis);
			if (HeldBend.Normalize())
			{
				Leg.KneeBend = HeldBend;
			}
			else
			{
				HeldBend = FVector::ZeroVector;
			}
		}
	}
	else
	{
		Leg.bKneeCatchUp = false;
	}
	if (PinnedLeg == Index)
	{
		// 놓일 때 무릎 이음이 걸려 있던 쪽에서 이어 돌도록 그 쪽을 적어 둔다.
		FVector Held = Knee - Circle.Center;
		Held -= Circle.Axis * FVector::DotProduct(Held, Circle.Axis);
		if (Held.Normalize())
		{
			Leg.KneeBend = Held;
		}
	}

	// 마디의 비틀림도 무릎이 실제로 꺾인 쪽을 따른다. 극을 쓰면 무릎을 잇는 동안 마디만 먼저 돌았다.
	const FVector Bend = HeldBend.IsZero() ? KneeDirection : HeldBend;
	const FQuat Segment1 = IGLining::FrameRotation(RestKnee - RestHip, FVector::UpVector, Knee - Hip, Bend);
	const FQuat Segment2 = IGLining::FrameRotation(RestAnkle - RestKnee, FVector::UpVector, Ankle - Knee, Bend);
	// 월드 자리로 적어 두고(물갈퀴·패드 막 뼈가 따른다) 컴포넌트 공간으로 놓는다.
	SegmentWorld[Index][0] = FTransform(Segment1 * RestSegment[Index][0].GetRotation(), Hip, RestSegment[Index][0].GetScale3D());
	SegmentWorld[Index][1] = FTransform(Segment2 * RestSegment[Index][1].GetRotation(), Knee, RestSegment[Index][1].GetScale3D());
	SegmentWorld[Index][2] = FTransform(Segment3 * RestSegment[Index][2].GetRotation(), Ankle, RestSegment[Index][2].GetScale3D());
	const FVector Offset = RestBody.GetLocation() - BodyCenter;
	for (int32 Segment = 0; Segment < 3; ++Segment)
	{
		FTransform Pose = SegmentWorld[Index][Segment];
		Pose.AddToTranslation(Offset);
		Body->SetBoneTransformByName(SegmentBones[Index][Segment], Pose, EBoneSpaces::ComponentSpace);
	}
	if (bMeasureLegs && bKneeContinuous && bSegmentsPosed && State != EIGLiningState::Hidden)
	{
		// 쉬는 자리로 순간 이동한 프레임은 세지 않는다(그 프레임에는 이을 무릎 방향이 비어 있다).
		const float Step = FVector::Distance(Knee, Leg.Knee);
		if (Step > 15.0f)
		{
			++KneeJumpFrames[Index];
		}
		if (Step > MaxKneeStep[Index])
		{
			MaxKneeStep[Index] = Step;
			WorstKneeStep = FString::Printf(TEXT("leg%d=%.1fcm state=%d"), Index, Step, static_cast<int32>(State));
		}
		if (Step > 30.0f)
		{
			UE_LOG(LogIndieGame, Display,
				TEXT("LINING_KNEE_JUMP leg=%d step=%.1f state=%d fold=%.2f swing=%d alpha=%.2f pinned=%d catchup=%d knee=%s ik=%s ikclear=%d hip=%s ankle=%s prev_knee=%s prev_hip=%s prev_ankle=%s"),
				Index, Step, static_cast<int32>(State), Leg.Fold, Leg.bSwinging ? 1 : 0, Leg.SwingAlpha, PinnedLeg == Index ? 1 : 0,
				Leg.bKneeCatchUp ? 1 : 0, *Knee.ToCompactString(), *IkKnee.ToCompactString(), IsKneeClear(Hip, IkKnee, Ankle) ? 1 : 0,
				*Hip.ToCompactString(), *Ankle.ToCompactString(), *Leg.Knee.ToCompactString(), *Leg.Hip.ToCompactString(),
				*Leg.Ankle.ToCompactString());
		}
	}
	Leg.Hip = Hip;
	Leg.Knee = Knee;
	Leg.Ankle = Ankle;
	// 패드 덩어리는 끝 마디에 붙어 돈다(쉬는 자세의 발목→패드를 그대로 돌린다).
	Leg.PadCenter = Ankle + Segment3.RotateVector(RestPad[Index] - RestAnkle);
	Leg.PadFrame = Segment3 * FRotationMatrix::MakeFromXZ(RestOutward[Index], FVector::UpVector).ToQuat();
	// 짚고 있는 발이 짚은 자리에서 벗어났는가(다리가 닿지 않으면 마디 길이를 지키느라 발이 끌려온다).
	if (!Leg.bSwinging && Leg.Fold < 0.01f && PinnedLeg != Index)
	{
		MaxPadSlip = FMath::Max(MaxPadSlip, FVector::Distance(Leg.PadCenter, PadPose));
		if (MaxPadSlip >= NextSlipLog)
		{
			UE_LOG(LogIndieGame, Display, TEXT("LINING_SLIP leg=%d state=%d pulling=%d blocked=%d slip=%.1f reach=%.2f"),
				Index, static_cast<int32>(State), bPulling ? 1 : 0, BlockedPulls, MaxPadSlip, LegReach(Index, BodyCenter, BodyRotation));
			NextSlipLog = MaxPadSlip * 2.0f;
		}
	}
}

float AIGLiningEntity::LeanKnee(const int32 Leg, const FVector& Hip, const FVector& Ankle, const FVector& Pole)
{
	float Target = 0.0f;
	UWorld* World = GetWorld();
	if (KneeLean <= 0.0f || !World || Legs[Leg].Fold >= 0.999f || PinnedLeg == Leg)
	{
		KneeLeanSide[Leg] = 0;
		KneeLeanHold[Leg] = 0.0f;
	}
	else
	{
		const IGLining::FKneeCircle Circle = IGLining::KneeCircle(Hip, Ankle, Length1[Leg], Length2[Leg], Pole);
		// SolveLeg처럼 접힌 자세와 Blend만큼 섞고 첫 마디 길이로 맞춘 무릎. 펴지는 중인 다리는 접힌 무릎 쪽으로 끌려
		// 있어서 덜 눕혀도 되고, Blend 0은 다 편 다리다.
		const float Fold = Legs[Leg].Fold > 0.001f ? IGLining::Smooth(Legs[Leg].Fold) : 0.0f;
		FVector FoldKnee = FVector::ZeroVector;
		if (Fold > 0.0f)
		{
			FVector FoldAnkle;
			FVector FoldPad;
			IGLining::FoldedJoints(Leg, HipLocal[Leg], FoldKnee, FoldAnkle, FoldPad);
			FoldKnee = BodyCenter + BodyRotation.RotateVector(FoldKnee);
		}
		const auto KneeAt = [this, Leg, &Hip, &Circle, &FoldKnee](const float Angle, const float Blend)
		{
			return Hip + (FMath::Lerp(Circle.At(Angle), FoldKnee, Blend) - Hip).GetSafeNormal() * Length1[Leg];
		};
		// 무릎 높이(벽 면에서 방 쪽).
		const auto HeightAt = [this, &KneeAt](const float Angle, const float Blend)
		{
			return FVector::DotProduct(KneeAt(Angle, Blend) - PathWallPoint, PathWallNormal);
		};
		// 그쪽(Sign)으로 한도까지 내려오는 가장 작은 각. 어디까지 돌려도 한도를 넘으면 가장 낮아지는 각이다.
		const auto NeededFor = [&HeightAt](const float Sign, const float Blend)
		{
			const float Start = HeightAt(0.0f, Blend);
			if (Start <= IGLining::AlongWallKneeHeight)
			{
				return 0.0f;
			}
			constexpr int32 Samples = 14;
			constexpr float MaxAngle = PI * 0.8f;
			float Previous = 0.0f;
			float Lowest = 0.0f;
			float LowestHeight = Start;
			for (int32 Index = 1; Index <= Samples; ++Index)
			{
				const float Theta = MaxAngle * Index / Samples;
				const float Sampled = HeightAt(Sign * Theta, Blend);
				if (Sampled <= IGLining::AlongWallKneeHeight)
				{
					float Low = Previous;
					float High = Theta;
					for (int32 Step = 0; Step < 5; ++Step)
					{
						const float Mid = 0.5f * (Low + High);
						(HeightAt(Sign * Mid, Blend) <= IGLining::AlongWallKneeHeight ? High : Low) = Mid;
					}
					return High;
				}
				if (Sampled < LowestHeight)
				{
					LowestHeight = Sampled;
					Lowest = Theta;
				}
				Previous = Theta;
			}
			return Lowest;
		};
		// 눕힌 무릎이 천장·바닥·옆 벽이나 소품에 묻히거나 그 너머로 넘어가거나, 몸 위에 얹히거나, 다른 다리의 무릎과
		// 맞닿으면 거기까지 눕히지 않는다.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningKnee), false, this);
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			Params.AddIgnoredActor(PlayerPawn);
		}
		const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
		const FCollisionShape Knob = FCollisionShape::MakeSphere(IGLining::LegRadius);
		const auto IsClear = [this, World, Leg, &Hip, &KneeAt, &Params, &Objects, &Knob](const float Angle, const float Blend)
		{
			const FVector Knee = KneeAt(Angle, Blend);
			const FVector Local = BodyRotation.UnrotateVector(Knee - BodyCenter);
			if (FMath::Abs(Local.X) < IGLining::BodyHalf.X && FMath::Abs(Local.Y) < IGLining::BodyHalf.Y)
			{
				return false;
			}
			for (int32 Other = 0; Other < LegCount; ++Other)
			{
				if (Other != Leg && FVector::DistSquared(Knee, Legs[Other].Knee) < FMath::Square(IGLining::LegRadius * 2.0f + 4.0f))
				{
					return false;
				}
			}
			FHitResult Hit;
			return !World->LineTraceSingleByObjectType(Hit, Hip, Knee, Objects, Params)
				&& !World->OverlapAnyTestByObjectType(Knee, FQuat::Identity, Objects, Knob, Params);
		};
		// 한도 쪽에서부터 덜 눕혀 가며 처음으로 비어 있는 각.
		const auto ClearFor = [&IsClear](const float Sign, const float Needed, const float Blend)
		{
			constexpr int32 Tries = 6;
			for (int32 Try = 0; Try < Tries; ++Try)
			{
				const float Theta = Needed * (1.0f - static_cast<float>(Try) / Tries);
				if (IsClear(Sign * Theta, Blend))
				{
					return Theta;
				}
			}
			return 0.0f;
		};
		// 다 편 다리를 그쪽으로 막힘 없이 눕힐 수 있는 가장 큰 각. 조금씩 늘려 가며 처음 막히기 전까지다.
		const auto RoomFor = [&IsClear](const float Sign)
		{
			constexpr int32 Steps = 9;
			constexpr float MaxRoom = PI * 0.75f;
			float Room = 0.0f;
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				const float Theta = MaxRoom * Step / Steps;
				if (!IsClear(Sign * Theta, 0.0f))
				{
					break;
				}
				Room = Theta;
			}
			return Room;
		};
		const float Height = HeightAt(0.0f, Fold);
		int32& Side = KneeLeanSide[Leg];
		if (Side == 0 && Height > IGLining::AlongWallKneeHeight)
		{
			// 몸 바깥쪽으로 눕힌다. 어느 쪽도 아니면 극이 벌써 기울인 쪽으로 눕혀서, 한도를 넘는 순간에 무릎이 튀지 않는다.
			// 그쪽이 천장이나 바닥에 가까워 다리를 다 폈을 때 금방 막히면 넓은 쪽으로 눕힌다. 걷다가 막혀 반대쪽으로
			// 넘기면 그동안 무릎이 방 쪽으로 넘어간다.
			FVector Away = Circle.Center - BodyCenter;
			Away -= PathWallNormal * FVector::DotProduct(Away, PathWallNormal);
			const float AwayScore = Away.Normalize() ? FVector::DotProduct(Circle.Side, Away) : 0.0f;
			const float Tilt = FVector::DotProduct(Circle.Side, PathWallNormal);
			const int32 Preferred = FMath::Abs(AwayScore) > 0.25f ? (AwayScore > 0.0f ? 1 : -1) : (Tilt > 0.0f ? -1 : 1);
			const float PreferredRoom = RoomFor(Preferred);
			Side = PreferredRoom >= NeededFor(Preferred, 0.0f) + IGLining::KneeLeanSpare || PreferredRoom >= RoomFor(-Preferred)
				? Preferred : -Preferred;
		}
		else if (Side != 0 && Height < IGLining::AlongWallKneeHeight - 4.0f)
		{
			// 눕히지 않아도 한도 안이다. 다음에 다시 넘으면 그때 쪽을 정한다.
			Side = 0;
		}
		KneeLeanHold[Leg] = FMath::Max(0.0f, KneeLeanHold[Leg] - PoseDeltaSeconds);
		const float Needed = Side != 0 ? NeededFor(Side, Fold) : 0.0f;
		float Clear = Side != 0 ? ClearFor(Side, Needed, Fold) : 0.0f;
		if (Side != 0 && Clear < Needed * 0.5f && KneeLeanHold[Leg] <= 0.0f)
		{
			// 몸이 옮겨 가며 눕힌 쪽이 천장·바닥이나 몸, 다른 무릎에 막혔다. 반대쪽이 다 비어 있으면 그쪽으로 넘긴다.
			// 넘긴 뒤 잠깐은 다시 넘기지 않는다(두 다리가 서로를 막으면 번갈아 넘기며 떨었다).
			const float OtherNeeded = NeededFor(-Side, Fold);
			const float OtherClear = ClearFor(-Side, OtherNeeded, Fold);
			if (OtherClear >= OtherNeeded - KINDA_SMALL_NUMBER)
			{
				Side = -Side;
				Clear = OtherClear;
				KneeLeanHold[Leg] = IGLining::KneeLeanSwitchHold;
			}
		}
		Target = Side * Clear * KneeLean;
	}
	// 눕히는 각은 한 틱에 뛰지 않고 원을 따라 돈다. 순간이동으로 놓을 때는 바로 맞춘다.
	const float Step = PoseDeltaSeconds > 0.0f ? IGLining::KneeLeanRate * PoseDeltaSeconds : PI;
	KneeLeanAngle[Leg] = FMath::Clamp(Target, KneeLeanAngle[Leg] - Step, KneeLeanAngle[Leg] + Step);
	return KneeLeanAngle[Leg];
}

void AIGLiningEntity::ResetKneeLean()
{
	KneeLean = 0.0f;
	for (int32 Leg = 0; Leg < LegCount; ++Leg)
	{
		KneeLeanAngle[Leg] = 0.0f;
		KneeLeanSide[Leg] = 0;
		KneeLeanHold[Leg] = 0.0f;
		// 다리를 새로 놓는 자리다(쉬는 자리, 틈을 누른 채 놓기, 숨기). 지난 무릎 쪽을 잇지 않는다.
		Legs[Leg].KneeBend = FVector::ZeroVector;
		Legs[Leg].bKneeCatchUp = false;
		Legs[Leg].KneeTurnSign = 0.0f;
	}
}

void AIGLiningEntity::UpdateColliders()
{
	// 몸판 상자는 눌리고 늘어난 몸을 따라가고(벽에 붙은 바닥면은 제자리), 처진 살 쪽으로 반만큼 비킨다.
	const FVector Extent = IGLining::BodyHalf * BodySquash;
	if (!BodyBlocker->GetUnscaledBoxExtent().Equals(Extent, 0.3f))
	{
		BodyBlocker->SetBoxExtent(Extent, false);
	}
	FVector Sag = FVector::ZeroVector;
	for (const FSoftPoint& Point : Mass)
	{
		Sag += Point.Offset;
	}
	const FVector BoxCenter = BodyCenter - BodyRotation.GetUpVector() * (1.0f - BodySquash.Z) * IGLining::PressStandOff
		+ Sag * (0.5f / LegCount);
	BodyBlocker->SetWorldLocationAndRotation(BoxCenter, BodyRotation, false, nullptr, ETeleportType::TeleportPhysics);
	for (int32 Index = 0; Index < LegCount; ++Index)
	{
		const FLeg& Leg = Legs[Index];
		const FVector Segments[2][2] = {{Leg.Hip, Leg.Knee}, {Leg.Knee, Leg.Ankle}};
		for (int32 Part = 0; Part < 2; ++Part)
		{
			UCapsuleComponent* Capsule = LegBlockers.IsValidIndex(Index * 2 + Part) ? LegBlockers[Index * 2 + Part].Get() : nullptr;
			if (!Capsule)
			{
				continue;
			}
			const FVector A = Segments[Part][0];
			const FVector B = Segments[Part][1];
			const FVector Dir = B - A;
			const float Half = Dir.Size() * 0.5f;
			Capsule->SetCapsuleSize(IGLining::LegRadius, FMath::Max(Half, IGLining::LegRadius + 1.0f));
			Capsule->SetWorldLocationAndRotation((A + B) * 0.5f, FRotationMatrix::MakeFromZ(Dir.GetSafeNormal()).ToQuat(),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
		// 패드 덩어리는 짚은 면에 붙은 납작한 상자다.
		if (UBoxComponent* Box = PadBlockers.IsValidIndex(Index) ? PadBlockers[Index].Get() : nullptr)
		{
			Box->SetWorldLocationAndRotation(Leg.PadCenter + Leg.PadFrame.GetUpVector() * IGLining::PadHalf.Z, Leg.PadFrame,
				false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
#if ENABLE_DRAW_DEBUG
	if (IGLining::DrawCollision() && GetWorld())
	{
		// 쉬는 동안은 0.1초에 한 번 틱하므로 선을 그만큼 남긴다.
		const float Life = PrimaryActorTick.TickInterval > 0.0f ? PrimaryActorTick.TickInterval + 0.05f : -1.0f;
		DrawDebugBox(GetWorld(), BodyBlocker->GetComponentLocation(), BodyBlocker->GetScaledBoxExtent(), BodyBlocker->GetComponentQuat(),
			FColor::Cyan, false, Life, 0, 1.0f);
		for (const UCapsuleComponent* Capsule : LegBlockers)
		{
			DrawDebugCapsule(GetWorld(), Capsule->GetComponentLocation(), Capsule->GetScaledCapsuleHalfHeight(),
				Capsule->GetScaledCapsuleRadius(), Capsule->GetComponentQuat(), FColor::Yellow, false, Life, 0, 1.0f);
		}
		for (const UBoxComponent* Box : PadBlockers)
		{
			DrawDebugBox(GetWorld(), Box->GetComponentLocation(), Box->GetScaledBoxExtent(), Box->GetComponentQuat(),
				FColor::Orange, false, Life, 0, 1.0f);
		}
	}
#endif
}

void AIGLiningEntity::SetBlockersEnabled(const bool bEnabled)
{
	const ECollisionEnabled::Type Mode = bEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
	BodyBlocker->SetCollisionEnabled(Mode);
	for (UCapsuleComponent* Capsule : LegBlockers)
	{
		Capsule->SetCollisionEnabled(Mode);
	}
	for (UBoxComponent* Box : PadBlockers)
	{
		Box->SetCollisionEnabled(Mode);
	}
}

void AIGLiningEntity::CheckGrip()
{
	if (!bGripEnabled || !IsMoving() || State == EIGLiningState::Gripping || State == EIGLiningState::Retreating)
	{
		return;
	}
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		return;
	}
	const FVector Chest = Player->GetActorLocation();
	bool bCaught = false;
	FVector CaughtBy = BodyCenter;
	// 옮기는 패드가 닿거나, 움직이는 몸판이 사람을 덮친다.
	for (const FLeg& Leg : Legs)
	{
		if (Leg.bSwinging && Leg.SwingAlpha > 0.35f && FVector::Distance(Leg.Pad, Chest) < IGLining::GripRadius + 24.0f)
		{
			bCaught = true;
			CaughtBy = Leg.Pad;
			break;
		}
	}
	if (!bCaught && (State == EIGLiningState::Stepping || State == EIGLiningState::Pressing || State == EIGLiningState::Bracing))
	{
		const FVector Local = BodyRotation.UnrotateVector(Chest - BodyCenter);
		bCaught = FMath::Abs(Local.X) < IGLining::BodyHalf.X + IGLining::GripRadius
			&& FMath::Abs(Local.Y) < IGLining::BodyHalf.Y + IGLining::GripRadius
			&& FMath::Abs(Local.Z) < IGLining::BodyHalf.Z + IGLining::GripDepth;
	}
	if (!bCaught)
	{
		return;
	}
	StateBeforeGrip = State;
	State = EIGLiningState::Gripping;
	UE_LOG(LogIndieGame, Display, TEXT("LINING_GRIP at=%s state=%d by=%s body=%s"), *Chest.ToCompactString(),
		static_cast<int32>(StateBeforeGrip), *CaughtBy.ToCompactString(), *BodyCenter.ToCompactString());
	PlayCue(TEXT("Lining_Press"), 1, Chest, 1.0f, 0.82f);
	OnGrippedPlayer.Broadcast(this);
}

void AIGLiningEntity::SetObstructions(const FName GapId, const bool bObstructed, const FGapPose* Pose)
{
	if (GapId.IsNone())
	{
		return;
	}
	if (const FIGLiningGap* Gap = FindGap(GapId))
	{
		if (AIGAccessHatch* Hatch = Gap->Hatch.Get())
		{
			if (bObstructed && Pose && Pose->bValid)
			{
				const float Yaw = FindBlockedHatchYaw(*Gap, *Pose);
				Hatch->SetObstructed(true, Yaw);
				UE_LOG(LogIndieGame, Display, TEXT("LINING_HATCH_BLOCKED %s yaw=%.0f"), *GapId.ToString(), Yaw);
			}
			else
			{
				Hatch->SetObstructed(bObstructed);
			}
		}
	}
}

void AIGLiningEntity::SealGap(const FName GapId)
{
	for (FIGLiningGap& Gap : Gaps)
	{
		if (Gap.Id == GapId)
		{
			Gap.bSealed = true;
			if (AIGAccessHatch* Hatch = Gap.Hatch.Get())
			{
				Hatch->SetSealed(true);
			}
			UE_LOG(LogIndieGame, Display, TEXT("LINING_SEALED %s"), *GapId.ToString());
		}
	}
	RefreshGapDrafts();
}

void AIGLiningEntity::PlayCue(const TCHAR* Sample, const int32 Variants, const FVector& Where, const float Volume, const float Pitch)
{
	if (!bAudible)
	{
		return;
	}
	USoundBase* Sound = nullptr;
	const FString Name(Sample);
	if (Variants > 1)
	{
		Sound = IGAudio::SampleVariant(*Name, Variants, CueSeed++);
	}
	else
	{
		Sound = IGAudio::Sample(*Name);
	}
	if (!Sound)
	{
		if (Name == TEXT("Lining_Plant"))
		{
			Sound = UIGToneSequenceSoundWave::CreateKickedPropKnock(this, false);
		}
		else if (Name == TEXT("Lining_Creak") || Name == TEXT("Lining_FrameCatch"))
		{
			Sound = UIGToneSequenceSoundWave::CreateSettleTimberCreak(this);
		}
		else
		{
			Sound = UIGToneSequenceSoundWave::CreatePackingTapeRip(this);
		}
	}
	IGAudio::SpawnExpendableOneShotAt(this, Sound, Where, Volume, Pitch, 120.0f, 1500.0f, EIGAudioBus::Entity);
}

void AIGLiningEntity::RefreshGapDrafts()
{
	TSet<FName> Wanted;
	if (State != EIGLiningState::Hidden && bAudible && GetWorld())
	{
		for (const FIGLiningGap& Gap : Gaps)
		{
			if (!Gap.bSealed && IsGapOpen(Gap.Id))
			{
				Wanted.Add(Gap.Id);
			}
		}
	}
	for (auto It = GapDrafts.CreateIterator(); It; ++It)
	{
		if (!Wanted.Contains(It.Key()))
		{
			if (UAudioComponent* Audio = It.Value().Get())
			{
				// 막히면 바람이 먹히듯 줄어 끊긴다.
				Audio->bAutoDestroy = true;
				Audio->FadeOut(0.6f, 0.0f);
			}
			It.RemoveCurrent();
		}
	}
	USoundBase* Wind = Wanted.Num() > 0 ? IGAudio::Sample(TEXT("Wind_Gap")) : nullptr;
	if (!Wind)
	{
		return;
	}
	for (const FName Id : Wanted)
	{
		const FIGLiningGap* Gap = FindGap(Id);
		if (!Gap || GapDrafts.Contains(Id))
		{
			continue;
		}
		UAudioComponent* Audio = NewObject<UAudioComponent>(this);
		Audio->bAutoActivate = false;
		Audio->bAutoDestroy = false;
		Audio->bAllowSpatialization = true;
		Audio->SetSound(Wind);
		Audio->AttenuationSettings = IGAudio::MakeAttenuation(this, 50.0f, 420.0f, EIGAudioBus::World);
		Audio->SetUsingAbsoluteLocation(true);
		Audio->RegisterComponent();
		Audio->SetWorldLocation(Gap->Center + Gap->Normal.GetSafeNormal() * 6.0f);
		// 넓은 틈일수록 낮게 운다.
		Audio->SetPitchMultiplier(FMath::GetMappedRangeValueClamped(FVector2D(30.0f, 90.0f), FVector2D(1.15f, 0.8f), FMath::Sqrt(Gap->Area())));
		Audio->FadeIn(1.2f, 0.16f);
		GapDrafts.Add(Id, Audio);
	}
}

void AIGLiningEntity::Caption(const FText& Text, const FVector& Where, const float Seconds, double& LastAt, const float MinInterval)
{
	const UWorld* World = GetWorld();
	if (!World || !bAudible)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now - LastAt < MinInterval)
	{
		return;
	}
	LastAt = Now;
	AIGHorrorHUD::PushAudioCaptionAt(this, Text, Seconds, Where);
}

bool AIGLiningEntity::IsKneeClear(const FVector& Hip, const FVector& Knee, const FVector& Ankle, const bool bThighOnly,
	const FVector& StandNormal) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningKneeClear), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	FHitResult Hit;
	// 발목 끝은 짚은 면 바로 위라 4 cm 덜 본다. 관통 검사(FindLegObstruction)와 같은 물체를 본다. 짚은 면 법선을 알면 그
	// 면에 닿는 것만 덜 보고, 옆 소품은 끝까지 본다. 관리실 작업대 옆 바닥을 짚은 정강이 끝이 작업대 아래 모서리를 1.5 cm
	// 지났는데 이 4 cm에 들어 빈 쪽으로 보였다.
	const FVector Down = Ankle - Knee;
	const float DownLength = Down.Size();
	const FVector AnkleEnd = DownLength <= 5.0f ? Knee : Ankle - Down / DownLength * 4.0f;
	const bool bShin = !bThighOnly && DownLength > 5.0f;
	if (World->LineTraceSingleByObjectType(Hit, Hip, Knee, Objects, Params))
	{
		return false;
	}
	if (bShin && World->LineTraceSingleByObjectType(Hit, Knee, StandNormal.IsZero() ? AnkleEnd : Ankle, Objects, Params)
		&& (StandNormal.IsZero() || FVector::Distance(Hit.ImpactPoint, Ankle) > 4.0f
			|| FVector::DotProduct(Hit.ImpactNormal, StandNormal) < 0.7f))
	{
		return false;
	}
	// 짚거나 막고 있는 틈의 열린 문짝도 지나지 않는다.
	UStaticMeshComponent* Door = ActiveHatchDoor();
	const FCollisionQueryParams DoorParams(SCENE_QUERY_STAT(LiningKneeDoor), false);
	return !Door || (!Door->LineTraceComponent(Hit, Hip, Knee, DoorParams)
		&& (!bShin || !Door->LineTraceComponent(Hit, Knee, AnkleEnd, DoorParams)));
}

bool AIGLiningEntity::IsLegThickClear(const FVector& Hip, const FVector& Knee, const FVector& Ankle, const float Radius,
	const FVector& StandNormal) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningLegThick), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
	const FCollisionQueryParams DoorParams(SCENE_QUERY_STAT(LiningLegThickDoor), false);
	UStaticMeshComponent* Door = ActiveHatchDoor();
	FHitResult Hit;
	const FVector Up = Knee - Hip;
	const float UpLength = Up.Size();
	if (UpLength > Radius + 1.0f)
	{
		const FVector ThighStart = Hip + Up / UpLength * Radius;
		if (World->SweepSingleByObjectType(Hit, ThighStart, Knee, FQuat::Identity, Objects, Sphere, Params)
			|| (Door && Door->SweepComponent(Hit, ThighStart, Knee, FQuat::Identity, Sphere)))
		{
			return false;
		}
	}
	const FVector Down = Ankle - Knee;
	const float DownLength = Down.Size();
	if (DownLength > 10.0f)
	{
		const FVector ShinEnd = Ankle - Down / DownLength * 8.0f;
		if (World->SweepSingleByObjectType(Hit, Knee, ShinEnd, FQuat::Identity, Objects, Sphere, Params)
			&& (StandNormal.IsZero() || FVector::Distance(Hit.Location, Ankle) > 25.0f
				|| FVector::DotProduct(Hit.ImpactNormal, StandNormal) < 0.7f))
		{
			return false;
		}
		if (Door && Door->SweepComponent(Hit, Knee, ShinEnd, FQuat::Identity, Sphere))
		{
			return false;
		}
	}
	return true;
}

int32 AIGLiningEntity::LegRoom(const FVector& Hip, const FVector& Knee, const FVector& Ankle, const FVector& StandNormal) const
{
	if (!IsKneeClear(Hip, Knee, Ankle, false, StandNormal))
	{
		return 0;
	}
	return IsLegThickClear(Hip, Knee, Ankle, IGLining::KneeRoomRadius, StandNormal) ? 2 : 1;
}

FVector AIGLiningEntity::ChooseKneeDirection(const int32 Index, const FVector& Hip, const FVector& Ankle, const FVector& Center,
	const FVector& Axis, const FVector& PoleSide, const float Radius, const bool bHasPrevious, const FVector& Previous,
	const FVector& StandNormal, const float MaxTurn)
{
	// 예전에는 극 쪽이 막히면 극에서 가장 가까운 빈 쪽(가운데 선 기준)을 목표로 삼고, 그쪽으로 도는 길이 막히면 반대쪽 호·
	// 멈춤 규칙을 덧대 비켰다. 다리 겉은 가운데 선보다 12 cm 굵어 빈 쪽으로 판정된 무릎도 벽·소품에 묻혔고, 돌아가는 길은
	// 매번 다른 규칙이 맡아 한 곳을 막으면 다른 곳이 뚫렸다. 이제 둘레를 한 번에 재서 목표와 도는 쪽을 같이 정한다.
	FLeg& Leg = Legs[Index];
	const auto At = [&Center, Radius](const FVector& Direction) { return Center + Direction * Radius; };
	const auto RoomOf = [this, &Hip, &Ankle, &StandNormal, &At](const FVector& Direction)
	{
		return LegRoom(Hip, At(Direction), Ankle, StandNormal);
	};
	const auto AngleBetween = [](const FVector& A, const FVector& B)
	{
		return FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.0f, 1.0f));
	};
	const auto SignTo = [&Axis](const FVector& From, const FVector& To)
	{
		return FVector::DotProduct(FVector::CrossProduct(From, To), Axis) >= 0.0f ? 1.0f : -1.0f;
	};
	// 빠른 길: 극 쪽이 굵기만큼 비었고 지난 쪽에서 거기까지 짧은 호도 15도마다 굵기만큼 비었으면 그쪽으로 돈다. 빈 방에서
	// 짚은 다리는 대부분 여기서 끝난다.
	if (RoomOf(PoleSide) == 2)
	{
		if (!bHasPrevious)
		{
			Leg.KneeTurnSign = 0.0f;
			return PoleSide;
		}
		const float Gap = AngleBetween(Previous, PoleSide);
		if (Gap <= MaxTurn)
		{
			Leg.KneeTurnSign = 0.0f;
			return PoleSide;
		}
		const float Sign = SignTo(Previous, PoleSide);
		bool bArcClear = true;
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Gap / FMath::DegreesToRadians(15.0f)));
		for (int32 Step = 0; Step < Steps && bArcClear; ++Step)
		{
			bArcClear = RoomOf(FQuat(Axis, Sign * Gap * Step / Steps).RotateVector(Previous)) == 2;
		}
		if (bArcClear)
		{
			Leg.KneeTurnSign = 0.0f;
			return FQuat(Axis, Sign * MaxTurn).RotateVector(Previous);
		}
	}
	// 둘레 24쪽(15도마다)의 빈 정도를 잰다. 0번이 극 쪽이다.
	constexpr int32 Count = 24;
	FVector Dirs[Count];
	int32 Rooms[Count];
	int32 BestRoom = 0;
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		Dirs[Slot] = FQuat(Axis, TWO_PI * Slot / Count).RotateVector(PoleSide);
		Rooms[Slot] = RoomOf(Dirs[Slot]);
		BestRoom = FMath::Max(BestRoom, Rooms[Slot]);
	}
	if (BestRoom == 0)
	{
		// 어디에도 둘 곳이 없다(엉덩이와 발목이 포개져 원이 벽·소품 속을 돈다). 지난 쪽을 지킨다. 옮기기 전에 길을 정한
		// 다리는 그 길(극 쪽)을 따라 돈다. 지난 쪽을 지키면 막힌 몇 프레임 사이 무릎이 원과 함께 엉뚱한 쪽으로 흘러가,
		// 길 끝의 빈 자리로 돌아오지 못했다(안쪽 방 큰 점검구).
		if (bHasPrevious && Leg.bSwinging && Leg.bSwingPlanned)
		{
			const float Gap = AngleBetween(Previous, PoleSide);
			return Gap <= MaxTurn ? PoleSide : FQuat(Axis, SignTo(Previous, PoleSide) * MaxTurn).RotateVector(Previous);
		}
		return bHasPrevious ? Previous : PoleSide;
	}
	// 가장 잘 빈 쪽 가운데 극에 가깝고(1) 지난 쪽에 가까운(0.5) 곳을 목표로 삼는다.
	int32 TargetSlot = 0;
	float BestCost = TNumericLimits<float>::Max();
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		if (Rooms[Slot] != BestRoom)
		{
			continue;
		}
		const float Cost = AngleBetween(Dirs[Slot], PoleSide) + (bHasPrevious ? 0.5f * AngleBetween(Dirs[Slot], Previous) : 0.0f);
		if (Cost < BestCost)
		{
			BestCost = Cost;
			TargetSlot = Slot;
		}
	}
	const FVector Target = Dirs[TargetSlot];
	if (!bHasPrevious)
	{
		Leg.KneeTurnSign = 0.0f;
		return Target;
	}
	const float Gap = AngleBetween(Previous, Target);
	if (Gap <= MaxTurn)
	{
		Leg.KneeTurnSign = 0.0f;
		return Target;
	}
	// 지난 쪽에서 목표까지 두 호에서 가장 덜 빈 방향. 지난 쪽은 가장 가까운 15도 방향으로 친다. 화면 주사율이 높으면 한
	// 프레임에 도는 각이 작아 지난 쪽이 목표와 같은 방향으로 잡힐 수 있다. 그때 방향을 차례로 돌며 재면 원 한 바퀴를 재게
	// 되어 두 호가 다 막힌 것으로 나오고, 무릎이 목표 몇 도 앞에서 멈췄다.
	const float FromPole = SignTo(PoleSide, Previous) * AngleBetween(PoleSide, Previous);
	const int32 PreviousSlot = (FMath::RoundToInt(FromPole / (TWO_PI / Count)) % Count + Count) % Count;
	const auto ArcRoom = [&Rooms, PreviousSlot, TargetSlot](const int32 Step)
	{
		int32 Least = Rooms[TargetSlot];
		if (PreviousSlot == TargetSlot)
		{
			return Least;
		}
		for (int32 Slot = (PreviousSlot + Step + Count) % Count; Slot != TargetSlot; Slot = (Slot + Step + Count) % Count)
		{
			Least = FMath::Min(Least, Rooms[Slot]);
		}
		return Least;
	};
	const int32 PlusRoom = ArcRoom(1);
	const int32 MinusRoom = ArcRoom(-1);
	const float ShortSign = SignTo(Previous, Target);
	float Sign = ShortSign;
	if (Leg.KneeTurnSign != 0.0f && (Leg.KneeTurnSign > 0.0f ? PlusRoom : MinusRoom) >= 1)
	{
		// 정한 쪽이 아직 비어 있으면 닿을 때까지 지킨다(프레임마다 고르면 두 자리를 오갔다).
		Sign = Leg.KneeTurnSign;
	}
	else if (PlusRoom >= 1 || MinusRoom >= 1)
	{
		Sign = PlusRoom != MinusRoom ? (PlusRoom > MinusRoom ? 1.0f : -1.0f) : ShortSign;
		Leg.KneeTurnSign = Sign;
	}
	else
	{
		// 두 호가 다 막혔다. 지난 쪽이 비어 있으면 길이 날 때까지 지키고, 지난 쪽부터 막혀 있으면 짧은 쪽으로 빠져나간다.
		Leg.KneeTurnSign = 0.0f;
		if (RoomOf(Previous) >= 1)
		{
			return Previous;
		}
	}
	return FQuat(Axis, Sign * MaxTurn).RotateVector(Previous);
}

void AIGLiningEntity::PlanSwingPole(const int32 Index)
{
	// 무릎은 프레임마다 지금 빈 쪽만 보고 고르니, 옮기는 사이 빈 틈이 닫히면 막힌 데를 지나 다른 틈으로 빠져나갔다. 안쪽 방 큰
	// 점검구로 가며 걸을 때 1번 다리 무릎이 극 쪽(바닥)을 두 번 가로질렀고(17 cm), 짚을 때도 극 쪽이 몇 프레임 만에 닫혀
	// 바닥으로 빠졌다(34 cm). 옮기는 내내 빈 한 방향은 없었다. 빈 쪽이 패드를 따라 옮겨 가기 때문이다. 그래서 옮기기 전에
	// 스윙을 여덟 자리로 나눠 자리마다 무릎 원 24방향의 빈 정도를 재고, 무릎 빠르기 안에서 빈 방향만 이어 가는 길을 찾는다.
	// 극은 옮기는 동안 그 길을 따른다. 몸 기준 극만으로도 내내 비어 있으면 그대로 둔다(벽 따라 걸을 때 눕히기도 그대로).
	FLeg& Leg = Legs[Index];
	Leg.bSwingPlanned = false;
	Leg.bFlatPeel = false;
	if (!CanPlanKneePath(Index))
	{
		return;
	}
	// 걷는 동안 옮기는 사이에는 몸이 서 있다. 틈을 짚을 때는 몸이 짚을 자리로 간다.
	const bool bBodyMoves = State == EIGLiningState::Bracing;
	// 엉덩이 바로 밑(다리 길이 30% 안)에서 출발하는 짚기는 패드를 평평하게 뗀다(SolveLeg).
	Leg.bFlatPeel = bBodyMoves && LegReach(Index, BodyCenter, BodyRotation) < 0.3f;
	const FQuat RotationEnd = bBodyMoves ? MoveToRotation : BodyRotation;
	FKneePath Path;
	MeasureKneePath(Index, Leg, HipWorld(Index, BodyCenter, BodyRotation), BodyRotation,
		HipWorld(Index, bBodyMoves ? MoveToCenter : BodyCenter, RotationEnd), RotationEnd, Path);
	// 몸 기준 극을 그대로 쓸 때보다 나을 때만 길을 따른다.
	if (Path.bDefaultClear || Path.Score <= Path.DefaultScore + 1.0f)
	{
		return;
	}
	for (int32 Sample = 0; Sample < IGLining::PoleSamples; ++Sample)
	{
		Leg.SwingPoles[Sample] = Path.Poles[Sample];
	}
	Leg.bSwingPlanned = true;
}

bool AIGLiningEntity::CanPlanKneePath(const int32 Index) const
{
	const FLeg& Leg = Legs[Index];
	return bKneeContinuity && PinnedLeg != Index && !Leg.LastPole.IsZero() && Leg.FoldFrom <= 0.001f && Leg.FoldTo <= 0.001f;
}

void AIGLiningEntity::MeasureKneePath(const int32 Index, const FLeg& Swing, const FVector& HipFrom, const FQuat& RotationFrom,
	const FVector& HipTo, const FQuat& RotationTo, FKneePath& Out) const
{
	constexpr int32 Samples = IGLining::PoleSamples;
	constexpr int32 Count = 24;
	// 스윙의 한 자리에서 다음 자리까지 무릎이 도는 한도(45도). 가운데 몫에서 스윙이 가장 빠를 때 무릎 빠르기로 도는 각이다.
	constexpr int32 MaxSlots = 3;
	FVector Hips[Samples];
	FVector Ankles[Samples];
	FVector Normals[Samples];
	for (int32 Sample = 0; Sample < Samples; ++Sample)
	{
		const float T = static_cast<float>(Sample) / (Samples - 1);
		Hips[Sample] = FMath::Lerp(HipFrom, HipTo, T);
		Normals[Sample] = FMath::Lerp(Swing.FromNormal, Swing.ToNormal, T).GetSafeNormal();
		Ankles[Sample] = AnkleFor(Index, SwingPadAt(Swing, T, Swing.SwingDetour, Swing.SwingDetourPeak), Normals[Sample],
			Hips[Sample], FQuat::Slerp(RotationFrom, RotationTo, T));
	}
	// 처음 자리 무릎 원에서 몸 기준 극 쪽(0번)부터 15도마다 24방향. 같은 월드 방향을 자리마다 그 자리 무릎 원에 내린다.
	const IGLining::FKneeCircle Start = IGLining::KneeCircle(Hips[0], Ankles[0], Length1[Index], Length2[Index], Swing.LastPole);
	FVector Directions[Count];
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		Directions[Slot] = FQuat(Start.Axis, TWO_PI * Slot / Count).RotateVector(Start.Bend);
	}
	int32 Rooms[Samples][Count];
	const auto Measure = [&](const int32 Sample, const int32 Slot)
	{
		const IGLining::FKneeCircle Circle =
			IGLining::KneeCircle(Hips[Sample], Ankles[Sample], Length1[Index], Length2[Index], Directions[Slot]);
		Rooms[Sample][Slot] = LegRoom(Hips[Sample], Circle.At(0.0f), Ankles[Sample], Normals[Sample]);
	};
	const auto Value = [](const int32 Room) { return Room == 2 ? 2.0f : Room == 1 ? 1.0f : -10.0f; };
	// 몸 기준 극 쪽부터 잰다. 내내 굵기만큼 비어 있으면(넓은 데서 걸을 때 대부분) 나머지는 재지 않는다.
	Out.bDefaultClear = true;
	Out.DefaultScore = 0.0f;
	Out.Blocked = 0;
	Out.DefaultBlocked = 0;
	for (int32 Sample = 0; Sample < Samples; ++Sample)
	{
		Measure(Sample, 0);
		Out.bDefaultClear &= Rooms[Sample][0] == 2;
		Out.DefaultScore += Value(Rooms[Sample][0]);
		Out.DefaultBlocked += Rooms[Sample][0] == 0 ? 1 : 0;
	}
	if (Out.bDefaultClear)
	{
		Out.Score = Out.DefaultScore;
		for (FVector& Pole : Out.Poles)
		{
			Pole = Directions[0];
		}
		return;
	}
	for (int32 Sample = 0; Sample < Samples; ++Sample)
	{
		for (int32 Slot = 1; Slot < Count; ++Slot)
		{
			Measure(Sample, Slot);
		}
	}
	const auto Steps = [](const int32 A, const int32 B)
	{
		const int32 Gap = FMath::Abs(A - B) % Count;
		return FMath::Min(Gap, Count - Gap);
	};
	// 지금 무릎이 있는 방향.
	int32 StartSlot = 0;
	FVector Current = Swing.KneeBend - Start.Axis * FVector::DotProduct(Swing.KneeBend, Start.Axis);
	if (Current.Normalize())
	{
		const float Angle = FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(Start.Bend, Current), Start.Axis),
			FVector::DotProduct(Start.Bend, Current));
		StartSlot = (FMath::RoundToInt(Angle / (TWO_PI / Count)) % Count + Count) % Count;
	}
	// 빈 정도의 합이 가장 큰 길. 막힌 방향은 크게 깎고, 많이 돌수록·몸 기준 극에서 멀수록 조금 깎는다. 한 자리에서 다음
	// 자리로 도는 사이의 방향은 두 자리 가운데 한 번이라도 비어 있어야 지난다.
	constexpr float Unreachable = -1.0e9f;
	float Score[Samples][Count];
	int32 From[Samples][Count];
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		Score[0][Slot] = Steps(StartSlot, Slot) <= MaxSlots ? Value(Rooms[0][Slot]) - 0.1f * Steps(StartSlot, Slot) : Unreachable;
		From[0][Slot] = INDEX_NONE;
	}
	for (int32 Sample = 1; Sample < Samples; ++Sample)
	{
		for (int32 Slot = 0; Slot < Count; ++Slot)
		{
			float Best = Unreachable;
			int32 BestFrom = INDEX_NONE;
			for (int32 Turn = -MaxSlots; Turn <= MaxSlots; ++Turn)
			{
				const int32 Prior = (Slot - Turn + Count) % Count;
				if (Score[Sample - 1][Prior] <= Unreachable)
				{
					continue;
				}
				bool bPassable = true;
				for (int32 Between = 1; Between < FMath::Abs(Turn) && bPassable; ++Between)
				{
					const int32 Passed = (Prior + (Turn > 0 ? Between : -Between) + Count) % Count;
					bPassable = FMath::Max(Rooms[Sample - 1][Passed], Rooms[Sample][Passed]) > 0;
				}
				const float Candidate = Score[Sample - 1][Prior] - 0.1f * FMath::Abs(Turn);
				if (bPassable && Candidate > Best)
				{
					Best = Candidate;
					BestFrom = Prior;
				}
			}
			Score[Sample][Slot] = BestFrom == INDEX_NONE ? Unreachable : Best + Value(Rooms[Sample][Slot]) - 0.02f * Steps(0, Slot);
			From[Sample][Slot] = BestFrom;
		}
	}
	int32 EndSlot = 0;
	for (int32 Slot = 1; Slot < Count; ++Slot)
	{
		if (Score[Samples - 1][Slot] > Score[Samples - 1][EndSlot])
		{
			EndSlot = Slot;
		}
	}
	Out.Score = Score[Samples - 1][EndSlot];
	for (int32 Sample = Samples - 1; Sample >= 0; --Sample)
	{
		Out.Poles[Sample] = Directions[EndSlot];
		Out.Blocked += Rooms[Sample][EndSlot] == 0 ? 1 : 0;
		EndSlot = From[Sample][EndSlot] == INDEX_NONE ? EndSlot : From[Sample][EndSlot];
	}
}

bool AIGLiningEntity::IsBodyObstructed(FString* OutComponent) const
{
	UWorld* World = GetWorld();
	if (!World || !BodyBlocker || State == EIGLiningState::Hidden)
	{
		return false;
	}
	// 벽에 붙는 바닥면과 가장자리 3 cm는 눌려 휘는 막이라 뺀다.
	const FVector Extent = BodyBlocker->GetScaledBoxExtent() - FVector(3.0f);
	if (Extent.GetMin() <= 0.0f)
	{
		return false;
	}
	const FCollisionShape Shape = FCollisionShape::MakeBox(Extent);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningBodyCheck), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	for (const FIGLiningGap& Gap : Gaps)
	{
		if (AIGAccessHatch* Hatch = Gap.Hatch.Get())
		{
			Params.AddIgnoredActor(Hatch);
		}
	}
	const FVector Center = BodyBlocker->GetComponentLocation();
	const FQuat Rotation = BodyBlocker->GetComponentQuat();
	TArray<FOverlapResult> Overlaps;
	if (World->OverlapMultiByObjectType(Overlaps, Center, Rotation, FCollisionObjectQueryParams(ECC_WorldStatic), Shape, Params)
		|| !Overlaps.IsEmpty())
	{
		if (OutComponent && !Overlaps.IsEmpty())
		{
			const UPrimitiveComponent* Component = Overlaps[0].GetComponent();
			*OutComponent = FString::Printf(TEXT("%s.%s"), Overlaps[0].GetActor() ? *Overlaps[0].GetActor()->GetName() : TEXT("none"),
				Component ? *Component->GetName() : TEXT("none"));
		}
		return true;
	}
	const UStaticMeshComponent* Door = ActiveHatchDoor();
	if (Door && Door->OverlapComponent(Center, Rotation, Shape))
	{
		if (OutComponent)
		{
			*OutComponent = Door->GetOwner() ? Door->GetOwner()->GetName() + TEXT(".DoorPanel") : TEXT("DoorPanel");
		}
		return true;
	}
	return false;
}

bool AIGLiningEntity::FindLegObstructionPart(const int32 Leg, int32& OutSegment, FString& OutComponent, FVector* OutImpact,
	FVector* OutNormal, float* OutInside) const
{
	UWorld* World = GetWorld();
	if (!World || Leg < 0 || Leg >= LegCount)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningLegCheck), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	const FLeg& L = Legs[Leg];
	// 끝 마디는 보이는 패드 덩어리(PadCenter)까지 잰다. 옮기는 중인 다리가 디딜 자리에 아직 못 닿으면 Pad(디딜 자리)는
	// 발목에서 멀리 있고, 그 사이를 그은 선은 화면에 없는 다리였다.
	const FVector Points[4] = {L.Hip, L.Knee, L.Ankle, L.PadCenter};
	const float EndSlack[3] = {0.0f, 0.0f, 6.0f};
	for (int32 Segment = 0; Segment < 3; ++Segment)
	{
		const FVector Delta = Points[Segment + 1] - Points[Segment];
		const float Length = Delta.Size();
		FHitResult Hit;
		if (Length <= EndSlack[Segment] + 1.0f)
		{
			continue;
		}
		const FVector End = Points[Segment + 1] - Delta / Length * EndSlack[Segment];
		UStaticMeshComponent* Door = ActiveHatchDoor();
		if (World->LineTraceSingleByObjectType(Hit, Points[Segment], End, Objects, Params)
			|| (Door && Door->LineTraceComponent(Hit, Points[Segment], End, FCollisionQueryParams(SCENE_QUERY_STAT(LiningLegDoor), false))))
		{
			OutSegment = Segment;
			OutComponent = Hit.GetActor() ? FString::Printf(TEXT("%s.%s"), *Hit.GetActor()->GetName(),
				Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("none")) : TEXT("none");
			if (OutImpact)
			{
				*OutImpact = Hit.ImpactPoint;
			}
			if (OutNormal)
			{
				*OutNormal = Hit.ImpactNormal;
			}
			if (OutInside)
			{
				// 마디 끝에서 거꾸로 쏴서 나오는 자리를 찾는다. 못 찾으면 끝이 물체 속이다.
				FHitResult Exit;
				const bool bExit = Hit.GetComponent()
					&& Hit.GetComponent()->LineTraceComponent(Exit, End, Hit.ImpactPoint, FCollisionQueryParams(SCENE_QUERY_STAT(LiningLegExit), false));
				*OutInside = FVector::Distance(Hit.ImpactPoint, bExit && !Exit.bStartPenetrating ? Exit.ImpactPoint : End);
			}
			return true;
		}
	}
	return false;
}

FString AIGLiningEntity::FindLegObstruction(const int32 Leg) const
{
	UWorld* World = GetWorld();
	if (!World || Leg < 0 || Leg >= LegCount)
	{
		return FString();
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiningLegCheck), false, this);
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(PlayerPawn);
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	const auto Blocked = [World, &Params, &Objects](const FVector& From, const FVector& To, const float EndSlack) -> FString
	{
		const FVector Delta = To - From;
		const float Length = Delta.Size();
		if (Length <= EndSlack + 1.0f)
		{
			return FString();
		}
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, From, To - Delta / Length * EndSlack, Objects, Params))
		{
			return FString::Printf(TEXT("%s@%s"), Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("none"),
				*Hit.ImpactPoint.ToCompactString());
		}
		return FString();
	};
	// 풀어 둔 마디 셋(엉덩이→무릎→발목→패드)을 그대로 따라간다.
	const FLeg& L = Legs[Leg];
	FString Found = Blocked(L.Hip, L.Knee, 0.0f);
	if (Found.IsEmpty())
	{
		Found = Blocked(L.Knee, L.Ankle, 0.0f);
	}
	if (Found.IsEmpty())
	{
		Found = Blocked(L.Ankle, L.Pad, 6.0f);
	}
	return Found;
}

void AIGLiningEntity::LogPose(const TCHAR* Tag) const
{
	if (!Body)
	{
		return;
	}
	FString Line = FString::Printf(TEXT("LINING_POSE %s actor=%s comp=%s"), Tag,
		*GetActorLocation().ToCompactString(), *Body->GetComponentLocation().ToCompactString());
	for (int32 Index = 0; Index < Body->GetNumBones(); ++Index)
	{
		const FName Name = Body->GetBoneName(Index);
		const FTransform Local = Body->BoneSpaceTransforms.IsValidIndex(Index) ? Body->BoneSpaceTransforms[Index] : FTransform::Identity;
		Line += FString::Printf(TEXT(" | %s w=%s l=%s nan=%d"), *Name.ToString(), *Body->GetBoneLocation(Name).ToCompactString(),
			*Local.GetLocation().ToCompactString(), Local.ContainsNaN() ? 1 : 0);
	}
	UE_LOG(LogIndieGame, Display, TEXT("%s"), *Line);
}

void AIGLiningEntity::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const TPair<FName, TObjectPtr<UAudioComponent>>& Draft : GapDrafts)
	{
		if (UAudioComponent* Audio = Draft.Value.Get())
		{
			Audio->Stop();
		}
	}
	GapDrafts.Reset();
	SetObstructions(HeldGap, false);
	SetObstructions(TargetGap, false);
	// 검사 실행마다 짚은 발이 끌려간 거리와 걷는 동안 버틴 발 수를 남긴다(관리실·302호·5층 어디서든).
	if (bHasMoved)
	{
		UE_LOG(LogIndieGame, Display, TEXT("LINING_CONTACT max_slip=%.1f min_hold=%d"), MaxPadSlip, MinPadsHoldingWhileWalking);
	}
	if (bMeasureLegs && MeasuredFrames > 0)
	{
		UE_LOG(LogIndieGame, Display,
			TEXT("LINING_LEGS frames=%d knee_max=%.1f,%.1f,%.1f,%.1f knee_over15=%d,%d,%d,%d obstructed=%d,%d,%d,%d worst=%s"),
			MeasuredFrames, MaxKneeStep[0], MaxKneeStep[1], MaxKneeStep[2], MaxKneeStep[3],
			KneeJumpFrames[0], KneeJumpFrames[1], KneeJumpFrames[2], KneeJumpFrames[3],
			ObstructedFrames[0], ObstructedFrames[1], ObstructedFrames[2], ObstructedFrames[3], *WorstKneeStep);
		FString ByState;
		for (const TPair<int32, int32>& Entry : BodyObstructionByState)
		{
			ByState += FString::Printf(TEXT(" state%d=%d"), Entry.Key, Entry.Value);
		}
		UE_LOG(LogIndieGame, Display, TEXT("LINING_BODY frames=%d obstructed=%d%s"), MeasuredFrames, BodyObstructedFrames, *ByState);
		UE_LOG(LogIndieGame, Display, TEXT("LINING_LEGS_THICK frames=%d radius=%.0f obstructed=%d,%d,%d,%d"), MeasuredFrames,
			IGLining::ThickMeasureRadius, ThickObstructedFrames[0], ThickObstructedFrames[1], ThickObstructedFrames[2], ThickObstructedFrames[3]);
		UE_LOG(LogIndieGame, Display,
			TEXT("LINING_SOLVE_TIME frames=%d avg_ms=%.3f max_ms=%.3f plans=%d plan_avg_ms=%.3f plan_max_ms=%.3f brace_choose_max_ms=%.3f"),
			MeasuredFrames, MeasuredFrames > 0 ? SolveTotalSeconds * 1000.0 / MeasuredFrames : 0.0, SolveMaxSeconds * 1000.0, PlanCount,
			PlanCount > 0 ? PlanTotalSeconds * 1000.0 / PlanCount : 0.0, PlanMaxSeconds * 1000.0, BraceChooseMaxSeconds * 1000.0);
		ObstructionBreakdown.ValueSort([](const int32 A, const int32 B) { return A > B; });
		int32 Shown = 0;
		for (const TPair<FString, int32>& Entry : ObstructionBreakdown)
		{
			const float* Deepest = ObstructionDepth.Find(Entry.Key);
			UE_LOG(LogIndieGame, Display, TEXT("LINING_OBSTRUCTION %s frames=%d max_in=%.1f"), *Entry.Key, Entry.Value, Deepest ? *Deepest : 0.0f);
			if (++Shown >= 40)
			{
				break;
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}
