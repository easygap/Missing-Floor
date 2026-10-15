[CmdletBinding()]
param(
	[ValidateSet(30, 60, 120)][int]$FrameRate = 60,
	[ValidateRange(30, 300)][int]$TimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-AudioPresentationProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$runLog = Join-Path $projectRoot "Saved/Logs/AudioPresentation-$FrameRate.log"
$recording = Join-Path $projectRoot 'Saved/AudioPresentation/presentation-mix.wav'
foreach ($path in @($runLog, $recording)) {
	if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
}
$arguments = @(
	('"{0}"' -f (Join-Path $projectRoot 'IndieGame.uproject')),
	'-game', '-unattended', '-nosplash', '-NoLoadingScreen',
	'-RenderOffscreen', '-d3d12', '-AudioMixer', '-AllowCommandletAudio',
	'-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0',
	'-Windowed', '-ResX=640', '-ResY=360', '-ForceRes',
	'-IGAudioPresentationProbe', '-IGSkipFrontend',
	('"-ExecCmds=t.MaxFPS {0}"' -f $FrameRate), ('"-abslog={0}"' -f $runLog)
)
# 화면 밖 실행도 소리가 나도록 이 프로세스의 비활성 음량을 열어 둔다.
# -nosound나 고정 시간 가속으로는 실제 페이드를 검사할 수 없다.
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
# Windows PowerShell 5.1은 핸들을 먼저 잡아 두지 않으면 ExitCode를 비워 둔다.
$null = $process.Handle
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
	& taskkill.exe /PID $process.Id /T /F | Out-Null
	throw "소리 연출 검사 시간 초과: $runLog"
}
$receipts = Select-String -LiteralPath $runLog -Pattern 'AUDIO_PRESENTATION_'
$receipts | ForEach-Object { Write-Host $_.Line }
if ($process.ExitCode -ne 0 -or -not ($receipts | Where-Object { $_.Line -match 'AUDIO_PRESENTATION_PROBE PASS failures=0' })) {
	throw "소리 연출 검사 실패: $runLog"
}
if (-not (Test-Path -LiteralPath $recording)) { throw "믹서 녹음이 없습니다: $recording" }
if (Select-String -LiteralPath $runLog -Pattern "Sound class 'BUS_.*does not exist|Unable to find sound class properties for sound class BUS_") {
	throw "게임 믹서에 등록되지 않은 소리 버스가 있습니다: $runLog"
}
& python (Join-Path $PSScriptRoot 'check_audio_presentation.py') $recording
if ($LASTEXITCODE -ne 0) { throw "믹서 녹음 파형 검사 실패: $recording" }
Write-Host "실제 믹서 녹음: $recording"

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
