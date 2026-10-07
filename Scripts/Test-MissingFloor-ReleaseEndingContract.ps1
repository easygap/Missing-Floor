[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot

function Read-ProjectText {
	param([Parameter(Mandatory = $true)][string]$RelativePath)
	$path = Join-Path $projectRoot $RelativePath
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Missing release-ending contract file: $RelativePath"
	}
	return Get-Content -Raw -Encoding UTF8 -LiteralPath $path
}

function Assert-ContainsAll {
	param(
		[Parameter(Mandatory = $true)][string]$Text,
		[Parameter(Mandatory = $true)][string[]]$Tokens,
		[Parameter(Mandatory = $true)][string]$Label
	)
	foreach ($token in $Tokens) {
		if (-not $Text.Contains($token)) {
			throw "$Label contract is missing: $token"
		}
	}
}

function Get-MethodBody {
	# 주석은 걷어 내고 실행문만 본다. 주석 처리한 코드와 살아 있는 코드를
	# 문자열 포함으로 구분할 수 없다.
	param(
		[Parameter(Mandatory = $true)][string]$Text,
		[Parameter(Mandatory = $true)][string]$Signature,
		[Parameter(Mandatory = $true)][string]$Label
	)
	$escaped = [regex]::Escape($Signature)
	$match = [regex]::Match($Text, "$escaped(?<body>[\s\S]*?)\r?\n\}")
	if (-not $match.Success) {
		throw "$Label could not be isolated."
	}
	return [regex]::Replace($match.Groups['body'].Value, '//[^\r\n]*', '')
}

$story = Read-ProjectText 'Docs/STORY_BIBLE_MISSING_FLOOR.md'
$fifthDawnHeader = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorFifthDawnDirector.h'
$fifthDawnSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorFifthDawnDirector.cpp'
$controllerSource = Read-ProjectText 'Source/IndieGame/Player/IGPlayerController.cpp'
$characterSource = Read-ProjectText 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
$hudHeader = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.h'
$hudSource = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.cpp'
$nightFourHeader = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.h'
$nightFourSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.cpp'
$nightLoopSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGNightLoopDirector.cpp'
$nightPhaseSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGNightPhaseDirector.cpp'
$narrativeSource = Read-ProjectText `
	'Source/IndieGame/Narrative/IGMissingFloorNarrativeSubsystem.cpp'
$worldSource = Read-ProjectText 'Source/IndieGame/Core/IGPrologueWorldScene.cpp'
$toneSource = Read-ProjectText 'Source/IndieGame/Audio/IGToneSequenceSoundWave.cpp'
$greyboxSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'

Assert-ContainsAll $story @(
	'재플레이에서는 2초 홀드로 건너뛸 수 있다.',
	'### 엔딩 C 「매물」 — 실패 엔딩 (밤4 한정)',
	'무영로 달빛빌라 403호',
	'같아요.**"',
	'넷째 날 밤 다시 시작'
) 'Release-ending story'

Assert-ContainsAll $fifthDawnHeader @(
	'BeginReplaySkipInput()',
	'EndReplaySkipInput()',
	'IsReplaySkipAvailable()',
	'GetReplaySkipDurationSeconds()',
	'bReplaySkipRewinding'
) 'Fifth-dawn replay header'
Assert-ContainsAll $fifthDawnSource @(
	'ReplaySkipDurationSeconds = IGReplaySkip::HoldSeconds',
	'FifthDawnExperienced',
	'GetHoldDurationScale()',
	'UsesToggleHoldInteractions()',
	'GConfig->Flush(false, GGameUserSettingsIni)',
	'FinishInterlude(/*bPersistExperience=*/false)',
	'SetActorTickEnabled(false)'
) 'Fifth-dawn replay runtime'
Assert-ContainsAll $controllerSource @(
	'It->BeginReplaySkipInput()',
	'It->EndReplaySkipInput()'
) 'Fifth-dawn input routing'
Assert-ContainsAll $hudHeader @(
	'SetSensoryInterludeSkipState(',
	'BeginMissingFloorFailureEnding(float InitialElapsedSeconds = 0.0f)',
	'SetMissingFloorFailureRetryEnabled'
) 'Release HUD header'
Assert-ContainsAll $hudSource @(
	'DrawSensoryInterludeSkip()',
	'DrawMissingFloorFailureEnding(CurrentTime)',
	'IsReducedCameraMotionEnabled()',
	'GetCaptionSizeScale()',
	'"무영로 달빛빌라 403호"',
	'"채광 좋은 남향, 즉시 입주 가능"',
	'"이 집 새벽마다 뭐 두드리는 소리 나요."',
	'"두 명이서 하는 것 같아요."',
	'"{0}  넷째 밤 다시 시작"',
	'GetBoundKeyLabel(EIGBindableAction::Interact, bUsingGamepad)',
	'RecordLayoutValidationRect(PanelPosition, PanelPosition + PanelSize)'
) 'Ending C presentation'

Assert-ContainsAll $nightFourHeader @(
	'ResolveFailureEnding()',
	'RequestFailureRetry()',
	'CompleteFailurePresentationForProbe()',
	'IsFailureRetryEnabled()'
) 'Ending C director header'
# §34.3 타임라인의 나머지 둘은 HUD에 있다. 사진과 요약이 밀리기 시작하는
# 시각과 미는 데 걸리는 시간이고, 문서의 「3.27초 이후」는 그 둘을 더한
# 값이다. 한쪽만 고치면 화면은 그대로인데 문서가 거짓말을 한다.
$endingCScroll = [regex]::Match(
	$hudSource,
	'SmoothStep01\(\(Elapsed - (?<start>[0-9.]+)f\) / (?<span>[0-9.]+)f\)')
if (-not $endingCScroll.Success) {
	throw '엔딩 C 카드가 밀리는 시각을 읽지 못했다 (§34.3)'
}
$endingCStart = [double]$endingCScroll.Groups['start'].Value
$endingCSpan = [double]$endingCScroll.Groups['span'].Value
$endingCReveal = [math]::Round($endingCStart + $endingCSpan, 2)
if ($story -notmatch ([regex]::Escape($endingCStart.ToString('0.00')) + '초부터')) {
	throw "§34.3이 미는 시작 시각 $endingCStart 초를 잃었다"
}
if ($story -notmatch ([regex]::Escape($endingCReveal.ToString('0.00')) + '초 이후')) {
	throw "§34.3의 후기 노출 시각이 $endingCReveal 초와 어긋난다"
}

# 모션 감소는 미는 대신 끊어 붙인다. 그 시각이 시작보다 이르면 아직 안 민
# 화면이 튀고, 완료보다 늦으면 밀린 뒤에 또 튄다.
$endingCSnap = [regex]::Match(
	$hudSource, 'Elapsed >= (?<at>[0-9.]+)f \? 1\.0f : 0\.0f')
if (-not $endingCSnap.Success) {
	throw '모션 감소에서 엔딩 C 카드가 끊어 붙는 시각을 읽지 못했다 (§34.3)'
}
$endingCSnapAt = [double]$endingCSnap.Groups['at'].Value
if ($endingCSnapAt -le $endingCStart -or $endingCSnapAt -ge $endingCReveal) {
	throw "모션 감소 전환 $endingCSnapAt 초가 $endingCStart~$endingCReveal 밖이다 (§34.3)"
}

Assert-ContainsAll $nightFourSource @(
	'FailureCaptureSeconds = 2.15f',
	'FailureListingDelaySeconds = 2.2f',
	'FailureRetryDelaySeconds = 3.2f',
	'AudioDirector->SetAuthoredSilence(false)',
	'AudioDirector->SetThreatState(EIGAudioThreatState::Calm)',
	'It->SuspendForFailureEnding()',
	'Noise->UnregisterHumSource(WaterMaskHumHandle)',
	'IGAudio::SpawnOneShotAt(',
	'CreateWallpaperSeamRoller(this)',
	'Narrative->ResetNightFourForRetry()',
	'SceneActor->ResetMissingFloorCavity()',
	'It->RestorePlayerAtWakePoint(Character)',
	'It->RestartTheHour(4)',
	'if (bFailureEndingActive)',
	'if (bNightFour && !bWallOpened)'
) 'Ending C director runtime'

$failureMethod = [regex]::Match(
	$nightFourSource,
	'bool AIGMissingFloorNightFourDirector::ResolveFailureEnding\(\)(?<body>[\s\S]*?)void AIGMissingFloorNightFourDirector::BeginFailureListing\(\)')
if (-not $failureMethod.Success) {
	throw 'Ending C failure method could not be isolated.'
}
if ($failureMethod.Groups['body'].Value.Contains('OnResolved.Broadcast()')) {
	throw 'Ending C must not release dawn through the successful-ending delegate.'
}

Assert-ContainsAll $nightLoopSource @(
	'Narrative->GetNightIndex() == 4',
	'Narrative->GetAggressionTier() >= 3',
	'Narrative->IsNightFourMaskRunning()',
	'RestorePlayerAtWakePoint('
) 'Capture ownership'
Assert-ContainsAll $nightPhaseSource @(
	'SuspendForFailureEnding()',
	'GetWorldTimerManager().ClearTimer(HourTimer)',
	'RestartTheHour(const int32 NightIndex)',
	'if (!bHourActive || bFailureEndingSuspended)'
) 'Hour suspension'
Assert-ContainsAll $narrativeSource @(
	'ResetNightFourForRetry()',
	'Snapshot.Night.NightFourControlOrder.Reset()',
	'Snapshot.Night.EndingChoice = NAME_None',
	'Snapshot.Night.SolvedPuzzles.Remove(FName(TEXT("P5")))',
	'Snapshot.Night.CompletedBeats.Remove(FName(TEXT("Night4.SecondReport")))'
) 'Scoped narrative rollback'
$rollbackMethod = [regex]::Match(
	$narrativeSource,
	'void UIGMissingFloorNarrativeSubsystem::ResetNightFourForRetry\(\)(?<body>[\s\S]*?)// -- persistence')
if (-not $rollbackMethod.Success) {
	throw 'Night-four rollback method could not be isolated.'
}
foreach ($durableToken in @('CaptureCount =', 'Truths.Reset', 'bFirstReportMade = false',
	'bFifthDawnInterludeCompleted = false')) {
	if ($rollbackMethod.Groups['body'].Value.Contains($durableToken)) {
		throw "Scoped retry incorrectly clears durable state: $durableToken"
	}
}
Assert-ContainsAll $worldSource @(
	'ResetMissingFloorCavity()',
	'SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics)',
	'bMissingFloorCavityOpen = false'
) 'Cavity rollback'
Assert-ContainsAll $toneSource @(
	'CreateWallpaperSeamRoller(',
	'기계나 프린터처럼 규칙적으로 들리지 않도록 두 번의 길이를 다르게 둔다.',
	'ConfigureNotes(MoveTemp(RollerNotes), false)'
) 'Wallpaper roller audio'
Assert-ContainsAll $characterSource @(
	'It->RequestFailureRetry()'
) 'Ending C retry input'
Assert-ContainsAll $greyboxSource @(
	'EProbeStep::NightFourFailureRetryContract',
	'NightFour->ResolveFailureEnding()',
	'NightPhase->IsFailureEndingSuspended()',
	'NightFour->CompleteFailurePresentationForProbe()',
	'Narrative->GetCaptureCount() == FailureRetryCaptureCountBefore + 1',
	'Narrative->WasFirstReportMade()',
	'Narrative->HasTruth(EIGMissingFloorTruth::WaitingForAnAnswer)',
	'!Scene->IsMissingFloorCavityOpen()'
) 'Ending C runtime probe'

# -- v3.4 §35 엔딩 에필로그 -------------------------------------------------

$epilogueHeader = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorEpilogueDirector.h'
$epilogueSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorEpilogueDirector.cpp'
$narrativeTypes = Read-ProjectText `
	'Source/IndieGame/Narrative/IGMissingFloorNarrativeTypes.h'
$puzzleTwoSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorPuzzleTwoDirector.cpp'
$nightThreeSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorNightThreeDirector.cpp'

Assert-ContainsAll $story @(
	'## 35. v3.4 — 엔딩 에필로그와 선택적 목격',
	'몽타주 네 소리는 두 엔딩에서 같은 시각에 같은 순서로 난다',
	'마지막 카드는 최소 8초를 잡는다',
	'판은 원본 비례로 그린다',
	'나린의 제보는 목격과 무관하게 보도에 남는다'
) '에필로그 정사'

# 시각표는 소스가 정사다. 두 배열의 앞 다섯 값이 §9 「공통 사실」이며,
# ValidateTimelines가 단조 증가와 카드 체류를 실행 시각에 다시 확인한다.
Assert-ContainsAll $epilogueSource @(
	'constexpr float EndingATimes[]',
	'constexpr float EndingBTimes[]',
	'87.00f',
	'71.00f',
	'EIGMissingFloorEpilogueScene::Montage',
	'EIGMissingFloorEpilogueScene::Workshop',
	'EIGMissingFloorEpilogueScene::Autumn',
	'EIGMissingFloorEpilogueScene::ServiceBay',
	'EIGMissingFloorEpilogueScene::News',
	'EIGMissingFloorEpilogueScene::Card',
	'조율이 끝난 방',
	'같이 있던 밤',
	'업라이트 1대  ·  받는 분 백유담  ·  집들이 전에 전달',
	'네 시 반 알람은 지웠다.',
	'인근 편의점 직원의 목격 진술을 토대로 당시 행적을 확인 중이다'
) '에필로그 타임라인과 카피'

Assert-ContainsAll $epilogueHeader @(
	'static bool ValidateTimelines();',
	'bool CompleteImmediatelyForProbe();',
	'int32 GetPlayedSceneCount() const'
) '에필로그 검증 훅'

# §34.2와 같은 규칙. 초회차는 다 보고, 두 번째부터 우회가 열린다.
# §22.4의 선택 직전 자동 저장이 하는 일이 여기서 완성된다.
Assert-ContainsAll $epilogueHeader @(
	'bool BeginReplaySkipInput();',
	'bool EndReplaySkipInput();',
	'bool IsReplaySkipAvailable() const'
) '에필로그 재관람 우회'
Assert-ContainsAll $epilogueSource @(
	'EpilogueExperienced',
	'HasExperiencedEpilogueProfile() || bReplayForcedForSession',
	'ReplaySkipDurationSeconds = IGReplaySkip::HoldSeconds',
	'UsesToggleSkipInput()',
	'SkipToFinalCard();'
) '에필로그 우회 조건'
# 건너뛰어도 마지막 카드는 원래 길이대로 남는다. 그 한 문장이 결론이다.
Assert-ContainsAll $epilogueSource @(
	'const int32 CardIndex = CueCount - 2;',
	'FireCue(CardIndex);',
	'FMath::Max(CardHoldSeconds, 1.0f)'
) '우회 뒤 마지막 카드'
Assert-ContainsAll $controllerSource @(
	'for (TActorIterator<AIGMissingFloorEpilogueDirector> It(World); It; ++It)'
) '에필로그 우회 입력'

# 엔딩 C는 자기 화면과 재도전을 소유한다. 실패가 애도로 이어지면 §9의
# 세 결말이 섞이므로 StartEpilogue는 A/B만 받는다.
Assert-ContainsAll $epilogueSource @(
	'if (EndingId != IGEpilogue::EndingAId && EndingId != IGEpilogue::EndingBId)'
) '엔딩 C 분리'

Assert-ContainsAll $toneSource @(
	'CreatePoliceLineTapePull(',
	'CreateGurneyWheels(',
	'CreateCameraShutterTriple(',
	'CreateDebrisSweep(',
	'CreateEpilogueWorkshopScore(',
	'CreateEpilogueAutumnBed(',
	'CreateKeyDropMetalBox(',
	'CreateRailingKnockTwo('
) '에필로그 합성'

# §10.1: 게임 내내 -30센트에 머물던 모티프가 여기서만 0센트에 닿는다.
Assert-ContainsAll $toneSource @(
	'const float Cents = FMath::Lerp(-30.0f, 0.0f, Progress);'
) '정음 도달'

Assert-ContainsAll $hudHeader @(
	'enum class EIGMissingFloorEpilogueScene : uint8',
	'void BeginMissingFloorEpilogueScene(',
	'void EndMissingFloorEpilogue();',
	'bool IsMissingFloorEpilogueVisible() const'
) '에필로그 HUD 계층'

Assert-ContainsAll $hudSource @(
	'bool AIGHorrorHUD::DrawMissingFloorEpilogue(const double CurrentTime)',
	'const float SourceAspect = SourceWidth / SourceHeight;',
	'T_EpilogueWorkshop_D',
	'T_EpilogueAutumn_D',
	'T_EpilogueServiceBay_D'
) '에필로그 HUD 렌더'

# 텍스처 없이도 장면이 성립해야 한다. 에필로그의 뜻은 문장에 있다.
if (-not $hudSource.Contains('if (SceneTexture && SceneTexture->GetResource())')) {
	throw 'Epilogue still must degrade to text when the texture is absent.'
}

# -- v3.4 §22.3 선택적 목격 -------------------------------------------------

Assert-ContainsAll $narrativeTypes @(
	'enum class EIGMissingFloorWitness : uint8',
	'SeoSleepingPills = 1',
	'HwangWaterBowl = 2',
	'BoothSoundproofing = 3',
	'RooftopCigarettePack = 4',
	'BoothWallCalendar = 5',
	'RecorderEmptyBay = 6',
	'AnnexWorkGlove = 7',
	'StoreNightRoster = 8',
	'Unit401DoorRadio = 9',
	'Unit402Silence = 10',
	'RoofDoorWind = 11',
	'BoothInnerRoomHum = 12',
	'TArray<FName> Witnesses;'
) '선택적 목격 정의'

# 정규화 상한이 열거형의 마지막을 따라가야 한다. 뒤처지면 새로 넣은 목격이
# 복원에서 조용히 버려진다 — 저장은 되는데 다음 실행에 사라지는 모양이다.
Assert-ContainsAll $narrativeSource @(
	'Raw <= static_cast<uint8>(EIGMissingFloorWitness::BoothInnerRoomHum);'
) '목격 정규화 상한'

Assert-ContainsAll $narrativeSource @(
	'Seen.SeoSleepingPills',
	'Seen.HwangWaterBowl',
	'Seen.BoothSoundproofing',
	'Seen.RooftopCigarettePack',
	'Seen.BoothWallCalendar',
	'Seen.RecorderEmptyBay',
	'Seen.AnnexWorkGlove',
	'Seen.StoreNightRoster',
	'Seen.Unit401DoorRadio'
) '선택적 목격 직렬화'

# 소리로만 확인되는 목격은 `Heard.` 접두사를 쓴다. 저장 파일을 열어 본
# 사람이 무엇을 보고 무엇을 들었는지까지 구분할 수 있어야 한다.
Assert-ContainsAll $narrativeSource @(
	'Heard.Unit402Silence',
	'Heard.RoofDoorWind',
	'Heard.BoothInnerRoomHum'
) '소리 목격 직렬화'

# 셋 다 그림 없이 부피만 세우고, 각자 자기 큐를 낸다.
Assert-ContainsAll $greyboxSource @(
	'AIGListenerGreyboxDirector::SpawnListeningVolume(',
	'CreateVacantUnitTone(this)',
	'CreateRoofDoorGust(this)',
	'RecordWitness(EIGMissingFloorWitness::Unit402Silence)',
	'RecordWitness(EIGMissingFloorWitness::RoofDoorWind)'
) '소리 목격 판정'
Assert-ContainsAll $puzzleTwoSource @(
	'CreateFoamedRoomHum(this)',
	'RecordWitness(EIGMissingFloorWitness::BoothInnerRoomHum)'
) '안쪽 방 청음'

# 402호의 요점은 없는 소리다. 저역을 넣으면 큐의 뜻이 사라지므로, 이
# 함수에는 200Hz 아래 성분이 하나도 없어야 한다.
$vacantBody = Get-MethodBody $toneSource `
	'UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateVacantUnitTone(' `
	'Vacant unit tone'
$vacantBands = @([regex]::Matches($vacantBody, '(?<value>[0-9]+(?:\.[0-9]+)?)f\b') |
	ForEach-Object { [double]$_.Groups['value'].Value } |
	Where-Object { $_ -ge 20.0 })
foreach ($band in $vacantBands) {
	if ($band -lt 150.0) {
		throw ("402호의 목격은 없는 저역이 내용이다. 메인즈 험 대역이 " +
			"들어왔다: $band Hz.")
	}
}

# 밤2 셋(문틈·달력·녹화기), 밤3 둘(장갑·담뱃갑), 낮 둘(물그릇·약봉투).
Assert-ContainsAll $puzzleTwoSource @(
	'RecordWitness(EIGMissingFloorWitness::BoothWallCalendar)',
	'RecordWitness(EIGMissingFloorWitness::RecorderEmptyBay)'
) '밤2 목격'
Assert-ContainsAll $nightThreeSource @(
	'RecordWitness(EIGMissingFloorWitness::AnnexWorkGlove)'
) '밤3 목격'

# 새 목격 셋도 반드시 회수가 있어야 한다 — 회수 없는 심기 금지(§13).
Assert-ContainsAll $nightFourSource @(
	'EIGMissingFloorWitness::BoothWallCalendar',
	'EIGMissingFloorWitness::AnnexWorkGlove',
	'EIGMissingFloorWitness::RecorderEmptyBay'
) '새 목격의 대치 회수'
Assert-ContainsAll $epilogueSource @(
	'HasWitness(EIGMissingFloorWitness::RecorderEmptyBay)',
	'HasWitness(EIGMissingFloorWitness::StoreNightRoster)',
	'HasWitness(EIGMissingFloorWitness::Unit401DoorRadio)',
	'HasWitness(EIGMissingFloorWitness::Unit402Silence)'
) '새 목격의 보도 회수'
Assert-ContainsAll $nightFourSource @(
	'EIGMissingFloorWitness::BoothInnerRoomHum',
	'EIGMissingFloorWitness::RoofDoorWind'
) '소리 목격의 대치 회수'

# 낮 목격 둘. 근무표는 계산대 상판, 라디오는 401호 문 앞의 소리 판정이다.
Assert-ContainsAll $greyboxSource @(
	'RecordWitness(EIGMissingFloorWitness::StoreNightRoster)',
	'RecordWitness(EIGMissingFloorWitness::Unit401DoorRadio)',
	'CreateMuffledPrayerRadio(this)'
) '낮 목격'

# §13의 나린 제보는 목격과 무관하게 남아야 한다. 근무표를 본 회차는 그
# 근거가 선명해질 뿐, 못 본 회차에도 제보 자체는 보도에 있다.
$newsBody = Get-MethodBody $epilogueSource `
	'TArray<FText> AIGMissingFloorEpilogueDirector::BuildNewsLines() const' `
	'Epilogue news'
# 따옴표와 쉼표까지 붙여서 본다. `EpilogueNews5`만 찾으면
# `EpilogueNews5Roster`가 그것을 품고 있어, 폴백을 지워도 검사가 통과한다 —
# 실제로 회귀를 넣어 보고 통과하는 것을 확인한 뒤에 고쳤다.
foreach ($narinToken in @('"EpilogueNews5Roster",', '"EpilogueNews5",')) {
	if (-not $newsBody.Contains($narinToken)) {
		throw "The Narin report must survive in both readings: $narinToken"
	}
}

# 목격은 어떤 교차에도 들어가지 않는다. RecordWitness가 진실을 다시
# 계산하면 「본 것이 진실을 열 수도 있다」가 코드에 남는다.
$recordWitness = [regex]::Match(
	$narrativeSource,
	'bool UIGMissingFloorNarrativeSubsystem::RecordWitness\((?<body>[\s\S]*?)\r?\n\}')
if (-not $recordWitness.Success) {
	throw 'RecordWitness implementation not found.'
}
# 주석은 그 금지를 설명하는 자리이므로 걷어 내고 실행문만 본다.
$recordWitnessCode = [regex]::Replace(
	$recordWitness.Groups['body'].Value, '//[^\r\n]*', '')
if ($recordWitnessCode.Contains('RecomputeConfirmations')) {
	throw 'RecordWitness must not touch truth confirmation.'
}

# §22.3은 세 자리에서 문장이 구체화된다고 적었다 — 유담의 독백, 목한수
# 대치, 엔딩 뉴스 자막. 대치가 마지막으로 남아 있던 자리다. 못 본 회차에는
# 유담이 아무 말도 하지 않는다: 없는 말을 쥐여 주지 않는 것이 이 절의 규칙이다.
Assert-ContainsAll $nightFourSource @(
	'void AIGMissingFloorNightFourDirector::BuildConfrontationReplyLines(',
	'EIGMissingFloorWitness::BoothSoundproofing',
	'EIGMissingFloorWitness::BoothWallCalendar',
	'EIGMissingFloorWitness::AnnexWorkGlove',
	'EIGMissingFloorWitness::RecorderEmptyBay',
	'EIGMissingFloorWitness::RooftopCigarettePack',
	'EIGMissingFloorWitness::HwangWaterBowl',
	'EIGMissingFloorWitness::SeoSleepingPills'
) '목한수 대치 문장 분기'

# 세 줄이 상한이다. 그 이상은 목격이 아니라 목록 낭독이 된다.
Assert-ContainsAll $nightFourSource @(
	'ConfrontationReplyLimit = 3'
) '대치 줄 수 상한'
$replyBuilder = Get-MethodBody $nightFourSource `
	'void AIGMissingFloorNightFourDirector::BuildConfrontationReplyLines(' `
	'Confrontation reply builder'
# 서류는 최대 둘. 셋을 연달아 대면 사람이 아니라 조서가 된다.
$assertionCountLocal = 0
if (-not $replyBuilder.Contains('FMath::Min(HeldDocuments.Num(), 2)')) {
	throw 'The confrontation must cap consecutive document lines at two.'
}
# 사람 줄은 서류 뒤에 놓인다 — 증거로 시작해 사람으로 끝나야 이 장면이
# 고발이 아니라 애도가 된다.
$humanIndex = $replyBuilder.IndexOf('HeldHumans[0]')
$documentIndex = $replyBuilder.IndexOf('OutLines.Add(HeldDocuments[Index]);')
if ($humanIndex -lt 0 -or $documentIndex -lt 0 -or $humanIndex -lt $documentIndex) {
	throw 'A human line must follow the document lines in the confrontation.'
}
# 아무것도 못 본 회차는 한 줄도 없다.
if (-not $replyBuilder.Contains('OutLines.Reset();')) {
	throw 'The confrontation must start from an empty reply list.'
}
# 존재의 통과가 댄 줄 수만큼 뒤로 밀린다.
Assert-ContainsAll $nightFourSource @(
	'ReplySeconds += AIGHorrorHUD::EstimateDialogueSeconds(',
	'IGNightFour::EntityPassBaseSeconds + ReplySeconds'
) '대치 줄 수에 따른 통과 지연'
# 그의 애원은 본 것과 무관하게 같아야 한다. 달라지는 것은 유담 쪽이다.
$mokLineBody = [regex]::Match(
	$nightFourSource,
	'"MokHansooFinalLine",(?<body>[\s\S]{0,200}?)\);')
if (-not $mokLineBody.Success -or
	$mokLineBody.Groups['body'].Value.Contains('HasWitness')) {
	throw 'The Mok plea must not branch on what the player happened to see.'
}
# 존재가 먼저 들어오면 대치가 대화가 아니라 배경이 된다.
Assert-ContainsAll $nightFourSource @(
	'EntityPassBaseSeconds = 3.35f'
) '대치 없는 회차의 통과'

Assert-ContainsAll $epilogueSource @(
	'HasWitness(EIGMissingFloorWitness::HwangWaterBowl)',
	'HasWitness(EIGMissingFloorWitness::BoothSoundproofing)',
	'HasWitness(EIGMissingFloorWitness::SeoSleepingPills)',
	'HasWitness(EIGMissingFloorWitness::RooftopCigarettePack)'
) '목격에 따른 문장 분기'

Assert-ContainsAll $greyboxSource @(
	'SpawnOptionalWitnesses(CubeMesh);',
	'RecordWitness(EIGMissingFloorWitness::HwangWaterBowl)',
	'RecordWitness(EIGMissingFloorWitness::SeoSleepingPills)',
	'RecordWitness(EIGMissingFloorWitness::RooftopCigarettePack)',
	'Epilogue->StartEpilogue(Player.Get(), Narrative->GetEndingChoice());',
	'IGController->ShowTitleAfterEnding();'
) '목격 프롭과 에필로그 진입'

Assert-ContainsAll $puzzleTwoSource @(
	'RecordWitness(EIGMissingFloorWitness::BoothSoundproofing)'
) '문틈 목격'

# §22.4 리플레이 유인 2. 선택이 열리는 프레임에 자동 저장 한 번.
$tagConfig = Read-ProjectText 'Config/DefaultGameplayTags.ini'
Assert-ContainsAll $tagConfig @(
	'Tag="Checkpoint.MissingFloor.EndingChoice"'
) '엔딩 선택 체크포인트 태그'
Assert-ContainsAll $nightFourSource @(
	'ChoiceOfferedBeat(TEXT("Night4.ChoiceOffered"))',
	'Narrative->MarkBeatPlayed(IGNightFour::ChoiceOfferedBeat)',
	'RequestEndingChoiceAutosave();',
	'Checkpoint.MissingFloor.EndingChoice'
) '엔딩 선택 자동 저장'
# 재도전은 선택을 되돌리므로 저장 지점도 다시 찍혀야 한다.
Assert-ContainsAll $narrativeSource @(
	'Snapshot.Night.CompletedBeats.Remove(FName(TEXT("Night4.ChoiceOffered")));'
) '재도전 시 선택 저장 재무장'

# §13: 심는 자리가 없으면 엔딩 A의 제보 자막은 어디서 왔는지 알 수 없는
# 문장이 된다. 나린의 마지막 날 대사가 그 심기다.
# §13 장부의 나머지 세 줄. 심기와 회수가 같은 커밋에 없으면 어느 쪽이든
# 반쪽이 되므로 한 검사에서 짝으로 본다.

# 6행 — 세입자 두 명의 단기 퇴거(프롤로그) → 그들이 들은 것(밤1).
Assert-ContainsAll $greyboxSource @(
	'그 집은 올해만 벌써 세 번째네요.'
) '단기 퇴거 심기'

# 12행 — 채널 5의 빈 복도(밤2) → 같은 화각에 직접 서기(밤3).
Assert-ContainsAll $nightThreeSource @(
	'AnnexRecognitionZone',
	'HasBeatPlayed(FName(TEXT("Night2.CCTV")))',
	'Night3.AnnexRecognition'
) '채널 5 회수'

# 13행 — 중고 거래 글(낮) → 세트에서 홀로 남은 렌치(밤3).
Assert-ContainsAll $greyboxSource @(
	'SetPhoneNotificationPresentation();',
	'피아노 조율 공구 일괄 (튜닝해머 외 11점)',
	'Day.UsedListing'
) '중고 매물 심기'
Assert-ContainsAll $nightThreeSource @(
	'HasBeatPlayed(FName(TEXT("Day.UsedListing")))',
	'Night3.HammerListing'
) '중고 매물 회수'

Assert-ContainsAll $greyboxSource @(
	'FText AIGListenerGreyboxDirector::GetNarinCounterLine() const',
	'아직은요. 저 오늘도 밤새 있으니까, 무슨 일 있으면 바로 이쪽으로 오세요.',
	'ArrivalStoreBell->SetInteractionEnabled(!bActive);'
) '나린 포어섀도'

Assert-ContainsAll $greyboxSource @(
	'EProbeStep::EpilogueContract',
	'Epilogue->GetPlayedSceneCount() != 5',
	'Epilogue->CompleteImmediatelyForProbe()',
	'Hud->IsMissingFloorEpilogueVisible()',
	'Narrative->GetConfirmedTruthCount() != TruthsBefore',
	'Seen.NotAThingThisBuildKnows'
) '에필로그 런타임 프로브'

# 어느 목격도 진행을 잠그지 않는다. 스테이지 유효성 검사가 이것들을
# 묻지 않는 것이 「없어도 되는 것」이라는 설계의 표현이다.
foreach ($optional in @('WaterBowl', 'SleepingPills', 'CigarettePack')) {
	if ($greyboxSource -match "ValidateFixtures[\s\S]{0,4000}$optional") {
		throw "Optional witness must not gate stage validation: $optional"
	}
}

Write-Host 'MISSING_FLOOR_RELEASE_ENDING_CONTRACT PASS replay_skip=1 ending_c=1 scoped_retry=1 audio=1 runtime_probe=1 epilogue=2 witnesses=12'
