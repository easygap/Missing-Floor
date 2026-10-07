[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot

function Read-ProjectText {
	param([Parameter(Mandatory = $true)][string]$RelativePath)
	$path = Join-Path $projectRoot $RelativePath
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Missing M5 contract file: $RelativePath"
	}
	return Get-Content -Raw -Encoding UTF8 -LiteralPath $path
}

function Assert-ContainsAll {
	param(
		[Parameter(Mandatory = $true)][string]$Text,
		[Parameter(Mandatory = $true)][string[]]$Tokens,
		[Parameter(Mandatory = $true)][string]$Label
	)
	foreach ($token in $Tokens) {
		if (-not $Text.Contains($token)) {
			throw "$Label contract is missing: $token"
		}
	}
}

$story = Read-ProjectText 'Docs/STORY_BIBLE_MISSING_FLOOR.md'
$matrix = Read-ProjectText 'Docs/MISSING_FLOOR_ART_MATRIX.md'
$promptRecord = Read-ProjectText 'Docs/IMAGEGEN_PROMPTS_2026-08-11.md'
$meshScript = Read-ProjectText 'Scripts/generate_meshes.py'
$buildScript = Read-ProjectText 'Scripts/Build-ArtAssets.ps1'
$auditScript = Read-ProjectText 'Scripts/validate_baked_art_assets.py'
$prepareScript = Read-ProjectText 'Scripts/Prepare-AIArt.ps1'
$pbrScript = Read-ProjectText 'Scripts/generate_ai_pbr_maps.py'
$materialScript = Read-ProjectText 'Scripts/create_textured_materials.py'
$nightFourHeader = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.h'
$nightFourSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGMissingFloorNightFourDirector.cpp'
$listenerHeader = Read-ProjectText 'Source/IndieGame/Entity/IGListenerEntity.h'
$listenerSource = Read-ProjectText 'Source/IndieGame/Entity/IGListenerEntity.cpp'
$greyboxSource = Read-ProjectText `
	'Source/IndieGame/Entity/IGListenerGreyboxDirector.cpp'

Assert-ContainsAll $story @(
	'## 30. v2.9 — 공동 리빌과 목한수 대치',
	'시선 내적 0.72',
	'1.05초간 완전한 정적',
	'Night4.FinalReveal',
	'Night4.FinalConfrontation',
	'Ending.C'
) 'M5 story'

$referenceFiles = @{
	'Content/SourceArt/AI/SheetFinalCavityRemainsReference_v1.png' =
		'950EA1404E6804C083F4EF060ABA7AE175EB70DAD9BE396333605E889C855297'
	'Content/SourceArt/AI/SheetMokHansooConfrontationReference_v1.png' =
		'466FC1AF73CD8852E955022FA5D9FBE1F38A04F6623F318F966F0373FEBCF618'
	'Content/SourceArt/AI/FinalCavityFrontBlend_v1.png' =
		'9C26AAC9D3160CBD73B3183A332BC822FA8B6A1B655D5B24A6D642E1952B6D11'
	'Content/SourceArt/AI/MokHansooFinalFrontBlend_v1.png' =
		'B2C6787D66E595F6A32BD6FD653060773097BF560CFE0EEDF0877EAB6FA6547C'
}
foreach ($entry in $referenceFiles.GetEnumerator()) {
	$path = Join-Path $projectRoot $entry.Key
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "M5 ImageGen reference is missing: $($entry.Key)"
	}
	$actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
	if ($actual -ne $entry.Value) {
		throw "M5 ImageGen reference hash changed: $($entry.Key)"
	}
}
Assert-ContainsAll $promptRecord @(
	'SheetFinalCavityRemainsReference_v1.png',
	'SheetMokHansooConfrontationReference_v1.png',
	'FinalCavityFrontBlend_v1.png',
	'MokHansooFinalFrontBlend_v1.png',
	'Primary request: a non-graphic, dry, partially skeletonized adult male remains',
	'Primary request: Mok Han-su, a weary Korean apartment maintenance manager',
	'front-facing PBR detail cutout for a first-person Korean apartment horror game',
	'front-facing PBR character detail cutout for a first-person Korean apartment horror game'
) 'M5 ImageGen prompt record'

$detailLayers = @{
	'T_SpriteFinalCavity' = 'M_SpriteFinalCavity'
	'T_SpriteMokFinalUpper' = 'M_SpriteMokFinalUpper'
}
foreach ($entry in $detailLayers.GetEnumerator()) {
	$stem = $entry.Key
	$material = $entry.Value
	Assert-ContainsAll $prepareScript @(
		"$stem`_D.png"
	) "M5 $stem preparation"
	Assert-ContainsAll $pbrScript @($stem) "M5 $stem PBR generation"
	Assert-ContainsAll $materialScript @($material, $stem) `
		"M5 $material generation"
	foreach ($suffix in @('D', 'N', 'R', 'A')) {
		$sourceTexture = Join-Path $projectRoot `
			"Content/SourceArt/$stem`_$suffix.png"
		$bakedTexture = Join-Path $projectRoot `
			"Content/Prototype/Textures/$stem`_$suffix.uasset"
		if (-not (Test-Path -LiteralPath $sourceTexture -PathType Leaf)) {
			throw "M5 source texture is missing: $stem`_$suffix"
		}
		if (-not (Test-Path -LiteralPath $bakedTexture -PathType Leaf)) {
			throw "M5 baked texture is missing: $stem`_$suffix"
		}
	}
	$materialPath = Join-Path $projectRoot `
		"Content/Prototype/Materials/$material.uasset"
	if (-not (Test-Path -LiteralPath $materialPath -PathType Leaf)) {
		throw "M5 baked material is missing: $material"
	}
}

$meshNames = @(
	'SM_FinalCavityClothingShell',
	'SM_FinalCavityBoneInsert',
	'SM_FinalCavityTarp',
	'SM_FinalCavityBrokenCaster',
	'SM_MokHansooWorkwear',
	'SM_MokHansooHeadHands',
	'SM_MokHansooGypsumBoard'
)
foreach ($name in $meshNames) {
	if (-not $meshScript.Contains('"' + $name + '"')) {
		throw "M5 mesh builder is missing: $name"
	}
	if (-not $buildScript.Contains("Content\Meshes\$name.uasset")) {
		throw "M5 targeted art output is missing: $name"
	}
	if (-not $auditScript.Contains('"' + $name + '"')) {
		throw "M5 baked-asset audit is missing: $name"
	}
	$assetPath = Join-Path $projectRoot "Content/Meshes/$name.uasset"
	if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
		throw "M5 baked mesh is missing: $name"
	}
}

Assert-ContainsAll $meshScript @(
	'def build_final_cavity_clothing_shell',
	'def build_final_cavity_bone_insert',
	'def build_final_cavity_tarp',
	'def build_final_cavity_broken_caster',
	'def build_mok_hansoo_workwear',
	'def build_mok_hansoo_head_hands',
	'def build_mok_hansoo_gypsum_board',
	'complete: {built}/{len(builders)} meshes'
) 'M5 Geometry Script'
Assert-ContainsAll $buildScript @(
	'\[MESHGEN\] complete: 12/12 meshes',
	'ART_BUILD PASS uasset_audit=1'
) 'M5 art build'

Assert-ContainsAll $nightFourHeader @(
	'IsFinalRevealPlaying()',
	'IsFinalConfrontationComplete()',
	'HandleNightFourCapture(APawn* Player)',
	'UpdateCavityReveal(float DeltaSeconds)',
	'TArray<TObjectPtr<UStaticMeshComponent>> CavityRevealVisuals',
	'TArray<TObjectPtr<UStaticMeshComponent>> MokVisuals',
	'UpdateFinaleDetailLayers()',
	'CavityDetailCard',
	'MokDetailCard'
) 'M5 director header'
Assert-ContainsAll $nightFourSource @(
	'const FName FinalRevealBeat(TEXT("Night4.FinalReveal"))',
	'const FName FinalConfrontationBeat(TEXT("Night4.FinalConfrontation"))',
	'static constexpr float Seconds[] = {1.8f, 2.0f, 2.2f}',
	'Facing >= 0.72f',
	'RevealStageElapsedSeconds >= 5.5f',
	'WaterMaskBed->FadeOut(0.14f, 0.0f)',
	'CreateWallKnockReply(this)',
	'0.25f,',
	'"MokHansooFinalLine"',
	'BeginFinalePass(',
	'0.18f,',
	'0.45f,',
	'|| !Narrative->HasBeatPlayed(IGNightFour::FinalConfrontationBeat)',
	'&& bFinalConfrontationComplete',
	'BeginFailureListing()',
	'CreateWallpaperSeamRoller(this)',
	'FailureRetryDelaySeconds = 3.2f',
	'ResetAfterFailureEnding()',
	'TEXT("M_SpriteFinalCavity")',
	'TEXT("M_SpriteMokFinalUpper")',
	'Distance > 105.0f',
	'Distance < 360.0f',
	'> 0.68f',
	'SetCastHiddenShadow('
) 'M5 director runtime'
if ($nightFourSource.Contains('TEXT("M_SpriteMok")') -or
	$nightFourSource.Contains('TEXT("T_SpriteMok_D")')) {
	throw 'Night-four close confrontation must not use the distant Mok sprite.'
}

Assert-ContainsAll $listenerHeader @(
	'FinaleLured',
	'void BeginFinalePass(',
	'bool IsFinalePassActive() const'
) 'M5 listener header'
Assert-ContainsAll $listenerSource @(
	'case EIGListenerState::FinaleLured:',
	'SetActorEnableCollision(false)',
	'|| State == EIGListenerState::FinaleLured',
	'FinaleRoutePoints[FinaleRouteIndex]'
) 'M5 listener pass'
Assert-ContainsAll $greyboxSource @(
	'if (!NightFour->IsFinalConfrontationComplete())',
	'StepDeadlineSeconds > 28.0f',
	'night-4 reveal/confrontation sequence stalled'
) 'M5 runtime probe'
Assert-ContainsAll $greyboxSource @(
	'SetFinaleCapturePreview(true, false)',
	'SetFinaleCapturePreview(true, true)',
	'CaptureShot(TEXT("night4-mok-confrontation"))'
) 'M5 visual capture'
Assert-ContainsAll $matrix @(
	'공동 최종 잔존물',
	'목한수 최종 대치',
	'근접 `T_SpriteMok_D` 사용 0',
	'`M_SpriteFinalCavity`',
	'`M_SpriteMokFinalUpper`',
	'105~360cm'
) 'M5 art matrix'

Write-Host 'MISSING_FLOOR_M5_REVEAL_CONTRACT PASS references=4 meshes=7 detail_layers=2 gaze_stages=3 endings=3'
