[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

# 창을 띄우는 측정은 -AllowVisibleWindow 없이 부르면 경로를 보거나 프로세스를 만들기
# 전에 거절해야 한다. 실제 게임은 띄우지 않는다. 없는 경로를 주어도 먼저 막히는지만 본다.
$cases = @(
    @{ Script = 'Run-WindowsRuntimeProfile.ps1'; Args = @{
        ArchiveDirectory = 'Z:\MissingFloor-DoesNotExist'; EvidenceDirectory = 'Z:\MissingFloor-Evidence';
        PresentationMode = 'Windowed' }; Message = 'Windowed 측정은 게임 창을 화면 앞에 띄웁니다.' },
    @{ Script = 'Measure-WindowsGameTelemetry.ps1'; Args = @{
        Launcher = 'Z:\MissingFloor.exe'; ShippingExecutable = 'Z:\MissingFloor.exe';
        GameArguments = @('-game'); PresentMonPath = 'Z:\PresentMon.exe';
        OutputDirectory = 'Z:\MissingFloor-Evidence' }; Message = '이 측정은 게임 창을 화면 앞에 띄웁니다.' }
)
foreach ($case in $cases) {
    $arguments = $case.Args
    $caught = $null
    try { & (Join-Path $PSScriptRoot $case.Script) @arguments }
    catch { $caught = $_.Exception.Message }
    if (-not $caught -or -not $caught.StartsWith($case.Message)) {
        throw "창을 띄우는 실행이 막히지 않았다: $($case.Script): $caught"
    }
}
Write-Host 'BACKGROUND_LAUNCH PASS visible_launch_rejected=2 processes_started=0'
