[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$utilityRoot = Split-Path -Parent $PSScriptRoot
$utilityStage = Join-Path $env:LOCALAPPDATA 'IndieGame/ArtImport'
$utilityProject = Join-Path $utilityStage 'ArtImport.uproject'
if (-not (Test-Path -LiteralPath $utilityProject)) {
    # 콘텐츠 전용 프로젝트 생성과 기존 재질 동기화를 맡긴다.
    & (Join-Path $PSScriptRoot 'Import-BlenderAssets.ps1') -Only 'SM_BoothMonitor'
}
# Import-BlenderAssets.ps1이 사용하는 콘텐츠 프로젝트의 이름을 그대로 찾는다.
$utilityProject = (Get-ChildItem -LiteralPath $utilityStage -Filter '*.uproject' | Select-Object -First 1).FullName
foreach ($part in @('Content/Prototype/Textures', 'Content/Prototype/Materials')) {
    & robocopy.exe (Join-Path $utilityRoot $part) (Join-Path $utilityStage $part) /E /R:2 /W:1 /NFL /NDL /NP /NJH /NJS | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "설비 재질 동기화 실패: $part" }
}
$utilitySources = Join-Path $utilityStage 'UtilitySources'
New-Item -ItemType Directory -Force -Path $utilitySources | Out-Null
Copy-Item -LiteralPath (Join-Path $utilityRoot 'Content/SourceArt/AI/TankSatinSteel_20260915.png') -Destination $utilitySources -Force
Copy-Item -LiteralPath (Join-Path $utilityRoot 'Content/SourceArt/AI/KoreanBrick_20260916.png') -Destination $utilitySources -Force
foreach ($mesh in @('SM_BottleCap', 'SM_WaterBottle')) {
    Copy-Item -LiteralPath (Join-Path $utilityRoot "Content/Meshes/$mesh.uasset") -Destination (Join-Path $utilityStage 'Content/Meshes') -Force
}
foreach ($script in @('mesh_lod_contract.py', 'apply_small_prop_lods.py')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination (Join-Path $utilityStage "Scripts/$script") -Force
}
& python (Join-Path $PSScriptRoot 'build_utility_prints.py')
if ($LASTEXITCODE -ne 0) { throw '검침창 인쇄 원본 생성 실패' }
foreach ($name in @('MeterCounter', 'MeterLabel', 'BoothAgentNote', 'BoothReceipts', 'BoothCalendar', 'LobbyWaterNotice', 'LobbyContactNotice', 'LobbyMeterSheet', 'LobbyForumPrint', 'PumpProcedure')) {
    Copy-Item -LiteralPath (Join-Path $utilityRoot "Content/SourceArt/UtilityPrints/$name.png") -Destination $utilitySources -Force
}
$atlas = Join-Path $utilityRoot 'Content/SourceArt/Cctv/CctvStandby.png'
& python (Join-Path $PSScriptRoot 'build_cctv_standby_atlas.py')
if ($LASTEXITCODE -ne 0) { throw '실제 맵 CCTV 화면 생성 실패' }
Copy-Item -LiteralPath $atlas -Destination $utilitySources -Force
$graniteSources = Join-Path $utilityStage 'Content/SourceArt/AI'
New-Item -ItemType Directory -Force -Path $graniteSources | Out-Null
Copy-Item -LiteralPath (Join-Path $utilityRoot 'Content/SourceArt/AI/PocheonGranite_20260915.png') -Destination $graniteSources -Force
Copy-Item -LiteralPath (Join-Path $utilityRoot 'Content/SourceArt/AI/LandingPaint_20260915.png') -Destination $graniteSources -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'retail_surface_contract.py') -Destination (Join-Path $utilityStage 'Scripts/retail_surface_contract.py') -Force
Copy-Item -LiteralPath (Join-Path $utilityRoot 'Content/SourceArt/AI/ApartmentNightVista_20260916.png') -Destination $utilitySources -Force
Copy-Item -LiteralPath (Join-Path $utilityRoot 'Content/SourceArt/AI/ApartmentDawnVista.png') -Destination $utilitySources -Force
# 밤 원경의 불빛 마스크는 build_night_view_masks.py가 만든다. 옥상 사방 원경은 네 장을 한 띠로 묶었다.
& python (Join-Path $PSScriptRoot 'build_night_view_masks.py')
if ($LASTEXITCODE -ne 0) { throw '밤 원경 불빛 마스크 생성 실패' }
foreach ($name in @('NightSkyline_D', 'NightSkyline_M', 'ApartmentNightVista_M')) {
    Copy-Item -LiteralPath (Join-Path $utilityRoot "Content/SourceArt/NightView/$name.png") -Destination $utilitySources -Force
}
# 5층 옥탑 외벽 패널은 위아래로도 이어지게 다듬은 사본을 쓴다.
& python (Join-Path $PSScriptRoot 'build_annex_panel_texture.py')
if ($LASTEXITCODE -ne 0) { throw '옥탑 패널 텍스처 가공 실패' }
Copy-Item -LiteralPath (Join-Path $utilityRoot 'Content/SourceArt/AnnexPanel/AnnexSandwichPanel_D.png') -Destination $utilitySources -Force
# 골목 이웃 창 뒤의 방 사진 네 장을 한 장으로 묶는다.
& python (Join-Path $PSScriptRoot 'build_room_interior_atlas.py')
if ($LASTEXITCODE -ne 0) { throw '방 사진 아틀라스 생성 실패' }
Copy-Item -LiteralPath (Join-Path $utilityRoot 'Content/SourceArt/RoomInterior/RoomInteriors_D.png') -Destination $utilitySources -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build_interior_materials.py') -Destination (Join-Path $utilityStage 'Scripts/build_interior_materials.py') -Force
$utilityScript = Join-Path $utilityStage 'Scripts/build_utility_materials.py'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build_utility_materials.py') -Destination $utilityScript -Force
$utilityEditor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $utilityProject -Commandlet
$utilityLog = Join-Path $utilityRoot 'Saved/Logs/UtilityMaterials.log'
& $utilityEditor $utilityProject -unattended -nop4 -nosplash -nullrhi -nosound -RenderOffscreen -stdout -FullStdOutLogOutput "-abslog=$utilityLog" "-ExecutePythonScript=$utilityScript" | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $utilityLog -Pattern 'UTILITY_MATERIALS PASS')) {
    throw "설비 재질 생성 실패: $utilityLog"
}
foreach ($name in @('M_ApartmentNightGlass', 'M_NightSkyline', 'M_NightSkyGlow', 'M_AnnexPanel', 'M_RoomInterior', 'M_PumpIndicator', 'M_Stucco_X', 'M_Stucco_Y', 'M_StuccoCeil', 'M_StuccoDado_X', 'M_UtilityStreetBrick', 'M_UtilityVillaBrick', 'M_GraniteTile_XY', 'M_UtilityTankSteel', 'M_UtilityFoundation', 'M_UtilityGraniteCladding', 'M_UtilityConcreteDark', 'M_CctvStandby', 'M_UtilityMeterCounter', 'M_UtilityMeterLabel', 'M_BoothAgentNote', 'M_BoothReceipts', 'M_BoothCalendar', 'M_LobbyWaterNotice', 'M_LobbyContactNotice', 'M_LobbyMeterSheet', 'M_LobbyForumPrint', 'M_PumpProcedure')) {
    Copy-Item -LiteralPath (Join-Path $utilityStage "Content/Prototype/Materials/$name.uasset") -Destination (Join-Path $utilityRoot 'Content/Prototype/Materials') -Force
}
foreach ($name in @('T_RoomInteriors_D', 'T_AnnexSandwichPanel_D', 'T_ApartmentNightVista_D', 'T_ApartmentNightVista_M', 'T_NightSkyline_D', 'T_NightSkyline_M', 'T_LandingPaint_20260915_D', 'T_UtilityTankSteel_D', 'T_CctvStandby_D', 'T_UtilityMeterCounter_D', 'T_UtilityMeterLabel_D', 'T_BoothAgentNote_D', 'T_BoothReceipts_D', 'T_BoothCalendar_D', 'T_LobbyWaterNotice_D', 'T_LobbyContactNotice_D', 'T_LobbyMeterSheet_D', 'T_LobbyForumPrint_D', 'T_PumpProcedure_D')) {
    $source = Join-Path $utilityStage "Content/Prototype/Textures/$name.uasset"
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $utilityRoot 'Content/Prototype/Textures') -Force }
}
Copy-Item -LiteralPath (Join-Path $utilityStage 'Content/Prototype/Textures/T_KoreanBrick_20260916_D.uasset') -Destination (Join-Path $utilityRoot 'Content/Prototype/Textures') -Force
Copy-Item -LiteralPath (Join-Path $utilityStage 'Content/Prototype/Textures/T_ApartmentDawnVista_D.uasset') -Destination (Join-Path $utilityRoot 'Content/Prototype/Textures') -Force
foreach ($mesh in @('SM_BottleCap', 'SM_WaterBottle')) {
    Copy-Item -LiteralPath (Join-Path $utilityStage "Content/Meshes/$mesh.uasset") -Destination (Join-Path $utilityRoot 'Content/Meshes') -Force
}
Select-String -LiteralPath $utilityLog -Pattern 'UTILITY_MATERIALS PASS' | ForEach-Object { Write-Host $_.Line }
