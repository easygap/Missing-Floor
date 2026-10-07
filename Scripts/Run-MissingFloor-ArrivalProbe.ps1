<#
.SYNOPSIS
	Production entry smoke test for 「없는 층」.

.DESCRIPTION
	Runs the same -IGMissingFloor route used by the title menu, verifies that the
	safe-evening arrival stage owns the world before Night 1, checks all seven
	arrival props and their initial gates, and confirms the moving boxes resolve
	to the baked PBR cardboard material. The run is NullRHI and offscreen so it
	never opens a game window.
#>
[CmdletBinding()]
param(
	[ValidateRange(30, 300)]
	[int]$TimeoutSeconds = 120
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
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$runLog = Join-Path $logDirectory 'MissingFloorArrivalProbe.log'
if (Test-Path -LiteralPath $runLog -PathType Leaf) {
	Remove-Item -LiteralPath $runLog -Force
}

$arguments = @(
	$projectFile,
	'-game',
	'-unattended',
	'-nosplash',
	'-NoLoadingScreen',
	'-RenderOffScreen',
	'-nullrhi',
	'-nosound',
	'-stdout',
	'-FullStdOutLogOutput',
	"-abslog=$runLog",
	'-IGMissingFloor',
	'-IGArrivalProbe',
	'-IGSkipFrontend'
)

Write-Host 'MISSINGFLOOR_ARRIVAL_PROBE running offscreen NullRHI'
$process = Start-Process -FilePath $editor -ArgumentList $arguments `
	-PassThru -WindowStyle Hidden
# Windows PowerShell 5.1은 핸들을 먼저 잡아 두지 않으면 ExitCode를 비워 둔다.
$null = $process.Handle
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
	try { & taskkill.exe /PID $process.Id /T /F | Out-Null } catch {}
	throw "Arrival probe exceeded ${TimeoutSeconds}s: $runLog"
}
if (-not (Test-Path -LiteralPath $runLog -PathType Leaf)) {
	throw "Arrival probe log was not created: $runLog"
}
$receipt = Select-String -LiteralPath $runLog -Pattern 'MISSINGFLOOR_ARRIVAL (PASS|FAIL)'
if (-not $receipt) {
	throw "Arrival probe emitted no receipt: $runLog"
}
$receipt | ForEach-Object { Write-Host $_.Line }
if ($process.ExitCode -ne 0 -or $receipt[-1].Line -notmatch ' PASS ') {
	throw "Arrival probe failed with exit code $($process.ExitCode): $runLog"
}

Write-Host 'MISSINGFLOOR_ARRIVAL_PROBE PASS route=production offscreen=true'
