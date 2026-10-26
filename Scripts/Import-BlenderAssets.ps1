[CmdletBinding()]
param(
	# 에셋 이름(SM_...). 비우면 Content/SourceArt/Blender의 manifest 전부.
	[string[]]$Only = @(),
	# Blender 빌더까지 먼저 돌린다.
	[switch]$Build
)

# Blender에서 구운 에셋을 UE 에셋으로 만들어 저장소 Content에 넣는다.
#
# 에디터는 게임 모듈이 없는 콘텐츠 전용 프로젝트에서 돈다. 2026-08-25부터
# Smart App Control이 이 저장소의 UnrealEditor-IndieGame.dll을 막아 원래
# 프로젝트로는 에디터를 못 여는데, 메시·텍스처·재질 에셋은 게임 코드를
# 참조하지 않으므로 어느 프로젝트에서 만들어도 같은 파일이다. 경로에 한글이
# 없는 %LOCALAPPDATA%\IndieGame\ArtImport에 그 프로젝트를 만들고, 저장소의
# Content 세 폴더를 거기 미러한 뒤 반입하고, 결과를 되가져온다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $projectRoot 'Content\SourceArt\Blender'
$importRoot = Join-Path $env:LOCALAPPDATA 'IndieGame\ArtImport'

if ($Build) {
	& (Join-Path $PSScriptRoot 'Build-BlenderAssets.ps1') -Only $Only
}

if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
	throw "Blender 산출물 폴더가 없다: $sourceRoot (먼저 Build-BlenderAssets.ps1)"
}

# --- 콘텐츠 전용 프로젝트 ----------------------------------------------------
New-Item -ItemType Directory -Force -Path (Join-Path $importRoot 'Config') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $importRoot 'Content') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $importRoot 'Scripts') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $importRoot 'Saved\Logs') | Out-Null

$uproject = Join-Path $importRoot 'ArtImport.uproject'
@'
{
	"FileVersion": 3,
	"EngineAssociation": "5.8",
	"Category": "Tools",
	"Description": "Content-only import project for Indie_Game art assets. No game module, so Smart App Control has nothing to block.",
	"Plugins": [
		{ "Name": "PythonScriptPlugin", "Enabled": true },
		{ "Name": "EditorScriptingUtilities", "Enabled": true },
		{ "Name": "GeometryScripting", "Enabled": true }
	]
}
'@ | Set-Content -LiteralPath $uproject -Encoding UTF8

@'
[/Script/EngineSettings.GameMapsSettings]
EditorStartupMap=/Engine/Maps/Templates/OpenWorld
GameDefaultMap=/Engine/Maps/Templates/OpenWorld

[/Script/Engine.RendererSettings]
r.DynamicGlobalIlluminationMethod=1
r.ReflectionMethod=1
r.Shadow.Virtual.Enable=1
r.GenerateMeshDistanceFields=True
r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=True

[/Script/WindowsTargetPlatform.WindowsTargetSettings]
DefaultGraphicsRHI=DefaultGraphicsRHI_DX12
-D3D12TargetedShaderFormats=PCD3D_SM5
+D3D12TargetedShaderFormats=PCD3D_SM6
'@ | Set-Content -LiteralPath (Join-Path $importRoot 'Config\DefaultEngine.ini') -Encoding UTF8

function Invoke-Mirror {
	param([string]$Source, [string]$Destination, [switch]$Mirror)
	New-Item -ItemType Directory -Force -Path $Destination | Out-Null
	$arguments = @($Source, $Destination)
	$arguments += if ($Mirror) { '/MIR' } else { '/E' }
	$arguments += @('/COPY:DAT', '/R:2', '/W:1', '/XJ', '/NFL', '/NDL', '/NP', '/NJH', '/NJS')
	& robocopy.exe @arguments | Out-Null
	if ($LASTEXITCODE -ge 8) {
		throw "robocopy failed ($LASTEXITCODE): $Source -> $Destination"
	}
}

# 저장소 → 임포트 프로젝트. 기존 메시·텍스처·재질이 있어야 M_Glass 참조가
# 풀리고 바운드 JSON에 전체 메시가 들어간다.
foreach ($relative in @('Content\Meshes', 'Content\Prototype\Textures', 'Content\Prototype\Materials')) {
	Invoke-Mirror -Source (Join-Path $projectRoot $relative) -Destination (Join-Path $importRoot $relative) -Mirror
}
# Blender 산출물과 스크립트도 ASCII 경로로 옮긴다. 에디터 커맨드라인이 한글
# 경로를 토막 내는 일이 있었다.
Invoke-Mirror -Source $sourceRoot -Destination (Join-Path $importRoot 'Import') -Mirror
foreach ($script in @('import_blender_assets.py', 'mesh_lod_contract.py', 'create_textured_materials.py', 'texture_atlas_contract.py')) {
	Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination (Join-Path $importRoot 'Scripts') -Force
}

$editor = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $uproject -Commandlet
if (-not $editor -or -not (Test-Path -LiteralPath $editor)) {
	throw "UnrealEditor-Cmd.exe를 찾지 못했다: $editor"
}

$boundsOut = Join-Path $importRoot 'Saved\mesh_bounds.json'
$logPath = Join-Path $importRoot ('Saved\Logs\BlenderImport_{0}.log' -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
$env:IG_BLENDER_SOURCE = Join-Path $importRoot 'Import'
$env:IG_ONLY = ($Only -join ',')
$env:IG_BOUNDS_OUT = $boundsOut
try {
	& $editor $uproject -unattended -nop4 -nosplash -nullrhi -nosound -RenderOffscreen -stdout -FullStdOutLogOutput `
		"-abslog=$logPath" "-ExecutePythonScript=$(Join-Path $importRoot 'Scripts\import_blender_assets.py')" | Out-Null
	$editorExit = $LASTEXITCODE
}
finally {
	Remove-Item Env:IG_BLENDER_SOURCE, Env:IG_ONLY, Env:IG_BOUNDS_OUT -ErrorAction SilentlyContinue
}
$pass = Select-String -LiteralPath $logPath -Pattern 'BLENDER_IMPORT PASS assets=\d+' | Select-Object -Last 1
if ($editorExit -ne 0 -or -not $pass) {
	Select-String -LiteralPath $logPath -Pattern 'LogPython: Error|Traceback|BLENDER_IMPORT' | Select-Object -Last 30 | ForEach-Object { Write-Host $_.Line }
	throw "Blender 에셋 반입 실패 (exit $editorExit). 로그: $logPath"
}

# 임포트 프로젝트 → 저장소. 새 파일과 바뀐 파일만 넘어온다.
foreach ($relative in @('Content\Meshes', 'Content\Prototype\Textures', 'Content\Prototype\Materials')) {
	Invoke-Mirror -Source (Join-Path $importRoot $relative) -Destination (Join-Path $projectRoot $relative)
}
if (Test-Path -LiteralPath $boundsOut) {
	Copy-Item -LiteralPath $boundsOut -Destination (Join-Path $projectRoot 'Docs\mesh_bounds.json') -Force
}

# 계약: manifest에 적힌 에셋이 실제로 저장소에 도착했는지 본다.
$missing = @()
foreach ($manifestPath in Get-ChildItem -LiteralPath $sourceRoot -Recurse -Filter 'manifest.json') {
	$manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath.FullName | ConvertFrom-Json
	if ($Only.Count -gt 0 -and $Only -notcontains $manifest.name) { continue }
	$expected = @("Content\Meshes\$($manifest.name).uasset")
	# 구운 텍스처가 없는 메시(raw_uv)는 인스턴스도 없다. 재질은 씬이 UV0에 준다.
	$roles = @($manifest.textures.PSObject.Properties | ForEach-Object { $_.Name })
	if ($roles.Count -gt 0) {
		$expected += "Content\Prototype\Materials\MI_$($manifest.name.Substring(3)).uasset"
	}
	foreach ($role in $roles) {
		$expected += "Content\Prototype\Textures\T_$($manifest.name.Substring(3))_$role.uasset"
	}
	foreach ($relative in $expected) {
		if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $relative) -PathType Leaf)) {
			$missing += $relative
		}
	}
}
if ($missing.Count -gt 0) {
	throw "반입 뒤 저장소에 없는 파일: $($missing -join ', ')"
}
Write-Host ('BLENDER_IMPORT PASS {0}' -f $pass.Line.Substring($pass.Line.IndexOf('assets=')))
