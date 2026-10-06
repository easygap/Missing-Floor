[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$pressureRoot = Split-Path -Parent $PSScriptRoot
$pressureStage = Join-Path $env:LOCALAPPDATA 'IndieGame/ArtImport'
$pressureProject = Join-Path $pressureStage 'ArtImport.uproject'
if (-not (Test-Path -LiteralPath $pressureProject)) {
    & (Join-Path $PSScriptRoot 'Import-BlenderAssets.ps1') -Only 'SM_TuningHammer'
}
# Blender 반입과 같은 프로젝트를 사용하므로 순서대로 실행한다.
$sources = Join-Path $pressureStage 'PressureSources'
New-Item -ItemType Directory -Force -Path $sources | Out-Null
Copy-Item -LiteralPath (Join-Path $pressureRoot 'Content/SourceArt/AI/AnnexPressureMask_20260916.png') -Destination $sources -Force
foreach ($name in @('import_pressure_marks.py', 'create_textured_materials.py', 'texture_atlas_contract.py')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $pressureStage "Scripts/$name") -Force
}
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $pressureProject -Commandlet
$log = Join-Path $pressureRoot 'Saved/Logs/PressureMarks.log'
& $editor $pressureProject -unattended -nop4 -nosplash -nullrhi -nosound -RenderOffscreen "-abslog=$log" "-ExecutePythonScript=$pressureStage/Scripts/import_pressure_marks.py" | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -Pattern 'PRESSURE_MARKS PASS') -or
    (Select-String -LiteralPath $log -Pattern 'LogPython: Error|Failed to compile Material')) { throw "압흔 반입 실패: $log" }
foreach ($file in @('Materials/M_AnnexPressure.uasset', 'Textures/T_AnnexPressure_M.uasset')) {
    Copy-Item -LiteralPath (Join-Path $pressureStage "Content/Prototype/$file") -Destination (Join-Path $pressureRoot "Content/Prototype/$file") -Force
}
Write-Host 'PRESSURE_MARKS_IMPORT PASS'
