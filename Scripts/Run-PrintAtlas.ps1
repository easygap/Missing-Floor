#requires -Version 5.1
<#
.SYNOPSIS
	Packs the print atlas, imports its pages and rebuilds the print materials.

.DESCRIPTION
	Build-ArtAssets.ps1 already runs these three stages, but it runs everything
	else with them: meshes, surface textures, photo-prop LODs, the full uasset
	audit. When the only thing that changed is a notice, a product label or the
	packer itself, that is twenty minutes to check a two-minute change.

	This does the atlas and nothing else:

	  1. build_texture_atlas.py            pack the pages outside the editor
	  2. build_texture_atlas.py --preflight prove the editor run can work
	  3. import_texture_atlas.py           import the pages as clamped BC7
	  4. create_textured_materials.py      rebuild only the print materials
	                                       that read the atlas (IG_PRINT_ATLAS_ONLY)
	  5. validate_baked_art_assets.py      confirm what actually landed

	-WhatIf stops after the preflight, which is the part worth running before
	you commit to an editor session.

.PARAMETER WhatIf
	Pack and preflight only. Never opens the editor.

.PARAMETER SkipPack
	Use the pages already on disk. Fails the preflight if they are stale, so
	this only skips work, it cannot ship a stale atlas.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
	[switch]$SkipPack
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'IndieGame.uproject'

# --- python ---------------------------------------------------------------
$python = Get-Command 'python' -ErrorAction SilentlyContinue
if (-not $python) {
	$python = Get-Command 'python3' -ErrorAction SilentlyContinue
}
if (-not $python) {
	throw 'python was not found; the atlas is packed outside the editor.'
}

function Invoke-AtlasPython {
	param(
		[Parameter(Mandatory = $true)][string]$Script,
		[string[]]$Arguments = @(),
		[Parameter(Mandatory = $true)][string]$FailureMessage
	)

	$scriptPath = Join-Path $PSScriptRoot $Script
	& $python.Source $scriptPath @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "$FailureMessage ($LASTEXITCODE)"
	}
}

if (-not $SkipPack) {
	Write-Host 'PRINT_ATLAS packing'
	Invoke-AtlasPython `
		-Script 'build_texture_atlas.py' `
		-FailureMessage 'Print atlas packing failed'
}

Write-Host 'PRINT_ATLAS preflight'
Invoke-AtlasPython `
	-Script 'build_texture_atlas.py' `
	-Arguments @('--preflight') `
	-FailureMessage 'Print atlas preflight failed'

if (-not $PSCmdlet.ShouldProcess('Unreal editor', 'import the atlas and rebuild the print materials')) {
	Write-Host 'PRINT_ATLAS stopping after preflight (-WhatIf)'
	return
}

# --- editor ---------------------------------------------------------------
# Same resolution the art build uses: PowerShell 7 when it is there, because
# 5.1 writes the engine path back in the system code page.
# Looked up without the .exe so the name resolves on both platforms:
# Get-Command finds pwsh.exe from 'pwsh' on Windows via PATHEXT, and asking
# for 'pwsh.exe' on Linux finds nothing at all -- which produced a script
# running under PowerShell announcing that PowerShell was not found.
$powerShellCore = Get-Command 'pwsh' -CommandType Application -ErrorAction SilentlyContinue |
	Select-Object -First 1
if ($powerShellCore) {
	$resolverShell = $powerShellCore.Source
} else {
	$windowsPowerShell = Get-Command 'powershell' -CommandType Application -ErrorAction SilentlyContinue |
		Select-Object -First 1
	if (-not $windowsPowerShell) {
		throw ('No PowerShell host found on PATH to run Resolve-UnrealEditor.ps1. ' +
			'This stage needs the Unreal editor, which this project builds on Windows.')
	}
	$resolverShell = $windowsPowerShell.Source
	Write-Host 'PRINT_ATLAS pwsh not found - resolving with Windows PowerShell'
}

$resolver = Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1'
$editorOutput = & $resolverShell -NoProfile -ExecutionPolicy Bypass -File $resolver `
	-ProjectPath $projectFile -Commandlet
if ($LASTEXITCODE -ne 0 -or -not $editorOutput) {
	throw 'Could not resolve the Unreal editor for this project.'
}
# Wrap before indexing: a resolver that printed a single line would give a
# character, not the path, if this indexed the string directly.
$editorCommand = ([string](@($editorOutput)[-1])).Trim()

$logRoot = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null

$stages = @(
	@{ Script = 'import_texture_atlas.py'; Success = 'PRINT_ATLAS_IMPORT PASS' },
	@{
		Script = 'create_textured_materials.py'
		Success = '\[IndieGame\] Print atlas material update complete: \d+ materials'
		Environment = 'IG_PRINT_ATLAS_ONLY'
	},
	@{ Script = 'validate_baked_art_assets.py'; Success = 'ART_UASSET_AUDIT PASS' }
)

foreach ($stage in $stages) {
	$stageName = [string]$stage.Script
	$scriptPath = Join-Path $PSScriptRoot $stageName
	$logName = 'PrintAtlas_{0}_{1}.log' -f (
		[IO.Path]::GetFileNameWithoutExtension($stageName)),
		(Get-Date -Format 'yyyyMMdd_HHmmss_fff')
	$logPath = Join-Path $logRoot $logName

	Write-Host "PRINT_ATLAS running $stageName"
	$stageEnvironment = if ($stage.ContainsKey('Environment')) { [string]$stage.Environment } else { $null }
	if ($stageEnvironment) {
		[Environment]::SetEnvironmentVariable($stageEnvironment, '1', 'Process')
	}
	& $editorCommand `
		$projectFile `
		-unattended `
		-nop4 `
		-nosplash `
		-nullrhi `
		-nosound `
		-RenderOffscreen `
		-stdout `
		-FullStdOutLogOutput `
		"-abslog=$logPath" `
		"-ExecutePythonScript=$scriptPath"
	$editorExit = $LASTEXITCODE
	if ($stageEnvironment) {
		[Environment]::SetEnvironmentVariable($stageEnvironment, $null, 'Process')
	}

	$success = Select-String -LiteralPath $logPath -Pattern ([string]$stage.Success) `
		-ErrorAction SilentlyContinue | Select-Object -Last 1
	if ($editorExit -ne 0 -or -not $success) {
		$errorLines = @(
			Select-String -LiteralPath $logPath `
				-Pattern 'LogPython: Error|RuntimeError|AtlasContractError|Traceback' `
				-ErrorAction SilentlyContinue |
			Select-Object -Last 20 |
			ForEach-Object { $_.Line }
		)
		if ($errorLines.Count -gt 0) {
			Write-Warning ($errorLines -join [Environment]::NewLine)
		}
		throw "Print atlas stage failed ($editorExit): $stageName"
	}
}

# The import stage reports a page count of zero when it found no manifest,
# which is a pass for the art build but never what this script was run for.
$importLog = Get-ChildItem -LiteralPath $logRoot -Filter 'PrintAtlas_import_texture_atlas_*.log' |
	Sort-Object LastWriteTime | Select-Object -Last 1
if ($importLog) {
	$importedNothing = Select-String -LiteralPath $importLog.FullName `
		-Pattern 'PRINT_ATLAS_IMPORT PASS pages=0' -ErrorAction SilentlyContinue
	if ($importedNothing) {
		throw 'The editor found no atlas manifest; nothing was imported.'
	}
}

# The point of the atlas is not that the pages exist, it is that the textures
# they replaced stop being cooked. Until the rebuilt materials are on disk
# every one of them still ships and the atlas is pure cost, so prove it here
# rather than believing the material stage's own log line.
Write-Host 'PRINT_ATLAS verifying the atlassed textures left the cook'
Invoke-AtlasPython `
	-Script 'check_cook_references.py' `
	-Arguments @('--check', '--require-atlas-dropped') `
	-FailureMessage 'Atlassed textures are still reachable from the cook'

Write-Host 'PRINT_ATLAS PASS atlas packed, imported and wired into the print materials'
