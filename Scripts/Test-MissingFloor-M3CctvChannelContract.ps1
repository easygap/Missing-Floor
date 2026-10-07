<#
.SYNOPSIS
	§14 CCTV 채널 5 계약 검사.

.DESCRIPTION
	이 비트는 게임 전체에서 한 번만 재생되고 되감을 수 없다. 그래서 깨졌다는
	사실이 플레이 중에 드러날 방법이 없고, 계약으로 묶어 두는 것 말고는 지킬
	방법이 없다. 세 가지를 검사한다.

	하나, 성능 예산(§14 상시 렌더 금지) — 렌더타깃과 캡처가 누르는 순간에만
	생기고 죽을 때 해제되는지, 캡처가 매 프레임이 아닌지.

	둘, 화각의 단일 출처(§17 회수표) — 카메라 프롭·씬 캡처·밤3 재확인이 같은
	세 값을 읽는지. 프롭 없이 채널만 있으면 밤3에 재확인할 대상이 사라진다.

	셋, §5.5와의 정합(§19.9 위험 8) — 라벨의 글자와 저장 불가 규칙이 서로
	모순되지 않는지. 문서가 지정한 문구 그대로여야 한다.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Read-Source([string]$RelativePath) {
	Get-Content -Raw -Encoding UTF8 -LiteralPath (
		Join-Path $projectRoot $RelativePath)
}

$channelHeader = Read-Source 'Source/IndieGame/Environment/IGCctvChannelFive.h'
$channel = Read-Source 'Source/IndieGame/Environment/IGCctvChannelFive.cpp'
$scene = Read-Source 'Source/IndieGame/Core/IGPrologueWorldScene.cpp'
$sceneHeader = Read-Source 'Source/IndieGame/Core/IGPrologueWorldScene.h'
$puzzleTwo = Read-Source 'Source/IndieGame/Entity/IGMissingFloorPuzzleTwoDirector.cpp'
$greybox = Read-Source 'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'
$toneHeader = Read-Source 'Source/IndieGame/Audio/IGToneSequenceSoundWave.h'
$tone = Read-Source 'Source/IndieGame/Audio/IGToneSequenceSoundWave.cpp'
$signs = Read-Source 'Scripts/Create-SignTextures.ps1'
$materials = Read-Source 'Scripts/create_textured_materials.py'
$validator = Read-Source 'Scripts/validate_baked_art_assets.py'
$artBuild = Read-Source 'Scripts/Build-ArtAssets.ps1'
$textures = Read-Source 'Scripts/generate_surface_textures.py'
$feedRunner = Read-Source 'Scripts/Run-MissingFloor-CctvFeedProbe.ps1'
$assertions = 0

function Require-All(
	[string]$Source,
	[string[]]$Needles,
	[string]$ContractName) {
	foreach ($needle in $Needles) {
		if (-not $Source.Contains($needle)) {
			throw "$ContractName invariant is missing: $needle"
		}
		$script:assertions++
	}
}

function Require-None(
	[string]$Source,
	[string[]]$Needles,
	[string]$ContractName) {
	foreach ($needle in $Needles) {
		if ($Source.Contains($needle)) {
			throw "$ContractName forbids: $needle"
		}
		$script:assertions++
	}
}

# --- §14 상시 렌더 금지 -----------------------------------------------------
# 아무것도 없는 상태에서 시작하고, 누르는 줄에서만 할당한다.
Require-All $channel @(
	'Feed = NewObject<UTextureRenderTarget2D>(this, TEXT("CctvChannelFiveFeed"));',
	'Capture = NewObject<USceneCaptureComponent2D>(',
	'Capture->bCaptureEveryFrame = false;',
	'Capture->bCaptureOnMovement = false;',
	'Capture->CaptureScene();',
	'ReleaseChannel();',
	'Capture->TextureTarget = nullptr;',
	'Feed->ReleaseResource();',
	'Feed = nullptr;'
) '§14 상시 렌더 금지'
# 매 프레임 캡처로 되돌리는 것은 이 기능의 유일한 성능 위험이다.
Require-None $channel @(
	'bCaptureEveryFrame = true'
) '§14 상시 렌더 금지'

# CIF. 아날로그 채널의 실제 해상도다. 전구 하나가 전구로 남고 나머지는 어둠이다.
Require-All $channel @(
	'constexpr int32 FeedWidth = 352;',
	'constexpr int32 FeedHeight = 288;',
	'constexpr float CaptureIntervalSeconds = 1.0f / 12.0f;',
	'Feed->RenderTargetFormat = RTF_RGBA8_SRGB;',
	'ESceneCaptureSource::SCS_FinalColorLDR'
) 'CIF analog channel'

# 1회 한정, 반복 재생 불가 — 서사 플래그가 아니라 액터가 막는다.
Require-All $channelHeader @(
	'bool Play();',
	'bool IsSpent() const'
) 'one-shot channel'
Require-All $channel @(
	'if (bUsed)',
	'bUsed = true;'
) 'one-shot channel'

# 상태 기계. 지직임 → 화면 → 지직임 → 4분할.
Require-All $channelHeader @(
	'Idle,',
	'Acquiring,',
	'Live,',
	'Collapsing,',
	'Spent'
) 'channel state machine'
Require-All $channel @(
	'constexpr float AcquireSeconds = 0.32f;',
	'constexpr float CollapseSeconds = 0.86f;',
	'ScreenFace->SetHiddenInGame(true);'
) 'channel state machine'

# --- 머티리얼 파라미터 계약 -------------------------------------------------
# 파이썬이 만드는 이름과 C++이 쓰는 이름이 어긋나면 화면은 조용히 검게 남는다.
Require-All $materials @(
	'def create_cctv_monitor_material(assets, tools):',
	'"M_CctvChannelFive"',
	'feed.set_editor_property("parameter_name", "Feed")',
	'static_amount.set_editor_property("parameter_name", "Static")',
	'gain.set_editor_property("parameter_name", "Gain")',
	'unreal.MaterialShadingModel.MSM_UNLIT',
	'unreal.NoiseFunction.NOISEFUNCTION_VALUE_ALU',
	'MP_EMISSIVE_COLOR'
) 'CCTV monitor material graph'
Require-All $channel @(
	'M_CctvChannelFive.M_CctvChannelFive',
	'SetTextureParameterValue(TEXT("Feed"), Feed)',
	'SetScalarParameterValue(TEXT("Static"), StaticMix)',
	'SetScalarParameterValue(',
	'TEXT("Gain"), IGCctvFive::ScreenGain)'
) 'CCTV monitor material binding'
Require-All $validator @(
	'CCTV_MONITOR_MATERIAL = "M_CctvChannelFive"',
	'CCTV_SCALAR_PARAMETERS = {"Static", "Gain"}',
	'CCTV_TEXTURE_PARAMETERS = {"Feed"}',
	'get_scalar_parameter_names',
	'get_texture_parameter_names'
) 'baked parameter audit'

# --- §17 화각의 단일 출처 ---------------------------------------------------
# 프롭·캡처·밤3 재확인이 같은 세 값을 읽어야 한다.
Require-All $scene @(
	'const FVector CctvCameraLocation(-355.0f, 486.0f, 1404.0f);',
	'const FRotator CctvCameraRotation(-25.0f, 29.0f, 0.0f);',
	'constexpr float CctvCameraFieldOfView = 78.0f;',
	'MissingFloorCctvCamera = CreateBlock(',
	'IGPrologueWorld::CctvCameraLocation',
	'IGPrologueWorld::CctvCameraRotation'
) '§17 single source of framing'
Require-All $sceneHeader @(
	'FVector GetMissingFloorCctvCameraLocation() const;',
	'FRotator GetMissingFloorCctvCameraRotation() const;',
	'float GetMissingFloorCctvFieldOfView() const;'
) '§17 single source of framing'
Require-All $channel @(
	'SceneActor->GetMissingFloorCctvCameraLocation(),',
	'SceneActor->GetMissingFloorCctvCameraRotation());',
	'Capture->FOVAngle = SceneActor->GetMissingFloorCctvFieldOfView();'
) '§17 single source of framing'

# 화면에 있어야 하는 것: 자재 더미, 비닐, 전구 하나. 그리고 지나가는 것은
# 없다. 형체 세 상자는 지웠다(2026-09-10) — 그는 화면에 안 나오고(§4.6)
# 소리로만 온다. 화면이 살아 있는 동안 복도가 한 번 운다.
Require-All $scene @(
	'StackBoards(FVector(0, 590, 1200), FVector(120, 80, 0), 40);',
	'StackBoards(FVector(-80, 780, 1200), FVector(140, 60, 0), 15);',
	'StackBoards(FVector(120, 880, 1200), FVector(90, 50, 0), 30);',
	'BoardStack->AddInstance(',
	'BoardStack->SetStaticMesh(PropMesh(TEXT("SM_GypsumCutBoard")))',
	'TEXT("SM_ConstructionSheetDrape")',
	'TexMat(TEXT("M_ConstructionFilm"), GlassMaterial)',
	'AddSheeting('
) 'beat 2-2 shot list'
Require-All $channel @(
	'constexpr float LiveSoundProgress = 0.46f;',
	'CreateEntityCrawlStep(this, false)'
) 'beat 2-2 the corridor sounds'
Require-All $channelHeader @(
	'bool HasLiveSoundPlayed() const'
) 'beat 2-2 probe receipt'
foreach ($forbidden in @('LowShape', 'IsShapeCrossing', 'IGCctvShapeOnly')) {
	if ($channel.Contains($forbidden)) {
		throw "채널 5에 형체가 다시 들어왔다: $forbidden"
	}
	$assertions++
}

# 카메라의 조명과 동축은 영구 설치물이다. 비트 동안만 존재하면 밤3의 같은
# 자리가 다른 장소가 된다.
Require-All $scene @(
	'const FVector CctvMount(-372.0f, 478.0f, 1438.0f);',
	'const FVector CoaxRun[] = {'
) 'permanent camera install'

# --- §5.5 정합 (§19.9 위험 8) -----------------------------------------------
# 라벨 문구는 문서가 지정한 그대로여야 한다.
Require-All $signs @(
	"'T_SignAux5MonitorOnly_D.png'",
	"Draw-CenteredText `$g 'AUX 5'",
	"Draw-CenteredText `$g 'MONITOR ONLY'",
	'-Width 640 -Height 240'
) '§5.5 AUX label wording'
Require-All $materials @(
	'"M_SignAux5MonitorOnly": {',
	'"tex_asset": "T_SignAux5MonitorOnly_D"'
) '§5.5 AUX label material'
Require-All $channel @(
	'M_SignAux5MonitorOnly.',
	'const FVector LabelCenter('
) '§5.5 AUX label placement'
Require-All $puzzleTwo @(
	'CctvChannelFive->Play()',
	'녹화는 안 되네. 어디로 이어지는지 봐 두자.'
) '§5.5 AUX label reading'

# --- 오디오 ----------------------------------------------------------------
# 한국 계통은 60 Hz이고, 이 모니터는 NTSC 수평 주파수로 만들어졌다.
Require-All $toneHeader @(
	'static UIGToneSequenceSoundWave* CreateCrtChannelSwitch(',
	'static UIGToneSequenceSoundWave* CreateCrtChannelBed('
) 'channel audio surface'
Require-All $tone @(
	'constexpr float MainsHz = 60.0f;',
	'constexpr float LineWhineHz = 15734.0f;',
	'IGCrtChannelCollapse',
	'IGCrtChannelAcquire',
	'IGCrtChannelBed'
) 'channel audio content'
Require-All $channel @(
	'CreateCrtChannelSwitch(this, false)',
	'CreateCrtChannelSwitch(this, true)',
	'CreateCrtChannelBed('
) 'channel audio wiring'

# --- 베이크된 에셋이 실제로 있는지 ------------------------------------------
foreach ($asset in @(
	'Content/SourceArt/T_SignAux5MonitorOnly_D.png',
	'Content/Prototype/Textures/T_SignAux5MonitorOnly_D.uasset',
	'Content/Prototype/Materials/M_SignAux5MonitorOnly.uasset',
	'Content/Prototype/Materials/M_CctvChannelFive.uasset')) {
	if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $asset) -PathType Leaf)) {
		throw "§14 baked asset is missing: $asset"
	}
	$assertions++
}
Require-All $textures @(
	'"T_SignAux5MonitorOnly_D",'
) 'texture import registry'
Require-All $artBuild @(
	'Content\Prototype\Textures\T_SignAux5MonitorOnly_D.uasset',
	'Content\Prototype\Materials\M_SignAux5MonitorOnly.uasset',
	'Content\Prototype\Materials\M_CctvChannelFive.uasset',
	"'\[IndieGame\] Imported 6 textures'"
) 'targeted bake manifest'

# --- 검증 경로 -------------------------------------------------------------
# 구조는 어디서나 돌고, 픽셀 판정은 실제 RHI를 요구한다. NullRHI에서 캡처는
# 검정이고 검정은 밝기 하한을 통과한다 — V5가 처음 스스로를 속인 방식이다.
Require-All $greybox @(
	'case EProbeStep::CctvChannelContract:',
	'MISSINGFLOOR_CCTV5 PASS',
	'MISSINGFLOOR_CCTV5_FEED PASS',
	'MISSINGFLOOR_CCTV5_FEED FAIL',
	'MISSINGFLOOR_CCTV5_FEED SKIP',
	'§14 상시 렌더 금지 broken before the press',
	'the corridor never sounded while the picture was up',
	'channel five played a second time',
	'FParse::Param(FCommandLine::Get(), TEXT("nullrhi"))',
	'ReadRenderTarget(',
	'ExportRenderTarget('
) 'runtime channel probe'
# 창은 절대 뜨지 않는다. 오프스크린 플래그가 빠진 조합은 실행되지 않는다.
Require-All $feedRunner @(
	"'-RenderOffScreen',",
	"if (`$arguments -notcontains '-RenderOffScreen')",
	"if (`$arguments -contains '-nullrhi')"
) 'offscreen runner guard'
# 노출 저작 스위치. 이게 없으면 화각 문제를 프레임에서 읽는 대신 좌표를
# 손으로 계산하게 된다.
Require-All $feedRunner @(
	"'-IGCctvExposure={0}' -f `$Exposure"
) 'authoring switches'
Require-All $channel @(
	'TEXT("IGCctvExposure=")'
) 'authoring switches'

Write-Host (
	"MISSINGFLOOR_M3_CCTV5_CONTRACT PASS assertions=$assertions " +
	'feed=352x288 capture_fps=12 live=5.60s plays=1 ' +
	'params=Feed|Static|Gain label=AUX5_MONITOR_ONLY mains=60Hz') `
	-ForegroundColor Green
