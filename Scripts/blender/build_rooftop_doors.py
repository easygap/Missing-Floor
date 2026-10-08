"""옥상으로 나가는 두 철문과 5층 철문 안쪽 빗장.

- SM_RooftopDoorLeaf: 계단실 꼭대기의 옥상 철문(85 cm). 계단 쪽에 경첩과 열쇠 구멍.
- SM_RooftopDoorSign: 옥상 철문 계단 쪽 안내판 「옥상 창고 / 관계자 외 출입금지」.
- SM_AnnexDoorLeaf: 옥상에서 5층 증축부로 들어가는 철문(88 cm). 옥상 쪽에 열쇠 구멍과
  호수 표찰을 떼어 낸 자리, 안쪽에 경첩.
- SM_DoorBarrelBolt, SM_DoorBarrelBoltPin, SM_DoorBarrelBoltKeeper: 5층 철문 안쪽 빗장의
  몸통, 미는 막대, 문설주 옆면의 받이쇠.

기준: Content/SourceArt/AI/RooftopDoor_20261007.png(옥상 철문 계단 쪽과 칠),
AnnexDoorOutside_20261007.png(5층 철문 옥상 쪽), DoorBarrelBolt_20261007.png(빗장),
RooftopDoorSign_20261007.png(안내판 인쇄면).

둘 다 창 없는 강판 민짝에 짙은 회녹색 에나멜을 칠한 옛 빌라 철문이다. 긴 손잡이판에 일자
레버를 달았고, 열쇠로 여는 쪽에만 판 아래에 원형 열쇠 구멍이 있다. 비를 맞는 옥상 쪽 면은
위쪽이 하얗게 바래고 빗물 자국이 내려오며, 아래 가장자리에 녹이 올라온다.

문짝 원점은 바닥 중심이고 경첩이 +X, 앞면 -Y다(방화문과 같은 관례). 씬은 두 문 모두 yaw
-90으로 달아서 앞면 -Y가 북쪽을 본다. 그래서 옥상 철문은 -Y가 옥상, +Y가 계단 쪽이고,
5층 철문은 -Y가 증축부 안, +Y가 옥상 쪽이다. 두 문 다 옥상에서 안쪽으로 열린다.

빗장 세 부품은 미는 방향이 +X이고 몸통이 문 면(y=0)에서 +Y로 솟는다. 받이쇠는 문설주
옆면(x=0)에 붙어 -X로 나온다. 막대는 몸통과 원점이 같고 풀린 자리에서 만든다.

    blender -b --factory-startup --python Scripts/blender/build_rooftop_doors.py -- <out_dir> [roof|annex|sign|bolt ...]
"""
import math
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import ig_blender_lib as ig  # noqa: E402

AI = os.path.join(HERE, '../../Content/SourceArt/AI')

T = 0.045                      # 문짝 두께
Z0, Z1 = 0.010, 2.045          # 바닥 틈 1 cm, 문틀 머리(2.05)까지 5 mm
LEVER_Z = 1.00
PLATE_Z = 0.955                # 손잡이판 가운데
PLATE_H, PLATE_W = 0.245, 0.048
KEY_Z = 0.885
PAINT = (0.072, 0.098, 0.093)  # 시안 가운데에서 잰 칠(선형)

# 빗장. 받이쇠까지의 거리는 씬이 맞춘다(문짝 끝 2 cm 틈 + 받이쇠 1.65 cm).
BOLT_PIN_Y = 0.0105
BOLT_TRAVEL = 0.045


def _math(nodes, links, operation, a, b=None, clamp=False):
    node = nodes.new('ShaderNodeMath')
    node.operation = operation
    node.use_clamp = clamp
    if isinstance(a, float):
        node.inputs[0].default_value = a
    else:
        links.new(a, node.inputs[0])
    if b is not None:
        if isinstance(b, float):
            node.inputs[1].default_value = b
        else:
            links.new(b, node.inputs[1])
    return node.outputs[0]


def _ramp(nodes, links, socket, lo, hi):
    """lo에서 0, hi에서 1. 거꾸로 주면 내려가는 경사다."""
    node = ig._map_range(nodes, links, socket, lo, hi, 0.0, 1.0)
    node.clamp = True
    return node.outputs['Result']


def _mix(nodes, links, fac, color_a, color_b):
    node = nodes.new('ShaderNodeMixRGB')
    links.new(fac, node.inputs['Fac'])
    if isinstance(color_a, tuple):
        node.inputs['Color1'].default_value = (*color_a, 1.0)
    else:
        links.new(color_a, node.inputs['Color1'])
    if isinstance(color_b, tuple):
        node.inputs['Color2'].default_value = (*color_b, 1.0)
    else:
        links.new(color_b, node.inputs['Color2'])
    return node.outputs[0]


def door_paint(name, width, weathered, scar=None):
    """짙은 회녹색 에나멜. weathered면 비 맞는 면의 칠이다.

    - 비 맞는 면: 위로 갈수록 하얗게 바랜 분필 칠, 위 가장자리에서 내려오는 빗물 자국, 녹이 더 많다.
    - 두 면 모두: 손잡이 둘레와 그 위 손때, 아래 가장자리의 녹, 모서리 칠 벗겨짐.
    - scar=(x, z, w, h): 호수 표찰을 떼어 낸 자리. 덜 바랜 칠과 접착 자국 테두리.

    어느 면이 비를 맞는지는 재질이 아니라 면에 붙인 재질 칸이 정한다. 굽기 전에 빌더가 Y를
    뒤집으므로(FBX 좌표) 법선이나 물체 좌표의 Y로 면을 고르면 반대쪽에 구워진다.
    """
    mat = bpy.data.materials.new(name)
    bsdf = ig._principled(mat)
    tree, nodes, links = ig._nodes(mat)
    coords = ig._object_coords(nodes)
    sep = nodes.new('ShaderNodeSeparateXYZ')
    links.new(coords, sep.inputs['Vector'])
    X, Z = sep.outputs['X'], sep.outputs['Z']
    weather = 1.0 if weathered else 0.0
    lever_x = -width * 0.5 + 0.075

    tone = ig._noise(nodes, links, 2.6, detail=3.0, roughness=0.55, coords=coords)
    tone_mul = ig._map_range(nodes, links, tone.outputs['Fac'], 0.35, 0.65, 0.92, 1.07)
    base = nodes.new('ShaderNodeMixRGB')
    base.blend_type = 'MULTIPLY'
    base.inputs['Fac'].default_value = 1.0
    base.inputs['Color1'].default_value = (*PAINT, 1.0)
    links.new(tone_mul.outputs['Result'], base.inputs['Color2'])
    color = base.outputs[0]

    # 볕에 바랜 분필 칠. 80 cm 위부터 고르게 올라온다. 얼룩은 크고 약하게만 준다.
    chalk_noise = ig._noise(nodes, links, 1.4, detail=2.0, roughness=0.5, coords=coords)
    chalk_patch = _ramp(nodes, links, chalk_noise.outputs['Fac'], 0.40, 0.60)
    chalk = _math(nodes, links, 'MULTIPLY', _ramp(nodes, links, Z, 0.8, 2.0), weather)
    chalk = _math(nodes, links, 'MULTIPLY', chalk, _math(nodes, links, 'ADD', 0.45, _math(nodes, links, 'MULTIPLY', chalk_patch, 0.20)))
    color = _mix(nodes, links, chalk, color, (0.170, 0.198, 0.192))

    # 빗물 자국: 위 가장자리에서 내려오는 가는 세로 줄. X로만 잘게 바뀌는 노이즈를 좁게 자른다.
    runs_map = nodes.new('ShaderNodeMapping')
    runs_map.inputs['Scale'].default_value = (26.0, 1.0, 0.45)
    links.new(coords, runs_map.inputs['Vector'])
    runs_noise = ig._noise(nodes, links, 1.0, detail=1.0, roughness=0.4, coords=runs_map.outputs['Vector'])
    runs_line = _ramp(nodes, links, runs_noise.outputs['Fac'], 0.56, 0.61)
    runs_fade = _math(nodes, links, 'MULTIPLY', _ramp(nodes, links, Z, 0.75, 1.98), _ramp(nodes, links, Z, Z1, Z1 - 0.02))
    runs = _math(nodes, links, 'MULTIPLY', _math(nodes, links, 'MULTIPLY', runs_line, runs_fade), weather)
    color = _mix(nodes, links, _math(nodes, links, 'MULTIPLY', runs, 0.35), color, (0.205, 0.225, 0.220))

    # 잔 긁힘. 가로로 늘인 노이즈를 아주 좁게 잘라 가는 선만 남기고, 열쇠 구멍 둘레에 몰린다.
    scratch_map = nodes.new('ShaderNodeMapping')
    scratch_map.inputs['Rotation'].default_value = (0.0, 0.35, 0.0)
    scratch_map.inputs['Scale'].default_value = (1.5, 1.0, 70.0)
    links.new(coords, scratch_map.inputs['Vector'])
    scratch_noise = ig._noise(nodes, links, 1.0, detail=1.0, roughness=0.3, coords=scratch_map.outputs['Vector'])
    scratch_line = _ramp(nodes, links, _math(nodes, links, 'ABSOLUTE', _math(nodes, links, 'SUBTRACT', scratch_noise.outputs['Fac'], 0.5)), 0.012, 0.0)
    scratch_area = ig._noise(nodes, links, 3.0, detail=1.0, roughness=0.4, coords=coords)
    scratches = _math(nodes, links, 'MULTIPLY', scratch_line, _ramp(nodes, links, scratch_area.outputs['Fac'], 0.56, 0.66))

    # 손때: 손잡이판 둘레와 그 위 어깨 높이. 노이즈로 끊는다.
    def gaussian(axis, center, spread):
        delta = _math(nodes, links, 'DIVIDE', _math(nodes, links, 'SUBTRACT', axis, center), spread)
        return _math(nodes, links, 'EXPONENT', _math(nodes, links, 'MULTIPLY', _math(nodes, links, 'MULTIPLY', delta, delta), -1.0))

    smudge_noise = ig._noise(nodes, links, 9.0, detail=3.0, roughness=0.6, coords=coords)
    smudge_cut = ig._map_range(nodes, links, smudge_noise.outputs['Fac'], 0.35, 0.68, 0.2, 1.0)
    around = _math(nodes, links, 'MULTIPLY', gaussian(X, lever_x + 0.03, 0.10), gaussian(Z, PLATE_Z + 0.04, 0.16))
    above = _math(nodes, links, 'MULTIPLY', gaussian(X, lever_x + 0.05, 0.08), gaussian(Z, 1.30, 0.13))
    hands = _math(nodes, links, 'ADD', _math(nodes, links, 'MULTIPLY', around, 0.7), _math(nodes, links, 'MULTIPLY', above, 0.55), clamp=True)
    hands = _math(nodes, links, 'MULTIPLY', hands, smudge_cut.outputs['Result'])
    color = _mix(nodes, links, hands, color, (0.032, 0.040, 0.038))
    key_area = _math(nodes, links, 'MULTIPLY', gaussian(X, lever_x, 0.035), gaussian(Z, KEY_Z, 0.05))
    scratches = _math(nodes, links, 'ADD', _math(nodes, links, 'MULTIPLY', scratches, 0.45),
                      _math(nodes, links, 'MULTIPLY', _math(nodes, links, 'MULTIPLY', scratch_line, key_area), 0.9), clamp=True)
    color = _mix(nodes, links, scratches, color, (0.21, 0.225, 0.22))

    # 아래 가장자리의 녹. 들쭉날쭉한 선까지 올라오고 양옆 모서리를 타고 더 오른다. 그 안에서도
    # 칠이 남은 데가 있다. 비 맞는 면이 더 심하다.
    rust_noise = ig._noise(nodes, links, 6.0, detail=4.0, roughness=0.6, coords=coords)
    rust_reach = _math(nodes, links, 'ADD', 0.025, _math(nodes, links, 'MULTIPLY', _ramp(nodes, links, rust_noise.outputs['Fac'], 0.36, 0.64), 0.17))
    side = _ramp(nodes, links, _math(nodes, links, 'ABSOLUTE', X), width * 0.5 - 0.07, width * 0.5 - 0.01)
    climb_noise = ig._noise(nodes, links, 3.0, detail=3.0, roughness=0.6, coords=coords)
    rust_reach = _math(nodes, links, 'ADD', rust_reach, _math(nodes, links, 'MULTIPLY', side, _math(nodes, links, 'MULTIPLY', _ramp(nodes, links, climb_noise.outputs['Fac'], 0.40, 0.62), 0.45)))
    rust = _ramp(nodes, links, _math(nodes, links, 'SUBTRACT', rust_reach, Z), 0.0, 0.025)
    patch_noise = ig._noise(nodes, links, 18.0, detail=3.0, roughness=0.6, coords=coords)
    rust = _math(nodes, links, 'MULTIPLY', rust, _ramp(nodes, links, patch_noise.outputs['Fac'], 0.30, 0.50))
    rust = _math(nodes, links, 'MULTIPLY', rust, _math(nodes, links, 'ADD', 0.55, _math(nodes, links, 'MULTIPLY', weather, 0.45)))
    rust_tone = ig._noise(nodes, links, 24.0, detail=2.0, roughness=0.5, coords=coords)
    rust_color = _mix(nodes, links, _ramp(nodes, links, rust_tone.outputs['Fac'], 0.38, 0.62), (0.060, 0.028, 0.015), (0.190, 0.078, 0.030))
    color = _mix(nodes, links, rust, color, rust_color)

    # 모서리 3 cm 안의 칠 벗겨짐. 손톱만 한 조각이 아래쪽에 몰린다.
    edge_x = _ramp(nodes, links, _math(nodes, links, 'ABSOLUTE', X), width * 0.5 - 0.03, width * 0.5 - 0.004)
    edge_top = _ramp(nodes, links, Z, Z1 - 0.03, Z1 - 0.004)
    low = _ramp(nodes, links, Z, 1.2, 0.3)
    edge = _math(nodes, links, 'ADD', _math(nodes, links, 'MULTIPLY', edge_x, _math(nodes, links, 'ADD', low, 0.25)), edge_top, clamp=True)
    chips_noise = ig._noise(nodes, links, 8.0, detail=0.0, roughness=0.3, coords=coords)
    chips = _math(nodes, links, 'MULTIPLY', edge, _ramp(nodes, links, chips_noise.outputs['Fac'], 0.69, 0.71))
    color = _mix(nodes, links, chips, color, (0.130, 0.055, 0.024))

    roughness = _math(nodes, links, 'ADD', 0.42, _math(nodes, links, 'MULTIPLY', chalk, 0.30))
    roughness = _math(nodes, links, 'ADD', roughness, _math(nodes, links, 'MULTIPLY', rust, 0.40))
    roughness = _math(nodes, links, 'SUBTRACT', roughness, _math(nodes, links, 'MULTIPLY', hands, 0.12))
    roughness = _math(nodes, links, 'SUBTRACT', roughness, _math(nodes, links, 'MULTIPLY', scratches, 0.15))

    if scar:
        sx, sz, sw, sh = scar
        inside_x = _ramp(nodes, links, _math(nodes, links, 'ABSOLUTE', _math(nodes, links, 'SUBTRACT', X, sx)), sw * 0.5, sw * 0.5 - 0.002)
        inside_z = _ramp(nodes, links, _math(nodes, links, 'ABSOLUTE', _math(nodes, links, 'SUBTRACT', Z, sz)), sh * 0.5, sh * 0.5 - 0.002)
        plate = _math(nodes, links, 'MULTIPLY', _math(nodes, links, 'MULTIPLY', inside_x, inside_z), weather)
        # 표찰 테두리를 따라 남은 양면테이프 자국. 1 cm 띠에서 노이즈로 듬성듬성.
        ring_x = _ramp(nodes, links, _math(nodes, links, 'ABSOLUTE', _math(nodes, links, 'SUBTRACT', X, sx)), sw * 0.5 + 0.008, sw * 0.5 - 0.004)
        ring_z = _ramp(nodes, links, _math(nodes, links, 'ABSOLUTE', _math(nodes, links, 'SUBTRACT', Z, sz)), sh * 0.5 + 0.008, sh * 0.5 - 0.004)
        ring = _math(nodes, links, 'SUBTRACT', _math(nodes, links, 'MULTIPLY', ring_x, ring_z), _math(nodes, links, 'MULTIPLY', inside_x, inside_z), clamp=True)
        glue_noise = ig._noise(nodes, links, 60.0, detail=2.0, roughness=0.6, coords=coords)
        glue = _math(nodes, links, 'MULTIPLY', _math(nodes, links, 'MULTIPLY', ring, weather), _ramp(nodes, links, glue_noise.outputs['Fac'], 0.42, 0.58))
        # 표찰 밑은 볕을 덜 받아 처음 칠 그대로다. 둘레보다 조금 짙고 조금 덜 거칠다.
        color = _mix(nodes, links, _math(nodes, links, 'MULTIPLY', plate, 0.8), color, (0.068, 0.093, 0.088))
        color = _mix(nodes, links, _math(nodes, links, 'MULTIPLY', glue, 0.75), color, (0.20, 0.17, 0.11))
        roughness = _math(nodes, links, 'SUBTRACT', roughness, _math(nodes, links, 'MULTIPLY', plate, 0.10))
        roughness = _math(nodes, links, 'ADD', roughness, _math(nodes, links, 'MULTIPLY', glue, 0.25))

    links.new(color, bsdf.inputs['Base Color'])
    rough_clamp = _math(nodes, links, 'ADD', roughness, 0.0, clamp=True)
    links.new(rough_clamp, bsdf.inputs['Roughness'])
    bsdf.inputs['Metallic'].default_value = 0.0
    # 오렌지필과 녹이 일으킨 칠.
    peel = ig._noise(nodes, links, 220.0, detail=2.0, coords=coords)
    height = _math(nodes, links, 'ADD', _math(nodes, links, 'MULTIPLY', peel.outputs['Fac'], 0.3),
                   _math(nodes, links, 'MULTIPLY', _math(nodes, links, 'MULTIPLY', rust, rust_tone.outputs['Fac']), 1.0))
    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.08
    bump.inputs['Distance'].default_value = 0.002
    links.new(height, bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])
    mat['ig_kind'] = 'baked'
    return mat


def hardware_materials():
    return {
        'steel': ig.mat_metal('SatinSteel', (0.60, 0.60, 0.585), roughness=0.36, streak=0.06, anisotropic=False),
        'dark': ig.mat_plastic('KeywayDark', (0.012, 0.012, 0.012), roughness=0.6, bump=0.0),
        'brass': ig.mat_metal('CylinderBrass', (0.62, 0.56, 0.42), roughness=0.34, streak=0.04, anisotropic=False),
    }


def handle_set(m, width, face_sign, with_key):
    """face_sign 쪽 면(+1이면 +Y)의 손잡이판, 레버, 나사, 열쇠 구멍. 레버는 경첩 쪽으로 뻗는다."""
    face = face_sign * T * 0.5
    lever_x = -width * 0.5 + 0.075
    rot = (math.pi * 0.5, 0.0, 0.0)
    parts = [
        ig.box('plate', (PLATE_W, 0.007, PLATE_H), location=(lever_x, face + face_sign * 0.0035, PLATE_Z),
               bevel=0.0035, segments=3, material=m['steel']),
        ig.cylinder('boss', 0.0115, 0.012, location=(lever_x, face + face_sign * 0.013, LEVER_Z), rotation=rot,
                    segments=28, bevel=0.0015, bevel_segments=2, material=m['steel']),
        ig.pipe('lever', [
            (lever_x, face + face_sign * 0.016, LEVER_Z),
            (lever_x, face + face_sign * 0.052, LEVER_Z),
            (lever_x + 0.018, face + face_sign * 0.060, LEVER_Z),
            (lever_x + 0.118, face + face_sign * 0.060, LEVER_Z),
        ], radius=0.0095, resolution=16, corner_radius=0.012, material=m['steel']),
    ]
    for z in (PLATE_Z - PLATE_H * 0.5 + 0.013, PLATE_Z + PLATE_H * 0.5 - 0.013):
        parts.append(ig.cylinder('screw', 0.0032, 0.0016, location=(lever_x, face + face_sign * 0.0078, z),
                                 rotation=rot, segments=14, bevel=0.0008, bevel_segments=1, material=m['steel']))
    if with_key:
        parts.append(ig.cylinder('cylinder_ring', 0.0135, 0.004, location=(lever_x, face + face_sign * 0.009, KEY_Z),
                                 rotation=rot, segments=32, bevel=0.0012, bevel_segments=2, material=m['steel']))
        parts.append(ig.cylinder('cylinder_face', 0.0098, 0.0012, location=(lever_x, face + face_sign * 0.0112, KEY_Z),
                                 rotation=rot, segments=28, material=m['brass']))
        parts.append(ig.box('keyway', (0.0022, 0.0012, 0.012), location=(lever_x, face + face_sign * 0.0119, KEY_Z - 0.001),
                            material=m['dark']))
        parts.append(ig.cylinder('keyway_top', 0.0021, 0.0012, location=(lever_x, face + face_sign * 0.0119, KEY_Z + 0.004),
                                 rotation=rot, segments=12, material=m['dark']))
    return parts


def hinge_set(m, width, face_sign):
    """경첩 셋. 문이 열리는 쪽 면에 너클이 조금 나온다."""
    return [ig.cylinder(f'hinge_{int(z * 100)}', 0.0088, 0.11, location=(width * 0.5 + 0.003, face_sign * 0.012, z),
                        segments=20, bevel=0.0015, bevel_segments=1, material=m['steel'])
            for z in (0.24, 1.03, 1.82)]


def build_leaf(out_root, name, width, key_sign, hinge_sign, weather_sign, scar=None, notes=''):
    ig.reset_scene()
    m = hardware_materials()
    leaf = ig.box('leaf', (width, T, Z1 - Z0), location=(0.0, 0.0, Z0), origin='bottom', bevel=0.003,
                  segments=2, material=door_paint(name + 'PaintIn', width, False))
    # 비 맞는 면만 바깥 칠. 모서리 면과 반대 면은 안쪽 칠이다.
    leaf.data.materials.append(door_paint(name + 'PaintOut', width, True, scar))
    for face in leaf.data.polygons:
        if face.normal.y * weather_sign > 0.5:
            face.material_index = 1
    if scar:
        # 표찰 나사 구멍 둘. 3 mm 파여 있다.
        sx, sz, sw, _ = scar
        for dx in (-sw * 0.5 + 0.012, sw * 0.5 - 0.012):
            hole = ig.cylinder('scar_hole', 0.0022, 0.006, location=(sx + dx, weather_sign * T * 0.5, sz),
                               rotation=(math.pi * 0.5, 0.0, 0.0), segments=12)
            ig.boolean(leaf, hole)
    parts = [leaf]
    parts += handle_set(m, width, key_sign, True)
    parts += handle_set(m, width, -key_sign, False)
    parts += hinge_set(m, width, hinge_sign)
    # 손잡이 쪽 옆면의 걸림쇠 판.
    parts.append(ig.box('strike', (0.004, 0.024, 0.20), location=(-width * 0.5 - 0.0015, 0.0, LEVER_Z - 0.03),
                        bevel=0.001, segments=1, material=m['steel']))
    return ig.build_asset(name, 'hero', parts, out_root, collision_parts=[[leaf]], texture_size=2048,
                          preview_yaw=180.0 if key_sign > 0 else 0.0, notes=notes)


def build_roof(out_root):
    width = 0.85
    build_leaf(
        out_root, 'SM_RooftopDoorLeaf', width, key_sign=1.0, hinge_sign=1.0, weather_sign=-1.0,
        notes=(f'옥상 철문 {width * 100:.0f} x {(Z1 - Z0) * 100:.1f} x {T * 100:.1f} cm(바닥 틈 1 cm). 경첩 +X, '
               '앞면 -Y가 옥상 쪽, +Y가 계단 쪽. +Y에 경첩 너클과 열쇠 구멍, -Y 면이 비를 맞는다. 원점 바닥 '
               '중심. AIGSwingDoor::ConfigureAuthoredLeaf로 단다. 충돌은 문짝 판 하나.'))


def build_annex(out_root):
    width = 0.88
    build_leaf(
        out_root, 'SM_AnnexDoorLeaf', width, key_sign=1.0, hinge_sign=-1.0, weather_sign=1.0,
        scar=(0.02, 1.55, 0.17, 0.065),
        notes=(f'5층 철문 {width * 100:.0f} x {(Z1 - Z0) * 100:.1f} x {T * 100:.1f} cm. 개구부 90 cm보다 2 cm 좁아 '
               '손잡이 쪽에 빗장 받이쇠 자리가 남는다. 경첩 +X, 앞면 -Y가 증축부 안, +Y가 옥상 쪽. +Y에 열쇠 '
               '구멍과 호수 표찰을 뗀 자리(나사 구멍 둘), -Y에 경첩 너클. 원점 바닥 중심. 충돌은 문짝 판 하나.'))


def build_sign(out_root):
    ig.reset_scene()
    ink = ig.mat_image_uv('RooftopDoorSign', os.path.join(AI, 'RooftopDoorSign_20261007.png'), roughness=0.38)
    back = ig.mat_metal('SignBack', (0.70, 0.70, 0.68), roughness=0.45, streak=0.05, anisotropic=False)
    thickness = 0.0012
    # 앞면(-Y)에 인쇄가 오는 판을 Z축으로 돌려 계단 쪽(+Y)을 보게 한다.
    plate = ig.image_quad('sign', (0.30, 0.20), (0.0, T * 0.5 + 0.0002 + thickness * 0.5, 1.47), ink,
                          thickness=thickness, rotation=(0.0, 0.0, math.pi))
    ig.set_active(plate)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    plate.data.materials.append(back)
    for face in plate.data.polygons:
        if face.normal.y < 0.5:
            face.material_index = 1
    ig.build_asset(
        'SM_RooftopDoorSign', 'prop', [plate], out_root, collision_parts=[], texture_size=1024,
        mirror_print_uv=True, preview_yaw=180.0,
        notes=('옥상 철문 계단 쪽 안내판 30 x 20 cm, 가운데 높이 1.47 m. 원점과 축은 SM_RooftopDoorLeaf와 같다. '
               '원본 SourceArt/AI/RooftopDoorSign_20261007(gpt-image). 충돌·그림자 없음.'))


def zinc():
    return ig.mat_metal('BoltZinc', (0.50, 0.495, 0.47), roughness=0.46, streak=0.05, anisotropic=False)


def build_bolt(out_root):
    rot_x = (0.0, math.pi * 0.5, 0.0)   # 원기둥 축을 X로
    rot_y = (math.pi * 0.5, 0.0, 0.0)   # 원기둥 축을 Y로

    # 몸통: 받침판과 막대가 지나는 관, 꼭지가 오가는 홈. x -0.09..0.06.
    ig.reset_scene()
    metal = zinc()
    rust = ig.mat_painted_steel('BoltRust', (0.16, 0.075, 0.035), roughness=0.8, wear=0.2, bump=0.05)
    flange = ig.box('flange', (0.150, 0.0016, 0.044), location=(-0.015, 0.0008, 0.0), bevel=0.0012, segments=2,
                    material=metal)
    tube = ig.cylinder('tube', 0.0092, 0.150, location=(-0.015, BOLT_PIN_Y, 0.0), rotation=rot_x, segments=28,
                       material=metal)
    bore = ig.cylinder('bore', 0.0068, 0.16, location=(-0.015, BOLT_PIN_Y, 0.0), rotation=rot_x, segments=24)
    ig.boolean(tube, bore)
    slot = ig.box('slot', (0.056, 0.02, 0.0090), location=(-0.0175, BOLT_PIN_Y + 0.009, 0.0))
    ig.boolean(tube, slot)
    # 풀린 자리에서 꼭지를 걸어 두는 턱. 홈 끝이 아래로 꺾인다.
    notch = ig.box('notch', (0.0095, 0.02, 0.014), location=(-0.041, BOLT_PIN_Y + 0.009, -0.006))
    ig.boolean(tube, notch)
    housing = [flange, tube]
    for x in (-0.078, 0.048):
        for z in (-0.0155, 0.0155):
            housing.append(ig.cylinder('screw', 0.0034, 0.0012, location=(x, 0.0022, z), rotation=rot_y,
                                       segments=14, bevel=0.0008, bevel_segments=1, material=rust if x > 0 else metal))
    ig.build_asset(
        'SM_DoorBarrelBolt', 'prop', housing, out_root, collision_parts=[], texture_size=512, preview_yaw=180.0,
        notes=('빗장 몸통 15 x 4.4 cm. 원점은 문 면 위 몸통 가운데에서 x로 +1.5 cm(막대 끝이 풀렸을 때 x 0.06), '
               '몸통은 +Y로 2 cm 솟는다. 막대는 +X로 민다. 충돌·그림자 없음.'))

    # 막대와 꼭지. 풀린 자리: 끝이 몸통 끝(x 0.06)과 같다.
    ig.reset_scene()
    metal = zinc()
    worn = ig.mat_metal('KnobWorn', (0.66, 0.65, 0.62), roughness=0.26, streak=0.04, anisotropic=False)
    pin = ig.cylinder('pin', 0.0062, 0.140, location=(-0.010, BOLT_PIN_Y, 0.0), rotation=rot_x, segments=24,
                      bevel=0.0012, bevel_segments=2, material=metal)
    stem = ig.cylinder('stem', 0.0042, 0.018, location=(-0.041, BOLT_PIN_Y + 0.009, 0.0), rotation=rot_y,
                       segments=16, material=metal)
    knob = ig.cylinder('knob', 0.0068, 0.009, location=(-0.041, BOLT_PIN_Y + 0.021, 0.0), rotation=rot_y,
                       segments=24, bevel=0.0032, bevel_segments=3, material=worn)
    ig.build_asset(
        'SM_DoorBarrelBoltPin', 'prop', [pin, stem, knob], out_root, collision_parts=[], texture_size=256,
        preview_yaw=180.0,
        notes=(f'빗장 막대 14 cm와 꼭지. 몸통과 원점이 같고 풀린 자리다. 걸면 +X로 {BOLT_TRAVEL * 100:.1f} cm '
               '민다. 충돌·그림자 없음.'))

    # 받이쇠: 문설주 옆면(x=0)에 붙는 판과 -X로 나온 구멍 뚫린 덩이.
    ig.reset_scene()
    metal = zinc()
    rust = ig.mat_painted_steel('KeeperRust', (0.16, 0.075, 0.035), roughness=0.8, wear=0.2, bump=0.05)
    plate = ig.box('plate', (0.0025, 0.028, 0.052), location=(-0.00125, 0.0, 0.0), bevel=0.0008, segments=1,
                   material=metal)
    block = ig.box('block', (0.014, 0.022, 0.024), location=(-0.0095, 0.0, 0.0), bevel=0.0015, segments=2,
                   material=metal)
    hole = ig.cylinder('hole', 0.0070, 0.04, location=(-0.0095, 0.0, 0.0), rotation=rot_x, segments=24)
    ig.boolean(block, hole)
    keeper = [plate, block]
    for z in (-0.020, 0.020):
        keeper.append(ig.cylinder('screw', 0.0030, 0.0012, location=(-0.0031, 0.0, z), rotation=rot_x,
                                  segments=14, bevel=0.0007, bevel_segments=1, material=rust))
    ig.build_asset(
        'SM_DoorBarrelBoltKeeper', 'prop', keeper, out_root, collision_parts=[], texture_size=256, preview_yaw=90.0,
        notes=('빗장 받이쇠. 원점은 문설주 옆면에 붙는 면 가운데, -X로 1.65 cm 나온다. 막대 구멍 지름 1.4 cm가 '
               'x축을 따라 뚫려 있다. 충돌·그림자 없음.'))


def main():
    out_root = ig.out_root_from_argv()
    only = sys.argv[sys.argv.index('--') + 2:] if '--' in sys.argv else []
    steps = [('roof', build_roof), ('annex', build_annex), ('sign', build_sign), ('bolt', build_bolt)]
    for key, step in steps:
        if not only or key in only:
            step(out_root)


main()
