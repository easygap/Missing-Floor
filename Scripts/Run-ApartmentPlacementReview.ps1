[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [ValidateRange(640, 3840)][int]$Width = 1920,
    [ValidateRange(360, 2160)][int]$Height = 1080,
    [ValidateRange(60, 180)][int]$TimeoutSeconds = 180
)

# 실제 403호의 소품 접지와 여섯 시점을 검사한다. 창을 만들지 않고 전용 사용자 폴더에 저장한다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$scope = Enter-IGArtifactScope -ProjectRoot $projectRoot -Name 'Run-ApartmentPlacementReview.ps1' -Parameters $PSBoundParameters
$succeeded = $false
$summaryPath = $null
$process = $null
$settingsSnapshots = @()
$summary = [ordered]@{
    schemaVersion = 1
    evidenceKind = 'Editor Development'
    shippingValidated = $false
    generatedAtUtc = [DateTime]::UtcNow.ToString('o')
    offscreen = $true
    audioEnabled = $false
    windowStyle = 'Hidden'
    resolution = "${Width}x${Height}"
    status = 'running'
    screenshots = @()
}
try {
    $projectFile = Join-Path $projectRoot 'IndieGame.uproject'
    $editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $projectFile -Commandlet
    if (-not $editor -or [IO.Path]::GetFileName($editor) -ine 'UnrealEditor-Cmd.exe' -or
        -not (Test-Path -LiteralPath $editor -PathType Leaf)) {
        throw '화면을 띄우지 않는 UnrealEditor-Cmd.exe를 찾지 못했습니다.'
    }
    if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
        $OutputDirectory = Join-Path $projectRoot ('Saved/Validation/ApartmentPlacement-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    }
    $outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
    if (Test-Path -LiteralPath $outputRoot) { throw "이전 결과를 덮어쓰지 않도록 새 폴더를 지정해 주세요: $outputRoot" }
    if ((Get-IGArtifactKind $projectRoot $outputRoot) -ne 'Validation') {
        throw '검사 결과는 Saved/Validation 아래 새 폴더에 저장해 주세요.'
    }
    Add-IGArtifactDirectory -Scope $scope -Directory $outputRoot -Variant 'ApartmentPlacement'
    if (-not (Test-Path -LiteralPath (Join-Path $outputRoot '.ig-artifact-owner'))) {
        throw "검사 결과 폴더를 등록하지 못했습니다: $outputRoot"
    }
    $summaryPath = Join-Path $outputRoot 'summary.json'
    $userRoot = Join-Path $outputRoot 'User'
    $captureRoot = Join-Path $userRoot 'Saved/ApartmentPlacementReview'
    $logPath = Join-Path $outputRoot 'editor.log'
    [void][IO.Directory]::CreateDirectory($userRoot)
    $summary.editor = $editor
    $summary.project = $projectFile
    $summary.sourceCommit = (& git -C $projectRoot rev-parse HEAD).Trim()
    $summary.moduleSha256 = (Get-FileHash -LiteralPath (Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-IndieGame.dll') -Algorithm SHA256).Hash
    $summary.harnessSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
    $summary.userDirectory = $userRoot

    # UserDir로 검사 설정을 분리한다. 에디터가 공용 설정에 쓰는 경우에도 원래 바이트를 복구한다.
    $settingsSnapshots = @(
        foreach ($relative in @('Saved/Config/WindowsEditor/GameUserSettings.ini', 'Saved/Config/Windows/GameUserSettings.ini')) {
            $path = Join-Path $projectRoot $relative
            $exists = Test-Path -LiteralPath $path -PathType Leaf
            [pscustomobject]@{ Path = $path; Existed = $exists; Bytes = $(if ($exists) { [IO.File]::ReadAllBytes($path) } else { $null }) }
        }
    )
    $arguments = @(
        $projectFile, '-game', '-unattended', '-nosplash', '-NoLoadingScreen',
        '-IGMissingFloor', '-IGArrivalCapture', '-IGApartmentPlacementReview', '-IGSkipFrontend',
        '-RenderOffscreen', '-nosound', '-d3d12', '-Windowed', "-ResX=$Width", "-ResY=$Height", '-ForceRes', '-NoVSync',
        '-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100',
        "-UserDir=$userRoot", "-abslog=$logPath"
    )
    if ($arguments -notcontains '-RenderOffscreen' -or $arguments -notcontains '-nosound') {
        throw '화면과 소리를 끈 상태에서만 검사할 수 있습니다.'
    }
    $quoted = @($arguments | ForEach-Object {
        if ($_.Contains('"')) { throw '실행 경로와 인자에는 큰따옴표를 넣을 수 없습니다.' }
        '"' + $_ + '"'
    })
    Write-Host "APARTMENT_PLACEMENT_EDITOR start resolution=${Width}x${Height} offscreen=1"
    $process = Start-Process -FilePath $editor -ArgumentList $quoted -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        [void]$process.WaitForExit(5000)
        throw '403호 소품 배치 검사 시간이 초과됐습니다.'
    }
    if ($process.ExitCode -ne 0) { throw "403호 소품 배치 검사 종료 코드: $($process.ExitCode)" }
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) { throw '403호 소품 배치 검사 로그가 없습니다.' }
    $logText = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
    $receipt = [regex]::Match($logText, 'APARTMENT_PLACEMENT_REVIEW PASS shots=6 geometry=1\b')
    if (-not $receipt.Success -or $logText -match 'APARTMENT_PLACEMENT_REVIEW FAIL|Failed to compile Material|Fatal error:') {
        throw "소품 접지 검사나 장면 촬영이 완료되지 않았습니다: $logPath"
    }
    $summary.receipt = $receipt.Value
    foreach ($name in @('apartment-wide', 'bedside-front', 'bedside-side', 'desk-front', 'desk-side', 'kitchen')) {
        $path = Join-Path $captureRoot ($name + '.png')
        if (-not (Test-IGArtifactPath $path $captureRoot) -or
            -not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "403호 캡처 경로를 확인하지 못했습니다: $path"
        }
        $stream = [IO.File]::OpenRead($path)
        try {
            $header = [byte[]]::new(24)
            if ($stream.Read($header, 0, 24) -ne 24 -or
                [BitConverter]::ToString($header, 0, 8) -cne '89-50-4E-47-0D-0A-1A-0A') {
                throw "PNG 파일을 읽을 수 없습니다: $path"
            }
            $actualWidth = [Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($header, 16))
            $actualHeight = [Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($header, 20))
        }
        finally { $stream.Dispose() }
        if ($actualWidth -ne $Width -or $actualHeight -ne $Height) { throw "403호 캡처 크기가 다릅니다: $path" }
        $summary.screenshots += [ordered]@{
            name = $name + '.png'
            path = $path
            width = $actualWidth
            height = $actualHeight
            bytes = (Get-Item -LiteralPath $path).Length
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        }
    }
    $summary.logSha256 = (Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash
    $summary.status = 'passed'
    $succeeded = $true
    Write-Host "APARTMENT_PLACEMENT_EDITOR PASS shots=6 geometry=1 evidence=EditorDevelopment summary=$summaryPath"
}
catch {
    $summary.status = 'failed'
    $summary.error = $_.Exception.Message
    throw
}
finally {
    try {
        if ($process) {
            if (-not $process.HasExited) {
                & taskkill.exe /PID $process.Id /T /F | Out-Null
                [void]$process.WaitForExit(5000)
            }
            $process.Dispose()
        }
        foreach ($snapshot in $settingsSnapshots) {
            if ($snapshot.Existed) {
                [IO.File]::WriteAllBytes($snapshot.Path, $snapshot.Bytes)
            }
            elseif (Test-Path -LiteralPath $snapshot.Path -PathType Leaf) {
                Remove-Item -LiteralPath $snapshot.Path -ErrorAction Stop
            }
        }
        if ($summaryPath) {
            [IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 6) + "`n", [Text.UTF8Encoding]::new($false))
        }
    }
    finally { Complete-IGArtifactScope -Scope $scope -Succeeded $succeeded }
}
