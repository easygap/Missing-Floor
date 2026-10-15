[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArchiveDirectory,
    [string]$EvidenceDirectory,
    [ValidatePattern('^[0-9a-fA-F]{40}$')][string]$ExpectedCommit,
    [switch]$RequireCleanCommit,
    [ValidateRange(60, 900)][int]$TimeoutSeconds = 360
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-WindowsPackageReview.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$archiveRoot = (Resolve-Path -LiteralPath $ArchiveDirectory).Path
$manifestArguments = @{ ArchiveDirectory = $archiveRoot; RequireCleanCommit = $RequireCleanCommit }
if ($ExpectedCommit) { $manifestArguments.ExpectedCommit = $ExpectedCommit }
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1') @manifestArguments
$packageManifest = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $archiveRoot 'manifest.json') | ConvertFrom-Json
$launcher = Join-Path $archiveRoot 'Windows/MissingFloor.exe'
if (-not (Test-Path -LiteralPath $launcher)) { throw "실행 파일이 없습니다: $launcher" }
if (-not $EvidenceDirectory) {
    $EvidenceDirectory = Join-Path $projectRoot ('Saved/Validation/WindowsPackage-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$EvidenceDirectory = [IO.Path]::GetFullPath($EvidenceDirectory)
if ($EvidenceDirectory.TrimEnd('\', '/') -eq $archiveRoot.TrimEnd('\', '/') -or
    $EvidenceDirectory.StartsWith($archiveRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw '검사 결과 폴더는 배포 폴더 밖에 두어야 합니다.'
}
if ((Test-Path -LiteralPath $EvidenceDirectory) -and @(Get-ChildItem -LiteralPath $EvidenceDirectory -Force).Count) {
    throw "검사 결과는 새 폴더에 저장해야 합니다: $EvidenceDirectory"
}
New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory $EvidenceDirectory

function Get-ArchiveHashes {
    @(Get-ChildItem -LiteralPath $archiveRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
        # Windows PowerShell 5.1에는 [IO.Path]::GetRelativePath가 없다.
        '{0} {1}' -f $_.FullName.Substring($archiveRoot.Length).TrimStart('\', '/'), (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    })
}
$before = Get-ArchiveHashes
$results = [Collections.Generic.List[object]]::new()

function Invoke-GameCase([string]$Name, [string[]]$ExtraArguments, [switch]$Audio, [string]$UserDirectory) {
    $caseRoot = Join-Path $EvidenceDirectory $Name
    New-Item -ItemType Directory -Path $caseRoot -Force | Out-Null
    $receipt = Join-Path $caseRoot 'receipt.txt'
    if (-not $UserDirectory) { $UserDirectory = Join-Path $caseRoot 'User' }
    $arguments = @('-unattended', '-nosplash', '-NoLoadingScreen', '-RenderOffscreen',
        '-Windowed', '-ResX=1280', '-ResY=720', '-ForceRes', '-IGSkipFrontend', '-IGCulture=ko',
        "-UserDir=$UserDirectory", "-IGMissingFloorResultPath=$receipt") + $ExtraArguments
    if ($Audio) {
        $arguments += @('-d3d12', '-AudioMixer', '-AllowCommandletAudio',
            '-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0', '-ExecCmds=t.MaxFPS 60')
    }
    else { $arguments += '-nosound' }
    $quoted = @($arguments | ForEach-Object {
        if ($_.Contains('"')) { throw '실행 인자에 잘못된 따옴표가 있습니다.' }
        '"' + $_ + '"'
    })
    $started = [DateTime]::UtcNow
    $process = Start-Process -FilePath $launcher -ArgumentList $quoted -WorkingDirectory (Split-Path $launcher -Parent) -WindowStyle Hidden -PassThru
    try {
        # Windows PowerShell 5.1은 핸들을 먼저 잡아 두지 않으면 ExitCode를 비워 둔다.
        $null = $process.Handle
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            # 런처가 띄운 Shipping 프로세스까지 같이 끈다. Kill($true)는 PowerShell 7 전용이다.
            & taskkill.exe /PID $process.Id /T /F | Out-Null
            throw "게임 실행 검사 시간 초과: $Name"
        }
        if ($process.ExitCode -ne 0) { throw "게임 실행 검사 실패: $Name (종료 코드 $($process.ExitCode))" }
    }
    finally { $process.Dispose() }
    if ($Audio) {
        $audioReceipts = @(Get-ChildItem -LiteralPath $caseRoot -Filter receipt.txt -Recurse | Where-Object { $_.Directory.Name -eq 'AudioPresentation' })
        if ($audioReceipts.Count -ne 1) { throw '소리 검사 결과 파일이 없습니다.' }
        $receipt = $audioReceipts[0].FullName
        $text = Get-Content -LiteralPath $receipt -Raw
        if ($text -notmatch 'AUDIO_PRESENTATION_PROBE PASS failures=0' -or $text -match 'CHECK .+ FAIL') { throw "소리 검사 실패: $receipt" }
        $recording = Join-Path (Split-Path $receipt -Parent) 'presentation-mix.wav'
        $waveform = @(& python (Join-Path $PSScriptRoot 'check_audio_presentation.py') $recording)
        $waveformExitCode = $LASTEXITCODE
        $waveform
        # Windows PowerShell 5.1의 Tee-Object는 UTF-16으로 쓴다. 다른 검사 기록처럼 UTF-8로 남긴다.
        [IO.File]::WriteAllLines((Join-Path $caseRoot 'waveform.json'), [string[]]$waveform)
        if ($waveformExitCode -ne 0) { throw '배포 파일의 실제 믹서 녹음이 검사를 통과하지 못했습니다.' }
    }
    elseif (-not (Test-Path -LiteralPath $receipt) -or (Get-Content -Raw -LiteralPath $receipt).Trim() -notmatch '^MISSINGFLOOR_GREYBOX PASS step=[0-9]+$') {
        throw "게임 진행 검사 결과가 없습니다: $Name"
    }
    if ((Get-Item -LiteralPath $receipt).LastWriteTimeUtc -lt $started) { throw "이전 검사 결과가 남아 있습니다: $Name" }
    $results.Add([ordered]@{ name = $Name; passed = $true; seconds = [math]::Round(([DateTime]::UtcNow - $started).TotalSeconds, 2); receipt = $receipt; receiptSha256 = (Get-FileHash -LiteralPath $receipt -Algorithm SHA256).Hash })
    Write-Host "WINDOWS_GAME_CASE PASS $Name"
}

# 기본 검수는 한국어로 고정한다. 외국어 화면 검수는 각 언어를 지정해 별도로 실행한다.
& (Join-Path $PSScriptRoot 'Run-MissingFloor-FrontendShippingProbe.ps1') -ArchiveDirectory $archiveRoot `
    -EvidenceDirectory (Join-Path $EvidenceDirectory 'Frontend') -ExtraArguments @('-IGCulture=ko')
$saveUser = Join-Path $EvidenceDirectory 'SaveUser'
Invoke-GameCase 'Arrival' @('-IGMissingFloor', '-IGArrivalProbe', '-IGArrivalSaveWrite', '-d3d12') -UserDirectory $saveUser
$saveFiles = @(Get-ChildItem -LiteralPath $saveUser -Recurse -Filter 'AutoSave_*.sav')
if ($saveFiles.Count -lt 1 -or $saveFiles.Count -gt 2) { throw '자동 저장 파일이 생성되지 않았습니다.' }
foreach ($save in $saveFiles) {
    if ($save.Length -le 0) { throw "자동 저장 파일이 비어 있습니다: $($save.Name)" }
}
Invoke-GameCase 'ArrivalResume' @('-IGMissingFloor', '-IGArrivalSaveRead', '-d3d12') -UserDirectory $saveUser
$seedSave = $saveFiles | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
& (Join-Path $PSScriptRoot 'Run-MissingFloor-SaveRecoveryProbe.ps1') -ArchiveDirectory $archiveRoot `
    -SeedSaveFile $seedSave.FullName -EvidenceDirectory (Join-Path $EvidenceDirectory 'SaveRecovery')
Invoke-GameCase 'FullGame' @('-IGListenerGreybox', '-IGListenerGreyboxProbe', '-nullrhi')
& (Join-Path $PSScriptRoot 'Run-MissingFloor-EndingLifecycleProbe.ps1') -ArchiveDirectory $archiveRoot `
    -EvidenceDirectory (Join-Path $EvidenceDirectory 'EndingLifecycle')
Invoke-GameCase 'Audio' @('-IGAudioPresentationProbe') -Audio
$after = Get-ArchiveHashes
if (@(Compare-Object $before $after).Count) { throw '검사 중 배포 폴더의 파일이 바뀌었습니다.' }
[ordered]@{
    schemaVersion = 2
    createdAt = [DateTime]::UtcNow.ToString('o')
    archive = $archiveRoot
    commit = $packageManifest.commit
    hasLocalChanges = $packageManifest.hasLocalChanges
    manifestSha256 = (Get-FileHash -LiteralPath (Join-Path $archiveRoot 'manifest.json') -Algorithm SHA256).Hash
    scope = '패키지 무결성, 화면·입력, 입주 저장·이어하기와 손상 슬롯 복원, 전체 진행, 소리 자동 검사. 여러 PC의 성능 인증과 사람의 완주 검수는 별도로 필요합니다.'
    launcherSha256 = (Get-FileHash -LiteralPath $launcher -Algorithm SHA256).Hash
    archiveUnchanged = $true
    frontend = (Join-Path $EvidenceDirectory 'Frontend/summary.json')
    saveRecovery = (Join-Path $EvidenceDirectory 'SaveRecovery/summary.json')
    endingLifecycle = (Join-Path $EvidenceDirectory 'EndingLifecycle/summary.json')
    cases = $results.ToArray()
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'summary.json') -Encoding utf8
Write-Host "WINDOWS_PACKAGE_REVIEW PASS $EvidenceDirectory"

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
