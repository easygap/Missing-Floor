[CmdletBinding()]
param(
	# 소스의 NSLOCTEXT를 모아 목록과 언어별 .po를 새로 만든다.
	[switch]$Gather,
	# 번역된 .po를 목록에 들이고 언어별 .locres를 만든다.
	[switch]$Compile,
	# 이미 빌드한 미러를 그대로 쓴다.
	[switch]$NoBuild
)

# 저장소 경로에 한글이 있어서 편집기 커맨드릿은 Build-ArtAssets.ps1이 쓰는 ASCII
# 미러에서 돌린다. 코드 빌드가 미러를 저장소와 맞추고 편집기 모듈도 최신으로 만든다.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Localize-Game.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
if (-not $Gather -and -not $Compile) {
	throw '-Gather 또는 -Compile 중 하나 이상을 고르세요.'
}
$repoRoot = Split-Path -Parent $PSScriptRoot
$localizationPart = 'Content\Localization\Game'

function Get-MirrorRoot {
	$hashAlgorithm = [Security.Cryptography.SHA256]::Create()
	try {
		$hashBytes = $hashAlgorithm.ComputeHash(
			[Text.Encoding]::UTF8.GetBytes($repoRoot.ToLowerInvariant()))
	}
	finally {
		$hashAlgorithm.Dispose()
	}
	$shortHash = -join ($hashBytes[0..3] | ForEach-Object { $_.ToString('x2') })
	return Join-Path (Join-Path $env:LOCALAPPDATA 'IndieGame\AsciiBuild') "Art_$shortHash"
}

function Sync-Folder([string]$From, [string]$To) {
	New-Item -ItemType Directory -Force -Path $To | Out-Null
	& robocopy.exe $From $To /MIR /R:2 /W:1 /NFL /NDL /NP /NJH /NJS | Out-Null
	if ($LASTEXITCODE -ge 8) { throw "동기화 실패($LASTEXITCODE): $From -> $To" }
}

function Invoke-Gather([string]$Mirror, [string]$ConfigName) {
	$project = Join-Path $Mirror 'IndieGame.uproject'
	$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $project -Commandlet
	$config = Join-Path $Mirror "Config\Localization\$ConfigName"
	$log = Join-Path $repoRoot "Saved\Logs\Localize-$([IO.Path]::GetFileNameWithoutExtension($ConfigName)).log"
	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $log) | Out-Null
	& $editor $project -run=GatherText "-config=$config" -unattended -nop4 -nosplash -nullrhi -NoShaderCompile "-abslog=$log" | Out-Null
	$exit = $LASTEXITCODE
	if ($exit -ne 0) { throw "$ConfigName 실패($exit). 로그: $log" }
	Write-Host "LOCALIZE $ConfigName PASS"
}

if (-not $NoBuild) {
	# 빌드 스크립트는 실패하면 예외를 던진다. 끝난 뒤의 $LASTEXITCODE는 마지막 robocopy 값이라 보지 않는다.
	& (Join-Path $PSScriptRoot 'Build-ArtAssets.ps1') -CodeOnly
}
$mirror = Get-MirrorRoot
if (-not (Test-Path -LiteralPath (Join-Path $mirror 'IndieGame.uproject'))) {
	throw "ASCII 미러가 없습니다: $mirror. Build-ArtAssets.ps1 -CodeOnly를 먼저 돌리세요."
}
Sync-Folder (Join-Path $repoRoot 'Source') (Join-Path $mirror 'Source')
Sync-Folder (Join-Path $repoRoot 'Config') (Join-Path $mirror 'Config')
if (Test-Path -LiteralPath (Join-Path $repoRoot $localizationPart)) {
	Sync-Folder (Join-Path $repoRoot $localizationPart) (Join-Path $mirror $localizationPart)
}
if ($Gather) {
	Invoke-Gather $mirror 'Game_Gather.ini'
}
if ($Compile) {
	# 수집은 archive의 번역을 내보낸다. JSON에만 있던 새 번역이 빈 문자열로
	# 되돌아가지 않도록 원본 번역을 다시 적용한 뒤 리소스를 만든다.
	Sync-Folder (Join-Path $mirror $localizationPart) (Join-Path $repoRoot $localizationPart)
	& python (Join-Path $PSScriptRoot 'apply_localization.py')
	if ($LASTEXITCODE -ne 0) { throw '번역 적용 검사를 통과하지 못했습니다.' }
	Sync-Folder (Join-Path $repoRoot $localizationPart) (Join-Path $mirror $localizationPart)
	Invoke-Gather $mirror 'Game_Compile.ini'
}
Sync-Folder (Join-Path $mirror $localizationPart) (Join-Path $repoRoot $localizationPart)
Get-ChildItem -LiteralPath (Join-Path $repoRoot $localizationPart) -Recurse -File |
	ForEach-Object { Write-Host ("  {0}  {1:N0} bytes" -f $_.FullName.Substring($repoRoot.Length + 1), $_.Length) }

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
