<#
.SYNOPSIS
	§11 V5 밤 구간 10지점 히스토그램 검증.

.DESCRIPTION
	복도·별관·계단·옥상에서 5% 미만 암부와 98% 초과 하이라이트의 비율을 잰다.
	측정한 프레임을 Docs/Media/v5-*.png로 남기므로 수치와 화면을 함께 확인할 수 있다.
	카메라가 지정한 위치에서 벗어나거나 검은 화면만 찍히면 실패한다.

.NOTES
	D3D12로 화면을 렌더링하되 게임 창은 띄우지 않는다. High·100% 해상도로 실행하고,
	검사가 끝나면 기존 사용자 그래픽 설정을 복원한다.
#>
[CmdletBinding()]
param(
	[ValidateRange(640, 3840)]
	[int]$ResX = 1280,
	[ValidateRange(360, 2160)]
	[int]$ResY = 720,
	# 밴드를 저작할 때만 쓴다. 측정값을 보고하되 판정하지 않는다.
	[switch]$ReportOnly,
	[ValidateRange(60, 900)]
	[int]$TimeoutSeconds = 420
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-MissingFloor-NightHistogram.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectFile = Join-Path $projectRoot 'IndieGame.uproject'
if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
	throw "언리얼 프로젝트를 찾을 수 없습니다: $projectFile"
}

$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') `
	-ProjectPath $projectFile -Commandlet
if ([string]::IsNullOrWhiteSpace($editor)) {
	throw 'IndieGame.uproject에 맞는 언리얼 에디터를 찾을 수 없습니다.'
}

$logDirectory = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Force $logDirectory | Out-Null
$runLog = Join-Path $logDirectory 'MissingFloorNightHistogram.log'
if (Test-Path -LiteralPath $runLog) {
	Remove-Item -LiteralPath $runLog -Force
}

$arguments = @(
	$projectFile,
	'-game',
	'-unattended',
	'-nosplash',
	'-NoLoadingScreen',
	# 창을 만들지 않는 플래그. 아래 가드가 이것의 존재를 강제한다.
	'-RenderOffScreen',
	'-d3d12',
	'-nosound',
	"-ResX=$ResX",
	"-ResY=$ResY",
	'-ForceRes',
	'-NoVSync',
	'-ExecCmds="Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100"',
	'-stdout',
	'-FullStdOutLogOutput',
	"-abslog=$runLog",
	'-IGListenerGreybox',
	'-IGSkipFrontend',
	'-IGNightHistogram'
)
if ($ReportOnly) {
	$arguments += '-IGNightHistogramReport'
}

# 창이 뜨지 않는다는 보장은 이 두 줄이다. 인자를 손대다 플래그가 빠지면
# 실행되지 않고 여기서 멈춘다.
if ($arguments -notcontains '-RenderOffScreen') {
	throw '-RenderOffScreen 없이는 실행하지 않습니다. 게임 창이 뜰 수 있습니다.'
}
foreach ($forbidden in @('-windowed', '-fullscreen', '-game -log')) {
	if ($arguments -contains $forbidden) {
		throw "화면을 띄울 수 있는 인자가 포함되었습니다: $forbidden"
	}
}

Write-Host (
	"§11 V5 히스토그램 스윕 시작 — ${ResX}x${ResY}, 오프스크린, 창 없음") `
	-ForegroundColor DarkGray
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$savedSettings = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
try {
	$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
	# Windows PowerShell 5.1은 핸들을 먼저 잡아 두지 않으면 ExitCode를 비워 둔다.
	$null = $process.Handle
	if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
		try { & taskkill.exe /PID $process.Id /T /F | Out-Null } catch {}
		throw "히스토그램 스윕이 ${TimeoutSeconds}초 안에 끝나지 않았습니다: $runLog"
	}
	if ($process.ExitCode -ne 0) { throw "히스토그램 실행 실패: $runLog" }
} finally {
	if ($null -ne $savedSettings) { [IO.File]::WriteAllBytes($settingsPath, $savedSettings) }
	elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
if (-not (Test-Path -LiteralPath $runLog -PathType Leaf)) {
	throw "히스토그램 로그가 생성되지 않았습니다: $runLog"
}

$measurements = Select-String -Path $runLog -Pattern 'MISSINGFLOOR_V5 point=' |
	ForEach-Object { ($_.Line -split 'MISSINGFLOOR_V5 ')[-1] }
foreach ($measurement in $measurements) {
	$colour = if ($measurement -match 'OK$') { 'Green' } else { 'Yellow' }
	Write-Host "  $measurement" -ForegroundColor $colour
}

$failed = Select-String -Path $runLog -Pattern 'MISSINGFLOOR_V5 FAIL'
if ($failed) {
	$failed | ForEach-Object { Write-Host $_.Line -ForegroundColor Red }
	throw "§11 V5 히스토그램 검증이 실패했습니다. 로그: $runLog"
}
if ($ReportOnly) {
	$report = Select-String -Path $runLog -Pattern 'MISSINGFLOOR_V5 REPORT'
	if (-not $report) {
		throw "§11 V5 보고 패스가 끝까지 돌지 않았습니다. 로그: $runLog"
	}
	Write-Host $report[-1].Line -ForegroundColor Cyan
	return
}

$passed = Select-String -Path $runLog -Pattern 'MISSINGFLOOR_V5 PASS'
if (-not $passed) {
	throw "§11 V5가 PASS를 남기지 않았습니다. 로그: $runLog"
}
Write-Host $passed[-1].Line -ForegroundColor Green

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
