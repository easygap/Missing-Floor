[CmdletBinding()]
param([ValidateSet(30, 60)][int]$FrameRate = 60)

# 밤4 망치질 사이를 두 번 겪는다. 한 번은 5층 철문 빗장을 걸어 두어 철문 밖의 부름이 열쇠를 꽂다
# 걸려 가고, 정전 속 어둑시니가 선 뒤 다섯째 타에 다 그친다. 한 번은 빗장 없이 두어 들어온
# 손님에게 잡히면 결말 C가 아니라 침대로 가고, 깨어난 403호도 깜깜해야 한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
foreach ($scenario in @('Bolted', 'Open')) {
	$logPath = Join-Path $projectRoot "Saved/Logs/NightFourHammer-$scenario-$FrameRate.log"
	$probeUser = Join-Path $projectRoot ("Saved/Validation/NightFourHammer-$scenario-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
	$extra = @()
	if ($scenario -eq 'Open') { $extra += '-IGNightFourHammerOpen' }
	& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
		-IGListenerGreybox -IGNightFourHammerProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
		"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
	Write-Host "== $scenario"
	Select-String -LiteralPath $logPath -Pattern 'NIGHT4HAMMER|NIGHT4_HAMMER_CALL|NIGHT4_WALL_ANSWER' | ForEach-Object { Write-Host $_.Line }
	if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'NIGHT4HAMMER_PROBE PASS failures=0')) {
		throw "밤4 망치질 검사($scenario)에 실패했습니다: $logPath"
	}
}
