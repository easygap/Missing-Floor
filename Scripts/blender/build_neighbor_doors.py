"""2·3층 이웃 문에 붙은 것들. 202호는 방 셋으로 쪼갠 원룸이고, 303호는 교대 근무를 하는 사람의 집이다.

- SM_DoorPrints202: 202호 문의 손글씨 쪽지. 「기사님 죄송해요ㅠ / 202-1, 202-2, 202-3 / 다 다른
  집이에요 / 호수 한 번만 봐 주세요!」 A4 복사용지에 유성 매직, 위 두 모서리를 투명 테이프로.
- SM_Doorbells202: 202호 문 서쪽 벽에 양면테이프로 붙인 무선 초인종 셋. 라벨이 202-1, 202-2, 202-3이다.
- SM_DoorPrints303: 303호 문의 안내문. 「벨 누르지 마세요 / 낮에 자는 사람이 있어요. / 노크도
  하지 말아 주세요. / 택배는 문 앞에 두고 가 주세요.」 A5 가로, 네 모서리를 투명 테이프로.
- SM_DawnDeliveryBag: 밤에 303호 문 옆에 놓인 새벽배송 보냉 가방.

기준은 Content/SourceArt/AI의 Door202Note·Doorbells202·Door303Sign·DawnDeliveryBag(2026-10-07,
gpt-image). 쪽지와 안내문의 글자는 게임의 읽기 화면 문구와 같다. 가방의 앞면과 옆면은 시안 사진을
펴서 그대로 입힌다(Scripts/rectify_concept_faces.py).

문에 붙는 두 장은 원점이 문짝 앞면(Y 0) 가운데 바닥이고 앞이 -Y다(build_door_prints와 같다).
초인종은 원점이 벽면 바닥이고 앞이 -Y다. 가방은 원점이 바닥 중심이고 앞(투명 주머니)이 -Y다.

    blender -b --factory-startup --python Scripts/blender/build_neighbor_doors.py -- <out_dir>
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
PRINTS = os.path.join(HERE, '../../Content/SourceArt/UtilityPrints')


def backed(ob, back):
    """앞면(-Y)만 인쇄하고 나머지 면은 종이 뒷면 재질로 둔다."""
    ob.data.materials.append(back)
    for face in ob.data.polygons:
        if face.normal.y > -0.5:
            face.material_index = 1
    return ob


def clear_tape(name, x, z, tilt_deg, y, material, size=(0.045, 0.018)):
    return ig.box(name, (size[0], 0.00006, size[1]), (x, y, z), rotation=(0.0, math.radians(tilt_deg), 0.0),
                  material=material)


def tape_material():
    # 투명 테이프. 종이 위에서 조금 번들거리고 누렇게 뜬 정도로만 보인다.
    return ig.mat_plastic('ClearTape', (0.80, 0.79, 0.72), roughness=0.22, bump=0.0)


def build_note_202(out_root):
    ig.reset_scene()
    ink = ig.mat_image_uv('Note202', os.path.join(AI, 'Door202Note_20261007.png'), roughness=0.86)
    back = ig.mat_plastic('PaperBack', (0.80, 0.80, 0.77), roughness=0.9, bump=0.0)
    tape = tape_material()
    size = (0.21, 0.297)
    x, z, tilt = -0.17, 1.33, -1.2
    thickness = 0.0001
    note = backed(ig.image_quad('note', size, (x, -(0.0002 + thickness * 0.5), z), ink,
                                rotation=(0.0, math.radians(tilt), 0.0), thickness=thickness), back)
    parts = [note]
    # 위 두 모서리. 쪽지가 기운 만큼 모서리 자리도 같이 돈다(Y축 회전).
    a = math.radians(tilt)
    for side, angle in ((-1.0, 32.0), (1.0, -28.0)):
        dx, dz = side * (size[0] * 0.5 - 0.006), size[1] * 0.5 - 0.004
        tx = x + dx * math.cos(a) + dz * math.sin(a)
        tz = z - dx * math.sin(a) + dz * math.cos(a)
        parts.append(clear_tape('tape', tx, tz, angle + tilt, -(0.0002 + thickness + 0.00004), tape))
    ig.build_asset(
        'SM_DoorPrints202', 'prop', parts, out_root, collision_parts=[], texture_size=1024,
        mirror_print_uv=True, origin='door-face',
        notes=('202호 문짝 앞면 가운데 바닥이 원점, 앞 -Y. A4 손글씨 쪽지(가운데 X -0.17, Z 1.33), 위 두 모서리 '
               '투명 테이프. 원본 SourceArt/AI/Door202Note_20261007(gpt-image). 충돌·그림자 없음.'))


# Doorbells202_20261007.png(1024 x 1536)에서 잰 초인종 셋의 자리(px). 위에서부터 202-1, 202-2, 202-3.
BELL_RECTS = [(369, 57, 652, 507), (369, 527, 651, 977), (370, 996, 651, 1447)]
BELL_IMAGE = (1024, 1536)
# 단추는 몸통 위에서 37%, 가운데. 지름은 몸통 폭의 44%다(사진에서 잰 값).
BUTTON_FROM_TOP = 0.37
BUTTON_DIAMETER_RATIO = 0.44


def planar_front_uv(ob, rect, size, center_xz, layer_name='ImageUV'):
    """앞을 보는 면에 rect(px)를 평면으로 비춘다. size·center_xz는 그 rect가 덮는 실제 폭·높이와 가운데."""
    bpy.context.view_layer.update()
    me = ob.data
    layer = me.uv_layers.get(layer_name) or me.uv_layers.new(name=layer_name)
    x0, y0, x1, y1 = rect
    iw, ih = BELL_IMAGE
    u0, u1 = x0 / iw, x1 / iw
    v0, v1 = 1.0 - y1 / ih, 1.0 - y0 / ih
    for poly in me.polygons:
        for li in poly.loop_indices:
            co = ob.matrix_world @ me.vertices[me.loops[li].vertex_index].co
            s = (co.x - center_xz[0]) / size[0] + 0.5
            t = (co.z - center_xz[1]) / size[1] + 0.5
            layer.data[li].uv = (u0 + (u1 - u0) * s, v0 + (v1 - v0) * t)


def build_doorbells_202(out_root):
    ig.reset_scene()
    face = ig.mat_image_uv('DoorbellFace', os.path.join(AI, 'Doorbells202_20261007.png'), roughness=0.42)
    shell = ig.mat_plastic('DoorbellShell', (0.60, 0.575, 0.50), roughness=0.45, bump=0.004)
    width, height, depth = 0.044, 0.070, 0.016
    gap = 0.008
    top = 1.515
    parts = []
    for index, rect in enumerate(BELL_RECTS):
        cz = top - height * 0.5 - index * (height + gap)
        body = ig.box(f'bell_{index}', (width, depth, height), location=(0.0, -depth * 0.5, cz),
                      bevel=0.006, segments=3, material=shell)
        ig.apply_modifiers(body)
        body.data.materials.append(face)
        for poly in body.data.polygons:
            if poly.normal.y < -0.95:
                poly.material_index = 1
        planar_front_uv(body, rect, (width, height), (0.0, cz))
        parts.append(body)
        # 단추. 앞으로 3 mm 솟은 둥근 머리에 같은 사진을 비춘다.
        radius = width * BUTTON_DIAMETER_RATIO * 0.5
        bz = cz + height * 0.5 - height * BUTTON_FROM_TOP
        button = ig.cylinder(f'button_{index}', radius, 0.003, location=(0.0, -depth - 0.0015, bz),
                             rotation=(math.pi * 0.5, 0.0, 0.0), segments=28, bevel=0.0012, bevel_segments=2,
                             material=shell)
        ig.apply_modifiers(button)
        # 사진은 앞을 보는 머리에만 비춘다. 둘레까지 비추면 사진이 옆으로 늘어진다.
        button.data.materials.append(face)
        for poly in button.data.polygons:
            if poly.normal.y < -0.9:
                poly.material_index = 1
        planar_front_uv(button, rect, (width, height), (0.0, cz))
        parts.append(button)
    ig.build_asset(
        'SM_Doorbells202', 'prop', parts, out_root, collision_parts=[], texture_size=512, mirror_print_uv=True,
        notes=('202호 문 서쪽 벽의 무선 초인종 셋(4.4 x 7 cm, 두께 1.6 cm). 원점은 벽면 바닥, 앞 -Y, 위에서부터 '
               '202-1·202-2·202-3. 앞면은 SourceArt/AI/Doorbells202_20261007(gpt-image)을 그대로 비춘다. 충돌 없음.'))


def build_sign_303(out_root):
    ig.reset_scene()
    ink = ig.mat_image_uv('Sign303', os.path.join(AI, 'Door303Sign_20261007.png'), roughness=0.84)
    back = ig.mat_plastic('PaperBack', (0.80, 0.80, 0.77), roughness=0.9, bump=0.0)
    tape = tape_material()
    size = (0.21, 0.148)
    x, z, tilt = -0.13, 1.40, 0.6
    thickness = 0.0001
    sign = backed(ig.image_quad('sign', size, (x, -(0.0002 + thickness * 0.5), z), ink,
                                rotation=(0.0, math.radians(tilt), 0.0), thickness=thickness), back)
    parts = [sign]
    y = -(0.0002 + thickness + 0.00004)
    for sx, sz, angle in ((-1, 1, 40.0), (1, 1, -40.0), (-1, -1, -40.0), (1, -1, 40.0)):
        parts.append(clear_tape('tape', x + sx * (size[0] * 0.5 - 0.004), z + sz * (size[1] * 0.5 - 0.004),
                                angle, y, tape, size=(0.032, 0.015)))
    ig.build_asset(
        'SM_DoorPrints303', 'prop', parts, out_root, collision_parts=[], texture_size=1024,
        mirror_print_uv=True, origin='door-face',
        notes=('303호 문짝 앞면 가운데 바닥이 원점, 앞 -Y. A5 가로 인쇄 안내문(가운데 X -0.13, Z 1.40), 네 모서리 '
               '투명 테이프. 원본 SourceArt/AI/Door303Sign_20261007(gpt-image). 충돌·그림자 없음.'))


def bag_face_uv(ob, size):
    """면이 보는 방향에 따라 앞·옆·위 사진을 평면으로 비춘다. 재질 번호도 같이 정한다."""
    sx, sy, sz = size
    me = ob.data
    layer = me.uv_layers.new(name='ImageUV')
    for poly in me.polygons:
        n = poly.normal
        axis = max(range(3), key=lambda i: abs(n[i]))
        poly.material_index = {0: 1, 1: 0, 2: 2}[axis]
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            if axis == 1:
                # 앞(-Y)과 뒤(+Y). 뒤는 좌우를 뒤집어 끈이 같은 X에서 이어진다.
                u = co.x / sx + 0.5
                if n.y > 0:
                    u = 1.0 - u
                uv = (u, co.z / sz)
            elif axis == 0:
                u = co.y / sy + 0.5
                if n.x < 0:
                    u = 1.0 - u
                uv = (u, co.z / sz)
            else:
                uv = (co.x / sx + 0.5, co.y / sy + 0.5)
            layer.data[li].uv = uv


def build_dawn_bag(out_root):
    ig.reset_scene()
    front = ig.mat_image_uv('BagFront', os.path.join(PRINTS, 'DawnBagFront.png'), roughness=0.62)
    side = ig.mat_image_uv('BagSide', os.path.join(PRINTS, 'DawnBagSide.png'), roughness=0.62)
    top = ig.mat_image_uv('BagTop', os.path.join(PRINTS, 'DawnBagTop.png'), roughness=0.62)
    webbing = ig.mat_image_uv('BagWebbing', os.path.join(PRINTS, 'DawnBagWebbing.png'), roughness=0.75)
    size = (0.45, 0.30, 0.28)
    body = ig.box('bag', size, origin='bottom', material=front)
    # 면을 잘게 나눠 속이 찬 천 가방처럼 옆이 조금 부풀게 한다. 면 가운데가 가장 많이
    # 나오고 모서리는 그대로다. 뚜껑도 가운데가 조금 솟는다.
    sub = body.modifiers.new('Subdivide', 'SUBSURF')
    sub.subdivision_type = 'SIMPLE'
    sub.levels = 3
    ig.apply_modifiers(body)
    half = (size[0] * 0.5, size[1] * 0.5, size[2] * 0.5)
    for v in body.data.vertices:
        nx, ny = v.co.x / half[0], v.co.y / half[1]
        nz = (v.co.z - half[2]) / half[2]
        v.co.y += 0.011 * ny * (1.0 - nx * nx) * (1.0 - nz * nz)
        v.co.x += 0.008 * nx * (1.0 - ny * ny) * (1.0 - nz * nz)
        v.co.z += 0.007 * max(nz, 0.0) * (1.0 - nx * nx) * (1.0 - ny * ny)
    ig.add_bevel(body, 0.022, segments=4, angle_deg=40.0)
    ig.apply_modifiers(body)
    # 바닥이 Z 0에 앉도록 맞춘다. 부풀린 뒤 바닥이 조금 떴다.
    low = min(v.co.z for v in body.data.vertices)
    for v in body.data.vertices:
        v.co.z -= low
    for material in (side, top):
        body.data.materials.append(material)
    bag_face_uv(body, size)
    parts = [body]
    # 손잡이 둘. 앞면 사진의 끈 자리(X -0.148, +0.026)에서 올라와 뚜껑 위에 눕는다. 가운데가 조금 뜬다.
    height = max(v.co.z for v in body.data.vertices)
    for index, x in enumerate((-0.148, 0.026)):
        path = [(x, -0.152, height - 0.03), (x, -0.150, height + 0.004), (x, -0.07, height + 0.016),
                (x, 0.0, height + 0.022), (x, 0.07, height + 0.016), (x, 0.150, height + 0.004),
                (x, 0.152, height - 0.03)]
        strap = ig.pipe(f'strap_{index}', [(0.0, p[1], p[2]) for p in path], radius=0.0034, resolution=10,
                        corner_radius=0.02, material=webbing)
        strap.scale = (5.0, 1.0, 1.0)
        ig.set_active(strap)
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        strap.location.x = x
        layer = strap.data.uv_layers.new(name='ImageUV')
        for poly in strap.data.polygons:
            for li in poly.loop_indices:
                co = strap.data.vertices[strap.data.loops[li].vertex_index].co
                layer.data[li].uv = (co.x / 0.034 + 0.5, (co.y + 0.16) / 0.32)
        parts.append(strap)
    return ig.build_asset(
        'SM_DawnDeliveryBag', 'prop', parts, out_root, collision_parts=[[body]], texture_size=1024,
        notes=('새벽배송 보냉 가방 45 x 30 x 28 cm. 원점 바닥 중심, 투명 주머니가 있는 앞면이 -Y. 앞·옆은 '
               'DawnDeliveryBag_20261007 시안을 편 사진, 손잡이 둘은 뚜껑 위에 눕는다. 충돌은 몸통 하나.'))


def main():
    out_root = ig.out_root_from_argv()
    build_note_202(out_root)
    build_doorbells_202(out_root)
    build_sign_303(out_root)
    build_dawn_bag(out_root)


main()
