[CmdletBinding()]
param(
	[int]$Port = 8199,
	# 서버를 내린다.
	[switch]$Stop
)

# 네이티브 이미지→3D 노드(Pixal3D·TRELLIS.2)를 돌릴 ComfyUI를 띄운다.
#
# ComfyUI는 이 저장소에 없다. 옆 프로젝트 lunia_z가 2026-09-08에 받아 검증한
# 설치(.venv, 모델 15GB)를 그대로 빌려 쓴다. 다시 받을 이유가 없고, 모델
# 해시 검증도 거기 receipt에 있다. IG_COMFYUI로 다른 설치를 가리킬 수 있다.
#
# 포트와 입출력 폴더는 우리 것으로 분리한다. lunia_z 쪽 Start-ComfyNative.ps1은
# 8198에 기본 폴더로 뜨므로, 같은 포트를 잡으면 그쪽 영수증이 우리 산출물을
# 본다. 입출력은 한글 경로를 피해 %LOCALAPPDATA%\IndieGame\Comfy 아래에 둔다.
#
# 이 GPU는 8GB 하나뿐이다. 다른 작업이 VRAM을 쓰고 있으면 생성이 OOM으로
# 죽으므로, 띄우기 전에 nvidia-smi로 남은 메모리를 본다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$comfyCandidates = @()
if ($env:IG_COMFYUI) { $comfyCandidates += $env:IG_COMFYUI }
$comfyCandidates += 'C:\y2026\99_개인폴더\99_project\lunia_z\Reference\Tools\ComfyUI-0.34.6'
$comfy = $comfyCandidates | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'main.py') } | Select-Object -First 1
if (-not $comfy) {
	throw 'ComfyUI를 찾지 못했다. IG_COMFYUI에 main.py가 있는 폴더를 넣어라.'
}
$python = Join-Path $comfy '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) {
	throw "ComfyUI 가상환경이 없다: $python"
}

$workRoot = Join-Path $env:LOCALAPPDATA 'IndieGame\Comfy'
$inputDir = Join-Path $workRoot 'input'
$outputDir = Join-Path $workRoot 'output'
$logDir = Join-Path $workRoot 'logs'
foreach ($dir in @($inputDir, $outputDir, $logDir)) {
	New-Item -ItemType Directory -Force -Path $dir | Out-Null
}
$receiptPath = Join-Path $workRoot 'server-receipt.json'
$url = "http://127.0.0.1:$Port"

if ($Stop) {
	if (Test-Path -LiteralPath $receiptPath) {
		$receipt = Get-Content -Raw -LiteralPath $receiptPath | ConvertFrom-Json
		$process = Get-Process -Id $receipt.pid -ErrorAction SilentlyContinue
		if ($process) {
			Stop-Process -Id $receipt.pid -Force
			Write-Host "COMFY stopped pid $($receipt.pid)"
		}
		Remove-Item -LiteralPath $receiptPath -Force
	}
	return
}

try { $running = Invoke-RestMethod -Uri "$url/system_stats" -TimeoutSec 3 } catch { $running = $null }
if ($running) {
	Write-Host "COMFY already running: $url (v$($running.system.comfyui_version))"
	Write-Output $url
	return
}
if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
	throw "포트 $Port 를 다른 프로세스가 쓰고 있다."
}

$freeMiB = 0
try {
	$freeMiB = [int](& nvidia-smi --query-gpu=memory.free --format=csv,noheader,nounits 2>$null | Select-Object -First 1)
} catch { }
if ($freeMiB -gt 0 -and $freeMiB -lt 5500) {
	throw "GPU 여유 메모리가 ${freeMiB}MiB 뿐이다. 다른 작업(nvidia-smi로 확인)이 끝난 뒤 다시 띄워라."
}

$env:HF_HUB_OFFLINE = '1'
$env:TRANSFORMERS_OFFLINE = '1'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$arguments = @(
	'-u', 'main.py',
	'--listen', '127.0.0.1', '--port', "$Port",
	'--input-directory', $inputDir,
	'--output-directory', $outputDir,
	'--disable-auto-launch', '--disable-all-custom-nodes', '--disable-api-nodes',
	'--reserve-vram', '1.0', '--preview-method', 'none'
)
$process = Start-Process -WindowStyle Hidden -FilePath $python -WorkingDirectory $comfy -ArgumentList $arguments `
	-RedirectStandardOutput (Join-Path $logDir "server_$stamp.stdout.log") `
	-RedirectStandardError (Join-Path $logDir "server_$stamp.stderr.log") -PassThru
[ordered]@{
	pid = $process.Id; url = $url; comfy = $comfy; input = $inputDir; output = $outputDir
	startedAt = [DateTimeOffset]::Now.ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath $receiptPath -Encoding UTF8

$deadline = (Get-Date).AddSeconds(180)
$stats = $null
while ((Get-Date) -lt $deadline) {
	Start-Sleep -Seconds 3
	if ($process.HasExited) {
		throw "ComfyUI가 바로 죽었다. 로그: $logDir\server_$stamp.stderr.log"
	}
	try { $stats = Invoke-RestMethod -Uri "$url/system_stats" -TimeoutSec 3; break } catch { }
}
if (-not $stats) {
	throw "ComfyUI가 180초 안에 응답하지 않았다. 로그: $logDir\server_$stamp.stderr.log"
}
Write-Host "COMFY started pid $($process.Id): $url (v$($stats.system.comfyui_version), vram free $($stats.devices[0].vram_free))"
Write-Output $url
