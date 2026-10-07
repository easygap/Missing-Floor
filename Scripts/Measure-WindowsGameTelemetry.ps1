[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Launcher,
    [Parameter(Mandatory)][string]$ShippingExecutable,
    [Parameter(Mandatory)][string[]]$GameArguments,
    [Parameter(Mandatory)][string]$PresentMonPath,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$ExpectedWindowTitle,
    [switch]$AllowVisibleWindow,
    [ValidateRange(60, 900)][int]$TimeoutSeconds = 420
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $AllowVisibleWindow) {
    throw '이 측정은 게임 창을 화면 앞에 띄웁니다. 창이 떠도 괜찮을 때 -AllowVisibleWindow를 함께 주세요.'
}
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$shippingPath = (Resolve-Path -LiteralPath $ShippingExecutable).Path
$processName = [IO.Path]::GetFileNameWithoutExtension($shippingPath)
if (@(Get-Process -Name $processName -ErrorAction SilentlyContinue).Count) {
    throw '같은 게임의 다른 실행이 남아 있습니다. 측정할 프로세스를 구분할 수 없습니다.'
}
foreach ($name in @('presentmon.csv', 'gpu-memory.csv', 'windows-telemetry.json')) {
    if (Test-Path -LiteralPath (Join-Path $outputRoot $name)) { throw "이미 있는 측정 파일입니다: $name" }
}
if ($GameArguments -contains '-RenderOffscreen' -or $GameArguments -contains '-nullrhi') {
    throw '화면 출력 검사는 실제 그래픽 창에서 실행해야 합니다.'
}
function ConvertTo-QuotedArgument([string]$Value) {
    if ($Value.Contains('"')) { throw '실행 인자에는 따옴표를 넣을 수 없습니다.' }
    return '"' + $Value + '"'
}
$sessionName = 'MissingFloor-' + [Guid]::NewGuid().ToString('N')
$presentCsv = Join-Path $outputRoot 'presentmon.csv'
$memoryCsv = Join-Path $outputRoot 'gpu-memory.csv'
# Windows PowerShell 5.1은 Stop 설정에서 네이티브 프로그램이 stderr에 쓴 첫 줄을 오류로
# 던진다. PresentMon은 버전 배너를 stderr에 쓰므로 이 호출만 Continue로 돌린다.
function Invoke-PresentMon([string[]]$Arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { return ((& $PresentMonPath @Arguments 2>&1 | ForEach-Object { "$_" }) -join "`n") }
    finally { $ErrorActionPreference = $previous }
}
$toolHelp = Invoke-PresentMon @('--help')
if ($toolHelp -notmatch '(?m)^PresentMon (?<version>(?<major>[12])\.\d+\.\d+)') {
    throw 'PresentMon 버전을 확인할 수 없습니다. 1.x 또는 2.x 콘솔 실행 파일을 지정해 주세요.'
}
$presentMonVersion = $Matches.version
if ($Matches.major -eq '2') {
    # 현재 분석기와 같은 열을 쓰며 입력 이벤트는 수집하지 않는다.
    $presentArguments = @('--process_name', [IO.Path]::GetFileName($shippingPath), '--no_console_stats',
        '--v1_metrics', '--no_track_input', '--terminate_on_proc_exit', '--timed', [string]($TimeoutSeconds + 10),
        '--terminate_after_timed', '--session_name', $sessionName, '--output_file', $presentCsv)
    $terminateSessionArguments = @('--terminate_existing_session', '--session_name', $sessionName)
}
else {
    $presentArguments = @('-process_name', [IO.Path]::GetFileName($shippingPath), '-no_top',
        '-qpc_time_s', '-terminate_on_proc_exit', '-timed', [string]($TimeoutSeconds + 10),
        '-terminate_after_timed', '-session_name', $sessionName, '-output_file', $presentCsv)
    $terminateSessionArguments = @('-terminate_existing', '-session_name', $sessionName)
}
$memoryAtStart = Get-CimInstance Win32_OperatingSystem | Select-Object TotalVisibleMemorySize,FreePhysicalMemory,TotalVirtualMemorySize,FreeVirtualMemory
$logger = $null
$game = $null
$writer = $null
$gameProcessId = $null
$sampleCount = 0
$queryFailures = 0
$windowTitles = [Collections.Generic.HashSet[string]]::new()
$elapsed = [Diagnostics.Stopwatch]::StartNew()
$started = [DateTime]::UtcNow
try {
    $logger = Start-Process -FilePath $PresentMonPath `
        -ArgumentList @($presentArguments | ForEach-Object { ConvertTo-QuotedArgument $_ }) `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $outputRoot 'presentmon.log') `
        -RedirectStandardError (Join-Path $outputRoot 'presentmon-error.log')
    $null = $logger.Handle
    if ($logger.WaitForExit(750)) { throw 'PresentMon이 게임 실행 전에 종료됐습니다. 진단 로그를 확인하세요.' }
    $writer = [IO.StreamWriter]::new($memoryCsv, $false, [Text.UTF8Encoding]::new($false))
    $writer.WriteLine('ElapsedSeconds,ProcessId,DedicatedBytes,SharedBytes,ValidCounters,QueryStatus')
    # 화면에 실제로 표시되는 경로를 잰다. 계측 도구만 숨기고 게임 창은 표시한다.
    # visible-window: intentional — 최소화하면 언리얼이 프레임을 내보내지 않아 PresentMon이 잴 것이 없다.
    $game = Start-Process -FilePath $Launcher `
        -ArgumentList @($GameArguments | ForEach-Object { ConvertTo-QuotedArgument $_ }) `
        -WorkingDirectory (Split-Path $Launcher -Parent) -WindowStyle Normal -PassThru
    $null = $game.Handle
    while (-not $game.HasExited) {
        if ($elapsed.Elapsed.TotalSeconds -gt $TimeoutSeconds) {
            & taskkill.exe /PID $game.Id /T /F | Out-Null
            throw '배포본 화면 출력 검사 시간이 초과됐습니다.'
        }
        if (-not $gameProcessId) {
            $candidates = @(Get-Process -Name $processName -ErrorAction SilentlyContinue |
                Where-Object { $_.Path -eq $shippingPath -and $_.StartTime.ToUniversalTime() -ge $started })
            if ($candidates.Count -gt 1) { throw '측정 중 같은 게임이 두 번 실행됐습니다.' }
            if ($candidates.Count -eq 1) { $gameProcessId = $candidates[0].Id }
        }
        if (-not $gameProcessId) { Start-Sleep -Milliseconds 200; continue }
        $runningGame = Get-Process -Id $gameProcessId -ErrorAction SilentlyContinue
        if ($runningGame -and -not [string]::IsNullOrWhiteSpace($runningGame.MainWindowTitle)) {
            $null = $windowTitles.Add($runningGame.MainWindowTitle.Trim())
        }
        $dedicated = -1L
        $shared = -1L
        $counterCount = 0
        $queryStatus = 'unavailable'
        try {
            # 동일 게임 PID에 속한 카운터만 읽는다. 다른 앱의 메모리를 합산하지 않는다.
            $counterPaths = @("\GPU Process Memory(pid_$($gameProcessId)_*)\Dedicated Usage",
                "\GPU Process Memory(pid_$($gameProcessId)_*)\Shared Usage")
            $counterSample = Get-Counter -Counter $counterPaths -MaxSamples 1 -ErrorAction Stop
            $valid = @($counterSample.CounterSamples | Where-Object { $_.Status -in @(0, 1) })
            $dedicatedCounters = @($valid | Where-Object { $_.Path -like '*\dedicated usage' })
            $sharedCounters = @($valid | Where-Object { $_.Path -like '*\shared usage' })
            if ($dedicatedCounters.Count -eq 0 -or $sharedCounters.Count -eq 0 -or
                $valid.Count -ne @($counterSample.CounterSamples).Count) { throw 'GPU 메모리 카운터가 불완전합니다.' }
            $dedicated = [long](($dedicatedCounters | Measure-Object -Property CookedValue -Sum).Sum)
            $shared = [long](($sharedCounters | Measure-Object -Property CookedValue -Sum).Sum)
            $counterCount = $valid.Count
            $queryStatus = 'ok'
        }
        catch { ++$queryFailures }
        $writer.WriteLine(('{0},{1},{2},{3},{4},{5}' -f
            $elapsed.Elapsed.TotalSeconds.ToString('F6', [Globalization.CultureInfo]::InvariantCulture),
            $gameProcessId, $dedicated, $shared, $counterCount, $queryStatus))
        $writer.Flush()
        ++$sampleCount
        # Get-Counter도 시간을 쓰므로 원본의 실제 표본 간격으로 범위를 판단한다.
        Start-Sleep -Milliseconds 250
        $game.Refresh()
    }
    if ($game.ExitCode -ne 0) { throw "배포본 종료 코드: $($game.ExitCode)" }
    if (-not $gameProcessId -or $sampleCount -lt 2) { throw '게임 프로세스의 메모리 기록이 부족합니다.' }
    if (-not $logger.WaitForExit(10000)) {
        # 이 검사에서 만든 세션 하나만 종료한다. 다른 계측 도구의 세션은 건드리지 않는다.
        $null = Invoke-PresentMon $terminateSessionArguments
        if (-not $logger.WaitForExit(5000)) { throw 'PresentMon 기록을 마무리하지 못했습니다.' }
    }
    if ($logger.ExitCode -ne 0) { throw "PresentMon 종료 코드: $($logger.ExitCode)" }
}
finally {
    if ($writer) { $writer.Dispose() }
    if ($game) {
        if (-not $game.HasExited) { & taskkill.exe /PID $game.Id /T /F | Out-Null }
        $game.Dispose()
    }
    if ($logger) {
        if (-not $logger.HasExited) {
            $null = Invoke-PresentMon $terminateSessionArguments
            $null = $logger.WaitForExit(5000)
        }
        $logger.Dispose()
    }
}
$report = [ordered]@{
    schemaVersion = 1; createdAt = [DateTime]::UtcNow.ToString('o'); processId = $gameProcessId;
    executableName = [IO.Path]::GetFileName($shippingPath);
    shippingSha256 = (Get-FileHash -LiteralPath $shippingPath -Algorithm SHA256).Hash.ToLowerInvariant();
    presentationMode = 'Windowed'; windowStyle = 'Normal'; offscreen = $false; sampleCount = $sampleCount; queryFailures = $queryFailures;
    observedWindowTitles = @($windowTitles | Sort-Object); expectedWindowTitle = $ExpectedWindowTitle;
    memorySource = 'Windows GPU Process Memory: Dedicated Usage / Shared Usage';
    memoryScope = '게임 프로세스의 GPU 메모리 표본. 시작·워밍업·경로·종료를 포함하며, 프로세스 사이 공유분도 포함한다. 순간 최댓값은 보증하지 않는다.';
    timingScope = 'PresentMon 전체 캡처. 게임 내부 경로 시계와 정렬하지 않았으므로 워밍업 포함 수치로만 쓴다.';
    presentMonVersion = $presentMonVersion; presentMonMetrics = 'v1';
    systemMemoryKiBAtStart = $memoryAtStart;
    systemMemoryKiBAtEnd = Get-CimInstance Win32_OperatingSystem | Select-Object TotalVisibleMemorySize,FreePhysicalMemory,TotalVirtualMemorySize,FreeVirtualMemory;
    environmentScope = '현재 데스크톱의 관측이다. 다른 프로그램을 종료하거나 드라이버 캐시를 지우지 않았다.';
    presentMonSha256 = (Get-FileHash -LiteralPath $PresentMonPath -Algorithm SHA256).Hash.ToLowerInvariant();
    presentCsv = 'presentmon.csv'; presentCsvSha256 = (Get-FileHash -LiteralPath $presentCsv -Algorithm SHA256).Hash.ToLowerInvariant();
    memoryCsv = 'gpu-memory.csv'; memoryCsvSha256 = (Get-FileHash -LiteralPath $memoryCsv -Algorithm SHA256).Hash.ToLowerInvariant();
    displayAdapters = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate);
    shippingReleaseCertified = $false
}
[IO.File]::WriteAllText((Join-Path $outputRoot 'windows-telemetry.json'),
    ($report | ConvertTo-Json -Depth 5) + "`n", [Text.UTF8Encoding]::new($false))
if ($ExpectedWindowTitle -and (-not $windowTitles.Contains($ExpectedWindowTitle) -or $windowTitles.Count -ne 1)) {
    throw "게임 창 제목이 배포 이름과 다릅니다: $($windowTitles -join ', ')"
}
Write-Host "WINDOWS_GAME_TELEMETRY process=$gameProcessId memory_samples=$sampleCount query_failures=$queryFailures"
