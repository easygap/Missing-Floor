# Renders the Korean signage/poster/label bitmaps used by the prologue into
# Content/SourceArt as PNGs. generate_surface_textures.py imports everything
# in that folder afterwards. Exact lettering uses System.Drawing and system
# fonts; selected paper bases come from the versioned SourceArt/AI directory.

[CmdletBinding()]
param(
    # Rebuild only the landing door plates and the 401 note. This keeps a
    # targeted Unreal import from touching unrelated authored signs.
    [switch]$CorridorEntranceOnly,
    # 입주 프롤로그에서 사용하는 한글 임대차계약서만 다시 만든다.
    # 문서는 허구이며 주민등록번호는 표기하지 않는다.
    [switch]$ArrivalPrologueOnly,
    # 담배 판매 안내만 다시 만든다. 글자가 가장자리에 닿지 않게 폭에 맞춘 뒤
    # 인쇄 아틀라스를 다시 묶을 때 쓴다.
    [switch]$PrintMarginOnly
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $projectRoot 'Content\SourceArt'
New-Item -ItemType Directory -Force $outDir | Out-Null

function New-SignBitmap {
    param(
        [int]$Width,
        [int]$Height,
        [System.Drawing.Color]$Background,
        [string]$BackgroundImagePath,
        [scriptblock]$Draw,
        [string]$FileName
    )
    $bitmap = New-Object System.Drawing.Bitmap($Width, $Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = 'AntiAlias'
    $graphics.TextRenderingHint = 'AntiAliasGridFit'
    if ($BackgroundImagePath) {
        if (-not (Test-Path -LiteralPath $BackgroundImagePath -PathType Leaf)) {
            throw "Sign background image is missing: $BackgroundImagePath"
        }
        $backgroundImage = [System.Drawing.Image]::FromFile($BackgroundImagePath)
        try {
            $graphics.InterpolationMode = 'HighQualityBicubic'
            $graphics.PixelOffsetMode = 'HighQuality'
            $graphics.DrawImage($backgroundImage, 0, 0, $Width, $Height)
        }
        finally {
            $backgroundImage.Dispose()
        }
    }
    else {
        $graphics.Clear($Background)
    }
    & $Draw $graphics $Width $Height
    $graphics.Dispose()
    $path = Join-Path $outDir $FileName
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    Write-Host "Wrote $path"
}

function Draw-CenteredText {
    param($Graphics, $Text, $FontFamily, [single]$Size, [System.Drawing.FontStyle]$Style,
        [System.Drawing.Color]$Color, [single]$CenterX, [single]$CenterY, [single]$MaxWidth = 0)
    $font = New-Object System.Drawing.Font($FontFamily, $Size, $Style, [System.Drawing.GraphicsUnit]::Pixel)
    if ($MaxWidth -gt 0) {
        # 폭을 넘으면 글자를 줄인다. 인쇄물 가장자리에 글자가 닿으면 잘린 것처럼 보인다.
        $measured = $Graphics.MeasureString($Text, $font).Width
        if ($measured -gt $MaxWidth) {
            $fittedSize = [single]($Size * $MaxWidth / $measured)
            $font.Dispose()
            $font = New-Object System.Drawing.Font($FontFamily, $fittedSize, $Style, [System.Drawing.GraphicsUnit]::Pixel)
        }
    }
    $brush = New-Object System.Drawing.SolidBrush($Color)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = 'Center'
    $format.LineAlignment = 'Center'
    $Graphics.DrawString($Text, $font, $brush, $CenterX, $CenterY, $format)
    $format.Dispose(); $brush.Dispose(); $font.Dispose()
}

$malgun = 'Malgun Gothic'
$mint = [System.Drawing.Color]::FromArgb(255, 22, 168, 128)
$white = [System.Drawing.Color]::White
$nearWhite = [System.Drawing.Color]::FromArgb(255, 236, 240, 238)
$dark = [System.Drawing.Color]::FromArgb(255, 24, 26, 28)
$red = [System.Drawing.Color]::FromArgb(255, 198, 40, 32)

# Windows ships the old Hangul word-processor "Pyunji" face as a font file
# even when it is not registered as a normal family. Loading it privately gives
# the fridge note a restrained handwritten shape without rasterising AI text.
$privateFonts = New-Object System.Drawing.Text.PrivateFontCollection
$pyunjiFontPath = Join-Path $env:WINDIR 'Fonts\HMFMPYUN.TTF'
if (Test-Path -LiteralPath $pyunjiFontPath -PathType Leaf) {
    $privateFonts.AddFontFile($pyunjiFontPath)
}
$noteFontFamily = $privateFonts.Families | Where-Object { $_.Name -eq 'Pyunji R' } | Select-Object -First 1
if (-not $noteFontFamily) {
    $noteFontFamily = $malgun
}

$stickyNotePaper = Join-Path $outDir 'AI\TextureStickyNotePaper_D.png'
$captureMercyNotePaper = Join-Path $outDir 'AI\TextureCaptureMercyNotePaper_D.png'
$cleanPaper = Join-Path $outDir 'T_PaperClean_V2_D.png'

function Write-ArrivalContract {
    # 국내 주택 임대차계약서의 일반적인 구성에 맞춰 제목, 부동산 표시,
    # 계약 조건, 특약, 서명 순서로 A4 세로 문서를 만든다.
    # 단서가 되는 문구는 이미지 모델에 맡기지 않고 직접 렌더링하며,
    # 이름과 주소는 모두 가상 정보만 사용한다.
    New-SignBitmap -Width 1448 -Height 2048 `
        -Background ([System.Drawing.Color]::FromArgb(255, 239, 235, 220)) `
        -BackgroundImagePath $cleanPaper `
        -FileName 'T_ArrivalContract_D.png' -Draw {
        param($g, $w, $h)

        $ink = [System.Drawing.Color]::FromArgb(255, 35, 34, 31)
        $muted = [System.Drawing.Color]::FromArgb(255, 78, 74, 67)
        $line = New-Object System.Drawing.Pen($ink, 4)
        $thin = New-Object System.Drawing.Pen($muted, 2)
        $seal = New-Object System.Drawing.Pen(
            [System.Drawing.Color]::FromArgb(205, 170, 38, 32), 8)
        $brush = New-Object System.Drawing.SolidBrush($ink)
        $mutedBrush = New-Object System.Drawing.SolidBrush($muted)
        $titleFont = New-Object System.Drawing.Font(
            $malgun, 82, [System.Drawing.FontStyle]::Bold,
            [System.Drawing.GraphicsUnit]::Pixel)
        $headingFont = New-Object System.Drawing.Font(
            $malgun, 37, [System.Drawing.FontStyle]::Bold,
            [System.Drawing.GraphicsUnit]::Pixel)
        $bodyFont = New-Object System.Drawing.Font(
            $malgun, 31, [System.Drawing.FontStyle]::Regular,
            [System.Drawing.GraphicsUnit]::Pixel)
        $smallFont = New-Object System.Drawing.Font(
            $malgun, 25, [System.Drawing.FontStyle]::Regular,
            [System.Drawing.GraphicsUnit]::Pixel)
        $format = New-Object System.Drawing.StringFormat
        $format.Trimming = [System.Drawing.StringTrimming]::EllipsisCharacter

        try {
            $titleFormat = New-Object System.Drawing.StringFormat
            $titleFormat.Alignment = 'Center'
            $g.DrawString('주택임대차계약서', $titleFont, $brush,
                [single]($w * 0.5), 86, $titleFormat)
            $titleFormat.Dispose()

            $g.DrawString('임대인과 임차인은 아래 표시 주택에 관하여 다음과 같이 임대차계약을 체결한다.',
                $smallFont, $mutedBrush, 104, 205)

            # 부동산 표시.
            $left = 96; $right = $w - 96; $top = 278; $row = 94
            $g.DrawRectangle($line, $left, $top, $right - $left, $row * 4)
            for ($i = 1; $i -lt 4; $i++) {
                $g.DrawLine($thin, $left, $top + $row * $i, $right, $top + $row * $i)
            }
            $labelX = $left + 245
            $g.DrawLine($thin, $labelX, $top, $labelX, $top + $row * 4)
            $labels = @('소 재 지', '토지·건물', '임대할 부분', '용    도')
            $values = @(
                '서울특별시 은평구 무영로 27-3  달빛빌라 403호',
                '철근콘크리트조 · 다세대주택 / 건축물대장상 지상 4층',
                '제4층 403호 전부  29.7㎡',
                '주거용'
            )
            for ($i = 0; $i -lt 4; $i++) {
                $g.DrawString($labels[$i], $headingFont, $brush, $left + 18, $top + $row * $i + 22)
                $g.DrawString($values[$i], $bodyFont, $brush, $labelX + 24, $top + $row * $i + 25)
            }

            # 계약 조건과 지급 항목.
            $termsTop = 740; $termsRow = 88
            $g.DrawString('제1조  보증금 및 차임', $headingFont, $brush, $left, $termsTop - 58)
            $g.DrawRectangle($line, $left, $termsTop, $right - $left, $termsRow * 4)
            for ($i = 1; $i -lt 4; $i++) {
                $g.DrawLine($thin, $left, $termsTop + $termsRow * $i, $right, $termsTop + $termsRow * $i)
            }
            $g.DrawString('보 증 금', $headingFont, $brush, $left + 20, $termsTop + 20)
            $g.DrawString('금 오백만원정 (￦ 5,000,000)', $bodyFont, $brush, $left + 230, $termsTop + 24)
            $g.DrawString('계 약 금', $headingFont, $brush, $left + 20, $termsTop + $termsRow + 20)
            $g.DrawString('금 오십만원정 — 계약 시 지급함', $bodyFont, $brush, $left + 230, $termsTop + $termsRow + 24)
            $g.DrawString('잔    금', $headingFont, $brush, $left + 20, $termsTop + $termsRow * 2 + 20)
            $g.DrawString('금 사백오십만원정 — 2025년 7월 25일', $bodyFont, $brush, $left + 230, $termsTop + $termsRow * 2 + 24)
            $g.DrawString('차    임', $headingFont, $brush, $left + 20, $termsTop + $termsRow * 3 + 20)
            $g.DrawString('월 금 사십만원정 / 매월 25일 지급', $bodyFont, $brush, $left + 230, $termsTop + $termsRow * 3 + 24)

            $g.DrawString('제2조  임대차 기간', $headingFont, $brush, $left, 1115)
            $g.DrawString('2025년 7월 25일부터 2026년 7월 24일까지 (12개월)',
                $bodyFont, $brush, $left + 42, 1172)

            $g.DrawString('특 약 사 항', $headingFont, $brush, $left, 1260)
            $specialTop = 1320
            $g.DrawRectangle($line, $left, $specialTop, $right - $left, 330)
            $special = @(
                '1. 현 시설 상태의 임대차이며 임차인은 입주 전 시설을 확인한다.',
                '2. 건축물대장상 본 건물은 지상 4층이며 옥상은 공용시설이다.',
                '3. 옥상 창고와 기계실은 임대 목적물에 포함되지 않으며 출입을 금한다.',
                '4. 관리비 및 공용 전기·수도 사용료는 별도 정산한다.'
            )
            for ($i = 0; $i -lt $special.Count; $i++) {
                $g.DrawString($special[$i], $bodyFont, $brush, $left + 26, $specialTop + 28 + 70 * $i)
            }

            $g.DrawString('2025년  7월  25일', $headingFont, $brush, 545, 1694)
            # 이름 뒤 (인) 자리에 도장을 찍고, 주소는 그 오른쪽 칸에 둔다.
            # 주민번호나 서명을 꾸며내지 않고도 체결된 계약서임을 알 수 있다.
            $addressX = $left + 470
            foreach ($party in @(
                @('임대인  목 한 수  ', '서울 은평구 무영로 27-3', 1778),
                @('임차인  백 유 담  ', '서울 은평구 무영로 27-3, 403호', 1848)
            )) {
                $nameText = [string]$party[0]
                $rowY = [int]$party[2]
                $g.DrawString($nameText + '(인)', $bodyFont, $brush, $left, $rowY)
                $sealX = $left + $g.MeasureString($nameText, $bodyFont).Width - 6
                $g.DrawEllipse($seal, [single]$sealX, [single]($rowY - 16), 72, 72)
                $g.DrawString('주소  ' + [string]$party[1], $bodyFont, $brush, $addressX, $rowY)
            }
            $g.DrawString('중개인  무영공인중개사사무소',
                $smallFont, $mutedBrush, $left, 1920)
        }
        finally {
            $format.Dispose(); $smallFont.Dispose(); $bodyFont.Dispose()
            $headingFont.Dispose(); $titleFont.Dispose(); $mutedBrush.Dispose()
            $brush.Dispose(); $seal.Dispose(); $thin.Dispose(); $line.Dispose()
        }
    }
}

function Write-CorridorEntranceSigns {
    # 문패는 평범한 401·402·403이다. 403호 다음 벽은 비워 둔다.
    foreach ($unit in @('401', '402', '403')) {
        $file = "T_Plate$unit`_D.png"
        New-SignBitmap -Width 128 -Height 64 -Background $nearWhite -FileName $file -Draw {
            param($g, $w, $h)
            Draw-CenteredText $g "${unit}호" $malgun 36 ([System.Drawing.FontStyle]::Bold) $dark ($w * 0.5) ($h * 0.5)
        }
    }

    # 다섯 번 포획되면 401호에서 조용한 도움을 한 번 건넨다.
    # 생성 이미지는 값싼 종이 질감에만 쓰고, 한글은 절차적으로 합성해
    # HUD 메모창 없이도 세계 안에서 정확히 읽히게 한다.
    New-SignBitmap -Width 1024 -Height 640 `
        -Background ([System.Drawing.Color]::FromArgb(255, 218, 207, 180)) `
        -BackgroundImagePath $captureMercyNotePaper `
        -FileName 'T_CaptureMercyNote_D.png' -Draw {
        param($g, $w, $h)

        $ballpoint = [System.Drawing.Color]::FromArgb(255, 35, 42, 54)
        Draw-CenteredText $g '소리를 줄여라.' $noteFontFamily 92 ([System.Drawing.FontStyle]::Regular) $ballpoint ($w * 0.50) ($h * 0.38)
        Draw-CenteredText $g '걔는 눈이 없어.' $noteFontFamily 92 ([System.Drawing.FontStyle]::Regular) $ballpoint ($w * 0.50) ($h * 0.62)
    }

    # §20.3 세계의 90초 반응. 같은 수첩에서 찢은 종이라 질감과 잉크는 그대로
    # 두고 문장만 바꾼다. 이 메모는 퍼즐에 대해 아무것도 말하지 않는다 —
    # 새벽 네시 반에 깨어 있는 사람이 낮에 문을 열어 두겠다고만 한다. 그것이
    # 세 번째 안전망인 황순금으로 가는 길이고, 답이 아니라 볼 곳이다.
    New-SignBitmap -Width 1024 -Height 640 `
        -Background ([System.Drawing.Color]::FromArgb(255, 218, 207, 180)) `
        -BackgroundImagePath $captureMercyNotePaper `
        -FileName 'T_MercyNoteUnderDoor_D.png' -Draw {
        param($g, $w, $h)

        $ballpoint = [System.Drawing.Color]::FromArgb(255, 35, 42, 54)
        Draw-CenteredText $g '낮에 와.' $noteFontFamily 96 ([System.Drawing.FontStyle]::Regular) $ballpoint ($w * 0.50) ($h * 0.38)
        Draw-CenteredText $g '문 열어 둘게.' $noteFontFamily 92 ([System.Drawing.FontStyle]::Regular) $ballpoint ($w * 0.50) ($h * 0.62)
    }

    # §5.5 저장 불가 규칙의 물리적 근거. 5번은 NVR을 거치지 않고 모니터의
    # 예비 BNC 입력에만 직결된 채널이라, 화면에는 뜨지만 어디에도 남지 않는다.
    # 설치업자가 붙인 라벨이 아니라 목한수가 직접 붙인 마스킹 테이프라서
    # 인쇄체가 아닌 유성펜 글씨이고, 글자는 문서가 지정한 두 줄 그대로다.
    # 640×240 keeps the 8:3 shape of a 12.8×4.8 cm strip while clearing the print
    # audit's 512×240 floor. She reads this leaning over the desk, so the marker
    # strokes have to survive that distance.
    New-SignBitmap -Width 640 -Height 240 `
        -Background ([System.Drawing.Color]::FromArgb(255, 231, 224, 205)) `
        -FileName 'T_SignAux5MonitorOnly_D.png' -Draw {
        param($g, $w, $h)

        # 테이프 위아래 눌린 자리. 라벨이 스티커가 아니라 붙인 물건으로 읽힌다.
        $edge = New-Object System.Drawing.SolidBrush(
            [System.Drawing.Color]::FromArgb(70, 120, 112, 96))
        $g.FillRectangle($edge, 0, 0, $w, [single]($h * 0.07))
        $g.FillRectangle($edge, 0, [single]($h * 0.93), $w, [single]($h * 0.07))
        $edge.Dispose()

        $marker = [System.Drawing.Color]::FromArgb(255, 28, 34, 52)
        Draw-CenteredText $g 'AUX 5' 'Segoe Print' 85 ([System.Drawing.FontStyle]::Bold) $marker ($w * 0.50) ($h * 0.33)
        Draw-CenteredText $g 'MONITOR ONLY' 'Segoe Print' 65 ([System.Drawing.FontStyle]::Regular) $marker ($w * 0.50) ($h * 0.71)
    }
}

# 가장자리까지 글자가 닿던 담배 판매 안내. 폭을 정해 두고 글자를 그 안에 맞춘다.
function Write-FittedPrints {
New-SignBitmap -Width 512 -Height 64 -Background $nearWhite -FileName 'T_TobaccoNotice_D.png' -Draw {
    param($g, $w, $h)
    Draw-CenteredText $g '청소년에게 담배를 판매하지 않습니다' $malgun 28 ([System.Drawing.FontStyle]::Bold) $dark ($w * 0.5) ($h * 0.5) ($w * 0.90)
}
}

if ($PrintMarginOnly) {
    Write-FittedPrints
    return
}

if ($CorridorEntranceOnly) {
    Write-CorridorEntranceSigns
    return
}

if ($ArrivalPrologueOnly) {
    Write-ArrivalContract
    return
}

# --- Store fascia: exact fictional POS identity -----------------------------
New-SignBitmap -Width 1024 -Height 256 -Background ([System.Drawing.Color]::FromArgb(255, 5, 31, 66)) -FileName 'T_SignMain_D.png' -Draw {
    param($g, $w, $h)

    # Deep-navy-to-teal fascia is plausible for a Korean independent chain
    # while remaining clearly fictional and trademark-safe.
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $gradient = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        $rect,
        [System.Drawing.Color]::FromArgb(255, 4, 28, 65),
        [System.Drawing.Color]::FromArgb(255, 5, 143, 147),
        0.0)
    $g.FillRectangle($gradient, $rect)
    $gradient.Dispose()

    # A deterministic vector crescent keeps the mark readable at a distance.
    $moon = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 238, 166))
    $cutout = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 4, 35, 73))
    $g.FillEllipse($moon, 43, 28, 145, 200)
    $g.FillEllipse($cutout, 92, 4, 132, 184)
    $moon.Dispose()
    $cutout.Dispose()

    Draw-CenteredText $g '새벽24' $malgun 126 ([System.Drawing.FontStyle]::Bold) $white ($w * 0.45) ($h * 0.50)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(210, 255, 255, 255), 4)
    $g.DrawLine($pen, [single]($w * 0.73), [single]($h * 0.18), [single]($w * 0.73), [single]($h * 0.82))
    $pen.Dispose()
    Draw-CenteredText $g '무영로점' $malgun 48 ([System.Drawing.FontStyle]::Bold) $white ($w * 0.86) ($h * 0.36)
    Draw-CenteredText $g '24 HOURS' $malgun 31 ([System.Drawing.FontStyle]::Regular) $white ($w * 0.86) ($h * 0.68)
}


# --- Vertical blade sign ---------------------------------------------------
New-SignBitmap -Width 192 -Height 640 -Background $white -FileName 'T_SignBlade_D.png' -Draw {
    param($g, $w, $h)
    $chars = @('편', '의', '점')
    for ($i = 0; $i -lt $chars.Count; $i++) {
        Draw-CenteredText $g $chars[$i] $malgun 128 ([System.Drawing.FontStyle]::Bold) $mint ($w * 0.5) ($h * (0.18 + 0.3 * $i))
    }
}

# --- Sale poster (storefront glass) ---------------------------------------
New-SignBitmap -Width 512 -Height 704 -Background $nearWhite -FileName 'T_PosterSale_D.png' -Draw {
    param($g, $w, $h)
    $band = New-Object System.Drawing.SolidBrush($red)
    $g.FillRectangle($band, 0, 0, $w, [single]($h * 0.30))
    $band.Dispose()
    Draw-CenteredText $g '1+1' $malgun 150 ([System.Drawing.FontStyle]::Bold) $white ($w * 0.5) ($h * 0.15)
    Draw-CenteredText $g '새벽샘물' $malgun 84 ([System.Drawing.FontStyle]::Bold) $dark ($w * 0.5) ($h * 0.45)
    Draw-CenteredText $g '500mL 1,000원' $malgun 52 ([System.Drawing.FontStyle]::Regular) $red ($w * 0.5) ($h * 0.62)
    $gray = [System.Drawing.Color]::FromArgb(255, 150, 152, 150)
    for ($i = 0; $i -lt 3; $i++) {
        $pen = New-Object System.Drawing.Pen($gray, 6)
        $y = [single]($h * (0.76 + $i * 0.07))
        $g.DrawLine($pen, [single]($w * 0.16), $y, [single]($w * 0.84), $y)
        $pen.Dispose()
    }
}

# --- Fridge note (the foreshadowing) --------------------------------------
New-SignBitmap -Width 512 -Height 512 `
    -Background ([System.Drawing.Color]::FromArgb(255, 245, 228, 130)) `
    -BackgroundImagePath $stickyNotePaper `
    -FileName 'T_NoteFridge_D.png' -Draw {
    param($g, $w, $h)
    Draw-CenteredText $g '물 사 올 것' $noteFontFamily 78 ([System.Drawing.FontStyle]::Regular) $dark ($w * 0.50) ($h * 0.38)
    Draw-CenteredText $g '- 나' $noteFontFamily 55 ([System.Drawing.FontStyle]::Regular) $dark ($w * 0.67) ($h * 0.67)
}

# --- 403 entrance easter egg ----------------------------------------------
Write-CorridorEntranceSigns

# --- Building name plate over the common entrance --------------------------
New-SignBitmap -Width 320 -Height 96 -Background ([System.Drawing.Color]::FromArgb(255, 30, 36, 48)) -FileName 'T_SignVilla_D.png' -Draw {
    param($g, $w, $h)
    Draw-CenteredText $g '달빛빌라' $malgun 50 ([System.Drawing.FontStyle]::Bold) $white ($w * 0.39) ($h * 0.5)
    Draw-CenteredText $g '27-3' $malgun 24 ([System.Drawing.FontStyle]::Regular) ([System.Drawing.Color]::FromArgb(255, 150, 200, 190)) ($w * 0.845) ($h * 0.52)
}

# --- Closed neighborhood shop signs (dark before dawn) ----------------------
New-SignBitmap -Width 512 -Height 128 -Background ([System.Drawing.Color]::FromArgb(255, 24, 60, 140)) -FileName 'T_SignLaundry_D.png' -Draw {
    param($g, $w, $h)
    Draw-CenteredText $g '하나세탁소' $malgun 72 ([System.Drawing.FontStyle]::Bold) $white ($w * 0.5) ($h * 0.5)
}
New-SignBitmap -Width 512 -Height 128 -Background ([System.Drawing.Color]::FromArgb(255, 120, 40, 120)) -FileName 'T_SignHair_D.png' -Draw {
    param($g, $w, $h)
    Draw-CenteredText $g '은하미용실' $malgun 70 ([System.Drawing.FontStyle]::Bold) $white ($w * 0.5) ($h * 0.5)
}
New-SignBitmap -Width 512 -Height 128 -Background ([System.Drawing.Color]::FromArgb(255, 170, 30, 24)) -FileName 'T_SignHof_D.png' -Draw {
    param($g, $w, $h)
    Draw-CenteredText $g '왕발통닭 · 호프' $malgun 62 ([System.Drawing.FontStyle]::Bold) ([System.Drawing.Color]::FromArgb(255, 255, 230, 120)) ($w * 0.5) ($h * 0.5)
}
New-SignBitmap -Width 512 -Height 128 -Background ([System.Drawing.Color]::FromArgb(255, 20, 110, 60)) -FileName 'T_SignSuper_D.png' -Draw {
    param($g, $w, $h)
    Draw-CenteredText $g '골목슈퍼' $malgun 74 ([System.Drawing.FontStyle]::Bold) $white ($w * 0.5) ($h * 0.5)
}

New-SignBitmap -Width 512 -Height 128 -Background ([System.Drawing.Color]::FromArgb(255, 18, 40, 110)) -FileName 'T_SignPC_D.png' -Draw {
    param($g, $w, $h)
    Draw-CenteredText $g '스타PC방' $malgun 68 ([System.Drawing.FontStyle]::Bold) ([System.Drawing.Color]::FromArgb(255, 120, 220, 255)) ($w * 0.42) ($h * 0.5)
    Draw-CenteredText $g '24' $malgun 54 ([System.Drawing.FontStyle]::Bold) $white ($w * 0.88) ($h * 0.5)
}
New-SignBitmap -Width 512 -Height 128 -Background ([System.Drawing.Color]::FromArgb(255, 150, 24, 90)) -FileName 'T_SignKaraoke_D.png' -Draw {
    param($g, $w, $h)
    Draw-CenteredText $g '달빛노래방' $malgun 66 ([System.Drawing.FontStyle]::Bold) ([System.Drawing.Color]::FromArgb(255, 255, 210, 240)) ($w * 0.5) ($h * 0.5)
}

Write-FittedPrints


# --- Calendar --------------------------------------------------------------
New-SignBitmap -Width 256 -Height 320 -Background $nearWhite -FileName 'T_Calendar_D.png' -Draw {
    param($g, $w, $h)
    Draw-CenteredText $g '2024  7월' $malgun 44 ([System.Drawing.FontStyle]::Bold) $dark ($w * 0.5) ($h * 0.11)
    $gray = [System.Drawing.Color]::FromArgb(255, 150, 152, 150)
    $weekdays = @('일','월','화','수','목','금','토')
    for ($c = 0; $c -lt 7; $c++) {
        $color = if ($c -eq 0) { $red } else { $gray }
        Draw-CenteredText $g $weekdays[$c] $malgun 15 ([System.Drawing.FontStyle]::Bold) $color ($w * (0.13 + $c * 0.125)) ($h * 0.235)
    }
    for ($r = 0; $r -lt 5; $r++) {
        for ($c = 0; $c -lt 7; $c++) {
            $color = if ($c -eq 0) { $red } else { $gray }
            $pen = New-Object System.Drawing.Pen($color, 2)
            $x = [single]($w * (0.08 + $c * 0.125)); $y = [single]($h * (0.3 + $r * 0.13))
            $g.DrawRectangle($pen, $x, $y, [single]($w*0.1), [single]($h*0.09))
            $pen.Dispose()
            # 2024-07-01 was Monday. Keep the texture itself consistent with
            # the canonical Friday, July 26 incident instead of drawing a
            # decorative but contradictory generic grid.
            $day = $r * 7 + $c
            if ($r -eq 0) { $day = $c }
            if ($r -eq 0 -and $c -eq 0) { continue }
            if ($r -gt 0) { $day = $r * 7 + $c }
            if ($day -gt 31) { continue }
            $dayColor = if ($c -eq 0) { $red } else { $dark }
            Draw-CenteredText $g ([string]$day) $malgun 15 ([System.Drawing.FontStyle]::Regular) $dayColor ($w * (0.13 + $c * 0.125)) ($h * (0.345 + $r * 0.13))
            if ($day -eq 26) {
                $markPen = New-Object System.Drawing.Pen($red, 3)
                $g.DrawEllipse($markPen, [single]($x + 2), [single]($y + 2), [single]($w*0.085), [single]($h*0.075))
                $markPen.Dispose()
            }
        }
    }
}

# --- Villa fittings, from the reference photos -----------------------------
# Digital door lock: matte black slab, 3x4 keypad, a hairline touch strip.
New-SignBitmap -Width 192 -Height 512 -Background ([System.Drawing.Color]::FromArgb(255, 20, 21, 23)) -FileName 'T_DoorLock_D.png' -Draw {
    param($g, $w, $h)
    $keyBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 40, 42, 46))
    $labels = @('1','2','3','4','5','6','7','8','9','*','0','#')
    for ($row = 0; $row -lt 4; $row++) {
        for ($col = 0; $col -lt 3; $col++) {
            $cx = $w * (0.24 + 0.26 * $col)
            $cy = $h * (0.34 + 0.145 * $row)
            $g.FillEllipse($keyBrush, ($cx - 20), ($cy - 20), 40, 40)
            Draw-CenteredText $g $labels[$row * 3 + $col] $malgun 24 ([System.Drawing.FontStyle]::Regular) ([System.Drawing.Color]::FromArgb(255, 200, 205, 212)) $cx $cy
        }
    }
    $strip = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 96, 100, 106))
    $g.FillRectangle($strip, ($w * 0.2), ($h * 0.16), ($w * 0.6), 6)
    Draw-CenteredText $g 'OPEN' $malgun 20 ([System.Drawing.FontStyle]::Bold) ([System.Drawing.Color]::FromArgb(255, 120, 190, 150)) ($w * 0.5) ($h * 0.92)
    $strip.Dispose(); $keyBrush.Dispose()
}

# Distribution board door: the grey steel panel every Korean landing has.
New-SignBitmap -Width 256 -Height 384 -Background ([System.Drawing.Color]::FromArgb(255, 92, 96, 98)) -FileName 'T_MeterBox_D.png' -Draw {
    param($g, $w, $h)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 60, 63, 66), 3)
    $g.DrawRectangle($pen, 10, 10, ($w - 20), ($h - 20))
    Draw-CenteredText $g '분전반' $malgun 34 ([System.Drawing.FontStyle]::Bold) ([System.Drawing.Color]::FromArgb(255, 232, 234, 236)) ($w * 0.5) ($h * 0.16)
    Draw-CenteredText $g '취급주의' $malgun 22 ([System.Drawing.FontStyle]::Regular) ([System.Drawing.Color]::FromArgb(255, 224, 196, 60)) ($w * 0.5) ($h * 0.86)
    $pen.Dispose()
}

# Video intercom handset plate on the corridor wall and inside the unit.
New-SignBitmap -Width 256 -Height 320 -Background ([System.Drawing.Color]::FromArgb(255, 232, 234, 232)) -FileName 'T_Intercom_D.png' -Draw {
    param($g, $w, $h)
    $screen = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 26, 32, 36))
    $g.FillRectangle($screen, ($w * 0.12), ($h * 0.09), ($w * 0.76), ($h * 0.46))
    $btn = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 198, 202, 204))
    for ($i = 0; $i -lt 3; $i++) {
        $g.FillEllipse($btn, ($w * (0.2 + 0.26 * $i)), ($h * 0.66), 34, 34)
    }
    Draw-CenteredText $g '통화' $malgun 20 ([System.Drawing.FontStyle]::Regular) $dark ($w * 0.5) ($h * 0.9)
    $screen.Dispose(); $btn.Dispose()
}

# Wall switch plate: two rockers, one with the pilot dot lit.
New-SignBitmap -Width 256 -Height 256 -Background ([System.Drawing.Color]::FromArgb(255, 238, 238, 234)) -FileName 'T_SwitchPlate_D.png' -Draw {
    param($g, $w, $h)
    $rocker = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 248, 248, 245))
    $edge = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 196, 196, 190), 3)
    for ($i = 0; $i -lt 2; $i++) {
        $x = $w * (0.1 + 0.44 * $i)
        $g.FillRectangle($rocker, $x, ($h * 0.16), ($w * 0.36), ($h * 0.68))
        $g.DrawRectangle($edge, $x, ($h * 0.16), ($w * 0.36), ($h * 0.68))
    }
    $pilot = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 250, 150, 60))
    $g.FillEllipse($pilot, ($w * 0.22), ($h * 0.72), 16, 16)
    $rocker.Dispose(); $edge.Dispose(); $pilot.Dispose()
}

Write-Host 'Sign textures complete.'
