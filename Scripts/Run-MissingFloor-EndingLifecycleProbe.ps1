[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArchiveDirectory,
    [string]$EvidenceDirectory,
    # 경로를 캐시 키로 잘못 기록한 구형 배포본을 진단할 때만 허용한다.
    [switch]$AllowLegacyProfilePathMetadata,
    [ValidateRange(180, 900)][int]$TimeoutSeconds = 450
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$archiveRoot = (Resolve-Path -LiteralPath $ArchiveDirectory).Path
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1') -ArchiveDirectory $archiveRoot
$manifestPath = Join-Path $archiveRoot 'manifest.json'
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
$manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath | ConvertFrom-Json
$executable = Join-Path $archiveRoot 'Windows/IndieGame/Binaries/Win64/IndieGame-Win64-Shipping.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw 'Shipping 실행 파일이 없습니다.' }
if (-not $EvidenceDirectory) {
    $EvidenceDirectory = Join-Path $projectRoot ('Saved/Validation/EndingLifecycle-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$EvidenceDirectory = [IO.Path]::GetFullPath($EvidenceDirectory)
if ($EvidenceDirectory.TrimEnd('\', '/') -eq $archiveRoot.TrimEnd('\', '/') -or
    $EvidenceDirectory.StartsWith($archiveRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw '검사 결과 폴더는 배포 폴더 밖에 두어야 합니다.'
}
if ((Test-Path -LiteralPath $EvidenceDirectory) -and @(Get-ChildItem -LiteralPath $EvidenceDirectory -Force).Count) {
    throw "엔딩 검사는 새 결과 폴더에서 실행해야 합니다: $EvidenceDirectory"
}
New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
$results = [Collections.Generic.List[object]]::new()

function Invoke-EndingProcess([string]$Name, [string]$Mode, [string]$Ending, [string]$UserDirectory, [string[]]$ExtraArguments) {
    $caseRoot = Join-Path $EvidenceDirectory $Name
    New-Item -ItemType Directory -Path $caseRoot -Force | Out-Null
    $receiptPath = Join-Path $caseRoot 'receipt.json'
    # 프로필을 세션 안에만 남기는 IGSkipFrontend/IGFreshOnboarding은 분기와 재시작 검사에 쓰지 않는다.
    $arguments = @('-unattended', '-nosplash', '-NoLoadingScreen', '-RenderOffscreen', '-nullrhi', '-nosound',
        '-ExecCmds=t.MaxFPS 60', "-UserDir=$UserDirectory", "-IGEndingResultPath=$receiptPath") + $ExtraArguments
    $quotedArguments = @($arguments | ForEach-Object {
        if ($_.Contains('"')) { throw '실행 인자에 잘못된 따옴표가 있습니다.' }
        '"' + $_ + '"'
    })
    $started = [DateTime]::UtcNow
    $process = Start-Process -FilePath $executable -ArgumentList $quotedArguments -WorkingDirectory (Split-Path $executable -Parent) -WindowStyle Hidden -PassThru
    $processId = $process.Id
    try {
        $null = $process.Handle
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            & taskkill.exe /PID $process.Id /T /F | Out-Null
            throw "엔딩 검사 시간 초과: $Name"
        }
        if ($process.ExitCode -ne 0) { throw "엔딩 검사 종료 코드 오류: $Name ($($process.ExitCode))" }
    }
    finally { $process.Dispose() }
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf) -or
        (Get-Item -LiteralPath $receiptPath).LastWriteTimeUtc -lt $started) { throw "새 검사 영수증이 없습니다: $Name" }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $profileEvidence = $null
    if ($receipt.probe -cne 'MISSINGFLOOR_ENDING_LIFECYCLE' -or $receipt.status -cne 'PASS' -or
        $receipt.mode -cne $Mode -or $receipt.ending -cne $Ending) {
        throw "엔딩 검사 실패: $Name ($($receipt.reason))"
    }
    if ($Mode -eq 'checkpoint') {
        if ($receipt.saveSlot -cnotmatch '^AutoSave_[01]$') { throw '검사가 실제 자동 저장 슬롯을 남기지 않았습니다.' }
    }
    else {
        $expectB = $Ending -eq 'B'
        if ([bool]$receipt.nightFiveAvailable -ne $expectB -or [bool]$receipt.endingBSeen -ne $expectB -or
            [bool]$receipt.endingASeen -eq $expectB) { throw "엔딩 분기의 프로필이 섞였거나 해금 상태가 다릅니다: $Name" }
        # UE 5.8의 GGameUserSettingsIni는 캐시 키일 수 있다. 영수증의 추정 경로로
        # 프로필 존재 여부를 판단하지 않고, 이 실행에 넘긴 독립 UserDir의 파일을 읽는다.
        $profilePath = [IO.Path]::GetFullPath((Join-Path $UserDirectory 'Saved/Config/Windows/GameUserSettings.ini'))
        if (-not (Test-Path -LiteralPath $profilePath -PathType Leaf)) { throw '독립 UserDir 안에 실제 프로필 파일이 생성되지 않았습니다.' }
        $iniValues = @{}
        $section = ''
        foreach ($line in [IO.File]::ReadAllLines($profilePath)) {
            if ($line -match '^\s*\[([^\]]+)\]\s*$') { $section = $Matches[1] }
            elseif ($line -match '^\s*([^;#][^=]*?)\s*=\s*(.*?)\s*$') { $iniValues[$section + '/' + $Matches[1]] = $Matches[2] }
        }
        $profileA = $iniValues['IndieGame.Onboarding/EndingSeen.Ending.A'] -in @('1', 'true')
        $profileB = $iniValues['IndieGame.Onboarding/EndingSeen.Ending.B'] -in @('1', 'true')
        $experienced = $iniValues['IndieGame.MissingFloorProfile/EpilogueExperienced'] -in @('1', 'true')
        if ($profileA -eq $expectB -or $profileB -ne $expectB -or -not $experienced) {
            throw "실제 프로필 파일에 엔딩 분기 또는 에필로그 완료 기록이 없습니다: $Name"
        }
        $reportedPath = [IO.Path]::GetFullPath([string]$receipt.profilePath)
        $metadataMismatch = -not $reportedPath.Equals($profilePath, [StringComparison]::OrdinalIgnoreCase)
        if ($metadataMismatch) {
            if (-not $AllowLegacyProfilePathMetadata) { throw "프로필 경로 메타데이터가 실제 파일과 다릅니다: $Name" }
            Write-Host "ENDING_RECEIPT_METADATA_WARNING $Name : 영수증의 profilePath가 실제 프로필 경로와 다릅니다. 독립 UserDir의 실제 파일 내용으로 검사했습니다."
        }
        $profileEvidence = [ordered]@{
            path = $profilePath; sha256 = (Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash
            endingASeen = $profileA; endingBSeen = $profileB; epilogueExperienced = $experienced
            reportedProfilePath = $receipt.profilePath; profilePathMetadataMismatch = $metadataMismatch
        }
        if ($Mode -eq 'ending') {
            $minimumSeconds = if ($expectB) { 82.0 } else { 98.0 }
            $expectedScenes = if ($expectB) { 5 } else { 6 }
            if (-not $receipt.restoredThroughSaveSubsystem -or $receipt.epilogueSeconds -lt $minimumSeconds -or
                $receipt.epilogueScenes -ne $expectedScenes) { throw "실제 복원·에필로그 재생 조건을 채우지 못했습니다: $Name" }
            if ($expectB -and (-not $receipt.vigilObserved -or -not $receipt.sevenSecondWaitObserved -or
                $receipt.vigilSeconds -lt 29.5)) { throw '엔딩 B에서 실제 기다림을 건너뛰었습니다.' }
        }
    }
    $results.Add([ordered]@{
        name = $Name; passed = $true; processId = $processId
        seconds = [math]::Round(([DateTime]::UtcNow - $started).TotalSeconds, 2)
        receipt = $receiptPath; receiptSha256 = (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash
        profileEvidence = $profileEvidence
    })
    Write-Host "ENDING_LIFECYCLE_CASE PASS $Name"
    return $receipt
}

$seedUser = Join-Path $EvidenceDirectory 'Checkpoint/User'
$checkpoint = Invoke-EndingProcess 'Checkpoint' 'checkpoint' '' $seedUser @(
    '-IGListenerGreybox', '-IGListenerGreyboxProbe', '-IGSkipFrontend', '-IGEndingCheckpointWrite')
$seedFile = Join-Path $seedUser ('Saved/SaveGames/' + $checkpoint.saveSlot + '.sav')
if (-not (Test-Path -LiteralPath $seedFile -PathType Leaf)) { throw '선택 직전 저장 파일을 찾지 못했습니다.' }
$seedHash = (Get-FileHash -LiteralPath $seedFile -Algorithm SHA256).Hash
foreach ($ending in @('A', 'B')) {
    $branchUser = Join-Path $EvidenceDirectory ("Ending$ending/User")
    $saveDirectory = Join-Path $branchUser 'Saved/SaveGames'
    New-Item -ItemType Directory -Path $saveDirectory -Force | Out-Null
    # 완주 과정의 같은 원본 파일만 복사한다. 서사·엔딩·프로필 값은 덧붙이지 않는다.
    Copy-Item -LiteralPath $seedFile -Destination (Join-Path $saveDirectory 'AutoSave_0.sav')
    $null = Invoke-EndingProcess "Ending$ending" 'ending' $ending $branchUser @('-IGMissingFloor', "-IGEndingResume=$ending")
    $null = Invoke-EndingProcess "Profile$ending" 'profile' $ending $branchUser @('-IGMissingFloor', "-IGEndingResume=$ending", '-IGEndingProfileRead')
}
if ((Get-FileHash -LiteralPath $seedFile -Algorithm SHA256).Hash -cne $seedHash) { throw '검사 중 원본 체크포인트가 바뀌었습니다.' }
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1') -ArchiveDirectory $archiveRoot
if ((Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash -cne $manifestHash) { throw '검사 중 패키지 명세가 바뀌었습니다.' }
[ordered]@{
    schemaVersion = 2; createdAt = [DateTime]::UtcNow.ToString('o'); archive = $archiveRoot
    commit = $manifest.commit; hasLocalChanges = $manifest.hasLocalChanges; manifestSha256 = $manifestHash
    scope = '실제 FullGame 진행의 선택 직전 저장, 새 프로세스의 정상 저장 복원, A/B 상호작용 완료, B의 30초 기다림, A 99초/B 83초 에필로그, 프로필 기록과 재시작 후 다섯째 밤 해금. NullRHI 기능 검사이며 영상·음질·실제 키 홀드·사람의 완주 평가는 별도입니다.'
    seed = $seedFile; seedSha256 = $seedHash; seedUnchanged = $true; cases = $results.ToArray()
    profilePathMetadataMismatchCount = @($results | Where-Object { $_.profileEvidence -and $_.profileEvidence.profilePathMetadataMismatch }).Count
    allowLegacyProfilePathMetadata = $AllowLegacyProfilePathMetadata.IsPresent
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'summary.json') -Encoding utf8
Write-Host "ENDING_LIFECYCLE PASS $EvidenceDirectory"
