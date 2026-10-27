[CmdletBinding()]
param(
	# 에셋 이름(SM_...) 또는 빌더 이름(unit_door). 비우면 전부.
	[string[]]$Only = @()
)

# Scripts/blender/build_*.py를 Blender 헤드리스로 돌려 Content/SourceArt/Blender에
# FBX·구운 텍스처·manifest.json·미리보기를 만든다. 다음 단계는
# Import-BlenderAssets.ps1.
#
# Blender는 설치하지 않고 MSI를 사용자 폴더에 풀어 쓴다(관리자 권한 불필요).
#   msiexec /a blender-5.2.0-windows-x64.msi /qn TARGETDIR=%LOCALAPPDATA%\Programs\Blender-5.2
# IG_BLENDER 환경 변수로 다른 blender.exe를 가리킬 수 있다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
$igArtifactScope = Enter-IGArtifactScope -ProjectRoot (Split-Path -Parent $PSScriptRoot) -Name 'Build-BlenderAssets.ps1' -Parameters $PSBoundParameters
$igArtifactSucceeded = $false
try {

$projectRoot = Split-Path -Parent $PSScriptRoot
$builderRoot = Join-Path $PSScriptRoot 'blender'
$outRoot = Join-Path $projectRoot 'Content\SourceArt\Blender'
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

$candidates = @()
if ($env:IG_BLENDER) { $candidates += $env:IG_BLENDER }
$candidates += Join-Path $env:LOCALAPPDATA 'Programs\Blender-5.2\Blender Foundation\Blender 5.2\blender.exe'
$installedBlenderRoot = Join-Path $env:ProgramFiles 'Blender Foundation'
if (Test-Path -LiteralPath $installedBlenderRoot -PathType Container) {
	$candidates += Get-ChildItem -LiteralPath $installedBlenderRoot -Filter 'blender.exe' -Recurse -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
}
$blender = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
if (-not $blender) {
	throw 'blender.exe를 찾지 못했다. IG_BLENDER를 설정하거나 MSI를 %LOCALAPPDATA%\Programs\Blender-5.2에 풀어라.'
}

# 빌더 하나가 에셋 여럿을 만들 수 있다. 어떤 빌더가 무엇을 만드는지는 여기 표가
# 유일한 출처다. Import 쪽은 manifest만 보므로 표는 이쪽에만 필요하다.
$builders = [ordered]@{
	'bedding' = @('SM_ApartmentBedding')
	'desk_cup' = @('SM_DeskPencilCup')
	'entry_props' = @('SM_VideoIntercom', 'SM_HangingWorkVest')
	'shipping_labels' = @('SM_ShippingLabels')
	'circuit_panel' = @('SM_LobbyCircuitPanel', 'SM_CircuitPanelPrints', 'SM_CircuitToggle', 'SM_CorridorCircuitCabinet', 'SM_CorridorCircuitPrint')
	'booth_stationery' = @('SM_ComplaintLedger', 'SM_ComplaintImpressionPad', 'SM_GraphitePencil')
	'tuning_tool' = @('SM_TuningHammer')
	'detail_props' = @('SM_TriangleKimbapA', 'SM_TriangleKimbapB', 'SM_TriangleKimbapC', 'SM_TriangleKimbapD', 'SM_GypsumCutBoard', 'SM_WorkPaintCan', 'SM_CottonWorkGlove', 'SM_TunerNotebook', 'SM_GypsumChipCluster', 'SM_ConstructionSheetDrape', 'SM_ConstructionSheetFloor')
	'roof_utility' = @('SM_RoofTank2000L', 'SM_RoofCleaningPipework', 'SM_RoofStairHandrail', 'SM_RoofTankPlate', 'SM_RoofDrainPlate', 'SM_RoofBypassPlate')
	'roof_stair_tread' = @('SM_RoofStairTread')
	'pump_panel' = @('SM_PumpControlPanel', 'SM_PumpSelector')
	'booth_pump' = @('SM_BoothPump')
	'booth_pump_pipework' = @('SM_BoothPumpPipework')
	'entrance_camera' = @('SM_EntranceCamera')
	'lift_call_plate' = @('SM_LiftCallPlate')
	'elevator' = @('SM_ElevatorCabSides', 'SM_ElevatorCabFront', 'SM_ElevatorCabBack', 'SM_ElevatorBackPanel', 'SM_ElevatorCabCeiling', 'SM_ElevatorCabFloor', 'SM_ElevatorDoorPanel', 'SM_ElevatorLandingFrame', 'SM_ElevatorCop', 'SM_ElevatorCctvDome', 'SM_ElevatorFullLamp', 'SM_ElevatorButtonLed')
	'utility_fixtures' = @('SM_InductionMeter', 'SM_MeterRotor', 'SM_MeterCabinetFive', 'SM_BoothMonitor', 'SM_BoothRecorder', 'SM_BoothKeyring')
	'retail_refresh' = @('SM_RetailPOS', 'SM_ServiceBell', 'SM_RetailPotato', 'SM_RetailShrimp', 'SM_RetailCorn', 'SM_RetailCupBeef', 'SM_RetailCupKimchi', 'SM_RetailBiscuit', 'SM_WaterBottle')
	'unit_door' = @('SM_UnitDoorLeaf', 'SM_UnitDoorHardware', 'SM_UnitDoorFrame', 'SM_UnitDoorLeafWideL', 'SM_UnitDoorHardwareWideL', 'SM_UnitDoorFrameWide')
	'corridor_fixtures' = @('SM_FireExtinguisherBox')
	'neighborhood_prints' = @('SM_DoorDeliveryMagnet', 'SM_RentalNoticeA4', 'SM_ApartmentCalendar2025', 'SM_NeighborMemo402')
	'door_prints' = @('SM_DoorPrints401', 'SM_DoorPrints402')
	'fire_safety' = @('SM_FireAlarmPanel', 'SM_FireExtinguisher')
	'house_slipper' = @('SM_HouseSlipper')
	'outdoor_condenser' = @('SM_AcOutdoorUnit')
	'witness_bowl' = @('SM_WitnessWaterBowl')
	'witness_props' = @('SM_WitnessPrescriptionEnvelope', 'SM_WitnessStoreRoster', 'SM_WitnessCigaretteButts')
	'seo_recorder' = @('SM_SeoVoiceRecorder')
	'lobby_mailboxes' = @('SM_MailboxUnit')
	'ceiling_light' = @('SM_CeilingLightRing', 'SM_CeilingLightDome')
	'stair_sensor_light' = @('SM_StairSensorLight')
	'notice_prints' = @('SM_NoticeLightsOutA4')
	'stair_fire_door' = @('SM_StairFireDoorLeaf', 'SM_FireDoorCloserArm', 'SM_FireDoorCloserRod', 'SM_FireDoorPrints', 'SM_FireDoorWedge', 'SM_ExitSignLamp')
	'neighbor_doors' = @('SM_DoorPrints202', 'SM_Doorbells202', 'SM_DoorPrints303', 'SM_DawnDeliveryBag')
	'bathroom' = @('SM_BathroomToilet', 'SM_BathroomBasin', 'SM_BathroomMirrorCabinet', 'SM_BathroomShower', 'SM_BathroomDoorLeaf', 'SM_BathroomDoorFrame', 'SM_BathroomTowelRail', 'SM_BathroomCornerShelf', 'SM_FloorDrain', 'SM_BathroomVentGrille')
	'notes303' = @('SM_Note303First', 'SM_Note303Second', 'SM_Note303Third', 'SM_Note303Last')
	'rooftop_doors' = @('SM_RooftopDoorLeaf', 'SM_AnnexDoorLeaf', 'SM_RooftopDoorSign', 'SM_DoorBarrelBolt', 'SM_DoorBarrelBoltPin', 'SM_DoorBarrelBoltKeeper')
	'fridge' = @('SM_FridgeBody', 'SM_FridgeDoor')
	'kitchen' = @('SM_KitchenBaseRun', 'SM_DrumWasher', 'SM_KitchenWallUnits', 'SM_RangeHood', 'SM_Microwave', 'SM_KitchenSink', 'SM_InductionHob')
	'apartment_props' = @('SM_Wardrobe', 'SM_WallAirConditioner')
	'alley_props' = @('SM_TrafficCone', 'SM_UtilityPole', 'SM_GasMeterBox', 'SM_ConvexMirror')
	'store_fixtures' = @('SM_StoreCoolerBank', 'SM_StoreCoolerDoor', 'SM_StoreGondola', 'SM_StoreCounter', 'SM_CardTerminal', 'SM_HotSnackWarmer', 'SM_ChestFreezer', 'SM_OpenShowcase')
	'apartment_fixtures' = @('SM_ApartmentWindow', 'SM_VenetianBlind', 'SM_WallSwitch', 'SM_ShoeCabinet')
	'villa_window' = @('SM_VillaWindow')
	'store_products' = @('SM_TobaccoCabinet', 'SM_WindowBar', 'SM_HotWaterDispenser', 'SM_TrashBin')
	'inner_room' = @('SM_InnerRoomCot', 'SM_InnerRoomCabinet', 'SM_InnerRoomWorkbench', 'SM_HandCart', 'SM_WasteSack', 'SM_StorageBox', 'SM_RemovedBoards', 'SM_BareBulb', 'SM_LinedWallPanelA', 'SM_LinedWallPanelB')
	'leftover_props' = @('SM_AccessHatchFrameS', 'SM_AccessHatchFrameM', 'SM_AccessHatchFrameL', 'SM_AccessHatchDoorS', 'SM_AccessHatchDoorM', 'SM_AccessHatchDoorL', 'SM_HatchSealPlug', 'SM_CoverPieceA', 'SM_CoverPieceB', 'SM_CoverPieceC', 'SM_CoverFlapA', 'SM_CoverFlapB', 'SM_CoverFrame', 'SM_CoverClamp', 'SM_ToolPouch', 'SM_GlovePouch', 'SM_InspectionOverlayBoard', 'SM_BentShieldSheet', 'SM_FinishBoard', 'SM_TimberWedge', 'SM_LeftoverLabelJeongwoo', 'SM_LeftoverLabelDoha', 'SM_LeftoverLabelMarked', 'SM_CollectionNoticePrint')
	'annex_wall_props' = @('SM_AnnexOverlayTop', 'SM_AnnexJointTape', 'SM_AnnexJointTorn', 'SM_AnnexBatten', 'SM_AnnexInnerFixings', 'SM_AnnexAngleClip', 'SM_AnnexBraceFrame', 'SM_HammerDent')
}

$selected = @()
foreach ($entry in $builders.GetEnumerator()) {
	if ($Only.Count -eq 0) { $selected += $entry.Key; continue }
	if ($Only -contains $entry.Key) { $selected += $entry.Key; continue }
	foreach ($asset in $entry.Value) {
		if ($Only -contains $asset) { $selected += $entry.Key; break }
	}
}
if ($selected.Count -eq 0) {
	throw "고른 이름에 맞는 빌더가 없다: $($Only -join ', ')"
}
if ($selected -contains 'fire_safety') {
	& python (Join-Path $PSScriptRoot 'build_fire_safety_prints.py')
	if ($LASTEXITCODE -ne 0) { throw '소방 설비 인쇄 원본 생성 실패' }
}
if ($selected -contains 'neighborhood_prints') {
	& python (Join-Path $PSScriptRoot 'build_apartment_calendar.py')
	if ($LASTEXITCODE -ne 0) { throw '2025년 달력 생성 실패' }
}
if ($selected -contains 'stair_fire_door' -or $selected -contains 'neighbor_doors') {
	& python (Join-Path $PSScriptRoot 'rectify_concept_faces.py')
	if ($LASTEXITCODE -ne 0) { throw '시안 면 펴기 실패' }
}
if ($selected -contains 'roof_utility') {
	& python (Join-Path $PSScriptRoot 'build_roof_utility_prints.py')
	if ($LASTEXITCODE -ne 0) { throw '옥상 설비 표찰 생성 실패' }
}

$logRoot = Join-Path $projectRoot ('Saved/Validation/BlenderBuild-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
Add-IGArtifactDirectory -Scope $igArtifactScope -Directory $logRoot -Variant ($selected -join '_')
if ($selected -contains 'leftover_props') {
	& python (Join-Path $PSScriptRoot 'build_leftover_prints.py')
	if ($LASTEXITCODE -ne 0) { throw '남은 짐 인쇄 원본 생성 실패' }
}
if ($selected -contains 'annex_wall_props') {
	& python (Join-Path $PSScriptRoot 'crop_annex_wall_textures.py')
	if ($LASTEXITCODE -ne 0) { throw '5층 벽 층 텍스처 자르기 실패' }
}
if ($selected -contains 'detail_props') {
	& python (Join-Path $PSScriptRoot 'build_detail_prints.py')
	if ($LASTEXITCODE -ne 0) { throw '간편식·페인트 통 인쇄 생성 실패' }
}
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$colorCheckLog = Join-Path $logRoot 'BlenderBaseColorCheck.log'
& $blender -b --factory-startup --python-exit-code 1 `
	--python (Join-Path $builderRoot 'check_base_color_bake.py') `
	-- (Join-Path $logRoot 'BaseColorCheck') *> $colorCheckLog
if ($LASTEXITCODE -ne 0 -or @(Select-String -LiteralPath $colorCheckLog -Pattern '^BASE_COLOR_BAKE PASS').Count -ne 2) {
	throw "금속 색 보존 검사 실패: $colorCheckLog"
}
Write-Host 'BASE_COLOR_BAKE PASS direct=1 from_high=1'
foreach ($builder in $selected) {
	$script = Join-Path $builderRoot "build_$builder.py"
	$logPath = Join-Path $logRoot "Blender_$builder.log"
	Write-Host "BLENDER_BUILD running build_$builder.py"
	# 부분 빌드를 지원하는 빌더에도 선택을 넘긴다. 안내문 한 장을 고르고 소품 전체를 다시 굽던 경로다.
	$oldOnly = $env:IG_ONLY
	$builderStartedAt = [DateTime]::UtcNow
	try {
		$env:IG_ONLY = if ($Only.Count -and $Only -notcontains $builder) {
			@($builders[$builder] | Where-Object { $Only -contains $_ }) -join ','
		} else { '' }
		& $blender -b --factory-startup --python-exit-code 1 --python $script -- $outRoot 2>&1 | Tee-Object -FilePath $logPath | Where-Object { $_ -match '^\[IGBL\]|Error|Traceback' } | ForEach-Object { Write-Host "  $_" }
	} finally {
		$env:IG_ONLY = $oldOnly
	}
	if ($LASTEXITCODE -ne 0) {
		throw "Blender 빌더 실패 ($LASTEXITCODE): build_$builder.py, 로그 $logPath"
	}
	$expectedAssets = if ($Only.Count -and $Only -notcontains $builder) {
		@($builders[$builder] | Where-Object { $Only -contains $_ })
	} else { @($builders[$builder]) }
	foreach ($asset in $expectedAssets) {
		$manifest = Join-Path $outRoot "$asset\manifest.json"
		if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
			throw "빌더가 manifest를 남기지 않았다: $manifest"
		}
		if ((Get-Item -LiteralPath $manifest).LastWriteTimeUtc -lt $builderStartedAt.AddSeconds(-1)) {
			throw "선택한 에셋을 새로 만들지 않았습니다: $asset"
		}
	}
}
Write-Host ('BLENDER_BUILD PASS builders={0}' -f $selected.Count)

$igArtifactSucceeded = $true
}
finally {
    Complete-IGArtifactScope -Scope $igArtifactScope -Succeeded $igArtifactSucceeded
}
