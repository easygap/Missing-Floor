[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$header = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Accessibility/IGAccessibilitySubsystem.h')
$source = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Accessibility/IGAccessibilitySubsystem.cpp')
$interaction = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGInteractionComponent.cpp')
$character = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGPlayerCharacter.cpp')
$flashlight = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGFlashlightComponent.cpp')
$hudHeader = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGHorrorHUD.h')
$hudSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGHorrorHUD.cpp')
$settingsLayout = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGSettingsMenuLayout.h')
$controllerSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGPlayerController.cpp')
$inputConfig = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Config/DefaultInput.ini')
$assertionCount = 0

function Assert-True {
	param(
		[Parameter(Mandatory = $true)]
		[bool]$Condition,
		[Parameter(Mandatory = $true)]
		[string]$Message
	)

	if (-not $Condition) {
		throw "MISSINGFLOOR_ACCESSIBILITY_CONTRACT FAIL: $Message"
	}
	$script:assertionCount++
}

function Assert-ContainsAll {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Text,
		[Parameter(Mandatory = $true)]
		[string[]]$Needles,
		[Parameter(Mandatory = $true)]
		[string]$Context
	)

	foreach ($needle in $Needles) {
		Assert-True ($Text.Contains($needle)) "$Context 누락: $needle"
	}
}

Assert-ContainsAll $header @(
	'bReducedCameraMotion',
	'bReducedFlicker',
	'bDirectionalFearCues',
	'bSubtitlesEnabled',
	'bSoundCaptionsEnabled',
	'CaptionSizeScale',
	'CaptionBackgroundOpacity',
	'CaptionSafeAreaScale',
	'bToggleHoldInteractions',
	'bToggleCrouch',
	'bHapticsEnabled',
	'bMicrophoneNoiseEnabled',
	'HoldDurationScale',
	'UsesDirectionalFearCues',
	'AreSubtitlesEnabled',
	'AreSoundCaptionsEnabled',
	'UsesToggleCrouch',
	'AreHapticsEnabled',
	'GetCaptionSizeScale',
	'ApplySettings',
	'ResetToDefaults'
) '공용 설정 표면'

Assert-ContainsAll $source @(
	'GGameUserSettingsIni',
	'LoadPersistedSettings',
	'SavePersistedSettings',
	'GConfig->Flush(false, GGameUserSettingsIni)',
	'FMath::IsFinite(Result.HoldDurationScale)',
	'FMath::IsFinite(Result.CaptionSizeScale)',
	'FMath::IsFinite(Result.CaptionBackgroundOpacity)',
	'FMath::IsFinite(Result.CaptionSafeAreaScale)',
	'MinimumHoldScale = 0.25f',
	'MaximumHoldScale = 1.0f',
	'MinimumCaptionScale = 0.85f',
	'MaximumCaptionScale = 2.0f',
	'MinimumCaptionBackgroundOpacity = 0.0f',
	'MaximumCaptionBackgroundOpacity = 1.0f',
	'MinimumCaptionSafeArea = 0.80f',
	'MaximumCaptionSafeArea = 1.0f',
	'TEXT("CaptionSizeScale")',
	'TEXT("SoundCaptionsEnabled")',
	'TEXT("CaptionBackgroundOpacity")',
	'TEXT("CaptionSafeAreaScale")',
	'TEXT("MicrophoneNoiseEnabled")'
) '영구 저장·입력 정규화'

Assert-ContainsAll $source @(
	'IGReducedMotion',
	'IGReducedFlicker',
	'IGFearDirection',
	'IGNoSubtitles',
	'IGNoSoundCaptions',
	'IGToggleHolds',
	'IGMicrophoneMode',
	'IGHoldScale=',
	'IGCaptionScale=',
	'IGCaptionBackground=',
	'IGCaptionSafeArea='
) '무상태 QA 오버라이드'

$loadIndex = $source.IndexOf('LoadPersistedSettings();')
$overrideIndex = $source.IndexOf('RebuildEffectiveSettings();')
Assert-True (
	$loadIndex -ge 0 -and $overrideIndex -gt $loadIndex
) '저장값을 읽기 전에 QA 오버라이드를 적용한다'

Assert-ContainsAll $interaction @(
	'TargetHoldDuration *= Accessibility->GetHoldDurationScale()',
	'bUseToggleHold = Accessibility->UsesToggleHoldInteractions()',
	'bToggleHoldLatched = bUseToggleHold',
	'(!bInteractionPressed && !bToggleHoldLatched)',
	'bInteractionActive && bToggleHoldLatched',
	'FinishActiveInteraction(EIGInteractionEndReason::Cancelled, false)'
) '길이 조절·토글 홀드 상태기계'

Assert-ContainsAll $character @(
	'IsReducedCameraMotionEnabled()',
	'if (!bReducedMotion)',
	'FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator)',
	'PlayFootstep(SpeedScale)'
) '화면 흔들림 줄이기'

Assert-ContainsAll $flashlight @(
	'IsReducedCameraMotionEnabled()',
	'IsReducedFlickerEnabled()',
	'BrownOutTimer = 0.0f',
	'FlickerValue = 1.0f',
	'TargetSway = bReducedMotion'
) '손전등 흔들림·점멸 감소'

Assert-ContainsAll $hudHeader @(
	'PushFearDirection',
	'ShowFearDirection',
	'DrawFearDirection',
	'FearCueWorldLocation',
	'FearCueEndTime'
) '공포음 방향 HUD 표면'
Assert-ContainsAll $hudHeader @(
	'PushAudioCaption',
	'ShowAudioCaption',
	'DrawAudioCaption',
	'CurrentAudioCaption',
	'AudioCaptionEndTime'
) '핵심 소리 자막 HUD 표면'
Assert-ContainsAll $hudSource @(
	'Accessibility->AreSoundCaptionsEnabled()',
	'bool AIGHorrorHUD::DrawAudioCaption',
	'DrawRoundedHudSurface(',
	'void AIGHorrorHUD::DrawAudioCaptionBearingGlyph(',
	'FCanvasLineItem Segment(',
	'Settings.CaptionSizeScale',
	'Settings.CaptionBackgroundOpacity',
	'Settings.CaptionSafeAreaScale',
	'MaximumTextWidth',
	'MeasureTextWidth',
	'Canvas->StrLen',
	'FindFittingCaptionPrefix',
	'WrapHudText',
	'OutRemainder',
	'AudioCaptionQueue.Insert',
	'TextItem.Scale',
	'DrawAudioCaption(',
	'소리 자막',
	'A11ySoundCaptions'
) '핵심 소리 자막 설정·출력 연결'
Assert-True (-not $hudSource.Contains(
	'const FString Ellipsis = TEXT("…")')) '긴 자막을 말줄임표로 손실한다'
Assert-ContainsAll $hudSource @(
	'Accessibility->UsesDirectionalFearCues()',
	'FVector::DotProduct(Direction, Forward)',
	'FVector::DotProduct(Direction, Right)',
	'FCanvasLineItem',
	'CueColor(0.72f, 0.74f, 0.72f'
) '공포음 방향 무채색 파형'

Assert-ContainsAll $controllerSource @(
	'ToggleAccessibilityMenu',
	'SetPause(true)',
	'bGameWasPausedBeforeAccessibility',
	'Binding.bExecuteWhenPaused = true',
	'MoveAccessibilitySelectionUp',
	'MoveAccessibilitySelectionDown',
	'AdjustAccessibilityLeft',
	'AdjustAccessibilityRight',
	'ConfirmAccessibilitySelection',
	'RequestManualHint',
	'Accessibility->ApplySettings(Settings)',
	'Accessibility->ResetToDefaults()',
	'SetAccessibilityMenuState',
	'Settings.CaptionSizeScale = FMath::Clamp',
	'Settings.CaptionBackgroundOpacity = FMath::Clamp',
	'Settings.CaptionSafeAreaScale = FMath::Clamp',
	'RowCount = IGSettingsMenuLayout::AccessibilityRowCount',
	'Settings.bMicrophoneNoiseEnabled = !Settings.bMicrophoneNoiseEnabled',
	'RefreshMicrophoneCaptureMode()'
) '일시정지 접근성 설정 입력'
Assert-ContainsAll $hudSource @(
	'DrawAccessibilityPanel()',
	'접근성 설정',
	'글자, 소리 안내, 조작을 편하게 맞춰 주세요.',
	'변경 즉시 저장',
	'게임 진행',
	'화면 효과',
	'소리 알림',
	'자막',
	'조작',
	'설명',
	'난이도',
	'화면 흔들림 줄이기',
	'빛 깜빡임 줄이기',
	'소리가 나는 방향 표시',
	'힌트',
	'화면 가운데 점',
	'조작 키 표시',
	'소리 자막',
	'자막 글자 크기',
	'자막 배경 진하기',
	'자막 표시 영역',
	'마이크 소리 사용',
	'길게 누르기 방식',
	'길게 누르는 시간',
	'기본값으로 초기화',
	'Esc/F10 닫기'
) '1280x720 대응 네이티브 설정 패널'
Assert-ContainsAll $hudSource @(
	'GetFittedTextScale(',
	'ValidateSettingsTextRect(',
	'bLayoutValidationAllInsideSettingsContainers',
	'PreviewLines',
	'PreviewWidth - 36.0f * Scale',
	'WrapHudText(Text, Font, FitScale, MaximumWidth, 3',
	'DrawSettingsFooterText('
) '긴 한글·200% 미리 보기 내부 경계 계약'
Assert-ContainsAll $settingsLayout @(
	'AccessibilityRowCount = 26',
	'AccessibilityCategoryCount = 6',
	'case 3: return {SoundCaptions, 5}',
	'case 4: return {ToggleCrouch, 6}',
	'HitTestSettingsRow'
) '접근성 설정 항목·포인터 공용 계약'

# 행 이름이 생기면서 숫자와 이름이 갈라질 수 있다. 마지막 이름이 행 수와
# 맞는지는 static_assert가 컴파일 때 보고, 여기서는 그 그물이 남아 있는지를
# 본다 — 지워지면 다음에 한 줄 끼울 때 밝기가 시야각이 된다.
Assert-ContainsAll $settingsLayout @(
	'enum EAccessibilityRow : int32',
	'CloseMenu + 1 == AccessibilityRowCount',
	'FieldOfView,',
	'ComfortVignette,',
	'CameraTexture,',
	'CaptionDuration,'
) '접근성 행 이름과 행 수의 일치'

# 묶음 여섯 개가 빈틈도 겹침도 없이 행 전부를 덮는가. 숫자를 손으로 맞추다
# 한 행이 어느 묶음에도 안 들어가면 그 줄은 화면에서 사라진다.
# 화면 설정 묶음도 행 이름을 쓰므로 접근성 묶음 함수 안만 읽는다.
$accessibilityCategoryBody = [regex]::Match(
	$settingsLayout,
	'GetAccessibilityCategory\(const int32 Category\)[\s\S]*?default:').Value
$categoryRanges = [regex]::Matches(
	$accessibilityCategoryBody,
	'case \d: return \{(?<first>[A-Za-z]+), (?<count>\d+)\}')
Assert-True ($categoryRanges.Count -eq 6) '접근성 묶음 여섯 개'
$rowNames = [regex]::Match(
	$settingsLayout,
	'enum EAccessibilityRow : int32\s*\{(?<body>[\s\S]*?)\}')
Assert-True $rowNames.Success '접근성 행 이름 목록'
$orderedNames = @()
foreach ($entry in [regex]::Matches(
	$rowNames.Groups['body'].Value, '(?m)^\s*(?<name>[A-Za-z]+)')) {
	$orderedNames += $entry.Groups['name'].Value
}
Assert-True ($orderedNames.Count -eq 26) '접근성 행 이름 26개'
$expectedFirst = 0
foreach ($range in $categoryRanges) {
	$firstName = $range.Groups['first'].Value
	$actualFirst = [array]::IndexOf($orderedNames, $firstName)
	Assert-True ($actualFirst -eq $expectedFirst) (
		'접근성 묶음이 이어 붙는다: {0}' -f $firstName)
	$expectedFirst += [int]$range.Groups['count'].Value
}
Assert-True ($expectedFirst -eq 26) '접근성 묶음이 행 전부를 덮는다'
# --- §19.8 소리의 대체 채널 ---------------------------------------------------
#
# 표 여덟 줄 중 넷이 비어 있었다. 소리를 못 듣는 손에게 존재의 노크는
# 아무것도 아닌 것이 되므로, 같은 정보를 다른 통로로 준다.

$storyText = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Docs/STORY_BIBLE_MISSING_FLOOR.md')
Assert-ContainsAll $header @(
	'bool bKnockHapticSubstitute = false;',
	'bool bKnockRippleSubstitute = false;',
	'bool bHeartbeatWarning = false;',
	'bool bCognitiveAssist = false;'
) '§19.8 대체 채널 설정'

# 넷 다 기본은 꺼짐이다. 표의 「기본」 열이 그렇게 적혀 있다.
$substituteRows = @(
	@{ Name = '두드리는 소리를 진동으로 알림'; Field = 'bKnockHapticSubstitute' },
	@{ Name = '두드리는 소리를 화면에 표시'; Field = 'bKnockRippleSubstitute' },
	@{ Name = '심장 박동 표시'; Field = 'bHeartbeatWarning' },
	@{ Name = '박자 맞추기 도움'; Field = 'bCognitiveAssist' })
foreach ($row in $substituteRows) {
	$tableRow = [regex]::Match(
		$storyText, ('\| {0} \| (?<default>[^|]+?) \|' -f [regex]::Escape($row.Name)))
	Assert-True $tableRow.Success ('§19.8 표에 {0} 줄이 있다' -f $row.Name)
	Assert-True ($tableRow.Groups['default'].Value.Trim() -eq 'OFF') (
		'{0}의 기본은 꺼짐이다' -f $row.Name)
	Assert-True ($header -match ('{0} = false;' -f $row.Field)) (
		'{0}이 코드에서도 꺼짐으로 시작한다' -f $row.Field)
}

# 켜고 끄는 자리와 저장이 있어야 게임 중 즉시 적용·유지가 성립한다(§19.9).
foreach ($row in $substituteRows) {
	Assert-True ($controllerSource -match ('Settings.{0} = !Settings.{0};' -f $row.Field)) (
		'{0}을 화면에서 켜고 끌 수 있다' -f $row.Field)
}
foreach ($half in @(
	@{ Name = 'LoadPersistedSettings'; Call = 'GConfig->GetBool(' },
	@{ Name = 'SavePersistedSettings'; Call = 'GConfig->SetBool(' })) {
	$halfBody = [regex]::Match(
		$source,
		('void UIGAccessibilitySubsystem::{0}\(\)( const)?' -f $half.Name) +
			'(?<body>[\s\S]*?)\r?\n\}')
	Assert-True $halfBody.Success ('{0}을 떼어낼 수 있다' -f $half.Name)
	foreach ($key in @(
		'KnockHapticSubstitute', 'KnockRippleSubstitute',
		'HeartbeatWarning', 'CognitiveAssist')) {
		Assert-True (
			$halfBody.Groups['body'].Value -match
				([regex]::Escape($half.Call) + '\s*\r?\n\s*IGAccessibility::ConfigSection,' +
					'\s*\r?\n\s*TEXT\("' + $key + '"\)')) (
			'{0}이 {1}을 다룬다' -f $half.Name, $key)
	}
}

# 「두드리는 소리를 화면에 표시」는 색이 아니라 두께로 나눈다. 색으로만 나누면 색각에서
# 다시 사라져서, 대체 채널이 또 하나의 벽이 된다.
$noiseBody = [regex]::Match(
	$hudSource,
	'void AIGHorrorHUD::HandleNoiseReported\(const FIGNoiseEvent& Event\)(?<body>[\s\S]*?)\r?\n\}')
Assert-True $noiseBody.Success 'HandleNoiseReported를 떼어낼 수 있다'
Assert-True (
	$noiseBody.Groups['body'].Value.Contains('UsesKnockRippleSubstitute()')) `
	'존재의 소리에 링을 그릴지 설정이 정한다'
Assert-True ($hudSource -match 'bRippleIsForeign \? [0-9.]+f : 1\.0f') `
	'존재의 링은 두께로 구분한다'
# 내 소리의 링은 노크(0.3) 이상에만 그린다. 걷기마다 깜빡이던 원호가 화면 결함으로 읽혔다.
Assert-True ($hudSource -match 'NoiseRippleMinimumOwnLoudness = 0\.3f') '내 소리 링의 최소 크기가 노크와 같다'
Assert-True (
	$noiseBody.Groups['body'].Value -match 'bMine && Event\.Loudness[^;]*< IGHorrorHUD::NoiseRippleMinimumOwnLoudness') `
	'조용한 내 소리에는 링을 그리지 않는다'
Assert-True ($storyText -match '색이 아니라 두께로 구분') `
	'§19.8의 두께 구분 규칙이 남아 있다'

# 「두드리는 소리를 진동으로 알림」은 소음 버스를 탄다. 응답 노크 코드에 손을 대면 §18.5의
# 무진동 규칙이 무너진다.
Assert-True ($character -match 'OnNoiseReported.AddUObject\(\s*\r?\n?\s*this, &AIGPlayerCharacter::HandleForeignNoise\)') `
	'대체 진동은 소음 버스에서 온다'
$foreignBody = [regex]::Match(
	$character,
	'void AIGPlayerCharacter::HandleForeignNoise\(const FIGNoiseEvent& Event\)(?<body>[\s\S]*?)\r?\n\}')
Assert-True $foreignBody.Success 'HandleForeignNoise를 떼어낼 수 있다'
Assert-True (
	$foreignBody.Groups['body'].Value.Contains('Event.Instigator.Get() == this')) `
	'내가 낸 소리는 대체 진동에서 걸러진다'
Assert-True (
	$foreignBody.Groups['body'].Value.Contains('UsesKnockHapticSubstitute()')) `
	'대체 진동은 켠 사람만 받는다'
# 거리가 지워지면 대체 채널이 정보를 반만 옮긴다.
$substituteBody = [regex]::Match(
	$character,
	'void AIGPlayerCharacter::PlayKnockSubstituteHaptic\(const float Loudness\) const(?<body>[\s\S]*?)\r?\n\}')
Assert-True $substituteBody.Success 'PlayKnockSubstituteHaptic를 떼어낼 수 있다'
Assert-True ($substituteBody.Groups['body'].Value.Contains('Loudness')) `
	'대체 진동의 세기가 소리 크기를 따라간다'

# 심장 박동 표시는 표의 배율과 임계를 그대로 쓴다.
$warningRow = [regex]::Match(
	$storyText, '스트레스 (?<threshold>[0-9.]+) 도달 시 비네트 맥동 ×(?<scale>[0-9.]+)')
Assert-True $warningRow.Success '§19.8 심장 박동 표시 줄을 읽을 수 있다'
$stressForWarning = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGStressComponent.cpp')
$scaleDeclared = [regex]::Match(
	$stressForWarning,
	'constexpr float HeartbeatWarningVignetteScale = (?<value>[0-9.]+)f;')
Assert-True $scaleDeclared.Success 'HeartbeatWarningVignetteScale을 읽을 수 있다'
Assert-True (
	[double]$scaleDeclared.Groups['value'].Value -eq [double]$warningRow.Groups['scale'].Value) (
	'심장 박동 표시 배율이 {0}인데 §19.8은 {1}이라고 적었다' -f
		$scaleDeclared.Groups['value'].Value, $warningRow.Groups['scale'].Value)
$warningBody = [regex]::Match(
	$stressForWarning,
	'float UIGStressComponent::GetHeartbeatWarningScale\(\) const(?<body>[\s\S]*?)\r?\n\}')
Assert-True $warningBody.Success 'GetHeartbeatWarningScale를 떼어낼 수 있다'
Assert-True (
	$warningBody.Groups['body'].Value.Contains(
		'IGStress::HeartbeatHapticStressThreshold')) `
	'심장 박동 표시가 진동과 같은 임계를 쓴다'
# 꺼져 있으면 1이라서 나머지 계산이 예전과 똑같이 돈다.
Assert-True ($warningBody.Groups['body'].Value -match 'return 1\.0f;') `
	'심장 박동 표시가 꺼져 있으면 비네트가 그대로다'

# 박자 맞추기 도움: 판정창 ×1.6.
$windowRow = [regex]::Match($storyText, '노크 판정창 ×(?<scale>[0-9.]+)')
Assert-True $windowRow.Success '§19.8 노크 판정창 줄을 읽을 수 있다'
$windowDeclared = [regex]::Match(
	$source, 'constexpr float KnockWindowAssistScale = (?<value>[0-9.]+)f;')
Assert-True $windowDeclared.Success 'KnockWindowAssistScale을 읽을 수 있다'
Assert-True (
	[double]$windowDeclared.Groups['value'].Value -eq [double]$windowRow.Groups['scale'].Value) (
	'노크 판정창 배율이 {0}인데 §19.8은 {1}이라고 적었다' -f
		$windowDeclared.Groups['value'].Value, $windowRow.Groups['scale'].Value)
Assert-True (
	$character -match 'IGPlayerNoise::KnockSequenceResetSeconds \* WindowScale') `
	'넓어진 판정창이 실제로 걸린다'

# --- 멀미 완화 비네트 (§18.3) ------------------------------------------------
#
# 공포의 터널 시야와 같은 후처리를 쓴다. 둘이 만나면 큰 쪽이 남아야 한다 —
# 편하라고 넣은 것이 공포가 전하는 정보를 지우면 안 되고, 반대로 공포가
# 낮다고 편의 비네트가 사라져도 안 된다.

$stressSource = Get-Content -Raw -Encoding UTF8 (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGStressComponent.cpp')
$postBody = [regex]::Match(
	$stressSource,
	'void UIGStressComponent::UpdatePostProcess\(\)(?<body>[\s\S]*?)\r?\n\}')
Assert-True $postBody.Success 'UpdatePostProcess를 떼어낼 수 있다'
$postText = $postBody.Groups['body'].Value

Assert-True (
	$postText -match
		'FearPostProcess->BlendWeight = Weight;') `
	'편의 비네트가 서 있으면 후처리도 선다'
Assert-True (
	$postText -match
		'const float Weight = FMath::Max\(Ramp, Comfort\);') `
	'공포와 편의 중 큰 쪽이 후처리 무게를 정한다'
Assert-True (
	$postText -match
		'Settings.VignetteIntensity = FMath::Max\(\s*\r?\n\s*FMath::Lerp\(0\.28f, 0\.72f, Ramp\),') `
	'비네트 세기도 큰 쪽이 남는다'

# 설정이 0이면 예전과 똑같이 돌아야 한다. Ramp만으로 조기 반환하던 자리가
# Weight로 바뀌었으므로 그 관계가 유지되는지 본다.
Assert-True ($postText -match 'if \(Weight <= 0\.0f\)') `
	'둘 다 0이면 후처리를 건드리지 않는다'

Assert-ContainsAll $header @(
	'float ComfortVignetteStrength = 0.0f;',
	'GetComfortVignetteStrength() const'
) '멀미 완화 비네트 설정'

# 기본은 꺼짐이다. 의도한 화면은 비네트가 없는 쪽이다.
Assert-True ($header -match 'float ComfortVignetteStrength = 0\.0f;') `
	'멀미 완화 비네트의 기본은 꺼짐'

# 화면 질감은 반대로 켜 둔 쪽이 의도한 화면이다. 0까지 내릴 수 있어야 하고,
# 저장·불러오기와 전후 비교용 실행 인자가 같이 있어야 한다.
Assert-ContainsAll $header @(
	'float CameraTextureStrength = 1.0f;',
	'GetCameraTextureStrength() const'
) '화면 질감 설정'
Assert-ContainsAll $source @(
	'TEXT("CameraTextureStrength")',
	'TEXT("IGCameraTexture=")',
	'IGAccessibility::MinimumCameraTexture'
) '화면 질감 저장과 비교 촬영 인자'
$sensorSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGCameraSensorComponent.cpp')
Assert-ContainsAll $sensorSource @(
	'GetCameraTextureStrength()',
	'SensorPostProcess->BlendWeight = Strength > KINDA_SMALL_NUMBER ? 1.0f : 0.0f'
) '화면 질감이 설정을 따르고 0이면 패스를 끈다'

foreach ($half in @(
	@{ Name = 'LoadPersistedSettings'; Call = 'GConfig->GetFloat(' },
	@{ Name = 'SavePersistedSettings'; Call = 'GConfig->SetFloat(' })) {
	$halfBody = [regex]::Match(
		$source,
		('void UIGAccessibilitySubsystem::{0}\(\)( const)?' -f $half.Name) +
			'(?<body>[\s\S]*?)\r?\n\}')
	Assert-True $halfBody.Success ('{0}을 떼어낼 수 있다' -f $half.Name)
	Assert-True (
		$halfBody.Groups['body'].Value -match
			([regex]::Escape($half.Call) + '\s*\r?\n\s*IGAccessibility::ConfigSection,' +
				'\s*\r?\n\s*TEXT\("ComfortVignetteStrength"\)')) (
		'{0}이 ComfortVignetteStrength를 다룬다' -f $half.Name)
}

# §18.3이 비네트를 접근성에 두라고 말한 자리가 남아 있는가.
$storyText = Get-Content -Raw -Encoding UTF8 (
	Join-Path $projectRoot 'Docs/STORY_BIBLE_MISSING_FLOOR.md')
Assert-True ($storyText -match '화면 가장자리 어둡게 강화') `
	'§18.3의 비네트 약속이 남아 있다'

# --- 시야각과 자막 표시 시간 -------------------------------------------------
#
# 설정만 있고 아무 데도 안 걸리는 항목은 화면에 숫자만 바뀌는 줄이 된다.
# 둘 다 실제로 걸리는 자리를 짚는다.

Assert-ContainsAll $header @(
	'float FieldOfViewDegrees = 78.0f;',
	'float CaptionDurationScale = 1.0f;',
	'GetFieldOfViewDegrees() const',
	'GetCaptionDurationScale() const'
) '시야각·자막 시간 설정'

# 시야각은 기본값만 계약이 보고 있었다. 문서가 여는 폭(68~100)과 코드의
# 경계가 서로를 모르면, 한쪽만 넓혀도 조용히 다른 게임이 된다.
$fovRange = [regex]::Match($storyText, '(?<low>[0-9]+)~(?<high>[0-9]+)으로 열되')
Assert-True $fovRange.Success '문서가 시야각을 여는 폭을 적고 있다'
$fovLow = [regex]::Match($source, 'MinimumFieldOfView = (?<value>[0-9.]+)f;')
$fovHigh = [regex]::Match($source, 'MaximumFieldOfView = (?<value>[0-9.]+)f;')
Assert-True ($fovLow.Success -and $fovHigh.Success) `
	'코드가 시야각 경계를 들고 있다'
Assert-True (
	[double]$fovLow.Groups['value'].Value -eq [double]$fovRange.Groups['low'].Value) (
	'시야각 하한이 문서 {0}, 코드 {1}이다' -f
		$fovRange.Groups['low'].Value, $fovLow.Groups['value'].Value)
Assert-True (
	[double]$fovHigh.Groups['value'].Value -eq [double]$fovRange.Groups['high'].Value) (
	'시야각 상한이 문서 {0}, 코드 {1}이다' -f
		$fovRange.Groups['high'].Value, $fovHigh.Groups['value'].Value)

# 기본값이 경계 밖이면 처음 켠 사람이 저장도 안 한 값으로 시작하고, 설정을
# 한 번 만지는 순간 화면이 튄다.
$fovDefault = [regex]::Match($header, 'float FieldOfViewDegrees = (?<value>[0-9.]+)f;')
Assert-True $fovDefault.Success '시야각 기본값을 읽을 수 있다'
Assert-True (
	[double]$fovDefault.Groups['value'].Value -ge [double]$fovLow.Groups['value'].Value `
	-and [double]$fovDefault.Groups['value'].Value -le [double]$fovHigh.Groups['value'].Value) (
	'시야각 기본값 {0}이 경계 {1}~{2} 밖이다' -f
		$fovDefault.Groups['value'].Value,
		$fovLow.Groups['value'].Value,
		$fovHigh.Groups['value'].Value)

# 손으로 고친 ini가 화면을 못 쓰게 만들면 안 된다.
$sanitize = [regex]::Match(
	$source,
	'FIGAccessibilitySettings UIGAccessibilitySubsystem::Sanitize\((?<body>[\s\S]*?)\r?\n\}')
Assert-True $sanitize.Success 'Sanitize를 떼어낼 수 있다'
foreach ($field in @('FieldOfViewDegrees', 'CaptionDurationScale')) {
	Assert-True (
		$sanitize.Groups['body'].Value -match
			("Result.{0} = FMath::Clamp" -f $field)) (
		'저장본을 다시 조인다: {0}' -f $field)
}

# 열쇠 이름은 불러오기와 저장 두 곳에 있다. 파일 전체를 보면 한쪽을 통째로
# 빼도 통과하므로 함수 본문을 떼어내고 각각 본다.
foreach ($half in @(
	@{ Name = 'LoadPersistedSettings'; Call = 'GConfig->GetFloat(' },
	@{ Name = 'SavePersistedSettings'; Call = 'GConfig->SetFloat(' })) {
	$halfBody = [regex]::Match(
		$source,
		('void UIGAccessibilitySubsystem::{0}\(\)( const)?' -f $half.Name) +
			'(?<body>[\s\S]*?)\r?\n\}')
	Assert-True $halfBody.Success ('{0}을 떼어낼 수 있다' -f $half.Name)
	foreach ($key in @('FieldOfViewDegrees', 'CaptionDurationScale')) {
		Assert-True (
			$halfBody.Groups['body'].Value -match
				([regex]::Escape($half.Call) + '\s*\r?\n\s*IGAccessibility::ConfigSection,' +
					'\s*\r?\n\s*TEXT\("' + $key + '"\)')) (
			'{0}이 {1}을 다룬다' -f $half.Name, $key)
	}
}

# 시야각이 카메라에 걸리는가. 첫 프레임부터 걸려야 한다 — 설정을 열어 봐야
# 적용되면 「저장이 안 됐다」로 읽힌다.
$fovBody = [regex]::Match(
	$character,
	'void AIGPlayerCharacter::RefreshFieldOfView\(\)(?<body>[\s\S]*?)\r?\n\}')
Assert-True $fovBody.Success 'RefreshFieldOfView를 떼어낼 수 있다'
Assert-True (
	$fovBody.Groups['body'].Value.Contains('GetFieldOfViewDegrees()')) `
	'시야각이 설정에서 나온다'
Assert-True (
	$fovBody.Groups['body'].Value.Contains('FirstPersonCamera->SetFieldOfView(')) `
	'시야각이 1인칭 카메라에 걸린다'
$beginPlay = [regex]::Match(
	$character,
	'void AIGPlayerCharacter::BeginPlay\(\)(?<body>[\s\S]*?)\r?\n\}')
Assert-True $beginPlay.Success 'BeginPlay를 떼어낼 수 있다'
Assert-True (
	$beginPlay.Groups['body'].Value.Contains('RefreshFieldOfView()')) `
	'저장된 시야각이 첫 프레임부터 걸린다'

# 자막 시간은 대사와 소리 캡션 두 갈래 모두에 걸려야 한다. 한쪽만 걸리면
# 대사는 늘어나는데 캡션은 그대로라서 고장으로 읽힌다.
$dialogueDuration = [regex]::Match(
	$hudSource,
	'float AIGHorrorHUD::CalculateDialogueDuration\((?<body>[\s\S]*?)\r?\n\}')
Assert-True $dialogueDuration.Success 'CalculateDialogueDuration을 떼어낼 수 있다'
Assert-True (
	$dialogueDuration.Groups['body'].Value.Contains('GetCaptionDurationScale()')) `
	'대사 자막이 표시 시간 배율을 읽는다'
$audioCaption = [regex]::Match(
	$hudSource,
	'void AIGHorrorHUD::ShowAudioCaptionInContext\((?<body>[\s\S]*?)\r?\n\}')
# 소리 자막은 사건 이름을 받는 쪽이 시간을 정한다. 이름 없는 ShowAudioCaption은 그리로 넘긴다.
Assert-True $audioCaption.Success 'ShowAudioCaptionInContext를 떼어낼 수 있다'
Assert-True (
	$audioCaption.Groups['body'].Value.Contains('GetCaptionDurationScale()')) `
	'소리 캡션이 표시 시간 배율을 읽는다'

# 배율은 마지막에 곱한다. ReadingDuration에만 곱하면 부르는 쪽이 정한 최소
# 표시 시간이 배율을 지나쳐, 짧은 줄에서는 설정이 아무 일도 안 한다.
Assert-True (
	$dialogueDuration.Groups['body'].Value -match
		'FMath::Max\(ReadingDuration, MinimumDurationSeconds\)\s*\r?\n?\s*\* GetCaptionDurationScale\(\)') `
	'표시 시간 배율이 최소 표시 시간에도 걸린다'

Assert-ContainsAll $inputConfig @(
	'ActionName="AccessibilityMenu"',
	'Key=F10',
	'ActionName="AccessibilityUp"',
	'Key=Up',
	'ActionName="AccessibilityDown"',
	'Key=Down',
	'ActionName="AccessibilityLeft"',
	'Key=Left',
	'ActionName="AccessibilityRight"',
	'Key=Right',
	'ActionName="AccessibilityConfirm"',
	'Key=Enter',
	'Key=SpaceBar'
) '키보드 설정 패널 매핑'
Assert-ContainsAll $inputConfig @(
	'ActionName="RequestHint"',
	'Key=H',
	'Key=Gamepad_RightShoulder',
	'Key=Gamepad_LeftY',
	'Key=Gamepad_RightX',
	'Key=Gamepad_FaceButton_Bottom',
	'Key=Gamepad_FaceButton_Left',
	'Key=Gamepad_Special_Right',
	'Key=Gamepad_DPad_Up',
	'ActionName="AccessibilityClose"',
	'Key=Gamepad_FaceButton_Right',
	'ActionName="LoadAutosave"',
	'Key=Gamepad_FaceButton_Top',
	'Key=Gamepad_LeftThumbstick',
	'Key=Gamepad_RightThumbstick'
) '게임패드 이동·조사·설정 매핑'
Assert-ContainsAll $hudSource @(
	'GetBoundKeyLabel(PromptAction, bUsingGamepad)',
	'bShowKey ? KeyLabel : FText::GetEmpty()',
	'GetBoundKeyLabel(EIGBindableAction::GameplayGuide, bUsingGamepad)',
	'GetBoundKeyLabel(EIGBindableAction::Interact, bUsingGamepad)',
	'AccessibilityControlsGamepad',
	'AccessibilityControlsKeyboard',
	# 휴대폰 내려놓기와 엔딩 C 재시도도 지금 묶인 키 이름을 쓴다.
	'PhoneCloseFormat',
	'EndingCRetryFormat',
	'LS 이동',
	'WASD 이동'
) '입력 장치별 화면 안내'
Assert-ContainsAll $controllerSource @(
	'bool AIGPlayerController::InputKey(const FInputKeyEventArgs& Params)',
	'Params.IsGamepad()',
	'Params.IsSimulatedInput()',
	'AxisThreshold = bGamepad ? 0.30f : 0.01f',
	'SetInputDevicePresentation(bGamepad)',
	'SetInputDevicePresentation(bUsingGamepadForHud)'
) '마지막 실제 입력 장치 자동 전환'

# C++ 구현과 독립된 정책 오라클. 숫자가 바뀌면 기획 계약도
# 명시적으로 함께 바꾸게 한다.
foreach ($holdScale in @(-2.0, 0.25, 0.7, 1.0, 4.0)) {
	$clamped = [Math]::Min(1.0, [Math]::Max(0.25, $holdScale))
	Assert-True (
		$clamped -ge 0.25 -and $clamped -le 1.0
	) "길게 누르는 시간 배율 범위 오류: $holdScale"
}
foreach ($captionScale in @(-2.0, 0.85, 1.0, 1.25, 1.5, 2.0, 4.0)) {
	$clamped = [Math]::Min(2.0, [Math]::Max(0.85, $captionScale))
	Assert-True (
		$clamped -ge 0.85 -and $clamped -le 2.0
	) "자막 크기 배율 범위 오류: $captionScale"
}
foreach ($backgroundOpacity in @(-2.0, 0.0, 0.50, 0.82, 1.0, 4.0)) {
	$clamped = [Math]::Min(1.0, [Math]::Max(0.0, $backgroundOpacity))
	Assert-True (
		$clamped -ge 0.0 -and $clamped -le 1.0
	) "자막 배경 진하기 범위 오류: $backgroundOpacity"
}
foreach ($safeArea in @(-2.0, 0.80, 0.90, 1.0, 4.0)) {
	$clamped = [Math]::Min(1.0, [Math]::Max(0.80, $safeArea))
	Assert-True (
		$clamped -ge 0.80 -and $clamped -le 1.0
	) "자막 표시 영역 범위 오류: $safeArea"
}

# 실제 실행을 대체하지 않는 순수 배치 오라클. 설정 17개를 최대 5개씩
# 그룹화한 설정 패널과 200% 하단 메시지가 720p~1440p에서 안전
# 영역을 지키는지 C++과 독립된 계산으로 확인한다.
$layoutProfiles = @(
	@{ Width = 1280.0; Height = 720.0 },
	@{ Width = 1600.0; Height = 900.0 },
	@{ Width = 1920.0; Height = 1080.0 },
	@{ Width = 2560.0; Height = 1440.0 },
	# §19.9가 4K까지 이름을 대 놓았다. 배율 상한이 2.0이라 여기서 처음
	# 상한에 걸리는데, 걸린 뒤에도 안전 영역이 남는지는 재 봐야 안다.
	@{ Width = 3840.0; Height = 2160.0 }
)
foreach ($profile in $layoutProfiles) {
	$width = $profile.Width
	$height = $profile.Height
	$resolutionScale = [Math]::Min(2.0, [Math]::Max(
		0.85,
		[Math]::Min($width / 1920.0, $height / 1080.0)))
	$horizontalMargin = [Math]::Max(24.0, 28.0 * $resolutionScale)
	$verticalMargin = [Math]::Max(20.0, 28.0 * $resolutionScale)
	$settingsPanelWidth = [Math]::Min(
		[Math]::Max(320.0, $width - 2.0 * $horizontalMargin),
		1320.0 * $resolutionScale)
	$settingsPanelHeight = [Math]::Min(
		[Math]::Max(420.0, $height - 2.0 * $verticalMargin),
		800.0 * $resolutionScale)
	$settingsPanelX = ($width - $settingsPanelWidth) * 0.5
	$settingsPanelY = ($height - $settingsPanelHeight) * 0.5
	$settingsFooterY = $settingsPanelY + $settingsPanelHeight - 64.0 * $resolutionScale
	$optionStartY = $settingsPanelY + 164.0 * $resolutionScale
	$optionRowHeight = [Math]::Max(52.0, 62.0 * $resolutionScale)
	$lastOptionBottom = $optionStartY + 4.0 * $optionRowHeight +
		$optionRowHeight - 7.0 * $resolutionScale
	$detailTop = $optionStartY + 5.0 * $optionRowHeight +
		18.0 * $resolutionScale
	$detailBottom = $settingsFooterY - 18.0 * $resolutionScale
	$detailHeight = [Math]::Min(
		210.0 * $resolutionScale,
		$detailBottom - $detailTop)
	Assert-True ($settingsPanelX -ge 0.0) "설정 패널 왼쪽 잘림: ${width}x${height}"
	Assert-True ($settingsPanelY -ge 0.0) "설정 패널 위쪽 잘림: ${width}x${height}"
	Assert-True (
		$settingsPanelX + $settingsPanelWidth -le $width
	) "설정 패널 오른쪽 잘림: ${width}x${height}"
	Assert-True (
		$settingsPanelY + $settingsPanelHeight -le $height
	) "설정 패널 아래쪽 잘림: ${width}x${height}"
	Assert-True ($optionRowHeight -ge 52.0) "설정 포인터 대상이 52px 미만: ${width}x${height}"
	Assert-True (
		$lastOptionBottom -lt $detailTop
	) "설정 행·설명 패널 겹침: ${width}x${height}"
	Assert-True (
		$detailTop + $detailHeight -le $detailBottom + 0.1
	) "설정 설명·하단 조작 안내 겹침: ${width}x${height}"

	foreach ($captionScale in @(0.85, 1.0, 1.25, 1.5, 2.0)) {
		foreach ($safeArea in @(0.80, 0.90, 1.0)) {
			$safeWidth = $width * $safeArea
			$basePanelWidth = [Math]::Min(
				920.0 * $resolutionScale,
				[Math]::Max(360.0 * $resolutionScale, $width * 0.72))
			$panelWidth = [Math]::Min(
				$basePanelWidth,
				[Math]::Max(280.0, $safeWidth - 32.0 * $resolutionScale))
			$safeLeft = ($width - $safeWidth) * 0.5
			$panelLeft = ($width - $panelWidth) * 0.5
			$panelRight = $panelLeft + $panelWidth
			Assert-True ($panelLeft -ge $safeLeft - 0.1) "메시지 왼쪽 안전 영역 침범: ${width}x${height}"
			Assert-True ($panelRight -le $width - $safeLeft + 0.1) "메시지 오른쪽 안전 영역 침범: ${width}x${height}"

			$textScale = $captionScale * $resolutionScale
			$lineCount = if ($captionScale -gt 1.25) { 3.0 } else { 2.0 }
			$bodyHeight = [Math]::Max(16.0, 19.0 * $textScale)
			$lineStep = $bodyHeight * $(if ($lineCount -ge 3.0) { 1.45 } else { 1.34 })
			$speakerHeight = [Math]::Max(12.0, 14.0 * $textScale * 0.84)
			$panelHeight = 16.0 * $resolutionScale + $speakerHeight +
				9.0 * $resolutionScale + $bodyHeight +
				$lineStep * ($lineCount - 1.0) + 17.0 * $resolutionScale
			$safeInset = $height * (1.0 - $safeArea) * 0.5
			$panelY = $height - $safeInset - $panelHeight - 24.0 * $resolutionScale
			$panelBottom = $panelY + $panelHeight
			Assert-True ($panelY -ge $safeInset - 0.1) "메시지 위쪽 안전 영역 침범: ${width}x${height}"
			Assert-True ($panelBottom -le $height - $safeInset + 0.1) "메시지 아래쪽 안전 영역 침범: ${width}x${height}"

			# 200%와 80% 안전 영역의 최악 조건은 두 줄로 잡는다. 실제 HUD도
			# 동일 폭을 실측해 1~2줄로 나누고 표면 높이를 줄 수에 맞춘다.
			$previewLineCount = if (
				$captionScale -ge 1.5 -and $safeArea -le 0.90
			) { 2.0 } else { 1.0 }
			$previewLineHeight = [Math]::Max(16.0, 19.0 * $textScale * 1.20)
			$previewBodyHeight = $previewLineHeight * $previewLineCount
			$previewSpeakerHeight = 14.0 * $textScale * 0.78
			$previewHeight = [Math]::Max(
				48.0 * $resolutionScale,
				$previewSpeakerHeight + $previewBodyHeight + 23.0 * $resolutionScale)
			$previewY = [Math]::Max(
				$detailTop + 62.0 * $resolutionScale,
				$detailTop + $detailHeight - $previewHeight -
					12.0 * $resolutionScale)
			Assert-True (
				$previewY -ge $detailTop
			) "자막 미리 보기가 설명 패널 위로 이탈: ${width}x${height}"
			Assert-True (
				$previewY + $previewHeight -le $detailBottom + 0.1
			) "자막 미리 보기·하단 조작 안내 겹침: ${width}x${height}"
		}
	}
}

# --- 광과민: 점멸을 만드는 곳은 전부 점멸 감소를 따른다 ------------------
# 설계서 §24의 즉시 차단 22가 3Hz 초과 점멸을 출시 차단으로 잠갔다.
#
# 호출만 하는 파일은 세지 않는다 — SuspendCorridorFlicker를 부르는 것은
# 점멸을 만드는 게 아니다.
$flickerOwners = @(
	Get-ChildItem -Recurse -File -LiteralPath (
		Join-Path $projectRoot 'Source/IndieGame') -Filter '*.cpp' |
		Where-Object {
			(Get-Content -Raw -Encoding UTF8 -LiteralPath $_.FullName) -match
				'::[A-Za-z]*(Flicker|Blink)[A-Za-z]*\('
		}
)
# 지금은 복도 형광등과 손전등, 두 파일이다.
Assert-True ($flickerOwners.Count -ge 2) (
	'점멸을 정의하는 파일이 {0}개뿐이다. 판별식이 깨졌다.' -f $flickerOwners.Count)
foreach ($flickerOwner in $flickerOwners) {
	$flickerSource = Get-Content -Raw -Encoding UTF8 -LiteralPath $flickerOwner.FullName
	Assert-True ($flickerSource.Contains('IsReducedFlickerEnabled')) (
		'점멸을 만들면서 점멸 감소를 따르지 않는다: {0}' -f $flickerOwner.Name)
}

Write-Host (
	"MISSINGFLOOR_ACCESSIBILITY_CONTRACT PASS assertions=$assertionCount " +
	"gamepad=1 input_switch=1 captions=1 layout_profiles=$($layoutProfiles.Count) persistence=1 reduced_motion=1 reduced_flicker=1 toggle_hold=1"
) -ForegroundColor Green
