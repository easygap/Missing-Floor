[CmdletBinding()]
param([string]$ArchiveDirectory, [switch]$RequireCleanCommit)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Package-Windows.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildCommit = (& git -C $projectRoot rev-parse HEAD)
if ($LASTEXITCODE -ne 0) { throw '패키징할 커밋을 확인하지 못했습니다.' }
$buildStatus = @(& git -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0) { throw '작업 폴더의 변경 내역을 확인하지 못했습니다.' }
if ($RequireCleanCommit -and $buildStatus.Count) {
    throw '출시 후보는 변경 사항을 모두 커밋한 뒤 만들어 주세요.'
}
if ([string]::IsNullOrWhiteSpace($ArchiveDirectory)) {
    $ArchiveDirectory = Join-Path $projectRoot ('Saved/Packages/Windows-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$ArchiveDirectory = [IO.Path]::GetFullPath($ArchiveDirectory)
if ((Test-Path -LiteralPath $ArchiveDirectory) -and
    @(Get-ChildItem -LiteralPath $ArchiveDirectory -Force).Count -gt 0) {
    throw "배포 폴더는 비어 있어야 합니다: $ArchiveDirectory"
}

Add-IGArtifactDirectory -Scope $igArtifactScope -Directory $ArchiveDirectory

# 한글 경로의 빌드 복사는 기존 스크립트 한 곳에서 관리한다.
& (Join-Path $PSScriptRoot 'Build-ArtAssets.ps1') -CodeOnly
$buildRoot = $projectRoot
if ($projectRoot -match '[^\x00-\x7F]') {
    # Build-ArtAssets.ps1의 Get-AsciiArtBuildRoot와 같은 계산이다. Windows PowerShell 5.1에는
    # [Convert]::ToHexString과 SHA256.HashData가 없어서 ComputeHash로 쓴다.
    $bytes = [Text.Encoding]::UTF8.GetBytes($projectRoot.ToLowerInvariant())
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hashBytes = $sha.ComputeHash($bytes) } finally { $sha.Dispose() }
    $hash = -join ($hashBytes[0..3] | ForEach-Object { $_.ToString('x2') })
    $buildRoot = Join-Path $env:LOCALAPPDATA "IndieGame/AsciiBuild/Art_$hash"
}
$buildProject = Join-Path $buildRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$engineRoot = Split-Path (Split-Path (Split-Path $editor -Parent) -Parent) -Parent
$uat = Join-Path $engineRoot 'Build/BatchFiles/RunUAT.bat'
# UE 5.8은 지정한 보관 폴더에 실행 파일을 바로 놓는다.
$windowsDirectory = Join-Path $ArchiveDirectory 'Windows'
$arguments = @(
    'BuildCookRun', "-project=$buildProject", '-target=IndieGame',
    '-noP4', '-unattended', '-utf8output', '-platform=Win64', '-clientconfig=Shipping',
    '-build', '-cook', '-allmaps', '-stage', '-pak', '-iostore', '-package',
    '-compressed', '-nodebuginfo', '-prereqs',
    '-applocaldirectory=$(EngineDir)/Binaries/ThirdParty/AppLocalDependencies',
    '-archive', "-archivedirectory=$windowsDirectory"
)
& $uat @arguments
if ($LASTEXITCODE -ne 0) { throw "Windows 배포 파일 생성 실패: $LASTEXITCODE" }

# 쿠커는 설정을 읽고도 에셋을 조용히 빠뜨릴 수 있다. 실제 컨테이너 목록으로 코드가
# 경로로 부르는 에셋이 다 들었는지 확인한다(Scripts/check_package_contents.py).
$containerCsv = Join-Path $ArchiveDirectory 'container.csv'
& $editor -run=IoStore "-List=$(Join-Path $windowsDirectory 'IndieGame/Content/Paks/IndieGame-Windows.utoc')" "-csv=$containerCsv" -unattended -nopause | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $containerCsv)) { throw "배포 컨테이너 목록을 읽지 못했습니다: $LASTEXITCODE" }
& python (Join-Path $PSScriptRoot 'check_package_contents.py') $containerCsv
if ($LASTEXITCODE -ne 0) { throw '배포본에 코드가 부르는 에셋이 빠져 있습니다.' }

$launcher = Join-Path $ArchiveDirectory 'Windows/MissingFloor.exe'
Move-Item -LiteralPath (Join-Path $ArchiveDirectory 'Windows/IndieGame.exe') -Destination $launcher
$game = Join-Path $ArchiveDirectory 'Windows/IndieGame/Binaries/Win64/IndieGame-Win64-Shipping.exe'
& (Join-Path $PSScriptRoot 'Copy-Windows-ExecutableVersionResource.ps1') -SourceExecutable $game -DestinationExecutable $launcher
foreach ($exe in @($launcher, $game)) {
    & (Join-Path $PSScriptRoot 'Test-Windows-ExecutableMetadata.ps1') -Executable $exe
    # 실행 파일 둘 다 저장소의 Application.ico를 달고 나와야 한다.
    & (Join-Path $PSScriptRoot 'Test-Windows-ExecutableIcon.ps1') -Executable $exe `
        -ExpectedIco (Join-Path $projectRoot 'Build/Windows/Application.ico') `
        -EvidencePng (Join-Path $ArchiveDirectory ('icon-' + [IO.Path]::GetFileNameWithoutExtension($exe) + '.png'))
}
$files = Get-ChildItem -LiteralPath (Join-Path $ArchiveDirectory 'Windows') -File -Recurse | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($ArchiveDirectory.Length).TrimStart('\', '/').Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$afterCommit = (& git -C $projectRoot rev-parse HEAD)
if ($LASTEXITCODE -ne 0) { throw '패키징 뒤 커밋을 확인하지 못했습니다.' }
$afterStatus = @(& git -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0) { throw '패키징 뒤 변경 내역을 확인하지 못했습니다.' }
if ($afterCommit -ne $buildCommit -or ($buildStatus -join "`n") -cne ($afterStatus -join "`n")) {
    throw '패키징 도중 소스가 바뀌었습니다. 완성된 커밋에서 다시 만들어 주세요.'
}
[ordered]@{
    schemaVersion = 2
    createdAt = [DateTime]::UtcNow.ToString('o')
    commit = $buildCommit
    hasLocalChanges = [bool]$buildStatus.Count
    buildProject = $buildProject
    files = @($files)
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ArchiveDirectory 'manifest.json') -Encoding utf8
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1') -ArchiveDirectory $ArchiveDirectory `
    -ExpectedCommit $buildCommit -RequireCleanCommit:$RequireCleanCommit
Write-Host "WINDOWS_PACKAGE PASS $ArchiveDirectory"

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
