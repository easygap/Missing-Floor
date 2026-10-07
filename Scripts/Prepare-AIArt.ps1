# Turns raw ImageGen artwork into the exact texture sheets the prologue needs.
#
# ImageGen returns whatever aspect ratio it feels like (usually near-square,
# with the actual label floating on a background). The game needs precise
# power-of-two sheets that wrap authored geometry: a bottle label has to be
# circumference-by-height or it shears when it goes round the sleeve.
#
# So each entry declares the crop taken from the raw art (as fractions of the
# source, which survives ImageGen changing its output resolution) and the
# final pixel size. Output lands in Content/SourceArt next to the
# System.Drawing-generated textures, and generate_surface_textures.py imports
# the whole folder without caring which tool made which file.

[CmdletBinding()]
param(
    [string[]]$OnlySource = @()
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$aiDir = Join-Path $projectRoot 'Content\SourceArt\AI'
$outDir = Join-Path $projectRoot 'Content\SourceArt'

# Source stem -> crop rectangle (left, top, width, height as 0..1 fractions)
# and the target texture size. CropTight trims the flat border ImageGen puts
# around a "flat lay" design so the label fills the sheet edge to edge.
# Patches are applied after the crop, in final-sheet pixel coordinates.
# ImageGen renders headline Hangul cleanly but invents fine print, and any
# invented brand name is a trademark risk however small it is on screen. So
# every body-copy block that names the product gets painted out and rewritten
# with our own fictional wording — the artwork is the AI's, the words are ours.
$patchesWater = @(
    @{ Rect = @(4, 0, 176, 84); Fill = '#FFFFFF'
       Lines = @('새벽샘물은 이 동네 지하 200 m,', '아무도 손대지 않은 물길에서', '밤사이 길어 올린 물입니다.')
       FontSize = 12; Color = '#1F3A66'; LineHeight = 17 }
    @{ Rect = @(921, 163, 96, 36); Fill = '#FFFFFF'
       Lines = @('(주)새벽샘물'); FontSize = 12; Color = '#1F3A66'; LineHeight = 16 }
    @{ Rect = @(921, 213, 96, 36); Fill = '#FFFFFF'
       Lines = @('(주)새벽샘물'); FontSize = 12; Color = '#1F3A66'; LineHeight = 16 }
    @{ Rect = @(852, 0, 172, 14); Fill = '#FFFFFF'
       Lines = @(); FontSize = 12; Color = '#1F3A66'; LineHeight = 16 }
)

$plan = @(
    # Grid sheets: one generation carries four or six designs, which is the
    # only way to get twenty textures out of a tool that takes five minutes a
    # picture. Each cell is cut out separately by the same crop machinery.
    [pscustomobject]@{
        Source = 'SheetSigns'; Target = 'T_SignLaundry_D.png'
        Crop = @(0.019, 0.011, 0.965, 0.148); Size = @(512, 128)
    }
    [pscustomobject]@{
        Source = 'SheetSigns'; Target = 'T_SignHair_D.png'
        Crop = @(0.019, 0.174, 0.965, 0.152); Size = @(512, 128)
    }
    [pscustomobject]@{
        Source = 'SheetSigns'; Target = 'T_SignHof_D.png'
        Crop = @(0.019, 0.343, 0.965, 0.153); Size = @(512, 128)
    }
    [pscustomobject]@{
        Source = 'SheetSigns'; Target = 'T_SignSuper_D.png'
        Crop = @(0.019, 0.512, 0.965, 0.153); Size = @(512, 128)
    }
    [pscustomobject]@{
        Source = 'SheetSigns'; Target = 'T_SignPC_D.png'
        Crop = @(0.019, 0.683, 0.965, 0.152); Size = @(512, 128)
    }
    [pscustomobject]@{
        Source = 'SheetSigns'; Target = 'T_SignKaraoke_D.png'
        Crop = @(0.019, 0.851, 0.965, 0.148); Size = @(512, 128)
    }
    [pscustomobject]@{
        Source = 'SheetPosters'; Target = 'T_PosterSale_D.png'
        Crop = @(0.082, 0.022, 0.386, 0.478); Size = @(512, 704)
    }
    # Blank aged paper: the base every readable note is printed onto. The
    # Korean copy is drawn on top at runtime by the HUD, never baked in.
    [pscustomobject]@{
        Source = 'SheetPaperNotes_v2'; Target = 'T_PaperClean_V2_D.png'
        Crop = @(0.091, 0.023, 0.318, 0.453); Size = @(512, 724)
    }
    [pscustomobject]@{
        Source = 'SheetPaperNotes_v2'; Target = 'T_PaperOld_V2_D.png'
        Crop = @(0.588, 0.522, 0.323, 0.453); Size = @(512, 724)
    }
    # Environment overlays arrive on a flat chroma backing. ChromaAlpha
    # removes only pixels that are decisively key-coloured; rust and damp
    # browns remain intact instead of being flattened into generic gray dirt.
    [pscustomobject]@{
        Source = 'SheetHorrorSurfaceBlends'; Target = 'T_DecalDampWallpaper_D.png'
        Crop = @(0.000, 0.000, 0.500, 0.500); Size = @(512, 512)
        Mode = 'ChromaAlpha'
    }
    # Standalone ImageGen material scans. They are intentionally kept as
    # ordinary color textures: roughness and translucency remain authored in
    # the UE material graph, so baked highlights cannot fight the flashlight.
    [pscustomobject]@{
        Source = 'TextureCarrierBagFilm'; Target = 'T_CarrierBagFilm_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    [pscustomobject]@{
        Source = 'TextureAlleyCatTabby'; Target = 'T_AlleyCatTabby_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    [pscustomobject]@{
        Source = 'TextureP3CabinetPaintedSteel'; Target = 'T_P3CabinetPaintedSteel_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    # The generated UI scan is intentionally free of frames and baked text.
    # Runtime geometry supplies the rounded mask and channel accents while this
    # low-contrast layer prevents a sterile flat-color subtitle surface.
    [pscustomobject]@{
        Source = 'TextureHudDialogueFilm'; Target = 'T_HudDialogueFilm_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    # 최초 실행 보정판은 글자 없는 벽면 계조만 ImageGen에서 가져온다.
    # 문구와 눈금은 HUD가 그려 해상도·언어·접근성 배율에 종속되지 않는다.
    [pscustomobject]@{
        Source = 'TextureAudioCalibrationWall_v1'; Target = 'T_AudioCalibrationWall_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    # The title key art owns environment and light only. Keeping the generated
    # source text-free lets the native HUD localize every label, scale type at
    # runtime, and hide Continue cleanly when no compatible save exists.
    [pscustomobject]@{
        Source = 'TitleBackgroundMissingFloor_v1'; Target = 'T_TitleBackground_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1920, 1080)
    }
    # §9 에필로그의 세 정지 화면. 타이틀 키아트와 같은 규칙이다 — 그림은
    # 장소와 빛만 갖고, 한글은 전부 런타임 HUD가 그린다. 원본 비례를 그대로
    # 두는 것도 계약이다: HUD가 판을 원본 비례로 맞추므로 여기서 정사각형에
    # 욱여넣으면 화면에서 다시 늘어난다.
    [pscustomobject]@{
        Source = 'EpilogueWorkshop_v1'; Target = 'T_EpilogueWorkshop_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1536, 1024)
    }
    [pscustomobject]@{
        Source = 'EpilogueAutumn_v1'; Target = 'T_EpilogueAutumn_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1536)
    }
    [pscustomobject]@{
        Source = 'EpilogueServiceBay_v1'; Target = 'T_EpilogueServiceBay_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1536, 1024)
    }
    # Apartment vertical-slice art direction. The wallpaper remains a neutral
    # BaseColor scan; N/R/A are derived offline so no generated highlight is
    # baked into the room. The patina sheet is data, not a photographed plane:
    # white is damage coverage and black leaves the wallpaper untouched.
    [pscustomobject]@{
        Source = 'TextureApartmentWallpaperVintage'; Target = 'T_ApartmentWallpaperV2_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    # 한국 저층 빌라 외벽. 생성본은 조명과 문자를 배제한 BaseColor만
    # 제공하고, 미세 요철·거칠기·차폐는 아래 PBR 파생 단계가 담당한다.
    [pscustomobject]@{
        Source = 'TextureKoreanVillaStucco_v1'; Target = 'T_KoreanVillaStucco_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    # 입주 상자는 인쇄가 없는 중립적인 스캔만 사용한다. 한글 단서는 HUD에서
    # 출력해 생성 이미지의 잘못된 글자가 증거로 보이지 않게 한다.
    [pscustomobject]@{
        Source = 'TextureMovingBoxCardboard_v1'; Target = 'T_MovingBoxCardboard_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    [pscustomobject]@{
        Source = 'MaskApartmentWallPatina'; Target = 'T_ApartmentWallPatina_M.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
        Mode = 'Mask'
    }
    # The Missing Floor art pass. The reference sheets remain in AI/ as
    # provenance; only data that can receive real game lighting is promoted.
    [pscustomobject]@{
        Source = 'TextureMissingFloorDryPlaster'; Target = 'T_MissingFloorDryPlaster_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    # The footstep surfaces README rule 2 asks the player to tell apart by eye.
    # Sound already separates them; until now the picture did not, because all
    # three were rendered with the corridor's concrete. These arrive as square
    # material scans and are conditioned by condition_ai_tiles.py before any
    # PBR channel is derived from them — the raw scans do not tile.
    [pscustomobject]@{
        Source = 'TextureVillaStairCheckerPlatePaintedSteel_v2'
        Target = 'T_MissingFloorSteelStair_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    [pscustomobject]@{
        Source = 'TextureRooftopUrethaneWaterproofing'
        Target = 'T_RooftopWaterproofing_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    [pscustomobject]@{
        Source = 'TextureApartmentEntranceDoorCharcoalSteel'
        Target = 'T_UnitDoorPaintedSteel_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1024)
    }
    [pscustomobject]@{
        Source = 'SheetMissingFloorResidueMasks'; Target = 'T_MissingFloorHandprints_M.png'
        Crop = @(0.000, 0.000, 0.500, 0.500); Size = @(512, 512)
        Mode = 'Mask'
    }
    [pscustomobject]@{
        Source = 'SheetMissingFloorResidueMasks'; Target = 'T_MissingFloorDragTrails_M.png'
        Crop = @(0.500, 0.000, 0.500, 0.500); Size = @(512, 512)
        Mode = 'Mask'
    }
    [pscustomobject]@{
        Source = 'SheetMissingFloorResidueMasks'; Target = 'T_MissingFloorDustJoint_M.png'
        Crop = @(0.000, 0.500, 0.500, 0.500); Size = @(512, 512)
        Mode = 'Mask'
    }
    [pscustomobject]@{
        Source = 'SheetMissingFloorResidueMasks'; Target = 'T_MissingFloorCavityScratches_M.png'
        Crop = @(0.500, 0.500, 0.500, 0.500); Size = @(512, 512)
        Mode = 'Mask'
    }
    # Fixed-distance figures are the only people allowed to become sprites.
    # The crop excludes the generator's white sheet gutters before keying.
    [pscustomobject]@{
        Source = 'SheetMissingFloorDistantCharacters'; Target = 'T_SpriteSeo_D.png'
        Crop = @(0.010, 0.010, 0.484, 0.484); Size = @(512, 512)
        Mode = 'ChromaAlpha'; Desaturate = 0.24
    }
    [pscustomobject]@{
        Source = 'SheetMissingFloorDistantCharacters'; Target = 'T_SpriteMok_D.png'
        Crop = @(0.506, 0.010, 0.484, 0.484); Size = @(512, 512)
        Mode = 'ChromaAlpha'; Desaturate = 0.32
    }
    # The listener is seen head-on down a long corridor. A lit 2.5D card
    # preserves the generated human anatomy at that one authored angle while
    # the continuous 3D shell remains available for contact shadow and side
    # reads. Green is used only for offline extraction and never reaches UE.
    [pscustomobject]@{
        Source = 'ListenerEntityFrontCutout'; Target = 'T_SpriteListenerFront_D.png'
        Crop = @(0.080, 0.130, 0.880, 0.740); Size = @(1024, 1024)
        Mode = 'ChromaAlphaGreen'; Desaturate = 0.10
    }
    # Four consecutive crawl phases generated from the approved anatomy and
    # front-cutout references. The two-pixel outer border and five-pixel grid
    # gutter stay outside these equal crops, so white sheet lines can never
    # leak into the masked material.
    [pscustomobject]@{
        Source = 'SheetListenerEntityCrawlPhases'; Target = 'T_SpriteListenerCrawl0_D.png'
        Crop = @(0.002, 0.002, 0.496, 0.496); Size = @(1024, 1024)
        Mode = 'ChromaAlphaGreen'; Desaturate = 0.10
    }
    [pscustomobject]@{
        Source = 'SheetListenerEntityCrawlPhases'; Target = 'T_SpriteListenerCrawl1_D.png'
        Crop = @(0.502, 0.002, 0.496, 0.496); Size = @(1024, 1024)
        Mode = 'ChromaAlphaGreen'; Desaturate = 0.10
    }
    [pscustomobject]@{
        Source = 'SheetListenerEntityCrawlPhases'; Target = 'T_SpriteListenerCrawl2_D.png'
        Crop = @(0.002, 0.502, 0.496, 0.496); Size = @(1024, 1024)
        Mode = 'ChromaAlphaGreen'; Desaturate = 0.10
    }
    [pscustomobject]@{
        Source = 'SheetListenerEntityCrawlPhases'; Target = 'T_SpriteListenerCrawl3_D.png'
        Crop = @(0.502, 0.502, 0.496, 0.496); Size = @(1024, 1024)
        Mode = 'ChromaAlphaGreen'; Desaturate = 0.10
    }
    # M5 keeps continuous 3D silhouettes and contact shadows. These two
    # front layers contribute only the close flashlight detail that a
    # procedural release mesh cannot carry at this stage.
    [pscustomobject]@{
        Source = 'FinalCavityFrontBlend_v1'; Target = 'T_SpriteFinalCavity_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1536)
        Mode = 'ChromaAlphaGreen'; Desaturate = 0.08
    }
    [pscustomobject]@{
        Source = 'MokHansooFinalFrontBlend_v1'; Target = 'T_SpriteMokFinalUpper_D.png'
        Crop = @(0.000, 0.000, 1.000, 1.000); Size = @(1024, 1536)
        Mode = 'ChromaAlphaGreen'; Desaturate = 0.06
        # The real 95 cm board and lower body remain 3D. Fade this face/jacket
        # layer behind the board top so no flat lower-body silhouette survives.
        AlphaFadeBottom = @(0.31, 0.39)
    }
    # M0 first-person knock. V2 keeps the approved hand while extending the
    # sleeve through each lower-right cell corner, so its crop can live beyond
    # the viewport instead of ending as a visible rectangle.
    [pscustomobject]@{
        Source = 'SheetFirstPersonKnockPhases_v2_RGBA'; Target = 'T_FPHandKnock0_D.png'
        Crop = @(0.002, 0.002, 0.496, 0.496); Size = @(768, 768)
        ContentScale = 0.63; Mode = 'PreserveAlphaGreenDespill'
    }
    [pscustomobject]@{
        Source = 'SheetFirstPersonKnockPhases_v2_RGBA'; Target = 'T_FPHandKnock1_D.png'
        Crop = @(0.502, 0.002, 0.496, 0.496); Size = @(768, 768)
        ContentScale = 0.63; Mode = 'PreserveAlphaGreenDespill'
    }
    [pscustomobject]@{
        Source = 'SheetFirstPersonKnockPhases_v2_RGBA'; Target = 'T_FPHandKnock2_D.png'
        Crop = @(0.002, 0.502, 0.496, 0.496); Size = @(768, 768)
        ContentScale = 0.63; Mode = 'PreserveAlphaGreenDespill'
    }
    [pscustomobject]@{
        Source = 'SheetFirstPersonKnockPhases_v2_RGBA'; Target = 'T_FPHandKnock3_D.png'
        Crop = @(0.502, 0.502, 0.496, 0.496); Size = @(768, 768)
        ContentScale = 0.63; Mode = 'PreserveAlphaGreenDespill'
    }
)

if ($OnlySource.Count -gt 0) {
    $sourceFilter = @{}
    foreach ($sourceName in $OnlySource) {
        $sourceFilter[$sourceName] = $true
    }
    $plan = @($plan | Where-Object { $sourceFilter.ContainsKey($_.Source) })
}

$written = 0
foreach ($entry in $plan) {
    $sourcePath = Join-Path $aiDir ($entry.Source + '.png')
    if (-not (Test-Path $sourcePath)) {
        Write-Host "skip $($entry.Source): not generated yet"
        continue
    }

    $source = [System.Drawing.Image]::FromFile($sourcePath)
    try {
        $cropRect = New-Object System.Drawing.Rectangle(
            [int]($source.Width * $entry.Crop[0]),
            [int]($source.Height * $entry.Crop[1]),
            [int]($source.Width * $entry.Crop[2]),
            [int]($source.Height * $entry.Crop[3]))

        $target = New-Object System.Drawing.Bitmap($entry.Size[0], $entry.Size[1])
        $graphics = [System.Drawing.Graphics]::FromImage($target)
        # Labels are read close up and at a glancing angle, so resampling
        # quality matters more here than in almost any other texture.
        $graphics.InterpolationMode = 'HighQualityBicubic'
        $graphics.PixelOffsetMode = 'HighQuality'
        $graphics.SmoothingMode = 'HighQuality'
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $contentScale = if ($null -ne $entry.PSObject.Properties['ContentScale']) {
            [double]$entry.ContentScale
        }
        else {
            1.0
        }
        $destRect = New-Object System.Drawing.Rectangle(
            0,
            0,
            [int][Math]::Round($entry.Size[0] * $contentScale),
            [int][Math]::Round($entry.Size[1] * $contentScale))
        $graphics.DrawImage($source, $destRect, $cropRect, [System.Drawing.GraphicsUnit]::Pixel)

        # Paint out and rewrite any fine print the generator invented.
        $graphics.TextRenderingHint = 'AntiAliasGridFit'
        $patches = if ($null -ne $entry.PSObject.Properties['Patches']) {
            @($entry.Patches)
        }
        else {
            @()
        }
        foreach ($patch in $patches) {
            $fill = [System.Drawing.ColorTranslator]::FromHtml($patch.Fill)
            $brush = New-Object System.Drawing.SolidBrush($fill)
            $graphics.FillRectangle(
                $brush, $patch.Rect[0], $patch.Rect[1], $patch.Rect[2], $patch.Rect[3])
            $brush.Dispose()

            if ($patch.Lines.Count -eq 0) { continue }
            $ink = New-Object System.Drawing.SolidBrush(
                [System.Drawing.ColorTranslator]::FromHtml($patch.Color))
            $font = New-Object System.Drawing.Font(
                'Malgun Gothic', [single]$patch.FontSize,
                [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
            for ($i = 0; $i -lt $patch.Lines.Count; $i++) {
                $graphics.DrawString(
                    $patch.Lines[$i], $font, $ink,
                    [single]$patch.Rect[0], [single]($patch.Rect[1] + $i * $patch.LineHeight))
            }
            $font.Dispose(); $ink.Dispose()
        }

        $graphics.Dispose()

        $entryMode = if ($null -ne $entry.PSObject.Properties['Mode']) {
            [string]$entry.Mode
        }
        else {
            ''
        }
        if ($entryMode -eq 'Mask') {
            for ($y = 0; $y -lt $target.Height; $y++) {
                for ($x = 0; $x -lt $target.Width; $x++) {
                    $pixel = $target.GetPixel($x, $y)
                    $value = [Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B))
                    $target.SetPixel(
                        $x,
                        $y,
                        [System.Drawing.Color]::FromArgb(255, $value, $value, $value))
                }
            }
        }
        elseif ($entryMode -eq 'ChromaAlpha') {
            $desaturate = if (
                $null -ne $entry.PSObject.Properties['Desaturate']
            ) {
                [double]$entry.Desaturate
            }
            else {
                0.0
            }
            for ($y = 0; $y -lt $target.Height; $y++) {
                for ($x = 0; $x -lt $target.Width; $x++) {
                    $pixel = $target.GetPixel($x, $y)
                    # Both red and blue rise together in a magenta-backed
                    # antialiased edge. Rust raises red alone; mildew and
                    # mineral scale keep RGB much closer together. This
                    # excess test therefore removes the key fringe without
                    # punching holes through the actual deposits.
                    $keyExcess =
                        [Math]::Min($pixel.R, $pixel.B) - $pixel.G
                    $isKey =
                        $keyExcess -gt 18 -and
                        $pixel.R -gt 110 -and
                        $pixel.B -gt 110
                    if ($isKey) {
                        $target.SetPixel(
                            $x,
                            $y,
                            [System.Drawing.Color]::FromArgb(0, 0, 0, 0))
                        continue
                    }

                    $spill = [Math]::Max(0.0, [double]$keyExcess)
                    $red = [Math]::Max(0.0, [double]$pixel.R - $spill)
                    $green = [double]$pixel.G
                    $blue = [Math]::Max(0.0, [double]$pixel.B - $spill)
                    if ($desaturate -gt 0.0) {
                        $luma = $red * 0.2126 + $green * 0.7152 + $blue * 0.0722
                        $red = $red + ($luma - $red) * $desaturate
                        $green = $green + ($luma - $green) * $desaturate
                        $blue = $blue + ($luma - $blue) * $desaturate
                    }
                    $target.SetPixel(
                        $x,
                        $y,
                        [System.Drawing.Color]::FromArgb(
                            255,
                            [int][Math]::Round($red),
                            [int][Math]::Round($green),
                            [int][Math]::Round($blue)))
                }
            }
        }
        elseif ($entryMode -eq 'ChromaAlphaGreen') {
            $desaturate = if (
                $null -ne $entry.PSObject.Properties['Desaturate']
            ) {
                [double]$entry.Desaturate
            }
            else {
                0.0
            }
            for ($y = 0; $y -lt $target.Height; $y++) {
                for ($x = 0; $x -lt $target.Width; $x++) {
                    $pixel = $target.GetPixel($x, $y)
                    $otherMax = [Math]::Max($pixel.R, $pixel.B)
                    $keyExcess = [double]$pixel.G - $otherMax

                    # A soft key keeps hair and plaster-dust antialiasing,
                    # then removes reflected green from the surviving RGB.
                    $dominance = [Math]::Max(
                        0.0,
                        [Math]::Min(1.0, ($keyExcess - 10.0) / 86.0))
                    $brightness = [Math]::Max(
                        0.0,
                        [Math]::Min(1.0, ($pixel.G - 70.0) / 150.0))
                    $keyStrength = $dominance * $brightness
                    $alpha = [int][Math]::Round(255.0 * (1.0 - $keyStrength))
                    if ($alpha -le 3) {
                        $target.SetPixel(
                            $x,
                            $y,
                            [System.Drawing.Color]::FromArgb(0, 0, 0, 0))
                        continue
                    }

                    $red = [double]$pixel.R
                    $blue = [double]$pixel.B
                    $neutralGreen = ($red + $blue) * 0.5 + 4.0
                    $green = [Math]::Min([double]$pixel.G, $neutralGreen)
                    if ($desaturate -gt 0.0) {
                        $luma = $red * 0.2126 + $green * 0.7152 + $blue * 0.0722
                        $red = $red + ($luma - $red) * $desaturate
                        $green = $green + ($luma - $green) * $desaturate
                        $blue = $blue + ($luma - $blue) * $desaturate
                    }
                    $target.SetPixel(
                        $x,
                        $y,
                        [System.Drawing.Color]::FromArgb(
                            $alpha,
                            [int][Math]::Round($red),
                            [int][Math]::Round($green),
                            [int][Math]::Round($blue)))
                }
            }
        }
        elseif ($entryMode -eq 'PreserveAlphaGreenDespill') {
            # The imagegen helper owns the soft matte. This second pass only
            # removes sub-pixel green reflected into surviving RGB during the
            # source render; it never expands or erodes the approved alpha.
            for ($y = 0; $y -lt $target.Height; $y++) {
                for ($x = 0; $x -lt $target.Width; $x++) {
                    $pixel = $target.GetPixel($x, $y)
                    if ($pixel.A -le 3) {
                        $target.SetPixel(
                            $x,
                            $y,
                            [System.Drawing.Color]::FromArgb(0, 0, 0, 0))
                        continue
                    }
                    $neutralGreen =
                        ([double]$pixel.R + [double]$pixel.B) * 0.5 + 4.0
                    $green = [Math]::Min([double]$pixel.G, $neutralGreen)
                    $target.SetPixel(
                        $x,
                        $y,
                        [System.Drawing.Color]::FromArgb(
                            $pixel.A,
                            $pixel.R,
                            [int][Math]::Round($green),
                            $pixel.B))
                }
            }
        }

        if ($null -ne $entry.PSObject.Properties['AlphaFadeBottom']) {
            $fadeStart = [double]$entry.AlphaFadeBottom[0]
            $fadeEnd = [double]$entry.AlphaFadeBottom[1]
            if ($fadeEnd -le $fadeStart) {
                throw "AlphaFadeBottom end must be greater than start: $($entry.Source)"
            }
            for ($y = 0; $y -lt $target.Height; $y++) {
                $ratio = [double]$y / [Math]::Max(1, $target.Height - 1)
                if ($ratio -lt $fadeStart) { continue }
                $keep = [Math]::Max(
                    0.0,
                    [Math]::Min(1.0, ($fadeEnd - $ratio) / ($fadeEnd - $fadeStart)))
                for ($x = 0; $x -lt $target.Width; $x++) {
                    $pixel = $target.GetPixel($x, $y)
                    if ($pixel.A -eq 0) { continue }
                    $target.SetPixel(
                        $x,
                        $y,
                        [System.Drawing.Color]::FromArgb(
                            [int][Math]::Round($pixel.A * $keep),
                            $pixel.R,
                            $pixel.G,
                            $pixel.B))
                }
            }
        }

        $targetPath = Join-Path $outDir $entry.Target
        $target.Save($targetPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $target.Dispose()
        Write-Host "wrote $($entry.Target)  ($($entry.Size[0])x$($entry.Size[1]))  from $($source.Width)x$($source.Height)"
        $written++
    }
    finally {
        $source.Dispose()
    }
}

Write-Host "AI art prepared: $written/$($plan.Count)"
