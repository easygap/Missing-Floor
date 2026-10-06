[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArchiveDirectory,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [ValidatePattern('^[0-9a-fA-F]{40}$')][string]$ExpectedCommit,
    [switch]$RequireCleanCommit,
    [ValidateRange(1, 3)][int]$RepeatCount = 3,
    [ValidateSet(2, 4)][int]$AntiAliasing = 2,
    [ValidateSet(1, 2)][int]$Quality = 2,
    [ValidateSet(30, 60)][int]$TargetFps = 60,
    [ValidateSet('720p', '1080p', '1440p')][string]$Resolution = '1080p',
    [ValidateSet('Offscreen', 'Windowed')][string]$PresentationMode = 'Offscreen',
    # Windowed는 실제 게임 창을 화면에 띄운다. 사람이 자리에 있을 때만 켠다.
    [switch]$AllowVisibleWindow,
    [string]$PresentMonPath,
    [string]$ExpectedWindowTitle,
    [ValidateRange(0, 600)][int]$WarmupSeconds = 120,
    [ValidateRange(60, 900)][int]$TimeoutSeconds = 420
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$archiveRoot = (Resolve-Path -LiteralPath $ArchiveDirectory).Path
$evidenceRoot = [IO.Path]::GetFullPath($EvidenceDirectory)
if (Test-Path -LiteralPath $evidenceRoot) { throw '새 결과 폴더를 지정해 주세요. 이전 측정을 덮어쓰지 않습니다.' }
if ($evidenceRoot.TrimEnd('\', '/') -eq $archiveRoot.TrimEnd('\', '/') -or
    $evidenceRoot.StartsWith($archiveRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw '측정 결과 폴더는 배포 폴더 밖에 두어야 합니다.'
}
$dimensions = @{ '720p' = @(1280, 720); '1080p' = @(1920, 1080); '1440p' = @(2560, 1440) }[$Resolution]
$width, $height = $dimensions
if ($PresentationMode -eq 'Windowed') {
    if (-not $AllowVisibleWindow) {
        throw 'Windowed 측정은 게임 창을 화면 앞에 띄웁니다. 창이 떠도 괜찮을 때 -AllowVisibleWindow를 함께 주세요.'
    }
    if (-not $PresentMonPath -or -not (Test-Path -LiteralPath $PresentMonPath -PathType Leaf)) {
        throw '화면 출력 검사는 PresentMon 콘솔 실행 파일 경로가 필요합니다.'
    }
    $PresentMonPath = (Resolve-Path -LiteralPath $PresentMonPath).Path
}
elseif ($PresentMonPath) { throw 'PresentMon은 Windowed 검사에서만 사용합니다.' }
$manifestArguments = @{ ArchiveDirectory = $archiveRoot; RequireCleanCommit = $RequireCleanCommit }
if ($ExpectedCommit) { $manifestArguments.ExpectedCommit = $ExpectedCommit }
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1') @manifestArguments
$packageManifestPath = Join-Path $archiveRoot 'manifest.json'
$packageManifest = Get-Content -LiteralPath $packageManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$manifestHashBefore = (Get-FileHash -LiteralPath $packageManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$shippingExecutable = Join-Path $archiveRoot 'Windows/IndieGame/Binaries/Win64/IndieGame-Win64-Shipping.exe'
$shippingHashBefore = (Get-FileHash -LiteralPath $shippingExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
$launcher = Join-Path $archiveRoot 'Windows\MissingFloor.exe'
$python = (Get-Command python -ErrorAction Stop).Source
$measurementCommit = (& git -C $projectRoot rev-parse HEAD).Trim()
$measurementWorkingTreeClean = @(& git -C $projectRoot status --porcelain).Count -eq 0
$measurementScripts = @('Run-WindowsRuntimeProfile.ps1', 'summarize_shipping_profile.py')
if ($PresentationMode -eq 'Windowed') { $measurementScripts += @('Measure-WindowsGameTelemetry.ps1', 'summarize_windows_telemetry.py') }
$measurementHashes = @($measurementScripts | ForEach-Object {
    [ordered]@{ path = "Scripts/$_"; sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $_) -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$results = [Collections.Generic.List[object]]::new()
for ($run = 1; $run -le $RepeatCount; $run++) {
    $runRoot = Join-Path $evidenceRoot "Run$run"
    New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
    $userRoot = Join-Path $runRoot 'User'
    $receipt = Join-Path $runRoot 'receipt.txt'
    # Shipping에서는 ExecCmds와 명령행 -ini: 덮어쓰기를 읽지 않는다.
    # 새 사용자 폴더의 일반 설정 파일로 입력하고 실제 관측값을 분석기에서 확인한다.
    $configRoot = Join-Path $userRoot 'Saved/Config/Windows'
    New-Item -ItemType Directory -Path $configRoot -Force | Out-Null
    $engineSettings = @"
[SystemSettings]
r.ScreenPercentage=100
r.ScreenPercentage.MinResolution=0
r.ScreenPercentage.MaxResolution=0
r.SecondaryScreenPercentage.GameViewport=100
r.DynamicRes.OperationMode=0
r.AntiAliasingMethod=$AntiAliasing
r.VSync=0
t.MaxFPS=0

[Audio]
UnfocusedVolumeMultiplier=1.0
"@
    $gameSettings = @"
[/Script/Engine.GameUserSettings]
bUseVSync=False
bUseDynamicResolution=False
FrameRateLimit=0.000000
ResolutionSizeX=$width
ResolutionSizeY=$height
LastUserConfirmedResolutionSizeX=$width
LastUserConfirmedResolutionSizeY=$height
DesiredScreenWidth=$width
DesiredScreenHeight=$height
bUseDesiredScreenHeight=False
FullscreenMode=2
LastConfirmedFullscreenMode=2
PreferredFullscreenMode=2
Version=5

[ScalabilityGroups]
sg.ResolutionQuality=100
sg.ViewDistanceQuality=$Quality
sg.AntiAliasingQuality=$Quality
sg.ShadowQuality=$Quality
sg.GlobalIlluminationQuality=$Quality
sg.ReflectionQuality=$Quality
sg.PostProcessQuality=$Quality
sg.TextureQuality=$Quality
sg.EffectsQuality=$Quality
sg.FoliageQuality=$Quality
sg.ShadingQuality=$Quality
sg.LandscapeQuality=$Quality
"@
    foreach ($settings in @(
        @{ Name = 'Engine.ini'; Text = $engineSettings },
        @{ Name = 'GameUserSettings.ini'; Text = $gameSettings }
    )) {
        [IO.File]::WriteAllText((Join-Path $configRoot $settings.Name), $settings.Text, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $runRoot "Input-$($settings.Name)"), $settings.Text, [Text.UTF8Encoding]::new($false))
    }
    $arguments = @('-unattended', '-nosplash', '-NoLoadingScreen', '-d3d12',
        '-Windowed', "-ResX=$width", "-ResY=$height", '-ForceRes', '-AudioMixer',
        '-AllowCommandletAudio', '-IGListenerGreybox', '-IGNightCapture', '-IGCaptureMetricsOnly',
        '-IGSkipFrontend', '-IGRuntimeProfile', "-IGPerformanceWarmupSeconds=$WarmupSeconds",
        "-UserDir=$userRoot", "-IGMissingFloorResultPath=$receipt")
    if ($PresentationMode -eq 'Offscreen') { $arguments += '-RenderOffscreen' }
    $quoted = @($arguments | ForEach-Object {
        if ($_.Contains('"')) { throw '실행 인자에 따옴표를 넣을 수 없습니다.' }
        '"' + $_ + '"'
    })
    $started = [DateTime]::UtcNow
    if ($PresentationMode -eq 'Windowed') {
        & (Join-Path $PSScriptRoot 'Measure-WindowsGameTelemetry.ps1') -Launcher $launcher `
            -ShippingExecutable $shippingExecutable -GameArguments $arguments `
            -PresentMonPath $PresentMonPath -OutputDirectory $runRoot -TimeoutSeconds $TimeoutSeconds `
            -ExpectedWindowTitle $ExpectedWindowTitle
    }
    else {
        $process = Start-Process -FilePath $launcher -ArgumentList $quoted -WorkingDirectory (Split-Path $launcher -Parent) -WindowStyle Hidden -PassThru
        try {
            $null = $process.Handle
            if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                & taskkill.exe /PID $process.Id /T /F | Out-Null
                throw "배포본 성능 검사 시간 초과: Run$run"
            }
            if ($process.ExitCode -ne 0) { throw "배포본 성능 검사 종료 코드: $($process.ExitCode)" }
        }
        finally { $process.Dispose() }
    }
    $settingsSnapshots = foreach ($settingsName in @('Engine.ini', 'GameUserSettings.ini')) {
        $beforeName = "Input-$settingsName"
        $afterName = "After-$settingsName"
        $liveSettings = Join-Path $configRoot $settingsName
        $afterExists = Test-Path -LiteralPath $liveSettings
        if ($afterExists) {
            Copy-Item -LiteralPath $liveSettings -Destination (Join-Path $runRoot $afterName)
        }
        # 엔진이 저장할 차이가 없다고 판단해 ini를 지울 수도 있으므로 부재도 그대로 기록한다.
        [ordered]@{
            name = $settingsName;
            before = $beforeName;
            beforeSha256 = (Get-FileHash -LiteralPath (Join-Path $runRoot $beforeName) -Algorithm SHA256).Hash.ToLowerInvariant();
            afterExists = $afterExists;
            after = $(if ($afterExists) { $afterName } else { $null });
            afterSha256 = $(if ($afterExists) { (Get-FileHash -LiteralPath $liveSettings -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null })
        }
    }
    [IO.File]::WriteAllText((Join-Path $runRoot 'settings.json'), ($settingsSnapshots | ConvertTo-Json -Depth 5) + "`n", [Text.UTF8Encoding]::new($false))
    if (-not (Test-Path -LiteralPath $receipt) -or
        (Get-Content -LiteralPath $receipt -Raw).Trim() -notmatch '^MISSINGFLOOR_GREYBOX PASS step=[0-9]+$') {
        throw "밤 장면 경로가 완료되지 않았습니다: Run$run"
    }
    $csvPath = Join-Path $userRoot 'Saved\Profiling\MissingFloorRuntime.csv'
    if (-not (Test-Path -LiteralPath $csvPath) -or (Get-Item -LiteralPath $csvPath).LastWriteTimeUtc -lt $started) {
        throw "새 Shipping 프레임 원본이 없습니다: Run$run"
    }
    & $python (Join-Path $PSScriptRoot 'summarize_shipping_profile.py') $csvPath $runRoot `
        --warmup-seconds $WarmupSeconds --quality $Quality --aa $AntiAliasing --target-fps $TargetFps `
        --width $width --height $height --presentation-mode $PresentationMode.ToLowerInvariant()
    if ($LASTEXITCODE -ne 0) { throw "성능 원본 검증 실패: Run$run" }
    $summary = Get-Content -LiteralPath (Join-Path $runRoot 'summary.json') -Raw | ConvertFrom-Json
    $windowsTelemetry = $null
    if ($PresentationMode -eq 'Windowed') {
        & $python (Join-Path $PSScriptRoot 'summarize_windows_telemetry.py') $runRoot
        if ($LASTEXITCODE -ne 0) { throw "Windows 화면 출력·메모리 기록 검증 실패: Run$run" }
        $windowsTelemetry = "Run$run/windows-telemetry-summary.json"
    }
    $results.Add([ordered]@{ run = $run; summary = "Run$run/summary.json"; windowsTelemetry = $windowsTelemetry;
        frameGatePassed = $summary.local_route_frame_gate_passed;
        p95ms = $summary.metrics.FrameTime.p95; onePercentLowFps = $summary.one_percent_low_fps;
        rawSha256 = $summary.source_sha256; gpuRawSha256 = $summary.gpu_source_sha256; settingsEvidence = "Run$run/settings.json";
        engineSettingsSha256 = (Get-FileHash -LiteralPath (Join-Path $runRoot 'Input-Engine.ini') -Algorithm SHA256).Hash.ToLowerInvariant();
        gameSettingsSha256 = (Get-FileHash -LiteralPath (Join-Path $runRoot 'Input-GameUserSettings.ini') -Algorithm SHA256).Hash.ToLowerInvariant() })
    Write-Host "SHIPPING_RUNTIME_PROFILE Run$run frame_gate=$($summary.local_route_frame_gate_passed)"
}
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1') @manifestArguments
$manifestHashAfter = (Get-FileHash -LiteralPath $packageManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$shippingHashAfter = (Get-FileHash -LiteralPath $shippingExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
if ($manifestHashAfter -ne $manifestHashBefore -or $shippingHashAfter -ne $shippingHashBefore) {
    throw '성능 측정 도중 배포본이나 파일 목록이 바뀌었습니다.'
}
foreach ($script in $measurementHashes) {
    if ((Get-FileHash -LiteralPath (Join-Path $projectRoot $script.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $script.sha256) {
        throw "측정 도중 검사 스크립트가 바뀌었습니다: $($script.path)"
    }
}
$scope = if ($PresentationMode -eq 'Windowed') {
    '현재 장비의 Shipping 창 모드 자동 밤 장면 경로. 화면 출력은 PresentMon, GPU 메모리는 Windows 카운터로 별도 확인'
} else { '현재 장비의 Shipping 오프스크린 자동 밤 장면 경로. 실제 화면 출력 비용은 제외됨' }
$report = [ordered]@{ scope = $scope;
    schemaVersion = 3; createdAt = [DateTime]::UtcNow.ToString('o');
    measurementCommit = $measurementCommit; measurementWorkingTreeClean = $measurementWorkingTreeClean; measurementScripts = $measurementHashes;
    commit = $packageManifest.commit; hasLocalChanges = $packageManifest.hasLocalChanges;
    manifestSha256 = $manifestHashBefore; shippingSha256 = $shippingHashBefore; archiveUnchanged = $true;
    repeatedRuns = $RepeatCount; warmupSeconds = $WarmupSeconds; quality = $Quality;
    antiAliasing = $AntiAliasing; targetFps = $TargetFps;
    presentationMode = $PresentationMode; resolution = $Resolution; width = $width; height = $height;
    allLocalFrameGatesPassed = @($results | Where-Object { -not $_.frameGatePassed }).Count -eq 0;
    shippingReleaseCertified = $false; runs = @($results.ToArray()) }
[IO.File]::WriteAllText((Join-Path $evidenceRoot 'summary.json'), ($report | ConvertTo-Json -Depth 10) + "`n", [Text.UTF8Encoding]::new($false))
