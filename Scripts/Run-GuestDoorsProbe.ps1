[CmdletBinding()]
param([switch]$Lure, [ValidateSet(30, 60)][int]$FrameRate = 60)

# 손님이 403호 말고 다른 문에도 오는지 잰다. 기본은 셋째 밤 401·402호를 거쳐 403호 걸쇠에
# 걸리는 것과 넷째 밤 5층 철문 빗장에 걸리는 것이다. -Lure는 옆집을 두드리는 사이 현관을
# 열었다 닫는 것과, 5층 철문을 열어 둔 채 기다리는 것이다. 손님이 오는 시간(그 시간
# 150초)까지 기다린다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$mode = if ($Lure) { 'Lure' } else { 'Doors' }
$logPath = Join-Path $projectRoot "Saved/Logs/GuestDoors-$mode-$FrameRate.log"
$probeUser = Join-Path $projectRoot ("Saved/Validation/GuestDoors-$mode-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
$probeArgs = @($projectPath, '-game', '-unattended', '-nosplash', '-nullrhi', '-nosound', '-NoLoadingScreen', '-RenderOffscreen',
	'-IGListenerGreybox', '-IGGuestDoorsProbe', '-IGSkipFrontend', '-UseFixedTimeStep', "-FPS=$FrameRate",
	"-UserDir=$probeUser", "-abslog=$logPath")
if ($Lure) { $probeArgs += '-IGGuestLure' }
& $editor @probeArgs | Out-Null
Select-String -LiteralPath $logPath -Pattern 'GUESTDOORS_(CHECK|PROBE|STAGE)' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'GUESTDOORS_PROBE PASS failures=0')) {
	throw "손님 문 검사에 실패했습니다: $logPath"
}
