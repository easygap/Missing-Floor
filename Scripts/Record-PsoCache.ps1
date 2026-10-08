<#
.SYNOPSIS
	묶음 PSO 캐시(Build/Windows/PipelineCaches/PSO_IndieGame_PCD3D_SM6.spc)를 다시 만든다.

.DESCRIPTION
	PSO 사전 준비는 부품이 쓸 PSO만 미리 컴파일한다. 후처리, UI, 전역 셰이더처럼
	그 밖에서 처음 그리는 PSO는 첫 실행에서 그리기를 멈추고 컴파일한다. Development
	배포본으로 실제 장면을 돌며 그 PSO를 기록하고, 쿠크가 남긴 셰이더 안정 키와
	합쳐 캐시를 만든다. 다음 Package-Windows.ps1이 이 파일을 배포본에 넣고,
	게임은 첫 실행 때 미리 컴파일한다.

	기록 장면은 타이틀·설정, 입주 낮, README 장면 경로, 밤 경로(High·Low),
	결말, 다섯째 밤, 계단 센서등, 밤4 망치질과 정전, 낮 장면, 403호 욕실, 에필로그다.
	장면을 새로 만들면 여기에 더하고 이 스크립트를 다시 돌린다.

.NOTES
	Shipping은 PSO를 기록하지 않고, 설치형 엔진은 Test 구성을 빌드하지 못해 Development로
	따로 묶는다. 쿠크한 셰이더는 같아서 안정 키가 그대로 맞는다. 결과물은 저장소에
	커밋하는 빌드 입력이다.

	UE 5.8은 셰이더 해시가 8바이트로 줄었는데, .spc를 읽는 쪽은 8바이트짜리 읽기를 모두
	해시로 여긴다. PSO 사용 마스크가 기본값(-1)이면 9바이트 가변 정수로 저장되고, 쿠크는
	그 뒤부터 어긋나게 읽다가 멈춘다. 그래서 기록할 때만 마스크를 1로 둔다. 게임은
	r.ShaderPipelineCache.GameFileMaskEnabled가 꺼져 있어 마스크와 상관없이 모두 미리 컴파일한다.
#>
[CmdletBinding()]
param(
	# 이미 만든 기록용 배포 폴더를 다시 쓸 때 지정한다.
	[string]$RecordArchive,
	[ValidateRange(60, 1200)][int]$TimeoutSeconds = 600
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$workRoot = Join-Path $projectRoot "Saved/PsoRecord/$stamp"
New-Item -ItemType Directory -Force -Path $workRoot | Out-Null

# 한글 경로에서는 Build-ArtAssets.ps1이 만든 ASCII 사본에서 쿠크한다(Package-Windows.ps1과 같은 계산).
& (Join-Path $PSScriptRoot 'Build-ArtAssets.ps1') -CodeOnly
$buildRoot = $projectRoot
if ($projectRoot -match '[^\x00-\x7F]') {
	$bytes = [Text.Encoding]::UTF8.GetBytes($projectRoot.ToLowerInvariant())
	$sha = [Security.Cryptography.SHA256]::Create()
	try { $hashBytes = $sha.ComputeHash($bytes) } finally { $sha.Dispose() }
	$hash = -join ($hashBytes[0..3] | ForEach-Object { $_.ToString('x2') })
	$buildRoot = Join-Path $env:LOCALAPPDATA "IndieGame/AsciiBuild/Art_$hash"
}
$buildProject = Join-Path $buildRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$engineRoot = Split-Path (Split-Path (Split-Path $editor -Parent) -Parent) -Parent

if (-not $RecordArchive) {
	$RecordArchive = Join-Path $projectRoot "Saved/Packages/PsoRecord-$stamp"
	$uat = Join-Path $engineRoot 'Build/BatchFiles/RunUAT.bat'
	& $uat BuildCookRun "-project=$buildProject" -target=IndieGame -noP4 -unattended -utf8output `
		-platform=Win64 -clientconfig=Development -build -cook -allmaps -stage -pak -iostore -package `
		-compressed -prereqs -archive "-archivedirectory=$(Join-Path $RecordArchive 'Windows')"
	if ($LASTEXITCODE -ne 0) { throw "PSO 기록용 배포본 생성 실패: $LASTEXITCODE" }
}
$RecordArchive = (Resolve-Path -LiteralPath $RecordArchive).Path
$launcher = Get-ChildItem -LiteralPath (Join-Path $RecordArchive 'Windows') -Filter 'IndieGame.exe' -File | Select-Object -First 1
if (-not $launcher) { throw "PSO 기록용 배포본의 실행 파일이 없습니다: $RecordArchive" }

function Write-SessionConfig([string]$UserRoot, [int]$Quality) {
	$configRoot = Join-Path $UserRoot 'Saved/Config/Windows'
	New-Item -ItemType Directory -Force -Path $configRoot | Out-Null
	$groups = @('ViewDistance', 'AntiAliasing', 'Shadow', 'GlobalIllumination', 'Reflection', 'PostProcess',
		'Texture', 'Effects', 'Foliage', 'Shading', 'Landscape') | ForEach-Object { "sg.${_}Quality=$Quality" }
	$text = "[/Script/Engine.GameUserSettings]`nbUseVSync=False`nFrameRateLimit=0.000000`nVersion=5`n`n[ScalabilityGroups]`nsg.ResolutionQuality=100`n" + ($groups -join "`n") + "`n"
	[IO.File]::WriteAllText((Join-Path $configRoot 'GameUserSettings.ini'), $text, [Text.UTF8Encoding]::new($false))
	# 마스크는 위 .NOTES의 엔진 문제를 피한다. 기록은 원래 종료할 때만 저장해서, 스스로 강제
	# 종료하는 검사 장면(타이틀은 7초 만에 끝난다)은 아무것도 남기지 못했다. 2초마다 저장해 둔다.
	$engine = "[SystemSettings]`nr.ShaderPipelineCache.PreCompileMask=1`nr.ShaderPipelineCache.AutoSaveTimeBoundPSO=2`n"
	[IO.File]::WriteAllText((Join-Path $configRoot 'Engine.ini'), $engine, [Text.UTF8Encoding]::new($false))
}

# 실제로 그리는 경로만 기록한다. -nullrhi 검사는 PSO를 만들지 않는다.
$sessions = @(
	@{ Name = 'Frontend'; Quality = 2; Args = @('-IGFrontendShippingProbe', '-IGSettingsLayoutReview', '-IGCulture=ko',
		'-IGFrontendExpectedWidth=1920', '-IGFrontendExpectedHeight=1080') },
	@{ Name = 'ArrivalDay'; Quality = 2; Args = @('-IGMissingFloor', '-IGArrivalProbe', '-IGSkipFrontend') },
	@{ Name = 'ReadmeRoute'; Quality = 2; Args = @('-IGMissingFloor', '-IGArrivalCapture', '-IGReadmeCapture', '-IGSkipFrontend') },
	@{ Name = 'NightHigh'; Quality = 2; Args = @('-IGListenerGreybox', '-IGNightCapture', '-IGCaptureMetricsOnly', '-IGSkipFrontend') },
	@{ Name = 'NightLow'; Quality = 1; Args = @('-IGListenerGreybox', '-IGNightCapture', '-IGCaptureMetricsOnly', '-IGSkipFrontend') },
	@{ Name = 'Endings'; Quality = 2; Args = @('-IGListenerGreybox', '-IGListenerGreyboxProbe', '-IGMissingFloor', '-IGSkipFrontend', '-IGEndingCheckpointWrite') },
	@{ Name = 'NightFive'; Quality = 2; Args = @('-IGNightFiveProbe', '-IGCulture=ko') },
	@{ Name = 'StairSensor'; Quality = 2; Args = @('-IGListenerGreybox', '-IGStairSensorProbe', '-IGStairSensorShots', '-IGSkipFrontend') },
	@{ Name = 'NightFourHammer'; Quality = 2; Args = @('-IGListenerGreybox', '-IGNightFourHammerProbe', '-IGNightFourHammerShots', '-IGSkipFrontend') },
	@{ Name = 'DayScenes'; Quality = 2; Args = @('-IGListenerGreybox', '-IGDayScenesProbe', '-IGDayScenesShots', '-IGSkipFrontend') },
	@{ Name = 'Bathroom'; Quality = 2; Args = @('-IGListenerGreybox', '-IGBathroomProbe', '-IGBathroomShots', '-IGSkipFrontend') },
	@{ Name = 'Epilogue'; Quality = 2; Args = @('-IGListenerGreybox', '-IGEpiloguePreview=A', '-IGSkipFrontend') }
)
$records = [Collections.Generic.List[string]]::new()
foreach ($session in $sessions) {
	$sessionRoot = Join-Path $workRoot $session.Name
	$userRoot = Join-Path $sessionRoot 'User'
	Write-SessionConfig $userRoot $session.Quality
	$arguments = @('-unattended', '-nosplash', '-NoLoadingScreen', '-RenderOffscreen', '-d3d12', '-nosound',
		'-Windowed', '-ResX=1920', '-ResY=1080', '-ForceRes', '-logPSO',
		"-UserDir=$userRoot", "-IGMissingFloorResultPath=$(Join-Path $sessionRoot 'receipt.txt')") + $session.Args
	if ($session.Name -eq 'Frontend') {
		# 타이틀 검사는 결과와 화면을 남길 자리를 따로 받는다.
		$arguments += @("-IGFrontendResultPath=$(Join-Path $sessionRoot 'frontend.txt')")
		foreach ($shot in @('Accessibility', 'Display', 'Title', 'Default', '')) {
			$arguments += "-IGFrontend${shot}ScreenshotPath=$(Join-Path $sessionRoot "$shot.png")"
		}
	}
	$quoted = @($arguments | ForEach-Object { '"' + $_ + '"' })
	$process = Start-Process -FilePath $launcher.FullName -ArgumentList $quoted -WorkingDirectory $launcher.DirectoryName -WindowStyle Hidden -PassThru
	try {
		$null = $process.Handle
		if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
			& taskkill.exe /PID $process.Id /T /F | Out-Null
			Write-Warning "PSO 기록 시간 초과: $($session.Name). 그때까지 기록한 PSO만 쓴다."
		}
		elseif ($process.ExitCode -ne 0) {
			Write-Warning "PSO 기록 실행 종료 코드 $($process.ExitCode): $($session.Name)"
		}
	}
	finally { $process.Dispose() }
	$found = @(Get-ChildItem -LiteralPath $userRoot -Recurse -Filter '*.rec.upipelinecache' -File -ErrorAction SilentlyContinue)
	Write-Host "PSO_RECORD $($session.Name) files=$($found.Count)"
	foreach ($file in $found) { $records.Add($file.FullName) }
}
if ($records.Count -eq 0) { throw '기록된 PSO 파일이 없습니다. -logPSO가 기록용 배포본에서 동작하는지 확인해 주세요.' }

# 쿠크가 남긴 셰이더 안정 키. Zen 저장소를 써도 메타데이터는 파일로 남는다.
# D3D11용 SM5 키도 함께 남는데, 캐시에 섞이면 머리말의 형식과 키가 달라져 쿠크가 멈춘다.
$cookKeys = @(Get-ChildItem -LiteralPath (Join-Path $buildRoot 'Saved/Cooked/Windows') -Recurse -Filter '*.shk' -File |
	Where-Object { $_.FullName -match 'PipelineCaches' })
$stableKeys = @($cookKeys | Where-Object { $_.Name -like '*-PCD3D_SM6.shk' })
if ($stableKeys.Count -eq 0) { throw '셰이더 안정 키(.shk)가 없습니다. DefaultEngine.ini의 NeedsShaderStableKeys를 확인해 주세요.' }

# 엔진 도구에는 ASCII 경로로 넘긴다. 기록 파일은 한글 경로의 사용자 폴더에 있다.
$toolRoot = Join-Path $env:TEMP "MissingFloorPso/$stamp"
New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
$toolRecords = for ($index = 0; $index -lt $records.Count; $index++) {
	$copy = Join-Path $toolRoot ("record-$index.rec.upipelinecache")
	Copy-Item -LiteralPath $records[$index] -Destination $copy
	$copy
}
$output = Join-Path $toolRoot 'PSO_IndieGame_PCD3D_SM6.spc'
$toolArguments = @($buildProject, '-run=ShaderPipelineCacheTools', 'expand') + @($toolRecords) + @($stableKeys.FullName) + @($output, '-unattended', '-nopause', '-utf8output')
& $editor @toolArguments
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output)) { throw "PSO 캐시 합치기 실패: $LASTEXITCODE" }

# 쿠크와 같은 조건(모든 형식의 키)으로 한 번 읽어 본다. 여기서 못 읽는 캐시는 쿠크도 멈춘다.
$verified = Join-Path $toolRoot 'IndieGame_PCD3D_SM6.upipelinecache'
$verifyArguments = @($buildProject, '-run=ShaderPipelineCacheTools', 'build', $output) + @($cookKeys.FullName) + @($verified, '-unattended', '-nopause', '-utf8output')
& $editor @verifyArguments
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $verified)) { throw "만든 PSO 캐시를 쿠크 조건으로 읽지 못했습니다: $LASTEXITCODE" }

$destinationRoot = Join-Path $projectRoot 'Build/Windows/PipelineCaches'
New-Item -ItemType Directory -Force -Path $destinationRoot | Out-Null
Copy-Item -LiteralPath $output -Destination (Join-Path $destinationRoot 'PSO_IndieGame_PCD3D_SM6.spc') -Force
Write-Host "PSO_CACHE PASS records=$($records.Count) stableKeys=$($stableKeys.Count) bytes=$((Get-Item -LiteralPath $output).Length)"
