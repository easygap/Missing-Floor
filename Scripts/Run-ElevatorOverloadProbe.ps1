[CmdletBinding()]
param([switch]$Leave, [ValidateSet(30, 60)][int]$FrameRate = 60)

# 만원을 실제로 겪는다. 첫 밤 뒤의 낮으로 맞춘 무대에서 4층 칸에 타 1층을 누른다.
# 버티면 칸이 4층을 지나 「5」까지 올라가 손바닥 자국 벽 앞에서 문을 열고, 떨어졌다가
# 누른 층에 내려 준다. -Leave면 부저가 우는 동안 내리고, 빈 칸이 「5」에 갔다 돌아온다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$variant = if ($Leave) { 'Leave' } else { 'Stay' }
$logPath = Join-Path $projectRoot "Saved/Logs/ElevatorOverload-$variant-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/ElevatorOverload-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
$extra = @()
if ($Leave) { $extra += '-IGElevatorOverloadLeave' }
& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
	-IGElevatorOverloadProbe @extra -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
Select-String -LiteralPath $logPath -Pattern 'ELEVATOR_(CHECK|PROBE|OVERLOAD)' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'ELEVATOR_PROBE PASS failures=0')) {
	throw "만원 검사에 실패했습니다: $logPath"
}
