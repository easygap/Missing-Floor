[CmdletBinding()]
param([switch]$DoorPrint, [switch]$PropFinish, [switch]$Interior, [switch]$BakeCctv)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-FixtureReview.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$fixtureRoot = Split-Path -Parent $PSScriptRoot
$fixtureProject = Join-Path $fixtureRoot 'IndieGame.uproject'
$fixtureEditor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $fixtureProject -Commandlet
$fixtureLog = Join-Path $fixtureRoot 'Saved/Logs/FixtureReview.log'
$fixtureStart = Get-Date
$fixtureArgs = @($fixtureProject, '-game', '-unattended', '-nosplash', '-NoLoadingScreen',
    '-RenderOffscreen', '-d3d12', '-nosound', '-Windowed', '-ResX=1920', '-ResY=1080', '-ForceRes',
    '-IGMissingFloor', '-IGArrivalCapture', '-IGFixtureAudit', '-IGSkipFrontend',
    '-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100', "-abslog=$fixtureLog")
if ($BakeCctv) { $fixtureArgs += '-IGBakeCctv' }
if ($Interior) { $fixtureArgs += '-IGInteriorAudit' }
if ($PropFinish) { $fixtureArgs += '-IGPropFinishAudit' }
if ($DoorPrint) { $fixtureArgs += '-IGDoorPrintAudit' }
$settingsPath = Join-Path $fixtureRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
try {
    & $fixtureEditor @fixtureArgs | Out-Null
    $fixtureExit = $LASTEXITCODE
} finally {
    if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
    elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
if ($fixtureExit -ne 0 -or -not (Select-String -LiteralPath $fixtureLog -Pattern 'FIXTURE_CAPTURE PASS|INTERIOR_CAPTURE PASS|PROP_FINISH_CAPTURE PASS|DOOR_PRINT_CAPTURE PASS') -or
    (Select-String -LiteralPath $fixtureLog -Pattern 'Failed to compile Material|Fatal error:|missing usage flag')) {
    throw "설비 화면 검사 실패: $fixtureLog"
}
$fixtureShots = @('utility-booth-valve', 'utility-meter-wide', 'utility-meter-close', 'utility-booth-front',
    'utility-booth-side', 'utility-tank-steel', 'utility-countertop', 'utility-booth-pump')
if ($BakeCctv) { $fixtureShots += @('cctv-source-entrance', 'cctv-source-parking', 'cctv-source-stair', 'cctv-source-corridor') }
if ($Interior) { $fixtureShots = @('interior-window-front', 'interior-window-side', 'interior-window-latch', 'interior-kitchen-sink', 'interior-pump-panel', 'interior-pump-side', 'interior-pump-fault', 'interior-pump-running', 'interior-sink-drain') }
if ($PropFinish) { $fixtureShots = @('finish-fire-panel', 'finish-extinguisher', 'finish-bottle-near', 'finish-bottle-far', 'finish-store-grout', 'finish-corridor-grout', 'finish-alley-brick', 'finish-stair-surface') }
if ($DoorPrint) { $fixtureShots = @('door-print-outside', 'door-print-inside', 'door-print-open', 'door-print-floor', 'door-print-fire-corridor', 'door-print-fire-pilotis', 'door-print-legacy-fire', 'door-print-calendar', 'door-print-rental-front', 'door-print-rental-side', 'door-print-neighbor-note') }
foreach ($shot in $fixtureShots) {
    $file = Get-Item -LiteralPath (Join-Path $fixtureRoot "Docs/Media/$shot.png")
    if ($file.LastWriteTime -lt $fixtureStart -or $file.Length -lt 10000) { throw "새 캡처 없음: $shot" }
}
Write-Host "FIXTURE_REVIEW PASS shots=$($fixtureShots.Count)"

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
