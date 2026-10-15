[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArchiveDirectory,
    [Parameter(Mandatory)][string]$SeedSaveFile,
    [string]$EvidenceDirectory,
    [ValidateRange(30, 300)][int]$TimeoutSeconds = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-MissingFloor-SaveRecoveryProbe.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$archiveRoot = (Resolve-Path -LiteralPath $ArchiveDirectory).Path.TrimEnd('\', '/')
$seedPath = (Resolve-Path -LiteralPath $SeedSaveFile).Path
if (-not (Test-Path -LiteralPath $seedPath -PathType Leaf)) { throw '검사에 쓸 입주 저장 파일이 없습니다.' }
$seedHash = (Get-FileHash -LiteralPath $seedPath -Algorithm SHA256).Hash
$seedBytes = [IO.File]::ReadAllBytes($seedPath)
if ($seedBytes.Length -lt 64) { throw '검사에 쓸 입주 저장 파일이 너무 짧습니다.' }
# 속성 구조와 저장 시각은 유지하고 맵 이름만 같은 길이의 없는 이름으로 바꾼다.
$missingMapBytes = [byte[]]$seedBytes.Clone()
$mapToken = [Text.Encoding]::ASCII.GetBytes('/Game/Maps/Prologue_Morning')
$missingMapToken = [Text.Encoding]::ASCII.GetBytes('/Game/Maps/Prologue_Missing')
$mapMatches = 0
for ($offset = 0; $offset -le $seedBytes.Length - $mapToken.Length; $offset++) {
    $isTokenMatch = $true
    for ($index = 0; $index -lt $mapToken.Length; $index++) {
        if ($seedBytes[$offset + $index] -ne $mapToken[$index]) { $isTokenMatch = $false; break }
    }
    if ($isTokenMatch) {
        [Array]::Copy($missingMapToken, 0, $missingMapBytes, $offset, $missingMapToken.Length)
        $mapMatches++
    }
}
if ($mapMatches -ne 1) { throw '입주 저장 파일의 맵 이름을 한 곳에서 찾을 수 없습니다.' }
# 정상 저장의 길이 필드만 바꾼다. 거대한 문자열·배열이 실제로 할당되면 안 된다.
if ([BitConverter]::ToInt32($seedBytes, 0) -ne 0x53415647 -or
    [BitConverter]::ToInt32($seedBytes, 4) -ne 3) { throw '검사 원본은 GVAS 3 저장이어야 합니다.' }
$branchLength = [BitConverter]::ToInt32($seedBytes, 26)
if ($branchLength -eq 0 -or [math]::Abs([long]$branchLength) -gt 1024) { throw '엔진 버전 문자열을 찾지 못했습니다.' }
$branchBytes = [math]::Abs([long]$branchLength) * $(if ($branchLength -lt 0) { 2 } else { 1 })
$customFormatOffset = [int](30 + $branchBytes)
if ([BitConverter]::ToInt32($seedBytes, $customFormatOffset) -ne 3) { throw '검사 원본의 버전 목록 형식이 다릅니다.' }
$customCountOffset = $customFormatOffset + 4
$customCount = [BitConverter]::ToInt32($seedBytes, $customCountOffset)
if ($customCount -lt 0 -or $customCount -gt 1024) { throw '검사 원본의 버전 목록 수가 다릅니다.' }
$classLengthOffset = $customCountOffset + 4 + 20 * $customCount
$classLength = [BitConverter]::ToInt32($seedBytes, $classLengthOffset)
if ($classLength -le 0 -or $classLength -gt 128 -or
    [Text.Encoding]::ASCII.GetString($seedBytes, $classLengthOffset + 4, $classLength).TrimEnd([char]0) -cne '/Script/IndieGame.IGSaveGame') {
    throw '검사 원본의 저장 클래스가 다릅니다.'
}
function New-LengthDamage([int]$Offset) {
    $bytes = [byte[]]$seedBytes.Clone()
    [Array]::Copy([BitConverter]::GetBytes([int]1073741824), 0, $bytes, $Offset, 4)
    return ,$bytes
}
function Find-TruthArrayCountOffset {
    $token = [Text.Encoding]::ASCII.GetBytes("Truths`0")
    $matches = [Collections.Generic.List[int]]::new()
    for ($index = $classLengthOffset + 4 + $classLength; $index -le $seedBytes.Length - $token.Length; $index++) {
        $same = $true
        for ($part = 0; $part -lt $token.Length; $part++) {
            if ($seedBytes[$index + $part] -ne $token[$part]) { $same = $false; break }
        }
        if ($same -and [BitConverter]::ToInt32($seedBytes, $index - 4) -eq $token.Length) { $matches.Add($index + $token.Length) }
    }
    if ($matches.Count -ne 1) { throw '진실 배열의 속성 이름을 한 곳에서 찾지 못했습니다.' }
    $cursor = $matches[0]
    $remaining = 1
    $nodeCount = 0
    while ($remaining -gt 0) {
        if (++$nodeCount -gt 32 -or $cursor + 4 -gt $seedBytes.Length) { throw '배열 속성 타입을 읽지 못했습니다.' }
        $nameLength = [BitConverter]::ToInt32($seedBytes, $cursor)
        if ($nameLength -le 0 -or $nameLength -gt 128 -or $cursor + 8 + $nameLength -gt $seedBytes.Length) { throw '배열 속성 타입 이름이 다릅니다.' }
        $typeName = [Text.Encoding]::ASCII.GetString($seedBytes, $cursor + 4, $nameLength).TrimEnd([char]0)
        if ($nodeCount -eq 1 -and $typeName -cne 'ArrayProperty') { throw '진실 속성이 배열이 아닙니다.' }
        $cursor += 4 + $nameLength
        $children = [BitConverter]::ToInt32($seedBytes, $cursor)
        if ($children -lt 0 -or $children -gt 8) { throw '배열 속성의 타입 트리가 다릅니다.' }
        $cursor += 4
        $remaining += $children - 1
    }
    $bodySize = [BitConverter]::ToInt32($seedBytes, $cursor)
    $flags = $seedBytes[$cursor + 4]
    $cursor += 5
    if ($flags -band 1) { $cursor += 4 }
    if ($flags -band 2) { $cursor += 16 }
    if (($flags -band 4) -or $bodySize -lt 4 -or $cursor + $bodySize -gt $seedBytes.Length -or
        [BitConverter]::ToInt32($seedBytes, $cursor) -notin 0..1024) { throw '진실 배열의 본문 범위를 확인하지 못했습니다.' }
    return $cursor
}
$wrongClassBytes = [byte[]]$seedBytes.Clone()
$wrongClassBytes[$classLengthOffset + 4] = [byte][char]'X'
$damagedData = @{
    HugeBranch = New-LengthDamage 26
    HugeCustomVersions = New-LengthDamage $customCountOffset
    HugeClass = New-LengthDamage $classLengthOffset
    WrongClass = $wrongClassBytes
    HugeArray = New-LengthDamage (Find-TruthArrayCountOffset)
    Oversized = [byte[]]::new(8 * 1024 * 1024 + 1)
}
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1') -ArchiveDirectory $archiveRoot
# 패키지 안의 Shipping 실행 파일을 직접 실행해 저장 복원을 확인한다.
$launcher = Join-Path $archiveRoot 'Windows/IndieGame/Binaries/Win64/IndieGame-Win64-Shipping.exe'
$manifest = Get-Content -LiteralPath (Join-Path $archiveRoot 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $EvidenceDirectory) {
    $EvidenceDirectory = Join-Path $projectRoot ('Saved/Validation/SaveRecovery-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
}
$evidenceRoot = [IO.Path]::GetFullPath($EvidenceDirectory).TrimEnd('\', '/')
if ($evidenceRoot -eq $archiveRoot -or
    $evidenceRoot.StartsWith($archiveRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw '저장 복원 검사 결과는 배포 폴더 밖에 두어야 합니다.'
}
if ((Test-Path -LiteralPath $evidenceRoot) -and @(Get-ChildItem -LiteralPath $evidenceRoot -Force).Count) {
    throw "검사 결과는 새 폴더에 저장해야 합니다: $evidenceRoot"
}
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory $evidenceRoot
$results = [Collections.Generic.List[object]]::new()

function Invoke-SaveCase([string]$Name, [int]$ValidSlot, [int[]]$DamagedSlots, [switch]$Truncated, [switch]$MissingMap, [string]$DamageKind) {
    $caseRoot = Join-Path $evidenceRoot $Name
    $userRoot = Join-Path $caseRoot 'User'
    $saveRoot = Join-Path $userRoot 'Saved/SaveGames'
    $receipt = Join-Path $caseRoot 'receipt.txt'
    New-Item -ItemType Directory -Path $saveRoot -Force | Out-Null
    if ($ValidSlot -ge 0) {
        [IO.File]::WriteAllBytes((Join-Path $saveRoot "AutoSave_$ValidSlot.sav"), $seedBytes)
    }
    foreach ($slot in $DamagedSlots) {
        # 원본을 건드리지 않고 이 실행의 복사본만 손상시킨다.
        $damaged = if ($DamageKind) { $damagedData[$DamageKind] }
            elseif ($MissingMap) { $missingMapBytes }
            elseif ($Truncated) { [byte[]]$seedBytes[0..31] }
            else { [Text.Encoding]::UTF8.GetBytes('invalid-save-fixture') }
        [IO.File]::WriteAllBytes((Join-Path $saveRoot "AutoSave_$slot.sav"), $damaged)
    }
    $before = @(Get-ChildItem -LiteralPath $saveRoot -Filter '*.sav' | Sort-Object Name | ForEach-Object {
        $_.Name + ' ' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    })
    $arguments = @('-unattended', '-nosplash', '-NoLoadingScreen', '-RenderOffscreen', '-nullrhi', '-nosound',
        '-IGSkipFrontend', '-IGMissingFloor', '-IGArrivalSaveRead',
        "-UserDir=$userRoot", "-IGMissingFloorResultPath=$receipt")
    $quoted = @($arguments | ForEach-Object {
        if ($_.Contains('"')) { throw '실행 인자에 잘못된 따옴표가 있습니다.' }
        '"' + $_ + '"'
    })
    $started = [DateTime]::UtcNow
    $process = Start-Process -FilePath $launcher -ArgumentList $quoted -WorkingDirectory (Split-Path $launcher -Parent) -WindowStyle Hidden -PassThru
    $peakWorkingSet = 0L
    $peakCommitted = 0L
    try {
        $null = $process.Handle
        while (-not $process.WaitForExit(100)) {
            try {
                $process.Refresh()
                $peakWorkingSet = [math]::Max($peakWorkingSet, $process.PeakWorkingSet64)
                $peakCommitted = [math]::Max($peakCommitted, $process.PeakPagedMemorySize64)
            }
            catch [InvalidOperationException] {
                if ($process.HasExited) { break }
                throw
            }
            if (([DateTime]::UtcNow - $started).TotalSeconds -gt $TimeoutSeconds) {
                & taskkill.exe /PID $process.Id /T /F | Out-Null
                throw "저장 복원 검사 시간 초과: $Name"
            }
        }
        $exitCode = $process.ExitCode
    }
    finally { $process.Dispose() }
    $expectSuccess = $ValidSlot -ge 0
    $expectedResult = if ($expectSuccess) { 'PASS' } else { 'FAIL' }
    # UE 5.8의 창 없는 실행에서는 RequestExitWithStatus(false, 1)도 0으로
    # 끝난다. 따라서 예상한 실패도 새 영수증의 FAIL을 반드시 확인한다.
    if (($expectSuccess -and $exitCode -ne 0) -or (-not $expectSuccess -and $exitCode -notin @(0, 1))) {
        throw "저장 복원 검사 종료 코드가 다릅니다: $Name actual=$exitCode"
    }
    if (-not (Test-Path -LiteralPath $receipt -PathType Leaf)) {
        throw "저장 복원 검사 영수증이 없습니다: $Name"
    }
    $receiptFile = Get-Item -LiteralPath $receipt
    $receiptText = (Get-Content -LiteralPath $receipt -Raw -Encoding UTF8).Trim()
    if ($receiptFile.LastWriteTimeUtc -lt $started -or
        $receiptText -notmatch "^MISSINGFLOOR_GREYBOX $expectedResult step=[0-9]+$") {
        throw "저장 복원 결과가 예상과 다릅니다: $Name $receiptText"
    }
    $after = @(Get-ChildItem -LiteralPath $saveRoot -Filter '*.sav' | Sort-Object Name | ForEach-Object {
        $_.Name + ' ' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    })
    if (($before -join "`n") -cne ($after -join "`n")) {
        throw "불러오기 검사 도중 저장 파일을 덮어썼습니다: $Name"
    }
    # NullRHI 저장 검사에서 2 GiB를 넘으면 손상 입력에 대한 대량 할당을 의심한다.
    if ($peakCommitted -gt 2GB -or $peakWorkingSet -gt 2GB) { throw "저장 검사 메모리 상한 초과: $Name" }
    $results.Add([ordered]@{
        name = $Name; passed = $true; restored = $expectSuccess; exitCode = $exitCode
        savesUnchanged = $true; seconds = [math]::Round(([DateTime]::UtcNow - $started).TotalSeconds, 2)
        receipt = $receiptText; receiptSha256 = (Get-FileHash -LiteralPath $receipt -Algorithm SHA256).Hash
        peakWorkingSetMiB = [math]::Round($peakWorkingSet / 1MB, 2)
        peakCommittedMiB = [math]::Round($peakCommitted / 1MB, 2)
    })
    Write-Host "SAVE_RECOVERY_CASE PASS $Name restored=$expectSuccess"
}

# 정상 슬롯 하나만으로 입주 조사·상자·취침 상태를 복원해야 한다.
Invoke-SaveCase 'SingleValidSlot' 0 @()
Invoke-SaveCase 'CorruptSlot0' 1 @(0)
Invoke-SaveCase 'CorruptSlot1' 0 @(1)
Invoke-SaveCase 'TruncatedSlot0' 1 @(0) -Truncated
Invoke-SaveCase 'MissingMapSlot0' 1 @(0) -MissingMap
Invoke-SaveCase 'OnlyMissingMap' -1 @(0) -MissingMap
Invoke-SaveCase 'BothCorrupt' -1 @(0, 1)
Invoke-SaveCase 'NoSave' -1 @()
foreach ($kind in @('HugeBranch', 'HugeCustomVersions', 'HugeClass', 'WrongClass', 'HugeArray', 'Oversized')) {
    Invoke-SaveCase ($kind + 'WithBackup') 1 @(0) -DamageKind $kind
}
Invoke-SaveCase 'OnlyHugeArray' -1 @(0) -DamageKind 'HugeArray'
Invoke-SaveCase 'OnlyOversized' -1 @(0) -DamageKind 'Oversized'
if ((Get-FileHash -LiteralPath $seedPath -Algorithm SHA256).Hash -ne $seedHash) {
    throw '원본 저장 파일이 검사 도중 바뀌었습니다.'
}
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1') -ArchiveDirectory $archiveRoot
[ordered]@{
    schemaVersion = 2; createdAt = [DateTime]::UtcNow.ToString('o'); commit = $manifest.commit
    hasLocalChanges = $manifest.hasLocalChanges; seedSaveSha256 = $seedHash; seedUnchanged = $true
    scope = 'Shipping NullRHI에서 입주 저장 복원, 손상·없는 맵·거대한 길이·다른 클래스·8 MiB 초과 슬롯의 안전 거부와 정상 슬롯 복구, 저장 파일 보존과 2 GiB 메모리 상한을 검사합니다. 화면·일반 플레이 성능·사람의 완주 평가는 별도입니다.'
    cases = $results.ToArray()
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'summary.json') -Encoding UTF8
Write-Host "SAVE_RECOVERY_PROBE PASS cases=$($results.Count) evidence=$evidenceRoot"

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
