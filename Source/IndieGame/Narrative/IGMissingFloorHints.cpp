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
				NSLOCTEXT("IGHint", "AssemblyFit1", "틀 귀퉁이에 고정쇠가 있네. 조각을 여기에 거는 건가."),
				NSLOCTEXT("IGHint", "AssemblyFit2", "조각 뒷면을 보자. 고정쇠가 물릴 구멍이 있을 거야."),
				NSLOCTEXT("IGHint", "AssemblyFit3", "구멍이 위로 오게 조각을 뒤집고, 고정쇠에 맞춰 돌려 보자. 긴 조각은 긴 칸에.") });
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
				NSLOCTEXT("IGHint", "AssemblyTake1", "덮개를 챙겨서 어디에 붙어 있던 건지 찾아보자.") });
			return true;
		default:
			return false;
		}
	}

	/**
	 * 셋째 밤 302호 구출(P7). 판 너머에서 부르는 소리를 들은 뒤에만 꺼내고, 멀리서는 밤의 일을 마친 뒤에만
	 * 떠올린다. 방에 들어가 보기 전에는 판을 누르는 것이 있다고 말하지 않고, 쐐기는 주운 뒤에야 이름으로
	 * 부른다. 하위 상태마다 목표 이름을 따로 두어, 상태가 바뀌면 첫 단계부터 다시 센다(§19.4).
	 */
	bool ResolveStorageRescue(const UObject* WorldContext, const UIGMissingFloorNarrativeSubsystem* N, FIGHintStep& Out)
	{
		const FIGLeftoverState& Leftover = N->GetLeftover();
		if (Leftover.JeongwooState != FName(TEXT("Trapped")) || !Beat(N, TEXT("Leftover.N3.Voice")))
		{
			return false;
		}
		const AIGMissingFloorLeftoverDirector* Director = FindLeftover(WorldContext);
		const bool bNear = Director && Director->IsPlayerNearStorage();
		if (!bNear && !Beat(N, TEXT("Night3.Goal")))
		{
			return false;
		}
		const FName Stage = Director ? Director->GetStorageStage() : NAME_None;
		// 판이 풀려 정우가 밀고 나오는 중이다. 할 일이 남지 않았으니 밤의 길로 돌려보낸다.
		if (Stage == FName(TEXT("Released")) || Stage == FName(TEXT("Freed")))
		{
			return false;
		}
		const bool bSawRoom = (Director && Director->IsPlayerInStorageRoom())
			|| N->HasBeatPlayed(AIGMissingFloorLeftoverDirector::SawStorageRoomBeat);
		if (!bSawRoom)
		{
			// 들은 것은 「판에 끼었어요」뿐이다. 무엇이 판을 누르는지는 방에 들어가야 안다.
			Out = Make(TEXT("Hint.Night3.Storage.Call"), {
				Beat(N, TEXT("Day.Jeongwoo.Intro"))
					? NSLOCTEXT("IGHint", "NightThreeStorageCall1Named", "정우 씨가 302호에서 판에 끼었다고 했어.")
					: NSLOCTEXT("IGHint", "NightThreeStorageCall1", "302호 안에서 누가 판에 끼었다고 했어."),
				NSLOCTEXT("IGHint", "NightThreeStorageCall2", "302호에 들어가서 판이 어떻게 걸렸는지 보자."),
				NSLOCTEXT("IGHint", "NightThreeStorageCall3", "3층 302호 문으로 들어가서 판 너머를 살펴보자.") });
			return true;
		}
		const bool bBraced = Leftover.WedgeSocket == FName(TEXT("Brace"));
		const FText BraceIt = N->HasLeftover(EIGLeftoverList::Taken, FName(TEXT("Wedge")))
			? NSLOCTEXT("IGHint", "NightThreeStorageBrace3Wedge", "쐐기는 장판 말고, 판 밑동이 철골 토막에 걸린 자리에 끼우자.")
			: NSLOCTEXT("IGHint", "NightThreeStorageBrace3Find", "문 옆 공사 찌꺼기에서 받칠 걸 찾아, 판 밑동이 철골 토막에 걸린 자리에 끼우자.");
		if (Stage == FName(TEXT("Fallen")))
		{
			Out = Make(TEXT("Hint.Night3.Storage.Fallen"), {
				NSLOCTEXT("IGHint", "NightThreeStorageFallen1", "판이 더 내려앉았어. 밑동이 밀렸어."),
				NSLOCTEXT("IGHint", "NightThreeStorageFallen2", "밑동을 받쳐 두면 판을 다시 세울 수 있을 거야."),
				bBraced
					? NSLOCTEXT("IGHint", "NightThreeStorageFallen3Lift", "쐐기가 받치고 있으니 판을 다시 세우자.")
					: BraceIt });
			return true;
		}
		if (!bBraced)
		{
			Out = Make(TEXT("Hint.Night3.Storage.Brace"), {
				NSLOCTEXT("IGHint", "NightThreeStorageBrace1", "판 밑동부터 보자."),
				NSLOCTEXT("IGHint", "NightThreeStorageBrace2", "밑동이 밀리면 판이 더 누울 거야. 먼저 받쳐 두자."),
				BraceIt });
			return true;
		}
		// 받쳐 뒀다. 이제 판을 누르는 것을 다른 틈으로 보낸다. 넓은 틈으로 옮겨 간다는 것은 관리실에서
		// 두 번째로 옮겨 가는 것을 본 회차만 기억으로 말한다.
		const FText Rule = Beat(N, TEXT("Leftover.N2.SecondMove"))
			? NSLOCTEXT("IGHint", "NightThreeStorageGap2Seen", "관리실에선 큰 점검구를 여니까 그리로 옮겨 갔지.")
			: NSLOCTEXT("IGHint", "NightThreeStorageGap2", "더 넓은 틈이 생기면 그리로 옮겨 갈지도 몰라.");
		Out = !Leftover.bWardrobeMoved
			? Make(TEXT("Hint.Night3.Storage.Wardrobe"), {
				NSLOCTEXT("IGHint", "NightThreeStorageGap1", "벽에 붙은 게 판을 누르고 있어."),
				Rule,
				NSLOCTEXT("IGHint", "NightThreeStorageGap3Wardrobe", "장롱을 밀어서 뒤쪽 벽을 살펴보자.") })
			: Make(TEXT("Hint.Night3.Storage.Hatch"), {
				NSLOCTEXT("IGHint", "NightThreeStorageHatch1", "장롱 뒤에 점검구가 있었어."),
				Rule,
				NSLOCTEXT("IGHint", "NightThreeStorageGap3Hatch", "장롱 뒤 점검구를 열자. 판을 누르던 게 그리로 옮겨 가면 판이 풀릴 거야.") });
		return true;
	}

	FIGHintStep ResolvePrologue(const UIGMissingFloorNarrativeSubsystem* N)
	{
		if (!Beat(N, TEXT("Arrival.Contract")))
		{
			return Make(TEXT("Hint.Arrival.Contract"), {
				NSLOCTEXT("IGHint", "ArrivalContract1", "책상 위에 계약서를 뒀지. 한번 읽어 보자."),
				NSLOCTEXT("IGHint", "ArrivalContract2", "403호 책상 위 임대차계약서. 그거부터 보자.") });
		}
		if (!Beat(N, TEXT("Arrival.Box.Parcel"))
			|| !Beat(N, TEXT("Arrival.Box.Notebook"))
			|| !Beat(N, TEXT("Arrival.Box.Voicemail")))
		{
			return Make(TEXT("Hint.Arrival.Boxes"), {
				NSLOCTEXT("IGHint", "ArrivalBoxes1", "짐을 아직 다 안 풀었네."),
				NSLOCTEXT("IGHint", "ArrivalBoxes2", "반송된 택배 상자, 조율 공구 상자, 예전 휴대폰 상자. 안 연 걸 열어 보자.") });
		}
		if (!Beat(N, TEXT("Arrival.Store")))
		{
			return Make(TEXT("Hint.Arrival.Store"), {
				NSLOCTEXT("IGHint", "ArrivalStore1", "편의점 사람이면 오빠를 봤을지도 몰라."),
				NSLOCTEXT("IGHint", "ArrivalStore2", "편의점에 가서 물어보자. 1층에서 골목으로 나가면 돼.") });
		}
		const bool b401 = Beat(N, TEXT("Arrival.Unit401"));
		const bool b402 = Beat(N, TEXT("Arrival.Unit402"));
		if (!b401 || !b402)
		{
			return Make(TEXT("Hint.Arrival.Neighbors"), {
				NSLOCTEXT("IGHint", "ArrivalNeighbors1", "같은 층 다른 집도 한번 둘러보자."),
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
				? NSLOCTEXT("IGHint", "NightOne3Sheet", "검침 기록지에 계량기 다섯 개가 다 적혀 있는지 보자.")
				: NSLOCTEXT("IGHint", "NightOne3Breaker", "이름표 없는 차단기를 올려 둔 채로 원판을 한 번 더 보자."));
		return Make(TEXT("Hint.Night1.P1"), {
			NSLOCTEXT("IGHint", "NightOne1", "위에 누가 있으면 전기를 쓰겠지. 1층 계량기함을 봐야겠다."),
			NSLOCTEXT("IGHint", "NightOne2", "계량기가 집마다 하나씩인지 세어 보자. 복도등을 꺼도 도는 게 있으면 다른 데로 전기가 가는 거야."),
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
					? NSLOCTEXT("IGHint", "NightTwo3Agent", "관리실 책상 위 부동산 문자 출력본도 읽어 보자.")
					: NSLOCTEXT("IGHint", "NightTwo3Desk", "관리실 책상을 한 번 더 둘러보자."));
			return Make(TEXT("Hint.Night2.P2"), {
				NSLOCTEXT("IGHint", "NightTwo1", "소음 민원이 들어갔다면 관리실에 기록이 남았겠지."),
				NSLOCTEXT("IGHint", "NightTwo2", "민원 대장이랑 접수철을 같이 보자. 접수철 밑장에 눌린 자국이 남았을지도."),
				Last });
		}
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		for (TActorIterator<AIGMissingFloorLeftoverDirector> It(World); World && It; ++It)
		{
			if (It->IsInnerRoomErrandRunning())
			{
				return Make(TEXT("Hint.Night2.InnerRoom"), {
					NSLOCTEXT("IGHint", "NightTwoInner1", "아저씨가 자리를 비우면 안쪽 방을 봐야 해."),
					NSLOCTEXT("IGHint", "NightTwoInner2", "안쪽 방에 뭐가 있는지 하나씩 살펴보자."),
					NSLOCTEXT("IGHint", "NightTwoInner3", "아저씨가 돌아오기 전에 관리실을 나가자.") });
			}
		}
		for (TActorIterator<AIGMissingFloorNightTwoBeatDirector> It(World); World && It; ++It)
		{
			if (It->GetReturnStage() == EIGNightTwoReturnStage::AwaitingBooth)
			{
				return Make(TEXT("Hint.Night2.Revisit"), {
					NSLOCTEXT("IGHint", "NightTwoRevisit1", "관리실에서 403호까지 돌아오는 길이 남았어."),
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
				NSLOCTEXT("IGHint", "NightThreeReturn2", "아까 벽에 그 박자로 두드리니까 다 조용해졌어. 복도에서도 통할까."),
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
					? NSLOCTEXT("IGHint", "NightThreeRhythm3Notebook", "조율 수첩에서 그 박자를 찾아보자.")
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
				NSLOCTEXT("IGHint", "NightThreeDoor2", "계약서에 옥상 창고 얘기가 있었지. 그 문부터 찾아보자."),
				NSLOCTEXT("IGHint", "NightThreeDoor3", "열쇠 꾸러미의 창고 열쇠로 그 철문을 열자.") });
		}
		return Make(TEXT("Hint.Night3.P3"), {
			NSLOCTEXT("IGHint", "NightThreeWall1", "벽마다 소리가 다른지 들어 보자."),
			N->HasSource(ETruth::SomeoneInTheWall, ESource::PipeAuditionCriterion)
				? NSLOCTEXT("IGHint", "NightThreeWall2Criterion", "수첩에 적혀 있었지. 빈 곳은 낮게 울리고 소리가 오래 간다고.")
				: NSLOCTEXT("IGHint", "NightThreeWall2Notebook", "조율 수첩을 읽어 보자. 오빠도 이 벽을 살펴봤을까."),
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
				NSLOCTEXT("IGHint", "NightFour2", "물탱크 물을 빼면 배관이 시끄럽겠지. 관리실에 점검 메모 같은 게 있을지도."),
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
					NSLOCTEXT("IGHint", "NightFourWall0c", "위쪽 덧판 이음매를 망치로 치자. 테이프 밑에 나사가 있을 거야.") });
			case 1:
				return Make(TEXT("Hint.Night4.Wall1"), {
					NSLOCTEXT("IGHint", "NightFourWall1a", "그게 이음매를 덮고 있어."),
					NSLOCTEXT("IGHint", "NightFourWall1b", "다른 데 틈을 내주면 그리로 옮겨 갈까."),
					NSLOCTEXT("IGHint", "NightFourWall1c", "구석 쪽 작은 덧판을 떼어 내자. 밑에 틈이 있으면 그리로 옮겨 갈 거야.") });
			case 2:
				return Leftover.bFramePlaced
					? Make(TEXT("Hint.Night4.Wall2Braced"), {
						NSLOCTEXT("IGHint", "NightFourWall2Ba", "틀에 걸려서 몸을 못 접네."),
						NSLOCTEXT("IGHint", "NightFourWall2Bb", "이음매 위로 나사가 드러나 있었지."),
						NSLOCTEXT("IGHint", "NightFourWall2Bc", "드러난 덧판 나사를 망치로 쳐 내자.") })
					: Make(TEXT("Hint.Night4.Wall2"), {
						NSLOCTEXT("IGHint", "NightFourWall2a", "천장까지 몸을 폈어. 다시 접히면 벽으로 와."),
						Beat(N, TEXT("Leftover.Seen.BraceFrame"))
							? NSLOCTEXT("IGHint", "NightFourWall2b", "구석에 철골 틀이 있었어.")
							: NSLOCTEXT("IGHint", "NightFourWall2bUnseen", "저 마디를 걸어 둘 만한 게 이 층에 없을까. 둘러보자."),
						NSLOCTEXT("IGHint", "NightFourWall2c", "구석에 기대 둔 철골 틀을 점검구 앞에 세워서 저 무릎 마디를 걸자.") });
			case 3:
				return !Layer(TEXT("Rebrace"))
					? Make(TEXT("Hint.Night4.Wall3"), {
						NSLOCTEXT("IGHint", "NightFourWall3a", "틀이 밀렸어."),
						NSLOCTEXT("IGHint", "NightFourWall3b", "저게 버티면서 틀을 밀고 있어."),
						NSLOCTEXT("IGHint", "NightFourWall3c", "밀린 틀을 다시 세우자.") })
					: Make(TEXT("Hint.Night4.Wall3Inner"), {
						NSLOCTEXT("IGHint", "NightFourWall3Ia", "덧판은 떼어 냈어. 안쪽 판이 남았어."),
						NSLOCTEXT("IGHint", "NightFourWall3Ib", "기둥을 따라 안쪽 판에도 나사가 박혀 있어."),
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
			NSLOCTEXT("IGHint", "NightFourChoice2", "튜닝 해머를 오빠 곁에 두고 갈까. 아니면 녹음을 끄고 조금 더 있을까.") });
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
					NSLOCTEXT("IGHint", "DayOne3", "밤에는 1층 관리실에 가서 소음 민원 기록부터 보자.") })
				: Make(TEXT("Hint.Day1.Missed"), {
					NSLOCTEXT("IGHint", "DayOneMissed1", "어젯밤에 계량기함을 끝까지 못 봤어."),
					NSLOCTEXT("IGHint", "DayOneMissed2", "401호 할머니께 여쭤보자."),
					NSLOCTEXT("IGHint", "DayOneMissed3", "밤이 오면 1층 계량기함부터 보자.") });
		case 2:
			return bNightDone
				? Make(TEXT("Hint.Day2"), {
					NSLOCTEXT("IGHint", "DayTwo1", "401호 할머니께 여쭤볼까."),
					NSLOCTEXT("IGHint", "DayTwo2", "401호 문을 두드려 보자."),
					NSLOCTEXT("IGHint", "DayTwo3", "관리실 책상 위에 열쇠 꾸러미가 있었어. 그걸로 옥상 문을 열 수 있겠다.") })
				: Make(TEXT("Hint.Day2.Missed"), {
					NSLOCTEXT("IGHint", "DayTwoMissed1", "어젯밤에 놓친 게 있어. 다시 가 봐야겠어."),
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
					NSLOCTEXT("IGHint", "DayThreeNotice1", "4층 복도를 한번 둘러보자."),
					NSLOCTEXT("IGHint", "DayThreeNotice2", "엘리베이터 옆에 새로 붙은 종이를 읽어 보자.") });
			}
			return bNightDone
				? Make(TEXT("Hint.Day3"), {
					NSLOCTEXT("IGHint", "DayThree1", "할머니께 말씀드려야겠어."),
					NSLOCTEXT("IGHint", "DayThree2", "401호 문을 두드려 보자."),
					NSLOCTEXT("IGHint", "DayThree3", "볼일을 마치면 403호에서 좀 쉬자. 밤에 다시 올라가야 해.") })
				: Make(TEXT("Hint.Day3.Missed"), {
					NSLOCTEXT("IGHint", "DayThreeMissed1", "어젯밤엔 끝까지 못 갔어."),
					NSLOCTEXT("IGHint", "DayThreeMissed2", "401호 할머니께 여쭤보자."),
					NSLOCTEXT("IGHint", "DayThreeMissed3", "밤이 되면 5층 벽 앞까지 가 보자.") });
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
				NSLOCTEXT("IGHint", "NightOneAfter2", "소리가 계단 위쪽에서 내려왔어. 4층 복도 천장 쪽이겠지."),
				NSLOCTEXT("IGHint", "NightOneAfter3", "4층 복도 천장 밑에서 소리를 들어 보자. 그다음엔 403호 침대에서 쉬어도 돼.") });
		}
		return Make(TEXT("Hint.Night.Rest"), {
			NSLOCTEXT("IGHint", "NightRest1", "오늘은 여기까지 보자."),
			NSLOCTEXT("IGHint", "NightRest2", "더 볼 데가 남았으면 마저 보고, 아니면 이제 쉬자."),
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
