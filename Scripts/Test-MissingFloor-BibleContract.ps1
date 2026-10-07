[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 바이블 전반의 계약.
#
# §18·§27의 입력·조작감은 `Test-MissingFloor-InputBindingContract.ps1`이
# 맡는다. 여기는 그 밖의 절 — 화면과 난이도, 재미의 구조, 몰입 계약,
# 즉시 차단 표, 제품 감사, 진실 게이트, 소음 모델, 구현 지도.
#
# 한동안 이 검사들이 전부 입력 계약 파일에 들어 있었다. §24를 찾는 사람이
# 「InputBinding」이라는 이름의 파일을 열 리가 없다 — §14가 이름이 틀리면
# 지도가 미로가 된다고 적어 두었는데, 계약 쪽에서 같은 일을 하고 있었다.

$projectRoot = Split-Path -Parent $PSScriptRoot
$assertionCount = 0

function Read-ProjectText {
	param([Parameter(Mandatory = $true)][string]$RelativePath)
	$path = Join-Path $projectRoot $RelativePath
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Missing bible contract file: $RelativePath"
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
		$script:assertionCount++
		if (-not $Text.Contains($token)) {
			throw "$Label is missing: $token"
		}
	}
}

$story = Read-ProjectText 'Docs/STORY_BIBLE_MISSING_FLOOR.md'
$controllerSource = Read-ProjectText 'Source/IndieGame/Player/IGPlayerController.cpp'
$characterSource = Read-ProjectText 'Source/IndieGame/Player/IGPlayerCharacter.cpp'

# --- §19 UI·UX 계약 ----------------------------------------------------------
#
# 화면 계층부터 합격식까지 열한 절이다. 여기서는 숫자와 「없어야 하는 것」을
# 본다. §19.6.1·19.6.2는 각자 자기 자리에서 이미 잠겨 있다.

$settingsLayoutSource = Read-ProjectText 'Source/IndieGame/Player/IGSettingsMenuLayout.h'
$hudSource = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.cpp'

# 19.1 — 밤에는 기록을 열 수 없다. 이 게임 UX의 중심 결정이다.
$assertionCount++
if ($story -notmatch '\*\*열 수 없음\*\*') {
	throw 'The §19.1 night journal lock was removed.'
}
$assertionCount++
if (-not $controllerSource.Contains('지금은 그럴 때가 아니야')) {
	throw 'The §19.1 refusal line is missing.'
}

# 19.2 — 기록은 Tab 홀드로 연다. 스치는 손에 전체 화면이 열리면 안 된다.
$journalRow = [regex]::Match($story, '열기 `Tab` 홀드 (?<seconds>[0-9.]+)s')
$assertionCount++
if (-not $journalRow.Success) {
	throw 'The §19.2 journal hold row could not be read.'
}
$journalDeclared = [regex]::Match(
	$controllerSource,
	'const double JournalHoldSeconds = (?<value>[0-9.]+) \*')
$assertionCount++
if (-not $journalDeclared.Success) {
	throw 'JournalHoldSeconds could not be read.'
}
$assertionCount++
if ([double]$journalDeclared.Groups['value'].Value -ne [double]$journalRow.Groups['seconds'].Value) {
	throw (
		'JournalHoldSeconds is {0} but §19.2 says {1}.' -f
			$journalDeclared.Groups['value'].Value, $journalRow.Groups['seconds'].Value)
}
# 필터·검색·정답 하이라이트는 없다. 빈칸을 보여 주면 세계가 체크리스트가 된다.
$assertionCount++
if ($story -notmatch '필터·검색·정답 하이라이트·미확인 항목 회색 슬롯 없음') {
	throw 'The §19.2 no-checklist rule was removed.'
}
foreach ($banned in @('JournalFilter', 'JournalSearch', 'DrawJournalHighlight')) {
	$assertionCount++
	if ($hudSource.Contains($banned)) {
		throw "The journal must not become a checklist (§19.2): $banned"
	}
}

# 19.4 — 힌트는 유담의 속말이고, 첫 단계는 볼 곳만 말한다. 낮에는 여전히
# 할머니를 먼저 가리킨다.
$assertionCount++
$hintSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Narrative/IGMissingFloorHints.cpp')
if (-not $hintSource.Contains('401호 할머니께 여쭤보자') -or
	-not $controllerSource.Contains('IGMissingFloorHints::Resolve(this)')) {
	throw 'The §19.4 hint ladder is missing.'
}

# 19.5 — 기상 연출이 짧아지는 것 자체가 정보다.
$wakeRow = [regex]::Match(
	$story,
	'기상 연출 길이: 1회차 (?<first>[0-9.]+)s, 2회차 (?<second>[0-9.]+)s, 3~4회차 (?<third>[0-9.]+)s, 5회차부터 (?<fifth>[0-9.]+)s')
$assertionCount++
if (-not $wakeRow.Success) {
	throw 'The §19.5 wake ladder could not be read.'
}
$loopSource = Read-ProjectText 'Source/IndieGame/Entity/IGNightLoopDirector.cpp'
$wakeBody = [regex]::Match(
	$loopSource,
	'float AIGNightLoopDirector::GetWakeFadeInSeconds\(\) const(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $wakeBody.Success) {
	throw 'GetWakeFadeInSeconds could not be isolated.'
}
$wakeSteps = @()
foreach ($step in [regex]::Matches(
	$wakeBody.Groups['body'].Value, 'return (?<value>[0-9.]+)f;')) {
	$wakeSteps += [double]$step.Groups['value'].Value
}
$expectedWake = @(
	[double]$wakeRow.Groups['first'].Value,
	[double]$wakeRow.Groups['second'].Value,
	[double]$wakeRow.Groups['third'].Value,
	[double]$wakeRow.Groups['fifth'].Value)
$assertionCount++
if ($wakeSteps.Count -ne $expectedWake.Count) {
	throw (
		'The wake ladder has {0} steps but §19.5 lists {1}.' -f
			$wakeSteps.Count, $expectedWake.Count)
}
for ($index = 0; $index -lt $expectedWake.Count; $index++) {
	$assertionCount++
	if ($wakeSteps[$index] -ne $expectedWake[$index]) {
		throw (
			'Wake step {0} is {1}s but §19.5 says {2}s.' -f
				$index, $wakeSteps[$index], $expectedWake[$index])
	}
}
# 짧아지기만 해야 한다. 중간이 길어지면 「그가 성급해졌다」가 뒤집힌다.
for ($index = 1; $index -lt $wakeSteps.Count; $index++) {
	$assertionCount++
	if ($wakeSteps[$index] -ge $wakeSteps[$index - 1]) {
		throw 'The wake ladder must only ever get shorter (§19.5).'
	}
}
# 다섯 번째 포획에만 문 아래 메모가 온다.
$mercyRow = [regex]::Match($story, '(?<count>[0-9]+)회 연속 포획에 한해')
$assertionCount++
if (-not $mercyRow.Success) {
	throw 'The §19.5 mercy-note threshold could not be read.'
}
$mercyDeclared = [regex]::Match(
	$loopSource, 'constexpr int32 MercyNoteCaptureThreshold = (?<value>[0-9]+);')
$assertionCount++
if (-not $mercyDeclared.Success) {
	throw 'MercyNoteCaptureThreshold could not be read.'
}
$assertionCount++
if ([int]$mercyDeclared.Groups['value'].Value -ne [int]$mercyRow.Groups['count'].Value) {
	throw (
		'MercyNoteCaptureThreshold is {0} but §19.5 says {1}.' -f
			$mercyDeclared.Groups['value'].Value, $mercyRow.Groups['count'].Value)
}
# 게임오버 화면·재시도 버튼·사망 카운터는 없다.
foreach ($banned in @('GameOver', 'RetryButton', 'DeathCount')) {
	$assertionCount++
	if ($hudSource.Contains($banned)) {
		throw "A reset is not a failure screen (§19.5): $banned"
	}
}

# 19.7 — 화면 설정은 명시적으로 적용한 뒤 10초 확인한다.
$confirmRow = [regex]::Match($story, '(?<seconds>[0-9]+)초\s*\r?\n?\s*확인하며, 응답이 없으면')
$assertionCount++
if (-not $confirmRow.Success) {
	throw 'The §19.7 confirmation window could not be read.'
}
$assertionCount++
if ($controllerSource -notmatch
	('DisplayConfirmationSecondsRemaining = {0};' -f $confirmRow.Groups['seconds'].Value)) {
	throw ('The display confirmation window must be {0}s (§19.7).' -f $confirmRow.Groups['seconds'].Value)
}
# 확인 창은 「이 설정 유지」에 커서를 둔다. 행을 하나 끼웠더니 조작 행을
# 가리키고 있었다 — 그래서 번호 대신 이름으로 적는다.
$assertionCount++
if (-not $controllerSource.Contains(
	'DisplaySettingsSelection = IGSettingsMenuLayout::ApplyOrKeep;')) {
	throw 'The confirmation window must land on the keep row (§19.7).'
}
$assertionCount++
if ($controllerSource -match 'DisplaySettingsSelection == [0-9]') {
	throw 'Display rows must be compared by name, not by number (§19.7).'
}
$assertionCount++
if ($settingsLayoutSource -notmatch 'enum EDisplayRow : int32') {
	throw 'The display row names are missing (§19.7).'
}
$assertionCount++
if ($settingsLayoutSource -notmatch 'BackOrRevert \+ 1 == DisplayRowCount') {
	throw 'The display row names must stay tied to the row count (§19.7).'
}
# 720p에서도 포인터 행이 44px 아래로 내려가지 않는다.
$rowHeightRow = [regex]::Match(
	$story, '포인터 행 높이는 (?<pixels>[0-9]+)px 미만으로 줄이지 않는다')
$assertionCount++
if (-not $rowHeightRow.Success) {
	throw 'The §19.7 minimum row height could not be read.'
}
foreach ($field in @('CategoryRowHeight', 'OptionRowHeight')) {
	$declared = [regex]::Match(
		$settingsLayoutSource,
		('Result.{0} = FMath::Max\((?<floor>[0-9.]+)f,' -f $field))
	$assertionCount++
	if (-not $declared.Success) {
		throw ('The settings row floor is missing: {0}' -f $field)
	}
	$assertionCount++
	if ([double]$declared.Groups['floor'].Value -lt [double]$rowHeightRow.Groups['pixels'].Value) {
		throw (
			'{0} can fall to {1}px but §19.7 says at least {2}px.' -f
				$field, $declared.Groups['floor'].Value, $rowHeightRow.Groups['pixels'].Value)
	}
}

# 19.8 — 파문 링은 동작 감소에서도 끄지 않는다. 연출이 아니라 정보다.
$assertionCount++
if ($story -notmatch '소음 파문 링은 동작 감소에서도 \*\*끄지 않는다\*\*') {
	throw 'The §19.8 ripple rule was removed.'
}

# --- §20 난이도 설계 ----------------------------------------------------------
#
# §20.2 튜닝 테이블은 자기 계약이 따로 본다. 여기서는 안전망 셋과 난이도
# 네 모드를 본다. 둘 다 문서가 숫자를 적어 둔 자리다.

$mercyHeader = Read-ProjectText 'Source/IndieGame/Entity/IGMissingFloorMercyDirector.h'
$mercySource = Read-ProjectText 'Source/IndieGame/Entity/IGMissingFloorMercyDirector.cpp'
$tuningSource = Read-ProjectText 'Source/IndieGame/Entity/IGListenerTuning.cpp'

# 20.3-1 — 2회 연속 리셋에 환경 힌트 하나.
$resetRow = [regex]::Match(
	$story, '\*\*관찰 재료 증가\*\*[^\r\n]*?(?<count>[0-9]+)회 연속 리셋 시 환경 힌트 (?<hints>[0-9]+)개')
$assertionCount++
if (-not $resetRow.Success) {
	throw 'The §20.3-1 reset-hint row could not be read.'
}
$resetDeclared = [regex]::Match(
	$mercyHeader,
	'static constexpr int32 ResetsForEnvironmentHint = (?<value>[0-9]+);')
$assertionCount++
if (-not $resetDeclared.Success) {
	throw 'ResetsForEnvironmentHint could not be read.'
}
$assertionCount++
if ([int]$resetDeclared.Groups['value'].Value -ne [int]$resetRow.Groups['count'].Value) {
	throw (
		'ResetsForEnvironmentHint is {0} but §20.3 says {1}.' -f
			$resetDeclared.Groups['value'].Value, $resetRow.Groups['count'].Value)
}
# 새 출처를 얻으면 세는 것이 처음으로 돌아가야 한다. 안 그러면 잘 하고 있는
# 플레이어에게도 언젠가 힌트가 켜진다. 같은 줄이 여러 곳에 있으므로 새 출처를
# 확인하는 자리에서 함께 도는지를 본다.
$assertionCount++
if ($mercySource -notmatch
	'SourceCount != LastSourceCount\)[\s\S]{0,400}?LastSourceCount = SourceCount;\s*\r?\n\s*StuckSeconds = 0\.0f;\s*\r?\n\s*ResetsSinceNewSource = 0;') {
	throw 'Learning something must stand both nets down together (§20.3-1).'
}

# 20.3-2 — 새 출처 없이 90초가 지나면 세계가 먼저 움직인다.
$stuckRow = [regex]::Match(
	$story, '새 출처 없이 (?<seconds>[0-9]+)초가 지나면')
$assertionCount++
if (-not $stuckRow.Success) {
	throw 'The §20.3-2 ninety-second row could not be read.'
}
$stuckDeclared = [regex]::Match(
	$mercyHeader,
	'static constexpr float StuckResponseSeconds = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $stuckDeclared.Success) {
	throw 'StuckResponseSeconds could not be read.'
}
$assertionCount++
if ([double]$stuckDeclared.Groups['value'].Value -ne [double]$stuckRow.Groups['seconds'].Value) {
	throw (
		'StuckResponseSeconds is {0} but §20.3 says {1}.' -f
			$stuckDeclared.Groups['value'].Value, $stuckRow.Groups['seconds'].Value)
}
# 메뉴를 열어 둔 것은 막힌 것이 아니다. 시계가 거기서 멈춰야 한다(§19.7).
$assertionCount++
if (-not $mercySource.Contains('World->IsPaused()')) {
	throw 'The stuck clock must stop while the game is paused (§20.3-2).'
}

# 안전망은 볼 곳을 줄 뿐 답을 말하지 않는다.
$assertionCount++
if ($story -notmatch '\*\*막힌 플레이어에게 주는 것은\s*\r?\n?\s*답이 아니라 볼 곳이다\.\*\*') {
	throw 'The §20.3 no-answers rule was removed.'
}

# --- §20.4 난이도 네 모드 -----------------------------------------------------
#
# 난이도 이름과 바꾸는 위치를 처음 보는 사람도 알 수 있어야 한다.
$assertionCount++
if ($story -notmatch '쉬움·보통·어려움·추격 없음으로 표시한다') {
	throw '난이도 네 가지의 표시 이름이 설계서와 다릅니다.'
}
foreach ($mode in @('쉬움', '보통', '어려움', '추격 없음')) {
	$assertionCount++
	if (-not $tuningSource.Contains($mode) -and -not $mercyHeader.Contains($mode)) {
		$tuningHeader = Read-ProjectText 'Source/IndieGame/Entity/IGListenerTuning.h'
		if (-not $tuningHeader.Contains($mode)) {
			throw "The §20.4 difficulty mode is missing: $mode"
		}
	}
}

# 쉬움의 세 배율. 문서가 표에 적어 둔 값 그대로다.
$quietRow = [regex]::Match(
	$story,
	'청취 반경 ×(?<hearing>[0-9.]+), CHASE 속도 ×(?<chase>[0-9.]+), WAITING 시간 ×(?<wait>[0-9.]+)')
$assertionCount++
if (-not $quietRow.Success) {
	throw 'The §20.4 quiet-night row could not be read.'
}
$quietBody = [regex]::Match(
	$tuningSource,
	'case EIGNightDifficulty::Quiet:(?<body>[\s\S]*?)break;')
$assertionCount++
if (-not $quietBody.Success) {
	throw 'The quiet-night branch could not be isolated.'
}
foreach ($pair in @(
	@{ Line = ('Tuning.HearingSensitivity *= {0}f;' -f $quietRow.Groups['hearing'].Value); Name = '청취 반경' },
	@{ Line = ('Tuning.ChaseSpeed *= {0}f;' -f $quietRow.Groups['chase'].Value); Name = 'CHASE 속도' },
	@{ Line = ('Tuning.WaitScale = {0}f;' -f $quietRow.Groups['wait'].Value); Name = 'WAITING 시간' })) {
	$assertionCount++
	if (-not $quietBody.Groups['body'].Value.Contains($pair.Line)) {
		throw ('The quiet night must follow §20.4: {0} ({1})' -f $pair.Name, $pair.Line)
	}
}

# 어려움의 세 축.
$hastyRow = [regex]::Match(
	$story,
	'티어 초기값 (?<tier>[0-9]+), 히트맵 가중 \+(?<heatmap>[0-9.]+), LISTENING −(?<listen>[0-9]+)s')
$assertionCount++
if (-not $hastyRow.Success) {
	throw 'The §20.4 hasty-night row could not be read.'
}
$hastyBody = [regex]::Match(
	$tuningSource,
	'case EIGNightDifficulty::Hasty:(?<body>[\s\S]*?)break;')
$assertionCount++
if (-not $hastyBody.Success) {
	throw 'The hasty-night branch could not be isolated.'
}
$assertionCount++
if (-not $hastyBody.Groups['body'].Value.Contains(
	('Tuning.HeatmapWeight + {0}f' -f $hastyRow.Groups['heatmap'].Value))) {
	throw 'The hasty night must raise the heatmap weight by the §20.4 amount.'
}
$assertionCount++
if (-not $hastyBody.Groups['body'].Value.Contains(
	('NightListenWindow[Night] - {0}.0f' -f $hastyRow.Groups['listen'].Value))) {
	throw 'The hasty night must shorten LISTENING by the §20.4 amount.'
}

# 추격 없음: 포획도 추격도 없다. 매복도 함께 꺼진다 — 잡을 수 없는
# 매복은 압박이 아니라 연출이다.
$listenBody = [regex]::Match(
	$tuningSource,
	'case EIGNightDifficulty::ListenOnly:(?<body>[\s\S]*?)break;')
$assertionCount++
if (-not $listenBody.Success) {
	throw 'The listen-only branch could not be isolated.'
}
foreach ($off in @(
	'Tuning.bChaseEnabled = false;',
	'Tuning.bCaptureEnabled = false;')) {
	$assertionCount++
	if (-not $listenBody.Groups['body'].Value.Contains($off)) {
		throw "The listen-only night must not chase or capture (§20.4): $off"
	}
}
# 포획이 없으니 엔딩 C의 보통 경로가 영영 안 열린다. 대체 경로가 있어야
# §20.5의 「모든 모드에서 엔딩 셋 도달」이 성립한다.
$nightFourHeader = Read-ProjectText 'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.h'
$assertionCount++
if (-not $nightFourHeader.Contains('bool ResolveDawnFailureEnding();')) {
	throw 'The listen-only night needs its substitute route to ending C (§20.4).'
}
$assertionCount++
if ($story -notmatch '엔딩 C의 도달 조건을 \*\*밤4의 05:30\s*\r?\n?\s*벽 미개방\*\*으로 대체한다') {
	throw 'The §20.4 substitute ending-C condition was removed.'
}
$assertionCount++
if ($story -notmatch '「추격 없음」으로 진실 10개 전부 확정 가능, 엔딩 A·B·C 전부 도달 가능') {
	throw 'The §20.5 all-modes-reachable criterion was removed.'
}

# 마이크는 난이도가 아니다. 밸런스 기준은 항상 꺼진 상태다.
$assertionCount++
if ($story -notmatch '\*\*마이크 모드는 난이도가 아니다\*\*') {
	throw 'The §20.4 microphone rule was removed.'
}
$accessibilityHeaderForMic = Read-ProjectText 'Source/IndieGame/Accessibility/IGAccessibilitySubsystem.h'
$assertionCount++
if ($accessibilityHeaderForMic -notmatch 'bool bMicrophoneNoiseEnabled = false;') {
	throw 'The microphone must stay off by default; it is the balance baseline (§20.4).'
}

# --- 합격식 현황표 ------------------------------------------------------------
#
# 바이블의 네 합격식은 스무 줄이고, MISSING_FLOOR_ACCEPTANCE.md가 그 스무 줄을
# 표로 다시 센다. 두 자리가 어긋나면 「자동으로 못 보는 줄」이 조용히 사라진다 —
# 그게 이 문서가 막으려는 유일한 사고다.

$acceptanceDoc = Read-ProjectText 'Docs/MISSING_FLOOR_ACCEPTANCE.md'

# §20.5의 「난이도 변경이 진행 중 세이브를 깨지 않는다」는 값을 대조해서
# 지킬 수 있는 줄이 아니다. 지켜지는 이유는 **두 파일이 서로를 모른다**는
# 것이다 — 난이도는 사용자 설정 ini에 있고 세이브는 스냅샷이며, 어느 쪽도
# 상대를 읽지 않는다. 그 분리가 살아 있는지를 본다.
# §5.7 마이크 소음의 상한. §20.5는 「마이크 ON/OFF 간 소음 판정 차이가
# §20.2를 벗어나지 않는다」고 요구하는데, 실측으로만 볼 수 있는 것은 문턱이
# 방 소리를 제대로 무시하는지까지다. **상한이 달리기보다 낮다**는 것은
# 숫자로 볼 수 있고, 그게 이 줄이 지키려는 균형이다.
$characterForMic = Read-ProjectText 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
$micBand = [regex]::Match(
	$characterForMic,
	'const float Loudness = FMath::Lerp\(\s*(?<floor>[0-9.]+)f,\s*(?<ceiling>[0-9.]+)f,')
$assertionCount++
if (-not $micBand.Success) {
	throw 'The microphone loudness band could not be read (§5.7).'
}
$micFloor = [double]$micBand.Groups['floor'].Value
$micCeiling = [double]$micBand.Groups['ceiling'].Value

$sprintLoudness = [regex]::Match(
	$characterForMic, 'constexpr float SprintFootstepLoudness = (?<value>[0-9.]+)f;')
$walkFloorLoudness = [regex]::Match(
	$characterForMic, 'constexpr float MinimumFootstepLoudness = (?<value>[0-9.]+)f;')
$walkCeilingLoudness = [regex]::Match(
	$characterForMic, 'constexpr float MaximumFootstepLoudness = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $sprintLoudness.Success -or -not $walkFloorLoudness.Success `
	-or -not $walkCeilingLoudness.Success) {
	throw 'The footstep loudness band could not be read (§5.1).'
}

# 마이크를 켜서 달리기보다 시끄러워질 수 있으면, 마이크 없는 플레이어가
# 불리해진다 — §5.7이 그 반대를 약속한다.
$assertionCount++
if ($micCeiling -ge [double]$sprintLoudness.Groups['value'].Value) {
	throw (
		'마이크 상한 {0}이 달리기 {1} 이상이다. 마이크를 켜면 더 시끄러워진다 (§5.7).' -f
			$micCeiling, $sprintLoudness.Groups['value'].Value)
}
# 바닥이 걷기 밴드 안에 있어야 「켜 두면 늘 들킨다」가 되지 않는다.
$assertionCount++
if ($micFloor -lt [double]$walkFloorLoudness.Groups['value'].Value `
	-or $micFloor -gt [double]$walkCeilingLoudness.Groups['value'].Value) {
	throw (
		'마이크 바닥 {0}이 걷기 밴드({1}~{2}) 밖이다 (§5.7).' -f
			$micFloor,
			$walkFloorLoudness.Groups['value'].Value,
			$walkCeilingLoudness.Groups['value'].Value)
}
# 쿨다운이 없으면 폴링 주기마다 보고해 발소리보다 촘촘해진다.
$assertionCount++
if ($characterForMic -notmatch
	'MicrophoneReportCooldown = IGPlayerNoise::MicrophoneReportCooldownSeconds;') {
	throw 'The microphone report cooldown was removed (§5.7).'
}
$assertionCount++
if ($story -notmatch '\*\*비명이 상한인\s*\r?\n?\s*0\.35\*\*') {
	throw 'The §5.7 microphone ceiling drifted from the code.'
}
$assertionCount++
if ($story -notmatch '\*\*상한이 달리기\(0\.50\)보다 낮은 것이 이 기능의 균형점이다\.\*\*') {
	throw 'The §5.7 reason the microphone ceiling sits below sprinting was removed.'
}

$tuningForDifficulty = Read-ProjectText 'Source/IndieGame/Entity/IGListenerTuning.cpp'
$narrativeTypesForDifficulty = Read-ProjectText `
	'Source/IndieGame/Narrative/IGMissingFloorNarrativeTypes.h'
$saveGameForDifficulty = Read-ProjectText 'Source/IndieGame/Save/IGSaveGame.h'
$entityForDifficulty = Read-ProjectText 'Source/IndieGame/Entity/IGListenerEntity.cpp'

# 1. 난이도는 사용자 설정 ini에만 적힌다.
$saveDifficulty = [regex]::Match(
	$tuningForDifficulty,
	'void SavePersistedDifficulty\([\s\S]*?\r?\n\t\}')
$assertionCount++
if (-not $saveDifficulty.Success) {
	throw 'SavePersistedDifficulty could not be read.'
}
$assertionCount++
if ($saveDifficulty.Value -notmatch 'GGameUserSettingsIni') {
	throw 'The night difficulty stopped living in the user settings file (§20.5).'
}

# 2. 스냅샷과 세이브 구조체에는 난이도가 없다. 있으면 난이도를 바꾸는 순간
#    저장된 값과 고른 값이 갈라지고, 어느 쪽이 맞는지 아무도 모른다.
foreach ($persisted in @(
	@{ Name = '서사 스냅샷'; Text = $narrativeTypesForDifficulty },
	@{ Name = '세이브 게임'; Text = $saveGameForDifficulty })) {
	$assertionCount++
	if ($persisted.Text -match 'Difficulty') {
		throw (
			'{0}이 난이도를 들고 있다. 난이도는 세이브 밖에 있어야 한다 (§20.5).' -f
				$persisted.Name)
	}
}

# 3. 존재는 난이도를 저장하지 않고 매번 다시 읽는다. 캐시해 두면 진행 중
#    바꾼 값이 다음 밤까지 안 먹는다.
$assertionCount++
if ($entityForDifficulty -notmatch
	'Difficulty = IGListenerTuning::ResolveActiveDifficulty\(\);') {
	throw 'The listener stopped resolving the night difficulty at spawn (§20.5).'
}

# 4. 명령줄 덮어쓰기는 되쓰지 않는다. 검증 한 번이 플레이어가 고른 값을
#    바꿔 버리면 그게 곧 「세이브를 깨는」 일이다.
$resolveDifficulty = [regex]::Match(
	$tuningForDifficulty,
	'EIGNightDifficulty ResolveActiveDifficulty\([\s\S]*?\r?\n\t\}')
$assertionCount++
if (-not $resolveDifficulty.Success) {
	throw 'ResolveActiveDifficulty could not be read.'
}
$assertionCount++
if ($resolveDifficulty.Value -match 'SavePersistedDifficulty') {
	throw 'A validation run would write back the difficulty it overrode (§20.5).'
}
$assertionCount++
if ($acceptanceDoc -notmatch '난이도는 사용자 설정에, 게임 진행은 저장 파일에 따로 기록한다') {
	throw 'The acceptance sheet lost the reason this line is a contract (§20.5).'
}
$bibleLineCount = 0
foreach ($section in @('18.7', '19.9', '20.5', '21.5')) {
	$body = [regex]::Match(
		$story,
		'### ' + [regex]::Escape($section) +
			'[^\r\n]*\r?\n(?<body>[\s\S]*?)(?=\r?\n###|\r?\n---|\r?\n## )')
	$assertionCount++
	if (-not $body.Success) {
		throw "The §$section acceptance section could not be read."
	}
	# 최상위 항목만 센다. 들여쓴 줄은 같은 줄의 부연이다.
	$items = [regex]::Matches($body.Groups['body'].Value, '(?m)^- ')
	$assertionCount++
	if ($items.Count -lt 1) {
		throw "The §$section acceptance section lost every line."
	}
	$bibleLineCount += $items.Count
}

# 표의 행을 센다. 각 절의 표에서 머리글과 정렬 줄을 뺀 나머지다.
$docLineCount = 0
foreach ($section in @('18.7', '19.9', '20.5', '21.5')) {
	$table = [regex]::Match(
		$acceptanceDoc,
		'## §' + [regex]::Escape($section) +
			'[^\r\n]*\r?\n(?<body>[\s\S]*?)(?=\r?\n## |\r?\n---)')
	$assertionCount++
	if (-not $table.Success) {
		throw "The acceptance sheet is missing §$section."
	}
	$rows = [regex]::Matches(
		$table.Groups['body'].Value, '(?m)^\| (?!확인 항목 \|)(?!---)[^|]+\|')
	$docLineCount += $rows.Count
}
$assertionCount++
if ($docLineCount -ne $bibleLineCount) {
	throw (
		'The acceptance sheet lists {0} lines but the bible has {1}.' -f
			$docLineCount, $bibleLineCount)
}

# 표에서 「사람」을 하나씩 계약으로 바꿔 놓으면, 아직 못 본 것을 봤다고 적은
# 것이 된다. 표가 스스로 밝힌 수와 실제 행 수를 맞대 본다.
$humanRows = ([regex]::Matches(
	$acceptanceDoc, '(?m)^\|[^|]+\|[^|]*\*\*사람\*\*[^|]*\|')).Count
$assertionCount++
if ($humanRows -lt 1) {
	throw 'The acceptance sheet must keep naming the lines a person has to check.'
}
$statedHuman = [regex]::Match($acceptanceDoc, '\*\*(?<count>\d+)개 항목\*\*은 사람이 플레이하거나 장비로 측정해야 한다')
$assertionCount++
if (-not $statedHuman.Success) {
	throw 'The acceptance sheet no longer says how many lines need a person.'
}
$assertionCount++
if ([int]$statedHuman.Groups['count'].Value -lt 1) {
	throw 'The acceptance sheet states a count this check cannot read.'
}
$assertionCount++
if ($humanRows -ne [int]$statedHuman.Groups['count'].Value) {
	throw (
		'The sheet says {0} lines need a person but {1} rows are marked.' -f
			$statedHuman.Groups['count'].Value, $humanRows)
}
$assertionCount++
if ($acceptanceDoc -notmatch '직접 확인하지 않은 항목은 통과로 표시하지 않는다') {
	throw 'The acceptance sheet lost the rule that keeps its own rows honest.'
}

# 감사를 이름으로 걸어 두었으니 그 감사가 실제로 있어야 한다.
$assertionCount++
if (-not (Test-Path (Join-Path $projectRoot 'Scripts/audit_hold_timing.py'))) {
	throw 'The acceptance sheet names an audit that does not exist.'
}

# §19.9는 4K까지 이름을 댄다. 레이아웃 검증이 거기까지 가는지 본다.
$assertionCount++
if ($story -notmatch '720p·1080p·1440p·4K에서 파문 링과 브래킷이 안전 영역 안에 들어온다') {
	throw 'The §19.9 resolution criterion was removed.'
}
$accessibilityContract = Read-ProjectText 'Scripts/Test-MissingFloor-AccessibilityContract.ps1'
$assertionCount++
if ($accessibilityContract -notmatch 'Width = 3840\.0; Height = 2160\.0') {
	throw 'The layout check must reach 4K, which §19.9 names.'
}

# --- §22 재미의 구조 ----------------------------------------------------------
#
# 이 절은 대부분 설계 철학이지만, 「만들지 않기로 한 것」이 구체적이다.
# 없는 것으로 지켜지는 규칙은 생기는 순간 조용히 깨진다.

$sceneSource = Read-ProjectText 'Source/IndieGame/Core/IGPrologueWorldScene.cpp'
$nightFourSource = Read-ProjectText 'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.cpp'
$frontendLayout = Read-ProjectText 'Source/IndieGame/Player/IGFrontendMenuLayout.h'

# 22.3 — 수집률 UI·업적 팝업·완료 퍼센트 없음.
$assertionCount++
if ($story -notmatch '수집률 UI·업적 팝업·완료 퍼센트 없음') {
	throw 'The §22.3 no-collection-UI rule was removed.'
}
foreach ($banned in @(
	'CollectionRate', 'AchievementPopup', 'CompletionPercent',
	'DrawCollectionProgress')) {
	$assertionCount++
	if ($hudSource.Contains($banned)) {
		throw "Discoveries are witnessed, not collected (§22.3): $banned"
	}
}
# 무엇을 놓쳤는지 알려 주지 않는다. 회색 슬롯이 곧 체크리스트다.
$assertionCount++
if ($story -notmatch '무엇을 놓쳤는지 알려 주지 않는다') {
	throw 'The §22.3 no-missing-list rule was removed.'
}

# 발견의 보상은 문장이다. 문서가 인용한 줄이 실제로 나가는 줄이어야 한다.
$rewardRow = [regex]::Match(
	$story,
	'약봉투를 봤다면 밤4의\s*\r?\n?\s*대치에서 유담이 한 줄을 더 (?:말한다|묻는다): "(?<line>[^"]+)"')
$assertionCount++
if (-not $rewardRow.Success) {
	throw 'The §22.3 reward line could not be read.'
}
# 문서는 줄을 접어 적고 코드는 문자열을 이어 붙인다. 공백을 지우고 견준다.
$documentedLine = $rewardRow.Groups['line'].Value -replace '\s', ''
$pillsBody = [regex]::Match(
	$nightFourSource,
	'EIGMissingFloorWitness::SeoSleepingPills,(?<body>[\s\S]{0,600}?)\r?\n\t\t\},')
$assertionCount++
if (-not $pillsBody.Success) {
	throw 'The sleeping-pills reply could not be isolated.'
}
# 앞의 둘은 NSLOCTEXT의 네임스페이스와 키다. 대사는 그 뒤부터다.
$codeLine = (
	[regex]::Matches($pillsBody.Groups['body'].Value, '"(?<part>[^"]*)"') |
		Select-Object -Skip 2 |
		ForEach-Object { $_.Groups['part'].Value }) -join ''
$codeLine = $codeLine -replace '\s', ''
$assertionCount++
if ($codeLine -ne $documentedLine) {
	throw (
		'The §22.3 reward line differs from what night four says: doc "{0}" code "{1}"' -f
			$documentedLine, $codeLine)
}
# 그 줄이 목격에 걸려 있어야 보상이지, 늘 나오면 보상이 아니다.
$assertionCount++
if (-not $nightFourSource.Contains('BuildConfrontationReplyLines')) {
	throw 'The extra line must hang off the witness list (§22.3).'
}

# 403호 정사와 404 이스터에그. 문패 셋과 벽의 메모.
# 재질 목록에만 이름이 있는 것과 문에 실제로 붙는 것은 다르다. 이웃 둘은
# 배치 배열에서, 403은 자기 자리를 그리는 호출에서 확인한다.
$neighbourPlates = [regex]::Match(
	$sceneSource,
	'const TCHAR\* NeighborPlates\[\] = \{(?<body>[^}]*)\};')
$assertionCount++
if (-not $neighbourPlates.Success) {
	throw 'The §22.3 neighbour nameplates are not placed.'
}
foreach ($plate in @('M_Plate401', 'M_Plate402')) {
	$assertionCount++
	if (-not $neighbourPlates.Groups['body'].Value.Contains($plate)) {
		throw "The §22.3 neighbour nameplate is missing: $plate"
	}
}
$assertionCount++
if ($sceneSource -notmatch 'TexMat\(TEXT\("M_Plate403"\)') {
	throw 'The §22.3 403 nameplate is not drawn on its own door.'
}
# 403호 다음 벽에는 아무것도 없다. 404를 농담으로도 공포 기호로도 쓰지 않는다(§0).
$assertionCount++
if ($sceneSource.Contains('404NotFound')) {
	throw 'The landing past 403 must stay bare (§22.3).'
}

# 22.4 — 「밤 5」는 엔딩 B를 본 세이브에만, 타이틀에서만 보인다.
$assertionCount++
if ($story -notmatch '「밤 5」 슬롯\(엔딩 B 한정, §9\)') {
	throw 'The §22.4 night-five gate was removed.'
}
$assertionCount++
if ($frontendLayout -notmatch '밤 5는 타이틀에서만, 그리고 엔딩 B를 본 세이브가 있을 때만 보인다') {
	throw 'The night-five gate must stay written where the menu is laid out (§22.4).'
}
# 선택 직전 자동 저장이 있어야 양쪽을 보는 비용이 낮다.
$assertionCount++
if ($story -notmatch '선택 직전 자동 저장이 있어') {
	throw 'The §22.4 pre-choice autosave promise was removed.'
}
# 정의만 남고 호출이 사라지면 이름은 그대로인데 저장은 안 된다. 부르는
# 자리를 따로 센다 — 정의 한 번, 호출 한 번 이상.
$autosaveUses = ([regex]::Matches(
	$nightFourSource, 'RequestEndingChoiceAutosave\(\)')).Count
$assertionCount++
if ($autosaveUses -lt 2) {
	throw 'The ending choice must actually call its autosave, not just declare it (§22.4).'
}

# 2회차 전용 컷·숨겨진 층·진 엔딩은 만들지 않는다.
$assertionCount++
if ($story -notmatch '2회차 전용 컷·숨겨진 층·진 엔딩은 만들지 않는다') {
	throw 'The §22.4 no-new-game-plus rule was removed.'
}
foreach ($banned in @('NewGamePlus', 'TrueEnding', 'HiddenFloor')) {
	$assertionCount++
	if ($nightFourSource.Contains($banned) -or $hudSource.Contains($banned)) {
		throw "This story ends once (§22.4): $banned"
	}
}

# --- §23 몰입 계약 ------------------------------------------------------------
#
# 열 줄짜리 금지 표다. 「몰입을 만드는 것은 추가가 아니라 제거」이므로 여기
# 걸리는 것은 전부 **없어야** 지켜진다. 없는 것은 grep으로 지킬 수 없으니
# 생기면 거절하는 쪽으로 건다.

$immersionRows = @()
$immersionTable = [regex]::Match(
	$story,
	'## 23\. 몰입 계약[^\r\n]*\r?\n(?<body>[\s\S]*?)\r?\n\r?\n몰입을 만드는 것은')
$assertionCount++
if (-not $immersionTable.Success) {
	throw 'The §23 prohibition table could not be read.'
}
foreach ($row in [regex]::Matches(
	$immersionTable.Groups['body'].Value, '(?m)^\| (?<ban>[^|]+?) \| (?<why>[^|]+?) \|\s*$')) {
	$ban = $row.Groups['ban'].Value.Trim()
	if ($ban -eq '금지' -or $ban -match '^-+$') {
		continue
	}
	$immersionRows += $ban
}
$assertionCount++
if ($immersionRows.Count -ne 10) {
	throw (
		'The §23 table has {0} rows; it is supposed to have ten.' -f
			$immersionRows.Count)
}
# 표의 각 줄이 실제로 그 문장인지도 본다. 하나를 조용히 갈아 끼우면 나머지
# 검사들이 무엇을 지키는지 알 수 없게 된다.
foreach ($expected in @(
	'포획 리셋·낮밤 전환의 로딩 화면',
	'튜토리얼 팝업·키 안내 오버레이',
	'퍼센트·게이지·카운터',
	'업적 토스트',
	'사망 카운터·재시도 버튼',
	'자동 저장 아이콘 상시 표시',
	'밤 중 기록 보기',
	'밤 중 `F9` 즉시 로드',
	'존재를 설명하는 컷신',
	'상표·실존 인물·실존 사건')) {
	$assertionCount++
	if ($immersionRows -notcontains $expected) {
		throw "The §23 table lost a row: $expected"
	}
}
$assertionCount++
if ($story -notmatch '몰입을 만드는 것은 추가가 아니라 \*\*제거\*\*다') {
	throw 'The §23 first principle was removed.'
}

# 아래 이름 목록은 의도를 적어 둔 것이지 관문이 아니다. 진행률을 넣는 사람은
# `DrawProgressGauge`라고 쓰지 않는다 — 다른 이름을 짓거나 그냥 글자에 %를
# 적는다. 관문은 화면에 나가는 글자여야 한다.
#
# HUD가 그리는 문구를 전부 훑어 퍼센트·진행률 분수가 섞였는지 본다. §24의
# 17번(밤 구간에 게이지·퍼센트 노출)이 여기서 닫힌다.
$hudTexts = [regex]::Matches($hudSource, 'NSLOCTEXT\(\s*"(?<ns>[^"]*)"\s*,\s*"[^"]*"\s*,\s*"(?<body>[^"]*)"')
$assertionCount++
if ($hudTexts.Count -lt 100) {
	throw (
		'HUD 문구를 {0}개만 읽었다. 퍼센트를 볼 수 없다 (§23).' -f $hudTexts.Count)
}
foreach ($hudText in $hudTexts) {
	# 조사 기록은 세계 안 문서를 옮겨 적는다. 7/26 같은 날짜는 진행률이 아니다.
	if ($hudText.Groups['ns'].Value -eq 'IGJournal') {
		continue
	}
	$drawn = $hudText.Groups['body'].Value
	$assertionCount++
	if ($drawn -match '%' -or $drawn -match '퍼센트') {
		throw (
			'§23은 퍼센트를 금지하는데 화면 문구에 들어갔다: {0}' -f $drawn)
	}
	$assertionCount++
	if ($drawn -match '[0-9]+\s*/\s*[0-9]+') {
		throw (
			'§23은 진행률 카운터를 금지하는데 화면 문구가 분수를 쓴다: {0}' -f $drawn)
	}
}

# §19.2와 §19.5도 같은 자로 잰다. 저널에 검색을 붙이는 사람은 `JournalSearch`
# 라고 이름 짓지 않고, 실패 화면을 만드는 사람은 `RetryButton`이라고 짓지
# 않는다. 화면에 나가는 말을 본다.
#
# 「사망」과 「실패」는 일부러 뺐다. 이 게임에는 시신 발견과 신고가 있고 저장
# 실패 피드백도 있다 — 서사가 정당하게 쓸 말을 금지하면 다음 사람이 검사를
# 피하려고 문장을 비튼다.
#
# 「다시 시작」도 안 막는다. 엔딩 C가 「넷째 날 밤 다시 시작」라고
# 말하는 것과 「재시도」 버튼을 다는 것의 차이가 §19.5의 전부다.
$forbiddenWords = @(
	@{ Word = '검색'; Section = '19.2'; Why = '저널이 체크리스트가 된다' },
	@{ Word = '필터'; Section = '19.2'; Why = '저널이 체크리스트가 된다' },
	@{ Word = '미확인'; Section = '19.2'; Why = '빈칸을 보여 주면 세계가 목록이 된다' },
	@{ Word = '게임 오버'; Section = '19.5'; Why = '리셋은 실패가 아니다' },
	@{ Word = '게임오버'; Section = '19.5'; Why = '리셋은 실패가 아니다' },
	@{ Word = '재시도'; Section = '19.5'; Why = '리셋은 실패가 아니다' },
	@{ Word = '다시 시도'; Section = '19.5'; Why = '리셋은 실패가 아니다' })
foreach ($hudText in $hudTexts) {
	$drawn = $hudText.Groups['body'].Value
	foreach ($forbidden in $forbiddenWords) {
		$assertionCount++
		if ($drawn.Contains($forbidden.Word)) {
			throw (
				'§{0}이 금지하는 말이 화면에 나간다 — {1}: 「{2}」 ({3})' -f
					$forbidden.Section, $forbidden.Why, $forbidden.Word, $drawn)
		}
	}
}

# 화면에 생기면 안 되는 것들. 이름이 하나라도 나타나면 표가 거짓이 된다.
foreach ($banned in @(
	@{ Symbol = 'DrawLoadingScreen'; Row = '로딩 화면' },
	@{ Symbol = 'ShowLoadingScreen'; Row = '로딩 화면' },
	@{ Symbol = 'DrawTutorialPopup'; Row = '튜토리얼 팝업' },
	@{ Symbol = 'DrawKeyPromptOverlay'; Row = '키 안내 오버레이' },
	@{ Symbol = 'DrawProgressGauge'; Row = '퍼센트·게이지·카운터' },
	@{ Symbol = 'DrawAchievementToast'; Row = '업적 토스트' },
	@{ Symbol = 'DrawDeathCounter'; Row = '사망 카운터' },
	@{ Symbol = 'DrawEntityCutscene'; Row = '존재를 설명하는 컷신' })) {
	$assertionCount++
	if ($hudSource.Contains($banned.Symbol)) {
		throw (
			'§23 forbids this and the HUD grew it: {0} ({1})' -f
				$banned.Symbol, $banned.Row)
	}
}

# 자동 저장 아이콘은 상시 표시가 금지된 것이지 표시 자체가 금지된 것은
# 아니다. §19.7이 점 하나 0.8초를 약속했고, 그게 없으면 수동 슬롯도 없는
# 게임에서 저장됐는지 물어볼 데가 없다.
$dotRow = [regex]::Match(
	$story, '저장\s*\r?\n?\s*표시는 화면 구석 점 하나 (?<seconds>[0-9.]+)초')
$assertionCount++
if (-not $dotRow.Success) {
	throw 'The §19.7 save dot could not be read.'
}
$dotDeclared = [regex]::Match(
	$hudSource, 'constexpr double SaveIndicatorSeconds = (?<value>[0-9.]+);')
$assertionCount++
if (-not $dotDeclared.Success) {
	throw 'SaveIndicatorSeconds could not be read.'
}
$assertionCount++
if ([double]$dotDeclared.Groups['value'].Value -ne [double]$dotRow.Groups['seconds'].Value) {
	throw (
		'The save dot lasts {0}s but §19.7 says {1}s.' -f
			$dotDeclared.Groups['value'].Value, $dotRow.Groups['seconds'].Value)
}
# 저장이 성공했을 때만 남는다. 실패는 이미 문장으로 말하고 있다.
$saveHandler = [regex]::Match(
	$controllerSource,
	'void AIGPlayerController::HandleSaveCompleted\((?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $saveHandler.Success) {
	throw 'HandleSaveCompleted could not be isolated.'
}
$assertionCount++
if (-not $saveHandler.Groups['body'].Value.Contains('ShowSaveIndicator()')) {
	throw 'A successful save must leave its mark (§19.7).'
}
# 사라져야 상시 표시가 아니다.
$dotDraw = [regex]::Match(
	$hudSource,
	'void AIGHorrorHUD::DrawSaveIndicator\(const double CurrentTime\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $dotDraw.Success) {
	throw 'DrawSaveIndicator could not be isolated.'
}
$assertionCount++
if (-not $dotDraw.Groups['body'].Value.Contains('CurrentTime >= SaveIndicatorEndTime')) {
	throw 'The save dot must go away; §23 forbids an always-on icon.'
}

# --- §24 즉시 차단 22개 -------------------------------------------------------
#
# 이 절이 스스로 경고한 것이 있다. 「선언과 구현이 갈라져도 아무도 모르는
# 상태가 결함 자체보다 위험하다」. 실제로 잠금 표가 스물둘 중 열여섯만 세고
# 있었고, 빠진 여섯 중 넷은 이미 계약이 보고 있었는데 표만 몰랐다.

# 차단 항목이 스물둘인가.
$blockerList = [regex]::Match(
	$story,
	'### 즉시 차단 22개\r?\n(?<body>[\s\S]*?)\r?\n### 즉시 차단 항목이 잠긴 자리')
$assertionCount++
if (-not $blockerList.Success) {
	throw 'The §24 blocker list could not be read.'
}
$blockerNumbers = @()
foreach ($item in [regex]::Matches(
	$blockerList.Groups['body'].Value, '(?m)^(?<number>[0-9]+)\. ')) {
	$blockerNumbers += [int]$item.Groups['number'].Value
}
$assertionCount++
if ($blockerNumbers.Count -ne 22) {
	throw (
		'The §24 list has {0} blockers; the heading says 22.' -f
			$blockerNumbers.Count)
}
# 번호가 1부터 22까지 빠짐없이 이어지는가. 하나를 지우면 뒤가 당겨져
# 세는 것만으로는 못 잡는다.
for ($index = 0; $index -lt 22; $index++) {
	$assertionCount++
	if ($blockerNumbers[$index] -ne ($index + 1)) {
		throw (
			'The §24 blocker numbering breaks at {0}.' -f $blockerNumbers[$index])
	}
}

# 잠금 표가 스물둘을 빠짐없이 덮는가.
$lockTable = [regex]::Match(
	$story,
	'### 즉시 차단 항목이 잠긴 자리\r?\n(?<body>[\s\S]*?)\r?\n\r?\n스물둘이 전부')
$assertionCount++
if (-not $lockTable.Success) {
	throw 'The §24 lock table could not be read.'
}
$covered = @{}
$namedScripts = @{}
$scriptsFor = @{}
$reasonFor = @{}
foreach ($row in [regex]::Matches(
	$lockTable.Groups['body'].Value, '(?m)^\| (?<items>[^|]+?) \| (?<where>[^|]+?) \|\s*$')) {
	$items = $row.Groups['items'].Value.Trim()
	if ($items -eq '항목' -or $items -match '^-+$') {
		continue
	}
	$where = $row.Groups['where'].Value.Trim()
	$rowScripts = [regex]::Matches($where, '`(?<name>Test-[A-Za-z0-9-]+\.ps1)`')
	foreach ($number in [regex]::Matches($items, '[0-9]+')) {
		$key = [int]$number.Value
		$covered[$key] = $true
		$scriptsFor[$key] = $rowScripts.Count
		if ($rowScripts.Count -eq 0) {
			$reasonFor[$key] = $where
		}
	}
	foreach ($script in $rowScripts) {
		$namedScripts[$script.Groups['name'].Value] = $true
	}
}
for ($number = 1; $number -le 22; $number++) {
	$assertionCount++
	if (-not $covered.ContainsKey($number)) {
		throw "The §24 lock table does not say what watches blocker $number."
	}
}

# 표가 이름을 댄 스크립트는 실제로 있어야 한다. 없는 이름을 적어 두면
# 「잠겼다」가 「잠긴 줄 알았다」가 된다.
foreach ($name in $namedScripts.Keys) {
	$assertionCount++
	if (-not (Test-Path (Join-Path $projectRoot (Join-Path 'Scripts' $name)))) {
		throw "The §24 lock table names a script that does not exist: $name"
	}
}
$assertionCount++
if ($namedScripts.Count -lt 8) {
	throw 'The §24 lock table stopped naming the scripts that watch it.'
}

# 여기가 뚫려 있었다. 표가 스물둘을 세는지도 보고 이름 댄 스크립트가 실재하는지도
# 봤지만, **한 줄이 스크립트 이름을 잃고 산문으로 바뀌는 것**은 못 잡았다. 한
# 줄의 스크립트 이름을 「정적 검사 대상 아님」으로 바꿔도 줄 수는 스물둘 그대로고
# 남은 이름들이 다 실재하니 통과했다.
#
# 세는 것과 번호마다 보는 것은 다르다. 스크립트 없이 닫히는 번호는 정해져 있고,
# 여기에 하나가 더 들어오려면 이 목록을 같이 고쳐야 한다. 9번(아이템 연속성)은
# 옛 이야기의 계약이 잠근다고 적혀 있었지만 그 계약은 Missing Floor의 물건을 보지
# 않았다. 옛 코드를 지우면서 실측 증거 쪽으로 옮겼다.
$manualBlockers = @(1, 2, 3, 4, 5, 9, 12, 13)
$manualReasons = @{
	'기하 감사와 사람 검수' = $true
	'정적 검사 대상 아님. 실측 증거로만 닫힌다' = $true
}
$assertionCount++
$manualActual = (@($reasonFor.Keys | Sort-Object) -join ' ')
if ($manualActual -ne ($manualBlockers -join ' ')) {
	throw (
		'The §24 blockers that close without a contract changed: {0} (expected {1}).' -f
			$manualActual, ($manualBlockers -join ' '))
}
# 빈 칸이나 새 변명으로 계약을 피해 가지 못하게 이유까지 못 박는다.
foreach ($number in $manualBlockers) {
	$assertionCount++
	if (-not $manualReasons.ContainsKey($reasonFor[$number])) {
		throw (
			'Blocker {0} closes without a contract for an unrecorded reason: {1}' -f
				$number, $reasonFor[$number])
	}
}
# 나머지 열넷은 반드시 스크립트를 대야 한다.
for ($number = 1; $number -le 22; $number++) {
	if ($manualBlockers -contains $number) {
		continue
	}
	$assertionCount++
	if ($scriptsFor[$number] -lt 1) {
		throw "The §24 lock table stopped naming what watches blocker $number."
	}
}

# 20번은 이 세션에 잠근 자리다. 표가 그 사실을 잊지 않게 한다.
$assertionCount++
if ($story -notmatch '`Test-MissingFloor-MixAndMovementContract\.ps1` — 더킹에 ENTITY 분기가 없음') {
	throw 'Blocker 20 lost the contract that watches it.'
}
$assertionCount++
if ($story -notmatch '선언과 구현이 갈라져도 아무도 모르는 상태가 결함 자체보다\s*\r?\n?위험하다') {
	throw 'The §24 warning this table exists to answer was removed.'
}

# 「일부러 깨서 잡히는 것까지 확인한 뒤에 넣는다」가 이 저장소의 방식이다.
$assertionCount++
if ($story -notmatch '\*\*일부러 깨서 잡히는 것까지 확인한 뒤\*\* 넣는다') {
	throw 'The §24 break-it-first rule was removed.'
}

# --- §26 제품 감사 계약 -------------------------------------------------------
#
# 26.2 첫 12분, 26.3 오디오 제작, 26.4 UI·조작. 26.5 성능 예산은 실측이라
# 합격식 문서가 맡는다.

$audioHeaderFor26 = Read-ProjectText 'Source/IndieGame/Audio/IGMissingFloorAudioSubsystem.h'
$uprojectText = Read-ProjectText 'IndieGame.uproject'

# 26.3-2 — 버스 이름은 §21.1의 여섯이다. 두 절이 같은 것을 다르게 부르면
# 어느 쪽을 고쳐야 하는지 알 수 없다.
$busNameRow = [regex]::Match(
	$story,
	'`BUS_ENTITY / PLAYER / PUZZLE / WORLD / UI / SCORE`')
$assertionCount++
if (-not $busNameRow.Success) {
	throw 'The §26.3 bus names no longer match §21.1.'
}
$busEnum = [regex]::Match(
	$audioHeaderFor26,
	'enum class EIGAudioBus : uint8\s*\r?\n\{(?<body>[\s\S]*?)\}')
$assertionCount++
if (-not $busEnum.Success) {
	throw 'The audio bus enum could not be read.'
}
foreach ($bus in @('Entity', 'Player', 'Puzzle', 'World', 'UI', 'Score')) {
	$assertionCount++
	if ($busEnum.Groups['body'].Value -notmatch ('(?m)^\s*' + $bus + ',')) {
		throw "The §26.3 bus name is not in the enum: $bus"
	}
}
# 옛 이름이 되살아나면 두 절이 다시 갈라진다.
foreach ($stale in @('BUS_ROOM', 'BUS_VOICE', 'BUS_SILENCE')) {
	$assertionCount++
	if ($story -match [regex]::Escape($stale)) {
		throw "The §26.3 bus list drifted back to a pre-implementation name: $stale"
	}
}

# 26.3-6 — 베타 기능은 출시 필수 경로에 두지 않는다.
$assertionCount++
if ($story -notmatch '\*\*안정판 우선\.\*\* Audio Gameplay Volumes처럼 Beta인 기능은 출시 필수 경로에') {
	throw 'The §26.3 stable-first rule was removed.'
}
foreach ($beta in @('AudioGameplayVolume', 'AudioGameplayVolumes')) {
	$assertionCount++
	if ($uprojectText -match [regex]::Escape($beta)) {
		throw "A beta audio plugin reached the shipping path (§26.3-6): $beta"
	}
}

# 26.4 — 공통 시스템 UI를 새 프레임워크로 전면 이식하지 않는다.
$assertionCount++
if ($story -notmatch '공통 시스템 UI를 새 프레임워크로 전면 이식하지 않는다') {
	throw 'The §26.4 no-CommonUI-port rule was removed.'
}
$assertionCount++
if ($uprojectText -match '"CommonUI"') {
	throw 'The native HUD path must stay; CommonUI was enabled (§26.4).'
}

# 26.4 재페이지 — 글자를 키우면 카드가 줄지, 카드가 작아지지 않는다.
$pageRow = [regex]::Match(
	$story,
	'(?<small>[0-9]+)~(?<smallMax>[0-9]+)%는 레인당 (?<three>[0-9]+)장, (?<midMin>[0-9]+)~(?<midMax>[0-9]+)%는 (?<two>[0-9]+)장, (?<bigMin>[0-9]+)~(?<bigMax>[0-9]+)%는 (?<one>[0-9]+)장')
$assertionCount++
if (-not $pageRow.Success) {
	throw 'The §26.4 repagination row could not be read.'
}
# 문서의 경계는 퍼센트, 코드의 경계는 배율이다. 116%는 1.15 초과, 151%는
# 1.50 초과로 옮겨진다.
$midThreshold = ([double]$pageRow.Groups['smallMax'].Value) / 100.0
$bigThreshold = ([double]$pageRow.Groups['midMax'].Value) / 100.0
$ladderSites = [regex]::Matches(
	$hudSource,
	'const int32 CardsPerLanePerPage = UserTextScale > (?<big>[0-9.]+)f\s*\r?\n\s*\? (?<one>[0-9]+)\s*\r?\n\s*: UserTextScale > (?<mid>[0-9.]+)f \? (?<two>[0-9]+) : (?<three>[0-9]+);')
$assertionCount++
if ($ladderSites.Count -lt 2) {
	throw 'The repagination ladder must stay in both the page count and the layout.'
}
foreach ($site in $ladderSites) {
	$assertionCount++
	if ([double]$site.Groups['big'].Value -ne $bigThreshold) {
		throw (
			'Repagination drops to one card above {0} but §26.4 says {1}.' -f
				$site.Groups['big'].Value, $bigThreshold)
	}
	$assertionCount++
	if ([double]$site.Groups['mid'].Value -ne $midThreshold) {
		throw (
			'Repagination drops to two cards above {0} but §26.4 says {1}.' -f
				$site.Groups['mid'].Value, $midThreshold)
	}
	foreach ($pair in @(
		@{ Group = 'one'; Expected = $pageRow.Groups['one'].Value },
		@{ Group = 'two'; Expected = $pageRow.Groups['two'].Value },
		@{ Group = 'three'; Expected = $pageRow.Groups['three'].Value })) {
		$assertionCount++
		if ($site.Groups[$pair.Group].Value -ne $pair.Expected) {
			throw (
				'Repagination shows {0} cards where §26.4 says {1}.' -f
					$site.Groups[$pair.Group].Value, $pair.Expected)
		}
	}
}
$assertionCount++
if ($story -notmatch '확대를 카드 안 축소로 상쇄하지 않는다') {
	throw 'The §26.4 no-shrink-to-fit rule was removed.'
}

# 26.4 — 생성 이미지의 글자가 정보가 되는 경로는 0이다.
$assertionCount++
if ($story -notmatch '생성 이미지의 글자\s*\r?\n?\s*환각이 정보가 되는 경로는 0이다') {
	throw 'The §26.4 no-hallucinated-text rule was removed.'
}

# 26.2 — 첫 12분에는 튜토리얼 팝업이 없고 밤 HUD가 0이다. §23이 같은 것을
# 금지하고 있으므로 여기서는 표가 그 약속을 계속 말하는지만 본다.
$assertionCount++
if ($story -notmatch '튜토리얼 팝업 대신 서로 다른 물리 반응') {
	throw 'The §26.2 no-tutorial-popup promise was removed.'
}
$assertionCount++
if ($story -notmatch '정보 대사 2문장 상한') {
	throw 'The §26.2 two-sentence cap was removed.'
}
$assertionCount++
if ($story -notmatch '강제 컷신 대신 첫 자율 공포 판단, 밤 HUD 0') {
	throw 'The §26.2 night-HUD-zero promise was removed.'
}

# --- 버전별 실행 기록이 자기 계약을 대는가 --------------------------------------
#
# §24가 적어 둔 이유가 여기에도 그대로 걸린다. 실제로 §27~§32는 계약이 다섯
# 개나 있는데 그 이름을 아무 데도 대지 않고 있었다. 계약이 있는 것과, 있다는
# 사실을 문서를 읽는 사람이 아는 것은 다른 일이다.

$versionSections = [regex]::Matches(
	$story, '(?m)^## (?<number>2[7-9]|3[0-5])\. ')
$assertionCount++
if ($versionSections.Count -ne 9) {
	throw (
		'Expected nine version sections (§27~§35), found {0}.' -f
			$versionSections.Count)
}
for ($index = 0; $index -lt $versionSections.Count; $index++) {
	$start = $versionSections[$index].Index
	$end = if ($index + 1 -lt $versionSections.Count) {
		$versionSections[$index + 1].Index
	} else {
		$story.Length
	}
	$body = $story.Substring($start, $end - $start)
	$number = $versionSections[$index].Groups['number'].Value
	$named = [regex]::Matches($body, 'Test-[A-Za-z0-9-]+\.ps1')
	$assertionCount++
	if ($named.Count -lt 1) {
		throw "§$number records what was built but never says what watches it."
	}
	foreach ($script in $named) {
		$assertionCount++
		if (-not (Test-Path (
			Join-Path $projectRoot (Join-Path 'Scripts' $script.Value)))) {
			throw "§$number names a contract that does not exist: $($script.Value)"
		}
	}
}

# --- §12 진실 게이트 · §13 포어섀도 장부 ----------------------------------------
#
# 앞 절들과 성격이 다르다. 여기 걸린 것은 숫자가 아니라 **인과**다 — 무엇이
# 확정돼야 무엇이 열리는가, 심은 것이 회수되는가.

$narrativeSource = Read-ProjectText 'Source/IndieGame/Narrative/IGMissingFloorNarrativeSubsystem.cpp'

# 12 — 최종 선택 게이트는 T6·T7·T9 확정 + 벽 개방이다.
$gateRow = [regex]::Match(
	$story, '최종 선택 게이트: (?<truths>T[0-9]+(?:·T[0-9]+)*) 확정 \+ 벽 개방')
$assertionCount++
if (-not $gateRow.Success) {
	throw 'The §12 final-choice gate could not be read.'
}
$gateTruths = $gateRow.Groups['truths'].Value -split '·'
$assertionCount++
if ($gateTruths.Count -ne 3) {
	throw (
		'The §12 gate names {0} truths; the code checks three.' -f $gateTruths.Count)
}
# 표에서 그 태그의 열거형 이름을 끌어온다. 태그와 이름을 두 번 적지 않는다.
$truthNames = @{
	'T6' = 'SomeoneInTheWall'
	'T7' = 'WasStillAlive'
	'T9' = 'WaitingForAnAnswer'
}
$unlockBody = [regex]::Match(
	$narrativeSource,
	'bool UIGMissingFloorNarrativeSubsystem::IsFinalChoiceUnlocked\(\) const(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $unlockBody.Success) {
	throw 'IsFinalChoiceUnlocked could not be isolated.'
}
foreach ($tag in $gateTruths) {
	$assertionCount++
	if (-not $truthNames.ContainsKey($tag)) {
		throw "The §12 gate names a truth this contract does not know: $tag"
	}
	$assertionCount++
	if (-not $unlockBody.Groups['body'].Value.Contains(
		('EIGMissingFloorTruth::{0}' -f $truthNames[$tag]))) {
		throw "The final choice must require $tag ($($truthNames[$tag]))."
	}
}
# 세 개뿐이다. 하나를 더 걸면 게이트가 문서보다 좁아진다.
$gateChecks = ([regex]::Matches(
	$unlockBody.Groups['body'].Value, 'HasTruth\(')).Count
$assertionCount++
if ($gateChecks -ne $gateTruths.Count) {
	throw (
		'The final choice checks {0} truths but §12 names {1}.' -f
			$gateChecks, $gateTruths.Count)
}

# 벽은 게이트가 열린 뒤에만 부술 수 있고, 선택은 벽이 열린 뒤에만 뜬다.
# 순서가 뒤집히면 진실을 모으지 않고도 엔딩에 닿는다.
$nightFourForGate = Read-ProjectText 'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.cpp'
$assertionCount++
if ($nightFourForGate -notmatch
	'const bool bCanBreak = bNightFour\s*\r?\n\s*&& Narrative->IsFinalChoiceUnlocked\(\)') {
	throw 'The wall must stay shut until the §12 gate opens.'
}
$assertionCount++
if ($nightFourForGate -notmatch
	'const bool bCanChoose = bNightFour\s*\r?\n\s*&& bWallOpened') {
	throw 'The ending choice must wait for the wall (§12).'
}
# 첫 신고 없이 선택이 뜨면 §24 즉시 차단 14가 깨진다.
$assertionCount++
if ($nightFourForGate -notmatch
	'&& Narrative->WasFirstReportMade\(\)') {
	throw 'The ending choice must wait for the first report (§12, §24-14).'
}

# 13 — 심은 것은 반드시 회수된다. 표의 네 칸이 다 차 있어야 그 원칙이 읽힌다.
$ledgerTable = [regex]::Match(
	$story,
	'## 13\. 포어섀도 장부[^\r\n]*\r?\n(?<body>[\s\S]*?)\r?\n\r?\n회수 없는 심기')
$assertionCount++
if (-not $ledgerTable.Success) {
	throw 'The §13 foreshadow ledger could not be read.'
}
$ledgerRows = 0
foreach ($row in [regex]::Matches(
	$ledgerTable.Groups['body'].Value,
	'(?m)^\| (?<plant>[^|]+?) \| (?<where>[^|]+?) \| (?<payoff>[^|]+?) \| (?<when>[^|]+?) \|')) {
	$fields = @('plant', 'where', 'payoff', 'when')
	if ($row.Groups['plant'].Value.Trim() -eq '심기') {
		continue
	}
	$ledgerRows++
	foreach ($field in $fields) {
		$assertionCount++
		$value = $row.Groups[$field].Value.Trim()
		if ($value.Length -lt 2 -or $value -match '^-+$') {
			throw (
				'A §13 ledger row has an empty {0}: {1}' -f
					$field, $row.Groups['plant'].Value.Trim())
		}
	}
}
$assertionCount++
if ($ledgerRows -lt 16) {
	throw "The §13 ledger lists $ledgerRows plantings; sixteen were authored."
}
$assertionCount++
if ($story -notmatch '회수 없는 심기, 심기 없는 회수 금지 원칙 유지') {
	throw 'The §13 plant-and-payoff rule was removed.'
}

# 표에 오른 출처는 전부 세계에 실물이 있어야 한다. 그 약속이 사라지면
# 도달 불가인 진실이 다시 생긴다 — 실제로 T5와 T8이 그랬다.
$assertionCount++
if ($story -notmatch '\*\*표에 오른 출처는 전부 세계에 실물이 있어야 한다\.\*\*') {
	throw 'The §12 every-source-is-real rule was removed.'
}
$assertionCount++
if ($story -notmatch '`Test-ArtAssetContract\.ps1`이 열거형을 읽어 게임플레이 파일과 대조한다') {
	throw 'The §12 source-reachability contract lost its name.'
}

# --- §5.1 소음 모델 · §17 기믹 배치표 -------------------------------------------
#
# §5.1은 값이 코드에 흩어져 있고, §17은 「몇 개인가」와 「없는가」다.

$noiseHeader = Read-ProjectText 'Source/IndieGame/Entity/IGNoiseSubsystem.h'
$doorHeaderFor5 = Read-ProjectText 'Source/IndieGame/Interaction/IGSwingDoor.h'
$entitySourceFor5 = Read-ProjectText 'Source/IndieGame/Entity/IGListenerEntity.cpp'
$stressForNoise = Read-ProjectText 'Source/IndieGame/Player/IGStressComponent.cpp'

# 문 여닫기 두 값. 조용히 여는 쪽이 더 조용해야 홀드가 값을 한다.
$doorRow = [regex]::Match(
	$story, '\| 문 여닫기\(천천히/그냥\) \| (?<quiet>[0-9.]+) / (?<normal>[0-9.]+) \|')
$assertionCount++
if (-not $doorRow.Success) {
	throw 'The §5.1 door row could not be read.'
}
foreach ($pair in @(
	@{ Field = 'QuietSwingLoudness'; Expected = $doorRow.Groups['quiet'].Value },
	@{ Field = 'NormalSwingLoudness'; Expected = $doorRow.Groups['normal'].Value })) {
	$declared = [regex]::Match(
		$doorHeaderFor5, ('float {0} = (?<value>[0-9.]+)f;' -f $pair.Field))
	$assertionCount++
	if (-not $declared.Success) {
		throw ('The §5.1 door loudness is missing: {0}' -f $pair.Field)
	}
	$assertionCount++
	if ([double]$declared.Groups['value'].Value -ne [double]$pair.Expected) {
		throw (
			'{0} is {1} but §5.1 says {2}.' -f
				$pair.Field, $declared.Groups['value'].Value, $pair.Expected)
	}
}
$assertionCount++
if ([double](
	[regex]::Match($doorHeaderFor5, 'float QuietSwingLoudness = (?<v>[0-9.]+)f;').Groups['v'].Value) -ge
	[double](
	[regex]::Match($doorHeaderFor5, 'float NormalSwingLoudness = (?<v>[0-9.]+)f;').Groups['v'].Value)) {
	throw 'Opening a door slowly must be the quieter option (§5.1).'
}

# 앉아 이동의 소음.
$crouchRow = [regex]::Match(
	$story, '\| 정지·앉아 이동 \| (?<value>[0-9.]+) \|')
$assertionCount++
if (-not $crouchRow.Success) {
	throw 'The §5.1 crouch row could not be read.'
}
$crouchDeclared = [regex]::Match(
	$characterSource, 'constexpr float CrouchFootstepLoudness = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $crouchDeclared.Success) {
	throw 'CrouchFootstepLoudness could not be read.'
}
$assertionCount++
if ([double]$crouchDeclared.Groups['value'].Value -ne [double]$crouchRow.Groups['value'].Value) {
	throw (
		'CrouchFootstepLoudness is {0} but §5.1 says {1}.' -f
			$crouchDeclared.Groups['value'].Value, $crouchRow.Groups['value'].Value)
}

# §5.1 소음표. 이 아홉 줄이 게임의 핵심 모델인데 절반만 지켜지고 있었다 —
# 앉기·달리기·심박은 계약이 보고 문 여닫기·낙하물·두꺼비집·프로타주는
# 아무도 안 봤다. 표 한 칸을 조용히 고치면 압박 곡선이 문서와 다른 게임이
# 된다는 것은 §20.2 튜닝 테이블에서 이미 배운 일이다.
$noiseSources = @{
	Character = Read-ProjectText 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
	SwingDoor = Read-ProjectText 'Source/IndieGame/Interaction/IGSwingDoor.h'
	NightOne = Read-ProjectText 'Source/IndieGame/Entity/IGNightOneBeatDirector.cpp'
	PuzzleOne = Read-ProjectText `
		'Source/IndieGame/Entity/IGMissingFloorPuzzleOneDirector.cpp'
	PuzzleTwo = Read-ProjectText `
		'Source/IndieGame/Entity/IGMissingFloorPuzzleTwoDirector.cpp'
	NightThree = Read-ProjectText `
		'Source/IndieGame/Entity/IGMissingFloorNightThreeDirector.cpp'
}
# 표의 행 → 그 값을 드는 상수. 한 행이 두 값을 적으면 상수도 둘이다.
#
# 문 여닫기와 정지·앉아 이동은 위 블록이 이미 본다. 문은 두 값 중 어느 쪽이
# 조용해야 하는지까지 보므로 여기로 옮기면 그 규칙이 사라진다 — 같은 값을
# 두 군데서 보는 것이 이 저장소가 계속 고쳐 온 결함이기도 하다.
$noiseRows = @(
	@{ Row = '달리기'; Where = 'Character'; Names = @('SprintFootstepLoudness') },
	@{ Row = '낙하물·부딪힘'; Where = 'NightOne'; Names = @('ImpactLoudness') },
	@{ Row = '두꺼비집·밸브'; Where = 'PuzzleOne'; Names = @('BreakerNoiseLoudness') },
	@{ Row = '두꺼비집·밸브'; Where = 'NightThree'; Names = @('ValveLoudness') },
	@{ Row = '프로타주'; Where = 'PuzzleTwo'; Names = @('FrottageLoudness') }
)
foreach ($noiseRow in $noiseRows) {
	$rowMatch = [regex]::Match(
		$story,
		'(?m)^\| ' + [regex]::Escape($noiseRow.Row) +
			'[^|]*\| (?<values>[^|]+?) \|')
	$assertionCount++
	if (-not $rowMatch.Success) {
		throw ('§5.1 소음표에서 「{0}」 행이 사라졌다.' -f $noiseRow.Row)
	}
	$rowValues = @(
		[regex]::Matches($rowMatch.Groups['values'].Value, '[0-9]+(?:\.[0-9]+)?') |
			ForEach-Object { [double]$_.Value })
	$assertionCount++
	if ($rowValues.Count -lt $noiseRow.Names.Count) {
		throw (
			'§5.1의 「{0}」 행이 값 {1}개를 적었는데 코드는 {2}개를 든다.' -f
				$noiseRow.Row, $rowValues.Count, $noiseRow.Names.Count)
	}
	for ($nameIndex = 0; $nameIndex -lt $noiseRow.Names.Count; $nameIndex++) {
		$constantName = $noiseRow.Names[$nameIndex]
		$constant = [regex]::Match(
			$noiseSources[$noiseRow.Where],
			'\b' + $constantName + ' = (?<value>[0-9.]+)f;')
		$assertionCount++
		if (-not $constant.Success) {
			throw ('§5.1의 「{0}」을 드는 {1}을 찾지 못했다.' -f
				$noiseRow.Row, $constantName)
		}
		$assertionCount++
		if ([double]$constant.Groups['value'].Value -ne $rowValues[$nameIndex]) {
			throw (
				'§5.1의 「{0}」은 {1}인데 {2}은 {3}이다.' -f
					$noiseRow.Row, $rowValues[$nameIndex], $constantName,
					$constant.Groups['value'].Value)
		}
	}
}
# 걷기만 코드가 밴드다. 표의 한 값이 그 안에 있어야 한다 — 표면마다 다른
# 발소리를 한 줄로 적은 것이라 밖으로 나가면 표가 거짓말이 된다.
$walkRow = [regex]::Match($story, '(?m)^\| 걷기 \| (?<value>[0-9.]+) \|')
$assertionCount++
if (-not $walkRow.Success) {
	throw '§5.1 소음표에서 걷기 행이 사라졌다.'
}
$walkStated = [double]$walkRow.Groups['value'].Value
$walkLow = [regex]::Match(
	$noiseSources['Character'], 'MinimumFootstepLoudness = (?<value>[0-9.]+)f;')
$walkHigh = [regex]::Match(
	$noiseSources['Character'], 'MaximumFootstepLoudness = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $walkLow.Success -or -not $walkHigh.Success) {
	throw '걷기 발소리 밴드를 읽지 못했다 (§5.1).'
}
$assertionCount++
if ($walkStated -lt [double]$walkLow.Groups['value'].Value `
	-or $walkStated -gt [double]$walkHigh.Groups['value'].Value) {
	throw (
		'§5.1의 걷기 {0}이 코드 밴드({1}~{2}) 밖이다.' -f
			$walkStated,
			$walkLow.Groups['value'].Value,
			$walkHigh.Groups['value'].Value)
}
# 표에 적힌 값이 실제로 보고되는 값인지 본다. 한동안 절반이 호출부에 맨
# 숫자로 박혀 있었고, 그래서 냉장고 문이 서랍·캐비닛 값을 쓰는 동안 표는
# 그 행을 「아직 없다」고 적어 두고 있었다. 이름이 없으면 아무도 모른다.
$noiseTableRows = @(
	@{ Row = '미닫이문'; Column = 1
		File = 'Source/IndieGame/Interaction/IGSlidingDoor.cpp'
		Name = 'SlideLoudness' },
	@{ Row = '서랍·캐비닛'; Pattern = '서랍·캐비닛\(열기/닫기\)'; Column = 1
		File = 'Source/IndieGame/Interaction/IGFridge.cpp'
		Name = 'OpenLoudness' },
	@{ Row = '서랍·캐비닛'; Pattern = '서랍·캐비닛\(열기/닫기\)'; Column = 2
		File = 'Source/IndieGame/Interaction/IGFridge.cpp'
		Name = 'CloseLoudness' },
	@{ Row = '노크'; Column = 1
		File = 'Source/IndieGame/Player/IGPlayerCharacter.h'
		Name = 'KnockLoudness' },
	@{ Row = '엘리베이터 부르기'; Column = 1
		File = 'Source/IndieGame/Interaction/IGElevator.cpp'
		Name = 'CallLoudness' },
	@{ Row = '소품 집기'; Column = 1
		File = 'Source/IndieGame/Interaction/IGPickupItem.cpp'
		Name = 'PickupLoudness' },
	@{ Row = '쪽지 넘기기·홀드 놓침'; Column = 1
		File = 'Source/IndieGame/Interaction/IGReadableNote.cpp'
		Name = 'PageLoudness' },
	@{ Row = '쪽지 넘기기·홀드 놓침'; Column = 1
		File = 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
		Name = 'ForcedReleaseLoudness' })
foreach ($entry in $noiseTableRows) {
	$rowMatch = [regex]::Match(
		$story,
		'\| ' + $(if ($entry.ContainsKey('Pattern')) { $entry.Pattern } else { $entry.Row }) +
			' \| (?<first>[0-9.]+)(?: / (?<second>[0-9.]+))? \|')
	$assertionCount++
	if (-not $rowMatch.Success) {
		throw ('§5.1 소리 표에서 {0} 행을 못 읽었다.' -f $entry.Row)
	}
	$stated = if ($entry.Column -eq 2) {
		$rowMatch.Groups['second'].Value } else { $rowMatch.Groups['first'].Value }
	$assertionCount++
	if ([string]::IsNullOrEmpty($stated)) {
		throw ('§5.1 소리 표의 {0} 행이 값을 잃었다.' -f $entry.Row)
	}
	$ownerText = Read-ProjectText $entry.File
	$constant = [regex]::Match(
		$ownerText, $entry.Name + ' = (?<value>[0-9.]+)f;')
	$assertionCount++
	if (-not $constant.Success) {
		throw ('{0}이(가) 사라졌다 (§5.1 {1}).' -f $entry.Name, $entry.Row)
	}
	$assertionCount++
	if ([double]$constant.Groups['value'].Value -ne [double]$stated) {
		throw (
			'§5.1은 {0}을(를) {1}로 적는데 {2}는 {3}이다.' -f
				$entry.Row, $stated, $entry.Name,
				$constant.Groups['value'].Value)
	}
}
# 이 값들이 다시 이름 없는 숫자로 돌아가면 표가 또 조용히 어긋난다.
foreach ($file in @(
	'Source/IndieGame/Interaction/IGElevator.cpp',
	'Source/IndieGame/Interaction/IGFridge.cpp',
	'Source/IndieGame/Interaction/IGPickupItem.cpp',
	'Source/IndieGame/Interaction/IGReadableNote.cpp',
	'Source/IndieGame/Interaction/IGSlidingDoor.cpp')) {
	$text = Read-ProjectText $file
	$assertionCount++
	if ($text -match 'ReportNoise\([^;]*?,\s*[0-9]+\.[0-9]+f\s*,') {
		throw (
			'{0} reports noise with a bare number again (§5.1).' -f $file)
	}
}

# 험 존: 반경 2m, 마스킹 0.2. 지금 서 있는 둘이 같은 값을 써야 한다.
$humRow = [regex]::Match(
	$story, '험 존\(반경 (?<radius>[0-9]+)m\)은 소음을 -(?<masking>[0-9.]+) 마스킹한다')
$assertionCount++
if (-not $humRow.Success) {
	throw 'The §5.1 hum-zone row could not be read.'
}
$expectedRadius = [double]$humRow.Groups['radius'].Value * 100.0
# 파일이 아니라 상수를 센다. 한 파일이 험을 둘 이상 들 수 있고, 실제로
# 그레이박스 디렉터가 냉장고와 보일러 둘을 든다.
$humZones = 0
foreach ($file in @(
	'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp',
	'Source/IndieGame/Entity/IGNightOneBeatDirector.cpp')) {
	$text = Read-ProjectText $file
	foreach ($radius in [regex]::Matches(
		$text, '(?<name>[A-Za-z]+)HumRadius = (?<value>[0-9.]+)f;')) {
		$humZones++
		$masking = [regex]::Match(
			$text, $radius.Groups['name'].Value + 'HumMasking = (?<value>[0-9.]+)f;')
		$assertionCount++
		if (-not $masking.Success) {
			throw (
				'§5.1 hum zone {0} has a radius but no masking.' -f
					$radius.Groups['name'].Value)
		}
		$assertionCount++
		if ([double]$radius.Groups['value'].Value -ne $expectedRadius) {
			throw (
				'{0} hum reaches {1}cm but §5.1 says {2}cm.' -f
					$radius.Groups['name'].Value,
					$radius.Groups['value'].Value, $expectedRadius)
		}
		$assertionCount++
		if ([double]$masking.Groups['value'].Value -ne [double]$humRow.Groups['masking'].Value) {
			throw (
				'{0} hum masks {1} but §5.1 says {2}.' -f
					$radius.Groups['name'].Value,
					$masking.Groups['value'].Value, $humRow.Groups['masking'].Value)
		}
	}
}
$assertionCount++
if ($humZones -ne 3) {
	throw "§5.1 says three hum zones stand; found $humZones."
}
# 보일러는 방이 아니라 벽장이다. 문서가 그 사실을 말하고 있어야 다음 사람이
# §6에서 없는 방을 찾으러 가지 않는다.
$assertionCount++
if ($story -notmatch '\*\*보일러는 방이 아니라 복도 벽장이다\.\*\*') {
	throw 'The §5.1 note about the boiler cupboard was removed.'
}
# 늘 우는 소리는 오래됐다는 이유로 밀리면 안 된다. 보이스 상한은 한 번 울고
# 마는 소리가 쌓이는 걸 막으려고 있는데, 험은 무대 설치 때 걸려서 serial이
# 제일 낮다 — 상한에 닿는 순간 맨 먼저 밀리고 퇴출은 페이드아웃이라 안
# 돌아온다. 마스킹이 제일 필요한 순간에 엄폐가 조용해진다.
$audioSubsystem = Read-ProjectText `
	'Source/IndieGame/Audio/IGMissingFloorAudioSubsystem.cpp'
$assertionCount++
if ($audioSubsystem -notmatch 'if \(Voices\[Index\]\.bPersistent\)') {
	throw 'Voice eviction stopped skipping persistent beds (§21).'
}
# 밀 수 있는 게 하나도 없을 때 빠져나가는 길. 없으면 반복문이 안 끝난다.
$assertionCount++
if ($audioSubsystem -notmatch 'if \(OldestIndex == INDEX_NONE\)') {
	throw 'The voice eviction loop lost its no-candidate exit (§21).'
}
# 험과 물소리는 베드로 건다. 자리는 세되 밀리지는 않는다.
foreach ($bed in @(
	@{ File = 'Source/IndieGame/Audio/IGAudioHelpers.cpp'; What = '험 소리' },
	@{ File = 'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.cpp'
		What = '밤4 물소리 베드' })) {
	$bedText = Read-ProjectText $bed.File
	$assertionCount++
	if ($bedText -notmatch 'RegisterPersistentBed\(') {
		throw (
			'{0}이(가) 더 이상 베드로 걸리지 않는다. 상한에 닿으면 밀린다 (§21).' -f
				$bed.What)
	}
}

# §4.3 규칙 7의 「최대 3」. 코드 다섯 군데가 각자 맨 숫자로 적고 있어서
# 한 군데만 고쳐도 컴파일이 되고, 대기 시간 표는 티어로 색인하니 상한이
# 표보다 커지면 배열 밖을 읽는다. 문서가 말한 수를 세 곳이 같이 쓰는지 본다.
$rule7 = [regex]::Match($story, '공격성 티어 \+1, 최대 (?<max>\d+)\)')
$assertionCount++
if (-not $rule7.Success) {
	throw 'The §4.3 rule 7 aggression cap could not be read.'
}
$aggressionMax = [int]$rule7.Groups['max'].Value
foreach ($file in @(
	'Source/IndieGame/Entity/IGListenerEntity.cpp',
	'Source/IndieGame/Entity/IGListenerTuning.cpp',
	'Source/IndieGame/Narrative/IGMissingFloorNarrativeSubsystem.cpp')) {
	$text = Read-ProjectText $file
	$clamps = [regex]::Matches(
		$text, 'Clamp\(\s*(?:(?:Snapshot\.Night\.)?AggressionTier|EffectiveTier)[^,]*,\s*0,\s*(?<max>\d+)\)')
	$assertionCount++
	if ($clamps.Count -eq 0) {
		throw "§4.3 rule 7: no aggression clamp left in $file."
	}
	foreach ($clamp in $clamps) {
		$assertionCount++
		if ([int]$clamp.Groups['max'].Value -ne $aggressionMax) {
			throw (
				'§4.3 rule 7 caps aggression at {0} but {1} clamps to {2}.' -f
					$aggressionMax, $file, $clamp.Groups['max'].Value)
		}
	}
}
# 대기 시간 표는 티어로 색인한다. 상한이 3이면 자리가 넷 있어야 한다.
$entitySource = Read-ProjectText 'Source/IndieGame/Entity/IGListenerEntity.cpp'
$waitTable = [regex]::Match(
	$entitySource, 'float Seconds\[(?<size>\d+)\] = \{(?<body>[^}]*)\}')
$assertionCount++
if (-not $waitTable.Success) {
	throw 'The §4.3 rule 7 wait table could not be read.'
}
$assertionCount++
if ([int]$waitTable.Groups['size'].Value -ne ($aggressionMax + 1)) {
	throw (
		'§4.3 rule 7 caps aggression at {0}, so the wait table needs {1} rows; it has {2}.' -f
			$aggressionMax, ($aggressionMax + 1), $waitTable.Groups['size'].Value)
}
# 「기다림은 회차마다 짧아진다」 — 표가 올라가면 규칙이 거짓말이 된다.
$waitSeconds = @($waitTable.Groups['body'].Value -split ',' | ForEach-Object {
	[double]($_ -replace '[^0-9.]', '') })
for ($i = 1; $i -lt $waitSeconds.Count; $i++) {
	$assertionCount++
	if ($waitSeconds[$i] -gt $waitSeconds[$i - 1]) {
		throw (
			'§4.3 rule 7 says the wait shortens each reset, but tier {0} waits {1}s after {2}s.' -f
				$i, $waitSeconds[$i], $waitSeconds[$i - 1])
	}
}

# §4.3 규칙 2는 플레이어가 배울 순서를 약속한다. 값이 전부 맞아도 순서가
# 뒤집힐 수 있고, 실제로 뒤집혀 있었다 — 표는 달리기 0.5 낙하물 0.6인데
# 규칙 문장은 낙하물이 먼저였다. 약속을 문서에서 읽어서 상수로 확인한다.
$rule2 = [regex]::Match(
	$story, '\*\*소리에는 반경이 있다\.\*\* (?<chain>[^.]+)\.')
$assertionCount++
if (-not $rule2.Success) {
	throw 'The §4.3 rule 2 loudness chain could not be read.'
}
$loudnessOwner = @{
	'발소리' = @{
		File = 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
		Name = 'MaximumFootstepLoudness' }
	'문 여닫이' = @{
		File = 'Source/IndieGame/Interaction/IGSwingDoor.h'
		Name = 'NormalSwingLoudness' }
	'달리기' = @{
		File = 'Source/IndieGame/Player/IGPlayerCharacter.cpp'
		Name = 'SprintFootstepLoudness' }
	'낙하물' = @{
		File = 'Source/IndieGame/Entity/IGNightOneBeatDirector.cpp'
		Name = 'ImpactLoudness' }
}
$chainTerms = @($rule2.Groups['chain'].Value -split '<' | ForEach-Object {
	$_.Trim() })
$assertionCount++
if ($chainTerms.Count -lt 3) {
	throw (
		'§4.3 rule 2 promises an ordering of {0} things; that is not a chain.' -f
			$chainTerms.Count)
}
$previousName = ''
$previousValue = -1.0
foreach ($term in $chainTerms) {
	$assertionCount++
	if (-not $loudnessOwner.ContainsKey($term)) {
		throw (
			'§4.3 rule 2 names "{0}", which no constant answers to.' -f $term)
	}
	$owner = $loudnessOwner[$term]
	$ownerText = Read-ProjectText $owner.File
	$match = [regex]::Match(
		$ownerText, $owner.Name + ' = (?<value>[0-9.]+)f;')
	$assertionCount++
	if (-not $match.Success) {
		throw ('{0} lost its loudness constant (§4.3 rule 2).' -f $owner.Name)
	}
	$value = [double]$match.Groups['value'].Value
	$assertionCount++
	if ($value -le $previousValue) {
		throw (
			'§4.3 rule 2 puts {0} above {1}, but {2}={3} and {4}={5}.' -f
				$term, $previousName, $owner.Name, $value,
				$previousName, $previousValue)
	}
	$previousName = $term
	$previousValue = $value
}

# 험이 소리를 내는지, 그리고 그 소리가 마스킹과 같은 자리에서 같은 거리까지
# 가는지 본다. 마스킹만 등록하고 소리를 안 내던 시절이 있었다 — 통과하는
# 계약과 배울 수 없는 규칙이 한동안 같이 서 있었다.
$humRegisterPattern = 'RegisterHumSource\(\s*(?<loc>[A-Za-z:_]+),\s*' +
	'(?<radius>[A-Za-z:_]+)HumRadius,'
$humLoopPattern = 'SpawnHumLoopAt\(\s*this,\s*TEXT\("[^"]+"\),\s*' +
	'(?<loc>[A-Za-z:_]+),\s*(?<radius>[A-Za-z:_]+)HumRadius\)'
foreach ($file in @(
	'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp',
	'Source/IndieGame/Entity/IGNightOneBeatDirector.cpp')) {
	$text = Read-ProjectText $file
	$registered = @([regex]::Matches($text, $humRegisterPattern) | ForEach-Object {
		'{0}|{1}' -f $_.Groups['loc'].Value, $_.Groups['radius'].Value })
	$sounded = @([regex]::Matches($text, $humLoopPattern) | ForEach-Object {
		'{0}|{1}' -f $_.Groups['loc'].Value, $_.Groups['radius'].Value })
	$assertionCount++
	if ($registered.Count -eq 0) {
		throw "§5.1 hum registrations became unreadable in $file."
	}
	$registeredKey = (($registered | Sort-Object) -join ' ')
	$soundedKey = (($sounded | Sort-Object) -join ' ')
	$assertionCount++
	if ($registeredKey -ne $soundedKey) {
		throw (
			'§5.1 hums mask at [{0}] but sound at [{1}] in {2}.' -f
				($registered -join ', '), ($sounded -join ', '), $file)
	}
}
# 감쇠 거리는 마스킹 반경에서 빼서 쓴다. 여기에 숫자를 박으면 소리는 나는데
# 안 가려지는 띠가 생기고, 그 띠에서 배운 규칙은 틀린 규칙이다.
$audioHelpers = Read-ProjectText 'Source/IndieGame/Audio/IGAudioHelpers.cpp'
$assertionCount++
if ($audioHelpers -notmatch 'MaskingRadius - HumBodyRadius') {
	throw 'The §5.1 hum loop stopped deriving its falloff from the masking radius.'
}
$assertionCount++
if ($story -notmatch '\*\*험은 들려야 험이다\.\*\*') {
	throw 'The §5.1 note about hums being audible was removed.'
}

# 벽장 자리는 씬이 든다. 험이 좌표를 따로 적으면 벽장만 옮겨진다.
$boilerGreybox = Read-ProjectText `
	'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'
$assertionCount++
if ($boilerGreybox -notmatch
	'AIGPrologueWorldScene::GetBoilerCupboardLocation\(\)') {
	throw 'The boiler hum stopped asking the cupboard where it stands (§5.1).'
}

# 험은 기계를 따라다녀야 한다. 냉장고 좌표가 두 벌이면 하나만 움직인다 —
# 세계 장면이 냉장고를 옮겨도 험은 옛 자리에서 계속 울고, 플레이어가 배운
# 「기계 옆이 안전하다」가 빈 벽을 가리킨다.
$greyboxSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'
$fridgeSceneHeader = Read-ProjectText 'Source/IndieGame/Core/IGPrologueWorldScene.h'
$fridgeSceneSource = Read-ProjectText 'Source/IndieGame/Core/IGPrologueWorldScene.cpp'

$assertionCount++
if ($greyboxSource -match 'FridgeHumLocation\s*\(') {
	throw 'The fridge hum took a coordinate of its own again (§5.1).'
}
# 험은 좌표를 지역 변수로 잡아 두고 마스킹과 소리가 같이 읽는다. 그 변수가
# 어디서 나왔는지를 본다 — 등록 인자만 보면 이름 하나로 무엇이든 넣을 수 있다.
$humBinding = [regex]::Match(
	$greyboxSource,
	'const FVector FridgeHumLocation =(?<expr>[\s\S]{0,200}?);')
$assertionCount++
if (-not $humBinding.Success) {
	throw 'The fridge hum registration could not be read.'
}
$assertionCount++
if ($humBinding.Groups['expr'].Value -notmatch 'GetFridgeLocation\(\)') {
	throw 'The fridge hum stopped asking the fridge where it stands (§5.1).'
}
$assertionCount++
if ($greyboxSource -notmatch
	'RegisterHumSource\(\s*FridgeHumLocation,') {
	throw 'The fridge hum no longer registers at the location it bound (§5.1).'
}
# 높이만 험이 정한다. 기계 몸통 한가운데를 바닥에서 재는 값이라 자리와는
# 다른 사실이다.
$assertionCount++
if ($humBinding.Groups['expr'].Value -notmatch 'FridgeHumHeightOffset') {
	throw 'The fridge hum lost the authored body height (§5.1).'
}
$assertionCount++
if ($fridgeSceneHeader -notmatch 'FVector GetFridgeLocation\(\) const;') {
	throw 'The world scene stopped telling anyone where the fridge stands.'
}
$fridgeAccessor = [regex]::Match(
	$fridgeSceneSource,
	'FVector AIGPrologueWorldScene::GetFridgeLocation\(\) const\r?\n\{(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $fridgeAccessor.Success) {
	throw 'GetFridgeLocation could not be read.'
}
$assertionCount++
if ($fridgeAccessor.Groups['body'].Value -notmatch 'Fridge->GetActorLocation\(\)') {
	throw 'GetFridgeLocation stopped reading the prop it names.'
}
$assertionCount++
if ($fridgeAccessor.Groups['body'].Value -notmatch 'IGPrologueWorld::FridgeLocation') {
	throw 'GetFridgeLocation lost the authored fallback.'
}
# 폴백과 스폰이 같은 상수를 써야 프롭이 서기 전에 물어봐도 거짓말이 아니다.
$assertionCount++
if ($fridgeSceneSource -notmatch
	'SpawnActor<AIGFridge>\([\s\S]{0,200}?IGPrologueWorld::FridgeLocation') {
	throw 'The fridge stopped spawning at the constant its accessor falls back to.'
}
# 런타임 프로브는 험의 중심이 아니라 냉장고에 서서 잰다. 둘이 갈라지면
# 정적 검사가 아니라 여기서 걸린다.
$assertionCount++
if ($greyboxSource -notmatch
	'GetMaskingAt\(\r?\n?\s*WorldScene->GetFridgeLocation\(\)\)') {
	throw 'The masking probe stopped measuring at the fridge itself (§5.1).'
}
$assertionCount++
if ($story -notmatch '\*\*험은 기계를 따라다닌다\.\*\*') {
	throw 'The §5.1 rule that a hum rides its machine was removed.'
}

# 노크 3연 동안의 전역 마스킹.
$bangRow = [regex]::Match(
	$story, '노크 3연 동안은 모든 플레이어 소음이 -(?<value>[0-9.]+) 마스킹된다')
$assertionCount++
if (-not $bangRow.Success) {
	throw 'The §5.1 knock-masking row could not be read.'
}
$bangDeclared = [regex]::Match(
	$entitySourceFor5, 'constexpr float BangMasking = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $bangDeclared.Success) {
	throw 'BangMasking could not be read.'
}
$assertionCount++
if ([double]$bangDeclared.Groups['value'].Value -ne [double]$bangRow.Groups['value'].Value) {
	throw (
		'BangMasking is {0} but §5.1 says {1}.' -f
			$bangDeclared.Groups['value'].Value, $bangRow.Groups['value'].Value)
}

# 심박은 반경이 계약이다. 소음값 × 전달거리가 3m에 닿는지 계산해서 본다.
$heartRow = [regex]::Match(
	$story, '3m를 채우는 값이 (?<loudness>[0-9.]+)이고 코드가 그것을 쓴다')
$assertionCount++
if (-not $heartRow.Success) {
	throw 'The §5.1 heartbeat note could not be read.'
}
$carry = [regex]::Match(
	$noiseHeader, 'CarryPerLoudness = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $carry.Success) {
	throw 'CarryPerLoudness could not be read.'
}
$assertionCount++
if (-not $stressForNoise.Contains(
	($heartRow.Groups['loudness'].Value + 'f,'))) {
	throw (
		'The heartbeat must report {0} to reach three meters (§5.1).' -f
			$heartRow.Groups['loudness'].Value)
}
$heartReach = [double]$heartRow.Groups['loudness'].Value * [double]$carry.Groups['value'].Value
$assertionCount++
if ($heartReach -lt 290.0 -or $heartReach -gt 310.0) {
	throw (
		'The heartbeat carries {0}cm; §5.1 wants about three meters.' -f $heartReach)
}

# 소리는 소음보다 멀리 가야 한다. 경고가 비용보다 먼저 오는 순서가 이 규칙의
# 전부이고, 뒤집히면 자기 심장이 자기를 판 것을 뒤늦게 알게 된다.
$heartbeatAudible = [regex]::Match(
	$stressForNoise, 'AudibleHeartbeatCarry = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $heartbeatAudible.Success) {
	throw 'AudibleHeartbeatCarry could not be read (§5.1).'
}
$assertionCount++
if ([double]$heartbeatAudible.Groups['value'].Value -le $heartReach) {
	throw (
		'심박 소리가 {0}cm인데 소음이 {1}cm까지 간다. 경고가 비용보다 먼저 와야 한다 (§5.1).' -f
			$heartbeatAudible.Groups['value'].Value, $heartReach)
}
$assertionCount++
if ($story -notmatch '\*\*심박 소리는 소음보다 멀리 간다\.\*\*') {
	throw 'The §5.1 rule that the heartbeat cue outruns its cost was removed.'
}

# --- §17 기믹 배치표 -----------------------------------------------------------
$adopted = [regex]::Match(
	$story, '### 17\.1 채택 — (?<count>[0-9]+)종\r?\n(?<body>[\s\S]*?)\r?\n### 17\.2')
$assertionCount++
if (-not $adopted.Success) {
	throw 'The §17.1 adoption table could not be read.'
}
$adoptedRows = 0
foreach ($row in [regex]::Matches(
	$adopted.Groups['body'].Value, '(?m)^\| (?<gimmick>[^|]+?) \| (?<lineage>[^|]+?) \|')) {
	if ($row.Groups['gimmick'].Value.Trim() -eq '기믹' -or
		$row.Groups['gimmick'].Value -match '^-+$') {
		continue
	}
	$adoptedRows++
}
$assertionCount++
if ($adoptedRows -ne [int]$adopted.Groups['count'].Value) {
	throw (
		'§17.1 says {0} adopted gimmicks but lists {1}.' -f
			$adopted.Groups['count'].Value, $adoptedRows)
}
# 기믹은 한 비트에 1회다. 그 원칙이 사라지면 표는 그냥 목록이 된다.
$assertionCount++
if ($story -notmatch '\*\*기믹은 게임의 정체성\(소리\)을 통과해야만 채택된다\.\*\*') {
	throw 'The §17 adoption principle was removed.'
}
$assertionCount++
if ($story -notmatch '한 밤에 신규 기믹 등장은 최대 1개') {
	throw 'The §17.4 density rule was removed.'
}
# 메타는 두 곳뿐이다 — 실시간 시계와 밤 5.
$assertionCount++
if ($story -notmatch '메타는 §17\.1의 두 곳뿐') {
	throw 'The §17.3 two-metas-only rule was removed.'
}
$assertionCount++
if ($story -notmatch '세이브 조작·가짜 크래시 금지') {
	throw 'The §17.1 no-save-tampering rule was removed.'
}
foreach ($banned in @('DeleteSaveThreat', 'FakeCrash', 'CorruptSaveEffect')) {
	$assertionCount++
	if ($hudSource.Contains($banned) -or $controllerSource.Contains($banned)) {
		throw "§17 forbids breaking the player's trust: $banned"
	}
}
# 바디캠 회피의 이유는 남되, FOV는 §18.3에서 열렸다. 두 절이 갈라지면
# 어느 쪽이 지금 규칙인지 알 수 없다.
$assertionCount++
if ($story -match 'FOV 78 고정 유지') {
	throw 'The §17.3 bodycam row still says the FOV is fixed; §18.3 opened it.'
}
$assertionCount++
if ($story -notmatch '기본 FOV 78 유지\(§18\.3에서 68~100으로 열되 기본값은 그대로\)') {
	throw 'The §17.3 bodycam row must point at the §18.3 range.'
}

# --- §14 구현 매핑 --------------------------------------------------------------
#
# 이 절은 설계를 실제 클래스로 잇는 지도다. 이름이 틀리면 지도가 아니라
# 미로가 된다 — 실제로 여섯 개가 틀려 있었다. 셋은 접두사, 둘은 만들지 않은
# 컴포넌트, 하나는 바뀐 시그니처.

$mappingSection = [regex]::Match(
	$story, '## 14\. 구현 매핑[^\r\n]*\r?\n(?<body>[\s\S]*?)\r?\n## 15\.')
$assertionCount++
if (-not $mappingSection.Success) {
	throw 'The §14 implementation map could not be read.'
}
# 헤더 파일 전체에서 선언된 타입 이름을 한 번만 모은다.
$declaredTypes = @{}
foreach ($file in Get-ChildItem -Path (Join-Path $projectRoot 'Source/IndieGame') `
	-Filter '*.h' -Recurse) {
	$text = Get-Content -Raw -Encoding UTF8 -LiteralPath $file.FullName
	foreach ($type in [regex]::Matches(
		$text, 'class(?: INDIEGAME_API)? (?<name>[AUF]IG[A-Za-z0-9]+)')) {
		$declaredTypes[$type.Groups['name'].Value] = $true
	}
}
$assertionCount++
if ($declaredTypes.Count -lt 20) {
	throw 'The type sweep found too few classes to trust (§14).'
}
# 절이 백틱으로 감싼 타입 이름은 전부 실제로 있어야 한다. 만들지 않기로 한
# 것은 이름이 아니라 문장으로 적는다 — 그래야 「어디 있지」로 시간을 안 쓴다.
$mappedTypes = 0
foreach ($mention in [regex]::Matches(
	$mappingSection.Groups['body'].Value, '`(?<name>[AUF]IG[A-Za-z0-9]+)`')) {
	$name = $mention.Groups['name'].Value
	$mappedTypes++
	$assertionCount++
	if (-not $declaredTypes.ContainsKey($name)) {
		throw "§14 maps to a class that does not exist: $name"
	}
}
$assertionCount++
if ($mappedTypes -lt 10) {
	throw '§14 stopped naming the classes it maps to.'
}

# 만들지 않기로 한 둘은 이름으로 남기지 않는다. 백틱 안에 있으면 위 검사가
# 잡지만, 왜 안 만들었는지가 사라지면 다음 사람이 다시 만들려 한다.
$assertionCount++
if ($mappingSection.Groups['body'].Value -notmatch
	'두드리기와 엿듣기는 \*\*컴포넌트로 나누지 않았다\.\*\*') {
	throw 'The §14 note about the two components that were never built was removed.'
}

# 소음 API는 반경을 받지 않는다. 부르는 쪽이 크기와 거리를 따로 정하면
# 「시끄러울수록 멀리 간다」가 깨진다.
$assertionCount++
if ($mappingSection.Groups['body'].Value -notmatch
	'`ReportNoise\(Location,\s*\r?\n?\s*Loudness, Instigator\)`') {
	throw 'The §14 noise API signature drifted from the code.'
}
$reportSignature = [regex]::Match(
	$noiseHeader,
	'FIGNoiseEvent ReportNoise\((?<args>[\s\S]{0,240}?)\);')
$assertionCount++
if (-not $reportSignature.Success) {
	throw 'ReportNoise could not be read from the header.'
}
$assertionCount++
if ($reportSignature.Groups['args'].Value -match 'Radius') {
	throw 'ReportNoise must derive its radius from loudness, not take one (§14).'
}
$assertionCount++
if ($story -notmatch '반경은 인자가 아니라 \*\*소음값에서 나온다\*\*') {
	throw 'The §14 derived-radius rule was removed.'
}

Write-Host (
	'MISSING_FLOOR_BIBLE_CONTRACT PASS assertions={0}' -f $assertionCount) `
	-ForegroundColor Green
