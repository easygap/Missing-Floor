<#
.SYNOPSIS
	§14 CCTV 채널 5의 화면이 실제로 렌더되는지 검증한다.

.DESCRIPTION
	그레이박스 프로브는 채널 5의 구조를 검증한다 — 누르기 전에는 아무것도
	할당되지 않고, 누르면 352×288 렌더타깃 하나가 생기고, 낮은 형체가
	가장자리를 지나가고, 죽으면 전부 해제되고, 두 번째 누르기는 거부된다.
	그 전부가 -nullrhi에서도 통과한다. 그림이 실제로 찍혔는지는 통과하지 않는다.

	NullRHI에서 SceneCapture2D는 아무것도 그리지 않고 렌더타깃은 완전한 검정으로
	남는다. 그리고 검정은 밝기 하한을 뺀 모든 검사를 만족시킨다 — §11 V5 스윕이
	처음에 스스로를 속인 방식이 정확히 이것이었다. 그래서 픽셀 판정은 실제 RHI가
	있을 때만 돌고, 여기서는 -nullrhi를 아예 금지한다.

	판정은 렌더타깃을 직접 읽어서 한다. 모니터를 찍은 스크린샷이 아니라 캡처가
	별관을 그렸는지를 보는 것이고, 1회로 끝나는 비트에서 얻을 수 있는 유일한
	측정값이다.

.NOTES
	게임 창은 절대 뜨지 않는다. -RenderOffScreen을 주면 Windows에서 엔진이
	실제 윈도우 애플리케이션 대신 Null 플랫폼 애플리케이션을 만들고
	(WindowsPlatformApplicationMisc.cpp), D3D12 뷰포트가 스왑체인을 만들지
	않는다(WindowsD3D12Viewport.cpp). 창이 숨겨지는 게 아니라 생성되지 않는다.

	이 스크립트는 그 플래그가 빠진 인자 조합을 실행 자체로 거부한다.
#>
[CmdletBinding()]
param(
	# 노출 저작용. 작을수록 밝다 — 두 경계가 같은 적응 목표를 클램프한다.
	# 지정하지 않으면 코드에 저작된 값을 쓴다.
	[ValidateRange(0.001, 4.0)]
	[double]$Exposure = 0,
	# 같은 화각을 크게 내보낸다. 출하 해상도는 CIF 그대로다.
	[ValidateRange(1, 6)]
	[int]$FeedScale = 1,
	[ValidateRange(60, 900)]
	[int]$TimeoutSeconds = 420
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
$runLog = Join-Path $logDirectory 'MissingFloorCctvFeed.log'
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
	'-d3d12',
	'-nosound',
	'-ResX=1280',
	'-ResY=720',
	'-ForceRes',
	'-NoVSync',
	'-stdout',
	'-FullStdOutLogOutput',
	"-abslog=$runLog",
	'-IGListenerGreybox',
	'-IGSkipFrontend',
	'-IGListenerGreyboxProbe',
	'-IGCctvFeedProbe'
)
if ($Exposure -gt 0) {
	$arguments += ('-IGCctvExposure={0}' -f $Exposure)
}
if ($FeedScale -gt 1) {
	$arguments += ('-IGCctvFeedScale={0}' -f $FeedScale)
	# 해상도가 바뀌면 구조 계약의 352x288 검사가 실패하므로 진단 전용이다.
	Write-Host '진단 배율 실행 — 구조 계약은 이 실행에서 판정하지 않는다' `
		-ForegroundColor DarkYellow
}

if ($arguments -notcontains '-RenderOffScreen') {
	throw '-RenderOffScreen 없이는 실행하지 않습니다. 게임 창이 뜰 수 있습니다.'
}
foreach ($forbidden in @('-windowed', '-fullscreen', '-game -log')) {
	if ($arguments -contains $forbidden) {
		throw "화면을 띄울 수 있는 인자가 포함되었습니다: $forbidden"
	}
}
# NullRHI에서는 캡처가 검정이고 검정은 통과해 버린다. 실행을 막는 편이 옳다.
if ($arguments -contains '-nullrhi') {
	throw '-nullrhi로는 화면을 판정할 수 없습니다. 검정이 통과합니다.'
}

Write-Host '§14 CCTV 채널 5 화면 판정 시작 — 오프스크린, 창 없음' `
	-ForegroundColor DarkGray
$process = Start-Process -FilePath $editor -ArgumentList $arguments `
	-PassThru -WindowStyle Hidden
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
	try { & taskkill.exe /PID $process.Id /T /F | Out-Null } catch {}
	throw "화면 판정이 ${TimeoutSeconds}초 안에 끝나지 않았습니다: $runLog"
}
if (-not (Test-Path -LiteralPath $runLog -PathType Leaf)) {
	throw "판정 로그가 생성되지 않았습니다: $runLog"
}

$structure = Select-String -Path $runLog -Pattern 'MISSINGFLOOR_CCTV5 (PASS|FAIL)'
foreach ($line in $structure) {
	$colour = if ($line.Line -match 'PASS') { 'Green' } else { 'Red' }
	Write-Host $line.Line -ForegroundColor $colour
}
if (-not $structure -or ($structure[-1].Line -notmatch 'PASS')) {
	throw "채널 5 구조 계약이 통과하지 않았습니다. 로그: $runLog"
}

$feed = Select-String -Path $runLog -Pattern 'MISSINGFLOOR_CCTV5_FEED'
if (-not $feed) {
	throw "화면 판정이 아예 실행되지 않았습니다. 로그: $runLog"
}
# 인자 괄호 안의 if는 Windows PowerShell 5.1이 명령으로 읽는다. $( )로 감싼다.
Write-Host $feed[-1].Line -ForegroundColor $(
	if ($feed[-1].Line -match 'PASS') { 'Green' } else { 'Red' })
if ($feed[-1].Line -notmatch 'PASS') {
	throw "채널 5가 그림을 그리지 못했습니다. 로그: $runLog"
}
