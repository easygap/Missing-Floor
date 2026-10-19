[CmdletBinding()]
param(
	[ValidateRange(640, 3840)][int]$ResX = 1920,
	[ValidateRange(360, 2160)][int]$ResY = 1080,
	# 비우면 Saved/Validation 아래 새 실행 폴더에 남기고 자동 정리에 맡긴다. C: 공간이 모자라면 다른
	# 드라이브의 빈 폴더를 준다(그 폴더는 자동 정리 밖이다).
	[string]$OutputDirectory = '',
	[ValidateRange(120, 3600)][int]$TimeoutSeconds = 1500,
	# 몇 장면만 다시 찍는다. 쉼표로 여럿(storage-wall-torch,corridor-floor-torch).
	[string]$Views = ''
)
# 화면 잡티를 원인별로 나눠 찍는다(IGListenerGreyboxNoiseReview.cpp). 같은 자리에서 톤매핑 양자화 잡음,
# 텍스처 밉 바이어스, 스페큘러, 루멘 GI·반사, AA 방식을 하나씩만 바꿔 정지 두 장과 천천히 도는 여덟
# 장을 남긴다. 화질 설정은 덮어쓰지 않는다. 실제로 쓰인 값은 기록의 SCREEN_NOISE_VIEW 줄에 남는다.
# 분석은 python Scripts/analyze_screen_noise.py <실행 폴더>로 한다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-ScreenNoiseReview.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
	$projectRoot = Split-Path -Parent $PSScriptRoot
	$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
	$runRoot = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else {
		Join-Path $projectRoot ('Saved/Validation/ScreenNoise-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
	}
	Add-IGArtifactDirectory -Scope $igArtifactScope -Directory $runRoot -Variant 'ScreenNoise'
	$frames = Join-Path $runRoot "$($ResX)x$($ResY)"
	$user = Join-Path $runRoot "User-$($ResX)x$($ResY)"
	$log = Join-Path $runRoot "ScreenNoise-$($ResX)x$($ResY).log"
	New-Item -ItemType Directory -Force -Path $frames, $user | Out-Null
	$arguments = @(
		('"{0}"' -f (Join-Path $projectRoot 'IndieGame.uproject')), '-game', '-unattended', '-nosplash', '-NoLoadingScreen',
		'-RenderOffscreen', '-d3d12', '-nosound', '-Windowed', "-ResX=$ResX", "-ResY=$ResY", '-ForceRes',
		'-IGMissingFloor', '-IGArrivalCapture', '-IGScreenNoiseReview', '-IGSkipFrontend', '-IGImportantSoundCaptions',
		'-UseFixedTimeStep', '-FPS=30', ('"-UserDir={0}"' -f $user), ('"-IGScreenNoiseOut={0}"' -f $frames),
		('"-abslog={0}"' -f $log)
	) + @(if ($Views) { "-IGScreenNoiseViews=$Views" })
	$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
	# Windows PowerShell 5.1은 핸들을 먼저 잡아 두지 않으면 ExitCode를 비워 둔다.
	$null = $process.Handle
	if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
		& taskkill.exe /PID $process.Id /T /F | Out-Null
		throw "화면 잡티 촬영 시간 초과: $log"
	}
	Select-String -LiteralPath $log -Pattern 'SCREEN_NOISE' | ForEach-Object { $_.Line }
	if ($process.ExitCode -ne 0 -or -not (Select-String -LiteralPath $log -Pattern 'SCREEN_NOISE PASS .* dropped=0')) {
		throw "화면 잡티 촬영 실패: $log"
	}
	Write-Host "SCREEN_NOISE_OUTPUT $runRoot"
	$igArtifactSucceeded = $true
}
finally {
	Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
