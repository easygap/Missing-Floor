[CmdletBinding()]
param(
	[ValidateSet(30, 60)][int]$FrameRate = 60,
	# 렌더를 켜고 판재 밑 녹음기와 골목 약봉투를 찍는다. 창은 띄우지 않는다(오프스크린).
	[switch]$Shots,
	# 찍을 때 자막 언어. 비우면 프로필 언어다.
	[string]$Culture
)

# 서일영의 녹음기와 골목 약봉투를 겪는다. 5층 판재 밑 녹음기를 틀다가 새벽이 오면 멈추고,
# 다시 틀어 끝까지 들으면 목격이 남는다. 골목 건너 남자가 섰다 간 자리에 약봉투가 생기고,
# 대치와 보도가 녹음기 줄을 고르는지 본다. 기본은 화면 없이(nullrhi) 돈다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-SeoRecorderProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$suffix = if ($Shots) { 'Shots' } else { "$FrameRate" }
$logPath = Join-Path $projectRoot "Saved/Logs/SeoRecorder-$suffix.log"
$probeUser = Join-Path $projectRoot ('Saved/Validation/SeoRecorder-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory (Split-Path -Parent $probeUser) -Variant ((Split-Path -Leaf (Split-Path -Parent $probeUser)) -replace '-20\d{6}.*$', '')
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
$extra = @()
if (-not [string]::IsNullOrWhiteSpace($Culture)) { $extra += ('-IGCulture=' + $Culture) }
try {
	if ($Shots) {
		& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen -d3d12 -nosound `
			-Windowed -ResX=1600 -ResY=900 -ForceRes -IGListenerGreybox -IGSeoRecorderProbe -IGSeoRecorderShots -IGSkipFrontend `
			'-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100' `
			"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
	}
	else {
		& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
			-IGListenerGreybox -IGSeoRecorderProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
			"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
	}
} finally {
	if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
	elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
Select-String -LiteralPath $logPath -Pattern 'SEORECORDER|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'SEORECORDER_PROBE PASS failures=0')) {
	throw "서일영 녹음기 검사에 실패했습니다: $logPath"
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
