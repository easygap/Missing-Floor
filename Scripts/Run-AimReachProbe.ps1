[CmdletBinding()]
param(
	[ValidateRange(60, 900)][int]$TimeoutSeconds = 420
)

# 밤과 낮을 차례로 열고, 그때 켜져 있는 상호작용 물체마다 사람이 설 수 있는 바닥에서 눈높이(서기·앉기)로
# 시선이 손 닿는 거리 안에 닿는지 본다. 걸어서 그 자리에 갈 수 있는지는 실제 입력 길 검사가 본다.
# 화면과 소리 없이 돈다. 결과는 AIM_REACH 줄이다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-AimReachProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot 'Saved/Logs/AimReach.log'
$probeRoot = Join-Path $projectRoot ('Saved/Validation/AimReach-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$probeUser = Join-Path $probeRoot 'User'
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory $probeRoot -Variant 'AimReach'
$arguments = @(
	('"{0}"' -f $projectPath), '-game', '-unattended', '-nosplash', '-NoLoadingScreen', '-RenderOffscreen', '-nullrhi', '-nosound',
	# 입주 저녁의 조사 물건까지 서는 실제 게임 무대로 연다.
	'-IGListenerGreybox', '-IGMissingFloor', '-IGAimReachProbe',
	'-IGSkipFrontend', '-UseFixedTimeStep', '-FPS=30',
	('"-UserDir={0}"' -f $probeUser), ('"-abslog={0}"' -f $logPath)
)
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
# Windows PowerShell 5.1은 핸들을 먼저 잡아 두지 않으면 ExitCode를 비워 둔다.
$null = $process.Handle
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
	& taskkill.exe /PID $process.Id /T /F | Out-Null
	throw "조준 감사 시간 초과: $logPath"
}
Select-String -LiteralPath $logPath -Pattern 'AIM_REACH' | ForEach-Object { Write-Host $_.Line }
if ($process.ExitCode -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'AIM_REACH_PROBE PASS')) {
	throw "조준 감사에 실패했습니다: $logPath"
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
