[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$assertions = 0

function Assert-True {
	param(
		[bool]$Condition,
		[string]$Message
	)
	$script:assertions++
	if (-not $Condition) {
		throw $Message
	}
}

function Assert-ContainsAll {
	param(
		[string]$Text,
		[string[]]$Tokens,
		[string]$Context
	)
	foreach ($token in $Tokens) {
		Assert-True $Text.Contains($token) "$Context is missing '$token'."
	}
}

function Read-ProjectText {
	param([string]$RelativePath)
	return Get-Content -Raw -Encoding UTF8 -LiteralPath (
		Join-Path $projectRoot $RelativePath)
}

$rawPath = Join-Path $projectRoot (
	'Content\SourceArt\AI\TextureCaptureMercyNotePaper_D.png')
$derivedPath = Join-Path $projectRoot (
	'Content\SourceArt\T_CaptureMercyNote_D.png')
$capturePath = Join-Path $projectRoot (
	'Docs\Media\m65-capture-mercy-note.png')
foreach ($path in @($rawPath, $derivedPath, $capturePath)) {
	Assert-True (Test-Path -LiteralPath $path -PathType Leaf) `
		"M6.5 mercy-note image is missing: $path"
}

$rawHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $rawPath).Hash
Assert-True ($rawHash -eq `
	'FEC6D7DDF6611A8E355FDABDE6B1B4AD607ADCB66EF66FAB35238E4AED7FFB99') `
	'M6.5 ImageGen paper source changed without a contract update.'

$rawImage = [System.Drawing.Image]::FromFile($rawPath)
try {
	Assert-True ($rawImage.Width -ge 1024 -and $rawImage.Height -ge 1024) `
		'M6.5 ImageGen paper source must remain at least 1K.'
}
finally {
	$rawImage.Dispose()
}
$derivedImage = [System.Drawing.Image]::FromFile($derivedPath)
try {
	Assert-True ($derivedImage.Width -eq 1024 -and $derivedImage.Height -eq 640) `
		'M6.5 in-world note must remain 1024x640 for the 18:11 mesh.'
}
finally {
	$derivedImage.Dispose()
}
$captureImage = [System.Drawing.Image]::FromFile($capturePath)
try {
	Assert-True ($captureImage.Width -eq 1920 -and $captureImage.Height -eq 1080) `
		'M6.5 visual-review still must remain a full-HD in-game capture.'
}
finally {
	$captureImage.Dispose()
}

$loopHeader = Read-ProjectText 'Source\IndieGame\Entity\IGNightLoopDirector.h'
$loopSource = Read-ProjectText 'Source\IndieGame\Entity\IGNightLoopDirector.cpp'
$audioHeader = Read-ProjectText 'Source\IndieGame\Audio\IGToneSequenceSoundWave.h'
$audioSource = Read-ProjectText 'Source\IndieGame\Audio\IGToneSequenceSoundWave.cpp'
$greyboxHeader = Read-ProjectText 'Source\IndieGame\Entity\IGListenerGreyboxDirector.h'
$greyboxSource = Read-ProjectText 'Source\IndieGame\Entity\IGListenerGreyboxDirector.cpp'
$meshScript = Read-ProjectText 'Scripts\generate_meshes.py'
$signScript = Read-ProjectText 'Scripts\Create-SignTextures.ps1'
$surfaceScript = Read-ProjectText 'Scripts\generate_surface_textures.py'
$materialScript = Read-ProjectText 'Scripts\create_textured_materials.py'
$auditScript = Read-ProjectText 'Scripts\validate_baked_art_assets.py'
$buildScript = Read-ProjectText 'Scripts\Build-ArtAssets.ps1'
$greyboxBatch = Read-ProjectText 'Scripts\Run-MissingFloor-Greybox.bat'
$story = Read-ProjectText 'Docs\STORY_BIBLE_MISSING_FLOOR.md'
$promptRecord = Read-ProjectText 'Docs\IMAGEGEN_PROMPTS_2026-08-12.md'

Assert-ContainsAll $loopHeader @(
	'virtual void Tick(float DeltaSeconds) override;',
	'bool IsMercyNoteVisible() const;',
	'bool IsMercyNoteSliding() const',
	'FVector GetMercyNoteLocation() const;',
	'PrimeMercyNoteCaptureProbe',
	'PlayMercyNoteCapturePreview',
	'TObjectPtr<UStaticMeshComponent> MercyNote',
	'FTimerHandle MercyNoteRevealTimer'
) 'M6.5 night-loop interface'

Assert-ContainsAll $loopSource @(
	'MercyNoteCaptureThreshold = 5',
	'MercyNoteRevealDelaySeconds = 0.18f',
	'MercyNoteSlideSeconds = 0.82f',
	'MercyNoteStartLocation(-150.0f, -239.0f, 900.12f)',
	'MercyNoteRestLocation(-150.0f, -269.5f, 900.12f)',
	'PrimaryActorTick.bStartWithTickEnabled = false',
	'CaptureCount = FMath::Max(CaptureCount, Narrative->GetCaptureCount())',
	'SetMercyNoteAtRest();',
	'ResetSceneVelocity();',
	'QueueMercyNoteReveal();',
	'Alpha * Alpha * (3.0f - 2.0f * Alpha)',
	'/Game/Meshes/SM_CaptureMercyNote.SM_CaptureMercyNote',
	'/Game/Prototype/Materials/M_CaptureMercyNote.',
	'UCollisionProfile::NoCollision_ProfileName',
	'SetGenerateOverlapEvents(false)',
	'SetCanEverAffectNavigation(false)',
	'SetCastShadow(false)',
	'SetCullDistance(850.0f)',
	'MissingFloor.CaptureMercyNote',
	'CreatePaperDoorSlide(this)',
	'EIGAudioBus::World'
) 'M6.5 world note reveal'

$slideStart = $loopSource.IndexOf('void AIGNightLoopDirector::BeginMercyNoteSlide()')
$slideEnd = $loopSource.IndexOf(
	'bool AIGNightLoopDirector::SpawnCaptureHandprint',
	$slideStart)
Assert-True ($slideStart -ge 0 -and $slideEnd -gt $slideStart) `
	'M6.5 note reveal block is malformed.'
$slideBlock = $loopSource.Substring($slideStart, $slideEnd - $slideStart)
foreach ($forbiddenUi in @(
	'AIGHorrorHUD',
	'PushThought',
	'PushAudioCaption',
	'SetInteraction',
	'InteractionPrompt'
)) {
	Assert-True (-not $slideBlock.Contains($forbiddenUi)) `
		"M6.5 mercy note must remain a world prop without UI: $forbiddenUi"
}

Assert-ContainsAll $audioHeader @(
	'CreatePaperDoorSlide(UObject* Outer)'
) 'M6.5 sound interface'
Assert-ContainsAll $audioSource @(
	'CreatePaperDoorSlide(',
	'TEXT("IGPaperDoorSlide")',
	'0.000f, 0.34f, 2850.0f',
	'0.110f, 0.58f, 930.0f',
	'0.748f, 0.040f, 720.0f'
) 'M6.5 paper-slide sound'

Assert-ContainsAll $meshScript @(
	'def build_capture_mercy_note():',
	'width = 18.0',
	'depth = 11.0',
	'0.22 * leading_release * leading_release',
	'uvs.append((1.0 - u, v))',
	'"SM_CaptureMercyNote"',
	'builders = (build_capture_mercy_note,)'
) 'M6.5 authored paper mesh'
Assert-ContainsAll $signScript @(
	'AI\TextureCaptureMercyNotePaper_D.png',
	"-FileName 'T_CaptureMercyNote_D.png'",
	"Draw-CenteredText `$g '소리를 줄여라.'",
	"Draw-CenteredText `$g '걔는 눈이 없어.'"
) 'M6.5 exact in-world lettering'
Assert-ContainsAll $surfaceScript @(
	'"T_CaptureMercyNote_D"',
	'CORRIDOR_SIGNAGE_TEXTURE_NAMES',
	'"T_CaptureMercyNote_D",'
) 'M6.5 texture import'
Assert-ContainsAll $materialScript @(
	'"M_CaptureMercyNote": {',
	'"tex_asset": "T_CaptureMercyNote_D", "rough": 0.92, "two_sided": True',
	'"M_CaptureMercyNote",'
) 'M6.5 material import'
Assert-ContainsAll $auditScript @(
	'"SM_CaptureMercyNote"',
	'"SM_CaptureMercyNote": 60',
	'"M_CaptureMercyNote": "T_CaptureMercyNote_D"',
	'"M_CaptureMercyNote",'
) 'M6.5 baked-asset audit'
Assert-ContainsAll $buildScript @(
	'Content\Meshes\SM_CaptureMercyNote.uasset',
	'Content\Prototype\Textures\T_CaptureMercyNote_D.uasset',
	'Content\Prototype\Materials\M_CaptureMercyNote.uasset',
	"SuccessPattern = '\[MESHGEN\] complete: 1/1 meshes'",
	# 여섯 장은 401·402·403 문패, 메모 둘, §14 CCTV 채널 5의 AUX 5 라벨이다.
	# 이 그룹은 세계 안의 작은 글자 데칼을 함께 굽는 자리이며, 라벨만 따로
	# 굽는 경로를 새로 만들면 그 라벨이 지시하는 화면과 다른 시점에 갱신될 수 있다.
	"SuccessPattern = '\[IndieGame\] Imported 6 textures'"
) 'M6.5 targeted art build'

Assert-ContainsAll $greyboxHeader @(
	'bMercyNoteProbeRequested = false'
) 'M6.5 runtime probe state'
Assert-ContainsAll $greyboxSource @(
	'IGM65MercyNoteProbe',
	'NightLoop->PrimeMercyNoteCaptureProbe();',
	'MISSINGFLOOR_M65_MERCY_NOTE PASS',
	'capture=5 slide=1 world_note=1 ui=0',
	'FMath::Clamp(StartStep, 0, 18)',
	'CaptureBeginBurst(TEXT("mercy-note"), 2.35f);',
	'NightLoop->PlayMercyNoteCapturePreview();',
	'TEXT("r.AntiAliasingMethod 1")',
	'ActionB(3.1f)',
	'CaptureShot(TEXT("m65-capture-mercy-note"))'
) 'M6.5 runtime and visual probe'
Assert-ContainsAll $greyboxBatch @(
	'-IGListenerGreyboxProbe %*',
	'-IGM65MercyNoteProbe',
	'MISSINGFLOOR_M65_MERCY_NOTE PASS'
) 'M6.5 command-line probe handoff'

Assert-ContainsAll $story @(
	'5회 연속 포획에 한해 다음 기상에서 황순금의 문 아래로 메모가 밀려 나온다:',
	'"소리를 줄여라. 걔는 눈이 없어."',
	'그것도 UI가',
	'세계 안의 종이다.'
) 'M6.5 story and UX boundary'
Assert-ContainsAll $promptRecord @(
	'내장 ImageGen',
	'TextureCaptureMercyNotePaper_D.png',
	'FEC6D7DDF6611A8E355FDABDE6B1B4AD607ADCB66EF66FAB35238E4AED7FFB99',
	'no words, no Hangul, no letters'
) 'M6.5 ImageGen provenance'

Write-Host ((
	'MISSING_FLOOR_M65_MERCY_NOTE_CONTRACT PASS assertions={0} ' +
	'capture_threshold=5 world_prop=1 ui=0') -f $assertions)
