[CmdletBinding()]
param(
	[ValidateSet('A', 'B')][string]$Ending = 'A',
	# en·ja·zh-Hans·zh-Hant로 찍으면 장면 글이 번역에서 넘치는지 본다. 프로필 언어는 그대로다.
	[string]$Culture
)

# 결말 에필로그를 밤4 없이 바로 실제 시간으로 흘리며 장면마다 한 장씩 찍는다(A 99초, B 83초).
# 창은 띄우지 않고 결과는 -UserDir 아래 Saved/EpiloguePreview에 남는다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot "Saved/Logs/EpiloguePreview-$Ending.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/EpiloguePreview-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
$extra = @()
if (-not [string]::IsNullOrWhiteSpace($Culture)) { $extra += ('-IGCulture=' + $Culture) }
try {
	& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen -d3d12 -nosound `
		-Windowed -ResX=1600 -ResY=900 -ForceRes -IGListenerGreybox "-IGEpiloguePreview=$Ending" -IGSkipFrontend `
		'-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100' `
		"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
} finally {
	if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
	elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
Select-String -LiteralPath $logPath -Pattern 'EPILOGUE_PREVIEW|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
