[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$retailRoot = Split-Path -Parent $PSScriptRoot
$retailImport = Join-Path $env:LOCALAPPDATA 'IndieGame/ArtImport'
$retailProject = Join-Path $retailImport 'ArtImport.uproject'
if (-not (Test-Path -LiteralPath $retailProject)) {
    throw '먼저 Import-BlenderAssets.ps1로 에디터 반입 작업 폴더를 만들어야 한다.'
}
# 메시 반입과 같은 작업 폴더를 쓰므로 두 반입 스크립트를 동시에 실행하지 않는다.
foreach ($relative in @('Content/Prototype/Materials', 'Content/Prototype/Textures', 'Content/SourceArt/Atlas')) {
    $destination = Join-Path $retailImport $relative
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -Path (Join-Path $retailRoot "$relative/*") -Destination $destination -Recurse -Force
}
Copy-Item -Path (Join-Path $retailRoot 'Content/SourceArt/*.png') -Destination (Join-Path $retailImport 'Content/SourceArt') -Force
foreach ($script in @('import_retail_refresh.py', 'import_texture_atlas.py', 'texture_atlas_contract.py', 'create_textured_materials.py', 'retail_surface_contract.py')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination (Join-Path $retailImport "Scripts/$script") -Force
}
$retailEditor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath (Join-Path $retailRoot 'IndieGame.uproject') -Commandlet
$retailLog = Join-Path $retailImport 'Saved/Logs/RetailRefresh.log'
& $retailEditor $retailProject -unattended -nosplash -nullrhi -nosound -RenderOffscreen "-ExecutePythonScript=$retailImport/Scripts/import_retail_refresh.py" "-abslog=$retailLog"
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $retailLog -Pattern 'RETAIL_REFRESH PASS') -or (Select-String -LiteralPath $retailLog -Pattern 'LogPython: Error|Failed to compile Material')) {
    throw "매장 재질 반입 실패: $retailLog"
}
foreach ($relative in @('Content/Prototype/Materials', 'Content/Prototype/Textures')) {
    Copy-Item -Path (Join-Path $retailImport "$relative/*") -Destination (Join-Path $retailRoot $relative) -Recurse -Force
}
Write-Host "RETAIL_REFRESH_IMPORT PASS log=$retailLog"
