[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'IndieGame.uproject'

$requiredFiles = @(
    'IndieGame.uproject',
    'Config/DefaultEngine.ini',
    'Config/DefaultGame.ini',
	'Config/DefaultGameUserSettings.ini',
    'Config/DefaultInput.ini',
    'Config/DefaultGameplayTags.ini',
	'Content/Maps/Prologue_Morning.umap',
	'Content/Prototype/Materials/M_RoomWall.uasset',
	'Content/Prototype/Materials/M_RoomFloor.uasset',
	'Content/Prototype/Materials/M_DarkWood.uasset',
	'Content/Prototype/Materials/M_Bedding.uasset',
	'Content/Prototype/Materials/M_Door.uasset',
	'Content/Prototype/Materials/M_Alarm.uasset',
	'Content/Prototype/Materials/M_FridgeBody.uasset',
	'Content/Prototype/Materials/M_FridgeInterior.uasset',
	'Content/Prototype/Materials/M_WindowGlow.uasset',
	'Content/Prototype/Materials/M_Asphalt.uasset',
	'Content/Prototype/Materials/M_Concrete.uasset',
	'Content/Prototype/Materials/M_ConcreteDark.uasset',
	'Content/Prototype/Materials/M_WindowDark.uasset',
	'Content/Prototype/Materials/M_NightSky.uasset',
	'Content/Prototype/Materials/M_StreetLampGlow.uasset',
	'Content/Prototype/Materials/M_TrashBag.uasset',
	'Content/Prototype/Materials/M_Cardboard.uasset',
	'Content/Prototype/Materials/M_StoreFloor.uasset',
	'Content/Prototype/Materials/M_LightPanel.uasset',
	'Content/Prototype/Materials/M_SignMint.uasset',
	'Content/Prototype/Materials/M_SignWhite.uasset',
	'Content/Prototype/Materials/M_Glass.uasset',
	'Content/Prototype/Materials/M_MetalFrame.uasset',
	'Content/Prototype/Materials/M_PlasticDark.uasset',
	'Content/Prototype/Materials/M_CounterTop.uasset',
	'Content/Prototype/Materials/M_CoolerBody.uasset',
	'Content/Prototype/Materials/M_ScreenGlow.uasset',
	'Content/Prototype/Materials/M_WaterBlue.uasset',
	'Content/Prototype/Materials/M_SignBladeLit.uasset',
	'Content/Prototype/Materials/M_SignMainLit.uasset',
	'Content/Prototype/Materials/M_BottleGreen.uasset',
	'Content/Prototype/Materials/M_BottleBrown.uasset',
	'Content/Prototype/Materials/M_SnackRed.uasset',
	'Content/Prototype/Materials/M_SnackYellow.uasset',
	'Content/Prototype/Materials/M_SnackBlue.uasset',
	'Content/SourceArt/AI/SheetPaperNotes_v2.png',
	'Content/SourceArt/AI/ApplicationIcon_20260930.png',
	'Content/SourceArt/AI/DialogueHUDConcept_v1.png',
	'Content/SourceArt/AI/TextureHudDialogueFilm.png',
	'Content/SourceArt/AI/ApartmentVisualTarget_v1.png',
	'Content/SourceArt/AI/TextureApartmentWallpaperVintage.png',
	'Content/SourceArt/AI/TextureMovingBoxCardboard_v1.png',
	'Content/SourceArt/AI/TextureCaptureMercyNotePaper_D.png',
	'Content/SourceArt/AI/MaskApartmentWallPatina.png',
	'Content/SourceArt/AI/TitleBackgroundMissingFloor_v1.png',
	'Content/SourceArt/T_HudDialogueFilm_D.png',
	'Content/SourceArt/T_TitleBackground_D.png',
	'Content/SourceArt/T_ApartmentWallpaperV2_D.png',
	'Content/SourceArt/T_ApartmentWallpaperV2_N.png',
	'Content/SourceArt/T_ApartmentWallpaperV2_R.png',
	'Content/SourceArt/T_ApartmentWallpaperV2_A.png',
	'Content/SourceArt/T_ApartmentWallPatina_M.png',
	'Content/SourceArt/T_MovingBoxCardboard_D.png',
	'Content/SourceArt/T_MovingBoxCardboard_N.png',
	'Content/SourceArt/T_MovingBoxCardboard_R.png',
	'Content/SourceArt/T_MovingBoxCardboard_A.png',
	'Content/SourceArt/T_CaptureMercyNote_D.png',
	'Build/Windows/ApplicationIcon.png',
	'Build/Windows/Application.ico',
	'Content/Prototype/Textures/T_PaperClean_V2_D.uasset',
	'Content/Prototype/Textures/T_PaperWet_V2_D.uasset',
	'Content/Prototype/Textures/T_PaperFolded_V2_D.uasset',
	'Content/Prototype/Textures/T_PaperOld_V2_D.uasset',
	'Content/Prototype/Textures/T_HudDialogueFilm_D.uasset',
	'Content/UI/Textures/T_TitleBackground_D.uasset',
	'Content/Prototype/Textures/T_ApartmentWallpaperV2_D.uasset',
	'Content/Prototype/Textures/T_ApartmentWallpaperV2_N.uasset',
	'Content/Prototype/Textures/T_ApartmentWallpaperV2_R.uasset',
	'Content/Prototype/Textures/T_ApartmentWallpaperV2_A.uasset',
	'Content/Prototype/Textures/T_ApartmentWallPatina_M.uasset',
	'Content/Prototype/Materials/M_ApartmentWallPatina.uasset',
	'Content/Prototype/Textures/T_MovingBoxCardboard_D.uasset',
	'Content/Prototype/Textures/T_MovingBoxCardboard_N.uasset',
	'Content/Prototype/Textures/T_MovingBoxCardboard_R.uasset',
	'Content/Prototype/Textures/T_MovingBoxCardboard_A.uasset',
	'Content/Prototype/Materials/M_MovingBoxCardboardUV.uasset',
	'Content/Prototype/Textures/T_CaptureMercyNote_D.uasset',
	'Content/Prototype/Materials/M_CaptureMercyNote.uasset',
	'Content/Meshes/SM_CaptureMercyNote.uasset',
	'Content/Prototype/Textures/T_SignMain_D.uasset',
	'Content/Prototype/Textures/T_PosterSale_D.uasset',
	'Content/Prototype/Textures/T_LabelWater_D.uasset',
	'Docs/Media/dialogue-hud-default-1080.png',
	'Docs/Media/dialogue-hud-accessibility-200-1080.png',
	'Docs/Media/title-menu-first-run-1080.png',
	'Docs/Media/m65-capture-mercy-note.png',
	'Docs/Media/m65-mercy-note-slide.gif',
	'Docs/Media/m65-first-run-audio-calibration.png',
	'Docs/Media/settings-display-1080.png',
	'Docs/Media/settings-accessibility-1080.png',
	'Source/IndieGame/UI/Fonts/Pretendard-Regular.otf',
	'Source/IndieGame/UI/Fonts/Pretendard-SemiBold.otf',
	'Source/IndieGame/UI/Fonts/GowunBatang-Bold.ttf',
	'Source/IndieGame/UI/Fonts/OFL-Pretendard.txt',
	'Source/IndieGame/UI/Fonts/OFL-GowunBatang.txt',
	'Docs/Media/readme-route-preview.gif',
	'Docs/IMAGEGEN_PROMPTS_2026-09-30.md',
	'Docs/IMAGEGEN_PROMPTS_2026-08-12.md',
	'Docs/IMAGEGEN_PROMPTS_2026-08-12_AUDIO_CALIBRATION.md',
	'Docs/PERFORMANCE.md',
	'Docs/UI_STYLE_GUIDE.md',
	'Scripts/RunGame.bat',
	'Scripts/Resolve-UnrealEditor.ps1',
	'Scripts/Run-MissingFloor-FrontendShippingProbe.ps1',
	'Scripts/Run-MissingFloor-SettingsPreview.ps1',
	'Scripts/Test-MissingFloor-AudioContract.ps1',
	'Scripts/Test-MissingFloor-AccessibilityContract.ps1',
	'Scripts/Test-MissingFloor-DialogueContract.ps1',
	'Scripts/Test-MissingFloor-FrontendContract.ps1',
	'Scripts/prepare_application_icon.py',
	'Scripts/Test-Windows-ExecutableIcon.ps1',
	'Scripts/Copy-Windows-ExecutableVersionResource.ps1',
	'Scripts/Test-Windows-ExecutableMetadata.ps1',
	'Scripts/Test-WindowsPackageManifest.ps1',
	'Scripts/Test-WindowsPackageManifestRegression.ps1',
	'Scripts/Run-MissingFloor-SaveRecoveryProbe.ps1',
	'Scripts/Run-MissingFloor-EndingLifecycleProbe.ps1',
	'Source/IndieGame/Entity/IGEndingLifecycleProbe.cpp',
	'Scripts/create_readme_media.py',
	'Scripts/Test-ArtAssetContract.ps1',
	'Scripts/Test-MissingFloor-M0InputContract.ps1',
	'Scripts/Test-MissingFloor-ProductionEntryContract.ps1',
	'Scripts/Run-MissingFloor-ArrivalProbe.ps1',
	'Scripts/Run-MissingFloor-ArrivalCapture.ps1',
	'Scripts/Test-MissingFloor-M5RevealContract.ps1',
	'Scripts/Test-MissingFloor-ReleaseEndingContract.ps1',
	'Scripts/Test-MissingFloor-ReleaseGateContract.ps1',
	'Scripts/Test-MissingFloor-SignatureSfxContract.ps1',
	'Scripts/Test-MissingFloor-TuningTableContract.ps1',
	'Scripts/Test-MissingFloor-MixAndMovementContract.ps1',
	'Scripts/Test-MissingFloor-InputBindingContract.ps1',
	'Scripts/Test-MissingFloor-BibleContract.ps1',
	'Scripts/Run-MissingFloor-EndingPreview.ps1',
	'Scripts/Test-MissingFloor-M6AudioVisualContract.ps1',
	'Scripts/Test-MissingFloor-M65MercyNoteContract.ps1',
	'Scripts/Test-MissingFloor-M8DifficultyContract.ps1',
	'Scripts/Test-MissingFloor-M65AudioCalibrationContract.ps1',
	'Scripts/Test-MissingFloor-M3CctvChannelContract.ps1',
	'Scripts/Test-MissingFloor-M3DoorBeatContract.ps1',
	'Scripts/Test-MissingFloor-M4PassByContract.ps1',
	'Scripts/Test-MissingFloor-NightFiveSlotContract.ps1',
	'Scripts/Run-MissingFloor-NightFiveProbe.ps1',
	'Scripts/Run-MissingFloor-CctvFeedProbe.ps1',
	'Scripts/Run-MissingFloor-AudioCalibrationPreview.bat',
	'Scripts/Build-ArtAssets.ps1',
	'Scripts/RunEditor.bat',
    'Source/IndieGame.Target.cs',
    'Source/IndieGameEditor.Target.cs',
    'Source/IndieGame/IndieGame.Build.cs',
    'Source/IndieGame/IndieGame.h',
	'Source/IndieGame/IndieGame.cpp',
	'Source/IndieGame/Player/IGFrontendMenuLayout.h',
	'Source/IndieGame/Sequence/IGObjectiveProvider.h',
	'Source/IndieGame/Accessibility/IGAccessibilitySubsystem.h',
	'Source/IndieGame/Accessibility/IGAccessibilitySubsystem.cpp',
	'Source/IndieGame/Core/IGPrologueGameMode.cpp',
	'Source/IndieGame/Save/IGSaveGame.h',
	'Source/IndieGame/Save/IGSaveSubsystem.cpp',
	'Source/IndieGame/Environment/IGNeighborhoodLifeDirector.h',
	'Source/IndieGame/Environment/IGNeighborhoodLifeDirector.cpp'
)

$missing = @(
    foreach ($relativePath in $requiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $relativePath))) {
            $relativePath
        }
    }
)

if ($missing.Count -gt 0) {
    throw "Missing required project files: $($missing -join ', ')"
}

$readme = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'README.md')
foreach ($requiredReadmeToken in @(
	# 소개에 쓰는 실제 화면과 실행 안내가 빠지지 않았는지 확인한다.
	# 퍼즐 해답이나 개별 연출의 검증용 캡처를 README에 고정하지 않는다.
	'Docs/Media/readme/game-corridor-day.webp',
	'Docs/Media/readme/game-corridor-night.webp',
	'Docs/Media/readme/game-bedroom.webp',
	'Docs/Media/readme/game-alley.webp',
	'Docs/Media/readme/game-store.webp',
	'Docs/Media/readme/game-booth.webp',
	'Docs/Media/readme/readme-route-preview.gif',
	'Docs/Media/readme/night-listener-chase.gif',
	'Docs/Media/readme/p1-meter-cabinet.webp',
	'Docs/PLAYING.md',
	'## 조작',
	'## 난이도와 설정',
	'## 다운로드'
)) {
	if (-not $readme.Contains($requiredReadmeToken)) {
		throw "README product overview is missing: $requiredReadmeToken"
	}
}
foreach ($forbiddenReadmeToken in @(
	'## Contributors',
	'## 기여자'
)) {
	if ($readme.Contains($forbiddenReadmeToken)) {
		throw "README must not contain a generated contributor section: $forbiddenReadmeToken"
	}
}
$readmeMediaReferences = @(
	[regex]::Matches($readme, '(?:src="|\]\()(?<path>Docs/[^\)"]+)') |
		ForEach-Object { $_.Groups['path'].Value } |
		Sort-Object -Unique
)
foreach ($readmeMediaReference in $readmeMediaReferences) {
	# 문서 안의 제목으로 가는 링크는 # 앞의 파일 경로만 확인한다.
	$readmeFilePath = $readmeMediaReference.Split('#')[0]
	if (-not (Test-Path -LiteralPath (
		Join-Path $projectRoot $readmeFilePath) -PathType Leaf)) {
		throw "README media or document link is missing: $readmeMediaReference"
	}
}
$readmeGif = Get-Item -LiteralPath (
	Join-Path $projectRoot 'Docs/Media/readme/readme-route-preview.gif')
if ($readmeGif.Length -lt 500KB -or $readmeGif.Length -gt 10MB) {
	throw 'README route preview must stay legible and below the 10 MB review budget.'
}
$chaseGif = Get-Item -LiteralPath (
	Join-Path $projectRoot 'Docs/Media/readme/night-listener-chase.gif')
if ($chaseGif.Length -lt 500KB -or $chaseGif.Length -gt 10MB) {
	throw 'README chase preview must stay legible and below the 10 MB review budget.'
}
# 접힌 영역의 이미지도 내려받을 수 있으므로, 실제 이미지 태그를 모두 센다.
# 일반 링크로 제공하는 장면 모음 GIF는 본문 이미지 용량에 넣지 않는다.
$readmeInlineImages = @(
	[regex]::Matches($readme, '(?:src="|!\[[^\]]*\]\()(?<path>Docs/[^\)"]+)') |
		ForEach-Object { $_.Groups['path'].Value } |
		Sort-Object -Unique
)
$readmeInlineBytes = 0L
foreach ($readmeInlineImage in $readmeInlineImages) {
	if (-not $readmeInlineImage.StartsWith('Docs/Media/readme/')) {
		throw "README 본문에는 축소본을 사용해 주세요: $readmeInlineImage"
	}
	$readmeInlineBytes += (Get-Item -LiteralPath (
		Join-Path $projectRoot $readmeInlineImage)).Length
}
if ($readmeInlineBytes -gt 6MB) {
	throw 'README 본문 이미지의 총용량은 6 MB 이하여야 합니다.'
}
$readmeMediaRecipe = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts/create_readme_media.py')
foreach ($requiredRecipeToken in @(
	'CAPTURES = (',
	'game-bedroom.png',
	'game-store.png',
	'Actual in-game capture route preview'
)) {
	if (-not $readmeMediaRecipe.Contains($requiredRecipeToken)) {
		throw "README media recipe is missing: $requiredRecipeToken"
	}
}

$descriptor = Get-Content -Raw -LiteralPath $projectFile | ConvertFrom-Json
if ($descriptor.FileVersion -ne 3) {
    throw 'IndieGame.uproject must use descriptor FileVersion 3.'
}

if (-not ($descriptor.Modules | Where-Object { $_.Name -eq 'IndieGame' -and $_.Type -eq 'Runtime' })) {
    throw 'IndieGame.uproject is missing the IndieGame Runtime module.'
}

if ($descriptor.EngineAssociation -ne '5.8') {
    throw 'IndieGame.uproject must target the portable launcher association 5.8.'
}

$androidFileServer = @(
	$descriptor.Plugins |
		Where-Object { $_.Name -eq 'AndroidFileServer' }
)
if ($androidFileServer.Count -ne 1 -or
	[bool]$androidFileServer[0].Enabled) {
	throw 'AndroidFileServer must stay explicitly disabled to prevent token generation.'
}

$engineResolver = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts/Resolve-UnrealEditor.ps1')
foreach ($resolverInvariant in @(
	'$env:IG_UNREAL_EDITOR',
	'LauncherInstalled.dat',
	'Build\Build.version',
	'Test-EngineAssociation',
	'[switch]$Commandlet',
	'UnrealEditor-Cmd.exe'
)) {
	if (-not $engineResolver.Contains($resolverInvariant)) {
		throw "Portable Unreal resolver invariant is missing: $resolverInvariant"
	}
}
# Run it, do not just read it. Every launcher and every art build starts by
# asking this script where the engine is, so the one thing it must never do is
# fail in a way that is not its own failure. It used to: Join-Path rejects a
# null -Path, and on a host with no ProgramFiles the candidate list threw
# during its own construction -- before the IG_UNREAL_EDITOR override this
# script's error message recommends had been tried at all.
#
# Both outcomes are correct here. A machine with the engine prints its path;
# a machine without it says so. A third outcome -- some other exception -- is
# the bug, and only running it can tell those apart.
#
# The resolver sets its own $ErrorActionPreference = 'Stop', which turns its
# closing Write-Error into a terminating error in this scope, so the missing
# engine arrives as an exception rather than as output. Catch it and read the
# message either way; the distinction being drawn is which message, not how
# it travelled.
$resolverText = ''
try {
	$resolverText = (
		& (Join-Path $projectRoot 'Scripts/Resolve-UnrealEditor.ps1') `
			-ProjectPath $projectFile -Commandlet 2>&1 | Out-String)
}
catch {
	$resolverText = [string]$_.Exception.Message
}
if ($resolverText -notmatch 'UnrealEditor(-Cmd)?\.exe' -and
	$resolverText -notmatch 'was not found\. Install it or set IG_UNREAL_EDITOR') {
	throw (
		'Resolve-UnrealEditor.ps1 failed with something other than its own ' +
		"missing-engine error: $($resolverText.Trim())")
}
$headlessScripts = @(
	'Scripts/Build-ArtAssets.ps1'
)
foreach ($headlessScript in $headlessScripts) {
	$headlessText = Get-Content -Raw -Encoding UTF8 -LiteralPath (
		Join-Path $projectRoot $headlessScript)
	foreach ($headlessInvariant in @(
		'-Commandlet',
		'-nullrhi',
		'-nosound',
		'-RenderOffscreen'
	)) {
		if (-not $headlessText.Contains($headlessInvariant)) {
			throw "Headless Unreal invariant is missing ($headlessInvariant): $headlessScript"
		}
	}
	foreach ($forbiddenFallback in @(
		'$editorCommand = $editor',
		'$editorCommand = $editorExecutable'
	)) {
		if ($headlessText.Contains($forbiddenFallback)) {
			throw "Headless Unreal script can fall back to a visible editor: $headlessScript"
		}
	}
}
# 자동 검사나 반입이 게임·에디터 창을 사용자 화면 앞에 띄우면 안 된다.
# -RenderOffscreen이 있으면 엔진이 창을 아예 만들지 않는다. 숨김 실행(-WindowStyle Hidden)은
# 런처만 숨기고 실제 게임 프로세스의 창은 그대로 뜨므로 대신이 되지 못한다.
# 일부러 창을 보여 줘야 하는 스크립트만 'visible-window: intentional' 표식을 단다.
$launchScripts = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Scripts') -Recurse -File |
	Where-Object { $_.Extension -in @('.ps1', '.bat') -and $_.Name -ne 'Validate-Project.ps1' })
foreach ($launchScript in $launchScripts) {
	$launchText = Get-Content -Raw -Encoding UTF8 -LiteralPath $launchScript.FullName
	$launchesUnreal = $launchText -match 'Resolve-UnrealEditor\.ps1' -or
		$launchText -match '(?i)Start-Process\s+-FilePath\s+\$(launcher|game\w*|exe\w*)\b'
	if (-not $launchesUnreal) { continue }
	if ($launchText.Contains('visible-window: intentional')) { continue }
	# 커맨드릿(-run=)과 UAT 쿠크는 처음부터 창을 만들지 않는다.
	$runsOnlyCommandlets = ($launchText -match '(?i)-run=|RunUAT') -and
		($launchText -notmatch '(?i)-game\b|-ExecutePythonScript')
	if ($runsOnlyCommandlets) { continue }
	if ($launchText -notmatch '(?i)-RenderOffscreen') {
		throw "게임이나 에디터 창이 화면에 뜰 수 있습니다. -RenderOffscreen을 넣거나 의도를 표시하세요: $($launchScript.FullName)"
	}
}
foreach ($launcherScript in @(
	'Scripts/RunGame.bat',
	'Scripts/RunEditor.bat'
)) {
	$launcherText = Get-Content -Raw -Encoding UTF8 -LiteralPath (
		Join-Path $projectRoot $launcherScript)
	if (-not $launcherText.Contains('Resolve-UnrealEditor.ps1') -or
		$launcherText.Contains(
			'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe')) {
		throw "Launcher must use the portable Unreal resolver: $launcherScript"
	}
}

$engineConfig = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'Config/DefaultEngine.ini')

foreach ($requiredSetting in @(
	'GameDefaultMap=/Game/Maps/Prologue_Morning',
	'GlobalDefaultGameMode=/Script/IndieGame.IGPrologueGameMode',
	'r.TextureStreaming=True',
	'r.PSOPrecaching=1',
	'r.PSOPrecache.Components=1',
	'r.PSOPrecache.GlobalShaders=1'
)) {
	if ($engineConfig -notmatch [regex]::Escape($requiredSetting)) {
		throw "Default playable scene setting is missing: $requiredSetting"
	}
}

$configFiles = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Config') -Recurse -File
$securityTokens = @($configFiles | Select-String -SimpleMatch 'SecurityToken=')
if ($securityTokens.Count -gt 0) {
	$locations = $securityTokens | ForEach-Object {
		$relativePath = $_.Path.Substring($projectRoot.Length + 1)
		"${relativePath}:$($_.LineNumber)"
	}
	throw "SecurityToken must not be committed under Config: $($locations -join ', ')"
}

$headers = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Source') -Recurse -Filter '*.h'
foreach ($header in $headers) {
    $includeLines = @(Select-String -LiteralPath $header.FullName -Pattern '^#include ')
    $generatedIncludes = @($includeLines | Where-Object { $_.Line -match '\.generated\.h"$' })

    if ($generatedIncludes.Count -gt 1) {
        throw "Multiple generated headers found in $($header.FullName)."
    }

    if ($generatedIncludes.Count -eq 1 -and $generatedIncludes[0].LineNumber -ne $includeLines[-1].LineNumber) {
        throw "Generated include must be the final include in $($header.FullName)."
    }
}

# 감사들은 못 보는 자리를 스스로 보고한다. 그 숫자가 늘어나는 것은 검사가
# 조용히 눈이 머는 것인데, findings=0은 그대로라 화면에서 구분되지 않는다.
# 지금 값을 천장으로 박아 두고 넘으면 막는다. 줄었으면 천장도 같이 내린다 —
# 낡은 천장은 「여기까지는 못 봐도 된다」로 읽힌다.
function Assert-AuditBlindSpot {
	param(
		[Parameter(Mandatory = $true)][AllowNull()]$Output,
		[Parameter(Mandatory = $true)][string]$Pattern,
		[Parameter(Mandatory = $true)][int]$Ceiling,
		[Parameter(Mandatory = $true)][string]$What
	)
	# 보고 줄이 아예 없으면 못 보는 것이 하나도 없다는 뜻이다.
	$blindMatch = [regex]::Match(($Output | Out-String), $Pattern)
	$blindCount = 0
	if ($blindMatch.Success) {
		$blindCount = [int]$blindMatch.Groups['count'].Value
	}
	if ($blindCount -gt $Ceiling) {
		throw (
			'감사가 못 보는 자리가 늘었다 — {0}: {1}건(천장 {2}건)' -f
				$What, $blindCount, $Ceiling)
	}
	if ($blindCount -lt $Ceiling) {
		throw (
			'감사가 더 많이 보게 됐다 — {0}: {1}건. 천장을 {1}로 내려라(지금 {2})' -f
				$What, $blindCount, $Ceiling)
	}
}

$utf8Strict = New-Object System.Text.UTF8Encoding($false, $true)
$koreanSourceFilesWithoutBom = @(
	# -Include with -LiteralPath is provider-dependent and has admitted binary
	# font files on some PowerShell versions. Filter FileInfo objects explicitly
	# before any byte stream is decoded as UTF-8.
	Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Source') -Recurse -File |
		Where-Object { $_.Extension -in @('.h', '.cpp') } |
		ForEach-Object {
			$sourceFile = $_
			$bytes = [System.IO.File]::ReadAllBytes($sourceFile.FullName)
			try {
				$text = $utf8Strict.GetString($bytes)
			}
			catch {
				throw "Source file is not valid UTF-8: $($sourceFile.FullName)"
			}

			if ($text -match '[\uAC00-\uD7A3]') {
				$hasUtf8Bom = $bytes.Length -ge 3 -and
					$bytes[0] -eq 0xEF -and
					$bytes[1] -eq 0xBB -and
					$bytes[2] -eq 0xBF

				if (-not $hasUtf8Bom) {
					$sourceFile.FullName.Substring($projectRoot.Length + 1)
				}
			}
		}
)
if ($koreanSourceFilesWithoutBom.Count -gt 0) {
	throw "C++ source files containing Korean literals must use UTF-8 BOM: $($koreanSourceFilesWithoutBom -join ', ')"
}

# 한 파일 안에서 줄바꿈이 섞이면 사람 눈에는 안 보이는데 도구는 걸린다. 문자열
# 바늘이 안 맞아 패치가 조용히 빗나가고, 편집기마다 다른 줄에 커서를 놓는다.
# 저장소에 들어가는 형태는 `.gitattributes`의 `text=auto`가 LF로 맞춰 주기
# 때문에 git diff에는 아무것도 안 뜬다. 작업 트리는 아무도 안 보고 있었다.
#
# 갓 받아 온 클론은 어느 OS에서든 한 가지로 통일돼 있다. 여기서 섞였다는 것은
# 도구가 다른 줄바꿈으로 덧썼다는 뜻이다.
$lineEndingExtensions = @(
	'.h', '.cpp', '.cs', '.ps1', '.py', '.md', '.ini', '.json', '.bat',
	'.txt', '.uproject')
$mixedLineEndingFiles = @(
	@(Get-ChildItem -LiteralPath $projectRoot -File) + @(
		@('Source', 'Scripts', 'Docs', 'Config') | ForEach-Object {
			$searchRoot = Join-Path $projectRoot $_
			if (Test-Path -LiteralPath $searchRoot -PathType Container) {
				Get-ChildItem -LiteralPath $searchRoot -Recurse -File
			}
		}) |
		Where-Object { $_.Extension -in $lineEndingExtensions } |
		ForEach-Object {
			$candidate = $_
			$text = [System.Text.Encoding]::UTF8.GetString(
				[System.IO.File]::ReadAllBytes($candidate.FullName))
			$carriageReturns = [regex]::Matches($text, "`r`n").Count
			$lineFeeds = [regex]::Matches($text, "`n").Count
			if ($carriageReturns -gt 0 -and $lineFeeds -gt $carriageReturns) {
				$candidate.FullName.Substring($projectRoot.Length + 1)
			}
		}
)
if ($mixedLineEndingFiles.Count -gt 0) {
	throw (
		'Line endings are mixed inside these files: {0}' -f
			($mixedLineEndingFiles -join ', '))
}

# 파일만 있고 아무도 안 부르는 계약. M8 난이도(108개 단언)와 REBIRTH 증거
# (113개)가 그랬다 — 통과도 하는데 검증기가 부르질 않아 그냥 안 돌고 있었다.
# 저장소 목록에는 계약이 있고, 화면에서는 「본다」와 구분되지 않는다.
$registeredChecks = Get-Content -Raw -Encoding UTF8 -LiteralPath $PSCommandPath
$unregisteredChecks = @(
	Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Scripts') -File |
		Where-Object {
			($_.Name -like 'Test-*.ps1') -or ($_.Name -like 'audit_*.py')
		} |
		Where-Object { -not $registeredChecks.Contains($_.Name) } |
		ForEach-Object { $_.Name })
if ($unregisteredChecks.Count -gt 0) {
	throw (
		'검증기가 부르지 않는 검사가 있다: {0}' -f
			($unregisteredChecks -join ', '))
}

# 그레이박스 셋업은 액터를 열다섯 개 순서대로 세우고 중간 어디서든 false로
# 빠진다. 빠지면 0.3초 뒤 처음부터 다시 도는데, 스폰에 이름을 지정하므로
# 만들다 만 액터가 살아 있으면 같은 이름 때문에 다음 스폰이 실패한다. 그러면
# 재시도가 영영 통과하지 못하고 6초 뒤 「월드 씬이나 플레이어가 없다」로
# 끝난다 — 실제 이유와 다른 말이다.
#
# 그래서 세우는 목록과 치우는 목록이 같아야 한다. 새 디렉터를 하나 더 세우면
# 여기서 걸린다.
$greyboxStageSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp')
$stageSetupBody = [regex]::Match(
	$greyboxStageSource,
	'bool AIGListenerGreyboxDirector::SetupStage\(\)\r?\n\{(?<body>[\s\S]*?)\r?\n\}')
$stageTeardownBody = [regex]::Match(
	$greyboxStageSource,
	'void AIGListenerGreyboxDirector::DestroyPartialStage\(\)\r?\n\{(?<body>[\s\S]*?)\r?\n\}')
if (-not $stageSetupBody.Success -or -not $stageTeardownBody.Success) {
	throw 'The greybox stage setup or its teardown could not be read.'
}
if ($stageSetupBody.Groups['body'].Value -notmatch 'DestroyPartialStage\(\);') {
	throw 'The greybox stage setup no longer clears what a failed attempt left behind.'
}
# 셋업이 직접 세우는 것과, 셋업이 부르는 헬퍼가 세우는 것을 함께 센다.
# 증인 다섯은 지금 마지막 실패 경로보다 뒤에 있지만, 그 사이에 실패가 하나
# 생기면 이름이 살아남아 재시도를 막는다.
# 셋업이 부르는 스폰 헬퍼는 셋이다. 셋업 본문만 보면 이 열둘을 놓친다. 밤 베드는
# 액터가 아니라 이름 붙인 컴포넌트라 teardown이 DestroyComponent로 걷는다.
$stageHelperBodies = ''
foreach ($stageHelperName in @('SpawnOptionalWitnesses', 'SpawnArrivalInteractables', 'SpawnNightAmbienceBeds')) {
	$stageHelperBody = [regex]::Match(
		$greyboxStageSource,
		'void AIGListenerGreyboxDirector::' + $stageHelperName +
			'\([^)]*\)\r?\n\{(?<body>[\s\S]*?)\r?\n\}')
	if (-not $stageHelperBody.Success) {
		throw "The greybox spawner $stageHelperName could not be read."
	}
	$stageHelperBodies += $stageHelperBody.Groups['body'].Value
}
# 셋을 넘어 더 부르기 시작하면 위 목록으로는 부족해진다.
$stageHelperCalls = @(
	[regex]::Matches(
		$stageSetupBody.Groups['body'].Value,
		'(?m)^\s*(?<name>Spawn[A-Za-z0-9_]*)\(') |
		ForEach-Object { $_.Groups['name'].Value } |
		Sort-Object -Unique)
foreach ($stageHelper in $stageHelperCalls) {
	if ($stageHelper -notin @('SpawnOptionalWitnesses', 'SpawnArrivalInteractables', 'SpawnNightAmbienceBeds')) {
		throw (
			'The greybox stage setup calls {0}; the teardown audit does not follow it.' -f
				$stageHelper)
	}
}
# 람다로 세우는 것(SpawnEvidence)도 같은 이름 규칙을 쓰므로 함께 센다.
$stageSpawned = @(
	[regex]::Matches(
		$stageSetupBody.Groups['body'].Value + $stageHelperBodies,
		'(?m)^\s*(?<name>[A-Za-z_][A-Za-z0-9_]*) = (?:World->SpawnActor<|SpawnEvidence\()') |
		ForEach-Object { $_.Groups['name'].Value } |
		Sort-Object -Unique)
if ($stageSpawned.Count -lt 27) {
	throw (
		'The greybox stage builds {0} actors; twenty-seven were authored.' -f
			$stageSpawned.Count)
}
foreach ($stageActor in $stageSpawned) {
	if ($stageTeardownBody.Groups['body'].Value -notmatch
		('(?m)^\s*' + [regex]::Escape($stageActor) + ' = nullptr;')) {
		throw (
			'The greybox stage builds {0} but never clears it; a failed attempt would block the retry.' -f
				$stageActor)
	}
}

$tickingActors = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Source') -Recurse -Include '*.h','*.cpp' |
    Select-String -Pattern 'PrimaryActorTick\.bCanEverTick\s*=\s*true'
# Reviewed exceptions. Every entry sets bStartWithTickEnabled = false; the
# first group enables Tick only for bounded animation/presentation windows and
# switches it off again. IGPlayerController ticks during the 10-second display
# confirmation and the bounded -IGFrontendShippingProbe process only.
# IGMissingFloorNightFourDirector starts disabled and wakes only while the
# final reveal, Mok retreat, ending prop movement, or the two camera-dependent
# detail layers are visible. It disables itself as soon as those presentation
# windows close.
# IGNightLoopDirector는 기본 Tick을 끄고, 5회 포획 후 종이가 미끄러지는
# 0.82초에만 켠 뒤 다시 타이머 기반 리셋 처리로 돌아간다.
# IGMissingFloorFifthDawnDirector는 기본 Tick을 끄고, 재관람 스킵을 누르는
# 동안과 중도 해제 후 진행률을 되감는 짧은 구간에만 켠다. 완료 또는
# 되감기 종료 즉시 스스로 비활성화하며 평상시 비용은 발생하지 않는다.
# IGMissingFloorEpilogueDirector는 같은 이유로 같은 방식이다. 87초 시각표는
# 타이머가 밀고, Tick은 재관람 우회를 누르는 동안과 중도 해제 뒤 진행률을
# 되감는 구간에만 켜진다. 마지막 카드로 건너뛰거나 되감기가 끝나면 스스로
# 비활성화한다.
# IGMissingFloorMercyDirector는 기본 Tick을 끄고, 문 아래로 종이가 밀려
# 들어오는 0.94초 동안만 켠 뒤 스스로 끈다. 90초 정체 시계는 타이머다.
# IGCctvChannelFive는 기본 Tick을 끄고, 채널 5가 화면에 있는 6.78초 동안만
# 켠다. 그 Tick이 초당 12회 씬 캡처와 낮은 형체의 이동을 구동하며, 채널이
# 죽는 프레임에 렌더타깃·캡처·형체를 해제하고 자신을 비활성화한다. 이것이
# §14의 상시 렌더 금지를 만족시키는 방식이므로 타이머로 대체할 수 없다.
#
# IGListenerEntity is the one deliberate always-on actor tick in the project.
# 위층 사람 is a pursuer: its state machine, crawl locomotion, drag-loop gain
# and threat pressure are per-frame concerns for its whole life, exactly like
# the player pawn's. It exists only while a night stage is armed
# (-IGListenerGreybox spawns it), it owns no timers that could substitute for
# the tick, and gating it would make the pursuit visibly step.
$reviewedTickingFiles = @(
	# 평소에는 꺼 둔다. 엔딩 저장·재시작 검사 인자가 있을 때만 켜고,
	# 에필로그 뒤 타이틀이 게임을 멈춘 상태에서도 프로필 결과를 읽은 뒤 종료한다.
	'IGListenerGreyboxDirector.cpp',
	# 전용 실행 인자에서만 생성하고 약 3초 뒤 종료한다. 실제 입력 제동 거리를 잰다.
	'IGGameplayRealismProbe.cpp',
	# 전용 인자로만 생성한다. 실제 오디오 페이드와 충돌을 순서대로 확인한 뒤 종료한다.
	# 일반 플레이에서는 생성하지 않으며, 후처리 Tick에서 같은 프레임의 상태를 읽는다.
	'IGAudioPresentationProbe.cpp',
	'IGPlayerCharacter.cpp',
	'IGPlayerController.cpp',
	'IGFridge.cpp',
	'IGSwingDoor.cpp',
	'IGSlidingDoor.cpp',
	'IGElevator.cpp',
	'IGNeighborhoodLifeDirector.cpp',
	'IGListenerEntity.cpp',
	'IGMissingFloorNightFourDirector.cpp',
	'IGNightLoopDirector.cpp',
	'IGMissingFloorFifthDawnDirector.cpp',
	'IGMissingFloorEpilogueDirector.cpp',
	'IGMissingFloorMercyDirector.cpp',
	'IGCctvChannelFive.cpp',
	# 숨는 자리. 사람이 드나들거나 안에 있는 동안에만 켜고, 다 나오면 끈다.
	'IGHidingSpot.cpp',
	# 어둠의 몸(어둑시니·손님). 나타나 있는 동안에만 켜고, 사라지면 끈다.
	'IGShadowFigure.cpp'
)
$unreviewedTickingActors = @($tickingActors | Where-Object {
	$reviewedTickingFiles -notcontains [System.IO.Path]::GetFileName($_.Path)
})
if ($unreviewedTickingActors.Count -gt 0) {
    $locations = $unreviewedTickingActors | ForEach-Object { "$($_.Path):$($_.LineNumber)" }
    throw "Actor Tick requires an explicit architecture review: $($locations -join ', ')"
}
$alwaysTickingComponents = @(Get-ChildItem `
	-LiteralPath (Join-Path $projectRoot 'Source') `
	-Recurse `
	-Include '*.h','*.cpp' |
	Select-String -Pattern 'PrimaryComponentTick\.bStartWithTickEnabled\s*=\s*true')
if ($alwaysTickingComponents.Count -gt 0) {
	$locations = $alwaysTickingComponents | ForEach-Object {
		"$($_.Path):$($_.LineNumber)"
	}
	throw (
		'Component Tick must start disabled and wake from explicit state: ' +
		($locations -join ', '))
}

$attributesFile = Join-Path $projectRoot '.gitattributes'
$attributes = Get-Content -Raw -LiteralPath $attributesFile
foreach ($extension in @('*.uasset', '*.umap', '*.png', '*.gif', '*.zip', '*.bin')) {
    if ($attributes -notmatch [regex]::Escape("$extension filter=lfs")) {
        throw "$extension must be tracked by Git LFS."
    }
}

$worldSceneSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Core/IGPrologueWorldScene.cpp')
$saveGameHeader = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Save/IGSaveGame.h')
$saveSubsystemSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Save/IGSaveSubsystem.cpp')
$gameplayTagsConfig = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Config/DefaultGameplayTags.ini')
$frontendShippingProbeScriptPath = Join-Path $projectRoot (
	'Scripts/Run-MissingFloor-FrontendShippingProbe.ps1')
$frontendShippingProbeScript = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	$frontendShippingProbeScriptPath)
$performanceContract = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Docs/PERFORMANCE.md')
$horrorHudSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGHorrorHUD.cpp')
$playerControllerSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGPlayerController.cpp')
$playerCharacterSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGPlayerCharacter.cpp')
$flashlightHeader = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGFlashlightComponent.h')
$flashlightSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGFlashlightComponent.cpp')
$stressSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Player/IGStressComponent.cpp')
$pickupItemSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Interaction/IGPickupItem.cpp')
$elevatorSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Interaction/IGElevator.cpp')
$swingDoorSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Interaction/IGSwingDoor.cpp')
$slidingDoorSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Interaction/IGSlidingDoor.cpp')
$neighborhoodSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Environment/IGNeighborhoodLifeDirector.cpp')
$surfaceMaterialSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts/create_textured_materials.py')
$surfaceAuditSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts/validate_baked_art_assets.py')
$artBuildSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts/Build-ArtAssets.ps1')

foreach ($surfaceResponseInvariant in @(
	'SURFACE_RESPONSE_DEFAULTS',
	'"macro_strength":',
	'"detail_normal_strength":',
	'"roughness_variation":',
	'"roughness_detail_strength":',
	'def _texture_exists(assets, name):',
	'SURFACE_RESPONSE_MARKER = "IG_SurfaceResponse_v1"',
	'unreal.MaterialProperty.MP_SPECULAR',
	'IG_SURFACE_RESPONSE_ONLY'
)) {
	if (-not $surfaceMaterialSource.Contains($surfaceResponseInvariant)) {
		throw "Layered surface-response invariant is missing: $surfaceResponseInvariant"
	}
}
foreach ($surfaceAuditInvariant in @(
	# Open paren only. This asserts the audit exists, not what it takes: the
	# closed form went stale the moment a material_specs parameter was added
	# for the targeted apartment build, and the gate then failed on a function
	# that was sitting right there.
	'def validate_surface_response_materials(',
	'Macro colour blend is missing',
	'Detail-normal blend is missing',
	'Roughness variation is missing',
	'pixel_samples <= 7'
)) {
	if (-not $surfaceAuditSource.Contains($surfaceAuditInvariant)) {
		throw "Surface-response UAsset audit is missing: $surfaceAuditInvariant"
	}
}
foreach ($surfaceBuildInvariant in @(
	'[switch]$SurfaceResponseOnly',
	'IG_SURFACE_RESPONSE_ONLY',
	'Surface response material update complete'
)) {
	if (-not $artBuildSource.Contains($surfaceBuildInvariant)) {
		throw "Targeted surface-response build is missing: $surfaceBuildInvariant"
	}
}
foreach ($propResponseInvariant in @(
	'PRINT_RESPONSE_MARKER = "IG_PrintResponse_v1"',
	'OPTICAL_RESPONSE_MARKER = "IG_OpticalResponse_v1"',
	'WET_GROUND_RESPONSE_MARKER = "IG_WetGroundResponse_v1"',
	'def _connect_print_response(material, base_sample, spec):',
	'def create_optical_prop_materials(assets, tools, update_in_place=False):',
	'"micro_stem": "T_PaperClean_V2"',
	'"micro_stem": "T_CarrierBagFilm"',
	'IG_PROP_RESPONSE_ONLY'
)) {
	if (-not $surfaceMaterialSource.Contains($propResponseInvariant)) {
		throw "Prop-response material invariant is missing: $propResponseInvariant"
	}
}
foreach ($propAuditInvariant in @(
	'def validate_prop_response_materials()',
	'Compiled print sample budget exceeded',
	'Compiled optical-prop sample budget exceeded',
	'Compiled wet-ground sample budget exceeded'
)) {
	if (-not $surfaceAuditSource.Contains($propAuditInvariant)) {
		throw "Prop-response UAsset audit is missing: $propAuditInvariant"
	}
}
foreach ($propBuildInvariant in @(
	'[switch]$PropResponseOnly',
	'IG_PROP_RESPONSE_ONLY',
	'Prop response material update complete'
)) {
	if (-not $artBuildSource.Contains($propBuildInvariant)) {
		throw "Targeted prop-response build is missing: $propBuildInvariant"
	}
}
foreach ($surfaceLightingInvariant in @(
	'PostProcess->Settings.LocalExposureDetailStrength = 1.12f;',
	'PostProcess->Settings.FilmSlope = 0.90f;',
	'PostProcess->Settings.AmbientOcclusionIntensity = 0.48f;',
	'PostProcess->Settings.LumenAmbientOcclusionIntensity = 0.55f;',
	'Light->ContactShadowLength = 0.0f;',
	'Light->SetSpecularScale(1.0f);'
)) {
	if (-not $worldSceneSource.Contains($surfaceLightingInvariant)) {
		throw "Surface-lighting response invariant is missing: $surfaceLightingInvariant"
	}
}

foreach ($spatialContinuityInvariant in @(
	'GetIntermediateCabBaseZ',
	'IntermediateDoorPanels',
	'RiderHalfHeight + FloorClearance',
	'bIntermediateStopEnabled ? GetIntermediateCabBaseZ() : -FloorDeltaZ'
)) {
	if (-not $elevatorSource.Contains($spatialContinuityInvariant)) {
		throw "Elevator spatial-continuity invariant is missing: $spatialContinuityInvariant"
	}
}
if ($elevatorSource.Contains(
	'Rider->GetActorLocation() - FVector(0, 0, FloorDeltaZ)')) {
	throw 'Elevator must land from its visible cab floor, not a relative falling offset.'
}
foreach ($worldContinuityInvariant in @(
	'constexpr float SecondFloorZ = 300.0f',
	'FVector(-140, -214.81f, 154)',
	'PropMesh(TEXT("SM_ApartmentCalendar2025"))',
	'BuildStairCore();',
	'BuildLowerFloors();',
	'constexpr float StairRise = StairStoreyHeight / 18.0f;',
	'constexpr float StairGoing = (StairFlightNorthY - StairFlightSouthY) / 8.0f;',
	'FVector(-214.5f, -352, 119), FVector(251, 4, 238), Metal, false',
	'FVector(800, -394, 620), FVector(160, 6, 1240)',
	'FVector(1015, -385, 230), FVector(270, 20, 460)',
	'TexMat(TEXT("M_StainlessUV"), FridgeBodyMaterial)',
	'CabVisuals.DiffuserMaterial = SignWhiteMaterial',
	'TexMat(TEXT("M_SteelDoorUV"), FridgeBodyMaterial)'
)) {
	if (-not $worldSceneSource.Contains($worldContinuityInvariant)) {
		throw "World spatial-continuity invariant is missing: $worldContinuityInvariant"
	}
}
if ($engineConfig -match '(?m)^r\.VolumetricFog\s*=') {
	throw (
		'r.VolumetricFog must remain scalability-owned; a project-level value ' +
		'prevents Low ShadowQuality from disabling its 3D volume.')
}
if ($worldSceneSource.Contains('FVector(-187.2f, -60, 152)')) {
	throw 'The calendar must not regress behind the wardrobe and bedside table.'
}
foreach ($elevatorLightingInvariant in @(
	'CabLight->SetIntensity(520.0f)',
	'CabLight->SetAttenuationRadius(300.0f)',
	'CabLight->SetLightColor(FLinearColor(0.84f, 0.91f, 1.0f))',
	'FloorFill->SetIntensity(36.0f)'
)) {
	if (-not $elevatorSource.Contains($elevatorLightingInvariant)) {
		throw "Elevator anti-clipping lighting invariant is missing: $elevatorLightingInvariant"
	}
}

foreach ($purchaseProfileContract in @(
	@{
		Profile = 'ProfileA500MlX2'
		Product = '새벽샘물 500mL'
		Quantity = 2
		UnitPrice = 1000
	},
	@{
		Profile = 'ProfileB1LX1'
		Product = '한강수 1L'
		Quantity = 1
		UnitPrice = 1500
	},
	@{
		Profile = 'ProfileC2LX2'
		Product = '맑은산 2L'
		Quantity = 2
		UnitPrice = 2000
	}
)) {
	$profilePattern = '(?s)case EIGPurchaseProfile::' +
		[regex]::Escape($purchaseProfileContract.Profile) +
		':.*?' + [regex]::Escape($purchaseProfileContract.Product) +
		'.*?Spec\.Quantity\s*=\s*' + $purchaseProfileContract.Quantity +
		';.*?Spec\.UnitPrice\s*=\s*' + $purchaseProfileContract.UnitPrice + ';'
	if ($worldSceneSource -notmatch $profilePattern) {
		throw "Purchase profile price contract is incomplete: $($purchaseProfileContract.Profile)"
	}
}

foreach ($requiredBagContract in @(
	'Store.PurchaseBagProxy',
	'AddStaticPurchaseBagProxy',
	'PurchaseProfileOnPickup == PurchaseProfile',
	'Component->SetVisibility(bShowBag, true)'
)) {
	if (-not $worldSceneSource.Contains($requiredBagContract)) {
		throw "Purchase-bag presentation contract is missing: $requiredBagContract"
	}
}

if (-not $worldSceneSource.Contains(
	'TArray<FIGDoorRequirement> NoDoorRequirements;')) {
	throw 'The 403 front door must not lock behind optional investigation.'
}

foreach ($selectionInvariant in @(
	'State.CH01.Morning.LeftApartment',
	'ReturnForProfileSwap',
	'ReleaseCarriedActor(this)',
	'HandlePurchaseSelectionChanged',
	'bHeavyBagInteractionProxyActive',
	'ProfileC2LX2',
	'Narrative->SetStorePurchaseProfile(PurchaseProfileOnPickup)',
	'Narrative->GetStorePurchaseProfile()'
)) {
	if (-not $gameplayTagsConfig.Contains($selectionInvariant) -and
		-not $pickupItemSource.Contains($selectionInvariant) -and
		-not $worldSceneSource.Contains($selectionInvariant) -and
		-not $playerCharacterSource.Contains($selectionInvariant)) {
		throw "Store water selection/static-proxy invariant is missing: $selectionInvariant"
	}
}
# 건전지는 닳지만 빛이 꺼지지는 않는다. 바닥값이 없어지거나 너무 낮아지면 다 닳은
# 손전등이 진행을 막는다.
$emptyCellFloor = [regex]::Match($flashlightSource, 'constexpr float EmptyCellFloor = ([0-9.]+)f;')
if (-not $emptyCellFloor.Success -or
	[double]::Parse($emptyCellFloor.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) -lt 0.12 -or
	-not $flashlightSource.Contains('FMath::Lerp(IGFlashlight::EmptyCellFloor, 1.0f, Knee)') -or
	-not $flashlightSource.Contains('presentation-only and always recover')) {
	throw 'A drained flashlight must keep EmptyCellFloor (>= 0.12) of its beam, and brown-outs must always recover.'
}
foreach ($requiredIdleTickInvariant in @(
	'PrimaryComponentTick.bStartWithTickEnabled = false',
	'SetComponentTickEnabled(true)',
	'SetComponentTickEnabled(false)'
)) {
	if (-not $flashlightSource.Contains($requiredIdleTickInvariant)) {
		throw "Flashlight idle-Tick invariant is missing: $requiredIdleTickInvariant"
	}
}
foreach ($requiredStressTickInvariant in @(
	'PrimaryComponentTick.bStartWithTickEnabled = false',
	'void UIGStressComponent::RefreshTickState()',
	'HeartbeatSuppressionRemaining > KINDA_SMALL_NUMBER',
	'SetComponentTickEnabled(bNeedsTick)'
)) {
	if (-not $stressSource.Contains($requiredStressTickInvariant)) {
		throw "Stress idle-Tick invariant is missing: $requiredStressTickInvariant"
	}
}
foreach ($requiredStorePerformanceInvariant in @(
	'Components/InstancedStaticMeshComponent.h',
	'ExpectedStoreStockInstances = 1318',
	'TEXT("SM_RetailCupBeef")',
	'TEXT("M_RetailPriceCupBeef")',
	'MaximumStoreStockBatches = 28',
	'StoreStockCullStartCentimeters = 1600',
	'StoreStockCullEndCentimeters = 2200',
	'Batch->SetAffectDistanceFieldLighting(false)',
	'Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision)',
	'FinalizeStoreStockBatches();'
)) {
	if (-not $worldSceneSource.Contains($requiredStorePerformanceInvariant)) {
		throw "Store runtime-performance invariant is missing: $requiredStorePerformanceInvariant"
	}
}

if ($worldSceneSource -match 'BindLambda\(\s*\[this(?:,|\])') {
	throw 'World-scene timer delegates must use weak UObject captures instead of raw this.'
}
$frontendShippingProbeTokens = $null
$frontendShippingProbeParseErrors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
	$frontendShippingProbeScriptPath,
	[ref]$frontendShippingProbeTokens,
	[ref]$frontendShippingProbeParseErrors)
if ($frontendShippingProbeParseErrors.Count -gt 0) {
	$parseMessages = $frontendShippingProbeParseErrors |
		ForEach-Object { $_.Message }
	throw "Frontend Shipping probe script does not parse: $($parseMessages -join '; ')"
}
foreach ($requiredFrontendShippingHarnessInvariant in @(
	"'-IGFrontendShippingProbe'",
	'"-IGFrontendExpectedWidth=$Width"',
	'"-IGFrontendExpectedHeight=$Height"',
	'"-IGFrontendResultPath=$receiptPath"',
	'"-IGFrontendAccessibilityScreenshotPath=$accessibilityScreenshotPath"',
	'"-IGFrontendDisplayScreenshotPath=$displayScreenshotPath"',
	'"-IGFrontendTitleScreenshotPath=$titleScreenshotPath"',
	'"-IGFrontendDefaultScreenshotPath=$defaultScreenshotPath"',
	'"-IGFrontendScreenshotPath=$screenshotPath"',
	"'-RenderOffscreen'",
	"'-d3d12'",
	'-WindowStyle Hidden',
	'-FilePath $launcher',
	'-WorkingDirectory (Split-Path -Parent $launcher)',
	'1280; height = 720',
	'1600; height = 900',
	'1920; height = 1080',
	'2560; height = 1440',
	'Assert-ArchiveUnchanged',
	'Get-PngDimensions',
	'dialogueScreenshotSha256',
	'accessibilityScreenshotSha256',
	'displayScreenshotSha256',
	'titleScreenshotSha256',
	'defaultDialogueScreenshotSha256',
	'keyboard_access=1 gamepad_access=1 dpad_down=1',
	'gamepad_pause=1 display=1 title=1 first_run=1',
	'dialogue=1 dialogue_default=1',
	'speaker=1 continuation=1 default_scale=100 max_scale=200',
	'sound_lane=1 samples=11 elements_min=',
	'input_events=11 bounds=',
	'MISSINGFLOOR_FRONTEND_SHIPPING PASS resolutions=4 input_events=44',
	'layout_samples=44 dialogue_cases=8 title_cases=4',
	'schemaVersion = 4',
	'titleCaseCount = $results.Count',
	'archiveUnchanged = $true',
	'layoutSampleCount'
)) {
	if (-not $frontendShippingProbeScript.Contains(
		$requiredFrontendShippingHarnessInvariant)) {
		throw (
			'Frontend Shipping harness invariant is missing: ' +
			$requiredFrontendShippingHarnessInvariant)
	}
}
foreach ($requiredPerformanceInvariant in @(
	'문서 버전: `Windows-v1`',
	'G3·G5를',
	'G6 성능 항목',
	'Intel Core i5-8400 또는 AMD Ryzen 5 2600',
	'Intel Core i5-12400 또는 AMD Ryzen 5 5600',
	'Windows 11 Home/Pro 25H2',
	'26H1을 포함한 다른 기능 업데이트',
	'1280×720',
	'1920×1080',
	'2560×1440',
	'3840×2160',
	'`r.ScreenPercentage=100`',
	'프레임 시간 `p95`',
	'`1% low`',
	'peak committed 12.0GB 이하',
	'peak 5.5GB 이하',
	'설치된 Shipping 배포물의 총 파일 크기는 8.0GB 이하',
	'`MIN-W10-NV`',
	'`MIN-W10-AMD`',
	'`MIN-W11-NV`',
	'`MIN-W11-AMD`',
	'`REC-W11-NV`',
	'`REC-W11-AMD`',
	'여섯 필수 장비',
	'기준일: `2026-08-06`',
	'`UInstancedStaticMeshComponent` 23개 배치(상한 24개)',
	'`instances=1301`',
	'Component Tick은 기본 활성 상태로 시작할 수 없으며',
	'현재 월드는 `BeginPlay`에서 절차적으로 조립되므로',
	'`stat PSOPrecache`',
	'판정은 **BLOCKED**'
)) {
	if (-not $performanceContract.Contains($requiredPerformanceInvariant)) {
		throw "Performance release contract is missing: $requiredPerformanceInvariant"
	}
}
# 없는 층의 진실·밤 상태·고른 물은 서사 스냅샷 하나에 실린다. 저장하고,
# 불러올 때는 스토리 태그보다 먼저 되돌려야 태그 콜백이 새 상태를 본다.
if (-not $saveGameHeader.Contains('FIGMissingFloorNarrativeSnapshot MissingFloorNarrative') -or
	-not $saveSubsystemSource.Contains('MissingFloorState->GetSnapshot()') -or
	-not $saveSubsystemSource.Contains('MissingFloorState->RestoreSnapshot(')) {
	throw 'The save must persist and restore the Missing Floor narrative snapshot.'
}
$restoreNarrativeIndex = $saveSubsystemSource.IndexOf(
	'MissingFloorState->RestoreSnapshot(')
$restoreStoryTagsIndex = $saveSubsystemSource.IndexOf(
	'StoryState->RestoreStateSnapshot(')
if ($restoreNarrativeIndex -lt 0 -or
	$restoreStoryTagsIndex -le $restoreNarrativeIndex) {
	throw 'The narrative snapshot must restore before story-tag callbacks.'
}
if (-not $worldSceneSource.Contains(
	'CreateBlock(FVector(200, -245.6f, 115), FVector(20, 0.8f, 230), WallX, false)')) {
	throw 'The apartment east-wall return must keep its correctly oriented material cap.'
}

if (-not $horrorHudSource.Contains(
	'return Provider ? Provider->GetObjectiveText() : FText::GetEmpty();')) {
	throw 'The HUD objective must fall back to its provider when no wake director exists.'
}

foreach ($requiredFrontendProbeControllerInvariant in @(
	'IGFrontendShippingProbe',
	'void AIGPlayerController::TickFrontendShippingProbe()',
	'FInputKeyEventArgs Pressed(',
	'FInputKeyEventArgs Released(',
	'!Params.IsSimulatedInput()',
	'EKeys::F10',
	'EKeys::Gamepad_DPad_Down',
	'EKeys::Gamepad_Special_Right',
	'EKeys::Gamepad_FaceButton_Bottom',
	'EKeys::Gamepad_FaceButton_Right',
	'EKeys::Gamepad_Special_Left',
	'TryCaptureFrontendProbeLayout(TEXT("display_gamepad"), 11)',
	'TEXT("dialogue_default_scale"),',
	'TEXT("dialogue_max_scale"),',
	'GetDialogueRenderSample(',
	'IGFrontendDefaultScreenshotPath=',
	'IGFrontendAccessibilityScreenshotPath=',
	'IGFrontendDisplayScreenshotPath=',
	'IGFrontendTitleScreenshotPath=',
	'IGFrontendScreenshotPath=',
	'bFrontendProbeCompilationDrained',
	'FAssetCompilingManager::Get().FinishAllCompilation()',
	'GShaderCompilingManager->FinishAllCompilation()',
	'FScreenshotRequest::RequestScreenshot(',
	'Settings.CaptionSizeScale = 2.0f',
	'FrontendProbeLayoutSampleCount != 11',
	'FrontendProbePressedEventCount != 11',
	'MISSINGFLOOR_FRONTEND PASS contract=4 resolution=%dx%d',
	'FPlatformMisc::RequestExitWithStatus'
)) {
	if (-not $playerControllerSource.Contains(
		$requiredFrontendProbeControllerInvariant)) {
		throw (
			'Frontend Shipping input-path invariant is missing: ' +
			$requiredFrontendProbeControllerInvariant)
	}
}
foreach ($requiredHudLayoutProbeInvariant in @(
	'bLayoutValidationEnabled = FParse::Param(',
	'bool AIGHorrorHUD::GetLayoutValidationSample(',
	'void AIGHorrorHUD::BeginLayoutValidationSample()',
	'void AIGHorrorHUD::RecordLayoutValidationRect(',
	'void AIGHorrorHUD::FinalizeLayoutValidationSample()',
	'if (bLayoutValidationEnabled)',
	'Canvas->StrLen(Font, Text.ToString(), TextWidth, TextHeight, true)',
	'bLayoutValidationAllInsideCanvas',
	'PixelTolerance = 1.5f'
)) {
	if (-not $horrorHudSource.Contains($requiredHudLayoutProbeInvariant)) {
		throw (
			'Frontend HUD layout-probe invariant is missing: ' +
			$requiredHudLayoutProbeInvariant)
	}
}
$layoutMeasurementIndex = $horrorHudSource.IndexOf(
	'Canvas->StrLen(Font, Text.ToString(), TextWidth, TextHeight, true)')
# 글자 겹침 검사(-IGTextAudit)도 같은 자리에서 잰다. 둘 다 명령줄로만 켜진다.
$layoutGateIndex = $horrorHudSource.LastIndexOf(
	'if (bLayoutValidationEnabled || bTextAuditEnabled)',
	$layoutMeasurementIndex)
if ($layoutMeasurementIndex -lt 0 -or
	$layoutGateIndex -lt 0 -or
	$layoutMeasurementIndex - $layoutGateIndex -gt 140 -or
	-not $horrorHudSource.Contains('bTextAuditEnabled = FParse::Param(')) {
	throw 'HUD layout measurement must remain gated out of normal gameplay.'
}

if (-not $playerCharacterSource.Contains(
	'InitCapsuleSize(30.0f, 96.0f)')) {
	throw 'First-person capsule must preserve clearance through the 84-88 cm interior doors.'
}

if (-not $worldSceneSource.Contains(
	'CreateBlock(FVector(131, -225, 220), FVector(110, 20, 20), WallX)')) {
	throw 'The apartment entrance must retain 210 cm clearance above the shoe step.'
}
if ($worldSceneSource.Contains(
	'CreateBlock(FVector(2405, -430, 12), FVector(12, 470, 24), Metal)')) {
	throw 'The convenience-store kick rail must not cross the automatic-door threshold.'
}
foreach ($doorSafetySource in @($swingDoorSource, $slidingDoorSource)) {
	if (-not $doorSafetySource.Contains(
		'UCollisionProfile::NoCollision_ProfileName')) {
		throw 'Moving door leaves must release blocking collision while opening.'
	}
}

# 골목에는 배달 오토바이만 다닌다. 몸을 통과하지 않고(ResolvePlayerClearance), 비킬 폭이
# 없으면 서며, 첫 외출의 루이턴 버스(LaunchAlleyNearMiss)는 한 판에 한 번이다.
foreach ($requiredNeighborhoodFeature in @(
	'PrimeOutdoorSequence',
	'bOutdoorSequencePrimed',
	'LaunchAlleyNearMiss',
	'ResolvePlayerClearance',
	'ComputeLaneLimits',
	'Neighborhood.ScooterNearMiss',
	'ActivateLeaves',
	'CatTraceRoot'
)) {
	if (-not $neighborhoodSource.Contains($requiredNeighborhoodFeature)) {
		throw "Required neighborhood-life feature is missing: $requiredNeighborhoodFeature"
	}
}
foreach ($forbiddenNeighborhoodFeature in @('PassengerCar', 'VehicleCabin')) {
	if ($neighborhoodSource.Contains($forbiddenNeighborhoodFeature)) {
		throw "폭 2.6 m 골목에 승용차를 다시 넣지 마세요: $forbiddenNeighborhoodFeature"
	}
}

$audioContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-AudioContract.ps1'
& $audioContractScript

$accessibilityContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-AccessibilityContract.ps1'
& $accessibilityContractScript

$dialogueContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-DialogueContract.ps1'
& $dialogueContractScript

$frontendContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-FrontendContract.ps1'
& $frontendContractScript

# System.Drawing.Common은 .NET 6부터 Windows에서만 돈다. 이 검사들은 맑은 고딕 글자 폭과
# 그림 크기를 재는 것이라 다른 운영체제에서는 잴 대상 자체가 없다. 건너뛴 사실은 남긴다.
function Invoke-WindowsOnlyContract([string]$ContractScript) {
	if ($IsLinux -or $IsMacOS) {
		Write-Host "SKIP $(Split-Path -Leaf $ContractScript) — System.Drawing은 Windows 전용"
		return
	}
	& $ContractScript
}

$artAssetContractScript = Join-Path $projectRoot `
	'Scripts/Test-ArtAssetContract.ps1'
Invoke-WindowsOnlyContract $artAssetContractScript

$missingFloorM0InputContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-M0InputContract.ps1'
& $missingFloorM0InputContractScript

$missingFloorProductionEntryContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-ProductionEntryContract.ps1'
& $missingFloorProductionEntryContractScript

$missingFloorM1CaptureContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-M1CaptureContract.ps1'
& $missingFloorM1CaptureContractScript

$missingFloorM1WakeEchoContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-M1WakeEchoContract.ps1'
& $missingFloorM1WakeEchoContractScript

$missingFloorM5RevealContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-M5RevealContract.ps1'
& $missingFloorM5RevealContractScript

$missingFloorReleaseEndingContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-ReleaseEndingContract.ps1'
& $missingFloorReleaseEndingContractScript

$missingFloorReleaseGateContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-ReleaseGateContract.ps1'
& $missingFloorReleaseGateContractScript

$missingFloorSignatureSfxContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-SignatureSfxContract.ps1'
& $missingFloorSignatureSfxContractScript

$missingFloorTuningTableContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-TuningTableContract.ps1'
& $missingFloorTuningTableContractScript

$missingFloorMixMovementContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-MixAndMovementContract.ps1'
& $missingFloorMixMovementContractScript

$missingFloorInputBindingContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-InputBindingContract.ps1'
& $missingFloorInputBindingContractScript

$missingFloorBibleContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-BibleContract.ps1'
& $missingFloorBibleContractScript

$missingFloorM6AudioVisualContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-M6AudioVisualContract.ps1'
& $missingFloorM6AudioVisualContractScript

$missingFloorM65MercyNoteContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-M65MercyNoteContract.ps1'
Invoke-WindowsOnlyContract $missingFloorM65MercyNoteContractScript

# §20 난이도 네 모드와 자비 안전망. 파일은 있었는데 아무도 부르지
# 않아서 108개 단언이 그냥 안 돌고 있었다.
$missingFloorM8DifficultyContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-M8DifficultyContract.ps1'
& $missingFloorM8DifficultyContractScript

$missingFloorM65AudioCalibrationContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-M65AudioCalibrationContract.ps1'
Invoke-WindowsOnlyContract $missingFloorM65AudioCalibrationContractScript

$missingFloorM3CctvChannelContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-M3CctvChannelContract.ps1'
& $missingFloorM3CctvChannelContractScript

$missingFloorM3DoorBeatContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-M3DoorBeatContract.ps1'
& $missingFloorM3DoorBeatContractScript

$missingFloorM4PassByContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-M4PassByContract.ps1'
& $missingFloorM4PassByContractScript

$missingFloorNightFiveSlotContractScript = Join-Path $projectRoot `
	'Scripts/Test-MissingFloor-NightFiveSlotContract.ps1'
& $missingFloorNightFiveSlotContractScript

# Physical plausibility of the code-authored world, and the offline half of
# the art contracts. Both run without Unreal, so they gate every commit rather
# than waiting for an editor pass.
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
	$python = Get-Command python3 -ErrorAction SilentlyContinue
}
if ($python) {
	$geometryAudit = Join-Path $projectRoot 'Scripts/audit_world_geometry.py'
	# 이 스캐너는 감사 여섯 개가 같이 쓴다. 람다 인자와 회전이 안 풀리면
	# 상자가 대각선 길이짜리 정육면체로 부풀어 조용히 거짓 양성을 쏟는다.
	& $python.Source $geometryAudit --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "World geometry scanner self-test failed ($LASTEXITCODE)"
	}

	$worldGeometryOutput = & $python.Source $geometryAudit --check
	$worldGeometryOutput | ForEach-Object { Write-Host $_ }
	if ($LASTEXITCODE -ne 0) {
		throw "World geometry audit found impossible placements ($LASTEXITCODE)"
	}
	Assert-AuditBlindSpot $worldGeometryOutput '자리를 풀지 못한 상자 (?<count>\d+)건' 9 `
		'좌표가 트랜스폼 지역 변수나 포인터 삼항에 걸려 자리를 풀지 못한 상자'

	$atlasPacker = Join-Path $projectRoot 'Scripts/build_texture_atlas.py'
	& $python.Source $atlasPacker --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Texture atlas packer self-test failed ($LASTEXITCODE)"
	}

	# What the packaged build will and will not contain. The cooker does not
	# read C++, so an asset only a LoadObject path names is absent from the pak
	# and null at runtime -- in the packaged build alone, which is the build
	# nobody runs while iterating. This catches that here instead.
	#
	# The self-test runs first because the audit itself skips on a checkout
	# that did not fetch LFS, and a gate that can skip needs something that
	# cannot.
	$cookReferences = Join-Path $projectRoot 'Scripts/check_cook_references.py'
	& $python.Source $cookReferences --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Cook reference audit self-test failed ($LASTEXITCODE)"
	}

	& $python.Source $cookReferences --check
	if ($LASTEXITCODE -ne 0) {
		throw "Cook reference audit found unreachable assets ($LASTEXITCODE)"
	}

	# The atlas only saves anything if the textures it replaced stop being
	# cooked. That cannot be observed until the editor rebuilds the print
	# materials, so this moves the one edge per material the rebuild moves and
	# reports the whole delta -- including what would break.
	& $python.Source $cookReferences --simulate-rebuild
	if ($LASTEXITCODE -ne 0) {
		throw "The atlas rebuild would not drop the textures it replaces ($LASTEXITCODE)"
	}

	# The full pass recreates its materials; the targeted IG_*_ONLY passes
	# update them in place and leave the pre-atlas sampler behind. This runs
	# each of those over the pre-atlas graph both ways, so the pass list stays
	# honest and the exposure stays visible.
	& $python.Source $cookReferences --simulate-targeted
	if ($LASTEXITCODE -ne 0) {
		throw "A targeted material pass would keep its atlassed textures ($LASTEXITCODE)"
	}

	# 쿠크에 들어 있어도 씬이 이름을 못 부르면 화면에는 없는 것과 같다.
	# LoadTexturedMaterials()의 배열이 그 관문인데, 이름 한 줄이 빠져도
	# TexMat은 조용히 폴백을 돌려주므로 컴파일도 쿠크도 통과한다.
	$sceneMaterials = Join-Path $projectRoot 'Scripts/audit_scene_materials.py'
	& $python.Source $sceneMaterials --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Scene material audit self-test failed ($LASTEXITCODE)"
	}

	& $python.Source $sceneMaterials --check
	if ($LASTEXITCODE -ne 0) {
		throw "A material the scene asks for never reaches it ($LASTEXITCODE)"
	}

	# 씬이 CreateBlock으로 짓는 것은 위 기하 감사가 좌표까지 읽는다. 밤과
	# 퍼즐의 소품은 디렉터가 SpawnActor 뒤에 Configure로 붙이므로 그 경로에
	# 있었고, 크기 인자의 뜻이 함수마다 달라서 조용히 1m 정육면체가 되거나
	# 재질 없이 엔진 기본 격자로 그려지고 있었다.
	$directorProps = Join-Path $projectRoot 'Scripts/audit_director_props.py'
	& $python.Source $directorProps --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Director prop audit self-test failed ($LASTEXITCODE)"
	}

	$directorPropsOutput = & $python.Source $directorProps --check
	$directorPropsOutput | ForEach-Object { Write-Host $_ }
	if ($LASTEXITCODE -ne 0) {
		throw "A director-spawned prop is not configured to contract ($LASTEXITCODE)"
	}
	Assert-AuditBlindSpot $directorPropsOutput '자리를 풀지 못한 소품 (?<count>\d+)건' 0 `
		'크기나 좌표가 리터럴이 아니라 자리를 풀지 못한 소품'
	Assert-AuditBlindSpot $directorPropsOutput '대조하지 못한 호출부 (?<count>\d+)건' 0 `
		'SpawnActor를 같은 파일에서 찾지 못해 대조 못 한 호출부'

	# 건축 재질은 월드 좌표를 읽으므로 축이 맞는 면에서만 무늬가 변한다.
	# 이름도 자리도 크기도 맞는데 면의 방향 하나가 어긋나면 그 면 전체가
	# 한 줄로 늘어나고, 위 감사 넷은 그것을 보지 않는다.
	$surfaceProjection = Join-Path $projectRoot 'Scripts/audit_surface_projection.py'
	& $python.Source $surfaceProjection --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Surface projection audit self-test failed ($LASTEXITCODE)"
	}

	$surfaceProjectionOutput = & $python.Source $surfaceProjection --check
	$surfaceProjectionOutput | ForEach-Object { Write-Host $_ }
	if ($LASTEXITCODE -ne 0) {
		throw "A world-projected material is stretched across the face it is on ($LASTEXITCODE)"
	}
	Assert-AuditBlindSpot $surfaceProjectionOutput 'unresolved=(?<count>\d+)' 5 `
		'호출부가 리터럴도 지역 변수도 아니라 재질을 풀지 못한 상자'

	# 간판과 명판은 메시 UV를 읽는데 엔진 기본 큐브는 여섯 면이 그 UV를
	# 나눠 쓴다. 두께가 있는 몸통에 인쇄를 통째로 주면 옆면에도 같은 그림이
	# 눌려 한 번 더 찍힌다.
	$printedFaces = Join-Path $projectRoot 'Scripts/audit_printed_faces.py'
	& $python.Source $printedFaces --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Printed face audit self-test failed ($LASTEXITCODE)"
	}

	$printedFacesOutput = & $python.Source $printedFaces --check
	$printedFacesOutput | ForEach-Object { Write-Host $_ }
	if ($LASTEXITCODE -ne 0) {
		throw "A printed material is wrapped around a whole body instead of its face ($LASTEXITCODE)"
	}
	Assert-AuditBlindSpot $printedFacesOutput 'unresolved=(?<count>\d+)' 5 `
		'인쇄 재질을 풀지 못한 상자'

	# 발소리 표면 태그는 소리만 정하는 게 아니라 반향 공간까지 고른다.
	# 옥상 슬래브 하나가 태그를 빼먹으면 탁 트인 옥상이 복도로 울린다.
	$footstepSurfaces = Join-Path $projectRoot 'Scripts/audit_footstep_surfaces.py'
	& $python.Source $footstepSurfaces --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Footstep surface audit self-test failed ($LASTEXITCODE)"
	}

	& $python.Source $footstepSurfaces --check
	if ($LASTEXITCODE -ne 0) {
		throw "A walkable surface is missing its footstep tag ($LASTEXITCODE)"
	}

	# §18.7은 홀드 완료 편차를 ±3%로 걸어 두었다. 눈으로 읽어서는 지킬 수
	# 없는 줄이라 실제로 돌려 본다 — 짧은 홀드를 새로 적어 넣으면 여기서
	# 걸린다.
	$holdTiming = Join-Path $projectRoot 'Scripts/audit_hold_timing.py'
	& $python.Source $holdTiming --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Hold timing audit self-test failed ($LASTEXITCODE)"
	}

	& $python.Source $holdTiming --check
	if ($LASTEXITCODE -ne 0) {
		throw "A hold completes outside the §18.7 window ($LASTEXITCODE)"
	}

	# 같은 사실을 두 파일이 각자 적어 두는 것. 냉장고 험, 4층 슬래브 높이,
	# 로비 모니터 치수가 차례로 그랬다. 셋 다 컴파일도 되고 화면도 뜬다.
	$duplicateConstants = Join-Path $projectRoot 'Scripts/audit_duplicate_constants.py'
	& $python.Source $duplicateConstants --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Duplicate constant audit self-test failed ($LASTEXITCODE)"
	}

	& $python.Source $duplicateConstants --check
	if ($LASTEXITCODE -ne 0) {
		throw "An authored constant is written in two places ($LASTEXITCODE)"
	}

	# 끊어진 접근성 설정은 화면으로 안 보인다. 메뉴에 뜨고 켜지고 저장되고,
	# 다시 켜면 켜져 있다. 바뀌는 게 없다는 것만 다르다.
	$accessibilityReach =
		Join-Path $projectRoot 'Scripts/audit_accessibility_reach.py'
	& $python.Source $accessibilityReach --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Accessibility reach audit self-test failed ($LASTEXITCODE)"
	}

	$accessibilityReachOutput = & $python.Source $accessibilityReach --check
	$accessibilityReachOutput | ForEach-Object { Write-Host $_ }
	if ($LASTEXITCODE -ne 0) {
		throw "An accessibility setting changes nothing a player can feel ($LASTEXITCODE)"
	}

	# 아무도 안 부르는 접근자. 설정 자체는 다른 경로로 살아 있어서 고장은
	# 아니지만, 이 수가 늘면 쓰지도 않는 문을 계속 세우고 있다는 뜻이다.
	Assert-AuditBlindSpot $accessibilityReachOutput `
		'아무도 안 부르는 접근자 (?<count>\d+)개' 0 `
		'설정을 읽지만 아무도 부르지 않는 접근자'

	# 합성기는 음을 그냥 더하고 ±1.0에서 자른다. 겹친 음의 합이 1을 넘으면
	# 파형이 int16으로 굳기 전에 깎이고, 그 뒤로는 버스를 줄이든 감쇠를 걸든
	# 되돌릴 방법이 없다.
	$toneHeadroom = Join-Path $projectRoot 'Scripts/audit_tone_headroom.py'
	& $python.Source $toneHeadroom --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Tone headroom audit self-test failed ($LASTEXITCODE)"
	}

	$toneHeadroomOutput = & $python.Source $toneHeadroom --check
	$toneHeadroomOutput | ForEach-Object { Write-Host $_ }
	if ($LASTEXITCODE -ne 0) {
		throw "A generated waveform clips before it reaches the mix ($LASTEXITCODE)"
	}

	# 반복문 안에서 음을 만들거나 진폭이 실행 중에 정해지는 생성기는 못 읽는다.
	# 못 읽은 것을 통과로 세지 않으니, 이 수가 늘면 판정 못 하는 파형이 늘어난
	# 것이다.
	Assert-AuditBlindSpot $toneHeadroomOutput `
		'판정을 못 하는 생성기 (?<count>\d+)개' 28 `
		'음을 다 못 읽어서 깎임 여부를 판정 못 한 생성기'

	# 설계값을 맨 숫자로 찾는 계약. 3300줄 문서에서 0.6은 열일곱 번 나오므로
	# 그런 줄은 절이 통째로 사라져도 통과한다.
	$weakNeedles = Join-Path $projectRoot 'Scripts/audit_weak_needles.py'
	& $python.Source $weakNeedles --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Weak needle audit self-test failed ($LASTEXITCODE)"
	}

	& $python.Source $weakNeedles --check
	if ($LASTEXITCODE -ne 0) {
		throw "A contract pins a design value with a bare number ($LASTEXITCODE)"
	}

	# 광원도 가구와 같은 리터럴 좌표로 놓는다. 옆 가구가 자라면 그 안으로
	# 들어가는데, 방이 어두워질 뿐 아무것도 실패하지 않는다.
	$lightPlacement = Join-Path $projectRoot 'Scripts/audit_light_placement.py'
	& $python.Source $lightPlacement --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Light placement audit self-test failed ($LASTEXITCODE)"
	}

	& $python.Source $lightPlacement --check
	if ($LASTEXITCODE -ne 0) {
		throw "A light source is sealed inside solid geometry ($LASTEXITCODE)"
	}

	# 스캔 소품의 맞춤 상자는 메시의 로컬 축에 먹는다. 상자를 월드 기준으로
	# 적으면 요각에서 가로세로가 뒤집히고, 아무것도 실패하지 않은 채 침대가
	# 벽 안으로 들어간다. 원본 glTF 바운드로 최종 크기를 직접 계산한다.
	$photoPropFit = Join-Path $projectRoot 'Scripts/audit_photo_prop_fit.py'
	& $python.Source $photoPropFit --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Photo prop fit audit self-test failed ($LASTEXITCODE)"
	}

	$photoPropOutput = & $python.Source $photoPropFit --check
	$photoPropOutput | ForEach-Object { Write-Host $_ }
	if ($LASTEXITCODE -ne 0) {
		throw "A scanned prop lands inside the structure it stands against ($LASTEXITCODE)"
	}
	Assert-AuditBlindSpot $photoPropOutput '건너뛴 원본 (?<count>\d+)종' 1 `
		'메시가 여럿이라 크기를 못 재는 사진 원본'

	# 나중에 세운 상자가 이미 있던 상자의 면과 소수점까지 같은 평면에 놓이면
	# 깊이 버퍼가 둘을 갈라내지 못한다. 파고든 깊이가 0이라 기하 감사도
	# 못 보고, 화면에서는 카메라가 움직일 때마다 두 재질이 번갈아 이긴다.
	$coplanarSurfaces = Join-Path $projectRoot 'Scripts/audit_coplanar_surfaces.py'
	& $python.Source $coplanarSurfaces --self-test
	if ($LASTEXITCODE -ne 0) {
		throw "Coplanar surface audit self-test failed ($LASTEXITCODE)"
	}

	& $python.Source $coplanarSurfaces --check
	if ($LASTEXITCODE -ne 0) {
		throw "Two drawn surfaces share a plane and will fight for depth ($LASTEXITCODE)"
	}

	# 검은 금속이 모두 오류인 것은 아니다. 색 손실 후보를 추려 사람이 볼 목록을 남긴다.
	$metalBaseColor = Join-Path $projectRoot 'Scripts/audit_metal_base_color.py'
	$metalBaseColorReport = Join-Path $projectRoot 'Saved/MetalBaseColorAudit.json'
	& $python.Source $metalBaseColor --output $metalBaseColorReport
	if ($LASTEXITCODE -ne 0) {
		throw "금속 색 손실 후보 검사 실패 ($LASTEXITCODE)"
	}
	# Windows PowerShell 5.1의 ConvertFrom-Json은 배열을 펼치지 않고 한 덩어리로
	# 넘긴다. 빈 목록 []도 한 개로 세어져 없는 후보를 경고하므로 한 번 펼친다.
	$metalBaseColorCandidates = @((Get-Content -Raw -Encoding UTF8 -LiteralPath $metalBaseColorReport | ConvertFrom-Json) | ForEach-Object { $_ })
	if ($metalBaseColorCandidates.Count -gt 0) {
		Write-Warning ("금속 색 손실 후보 {0}개를 화면에서 확인해야 한다: {1}" -f $metalBaseColorCandidates.Count, $metalBaseColorReport)
	}
} else {
	Write-Warning 'python not found; skipped the geometry audit, atlas self-test, cook reference audit, scene material audit, director prop audit, surface projection audit, printed face audit, footstep surface audit, light placement audit, photo prop fit audit and coplanar surface audit.'
}

# Test-WindowsPackageManifest.ps1은 배포 경로가 필요하므로 임시 패키지로
# 정상 결과와 손상·누락 차단을 실행한다. 실제 배포물은 Package-Windows.ps1이 검사한다.
& (Join-Path $PSScriptRoot 'Test-WindowsPackageManifestRegression.ps1')

if ($python) {
    & $python.Source (Join-Path $PSScriptRoot 'test_windows_telemetry.py')
    if ($LASTEXITCODE -ne 0) { throw 'Windows 화면 출력·GPU 메모리 계측 검사 실패' }
}

Write-Host 'Project structure validation passed (this is not an Unreal build).' -ForegroundColor Green
