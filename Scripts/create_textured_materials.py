"""Create the textured PBR material set for the prologue realism pass.

Requires the textures imported by generate_surface_textures.py. Architecture
materials sample in world space (per-axis variants) so scaled greybox blocks
never stretch their textures; prop materials use mesh UVs so movable physics
objects carry their surface with them.
"""

import os
import sys

import unreal

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import texture_atlas_contract  # noqa: E402


MATERIAL_ROOT = "/Game/Prototype/Materials"
TEXTURE_ROOT = "/Game/Prototype/Textures"
SURFACE_RESPONSE_MARKER = "IG_SurfaceResponse_v1"
PRINT_RESPONSE_MARKER = "IG_PrintResponse_v1"
OPTICAL_RESPONSE_MARKER = "IG_OpticalResponse_v1"
WET_GROUND_RESPONSE_MARKER = "IG_WetGroundResponse_v1"

# A single 1K/2K scan still reads like wallpaper when it is repeated across a
# whole room.  These restrained, material-family defaults add a second spatial
# scale only where the source is non-directional enough to survive it.  The
# values are deliberately small: they break repetition and wake up highlights
# under a moving flashlight without turning plaster into rock or cloth into
# foil.  Each material can override any value in its own spec.
SURFACE_RESPONSE_DEFAULTS = {
    "Jangpan": {
        "macro_strength": 0.045, "macro_scale": 4.6,
        "normal_strength": 0.42, "specular": 0.34,
    },
    "ApartmentWallpaperV2": {
        "macro_strength": 0.045, "macro_scale": 4.2,
        "normal_strength": 0.38, "roughness_detail_strength": 0.05,
        "roughness_detail_scale": 3.1, "ao_strength": 0.82,
        "specular": 0.28,
    },
    # 무지 엠보싱 벽지. macro는 꺼 둔다 — 저주파 복사본을 겹치는 장치는
    # 무늬의 반복을 깨려는 것인데, 이쪽은 깰 무늬가 없고 세로 결만 있어서
    # 밝기만 흔들리며 결이 흐려진다. 결의 방향성은 노멀이 소유한다.
    "ApartmentWallpaperEmboss": {
        "macro_strength": 0.0,
        "normal_strength": 0.46, "roughness_detail_strength": 0.05,
        "roughness_detail_scale": 3.1, "ao_strength": 0.86,
        "specular": 0.30,
    },
    "WoodDark": {
        "normal_strength": 0.65, "roughness_detail_strength": 0.08,
        "roughness_detail_scale": 3.7, "specular": 0.38,
    },
    "Blanket": {
        "normal_strength": 0.56, "detail_normal_strength": 0.04,
        "detail_normal_scale": 4.3, "specular": 0.22,
    },
    "Brick": {
        "macro_strength": 0.060, "macro_scale": 5.2,
        "normal_strength": 1.16, "roughness_detail_strength": 0.10,
        "roughness_detail_scale": 4.1, "specular": 0.32,
    },
    "Concrete": {
        "macro_strength": 0.075, "macro_scale": 5.4,
        "normal_strength": 0.72, "detail_normal_strength": 0.06,
        "detail_normal_scale": 4.7, "roughness_detail_strength": 0.16,
        "roughness_detail_scale": 4.7, "specular": 0.32,
    },
    "Shutter": {
        "macro_strength": 0.035, "macro_scale": 4.8,
        "normal_strength": 1.12, "roughness_detail_strength": 0.08,
        "roughness_detail_scale": 5.1, "specular": 0.40,
    },
    "StoreTile": {
        "macro_strength": 0.025, "macro_scale": 6.0,
        "normal_strength": 0.42, "roughness_variation": 0.18,
        "specular": 0.50,
    },
    "CeilingTile": {
        "macro_strength": 0.035, "macro_scale": 5.0,
        "normal_strength": 0.55, "specular": 0.24,
    },
    "MetalBrushed": {
        "normal_strength": 0.30, "detail_normal_strength": 0.03,
        "detail_normal_scale": 5.3, "roughness_detail_strength": 0.14,
        "roughness_detail_scale": 4.9, "specular": 0.50,
    },
    "Stucco": {
        # 도장 미장면의 잔결이다. 스캔 노멀을 두 번 증폭하면 돌처럼 보인다.
        "response_revision": 2,
        "macro_strength": 0.035, "macro_scale": 5.8,
        "normal_strength": 0.12, "detail_normal_strength": 0.0,
        "detail_normal_scale": 4.8, "roughness_detail_strength": 0.06,
        "roughness_detail_scale": 4.8, "specular": 0.28,
    },
    "KoreanVillaStucco": {
        "macro_strength": 0.052, "macro_scale": 6.2,
        "normal_strength": 1.08, "detail_normal_strength": 0.10,
        "detail_normal_scale": 5.4, "roughness_detail_strength": 0.08,
        "roughness_detail_scale": 5.4, "ao_strength": 0.84,
        "specular": 0.24,
    },
    "MovingBoxCardboard": {
        "normal_strength": 1.08, "roughness_detail_strength": 0.10,
        "roughness_detail_scale": 4.4, "ao_strength": 0.76,
        "specular": 0.24,
    },
    "GraniteTile": {
        "macro_strength": 0.025, "macro_scale": 6.4,
        "normal_strength": 0.24, "roughness_variation": 0.18,
        "specular": 0.46,
    },
    "GranitePanel": {
        "macro_strength": 0.040, "macro_scale": 5.7,
        "normal_strength": 0.48, "roughness_detail_strength": 0.07,
        "roughness_detail_scale": 4.3, "specular": 0.42,
    },
    "MarbleFloor": {
        "macro_strength": 0.020, "macro_scale": 7.0,
        "normal_strength": 0.22, "roughness_variation": 0.18,
        "specular": 0.50,
    },
    # 빌라 파사드 적벽돌. 줄눈이 노멀에 있으니 세기를 조금 올리고, 큰 얼룩으로
    # 층마다 색이 조금씩 다른 벽돌 로트를 흉내 낸다.
    "VillaBrick": {
        "macro_strength": 0.070, "macro_scale": 4.4,
        "normal_strength": 1.22, "roughness_detail_strength": 0.10,
        "roughness_detail_scale": 4.0, "ao_strength": 0.88,
        "specular": 0.30,
    },
    "MissingFloorDryPlaster": {
        "macro_strength": 0.080, "macro_scale": 5.6,
        "normal_strength": 0.68, "detail_normal_strength": 0.06,
        "detail_normal_scale": 4.6, "roughness_detail_strength": 0.14,
        "roughness_detail_scale": 4.6, "ao_strength": 0.88,
        "specular": 0.22,
    },
}

# mapping: XY (floors/ceilings), XZ (walls running along X), YZ (walls along Y),
#          UV (mesh UVs with a tiling multiplier)
# tile: world centimeters per texture repeat (or UV multiplier for UV mapping)
TEXTURED_MATERIALS = {
    # --- apartment ---------------------------------------------------------
    # Tile sizes are the real-world repeat of the surface: a 1K photo over a
    # 115 cm span is ~9 px/cm, which is what makes floors and walls hold up
    # when the camera is a metre away.
    "M_Jangpan":        {"tex": "Jangpan", "mapping": "XY", "tile": 115.0},
    "M_Wallpaper_X":    {"tex": "ApartmentWallpaperV2", "mapping": "XZ", "tile": 165.0,
                         "rough": 0.86, "ao": True, "tint": (0.72, 0.70, 0.65)},
    "M_Wallpaper_Y":    {"tex": "ApartmentWallpaperV2", "mapping": "YZ", "tile": 165.0,
                         "rough": 0.86, "ao": True, "tint": (0.72, 0.70, 0.65)},
    "M_WallpaperCeil":  {"tex": "ApartmentWallpaperV2", "mapping": "XY", "tile": 220.0,
                         "rough": 0.92, "ao": True, "desaturate": 0.82,
                         "tint": (0.48, 0.48, 0.46)},
    "M_WoodFurnitureUV": {"tex": "WoodDark", "mapping": "UV", "tile": 1.0, "rough": 0.55},
    "M_BeddingUV":      {"tex": "Blanket", "mapping": "UV", "tile": 2.0, "rough": 0.95},
    # --- alley -------------------------------------------------------------
    # (M_AsphaltWorld is built by create_wet_asphalt: dew puddles + mirror wet)
    "M_VillaStucco_X":  {"tex": "KoreanVillaStucco", "mapping": "XZ", "tile": 235.0,
                          "rough": 0.88, "ao": True, "tint": (0.82, 0.85, 0.88)},
    "M_VillaStucco_Y":  {"tex": "KoreanVillaStucco", "mapping": "YZ", "tile": 235.0,
                          "rough": 0.88, "ao": True, "tint": (0.82, 0.85, 0.88)},
    "M_MovingBoxCardboardUV": {
        "tex": "MovingBoxCardboard", "mapping": "UV", "tile": 2.0,
        "rough": 0.87, "ao": True,
    },
    "M_Concrete_XY":    {"tex": "Concrete", "mapping": "XY", "tile": 150.0},
    "M_Concrete_X":     {"tex": "Concrete", "mapping": "XZ", "tile": 150.0},
    "M_Shutter_X":      {"tex": "Shutter", "mapping": "XZ", "tile": 130.0},
    "M_ConcreteDark_X": {"tex": "Concrete", "mapping": "XZ", "tile": 260.0,
                         "tint": (0.32, 0.33, 0.36)},
    # 수평면용. _X는 월드 좌표를 (X, Z)로 마스킹하므로 Z가 일정한 바닥에서는
    # V가 상수가 되어 텍스처가 한 줄로 잘려 Y 방향으로 무한히 늘어난다.
    # 5층 별관 바닥이 그 상태였고, 프레임마다 보였던 긴 평행 줄무늬가 그것이다.
    "M_ConcreteDark_XY": {"tex": "Concrete", "mapping": "XY", "tile": 260.0,
                          "tint": (0.32, 0.33, 0.36)},
    # --- store -------------------------------------------------------------
    # Shop floors are buffed to a mirror; the photo roughness map is far too
    # matte for that, so this one forces a polished value.
    "M_StoreTileWorld": {"tex": "StoreTile", "mapping": "XY", "tile": 60.0,
                         "force_rough": 0.31, "retail_finish": "floor"},
    "M_StoreCeilWorld": {"tex": "CeilingTile", "mapping": "XY", "tile": 60.0, "rough": 0.8, "retail_finish": "ceiling"},
    "M_StoreWall_X":    {"tex": "Concrete", "mapping": "XZ", "tile": 150.0,
                         "tint": (1.25, 1.25, 1.22), "retail_finish": "wall"},
    "M_StoreWall_Y":    {"tex": "Concrete", "mapping": "YZ", "tile": 150.0,
                         "tint": (1.25, 1.25, 1.22), "retail_finish": "wall"},
    "M_MetalUV":        {"tex": "MetalBrushed", "mapping": "UV", "tile": 1.0,
                         "metallic": 0.85},
    "M_ShelfSteelUV":   {"tex": "MetalBrushed", "mapping": "UV", "tile": 1.0,
                         "tint": (0.60, 0.66, 0.72), "metallic": 0.4, "rough": 0.5},
    # --- villa: corridor, lift car, facade, kitchenette ---------------------
    # A Korean walk-up villa is troweled stucco inside the hallway, 600 mm
    # speckled granite tile underfoot, and 900 mm granite cladding outside.
    # The lift car is hairline stainless over a marble floor. Roughness is
    # forced on the polished surfaces: the photo maps are far too matte to
    # give back the reflections these materials are recognised by.
    # 덧칠한 공용부 벽은 생성 색상 원본 한 장으로 통일한다. 얇은 개구부도 면 방향으로 투영한다.
    "M_Stucco_X": {"tex": "Stucco", "mapping": "DOMINANT", "tile": 180., "retail_finish": "landing_wall"},
    "M_Stucco_Y": {"tex": "Stucco", "mapping": "DOMINANT", "tile": 180., "retail_finish": "landing_wall"},
    "M_StuccoCeil": {"tex": "Stucco", "mapping": "DOMINANT", "tile": 180., "retail_finish": "landing_ceiling"},
    "M_StuccoDado_X": {"tex": "Stucco", "mapping": "DOMINANT", "tile": 180., "retail_finish": "landing_dado"},
    # 포천석 사진에서 새로 만든 원본과 600mm 줄눈. 테라초를 화강석으로 위장하지 않는다.
    "M_GraniteTile_XY": {"tex": "GraniteTile", "mapping": "DOMINANT", "tile": 60.0, "retail_finish": "granite"},
    # Worktops need UV mapping, not world mapping: a world-XY stone smears
    # into stripes the moment it wraps a vertical edge or a splashback.
    "M_CounterStoneUV": {"tex": "MarbleFloor", "mapping": "UV", "tile": 1.3,
                         "desaturate": 0.7, "tint": (2.3, 2.3, 2.25),
                         "force_rough": 0.17},
    "M_StainlessUV":    {"tex": "MetalBrushed", "mapping": "UV", "tile": 1.0,
                         "tint": (0.72, 0.75, 0.78), "metallic": 1.0, "force_rough": 0.42},
    "M_SteelDoorUV":    {"tex": "MetalBrushed", "mapping": "UV", "tile": 1.0,
                          "tint": (0.22, 0.23, 0.25), "metallic": 0.65, "force_rough": 0.48},
    "M_KitchenGlossUV": {"tex": "MetalBrushed", "mapping": "UV", "tile": 1.0,
                          "desaturate": 1.0, "tint": (1.72, 1.70, 1.64),
                          "metallic": 0.0, "force_rough": 0.13},
    # 「없는 층」의 마른 석고 표면. 방향별 월드 매핑을 따로 두어
    # 그레이박스 벽을 늘려도 가루결과 균열의 밀도가 변하지 않는다.
    "M_MissingFloorPlaster_X": {
        "tex": "MissingFloorDryPlaster", "mapping": "XZ", "tile": 138.0,
        "rough": 0.91, "ao": True, "tint": (0.78, 0.76, 0.71),
    },
    "M_MissingFloorPlaster_Y": {
        "tex": "MissingFloorDryPlaster", "mapping": "YZ", "tile": 138.0,
        "rough": 0.91, "ao": True, "tint": (0.78, 0.76, 0.71),
    },
    "M_MissingFloorPlaster_XY": {
        "tex": "MissingFloorDryPlaster", "mapping": "XY", "tile": 138.0,
        "rough": 0.93, "ao": True, "tint": (0.74, 0.73, 0.69),
    },
    # 발소리 표면. §11 규칙 2가 「어느 바닥을 고르느냐」를 선택으로 만드는데,
    # 한때 전부 복도 콘크리트로 그려지고 있었다. 소리는 다른데 그림이 같으면
    # 고를 수가 없다.
    #
    # 계단 타일은 55cm에 다이아몬드 16개 = 피치 34mm로, 실제 체커플레이트
    # 규격 안이다. 거칠기를 콘크리트(0.9+)보다 낮게 두는 것이 핵심이다 —
    # 손전등이 스칠 때 계단만 반사가 다르고, 그것이 눈으로 읽히는 차이다.
    "M_MissingFloorSteelStair": {
        "tex": "MissingFloorSteelStair", "mapping": "XY", "tile": 55.0,
        "rough": 0.68, "ao": True, "tint": (0.86, 0.87, 0.90),
    },
    "M_RooftopWaterproofing_XY": {
        "tex": "RooftopWaterproofing", "mapping": "XY", "tile": 150.0,
        "rough": 0.82, "ao": True, "tint": (0.80, 0.82, 0.76),
    },
    # 세대 현관문 문짝. 지금까지 브러시드 스테인리스를 쓰고 있었는데, 한국
    # 빌라 현관문은 무광 도장 강판이라 재질 계열 자체가 다르다. 기하(브러시드
    # 밴드·인레이·레버·도어록·도어스코프)는 이미 3D이므로 표면만 바꾼다.
    # metallic 0 — 빛이 만나는 것은 강판이 아니라 그 위의 도장이다.
    "M_UnitDoorPaintedSteel": {
        "tex": "UnitDoorPaintedSteel", "mapping": "UV", "tile": 1.0,
        "rough": 0.74, "ao": True, "metallic": 0.0, "tint": (0.92, 0.93, 0.95),
    },
}

# Lit poster/label materials: texture straight onto mesh UVs.
DECAL_MATERIALS = {
    "M_RetailTobaccoAd": {"tex_asset": "T_RetailTobaccoAd_D", "rough": 0.6},
    **{f"M_RetailPrice{sku}": {"tex_asset": f"T_RetailPrice{sku}_D", "rough": 0.7}
       for sku in ("Potato", "Shrimp", "Corn", "CupBeef", "CupKimchi", "Biscuit", "Water", "Soda", "Barley", "GreenTea")},
    "M_LabelWater1L": {"tex_asset": "T_LabelWater1L_D", "rough": 0.55},
    "M_LabelWater2L": {"tex_asset": "T_LabelWater2L_D", "rough": 0.55},
    "M_NeighborhoodDelivery": {"tex_asset": "T_NeighborhoodDelivery_D", "rough": 0.85},
    "M_ArrivalContract": {
        "tex_asset": "T_ArrivalContract_D", "rough": 0.82,
        "two_sided": True,
    },
    "M_PosterSale":    {"tex_asset": "T_PosterSale_D", "rough": 0.55, "emissive_scale": 0.06},
    "M_NoteFridge":    {"tex_asset": "T_NoteFridge_D", "rough": 0.86},
    "M_CaptureMercyNote": {
        "tex_asset": "T_CaptureMercyNote_D", "rough": 0.92, "two_sided": True,
    },
    # §20.3's world response. Same pad as the five-capture note, so the same
    # cheap recycled fibre and the same 0.92 roughness; only the words differ.
    "M_MercyNoteUnderDoor": {
        "tex_asset": "T_MercyNoteUnderDoor_D", "rough": 0.92, "two_sided": True,
    },
    # §5.5 채널 5가 저장되지 않는 이유를 손으로 만질 수 있게 하는 라벨. 마스킹
    # 테이프라 종이보다 살짝 매끈하고, 자체 발광은 없다 — 관리실 형광등과
    # 손전등만이 이 글자를 읽게 해준다.
    "M_SignAux5MonitorOnly": {
        "tex_asset": "T_SignAux5MonitorOnly_D", "rough": 0.74,
    },
    "M_SignVilla":     {"tex_asset": "T_SignVilla_D", "rough": 0.4, "emissive_scale": 0.25},
    "M_Plate401":      {"tex_asset": "T_Plate401_D", "rough": 0.35},
    "M_Plate402":      {"tex_asset": "T_Plate402_D", "rough": 0.35},
    "M_Plate403":      {"tex_asset": "T_Plate403_D", "rough": 0.35},
    "M_SignLaundry":   {"tex_asset": "T_SignLaundry_D", "rough": 0.45, "emissive_scale": 0.05},
    "M_SignHair":      {"tex_asset": "T_SignHair_D", "rough": 0.45, "emissive_scale": 0.05},
    "M_SignHof":       {"tex_asset": "T_SignHof_D", "rough": 0.45, "emissive_scale": 0.5},
    "M_SignSuper":     {"tex_asset": "T_SignSuper_D", "rough": 0.45, "emissive_scale": 0.05},
    # Aged paper stock for readable notes. The Korean copy is drawn over these
    # at runtime by the HUD, so the sheets themselves carry no text — only
    # creases, tape, water damage and age.
    # V2 preserves the faulty first paper sheet for provenance while replacing
    # the live materials with the verified, text-free ImageGen paper stock.
    "M_PaperClean": {
        "tex_asset": "T_PaperClean_V2_D", "rough": 0.82,
        "micro_stem": "T_PaperClean_V2", "normal_strength": 0.24,
        "rough_low": 0.76, "rough_high": 0.90, "ao": True,
        "specular": 0.22,
    },
    "M_PaperOld": {
        "tex_asset": "T_PaperOld_V2_D", "rough": 0.86,
        "micro_stem": "T_PaperClean_V2", "normal_strength": 0.30,
        "rough_low": 0.80, "rough_high": 0.94, "ao": True,
        "specular": 0.18,
    },
    "M_Calendar":      {"tex_asset": "T_Calendar_D", "rough": 0.7},
    "M_TobaccoNotice": {"tex_asset": "T_TobaccoNotice_D", "rough": 0.5},
    "M_SignPC":        {"tex_asset": "T_SignPC_D", "rough": 0.45, "emissive_scale": 0.05},
    "M_SignKaraoke":   {"tex_asset": "T_SignKaraoke_D", "rough": 0.45, "emissive_scale": 0.45},
    # Villa fittings. The lift readouts are the only thing genuinely emitting
    # in the shaft, so they carry a strong emissive; the rest are plastic.
    "M_DoorLock":      {"tex_asset": "T_DoorLock_D", "rough": 0.34, "emissive_scale": 0.12},
    "M_Intercom":      {"tex_asset": "T_Intercom_D", "rough": 0.34, "emissive_scale": 0.08},
    "M_LiftCOP":       {"tex_asset": "T_LiftCOP_D", "rough": 0.26, "emissive_scale": 0.03},
    "M_LiftHall":      {"tex_asset": "T_LiftHall_D", "rough": 0.3, "emissive_scale": 1.4},
    "M_SwitchPlate":   {"tex_asset": "T_SwitchPlate_D", "rough": 0.4, "emissive_scale": 0.1},
    # Product labels: printed plastic film, so fairly smooth and unlit-free.
    "M_LabelWater": {
        "tex_asset": "T_LabelWater_D", "rough": 0.28,
        "micro_stem": "T_CarrierBagFilm", "normal_strength": 0.06,
        "rough_low": 0.20, "rough_high": 0.34, "specular": 0.56,
    },
    "M_LabelGreenTea": {
        "tex_asset": "T_LabelGreenTea_D", "rough": 0.28,
        "micro_stem": "T_CarrierBagFilm", "normal_strength": 0.06,
        "rough_low": 0.20, "rough_high": 0.34, "specular": 0.56,
    },
    "M_LabelBarley": {
        "tex_asset": "T_LabelBarley_D", "rough": 0.28,
        "micro_stem": "T_CarrierBagFilm", "normal_strength": 0.06,
        "rough_low": 0.20, "rough_high": 0.34, "specular": 0.56,
    },
    "M_LabelSoda": {
        "tex_asset": "T_LabelSoda_D", "rough": 0.28,
        "micro_stem": "T_CarrierBagFilm", "normal_strength": 0.06,
        "rough_low": 0.20, "rough_high": 0.34, "specular": 0.56,
    },
    # ImageGen scans are BaseColor inputs on authored geometry, not finished
    # materials. Companion N/R/A/W/M maps make them respond to flashlight,
    # Lumen reflections and contact shadowing without baking light into colour.
    "M_AlleyCatTabbyUV": {
        "tex_asset": "T_AlleyCatTabby_D", "pbr_stem": "T_AlleyCatTabby",
        "specular": 0.32,
    },
    "M_P3CabinetMetalUV": {
        "tex_asset": "T_P3CabinetPaintedSteel_D", "pbr_stem": "T_P3CabinetPaintedSteel",
        "tile_u": 2.2, "wet_rough": 0.38, "wet_dark": 0.84,
        "wet_normal_flatten": 0.22, "specular": 0.50,
    },
    # 위층 사람 셸. 벽과 같은 석고 텍스처를 쓰되(§통합 비주얼 규칙: 존재가
    # 환경에서 태어났다), 사람 쪽만 세 가지가 다르다: 균열 노멀이 벽보다
    # 세고, 메시에 구운 정점 AO 골에 분진이 앉고, 숨과 잔떨림 WPO가 돈다.
    # 진폭 파라미터는 IGListenerEntity가 상태에 따라 MID로 조종한다.
    "M_MissingFloorListenerPlasterUV": {
        "tex_asset": "T_MissingFloorDryPlaster_D",
        "pbr_stem": "T_MissingFloorDryPlaster", "tile_u": 2.8,
        "specular": 0.16,
        "crack_normal_strength": 1.6,
        "cavity_dust": {
            "amount": 1.25, "lift": 1.22, "desat": 0.4,
            "flatten": 0.45, "occlusion": 0.8,
        },
        "breath": {
            "amplitude": 0.45, "rate": 0.22,
            "tremor": 0.1, "tremor_rate": 7.0,
        },
    },
}

# ImageGen source is split by Prepare-AIArt.ps1. Evidence sheets remain
# grayscale value masks so one texture controls the irregular wet edge; the
# surface overlays carry authored colour plus a keyed alpha channel.
EVIDENCE_MASK_MATERIALS = {
    "M_ApartmentWallPatina": {
        # 벽지 위의 투영 재질은 create_apartment_patina_material에서 만든다.
        "tex_asset": "T_ApartmentWallPatina_M",
    },
    # 5층의 분진 잔흔 네 장. 어두운 콘크리트 바닥 위의 석고 분진은 **살짝**
    # 밝은 얼룩이지 흰 자국이 아니다. 원래 값(0.48~0.68 알베도, 증폭 1.8~2.5)은
    # 바닥의 두 배 밝기에 이진 실루엣이라, CCTV 프레임에서 바닥 위에 떠 있는
    # 도장 자국처럼 읽혔다. 증폭을 낮춰 실루엣을 짙은 심지에만 남기고, 얇은
    # 자리는 substrate 쪽으로 보내 사라지게 한다.
    #
    # substrate는 그 잔흔이 실제로 얹히는 면의 알베도이고, **계산이 아니라
    # 측정**으로 얻는다. 확산맵 평균과 틴트를 곱해 추정했더니 바닥이 0.058로
    # 나왔는데 실측은 0.124였다 — 두 배 이상 어긋났고, 그 값으로는 잔흔의 얇은
    # 자리가 바닥보다 어두워져 석고 분진이 흙때로 읽혔다.
    #
    # 재는 방법: 잔흔 하나를 균일 알베도(0.30)로 굽고 근접 시점 프레임에서
    # 잔흔이 덮은 픽셀과 바로 인접한 바닥의 중앙값 비를 낸다. 비가 r이면
    # 그 면의 실효 알베도는 0.30 / r이다. 바닥은 r=2.43 → 0.124, 벽은
    # 손자국 심지(0.23)가 깨끗한 석고의 0.47배로 나와 0.487이었다.
    "M_AnnexPressure": {
        "tex_asset": "T_AnnexPressure_M", "rough": 0.94,
        "color": (0.19, 0.18, 0.16), "mask_gain": 1.1, "specular": 0.08,
        "substrate": (0.487, 0.474, 0.443),
    },
    "M_MissingFloorHandprints": {
        "tex_asset": "T_MissingFloorHandprints_M", "rough": 0.94,
        "color": (0.23, 0.22, 0.205), "mask_gain": 1.25, "specular": 0.08,
        "substrate": (0.487, 0.474, 0.443),
    },
    # 0.24는 실측 바닥(0.124)의 정확히 두 배다. 그 전의 0.20과 0.22는 둘 다
    # 바닥보다 어두운 값이었고, 그래서 「한 단계 올려도」 프레임이 달라지지
    # 않았다 — 어느 쪽이든 분진이 아니라 때였다. 증폭 1.15는 건드리지 않는다:
    # 실루엣을 짙은 심지에 묶어 두는 그 값이 도장 자국으로 되돌아가지 않게
    # 하는 장치다.
    "M_MissingFloorDragTrails": {
        "tex_asset": "T_MissingFloorDragTrails_M", "rough": 0.96,
        "color": (0.24, 0.23, 0.21), "mask_gain": 1.15, "specular": 0.06,
        "substrate": (0.124, 0.126, 0.132),
    },
    "M_MissingFloorDustJoint": {
        "tex_asset": "T_MissingFloorDustJoint_M", "rough": 0.98,
        "color": (0.26, 0.25, 0.23), "mask_gain": 1.10, "specular": 0.04,
        "substrate": (0.124, 0.126, 0.132),
    },
    # 긁힌 자국은 분진이 아니라 파인 자리다 — 석고보다 어두운 색이 맞고,
    # 얇은 자리는 벽으로 사라져야 한다.
    "M_MissingFloorCavityScratches": {
        "tex_asset": "T_MissingFloorCavityScratches_M", "rough": 0.92,
        "color": (0.34, 0.32, 0.29), "mask_gain": 1.9, "specular": 0.08,
        "substrate": (0.487, 0.474, 0.443),
    },
}

SURFACE_OVERLAY_MATERIALS = {
    "M_DecalDampWallpaper": {
        "tex_asset": "T_DecalDampWallpaper_D", "rough": 0.78,
    },
    # Each person card is fixed to an authored viewing cue, receives real
    # scene light, and stays masked/opaque so hair edges cannot sort like a
    # translucent card. The listener front layer also carries conservative
    # N/R/A maps and is paired with a continuous contact-shadow shell.
    "M_SpriteSeo": {"tex_asset": "T_SpriteSeo_D", "rough": 0.82},
    "M_SpriteMok": {"tex_asset": "T_SpriteMok_D", "rough": 0.86},
    "M_SpriteListenerFront": {
        "tex_asset": "T_SpriteListenerFront_D",
        "pbr_stem": "T_SpriteListenerFront",
        "rough": 0.86,
        "specular": 0.14,
    },
    "M_SpriteListenerCrawl0": {
        "tex_asset": "T_SpriteListenerCrawl0_D",
        "pbr_stem": "T_SpriteListenerCrawl0",
        "rough": 0.86,
        "specular": 0.14,
    },
    "M_SpriteListenerCrawl1": {
        "tex_asset": "T_SpriteListenerCrawl1_D",
        "pbr_stem": "T_SpriteListenerCrawl1",
        "rough": 0.86,
        "specular": 0.14,
    },
    "M_SpriteListenerCrawl2": {
        "tex_asset": "T_SpriteListenerCrawl2_D",
        "pbr_stem": "T_SpriteListenerCrawl2",
        "rough": 0.86,
        "specular": 0.14,
    },
    "M_SpriteListenerCrawl3": {
        "tex_asset": "T_SpriteListenerCrawl3_D",
        "pbr_stem": "T_SpriteListenerCrawl3",
        "rough": 0.86,
        "specular": 0.14,
    },
    "M_SpriteFinalCavity": {
        "tex_asset": "T_SpriteFinalCavity_D",
        "pbr_stem": "T_SpriteFinalCavity",
        "rough": 0.90,
        "specular": 0.10,
    },
    "M_SpriteMokFinalUpper": {
        "tex_asset": "T_SpriteMokFinalUpper_D",
        "pbr_stem": "T_SpriteMokFinalUpper",
        "rough": 0.84,
        "specular": 0.14,
    },
}

# Emissive signage: the texture *is* the light source.
SIGN_MATERIALS = {
    # At the old 2.2/1.8 multipliers the pale lettering clipped to cyan-white
    # before the camera exposed the alley, erasing the Korean store identity.
    # These remain visibly self-lit while preserving the print and mint band.
    "M_SignMainLit":  {"tex_asset": "T_SignMain_D", "emissive_scale": 0.85},
    "M_SignBladeLit": {"tex_asset": "T_SignBlade_D", "emissive_scale": 0.65},
}

# These materials are bound to the batched convenience-store stock. Unreal
# does not compile the instanced-static-mesh shader permutation implicitly for
# generated assets; without the persisted usage flag the editor substitutes
# its grey default material at runtime even though the texture graph is valid.
INSTANCED_PRODUCT_MATERIALS = {
    *(f"M_RetailPrice{sku}" for sku in ("Potato", "Shrimp", "Corn", "CupBeef", "CupKimchi", "Biscuit", "Water", "Soda", "Barley", "GreenTea")),
    "M_RetailPET",
    "M_BottleBrown",
    "M_BottleGreen",
    "M_FridgeInterior",
    "M_LabelBarley",
    "M_LabelGreenTea",
    "M_LabelSoda",
    "M_LabelWater",
    "M_LabelWater1L",
    "M_LabelWater2L",
    "M_SnackBlue",
    "M_SnackRed",
    "M_SnackYellow",
    "M_StainlessUV",
}

WRAPPED_LABEL_MATERIALS = {
    "M_LabelBarley",
    "M_LabelGreenTea",
    "M_LabelSoda",
    "M_LabelWater",
    "M_LabelWater1L",
    "M_LabelWater2L",
}

# High-visibility fallback materials still used by the authored convenience
# store props. They remain deliberately inexpensive, but no longer render as
# flat greybox colours: glass gains grazing opacity, bottles gain a coloured
# rim, coated steel carries brushed micro-response, and dark plastics retain a
# readable silhouette without fake emissive light.
OPTICAL_PROP_MATERIALS = {
    "M_RetailPET": {
        "base": (0.18, 0.23, 0.25), "edge": (0.62, 0.69, 0.72),
        "rough": 0.14, "specular": 0.58,
        "opacity_center": 0.23, "opacity_edge": 0.64,
        "refraction": 1.02, "two_sided": False,
    },
    "M_Glass": {
        "base": (0.018, 0.026, 0.030),
        "edge": (0.16, 0.20, 0.21),
        "rough": 0.065, "specular": 0.68,
        "opacity_center": 0.055, "opacity_edge": 0.24,
        "refraction": 1.012, "two_sided": True,
    },
    "M_BottleGreen": {
        "base": (0.025, 0.16, 0.045),
        "edge": (0.14, 0.50, 0.20),
        "rough": 0.17, "specular": 0.64,
    },
    "M_BottleBrown": {
        "base": (0.13, 0.040, 0.012),
        "edge": (0.43, 0.16, 0.035),
        "rough": 0.21, "specular": 0.60,
    },
    "M_FridgeBody": {
        "base": (0.62, 0.64, 0.64),
        "micro_stem": "T_MetalBrushed", "micro_tile": 6.0,
        "normal_strength": 0.18, "rough_low": 0.30,
        "rough_high": 0.47, "metallic": 0.05, "specular": 0.48,
    },
    "M_FridgeInterior": {
        "base": (0.66, 0.69, 0.70),
        "edge": (0.82, 0.85, 0.85),
        "rough": 0.38, "specular": 0.48,
    },
    "M_PlasticDark": {
        "base": (0.020, 0.022, 0.025),
        "edge": (0.075, 0.080, 0.085),
        "rough": 0.34, "specular": 0.52,
    },
    "M_TrashBag": {
        "base": (0.025, 0.030, 0.026),
        "edge": (0.085, 0.095, 0.086),
        "rough": 0.26, "specular": 0.56,
    },
    "M_MetalFrame": {
        "base": (0.30, 0.32, 0.35),
        "micro_stem": "T_MetalBrushed", "micro_tile": 4.0,
        "normal_strength": 0.32, "rough_low": 0.24,
        "rough_high": 0.43, "metallic": 0.88, "specular": 0.50,
    },
}


def _expr(material, expression_class, x=-600, y=0):
    expression = unreal.MaterialEditingLibrary.create_material_expression(
        material, expression_class, x, y
    )
    if expression is None:
        raise RuntimeError(
            f"Could not create {expression_class} on {material.get_path_name()}"
        )
    return expression


def _load_texture(name):
    """Prefers the CC0 photo capture (T_Photo_*) over the procedural fallback."""
    if name.startswith("T_") and not name.startswith("T_Photo_"):
        photo = unreal.load_asset(f"{TEXTURE_ROOT}/T_Photo_{name[2:]}")
        if photo is not None:
            return photo
    texture = unreal.load_asset(f"{TEXTURE_ROOT}/{name}")
    if texture is None:
        raise RuntimeError(f"Missing texture asset: {TEXTURE_ROOT}/{name}")
    return texture


def _texture_exists(assets, name):
    """Matches _load_texture's photo-first lookup without loading a package."""
    if name.startswith("T_") and not name.startswith("T_Photo_"):
        if assets.does_asset_exist(f"{TEXTURE_ROOT}/T_Photo_{name[2:]}"):
            return True
    return assets.does_asset_exist(f"{TEXTURE_ROOT}/{name}")


def _surface_value(spec, base_name, key, default=None):
    """Resolve an explicit material override before its family default."""
    if key in spec:
        return spec[key]
    return SURFACE_RESPONSE_DEFAULTS.get(base_name, {}).get(key, default)


def _mask_channels(material, source, source_pin, channels, x, y):
    mask = _expr(material, unreal.MaterialExpressionComponentMask, x, y)
    mask.set_editor_property("r", "R" in channels)
    mask.set_editor_property("g", "G" in channels)
    mask.set_editor_property("b", "B" in channels)
    mask.set_editor_property("a", "A" in channels)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        source, source_pin, mask, ""
    )
    return mask


def _strengthen_normal(material, source, source_pin, strength, y_offset):
    """Scale tangent XY while preserving Z, then renormalize the vector."""
    if abs(float(strength) - 1.0) < 0.001:
        return source, source_pin
    xy = _mask_channels(material, source, source_pin, "RG", -430, y_offset)
    gain = _expr(material, unreal.MaterialExpressionConstant, -430, y_offset + 130)
    gain.set_editor_property("r", float(strength))
    scaled_xy = _expr(material, unreal.MaterialExpressionMultiply, -250, y_offset)
    unreal.MaterialEditingLibrary.connect_material_expressions(xy, "", scaled_xy, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(gain, "", scaled_xy, "B")
    z = _mask_channels(material, source, source_pin, "B", -250, y_offset + 150)
    packed = _expr(material, unreal.MaterialExpressionAppendVector, -60, y_offset + 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scaled_xy, "", packed, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(z, "", packed, "B")
    normalized = _expr(material, unreal.MaterialExpressionNormalize, 130, y_offset + 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        packed, "", normalized, ""
    )
    return normalized, ""


def _blend_detail_normal(
    material,
    primary,
    primary_pin,
    detail,
    detail_pin,
    strength,
    y_offset,
):
    """Blend a weak high-frequency tangent normal over the authored normal."""
    primary_xy = _mask_channels(
        material, primary, primary_pin, "RG", -180, y_offset
    )
    detail_xy = _mask_channels(
        material, detail, detail_pin, "RG", -180, y_offset + 150
    )
    gain = _expr(material, unreal.MaterialExpressionConstant, 0, y_offset + 260)
    gain.set_editor_property("r", float(strength))
    detail_xy_scaled = _expr(
        material, unreal.MaterialExpressionMultiply, 0, y_offset + 120
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        detail_xy, "", detail_xy_scaled, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        gain, "", detail_xy_scaled, "B"
    )
    combined_xy = _expr(material, unreal.MaterialExpressionAdd, 190, y_offset + 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        primary_xy, "", combined_xy, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        detail_xy_scaled, "", combined_xy, "B"
    )

    primary_z = _mask_channels(
        material, primary, primary_pin, "B", 0, y_offset - 110
    )
    detail_z = _mask_channels(
        material, detail, detail_pin, "B", 0, y_offset + 380
    )
    one = _expr(material, unreal.MaterialExpressionConstant, 190, y_offset + 380)
    one.set_editor_property("r", 1.0)
    detail_z_weighted = _expr(
        material, unreal.MaterialExpressionLinearInterpolate, 370, y_offset + 330
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        one, "", detail_z_weighted, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        detail_z, "", detail_z_weighted, "B"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        gain, "", detail_z_weighted, "Alpha"
    )
    combined_z = _expr(material, unreal.MaterialExpressionMultiply, 550, y_offset + 250)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        primary_z, "", combined_z, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        detail_z_weighted, "", combined_z, "B"
    )
    packed = _expr(material, unreal.MaterialExpressionAppendVector, 550, y_offset + 60)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        combined_xy, "", packed, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        combined_z, "", packed, "B"
    )
    normalized = _expr(material, unreal.MaterialExpressionNormalize, 740, y_offset + 60)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        packed, "", normalized, ""
    )
    return normalized, ""


def _remap_grayscale(material, source, source_pin, low, high, x, y):
    low_value = _expr(material, unreal.MaterialExpressionConstant, x, y)
    low_value.set_editor_property("r", float(low))
    high_value = _expr(material, unreal.MaterialExpressionConstant, x, y + 100)
    high_value.set_editor_property("r", float(high))
    remapped = _expr(
        material, unreal.MaterialExpressionLinearInterpolate, x + 180, y + 40
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        low_value, "", remapped, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        high_value, "", remapped, "B"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        source, source_pin, remapped, "Alpha"
    )
    return remapped, ""


def _make_uv_source(material, mapping, tile, y_offset):
    """Returns an expression producing 2D UVs for the requested mapping."""
    if mapping == "UV":
        coords = _expr(material, unreal.MaterialExpressionTextureCoordinate, -1100, y_offset)
        coords.set_editor_property("u_tiling", tile)
        coords.set_editor_property("v_tiling", tile)
        return coords

    world_position = _expr(
        material, unreal.MaterialExpressionWorldPosition, -1300, y_offset
    )
    mask = _expr(material, unreal.MaterialExpressionComponentMask, -1100, y_offset)
    mask.set_editor_property("r", mapping[0] == "X")
    mask.set_editor_property("g", mapping in ("XY", "YZ"))
    mask.set_editor_property("b", mapping in ("XZ", "YZ"))
    unreal.MaterialEditingLibrary.connect_material_expressions(
        world_position, "", mask, ""
    )

    scale = _expr(material, unreal.MaterialExpressionConstant, -1100, y_offset + 150)
    scale.set_editor_property("r", 1.0 / tile)
    multiply = _expr(material, unreal.MaterialExpressionMultiply, -900, y_offset)
    unreal.MaterialEditingLibrary.connect_material_expressions(mask, "", multiply, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(scale, "", multiply, "B")
    return multiply


_PRINT_ATLAS = texture_atlas_contract.try_load_manifest()


def _atlas_page_texture(page_index):
    """The imported atlas page, or None when the pages are not in the build."""
    return unreal.load_asset(texture_atlas_contract.page_package_path(page_index))


def _atlas_binding(spec):
    """(page texture, uv transform) when this material can read the atlas.

    Everything about the atlas is optional. A checkout that has not run the
    packer, or a spec that tiles or carries its own PBR companions, keeps the
    per-texture path and looks identical -- it just costs its own draw call.
    """
    if _PRINT_ATLAS is None:
        return None
    if spec.get("tile_u") or spec.get("pbr_stem"):
        return None
    stem = spec.get("tex_asset")
    transform = texture_atlas_contract.atlas_uv_transform(_PRINT_ATLAS, stem)
    if transform is None:
        return None
    page_index = _PRINT_ATLAS["entries"][stem]["page"]
    page = _atlas_page_texture(page_index)
    if page is None:
        return None
    return page, transform


def _retire_pre_atlas_samples(material, page):
    """Point any leftover per-texture sampler at the page it was replaced by.

    The in-place update appends a replacement graph and reconnects the
    outputs; it does not delete the old expressions, because deleting from a
    material the prologue CDO is holding can invalidate a rooted object and
    take the editor down before the package saves. Disconnected nodes cost
    nothing in the compiled shader, so that was harmless -- until the atlas.

    A disconnected UMaterialExpressionTextureSample still holds a hard
    UTexture2D pointer, and that pointer is still serialized. So a material
    last touched by a targeted pass keeps its individual texture in the cook
    even though the page is what it draws: the atlas pays for the page and
    saves nothing. Retiring the reference needs no deletion -- the dead node
    can keep its place in the graph as long as it stops naming the texture.
    """
    if page is None:
        return 0
    retired = 0
    for expression in unreal.MaterialEditingLibrary.get_material_expressions(
        material
    ):
        if not isinstance(
            expression, unreal.MaterialExpressionTextureSample
        ):
            continue
        try:
            texture = expression.get_editor_property("texture")
        except Exception:  # noqa: BLE001 - a sampler subclass without one
            continue
        if texture is None or texture == page:
            continue
        if not _is_pre_atlas_texture(str(texture.get_name())):
            continue  # a companion normal/roughness map, still sampled
        expression.set_editor_property("texture", page)
        retired += 1
    return retired


def _is_pre_atlas_texture(name):
    """True for a texture an atlas page replaced, under either of its names.

    _load_texture prefers the photo capture, so the sampler a pre-atlas pass
    left behind may hold T_Photo_Plate401_D rather than T_Plate401_D. Matching
    only the contracted name would leave that one holding its texture, and the
    saving would quietly not happen for exactly the assets that have a capture.
    """
    if name in texture_atlas_contract.PRINT_ATLAS_ENTRIES:
        return True
    if name.startswith("T_Photo_"):
        return f"T_{name[len('T_Photo_'):]}" in (
            texture_atlas_contract.PRINT_ATLAS_ENTRIES)
    return False


def _atlas_uv(material, transform):
    """UV0 * scale + bias, so one page serves a page's worth of artwork."""
    scale_u, scale_v, bias_u, bias_v = transform
    coordinate = _expr(
        material, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
    scale = _expr(material, unreal.MaterialExpressionConstant2Vector, -1400, 150)
    scale.set_editor_property("r", scale_u)
    scale.set_editor_property("g", scale_v)
    scaled = _expr(material, unreal.MaterialExpressionMultiply, -1200, 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        coordinate, "", scaled, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scale, "", scaled, "B")
    bias = _expr(material, unreal.MaterialExpressionConstant2Vector, -1200, 190)
    bias.set_editor_property("r", bias_u)
    bias.set_editor_property("g", bias_v)
    offset = _expr(material, unreal.MaterialExpressionAdd, -1000, 80)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scaled, "", offset, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(
        bias, "", offset, "B")
    return offset


def _sample(material, texture, uv_expression, sampler_type, y_offset):
    sample = _expr(material, unreal.MaterialExpressionTextureSample, -650, y_offset)
    sample.set_editor_property("texture", texture)
    sample.set_editor_property("sampler_type", sampler_type)
    if uv_expression is not None:
        unreal.MaterialEditingLibrary.connect_material_expressions(
            uv_expression, "", sample, "UVs"
        )
    return sample


def _recreate_material(assets, tools, name):
    asset_path = f"{MATERIAL_ROOT}/{name}"
    if assets.does_asset_exist(asset_path) and not assets.delete_asset(asset_path):
        raise RuntimeError(f"Could not replace material: {asset_path}")
    material = tools.create_asset(
        name, MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew()
    )
    if material is None:
        raise RuntimeError(f"Could not create material: {asset_path}")
    return material


def surface_response_marker(spec):
    revision = int(_surface_value(spec, spec["tex"], "response_revision", 1))
    return SURFACE_RESPONSE_MARKER if revision == 1 else f"IG_SurfaceResponse_v{revision}_{spec['tex']}"


def _has_surface_response_marker(material, spec):
    """Return true when this exact bounded response graph is already active.

    Structural materials may be rooted by the scene CDO while this commandlet
    is running, so deleting their old expressions can assert inside Unreal.
    A compiled scalar parameter gives the in-place migration an idempotent,
    package-persistent marker without touching those live references.
    """
    for expression in unreal.MaterialEditingLibrary.get_material_expressions(
        material
    ):
        if not isinstance(
            expression, unreal.MaterialExpressionScalarParameter
        ):
            continue
        if str(expression.get_editor_property("parameter_name")) == (
            surface_response_marker(spec)
        ):
            return True
    return False


def create_textured_materials(assets, tools, specs=None, update_in_place=False):
    created = []
    for name, spec in (specs or TEXTURED_MATERIALS).items():
        asset_path = f"{MATERIAL_ROOT}/{name}"
        if spec.get("retail_finish"):
            import retail_surface_contract
            material = _material_for_layered_update(assets, tools, name, True)
            created.append(retail_surface_contract.author(material, spec["retail_finish"]))
            continue
        if update_in_place and assets.does_asset_exist(asset_path):
            # Most live structural materials are held by the prologue scene
            # CDO before this commandlet begins.  Deleting their packages can
            # leave a valid graph only in memory and no .uasset on disk.  Clear
            # the graph in place so references remain stable and saving is
            # atomic from the editor's point of view.
            material = unreal.load_asset(asset_path)
            if material is None:
                raise RuntimeError(f"Could not load material: {asset_path}")
            if _has_surface_response_marker(material, spec):
                unreal.log(
                    f"[IndieGame] Surface response already current: {name}"
                )
                created.append(material)
                continue
        else:
            material = _recreate_material(assets, tools, name)
        base_name = spec["tex"]
        mapping = spec["mapping"]
        tile = spec["tile"]

        uv_color = _make_uv_source(material, mapping, tile, 0)
        diffuse = _sample(
            material,
            _load_texture(f"T_{base_name}_D"),
            uv_color,
            unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
            0,
        )

        # Colour chain: sample -> optional desaturation -> optional tint.
        # Desaturation is what turns a confetti terrazzo scan into the fine
        # grey speckle of Korean 화강석; a tint alone cannot remove chroma.
        color_source = diffuse
        color_pin = "RGB"
        desaturate = spec.get("desaturate")
        if desaturate is not None:
            fraction = _expr(material, unreal.MaterialExpressionConstant, -650, 300)
            fraction.set_editor_property("r", desaturate)
            grey = _expr(material, unreal.MaterialExpressionDesaturation, -450, 140)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                color_source, color_pin, grey, ""
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                fraction, "", grey, "Fraction"
            )
            color_source = grey
            color_pin = ""

        tint = spec.get("tint")
        if tint:
            tint_constant = _expr(material, unreal.MaterialExpressionConstant3Vector, -650, 220)
            tint_constant.set_editor_property(
                "constant", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0)
            )
            tinted = _expr(material, unreal.MaterialExpressionMultiply, -400, 60)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                color_source, color_pin, tinted, "A"
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                tint_constant, "", tinted, "B"
            )
            color_source = tinted
            color_pin = ""

        # A low-frequency copy of the same calibrated scan breaks the visible
        # wallpaper grid on long walls.  It only changes luminance by a few
        # percent and is disabled for UV props, where object UVs already give
        # each asset a unique frame of the texture.
        macro_strength = float(
            _surface_value(spec, base_name, "macro_strength", 0.0)
        )
        if mapping != "UV" and macro_strength > 0.0:
            macro_scale = float(
                _surface_value(spec, base_name, "macro_scale", 5.0)
            )
            macro_sample = _sample(
                material,
                _load_texture(f"T_{base_name}_D"),
                _make_uv_source(material, mapping, tile * macro_scale, 1480),
                unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                1480,
            )
            macro_fraction = _expr(
                material, unreal.MaterialExpressionConstant, -650, 1660
            )
            macro_fraction.set_editor_property("r", 1.0)
            macro_grey = _expr(
                material, unreal.MaterialExpressionDesaturation, -450, 1500
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                macro_sample, "RGB", macro_grey, ""
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                macro_fraction, "", macro_grey, "Fraction"
            )
            macro_low = 1.0 - macro_strength * 0.5
            macro_high = 1.0 + macro_strength * 0.5
            modulation, modulation_pin = _remap_grayscale(
                material,
                macro_grey,
                "",
                macro_low,
                macro_high,
                -250,
                1510,
            )
            macro_blend = _expr(
                material, unreal.MaterialExpressionMultiply, 120, 100
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                color_source, color_pin, macro_blend, "A"
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                modulation, modulation_pin, macro_blend, "B"
            )
            color_source = macro_blend
            color_pin = ""
        unreal.MaterialEditingLibrary.connect_material_property(
            color_source, color_pin, unreal.MaterialProperty.MP_BASE_COLOR
        )

        normal = _sample(
            material,
            _load_texture(f"T_{base_name}_N"),
            _make_uv_source(material, mapping, tile, 420),
            unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
            420,
        )
        normal_source, normal_pin = _strengthen_normal(
            material,
            normal,
            "RGB",
            float(_surface_value(spec, base_name, "normal_strength", 1.0)),
            420,
        )
        detail_normal_strength = float(
            _surface_value(spec, base_name, "detail_normal_strength", 0.0)
        )
        if detail_normal_strength > 0.0:
            detail_normal_scale = float(
                _surface_value(spec, base_name, "detail_normal_scale", 4.5)
            )
            detail_normal = _sample(
                material,
                _load_texture(f"T_{base_name}_N"),
                _make_uv_source(
                    material,
                    mapping,
                    tile / detail_normal_scale,
                    1900,
                ),
                unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                1900,
            )
            normal_source, normal_pin = _blend_detail_normal(
                material,
                normal_source,
                normal_pin,
                detail_normal,
                "RGB",
                detail_normal_strength,
                2050,
            )
        unreal.MaterialEditingLibrary.connect_material_property(
            normal_source, normal_pin, unreal.MaterialProperty.MP_NORMAL
        )

        rough_asset = f"T_{base_name}_R"
        forced_rough = spec.get("force_rough")
        rough_map_exists = _texture_exists(assets, rough_asset)
        rough_source = None
        rough_pin = ""
        if forced_rough is not None:
            variation = float(
                _surface_value(spec, base_name, "roughness_variation", 0.0)
            )
            if rough_map_exists and variation > 0.0:
                rough_sample = _sample(
                    material,
                    _load_texture(rough_asset),
                    _make_uv_source(material, mapping, tile, 840),
                    unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
                    840,
                )
                rough_source, rough_pin = _remap_grayscale(
                    material,
                    rough_sample,
                    "R",
                    max(0.02, forced_rough * (1.0 - variation)),
                    min(0.98, forced_rough * (1.0 + variation)),
                    -420,
                    900,
                )
            else:
                rough_source = _expr(
                    material, unreal.MaterialExpressionConstant, -650, 880
                )
                rough_source.set_editor_property("r", forced_rough)
        elif rough_map_exists:
            rough_sample = _sample(
                material,
                _load_texture(rough_asset),
                _make_uv_source(material, mapping, tile, 840),
                # Roughness imports use TC_GRAYSCALE.  Sampling them as
                # Linear Color makes the entire material fail compilation
                # and Unreal falls back to the grey checkerboard material.
                unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
                840,
            )
            rough_source = rough_sample
            rough_pin = "R"
            target_roughness = spec.get("rough")
            if target_roughness is not None:
                span = float(spec.get("roughness_map_span", 0.16))
                rough_source, rough_pin = _remap_grayscale(
                    material,
                    rough_sample,
                    "R",
                    max(0.02, float(target_roughness) - span * 0.5),
                    min(0.98, float(target_roughness) + span * 0.5),
                    -420,
                    900,
                )

            rough_detail_strength = float(
                _surface_value(
                    spec, base_name, "roughness_detail_strength", 0.0
                )
            )
            if rough_detail_strength > 0.0:
                rough_detail_scale = float(
                    _surface_value(
                        spec, base_name, "roughness_detail_scale", 4.5
                    )
                )
                detail_rough = _sample(
                    material,
                    _load_texture(rough_asset),
                    _make_uv_source(
                        material,
                        mapping,
                        tile / rough_detail_scale,
                        2700,
                    ),
                    unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
                    2700,
                )
                detail_source = detail_rough
                detail_pin = "R"
                if target_roughness is not None:
                    span = float(spec.get("roughness_map_span", 0.16))
                    detail_source, detail_pin = _remap_grayscale(
                        material,
                        detail_rough,
                        "R",
                        max(0.02, float(target_roughness) - span * 0.5),
                        min(0.98, float(target_roughness) + span * 0.5),
                        -420,
                        2770,
                    )
                detail_weight = _expr(
                    material, unreal.MaterialExpressionConstant, 0, 2840
                )
                detail_weight.set_editor_property("r", rough_detail_strength)
                detail_mix = _expr(
                    material,
                    unreal.MaterialExpressionLinearInterpolate,
                    180,
                    2740,
                )
                unreal.MaterialEditingLibrary.connect_material_expressions(
                    rough_source, rough_pin, detail_mix, "A"
                )
                unreal.MaterialEditingLibrary.connect_material_expressions(
                    detail_source, detail_pin, detail_mix, "B"
                )
                unreal.MaterialEditingLibrary.connect_material_expressions(
                    detail_weight, "", detail_mix, "Alpha"
                )
                rough_source = detail_mix
                rough_pin = ""
        else:
            rough_source = _expr(
                material, unreal.MaterialExpressionConstant, -650, 880
            )
            rough_source.set_editor_property("r", spec.get("rough", 0.8))
        unreal.MaterialEditingLibrary.connect_material_property(
            rough_source, rough_pin, unreal.MaterialProperty.MP_ROUGHNESS
        )

        # Use every authored cavity map, including photo-prefixed assets.  AO
        # is softened rather than multiplied at full strength so fine wallpaper
        # emboss and plaster pores seat into light without dirty black seams.
        ao_asset = f"T_{base_name}_A"
        if _texture_exists(assets, ao_asset):
            ao_sample = _sample(
                material,
                _load_texture(ao_asset),
                _make_uv_source(material, mapping, tile, 1120),
                unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
                1120,
            )
            ao_strength = float(
                _surface_value(
                    spec,
                    base_name,
                    "ao_strength",
                    1.0 if spec.get("ao") else 0.75,
                )
            )
            ao_source, ao_pin = _remap_grayscale(
                material,
                ao_sample,
                "R",
                1.0 - ao_strength,
                1.0,
                -420,
                1180,
            )
            unreal.MaterialEditingLibrary.connect_material_property(
                ao_source, ao_pin, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION
            )

        metallic = spec.get("metallic")
        if metallic is not None:
            metallic_constant = _expr(material, unreal.MaterialExpressionConstant, -650, 1020)
            metallic_constant.set_editor_property("r", metallic)
            unreal.MaterialEditingLibrary.connect_material_property(
                metallic_constant, "", unreal.MaterialProperty.MP_METALLIC
            )

        specular_constant = _expr(
            material, unreal.MaterialExpressionConstant, -650, 1260
        )
        specular_constant.set_editor_property(
            "r", float(_surface_value(spec, base_name, "specular", 0.50))
        )
        response_marker = _expr(
            material, unreal.MaterialExpressionScalarParameter, -650, 1380
        )
        response_marker.set_editor_property(
            "parameter_name", surface_response_marker(spec)
        )
        response_marker.set_editor_property("default_value", 1.0)
        marked_specular = _expr(
            material, unreal.MaterialExpressionMultiply, -420, 1300
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            specular_constant, "", marked_specular, "A"
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            response_marker, "", marked_specular, "B"
        )
        unreal.MaterialEditingLibrary.connect_material_property(
            marked_specular, "", unreal.MaterialProperty.MP_SPECULAR
        )

        unreal.MaterialEditingLibrary.layout_material_expressions(material)
        unreal.MaterialEditingLibrary.recompile_material(material)
        unreal.log(f"[IndieGame] Created textured material: {name}")
        created.append(material)
    return created


def _connect_marked_scalar(
    material, value, marker_name, material_property, x, y
):
    """Connect a constant through a persisted version marker.

    The marker is part of the compiled path, so the UAsset audit can prove the
    live material was migrated rather than merely containing a detached note.
    """
    constant = _expr(material, unreal.MaterialExpressionConstant, x, y)
    constant.set_editor_property("r", float(value))
    marker = _expr(material, unreal.MaterialExpressionScalarParameter, x, y + 120)
    marker.set_editor_property("parameter_name", marker_name)
    marker.set_editor_property("default_value", 1.0)
    marked = _expr(material, unreal.MaterialExpressionMultiply, x + 190, y + 40)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        constant, "", marked, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        marker, "", marked, "B"
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        marked, "", material_property
    )
    return marked


def _connect_print_response(material, base_sample, spec):
    """Give paper and printed film a bounded, reusable micro-surface.

    Labels keep their authored colour as BaseColor. A neutral companion normal
    and roughness map supplies only wrinkle/fibre response, avoiding the common
    mistake where Korean lettering is interpreted as embossed geometry.
    """
    unreal.MaterialEditingLibrary.connect_material_property(
        base_sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR
    )

    micro_stem = spec.get("micro_stem")
    if micro_stem:
        micro_uv = None
        micro_tile = float(spec.get("micro_tile", 1.0))
        if abs(micro_tile - 1.0) > 0.001:
            micro_uv = _expr(
                material, unreal.MaterialExpressionTextureCoordinate, -1040, 640
            )
            micro_uv.set_editor_property("u_tiling", micro_tile)
            micro_uv.set_editor_property("v_tiling", micro_tile)

        normal = _sample(
            material,
            _load_texture(f"{micro_stem}_N"),
            micro_uv,
            unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
            680,
        )
        normal_source, normal_pin = _strengthen_normal(
            material,
            normal,
            "RGB",
            float(spec.get("normal_strength", 0.12)),
            680,
        )
        unreal.MaterialEditingLibrary.connect_material_property(
            normal_source, normal_pin, unreal.MaterialProperty.MP_NORMAL
        )

        roughness = _sample(
            material,
            _load_texture(f"{micro_stem}_R"),
            micro_uv,
            unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
            900,
        )
        rough_low = _expr(material, unreal.MaterialExpressionConstant, -420, 920)
        rough_low.set_editor_property(
            "r", float(spec.get("rough_low", spec.get("rough", 0.6) - 0.06))
        )
        rough_high = _expr(material, unreal.MaterialExpressionConstant, -420, 1040)
        rough_high.set_editor_property(
            "r", float(spec.get("rough_high", spec.get("rough", 0.6) + 0.06))
        )
        rough_mix = _expr(
            material, unreal.MaterialExpressionLinearInterpolate, -200, 960
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            rough_low, "", rough_mix, "A"
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            rough_high, "", rough_mix, "B"
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            roughness, "R", rough_mix, "Alpha"
        )
        unreal.MaterialEditingLibrary.connect_material_property(
            rough_mix, "", unreal.MaterialProperty.MP_ROUGHNESS
        )

        if spec.get("ao"):
            ao = _sample(
                material,
                _load_texture(f"{micro_stem}_A"),
                micro_uv,
                unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
                1160,
            )
            unreal.MaterialEditingLibrary.connect_material_property(
                ao, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION
            )
    else:
        roughness = _expr(material, unreal.MaterialExpressionConstant, -650, 340)
        roughness.set_editor_property("r", spec.get("rough", 0.6))
        unreal.MaterialEditingLibrary.connect_material_property(
            roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
        )

    _connect_marked_scalar(
        material,
        spec.get("specular", 0.32),
        PRINT_RESPONSE_MARKER,
        unreal.MaterialProperty.MP_SPECULAR,
        -160,
        1220,
    )


def create_flat_texture_materials(
    assets, tools, specs, emissive_only, update_in_place=False
):
    created = []
    skipped = []
    atlassed = []
    retired = 0
    for name, spec in specs.items():
        # Artwork arrives in batches — a generated sheet may not have landed
        # yet. Skipping the material is right: the C++ side already falls back
        # to a flat colour for anything it cannot load, so a half-finished
        # asset run still produces a playable build.
        source_asset = spec["tex_asset"]
        if not (
            assets.does_asset_exist(f"{TEXTURE_ROOT}/{source_asset}")
            or assets.does_asset_exist(f"{TEXTURE_ROOT}/T_Photo_{source_asset[2:]}")
            or _atlas_binding(spec) is not None
        ):
            skipped.append(name)
            continue

        asset_path = f"{MATERIAL_ROOT}/{name}"
        if update_in_place and assets.does_asset_exist(asset_path):
            # Sign materials are loaded by the prologue scene CDO while this
            # commandlet is running, so deleting their packages fails with a
            # sharing violation. Rebuilding the graph in place keeps those
            # live references valid and still saves the corrected asset.
            material = unreal.load_asset(asset_path)
            if material is None:
                raise RuntimeError(f"Could not load material: {asset_path}")
            # Rooted materials are already referenced by the prologue CDO in
            # commandlet runs. Appending a replacement graph and reconnecting
            # the outputs is safe; the compiler prunes the disconnected legacy
            # nodes. Deleting every expression here can invalidate a rooted
            # object and crash the editor before the package is saved.
        else:
            material = _recreate_material(assets, tools, name)
        material.set_editor_property("two_sided", bool(spec.get("two_sided", False)))

        uv = None
        binding = _atlas_binding(spec)
        if binding is not None:
            texture, transform = binding
            uv = _atlas_uv(material, transform)
            # An in-place update leaves the pre-atlas sampler in the graph,
            # and a disconnected sampler still holds -- and still cooks --
            # the texture the page replaced.
            retired += _retire_pre_atlas_samples(material, texture)
            atlassed.append(name)
        else:
            texture = _load_texture(source_asset)
            tile_u = spec.get("tile_u")
            if tile_u:
                uv = _expr(
                    material, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
                uv.set_editor_property("u_tiling", tile_u)
                uv.set_editor_property("v_tiling", 1.0)

        sample = _sample(
            material, texture, uv, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 0
        )

        # 위층 사람 셸의 미동: 느린 법선 방향 팽창(숨)과 위상이 몸을 타고
        # 흐르는 잔떨림. 뼈대 없이 정적 메시를 살아 있게 하는 WPO다. 진폭
        # 둘만 파라미터라서 상태 머신이 MID로 죽이고 살린다 — Waiting에서
        # 숨이 멎는 것이 대답 노크가 통했다는 몸의 확인이다.
        if spec.get("breath"):
            breath = spec["breath"]
            time_expr = _expr(material, unreal.MaterialExpressionTime, -1300, 1300)
            breath_rate = _expr(
                material, unreal.MaterialExpressionConstant, -1300, 1440)
            breath_rate.set_editor_property("r", float(breath.get("rate", 0.22)))
            breath_phase = _expr(
                material, unreal.MaterialExpressionMultiply, -1100, 1320)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                time_expr, "", breath_phase, "A")
            unreal.MaterialEditingLibrary.connect_material_expressions(
                breath_rate, "", breath_phase, "B")
            breath_wave = _expr(material, unreal.MaterialExpressionSine, -950, 1320)
            breath_wave.set_editor_property("period", 1.0)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                breath_phase, "", breath_wave, "")
            breath_amp = _expr(
                material, unreal.MaterialExpressionScalarParameter, -950, 1460)
            breath_amp.set_editor_property("parameter_name", "BreathAmplitude")
            breath_amp.set_editor_property(
                "default_value", float(breath.get("amplitude", 0.45)))
            breath_offset = _expr(
                material, unreal.MaterialExpressionMultiply, -760, 1340)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                breath_wave, "", breath_offset, "A")
            unreal.MaterialEditingLibrary.connect_material_expressions(
                breath_amp, "", breath_offset, "B")

            world_position = _expr(
                material, unreal.MaterialExpressionWorldPosition, -1300, 1580)
            ripple_direction = _expr(
                material, unreal.MaterialExpressionConstant3Vector, -1300, 1720)
            ripple_direction.set_editor_property(
                "constant", unreal.LinearColor(0.011, 0.007, 0.013, 0.0))
            ripple = _expr(
                material, unreal.MaterialExpressionDotProduct, -1100, 1620)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                world_position, "", ripple, "A")
            unreal.MaterialEditingLibrary.connect_material_expressions(
                ripple_direction, "", ripple, "B")
            tremor_rate = _expr(
                material, unreal.MaterialExpressionConstant, -1300, 1860)
            tremor_rate.set_editor_property(
                "r", float(breath.get("tremor_rate", 7.0)))
            tremor_time = _expr(
                material, unreal.MaterialExpressionMultiply, -1100, 1780)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                time_expr, "", tremor_time, "A")
            unreal.MaterialEditingLibrary.connect_material_expressions(
                tremor_rate, "", tremor_time, "B")
            tremor_phase = _expr(material, unreal.MaterialExpressionAdd, -950, 1700)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                tremor_time, "", tremor_phase, "A")
            unreal.MaterialEditingLibrary.connect_material_expressions(
                ripple, "", tremor_phase, "B")
            tremor_wave = _expr(material, unreal.MaterialExpressionSine, -800, 1700)
            tremor_wave.set_editor_property("period", 1.0)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                tremor_phase, "", tremor_wave, "")
            tremor_amp = _expr(
                material, unreal.MaterialExpressionScalarParameter, -800, 1840)
            tremor_amp.set_editor_property("parameter_name", "TremorAmplitude")
            tremor_amp.set_editor_property(
                "default_value", float(breath.get("tremor", 0.1)))
            tremor_offset = _expr(
                material, unreal.MaterialExpressionMultiply, -640, 1720)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                tremor_wave, "", tremor_offset, "A")
            unreal.MaterialEditingLibrary.connect_material_expressions(
                tremor_amp, "", tremor_offset, "B")

            vitals = _expr(material, unreal.MaterialExpressionAdd, -500, 1520)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                breath_offset, "", vitals, "A")
            unreal.MaterialEditingLibrary.connect_material_expressions(
                tremor_offset, "", vitals, "B")
            surface_normal = _expr(
                material, unreal.MaterialExpressionVertexNormalWS, -500, 1660)
            vitals_offset = _expr(
                material, unreal.MaterialExpressionMultiply, -340, 1560)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                surface_normal, "", vitals_offset, "A")
            unreal.MaterialEditingLibrary.connect_material_expressions(
                vitals, "", vitals_offset, "B")
            unreal.MaterialEditingLibrary.connect_material_property(
                vitals_offset, "",
                unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET
            )

        emissive_scale = spec.get("emissive_scale", 0.0)
        if emissive_only:
            dark = _expr(material, unreal.MaterialExpressionConstant3Vector, -650, 300)
            dark.set_editor_property("constant", unreal.LinearColor(0.02, 0.02, 0.02, 1.0))
            unreal.MaterialEditingLibrary.connect_material_property(
                dark, "", unreal.MaterialProperty.MP_BASE_COLOR
            )
        else:
            if spec.get("pbr_stem"):
                _connect_scan_pbr(material, sample, uv, spec)
            else:
                _connect_print_response(material, sample, spec)

        if emissive_scale > 0.0:
            scale_constant = _expr(material, unreal.MaterialExpressionConstant, -650, 500)
            scale_constant.set_editor_property("r", emissive_scale)
            emissive = _expr(material, unreal.MaterialExpressionMultiply, -400, 460)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                sample, "RGB", emissive, "A"
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                scale_constant, "", emissive, "B"
            )
            unreal.MaterialEditingLibrary.connect_material_property(
                emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
            )

        unreal.MaterialEditingLibrary.layout_material_expressions(material)
        unreal.MaterialEditingLibrary.recompile_material(material)
        unreal.log(f"[IndieGame] Created sign material: {name}")
        created.append(material)

    if atlassed:
        unreal.log_warning(
            f"[IndieGame] {len(atlassed)} print material(s) read the shared "
            "atlas page instead of their own texture"
        )
    if retired:
        unreal.log_warning(
            f"[IndieGame] retired {retired} pre-atlas sampler reference(s); "
            "without this a targeted in-place pass keeps the replaced "
            "textures in the cook"
        )
    if skipped:
        unreal.log_warning(
            f"[IndieGame] Skipped {len(skipped)} material(s) with no artwork yet: "
            + ", ".join(skipped)
        )
    return created


def create_masked_texture_materials(assets, tools, specs, mask_only):
    """Builds two-sided plane overlays without translucent sorting.

    Evidence masks use R as opacity and a physically wet constant surface.
    Chroma-keyed environmental overlays use RGB for colour and A for opacity.
    The planes sit a few millimetres above authored surfaces, so masked mode
    avoids the halo/sort failures that are especially visible in flashlight
    sweeps.
    """
    created = []
    for name, spec in specs.items():
        if name == "M_ApartmentWallPatina":
            created.append(create_apartment_patina_material(assets, tools))
            continue
        source_asset = spec["tex_asset"]
        if not assets.does_asset_exist(f"{TEXTURE_ROOT}/{source_asset}"):
            unreal.log_warning(
                f"[IndieGame] Skipped {name}: missing {source_asset}"
            )
            continue

        material = _recreate_material(assets, tools, name)
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        material.set_editor_property("two_sided", True)
        material.set_editor_property("opacity_mask_clip_value", 0.08)
        texture = _load_texture(source_asset)
        sample = _sample(
            material, texture, None,
            (unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
             if mask_only else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
            0,
        )

        if mask_only:
            gain = _expr(material, unreal.MaterialExpressionConstant, -760, 180)
            gain.set_editor_property("r", spec.get("mask_gain", 4.0))
            amplified = _expr(material, unreal.MaterialExpressionMultiply, -560, 100)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                sample, "R", amplified, "A"
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                gain, "", amplified, "B"
            )
            opacity = _expr(material, unreal.MaterialExpressionSaturate, -380, 100)
            unreal.MaterialEditingLibrary.connect_material_expressions(
                amplified, "", opacity, ""
            )
            unreal.MaterialEditingLibrary.connect_material_property(
                opacity, "", unreal.MaterialProperty.MP_OPACITY_MASK
            )
            color = spec.get("color", (0.025, 0.034, 0.038))
            base = _expr(material, unreal.MaterialExpressionConstant3Vector, -380, -80)
            base.set_editor_property(
                "constant", unreal.LinearColor(color[0], color[1], color[2], 1.0)
            )
            substrate = spec.get("substrate")
            if substrate is None:
                unreal.MaterialEditingLibrary.connect_material_property(
                    base, "", unreal.MaterialProperty.MP_BASE_COLOR
                )
            else:
                # 얇은 잔흔은 바닥으로 사라진다. 검정으로 사라지지 않는다.
                #
                # 앞선 판은 같은 마스크로 알베도를 곱해서 농도를 만들었다.
                # BLEND_MASKED에는 부분 투명이 없으니 얇은 자리를 표현할
                # 방법이 알베도뿐인데, 곱셈은 그것을 **검정 쪽으로** 끌어당겼다.
                # 프레임에서 잰 결과가 그대로 나왔다 — 잔흔이 덮은 픽셀의
                # 중앙값이 바로 인접한 바닥의 0.84배, 즉 석고 분진이 아니라
                # 흙때로 읽혔다.
                #
                # 물리적으로 얇은 가루층은 기질과 가루의 혼합이다. 그래서
                # 기질색에서 잔흔색으로 보간한다. 마스크가 옅은 자리는 기질과
                # 같아져 사라지고, 짙게 쌓인 심지만 밝아진다. 기질값은 그 면의
                # 실제 재질에서 온다 — 별관 바닥은 콘크리트 다크(확산맵 선형
                # 0.180 × 틴트 0.32), 베이 벽은 마른 석고(0.486 × 0.78).
                ground = _expr(
                    material, unreal.MaterialExpressionConstant3Vector, -760, 320
                )
                ground.set_editor_property(
                    "constant",
                    unreal.LinearColor(
                        substrate[0], substrate[1], substrate[2], 1.0
                    ),
                )
                shade = _expr(
                    material,
                    unreal.MaterialExpressionLinearInterpolate,
                    -560,
                    300,
                )
                unreal.MaterialEditingLibrary.connect_material_expressions(
                    ground, "", shade, "A"
                )
                unreal.MaterialEditingLibrary.connect_material_expressions(
                    base, "", shade, "B"
                )
                # 불투명도와 같은 증폭값을 쓴다. 실루엣과 농도가 어긋나면
                # 테두리에 밝은 띠가 생긴다.
                unreal.MaterialEditingLibrary.connect_material_expressions(
                    opacity, "", shade, "Alpha"
                )
                unreal.MaterialEditingLibrary.connect_material_property(
                    shade, "", unreal.MaterialProperty.MP_BASE_COLOR
                )
            specular = _expr(material, unreal.MaterialExpressionConstant, -380, 360)
            specular.set_editor_property("r", spec.get("specular", 0.62))
            unreal.MaterialEditingLibrary.connect_material_property(
                specular, "", unreal.MaterialProperty.MP_SPECULAR
            )
        elif spec.get("pbr_stem"):
            _connect_scan_pbr(material, sample, None, spec)
            unreal.MaterialEditingLibrary.connect_material_property(
                sample, "A", unreal.MaterialProperty.MP_OPACITY_MASK
            )
        else:
            unreal.MaterialEditingLibrary.connect_material_property(
                sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR
            )
            unreal.MaterialEditingLibrary.connect_material_property(
                sample, "A", unreal.MaterialProperty.MP_OPACITY_MASK
            )

        if not spec.get("pbr_stem"):
            roughness = _expr(material, unreal.MaterialExpressionConstant, -180, 300)
            roughness.set_editor_property("r", spec.get("rough", 0.75))
            unreal.MaterialEditingLibrary.connect_material_property(
                roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
            )
        unreal.MaterialEditingLibrary.layout_material_expressions(material)
        unreal.MaterialEditingLibrary.recompile_material(material)
        unreal.log(f"[IndieGame] Created masked overlay material: {name}")
        created.append(material)
    return created


def create_apartment_patina_material(assets, tools):
    """벽지 무늬를 남기고 가장자리에서 옅어지는 습기 자국."""
    material = _recreate_material(assets, tools, "M_ApartmentWallPatina")
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    sample = _sample(material, _load_texture("T_ApartmentWallPatina_M"), None,
                     unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 0)
    uv = _expr(material, unreal.MaterialExpressionTextureCoordinate, -1000, 300)
    inverse = _expr(material, unreal.MaterialExpressionOneMinus, -850, 400)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", inverse, "")
    edge = _expr(material, unreal.MaterialExpressionMultiply, -700, 320)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", edge, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(inverse, "", edge, "B")
    # 16u(1-u)v(1-v): 원본 그림이 닿은 사각 경계까지 농도가 남지 않는다.
    channels = []
    for index in range(2):
        channel = _expr(material, unreal.MaterialExpressionComponentMask, -550, 300 + index*120)
        channel.set_editor_property("r", index == 0)
        channel.set_editor_property("g", index == 1)
        if not unreal.MaterialEditingLibrary.connect_material_expressions(edge, "", channel, ""):
            raise RuntimeError("벽 얼룩의 테두리 계산 연결 실패")
        channels.append(channel)
    border = _expr(material, unreal.MaterialExpressionMultiply, -380, 360)
    unreal.MaterialEditingLibrary.connect_material_expressions(channels[0], "", border, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(channels[1], "", border, "B")
    density = _expr(material, unreal.MaterialExpressionMultiply, -200, 240)
    density.set_editor_property("const_b", 16.0 * 0.48)
    unreal.MaterialEditingLibrary.connect_material_expressions(border, "", density, "A")
    opacity = _expr(material, unreal.MaterialExpressionMultiply, 0, 120)
    unreal.MaterialEditingLibrary.connect_material_expressions(density, "", opacity, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(sample, "R", opacity, "B")
    unreal.MaterialEditingLibrary.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    color = _expr(material, unreal.MaterialExpressionConstant3Vector, -200, -80)
    color.set_editor_property("constant", unreal.LinearColor(0.12, 0.105, 0.078, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    return material


def create_corridor_scuff_material(assets, tools):
    """화강석 무늬와 줄눈이 비치는 옅은 끌림 자국."""
    material = _recreate_material(assets, tools, "M_CorridorCasterScuff")
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # 색만 얹는다. 기존 돌의 노멀과 거칠기는 바꾸지 않는다.
    sample = _sample(material, _load_texture("T_MissingFloorDragTrails_M"), None,
                     unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 0)
    opacity = _expr(material, unreal.MaterialExpressionMultiply, -300, 80)
    opacity.set_editor_property("const_b", 0.18)
    unreal.MaterialEditingLibrary.connect_material_expressions(sample, "R", opacity, "A")
    unreal.MaterialEditingLibrary.connect_material_property(
        opacity, "", unreal.MaterialProperty.MP_OPACITY)
    color = _expr(material, unreal.MaterialExpressionConstant3Vector, -300, -80)
    color.set_editor_property("constant", unreal.LinearColor(0.12, 0.115, 0.105, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(
        color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    return material


def _material_for_layered_update(assets, tools, name, update_in_place):
    asset_path = f"{MATERIAL_ROOT}/{name}"
    if update_in_place and assets.does_asset_exist(asset_path):
        material = unreal.load_asset(asset_path)
        if material is None:
            raise RuntimeError(f"Could not load material: {asset_path}")
        return material
    return _recreate_material(assets, tools, name)


def create_optical_prop_materials(assets, tools, update_in_place=False):
    """Build the cheap but physically legible glass/metal/plastic fallbacks."""
    created = []
    for name, spec in OPTICAL_PROP_MATERIALS.items():
        material = _material_for_layered_update(
            assets, tools, name, update_in_place
        )
        material.set_editor_property("two_sided", bool(spec.get("two_sided", False)))
        if "opacity_center" in spec:
            material.set_editor_property(
                "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT
            )
            material.set_editor_property(
                "translucency_lighting_mode",
                unreal.TranslucencyLightingMode.TLM_SURFACE,
            )

        base_value = spec["base"]
        base = _expr(
            material, unreal.MaterialExpressionConstant3Vector, -760, -100
        )
        base.set_editor_property(
            "constant",
            unreal.LinearColor(base_value[0], base_value[1], base_value[2], 1.0),
        )
        fresnel = None
        if "edge" in spec:
            edge_value = spec["edge"]
            edge = _expr(
                material, unreal.MaterialExpressionConstant3Vector, -760, 40
            )
            edge.set_editor_property(
                "constant",
                unreal.LinearColor(edge_value[0], edge_value[1], edge_value[2], 1.0),
            )
            fresnel = _expr(material, unreal.MaterialExpressionFresnel, -560, 60)
            rim_base = _expr(
                material, unreal.MaterialExpressionLinearInterpolate, -340, -40
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                base, "", rim_base, "A"
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                edge, "", rim_base, "B"
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                fresnel, "", rim_base, "Alpha"
            )
            base_source = rim_base
        else:
            base_source = base
        unreal.MaterialEditingLibrary.connect_material_property(
            base_source, "", unreal.MaterialProperty.MP_BASE_COLOR
        )

        micro_stem = spec.get("micro_stem")
        if micro_stem:
            micro_uv = _expr(
                material, unreal.MaterialExpressionTextureCoordinate, -1040, 360
            )
            micro_uv.set_editor_property(
                "u_tiling", float(spec.get("micro_tile", 1.0))
            )
            micro_uv.set_editor_property(
                "v_tiling", float(spec.get("micro_tile", 1.0))
            )
            normal = _sample(
                material,
                _load_texture(f"{micro_stem}_N"),
                micro_uv,
                unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                360,
            )
            normal_source, normal_pin = _strengthen_normal(
                material,
                normal,
                "RGB",
                float(spec.get("normal_strength", 0.25)),
                420,
            )
            unreal.MaterialEditingLibrary.connect_material_property(
                normal_source, normal_pin, unreal.MaterialProperty.MP_NORMAL
            )
            rough_map = _sample(
                material,
                _load_texture(f"{micro_stem}_R"),
                micro_uv,
                unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
                720,
            )
            rough_low = _expr(
                material, unreal.MaterialExpressionConstant, -420, 760
            )
            rough_low.set_editor_property("r", float(spec["rough_low"]))
            rough_high = _expr(
                material, unreal.MaterialExpressionConstant, -420, 880
            )
            rough_high.set_editor_property("r", float(spec["rough_high"]))
            roughness = _expr(
                material, unreal.MaterialExpressionLinearInterpolate, -200, 800
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                rough_low, "", roughness, "A"
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                rough_high, "", roughness, "B"
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                rough_map, "R", roughness, "Alpha"
            )
        else:
            roughness = _expr(
                material, unreal.MaterialExpressionConstant, -240, 760
            )
            roughness.set_editor_property("r", float(spec["rough"]))
        unreal.MaterialEditingLibrary.connect_material_property(
            roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
        )

        if "metallic" in spec:
            metallic = _expr(
                material, unreal.MaterialExpressionConstant, -240, 980
            )
            metallic.set_editor_property("r", float(spec["metallic"]))
            unreal.MaterialEditingLibrary.connect_material_property(
                metallic, "", unreal.MaterialProperty.MP_METALLIC
            )
        _connect_marked_scalar(
            material,
            spec.get("specular", 0.5),
            OPTICAL_RESPONSE_MARKER,
            unreal.MaterialProperty.MP_SPECULAR,
            -80,
            1100,
        )

        if "opacity_center" in spec:
            if fresnel is None:
                fresnel = _expr(
                    material, unreal.MaterialExpressionFresnel, -560, 60
                )
            opacity_center = _expr(
                material, unreal.MaterialExpressionConstant, -560, 1240
            )
            opacity_center.set_editor_property("r", float(spec["opacity_center"]))
            opacity_edge = _expr(
                material, unreal.MaterialExpressionConstant, -560, 1360
            )
            opacity_edge.set_editor_property("r", float(spec["opacity_edge"]))
            opacity = _expr(
                material, unreal.MaterialExpressionLinearInterpolate, -340, 1300
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                opacity_center, "", opacity, "A"
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                opacity_edge, "", opacity, "B"
            )
            unreal.MaterialEditingLibrary.connect_material_expressions(
                fresnel, "", opacity, "Alpha"
            )
            unreal.MaterialEditingLibrary.connect_material_property(
                opacity, "", unreal.MaterialProperty.MP_OPACITY
            )
            refraction = _expr(
                material, unreal.MaterialExpressionConstant, -120, 1440
            )
            refraction.set_editor_property("r", float(spec["refraction"]))
            unreal.MaterialEditingLibrary.connect_material_property(
                refraction, "", unreal.MaterialProperty.MP_REFRACTION
            )

        if name in INSTANCED_PRODUCT_MATERIALS:
            material.set_editor_property("used_with_instanced_static_meshes", True)
        unreal.MaterialEditingLibrary.layout_material_expressions(material)
        unreal.MaterialEditingLibrary.recompile_material(material)
        unreal.log(f"[IndieGame] Created optical prop response: {name}")
        created.append(material)
    return created


def create_carrier_bag_material(assets, tools, update_in_place=False, construction=False):
    """Thin printed LDPE without an opaque glass-box silhouette.

    The texture supplies wrinkles and fictional pale-blue print. Opacity stays
    in a narrow physical range, so the bottles inside stay visible in every
    purchase profile.
    """
    source_asset = "T_CarrierBagFilm_D"
    if not assets.does_asset_exist(f"{TEXTURE_ROOT}/{source_asset}"):
        unreal.log_warning(
            f"[IndieGame] Skipped M_CarrierBagFilm: missing {source_asset}"
        )
        return None

    name = "M_ConstructionFilm" if construction else "M_CarrierBagFilm"
    material = _material_for_layered_update(assets, tools, name, update_in_place)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)
    if construction:
        # 표면 주름의 반사가 보이는 보양 비닐. 장바구니 재질에는 영향을 주지 않는다.
        material.set_editor_property("translucency_lighting_mode",
            unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    sample = _sample(
        material,
        _load_texture(source_asset),
        None,
        unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
        0,
    )
    _connect_scan_pbr(
        material,
        sample,
        None,
        {
            "pbr_stem": "T_CarrierBagFilm",
            "specular": 0.52,
        },
    )

    opacity_scale = _expr(material, unreal.MaterialExpressionConstant, -620, 220)
    opacity_scale.set_editor_property("r", 0.40 if construction else 0.24)
    opacity_detail = _expr(material, unreal.MaterialExpressionMultiply, -420, 160)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        sample, "R", opacity_detail, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        opacity_scale, "", opacity_detail, "B"
    )
    opacity_floor = _expr(material, unreal.MaterialExpressionConstant, -420, 300)
    opacity_floor.set_editor_property("r", 0.28 if construction else 0.11)
    opacity = _expr(material, unreal.MaterialExpressionAdd, -220, 210)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        opacity_detail, "", opacity, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        opacity_floor, "", opacity, "B"
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        opacity, "", unreal.MaterialProperty.MP_OPACITY
    )

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.log(f"[IndieGame] Created film material: {name}")
    return material


def enable_instanced_product_usage(created):
    """Persist the shader permutation required by batched retail props."""
    by_name = {material.get_name(): material for material in created}
    for name in sorted(INSTANCED_PRODUCT_MATERIALS):
        material = by_name.get(name)
        if material is None:
            material = unreal.load_asset(f"{MATERIAL_ROOT}/{name}")
        if material is None:
            raise RuntimeError(f"Missing instanced product material: {name}")
        material.set_editor_property("used_with_instanced_static_meshes", True)
        if name in WRAPPED_LABEL_MATERIALS:
            # A closed film sleeve has no meaningful exposed back, but making
            # these tiny surfaces two-sided prevents a platform winding-rule
            # difference from turning the wrap invisible. The affected pixel
            # area is negligible compared with the cooler glass behind it.
            material.set_editor_property("two_sided", True)
        unreal.MaterialEditingLibrary.recompile_material(material)
        if material not in created:
            created.append(material)
    return created


def _connect_scan_pbr(material, base_sample, uv, spec):
    """Connect a generated scan as a complete, flashlight-reactive PBR surface."""
    stem = spec["pbr_stem"]
    wet_sample = None
    wet_output = None
    wet_output_pin = "R"
    if "wet_rough" in spec:
        wet_sample = _sample(
            material,
            _load_texture(f"{stem}_W"),
            uv,
            unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
            620,
        )
        wet_output = wet_sample
        wet_dark = _expr(material, unreal.MaterialExpressionConstant, -420, 700)
        wet_dark.set_editor_property("r", spec.get("wet_dark", 0.75))
        darkened = _expr(material, unreal.MaterialExpressionMultiply, -220, 80)
        unreal.MaterialEditingLibrary.connect_material_expressions(base_sample, "RGB", darkened, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(wet_dark, "", darkened, "B")
        wet_base = _expr(material, unreal.MaterialExpressionLinearInterpolate, 0, 20)
        unreal.MaterialEditingLibrary.connect_material_expressions(base_sample, "RGB", wet_base, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(darkened, "", wet_base, "B")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            wet_output, wet_output_pin, wet_base, "Alpha"
        )
        base_output = wet_base
        base_output_pin = ""
    else:
        base_output = base_sample
        base_output_pin = "RGB"

    normal_sample = _sample(
        material,
        _load_texture(f"{stem}_N"),
        uv,
        unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
        180,
    )
    normal_output = normal_sample
    normal_output_pin = "RGB"
    if wet_sample is not None and spec.get("wet_normal_flatten", 0.0) > 0.0:
        flatten_scale = _expr(material, unreal.MaterialExpressionConstant, -420, 820)
        flatten_scale.set_editor_property("r", spec["wet_normal_flatten"])
        flatten_alpha = _expr(material, unreal.MaterialExpressionMultiply, -220, 760)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            wet_output, wet_output_pin, flatten_alpha, "A"
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(flatten_scale, "", flatten_alpha, "B")
        flat_normal = _expr(material, unreal.MaterialExpressionConstant3Vector, -220, 900)
        flat_normal.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
        flattened = _expr(material, unreal.MaterialExpressionLinearInterpolate, 0, 220)
        unreal.MaterialEditingLibrary.connect_material_expressions(normal_sample, "RGB", flattened, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(flat_normal, "", flattened, "B")
        unreal.MaterialEditingLibrary.connect_material_expressions(flatten_alpha, "", flattened, "Alpha")
        normal_output = flattened
        normal_output_pin = ""
    # ASSET_STYLE 통합 규칙: 위층 사람만 균열 노멀을 벽보다 세게 받는다.
    # 벽 재질에는 이 키가 없어 접선 XY가 그대로 지나간다.
    crack_strength = spec.get("crack_normal_strength")
    if crack_strength:
        normal_output, normal_output_pin = _strengthen_normal(
            material, normal_output, normal_output_pin, crack_strength, 1180
        )

    rough_sample = _sample(
        material,
        _load_texture(f"{stem}_R"),
        uv,
        unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
        340,
    )
    rough_output = rough_sample
    rough_output_pin = "R"
    if wet_sample is not None:
        wet_rough = _expr(material, unreal.MaterialExpressionConstant, -220, 1040)
        wet_rough.set_editor_property("r", spec["wet_rough"])
        rough_lerp = _expr(material, unreal.MaterialExpressionLinearInterpolate, 0, 400)
        unreal.MaterialEditingLibrary.connect_material_expressions(rough_sample, "R", rough_lerp, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(wet_rough, "", rough_lerp, "B")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            wet_output, wet_output_pin, rough_lerp, "Alpha"
        )
        rough_output = rough_lerp
        rough_output_pin = ""

    ao_sample = _sample(
        material,
        _load_texture(f"{stem}_A"),
        uv,
        unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
        500,
    )
    ao_output = ao_sample
    ao_output_pin = "R"

    # 골 분진. 메시에 구운 정점 AO(R, 1=트임 0=골)를 읽어 골에 마른 석고
    # 가루를 앉힌다: 알베도는 탁하고 밝게, 거칠기는 무광 끝까지, 균열 노멀은
    # 가루가 메운 만큼 죽이고, 구운 폐색은 A맵 위에 겹쳐 골을 더 깊이
    # 앉힌다. 색조는 같은 석고 계열에서만 움직인다(§통합 비주얼 규칙).
    # 정점색이 없는 메시는 흰색으로 읽혀 분진이 정확히 0이 되는 폴백이다.
    if "cavity_dust" in spec:
        dust = spec["cavity_dust"]
        vertex_color = _expr(
            material, unreal.MaterialExpressionVertexColor, -650, 1400)
        occlusion = _mask_channels(material, vertex_color, "", "R", -470, 1400)
        cavity = _expr(material, unreal.MaterialExpressionOneMinus, -300, 1400)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            occlusion, "", cavity, "")
        dust_amount = _expr(
            material, unreal.MaterialExpressionScalarParameter, -300, 1540)
        dust_amount.set_editor_property("parameter_name", "DustAmount")
        dust_amount.set_editor_property(
            "default_value", float(dust.get("amount", 1.0)))
        dust_raw = _expr(material, unreal.MaterialExpressionMultiply, -120, 1440)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            cavity, "", dust_raw, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            dust_amount, "", dust_raw, "B")
        dust_mask = _expr(material, unreal.MaterialExpressionSaturate, 40, 1440)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            dust_raw, "", dust_mask, "")

        desaturated = _expr(
            material, unreal.MaterialExpressionDesaturation, -120, 1620)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            base_output, base_output_pin, desaturated, "")
        desat_fraction = _expr(
            material, unreal.MaterialExpressionConstant, -300, 1700)
        desat_fraction.set_editor_property("r", float(dust.get("desat", 0.4)))
        unreal.MaterialEditingLibrary.connect_material_expressions(
            desat_fraction, "", desaturated, "Fraction")
        dust_lift = _expr(material, unreal.MaterialExpressionConstant, 40, 1700)
        dust_lift.set_editor_property("r", float(dust.get("lift", 1.2)))
        dust_tone = _expr(material, unreal.MaterialExpressionMultiply, 220, 1620)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            desaturated, "", dust_tone, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            dust_lift, "", dust_tone, "B")
        dusted_base = _expr(
            material, unreal.MaterialExpressionLinearInterpolate, 400, 1480)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            base_output, base_output_pin, dusted_base, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            dust_tone, "", dusted_base, "B")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            dust_mask, "", dusted_base, "Alpha")
        base_output = dusted_base
        base_output_pin = ""

        dust_rough = _expr(material, unreal.MaterialExpressionConstant, 220, 1780)
        dust_rough.set_editor_property("r", 0.97)
        dusted_rough = _expr(
            material, unreal.MaterialExpressionLinearInterpolate, 400, 1720)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            rough_output, rough_output_pin, dusted_rough, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            dust_rough, "", dusted_rough, "B")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            dust_mask, "", dusted_rough, "Alpha")
        rough_output = dusted_rough
        rough_output_pin = ""

        fill_scale = _expr(material, unreal.MaterialExpressionConstant, 220, 1860)
        fill_scale.set_editor_property("r", float(dust.get("flatten", 0.45)))
        fill_alpha = _expr(material, unreal.MaterialExpressionMultiply, 400, 1860)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            dust_mask, "", fill_alpha, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            fill_scale, "", fill_alpha, "B")
        filled_flat = _expr(
            material, unreal.MaterialExpressionConstant3Vector, 400, 1940)
        filled_flat.set_editor_property(
            "constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
        filled_normal = _expr(
            material, unreal.MaterialExpressionLinearInterpolate, 580, 1780)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            normal_output, normal_output_pin, filled_normal, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            filled_flat, "", filled_normal, "B")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            fill_alpha, "", filled_normal, "Alpha")
        normal_output = filled_normal
        normal_output_pin = ""

        seat_one = _expr(material, unreal.MaterialExpressionConstant, 220, 2020)
        seat_one.set_editor_property("r", 1.0)
        seat_weight = _expr(material, unreal.MaterialExpressionConstant, 220, 2100)
        seat_weight.set_editor_property("r", float(dust.get("occlusion", 0.8)))
        seated = _expr(
            material, unreal.MaterialExpressionLinearInterpolate, 400, 2020)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            seat_one, "", seated, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            occlusion, "", seated, "B")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            seat_weight, "", seated, "Alpha")
        seated_ao = _expr(material, unreal.MaterialExpressionMultiply, 580, 2020)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            ao_output, ao_output_pin, seated_ao, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            seated, "", seated_ao, "B")
        ao_output = seated_ao
        ao_output_pin = ""

    unreal.MaterialEditingLibrary.connect_material_property(
        base_output, base_output_pin, unreal.MaterialProperty.MP_BASE_COLOR
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        normal_output, normal_output_pin, unreal.MaterialProperty.MP_NORMAL
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        rough_output, rough_output_pin, unreal.MaterialProperty.MP_ROUGHNESS
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        ao_output, ao_output_pin, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION
    )


    specular = _expr(material, unreal.MaterialExpressionConstant, 0, 1120)
    specular.set_editor_property("r", spec.get("specular", 0.5))
    unreal.MaterialEditingLibrary.connect_material_property(
        specular, "", unreal.MaterialProperty.MP_SPECULAR
    )
    return {
        "normal": normal_output,
        "normal_pin": normal_output_pin,
        "roughness": rough_output,
        "roughness_pin": rough_output_pin,
        "ao": ao_output,
    }


def create_wet_asphalt(assets, tools, update_in_place=False):
    """Dew-wet alley asphalt: large-scale puddle mask flattens the normal and
    drops roughness to a mirror so Lumen reflects the signs and streetlights."""
    material = _material_for_layered_update(
        assets, tools, "M_AsphaltWorld", update_in_place
    )

    base_uv = _make_uv_source(material, "XY", 260.0, 0)
    diffuse = _sample(
        material, _load_texture("T_Asphalt_D"), base_uv,
        unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 0)
    normal = _sample(
        material, _load_texture("T_Asphalt_N"),
        _make_uv_source(material, "XY", 260.0, 380),
        unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 380)
    rough = _sample(
        material, _load_texture("T_Asphalt_R"),
        _make_uv_source(material, "XY", 260.0, 760),
        unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 760)

    # Puddle mask: the same roughness map read at street scale.
    mask = _sample(
        material, _load_texture("T_Asphalt_R"),
        _make_uv_source(material, "XY", 1150.0, 1140),
        unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, 1140)
    threshold = _expr(material, unreal.MaterialExpressionConstant, -1100, 1320)
    # Real asphalt roughness commonly sits well above 0.5. The previous 0.42
    # threshold therefore evaluated to zero across almost the whole scan and
    # the authored wet alley rendered dry. This higher threshold extracts the
    # broad darker basins, while a low dew floor keeps the rest merely damp.
    threshold.set_editor_property("r", 0.78)
    below = _expr(material, unreal.MaterialExpressionSubtract, -900, 1240)
    unreal.MaterialEditingLibrary.connect_material_expressions(threshold, "", below, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(mask, "R", below, "B")
    sharpen = _expr(material, unreal.MaterialExpressionConstant, -900, 1380)
    sharpen.set_editor_property("r", 4.8)
    scaled = _expr(material, unreal.MaterialExpressionMultiply, -740, 1260)
    unreal.MaterialEditingLibrary.connect_material_expressions(below, "", scaled, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(sharpen, "", scaled, "B")
    puddle_mask = _expr(material, unreal.MaterialExpressionSaturate, -600, 1260)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scaled, "", puddle_mask, ""
    )
    dew_floor = _expr(material, unreal.MaterialExpressionConstant, -600, 1430)
    dew_floor.set_editor_property("r", 0.10)
    puddle = _expr(material, unreal.MaterialExpressionMax, -420, 1320)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        puddle_mask, "", puddle, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        dew_floor, "", puddle, "B"
    )

    # Base color darkens where wet.
    dark_scale = _expr(material, unreal.MaterialExpressionLinearInterpolate, -420, 120)
    one = _expr(material, unreal.MaterialExpressionConstant, -600, 40)
    one.set_editor_property("r", 1.0)
    wet_dark = _expr(material, unreal.MaterialExpressionConstant, -600, 180)
    wet_dark.set_editor_property("r", 0.62)
    unreal.MaterialEditingLibrary.connect_material_expressions(one, "", dark_scale, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(wet_dark, "", dark_scale, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(puddle, "", dark_scale, "Alpha")
    tinted = _expr(material, unreal.MaterialExpressionMultiply, -240, 60)
    unreal.MaterialEditingLibrary.connect_material_expressions(diffuse, "RGB", tinted, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(dark_scale, "", tinted, "B")
    unreal.MaterialEditingLibrary.connect_material_property(
        tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Roughness collapses to a mirror inside puddles.
    mirror = _expr(material, unreal.MaterialExpressionConstant, -420, 820)
    mirror.set_editor_property("r", 0.075)
    rough_mix = _expr(material, unreal.MaterialExpressionLinearInterpolate, -240, 780)
    unreal.MaterialEditingLibrary.connect_material_expressions(rough, "R", rough_mix, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(mirror, "", rough_mix, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(puddle, "", rough_mix, "Alpha")
    unreal.MaterialEditingLibrary.connect_material_property(
        rough_mix, "", unreal.MaterialProperty.MP_ROUGHNESS)

    # Standing water lies flat: blend the normal toward straight up.
    flat = _expr(material, unreal.MaterialExpressionConstant3Vector, -420, 480)
    flat.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    normal_mix = _expr(material, unreal.MaterialExpressionLinearInterpolate, -240, 440)
    unreal.MaterialEditingLibrary.connect_material_expressions(normal, "RGB", normal_mix, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(flat, "", normal_mix, "B")
    flatten_scale = _expr(material, unreal.MaterialExpressionConstant, -420, 620)
    flatten_scale.set_editor_property("r", 0.82)
    flatten_alpha = _expr(
        material, unreal.MaterialExpressionMultiply, -240, 600
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        puddle, "", flatten_alpha, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        flatten_scale, "", flatten_alpha, "B"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        flatten_alpha, "", normal_mix, "Alpha"
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        normal_mix, "", unreal.MaterialProperty.MP_NORMAL)

    _connect_marked_scalar(
        material,
        0.58,
        WET_GROUND_RESPONSE_MARKER,
        unreal.MaterialProperty.MP_SPECULAR,
        -40,
        1500,
    )

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.log("[IndieGame] Created wet asphalt: M_AsphaltWorld")
    return material


def create_cctv_monitor_material(assets, tools):
    """§14 CCTV 채널 5 — the render-target monitor face.

    Three runtime parameters, and the C++ side sets all three by these exact
    names: `Feed` takes the scene-capture render target, `Static` crossfades to
    snow, `Gain` is the tube's brightness. The default `Feed` texture is engine
    black on purpose — if the binding ever fails the channel is simply dead,
    which is the one wrong state that cannot spoil the reveal.

    The look is not a picture pasted on a plane. It is a small analog tube in a
    dark booth: the feed is desaturated (a cheap IR sensor has no colour),
    tinted to phosphor, cut by 144 scanline pairs, crossed by the slow bright
    sync bar every analog monitor drifts, then vignetted the way a curved tube
    loses its corners. Emissive-only and unlit, so the screen is the light
    source and Lumen carries it onto Mok Hansu's desk.
    """
    material = _recreate_material(assets, tools, "M_CctvChannelFive")
    material.set_editor_property(
        "shading_model", unreal.MaterialShadingModel.MSM_UNLIT
    )
    material.set_editor_property("two_sided", False)

    uv = _expr(material, unreal.MaterialExpressionTextureCoordinate, -1800, 0)
    uv.set_editor_property("u_tiling", 1.0)
    uv.set_editor_property("v_tiling", 1.0)

    black = unreal.load_asset("/Engine/EngineResources/Black")
    if black is None:
        black = unreal.load_asset("/Engine/EngineResources/DefaultTexture")
    if black is None:
        raise RuntimeError("Could not load an engine default texture for Feed")
    feed = _expr(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -1550, 0
    )
    feed.set_editor_property("parameter_name", "Feed")
    feed.set_editor_property("texture", black)
    feed.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        uv, "", feed, "UVs"
    )

    # A 2.8 mm CCTV lens on an IR sensor has no colour at all, so the feed is
    # taken to luminance and given back only the tube's own phosphor cast.
    mono = _expr(material, unreal.MaterialExpressionDesaturation, -1300, 0)
    full = _expr(material, unreal.MaterialExpressionConstant, -1500, 180)
    full.set_editor_property("r", 1.0)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        feed, "RGB", mono, ""
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        full, "", mono, "Fraction"
    )
    phosphor = _expr(material, unreal.MaterialExpressionConstant3Vector, -1300, 200)
    phosphor.set_editor_property(
        "constant", unreal.LinearColor(0.74, 0.86, 0.93, 1.0)
    )
    tinted = _expr(material, unreal.MaterialExpressionMultiply, -1080, 60)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        mono, "", tinted, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        phosphor, "", tinted, "B"
    )

    # 288 visible lines on a CIF channel read as 144 dark/bright pairs.
    v_mask = _expr(material, unreal.MaterialExpressionComponentMask, -1550, 420)
    v_mask.set_editor_property("r", False)
    v_mask.set_editor_property("g", True)
    v_mask.set_editor_property("b", False)
    v_mask.set_editor_property("a", False)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", v_mask, "")

    line_count = _expr(material, unreal.MaterialExpressionConstant, -1550, 560)
    line_count.set_editor_property("r", 144.0)
    scan_phase = _expr(material, unreal.MaterialExpressionMultiply, -1380, 460)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        v_mask, "", scan_phase, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        line_count, "", scan_phase, "B"
    )
    scan_sine = _expr(material, unreal.MaterialExpressionSine, -1220, 460)
    scan_sine.set_editor_property("period", 1.0)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scan_phase, "", scan_sine, ""
    )
    scan_half = _expr(material, unreal.MaterialExpressionConstant, -1220, 600)
    scan_half.set_editor_property("r", 0.5)
    scan_scaled = _expr(material, unreal.MaterialExpressionMultiply, -1060, 480)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scan_sine, "", scan_scaled, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scan_half, "", scan_scaled, "B"
    )
    scan_norm = _expr(material, unreal.MaterialExpressionAdd, -900, 480)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scan_scaled, "", scan_norm, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scan_half, "", scan_norm, "B"
    )
    scan_floor = _expr(material, unreal.MaterialExpressionConstant, -900, 620)
    scan_floor.set_editor_property("r", 0.72)
    scan_ceil = _expr(material, unreal.MaterialExpressionConstant, -900, 700)
    scan_ceil.set_editor_property("r", 1.0)
    scan_gain = _expr(material, unreal.MaterialExpressionLinearInterpolate, -740, 520)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scan_floor, "", scan_gain, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scan_ceil, "", scan_gain, "B"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scan_norm, "", scan_gain, "Alpha"
    )

    # The slow sync bar. Nobody in the building ever adjusted the vertical hold.
    time_expr = _expr(material, unreal.MaterialExpressionTime, -1800, 900)
    roll_speed = _expr(material, unreal.MaterialExpressionConstant, -1800, 1020)
    roll_speed.set_editor_property("r", -0.11)
    roll_drift = _expr(material, unreal.MaterialExpressionMultiply, -1620, 940)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        time_expr, "", roll_drift, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        roll_speed, "", roll_drift, "B"
    )
    roll_sum = _expr(material, unreal.MaterialExpressionAdd, -1440, 900)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        v_mask, "", roll_sum, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        roll_drift, "", roll_sum, "B"
    )
    roll_wrap = _expr(material, unreal.MaterialExpressionFrac, -1280, 900)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        roll_sum, "", roll_wrap, ""
    )
    roll_sine = _expr(material, unreal.MaterialExpressionSine, -1120, 900)
    roll_sine.set_editor_property("period", 1.0)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        roll_wrap, "", roll_sine, ""
    )
    roll_lobe = _expr(material, unreal.MaterialExpressionSaturate, -960, 900)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        roll_sine, "", roll_lobe, ""
    )
    roll_tight = _expr(material, unreal.MaterialExpressionPower, -800, 900)
    roll_tight.set_editor_property("const_exponent", 6.0)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        roll_lobe, "", roll_tight, "Base"
    )
    bar_floor = _expr(material, unreal.MaterialExpressionConstant, -800, 1040)
    bar_floor.set_editor_property("r", 1.0)
    bar_peak = _expr(material, unreal.MaterialExpressionConstant, -800, 1120)
    bar_peak.set_editor_property("r", 1.10)
    bar_gain = _expr(material, unreal.MaterialExpressionLinearInterpolate, -640, 940)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        bar_floor, "", bar_gain, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        bar_peak, "", bar_gain, "B"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        roll_tight, "", bar_gain, "Alpha"
    )

    tube_gain = _expr(material, unreal.MaterialExpressionMultiply, -480, 700)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        scan_gain, "", tube_gain, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        bar_gain, "", tube_gain, "B"
    )
    picture = _expr(material, unreal.MaterialExpressionMultiply, -320, 300)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        tinted, "", picture, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        tube_gain, "", picture, "B"
    )

    # 지직임. Animated value noise, one level: this runs on a 34 cm plane and
    # the beat is seven seconds long, so it must not cost like a screen effect.
    snow_scale = _expr(material, unreal.MaterialExpressionConstant, -1800, 1300)
    snow_scale.set_editor_property("r", 320.0)
    snow_uv = _expr(material, unreal.MaterialExpressionMultiply, -1620, 1240)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        uv, "", snow_uv, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        snow_scale, "", snow_uv, "B"
    )
    snow_rate = _expr(material, unreal.MaterialExpressionConstant, -1800, 1420)
    snow_rate.set_editor_property("r", 37.0)
    snow_time = _expr(material, unreal.MaterialExpressionMultiply, -1620, 1400)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        time_expr, "", snow_time, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        snow_rate, "", snow_time, "B"
    )
    snow_pos = _expr(material, unreal.MaterialExpressionAppendVector, -1440, 1300)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        snow_uv, "", snow_pos, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        snow_time, "", snow_pos, "B"
    )
    snow_noise = _expr(material, unreal.MaterialExpressionNoise, -1240, 1300)
    snow_noise.set_editor_property(
        "noise_function", unreal.NoiseFunction.NOISEFUNCTION_VALUE_ALU
    )
    snow_noise.set_editor_property("scale", 1.0)
    snow_noise.set_editor_property("quality", 1)
    snow_noise.set_editor_property("levels", 1)
    snow_noise.set_editor_property("turbulence", False)
    snow_noise.set_editor_property("output_min", 0.02)
    snow_noise.set_editor_property("output_max", 1.0)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        snow_pos, "", snow_noise, "Position"
    )
    snow_tint = _expr(material, unreal.MaterialExpressionConstant3Vector, -1240, 1460)
    snow_tint.set_editor_property(
        "constant", unreal.LinearColor(0.86, 0.92, 1.0, 1.0)
    )
    snow = _expr(material, unreal.MaterialExpressionMultiply, -1020, 1340)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        snow_noise, "", snow, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        snow_tint, "", snow, "B"
    )

    static_amount = _expr(
        material, unreal.MaterialExpressionScalarParameter, -1020, 1500
    )
    static_amount.set_editor_property("parameter_name", "Static")
    static_amount.set_editor_property("default_value", 0.0)
    signal = _expr(material, unreal.MaterialExpressionLinearInterpolate, -160, 500)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        picture, "", signal, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(snow, "", signal, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(
        static_amount, "", signal, "Alpha"
    )

    # A curved tube loses its corners. This is also what keeps the monitor from
    # lighting the booth like a lightbox instead of a screen.
    centre = _expr(material, unreal.MaterialExpressionConstant2Vector, -1550, 1700)
    centre.set_editor_property("r", 0.5)
    centre.set_editor_property("g", 0.5)
    radius = _expr(material, unreal.MaterialExpressionDistance, -1360, 1660)
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", radius, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(
        centre, "", radius, "B"
    )
    vignette_slope = _expr(material, unreal.MaterialExpressionConstant, -1360, 1800)
    vignette_slope.set_editor_property("r", 0.92)
    vignette_fall = _expr(material, unreal.MaterialExpressionMultiply, -1180, 1700)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        radius, "", vignette_fall, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        vignette_slope, "", vignette_fall, "B"
    )
    vignette_raw = _expr(material, unreal.MaterialExpressionOneMinus, -1000, 1700)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        vignette_fall, "", vignette_raw, ""
    )
    vignette_clamped = _expr(material, unreal.MaterialExpressionSaturate, -840, 1700)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        vignette_raw, "", vignette_clamped, ""
    )
    vignette = _expr(material, unreal.MaterialExpressionPower, -680, 1700)
    vignette.set_editor_property("const_exponent", 1.6)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        vignette_clamped, "", vignette, "Base"
    )

    shaded = _expr(material, unreal.MaterialExpressionMultiply, -40, 900)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        signal, "", shaded, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        vignette, "", shaded, "B"
    )
    gain = _expr(material, unreal.MaterialExpressionScalarParameter, -40, 1080)
    gain.set_editor_property("parameter_name", "Gain")
    gain.set_editor_property("default_value", 1.0)
    emissive = _expr(material, unreal.MaterialExpressionMultiply, 120, 940)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        shaded, "", emissive, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        gain, "", emissive, "B"
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    )

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.log("[IndieGame] Created CCTV monitor material: M_CctvChannelFive")
    return material


def run():
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    if assets is None or tools is None:
        raise RuntimeError("Unreal editor asset services are unavailable")

    if os.environ.get("IG_SURFACE_RESPONSE_ONLY") == "1":
        surfaces = create_textured_materials(
            assets, tools, update_in_place=True
        )
        if len(surfaces) != len(TEXTURED_MATERIALS):
            raise RuntimeError(
                f"Surface material count mismatch: {len(surfaces)} / "
                f"{len(TEXTURED_MATERIALS)}"
            )
        # Only M_StainlessUV from this targeted set is used by an ISM.  The
        # full-build helper also loads and appends seventeen retail-label
        # materials, which must not inflate or dirty a surface-only migration.
        for surface in surfaces:
            if surface.get_name() in INSTANCED_PRODUCT_MATERIALS:
                surface.set_editor_property(
                    "used_with_instanced_static_meshes", True
                )
                unreal.MaterialEditingLibrary.recompile_material(surface)
        failed = []
        for surface in surfaces:
            if not assets.save_loaded_asset(surface, False):
                failed.append(surface.get_name())
        if failed:
            raise RuntimeError(
                "Could not save layered surface-response materials: "
                + ", ".join(failed)
            )
        unreal.log(
            f"[IndieGame] Surface response material update complete: "
            f"{len(surfaces)} materials"
        )
        return

    if os.environ.get("IG_ARRIVAL_PROLOGUE_ONLY") == "1":
        arrival = create_flat_texture_materials(
            assets,
            tools,
            {"M_ArrivalContract": DECAL_MATERIALS["M_ArrivalContract"]},
            False,
        )
        if len(arrival) != 1 or not assets.save_loaded_assets(arrival, False):
            raise RuntimeError("Could not save arrival-prologue materials")
        unreal.log("[IndieGame] Arrival prologue material update complete")
        return

    if os.environ.get("IG_RETAIL_REALISM_ONLY") == "1":
        names = (
            "M_VillaStucco_X",
            "M_VillaStucco_Y",
            "M_StainlessUV",
            "M_SteelDoorUV",
        )
        retail = create_textured_materials(
            assets,
            tools,
            {name: TEXTURED_MATERIALS[name] for name in names},
            update_in_place=True,
        )
        if len(retail) != len(names) or not assets.save_loaded_assets(
            retail, False
        ):
            raise RuntimeError("Could not save retail/alley realism materials")
        unreal.log(
            f"[IndieGame] Retail realism material update complete: "
            f"{len(retail)} materials"
        )
        return

    if os.environ.get("IG_PROP_RESPONSE_ONLY") == "1":
        print_names = (
            "M_PaperClean",
            "M_PaperOld",
            "M_LabelWater",
    "M_LabelWater1L",
    "M_LabelWater2L",
            "M_LabelGreenTea",
            "M_LabelBarley",
            "M_LabelSoda",
        )
        created = create_flat_texture_materials(
            assets,
            tools,
            {name: DECAL_MATERIALS[name] for name in print_names},
            False,
            update_in_place=True,
        )
        if len(created) != len(print_names):
            raise RuntimeError(
                f"Print response material count mismatch: {len(created)} / "
                f"{len(print_names)}"
            )
        created += create_optical_prop_materials(
            assets, tools, update_in_place=True
        )
        carrier_bag = create_carrier_bag_material(
            assets, tools, update_in_place=True
        )
        if carrier_bag is None:
            raise RuntimeError("Could not update M_CarrierBagFilm")
        created.append(carrier_bag)
        created.append(create_wet_asphalt(assets, tools, update_in_place=True))
        for material in created:
            name = material.get_name()
            if name in INSTANCED_PRODUCT_MATERIALS:
                material.set_editor_property(
                    "used_with_instanced_static_meshes", True
                )
            if name in WRAPPED_LABEL_MATERIALS:
                material.set_editor_property("two_sided", True)
            unreal.MaterialEditingLibrary.recompile_material(material)
        failed = []
        for material in created:
            if not assets.save_loaded_asset(material, False):
                failed.append(material.get_name())
        if failed:
            raise RuntimeError(
                "Could not save prop response materials: " + ", ".join(failed)
            )
        unreal.log(
            f"[IndieGame] Prop response material update complete: "
            f"{len(created)} materials"
        )
        return

    if os.environ.get("IG_CORRIDOR_SIGNAGE_ONLY") == "1":
        names = (
            "M_CaptureMercyNote",
            "M_MercyNoteUnderDoor",
            "M_SignAux5MonitorOnly",
            "M_Plate402",
        )
        signage = create_flat_texture_materials(
            assets,
            tools,
            {name: DECAL_MATERIALS[name] for name in names},
            False,
            update_in_place=True,
        )
        if len(signage) != len(names):
            raise RuntimeError("Could not build corridor entrance signage materials")
        # The channel-5 tube ships in the same pass as its AUX label: the screen
        # and the reason it is not recorded are one beat, and baking them apart
        # is how the two end up contradicting each other (§19.9 위험 8).
        signage.append(create_cctv_monitor_material(assets, tools))
        if not assets.save_loaded_assets(signage, False):
            raise RuntimeError("Could not save corridor entrance signage materials")
        unreal.log("[IndieGame] Corridor entrance signage material update complete")
        return
    if os.environ.get("IG_MISSING_FLOOR_ONLY") == "1":
        world_names = (
            "M_MissingFloorPlaster_X",
            "M_MissingFloorPlaster_Y",
            "M_MissingFloorPlaster_XY",
            # 발소리 표면과 현관문 문짝. 표면은 한때 복도 콘크리트로,
            # 문짝은 브러시드 스테인리스로 그려지고 있었다.
            "M_MissingFloorSteelStair",
            "M_RooftopWaterproofing_XY",
            "M_UnitDoorPaintedSteel",
        )
        residue_names = (
            "M_MissingFloorHandprints",
            "M_MissingFloorDragTrails",
            "M_MissingFloorDustJoint",
            "M_MissingFloorCavityScratches",
        )
        sprite_names = (
            "M_SpriteSeo",
            "M_SpriteMok",
            "M_SpriteListenerFront",
            "M_SpriteListenerCrawl0",
            "M_SpriteListenerCrawl1",
            "M_SpriteListenerCrawl2",
            "M_SpriteListenerCrawl3",
            "M_SpriteFinalCavity",
            "M_SpriteMokFinalUpper",
        )
        missing_floor = create_textured_materials(
            assets,
            tools,
            {name: TEXTURED_MATERIALS[name] for name in world_names},
        )
        missing_floor += create_flat_texture_materials(
            assets,
            tools,
            {
                name: DECAL_MATERIALS[name]
                for name in (
                    "M_MissingFloorListenerPlasterUV",
                )
            },
            False,
        )
        missing_floor += create_masked_texture_materials(
            assets,
            tools,
            {name: EVIDENCE_MASK_MATERIALS[name] for name in residue_names},
            True,
        )
        missing_floor += create_masked_texture_materials(
            assets,
            tools,
            {name: SURFACE_OVERLAY_MATERIALS[name] for name in sprite_names},
            False,
        )
        # 발자국과 끌림 자국은 UIGSettledDustComponent의 인스턴스 평면으로
        # 그린다. 생성 재질은 나중에 ISM에서 사용해도 필요한 셰이더 순열이
        # 자동으로 저장되지 않는다. 이 플래그가 없으면 실행 첫 프레임에 기본
        # 재질로 대체되고 경고가 발생하므로 실제 배칭 대상 두 재질을 여기서
        # 다시 컴파일하고 저장한다.
        for material in missing_floor:
            if material.get_name() in {
                "M_MissingFloorHandprints",
                "M_MissingFloorDragTrails",
            }:
                material.set_editor_property(
                    "used_with_instanced_static_meshes", True
                )
                unreal.MaterialEditingLibrary.recompile_material(material)
        # 벽 6(석고 셋·발소리 표면 둘·현관문 강판), 위층 사람 1, 흔적 4, 인물 카드 9.
        if len(missing_floor) != 20 or not assets.save_loaded_assets(
            missing_floor, False
        ):
            raise RuntimeError("Could not save missing-floor visual materials")
        unreal.log("[IndieGame] Missing-floor visual material update complete")
        return
    if os.environ.get("IG_APARTMENT_VISUAL_ONLY") == "1":
        apartment_material_names = (
            "M_Wallpaper_X",
            "M_Wallpaper_Y",
            "M_WallpaperCeil",
        )
        apartment_materials = create_textured_materials(
            assets,
            tools,
            {
                name: TEXTURED_MATERIALS[name]
                for name in apartment_material_names
            },
        )
        apartment_materials += create_masked_texture_materials(
            assets,
            tools,
            {
                "M_ApartmentWallPatina": EVIDENCE_MASK_MATERIALS[
                    "M_ApartmentWallPatina"
                ]
            },
            True,
        )
        apartment_materials.append(create_corridor_scuff_material(assets, tools))
        if len(apartment_materials) != 5 or not assets.save_loaded_assets(
            apartment_materials, False
        ):
            raise RuntimeError("Could not save apartment visual materials")
        unreal.log("[IndieGame] Apartment visual material update complete")
        return
    if os.environ.get("IG_RETAIL_SIGNS_ONLY") == "1":
        signs = create_flat_texture_materials(
            assets, tools, SIGN_MATERIALS, True, update_in_place=True
        )
        if not assets.save_loaded_assets(signs, False):
            raise RuntimeError("Could not save retail sign materials")
        unreal.log("[IndieGame] Retail sign emissive polish complete")
        return

    created = []
    created += create_textured_materials(assets, tools)
    created += create_flat_texture_materials(assets, tools, DECAL_MATERIALS, False)
    created += create_flat_texture_materials(assets, tools, SIGN_MATERIALS, True)
    created += create_masked_texture_materials(
        assets, tools, EVIDENCE_MASK_MATERIALS, True
    )
    created += create_masked_texture_materials(
        assets, tools, SURFACE_OVERLAY_MATERIALS, False
    )
    created.append(create_corridor_scuff_material(assets, tools))
    carrier_bag = create_carrier_bag_material(assets, tools)
    if carrier_bag is not None:
        created.append(carrier_bag)
    construction_film = create_carrier_bag_material(assets, tools, construction=True)
    if construction_film is not None:
        created.append(construction_film)
    created.append(create_wet_asphalt(assets, tools))
    created += create_optical_prop_materials(
        assets, tools, update_in_place=True
    )
    enable_instanced_product_usage(created)

    if not assets.save_loaded_assets(created, False):
        raise RuntimeError("Could not save textured materials")
    unreal.log(f"[IndieGame] Textured material pass complete: {len(created)} materials")


if __name__ == "__main__":
    run()
