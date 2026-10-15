#include "Narrative/IGMissingFloorHints.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/IGMissingFloorLeftoverDirector.h"
#include "Entity/IGMissingFloorNightTwoBeatDirector.h"
#include "GameplayTagContainer.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Narrative/IGStoryHelpers.h"

namespace
{
	using ETruth = EIGMissingFloorTruth;
	using ESource = EIGMissingFloorSource;

	bool Beat(const UIGMissingFloorNarrativeSubsystem* N, const TCHAR* Id)
	{
		return N->HasBeatPlayed(FName(Id));
	}

	FIGHintStep Make(const TCHAR* GoalId, std::initializer_list<FText> Tiers)
	{
		FIGHintStep Step;
		Step.GoalId = FName(GoalId);
		for (const FText& Tier : Tiers)
		{
			Step.Tiers.Add(Tier);
		}
		return Step;
	}

	const AIGMissingFloorLeftoverDirector* FindLeftover(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		for (TActorIterator<AIGMissingFloorLeftoverDirector> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	/**
	 * 302호 조립대 앞에서만 꺼내는 덮개 맞추기(P6) 힌트. 고르는 일이라 밤의 길을 가로채지 않는다.
	 * 관찰할 것 → 둘을 잇는 관계 → 풀이 순서다.
	 */
	bool ResolveAssembly(const UObject* WorldContext, FIGHintStep& Out)
	{
		const AIGMissingFloorLeftoverDirector* Leftover = FindLeftover(WorldContext);
		if (!Leftover || !Leftover->IsPlayerAtAssemblyTable())
		{
			return false;
		}
		switch (Leftover->GetAssemblyHintStage())
		{
		case 0:
			Out = Make(TEXT("Hint.Leftover.Assembly"), {
				NSLOCTEXT("IGHint", "AssemblyFit1", "틀은 뒷면이 위로 누워 있어. 귀퉁이 고정쇠부터 보자."),
				NSLOCTEXT("IGHint", "AssemblyFit2", "조각 뒷면에도 구멍이 있어. 고정쇠가 그 구멍에 물려야 해."),
				NSLOCTEXT("IGHint", "AssemblyFit3", "조각을 뒤집어 구멍 쪽이 위로 오게 하고, 구멍이 고정쇠 앞에 오도록 돌려서 대 보자. 긴 조각은 긴 칸이야.") });
			return true;
		case 1:
			Out = Make(TEXT("Hint.Leftover.AssemblyFlip"), {
				NSLOCTEXT("IGHint", "AssemblyFlip1", "다 맞췄어. 앞면은 어떻게 생겼을까."),
				NSLOCTEXT("IGHint", "AssemblyFlip2", "맞춘 덮개를 뒤집어 보자.") });
			return true;
		case 2:
			Out = Make(TEXT("Hint.Leftover.AssemblyMembrane"), {
				NSLOCTEXT("IGHint", "AssemblyMembrane1", "얼룩 한쪽에 방수막이 덧대져 있어."),
				NSLOCTEXT("IGHint", "AssemblyMembrane2", "방수막을 들춰 보자.") });
			return true;
		case 3:
			Out = Make(TEXT("Hint.Leftover.AssemblyTake"), {
				NSLOCTEXT("IGHint", "AssemblyTake1", "다 봤어. 덮개를 챙기자.") });
			return true;
		default:
			return false;
		}
	}

	/** 셋째 밤 302호 구출(P7). 302호 곁에 있거나 밤의 일을 마친 뒤에만 꺼낸다. */
	bool ResolveStorageRescue(const UObject* WorldContext, const UIGMissingFloorNarrativeSubsystem* N, FIGHintStep& Out)
	{
		const FIGLeftoverState& Leftover = N->GetLeftover();
		if (Leftover.JeongwooState != FName(TEXT("Trapped")))
		{
			return false;
		}
		const AIGMissingFloorLeftoverDirector* Director = FindLeftover(WorldContext);
		const bool bNear = Director && Director->IsPlayerNearStorage();
		if (!bNear && !Beat(N, TEXT("Night3.Goal")))
		{
			return false;
		}
		const bool bFallen = Director && Director->GetStorageStage() == FName(TEXT("Fallen"));
		const FText Last = Leftover.WedgeSocket != FName(TEXT("Brace"))
			? NSLOCTEXT("IGHint", "NightThreeStorage3Wedge", "판 밑동이 바닥에 걸리는 자리에 쐐기를 끼우자.")
			: bFallen
			? NSLOCTEXT("IGHint", "NightThreeStorage3Lift", "쐐기를 물렸으니 넘어진 판을 다시 세워 받치자.")
			: (!Leftover.bWardrobeMoved
				? NSLOCTEXT("IGHint", "NightThreeStorage3Wardrobe", "장롱을 밀면 뒤에 큰 점검구가 있어. 그걸 열자.")
				: NSLOCTEXT("IGHint", "NightThreeStorage3Hatch", "장롱 뒤 점검구를 열자. 저게 그리로 옮겨 가면 판이 풀려."));
		// 2단계는 생각할 관계까지만. 관리실에서 큰 점검구로 옮겨 가는 것을 봤으면 그걸 떠올린다.
		Out = Make(TEXT("Hint.Night3.Storage"), {
			NSLOCTEXT("IGHint", "NightThreeStorage1", "302호에 판이 넘어와서 정우 씨가 못 나오고 있어."),
			Beat(N, TEXT("Leftover.SeenMove"))
				? NSLOCTEXT("IGHint", "NightThreeStorage2Seen", "판은 밑동에서 밀려 나와. 관리실에선 큰 점검구를 열자 저게 그리로 옮겨 갔지.")
				: NSLOCTEXT("IGHint", "NightThreeStorage2", "판은 밑동에서 밀려 나와. 누르는 걸 다른 데로 돌릴 수는 없을까."),
			Last });
		return true;
	}

	FIGHintStep ResolvePrologue(const UIGMissingFloorNarrativeSubsystem* N)
	{
		if (!Beat(N, TEXT("Arrival.Contract")))
		{
			return Make(TEXT("Hint.Arrival.Contract"), {
				NSLOCTEXT("IGHint", "ArrivalContract1", "책상 위에 계약서를 뒀지. 한번 읽어 보자."),
				NSLOCTEXT("IGHint", "ArrivalContract2", "403호 책상 위 임대차계약서. 그것부터 보자.") });
		}
		if (!Beat(N, TEXT("Arrival.Box.Parcel"))
			|| !Beat(N, TEXT("Arrival.Box.Notebook"))
			|| !Beat(N, TEXT("Arrival.Box.Voicemail")))
		{
			return Make(TEXT("Hint.Arrival.Boxes"), {
				NSLOCTEXT("IGHint", "ArrivalBoxes1", "짐을 아직 다 안 풀었네."),
				NSLOCTEXT("IGHint", "ArrivalBoxes2", "택배 상자, 공구 상자, 예전 휴대폰 상자. 셋 다 열어 보자.") });
		}
		if (!Beat(N, TEXT("Arrival.Store")))
		{
			return Make(TEXT("Hint.Arrival.Store"), {
				NSLOCTEXT("IGHint", "ArrivalStore1", "편의점에 사진 한번 보여 드릴까."),
				NSLOCTEXT("IGHint", "ArrivalStore2", "편의점에 가서 물어보자. 1층에서 골목으로 나가면 돼.") });
		}
		const bool b401 = Beat(N, TEXT("Arrival.Unit401"));
		const bool b402 = Beat(N, TEXT("Arrival.Unit402"));
		if (!b401 || !b402)
		{
			return Make(TEXT("Hint.Arrival.Neighbors"), {
				NSLOCTEXT("IGHint", "ArrivalNeighbors1", "옆집에는 아직 인사도 못 했네."),
				!b401 && !b402
					? NSLOCTEXT("IGHint", "ArrivalNeighborsBoth", "401호는 문을 두드려 보고, 402호는 문에 붙은 메모를 읽어 보자.")
					: (!b401
						? NSLOCTEXT("IGHint", "ArrivalNeighbors401", "401호 문을 두드려 보자. 누가 사는지는 알아야지.")
						: NSLOCTEXT("IGHint", "ArrivalNeighbors402", "402호 문에 붙은 메모를 아직 안 읽었네.")) });
		}
		if (!Beat(N, TEXT("Arrival.RoofDoor")))
		{
			return Make(TEXT("Hint.Arrival.Roof"), {
				NSLOCTEXT("IGHint", "ArrivalRoof1", "계약서에 옥상 얘기가 있었는데."),
				NSLOCTEXT("IGHint", "ArrivalRoof2", "계단 끝까지 올라가서 옥상 문을 확인해 보자.") });
		}
		return Make(TEXT("Hint.Arrival.Sleep"), {
			NSLOCTEXT("IGHint", "ArrivalSleep1", "오늘은 이만 자자."),
			NSLOCTEXT("IGHint", "ArrivalSleep2", "403호 침대에 눕자. 알람은 네 시 반에 맞춰 뒀어.") });
	}

	FIGHintStep ResolveNightOne(const UIGMissingFloorNarrativeSubsystem* N)
	{
		const bool bBothStates = Beat(N, TEXT("P1.Isolation.Unpowered"))
			&& Beat(N, TEXT("P1.Isolation.Powered"));
		const bool bSheet = N->HasSource(ETruth::LivedUpstairs, ESource::MeterReadingSheet);
		const FText Last = !bBothStates
			? NSLOCTEXT("IGHint", "NightOne3Dial", "복도등을 끄고, 이름표 없는 차단기를 내렸다 올리면서 다섯 번째 원판을 보자.")
			: (!bSheet
				? NSLOCTEXT("IGHint", "NightOne3Sheet", "검침 기록지도 읽어 봐야 해. 마지막 칸이 이상했어.")
				: NSLOCTEXT("IGHint", "NightOne3Breaker", "이름표 없는 차단기를 올려 둔 채로 원판을 한 번 더 보자."));
		return Make(TEXT("Hint.Night1.P1"), {
			NSLOCTEXT("IGHint", "NightOne1", "위에 누가 있으면 전기를 쓰겠지. 1층 계량기함을 봐야겠다."),
			NSLOCTEXT("IGHint", "NightOne2", "이름 없는 계량기가 하나 더 있었지. 복도등을 꺼도 돌아가면 다른 데 전기가 들어가는 거야."),
			Last });
	}

	FIGHintStep ResolveNightTwo(
		const UObject* WorldContext,
		const UIGMissingFloorNarrativeSubsystem* N)
	{
		if (!N->IsPuzzleSolved(FName(TEXT("P2"))))
		{
			const FText Last = !N->HasSource(ETruth::WasStillAlive, ESource::CarbonLedgerOriginal)
				? NSLOCTEXT("IGHint", "NightTwo3Carbon", "연필로 접수철 밑장을 끝까지 문질러 보자.")
				: (!N->HasSource(ETruth::WasStillAlive, ESource::AgentMoveOutMessage)
					? NSLOCTEXT("IGHint", "NightTwo3Agent", "책상에 부동산 문자 출력본이 있었어. 그것도 읽어 보자.")
					: NSLOCTEXT("IGHint", "NightTwo3Desk", "관리실 책상을 한 번 더 둘러보자."));
			return Make(TEXT("Hint.Night2.P2"), {
				NSLOCTEXT("IGHint", "NightTwo1", "소음 민원이 들어갔다면 관리실에 기록이 남았겠지."),
				NSLOCTEXT("IGHint", "NightTwo2", "민원 대장이 너무 깨끗해. 접수철은 윗장이 뜯겼던데, 밑장에 눌린 자국이 남았을지도."),
				Last });
		}
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		for (TActorIterator<AIGMissingFloorLeftoverDirector> It(World); World && It; ++It)
		{
			if (It->IsInnerRoomErrandRunning())
			{
				return Make(TEXT("Hint.Night2.InnerRoom"), {
					NSLOCTEXT("IGHint", "NightTwoInner1", "아저씨가 나간 사이에 안쪽 방을 봐야 해."),
					NSLOCTEXT("IGHint", "NightTwoInner2", "작업대와 벽에 붙은 조각, 보관함, 상자를 살펴보자."),
					NSLOCTEXT("IGHint", "NightTwoInner3", "손수레 소리가 다시 가까워지기 전에 관리실을 나가자.") });
			}
		}
		for (TActorIterator<AIGMissingFloorNightTwoBeatDirector> It(World); World && It; ++It)
		{
			if (It->GetReturnStage() == EIGNightTwoReturnStage::AwaitingBooth)
			{
				return Make(TEXT("Hint.Night2.Revisit"), {
					NSLOCTEXT("IGHint", "NightTwoRevisit1", "어젯밤엔 403호까지 못 돌아왔어."),
					NSLOCTEXT("IGHint", "NightTwoRevisit2", "1층 관리실에 다시 들렀다가 403호로 돌아오자.") });
			}
		}
		return Make(TEXT("Hint.Night2.Return"), {
			NSLOCTEXT("IGHint", "NightTwoReturn1", "이제 403호로 돌아가야 해."),
			NSLOCTEXT("IGHint", "NightTwoReturn2", "계단으로 올라가서 403호 안까지. 발소리를 줄이자."),
			NSLOCTEXT("IGHint", "NightTwoReturn3", "쫓아오면 뛰어서라도 403호 안으로 들어가자.") });
	}

	FIGHintStep ResolveNightThree(
		const UObject* WorldContext,
		const UIGMissingFloorNarrativeSubsystem* N)
	{
		// 302호 구출(P7)은 Resolve가 302호 곁에서만 먼저 본다. 여기서는 밤의 길만 본다.
		const bool bSomeoneInWall = N->HasTruth(ETruth::SomeoneInTheWall);
		const bool bAnswered = N->HasTruth(ETruth::WaitingForAnAnswer);
		if (bAnswered)
		{
			return Make(TEXT("Hint.Night3.Return"), {
				NSLOCTEXT("IGHint", "NightThreeReturn1", "이제 403호로 돌아가자."),
				NSLOCTEXT("IGHint", "NightThreeReturn2", "아까 그 박자로 두드리니까 멈췄어. 복도에서도 통할까."),
				NSLOCTEXT("IGHint", "NightThreeReturn3", "복도에서 둘, 쉬고, 하나로 두드리고, 멈춘 사이에 403호로 들어가자.") });
		}
		if (bSomeoneInWall)
		{
			// 벽에 박자를 대는 자리는 박자 출처가 둘 모여야 선다. 음성 메시지는
			// 처음부터 있으니 수첩이나 할머니 일지 하나가 더 있어야 한다.
			const int32 RhythmSources =
				(N->HasSource(ETruth::WaitingForAnAnswer, ESource::AnswerRhythmVoicemail) ? 1 : 0)
				+ (N->HasSource(ETruth::WaitingForAnAnswer, ESource::AnswerRhythmNotebook) ? 1 : 0)
				+ (N->HasSource(ETruth::WaitingForAnAnswer, ESource::AnswerRhythmJournal) ? 1 : 0);
			return Make(TEXT("Hint.Night3.P4"), {
				NSLOCTEXT("IGHint", "NightThreeRhythm1", "오빠가 문 두드리던 박자가 있었는데."),
				NSLOCTEXT("IGHint", "NightThreeRhythm2", "음성 메시지에서 그랬잖아. “문 두드리면 알지? 둘, 하나.”"),
				RhythmSources < 2
					? NSLOCTEXT("IGHint", "NightThreeRhythm3Notebook", "조율 수첩에도 그 박자가 적혀 있었을 거야. 수첩부터 다시 보자.")
					: NSLOCTEXT("IGHint", "NightThreeRhythm3", "둘, 쉬고, 하나. 그 박자로 비어 있는 벽을 두드려 보자.") });
		}
		const bool bHasKey = IGStory::HasState(
			WorldContext,
			FGameplayTag::RequestGameplayTag(FName(TEXT("State.MissingFloor.HasStairKey")), false));
		if (!bHasKey)
		{
			return Make(TEXT("Hint.Night3.Key"), {
				NSLOCTEXT("IGHint", "NightThreeKey1", "옥상 문을 열 열쇠가 있어야 해."),
				NSLOCTEXT("IGHint", "NightThreeKey2", "관리실 책상 위에 열쇠 꾸러미가 있었지."),
				NSLOCTEXT("IGHint", "NightThreeKey3", "1층 관리실에서 열쇠 꾸러미를 챙겨서 옥상으로 올라가자.") });
		}
		const bool bReachedFifth = N->HasSource(ETruth::TenantIdentity, ESource::TunerNotebookName)
			|| N->HasSource(ETruth::SomeoneInTheWall, ESource::PipeAuditionCriterion)
			|| Beat(N, TEXT("Night3.WrenchTaken"));
		if (!bReachedFifth)
		{
			return Make(TEXT("Hint.Night3.Door"), {
				NSLOCTEXT("IGHint", "NightThreeDoor1", "옥상부터 둘러보자."),
				NSLOCTEXT("IGHint", "NightThreeDoor2", "옥상 물탱크 옆으로 가면 철문이 있어."),
				NSLOCTEXT("IGHint", "NightThreeDoor3", "열쇠 꾸러미의 다른 열쇠로 5층 철문을 열자.") });
		}
		return Make(TEXT("Hint.Night3.P3"), {
			NSLOCTEXT("IGHint", "NightThreeWall1", "벽마다 소리가 다른지 들어 보자."),
			N->HasSource(ETruth::SomeoneInTheWall, ESource::PipeAuditionCriterion)
				? NSLOCTEXT("IGHint", "NightThreeWall2Criterion", "수첩에 적혀 있었지. 빈 곳은 낮게 울리고 소리가 오래 간다고.")
				: NSLOCTEXT("IGHint", "NightThreeWall2Notebook", "조율 수첩에 벽 얘기가 있었을 거야. 그것부터 읽자."),
			NSLOCTEXT("IGHint", "NightThreeWall3", "배관 밸브를 열고 벽마다 귀를 대 보자. 아니면 하나씩 두드려서 어디가 오래 울리는지 들어 보자.") });
	}

	FIGHintStep ResolveNightFour(const UIGMissingFloorNarrativeSubsystem* N)
	{
		if (!N->IsPuzzleSolved(FName(TEXT("P5"))))
		{
			const FText Last = !N->HasNightFourControl(FName(TEXT("P5.RoofCleaningDrain")))
				? NSLOCTEXT("IGHint", "NightFour3Drain", "옥상 저수조의 세척 배수 밸브부터 열자.")
				: (!N->HasNightFourControl(FName(TEXT("P5.RoofFloatBypass")))
					? NSLOCTEXT("IGHint", "NightFour3Bypass", "다음은 옥상 우회 밸브야.")
					: NSLOCTEXT("IGHint", "NightFour3Pump", "마지막으로 1층 관리실의 이송 펌프를 수동으로 돌리자."));
			return Make(TEXT("Hint.Night4.P5"), {
				NSLOCTEXT("IGHint", "NightFour1", "망치 소리가 너무 클 텐데. 좀 덜 들리게 할 수 없을까."),
				NSLOCTEXT("IGHint", "NightFour2", "탱크를 청소할 땐 배관이 시끄럽댔지. 점검 메모를 보자."),
				Last });
		}
		const FIGLeftoverState& Leftover = N->GetLeftover();
		if (!N->IsNightFourWallOpened() && !Leftover.bLegacyWallSequence)
		{
			const auto Layer = [N](const TCHAR* Name)
			{
				return N->HasLeftover(EIGLeftoverList::WallLayers, FName(Name));
			};
			switch (Leftover.WallStage)
			{
			case 0:
				return Make(TEXT("Hint.Night4.Wall0"), {
					NSLOCTEXT("IGHint", "NightFourWall0a", "물소리가 나는 동안 벽을 뜯어야 해."),
					NSLOCTEXT("IGHint", "NightFourWall0b", "가운데 칸에만 판을 한 겹 더 댔어. 고정한 데를 찾자."),
					NSLOCTEXT("IGHint", "NightFourWall0c", "위쪽 덧판 이음매를 망치로 치자. 테이프 밑에 나사가 있어.") });
			case 1:
				return Make(TEXT("Hint.Night4.Wall1"), {
					NSLOCTEXT("IGHint", "NightFourWall1a", "그게 이음매를 덮고 있어."),
					NSLOCTEXT("IGHint", "NightFourWall1b", "더 큰 틈을 내주면 그리로 옮겨 가겠지."),
					NSLOCTEXT("IGHint", "NightFourWall1c", "구석 쪽 작은 덧판을 떼어서 점검구를 열자.") });
			case 2:
				return Leftover.bFramePlaced
					? Make(TEXT("Hint.Night4.Wall2Braced"), {
						NSLOCTEXT("IGHint", "NightFourWall2Ba", "이제 못 접어."),
						NSLOCTEXT("IGHint", "NightFourWall2Bb", "이음매 위로 나사가 드러나 있었지."),
						NSLOCTEXT("IGHint", "NightFourWall2Bc", "드러난 덧판 나사를 망치로 쳐 내자.") })
					: Make(TEXT("Hint.Night4.Wall2"), {
						NSLOCTEXT("IGHint", "NightFourWall2a", "천장까지 몸을 폈어. 다시 접히면 벽으로 와."),
						Beat(N, TEXT("Leftover.Seen.BraceFrame"))
							? NSLOCTEXT("IGHint", "NightFourWall2b", "구석에 철골 틀이 있었어.")
							: NSLOCTEXT("IGHint", "NightFourWall2bUnseen", "저 마디를 걸어 둘 만한 게 이 층에 없을까. 둘러보자."),
						NSLOCTEXT("IGHint", "NightFourWall2c", "철골 틀을 세워서 저 무릎 마디를 걸어 두자.") });
			case 3:
				return !Layer(TEXT("Rebrace"))
					? Make(TEXT("Hint.Night4.Wall3"), {
						NSLOCTEXT("IGHint", "NightFourWall3a", "틀이 밀렸어."),
						NSLOCTEXT("IGHint", "NightFourWall3b", "저게 버티면서 틀을 밀고 있어."),
						NSLOCTEXT("IGHint", "NightFourWall3c", "밀린 틀을 다시 세우자.") })
					: Make(TEXT("Hint.Night4.Wall3Inner"), {
						NSLOCTEXT("IGHint", "NightFourWall3Ia", "덧판은 떼어 냈어. 안쪽 판이 남았어."),
						NSLOCTEXT("IGHint", "NightFourWall3Ib", "안쪽 판은 기둥을 따라 나사로 박았어."),
						NSLOCTEXT("IGHint", "NightFourWall3Ic", "양쪽 기둥 자리의 나사를 쳐 내자.") });
			default:
				return Make(TEXT("Hint.Night4.Wall4"), {
					NSLOCTEXT("IGHint", "NightFourWall4a", "거의 다 됐어."),
					NSLOCTEXT("IGHint", "NightFourWall4b", "위에 철물 하나가 남았어."),
					NSLOCTEXT("IGHint", "NightFourWall4c", "위쪽 가운데 ㄱ자 철물을 쳐 내자.") });
			}
		}
		if (!N->IsNightFourWallOpened())
		{
			return Make(TEXT("Hint.Night4.Wall"), {
				NSLOCTEXT("IGHint", "NightFourWall1", "물소리가 나는 동안 벽을 쳐야 해."),
				NSLOCTEXT("IGHint", "NightFourWall2", "낮에 사 온 망치가 있잖아. 5층 벽 앞으로 가자."),
				NSLOCTEXT("IGHint", "NightFourWall3", "물소리가 날 때 5층 가운데 벽을 망치로 치자. 구멍이 날 때까지.") });
		}
		if (!Beat(N, TEXT("Night4.ChoiceOffered")))
		{
			return Make(TEXT("Hint.Night4.Reveal"), {
				NSLOCTEXT("IGHint", "NightFourReveal1", "벽 안을 봐야 해."),
				NSLOCTEXT("IGHint", "NightFourReveal2", "손전등으로 벽 안쪽을 비춰 보자.") });
		}
		return Make(TEXT("Hint.Night4.Choice"), {
			NSLOCTEXT("IGHint", "NightFourChoice1", "오빠 곁에 가 보자."),
			NSLOCTEXT("IGHint", "NightFourChoice2", "튜닝 해머를 돌려놓고 갈까. 아니면 녹음을 끄고 조금 더 있을까.") });
	}

	FIGHintStep ResolveDay(const UIGMissingFloorNarrativeSubsystem* N, const int32 NightIndex)
	{
		const bool bNightDone = N->HasBeatPlayed(FName(*FString::Printf(TEXT("Night%d.Goal"), NightIndex)));
		switch (NightIndex)
		{
		case 1:
			return bNightDone
				? Make(TEXT("Hint.Day1"), {
					NSLOCTEXT("IGHint", "DayOne1", "401호 할머니는 이 건물에 오래 사셨잖아."),
					NSLOCTEXT("IGHint", "DayOne2", "401호 문을 두드려서 여쭤보자."),
					NSLOCTEXT("IGHint", "DayOne3", "밤에는 1층 관리실. 민원 대장부터 보자.") })
				: Make(TEXT("Hint.Day1.Missed"), {
					NSLOCTEXT("IGHint", "DayOneMissed1", "어젯밤엔 아무것도 못 알아냈어."),
					NSLOCTEXT("IGHint", "DayOneMissed2", "401호 할머니께 여쭤보자."),
					NSLOCTEXT("IGHint", "DayOneMissed3", "밤이 오면 1층 계량기함부터 보자.") });
		case 2:
			return bNightDone
				? Make(TEXT("Hint.Day2"), {
					NSLOCTEXT("IGHint", "DayTwo1", "할머니께 또 여쭤볼까."),
					NSLOCTEXT("IGHint", "DayTwo2", "401호 문을 두드려 보자."),
					NSLOCTEXT("IGHint", "DayTwo3", "관리실 책상 위에 열쇠 꾸러미가 있었어. 그걸로 옥상 문을 열 수 있겠다.") })
				: Make(TEXT("Hint.Day2.Missed"), {
					NSLOCTEXT("IGHint", "DayTwoMissed1", "어젯밤엔 403호까지 못 돌아왔어."),
					NSLOCTEXT("IGHint", "DayTwoMissed2", "401호 할머니께 여쭤보자."),
					NSLOCTEXT("IGHint", "DayTwoMissed3", "밤이 되면 관리실에 들렀다가 403호로 돌아오자.") });
		case 3:
			if (Beat(N, TEXT("Night3.SiteCheckDispatched")) && !Beat(N, TEXT("Night3.SiteCheck")))
			{
				return Make(TEXT("Hint.Day3.Police"), {
					NSLOCTEXT("IGHint", "DayThreePolice1", "경찰이 온다고 했어."),
					NSLOCTEXT("IGHint", "DayThreePolice2", "옥상으로 올라가서 5층 철문 안에서 기다리자.") });
			}
			if (Beat(N, TEXT("Day.EvictionPosted"))
				&& !N->HasSource(ETruth::StillCoveringIt, ESource::EvictionWarning))
			{
				return Make(TEXT("Hint.Day3.Notice"), {
					NSLOCTEXT("IGHint", "DayThreeNotice1", "4층 복도에 종이가 붙어 있던데."),
					NSLOCTEXT("IGHint", "DayThreeNotice2", "엘리베이터 옆에 붙은 퇴거 통보문을 읽어 보자.") });
			}
			return bNightDone
				? Make(TEXT("Hint.Day3"), {
					NSLOCTEXT("IGHint", "DayThree1", "할머니께 말씀드려야겠어."),
					NSLOCTEXT("IGHint", "DayThree2", "401호 문을 두드려 보자."),
					NSLOCTEXT("IGHint", "DayThree3", "할 일을 다 했으면 403호에 가서 자자.") })
				: Make(TEXT("Hint.Day3.Missed"), {
					NSLOCTEXT("IGHint", "DayThreeMissed1", "어젯밤엔 끝까지 못 갔어."),
					NSLOCTEXT("IGHint", "DayThreeMissed2", "401호 할머니께 여쭤보자."),
					NSLOCTEXT("IGHint", "DayThreeMissed3", "밤이 되면 5층 벽 앞까지 다시 가 보자.") });
		default:
			return Make(TEXT("Hint.Day.Other"), {
				NSLOCTEXT("IGHint", "DayOther1", "401호 할머니께 여쭤보자.") });
		}
	}
}

FIGHintStep IGMissingFloorHints::Resolve(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UIGMissingFloorNarrativeSubsystem* N = GameInstance
		? GameInstance->GetSubsystem<UIGMissingFloorNarrativeSubsystem>()
		: nullptr;
	if (!N)
	{
		return FIGHintStep();
	}
	const int32 NightIndex = N->GetNightIndex();
	if (NightIndex <= 0)
	{
		return ResolvePrologue(N);
	}
	// 고르는 일(조립대 앞의 덮개, 302호 곁의 정우)은 그 자리에 있을 때만 그 힌트를 꺼낸다.
	FIGHintStep Local;
	if (NightIndex >= 3 && ResolveAssembly(WorldContext, Local))
	{
		return Local;
	}
	if (NightIndex == 3 && N->IsHourSealed() && ResolveStorageRescue(WorldContext, N, Local))
	{
		return Local;
	}
	if (!N->IsHourSealed())
	{
		return ResolveDay(N, NightIndex);
	}
	// 그 밤의 일을 마쳤으면 남은 것을 보고 침대에서 쉬라고만 떠올린다. 밤4는 결말이 밤을 닫는다.
	if (NightIndex <= 3 && N->HasBeatPlayed(FName(*FString::Printf(TEXT("Night%d.Goal"), NightIndex))))
	{
		if (NightIndex == 1 && !Beat(N, TEXT("Night1.AboveConfirmed")))
		{
			return Make(TEXT("Hint.Night1.After"), {
				NSLOCTEXT("IGHint", "NightOneAfter1", "위에서 형광등 소리가 났어."),
				NSLOCTEXT("IGHint", "NightOneAfter2", "4층 복도 천장 쪽이 제일 크게 울렸어."),
				NSLOCTEXT("IGHint", "NightOneAfter3", "4층 복도 천장 밑에서 소리를 들어 보자. 그다음엔 403호 침대에서 쉬어도 돼.") });
		}
		return Make(TEXT("Hint.Night.Rest"), {
			NSLOCTEXT("IGHint", "NightRest1", "오늘 밤 할 일은 끝났어."),
			NSLOCTEXT("IGHint", "NightRest2", "더 볼 데가 있으면 보고, 아니면 403호로 돌아가자."),
			NSLOCTEXT("IGHint", "NightRest3", "403호 침대에서 아침까지 쉬자. 밖이 조용할 때.") });
	}
	switch (NightIndex)
	{
	case 1: return ResolveNightOne(N);
	case 2: return ResolveNightTwo(WorldContext, N);
	case 3: return ResolveNightThree(WorldContext, N);
	case 4: return ResolveNightFour(N);
	default: return FIGHintStep();
	}
}
