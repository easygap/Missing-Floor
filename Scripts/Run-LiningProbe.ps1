[CmdletBinding()]
param(
	[ValidateSet(30, 60)][int]$FrameRate = 60,
	[switch]$Shots,
	# -Shots와 함께: 몸판·다리·패드 충돌 모양을 선으로 그려 보이는 몸과 맞춰 본다.
	[switch]$Collision,
	# -Shots와 함께: 큰 점검구로 옮겨 가는 동안을 문간에서 30fps로 이어 찍는다(Saved/LiningFrames).
	[switch]$Frames
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
# 기본도 오프스크린으로 그린다. 처음에는 else에서 @('-nullrhi')를 돌려줬는데, if 식이 원소 하나짜리 배열을 문자열로 풀어
# @render가 글자를 하나씩 넘겼고(- n u l l r h i) 엔진은 그걸 모르는 인자로 버렸다. 이 검사의 기준은 모두 그려 가며 돈
# 실행에서 잡혔다.
$render = @(if ($Shots) { '-d3d12', '-Windowed', '-ResX=1280', '-ResY=720', '-ForceRes', '-IGLiningShots' })
if ($Shots -and $Collision) { $render += '-IGLiningDrawCollision' }
if ($Shots -and $Frames) { $render += '-IGLiningFrames' }
& $editor $projectPath -game -unattended -nosplash -nosound -NoLoadingScreen -RenderOffscreen @render `
	-IGListenerGreybox -IGLiningProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
Select-String -LiteralPath $logPath -Pattern 'LINING_(CHECK|PROBE|BODY|TARGET|SEALED|GRIP|ARRIVED|SOFT)' | ForEach-Object { Write-Host $_.Line }
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
