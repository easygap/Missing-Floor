[CmdletBinding()]
param(
	[ValidateSet(30, 60)][int]$FrameRate = 60,
	# 오프스크린 D3D로 쉬기 안내·천장 귀 기울이기·생각·노크 자막을 찍는다(Saved/NightFlowReview).
	[switch]$Shots,
	# en·ja·zh-Hans·zh-Hant로 찍으면 그 언어 문장을 본다.
	[string]$Culture
)

# 밤의 일을 마친 것과 새벽을 나눈 흐름을 실제 감독으로 겪는다. 그레이박스 검사 표시 없이 돌아서
# 게임과 같은 길을 탄다: 목표를 적어도 밤이 이어지는가, 밤의 침대가 「아침까지 쉬기」로 바뀌고
# 그걸로 날이 새는가, 첫째 밤 천장 밑에서 귀를 기울이면 그가 오는가, 문서를 읽는 동안 이야기
# 시계가 서는가, 05:30이 읽기를 기다리는가, 못 채운 밤이 되풀이되는가, 불러온 밤의 침대가 다시
# 열리는가. 장면은 순간 이동과 상태 주입으로 만들므로 사람의 플레이 검수를 대신하지 않는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-NightFlowProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$tag = $(if ($Culture) { "-$Culture" } else { '' }) + $(if ($Shots) { '-shots' } else { '' })
$logPath = Join-Path $projectRoot "Saved/Logs/NightFlow$tag-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/NightFlow' + $tag + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
# 기본도 오프스크린으로 그린다. 화면을 그리지 않으면(-nullrhi) HUD가 대사 줄을 넘기지 않아서, 말이 끝나기를 기다리는
# 새벽이 40초 한도까지 밀린다. 처음에는 else에서 @('-nullrhi')를 돌려줬는데, if 식이 원소 하나짜리 배열을 문자열로 풀어
# @render가 글자를 하나씩 넘겼고(- n u l l r h i) 엔진은 그걸 모르는 인자로 버렸다. 이 검사는 처음부터 그려 가며 돌았다.
$render = @(if ($Shots) { '-d3d12', '-Windowed', '-ResX=1280', '-ResY=720', '-ForceRes', '-IGNightFlowShots' })
$extra = @()
if ($Culture) { $extra += ('-IGCulture=' + $Culture) }
& $editor $projectPath -game -unattended -nosplash -nosound -NoLoadingScreen -RenderOffscreen @render `
	-IGListenerGreybox -IGNightFlowProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
Select-String -LiteralPath $logPath -Pattern 'NIGHTFLOW_(CHECK|PROBE|LISTENER)' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'NIGHTFLOW_PROBE PASS failures=0')) {
	throw "밤 흐름 검사에 실패했습니다: $logPath"
}
if ($Shots) {
	Write-Host ("NIGHTFLOW_SHOTS " + (Join-Path $probeUser 'Saved/NightFlowReview'))
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
