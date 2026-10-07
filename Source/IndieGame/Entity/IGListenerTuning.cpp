#include "Entity/IGListenerTuning.h"

#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

namespace IGListenerTuning
{
	namespace
	{
		/** §20.2 튜닝 테이블 (초기값). 인덱스는 밤 1~4에서 1을 뺀 값이다. */
		constexpr float NightHearingRadius[] = {900.0f, 1100.0f, 1100.0f, 1300.0f};
		constexpr float NightListenWindow[] = {8.0f, 8.0f, 7.0f, 6.0f};
		constexpr int32 NightPatrolNodes[] = {6, 11, 14, 18};
		constexpr float NightChaseMultiplier[] = {3.0f, 4.2f, 4.2f, 4.6f};
		constexpr float NightInvestigateHold[] = {6.0f, 6.0f, 5.0f, 5.0f};
		constexpr float NightHeatmapWeight[] = {0.0f, 0.3f, 0.5f, 0.7f};
		constexpr bool NightAmbushAllowed[] = {false, false, true, true};

		/**
		 * CHASE 속도 배율의 기준은 기는 속도다(§4.5 CrawlSpeed 110). 밤1의 ×3.0은
		 * 330cm/s로 걷기 175보다 빠르고 달리기 410보다 느리다. 첫 밤에는 달려서
		 * 거리를 벌 수 있다. 밤2부터는 462·462·506으로 달리기보다 빠르다. 뛰어서
		 * 거리를 버는 게 아니라 문을 닫거나 숨거나 험 옆에서 소리를 끊어야 산다.
		 */
		constexpr float CrawlSpeedBase = 110.0f;

		/**
		 * 공격성 티어는 §20.2 값에 곱해지는 별도 축이다(§4.3-7). 테이블의
		 * LISTENING 8→6→5→4초는 밤1 기준값 8초에서의 티어 축이므로, 배율은
		 * 그 값을 8로 나눈 것이다. 밤이 끝나면 티어는 1로 하강한다.
		 */
		constexpr float TierListenScale[] = {1.0f, 0.75f, 0.625f, 0.5f};

		const TCHAR* DifficultySection = TEXT("/Script/IndieGame.MissingFloor");
		const TCHAR* DifficultyKey = TEXT("NightDifficulty");

		int32 NightArrayIndex(const int32 NightIndex)
		{
			return FMath::Clamp(NightIndex, FirstNight, LastNight) - FirstNight;
		}

	}

	EIGNightDifficulty ClampDifficulty(const int32 Raw)
	{
		return static_cast<EIGNightDifficulty>(FMath::Clamp(
			Raw, 0, static_cast<int32>(EIGNightDifficulty::Count) - 1));
	}

	FIGListenerTuning Resolve(
		const int32 NightIndex,
		const EIGNightDifficulty Difficulty,
		const int32 AggressionTier)
	{
		const int32 Night = NightArrayIndex(NightIndex);
		const int32 Tier = FMath::Clamp(AggressionTier, 0, 3);

		FIGListenerTuning Tuning;
		Tuning.HearingSensitivity =
			NightHearingRadius[Night] / ReferenceHearingRadius;
		Tuning.ListenWindowSeconds =
			NightListenWindow[Night] * TierListenScale[Tier];
		Tuning.PatrolNodeCount = NightPatrolNodes[Night];
		Tuning.ChaseSpeed = CrawlSpeedBase * NightChaseMultiplier[Night];
		Tuning.InvestigateHoldSeconds = NightInvestigateHold[Night];
		Tuning.HeatmapWeight = NightHeatmapWeight[Night];
		Tuning.bTierThreeAmbushAllowed = NightAmbushAllowed[Night];
		Tuning.WaitScale = 1.0f;
		Tuning.bCaptureEnabled = true;
		Tuning.bChaseEnabled = true;

		switch (Difficulty)
		{
		case EIGNightDifficulty::Quiet:
			// 공포는 남기고 추격 압박만 걷어낸다. 기다림이 길어지는 것이
			// 이 모드의 관용이다 — 대답할 시간이 더 있다.
			Tuning.HearingSensitivity *= 0.75f;
			Tuning.ChaseSpeed *= 0.8f;
			Tuning.WaitScale = 1.5f;
			break;

		case EIGNightDifficulty::Hasty:
			// 어려움의 최소 단계는 적이 별도로 계산한다. 여기서는 노크를
			// 기다리는 시간과 자주 다닌 곳을 다시 찾을 확률을 바꾼다.
			Tuning.ListenWindowSeconds = FMath::Max(
				2.0f,
				(NightListenWindow[Night] - 1.0f) * TierListenScale[Tier]);
			Tuning.HeatmapWeight = FMath::Min(1.0f, Tuning.HeatmapWeight + 0.2f);
			break;

		case EIGNightDifficulty::ListenOnly:
			// 그는 여전히 듣고, 여전히 온다. 다만 달려들지 않고 잡지 않는다.
			// 매복도 사라진다 — 잡을 수 없는 매복은 연출일 뿐 압박이 아니다.
			Tuning.bChaseEnabled = false;
			Tuning.bCaptureEnabled = false;
			Tuning.bTierThreeAmbushAllowed = false;
			break;

		case EIGNightDifficulty::Standard:
		case EIGNightDifficulty::Count:
		default:
			break;
		}

		return Tuning;
	}

	FText GetDifficultyLabel(const EIGNightDifficulty Difficulty)
	{
		switch (Difficulty)
		{
		case EIGNightDifficulty::Quiet:
			return NSLOCTEXT("IGMissingFloor", "DifficultyQuiet", "쉬움");
		case EIGNightDifficulty::Hasty:
			return NSLOCTEXT("IGMissingFloor", "DifficultyHasty", "어려움");
		case EIGNightDifficulty::ListenOnly:
			return NSLOCTEXT(
				"IGMissingFloor",
				"DifficultyListenOnly",
				"추격 없음");
		case EIGNightDifficulty::Standard:
		default:
			return NSLOCTEXT("IGMissingFloor", "DifficultyStandard", "보통");
		}
	}

	EIGNightDifficulty LoadPersistedDifficulty()
	{
		int32 Stored = static_cast<int32>(EIGNightDifficulty::Standard);
		if (GConfig)
		{
			GConfig->GetInt(
				DifficultySection,
				DifficultyKey,
				Stored,
				GGameUserSettingsIni);
		}
		return ClampDifficulty(Stored);
	}

	void SavePersistedDifficulty(const EIGNightDifficulty Difficulty)
	{
		if (!GConfig)
		{
			return;
		}
		GConfig->SetInt(
			DifficultySection,
			DifficultyKey,
			static_cast<int32>(ClampDifficulty(static_cast<int32>(Difficulty))),
			GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	EIGNightDifficulty ResolveActiveDifficulty()
	{
		// Harness and QA switch, deliberately not written back: a validation run
		// must never change what the player chose.
		int32 Override = INDEX_NONE;
		if (FParse::Value(
			FCommandLine::Get(),
			TEXT("IGNightDifficulty="),
			Override))
		{
			return ClampDifficulty(Override);
		}
		return LoadPersistedDifficulty();
	}
}
