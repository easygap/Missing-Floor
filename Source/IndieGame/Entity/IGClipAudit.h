#pragma once

#include "CoreMinimal.h"

class AActor;
class USkinnedAsset;
class USkinnedMeshComponent;

namespace IGClipAudit
{
	/** -IGClipAudit나 괴물이 나오는 장면 검사로 띄웠는지. */
	bool IsRequested();
}

/**
 * 스킨 메시 정점을 고르게 몇 개 골라 CPU에서 지금 자세로 옮긴다. 정점마다 가장 세게 끄는 뼈도 같이 둔다.
 * 관통 측정과 위층 사람의 몸 맞추기가 쓴다. 정점 자리와 무게를 CPU에서 못 읽으면(쿠크한 빌드 따위) 비어 있다.
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
	/** Ignored는 몸에 닿아도 관통으로 치지 않을 배우(붙잡힌 플레이어 따위)다. Phase는 기록에 같이 남는다. */
	void Measure(USkinnedMeshComponent* Mesh, const FVector& Core, const AActor* Ignored, const FString& Phase);
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
};
