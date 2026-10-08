[CmdletBinding()]
param(
	# en·ja·zh-Hans·zh-Hant로 찍으면 번역 길이가 쪽지와 폰 화면에 들어가는지 본다. 프로필 언어는 그대로다.
	[string]$Culture
)

# 낮 장면 검사를 렌더하며 몇 장면을 찍는다. 연석의 정우, 403호 문의 303호 쪽지 셋, 폰의 동네
# 이야기. 창은 띄우지 않고 결과는 -UserDir 아래 Saved/DayScenesReview에 남는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot 'Saved/Logs/DayScenesShots.log'
$probeUser = Join-Path $projectRoot ('Saved/Validation/DayScenesShots-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
$cultureArguments = @()
if (-not [string]::IsNullOrWhiteSpace($Culture)) { $cultureArguments += ('-IGCulture=' + $Culture) }
try {
	& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen -d3d12 -nosound `
		-Windowed -ResX=1600 -ResY=900 -ForceRes -IGListenerGreybox -IGDayScenesProbe -IGDayScenesShots -IGSkipFrontend `
		'-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100' `
		"-UserDir=$probeUser" "-abslog=$logPath" @cultureArguments | Out-Null
} finally {
	if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
	elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
Select-String -LiteralPath $logPath -Pattern 'DAYSCENES_(SHOT|PROBE|CHECK)|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
