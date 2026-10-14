"""「302호에 남은 짐」 소품 둘째 묶음(2026-10-13).

벽 점검구(작은·중간·큰 세 크기의 틀과 문짝), 덧댄 것이 눌러 막은 자리의 메움재, 5층 배관
점검구에서 떼어 낸 덮개 조각 셋과 금속 틀·고정쇠, 오빠의 공구 주머니, 정우의 장갑 주머니,
5층 점검 자리를 덮은 덧판과 꺾인 차폐판, 작업 자재 쐐기, 302호에 기대 둔 마감판.

형태는 Content/SourceArt/AI의 같은 날 시안(AccessHatch·AccessCoverFront·ToolPouch·SacksBoxes)을
보고 잡았다. 덮개 앞면은 AccessCoverFront 그림을 덮개 전체 좌표로 펴서 조각마다 제 몫을 굽는다.
그래서 조각을 맞추면 얼룩이 한 장으로 이어진다.

    blender -b --factory-startup --python Scripts/blender/build_leftover_props.py
    (IG_ONLY=SM_AccessHatchFrameS,... 로 일부만)

좌표는 UE 기준 미터다. 점검구 틀·문짝과 덮개는 +X가 방 쪽 법선이다(벽 면이 X 0).
"""

import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import bpy  # noqa: E402
import bmesh  # noqa: E402
from mathutils import Vector  # noqa: E402

import ig_blender_lib as ig  # noqa: E402
from build_inner_room import bx, flat, lumpy, part, tiled, tile_uv, tube  # noqa: E402

SOURCE_ART = os.path.abspath(os.path.join(HERE, "..", "..", "Content", "SourceArt"))
OUT = ig.out_root_from_argv()

# 점검구 개구부(가로 Y, 세로 Z, m). IGAccessHatch의 OpeningSize와 같다.
HATCH_SIZES = {"S": (0.45, 0.45), "M": (0.50, 0.70), "L": (0.80, 1.00)}
FRAME_BAND = 0.03
FRAME_PROUD = 0.006
SLEEVE_DEPTH = 0.04
LEAF_T = 0.012

# 배관 점검구 덮개. 가로(Y) 60, 세로(Z) 70, 판 두께 1.2 cm, 앞면 방수막 1.5 mm.
COVER_W, COVER_H = 0.60, 0.70
BOARD_T = 0.012
MEMBRANE_T = 0.0015
FLAP_Z = 0.23
# 고정쇠 넷이 들어가는 나사 구멍. 조각 A가 왼쪽 둘(Y-), B가 오른쪽 위, C가 오른쪽 아래.
HOLES = {"A": [(-0.27, 0.32), (-0.27, -0.32)], "B": [(0.27, 0.32)], "C": [(0.27, -0.32)]}
A_EDGE = [(-0.02, 0.35), (0.0, 0.22), (-0.04, 0.10), (-0.0015, 0.0), (0.01, -0.03), (-0.03, -0.18), (0.0, -0.28),
          (-0.02, -0.35)]
BC_EDGE = [(-0.0015, 0.0), (0.08, 0.03), (0.16, -0.01), (0.23, 0.02), (0.30, 0.0)]
VENT_SLOTS = {"A": [-0.25, -0.16, -0.07], "B": [0.06, 0.15, 0.24]}


def piece_polygon(name):
    if name == "A":
        return [(-0.30, 0.35)] + A_EDGE + [(-0.30, -0.35)]
    if name == "B":
        upper = A_EDGE[:4]  # (-0.02,0.35) .. (-0.0015,0.0)
        return [(-0.02, 0.35), (0.30, 0.35), (0.30, 0.0)] + list(reversed(BC_EDGE[1:-1])) + \
            [(-0.0015, 0.0)] + list(reversed(upper[1:3]))
    lower = A_EDGE[3:]  # (-0.0015,0.0) .. (-0.02,-0.35)
    return BC_EDGE + [(0.30, -0.35)] + list(reversed(lower[1:]))


def clip(poly, z0, keep_above):
    """Sutherland-Hodgman. z0 위(또는 아래)만 남긴다."""
    def inside(p):
        return p[1] >= z0 if keep_above else p[1] <= z0

    out = []
    for i, cur in enumerate(poly):
        prev = poly[i - 1]
        if inside(cur):
            if not inside(prev):
                t = (z0 - prev[1]) / (cur[1] - prev[1])
                out.append((prev[0] + (cur[0] - prev[0]) * t, z0))
            out.append(cur)
        elif inside(prev):
            t = (z0 - prev[1]) / (cur[1] - prev[1])
            out.append((prev[0] + (cur[0] - prev[0]) * t, z0))
    return out


def extrude_yz(name, points, x0, x1, mat, image_uv=False, uv_z_offset=0.0):
    """YZ 평면 다각형을 X로 x0..x1 만큼 민다. image_uv면 덮개 전체 좌표로 ImageUV를 단다.
    uv_z_offset은 원점을 옮긴 조각(방수막 윗부분)이 덮개 좌표로 돌아가는 Z 차이다."""
    bm = bmesh.new()
    back = [bm.verts.new((x0, y, z)) for y, z in points]
    front = [bm.verts.new((x1, y, z)) for y, z in points]
    bm.faces.new(back)
    bm.faces.new(list(reversed(front)))
    n = len(points)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((back[i], back[j], front[j], front[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    ig.assign_material(ob, mat)
    if image_uv:
        layer = me.uv_layers.new(name="ImageUV")
        for poly in me.polygons:
            for li in poly.loop_indices:
                co = me.vertices[me.loops[li].vertex_index].co
                # 방에서 앞면(+X)을 보면 화면 오른쪽이 -Y다.
                layer.data[li].uv = ((COVER_W * 0.5 - co.y) / COVER_W,
                                     (co.z + uv_z_offset + COVER_H * 0.5) / COVER_H)
        return ob
    return tile_uv(ob)


# --------------------------------------------------------------------------
# 점검구
# --------------------------------------------------------------------------

def hatch_paint(name):
    return ig.mat_painted_steel(name, (0.80, 0.79, 0.74), roughness=0.42, wear=0.35, bump=0.03,
                                dirt_color=(0.52, 0.47, 0.38))


def build_hatch_frame(size_key):
    w, h = HATCH_SIZES[size_key]
    paint = hatch_paint(f"HatchFrame{size_key}")
    screw = flat(f"HatchScrew{size_key}", (0.55, 0.55, 0.53), 0.35, metallic=0.9)
    b = FRAME_BAND
    parts = []
    # 벽 면에 붙는 테두리. 안쪽 모서리는 둥글게 접혀 있다.
    for z, length in ((h * 0.5 + b * 0.5, w + 2 * b), (-h * 0.5 - b * 0.5, w + 2 * b)):
        parts.append(bx(f"BandZ{z:+.2f}", (FRAME_PROUD, length, b), (FRAME_PROUD * 0.5, 0.0, z), paint, bevel=0.0015))
    for y in (w * 0.5 + b * 0.5, -w * 0.5 - b * 0.5):
        parts.append(bx(f"BandY{y:+.2f}", (FRAME_PROUD, b, h), (FRAME_PROUD * 0.5, y, 0.0), paint, bevel=0.0015))
    # 벽 속으로 들어가는 슬리브. 개구부 테두리를 따라 4 cm.
    t = 0.0015
    for z in (h * 0.5 + t * 0.5, -h * 0.5 - t * 0.5):
        parts.append(bx(f"SleeveZ{z:+.2f}", (SLEEVE_DEPTH, w, t), (-SLEEVE_DEPTH * 0.5, 0.0, z), paint))
    for y in (w * 0.5 + t * 0.5, -w * 0.5 - t * 0.5):
        parts.append(bx(f"SleeveY{y:+.2f}", (SLEEVE_DEPTH, t, h), (-SLEEVE_DEPTH * 0.5, y, 0.0), paint))
    # 모서리 고정 나사.
    for y in (w * 0.5 + b * 0.5, -w * 0.5 - b * 0.5):
        for z in (h * 0.5 + b * 0.5, -h * 0.5 - b * 0.5):
            parts.append(part(ig.cylinder(f"Screw{y:+.2f}{z:+.2f}", 0.0035, 0.002,
                                          location=(FRAME_PROUD + 0.001, y, z), rotation=(0.0, math.pi * 0.5, 0.0),
                                          segments=12, material=screw)))
    ig.build_asset(f"SM_AccessHatchFrame{size_key}", "prop", parts, OUT, collision_parts=[], texture_size=512,
                   preview_yaw=90, origin="opening-center",
                   notes=(f"벽 점검구 틀 {int(w * 100)}x{int(h * 100)}. 원점 개구부 가운데(벽 면 X 0), +X가 방 쪽. "
                          "테두리 3 cm가 6 mm 솟고 슬리브가 벽 속 4 cm. IGAccessHatch 틀."))


def build_hatch_door(size_key):
    w, h = HATCH_SIZES[size_key]
    paint = hatch_paint(f"HatchDoor{size_key}")
    chrome = ig.mat_metal(f"HatchLock{size_key}", color=(0.72, 0.72, 0.70), roughness=0.28)
    dark = flat(f"HatchKeyway{size_key}", (0.02, 0.02, 0.02), 0.5)
    lw, lh = w - 0.01, h - 0.01
    parts = []
    leaf = ig.box("Leaf", (LEAF_T, lw - 0.004, lh), location=(LEAF_T * 0.5, 0.003 + (lw - 0.004) * 0.5, 0.0),
                  bevel=0.0018, segments=2, material=paint)
    parts.append(part(leaf))
    # 앞면 가장자리를 2 mm 꺾은 테. 판을 접어 만든 문짝이다.
    for z in (lh * 0.5 - 0.006, -lh * 0.5 + 0.006):
        parts.append(bx(f"Return{z:+.2f}", (0.002, lw - 0.016, 0.004), (LEAF_T + 0.001, 0.003 + lw * 0.5, z), paint))
    # 반 바퀴 자물쇠(손잡이 쪽 가운데). 열쇠 홈은 세로.
    lock_y = 0.003 + lw - 0.055
    parts.append(part(ig.cylinder("LockBody", 0.011, 0.006, location=(LEAF_T + 0.003, lock_y, 0.0),
                                  rotation=(0.0, math.pi * 0.5, 0.0), segments=24, bevel=0.0012, material=chrome)))
    parts.append(bx("Keyway", (0.002, 0.0018, 0.009), (LEAF_T + 0.0062, lock_y, 0.0), dark))
    parts.append(bx("CamTongue", (0.004, 0.012, 0.030), (-0.003, lock_y + 0.004, 0.0), chrome))
    # 경첩 둘(경첩 쪽 세로 변, 문짝 앞면 쪽 모서리).
    for z in (lh * 0.5 - 0.07, -lh * 0.5 + 0.07):
        parts.append(part(ig.cylinder(f"Hinge{z:+.2f}", 0.0042, 0.05, location=(LEAF_T * 0.5, -0.001, z),
                                      segments=12, material=chrome)))
    # 자물쇠 둘레의 손때.
    stain = flat(f"HatchSmudge{size_key}", (0.46, 0.40, 0.31), 0.6)
    smudge = ig.box("Smudge", (0.0004, 0.07, 0.09), location=(LEAF_T + 0.0002, lock_y - 0.01, -0.01),
                    material=stain)
    parts.append(part(smudge))
    ig.build_asset(f"SM_AccessHatchDoor{size_key}", "prop", parts, OUT, collision_parts=[[leaf]],
                   texture_size=512, preview_yaw=90, origin="hinge-axis",
                   notes=(f"점검구 문짝 {int(w * 100)}x{int(h * 100)}용. 원점이 경첩 축 가운데, 문짝은 +Y로 "
                          f"{lw * 100:.0f} cm, 두께 X 0..1.2 cm(+X가 방 쪽 앞면). 자물쇠는 손잡이 쪽 가운데."))


def build_seal_plug():
    foam = tiled("PlugFoam", "T_LiningFoam", 0.45, normal_strength=1.6)
    glue = flat("PlugGlue", (0.010, 0.009, 0.008), 0.18)
    membrane = tiled("PlugMembrane", "T_LiningMembrane", 0.6, normal_strength=0.8)
    body = ig.box("Plug", (1.0, 0.98, 0.98), location=(0.0, 0.0, 0.0), material=foam)
    sub = body.modifiers.new("Sub", "SUBSURF")
    sub.subdivision_type = "SIMPLE"
    sub.levels = 4
    ig.apply_modifiers(body)
    rnd = random.Random(31)
    for v in body.data.vertices:
        if v.co.x > 0.45:
            # 개구부로 눌려 들어온 방음재. 가운데가 볼록하고 결이 뭉개졌다.
            r = math.sqrt(v.co.y ** 2 + v.co.z ** 2)
            v.co.x += 0.18 * max(0.0, 1.0 - r * 1.6) + rnd.uniform(-0.02, 0.02)
    parts = [part(body)]
    # 검은 접착층이 테두리를 따라 비어져 나왔다.
    for i, (y0, z0, y1, z1) in enumerate(((-0.47, 0.46, 0.47, 0.45), (0.47, 0.44, 0.46, -0.46),
                                         (0.46, -0.47, -0.45, -0.46), (-0.46, -0.45, -0.47, 0.44))):
        mid = ((y0 + y1) * 0.5 + rnd.uniform(-0.03, 0.03), (z0 + z1) * 0.5 + rnd.uniform(-0.03, 0.03))
        parts.append(tube(f"GlueRim{i}", [(0.52, y0, z0), (0.56, mid[0], mid[1]), (0.52, y1, z1)], 0.035, glue))
    parts.append(bx("MembraneScrap", (0.01, 0.42, 0.30), (0.60, 0.12, -0.18), membrane, rotation=(0.15, 0.0, 0.0)))
    ig.build_asset("SM_HatchSealPlug", "prop", parts, OUT, collision_parts=[], texture_size=512, preview_yaw=90,
                   origin="center",
                   notes=("덧댄 것이 눌러 막은 점검구 자리. 1 m 정육면체 기준이라 IGAccessHatch가 X 4 cm, "
                          "개구부 크기로 줄여 쓴다. +X 면이 방 쪽(뭉개진 방음재와 검은 접착층)."))


# --------------------------------------------------------------------------
# 배관 점검구 덮개 조각
# --------------------------------------------------------------------------

def cover_materials(tag):
    image = os.path.join(SOURCE_ART, "AI", "AccessCoverFront_20261013.png")
    front = ig.mat_image_uv(f"CoverFront{tag}", image, roughness=0.62)
    board = tiled(f"CoverBoard{tag}", "T_WorkbenchPlywood", 0.5)
    wrap = tiled(f"CoverWrap{tag}", "T_LiningMembrane", 0.4, normal_strength=0.6)
    hole = flat(f"CoverHole{tag}", (0.015, 0.012, 0.010), 0.8)
    ring = flat(f"CoverHoleRing{tag}", (0.62, 0.52, 0.38), 0.7)
    foam = tiled(f"CoverVentFoam{tag}", "T_LiningFoam", 0.2, normal_strength=1.2)
    return front, board, wrap, hole, ring, foam


def build_cover_piece(name):
    front, board, wrap, hole, ring, foam = cover_materials(name)
    poly = piece_polygon(name)
    parts = []
    slab = extrude_yz(f"Board{name}", poly, -BOARD_T, 0.0, board)
    for i, y in enumerate(VENT_SLOTS.get(name, [])):
        cutter = ig.box(f"Slot{i}", (0.05, 0.07, 0.03), location=(-BOARD_T * 0.5, y, 0.275))
        ig.boolean(slab, cutter)
        # 막아 놓은 통풍구. 방음재를 쑤셔 넣었다.
        parts.append(bx(f"VentFoam{i}", (BOARD_T * 0.8, 0.066, 0.026), (-BOARD_T * 0.55, y, 0.275), foam,
                        bevel=0.002))
    parts.insert(0, slab)
    main = clip(poly, FLAP_Z, keep_above=False) if name in ("A", "B") else poly
    parts.append(extrude_yz(f"Membrane{name}", main, 0.0, MEMBRANE_T, front, image_uv=True))
    # 뒷면: 덮개 바깥 변에만 방수막이 접혀 넘어와 있다. 자른 변은 판이 그대로 드러난다.
    edge = 0.015
    xs = (-BOARD_T - 0.0012, -BOARD_T)
    def wrap_strip(tag, y0, y1, z0, z1):
        parts.append(extrude_yz(f"Wrap{name}{tag}", [(y0, z0), (y1, z0), (y1, z1), (y0, z1)], xs[0], xs[1], wrap))
    ys = [p[0] for p in poly]
    y_lo, y_hi = min(ys), max(ys)
    if name == "A":
        wrap_strip("L", -0.30, -0.30 + edge, -0.35, 0.35)
        wrap_strip("T", -0.30, -0.025, 0.35 - edge, 0.35)
        wrap_strip("B", -0.30, -0.025, -0.35, -0.35 + edge)
    elif name == "B":
        wrap_strip("R", 0.30 - edge, 0.30, 0.0, 0.35)
        wrap_strip("T", -0.015, 0.30, 0.35 - edge, 0.35)
    else:
        wrap_strip("R", 0.30 - edge, 0.30, -0.35, 0.0)
        wrap_strip("B", -0.015, 0.30, -0.35, -0.35 + edge)
    # 고정쇠가 물리는 나사 구멍. 둘레 종이가 일어나 밝다.
    for y, z in HOLES[name]:
        parts.append(part(ig.cylinder(f"Ring{y:+.2f}{z:+.2f}", 0.0085, 0.0008, location=(-BOARD_T - 0.0004, y, z),
                                      rotation=(0.0, math.pi * 0.5, 0.0), segments=20, material=ring)))
        parts.append(part(ig.cylinder(f"Hole{y:+.2f}{z:+.2f}", 0.0042, 0.0012, location=(-BOARD_T - 0.0008, y, z),
                                      rotation=(0.0, math.pi * 0.5, 0.0), segments=16, material=hole)))
    ig.build_asset(f"SM_CoverPiece{name}", "prop", parts, OUT, collision_parts=[[slab]], texture_size=1024,
                   preview_yaw=90, origin="cover-center",
                   notes=(f"5층 배관 점검구 덮개 조각 {name}. 원점이 덮개 가운데(앞면 X 0), 앞면 +X에 방수막과 얼룩, "
                          "뒷면에 고정쇠 나사 구멍과 바깥 변의 접힌 방수막. 맞춘 자리 그대로 놓으면 한 장이 된다."))


def build_cover_flap(name):
    front, board, wrap, hole, ring, foam = cover_materials(f"Flap{name}")
    top = clip(piece_polygon(name), FLAP_Z, keep_above=True)
    # 원점은 덮개 위 변(경첩선) 가운데. 조각에 붙여 들춘다.
    shifted = [(y, z - 0.35) for y, z in top]
    ob = extrude_yz(f"Flap{name}", shifted, 0.0, MEMBRANE_T, front, image_uv=True, uv_z_offset=0.35)
    ig.build_asset(f"SM_CoverFlap{name}", "prop", [ob], OUT, collision_parts=[], texture_size=512,
                   preview_yaw=90, origin="cover-top-edge",
                   notes=(f"덮개 조각 {name}의 윗부분 방수막. 원점이 덮개 위 변 가운데(Z +35 cm). 들추면 막아 놓은 "
                          "통풍구가 보인다."))


def build_cover_frame():
    steel = ig.mat_metal("CoverFrameSteel", color=(0.60, 0.61, 0.60), roughness=0.38)
    screw = flat("CoverFrameScrew", (0.45, 0.45, 0.43), 0.35, metallic=0.9)
    parts = []
    w, h = COVER_W + 0.002, COVER_H + 0.002
    t = 0.0016
    # 앞면 테(벽 면에 걸리는 날개)와 덮개 둘레 슬리브.
    for z in (h * 0.5 + 0.01, -h * 0.5 - 0.01):
        parts.append(bx(f"FlangeZ{z:+.2f}", (t, w + 0.04, 0.02), (0.001, 0.0, z), steel, bevel=0.0006))
    for y in (w * 0.5 + 0.01, -w * 0.5 - 0.01):
        parts.append(bx(f"FlangeY{y:+.2f}", (t, 0.02, h), (0.001, y, 0.0), steel, bevel=0.0006))
    for z in (h * 0.5 + t * 0.5, -h * 0.5 - t * 0.5):
        parts.append(bx(f"SleeveZ{z:+.2f}", (0.03, w, t), (-0.014, 0.0, z), steel))
    for y in (w * 0.5 + t * 0.5, -w * 0.5 - t * 0.5):
        parts.append(bx(f"SleeveY{y:+.2f}", (0.03, t, h), (-0.014, y, 0.0), steel))
    # 날개의 벽 고정 구멍(5층 벽의 나사 자리와 같은 간격).
    for y, z in ((-0.20, h * 0.5 + 0.01), (0.20, h * 0.5 + 0.01), (-0.20, -h * 0.5 - 0.01), (0.20, -h * 0.5 - 0.01)):
        parts.append(part(ig.cylinder(f"Fix{y:+.2f}{z:+.2f}", 0.0032, 0.003, location=(0.0025, y, z),
                                      rotation=(0.0, math.pi * 0.5, 0.0), segments=12, material=screw)))
    # 고정쇠가 도는 기둥(모서리 안쪽).
    for y in (0.30, -0.30):
        for z in (0.35, -0.35):
            parts.append(part(ig.cylinder(f"Post{y:+.2f}{z:+.2f}", 0.004, 0.02,
                                          location=(-0.022, y - math.copysign(0.004, y), z - math.copysign(0.004, z)),
                                          rotation=(0.0, math.pi * 0.5, 0.0), segments=12, material=screw)))
    ig.build_asset("SM_CoverFrame", "prop", parts, OUT, collision_parts=[], texture_size=512, preview_yaw=90,
                   origin="cover-center",
                   notes=("배관 점검구 덮개의 금속 틀. 원점이 덮개 가운데, 앞면 날개가 X 0(벽 면), 슬리브가 -X 3 cm. "
                          "모서리 안쪽 기둥에 고정쇠(SM_CoverClamp)를 단다."))


def build_cover_clamp():
    steel = ig.mat_metal("ClampSteel", color=(0.66, 0.66, 0.64), roughness=0.3)
    parts = []
    # 원점이 도는 축(X). 날개는 +Y로 4.5 cm, 끝 아래(-X)에 구멍에 물리는 핀.
    parts.append(bx("Tab", (0.003, 0.045, 0.012), (0.0, 0.0225, 0.0), steel, bevel=0.0008))
    parts.append(part(ig.cylinder("Boss", 0.006, 0.005, location=(0.0, 0.0, 0.0), rotation=(0.0, math.pi * 0.5, 0.0),
                                  segments=16, material=steel)))
    parts.append(part(ig.cylinder("Pin", 0.0028, 0.006, location=(-0.004, 0.042, 0.0),
                                  rotation=(0.0, math.pi * 0.5, 0.0), segments=12, material=steel)))
    ig.build_asset("SM_CoverClamp", "prop", parts, OUT, collision_parts=[], texture_size=256, preview_yaw=90,
                   origin="pivot",
                   notes="덮개 고정쇠. 원점이 축(X), 날개 +Y 4.5 cm, 끝 -X 쪽 핀이 나사 구멍에 물린다.")


# --------------------------------------------------------------------------
# 주머니 둘
# --------------------------------------------------------------------------

def build_tool_pouch():
    canvas = tiled("PouchCanvas", "T_CotCanvas", 0.25, tint=(0.50, 0.33, 0.16))
    leather = flat("PouchLeather", (0.085, 0.035, 0.018), 0.55)
    felt = flat("PouchFelt", (0.42, 0.38, 0.33), 0.95)
    rubber = flat("PouchRubber", (0.05, 0.05, 0.05), 0.7)
    red = flat("PouchFeltRed", (0.36, 0.06, 0.05), 0.9)
    parts = []
    # 말아 둔 천 주머니. X로 45 cm, 지름 11 cm. 겉면이 조금 납작하다.
    roll = ig.cylinder("Roll", 0.055, 0.45, location=(0.0, 0.0, 0.05), rotation=(0.0, math.pi * 0.5, 0.0),
                       segments=40, material=canvas)
    for v in roll.data.vertices:
        v.co.y *= 0.92
    parts.append(part(roll))
    parts.append(part(ig.torus("FlapEdge", 0.056, 0.004, location=(0.21, 0.0, 0.05),
                               rotation=(0.0, math.pi * 0.5, 0.0), material=leather)))
    parts.append(part(ig.torus("FlapEdge2", 0.056, 0.004, location=(-0.21, 0.0, 0.05),
                               rotation=(0.0, math.pi * 0.5, 0.0), material=leather)))
    # 가죽 끈 두 바퀴와 늘어진 끝.
    for x in (-0.012, 0.012):
        parts.append(part(ig.torus(f"Strap{x:+.2f}", 0.0565, 0.0045, location=(x, 0.0, 0.05),
                                   rotation=(0.0, math.pi * 0.5, 0.0), material=leather)))
    parts.append(tube("StrapTail", [(0.0, -0.05, 0.03), (0.01, -0.07, 0.005), (0.03, -0.085, 0.002)], 0.005, leather))
    # 한쪽 끝으로 비어져 나온 펠트 조각과 고무 쐐기(조율 때 현을 막는 것).
    for i, (y, z, mat) in enumerate(((-0.02, 0.06, felt), (0.015, 0.035, red), (0.02, 0.07, felt))):
        parts.append(bx(f"Felt{i}", (0.03, 0.012, 0.03), (0.235, y, z), mat, rotation=(0.2 * i, 0.0, 0.1), bevel=0.002))
    parts.append(part(ig.cylinder("Mute", 0.008, 0.035, location=(0.232, -0.005, 0.045), radius_top=0.004,
                                  rotation=(0.0, math.pi * 0.5, 0.0), material=rubber)))
    ig.build_asset("SM_ToolPouch", "prop", parts, OUT, collision_parts=None, texture_size=1024, preview_yaw=30,
                   notes="오빠의 말아 둔 천 공구 주머니. 길이 X 45 cm, 지름 11 cm. 끝에 펠트와 고무 쐐기가 비어져 나왔다.")


def build_glove_pouch():
    nylon = flat("GloveNylon", (0.015, 0.022, 0.045), 0.58)
    zip_ = flat("GloveZip", (0.02, 0.02, 0.02), 0.4, metallic=0.4)
    glove = tiled("GloveKnit", "T_CotCanvas", 0.08, tint=(0.40, 0.40, 0.42))
    rubber = flat("GloveDip", (0.55, 0.06, 0.04), 0.6)
    parts = [lumpy("Pouch", (0.24, 0.13, 0.09), (0.0, 0.0, 0.045), nylon, sag=0.05, bulge=0.05, seed=2, levels=2,
                   bevel=0.02)]
    parts.append(tube("Zip", [(-0.11, -0.03, 0.088), (0.0, -0.035, 0.092), (0.11, -0.03, 0.088)], 0.003, zip_))
    # 지퍼 틈으로 나온 코팅 장갑 손목 둘.
    parts.append(lumpy("Cuff1", (0.07, 0.05, 0.03), (0.09, -0.02, 0.10), glove, sag=0.0, bulge=0.04, seed=5))
    parts.append(lumpy("Palm1", (0.05, 0.04, 0.02), (0.13, -0.025, 0.105), rubber, sag=0.0, bulge=0.04, seed=6))
    ig.build_asset("SM_GlovePouch", "prop", parts, OUT, collision_parts=None, texture_size=512, preview_yaw=30,
                   notes="정우의 겨울 장비 주머니(남색 나일론, 코팅 장갑). 24x13x9 cm.")


# --------------------------------------------------------------------------
# 5층 배관 점검 자리, 302호 마감판, 쐐기
# --------------------------------------------------------------------------

def build_overlay_board():
    gypsum = tiled("OverlayGypsum", "T_MissingFloorDryPlaster", 0.9)
    screw = flat("OverlayScrew", (0.30, 0.30, 0.29), 0.35, metallic=0.9)
    silicone = flat("OverlaySilicone", (0.50, 0.49, 0.46), 0.4)
    membrane = tiled("OverlayMembrane", "T_LiningMembrane", 0.5, normal_strength=0.7)
    # 원점은 판 가운데 뒷면(벽 면). 앞면 +X. 가로 Y 80, 세로 Z 90.
    board = ig.box("Board", (0.0125, 0.80, 0.90), location=(0.00625, 0.0, 0.0), bevel=0.0015, material=gypsum)
    parts = [part(board)]
    for y in (-0.37, 0.37):
        for z in (-0.42, 0.0, 0.42):
            parts.append(part(ig.cylinder(f"Screw{y:+.2f}{z:+.2f}", 0.0045, 0.0015, location=(0.0133, y, z),
                                          rotation=(0.0, math.pi * 0.5, 0.0), segments=12, material=screw)))
    # 판 둘레를 실리콘으로 쳐 바르고, 아래 변으로 방수막이 비어져 나왔다.
    parts.append(tube("SiliconeTop", [(0.006, -0.40, 0.452), (0.008, 0.0, 0.455), (0.006, 0.40, 0.451)], 0.006, silicone))
    parts.append(tube("SiliconeL", [(0.006, -0.402, 0.45), (0.008, -0.404, 0.0), (0.006, -0.402, -0.45)], 0.006, silicone))
    parts.append(bx("MembraneTail", (0.002, 0.34, 0.05), (0.0145, 0.10, -0.47), membrane, rotation=(0.0, 0.0, 0.03)))
    ig.build_asset("SM_InspectionOverlayBoard", "prop", parts, OUT, collision_parts=[[board]], texture_size=1024,
                   preview_yaw=90, origin="back-center",
                   notes=("5층 배관 점검 자리를 덮어 박은 석고 덧판 80x90. 원점이 판 가운데 뒷면(벽 면), 앞면 +X. "
                          "나사 여섯과 둘레 실리콘."))


def build_bent_shield():
    sheet = ig.mat_metal("ShieldGalv", color=(0.66, 0.67, 0.66), roughness=0.42)
    parts = []
    # 평면도로 ㄱ자. 원점 바닥, 한 날개는 +Y로 45 cm, 다른 날개는 +X로 30 cm, 높이 140.
    a = ig.box("WingA", (0.0012, 0.45, 1.40), location=(0.0, 0.225, 0.70), material=sheet)
    b = ig.box("WingB", (0.30, 0.0012, 1.40), location=(0.15, 0.45, 0.70), material=sheet)
    for ob in (a, b):
        sub = ob.modifiers.new("Sub", "SUBSURF")
        sub.subdivision_type = "SIMPLE"
        sub.levels = 3
        ig.apply_modifiers(ob)
    rnd = random.Random(9)
    for v in a.data.vertices:
        v.co.x += 0.012 * math.sin(v.co.z * 7.0 + 0.4) * math.sin(v.co.y * 5.0) + rnd.uniform(-0.001, 0.001)
    parts += [part(a), part(b)]
    for z in (0.15, 0.70, 1.25):
        parts.append(part(ig.cylinder(f"Rivet{z:.2f}", 0.004, 0.003, location=(-0.0015, 0.02, z),
                                      rotation=(0.0, math.pi * 0.5, 0.0), segments=10, material=sheet)))
    ig.build_asset("SM_BentShieldSheet", "prop", parts, OUT, collision_parts=[[a], [b]], texture_size=512,
                   preview_yaw=45,
                   notes="배관 점검 칸을 큰 공간과 가르는 ㄱ자 아연 판. 원점 바닥 모서리, 날개 +Y 45와 +X 30, 높이 140.")


def build_finish_board():
    face = tiled("FinishFace", "T_ApartmentWallpaperV2", 0.53)
    back = tiled("FinishBack", "T_WorkbenchPlywood", 0.6)
    edge = flat("FinishEdge", (0.55, 0.47, 0.36), 0.7)
    # 원점 바닥 가운데, 앞면 -Y(벽지), 뒷면 +Y(합판). 가로 X 100, 세로 Z 180, 두께 1.5 cm.
    core = ig.box("Core", (1.0, 0.012, 1.8), location=(0.0, 0.0, 0.9), bevel=0.001, material=back)
    paper = ig.box("Paper", (0.996, 0.0012, 1.796), location=(0.0, -0.0066, 0.9), material=face)
    parts = [part(core), part(paper)]
    parts.append(bx("EdgeTop", (1.0, 0.0135, 0.002), (0.0, -0.0006, 1.801), edge))
    ig.build_asset("SM_FinishBoard", "prop", parts, OUT, collision_parts=[[core]], texture_size=1024,
                   notes="302호에 기대 둔 벽지 마감판 100x180, 두께 1.5 cm. 원점 바닥 가운데, 앞면 -Y.")


def build_wedge():
    wood = tiled("WedgeWood", "T_WorkbenchPlywood", 0.15)
    # 원점 바닥 가운데. 두꺼운 끝이 +X(5 cm), 얇은 끝 -X(0.5 cm), 길이 22, 폭 6.
    bm = bmesh.new()
    pts = [(-0.11, -0.03, 0.0), (0.11, -0.03, 0.0), (0.11, 0.03, 0.0), (-0.11, 0.03, 0.0),
           (-0.11, -0.03, 0.005), (0.11, -0.03, 0.05), (0.11, 0.03, 0.05), (-0.11, 0.03, 0.005)]
    vs = [bm.verts.new(p) for p in pts]
    for f in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        bm.faces.new([vs[i] for i in f])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new("Wedge")
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("Wedge", me)
    bpy.context.scene.collection.objects.link(ob)
    ig.assign_material(ob, wood)
    ig.add_bevel(ob, 0.002)
    ig.build_asset("SM_TimberWedge", "prop", [part(ob)], OUT, collision_parts=None, texture_size=256,
                   notes="공사 자재 나무 쐐기. 길이 X 22(두꺼운 끝 +X 5 cm), 폭 6. 원점 바닥 가운데.")


# --------------------------------------------------------------------------
# 글자 인쇄(Scripts/build_leftover_prints.py가 만든 PNG). 앞면 -Y, mirror_print_uv.
# --------------------------------------------------------------------------

PRINTS = os.path.join(SOURCE_ART, "LeftoverPrints")
PAPER_T = 0.0002


def print_quad(asset, image, size, center, notes, rotation=(0.0, 0.0, 0.0), roughness=0.86):
    ink = ig.mat_image_uv(f"{asset}Ink", os.path.join(PRINTS, image), roughness=roughness)
    back = flat(f"{asset}Back", (0.62, 0.58, 0.50), 0.9)
    quad = ig.image_quad("print", size, center, ink, rotation=rotation, thickness=PAPER_T)
    quad.data.materials.append(back)
    for face in quad.data.polygons:
        if face.normal.y > -0.5:
            face.material_index = 1
    ig.build_asset(asset, "prop", [quad], OUT, collision_parts=[], texture_size=1024, mirror_print_uv=True,
                   origin="box-bottom-center", notes=notes)


def build_labels():
    # SM_StorageBox의 빈 이름표(앞면 -Y, 가운데 X 0.12, Z 0.187, 16x9 cm) 바로 앞에 붙는다.
    print_quad("SM_LeftoverLabelJeongwoo", "LeftoverLabelJeongwoo.png", (0.16, 0.09), (0.12, -0.1934, 0.187),
               "상자 이름표 「202 정우 / 겨울 장비」와 빨간 「주인 없음」 딱지. SM_StorageBox와 같은 원점에 둔다.")
    ig.reset_scene()
    print_quad("SM_LeftoverLabelDoha", "LeftoverLabelDoha.png", (0.16, 0.09), (0.12, -0.1934, 0.187),
               "오빠 글씨의 상자 이름표 「백도하 / 작업 공구」. SM_StorageBox와 같은 원점에 둔다.")
    ig.reset_scene()
    print_quad("SM_LeftoverLabelMarked", "LeftoverLabelMarked.png", (0.20, 0.05), (-0.09, -0.1936, 0.285),
               "정우가 오빠 상자 앞면 위쪽에 붙인 종이테이프 「버리지 마세요」. SM_StorageBox와 같은 원점.",
               rotation=(0.0, math.radians(-4.0), 0.0), roughness=0.6)
    ig.reset_scene()
    # 수거 안내 A4. 원점은 종이 아래 가운데(벽 면), 앞면 -Y.
    print_quad("SM_CollectionNoticePrint", "LeftoverCollectionNotice.png", (0.21, 0.297), (0.0, -0.0004, 0.1485),
               "1층 게시판 옆 「대형 폐기물 수거 안내」 A4. 수거일을 볼펜으로 긋고 손글씨로 고쳤다. 원점 종이 아래 가운데.")


def build_labels_303():
    # 302호 문간 상자 더미의 303호 김치통 상자. 이름표는 다른 상자와 같은 자리, 딱지는 그 위 글자를 가린다.
    print_quad("SM_LeftoverLabel303", "LeftoverLabel303.png", (0.16, 0.09), (0.12, -0.1934, 0.187),
               "303호가 써 붙인 상자 이름표 「303 김치통 / 겨울까지만 부탁드려요」. SM_StorageBox와 같은 원점.")
    ig.reset_scene()
    print_quad("SM_LeftoverStickerUnclaimed", "LeftoverStickerUnclaimed.png", (0.08, 0.03), (0.12, -0.1938, 0.205),
               "관리인이 덧붙인 빨간 「주인 없음」 딱지. 303호 이름표 글자를 가린다. 들추면 숨긴다. SM_StorageBox와 같은 원점.",
               rotation=(0.0, math.radians(-7.0), 0.0), roughness=0.5)


if __name__ == "__main__":
    only = os.environ.get("IG_ONLY", "")
    builders = {}
    for key in HATCH_SIZES:
        builders[f"SM_AccessHatchFrame{key}"] = (lambda k=key: build_hatch_frame(k))
        builders[f"SM_AccessHatchDoor{key}"] = (lambda k=key: build_hatch_door(k))
    builders["SM_HatchSealPlug"] = build_seal_plug
    for key in ("A", "B", "C"):
        builders[f"SM_CoverPiece{key}"] = (lambda k=key: build_cover_piece(k))
    for key in ("A", "B"):
        builders[f"SM_CoverFlap{key}"] = (lambda k=key: build_cover_flap(k))
    builders["SM_CoverFrame"] = build_cover_frame
    builders["SM_CoverClamp"] = build_cover_clamp
    builders["SM_ToolPouch"] = build_tool_pouch
    builders["SM_GlovePouch"] = build_glove_pouch
    builders["SM_InspectionOverlayBoard"] = build_overlay_board
    builders["SM_BentShieldSheet"] = build_bent_shield
    builders["SM_FinishBoard"] = build_finish_board
    builders["SM_TimberWedge"] = build_wedge
    builders["SM_LeftoverLabelJeongwoo"] = build_labels
    builders["SM_LeftoverLabel303"] = build_labels_303
    for name, builder in builders.items():
        if only and name not in only.split(","):
            continue
        ig.reset_scene()
        builder()
