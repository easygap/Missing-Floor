[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$fixtureParent = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Saved/Validation'))
$fixture = Join-Path $fixtureParent ('PackageManifest-' + [Guid]::NewGuid().ToString('N'))
$checker = Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1'
$commit = '0123456789abcdef0123456789abcdef01234567'
$checks = 0
New-Item -ItemType Directory -Path $fixture -Force | Out-Null

function Write-Manifest {
    $files = @(Get-ChildItem -LiteralPath (Join-Path $fixture 'Windows') -File -Recurse | ForEach-Object {
        [ordered]@{
            path = $_.FullName.Substring($fixture.Length + 1).Replace('\', '/')
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    })
    [ordered]@{ commit = $commit; hasLocalChanges = $false; files = $files } |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $fixture 'manifest.json') -Encoding UTF8
}

function Assert-Rejected([string]$Label, [scriptblock]$Action, [string]$ExpectedMessage) {
    $rejected = $false
    try { & $Action }
    catch {
        if ($_.Exception.Message -notmatch $ExpectedMessage) { throw "$Label 검사에서 다른 오류가 났습니다: $($_.Exception.Message)" }
        $rejected = $true
    }
    if (-not $rejected) { throw "$Label 문제가 있는 배포물을 통과시켰습니다." }
    $script:checks++
}

try {
    $binaryRoot = 'Windows/IndieGame/Binaries/Win64'
    $paths = @(
        'Windows/MissingFloor.exe', "$binaryRoot/IndieGame-Win64-Shipping.exe",
        "$binaryRoot/vcruntime140.dll", "$binaryRoot/msvcp140.dll",
        'Windows/Engine/Extras/Redist/en-us/vc_redist.x64.exe'
    )
    foreach ($container in @('IndieGame-Windows.pak', 'IndieGame-Windows.utoc', 'IndieGame-Windows.ucas', 'global.utoc', 'global.ucas')) {
        $paths += "Windows/IndieGame/Content/Paks/$container"
    }
    foreach ($font in @('Pretendard-Regular.otf', 'Pretendard-SemiBold.otf', 'GowunBatang-Bold.ttf', 'OFL-Pretendard.txt', 'OFL-GowunBatang.txt',
        'NotoSansJP-Regular.otf', 'NotoSansSC-Regular.otf', 'NotoSansTC-Regular.otf', 'OFL-NotoSansCJK.txt')) {
        $paths += "$binaryRoot/UI/Fonts/$font"
    }
    foreach ($relative in $paths) {
        $path = Join-Path $fixture $relative
        New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force | Out-Null
        [IO.File]::WriteAllText($path, '검사용 파일')
    }
    Write-Manifest
    & $checker -ArchiveDirectory $fixture -ExpectedCommit $commit -RequireCleanCommit
    $checks++
    Assert-Rejected '다른 커밋' { & $checker -ArchiveDirectory $fixture -ExpectedCommit ('a' * 40) } '커밋이 다릅니다'

    $launcher = Join-Path $fixture 'Windows/MissingFloor.exe'
    [IO.File]::WriteAllText($launcher, '내용이 달라진 파일')
    Assert-Rejected '변조' { & $checker -ArchiveDirectory $fixture } '목록을 만든 뒤 바뀌었습니다'
    Write-Manifest
    $extra = Join-Path $fixture 'Windows/extra.txt'
    [IO.File]::WriteAllText($extra, '목록에 없는 파일')
    Assert-Rejected '목록 밖 파일' { & $checker -ArchiveDirectory $fixture } '파일 수가 목록과 다릅니다'
    Remove-Item -LiteralPath $extra

    $runtime = Join-Path $fixture "$binaryRoot/vcruntime140.dll"
    Remove-Item -LiteralPath $runtime
    Assert-Rejected '빠진 파일' { & $checker -ArchiveDirectory $fixture } '배포 파일이 빠졌습니다'
    Write-Manifest
    Assert-Rejected '필수 런타임 누락' { & $checker -ArchiveDirectory $fixture } '실행에 필요한 파일'
    [IO.File]::WriteAllText($runtime, '')
    Write-Manifest
    Assert-Rejected '빈 런타임' { & $checker -ArchiveDirectory $fixture } '실행에 필요한 파일'
    [IO.File]::WriteAllText($runtime, '실행 구성 요소')
    Write-Manifest

    $manifestPath = Join-Path $fixture 'manifest.json'
    $validManifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath
    $changed = $validManifest | ConvertFrom-Json
    $changed.hasLocalChanges = $true
    $changed | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Assert-Rejected '미커밋 변경' { & $checker -ArchiveDirectory $fixture -RequireCleanCommit } '커밋하지 않은 변경'
    $changed = $validManifest | ConvertFrom-Json
    $changed.files += $changed.files[0]
    $changed | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Assert-Rejected '중복 경로' { & $checker -ArchiveDirectory $fixture } '같은 경로가 두 번'
    $changed = $validManifest | ConvertFrom-Json
    $changed.files[0].path = 'Windows/../outside.txt'
    $changed | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Assert-Rejected '폴더 밖 경로' { & $checker -ArchiveDirectory $fixture } '경로가 올바르지 않습니다'
    Set-Content -LiteralPath $manifestPath -Value $validManifest -Encoding UTF8
    $debugFile = Join-Path $fixture 'Windows/IndieGame.pdb'
    [IO.File]::WriteAllText($debugFile, '개발용 심볼')
    Write-Manifest
    Assert-Rejected '개발 파일 포함' { & $checker -ArchiveDirectory $fixture } '개발 파일이 들어 있습니다'
    Write-Host "WINDOWS_PACKAGE_MANIFEST_REGRESSION PASS checks=$checks"
}
finally {
    # 이 실행에서 만든 임시 폴더만 지운다. 기존 검사 결과는 건드리지 않는다.
    $resolvedFixture = [IO.Path]::GetFullPath($fixture)
    if ((Split-Path $resolvedFixture -Parent) -ne $fixtureParent -or
        (Split-Path $resolvedFixture -Leaf) -notmatch '^PackageManifest-[0-9a-f]{32}$') {
        throw "임시 검사 폴더가 예상 경로를 벗어났습니다: $resolvedFixture"
    }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
}
