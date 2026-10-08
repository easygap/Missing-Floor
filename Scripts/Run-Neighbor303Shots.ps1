[CmdletBinding()]
param(
	# 3층과 4층 사이 계단에서 마주치는 쪽을 찍는다. 주지 않으면 4층 계단 목이다.
	[switch]$Stairs,
	# en·ja·zh-Hans·zh-Hant로 찍으면 자막 길이를 본다. 프로필 언어는 그대로다.
	[string]$Culture
)

# 303호 계단 검사를 렌더하며 몇 장면을 찍는다. 마주쳐 말하는 303호와 내려가는 뒷모습이다.
# 창은 띄우지 않고 결과는 -UserDir 아래 Saved/Neighbor303Review에 남는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot 'Saved/Logs/Neighbor303Shots.log'
$probeUser = Join-Path $projectRoot ('Saved/Validation/Neighbor303Shots-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
$extra = @()
if ($Stairs) { $extra += '-IGNeighbor303Stairs' }
if (-not [string]::IsNullOrWhiteSpace($Culture)) { $extra += ('-IGCulture=' + $Culture) }
try {
	& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen -d3d12 -nosound `
		-Windowed -ResX=1600 -ResY=900 -ForceRes -IGListenerGreybox -IGNeighbor303Probe -IGNeighbor303Shots -IGSkipFrontend `
		'-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100' `
		"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
} finally {
	if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
	elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
Select-String -LiteralPath $logPath -Pattern 'NEIGHBOR303_(SHOT|PROBE|CHECK)|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
