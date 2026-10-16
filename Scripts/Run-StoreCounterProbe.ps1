[CmdletBinding()]
param(
	# stay: 끝까지 지켜보고 영수증을 읽은 뒤 나갔다 들어온다. leave: 나린이 단말기를 누르기 시작하면 가게를
	# 나간다. spam: 나린이 일하는 동안 벨을 거듭 누른다.
	[ValidateSet('stay', 'leave', 'spam')][string]$Variant = 'stay',
	# 마스터 믹스를 녹음하고(-DeterministicAudio라 스피커로는 나가지 않는다) 몇 장면을 찍는다.
	[switch]$Record,
	[ValidateSet(30, 60)][int]$FrameRate = 30,
	[ValidateRange(60, 600)][int]$TimeoutSeconds = 360
)

# 편의점 계산대 장면을 실제 키·마우스 입력으로 겪는다. 준비(입주 저녁, 나린과 인사, 택배 안내문,
# 골목에 서기)만 상태로 하고 나머지는 걷고 겨누고 누른다. 화면은 오프스크린으로 그린다(대사가 넘어가야
# 하므로). 결과는 STORE_CHECK·STORE_AV 줄과, -Record면 mix.wav·장면 사진이다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-StoreCounterProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$tag = $Variant + $(if ($Record) { '-av' } else { '' })
$logPath = Join-Path $projectRoot "Saved/Logs/StoreCounter-$tag.log"
$probeRoot = Join-Path $projectRoot ('Saved/Validation/StoreCounter-' + $tag + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$probeUser = Join-Path $probeRoot 'User'
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory $probeRoot -Variant ('StoreCounter-' + $tag)
# 숨긴 창은 초점이 없어 엔진이 소리를 줄인다(UnfocusedVolumeMultiplier). 녹음할 때는 열어 둔다.
$audio = if ($Record) { @('-DeterministicAudio', '-AudioMixer', '-IGStoreCounterRecord', '-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0') } else { @('-nosound') }
$arguments = @(
	('"{0}"' -f $projectPath), '-game', '-unattended', '-nosplash', '-NoLoadingScreen', '-RenderOffscreen',
	'-d3d12', '-Windowed', '-ResX=1280', '-ResY=720', '-ForceRes',
	# 계산대 벨은 실제 게임 무대(입주 저녁의 조사 물건)에만 선다.
	'-IGListenerGreybox', '-IGMissingFloor', $(if ($Variant -eq 'stay') { '-IGStoreCounterProbe' } else { "-IGStoreCounterProbe=$Variant" }),
	'-IGSkipFrontend', '-UseFixedTimeStep', "-FPS=$FrameRate",
	('"-UserDir={0}"' -f $probeUser), ('"-abslog={0}"' -f $logPath)
) + $audio
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
# Windows PowerShell 5.1은 핸들을 먼저 잡아 두지 않으면 ExitCode를 비워 둔다.
$null = $process.Handle
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
	& taskkill.exe /PID $process.Id /T /F | Out-Null
	throw "계산대 검사 시간 초과: $logPath"
}
Select-String -LiteralPath $logPath -Pattern 'STORE_(CHECK|PROBE)|INPUT_(STUCK|FOCUS|BLOCK)' | ForEach-Object { Write-Host $_.Line }
$output = Join-Path $probeUser ('Saved/StoreCounterAV/' + $Variant)
if ($Record) {
	$mix = Join-Path $output 'mix.wav'
	if (-not (Test-Path -LiteralPath $mix)) { throw "믹서 녹음이 없습니다: $mix" }
	& python (Join-Path $PSScriptRoot 'check_store_counter_audio.py') $logPath $mix $output
	if ($LASTEXITCODE -ne 0) { throw "계산대 소리 검사 실패: $mix" }
	Write-Host "STORE_AV_OUTPUT $output"
}
if ($process.ExitCode -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'STORE_PROBE PASS')) {
	throw "계산대 검사에 실패했습니다: $logPath"
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
