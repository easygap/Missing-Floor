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
	// 계산대 집기 자리는 IGPrologueWorldScene이 깐다(포스 원점 2608,-190,99). 아래 셋은 그 포스 메시
	// (build_retail_refresh.py)의 직원 화면, 감열 프린터 출구, 출구 바로 앞 손님 쪽 상판이다.
	const FVector PosScreen(2608.0f, -181.0f, 133.0f);
	const FVector PrinterExit(2581.5f, -201.2f, 109.2f);
	const FVector ReceiptRest(2580.0f, -221.0f, 99.16f);
	constexpr float ReceiptRestYaw = 95.0f;
	/** 뜯기 전 밖으로 나온 종이 길이와 폭(cm). 실제 영수증 폭 79 mm. */
	constexpr float StripLength = 13.0f;
	constexpr float StripWidth = 7.8f;
	/** 일을 시작하고 몸을 돌려 숙이기까지, 끝내고 다시 손님 쪽으로 서기까지. */
	constexpr float EaseInSeconds = 0.35f;
	constexpr float RelaxSeconds = 0.45f;
	/** 이 안에 손님이 있으면 몸짓을 매 프레임 그린다. 밖이면 틱을 끈다. */
	constexpr float AwakeRadius = 1500.0f;

	/** 기록을 찾는 동안 단말기를 누르는 때. 메뉴를 고르고 날짜를 넣고 조회를 누른다. */
	constexpr float SearchTaps[] = {0.42f, 0.78f, 1.10f, 1.58f, 1.92f};
	constexpr float RecordSearchSeconds = 2.70f;
	/** 출력을 누르고, 프린터가 돌고(녹음 1.40초에 커터), 뜯고, 놓는다. */
	constexpr float PrintTap = 0.32f;
	constexpr float PrintStart = 0.70f;
	constexpr float PrintFeedSeconds = 1.38f;
	constexpr float PrintPull = 2.35f;
	constexpr float PrintPlace = 2.85f;
	constexpr float PrintSeconds = 3.30f;

	float Smooth(const float X)
	{
		const float T = FMath::Clamp(X, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	/** 누를 때마다 몸이 조금 숙여졌다 돌아온다. 누르는 순간이 가장 깊다. */
	float TapDip(const float Seconds, const float TapAt)
	{
		const float X = Seconds - TapAt;
		if (X < -0.07f || X > 0.14f)
		{
			return 0.0f;
		}
		return X < 0.0f ? Smooth((X + 0.07f) / 0.07f) : 1.0f - Smooth(X / 0.14f);
	}

	/** 몸통을 숙이는 각, 머리를 숙이는 각, 바라볼 자리(없으면 손님). */
	struct FPose
	{
		float TorsoLean = 0.0f;
		float HeadPitch = 0.0f;
		TOptional<FVector> LookAt;
	};

	FPose TaskPose(const EIGClerkTask Task, const float T)
	{
		FPose Pose;
		if (Task == EIGClerkTask::SearchRecord)
		{
			float Dip = 0.0f;
			for (const float Tap : SearchTaps)
			{
				Dip = FMath::Max(Dip, TapDip(T, Tap));
			}
			Pose.TorsoLean = 7.0f + 1.2f * Dip;
			Pose.HeadPitch = 17.0f + 3.0f * Dip + (T > 2.0f ? 1.0f : 0.0f);
			Pose.LookAt = PosScreen;
			return Pose;
		}
		if (Task == EIGClerkTask::PrintReceipt)
		{
			if (T < 0.45f)
			{
				Pose.TorsoLean = 6.0f + 1.2f * TapDip(T, PrintTap);
				Pose.HeadPitch = 15.0f + 3.0f * TapDip(T, PrintTap);
				Pose.LookAt = PosScreen;
			}
			else if (T < PrintPull + 0.05f)
			{
				// 종이가 나오는 동안 프린터를 들여다본다. 뜯는 순간 몸이 짧게 들린다.
				const float Pull = TapDip(T, PrintPull);
				Pose.TorsoLean = 10.0f - 3.5f * Pull;
				Pose.HeadPitch = 24.0f - 5.0f * Pull;
				Pose.LookAt = PrinterExit;
			}
			else
			{
				// 손님 앞 상판에 내려놓는다.
				Pose.TorsoLean = 8.0f;
				Pose.HeadPitch = 14.0f;
				Pose.LookAt = ReceiptRest;
			}
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
	GetWorldTimerManager().SetTimer(FacingTimer, this, &AIGStoreClerk::UpdateTickState, 0.1f, true);
}

void AIGStoreClerk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(FacingTimer);
	CancelTask();
	Super::EndPlay(EndPlayReason);
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
		ReceiptStrip->SetHiddenInGame(true);
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
	// 출구에서 손님 쪽으로 밀려 나오며 25도쯤 처진다. 소리가 나는 동안 길어지고 커터가 자르면 멈춘다.
	const float Length = FMath::Max(0.3f, IGStoreClerk::StripLength
		* FMath::Clamp((TaskSeconds - IGStoreClerk::PrintStart) / IGStoreClerk::PrintFeedSeconds, 0.0f, 1.0f));
	const FVector Direction = FVector(0.0f, -FMath::Cos(FMath::DegreesToRadians(25.0f)), -FMath::Sin(FMath::DegreesToRadians(25.0f)));
	const FQuat Tilt(FVector::ForwardVector, FMath::DegreesToRadians(25.0f));
	ReceiptStrip->SetWorldLocationAndRotation(IGStoreClerk::PrinterExit + Direction * (Length * 0.5f), Tilt);
	ReceiptStrip->SetWorldScale3D(FVector(IGStoreClerk::StripWidth / 100.0f, Length / 100.0f, 0.0012f));
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
	if (IsBusy())
	{
		const float TaskSeconds = static_cast<float>(Now - TaskStartedAt);
		UpdateReceiptStrip(TaskSeconds);
		FireCuesUpTo(TaskSeconds);
	}
	ApplyPose(Now);
}

void AIGStoreClerk::ApplyPose(const double Now)
{
	// 일하는 몸짓의 세기. 시작하면 0.35초에 걸쳐 들어가고, 끝나면 0.45초에 걸쳐 빠진다.
	float Weight = 0.0f;
	IGStoreClerk::FPose Pose;
	if (IsBusy())
	{
		const float TaskSeconds = static_cast<float>(Now - TaskStartedAt);
		Weight = IGStoreClerk::Smooth(TaskSeconds / IGStoreClerk::EaseInSeconds);
		Pose = IGStoreClerk::TaskPose(CurrentTask, TaskSeconds);
	}
	else if (RelaxStartedAt >= 0.0)
	{
		const float Relax = static_cast<float>(Now - RelaxStartedAt) / IGStoreClerk::RelaxSeconds;
		Weight = RelaxFromWeight * (1.0f - IGStoreClerk::Smooth(Relax));
		// 마지막에 숙이던 자세(놓는 자세)에서 빠져나온다.
		Pose.TorsoLean = 8.0f;
		Pose.HeadPitch = 14.0f;
		if (Relax >= 1.0f)
		{
			RelaxStartedAt = -1.0;
		}
	}

	// 몸을 돌리는 것은 액터 방향이다. 일할 때는 볼 것 쪽으로 빨리, 쉴 때는 손님 쪽으로 천천히 돈다.
	float DesiredYaw = 0.0f;
	float TurnSpeed = 1.4f;
	const auto YawToward = [this](const FVector& Point)
	{
		const FVector Offset = Point - GetActorLocation();
		return FMath::Clamp(FMath::FindDeltaAngleDegrees(0.0f, Offset.Rotation().Yaw + 90.0f), -40.0f, 40.0f);
	};
	if (IsBusy() && Pose.LookAt.IsSet())
	{
		DesiredYaw = YawToward(Pose.LookAt.GetValue());
		TurnSpeed = 6.0f;
	}
	else if (const APawn* Customer = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		const FVector Offset = Customer->GetActorLocation() - GetActorLocation();
		if (Offset.SizeSquared() < FMath::Square(380.0f) && Offset.Y < -55.0f)
		{
			DesiredYaw = FMath::Clamp(FMath::FindDeltaAngleDegrees(0.0f, Offset.Rotation().Yaw + 90.0f), -22.0f, 22.0f);
		}
	}
	const UWorld* World = GetWorld();
	CurrentYaw = FMath::FInterpTo(CurrentYaw, DesiredYaw, World ? World->GetDeltaSeconds() : 0.033f, TurnSpeed);
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
	const float HeadPitch = 0.7f * FMath::Sin(Phase) + Weight * Pose.HeadPitch;
	const float HeadTurn = 1.7f * FMath::Sin(Phase);
	const FQuat TorsoDelta = FQuat(FVector::UpVector, FMath::DegreesToRadians(TorsoSway))
		* FQuat(FVector::ForwardVector, FMath::DegreesToRadians(TorsoLean));
	const FQuat HeadDelta = FQuat(FVector::UpVector, FMath::DegreesToRadians(HeadTurn))
		* FQuat(FVector::ForwardVector, FMath::DegreesToRadians(HeadPitch));
	const FVector TorsoJoint = RefTorso.GetLocation();
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
}
