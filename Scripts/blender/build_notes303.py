"""303호가 403호 현관문 바깥에 붙이는 쪽지 넷(EXPANSION_PLAN §2.3).

- SM_Note303First: 입주 날 이미 붙어 있는 첫 쪽지. 노란 줄 메모지, 위에 투명 테이프 한 줄.
  「새로 오신 분, / 밤에는 발소리 / 조금만 조심해 주세요. / 303호」
- SM_Note303Second: 첫째 낮에 더 붙는 쪽지. 급하게 찢은 공책 종이라 위 가장자리가 찢긴 모양 그대로
  잘려 있고, 위 두 귀퉁이를 테이프로. 「새벽 4시 반에 / 왜 걸어 다니세요? / 다 울려요. / 303호」
- SM_Note303Third: 둘째 낮에 더 붙는 인쇄한 A4. 네 귀퉁이를 테이프로. 「참는 데도 / 한계가 있습니다. / 303호」
- SM_Note303Last: 에필로그에서 붙어 있는 마지막 쪽지. 하늘색 줄 메모지.
  「그동안 / 시끄럽다고만 해서 / 죄송합니다. / 303호」

기준은 Content/SourceArt/AI/Note303{First,Second,Third,Last}_20261007(gpt-image). 글자는 게임의 읽기
화면 문구와 같다.

원점은 403호 문짝 바깥면 가운데 바닥이고 앞이 -Y다(build_neighbor_doors와 같다). X는 문짝 가운데에서
손잡이 쪽이 +다. 문짝(SM_UnitDoorLeafWideL)의 도어스코프가 X 0, Z 1.55에 있고 스테인리스 띠가 X 0.185부터
시작하므로 쪽지는 그 둘을 비켜 둔다. 문 바깥의 배달 자석은 X -0.23, Z 1.32 언저리다.

    blender -b --factory-startup --python Scripts/blender/build_notes303.py -- <out_dir> [first|second|third|last ...]
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
THICKNESS = 0.0001
GAP = 0.0002


def paper_back():
    return ig.mat_plastic('PaperBack', (0.80, 0.80, 0.77), roughness=0.9, bump=0.0)


def tape_material():
    # 투명 테이프. 종이 위에서 조금 번들거리고 누렇게 뜬 정도로만 보인다.
    return ig.mat_plastic('ClearTape', (0.80, 0.79, 0.72), roughness=0.22, bump=0.0)


def backed(ob, back):
    """앞면(-Y)만 인쇄하고 나머지 면은 종이 뒷면 재질로 둔다."""
    ob.data.materials.append(back)
    for face in ob.data.polygons:
        if face.normal.y > -0.5:
            face.material_index = 1
    return ob


def tape(x, z, tilt_deg, material, size=(0.040, 0.016)):
    return ig.box('tape', (size[0], 0.00006, size[1]), (x, -(GAP + THICKNESS + 0.00004), z),
                  rotation=(0.0, math.radians(tilt_deg), 0.0), material=material)


def corner(center, size, tilt_deg, sx, sz, inset=0.004):
    """기운 종이의 귀퉁이 자리. sx, sz는 -1 또는 1."""
    a = math.radians(tilt_deg)
    dx, dz = sx * (size[0] * 0.5 - inset), sz * (size[1] * 0.5 - inset)
    return (center[0] + dx * math.cos(a) + dz * math.sin(a), center[1] - dx * math.sin(a) + dz * math.cos(a))


def flat_note(name, image, size, center, tilt_deg, roughness=0.86):
    ink = ig.mat_image_uv(name, os.path.join(AI, image), roughness=roughness)
    note = ig.image_quad('note', size, (center[0], -(GAP + THICKNESS * 0.5), center[1]), ink,
                         rotation=(0.0, math.radians(tilt_deg), 0.0), thickness=THICKNESS)
    return backed(note, paper_back())


def torn_note(name, image, size, center, tilt_deg, step=8, threshold=0.95):
    """위 가장자리가 찢긴 종이. 그림에서 열마다 종이가 시작하는 높이를 읽어 그 선대로 판을 자른다."""
    path = os.path.join(AI, image)
    img = bpy.data.images.load(path)
    width, height = img.size
    pixels = img.pixels[:]
    channels = img.channels

    def lum(px, py_from_top):
        row = height - 1 - py_from_top
        i = (row * width + px) * channels
        return 0.2126 * pixels[i] + 0.7152 * pixels[i + 1] + 0.0722 * pixels[i + 2]

    tops = []
    for px in list(range(0, width, step)) + [width - 1]:
        top = 0
        for py in range(0, height // 4):
            if lum(px, py) < threshold:
                top = py
                break
        tops.append((px, top))
    ink = ig.mat_image_uv(name, path, roughness=0.86)
    back = paper_back()
    bm = bmesh.new()
    sx, sz = size
    # 앞면(-Y)과 뒷면을 각각 다각형 하나로 두고 옆을 막는다. 위쪽 선은 찢긴 자리를 따라간다.
    outline = [(-sx * 0.5, -sz * 0.5), (sx * 0.5, -sz * 0.5)]
    for px, top in reversed(tops):
        outline.append(((px / (width - 1) - 0.5) * sx, (0.5 - top / (height - 1)) * sz))
    front = [bm.verts.new((x, -THICKNESS * 0.5, z)) for x, z in outline]
    rear = [bm.verts.new((x, THICKNESS * 0.5, z)) for x, z in outline]
    face_front = bm.faces.new(front)
    face_rear = bm.faces.new(list(reversed(rear)))
    count = len(outline)
    for i in range(count):
        j = (i + 1) % count
        bm.faces.new((front[j], front[i], rear[i], rear[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bmesh.ops.triangulate(bm, faces=[face_front, face_rear])
    me = bpy.data.meshes.new('torn')
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new('torn', me)
    bpy.context.collection.objects.link(ob)
    layer = me.uv_layers.new(name='ImageUV')
    for poly in me.polygons:
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            u = co.x / sx + 0.5
            v = co.z / sz + 0.5
            if poly.normal.y > 0.5:
                u = 1.0 - u
            layer.data[li].uv = (u, v)
    me.materials.append(ink)
    ob.location = (center[0], -(GAP + THICKNESS * 0.5), center[1])
    ob.rotation_euler = (0.0, math.radians(tilt_deg), 0.0)
    ig.set_active(ob)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    ig.log(f'{name}: torn outline {len(tops)} points, top band {min(t for _, t in tops)}..{max(t for _, t in tops)} px')
    return backed(ob, back)


def finish(name, parts, notes):
    ig.build_asset(name, 'prop', parts, out_root_global, collision_parts=[], texture_size=1024,
                   mirror_print_uv=True, origin='door-face', notes=notes)


def build_first():
    ig.reset_scene()
    size, center, tilt = (0.10, 0.15), (-0.09, 1.52), -2.0
    parts = [flat_note('Note303First', 'Note303First_20261007.png', size, center, tilt)]
    top = corner(center, size, tilt, 0.0, 1.0, inset=0.002)
    parts.append(tape(top[0], top[1], tilt + 3.0, tape_material(), size=(0.050, 0.018)))
    finish('SM_Note303First', parts,
           '403호 문짝 바깥면 가운데 바닥이 원점, 앞 -Y. 노란 줄 메모지 10 x 15 cm(가운데 X -0.09, Z 1.52), 위 테이프 '
           '한 줄. 원본 SourceArt/AI/Note303First_20261007(gpt-image). 충돌·그림자 없음.')


def build_second():
    ig.reset_scene()
    size, center, tilt = (0.10, 0.15), (0.10, 1.44), 3.0
    parts = [torn_note('Note303Second', 'Note303Second_20261007.png', size, center, tilt)]
    material = tape_material()
    for sx, angle in ((-1.0, 35.0), (1.0, -30.0)):
        x, z = corner(center, size, tilt, sx, 1.0, inset=0.010)
        parts.append(tape(x, z, angle + tilt, material, size=(0.034, 0.015)))
    finish('SM_Note303Second', parts,
           '403호 문짝 바깥면 가운데 바닥이 원점, 앞 -Y. 찢은 공책 종이 10 x 15 cm(가운데 X 0.10, Z 1.44), 위 가장자리는 '
           '찢긴 선대로 자른 판, 위 두 귀퉁이 테이프. 원본 SourceArt/AI/Note303Second_20261007(gpt-image). 충돌·그림자 없음.')


def build_third():
    ig.reset_scene()
    size, center, tilt = (0.21, 0.297), (-0.28, 1.58), -0.8
    parts = [flat_note('Note303Third', 'Note303Third_20261007.png', size, center, tilt, roughness=0.84)]
    material = tape_material()
    for sx, sz, angle in ((-1, 1, 40.0), (1, 1, -40.0), (-1, -1, -40.0), (1, -1, 40.0)):
        x, z = corner(center, size, tilt, sx, sz)
        parts.append(tape(x, z, angle, material, size=(0.034, 0.015)))
    finish('SM_Note303Third', parts,
           '403호 문짝 바깥면 가운데 바닥이 원점, 앞 -Y. 인쇄한 A4(가운데 X -0.28, Z 1.58), 네 귀퉁이 테이프. '
           '원본 SourceArt/AI/Note303Third_20261007(gpt-image). 충돌·그림자 없음.')


def build_last():
    ig.reset_scene()
    size, center, tilt = (0.10, 0.15), (-0.09, 1.52), -1.5
    parts = [flat_note('Note303Last', 'Note303Last_20261007.png', size, center, tilt)]
    top = corner(center, size, tilt, 0.0, 1.0, inset=0.002)
    parts.append(tape(top[0], top[1], tilt - 2.0, tape_material(), size=(0.050, 0.018)))
    finish('SM_Note303Last', parts,
           '403호 문짝 바깥면 가운데 바닥이 원점, 앞 -Y. 하늘색 줄 메모지 10 x 15 cm(가운데 X -0.09, Z 1.52), 위 테이프 '
           '한 줄. 원본 SourceArt/AI/Note303Last_20261007(gpt-image). 충돌·그림자 없음.')


out_root_global = ig.out_root_from_argv()
_only = sys.argv[sys.argv.index('--') + 2:] if '--' in sys.argv else []
for _key, _step in (('first', build_first), ('second', build_second), ('third', build_third), ('last', build_last)):
    if not _only or _key in _only:
        _step()
