[CmdletBinding()]
param([ValidateSet(30, 60)][int]$FrameRate = 60)

# 403호 욕실에 숨어 본다. 문을 열고 들어가 안에서 닫고 잠그면 숨은 것이 되는지, 욕실 안
# 소리에 위층 사람이 욕실 문 앞에 와서 두드리는지, 셋째 밤 손님이 잠긴 손잡이만 덜컥거리고
# 나가는지, 잠금을 풀면 숨음이 끝나는지 잰다. 손님이 오는 시간(그 시간 150초)까지 기다린다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot "Saved/Logs/Bathroom-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/Bathroom-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
	-IGListenerGreybox -IGBathroomProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
Select-String -LiteralPath $logPath -Pattern 'BATHROOM_(CHECK|PROBE|LISTENER|GUEST)' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'BATHROOM_PROBE PASS failures=0')) {
	throw "욕실 검사에 실패했습니다: $logPath"
}
