[CmdletBinding()]
param([ValidateSet(30, 60)][int]$FrameRate = 60)

# 첫째 밤의 계단탑에서 센서등과 어둑시니를 겪는다. 2층 참에서 걸으면 켜지고 멈추면
# 9초 뒤에 꺼지는지, 차단기가 내려가 있으면 켜지지 않는지, 3층 참에 멈춰 서면 등이 일찍
# 꺼지고 계단 아래 어둠에 어둑시니가 서는지, 한 걸음 움직여 등이 켜지면 사라지는지 잰다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot "Saved/Logs/StairSensor-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/StairSensor-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
	-IGListenerGreybox -IGStairSensorProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
Select-String -LiteralPath $logPath -Pattern 'STAIR_SENSOR_(CHECK|PROBE|WALK|INTRO)' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'STAIR_SENSOR_PROBE PASS failures=0')) {
	throw "계단 센서등 검사에 실패했습니다: $logPath"
}
