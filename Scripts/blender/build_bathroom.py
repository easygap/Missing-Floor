"""403호 욕실의 비품. 문을 닫고 잠그면 숨는 자리가 되는 방이다(EXPANSION_PLAN §4).

기준: Content/SourceArt/AI/BathroomConcept_20261007.png(배치·색), BathroomToilet·BathroomBasin·
BathroomMirrorCabinet·BathroomShower·BathroomDoor_20261007.png(형상). 1990년대 빌라 원룸 욕실이다.
흰 유약 타일 벽에 회색 논슬립 바닥, 벽걸이 세면대와 거울장, 투피스 양변기, 벽걸이 샤워 수전,
크롬 수건걸이에 회색 수건, 코너 선반에 병 둘, 스테인리스 배수구, 천장 환풍구. 벽과 바닥 타일은
씬이 월드 투영 재질(M_BathroomWallTile_*, M_BathroomFloorTile_XY)로 깐다.

벽에 붙는 것들은 원점이 벽면과 바닥이 만나는 점이고 앞(방 쪽)이 -Y다. 씬은 벽면 좌표에
그대로 놓고 벽이 보는 쪽으로 돌린다. 문짝은 경첩이 +X, 앞면 -Y가 403호 쪽이고(방화문과 같은
관례), 문틀은 개구부 바닥 가운데가 원점이다. 코너 선반은 모서리가 원점이고 x<0, y>0 사분면에
놓인다.

    blender -b --factory-startup --python Scripts/blender/build_bathroom.py -- <out_dir> [toilet|basin|mirror|shower|door|frame|towel|shelf|drain|vent ...]
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import ig_blender_lib as ig  # noqa: E402


# --------------------------------------------------------------------------
# 재질
# --------------------------------------------------------------------------

def mat_ceramic(name, color=(0.80, 0.80, 0.775), roughness=0.07):
    """유약 바른 위생도기. 매끈하고 번들거리며, 바닥에 닿는 아래 4 cm만 조금 누렇다."""
    mat = bpy.data.materials.new(name)
    bsdf = ig._principled(mat)
    tree, nodes, links = ig._nodes(mat)
    coords = ig._object_coords(nodes)
    sep = nodes.new('ShaderNodeSeparateXYZ')
    links.new(coords, sep.inputs['Vector'])
    foot = ig._map_range(nodes, links, sep.outputs['Z'], 0.045, 0.0, 0.0, 0.55)
    foot.clamp = True
    base = nodes.new('ShaderNodeRGB')
    base.outputs[0].default_value = (*color, 1.0)
    stain = nodes.new('ShaderNodeRGB')
    stain.outputs[0].default_value = (0.62, 0.58, 0.47, 1.0)
    mix = nodes.new('ShaderNodeMixRGB')
    links.new(foot.outputs['Result'], mix.inputs['Fac'])
    links.new(base.outputs[0], mix.inputs['Color1'])
    links.new(stain.outputs[0], mix.inputs['Color2'])
    links.new(mix.outputs[0], bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = roughness
    bsdf.inputs['Metallic'].default_value = 0.0
    mat['ig_kind'] = 'baked'
    return mat


def mat_chrome(name='Chrome'):
    return ig.mat_metal(name, (0.90, 0.90, 0.90), roughness=0.07, streak=0.02, anisotropic=False)


def mat_terry(name, color):
    """수건. 고리 파일이 빛을 먹어 거칠고, 손바닥만 한 결이 부드럽게 일렁인다."""
    mat = bpy.data.materials.new(name)
    bsdf = ig._principled(mat)
    tree, nodes, links = ig._nodes(mat)
    coords = ig._object_coords(nodes)
    pile = ig._noise(nodes, links, 260.0, detail=1.0, roughness=0.4, coords=coords)
    tone = ig._map_range(nodes, links, pile.outputs['Fac'], 0.3, 0.7, 0.93, 1.05)
    base = nodes.new('ShaderNodeRGB')
    base.outputs[0].default_value = (*color, 1.0)
    tinted = nodes.new('ShaderNodeMixRGB')
    tinted.blend_type = 'MULTIPLY'
    tinted.inputs['Fac'].default_value = 1.0
    links.new(base.outputs[0], tinted.inputs['Color1'])
    links.new(tone.outputs['Result'], tinted.inputs['Color2'])
    links.new(tinted.outputs[0], bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = 0.96
    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.35
    bump.inputs['Distance'].default_value = 0.0015
    links.new(pile.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])
    mat['ig_kind'] = 'baked'
    return mat


def mat_mirror(name='Mirror'):
    mat = bpy.data.materials.new(name)
    bsdf = ig._principled(mat)
    bsdf.inputs['Base Color'].default_value = (0.93, 0.94, 0.94, 1.0)
    bsdf.inputs['Metallic'].default_value = 1.0
    bsdf.inputs['Roughness'].default_value = 0.025
    mat['ig_kind'] = 'baked'
    return mat


# --------------------------------------------------------------------------
# 단면을 쌓는 형상
# --------------------------------------------------------------------------

def superellipse(cx, cy, rx, ry, z, count=40, power=2.4, back_y=None):
    """초타원 둘레의 점. back_y를 주면 그보다 +Y로 나가는 점을 그 선에 눌러 벽에 붙는 평평한 등을 만든다."""
    points = []
    exponent = 2.0 / power
    for index in range(count):
        angle = math.tau * index / count
        c, s = math.cos(angle), math.sin(angle)
        x = cx + rx * math.copysign(abs(c) ** exponent, c)
        y = cy + ry * math.copysign(abs(s) ** exponent, s)
        if back_y is not None:
            y = min(y, back_y)
        points.append((x, y, z))
    return points


def loft(name, rings, material, cap_bottom=True, cap_top=True):
    """같은 점 수의 고리들을 아래에서 위로 이어 닫힌 껍데기를 만든다."""
    bm = bmesh.new()
    layers = [[bm.verts.new(p) for p in ring] for ring in rings]
    count = len(rings[0])
    for lower, upper in zip(layers, layers[1:]):
        for index in range(count):
            nxt = (index + 1) % count
            bm.faces.new((lower[index], lower[nxt], upper[nxt], upper[index]))
    if cap_bottom:
        bm.faces.new(list(reversed(layers[0])))
    if cap_top:
        bm.faces.new(layers[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob = ig._link(bpy.data.objects.new(name, ig._mesh_from_bm(name, bm)))
    for poly in ob.data.polygons:
        poly.use_smooth = True
    ig.assign_material(ob, material)
    return ob


def slab(name, outline, z0, thickness, material, bevel):
    """둘레선을 위로 밀어 올린 판. 모서리만 둥글린다(변좌, 뚜껑)."""
    bm = bmesh.new()
    verts = [bm.verts.new((x, y, z0)) for x, y, _ in outline]
    face = bm.faces.new(verts)
    extruded = bmesh.ops.extrude_face_region(bm, geom=[face])
    top = [v for v in extruded['geom'] if isinstance(v, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, vec=Vector((0.0, 0.0, thickness)), verts=top)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob = ig._link(bpy.data.objects.new(name, ig._mesh_from_bm(name, bm)))
    ig.assign_material(ob, material)
    ig.add_bevel(ob, bevel, segments=3, angle_deg=60.0)
    ig.apply_modifiers(ob)
    for poly in ob.data.polygons:
        poly.use_smooth = True
    return ob


def smooth(ob, levels=2):
    mod = ob.modifiers.new('Subsurf', 'SUBSURF')
    mod.levels = levels
    mod.render_levels = levels
    ig.apply_modifiers(ob)
    for poly in ob.data.polygons:
        poly.use_smooth = True
    return ob


# --------------------------------------------------------------------------
# 비품
# --------------------------------------------------------------------------

def build_toilet(out_root):
    """투피스 양변기. 뚜껑을 닫아 두었다. 원점은 물탱크 뒤 벽면과 바닥, 앞 -Y."""
    ig.reset_scene()
    ceramic = mat_ceramic('ToiletCeramic')
    seat_mat = ig.mat_plastic('ToiletSeat', (0.83, 0.83, 0.81), roughness=0.22, bump=0.0)
    chrome = mat_chrome()
    # 몸통: 받침에서 테두리까지. 받침은 좁고 길며, 25 cm 위부터 앞으로 뻗은 긴 타원으로 벌어진다.
    # 바닥 면은 막지 않는다. 바닥에 가려 보이지 않고, 막으면 세분화가 받침에 주름을 만든다.
    bowl_profile = [
        (0.000, -0.335, 0.104, 0.164, 2.0), (0.060, -0.337, 0.106, 0.168, 2.0),
        (0.140, -0.344, 0.116, 0.180, 2.0), (0.210, -0.358, 0.133, 0.199, 2.0),
        (0.270, -0.378, 0.156, 0.226, 2.05), (0.320, -0.398, 0.174, 0.247, 2.1),
        (0.360, -0.413, 0.184, 0.258, 2.1), (0.388, -0.420, 0.187, 0.262, 2.1),
        (0.400, -0.420, 0.184, 0.259, 2.1),
    ]
    bowl_rings = [superellipse(0.0, cy, rx, ry, z, count=48, power=power) for z, cy, rx, ry, power in bowl_profile]
    bowl = smooth(loft('bowl', bowl_rings, ceramic, cap_bottom=False), 2)
    # 물탱크. 뚜껑은 몸통보다 5 mm 크다.
    tank = ig.box('tank', (0.47, 0.170, 0.335), location=(0.0, -0.105, 0.400), origin='bottom',
                  bevel=0.022, segments=4, material=ceramic)
    tank_lid = ig.box('tank_lid', (0.48, 0.180, 0.026), location=(0.0, -0.105, 0.735), origin='bottom',
                      bevel=0.010, segments=3, material=ceramic)
    # 물탱크가 얹히는 뒤쪽 받침. 몸통 뒤와 물탱크 밑을 한 덩어리로 잇는다.
    deck = ig.box('rear_deck', (0.30, 0.17, 0.13), location=(0.0, -0.11, 0.27), origin='bottom',
                  bevel=0.035, segments=4, material=ceramic)
    # 변좌와 닫힌 뚜껑. 뚜껑은 변좌보다 조금 작고 모서리가 더 둥글다.
    seat = slab('seat', superellipse(0.0, -0.43, 0.184, 0.249, 0.0, count=64, power=2.0), 0.400, 0.017,
                seat_mat, 0.004)
    lid = slab('lid', superellipse(0.0, -0.425, 0.181, 0.246, 0.0, count=64, power=2.05), 0.417, 0.024,
               seat_mat, 0.008)
    hinge = ig.box('seat_hinge', (0.13, 0.03, 0.022), location=(0.0, -0.200, 0.400), origin='bottom',
                   bevel=0.006, segments=2, material=seat_mat)
    # 물 내림 손잡이(물탱크 앞 왼쪽 모서리)와 벽의 앵글 밸브, 물탱크로 올라가는 급수 호스.
    lever = ig.pipe('flush_lever', [(-0.185, -0.192, 0.66), (-0.185, -0.205, 0.66), (-0.13, -0.205, 0.655)],
                    radius=0.006, resolution=10, corner_radius=0.006, material=chrome)
    lever_plate = ig.cylinder('flush_rose', 0.016, 0.006, location=(-0.185, -0.192, 0.66),
                              rotation=(math.pi * 0.5, 0.0, 0.0), segments=24, material=chrome)
    valve = ig.cylinder('angle_valve', 0.012, 0.05, location=(-0.16, -0.025, 0.18),
                        rotation=(math.pi * 0.5, 0.0, 0.0), segments=20, material=chrome)
    flange = ig.cylinder('valve_flange', 0.025, 0.004, location=(-0.16, -0.002, 0.18),
                         rotation=(math.pi * 0.5, 0.0, 0.0), segments=24, material=chrome)
    hose = ig.pipe('supply_hose', [(-0.16, -0.05, 0.18), (-0.16, -0.075, 0.20), (-0.16, -0.09, 0.30),
                                   (-0.16, -0.09, 0.405)], radius=0.0055, resolution=10, corner_radius=0.02,
                   material=chrome)
    parts = [bowl, deck, tank, tank_lid, seat, lid, hinge, lever, lever_plate, valve, flange, hose]
    ig.build_asset(
        'SM_BathroomToilet', 'prop', parts, out_root,
        collision_parts=[[bowl, seat, lid], [deck, tank, tank_lid]], texture_size=1024, preview_yaw=35,
        notes=('투피스 양변기 48 x 68 x 76 cm, 뚜껑 닫힘. 원점은 물탱크 뒤 벽면과 바닥, 앞 -Y. '
               '시안 SourceArt/AI/BathroomToilet_20261007. 충돌은 몸통과 물탱크 둘.'))


def build_basin(out_root):
    """벽걸이 세면대와 수전·병 트랩. 원점은 벽면과 바닥, 앞 -Y. 테두리 윗면 82 cm."""
    ig.reset_scene()
    ceramic = mat_ceramic('BasinCeramic')
    chrome = mat_chrome()
    dark = ig.mat_plastic('DrainDark', (0.03, 0.03, 0.03), roughness=0.5, bump=0.0)
    top, under = 0.82, 0.66
    # 바깥 몸통. 등은 벽에 닿는 평평한 면이다(back_y).
    outer = [
        superellipse(0.0, -0.17, 0.150, 0.120, under, power=2.6, back_y=-0.005),
        superellipse(0.0, -0.18, 0.200, 0.160, under + 0.06, power=2.8, back_y=-0.002),
        superellipse(0.0, -0.18, 0.222, 0.178, top - 0.025, power=3.0, back_y=0.0),
        superellipse(0.0, -0.18, 0.225, 0.180, top, power=3.0, back_y=0.0),
    ]
    # 윗면 테두리 안쪽에서 대야로 내려간다. 대야는 앞으로 치우쳐 뒤에 수전 자리가 남는다.
    inner = [
        superellipse(0.0, -0.205, 0.180, 0.128, top, power=2.4),
        superellipse(0.0, -0.205, 0.172, 0.120, top - 0.012, power=2.3),
        superellipse(0.0, -0.205, 0.140, 0.095, top - 0.070, power=2.1),
        superellipse(0.0, -0.205, 0.080, 0.055, top - 0.118, power=2.0),
        superellipse(0.0, -0.205, 0.022, 0.022, top - 0.128, power=2.0),
    ]
    bm = bmesh.new()
    outer_layers = [[bm.verts.new(p) for p in ring] for ring in outer]
    inner_layers = [[bm.verts.new(p) for p in ring] for ring in inner]
    count = len(outer[0])

    def bridge(lower, upper):
        for index in range(count):
            nxt = (index + 1) % count
            bm.faces.new((lower[index], lower[nxt], upper[nxt], upper[index]))

    for lower, upper in zip(outer_layers, outer_layers[1:]):
        bridge(lower, upper)
    # 테두리 윗면: 바깥 고리에서 안쪽 고리로.
    bridge(outer_layers[-1], inner_layers[0])
    for upper, lower in zip(inner_layers, inner_layers[1:]):
        bridge(upper, lower)
    bm.faces.new(inner_layers[-1])
    bm.faces.new(list(reversed(outer_layers[0])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    body = ig._link(bpy.data.objects.new('basin', ig._mesh_from_bm('basin', bm)))
    ig.assign_material(body, ceramic)
    body = smooth(body, 2)
    drain = ig.cylinder('drain', 0.019, 0.004, location=(0.0, -0.205, top - 0.127), segments=24, material=chrome)
    drain_hole = ig.cylinder('drain_hole', 0.011, 0.002, location=(0.0, -0.205, top - 0.1245), segments=16,
                             material=dark)
    # 싱글 레버 혼합 수전. 몸통, 앞으로 뻗은 토수구, 위의 레버.
    tap_body = ig.cylinder('tap_body', 0.022, 0.095, location=(0.0, -0.045, top + 0.0475), segments=28,
                           radius_top=0.019, bevel=0.003, bevel_segments=2, material=chrome)
    spout = ig.pipe('spout', [(0.0, -0.045, top + 0.075), (0.0, -0.075, top + 0.080), (0.0, -0.118, top + 0.072),
                              (0.0, -0.128, top + 0.060)], radius=0.010, resolution=16, corner_radius=0.02,
                    material=chrome)
    lever = ig.pipe('tap_lever', [(0.0, -0.040, top + 0.098), (0.0, -0.030, top + 0.112), (0.0, 0.002, top + 0.124)],
                    radius=0.0065, resolution=12, corner_radius=0.012, material=chrome)
    # 병 트랩: 대야 밑 배수구에서 내려와 병에 들어가고 옆으로 벽에 들어간다.
    tail = ig.cylinder('tail_pipe', 0.016, 0.15, location=(0.0, -0.205, under - 0.05), segments=20, material=chrome)
    bottle = ig.cylinder('trap_bottle', 0.032, 0.12, location=(0.0, -0.205, 0.47), segments=28, bevel=0.006,
                         bevel_segments=2, material=chrome)
    waste = ig.pipe('waste_pipe', [(0.0, -0.18, 0.50), (0.0, -0.10, 0.50), (0.0, -0.01, 0.50)],
                    radius=0.016, resolution=16, material=chrome)
    rose = ig.cylinder('waste_rose', 0.034, 0.006, location=(0.0, -0.003, 0.50), rotation=(math.pi * 0.5, 0.0, 0.0),
                       segments=28, material=chrome)
    parts = [body, drain, drain_hole, tap_body, spout, lever, tail, bottle, waste, rose]
    ig.build_asset(
        'SM_BathroomBasin', 'prop', parts, out_root,
        collision_parts=[[body]], texture_size=1024, preview_yaw=25,
        notes=('벽걸이 세면대 45 x 36 cm, 테두리 윗면 82 cm. 원점은 벽면과 바닥, 앞 -Y. 싱글 레버 수전과 '
               '병 트랩. 시안 SourceArt/AI/BathroomBasin_20261007. 충돌은 몸통 하나.'))


def build_mirror_cabinet(out_root):
    """세면대 위 거울장. 원점은 벽면과 장 밑면, 앞 -Y. 폭 60, 높이 70, 깊이 13."""
    ig.reset_scene()
    pvc = ig.mat_plastic('CabinetPVC', (0.80, 0.80, 0.78), roughness=0.32, bump=0.0)
    mirror = mat_mirror()
    glass = ig.mat_plastic('ShelfGlassEdge', (0.48, 0.62, 0.58), roughness=0.08, bump=0.0)
    w, h, d = 0.60, 0.70, 0.13
    shelf_h = 0.14
    body = ig.box('body', (w, d, h), location=(0.0, -d * 0.5, 0.0), origin='bottom', bevel=0.004, segments=2,
                  material=pvc)
    # 아래 열린 칸. 몸통을 파고 안쪽 바닥에 유리 선반 가장자리.
    cut = ig.box('shelf_cut', (w - 0.03, d, shelf_h - 0.02), location=(0.0, -d * 0.5 - 0.012, 0.012), origin='bottom')
    ig.boolean(body, cut)
    glass_shelf = ig.box('glass_shelf', (w - 0.03, d - 0.02, 0.008), location=(0.0, -d * 0.5 - 0.004, 0.016),
                         origin='bottom', bevel=0.002, segments=1, material=glass)
    parts = [body, glass_shelf]
    door_w = (w - 0.006) * 0.5
    door_h = h - shelf_h - 0.006
    for side in (-1.0, 1.0):
        cx = side * (door_w * 0.5 + 0.0015)
        frame = ig.box(f'door_frame_{int(side)}', (door_w, 0.018, door_h), location=(cx, -d - 0.009, shelf_h),
                       origin='bottom', bevel=0.003, segments=2, material=pvc)
        glass_cut = ig.box('glass_cut', (door_w - 0.024, 0.03, door_h - 0.024), location=(cx, -d - 0.02, shelf_h + 0.012),
                           origin='bottom')
        ig.boolean(frame, glass_cut)
        pane = ig.box(f'mirror_{int(side)}', (door_w - 0.024, 0.004, door_h - 0.024),
                      location=(cx, -d - 0.006, shelf_h + 0.012), origin='bottom', material=mirror)
        parts += [frame, pane]
    ig.build_asset(
        'SM_BathroomMirrorCabinet', 'prop', parts, out_root,
        collision_parts=[[body]], texture_size=1024, preview_yaw=20,
        notes=('세면대 위 거울장 60 x 70 x 13 cm, 거울 문 둘과 아래 열린 칸. 원점은 벽면과 장 밑면, 앞 -Y. '
               '거울은 금속 1·거칠기 0.03으로 굽는다. 시안 SourceArt/AI/BathroomMirrorCabinet_20261007.'))


def build_shower(out_root):
    """벽걸이 샤워 수전, 호스, 걸이, 손 샤워기. 원점은 벽면과 바닥, 앞 -Y. 수전 가운데 높이 1 m."""
    ig.reset_scene()
    chrome = mat_chrome()
    nozzle = ig.mat_plastic('NozzlePlate', (0.10, 0.10, 0.10), roughness=0.45, bump=0.0)
    hose_mat = ig.mat_metal('HoseSteel', (0.78, 0.78, 0.77), roughness=0.22, streak=0.10)
    z = 1.00
    parts = []
    # 벽에서 나온 편심 다리 둘과 벽 덮개.
    for x in (-0.075, 0.075):
        parts.append(ig.cylinder('leg', 0.011, 0.055, location=(x, -0.0275, z), rotation=(math.pi * 0.5, 0.0, 0.0),
                                 segments=20, material=chrome))
        parts.append(ig.cylinder('cover', 0.030, 0.012, location=(x, -0.008, z), rotation=(math.pi * 0.5, 0.0, 0.0),
                                 segments=28, bevel=0.002, bevel_segments=1, material=chrome))
    # 가로 막대 몸통과 양 끝 손잡이.
    parts.append(ig.cylinder('bar', 0.023, 0.27, location=(0.0, -0.068, z), rotation=(0.0, math.pi * 0.5, 0.0),
                             segments=32, material=chrome))
    for x in (-0.152, 0.152):
        knob = ig.cylinder('knob', 0.026, 0.035, location=(x, -0.068, z), rotation=(0.0, math.pi * 0.5, 0.0),
                           segments=24, bevel=0.003, bevel_segments=2, material=chrome)
        parts.append(knob)
    # 가운데 아래로 내린 토수구.
    parts.append(ig.pipe('spout', [(0.0, -0.075, z - 0.015), (0.0, -0.095, z - 0.035), (0.0, -0.118, z - 0.048)],
                         radius=0.011, resolution=14, corner_radius=0.015, material=chrome))
    # 호스: 막대 오른쪽 아래 출구에서 아래로 늘어졌다가 걸이의 손 샤워기로 올라간다.
    outlet = (0.06, -0.070, z - 0.022)
    parts.append(ig.cylinder('outlet', 0.010, 0.02, location=(outlet[0], outlet[1], outlet[2] - 0.006), segments=16,
                             material=chrome))
    hose_path = [(0.06, -0.070, z - 0.03), (0.07, -0.080, z - 0.20), (0.11, -0.10, z - 0.38),
                 (0.17, -0.11, z - 0.40), (0.21, -0.10, z - 0.25), (0.20, -0.085, z + 0.15),
                 (0.17, -0.075, z + 0.42)]
    parts.append(ig.pipe('hose', hose_path, radius=0.0065, resolution=10, corner_radius=0.06, corner_steps=8,
                         material=hose_mat))
    # 벽 걸이와 손 샤워기. 손잡이는 걸이에 비스듬히 꽂혀 머리가 앞으로 숙인다.
    hook_z = z + 0.55
    parts.append(ig.cylinder('hook_base', 0.020, 0.012, location=(0.15, -0.006, hook_z),
                             rotation=(math.pi * 0.5, 0.0, 0.0), segments=24, material=chrome))
    parts.append(ig.box('hook_arm', (0.030, 0.050, 0.022), location=(0.15, -0.035, hook_z), bevel=0.006, segments=2,
                        material=chrome))
    handle_start = Vector((0.16, -0.065, hook_z - 0.10))
    handle_end = Vector((0.15, -0.072, hook_z + 0.10))
    direction = handle_end - handle_start
    pitch = math.atan2(direction.y, direction.z)
    parts.append(ig.cylinder('handle', 0.014, direction.length, location=tuple((handle_start + handle_end) * 0.5),
                             rotation=(-pitch, 0.0, 0.0), segments=24, radius_top=0.018, material=chrome))
    # 머리는 손잡이 위에서 앞으로 숙여 물이 앞아래로 떨어지게 걸린다(축을 X로 135도).
    tilt = math.radians(135.0)
    normal = Vector((0.0, -math.sin(tilt), math.cos(tilt)))
    head_center = handle_end + Vector((0.0, -0.022, 0.022))
    parts.append(ig.cylinder('head', 0.050, 0.022, location=tuple(head_center), rotation=(tilt, 0.0, 0.0),
                             segments=40, bevel=0.004, bevel_segments=2, material=chrome))
    parts.append(ig.cylinder('nozzles', 0.040, 0.002, location=tuple(head_center + normal * 0.0115),
                             rotation=(tilt, 0.0, 0.0), segments=36, material=nozzle))
    ig.build_asset(
        'SM_BathroomShower', 'prop', parts, out_root, collision_parts=[], texture_size=512, preview_yaw=25,
        notes=('벽걸이 샤워 수전(가운데 높이 1 m)과 호스, 벽 걸이의 손 샤워기(1.55 m). 원점은 벽면과 바닥, 앞 -Y. '
               '시안 SourceArt/AI/BathroomShower_20261007. 충돌 없음.'))


DOOR_W, DOOR_T, DOOR_Z0, DOOR_H = 0.70, 0.036, 0.035, 1.99
DOOR_LEVER_X = -DOOR_W * 0.5 + 0.065
DOOR_LEVER_Z = 0.95


def build_door(out_root):
    """욕실 문짝. ABS 민짝에 세로 판 둘, 아래 루버. 경첩 +X, 앞면 -Y가 403호 쪽, 원점 바닥 가운데."""
    ig.reset_scene()
    abs_white = ig.mat_painted_steel('DoorABS', (0.74, 0.72, 0.66), roughness=0.36, wear=0.45, bump=0.02,
                                     dirt_color=(0.44, 0.42, 0.37))
    chrome = mat_chrome()
    dark = ig.mat_plastic('LouvreShadow', (0.05, 0.05, 0.05), roughness=0.6, bump=0.0)
    slab = ig.box('slab', (DOOR_W, DOOR_T, DOOR_H), location=(0.0, 0.0, DOOR_Z0), origin='bottom', bevel=0.003,
                  segments=2, material=abs_white)
    # 양면의 얕은 세로 판 둘. 3 mm 파인 홈 테두리로 판을 그린다.
    for face_sign in (-1.0, 1.0):
        for px in (-0.155, 0.155):
            groove = ig.box('panel_cut', (0.22, 0.008, 1.32), location=(px, face_sign * DOOR_T * 0.5, DOOR_Z0 + 0.52),
                            origin='bottom')
            ig.boolean(slab, groove)
            raised = ig.box('panel', (0.204, 0.008, 1.304), location=(px, face_sign * (DOOR_T * 0.5 - 0.005),
                                                                       DOOR_Z0 + 0.528), origin='bottom',
                            bevel=0.002, segments=1, material=abs_white)
            ig.boolean(slab, raised, 'UNION')
    # 아래 루버: 문짝을 뚫은 구멍에 비스듬한 살 여섯.
    vent_w, vent_z0, vent_h = 0.46, DOOR_Z0 + 0.09, 0.17
    hole = ig.box('vent_hole', (vent_w, DOOR_T * 2.0, vent_h), location=(0.0, 0.0, vent_z0), origin='bottom')
    ig.boolean(slab, hole)
    parts = [slab]
    for index in range(6):
        slat_z = vent_z0 + 0.016 + index * 0.028
        parts.append(ig.box('slat', (vent_w, 0.004, 0.034), location=(0.0, 0.0, slat_z),
                            rotation=(math.radians(-38.0), 0.0, 0.0), material=abs_white))
    # 레버 양쪽. 욕실 쪽(+Y) 로제트 가운데에 누름 잠금 단추, 403호 쪽에는 비상 해제 홈.
    for face_sign in (-1.0, 1.0):
        face = face_sign * DOOR_T * 0.5
        parts.append(ig.cylinder('rose', 0.026, 0.008, location=(DOOR_LEVER_X, face + face_sign * 0.004, DOOR_LEVER_Z),
                                 rotation=(math.pi * 0.5, 0.0, 0.0), segments=32, bevel=0.0015, bevel_segments=2,
                                 material=chrome))
        parts.append(ig.cylinder('neck', 0.009, 0.040, location=(DOOR_LEVER_X, face + face_sign * 0.026, DOOR_LEVER_Z),
                                 rotation=(math.pi * 0.5, 0.0, 0.0), segments=20, material=chrome))
        parts.append(ig.pipe('lever', [
            (DOOR_LEVER_X, face + face_sign * 0.046, DOOR_LEVER_Z),
            (DOOR_LEVER_X + 0.018, face + face_sign * 0.053, DOOR_LEVER_Z),
            (DOOR_LEVER_X + 0.105, face + face_sign * 0.053, DOOR_LEVER_Z - 0.004),
            (DOOR_LEVER_X + 0.116, face + face_sign * 0.049, DOOR_LEVER_Z - 0.010),
        ], radius=0.0095, resolution=14, corner_radius=0.011, material=chrome))
    parts.append(ig.cylinder('lock_button', 0.0065, 0.010,
                             location=(DOOR_LEVER_X + 0.024, DOOR_T * 0.5 + 0.010, DOOR_LEVER_Z + 0.040),
                             rotation=(math.pi * 0.5, 0.0, 0.0), segments=16, bevel=0.001, bevel_segments=1,
                             material=chrome))
    parts.append(ig.box('release_slot', (0.010, 0.002, 0.0025),
                        location=(DOOR_LEVER_X + 0.024, -DOOR_T * 0.5 - 0.0085, DOOR_LEVER_Z + 0.040), material=dark))
    for z in (0.22, 1.82):
        parts.append(ig.cylinder('hinge', 0.007, 0.09, location=(DOOR_W * 0.5 + 0.002, 0.008, z), segments=16,
                                 material=chrome))
    ig.build_asset(
        'SM_BathroomDoorLeaf', 'prop', parts, out_root, collision_parts=[[slab]], texture_size=2048, preview_yaw=30,
        notes=('욕실 문짝 70 x 199 x 3.6 cm(바닥에서 3.5 cm 띄움). 경첩 +X, 앞면 -Y가 403호 쪽, 원점 바닥 가운데. '
               'AIGSwingDoor::ConfigureAuthoredLeaf로 단다. 아래 루버로 안쪽 불빛이 샌다. 욕실 쪽 로제트에 '
               f'누름 잠금 단추(문짝 기준 X {DOOR_LEVER_X + 0.024:.3f}, Z {DOOR_LEVER_Z + 0.04:.2f}).'))


def build_frame(out_root):
    """욕실 문틀. 개구부 76 x 205, 벽 두께 20 cm를 덮는 문선과 양면 몰딩, 문 닫힘 턱.
    원점은 개구부 바닥 가운데. 개구부 폭이 X, 벽 두께가 Y(-Y가 403호 쪽)."""
    ig.reset_scene()
    paint = ig.mat_painted_steel('FrameWhite', (0.76, 0.74, 0.69), roughness=0.38, wear=0.30, bump=0.02,
                                 dirt_color=(0.46, 0.44, 0.40))
    width, height, depth = 0.76, 2.05, 0.20
    lining = 0.018
    casing_w, casing_t = 0.06, 0.012
    parts = []
    for side in (-1.0, 1.0):
        parts.append(ig.box('jamb', (lining, depth, height), location=(side * (width * 0.5 - lining * 0.5), 0.0, 0.0),
                            origin='bottom', bevel=0.002, segments=1, material=paint))
        for face in (-1.0, 1.0):
            parts.append(ig.box('casing', (casing_w, casing_t, height + casing_w),
                                location=(side * (width * 0.5 + casing_w * 0.5 - 0.004),
                                          face * (depth * 0.5 + casing_t * 0.5), 0.0),
                                origin='bottom', bevel=0.003, segments=2, material=paint))
        # 문 닫힘 턱. 문짝은 욕실 쪽(+Y)으로 열리므로 턱은 문짝의 403호 쪽 앞에 선다.
        parts.append(ig.box('stop', (0.012, 0.030, height - 0.02), location=(side * (width * 0.5 - lining - 0.006),
                                                                           -0.060, 0.0),
                            origin='bottom', bevel=0.002, segments=1, material=paint))
    parts.append(ig.box('head', (width, depth, lining), location=(0.0, 0.0, height - lining), origin='bottom',
                        bevel=0.002, segments=1, material=paint))
    for face in (-1.0, 1.0):
        parts.append(ig.box('head_casing', (width + casing_w * 2.0 - 0.008, casing_t, casing_w),
                            location=(0.0, face * (depth * 0.5 + casing_t * 0.5), height), origin='bottom',
                            bevel=0.003, segments=2, material=paint))
    parts.append(ig.box('head_stop', (width - 2.0 * lining, 0.030, 0.012), location=(0.0, -0.060, height - lining - 0.012),
                        origin='bottom', bevel=0.002, segments=1, material=paint))
    ig.build_asset(
        'SM_BathroomDoorFrame', 'prop', parts, out_root,
        collision_parts=[[parts[0]], [parts[4]]], texture_size=1024,
        notes='욕실 문틀. 개구부 76 x 205, 벽 두께 20. 원점 개구부 바닥 가운데, 폭 X, 두께 Y(-Y가 403호 쪽).')


def build_towel(out_root):
    """크롬 수건걸이와 걸쳐 둔 회색 수건. 원점은 벽면과 바닥, 앞 -Y. 걸이 높이 1.15 m."""
    ig.reset_scene()
    chrome = mat_chrome()
    terry = mat_terry('TowelGrey', (0.30, 0.31, 0.32))
    rail_z, rail_y, length = 1.15, -0.065, 0.50
    parts = [ig.cylinder('rail', 0.009, length, location=(0.0, rail_y, rail_z), rotation=(0.0, math.pi * 0.5, 0.0),
                         segments=20, material=chrome)]
    for x in (-length * 0.5 + 0.02, length * 0.5 - 0.02):
        parts.append(ig.pipe('post', [(x, -0.004, rail_z), (x, rail_y, rail_z)], radius=0.010, resolution=12,
                             material=chrome))
        parts.append(ig.cylinder('flange', 0.024, 0.008, location=(x, -0.004, rail_z),
                                 rotation=(math.pi * 0.5, 0.0, 0.0), segments=24, material=chrome))
    # 수건: 걸이에 반 접어 걸쳤다. 앞자락이 조금 길고 아래가 살짝 벌어진다.
    bm = bmesh.new()
    width, thick = 0.34, 0.006
    columns = 12
    profile = []
    for step in range(9):
        angle = math.pi * step / 8.0
        profile.append((rail_y + 0.0125 * math.cos(angle), rail_z + 0.0125 * math.sin(angle) + 0.004))
    front = [(rail_y - 0.0125 - 0.004 * t, rail_z - 0.48 * t) for t in [k / 8.0 for k in range(1, 9)]]
    back = [(rail_y + 0.0125 + 0.006 * t, rail_z - 0.40 * t) for t in [k / 8.0 for k in range(1, 9)]]
    path = list(reversed(back)) + profile + front
    grid = []
    for column in range(columns + 1):
        x = -width * 0.5 + width * column / columns
        row = []
        for y, z in path:
            wobble = 0.004 * math.sin(column * 1.7 + z * 21.0)
            row.append(bm.verts.new((x, y + wobble, z)))
        grid.append(row)
    for column in range(columns):
        for index in range(len(path) - 1):
            bm.faces.new((grid[column][index], grid[column + 1][index], grid[column + 1][index + 1],
                          grid[column][index + 1]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    towel = ig._link(bpy.data.objects.new('towel', ig._mesh_from_bm('towel', bm)))
    ig.assign_material(towel, terry)
    solid = towel.modifiers.new('Thick', 'SOLIDIFY')
    solid.thickness = thick
    ig.apply_modifiers(towel)
    towel = smooth(towel, 1)
    parts.append(towel)
    ig.build_asset(
        'SM_BathroomTowelRail', 'prop', parts, out_root, collision_parts=[], texture_size=1024, preview_yaw=30,
        notes='크롬 수건걸이 50 cm(높이 1.15 m)와 반 접어 걸친 회색 수건. 원점은 벽면과 바닥, 앞 -Y. 충돌 없음.')


def build_shelf(out_root):
    """흰 플라스틱 코너 선반과 병 둘. 원점은 두 벽이 만나는 모서리 바닥, 선반은 x<0, y>0 쪽."""
    ig.reset_scene()
    plastic = ig.mat_plastic('ShelfWhite', (0.82, 0.82, 0.80), roughness=0.30, bump=0.0)
    bottle_a = ig.mat_plastic('BottleCream', (0.80, 0.77, 0.68), roughness=0.28, bump=0.0)
    bottle_b = ig.mat_plastic('BottleTeal', (0.20, 0.42, 0.44), roughness=0.22, bump=0.0)
    cap = ig.mat_plastic('CapWhite', (0.86, 0.86, 0.84), roughness=0.35, bump=0.0)
    shelf_z, radius = 1.38, 0.21
    bm = bmesh.new()
    steps = 18
    top = [bm.verts.new((0.0, 0.0, shelf_z + 0.012))]
    bottom = [bm.verts.new((0.0, 0.0, shelf_z))]
    for step in range(steps + 1):
        angle = math.pi * 0.5 * step / steps
        x, y = -radius * math.cos(angle), radius * math.sin(angle)
        top.append(bm.verts.new((x, y, shelf_z + 0.012)))
        bottom.append(bm.verts.new((x, y, shelf_z)))
    bm.faces.new(top)
    bm.faces.new(list(reversed(bottom)))
    for index in range(1, steps + 1):
        bm.faces.new((bottom[index], bottom[index + 1], top[index + 1], top[index]))
    bm.faces.new((bottom[0], bottom[1], top[1], top[0]))
    bm.faces.new((bottom[-1], bottom[0], top[0], top[-1]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    board = ig._link(bpy.data.objects.new('board', ig._mesh_from_bm('board', bm)))
    ig.assign_material(board, plastic)
    lip = ig.pipe('lip', [(-radius * math.cos(math.pi * 0.5 * k / 12), radius * math.sin(math.pi * 0.5 * k / 12),
                           shelf_z + 0.02) for k in range(13)], radius=0.005, resolution=8, material=plastic)
    parts = [board, lip]
    # 펌프 병과 뚜껑 달린 병.
    pump = ig.lathe('pump_bottle', [(0.0, 0.0), (0.030, 0.0), (0.032, 0.006), (0.032, 0.150), (0.026, 0.168),
                                    (0.012, 0.176), (0.0, 0.176)], segments=32,
                    location=(-0.060, 0.050, shelf_z + 0.012), material=bottle_a)
    pump_head = ig.cylinder('pump_head', 0.010, 0.040, location=(-0.060, 0.050, shelf_z + 0.208), segments=16,
                            material=cap)
    nozzle = ig.box('pump_nozzle', (0.010, 0.030, 0.008), location=(-0.060, 0.034, shelf_z + 0.226), material=cap)
    flip = ig.lathe('flip_bottle', [(0.0, 0.0), (0.026, 0.0), (0.028, 0.005), (0.028, 0.140), (0.020, 0.152),
                                    (0.0, 0.152)], segments=32, location=(-0.130, 0.060, shelf_z + 0.012),
                    material=bottle_b)
    flip_cap = ig.cylinder('flip_cap', 0.020, 0.024, location=(-0.130, 0.060, shelf_z + 0.176), segments=24,
                           bevel=0.003, bevel_segments=1, material=cap)
    parts += [pump, pump_head, nozzle, flip, flip_cap]
    ig.build_asset(
        'SM_BathroomCornerShelf', 'prop', parts, out_root, collision_parts=[], texture_size=512,
        notes='흰 플라스틱 코너 선반(높이 1.38 m, 반지름 21 cm)과 이름표 없는 병 둘. 원점은 모서리 바닥. 충돌 없음.')


def build_drain(out_root):
    """스테인리스 사각 배수구 12 x 12 cm. 원점은 바닥면 가운데, 판은 바닥 위로 2 mm."""
    ig.reset_scene()
    steel = ig.mat_metal('DrainSteel', (0.72, 0.72, 0.70), roughness=0.30, streak=0.05, anisotropic=False)
    dark = ig.mat_plastic('DrainVoid', (0.02, 0.02, 0.02), roughness=0.6, bump=0.0)
    plate = ig.box('plate', (0.12, 0.12, 0.002), location=(0.0, 0.0, 0.0), origin='bottom', bevel=0.0008, segments=1,
                   material=steel)
    for index in range(7):
        y = -0.042 + index * 0.014
        slot = ig.box('slot', (0.080, 0.006, 0.01), location=(0.0, y, -0.004), origin='bottom')
        ig.boolean(plate, slot)
    void = ig.box('void', (0.085, 0.10, 0.001), location=(0.0, 0.0, -0.0005), origin='bottom', material=dark)
    ig.build_asset('SM_FloorDrain', 'prop', [plate, void], out_root, collision_parts=[], texture_size=256,
                   notes='스테인리스 사각 배수구 12 x 12 cm, 바닥 위 2 mm. 원점 바닥면 가운데. 충돌 없음.')


def build_vent(out_root):
    """천장 환풍구 덮개 24 x 24 cm. 원점은 천장면 가운데, 아래(-Z)로 1.2 cm."""
    ig.reset_scene()
    plastic = ig.mat_plastic('VentWhite', (0.80, 0.80, 0.78), roughness=0.35, bump=0.0)
    dark = ig.mat_plastic('VentDark', (0.03, 0.03, 0.03), roughness=0.6, bump=0.0)
    frame = ig.box('frame', (0.24, 0.24, 0.012), location=(0.0, 0.0, -0.012), origin='bottom', bevel=0.003, segments=2,
                   material=plastic)
    hole = ig.box('hole', (0.17, 0.17, 0.05), location=(0.0, 0.0, -0.03), origin='bottom')
    ig.boolean(frame, hole)
    parts = [frame, ig.box('duct', (0.17, 0.17, 0.002), location=(0.0, 0.0, -0.002), origin='bottom', material=dark)]
    for index in range(9):
        x = -0.076 + index * 0.019
        parts.append(ig.box('vane', (0.004, 0.17, 0.010), location=(x, 0.0, -0.010), origin='bottom',
                            rotation=(0.0, math.radians(30.0), 0.0), material=plastic))
    ig.build_asset('SM_BathroomVentGrille', 'prop', parts, out_root, collision_parts=[], texture_size=256,
                   notes='천장 환풍구 덮개 24 x 24 cm. 원점은 천장면 가운데, 아래로 1.2 cm. 충돌 없음.')


def main():
    out_root = ig.out_root_from_argv()
    only = sys.argv[sys.argv.index('--') + 2:] if '--' in sys.argv else []
    steps = [('toilet', build_toilet), ('basin', build_basin), ('mirror', build_mirror_cabinet),
             ('shower', build_shower), ('door', build_door), ('frame', build_frame), ('towel', build_towel),
             ('shelf', build_shelf), ('drain', build_drain), ('vent', build_vent)]
    for key, step in steps:
        if not only or key in only:
            step(out_root)


main()
