[CmdletBinding()]
param([ValidateSet(30, 60, 120)][int]$FrameRate = 60)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot "Saved/Logs/GameplayRealism-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/GameplayRealism-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
# 검사 중 접근성 설정을 바꿔도 실제 플레이 설정과 저장 파일은 건드리지 않는다.
& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
	-IGGameplayRealismProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" "-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'REALISM_PROBE PASS failures=0')) {
	Select-String -LiteralPath $logPath -Pattern 'REALISM_' | ForEach-Object { Write-Host $_.Line }
	throw "게임플레이 검사에 실패했습니다: $logPath"
}
Select-String -LiteralPath $logPath -Pattern 'REALISM_' | ForEach-Object { Write-Host $_.Line }
