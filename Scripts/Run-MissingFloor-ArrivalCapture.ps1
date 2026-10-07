<#
.SYNOPSIS
	Renders the production arrival boxes and contract for visual review.
#>
[CmdletBinding()]
param(
	[ValidateRange(640, 3840)][int]$ResX = 1920,
	[ValidateRange(360, 2160)][int]$ResY = 1080,
	[ValidateRange(60, 600)][int]$TimeoutSeconds = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectFile = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') `
	-ProjectPath $projectFile -Commandlet
if ([string]::IsNullOrWhiteSpace($editor)) {
	throw 'IndieGame.uproject에 맞는 UnrealEditor-Cmd.exe를 찾을 수 없습니다.'
}

$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$mediaDirectory = Join-Path $projectRoot 'Docs\Media\readme'
New-Item -ItemType Directory -Force -Path $logDirectory, $mediaDirectory | Out-Null
$runLog = Join-Path $logDirectory 'MissingFloorArrivalCapture.log'
$captures = @(
	(Join-Path $mediaDirectory 'arrival-moving-boxes.png'),
	(Join-Path $mediaDirectory 'arrival-contract.png')
)
foreach ($path in @($runLog) + $captures) {
	if (Test-Path -LiteralPath $path -PathType Leaf) {
		Remove-Item -LiteralPath $path -Force
	}
}
$startedAt = Get-Date
$arguments = @(
	$projectFile,
	'-game', '-unattended', '-nosplash', '-NoLoadingScreen',
	'-RenderOffScreen', '-d3d12', '-nosound', '-NoVSync',
	"-ResX=$ResX", "-ResY=$ResY", '-ForceRes',
	'-stdout', '-FullStdOutLogOutput', "-abslog=$runLog",
	'-IGMissingFloor', '-IGArrivalCapture', '-IGSkipFrontend'
)
if ($arguments -notcontains '-RenderOffScreen' -or $arguments -notcontains '-d3d12') {
	throw 'Arrival capture requires offscreen D3D12.'
}

Write-Host "MISSINGFLOOR_ARRIVAL_CAPTURE running ${ResX}x${ResY} offscreen D3D12"
$process = Start-Process -FilePath $editor -ArgumentList $arguments `
	-PassThru -WindowStyle Hidden
# Windows PowerShell 5.1은 핸들을 먼저 잡아 두지 않으면 ExitCode를 비워 둔다.
$null = $process.Handle
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
	try { & taskkill.exe /PID $process.Id /T /F | Out-Null } catch {}
	throw "Arrival capture exceeded ${TimeoutSeconds}s: $runLog"
}
$receipt = Select-String -LiteralPath $runLog `
	-Pattern 'MISSINGFLOOR_ARRIVAL_CAPTURE (PASS|FAIL)'
if ($process.ExitCode -ne 0 -or -not $receipt -or $receipt[-1].Line -notmatch ' PASS ') {
	throw "Arrival capture failed with exit code $($process.ExitCode): $runLog"
}
foreach ($capture in $captures) {
	$exists = Test-Path -LiteralPath $capture -PathType Leaf
	$isFresh = $exists -and (Get-Item -LiteralPath $capture).LastWriteTime -ge $startedAt
	if (-not $isFresh) {
		throw "Arrival capture is missing or stale: $capture"
	}
}
Write-Host $receipt[-1].Line
Write-Host 'MISSINGFLOOR_ARRIVAL_CAPTURE_HARNESS PASS fresh_shots=2'
