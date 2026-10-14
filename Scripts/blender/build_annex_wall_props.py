"""밤4 5층 시신 벽을 뜯는 단계의 소품(2026-10-14).

목한수가 시신 벽 칸(가로 120, 높이 240) 위에 한 겹 더 댄 것들과 그걸 떼어 낼 때 드러나는 것들,
덧댄 것의 무릎 마디를 거는 철골 틀, 잘못 친 자리에 남는 망치 자국.

- SM_AnnexOverlayTop: 칸 위쪽에 덧댄 석고 덧판(106x53). 앞면은 시안의 덧판 면을 펴서 굽는다.
- SM_AnnexJointTape: 덧판 아래 이음을 덮은 테이프와 퍼티 띠. 첫째 단계에 뜯긴다.
- SM_AnnexJointTorn: 뜯기고 남은 테이프 조각, 드러난 덧판 나사 다섯, 이음 사이의 검은 접착층.
- SM_AnnexBatten: 칸 양옆에 세로로 박은 각재(4.5x236). 시안의 각재 면(30 cm마다 나사)을 굽는다.
- SM_AnnexInnerFixings: 바깥층을 떼면 드러나는 안쪽 판 나사 두 줄과 각재 자국, 이음에 쑤셔 넣은 방음재.
- SM_AnnexAngleClip: 안쪽 판 위 가운데를 천장에 물린 ㄱ자 철물. 마지막에 친다.
- SM_AnnexBraceFrame: 공사하고 남은 경량 철골 틀(ㄷ자 스터드 둘과 위아래 런너, 60x238).
- SM_HammerDent: 고정되지 않은 자리를 쳤을 때 남는 얕은 자국과 들뜬 종이.

형태는 Content/SourceArt/AI의 AnnexWallLayers_20261014·SteelBraceFrame_20261014 시안을 보고 잡았다.
면 그림은 Scripts/crop_annex_wall_textures.py가 시안에서 잘라 Content/SourceArt/AnnexWall에 둔다.

    blender -b --factory-startup --python Scripts/blender/build_annex_wall_props.py
    (IG_ONLY=SM_AnnexBraceFrame,... 로 일부만)

좌표는 UE 기준 미터다. 벽에 붙는 것은 원점이 벽 면(뒷면), 앞면이 +X다. 게임에서는 yaw 180으로
동쪽 벽(X 250)에 붙여 앞면이 방(-X)을 본다. 그러면 로컬 +Y가 월드 -Y가 된다.
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

import ig_blender_lib as ig  # noqa: E402
from build_inner_room import bx, flat, part, tiled, tube  # noqa: E402

SOURCE_ART = os.path.abspath(os.path.join(HERE, "..", "..", "Content", "SourceArt"))
WALL_ART = os.path.join(SOURCE_ART, "AnnexWall")
OUT = ig.out_root_from_argv()

# 시신 벽 칸. 각재 안쪽 폭 106, 덧판 높이 53, 이음은 덧판 아래 변(로컬 Z 0).
INNER_W = 1.06
OVERLAY_H = 0.53
OVERLAY_T = 0.0125
BATTEN_W, BATTEN_T, BATTEN_H = 0.045, 0.025, 2.36


def image_front_uv(ob, width, z0, z1, y_center=0.0):
    """앞면(+X를 보는 면)에 그림 한 장을 0..1로 편다. 방에서 앞면을 보면 화면 오른쪽이 -Y다.
    다른 면은 월드 투영 그대로 둔다(테두리 재질이라 그림을 안 쓴다)."""
    ig.apply_modifiers(ob)
    bpy.context.view_layer.update()
    me = ob.data
    me.transform(ob.matrix_world)
    ob.matrix_world.identity()
    layer = me.uv_layers.get("ImageUV") or me.uv_layers.new(name="ImageUV")
    for poly in me.polygons:
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            layer.data[li].uv = ((width * 0.5 - (co.y - y_center)) / width, (co.z - z0) / (z1 - z0))
    return ob


def split_front(ob, face_mat, edge_mat, threshold=0.5):
    """앞면만 face_mat, 나머지는 edge_mat."""
    me = ob.data
    me.materials.clear()
    me.materials.append(edge_mat)
    me.materials.append(face_mat)
    for poly in me.polygons:
        poly.material_index = 1 if poly.normal.x > threshold else 0
    return ob


def extrude_xz(name, points, y0, y1, mat):
    """XZ 평면 단면을 Y로 y0..y1 만큼 민다(띠처럼 꺾인 테이프 단면)."""
    bm = bmesh.new()
    left = [bm.verts.new((x, y0, z)) for x, z in points]
    right = [bm.verts.new((x, y1, z)) for x, z in points]
    bm.faces.new(left)
    bm.faces.new(list(reversed(right)))
    n = len(points)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((left[i], left[j], right[j], right[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    ig.assign_material(ob, mat)
    return ob


def plate_yz(name, outline, x0, x1, mat):
    """YZ 평면 외곽선을 X로 민 얇은 조각(찢긴 테이프)."""
    bm = bmesh.new()
    back = [bm.verts.new((x0, y, z)) for y, z in outline]
    front = [bm.verts.new((x1, y, z)) for y, z in outline]
    bm.faces.new(back)
    bm.faces.new(list(reversed(front)))
    n = len(outline)
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
    return ob


def screw_head(name, location, mat, recess, radius=0.0042, rust=None, angle=0.0):
    """나팔머리 석고 나사. 축은 X, 머리 위 + 홈. 원점 쪽 면이 판에 묻힌다."""
    parts = [part(ig.cylinder(name, radius, 0.0018, location=(location[0] + 0.0009, location[1], location[2]),
                              rotation=(0.0, math.pi * 0.5, 0.0), segments=14, material=rust or mat))]
    for k, rot in enumerate((angle, angle + math.pi * 0.5)):
        parts.append(bx(f"{name}Slot{k}", (0.0006, radius * 1.3, 0.0008),
                        (location[0] + 0.0019, location[1], location[2]), recess, rotation=(rot, 0.0, 0.0)))
    return parts


def gypsum_edge():
    return flat("GypsumCore", (0.78, 0.76, 0.70), 0.9)


# --------------------------------------------------------------------------
# 바깥층
# --------------------------------------------------------------------------

def build_overlay_top():
    face = ig.mat_image_uv("AnnexOverlayFaceImg", os.path.join(WALL_ART, "AnnexOverlayFace.png"), roughness=0.84)
    edge = gypsum_edge()
    # 원점은 덧판 아래 변 가운데 뒷면(이음 자리). 위로 53, 앞면 +X.
    board = ig.box("Board", (OVERLAY_T, INNER_W, OVERLAY_H), location=(OVERLAY_T * 0.5, 0.0, OVERLAY_H * 0.5),
                   bevel=0.0012)
    image_front_uv(board, INNER_W, 0.0, OVERLAY_H)
    split_front(board, face, edge)
    ig.build_asset("SM_AnnexOverlayTop", "prop", [board], OUT, collision_parts=[[board]], texture_size=1024,
                   preview_yaw=270, origin="back-bottom-center",
                   notes=("시신 벽 칸 위쪽에 덧댄 석고 덧판 106x53x1.25. 원점이 아래 변 가운데 뒷면(이음 자리), "
                          "앞면 +X. 셋째 단계에 바깥층과 같이 떨어진다."))


def build_joint_tape():
    band = ig.mat_image_uv("AnnexJointBandImg", os.path.join(WALL_ART, "AnnexJointBand.png"), roughness=0.78)
    # 단면(X, Z): 덧판 앞면(1.25 cm)에서 퍼티가 비스듬히 벽 면까지 내려온다. 띠 높이 14 cm.
    t = 0.0007
    upper, lower = 0.07, -0.07
    outline = [(OVERLAY_T + t, upper), (OVERLAY_T + t, 0.006), (t + 0.0015, -0.012), (t, lower),
               (0.0, lower), (0.0015, -0.012), (OVERLAY_T, 0.006), (OVERLAY_T, upper)]
    strip = extrude_xz("Band", outline, -INNER_W * 0.5, INNER_W * 0.5, band)
    image_front_uv(strip, INNER_W, lower, upper)
    ig.build_asset("SM_AnnexJointTape", "prop", [strip], OUT, collision_parts=[], texture_size=1024,
                   preview_yaw=270, origin="joint-center",
                   notes=("덧판 아래 이음을 덮은 종이 테이프와 갈라진 퍼티(높이 14). 원점이 이음 가운데 벽 면. "
                          "첫째 단계에서 뜯겨 SM_AnnexJointTorn으로 바뀐다."))


def build_joint_torn():
    band = ig.mat_image_uv("AnnexTornBandImg", os.path.join(WALL_ART, "AnnexJointBand.png"), roughness=0.8)
    glue = tiled("AnnexSeamGlue", "T_LiningMembrane", 0.35, normal_strength=0.6)
    foam = tiled("AnnexSeamFoam", "T_LiningFoam", 0.25, normal_strength=0.6)
    screw = flat("AnnexOuterScrew", (0.05, 0.05, 0.05), 0.42, metallic=0.6)
    rust = flat("AnnexOuterScrewRust", (0.20, 0.09, 0.04), 0.7, metallic=0.2)
    recess = flat("AnnexScrewRecess", (0.01, 0.01, 0.01), 0.6)
    parts = []
    # 덧판 아래 변과 벽 사이 틈. 검은 접착층이 비어져 나오고 사이사이 누런 방음재.
    parts.append(bx("Glue", (0.004, INNER_W - 0.02, 0.010), (0.002, 0.0, -0.004), glue))
    rnd = random.Random(14)
    for k in range(7):
        y = -0.46 + k * 0.155 + rnd.uniform(-0.03, 0.03)
        parts.append(bx(f"Foam{k}", (0.006, rnd.uniform(0.03, 0.07), 0.008), (0.003, y, -0.003), foam,
                        rotation=(rnd.uniform(-0.2, 0.2), 0.0, 0.0)))
    # 드러난 덧판 나사(바깥 고정점). 덧판 아래 변 1.4 cm 위.
    for k, y in enumerate((-0.45, -0.22, 0.0, 0.22, 0.45)):
        parts += screw_head(f"Screw{k}", (OVERLAY_T, y, 0.014), screw, recess,
                            rust=rust if k in (1, 4) else None, angle=0.3 * k)
    # 남은 테이프 조각. 덧판 앞면 아래에 붙은 위쪽 반만 남았다. 가운데 하나는 반쯤 떨어져 늘어졌다.
    pieces = [
        [(-0.53, 0.07), (-0.38, 0.07), (-0.36, 0.035), (-0.41, 0.012), (-0.47, 0.024), (-0.53, 0.006)],
        [(0.18, 0.07), (0.53, 0.07), (0.53, 0.004), (0.44, 0.02), (0.36, 0.009), (0.27, 0.03), (0.20, 0.018)],
    ]
    for k, outline in enumerate(pieces):
        ob = plate_yz(f"Tape{k}", outline, OVERLAY_T, OVERLAY_T + 0.0007, band)
        image_front_uv(ob, INNER_W, -0.07, 0.07)
        parts.append(ob)
    flap = plate_yz("Flap", [(-0.12, 0.0), (0.10, 0.0), (0.08, -0.06), (0.02, -0.075), (-0.05, -0.05), (-0.11, -0.068)],
                    0.0, 0.0007, band)
    image_front_uv(flap, INNER_W, -0.07, 0.07)
    flap.location = (OVERLAY_T + 0.0008, 0.0, 0.004)
    flap.rotation_euler = (0.0, math.radians(-38.0), 0.0)
    parts.append(part(flap))
    ig.build_asset("SM_AnnexJointTorn", "prop", parts, OUT, collision_parts=[], texture_size=1024,
                   preview_yaw=270, origin="joint-center",
                   notes=("이음 테이프를 뜯은 뒤. 남은 조각, 늘어진 한 조각, 드러난 덧판 나사 다섯(바깥 고정점), "
                          "이음 사이 검은 접착층과 방음재. 원점이 이음 가운데 벽 면."))


def build_batten():
    face = ig.mat_image_uv("AnnexBattenFaceImg", os.path.join(WALL_ART, "AnnexBattenFace.png"), roughness=0.7)
    side = flat("AnnexBattenSide", (0.60, 0.47, 0.30), 0.75)
    # 원점이 가운데 뒷면. 앞면 +X, 세로 Z ±1.18.
    wood = ig.box("Batten", (BATTEN_T, BATTEN_W, BATTEN_H), location=(BATTEN_T * 0.5, 0.0, 0.0), bevel=0.002)
    image_front_uv(wood, BATTEN_W, -BATTEN_H * 0.5, BATTEN_H * 0.5)
    split_front(wood, face, side)
    ig.build_asset("SM_AnnexBatten", "prop", [wood], OUT, collision_parts=[[wood]], texture_size=512,
                   preview_yaw=270, origin="back-center",
                   notes="시신 벽 칸 양옆에 세로로 박은 각재 4.5x2.5x236. 30 cm마다 나사. 원점 가운데 뒷면, 앞면 +X.")


# --------------------------------------------------------------------------
# 안쪽 판
# --------------------------------------------------------------------------

def build_inner_fixings():
    ghost = tiled("AnnexBattenGhost", "T_MissingFloorDryPlaster", 0.6, tint=(1.06, 1.05, 1.02))
    screw = flat("AnnexInnerScrew", (0.07, 0.07, 0.068), 0.4, metallic=0.7)
    rust = flat("AnnexInnerScrewRust", (0.24, 0.11, 0.05), 0.72, metallic=0.2)
    recess = flat("AnnexInnerRecess", (0.01, 0.01, 0.01), 0.6)
    hole = flat("AnnexBattenHole", (0.12, 0.11, 0.10), 0.85)
    glue = tiled("AnnexInnerGlue", "T_LiningMembrane", 0.35, normal_strength=0.6)
    foam = tiled("AnnexInnerFoam", "T_LiningFoam", 0.25, normal_strength=0.6)
    parts = []
    # 원점은 칸 가운데 벽 면(높이 1320). 각재가 있던 자리(Y ±0.555)는 칠이 덜 바랬다.
    for side in (-1.0, 1.0):
        y = side * 0.555
        parts.append(bx(f"Ghost{side:+.0f}", (0.0004, BATTEN_W, BATTEN_H - 0.02), (0.0002, y, 0.0), ghost))
        # 각재를 박았던 나사 구멍.
        for k in range(8):
            z = -1.05 + k * 0.30
            parts.append(part(ig.cylinder(f"Hole{side:+.0f}{k}", 0.0026, 0.0006, location=(0.0005, y, z),
                                          rotation=(0.0, math.pi * 0.5, 0.0), segments=10, material=hole)))
        # 안쪽 판을 기둥에 박은 나사(안쪽 고정점). 각재 구멍 사이에 하나씩.
        for k in range(7):
            z = -0.90 + k * 0.30
            parts += screw_head(f"Inner{side:+.0f}{k}", (0.0, y + side * 0.012, z), screw, recess,
                                rust=rust if (k + int(side)) % 3 == 0 else None, angle=0.5 * k)
    # 덧판이 덮고 있던 이음(높이 1383, 로컬 +0.63). 검은 접착층이 눌려 번졌다.
    parts.append(bx("SeamGlue", (0.003, INNER_W - 0.04, 0.012), (0.0015, 0.0, 0.629), glue))
    rnd = random.Random(31)
    for k in range(9):
        y = -0.48 + k * 0.12 + rnd.uniform(-0.02, 0.02)
        parts.append(bx(f"SeamFoam{k}", (0.007, rnd.uniform(0.025, 0.06), 0.01), (0.0035, y, 0.63), foam,
                        rotation=(rnd.uniform(-0.25, 0.25), 0.0, 0.0)))
    ig.build_asset("SM_AnnexInnerFixings", "prop", parts, OUT, collision_parts=[], texture_size=1024,
                   preview_yaw=270, origin="bay-center",
                   notes=("바깥층을 떼면 드러나는 안쪽 판. 각재 자국 둘과 그 사이의 나사 두 줄(안쪽 고정점), "
                          "각재 나사 구멍, 이음의 접착층과 방음재. 원점이 칸 가운데 벽 면(월드 높이 1320)."))


def build_angle_clip():
    steel = ig.mat_metal("AnnexClipGalv", color=(0.62, 0.63, 0.62), roughness=0.46)
    screw = flat("AnnexClipScrew", (0.07, 0.07, 0.068), 0.4, metallic=0.7)
    recess = flat("AnnexClipRecess", (0.01, 0.01, 0.01), 0.6)
    parts = []
    # 원점은 벽 면과 천장이 만나는 모서리. 세운 날개는 벽에(아래로 7 cm), 누운 날개는 천장에(앞으로 5 cm).
    t = 0.0022
    parts.append(bx("Wall", (t, 0.06, 0.07), (t * 0.5, 0.0, -0.035), steel, bevel=0.0006))
    parts.append(bx("Ceiling", (0.05, 0.06, t), (0.025, 0.0, -t * 0.5), steel, bevel=0.0006))
    parts.append(bx("Rib", (0.011, 0.004, 0.011), (0.0075, 0.0, -0.0075), steel, rotation=(0.0, math.radians(45.0), 0.0)))
    for k, z in enumerate((-0.022, -0.052)):
        parts += screw_head(f"WallScrew{k}", (t, -0.015 + 0.03 * k, z), screw, recess, radius=0.0038, angle=0.7 * k)
    ig.build_asset("SM_AnnexAngleClip", "prop", parts, OUT, collision_parts=[], texture_size=256,
                   preview_yaw=240, origin="wall-ceiling-corner",
                   notes=("안쪽 판 위 가운데를 천장에 물린 ㄱ자 아연 철물(6x7x5). 원점이 벽 면과 천장의 모서리, "
                          "앞 +X. 마지막 단계에 친다."))


# --------------------------------------------------------------------------
# 철골 틀, 망치 자국
# --------------------------------------------------------------------------

def c_channel(name, length, depth, flange, t, mat, axis="z"):
    """ㄷ자 단면. 웹이 X로 depth, 날개 둘이 +Y로 flange. axis 방향으로 length."""
    parts = []
    if axis == "z":
        parts.append(bx(f"{name}Web", (depth, t, length), (0.0, 0.0, 0.0), mat))
        for side in (-1.0, 1.0):
            parts.append(bx(f"{name}Fl{side:+.0f}", (t, flange, length), (side * (depth * 0.5 - t * 0.5), flange * 0.5, 0.0), mat))
    else:
        parts.append(bx(f"{name}Web", (depth, length, t), (0.0, 0.0, 0.0), mat))
        for side in (-1.0, 1.0):
            parts.append(bx(f"{name}Fl{side:+.0f}", (t, length, flange), (side * (depth * 0.5 - t * 0.5), 0.0, flange * 0.5), mat))
    return parts


def build_brace_frame():
    galv = ig.mat_metal("BraceGalv", color=(0.58, 0.60, 0.59), roughness=0.5)
    screw = flat("BraceScrew", (0.30, 0.30, 0.29), 0.35, metallic=0.9)
    tape = flat("BraceTape", (0.30, 0.31, 0.32), 0.55)
    # 현장에서 쓰다 남은 자재 표시. 손전등에 비친 다리 더미 속에서도 틀이 따로 읽힌다.
    hazard_yellow = flat("BraceHazardYellow", (0.78, 0.60, 0.05), 0.6)
    hazard_black = flat("BraceHazardBlack", (0.03, 0.03, 0.03), 0.6)
    paint = flat("BraceOrangePaint", (0.85, 0.28, 0.04), 0.7)
    parts = []
    # 원점은 바닥 가운데. 틀은 YZ 평면(폭 Y 60, 높이 238), 깊이 X 7.5.
    width, height, depth, t = 0.60, 2.38, 0.075, 0.0016
    for side in (-1.0, 1.0):
        y = side * (width * 0.5 - 0.004)
        for ob in c_channel(f"Stud{side:+.0f}", height - 0.012, depth, 0.034, t, galv):
            ob.data.transform(ob.matrix_world)
            ob.matrix_world.identity()
            # 날개가 틀 안쪽을 보게 돌린다.
            if side > 0:
                for v in ob.data.vertices:
                    v.co.y = -v.co.y
            for v in ob.data.vertices:
                v.co.y += y
                v.co.z += height * 0.5
            parts.append(part(ob))
    for z in (0.0, height):
        for ob in c_channel(f"Track{z:.1f}", width + 0.004, depth + 0.004, 0.03, t, galv, axis="y"):
            ob.data.transform(ob.matrix_world)
            ob.matrix_world.identity()
            if z > 0:
                for v in ob.data.vertices:
                    v.co.z = -v.co.z
            for v in ob.data.vertices:
                v.co.z += z
            parts.append(part(ob))
    # 모서리 피스.
    for side in (-1.0, 1.0):
        for z in (0.015, height - 0.015):
            for x in (-depth * 0.5 - 0.0008, depth * 0.5 + 0.0008):
                parts.append(part(ig.cylinder(f"Fix{side:+.0f}{z:.2f}{x:+.3f}", 0.0036, 0.002,
                                              location=(x, side * (width * 0.5 - 0.02), z),
                                              rotation=(0.0, math.pi * 0.5, 0.0), segments=10, material=screw)))
    # 한쪽 스터드에 감은 회색 테이프 조각.
    parts.append(bx("Tape", (depth + 0.006, 0.05, 0.06), (0.0, -width * 0.5 + 0.02, 1.05), tape))
    # 양쪽 스터드 허리에 감은 노랑·검정 띠(줄무늬 여섯)와 위 런너 끝의 주황 스프레이 표시.
    for side in (-1.0, 1.0):
        y = side * (width * 0.5 - 0.02)
        for k in range(6):
            mat = hazard_yellow if k % 2 == 0 else hazard_black
            parts.append(bx(f"Hazard{side:+.0f}{k}", (depth + 0.008, 0.046, 0.05),
                            (0.0, y, 1.45 + k * 0.05), mat))
    parts.append(bx("PaintMark", (0.004, 0.18, 0.05), (depth * 0.5 + 0.004, 0.0, height - 0.02), paint))
    parts.append(bx("PaintMarkBack", (0.004, 0.18, 0.05), (-depth * 0.5 - 0.004, 0.0, height - 0.02), paint))
    # 위쪽 날개 하나가 살짝 휘었다.
    bent = bx("BentLip", (0.0016, 0.03, 0.22), (depth * 0.5 - 0.0008, width * 0.5 - 0.022, height - 0.30), galv,
              rotation=(0.0, 0.0, math.radians(14.0)))
    parts.append(bent)
    ig.build_asset("SM_AnnexBraceFrame", "prop", parts, OUT, collision_parts=[parts[:3] + parts[3:6]],
                   texture_size=512, preview_yaw=60, origin="bottom-center",
                   notes=("공사하고 남은 경량 철골 틀 60x238(ㄷ자 스터드 둘, 위아래 런너). 원점 바닥 가운데, "
                          "틀 면은 YZ, 깊이 X 7.5. 위 런너가 덧댄 것의 무릎 마디를 건다."))


def build_hammer_dent():
    paint = tiled("DentPaint", "T_MissingFloorDryPlaster", 0.6)
    core = flat("DentCore", (0.66, 0.64, 0.58), 0.95)
    parts = []
    # 원점이 벽 면 가운데. 오목하게 눌린 자리(앞면 +X), 가운데 석고 심이 드러났다.
    bm = bmesh.new()
    rings, segs = 5, 24
    rnd = random.Random(5)
    verts = []
    for r in range(rings + 1):
        radius = 0.034 * r / rings
        row = []
        for s in range(segs):
            a = 2.0 * math.pi * s / segs
            wobble = 1.0 + (rnd.uniform(-0.18, 0.18) if r == rings else rnd.uniform(-0.06, 0.06))
            depth = -0.004 * (1.0 - (r / rings) ** 2)
            row.append(bm.verts.new((0.0006 + depth, math.cos(a) * radius * wobble, math.sin(a) * radius * wobble)))
        verts.append(row)
    for r in range(rings):
        for s in range(segs):
            n = (s + 1) % segs
            bm.faces.new((verts[r][s], verts[r][n], verts[r + 1][n], verts[r + 1][s]))
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    me = bpy.data.meshes.new("Dent")
    bm.to_mesh(me)
    bm.free()
    dent = bpy.data.objects.new("Dent", me)
    bpy.context.scene.collection.objects.link(dent)
    ig.assign_material(dent, core)
    parts.append(part(dent))
    # 들뜬 종이 조각과 금 셋.
    for k in range(5):
        a = 2.0 * math.pi * k / 5 + rnd.uniform(-0.3, 0.3)
        r = 0.036 + rnd.uniform(0.0, 0.01)
        parts.append(bx(f"Flake{k}", (0.0012, rnd.uniform(0.012, 0.02), rnd.uniform(0.006, 0.012)),
                        (0.0012, math.cos(a) * r, math.sin(a) * r), paint,
                        rotation=(a, rnd.uniform(-0.3, 0.1), 0.0)))
    crack = flat("DentCrack", (0.42, 0.41, 0.38), 0.9)
    for k in range(3):
        a = 2.0 * math.pi * k / 3 + 0.4
        length = rnd.uniform(0.014, 0.024)
        # 벽 면에 붙은 가는 금. 몸통 길이 방향을 반지름 방향(YZ 평면의 a)으로 눕힌다.
        parts.append(bx(f"Crack{k}", (0.0003, length, 0.0011), (0.0002, math.cos(a) * (0.033 + length * 0.5),
                                                                math.sin(a) * (0.033 + length * 0.5)),
                        crack, rotation=(a, 0.0, 0.0)))
    ig.build_asset("SM_HammerDent", "prop", parts, OUT, collision_parts=[], texture_size=256, preview_yaw=270,
                   origin="wall-center",
                   notes="고정되지 않은 자리를 망치로 쳤을 때 남는 얕은 자국(지름 7). 원점이 벽 면 가운데, 앞면 +X.")


if __name__ == "__main__":
    only = os.environ.get("IG_ONLY", "")
    builders = {
        "SM_AnnexOverlayTop": build_overlay_top,
        "SM_AnnexJointTape": build_joint_tape,
        "SM_AnnexJointTorn": build_joint_torn,
        "SM_AnnexBatten": build_batten,
        "SM_AnnexInnerFixings": build_inner_fixings,
        "SM_AnnexAngleClip": build_angle_clip,
        "SM_AnnexBraceFrame": build_brace_frame,
        "SM_HammerDent": build_hammer_dent,
    }
    for name, builder in builders.items():
        if only and name not in only.split(","):
            continue
        ig.reset_scene()
        builder()
