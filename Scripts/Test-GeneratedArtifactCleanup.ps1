[CmdletBinding()]
param([string]$HoldProject, [string]$ReadyFile, [string]$StopFile)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    Write-Host 'ARTIFACT_CLEANUP_TEST SKIP Windows 파일 잠금 검사는 이 운영체제에서 실행하지 않습니다.'
    return
}

if ($HoldProject) {
    $scope = Enter-IGArtifactScope -ProjectRoot $HoldProject -Name '다른 프로세스'
    try {
        [IO.File]::WriteAllText($ReadyFile, 'ready')
        $until = [DateTime]::UtcNow.AddSeconds(20)
        while (-not (Test-Path -LiteralPath $StopFile) -and [DateTime]::UtcNow -lt $until) { Start-Sleep -Milliseconds 50 }
    }
    finally { $scope.Lease.Dispose() }
    return
}

$parent = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Saved/Validation'))
$fixture = Join-Path $parent ('ArtifactCleanupTest-' + [Guid]::NewGuid().ToString('N'))
$project = Join-Path $fixture 'Project'
$checks = 0
$child = $null
$heldFile = $null
$junction = $null

function Assert-Cleanup([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "자동 정리 검사 실패: $Message" }
    $script:checks++
}

function Write-Fixture([string]$Path, [string]$Text = '검사용 내용') {
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path, $Text)
}

function New-Run([string]$Name, [bool]$Success, [string]$Kind = 'Validation', [string]$Group = '화면 검사') {
    $scope = Enter-IGArtifactScope -ProjectRoot $project -Name $Group
    $dir = Join-Path $project ("Saved/$Kind/$Name")
    if ($Kind -eq 'Package') { $dir = Join-Path $project "Saved/Packages/$Name" }
    Add-IGArtifactDirectory -Scope $scope -Directory $dir
    $relative = if ($Kind -eq 'Package') { 'Windows/Game.pak' } else { 'shot.png' }
    Write-Fixture (Join-Path $dir $relative)
    Write-Fixture (Join-Path $dir 'User/Saved/SaveGames/AutoSave_0.sav')
    Write-Fixture (Join-Path $dir 'User/Saved/SaveGames/AutoSave_0.sav.bak')
    Write-Fixture (Join-Path $dir 'Originals/source.png')
    Write-Fixture (Join-Path $dir 'Source/mesh.cpp')
    Write-Fixture (Join-Path $dir 'summary.json') '{}'
    Complete-IGArtifactScope -Scope $scope -Succeeded $Success
    return [pscustomobject]@{ Directory = $dir; File = (Join-Path $dir $relative); Id = $scope.Runs[0].Id }
}

function New-TrailerRun([string]$Name) {
    $scope = Enter-IGArtifactScope -ProjectRoot $project -Name '예고편'
    $dir = Join-Path $project "Saved/Trailer/Runs/$Name"
    Add-IGArtifactDirectory -Scope $scope -Directory $dir -Variant 'Trailer'
    foreach ($relative in @('alley/frame_0001.png', 'trailer_audio.wav', 'trailer_mix.wav', 'Trailer-all.log',
        'MissingFloor-Trailer.mp4', 'MissingFloor-Trailer-web.mp4', 'still-ko-1.5.png', 'cards/ko/knock.png', 'edl.json',
        'User/Saved/SaveGames/AutoSave_0.sav', 'User/Saved/Logs/IndieGame.log')) {
        Write-Fixture (Join-Path $dir $relative)
    }
    Complete-IGArtifactScope -Scope $scope -Succeeded $true
    return [pscustomobject]@{ Directory = $dir; Id = $scope.Runs[0].Id }
}

function Age-Run($Run, [int]$Days) {
    $path = Join-Path $project ("Saved/ArtifactCleanup/$($Run.Id).json")
    $record = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
    $record.finishedUtc = [DateTime]::UtcNow.AddDays(-$Days).ToString('o')
    [IO.File]::WriteAllText($path, ($record | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
}

try {
    Write-Fixture (Join-Path $project 'IndieGame.uproject') '{}'
    Write-Fixture (Join-Path $project 'Content/original.png')
    Write-Fixture (Join-Path $project 'Saved/Releases/latest.zip')
    Write-Fixture (Join-Path $project 'Saved/SaveGames/manual.sav')

    $runs = @(1..5 | ForEach-Object { New-Run "Run$_" $true })
    $failed = @(1..2 | ForEach-Object { New-Run "Failed$_" $false })
    $packages = @(1..3 | ForEach-Object { New-Run "Package$_" $true 'Package' '배포 검사' })
    for ($index = 0; $index -lt $runs.Count; $index++) { Age-Run $runs[$index] (10 - $index) }
    Age-Run $failed[0] 5; Age-Run $failed[1] 4
    for ($index = 0; $index -lt $packages.Count; $index++) { Age-Run $packages[$index] (8 - $index) }

    # 완료 뒤 바뀐 파일은 크기와 수정 시각을 되돌려도 해시가 달라 삭제하지 않아야 한다.
    $changed = $runs[1].File
    $oldInfo = Get-Item -LiteralPath $changed
    [IO.File]::WriteAllBytes($changed, [byte[]](1..$oldInfo.Length))
    [IO.File]::SetLastWriteTimeUtc($changed, $oldInfo.LastWriteTimeUtc)
    Write-Fixture (Join-Path $runs[1].Directory 'added-later.png')
    Write-Fixture (Join-Path $runs[2].Directory '.keep')

    # 목록이 잘못 만들어져도 허용된 출력 폴더 밖의 파일을 지우면 안 된다.
    $forged = Get-Content -LiteralPath (Join-Path $project "Saved/ArtifactCleanup/$($runs[0].Id).json") -Raw -Encoding UTF8 | ConvertFrom-Json
    $forged.id = [Guid]::NewGuid().ToString('N')
    $forged.directory = Join-Path $fixture 'NotGenerated'
    $forged.kind = ''
    $forged.finishedUtc = [DateTime]::UtcNow.AddDays(-20).ToString('o')
    Write-Fixture (Join-Path $forged.directory '.ig-artifact-owner') $forged.id
    Write-Fixture (Join-Path $forged.directory 'shot.png')
    [IO.File]::SetLastWriteTimeUtc((Join-Path $forged.directory 'shot.png'), [DateTime]::new([long]$forged.files[0].ticks, [DateTimeKind]::Utc))
    Write-Fixture (Join-Path $project "Saved/ArtifactCleanup/$($forged.id).json") ($forged | ConvertTo-Json -Depth 6)

    $scope = Enter-IGArtifactScope -ProjectRoot $project -Name '실행 중'
    try {
        $busy = Invoke-IGArtifactCleanup -ProjectRoot $project
        Assert-Cleanup $busy.Busy '같은 프로세스의 진행 중 작업을 보호하지 못함'
        Assert-Cleanup (Test-Path -LiteralPath $runs[0].File) '진행 중에 파일을 삭제함'
        $nested = Join-Path $project 'Saved/Validation/Outer'
        Add-IGArtifactDirectory -Scope $scope -Directory $nested
        $inner = Enter-IGArtifactScope -ProjectRoot $project -Name '안쪽 검사'
        try {
            Add-IGArtifactDirectory -Scope $inner -Directory (Join-Path $nested 'Frontend')
            Assert-Cleanup ($inner.Runs.Count -eq 0) '부모와 자식 폴더를 중복 등록함'
        }
        finally { $inner.Lease.Dispose() }
        foreach ($protected in @('Content', 'Saved/Releases', 'Saved/SaveGames')) {
            Add-IGArtifactDirectory -Scope $scope -Directory (Join-Path $project "$protected/New")
        }
        Assert-Cleanup ($scope.Runs.Count -eq 1) '원본·배포 보관·세이브 폴더를 등록함'
    }
    finally { $scope.Lease.Dispose() }

    # 다른 PowerShell 프로세스가 공유 잠금을 잡고 있어도 정리를 시작할 수 없어야 한다.
    $ready = Join-Path $fixture 'ready.txt'; $stop = Join-Path $fixture 'stop.txt'
    $shell = (Get-Process -Id $PID).Path
    $args = @('-NoProfile', '-File', ('"' + $PSCommandPath + '"'), '-HoldProject', ('"' + $project + '"'),
        '-ReadyFile', ('"' + $ready + '"'), '-StopFile', ('"' + $stop + '"'))
    $child = Start-Process -FilePath $shell -ArgumentList $args -WindowStyle Hidden -PassThru
    $until = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path -LiteralPath $ready) -and -not $child.HasExited -and [DateTime]::UtcNow -lt $until) { Start-Sleep -Milliseconds 50 }
    Assert-Cleanup (Test-Path -LiteralPath $ready) '다른 프로세스 잠금 준비 실패'
    Assert-Cleanup (Invoke-IGArtifactCleanup -ProjectRoot $project).Busy '다른 프로세스의 진행 중 작업을 보호하지 못함'
    Write-Fixture $stop
    Assert-Cleanup ($child.WaitForExit(10000)) '검사용 프로세스가 종료되지 않음'
    $child.Dispose(); $child = $null

    $preview = Invoke-IGArtifactCleanup -ProjectRoot $project -WhatIf
    Assert-Cleanup ($preview.DeletedFiles -eq 0 -and (Test-Path -LiteralPath $runs[0].File)) '미리 보기에서 삭제함'
    $heldFile = [IO.File]::Open($runs[0].File, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    $first = Invoke-IGArtifactCleanup -ProjectRoot $project
    Assert-Cleanup (Test-Path -LiteralPath $runs[0].File) '열려 있는 파일을 삭제함'
    Assert-Cleanup (-not (Test-Path -LiteralPath $failed[0].File)) '오래된 실패 결과를 정리하지 않음'
    Assert-Cleanup (-not (Test-Path -LiteralPath $packages[0].File)) '오래된 패키지를 정리하지 않음'
    $heldFile.Dispose(); $heldFile = $null
    $second = Invoke-IGArtifactCleanup -ProjectRoot $project
    Assert-Cleanup (-not (Test-Path -LiteralPath $runs[0].File)) '잠금이 풀린 파일을 정리하지 않음'
    Assert-Cleanup ($second.DeletedFiles -eq 1 -and $second.DeletedBytes -gt 0) '삭제 건수·용량 집계 오류'
    Assert-Cleanup (Test-Path -LiteralPath (Join-Path $forged.directory 'shot.png')) '잘못된 기록으로 범위 밖 파일을 삭제함'
    foreach ($file in @($changed, $runs[2].File, $runs[3].File, $runs[4].File, $failed[1].File, $packages[1].File, $packages[2].File,
        (Join-Path $runs[1].Directory 'added-later.png'), (Join-Path $runs[0].Directory 'User/Saved/SaveGames/AutoSave_0.sav'),
        (Join-Path $runs[0].Directory 'User/Saved/SaveGames/AutoSave_0.sav.bak'), (Join-Path $runs[0].Directory 'Originals/source.png'),
        (Join-Path $runs[0].Directory 'Source/mesh.cpp'), (Join-Path $runs[0].Directory 'summary.json'),
        (Join-Path $project 'Content/original.png'), (Join-Path $project 'Saved/Releases/latest.zip'), (Join-Path $project 'Saved/SaveGames/manual.sav'))) {
        Assert-Cleanup (Test-Path -LiteralPath $file) "보호 대상이 사라짐: $file"
    }

    $recent = @(1..3 | ForEach-Object { New-Run "Recent$_" $true 'Validation' '최근 검사' })
    Assert-Cleanup (Test-Path -LiteralPath $recent[0].File) '하루가 안 지난 결과를 삭제함'
    $scope = Enter-IGArtifactScope -ProjectRoot $project -Name '기존 폴더'
    try {
        Add-IGArtifactDirectory -Scope $scope -Directory $runs[0].Directory
        Assert-Cleanup ($scope.Runs.Count -eq 0) '기존 파일의 소유권을 가져옴'
    }
    finally { $scope.Lease.Dispose() }

    # 오래된 빌드를 다시 검수에 쓰면 종료 뒤에도 하루 동안 남긴다.
    $reused = @(1..3 | ForEach-Object { New-Run "Reused$_" $true 'Package' '재사용 빌드' })
    for ($index = 0; $index -lt $reused.Count; $index++) { Age-Run $reused[$index] (8 - $index) }
    $scope = Enter-IGArtifactScope -ProjectRoot $project -Name '재검수' -Parameters @{ ArchiveDirectory = $reused[0].Directory }
    $usage = Join-Path $project ("Saved/ArtifactCleanup/$($reused[0].Id).use")
    Assert-Cleanup (Test-Path -LiteralPath $usage) '기존 빌드의 사용 시각을 기록하지 않음'
    [IO.File]::SetLastWriteTimeUtc($usage, [DateTime]::UtcNow.AddDays(-2))
    Complete-IGArtifactScope -Scope $scope -Succeeded $true
    Assert-Cleanup (Test-Path -LiteralPath $reused[0].File) '검수에 다시 쓴 빌드를 종료 직후 삭제함'
    [IO.File]::SetLastWriteTimeUtc($usage, [DateTime]::UtcNow.AddDays(-2))
    $expired = Invoke-IGArtifactCleanup -ProjectRoot $project
    Assert-Cleanup (-not (Test-Path -LiteralPath $reused[0].File) -and -not (Test-Path -LiteralPath $usage)) '재사용 보호 기간이 끝난 임시 결과를 정리하지 않음'

    # 예고편은 실행마다 새 폴더(Saved/Trailer/Runs)만 맡는다. 오래된 실행에서는 다시 찍으면 되는 장면 프레임과
    # 중간 소리, 기록만 지우고 영상·정지 화면·글자 판·편집표·세이브는 남긴다. 예전 Saved/Trailer는 등록하지 않는다.
    $legacyShot = Join-Path $project 'Saved/Trailer/alley'
    Write-Fixture (Join-Path $legacyShot 'frame_0001.png')
    $scope = Enter-IGArtifactScope -ProjectRoot $project -Name '예전 예고편'
    try {
        Add-IGArtifactDirectory -Scope $scope -Directory $legacyShot
        Add-IGArtifactDirectory -Scope $scope -Directory (Join-Path $project 'Saved/Trailer/New')
        Assert-Cleanup ($scope.Runs.Count -eq 0) '예전 예고편 폴더를 정리 대상으로 등록함'
    }
    finally { $scope.Lease.Dispose() }
    $trailers = @(1..3 | ForEach-Object { New-TrailerRun "Trailer$_" })
    Assert-Cleanup (Test-Path -LiteralPath (Join-Path $trailers[0].Directory 'alley/frame_0001.png')) '막 만든 예고편 프레임을 지움'
    for ($index = 0; $index -lt $trailers.Count; $index++) { Age-Run $trailers[$index] (6 - $index) }
    [void](Invoke-IGArtifactCleanup -ProjectRoot $project)
    foreach ($gone in @('alley/frame_0001.png', 'trailer_audio.wav', 'trailer_mix.wav', 'Trailer-all.log')) {
        Assert-Cleanup (-not (Test-Path -LiteralPath (Join-Path $trailers[0].Directory $gone))) "오래된 예고편 중간 파일을 정리하지 않음: $gone"
    }
    foreach ($kept in @('MissingFloor-Trailer.mp4', 'MissingFloor-Trailer-web.mp4', 'still-ko-1.5.png', 'cards/ko/knock.png',
        'edl.json', 'User/Saved/SaveGames/AutoSave_0.sav', 'User/Saved/Logs/IndieGame.log')) {
        Assert-Cleanup (Test-Path -LiteralPath (Join-Path $trailers[0].Directory $kept)) "예고편 결과를 지움: $kept"
    }
    foreach ($trailer in @($trailers[1], $trailers[2])) {
        Assert-Cleanup (Test-Path -LiteralPath (Join-Path $trailer.Directory 'alley/frame_0001.png')) '최근 예고편 두 번의 프레임을 지움'
    }
    Assert-Cleanup (Test-Path -LiteralPath (Join-Path $legacyShot 'frame_0001.png')) '예전 예고편 프레임을 지움'

    # 연결 폴더와 상위 경로가 들어간 조작된 목록도 보호한다.
    $scope = Enter-IGArtifactScope -ProjectRoot $project -Name '연결 경로'
    try {
        $junction = Join-Path $project 'Saved/Validation/Linked'
        New-Item -ItemType Junction -Path $junction -Target (Join-Path $project 'Content') | Out-Null
        Add-IGArtifactDirectory -Scope $scope -Directory (Join-Path $junction 'New')
        Assert-Cleanup ($scope.Runs.Count -eq 0) '연결 폴더를 등록함'
        Assert-Cleanup (-not (Test-IGArtifactFile '../Content/original.png' 'Validation')) '상위 경로를 허용함'
        Assert-Cleanup (-not (Test-IGArtifactFile 'C:/other/file.png' 'Validation')) '절대 경로를 허용함'
    }
    finally { $scope.Lease.Dispose() }
    # 실제 검사 진입점을 작은 가짜 실행 파일에 연결해 성공·실패 양쪽의 finally를 확인한다.
    $fixtureScripts = Join-Path $project 'Scripts'
    [void][IO.Directory]::CreateDirectory($fixtureScripts)
    foreach ($name in @('GeneratedArtifactCleanup.ps1', 'Run-LiningProbe.ps1')) {
        [IO.File]::WriteAllText((Join-Path $fixtureScripts $name), [IO.File]::ReadAllText((Join-Path $PSScriptRoot $name)), [Text.UTF8Encoding]::new($true))
    }
    Write-Fixture (Join-Path $fixtureScripts 'Resolve-UnrealEditor.ps1') @'
param([switch]$Commandlet)
Join-Path $PSScriptRoot 'fake-editor.ps1'
'@
    Write-Fixture (Join-Path $fixtureScripts 'fake-editor.ps1') @'
if ($args -notcontains '-RenderOffscreen') { throw '검사용 실행은 화면 밖에서 돌아야 합니다.' }
$log = ($args | Where-Object { ([string]$_).StartsWith('-abslog=') }).Substring(8)
$user = ($args | Where-Object { ([string]$_).StartsWith('-UserDir=') }).Substring(9)
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $log))
[void][IO.Directory]::CreateDirectory($user)
[IO.File]::WriteAllText((Join-Path $user 'shot.png'), 'frame')
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'fail.flag')) {
    [IO.File]::WriteAllText($log, 'LINING_PROBE FAIL failures=1')
    $global:LASTEXITCODE = 1
}
else {
    [IO.File]::WriteAllText($log, 'LINING_PROBE PASS failures=0')
    $global:LASTEXITCODE = 0
}
'@
    & (Join-Path $fixtureScripts 'Run-LiningProbe.ps1') -FrameRate 30
    Write-Fixture (Join-Path $fixtureScripts 'fail.flag')
    $failure = $null
    try { & (Join-Path $fixtureScripts 'Run-LiningProbe.ps1') -FrameRate 30 }
    catch { $failure = $_.Exception.Message }
    Assert-Cleanup ($failure -like '덧댄 것 검사에 실패했습니다:*') '정리가 원래 검사 오류를 덮어씀'
    $integration = @(Get-ChildItem -LiteralPath (Join-Path $project 'Saved/ArtifactCleanup') -Filter '*.json' -File |
        ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json } |
        Where-Object { $_.group -match 'Run-LiningProbe' })
    Assert-Cleanup ($integration.Count -eq 2) '검사 종료 기록이 빠짐'
    Assert-Cleanup (@($integration | Where-Object { $_.succeeded }).Count -eq 1) '성공 기록 오류'
    Assert-Cleanup (@($integration | Where-Object { -not $_.succeeded }).Count -eq 1) '실패 기록 오류'
    Assert-Cleanup (-not (Invoke-IGArtifactCleanup -ProjectRoot $project).Busy) '오류 뒤 실행 잠금이 남음'

    # 예고편 단독 촬영도 실행 폴더·소유 기록·잠금·종료 기록을 갖는다. 가짜 엔진은 Start-Process로 뜨는
    # 배치 파일이다. 성공, 실패, 시간 초과, 부모(Build-Trailer)가 넘긴 실행 폴더를 차례로 본다.
    Remove-Item -LiteralPath (Join-Path $fixtureScripts 'fail.flag')
    $fixtureTrailer = Join-Path $fixtureScripts 'Trailer'
    [void][IO.Directory]::CreateDirectory($fixtureTrailer)
    [IO.File]::WriteAllText((Join-Path $fixtureTrailer 'Run-TrailerCapture.ps1'),
        [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Trailer/Run-TrailerCapture.ps1')), [Text.UTF8Encoding]::new($true))
    Write-Fixture (Join-Path $fixtureScripts 'Resolve-UnrealEditor.ps1') @'
param([switch]$Commandlet)
Join-Path $PSScriptRoot 'fake-trailer-editor.cmd'
'@
    [IO.File]::WriteAllText((Join-Path $fixtureScripts 'fake-trailer-editor.cmd'),
        "@powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0fake-trailer-editor.ps1`" %*`r`n@exit /b %errorlevel%`r`n")
    Write-Fixture (Join-Path $fixtureScripts 'fake-trailer-editor.ps1') @'
# 엔진처럼 실행 줄 전체를 읽는다. -File로 넘긴 인자는 PowerShell이 다시 나누므로 쓰지 않는다.
$line = [Environment]::CommandLine
function Get-Arg([string]$Name) { if ($line -match "-$Name=([^`"\s]+)") { $Matches[1] } else { '' } }
if ($line -notmatch '(^|\s)-RenderOffscreen(\s|$)') { exit 3 }
$out = Get-Arg 'IGTrailerOut'
$log = Get-Arg 'abslog'
$user = Get-Arg 'UserDir'
if (-not $out -or -not $log -or -not $user) { exit 4 }
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'hang.flag')) { Start-Sleep -Seconds 60 }
$shots = @((Get-Arg 'IGTrailerShot').Split(',') | Where-Object { $_ })
if (-not $shots.Count) { $shots = @('alley') }
foreach ($shot in $shots) {
    [void][IO.Directory]::CreateDirectory((Join-Path $out $shot))
    [IO.File]::WriteAllText((Join-Path $out "$shot/frame_0000.png"), 'frame')
}
[void][IO.Directory]::CreateDirectory((Join-Path $user 'Saved/SaveGames'))
[IO.File]::WriteAllText((Join-Path $user 'Saved/SaveGames/AutoSave_0.sav'), 'save')
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'fail.flag')) {
    [IO.File]::WriteAllText($log, 'TRAILER_CAPTURE FAIL reason=test')
    exit 1
}
[IO.File]::WriteAllText($log, 'TRAILER_CAPTURE PASS dropped=0')
exit 0
'@
    $trailerCapture = Join-Path $fixtureTrailer 'Run-TrailerCapture.ps1'
    $runsRoot = Join-Path $project 'Saved/Trailer/Runs'
    function Get-TrailerRecords {
        @(Get-ChildItem -LiteralPath (Join-Path $project 'Saved/ArtifactCleanup') -Filter '*.json' -File |
            ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json } |
            Where-Object { $_.group -match 'Run-TrailerCapture' })
    }
    $before = @(Get-ChildItem -LiteralPath $runsRoot -Directory).Count
    # TRAILER_RUN은 Write-Host로 알리므로 정보 스트림을 받아 읽는다.
    $standalone = @(& $trailerCapture -Shot door-402,booth-cctv -TimeoutSeconds 60 6>&1 | ForEach-Object { [string]$_ } |
        Where-Object { $_ -like 'TRAILER_RUN *' })
    Assert-Cleanup ($standalone.Count -eq 1) '단독 촬영이 실행 폴더를 알리지 않음'
    $standaloneRun = $standalone[0].Substring(12)
    Assert-Cleanup ((Split-Path -Parent $standaloneRun) -eq [IO.Path]::GetFullPath($runsRoot) -and
        (Split-Path -Leaf $standaloneRun) -match '^\d{8}-\d{6}-\d{3}-door-402_booth-cctv$') "단독 촬영 실행 폴더 이름이 다름: $standaloneRun"
    foreach ($made in @('door-402/frame_0000.png', 'booth-cctv/frame_0000.png', 'Trailer-door-402_booth-cctv.log',
        'User/Saved/SaveGames/AutoSave_0.sav', '.ig-artifact-owner')) {
        Assert-Cleanup (Test-Path -LiteralPath (Join-Path $standaloneRun $made)) "단독 촬영 결과가 실행 폴더에 없음: $made"
    }
    Assert-Cleanup (-not (Test-Path -LiteralPath (Join-Path $project 'Saved/Trailer/door-402')) -and
        -not (Test-Path -LiteralPath (Join-Path $project 'Saved/Validation/Trailer-User')) -and
        -not (Test-Path -LiteralPath (Join-Path $project 'Saved/Logs/Trailer-door-402_booth-cctv.log'))) '단독 촬영이 예전 고정 경로에 씀'
    $records = @(Get-TrailerRecords)
    Assert-Cleanup ($records.Count -eq 1 -and $records[0].succeeded -and $records[0].directory -eq $standaloneRun) '단독 촬영 성공 기록 오류'
    Assert-Cleanup (@($records[0].files | Where-Object { $_.path -eq 'door-402/frame_0000.png' }).Count -eq 1) '단독 촬영 프레임이 정리 목록에 없음'
    Assert-Cleanup (Test-Path -LiteralPath (Join-Path $standaloneRun 'door-402/frame_0000.png')) '단독 촬영이 끝나자마자 프레임을 지움'

    Write-Fixture (Join-Path $fixtureScripts 'fail.flag')
    $failure = $null
    try { & $trailerCapture -Shot door-402 -TimeoutSeconds 60 | Out-Null }
    catch { $failure = $_.Exception.Message }
    Remove-Item -LiteralPath (Join-Path $fixtureScripts 'fail.flag')
    Assert-Cleanup ($failure -like '트레일러 촬영 실패:*') "단독 촬영 실패가 원래 오류로 남지 않음: $failure"
    Write-Fixture (Join-Path $fixtureScripts 'hang.flag')
    $failure = $null
    try { & $trailerCapture -Shot booth-cctv -TimeoutSeconds 3 | Out-Null }
    catch { $failure = $_.Exception.Message }
    Remove-Item -LiteralPath (Join-Path $fixtureScripts 'hang.flag')
    Assert-Cleanup ($failure -like '트레일러 촬영 시간 초과:*') "단독 촬영 시간 초과가 원래 오류로 남지 않음: $failure"
    $records = @(Get-TrailerRecords)
    Assert-Cleanup ($records.Count -eq 3 -and @($records | Where-Object { -not $_.succeeded }).Count -eq 2) '단독 촬영 실패·시간 초과 기록이 빠짐'
    $hungRun = @($records | Where-Object { $_.directory -like '*-booth-cctv' })[0].directory
    Assert-Cleanup (-not (Test-IGArtifactInUse $hungRun @(Get-IGArtifactProcesses))) '시간 초과한 가짜 엔진이 남아 있음'
    Assert-Cleanup (-not (Invoke-IGArtifactCleanup -ProjectRoot $project).Busy) '단독 촬영 뒤 실행 잠금이 남음'

    # 부모가 등록한 실행 폴더에 찍을 때는 잠금·기록·정리를 부모에게 맡긴다. 부모 범위가 열린 동안
    # 정리는 잠금 때문에 돌지 않고, 기록은 부모 것 하나만 남는다.
    $parentScope = Enter-IGArtifactScope -ProjectRoot $project -Name 'Build-Trailer.ps1'
    $managedRun = Join-Path $runsRoot 'Managed'
    try {
        Add-IGArtifactDirectory -Scope $parentScope -Directory $managedRun -Variant 'Trailer'
        & $trailerCapture -OutputDirectory $managedRun -TimeoutSeconds 60 | Out-Null
        Assert-Cleanup (Test-Path -LiteralPath (Join-Path $managedRun 'alley/frame_0000.png')) '부모 실행 폴더에 프레임이 없음'
        Assert-Cleanup (Test-Path -LiteralPath (Join-Path $managedRun 'Trailer-all.log')) '부모 실행 폴더에 기록이 없음'
        Assert-Cleanup ((Invoke-IGArtifactCleanup -ProjectRoot $project).Busy) '부모가 쓰는 중인데 정리가 돌았음'
        Assert-Cleanup (@(Get-TrailerRecords).Count -eq 3) '부모가 넘긴 실행 폴더를 자식이 따로 기록함'
    }
    finally { Complete-IGArtifactScope -Scope $parentScope -Succeeded $true }
    $managedRecords = @(Get-ChildItem -LiteralPath (Join-Path $project 'Saved/ArtifactCleanup') -Filter '*.json' -File |
        ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json } |
        Where-Object { $_.directory -eq [IO.Path]::GetFullPath($managedRun) })
    Assert-Cleanup ($managedRecords.Count -eq 1 -and $managedRecords[0].group -match 'Build-Trailer') '부모 실행 폴더 기록 오류'
    $orphan = Join-Path $runsRoot 'Orphan'
    [void][IO.Directory]::CreateDirectory($orphan)
    $failure = $null
    try { & $trailerCapture -OutputDirectory $orphan -TimeoutSeconds 60 | Out-Null }
    catch { $failure = $_.Exception.Message }
    Assert-Cleanup ($failure -like '정리 기록이 없는 실행 폴더에는 찍지 않습니다*' -and
        -not (Test-Path -LiteralPath (Join-Path $orphan 'alley'))) '정리 기록 없는 실행 폴더에 찍음'

    # 같은 장면 묶음의 단독 촬영은 최근 성공 두 번을 남기고, 더 오래된 실행에서는 프레임과 기록만 지운다.
    $repeated = @(1..3 | ForEach-Object {
        Start-Sleep -Milliseconds 20
        @(& $trailerCapture -Shot alley -TimeoutSeconds 60 6>&1 | ForEach-Object { [string]$_ } |
            Where-Object { $_ -like 'TRAILER_RUN *' })[0].Substring(12)
    })
    $repeatedRecords = @(Get-TrailerRecords | Where-Object { $repeated -contains $_.directory } | Sort-Object { $repeated.IndexOf($_.directory) })
    for ($index = 0; $index -lt $repeatedRecords.Count; $index++) { Age-Run ([pscustomobject]@{ Id = $repeatedRecords[$index].id }) (5 - $index) }
    [void](Invoke-IGArtifactCleanup -ProjectRoot $project)
    Assert-Cleanup (-not (Test-Path -LiteralPath (Join-Path $repeated[0] 'alley/frame_0000.png')) -and
        -not (Test-Path -LiteralPath (Join-Path $repeated[0] 'Trailer-alley.log'))) '오래된 단독 촬영 프레임을 정리하지 않음'
    Assert-Cleanup (Test-Path -LiteralPath (Join-Path $repeated[0] 'User/Saved/SaveGames/AutoSave_0.sav')) '단독 촬영 세이브를 지움'
    foreach ($recent in @($repeated[1], $repeated[2])) {
        Assert-Cleanup (Test-Path -LiteralPath (Join-Path $recent 'alley/frame_0000.png')) '최근 단독 촬영 두 번의 프레임을 지움'
    }
    Assert-Cleanup (Test-Path -LiteralPath (Join-Path $standaloneRun 'door-402/frame_0000.png')) '다른 장면 묶음의 단독 촬영 프레임을 지움'
    Write-Host "ARTIFACT_CLEANUP_TEST PASS checks=$checks"
}
finally {
    if ($heldFile) { $heldFile.Dispose() }
    if ($child) { if (-not $child.HasExited) { $child.Kill(); [void]$child.WaitForExit(5000) }; $child.Dispose() }
    if ($junction -and (Test-Path -LiteralPath $junction)) { [IO.Directory]::Delete($junction) }
    $resolved = [IO.Path]::GetFullPath($fixture)
    if ((Split-Path -Parent $resolved) -ne $parent -or (Split-Path -Leaf $resolved) -notmatch '^ArtifactCleanupTest-[0-9a-f]{32}$') { throw '검사용 임시 폴더의 경로가 달라졌습니다.' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
