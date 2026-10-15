[CmdletBinding()]
param(
	[ValidateSet(30, 60)][int]$FrameRate = 60,
	[switch]$Shots
)

# 덧댄 것이 열린 틈 가운데 넓은 쪽을 골라 가서 누르는지, 누르는 동안 덮개가 다리에 걸려 튕기는지,
# 가는 도중에 다른 틈이 열려도 방향을 꺾지 않는지, 막은 틈은 다시 뜯어야 대상이 되는지 잰다.
# 302호 벽에 점검구 둘을 붙여 빌려 쓴다. -Shots면 오프스크린 D3D로 걸음마다 찍는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-LiningProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot "Saved/Logs/Lining-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/Lining-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
$render = if ($Shots) { @('-d3d12', '-Windowed', '-ResX=1280', '-ResY=720', '-ForceRes', '-IGLiningShots') } else { @('-nullrhi') }
& $editor $projectPath -game -unattended -nosplash -nosound -NoLoadingScreen -RenderOffscreen @render `
	-IGListenerGreybox -IGLiningProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
Select-String -LiteralPath $logPath -Pattern 'LINING_(CHECK|PROBE|BODY|TARGET|SEALED|GRIP|ARRIVED)' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'LINING_PROBE PASS failures=0')) {
	throw "덧댄 것 검사에 실패했습니다: $logPath"
}
if ($Shots) {
	Write-Host ("LINING_SHOTS " + (Join-Path $probeUser 'Saved/LiningReview'))
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
