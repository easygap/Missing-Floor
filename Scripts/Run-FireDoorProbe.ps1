[CmdletBinding()]
param([ValidateSet(30, 60)][int]$FrameRate = 60)

# 2·3·4층 계단 목의 방화문을 겪는다. 세 층 모두 고임목으로 괴어 열려 있는지, 2층 고임목을
# 툭 빼면 쾅 닫히고 3층 고임목을 길게 누르면 조용히 닫히는지, 닫힌 2층 문 앞에서 위층
# 사람이 두드리는지, 셋째 밤의 관리인이 그 문을 열고 지나가는지 잰다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot "Saved/Logs/FireDoor-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/FireDoor-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
	-IGListenerGreybox -IGFireDoorProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
Select-String -LiteralPath $logPath -Pattern 'FIRE_DOOR_(CHECK|PROBE|NOISE|LEAF|LISTENER|MANAGER)' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'FIRE_DOOR_PROBE PASS failures=0')) {
	throw "방화문 검사에 실패했습니다: $logPath"
}
