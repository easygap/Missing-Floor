[CmdletBinding()]
param()

# 에필로그 원본이 있어도 반입을 빼먹으면 엔딩은 글만 나온다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$importRoot = Join-Path $env:LOCALAPPDATA 'IndieGame/ArtImport'
$projectPath = Join-Path $importRoot 'ArtImport.uproject'
if (-not (Test-Path -LiteralPath $projectPath)) {
	throw 'Import-BlenderAssets.ps1로 콘텐츠 반입 프로젝트를 먼저 준비해야 합니다.'
}
$source = Join-Path $importRoot 'Content/SourceArt'
New-Item -ItemType Directory -Force -Path $source | Out-Null
$names = @('T_TitleBackground_D', 'T_EpilogueWorkshop_D', 'T_EpilogueAutumn_D', 'T_EpilogueServiceBay_D')
foreach ($name in $names) {
	Copy-Item -LiteralPath (Join-Path $projectRoot "Content/SourceArt/$name.png") -Destination $source -Force
}
$scriptPath = Join-Path $importRoot 'Scripts/generate_surface_textures.py'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'generate_surface_textures.py') -Destination $scriptPath -Force
$logPath = Join-Path $importRoot 'Saved/Logs/EndingStillsImport.log'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$env:IG_FRONTEND_UI_ONLY = '1'
try {
	& $editor $projectPath -unattended -nullrhi -nosound -nosplash -RenderOffscreen "-abslog=$logPath" "-ExecutePythonScript=$scriptPath" | Out-Null
	if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'Imported 4 textures')) {
		throw "엔딩 이미지 반입에 실패했습니다: $logPath"
	}
} finally { Remove-Item Env:IG_FRONTEND_UI_ONLY -ErrorAction SilentlyContinue }
$destination = Join-Path $projectRoot 'Content/UI/Textures'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach ($name in $names) {
	Copy-Item -LiteralPath (Join-Path $importRoot "Content/UI/Textures/$name.uasset") -Destination $destination -Force
}
Write-Host 'ENDING_STILLS_IMPORT PASS assets=4'
