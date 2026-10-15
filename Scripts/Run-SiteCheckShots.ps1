[CmdletBinding()]
param(
	# 403호에서 기다리는 쪽을 찍는다. 주지 않으면 옥상과 5층에서 보는 쪽이다.
	[switch]$Away,
	# en·ja·zh-Hans·zh-Hant로 찍으면 대사 길이를 본다. 프로필 언어는 그대로다.
	[string]$Culture
)

# 경찰 현장 확인 검사를 렌더하며 몇 장면을 찍는다. 옥상 문을 나서는 두 사람, 5층 문을 여는 관리인,
# 벽을 두드리는 경찰관, 돌아서서 말하는 장면이다. 결과는 -UserDir 아래 Saved/SiteCheckReview에 남는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-SiteCheckShots.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot 'Saved/Logs/SiteCheckShots.log'
$probeUser = Join-Path $projectRoot ('Saved/Validation/SiteCheckShots-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
$extra = @()
if ($Away) { $extra += '-IGSiteCheckAway' }
if (-not [string]::IsNullOrWhiteSpace($Culture)) { $extra += ('-IGCulture=' + $Culture) }
try {
	& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen -d3d12 -nosound `
		-Windowed -ResX=1600 -ResY=900 -ForceRes -IGListenerGreybox -IGSiteCheckProbe -IGSiteCheckShots -IGSkipFrontend `
		'-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100' `
		"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
} finally {
	if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
	elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
Select-String -LiteralPath $logPath -Pattern 'SITECHECK_(SHOT|PROBE|CHECK)|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
