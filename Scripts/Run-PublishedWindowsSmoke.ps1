[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+$')][string]$ReleaseTag,
    [Parameter(Mandatory)][string]$WorkDirectory,
    [ValidateRange(120, 900)][int]$TimeoutSeconds = 480
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($WorkDirectory)
if (Test-Path -LiteralPath $root) { throw '공개 배포본 검사는 새 폴더에서 실행해야 합니다.' }
$downloadRoot = Join-Path $root 'Download'
$installRoot = Join-Path $root 'Game'
$evidenceRoot = Join-Path $root 'Evidence'
New-Item -ItemType Directory -Path $downloadRoot, $installRoot, $evidenceRoot | Out-Null
$version = $ReleaseTag.Substring(1)
$assetName = "MissingFloor-$version-Windows.zip"
$repository = 'easygap/Missing-Floor'
$releaseText = & gh api "repos/$repository/releases/tags/$ReleaseTag"
if ($LASTEXITCODE -ne 0) { throw '공개 릴리스 정보를 읽지 못했습니다.' }
$release = $releaseText | ConvertFrom-Json
if ($release.draft -or $release.tag_name -cne $ReleaseTag) { throw '공개된 버전과 요청한 태그가 다릅니다.' }
$assets = @($release.assets | Where-Object { $_.name -ceq $assetName })
if ($assets.Count -ne 1 -or $assets[0].digest -notmatch '^sha256:[a-f0-9]{64}$') {
    throw 'GitHub 서버의 배포 파일 해시를 확인하지 못했습니다.'
}
& gh release download $ReleaseTag --repo $repository --pattern $assetName --pattern SHA256SUMS.txt --dir $downloadRoot
if ($LASTEXITCODE -ne 0) { throw '공개 배포 파일을 내려받지 못했습니다.' }
$zipPath = Join-Path $downloadRoot $assetName
$zipSha = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$sumPattern = '^([a-fA-F0-9]{64})\s+' + [regex]::Escape($assetName) + '$'
$sumLines = @(Get-Content -LiteralPath (Join-Path $downloadRoot 'SHA256SUMS.txt') | Where-Object { $_ -match $sumPattern })
if ($sumLines.Count -ne 1 -or $sumLines[0] -notmatch $sumPattern) { throw '해시 목록에 배포 ZIP이 없거나 중복됐습니다.' }
if ($Matches[1].ToLowerInvariant() -cne $zipSha -or $assets[0].digest -cne "sha256:$zipSha" -or
    (Get-Item -LiteralPath $zipPath).Length -ne $assets[0].size) { throw '다운로드한 ZIP이 공개 원본과 다릅니다.' }

# 압축을 풀기 전에 모든 항목이 새 게임 폴더 안에 머무는지 확인한다.
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $archive.Entries) {
        $name = $entry.FullName.Replace('\', '/')
        if ([IO.Path]::IsPathRooted($name) -or $name.Contains(':') -or $name.Split('/') -contains '..' -or
            -not $names.Add($name)) { throw "잘못된 ZIP 항목입니다: $name" }
        $destination = [IO.Path]::GetFullPath((Join-Path $installRoot $name))
        if (-not $destination.StartsWith($installRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) { throw 'ZIP 항목이 설치 폴더를 벗어납니다.' }
    }
}
finally { $archive.Dispose() }
Expand-Archive -LiteralPath $zipPath -DestinationPath $installRoot
$launcher = Join-Path $installRoot 'MissingFloor.exe'
$shipping = Join-Path $installRoot 'IndieGame/Binaries/Win64/IndieGame-Win64-Shipping.exe'
foreach ($path in @($launcher, $shipping)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-Item -LiteralPath $path).VersionInfo.ProductVersion.Trim() -cne $version) {
        throw '실행 파일 또는 제품 버전이 다릅니다.'
    }
}
$receipt = Join-Path $evidenceRoot 'receipt.txt'
$userRoot = Join-Path $root 'User'
# 진행 검사에는 한국어 대사와 직접 대조하는 항목이 있어 호스트 언어를 따르지 않는다.
# -RenderOffScreen이 있으면 창 자체를 만들지 않는다. 숨김 실행만으로는 실제 게임 프로세스가 창을 띄운다.
$arguments = @('-unattended', '-nosplash', '-NoLoadingScreen', '-nullrhi', '-nosound', '-RenderOffScreen',
    '-IGSkipFrontend', '-IGCulture=ko', '-IGListenerGreybox', '-IGListenerGreyboxProbe',
    "-UserDir=$userRoot", "-IGMissingFloorResultPath=$receipt")
$quoted = @($arguments | ForEach-Object {
    if ($_.Contains('"')) { throw '실행 인자에 따옴표를 넣을 수 없습니다.' }
    '"' + $_ + '"'
})
$started = [DateTime]::UtcNow
$process = Start-Process -FilePath $launcher -ArgumentList $quoted -WorkingDirectory $installRoot -WindowStyle Hidden -PassThru
try {
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        throw '진행 로직 검사가 제한 시간 안에 끝나지 않았습니다.'
    }
    if ($process.ExitCode -ne 0) { throw "배포본 종료 코드: $($process.ExitCode)" }
}
finally { $process.Dispose() }
if (-not (Test-Path -LiteralPath $receipt) -or (Get-Item -LiteralPath $receipt).LastWriteTimeUtc -lt $started -or
    (Get-Content -LiteralPath $receipt -Raw).Trim() -cnotmatch '^MISSINGFLOOR_GREYBOX PASS step=([0-9]+)$') {
    throw '진행 로직을 완료한 새 검사 기록이 없습니다.'
}
$terminalStage = [int]$Matches[1]
if ($terminalStage -ne 38) { throw '예상한 진행 검사 완료 상태가 아닙니다.' }
$result = [ordered]@{
    schemaVersion = 1; status = 'PASS'; version = $version; releaseTag = $ReleaseTag;
    releaseUrl = $release.html_url; zipSha256 = $zipSha; zipBytes = $assets[0].size;
    serverDigestMatched = $true; checksumFileMatched = $true;
    shippingSha256 = (Get-FileHash -LiteralPath $shipping -Algorithm SHA256).Hash.ToLowerInvariant();
    receiptSha256 = (Get-FileHash -LiteralPath $receipt -Algorithm SHA256).Hash.ToLowerInvariant();
    terminalStage = $terminalStage; elapsedSeconds = [math]::Round(([DateTime]::UtcNow - $started).TotalSeconds, 3);
    os = (Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber);
    runnerImage = $env:ImageOS; runnerImageVersion = $env:ImageVersion; workflowCommit = $env:GITHUB_SHA;
    workflowRunId = $env:GITHUB_RUN_ID; workflowRunAttempt = $env:GITHUB_RUN_ATTEMPT;
    scope = '공개 ZIP의 다운로드·해시·파일 버전과 NullRHI 진행 로직 검사. Unreal Engine 설치 단계 없이 실행한다. 호스트에 개발용 런타임이 미리 설치돼 있을 수 있다.';
    culture = 'ko'; graphicsTested = $false; audioTested = $false; supportedDesktopOsCertified = $false;
    humanFullPlaythroughApproved = $false; shippingReleaseCertified = $false
}
[IO.File]::WriteAllText((Join-Path $evidenceRoot 'summary.json'), ($result | ConvertTo-Json -Depth 5) + "`n", [Text.UTF8Encoding]::new($false))
Write-Host "PUBLISHED_WINDOWS_SMOKE PASS version=$version terminal_stage=$terminalStage zip_sha256=$zipSha"
