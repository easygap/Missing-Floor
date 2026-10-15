[CmdletBinding()]
param([switch]$Lure)

# 손님 문 검사를 렌더하며 몇 장면을 찍는다. 기본은 5층 철문 빗장과 두 철문, -Lure는 옆집 앞에
# 나타난 몸과 열린 5층 철문간에 선 손님이다. 창은 띄우지 않고 결과는 -UserDir 아래
# Saved/GuestDoorsReview에 남는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-GuestDoorsShots.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$mode = if ($Lure) { 'Lure' } else { 'Doors' }
$logPath = Join-Path $projectRoot "Saved/Logs/GuestDoorsShots-$mode.log"
$probeUser = Join-Path $projectRoot ("Saved/Validation/GuestDoorsShots-$mode-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
$shotArgs = @($projectPath, '-game', '-unattended', '-nosplash', '-NoLoadingScreen', '-RenderOffscreen', '-d3d12', '-nosound',
	'-Windowed', '-ResX=1600', '-ResY=900', '-ForceRes', '-IGListenerGreybox', '-IGGuestDoorsProbe', '-IGGuestDoorsShots',
	'-IGSkipFrontend', '-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100',
	"-UserDir=$probeUser", "-abslog=$logPath")
if ($Lure) { $shotArgs += '-IGGuestLure' }
try {
	& $editor @shotArgs | Out-Null
} finally {
	if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
	elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
Select-String -LiteralPath $logPath -Pattern 'GUESTDOORS_(SHOT|PROBE|CHECK)|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
