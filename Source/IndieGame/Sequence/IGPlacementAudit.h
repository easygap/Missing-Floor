#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/**
 * 소품 배치 실측. 바운드는 후보를 고르는 데만 쓰고, 판정은 메시 원본의 삼각형으로 한다.
 * - 지지면: 밑면 다섯 점에서 아래로 쏜 선이 다른 메시 면에 닿는 높이. 밑에 없으면 옆벽·천장 접촉을 보고,
 *   그래도 없으면 다른 메시 면에 실제로 붙었는지(꼭짓점과 면 사이 거리, 모서리가 면을 뚫는지) 본다.
 *   집게 등, 끼워 넣은 싱크대, 문짝의 손잡이가 여기서 「매단 것」으로 갈린다.
 * - 관통: 바운드가 겹친 두 메시에서 한쪽 꼭짓점이 닫힌 다른 쪽 메시 안에 있는지(세 방향 교차 홀짝)와 깊이.
 * - 광원: 등 기구 메시 안이나 곁에 있는지, 닫힌 벽 속에 묻혔는지, 그림자를 지는지.
 * 원본 메시는 에디터 데이터에만 있으므로 배포본에서는 아무것도 재지 않는다(bMeasured=false).
 */
namespace IGPlacementAudit
{
	struct FOptions
	{
		/** 이 상자와 겹치는 정적 메시와 광원만 본다. 비워 두면 월드 전체다. */
		FBox Area = FBox(ForceInit);
		/** 로그 줄에 붙는 이름. 예: day1.all, n3.trapped.302 */
		FString Label;
		/** 밑면과 지지면 사이 허용 틈(cm). */
		float SupportToleranceCm = 1.0f;
		/** 이보다 깊이 파고들어야 관통 후보로 센다(cm). */
		float PenetrationToleranceCm = 1.0f;
		/** 받침이 없을 때 다른 메시 면과 이만큼 가까우면 붙은 것으로 본다(cm). */
		float ContactToleranceCm = 1.0f;
		/** 보지 않을 액터. 플레이어와 의도된 연출의 몸(덧댄 것, 위층 사람 따위). */
		TArray<TWeakObjectPtr<const AActor>> Ignore;
	};

	struct FFlag
	{
		/** floating, penetration, light_no_fixture, light_buried */
		FName Kind;
		FString Name;
		FString Other;
		FBox Box = FBox(ForceInit);
		float Cm = 0.0f;
	};

	struct FResult
	{
		bool bMeasured = false;
		int32 Props = 0;
		int32 Supported = 0;
		/** Supported 가운데 밑받침 없이 다른 면에 붙어만 있는 것. */
		int32 Attached = 0;
		int32 Floating = 0;
		int32 PairsChecked = 0;
		int32 Penetrations = 0;
		int32 OpenMeshPairs = 0;
		int32 Lights = 0;
		int32 LightsWithoutFixture = 0;
		int32 LightsBuried = 0;
		int32 Unmeasured = 0;
		TArray<FFlag> Flags;
	};

	FResult Run(UWorld* World, const FOptions& Options);
}
