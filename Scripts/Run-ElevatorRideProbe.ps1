[CmdletBinding()]
param([ValidateSet(30, 60)][int]$FrameRate = 60)

# 승강기를 실제로 타 본다. 4층에서 불러 1층까지 내려가고, 2층에서 불러 3층으로 올라간다.
# 칸이 승강로를 실제 시간에 걸쳐 오르내리는지, 탄 사람이 칸 바닥을 딛고 따라가는지,
# 한 번이라도 순간이동하는지, 그 시간에는 버튼을 눌러도 칸이 서 있는지 잰다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot "Saved/Logs/ElevatorRide-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/ElevatorRide-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
	-IGElevatorRideProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
Select-String -LiteralPath $logPath -Pattern 'ELEVATOR_(CHECK|PROBE|CALL|RIDE|EXIT)' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'ELEVATOR_PROBE PASS failures=0')) {
	throw "승강기 검사에 실패했습니다: $logPath"
}
