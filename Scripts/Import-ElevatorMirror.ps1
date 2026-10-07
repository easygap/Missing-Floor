[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

# 승강기 거울 재질을 콘텐츠 전용 반입 프로젝트에서 만들고 저장소로 가져온다.
# 메시 반입과 같은 작업 폴더를 쓰므로 Import-BlenderAssets.ps1과 동시에 돌리지 않는다.
$root = Split-Path -Parent $PSScriptRoot
$import = Join-Path $env:LOCALAPPDATA 'IndieGame/ArtImport'
$project = Join-Path $import 'ArtImport.uproject'
if (-not (Test-Path -LiteralPath $project)) {
    throw '먼저 Import-BlenderAssets.ps1로 에디터 반입 작업 폴더를 만들어야 한다.'
}
foreach ($script in @('import_elevator_mirror.py', 'create_textured_materials.py', 'texture_atlas_contract.py', 'retail_surface_contract.py')) {
    $source = Join-Path $PSScriptRoot $script
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $import "Scripts/$script") -Force
    }
}
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath (Join-Path $root 'IndieGame.uproject') -Commandlet
$log = Join-Path $import 'Saved/Logs/ElevatorMirror.log'
& $editor $project -unattended -nosplash -nullrhi -nosound -RenderOffscreen "-ExecutePythonScript=$import/Scripts/import_elevator_mirror.py" "-abslog=$log"
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -Pattern 'ELEVATOR_MIRROR PASS') -or (Select-String -LiteralPath $log -Pattern 'LogPython: Error|Failed to compile Material')) {
    throw "승강기 거울 재질 반입 실패: $log"
}
Copy-Item -LiteralPath (Join-Path $import 'Content/Prototype/Materials/M_ElevatorMirror.uasset') -Destination (Join-Path $root 'Content/Prototype/Materials/M_ElevatorMirror.uasset') -Force
Write-Host "ELEVATOR_MIRROR_IMPORT PASS log=$log"
