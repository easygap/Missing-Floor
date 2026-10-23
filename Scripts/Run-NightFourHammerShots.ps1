[CmdletBinding()]
param(
	# 빗장 없이 두어 잡힌 뒤 깨어난 403호를 찍는다. 주지 않으면 빗장을 건 쪽이다.
	[switch]$Open,
	# en·ja·zh-Hans·zh-Hant로 찍으면 자막 길이를 본다. 프로필 언어는 그대로다.
	[string]$Culture,
	# 망치 자리 화면을 원인별로 갈라 찍는다(HUD·덧댄 것·언릿·노멀·그림자·안티앨리어싱·와이어프레임, 움직임).
	# 그래픽 설정은 건드리지 않고 새 프로필 기본값으로 1920×1080에서 찍는다. 결과는 Saved/NightFourWallDiag.
	[switch]$Diag
)

# 밤4 망치질 검사를 렌더하며 몇 장면을 찍는다. 벽 안의 대답 자막, 정전 속 어둑시니, 깜깜한 403호.
# 창은 띄우지 않고 결과는 -UserDir 아래 Saved/NightFourHammerReview에 남는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-NightFourHammerShots.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot 'Saved/Logs/NightFourHammerShots.log'
$probeUser = Join-Path $projectRoot ('Saved/Validation/NightFourHammerShots-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
$extra = @()
if ($Open) { $extra += '-IGNightFourHammerOpen' }
$resolution = @('-ResX=1600', '-ResY=900')
$scalability = '-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100'
if ($Diag) {
	$extra += @('-IGNightFourWallDiag', '-UseFixedTimeStep', '-FPS=30')
	$resolution = @('-ResX=1920', '-ResY=1080')
	$scalability = '-IGDiagDefaultSettings'
}
if (-not [string]::IsNullOrWhiteSpace($Culture)) { $extra += ('-IGCulture=' + $Culture) }
try {
	& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen -d3d12 -nosound `
		-Windowed @resolution -ForceRes -IGListenerGreybox -IGNightFourHammerProbe -IGNightFourHammerShots -IGSkipFrontend `
		$scalability "-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
} finally {
	if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
	elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
Select-String -LiteralPath $logPath -Pattern 'NIGHT4HAMMER_(SHOT|PROBE|CHECK)|NIGHT4DIAG_(SETTINGS|MOMENT)|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
if ($Diag) { Write-Host ('NIGHT4DIAG_DIR ' + (Join-Path $probeUser 'Saved/NightFourWallDiag')) }

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
