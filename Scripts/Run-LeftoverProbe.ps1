[CmdletBinding()]
param(
	[ValidateSet(30, 60)][int]$FrameRate = 60,
	# dawn: 밤3에 정우를 꺼내지 않고 새벽을 맞는 길. dawnbrace: 꺼낸 정우가 판을 받치는 한가운데 새벽(낮3 문 앞 상자).
	# night2: 밤2 안쪽 방만. wall: 밤4 벽 단계마다 저장 상태로 되살린 모습(불러오기 길).
	# input: 밤3 302호를 순간 이동 없이 실제 키·마우스 입력으로 지난다(구간별 시간을 남긴다).
	# lines: 이 줄기의 새 문장을 한 줄씩 띄워 찍는다(-Shots, -Culture, -TextScale과 같이).
	# night3av: input에 이어 계단·옥상·5층(밸브, 벽 듣기, 수첩, 박자 두드리기, 벽의 대답)과 복도의 그를 지나
	# 403호까지 실제 입력으로 간다. -Record와 같이 쓰면 믹스를 녹음한다.
	# hints: 밤3 302호 구출의 H 힌트 세 단계를 상태마다 남긴다(-HintScenario). save는 받친 판과 밀어 둔 장롱을
	# 자동 저장에 남기고, 같은 사용자 폴더로 다시 켜서 이어 한 밤을 본다. fallensave는 누운 판을,
	# fallenbracedsave는 누운 판 밑동에 쐐기만 끼우고 세우지 않은 채로 저장한다.
	[ValidateSet('full', 'dawn', 'dawnbrace', 'night2', 'wall', 'input', 'lines', 'night3av', 'hints')][string]$Variant = 'full',
	[ValidateSet('named', 'unnamed', 'talk', 'fallen', 'remote', 'save', 'fallensave', 'fallenbracedsave')][string]$HintScenario = 'named',
	# 스피커로 내지 않고 마스터 믹스를 녹음한다(-DeterministicAudio). 오프스크린 D3D로 그린다.
	[switch]$Record,
	[switch]$Shots,
	# en·ja·zh-Hans·zh-Hant로 찍으면 이 줄기의 글(안내문·대장·힌트·자막)을 그 언어로 본다.
	[string]$Culture,
	# 2면 자막·안내 글자를 가장 크게 키운 설정으로 본다.
	[ValidateSet(1, 2)][int]$TextScale = 1,
	# -Shots와 함께: 1920x1080으로 HUD 없이 찍는다(소개 문서 화면).
	[switch]$Clean,
	# -Shots와 함께: 덧댄 것이 움직이는 구간을 30fps로 이어 찍는다(Saved/LeftoverFrames, 검수용 영상 재료).
	[switch]$Frames,
	# -Shots와 함께: 덧댄 것의 몸판·다리·패드 충돌 모양을 선으로 그려 보이는 몸과 맞춰 본다.
	[switch]$Collision,
	# 입주 저녁에 골목에서 정우의 오토바이와 스친 회차로 본다. 첫째 낮 302호 문 앞 첫마디가 사과여야 한다.
	[switch]$NearMiss
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
$tag = $Variant + $(if ($Variant -eq 'hints') { "-$HintScenario" } else { '' }) + $(if ($Culture) { "-$Culture" } else { '' }) + $(if ($TextScale -gt 1) { "-x$TextScale" } else { '' }) + $(if ($Clean) { '-clean' } else { '' }) + $(if ($Collision) { '-collision' } else { '' }) + $(if ($NearMiss) { '-nearmiss' } else { '' })
$probeUser = Join-Path $projectRoot ('Saved/Validation/Leftover-' + $tag + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
# 로그는 실행 폴더 안에 둔다. 예전에는 Saved/Logs의 같은 이름을 썼는데, 두 작업이 같은 변형을 동시에
# 돌리면 엔진이 한쪽 이름을 _2로 바꿔서 이 스크립트가 남의 실행 로그를 읽고 판정했다(10-19).
$logPath = Join-Path (Split-Path -Parent $probeUser) "Leftover-$tag-$FrameRate.log"
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
$extra = @()
if ($Culture) { $extra += ('-IGCulture=' + $Culture) }
if ($TextScale -gt 1) { $extra += ('-IGCaptionScale=' + $TextScale) }
if ($Shots -and $Frames) { $extra += '-IGLeftoverFrames' }
if ($Shots -and $Collision) { $extra += '-IGLiningDrawCollision' }
if ($Variant -eq 'hints') { $extra += ('-IGHintScenario=' + $HintScenario) }
if ($NearMiss) { $extra += '-IGLeftoverNearMiss' }
$audio = @('-nosound')
if ($Record) {
	# 숨긴 창은 초점이 없어 엔진이 소리를 줄인다. 녹음할 때는 열어 둔다.
	$audio = @('-DeterministicAudio', '-AudioMixer', '-IGLeftoverRecord', '-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0')
}
# 기본도 오프스크린으로 그린다. 처음에는 else에서 @('-nullrhi')를 돌려줬는데, if 식이 원소 하나짜리 배열을 문자열로 풀어
# @render가 글자를 하나씩 넘겼고(- n u l l r h i) 엔진은 그걸 모르는 인자로 버렸다. 이 검사의 기준은 모두 그려 가며 돈
# 실행에서 잡혔다.
$render = @(if ($Record) { '-d3d12', '-Windowed', '-ResX=1280', '-ResY=720', '-ForceRes' }
	elseif ($Shots -and $Clean) { '-d3d12', '-Windowed', '-ResX=1920', '-ResY=1080', '-ForceRes', '-IGLeftoverShots', '-IGLeftoverCleanShots' }
	elseif ($Shots) { '-d3d12', '-Windowed', '-ResX=1280', '-ResY=720', '-ForceRes', '-IGLeftoverShots' })
$probeArg = if ($Variant -eq 'full') { '-IGLeftoverProbe' } else { "-IGLeftoverProbe=$Variant" }
& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen @audio @render `
	-IGListenerGreybox $probeArg -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
	"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
$exitCode = $LASTEXITCODE
$probePattern = 'LEFTOVER_(CHECK|PROBE|N2|N3|DAY1|TAKEN|LINING|HINT_DUMP|RESUME)|N3AV t=[0-9.]+ (setup|record)|MANAGER_(ERRAND|CHASE|CATCH|INVESTIGATE)|LINING_(TARGET|SEALED|GRIP|PLACED)|INPUT_(SEGMENT|ROUTE|MOUSE|STUCK|FOCUS|BLOCK|LINING_LEG)|LEFTOVER_LEG'
Select-String -LiteralPath $logPath -Pattern $probePattern | ForEach-Object { Write-Host $_.Line }
if ($exitCode -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'LEFTOVER_PROBE PASS failures=0')) {
	throw "남은 짐 검사에 실패했습니다: $logPath"
}
if ($Variant -eq 'hints' -and $HintScenario -in @('save', 'fallensave', 'fallenbracedsave')) {
	# 같은 사용자 폴더로 다시 켜면 방금 남긴 밤 저장을 불러와 이어 한다.
	$resumeLog = Join-Path (Split-Path -Parent $probeUser) "Leftover-$tag-resume-$FrameRate.log"
	& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen @audio @render `
		-IGListenerGreybox $probeArg -IGSkipFrontend -IGHintResume -UseFixedTimeStep "-FPS=$FrameRate" `
		"-UserDir=$probeUser" "-abslog=$resumeLog" @extra | Out-Null
	$exitCode = $LASTEXITCODE
	Select-String -LiteralPath $resumeLog -Pattern $probePattern | ForEach-Object { Write-Host $_.Line }
	if ($exitCode -ne 0 -or -not (Select-String -LiteralPath $resumeLog -Pattern 'LEFTOVER_PROBE PASS failures=0')) {
		throw "저장을 불러온 밤의 남은 짐 검사에 실패했습니다: $resumeLog"
	}
	Write-Host "LEFTOVER_LOG $resumeLog"
}
Write-Host "LEFTOVER_LOG $logPath"
if ($Shots) {
	Write-Host ("LEFTOVER_SHOTS " + (Join-Path $probeUser 'Saved/LeftoverReview'))
}
if ($Record) {
	$mix = Join-Path $probeUser 'Saved/Night3AV/mix.wav'
	if (-not (Test-Path -LiteralPath $mix)) { throw "믹서 녹음이 없습니다: $mix" }
	Write-Host "N3AV_MIX $mix"
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
