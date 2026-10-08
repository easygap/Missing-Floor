"""계단실 방화문. 2·3·4층 계단 목에 하나씩 선다.

- SM_StairFireDoorLeaf: 문짝, 경첩, 양쪽 레버, 계단 쪽 위 모서리의 도어클로저 몸통.
- SM_FireDoorCloserArm, SM_FireDoorCloserRod: 클로저 팔 두 마디. 씬이 문이 돌 때마다 맞춘다.
- SM_FireDoorPrints: 문짝 양면의 「방화문 / 항상 닫아 두세요 / 고임목 사용 금지」 스티커.
- SM_FireDoorWedge: 문 밑에 끼워 둔 나무 고임목.
- SM_ExitSignLamp: 계단 목 머리에 붙은 소형 피난구 유도등. 발광면은 ExitSignFace_20261007.

기준: Content/SourceArt/AI/FireDoorConcept_20261007.png(문짝과 클로저),
FireDoorSticker_20261007.png(스티커 인쇄면), FireDoorWedge_20261007.png(고임목).
고임목 윗면은 시안 사진을 펴서 그대로 쓴다(Scripts/rectify_concept_faces.py).

1990년대 빌라의 강판 방화문이다. 창 없는 민짝에 밝은 회색 에나멜을 칠했고, 손잡이 둘레와
발로 미는 아래 40 cm, 어깨 높이에서 손으로 미는 자리가 지저분하다. 모서리 몇 군데는
칠이 떨어져 짙은 녹막이가 보인다.

문짝 원점은 바닥 중심이고 경첩이 +X, 앞면 -Y가 계단 쪽이다. AIGSwingDoor의
ConfigureAuthoredLeaf가 yaw -90으로 달면 경첩 축이 액터 원점에, 문짝이 액터 +Y로, 앞면이
액터 -X(계단 쪽)로 온다. 403호 문(L 변형)과 같은 관례다. 스티커 메시도 원점이 같다.

클로저 팔은 원점이 회전축이고 +X로 뻗는다. 팔(피니언에서 팔꿈치까지 34 cm)은 문짝 위
클로저 몸통의 피니언에서, 막대(슈에서 팔꿈치까지 31 cm)는 문틀 머리 밑에 매단 슈에서
나온다. 막대는 팔보다 8 mm 위에 지나가 팔꿈치에서 겹친다.

    blender -b --factory-startup --python Scripts/blender/build_stair_fire_door.py -- <out_dir> [leaf|arm|prints|wedge ...]
"""
import math
import os
import sys

import bmesh
import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import ig_blender_lib as ig  # noqa: E402

AI = os.path.join(HERE, '../../Content/SourceArt/AI')
PRINTS = os.path.join(HERE, '../../Content/SourceArt/UtilityPrints')

W, T = 1.20, 0.05            # 문짝 폭·두께
Z0, Z1 = 0.015, 2.095        # 바닥 틈 1.5 cm, 위 틈 0.5 cm(문틀 머리 밑면 2.10)
FRONT = -T * 0.5             # 계단 쪽 면
HINGE_X = W * 0.5
LEVER_X = -W * 0.5 + 0.075   # 레버 로제트 중심
LEVER_Z = 1.00
# 클로저 몸통과 피니언. 씬의 IGFireDoor 상수(액터 기준 피니언 (-5, 28, 205))와 같은 자리다.
CLOSER_X = 0.39
CLOSER_DEPTH = 0.05
CLOSER_Z0, CLOSER_Z1 = 1.975, 2.040
PINION_X = HINGE_X - 0.28
PINION_Y = FRONT - CLOSER_DEPTH * 0.5
ARM_LENGTH = 0.34
ROD_LENGTH = 0.31


def fire_door_paint():
    """밝은 회색 에나멜. 아래쪽 때, 레버 둘레와 미는 자리의 손때, 모서리의 칠 벗겨짐."""
    mat = ig.mat_painted_steel('FireDoorEnamel', (0.40, 0.40, 0.385), roughness=0.40, wear=0.55,
                               bump=0.06, dirt_color=(0.19, 0.18, 0.16))
    tree, nodes, links = ig._nodes(mat)
    bsdf = nodes.get('Principled BSDF')
    base_link = bsdf.inputs['Base Color'].links[0]
    painted = base_link.from_socket
    coords = ig._object_coords(nodes)
    sep = nodes.new('ShaderNodeSeparateXYZ')
    links.new(coords, sep.inputs['Vector'])

    def gaussian(axis_socket, center, width):
        delta = nodes.new('ShaderNodeMath')
        delta.operation = 'SUBTRACT'
        delta.inputs[1].default_value = center
        links.new(axis_socket, delta.inputs[0])
        scaled = nodes.new('ShaderNodeMath')
        scaled.operation = 'DIVIDE'
        scaled.inputs[1].default_value = width
        links.new(delta.outputs[0], scaled.inputs[0])
        square = nodes.new('ShaderNodeMath')
        square.operation = 'MULTIPLY'
        links.new(scaled.outputs[0], square.inputs[0])
        links.new(scaled.outputs[0], square.inputs[1])
        negate = nodes.new('ShaderNodeMath')
        negate.operation = 'MULTIPLY'
        negate.inputs[1].default_value = -1.0
        links.new(square.outputs[0], negate.inputs[0])
        gauss = nodes.new('ShaderNodeMath')
        gauss.operation = 'EXPONENT'
        links.new(negate.outputs[0], gauss.inputs[0])
        return gauss.outputs[0]

    def multiply(a, b):
        node = nodes.new('ShaderNodeMath')
        node.operation = 'MULTIPLY'
        links.new(a, node.inputs[0])
        if isinstance(b, float):
            node.inputs[1].default_value = b
        else:
            links.new(b, node.inputs[1])
        return node.outputs[0]

    def add(a, b):
        node = nodes.new('ShaderNodeMath')
        node.operation = 'ADD'
        node.use_clamp = True
        links.new(a, node.inputs[0])
        links.new(b, node.inputs[1])
        return node.outputs[0]

    smudge = ig._noise(nodes, links, 11.0, detail=3.0, roughness=0.6, coords=coords)
    smudge_mask = ig._map_range(nodes, links, smudge.outputs['Fac'], 0.35, 0.70, 0.25, 1.0)
    # 레버 둘레 한 뼘.
    lever = multiply(gaussian(sep.outputs['X'], LEVER_X + 0.03, 0.11), gaussian(sep.outputs['Z'], LEVER_Z, 0.13))
    # 어깨 높이에서 손바닥으로 미는 자리. 손잡이 쪽 가장자리 30 cm.
    push = multiply(gaussian(sep.outputs['X'], -W * 0.5 + 0.16, 0.13), gaussian(sep.outputs['Z'], 1.32, 0.16))
    hands = multiply(add(multiply(lever, 0.85), multiply(push, 0.55)), smudge_mask.outputs['Result'])
    grime_rgb = nodes.new('ShaderNodeRGB')
    grime_rgb.outputs[0].default_value = (0.215, 0.205, 0.185, 1.0)
    with_hands = nodes.new('ShaderNodeMixRGB')
    links.new(hands, with_hands.inputs['Fac'])
    links.new(painted, with_hands.inputs['Color1'])
    links.new(grime_rgb.outputs[0], with_hands.inputs['Color2'])

    # 모서리 3 cm 안에서만 드문드문 칠이 떨어진다. 점이 아니라 손톱만 한 조각이고, 문을
    # 차고 미는 아래쪽에 몰린다.
    edge_x = nodes.new('ShaderNodeMath')
    edge_x.operation = 'ABSOLUTE'
    links.new(sep.outputs['X'], edge_x.inputs[0])
    near_side = ig._map_range(nodes, links, edge_x.outputs[0], W * 0.5 - 0.03, W * 0.5 - 0.004, 0.0, 1.0)
    near_side.clamp = True
    near_bottom = ig._map_range(nodes, links, sep.outputs['Z'], Z0 + 0.05, Z0 + 0.006, 0.0, 1.0)
    near_bottom.clamp = True
    low = ig._map_range(nodes, links, sep.outputs['Z'], 0.9, 0.3, 0.0, 1.0)
    low.clamp = True
    edge = multiply(add(near_side.outputs['Result'], near_bottom.outputs['Result']), low.outputs['Result'])
    chips = ig._noise(nodes, links, 7.0, detail=0.0, roughness=0.3, coords=coords)
    chip_mask = ig._map_range(nodes, links, chips.outputs['Fac'], 0.70, 0.72, 0.0, 1.0)
    chip_mask.clamp = True
    chipped = multiply(edge, chip_mask.outputs['Result'])
    primer = nodes.new('ShaderNodeRGB')
    primer.outputs[0].default_value = (0.075, 0.078, 0.080, 1.0)
    final = nodes.new('ShaderNodeMixRGB')
    links.new(chipped, final.inputs['Fac'])
    links.new(with_hands.outputs[0], final.inputs['Color1'])
    links.new(primer.outputs[0], final.inputs['Color2'])
    links.remove(base_link)
    links.new(final.outputs[0], bsdf.inputs['Base Color'])
    return mat


def materials():
    return {
        'paint': fire_door_paint(),
        'steel': ig.mat_metal('SatinSteel', (0.62, 0.62, 0.60), roughness=0.38, streak=0.06, anisotropic=False),
        'closer': ig.mat_painted_steel('CloserSilver', (0.34, 0.345, 0.35), roughness=0.38, wear=0.15, bump=0.02),
        'black': ig.mat_painted_steel('CloserArmBlack', (0.026, 0.026, 0.028), roughness=0.48, wear=0.08, bump=0.02),
    }


def lever_parts(m, face_sign):
    """face_sign -1이면 계단 쪽(-Y), +1이면 복도 쪽(+Y) 레버. 레버는 경첩 쪽으로 뻗는다."""
    face = face_sign * T * 0.5
    parts = [
        ig.cylinder('rose', 0.026, 0.008, location=(LEVER_X, face + face_sign * 0.004, LEVER_Z),
                    rotation=(math.pi * 0.5, 0.0, 0.0), segments=36, bevel=0.0015, bevel_segments=2,
                    material=m['steel']),
        ig.cylinder('neck', 0.0100, 0.042, location=(LEVER_X, face + face_sign * 0.029, LEVER_Z),
                    rotation=(math.pi * 0.5, 0.0, 0.0), segments=24, material=m['steel']),
        ig.pipe('lever', [
            (LEVER_X, face + face_sign * 0.050, LEVER_Z),
            (LEVER_X + 0.020, face + face_sign * 0.058, LEVER_Z),
            (LEVER_X + 0.112, face + face_sign * 0.058, LEVER_Z - 0.004),
            (LEVER_X + 0.124, face + face_sign * 0.054, LEVER_Z - 0.011),
        ], radius=0.0100, resolution=16, corner_radius=0.012, material=m['steel']),
    ]
    return parts


def build_leaf(out_root):
    ig.reset_scene()
    m = materials()
    leaf = ig.box('leaf', (W, T, Z1 - Z0), location=(0.0, 0.0, Z0), origin='bottom', bevel=0.003, segments=2,
                  material=m['paint'])
    parts = [leaf]
    # 경첩 셋. 축은 문짝 모서리, 손잡이 반대쪽이다. 너클이 복도 쪽으로 조금 나온다.
    for z in (0.26, 1.05, 1.84):
        parts.append(ig.cylinder(f'hinge_{int(z * 100)}', 0.0085, 0.11, location=(HINGE_X + 0.003, 0.012, z),
                                 segments=20, bevel=0.0015, bevel_segments=1, material=m['steel']))
    # 손잡이 쪽 옆면의 걸쇠 판.
    parts.append(ig.box('strike', (0.004, 0.024, 0.22), location=(-W * 0.5 - 0.0015, 0.0, LEVER_Z),
                        bevel=0.001, segments=1, material=m['steel']))
    parts += lever_parts(m, -1.0)
    parts += lever_parts(m, 1.0)
    # 도어클로저 몸통. 계단 쪽 면, 경첩 쪽 위 모서리. 끝마구리에 조절 나사가 둘 있다.
    body_len = 0.26
    body = ig.box('closer_body', (body_len, CLOSER_DEPTH, CLOSER_Z1 - CLOSER_Z0),
                  location=(CLOSER_X, FRONT - CLOSER_DEPTH * 0.5, (CLOSER_Z0 + CLOSER_Z1) * 0.5),
                  bevel=0.012, segments=4, material=m['closer'])
    parts.append(body)
    for sign in (-1.0, 1.0):
        parts.append(ig.cylinder('valve', 0.0045, 0.004,
                                 location=(CLOSER_X + sign * (body_len * 0.5 + 0.0015), FRONT - CLOSER_DEPTH * 0.5,
                                           CLOSER_Z0 + 0.022),
                                 rotation=(0.0, math.pi * 0.5, 0.0), segments=16, material=m['steel']))
    # 몸통 받침판과 피니언 보스. 팔이 보스 위(2.052)에 물린다.
    parts.append(ig.box('closer_plate', (body_len + 0.02, 0.004, CLOSER_Z1 - CLOSER_Z0 + 0.012),
                        location=(CLOSER_X, FRONT - 0.002, (CLOSER_Z0 + CLOSER_Z1) * 0.5),
                        bevel=0.001, segments=1, material=m['closer']))
    parts.append(ig.cylinder('pinion', 0.011, 0.008, location=(PINION_X, PINION_Y, CLOSER_Z1 + 0.004),
                             segments=24, bevel=0.001, bevel_segments=1, material=m['black']))
    ig.log(f'fire door leaf: pinion at ({PINION_X:.3f}, {PINION_Y:.3f}, {CLOSER_Z1 + 0.008:.3f})')
    return ig.build_asset(
        'SM_StairFireDoorLeaf', 'hero', parts, out_root,
        collision_parts=[[leaf]],
        notes=('계단실 방화문 문짝 120 x 208 x 5 cm(바닥 틈 1.5 cm). 경첩 +X, 앞면 -Y가 계단 쪽, 원점 바닥 중심. '
               'AIGSwingDoor::ConfigureAuthoredLeaf로 단다. 계단 쪽 위 모서리에 도어클로저 몸통, '
               f'피니언 ({PINION_X:.2f}, {PINION_Y:.3f}, 2.048). 양쪽 레버. 충돌은 문짝 판 하나.'),
        texture_size=2048, preview_yaw=35)


def build_arm(out_root):
    ig.reset_scene()
    m = materials()
    # 팔: 피니언에서 팔꿈치까지 납작한 강철 띠. 피니언 쪽 끝에 볼트 머리.
    arm = [
        ig.box('bar', (ARM_LENGTH, 0.022, 0.007), location=(ARM_LENGTH * 0.5, 0.0, 0.0),
               bevel=0.002, segments=1, material=m['black']),
        ig.cylinder('pinion_end', 0.0135, 0.009, segments=24, bevel=0.0015, bevel_segments=1, material=m['black']),
        ig.cylinder('bolt', 0.006, 0.004, location=(0.0, 0.0, 0.0065), segments=6, material=m['steel']),
        ig.cylinder('elbow_end', 0.0105, 0.009, location=(ARM_LENGTH, 0.0, 0.0), segments=24,
                    bevel=0.0015, bevel_segments=1, material=m['black']),
    ]
    ig.build_asset(
        'SM_FireDoorCloserArm', 'prop', arm, out_root, collision_parts=[], texture_size=256,
        notes=f'도어클로저 팔. 원점이 피니언 축, +X로 {ARM_LENGTH * 100:.0f} cm 끝이 팔꿈치. 충돌 없음.')

    ig.reset_scene()
    m = materials()
    # 막대: 문틀 머리 밑에 매단 둥근 슈에서 팔꿈치까지. 슈는 둥글어서 같이 돌아도 티가 안 난다.
    rod_z = 0.008
    rod = [
        ig.cylinder('shoe', 0.018, 0.054, location=(0.0, 0.0, 0.021), segments=28, bevel=0.002,
                    bevel_segments=1, material=m['black']),
        ig.cylinder('shoe_plate', 0.026, 0.004, location=(0.0, 0.0, 0.046), segments=28, material=m['black']),
        ig.cylinder('rod', 0.0055, ROD_LENGTH - 0.030, location=(0.015 + (ROD_LENGTH - 0.030) * 0.5, 0.0, rod_z),
                    rotation=(0.0, math.pi * 0.5, 0.0), segments=16, material=m['black']),
        ig.cylinder('sleeve', 0.0078, 0.07, location=(0.15, 0.0, rod_z), rotation=(0.0, math.pi * 0.5, 0.0),
                    segments=16, bevel=0.0012, bevel_segments=1, material=m['black']),
        ig.cylinder('eye', 0.0100, 0.007, location=(ROD_LENGTH, 0.0, rod_z), segments=24, bevel=0.0012,
                    bevel_segments=1, material=m['black']),
    ]
    ig.build_asset(
        'SM_FireDoorCloserRod', 'prop', rod, out_root, collision_parts=[], texture_size=256,
        notes=(f'도어클로저 막대와 슈. 원점이 슈 축(슈 윗면 +5 cm가 문틀 머리 밑면), +X로 {ROD_LENGTH * 100:.0f} cm '
               '끝이 팔꿈치. 팔보다 8 mm 위를 지난다. 충돌 없음.'))


def build_prints(out_root):
    ig.reset_scene()
    source = os.path.join(AI, 'FireDoorSticker_20261007.png')
    ink = ig.mat_image_uv('FireDoorSticker', source, roughness=0.34)
    back = ig.mat_plastic('StickerBack', (0.80, 0.80, 0.78), roughness=0.6, bump=0.0)
    size = (0.30, 0.20)
    thickness = 0.0003
    parts = []
    for face_sign, tilt in ((-1.0, 0.8), (1.0, -0.6)):
        y = face_sign * (T * 0.5 + 0.0002 + thickness * 0.5)
        # 앞면(-Y)에 인쇄가 오는 판이다. 복도 쪽 것은 Z축으로 돌려 인쇄가 +Y를 보게 한다.
        sticker = ig.image_quad('sticker', size, (-0.04, y, 1.46), ink, thickness=thickness,
                                rotation=(0.0, math.radians(tilt), 0.0 if face_sign < 0 else math.pi))
        ig.set_active(sticker)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
        sticker.data.materials.append(back)
        for face in sticker.data.polygons:
            if face.normal.y * face_sign < 0.5:
                face.material_index = 1
        parts.append(sticker)
    ig.build_asset(
        'SM_FireDoorPrints', 'prop', parts, out_root, collision_parts=[], texture_size=1024,
        mirror_print_uv=True,
        notes=('방화문 양면 스티커 30 x 20 cm, 가운데 높이 1.46 m. 원점과 축은 SM_StairFireDoorLeaf와 같다. '
               '원본 SourceArt/AI/FireDoorSticker_20261007(gpt-image). 충돌·그림자 없음.'))


def wedge_wood(name, color, end_grain=False):
    """소나무 각재. 결은 길이 방향(X)으로 늘인 노이즈, 마구리는 둥근 나이테 대신 톱자국 결."""
    mat = bpy.data.materials.new(name)
    bsdf = ig._principled(mat)
    tree, nodes, links = ig._nodes(mat)
    coords = ig._object_coords(nodes)
    mapping = nodes.new('ShaderNodeMapping')
    mapping.inputs['Scale'].default_value = (1.0, 1.0, 14.0) if end_grain else (1.0, 26.0, 9.0)
    links.new(coords, mapping.inputs['Vector'])
    grain = ig._noise(nodes, links, 22.0, detail=3.0, roughness=0.55, coords=mapping.outputs['Vector'])
    tone = ig._map_range(nodes, links, grain.outputs['Fac'], 0.3, 0.7, 0.80, 1.10)
    base = nodes.new('ShaderNodeRGB')
    base.outputs[0].default_value = (*color, 1.0)
    tinted = nodes.new('ShaderNodeMixRGB')
    tinted.blend_type = 'MULTIPLY'
    tinted.inputs['Fac'].default_value = 1.0
    links.new(base.outputs[0], tinted.inputs['Color1'])
    links.new(tone.outputs['Result'], tinted.inputs['Color2'])
    # 얇은 끝(+X)으로 갈수록 발에 차여 검게 뭉갠다. 윗면 사진과 같은 쪽이다.
    sep = nodes.new('ShaderNodeSeparateXYZ')
    links.new(coords, sep.inputs['Vector'])
    crushed = ig._map_range(nodes, links, sep.outputs['X'], 0.02, 0.09, 0.0, 0.75)
    crushed.clamp = True
    dark = nodes.new('ShaderNodeRGB')
    dark.outputs[0].default_value = (0.085, 0.075, 0.060, 1.0)
    final = nodes.new('ShaderNodeMixRGB')
    links.new(crushed.outputs['Result'], final.inputs['Fac'])
    links.new(tinted.outputs[0], final.inputs['Color1'])
    links.new(dark.outputs[0], final.inputs['Color2'])
    links.new(final.outputs[0], bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value = 0.78
    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.12
    bump.inputs['Distance'].default_value = 0.0008
    links.new(grain.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])
    mat['ig_kind'] = 'baked'
    return mat


def build_wedge(out_root):
    ig.reset_scene()
    length, width, thick, thin = 0.18, 0.07, 0.042, 0.006
    top_image = ig.mat_image_uv('WedgeTop', os.path.join(PRINTS, 'FireDoorWedgeTop.png'), roughness=0.80)
    side = wedge_wood('WedgeSide', (0.36, 0.215, 0.115))
    end = wedge_wood('WedgeEnd', (0.27, 0.165, 0.090), end_grain=True)
    bm = bmesh.new()
    hx, hy = length * 0.5, width * 0.5
    # 두꺼운 끝이 -X, 얇은 끝이 +X. 바닥이 Z 0.
    v = [bm.verts.new(p) for p in (
        (-hx, -hy, 0.0), (hx, -hy, 0.0), (hx, hy, 0.0), (-hx, hy, 0.0),
        (-hx, -hy, thick), (hx, -hy, thin), (hx, hy, thin), (-hx, hy, thick))]
    faces = [
        ((0, 3, 2, 1), 1),   # 바닥
        ((4, 5, 6, 7), 0),   # 비스듬한 윗면(사진)
        ((0, 1, 5, 4), 1),   # 옆
        ((2, 3, 7, 6), 1),   # 옆
        ((3, 0, 4, 7), 2),   # 두꺼운 마구리
        ((1, 2, 6, 5), 2),   # 얇은 끝
    ]
    for index, material in faces:
        face = bm.faces.new([v[i] for i in index])
        face.material_index = material
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new('wedge')
    bm.to_mesh(me)
    bm.free()
    ob = ig._link(bpy.data.objects.new('wedge', me))
    for material in (top_image, side, end):
        me.materials.append(material)
    # 윗면 사진은 왼쪽이 얇은 끝, 위가 먼 쪽 모서리다.
    layer = me.uv_layers.new(name='ImageUV')
    for poly in me.polygons:
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            layer.data[li].uv = ((hx - co.x) / length, (co.y + hy) / width)
    ig.add_bevel(ob, 0.0018, segments=2)
    ig.apply_modifiers(ob)
    return ig.build_asset(
        'SM_FireDoorWedge', 'prop', [ob], out_root, collision_parts=[], texture_size=512,
        notes=('나무 고임목 18 x 7 cm, 두께 4.2 cm에서 0.6 cm로. 원점 바닥 중심, 얇은 끝이 +X. 윗면은 '
               'FireDoorWedge_20261007을 편 FireDoorWedgeTop. 충돌 없음.'))


def build_exit_sign(out_root):
    """소형 피난구 유도등 32 x 13.5 cm. 누렇게 바랜 흰 ABS 틀 안에 녹색 아크릴 발광면.

    원점은 벽에 닿는 뒷면 가운데이고 앞이 -Y다. 발광은 면에만 굽는다."""
    ig.reset_scene()
    face = ig.mat_image_uv('ExitSignFace', os.path.join(AI, 'ExitSignFace_20261007.png'), roughness=0.30,
                           emission_strength=3.0)
    frame = ig.mat_plastic('ExitSignFrame', (0.66, 0.64, 0.57), roughness=0.42, bump=0.004)
    width, height, depth = 0.32, 0.135, 0.045
    housing = ig.box('housing', (width, depth, height), location=(0.0, -depth * 0.5, 0.0),
                     bevel=0.006, segments=3, material=frame)
    # 발광면은 틀 앞면에 붙은 아크릴 판이다. 둘레에 틀이 1 cm씩 남는다.
    panel = ig.image_quad('face', (width - 0.02, height - 0.02), (0.0, -depth - 0.0006, 0.0), face,
                          thickness=0.001)
    # 천장 쪽 고정 나사 둘.
    parts = [housing, panel]
    for x in (-0.13, 0.13):
        parts.append(ig.cylinder('screw', 0.0035, 0.002, location=(x, -depth - 0.001, height * 0.5 - 0.006),
                                 rotation=(math.pi * 0.5, 0.0, 0.0), segments=12, material=frame))
    ig.build_asset(
        'SM_ExitSignLamp', 'prop', parts, out_root, collision_parts=[], texture_size=1024,
        mirror_print_uv=True, origin='center',
        notes=('소형 피난구 유도등 32 x 13.5 x 4.5 cm. 원점은 벽에 닿는 뒷면 가운데, 앞 -Y. 발광면은 '
               'SourceArt/AI/ExitSignFace_20261007(gpt-image), 발광 3. 충돌 없음.'))


def main():
    out_root = ig.out_root_from_argv()
    only = sys.argv[sys.argv.index('--') + 2:] if '--' in sys.argv else []
    if not only or 'leaf' in only:
        build_leaf(out_root)
    if not only or 'arm' in only:
        build_arm(out_root)
    if not only or 'prints' in only:
        build_prints(out_root)
    if not only or 'wedge' in only:
        build_wedge(out_root)
    if not only or 'exit' in only:
        build_exit_sign(out_root)


main()
