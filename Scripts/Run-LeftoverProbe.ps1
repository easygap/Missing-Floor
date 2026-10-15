[CmdletBinding()]
param(
	[ValidateSet(30, 60)][int]$FrameRate = 60,
	# dawn: 밤3에 정우를 꺼내지 않고 새벽을 맞는 길. night2: 밤2 안쪽 방만.
	# wall: 밤4 벽 단계마다 저장 상태로 되살린 모습(불러오기 길).
	[ValidateSet('full', 'dawn', 'night2', 'wall')][string]$Variant = 'full',
	[switch]$Shots,
	# en·ja·zh-Hans·zh-Hant로 찍으면 이 줄기의 글(안내문·대장·힌트·자막)을 그 언어로 본다.
	[string]$Culture,
	# 2면 자막·안내 글자를 가장 크게 키운 설정으로 본다.
	[ValidateSet(1, 2)][int]$TextScale = 1,
	# -Shots와 함께: 1920x1080으로 HUD 없이 찍는다(소개 문서 화면).
	[switch]$Clean,
	# -Shots와 함께: 덧댄 것이 움직이는 구간을 30fps로 이어 찍는다(Saved/LeftoverFrames, 검수용 영상 재료).
	[switch]$Frames
)

# 「302호에 남은 짐」을 실제 감독·관리인·덧댄 것·정우로 한 실행에서 겪는다. 낮1 짐 장면, 밤2 안쪽 방과
# 손수레 심부름, 낮2 정우, 밤3 302호(쐐기·장롱·큰 점검구·판 받치기·같이 나가기), 덮개 조립, 5층 대 보기,
# 밤4 5층 덧댄 것, 저장 왕복과 확장 전 저장. -Shots면 오프스크린 D3D로 걸음마다 찍는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-LeftoverProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$tag = $Variant + $(if ($Culture) { "-$Culture" } else { '' }) + $(if ($TextScale -gt 1) { "-x$TextScale" } else { '' }) + $(if ($Clean) { '-clean' } else { '' })
$logPath = Join-Path $projectRoot "Saved/Logs/Leftover-$tag-$FrameRate.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/Leftover-' + $tag + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
$extra = @()
if ($Culture) { $extra += ('-IGCulture=' + $Culture) }
if ($TextScale -gt 1) { $extra += ('-IGCaptionScale=' + $TextScale) }
if ($Shots -and $Frames) { $extra += '-IGLeftoverFrames' }
$render = if ($Shots -and $Clean) { @('-d3d12', '-Windowed', '-ResX=1920', '-ResY=1080', '-ForceRes', '-IGLeftoverShots', '-IGLeftoverCleanShots') }
	elseif ($Shots) { @('-d3d12', '-Windowed', '-ResX=1280', '-ResY=720', '-ForceRes', '-IGLeftoverShots') } else { @('-nullrhi') }
$probeArg = if ($Variant -eq 'full') { '-IGLeftoverProbe' } else { "-IGLeftoverProbe=$Variant" }
& $editor $projectPath -game -unattended -nosplash -nosound -NoLoadingScreen -RenderOffscreen @render `
	-IGListenerGreybox $probeArg -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
Select-String -LiteralPath $logPath -Pattern 'LEFTOVER_(CHECK|PROBE|N2|N3|DAY1|TAKEN|LINING)|MANAGER_(ERRAND|CHASE|CATCH|INVESTIGATE)|LINING_(TARGET|SEALED|GRIP|PLACED)' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'LEFTOVER_PROBE PASS failures=0')) {
	throw "남은 짐 검사에 실패했습니다: $logPath"
}
if ($Shots) {
	Write-Host ("LEFTOVER_SHOTS " + (Join-Path $probeUser 'Saved/LeftoverReview'))
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
