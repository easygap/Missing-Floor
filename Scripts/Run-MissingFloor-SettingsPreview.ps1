[CmdletBinding()]
param(
	[string]$ArchiveDirectory,
	[string]$OutputDirectory,
	[ValidateRange(20, 180)]
	[int]$TimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-MissingFloor-SettingsPreview.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($ArchiveDirectory)) {
	throw '검증할 BuildCookRun Shipping 아카이브의 -ArchiveDirectory가 필요합니다.'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = Join-Path $projectRoot 'Saved\Validation\SettingsPreview'
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) {
	$existing = @(Get-ChildItem -LiteralPath $outputRoot -Force)
	if ($existing.Count -gt 0) {
		throw "설정 미리 보기 출력 폴더는 비어 있어야 합니다: $outputRoot"
	}
}

$probeScript = Join-Path $PSScriptRoot 'Run-MissingFloor-FrontendShippingProbe.ps1'
& $probeScript `
	-ArchiveDirectory $ArchiveDirectory `
	-EvidenceDirectory $outputRoot `
	-TimeoutSeconds $TimeoutSeconds

$mediaRoot = Join-Path $projectRoot 'Docs\Media'
$displaySource = Join-Path $outputRoot '1920x1080\settings-display.png'
$accessibilitySource = Join-Path $outputRoot '1920x1080\settings-accessibility.png'
foreach ($capture in @(
	[pscustomobject]@{
		source = $displaySource
		target = Join-Path $mediaRoot 'settings-display-1080.png'
	},
	[pscustomobject]@{
		source = $accessibilitySource
		target = Join-Path $mediaRoot 'settings-accessibility-1080.png'
	}
)) {
	if (-not (Test-Path -LiteralPath $capture.source -PathType Leaf)) {
		throw "설정 미리 보기 PNG가 생성되지 않았습니다: $($capture.source)"
	}
	Copy-Item -LiteralPath $capture.source -Destination $capture.target -Force
}

Write-Host (
	'MISSING_FLOOR_SETTINGS_PREVIEW PASS ' +
	"display=$displaySource accessibility=$accessibilitySource") `
	-ForegroundColor Green

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
