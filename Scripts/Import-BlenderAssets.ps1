[CmdletBinding()]
param([string[]]$Only = @(), [switch]$Build)

# 검증한 ASCII 연결 경로로 원본 프로젝트에 직접 반입한다.
# 메시 하나를 바꿀 때도 Content 전체를 ArtImport에 복제하던 경로는 쓰지 않는다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
. (Join-Path $PSScriptRoot 'Resolve-AsciiWorkspace.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
$scope = Enter-IGArtifactScope -ProjectRoot $projectRoot -Name 'Import-BlenderAssets.ps1' -Parameters $PSBoundParameters
$succeeded = $false
$oldEnvironment = @{}
try {
    $Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
    if ($Build) { & (Join-Path $PSScriptRoot 'Build-BlenderAssets.ps1') -Only $Only }
    # 다른 반입 스크립트가 쓰는 콘텐츠 전용 프로젝트는 작은 설정 파일만 준비한다.
    # 이 호환 경로에 원본 Content나 Blender 폴더 전체를 복제하지 않는다.
    $legacyRoot = Join-Path $env:LOCALAPPDATA 'IndieGame/ArtImport'
    $legacyProject = Join-Path $legacyRoot 'ArtImport.uproject'
    if (-not (Test-Path -LiteralPath $legacyProject)) {
        foreach ($part in @('Config', 'Content/Meshes', 'Content/Prototype/Materials', 'Content/Prototype/Textures', 'Scripts', 'Saved/Logs')) {
            $null = New-Item -ItemType Directory -Path (Join-Path $legacyRoot $part) -Force
        }
        $projectInfo = Get-Content -LiteralPath (Join-Path $projectRoot 'IndieGame.uproject') -Raw -Encoding UTF8 | ConvertFrom-Json
        @{
            FileVersion = 3
            EngineAssociation = $projectInfo.EngineAssociation
            Plugins = @(
                @{ Name = 'PythonScriptPlugin'; Enabled = $true },
                @{ Name = 'EditorScriptingUtilities'; Enabled = $true },
                @{ Name = 'GeometryScripting'; Enabled = $true }
            )
        } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $legacyProject -Encoding UTF8
    }
    $workspace = Resolve-IGAsciiWorkspace -ProjectRoot $projectRoot
    $sourceRoot = Join-Path $workspace 'Content/SourceArt/Blender'
    $manifests = @(Get-ChildItem -LiteralPath $sourceRoot -Directory | ForEach-Object {
        if ($Only.Count -and $Only -notcontains $_.Name) { return }
        $path = Join-Path $_.FullName 'manifest.json'
        if (Test-Path -LiteralPath $path) { Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json }
    })
    if (-not $manifests.Count) { throw '반입할 Blender 에셋이 없습니다.' }
    foreach ($name in $Only) {
        if ($manifests.name -notcontains $name) { throw "에셋 목록에 없습니다: $name" }
    }
    $project = Join-Path $workspace 'IndieGame.uproject'
    $editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $project -Commandlet
    $runRoot = Join-Path $projectRoot ('Saved/Validation/BlenderImport-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    Add-IGArtifactDirectory -Scope $scope -Directory $runRoot -Variant 'Import'
    $log = Join-Path $runRoot 'import.log'
    foreach ($key in @('IG_BLENDER_SOURCE', 'IG_ONLY', 'IG_BOUNDS_OUT')) {
        $oldEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    }
    $env:IG_BLENDER_SOURCE = $sourceRoot
    $env:IG_ONLY = $Only -join ','
    $env:IG_BOUNDS_OUT = Join-Path $workspace 'Docs/mesh_bounds.json'
    $arguments = @(
        ('"{0}"' -f $project), '-unattended', '-nop4', '-nosplash', '-nullrhi', '-nosound', '-RenderOffscreen',
        '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities', ('"-abslog={0}"' -f $log),
        ('"-ExecutePythonScript={0}"' -f (Join-Path $workspace 'Scripts/import_blender_assets.py'))
    )
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $null = $process.Handle
    if (-not $process.WaitForExit(900000)) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction Stop }
        throw "Blender 에셋 반입 시간 초과: $log"
    }
    if ($process.ExitCode -ne 0 -or -not (Select-String -LiteralPath $log -Pattern 'BLENDER_IMPORT PASS assets=')) {
        Get-Content -LiteralPath $log -Tail 25 | Write-Host
        throw "Blender 에셋 반입 실패: $log"
    }
    foreach ($manifest in $manifests) {
        $expected = @("Content/Meshes/$($manifest.name).uasset")
        $roles = @($manifest.textures.PSObject.Properties | ForEach-Object Name)
        if ($roles.Count) { $expected += "Content/Prototype/Materials/MI_$($manifest.name.Substring(3)).uasset" }
        foreach ($role in $roles) { $expected += "Content/Prototype/Textures/T_$($manifest.name.Substring(3))_$role.uasset" }
        foreach ($relative in $expected) {
            if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $relative) -PathType Leaf)) {
                throw "반입한 파일을 찾지 못했습니다: $relative"
            }
        }
    }
    Select-String -LiteralPath $log -Pattern 'BLENDER_IMPORT PASS' | ForEach-Object { Write-Host $_.Line }
    $succeeded = $true
}
finally {
    foreach ($key in $oldEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($key, $oldEnvironment[$key], 'Process')
    }
    Complete-IGArtifactScope -Scope $scope -Succeeded $succeeded
}
