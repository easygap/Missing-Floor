[CmdletBinding()]
param(
	# 비우면 표의 모든 장면을 찍는다. 쉼표로 여럿을 줄 수 있다(alley,booth-cctv).
	[string]$Shot = '',
	[int]$Width = 1920,
	[int]$Height = 1080,
	[int]$TimeoutSeconds = 2400,
	# 비우면 예전처럼 Saved/Trailer에 찍는다. Build-Trailer.ps1은 실행마다 새 폴더를 준다.
	[string]$OutputDirectory = ''
)
# 트레일러 재료를 30fps 고정 간격으로 찍는다. 프레임은 Saved/Trailer/<장면>/에 쌓인다.
# 다음 순서로 영상을 만든다.
#   1. 이 스크립트
#   2. python Scripts/Trailer/cards.py (소셜 미리보기는 --social <경로>)
#   3. python Scripts/Trailer/assemble.py --marks
#   4. python Scripts/Trailer/compose.py
#   5. python Scripts/Trailer/assemble.py ko, 그다음 en
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$editor = & (Join-Path $root 'Scripts\Resolve-UnrealEditor.ps1') -Commandlet
$tag = if ($Shot) { $Shot.Replace(',', '_') } else { 'all' }
$log = Join-Path $root "Saved\Logs\Trailer-$tag.log"
$user = Join-Path $root 'Saved\Validation\Trailer-User'
if ($OutputDirectory) {
	# 실행 폴더 하나에 프레임·기록·사용자 폴더를 모은다. 다른 촬영과 기록 파일을 나눠 쓰지 않는다.
	$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
	$log = Join-Path $OutputDirectory "Trailer-$tag.log"
	$user = Join-Path $OutputDirectory 'User'
}
New-Item -ItemType Directory -Force -Path $user | Out-Null
$arguments = @((Join-Path $root 'IndieGame.uproject'), '-game', '-unattended', '-nosplash', '-NoLoadingScreen',
	'-RenderOffscreen', '-d3d12', '-nosound', '-Windowed', "-ResX=$Width", "-ResY=$Height", '-ForceRes',
	'-IGMissingFloor', '-IGArrivalCapture', '-IGTrailerCapture', '-IGSkipFrontend',
	'-UseFixedTimeStep', '-FPS=30', "-UserDir=$user",
	'"-ExecCmds=Scalability 3,r.ScreenPercentage 100"', "-abslog=$log")
if ($Shot) { $arguments += "-IGTrailerShot=$Shot" }
if ($OutputDirectory) { $arguments += ('"-IGTrailerOut={0}"' -f $OutputDirectory) }
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
$null = $process.Handle
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
	$process.Kill()
	throw "트레일러 촬영 시간 초과: $log"
}
Select-String -LiteralPath $log -Pattern 'TRAILER_' | ForEach-Object { $_.Line }
if ($process.ExitCode -ne 0 -or -not (Select-String -LiteralPath $log -Pattern 'TRAILER_CAPTURE PASS dropped=0')) {
	throw "트레일러 촬영 실패: $log"
}
