[CmdletBinding()]
param([ValidateSet(30, 60)][int]$FrameRate = 60)

# 2026-10-13에 고친 겹침과 구멍을 한 실행에서 다시 겪는다. 벽의 대답을 기다리는 동안과 녹음기를
# 듣는 동안 다른 괴이와 자동 저장이 비켜서는지, 단서보다 먼저 친 박자, 한 번씩 치는 노크의 한도,
# 첫 노크에 문을 열어 미룬 방문과 다시 열린 시간, 열어 둔 현관, 셋째 밤 유인 때 문 닫기, 걸쇠가
# 뜯긴 밤의 재방문과 잠근 욕실, 잡힌 뒤와 첫째 밤의 어둠, 녹음기가 잡히면 멈추는지를 잰다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-HorrorRegressionProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot "Saved/Logs/HorrorRegression-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/HorrorRegression-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
	-IGListenerGreybox -IGHorrorRegressionProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
Select-String -LiteralPath $logPath -Pattern 'HORROR_REGRESSION' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'HORROR_REGRESSION_PROBE PASS failures=0')) {
	throw "공포 회귀 검사에 실패했습니다: $logPath"
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
