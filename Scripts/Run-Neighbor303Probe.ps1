[CmdletBinding()]
param([ValidateSet(30, 60)][int]$FrameRate = 60)

# 셋째 낮 계단의 303호를 두 번 겪는다. 한 번은 문 닫은 403호 안에 있다가 4층 계단 목에 선
# 303호를 복도에서 만나고, 한 번은 3층과 4층 사이 계단에 서 있다가 올라오는 303호와 계단에서
# 마주친다. 둘 다 세 줄을 나눈 뒤 303호가 계단으로 내려가 건물을 나가야 한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
foreach ($scenario in @('Neck', 'Stairs')) {
	$logPath = Join-Path $projectRoot "Saved/Logs/Neighbor303-$scenario-$FrameRate.log"
	$probeUser = Join-Path $projectRoot ("Saved/Validation/Neighbor303-$scenario-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
	$extra = @()
	if ($scenario -eq 'Stairs') { $extra += '-IGNeighbor303Stairs' }
	& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
		-IGListenerGreybox -IGNeighbor303Probe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
		"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
	Write-Host "== $scenario"
	Select-String -LiteralPath $logPath -Pattern 'NEIGHBOR303' | ForEach-Object { Write-Host $_.Line }
	if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'NEIGHBOR303_PROBE PASS failures=0')) {
		throw "303호 계단 검사($scenario)에 실패했습니다: $logPath"
	}
}
