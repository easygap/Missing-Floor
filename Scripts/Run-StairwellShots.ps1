[CmdletBinding()]
param()

# 뒤따르는 발을 렌더해 몇 장면을 찍는다. 창은 띄우지 않고 결과는 -UserDir 아래 Saved/StairwellReview에 남는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-StairwellShots.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot 'Saved/Logs/StairwellShots.log'
$probeUser = Join-Path $projectRoot ('Saved/Validation/StairwellShots-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
try {
	& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen -d3d12 -nosound `
		-Windowed -ResX=1600 -ResY=900 -ForceRes -IGStairwellProbe -IGStairwellShots -IGSkipFrontend `
		'-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100' `
		"-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
} finally {
	if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
	elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
Select-String -LiteralPath $logPath -Pattern 'STAIRWELL_(SHOT|PROBE|CHECK)|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
