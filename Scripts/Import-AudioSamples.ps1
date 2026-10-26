[CmdletBinding()]
param(
	# 이름을 주면 그 샘플만 다시 반입한다(예: -Only Entity_Alert,Entity_CrawlStep_2).
	# 나머지 uasset은 손대지 않으므로 바뀌지 않은 에셋이 LFS 변경으로 올라오지 않는다.
	[string[]]$Only = @()
)

# Content/SourceArt/Audio(CC0 녹음을 curate_cc0_audio.py로 다듬은 WAV)를
# /Game/Audio/S_<이름> USoundWave로 만들어 저장소 Content/Audio에 넣는다.
# Import-BlenderAssets.ps1과 같은 콘텐츠 전용 프로젝트(ArtImport)를 쓴다 —
# 게임 모듈을 다시 빌드할 필요가 없고 한글 경로도 피한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $projectRoot 'Content\SourceArt\Audio'
$importRoot = Join-Path $env:LOCALAPPDATA 'IndieGame\ArtImport'
$uproject = Join-Path $importRoot 'ArtImport.uproject'

if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot 'manifest.json') -PathType Leaf)) {
	throw "오디오 산출물이 없다: $sourceRoot (먼저 python Scripts\curate_cc0_audio.py)"
}
if (-not (Test-Path -LiteralPath $uproject -PathType Leaf)) {
	throw "콘텐츠 전용 프로젝트가 없다: $uproject (먼저 Import-BlenderAssets.ps1을 한 번 돌려라)"
}

function Invoke-Mirror {
	param([string]$Source, [string]$Destination, [switch]$Mirror)
	New-Item -ItemType Directory -Force -Path $Destination | Out-Null
	$arguments = @($Source, $Destination)
	$arguments += if ($Mirror) { '/MIR' } else { '/E' }
	$arguments += @('/COPY:DAT', '/R:2', '/W:1', '/XJ', '/NFL', '/NDL', '/NP', '/NJH', '/NJS')
	& robocopy.exe @arguments | Out-Null
	if ($LASTEXITCODE -ge 8) {
		throw "robocopy failed ($LASTEXITCODE): $Source -> $Destination"
	}
}

Invoke-Mirror -Source $sourceRoot -Destination (Join-Path $importRoot 'ImportAudio') -Mirror
$existing = Join-Path $projectRoot 'Content\Audio'
if (Test-Path -LiteralPath $existing -PathType Container) {
	Invoke-Mirror -Source $existing -Destination (Join-Path $importRoot 'Content\Audio') -Mirror
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'import_audio_samples.py') -Destination (Join-Path $importRoot 'Scripts') -Force

$editor = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $uproject -Commandlet
if (-not $editor -or -not (Test-Path -LiteralPath $editor)) {
	throw "UnrealEditor-Cmd.exe를 찾지 못했다: $editor"
}

$logPath = Join-Path $importRoot ('Saved\Logs\AudioImport_{0}.log' -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
$env:IG_AUDIO_SOURCE = Join-Path $importRoot 'ImportAudio'
$env:IG_AUDIO_ONLY = ($Only | Where-Object { $_ }) -join ','
try {
	& $editor $uproject -unattended -nop4 -nosplash -nullrhi -nosound -RenderOffscreen -stdout -FullStdOutLogOutput `
		"-abslog=$logPath" "-ExecutePythonScript=$(Join-Path $importRoot 'Scripts\import_audio_samples.py')" | Out-Null
	$editorExit = $LASTEXITCODE
}
finally {
	Remove-Item Env:IG_AUDIO_SOURCE -ErrorAction SilentlyContinue
	Remove-Item Env:IG_AUDIO_ONLY -ErrorAction SilentlyContinue
}
$pass = Select-String -LiteralPath $logPath -Pattern 'AUDIO_IMPORT PASS sounds=\d+' | Select-Object -Last 1
if ($editorExit -ne 0 -or -not $pass) {
	Select-String -LiteralPath $logPath -Pattern 'LogPython: Error|Traceback|AUDIO_IMPORT' | Select-Object -Last 30 | ForEach-Object { Write-Host $_.Line }
	throw "오디오 반입 실패 (exit $editorExit). 로그: $logPath"
}

Invoke-Mirror -Source (Join-Path $importRoot 'Content\Audio') -Destination $existing
Write-Host $pass.Line.Substring($pass.Line.IndexOf('AUDIO_IMPORT'))
