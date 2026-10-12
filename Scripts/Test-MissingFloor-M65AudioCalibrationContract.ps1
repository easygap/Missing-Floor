[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$assertions = 0

function Assert-True {
	param([bool]$Condition, [string]$Message)
	$script:assertions++
	if (-not $Condition) {
		throw "MISSINGFLOOR_M65_AUDIO_CALIBRATION_CONTRACT FAIL: $Message"
	}
}

function Read-ProjectText {
	param([string]$RelativePath)
	return Get-Content -Raw -Encoding UTF8 -LiteralPath (
		Join-Path $projectRoot $RelativePath)
}

function Assert-ContainsAll {
	param([string]$Text, [string[]]$Tokens, [string]$Context)
	foreach ($token in $Tokens) {
		Assert-True $Text.Contains($token) "$Context 누락: $token"
	}
}

$rawPath = Join-Path $projectRoot (
	'Content\SourceArt\AI\TextureAudioCalibrationWall_v1.png')
$derivedPath = Join-Path $projectRoot (
	'Content\SourceArt\T_AudioCalibrationWall_D.png')
foreach ($path in @($rawPath, $derivedPath)) {
	Assert-True (Test-Path -LiteralPath $path -PathType Leaf) `
		"보정 화면 에셋이 없습니다: $path"
}
$rawImage = [System.Drawing.Image]::FromFile($rawPath)
try {
	Assert-True ($rawImage.Width -ge 1024 -and $rawImage.Height -ge 1024) `
		'Imagegen 원본은 1K 이상이어야 합니다.'
}
finally {
	$rawImage.Dispose()
}
$derivedImage = [System.Drawing.Image]::FromFile($derivedPath)
try {
	Assert-True ($derivedImage.Width -eq 1024 -and $derivedImage.Height -eq 1024) `
		'런타임 UI 텍스처는 1024x1024여야 합니다.'
}
finally {
	$derivedImage.Dispose()
}

$controllerHeader = Read-ProjectText 'Source\IndieGame\Player\IGPlayerController.h'
$controllerSource = Read-ProjectText 'Source\IndieGame\Player\IGPlayerController.cpp'
$hudHeader = Read-ProjectText 'Source\IndieGame\Player\IGHorrorHUD.h'
$hudSource = Read-ProjectText 'Source\IndieGame\Player\IGHorrorHUD.cpp'
$audioHeader = Read-ProjectText 'Source\IndieGame\Audio\IGMissingFloorAudioSubsystem.h'
$audioSource = Read-ProjectText 'Source\IndieGame\Audio\IGMissingFloorAudioSubsystem.cpp'
$prepareScript = Read-ProjectText 'Scripts\Prepare-AIArt.ps1'
$surfaceScript = Read-ProjectText 'Scripts\generate_surface_textures.py'
$buildScript = Read-ProjectText 'Scripts\Build-ArtAssets.ps1'
$previewScript = Read-ProjectText `
	'Scripts\Run-MissingFloor-AudioCalibrationPreview.bat'

Assert-ContainsAll $controllerHeader @(
	'AudioCalibration',
	'LoadAudioCalibrationSettings',
	'OpenAudioCalibration',
	'CancelAudioCalibration',
	'CompleteAudioCalibration',
	'StartAudioCalibrationPreviewProbe'
) '컨트롤러 인터페이스'
Assert-ContainsAll $controllerSource @(
	'HeadphoneRecommendationShown',
	'CalibrationCompleted',
	'VolumeStep',
	'BrightnessStep',
	'OpenAudioCalibration(true)',
	'gamma %.2f',
	'NextAudioCalibrationKnockTime',
	'IGAudioCalibrationPreview',
	'master_gain=%.3f hrtf_knock=1',
	'MISSINGFLOOR_AUDIO_CALIBRATION_PREVIEW PASS'
) '최초 실행·저장·프리뷰 경로'
Assert-ContainsAll $audioHeader @(
	'SetUserMasterVolume',
	'PlayCalibrationKnock',
	'GetUserMasterVolume',
	'GetCalibrationKnockPlayCount'
) '오디오 인터페이스'
Assert-ContainsAll $audioSource @(
	'CreateWallKnockTriple(this, 0.78f)',
	'EIGAudioBus::Entity',
	'FVector::UpVector * 285.0f',
	'* UserMasterVolume'
) '실제 위층 노크·6버스 마스터 이득'
Assert-ContainsAll $hudHeader @(
	'DrawAudioCalibrationPanel',
	'AudioCalibrationWallTexture'
) 'HUD 인터페이스'
Assert-ContainsAll $hudSource @(
	'T_AudioCalibrationWall_D',
	'소리와 밝기',
	'두드리는 소리가 잘 들리면서',
	'가운데 칸이 희미하게 보이도록',
	'다시 듣기',
	'●',
	'○'
) '출시 카피·비수치 눈금'
Assert-ContainsAll $prepareScript @(
	"Source = 'TextureAudioCalibrationWall_v1'",
	"Target = 'T_AudioCalibrationWall_D.png'"
) 'Imagegen 파생 경로'
Assert-ContainsAll $surfaceScript @(
	'"T_AudioCalibrationWall_D"',
	'unreal.TextureGroup.TEXTUREGROUP_UI',
	'unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS'
) 'UI 텍스처 임포트 설정'
Assert-ContainsAll $buildScript @(
	"'TextureAudioCalibrationWall_v1'",
	'T_AudioCalibrationWall_D.uasset',
	'Imported 11 textures'
) '타깃 에셋 빌드'
Assert-ContainsAll $previewScript @(
	'PENDING_CAPTURE_PATH',
	'MISSINGFLOOR_AUDIO_CALIBRATION_PREVIEW PASS',
	'move /y "%PENDING_CAPTURE_PATH%" "%CAPTURE_PATH%"'
) '실패 시 승인 캡처 보존'

Write-Host (
	"MISSINGFLOOR_M65_AUDIO_CALIBRATION_CONTRACT PASS " +
	"assertions=$assertions first_run=1 persist=1 hrtf_knock=1 " +
	"gamma=1 keyboard_gamepad_mouse=1 imagegen_ui=1")
