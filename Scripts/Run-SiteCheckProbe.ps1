[CmdletBinding()]
param([ValidateSet(30, 60)][int]$FrameRate = 60)

# 신고 다음 날 아침의 현장 확인을 두 번 겪는다. 한 번은 옥상에서 두 사람이 올라오는 걸 보고
# 5층 안에서 경찰관이 벽을 두드리고 말하는 걸 듣는다. 한 번은 문 닫은 403호에 있다가 2분 뒤
# 두 사람끼리 두드린 결과를 문자로 받는다. 둘 다 끝나면 두 사람이 사라지고 철문이 닫혀야 한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
foreach ($scenario in @('Witness', 'Away')) {
	$logPath = Join-Path $projectRoot "Saved/Logs/SiteCheck-$scenario-$FrameRate.log"
	$probeUser = Join-Path $projectRoot ("Saved/Validation/SiteCheck-$scenario-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
	$extra = @()
	if ($scenario -eq 'Away') { $extra += '-IGSiteCheckAway' }
	& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
		-IGListenerGreybox -IGSiteCheckProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
		"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
	Write-Host "== $scenario"
	Select-String -LiteralPath $logPath -Pattern 'SITECHECK' | ForEach-Object { Write-Host $_.Line }
	if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'SITECHECK_PROBE PASS failures=0')) {
		throw "경찰 현장 확인 검사($scenario)에 실패했습니다: $logPath"
	}
}
