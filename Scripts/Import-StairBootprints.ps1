[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

# 계단참 젖은 발자국 마스크와 데칼 재질을 콘텐츠 전용 반입 프로젝트에서 만들고 저장소로 가져온다.
# 메시 반입과 같은 작업 폴더를 쓰므로 Import-BlenderAssets.ps1과 동시에 돌리지 않는다.
$root = Split-Path -Parent $PSScriptRoot
$import = Join-Path $env:LOCALAPPDATA 'IndieGame/ArtImport'
$project = Join-Path $import 'ArtImport.uproject'
if (-not (Test-Path -LiteralPath $project)) {
    throw '먼저 Import-BlenderAssets.ps1로 에디터 반입 작업 폴더를 만들어야 한다.'
}
foreach ($script in @('import_stair_bootprints.py', 'create_textured_materials.py', 'texture_atlas_contract.py', 'retail_surface_contract.py')) {
    $source = Join-Path $PSScriptRoot $script
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $import "Scripts/$script") -Force
    }
}
$staging = Join-Path $import 'SourceArt'
New-Item -ItemType Directory -Force -Path $staging | Out-Null
$png = Join-Path $staging 'T_StairWetBootprints_M.png'
Copy-Item -LiteralPath (Join-Path $root 'Content/SourceArt/T_StairWetBootprints_M.png') -Destination $png -Force
$env:IG_BOOTPRINTS_PNG = $png
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath (Join-Path $root 'IndieGame.uproject') -Commandlet
$log = Join-Path $import 'Saved/Logs/StairBootprints.log'
& $editor $project -unattended -nosplash -nullrhi -nosound -RenderOffscreen "-ExecutePythonScript=$import/Scripts/import_stair_bootprints.py" "-abslog=$log"
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -Pattern 'STAIR_BOOTPRINTS PASS') -or (Select-String -LiteralPath $log -Pattern 'LogPython: Error|Failed to compile Material')) {
    throw "계단 발자국 반입 실패: $log"
}
Copy-Item -LiteralPath (Join-Path $import 'Content/Prototype/Textures/T_StairWetBootprints_M.uasset') -Destination (Join-Path $root 'Content/Prototype/Textures/T_StairWetBootprints_M.uasset') -Force
Copy-Item -LiteralPath (Join-Path $import 'Content/Prototype/Materials/M_StairWetBootprints.uasset') -Destination (Join-Path $root 'Content/Prototype/Materials/M_StairWetBootprints.uasset') -Force
Write-Host "STAIR_BOOTPRINTS_IMPORT PASS log=$log"
