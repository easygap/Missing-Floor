# -IGNightCapture 연속 프레임을 GIF로 묶는다.
# 어두운 복도에 색 단계가 생기지 않도록 ffmpeg 팔레트를 두 번에 나눠 처리한다.
#   Saved/NightCapture/mercy-note   -> Docs/Media/m65-mercy-note-slide.gif
# README의 추격 GIF는 create_readme_media.py가 새 촬영으로 만든다. 여기서
# 다시 묶으면 예전 촬영으로 덮어쓰게 되므로 이 스크립트에서는 뺐다.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$Only = $args

function Convert-Burst {
    param(
        [string]$BurstName,
	[string]$OutputName,
	[int]$Fps,
	[int]$MinimumFrames = 8
    )
	$framesDir = Join-Path $projectRoot "Saved\NightCapture\$BurstName"
	$frames = @(Get-ChildItem -LiteralPath $framesDir -Filter 'frame_*.png' -ErrorAction SilentlyContinue |
		Sort-Object Name)
	$frameCount = $frames.Count
	if ($frameCount -lt $MinimumFrames) {
		throw "Too few frames in $framesDir ($frameCount); run Run-MissingFloor-NightCapture.bat first."
	}
	# 1080p 캡처가 밀리면 프레임 하나가 비어 있을 수 있다. 남은 프레임을
	# 연속 번호로 다시 묶어 ffmpeg이 중간에서 멈추지 않게 한다.
	$sequenceDirectory = Join-Path $framesDir '_sequence'
	$sequenceDirectory = [IO.Path]::GetFullPath($sequenceDirectory)
	$captureRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Saved\NightCapture')) + '\'
	if (-not $sequenceDirectory.StartsWith($captureRoot, [StringComparison]::OrdinalIgnoreCase)) {
		throw "캡처 폴더 바깥 경로는 정리할 수 없습니다: $sequenceDirectory"
	}
	if (Test-Path -LiteralPath $sequenceDirectory) {
		Remove-Item -LiteralPath $sequenceDirectory -Recurse -Force
	}
	New-Item -ItemType Directory -Path $sequenceDirectory | Out-Null
	for ($frameIndex = 0; $frameIndex -lt $frames.Count; $frameIndex++) {
		Copy-Item -LiteralPath $frames[$frameIndex].FullName -Destination (
			Join-Path $sequenceDirectory ('frame_{0:D5}.png' -f $frameIndex))
	}
	$output = Join-Path $projectRoot "Docs\Media\$OutputName"
	ffmpeg -y -framerate $Fps -i (Join-Path $sequenceDirectory 'frame_%05d.png') `
		-vf "fps=$Fps,scale=960:-1:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4" `
		-loop 0 $output
	Remove-Item -LiteralPath $sequenceDirectory -Recurse -Force
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed for $BurstName with exit code $LASTEXITCODE" }
    $size = (Get-Item -LiteralPath $output).Length
    if ($size -gt 10MB) {
        throw "$OutputName is $([math]::Round($size / 1MB, 1)) MB; over the 10 MB review budget."
    }
    Write-Output ("{0}: {1} frames -> {2:N1} MB" -f $OutputName, $frameCount, ($size / 1MB))
}

if ($Only.Count -eq 0 -or $Only -contains 'mercy-note') {
	Convert-Burst -BurstName 'mercy-note' -OutputName 'm65-mercy-note-slide.gif' -Fps 7 -MinimumFrames 12
}
