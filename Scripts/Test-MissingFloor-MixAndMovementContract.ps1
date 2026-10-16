[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# STORY_BIBLE_MISSING_FLOOR.md의 남은 숫자 표 둘을 코드 상수와 값으로 대조한다.
#
#   §10.4 거리 리버브 프리셋   복도/계단실/옥상 × 감쇠·HF·확산·첫 반사
#   §18.2 이동감 수치 계약     걷기/앉기/달리기 × 속도·가속·제동·소음
#
# §21.2·§20.2를 잠근 계약과 같은 방식이다. 표를 파싱하고 상수를 파싱해 숫자를
# 맞추므로 어느 쪽이 움직여도 잡힌다.
#
# 두 표는 값만이 아니라 **값들 사이의 관계**가 설계다. 그래서 관계도 같이 본다.
#
#   - 계단실은 복도의 두 배 넘게 울려야 한다. 그 차이가 「계단에서 내 발소리가
#     훨씬 오래 남는다」는 §5.1의 학습을 만든다
#   - ENTITY의 젖음 하한은 0이 아니어야 한다. 완전히 마르는 것은 「같은 방」의
#     뜻으로 예약되어 있고, 그 예외는 포획 노크 하나뿐이다(§21.3)
#   - 달리기 제동만 셋 중 가장 낮아야 한다. 급정지가 안 되는 것이 달린 대가다

$projectRoot = Split-Path -Parent $PSScriptRoot
$assertionCount = 0

function Read-ProjectText {
	param([Parameter(Mandatory = $true)][string]$RelativePath)
	$path = Join-Path $projectRoot $RelativePath
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Missing mix/movement contract file: $RelativePath"
	}
	return Get-Content -Raw -Encoding UTF8 -LiteralPath $path
}

function Get-Section {
	param(
		[Parameter(Mandatory = $true)][string]$Text,
		[Parameter(Mandatory = $true)][string]$StartPattern,
		[Parameter(Mandatory = $true)][string]$EndPattern,
		[Parameter(Mandatory = $true)][string]$Label
	)
	$match = [regex]::Match($Text, "$StartPattern(?<body>[\s\S]*?)$EndPattern")
	if (-not $match.Success) {
		throw "$Label section could not be isolated."
	}
	return $match.Groups['body'].Value
}

function Get-TableRows {
	# 정렬 행을 지나기 전은 전부 머리글이다(§21.2 계약과 같은 규칙).
	param([Parameter(Mandatory = $true)][string]$Section)
	$rows = @()
	$pastHeader = $false
	foreach ($line in ($Section -split "`r?`n")) {
		$trimmed = $line.Trim()
		if (-not $trimmed.StartsWith('|') -or -not $trimmed.EndsWith('|')) {
			continue
		}
		$columns = @($trimmed.Trim('|').Split('|') | ForEach-Object { $_.Trim() })
		if ($columns.Count -lt 2) {
			continue
		}
		if ($columns[0] -match '^:?-{2,}:?$') {
			$pastHeader = $true
			continue
		}
		if (-not $pastHeader) {
			continue
		}
		$rows += ,$columns
	}
	return $rows
}

function ConvertTo-Cell {
	<#
	표의 칸을 숫자로 바꾼다. 없는 값(—)은 $null이다.

	표는 사람이 읽는 표기를 쓴다: `300 (유지)`, `**900**`, `×0.86`, `+0.13`,
	`9ms`. 굵게와 괄호 주석을 떼고, 밀리초는 코드가 쓰는 초로 내린다.
	#>
	param([Parameter(Mandatory = $true)][string]$Cell)
	$clean = $Cell.Trim().Replace('**', '')
	$clean = [regex]::Replace($clean, '\([^)]*\)', '').Trim()
	if ($clean -eq '—' -or $clean -eq '-' -or $clean -eq '') {
		return $null
	}
	$clean = $clean.TrimStart('×', '+')
	if ($clean -match '^(?<value>[0-9]+(?:\.[0-9]+)?)\s*ms$') {
		return [double]$Matches['value'] / 1000.0
	}
	if ($clean -match '^(?<value>[0-9]+(?:\.[0-9]+)?)\s*s$') {
		return [double]$Matches['value']
	}
	if ($clean -match '^(?<value>[0-9]+(?:\.[0-9]+)?)$') {
		return [double]$Matches['value']
	}
	throw "A table cell is not a number: '$Cell'"
}

function Get-Constant {
	<#
	`constexpr ... Name = 1.25f;` 또는 `Object->Field = 0.80f;`의 값.
	#>
	param(
		[Parameter(Mandatory = $true)][string]$Text,
		[Parameter(Mandatory = $true)][string]$Expression
	)
	$escaped = [regex]::Escape($Expression)
	$match = [regex]::Match($Text, "$escaped\s*=\s*(?<value>-?[0-9]+(?:\.[0-9]+)?)f?\s*;")
	if (-not $match.Success) {
		throw "Constant could not be found: $Expression"
	}
	return [double]$match.Groups['value'].Value
}

function Assert-Number {
	param(
		[Parameter(Mandatory = $true)][double]$Expected,
		[Parameter(Mandatory = $true)][double]$Actual,
		[Parameter(Mandatory = $true)][string]$Label
	)
	$script:assertionCount++
	if ([Math]::Abs($Expected - $Actual) -gt 0.0005) {
		throw "$Label differs: doc $Expected, code $Actual."
	}
}

$story = Read-ProjectText 'Docs/STORY_BIBLE_MISSING_FLOOR.md'
$audioSource = Read-ProjectText 'Source/IndieGame/Audio/IGMissingFloorAudioSubsystem.cpp'
$characterSource = Read-ProjectText 'Source/IndieGame/Player/IGPlayerCharacter.cpp'

# --- §10.4 거리 리버브 프리셋 -----------------------------------------------

$reverbSection = Get-Section $story '\*\*확정 파라미터 \(2026-08-12 구현\):\*\*' `
	'복도를 실측보다 짧게 잡은 것은 의도다' '§10.4 리버브 프리셋'
$reverbRows = Get-TableRows $reverbSection

$assertionCount++
if ($reverbRows.Count -ne 3) {
	throw "§10.4 must list three spaces, found $($reverbRows.Count)."
}

# 감쇠와 HF는 namespace 상수 선언에서, 확산과 첫 반사는 프리셋 조립부의
# 대입문에서 읽는다. 호출부는 `IGMissingFloorMix::` 한정 이름을 쓰지만 값이
# 적힌 자리는 선언이다.
$reverbFields = [ordered]@{
	'복도'   = @{
		Decay        = 'constexpr float CorridorDecayTime'
		DecayHFRatio = 'constexpr float CorridorDecayHFRatio'
		Diffusion    = 'Corridor->Diffusion'
		Reflections  = 'Corridor->ReflectionsDelay'
	}
	'계단실' = @{
		Decay        = 'constexpr float StairwellDecayTime'
		DecayHFRatio = 'constexpr float StairwellDecayHFRatio'
		Diffusion    = 'Stairwell->Diffusion'
		Reflections  = 'Stairwell->ReflectionsDelay'
	}
}

foreach ($row in $reverbRows) {
	$space = $row[0]
	if ($row.Count -lt 5) {
		throw "§10.4 row '$space' is missing columns."
	}
	$decay = ConvertTo-Cell $row[1]

	if ($space -eq '옥상') {
		# 옥상은 프리셋이 없다 — 반사면이 바닥뿐이다. 네 칸이 전부 비어야 하고,
		# 코드에도 옥상 프리셋이 있으면 안 된다.
		for ($column = 1; $column -le 4; $column++) {
			$assertionCount++
			if ($null -ne (ConvertTo-Cell $row[$column])) {
				throw '§10.4 옥상 must stay without a reverb preset.'
			}
		}
		$assertionCount++
		if ([regex]::IsMatch($audioSource, 'Rooftop(Decay|Diffusion|Reflections)')) {
			throw 'A rooftop reverb preset appeared; §10.4 says the roof has none.'
		}
		continue
	}

	if (-not $reverbFields.Contains($space)) {
		throw "§10.4 has a space this contract does not map: '$space'"
	}
	$fields = $reverbFields[$space]
	Assert-Number $decay (Get-Constant $audioSource $fields.Decay) `
		"§10.4 '$space' 감쇠"
	Assert-Number (ConvertTo-Cell $row[2]) `
		(Get-Constant $audioSource $fields.DecayHFRatio) "§10.4 '$space' DecayHFRatio"
	Assert-Number (ConvertTo-Cell $row[3]) `
		(Get-Constant $audioSource $fields.Diffusion) "§10.4 '$space' 확산"
	Assert-Number (ConvertTo-Cell $row[4]) `
		(Get-Constant $audioSource $fields.Reflections) "§10.4 '$space' 첫 반사"
}

# 「두 프리셋의 감쇠 비가 2배 이상이라는 것이 계약이다」 — 문서가 명시한 관계다.
$corridorDecay = Get-Constant $audioSource 'constexpr float CorridorDecayTime'
$stairwellDecay = Get-Constant $audioSource 'constexpr float StairwellDecayTime'
$assertionCount++
if (($stairwellDecay / $corridorDecay) -lt 2.0) {
	throw ('The stairwell must ring at least twice as long as the corridor ' +
		"(§10.4): $stairwellDecay / $corridorDecay.")
}
$assertionCount++
if (-not $story.Contains('두 프리셋의 감쇠 비가 2배 이상이라는 것이 계약이다')) {
	throw 'The §10.4 decay-ratio contract sentence was removed.'
}

# 젖음 하한. ENTITY > PUZZLE > WORLD이고 ENTITY는 0이 아니다 — 드라이는
# 「같은 방」의 뜻이라 포획 노크 하나에 예약되어 있다.
$entityFloor = Get-Constant $audioSource 'constexpr float EntityReverbFloor'
$puzzleFloor = Get-Constant $audioSource 'constexpr float PuzzleReverbFloor'
$worldFloor = Get-Constant $audioSource 'constexpr float WorldReverbFloor'
$playerSend = Get-Constant $audioSource 'constexpr float PlayerManualReverbSend'
Assert-Number 0.22 $entityFloor '§10.4 ENTITY 젖음 하한'
Assert-Number 0.15 $puzzleFloor '§10.4 PUZZLE 젖음 하한'
Assert-Number 0.10 $worldFloor '§10.4 WORLD 젖음 하한'
Assert-Number 0.30 $playerSend '§10.4 PLAYER 고정 센드'
$assertionCount++
if ($entityFloor -le 0.0) {
	throw 'ENTITY must never go fully dry outside the capture knock (§10.4).'
}
$assertionCount++
if (-not ($entityFloor -gt $puzzleFloor -and $puzzleFloor -gt $worldFloor)) {
	throw 'The wetness floors must stay ordered ENTITY > PUZZLE > WORLD (§10.4).'
}

# --- §18.2 이동감 수치 계약 -------------------------------------------------

$movementSection = Get-Section $story '### 18\.2 이동감 수치 계약' '### 18\.3' `
	'§18.2 이동감 수치 계약'
$movementRows = Get-TableRows $movementSection

$assertionCount++
if ($movementRows.Count -ne 5) {
	throw "§18.2 must list five states, found $($movementRows.Count)."
}

$movementConstants = [ordered]@{
	'걷기'      = @{
		Speed        = 'constexpr float ReferenceWalkSpeed'
		Acceleration = 'constexpr float WalkAcceleration'
		Braking      = 'constexpr float WalkBraking'
	}
	'앉아 이동' = @{
		Speed        = 'constexpr float CrouchSpeed'
		Acceleration = 'constexpr float CrouchAcceleration'
		Braking      = 'constexpr float CrouchBraking'
	}
	'달리기'    = @{
		Speed        = 'constexpr float SprintSpeed'
		Acceleration = 'constexpr float SprintAcceleration'
		Braking      = 'constexpr float SprintBraking'
	}
}

# 지형 행은 속도 배율이고, 소음 열은 §21.2 장판 기준의 증가분이다.
$terrainScales = @{
	'자재·파편 지대(5층)' = 0.86
	'물 고인 바닥(밤4)'   = 0.78
}
$surfaceScaleBody = Get-Section $characterSource `
	'float AIGPlayerCharacter::GetSurfaceMovementScale\(' 'return 1\.0f;' `
	'Surface movement scale'

foreach ($row in $movementRows) {
	$state = $row[0]
	if ($row.Count -lt 5) {
		throw "§18.2 row '$state' is missing columns."
	}

	if ($terrainScales.ContainsKey($state)) {
		$assertionCount++
		$expectedScale = $terrainScales[$state]
		if (-not $surfaceScaleBody.Contains(("{0:0.00}f" -f $expectedScale))) {
			throw "§18.2 '$state' movement scale $expectedScale is not in the code."
		}
		Assert-Number $expectedScale (ConvertTo-Cell $row[1]) "§18.2 '$state' 배율"
		continue
	}

	if (-not $movementConstants.Contains($state)) {
		throw "§18.2 has a state this contract does not map: '$state'"
	}
	$constants = $movementConstants[$state]
	Assert-Number (ConvertTo-Cell $row[1]) `
		(Get-Constant $characterSource $constants.Speed) "§18.2 '$state' 속도"
	Assert-Number (ConvertTo-Cell $row[2]) `
		(Get-Constant $characterSource $constants.Acceleration) "§18.2 '$state' 가속"
	Assert-Number (ConvertTo-Cell $row[3]) `
		(Get-Constant $characterSource $constants.Braking) "§18.2 '$state' 제동"
}

# 「달리기 제동만 낮게 둔다」 — 급정지가 안 되므로 달린 대가가 코너에서
# 청구된다. 값이 다 맞아도 이 관계가 뒤집히면 그 문장이 거짓이 된다.
$walkBraking = Get-Constant $characterSource 'constexpr float WalkBraking'
$crouchBraking = Get-Constant $characterSource 'constexpr float CrouchBraking'
$sprintBraking = Get-Constant $characterSource 'constexpr float SprintBraking'
$assertionCount++
if (-not ($sprintBraking -lt $walkBraking -and $sprintBraking -lt $crouchBraking)) {
	throw 'Sprint braking must stay the lowest of the three (§18.2).'
}

# 제동값은 제동 마찰을 떼어야 걸린다. 기본 지면 마찰 8×2가 속도에 곱해져 먼저
# 깎으면 세 값은 대조만 되고 화면에는 나오지 않는다.
foreach ($friction in @(
	'MovementComponent->bUseSeparateBrakingFriction = true;',
	'MovementComponent->BrakingFriction = 0.0f;')) {
	$assertionCount++
	if (-not $characterSource.Contains($friction)) {
		throw "The §18.2 braking values need the braking friction detached: $friction"
	}
}
$assertionCount++
if (-not $story.Contains('제동 마찰을 0으로 떼어야')) {
	throw 'The §18.2 braking-friction sentence was removed.'
}

# 손맛 회전은 카메라 매니저 모디파이어로 얹는다. 컴포넌트 상대 회전은 폰 제어
# 회전에 덮여 화면에 안 나온다.
$controllerSource = Get-Content -Raw -Encoding UTF8 (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGPlayerController.cpp')
$assertionCount++
if (-not $controllerSource.Contains(
	'PlayerCameraManager->AddNewCameraModifier(UIGCameraFeelModifier::StaticClass())')) {
	throw 'The camera feel modifier must be registered on the camera manager (§18.3).'
}
$modifierSource = Get-Content -Raw -Encoding UTF8 (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGCameraFeelModifier.cpp')
$assertionCount++
if (-not $modifierSource.Contains('GetCameraFeelRotation()')) {
	throw 'The camera feel modifier must read the pawn feel rotation (§18.3).'
}
$assertionCount++
if (-not $story.Contains('UIGCameraFeelModifier')) {
	throw 'The §18.3 camera modifier sentence was removed.'
}

# 앉기 전환은 즉시가 아니다. 은신 결정에도 비용이 있어야 그 결정이 플레이다.
Assert-Number 0.35 (Get-Constant $characterSource 'constexpr float CrouchTransitionSeconds') `
	'§18.2 앉기 전환 시간'
Assert-Number 0.5 (Get-Constant $characterSource 'constexpr float CrouchTransitionSpeedScale') `
	'§18.2 앉기 전환 속도 배율'
$assertionCount++
if (-not $story.Contains('앉기 전환은 0.35s')) {
	throw 'The §18.2 crouch-transition sentence lost its authored seconds.'
}

# 달리기 제동은 스태미나 게이지가 아니라 소음이 건다(§19의 게이지 금지).
Assert-Number 3.5 `
	(Get-Constant $characterSource 'constexpr float SprintBreathThresholdSeconds') `
	'§18.2 달리기 호흡 문턱'
$assertionCount++
if (-not $characterSource.Contains('BreathLoad * 0.20f')) {
	throw 'The sprint breath load must stay capped at +0.20 (§18.2).'
}
$assertionCount++
if (-not $story.Contains('3.5초 이후 초당')) {
	throw 'The §18.2 sprint-noise sentence lost its authored threshold.'
}

# §18.2의 소음 열은 §21.2 장판 행과 같은 값이어야 한다. 두 표가 같은 사실을
# 다르게 적으면 어느 쪽이 정사인지 알 수 없어진다.
$footstepSection = Get-Section $story '### 21\.2 발소리 매트릭스' '### 21\.3' `
	'§21.2 발소리 매트릭스'
$footstepRows = Get-TableRows $footstepSection
$vinylRow = @($footstepRows | Where-Object { $_[0] -like '장판*' })
$assertionCount++
if ($vinylRow.Count -ne 1) {
	throw 'The §21.2 vinyl row could not be found for the §18.2 cross-check.'
}
$walkRow = @($movementRows | Where-Object { $_[0] -eq '걷기' })[0]
$crouchRow = @($movementRows | Where-Object { $_[0] -eq '앉아 이동' })[0]
$sprintRow = @($movementRows | Where-Object { $_[0] -eq '달리기' })[0]
Assert-Number (ConvertTo-Cell $vinylRow[0][1]) (ConvertTo-Cell $walkRow[4]) `
	'§18.2 걷기 소음 vs §21.2 장판'
Assert-Number (ConvertTo-Cell $vinylRow[0][2]) (ConvertTo-Cell $crouchRow[4]) `
	'§18.2 앉기 소음 vs §21.2 장판'
Assert-Number (ConvertTo-Cell $vinylRow[0][3]) (ConvertTo-Cell $sprintRow[4]) `
	'§18.2 달리기 소음 vs §21.2 장판'


# --- §21.1 버스와 우선순위 --------------------------------------------------
#
# 값 자체는 M6 오디오 계약이 이미 코드 쪽에서 잠그고 있다. 여기서 더하는 것은
# 셋이다.
#
#   1. **문서 쪽 대조.** M6 계약은 `'0.0f,   // ENTITY'`처럼 코드 리터럴만
#      보므로 표가 바뀌어도 아무것도 실패하지 않는다. 표를 파싱해 맞춘다
#   2. **§24 즉시 차단 20.** 존재의 소리는 어떤 것에도 눌리지 않는다.
#      두 믹스 경로 어디에도 ENTITY를 낮추는 분기가 없어야 한다
#   3. **두 경로의 합치.** 더킹 규칙이 RefreshMix(실제 적용)와
#      GetEffectiveBusDecibels(질의·영수증) 두 곳에 적혀 있다. 한쪽만 고치면
#      들리는 믹스와 계약이 보고하는 믹스가 갈라진다

$busSection = Get-Section $story '### 21\.1 버스와 우선순위' '### 21\.2' `
	'§21.1 버스와 우선순위'
$busRows = Get-TableRows $busSection

$assertionCount++
if ($busRows.Count -ne 7) {
	throw "§21.1 must list seven buses, found $($busRows.Count)."
}

# 표의 순서가 EIGAudioBus의 순서이자 BaseDecibels의 인덱스다.
$busBaseDecibels = @([regex]::Matches(
	(Get-Section $audioSource 'constexpr float BaseDecibels\[\] =\s*\{' '\};' `
		'BaseDecibels'),
	'(?<value>-?[0-9]+(?:\.[0-9]+)?)f') |
	ForEach-Object { [double]$_.Groups['value'].Value })
$assertionCount++
if ($busBaseDecibels.Count -ne 7) {
	throw "BaseDecibels must hold seven levels, found $($busBaseDecibels.Count)."
}

for ($busIndex = 0; $busIndex -lt 7; $busIndex++) {
	$row = $busRows[$busIndex]
	if ($row.Count -lt 4) {
		throw "§21.1 row '$($row[0])' is missing columns."
	}
	$busName = $row[0].Trim('`')
	$assertionCount++
	if (-not $audioSource.Contains("TEXT(""$busName"")")) {
		throw "§21.1 names a bus the code does not build: $busName"
	}
	# 표는 사람 표기라 유니코드 빼기(−)와 `dB` 접미사를 쓴다.
	$levelCell = $row[2].Replace([char]0x2212, '-').Replace('dB', '').Trim()
	$assertionCount++
	if ($levelCell -notmatch '^-?[0-9]+(?:\.[0-9]+)?$') {
		throw "§21.1 '$busName' has an unreadable level: '$($row[2])'"
	}
	Assert-Number ([double]$levelCell) $busBaseDecibels[$busIndex] `
		"§21.1 '$busName' 기본"
}

# §24 즉시 차단 20 — 존재의 소리는 절대 눌리지 않는다. 두 믹스 경로 어느
# 쪽에도 ENTITY를 낮추는 분기가 있으면 안 된다.
$duckingBody = Get-Section $audioSource `
	'float UIGMissingFloorAudioSubsystem::GetBusDuckingDecibels\(' `
	'float UIGMissingFloorAudioSubsystem::GetEffectiveBusDecibels' `
	'GetBusDuckingDecibels'
$assertionCount++
if ($duckingBody -match 'Bus\s*==\s*EIGAudioBus::Entity') {
	throw '§24 즉시 차단 20: the entity bus must never be ducked.'
}
$assertionCount++
if (-not $story.Contains('**존재의 소리는 절대 눌리지 않는다.**')) {
	throw 'The §21.1 first principle sentence was removed.'
}

# 더킹 규칙은 한 함수에만 있어야 한다. 예전에는 RefreshMix(실제 적용)와
# GetEffectiveBusDecibels(질의·영수증)에 같은 규칙이 두 벌로 적혀 있었고,
# 한쪽만 조율하면 들리는 믹스와 계약이 보고하는 믹스가 갈라진다 — 그 차이는
# 귀로만 발견된다.
foreach ($duck in @('-4.0f', '-6.0f', '-16.0f')) {
	$assertionCount++
	if (-not $duckingBody.Contains($duck)) {
		throw "The ducking rule lost a step: $duck"
	}
}
foreach ($condition in @(
	'bEntityNearPlayer',
	'bPlayerListening || bEntityListening',
	'bAuthoredSilence')) {
	$assertionCount++
	if (-not $duckingBody.Contains($condition)) {
		throw "The ducking rule lost a condition: $condition"
	}
}

# 두 소비자가 그 함수를 부르는지. 어느 쪽이든 자기 계산을 다시 쓰기 시작하면
# 두 벌이던 시절로 돌아간다.
$refreshMixBody = Get-Section $audioSource `
	'void UIGMissingFloorAudioSubsystem::RefreshMix\(const float FadeSeconds\)' `
	'void UIGMissingFloorAudioSubsystem::PruneVoices' 'RefreshMix'
$effectiveBody = Get-Section $audioSource `
	'float UIGMissingFloorAudioSubsystem::GetEffectiveBusDecibels\(' `
	'int32 UIGMissingFloorAudioSubsystem::GetVoiceCap' 'GetEffectiveBusDecibels'
foreach ($consumer in @(
	@{ Name = 'RefreshMix'; Body = $refreshMixBody },
	@{ Name = 'GetEffectiveBusDecibels'; Body = $effectiveBody })) {
	$assertionCount++
	if (-not $consumer.Body.Contains('GetBusDuckingDecibels(Bus)')) {
		throw ($consumer.Name + ' must read the single ducking rule (§21.1).')
	}
	foreach ($duck in @('-4.0f', '-6.0f', '-16.0f')) {
		$assertionCount++
		if ($consumer.Body.Contains($duck)) {
			throw ($consumer.Name + " re-states a ducking step: $duck")
		}
	}
}

# §21.4의 침묵 바닥. WORLD는 기본 -8에서 -16이 더 내려가 -24에 닿는다.
# 리터럴이 아니라 두 값의 합이므로, 어느 쪽이 움직여도 바닥이 어긋난다.
$assertionCount++
$worldSilence = $busBaseDecibels[3] + (-16.0)
if ([Math]::Abs($worldSilence - (-24.0)) -gt 0.0005) {
	throw ("The authored -24 dB silence floor no longer falls out of the " +
		"world bus: $worldSilence.")
}
$assertionCount++
if (-not $duckingBody.Contains('-24 dB silence floor')) {
	throw 'The silence-floor rationale was removed from the ducking rule.'
}

# PLAYER 더킹의 「존재 6m 이내」. 표의 문장과 상수가 같은 거리를 말해야 한다.
$assertionCount++
if (-not $busRows[1][3].Contains('6m')) {
	throw '§21.1 PLAYER ducking lost its authored six-metre distance.'
}
Assert-Number 600.0 (Get-Constant $audioSource 'constexpr float EntityNearDistance') `
	'§21.1 PLAYER 더킹 거리'

# SCORE는 침묵 구간에서 −∞다. 코드의 −96dB이 그 무한대의 실현이다.
$assertionCount++
if (-not $busRows[5][3].Contains('−∞')) {
	throw '§21.1 SCORE ducking lost its authored silence.'
}
Assert-Number -96.0 (Get-Constant $audioSource 'constexpr float SilentDecibels') `
	'§21.1 SCORE 침묵'

# PUZZLE·UI·SPEECH는 더킹이 없다. 표의 「—」가 코드에도 분기 없음으로 남아야 한다.
foreach ($quietIndex in @(2, 4, 6)) {
	$assertionCount++
	if ($busRows[$quietIndex][3].Trim() -ne '—') {
		throw ("§21.1 '" + $busRows[$quietIndex][0] +
			"' gained a ducking rule this contract does not model.")
	}
}
foreach ($undockedBus in @('EIGAudioBus::Puzzle', 'EIGAudioBus::UI', 'EIGAudioBus::Speech')) {
	$assertionCount++
	if ($refreshMixBody -match [regex]::Escape("Bus == $undockedBus")) {
		throw "A ducking branch appeared for an undocked bus: $undockedBus"
	}
}

# 동시 발음 상한. 표 아래 문장이 네 버스의 값을 적어 두었다.
$voiceCaps = @([regex]::Matches(
	(Get-Section $audioSource 'constexpr int32 VoiceCaps\[\] =\s*\{' '\};' 'VoiceCaps'),
	'(?<value>[0-9]+)\s*,') |
	ForEach-Object { [int]$_.Groups['value'].Value })
$assertionCount++
if ($voiceCaps.Count -lt 4) {
	throw 'VoiceCaps could not be read.'
}
$authoredCaps = [regex]::Match(
	$story, '동시 발음 상한: ENTITY (?<entity>[0-9]+), PLAYER (?<player>[0-9]+), PUZZLE (?<puzzle>[0-9]+), WORLD (?<world>[0-9]+)')
$assertionCount++
if (-not $authoredCaps.Success) {
	throw 'The §21.1 voice-cap sentence could not be read.'
}
Assert-Number ([double]$authoredCaps.Groups['entity'].Value) ([double]$voiceCaps[0]) `
	'§21.1 ENTITY 동시 발음'
Assert-Number ([double]$authoredCaps.Groups['player'].Value) ([double]$voiceCaps[1]) `
	'§21.1 PLAYER 동시 발음'
Assert-Number ([double]$authoredCaps.Groups['puzzle'].Value) ([double]$voiceCaps[2]) `
	'§21.1 PUZZLE 동시 발음'
Assert-Number ([double]$authoredCaps.Groups['world'].Value) ([double]$voiceCaps[3]) `
	'§21.1 WORLD 동시 발음'

# 상한을 넘겨 밀어낼 때는 페이드다. 컷은 금지 — 잘린 소리는 그 자체로
# 사건처럼 들린다.
$assertionCount++
if (-not $audioSource.Contains('Oldest->FadeOut(')) {
	throw 'Voices over the cap must fade, never cut (§21.1).'
}
$assertionCount++
if (-not $story.Contains('**컷 금지**')) {
	throw 'The §21.1 no-cut rule was removed.'
}

# --- 출력 방식 (§10.5) ------------------------------------------------------
#
# 훅이 「위에서 나는 소리」라 기본은 바이노럴이다. 그런데 바이노럴을 스피커로
# 틀면 좌우가 서로 새어 위아래가 오히려 뭉개진다. 스피커를 막지 않기로 한
# 이상, 스피커로 듣는다고 말할 자리가 있어야 한다.

$helperSource = Get-Content -Raw -Encoding UTF8 (
	Join-Path $projectRoot 'Source/IndieGame/Audio/IGAudioHelpers.cpp')

# 감쇠가 알고리즘을 박아 두면 설정이 있어도 아무 일도 일어나지 않는다.
$assertionCount++
if ($helperSource -match 'SpatializationAlgorithm\s*=\s*SPATIALIZATION_HRTF') {
	throw 'Attenuation must not hard-code HRTF; it reads the output mode (§10.5).'
}
$assertionCount++
if (-not $helperSource.Contains(
	'Settings.SpatializationAlgorithm = GetSpatializationAlgorithm()')) {
	throw 'Attenuation must take its algorithm from the output mode (§10.5).'
}

# 두 방식이 실제로 다른 알고리즘으로 갈라지는가. 갈라지지 않으면 화면의
# 글자만 바뀌고 소리는 그대로다.
$algorithmBody = [regex]::Match(
	$helperSource,
	'ESoundSpatializationAlgorithm GetSpatializationAlgorithm\(\)(?<body>[\s\S]*?)\r?\n\t\}')
$assertionCount++
if (-not $algorithmBody.Success) {
	throw 'GetSpatializationAlgorithm could not be isolated.'
}
foreach ($branch in @('SPATIALIZATION_HRTF', 'SPATIALIZATION_Default')) {
	$assertionCount++
	if (-not $algorithmBody.Groups['body'].Value.Contains($branch)) {
		throw "Output mode must pick a distinct algorithm: $branch"
	}
}

# 새로 나는 소리만 바꾸면 이미 돌고 있는 환경음 루프가 옛 방식으로 남는다.
$outputBody = [regex]::Match(
	$audioSource,
	'void UIGMissingFloorAudioSubsystem::SetHeadphoneOutput\(const bool bHeadphones\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $outputBody.Success) {
	throw 'SetHeadphoneOutput could not be isolated.'
}
foreach ($piece in @(
	'IGAudio::SetOutputMode(',
	'ActiveVoices[BusIndex]',
	'AdjustAttenuation(')) {
	$assertionCount++
	if (-not $outputBody.Groups['body'].Value.Contains($piece)) {
		throw "Switching output must re-apply to live voices: $piece"
	}
}

# 기본은 헤드폰이다. §10.5가 스피커를 막지 않는 것이지 권하는 것이 아니다.
$audioHeader = Get-Content -Raw -Encoding UTF8 (
	Join-Path $projectRoot 'Source/IndieGame/Audio/IGMissingFloorAudioSubsystem.h')
$assertionCount++
if (-not $audioHeader.Contains('bool bHeadphoneOutput = true;')) {
	throw 'Headphones must remain the default output mode (§10.5).'
}
$controllerSource = Get-Content -Raw -Encoding UTF8 (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGPlayerController.cpp')
foreach ($half in @(
	@{ Name = 'LoadAudioCalibrationSettings'; Call = 'GConfig->GetBool(' },
	@{ Name = 'CompleteAudioCalibration'; Call = 'GConfig->SetBool(' })) {
	$halfBody = [regex]::Match(
		$controllerSource,
		('void AIGPlayerController::{0}\(\)' -f $half.Name) +
			'(?<body>[\s\S]*?)\r?\n\}')
	$assertionCount++
	if (-not $halfBody.Success) {
		throw ('{0} could not be isolated.' -f $half.Name)
	}
	$assertionCount++
	if ($halfBody.Groups['body'].Value -notmatch
		([regex]::Escape($half.Call) + '\s*\r?\n\s*IGAudioCalibration::ConfigSection,' +
			'\s*\r?\n\s*TEXT\("HeadphoneOutput"\)')) {
		throw ('The output mode must persist to user settings: {0} (§10.5).' -f $half.Name)
	}
}
$assertionCount++
if ($story -notmatch '스피커 플레이도 막지 않는다') {
	throw 'The §10.5 speakers-allowed rule was removed.'
}

# --- 자막 방위 딱지 (§10.5) -------------------------------------------------
#
# 「자막 모드는 소리 방위를 병기한다」가 그동안은 대사에 「뒤쪽」을 손으로
# 써 넣는 것이 전부였다. 손으로 쓴 방위는 플레이어가 돌아서면 그대로 틀린다.

$hudSourceForBearing = Get-Content -Raw -Encoding UTF8 (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGHorrorHUD.cpp')
$bearingBody = [regex]::Match(
	$hudSourceForBearing,
	'FText AIGHorrorHUD::MakeSoundBearingTag\((?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $bearingBody.Success) {
	throw 'MakeSoundBearingTag could not be isolated.'
}
$bearingText = $bearingBody.Groups['body'].Value

# 훅이 「위에서 나는 소리」다. 고도가 좌우보다 먼저 결정되지 않으면 이 게임의
# 정체성이 자막에서 사라진다.
$elevationAt = $bearingText.IndexOf('SoundBearingElevationDegrees')
$yawAt = $bearingText.IndexOf('FindDeltaAngleDegrees')
$assertionCount++
if ($elevationAt -lt 0 -or $yawAt -lt 0) {
	throw 'Bearing must weigh both elevation and yaw (§10.5).'
}
$assertionCount++
if ($elevationAt -gt $yawAt) {
	throw 'Elevation must decide before yaw; up and down are the hook (§10.5).'
}

# 보이는 소리에 딱지를 붙이면 읽을 것만 늘어난다.
$assertionCount++
if ($bearingText -notmatch
	'AbsoluteYaw <= SoundBearingOnScreenDegrees\)\s*\r?\n\s*\{\s*\r?\n\s*return FText::GetEmpty\(\);') {
	throw 'Sounds already on screen must carry no bearing tag (§10.5).'
}

foreach ($word in @('"위"', '"아래"', '"뒤"', '"왼쪽"', '"오른쪽"')) {
	$assertionCount++
	if (-not $bearingText.Contains($word)) {
		throw "The bearing vocabulary is incomplete: $word"
	}
}

# 딱지를 붙이는 자리에서 문장이 방위를 또 말하면 「[뒤] 뒤쪽에서 물이 튀는
# 소리」가 된다. 실제로 두 자리가 그렇게 되어 있었다.
$bearingWords = @('뒤쪽', '앞쪽', '왼쪽', '오른쪽', '위층', '아래층', '위에서', '아래에서')
$spatialCallCount = 0
$sourceRoot = Join-Path $projectRoot 'Source/IndieGame'
foreach ($file in Get-ChildItem -Path $sourceRoot -Filter '*.cpp' -Recurse) {
	$text = Get-Content -Raw -Encoding UTF8 -LiteralPath $file.FullName
	foreach ($call in [regex]::Matches(
		$text, '(?<!void )AIGHorrorHUD::Push(?:Background)?AudioCaptionAt\((?<args>[\s\S]{0,400}?)\);')) {
		$spatialCallCount++
		foreach ($literal in [regex]::Matches(
			$call.Groups['args'].Value, '"(?<text>[^"]*)"')) {
			foreach ($word in $bearingWords) {
				$assertionCount++
				if ($literal.Groups['text'].Value.Contains($word)) {
					throw (
						'A located caption must not also spell out the direction: ' +
						('{0} in {1}' -f $word, $file.Name))
				}
			}
		}
	}
}
$assertionCount++
if ($spatialCallCount -lt 7) {
	throw (
		'The §10.5 bearing lane is barely wired: only {0} located captions.' -f
			$spatialCallCount)
}

# --- 사용자가 줄일 수 있는 버스 (§21.1) -------------------------------------
#
# 대원칙은 「존재의 소리는 절대 눌리지 않는다」다. 더킹에 ENTITY 분기가 없는
# 것과 같은 이유로, 사용자 음량에도 ENTITY 분기가 없어야 한다. 놓친 노크가
# 믹스의 여유처럼 보이면 그건 게임의 실패다.

$scaleBody = [regex]::Match(
	$audioSource,
	'float UIGMissingFloorAudioSubsystem::GetBusUserScale\(const EIGAudioBus Bus\) const(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $scaleBody.Success) {
	throw 'GetBusUserScale could not be isolated.'
}
$scaleText = $scaleBody.Groups['body'].Value
foreach ($locked in @('Entity', 'Player', 'Puzzle', 'Ui')) {
	$assertionCount++
	if ($scaleText -match ('EIGAudioBus::{0}\b' -f $locked)) {
		throw "The user must not be able to turn down BUS_$locked (§21.1)."
	}
}
foreach ($open in @('Score', 'World')) {
	$assertionCount++
	if ($scaleText -notmatch ('EIGAudioBus::{0}\b' -f $open)) {
		throw "BUS_$open should be user-adjustable (§21.1)."
	}
}

# 배율이 실제로 믹스에 걸리는가.
$refreshBody = [regex]::Match(
	$audioSource,
	'void UIGMissingFloorAudioSubsystem::RefreshMix\(const float FadeSeconds\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $refreshBody.Success) {
	throw 'RefreshMix could not be isolated.'
}
$assertionCount++
if (-not $refreshBody.Groups['body'].Value.Contains('GetBusUserScale(Bus)')) {
	throw 'The per-bus user scale must reach the running mix (§21.1).'
}

# 음악은 0까지, 환경음은 바닥까지. 건물이 내는 소리 자체가 단서라서 끌 수 없다.
$scoreSetter = [regex]::Match(
	$audioSource,
	'void UIGMissingFloorAudioSubsystem::SetScoreUserVolume\((?<body>[\s\S]*?)\r?\n\}')
$ambienceSetter = [regex]::Match(
	$audioSource,
	'void UIGMissingFloorAudioSubsystem::SetAmbienceUserVolume\((?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $scoreSetter.Success -or -not $ambienceSetter.Success) {
	throw 'The score and ambience setters could not be isolated.'
}
$assertionCount++
if ($scoreSetter.Groups['body'].Value -notmatch
	'FMath::Clamp\(Volume01, 0\.0f, 1\.0f\)') {
	throw 'Music must be allowed to reach silence (§21.1).'
}
$assertionCount++
if ($ambienceSetter.Groups['body'].Value -notmatch
	'FMath::Clamp\(Volume01, MinimumAmbienceVolume, 1\.0f\)') {
	throw 'Ambience must keep its floor; the building itself is a clue (§21.1).'
}
$assertionCount++
if ($audioHeader -notmatch 'MinimumAmbienceVolume = 0\.40f') {
	throw 'The ambience floor moved without the contract following (§21.1).'
}

# 화면의 단계표가 그 범위와 같은 말을 하는가.
$assertionCount++
if ($controllerSource -notmatch
	'MusicValues\[MusicStepCount\] =\s*\r?\n\s*\{\s*\r?\n\s*0\.0f,') {
	throw 'The music step table must start at silence (§21.1).'
}
$assertionCount++
if ($controllerSource -notmatch
	'AmbienceValues\[AmbienceStepCount\] =\s*\r?\n\s*\{\s*\r?\n\s*0\.40f,') {
	throw 'The ambience step table must start at the declared floor (§21.1).'
}
$assertionCount++
if ($story -notmatch '존재의 소리는 절대 눌리지 않는다') {
	throw 'The §21.1 first principle was removed.'
}

# --- §21.4 침묵과 더킹 --------------------------------------------------------
#
# 여기 숫자는 §21.1 표의 기본값과 **더해져야** 맞는다. 두 자리를 따로 읽으면
# 한쪽만 옮겨도 아무 데서도 안 걸린다 — 계산해서 본다.

$hudSourceForMix = Get-Content -Raw -Encoding UTF8 (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGHorrorHUD.cpp')

# 침묵 2회에서 WORLD가 내려가는 바닥.
$silenceRow = [regex]::Match(
	$story,
	'침묵 2회\(§10\.2\)는 볼륨 페이드가 아니라 \*\*버스 정지\*\*다: `BUS_SCORE` −∞,\s*\r?\n?\s*`BUS_WORLD` (?<floor>-?−?[0-9]+)dB')
$assertionCount++
if (-not $silenceRow.Success) {
	throw 'The §21.4 silence floor could not be read.'
}
# 문서는 전각 마이너스를 쓴다. 숫자만 떼어 부호를 붙인다.
$silenceFloor = -[double]($silenceRow.Groups['floor'].Value -replace '[^0-9]', '')

# §21.1 표의 WORLD 기본값.
$worldBaseRow = [regex]::Match(
	$story, '\| `BUS_WORLD` \| [^|]+ \| (?<base>-?−?[0-9]+) \|')
$assertionCount++
if (-not $worldBaseRow.Success) {
	throw 'The §21.1 WORLD base could not be read.'
}
$worldBase = -[double]($worldBaseRow.Groups['base'].Value -replace '[^0-9]', '')

# 코드의 더킹 값.
$duckBody = [regex]::Match(
	$audioSource,
	'float UIGMissingFloorAudioSubsystem::GetBusDuckingDecibels\(\s*\r?\n?\s*const EIGAudioBus Bus\) const(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $duckBody.Success) {
	throw 'GetBusDuckingDecibels could not be isolated.'
}
$silenceDuck = [regex]::Match(
	$duckBody.Groups['body'].Value,
	'Bus == EIGAudioBus::World && bAuthoredSilence\)[\s\S]{0,400}?return (?<value>-?[0-9.]+)f;')
$assertionCount++
if (-not $silenceDuck.Success) {
	throw 'The authored-silence ducking for WORLD could not be read.'
}
$assertionCount++
if (($worldBase + [double]$silenceDuck.Groups['value'].Value) -ne $silenceFloor) {
	throw (
		'WORLD lands at {0}dB in silence ({1} base {2} duck) but §21.4 says {3}dB.' -f
			($worldBase + [double]$silenceDuck.Groups['value'].Value),
			$worldBase, $silenceDuck.Groups['value'].Value, $silenceFloor)
}

# 침묵에서도 플레이어 호흡은 남는다. PLAYER에 침묵 분기가 생기면 그 약속이
# 깨진다 — 숨소리까지 사라지면 그건 침묵이 아니라 음소거다.
$assertionCount++
if ($duckBody.Groups['body'].Value -match
	'Bus == EIGAudioBus::Player && bAuthoredSilence') {
	throw 'Authored silence must leave the player breathing (§21.4).'
}
$assertionCount++
if ($story -notmatch '플레이어 호흡만 남긴다') {
	throw 'The §21.4 breathing rule was removed.'
}

# LISTENING 창의 WORLD −6dB가 「지금 들리고 있다」의 유일한 신호다.
$listenRow = [regex]::Match(
	$story,
	'LISTENING 창의 `BUS_WORLD` (?<value>-?−?[0-9]+)dB이 "지금 들리고 있다"를 알리는 \*\*유일한')
$assertionCount++
if (-not $listenRow.Success) {
	throw 'The §21.4 listening-window row could not be read.'
}
$listenDuck = -[double]($listenRow.Groups['value'].Value -replace '[^0-9]', '')
$listenDeclared = [regex]::Match(
	$duckBody.Groups['body'].Value,
	'bPlayerListening \|\| bEntityListening\)\)[\s\S]{0,200}?return (?<value>-?[0-9.]+)f;')
$assertionCount++
if (-not $listenDeclared.Success) {
	throw 'The listening ducking could not be read.'
}
$assertionCount++
if ([double]$listenDeclared.Groups['value'].Value -ne $listenDuck) {
	throw (
		'The listening window ducks WORLD by {0}dB but §21.4 says {1}dB.' -f
			$listenDeclared.Groups['value'].Value, $listenDuck)
}
# 유일한 신호다. 화면이 같은 말을 하면 믹스가 상태를 말한다는 문장이 거짓이
# 된다 — UI 대신 믹스로 알리기로 한 자리다.
foreach ($banned in @(
	'DrawListeningIndicator', 'ListeningIcon', 'bDrawListeningState')) {
	$assertionCount++
	if ($hudSourceForMix.Contains($banned)) {
		throw "The listening window is told by the mix, not the HUD (§21.4): $banned"
	}
}
$assertionCount++
if ($story -notmatch 'UI 대신 믹스가 상태를 말한다') {
	throw 'The §21.4 mix-over-UI rule was removed.'
}

# 스팅 상한과 전대역 폭음 금지는 §10.4에서 이어받은 규칙이다. 값을 재는
# 것은 §21.5의 실측이고, 여기서는 규칙이 두 자리에 살아 있는지만 본다.
$assertionCount++
if ($story -notmatch '스팅 상한 직전 베드 \+6dB, 전대역 폭음 금지\(§10\.4 계승\)') {
	throw 'The §21.4 sting ceiling was removed.'
}
$assertionCount++
if ($story -notmatch '스팅은 직전 베드 \+6dB 상한 유지\. 전대역 폭음 금지 유지\.') {
	throw 'The §10.4 sting ceiling this section inherits was removed.'
}

Write-Host (
	'MISSING_FLOOR_MIX_MOVEMENT_CONTRACT PASS buses={0} spaces={1} states={2} assertions={3}' -f `
		$busRows.Count, $reverbRows.Count, $movementRows.Count, $assertionCount) `
	-ForegroundColor Green
