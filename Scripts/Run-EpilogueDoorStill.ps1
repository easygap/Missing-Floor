[CmdletBinding()]
param()

# 에필로그 「403호 문」 정지 화면을 게임 안에서 찍는다. 303호 쪽지 셋을 떼고 마지막 쪽지만 붙인
# 403호 문을 복도에서 찍어 Content/SourceArt/T_EpilogueDoorNote_D.png(1024×1536)로 둔다. 쪽지 글씨는
# 읽히지 않는 거리이고 문 위 호수 표찰은 들지 않아 그림에 한글이 구워지지 않는다. 구도는
# IGDayScenesProbe의 DoorStill 값이다. 다음은 Import-EndingStills.ps1이다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
$logPath = Join-Path $projectRoot 'Saved/Logs/EpilogueDoorStill.log'
$probeUser = Join-Path $projectRoot ('Saved/Validation/EpilogueDoorStill-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
try {
	& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen -d3d12 -nosound `
		-Windowed -ResX=1024 -ResY=1536 -ForceRes -IGListenerGreybox -IGDayScenesProbe -IGEpilogueDoorStill -IGSkipFrontend `
		'-ExecCmds=Scalability 3,sg.ResolutionQuality 100,r.ScreenPercentage 100' `
		"-UserDir=$probeUser" "-abslog=$logPath" | Out-Null
} finally {
	if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
	elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
}
Select-String -LiteralPath $logPath -Pattern 'DAYSCENES_(SHOT|PROBE|CHECK)|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
$shot = Join-Path $probeUser 'Saved/DayScenesReview/epilogue-door-note.png'
if (-not (Test-Path -LiteralPath $shot)) { throw "정지 화면이 찍히지 않았다: $shot" }
$target = Join-Path $projectRoot 'Content/SourceArt/T_EpilogueDoorNote_D.png'
# 문은 세로로 긴 물건이라 가을 장면처럼 세로 2:3, 1024×1536 RGBA로 둔다.
& py -3.11 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).convert('RGB').resize((1024, 1536), Image.LANCZOS).convert('RGBA').save(sys.argv[2])" $shot $target
if ($LASTEXITCODE -ne 0) { throw '정지 화면을 옮기지 못했다.' }
Write-Host "EPILOGUE_DOOR_STILL $target"
