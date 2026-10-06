[CmdletBinding()]
param(
	[ValidateRange(60, 600)][int]$TimeoutSeconds = 240,
	# 전후 비교 촬영에 쓴다. 예: -ExtraArguments '-IGCameraTexture=0'
	[string[]]$ExtraArguments = @(),
	# 번역 README에 걸 화면을 그 언어로 찍는다. 결과는 game-이름-언어.png로 남고
	# 한국어 원본(game-이름.png)은 그대로 둔다. 예: -Culture ja
	[ValidateSet('', 'en', 'ja', 'zh-Hans', 'zh-Hant')][string]$Culture = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$runLog = Join-Path $projectRoot 'Saved/Logs/ReadmeCapture.log'
$settings = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$mediaRoot = Join-Path $projectRoot 'Docs/Media'
$shotNames = @('bedroom','corridor-day','alley','store','corridor-night','booth','bedroom-dawn','alley-dawn')
$startedAt = Get-Date
$koreanBackup = $null
try {
	if ($Culture) {
		# 게임은 언제나 game-이름.png에 쓴다. 한국어 원본을 잠깐 옮겨 두었다가 끝나면 되돌린다.
		$koreanBackup = Join-Path $projectRoot ('Saved/Validation/Readme-Korean-' + [guid]::NewGuid().ToString('N'))
		New-Item -ItemType Directory -Path $koreanBackup | Out-Null
		foreach ($name in $shotNames) {
			$shot = Join-Path $mediaRoot "game-$name.png"
			if (Test-Path -LiteralPath $shot) { Copy-Item -LiteralPath $shot -Destination $koreanBackup }
		}
	}
	$settingsBackup = if (Test-Path -LiteralPath $settings) { [IO.File]::ReadAllBytes($settings) } else { $null }
	$process = $null
	try {
		$arguments = @(
			('"{0}"' -f (Join-Path $projectRoot 'IndieGame.uproject')),
			'-game', '-unattended', '-nosplash', '-NoLoadingScreen',
			'-RenderOffscreen', '-d3d12', '-nosound', '-Windowed', '-ResX=1920', '-ResY=1080', '-ForceRes',
			('-UserDir={0}' -f (Join-Path $projectRoot 'Saved/Validation/Readme-User')),
			'-IGMissingFloor', '-IGArrivalCapture', '-IGReadmeCapture', '-IGSkipFrontend',
			'"-ExecCmds=Scalability 2,r.ScreenPercentage 100,t.MaxFPS 60"', ('"-abslog={0}"' -f $runLog)
		) + @(if ($Culture) { "-IGCulture=$Culture" }) + $ExtraArguments
		$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
		# Windows PowerShell 5.1은 핸들을 먼저 잡아 두지 않으면 ExitCode를 비워 둔다.
		$null = $process.Handle
		if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
			& taskkill.exe /PID $process.Id /T /F | Out-Null
			throw "소개 화면 촬영 시간 초과: $runLog"
		}
		if ($process.ExitCode -ne 0 -or -not (Select-String -LiteralPath $runLog -Pattern 'README_CAPTURE PASS shots=8 production=1')) {
			throw "소개 화면 촬영 실패: $runLog"
		}
	}
	finally {
		if ($null -ne $settingsBackup) { [IO.File]::WriteAllBytes($settings, $settingsBackup) }
		elseif (Test-Path -LiteralPath $settings) { Remove-Item -LiteralPath $settings }
	}
	foreach ($name in $shotNames) {
		$shot = Get-Item -LiteralPath (Join-Path $mediaRoot "game-$name.png")
		if ($shot.LastWriteTime -lt $startedAt -or $shot.Length -lt 10000) { throw "새 화면이 없습니다: $name" }
	}
	if ($Culture) {
		foreach ($name in $shotNames) {
			Move-Item -Force -LiteralPath (Join-Path $mediaRoot "game-$name.png") `
				-Destination (Join-Path $mediaRoot "game-$name-$Culture.png")
		}
	}
}
finally {
	if ($koreanBackup) {
		foreach ($kept in Get-ChildItem -LiteralPath $koreanBackup -File) {
			Copy-Item -Force -LiteralPath $kept.FullName -Destination $mediaRoot
		}
		Remove-Item -Recurse -Force -LiteralPath $koreanBackup
	}
}
$label = if ($Culture) { " ($Culture)" } else { '' }
Write-Host "README_CAPTURE PASS 새 플레이 화면 8장, 1920×1080$label"
