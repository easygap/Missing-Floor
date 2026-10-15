[CmdletBinding()]
param([switch]$Shots, [ValidateSet(30, 60)][int]$FrameRate = 60)

# 괴이 넷(손님, 관리인, 어둑시니, 위층 사람)에게 차례로 잡혀 보고 잡힌 뒤가 저마다
# 다른지 잰다. 깨어나는 자리, 남는 값(걸쇠·열쇠·건전지·손전등), 밀린 시계, 바닥에서
# 일어나는 동안의 걷기 잠금, 위층 사람의 12초 유예, 그리고 잡히는 동안 화면이 기울거나
# 흔들리지 않는지 본다. 침대로 돌아가면 실패다.
# -Shots는 렌더를 켜고 잡히는 동안과 깨어난 뒤를 찍는다(Saved/CaptureVarietyReview).

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-CaptureVarietyProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$mode = if ($Shots) { 'Shots' } else { 'Check' }
$logPath = Join-Path $projectRoot "Saved/Logs/CaptureVariety-$mode-$FrameRate.log"
$probeUser = Join-Path $projectRoot ("Saved/Validation/CaptureVariety-$mode-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
$probeArgs = @($projectPath, '-game', '-unattended', '-nosplash', '-nosound', '-NoLoadingScreen',
	'-IGListenerGreybox', '-IGCaptureVarietyProbe', '-IGSkipFrontend', '-UseFixedTimeStep', "-FPS=$FrameRate",
	"-UserDir=$probeUser", "-abslog=$logPath")
if ($Shots) {
	$review = Join-Path $projectRoot 'Saved/CaptureVarietyReview'
	if (Test-Path -LiteralPath $review) { Remove-Item -LiteralPath $review -Recurse -Force }
	$probeArgs += @('-IGCaptureVarietyShots', '-windowed', '-ResX=1280', '-ResY=720', '-RenderOffscreen')
}
else {
	$probeArgs += @('-nullrhi', '-RenderOffscreen')
}
& $editor @probeArgs | Out-Null
Select-String -LiteralPath $logPath -Pattern 'CAPTUREVARIETY_(CHECK|PROBE|WAKE|ROOM|EODUKSINI|SHOT)' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'CAPTUREVARIETY_PROBE PASS failures=0')) {
	throw "포획 검사에 실패했습니다: $logPath"
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
