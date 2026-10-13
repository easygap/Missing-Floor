[CmdletBinding()]
param(
	[ValidateSet(30, 60)][int]$FrameRate = 60,
	[string[]]$Scenarios = @('Latched', 'Open', 'Hidden'),
	# 카트 뒤에 숨은 회차만 렌더를 켜고 돌려 카트 뒤 화면과 문간을 비추는 화면을 찍는다.
	# 창은 띄우지 않는다(오프스크린). 결과는 Saved/KeyHuntReview.
	[switch]$Shots
)

# 셋째 밤, 계단 열쇠 꾸러미가 없어진 걸 안 관리인이 열쇠를 찾으러 도는 길을 세 번 겪는다.
# 처음은 걸쇠를 건 403호 안에 있고 5층 철문에 빗장이 걸려 있다(문이 둘 다 버텨야 한다).
# 다음은 걸쇠도 빗장도 없이 5층 안에 서 있다(관리인은 403호를 열어 보고, 5층은 문을 열고
# 유담을 보면 들어오지 않고 문을 쾅 닫고 내려가야 한다). 마지막은 빗장 없이 5층 공구 카트
# 뒤에 숨어 있다(문을 열고 비춰 봐도 못 보고 조용히 닫고 가야 한다). 화면 없이(nullrhi) 돈다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'IndieGame.uproject'
$editor = & (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -Commandlet
# -File로 넘기면 쉼표 목록이 한 문자열로 온다.
$Scenarios = @($Scenarios | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if ($Shots) { $Scenarios = @('Hidden') }
$settingsPath = Join-Path $projectRoot 'Saved/Config/WindowsEditor/GameUserSettings.ini'
foreach ($scenario in $Scenarios) {
	$suffix = if ($Shots) { 'Shots' } else { "$FrameRate" }
	$logPath = Join-Path $projectRoot "Saved/Logs/ManagerKeyHunt-$scenario-$suffix.log"
	$probeUser = Join-Path $projectRoot ("Saved/Validation/ManagerKeyHunt-$scenario-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '/User')
	$extra = @()
	if ($scenario -eq 'Open') { $extra += '-IGManagerKeyHuntOpen' }
	if ($scenario -eq 'Hidden') { $extra += '-IGManagerKeyHuntHidden' }
	if ($Shots) {
		# 해상도 인자가 프로젝트의 GameUserSettings.ini를 고쳐 쓰므로 찍은 뒤 되돌린다.
		$settingsBytes = if (Test-Path -LiteralPath $settingsPath) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
		try {
			& $editor $projectPath -game -unattended -nosplash -NoLoadingScreen -RenderOffscreen -d3d12 -nosound `
				-Windowed -ResX=1600 -ResY=900 -ForceRes -IGListenerGreybox -IGManagerKeyHuntProbe -IGManagerKeyHuntShots `
				-IGSkipFrontend '-ExecCmds=Scalability 2,sg.ResolutionQuality 100,r.ScreenPercentage 100' `
				"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
		} finally {
			if ($null -ne $settingsBytes) { [IO.File]::WriteAllBytes($settingsPath, $settingsBytes) }
			elseif (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath }
		}
	}
	else {
		& $editor $projectPath -game -unattended -nosplash -nullrhi -nosound -NoLoadingScreen -RenderOffscreen `
			-IGListenerGreybox -IGManagerKeyHuntProbe -IGSkipFrontend -UseFixedTimeStep "-FPS=$FrameRate" `
			"-UserDir=$probeUser" "-abslog=$logPath" @extra | Out-Null
	}
	Write-Host "== $scenario"
	Select-String -LiteralPath $logPath -Pattern 'KEYHUNT|MANAGER_KEYHUNT|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
	if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'KEYHUNT_PROBE PASS failures=0')) {
		throw "관리인 열쇠 찾기 검사($scenario)에 실패했습니다: $logPath"
	}
}
