[CmdletBinding()]
param(
	# 빗장 없이 두어 잡힌 뒤 깨어난 403호를 찍는다. 주지 않으면 빗장을 건 쪽이다.
	[switch]$Open,
	# en·ja·zh-Hans·zh-Hant로 찍으면 자막 길이를 본다. 프로필 언어는 그대로다.
	[string]$Culture
)

# 밤4 망치질 검사를 렌더하며 몇 장면을 찍는다. 벽 안의 대답 자막, 정전 속 어둑시니, 깜깜한 403호.
# 창은 띄우지 않고 결과는 -UserDir 아래 Saved/NightFourHammerReview에 남는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot 'Saved/Logs/NightFourHammerShots.log'
$probeUser = Join-Path $projectRoot ('Saved/Validation/NightFourHammerShots-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
$extra = @()
if ($Open) { $extra += '-IGNightFourHammerOpen' }
if (-not [string]::IsNullOrWhiteSpace($Culture)) { $extra += ('-IGCulture=' + $Culture) }
try {
	& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen -d3d12 -nosound `
		-Windowed -ResX=1600 -ResY=900 -ForceRes -IGListenerGreybox -IGNightFourHammerProbe -IGNightFourHammerShots -IGSkipFrontend `
		'-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100' `
		"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
} finally {
	if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
	elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
Select-String -LiteralPath $logPath -Pattern 'NIGHT4HAMMER_(SHOT|PROBE|CHECK)|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
