"""관리실 안쪽 방과 「302호에 남은 짐」의 소품(2026-10-13).

목한수가 자는 간이침대, 서류철이 든 회색 철제 보관함, 합판 작업대, 필로티에 두는 평대차
손수레, 수거를 기다리는 흰 PP 마대와 끈 묶은 판지 상자, 벽에서 떼어 낸 판재 더미, 줄에
매달린 백열등. 형태는 Content/SourceArt/AI의 같은 날 시안(FoldingCot·SteelCabinet·HandCart·
SacksBoxes·LiningInSitu)을 보고 잡았고, 겉면은 같은 날 뽑아 손본 타일(T_CotCanvas·
T_BlanketFloral·T_CabinetPaintGrey·T_WorkbenchPlywood·T_WovenSack·T_LiningMembrane)과 기존
판지·석고 타일을 반복해 굽는다.

    blender -b --factory-startup --python Scripts/blender/build_inner_room.py
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import bpy  # noqa: E402
import bmesh  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import ig_blender_lib as ig  # noqa: E402

SOURCE_ART = os.path.abspath(os.path.join(HERE, "..", "..", "Content", "SourceArt"))
OUT = ig.out_root_from_argv()


def tiled(name, stem, tile_m, metallic=0.0, normal_strength=1.0, tint=None):
    """저장소 타일(D·N·R)을 ImageUV로 반복해 읽는다. _N은 DirectX라 G를 뒤집는다."""
    mat = bpy.data.materials.new(name)
    bsdf = ig._principled(mat)
    tree, nodes, links = ig._nodes(mat)
    uv = nodes.new("ShaderNodeUVMap")
    uv.uv_map = "ImageUV"
    mapping = nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (1.0 / tile_m, 1.0 / tile_m, 1.0)
    links.new(uv.outputs["UV"], mapping.inputs["Vector"])

    def image(suffix, colorspace):
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(os.path.join(SOURCE_ART, f"{stem}_{suffix}.png"))
        tex.image.colorspace_settings.name = colorspace
        tex.extension = "REPEAT"
        links.new(mapping.outputs["Vector"], tex.inputs["Vector"])
        return tex

    base = image("D", "sRGB")
    if tint is not None:
        mix = nodes.new("ShaderNodeMixRGB")
        mix.blend_type = "MULTIPLY"
        mix.inputs["Fac"].default_value = 1.0
        links.new(base.outputs["Color"], mix.inputs["Color1"])
        mix.inputs["Color2"].default_value = (*tint, 1.0)
        links.new(mix.outputs["Color"], bsdf.inputs["Base Color"])
    else:
        links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
    rough = image("R", "Non-Color")
    links.new(rough.outputs["Color"], bsdf.inputs["Roughness"])
    normal = image("N", "Non-Color")
    sep = nodes.new("ShaderNodeSeparateColor")
    inv = nodes.new("ShaderNodeMath")
    inv.operation = "SUBTRACT"
    inv.inputs[0].default_value = 1.0
    comb = nodes.new("ShaderNodeCombineColor")
    links.new(normal.outputs["Color"], sep.inputs["Color"])
    links.new(sep.outputs["Red"], comb.inputs["Red"])
    links.new(sep.outputs["Green"], inv.inputs[1])
    links.new(inv.outputs["Value"], comb.inputs["Green"])
    links.new(sep.outputs["Blue"], comb.inputs["Blue"])
    nmap = nodes.new("ShaderNodeNormalMap")
    nmap.inputs["Strength"].default_value = normal_strength
    links.new(comb.outputs["Color"], nmap.inputs["Color"])
    links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    bsdf.inputs["Metallic"].default_value = metallic
    mat["ig_kind"] = "baked"
    return mat


def flat(name, color, roughness, metallic=0.0):
    mat = bpy.data.materials.new(name)
    bsdf = ig._principled(mat)
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    mat["ig_kind"] = "baked"
    return mat


def tile_uv(ob):
    """오브젝트 변환을 굽고, 면이 향한 축으로 월드 좌표를 투영한 ImageUV를 단다."""
    ig.apply_modifiers(ob)
    bpy.context.view_layer.update()
    me = ob.data
    me.transform(ob.matrix_world)
    ob.matrix_world.identity()
    layer = me.uv_layers.get("ImageUV") or me.uv_layers.new(name="ImageUV")
    for poly in me.polygons:
        n = poly.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            if ax == 0:
                u, v = co.y, co.z
            elif ax == 1:
                u, v = co.x, co.z
            else:
                u, v = co.x, co.y
            layer.data[li].uv = (u, v)
    return ob


def part(ob):
    return tile_uv(ob)


def bx(name, size, location, mat, rotation=(0.0, 0.0, 0.0), bevel=None):
    return part(ig.box(name, size, location=location, rotation=rotation, bevel=bevel, material=mat))


def tube(name, points, radius, mat):
    ob = ig.pipe(name, [Vector(p) for p in points], radius, resolution=8)
    ig.assign_material(ob, mat)
    return part(ob)


def lumpy(name, size, location, mat, sag=0.0, bulge=0.06, seed=0, wrinkle=0.0, levels=2, bevel=None):
    """천·마대처럼 무른 덩어리. 모서리를 둥글리고 윗면이 처진다. wrinkle이면 구김을 넣는다."""
    ob = ig.box(name, size, location=location, material=mat)
    if bevel:
        bev = ob.modifiers.new("Bevel", "BEVEL")
        bev.width = bevel
        bev.segments = 4
    mod = ob.modifiers.new("Sub", "SUBSURF")
    mod.levels = levels
    ig.apply_modifiers(ob)
    import random
    rnd = random.Random(seed)
    me = ob.data
    for v in me.vertices:
        local = Vector((v.co.x / max(size[0], 1e-4), v.co.y / max(size[1], 1e-4), v.co.z / max(size[2], 1e-4)))
        top = max(0.0, local.z)
        v.co.z -= sag * size[2] * (1.0 - (abs(local.x) * 2.0) ** 2) * top
        swell = 1.0 + bulge * (1.0 - (local.z * 2.0) ** 2) + rnd.uniform(-0.015, 0.015)
        v.co.x *= swell
        v.co.y *= swell
        if wrinkle > 0.0:
            # 손으로 구긴 천. 몇 가닥의 큰 주름이 엇갈린다.
            w = (math.sin(v.co.x * 23.0 + seed) * math.cos(v.co.y * 17.0 - seed * 0.7)
                 + 0.6 * math.sin((v.co.x + v.co.y) * 41.0 + seed * 1.3))
            v.co.z += wrinkle * w * (0.4 + top)
    return part(ob)


# --------------------------------------------------------------------------
# 간이침대
# --------------------------------------------------------------------------

def build_cot():
    steel = flat("CotSteel", (0.018, 0.018, 0.02), 0.45, metallic=0.85)
    canvas = tiled("CotCanvas", "T_CotCanvas", 0.5)
    blanket = tiled("CotBlanket", "T_BlanketFloral", 0.6)
    pillow = tiled("CotPillow", "T_CotCanvas", 0.4, tint=(0.55, 0.55, 0.58))
    L, W, H = 1.90, 0.65, 0.40
    parts = []
    # 캔버스 바닥판(가운데가 조금 처진다).
    sling = ig.box("Sling", (L - 0.06, W - 0.04, 0.02), location=(0, 0, 0), material=canvas)
    simple = sling.modifiers.new("Sub", "SUBSURF")
    simple.subdivision_type = "SIMPLE"
    simple.levels = 4
    ig.apply_modifiers(sling)
    for v in sling.data.vertices:
        u = v.co.x / (L * 0.5)
        w = v.co.y / (W * 0.5)
        v.co.z += H - 0.012 - 0.045 * max(0.0, 1.0 - u * u) * max(0.0, 1.0 - w * w)
    parts.append(part(sling))
    # 긴 쪽 관 둘, 짧은 쪽 끝 막대 둘.
    for y in (-W * 0.5, W * 0.5):
        parts.append(tube(f"Rail{y:+.2f}", [(-L * 0.5, y, H), (L * 0.5, y, H)], 0.014, steel))
    for x in (-L * 0.5 + 0.03, L * 0.5 - 0.03):
        parts.append(tube(f"End{x:+.2f}", [(x, -W * 0.5, H - 0.02), (x, W * 0.5, H - 0.02)], 0.011, steel))
    # X자 접이식 다리 셋.
    for x in (-0.72, 0.0, 0.72):
        parts.append(tube(f"LegA{x:+.2f}", [(x - 0.14, -W * 0.5 + 0.02, 0.01), (x + 0.14, -W * 0.5 + 0.02, H - 0.02)], 0.011, steel))
        parts.append(tube(f"LegB{x:+.2f}", [(x + 0.14, -W * 0.5 + 0.02, 0.01), (x - 0.14, -W * 0.5 + 0.02, H - 0.02)], 0.011, steel))
        parts.append(tube(f"LegC{x:+.2f}", [(x - 0.14, W * 0.5 - 0.02, 0.01), (x + 0.14, W * 0.5 - 0.02, H - 0.02)], 0.011, steel))
        parts.append(tube(f"LegD{x:+.2f}", [(x + 0.14, W * 0.5 - 0.02, 0.01), (x - 0.14, W * 0.5 - 0.02, H - 0.02)], 0.011, steel))
        parts.append(tube(f"Foot{x:+.2f}", [(x - 0.14, -W * 0.5 + 0.02, 0.01), (x - 0.14, W * 0.5 - 0.02, 0.01)], 0.009, steel))
    # 발치에 구겨 둔 담요, 머리맡 베개.
    parts.append(lumpy("Blanket", (0.66, 0.62, 0.10), (-0.52, 0.0, H + 0.035), blanket, sag=0.10, bulge=0.04, seed=3,
                       wrinkle=0.018, levels=3, bevel=0.03))
    parts.append(lumpy("BlanketFold", (0.30, 0.60, 0.07), (-0.30, 0.02, H + 0.085), blanket, sag=0.05, bulge=0.02, seed=5,
                       wrinkle=0.012, levels=3, bevel=0.025))
    parts.append(lumpy("Pillow", (0.46, 0.32, 0.12), (0.68, 0.0, H + 0.045), pillow, sag=0.10, bulge=0.03, seed=7,
                       wrinkle=0.006, levels=2, bevel=0.035))
    ig.build_asset("SM_InnerRoomCot", "prop", parts, OUT, collision_parts=None, texture_size=1024,
                   notes="관리실 안쪽 방 목한수의 간이침대. 길이 X 190 cm, 폭 Y 65, 높이 40")


# --------------------------------------------------------------------------
# 회색 철제 보관함
# --------------------------------------------------------------------------

def build_cabinet():
    paint = tiled("CabinetPaint", "T_CabinetPaintGrey", 0.6, metallic=0.2)
    dark = flat("CabinetGap", (0.02, 0.02, 0.022), 0.6)
    handle = flat("CabinetHandle", (0.06, 0.06, 0.065), 0.35, metallic=0.9)
    W, D, H = 0.90, 0.45, 1.80
    parts = [bx("Carcass", (W, D, H - 0.06), (0, 0, 0.06 + (H - 0.06) * 0.5), paint, bevel=0.004)]
    # 양문. 가운데 틈과 손잡이 홈, 작은 자물쇠.
    for side in (-1, 1):
        x = side * (W * 0.25 - 0.002)
        parts.append(bx(f"Door{side}", (W * 0.5 - 0.012, 0.012, H - 0.12), (x, -D * 0.5 - 0.004, 0.06 + (H - 0.06) * 0.5), paint, bevel=0.003))
        parts.append(bx(f"Pull{side}", (0.022, 0.012, 0.13), (side * 0.035, -D * 0.5 - 0.012, 1.05), handle))
    parts.append(bx("Gap", (0.006, 0.006, H - 0.14), (0, -D * 0.5 - 0.008, 0.06 + (H - 0.06) * 0.5), dark))
    parts.append(part(ig.cylinder("Lock", 0.011, 0.012, location=(0.055, -D * 0.5 - 0.012, 1.18), rotation=(math.pi * 0.5, 0, 0), material=handle)))
    # 다리.
    for x in (-W * 0.5 + 0.04, W * 0.5 - 0.04):
        for y in (-D * 0.5 + 0.04, D * 0.5 - 0.04):
            parts.append(bx(f"Foot{x:+.2f}{y:+.2f}", (0.04, 0.04, 0.06), (x, y, 0.03), dark))
    ig.build_asset("SM_InnerRoomCabinet", "prop", parts, OUT, collision_parts=None, texture_size=1024,
                   notes="관리실 안쪽 방 회색 철제 보관함. 폭 X 90, 깊이 Y 45(앞면 -Y), 높이 180")


# --------------------------------------------------------------------------
# 작업대
# --------------------------------------------------------------------------

def build_workbench():
    ply = tiled("BenchPly", "T_WorkbenchPlywood", 0.8)
    frame = tiled("BenchFrame", "T_CabinetPaintGrey", 0.5, metallic=0.3, tint=(0.45, 0.47, 0.5))
    tape = flat("BlackTape", (0.012, 0.012, 0.012), 0.45)
    tool = flat("ToolHandle", (0.42, 0.06, 0.04), 0.4)
    steel = flat("ToolSteel", (0.55, 0.55, 0.56), 0.3, metallic=1.0)
    W, D, H = 1.40, 0.60, 0.80
    parts = [bx("Top", (W, D, 0.035), (0, 0, H - 0.0175), ply, bevel=0.003)]
    parts.append(bx("Shelf", (W - 0.08, D - 0.08, 0.02), (0, 0, 0.18), ply))
    for x in (-W * 0.5 + 0.03, W * 0.5 - 0.03):
        for y in (-D * 0.5 + 0.03, D * 0.5 - 0.03):
            parts.append(bx(f"Leg{x:+.2f}{y:+.2f}", (0.045, 0.045, H - 0.035), (x, y, (H - 0.035) * 0.5), frame))
    parts.append(bx("Apron", (W - 0.06, 0.02, 0.08), (0, -D * 0.5 + 0.02, H - 0.075), frame))
    # 위에 놓인 것: 검은 테이프 한 롤, 드라이버, 칼.
    parts.append(part(ig.torus("TapeRoll", 0.045, 0.012, location=(0.52, 0.12, H + 0.012), material=tape)))
    parts.append(part(ig.cylinder("DriverGrip", 0.013, 0.10, location=(0.30, 0.18, H + 0.014), rotation=(0, math.pi * 0.5, 0.3), material=tool)))
    parts.append(part(ig.cylinder("DriverShaft", 0.003, 0.11, location=(0.40, 0.21, H + 0.014), rotation=(0, math.pi * 0.5, 0.3), material=steel)))
    parts.append(bx("Cutter", (0.15, 0.022, 0.012), (-0.45, 0.20, H + 0.007), tool, rotation=(0, 0, -0.4)))
    ig.build_asset("SM_InnerRoomWorkbench", "prop", parts, OUT, collision_parts=None, texture_size=1024,
                   notes="관리실 안쪽 방 작업대. 폭 X 140, 깊이 Y 60, 높이 80. 윗면 위에 물증 액터를 따로 놓는다")


# --------------------------------------------------------------------------
# 평대차 손수레
# --------------------------------------------------------------------------

def build_hand_cart():
    deck = tiled("CartDeck", "T_CabinetPaintGrey", 0.5, metallic=0.4, tint=(0.7, 0.72, 0.74))
    mat = flat("CartMat", (0.03, 0.03, 0.032), 0.85)
    blue = flat("CartHandle", (0.05, 0.08, 0.30), 0.45, metallic=0.5)
    rubber = flat("CartWheel", (0.02, 0.02, 0.02), 0.8)
    L, W = 0.90, 0.60
    deck_z = 0.15
    parts = [bx("Deck", (L, W, 0.03), (0, 0, deck_z), deck, bevel=0.004),
             bx("Mat", (L - 0.04, W - 0.04, 0.006), (0, 0, deck_z + 0.018), mat)]
    # 손잡이(한쪽 짧은 변, +X).
    hx = L * 0.5 - 0.02
    parts.append(tube("Handle", [(hx, -W * 0.5 + 0.03, deck_z), (hx, -W * 0.5 + 0.03, 0.98), (hx, W * 0.5 - 0.03, 0.98), (hx, W * 0.5 - 0.03, deck_z)], 0.013, blue))
    parts.append(tube("HandleBar", [(hx, -W * 0.5 + 0.03, 0.60), (hx, W * 0.5 - 0.03, 0.60)], 0.010, blue))
    for x in (-L * 0.5 + 0.08, L * 0.5 - 0.08):
        for y in (-W * 0.5 + 0.07, W * 0.5 - 0.07):
            parts.append(part(ig.cylinder(f"Wheel{x:+.2f}{y:+.2f}", 0.05, 0.03, location=(x, y, 0.05), rotation=(math.pi * 0.5, 0, 0), material=rubber)))
            parts.append(bx(f"Fork{x:+.2f}{y:+.2f}", (0.04, 0.05, 0.07), (x, y, 0.105), deck))
    ig.build_asset("SM_HandCart", "prop", parts, OUT, collision_parts=None, texture_size=1024,
                   notes="필로티 평대차. 길이 X 90(손잡이 +X), 폭 Y 60, 손잡이 높이 98")


def build_hand_cart_folded():
    """손잡이를 접어 세운 평대차(Content/SourceArt/AI/HandCartFolded_20261029). 밤 동안 벽에 세워 둔다.

    펼친 손수레와 같은 판·매트·바퀴를 쓰고, 손잡이를 경첩(손잡이 쪽 끝, 판 위)에서 매트 위로 눕힌 뒤 경첩 쪽 끝을 바닥에
    대고 세운다. 손잡이 가로대에 사슬을 감아 자물쇠를 건다. 시안에는 바퀴가 한쪽에 셋으로 그려졌지만 펼친 손수레처럼
    넷이다. 바퀴 쪽이 벽에 닿는 X 0, 매트 쪽(바깥)이 +X, 폭은 Y ±30, 바닥이 Z 0이다.
    """
    deck = tiled("CartDeck", "T_CabinetPaintGrey", 0.5, metallic=0.4, tint=(0.7, 0.72, 0.74))
    mat = flat("CartMat", (0.03, 0.03, 0.032), 0.85)
    blue = flat("CartHandle", (0.05, 0.08, 0.30), 0.45, metallic=0.5)
    rubber = flat("CartWheel", (0.02, 0.02, 0.02), 0.8)
    steel = flat("CartChain", (0.40, 0.40, 0.38), 0.45, metallic=0.9)
    brass = flat("CartPadlock", (0.52, 0.38, 0.14), 0.4, metallic=0.9)
    L, W = 0.90, 0.60
    deck_z = 0.15
    parts = [bx("Deck", (L, W, 0.03), (0, 0, deck_z), deck, bevel=0.004),
             bx("Mat", (L - 0.04, W - 0.04, 0.006), (0, 0, deck_z + 0.018), mat)]
    # 펼쳤을 때 높이 z이던 손잡이 점은 접으면 경첩에서 (z - deck_z)만큼 -X 쪽, 매트 위 관 반지름 높이에 눕는다.
    hx = L * 0.5 - 0.02
    lay = deck_z + 0.021 + 0.013
    y0, y1 = -W * 0.5 + 0.03, W * 0.5 - 0.03

    def fold(z):
        return hx - (z - deck_z)

    parts.append(tube("Handle", [(hx, y0, lay), (fold(0.98), y0, lay), (fold(0.98), y1, lay), (hx, y1, lay)], 0.013, blue))
    bar_x = fold(0.60)
    parts.append(tube("HandleBar", [(bar_x, y0, lay), (bar_x, y1, lay)], 0.010, blue))
    for y in (y0, y1):
        parts.append(bx(f"Hinge{y:+.2f}", (0.05, 0.022, 0.026), (hx, y, deck_z + 0.028), deck))
    for x in (-L * 0.5 + 0.08, L * 0.5 - 0.08):
        for y in (-W * 0.5 + 0.07, W * 0.5 - 0.07):
            parts.append(part(ig.cylinder(f"Wheel{x:+.2f}{y:+.2f}", 0.05, 0.03, location=(x, y, 0.05), rotation=(math.pi * 0.5, 0, 0), material=rubber)))
            parts.append(bx(f"Fork{x:+.2f}{y:+.2f}", (0.04, 0.05, 0.07), (x, y, 0.105), deck))
    # 가로대를 감은 사슬. 세우면 아래(경첩 쪽, 지금 +X)로 늘어진다. 첫 고리가 가로대를 감고 고리마다 90도 돈다.
    link = 0.026
    for i in range(6):
        rotation = (math.pi * 0.5, 0, 0) if i % 2 == 0 else (0, 0, 0)
        parts.append(part(ig.torus(f"Link{i}", 0.012, 0.0032, location=(bar_x + i * link, 0.02, lay + 0.004), rotation=rotation,
                                   major_segments=16, minor_segments=8, material=steel)))
    lock_x = bar_x + 6 * link + 0.016
    parts.append(part(ig.torus("Shackle", 0.011, 0.0028, location=(lock_x - 0.014, 0.02, lay + 0.004), rotation=(math.pi * 0.5, 0, 0),
                               major_segments=16, minor_segments=8, material=steel)))
    parts.append(bx("Padlock", (0.034, 0.030, 0.013), (lock_x + 0.010, 0.02, lay + 0.004), brass, bevel=0.003))
    # 세운다. 매트 쪽(+Z)이 바깥(+X)으로, 경첩 쪽(+X)이 바닥(-Z)으로 가도록 Y축으로 90도 돌린 뒤 바닥과 벽에 맞춘다.
    stand = Matrix.Rotation(math.radians(90.0), 4, "Y")
    for ob in parts:
        ob.data.transform(stand)
    low = Vector((min(v.co.x for ob in parts for v in ob.data.vertices), 0.0,
                  min(v.co.z for ob in parts for v in ob.data.vertices)))
    for ob in parts:
        ob.data.transform(Matrix.Translation(-low))
    ig.build_asset("SM_HandCartFolded", "prop", parts, OUT, collision_parts=None, texture_size=1024,
                   notes="손잡이를 접어 세운 평대차. 바퀴 쪽이 벽(X 0), 매트 쪽 +X, 폭 Y 60, 높이 90, 두께 약 20")


# --------------------------------------------------------------------------
# 마대·상자
# --------------------------------------------------------------------------

def build_sack():
    sack = tiled("Sack", "T_WovenSack", 0.3)
    rope = flat("SackRope", (0.62, 0.55, 0.40), 0.8)
    body = ig.box("SackBody", (0.50, 0.38, 0.66), location=(0, 0, 0.33), material=sack)
    bev = body.modifiers.new("Bevel", "BEVEL")
    bev.width = 0.07
    bev.segments = 4
    sub = body.modifiers.new("Sub", "SUBSURF")
    sub.levels = 2
    ig.apply_modifiers(body)
    # 윗부분을 끈 쪽으로 모아 오므린다. 짐이 든 아래는 각지게 처진다.
    for v in body.data.vertices:
        # 정점은 몸통 가운데(높이 33 cm) 기준이다.
        if v.co.z + 0.33 > 0.46:
            t = min(1.0, (v.co.z + 0.33 - 0.46) / 0.20)
            gather = 1.0 - 0.80 * (t * t * (3.0 - 2.0 * t))
            v.co.x *= gather
            v.co.y *= gather
        else:
            v.co.x *= 1.0 + 0.05 * math.sin(v.co.z * 9.0)
    parts = [part(body)]
    parts.append(part(ig.cylinder("Neck", 0.055, 0.05, location=(0, 0, 0.685), radius_top=0.045, material=sack)))
    parts.append(part(ig.torus("Tie", 0.05, 0.008, location=(0, 0, 0.69), material=rope)))
    parts.append(part(ig.cylinder("Ear", 0.045, 0.06, location=(0, 0, 0.735), radius_top=0.075, material=sack)))
    ig.build_asset("SM_WasteSack", "prop", parts, OUT, collision_parts=None, texture_size=512,
                   notes="수거를 기다리는 흰 PP 마대(목을 끈으로 묶음). 높이 약 80")


def build_box():
    card = tiled("BoxCard", "T_MovingBoxCardboard", 0.5)
    tape = flat("BoxTape", (0.55, 0.42, 0.22), 0.35)
    label = flat("BoxLabel", (0.86, 0.85, 0.80), 0.7)
    W, D, H = 0.50, 0.38, 0.34
    parts = [bx("Box", (W, D, H), (0, 0, H * 0.5), card, bevel=0.004)]
    parts.append(bx("TapeTop", (0.06, D + 0.004, 0.002), (0, 0, H + 0.001), tape))
    parts.append(bx("TapeFront", (0.06, 0.002, 0.08), (0, -D * 0.5 - 0.001, H - 0.04), tape))
    parts.append(bx("TapeBack", (0.06, 0.002, 0.08), (0, D * 0.5 + 0.001, H - 0.04), tape))
    # 이름표 자리(빈 종이). 이름은 게임이 따로 그린다.
    parts.append(bx("Label", (0.16, 0.002, 0.09), (0.12, -D * 0.5 - 0.0015, H * 0.55), label))
    ig.build_asset("SM_StorageBox", "prop", parts, OUT, collision_parts=None, texture_size=512,
                   notes="끈 대신 테이프로 닫은 판지 상자 50×38×34. 앞면(-Y)에 빈 이름표")


# --------------------------------------------------------------------------
# 떼어 낸 판재 더미
# --------------------------------------------------------------------------

def build_removed_boards():
    gypsum = tiled("BoardGypsum", "T_MissingFloorDryPlaster", 0.8)
    ply = tiled("BoardPly", "T_WorkbenchPlywood", 0.8, tint=(0.8, 0.78, 0.74))
    membrane = tiled("BoardMembrane", "T_LiningMembrane", 1.0)
    silicone = flat("BoardSilicone", (0.50, 0.50, 0.47), 0.42)
    screw = flat("BoardScrew", (0.2, 0.2, 0.21), 0.4, metallic=0.9)
    parts = []
    # 벽에 기대 선 판 넷(앞면 -Y, 위가 벽 쪽으로 기운다). 앞뒤로 조금씩 어긋난다.
    lean = math.radians(8.0)
    specs = [(ply, 1.20, 1.80, 0.012, 0.00), (gypsum, 1.20, 1.75, 0.0125, 0.03), (gypsum, 0.90, 1.60, 0.0125, 0.06), (ply, 1.10, 1.50, 0.012, 0.09)]
    for i, (mat, w, h, t, y) in enumerate(specs):
        x = (i - 1.5) * 0.04
        parts.append(bx(f"Board{i}", (w, t, h), (x, -y, h * 0.5 * math.cos(lean)), mat, rotation=(lean, 0, 0)))
    # 맨 앞 판에 남은 방수막 조각과 실리콘 줄, 나사 자국.
    front_y = -0.09 - 0.02
    parts.append(bx("MembranePatch", (0.46, 0.004, 0.52), (0.18, front_y, 0.72), membrane, rotation=(lean, 0, 0)))
    parts.append(bx("MembranePatch2", (0.30, 0.004, 0.24), (-0.30, front_y, 1.12), membrane, rotation=(lean, 0, 0.05)))
    parts.append(tube("Bead", [(-0.45, front_y - 0.004, 0.30), (-0.10, front_y - 0.006, 0.33), (0.40, front_y - 0.004, 0.31)], 0.009, silicone))
    for i, (sx, sz) in enumerate(((-0.5, 0.4), (-0.5, 1.0), (0.48, 0.42), (0.48, 1.05), (0.0, 1.38))):
        parts.append(part(ig.cylinder(f"Screw{i}", 0.006, 0.004, location=(sx, front_y - 0.002 - sz * math.sin(lean) * 0.0, sz), rotation=(math.pi * 0.5 - lean, 0, 0), material=screw)))
    ig.build_asset("SM_RemovedBoards", "prop", parts, OUT, collision_parts=None, texture_size=1024,
                   notes="벽에서 떼어 낸 판재 넷(합판·석고보드). 방수막 조각과 실리콘, 나사 자국이 남았다. 앞면 -Y, 벽에 기댐")


# --------------------------------------------------------------------------
# 백열등
# --------------------------------------------------------------------------

def build_bulb():
    cord = flat("BulbCord", (0.02, 0.02, 0.02), 0.6)
    socket = flat("BulbSocket", (0.75, 0.72, 0.62), 0.5)
    glass = ig.mat_emissive("BulbGlow", (1.0, 0.82, 0.55), strength=3.0)
    parts = [tube("Cord", [(0, 0, 0.0), (0, 0, -0.42)], 0.004, cord),
             part(ig.cylinder("Socket", 0.018, 0.05, location=(0, 0, -0.445), material=socket))]
    bulb = ig.box("Bulb", (0.06, 0.06, 0.08), location=(0, 0, -0.51), material=glass)
    mod = bulb.modifiers.new("Sub", "SUBSURF")
    mod.levels = 3
    parts.append(part(bulb))
    ig.build_asset("SM_BareBulb", "prop", parts, OUT, collision_parts=[], texture_size=256, origin="top-center",
                   notes="천장에 줄로 매단 백열등. 원점이 천장 매다는 점, 전구 가운데가 아래 51 cm")


# --------------------------------------------------------------------------
# 방음 벽판(관리실 안쪽 방). 1 m 판을 벽에 이어 붙인다.
# --------------------------------------------------------------------------

def build_lined_panels():
    foam = tiled("PanelFoam", "T_LiningFoam", 0.60, normal_strength=1.4)
    membrane = tiled("PanelMembrane", "T_LiningMembrane", 1.0, normal_strength=0.8)
    silicone = flat("PanelSilicone", (0.48, 0.48, 0.45), 0.42)
    glue = flat("PanelGlue", (0.012, 0.011, 0.010), 0.22)
    # A: 방음재 판 넷을 실리콘으로 이어 붙인 판. 앞면 -Y, 원점은 판 아래 가운데.
    parts = []
    for i, (x, z, w, h) in enumerate(((-0.25, 0.25, 0.49, 0.49), (0.25, 0.25, 0.49, 0.49), (-0.25, 0.75, 0.49, 0.49), (0.25, 0.75, 0.49, 0.49))):
        parts.append(bx(f"Foam{i}", (w, 0.035, h), (x, -0.0175, z), foam, bevel=0.006))
    parts.append(tube("SeamV", [(0.0, -0.036, 0.02), (0.003, -0.038, 0.5), (-0.002, -0.036, 0.98)], 0.008, silicone))
    parts.append(tube("SeamH", [(-0.48, -0.036, 0.5), (0.0, -0.038, 0.502), (0.48, -0.036, 0.498)], 0.008, silicone))
    ig.build_asset("SM_LinedWallPanelA", "prop", parts, OUT, collision_parts=[], texture_size=1024,
                   notes="계란판 방음재 판 넷을 실리콘으로 이은 1 m 판. 앞면 -Y, 두께 3.5 cm, 원점 아래 가운데")
    ig.reset_scene()
    # B: 방음재 위에 방수막을 덧대고 가장자리를 실리콘으로 짠 판. 덧댄 자리를 드러낸다.
    foam = tiled("PanelFoamB", "T_LiningFoam", 0.60, normal_strength=1.4)
    membrane = tiled("PanelMembraneB", "T_LiningMembrane", 1.0, normal_strength=0.8)
    silicone = flat("PanelSiliconeB", (0.48, 0.48, 0.45), 0.42)
    glue = flat("PanelGlueB", (0.012, 0.011, 0.010), 0.22)
    parts = [bx("FoamBase", (0.98, 0.035, 0.98), (0.0, -0.0175, 0.5), foam, bevel=0.006)]
    parts.append(bx("Membrane", (0.62, 0.006, 0.70), (0.12, -0.038, 0.46), membrane, rotation=(0, 0.04, 0)))
    parts.append(bx("Membrane2", (0.30, 0.006, 0.22), (-0.30, -0.039, 0.84), membrane, rotation=(0, -0.08, 0)))
    parts.append(tube("Bead1", [(-0.20, -0.043, 0.10), (-0.19, -0.045, 0.45), (-0.21, -0.043, 0.80)], 0.009, silicone))
    parts.append(tube("Bead2", [(-0.19, -0.043, 0.81), (0.15, -0.045, 0.82), (0.44, -0.043, 0.80)], 0.009, silicone))
    parts.append(tube("Glue", [(0.40, -0.040, 0.12), (0.43, -0.046, 0.18), (0.41, -0.041, 0.24)], 0.006, glue))
    ig.build_asset("SM_LinedWallPanelB", "prop", parts, OUT, collision_parts=[], texture_size=1024,
                   notes="방음재 위에 방수막을 덧대고 실리콘으로 두른 1 m 판. 앞면 -Y")


if __name__ == "__main__":
    ig.reset_scene()
    only = set(filter(None, os.environ.get("IG_ONLY", "").split(",")))
    # 두 벽판을 함께 만드는 함수이므로 B만 골라도 실행한다.
    if "SM_LinedWallPanelB" in only:
        only.add("SM_LinedWallPanelA")
    builders = {
        "SM_InnerRoomCot": build_cot,
        "SM_InnerRoomCabinet": build_cabinet,
        "SM_InnerRoomWorkbench": build_workbench,
        "SM_HandCart": build_hand_cart,
        "SM_HandCartFolded": build_hand_cart_folded,
        "SM_WasteSack": build_sack,
        "SM_StorageBox": build_box,
        "SM_RemovedBoards": build_removed_boards,
        "SM_BareBulb": build_bulb,
        "SM_LinedWallPanelA": build_lined_panels,
    }
    for name, builder in builders.items():
        if only and name not in only:
            continue
        ig.reset_scene()
        builder()
