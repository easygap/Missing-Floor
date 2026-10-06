[CmdletBinding()]
param()
# 공용부 벽·천장·석재 바닥 재질에 때를 입힌다(build_grime_masks.py → retail_surface_contract.author).
# 설비 재질 전체를 다시 만드는 Build-UtilityMaterials.ps1보다 좁게, 여섯 재질과 마스크 셋만 되가져온다.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $env:LOCALAPPDATA 'IndieGame/ArtImport'
if (-not (Get-ChildItem -LiteralPath $stage -Filter '*.uproject' -ErrorAction SilentlyContinue)) {
	& (Join-Path $PSScriptRoot 'Import-BlenderAssets.ps1') -Only 'SM_BoothMonitor'
}
$project = (Get-ChildItem -LiteralPath $stage -Filter '*.uproject' | Select-Object -First 1).FullName
$materials = @('M_Stucco_X', 'M_Stucco_Y', 'M_StuccoCeil', 'M_StuccoDado_X', 'M_GraniteTile_XY')
$masks = @('WallGrime_M', 'CeilingStain_M', 'FloorGrime_M')

& python (Join-Path $PSScriptRoot 'build_grime_masks.py')
if ($LASTEXITCODE -ne 0) { throw '때 마스크 생성 실패' }

$stageGrime = Join-Path $stage 'Content/SourceArt/Grime'
$stageAi = Join-Path $stage 'Content/SourceArt/AI'
New-Item -ItemType Directory -Force -Path $stageGrime, $stageAi, (Join-Path $stage 'Content/Prototype/Materials') | Out-Null
foreach ($name in $masks) {
	Copy-Item -LiteralPath (Join-Path $root "Content/SourceArt/Grime/$name.png") -Destination $stageGrime -Force
}
foreach ($name in @('LandingPaint_20260915.png', 'PocheonGranite_20260915.png')) {
	Copy-Item -LiteralPath (Join-Path $root "Content/SourceArt/AI/$name") -Destination $stageAi -Force
}
foreach ($name in $materials) {
	Copy-Item -LiteralPath (Join-Path $root "Content/Prototype/Materials/$name.uasset") -Destination (Join-Path $stage 'Content/Prototype/Materials') -Force
}
foreach ($script in @('retail_surface_contract.py', 'build_grime_materials.py')) {
	Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination (Join-Path $stage "Scripts/$script") -Force
}

$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $project -Commandlet
$log = Join-Path $root 'Saved/Logs/GrimeMaterials.log'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $log) | Out-Null
& $editor $project -unattended -nop4 -nosplash -nullrhi -nosound -RenderOffscreen -stdout -FullStdOutLogOutput "-abslog=$log" "-ExecutePythonScript=$(Join-Path $stage 'Scripts/build_grime_materials.py')" | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -Pattern 'GRIME_MATERIALS PASS')) {
	throw "때 재질 생성 실패: $log"
}
foreach ($name in $materials) {
	Copy-Item -LiteralPath (Join-Path $stage "Content/Prototype/Materials/$name.uasset") -Destination (Join-Path $root 'Content/Prototype/Materials') -Force
}
foreach ($name in $masks) {
	Copy-Item -LiteralPath (Join-Path $stage "Content/Prototype/Textures/T_$name.uasset") -Destination (Join-Path $root 'Content/Prototype/Textures') -Force
}
Select-String -LiteralPath $log -Pattern 'GRIME_MATERIALS PASS' | ForEach-Object { Write-Host $_.Line }
