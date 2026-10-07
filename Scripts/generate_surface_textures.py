"""Generate the tileable surface textures used by the prologue, then import them.

Runs inside UnrealEditor-Cmd via -ExecutePythonScript. Everything is produced
procedurally (pure Python PNG writer + value noise), so the project stays free
of external assets. Korean signage bitmaps are produced separately by
Scripts/Create-SignTextures.ps1 into Content/SourceArt and imported here too.
"""

import math
import os
import struct
import zlib

import unreal


PROJECT_DIR = unreal.SystemLibrary.get_project_directory()
SOURCE_ART_DIR = os.path.join(PROJECT_DIR, "Content", "SourceArt")
TEXTURE_PACKAGE_ROOT = "/Game/Prototype/Textures"
FRONTEND_TEXTURE_PACKAGE_ROOT = "/Game/UI/Textures"
HUD_UI_ONLY = os.environ.get("IG_HUD_UI_ONLY") == "1"
HUD_UI_TEXTURE_NAMES = {
    "T_AudioCalibrationWall_D",
    "T_HudDialogueFilm_D",
    "T_MissingFloorJournalPaper_D",
    "T_FPHandKnock0_D",
    "T_FPHandKnock1_D",
    "T_FPHandKnock2_D",
    "T_FPHandKnock3_D",
    "T_TitleBackground_D",
    "T_EpilogueWorkshop_D",
    "T_EpilogueAutumn_D",
    "T_EpilogueServiceBay_D",
}
FRONTEND_UI_ONLY = os.environ.get("IG_FRONTEND_UI_ONLY") == "1"
FRONTEND_UI_TEXTURE_NAMES = {
    "T_TitleBackground_D",
    "T_EpilogueWorkshop_D",
    "T_EpilogueAutumn_D",
    "T_EpilogueServiceBay_D",
}
APARTMENT_VISUAL_ONLY = os.environ.get("IG_APARTMENT_VISUAL_ONLY") == "1"
APARTMENT_VISUAL_TEXTURE_NAMES = {
    "T_ApartmentWallpaperV2_D",
    "T_ApartmentWallpaperV2_N",
    "T_ApartmentWallpaperV2_R",
    "T_ApartmentWallpaperV2_A",
    "T_ApartmentWallPatina_M",
}
MISSING_FLOOR_ONLY = os.environ.get("IG_MISSING_FLOOR_ONLY") == "1"
MISSING_FLOOR_TEXTURE_NAMES = {
    "T_MissingFloorDryPlaster_D",
    "T_MissingFloorDryPlaster_N",
    "T_MissingFloorDryPlaster_R",
    "T_MissingFloorDryPlaster_A",
    "T_MissingFloorHandprints_M",
    "T_MissingFloorDragTrails_M",
    "T_MissingFloorDustJoint_M",
    "T_MissingFloorCavityScratches_M",
    "T_SpriteSeo_D",
    "T_SpriteMok_D",
    "T_SpriteListenerFront_D",
    "T_SpriteListenerFront_N",
    "T_SpriteListenerFront_R",
    "T_SpriteListenerFront_A",
    "T_SpriteListenerCrawl0_D",
    "T_SpriteListenerCrawl0_N",
    "T_SpriteListenerCrawl0_R",
    "T_SpriteListenerCrawl0_A",
    "T_SpriteListenerCrawl1_D",
    "T_SpriteListenerCrawl1_N",
    "T_SpriteListenerCrawl1_R",
    "T_SpriteListenerCrawl1_A",
    "T_SpriteListenerCrawl2_D",
    "T_SpriteListenerCrawl2_N",
    "T_SpriteListenerCrawl2_R",
    "T_SpriteListenerCrawl2_A",
    "T_SpriteListenerCrawl3_D",
    "T_SpriteListenerCrawl3_N",
    "T_SpriteListenerCrawl3_R",
    "T_SpriteListenerCrawl3_A",
    "T_SpriteFinalCavity_D",
    "T_SpriteFinalCavity_N",
    "T_SpriteFinalCavity_R",
    "T_SpriteFinalCavity_A",
    "T_SpriteMokFinalUpper_D",
    "T_SpriteMokFinalUpper_N",
    "T_SpriteMokFinalUpper_R",
    "T_SpriteMokFinalUpper_A",
    "T_MissingFloorSteelStair_D",
    "T_MissingFloorSteelStair_N",
    "T_MissingFloorSteelStair_R",
    "T_MissingFloorSteelStair_A",
    "T_RooftopWaterproofing_D",
    "T_RooftopWaterproofing_N",
    "T_RooftopWaterproofing_R",
    "T_RooftopWaterproofing_A",
    "T_UnitDoorPaintedSteel_D",
    "T_UnitDoorPaintedSteel_N",
    "T_UnitDoorPaintedSteel_R",
    "T_UnitDoorPaintedSteel_A",
}
CORRIDOR_SIGNAGE_ONLY = os.environ.get("IG_CORRIDOR_SIGNAGE_ONLY") == "1"
CORRIDOR_SIGNAGE_TEXTURE_NAMES = {
    "T_CaptureMercyNote_D",
    "T_MercyNoteUnderDoor_D",
    "T_Plate401_D",
    "T_Plate402_D",
    "T_Plate403_D",
    "T_SignAux5MonitorOnly_D",
}
PROP_RESPONSE_ONLY = os.environ.get("IG_PROP_RESPONSE_ONLY") == "1"
PROP_RESPONSE_TEXTURE_NAMES = {
    "T_PaperClean_V2_N",
    "T_PaperClean_V2_R",
    "T_PaperClean_V2_A",
}
RETAIL_REALISM_ONLY = os.environ.get("IG_RETAIL_REALISM_ONLY") == "1"
RETAIL_REALISM_TEXTURE_NAMES = {
    "T_KoreanVillaStucco_D",
    "T_KoreanVillaStucco_N",
    "T_KoreanVillaStucco_R",
    "T_KoreanVillaStucco_A",
}
ARRIVAL_PROLOGUE_ONLY = os.environ.get("IG_ARRIVAL_PROLOGUE_ONLY") == "1"
ARRIVAL_PROLOGUE_TEXTURE_NAMES = {
    "T_ArrivalContract_D",
    "T_MovingBoxCardboard_D",
    "T_MovingBoxCardboard_N",
    "T_MovingBoxCardboard_R",
    "T_MovingBoxCardboard_A",
}


# ---------------------------------------------------------------------------
# Minimal PNG writer (RGB8)
# ---------------------------------------------------------------------------

def write_png(path, width, height, pixels):
    """pixels: list of rows; each row is a bytearray of length width*3."""

    def chunk(tag, data):
        payload = tag + data
        return (
            struct.pack(">I", len(data))
            + payload
            + struct.pack(">I", zlib.crc32(payload) & 0xFFFFFFFF)
        )

    raw = b"".join(b"\x00" + bytes(row) for row in pixels)
    blob = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )
    with open(path, "wb") as handle:
        handle.write(blob)


# ---------------------------------------------------------------------------
# Tileable value noise
# ---------------------------------------------------------------------------

def _hash01(x, y, seed):
    n = (x * 374761393 + y * 668265263 + seed * 982451653) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def value_noise(fx, fy, period, seed):
    x0 = int(math.floor(fx))
    y0 = int(math.floor(fy))
    tx = fx - x0
    ty = fy - y0
    sx = tx * tx * (3.0 - 2.0 * tx)
    sy = ty * ty * (3.0 - 2.0 * ty)
    xa = x0 % period
    xb = (x0 + 1) % period
    ya = y0 % period
    yb = (y0 + 1) % period
    a = _hash01(xa, ya, seed)
    b = _hash01(xb, ya, seed)
    c = _hash01(xa, yb, seed)
    d = _hash01(xb, yb, seed)
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy


def fbm(fx, fy, period, seed, octaves=4):
    total = 0.0
    weight = 0.0
    amplitude = 1.0
    frequency = 1
    for octave in range(octaves):
        total += amplitude * value_noise(
            fx * frequency, fy * frequency, period * frequency, seed + octave * 101
        )
        weight += amplitude
        amplitude *= 0.5
        frequency *= 2
    return total / weight


def clamp01(value):
    return 0.0 if value < 0.0 else (1.0 if value > 1.0 else value)


# ---------------------------------------------------------------------------
# Texture builders: each returns (color_rows, height_field, rough_rows|None)
# ---------------------------------------------------------------------------

SIZE = 256


def _rows():
    return [bytearray(SIZE * 3) for _ in range(SIZE)]


def _gray_rows():
    return [bytearray(SIZE * 3) for _ in range(SIZE)]


def _put(rows, x, y, r, g, b):
    row = rows[y]
    i = x * 3
    row[i] = int(clamp01(r) * 255)
    row[i + 1] = int(clamp01(g) * 255)
    row[i + 2] = int(clamp01(b) * 255)


def build_jangpan():
    """Warm yellowish vinyl sheet flooring with faint plank lines."""
    color = _rows()
    height = [[0.0] * SIZE for _ in range(SIZE)]
    rough = _gray_rows()
    for y in range(SIZE):
        for x in range(SIZE):
            u = x / SIZE
            v = y / SIZE
            grain = fbm(u * 9.0, v * 60.0, 9, 11, 3)
            blotch = fbm(u * 4.0, v * 4.0, 4, 12, 3)
            plank = v * 4.0 % 1.0
            seam = 1.0 if (plank < 0.02 or plank > 0.98) else 0.0
            base = 0.62 + grain * 0.10 + blotch * 0.06 - seam * 0.10
            r = base * 0.86
            g = base * 0.70
            b = base * 0.42
            _put(color, x, y, r, g, b)
            height[y][x] = base - seam * 0.5
            gloss = 0.42 + blotch * 0.18 + seam * 0.2
            _put(rough, x, y, gloss, gloss, gloss)
    return color, height, rough


def build_wallpaper():
    """Pale weave wallpaper with low-frequency damp staining."""
    color = _rows()
    height = [[0.0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            u = x / SIZE
            v = y / SIZE
            weave = (
                0.5
                + 0.24 * math.sin(u * math.pi * 2 * 64)
                + 0.10 * math.sin(v * math.pi * 2 * 96)
            )
            stain = fbm(u * 3.0, v * 3.0, 3, 21, 4)
            damp = clamp01((stain - 0.62) * 2.4)
            base = 0.46 + weave * 0.05 - damp * 0.20
            r = base * 0.92
            g = base * 0.97
            b = base * 0.90
            _put(color, x, y, r, g, b)
            height[y][x] = weave * 0.3 + stain * 0.2
    return color, height, None


def build_concrete():
    color = _rows()
    height = [[0.0] * SIZE for _ in range(SIZE)]
    rough = _gray_rows()
    for y in range(SIZE):
        for x in range(SIZE):
            u = x / SIZE
            v = y / SIZE
            blotch = fbm(u * 7.0, v * 7.0, 7, 31, 5)
            speck = value_noise(u * 96, v * 96, 96, 32)
            base = 0.44 + blotch * 0.07 + (speck - 0.5) * 0.06
            _put(color, x, y, base, base * 0.99, base * 0.96)
            height[y][x] = blotch * 0.6 + speck * 0.2
            _put(rough, x, y, 0.88, 0.88, 0.88)
    return color, height, rough


def build_asphalt():
    color = _rows()
    height = [[0.0] * SIZE for _ in range(SIZE)]
    rough = _gray_rows()
    cracks = [[0.0] * SIZE for _ in range(SIZE)]
    # A few random-walk cracks etched into the surface.
    for crack in range(5):
        cx = (crack * 53) % SIZE
        cy = (crack * 97) % SIZE
        heading = crack * 1.3
        for step in range(300):
            heading += (_hash01(step, crack, 77) - 0.5) * 0.9
            cx = (cx + math.cos(heading)) % SIZE
            cy = (cy + math.sin(heading) * 0.7) % SIZE
            cracks[int(cy)][int(cx)] = 1.0
    for y in range(SIZE):
        for x in range(SIZE):
            u = x / SIZE
            v = y / SIZE
            grit = value_noise(u * 128, v * 128, 128, 41)
            patch = fbm(u * 6.0, v * 6.0, 6, 42, 4)
            crack = max(
                cracks[y][x],
                cracks[y][(x + 1) % SIZE] * 0.6,
                cracks[(y + 1) % SIZE][x] * 0.6,
            )
            base = 0.10 + grit * 0.07 + patch * 0.025 - crack * 0.06
            _put(color, x, y, base, base, base * 1.06)
            height[y][x] = grit * 0.5 + patch * 0.4 - crack * 1.2
            _put(rough, x, y, 0.94, 0.94, 0.94)
    return color, height, rough


def build_store_tile():
    color = _rows()
    height = [[0.0] * SIZE for _ in range(SIZE)]
    rough = _gray_rows()
    for y in range(SIZE):
        for x in range(SIZE):
            u = x / SIZE
            v = y / SIZE
            fx = (u * 2.0) % 1.0
            fy = (v * 2.0) % 1.0
            grout = 1.0 if (fx < 0.025 or fy < 0.025) else 0.0
            mottle = fbm(u * 6.0, v * 6.0, 6, 61, 3)
            base = 0.62 + mottle * 0.08 - grout * 0.28
            _put(color, x, y, base, base * 0.99, base * 0.95)
            height[y][x] = (1.0 - grout) + mottle * 0.1
            gloss = 0.16 + mottle * 0.12 + grout * 0.5
            _put(rough, x, y, gloss, gloss, gloss)
    return color, height, rough


def build_ceiling_tile():
    color = _rows()
    height = [[0.0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            u = x / SIZE
            v = y / SIZE
            fx = (u * 2.0) % 1.0
            fy = (v * 2.0) % 1.0
            seam = 1.0 if (fx < 0.02 or fy < 0.02) else 0.0
            holes = value_noise(u * 64, v * 64, 64, 71)
            pin = 1.0 if holes > 0.90 else 0.0
            base = 0.60 - seam * 0.2 - pin * 0.05
            _put(color, x, y, base, base, base * 0.97)
            height[y][x] = 1.0 - seam - pin * 0.15
    return color, height, None


def build_wood_dark():
    color = _rows()
    height = [[0.0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            u = x / SIZE
            v = y / SIZE
            grain = fbm(u * 3.0, v * 24.0, 3, 81, 4)
            ring = 0.5 + 0.5 * math.sin((u * 4.0 + grain * 2.2) * math.pi * 2)
            base = 0.16 + grain * 0.10 + ring * 0.05
            _put(color, x, y, base * 1.25, base * 0.62, base * 0.34)
            height[y][x] = grain + ring * 0.3
    return color, height, None


def build_metal_brushed():
    color = _rows()
    height = [[0.0] * SIZE for _ in range(SIZE)]
    rough = _gray_rows()
    for y in range(SIZE):
        for x in range(SIZE):
            u = x / SIZE
            v = y / SIZE
            streak = fbm(u * 2.0, v * 90.0, 2, 91, 3)
            base = 0.40 + streak * 0.14
            _put(color, x, y, base, base * 1.02, base * 1.06)
            height[y][x] = streak
            gloss = 0.28 + streak * 0.16
            _put(rough, x, y, gloss, gloss, gloss)
    return color, height, rough


def build_blanket():
    """Quilted bedding cloth."""
    color = _rows()
    height = [[0.0] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            u = x / SIZE
            v = y / SIZE
            weave = 0.5 + 0.16 * math.sin(u * math.pi * 2 * 110) * math.sin(
                v * math.pi * 2 * 110
            )
            quilt_x = abs(((u * 4.0) % 1.0) - 0.5)
            quilt_y = abs(((v * 4.0) % 1.0) - 0.5)
            seam = 1.0 if (quilt_x > 0.47 or quilt_y > 0.47) else 0.0
            puff = (0.5 - quilt_x) * (0.5 - quilt_y) * 4.0
            wrinkle = fbm(u * 5.0, v * 5.0, 5, 96, 3)
            base = 0.30 + weave * 0.06 + wrinkle * 0.08 - seam * 0.10
            _put(color, x, y, base * 0.75, base * 0.85, base * 1.05)
            height[y][x] = puff + wrinkle * 0.4 - seam * 0.8
    return color, height, None


# ---------------------------------------------------------------------------
# Normal map from a height field
# ---------------------------------------------------------------------------

def normal_rows_from_height(height, strength=2.2):
    rows = _gray_rows()
    for y in range(SIZE):
        for x in range(SIZE):
            left = height[y][(x - 1) % SIZE]
            right = height[y][(x + 1) % SIZE]
            up = height[(y - 1) % SIZE][x]
            down = height[(y + 1) % SIZE][x]
            dx = (left - right) * strength
            dy = (up - down) * strength
            length = math.sqrt(dx * dx + dy * dy + 1.0)
            nx = dx / length
            ny = dy / length
            nz = 1.0 / length
            _put(rows, x, y, nx * 0.5 + 0.5, ny * 0.5 + 0.5, nz * 0.5 + 0.5)
    return rows


# ---------------------------------------------------------------------------
# Generation + import
# ---------------------------------------------------------------------------

def build_shutter():
    """Rolled-steel shop shutter: horizontal slats with grime streaks."""
    color = _rows()
    height = [[0.0] * SIZE for _ in range(SIZE)]
    rough = _gray_rows()
    for y in range(SIZE):
        for x in range(SIZE):
            u = x / SIZE
            v = y / SIZE
            slat = (v * 18.0) % 1.0
            groove = 1.0 if slat < 0.16 else 0.0
            streak = fbm(u * 2.0, v * 30.0, 2, 87, 3)
            grime = fbm(u * 4.0, v * 4.0, 4, 88, 3)
            base = 0.34 + streak * 0.08 - groove * 0.16 - grime * 0.10
            _put(color, x, y, base * 0.95, base * 0.97, base)
            height[y][x] = (1.0 - groove) + streak * 0.15
            gloss = 0.45 + grime * 0.25
            _put(rough, x, y, gloss, gloss, gloss)
    return color, height, rough


SURFACES = {
    "Shutter": build_shutter,
    "Jangpan": build_jangpan,
    "Wallpaper": build_wallpaper,
    "Concrete": build_concrete,
    "Asphalt": build_asphalt,
    "StoreTile": build_store_tile,
    "CeilingTile": build_ceiling_tile,
    "WoodDark": build_wood_dark,
    "MetalBrushed": build_metal_brushed,
    "Blanket": build_blanket,
}


def generate_surface_pngs():
    os.makedirs(SOURCE_ART_DIR, exist_ok=True)
    generated = []
    for name, builder in SURFACES.items():
        unreal.log(f"[IndieGame] Generating texture set: {name}")
        color, height, rough = builder()
        diffuse_path = os.path.join(SOURCE_ART_DIR, f"T_{name}_D.png")
        write_png(diffuse_path, SIZE, SIZE, color)
        generated.append(diffuse_path)
        normal_path = os.path.join(SOURCE_ART_DIR, f"T_{name}_N.png")
        write_png(normal_path, SIZE, SIZE, normal_rows_from_height(height))
        generated.append(normal_path)
        if rough is not None:
            rough_path = os.path.join(SOURCE_ART_DIR, f"T_{name}_R.png")
            write_png(rough_path, SIZE, SIZE, rough)
            generated.append(rough_path)
    return generated


def import_textures():
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset_subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if asset_tools is None or asset_subsystem is None:
        raise RuntimeError("Editor asset services are unavailable")

    png_files = sorted(
        os.path.join(SOURCE_ART_DIR, entry)
        for entry in os.listdir(SOURCE_ART_DIR)
        if entry.lower().endswith(".png")
        and (
            (
                not HUD_UI_ONLY
                and not FRONTEND_UI_ONLY
                and not APARTMENT_VISUAL_ONLY
                and not MISSING_FLOOR_ONLY
                and not CORRIDOR_SIGNAGE_ONLY
                and not PROP_RESPONSE_ONLY
                and not RETAIL_REALISM_ONLY
                and not ARRIVAL_PROLOGUE_ONLY
            )
            or (
                HUD_UI_ONLY
                and os.path.splitext(entry)[0] in HUD_UI_TEXTURE_NAMES
            )
            or (
                FRONTEND_UI_ONLY
                and os.path.splitext(entry)[0] in FRONTEND_UI_TEXTURE_NAMES
            )
            or (
                APARTMENT_VISUAL_ONLY
                and os.path.splitext(entry)[0] in APARTMENT_VISUAL_TEXTURE_NAMES
            )
            or (
                MISSING_FLOOR_ONLY
                and os.path.splitext(entry)[0] in MISSING_FLOOR_TEXTURE_NAMES
            )
            or (
                CORRIDOR_SIGNAGE_ONLY
                and os.path.splitext(entry)[0] in CORRIDOR_SIGNAGE_TEXTURE_NAMES
            )
            or (
                PROP_RESPONSE_ONLY
                and os.path.splitext(entry)[0] in PROP_RESPONSE_TEXTURE_NAMES
            )
            or (
                RETAIL_REALISM_ONLY
                and os.path.splitext(entry)[0] in RETAIL_REALISM_TEXTURE_NAMES
            )
            or (
                ARRIVAL_PROLOGUE_ONLY
                and os.path.splitext(entry)[0] in ARRIVAL_PROLOGUE_TEXTURE_NAMES
            )
        )
    )
    if not png_files:
        raise RuntimeError(f"No PNG files found in {SOURCE_ART_DIR}")

    tasks = []
    for png_file in png_files:
        asset_name = os.path.splitext(os.path.basename(png_file))[0]
        task = unreal.AssetImportTask()
        task.filename = png_file
        task.destination_path = (
            FRONTEND_TEXTURE_PACKAGE_ROOT
            if asset_name in FRONTEND_UI_TEXTURE_NAMES
            else TEXTURE_PACKAGE_ROOT
        )
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    asset_tools.import_asset_tasks(tasks)

    imported = []
    for png_file in png_files:
        asset_name = os.path.splitext(os.path.basename(png_file))[0]
        package_root = (
            FRONTEND_TEXTURE_PACKAGE_ROOT
            if asset_name in FRONTEND_UI_TEXTURE_NAMES
            else TEXTURE_PACKAGE_ROOT
        )
        asset_path = f"{package_root}/{asset_name}"
        texture = unreal.load_asset(asset_path)
        if texture is None:
            raise RuntimeError(f"Import failed for {asset_path}")

        if asset_name.endswith("_N"):
            texture.set_editor_property(
                "compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP
            )
            texture.set_editor_property("srgb", False)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
        elif asset_name.endswith(("_R", "_A", "_W")):
            texture.set_editor_property("srgb", False)
            texture.set_editor_property(
                "compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE
            )
        elif asset_name.endswith("_M"):
            texture.set_editor_property("srgb", False)
            texture.set_editor_property(
                "compression_settings", unreal.TextureCompressionSettings.TC_MASKS
            )
        elif asset_name in HUD_UI_TEXTURE_NAMES:
            # This texture is sampled in screen space. World streaming and
            # generated mips make its fine grain shimmer at changing UI scales.
            texture.set_editor_property(
                "compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON
            )
            texture.set_editor_property(
                "lod_group", unreal.TextureGroup.TEXTUREGROUP_UI
            )
            texture.set_editor_property(
                "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS
            )
            texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
            texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
            texture.set_editor_property("never_stream", True)
        elif (
            asset_name in {
                "T_ArrivalContract_D",
                "T_NoteFridge_D",
                "T_CaptureMercyNote_D",
                "T_MercyNoteUnderDoor_D",
                "T_SignAux5MonitorOnly_D",
            }
            or asset_name.startswith("T_Label")
            or asset_name.startswith("T_Plate")
        ):
            # Printed film, entrance plates and the 76 mm memo are inspected
            # at oblique angles.
            # Clamp the authored 0/1 borders; the World texture group's default
            # sampler retains project-wide anisotropy without overriding it per asset.
            texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
            texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
            texture.set_editor_property("filter", unreal.TextureFilter.TF_DEFAULT)
            texture.set_editor_property(
                "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SHARPEN2
            )
        elif asset_name.startswith("T_Sprite"):
            texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
            texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
            texture.set_editor_property(
                "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SHARPEN1
            )
        imported.append(texture)

    if not asset_subsystem.save_loaded_assets(imported, False):
        raise RuntimeError("Could not save imported textures")
    unreal.log(f"[IndieGame] Imported {len(imported)} textures")


if __name__ == "__main__":
    if (
        not HUD_UI_ONLY
        and not FRONTEND_UI_ONLY
        and not APARTMENT_VISUAL_ONLY
        and not MISSING_FLOOR_ONLY
        and not CORRIDOR_SIGNAGE_ONLY
        and not PROP_RESPONSE_ONLY
        and not RETAIL_REALISM_ONLY
        and not ARRIVAL_PROLOGUE_ONLY
    ):
        generate_surface_pngs()
    import_textures()
