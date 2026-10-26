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
	[int]$TimeoutSeconds = 120,
	# 입주 조사 뒤 첫 외출(골목 오토바이)까지 겪고 자동 저장한 다음, 같은 사용자 폴더로 다시 켜서
	# 입주 상태와 오토바이 일이 이어지는지 본다.
	[switch]$SaveResume
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-MissingFloor-ArrivalProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
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
$userRoot = $null
if ($SaveResume) {
	$userRoot = Join-Path $projectRoot ('Saved\Validation\ArrivalSaveResume-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '\User')
	Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $userRoot) -Variant 'ArrivalSaveResume'
	$arguments += @('-IGArrivalSaveWrite', "-UserDir=$userRoot")
}

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

if ($SaveResume) {
	if (-not (Select-String -LiteralPath $runLog -Pattern 'MISSINGFLOOR_ARRIVAL near_miss=1 night=0' -Quiet)) {
		throw "첫 외출의 오토바이 일이 입주 저녁(밤 번호 0)에 기록되지 않았습니다: $runLog"
	}
	$resumeLog = Join-Path $logDirectory 'MissingFloorArrivalResume.log'
	if (Test-Path -LiteralPath $resumeLog -PathType Leaf) {
		Remove-Item -LiteralPath $resumeLog -Force
	}
	$resumeArguments = @($projectFile, '-game', '-unattended', '-nosplash', '-NoLoadingScreen', '-RenderOffScreen',
		'-nullrhi', '-nosound', '-stdout', '-FullStdOutLogOutput', "-abslog=$resumeLog", '-IGMissingFloor',
		'-IGArrivalSaveRead', '-IGArrivalExpectNearMiss', '-IGSkipFrontend', "-UserDir=$userRoot")
	$resume = Start-Process -FilePath $editor -ArgumentList $resumeArguments -PassThru -WindowStyle Hidden
	$null = $resume.Handle
	if (-not $resume.WaitForExit($TimeoutSeconds * 1000)) {
		try { & taskkill.exe /PID $resume.Id /T /F | Out-Null } catch {}
		throw "Arrival resume exceeded ${TimeoutSeconds}s: $resumeLog"
	}
	$resumeLine = @(Select-String -LiteralPath $resumeLog -Pattern 'MISSINGFLOOR_ARRIVAL_RESUME ')
	$resumeLine | ForEach-Object { Write-Host $_.Line }
	if ($resume.ExitCode -ne 0 -or $resumeLine.Count -eq 0 -or $resumeLine[-1].Line -notmatch 'remembered=1 parked=1') {
		throw "입주 저녁 저장을 다시 켰을 때 입주 상태나 오토바이 일이 이어지지 않았습니다(exit $($resume.ExitCode)): $resumeLog"
	}
	Write-Host 'MISSINGFLOOR_ARRIVAL_PROBE PASS save_resume=1 near_miss=1'
}

Write-Host 'MISSINGFLOOR_ARRIVAL_PROBE PASS route=production offscreen=true'

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
