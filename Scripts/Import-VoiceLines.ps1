[CmdletBinding()]
param(
	# 목소리 제작 도구가 청취 검수 뒤 반출한 폴더. manifest.json과 VO_<줄 키>.wav가 들어 있다.
	[Parameter(Mandatory)][string]$ExportFolder,
	# ArtImport에서 반입만 해 보고 저장소에는 넣지 않는다(가져오기 설정을 확인할 때).
	[switch]$DryRun,
	# 반출 목록을 지금 소스·WAV와 대조만 하고 끝낸다. 언리얼을 띄우지 않는다.
	[switch]$ValidateOnly,
	# 대조할 프로젝트 폴더. 비우면 이 스크립트가 든 저장소다(검사가 임시 프로젝트를 줄 때 쓴다).
	[string]$ProjectRoot
)

# 청취 검수를 마친 인물 목소리를 /Game/Audio/Voice/ko/VO_<줄 키> USoundWave로 반입한다.
# 원본 WAV는 Content/SourceArt/Voice/ko에 두고, 만든 방법과 사용권은 그 옆 manifest.json에 남긴다.
# 검수자 이름 같은 내부 기록은 저장소에 옮기지 않는다. Import-AudioSamples.ps1과 같은 콘텐츠 전용
# 프로젝트(ArtImport)를 쓴다. 게임 모듈을 다시 빌드할 필요가 없고 한글 경로도 피한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot $ProjectRoot -Name 'Import-VoiceLines.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
$importRoot = Join-Path $env:LOCALAPPDATA 'IndieGame\ArtImport'
$staging = Join-Path $importRoot 'ImportVoice'
try {
$projectRoot = $ProjectRoot
$uproject = Join-Path $importRoot 'ArtImport.uproject'
$exportPath = (Resolve-Path -LiteralPath $ExportFolder).Path
$manifestPath = Join-Path $exportPath 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
	throw "반출 목록이 없다: $manifestPath"
}
if (-not $ValidateOnly -and -not (Test-Path -LiteralPath $uproject -PathType Leaf)) {
	throw "콘텐츠 전용 프로젝트가 없다: $uproject (먼저 Import-BlenderAssets.ps1을 한 번 돌려라)"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$assets = @($manifest.assets)
if ($assets.Count -eq 0) {
	throw "반출 목록에 목소리가 없다: $manifestPath"
}

# WAV의 fmt 칸을 읽는다. 앞에 다른 칸이 끼어 있어도 찾아간다.
function Get-WavFormat([byte[]]$Bytes) {
	if ($Bytes.Length -lt 44 -or [System.Text.Encoding]::ASCII.GetString($Bytes, 0, 4) -ne 'RIFF' -or
		[System.Text.Encoding]::ASCII.GetString($Bytes, 8, 4) -ne 'WAVE') {
		return $null
	}
	$offset = 12
	while ($offset + 24 -le $Bytes.Length) {
		$chunk = [System.Text.Encoding]::ASCII.GetString($Bytes, $offset, 4)
		$size = [System.BitConverter]::ToUInt32($Bytes, $offset + 4)
		if ($chunk -eq 'fmt ') {
			return [pscustomobject]@{
				Channels = [System.BitConverter]::ToUInt16($Bytes, $offset + 10)
				SampleRate = [System.BitConverter]::ToUInt32($Bytes, $offset + 12)
				Bits = [System.BitConverter]::ToUInt16($Bytes, $offset + 22)
			}
		}
		$offset += 8 + [int]$size + ([int]$size % 2)
	}
	return $null
}

# 소스의 NSLOCTEXT를 이름공간·줄 키별 문장으로 모은다. 붙여 쓴 문자열은 이어 붙이고 이스케이프는
# 제작 도구(voice_project.py)와 같게 JSON 규칙으로 푼다.
function Get-SourceLines([string]$Root) {
	$macro = [regex]::new('NSLOCTEXT\(\s*"([^"\\]*)"\s*,\s*"([^"\\]*)"\s*,\s*((?:"(?:\\.|[^"\\])*"\s*)+)\)',
		[System.Text.RegularExpressions.RegexOptions]::Singleline)
	$token = [regex]::new('"(?:\\.|[^"\\])*"')
	$index = @{}
	foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Root 'Source\IndieGame') -Recurse -File -Include *.cpp, *.h) {
		$content = [System.IO.File]::ReadAllText($file.FullName, [System.Text.UTF8Encoding]::new($false))
		foreach ($match in $macro.Matches($content)) {
			$value = (@($token.Matches($match.Groups[3].Value) | ForEach-Object { $_.Value | ConvertFrom-Json })) -join ''
			$key = $match.Groups[1].Value + '/' + $match.Groups[2].Value
			if (-not $index.ContainsKey($key)) { $index[$key] = New-Object System.Collections.Generic.List[string] }
			$index[$key].Add($value)
		}
	}
	return $index
}

function Get-TextSha256([string]$Text) {
	$sha = [System.Security.Cryptography.SHA256]::Create()
	try { return (($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Text)) | ForEach-Object { $_.ToString('x2') }) -join '') }
	finally { $sha.Dispose() }
}

# 검수한 파일이 그대로인지, 게임 규격(모노 48 kHz·24비트)인지, 반출한 뒤 그 줄의 대사가 바뀌지 않았는지 본다.
$sourceLines = Get-SourceLines $projectRoot
foreach ($asset in $assets) {
	if ($asset.asset_name -notmatch '^VO_[A-Za-z0-9_]+$') {
		throw "목소리 이름이 VO_<줄 키>가 아니다: $($asset.asset_name)"
	}
	$wav = Join-Path $exportPath ($asset.asset_name + '.wav')
	if (-not (Test-Path -LiteralPath $wav -PathType Leaf)) {
		throw "WAV가 없다: $wav"
	}
	$hash = (Get-FileHash -LiteralPath $wav -Algorithm SHA256).Hash.ToLowerInvariant()
	if ($hash -ne ([string]$asset.sha256).ToLowerInvariant()) {
		throw "검수한 뒤 바뀐 파일이다: $wav"
	}
	$format = Get-WavFormat ([System.IO.File]::ReadAllBytes($wav))
	if (-not $format -or $format.Channels -ne 1) {
		throw "모노 WAV가 아니다: $wav"
	}
	if ($format.SampleRate -ne 48000 -or $format.Bits -ne 24) {
		throw "게임 규격(48 kHz·24비트)이 아니다($($format.SampleRate) Hz, $($format.Bits)비트). 제작 도구의 project export로 다시 반출한다: $wav"
	}
	$lineKey = [string]$asset.namespace + '/' + [string]$asset.line_id
	if (-not $sourceLines.ContainsKey($lineKey)) {
		throw "그 줄 키가 지금 소스에 없다(대사가 바뀌었으면 다시 만들고 검수한다): $lineKey"
	}
	$current = @($sourceLines[$lineKey] | Select-Object -Unique)
	if ($current.Count -ne 1) {
		throw "같은 줄 키에 문장이 여럿이다: $lineKey"
	}
	if ($current[0] -cne [string]$asset.subtitle_text) {
		throw "반출한 뒤 대사가 바뀌었다(다시 만들고 검수한다): $lineKey`n  반출: $($asset.subtitle_text)`n  지금: $($current[0])"
	}
	$expectedHash = if ($asset.PSObject.Properties['source_text_sha256']) { [string]$asset.source_text_sha256 } else { '' }
	if ($expectedHash -and (Get-TextSha256 $current[0]) -ne $expectedHash.ToLowerInvariant()) {
		throw "반출 목록의 원문 해시가 지금 대사와 다르다: $lineKey"
	}
}
if ($ValidateOnly) {
	Write-Host "[VOICE_IMPORT] 대조 통과: $($assets.Count)줄(소스 원문·해시, WAV 해시·규격)"
	$igArtifactSucceeded = $true
	return
}

if (Test-Path -LiteralPath $staging) {
	Remove-Item -LiteralPath $staging -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $staging | Out-Null
foreach ($asset in $assets) {
	Copy-Item -LiteralPath (Join-Path $exportPath ($asset.asset_name + '.wav')) -Destination $staging
}
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $staging 'manifest.json')
$repoVoice = Join-Path $projectRoot 'Content\Audio\Voice'
$importVoice = Join-Path $importRoot 'Content\Audio\Voice'
if (Test-Path -LiteralPath $repoVoice -PathType Container) {
	& robocopy.exe $repoVoice $importVoice /E /COPY:DAT /R:2 /W:1 /XJ /NFL /NDL /NP /NJH /NJS | Out-Null
	if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE): $repoVoice -> $importVoice" }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'import_voice_lines.py') -Destination (Join-Path $importRoot 'Scripts') -Force

$editor = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Resolve-UnrealEditor.ps1') -ProjectPath $uproject -Commandlet
if (-not $editor -or -not (Test-Path -LiteralPath $editor)) {
	throw "UnrealEditor-Cmd.exe를 찾지 못했다: $editor"
}
$logPath = Join-Path $importRoot ('Saved\Logs\VoiceImport_{0}.log' -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
$env:IG_VOICE_SOURCE = $staging
& $editor $uproject -unattended -nop4 -nosplash -nullrhi -nosound -RenderOffscreen -stdout -FullStdOutLogOutput `
	"-abslog=$logPath" "-ExecutePythonScript=$(Join-Path $importRoot 'Scripts\import_voice_lines.py')" | Out-Null
$editorExit = $LASTEXITCODE
if ($editorExit -ne 0 -or -not (Select-String -LiteralPath $logPath -Pattern 'VOICE_IMPORT PASS' -Quiet)) {
	throw "목소리 반입 실패(exit $editorExit): $logPath"
}
Select-String -LiteralPath $logPath -Pattern '\[VOICE_IMPORT\]' | ForEach-Object { Write-Host $_.Line }

if ($DryRun) {
	Write-Host "반입만 해 보았다(저장소는 그대로): $importVoice"
}
else {
	# 원본은 SourceArt에, 게임 자산은 Content/Audio/Voice에 둔다. 다른 목소리는 지우지 않는다.
	$sourceArt = Join-Path $projectRoot 'Content\SourceArt\Voice\ko'
	New-Item -ItemType Directory -Force -Path $sourceArt | Out-Null
	foreach ($asset in $assets) {
		Copy-Item -LiteralPath (Join-Path $staging ($asset.asset_name + '.wav')) -Destination $sourceArt -Force
	}
	& robocopy.exe $importVoice $repoVoice /E /COPY:DAT /R:2 /W:1 /XJ /NFL /NDL /NP /NJH /NJS | Out-Null
	if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE): $importVoice -> $repoVoice" }

	# 만든 방법과 사용권. 검수자 이름, 점수표, 연기 지시 원문은 제작 도구 쪽에 남기고 여기에는 옮기지 않는다.
	$provenancePath = Join-Path $projectRoot 'Content\SourceArt\Voice\manifest.json'
	$lines = [ordered]@{}
	if (Test-Path -LiteralPath $provenancePath -PathType Leaf) {
		$previous = Get-Content -LiteralPath $provenancePath -Raw -Encoding UTF8 | ConvertFrom-Json
		foreach ($line in @($previous.lines)) { $lines[$line.asset_name] = $line }
	}
	$takesRoot = Join-Path (Split-Path -Parent (Split-Path -Parent $exportPath)) 'takes'
	foreach ($asset in $assets) {
		$source = if ($asset.PSObject.Properties['provenance']) { $asset.provenance } else { $null }
		if ($source) {
			# 반출 목록이 출처를 들고 온다(Drama 3 등). 확인할 수 없는 서버 모델 판은 비워 두고 그 사실을 적는다.
			$lines[$asset.asset_name] = [ordered]@{
				asset_name = $asset.asset_name
				line_id = $asset.line_id
				character = $asset.character
				subtitle_text = $asset.subtitle_text
				spoken_text = $asset.spoken_text
				duration_seconds = $asset.duration_seconds
				sha256 = $asset.sha256
				release = $manifest.release
				method = $asset.method
				service = $source.service
				model = $source.model
				model_revision = $source.model_revision
				model_revision_note = $source.model_revision_note
				voice_id = $source.voice_id
				voice_title = $source.voice_title
				voice_source_class = $source.voice_source_class
				voice_license_note = $source.voice_license_note
				generated_utc = $source.generated_utc
				api_original_sha256 = $source.api_original_sha256
				format = $manifest.format
				loudness = $asset.mastering
				listening_review = 'passed'
				gameplay_review = 'pending'
			}
			continue
		}
		$take = Get-ChildItem -LiteralPath $takesRoot -Recurse -Filter ($asset.take_id + '.json') -ErrorAction SilentlyContinue |
			Select-Object -First 1
		$model = $null
		$modelRevision = $null
		$modelLicense = $null
		$method = $null
		if ($take) {
			$record = Get-Content -LiteralPath $take.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
			$model = $record.model.repo
			$modelRevision = $record.model.revision
			$modelLicense = $record.model.license
			$method = $record.method
		}
		$lines[$asset.asset_name] = [ordered]@{
			asset_name = $asset.asset_name
			line_id = $asset.line_id
			subtitle_text = $asset.subtitle_text
			spoken_text = $asset.spoken_text
			duration_seconds = $asset.duration_seconds
			sha256 = $asset.sha256
			release = $manifest.release
			model = $model
			model_revision = $modelRevision
			model_license = $modelLicense
			method = $method
			listening_review = 'passed'
			gameplay_review = 'pending'
		}
	}
	$provenance = [ordered]@{
		note = '게임에 넣은 인물 목소리. 미리 만들어 청취 검수를 마친 줄만 들어간다. 실존 인물이나 다른 작품의 목소리를 쓰지 않는다. 원본은 모노 48 kHz 24비트.'
		lines = @($lines.Values)
	}
	[System.IO.File]::WriteAllText($provenancePath, ($provenance | ConvertTo-Json -Depth 8), [System.Text.UTF8Encoding]::new($false))
	Write-Host "반입했다: $($assets.Count)줄 -> $repoVoice"
}
$igArtifactSucceeded = $true
}
finally {
	Remove-Item Env:IG_VOICE_SOURCE -ErrorAction SilentlyContinue
	# 이번 실행에서 만든 사본만 지운다. 원본은 제작 도구의 반출 폴더에 그대로 있다.
	if (Test-Path -LiteralPath $staging) {
		Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
	}
	Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
