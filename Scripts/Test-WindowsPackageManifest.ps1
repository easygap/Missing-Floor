[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArchiveDirectory,
    [ValidatePattern('^[0-9a-fA-F]{40}$')][string]$ExpectedCommit,
    [switch]$RequireCleanCommit
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$archiveRoot = (Resolve-Path -LiteralPath $ArchiveDirectory).Path.TrimEnd([char[]]@('\', '/'))
$windowsRoot = Join-Path $archiveRoot 'Windows'
$manifestPath = Join-Path $archiveRoot 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "배포 파일 목록이 없습니다: $manifestPath"
}
$manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath | ConvertFrom-Json
if ($manifest.commit -notmatch '^[0-9a-fA-F]{40}$' -or $manifest.hasLocalChanges -isnot [bool]) {
    throw '배포 파일 목록의 커밋 정보가 올바르지 않습니다.'
}
if ($ExpectedCommit -and $manifest.commit -ne $ExpectedCommit) {
    throw "검사할 커밋과 배포 파일의 커밋이 다릅니다: $($manifest.commit)"
}
if ($RequireCleanCommit -and $manifest.hasLocalChanges) {
    throw '커밋하지 않은 변경이 들어 있는 배포 파일은 출시 후보로 검사할 수 없습니다.'
}
if (-not (Test-Path -LiteralPath $windowsRoot -PathType Container)) {
    throw "게임 폴더가 없습니다: $windowsRoot"
}
# 링크로 바깥 파일을 읽거나 설치 폴더 밖을 배포하는 일을 막는다.
if ((Get-Item -LiteralPath $windowsRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw '게임 폴더는 심볼릭 링크나 연결 지점일 수 없습니다.'
}
$entries = @(Get-ChildItem -LiteralPath $windowsRoot -Force -Recurse)
if (@($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
    throw '배포 폴더에 심볼릭 링크나 연결 지점이 있습니다.'
}
$actualFiles = @($entries | Where-Object { -not $_.PSIsContainer })
$listed = @{}
$totalBytes = 0L
foreach ($file in @($manifest.files)) {
    $relative = [string]$file.path
    if ($relative -notmatch '^Windows/[^\\:]+$' -or
        @($relative.Split('/') | Where-Object { $_ -eq '.' -or $_ -eq '..' -or $_ -eq '' }).Count) {
        throw "배포 파일 경로가 올바르지 않습니다: $relative"
    }
    if ($listed.ContainsKey($relative)) { throw "배포 파일 목록에 같은 경로가 두 번 있습니다: $relative" }
    if ($file.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or [long]$file.bytes -lt 0) {
        throw "배포 파일의 크기나 해시가 올바르지 않습니다: $relative"
    }
    $path = Join-Path $archiveRoot $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "배포 파일이 빠졌습니다: $relative" }
    $actual = Get-Item -LiteralPath $path
    if ($actual.Length -ne [long]$file.bytes -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) {
        throw "배포 파일이 목록을 만든 뒤 바뀌었습니다: $relative"
    }
    $listed[$relative] = $actual.Length
    $totalBytes += $actual.Length
}
if ($actualFiles.Count -ne $listed.Count) {
    throw '배포 폴더의 파일 수가 목록과 다릅니다. 목록에 없는 파일이 있는지 확인해 주세요.'
}
if ($totalBytes -gt 8000000000L) { throw '설치 용량이 출시 기준인 8.0 GB를 넘었습니다.' }

$binaryRoot = 'Windows/IndieGame/Binaries/Win64'
$required = @(
    'Windows/MissingFloor.exe',
    "$binaryRoot/IndieGame-Win64-Shipping.exe",
    "$binaryRoot/vcruntime140.dll", "$binaryRoot/msvcp140.dll",
    'Windows/Engine/Extras/Redist/en-us/vc_redist.x64.exe',
    'Windows/IndieGame/Content/Paks/IndieGame-Windows.pak',
    'Windows/IndieGame/Content/Paks/IndieGame-Windows.utoc',
    'Windows/IndieGame/Content/Paks/IndieGame-Windows.ucas',
    'Windows/IndieGame/Content/Paks/global.utoc',
    'Windows/IndieGame/Content/Paks/global.ucas'
)
foreach ($font in @('Pretendard-Regular.otf', 'Pretendard-SemiBold.otf', 'GowunBatang-Bold.ttf', 'OFL-Pretendard.txt', 'OFL-GowunBatang.txt',
    'NotoSansJP-Regular.otf', 'NotoSansSC-Regular.otf', 'NotoSansTC-Regular.otf', 'OFL-NotoSansCJK.txt')) {
    $required += "$binaryRoot/UI/Fonts/$font"
}
foreach ($relative in $required) {
    if (-not $listed.ContainsKey($relative) -or $listed[$relative] -le 0) {
        throw "실행에 필요한 파일이 빠졌거나 비어 있습니다: $relative"
    }
}
foreach ($relative in $listed.Keys) {
    if ($relative -match '(?i)(?:^|/)(?:Saved|Intermediate|SourceArt|\.git)(?:/|$)|\.(?:pdb|psd|blend|fbx)$') {
        throw "배포에 필요 없는 개발 파일이 들어 있습니다: $relative"
    }
}
Write-Host "WINDOWS_PACKAGE_MANIFEST PASS files=$($listed.Count) bytes=$totalBytes commit=$($manifest.commit)"
