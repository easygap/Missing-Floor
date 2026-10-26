[CmdletBinding()]
param(
    [string]$ArchiveDirectory,
    [switch]$RequireCleanCommit,
    # C: 여유가 모자랄 때 쿠크 저장소(Zen)를 이 폴더에 따로 띄운다.
    [string]$ZenDataDirectory,
    [ValidateRange(1024, 65535)][int]$ZenPort = 8568
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
. (Join-Path $PSScriptRoot 'Resolve-AsciiWorkspace.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Package-Windows.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
$zenProcess = $null
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

# 코드 빌드와 쿠크가 검증한 ASCII 경로로 같은 작업 폴더를 사용한다.
& (Join-Path $PSScriptRoot 'Build-ArtAssets.ps1') -CodeOnly
$buildRoot = Resolve-IGAsciiWorkspace -ProjectRoot $projectRoot
$buildProject = Join-Path $buildRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$engineRoot = Split-Path (Split-Path (Split-Path $editor -Parent) -Parent) -Parent
$uat = Join-Path $engineRoot 'Build/BatchFiles/RunUAT.bat'

# Zen은 C: 여유가 2 GB 아래면 쓰기를 거부해 쿠크가 멈춘다. 그럴 때는 다른 드라이브에 Zen을 따로 띄우고
# 이 실행의 엔진 프로세스가 새로 띄우지 않고 거기에만 붙게 한다. 자동 실행은 같은 포트에 데이터 폴더가 다른
# Zen이 있으면 그걸 내리므로, 같은 PC의 다른 프로젝트가 쓰는 기본 포트 8558은 피한다.
$zenArguments = @()
if ($ZenDataDirectory) {
    if ($ZenPort -eq 8558) { throw '따로 띄우는 Zen은 기본 포트 8558을 쓸 수 없습니다.' }
    $ZenDataDirectory = [IO.Path]::GetFullPath($ZenDataDirectory)
    New-Item -ItemType Directory -Force -Path $ZenDataDirectory | Out-Null
    $zenUtility = Join-Path $engineRoot 'Binaries/Win64/zen.exe'
    $zenProcess = Start-Process -FilePath (Join-Path $engineRoot 'Binaries/Win64/zenserver.exe') -WindowStyle Hidden -PassThru `
        -ArgumentList @('--port', $ZenPort, '--data-dir', ('"' + $ZenDataDirectory + '"'), '--http', 'asio',
            '--http-forceloopback', '--owner-pid', $PID, '--quiet')
    $null = $zenProcess.Handle
    $zenReady = $false
    $zenDeadline = (Get-Date).AddSeconds(60)
    while (-not $zenReady -and -not $zenProcess.HasExited -and (Get-Date) -lt $zenDeadline) {
        Start-Sleep -Milliseconds 500
        try { $zenReady = (Invoke-WebRequest -UseBasicParsing -TimeoutSec 2 "http://127.0.0.1:$ZenPort/health/").StatusCode -eq 200 }
        catch { $zenReady = $false }
    }
    if (-not $zenReady) { throw "따로 띄운 Zen이 포트 ${ZenPort}에서 응답하지 않습니다." }
    $zenArguments = @("-NoZenAutoLaunch=127.0.0.1:$ZenPort")
}

# UE 5.8은 지정한 보관 폴더에 실행 파일을 바로 놓는다.
$windowsDirectory = Join-Path $ArchiveDirectory 'Windows'
$arguments = @(
    'BuildCookRun', "-project=$buildProject", '-target=IndieGame',
    '-noP4', '-unattended', '-utf8output', '-platform=Win64', '-clientconfig=Shipping',
    '-build', '-cook', '-allmaps', '-stage', '-pak', '-iostore', '-package',
    '-compressed', '-nodebuginfo', '-prereqs',
    '-applocaldirectory=$(EngineDir)/Binaries/ThirdParty/AppLocalDependencies',
    '-archive', "-archivedirectory=$windowsDirectory"
) + $zenArguments
& $uat @arguments
if ($LASTEXITCODE -ne 0) { throw "Windows 배포 파일 생성 실패: $LASTEXITCODE" }

# 쿠커는 설정을 읽고도 에셋을 조용히 빠뜨릴 수 있다. 실제 컨테이너 목록으로 코드가
# 경로로 부르는 에셋이 다 들었는지 확인한다(Scripts/check_package_contents.py).
$containerCsv = Join-Path $ArchiveDirectory 'container.csv'
& $editor -run=IoStore "-List=$(Join-Path $windowsDirectory 'IndieGame/Content/Paks/IndieGame-Windows.utoc')" "-csv=$containerCsv" -unattended -nopause @zenArguments | Out-Null
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
    if ($zenProcess -and -not $zenProcess.HasExited) {
        & $zenUtility down --port $ZenPort | Out-Null
        if (-not $zenProcess.WaitForExit(30000)) { Stop-Process -Id $zenProcess.Id -ErrorAction SilentlyContinue }
    }
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
