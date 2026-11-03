[CmdletBinding()]
param(
	# 밤 난이도. listen은 설정의 「듣기만 하는 밤」이다(잡히지 않고 이야기와 결말은 같다). 길과 진행이 막히는지
	# 먼저 볼 때 쓴다. standard는 새 설치 그대로다.
	[ValidateSet('listen', 'standard')][string]$Difficulty = 'listen',
	[ValidateSet('ko', 'en', 'ja', 'zh-Hans')][string]$Culture = 'ko',
	# 게임 안에서 흐르는 시간의 상한(분). 넘으면 그 자리에서 멈추고 어디까지 갔는지 적는다.
	[ValidateRange(5, 600)][int]$GameMinutes = 40,
	[ValidateRange(120, 36000)][int]$TimeoutSeconds = 3600,
	# 앞선 실행 폴더(Saved/Validation/FullPlaythrough-…)를 주면 그 자동 저장에서 이어 한다(메뉴의 이어 하기).
	# 뒤쪽 낮과 밤을 고칠 때 처음부터 다시 걷지 않으려고 쓴다. 다 고친 뒤에는 새 게임으로 한 번 끝까지 돌린다.
	[string]$ResumeFrom = ''
)

# 새 게임을 처음부터 실제 키·마우스 입력으로만 해 본다(-IGFullPlaythrough). 순간 이동이나 상호작용 직접
# 호출, 목표 채우기 없이 게임이 띄우는 목표를 차례로 한다. 화면은 오프스크린으로 그린다(대사가 넘어가야
# 하므로). 결과는 PLAYTHROUGH 줄과 단계마다 찍은 화면(User/Saved/FullPlaythrough)이다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-FullPlaythrough.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$tag = $Difficulty + '-' + $Culture
$logPath = Join-Path $projectRoot "Saved/Logs/FullPlaythrough-$tag.log"
$probeRoot = Join-Path $projectRoot ('Saved/Validation/FullPlaythrough-' + $tag + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$probeUser = Join-Path $probeRoot 'User'
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory $probeRoot -Variant ('FullPlaythrough-' + $tag)
$resumeArguments = @()
if ($ResumeFrom) {
	$sourceSaves = Join-Path $ResumeFrom 'User/Saved/SaveGames'
	if (-not (Test-Path -LiteralPath $sourceSaves -PathType Container)) { throw "이어 할 자동 저장이 없습니다: $sourceSaves" }
	$targetSaves = Join-Path $probeUser 'Saved/SaveGames'
	New-Item -ItemType Directory -Force -Path $targetSaves | Out-Null
	foreach ($save in Get-ChildItem -LiteralPath $sourceSaves -Filter '*.sav' -File) {
		Copy-Item -LiteralPath $save.FullName -Destination (Join-Path $targetSaves $save.Name)
	}
	$resumeArguments = @('-IGFullPlaythroughResume')
}
$difficultyArgument = if ($Difficulty -eq 'listen') { '-IGNightDifficulty=3' } else { '-IGNightDifficulty=1' }
$arguments = @(
	('"{0}"' -f $projectPath), '-game', '-unattended', '-nosplash', '-NoLoadingScreen', '-RenderOffscreen',
	'-d3d12', '-Windowed', '-ResX=1280', '-ResY=720', '-ForceRes', '-nosound',
	'-IGListenerGreybox', '-IGMissingFloor', '-IGFullPlaythrough', "-IGFullPlaythroughMinutes=$GameMinutes",
	'-IGSkipFrontend', $difficultyArgument, "-IGCulture=$Culture", "-Culture=$Culture",
	'-UseFixedTimeStep', '-FPS=30',
	('"-UserDir={0}"' -f $probeUser), ('"-abslog={0}"' -f $logPath)
) + $resumeArguments
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
# Windows PowerShell 5.1은 핸들을 먼저 잡아 두지 않으면 ExitCode를 비워 둔다.
$null = $process.Handle
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
	& taskkill.exe /PID $process.Id /T /F | Out-Null
	throw "완주 검사 시간 초과: $logPath"
}
Select-String -LiteralPath $logPath -Pattern 'PLAYTHROUGH_(STAGE|CHECK|RETRY|BLOCKED|PATH .* none|STAND .* none|NOTE)|PLAYTHROUGH (PASS|FAIL)|INPUT_(STUCK|FOCUS|BLOCK)' |
	ForEach-Object { Write-Host ($_.Line -replace '^\[[^\]]*\]\[[^\]]*\]LogIndieGame: Display: ', '') }
Write-Host "PLAYTHROUGH_SHOTS $(Join-Path $probeUser 'Saved/FullPlaythrough')"
if ($process.ExitCode -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'PLAYTHROUGH PASS')) {
	throw "완주 검사가 끝까지 가지 못했습니다: $logPath"
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
