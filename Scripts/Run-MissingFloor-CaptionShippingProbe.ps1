[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArchiveDirectory,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    # 같은 배포본에 -IGCaptionLifecycleReview를 붙여 완료한 Frontend 결과를 재사용한다.
    [string]$FrontendEvidenceDirectory,
    [ValidateRange(45, 300)][int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-MissingFloor-CaptionShippingProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$archiveRoot = (Resolve-Path -LiteralPath $ArchiveDirectory).Path.TrimEnd('\', '/')
$evidenceRoot = [IO.Path]::GetFullPath($EvidenceDirectory).TrimEnd('\', '/')
if ($evidenceRoot -eq $archiveRoot -or
    $evidenceRoot.StartsWith($archiveRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw '자막 검사 결과는 배포 폴더 밖에 저장해야 합니다.'
}
if ((Test-Path -LiteralPath $evidenceRoot) -and @(Get-ChildItem -LiteralPath $evidenceRoot -Force).Count) {
    throw "자막 검사는 새 결과 폴더에서 실행해야 합니다: $evidenceRoot"
}
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory $evidenceRoot
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1') -ArchiveDirectory $archiveRoot
$manifestPath = Join-Path $archiveRoot 'manifest.json'
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$executable = Join-Path $archiveRoot 'Windows/IndieGame/Binaries/Win64/IndieGame-Win64-Shipping.exe'
$shippingHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash

function Get-EvidenceFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "검사 증거 파일이 없습니다: $Path" }
    return [pscustomobject]@{ path = [IO.Path]::GetFullPath($Path); sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
}

function Get-CaptionPng([string]$Path, [int]$Width, [int]$Height) {
    $file = Get-EvidenceFile $Path
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 128 -or ($bytes[0..7] -join ',') -cne '137,80,78,71,13,10,26,10') {
        throw "캡처가 정상 PNG가 아닙니다: $Path"
    }
    $actualWidth = [Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($bytes, 16))
    $actualHeight = [Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($bytes, 20))
    if ($actualWidth -ne $Width -or $actualHeight -ne $Height) { throw "캡처 해상도가 다릅니다: $Path" }
    return [pscustomobject]@{ path = $file.path; sha256 = $file.sha256; width = $actualWidth; height = $actualHeight }
}

if (-not $FrontendEvidenceDirectory) {
    $FrontendEvidenceDirectory = Join-Path $evidenceRoot 'Frontend'
    & (Join-Path $PSScriptRoot 'Run-MissingFloor-FrontendShippingProbe.ps1') -ArchiveDirectory $archiveRoot `
        -EvidenceDirectory $FrontendEvidenceDirectory -TimeoutSeconds ([Math]::Min(180, $TimeoutSeconds)) `
        -ExtraArguments @('-IGCulture=ko', '-IGCaptionLifecycleReview')
}
$frontendRoot = (Resolve-Path -LiteralPath $FrontendEvidenceDirectory).Path
$frontendSummaryPath = Join-Path $frontendRoot 'summary.json'
$frontend = Get-Content -LiteralPath $frontendSummaryPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($frontend.shippingExecutableSha256 -cne $shippingHash -or -not $frontend.archiveUnchanged -or
    $frontend.resolutionCount -ne 4 -or $frontend.inputEventCount -ne 44 -or $frontend.layoutSampleCount -ne 44 -or
    @($frontend.results).Count -ne 4) {
    throw 'Frontend 결과의 배포본 해시나 검수 범위가 다릅니다.'
}
$pauseResults = @(
    foreach ($resolution in @('1280x720', '1600x900', '1920x1080', '2560x1440')) {
        $frontendCase = @($frontend.results | Where-Object { $_.resolution -ceq $resolution })
        if ($frontendCase.Count -ne 1) { throw "Frontend 해상도 결과가 없거나 중복됐습니다: $resolution" }
        $caseRoot = Join-Path $frontendRoot $resolution
        $receiptPath = Join-Path $caseRoot 'caption-pause.txt'
        $receiptFile = Get-EvidenceFile $receiptPath
        $receipt = (Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8).Trim()
        if ($receipt -cnotmatch '^MISSINGFLOOR_CAPTION_PAUSE PASS pause_seconds=([0-9]+\.[0-9]+) gameplay_draw=1 pause_draw=0 resume_draw=1 queue_preserved=1$') {
            throw "일시정지 자막 검사가 통과하지 않았습니다: $receipt"
        }
        $seconds = [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
        if ($seconds -lt 6.0) { throw '자막 수명보다 길게 일시정지하지 않았습니다.' }
        $dimensions = $resolution.Split('x')
        $screenshots = @(
            Get-CaptionPng (Join-Path $caseRoot 'caption-paused.png') ([int]$dimensions[0]) ([int]$dimensions[1])
            Get-CaptionPng (Join-Path $caseRoot 'caption-resumed.png') ([int]$dimensions[0]) ([int]$dimensions[1])
        )
        [pscustomobject]@{ resolution = $resolution; pauseSeconds = $seconds; passed = $true; receipt = $receiptFile; screenshots = $screenshots }
    }
)

$nightRoot = Join-Path $evidenceRoot 'NightFive'
$userDirectory = Join-Path $nightRoot 'User'
New-Item -ItemType Directory -Path $userDirectory -Force | Out-Null
$nightReceiptPath = Join-Path $nightRoot 'receipt.txt'
$arguments = @('/Game/Maps/Prologue_Morning', '-unattended', '-nosplash', '-NoLoadingScreen',
    '-RenderOffscreen', '-d3d12', '-nosound', '-Windowed', '-ResX=1280', '-ResY=720', '-ForceRes', '-NoVSync',
    '-IGCulture=ko', '-IGNightFiveProbe', '-IGCaptionLifecycleReview',
    "-IGCaptionResultPath=$nightReceiptPath", "-IGCaptionScreenshotDirectory=$nightRoot", "-UserDir=$userDirectory")
$quotedArguments = @($arguments | ForEach-Object {
    if ($_.Contains('"')) { throw '실행 인자에 잘못된 따옴표가 있습니다.' }
    '"' + $_ + '"'
})
$startedAt = [DateTime]::UtcNow
$process = Start-Process -FilePath $executable -ArgumentList $quotedArguments -WorkingDirectory (Split-Path $executable -Parent) `
    -WindowStyle Hidden -PassThru
$processId = $process.Id
try {
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        throw '다섯째 밤 자막 검사가 제한 시간 안에 끝나지 않았습니다.'
    }
    if ($process.ExitCode -ne 0) { throw "다섯째 밤 자막 검사 종료 코드: $($process.ExitCode)" }
}
finally { $process.Dispose() }
$nightReceiptFile = Get-EvidenceFile $nightReceiptPath
if ((Get-Item -LiteralPath $nightReceiptPath).LastWriteTimeUtc -lt $startedAt) { throw '이번 실행의 영수증이 아닙니다.' }
$nightReceipt = Get-Content -LiteralPath $nightReceiptPath -Raw -Encoding UTF8
if (($nightReceipt -split '\r?\n')[0] -cne 'MISSINGFLOOR_CAPTION_NIGHT5 PASS signal_draw=1 answer_draw=1 return_clear=1 unlock_bypass=1') {
    throw "다섯째 밤 자막 검사가 통과하지 않았습니다: $nightReceipt"
}
$nightScreenshots = @(
    Get-CaptionPng (Join-Path $nightRoot 'night-five-signal.png') 1280 720
    Get-CaptionPng (Join-Path $nightRoot 'night-five-answer.png') 1280 720
)
if (@(Get-ChildItem -LiteralPath $userDirectory -Recurse -Filter '*.sav' -File).Count) {
    throw '자막 표시 검사에서 저장 파일이 생성됐습니다.'
}
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1') -ArchiveDirectory $archiveRoot
if ((Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash -cne $manifestHash) {
    throw '검사 중 배포 매니페스트가 바뀌었습니다.'
}
$summary = [pscustomobject]@{
    schemaVersion = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString('o')
    commit = $manifest.commit
    shippingExecutableSha256 = $shippingHash
    archiveUnchanged = $true
    scope = 'D3D12에서 4개 해상도의 플레이 자막, 6초 일시정지 뒤 복귀와 타이틀 자막 제거를 검사합니다. 다섯째 밤은 해금 조회를 우회해 두 소리 자막의 실제 출력과 타이틀 복귀만 검사합니다. 소리 재생과 실제 엔딩 해금 검사는 별도입니다.'
    frontendSummary = Get-EvidenceFile $frontendSummaryPath
    pauseCases = $pauseResults
    nightFive = [pscustomobject]@{ passed = $true; unlockBypass = $true; processId = $processId; receipt = $nightReceiptFile; screenshots = $nightScreenshots }
}
$summaryPath = Join-Path $evidenceRoot 'summary.json'
$json = $summary | ConvertTo-Json -Depth 7
[IO.File]::WriteAllText($summaryPath, $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Host "CAPTION_SHIPPING PASS pause_cases=4 night_five_captions=2 unlock_bypass=1 summary=$summaryPath"

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
