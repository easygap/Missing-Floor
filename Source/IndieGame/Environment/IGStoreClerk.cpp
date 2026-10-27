#include "Environment/IGStoreClerk.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "IndieGame.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"

namespace IGStoreClerk
{
	// 계산대 집기 자리는 IGPrologueWorldScene이 깐다(계산대 2590,-200 / 포스 원점 2608,-190,99). 아래는 그 포스
	// 메시(build_retail_refresh.py)의 직원 화면 가운데, 감열 프린터 출구, 출구 앞 손님 쪽 상판이다.
	const FVector PosScreen(2608.0f, -181.0f, 133.0f);
	/** 직원 화면 면이 향하는 쪽(나린 쪽 +Y, 뒤로 10도 누움)과 화면을 따라 위로 가는 쪽. 오른쪽은 +X. */
	const FVector ScreenNormal(0.0f, 0.985f, 0.174f);
	const FVector ScreenUp(0.0f, -0.174f, 0.985f);
	const FVector PrinterExit(2581.5f, -201.2f, 109.2f);
	const FVector ReceiptRest(2580.0f, -218.0f, 99.16f);
	/** 놓인 영수증은 긴 쪽이 손님을 향한다. 계산대 위 영수증(8×19 cm)과 같은 크기다. */
	constexpr float ReceiptRestYaw = 4.0f;
	constexpr float StripLength = 19.0f;
	constexpr float StripWidth = 8.0f;
	/** 출구에서 나온 종이가 처지는 각. */
	constexpr float StripSagDegrees = 25.0f;
	/**
	 * 손끝이 종이 앞 가장자리에서 이만큼 안쪽을 집는다. 출구에 너무 붙여 집으면 손등이 프린터 앞 위
	 * 모서리에 걸린다. 손끝 아래 종이까지의 높이.
	 */
	constexpr float PinchInset = 4.0f;
	constexpr float PaperUnderTip = 0.4f;
	/** 일을 시작하고 몸을 돌려 숙이기까지, 끝내고 다시 손님 쪽으로 서기까지. */
	constexpr float EaseInSeconds = 0.35f;
	constexpr float RelaxSeconds = 0.45f;
	/** 이 안에 손님이 있으면 몸짓을 매 프레임 그린다. 밖이면 틱을 끈다. */
	constexpr float AwakeRadius = 1500.0f;
	/**
	 * 일하는 동안 계산대 쪽(-Y)으로 다가서는 거리. 원래 자리는 계산대 뒷면(Y -168)에서 40 cm 뒤라 손이
	 * 프린터와 손님 앞 상판에 닿지 않는다. 다가서도 배는 계산대 뒷면에 닿지 않는다. 발은 계산대에 가려 안 보인다.
	 */
	constexpr float WorkStep = 22.0f;
	/** 계산대 뒷모서리와 상판 윗면(월드). 쉬는 손이 짚는 자리다. */
	constexpr float CounterBackY = -168.0f;
	constexpr float CounterTopZ = 99.0f;
	/** 화면을 누르기 전 손끝이 화면 앞에 머무는 거리(cm). */
	constexpr float HoverGap = 4.0f;
	/** 팔이 모자랄 때 어깨를 그쪽으로 내미는 한도, 손목을 꺾는 한도, 아래팔을 비트는 한도. */
	constexpr float MaxShoulderReach = 6.0f;
	constexpr float MaxWristBend = 70.0f;
	constexpr float MaxForearmRoll = 110.0f;

	/** 기록을 찾는 동안 단말기를 누르는 때와 누르는 화면 자리(가운데에서 오른쪽, 위로 cm). 택배 메뉴 → 보관 조회 → 날짜 둘 → 조회. */
	constexpr float SearchTaps[] = {0.42f, 0.78f, 1.10f, 1.58f, 1.92f};
	const FVector2D SearchKeys[] = {{-10.0f, 6.0f}, {-4.0f, 1.5f}, {3.5f, 3.0f}, {6.5f, -2.5f}, {11.5f, -7.0f}};
	constexpr float RecordSearchSeconds = 2.70f;
	/** 출력을 누르고, 프린터가 돌고(녹음 1.40초에 커터), 뜯고, 놓는다. */
	constexpr float PrintTap = 0.32f;
	const FVector2D PrintKey(11.5f, -7.0f);
	constexpr float PrintStart = 0.70f;
	constexpr float PrintFeedSeconds = 1.38f;
	/** 왼손이 프린터로 가기 시작해 출구 위에 닿고, 종이가 다 나오면 집는다. */
	constexpr float PaperReach = 0.40f;
	constexpr float PaperReady = 0.95f;
	constexpr float PinchStart = 1.95f;
	constexpr float PinchAt = 2.14f;
	constexpr float PrintPull = 2.35f;
	/** 놓을 자리 위에 닿고, 내려놓고, 손을 뗀다. */
	constexpr float CarryArrive = 2.68f;
	constexpr float PrintPlace = 2.85f;
	constexpr float PrintSeconds = 3.30f;

	float Smooth(const float X)
	{
		const float T = FMath::Clamp(X, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	/** 누를 때마다 손끝이 화면에 닿았다 떨어진다. 닿는 순간(삑)부터 0.06초 머문다. */
	float TapDip(const float Seconds, const float TapAt)
	{
		const float X = Seconds - TapAt;
		if (X < -0.07f || X > 0.18f)
		{
			return 0.0f;
		}
		if (X < 0.0f)
		{
			return Smooth((X + 0.07f) / 0.07f);
		}
		return X < 0.06f ? 1.0f : 1.0f - Smooth((X - 0.06f) / 0.12f);
	}

	float SearchPress(const float T)
	{
		float Press = 0.0f;
		for (const float Tap : SearchTaps)
		{
			Press = FMath::Max(Press, TapDip(T, Tap));
		}
		return Press;
	}

	/** 지금 누르거나 누르러 가는 화면 자리. 누르고 0.16초 뒤 떠나 다음 탭 0.06초 전에 닿는다. */
	FVector2D SearchKeyAt(const float T)
	{
		const int32 Count = UE_ARRAY_COUNT(SearchTaps);
		for (int32 Index = 0; Index + 1 < Count; ++Index)
		{
			const float Leave = SearchTaps[Index] + 0.16f;
			const float Arrive = SearchTaps[Index + 1] - 0.06f;
			if (T < Arrive)
			{
				return FMath::Lerp(SearchKeys[Index], SearchKeys[Index + 1], Smooth((T - Leave) / (Arrive - Leave)));
			}
		}
		return SearchKeys[Count - 1];
	}

	/** 손가락이 닿는 화면 위 자리. 화면 유리에서 손끝 살 두께만큼 띄운다. */
	FVector ScreenPoint(const FVector2D& Key)
	{
		return PosScreen + FVector(Key.X, 0.0f, 0.0f) + ScreenUp * Key.Y + ScreenNormal * 0.4f;
	}

	/**
	 * 계산대 위 집기의 덩어리(월드). 포스 메시(build_retail_refresh.py)의 프린터·직원 모니터·받침·기둥·
	 * 금전함과 계산대 몸통이다. 모니터만 10도 누운 상자이고 앞면이 화면 유리 2 mm 뒤다. 일하는 동안 팔이
	 * 이 안으로 들어가면 검사 로그에 남긴다.
	 */
	struct FFixtureBox
	{
		const TCHAR* Name;
		FVector Center;
		FVector Extent;
		FVector AxisY = FVector::YAxisVector;
		FVector AxisZ = FVector::ZAxisVector;
	};
	const FFixtureBox Fixtures[] = {
		{TEXT("printer"), FVector(2581.5f, -192.0f, 105.5f), FVector(6.5f, 8.75f, 6.5f)},
		{TEXT("monitor"), PosScreen - ScreenNormal * 2.0f, FVector(18.5f, 1.8f, 12.25f), ScreenNormal, ScreenUp},
		{TEXT("monitor_base"), FVector(2608.0f, -186.5f, 109.75f), FVector(10.0f, 9.5f, 1.25f)},
		{TEXT("pole"), FVector(2608.0f, -182.5f, 119.5f), FVector(2.5f, 3.0f, 9.0f)},
		{TEXT("drawer"), FVector(2608.0f, -190.0f, 103.75f), FVector(19.0f, 19.0f, 4.75f)},
		{TEXT("counter"), FVector(2590.0f, -200.0f, 52.5f), FVector(112.0f, 32.0f, 46.5f)},
	};
	/** 금전함 윗면. 그 위로 떨어지는 쉬는 손은 계산대 상판이 아니라 여기를 짚는다. */
	constexpr float DrawerTopZ = 108.5f;

	/** 반지름 Radius인 팔 토막(A-B)이 상자 안으로 들어간 깊이(cm). 닿지 않으면 0 이하다. */
	float Penetration(const FVector& A, const FVector& B, const float Radius, const FFixtureBox& Box)
	{
		const FVector AxisX = FVector::CrossProduct(Box.AxisY, Box.AxisZ);
		float Deepest = -Radius;
		for (int32 Step = 0; Step <= 8; ++Step)
		{
			const FVector Offset = FMath::Lerp(A, B, Step / 8.0f) - Box.Center;
			const FVector Local(FVector::DotProduct(Offset, AxisX), FVector::DotProduct(Offset, Box.AxisY), FVector::DotProduct(Offset, Box.AxisZ));
			const FVector Outside(FMath::Max(FMath::Abs(Local.X) - Box.Extent.X, 0.0f),
				FMath::Max(FMath::Abs(Local.Y) - Box.Extent.Y, 0.0f), FMath::Max(FMath::Abs(Local.Z) - Box.Extent.Z, 0.0f));
			Deepest = FMath::Max(Deepest, Radius - Outside.Size());
		}
		return Deepest;
	}

	/** 출구에서 손님 쪽으로 처지며 나오는 종이의 방향. */
	FVector StripDirection()
	{
		const float Sag = FMath::DegreesToRadians(StripSagDegrees);
		return FVector(0.0f, -FMath::Cos(Sag), -FMath::Sin(Sag));
	}

	/** 놓인 영수증에서 손님 쪽 끝으로 가는 방향. */
	FVector RestFarDirection()
	{
		const float Yaw = FMath::DegreesToRadians(ReceiptRestYaw);
		return FVector(FMath::Sin(Yaw), -FMath::Cos(Yaw), 0.0f);
	}

	/** 영수증을 내려놓을 때 손끝 자리. 종이 앞 가장자리에서 PinchInset 안쪽을 집고 있다. */
	FVector PlaceTip()
	{
		return ReceiptRest - RestFarDirection() * (StripLength * 0.5f - PinchInset) + FVector(0.0f, 0.0f, PaperUnderTip);
	}

	/** 손끝이 화면을 향하고 손바닥이 아래를 본다. 팔꿈치는 바깥 아래로. */
	AIGStoreClerk::FHandGoal ScreenGoal(const FVector2D& Key, const float Press, const FVector& Outward)
	{
		AIGStoreClerk::FHandGoal Goal;
		Goal.Tip = ScreenPoint(Key) + ScreenNormal * (HoverGap * (1.0f - Press));
		Goal.Pointing = (-ScreenNormal + FVector(0.0f, 0.0f, 0.12f)).GetSafeNormal();
		Goal.Palm = FVector(0.0f, -0.25f, -1.0f).GetSafeNormal();
		Goal.Pole = (Outward * 0.55f + FVector(0.0f, 0.25f, -0.8f)).GetSafeNormal();
		return Goal;
	}

	AIGStoreClerk::FHandGoal BlendGoal(const AIGStoreClerk::FHandGoal& A, const AIGStoreClerk::FHandGoal& B, const float Alpha);

	/** 손을 옮기는 길 가운데를 Lift cm 들어 올린다. 계산대 모서리나 프린터를 넘어가는 손이 쓴다. */
	AIGStoreClerk::FHandGoal ArcGoal(const AIGStoreClerk::FHandGoal& A, const AIGStoreClerk::FHandGoal& B, const float Alpha, const float Lift)
	{
		AIGStoreClerk::FHandGoal Goal = BlendGoal(A, B, Alpha);
		const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
		Goal.Tip.Z += Lift * 4.0f * T * (1.0f - T);
		return Goal;
	}

	AIGStoreClerk::FHandGoal BlendGoal(const AIGStoreClerk::FHandGoal& A, const AIGStoreClerk::FHandGoal& B, const float Alpha)
	{
		if (Alpha <= 0.0f)
		{
			return A;
		}
		if (Alpha >= 1.0f)
		{
			return B;
		}
		AIGStoreClerk::FHandGoal Goal;
		Goal.Tip = FMath::Lerp(A.Tip, B.Tip, Alpha);
		Goal.Pointing = FMath::Lerp(A.Pointing, B.Pointing, Alpha).GetSafeNormal(UE_SMALL_NUMBER, B.Pointing);
		Goal.Palm = FMath::Lerp(A.Palm, B.Palm, Alpha).GetSafeNormal(UE_SMALL_NUMBER, B.Palm);
		Goal.Pole = FMath::Lerp(A.Pole, B.Pole, Alpha).GetSafeNormal(UE_SMALL_NUMBER, B.Pole);
		return Goal;
	}

	/** 몸통을 숙이는 각, 계산대로 다가선 정도(0~1), 바라볼 자리(없으면 손님). */
	struct FPose
	{
		float TorsoLean = 0.0f;
		float Step = 0.0f;
		TOptional<FVector> LookAt;
	};

	FPose TaskPose(const EIGClerkTask Task, const float T)
	{
		FPose Pose;
		if (Task == EIGClerkTask::SearchRecord)
		{
			// 화면 앞으로 한 뼘 다가서서 누르는 자리를 본다. 마지막을 누르고 나면 결과를 읽는다.
			Pose.TorsoLean = 7.0f + 1.2f * SearchPress(T);
			Pose.Step = 0.35f;
			Pose.LookAt = ScreenPoint(SearchKeyAt(T));
			return Pose;
		}
		if (Task != EIGClerkTask::PrintReceipt)
		{
			return Pose;
		}
		if (T < 0.45f)
		{
			Pose.TorsoLean = 6.0f + 1.2f * TapDip(T, PrintTap);
			Pose.LookAt = ScreenPoint(PrintKey);
		}
		else if (T < PrintPull + 0.05f)
		{
			// 계산대로 다가서서 종이가 나오는 출구를 본다. 뜯는 순간 몸이 짧게 들린다.
			Pose.TorsoLean = 9.0f - 2.5f * TapDip(T, PrintPull);
			Pose.Step = Smooth((T - 0.36f) / 0.54f);
			Pose.LookAt = PrinterExit + StripDirection() * 6.0f;
		}
		else if (T < PrintPlace + 0.10f)
		{
			// 손님 앞 상판까지 팔을 뻗어 내려놓는다.
			Pose.TorsoLean = FMath::Lerp(9.0f, 16.0f, Smooth((T - PrintPull) / (PrintPlace - PrintPull)));
			Pose.Step = 1.0f;
			Pose.LookAt = ReceiptRest;
		}
		else
		{
			// 손을 거두며 몸을 세우고 손님을 본다.
			const float Back = Smooth((T - PrintPlace - 0.10f) / (PrintSeconds - PrintPlace - 0.10f));
			Pose.TorsoLean = FMath::Lerp(16.0f, 8.0f, Back);
			Pose.Step = 1.0f - Back;
		}
		return Pose;
	}
}

AIGStoreClerk::AIGStoreClerk()
{
	// 손님이 가게 안에 있거나 일하는 동안에만 켠다(UpdateTickState). 몸짓과 소리를 같은 프레임에 맞춘다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = 1.0f / 30.0f;
	Body = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Narin"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ReceiptStrip = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ReceiptStrip"));
	ReceiptStrip->SetupAttachment(Body);
	ReceiptStrip->SetUsingAbsoluteLocation(true);
	ReceiptStrip->SetUsingAbsoluteRotation(true);
	ReceiptStrip->SetUsingAbsoluteScale(true);
	ReceiptStrip->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ReceiptStrip->SetCastShadow(false);
	ReceiptStrip->SetHiddenInGame(true);
}

void AIGStoreClerk::BeginPlay()
{
	Super::BeginPlay();
	if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Meshes/SK_NarinClerk.SK_NarinClerk")))
	{
		Body->SetSkinnedAssetAndUpdate(Mesh);
		// 기준 자세. rig_store_clerk.py의 Torso는 엉덩이(85 cm)에서, Head는 목(135 cm)에서 꺾인다.
		bHasRig = Body->GetBoneIndex(TEXT("Torso")) != INDEX_NONE && Body->GetBoneIndex(TEXT("Head")) != INDEX_NONE;
		if (bHasRig)
		{
			RefTorso = Body->GetBoneTransformByName(TEXT("Torso"), EBoneSpaces::ComponentSpace);
			RefHead = Body->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::ComponentSpace);
			// 팔 셋(위팔·아래팔·손)과 손끝 뼈가 좌우로 있으면 손을 쓴다. 어느 쪽이 컴포넌트 공간의 +X인지는 뼈 자리로 읽는다.
			bHasArms = true;
			const TCHAR* Sides[2] = {TEXT("R"), TEXT("L")};
			for (int32 Index = 0; Index < 2; ++Index)
			{
				FClerkArm& Arm = Arms[Index];
				Arm.Upper = FName(*FString::Printf(TEXT("UpperArm_%s"), Sides[Index]));
				Arm.Lower = FName(*FString::Printf(TEXT("LowerArm_%s"), Sides[Index]));
				Arm.Hand = FName(*FString::Printf(TEXT("Hand_%s"), Sides[Index]));
				const FName Tip(*FString::Printf(TEXT("Fingertip_%s"), Sides[Index]));
				if (Body->GetBoneIndex(Arm.Upper) == INDEX_NONE || Body->GetBoneIndex(Arm.Lower) == INDEX_NONE
					|| Body->GetBoneIndex(Arm.Hand) == INDEX_NONE || Body->GetBoneIndex(Tip) == INDEX_NONE)
				{
					bHasArms = false;
					break;
				}
				Arm.RefUpper = Body->GetBoneTransformByName(Arm.Upper, EBoneSpaces::ComponentSpace);
				Arm.RefLower = Body->GetBoneTransformByName(Arm.Lower, EBoneSpaces::ComponentSpace);
				Arm.RefHand = Body->GetBoneTransformByName(Arm.Hand, EBoneSpaces::ComponentSpace);
				Arm.RefTip = Body->GetBoneTransformByName(Tip, EBoneSpaces::ComponentSpace).GetLocation();
				Arm.UpperLength = FVector::Dist(Arm.RefUpper.GetLocation(), Arm.RefLower.GetLocation());
				Arm.LowerLength = FVector::Dist(Arm.RefLower.GetLocation(), Arm.RefHand.GetLocation());
				Arm.HandLength = FVector::Dist(Arm.RefHand.GetLocation(), Arm.RefTip);
				Arm.Side = FMath::Sign(Arm.RefUpper.GetLocation().X);
				// 벌린 팔의 손바닥은 허벅지 쪽(몸 안쪽)을 본다. 손 방향에 수직인 성분만 남긴다.
				const FVector HandDir = (Arm.RefTip - Arm.RefHand.GetLocation()).GetSafeNormal();
				const FVector Inward(-Arm.Side, 0.0f, 0.0f);
				Arm.RefPalm = (Inward - HandDir * FVector::DotProduct(Inward, HandDir)).GetSafeNormal();
			}
			if (bHasArms)
			{
				// 포스 화면에 가까운 어깨 쪽 손이 단말기를 누른다.
				const FTransform& Component = Body->GetComponentTransform();
				const float ToScreen0 = FVector::DistSquared(Component.TransformPosition(Arms[0].RefUpper.GetLocation()), IGStoreClerk::PosScreen);
				const float ToScreen1 = FVector::DistSquared(Component.TransformPosition(Arms[1].RefUpper.GetLocation()), IGStoreClerk::PosScreen);
				WorkingHand = ToScreen0 <= ToScreen1 ? 0 : 1;
			}
			UE_LOG(LogIndieGame, Display, TEXT("STORE_CLERK_RIG arms=%d screen_hand=%s shoulderR=%s tipR=%s upper=%.1f lower=%.1f hand=%.1f"),
				bHasArms ? 1 : 0, WorkingHand == 0 ? TEXT("R") : TEXT("L"),
				*Arms[0].RefUpper.GetLocation().ToCompactString(), *Arms[0].RefTip.ToCompactString(),
				Arms[0].UpperLength, Arms[0].LowerLength, Arms[0].HandLength);
		}
		else
		{
			UE_LOG(LogIndieGame, Warning, TEXT("STORE_CLERK rig bones missing; Narin keeps the rest pose"));
		}
	}
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		ReceiptStrip->SetStaticMesh(Cube);
		ReceiptStrip->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Prototype/Materials/M_PaperClean.M_PaperClean")));
	}
	// 단말기·프린터 소리는 처음 낼 때 앞부분을 읽느라 0.15초쯤 늦었다(믹서 녹음으로 잼). 몸짓과 같은
	// 순간에 나도록 미리 읽어 둔다.
	for (const TCHAR* Name : {TEXT("Store_PosBeep"), TEXT("Store_ReceiptPrint"), TEXT("Store_ReceiptPull")})
	{
		if (USoundBase* Sound = IGAudio::Sample(Name))
		{
			UGameplayStatics::PrimeSound(Sound);
		}
	}
	CurrentYaw = GetActorRotation().Yaw;
	HomeLocation = GetActorLocation();
	GetWorldTimerManager().SetTimer(FacingTimer, this, &AIGStoreClerk::UpdateTickState, 0.1f, true);
}

void AIGStoreClerk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(FacingTimer);
	CancelTask();
	Super::EndPlay(EndPlayReason);
}

float AIGStoreClerk::GetTaskSeconds() const
{
	const UWorld* World = GetWorld();
	return IsBusy() && World ? static_cast<float>(World->GetTimeSeconds() - TaskStartedAt) : -1.0f;
}

FTransform AIGStoreClerk::GetReceiptRestTransform()
{
	return FTransform(FRotator(0.0f, IGStoreClerk::ReceiptRestYaw, 0.0f), IGStoreClerk::ReceiptRest);
}

FVector AIGStoreClerk::GetPrinterExit()
{
	return IGStoreClerk::PrinterExit;
}

FVector AIGStoreClerk::GetPosScreen()
{
	return IGStoreClerk::PosScreen;
}

void AIGStoreClerk::UpdateTickState()
{
	const APawn* Customer = UGameplayStatics::GetPlayerPawn(this, 0);
	const bool bNear = Customer
		&& FVector::DistSquared(Customer->GetActorLocation(), GetActorLocation()) < FMath::Square(IGStoreClerk::AwakeRadius);
	const bool bRelaxing = RelaxStartedAt >= 0.0;
	const bool bWanted = !IsHidden() && (bNear || IsBusy() || bRelaxing);
	if (IsActorTickEnabled() != bWanted)
	{
		SetActorTickEnabled(bWanted);
	}
}

float AIGStoreClerk::StartTask(const EIGClerkTask Task, TFunction<void(EIGClerkCue)> OnCue)
{
	CancelTask();
	const UWorld* World = GetWorld();
	if (!World || Task == EIGClerkTask::None)
	{
		return 0.0f;
	}
	CurrentTask = Task;
	TaskCue = MoveTemp(OnCue);
	TaskStartedAt = World->GetTimeSeconds();
	RelaxStartedAt = -1.0;
	FiredEvents = 0;
	SetActorTickEnabled(true);
	UE_LOG(LogIndieGame, Log, TEXT("STORE_CLERK t=%.3f task=%s"), TaskStartedAt,
		Task == EIGClerkTask::SearchRecord ? TEXT("search") : TEXT("print"));
	return Task == EIGClerkTask::SearchRecord ? IGStoreClerk::RecordSearchSeconds : IGStoreClerk::PrintSeconds;
}

void AIGStoreClerk::CancelTask()
{
	if (UAudioComponent* Sound = PrintSound.Get())
	{
		Sound->Stop();
	}
	PrintSound.Reset();
	if (ReceiptStrip)
	{
		ReceiptStrip->SetHiddenInGame(true);
	}
	if (CurrentTask != EIGClerkTask::None)
	{
		// 하던 자세에서 천천히 손님 쪽으로 돌아온다. 끊긴 일의 신호는 더 보내지 않는다.
		const UWorld* World = GetWorld();
		RelaxStartedAt = World ? World->GetTimeSeconds() : -1.0;
		RelaxFromWeight = 1.0f;
	}
	CurrentTask = EIGClerkTask::None;
	TaskCue = nullptr;
	TaskStartedAt = -1.0;
	FiredEvents = 0;
}

void AIGStoreClerk::FinishTask()
{
	TFunction<void(EIGClerkCue)> Cue = MoveTemp(TaskCue);
	TaskCue = nullptr;
	const UWorld* World = GetWorld();
	CurrentTask = EIGClerkTask::None;
	TaskStartedAt = -1.0;
	RelaxStartedAt = World ? World->GetTimeSeconds() : -1.0;
	RelaxFromWeight = 1.0f;
	if (Cue)
	{
		Cue(EIGClerkCue::Finished);
	}
}

void AIGStoreClerk::PlayBeep()
{
	UE_LOG(LogIndieGame, Log, TEXT("STORE_CLERK t=%.3f sound=beep"), GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	// 포스는 화면을 누를 때마다 압전 부저가 삑 한다. 직원 화면 뒤에서 난다.
	IGAudio::SpawnOneShotAt(
		this,
		IGAudio::SampleOr(TEXT("Store_PosBeep"), [this]() -> USoundBase*
		{
			return UIGToneSequenceSoundWave::CreateMenuTick(this, true);
		}),
		IGStoreClerk::PosScreen,
		0.38f,
		1.0f,
		60.0f,
		700.0f,
		EIGAudioBus::World);
}

void AIGStoreClerk::FireCuesUpTo(const float TaskSeconds)
{
	// 틱이 늦게 와도 소리와 신호는 빠짐없이 차례로 낸다.
	const auto Cue = [this](const EIGClerkCue Which)
	{
		if (TaskCue)
		{
			TaskCue(Which);
		}
	};
	if (CurrentTask == EIGClerkTask::SearchRecord)
	{
		const int32 TapCount = UE_ARRAY_COUNT(IGStoreClerk::SearchTaps);
		while (FiredEvents < TapCount && TaskSeconds >= IGStoreClerk::SearchTaps[FiredEvents])
		{
			PlayBeep();
			if (FiredEvents == 0)
			{
				Cue(EIGClerkCue::FirstTap);
			}
			++FiredEvents;
		}
		if (FiredEvents >= TapCount && TaskSeconds >= IGStoreClerk::RecordSearchSeconds)
		{
			FinishTask();
		}
		return;
	}
	if (CurrentTask != EIGClerkTask::PrintReceipt)
	{
		return;
	}
	if (FiredEvents == 0 && TaskSeconds >= IGStoreClerk::PrintTap)
	{
		PlayBeep();
		++FiredEvents;
	}
	if (FiredEvents == 1 && TaskSeconds >= IGStoreClerk::PrintStart)
	{
		// 감열 프린터. 글줄을 찍는 동안 모터가 끊어 돌고, 끝에서 빈 종이를 밀어 커터가 자른다.
		PrintSound = IGAudio::SpawnOneShotAt(
			this,
			IGAudio::SampleOr(TEXT("Store_ReceiptPrint"), [this]() -> USoundBase*
			{
				return UIGToneSequenceSoundWave::CreateJournalPageTurn(this);
			}),
			IGStoreClerk::PrinterExit,
			0.5f,
			1.0f,
			60.0f,
			700.0f,
			EIGAudioBus::World);
		++FiredEvents;
		UE_LOG(LogIndieGame, Log, TEXT("STORE_CLERK t=%.3f sound=print"), GetWorld()->GetTimeSeconds());
		Cue(EIGClerkCue::PrintStarted);
	}
	if (FiredEvents == 2 && TaskSeconds >= IGStoreClerk::PrintPull)
	{
		// 반쯤 잘린 종이를 손으로 뜯는다.
		IGAudio::SpawnOneShotAt(
			this,
			IGAudio::SampleOr(TEXT("Store_ReceiptPull"), [this]() -> USoundBase*
			{
				return UIGToneSequenceSoundWave::CreateJournalPageTurn(this);
			}),
			IGStoreClerk::PrinterExit,
			0.4f,
			1.0f,
			40.0f,
			600.0f,
			EIGAudioBus::World);
		// 뜯은 종이는 이제 손에 들려 간다(UpdateCarriedReceipt).
		++FiredEvents;
		UE_LOG(LogIndieGame, Log, TEXT("STORE_CLERK t=%.3f sound=pull"), GetWorld()->GetTimeSeconds());
	}
	if (FiredEvents == 3 && TaskSeconds >= IGStoreClerk::PrintPlace)
	{
		// 손님 앞 상판에 종이를 내려놓는 작은 소리.
		IGAudio::SpawnOneShotAt(
			this,
			IGAudio::Sample(TEXT("Paper_Turn_0")),
			IGStoreClerk::ReceiptRest,
			0.16f,
			1.25f,
			40.0f,
			500.0f,
			EIGAudioBus::World);
		// 손에 든 종이와 같은 자리·크기로 계산대 위 영수증이 선다(ReceiptPlaced를 받은 쪽이 세운다).
		ReceiptStrip->SetHiddenInGame(true);
		++FiredEvents;
		UE_LOG(LogIndieGame, Log, TEXT("STORE_CLERK t=%.3f sound=place"), GetWorld()->GetTimeSeconds());
		Cue(EIGClerkCue::ReceiptPlaced);
	}
	if (FiredEvents >= 4 && TaskSeconds >= IGStoreClerk::PrintSeconds)
	{
		FinishTask();
	}
}

void AIGStoreClerk::UpdateReceiptStrip(const float TaskSeconds)
{
	if (CurrentTask != EIGClerkTask::PrintReceipt || TaskSeconds < IGStoreClerk::PrintStart || FiredEvents >= 3)
	{
		return;
	}
	// 출구에서 손님 쪽으로 밀려 나오며 처진다. 소리가 나는 동안 길어지고 커터가 자르면 멈춘다. 다 나온
	// 끝은 상판 위 2 cm에 머문다.
	const float Length = FMath::Max(0.3f, IGStoreClerk::StripLength
		* FMath::Clamp((TaskSeconds - IGStoreClerk::PrintStart) / IGStoreClerk::PrintFeedSeconds, 0.0f, 1.0f));
	const FVector Direction = IGStoreClerk::StripDirection();
	ReceiptStrip->SetWorldLocationAndRotation(IGStoreClerk::PrinterExit + Direction * (Length * 0.5f),
		FRotationMatrix::MakeFromYZ(Direction, FVector::UpVector).ToQuat());
	ReceiptStrip->SetWorldScale3D(FVector(IGStoreClerk::StripWidth / 100.0f, Length / 100.0f, 0.0012f));
	ReceiptStrip->SetHiddenInGame(false);
}

void AIGStoreClerk::UpdateCarriedReceipt(const float TaskSeconds)
{
	if (CurrentTask != EIGClerkTask::PrintReceipt || FiredEvents != 3 || !bHasArms)
	{
		return;
	}
	// 손끝 바로 밑에 종이가 있고, 집은 자리에서 PinchInset 뒤가 종이 앞 가장자리다. 뜯을 때는 출구에서
	// 나오던 기울기 그대로이고, 놓을 자리로 가며 평평해진다. 먼 끝은 상판 아래로 내려가지 않는다.
	const FVector Hold = SolvedTip[1 - WorkingHand] - FVector(0.0f, 0.0f, IGStoreClerk::PaperUnderTip);
	const float Flatten = IGStoreClerk::Smooth(
		(TaskSeconds - IGStoreClerk::PrintPull) / (IGStoreClerk::PrintPlace - IGStoreClerk::PrintPull));
	FVector Far = FMath::Lerp(IGStoreClerk::StripDirection(), IGStoreClerk::RestFarDirection(), Flatten).GetSafeNormal();
	FVector Near = Hold - Far * IGStoreClerk::PinchInset;
	const float LowestZ = IGStoreClerk::CounterTopZ + 0.15f;
	if (Near.Z + Far.Z * IGStoreClerk::StripLength < LowestZ)
	{
		const float Drop = FMath::Clamp((LowestZ - Near.Z) / IGStoreClerk::StripLength, -0.9f, 0.9f);
		const FVector Flat = FVector(Far.X, Far.Y, 0.0f).GetSafeNormal();
		Far = (Flat * FMath::Sqrt(1.0f - Drop * Drop) + FVector(0.0f, 0.0f, Drop)).GetSafeNormal();
		Near = Hold - Far * IGStoreClerk::PinchInset;
	}
	FVector Center = Near + Far * (IGStoreClerk::StripLength * 0.5f);
	FQuat Rotation = FRotationMatrix::MakeFromYZ(Far, FVector::UpVector).ToQuat();
	// 내려놓기 직전에는 종이가 놓일 자리로 먼저 맞춘다. 손이 조금 덜 닿아도 놓이는 순간 영수증이 튀지 않는다.
	const float Settle = IGStoreClerk::Smooth((TaskSeconds - (IGStoreClerk::PrintPlace - 0.14f)) / 0.12f);
	if (Settle > 0.0f)
	{
		Center = FMath::Lerp(Center, GetReceiptRestTransform().GetLocation(), Settle);
		Rotation = FQuat::Slerp(Rotation,
			FRotationMatrix::MakeFromYZ(IGStoreClerk::RestFarDirection(), FVector::UpVector).ToQuat(), Settle);
	}
	ReceiptStrip->SetWorldLocationAndRotation(Center, Rotation);
	ReceiptStrip->SetWorldScale3D(FVector(IGStoreClerk::StripWidth / 100.0f, IGStoreClerk::StripLength / 100.0f, 0.0012f));
	ReceiptStrip->SetHiddenInGame(false);
}

void AIGStoreClerk::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (IsHidden())
	{
		// 밤에는 가게가 닫혀 나린이 없다. 하던 일은 거둔다.
		CancelTask();
		RelaxStartedAt = -1.0;
		SetActorTickEnabled(false);
		return;
	}
	const double Now = World->GetTimeSeconds();
	const float TaskSeconds = IsBusy() ? static_cast<float>(Now - TaskStartedAt) : 0.0f;
	if (IsBusy())
	{
		UpdateReceiptStrip(TaskSeconds);
		FireCuesUpTo(TaskSeconds);
	}
	ApplyPose(Now);
	// 손을 푼 뒤에 종이를 손끝에 단다.
	UpdateCarriedReceipt(TaskSeconds);
}

FVector AIGStoreClerk::Outward(const int32 Index) const
{
	return Body->GetComponentTransform().TransformVectorNoScale(FVector(Arms[Index].Side, 0.0f, 0.0f));
}

AIGStoreClerk::FHandGoal AIGStoreClerk::RestingHand(const int32 Index) const
{
	// 검수한 내린 팔(rig_store_clerk.py의 hang): 손목이 몸 가운데에서 23.5 cm 바깥, 80 cm 높이. 손끝은
	// 아래로, 손바닥은 허벅지 쪽, 팔꿈치는 조금 뒤로.
	const FClerkArm& Arm = Arms[Index];
	const FTransform& Component = Body->GetComponentTransform();
	const FVector Pointing = FVector(Arm.Side * 0.06f, -0.10f, -1.0f).GetSafeNormal();
	FHandGoal Goal;
	Goal.Tip = Component.TransformPosition(FVector(Arm.Side * 23.5f, 2.0f, 80.0f) + Pointing * Arm.HandLength);
	Goal.Pointing = Component.TransformVectorNoScale(Pointing);
	Goal.Palm = Component.TransformVectorNoScale(FVector(-Arm.Side, 0.0f, 0.0f));
	Goal.Pole = Component.TransformVectorNoScale(FVector(Arm.Side * 0.5f, 1.0f, 0.0f).GetSafeNormal());
	return Goal;
}

AIGStoreClerk::FHandGoal AIGStoreClerk::CounterHand(const int32 Index) const
{
	// 어깨 바로 앞, 계산대 뒷모서리에서 5 cm 안쪽 상판을 손가락 끝으로 짚는다. 손목은 모서리 위로 떠 있다.
	const FClerkArm& Arm = Arms[Index];
	const FVector Shoulder = Body->GetComponentTransform().TransformPosition(Arm.RefUpper.GetLocation());
	const FVector Out = Outward(Index);
	FHandGoal Goal;
	Goal.Tip = FVector(Shoulder.X + Out.X * 3.0f, IGStoreClerk::CounterBackY - 5.0f, IGStoreClerk::CounterTopZ + 1.6f);
	// 포스 금전함 위로 떨어지면 금전함 윗면을 짚는다.
	const IGStoreClerk::FFixtureBox& Drawer = IGStoreClerk::Fixtures[4];
	if (FMath::Abs(Goal.Tip.X - Drawer.Center.X) < Drawer.Extent.X + 1.0f && FMath::Abs(Goal.Tip.Y - Drawer.Center.Y) < Drawer.Extent.Y + 1.0f)
	{
		Goal.Tip.Z = IGStoreClerk::DrawerTopZ + 1.6f;
	}
	Goal.Pointing = FVector(-Out.X * 0.25f, -0.55f, -0.80f).GetSafeNormal();
	Goal.Palm = FVector::DownVector;
	Goal.Pole = (Out * 0.8f + FVector(0.0f, 0.5f, -0.3f)).GetSafeNormal();
	return Goal;
}

void AIGStoreClerk::TaskHands(const float T, FHandGoal OutGoals[2]) const
{
	const int32 ScreenHand = WorkingHand;
	const int32 PaperHand = 1 - WorkingHand;
	OutGoals[ScreenHand] = CounterHand(ScreenHand);
	OutGoals[PaperHand] = CounterHand(PaperHand);
	if (CurrentTask == EIGClerkTask::SearchRecord)
	{
		// 메뉴에서 조회까지 다섯 번 누른다. 누르는 사이에는 다음 자리로 옮겨 가 화면 앞에 머문다.
		FHandGoal Goal = IGStoreClerk::ScreenGoal(IGStoreClerk::SearchKeyAt(T), IGStoreClerk::SearchPress(T), Outward(ScreenHand));
		// 마지막을 누르고 나면 손을 화면에서 떼고 결과를 읽는다.
		const float Back = IGStoreClerk::Smooth((T - 2.10f) / 0.40f);
		Goal.Tip += IGStoreClerk::ScreenNormal * (7.0f * Back) - FVector(0.0f, 0.0f, 5.0f * Back);
		OutGoals[ScreenHand] = Goal;
		return;
	}
	if (CurrentTask != EIGClerkTask::PrintReceipt)
	{
		return;
	}
	// 화면 쪽 손: 출력을 누르고 계산대 모서리로 내려온다.
	const FHandGoal Tap = IGStoreClerk::ScreenGoal(IGStoreClerk::PrintKey,
		IGStoreClerk::TapDip(T, IGStoreClerk::PrintTap), Outward(ScreenHand));
	OutGoals[ScreenHand] = IGStoreClerk::BlendGoal(Tap, CounterHand(ScreenHand), IGStoreClerk::Smooth((T - 0.46f) / 0.38f));

	// 프린터 쪽 손: 출구 위에서 종이가 다 나오기를 기다렸다가, 출구 가까이를 집어 위로 뜯고, 손님 앞
	// 상판까지 들고 가 내려놓은 뒤 손을 뗀다. 손끝은 아래 앞을, 손바닥은 종이를 본다.
	const FVector Out = Outward(PaperHand);
	const FVector Up = FVector::UpVector;
	const FVector Pinch = IGStoreClerk::PrinterExit + IGStoreClerk::StripDirection() * IGStoreClerk::PinchInset
		+ Up * IGStoreClerk::PaperUnderTip;
	const FVector Torn = Pinch + FVector(0.0f, 1.5f, 3.0f);
	const FVector Place = IGStoreClerk::PlaceTip();
	FHandGoal Paper;
	Paper.Palm = FVector(0.0f, -0.35f, -0.94f).GetSafeNormal();
	Paper.Pole = (Out * 0.7f + FVector(0.0f, 0.35f, -0.5f)).GetSafeNormal();
	if (T < IGStoreClerk::PrintPull)
	{
		const FVector Hover = Pinch + Up * 7.0f + FVector(0.0f, 1.0f, 0.0f);
		Paper.Tip = FMath::Lerp(Hover, Pinch, IGStoreClerk::Smooth((T - IGStoreClerk::PinchStart) / (IGStoreClerk::PinchAt - IGStoreClerk::PinchStart)));
		Paper.Pointing = (-Out * 0.2f + FVector(0.0f, -0.45f, -0.87f)).GetSafeNormal();
		// 계산대 모서리에서 출구 앞까지는 프린터 위로 넘어간다.
		OutGoals[PaperHand] = IGStoreClerk::ArcGoal(CounterHand(PaperHand), Paper,
			IGStoreClerk::Smooth((T - IGStoreClerk::PaperReach) / (IGStoreClerk::PaperReady - IGStoreClerk::PaperReach)), 14.0f);
		return;
	}
	if (T < IGStoreClerk::PrintPlace)
	{
		// 뜯고(0.12초), 놓을 자리 위로 옮겨, 내려놓는다.
		const FVector Lifted = FMath::Lerp(Pinch, Torn, IGStoreClerk::Smooth((T - IGStoreClerk::PrintPull) / 0.12f));
		const FVector Above = Place + Up * 8.0f;
		const float Carry = IGStoreClerk::Smooth((T - IGStoreClerk::PrintPull - 0.08f) / (IGStoreClerk::CarryArrive - IGStoreClerk::PrintPull - 0.08f));
		Paper.Tip = FMath::Lerp(FMath::Lerp(Lifted, Above, Carry), Place,
			IGStoreClerk::Smooth((T - IGStoreClerk::CarryArrive) / (IGStoreClerk::PrintPlace - IGStoreClerk::CarryArrive)));
		// 내려놓을수록 손끝을 세운다. 손목이 높아야 아래팔이 프린터 앞 모서리 위로 지나간다.
		Paper.Pointing = FMath::Lerp((-Out * 0.1f + FVector(0.0f, -0.55f, -0.83f)).GetSafeNormal(),
			(-Out * 0.05f + FVector(0.0f, -0.35f, -0.94f)).GetSafeNormal(), Carry).GetSafeNormal();
		OutGoals[PaperHand] = Paper;
		return;
	}
	// 손을 떼어 조금 들고 물렸다가 프린터를 넘어 계산대 모서리로 돌아온다.
	const float Release = IGStoreClerk::Smooth((T - IGStoreClerk::PrintPlace - 0.02f) / 0.18f);
	Paper.Tip = Place + FVector(0.0f, 2.0f * Release, 5.0f * Release);
	Paper.Pointing = (-Out * 0.05f + FVector(0.0f, -0.35f, -0.94f)).GetSafeNormal();
	OutGoals[PaperHand] = IGStoreClerk::ArcGoal(Paper, CounterHand(PaperHand),
		IGStoreClerk::Smooth((T - IGStoreClerk::PrintPlace - 0.17f) / 0.28f), 14.0f);
}

void AIGStoreClerk::SolveArm(const int32 Index, const FQuat& TorsoDelta, const FVector& TorsoJoint, const FHandGoal& Goal)
{
	const FClerkArm& Arm = Arms[Index];
	const FTransform& Component = Body->GetComponentTransform();
	const FVector Tip = Component.InverseTransformPosition(Goal.Tip);
	const FVector Pointing = Component.InverseTransformVectorNoScale(Goal.Pointing).GetSafeNormal(UE_SMALL_NUMBER, FVector::DownVector);
	const FVector PalmGoal = Component.InverseTransformVectorNoScale(Goal.Palm);
	const FVector Pole = Component.InverseTransformVectorNoScale(Goal.Pole);

	// 몸통을 따라간 기준 자세의 어깨와 마디 방향.
	FVector Shoulder = TorsoJoint + TorsoDelta.RotateVector(Arm.RefUpper.GetLocation() - TorsoJoint);
	const FVector RefUpperDir = TorsoDelta.RotateVector(Arm.RefLower.GetLocation() - Arm.RefUpper.GetLocation()).GetSafeNormal();
	const FVector RefLowerDir = TorsoDelta.RotateVector(Arm.RefHand.GetLocation() - Arm.RefLower.GetLocation()).GetSafeNormal();
	const FVector RefHandDir = TorsoDelta.RotateVector(Arm.RefTip - Arm.RefHand.GetLocation()).GetSafeNormal();
	const FVector RefPalm = TorsoDelta.RotateVector(Arm.RefPalm);

	// 손목 자리를 잡고 팔꿈치를 푼다. 팔이 모자라면 어깨를 그쪽으로 조금 내민다(어깨뼈가 앞으로 나오는 만큼).
	const FVector RestShoulder = Shoulder;
	const float Reach = (Arm.UpperLength + Arm.LowerLength) * 0.995f;
	FVector Elbow = FVector::ZeroVector;
	FVector Wrist = FVector::ZeroVector;
	FVector LowerDir = FVector::DownVector;
	float Excess = 0.0f;
	const auto SolveFor = [&](const FVector& WristGoal)
	{
		Shoulder = RestShoulder;
		FVector ToWrist = WristGoal - Shoulder;
		Excess = ToWrist.Size() - Reach;
		if (Excess > 0.0f)
		{
			Shoulder += ToWrist.GetSafeNormal() * FMath::Min(Excess, IGStoreClerk::MaxShoulderReach);
			ToWrist = WristGoal - Shoulder;
		}
		const FVector Along = ToWrist.GetSafeNormal(UE_SMALL_NUMBER, FVector::DownVector);
		const float Distance = FMath::Clamp(ToWrist.Size(), FMath::Abs(Arm.UpperLength - Arm.LowerLength) + 1.0f, Reach);
		Wrist = Shoulder + Along * Distance;
		// 팔꿈치: 어깨-손목 축에서 얼마나 떨어지는지는 코사인 법칙으로, 그 방향은 Pole 쪽이다.
		const float AlongElbow = (FMath::Square(Arm.UpperLength) - FMath::Square(Arm.LowerLength) + FMath::Square(Distance)) / (2.0f * Distance);
		const float Bend = FMath::Sqrt(FMath::Max(FMath::Square(Arm.UpperLength) - FMath::Square(AlongElbow), 0.0f));
		FVector BendSide = Pole - Along * FVector::DotProduct(Pole, Along);
		if (!BendSide.Normalize())
		{
			BendSide = FVector::CrossProduct(Along, FVector::ForwardVector * Arm.Side).GetSafeNormal(UE_SMALL_NUMBER, FVector::BackwardVector);
		}
		Elbow = Shoulder + Along * AlongElbow + BendSide * Bend;
		LowerDir = (Wrist - Elbow).GetSafeNormal();
	};
	// 손목은 MaxWristBend까지만 꺾는다. 꺾인 손 방향으로 손목 자리를 다시 잡아 한 번 더 풀면 손끝이 목표에 닿는다.
	const auto LimitBend = [](const FVector& Forearm, const FVector& Wanted)
	{
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forearm, Wanted), -1.0f, 1.0f)));
		if (Angle <= IGStoreClerk::MaxWristBend)
		{
			return Wanted;
		}
		return FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Forearm, Wanted), IGStoreClerk::MaxWristBend / Angle).RotateVector(Forearm);
	};
	SolveFor(Tip - Pointing * Arm.HandLength);
	FVector HandDir = LimitBend(LowerDir, Pointing);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		SolveFor(Tip - HandDir * Arm.HandLength);
		HandDir = LimitBend(LowerDir, HandDir);
	}
	const float WristAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(LowerDir, Pointing), -1.0f, 1.0f)));
	const FVector UpperDir = (Elbow - Shoulder).GetSafeNormal();

	// 기준 자세에서 위팔, 아래팔 순으로 가장 작게 돌려 맞춘다.
	const FQuat UpperSwing = FQuat::FindBetweenNormals(RefUpperDir, UpperDir);
	const FQuat LowerSwing = FQuat::FindBetweenNormals(UpperSwing.RotateVector(RefLowerDir), LowerDir);
	const FQuat Carried = LowerSwing * UpperSwing;
	// 손바닥은 아래팔 축으로 비틀어 맞춘다. 반은 아래팔이, 나머지는 손목이 돈다.
	float Roll = 0.0f;
	{
		const FQuat Bent = FQuat::FindBetweenNormals(LowerDir, HandDir);
		const FVector Have = Carried.RotateVector(RefPalm);
		const FVector Want = Bent.Inverse().RotateVector(PalmGoal.GetSafeNormal(UE_SMALL_NUMBER, Have));
		const FVector A = (Have - LowerDir * FVector::DotProduct(Have, LowerDir)).GetSafeNormal();
		const FVector B = (Want - LowerDir * FVector::DotProduct(Want, LowerDir)).GetSafeNormal();
		if (!A.IsNearlyZero() && !B.IsNearlyZero())
		{
			Roll = FMath::Clamp(FMath::Atan2(FVector::DotProduct(LowerDir, FVector::CrossProduct(A, B)), FVector::DotProduct(A, B)),
				-FMath::DegreesToRadians(IGStoreClerk::MaxForearmRoll), FMath::DegreesToRadians(IGStoreClerk::MaxForearmRoll));
		}
	}
	const FQuat HalfRoll(LowerDir, Roll * 0.5f);
	const FQuat FullRoll(LowerDir, Roll);
	// 손 방향: 기준 자세의 손(아래팔에서 19도쯤 꺾여 있다)을 비튼 뒤, 원하는 쪽으로 돌린다.
	const FVector RolledHand = (FullRoll * Carried).RotateVector(RefHandDir);
	const FQuat HandSwing = FQuat::FindBetweenNormals(RolledHand, HandDir);

	const FQuat UpperRotation = UpperSwing * TorsoDelta;
	const FQuat LowerRotation = HalfRoll * LowerSwing * UpperRotation;
	const FQuat HandRotation = HandSwing * FullRoll * LowerSwing * UpperRotation;
	Body->SetBoneTransformByName(Arm.Upper,
		FTransform(UpperRotation * Arm.RefUpper.GetRotation(), Shoulder, Arm.RefUpper.GetScale3D()), EBoneSpaces::ComponentSpace);
	Body->SetBoneTransformByName(Arm.Lower,
		FTransform(LowerRotation * Arm.RefLower.GetRotation(), Elbow, Arm.RefLower.GetScale3D()), EBoneSpaces::ComponentSpace);
	Body->SetBoneTransformByName(Arm.Hand,
		FTransform(HandRotation * Arm.RefHand.GetRotation(), Wrist, Arm.RefHand.GetScale3D()), EBoneSpaces::ComponentSpace);
	SolvedTip[Index] = Component.TransformPosition(Wrist + HandDir * Arm.HandLength);

	// 일하는 동안 손이 목표에 2 cm 넘게 못 미치거나 팔이 집기를 1 cm 넘게 파고들면 검사 로그에 남긴다
	// (0.25초에 한 번). 위팔 반지름 5 cm, 아래팔 4 cm, 손 1.5 cm로 본다.
	const UWorld* World = GetWorld();
	if (!World || !IsBusy() || World->GetTimeSeconds() - LastReachLogAt[Index] < 0.25)
	{
		return;
	}
	const float TaskSeconds = static_cast<float>(World->GetTimeSeconds() - TaskStartedAt);
	const float Miss = FVector::Dist(SolvedTip[Index], Goal.Tip);
	if (Miss > 2.0f)
	{
		LastReachLogAt[Index] = World->GetTimeSeconds();
		UE_LOG(LogIndieGame, Log, TEXT("STORE_CLERK_REACH t=%.2f hand=%d miss=%.1f excess=%.1f wrist_bend=%.0f"),
			TaskSeconds, Index, Miss, FMath::Max(Excess, 0.0f), WristAngle);
	}
	const FVector Joints[4] = {Component.TransformPosition(Shoulder), Component.TransformPosition(Elbow),
		Component.TransformPosition(Wrist), SolvedTip[Index]};
	const float Radii[3] = {5.0f, 4.0f, 1.5f};
	const TCHAR* Segments[3] = {TEXT("upper"), TEXT("lower"), TEXT("hand")};
	for (const IGStoreClerk::FFixtureBox& Fixture : IGStoreClerk::Fixtures)
	{
		for (int32 Segment = 0; Segment < 3; ++Segment)
		{
			const float Depth = IGStoreClerk::Penetration(Joints[Segment], Joints[Segment + 1], Radii[Segment], Fixture);
			if (Depth > 1.0f)
			{
				LastReachLogAt[Index] = World->GetTimeSeconds();
				UE_LOG(LogIndieGame, Log, TEXT("STORE_CLERK_CLIP t=%.2f hand=%d part=%s segment=%s depth=%.1f"),
					TaskSeconds, Index, Fixture.Name, Segments[Segment], Depth);
			}
		}
	}
}

void AIGStoreClerk::ApplyPose(const double Now)
{
	// 일하는 몸짓의 세기. 시작하면 0.35초에 걸쳐 들어가고, 끝나면 0.45초에 걸쳐 빠진다.
	float Weight = 0.0f;
	float TaskSeconds = 0.0f;
	IGStoreClerk::FPose Pose;
	if (IsBusy())
	{
		TaskSeconds = static_cast<float>(Now - TaskStartedAt);
		Weight = IGStoreClerk::Smooth(TaskSeconds / IGStoreClerk::EaseInSeconds);
		Pose = IGStoreClerk::TaskPose(CurrentTask, TaskSeconds);
		HeldLean = Pose.TorsoLean;
		HeldStep = Pose.Step;
	}
	else if (RelaxStartedAt >= 0.0)
	{
		// 마지막 자세에서 빠져나와 손님 쪽으로 선다.
		const float Relax = static_cast<float>(Now - RelaxStartedAt) / IGStoreClerk::RelaxSeconds;
		Weight = RelaxFromWeight * (1.0f - IGStoreClerk::Smooth(Relax));
		Pose.TorsoLean = HeldLean;
		Pose.Step = HeldStep;
		if (Relax >= 1.0f)
		{
			RelaxStartedAt = -1.0;
			bHeldGoals = false;
		}
	}
	const UWorld* World = GetWorld();
	const float DeltaSeconds = World ? World->GetDeltaSeconds() : 0.033f;

	// 계산대로 다가선다. 발은 계산대에 가려 보이지 않는다.
	SetActorLocation(HomeLocation + FVector(0.0f, -IGStoreClerk::WorkStep * Pose.Step * Weight, 0.0f));
	// 몸을 돌리는 것은 액터 방향이다. 일할 때는 볼 것 쪽으로 빨리, 쉴 때는 손님 쪽으로 천천히 돈다.
	float DesiredYaw = 0.0f;
	float TurnSpeed = 1.4f;
	TOptional<FVector> LookTarget;
	if (IsBusy() && Pose.LookAt.IsSet())
	{
		const FVector Offset = Pose.LookAt.GetValue() - GetActorLocation();
		DesiredYaw = FMath::Clamp(FMath::FindDeltaAngleDegrees(0.0f, Offset.Rotation().Yaw + 90.0f), -40.0f, 40.0f);
		TurnSpeed = 6.0f;
		LookTarget = Pose.LookAt;
	}
	else if (const APawn* Customer = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		const FVector Offset = Customer->GetActorLocation() - GetActorLocation();
		if (Offset.SizeSquared() < FMath::Square(380.0f) && Offset.Y < -55.0f)
		{
			DesiredYaw = FMath::Clamp(FMath::FindDeltaAngleDegrees(0.0f, Offset.Rotation().Yaw + 90.0f), -22.0f, 22.0f);
			LookTarget = Customer->GetPawnViewLocation();
		}
	}
	CurrentYaw = FMath::FInterpTo(CurrentYaw, DesiredYaw, DeltaSeconds, TurnSpeed);
	SetActorRotation(FRotator(0.0f, CurrentYaw, 0.0f));

	if (!bHasRig)
	{
		return;
	}
	// 숨 쉬는 작은 움직임(예전 대기 애니메이션과 같은 6초 주기)에 일하는 자세를 얹는다.
	// 몸이 -Y(손님 쪽)를 보므로 앞으로 숙이는 것은 X축 둘레 회전이다.
	const float Phase = static_cast<float>(FMath::Fmod(Now, 6.0) / 6.0) * UE_TWO_PI;
	const float TorsoLean = 0.22f * FMath::Sin(Phase * 2.0f) + Weight * Pose.TorsoLean;
	const float TorsoSway = 0.18f * FMath::Sin(Phase);
	const FQuat TorsoDelta = FQuat(FVector::UpVector, FMath::DegreesToRadians(TorsoSway))
		* FQuat(FVector::ForwardVector, FMath::DegreesToRadians(TorsoLean));
	const FVector TorsoJoint = RefTorso.GetLocation();

	// 고개: 볼 곳이 있으면 목이 그쪽으로 돌고 숙인다. 몸통이 숙인 만큼은 빼고 눈높이에서 잰다.
	float WantYaw = 0.0f;
	float WantPitch = 0.0f;
	if (LookTarget.IsSet())
	{
		const FTransform& Component = Body->GetComponentTransform();
		const FVector Eye = Component.TransformPosition(
			TorsoJoint + TorsoDelta.RotateVector(RefHead.GetLocation() + FVector(0.0f, -8.0f, 12.0f) - TorsoJoint));
		const FVector Local = Component.InverseTransformVectorNoScale(LookTarget.GetValue() - Eye);
		WantYaw = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Local.X, -Local.Y)), -35.0f, 35.0f);
		// 눈을 내리깔 수 있으니 목은 30도까지만 숙인다. 더 숙이면 손님 쪽에서 얼굴이 정수리로만 보인다.
		WantPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(-Local.Z, FVector2D(Local.X, Local.Y).Size())) - TorsoLean, -12.0f, 30.0f);
	}
	HeadYaw = FMath::FInterpTo(HeadYaw, WantYaw, DeltaSeconds, 7.0f);
	HeadAim = FMath::FInterpTo(HeadAim, WantPitch, DeltaSeconds, 7.0f);
	// 상대나 단말기를 볼 때만 고개가 움직인다. 호흡 주기마다 좌우로 흔들지 않는다.
	const float HeadPitch = HeadAim;
	const float HeadTurn = HeadYaw;
	const FQuat HeadDelta = FQuat(FVector::UpVector, FMath::DegreesToRadians(HeadTurn))
		* FQuat(FVector::ForwardVector, FMath::DegreesToRadians(HeadPitch));
	Body->SetBoneTransformByName(
		TEXT("Torso"),
		FTransform(TorsoDelta * RefTorso.GetRotation(), TorsoJoint, RefTorso.GetScale3D()),
		EBoneSpaces::ComponentSpace);
	Body->SetBoneTransformByName(
		TEXT("Head"),
		FTransform(
			TorsoDelta * HeadDelta * RefHead.GetRotation(),
			TorsoJoint + TorsoDelta.RotateVector(RefHead.GetLocation() - TorsoJoint),
			RefHead.GetScale3D()),
		EBoneSpaces::ComponentSpace);

	if (!bHasArms)
	{
		return;
	}
	// 두 손: 일하는 동안은 일의 시각에서, 마친 뒤에는 마지막 손 자리에서 내린 팔로 돌아간다.
	FHandGoal Goals[2];
	if (IsBusy())
	{
		TaskHands(TaskSeconds, HeldGoals);
		bHeldGoals = true;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FHandGoal Rest = RestingHand(Index);
		Goals[Index] = bHeldGoals ? IGStoreClerk::BlendGoal(Rest, HeldGoals[Index], Weight) : Rest;
		// 내린 손에서 계산대 위로 오가는 손은 곧장 가면 계산대 뒷모서리를 뚫는다. 가운데에서 14 cm 들어
		// 올려 모서리를 넘고, 계산대 위에서는 손끝이 상판 아래로 내려가지 않는다.
		Goals[Index].Tip.Z += 14.0f * 4.0f * Weight * (1.0f - Weight);
		if (Goals[Index].Tip.Y < IGStoreClerk::CounterBackY + 1.0f)
		{
			Goals[Index].Tip.Z = FMath::Max(Goals[Index].Tip.Z, IGStoreClerk::CounterTopZ + 0.5f);
		}
		SolveArm(Index, TorsoDelta, TorsoJoint, Goals[Index]);
	}
}
