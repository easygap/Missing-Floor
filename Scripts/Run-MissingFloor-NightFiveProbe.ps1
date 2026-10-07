<#
.SYNOPSIS
	§9 「밤 5」 슬롯 검증.

.DESCRIPTION
	두 가지를 검사한다.

	하나는 순수 논리다. 밤 5는 액션 목록의 **마지막**이면서 화면에서는 「이어하기」
	바로 **밑**이다. 이 분리 덕분에 기존 액션 인덱스가 하나도 움직이지 않지만,
	대응이 깨지면 플레이어가 누른 줄과 실행되는 액션이 정확히 한 행씩 어긋난다 —
	조용히. 그래서 (이어하기 유무) × (밤 5 유무) 네 조합 모두에서 액션↔자리
	대응이 전단사인지, 밤 5가 이어하기 바로 밑인지, 일시정지 메뉴에는 절대
	없는지를 실행해서 확인한다.

	다른 하나는 30초 자체다. 소리 두 개가 저작된 시각에 나가고, 30초에 끝나고,
	끝난 뒤 행이 흐려지지만 여전히 고를 수 있는지.

	하네스는 세이브를 만들지 않는다(§14). 조회 결과만 덮어써서 그 행이 있는
	세계를 만든다 — 세이브 파일을 위조하는 것과는 다른 일이다.

.NOTES
	게임 창은 절대 뜨지 않는다. -RenderOffScreen을 주면 Windows에서 엔진이
	실제 윈도우 애플리케이션 대신 Null 플랫폼 애플리케이션을 만들고
	(WindowsPlatformApplicationMisc.cpp), D3D12 뷰포트가 스왑체인을 만들지
	않는다(WindowsD3D12Viewport.cpp). 창이 숨겨지는 게 아니라 생성되지 않는다.

	이 스크립트는 그 플래그가 빠진 인자 조합을 실행 자체로 거부한다.
#>
[CmdletBinding()]
param(
	[ValidateRange(60, 900)]
	[int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectFile = Join-Path $projectRoot 'IndieGame.uproject'
if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
	throw "언리얼 프로젝트를 찾을 수 없습니다: $projectFile"
}

$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') `
	-ProjectPath $projectFile -Commandlet
if ([string]::IsNullOrWhiteSpace($editor)) {
	throw 'IndieGame.uproject에 맞는 언리얼 에디터를 찾을 수 없습니다.'
}

$logDirectory = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Force $logDirectory | Out-Null
$runLog = Join-Path $logDirectory 'MissingFloorNightFive.log'
if (Test-Path -LiteralPath $runLog) {
	Remove-Item -LiteralPath $runLog -Force
}

$arguments = @(
	$projectFile,
	'-game',
	'-unattended',
	'-nosplash',
	'-NoLoadingScreen',
	# 창을 만들지 않는 플래그. 아래 가드가 이것의 존재를 강제한다.
	'-RenderOffScreen',
	'-nullrhi',
	'-nosound',
	'-stdout',
	'-FullStdOutLogOutput',
	"-abslog=$runLog",
	'-IGNightFiveProbe'
)

if ($arguments -notcontains '-RenderOffScreen') {
	throw '-RenderOffScreen 없이는 실행하지 않습니다. 게임 창이 뜰 수 있습니다.'
}
foreach ($forbidden in @('-windowed', '-fullscreen', '-game -log')) {
	if ($arguments -contains $forbidden) {
		throw "화면을 띄울 수 있는 인자가 포함되었습니다: $forbidden"
	}
}

Write-Host '§9 「밤 5」 슬롯 검증 시작 — 오프스크린, 창 없음' `
	-ForegroundColor DarkGray
$process = Start-Process -FilePath $editor -ArgumentList $arguments `
	-PassThru -WindowStyle Hidden
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
	try { & taskkill.exe /PID $process.Id /T /F | Out-Null } catch {}
	throw "밤 5 검증이 ${TimeoutSeconds}초 안에 끝나지 않았습니다: $runLog"
}
if (-not (Test-Path -LiteralPath $runLog -PathType Leaf)) {
	throw "검증 로그가 생성되지 않았습니다: $runLog"
}

$lines = Select-String -Path $runLog -Pattern 'MISSINGFLOOR_NIGHT5'
foreach ($line in $lines) {
	$colour = if ($line.Line -match 'PASS') { 'Green' } else { 'Red' }
	Write-Host $line.Line -ForegroundColor $colour
}
if (-not $lines) {
	throw "밤 5 검증이 아무것도 보고하지 않았습니다. 로그: $runLog"
}
if ($lines[-1].Line -notmatch 'PASS') {
	throw "§9 「밤 5」 검증이 실패했습니다. 로그: $runLog"
}

# §14: 슬롯을 위한 실제 세이브 파일은 만들지 않는다. 검증 실행이 세이브를
# 남기지 않았다는 것을 파일 시스템으로 확인한다.
$saveDirectory = Join-Path $projectRoot 'Saved\SaveGames'
if (Test-Path -LiteralPath $saveDirectory) {
	$suspicious = Get-ChildItem -LiteralPath $saveDirectory -Filter '*ight*ive*' `
		-ErrorAction SilentlyContinue
	if ($suspicious) {
		throw "밤 5가 세이브 파일을 만들었습니다: $($suspicious.Name -join ', ')"
	}
}
Write-Host '세이브 파일 없음 확인' -ForegroundColor Green
