[CmdletBinding()]
param([ValidateSet(30, 60)][int]$FrameRate = 60)

# 뒤따르는 발을 겪는다. 4층 출입구에서 계단을 걸어 내려가며 한 층 뒤의 발소리, 멈춘 뒤의
# 한 발, 돌아봤을 때 고개를 드는 몸, 눈을 돌리면 남는 젖은 발자국을 차례로 잰다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot "Saved/Logs/Stairwell-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/Stairwell-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
	-IGStairwellProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
Select-String -LiteralPath $logPath -Pattern 'STAIRWELL_(CHECK|PROBE|WALK|LOOK|PRESENCE)' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'STAIRWELL_PROBE PASS failures=0')) {
	throw "뒤따르는 발 검사에 실패했습니다: $logPath"
}
