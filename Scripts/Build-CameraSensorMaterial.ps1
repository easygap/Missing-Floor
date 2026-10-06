[CmdletBinding()]
param()
# 화면 질감 후처리 재질(M_PP_CameraSensor)을 만든다. 게임 모듈이 필요 없는 재질이라
# Import-BlenderAssets.ps1이 쓰는 콘텐츠 전용 프로젝트(ArtImport)에서 만들고 되가져온다.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $env:LOCALAPPDATA 'IndieGame/ArtImport'
if (-not (Get-ChildItem -LiteralPath $stage -Filter '*.uproject' -ErrorAction SilentlyContinue)) {
	& (Join-Path $PSScriptRoot 'Import-BlenderAssets.ps1') -Only 'SM_BoothMonitor'
}
$project = (Get-ChildItem -LiteralPath $stage -Filter '*.uproject' | Select-Object -First 1).FullName
$assetName = 'M_PP_CameraSensor'
$repoAsset = Join-Path $root "Content/Prototype/Materials/$assetName.uasset"
$stageAsset = Join-Path $stage "Content/Prototype/Materials/$assetName.uasset"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $stageAsset) | Out-Null
if (Test-Path -LiteralPath $repoAsset) {
	Copy-Item -LiteralPath $repoAsset -Destination $stageAsset -Force
}
$script = Join-Path $stage 'Scripts/build_camera_sensor_material.py'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build_camera_sensor_material.py') -Destination $script -Force
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $project -Commandlet
$log = Join-Path $root 'Saved/Logs/CameraSensorMaterial.log'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $log) | Out-Null
& $editor $project -unattended -nop4 -nosplash -nullrhi -nosound -RenderOffscreen -stdout -FullStdOutLogOutput "-abslog=$log" "-ExecutePythonScript=$script" | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -Pattern 'CAMERA_SENSOR_MATERIAL PASS')) {
	throw "화면 질감 재질 생성 실패: $log"
}
Copy-Item -LiteralPath $stageAsset -Destination $repoAsset -Force
Select-String -LiteralPath $log -Pattern 'CAMERA_SENSOR_MATERIAL PASS' | ForEach-Object { Write-Host $_.Line }
