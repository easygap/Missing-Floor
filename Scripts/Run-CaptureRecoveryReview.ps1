[CmdletBinding()]
param([int]$Width = 1920, [int]$Height = 1080)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-CaptureRecoveryReview.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$captureRoot = Split-Path -Parent $PSScriptRoot
$captureEditor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$captureLog = Join-Path $captureRoot "Saved/Logs/CaptureRecovery-${Width}x${Height}.log"
$captureStart = Get-Date
& $captureEditor (Join-Path $captureRoot 'IndieGame.uproject') -game -unattended -nosplash -NoLoadingScreen `
    -RenderOffscreen -d3d12 -nosound -Windowed "-ResX=$Width" "-ResY=$Height" -ForceRes `
    -IGListenerGreybox -IGNightCapture -IGNightCaptureStartStep=18 -IGImmersionCapture -IGSkipFrontend `
    '-ExecCmds=Scalability 2,r.ScreenPercentage 100,t.MaxFPS 60' "-abslog=$captureLog" | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $captureLog -Pattern 'PHYSICAL_CAPTURE PASS')) {
    throw "포획·복귀 검사 실패: $captureLog"
}
$captureFrames = @(Select-String -LiteralPath $captureLog -Pattern 'IMMERSION_FRAME frame=(\d+) time=([\d.]+)' | ForEach-Object {
    [pscustomobject]@{ frame = [int]$_.Matches[0].Groups[1].Value; time = [double]::Parse($_.Matches[0].Groups[2].Value,[cultureinfo]::InvariantCulture) }
})
if ($captureFrames.Count -lt 20) { throw '검수에 필요한 연속 프레임이 부족합니다.' }
$captureOutput = Join-Path $captureRoot "Saved/CaptureRecovery-${Width}x${Height}"
New-Item -ItemType Directory -Path $captureOutput -Force | Out-Null
foreach ($captureFrame in $captureFrames) {
    $captureName = 'frame_{0:D5}.png' -f $captureFrame.frame
    $captureSource = Get-Item -LiteralPath (Join-Path $captureRoot "Saved/NightCapture/physical-capture/$captureName")
    if ($captureSource.LastWriteTime -lt $captureStart) { throw "예전 프레임이 섞였습니다: $captureName" }
    Copy-Item -LiteralPath $captureSource.FullName -Destination (Join-Path $captureOutput $captureName) -Force
}
$captureFrames | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $captureOutput 'frames.json') -Encoding utf8
Select-String -LiteralPath $captureLog -Pattern 'PHYSICAL_CAPTURE' | ForEach-Object { Write-Host $_.Line }
Write-Host "연속 프레임 $($captureFrames.Count)장: $captureOutput"

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
