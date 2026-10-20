[CmdletBinding()]
param(
	# 소스의 NSLOCTEXT를 모아 목록과 언어별 .po를 새로 만든다.
	[switch]$Gather,
	# 번역된 .po를 목록에 들이고 언어별 .locres를 만든다.
	[switch]$Compile,
	# 코드 빌드를 생략하고 현재 작업 폴더의 모듈을 쓴다.
	[switch]$NoBuild
)

# 커맨드릿은 검증한 ASCII junction으로 같은 작업 폴더를 연다.
# 수집·번역 적용·컴파일이 모두 원본 파일에 기록되므로 동기화하지 않는다.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
. (Join-Path $PSScriptRoot 'Resolve-AsciiWorkspace.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Localize-Game.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
if (-not $Gather -and -not $Compile) {
	throw '-Gather 또는 -Compile 중 하나 이상을 고르세요.'
}
$repoRoot = Split-Path -Parent $PSScriptRoot
$localizationPart = 'Content\Localization\Game'

function Invoke-Gather([string]$BuildRoot, [string]$ConfigName) {
	$project = Join-Path $BuildRoot 'IndieGame.uproject'
	$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $project -Commandlet
	$config = Join-Path $BuildRoot "Config\Localization\$ConfigName"
	$log = Join-Path $repoRoot "Saved\Logs\Localize-$([IO.Path]::GetFileNameWithoutExtension($ConfigName)).log"
	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $log) | Out-Null
	& $editor $project -run=GatherText "-config=$config" -unattended -nop4 -nosplash -nullrhi -NoShaderCompile "-abslog=$log" | Out-Null
	$exit = $LASTEXITCODE
	if ($exit -ne 0) { throw "$ConfigName 실패($exit). 로그: $log" }
	Write-Host "LOCALIZE $ConfigName PASS"
}

if (-not $NoBuild) {
	# 빌드 실패는 Build-ArtAssets.ps1이 예외로 전달한다.
	& (Join-Path $PSScriptRoot 'Build-ArtAssets.ps1') -CodeOnly
}
$buildRoot = Resolve-IGAsciiWorkspace -ProjectRoot $repoRoot
if ($Gather) {
	Invoke-Gather $buildRoot 'Game_Gather.ini'
}
if ($Compile) {
	# 수집은 archive의 번역을 내보낸다. JSON에만 있던 새 번역이 빈 문자열로
	# 되돌아가지 않도록 원본 번역을 다시 적용한 뒤 리소스를 만든다.
	& python (Join-Path $PSScriptRoot 'apply_localization.py')
	if ($LASTEXITCODE -ne 0) { throw '번역 적용 검사를 통과하지 못했습니다.' }
	Invoke-Gather $buildRoot 'Game_Compile.ini'
}
Get-ChildItem -LiteralPath (Join-Path $repoRoot $localizationPart) -Recurse -File |
	ForEach-Object { Write-Host ("  {0}  {1:N0} bytes" -f $_.FullName.Substring($repoRoot.Length + 1), $_.Length) }

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
