[CmdletBinding()]
param(
	# 목소리 제작 도구가 청취 검수 뒤 반출한 폴더. manifest.json과 VO_<줄 키>.wav가 들어 있다.
	[Parameter(Mandatory)][string]$ExportFolder,
	# ArtImport에서 반입만 해 보고 저장소에는 넣지 않는다(가져오기 설정을 확인할 때).
	[switch]$DryRun
)

# 청취 검수를 마친 인물 목소리를 /Game/Audio/Voice/ko/VO_<줄 키> USoundWave로 반입한다.
# 원본 WAV는 Content/SourceArt/Voice/ko에 두고, 만든 방법과 사용권은 그 옆 manifest.json에 남긴다.
# 검수자 이름 같은 내부 기록은 저장소에 옮기지 않는다. Import-AudioSamples.ps1과 같은 콘텐츠 전용
# 프로젝트(ArtImport)를 쓴다. 게임 모듈을 다시 빌드할 필요가 없고 한글 경로도 피한다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Import-VoiceLines.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
$importRoot = Join-Path $env:LOCALAPPDATA 'IndieGame\ArtImport'
$staging = Join-Path $importRoot 'ImportVoice'
try {
$projectRoot = Split-Path -Parent $PSScriptRoot
$uproject = Join-Path $importRoot 'ArtImport.uproject'
$exportPath = (Resolve-Path -LiteralPath $ExportFolder).Path
$manifestPath = Join-Path $exportPath 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
	throw "반출 목록이 없다: $manifestPath"
}
if (-not (Test-Path -LiteralPath $uproject -PathType Leaf)) {
	throw "콘텐츠 전용 프로젝트가 없다: $uproject (먼저 Import-BlenderAssets.ps1을 한 번 돌려라)"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$assets = @($manifest.assets)
if ($assets.Count -eq 0) {
	throw "반출 목록에 목소리가 없다: $manifestPath"
}

# 검수한 파일이 그대로인지, 한 사람 목소리(모노)인지, 그 줄 키가 지금 소스에 있는지 본다.
$sourceText = (Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Source\IndieGame') -Recurse -Include *.cpp, *.h |
	ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 }) -join "`n"
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
	$header = [System.IO.File]::ReadAllBytes($wav)
	if ($header.Length -lt 44 -or [System.BitConverter]::ToInt16($header, 22) -ne 1) {
		throw "모노 WAV가 아니다: $wav"
	}
	$keyPattern = 'NSLOCTEXT\(\s*"[^"]+",\s*"' + [regex]::Escape([string]$asset.line_id) + '"'
	if ($sourceText -notmatch $keyPattern) {
		throw "그 줄 키가 지금 소스에 없다(대사가 바뀌었으면 다시 만들고 검수한다): $($asset.line_id)"
	}
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

	# 만든 방법과 사용권. 검수자 이름과 점수표 원본은 제작 도구 쪽에 남기고 여기에는 옮기지 않는다.
	$provenancePath = Join-Path $projectRoot 'Content\SourceArt\Voice\manifest.json'
	$lines = [ordered]@{}
	if (Test-Path -LiteralPath $provenancePath -PathType Leaf) {
		$previous = Get-Content -LiteralPath $provenancePath -Raw -Encoding UTF8 | ConvertFrom-Json
		foreach ($line in @($previous.lines)) { $lines[$line.asset_name] = $line }
	}
	$takesRoot = Join-Path (Split-Path -Parent (Split-Path -Parent $exportPath)) 'takes'
	foreach ($asset in $assets) {
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
		note = '게임에 넣은 인물 목소리. 미리 만들어 청취 검수를 마친 줄만 들어간다. 실존 인물이나 다른 작품의 목소리를 쓰지 않는다.'
		lines = @($lines.Values)
	}
	[System.IO.File]::WriteAllText($provenancePath, ($provenance | ConvertTo-Json -Depth 6), [System.Text.UTF8Encoding]::new($false))
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
