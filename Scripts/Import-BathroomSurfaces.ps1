[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

# 403호 욕실 벽·바닥 타일 텍스처와 재질을 콘텐츠 전용 반입 프로젝트에서 만들고 저장소로 가져온다.
# 메시 반입과 같은 작업 폴더를 쓰므로 Import-BlenderAssets.ps1과 동시에 돌리지 않는다.
# 먼저 python Scripts/condition_ai_tiles.py와 generate_ai_pbr_maps.py로 _D/_N/_R/_A를 만들어 둔다.
$root = Split-Path -Parent $PSScriptRoot
$import = Join-Path $env:LOCALAPPDATA 'IndieGame/ArtImport'
$project = Join-Path $import 'ArtImport.uproject'
if (-not (Test-Path -LiteralPath $project)) {
    throw '먼저 Import-BlenderAssets.ps1로 에디터 반입 작업 폴더를 만들어야 한다.'
}
foreach ($script in @('import_bathroom_surfaces.py', 'create_textured_materials.py', 'texture_atlas_contract.py', 'retail_surface_contract.py')) {
    $source = Join-Path $PSScriptRoot $script
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $import "Scripts/$script") -Force
    }
}
$staging = Join-Path $import 'SourceArt/Bathroom'
New-Item -ItemType Directory -Force -Path $staging | Out-Null
$stems = @('T_BathroomWallTile', 'T_BathroomFloorTile')
foreach ($stem in $stems) {
    foreach ($suffix in @('D', 'N', 'R', 'A')) {
        Copy-Item -LiteralPath (Join-Path $root "Content/SourceArt/${stem}_$suffix.png") -Destination (Join-Path $staging "${stem}_$suffix.png") -Force
    }
}
$env:IG_BATHROOM_SURFACES_DIR = $staging
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath (Join-Path $root 'IndieGame.uproject') -Commandlet
$log = Join-Path $import 'Saved/Logs/BathroomSurfaces.log'
& $editor $project -unattended -nosplash -nullrhi -nosound -RenderOffscreen "-ExecutePythonScript=$import/Scripts/import_bathroom_surfaces.py" "-abslog=$log"
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -Pattern 'BATHROOM_SURFACES PASS') -or (Select-String -LiteralPath $log -Pattern 'LogPython: Error|Failed to compile Material')) {
    throw "욕실 타일 반입 실패: $log"
}
foreach ($stem in $stems) {
    foreach ($suffix in @('D', 'N', 'R', 'A')) {
        Copy-Item -LiteralPath (Join-Path $import "Content/Prototype/Textures/${stem}_$suffix.uasset") -Destination (Join-Path $root "Content/Prototype/Textures/${stem}_$suffix.uasset") -Force
    }
}
foreach ($material in @('M_BathroomWallTile_X', 'M_BathroomWallTile_Y', 'M_BathroomFloorTile_XY')) {
    Copy-Item -LiteralPath (Join-Path $import "Content/Prototype/Materials/$material.uasset") -Destination (Join-Path $root "Content/Prototype/Materials/$material.uasset") -Force
}
Write-Host "BATHROOM_SURFACES_IMPORT PASS log=$log"
