[CmdletBinding()]
param(
	[string]$OutputDirectory,
	[ValidateRange(30, 180)]
	[int]$TimeoutSeconds = 120,
	# 번역본 글자 폭을 볼 때 쓴다. 비우면 저장된 언어 설정을 따른다.
	[string]$Culture
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-MissingFloor-EndingPreview.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectFile = Join-Path $projectRoot 'IndieGame.uproject'
$resolver = Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1'
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = Join-Path $projectRoot 'Saved\Validation\MissingFloorEndingPreview'
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

$editor = & $resolver -ProjectPath $projectFile -Commandlet
if ([string]::IsNullOrWhiteSpace($editor) -or
	-not (Test-Path -LiteralPath $editor -PathType Leaf)) {
	throw 'Unreal Editor commandlet executable was not found.'
}

function Get-PngDimensions {
	param([Parameter(Mandatory = $true)][string]$Path)
	$bytes = [IO.File]::ReadAllBytes($Path)
	if ($bytes.Length -lt 24 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80 -or
		$bytes[2] -ne 78 -or $bytes[3] -ne 71) {
		throw "Ending preview is not a valid PNG: $Path"
	}
	$width = [BitConverter]::ToInt32([byte[]]@(
		$bytes[19], $bytes[18], $bytes[17], $bytes[16]), 0)
	$height = [BitConverter]::ToInt32([byte[]]@(
		$bytes[23], $bytes[22], $bytes[21], $bytes[20]), 0)
	return [pscustomobject]@{ width = $width; height = $height }
}

$cases = @(
	[pscustomobject]@{
		name = 'listing-1920x1080-default'
		width = 1920
		height = 1080
		elapsed = 0.80
		captionScale = 1.0
		reducedMotion = $false
	},
	[pscustomobject]@{
		name = 'comment-1280x720-text-200-reduced'
		width = 1280
		height = 720
		elapsed = 4.00
		captionScale = 2.0
		reducedMotion = $true
	}
)

foreach ($case in $cases) {
	$capturePath = Join-Path $outputRoot ($case.name + '.png')
	$logPath = Join-Path $outputRoot ($case.name + '.log')
	foreach ($oldPath in @($capturePath, $logPath)) {
		if (Test-Path -LiteralPath $oldPath -PathType Leaf) {
			Remove-Item -LiteralPath $oldPath -Force
		}
	}
	$arguments = @(
		$projectFile,
		'/Game/Maps/Prologue_Morning',
		'-game', '-unattended', '-nosplash', '-NoLoadingScreen',
		'-RenderOffscreen', '-d3d12', '-nosound', '-Windowed',
		('-ResX=' + $case.width),
		('-ResY=' + $case.height),
		'-ForceRes', '-NoVSync',
		('-abslog=' + $logPath),
		'-IGMissingFloorEndingPreview',
		('-IGEndingPreviewExpectedWidth=' + $case.width),
		('-IGEndingPreviewExpectedHeight=' + $case.height),
		('-IGEndingPreviewElapsed=' + $case.elapsed.ToString(
			[Globalization.CultureInfo]::InvariantCulture)),
		('-IGEndingPreviewScreenshotPath=' + $capturePath),
		('-IGCaptionScale=' + $case.captionScale.ToString(
			[Globalization.CultureInfo]::InvariantCulture))
	)
	if ($case.reducedMotion) {
		$arguments += '-IGReducedMotion'
	}
	if (-not [string]::IsNullOrWhiteSpace($Culture)) {
		$arguments += ('-IGCulture=' + $Culture)
	}

	# 창을 숨긴다. 떠 있는 콘솔 창을 누가 닫으면 편집기가 그대로 꺼진다.
	$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
	# Windows PowerShell 5.1은 핸들을 먼저 잡아 두지 않으면 ExitCode를 비워 둔다.
	$null = $process.Handle
	if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
		$process.Kill()
		throw "Ending preview timed out: $($case.name)"
	}
	if ($process.ExitCode -ne 0) {
		throw "Ending preview exited with code $($process.ExitCode): $($case.name)"
	}
	if (-not (Test-Path -LiteralPath $capturePath -PathType Leaf)) {
		throw "Ending preview capture is missing: $capturePath"
	}
	$logText = Get-Content -Raw -Encoding UTF8 -LiteralPath $logPath
	if (-not $logText.Contains('MISSINGFLOOR_ENDING_PREVIEW PASS')) {
		throw "Ending preview did not report PASS: $logPath"
	}
	$dimensions = Get-PngDimensions -Path $capturePath
	if ($dimensions.width -ne $case.width -or $dimensions.height -ne $case.height) {
		throw (
			"Ending preview dimensions differ: {0} expected={1}x{2} actual={3}x{4}" -f
			$case.name, $case.width, $case.height,
			$dimensions.width, $dimensions.height)
	}
	Write-Host (
		'MISSINGFLOOR_ENDING_PREVIEW CASE PASS ' +
		"name=$($case.name) path=$capturePath")
}

Write-Host (
	'MISSINGFLOOR_ENDING_PREVIEW PASS cases=2 output=' + $outputRoot) `
	-ForegroundColor Green

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
