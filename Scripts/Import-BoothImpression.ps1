[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$boothRoot = Split-Path -Parent $PSScriptRoot
$boothStage = Join-Path $env:LOCALAPPDATA 'IndieGame/ArtImport'
$boothProject = Join-Path $boothStage 'ArtImport.uproject'
if (-not (Test-Path -LiteralPath $boothProject)) { throw '먼저 Import-BlenderAssets.ps1로 소품을 반입해야 합니다.' }
$sources = Join-Path $boothStage 'BoothSources'
New-Item -ItemType Directory -Force -Path $sources | Out-Null
Copy-Item -LiteralPath (Join-Path $boothRoot 'Content/SourceArt/AI/ComplaintRubbing_20260917.png') -Destination $sources -Force
Copy-Item -LiteralPath (Join-Path $boothRoot 'Content/SourceArt/UtilityPrints/ComplaintImpressionMask.png') -Destination $sources -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'import_booth_impression.py') -Destination (Join-Path $boothStage 'Scripts') -Force
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $boothProject -Commandlet
$log = Join-Path $boothRoot 'Saved/Logs/BoothImpressionImport.log'
& $editor $boothProject -unattended -nop4 -nosplash -nullrhi -nosound -RenderOffscreen "-abslog=$log" "-ExecutePythonScript=$boothStage/Scripts/import_booth_impression.py" | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -Pattern 'BOOTH_IMPRESSION PASS') -or
    (Select-String -LiteralPath $log -Pattern 'LogPython: Error|Failed to compile Material')) { throw "접수철 재질 반입 실패: $log" }
foreach ($file in @('Materials/M_ComplaintImpression.uasset','Textures/T_ComplaintRubbing_D.uasset','Textures/T_ComplaintImpression_M.uasset')) {
    Copy-Item -LiteralPath (Join-Path $boothStage "Content/Prototype/$file") -Destination (Join-Path $boothRoot "Content/Prototype/$file") -Force
}
Write-Host 'BOOTH_IMPRESSION_IMPORT PASS'
