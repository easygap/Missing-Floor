[CmdletBinding()]
param([ValidateSet(30, 60)][int]$FrameRate = 60)

# 위층 사람이 계단탑을 타고 층을 오가며 쫓고, 소리를 놓친 뒤 둘레를 뒤지는지 잰다.
# 밤2 관리실 붕괴처럼 1층에서 큰 소리를 두 번 내고, 그가 4층에서 내려와 뒤지다가
# 계단을 뛰어 올라가는 발소리를 따라 다시 올라오는지 본다. 순간이동, 디딤판 위로
# 뜨는 몸, 소리가 끊기자마자 순찰로 돌아가는 것을 실패로 친다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot "Saved/Logs/ListenerPursuit-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/ListenerPursuit-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
	-IGListenerGreybox -IGListenerPursuitProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" '-LogCmds=LogIndieGame Verbose' | Out-Null
$lines = Select-String -LiteralPath $logPath -Pattern 'PURSUIT_(CHECK|PROBE|METRICS|DESCENT|ASCENT|SEARCH_PAUSE)|LISTENER_(SEARCH|WEDGED)'
$lines | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'PURSUIT_PROBE PASS failures=0')) {
	throw "위층 사람 추격·수색 검사에 실패했습니다: $logPath"
}
