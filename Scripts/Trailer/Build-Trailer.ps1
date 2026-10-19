[CmdletBinding()]
param(
	[int]$TimeoutSeconds = 2400
)
# 예고편을 새 실행 폴더(Saved/Trailer/Runs/<시각>)에서 처음부터 만든다.
#   촬영 → 글자 판 → 편집표 → 소리 → 한국어·영어 영상 → 배포용·README용 인코딩 → 영상 확인
# 실행 폴더는 자동 정리에 등록한다. 지울 수 있는 것은 장면 프레임, 중간 소리, 기록뿐이고
# 영상·정지 화면·글자 판·편집표·음량 측정은 남는다(GeneratedArtifactCleanup.ps1의 Trailer 규칙).
# 두 언어 영상을 ffprobe로 확인한 뒤에야 성공으로 닫는다. 도중에 실패해도 finally에서 실패로
# 닫으므로, 하루 안의 실행과 마지막 실패 한 번은 남는다. 예전 Saved/Trailer의 프레임과 영상은
# 이 스크립트가 손대지 않는다.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $root 'Scripts\GeneratedArtifactCleanup.ps1')
$scope = Enter-IGArtifactScope -ProjectRoot $root -Name 'Build-Trailer.ps1' -Parameters $PSBoundParameters
$succeeded = $false
$previousTrailerDir = $env:IG_TRAILER_DIR
try {
	$run = Join-Path $root ('Saved\Trailer\Runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
	Add-IGArtifactDirectory -Scope $scope -Directory $run -Variant 'Trailer'
	if (-not (Test-Path -LiteralPath (Join-Path $run '.ig-artifact-owner'))) {
		throw "새 실행 폴더를 정리 대상으로 등록하지 못했습니다: $run"
	}
	Write-Host "TRAILER_RUN $run"

	& (Join-Path $PSScriptRoot 'Run-TrailerCapture.ps1') -OutputDirectory $run -TimeoutSeconds $TimeoutSeconds

	$env:IG_TRAILER_DIR = $run
	$steps = @(
		@('cards.py'),
		@('assemble.py', '--marks'),
		@('compose.py'),
		@('assemble.py', 'ko'),
		@('assemble.py', 'en')
	)
	foreach ($step in $steps) {
		& python (Join-Path $PSScriptRoot $step[0]) @($step | Select-Object -Skip 1)
		if ($LASTEXITCODE -ne 0) { throw ('예고편 단계가 실패했습니다: ' + ($step -join ' ')) }
	}

	# 원본은 화면 잡티 때문에 크다. 배포용(-hq)과 README용(-web, 10MB 아래)을 따로 만든다(Docs/TRAILER.md).
	foreach ($name in @('MissingFloor-Trailer', 'MissingFloor-Trailer-en')) {
		$source = Join-Path $run "$name.mp4"
		& ffmpeg -y -loglevel error -i $source -c:v libx264 -preset slow -crf 22 -tune grain -c:a aac -b:a 160k `
			-movflags +faststart (Join-Path $run "$name-hq.mp4")
		if ($LASTEXITCODE -ne 0) { throw "배포용 인코딩 실패: $name" }
		$passLog = Join-Path $run "$name-web-pass"
		& ffmpeg -y -loglevel error -i $source -vf 'scale=1280:720:flags=lanczos,hqdn3d=2:1.5:3:3' -c:v libx264 `
			-preset slow -b:v 1150k -pass 1 -passlogfile $passLog -an -f mp4 NUL
		if ($LASTEXITCODE -ne 0) { throw "README용 인코딩 1패스 실패: $name" }
		& ffmpeg -y -loglevel error -i $source -vf 'scale=1280:720:flags=lanczos,hqdn3d=2:1.5:3:3' -c:v libx264 `
			-preset slow -b:v 1150k -pass 2 -passlogfile $passLog -c:a aac -b:a 96k -movflags +faststart `
			(Join-Path $run "$name-web.mp4")
		if ($LASTEXITCODE -ne 0) { throw "README용 인코딩 2패스 실패: $name" }
		Get-ChildItem -LiteralPath $run -Filter "$name-web-pass*" -File | Remove-Item
	}

	# 영상이 실제로 끝까지 나왔는지 본다. 길이, 화면 크기, 초당 프레임, 소리 줄.
	$expected = @(
		@{ Name = 'MissingFloor-Trailer.mp4'; Width = 1920; Height = 1080 },
		@{ Name = 'MissingFloor-Trailer-en.mp4'; Width = 1920; Height = 1080 },
		@{ Name = 'MissingFloor-Trailer-hq.mp4'; Width = 1920; Height = 1080 },
		@{ Name = 'MissingFloor-Trailer-en-hq.mp4'; Width = 1920; Height = 1080 },
		@{ Name = 'MissingFloor-Trailer-web.mp4'; Width = 1280; Height = 720; MaxBytes = 10MB },
		@{ Name = 'MissingFloor-Trailer-en-web.mp4'; Width = 1280; Height = 720; MaxBytes = 10MB }
	)
	foreach ($video in $expected) {
		$path = Join-Path $run $video.Name
		if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "영상이 없습니다: $path" }
		$probe = & ffprobe -v error -show_entries 'format=duration:stream=codec_type,width,height,r_frame_rate' -of json $path |
			Out-String | ConvertFrom-Json
		if ($LASTEXITCODE -ne 0) { throw "영상을 읽지 못했습니다: $path" }
		$duration = [double]$probe.format.duration
		$picture = @($probe.streams | Where-Object { $_.codec_type -eq 'video' })[0]
		$sound = @($probe.streams | Where-Object { $_.codec_type -eq 'audio' })
		if ($duration -lt 61.8 -or $duration -gt 62.3) { throw ('영상 길이가 다릅니다: {0} {1:N2}s' -f $video.Name, $duration) }
		if ($picture.width -ne $video.Width -or $picture.height -ne $video.Height) { throw "화면 크기가 다릅니다: $($video.Name)" }
		if ($picture.r_frame_rate -ne '30/1') { throw "초당 프레임이 다릅니다: $($video.Name) $($picture.r_frame_rate)" }
		if (-not $sound.Count) { throw "소리가 없습니다: $($video.Name)" }
		if ($video.ContainsKey('MaxBytes') -and (Get-Item -LiteralPath $path).Length -ge $video.MaxBytes) {
			throw "README용 영상이 10MB를 넘습니다: $($video.Name)"
		}
		Write-Host ('TRAILER_VIDEO {0} {1:N2}s {2}x{3} {4:N0} bytes' -f $video.Name, $duration, $picture.width, $picture.height,
			(Get-Item -LiteralPath $path).Length)
	}
	Write-Host "TRAILER_BUILD PASS $run"
	$succeeded = $true
}
finally {
	$env:IG_TRAILER_DIR = $previousTrailerDir
	Complete-IGArtifactScope -Scope $scope -Succeeded $succeeded
}
