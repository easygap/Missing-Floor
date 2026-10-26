[CmdletBinding()]
param([int]$Width=1920, [int]$Height=1080, [ValidateSet(1,2)][int]$TextScale=1, [string]$Culture='')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Run-ReadingReview.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {
$readingRoot = Split-Path -Parent $PSScriptRoot
$readingEditor = & "$PSScriptRoot/Resolve-UnrealEditor.ps1" -Commandlet
$readingKey = "${Width}x${Height}-$TextScale" + $(if ($Culture) { "-$Culture" } else { '' })
# 번역본 글자 폭을 볼 때만 언어를 고른다. 비우면 저장된 설정을 따른다.
$readingCulture = @(if ($Culture) { "-IGCulture=$Culture" })
$readingLog = "$readingRoot/Saved/Logs/ReadingReview-$readingKey.log"
$readingSettings = "$readingRoot/Saved/Config/WindowsEditor/GameUserSettings.ini"
$readingBackup = if (Test-Path -LiteralPath $readingSettings) { [IO.File]::ReadAllBytes($readingSettings) } else { $null }
$readingStart = Get-Date
try {
    & $readingEditor "$readingRoot/IndieGame.uproject" -game -unattended -nosplash -NoLoadingScreen `
        -RenderOffscreen -d3d12 -nosound -Windowed "-ResX=$Width" "-ResY=$Height" -ForceRes `
        -IGMissingFloor -IGArrivalCapture -IGReadingReview -IGSkipFrontend `
        "-IGCaptionScale=$TextScale" @readingCulture '-ExecCmds=Scalability 2,r.ScreenPercentage 100,t.MaxFPS 60' "-abslog=$readingLog" | Out-Null
    $readingResult = $LASTEXITCODE
}
finally {
    if ($null -ne $readingBackup) { [IO.File]::WriteAllBytes($readingSettings,$readingBackup) }
    elseif (Test-Path -LiteralPath $readingSettings) { Remove-Item -LiteralPath $readingSettings }
}
Select-String -LiteralPath $readingLog -Pattern 'READING_' | ForEach-Object { Write-Host $_.Line }
if ($readingResult -ne 0 -or -not (Select-String -LiteralPath $readingLog -Pattern 'READING_REVIEW PASS failures=0')) {
    throw "문서 실행 검사 실패: $readingLog"
}
$readingShots = @(Get-ChildItem -LiteralPath "$readingRoot/Docs/Media" -Filter 'reading-*.png' |
    Where-Object { $_.LastWriteTime -gt $readingStart })
if ($readingShots.Count -lt 5) { throw '문서 캡처가 빠졌다' }
# 언어를 고른 실행은 번역 확인용이라 저장소 사진 옆에 두지 않는다.
$readingDestination = if ($Culture) { "$readingRoot/Saved/Validation/Culture/$Culture" } else { "$readingRoot/Docs/Media" }
New-Item -ItemType Directory -Force -Path $readingDestination | Out-Null
foreach ($shot in $readingShots) {
    Move-Item -LiteralPath $shot.FullName -Destination "$readingDestination/$($shot.BaseName)-$readingKey.png" -Force
}

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
