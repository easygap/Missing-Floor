[CmdletBinding()]
param()
# 간판과 포장의 한글은 글꼴로 조판한다. 상호와 제품명은 영수증 데이터와 맞춘다.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$retailOut = Join-Path (Split-Path -Parent $PSScriptRoot) 'Content/SourceArt'
function Text-At($g, $text, $size, $x, $y, $color, $bold = $false) {
    $style = if ($bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
    $font = [System.Drawing.Font]::new('Malgun Gothic', $size, $style, [System.Drawing.GraphicsUnit]::Pixel)
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml($color))
    $g.DrawString($text, $font, $brush, [single]$x, [single]$y)
    $brush.Dispose(); $font.Dispose()
}
function Sheet($name, $width, $height, $draw) {
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([System.Drawing.Color]::White)
    & $draw $g $width $height
    $bitmap.Save((Join-Path $retailOut $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bitmap.Dispose()
}
Sheet 'T_SignMain_D.png' 2048 256 {
    param($g, $w, $h)
    $ink = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#16483E'))
    $g.FillRectangle($ink, 0, 0, $w, $h)
    $ink.Dispose()
    Text-At $g '새벽24' 180 112 6 '#FFFFFF' $true
    Text-At $g '편의점' 88 875 55 '#D9E8CF' $true
    Text-At $g '무영로점' 62 1545 42 '#FFFFFF' $true
    Text-At $g '24시간 영업' 42 1548 137 '#E1E8DF'
}
Sheet 'T_SignBlade_D.png' 256 512 {
    param($g, $w, $h)
    Text-At $g '새벽' 108 13 39 '#16483E' $true
    Text-At $g '24' 177 16 186 '#16483E' $true
    Text-At $g '편의점' 46 48 406 '#16483E'
}
$specs = @(
    @{ File='T_LabelWater_D.png'; Name='새벽샘물'; Color='#174E83'; Type='먹는샘물' },
    @{ File='T_LabelWater1L_D.png'; Name='한강수'; Color='#25637D'; Type='먹는샘물'; Volume='1L' },
    @{ File='T_LabelWater2L_D.png'; Name='맑은산'; Color='#286346'; Type='먹는샘물'; Volume='2L' },
    @{ File='T_LabelGreenTea_D.png'; Name='녹차'; Color='#28632B'; Type='녹차 음료' },
    @{ File='T_LabelBarley_D.png'; Name='보리차'; Color='#95692B'; Type='보리차 음료' },
    @{ File='T_LabelSoda_D.png'; Name='탄산수'; Color='#176A8D'; Type='탄산수' }
)
foreach ($spec in $specs) {
    Sheet $spec.File 1024 256 {
        param($g, $w, $h)
        $band = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml($spec.Color))
        $g.FillRectangle($band, 0, 232, $w, 24)
        $band.Dispose()
        $titleWidth = if ($spec.Name.Length -eq 4) { 354 } else { 410 }
        Text-At $g $spec.Name 79 $titleWidth 35 $spec.Color $true
        $volume = if ($spec.Volume) { $spec.Volume } else { '500mL' }
        Text-At $g $volume 32 447 143 $spec.Color
        Text-At $g $spec.Type 20 22 30 '#3A4144' $true
        Text-At $g '직사광선을 피해 보관하세요.' 16 22 67 '#42484B'
        Text-At $g '개봉 후에는 냉장 보관하세요.' 16 22 93 '#42484B'
        Text-At $g '분리배출  ·  PET' 17 22 174 '#42484B'
        $bar = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(34, 39, 43))
        for ($i=0; $i -lt 52; $i++) {
            $g.FillRectangle($bar, (814+$i*3), 110, (1+$i%2), 69)
        }
        $bar.Dispose()
        Text-At $g '제품 정보' 19 822 48 '#42484B'
        Text-At $g $volume 21 852 189 '#42484B'
    }
}
Sheet 'T_NeighborhoodDelivery_D.png' 768 1024 {
    param($g, $w, $h)
    Text-At $g '택배 배송 안내' 70 87 80 '#252B29' $true
    Text-At $g '달빛빌라 옥탑' 52 132 256 '#252B29' $true
    Text-At $g '부재 시 앞쪽 편의점에' 43 94 397 '#252B29'
    Text-At $g '맡겨 주세요.' 43 225 465 '#252B29'
    Text-At $g '새벽24 무영로점' 45 175 608 '#16483E' $true
    Text-At $g '장기 보관은 어렵습니다.' 31 168 779 '#484E4B'
    Text-At $g '달빛빌라 관리실  목한수' 30 174 886 '#484E4B'
}
Sheet 'T_RetailTobaccoAd_D.png' 1024 256 {
    param($g, $w, $h)
    $g.Clear([System.Drawing.ColorTranslator]::FromHtml('#102C43'))
    Text-At $g 'SEORIM' 96 43 26 '#F5F4E9' $true
    Text-At $g 'BLUE' 48 51 152 '#BDCFD5'
    $pack = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#E2E3E0'))
    $g.FillRectangle($pack, 823, 25, 131, 201)
    $pack.Dispose()
    Text-At $g '서림' 35 840 143 '#123D5C' $true
    Text-At $g '흡연은 건강에 해롭습니다' 20 484 210 '#D5DDDF'
}
$prices = @(
    @{Key='Potato';Name='감자칩';Price='1,700원'},
    @{Key='Shrimp';Name='새우스낵';Price='1,500원'},
    @{Key='Corn';Name='콘스낵';Price='1,500원'},
    @{Key='CupBeef';Name='육개장 사발면';Price='1,000원'},
    @{Key='CupKimchi';Name='김치라면';Price='1,000원'},
    @{Key='Biscuit';Name='초코비스킷';Price='1,800원'},
    @{Key='Water';Name='새벽샘물 500mL';Price='1,000원'},
    @{Key='Soda';Name='탄산수 500mL';Price='1,200원'},
    @{Key='Barley';Name='보리차 500mL';Price='1,500원'},
    @{Key='GreenTea';Name='녹차 500mL';Price='1,500원'}
)
foreach ($price in $prices) {
    Sheet ('T_RetailPrice'+$price.Key+'_D.png') 512 128 {
        param($g, $w, $h)
        Text-At $g $price.Name 28 14 6 '#252B29'
        Text-At $g $price.Price 59 236 42 '#151A17' $true
        Text-At $g '새벽24' 18 17 95 '#4E5851'
    }
}
Write-Host 'RETAIL_GRAPHICS PASS'
