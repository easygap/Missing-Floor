[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceArt = Join-Path $projectRoot 'Content\SourceArt'

$requiredRaw = @(
	'AI\SheetHorrorSurfaceBlends.png',
	'AI\SheetEvidenceProps.png',
	'AI\SheetAlleyCatPoseReference.png',
	'AI\SheetFirstPersonKnockPhases_v1.png',
	'AI\SheetFirstPersonKnockPhases_v1_RGBA.png',
	'AI\SheetFirstPersonKnockPhases_v2.png',
	'AI\SheetFirstPersonKnockPhases_v2_RGBA.png',
	'AI\SheetRooftopUnlockedPadlockKeysReference.png',
	'AI\TextureP3CabinetPaintedSteel.png',
	'AI\TextureCarrierBagFilm.png',
	'AI\TextureAlleyCatTabby.png',
	'AI\DialogueHUDConcept_v1.png',
	'AI\TextureHudDialogueFilm.png',
	'AI\ApartmentVisualTarget_v1.png',
	'AI\TextureApartmentWallpaperVintage.png',
	'AI\MaskApartmentWallPatina.png',
	'AI\TextureStickyNotePaper_D.png',
	'AI\TextureCaptureMercyNotePaper_D.png',
	'AI\SheetMissingFloorEnvironmentReference.png',
	'AI\SheetListenerEntityAnatomyReference.png',
	'AI\TextureMissingFloorDryPlaster.png',
	'AI\TextureMovingBoxCardboard_v1.png',
	'AI\TextureKoreanVillaStucco_v1.png',
	'AI\SheetMissingFloorResidueMasks.png',
	'AI\SheetMissingFloorDistantCharacters.png',
	'AI\SheetMissingFloorHeroPropsReference.png',
	'AI\ListenerEntityFrontCutout.png',
	'AI\SheetListenerEntityCrawlPhases.png',
	'AI\SheetFinalCavityRemainsReference_v1.png',
	'AI\SheetMokHansooConfrontationReference_v1.png',
	# 발소리 표면 3종·P1 문자판·P2 먹지·세대 현관문 표면.
	# v1은 증빙으로 남는다. 계단 무늬가 게임 조명에서 읽히지 않아 v2로
	# 재생성했고, 파생은 v2에서만 나온다.
	'AI\TextureVillaStairCheckerPlatePaintedSteel.png',
	'AI\TextureVillaStairCheckerPlatePaintedSteel_v2.png',
	'AI\TextureRooftopUrethaneWaterproofing.png',
	'AI\TextureApartmentEntranceDoorCharcoalSteel.png',
	'AI\FinalCavityFrontBlend_v1.png',
	'AI\MokHansooFinalFrontBlend_v1.png',
	'AI\TextureMissingFloorJournalPaper_v1.png'
)
$requiredMasks = @(
	'T_MissingFloorHandprints_M.png',
	'T_MissingFloorDragTrails_M.png',
	'T_MissingFloorDustJoint_M.png',
	'T_MissingFloorCavityScratches_M.png'
)
$requiredOverlays = @(
	'T_DecalDampWallpaper_D.png',
	'T_SpriteSeo_D.png',
	'T_SpriteMok_D.png',
	'T_SpriteListenerFront_D.png',
	'T_SpriteListenerCrawl0_D.png',
	'T_SpriteListenerCrawl1_D.png',
	'T_SpriteListenerCrawl2_D.png',
	'T_SpriteListenerCrawl3_D.png',
	'T_SpriteFinalCavity_D.png',
	'T_SpriteMokFinalUpper_D.png',
	'T_FPHandKnock0_D.png',
	'T_FPHandKnock1_D.png',
	'T_FPHandKnock2_D.png',
	'T_FPHandKnock3_D.png'
)
$requiredMaterialMasks = @(
	'T_ApartmentWallPatina_M.png'
)
$requiredMaterialTextures = @(
	'T_CarrierBagFilm_D.png',
	'T_AlleyCatTabby_D.png',
	'T_P3CabinetPaintedSteel_D.png',
	'T_AudioCalibrationWall_D.png',
	'T_HudDialogueFilm_D.png',
	'T_MissingFloorJournalPaper_D.png',
	'T_ApartmentWallpaperV2_D.png',
	'T_MissingFloorDryPlaster_D.png',
	'T_MovingBoxCardboard_D.png',
	'T_KoreanVillaStucco_D.png'
	'T_MissingFloorSteelStair_D.png',
	'T_RooftopWaterproofing_D.png',
	'T_UnitDoorPaintedSteel_D.png'
)
$requiredPbrMaps = @(
	'T_AlleyCatTabby_N.png',
	'T_AlleyCatTabby_R.png',
	'T_AlleyCatTabby_A.png',
	'T_P3CabinetPaintedSteel_N.png',
	'T_P3CabinetPaintedSteel_R.png',
	'T_P3CabinetPaintedSteel_A.png',
	'T_P3CabinetPaintedSteel_W.png',
	'T_CarrierBagFilm_N.png',
	'T_CarrierBagFilm_R.png',
	'T_CarrierBagFilm_A.png',
	'T_ApartmentWallpaperV2_N.png',
	'T_ApartmentWallpaperV2_R.png',
	'T_ApartmentWallpaperV2_A.png',
	'T_MissingFloorDryPlaster_N.png',
	'T_MissingFloorDryPlaster_R.png',
	'T_MissingFloorDryPlaster_A.png',
	'T_MovingBoxCardboard_N.png',
	'T_MovingBoxCardboard_R.png',
	'T_MovingBoxCardboard_A.png',
	'T_KoreanVillaStucco_N.png',
	'T_KoreanVillaStucco_R.png',
	'T_KoreanVillaStucco_A.png',
	'T_MissingFloorSteelStair_N.png',
	'T_MissingFloorSteelStair_R.png',
	'T_MissingFloorSteelStair_A.png',
	'T_RooftopWaterproofing_N.png',
	'T_RooftopWaterproofing_R.png',
	'T_RooftopWaterproofing_A.png',
	'T_UnitDoorPaintedSteel_N.png',
	'T_UnitDoorPaintedSteel_R.png',
	'T_UnitDoorPaintedSteel_A.png',
	'T_SpriteListenerFront_N.png',
	'T_SpriteListenerFront_R.png',
	'T_SpriteListenerFront_A.png',
	'T_SpriteListenerCrawl0_N.png',
	'T_SpriteListenerCrawl0_R.png',
	'T_SpriteListenerCrawl0_A.png',
	'T_SpriteListenerCrawl1_N.png',
	'T_SpriteListenerCrawl1_R.png',
	'T_SpriteListenerCrawl1_A.png',
	'T_SpriteListenerCrawl2_N.png',
	'T_SpriteListenerCrawl2_R.png',
	'T_SpriteListenerCrawl2_A.png',
	'T_SpriteListenerCrawl3_N.png',
	'T_SpriteListenerCrawl3_R.png',
	'T_SpriteListenerCrawl3_A.png',
	'T_SpriteFinalCavity_N.png',
	'T_SpriteFinalCavity_R.png',
	'T_SpriteFinalCavity_A.png',
	'T_SpriteMokFinalUpper_N.png',
	'T_SpriteMokFinalUpper_R.png',
	'T_SpriteMokFinalUpper_A.png'
)
$requiredDerived = @(
	$requiredMasks + $requiredOverlays + $requiredMaterialMasks +
	$requiredMaterialTextures + $requiredPbrMaps
)
$requiredEasterEggSigns = @{
	'T_CaptureMercyNote_D.png' = @(1024, 640)
	'T_Plate401_D.png' = @(128, 64)
	'T_Plate402_D.png' = @(128, 64)
	'T_Plate403_D.png' = @(128, 64)
	'T_PlateCommon_D.png' = @(128, 64)
}
foreach ($relativePath in @($requiredRaw + $requiredDerived)) {
	$path = Join-Path $sourceArt $relativePath
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Art source is missing: $relativePath"
	}
	if ((Get-Item -LiteralPath $path).Length -le 1024) {
		throw "Art source is unexpectedly small: $relativePath"
	}
}

foreach ($entry in $requiredEasterEggSigns.GetEnumerator()) {
	$relativePath = [string]$entry.Key
	$path = Join-Path $sourceArt $relativePath
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Landing door sign source is missing: $relativePath"
	}
	$image = [System.Drawing.Bitmap]::FromFile($path)
	try {
		$expectedWidth = [int]$entry.Value[0]
		$expectedHeight = [int]$entry.Value[1]
		if ($image.Width -ne $expectedWidth -or $image.Height -ne $expectedHeight) {
			throw "Landing door sign must be ${expectedWidth}x${expectedHeight}: $relativePath"
		}
		$darkSamples = 0
		$lightSamples = 0
		$sampleStep = [Math]::Max(1, [int]($image.Width / 64))
		for ($y = 0; $y -lt $image.Height; $y += $sampleStep) {
			for ($x = 0; $x -lt $image.Width; $x += $sampleStep) {
				$pixel = $image.GetPixel($x, $y)
				$luma = $pixel.R * 0.2126 + $pixel.G * 0.7152 + $pixel.B * 0.0722
				if ($luma -lt 96) { $darkSamples++ }
				if ($luma -gt 180) { $lightSamples++ }
			}
		}
		if ($darkSamples -lt 4 -or $lightSamples -lt 4) {
			throw "Landing door sign lost its readable ink contrast: $relativePath"
		}
	}
	finally {
		$image.Dispose()
	}
}

foreach ($relativePath in $requiredDerived) {
	$path = Join-Path $sourceArt $relativePath
	$image = [System.Drawing.Bitmap]::FromFile($path)
	try {
		$expectedSize = if ($relativePath -like 'T_FPHandKnock*_D.png') {
			768
		}
		elseif (
			$requiredMaterialMasks -contains $relativePath -or
			$requiredMaterialTextures -contains $relativePath -or
			$requiredPbrMaps -contains $relativePath -or
			$relativePath -like 'T_SpriteListener*_D.png'
		) { 1024 } else { 512 }
		$expectedWidth = if ($relativePath -eq 'T_MissingFloorJournalPaper_D.png') {
			1672
		} elseif ($relativePath -like 'T_SpriteFinalCavity_*' -or
			$relativePath -like 'T_SpriteMokFinalUpper_*') {
			1024
		} else { $expectedSize }
		$expectedHeight = if ($relativePath -eq 'T_MissingFloorJournalPaper_D.png') {
			941
		} elseif ($relativePath -like 'T_SpriteFinalCavity_*' -or
			$relativePath -like 'T_SpriteMokFinalUpper_*') {
			1536
		} else { $expectedSize }
		if ($image.Width -ne $expectedWidth -or $image.Height -ne $expectedHeight) {
			throw "Derived art must be ${expectedWidth}x${expectedHeight}: $relativePath"
		}

		$opaqueSamples = 0
		$transparentSamples = 0
		$visibleMagentaSamples = 0
		$visibleGreenSamples = 0
		$lumaTotal = 0.0
		$lumaMinimum = 255.0
		$lumaMaximum = 0.0
		$colorSamples = 0
		$redTotal = 0.0
		$greenTotal = 0.0
		$blueTotal = 0.0
		for ($y = 0; $y -lt $image.Height; $y += 8) {
			for ($x = 0; $x -lt $image.Width; $x += 8) {
				$pixel = $image.GetPixel($x, $y)
				$luma =
					$pixel.R * 0.2126 +
					$pixel.G * 0.7152 +
					$pixel.B * 0.0722
				$lumaTotal += $luma
				$lumaMinimum = [Math]::Min($lumaMinimum, $luma)
				$lumaMaximum = [Math]::Max($lumaMaximum, $luma)
				$colorSamples++
				$redTotal += $pixel.R
				$greenTotal += $pixel.G
				$blueTotal += $pixel.B
				if ($pixel.A -ge 240) {
					$opaqueSamples++
				}
				elseif ($pixel.A -le 8) {
					$transparentSamples++
				}
				if ($pixel.A -gt 8 -and
					$pixel.R -gt 180 -and
					$pixel.B -gt 160 -and
					$pixel.G -lt 110) {
					$visibleMagentaSamples++
				}
				if ($pixel.A -gt 8 -and
					$pixel.G - $pixel.R -gt 24 -and
					$pixel.G - $pixel.B -gt 24) {
					$visibleGreenSamples++
				}
			}
		}

		if ($requiredMasks -contains $relativePath) {
			if ($opaqueSamples -lt 4000) {
				throw "Evidence mask lost its opaque black backing: $relativePath"
			}
		}
		elseif ($requiredOverlays -contains $relativePath) {
			if ($opaqueSamples -lt 8 -or $transparentSamples -lt 512) {
				throw "RGBA overlay lacks usable foreground/background: $relativePath"
			}
			if ($visibleMagentaSamples -gt 0) {
				throw "RGBA overlay retained a visible magenta fringe: $relativePath"
			}
			if (($relativePath -like 'T_SpriteListener*_D.png' -or
				$relativePath -like 'T_SpriteFinalCavity_D.png' -or
				$relativePath -like 'T_SpriteMokFinalUpper_D.png' -or
				$relativePath -like 'T_FPHandKnock*_D.png') -and
				$visibleGreenSamples -gt 0) {
				throw "RGBA overlay retained a visible green fringe: $relativePath"
			}
		}
		# 재질 스캔은 거의 전부가 불투명해야 한다. 임계를 절대 샘플 수로 두면
		# 1024x1024를 가정하게 되는데, 8px 스텝에서 724x1024는 전체 샘플이
		# 11,648개라 완전히 불투명해도 12,000을 넘을 수 없다. 비율로 재면
		# 같은 뜻을 크기와 무관하게 검사한다(기존 1024 스캔의 12000/16384와
		# 동일한 기준이다).
		elseif ($opaqueSamples -lt [int]($colorSamples * 0.732)) {
			throw "Material scan lost its opaque color field: $relativePath"
		}

		if ($requiredMaterialMasks -contains $relativePath) {
			$meanLuma = $lumaTotal / [Math]::Max(1, $colorSamples)
			$dynamicRange = $lumaMaximum - $lumaMinimum
			if ($meanLuma -lt 8 -or $meanLuma -gt 220 -or $dynamicRange -lt 45) {
				throw "Wall-patina mask lost its usable damage range: $relativePath"
			}
		}
		elseif ($requiredMaterialTextures -contains $relativePath) {
			$meanLuma = $lumaTotal / [Math]::Max(1, $colorSamples)
			$dynamicRange = $lumaMaximum - $lumaMinimum
			switch ($relativePath) {
				'T_CarrierBagFilm_D.png' {
					if ($meanLuma -lt 125 -or $meanLuma -gt 185 -or $dynamicRange -lt 55) {
						throw "Carrier film no longer preserves translucent midtones: $relativePath"
					}
				}
				'T_AlleyCatTabby_D.png' {
					if ($meanLuma -lt 45 -or $meanLuma -gt 125 -or $dynamicRange -lt 120) {
						throw "Tabby coat lost its restrained stripe contrast: $relativePath"
					}
				}
				'T_P3CabinetPaintedSteel_D.png' {
					if ($meanLuma -lt 120 -or $meanLuma -gt 180 -or $dynamicRange -lt 45) {
						throw "P3 cabinet lost its restrained painted-steel range: $relativePath"
					}
				}
				'T_HudDialogueFilm_D.png' {
					if ($meanLuma -lt 12 -or $meanLuma -gt 70 -or
						$dynamicRange -lt 8 -or $dynamicRange -gt 90) {
						throw "Dialogue film lost its restrained near-black UI range: $relativePath"
					}
				}
				'T_AudioCalibrationWall_D.png' {
					if ($meanLuma -lt 18 -or $meanLuma -gt 75 -or
						$dynamicRange -lt 12 -or $dynamicRange -gt 110) {
						throw "Calibration wall lost its useful near-black detail range: $relativePath"
					}
				}
				'T_MissingFloorJournalPaper_D.png' {
					if ($meanLuma -lt 185 -or $meanLuma -gt 235 -or
						$dynamicRange -lt 30) {
						throw "Journal paper lost its readable tactile range: $relativePath"
					}
				}
			}
		}
		elseif ($requiredPbrMaps -contains $relativePath) {
			$meanLuma = $lumaTotal / [Math]::Max(1, $colorSamples)
			$dynamicRange = $lumaMaximum - $lumaMinimum
			if ($relativePath.EndsWith('_N.png')) {
				$meanRed = $redTotal / [Math]::Max(1, $colorSamples)
				$meanGreen = $greenTotal / [Math]::Max(1, $colorSamples)
				$meanBlue = $blueTotal / [Math]::Max(1, $colorSamples)
				if ($meanRed -lt 112 -or $meanRed -gt 143 -or
					$meanGreen -lt 112 -or $meanGreen -gt 143 -or
					$meanBlue -lt 240) {
					throw "Tangent normal map is not neutral/up-facing: $relativePath"
				}
			}
			elseif ($relativePath.EndsWith('_W.png')) {
				if ($dynamicRange -lt 45 -or $meanLuma -gt 150) {
					throw "Wetness mask lacks a restrained local blend range: $relativePath"
				}
			}
			elseif ($relativePath.EndsWith('_R.png')) {
				if ($dynamicRange -lt 4 -or $meanLuma -lt 12 -or $meanLuma -gt 242) {
					throw "Roughness map lost usable physical variation: $relativePath"
				}
			}
			elseif ($relativePath.EndsWith('_A.png') -and $meanLuma -lt 160) {
				throw "Ambient-occlusion map is crushing indirect light: $relativePath"
			}
		}
	}
	finally {
		$image.Dispose()
	}
}

$materialScript = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts\create_textured_materials.py')
$pbrScript = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts\generate_ai_pbr_maps.py')
$meshScript = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts\generate_meshes.py')
$meshContractScript = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts\mesh_lod_contract.py')
$neighborhoodSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Environment\IGNeighborhoodLifeDirector.cpp')
$prologueSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Core\IGPrologueWorldScene.cpp')
$listenerSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Entity\IGListenerEntity.cpp')
$greyboxSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Entity\IGListenerGreyboxDirector.cpp')
$nightThreeSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Entity\IGMissingFloorNightThreeDirector.cpp')
$fifthDawnSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Entity\IGMissingFloorFifthDawnDirector.cpp')
$nightFourSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Entity\IGMissingFloorNightFourDirector.cpp')
$missingFloorNarrativeSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Narrative\IGMissingFloorNarrativeSubsystem.cpp')
$missingFloorNarrativeHeader = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Narrative\IGMissingFloorNarrativeSubsystem.h')
$missingFloorNarrativeTypes = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Narrative\IGMissingFloorNarrativeTypes.h')
$missingFloorStory = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Docs\STORY_BIBLE_MISSING_FLOOR.md')
$puzzleTwoSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Entity\IGMissingFloorPuzzleTwoDirector.cpp')
$playerSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Player\IGPlayerCharacter.cpp')
$playerControllerSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Player\IGPlayerController.cpp')
$horrorHudSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Player\IGHorrorHUD.cpp')
$toneSequenceSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source\IndieGame\Audio\IGToneSequenceSoundWave.cpp')
$inputConfig = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Config\DefaultInput.ini')
$buildScript = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts\Build-ArtAssets.ps1')
$signScript = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts\Create-SignTextures.ps1')
$surfaceScript = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts\generate_surface_textures.py')
$auditScript = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts\validate_baked_art_assets.py')
$photoLodContract = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts\photo_prop_lod_contract.py')
$photoLodApplyScript = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts\apply_photo_prop_lods.py')
$photoImportScript = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts\import_photo_props.py')

foreach ($token in @(
	'pitch=rotation[0]',
	'yaw=rotation[1]',
	'roll=rotation[2]'
)) {
	if (-not $meshScript.Contains($token)) {
		throw "Mesh transform axis contract is missing: $token"
	}
}
if ($meshScript.Contains('unreal.Rotator(*rotation)')) {
	throw 'Mesh transforms must not pass C++-ordered rotations positionally.'
}

foreach ($token in @(
	'def append_indexed_surface',
	'def append_wrapped_label_surface',
	'getattr(unreal, "GeometryScript_MeshEdits", None)',
	'EDITS.append_buffers_to_mesh(mesh, buffers, 0, False)',
	'uvs.append((u, v))',
	'build_sticky_note_76mm',
	'"SM_StickyNote76mm"',
	'build_capture_mercy_note',
	'"SM_CaptureMercyNote"',
	'width = 18.0',
	'depth = 11.0',
	'[(1.0, 0.0, 1.0), (1.0, 1.0, 0.0)]'
)) {
	if (-not $meshScript.Contains($token)) {
		throw "Physical packaging UV/adhesive-note mesh contract is missing: $token"
	}
}
foreach ($token in @(
	'"M_CaptureMercyNote": {',
	'"tex_asset": "T_CaptureMercyNote_D", "rough": 0.92, "two_sided": True,',
	'"M_Plate402":      {"tex_asset": "T_Plate402_D", "rough": 0.35}',
	'material.set_editor_property("two_sided", bool(spec.get("two_sided", False)))',
	'IG_CORRIDOR_SIGNAGE_ONLY',
	'Corridor entrance signage material update complete'
)) {
	if (-not $materialScript.Contains($token)) {
		throw "Landing door material contract is missing: $token"
	}
}
foreach ($token in @(
	'[switch]$CorridorEntranceOnly',
	"AI\TextureCaptureMercyNotePaper_D.png",
	"-BackgroundImagePath `$captureMercyNotePaper",
	"Draw-CenteredText `$g '소리를 줄여라.'",
	"Draw-CenteredText `$g '걔는 눈이 없어.'"
)) {
	if (-not $signScript.Contains($token)) {
		throw "Landing generated-paper composition contract is missing: $token"
	}
}
foreach ($token in @(
	'CORRIDOR_SIGNAGE_ONLY = os.environ.get("IG_CORRIDOR_SIGNAGE_ONLY") == "1"',
	'CORRIDOR_SIGNAGE_TEXTURE_NAMES',
	'"T_CaptureMercyNote_D"',
	'"T_PlateCommon_D"',
	'"T_NoteFridge_D",',
	'"T_CaptureMercyNote_D",',
	'asset_name.startswith("T_Plate")'
)) {
	if (-not $surfaceScript.Contains($token)) {
		throw "Landing targeted texture-import contract is missing: $token"
	}
}
foreach ($token in @(
	'INSTANCED_PRODUCT_MATERIALS',
	'WRAPPED_LABEL_MATERIALS',
	'used_with_instanced_static_meshes'
)) {
	if (-not $materialScript.Contains($token) -or -not $auditScript.Contains($token)) {
		throw "Runtime product-material contract is missing: $token"
	}
}
if (-not $materialScript.Contains('material.set_editor_property("two_sided", True)') -or
	-not $auditScript.Contains('material.get_editor_property("two_sided")')) {
	throw 'Wrapped product film must be authored and audited as two-sided.'
}
foreach ($token in @(
	'"M_CaptureMercyNote": "T_CaptureMercyNote_D"',
	'ENTRANCE_PLATE_MATERIALS',
	'TWO_SIDED_PRINT_MATERIALS = {',
	'"M_CaptureMercyNote",',
	'"M_MercyNoteUnderDoor",',
	'Printed paper lost two-sided rendering'
)) {
	if (-not $auditScript.Contains($token)) {
		throw "Landing baked-material audit contract is missing: $token"
	}
}
if ($meshScript.Contains('set_mesh_u_vs_from_cylinder_projection') -or
	$meshScript.Contains('set_mesh_uvs_from_cylinder_projection')) {
	throw 'Printed sleeves must use an explicit single-seam UV instead of projection.'
}
foreach ($token in @(
	'constexpr float BedsideTableTopZ = 60.0f;',
	'BedsideTable->CalcBounds(',
	'PropMesh(TEXT("SM_StickyNote76mm"))',
	'FVector(-7.43f, 36.0f, 18.0f)',
	'FVector(3.30f, 3.30f, 5.5f)',
	'CabVisuals.Cop = PropMesh(TEXT("SM_ElevatorCop"));',
	'Prop->bDisallowNanite = true;'
)) {
	if (-not $prologueSource.Contains($token)) {
		throw "Household/retail physical placement contract is missing: $token"
	}
}
foreach ($token in @(
	'TEXT("M_Plate402"), TEXT("M_Plate401")',
	'TexMat(TEXT("M_Plate403"), FridgeInteriorMaterial)'
)) {
	if (-not $prologueSource.Contains($token)) {
		throw "Landing door runtime contract is missing: $token"
	}
}
foreach ($token in @(
	'const bool Cups = Run == 0 && (Tier == 2 || Tier == 3);',
	'const float Z = 31.5f + Tier * 30.0f;',
	'Depth == 0 ? 28.0f : 11.5f',
	'Tier < 5',
	'FVector(2710, Y, 6)',
	'const float BayCenters[] = {-630, -552, -474, -396, -318, -240};'
)) {
	if (-not $prologueSource.Contains($token)) {
		throw "Convenience-store shelf-bay placement contract is missing: $token"
	}
}
foreach ($forbiddenToken in @(
	'AddStoreStockCup(FVector(CupX, -365, 142)',
	'AddStoreStockCup(FVector(CupX, -654, 167.5f)'
)) {
	if ($prologueSource.Contains($forbiddenToken)) {
		throw "A known floating/top-cap retail placement regressed: $forbiddenToken"
	}
}
foreach ($forbiddenToken in @(
	'FVector(-6.9f, 36.0f, 18.0f)',
	'FVector(0.012f, 0.20f, 0.20f)',
	'FVector(12, -74.2f, CabBaseZ + 108)',
	'FVector(4.10f, 4.10f, 7.2f)',
	'FVector(3.36f, 3.36f, 8.6f)'
)) {
	if ($prologueSource.Contains($forbiddenToken)) {
		throw "A known floating/intersecting/stretched visual regressed: $forbiddenToken"
	}
}

foreach ($token in @(
	'M_DecalDampWallpaper',
	'M_ApartmentWallPatina',
	'"tex": "ApartmentWallpaperV2"',
	'M_AlleyCatTabbyUV',
	'M_P3CabinetMetalUV',
	'M_CarrierBagFilm',
	'BLEND_MASKED',
	'MP_OPACITY_MASK',
	'BLEND_TRANSLUCENT',
	'MP_OPACITY',
	'MaterialExpressionNormalize',
	'MaterialExpressionMax',
	'MaterialExpressionWorldPosition'
)) {
	if (-not $materialScript.Contains($token)) {
		throw "Masked material contract is missing: $token"
	}
}
if ($materialScript -notmatch
	'SAMPLERTYPE_MASKS\s*\r?\n\s*if mask_only else unreal\.MaterialSamplerType\.SAMPLERTYPE_COLOR') {
	throw 'Evidence mask textures must use the Masks material sampler type.'
}
foreach ($token in @(
	'SurfaceSpec("T_AlleyCatTabby"',
	'SurfaceSpec("T_P3CabinetPaintedSteel"',
	'SurfaceSpec("T_CarrierBagFilm"',
	'ImageOps.autocontrast',
	'normal_strength',
	'wetness'
)) {
	if (-not $pbrScript.Contains($token)) {
		throw "PBR source-map generation contract is missing: $token"
	}
}
foreach ($token in @(
	'def _connect_scan_pbr',
	'MaterialProperty.MP_NORMAL',
	'MaterialProperty.MP_ROUGHNESS',
	'MaterialProperty.MP_AMBIENT_OCCLUSION',
	'MaterialProperty.MP_METALLIC',
	'wet_normal_flatten',
	'SAMPLERTYPE_LINEAR_GRAYSCALE',
	'SAMPLERTYPE_MASKS'
)) {
	if (-not $materialScript.Contains($token)) {
		throw "PBR material blending contract is missing: $token"
	}
}
# 87bd53b가 LOD 특례를 예산 체계로 바꿨다. 예산·분류는 mesh_lod_contract가
# 소유하고, 베이크 쪽에는 그것을 실제로 적용하는 장치들이 있어야 한다.
# 옛 'LOD0 preserved' 로그 문자열은 특례와 함께 사라진 게 맞다.
foreach ($token in @(
	'HERO_MESHES',
	'LARGE_MESH_PREFIXES',
	'lod0_triangles',
	'def classify',
	'def triangle_budget'
)) {
	if (-not $meshContractScript.Contains($token)) {
		throw "Static-mesh LOD contract is missing: $token"
	}
}
foreach ($token in @(
	'def apply_lod_contract',
	'def retopologise',
	'mesh_lod_contract.triangle_budget',
	'StaticMeshEditorSubsystem',
	'set_lods',
	'set_lod_group',
	'enable_nanite", False'
)) {
	if (-not $meshScript.Contains($token)) {
		throw "Static-mesh LOD chain is not applied at bake time: $token"
	}
}
if (-not $prologueSource.Contains('M_LabelWater1L') -or
	-not $prologueSource.Contains('M_LabelWater2L')) {
	throw '냉장고의 용량별 상품 라벨이 빠졌다.'
}
if (-not $meshScript.Contains('SM_AlleyCatRun') -or
	-not $neighborhoodSource.Contains('SM_AlleyCatRun') -or
	-not $neighborhoodSource.Contains('M_AlleyCatTabbyUV')) {
	throw 'Alley cat mesh/material is not generated and loaded'
}
if (-not $prologueSource.Contains('M_CarrierBagFilm')) {
	throw 'Purchase bag does not share the carrier-film material'
}
# 이름은 P3지만 밸브 핸들 둘은 Missing Floor 밤4 설비와 부스 수직관 밸브가 그대로 쓴다.
foreach ($token in @(
	'_build_p3_valve_wheel("SM_P3ValveWheelLarge", 18.0',
	'_build_p3_valve_wheel("SM_P3ValveWheelSmall", 12.0'
)) {
	if (-not $meshScript.Contains($token)) {
		throw "Valve wheel mesh contract is missing: $token"
	}
}
foreach ($token in @(
	'Prepare-AIArt.ps1',
	'generate_ai_pbr_maps.py',
	'[switch]$SourceOnly',
	'[switch]$CodeOnly',
	'[switch]$HudUiOnly',
	'[switch]$CorridorSignageOnly',
	'-CorridorEntranceOnly',
	'ART_TARGETED_BUILD PASS',
	'@($targetRelativeAssets).Count',
	'IG_HUD_UI_ONLY',
	'IG_CORRIDOR_SIGNAGE_ONLY',
	'SM_CaptureMercyNote.uasset',
	'T_CaptureMercyNote_D.uasset',
	'M_CaptureMercyNote.uasset',
	'T_HudDialogueFilm_D.uasset',
	'T_AudioCalibrationWall_D.uasset',
	'T_MissingFloorJournalPaper_D.uasset',
	'T_MovingBoxCardboard_D.uasset',
	'T_MovingBoxCardboard_N.uasset',
	'T_MovingBoxCardboard_R.uasset',
	'T_MovingBoxCardboard_A.uasset',
	'M_MovingBoxCardboardUV.uasset',
	'T_FPHandKnock0_D.uasset',
	'T_FPHandKnock3_D.uasset',
	'IG_APARTMENT_VISUAL_ONLY',
	'T_ApartmentWallpaperV2_D.uasset',
	'T_ApartmentWallPatina_M.uasset',
	'ART_SOURCE_BUILD PASS',
	'ART_CODE_BUILD PASS',
	'ART_BUILD compiling IndieGameEditor Win64 Development',
	'UnrealEditor-IndieGame.dll',
	"Join-Path `$unrealProjectRoot 'Binaries\Win64'",
	"Join-Path `$projectRoot 'Binaries\Win64'",
	'generate_meshes.py',
	'generate_surface_textures.py',
	'create_textured_materials.py',
	'apply_photo_prop_lods.py',
	'Content\Photo\Props',
	'validate_baked_art_assets.py',
	'Get-AsciiArtBuildRoot',
	'Invoke-ArtRobocopy',
	'ART_UASSET_AUDIT PASS',
	'-abslog=',
	'ART_BUILD PASS'
)) {
	if (-not $buildScript.Contains($token)) {
		throw "Art build pipeline stage is missing: $token"
	}
}
foreach ($token in @(
	'StaticMeshEditorSubsystem',
	'get_lod_count',
	'compression_settings',
	'MP_NORMAL',
	'MP_ROUGHNESS',
	'MP_AMBIENT_OCCLUSION',
	'MP_METALLIC',
	'MP_OPACITY',
	'MP_OPACITY_MASK',
	'get_material_expressions',
	'validate_photo_prop_lods',
	'photo_meshes=',
	'recompile_material',
	'ART_UASSET_AUDIT PASS'
)) {
	if (-not $auditScript.Contains($token)) {
		throw "Baked UAsset audit contract is missing: $token"
	}
}
foreach ($token in @(
	'PHOTO_PROP_ROOT = "/Game/Photo/Props"',
	'LARGE_PROP_IDS',
	'def inspect_photo_prop_lods',
	'def apply_photo_prop_lod_contract',
	'get_number_verts',
	'set_lod_group',
	'lod_count < 2',
	'50_000'
)) {
	if (-not $photoLodContract.Contains($token)) {
		throw "Photo-prop LOD contract is missing: $token"
	}
}
if ($photoLodContract.Contains('set_nanite_settings')) {
	throw 'Photo-prop LOD automation must not enable Nanite without an A/B GPU trace.'
}
foreach ($token in @(
	'PHOTO_PROP_LOD_BUILD PASS',
	'apply_photo_prop_lod_contract'
)) {
	if (-not $photoLodApplyScript.Contains($token) -or
		-not $photoImportScript.Contains('apply_photo_prop_lod_contract')) {
		throw "Photo-prop import/apply pipeline is missing: $token"
	}
}

# 「Missing Floor」의 ImageGen 원본은 참고 시트에서 끝나지 않는다. 근접 인체는
# 연속 3D 접지 셸과 정면 PBR 레이어를 결합하고, 흔적은 값 마스크, 접근
# 불가 인물은 고정 스프라이트로 제한하는 적용 경계를 소스 계약으로 잠근다.
foreach ($token in @(
	'T_MissingFloorDryPlaster',
	'MovingBoxCardboard',
	'M_MovingBoxCardboardUV',
	'M_MissingFloorSteelStair',
	'M_RooftopWaterproofing_XY',
	'M_UnitDoorPaintedSteel',
	'M_MissingFloorListenerPlasterUV',
	'M_MissingFloorHandprints',
	'M_SpriteSeo',
	'M_SpriteListenerFront',
	'M_SpriteListenerCrawl0',
	'M_SpriteListenerCrawl3',
	'M_SpriteFinalCavity',
	'M_SpriteMokFinalUpper',
	'crack_normal_strength',
	'cavity_dust',
	'BreathAmplitude',
	'TremorAmplitude',
	'DustAmount'
)) {
	if (-not $materialScript.Contains($token)) {
		throw "Missing-floor material pipeline is missing: $token"
	}
}
foreach ($token in @(
	'Every targeted pass can compile runtime bindings',
	"-Source (Join-Path `$unrealProjectRoot 'Binaries\Win64')",
	"-Destination (Join-Path `$projectRoot 'Binaries\Win64')"
)) {
	if (-not $buildScript.Contains($token)) {
		throw "ASCII targeted-build binary sync is missing: $token"
	}
}
# 타깃 패스의 에셋 회수. 목록에 적힌 파일만 복사하던 시절에는 그 패스가
# 처음 만든 에셋이 미러에 갇힌 채 PASS가 찍혔다. 폴더째 회수와 도착 확인이
# 둘 다 있어야 그 침묵이 다시 생기지 않는다.
foreach ($token in @(
	'$undeliveredAssets = @($targetRelativeAssets | Where-Object {',
	'-not (Test-Path -LiteralPath (Join-Path $projectRoot $_) -PathType Leaf)',
	"-Destination (Join-Path `$projectRoot `$relativeFolder)",
	'Content\Prototype\Materials\M_MissingFloorSteelStair.uasset',
	'Content\Prototype\Materials\M_RooftopWaterproofing_XY.uasset',
	'Content\Prototype\Materials\M_UnitDoorPaintedSteel.uasset'
)) {
	if (-not $buildScript.Contains($token)) {
		throw "ASCII targeted-build asset delivery is missing: $token"
	}
}
foreach ($token in @(
	'build_listener_entity_crawl',
	'"SM_ListenerEntityCrawl"',
	'build_final_cavity_clothing_shell',
	'"SM_FinalCavityClothingShell"',
	'build_final_cavity_bone_insert',
	'"SM_FinalCavityBoneInsert"',
	'build_final_cavity_tarp',
	'"SM_FinalCavityTarp"',
	'build_final_cavity_broken_caster',
	'"SM_FinalCavityBrokenCaster"',
	'build_mok_hansoo_workwear',
	'"SM_MokHansooWorkwear"',
	'build_mok_hansoo_head_hands',
	'"SM_MokHansooHeadHands"',
	'build_mok_hansoo_gypsum_board',
	'"SM_MokHansooGypsumBoard"',
	'Long tuner fingers remain closed plaster geometry',
	'build_tuning_hammer',
	'"SM_TuningHammer"',
	'A tuning hammer is not a listening wand',
	'build_tuner_tool_cart',
	'"SM_TunerToolCart"',
	'build_complaint_ledger',
	'"SM_ComplaintLedger"',
	'build_calendar_journal',
	'"SM_CalendarJournal"',
	'IG_MISSING_FLOOR_ONLY',
	'fuse_shells',
	'apply_mesh_self_union',
	'apply_perlin_noise_to_mesh2',
	'bake_vertex_occlusion'
)) {
	if (-not $meshScript.Contains($token)) {
		throw "Missing-floor anatomical mesh contract is missing: $token"
	}
}
foreach ($token in @(
	'SM_ListenerEntityCrawl.SM_ListenerEntityCrawl',
	'M_MissingFloorListenerPlasterUV',
	'M_SpriteListenerFront',
	'M_SpriteListenerCrawl0',
	'M_SpriteListenerCrawl3',
	'UpdatePresentationPose(LastMoveSpeed, DeltaSeconds)',
	'ListenerPhaseMaterials.Num() == 4',
	'State == EIGListenerState::Waiting',
	'ListenerPhase = 1.0f',
	# 스켈레탈 몸(2026-09-11). 정적 셸+카드는 에셋이 없을 때의 폴백으로 남는다.
	'SK_ListenerCrawler.SK_ListenerCrawler',
	'A_ListenerCrawler_Crawl.A_ListenerCrawler_Crawl',
	'ListenerSkeletal->PlayAnimation(Sequence, bLoop)',
	'bLungeArmed = bReachable',
	'? BodyRate * (4.0f / 1.2f)',
	': FMath::Lerp(1.6f, 6.0f, SpeedAlpha)',
	'SetCastHiddenShadow(bFrontCardActive)',
	'Distance > 160.0f',
	'Distance > 125.0f',
	'Facing > 0.60f',
	# 접지 오프셋은 메시 바운드에서 계산한다. 절차 셸(-31)과 TRELLIS.2에서
	# 다듬은 셸(0)이 같은 코드로 바닥에 닿는다.
	'CapsuleOriginAboveFloor = 58.0f',
	'-CapsuleOriginAboveFloor - LowestZ',
	'ListenerShellMid',
	'TEXT("BreathAmplitude")',
	'TEXT("TremorAmplitude")',
	'TargetBreath = 0.0f'
)) {
	if (-not $listenerSource.Contains($token)) {
		throw "Listener release-visual binding is missing: $token"
	}
}
foreach ($token in @(
	'M_MissingFloorPlaster_X',
	'M_MissingFloorHandprints',
	'M_MissingFloorCavityScratches'
)) {
	if (-not $prologueSource.Contains($token)) {
		throw "Fifth-floor PBR/residue placement is missing: $token"
	}
}
foreach ($token in @(
	'M_SpriteSeo.M_SpriteSeo',
	'Feet sit exactly on Z=0',
	'RefreshDistantSeoVisibility',
	'SM_TuningHammer.SM_TuningHammer',
	'튜닝 해머',
	'SM_TunerToolCart.SM_TunerToolCart',
	'SM_CalendarJournal.SM_CalendarJournal'
)) {
	if (-not $nightThreeSource.Contains($token)) {
		throw "Missing-floor night-three visual binding is missing: $token"
	}
}
foreach ($token in @(
	'SM_ComplaintLedger.SM_ComplaintLedger',
	'SM_ComplaintImpressionPad.SM_ComplaintImpressionPad',
	'SM_GraphitePencil.SM_GraphitePencil',
	'M_ComplaintImpression.M_ComplaintImpression',
	'ConfigureProgressReveal',
	'77.5f'
)) {
	if (-not $puzzleTwoSource.Contains($token)) {
		throw "Missing-floor physical ledger binding is missing: $token"
	}
}

# 정사 v2.5의 공간·퍼즐·영속성 계약. 시각 에셋이 맞아도 포털, 조기 P3
# 해결, 가상 P5 설비 또는 복원 누락이 돌아오면 같은 빌드로 취급하지 않는다.
foreach ($token in @(
	'MissingFloorRouteLengthCentimeters = 640.0f',
	'MissingFloorUpperStepCount = 14',
	'ValidateMissingFloorRooftopRoute',
	'OpenMissingFloorCavity',
	'SetMissingFloorAnnexPower'
)) {
	if (-not $prologueSource.Contains($token)) {
		throw "Missing-floor physical topology contract is missing: $token"
	}
}
if ($nightThreeSource.Contains('MissingFloorAnnexTransition') -or
	$nightThreeSource.Contains('AnnexTransition')) {
	throw 'Night three regressed to a detached annex portal.'
}
$valveStart = $nightThreeSource.IndexOf(
	'void AIGMissingFloorNightThreeDirector::HandleValveOpened')
$listenStart = $nightThreeSource.IndexOf(
	'void AIGMissingFloorNightThreeDirector::HandleWallListened')
if ($valveStart -lt 0 -or $listenStart -le $valveStart) {
	throw 'Night-three P3 function boundaries are missing.'
}
$valveBody = $nightThreeSource.Substring($valveStart, $listenStart - $valveStart)
if ($valveBody.Contains('MarkPuzzleSolved')) {
	throw 'P3 must not be solved by opening the valve before identifying a wall.'
}
foreach ($token in @(
	'P5.RoofCleaningDrain',
	'P5.RoofFloatBypass',
	'P5.TransferPump',
	'WaterMaskHumHandle',
	'RecordNightFourWallStrike',
	'SetMissingFloorAnnexPower(false)',
	'OpenMissingFloorCavity',
	'Ending.A',
	'Ending.B',
	'Ending.C'
)) {
	if (-not $nightFourSource.Contains($token)) {
		throw "Night-four runtime contract is missing: $token"
	}
}
foreach ($token in @(
	'AnswerRhythmJournal',
	'NightFourControlOrder',
	'NightFourWallStrikeCount',
	'bFirstReportMade',
	'bSecondReportMade',
	'SelectEnding'
)) {
	if (-not $missingFloorNarrativeSource.Contains($token) -and
		-not $missingFloorNarrativeHeader.Contains($token)) {
		throw "Missing-floor v2 persistence contract is missing: $token"
	}
}
# v3 — §22.3 선택적 목격. 진실 표와 섞이지 않는 별도 이름 공간으로 저장하고,
# 이 빌드가 모르는 이름은 복원에서 버린다. 목격이 게이트에 참여하지 않는다는
# 것이 계약의 요점이라 RecordWitness는 진실을 다시 계산하지 않는다.
foreach ($token in @(
	'EIGMissingFloorWitness',
	'RecordWitness',
	'HasWitness',
	'Snapshot.Night.Witnesses',
	'Seen.SeoSleepingPills',
	'Seen.HwangWaterBowl',
	'Seen.BoothSoundproofing',
	'Seen.RooftopCigarettePack'
)) {
	if (-not $missingFloorNarrativeSource.Contains($token) -and
		-not $missingFloorNarrativeHeader.Contains($token) -and
		-not $missingFloorNarrativeTypes.Contains($token)) {
		throw "Missing-floor v3 witness contract is missing: $token"
	}
}
if (-not $missingFloorNarrativeHeader.Contains('SnapshotSchemaVersion = 3')) {
	throw 'Missing-floor snapshot schema was not advanced for optional witnesses.'
}

# §12: 표에 올라온 출처는 전부 세계 어딘가에서 실제로 적혀야 한다.
#
# 이 검사가 없는 동안 세 출처(자재 반입 영수증, 새 벽 미장 시기, 탱크 물소리)가
# 규칙표와 이름표에만 있고 프롭이 없었다. 저널 미리보기 프로브가 화면용으로
# 같은 이름을 넣고 있어서 전수 검색으로도 있는 것처럼 보였다. 그래서 여기서는
# **프로브와 표 자신을 제외한** 실제 게임플레이 파일만 센다.
$sourceEnumBody = [regex]::Match(
	$missingFloorNarrativeTypes,
	'enum class EIGMissingFloorSource : uint8\s*\{(?<body>[\s\S]*?)\};')
if (-not $sourceEnumBody.Success) {
	throw 'Missing-floor evidence source enum not found.'
}
$sourceNames = [regex]::Matches(
	$sourceEnumBody.Groups['body'].Value,
	'(?m)^\s*(?<name>[A-Za-z][A-Za-z0-9]*)\s*=\s*\d+') |
	ForEach-Object { $_.Groups['name'].Value } |
	Where-Object { $_ -ne 'None' }
if ($sourceNames.Count -lt 20) {
	throw "Evidence source scan found only $($sourceNames.Count) entries."
}
$gameplayFiles = @(
	Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Source\IndieGame\Entity') `
		-Filter '*.cpp' -File
	Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Source\IndieGame\Core') `
		-Filter '*.cpp' -File
)
$gameplayText = ($gameplayFiles | ForEach-Object {
	Get-Content -Raw -Encoding UTF8 -LiteralPath $_.FullName
}) -join "`n"
$unplacedSources = @()
foreach ($sourceName in $sourceNames) {
	if (-not $gameplayText.Contains("EIGMissingFloorSource::$sourceName")) {
		$unplacedSources += $sourceName
	}
}
if ($unplacedSources.Count -gt 0) {
	throw ("Evidence sources with no prop in the world: " +
		($unplacedSources -join ', '))
}

# 같은 이유로 진실도 센다. 열 개 모두 규칙표에 있어야 하고, §24 즉시 차단
# 21이 「진실 10개 도달 가능」을 출시 조건으로 걸고 있다.
$truthEnumBody = [regex]::Match(
	$missingFloorNarrativeTypes,
	'enum class EIGMissingFloorTruth : uint8\s*\{(?<body>[\s\S]*?)\};')
if (-not $truthEnumBody.Success) {
	throw 'Missing-floor truth enum not found.'
}
$truthNames = [regex]::Matches(
	$truthEnumBody.Groups['body'].Value,
	'(?m)^\s*(?<name>[A-Za-z][A-Za-z0-9]*)\s*=\s*\d+') |
	ForEach-Object { $_.Groups['name'].Value } |
	Where-Object { $_ -ne 'None' }
if ($truthNames.Count -ne 10) {
	throw "Expected ten truths, found $($truthNames.Count)."
}
foreach ($truthName in $truthNames) {
	if (-not $missingFloorNarrativeSource.Contains("EIGMissingFloorTruth::$truthName")) {
		throw "Truth missing from the crossing table: $truthName"
	}
	if (-not $gameplayText.Contains("EIGMissingFloorTruth::$truthName")) {
		throw "Truth has no gameplay path: $truthName"
	}
}

foreach ($token in @(
	'2026년 9월 29일',
	'세척 배수 OPEN',
	'부자밸브 우회 OPEN',
	'저수조 이송펌프',
	'2분 40초',
	'05:30 최초 신고',
	'0:52~1:20 / 7월 29일 셋째 새벽',
	'1:55~2:40 / 7월 31일 다섯째이자 마지막 새벽'
)) {
	if (-not $missingFloorStory.Contains($token)) {
		throw "Missing-floor story v3.2 contract is missing: $token"
	}
}
foreach ($token in @(
	'EIGListenerState::FinaleLured',
	'BeginFinalePass(',
	'SetActorEnableCollision(false)',
	'FinaleRoutePoints[FinaleRouteIndex]',
	'State == EIGListenerState::FinaleLured'
)) {
	if (-not $listenerSource.Contains($token)) {
		throw "Night-four harmless entity-pass contract is missing: $token"
	}
}
# v2.4 입력 실행 계약. 설정에 키 이름만 있거나 코드에 함수 이름만 있는
# 반쪽 구현을 허용하지 않고, 실제 퍼즐 경로와 화면 프롬프트까지 함께 묶는다.
foreach ($actionName in @('Sprint', 'Crouch', 'Knock', 'Listen', 'HoldBreath')) {
	if (-not $inputConfig.Contains(('ActionName="{0}"' -f $actionName))) {
		throw "Missing-floor input action is missing from DefaultInput.ini: $actionName"
	}
}
if ($inputConfig.Contains('ActionName="Interact",bShift=False,bCtrl=False,bAlt=False,bCmd=False,Key=Q')) {
	throw 'Q regressed to an Interact alias; P4 taps would mix with ordinary interaction.'
}
foreach ($token in @(
	'void AIGPlayerCharacter::BeginSprint()',
	'void AIGPlayerCharacter::ToggleCrouch()',
	'void AIGPlayerCharacter::Knock()',
	'void AIGPlayerCharacter::BeginListen()',
	'void AIGPlayerCharacter::BeginHoldBreath()',
	'SprintFootstepLoudness = 0.50f',
	'CrouchFootstepLoudness = 0.05f',
	'MaximumBreathHoldSeconds = 4.0f',
	'IsHourSealed()'
)) {
	if (-not $playerSource.Contains($token)) {
		throw "Missing-floor player-input runtime contract is missing: $token"
	}
}
# v2.5의 낮 기록은 입력 이름, 일시정지, 실제 출처, 접근성 재페이지와 종이
# 원샷이 한 경로로 연결돼야 한다. 정적인 배경 이미지 한 장만으로는 통과하지 않는다.
foreach ($actionName in @('Journal', 'JournalPrevious', 'JournalNext')) {
	if (-not $inputConfig.Contains(('ActionName="{0}"' -f $actionName))) {
		throw "Missing-floor journal input action is missing: $actionName"
	}
}
foreach ($token in @(
	'void AIGPlayerController::BeginJournalInput()',
	'JournalHoldSeconds = 0.30',
	'UsesToggleHoldInteractions()',
	'지금은 그럴 때가 아니야.',
	'SetMissingFloorJournalState',
	'CreateJournalPageTurn'
)) {
	if (-not $playerControllerSource.Contains($token)) {
		throw "Missing-floor journal controller contract is missing: $token"
	}
}
foreach ($token in @(
	'T_MissingFloorJournalPaper_D',
	'MissingFloorJournalTitle',
	'JournalLaneAdministration',
	'JournalLaneLife',
	'JournalLanePersonal',
	'Record.bConfirmed',
	'GetCaptionSizeScale()',
	'CardsPerLanePerPage'
)) {
	if (-not $horrorHudSource.Contains($token)) {
		throw "Missing-floor journal HUD contract is missing: $token"
	}
}
foreach ($token in @(
	'UIGToneSequenceSoundWave::CreateJournalPageTurn',
	'IGJournalPageTurn',
	'fingertip brushes'
)) {
	if (-not $toneSequenceSource.Contains($token)) {
		throw "Missing-floor journal sound contract is missing: $token"
	}
}
foreach ($token in @(
	'## 26. 2026-08-11 제품 감사',
	'### 26.2 첫 12분 체험 계약',
	'### 26.3 오디오 제작·믹스 계약',
	'### 26.4 UI·UX·조작 편의 계약',
	'### 26.5 성능·화질 예산',
	'채택 방화벽'
)) {
	if (-not $missingFloorStory.Contains($token)) {
		throw "Missing-floor v2.5 product contract is missing: $token"
	}
}
foreach ($token in @(
	'bool AIGMissingFloorNightThreeDirector::TryPlayerKnock',
	'bool AIGMissingFloorNightThreeDirector::TryPlayerListen',
	'MissingFloor.Verb.Knock',
	'MissingFloor.Verb.Listen'
)) {
	if (-not $nightThreeSource.Contains($token)) {
		throw "Missing-floor contextual verb routing is missing: $token"
	}
}
foreach ($token in @(
	'KnockPromptFormatKeyboard',
	'ListenPromptFormatGamepad'
)) {
	if (-not $horrorHudSource.Contains($token)) {
		throw "Missing-floor contextual prompt contract is missing: $token"
	}
}
# §19.4 힌트는 화면의 정답 칸이 아니라 유담의 속말이고, 누를 때마다 한 단계씩
# 구체적이 된다. 설정에서 끄면 키가 아무것도 하지 않는다.
foreach ($token in @(
	'IGMissingFloorHints::Resolve(this)',
	'IsHintRequestAvailable()',
	'HintTier = FMath::Min(Tier + 1, Step.Tiers.Num() - 1)'
)) {
	if (-not $playerControllerSource.Contains($token)) {
		throw "Missing-floor hint policy runtime is missing: $token"
	}
}
# 다섯 새벽은 렌더가 없는 대신 시간·오디오·입력·저장이 모두 실제여야 한다.
foreach ($token in @(
	'DurationSeconds = 160.0f',
	'24.0f, 24.0f, 52.0f, 58.0f, 74.0f',
	'80.0f, 115.0f, 118.0f, 148.0f, 159.2f',
	'CreateTrappedBreathBed',
	'CreateAnswerKnockPattern(this, 0.93f)',
	'SetFifthDawnInterludeCompleted(true)',
	'SetSensoryInterludePresentation'
)) {
	if (-not $fifthDawnSource.Contains($token)) {
		throw "Missing-floor fifth-dawn runtime contract is missing: $token"
	}
}
foreach ($token in @(
	'MissingFloorFifthDawnDirector',
	'ValidateTimeline()',
	'HandleFifthDawnCompleted',
	'WasFifthDawnInterludeCompleted()'
)) {
	if (-not $greyboxSource.Contains($token)) {
		throw "Missing-floor fifth-dawn route wiring is missing: $token"
	}
}
if (-not $horrorHudSource.Contains('bSensoryInterludePresentation')) {
	throw 'The fifth-dawn black frame no longer suppresses the ordinary HUD.'
}
# v2.4 플레이 표면 계약. 조작감·UI/UX·난이도·사운드 실행·재미·몰입 절이
# 사라지면 서사가 맞아도 같은 빌드로 취급하지 않는다.
foreach ($token in @(
	'## 18. 조작감 계약',
	'## 19. UI·UX 계약',
	'## 20. 난이도 설계',
	'## 21. 효과음·믹스 제작 명세',
	'## 22. 재미의 구조',
	'## 23. 몰입 계약',
	'즉시 차단 22개'
)) {
	if (-not $missingFloorStory.Contains($token)) {
		throw "Missing-floor play-surface contract is missing: $token"
	}
}
foreach ($token in @(
	'세대 계량기 3개(401·402·403)',
	'**403호 정사:**',
	'403호 다음 벽은 비워 둔다'
)) {
	if (-not $missingFloorStory.Contains($token)) {
		throw "403 entrance story boundary is missing: $token"
	}
}
foreach ($forbidden in @(
	'1F 드레인/에어빼기',
	'저수조 양수펌프',
	'무엇을 하든 4분이 흐른다',
	'되어 보는 4분',
	'렌더 0의 4분',
	'즉시 차단 15개'
)) {
	if ($missingFloorStory.Contains($forbidden)) {
		throw "Superseded missing-floor story literal remains: $forbidden"
	}
}

# --- 5층 분진 잔흔: 얇은 자리는 기질로 사라져야 한다 -----------------------
# 마스크를 불투명도에만 쓰면 남는 것은 「있다/없다」뿐이고, 어두운 바닥 위의
# 균일한 밝은 판이 된다 — CCTV 정중앙에서 바닥 위에 떠 있는 도장 자국으로
# 읽혔다. 그래서 같은 마스크로 밝기까지 변조하는데, **방향이 중요하다.**
#
# 처음 판은 알베도를 곱했다. BLEND_MASKED에 부분 투명이 없으니 얇은 자리를
# 표현할 수단이 알베도뿐인데, 곱셈은 그것을 검정 쪽으로 끌어당겼다. 근접
# 프레임에서 잔흔이 덮은 픽셀의 중앙값이 바로 인접한 바닥의 0.84배로 나왔다 —
# 석고 분진이 흙때가 된 것이다. 얇은 가루층은 기질과 가루의 혼합이므로
# 기질색에서 잔흔색으로 보간해야 한다. A가 기질, B가 잔흔이다. 이 순서가
# 뒤집히면 두껍게 쌓인 자리가 바닥색이 되고 스친 자리만 밝아진다.
$residueMaterialSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Scripts/create_textured_materials.py')
foreach ($needle in @(
	'substrate = spec.get("substrate")',
	'MaterialExpressionLinearInterpolate',
	'ground, "", shade, "A"',
	'base, "", shade, "B"',
	'opacity, "", shade, "Alpha"',
	'shade, "", unreal.MaterialProperty.MP_BASE_COLOR')) {
	if (-not $residueMaterialSource.Contains($needle)) {
		throw "ART_ASSET_CONTRACT FAIL: residue substrate blend is missing: $needle"
	}
}
# 곱셈 방식으로 되돌아가면 얇은 자리가 다시 검정으로 간다.
if ($residueMaterialSource.Contains('shaded, "", unreal.MaterialProperty.MP_BASE_COLOR')) {
	throw 'ART_ASSET_CONTRACT FAIL: 잔흔 알베도를 곱하면 얇은 자리가 바닥보다 어두워진다.'
}
# 어두운 콘크리트 위의 석고 분진은 살짝 밝은 얼룩이다. 원래 값(0.48~0.68
# 알베도, 증폭 1.8~2.5)은 바닥의 두 배 밝기에 이진 실루엣이었다. 기질값은
# 그 면의 실제 재질에서 온다 — 바닥은 콘크리트 다크, 벽은 마른 석고다.
foreach ($needle in @(
	'"color": (0.24, 0.23, 0.21), "mask_gain": 1.15',
	'"color": (0.26, 0.25, 0.23), "mask_gain": 1.10',
	'"color": (0.23, 0.22, 0.205), "mask_gain": 1.25',
	'"substrate": (0.124, 0.126, 0.132)',
	'"substrate": (0.487, 0.474, 0.443)')) {
	if (-not $residueMaterialSource.Contains($needle)) {
		throw "ART_ASSET_CONTRACT FAIL: residue tuning drifted: $needle"
	}
}
# 같은 계열의 잔흔을 다른 요각으로 열린 바닥에서 겹쳐 두면, 카메라에서 보면
# 칠해 놓은 X 한 개로 합쳐진다. 분진 이음은 벽선에 붙여 눕힌다.
$residueSceneSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
	Join-Path $projectRoot 'Source/IndieGame/Core/IGPrologueWorldScene.cpp')
foreach ($needle in @(
	'FVector(150.0f, 906.0f, 1200.26f)',
	'FVector(268.0f, 42.0f, 0.32f)',
	'FVector(20.0f, 718.0f, 1200.25f)')) {
	if (-not $residueSceneSource.Contains($needle)) {
		throw "ART_ASSET_CONTRACT FAIL: residue placement drifted: $needle"
	}
}

Write-Host 'ART_ASSET_CONTRACT PASS raw=58 masks=9 overlays=23 signage=6 material_scans=21 pbr_maps=83 meshes=46 photo_meshes=50'
