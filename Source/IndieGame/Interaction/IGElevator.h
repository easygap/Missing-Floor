#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGElevator.generated.h"

class AIGElevatorButton;
class APawn;
class UAnimSequence;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UPrimitiveComponent;
class USceneCaptureComponent2D;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTextureRenderTarget2D;

/** 승강기 버튼의 종류. 층 버튼은 Floor 값으로 층을 가른다(0이 1층, 4가 「5」, -1이 B1). */
UENUM()
enum class EIGElevatorButtonKind : uint8
{
	HallCall,
	Floor,
	Open,
	Close,
	Alarm
};

/** 칸 뒷벽 거울에 유담이 어떻게 비치는가. */
UENUM()
enum class EIGElevatorReflection : uint8
{
	/** 그대로 비친다. 마주 보면 얼굴이 보인다. */
	Normal,
	/** 마주 보고 서 있어도 거울에는 뒷모습이 비친다. */
	Back,
	/** 거울에 아무도 없다. */
	Absent
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FIGElevatorButtonPressed, EIGElevatorButtonKind, int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FIGElevatorArrived, int32);

/**
 * 빌라 승강기 한 대. 칸 하나가 승강로를 실제로 오르내린다.
 *
 * 1층부터 4층까지 층마다 승강장 문틀·문짝·호출판·층 표시창이 있고, 칸에는
 * 칸 문 두 짝과 조작반이 붙어 같이 움직인다. 탄 사람은 칸 바닥에 서 있으므로
 * 캐릭터 이동이 바닥을 따라간다. 순간이동은 없다.
 *
 * 액터 원점은 1층 바닥 높이의 승강로 가운데이고, 액터는 yaw -90으로 놓여
 * 로컬 -Y가 승강장(세계 -X)을 본다. 저작 메시의 「앞 -Y」와 같은 방향이다.
 *
 * 뒷벽 가운데는 광택 스테인리스 거울이다. 칸 안에 사람이 있는 동안 거울 뒤에서
 * 칸을 거꾸로 비추는 캡처가 돌아, 흐린 반사로 유담 자신이 비친다(평소에는
 * 렌더하지 않는 몸, SK_YudamReflection). 만원 사건은 이 거울을 쓴다.
 */
UCLASS()
class INDIEGAME_API AIGElevator : public AActor
{
	GENERATED_BODY()

public:
	AIGElevator();
	virtual void Tick(float DeltaSeconds) override;

	struct FIGElevatorVisuals
	{
		UStaticMesh* CabSides = nullptr;
		UStaticMesh* CabFront = nullptr;
		UStaticMesh* CabBack = nullptr;
		UStaticMesh* BackPanel = nullptr;
		UStaticMesh* CabCeiling = nullptr;
		UStaticMesh* CabFloor = nullptr;
		UStaticMesh* DoorPanel = nullptr;
		UStaticMesh* LandingFrame = nullptr;
		UStaticMesh* Cop = nullptr;
		UStaticMesh* Cctv = nullptr;
		UStaticMesh* CallPlate = nullptr;
		/** 조작반 버튼 옆 등(SM_ElevatorButtonLed). 구운 발광 맵을 세기로 켜고 끈다. */
		UStaticMesh* IndicatorMesh = nullptr;
		/** 문 위 「만원」 표시등(SM_ElevatorFullLamp). */
		UStaticMesh* FullLamp = nullptr;
		/** 거울 캡처를 보여 주는 무광원 재질(M_ElevatorMirror). 없으면 그냥 스테인리스다. */
		UMaterialInterface* MirrorMaterial = nullptr;
		/** 캡처가 쉬는 동안 거울 판에 씌우는 광택 스테인리스. */
		UMaterialInterface* BackPanelMaterial = nullptr;
		USkeletalMesh* ReflectionMesh = nullptr;
		UAnimSequence* ReflectionIdle = nullptr;
		UAnimSequence* ReflectionWalk = nullptr;
		UAnimSequence* ReflectionLookBack = nullptr;
	};

	void Configure(const FIGElevatorVisuals& Visuals);

	static constexpr int32 LandingCount = 4;
	/** 층 사이 높이. 1층 바닥이 0이다. */
	static constexpr float StoreyHeight = 300.0f;
	/** 4층 위, 옥상 기계실 안. 정상 운행에서는 가지 않는다. */
	static constexpr float OverrunZ = 1200.0f;
	/** 칸 안쪽 높이. 바닥 윗면(칸 원점)에서 천장 아랫면까지다. */
	static constexpr float CabHeight = 225.0f;
	static float GetLandingZ(const int32 Landing) { return Landing * StoreyHeight; }

	float GetCabZ() const { return CabZ; }
	/** 칸이 멈춰 선 층. 움직이는 중이거나 층 사이면 INDEX_NONE. */
	int32 GetStoppedLanding() const;
	bool IsMoving() const { return bMoving; }
	bool AreDoorsClosed() const { return DoorOpenAlpha <= 0.001f; }
	bool AreDoorsFullyOpen() const { return DoorOpenAlpha >= 0.999f; }
	bool IsPawnInsideCab(const APawn* Pawn) const;
	USceneComponent* GetCabRoot() const { return CabRoot; }
	/** 칸 바닥 가운데의 세계 좌표. */
	FVector GetCabFloorWorldLocation() const;
	/** 층 승강장 문 앞(복도 쪽 1 m)의 세계 좌표. */
	FVector GetLandingFrontWorldLocation(int32 Landing) const;

	// --- 버튼 ------------------------------------------------------------
	bool CanPress(EIGElevatorButtonKind Kind, int32 Floor, const AActor* Presser) const;
	FText GetButtonPrompt(EIGElevatorButtonKind Kind, int32 Floor) const;
	void Press(EIGElevatorButtonKind Kind, int32 Floor, AActor* Presser);
	FVector GetHallCallWorldLocation(int32 Landing) const;

	/**
	 * 그 시간(04:30~05:30)의 승강기. 칸은 선 자리에 문을 닫은 채 멈춰 있다.
	 * 버튼은 눌리지만 불이 안 들어오고 층 표시가 꺼진다.
	 */
	void SetHourDead(bool bDead);
	bool IsHourDead() const { return bHourDead; }
	/** 새 장·불러오기. 칸을 1층에 문 닫힌 채 세우고 대본 제어를 푼다. */
	void ResetForNewRide();

	// --- 대본 제어(만원 사건) -------------------------------------------
	/** 켜는 동안 버튼은 소리만 내고 OnButtonPressed로만 알린다. 칸은 대본이 움직인다. */
	void SetScriptedControl(bool bScripted);
	bool IsScripted() const { return bScripted; }
	/** 목표 높이까지 가속·등속·감속으로 간다. */
	void ScriptTravelTo(float TargetZ, float MaxSpeed, float Accel);
	/** 그 자리에서 선다. JoltCm만큼 아래로 출렁였다 돌아온다. */
	void ScriptHalt(float JoltCm);
	/** DropCm만큼 떨어지다 제동이 걸린다. 칸 안은 크게 흔들린다. */
	void ScriptDrop(float DropCm);
	/** 칸 문(층이 맞으면 승강장 문도)을 Seconds에 걸쳐 열거나 닫는다. */
	void ScriptSetDoors(bool bOpen, float Seconds);
	/** 칸 안·승강장 층 표시를 덮어쓴다. 빈 문자열이면 꺼진 것처럼 보인다. */
	void ScriptSetDisplay(const FText& Text);
	void ScriptClearDisplay();
	void ScriptSetFullLamp(bool bLit);
	/** 칸 등 세기 0~1. 떨림을 켜면 형광등처럼 떤다. */
	void ScriptSetCabLight(float Level, bool bFlicker);
	void ScriptSetButtonLit(int32 Floor, bool bLit);
	/** 칸 무게로 줄이 늘어난 만큼 칸이 가라앉는다. 대본이 0으로 되돌린다. */
	void ScriptSetSag(float SagCm);
	void PlayOverloadBuzzer(bool bOn);
	void PlayCableCreak(float Volume);

	// --- 거울 ------------------------------------------------------------
	void SetReflection(EIGElevatorReflection Mode);
	EIGElevatorReflection GetReflection() const { return Reflection; }
	/** 반사 몸이 고개를 돌려 뒤를 본다(LookBack 한 번). */
	void PlayReflectionLookBack();
	/** 거울에만 보이는 몸을 하나 더 세운다. 칸 바닥 기준 로컬 위치와 yaw. */
	USkeletalMeshComponent* AddReflectionFigure(const FVector& CabLocal, float Yaw);
	void ClearReflectionFigures();
	const TArray<TObjectPtr<USkeletalMeshComponent>>& GetReflectionFigures() const { return ReflectionFigures; }
	/** 거울 캡처가 지금 돌고 있는가. 칸 안에 사람이 있을 때만 돈다. */
	bool IsMirrorLive() const { return bMirrorLive; }

	FIGElevatorButtonPressed OnButtonPressed;
	FIGElevatorArrived OnArrived;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EIGElevatorPhase : uint8
	{
		Idle,
		DoorsOpening,
		DoorsOpen,
		DoorsClosing,
		Travelling
	};

	UStaticMeshComponent* MakeMesh(USceneComponent* Parent, UStaticMesh* Mesh, FName Name,
		const FVector& Location, const FRotator& Rotation, bool bCollide);
	UTextRenderComponent* MakeDisplay(USceneComponent* Parent, FName Name, const FVector& Location,
		const FRotator& Rotation, float WorldSize);
	AIGElevatorButton* SpawnButton(USceneComponent* Parent, EIGElevatorButtonKind Kind, int32 Floor,
		const FVector& Location, const FRotator& Rotation, const FVector& Extent);
	void BuildMirror(const FIGElevatorVisuals& Visuals);

	void SetPhase(EIGElevatorPhase NewPhase);
	void ApplyCabTransform();
	void ApplyDoors();
	void BeginTravel(float TargetZ, float MaxSpeed, float Accel);
	void AdvanceTravel(float DeltaSeconds);
	void Arrive();
	void ServeNextCall();
	int32 PickNextCall() const;
	bool HasPendingCall() const;
	void ClearCall(int32 Landing);
	bool IsDoorwayObstructed() const;
	void UpdateDisplays();
	void SetDisplayText(const FText& Text);
	void RefreshButtonLights();
	void SetCabLightLevel(float Level);
	void UpdateCabLightFlicker(float DeltaSeconds);
	void UpdateLightCulling();
	/** 할 일이 생기면 틱을 깨운다. 칸이 서 있고 문이 닫혀 있고 안에 아무도 없으면 다시 잔다. */
	void Wake();
	bool CanSleep() const;
	void UpdateMirror(float DeltaSeconds);
	void UpdateReflectionBody(const APawn* Pawn, const FVector& EyeLocation, float DeltaSeconds);
	void StartMotorHum();
	void StopMotorHum();
	void PlayDoorMotor(bool bOpening);
	void PlayChime(int32 Landing);
	void PlayButtonClick(const FVector& Location, bool bAccepted);
	void ReportPressNoise(const FVector& Location, AActor* Presser);
	APawn* GetPlayerPawn() const;

	UPROPERTY(VisibleAnywhere, Category = "Elevator")
	TObjectPtr<USceneComponent> ElevatorRoot;

	UPROPERTY(VisibleAnywhere, Category = "Elevator")
	TObjectPtr<USceneComponent> CabRoot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CarDoors;

	/** 층마다 두 짝. 층 i의 짝은 [2i], [2i+1]. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> LandingDoors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> HallDisplays;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> CabDisplays;

	/** 조작반 버튼 옆 등. [0..3]이 1~4층, [4]가 「5」, [5]가 B1. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ButtonLamps;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FullLamp;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FullLampMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CeilingMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CeilingMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BackPanelMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MirrorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BackPanelSteel;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> MirrorCapture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> MirrorTarget;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ReflectionBody;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> ReflectionFigures;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> ReflectionMesh;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ReflectionIdle;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ReflectionWalk;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ReflectionLookBack;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> CabLight;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> FloorFill;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MotorHum;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Buzzer;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AIGElevatorButton>> Buttons;

	EIGElevatorPhase Phase = EIGElevatorPhase::Idle;
	float CabZ = 0.0f;
	/** 칸이 출렁이거나 가라앉은 만큼. 칸 높이에 더해 그린다. */
	float CabOffsetZ = 0.0f;
	float SagZ = 0.0f;
	float JoltZ = 0.0f;
	float JoltVelocity = 0.0f;
	float TravelStartZ = 0.0f;
	float TravelTargetZ = 0.0f;
	float TravelSpeed = 0.0f;
	float TravelMaxSpeed = 105.0f;
	float TravelAccel = 80.0f;
	bool bMoving = false;
	bool bDropping = false;
	float DropRemaining = 0.0f;
	float DoorOpenAlpha = 0.0f;
	float DoorTargetAlpha = 0.0f;
	float DoorSpeed = 1.0f;
	float DoorHoldSeconds = 0.0f;
	/** 칸이 마지막으로 멈춘 층. 문 열기·표시가 이 층을 쓴다. */
	int32 CurrentLanding = 0;
	int32 LastShownFloor = -1;
	uint8 PendingCalls = 0;
	bool bHourDead = false;
	bool bHourDeadThoughtShown = false;
	bool bFiveThoughtShown = false;
	bool bAlarmAnswered = false;
	bool bScripted = false;
	bool bDisplayOverride = false;
	bool bFullLampLit = false;
	bool bCabLightFlicker = false;
	float CabLightLevel = 1.0f;
	float FlickerClock = 0.0f;
	uint8 ScriptLitButtons = 0;
	bool bMirrorLive = false;
	float MirrorClock = 0.0f;
	EIGElevatorReflection Reflection = EIGElevatorReflection::Normal;
	bool bReflectionWalking = false;
	float ReflectionLookBackSeconds = 0.0f;
	bool bVisualsConfigured = false;
	int32 ComponentCounter = 0;
	FTimerHandle AlarmReplyTimer;
	FTimerHandle LightCullTimer;
};
