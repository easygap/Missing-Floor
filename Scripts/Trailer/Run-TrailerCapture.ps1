[CmdletBinding()]
param(
	# 비우면 표의 모든 장면을 찍는다. 쉼표로 여럿을 줄 수 있다(alley,booth-cctv). 열린 PowerShell 안에서
	# 부르면 쉼표가 배열이 되므로 배열로 받아 다시 잇는다.
	[string[]]$Shot = @(),
	[int]$Width = 1920,
	[int]$Height = 1080,
	[int]$TimeoutSeconds = 2400,
	# Build-Trailer.ps1이 넘기는 실행 폴더. 그 폴더의 잠금·소유 기록·정리는 부른 쪽이 맡는다.
	# 비우면 이 스크립트가 Saved/Trailer/Runs 아래에 실행 폴더를 새로 만들어 등록하고 끝날 때 닫는다.
	[string]$OutputDirectory = ''
)
# 트레일러 재료를 30fps 고정 간격으로 찍는다. 프레임은 실행 폴더의 <장면>/frame_NNNN.png로 남는다.
# 단독으로 돌리면 실행마다 Saved/Trailer/Runs/<시각>-<장면> 폴더를 만들고 기록과 사용자 폴더도 그 안에
# 둔다. 성공이든 실패든 시간 초과든 끝나면 자동 정리에 기록을 남긴다. 프레임은 끝난 뒤 24시간은 그대로
# 있고, 같은 장면 묶음의 최근 성공 두 번과 실패 한 번은 그 뒤에도 남는다. 계속 쓸 실행 폴더에는 .keep을
# 둔다. 영상까지 만드는 순서와 단독 촬영 프레임을 쓰는 법은 Docs/TRAILER.md에 있다.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $root 'Scripts\GeneratedArtifactCleanup.ps1')
$shotList = (@($Shot | Where-Object { $_ }) -join ',')
$tag = if ($shotList) { ($shotList -replace '[^A-Za-z0-9,-]', '').Replace(',', '_') } else { 'all' }
$scope = $null
$succeeded = $false
$process = $null
if ($OutputDirectory) {
	$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
	# 부른 쪽이 등록한 실행 폴더에서는 잠금을 다시 잡지 않고 정리도 돌리지 않는다. 여기서 정리하면 부른
	# 쪽이 아직 인코딩할 프레임이 사라질 수 있다. 정리 기록이 끝난 옛 실행 폴더에 덧붙여 찍으면 그 프레임은
	# 아무도 정리하지 않으므로 받지 않는다.
	if ((Test-IGArtifactPath $OutputDirectory (Join-Path $root 'Saved\Trailer\Runs')) -and
		-not (Test-Path -LiteralPath (Join-Path $OutputDirectory '.ig-artifact-owner'))) {
		throw "정리 기록이 없는 실행 폴더에는 찍지 않습니다. -OutputDirectory를 비우면 새 폴더를 만듭니다: $OutputDirectory"
	}
}
else {
	$scope = Enter-IGArtifactScope -ProjectRoot $root -Name 'Run-TrailerCapture.ps1' -Parameters $PSBoundParameters
}
try {
	if (-not $OutputDirectory) {
		$OutputDirectory = Join-Path $root ('Saved\Trailer\Runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + "-$tag")
		Add-IGArtifactDirectory -Scope $scope -Directory $OutputDirectory -Variant 'Shots'
		if (-not (Test-Path -LiteralPath (Join-Path $OutputDirectory '.ig-artifact-owner'))) {
			throw "새 실행 폴더를 정리 대상으로 등록하지 못했습니다: $OutputDirectory"
		}
		Write-Host "TRAILER_RUN $OutputDirectory"
	}
	# 기록과 사용자 폴더도 실행 폴더 안에 둔다. 다른 촬영과 같은 기록 파일·세이브를 나눠 쓰지 않는다.
	$log = Join-Path $OutputDirectory "Trailer-$tag.log"
	$user = Join-Path $OutputDirectory 'User'
	New-Item -ItemType Directory -Force -Path $user | Out-Null
	$editor = & (Join-Path $root 'Scripts\Resolve-UnrealEditor.ps1') -Commandlet
	$arguments = @((Join-Path $root 'IndieGame.uproject'), '-game', '-unattended', '-nosplash', '-NoLoadingScreen',
		'-RenderOffscreen', '-d3d12', '-nosound', '-Windowed', "-ResX=$Width", "-ResY=$Height", '-ForceRes',
		'-IGMissingFloor', '-IGArrivalCapture', '-IGTrailerCapture', '-IGSkipFrontend',
		'-UseFixedTimeStep', '-FPS=30', "-UserDir=$user",
		'"-ExecCmds=Scalability 3,r.ScreenPercentage 100"', "-abslog=$log", ('"-IGTrailerOut={0}"' -f $OutputDirectory))
	if ($shotList) { $arguments += "-IGTrailerShot=$shotList" }
	$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
	$null = $process.Handle
	if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
		throw "트레일러 촬영 시간 초과: $log"
	}
	if (Test-Path -LiteralPath $log -PathType Leaf) {
		Select-String -LiteralPath $log -Pattern 'TRAILER_' | ForEach-Object { $_.Line }
	}
	if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $log -PathType Leaf) -or
		-not (Select-String -LiteralPath $log -Pattern 'TRAILER_CAPTURE PASS dropped=0')) {
		throw "트레일러 촬영 실패: $log"
	}
	$succeeded = $true
}
finally {
	if ($process -and -not $process.HasExited) {
		# 시간 초과나 중단이면 셰이더 컴파일러까지 프로세스 트리를 끝낸다. 실행 폴더를 쓰는 프로세스가 남아
		# 있으면 정리 기록을 쓰지 못해 그 실행은 영영 정리되지 않는다.
		& taskkill.exe /PID $process.Id /T /F | Out-Null
		$null = $process.WaitForExit(30000)
	}
	if ($scope) {
		$until = [DateTime]::UtcNow.AddSeconds(30)
		try {
			while ($OutputDirectory -and [DateTime]::UtcNow -lt $until -and
				(Test-IGArtifactInUse $OutputDirectory @(Get-IGArtifactProcesses))) {
				Start-Sleep -Milliseconds 500
			}
		}
		catch { Write-Warning "실행 폴더를 쓰는 프로세스를 확인하지 못했습니다: $($_.Exception.Message)" }
		Complete-IGArtifactScope -Scope $scope -Succeeded $succeeded
	}
}
