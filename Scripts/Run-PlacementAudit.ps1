[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [switch]$Shots,
    [ValidateRange(640, 3840)][int]$Width = 1600,
    [ValidateRange(360, 2160)][int]$Height = 900,
    [ValidateRange(120, 3600)][int]$TimeoutSeconds = 1200
)

# 낮과 밤1·3·4의 첫 상태에서 월드 전체 소품의 지지면·관통·광원을 실제 메시 삼각형으로 잰다.
# 창과 소리 없이 돌리고, -Shots면 걸린 물건과 구역 개관을 찍는다. 판정은 사람이 화면과 수치로 한다.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$scope = Enter-IGArtifactScope -ProjectRoot $projectRoot -Name 'Run-PlacementAudit.ps1' -Parameters $PSBoundParameters
$succeeded = $false
$summaryPath = $null
$process = $null
$summary = [ordered]@{
    schemaVersion = 1
    evidenceKind = 'Editor Development'
    shippingValidated = $false
    generatedAtUtc = [DateTime]::UtcNow.ToString('o')
    offscreen = $true
    audioEnabled = $false
    windowStyle = 'Hidden'
    status = 'running'
    labels = @()
}
try {
    $projectFile = Join-Path $projectRoot 'IndieGame.uproject'
    $editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $projectFile -Commandlet
    if (-not $editor -or [IO.Path]::GetFileName($editor) -ine 'UnrealEditor-Cmd.exe') {
        throw '화면을 띄우지 않는 UnrealEditor-Cmd.exe를 찾지 못했습니다.'
    }
    if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
        $OutputDirectory = Join-Path $projectRoot ('Saved/Validation/PlacementAudit-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    }
    $outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
    if (Test-Path -LiteralPath $outputRoot) { throw "이전 결과를 덮어쓰지 않도록 새 폴더를 지정해 주세요: $outputRoot" }
    if ((Get-IGArtifactKind $projectRoot $outputRoot) -ne 'Validation') {
        throw '검사 결과는 Saved/Validation 아래 새 폴더에 저장해 주세요.'
    }
    Add-IGArtifactDirectory -Scope $scope -Directory $outputRoot -Variant 'PlacementAudit'
    $summaryPath = Join-Path $outputRoot 'summary.json'
    $userRoot = Join-Path $outputRoot 'User'
    $logPath = Join-Path $outputRoot 'editor.log'
    [void][IO.Directory]::CreateDirectory($userRoot)
    $summary.sourceCommit = (& git -C $projectRoot rev-parse HEAD).Trim()
    $summary.moduleSha256 = (Get-FileHash -LiteralPath (Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-IndieGame.dll') -Algorithm SHA256).Hash
    $arguments = @(
        $projectFile, '-game', '-unattended', '-nosplash', '-NoLoadingScreen',
        '-IGMissingFloor', '-IGSkipFrontend', '-IGPlacementProbe',
        '-RenderOffscreen', '-nosound', '-d3d12', '-Windowed', "-ResX=$Width", "-ResY=$Height", '-ForceRes', '-NoVSync',
        '-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100',
        "-UserDir=$userRoot", "-abslog=$logPath"
    )
    if ($Shots) { $arguments += '-IGPlacementShots' }
    $quoted = @($arguments | ForEach-Object {
        if ($_.Contains('"')) { throw '실행 경로와 인자에는 큰따옴표를 넣을 수 없습니다.' }
        '"' + $_ + '"'
    })
    Write-Host "PLACEMENT_AUDIT start shots=$([int][bool]$Shots) offscreen=1"
    $process = Start-Process -FilePath $editor -ArgumentList $quoted -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        [void]$process.WaitForExit(5000)
        throw '소품 배치 검사 시간이 초과됐습니다.'
    }
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) { throw '소품 배치 검사 로그가 없습니다.' }
    $logText = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
    $receipt = [regex]::Match($logText, 'PLACEMENT_PROBE DONE [^\r\n]*')
    if ($process.ExitCode -ne 0 -or -not $receipt.Success -or $logText -match 'Fatal error:') {
        throw "소품 배치 검사가 끝까지 돌지 않았습니다(종료 코드 $($process.ExitCode)): $logPath"
    }
    $summary.receipt = $receipt.Value
    foreach ($match in [regex]::Matches($logText, 'IG_PLACEMENT_SUMMARY ([^\r\n]*)')) {
        $fields = [ordered]@{}
        foreach ($pair in ($match.Groups[1].Value -split ' ')) {
            $kv = $pair -split '=', 2
            if ($kv.Count -eq 2) { $fields[$kv[0]] = $kv[1] }
        }
        $summary.labels += $fields
    }
    $shotRoot = Join-Path $userRoot 'Saved/PlacementReview'
    $summary.shots = @(if (Test-Path -LiteralPath $shotRoot) { Get-ChildItem -LiteralPath $shotRoot -Filter '*.png' | ForEach-Object Name })
    $summary.logSha256 = (Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash
    $summary.status = 'measured'
    $succeeded = $true
    Write-Host "PLACEMENT_AUDIT DONE $($receipt.Value) summary=$summaryPath"
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
        if ($summaryPath) {
            [IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 6) + "`n", [Text.UTF8Encoding]::new($false))
        }
    }
    finally { Complete-IGArtifactScope -Scope $scope -Succeeded $succeeded }
}
