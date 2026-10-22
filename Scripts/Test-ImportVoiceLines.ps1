[CmdletBinding()]
param()

# Import-VoiceLines.ps1의 반입 전 대조를 임시 프로젝트로 검사한다. 언리얼은 띄우지 않는다(-ValidateOnly).
# 반출한 뒤 같은 줄 키의 대사가 바뀌면 반입을 막는지, WAV 해시·규격이 틀리면 막는지 본다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$importScript = Join-Path $PSScriptRoot 'Import-VoiceLines.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('IGVoiceImportTest-' + [Guid]::NewGuid().ToString('N'))
$project = Join-Path $fixture 'Project'
$export = Join-Path $fixture 'Export'
$sourceFile = Join-Path $project 'Source\IndieGame\Dialogue.cpp'
$checks = 0
$utf8 = [System.Text.UTF8Encoding]::new($false)

function New-TestWav([string]$Path, [int]$Rate, [int]$Bits) {
	$frames = $Rate / 10
	$width = $Bits / 8
	$data = New-Object byte[] ($frames * $width)
	for ($i = 0; $i -lt $frames; $i++) {
		$value = [int]([Math]::Sin(2 * [Math]::PI * 220 * $i / $Rate) * 0.1 * ([Math]::Pow(2, $Bits - 1) - 1))
		$bytes = [BitConverter]::GetBytes($value)
		[Array]::Copy($bytes, 0, $data, $i * $width, $width)
	}
	$stream = [IO.MemoryStream]::new()
	$writer = [IO.BinaryWriter]::new($stream)
	$writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
	$writer.Write([uint32](36 + $data.Length))
	$writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
	$writer.Write([uint32]16)
	$writer.Write([uint16]1)
	$writer.Write([uint16]1)
	$writer.Write([uint32]$Rate)
	$writer.Write([uint32]($Rate * $width))
	$writer.Write([uint16]$width)
	$writer.Write([uint16]$Bits)
	$writer.Write([Text.Encoding]::ASCII.GetBytes('data'))
	$writer.Write([uint32]$data.Length)
	$writer.Write($data)
	$writer.Flush()
	[IO.File]::WriteAllBytes($Path, $stream.ToArray())
	$writer.Dispose()
}

function Get-TextSha256([string]$Text) {
	$sha = [System.Security.Cryptography.SHA256]::Create()
	try { return (($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text)) | ForEach-Object { $_.ToString('x2') }) -join '') }
	finally { $sha.Dispose() }
}

function Write-Case([string]$Source, [object[]]$Assets) {
	[IO.File]::WriteAllText($sourceFile, $Source, $utf8)
	$manifest = [ordered]@{ project = 'fixture'; release = 'fixture01'; format = 'mono 48 kHz PCM 24-bit'; assets = $Assets }
	[IO.File]::WriteAllText((Join-Path $export 'manifest.json'), ($manifest | ConvertTo-Json -Depth 6), $utf8)
}

function New-Asset([string]$Key, [string]$Text, [string]$Wav, [string]$TextHash = '') {
	$path = Join-Path $export ("VO_$Key.wav")
	if ($Wav -ne $path) { Copy-Item -LiteralPath $Wav -Destination $path -Force }
	return [ordered]@{
		asset_name = "VO_$Key"; line_id = $Key; namespace = 'Game'; subtitle_text = $Text
		sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
		source_text_sha256 = $(if ($TextHash) { $TextHash } else { Get-TextSha256 $Text })
	}
}

function Invoke-Validate {
	# 실패하는 경우를 일부러 돌리므로 자식의 오류 출력이 이 스크립트를 멈추지 않게 한다.
	$ErrorActionPreference = 'Continue'
	$output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $importScript -ExportFolder $export -ProjectRoot $project -ValidateOnly 2>&1 |
		ForEach-Object { "$_" }
	return [pscustomobject]@{ Code = $LASTEXITCODE; Text = ($output -join "`n") }
}

function Assert-Result([string]$Name, [object]$Result, [bool]$ShouldPass, [string]$Expected) {
	$passed = $Result.Code -eq 0
	if ($passed -ne $ShouldPass -or ($Expected -and -not $Result.Text.Contains($Expected))) {
		throw "목소리 반입 대조 검사 실패($Name): 종료 코드 $($Result.Code)`n$($Result.Text)"
	}
	$script:checks++
}

try {
	[void][IO.Directory]::CreateDirectory((Split-Path -Parent $sourceFile))
	[void][IO.Directory]::CreateDirectory($export)
	# 자동 정리 기록은 uproject가 있는 폴더만 받는다. 임시 프로젝트에도 빈 자리표시를 둔다.
	[IO.File]::WriteAllText((Join-Path $project 'IndieGame.uproject'), '{}', $utf8)
	$good = Join-Path $fixture 'good.wav'
	$wrong = Join-Path $fixture 'wrong.wav'
	New-TestWav $good 48000 24
	New-TestWav $wrong 44100 16
	$source = 'NSLOCTEXT("Game", "LineA", "언니, 저 나린이에요.")' + "`n" + 'NSLOCTEXT("Game", "LineB", "여기 " "있어요.")' + "`n"

	Write-Case $source @((New-Asset 'LineA' '언니, 저 나린이에요.' $good), (New-Asset 'LineB' '여기 있어요.' $good))
	Assert-Result '그대로인 대사와 붙여 쓴 문자열' (Invoke-Validate) $true '대조 통과'

	Write-Case ($source -replace '저 나린이에요', '나 나린이야') @((New-Asset 'LineA' '언니, 저 나린이에요.' $good))
	Assert-Result '반출 뒤 바뀐 대사' (Invoke-Validate) $false '반출한 뒤 대사가 바뀌었다'

	Write-Case 'NSLOCTEXT("Game", "Other", "다른 줄")' @((New-Asset 'LineA' '언니, 저 나린이에요.' $good))
	Assert-Result '없어진 줄 키' (Invoke-Validate) $false '그 줄 키가 지금 소스에 없다'

	Write-Case $source @((New-Asset 'LineA' '언니, 저 나린이에요.' $good ('0' * 64)))
	Assert-Result '원문 해시 불일치' (Invoke-Validate) $false '원문 해시'

	Write-Case $source @((New-Asset 'LineA' '언니, 저 나린이에요.' $wrong))
	Assert-Result '게임 규격이 아닌 WAV' (Invoke-Validate) $false '게임 규격'

	$asset = New-Asset 'LineA' '언니, 저 나린이에요.' $good
	$asset.sha256 = '1' * 64
	Write-Case $source @($asset)
	Assert-Result '검수 뒤 바뀐 WAV' (Invoke-Validate) $false '검수한 뒤 바뀐 파일'

	Write-Host "VOICE_IMPORT_TEST PASS checks=$checks"
}
finally {
	if (Test-Path -LiteralPath $fixture) {
		$resolved = [IO.Path]::GetFullPath($fixture)
		if ($resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()), [StringComparison]::OrdinalIgnoreCase)) {
			Remove-Item -LiteralPath $resolved -Recurse -Force
		}
	}
}
