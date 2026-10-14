[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArchiveDirectory,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [ValidatePattern('^[0-9a-fA-F]{40}$')][string]$ExpectedCommit,
    [switch]$RequireCleanCommit,
    [ValidateRange(1, 3)][int]$RepeatCount = 1,
    [ValidateSet(1, 2)][int]$Quality = 2,
    [ValidateSet(30, 60)][int]$TargetFps = 60,
    [ValidateRange(0, 120)][int]$WarmupSeconds = 20,
    [ValidateRange(120, 1200)][int]$TimeoutSeconds = 600
)

# 0.10.0에서 늘어난 장면(관리실 안쪽 방, 302호, 5층 벽과 덧댄 것)을 Shipping 배포본으로 잰다.
# 기본 성능 검사(Run-WindowsRuntimeProfile.ps1)의 밤 장면 경로에는 이 방들이 없다. 남은 짐 검사
# 경로를 실제 시간으로 돌리며 프레임을 기록하고 구간마다 요약한다. 창은 띄우지 않는다(오프스크린).
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$archiveRoot = (Resolve-Path -LiteralPath $ArchiveDirectory).Path
if (Test-Path -LiteralPath $EvidenceDirectory) { throw "증거 폴더가 이미 있습니다: $EvidenceDirectory" }
New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
$evidenceRoot = (Resolve-Path -LiteralPath $EvidenceDirectory).Path

$manifestArguments = @{ ArchiveDirectory = $archiveRoot; RequireCleanCommit = $RequireCleanCommit }
if ($ExpectedCommit) { $manifestArguments.ExpectedCommit = $ExpectedCommit }
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifest.ps1') @manifestArguments
$launcher = Join-Path $archiveRoot 'Windows\MissingFloor.exe'
$python = (Get-Command python -ErrorAction Stop).Source
$width = 1920
$height = 1080

for ($run = 1; $run -le $RepeatCount; $run++) {
    $runRoot = Join-Path $evidenceRoot "Run$run"
    $userRoot = Join-Path $runRoot 'User'
    $receipt = Join-Path $runRoot 'receipt.txt'
    $configRoot = Join-Path $userRoot 'Saved/Config/Windows'
    New-Item -ItemType Directory -Path $configRoot -Force | Out-Null
    # 밤 장면 경로 검사와 같은 설정이다(Shipping은 명령행 설정 덮어쓰기를 읽지 않는다).
    $engineSettings = "[SystemSettings]`nr.ScreenPercentage=100`nr.DynamicRes.OperationMode=0`nr.AntiAliasingMethod=2`nr.VSync=0`nt.MaxFPS=0`n"
    $groups = @('ViewDistance', 'AntiAliasing', 'Shadow', 'GlobalIllumination', 'Reflection', 'PostProcess',
        'Texture', 'Effects', 'Foliage', 'Shading', 'Landscape') | ForEach-Object { "sg.${_}Quality=$Quality" }
    $gameSettings = "[/Script/Engine.GameUserSettings]`nbUseVSync=False`nbUseDynamicResolution=False`nFrameRateLimit=0.000000`n" +
        "ResolutionSizeX=$width`nResolutionSizeY=$height`nFullscreenMode=2`nLastConfirmedFullscreenMode=2`nPreferredFullscreenMode=2`nVersion=5`n`n" +
        "[ScalabilityGroups]`nsg.ResolutionQuality=100`n" + ($groups -join "`n") + "`n"
    [IO.File]::WriteAllText((Join-Path $configRoot 'Engine.ini'), $engineSettings, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $configRoot 'GameUserSettings.ini'), $gameSettings, [Text.UTF8Encoding]::new($false))
    $arguments = @('-unattended', '-nosplash', '-NoLoadingScreen', '-d3d12', '-RenderOffscreen',
        '-Windowed', "-ResX=$width", "-ResY=$height", '-ForceRes', '-AudioMixer', '-AllowCommandletAudio',
        '-IGListenerGreybox', '-IGLeftoverProbe', '-IGSkipFrontend', '-IGRuntimeProfile',
        "-UserDir=$userRoot", "-IGMissingFloorResultPath=$receipt")
    $quoted = @($arguments | ForEach-Object { '"' + $_ + '"' })
    $started = [DateTime]::UtcNow
    $process = Start-Process -FilePath $launcher -ArgumentList $quoted -WorkingDirectory (Split-Path $launcher -Parent) -WindowStyle Hidden -PassThru
    try {
        $null = $process.Handle
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            & taskkill.exe /PID $process.Id /T /F | Out-Null
            throw "남은 짐 경로 성능 검사 시간 초과: Run$run"
        }
        $exitCode = $process.ExitCode
    }
    finally { $process.Dispose() }
    if (-not (Test-Path -LiteralPath $receipt)) { throw "남은 짐 경로가 끝나지 않았습니다(결과 파일 없음): Run$run" }
    $receiptText = (Get-Content -LiteralPath $receipt -Raw).Trim()
    $csvPath = Join-Path $userRoot 'Saved\Profiling\MissingFloorRuntime.csv'
    if (-not (Test-Path -LiteralPath $csvPath) -or (Get-Item -LiteralPath $csvPath).LastWriteTimeUtc -lt $started) {
        throw "새 프레임 기록이 없습니다: Run$run"
    }
    & $python (Join-Path $PSScriptRoot 'summarize_leftover_profile.py') $csvPath $runRoot `
        --warmup-seconds $WarmupSeconds --target-fps $TargetFps
    if ($LASTEXITCODE -ne 0) { throw "프레임 기록 요약 실패: Run$run" }
    Write-Host "LEFTOVER_RUNTIME_PROFILE Run$run exit=$exitCode receipt=$receiptText"
}
