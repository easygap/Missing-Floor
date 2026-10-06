#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IGPrologueWorldScene.generated.h"

class AIGCheckoutCounter;
class APawn;
class AIGElevator;
class AIGFridge;
class AIGInspectable;
class AIGNeighborhoodLifeDirector;
class AIGPickupItem;
class AIGSlidingDoor;
class AIGStairTransition;
class AIGSwingDoor;
class AIGZoneTrigger;
class UAudioComponent;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMaterialInterface;
class UPointLightComponent;
class UPostProcessComponent;
class UPrimitiveComponent;
class USceneComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UIGSettledDustComponent;
class UInstancedStaticMeshComponent;
enum class EIGPurchaseProfile : uint8;

/**
 * 광원이 속한 공간. 보이지 않는 공간의 광원은 켜 두지 않는다.
 * 층을 가로지르면 바닥에 가려도 거리 컷(16 m) 안이라 계산됐다.
 */
enum class EIGLightZone : uint8
{
	Always,
	FourthFloor,
	UpperStair,
	Annex,
	Lobby,
	Alley,
	Store,
	// 4층 승강기 칸. 계단이나 옥상, 닫힌 403호 안에서는 보이지 않는다.
	FourthFloorRooms,
	// 403호 안. 문이 닫혀 있으면 복도에서 보이지 않는다.
	HomeInterior,
	Count
};

/**
 * 빌라(403호·복도·로비·5층·옥상)와 골목, 편의점을 BeginPlay에 기본 도형과
 * 저작 메시로 세우는 월드. 다 세운 뒤에는 Tick하지 않는다. 밤마다 무엇이
 * 일어나는지는 없는 층 디렉터들이 정하고, 이 씬은 아래 동사로만 건물을 바꾼다.
 */
UCLASS(NotBlueprintable, Transient)
class INDIEGAME_API AIGPrologueWorldScene : public AActor
{
	GENERATED_BODY()

public:
	/**
	 * 403호가 앉은 4층 슬래브의 월드 Z. 도로에서 세 층 위다.
	 *
	 * 밤 디렉터들이 이 숫자를 각자 다시 적고 있었다. 씬의 좌표 이름공간이
	 * .cpp 안에 있어 밖에서 볼 수 없었기 때문인데, 그 상태로 슬래브를 옮기면
	 * 문·노크·순찰 높이가 옛 층에 남는다. 건물의 사실은 건물이 하나만 든다.
	 */
	static constexpr float FourthFloorZ = 900.0f;

	/**
	 * 403호 현관문이 선 자리(cm). 남쪽 벽이 Y=-225라 집 안은 Y가
	 * 0에 가까운 쪽, 복도는 그 반대쪽이다.
	 *
	 * 성분으로 내는 것은 FVector의 세 인자 생성자가 constexpr이
	 * 아니어서다. 밤 2가 이 문에서 노크·문구멍·인물 자리를 재므로
	 * 정적 초기화 순서에 걸리지 않는 값이어야 한다.
	 */
	static constexpr float HomeDoorX = 78.0f;
	static constexpr float HomeDoorY = -225.0f;
	static constexpr float WideDoorLeafWidth = 106.0f;
	static constexpr float WideDoorClearWidth = 108.0f;

	/** 실제 배치된 벽과 문에 플레이어 캡슐을 통과시킨다. 시작 구간 검사에서 사용한다. */
	bool AuditPlayerClearance(APawn* Pawn, AIGSwingDoor* BoothDoor);

	/** 403호 현관문. 그가 문 앞에서 두드리려면 문이 닫혀 있는지 알아야 한다. */
	AIGSwingDoor* GetHomeDoor() const { return HomeDoor; }

	/** 1층 공동현관. 그 시간에 밀어 본 것을 밖에서 이어 받는다(폰 신호). */
	AIGSwingDoor* GetBuildingDoor() const { return BuildingDoor; }

	/** 종이는 책상 메시의 실제 상판 높이에 놓는다. */
	float GetDeskSurfaceWorldZ() const { return DeskSurfaceWorldZ; }

	/**
	 * 복도 동쪽 끝 설비 벽장. §5.1의 세 번째 험 존이 여기 붙는다.
	 *
	 * 보일러실이라는 방은 이 게임에 없다 — §6의 공간 표에 행이 없고 배관
	 * 샤프트 설명에 한 번 나올 뿐이다. 배전반과 같은 방식으로 벽에 붙인
	 * 벽장이며, 문도 창도 없는 X 475~690 구간에 선다.
	 */
	static FVector GetBoilerCupboardLocation();

	/**
	 * 5층 베이 위를 지나는 공용 라이저.
	 *
	 * 밤 4의 망치가 「움직이는 라이저가 먼저 자기를 부른다」고
	 * 소리를 내는 자리이자, §32 자비 디렉터의 배관 울음이 나는
	 * 자리다. 둘이 같은 배관이어야 그 울음이 「이 건물에 물이 있고
	 * 움직인다」는 같은 말을 한다.
	 */
	static constexpr float SharedRiserX = 310.0f;
	static constexpr float SharedRiserY = 700.0f;
	static constexpr float SharedRiserZ = 1300.0f;
	static FVector GetSharedRiserLocation()
	{
		return FVector(SharedRiserX, SharedRiserY, SharedRiserZ);
	}

	/**
	 * 플레이어가 처음 눈뜨는 자리. 캡처 투어가 여기로 보내는데, 좌표를
	 * 투어 쪽에 적어 두면 침대를 옮겼을 때 투어만 옛 방을 찍는다.
	 */
	static FVector GetPlayerStartLocation();

	/**
	 * 로비 CCTV 모니터 화면의 실치수(cm).
	 *
	 * 케이스와 4분할 발광면은 씬이 세우고, 채널 5의 렌더 면은
	 * AIGCctvChannelFive가 세운다. 같은 화면이다. 치수를 각자 적어
	 * 두면 모니터를 키웠을 때 채널 5만 옛 크기로 남아, 화면 안에
	 * 화면이 뜬다.
	 */
	static constexpr float CctvScreenWidth = 34.0f;
	static constexpr float CctvScreenHeight = 25.5f;
	static constexpr float CctvScreenCenterZ = 105.5f;

	AIGPrologueWorldScene();

protected:
	virtual void BeginPlay() override;

public:
	// --- chapter control surface -------------------------------------------
	// The scene owns every piece of geometry and lighting in the prologue, and
	// nothing outside it should reach into that. Chapter directors drive the
	// world through this small, explicit set of verbs instead.

	/**
	 * Hands the west corridor fixture to a chapter director.
	 *
	 * The prologue runs a 10 Hz flicker timer on that fixture for the whole
	 * session; while suspended it leaves the light alone.
	 */
	void SuspendCorridorFlicker(bool bSuspend);

	/**
	 * Lights or kills one ceiling fixture, disc included.
	 *
	 * Corridor fixtures are indexed west to east (four of them); lobby
	 * fixtures are mailboxes-then-lift (two). Out-of-range indices are
	 * ignored rather than fatal — callers are chapter scripts, not code that
	 * should have to know how many lamps a corridor has.
	 */
	void SetFixtureLive(int32 Index, bool bLive, bool bCorridor);
	FVector GetCorridorFixtureLocation(int32 Index) const;

	int32 GetCorridorFixtureCount() const { return CorridorLights.Num(); }
	int32 GetLobbyFixtureCount() const { return LobbyLights.Num(); }

	/**
	 * 없는 층 '그 시간' (§1): seals or releases the building envelope.
	 *
	 * Two seals are needed, not one. The common entrance is the obvious door,
	 * but the ground-floor stair mouth opens into the open pilotis car park, so
	 * a player who takes the stairs down walks out to the alley without ever
	 * touching the entrance. The connector gate closes that hole.
	 *
	 * Sealing is expressed physically — a shut leaf, a dead lift button, a
	 * shutter across the passage — never as a HUD notice.
	 */
	void SetTheHourSealed(bool bSealed);

	/**
	 * §11 V2 403호 3단계 노화. Stage 0 is the prologue, when the flat is simply
	 * a flat; stage 2 is night four, with the ceiling corner cracked and the
	 * damp down the east wall. Nothing here is interactive or story-gated — it
	 * is the building getting worse while she lives in it, and the only player
	 * who ever notices is the one who looks up twice.
	 */
	UFUNCTION(BlueprintCallable, Category = "Missing Floor")
	void SetUnit403AgeStage(int32 Stage);

	UFUNCTION(BlueprintPure, Category = "Missing Floor")
	int32 GetUnit403AgeStage() const { return Unit403AgeStage; }

	/** Planes actually revealed at the current stage. Contract and diagnostics. */
	int32 GetUnit403AgingPlaneCount() const;

	UFUNCTION(BlueprintPure, Category = "Story|Night")
	bool IsTheHourSealed() const { return bTheHourSealed; }

	/** True once BuildLobby has produced the connector gate the seal needs. */
	bool HasNightSealGeometry() const { return StairCoreNightGate != nullptr; }

	/** P1 fixtures, so the puzzle's director can dress them without rebuilding. */
	UStaticMeshComponent* GetFifthMeterDisc() const { return FifthMeterDisc; }
	void AdvanceUtilityMeters(float Degrees, bool bUnnamedPowered, bool bCommonPowered);
	float GetUtilityMeterUpdateInterval() const;
	UStaticMeshComponent* GetUnnamedBreakerToggle() const { return UnnamedBreakerToggle; }
	UStaticMeshComponent* GetCommonBreakerToggle() const { return CommonBreakerToggle; }
	void SetCommonInspectionLightsEnabled(bool bEnabled);
	bool AreCommonInspectionLightsOff() const;

	/**
	 * 없는 층 밤1: slides the stair teleport west so the 3.5F half-landing
	 * becomes a walkable viewing pocket instead of being swallowed by the
	 * portal. Off restores the legacy trigger position byte-for-byte, so the
	 * chapters that never meet the night keep their exact traversal.
	 */
	void SetNightStairPocketEnabled(bool bEnabled);

	/**
	 * Release topology for 없는 층 night 3. The path is built from real
	 * collision surfaces: fourteen upper treads, a roof threshold, and a
	 * 6.4 m L-shaped maintenance lane. This deliberately validates structure,
	 * not a teleport trigger hidden behind a door.
	 */
	bool ValidateMissingFloorRooftopRoute(
		float& OutCenterlineLengthCentimeters,
		int32& OutUpperStepCount) const;

	/** Night 4 breaker cut; keeps geometry and flashlight independent. */
	void SetMissingFloorAnnexPower(bool bPowered);
	/**
	 * 관리실 CCTV 5번처럼 다른 층을 따로 렌더하는 화면이 살아 있는 동안 층별 조명
	 * 구역을 풀어 모든 층의 등을 켜 둔다. 켤 때와 끌 때 짝을 맞춰 부른다.
	 */
	void SetRemoteViewActive(bool bActive);

	/**
	 * §14 CCTV 채널 5's vantage, owned by the world rather than by the beat.
	 *
	 * The camera housing is permanent annex dressing because §17 asks the player
	 * to stand here in 밤3 and recognise the frame they were shown in 밤2. The
	 * scene capture borrows these three values so the live channel and the prop
	 * can never disagree about where the shot is taken from.
	 */
	UStaticMeshComponent* GetMissingFloorCctvCamera() const
	{
		return MissingFloorCctvCamera;
	}
	FVector GetMissingFloorCctvCameraLocation() const;
	FRotator GetMissingFloorCctvCameraRotation() const;
	float GetMissingFloorCctvFieldOfView() const;

	/** Removes only the cavity-facing gypsum panel, never the structural studs. */
	bool OpenMissingFloorCavity();
	/** 밤 4 재시도 때 탈착 패널의 외형과 충돌을 함께 복구한다. */
	bool ResetMissingFloorCavity();
	bool IsMissingFloorCavityOpen() const { return bMissingFloorCavityOpen; }

	/**
	 * Knocks the corridor extinguisher off its bracket. The prop is kinematic
	 * its whole life until this call — zero simulation cost at rest — and it
	 * freezes again once settled. Returns false when already dropped.
	 */
	bool DropCorridorExtinguisher();
	bool IsCorridorExtinguisherDropped() const { return bCorridorExtinguisherDropped; }
	FVector GetCorridorExtinguisherLocation() const;

	/**
	 * 냉장고가 실제로 서 있는 자리. §5.1의 험 존이 여기 붙는다.
	 * 좌표를 다른 파일에 한 번 더 적으면 냉장고만 옮겨지고 험은
	 * 옛 자리에서 계속 운다.
	 */
	FVector GetFridgeLocation() const;

private:
	// --- assembly helpers -------------------------------------------------
	UStaticMeshComponent* CreateBlock(
		const FVector& Center,
		const FVector& SizeCentimeters,
		UMaterialInterface* Material,
		bool bEnableCollision = true,
		UStaticMesh* MeshOverride = nullptr,
		const FRotator& Rotation = FRotator::ZeroRotator,
		USceneComponent* Parent = nullptr);
	/**
	 * 인쇄면을 몸통에서 떼어 낸 블록. 몸통을 돌려준다.
	 *
	 * 엔진 큐브는 여섯 면이 같은 UV를 쓴다. 그래서 두꺼운 몸통에 인쇄 재질을
	 * 그대로 주면 정면뿐 아니라 옆면·윗면·뒷면에도 같은 글자가 눌려 찍힌다.
	 * 소화전함 9 cm 마구리에 「소화전」이 한 번 더 나오고, 계량기함 6 cm
	 * 옆면에 「분전반」이 세로로 눌려 있던 것이 그것이다.
	 *
	 * PrintFacing은 월드 단위축이어야 한다. bPrintBothFaces는 돌출 간판처럼
	 * 양쪽에서 읽는 것에 쓴다.
	 */
	UStaticMeshComponent* CreatePrintedBlock(
		const FVector& Center,
		const FVector& SizeCentimeters,
		UMaterialInterface* BodyMaterial,
		UMaterialInterface* PrintMaterial,
		const FVector& PrintFacing,
		bool bEnableCollision = false,
		bool bPrintBothFaces = false);
	UStaticMeshComponent* CreatePhysicsProp(
		UStaticMesh* Mesh,
		UMaterialInterface* Material,
		const FVector& Scale,
		const FVector& Location,
		const FRotator& Rotation,
		float MassKg);
	UPointLightComponent* CreateLight(
		const FVector& Location,
		float Intensity,
		float Radius,
		const FLinearColor& Color,
		bool bCastShadows,
		float SourceRadius = 0.0f,
		USceneComponent* Parent = nullptr,
		bool bDownlight = false);
	UAudioComponent* CreateAmbientBed(
		USoundBase* Sound,
		const FVector& Location,
		float Volume,
		float InnerRadius,
		float FalloffDistance);

	/** Non-colliding dressing mesh parented to an existing component. */
	UStaticMeshComponent* CreateDecoOnComponent(
		USceneComponent* Parent,
		UStaticMesh* Mesh,
		UMaterialInterface* Material,
		const FVector& RelativeLocation,
		const FRotator& RelativeRotation,
		const FVector& RelativeScale);

	/** Loads the generated textured materials; missing entries fall back to flats. */
	void LoadTexturedMaterials();
	UMaterialInterface* TexMat(FName MaterialName, UMaterialInterface* Fallback) const;

	/** Resolves an imported photogrammetry prop's static mesh, or nullptr. */
	UStaticMesh* FindPhotoPropMesh(const TCHAR* AssetId) const;

	/**
	 * Returns a hand-authored prop mesh from /Game/Meshes (built by
	 * Scripts/generate_meshes.py), or Fallback when it has not been generated.
	 */
	UStaticMesh* PropMesh(const TCHAR* MeshName, UStaticMesh* Fallback = nullptr) const;

	/**
	 * Places an authored prop mesh at real scale. These meshes are modelled in
	 * centimeters with their base at the origin, so BaseLocation is where the
	 * object rests. Returns nullptr when the mesh has not been generated.
	 */
	UStaticMeshComponent* CreateProp(
		const TCHAR* MeshName,
		const FVector& BaseLocation,
		UMaterialInterface* Material,
		float YawDegrees = 0.0f,
		float UniformScale = 1.0f,
		bool bEnableCollision = false);

	/**
	 * Adds one non-interactive store item to a mesh/material batch. Store stock
	 * has no gameplay collision and may fade once it is too small to read.
	 */
	bool AddStoreStockInstance(
		UStaticMesh* Mesh,
		UMaterialInterface* Material,
		const FTransform& RelativeTransform,
		bool bCastShadow);

	/** Adds an authored store prop at real scale; returns false if it is absent. */
	bool AddStoreStockProp(
		const TCHAR* MeshName,
		const FVector& BaseLocation,
		UMaterialInterface* Material,
		float YawDegrees = 0.0f,
		float UniformScale = 1.0f,
		bool bCastShadow = true);

	/** Adds a cube fallback to the same store-stock batching path. */
	void AddStoreStockBlock(
		const FVector& Center,
		const FVector& SizeCentimeters,
		UMaterialInterface* Material,
		bool bCastShadow,
		const FRotator& Rotation = FRotator::ZeroRotator);

	/** Wraps a printed, non-shadowing label around one batched bottle. */
	void AddStoreStockBottleLabel(
		const FVector& BottleBase,
		float Radius,
		float BandBottomZ,
		float BandHeight,
		const TCHAR* LabelMaterialName,
		float YawDegrees);

	/** Registers completed batches once, after all instances have been added. */
	void FinalizeStoreStockBatches();

	/** Runtime release contract for draw-call batching and per-instance culling. */
	bool ValidateStoreStockBatches(
		int32& OutBatchCount,
		int32& OutInstanceCount) const;

	/**
	 * Places a scanned prop uniformly scaled to fit TargetSize, resting on
	 * FloorCenter.Z. Returns nullptr (leaving the greybox fallback to the
	 * caller) when the prop was not imported.
	 */
	UStaticMeshComponent* PlacePhotoProp(
		const TCHAR* AssetId,
		const FVector& FloorCenter,
		const FVector& TargetSize,
		float YawDegrees,
		bool bEnableCollision = true);

	/**
	 * Places furniture at audited real-world dimensions on all three axes.
	 *
	 * Photo scans such as the two-metre office desk cannot be uniformly shrunk
	 * to fit a compact room without also becoming coffee-table height. Use this
	 * only where support height and seated ergonomics are the gameplay contract;
	 * decorative scans continue through the aspect-preserving helper above.
	 */
	UStaticMeshComponent* PlacePhotoPropExactSize(
		const TCHAR* AssetId,
		const FVector& FloorCenter,
		const FVector& TargetSize,
		float YawDegrees,
		bool bEnableCollision = true);

	UStaticMeshComponent* PlacePhotoPropInternal(
		const TCHAR* AssetId,
		const FVector& FloorCenter,
		const FVector& TargetSize,
		float YawDegrees,
		bool bEnableCollision,
		bool bPreserveAspectRatio);

	// --- construction stages ---------------------------------------------
	void InitializePrologue();
	bool PositionPlayer();
	void BuildApartment();

	/** Shows the aging planes the current stage has reached, hides the rest. */
	void ApplyUnit403AgeStage();
	void BuildCorridor();
	void BuildLobby();
	/**
	 * 없는 층 밤3: extends the real 4F stair to the roof, builds the 6.4 m
	 * tank-side lane, and places the half-finished annex on the same slab.
	 */
	void BuildFifthFloorAnnex();
	void BuildAlley();
	void BuildStore();
	void BuildSkyAndFog();
	/** 옥상과 골목 하늘 끝에 보이는 먼 동네. 새벽 네 시 반의 빌라촌 불빛이다. */
	void BuildDistantSkyline();
	void SpawnInteractables();
	/** §4 숨는 자리, 건전지, 계단실 방화문과 고임목, 403호 걸쇠. */
	void SpawnShelterProps(const FActorSpawnParameters& SpawnParameters);
	void SpawnStairTransition();
	void AddStaticPurchaseBagProxy(
		AIGPickupItem* WaterBottle,
		EIGPurchaseProfile PurchaseProfile);
	void RefreshPurchaseProfilePresentation();
	void CreateAmbience();

	UFUNCTION()
	void HandleStoryStateChanged(FGameplayTag StateTag, bool bAdded);

	/** 물병을 바꿔 들면 계산대 문구와 봉투 표시를 다시 맞춘다. */
	UFUNCTION()
	void HandlePurchaseSelectionChanged(AIGPickupItem* Item);

	/** 발에 걸린 물리 소품이 부딪는 소리. 소화기는 제 낙하음이 따로 있어 뺀다. */
	UFUNCTION()
	void HandlePhysicsPropHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit);

	/** Aging-ballast shimmer on one corridor fluorescent. */
	void HandleCorridorFlicker();

	// --- components -------------------------------------------------------
	UPROPERTY(VisibleAnywhere, Category = "Prologue|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Prologue|Components")
	TArray<TObjectPtr<UStaticMeshComponent>> GeometryComponents;

	/**
	 * Dense, non-interactive convenience-store stock. A material/mesh pair owns
	 * one component instead of one UObject and draw submission per item.
	 */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> StoreStockBatches;

	UPROPERTY(VisibleAnywhere, Category = "Prologue|Lighting")
	TArray<TObjectPtr<UPointLightComponent>> Lights;

	UPROPERTY(VisibleAnywhere, Category = "Prologue|Lighting")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> AmbientBeds;

	// --- assets -----------------------------------------------------------
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> ConeMesh;

	/** Textured PBR materials generated by Scripts/create_textured_materials.py. */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UMaterialInterface>> TexturedMaterials;

	/** Lathed/beveled prop meshes generated by Scripts/generate_meshes.py. */
	UPROPERTY(Transient) mutable TMap<FName, TObjectPtr<UStaticMesh>> PropMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Prologue|Atmosphere")
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	UPROPERTY(VisibleAnywhere, Category = "Prologue|Atmosphere")
	TObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;

	UPROPERTY(VisibleAnywhere, Category = "Prologue|Atmosphere")
	TObjectPtr<USkyLightComponent> SkyAmbient;

	UPROPERTY(VisibleAnywhere, Category = "Prologue|Atmosphere")
	TObjectPtr<UDirectionalLightComponent> PreDawnSun;

	UPROPERTY(VisibleAnywhere, Category = "Prologue|Atmosphere")
	TObjectPtr<UDirectionalLightComponent> MoonLight;

	/** Everything on the 4th floor (unit 403 + its corridor) hangs off this. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> UpperFloorRoot;

	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WallMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FloorMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WoodMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BeddingMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> DoorMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> AlarmMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> AsphaltMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> ConcreteMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> ConcreteDarkMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> StoreFloorMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LightPanelMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SignMintMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SignWhiteMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> GlassMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> MetalFrameMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FridgeBodyMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FridgeInteriorMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> PlasticDarkMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> TrashBagMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> CardboardMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WaterBlueMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BottleGreenMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BottleBrownMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SnackRedMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SnackYellowMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SnackBlueMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WindowGlowMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> WindowDarkMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> NightSkyMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> StreetLampGlowMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> CoolerBodyMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> CounterTopMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> ScreenGlowMaterial;

	// --- spawned actors ---------------------------------------------------
	UPROPERTY(Transient) TObjectPtr<AIGFridge> Fridge;
	UPROPERTY(Transient) TObjectPtr<AIGSwingDoor> HomeDoor;
	UPROPERTY(Transient) TObjectPtr<AIGSwingDoor> BuildingDoor;
	UPROPERTY(Transient) TObjectPtr<AIGElevator> Elevator;
	UPROPERTY(Transient) TObjectPtr<AIGStairTransition> StairTransition;
	UPROPERTY(Transient) TObjectPtr<AIGSlidingDoor> StoreDoor;
	UPROPERTY(Transient) TObjectPtr<AIGCheckoutCounter> Checkout;
	UPROPERTY(Transient) TObjectPtr<AIGNeighborhoodLifeDirector> NeighborhoodLifeDirector;
	UPROPERTY(Transient) TObjectPtr<AIGZoneTrigger> ChapterOneApartmentExitZone;
	UPROPERTY(Transient) TObjectPtr<AIGZoneTrigger> LeftHomeZone;
	UPROPERTY(Transient) TObjectPtr<AIGZoneTrigger> StoreEntryZone;
	UPROPERTY(Transient) TArray<TObjectPtr<AIGPickupItem>> WaterBottles;

	// Ceiling fixtures, kept so a later chapter can put them out one at a
	// time. Each fixture is TWO things: the point light and the emissive disc
	// under it. Killing only the light leaves a bright ring hanging on a
	// black ceiling, which reads as a rendering bug rather than a power cut.
	UPROPERTY(Transient) TArray<TObjectPtr<UPointLightComponent>> CorridorLights;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> CorridorLightDiscs;
	UPROPERTY(Transient) TArray<TObjectPtr<UPointLightComponent>> LobbyLights;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> LobbyLightDiscs;
	UPROPERTY(Transient) TObjectPtr<AIGPickupItem> Flashlight;
	/**
	 * 없는 층: the roller shutter across the lobby-to-stair-core connector.
	 * Built hidden and non-colliding; SetTheHourSealed is the only thing that
	 * ever shows it, so the legacy chapters never meet it.
	 */
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> StairCoreNightGate;
	/** P1: the fifth meter's dial, which never turns, and its dead breaker. */
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> FifthMeterDisc;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> UtilityMeterDiscs;
	TArray<float> UtilityMeterPhases;

	/** §14 CCTV 채널 5's housing, high in the annex's south-west corner. */
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> MissingFloorCctvCamera;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> UnnamedBreakerToggle;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> CommonBreakerToggle;
	/** 밤1: the corridor extinguisher, kinematic until its scripted fall. */
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> CorridorExtinguisher;
	bool bCorridorExtinguisherDropped = false;
	FTimerHandle ExtinguisherSettleTimer;
	/** 소품마다 마지막으로 소리를 낸 시각. 구르며 닿는 접촉이 톡톡 이어지지 않게. */
	TMap<TWeakObjectPtr<UPrimitiveComponent>, double> PropImpactLastSeconds;
	double PropImpactLastAnySeconds = -1.0;
	/** 막 생긴 소품이 바닥에 자리 잡는 충돌은 소리가 아니다. 이 시각 전은 듣지 않는다. */
	double PropImpactArmedAtSeconds = 0.0;

	/** Set while a chapter owns the west corridor fixture; see the flicker handler. */
	bool bCorridorFlickerSuspended = false;
	bool bCommonInspectionLightsEnabled = true;
	bool bTheHourSealed = false;

	/**
	 * 밤의 등 배율. 그 시간에는 복도 등 넷 중 서쪽 하나만 떨리며 남고 로비는
	 * 우편함 위 하나만 남는다. SetFixtureLive가 다시 켤 때도 이 배율을 곱하므로
	 * 연출이 밤에 등을 되살려도 낮의 밝기로 돌아오지 않는다.
	 */
	float NightFixtureScale(int32 Index, bool bCorridor) const;
	/** 등·안개·카메라 룩을 그 시간에 맞추거나 새벽으로 되돌린다. */
	void ApplyNightAtmosphere(bool bSealed);
	/** 밤이 끝난 뒤의 아침빛. 입주 저녁과 새벽의 조명을 구분한다. */
	void ApplyExteriorTimeOfDay();
	/** 공용 복도의 센서등. 계량기 퍼즐과 야간 연출이 등을 끄면 개입하지 않는다. */
	void UpdateCorridorSensors(const FVector& LocalEye);
	TArray<double> CorridorSensorLastSeen;
	/**
	 * 창 너머 도로. 그 시간에는 건물이 닫혀 바깥이 멀어지고, 새벽에는 천천히
	 * 차오른다. 페이더만 움직인다 — 배수를 건드리거나 0으로 내리면 멈춘다.
	 */
	void ApplyStreetNightLevel(bool bNight);

	/**
	 * §11 V1 ③ 세대 문틈 누광. 그 시간 401호는 라디오를 끄고 불만 켠 채 벽
	 * 소리를 듣는다. 작년 7월 29일 새벽 벽에 같은 리듬으로 대답한 사람이다(§3.5).
	 * 문 아래로 따뜻한 줄 하나와 바닥에 번지는 빛이 새고, 유담이 문 앞에 서면
	 * 안쪽에서 발 그림자가 그 줄을 끊는다. 밤마다 한 번, 문 너머로 확인하는 순간이다.
	 */
	void SetUnit401GapLit(bool bLit);
	void PollUnit401GapVisitor();
	void AdvanceUnit401GapBeat();
	/** 문틈 줄을 칸으로 나눠 발이 선 자리만 가린다. First < 0이면 전부 켠다. */
	void SetUnit401GapShadow(int32 FirstSegment, int32 SegmentCount);

	/**
	 * 카메라가 있는 층에서 보일 수 없는 공간의 광원을 끈다. 4층의 창은 모두
	 * 불투명한 원경이라 바깥 빛이 들어오지 않고, 계단의 층 압축은 모퉁이 뒤라
	 * 로비가 보이지 않는다. 세기(연출)는 건드리지 않고 표시 여부만 바꾼다.
	 */
	void UpdateLightZones();
	UFUNCTION()
	void HandleStairTransitionForLights(bool bGoingDown);

	/**
	 * 창밖과 옥상 둘레의 동네가 몇 집이나 깨어 있는지(0~1). 입주한 저녁에는 거의
	 * 다 켜져 있고, 04:30에는 몇 집만 남았다가 새벽 출근하는 집부터 하나둘 켜진다.
	 * 원경 재질이 커스텀 프리미티브 데이터 0번으로 받아 창마다 켜고 끈다.
	 */
	void UpdateNeighborhoodAwake();
	UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> NightViewSurfaces;
	FTimerHandle NeighborhoodAwakeTimer;
	double HourSealedAtSeconds = -1.0;
	EIGLightZone BuildingLightZone = EIGLightZone::Always;
	TArray<TWeakObjectPtr<class ULightComponent>> ZoneLights[static_cast<int32>(EIGLightZone::Count)];
	int32 ActiveLightBand = -1;
	// 0: 403호 문이 열려 있거나 4층이 아님, 1: 닫힌 403호 안, 2: 닫힌 403호 밖.
	int32 ActiveHomeView = -1;
	FTimerHandle LightZoneTimer;

	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Unit401GapSegments;
	UPROPERTY(Transient) TObjectPtr<class URectLightComponent> Unit401GapLight;
	FTimerHandle Unit401GapPollTimer;
	FTimerHandle Unit401GapBeatTimer;
	float Unit401GapLightIntensity = 0.0f;
	int32 Unit401GapBeatStep = 0;
	bool bUnit401GapBeatPlayed = false;

	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> DegradedCorridorLight;
	UPROPERTY(Transient) TObjectPtr<class AIGStoreClerk> StoreClerk;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> JingleComponent;
	/** Bed_City_Night. 새벽마다 차오르는 도로. */
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> StreetBedComponent;

	/** When set, assembly helpers parent to this instead of SceneRoot. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> ActiveParent;

	/** Collision receipts for the portal-free missing-floor route. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> MissingFloorRooftopRouteFloors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> MissingFloorUpperStairSteps;

	/** Night 4: the three practical lights on the upper stair/roof/annex circuit. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> MissingFloorAnnexLights;
	/** 4일 차 밤의 별관 차단기. 층별 조명 구역이 이 값을 거스르지 않는다. */
	bool bMissingFloorAnnexPowered = true;
	/** SetRemoteViewActive로 켜 둔 화면 수. 0보다 크면 조명 구역은 모든 층을 켠다. */
	int32 RemoteViewCount = 0;

	/** §11 V2: the fifth-floor dust that holds footprints and drag marks. */
	UPROPERTY(Transient)
	TObjectPtr<UIGSettledDustComponent> SettledDust;

	/** §11 V2: 403호 3단계 노화. Both stages are built hidden and revealed. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Unit403AgeStageOne;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Unit403AgeStageTwo;

	int32 Unit403AgeStage = 0;

	/** The middle bay's removable gypsum face; studs and evidence remain. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> MissingFloorCavityWallPanel;

	/** Front-face hand residue follows the gypsum panel when it is removed. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> MissingFloorCavityWallResidue;
	bool bMissingFloorCavityOpen = false;

	/** Measured desk support plane. */
	float DeskSurfaceWorldZ = 0.0f;

	FTimerHandle CorridorFlickerHandle;
	/**
	 * 위층 사람이 죽어 가는 서쪽 등에 얼마나 가까운지(0~1). 두드리거나 쫓으면
	 * 더 올라간다. 등이 끊기는 빈도가 이 값을 따른다 — 그 시간에만 쓴다.
	 */
	float ComputeListenerNearness();
	TWeakObjectPtr<class AIGListenerEntity> CachedListener;
	double NextListenerSearchSeconds = 0.0;
	float DegradedLightBaseIntensity = 850.0f;
	uint32 FlickerHashCounter = 0;
	int32 PlayerPositionAttempts = 0;
	int32 BlockCounter = 0;
	bool bPrologueInitialized = false;
};
