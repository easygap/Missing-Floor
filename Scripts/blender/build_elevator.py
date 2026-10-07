"""승강기 칸·승강장 문틀·문짝·조작반·CCTV를 gpt-image 시안과 텍스처로 만든다.

원본은 Content/SourceArt/AI/Elevator*_20261006(시안·스테인리스 판·바닥 비닐·천장·조작반)
이다. 판의 결과 손때, 바닥 무늬, 천장 확산판, 조작반 버튼은 전부 그 그림에서 오고,
형상(판 나눔·홈·손잡이·문틀 단면)만 여기서 시안에 맞춰 짠다.

저작 좌표는 다른 소품과 같다. 앞이 -Y, 단위는 미터. 칸은 안쪽 바닥 가운데가 원점이고
너비 150(X) 깊이 120(Y) 높이 225(Z) cm다. 씬은 칸을 yaw -90으로 돌려 앞이 -X(승강장)를
보게 놓는다.

거울 판(SM_ElevatorBackPanel)은 굽지 않는다. 씬이 거울 재질을 직접 씌운다. 그래서 뒷벽
메시는 거울 자리 뒤를 비워 둔다. 거울 캡처 카메라가 그 뒤에서 칸 안을 본다.

    blender -b --factory-startup --python Scripts/blender/build_elevator.py -- [out_dir]
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
import ig_blender_lib as ig  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
AI = os.path.abspath(os.path.join(HERE, "..", "..", "Content", "SourceArt", "AI"))
PANEL = os.path.join(AI, "ElevatorStainlessPanel_20261006.png")
FLOOR = os.path.join(AI, "ElevatorFloorVinyl_20261006.png")
CEILING = os.path.join(AI, "ElevatorCeilingPanel_20261006.png")
COP = os.path.join(AI, "ElevatorCopFaceplate_20261006.png")
LAMP_OFF = os.path.join(AI, "ElevatorFullLampOff_20261007.png")
LAMP_LIT = os.path.join(AI, "ElevatorFullLampLit_20261007.png")
# 표시등 판이 그림(1536 x 1024)에서 차지하는 칸. 두 그림이 같은 자리다.
LAMP_PX = (97, 196, 1437, 789)
LAMP_SIZE = (1536, 1024)
CCTV_REF = "ElevatorCctvDomeReference_20261006.json"

# 칸 안쪽 치수(m)
HALF_W = 0.75
HALF_D = 0.60
HEIGHT = 2.25
DOOR_HALF = 0.375
DOOR_H = 2.10

# 조작반 그림(1024 x 1536)에서 판이 차지하는 칸. 판 바깥 회색 여백은 잘라 낸다.
COP_PX = (366, 28, 656, 1505)
COP_SIZE = (1024, 1536)
COP_W = 0.18
COP_H = 0.92


# 엘리베이터 안쪽 패널은 메탈릭을 반쯤만 준다. 1.0이면 좁은 안쪽에서 비칠 밝은 게 천장
# 조명 하나뿐이라 벽이 거의 까맣게 나왔다(2026-10-07 렌더 확인). 실제 헤어라인 판은 결을
# 따라 빛을 퍼뜨려서 안이 밝다.
CAB_METALLIC = 0.45
CAB_ROUGHNESS = 0.4


def image_material(name, path, roughness, metallic, repeat=False, emission=0.0):
    mat = ig.mat_image_uv(name, path, roughness=roughness, metallic=metallic,
                          emission_strength=emission)
    if repeat:
        for node in mat.node_tree.nodes:
            if node.type == "TEX_IMAGE":
                node.extension = "REPEAT"
    return mat


def lamp_material(name, base_path, emit_path, strength):
    """바탕색은 꺼진 그림, 발광은 켜진 그림. UE에서는 EmissiveStrength로 켜고 끈다."""
    mat = ig.mat_image_uv(name, base_path, roughness=0.18, metallic=0.0)
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    uv = next(n for n in nodes if n.type == "UVMAP")
    emit = nodes.new("ShaderNodeTexImage")
    emit.image = bpy.data.images.load(emit_path)
    emit.extension = "EXTEND"
    links.new(uv.outputs["UV"], emit.inputs["Vector"])
    links.new(emit.outputs["Color"], bsdf.inputs["Emission Color"])
    bsdf.inputs["Emission Strength"].default_value = strength
    mat["ig_emissive"] = True
    return mat


def project_uv(ob, lo, hi, rect=(0.0, 0.0, 1.0, 1.0), repeat=(1.0, 1.0), uv_layer="ImageUV"):
    """상자 면마다 가장 큰 축으로 평면 투영한 ImageUV를 단다.

    lo, hi: 투영 기준 상자(m). rect: 그림에서 쓸 칸(u0, v0, u1, v1). repeat: 반복 횟수.
    정면·뒷면(±Y)은 X와 Z를, 옆면(±X)은 Y와 Z를, 위아래(±Z)는 X와 Y를 쓴다.
    """
    bpy.context.view_layer.update()
    me = ob.data
    layer = me.uv_layers.get(uv_layer) or me.uv_layers.new(name=uv_layer)
    mw = ob.matrix_world
    u0, v0, u1, v1 = rect
    for poly in me.polygons:
        n = (mw.to_3x3() @ poly.normal).normalized()
        ax = max(range(3), key=lambda i: abs(n[i]))
        for li in poly.loop_indices:
            co = mw @ me.vertices[me.loops[li].vertex_index].co
            if ax == 1:
                a = (co.x - lo[0]) / max(hi[0] - lo[0], 1e-6)
                b = (co.z - lo[2]) / max(hi[2] - lo[2], 1e-6)
                if n.y > 0:
                    a = 1.0 - a
            elif ax == 0:
                a = (co.y - lo[1]) / max(hi[1] - lo[1], 1e-6)
                b = (co.z - lo[2]) / max(hi[2] - lo[2], 1e-6)
                if n.x < 0:
                    a = 1.0 - a
            else:
                a = (co.x - lo[0]) / max(hi[0] - lo[0], 1e-6)
                b = (co.y - lo[1]) / max(hi[1] - lo[1], 1e-6)
            layer.data[li].uv = (u0 + (u1 - u0) * a * repeat[0], v0 + (v1 - v0) * b * repeat[1])
    return ob


def panel_box(name, size, location, mat, rect, bevel=0.0015):
    ob = ig.box(name, size, location, material=mat, bevel=bevel, segments=1)
    ig.apply_modifiers(ob)
    lo = [location[i] - size[i] * 0.5 for i in range(3)]
    hi = [location[i] + size[i] * 0.5 for i in range(3)]
    return project_uv(ob, lo, hi, rect)


def steel_rect(u0, width_m, z0, z1):
    """판 그림(100 x 220 cm)에서 u0부터 width_m 폭, 높이 z0..z1(m) 칸."""
    return (u0, z0 / 2.2, u0 + width_m / 1.0, z1 / 2.2)


# --------------------------------------------------------------------------

def build_cab_sides(out_root):
    """양 옆벽. 한 벽에 판 셋, 판 사이 4 mm 홈, 밑동 걸레판과 위 띠."""
    ig.reset_scene()
    steel = image_material("CabSteel", PANEL, roughness=CAB_ROUGHNESS, metallic=CAB_METALLIC)
    groove = ig.mat_plastic("CabGroove", (0.025, 0.026, 0.028), roughness=0.7, bump=0)
    parts, hulls = [], []
    for side, sign in (("l", -1.0), ("r", 1.0)):
        x_in = sign * HALF_W
        backing = ig.box(f"backing_{side}", (0.012, 2 * HALF_D, HEIGHT),
                         (x_in + sign * 0.026, 0.0, HEIGHT * 0.5), material=groove)
        parts.append(backing)
        hulls.append([backing])
        panel_w = (2 * HALF_D - 0.008) / 3.0
        for i in range(3):
            y = -HALF_D + panel_w * 0.5 + i * (panel_w + 0.004)
            u0 = (0.05, 0.33, 0.58)[i] if sign > 0 else (0.52, 0.12, 0.31)[i]
            parts.append(panel_box(f"panel_{side}{i}", (0.016, panel_w, 2.05),
                                   (x_in + sign * 0.012, y, 0.10 + 1.025), steel,
                                   steel_rect(u0, panel_w, 0.10, 2.15)))
        parts.append(panel_box(f"kick_{side}", (0.02, 2 * HALF_D, 0.10),
                               (x_in + sign * 0.010, 0.0, 0.05), steel, steel_rect(0.0, 1.0, 0.0, 0.10)))
        parts.append(panel_box(f"top_{side}", (0.018, 2 * HALF_D, 0.10),
                               (x_in + sign * 0.011, 0.0, 2.20), steel, steel_rect(0.0, 1.0, 2.10, 2.20)))
    return ig.build_asset("SM_ElevatorCabSides", "large", parts, out_root, collision_parts=hulls,
                          texture_size=2048,
                          notes="칸 옆벽 둘(안쪽 X ±75 cm). 판 셋과 홈, 걸레판. 원본 ElevatorStainlessPanel_20261006. "
                                "원점 칸 바닥 가운데, 앞 -Y.")


def build_cab_front(out_root):
    """앞벽: 문 양옆 판(문짝이 들어가 숨는 자리), 문 위 띠, 안쪽 층 표시창."""
    ig.reset_scene()
    steel = image_material("FrontSteel", PANEL, roughness=CAB_ROUGHNESS, metallic=CAB_METALLIC)
    dark = ig.mat_gloss("Display", (0.006, 0.006, 0.007), roughness=0.08)
    side_w = HALF_W - DOOR_HALF
    parts, hulls = [], []
    for sign in (-1.0, 1.0):
        ret = panel_box(f"return_{sign:+.0f}", (side_w, 0.06, HEIGHT),
                        (sign * (DOOR_HALF + side_w * 0.5), -HALF_D - 0.03, HEIGHT * 0.5), steel,
                        steel_rect(0.2 if sign < 0 else 0.55, side_w, 0.0, HEIGHT))
        parts.append(ret)
        hulls.append([ret])
    transom = panel_box("transom", (2 * DOOR_HALF, 0.06, HEIGHT - DOOR_H),
                        (0.0, -HALF_D - 0.03, DOOR_H + (HEIGHT - DOOR_H) * 0.5), steel,
                        steel_rect(0.1, 2 * DOOR_HALF, DOOR_H, HEIGHT))
    parts.append(transom)
    hulls.append([transom])
    parts.append(ig.box("display", (0.12, 0.004, 0.07), (0.0, -HALF_D + 0.002, 2.175), material=dark, bevel=0.002))
    return ig.build_asset("SM_ElevatorCabFront", "prop", parts, out_root, collision_parts=hulls,
                          texture_size=1024,
                          notes="칸 앞벽(안쪽 Y -60 cm), 문 너비 75 cm·높이 210 cm. 층 표시창은 검은 판이고 숫자는 씬 글자.")


def build_cab_back(out_root):
    """뒷벽 테두리와 손잡이. 가운데 거울 자리(X ±45, Z 10..215)는 비운다."""
    ig.reset_scene()
    steel = image_material("BackSteel", PANEL, roughness=CAB_ROUGHNESS, metallic=CAB_METALLIC)
    rail = ig.mat_metal("Handrail", (0.80, 0.80, 0.79), roughness=0.22, streak=0.04)
    parts, hulls = [], []
    y = HALF_D + 0.012
    for sign in (-1.0, 1.0):
        strip = panel_box(f"strip_{sign:+.0f}", (0.30, 0.024, 2.05), (sign * 0.60, y, 1.125), steel,
                          steel_rect(0.08 if sign < 0 else 0.62, 0.30, 0.10, 2.15))
        parts.append(strip)
        hulls.append([strip])
    parts.append(panel_box("kick", (2 * HALF_W, 0.024, 0.10), (0.0, y, 0.05), steel, steel_rect(0.0, 1.0, 0.0, 0.10)))
    parts.append(panel_box("top", (2 * HALF_W, 0.024, 0.10), (0.0, y, 2.20), steel, steel_rect(0.0, 1.0, 2.10, 2.20)))
    # 손잡이: 지름 38 mm 둥근 봉, 벽에서 5 cm.
    parts.append(ig.cylinder("rail", 0.019, 1.24, (0.0, HALF_D - 0.05, 0.90),
                             rotation=(0.0, math.pi / 2, 0.0), segments=20, material=rail))
    for sign in (-1.0, 1.0):
        parts.append(ig.cylinder(f"rail_cap_{sign:+.0f}", 0.019, 0.02, (sign * 0.62, HALF_D - 0.05, 0.90),
                                 rotation=(0.0, math.pi / 2, 0.0), segments=20, material=rail, bevel=0.006))
        parts.append(ig.cylinder(f"bracket_{sign:+.0f}", 0.010, 0.05, (sign * 0.52, HALF_D - 0.025, 0.90),
                                 rotation=(math.pi / 2, 0.0, 0.0), segments=12, material=rail))
    return ig.build_asset("SM_ElevatorCabBack", "prop", parts, out_root, collision_parts=hulls,
                          texture_size=1024,
                          notes="칸 뒷벽 테두리(안쪽 Y +60 cm)와 손잡이(높이 90 cm). 거울 판 자리는 비어 있다.")


def build_back_panel(out_root):
    """거울 판. 굽지 않고 UV0 0..1을 그대로 내보낸다. 씬이 거울 재질을 씌운다."""
    ig.reset_scene()
    mirror = ig.mat_metal("Mirror", (0.82, 0.83, 0.84), roughness=0.08, streak=0.0)
    plate = ig.box("mirror", (0.90, 0.006, 2.05), (0.0, HALF_D + 0.002, 1.125), material=mirror)
    me = plate.data
    uv = me.uv_layers.new(name="UVMap")
    for poly in me.polygons:
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            uv.data[li].uv = (co.x / 0.90 + 0.5, co.z / 2.05 + 0.5)
    return ig.build_asset("SM_ElevatorBackPanel", "prop", [plate], out_root, collision_parts=[[plate]],
                          raw_uv=True, preview=False,
                          notes="칸 뒷벽 가운데 거울 판 90 x 205 cm. UV0을 화면 좌표 대신 판 좌표로 둔다.")


def build_cab_ceiling(out_root):
    """천장 한 장과 가운데 아크릴 확산판. 확산판만 빛난다."""
    ig.reset_scene()
    ceiling = image_material("Ceiling", CEILING, roughness=CAB_ROUGHNESS, metallic=CAB_METALLIC)
    glow = image_material("Diffuser", CEILING, roughness=0.6, metallic=0.0, emission=3.0)
    slab = ig.box("slab", (2 * HALF_W, 2 * HALF_D, 0.02), (0.0, 0.0, HEIGHT + 0.01), material=ceiling)
    project_uv(slab, (-HALF_W, -HALF_D, 0), (HALF_W, HALF_D, 0))
    # 확산판 자리: 그림(1415 x 1111)의 x 185..1230, y 270..840.
    u0, u1 = 185 / 1415, 1230 / 1415
    v0, v1 = 1.0 - 840 / 1111, 1.0 - 270 / 1111
    dx0 = -HALF_W + 2 * HALF_W * u0
    dx1 = -HALF_W + 2 * HALF_W * u1
    dy0 = -HALF_D + 2 * HALF_D * v0
    dy1 = -HALF_D + 2 * HALF_D * v1
    diffuser = ig.box("diffuser", (dx1 - dx0, dy1 - dy0, 0.004),
                      ((dx0 + dx1) * 0.5, (dy0 + dy1) * 0.5, HEIGHT - 0.002), material=glow)
    project_uv(diffuser, (dx0, dy0, 0), (dx1, dy1, 0), rect=(u0, v0, u1, v1))
    return ig.build_asset("SM_ElevatorCabCeiling", "prop", [slab, diffuser], out_root, collision_parts=[[slab]],
                          texture_size=1024,
                          notes="칸 천장(높이 225 cm). 원본 ElevatorCeilingPanel_20261006. 확산판만 발광.")


def build_cab_floor(out_root):
    """바닥 비닐 타일(30 cm)과 문턱. 문턱은 승강장 쪽으로 15 cm 나온다."""
    ig.reset_scene()
    vinyl = image_material("Vinyl", FLOOR, roughness=0.58, metallic=0.0, repeat=True)
    sill_mat = image_material("Sill", PANEL, roughness=0.42, metallic=1.0)
    slab = ig.box("slab", (2 * HALF_W, 2 * HALF_D, 0.10), (0.0, 0.0, -0.05), material=vinyl)
    # 그림 한 장이 타일 4 x 4, 120 cm다.
    project_uv(slab, (-HALF_W, -HALF_D, 0), (-HALF_W + 1.2, -HALF_D + 1.2, 0))
    sill = ig.box("sill", (2 * DOOR_HALF + 0.05, 0.15, 0.10), (0.0, -HALF_D - 0.075, -0.05),
                  material=sill_mat, bevel=0.002, segments=1)
    ig.apply_modifiers(sill)
    project_uv(sill, (-0.4, -0.75, -0.1), (0.4, -0.6, 0.0), rect=(0.1, 0.0, 0.9, 0.05))
    return ig.build_asset("SM_ElevatorCabFloor", "prop", [slab, sill], out_root, collision_parts=[[slab], [sill]],
                          texture_size=1024,
                          notes="칸 바닥(윗면 Z 0)과 문턱. 원본 ElevatorFloorVinyl_20261006(30 cm 타일 4 x 4).")


def build_door_panel(out_root):
    """문짝 한 장 37.5 x 210 x 3 cm. 칸 문과 승강장 문이 같은 메시를 쓴다."""
    ig.reset_scene()
    steel = image_material("DoorSteel", PANEL, roughness=0.36, metallic=0.6)
    leaf = ig.box("leaf", (DOOR_HALF, 0.03, DOOR_H), (0.0, 0.0, DOOR_H * 0.5), material=steel, bevel=0.002, segments=1)
    ig.apply_modifiers(leaf)
    project_uv(leaf, (-DOOR_HALF * 0.5, -0.015, 0.0), (DOOR_HALF * 0.5, 0.015, DOOR_H),
               rect=steel_rect(0.55, DOOR_HALF, 0.0, DOOR_H))
    return ig.build_asset("SM_ElevatorDoorPanel", "prop", [leaf], out_root, collision_parts=[[leaf]],
                          texture_size=1024,
                          notes="승강기 문짝 37.5 x 210 x 3 cm. 원점 아래 가운데. 칸·승강장 공용.")


def build_landing_frame(out_root):
    """승강장 문틀. 벽 구멍(110 x 240 cm)에 끼우는 기둥 둘, 위 띠, 층 표시창, 문턱.

    원점은 바닥 높이의 벽 면 가운데다. 앞(-Y)이 복도 쪽.
    """
    ig.reset_scene()
    steel = image_material("FrameSteel", PANEL, roughness=0.36, metallic=1.0)
    dark = ig.mat_gloss("HallDisplay", (0.006, 0.006, 0.007), roughness=0.08)
    parts = []
    jamb_w = 0.55 - DOOR_HALF
    for sign in (-1.0, 1.0):
        x = sign * (DOOR_HALF + jamb_w * 0.5)
        parts.append(panel_box(f"jamb_face_{sign:+.0f}", (jamb_w + 0.02, 0.02, 2.12), (x, -0.01, 1.06), steel,
                               steel_rect(0.3 if sign < 0 else 0.6, jamb_w, 0.0, 2.12)))
        parts.append(panel_box(f"jamb_reveal_{sign:+.0f}", (0.015, 0.14, 2.10),
                               (sign * (DOOR_HALF + 0.0075), 0.07, 1.05), steel, steel_rect(0.4, 0.14, 0.0, 2.10)))
    parts.append(panel_box("head", (1.12, 0.02, 0.30), (0.0, -0.01, 2.25), steel, steel_rect(0.0, 1.0, 1.8, 2.1)))
    parts.append(panel_box("head_reveal", (2 * DOOR_HALF + 0.03, 0.14, 0.015), (0.0, 0.07, 2.1075), steel,
                           steel_rect(0.2, 0.75, 0.0, 0.14)))
    parts.append(ig.box("display", (0.16, 0.004, 0.07), (0.0, -0.022, 2.26), material=dark, bevel=0.002))
    sill = ig.box("sill", (2 * DOOR_HALF + 0.03, 0.16, 0.02), (0.0, 0.08, -0.008), material=steel, bevel=0.002, segments=1)
    ig.apply_modifiers(sill)
    project_uv(sill, (-0.4, 0.0, -0.02), (0.4, 0.16, 0.0), rect=(0.1, 0.0, 0.9, 0.05))
    parts.append(sill)
    return ig.build_asset("SM_ElevatorLandingFrame", "prop", parts, out_root, collision_parts=None,
                          texture_size=1024,
                          notes="승강장 문틀(벽 구멍 110 cm, 문 75 cm). 원점 바닥 높이 벽 면 가운데. "
                                "원본 ElevatorLandingConcept_20261006, 판 ElevatorStainlessPanel_20261006.")


def build_cop(out_root):
    """조작반 판. 버튼은 그림 위에 살짝 솟은 원판으로 다시 얹는다."""
    ig.reset_scene()
    plate_mat = image_material("CopPlate", COP, roughness=0.36, metallic=0.85)
    u0 = COP_PX[0] / COP_SIZE[0]
    u1 = COP_PX[2] / COP_SIZE[0]
    v0 = 1.0 - COP_PX[3] / COP_SIZE[1]
    v1 = 1.0 - COP_PX[1] / COP_SIZE[1]
    plate = ig.box("plate", (COP_W, 0.012, COP_H), (0.0, 0.0, COP_H * 0.5), material=plate_mat, bevel=0.003, segments=2)
    ig.apply_modifiers(plate)
    project_uv(plate, (-COP_W * 0.5, -0.006, 0.0), (COP_W * 0.5, 0.006, COP_H), rect=(u0, v0, u1, v1))
    parts = [plate]
    # 버튼 중심(그림 y 픽셀)과 반지름. 그림에서 잰 값이다.
    for index, (py, radius_px) in enumerate(((300, 42), (400, 42), (500, 42), (601, 42), (702, 42), (803, 42),
                                             (948, 42), (1048, 42), (1158, 42))):
        z = (COP_PX[3] - py) / (COP_PX[3] - COP_PX[1]) * COP_H
        r = radius_px / (COP_PX[2] - COP_PX[0]) * COP_W
        button = ig.cylinder(f"button_{index}", r, 0.004, (0.0, -0.008, z), rotation=(math.pi / 2, 0.0, 0.0),
                             segments=28, material=plate_mat, bevel=0.0012, bevel_segments=2)
        ig.apply_modifiers(button)
        bu = 510 / COP_SIZE[0]
        bv = 1.0 - py / COP_SIZE[1]
        du = radius_px / COP_SIZE[0]
        dv = radius_px / COP_SIZE[1]
        project_uv(button, (-r, -0.01, z - r), (r, -0.006, z + r), rect=(bu - du, bv - dv, bu + du, bv + dv))
        parts.append(button)
    return ig.build_asset("SM_ElevatorCop", "prop", parts, out_root, collision_parts=None, texture_size=1024,
                          notes="칸 조작반 18 x 92 cm. 원본 ElevatorCopFaceplate_20261006. 위에서부터 5·4·3·2·1·B1, "
                                "열림·닫힘·비상. 표시창 숫자는 씬 글자. 원점 판 아래 가운데, 앞 -Y.")


def build_cctv(out_root):
    """천장 모서리 CCTV 돔. 원점은 천장에 닿는 바닥 고리 윗면 가운데, 돔은 아래(-Z)로 늘어진다."""
    ig.reset_scene()
    shell = ig.mat_plastic("DomeBase", (0.80, 0.78, 0.72), roughness=0.5, bump=0.004)
    smoke = ig.mat_gloss("DomeSmoke", (0.018, 0.019, 0.021), roughness=0.06)
    lens = ig.mat_gloss("Lens", (0.002, 0.002, 0.003), roughness=0.02)
    base = ig.cylinder("base", 0.058, 0.026, (0.0, 0.0, -0.013), segments=40, material=shell, bevel=0.004)
    dome = ig.lathe("dome", [(0.0, -0.072), (0.020, -0.070), (0.034, -0.062), (0.043, -0.048),
                             (0.048, -0.034), (0.049, -0.026), (0.0, -0.026)], segments=40, material=smoke)
    eye = ig.cylinder("lens", 0.009, 0.012, (0.0, -0.022, -0.050), rotation=(math.radians(60), 0.0, 0.0),
                      segments=20, material=lens)
    parts = [base, dome, eye]
    for angle in (0.0, math.pi):
        parts.append(ig.cylinder(f"screw_{angle:.1f}", 0.004, 0.002, (0.050 * math.cos(angle), 0.050 * math.sin(angle), -0.027),
                                 segments=10, material=shell))
    return ig.build_asset("SM_ElevatorCctvDome", "prop", parts, out_root, collision_parts=None, texture_size=512,
                          origin="top-center",
                          notes=f"칸 천장 모서리 CCTV 돔 지름 11.6 cm. 참조 {CCTV_REF}. 원점 천장 접면 가운데.")


def build_full_lamp(out_root):
    """「만원」 표시등 판 13 x 5.8 cm. 원점은 판 뒷면 아래 가운데, 앞 -Y."""
    ig.reset_scene()
    mat = lamp_material("FullLamp", LAMP_OFF, LAMP_LIT, 6.0)
    w = 0.13
    h = w * (LAMP_PX[3] - LAMP_PX[1]) / (LAMP_PX[2] - LAMP_PX[0])
    plate = ig.box("plate", (w, 0.004, h), (0.0, -0.002, h * 0.5), material=mat, bevel=0.0008, segments=1)
    ig.apply_modifiers(plate)
    u0 = LAMP_PX[0] / LAMP_SIZE[0]
    u1 = LAMP_PX[2] / LAMP_SIZE[0]
    v0 = 1.0 - LAMP_PX[3] / LAMP_SIZE[1]
    v1 = 1.0 - LAMP_PX[1] / LAMP_SIZE[1]
    project_uv(plate, (-w * 0.5, -0.004, 0.0), (w * 0.5, 0.0, h), rect=(u0, v0, u1, v1))
    return ig.build_asset("SM_ElevatorFullLamp", "prop", [plate], out_root, collision_parts=None, texture_size=512,
                          notes="칸 「만원」 표시등. 바탕 ElevatorFullLampOff_20261007, 발광 ElevatorFullLampLit_20261007. "
                                "씬이 EmissiveStrength로 켜고 끈다. 원점 판 뒷면 아래 가운데, 앞 -Y.")


def build_button_led(out_root):
    """조작반 버튼 옆 작은 등. 지름 7 mm 볼록 렌즈. 원점은 바닥(벽 쪽) 가운데, 앞 -Y."""
    ig.reset_scene()
    lens = ig.mat_emissive("LedLens", (1.0, 0.32, 0.08), strength=4.0)
    housing = ig.mat_gloss("LedHousing", (0.05, 0.012, 0.008), roughness=0.25)
    dome = ig.lathe("dome", [(0.0, 0.0028), (0.0018, 0.0025), (0.0030, 0.0014), (0.0035, 0.0), (0.0, 0.0)],
                    segments=16, rotation=(math.pi / 2, 0.0, 0.0), material=lens)
    rim = ig.cylinder("rim", 0.0042, 0.0006, (0.0, 0.0003, 0.0), rotation=(math.pi / 2, 0.0, 0.0),
                      segments=16, material=housing)
    return ig.build_asset("SM_ElevatorButtonLed", "prop", [dome, rim], out_root, collision_parts=None,
                          texture_size=128, origin="back-center",
                          notes="조작반 버튼 옆 등. 씬이 EmissiveStrength로 켜고 끈다.")


def main():
    out_root = ig.out_root_from_argv()
    only = None
    if "--" in sys.argv:
        args = sys.argv[sys.argv.index("--") + 1:]
        if len(args) > 1:
            only = set(args[1:])
    builders = {
        "SM_ElevatorCabSides": build_cab_sides,
        "SM_ElevatorCabFront": build_cab_front,
        "SM_ElevatorCabBack": build_cab_back,
        "SM_ElevatorBackPanel": build_back_panel,
        "SM_ElevatorCabCeiling": build_cab_ceiling,
        "SM_ElevatorCabFloor": build_cab_floor,
        "SM_ElevatorDoorPanel": build_door_panel,
        "SM_ElevatorLandingFrame": build_landing_frame,
        "SM_ElevatorCop": build_cop,
        "SM_ElevatorCctvDome": build_cctv,
        "SM_ElevatorFullLamp": build_full_lamp,
        "SM_ElevatorButtonLed": build_button_led,
    }
    for name, builder in builders.items():
        if only and name not in only:
            continue
        builder(out_root)


main()
