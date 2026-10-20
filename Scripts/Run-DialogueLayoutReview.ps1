[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [ValidateSet('720p', '900p', '1080p', '1440p')]
    [string[]]$Resolutions = @('720p', '900p', '1080p', '1440p'),
    [string]$OutputDirectory,
    [ValidateRange(30, 300)][int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$scope = Enter-IGArtifactScope -ProjectRoot $projectRoot -Name 'Run-DialogueLayoutReview.ps1' -Parameters $PSBoundParameters
$succeeded = $false
$summaryPath = $null
$results = [Collections.Generic.List[object]]::new()
$summary = [ordered]@{
    schemaVersion = 1
    evidenceKind = 'Editor Development'
    shippingValidated = $false
    culture = 'ko'
    offscreen = $true
    windowStyle = 'Hidden'
    generatedAtUtc = [DateTime]::UtcNow.ToString('o')
    status = 'running'
    results = $results
}
try {
    $projectFile = Join-Path $projectRoot 'IndieGame.uproject'
    $editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $projectFile -Commandlet
    if (-not $editor -or [IO.Path]::GetFileName($editor) -ine 'UnrealEditor-Cmd.exe' -or
        -not (Test-Path -LiteralPath $editor -PathType Leaf)) {
        throw '화면을 띄우지 않는 UnrealEditor-Cmd.exe를 찾지 못했습니다.'
    }
    if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
        $OutputDirectory = Join-Path $projectRoot ('Saved/Validation/DialogueLayout-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    }
    $outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
    if (Test-Path -LiteralPath $outputRoot) {
        throw "이전 결과를 덮어쓰지 않도록 새 폴더를 지정해 주세요: $outputRoot"
    }
    if ((Get-IGArtifactKind $projectRoot $outputRoot) -ne 'Validation') {
        throw '검사 결과는 자동 정리가 가능한 Saved/Validation 아래 새 폴더에 저장해 주세요.'
    }
    Add-IGArtifactDirectory -Scope $scope -Directory $outputRoot
    if (-not (Test-Path -LiteralPath (Join-Path $outputRoot '.ig-artifact-owner'))) {
        throw "검사 결과 폴더를 등록하지 못했습니다: $outputRoot"
    }
    $summaryPath = Join-Path $outputRoot 'summary.json'
    $summary.editor = $editor
    $summary.project = $projectFile
    $summary.sourceCommit = (& git -C $projectRoot rev-parse HEAD).Trim()
    $summary.moduleSha256 = (Get-FileHash -LiteralPath (Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-IndieGame.dll') -Algorithm SHA256).Hash

    function Get-ReviewPngSize([string]$Path) {
        $stream = [IO.File]::OpenRead($Path)
        try {
            $header = [byte[]]::new(24)
            if ($stream.Read($header, 0, $header.Length) -ne 24 -or
                [BitConverter]::ToString($header, 0, 8) -cne '89-50-4E-47-0D-0A-1A-0A') {
                throw "PNG 파일을 읽을 수 없습니다: $Path"
            }
            return @(
                [Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($header, 16)),
                [Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($header, 20))
            )
        }
        finally { $stream.Dispose() }
    }

    $sizes = @{ '720p' = @(1280, 720); '900p' = @(1600, 900); '1080p' = @(1920, 1080); '1440p' = @(2560, 1440) }
    foreach ($resolution in @($Resolutions | Select-Object -Unique)) {
        $width, $height = $sizes[$resolution]
        $caseRoot = Join-Path $outputRoot "${width}x${height}"
        $userRoot = Join-Path $caseRoot 'User'
        $screenshotsRoot = Join-Path $caseRoot 'Screenshots'
        [void][IO.Directory]::CreateDirectory($userRoot)
        [void][IO.Directory]::CreateDirectory($screenshotsRoot)
        $receiptPath = Join-Path $caseRoot 'receipt.txt'
        $logPath = Join-Path $caseRoot 'editor.log'
        $arguments = @(
            $projectFile, '/Game/Maps/Prologue_Morning',
            '-game', '-unattended', '-nosplash', '-NoLoadingScreen',
            '-RenderOffscreen', '-nosound', '-d3d12', '-Windowed', '-ForceRes', '-NoVSync',
            "-ResX=$width", "-ResY=$height", "-UserDir=$userRoot", "-abslog=$logPath",
            '-IGFrontendShippingProbe', '-IGFrontendCopyReview', '-IGSettingsLayoutReview', '-IGTextAudit', '-IGCulture=ko',
            "-IGFrontendExpectedWidth=$width", "-IGFrontendExpectedHeight=$height", "-IGFrontendResultPath=$receiptPath",
            "-IGFrontendAccessibilityScreenshotPath=$(Join-Path $screenshotsRoot 'settings-accessibility.png')",
            "-IGFrontendDisplayScreenshotPath=$(Join-Path $screenshotsRoot 'settings-display.png')",
            "-IGFrontendTitleScreenshotPath=$(Join-Path $screenshotsRoot 'title-first-run.png')",
            "-IGFrontendDefaultScreenshotPath=$(Join-Path $screenshotsRoot 'dialogue-default.png')",
            "-IGFrontendScreenshotPath=$(Join-Path $screenshotsRoot 'dialogue.png')"
        )
        # 프로브 이름에 Shipping이 들어가지만 이 실행의 증거는 Editor Development에만 해당한다.
        $quoted = @($arguments | ForEach-Object {
            if ($_.Contains('"')) { throw '실행 경로와 인자에는 큰따옴표를 넣을 수 없습니다.' }
            '"' + $_ + '"'
        })
        if ($arguments -notcontains '-RenderOffscreen' -or $arguments -notcontains '-nosound') {
            throw '화면과 소리를 끈 상태에서만 검사할 수 있습니다.'
        }
        Write-Host "DIALOGUE_LAYOUT_EDITOR start resolution=${width}x${height} offscreen=1"
        $process = Start-Process -FilePath $editor -ArgumentList $quoted -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
        try {
            $null = $process.Handle
            if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                & taskkill.exe /PID $process.Id /T /F | Out-Null
                [void]$process.WaitForExit(5000)
                throw "대사 화면 검사 시간이 초과됐습니다: $resolution"
            }
            if ($process.ExitCode -ne 0) { throw "대사 화면 검사 종료 코드: $($process.ExitCode), $resolution" }
        }
        finally {
            if (-not $process.HasExited) {
                & taskkill.exe /PID $process.Id /T /F | Out-Null
                [void]$process.WaitForExit(5000)
            }
            $process.Dispose()
        }
        foreach ($required in @($receiptPath, $logPath, (Join-Path $screenshotsRoot 'settings-layout.txt'))) {
            if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "검사 결과 파일이 없습니다: $required" }
        }
        $receipt = (Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8).Trim()
        $settingsReceipt = (Get-Content -LiteralPath (Join-Path $screenshotsRoot 'settings-layout.txt') -Raw -Encoding UTF8).Trim()
        $logText = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
        $outOfBounds = [regex]::Matches($logText, 'TEXT_AUDIT OFFSCREEN\b').Count
        $outOfContainer = [regex]::Matches($logText, 'TEXT_AUDIT OUTSIDE_PANEL\b').Count
        $overlaps = [regex]::Matches($logText, 'TEXT_AUDIT OVERLAP\b').Count
        $case = [ordered]@{
            resolution = "${width}x${height}"
            receipt = $receipt
            settingsReceipt = $settingsReceipt
            textAudit = @{ out_of_bounds = $outOfBounds; out_of_container = $outOfContainer; overlaps = $overlaps }
            screenshots = @()
        }
        $results.Add($case)
        if ($receipt -notmatch ("^MISSINGFLOOR_FRONTEND PASS contract=4 resolution=${width}x${height} ") -or
            $receipt -notmatch '\bdialogue=1 dialogue_default=1\b' -or
            $receipt -notmatch '\bdefault_scale=100 max_scale=200\b' -or
            $receipt -notmatch '\bsamples=11\b') {
            throw "대사·메뉴 검사 결과가 통과 조건과 다릅니다: $receipt"
        }
        if ($settingsReceipt -cne 'MISSINGFLOOR_SETTINGS_LAYOUT PASS samples=158 text_failures=0' -or
            $outOfBounds -ne 0 -or $outOfContainer -ne 0 -or $overlaps -ne 0) {
            throw "글자 배치를 확인해 주세요: $resolution, 화면 밖=$outOfBounds, 영역 밖=$outOfContainer, 겹침=$overlaps"
        }
        foreach ($name in @('settings-accessibility.png', 'settings-display.png', 'title-first-run.png',
            'dialogue-default.png', 'dialogue.png', 'notice.png', 'audio-brightness.png', 'night-difficulty.png', 'key-settings.png')) {
            $path = Join-Path $screenshotsRoot $name
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "화면 캡처가 없습니다: $path" }
            $actualWidth, $actualHeight = Get-ReviewPngSize $path
            if ($actualWidth -ne $width -or $actualHeight -ne $height) { throw "화면 캡처 크기가 다릅니다: $path" }
            $case.screenshots += @{ name = $name; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
        }
        # 전체 항목 검사에 쓴 79장은 성공한 실행에서만 지운다. 대표 화면 9장과 실패 화면은 남긴다.
        $rowCaptures = @(
            foreach ($index in 0..78) {
                $path = [IO.Path]::GetFullPath((Join-Path $screenshotsRoot ('settings-row-{0:D2}.png' -f $index)))
                if (-not (Test-IGArtifactPath $path $screenshotsRoot) -or
                    (Split-Path -Parent $path) -ine $screenshotsRoot -or
                    -not (Test-Path -LiteralPath $path -PathType Leaf)) {
                    throw "설정 항목 캡처 경로를 확인하지 못했습니다: $path"
                }
                $actualWidth, $actualHeight = Get-ReviewPngSize $path
                if ($actualWidth -ne $width -or $actualHeight -ne $height) {
                    throw "설정 항목 캡처 크기가 다릅니다: $path"
                }
                Get-Item -LiteralPath $path
            }
        )
        $cleanup = [ordered]@{ verifiedFiles = $rowCaptures.Count; deletedFiles = 0; deletedBytes = [long]0; retainedFiles = 0 }
        $case.intermediateCaptureCleanup = $cleanup
        foreach ($capture in $rowCaptures) {
            $stream = $null
            try {
                if (-not (Test-IGArtifactPath $capture.FullName $screenshotsRoot)) {
                    throw '설정 항목 캡처의 경로가 바뀌었습니다.'
                }
                $stream = [IO.File]::Open($capture.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Delete)
                if ($stream.Length -ne $capture.Length -or
                    (Get-Item -LiteralPath $capture.FullName).LastWriteTimeUtc -ne $capture.LastWriteTimeUtc) {
                    throw '설정 항목 캡처가 검사 후 변경됐습니다.'
                }
                Remove-Item -LiteralPath $capture.FullName -ErrorAction Stop
                $stream.Dispose()
                $stream = $null
                if (Test-Path -LiteralPath $capture.FullName) { throw '중간 캡처의 삭제 결과를 확인하지 못했습니다.' }
                $cleanup.deletedFiles++
                $cleanup.deletedBytes += $capture.Length
            }
            catch {
                $cleanup.retainedFiles++
                Write-Warning "중간 캡처를 보존했습니다: $($capture.Name): $($_.Exception.Message)"
            }
            finally { if ($stream) { $stream.Dispose() } }
        }
        Write-Host "DIALOGUE_LAYOUT_EDITOR CASE PASS resolution=${width}x${height} out_of_bounds=0 out_of_container=0 overlaps=0"
    }
    $summary.status = 'passed'
    $succeeded = $true
    Write-Host "DIALOGUE_LAYOUT_EDITOR PASS cases=$($results.Count) evidence=EditorDevelopment shipping_validated=0 summary=$summaryPath"
}
catch {
    $summary.status = 'failed'
    $summary.error = $_.Exception.Message
    throw
}
finally {
    try {
        if ($summaryPath) {
            [IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
        }
    }
    finally { Complete-IGArtifactScope -Scope $scope -Succeeded $succeeded }
}
