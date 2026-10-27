[CmdletBinding()]
param(
	[ValidateSet(30, 60)][int]$FrameRate = 60,
	# 입주 저녁에 골목에서 오토바이와 스치지 않은 회차로 본다. 첫째 낮 정우가 처음 보는 이웃으로 인사해야 한다.
	[switch]$NoNearMiss
)

# 새 이웃과 낮 장면을 겪는다. 입주 저녁의 황순금, 첫째 낮(303호 둘째 쪽지, 동네장터 두 장, 정우의
# 인사), 둘째 낮(셋째 쪽지, 정우의 글, 엄마 목소리 이야기, 손 있는 날)과 문과 같이 도는 쪽지다.
# 쪽지가 붙는 새벽마다 403호 안에서 종이 붙이는 소리가 나야 한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-DayScenesProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$tag = if ($NoNearMiss) { 'DayScenes-NoNearMiss' } else { 'DayScenes' }
$extra = @()
if ($NoNearMiss) { $extra += '-IGDayScenesNoNearMiss' }
$probeUser = Join-Path $projectRoot ("Saved/Validation/$tag-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
$logPath = Join-Path (Split-Path -Parent $probeUser) "$tag-$FrameRate.log"
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
$arguments = @(
	('"{0}"' -f $projectPath), '-game', '-unattended', '-nosplash', '-nullrhi', '-nosound',
	'-NoLoadingScreen', '-RenderOffscreen', '-IGListenerGreybox', '-IGDayScenesProbe',
	'-IGSkipFrontend', '-UseFixedTimeStep', "-FPS=$FrameRate",
	('"-UserDir={0}"' -f $probeUser), ('"-abslog={0}"' -f $logPath)
) + $extra
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
$null = $process.Handle
if (-not $process.WaitForExit(900000)) {
	if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction Stop }
	throw "낮 장면 검사 시간 초과: $logPath"
}
$exitCode = $process.ExitCode
Select-String -LiteralPath $logPath -Pattern 'DAYSCENES_(CHECK|PROBE)|MISSINGFLOOR_DAY' | ForEach-Object { Write-Host $_.Line }
$posted = @(Select-String -LiteralPath $logPath -Pattern 'MISSINGFLOOR_DAY note303_posted_sound').Count
if ($posted -ne 2) {
	throw "쪽지 붙이는 소리가 두 번 나야 하는데 $posted 번이었습니다: $logPath"
}
if ($exitCode -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'DAYSCENES_PROBE PASS failures=0')) {
	throw "낮 장면 검사에 실패했습니다: $logPath"
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
