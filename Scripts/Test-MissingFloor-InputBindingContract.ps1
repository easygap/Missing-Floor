[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 입력과 조작감의 계약. 키 재설정(§19.8), 시점 감도와 패드 문법·헤드밥(§18.3),
# 상호작용과 두드리기의 손맛(§18.4·§18.5), 햅틱(§18.6), 조작감 합격식(§18.7),
# M0 입력 실행(§27)까지.
#
# 바이블의 나머지 절은 `Test-MissingFloor-BibleContract.ps1`이 본다.
#
# 키 재설정 검사가 맨 앞에 있는 것은 만들면서 바로 깨졌기 때문이다. 재설정 표에
# `LoadAutosave`의 패드 기본값을 Y로 적었는데, Y는 기록 보기이 이미 쓰고
# 있었다. 한 장치에서 두 동사가 같은 버튼을 쓰는 것을 재설정 화면이 금지하는데
# **기본값이 그 규칙을 어기고 있었다.**
#
# 그래서 두 방향을 본다.
#
#   1. 표의 기본값 == `Config/DefaultInput.ini`. 한쪽만 고치면 처음 실행한
#      사람은 표에 없는 키를 쓰게 된다
#   2. 표 안에서 같은 장치에 중복 키가 없다. 기본값 자체가 규칙을 지켜야 한다

$projectRoot = Split-Path -Parent $PSScriptRoot
$assertionCount = 0

function Read-ProjectText {
	param([Parameter(Mandatory = $true)][string]$RelativePath)
	$path = Join-Path $projectRoot $RelativePath
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Missing input-binding contract file: $RelativePath"
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

$bindingSource = Read-ProjectText 'Source/IndieGame/Player/IGInputBindingSubsystem.cpp'
$bindingHeader = Read-ProjectText 'Source/IndieGame/Player/IGInputBindingSubsystem.h'
$inputConfig = Read-ProjectText 'Config/DefaultInput.ini'
$controllerSource = Read-ProjectText 'Source/IndieGame/Player/IGPlayerController.cpp'
$hudSource = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.cpp'

# --- 표를 읽는다 ------------------------------------------------------------

$tableBody = [regex]::Match(
	$bindingSource,
	'static const TArray<FIGBindableActionInfo> Table = \{(?<body>[\s\S]*?)\n\t\t\};')
if (-not $tableBody.Success) {
	throw 'The bindable action table could not be isolated.'
}
$entries = @([regex]::Matches(
	$tableBody.Groups['body'].Value,
	'TEXT\("(?<action>[A-Za-z]+)"\)[\s\S]*?EKeys::(?<keyboard>[A-Za-z_0-9]+),[\s\S]*?EKeys::(?<gamepad>[A-Za-z_0-9]+),'))

$assertionCount++
if ($entries.Count -lt 10) {
	throw "The bindable action table lost entries: found $($entries.Count)."
}

# --- 1. 기본값이 DefaultInput.ini와 같은가 ---------------------------------

foreach ($entry in $entries) {
	$action = $entry.Groups['action'].Value
	$configured = @([regex]::Matches(
		$inputConfig,
		"ActionName=""$action"",[^)]*Key=(?<key>[A-Za-z_0-9]+)") |
		ForEach-Object { $_.Groups['key'].Value })
	$assertionCount++
	if ($configured.Count -lt 1) {
		throw "The action table names an action with no default mapping: $action"
	}
	foreach ($column in @('keyboard', 'gamepad')) {
		$tableKey = $entry.Groups[$column].Value
		$assertionCount++
		if ($tableKey -eq 'Invalid') {
			# 기본값이 없다고 적었으면 설정에도 그 장치의 매핑이 없어야 한다.
			$hasPad = @($configured | Where-Object { $_ -like 'Gamepad_*' }).Count -gt 0
			$hasKeyboard = @($configured | Where-Object { $_ -notlike 'Gamepad_*' }).Count -gt 0
			if (($column -eq 'gamepad' -and $hasPad) -or
				($column -eq 'keyboard' -and $hasKeyboard)) {
				throw ("$action declares no $column default but DefaultInput.ini " +
					'binds one.')
			}
			continue
		}
		if ($configured -notcontains $tableKey) {
			throw ("$action $column default is $tableKey, which is not in " +
				'DefaultInput.ini.')
		}
	}
}

# --- 2. 기본값 자체가 재설정 규칙을 지키는가 --------------------------------

foreach ($column in @('keyboard', 'gamepad')) {
	$seen = @{}
	foreach ($entry in $entries) {
		$key = $entry.Groups[$column].Value
		if ($key -eq 'Invalid') {
			continue
		}
		$assertionCount++
		if ($seen.ContainsKey($key)) {
			throw ("Two actions share the same $column default: " +
				"$($seen[$key]) and $($entry.Groups['action'].Value) both use $key.")
		}
		$seen[$key] = $entry.Groups['action'].Value
	}
}

# 두드리기와 조사가 같은 키를 쓰면 §24 즉시 차단 16이 깨진다. 위의 중복
# 검사가 이미 막지만, 그 규칙이 왜 있는지를 이름으로 남겨 둔다.
$assertionCount++
$knock = @($entries | Where-Object { $_.Groups['action'].Value -eq 'Knock' })
$interact = @($entries | Where-Object { $_.Groups['action'].Value -eq 'Interact' })
if ($knock.Count -ne 1 -or $interact.Count -ne 1) {
	throw 'Knock and Interact must both be rebindable actions.'
}
if ($knock[0].Groups['keyboard'].Value -eq $interact[0].Groups['keyboard'].Value) {
	throw '§24 즉시 차단 16: knock must never share a key with interact.'
}

# --- 3. 규칙이 코드에 있는가 ------------------------------------------------

Assert-ContainsAll $bindingHeader @(
	'bool TryRebind(',
	'void ResetToDefaults();',
	'static bool IsReservedKey(const FKey& Key);',
	'bool HasAnyOverride() const'
) '재설정 API'

# 거절할 때는 왜인지 말한다. 「안 됩니다」만 말하는 화면은 고장과 구분되지 않는다.
Assert-ContainsAll $bindingSource @(
	'OutFailureReason',
	'RebindReserved',
	'RebindConflict',
	'RebindNeedsPad',
	'RebindNeedsKeyboard'
) '거절 사유'

# 나가는 문은 잠글 수 없다.
Assert-ContainsAll $bindingSource @(
	'Key == EKeys::Escape',
	'Key == EKeys::F10'
) '고정 키'

# DefaultInput.ini는 패키지에서 읽기 전용이고 다음 사람의 기본값이다.
# 재설정은 사용자 설정에만 남아야 한다.
$assertionCount++
if ($bindingSource.Contains('SaveKeyMappings()')) {
	throw 'Rebinding must not write DefaultInput.ini; user overrides live in GameUserSettings.'
}
Assert-ContainsAll $bindingSource @(
	'GGameUserSettingsIni',
	'ForceRebuildKeymaps()'
) '재설정 영속화'

# 저장이 규칙을 어기고 있으면 버린다. 남겨 두면 영영 눌리지 않는 키를
# 화면이 「지금 이 키」라고 보여 준다.
$loadBody = [regex]::Match(
	$bindingSource,
	'void UIGInputBindingSubsystem::LoadOverrides\(\)(?<body>[\s\S]*?)\r?\n\}')
if (-not $loadBody.Success) {
	throw 'LoadOverrides could not be isolated.'
}
foreach ($guard in @('IsReservedKey(StoredKey)', 'StoredKey.IsGamepadKey()')) {
	$assertionCount++
	if (-not $loadBody.Groups['body'].Value.Contains($guard)) {
		throw "Stored overrides must be re-validated on load: $guard"
	}
}

# --- 4. 화면이 있는가 -------------------------------------------------------

Assert-ContainsAll $controllerSource @(
	'EIGSystemMenuMode::KeyBindings',
	'UIGInputBindingSubsystem'
) '재설정 화면 진입'
Assert-ContainsAll $hudSource @(
	'bSystemMenuIsKeyBindings'
) '재설정 화면 렌더'

# --- 5. 시점 감도와 상하 반전 -----------------------------------------------
#
# 1인칭에서 감도를 못 바꾸면 손이 맞지 않는 사람은 그 자리에서 끝난다.
# 마우스와 패드는 곡선이 달라 값이 둘이어야 한다 — 하나로 묶으면 한쪽을
# 맞추는 순간 다른 쪽이 어긋난다.

Assert-ContainsAll $bindingSource @(
	'AdjustMouseSensitivity',
	'AdjustGamepadSensitivity',
	'ToggleInvertLookY'
) '시점 조절 진입점'

foreach ($axis in @('Mouse', 'Gamepad')) {
	$adjustBody = [regex]::Match(
		$bindingSource,
		('void UIGInputBindingSubsystem::Adjust{0}Sensitivity\(' -f $axis) +
			'const int32 Direction\)(?<body>[\s\S]*?)\r?\n\}')
	$assertionCount++
	if (-not $adjustBody.Success) {
		throw "Adjust${axis}Sensitivity could not be isolated."
	}
	$assertionCount++
	if ($adjustBody.Groups['body'].Value -notmatch
		('{0}Sensitivity\s*=\s*FMath::Clamp' -f $axis)) {
		throw "Adjust${axis}Sensitivity must clamp to the declared range."
	}
	$assertionCount++
	if (-not $adjustBody.Groups['body'].Value.Contains('LookSensitivityStep')) {
		throw "Adjust${axis}Sensitivity must move by the declared step."
	}
}

# 저장한 값도 다시 조인다. 손으로 고친 ini가 화면을 못 쓰게 만들면 안 된다.
$lookLoadBody = [regex]::Match(
	$bindingSource,
	'void UIGInputBindingSubsystem::LoadLookSettings\(\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $lookLoadBody.Success) {
	throw 'LoadLookSettings could not be isolated.'
}
$assertionCount++
if (([regex]::Matches(
		$lookLoadBody.Groups['body'].Value, 'FMath::Clamp')).Count -lt 2) {
	throw 'Stored look sensitivities must be re-clamped on load.'
}

# 되돌리기가 키만 되돌리고 감도를 남기면 「전부 기본값으로」가 거짓말이 된다.
$resetBody = [regex]::Match(
	$bindingSource,
	'void UIGInputBindingSubsystem::ResetToDefaults\(\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $resetBody.Success) {
	throw 'ResetToDefaults could not be isolated.'
}
foreach ($restored in @(
	'MouseSensitivity = 1.0f',
	'GamepadSensitivity = 1.0f',
	'bInvertLookY = false')) {
	$assertionCount++
	if (-not $resetBody.Groups['body'].Value.Contains($restored)) {
		throw "Reset-all must also restore look settings: $restored"
	}
}

# 값이 실제로 시점에 걸리는가. 설정만 있고 카메라가 안 읽으면 아무 일도
# 일어나지 않는 화면이 된다.
$characterSource = Get-Content -Raw -Encoding UTF8 (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGPlayerCharacter.cpp')
Assert-ContainsAll $characterSource @(
	'AddControllerYawInput(Value * GetLookSensitivity())',
	'IsLookInverted()'
) '시점 적용'

# Turn/LookUp은 마우스와 스틱을 같은 축으로 받는다. 장치를 안 가리면 한쪽
# 값이 다른 장치에도 걸린다.
$sensitivityBody = [regex]::Match(
	$characterSource,
	'float AIGPlayerCharacter::GetLookSensitivity\(\) const(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $sensitivityBody.Success) {
	throw 'GetLookSensitivity could not be isolated.'
}
foreach ($branch in @(
	'IsUsingGamepadLook()',
	'GetGamepadSensitivity()',
	'GetMouseSensitivity()')) {
	$assertionCount++
	if (-not $sensitivityBody.Groups['body'].Value.Contains($branch)) {
		throw "Look sensitivity must pick per device: $branch"
	}
}
# 장치 판정 자체는 게임이 이미 들고 있는 값을 봐야 한다. 여기가 따로 놀면
# 화면은 패드 안내를 보여 주는데 시점은 마우스 감도로 도는 일이 생긴다.
$deviceBody = [regex]::Match(
	$characterSource,
	'bool AIGPlayerCharacter::IsUsingGamepadLook\(\) const(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $deviceBody.Success) {
	throw 'IsUsingGamepadLook could not be isolated.'
}
$assertionCount++
if (-not $deviceBody.Groups['body'].Value.Contains('IsUsingGamepadForHud()')) {
	throw 'The look device must follow the HUD device judgement.'
}

# --- §18.3 패드 시점 문법 ---------------------------------------------------
#
# 데드존·응답 곡선·최대 회전은 문서가 숫자까지 적어 둔 것이고, 그동안
# 코드에는 한 줄도 없었다.

$story = Read-ProjectText 'Docs/STORY_BIBLE_MISSING_FLOOR.md'
$padRow = [regex]::Match(
	$story,
	'데드존 내측 (?<inner>[0-9.]+) 외측 (?<outer>[0-9.]+), 응답 곡선 지수 (?<curve>[0-9.]+), 최대 회전 (?<rate>[0-9]+)')
$assertionCount++
if (-not $padRow.Success) {
	throw 'The §18.3 gamepad look row could not be read.'
}
$padValues = @{
	'PadInnerDeadzone' = $padRow.Groups['inner'].Value
	'PadOuterDeadzone' = $padRow.Groups['outer'].Value
	'PadResponseExponent' = $padRow.Groups['curve'].Value
	'PadMaximumTurnRateDegrees' = $padRow.Groups['rate'].Value
}
foreach ($name in $padValues.Keys) {
	$declared = [regex]::Match(
		$characterSource,
		('constexpr float {0} = (?<value>[0-9.]+)f;' -f $name))
	$assertionCount++
	if (-not $declared.Success) {
		throw "The §18.3 gamepad constant is missing: $name"
	}
	$assertionCount++
	if ([double]$declared.Groups['value'].Value -ne [double]$padValues[$name]) {
		throw (
			'{0} is {1} but §18.3 says {2}.' -f
				$name, $declared.Groups['value'].Value, $padValues[$name])
	}
}

# 데드존은 원시 스틱에 걸어야 한다. 들어온 축 값에 걸면 마우스가 섞여 있어
# 값을 되돌릴 수 없다.
$shapeBody = [regex]::Match(
	$characterSource,
	'float AIGPlayerCharacter::ShapeGamepadLookAxis\(const float RawStick\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $shapeBody.Success) {
	throw 'ShapeGamepadLookAxis could not be isolated.'
}
foreach ($piece in @(
	'IGPlayerNoise::PadInnerDeadzone',
	'IGPlayerNoise::PadOuterDeadzone',
	'FMath::Pow(Normalized, IGPlayerNoise::PadResponseExponent)')) {
	$assertionCount++
	if (-not $shapeBody.Groups['body'].Value.Contains($piece)) {
		throw "The stick curve must use the declared §18.3 shape: $piece"
	}
}
$applyBody = [regex]::Match(
	$characterSource,
	'bool AIGPlayerCharacter::ApplyGamepadLook\((?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $applyBody.Success) {
	throw 'ApplyGamepadLook could not be isolated.'
}
$assertionCount++
if (-not $applyBody.Groups['body'].Value.Contains('PlayerInput->GetKeyValue(StickAxis)')) {
	throw 'The deadzone must read the raw stick, not the blended axis (§18.3).'
}
# 초당 회전 상한. DeltaSeconds가 빠지면 프레임률이 곧 감도가 된다.
$assertionCount++
if (-not $applyBody.Groups['body'].Value.Contains('World->GetDeltaSeconds()')) {
	throw 'The pad turn rate must be measured per second (§18.3).'
}
$assertionCount++
if (-not $applyBody.Groups['body'].Value.Contains(
	'IGPlayerNoise::PadMaximumTurnRateDegrees')) {
	throw 'The pad turn rate cap is not applied (§18.3).'
}
# 각도 그대로 넣는다는 전제가 설정에 남아 있는가.
$assertionCount++
if ($inputConfig -notmatch 'bEnableLegacyInputScales=False') {
	throw 'Legacy input scales must stay off or the turn-rate cap breaks (§18.3).'
}

# 수직 별도 배율. 문서가 폭까지 적어 두었다.
$verticalRow = [regex]::Match(
	$story, '수직 별도 배율 (?<min>[0-9]+\.[0-9]+)~(?<max>[0-9]+\.[0-9]+)')
$assertionCount++
if (-not $verticalRow.Success) {
	throw 'The §18.3 vertical look scale row could not be read.'
}
foreach ($bound in @(
	@{ Name = 'MinimumVerticalLookScale'; Value = $verticalRow.Groups['min'].Value },
	@{ Name = 'MaximumVerticalLookScale'; Value = $verticalRow.Groups['max'].Value })) {
	$declared = [regex]::Match(
		$bindingHeader, ('{0} = (?<value>[0-9.]+)f;' -f $bound.Name))
	$assertionCount++
	if (-not $declared.Success) {
		throw ('The vertical look bound is missing: {0}' -f $bound.Name)
	}
	$assertionCount++
	if ([double]$declared.Groups['value'].Value -ne [double]$bound.Value) {
		throw (
			'{0} is {1} but §18.3 says {2}.' -f
				$bound.Name, $declared.Groups['value'].Value, $bound.Value)
	}
}
$assertionCount++
if (-not $characterSource.Contains('GetVerticalLookScale()')) {
	throw 'The vertical scale must reach the pitch input (§18.3).'
}

# 화면의 행 번호가 시점 셋만큼 밀려 있는가. 여기가 어긋나면 조사를 고르고
# 두드리기가 바뀐다.
$assertionCount++
if ($controllerSource -notmatch
	'TryRebind\(\s*\r?\n?\s*KeyBindingSelection - UIGInputBindingSubsystem::LookRowCount') {
	throw 'Rebind must offset the screen row by the look rows.'
}
$assertionCount++
if ($hudSource -notmatch
	'Row \+ UIGInputBindingSubsystem::LookRowCount\s*\r?\n?\s*== SystemMenuKeyBindingSelection') {
	throw 'Binding rows must be drawn with the look-row offset.'
}

# --- 헤드밥 (§18.3) ---------------------------------------------------------
#
# 문서가 자세별 진폭과 주기를 적어 두었다. 그동안 코드에는 진폭이 하나뿐이었고,
# 동작 감소에서는 아예 0이 됐다 — 문서가 「0으로 만들지 않는다」고 못 박은
# 바로 그 자리다.

$bobRow = [regex]::Match(
	$story,
	'헤드밥 진폭: 걷기 (?<walk>[0-9.]+)cm\(주기 (?<period>[0-9.]+)s\), 앉기 (?<crouch>[0-9.]+)cm, 달리기 (?<sprint>[0-9.]+)cm')
$assertionCount++
if (-not $bobRow.Success) {
	throw 'The §18.3 head bob row could not be read.'
}
$bobValues = @{
	'WalkBobAmplitude' = $bobRow.Groups['walk'].Value
	'CrouchBobAmplitude' = $bobRow.Groups['crouch'].Value
	'SprintBobAmplitude' = $bobRow.Groups['sprint'].Value
}
foreach ($name in $bobValues.Keys) {
	$declared = [regex]::Match(
		$characterSource,
		('constexpr float {0} = (?<value>[0-9.]+)f;' -f $name))
	$assertionCount++
	if (-not $declared.Success) {
		throw "The §18.3 head bob constant is missing: $name"
	}
	$assertionCount++
	if ([double]$declared.Groups['value'].Value -ne [double]$bobValues[$name]) {
		throw (
			'{0} is {1} but §18.3 says {2}.' -f
				$name, $declared.Groups['value'].Value, $bobValues[$name])
	}
}

# 자세를 실제로 가르는가. 상수만 세 개 두고 하나만 쓰면 표만 맞는다.
$amplitudeBody = [regex]::Match(
	$characterSource,
	'float AIGPlayerCharacter::GetHeadBobAmplitude\(\) const(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $amplitudeBody.Success) {
	throw 'GetHeadBobAmplitude could not be isolated.'
}
foreach ($stance in @('bIsCrouched', 'bSprinting')) {
	$assertionCount++
	if (-not $amplitudeBody.Groups['body'].Value.Contains($stance)) {
		throw "The head bob must read the stance: $stance"
	}
}
foreach ($name in $bobValues.Keys) {
	$assertionCount++
	if (-not $amplitudeBody.Groups['body'].Value.Contains(
		('IGPlayerNoise::{0}' -f $name))) {
		throw "The head bob amplitude is declared but unused: $name"
	}
}

# 위아래 움직임은 abs(sin)이므로 한 걸음마다 반복된다. 2로 나누면 박자가 두 배가 된다.
$characterHeader = Read-ProjectText 'Source/IndieGame/Player/IGPlayerCharacter.h'
$strideMatch = [regex]::Match(
	$characterHeader, 'float StepDistance = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $strideMatch.Success) {
	throw 'StepDistance could not be read.'
}
$referenceMatch = [regex]::Match(
	$characterSource, 'constexpr float ReferenceWalkSpeed = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $referenceMatch.Success) {
	throw 'ReferenceWalkSpeed could not be read.'
}
$expectedStride =
	[double]$bobRow.Groups['period'].Value * [double]$referenceMatch.Groups['value'].Value
$assertionCount++
if ([Math]::Abs([double]$strideMatch.Groups['value'].Value - $expectedStride) -gt 0.5) {
	throw (
		'StepDistance is {0} but §18.3 period {1}s at {2}cm/s wants {3}.' -f
			$strideMatch.Groups['value'].Value,
			$bobRow.Groups['period'].Value,
			$referenceMatch.Groups['value'].Value,
			$expectedStride)
}

# 동작 감소가 헤드밥을 0으로 만들면 안 된다. 다른 흔들림과 같은 가드 안에
# 들어 있으면 그렇게 된다.
$motionBody = [regex]::Match(
	$characterSource,
	'void AIGPlayerCharacter::UpdateCameraMotion\(const float DeltaSeconds\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $motionBody.Success) {
	throw 'UpdateCameraMotion could not be isolated.'
}
$motionText = $motionBody.Groups['body'].Value
$assertionCount++
if ($motionText -notmatch
	'const float BobScale = bReducedMotion\s*\r?\n?\s*\?\s*IGPlayerNoise::ReducedMotionBobScale') {
	throw 'Reduced motion must scale the head bob, not remove it (§18.3).'
}
$assertionCount++
if ($motionText -notmatch
	'if \(bWalking\)\s*\r?\n\s*\{\s*\r?\n(?![\s\S]{0,200}?if \(!bReducedMotion\))') {
	throw 'The head bob must not sit inside a reduced-motion guard (§18.3).'
}
$scaleMatch = [regex]::Match(
	$characterSource, 'constexpr float ReducedMotionBobScale = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $scaleMatch.Success) {
	throw 'ReducedMotionBobScale is missing.'
}
$assertionCount++
if ([double]$scaleMatch.Groups['value'].Value -ne 0.25) {
	throw (
		'ReducedMotionBobScale is {0} but §18.3 says 0.25.' -f
			$scaleMatch.Groups['value'].Value)
}
$assertionCount++
if ($story -notmatch '\*\*0으로 만들지 않는다\*\*') {
	throw 'The §18.3 rule that the bob never reaches zero was removed.'
}

# --- §18.4 상호작용 손맛 -----------------------------------------------------
#
# 조준 반경부터 입력 버퍼까지 열한 줄이 숫자로 적혀 있다. 대부분은 코드와
# 맞았지만 망치 차징이 0.8이 아니라 1.0이었고, 서랍은 만들어진 적이 없었다.

$interactionHeader = Read-ProjectText 'Source/IndieGame/Player/IGInteractionComponent.h'
$hudSource = Read-ProjectText 'Source/IndieGame/Player/IGHorrorHUD.cpp'

# 조준: 스윕 반경과 최대 거리.
$reachRow = [regex]::Match(
	$story, '반경 (?<radius>[0-9]+)cm 스윕 보정[\s\S]{0,80}?최대 거리 (?<distance>[0-9]+)cm')
$assertionCount++
if (-not $reachRow.Success) {
	throw 'The §18.4 reach row could not be read.'
}
foreach ($field in @(
	@{ Name = 'FocusSweepRadius'; Value = $reachRow.Groups['radius'].Value },
	@{ Name = 'TraceDistance'; Value = $reachRow.Groups['distance'].Value })) {
	$declared = [regex]::Match(
		$interactionHeader, ('float {0} = (?<value>[0-9.]+)f;' -f $field.Name))
	$assertionCount++
	if (-not $declared.Success) {
		throw ('The §18.4 reach field is missing: {0}' -f $field.Name)
	}
	$assertionCount++
	if ([double]$declared.Groups['value'].Value -ne [double]$field.Value) {
		throw (
			'{0} is {1} but §18.4 says {2}.' -f
				$field.Name, $declared.Groups['value'].Value, $field.Value)
	}
}

# 브래킷: 지연과 형성 시간. 즉시 팝인은 금지다.
$bracketRow = [regex]::Match(
	$story, '대상 진입 후 (?<delay>[0-9]+)ms 지연 뒤 (?<reveal>[0-9]+)ms에 걸쳐 맺힌다')
$assertionCount++
if (-not $bracketRow.Success) {
	throw 'The §18.4 bracket row could not be read.'
}
foreach ($field in @(
	@{ Name = 'FocusAcquireDelaySeconds'; Ms = $bracketRow.Groups['delay'].Value },
	@{ Name = 'FocusAcquireRevealSeconds'; Ms = $bracketRow.Groups['reveal'].Value })) {
	$declared = [regex]::Match(
		$hudSource, ('constexpr float {0} = (?<value>[0-9.]+)f;' -f $field.Name))
	$assertionCount++
	if (-not $declared.Success) {
		throw ('The §18.4 bracket field is missing: {0}' -f $field.Name)
	}
	$expectedSeconds = [double]$field.Ms / 1000.0
	$assertionCount++
	if ([Math]::Abs([double]$declared.Groups['value'].Value - $expectedSeconds) -gt 0.0005) {
		throw (
			'{0} is {1}s but §18.4 says {2}ms.' -f
				$field.Name, $declared.Groups['value'].Value, $field.Ms)
	}
}
$assertionCount++
if ($story -notmatch '즉시 팝인 금지') {
	throw 'The §18.4 no-popin rule was removed.'
}

# 홀드 진행은 키 표시의 테두리가 닫히는 것이다. 링·숫자·퍼센트는 금지다.
$assertionCount++
if ($hudSource -notmatch 'DrawInteractionPrompt\([^;]*HoldProgress,[^;]*\);' -or
	-not $hudSource.Contains('const float Close = FMath::Clamp(HoldProgress, 0.0f, 1.0f);')) {
	throw 'Hold progress must be drawn as the key outline closing (§18.4).'
}
foreach ($banned in @('DrawHoldRing', 'HoldPercentText', 'DrawHoldGauge')) {
	$assertionCount++
	if ($hudSource.Contains($banned)) {
		throw "Ring gauges, numbers and percentages are banned (§18.4): $banned"
	}
}
$assertionCount++
if ($story -notmatch '숫자·퍼센트·링 게이지 전부 금지') {
	throw 'The §18.4 gauge ban was removed.'
}

# 홀드 시간. 문서가 부르는 이름과 코드의 자리를 짝지어 둔다.
$holdRow = [regex]::Match(
	$story,
	'홀드 시간: 문 조용히 열기 (?<door>[0-9.]+)s[\s\S]{0,60}?망치 스윙\s*\r?\n?\s*차징 (?<hammer>[0-9.]+)s, 엔딩 A의 튜닝 해머 되돌리기 (?<wrench>[0-9.]+)s')
$assertionCount++
if (-not $holdRow.Success) {
	throw 'The §18.4 hold-time row could not be read.'
}
$doorHold = [regex]::Match(
	(Read-ProjectText 'Source/IndieGame/Interaction/IGSwingDoor.h'),
	'float QuietOpenHoldSeconds = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $doorHold.Success) {
	throw 'QuietOpenHoldSeconds could not be read.'
}
$assertionCount++
if ([double]$doorHold.Groups['value'].Value -ne [double]$holdRow.Groups['door'].Value) {
	throw (
		'QuietOpenHoldSeconds is {0} but §18.4 says {1}.' -f
			$doorHold.Groups['value'].Value, $holdRow.Groups['door'].Value)
}

$nightFour = Read-ProjectText 'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.cpp'
foreach ($target in @(
	@{ Prompt = 'WallBreakPrompt'; Value = $holdRow.Groups['hammer'].Value; Name = '망치 스윙 차징' },
	@{ Prompt = 'EndingAPrompt'; Value = $holdRow.Groups['wrench'].Value; Name = '튜닝 해머 되돌리기' })) {
	# HoldSeconds는 NoiseLoudness 바로 앞에 온다. 두 숫자가 붙어 있어서
	# 자리를 세지 않고 읽으면 소음 크기를 홀드 시간으로 착각한다.
	$configured = [regex]::Match(
		$nightFour,
		[regex]::Escape($target.Prompt) +
			'[\s\S]{0,400}?EIGMissingFloorSource::None,\s*\r?\n(?:\s*//[^\r\n]*\r?\n)*\s*(?<hold>[0-9.]+)f,\s*\r?\n\s*(?<noise>[0-9.]+)f\);')
	$assertionCount++
	if (-not $configured.Success) {
		throw ('The hold time could not be read: {0}' -f $target.Prompt)
	}
	$assertionCount++
	if ([double]$configured.Groups['hold'].Value -ne [double]$target.Value) {
		throw (
			'{0} holds {1}s but §18.4 says {2}s.' -f
				$target.Name, $configured.Groups['hold'].Value, $target.Value)
	}
}

# 프로타주는 홀드 내내 소리가 난다. 들리지 않는 비용은 고를 수 없는 비용이다.
$assertionCount++
if (-not (Read-ProjectText 'Source/IndieGame/Entity/IGMissingFloorEvidence.h').Contains(
	'void SetSustainedRubCue(bool bEnabled);')) {
	throw 'Frottage must sound for the whole hold (§18.4).'
}

# 서랍은 만들어진 적이 없다. 표에 되살아나면 다음 사람이 찾다가 시간을 버린다.
$assertionCount++
if ($story -match '홀드 시간:[^\r\n]*서랍') {
	throw 'The §18.4 hold list names a drawer that no interaction implements.'
}

# 되감기와 입력 버퍼.
$rewindRow = [regex]::Match($story, '진행도는 (?<scale>[0-9.]+)배 속도로 되감긴다')
$bufferRow = [regex]::Match($story, '입력 버퍼 (?<ms>[0-9]+)ms')
$assertionCount++
if (-not $rewindRow.Success -or -not $bufferRow.Success) {
	throw 'The §18.4 rewind or buffer row could not be read.'
}
$rewindDeclared = [regex]::Match(
	$interactionHeader, 'float HoldRewindSpeedScale = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $rewindDeclared.Success) {
	throw 'HoldRewindSpeedScale could not be read.'
}
$assertionCount++
if ([double]$rewindDeclared.Groups['value'].Value -ne [double]$rewindRow.Groups['scale'].Value) {
	throw (
		'HoldRewindSpeedScale is {0} but §18.4 says {1}.' -f
			$rewindDeclared.Groups['value'].Value, $rewindRow.Groups['scale'].Value)
}
$bufferDeclared = [regex]::Match(
	$interactionHeader, 'float InputBufferSeconds = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $bufferDeclared.Success) {
	throw 'InputBufferSeconds could not be read.'
}
$bufferExpected = [double]$bufferRow.Groups['ms'].Value / 1000.0
$bufferActual = [double]$bufferDeclared.Groups['value'].Value
$assertionCount++
if ([Math]::Abs($bufferActual - $bufferExpected) -gt 0.0005) {
	throw (
		'InputBufferSeconds is {0}s but §18.4 says {1}ms.' -f
			$bufferDeclared.Groups['value'].Value, $bufferRow.Groups['ms'].Value)
}

# 소음은 접촉 프레임에 보고한다. 문이 걸쇠를 넘는 순간이지 문이 다 열린
# 뒤가 아니다.
$assertionCount++
if (-not (Read-ProjectText 'Source/IndieGame/Interaction/IGSwingDoor.cpp').Contains(
	'void AIGSwingDoor::ReportSwingNoise(const float Loudness) const')) {
	throw 'The door must report its noise from a contact-frame helper (§18.4).'
}
$assertionCount++
if ($story -notmatch '\*\*물리 접촉 프레임에\*\*') {
	throw 'The §18.4 contact-frame noise rule was removed.'
}

# --- §18.5 두드리기의 손맛 ---------------------------------------------------
#
# 이 게임의 시그니처 입력이다. 여섯 줄 전부 이미 맞았지만, 맞았다고 두면
# 다음에 누가 옮긴다. 특히 마지막 한 줄은 P4의 소름이 걸려 있다.

# 타격 프레임의 화면 흔들림.
$kickRow = [regex]::Match($story, '화면 미세\s*\r?\n?\s*흔들림 (?<degrees>[0-9.]+)도')
$assertionCount++
if (-not $kickRow.Success) {
	throw 'The §18.5 knock camera kick row could not be read.'
}
$kickDeclared = [regex]::Match(
	$characterSource, 'constexpr float KnockCameraKickDegrees = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $kickDeclared.Success) {
	throw 'KnockCameraKickDegrees could not be read.'
}
$assertionCount++
if ([double]$kickDeclared.Groups['value'].Value -ne [double]$kickRow.Groups['degrees'].Value) {
	throw (
		'KnockCameraKickDegrees is {0} but §18.5 says {1}.' -f
			$kickDeclared.Groups['value'].Value, $kickRow.Groups['degrees'].Value)
}

# 세 번째 탭 이후의 입력 잠금.
$lockRow = [regex]::Match($story, '세 번째 탭 이후 (?<seconds>[0-9.]+)초 입력 잠금')
$assertionCount++
if (-not $lockRow.Success) {
	throw 'The §18.5 input lock row could not be read.'
}
$lockDeclared = [regex]::Match(
	$characterSource, 'constexpr float KnockInputLockSeconds = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $lockDeclared.Success) {
	throw 'KnockInputLockSeconds could not be read.'
}
$assertionCount++
if ([double]$lockDeclared.Groups['value'].Value -ne [double]$lockRow.Groups['seconds'].Value) {
	throw (
		'KnockInputLockSeconds is {0} but §18.5 says {1}.' -f
			$lockDeclared.Groups['value'].Value, $lockRow.Groups['seconds'].Value)
}
# 세 번째다. 두 번이나 네 번이면 §7 P4의 「둘-쉬고-하나」와 어긋난다.
$tapBody = [regex]::Match(
	$characterSource,
	'void AIGPlayerCharacter::RegisterKnockSequenceTap\(\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $tapBody.Success) {
	throw 'RegisterKnockSequenceTap could not be isolated.'
}
$assertionCount++
if ($tapBody.Groups['body'].Value -notmatch 'KnockSequenceTapCount >= 3') {
	throw 'The lock must arm on the third tap (§18.5).'
}
$assertionCount++
if (-not $tapBody.Groups['body'].Value.Contains(
	'KnockInputLockedUntil = Now + IGPlayerNoise::KnockInputLockSeconds')) {
	throw 'The third tap must actually arm the input lock (§18.5).'
}

# 진동: 내 노크는 0.06s/강도 0.35 단발.
$hapticRow = [regex]::Match(
	$story, '내 노크는 (?<seconds>[0-9.]+)s/강도 (?<intensity>[0-9.]+) 단발')
$assertionCount++
if (-not $hapticRow.Success) {
	throw 'The §18.5 haptic row could not be read.'
}
$feedbackBody = [regex]::Match(
	$characterSource,
	'void AIGPlayerCharacter::ApplyPlayerKnockFeedback\(\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $feedbackBody.Success) {
	throw 'ApplyPlayerKnockFeedback could not be isolated.'
}
$expectedHaptic = 'PlayHapticFeedback({0}f, {1}f);' -f `
	$hapticRow.Groups['intensity'].Value, $hapticRow.Groups['seconds'].Value
$assertionCount++
if (-not $feedbackBody.Groups['body'].Value.Contains($expectedHaptic)) {
	throw ('The knock haptic must match §18.5: {0}' -f $expectedHaptic)
}

# **벽에서 돌아오는 응답 노크에는 진동이 없다.** 이 한 줄이 P4의 소름을
# 만든다 — 내 손이 아닌 것이 내 손처럼 느껴지면 그 장면은 끝이다.
foreach ($replyPath in @(
	'Source/IndieGame/Entity/IGListenerEntity.cpp',
	'Source/IndieGame/Audio/IGMissingFloorAudioSubsystem.cpp')) {
	$replySource = Read-ProjectText $replyPath
	foreach ($buzz in @(
		'PlayHapticFeedback',
		'PlayDynamicForceFeedback',
		'ClientPlayForceFeedback')) {
		$assertionCount++
		if ($replySource.Contains($buzz)) {
			throw (
				'The wall''s reply knock must never buzz the pad (§18.5): {0} in {1}' -f
					$buzz, $replyPath)
		}
	}
}
$assertionCount++
if ($story -notmatch '\*\*벽에서 돌아오는 응답 노크에는 진동이\s*\r?\n?\s*없다\*\*') {
	throw 'The §18.5 silent-reply rule was removed.'
}

# 입력→발음 지연 40ms 이하. 타이머를 끼우면 그 자리에서 깨진다.
$latencyRow = [regex]::Match($story, '입력→발음 지연 \*\*(?<ms>[0-9]+)ms 이하\*\*')
$assertionCount++
if (-not $latencyRow.Success) {
	throw 'The §18.5 latency row could not be read.'
}
$knockBody = [regex]::Match(
	$characterSource,
	'void AIGPlayerCharacter::Knock\(\)(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nbool AIGPlayerCharacter::OfferAnswerKnock')
$assertionCount++
if (-not $knockBody.Success) {
	throw 'Knock could not be isolated.'
}
$knockText = $knockBody.Groups['body'].Value
$assertionCount++
if (-not $knockText.Contains('IGAudio::SpawnOneShotAt(')) {
	throw 'The knock must make its sound in the same call as the input (§18.5).'
}
foreach ($delay in @('SetTimer', 'FTimerDelegate', 'Delay(')) {
	$assertionCount++
	if ($knockText.Contains($delay)) {
		throw "Nothing may sit between the tap and the sound (§18.5): $delay"
	}
}

# 단발 탭만. 자동 연타도, 정답을 대신 쳐 주는 것도 없다.
$assertionCount++
if ($story -notmatch '자동 연타·자동 정답 재생 금지') {
	throw 'The §18.5 no-autofire rule was removed.'
}
foreach ($auto in @('AutoKnock', 'RepeatKnock', 'PlayKnockPattern(')) {
	$assertionCount++
	if ($characterSource.Contains($auto)) {
		throw "The knock is a single tap; nothing may play it for the player (§18.5): $auto"
	}
}

# 실패해도 벽의 잔향만 돌아온다. 실패 문구도 붉은 표시도 없다.
$assertionCount++
if ($story -notmatch '실패 시 실패 문구·붉은 표시 없음') {
	throw 'The §18.5 no-failure-message rule was removed.'
}
$assertionCount++
if ($knockText -match 'PushThought|PushDialogue|PushAudioCaption') {
	throw 'A missed knock must say nothing (§18.5).'
}

# --- §18.6 햅틱 계약 ---------------------------------------------------------
#
# 여섯 줄짜리 표다. 셋은 이미 있었고 셋은 없었다 — CHASE 진입, 심박,
# 낙하물·충돌.

$hapticRows = @{}
foreach ($row in [regex]::Matches(
	$story,
	'(?m)^\| (?<event>[^|]+?) \| (?<intensity>[^|]+?) \| (?<length>[^|]+?) \|\s*$')) {
	$hapticRows[$row.Groups['event'].Value.Trim()] = @{
		Intensity = $row.Groups['intensity'].Value.Trim()
		Length = $row.Groups['length'].Value.Trim()
	}
}
foreach ($event in @(
	'내 노크', '발소리(달리기 한정)', '낙하물·충돌',
	'CHASE 진입', '포획', '심박(스트레스 0.85+)')) {
	$assertionCount++
	if (-not $hapticRows.ContainsKey($event)) {
		throw "The §18.6 haptic table lost a row: $event"
	}
}

# 표의 숫자를 코드 상수와 짝짓는다.
$hapticConstants = @(
	@{ Event = '내 노크'; Call = 'PlayHapticFeedback(0.35f, 0.06f);' },
	@{ Event = '발소리(달리기 한정)'; Call = 'PlayHapticFeedback(0.12f, 0.04f);' })
foreach ($pair in $hapticConstants) {
	$row = $hapticRows[$pair.Event]
	$expected = 'PlayHapticFeedback({0}f, {1}f);' -f `
		$row.Intensity, ($row.Length -replace 's$', '')
	$assertionCount++
	if ($expected -ne $pair.Call) {
		throw (
			'The §18.6 row moved without the code following: {0} wants {1}' -f
				$pair.Event, $expected)
	}
	$assertionCount++
	if (-not $characterSource.Contains($pair.Call)) {
		throw ('The §18.6 haptic is missing: {0}' -f $pair.Event)
	}
}

# 달리기 한정이라고 적혀 있다. 걸을 때도 울리면 표가 거짓말이 된다.
$assertionCount++
if ($characterSource -notmatch
	'if \(bSprinting && !bIsCrouched\)\s*\r?\n\s*\{\s*\r?\n\s*PlayHapticFeedback\(0\.12f, 0\.04f\);') {
	throw 'The footstep haptic is sprint-only (§18.6).'
}

# 나머지 넷은 이름 붙인 상수로 둔다. 표를 옮기면 여기서 잡힌다.
$namedHaptics = @(
	@{ Name = 'ImpactHapticIntensity'; Value = '0.55' },
	@{ Name = 'ImpactHapticSeconds'; Value = '0.20' },
	@{ Name = 'ChaseHapticIntensity'; Value = '0.25' },
	@{ Name = 'ChaseHapticFadeInSeconds'; Value = '0.40' },
	@{ Name = 'HeartbeatHapticIntensity'; Value = '0.10' },
	@{ Name = 'CaptureHapticIntensity'; Value = '0.70' })
foreach ($named in $namedHaptics) {
	$declared = [regex]::Match(
		$characterSource,
		('constexpr float {0} = (?<value>[0-9.]+)f;' -f $named.Name))
	$assertionCount++
	if (-not $declared.Success) {
		throw ('The §18.6 haptic constant is missing: {0}' -f $named.Name)
	}
	$assertionCount++
	if ([double]$declared.Groups['value'].Value -ne [double]$named.Value) {
		throw (
			'{0} is {1} but §18.6 says {2}.' -f
				$named.Name, $declared.Groups['value'].Value, $named.Value)
	}
}

# 표의 문자열과 상수를 맞대 본다. 표만 고치고 코드를 두면 조용히 어긋난다.
$assertionCount++
if ($hapticRows['낙하물·충돌'].Intensity -ne '0.55' -or
	$hapticRows['낙하물·충돌'].Length -ne '0.20s') {
	throw 'The §18.6 impact row moved; the code constants did not follow.'
}
$assertionCount++
if ($hapticRows['CHASE 진입'].Intensity -notmatch '^0\.25' -or
	$hapticRows['CHASE 진입'].Length -notmatch '0\.4s') {
	throw 'The §18.6 chase row moved; the code constants did not follow.'
}
$assertionCount++
if ($hapticRows['포획'].Intensity -notmatch '^0\.70') {
	throw 'The §18.6 capture row moved; the code constants did not follow.'
}
$assertionCount++
if ($hapticRows['심박(스트레스 0.85+)'].Intensity -notmatch '^0\.10') {
	throw 'The §18.6 heartbeat row moved; the code constants did not follow.'
}

# CHASE는 지속이다. 페이드인이 있고, 끝날 때 반드시 멈춘다 — 안 멈추면
# 추격이 끝났는데 패드만 계속 우는 상태가 남는다.
$chaseBody = [regex]::Match(
	$characterSource,
	'void AIGPlayerCharacter::UpdateChaseHaptic\(const float DeltaSeconds\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $chaseBody.Success) {
	throw 'UpdateChaseHaptic could not be isolated.'
}
foreach ($piece in @(
	'IGPlayerNoise::ChaseHapticFadeInSeconds',
	'EDynamicForceFeedbackAction::Update',
	'EDynamicForceFeedbackAction::Start')) {
	$assertionCount++
	if (-not $chaseBody.Groups['body'].Value.Contains($piece)) {
		throw "The chase haptic must ramp and hold (§18.6): $piece"
	}
}
$stopBody = [regex]::Match(
	$characterSource,
	'void AIGPlayerCharacter::StopChaseHaptic\(\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $stopBody.Success) {
	throw 'StopChaseHaptic could not be isolated.'
}
$assertionCount++
if (-not $stopBody.Groups['body'].Value.Contains(
	'EDynamicForceFeedbackAction::Stop')) {
	throw 'Leaving the chase must stop the sustained buzz (§18.6).'
}
# 진동 전체 끄기를 켜 두었는데 지속 진동만 살아남으면 안 된다.
$assertionCount++
if (-not $chaseBody.Groups['body'].Value.Contains('AreHapticsEnabled()')) {
	throw 'The sustained chase haptic must honour the haptics-off option (§18.6).'
}

# 심박은 소리를 만드는 자리에서 함께 낸다. 임계값도 한 번만 적는다.
$stressSourceForHaptics = Read-ProjectText 'Source/IndieGame/Player/IGStressComponent.cpp'
$beatBody = [regex]::Match(
	$stressSourceForHaptics,
	'void UIGStressComponent::PlayHeartbeat\(const float EffectiveStress\)(?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $beatBody.Success) {
	throw 'PlayHeartbeat could not be isolated.'
}
$assertionCount++
if (-not $beatBody.Groups['body'].Value.Contains('PlayHeartbeatHaptic()')) {
	throw 'The heartbeat haptic must fire where the beat is made (§18.6).'
}
$thresholdDeclared = [regex]::Match(
	$stressSourceForHaptics,
	'constexpr float HeartbeatHapticStressThreshold = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $thresholdDeclared.Success) {
	throw 'HeartbeatHapticStressThreshold could not be read.'
}
$assertionCount++
if ([double]$thresholdDeclared.Groups['value'].Value -ne 0.85) {
	throw (
		'HeartbeatHapticStressThreshold is {0} but §18.6 says 0.85.' -f
			$thresholdDeclared.Groups['value'].Value)
}

# 전체 OFF 옵션이 살아 있는가.
$assertionCount++
if ($story -notmatch '전체 OFF 옵션을 제공하되') {
	throw 'The §18.6 haptics-off promise was removed.'
}
$hapticGate = [regex]::Match(
	$characterSource,
	'void AIGPlayerCharacter::PlayHapticFeedback\((?<body>[\s\S]*?)\r?\n\}')
$assertionCount++
if (-not $hapticGate.Success) {
	throw 'PlayHapticFeedback could not be isolated.'
}
$assertionCount++
if (-not $hapticGate.Groups['body'].Value.Contains('AreHapticsEnabled()')) {
	throw 'Every haptic must pass the off switch (§18.6).'
}

# --- §18.7 조작감 합격식 ------------------------------------------------------
#
# 다섯 줄 중 하나는 사람을 앉혀 놓고 봐야 하는 것이고(초견 5명), 하나는
# 실측이다(입력→소리 지연). 나머지 셋은 여기서 본다. 홀드 편차는 숫자를
# 실제로 돌려 봐야 해서 audit_hold_timing.py가 따로 맡는다.

# 다섯 동사가 독립 액션으로 있는가. 조사의 별칭으로 살면 §24 즉시 차단 16이다.
$verbKeys = @{}
foreach ($mapping in [regex]::Matches(
	$inputConfig,
	'ActionMappings=\(ActionName="(?<action>[^"]+)"[^)]*?Key=(?<key>[A-Za-z_0-9]+)')) {
	$action = $mapping.Groups['action'].Value
	if (-not $verbKeys.ContainsKey($action)) {
		$verbKeys[$action] = @()
	}
	$verbKeys[$action] += $mapping.Groups['key'].Value
}
$acceptanceVerbs = @('Crouch', 'Sprint', 'Knock', 'Listen', 'HoldBreath')
foreach ($verb in $acceptanceVerbs) {
	$assertionCount++
	if (-not $verbKeys.ContainsKey($verb)) {
		throw "The §18.7 acceptance verb has no independent action: $verb"
	}
}
# 조사와 키를 나눠 쓰면 그건 별칭이다.
$interactKeys = $verbKeys['Interact']
foreach ($verb in $acceptanceVerbs) {
	foreach ($key in $verbKeys[$verb]) {
		$assertionCount++
		if ($interactKeys -contains $key) {
			throw "The §18.7 verb shares a key with Interact: $verb on $key"
		}
	}
}

# 패드만으로도, 키보드만으로도 완주할 수 있어야 한다. 장치별로 훑는다.
# 엿듣기는 키보드에서 벽 조준 중 조사 홀드로 대신하는 것이 설계이고,
# 그 예외는 재설정 표의 설명에 적혀 있어야 한다 — 말 없이 비어 있으면
# 빠뜨린 것과 구분되지 않는다.
foreach ($verb in $acceptanceVerbs) {
	$hasPad = @($verbKeys[$verb] | Where-Object { $_ -like 'Gamepad_*' }).Count -gt 0
	$hasKeyboard = @($verbKeys[$verb] | Where-Object { $_ -notlike 'Gamepad_*' }).Count -gt 0
	$assertionCount++
	if (-not $hasPad) {
		throw "The §18.7 pad-only playthrough is broken: $verb has no gamepad key"
	}
	$assertionCount++
	if (-not $hasKeyboard -and $verb -ne 'Listen') {
		throw "The §18.7 keyboard-only playthrough is broken: $verb has no key"
	}
}
$assertionCount++
if ($bindingSource -notmatch 'DescListen[^)]*키보드에서는 벽을 보며 조사 키를 길게 누르세요') {
	throw 'The keyboard route for 엿듣기 must stay written down (§18.7).'
}
$assertionCount++
if ($story -notmatch '패드만으로 프롤로그~밤1 완주, 마우스·키보드만으로 동일 완주') {
	throw 'The §18.7 both-devices criterion was removed.'
}
$assertionCount++
if ($story -notmatch '`Interact` 별칭이 남아 있지 않다') {
	throw 'The §18.7 no-alias criterion was removed.'
}

# 홀드 편차는 숫자를 돌려서 본다. 계약은 그 감사가 실제로 걸려 있는지만
# 확인한다 — 스크립트만 있고 아무도 안 부르면 없는 것과 같다.
$validateScript = Read-ProjectText 'Scripts/Validate-Project.ps1'
$assertionCount++
if (-not $validateScript.Contains('Scripts/audit_hold_timing.py')) {
	throw 'The §18.7 hold-timing audit is not wired into validation.'
}
$assertionCount++
if ($story -notmatch '60fps·30fps 양쪽에서 홀드 완료 시간 편차 ±3% 이내') {
	throw 'The §18.7 hold deviation window was removed.'
}

# 사람이 봐야 하는 줄은 자동으로 못 본다. 지워지지만 않게 지킨다.
$assertionCount++
if ($story -notmatch '초견 5명 중 4명 이상이 튜토리얼 텍스트 없이') {
	throw 'The §18.7 playtest criterion was removed.'
}

# --- §27 M0 입력 계약 실행 보완 -----------------------------------------------
#
# 이 절은 스스로 「수치나 우선순위가 충돌하면 이 절을 M0의 최신 계약으로
# 사용한다」고 적었다. 그러니 §18과 겹치는 값은 서로 같아야 하고, §18이
# 다루지 않는 줄은 여기서만 지켜진다.

# 27.3 — 이동 네 상태. 걷기·앉기·달리기는 §18.2와 겹치고, 듣기는 여기에만
# 있다. 하필 그 행만 코드에 숫자가 박혀 있었다.
$movementSection27 = [regex]::Match(
	$story,
	'### 27\.3 이동 상태 계약\r?\n(?<body>[\s\S]*?)\r?\n### 27\.4')
$assertionCount++
if (-not $movementSection27.Success) {
	throw 'The §27.3 movement table could not be read.'
}
$stateConstants = @{
	'걷기' = @('WalkAcceleration', 'WalkBraking')
	'앉기' = @('CrouchAcceleration', 'CrouchBraking')
	'달리기' = @('SprintAcceleration', 'SprintBraking')
	'듣기' = @('ListenAcceleration', 'ListenBraking')
}
$stateSpeeds = @{
	'걷기' = 'ReferenceWalkSpeed'
	'앉기' = 'CrouchSpeed'
	'달리기' = 'SprintSpeed'
	'듣기' = 'ListenSpeed'
}
$seenStates = 0
foreach ($row in [regex]::Matches(
	$movementSection27.Groups['body'].Value,
	'(?m)^\| (?<state>[^|]+?) \| (?<speed>[0-9]+) \| (?<accel>[0-9]+) \| (?<brake>[0-9]+) \|')) {
	$state = $row.Groups['state'].Value.Trim()
	$assertionCount++
	if (-not $stateConstants.ContainsKey($state)) {
		throw "The §27.3 table grew a state this contract does not know: $state"
	}
	$seenStates++
	$fields = @(
		@{ Name = $stateSpeeds[$state]; Expected = $row.Groups['speed'].Value },
		@{ Name = $stateConstants[$state][0]; Expected = $row.Groups['accel'].Value },
		@{ Name = $stateConstants[$state][1]; Expected = $row.Groups['brake'].Value })
	foreach ($field in $fields) {
		$declared = [regex]::Match(
			$characterSource,
			('constexpr float {0} = (?<value>[0-9.]+)f;' -f $field.Name))
		$assertionCount++
		if (-not $declared.Success) {
			throw ('The §27.3 movement constant is missing: {0}' -f $field.Name)
		}
		$assertionCount++
		if ([double]$declared.Groups['value'].Value -ne [double]$field.Expected) {
			throw (
				'{0} is {1} but §27.3 says {2}.' -f
					$field.Name, $declared.Groups['value'].Value, $field.Expected)
		}
	}
}
$assertionCount++
if ($seenStates -ne 4) {
	throw "The §27.3 table lists $seenStates states; it is supposed to list four."
}
# 듣는 동안 제동이 가장 세다. 그게 「멈추는 것도 빨라야 소리를 놓치지 않는다」의
# 구현이고, 네 상태 중 유일하게 걷기보다 큰 값이다.
$listenBraking = [regex]::Match(
	$characterSource, 'constexpr float ListenBraking = (?<value>[0-9.]+)f;')
$walkBraking = [regex]::Match(
	$characterSource, 'constexpr float WalkBraking = (?<value>[0-9.]+)f;')
$assertionCount++
if (-not $listenBraking.Success -or -not $walkBraking.Success) {
	throw 'The listen and walk braking could not be compared.'
}
$assertionCount++
if ([double]$listenBraking.Groups['value'].Value -le [double]$walkBraking.Groups['value'].Value) {
	throw 'Listening must stop faster than walking (§27.3).'
}
# 듣기 상태가 그 상수를 실제로 쓴다. 숫자를 도로 박아 넣으면 표와 코드가
# 다시 갈라진다.
$assertionCount++
if ($characterSource -notmatch
	'MovementComponent->MaxAcceleration = IGPlayerNoise::ListenAcceleration;') {
	throw 'The listening state must use its named acceleration (§27.3).'
}
$assertionCount++
if ($characterSource -notmatch
	'MovementComponent->BrakingDecelerationWalking =\s*\r?\n?\s*IGPlayerNoise::ListenBraking;') {
	throw 'The listening state must use its named braking (§27.3).'
}

# 27.2 — 동사 태그가 조준 대상에서 입력을 가른다.
foreach ($verb in @('MissingFloor.Verb.Knock', 'MissingFloor.Verb.Listen')) {
	$assertionCount++
	if ($story -notmatch [regex]::Escape($verb)) {
		throw "The §27.2 verb tag was removed from the doc: $verb"
	}
}
$verbTagPlaced = @{ 'Knock' = 0; 'Listen' = 0 }
$verbTagRead = @{ 'Knock' = 0; 'Listen' = 0 }
foreach ($file in Get-ChildItem -Path (Join-Path $projectRoot 'Source/IndieGame') `
	-Filter '*.cpp' -Recurse) {
	$text = Get-Content -Raw -Encoding UTF8 -LiteralPath $file.FullName
	foreach ($placed in [regex]::Matches(
		$text,
		'Tags\.AddUnique\(FName\(TEXT\("MissingFloor\.Verb\.(?<verb>Knock|Listen)"\)\)\)')) {
		$verbTagPlaced[$placed.Groups['verb'].Value]++
	}
	foreach ($use in [regex]::Matches(
		$text, 'MissingFloor\.Verb\.(?<verb>Knock|Listen)"')) {
		$verbTagRead[$use.Groups['verb'].Value]++
	}
}
foreach ($verb in @('Knock', 'Listen')) {
	$assertionCount++
	if ($verbTagPlaced[$verb] -lt 1) {
		throw "The §27.2 verb tag is never placed on anything: $verb"
	}
	# 붙인 자리도 같은 문자열이라 전체에서 빼야 읽는 자리만 남는다.
	$actualReads = $verbTagRead[$verb] - $verbTagPlaced[$verb]
	$assertionCount++
	if ($actualReads -lt 1) {
		throw "The §27.2 verb tag is placed but nothing reads it: $verb"
	}
}
# 일반 노크는 문에만 허용한다. 병·종이·버튼을 두드리는 우발 입력을 막는 줄이다.
$assertionCount++
if ($story -notmatch '`Interaction\.Door` 물성을 가진 문에만 허용해') {
	throw 'The §27.2 door-only knock rule was removed.'
}
$assertionCount++
if ($characterSource -notmatch 'FName\(TEXT\("Interaction\.Door"\)\)') {
	throw 'The ordinary knock must still be limited to doors (§27.2).'
}
# Q는 어떤 경로에서도 Interact의 별칭이 아니다(§24 즉시 차단 16).
$assertionCount++
if ($story -notmatch 'Q는 어떤 경로에서도 `Interact`의 별칭이 아니다') {
	throw 'The §27.2 no-alias rule was removed.'
}

# 27.5 — 응답음은 플레이어 피드백 함수를 통하지 않는다. §18.5가 같은 것을
# 다른 말로 잠갔고, 이 절은 그 이유를 적어 둔 자리다.
$assertionCount++
if ($story -notmatch '응답음은 플레이어 피드백\s*\r?\n?함수를 통하지 않으므로 카메라 킥과 진동이 없다') {
	throw 'The §27.5 silent-reply rule was removed.'
}
# 잠금은 노크만 잠근다. 이동·시점·숨 참기를 빼앗으면 그건 벌이다.
$assertionCount++
if ($story -notmatch '노크 액션만 0\.9초 잠근다\. 이동·시점·숨 참기·취소는\s*\r?\n?계속 가능하다') {
	throw 'The §27.5 lock must not take the camera or movement.'
}
# 잠금은 Knock의 첫 관문에서만 되돌린다. 다른 동사의 진입점에 같은 검사가
# 생기면 그건 전체 입력 잠금이고, §27.5가 금지한 것이다.
$assertionCount++
if ($characterSource -notmatch
	'CurrentWorld->GetTimeSeconds\(\) < KnockInputLockedUntil\)\s*\r?\n\s*\{\s*\r?\n\s*return;') {
	throw 'The knock lock must turn the knock away and nothing else (§27.5).'
}
# 한 번 걸고 한 번 읽는다. 세 번째 자리가 생기면 무엇이 잠기는지 알 수 없다.
$lockUses = ([regex]::Matches($characterSource, 'KnockInputLockedUntil')).Count
$assertionCount++
if ($lockUses -ne 2) {
	throw (
		'KnockInputLockedUntil appears {0} times; it is set once and read once (§27.5).' -f
			$lockUses)
}


# 시점 행 수는 화면과 컨트롤러가 함께 보는 값이다. 여기서도 코드에서 읽는다.
$lookRowMatch = [regex]::Match(
	$bindingHeader, 'LookRowCount = (?<count>[0-9]+);')
$assertionCount++
if (-not $lookRowMatch.Success) {
	throw 'LookRowCount could not be read.'
}
$lookRowCount = [int]$lookRowMatch.Groups['count'].Value

Write-Host (
	'MISSING_FLOOR_INPUT_BINDING_CONTRACT PASS actions={0} look={1} assertions={2}' -f `
		$entries.Count, $lookRowCount, $assertionCount) -ForegroundColor Green
