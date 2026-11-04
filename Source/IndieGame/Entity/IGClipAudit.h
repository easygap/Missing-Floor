#pragma once

#include "CoreMinimal.h"

class AActor;
class USkinnedAsset;
class USkinnedMeshComponent;
class UStaticMeshComponent;

namespace IGClipAudit
{
	/** -IGClipAudit나 괴물이 나오는 장면 검사로 띄웠는지. */
	bool IsRequested();
	/**
	 * -IGClipAuditTrace. 계단탑 안 프레임과 2 cm 넘게 지난 프레임마다 몸 자리·방향·가장 깊은 곳을 남기고, 쫓는
	 * 동안 몸 기준 좌표의 살과 관절이 어디까지 나오는지(5도마다 가장 먼 점)를 끝에 남긴다. 길을 다시 짤 때 쓴다.
	 */
	bool IsTraceRequested();
}

/**
 * 스킨 메시 정점을 고르게 몇 개 골라 CPU에서 지금 자세로 옮긴다. 정점마다 가장 세게 끄는 뼈도 같이 둔다.
 * 관통 측정이 쓴다. 정점 자리와 무게를 CPU에서 못 읽으면(쿠크한 빌드 따위) 비어 있다.
 */
struct FIGSkinSamples
{
	struct FSample
	{
		int32 Vertex = 0;
		int32 Bone = 0;
	};

	/** 이 메시로 이미 골라 두었는지. 못 읽어 비어 있어도 다시 고르지 않는다. */
	bool IsPreparedFor(const USkinnedMeshComponent& Mesh) const;
	void Prepare(USkinnedMeshComponent& Mesh, int32 MaxSamples);
	/** 표본마다 월드 자리를 Samples와 같은 순서로 채운다. */
	void Evaluate(USkinnedMeshComponent& Mesh, TArray<FVector>& OutPositions) const;

	TArray<FSample> Samples;
	int32 VertexCount = 0;

private:
	TWeakObjectPtr<const USkinnedAsset> PreparedAsset;
};

/**
 * 검사 실행에서 괴물 몸이 벽·문·천장·계단을 지나는지 잰다. 몸속이라고 믿을 수 있는 점(Core)에서 첫 뼈까지,
 * 뼈마다 부모 관절에서 제 관절까지 긋고, 살은 표본 정점마다 그 정점을 가장 세게 끄는 뼈의 관절에서 정점까지
 * 긋는다. 선이 보이는 메시에 막히면 막힌 자리부터 끝까지를 지나간 깊이로 센다. 틱 맨 앞에서 부르면 지난
 * 프레임에 그린 자세와 자리를 잰다. 결과는 끝날 때 CLIP_AUDIT 로그로 남긴다.
 */
struct FIGClipAudit
{
	/** 검사 실행일 때만 켠다. 이름은 로그에 찍힌다. */
	void Start(const TCHAR* InWho);
	bool IsOn() const { return bOn; }
	/**
	 * Ignored는 몸에 닿아도 관통으로 치지 않을 배우(붙잡힌 플레이어 따위)다. Phase는 기록에 같이 남는다. Held는 몸에
	 * 붙여 든 물건(안은 상자 따위)이다. Core에서 그 물건의 꼭짓점·모서리·면 가운데까지 긋는다.
	 */
	void Measure(USkinnedMeshComponent* Mesh, const FVector& Core, const AActor* Ignored, const FString& Phase,
		TConstArrayView<const UStaticMeshComponent*> Held = {});
	void Report() const;

private:
	struct FPart
	{
		int32 Frames = 0;
		float Deepest = 0.0f;
		FString Phase;
		FVector At = FVector::ZeroVector;
	};

	void Prepare(USkinnedMeshComponent& Mesh);
	void Note(const FString& Key, float Depth, const FString& Phase, const FVector& From, const FVector& To,
		const FVector& Impact, TSet<FString>& FrameKeys);

	FString Who;
	bool bOn = false;
	TWeakObjectPtr<const USkinnedAsset> PreparedAsset;
	TArray<int32> Parents;
	/** 바닥 원점에 놓인 손잡이 뼈(뼈대 뿌리, root). 몸이 아니라 마디도 살도 Core에서 긋는다. */
	TArray<bool> Handles;
	TArray<FString> BoneNames;
	FIGSkinSamples Skin;
	int32 Frames = 0;
	int32 FramesOver2 = 0;
	int32 FramesOver5 = 0;
	float Deepest = 0.0f;
	FString DeepestKey;
	TMap<FString, FPart> Parts;
	/** 5 cm 넘게 지난 프레임을 그 프레임에서 가장 깊이 지난 물체와 Phase로 나눠 센다. */
	TMap<FString, int32> Causes;
	/**
	 * 면 뒤로 실제로 들어간 깊이(면에 수직으로 잰다). 위의 깊이는 선을 따라 재서, 벽을 따라 나란히 스치는 팔다리를
	 * 1 cm 들어가도 수십 cm로 센다. 눈에 보이는 관통은 이쪽을 본다.
	 */
	int32 FramesBehind2 = 0;
	int32 FramesBehind5 = 0;
	float DeepestBehind = 0.0f;
	FString DeepestBehindKey;
	FVector DeepestBehindAt = FVector::ZeroVector;
	TMap<FString, int32> BehindCauses;
	/** 5 cm 넘게 들어간 프레임을 몸이 있던 자리(50 cm 칸)와 Phase로 나눠 센다. 어디서 고쳐야 하는지 본다. */
	TMap<FString, FPart> BehindSpots;
	/** -IGClipAuditTrace일 때 5도마다 몸 가운데에서 가장 먼 점(몸 기준 좌표). */
	TArray<FVector> Footprint;
};
